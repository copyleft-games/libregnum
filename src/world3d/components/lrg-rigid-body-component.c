/* lrg-rigid-body-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRigidBodyComponent and the body backend the physics components share
 * (see lrg-world3d-physics-private.h).
 *
 * Per fixed substep (driven by LrgWorld3D):
 *
 *   pre_step   build the body when missing or dirty (colliders / settings
 *              changed, entity scale changed), then entity -> body:
 *                static     teleport when game code moved the entity
 *                kinematic  move to the entity pose over the step
 *                dynamic    teleport when game code moved the entity
 *   (physics step)
 *   post_step  dynamic: body -> entity, keep the last two poses for
 *              interpolation; contact begin / end -> collision signals
 *   interpolate (once per frame) dynamic: entity = blend (prev, curr, alpha)
 *
 * "Game code moved the entity" is detected with the entity's world
 * version: every write the component makes itself is followed by
 * remembering the new version.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-rigid-body-component.h"
#include "lrg-collider-component3d.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../lrg-world3d-physics-private.h"
#include "../../core/lrg-math3d.h"
#include "../../lrg-log.h"

struct _LrgRigidBodyComponent
{
	LrgComponent         parent_instance;

	/* Settings */
	LrgDynamicsBodyKind  kind;
	gfloat               mass;
	gfloat               friction;
	gfloat               restitution;
	gfloat               linear_damping;
	gfloat               angular_damping;
	gfloat               gravity_factor;
	gboolean             ccd;
	gchar               *layer;
	gboolean             interpolate;

	/* Velocities given before the body exists */
	gfloat               pending_linear[3];
	gfloat               pending_angular[3];
	gboolean             has_pending_velocity;

	/* Runtime */
	LrgWorld3D          *world;
	LrgDynamicsWorld    *physics;
	guint                body_id;
	gboolean             dirty;
	gfloat               built_scale[3];
	guint64              synced_version;
	gfloat               prev_pos[3];
	gfloat               prev_rot[4];
	gfloat               curr_pos[3];
	gfloat               curr_rot[4];
	gboolean             have_pose;
	GArray              *touching;     /* guint body ids in contact last step */
	GPtrArray           *touching_entities; /* LrgEntity3D, owned refs */
};

G_DEFINE_FINAL_TYPE (LrgRigidBodyComponent, lrg_rigid_body_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_BODY_KIND,
	PROP_MASS,
	PROP_FRICTION,
	PROP_RESTITUTION,
	PROP_LINEAR_DAMPING,
	PROP_ANGULAR_DAMPING,
	PROP_GRAVITY_FACTOR,
	PROP_CCD,
	PROP_LAYER,
	PROP_INTERPOLATE,
	N_PROPS
};

enum
{
	SIGNAL_COLLISION_ENTERED,
	SIGNAL_COLLISION_EXITED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Body backend (shared with the collider and joint components)
 * ========================================================================== */

void
_lrg_physics_body_desc_init (LrgPhysicsBodyDesc *desc)
{
	memset (desc, 0, sizeof (*desc));
	desc->kind = LRG_DYNAMICS_BODY_STATIC;
	desc->mass = 1.0f;
	desc->friction = 0.5f;
	desc->restitution = 0.0f;
	desc->linear_damping = 0.05f;
	desc->angular_damping = 0.05f;
	desc->gravity_factor = 1.0f;
}

/* The entity's world pose without scale. */
static void
entity_pose (LrgEntity3D *entity,
             gfloat      *position,
             gfloat      *rotation)
{
	lrg_entity3d_get_world_position (entity, position);
	lrg_entity3d_get_world_rotation (entity, rotation);
}

/*
 * resolve_layer:
 *
 * A layer name (or decimal index) through the physics world's layer table;
 * %NULL, an unknown name or a world without a table is layer 0.
 */
static guint
resolve_layer (LrgDynamicsWorld *physics,
               const gchar      *layer)
{
	LrgPhysicsLayers  *layers;
	g_autoptr(GError)  error = NULL;
	gint               index;

	if (layer == NULL)
		return 0;
	layers = lrg_dynamics_world_get_layers (physics);
	if (layers == NULL)
	{
		gchar  *end = NULL;
		guint64 v = g_ascii_strtoull (layer, &end, 10);

		if (end != layer && *end == '\0' && v < LRG_PHYSICS_LAYERS_MAX)
			return (guint) v;
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D, "physics layer '%s': the world has no layer table", layer);
		return 0;
	}
	index = lrg_physics_layers_resolve (layers, layer, &error);
	if (index < 0)
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D, "physics layer '%s': %s", layer,
		             error != NULL ? error->message : "unknown");
		return 0;
	}
	return (guint) index;
}

guint
_lrg_physics_body_create (LrgWorld3D               *world,
                          LrgDynamicsWorld         *physics,
                          LrgEntity3D              *entity,
                          LrgPhysicsShape          *shape,
                          const LrgPhysicsBodyDesc *desc)
{
	g_autoptr(GError) error = NULL;
	gfloat            pos[3];
	gfloat            rot[4];
	guint             id = LRG_PHYSICS_NO_BODY;
	LrgDynamicsBodyKind kind = desc->kind;

	/* Shapes without a volume cannot be dynamic: degrade to kinematic. */
	if (kind == LRG_DYNAMICS_BODY_DYNAMIC && !lrg_physics_shape_is_convex (shape) &&
	    lrg_physics_shape_get_kind (shape) != LRG_PHYSICS_SHAPE_KIND_COMPOUND)
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D, "entity '%s': a %s shape cannot be dynamic; using kinematic",
		             lrg_entity3d_get_name (entity) != NULL ? lrg_entity3d_get_name (entity) : "?",
		             lrg_physics_shape_get_kind (shape) == LRG_PHYSICS_SHAPE_KIND_PLANE ? "plane" : "mesh");
		kind = LRG_DYNAMICS_BODY_KINEMATIC;
	}

	entity_pose (entity, pos, rot);
	if (!lrg_dynamics_world_add_body (physics, shape, kind,
	                                  kind == LRG_DYNAMICS_BODY_DYNAMIC ? desc->mass : 0.0f,
	                                  pos, rot, &id, &error))
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D, "entity '%s': cannot add a body: %s",
		             lrg_entity3d_get_name (entity) != NULL ? lrg_entity3d_get_name (entity) : "?",
		             error->message);
		return LRG_PHYSICS_NO_BODY;
	}

	/* Settings; each call only fails on a bad id, which cannot happen. */
	lrg_dynamics_world_set_body_layer (physics, id, resolve_layer (physics, desc->layer),
	                                   LRG_PHYSICS_LAYERS_ALL, NULL);
	lrg_dynamics_world_set_body_material (physics, id, desc->friction, desc->restitution, NULL);
	lrg_dynamics_world_set_damping (physics, id, desc->linear_damping, desc->angular_damping, NULL);
	lrg_dynamics_world_set_gravity_factor (physics, id, desc->gravity_factor, NULL);
	if (desc->ccd && kind == LRG_DYNAMICS_BODY_DYNAMIC)
		lrg_dynamics_world_set_ccd (physics, id, TRUE, NULL);
	if (desc->sensor)
		lrg_dynamics_world_set_sensor (physics, id, TRUE, NULL);
	lrg_dynamics_world_set_user_data (physics, id, (guint64) (guintptr) entity);

	if (world != NULL)
		_lrg_world3d_physics_map_body (world, id, entity);
	return id;
}

void
_lrg_physics_body_destroy (LrgWorld3D       *world,
                           LrgDynamicsWorld *physics,
                           guint             body_id)
{
	if (body_id == LRG_PHYSICS_NO_BODY)
		return;
	if (physics != NULL)
		lrg_dynamics_world_remove_body (physics, body_id, NULL);
	if (world != NULL)
		_lrg_world3d_physics_unmap_body (world, body_id);
}

void
_lrg_physics_body_teleport (LrgDynamicsWorld *physics,
                            guint             body_id,
                            LrgEntity3D      *entity)
{
	gfloat pos[3];
	gfloat rot[4];

	entity_pose (entity, pos, rot);
	lrg_dynamics_world_set_pose (physics, body_id, pos, rot, NULL);
}

void
_lrg_physics_body_move_kinematic (LrgDynamicsWorld *physics,
                                  guint             body_id,
                                  LrgEntity3D      *entity,
                                  gfloat            dt)
{
	gfloat pos[3];
	gfloat rot[4];

	if (!(dt > 0.0f))
		return;
	entity_pose (entity, pos, rot);
	lrg_dynamics_world_move_kinematic (physics, body_id, pos, rot, dt, NULL);
}

gboolean
_lrg_physics_body_get_pose (LrgDynamicsWorld *physics,
                            guint             body_id,
                            gfloat           *position,
                            gfloat           *rotation)
{
	return lrg_dynamics_world_get_position (physics, body_id, position, NULL) &&
	       lrg_dynamics_world_get_orientation (physics, body_id, rotation, NULL);
}

void
_lrg_physics_sensor_overlaps (LrgDynamicsWorld *physics,
                              guint             sensor_id,
                              GArray           *out)
{
	g_autoptr(GArray) ids = lrg_dynamics_world_get_sensor_overlaps (physics, sensor_id);

	if (ids != NULL && ids->len > 0)
		g_array_append_vals (out, ids->data, ids->len);
}

LrgPhysicsShape *
_lrg_physics_build_shape (LrgEntity3D *entity,
                          GPtrArray   *colliders)
{
	gfloat                     scale[3];
	g_autoptr(LrgPhysicsShape) compound = NULL;
	guint                      i;

	if (colliders == NULL || colliders->len == 0)
		return NULL;
	lrg_entity3d_get_world_scale (entity, scale);

	/* One collider without an offset or rotation: its shape as is. */
	if (colliders->len == 1)
	{
		LrgColliderComponent3D *c = g_ptr_array_index (colliders, 0);
		gfloat                  pos[3];
		gfloat                  rot[4];

		lrg_collider_component3d_get_local_transform (c, scale, pos, rot);
		if (pos[0] == 0.0f && pos[1] == 0.0f && pos[2] == 0.0f &&
		    rot[0] == 0.0f && rot[1] == 0.0f && rot[2] == 0.0f)
			return lrg_collider_component3d_build_shape (c, scale);
	}

	/* Otherwise a compound in attach order (deterministic child ids). */
	compound = lrg_physics_shape_new_compound ();
	for (i = 0; i < colliders->len; i++)
	{
		LrgColliderComponent3D    *c = g_ptr_array_index (colliders, i);
		g_autoptr(LrgPhysicsShape) child = lrg_collider_component3d_build_shape (c, scale);
		gfloat                     pos[3];
		gfloat                     rot[4];

		if (child == NULL)
			continue;
		lrg_collider_component3d_get_local_transform (c, scale, pos, rot);
		lrg_physics_shape_compound_add_child (compound, child, pos, rot);
	}
	if (lrg_physics_shape_get_child_count (compound) == 0)
		return NULL;
	return g_steal_pointer (&compound);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* The non-trigger, enabled colliders on the entity. */
static GPtrArray *
body_colliders (LrgEntity3D *entity)
{
	g_autoptr(GPtrArray) all = lrg_entity3d_get_components_of_type (entity, LRG_TYPE_COLLIDER_COMPONENT3D);
	GPtrArray           *out = g_ptr_array_new ();
	guint                i;

	for (i = 0; i < all->len; i++)
	{
		LrgColliderComponent3D *c = g_ptr_array_index (all, i);

		if (!lrg_collider_component3d_get_trigger (c) && lrg_component_get_enabled (LRG_COMPONENT (c)))
			g_ptr_array_add (out, c);
	}
	return out;
}

static void
mark_dirty (LrgRigidBodyComponent *self)
{
	self->dirty = TRUE;
}

void
_lrg_rigid_body_component_colliders_changed (LrgEntity3D *entity)
{
	LrgComponent *rb;

	if (entity == NULL)
		return;
	rb = lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT);
	if (rb != NULL)
		mark_dirty (LRG_RIGID_BODY_COMPONENT (rb));
}

/* Emits collision-exited for every remembered contact and forgets them. */
static void
clear_contacts (LrgRigidBodyComponent *self,
                gboolean               emit)
{
	g_autoptr(GPtrArray) gone = self->touching_entities;
	guint                i;

	self->touching_entities = g_ptr_array_new_with_free_func (g_object_unref);
	g_array_set_size (self->touching, 0);
	for (i = 0; emit && i < gone->len; i++)
		g_signal_emit (self, signals[SIGNAL_COLLISION_EXITED], 0, g_ptr_array_index (gone, i));
}

static void
drop_body (LrgRigidBodyComponent *self,
           LrgDynamicsWorld      *physics)
{
	if (self->body_id != LRG_PHYSICS_NO_BODY && physics != NULL && self->world != NULL)
		_lrg_physics_body_destroy (self->world, physics, self->body_id);
	self->body_id = LRG_PHYSICS_NO_BODY;
	self->have_pose = FALSE;
	self->dirty = TRUE;
}

static void
fill_desc (LrgRigidBodyComponent *self,
           LrgPhysicsBodyDesc    *desc)
{
	_lrg_physics_body_desc_init (desc);
	desc->kind = self->kind;
	desc->mass = self->mass;
	desc->friction = self->friction;
	desc->restitution = self->restitution;
	desc->linear_damping = self->linear_damping;
	desc->angular_damping = self->angular_damping;
	desc->gravity_factor = self->gravity_factor;
	desc->ccd = self->ccd;
	desc->layer = self->layer;
}

/* Remembers the entity pose as both interpolation states. */
static void
reset_interpolation (LrgRigidBodyComponent *self,
                     LrgEntity3D           *entity)
{
	entity_pose (entity, self->curr_pos, self->curr_rot);
	memcpy (self->prev_pos, self->curr_pos, sizeof (self->curr_pos));
	memcpy (self->prev_rot, self->curr_rot, sizeof (self->curr_rot));
	self->have_pose = TRUE;
}

/*
 * build_body:
 *
 * (Re)creates the body from the colliders, keeping the velocity of a body
 * being rebuilt and applying velocities given before it existed.
 */
static void
build_body (LrgRigidBodyComponent *self,
            LrgEntity3D           *entity,
            LrgDynamicsWorld      *physics)
{
	g_autoptr(GPtrArray)       colliders = body_colliders (entity);
	g_autoptr(LrgPhysicsShape) shape = NULL;
	LrgPhysicsBodyDesc         desc;
	gfloat                     lin[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                     ang[3] = { 0.0f, 0.0f, 0.0f };
	gboolean                   keep = FALSE;

	if (self->body_id != LRG_PHYSICS_NO_BODY)
	{
		keep = lrg_dynamics_world_get_linear_velocity (physics, self->body_id, lin, NULL) &&
		       lrg_dynamics_world_get_angular_velocity (physics, self->body_id, ang, NULL);
		drop_body (self, physics);
	}

	lrg_entity3d_get_world_scale (entity, self->built_scale);
	shape = _lrg_physics_build_shape (entity, colliders);
	self->dirty = FALSE;
	if (shape == NULL)
		return;

	fill_desc (self, &desc);
	self->body_id = _lrg_physics_body_create (self->world, physics, entity, shape, &desc);
	if (self->body_id == LRG_PHYSICS_NO_BODY)
		return;

	if (self->has_pending_velocity)
	{
		memcpy (lin, self->pending_linear, sizeof (lin));
		memcpy (ang, self->pending_angular, sizeof (ang));
		keep = TRUE;
		self->has_pending_velocity = FALSE;
	}
	if (keep && self->kind == LRG_DYNAMICS_BODY_DYNAMIC)
	{
		lrg_dynamics_world_set_linear_velocity (physics, self->body_id, lin, NULL);
		lrg_dynamics_world_set_angular_velocity (physics, self->body_id, ang, NULL);
	}
	reset_interpolation (self, entity);
	self->synced_version = lrg_entity3d_get_world_version (entity);
}

/* TRUE when the entity's world scale differs from the built one. */
static gboolean
scale_changed (LrgRigidBodyComponent *self,
               LrgEntity3D           *entity)
{
	gfloat s[3];
	guint  k;

	lrg_entity3d_get_world_scale (entity, s);
	for (k = 0; k < 3; k++)
		if (fabsf (s[k] - self->built_scale[k]) > 1e-5f * MAX (1.0f, fabsf (s[k])))
			return TRUE;
	return FALSE;
}

/* Writes a pose to the entity and remembers the resulting version. */
static void
write_entity (LrgRigidBodyComponent *self,
              LrgEntity3D           *entity,
              const gfloat          *position,
              const gfloat          *rotation)
{
	lrg_entity3d_set_world_position (entity, position);
	lrg_entity3d_set_world_rotation (entity, rotation);
	self->synced_version = lrg_entity3d_get_world_version (entity);
}

/* ==========================================================================
 * Physics client hooks
 * ========================================================================== */

static void
client_physics_changed (GObject          *client,
                        LrgDynamicsWorld *old_physics,
                        LrgDynamicsWorld *new_physics)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (client);

	drop_body (self, old_physics);
	clear_contacts (self, TRUE);
	self->physics = new_physics;
}

static void
client_pre_step (GObject          *client,
                 LrgDynamicsWorld *physics,
                 gfloat            dt)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (client);
	LrgEntity3D           *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	gboolean               moved;

	if (entity == NULL)
		return;

	/* A disabled component or an inactive entity has no body. */
	if (!lrg_component_get_enabled (LRG_COMPONENT (self)) || !lrg_entity3d_is_active_in_hierarchy (entity))
	{
		if (self->body_id != LRG_PHYSICS_NO_BODY)
			drop_body (self, physics);
		clear_contacts (self, TRUE);
		return;
	}

	if (self->body_id == LRG_PHYSICS_NO_BODY || self->dirty || scale_changed (self, entity))
	{
		build_body (self, entity, physics);
		return;
	}

	moved = lrg_entity3d_get_world_version (entity) != self->synced_version;
	switch (self->kind)
	{
	case LRG_DYNAMICS_BODY_KINEMATIC:
		/* every step: an unmoved entity gives the body zero velocity */
		_lrg_physics_body_move_kinematic (physics, self->body_id, entity, dt);
		break;
	case LRG_DYNAMICS_BODY_STATIC:
	case LRG_DYNAMICS_BODY_DYNAMIC:
	default:
		if (moved)
		{
			_lrg_physics_body_teleport (physics, self->body_id, entity);
			reset_interpolation (self, entity);
		}
		break;
	}
	self->synced_version = lrg_entity3d_get_world_version (entity);
}

/* Contact begin / end, once per other entity, stable order. */
static void
update_contacts (LrgRigidBodyComponent *self,
                 LrgEntity3D           *entity,
                 LrgDynamicsWorld      *physics)
{
	g_autoptr(GArray)    bodies = g_array_new (FALSE, FALSE, sizeof (guint));
	g_autoptr(GPtrArray) now = g_ptr_array_new_with_free_func (g_object_unref);
	guint                i;

	(void) physics;
	_lrg_world3d_physics_body_contacts (self->world, self->body_id, bodies);
	for (i = 0; i < bodies->len; i++)
	{
		LrgEntity3D *other = lrg_world3d_find_body_entity (self->world, g_array_index (bodies, guint, i));

		if (other == NULL || other == entity || g_ptr_array_find (now, other, NULL))
			continue;
		g_ptr_array_add (now, g_object_ref (other));
	}

	for (i = self->touching_entities->len; i > 0; i--)
	{
		LrgEntity3D *old = g_ptr_array_index (self->touching_entities, i - 1);

		if (!g_ptr_array_find (now, old, NULL))
		{
			g_autoptr(LrgEntity3D) keep = g_object_ref (old);

			g_ptr_array_remove_index (self->touching_entities, i - 1);
			g_signal_emit (self, signals[SIGNAL_COLLISION_EXITED], 0, keep);
		}
	}
	for (i = 0; i < now->len; i++)
	{
		LrgEntity3D *other = g_ptr_array_index (now, i);

		if (g_ptr_array_find (self->touching_entities, other, NULL))
			continue;
		g_ptr_array_add (self->touching_entities, g_object_ref (other));
		g_signal_emit (self, signals[SIGNAL_COLLISION_ENTERED], 0, other);
	}
}

static void
client_post_step (GObject          *client,
                  LrgDynamicsWorld *physics,
                  gfloat            dt)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (client);
	LrgEntity3D           *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	gfloat                 pos[3];
	gfloat                 rot[4];

	(void) dt;
	if (entity == NULL || self->body_id == LRG_PHYSICS_NO_BODY)
		return;

	if (self->kind == LRG_DYNAMICS_BODY_DYNAMIC &&
	    _lrg_physics_body_get_pose (physics, self->body_id, pos, rot))
	{
		if (!self->have_pose)
		{
			memcpy (self->curr_pos, pos, sizeof (pos));
			memcpy (self->curr_rot, rot, sizeof (rot));
		}
		memcpy (self->prev_pos, self->curr_pos, sizeof (pos));
		memcpy (self->prev_rot, self->curr_rot, sizeof (rot));
		memcpy (self->curr_pos, pos, sizeof (pos));
		memcpy (self->curr_rot, rot, sizeof (rot));
		self->have_pose = TRUE;

		/* game logic of the next substep sees the physics pose */
		write_entity (self, entity, pos, rot);
	}

	if (g_signal_has_handler_pending (self, signals[SIGNAL_COLLISION_ENTERED], 0, TRUE) ||
	    g_signal_has_handler_pending (self, signals[SIGNAL_COLLISION_EXITED], 0, TRUE) ||
	    self->touching_entities->len > 0)
		update_contacts (self, entity, physics);
}

static void
client_interpolate (GObject          *client,
                    LrgDynamicsWorld *physics,
                    gfloat            alpha)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (client);
	LrgEntity3D           *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	gfloat                 pos[3];
	gfloat                 rot[4];
	guint                  k;

	(void) physics;
	if (entity == NULL || self->body_id == LRG_PHYSICS_NO_BODY || !self->have_pose ||
	    self->kind != LRG_DYNAMICS_BODY_DYNAMIC)
		return;
	if (!self->interpolate)
		alpha = 1.0f;

	for (k = 0; k < 3; k++)
		pos[k] = self->prev_pos[k] + (self->curr_pos[k] - self->prev_pos[k]) * alpha;
	lrg_math3d_quat_slerp (self->prev_rot, self->curr_rot, alpha, rot);
	write_entity (self, entity, pos, rot);
}

static const LrgPhysicsClientClass body_client = {
	client_physics_changed,
	client_pre_step,
	client_post_step,
	client_interpolate
};

/* ==========================================================================
 * LrgComponent hooks
 * ========================================================================== */

static void
lrg_rigid_body_component_world_entered (LrgComponent *component,
                                        LrgWorld3D   *world)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (component);

	self->world = world;
	self->dirty = TRUE;
	_lrg_world3d_physics_add_client (world, G_OBJECT (self), LRG_PHYSICS_CLIENT_BODY, &body_client);
}

static void
lrg_rigid_body_component_world_exited (LrgComponent *component,
                                       LrgWorld3D   *world)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (component);

	drop_body (self, lrg_world3d_get_physics (world));
	clear_contacts (self, FALSE);
	_lrg_world3d_physics_remove_client (world, G_OBJECT (self));
	self->world = NULL;
	self->physics = NULL;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_rigid_body_component_finalize (GObject *object)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (object);

	g_free (self->layer);
	g_clear_pointer (&self->touching, g_array_unref);
	g_clear_pointer (&self->touching_entities, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_rigid_body_component_parent_class)->finalize (object);
}

static void
lrg_rigid_body_component_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_BODY_KIND:
		g_value_set_enum (value, (gint) self->kind);
		break;
	case PROP_MASS:
		g_value_set_float (value, self->mass);
		break;
	case PROP_FRICTION:
		g_value_set_float (value, self->friction);
		break;
	case PROP_RESTITUTION:
		g_value_set_float (value, self->restitution);
		break;
	case PROP_LINEAR_DAMPING:
		g_value_set_float (value, self->linear_damping);
		break;
	case PROP_ANGULAR_DAMPING:
		g_value_set_float (value, self->angular_damping);
		break;
	case PROP_GRAVITY_FACTOR:
		g_value_set_float (value, self->gravity_factor);
		break;
	case PROP_CCD:
		g_value_set_boolean (value, self->ccd);
		break;
	case PROP_LAYER:
		g_value_set_string (value, self->layer);
		break;
	case PROP_INTERPOLATE:
		g_value_set_boolean (value, self->interpolate);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/* Sets a float setting; the body is rebuilt when it changed. */
static void
set_setting (LrgRigidBodyComponent *self,
             gfloat                *field,
             gfloat                 value,
             guint                  prop)
{
	if (*field == value)
		return;
	*field = value;
	mark_dirty (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_rigid_body_component_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
	LrgRigidBodyComponent *self = LRG_RIGID_BODY_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_BODY_KIND:
		lrg_rigid_body_component_set_body_kind (self, (LrgDynamicsBodyKind) g_value_get_enum (value));
		break;
	case PROP_MASS:
		lrg_rigid_body_component_set_mass (self, g_value_get_float (value));
		break;
	case PROP_FRICTION:
		lrg_rigid_body_component_set_friction (self, g_value_get_float (value));
		break;
	case PROP_RESTITUTION:
		lrg_rigid_body_component_set_restitution (self, g_value_get_float (value));
		break;
	case PROP_LINEAR_DAMPING:
		set_setting (self, &self->linear_damping, MAX (g_value_get_float (value), 0.0f), prop_id);
		break;
	case PROP_ANGULAR_DAMPING:
		set_setting (self, &self->angular_damping, MAX (g_value_get_float (value), 0.0f), prop_id);
		break;
	case PROP_GRAVITY_FACTOR:
		lrg_rigid_body_component_set_gravity_factor (self, g_value_get_float (value));
		break;
	case PROP_CCD:
		lrg_rigid_body_component_set_ccd (self, g_value_get_boolean (value));
		break;
	case PROP_LAYER:
		lrg_rigid_body_component_set_layer (self, g_value_get_string (value));
		break;
	case PROP_INTERPOLATE:
		lrg_rigid_body_component_set_interpolate (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_rigid_body_component_class_init (LrgRigidBodyComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_rigid_body_component_finalize;
	object_class->get_property = lrg_rigid_body_component_get_property;
	object_class->set_property = lrg_rigid_body_component_set_property;

	component_class->world_entered = lrg_rigid_body_component_world_entered;
	component_class->world_exited = lrg_rigid_body_component_world_exited;

	/**
	 * LrgRigidBodyComponent:body-kind:
	 *
	 * Static, kinematic or dynamic.
	 */
	properties[PROP_BODY_KIND] =
		g_param_spec_enum ("body-kind", NULL, "Body kind", LRG_TYPE_DYNAMICS_BODY_KIND,
		                   LRG_DYNAMICS_BODY_DYNAMIC, rw);

	/**
	 * LrgRigidBodyComponent:mass:
	 *
	 * Mass in kg of a dynamic body.
	 */
	properties[PROP_MASS] = g_param_spec_float ("mass", NULL, "Mass", 0.001f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgRigidBodyComponent:friction:
	 *
	 * Friction coefficient (colliders may override it).
	 */
	properties[PROP_FRICTION] = g_param_spec_float ("friction", NULL, "Friction", 0.0f, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgRigidBodyComponent:restitution:
	 *
	 * Bounciness 0..1 (colliders may override it).
	 */
	properties[PROP_RESTITUTION] =
		g_param_spec_float ("restitution", NULL, "Restitution", 0.0f, 1.0f, 0.0f, rw);

	/**
	 * LrgRigidBodyComponent:linear-damping:
	 *
	 * Linear velocity damping per second.
	 */
	properties[PROP_LINEAR_DAMPING] =
		g_param_spec_float ("linear-damping", NULL, "Linear damping", 0.0f, G_MAXFLOAT, 0.05f, rw);

	/**
	 * LrgRigidBodyComponent:angular-damping:
	 *
	 * Angular velocity damping per second.
	 */
	properties[PROP_ANGULAR_DAMPING] =
		g_param_spec_float ("angular-damping", NULL, "Angular damping", 0.0f, G_MAXFLOAT, 0.05f, rw);

	/**
	 * LrgRigidBodyComponent:gravity-factor:
	 *
	 * Multiplier on the world gravity.
	 */
	properties[PROP_GRAVITY_FACTOR] =
		g_param_spec_float ("gravity-factor", NULL, "Gravity factor", -G_MAXFLOAT, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgRigidBodyComponent:ccd:
	 *
	 * Continuous collision detection for fast bodies.
	 */
	properties[PROP_CCD] = g_param_spec_boolean ("ccd", NULL, "CCD", FALSE, rw);

	/**
	 * LrgRigidBodyComponent:layer:
	 *
	 * Collision layer name, %NULL for the default layer.
	 */
	properties[PROP_LAYER] = g_param_spec_string ("layer", NULL, "Collision layer", NULL, rw);

	/**
	 * LrgRigidBodyComponent:interpolate:
	 *
	 * Blend the rendered transform between the last two physics states.
	 */
	properties[PROP_INTERPOLATE] = g_param_spec_boolean ("interpolate", NULL, "Interpolate", TRUE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgRigidBodyComponent::collision-entered:
	 * @self: the body component
	 * @other: the entity whose body started touching this one
	 *
	 * Emitted after the physics step in which a body of @other started to
	 * touch this body (once per entity).  Contacts are only tracked while a
	 * handler is connected to this signal or
	 * #LrgRigidBodyComponent::collision-exited.
	 */
	signals[SIGNAL_COLLISION_ENTERED] =
		g_signal_new ("collision-entered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
		              NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);

	/**
	 * LrgRigidBodyComponent::collision-exited:
	 * @self: the body component
	 * @other: the entity that no longer touches this body
	 *
	 * Emitted when the last contact with @other ended, or the body went
	 * away.
	 */
	signals[SIGNAL_COLLISION_EXITED] =
		g_signal_new ("collision-exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
		              NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);
}

static void
lrg_rigid_body_component_init (LrgRigidBodyComponent *self)
{
	self->kind = LRG_DYNAMICS_BODY_DYNAMIC;
	self->mass = 1.0f;
	self->friction = 0.5f;
	self->restitution = 0.0f;
	self->linear_damping = 0.05f;
	self->angular_damping = 0.05f;
	self->gravity_factor = 1.0f;
	self->interpolate = TRUE;
	self->dirty = TRUE;
	self->body_id = LRG_PHYSICS_NO_BODY;
	self->built_scale[0] = self->built_scale[1] = self->built_scale[2] = 1.0f;
	self->touching = g_array_new (FALSE, FALSE, sizeof (guint));
	self->touching_entities = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgRigidBodyComponent *
lrg_rigid_body_component_new (LrgDynamicsBodyKind kind)
{
	LrgRigidBodyComponent *self = g_object_new (LRG_TYPE_RIGID_BODY_COMPONENT, NULL);

	self->kind = kind;
	return self;
}

LrgDynamicsBodyKind
lrg_rigid_body_component_get_body_kind (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), LRG_DYNAMICS_BODY_DYNAMIC);
	return self->kind;
}

void
lrg_rigid_body_component_set_body_kind (LrgRigidBodyComponent *self,
                                        LrgDynamicsBodyKind    kind)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	if (self->kind == kind)
		return;
	self->kind = kind;
	mark_dirty (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BODY_KIND]);
}

gfloat
lrg_rigid_body_component_get_mass (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), 0.0f);
	return self->mass;
}

void
lrg_rigid_body_component_set_mass (LrgRigidBodyComponent *self,
                                   gfloat                 mass)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	set_setting (self, &self->mass, MAX (mass, 0.001f), PROP_MASS);
}

gfloat
lrg_rigid_body_component_get_friction (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), 0.0f);
	return self->friction;
}

void
lrg_rigid_body_component_set_friction (LrgRigidBodyComponent *self,
                                       gfloat                 friction)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	set_setting (self, &self->friction, MAX (friction, 0.0f), PROP_FRICTION);
}

gfloat
lrg_rigid_body_component_get_restitution (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), 0.0f);
	return self->restitution;
}

void
lrg_rigid_body_component_set_restitution (LrgRigidBodyComponent *self,
                                          gfloat                 restitution)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	set_setting (self, &self->restitution, CLAMP (restitution, 0.0f, 1.0f), PROP_RESTITUTION);
}

void
lrg_rigid_body_component_set_damping (LrgRigidBodyComponent *self,
                                      gfloat                 linear,
                                      gfloat                 angular)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	set_setting (self, &self->linear_damping, MAX (linear, 0.0f), PROP_LINEAR_DAMPING);
	set_setting (self, &self->angular_damping, MAX (angular, 0.0f), PROP_ANGULAR_DAMPING);
}

void
lrg_rigid_body_component_get_damping (LrgRigidBodyComponent *self,
                                      gfloat                *linear,
                                      gfloat                *angular)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	if (linear != NULL)
		*linear = self->linear_damping;
	if (angular != NULL)
		*angular = self->angular_damping;
}

gfloat
lrg_rigid_body_component_get_gravity_factor (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), 1.0f);
	return self->gravity_factor;
}

void
lrg_rigid_body_component_set_gravity_factor (LrgRigidBodyComponent *self,
                                             gfloat                 factor)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	set_setting (self, &self->gravity_factor, factor, PROP_GRAVITY_FACTOR);
}

gboolean
lrg_rigid_body_component_get_ccd (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), FALSE);
	return self->ccd;
}

void
lrg_rigid_body_component_set_ccd (LrgRigidBodyComponent *self,
                                  gboolean               ccd)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	ccd = !!ccd;
	if (self->ccd == ccd)
		return;
	self->ccd = ccd;
	mark_dirty (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CCD]);
}

const gchar *
lrg_rigid_body_component_get_layer (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), NULL);
	return self->layer;
}

void
lrg_rigid_body_component_set_layer (LrgRigidBodyComponent *self,
                                    const gchar           *layer)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	if (layer != NULL && *layer == '\0')
		layer = NULL;
	if (g_strcmp0 (self->layer, layer) == 0)
		return;
	g_free (self->layer);
	self->layer = g_strdup (layer);
	mark_dirty (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

gboolean
lrg_rigid_body_component_get_interpolate (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), FALSE);
	return self->interpolate;
}

void
lrg_rigid_body_component_set_interpolate (LrgRigidBodyComponent *self,
                                          gboolean               interpolate)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	interpolate = !!interpolate;
	if (self->interpolate == interpolate)
		return;
	self->interpolate = interpolate;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTERPOLATE]);
}

guint
lrg_rigid_body_component_get_body_id (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), 0);
	return self->body_id;
}

LrgDynamicsWorld *
lrg_rigid_body_component_get_physics (LrgRigidBodyComponent *self)
{
	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), NULL);
	return self->physics;
}

void
lrg_rigid_body_component_rebuild (LrgRigidBodyComponent *self)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	mark_dirty (self);
}

void
lrg_rigid_body_component_get_linear_velocity (LrgRigidBodyComponent *self,
                                              gfloat                *out)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	g_return_if_fail (out != NULL);

	out[0] = out[1] = out[2] = 0.0f;
	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL)
		lrg_dynamics_world_get_linear_velocity (self->physics, self->body_id, out, NULL);
	else if (self->has_pending_velocity)
		memcpy (out, self->pending_linear, sizeof (self->pending_linear));
}

void
lrg_rigid_body_component_set_linear_velocity (LrgRigidBodyComponent *self,
                                              const gfloat          *velocity)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	g_return_if_fail (velocity != NULL);

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL && !self->dirty)
	{
		lrg_dynamics_world_set_linear_velocity (self->physics, self->body_id, velocity, NULL);
		return;
	}
	if (!self->has_pending_velocity)
		memset (self->pending_angular, 0, sizeof (self->pending_angular));
	memcpy (self->pending_linear, velocity, sizeof (self->pending_linear));
	self->has_pending_velocity = TRUE;
}

void
lrg_rigid_body_component_get_angular_velocity (LrgRigidBodyComponent *self,
                                               gfloat                *out)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	g_return_if_fail (out != NULL);

	out[0] = out[1] = out[2] = 0.0f;
	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL)
		lrg_dynamics_world_get_angular_velocity (self->physics, self->body_id, out, NULL);
	else if (self->has_pending_velocity)
		memcpy (out, self->pending_angular, sizeof (self->pending_angular));
}

void
lrg_rigid_body_component_set_angular_velocity (LrgRigidBodyComponent *self,
                                               const gfloat          *velocity)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	g_return_if_fail (velocity != NULL);

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL && !self->dirty)
	{
		lrg_dynamics_world_set_angular_velocity (self->physics, self->body_id, velocity, NULL);
		return;
	}
	if (!self->has_pending_velocity)
		memset (self->pending_linear, 0, sizeof (self->pending_linear));
	memcpy (self->pending_angular, velocity, sizeof (self->pending_angular));
	self->has_pending_velocity = TRUE;
}

void
lrg_rigid_body_component_apply_force (LrgRigidBodyComponent *self,
                                      const gfloat          *force)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	g_return_if_fail (force != NULL);

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL)
		lrg_dynamics_world_apply_force (self->physics, self->body_id, force, NULL);
}

void
lrg_rigid_body_component_apply_impulse (LrgRigidBodyComponent *self,
                                        const gfloat          *impulse,
                                        const gfloat          *point)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	g_return_if_fail (impulse != NULL);

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL)
		lrg_dynamics_world_apply_impulse (self->physics, self->body_id, impulse, point, NULL);
}

void
lrg_rigid_body_component_apply_torque (LrgRigidBodyComponent *self,
                                       const gfloat          *torque)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));
	g_return_if_fail (torque != NULL);

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL)
		lrg_dynamics_world_apply_torque (self->physics, self->body_id, torque, NULL);
}

void
lrg_rigid_body_component_wake (LrgRigidBodyComponent *self)
{
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self));

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL)
		lrg_dynamics_world_wake_body (self->physics, self->body_id, NULL);
}

gboolean
lrg_rigid_body_component_is_sleeping (LrgRigidBodyComponent *self)
{
	gboolean sleeping = TRUE;

	g_return_val_if_fail (LRG_IS_RIGID_BODY_COMPONENT (self), TRUE);

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->physics != NULL &&
	    !lrg_dynamics_world_get_sleeping (self->physics, self->body_id, &sleeping, NULL))
		return TRUE;
	return sleeping;
}
