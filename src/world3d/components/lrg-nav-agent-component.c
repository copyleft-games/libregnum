/* lrg-nav-agent-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A navigation agent on an entity.  The component owns an LrgNavAgent,
 * puts it in the world's crowd while the entity is in a world, and syncs
 * it with the entity:
 *
 *   fixed_update (before the crowd step):  entity -> agent (teleports,
 *                                          or a dynamic body's motion)
 *   LrgNavCrowd::stepped (after the step): agent -> entity (position and
 *                                          facing, or body velocity)
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-nav-agent-component.h"
#include "lrg-rigid-body-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../../core/lrg-math3d.h"
#include "../../pathfinding/lrg-nav-agent-private.h"

#define TELEPORT_EPSILON (1e-3f)

struct _LrgNavAgentComponent
{
	LrgComponent  parent_instance;

	LrgNavAgent  *agent;          /* owned */
	LrgNavCrowd  *crowd;          /* owned ref while in a world */
	gulong        stepped_id;

	gfloat        base_offset;
	gboolean      update_rotation;
	gfloat        angular_speed;  /* degrees per second */
	gfloat        yaw;            /* radians, about +Y, 0 faces +Z */

	gboolean      has_pending;
	gfloat        pending[3];
	gchar        *target_node;    /* nullable: an entity name that wins */

	gboolean      has_written;
	gfloat        written[3];     /* the entity position we wrote last */
};

G_DEFINE_FINAL_TYPE (LrgNavAgentComponent, lrg_nav_agent_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_SPEED,
	PROP_ACCELERATION,
	PROP_RADIUS,
	PROP_HEIGHT,
	PROP_AVOIDANCE_PRIORITY,
	PROP_AVOIDANCE_ENABLED,
	PROP_STOPPING_DISTANCE,
	PROP_BASE_OFFSET,
	PROP_UPDATE_ROTATION,
	PROP_ANGULAR_SPEED,
	PROP_DESTINATION_X,
	PROP_DESTINATION_Y,
	PROP_DESTINATION_Z,
	PROP_TARGET_NODE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_ARRIVED,
	SIGNAL_PATH_FAILED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* the agent property behind a component property, or NULL */
static const gchar *
agent_property (guint prop_id)
{
	switch (prop_id)
	{
	case PROP_SPEED:              return "max-speed";
	case PROP_ACCELERATION:       return "max-acceleration";
	case PROP_RADIUS:             return "radius";
	case PROP_HEIGHT:             return "height";
	case PROP_AVOIDANCE_PRIORITY: return "avoidance-priority";
	case PROP_AVOIDANCE_ENABLED:  return "avoidance-enabled";
	case PROP_STOPPING_DISTANCE:  return "stopping-distance";
	default:                      return NULL;
	}
}

/* ==========================================================================
 * Sync
 * ========================================================================== */

static LrgRigidBodyComponent *
dynamic_body (LrgNavAgentComponent *self)
{
	LrgEntity3D  *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	LrgComponent *rb;

	if (entity == NULL)
		return NULL;
	rb = lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT);
	if (rb == NULL || !lrg_component_get_enabled (rb) ||
	    lrg_rigid_body_component_get_body_kind (LRG_RIGID_BODY_COMPONENT (rb)) != LRG_DYNAMICS_BODY_DYNAMIC)
		return NULL;
	return LRG_RIGID_BODY_COMPONENT (rb);
}

/* the agent's feet for the entity's pivot */
static void
entity_feet (LrgNavAgentComponent *self,
             LrgEntity3D          *entity,
             gfloat               *out)
{
	lrg_entity3d_get_world_position (entity, out);
	out[1] -= self->base_offset;
}

static void
read_yaw (LrgNavAgentComponent *self,
          LrgEntity3D          *entity)
{
	gfloat q[4];
	gfloat fx;
	gfloat fz;

	lrg_entity3d_get_world_rotation (entity, q);
	/* the rotated +Z axis */
	fx = 2.0f * (q[0] * q[2] + q[3] * q[1]);
	fz = 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]);
	self->yaw = atan2f (fx, fz);
}

static void resolve_target (LrgNavAgentComponent *self);

/* entity -> agent: called before the crowd steps */
static void
lrg_nav_agent_component_fixed_update (LrgComponent *component,
                                      gfloat        step)
{
	LrgNavAgentComponent *self = LRG_NAV_AGENT_COMPONENT (component);
	LrgEntity3D          *entity = lrg_component_get_entity (component);
	gfloat                pos[3];

	(void) step;
	if (entity == NULL || self->crowd == NULL)
		return;

	/* A named target wins, even before it exists: the numeric pending
	 * destination stays stored and is not planned. A missing name does
	 * not skip the rest of the update. */
	if (self->target_node != NULL && self->target_node[0] != '\0')
		resolve_target (self);
	else if (self->has_pending && lrg_nav_agent_get_mesh (self->agent) != NULL)
	{
		self->has_pending = FALSE;
		if (!lrg_nav_agent_set_destination (self->agent, self->pending))
			return;
	}

	lrg_entity3d_get_world_position (entity, pos);
	if (dynamic_body (self) != NULL)
	{
		gfloat feet[3];

		/* the body carries the agent */
		entity_feet (self, entity, feet);
		_lrg_nav_agent_follow_position (self->agent, feet);
		return;
	}
	if (self->has_written &&
	    (fabsf (pos[0] - self->written[0]) > TELEPORT_EPSILON ||
	     fabsf (pos[1] - self->written[1]) > TELEPORT_EPSILON ||
	     fabsf (pos[2] - self->written[2]) > TELEPORT_EPSILON))
	{
		gfloat feet[3];

		/* game code moved the entity: the agent jumps there */
		entity_feet (self, entity, feet);
		lrg_nav_agent_warp (self->agent, feet);
		self->has_written = FALSE;
	}
}

/* agent -> entity: after every crowd step */
static void
on_crowd_stepped (LrgNavCrowd *crowd,
                  gfloat       delta,
                  gpointer     user_data)
{
	LrgNavAgentComponent  *self = LRG_NAV_AGENT_COMPONENT (user_data);
	LrgEntity3D           *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	LrgRigidBodyComponent *body;
	gfloat                 pos[3];
	gfloat                 vel[3];

	(void) crowd;
	if (entity == NULL || !lrg_component_get_enabled (LRG_COMPONENT (self)))
		return;
	lrg_nav_agent_get_velocity (self->agent, vel);

	body = dynamic_body (self);
	if (body != NULL)
	{
		gfloat bv[3];

		/* steer the body: horizontal velocity from the agent, vertical its own */
		lrg_rigid_body_component_get_linear_velocity (body, bv);
		bv[0] = vel[0];
		bv[2] = vel[2];
		lrg_rigid_body_component_set_linear_velocity (body, bv);
	}
	else
	{
		lrg_nav_agent_get_position (self->agent, pos);
		pos[1] += self->base_offset;
		lrg_entity3d_set_world_position (entity, pos);
		lrg_entity3d_get_world_position (entity, self->written);
		self->has_written = TRUE;
	}

	/* face the velocity, turning at most angular-speed */
	if (self->update_rotation && vel[0] * vel[0] + vel[2] * vel[2] > 0.01f)
	{
		static const gfloat up[3] = { 0.0f, 1.0f, 0.0f };
		gfloat              target = atan2f (vel[0], vel[2]);
		gfloat              diff = target - self->yaw;
		gfloat              max_turn = self->angular_speed * (gfloat) G_PI / 180.0f * delta;
		gfloat              q[4];

		while (diff > (gfloat) G_PI)
			diff -= 2.0f * (gfloat) G_PI;
		while (diff < -(gfloat) G_PI)
			diff += 2.0f * (gfloat) G_PI;
		if (self->angular_speed > 0.0f)
			diff = CLAMP (diff, -max_turn, max_turn);
		self->yaw += diff;
		lrg_math3d_quat_from_axis_angle (up, self->yaw, q);
		lrg_entity3d_set_rotation (entity, q);
	}
}

/* ==========================================================================
 * World membership
 * ========================================================================== */

static void
lrg_nav_agent_component_world_entered (LrgComponent *component,
                                       LrgWorld3D   *world)
{
	LrgNavAgentComponent *self = LRG_NAV_AGENT_COMPONENT (component);
	LrgEntity3D          *entity = lrg_component_get_entity (component);
	gfloat                feet[3];

	if (self->crowd != NULL)
		return;
	self->crowd = g_object_ref (lrg_world3d_get_nav_crowd (world));
	lrg_nav_crowd_add_agent (self->crowd, self->agent);
	self->stepped_id = g_signal_connect (self->crowd, "stepped", G_CALLBACK (on_crowd_stepped), self);
	if (entity != NULL)
	{
		entity_feet (self, entity, feet);
		lrg_nav_agent_warp (self->agent, feet);
		read_yaw (self, entity);
	}
	self->has_written = FALSE;
	if (self->target_node != NULL && self->target_node[0] != '\0')
		resolve_target (self);
	else if (self->has_pending && lrg_nav_agent_get_mesh (self->agent) != NULL)
	{
		self->has_pending = FALSE;
		lrg_nav_agent_set_destination (self->agent, self->pending);
	}
}

static void
lrg_nav_agent_component_world_exited (LrgComponent *component,
                                      LrgWorld3D   *world)
{
	LrgNavAgentComponent *self = LRG_NAV_AGENT_COMPONENT (component);

	(void) world;
	if (self->crowd == NULL)
		return;
	g_clear_signal_handler (&self->stepped_id, self->crowd);
	lrg_nav_crowd_remove_agent (self->crowd, self->agent);
	g_clear_object (&self->crowd);
	lrg_nav_agent_set_mesh (self->agent, NULL);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
forward_arrived (LrgNavAgent *agent,
                 gpointer     user_data)
{
	(void) agent;
	g_signal_emit (user_data, signals[SIGNAL_ARRIVED], 0);
}

static void
forward_path_failed (LrgNavAgent *agent,
                     gpointer     user_data)
{
	(void) agent;
	g_signal_emit (user_data, signals[SIGNAL_PATH_FAILED], 0);
}

static void
lrg_nav_agent_component_dispose (GObject *object)
{
	LrgNavAgentComponent *self = LRG_NAV_AGENT_COMPONENT (object);

	if (self->crowd != NULL)
	{
		g_clear_signal_handler (&self->stepped_id, self->crowd);
		lrg_nav_crowd_remove_agent (self->crowd, self->agent);
		g_clear_object (&self->crowd);
	}
	if (self->agent != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->agent, self);
		g_clear_object (&self->agent);
	}
	g_clear_pointer (&self->target_node, g_free);
	G_OBJECT_CLASS (lrg_nav_agent_component_parent_class)->dispose (object);
}

static void
lrg_nav_agent_component_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
	LrgNavAgentComponent *self = LRG_NAV_AGENT_COMPONENT (object);
	const gchar          *name = agent_property (prop_id);

	if (name != NULL)
	{
		g_object_get_property (G_OBJECT (self->agent), name, value);
		return;
	}
	switch (prop_id)
	{
	case PROP_BASE_OFFSET:     g_value_set_float (value, self->base_offset); break;
	case PROP_UPDATE_ROTATION: g_value_set_boolean (value, self->update_rotation); break;
	case PROP_ANGULAR_SPEED:   g_value_set_float (value, self->angular_speed); break;
	case PROP_DESTINATION_X:
	case PROP_DESTINATION_Y:
	case PROP_DESTINATION_Z:
	{
		gfloat dest[3] = { 0.0f, 0.0f, 0.0f };

		lrg_nav_agent_component_get_destination (self, dest);
		g_value_set_float (value, dest[prop_id - PROP_DESTINATION_X]);
		break;
	}
	case PROP_TARGET_NODE:
		g_value_set_string (value, self->target_node);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_nav_agent_component_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
	LrgNavAgentComponent *self = LRG_NAV_AGENT_COMPONENT (object);
	const gchar          *name = agent_property (prop_id);

	if (name != NULL)
	{
		GValue old = G_VALUE_INIT;

		g_value_init (&old, G_VALUE_TYPE (value));
		g_object_get_property (G_OBJECT (self->agent), name, &old);
		if (g_param_values_cmp (pspec, &old, value) != 0)
		{
			g_object_set_property (G_OBJECT (self->agent), name, value);
			g_object_notify_by_pspec (object, pspec);
		}
		g_value_unset (&old);
		return;
	}
	switch (prop_id)
	{
	case PROP_BASE_OFFSET:
		if (self->base_offset != g_value_get_float (value))
		{
			self->base_offset = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_UPDATE_ROTATION:
		if (self->update_rotation != g_value_get_boolean (value))
		{
			self->update_rotation = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_ANGULAR_SPEED:
		if (self->angular_speed != g_value_get_float (value))
		{
			self->angular_speed = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_DESTINATION_X:
	case PROP_DESTINATION_Y:
	case PROP_DESTINATION_Z:
	{
		gfloat dest[3] = { 0.0f, 0.0f, 0.0f };
		gfloat v = g_value_get_float (value);
		guint  axis = prop_id - PROP_DESTINATION_X;

		lrg_nav_agent_component_get_destination (self, dest);
		if (dest[axis] != v)
		{
			dest[axis] = v;
			lrg_nav_agent_component_set_destination (self, dest);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	}
	case PROP_TARGET_NODE:
	{
		const gchar *s = g_value_get_string (value);
		gchar       *copy = (s != NULL && s[0] != '\0') ? g_strdup (s) : NULL;
		LrgEntity3D *entity;

		if (g_strcmp0 (self->target_node, copy) == 0)
		{
			g_free (copy);
			break;
		}
		g_free (self->target_node);
		self->target_node = copy;
		g_object_notify_by_pspec (object, pspec);
		entity = lrg_component_get_entity (LRG_COMPONENT (self));
		if (entity != NULL && lrg_entity3d_get_world (entity) != NULL)
			resolve_target (self);
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static GParamSpec *
comp_float (const gchar *name,
            const gchar *blurb,
            gfloat       min,
            gfloat       max,
            gfloat       def)
{
	return g_param_spec_float (name, NULL, blurb, min, max, def,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_nav_agent_component_class_init (LrgNavAgentComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->dispose = lrg_nav_agent_component_dispose;
	object_class->get_property = lrg_nav_agent_component_get_property;
	object_class->set_property = lrg_nav_agent_component_set_property;

	component_class->world_entered = lrg_nav_agent_component_world_entered;
	component_class->world_exited = lrg_nav_agent_component_world_exited;
	component_class->fixed_update = lrg_nav_agent_component_fixed_update;

	/**
	 * LrgNavAgentComponent:speed:
	 *
	 * Top speed in metres per second (#LrgNavAgent:max-speed).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_SPEED] = comp_float ("speed", "Speed", 0.0f, 1000.0f, 3.5f);

	/**
	 * LrgNavAgentComponent:acceleration:
	 *
	 * Most velocity change per second (#LrgNavAgent:max-acceleration).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_ACCELERATION] = comp_float ("acceleration", "Acceleration", 0.01f, 10000.0f, 8.0f);

	/**
	 * LrgNavAgentComponent:radius:
	 *
	 * The avoidance radius (#LrgNavAgent:radius).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_RADIUS] = comp_float ("radius", "Radius", 0.01f, 100.0f, 0.3f);

	/**
	 * LrgNavAgentComponent:height:
	 *
	 * The agent's height (#LrgNavAgent:height).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_HEIGHT] = comp_float ("height", "Height", 0.01f, 100.0f, 1.8f);

	/**
	 * LrgNavAgentComponent:avoidance-priority:
	 *
	 * 0 .. 1, higher gives way less (#LrgNavAgent:avoidance-priority).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_AVOIDANCE_PRIORITY] = comp_float ("avoidance-priority", "Avoidance priority", 0.0f, 1.0f, 0.5f);

	/**
	 * LrgNavAgentComponent:avoidance-enabled:
	 *
	 * Whether the agent avoids others (#LrgNavAgent:avoidance-enabled).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_AVOIDANCE_ENABLED] =
		g_param_spec_boolean ("avoidance-enabled", NULL, "Avoid others", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNavAgentComponent:stopping-distance:
	 *
	 * How close counts as arrived (#LrgNavAgent:stopping-distance).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_STOPPING_DISTANCE] = comp_float ("stopping-distance", "Stopping distance", 0.0f, 1000.0f, 0.1f);

	/**
	 * LrgNavAgentComponent:base-offset:
	 *
	 * Height of the entity's pivot above the agent's feet (a capsule
	 * centred on its pivot: half its height).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_BASE_OFFSET] = comp_float ("base-offset", "Base offset", -100.0f, 100.0f, 0.0f);

	/**
	 * LrgNavAgentComponent:update-rotation:
	 *
	 * Whether the entity turns (about +Y, +Z forward) to face its
	 * velocity.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_UPDATE_ROTATION] =
		g_param_spec_boolean ("update-rotation", NULL, "Face the velocity", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNavAgentComponent:angular-speed:
	 *
	 * Fastest turn in degrees per second (0 turns at once).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_ANGULAR_SPEED] = comp_float ("angular-speed", "Angular speed", 0.0f, 100000.0f, 720.0f);

	/**
	 * LrgNavAgentComponent:destination-x:
	 *
	 * World X of the destination. Setting one axis keeps the other two
	 * (or zero when no destination is stored yet).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_DESTINATION_X] = comp_float ("destination-x", "Destination X", -1.0e8f, 1.0e8f, 0.0f);

	/**
	 * LrgNavAgentComponent:destination-y:
	 *
	 * World Y of the destination.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_DESTINATION_Y] = comp_float ("destination-y", "Destination Y", -1.0e8f, 1.0e8f, 0.0f);

	/**
	 * LrgNavAgentComponent:destination-z:
	 *
	 * World Z of the destination.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_DESTINATION_Z] = comp_float ("destination-z", "Destination Z", -1.0e8f, 1.0e8f, 0.0f);

	/**
	 * LrgNavAgentComponent:target-node:
	 *
	 * Name of an entity in the same world. While set, the agent walks
	 * to that entity's local position and a numeric destination is not
	 * planned, even when the name is not in the world yet.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_TARGET_NODE] =
		g_param_spec_string ("target-node", NULL, "Entity to walk to", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgNavAgentComponent::arrived:
	 * @component: the component
	 *
	 * The agent reached its destination (after the crowd step).
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_ARRIVED] = g_signal_new ("arrived", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                                        0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgNavAgentComponent::path-failed:
	 * @component: the component
	 *
	 * No path to the destination exists on the world's mesh.
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_PATH_FAILED] = g_signal_new ("path-failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                                            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_nav_agent_component_init (LrgNavAgentComponent *self)
{
	self->agent = lrg_nav_agent_new ();
	self->update_rotation = TRUE;
	self->angular_speed = 720.0f;
	g_signal_connect (self->agent, "arrived", G_CALLBACK (forward_arrived), self);
	g_signal_connect (self->agent, "path-failed", G_CALLBACK (forward_path_failed), self);
}

LrgNavAgentComponent *
lrg_nav_agent_component_new (void)
{
	return g_object_new (LRG_TYPE_NAV_AGENT_COMPONENT, NULL);
}

/* ==========================================================================
 * API
 * ========================================================================== */

LrgNavAgent *
lrg_nav_agent_component_get_agent (LrgNavAgentComponent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self), NULL);
	return self->agent;
}

gboolean
lrg_nav_agent_component_set_destination (LrgNavAgentComponent *self,
                                         const gfloat         *destination)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self), FALSE);
	g_return_val_if_fail (destination != NULL, FALSE);

	if (self->crowd == NULL || lrg_nav_agent_get_mesh (self->agent) == NULL)
	{
		/* planned when the entity is in a world with a mesh */
		memcpy (self->pending, destination, sizeof (self->pending));
		self->has_pending = TRUE;
		return TRUE;
	}
	self->has_pending = FALSE;
	return lrg_nav_agent_set_destination (self->agent, destination);
}

gboolean
lrg_nav_agent_component_get_destination (LrgNavAgentComponent *self,
                                         gfloat               *out)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self), FALSE);

	if (self->has_pending)
	{
		if (out != NULL)
			memcpy (out, self->pending, sizeof (self->pending));
		return TRUE;
	}
	return lrg_nav_agent_get_destination (self->agent, out);
}

/* Walk to target-node's local position when that entity is in the world. */
static void
resolve_target (LrgNavAgentComponent *self)
{
	LrgEntity3D *entity;
	LrgWorld3D  *world;
	LrgEntity3D *target;
	gfloat       pos[3];
	gfloat       cur[3];
	gboolean     have;

	if (self->target_node == NULL || self->target_node[0] == '\0')
		return;
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	world = entity != NULL ? lrg_entity3d_get_world (entity) : NULL;
	target = world != NULL ? lrg_world3d_find_by_name (world, self->target_node) : NULL;
	if (target == NULL)
		return;

	lrg_entity3d_get_position (target, pos);
	have = lrg_nav_agent_component_get_destination (self, cur);
	if (have && cur[0] == pos[0] && cur[1] == pos[1] && cur[2] == pos[2])
		return;
	if (!lrg_nav_agent_component_set_destination (self, pos))
		return;
	if (properties[PROP_DESTINATION_X] != NULL)
	{
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESTINATION_X]);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESTINATION_Y]);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESTINATION_Z]);
	}
}

void
lrg_nav_agent_component_stop (LrgNavAgentComponent *self)
{
	g_return_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self));

	self->has_pending = FALSE;
	lrg_nav_agent_stop (self->agent);
}

LrgNavAgentState
lrg_nav_agent_component_get_state (LrgNavAgentComponent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self), LRG_NAV_AGENT_STATE_IDLE);
	return lrg_nav_agent_get_state (self->agent);
}

gboolean
lrg_nav_agent_component_has_arrived (LrgNavAgentComponent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self), FALSE);
	return lrg_nav_agent_get_state (self->agent) == LRG_NAV_AGENT_STATE_ARRIVED;
}

gfloat
lrg_nav_agent_component_get_remaining_distance (LrgNavAgentComponent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self), 0.0f);
	return lrg_nav_agent_get_remaining_distance (self->agent);
}

#define COMPONENT_FLOAT_ACCESSORS(getter, setter, prop)                        \
gfloat                                                                         \
getter (LrgNavAgentComponent *self)                                            \
{                                                                              \
	gfloat v = 0.0f;                                                           \
                                                                               \
	g_return_val_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self), 0.0f);            \
	g_object_get (self, prop, &v, NULL);                                       \
	return v;                                                                  \
}                                                                              \
                                                                               \
void                                                                           \
setter (LrgNavAgentComponent *self,                                            \
        gfloat                value)                                           \
{                                                                              \
	GParamSpec *ps;                                                            \
	GParamSpecFloat *pf;                                                         \
                                                                               \
	g_return_if_fail (LRG_IS_NAV_AGENT_COMPONENT (self));                      \
	ps = g_object_class_find_property (G_OBJECT_GET_CLASS (self), prop);       \
	pf = G_PARAM_SPEC_FLOAT (ps);                                              \
	g_object_set (self, prop, CLAMP (value, pf->minimum, pf->maximum), NULL);  \
}

COMPONENT_FLOAT_ACCESSORS (lrg_nav_agent_component_get_speed, lrg_nav_agent_component_set_speed, "speed")
COMPONENT_FLOAT_ACCESSORS (lrg_nav_agent_component_get_acceleration, lrg_nav_agent_component_set_acceleration,
                           "acceleration")
COMPONENT_FLOAT_ACCESSORS (lrg_nav_agent_component_get_radius, lrg_nav_agent_component_set_radius, "radius")
COMPONENT_FLOAT_ACCESSORS (lrg_nav_agent_component_get_height, lrg_nav_agent_component_set_height, "height")
COMPONENT_FLOAT_ACCESSORS (lrg_nav_agent_component_get_avoidance_priority,
                           lrg_nav_agent_component_set_avoidance_priority, "avoidance-priority")
