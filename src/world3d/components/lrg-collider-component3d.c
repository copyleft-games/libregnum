/* lrg-collider-component3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColliderComponent3D: one collision shape on an entity.
 *
 * Ownership of the physics body:
 *
 *   rigid body on the entity, not a trigger   -> merged into the rigid body
 *                                                 (it rebuilds its shape)
 *   no rigid body, not a trigger              -> the FIRST such collider of
 *                                                 the entity owns one static
 *                                                 body made of all of them
 *   trigger                                   -> its own kinematic sensor
 *                                                 body following the entity
 *
 * The collider is a physics client of the world (see
 * lrg-world3d-physics-private.h): pre_step builds / syncs its own body,
 * post_step turns the sensor's overlaps into trigger-entered / -exited.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-collider-component3d.h"
#include "lrg-rigid-body-component.h"
#include "lrg-mesh-renderer-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../lrg-world3d-physics-private.h"
#include "../../core/lrg-math3d.h"
#include "../../lrg-log.h"

#define MIN_DIMENSION (0.001f)

GType
lrg_collider3d_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_COLLIDER3D_SPHERE, "LRG_COLLIDER3D_SPHERE", "sphere" },
			{ LRG_COLLIDER3D_CAPSULE, "LRG_COLLIDER3D_CAPSULE", "capsule" },
			{ LRG_COLLIDER3D_CYLINDER, "LRG_COLLIDER3D_CYLINDER", "cylinder" },
			{ LRG_COLLIDER3D_BOX, "LRG_COLLIDER3D_BOX", "box" },
			{ LRG_COLLIDER3D_CONVEX, "LRG_COLLIDER3D_CONVEX", "convex" },
			{ LRG_COLLIDER3D_MESH, "LRG_COLLIDER3D_MESH", "mesh" },
			{ LRG_COLLIDER3D_PLANE, "LRG_COLLIDER3D_PLANE", "plane" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgCollider3DKind"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

struct _LrgColliderComponent3D
{
	LrgComponent       parent_instance;

	/* Shape description */
	LrgCollider3DKind  kind;
	gfloat             half_extents[3];
	gfloat             radius;
	gfloat             half_height;
	gfloat             offset[3];
	gfloat             rotation[3];      /* Euler degrees */
	gboolean           trigger;
	gfloat             friction;         /* < 0: the body's */
	gfloat             restitution;      /* < 0: the body's */
	gchar             *layer;
	gchar             *mesh;
	GArray            *mesh_vertices;    /* gfloat xyz, nullable */
	GArray            *mesh_indices;     /* guint32, nullable */

	/* Runtime */
	LrgWorld3D        *world;            /* while registered */
	LrgDynamicsWorld  *physics;          /* the world's, borrowed */
	guint              body_id;          /* own body (static or sensor), 0 = none */
	gboolean           dirty;            /* own body needs a rebuild */
	guint64            synced_version;   /* entity world version of the last sync */
	GPtrArray         *overlaps;         /* LrgEntity3D, owned refs (triggers) */
};

G_DEFINE_FINAL_TYPE (LrgColliderComponent3D, lrg_collider_component3d, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_KIND,
	PROP_HALF_EXTENT_X,
	PROP_HALF_EXTENT_Y,
	PROP_HALF_EXTENT_Z,
	PROP_RADIUS,
	PROP_HALF_HEIGHT,
	PROP_OFFSET_X,
	PROP_OFFSET_Y,
	PROP_OFFSET_Z,
	PROP_ROTATION_X,
	PROP_ROTATION_Y,
	PROP_ROTATION_Z,
	PROP_TRIGGER,
	PROP_FRICTION,
	PROP_RESTITUTION,
	PROP_LAYER,
	PROP_MESH,
	N_PROPS
};

enum
{
	SIGNAL_TRIGGER_ENTERED,
	SIGNAL_TRIGGER_EXITED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Change tracking
 * ========================================================================== */

/*
 * shape_changed:
 *
 * Something the body is built from changed: the own body is rebuilt on the
 * next pre_step, and the entity's rigid body (if any) rebuilds too.
 */
static void
shape_changed (LrgColliderComponent3D *self)
{
	LrgEntity3D *entity = lrg_component_get_entity (LRG_COMPONENT (self));

	self->dirty = TRUE;
	if (entity != NULL)
		_lrg_rigid_body_component_colliders_changed (entity);
}

/* Sets a float field, notifying its property when it changed. */
static void
set_field (LrgColliderComponent3D *self,
           gfloat                 *field,
           gfloat                  value,
           guint                   prop)
{
	if (*field == value)
		return;
	*field = value;
	shape_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* ==========================================================================
 * Body ownership
 * ========================================================================== */

/* TRUE when the entity has a rigid body component. */
static gboolean
entity_has_rigid_body (LrgEntity3D *entity)
{
	return lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT) != NULL;
}

/*
 * static_owner:
 *
 * The first non-trigger collider of the entity: it owns the one static
 * body made of all of them.
 */
static LrgColliderComponent3D *
static_owner (LrgEntity3D *entity)
{
	g_autoptr(GPtrArray) all = lrg_entity3d_get_components_of_type (entity, LRG_TYPE_COLLIDER_COMPONENT3D);
	guint                i;

	for (i = 0; i < all->len; i++)
	{
		LrgColliderComponent3D *c = g_ptr_array_index (all, i);

		if (!c->trigger && lrg_component_get_enabled (LRG_COMPONENT (c)))
			return c;
	}
	return NULL;
}

/* The non-trigger, enabled colliders of the entity (borrowed pointers). */
static GPtrArray *
solid_colliders (LrgEntity3D *entity)
{
	g_autoptr(GPtrArray) all = lrg_entity3d_get_components_of_type (entity, LRG_TYPE_COLLIDER_COMPONENT3D);
	GPtrArray           *out = g_ptr_array_new ();
	guint                i;

	for (i = 0; i < all->len; i++)
	{
		LrgColliderComponent3D *c = g_ptr_array_index (all, i);

		if (!c->trigger && lrg_component_get_enabled (LRG_COMPONENT (c)))
			g_ptr_array_add (out, c);
	}
	return out;
}

/* Clears the trigger's overlap set, emitting trigger-exited for each. */
static void
clear_overlaps (LrgColliderComponent3D *self,
                gboolean                emit)
{
	g_autoptr(GPtrArray) gone = NULL;
	guint                i;

	if (self->overlaps->len == 0)
		return;
	gone = self->overlaps;
	self->overlaps = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; emit && i < gone->len; i++)
		g_signal_emit (self, signals[SIGNAL_TRIGGER_EXITED], 0, g_ptr_array_index (gone, i));
}

static void
drop_body (LrgColliderComponent3D *self,
           LrgDynamicsWorld       *physics)
{
	if (self->body_id != LRG_PHYSICS_NO_BODY && physics != NULL && self->world != NULL)
		_lrg_physics_body_destroy (self->world, physics, self->body_id);
	self->body_id = LRG_PHYSICS_NO_BODY;
	self->dirty = TRUE;
}

/*
 * build_own_body:
 *
 * A trigger: a kinematic sensor body of just this collider.  A static
 * owner: a static body of every solid collider on the entity.
 */
static void
build_own_body (LrgColliderComponent3D *self,
                LrgEntity3D            *entity,
                LrgDynamicsWorld       *physics)
{
	g_autoptr(GPtrArray)       list = NULL;
	g_autoptr(LrgPhysicsShape) shape = NULL;
	LrgPhysicsBodyDesc         desc;

	_lrg_physics_body_desc_init (&desc);
	if (self->trigger)
	{
		list = g_ptr_array_new ();
		g_ptr_array_add (list, self);
		desc.kind = LRG_DYNAMICS_BODY_KINEMATIC;
		desc.sensor = TRUE;
	}
	else
	{
		list = solid_colliders (entity);
		desc.kind = LRG_DYNAMICS_BODY_STATIC;
	}
	desc.layer = self->layer;
	if (self->friction >= 0.0f)
		desc.friction = self->friction;
	if (self->restitution >= 0.0f)
		desc.restitution = self->restitution;

	shape = _lrg_physics_build_shape (entity, list);
	if (shape == NULL)
		return;
	self->body_id = _lrg_physics_body_create (self->world, physics, entity, shape, &desc);
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
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (client);

	drop_body (self, old_physics);
	clear_overlaps (self, TRUE);
	self->physics = new_physics;
}

static void
client_pre_step (GObject          *client,
                 LrgDynamicsWorld *physics,
                 gfloat            dt)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (client);
	LrgEntity3D            *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	gboolean                wants_body;

	if (entity == NULL)
		return;

	/* Does this collider own a body right now? */
	wants_body = lrg_component_get_enabled (LRG_COMPONENT (self)) &&
	             lrg_entity3d_is_active_in_hierarchy (entity) &&
	             (self->trigger || (!entity_has_rigid_body (entity) && static_owner (entity) == self));

	if (!wants_body)
	{
		if (self->body_id != LRG_PHYSICS_NO_BODY)
			drop_body (self, physics);
		clear_overlaps (self, TRUE);
		return;
	}

	if (self->body_id != LRG_PHYSICS_NO_BODY && self->dirty)
		drop_body (self, physics);
	if (self->body_id == LRG_PHYSICS_NO_BODY)
	{
		build_own_body (self, entity, physics);
		self->dirty = FALSE;
		return;
	}

	/* Follow the entity: a sensor moves kinematically, a static body is
	 * teleported only when game code moved the entity. */
	if (lrg_entity3d_get_world_version (entity) != self->synced_version)
	{
		if (self->trigger)
			_lrg_physics_body_move_kinematic (physics, self->body_id, entity, dt);
		else
			_lrg_physics_body_teleport (physics, self->body_id, entity);
		self->synced_version = lrg_entity3d_get_world_version (entity);
	}
}

/* Index of @entity in the overlap set, or -1. */
static gint
overlap_index (LrgColliderComponent3D *self,
               LrgEntity3D            *entity)
{
	guint i;

	for (i = 0; i < self->overlaps->len; i++)
		if (g_ptr_array_index (self->overlaps, i) == entity)
			return (gint) i;
	return -1;
}

/*
 * client_post_step:
 *
 * A trigger turns the sensor's current overlaps into entity enter / exit
 * signals.  Several bodies of one entity count once; the trigger's own
 * entity never counts.
 */
static void
client_post_step (GObject          *client,
                  LrgDynamicsWorld *physics,
                  gfloat            dt)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (client);
	LrgEntity3D            *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	g_autoptr(GPtrArray)    now = NULL;
	g_autoptr(GArray)       bodies = NULL;
	guint                   i;

	(void) dt;
	if (!self->trigger || self->body_id == LRG_PHYSICS_NO_BODY || entity == NULL || self->world == NULL)
		return;

	bodies = g_array_new (FALSE, FALSE, sizeof (guint));
	_lrg_physics_sensor_overlaps (physics, self->body_id, bodies);

	now = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < bodies->len; i++)
	{
		LrgEntity3D *other = lrg_world3d_find_body_entity (self->world, g_array_index (bodies, guint, i));

		if (other == NULL || other == entity || g_ptr_array_find (now, other, NULL))
			continue;
		g_ptr_array_add (now, g_object_ref (other));
	}

	/* exits first, then enters, both in a stable order */
	for (i = self->overlaps->len; i > 0; i--)
	{
		LrgEntity3D *old = g_ptr_array_index (self->overlaps, i - 1);

		if (!g_ptr_array_find (now, old, NULL))
		{
			g_autoptr(LrgEntity3D) keep = g_object_ref (old);

			g_ptr_array_remove_index (self->overlaps, i - 1);
			g_signal_emit (self, signals[SIGNAL_TRIGGER_EXITED], 0, keep);
		}
	}
	for (i = 0; i < now->len; i++)
	{
		LrgEntity3D *other = g_ptr_array_index (now, i);

		if (overlap_index (self, other) >= 0)
			continue;
		g_ptr_array_add (self->overlaps, g_object_ref (other));
		g_signal_emit (self, signals[SIGNAL_TRIGGER_ENTERED], 0, other);
	}
}

static const LrgPhysicsClientClass collider_client = {
	client_physics_changed,
	client_pre_step,
	client_post_step,
	NULL
};

/* ==========================================================================
 * LrgComponent hooks
 * ========================================================================== */

static void
lrg_collider_component3d_world_entered (LrgComponent *component,
                                        LrgWorld3D   *world)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (component);

	self->world = world;
	self->dirty = TRUE;
	_lrg_world3d_physics_add_client (world, G_OBJECT (self), LRG_PHYSICS_CLIENT_COLLIDER, &collider_client);
	shape_changed (self);
}

static void
lrg_collider_component3d_world_exited (LrgComponent *component,
                                       LrgWorld3D   *world)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (component);

	drop_body (self, lrg_world3d_get_physics (world));
	clear_overlaps (self, FALSE);
	_lrg_world3d_physics_remove_client (world, G_OBJECT (self));
	self->world = NULL;
	self->physics = NULL;
	shape_changed (self);
}

static void
lrg_collider_component3d_detached_entity (LrgComponent *component,
                                          LrgEntity3D  *entity)
{
	/* The rigid body must forget this collider; world_exited already
	 * ran when the entity is in a world. */
	_lrg_rigid_body_component_colliders_changed (entity);
}

static gboolean
lrg_collider_component3d_get_bounds (LrgComponent     *component,
                                     LrgBoundingBox3D *out)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (component);
	gfloat                  e;

	/* A conservative local box around the offset (rotation ignored). */
	switch (self->kind)
	{
	case LRG_COLLIDER3D_SPHERE:
		e = self->radius;
		out->min.x = self->offset[0] - e;
		out->min.y = self->offset[1] - e;
		out->min.z = self->offset[2] - e;
		out->max.x = self->offset[0] + e;
		out->max.y = self->offset[1] + e;
		out->max.z = self->offset[2] + e;
		return TRUE;
	case LRG_COLLIDER3D_BOX:
		e = sqrtf (self->half_extents[0] * self->half_extents[0] +
		           self->half_extents[1] * self->half_extents[1] +
		           self->half_extents[2] * self->half_extents[2]);
		break;
	case LRG_COLLIDER3D_CAPSULE:
	case LRG_COLLIDER3D_CYLINDER:
		e = self->radius + self->half_height;
		break;
	default:
		return FALSE;   /* meshes and planes: the renderer's bounds */
	}
	out->min.x = self->offset[0] - e;
	out->min.y = self->offset[1] - e;
	out->min.z = self->offset[2] - e;
	out->max.x = self->offset[0] + e;
	out->max.y = self->offset[1] + e;
	out->max.z = self->offset[2] + e;
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_collider_component3d_finalize (GObject *object)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (object);

	g_free (self->layer);
	g_free (self->mesh);
	g_clear_pointer (&self->mesh_vertices, g_array_unref);
	g_clear_pointer (&self->mesh_indices, g_array_unref);
	g_clear_pointer (&self->overlaps, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_collider_component3d_parent_class)->finalize (object);
}

static void
lrg_collider_component3d_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (object);

	switch (prop_id)
	{
	case PROP_KIND:
		g_value_set_enum (value, (gint) self->kind);
		break;
	case PROP_HALF_EXTENT_X:
	case PROP_HALF_EXTENT_Y:
	case PROP_HALF_EXTENT_Z:
		g_value_set_float (value, self->half_extents[prop_id - PROP_HALF_EXTENT_X]);
		break;
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_HALF_HEIGHT:
		g_value_set_float (value, self->half_height);
		break;
	case PROP_OFFSET_X:
	case PROP_OFFSET_Y:
	case PROP_OFFSET_Z:
		g_value_set_float (value, self->offset[prop_id - PROP_OFFSET_X]);
		break;
	case PROP_ROTATION_X:
	case PROP_ROTATION_Y:
	case PROP_ROTATION_Z:
		g_value_set_float (value, self->rotation[prop_id - PROP_ROTATION_X]);
		break;
	case PROP_TRIGGER:
		g_value_set_boolean (value, self->trigger);
		break;
	case PROP_FRICTION:
		g_value_set_float (value, self->friction);
		break;
	case PROP_RESTITUTION:
		g_value_set_float (value, self->restitution);
		break;
	case PROP_LAYER:
		g_value_set_string (value, self->layer);
		break;
	case PROP_MESH:
		g_value_set_string (value, self->mesh);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_collider_component3d_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
	LrgColliderComponent3D *self = LRG_COLLIDER_COMPONENT3D (object);
	gfloat                  f;

	switch (prop_id)
	{
	case PROP_KIND:
		lrg_collider_component3d_set_kind (self, (LrgCollider3DKind) g_value_get_enum (value));
		break;
	case PROP_HALF_EXTENT_X:
	case PROP_HALF_EXTENT_Y:
	case PROP_HALF_EXTENT_Z:
		f = MAX (g_value_get_float (value), MIN_DIMENSION);
		set_field (self, &self->half_extents[prop_id - PROP_HALF_EXTENT_X], f, prop_id);
		break;
	case PROP_RADIUS:
		lrg_collider_component3d_set_radius (self, g_value_get_float (value));
		break;
	case PROP_HALF_HEIGHT:
		lrg_collider_component3d_set_half_height (self, g_value_get_float (value));
		break;
	case PROP_OFFSET_X:
	case PROP_OFFSET_Y:
	case PROP_OFFSET_Z:
		set_field (self, &self->offset[prop_id - PROP_OFFSET_X], g_value_get_float (value), prop_id);
		break;
	case PROP_ROTATION_X:
	case PROP_ROTATION_Y:
	case PROP_ROTATION_Z:
		set_field (self, &self->rotation[prop_id - PROP_ROTATION_X], g_value_get_float (value), prop_id);
		break;
	case PROP_TRIGGER:
		lrg_collider_component3d_set_trigger (self, g_value_get_boolean (value));
		break;
	case PROP_FRICTION:
		lrg_collider_component3d_set_friction (self, g_value_get_float (value));
		break;
	case PROP_RESTITUTION:
		lrg_collider_component3d_set_restitution (self, g_value_get_float (value));
		break;
	case PROP_LAYER:
		lrg_collider_component3d_set_layer (self, g_value_get_string (value));
		break;
	case PROP_MESH:
		lrg_collider_component3d_set_mesh (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_collider_component3d_class_init (LrgColliderComponent3DClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_collider_component3d_finalize;
	object_class->get_property = lrg_collider_component3d_get_property;
	object_class->set_property = lrg_collider_component3d_set_property;

	component_class->world_entered = lrg_collider_component3d_world_entered;
	component_class->world_exited = lrg_collider_component3d_world_exited;
	component_class->detached_entity = lrg_collider_component3d_detached_entity;
	component_class->get_bounds = lrg_collider_component3d_get_bounds;

	/**
	 * LrgColliderComponent3D:kind:
	 *
	 * The shape kind.
	 */
	properties[PROP_KIND] =
		g_param_spec_enum ("kind", NULL, "Shape kind", LRG_TYPE_COLLIDER3D_KIND, LRG_COLLIDER3D_BOX, rw);

	/**
	 * LrgColliderComponent3D:half-extent-x:
	 *
	 * Box half extent along local X.
	 */
	properties[PROP_HALF_EXTENT_X] =
		g_param_spec_float ("half-extent-x", NULL, "Box half extent X", MIN_DIMENSION, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgColliderComponent3D:half-extent-y:
	 *
	 * Box half extent along local Y.
	 */
	properties[PROP_HALF_EXTENT_Y] =
		g_param_spec_float ("half-extent-y", NULL, "Box half extent Y", MIN_DIMENSION, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgColliderComponent3D:half-extent-z:
	 *
	 * Box half extent along local Z.
	 */
	properties[PROP_HALF_EXTENT_Z] =
		g_param_spec_float ("half-extent-z", NULL, "Box half extent Z", MIN_DIMENSION, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgColliderComponent3D:radius:
	 *
	 * Sphere, capsule or cylinder radius.
	 */
	properties[PROP_RADIUS] =
		g_param_spec_float ("radius", NULL, "Radius", MIN_DIMENSION, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgColliderComponent3D:half-height:
	 *
	 * Capsule (half the cap-centre distance) or cylinder (half the height)
	 * half height along local Y.
	 */
	properties[PROP_HALF_HEIGHT] =
		g_param_spec_float ("half-height", NULL, "Half height", 0.0f, G_MAXFLOAT, 0.5f, rw);

	/**
	 * LrgColliderComponent3D:offset-x:
	 *
	 * Local X offset of the shape from the entity origin.
	 */
	properties[PROP_OFFSET_X] =
		g_param_spec_float ("offset-x", NULL, "Offset X", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgColliderComponent3D:offset-y:
	 *
	 * Local Y offset.
	 */
	properties[PROP_OFFSET_Y] =
		g_param_spec_float ("offset-y", NULL, "Offset Y", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgColliderComponent3D:offset-z:
	 *
	 * Local Z offset.
	 */
	properties[PROP_OFFSET_Z] =
		g_param_spec_float ("offset-z", NULL, "Offset Z", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgColliderComponent3D:rotation-x:
	 *
	 * Local rotation about X in degrees.
	 */
	properties[PROP_ROTATION_X] =
		g_param_spec_float ("rotation-x", NULL, "Rotation X", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgColliderComponent3D:rotation-y:
	 *
	 * Local rotation about Y in degrees.
	 */
	properties[PROP_ROTATION_Y] =
		g_param_spec_float ("rotation-y", NULL, "Rotation Y", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgColliderComponent3D:rotation-z:
	 *
	 * Local rotation about Z in degrees.
	 */
	properties[PROP_ROTATION_Z] =
		g_param_spec_float ("rotation-z", NULL, "Rotation Z", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgColliderComponent3D:trigger:
	 *
	 * A sensor: reports overlaps, does not collide.
	 */
	properties[PROP_TRIGGER] = g_param_spec_boolean ("trigger", NULL, "Trigger", FALSE, rw);

	/**
	 * LrgColliderComponent3D:friction:
	 *
	 * Friction override, negative for the body's.
	 */
	properties[PROP_FRICTION] =
		g_param_spec_float ("friction", NULL, "Friction override", -1.0f, G_MAXFLOAT, -1.0f, rw);

	/**
	 * LrgColliderComponent3D:restitution:
	 *
	 * Restitution override, negative for the body's.
	 */
	properties[PROP_RESTITUTION] =
		g_param_spec_float ("restitution", NULL, "Restitution override", -1.0f, 1.0f, -1.0f, rw);

	/**
	 * LrgColliderComponent3D:layer:
	 *
	 * Collision layer name of a collider with its own body, %NULL for the
	 * default.
	 */
	properties[PROP_LAYER] = g_param_spec_string ("layer", NULL, "Collision layer", NULL, rw);

	/**
	 * LrgColliderComponent3D:mesh:
	 *
	 * Model path for convex / mesh colliders, %NULL for the entity's model.
	 */
	properties[PROP_MESH] = g_param_spec_string ("mesh", NULL, "Mesh source", NULL, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgColliderComponent3D::trigger-entered:
	 * @self: the trigger collider
	 * @other: the entity whose body started overlapping
	 *
	 * Emitted from the fixed update after the physics step in which a body
	 * of @other started to overlap the trigger (once per entity, however
	 * many bodies it has).
	 */
	signals[SIGNAL_TRIGGER_ENTERED] =
		g_signal_new ("trigger-entered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
		              NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);

	/**
	 * LrgColliderComponent3D::trigger-exited:
	 * @self: the trigger collider
	 * @other: the entity that no longer overlaps
	 *
	 * Emitted when the last body of @other stopped overlapping the trigger,
	 * when the trigger's body goes away (the physics world changed), or
	 * when the trigger is disabled.
	 */
	signals[SIGNAL_TRIGGER_EXITED] =
		g_signal_new ("trigger-exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
		              NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);
}

static void
lrg_collider_component3d_init (LrgColliderComponent3D *self)
{
	self->kind = LRG_COLLIDER3D_BOX;
	self->half_extents[0] = self->half_extents[1] = self->half_extents[2] = 0.5f;
	self->radius = 0.5f;
	self->half_height = 0.5f;
	self->friction = -1.0f;
	self->restitution = -1.0f;
	self->dirty = TRUE;
	self->body_id = LRG_PHYSICS_NO_BODY;
	self->overlaps = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgColliderComponent3D *
lrg_collider_component3d_new (LrgCollider3DKind kind)
{
	LrgColliderComponent3D *self = g_object_new (LRG_TYPE_COLLIDER_COMPONENT3D, NULL);

	self->kind = kind;
	return self;
}

LrgColliderComponent3D *
lrg_collider_component3d_new_box (gfloat hx,
                                  gfloat hy,
                                  gfloat hz)
{
	LrgColliderComponent3D *self = lrg_collider_component3d_new (LRG_COLLIDER3D_BOX);

	lrg_collider_component3d_set_half_extents (self, hx, hy, hz);
	return self;
}

LrgColliderComponent3D *
lrg_collider_component3d_new_sphere (gfloat radius)
{
	LrgColliderComponent3D *self = lrg_collider_component3d_new (LRG_COLLIDER3D_SPHERE);

	lrg_collider_component3d_set_radius (self, radius);
	return self;
}

LrgColliderComponent3D *
lrg_collider_component3d_new_capsule (gfloat radius,
                                      gfloat half_height)
{
	LrgColliderComponent3D *self = lrg_collider_component3d_new (LRG_COLLIDER3D_CAPSULE);

	lrg_collider_component3d_set_radius (self, radius);
	lrg_collider_component3d_set_half_height (self, half_height);
	return self;
}

LrgCollider3DKind
lrg_collider_component3d_get_kind (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), LRG_COLLIDER3D_BOX);
	return self->kind;
}

void
lrg_collider_component3d_set_kind (LrgColliderComponent3D *self,
                                   LrgCollider3DKind       kind)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	if (self->kind == kind)
		return;
	self->kind = kind;
	shape_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KIND]);
}

void
lrg_collider_component3d_get_half_extents (LrgColliderComponent3D *self,
                                           gfloat                 *out)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->half_extents, sizeof (self->half_extents));
}

void
lrg_collider_component3d_set_half_extents (LrgColliderComponent3D *self,
                                           gfloat                  hx,
                                           gfloat                  hy,
                                           gfloat                  hz)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	set_field (self, &self->half_extents[0], MAX (hx, MIN_DIMENSION), PROP_HALF_EXTENT_X);
	set_field (self, &self->half_extents[1], MAX (hy, MIN_DIMENSION), PROP_HALF_EXTENT_Y);
	set_field (self, &self->half_extents[2], MAX (hz, MIN_DIMENSION), PROP_HALF_EXTENT_Z);
}

gfloat
lrg_collider_component3d_get_radius (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), 0.0f);
	return self->radius;
}

void
lrg_collider_component3d_set_radius (LrgColliderComponent3D *self,
                                     gfloat                  radius)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	set_field (self, &self->radius, MAX (radius, MIN_DIMENSION), PROP_RADIUS);
}

gfloat
lrg_collider_component3d_get_half_height (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), 0.0f);
	return self->half_height;
}

void
lrg_collider_component3d_set_half_height (LrgColliderComponent3D *self,
                                          gfloat                  half_height)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	set_field (self, &self->half_height, MAX (half_height, 0.0f), PROP_HALF_HEIGHT);
}

void
lrg_collider_component3d_get_offset (LrgColliderComponent3D *self,
                                     gfloat                 *out)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->offset, sizeof (self->offset));
}

void
lrg_collider_component3d_set_offset (LrgColliderComponent3D *self,
                                     gfloat                  x,
                                     gfloat                  y,
                                     gfloat                  z)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	set_field (self, &self->offset[0], x, PROP_OFFSET_X);
	set_field (self, &self->offset[1], y, PROP_OFFSET_Y);
	set_field (self, &self->offset[2], z, PROP_OFFSET_Z);
}

void
lrg_collider_component3d_get_rotation (LrgColliderComponent3D *self,
                                       gfloat                 *out)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->rotation, sizeof (self->rotation));
}

void
lrg_collider_component3d_set_rotation (LrgColliderComponent3D *self,
                                       gfloat                  x,
                                       gfloat                  y,
                                       gfloat                  z)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	set_field (self, &self->rotation[0], x, PROP_ROTATION_X);
	set_field (self, &self->rotation[1], y, PROP_ROTATION_Y);
	set_field (self, &self->rotation[2], z, PROP_ROTATION_Z);
}

gboolean
lrg_collider_component3d_get_trigger (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), FALSE);
	return self->trigger;
}

void
lrg_collider_component3d_set_trigger (LrgColliderComponent3D *self,
                                      gboolean                trigger)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	trigger = !!trigger;
	if (self->trigger == trigger)
		return;
	self->trigger = trigger;
	shape_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGER]);
}

gfloat
lrg_collider_component3d_get_friction (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), -1.0f);
	return self->friction;
}

void
lrg_collider_component3d_set_friction (LrgColliderComponent3D *self,
                                       gfloat                  friction)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	set_field (self, &self->friction, friction < 0.0f ? -1.0f : friction, PROP_FRICTION);
}

gfloat
lrg_collider_component3d_get_restitution (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), -1.0f);
	return self->restitution;
}

void
lrg_collider_component3d_set_restitution (LrgColliderComponent3D *self,
                                          gfloat                  restitution)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	set_field (self, &self->restitution, restitution < 0.0f ? -1.0f : MIN (restitution, 1.0f),
	           PROP_RESTITUTION);
}

const gchar *
lrg_collider_component3d_get_layer (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), NULL);
	return self->layer;
}

void
lrg_collider_component3d_set_layer (LrgColliderComponent3D *self,
                                    const gchar            *layer)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	if (layer != NULL && *layer == '\0')
		layer = NULL;
	if (g_strcmp0 (self->layer, layer) == 0)
		return;
	g_free (self->layer);
	self->layer = g_strdup (layer);
	shape_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

const gchar *
lrg_collider_component3d_get_mesh (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), NULL);
	return self->mesh;
}

void
lrg_collider_component3d_set_mesh (LrgColliderComponent3D *self,
                                   const gchar            *mesh)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	if (mesh != NULL && *mesh == '\0')
		mesh = NULL;
	if (g_strcmp0 (self->mesh, mesh) == 0)
		return;
	g_free (self->mesh);
	self->mesh = g_strdup (mesh);
	shape_changed (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESH]);
}

void
lrg_collider_component3d_set_mesh_data (LrgColliderComponent3D *self,
                                        const gfloat           *vertices,
                                        gsize                   n_floats,
                                        const guint32          *indices,
                                        gsize                   n_indices)
{
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	g_return_if_fail (vertices != NULL || n_floats == 0);

	g_clear_pointer (&self->mesh_vertices, g_array_unref);
	g_clear_pointer (&self->mesh_indices, g_array_unref);
	if (n_floats >= 3)
	{
		self->mesh_vertices = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), (guint) n_floats);
		g_array_append_vals (self->mesh_vertices, vertices, (guint) (n_floats - n_floats % 3));
	}
	if (indices != NULL && n_indices >= 3)
	{
		self->mesh_indices = g_array_sized_new (FALSE, FALSE, sizeof (guint32), (guint) n_indices);
		g_array_append_vals (self->mesh_indices, indices, (guint) (n_indices - n_indices % 3));
	}
	shape_changed (self);
}

/* --------------------------------------------------------------------------
 * Mesh geometry
 * -------------------------------------------------------------------------- */

/*
 * append_model:
 *
 * Appends the CPU-side triangles of every mesh of @model (with the model's
 * own transform) to @verts / @indices.  Meshes without CPU data are
 * skipped.
 */
static void
append_model (GrlModel *model,
              GArray   *verts,
              GArray   *indices)
{
	Model *raw = grl_model_get_handle (model);
	gint   i;

	if (raw == NULL)
		return;
	for (i = 0; i < raw->meshCount; i++)
	{
		const Mesh *mesh = &raw->meshes[i];
		guint32      base = verts->len / 3;
		gint         v;

		if (mesh->vertices == NULL || mesh->vertexCount <= 0)
			continue;
		for (v = 0; v < mesh->vertexCount; v++)
		{
			gfloat x = mesh->vertices[v * 3 + 0];
			gfloat y = mesh->vertices[v * 3 + 1];
			gfloat z = mesh->vertices[v * 3 + 2];
			gfloat p[3];

			/* raylib Matrix: m0 m4 m8 m12 is the first row */
			p[0] = raw->transform.m0 * x + raw->transform.m4 * y + raw->transform.m8 * z + raw->transform.m12;
			p[1] = raw->transform.m1 * x + raw->transform.m5 * y + raw->transform.m9 * z + raw->transform.m13;
			p[2] = raw->transform.m2 * x + raw->transform.m6 * y + raw->transform.m10 * z + raw->transform.m14;
			g_array_append_vals (verts, p, 3);
		}
		if (mesh->indices != NULL)
		{
			gint k;

			for (k = 0; k < mesh->triangleCount * 3; k++)
			{
				guint32 idx = base + (guint32) mesh->indices[k];

				g_array_append_val (indices, idx);
			}
		}
		else
		{
			gint k;

			for (k = 0; k + 2 < mesh->vertexCount; k += 3)
			{
				guint32 tri[3];

				tri[0] = base + (guint32) k;
				tri[1] = base + (guint32) k + 1;
				tri[2] = base + (guint32) k + 2;
				g_array_append_vals (indices, tri, 3);
			}
		}
	}
}

/*
 * gather_geometry:
 *
 * The collider's triangles in entity space: explicit data, else the mesh
 * path (loaded only when a window / GL context exists, raylib uploads on
 * load), else the model of the entity's mesh renderer.
 */
static gboolean
gather_geometry (LrgColliderComponent3D *self,
                 GArray                 *verts,
                 GArray                 *indices)
{
	LrgEntity3D *entity;

	if (self->mesh_vertices != NULL)
	{
		g_array_append_vals (verts, self->mesh_vertices->data, self->mesh_vertices->len);
		if (self->mesh_indices != NULL)
			g_array_append_vals (indices, self->mesh_indices->data, self->mesh_indices->len);
		return verts->len >= 9;
	}

	if (self->mesh != NULL)
	{
		g_autoptr(GrlModel) model = NULL;

		if (!IsWindowReady ())
		{
			lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
			             "collider mesh '%s': models load only with a window; use "
			             "lrg_collider_component3d_set_mesh_data()", self->mesh);
			return FALSE;
		}
		model = grl_model_new_from_file (self->mesh, NULL);
		if (model != NULL)
			append_model (model, verts, indices);
		return verts->len >= 9;
	}

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
	{
		LrgComponent *renderer = lrg_entity3d_get_component (entity, LRG_TYPE_MESH_RENDERER_COMPONENT);
		GrlModel     *model = NULL;

		if (renderer != NULL)
			model = lrg_mesh_renderer_component_get_model (LRG_MESH_RENDERER_COMPONENT (renderer));
		if (model != NULL)
			append_model (model, verts, indices);
	}
	return verts->len >= 9;
}

LrgPhysicsShape *
lrg_collider_component3d_build_shape (LrgColliderComponent3D *self,
                                      const gfloat           *scale)
{
	gfloat            s[3] = { 1.0f, 1.0f, 1.0f };
	gfloat            radial;
	g_autoptr(GError) error = NULL;
	LrgPhysicsShape  *shape = NULL;

	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), NULL);

	if (scale != NULL)
	{
		s[0] = fabsf (scale[0]);
		s[1] = fabsf (scale[1]);
		s[2] = fabsf (scale[2]);
	}
	radial = MAX (s[0], s[2]);

	switch (self->kind)
	{
	case LRG_COLLIDER3D_SPHERE:
		shape = lrg_physics_shape_new_sphere (self->radius * MAX (radial, s[1]));
		break;
	case LRG_COLLIDER3D_CAPSULE:
		shape = lrg_physics_shape_new_capsule (self->radius * radial, self->half_height * s[1]);
		break;
	case LRG_COLLIDER3D_CYLINDER:
		shape = lrg_physics_shape_new_cylinder (self->radius * radial, MAX (self->half_height * s[1], MIN_DIMENSION));
		break;
	case LRG_COLLIDER3D_BOX:
		shape = lrg_physics_shape_new_box (self->half_extents[0] * s[0], self->half_extents[1] * s[1],
		                                   self->half_extents[2] * s[2]);
		break;
	case LRG_COLLIDER3D_PLANE:
		{
			static const gfloat up[3] = { 0.0f, 1.0f, 0.0f };

			/* solid below local Y = 0 of the collider frame */
			shape = lrg_physics_shape_new_plane (up, 0.0f);
		}
		break;
	case LRG_COLLIDER3D_CONVEX:
	case LRG_COLLIDER3D_MESH:
		{
			g_autoptr(GArray) verts = g_array_new (FALSE, FALSE, sizeof (gfloat));
			g_autoptr(GArray) indices = g_array_new (FALSE, FALSE, sizeof (guint32));
			guint             i;

			if (!gather_geometry (self, verts, indices))
				return NULL;
			for (i = 0; i < verts->len; i++)
				g_array_index (verts, gfloat, i) *= s[i % 3];
			if (self->kind == LRG_COLLIDER3D_CONVEX)
				shape = lrg_physics_shape_new_convex_hull ((const gfloat *) verts->data, verts->len, 0.0f, &error);
			else if (indices->len >= 3)
				shape = lrg_physics_shape_new_triangle_mesh ((const gfloat *) verts->data, verts->len,
				                                             (const guint32 *) indices->data, indices->len,
				                                             &error);
		}
		break;
	default:
		break;
	}

	if (shape == NULL && error != NULL)
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D, "collider shape: %s", error->message);
	return shape;
}

void
lrg_collider_component3d_get_local_transform (LrgColliderComponent3D *self,
                                              const gfloat           *scale,
                                              gfloat                 *position,
                                              gfloat                 *rotation)
{
	gfloat euler[3];
	guint  k;

	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self));
	g_return_if_fail (position != NULL && rotation != NULL);

	for (k = 0; k < 3; k++)
	{
		position[k] = self->offset[k] * (scale != NULL ? scale[k] : 1.0f);
		euler[k] = self->rotation[k] * (gfloat) (G_PI / 180.0);
	}
	lrg_math3d_euler_to_quat (euler, rotation);
}

guint
lrg_collider_component3d_get_body_id (LrgColliderComponent3D *self)
{
	LrgEntity3D  *entity;
	LrgComponent *rb;

	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), 0);

	if (self->body_id != LRG_PHYSICS_NO_BODY)
		return self->body_id;
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity == NULL)
		return LRG_PHYSICS_NO_BODY;
	if (!self->trigger)
	{
		LrgColliderComponent3D *owner;

		rb = lrg_entity3d_get_component (entity, LRG_TYPE_RIGID_BODY_COMPONENT);
		if (rb != NULL)
			return lrg_rigid_body_component_get_body_id (LRG_RIGID_BODY_COMPONENT (rb));
		owner = static_owner (entity);
		if (owner != NULL)
			return owner->body_id;
	}
	return LRG_PHYSICS_NO_BODY;
}

guint
lrg_collider_component3d_get_n_overlaps (LrgColliderComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), 0);
	return self->overlaps->len;
}

gboolean
lrg_collider_component3d_is_overlapping (LrgColliderComponent3D *self,
                                         LrgEntity3D            *entity)
{
	g_return_val_if_fail (LRG_IS_COLLIDER_COMPONENT3D (self), FALSE);
	return overlap_index (self, entity) >= 0;
}
