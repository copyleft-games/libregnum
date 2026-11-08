/* lrg-joint-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgJointComponent: an LrgJoint between the body of its entity and the
 * body of a target entity (or the world), built lazily.
 *
 * The component is a physics client with the lowest priority, so in every
 * pre_step the bodies of the frame were already (re)built.  It then:
 *
 *   1. drops a live joint whose bodies changed (a body rebuilt gets a new
 *      id, and removing a body removes its constraints);
 *   2. builds the joint when both bodies exist: the target is the entity
 *      given, or the entity with target-guid (looked up every step until
 *      found), or the fixed world.  The authored anchor and axis are in the
 *      entity's local space and are turned into world space with the
 *      entity's pose at that moment: the joint is at rest in the pose the
 *      bodies have when it is built.
 *
 * post_step polls the joint's broken state and emits ::broken once.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-joint-component.h"
#include "lrg-rigid-body-component.h"
#include "lrg-collider-component3d.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../lrg-world3d-physics-private.h"
#include "../../core/lrg-math3d.h"
#include "../../lrg-log.h"

struct _LrgJointComponent
{
	LrgComponent      parent_instance;

	/* Settings */
	LrgJointKind      kind;
	LrgEntity3D      *target;          /* weak */
	gchar            *target_guid;
	gfloat            anchor[3];
	gfloat            axis[3];
	gboolean          limits_enabled;
	gfloat            limit_min;
	gfloat            limit_max;
	gfloat            spring_frequency;
	gfloat            spring_damping;
	gfloat            motor_velocity;
	gfloat            motor_max_force;
	gfloat            break_force;

	/* Runtime */
	LrgWorld3D       *world;
	LrgDynamicsWorld *physics;
	LrgJoint         *joint;           /* owned, in @physics */
	guint             body_a;
	guint             body_b;
	gboolean          broken;
};

G_DEFINE_FINAL_TYPE (LrgJointComponent, lrg_joint_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_KIND,
	PROP_TARGET,
	PROP_TARGET_GUID,
	PROP_ANCHOR_X,
	PROP_ANCHOR_Y,
	PROP_ANCHOR_Z,
	PROP_AXIS_X,
	PROP_AXIS_Y,
	PROP_AXIS_Z,
	PROP_LIMITS_ENABLED,
	PROP_LIMIT_MIN,
	PROP_LIMIT_MAX,
	PROP_SPRING_FREQUENCY,
	PROP_SPRING_DAMPING,
	PROP_MOTOR_VELOCITY,
	PROP_MOTOR_MAX_FORCE,
	PROP_BREAK_FORCE,
	N_PROPS
};

enum
{
	SIGNAL_BROKEN,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Joint lifetime
 * ========================================================================== */

static void
drop_joint (LrgJointComponent *self)
{
	if (self->joint != NULL)
	{
		if (self->physics != NULL && lrg_physics_constraint_get_in_world (LRG_PHYSICS_CONSTRAINT (self->joint)))
			lrg_dynamics_world_remove_constraint (self->physics, LRG_PHYSICS_CONSTRAINT (self->joint));
		g_clear_object (&self->joint);
	}
	self->body_a = LRG_PHYSICS_NO_BODY;
	self->body_b = LRG_PHYSICS_NO_BODY;
}

/* Settings changed: rebuild on the next pre_step (keeps the broken state). */
static void
settings_changed (LrgJointComponent *self)
{
	drop_joint (self);
}

/*
 * entity_body:
 *
 * The body an entity takes part in: its rigid body's, else the static
 * body of its colliders.  LRG_PHYSICS_NO_BODY while it has none.
 */
static guint
entity_body (LrgEntity3D *entity)
{
	LrgComponent         *rb = lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT);
	g_autoptr(GPtrArray)  colliders = NULL;
	guint                 i;

	if (rb != NULL)
		return lrg_rigid_body_component_get_body_id (LRG_RIGID_BODY_COMPONENT (rb));
	colliders = lrg_entity3d_get_components_of_type (entity, LRG_TYPE_COLLIDER_COMPONENT3D);
	for (i = 0; i < colliders->len; i++)
	{
		LrgColliderComponent3D *c = g_ptr_array_index (colliders, i);

		if (!lrg_collider_component3d_get_trigger (c))
			return lrg_collider_component3d_get_body_id (c);
	}
	return LRG_PHYSICS_NO_BODY;
}

/* The target entity: the one given, else the GUID lookup (may be NULL). */
static LrgEntity3D *
resolve_target (LrgJointComponent *self)
{
	LrgEntity3D *found;

	if (self->target != NULL)
		return self->target;
	if (self->target_guid == NULL || self->world == NULL)
		return NULL;
	found = lrg_world3d_find_by_guid (self->world, self->target_guid);
	if (found != NULL)
	{
		self->target = found;
		g_object_add_weak_pointer (G_OBJECT (found), (gpointer *) &self->target);
	}
	return found;
}

/* The authored local frame in world space. */
static void
world_frame (LrgJointComponent *self,
             LrgEntity3D       *entity,
             gfloat            *anchor,
             gfloat            *axis)
{
	gfloat rot[4];

	lrg_entity3d_transform_point (entity, self->anchor, anchor);
	lrg_entity3d_get_world_rotation (entity, rot);
	lrg_math3d_quat_rotate_vector (rot, self->axis, axis);
}

/* Builds the LrgJoint for the current kind and settings. */
static LrgJoint *
create_joint (LrgJointComponent *self,
              LrgEntity3D       *entity,
              LrgEntity3D       *target,
              GError           **error)
{
	LrgDynamicsWorld *w = self->physics;
	gfloat            anchor[3];
	gfloat            axis[3];
	gfloat            other[3];
	gfloat            limit;
	LrgJoint         *joint = NULL;

	world_frame (self, entity, anchor, axis);
	limit = self->limits_enabled ? self->limit_max : (gfloat) G_PI;

	switch (self->kind)
	{
	case LRG_JOINT_FIXED:
		joint = lrg_joint_new_fixed (w, self->body_a, self->body_b, error);
		break;
	case LRG_JOINT_POINT:
		joint = lrg_joint_new_point (w, self->body_a, self->body_b, anchor, error);
		break;
	case LRG_JOINT_HINGE:
		joint = lrg_joint_new_hinge (w, self->body_a, self->body_b, anchor, axis, error);
		break;
	case LRG_JOINT_SLIDER:
		joint = lrg_joint_new_slider (w, self->body_a, self->body_b, anchor, axis, error);
		break;
	case LRG_JOINT_DISTANCE:
		/* the other anchor: the target's origin, or the world point above */
		if (target != NULL)
			lrg_entity3d_get_world_position (target, other);
		else
			memcpy (other, anchor, sizeof (other));
		joint = lrg_joint_new_distance (w, self->body_a, self->body_b, anchor, other, error);
		break;
	case LRG_JOINT_CONE:
		joint = lrg_joint_new_cone (w, self->body_a, self->body_b, anchor, axis,
		                            CLAMP (limit, 0.0f, (gfloat) G_PI), error);
		break;
	case LRG_JOINT_SWING_TWIST:
		{
			gfloat twist = self->limits_enabled ? MIN (fabsf (self->limit_min), (gfloat) G_PI) : (gfloat) G_PI;

			joint = lrg_joint_new_swing_twist (w, self->body_a, self->body_b, anchor, axis, NULL,
			                                   CLAMP (limit, 0.0f, (gfloat) G_PI),
			                                   CLAMP (limit, 0.0f, (gfloat) G_PI), -twist, twist, error);
		}
		break;
	case LRG_JOINT_SIX_DOF:
	default:
		joint = lrg_joint_new_six_dof (w, self->body_a, self->body_b, anchor, axis, NULL, error);
		break;
	}
	if (joint == NULL)
		return NULL;

	/* Generic settings on the joint's main axis. */
	if (self->limits_enabled)
	{
		if (self->kind == LRG_JOINT_HINGE)
			lrg_joint_set_limits (joint, LRG_JOINT_AXIS_ROTATION_X, self->limit_min, self->limit_max);
		else if (self->kind == LRG_JOINT_SLIDER)
			lrg_joint_set_limits (joint, LRG_JOINT_AXIS_TRANSLATION_X, self->limit_min, self->limit_max);
		else if (self->kind == LRG_JOINT_DISTANCE)
			lrg_joint_set_distance_range (joint, MAX (self->limit_min, 0.0f),
			                              MAX (self->limit_max, MAX (self->limit_min, 0.0f)));
	}
	if (self->spring_frequency > 0.0f &&
	    (self->kind == LRG_JOINT_SLIDER || self->kind == LRG_JOINT_DISTANCE))
		lrg_joint_set_limit_spring (joint, LRG_JOINT_AXIS_TRANSLATION_X, self->spring_frequency,
		                            self->spring_damping);
	if (self->motor_max_force > 0.0f &&
	    (self->kind == LRG_JOINT_HINGE || self->kind == LRG_JOINT_SLIDER))
	{
		LrgJointAxis motor_axis = self->kind == LRG_JOINT_HINGE ? LRG_JOINT_AXIS_ROTATION_X
		                                                        : LRG_JOINT_AXIS_TRANSLATION_X;

		lrg_joint_set_motor (joint, motor_axis, LRG_JOINT_MOTOR_VELOCITY, self->motor_max_force);
		lrg_joint_set_motor_target_velocity (joint, motor_axis, self->motor_velocity);
	}
	if (self->break_force > 0.0f)
		lrg_joint_set_break_force (joint, self->break_force);
	return joint;
}

/* ==========================================================================
 * Physics client hooks
 * ========================================================================== */

static void
client_physics_changed (GObject          *client,
                        LrgDynamicsWorld *old_physics,
                        LrgDynamicsWorld *new_physics)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (client);

	(void) old_physics;
	drop_joint (self);
	self->physics = new_physics;
}

static void
client_pre_step (GObject          *client,
                 LrgDynamicsWorld *physics,
                 gfloat            dt)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (client);
	LrgEntity3D       *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	LrgEntity3D       *target;
	guint              a;
	guint              b;
	gboolean           waiting;
	g_autoptr(GError)  error = NULL;

	(void) dt;
	self->physics = physics;
	if (entity == NULL || self->broken)
		return;
	if (!lrg_component_get_enabled (LRG_COMPONENT (self)) || !lrg_entity3d_is_active_in_hierarchy (entity))
	{
		drop_joint (self);
		return;
	}

	/* Current bodies of both sides.  Note LRG_DYNAMICS_WORLD_BODY and
	 * LRG_PHYSICS_NO_BODY are the same value: "waiting" is tracked apart. */
	a = entity_body (entity);
	target = resolve_target (self);
	waiting = (a == LRG_PHYSICS_NO_BODY);
	if (target != NULL)
	{
		b = entity_body (target);
		waiting = waiting || b == LRG_PHYSICS_NO_BODY;
	}
	else
	{
		b = LRG_DYNAMICS_WORLD_BODY;
		waiting = waiting || self->target_guid != NULL;   /* not instantiated yet */
	}

	/* A live joint whose bodies changed (or were removed) is stale. */
	if (self->joint != NULL &&
	    (a != self->body_a || b != self->body_b ||
	     !lrg_physics_constraint_get_in_world (LRG_PHYSICS_CONSTRAINT (self->joint))))
		drop_joint (self);
	if (self->joint != NULL || waiting || a == b)
		return;

	self->body_a = a;
	self->body_b = b;
	self->joint = create_joint (self, entity, target, &error);
	if (self->joint == NULL || !lrg_dynamics_world_add_constraint (physics, LRG_PHYSICS_CONSTRAINT (self->joint),
	                                                               NULL, &error))
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D, "entity '%s': cannot build the joint: %s",
		             lrg_entity3d_get_name (entity) != NULL ? lrg_entity3d_get_name (entity) : "?",
		             error != NULL ? error->message : "unknown");
		g_clear_object (&self->joint);
		self->broken = TRUE;   /* do not retry every step */
	}
}

static void
client_post_step (GObject          *client,
                  LrgDynamicsWorld *physics,
                  gfloat            dt)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (client);

	(void) physics;
	(void) dt;
	if (self->joint != NULL && !self->broken && lrg_joint_is_broken (self->joint))
	{
		self->broken = TRUE;
		g_signal_emit (self, signals[SIGNAL_BROKEN], 0);
	}
}

static const LrgPhysicsClientClass joint_client = {
	client_physics_changed,
	client_pre_step,
	client_post_step,
	NULL
};

/* ==========================================================================
 * LrgComponent hooks
 * ========================================================================== */

static void
lrg_joint_component_world_entered (LrgComponent *component,
                                   LrgWorld3D   *world)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (component);

	self->world = world;
	_lrg_world3d_physics_add_client (world, G_OBJECT (self), LRG_PHYSICS_CLIENT_JOINT, &joint_client);
}

static void
lrg_joint_component_world_exited (LrgComponent *component,
                                  LrgWorld3D   *world)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (component);

	drop_joint (self);
	_lrg_world3d_physics_remove_client (world, G_OBJECT (self));
	self->world = NULL;
	self->physics = NULL;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
clear_target (LrgJointComponent *self)
{
	if (self->target != NULL)
		g_object_remove_weak_pointer (G_OBJECT (self->target), (gpointer *) &self->target);
	self->target = NULL;
}

static void
lrg_joint_component_dispose (GObject *object)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (object);

	drop_joint (self);
	clear_target (self);
	G_OBJECT_CLASS (lrg_joint_component_parent_class)->dispose (object);
}

static void
lrg_joint_component_finalize (GObject *object)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (object);

	g_free (self->target_guid);
	G_OBJECT_CLASS (lrg_joint_component_parent_class)->finalize (object);
}

/* Float property storage by id. */
static gfloat *
float_field (LrgJointComponent *self,
             guint              prop_id)
{
	switch (prop_id)
	{
	case PROP_ANCHOR_X: return &self->anchor[0];
	case PROP_ANCHOR_Y: return &self->anchor[1];
	case PROP_ANCHOR_Z: return &self->anchor[2];
	case PROP_AXIS_X: return &self->axis[0];
	case PROP_AXIS_Y: return &self->axis[1];
	case PROP_AXIS_Z: return &self->axis[2];
	case PROP_LIMIT_MIN: return &self->limit_min;
	case PROP_LIMIT_MAX: return &self->limit_max;
	case PROP_SPRING_FREQUENCY: return &self->spring_frequency;
	case PROP_SPRING_DAMPING: return &self->spring_damping;
	case PROP_MOTOR_VELOCITY: return &self->motor_velocity;
	case PROP_MOTOR_MAX_FORCE: return &self->motor_max_force;
	case PROP_BREAK_FORCE: return &self->break_force;
	default: return NULL;
	}
}

static void
set_float (LrgJointComponent *self,
           guint              prop_id,
           gfloat             value)
{
	gfloat *field = float_field (self, prop_id);

	if (field == NULL || *field == value)
		return;
	*field = value;
	settings_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop_id]);
}

static void
lrg_joint_component_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (object);
	gfloat            *field;

	switch (prop_id)
	{
	case PROP_KIND:
		g_value_set_enum (value, (gint) self->kind);
		return;
	case PROP_TARGET:
		g_value_set_object (value, self->target);
		return;
	case PROP_TARGET_GUID:
		g_value_set_string (value, self->target_guid);
		return;
	case PROP_LIMITS_ENABLED:
		g_value_set_boolean (value, self->limits_enabled);
		return;
	default:
		break;
	}
	field = float_field (self, prop_id);
	if (field != NULL)
		g_value_set_float (value, *field);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_joint_component_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgJointComponent *self = LRG_JOINT_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_KIND:
		lrg_joint_component_set_kind (self, (LrgJointKind) g_value_get_enum (value));
		return;
	case PROP_TARGET:
		lrg_joint_component_set_target (self, (LrgEntity3D *) g_value_get_object (value));
		return;
	case PROP_TARGET_GUID:
		lrg_joint_component_set_target_guid (self, g_value_get_string (value));
		return;
	case PROP_LIMITS_ENABLED:
		lrg_joint_component_set_limits (self, g_value_get_boolean (value), self->limit_min, self->limit_max);
		return;
	default:
		break;
	}
	if (float_field (self, prop_id) != NULL)
		set_float (self, prop_id, g_value_get_float (value));
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_joint_component_class_init (LrgJointComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_joint_component_dispose;
	object_class->finalize = lrg_joint_component_finalize;
	object_class->get_property = lrg_joint_component_get_property;
	object_class->set_property = lrg_joint_component_set_property;

	component_class->world_entered = lrg_joint_component_world_entered;
	component_class->world_exited = lrg_joint_component_world_exited;

	/**
	 * LrgJointComponent:kind:
	 *
	 * The joint kind.
	 */
	properties[PROP_KIND] =
		g_param_spec_enum ("kind", NULL, "Joint kind", LRG_TYPE_JOINT_KIND, LRG_JOINT_POINT, rw);

	/**
	 * LrgJointComponent:target:
	 *
	 * The other entity (weak), %NULL for the world or an unresolved GUID.
	 */
	properties[PROP_TARGET] =
		g_param_spec_object ("target", NULL, "Target entity", LRG_TYPE_ENTITY3D, rw);

	/**
	 * LrgJointComponent:target-guid:
	 *
	 * GUID of the target entity, %NULL for the world.
	 */
	properties[PROP_TARGET_GUID] = g_param_spec_string ("target-guid", NULL, "Target GUID", NULL, rw);

	/**
	 * LrgJointComponent:anchor-x:
	 *
	 * Anchor X in the entity's local space.
	 */
	properties[PROP_ANCHOR_X] = g_param_spec_float ("anchor-x", NULL, "Anchor X", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:anchor-y:
	 *
	 * Anchor Y in the entity's local space.
	 */
	properties[PROP_ANCHOR_Y] = g_param_spec_float ("anchor-y", NULL, "Anchor Y", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:anchor-z:
	 *
	 * Anchor Z in the entity's local space.
	 */
	properties[PROP_ANCHOR_Z] = g_param_spec_float ("anchor-z", NULL, "Anchor Z", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:axis-x:
	 *
	 * Axis X in the entity's local space.
	 */
	properties[PROP_AXIS_X] = g_param_spec_float ("axis-x", NULL, "Axis X", -1.0f, 1.0f, 0.0f, rw);

	/**
	 * LrgJointComponent:axis-y:
	 *
	 * Axis Y in the entity's local space.
	 */
	properties[PROP_AXIS_Y] = g_param_spec_float ("axis-y", NULL, "Axis Y", -1.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgJointComponent:axis-z:
	 *
	 * Axis Z in the entity's local space.
	 */
	properties[PROP_AXIS_Z] = g_param_spec_float ("axis-z", NULL, "Axis Z", -1.0f, 1.0f, 0.0f, rw);

	/**
	 * LrgJointComponent:limits-enabled:
	 *
	 * Whether the limits apply.
	 */
	properties[PROP_LIMITS_ENABLED] = g_param_spec_boolean ("limits-enabled", NULL, "Limits", FALSE, rw);

	/**
	 * LrgJointComponent:limit-min:
	 *
	 * Lower limit.
	 */
	properties[PROP_LIMIT_MIN] =
		g_param_spec_float ("limit-min", NULL, "Lower limit", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:limit-max:
	 *
	 * Upper limit.
	 */
	properties[PROP_LIMIT_MAX] =
		g_param_spec_float ("limit-max", NULL, "Upper limit", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:spring-frequency:
	 *
	 * Soft-limit spring frequency in Hz, 0 for rigid.
	 */
	properties[PROP_SPRING_FREQUENCY] =
		g_param_spec_float ("spring-frequency", NULL, "Spring Hz", 0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:spring-damping:
	 *
	 * Soft-limit damping ratio.
	 */
	properties[PROP_SPRING_DAMPING] =
		g_param_spec_float ("spring-damping", NULL, "Spring damping", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgJointComponent:motor-velocity:
	 *
	 * Velocity motor target.
	 */
	properties[PROP_MOTOR_VELOCITY] =
		g_param_spec_float ("motor-velocity", NULL, "Motor velocity", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:motor-max-force:
	 *
	 * Motor force cap, 0 for off.
	 */
	properties[PROP_MOTOR_MAX_FORCE] =
		g_param_spec_float ("motor-max-force", NULL, "Motor max force", 0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgJointComponent:break-force:
	 *
	 * Force above which the joint breaks, 0 for never.
	 */
	properties[PROP_BREAK_FORCE] =
		g_param_spec_float ("break-force", NULL, "Break force", 0.0f, G_MAXFLOAT, 0.0f, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgJointComponent::broken:
	 * @self: the joint component
	 *
	 * Emitted once, after the physics step in which the joint's load
	 * passed #LrgJointComponent:break-force.
	 */
	signals[SIGNAL_BROKEN] =
		g_signal_new ("broken", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
		              NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_joint_component_init (LrgJointComponent *self)
{
	self->kind = LRG_JOINT_POINT;
	self->axis[1] = 1.0f;
	self->spring_damping = 1.0f;
	self->body_a = LRG_PHYSICS_NO_BODY;
	self->body_b = LRG_PHYSICS_NO_BODY;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgJointComponent *
lrg_joint_component_new (LrgJointKind kind)
{
	LrgJointComponent *self = g_object_new (LRG_TYPE_JOINT_COMPONENT, NULL);

	self->kind = kind;
	return self;
}

LrgJointKind
lrg_joint_component_get_kind (LrgJointComponent *self)
{
	g_return_val_if_fail (LRG_IS_JOINT_COMPONENT (self), LRG_JOINT_POINT);
	return self->kind;
}

void
lrg_joint_component_set_kind (LrgJointComponent *self,
                              LrgJointKind       kind)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	if (self->kind == kind)
		return;
	self->kind = kind;
	settings_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KIND]);
}

LrgEntity3D *
lrg_joint_component_get_target (LrgJointComponent *self)
{
	g_return_val_if_fail (LRG_IS_JOINT_COMPONENT (self), NULL);
	return self->target;
}

void
lrg_joint_component_set_target (LrgJointComponent *self,
                                LrgEntity3D       *target)
{
	const gchar *guid;

	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	g_return_if_fail (target == NULL || LRG_IS_ENTITY3D (target));

	if (self->target == target)
		return;
	clear_target (self);
	if (target != NULL)
	{
		self->target = target;
		g_object_add_weak_pointer (G_OBJECT (target), (gpointer *) &self->target);
	}
	guid = target != NULL ? lrg_entity3d_ensure_guid (target) : NULL;
	if (g_strcmp0 (guid, self->target_guid) != 0)
	{
		g_free (self->target_guid);
		self->target_guid = g_strdup (guid);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_GUID]);
	}
	settings_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET]);
}

const gchar *
lrg_joint_component_get_target_guid (LrgJointComponent *self)
{
	g_return_val_if_fail (LRG_IS_JOINT_COMPONENT (self), NULL);
	return self->target_guid;
}

void
lrg_joint_component_set_target_guid (LrgJointComponent *self,
                                     const gchar       *guid)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));

	if (guid != NULL && *guid == '\0')
		guid = NULL;
	if (g_strcmp0 (guid, self->target_guid) == 0)
		return;
	g_free (self->target_guid);
	self->target_guid = g_strdup (guid);
	clear_target (self);   /* resolved again by GUID */
	settings_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_GUID]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET]);
}

void
lrg_joint_component_get_anchor (LrgJointComponent *self,
                                gfloat            *out)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->anchor, sizeof (self->anchor));
}

void
lrg_joint_component_set_anchor (LrgJointComponent *self,
                                gfloat             x,
                                gfloat             y,
                                gfloat             z)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	set_float (self, PROP_ANCHOR_X, x);
	set_float (self, PROP_ANCHOR_Y, y);
	set_float (self, PROP_ANCHOR_Z, z);
}

void
lrg_joint_component_get_axis (LrgJointComponent *self,
                              gfloat            *out)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->axis, sizeof (self->axis));
}

void
lrg_joint_component_set_axis (LrgJointComponent *self,
                              gfloat             x,
                              gfloat             y,
                              gfloat             z)
{
	gfloat len;

	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));

	len = sqrtf (x * x + y * y + z * z);
	if (!(len > 1e-6f))
		return;
	set_float (self, PROP_AXIS_X, x / len);
	set_float (self, PROP_AXIS_Y, y / len);
	set_float (self, PROP_AXIS_Z, z / len);
}

gboolean
lrg_joint_component_get_limits (LrgJointComponent *self,
                                gfloat            *min,
                                gfloat            *max)
{
	g_return_val_if_fail (LRG_IS_JOINT_COMPONENT (self), FALSE);
	if (min != NULL)
		*min = self->limit_min;
	if (max != NULL)
		*max = self->limit_max;
	return self->limits_enabled;
}

void
lrg_joint_component_set_limits (LrgJointComponent *self,
                                gboolean           enabled,
                                gfloat             min,
                                gfloat             max)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));

	enabled = !!enabled;
	if (self->limits_enabled != enabled)
	{
		self->limits_enabled = enabled;
		settings_changed (self);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIMITS_ENABLED]);
	}
	set_float (self, PROP_LIMIT_MIN, min);
	set_float (self, PROP_LIMIT_MAX, max);
}

void
lrg_joint_component_set_spring (LrgJointComponent *self,
                                gfloat             frequency,
                                gfloat             damping)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	set_float (self, PROP_SPRING_FREQUENCY, MAX (frequency, 0.0f));
	set_float (self, PROP_SPRING_DAMPING, MAX (damping, 0.0f));
}

void
lrg_joint_component_get_spring (LrgJointComponent *self,
                                gfloat            *frequency,
                                gfloat            *damping)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	if (frequency != NULL)
		*frequency = self->spring_frequency;
	if (damping != NULL)
		*damping = self->spring_damping;
}

void
lrg_joint_component_set_motor (LrgJointComponent *self,
                               gfloat             velocity,
                               gfloat             max_force)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	set_float (self, PROP_MOTOR_VELOCITY, velocity);
	set_float (self, PROP_MOTOR_MAX_FORCE, MAX (max_force, 0.0f));
}

void
lrg_joint_component_get_motor (LrgJointComponent *self,
                               gfloat            *velocity,
                               gfloat            *max_force)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	if (velocity != NULL)
		*velocity = self->motor_velocity;
	if (max_force != NULL)
		*max_force = self->motor_max_force;
}

gfloat
lrg_joint_component_get_break_force (LrgJointComponent *self)
{
	g_return_val_if_fail (LRG_IS_JOINT_COMPONENT (self), 0.0f);
	return self->break_force;
}

void
lrg_joint_component_set_break_force (LrgJointComponent *self,
                                     gfloat             force)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	set_float (self, PROP_BREAK_FORCE, MAX (force, 0.0f));
}

LrgJoint *
lrg_joint_component_get_joint (LrgJointComponent *self)
{
	g_return_val_if_fail (LRG_IS_JOINT_COMPONENT (self), NULL);
	return self->broken ? NULL : self->joint;
}

gboolean
lrg_joint_component_get_broken (LrgJointComponent *self)
{
	g_return_val_if_fail (LRG_IS_JOINT_COMPONENT (self), FALSE);
	return self->broken;
}

void
lrg_joint_component_rebuild (LrgJointComponent *self)
{
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (self));
	drop_joint (self);
	self->broken = FALSE;
}
