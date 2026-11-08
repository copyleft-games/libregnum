/* lrg-node-physics.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The `physics:` block of an LrgNode: LrgNodePhysics, LrgNodePhysicsShape
 * and LrgNodePhysicsJoint.
 *
 * The three types are plain property bags.  Their float and boolean
 * properties are described by small tables of (name, struct offset, range,
 * default) rows so that the GObject property code, copy and equality all
 * come from one place and cannot drift apart when a field is added.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-node-physics.h"

/* ==========================================================================
 * Table-driven float / boolean properties
 * ========================================================================== */

typedef struct
{
	const gchar *name;      /* property name */
	const gchar *blurb;     /* one-line description */
	gsize        offset;    /* offset of the gfloat / gboolean field */
	gboolean     is_bool;   /* gboolean field, else gfloat */
	gfloat       min;
	gfloat       max;
	gfloat       def;
} FieldRow;

#define FIELD_FLOAT(st, f, n, b, lo, hi, d) { n, b, G_STRUCT_OFFSET (st, f), FALSE, lo, hi, d }
#define FIELD_BOOL(st, f, n, b, d)          { n, b, G_STRUCT_OFFSET (st, f), TRUE, 0.0f, 1.0f, d }

#define FIELD_PTR_F(obj, row) ((gfloat *) G_STRUCT_MEMBER_P (obj, (row)->offset))
#define FIELD_PTR_B(obj, row) ((gboolean *) G_STRUCT_MEMBER_P (obj, (row)->offset))

/* Builds the GParamSpec of one row. */
static GParamSpec *
field_pspec (const FieldRow *row)
{
	GParamFlags flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	if (row->is_bool)
		return g_param_spec_boolean (row->name, NULL, row->blurb, row->def != 0.0f, flags);
	return g_param_spec_float (row->name, NULL, row->blurb, row->min, row->max, row->def, flags);
}

/* Writes the defaults of every row into @obj. */
static void
fields_init (gpointer        obj,
             const FieldRow *rows,
             guint           n_rows)
{
	guint i;

	for (i = 0; i < n_rows; i++)
	{
		if (rows[i].is_bool)
			*FIELD_PTR_B (obj, &rows[i]) = rows[i].def != 0.0f;
		else
			*FIELD_PTR_F (obj, &rows[i]) = rows[i].def;
	}
}

/* Copies every row's field from @src to @dst. */
static void
fields_copy (gpointer        dst,
             gconstpointer   src,
             const FieldRow *rows,
             guint           n_rows)
{
	guint i;

	for (i = 0; i < n_rows; i++)
	{
		if (rows[i].is_bool)
			*FIELD_PTR_B (dst, &rows[i]) = *FIELD_PTR_B (src, &rows[i]);
		else
			*FIELD_PTR_F (dst, &rows[i]) = *FIELD_PTR_F (src, &rows[i]);
	}
}

/* TRUE when every row's field is equal (booleans by truth value). */
static gboolean
fields_equal (gconstpointer   a,
              gconstpointer   b,
              const FieldRow *rows,
              guint           n_rows)
{
	guint i;

	for (i = 0; i < n_rows; i++)
	{
		if (rows[i].is_bool)
		{
			if (!*FIELD_PTR_B (a, &rows[i]) != !*FIELD_PTR_B (b, &rows[i]))
				return FALSE;
		}
		else if (*FIELD_PTR_F (a, &rows[i]) != *FIELD_PTR_F (b, &rows[i]))
			return FALSE;
	}
	return TRUE;
}

/* Reads row @index of @obj into @value. */
static void
field_get (gpointer        obj,
           const FieldRow *row,
           GValue         *value)
{
	if (row->is_bool)
		g_value_set_boolean (value, *FIELD_PTR_B (obj, row));
	else
		g_value_set_float (value, *FIELD_PTR_F (obj, row));
}

/* Writes @value into the row's field; notifies @pspec when it changed. */
static void
field_set (GObject        *obj,
           const FieldRow *row,
           const GValue   *value,
           GParamSpec     *pspec)
{
	if (row->is_bool)
	{
		gboolean v = g_value_get_boolean (value);

		if (!*FIELD_PTR_B (obj, row) == !v)
			return;
		*FIELD_PTR_B (obj, row) = v;
	}
	else
	{
		gfloat v = g_value_get_float (value);

		if (*FIELD_PTR_F (obj, row) == v)
			return;
		*FIELD_PTR_F (obj, row) = v;
	}
	g_object_notify_by_pspec (obj, pspec);
}

/* Sets one float field by property name through GObject (notifies). */
static void
set_float (gpointer     obj,
           const gchar *name,
           gfloat       v)
{
	g_object_set (obj, name, (gdouble) v, NULL);
}

/* Replaces a string field; returns TRUE when it changed. */
static gboolean
replace_string (gchar       **field,
                const gchar  *value)
{
	if (value != NULL && *value == '\0')
		value = NULL;
	if (g_strcmp0 (*field, value) == 0)
		return FALSE;
	g_free (*field);
	*field = g_strdup (value);
	return TRUE;
}

/* ==========================================================================
 * LrgNodePhysicsShape
 * ========================================================================== */

struct _LrgNodePhysicsShape
{
	GObject            parent_instance;

	LrgCollider3DKind  kind;
	gfloat             half_extents[3];
	gfloat             radius;
	gfloat             half_height;
	gfloat             offset[3];
	gfloat             rotation[3];
	gboolean           trigger;
	gfloat             friction;
	gfloat             restitution;
	gchar             *layer;
	gchar             *mesh;
};

G_DEFINE_FINAL_TYPE (LrgNodePhysicsShape, lrg_node_physics_shape, G_TYPE_OBJECT)

static const FieldRow shape_rows[] = {
	FIELD_FLOAT (LrgNodePhysicsShape, half_extents[0], "half-extent-x", "Box half extent X", 0.001f, G_MAXFLOAT, 0.5f),
	FIELD_FLOAT (LrgNodePhysicsShape, half_extents[1], "half-extent-y", "Box half extent Y", 0.001f, G_MAXFLOAT, 0.5f),
	FIELD_FLOAT (LrgNodePhysicsShape, half_extents[2], "half-extent-z", "Box half extent Z", 0.001f, G_MAXFLOAT, 0.5f),
	FIELD_FLOAT (LrgNodePhysicsShape, radius, "radius", "Sphere / capsule / cylinder radius", 0.001f, G_MAXFLOAT, 0.5f),
	FIELD_FLOAT (LrgNodePhysicsShape, half_height, "half-height", "Capsule / cylinder half height", 0.0f, G_MAXFLOAT, 0.5f),
	FIELD_FLOAT (LrgNodePhysicsShape, offset[0], "offset-x", "Local offset X", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsShape, offset[1], "offset-y", "Local offset Y", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsShape, offset[2], "offset-z", "Local offset Z", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsShape, rotation[0], "rotation-x", "Local rotation X (degrees)", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsShape, rotation[1], "rotation-y", "Local rotation Y (degrees)", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsShape, rotation[2], "rotation-z", "Local rotation Z (degrees)", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_BOOL (LrgNodePhysicsShape, trigger, "trigger", "Sensor shape", 0.0f),
	FIELD_FLOAT (LrgNodePhysicsShape, friction, "friction", "Friction override (negative: the body's)", -1.0f, G_MAXFLOAT, -1.0f),
	FIELD_FLOAT (LrgNodePhysicsShape, restitution, "restitution", "Restitution override (negative: the body's)", -1.0f, 1.0f, -1.0f)
};

enum
{
	SHAPE_PROP_0,
	SHAPE_PROP_KIND,
	SHAPE_PROP_LAYER,
	SHAPE_PROP_MESH,
	SHAPE_PROP_FIRST_FIELD
};

#define N_SHAPE_ROWS  (G_N_ELEMENTS (shape_rows))
#define N_SHAPE_PROPS (SHAPE_PROP_FIRST_FIELD + N_SHAPE_ROWS)

static GParamSpec *shape_props[N_SHAPE_PROPS];

static void
lrg_node_physics_shape_finalize (GObject *object)
{
	LrgNodePhysicsShape *self = LRG_NODE_PHYSICS_SHAPE (object);

	g_free (self->layer);
	g_free (self->mesh);
	G_OBJECT_CLASS (lrg_node_physics_shape_parent_class)->finalize (object);
}

static void
lrg_node_physics_shape_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgNodePhysicsShape *self = LRG_NODE_PHYSICS_SHAPE (object);

	switch (prop_id)
	{
	case SHAPE_PROP_KIND:
		g_value_set_enum (value, (gint) self->kind);
		break;
	case SHAPE_PROP_LAYER:
		g_value_set_string (value, self->layer);
		break;
	case SHAPE_PROP_MESH:
		g_value_set_string (value, self->mesh);
		break;
	default:
		if (prop_id >= SHAPE_PROP_FIRST_FIELD && prop_id < N_SHAPE_PROPS)
			field_get (self, &shape_rows[prop_id - SHAPE_PROP_FIRST_FIELD], value);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_physics_shape_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgNodePhysicsShape *self = LRG_NODE_PHYSICS_SHAPE (object);

	switch (prop_id)
	{
	case SHAPE_PROP_KIND:
		lrg_node_physics_shape_set_kind (self, (LrgCollider3DKind) g_value_get_enum (value));
		break;
	case SHAPE_PROP_LAYER:
		lrg_node_physics_shape_set_layer (self, g_value_get_string (value));
		break;
	case SHAPE_PROP_MESH:
		lrg_node_physics_shape_set_mesh (self, g_value_get_string (value));
		break;
	default:
		if (prop_id >= SHAPE_PROP_FIRST_FIELD && prop_id < N_SHAPE_PROPS)
			field_set (object, &shape_rows[prop_id - SHAPE_PROP_FIRST_FIELD], value, pspec);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_physics_shape_class_init (LrgNodePhysicsShapeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	guint         i;

	object_class->finalize = lrg_node_physics_shape_finalize;
	object_class->get_property = lrg_node_physics_shape_get_property;
	object_class->set_property = lrg_node_physics_shape_set_property;

	/**
	 * LrgNodePhysicsShape:kind:
	 *
	 * The shape kind.
	 */
	shape_props[SHAPE_PROP_KIND] =
		g_param_spec_enum ("kind", NULL, "Shape kind", LRG_TYPE_COLLIDER3D_KIND, LRG_COLLIDER3D_BOX, rw);

	/**
	 * LrgNodePhysicsShape:layer:
	 *
	 * Collision layer name, %NULL for the body's.
	 */
	shape_props[SHAPE_PROP_LAYER] = g_param_spec_string ("layer", NULL, "Collision layer", NULL, rw);

	/**
	 * LrgNodePhysicsShape:mesh:
	 *
	 * Mesh source of a convex / mesh shape, %NULL for the node's model.
	 */
	shape_props[SHAPE_PROP_MESH] = g_param_spec_string ("mesh", NULL, "Mesh source", NULL, rw);

	/**
	 * LrgNodePhysicsShape:half-extent-x:
	 *
	 * Box half extent along local X.
	 */
	/**
	 * LrgNodePhysicsShape:half-extent-y:
	 *
	 * Box half extent along local Y.
	 */
	/**
	 * LrgNodePhysicsShape:half-extent-z:
	 *
	 * Box half extent along local Z.
	 */
	/**
	 * LrgNodePhysicsShape:radius:
	 *
	 * Sphere, capsule or cylinder radius.
	 */
	/**
	 * LrgNodePhysicsShape:half-height:
	 *
	 * Capsule (cap centre distance / 2) or cylinder (height / 2) half height.
	 */
	/**
	 * LrgNodePhysicsShape:offset-x:
	 *
	 * Local offset X from the node origin.
	 */
	/**
	 * LrgNodePhysicsShape:offset-y:
	 *
	 * Local offset Y.
	 */
	/**
	 * LrgNodePhysicsShape:offset-z:
	 *
	 * Local offset Z.
	 */
	/**
	 * LrgNodePhysicsShape:rotation-x:
	 *
	 * Local rotation about X in degrees.
	 */
	/**
	 * LrgNodePhysicsShape:rotation-y:
	 *
	 * Local rotation about Y in degrees.
	 */
	/**
	 * LrgNodePhysicsShape:rotation-z:
	 *
	 * Local rotation about Z in degrees.
	 */
	/**
	 * LrgNodePhysicsShape:trigger:
	 *
	 * Whether the shape is a sensor.
	 */
	/**
	 * LrgNodePhysicsShape:friction:
	 *
	 * Friction override; negative uses the body's.
	 */
	/**
	 * LrgNodePhysicsShape:restitution:
	 *
	 * Restitution override; negative uses the body's.
	 */
	for (i = 0; i < N_SHAPE_ROWS; i++)
		shape_props[SHAPE_PROP_FIRST_FIELD + i] = field_pspec (&shape_rows[i]);

	g_object_class_install_properties (object_class, N_SHAPE_PROPS, shape_props);
}

static void
lrg_node_physics_shape_init (LrgNodePhysicsShape *self)
{
	self->kind = LRG_COLLIDER3D_BOX;
	fields_init (self, shape_rows, N_SHAPE_ROWS);
}

LrgNodePhysicsShape *
lrg_node_physics_shape_new (LrgCollider3DKind kind)
{
	LrgNodePhysicsShape *self = g_object_new (LRG_TYPE_NODE_PHYSICS_SHAPE, NULL);

	self->kind = kind;
	return self;
}

LrgNodePhysicsShape *
lrg_node_physics_shape_copy (LrgNodePhysicsShape *self)
{
	LrgNodePhysicsShape *copy;

	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), NULL);

	copy = lrg_node_physics_shape_new (self->kind);
	fields_copy (copy, self, shape_rows, N_SHAPE_ROWS);
	copy->layer = g_strdup (self->layer);
	copy->mesh = g_strdup (self->mesh);
	return copy;
}

gboolean
lrg_node_physics_shape_equal (LrgNodePhysicsShape *a,
                              LrgNodePhysicsShape *b)
{
	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;
	return a->kind == b->kind &&
	       g_strcmp0 (a->layer, b->layer) == 0 &&
	       g_strcmp0 (a->mesh, b->mesh) == 0 &&
	       fields_equal (a, b, shape_rows, N_SHAPE_ROWS);
}

LrgCollider3DKind
lrg_node_physics_shape_get_kind (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), LRG_COLLIDER3D_BOX);
	return self->kind;
}

void
lrg_node_physics_shape_set_kind (LrgNodePhysicsShape *self,
                                 LrgCollider3DKind    kind)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	if (self->kind == kind)
		return;
	self->kind = kind;
	g_object_notify_by_pspec (G_OBJECT (self), shape_props[SHAPE_PROP_KIND]);
}

void
lrg_node_physics_shape_get_half_extents (LrgNodePhysicsShape *self,
                                         gfloat              *out)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->half_extents, sizeof (self->half_extents));
}

void
lrg_node_physics_shape_set_half_extents (LrgNodePhysicsShape *self,
                                         gfloat               hx,
                                         gfloat               hy,
                                         gfloat               hz)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	set_float (self, "half-extent-x", MAX (hx, 0.001f));
	set_float (self, "half-extent-y", MAX (hy, 0.001f));
	set_float (self, "half-extent-z", MAX (hz, 0.001f));
}

gfloat
lrg_node_physics_shape_get_radius (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), 0.0f);
	return self->radius;
}

void
lrg_node_physics_shape_set_radius (LrgNodePhysicsShape *self,
                                   gfloat               radius)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	set_float (self, "radius", MAX (radius, 0.001f));
}

gfloat
lrg_node_physics_shape_get_half_height (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), 0.0f);
	return self->half_height;
}

void
lrg_node_physics_shape_set_half_height (LrgNodePhysicsShape *self,
                                        gfloat               half_height)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	set_float (self, "half-height", MAX (half_height, 0.0f));
}

void
lrg_node_physics_shape_get_offset (LrgNodePhysicsShape *self,
                                   gfloat              *out)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->offset, sizeof (self->offset));
}

void
lrg_node_physics_shape_set_offset (LrgNodePhysicsShape *self,
                                   gfloat               x,
                                   gfloat               y,
                                   gfloat               z)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	set_float (self, "offset-x", x);
	set_float (self, "offset-y", y);
	set_float (self, "offset-z", z);
}

void
lrg_node_physics_shape_get_rotation (LrgNodePhysicsShape *self,
                                     gfloat              *out)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->rotation, sizeof (self->rotation));
}

void
lrg_node_physics_shape_set_rotation (LrgNodePhysicsShape *self,
                                     gfloat               x,
                                     gfloat               y,
                                     gfloat               z)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	set_float (self, "rotation-x", x);
	set_float (self, "rotation-y", y);
	set_float (self, "rotation-z", z);
}

gboolean
lrg_node_physics_shape_get_trigger (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), FALSE);
	return self->trigger;
}

void
lrg_node_physics_shape_set_trigger (LrgNodePhysicsShape *self,
                                    gboolean             trigger)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	g_object_set (self, "trigger", trigger, NULL);
}

gfloat
lrg_node_physics_shape_get_friction (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), -1.0f);
	return self->friction;
}

void
lrg_node_physics_shape_set_friction (LrgNodePhysicsShape *self,
                                     gfloat               friction)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	set_float (self, "friction", friction < 0.0f ? -1.0f : friction);
}

gfloat
lrg_node_physics_shape_get_restitution (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), -1.0f);
	return self->restitution;
}

void
lrg_node_physics_shape_set_restitution (LrgNodePhysicsShape *self,
                                        gfloat               restitution)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	set_float (self, "restitution", restitution < 0.0f ? -1.0f : MIN (restitution, 1.0f));
}

const gchar *
lrg_node_physics_shape_get_layer (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), NULL);
	return self->layer;
}

void
lrg_node_physics_shape_set_layer (LrgNodePhysicsShape *self,
                                  const gchar         *layer)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	if (replace_string (&self->layer, layer))
		g_object_notify_by_pspec (G_OBJECT (self), shape_props[SHAPE_PROP_LAYER]);
}

const gchar *
lrg_node_physics_shape_get_mesh (LrgNodePhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self), NULL);
	return self->mesh;
}

void
lrg_node_physics_shape_set_mesh (LrgNodePhysicsShape *self,
                                 const gchar         *mesh)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	if (replace_string (&self->mesh, mesh))
		g_object_notify_by_pspec (G_OBJECT (self), shape_props[SHAPE_PROP_MESH]);
}

void
lrg_node_physics_shape_apply (LrgNodePhysicsShape    *self,
                              LrgColliderComponent3D *collider)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (self));
	g_return_if_fail (LRG_IS_COLLIDER_COMPONENT3D (collider));

	lrg_collider_component3d_set_kind (collider, self->kind);
	lrg_collider_component3d_set_half_extents (collider, self->half_extents[0],
	                                           self->half_extents[1], self->half_extents[2]);
	lrg_collider_component3d_set_radius (collider, self->radius);
	lrg_collider_component3d_set_half_height (collider, self->half_height);
	lrg_collider_component3d_set_offset (collider, self->offset[0], self->offset[1], self->offset[2]);
	lrg_collider_component3d_set_rotation (collider, self->rotation[0], self->rotation[1], self->rotation[2]);
	lrg_collider_component3d_set_trigger (collider, self->trigger);
	lrg_collider_component3d_set_friction (collider, self->friction);
	lrg_collider_component3d_set_restitution (collider, self->restitution);
	lrg_collider_component3d_set_layer (collider, self->layer);
	lrg_collider_component3d_set_mesh (collider, self->mesh);
}

/* ==========================================================================
 * LrgNodePhysicsJoint
 * ========================================================================== */

struct _LrgNodePhysicsJoint
{
	GObject                parent_instance;

	LrgJointKind  kind;
	gchar                 *target;
	gfloat                 anchor[3];
	gfloat                 axis[3];
	gboolean               limits_enabled;
	gfloat                 limit_min;
	gfloat                 limit_max;
	gfloat                 spring_frequency;
	gfloat                 spring_damping;
	gfloat                 motor_velocity;
	gfloat                 motor_max_force;
	gfloat                 break_force;
};

G_DEFINE_FINAL_TYPE (LrgNodePhysicsJoint, lrg_node_physics_joint, G_TYPE_OBJECT)

static const FieldRow joint_rows[] = {
	FIELD_FLOAT (LrgNodePhysicsJoint, anchor[0], "anchor-x", "Local anchor X", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, anchor[1], "anchor-y", "Local anchor Y", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, anchor[2], "anchor-z", "Local anchor Z", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, axis[0], "axis-x", "Local axis X", -1.0f, 1.0f, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, axis[1], "axis-y", "Local axis Y", -1.0f, 1.0f, 1.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, axis[2], "axis-z", "Local axis Z", -1.0f, 1.0f, 0.0f),
	FIELD_BOOL (LrgNodePhysicsJoint, limits_enabled, "limits-enabled", "Limits apply", 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, limit_min, "limit-min", "Lower limit", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, limit_max, "limit-max", "Upper limit", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, spring_frequency, "spring-frequency", "Spring frequency (Hz)", 0.0f, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, spring_damping, "spring-damping", "Spring damping ratio", 0.0f, G_MAXFLOAT, 1.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, motor_velocity, "motor-velocity", "Motor target velocity", -G_MAXFLOAT, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, motor_max_force, "motor-max-force", "Motor max force (0: off)", 0.0f, G_MAXFLOAT, 0.0f),
	FIELD_FLOAT (LrgNodePhysicsJoint, break_force, "break-force", "Break force (0: never)", 0.0f, G_MAXFLOAT, 0.0f)
};

enum
{
	JOINT_PROP_0,
	JOINT_PROP_KIND,
	JOINT_PROP_TARGET,
	JOINT_PROP_FIRST_FIELD
};

#define N_JOINT_ROWS  (G_N_ELEMENTS (joint_rows))
#define N_JOINT_PROPS (JOINT_PROP_FIRST_FIELD + N_JOINT_ROWS)

static GParamSpec *joint_props[N_JOINT_PROPS];

static void
lrg_node_physics_joint_finalize (GObject *object)
{
	LrgNodePhysicsJoint *self = LRG_NODE_PHYSICS_JOINT (object);

	g_free (self->target);
	G_OBJECT_CLASS (lrg_node_physics_joint_parent_class)->finalize (object);
}

static void
lrg_node_physics_joint_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgNodePhysicsJoint *self = LRG_NODE_PHYSICS_JOINT (object);

	switch (prop_id)
	{
	case JOINT_PROP_KIND:
		g_value_set_enum (value, (gint) self->kind);
		break;
	case JOINT_PROP_TARGET:
		g_value_set_string (value, self->target);
		break;
	default:
		if (prop_id >= JOINT_PROP_FIRST_FIELD && prop_id < N_JOINT_PROPS)
			field_get (self, &joint_rows[prop_id - JOINT_PROP_FIRST_FIELD], value);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_physics_joint_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgNodePhysicsJoint *self = LRG_NODE_PHYSICS_JOINT (object);

	switch (prop_id)
	{
	case JOINT_PROP_KIND:
		lrg_node_physics_joint_set_kind (self, (LrgJointKind) g_value_get_enum (value));
		break;
	case JOINT_PROP_TARGET:
		lrg_node_physics_joint_set_target (self, g_value_get_string (value));
		break;
	default:
		if (prop_id >= JOINT_PROP_FIRST_FIELD && prop_id < N_JOINT_PROPS)
			field_set (object, &joint_rows[prop_id - JOINT_PROP_FIRST_FIELD], value, pspec);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_physics_joint_class_init (LrgNodePhysicsJointClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	guint         i;

	object_class->finalize = lrg_node_physics_joint_finalize;
	object_class->get_property = lrg_node_physics_joint_get_property;
	object_class->set_property = lrg_node_physics_joint_set_property;

	/**
	 * LrgNodePhysicsJoint:kind:
	 *
	 * The joint type.
	 */
	joint_props[JOINT_PROP_KIND] =
		g_param_spec_enum ("kind", NULL, "Joint type", LRG_TYPE_JOINT_KIND,
		                   LRG_JOINT_POINT, rw);

	/**
	 * LrgNodePhysicsJoint:target:
	 *
	 * GUID of the target node, %NULL for the world.
	 */
	joint_props[JOINT_PROP_TARGET] = g_param_spec_string ("target", NULL, "Target node GUID", NULL, rw);

	/**
	 * LrgNodePhysicsJoint:anchor-x:
	 *
	 * Anchor X in the node's local space.
	 */
	/**
	 * LrgNodePhysicsJoint:anchor-y:
	 *
	 * Anchor Y in the node's local space.
	 */
	/**
	 * LrgNodePhysicsJoint:anchor-z:
	 *
	 * Anchor Z in the node's local space.
	 */
	/**
	 * LrgNodePhysicsJoint:axis-x:
	 *
	 * Axis X in the node's local space.
	 */
	/**
	 * LrgNodePhysicsJoint:axis-y:
	 *
	 * Axis Y in the node's local space.
	 */
	/**
	 * LrgNodePhysicsJoint:axis-z:
	 *
	 * Axis Z in the node's local space.
	 */
	/**
	 * LrgNodePhysicsJoint:limits-enabled:
	 *
	 * Whether the limits apply.
	 */
	/**
	 * LrgNodePhysicsJoint:limit-min:
	 *
	 * Lower limit (radians or distance).
	 */
	/**
	 * LrgNodePhysicsJoint:limit-max:
	 *
	 * Upper limit (radians or distance; the cone half angle).
	 */
	/**
	 * LrgNodePhysicsJoint:spring-frequency:
	 *
	 * Spring frequency in Hz, 0 for rigid.
	 */
	/**
	 * LrgNodePhysicsJoint:spring-damping:
	 *
	 * Spring damping ratio.
	 */
	/**
	 * LrgNodePhysicsJoint:motor-velocity:
	 *
	 * Motor target velocity.
	 */
	/**
	 * LrgNodePhysicsJoint:motor-max-force:
	 *
	 * Motor maximum force, 0 for off.
	 */
	/**
	 * LrgNodePhysicsJoint:break-force:
	 *
	 * Break force, 0 for unbreakable.
	 */
	for (i = 0; i < N_JOINT_ROWS; i++)
		joint_props[JOINT_PROP_FIRST_FIELD + i] = field_pspec (&joint_rows[i]);

	g_object_class_install_properties (object_class, N_JOINT_PROPS, joint_props);
}

static void
lrg_node_physics_joint_init (LrgNodePhysicsJoint *self)
{
	self->kind = LRG_JOINT_POINT;
	fields_init (self, joint_rows, N_JOINT_ROWS);
}

LrgNodePhysicsJoint *
lrg_node_physics_joint_new (LrgJointKind kind)
{
	LrgNodePhysicsJoint *self = g_object_new (LRG_TYPE_NODE_PHYSICS_JOINT, NULL);

	self->kind = kind;
	return self;
}

LrgNodePhysicsJoint *
lrg_node_physics_joint_copy (LrgNodePhysicsJoint *self)
{
	LrgNodePhysicsJoint *copy;

	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self), NULL);

	copy = lrg_node_physics_joint_new (self->kind);
	fields_copy (copy, self, joint_rows, N_JOINT_ROWS);
	copy->target = g_strdup (self->target);
	return copy;
}

gboolean
lrg_node_physics_joint_equal (LrgNodePhysicsJoint *a,
                              LrgNodePhysicsJoint *b)
{
	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;
	return a->kind == b->kind &&
	       g_strcmp0 (a->target, b->target) == 0 &&
	       fields_equal (a, b, joint_rows, N_JOINT_ROWS);
}

LrgJointKind
lrg_node_physics_joint_get_kind (LrgNodePhysicsJoint *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self), LRG_JOINT_POINT);
	return self->kind;
}

void
lrg_node_physics_joint_set_kind (LrgNodePhysicsJoint   *self,
                                 LrgJointKind  kind)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	if (self->kind == kind)
		return;
	self->kind = kind;
	g_object_notify_by_pspec (G_OBJECT (self), joint_props[JOINT_PROP_KIND]);
}

const gchar *
lrg_node_physics_joint_get_target (LrgNodePhysicsJoint *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self), NULL);
	return self->target;
}

void
lrg_node_physics_joint_set_target (LrgNodePhysicsJoint *self,
                                   const gchar         *guid)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	if (replace_string (&self->target, guid))
		g_object_notify_by_pspec (G_OBJECT (self), joint_props[JOINT_PROP_TARGET]);
}

void
lrg_node_physics_joint_get_anchor (LrgNodePhysicsJoint *self,
                                   gfloat              *out)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->anchor, sizeof (self->anchor));
}

void
lrg_node_physics_joint_set_anchor (LrgNodePhysicsJoint *self,
                                   gfloat               x,
                                   gfloat               y,
                                   gfloat               z)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	set_float (self, "anchor-x", x);
	set_float (self, "anchor-y", y);
	set_float (self, "anchor-z", z);
}

void
lrg_node_physics_joint_get_axis (LrgNodePhysicsJoint *self,
                                 gfloat              *out)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->axis, sizeof (self->axis));
}

void
lrg_node_physics_joint_set_axis (LrgNodePhysicsJoint *self,
                                 gfloat               x,
                                 gfloat               y,
                                 gfloat               z)
{
	gfloat len;

	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));

	/* a zero axis is ignored; the stored axis is unit length */
	len = sqrtf (x * x + y * y + z * z);
	if (!(len > 1e-6f))
		return;
	set_float (self, "axis-x", x / len);
	set_float (self, "axis-y", y / len);
	set_float (self, "axis-z", z / len);
}

gboolean
lrg_node_physics_joint_get_limits (LrgNodePhysicsJoint *self,
                                   gfloat              *min,
                                   gfloat              *max)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self), FALSE);
	if (min != NULL)
		*min = self->limit_min;
	if (max != NULL)
		*max = self->limit_max;
	return self->limits_enabled;
}

void
lrg_node_physics_joint_set_limits (LrgNodePhysicsJoint *self,
                                   gboolean             enabled,
                                   gfloat               min,
                                   gfloat               max)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	g_object_set (self, "limits-enabled", enabled, NULL);
	set_float (self, "limit-min", min);
	set_float (self, "limit-max", max);
}

void
lrg_node_physics_joint_get_spring (LrgNodePhysicsJoint *self,
                                   gfloat              *frequency,
                                   gfloat              *damping)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	if (frequency != NULL)
		*frequency = self->spring_frequency;
	if (damping != NULL)
		*damping = self->spring_damping;
}

void
lrg_node_physics_joint_set_spring (LrgNodePhysicsJoint *self,
                                   gfloat               frequency,
                                   gfloat               damping)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	set_float (self, "spring-frequency", MAX (frequency, 0.0f));
	set_float (self, "spring-damping", MAX (damping, 0.0f));
}

void
lrg_node_physics_joint_get_motor (LrgNodePhysicsJoint *self,
                                  gfloat              *velocity,
                                  gfloat              *max_force)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	if (velocity != NULL)
		*velocity = self->motor_velocity;
	if (max_force != NULL)
		*max_force = self->motor_max_force;
}

void
lrg_node_physics_joint_set_motor (LrgNodePhysicsJoint *self,
                                  gfloat               velocity,
                                  gfloat               max_force)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	set_float (self, "motor-velocity", velocity);
	set_float (self, "motor-max-force", MAX (max_force, 0.0f));
}

gfloat
lrg_node_physics_joint_get_break_force (LrgNodePhysicsJoint *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self), 0.0f);
	return self->break_force;
}

void
lrg_node_physics_joint_set_break_force (LrgNodePhysicsJoint *self,
                                        gfloat               force)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	set_float (self, "break-force", MAX (force, 0.0f));
}

void
lrg_node_physics_joint_apply (LrgNodePhysicsJoint *self,
                              LrgJointComponent   *joint)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (self));
	g_return_if_fail (LRG_IS_JOINT_COMPONENT (joint));

	lrg_joint_component_set_kind (joint, self->kind);
	lrg_joint_component_set_target_guid (joint, self->target);
	lrg_joint_component_set_anchor (joint, self->anchor[0], self->anchor[1], self->anchor[2]);
	lrg_joint_component_set_axis (joint, self->axis[0], self->axis[1], self->axis[2]);
	lrg_joint_component_set_limits (joint, self->limits_enabled, self->limit_min, self->limit_max);
	lrg_joint_component_set_spring (joint, self->spring_frequency, self->spring_damping);
	lrg_joint_component_set_motor (joint, self->motor_velocity, self->motor_max_force);
	lrg_joint_component_set_break_force (joint, self->break_force);
}

/* ==========================================================================
 * LrgNodePhysics
 * ========================================================================== */

struct _LrgNodePhysics
{
	GObject              parent_instance;

	LrgDynamicsBodyKind  body_kind;
	gfloat               mass;
	gchar               *layer;
	gfloat               friction;
	gfloat               restitution;
	gfloat               linear_damping;
	gfloat               angular_damping;
	gfloat               gravity_factor;
	gboolean             ccd;

	GPtrArray           *shapes;   /* LrgNodePhysicsShape, owned refs */
	GPtrArray           *joints;   /* LrgNodePhysicsJoint, owned refs */
};

G_DEFINE_FINAL_TYPE (LrgNodePhysics, lrg_node_physics, G_TYPE_OBJECT)

static const FieldRow body_rows[] = {
	FIELD_FLOAT (LrgNodePhysics, mass, "mass", "Mass (kg)", 0.001f, G_MAXFLOAT, 1.0f),
	FIELD_FLOAT (LrgNodePhysics, friction, "friction", "Friction", 0.0f, G_MAXFLOAT, 0.5f),
	FIELD_FLOAT (LrgNodePhysics, restitution, "restitution", "Restitution", 0.0f, 1.0f, 0.0f),
	FIELD_FLOAT (LrgNodePhysics, linear_damping, "linear-damping", "Linear damping", 0.0f, G_MAXFLOAT, 0.05f),
	FIELD_FLOAT (LrgNodePhysics, angular_damping, "angular-damping", "Angular damping", 0.0f, G_MAXFLOAT, 0.05f),
	FIELD_FLOAT (LrgNodePhysics, gravity_factor, "gravity-factor", "Gravity factor", -G_MAXFLOAT, G_MAXFLOAT, 1.0f),
	FIELD_BOOL (LrgNodePhysics, ccd, "ccd", "Continuous collision detection", 0.0f)
};

enum
{
	BODY_PROP_0,
	BODY_PROP_BODY_KIND,
	BODY_PROP_LAYER,
	BODY_PROP_FIRST_FIELD
};

#define N_BODY_ROWS  (G_N_ELEMENTS (body_rows))
#define N_BODY_PROPS (BODY_PROP_FIRST_FIELD + N_BODY_ROWS)

static GParamSpec *body_props[N_BODY_PROPS];

static void
lrg_node_physics_finalize (GObject *object)
{
	LrgNodePhysics *self = LRG_NODE_PHYSICS (object);

	g_free (self->layer);
	g_clear_pointer (&self->shapes, g_ptr_array_unref);
	g_clear_pointer (&self->joints, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_node_physics_parent_class)->finalize (object);
}

static void
lrg_node_physics_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgNodePhysics *self = LRG_NODE_PHYSICS (object);

	switch (prop_id)
	{
	case BODY_PROP_BODY_KIND:
		g_value_set_enum (value, (gint) self->body_kind);
		break;
	case BODY_PROP_LAYER:
		g_value_set_string (value, self->layer);
		break;
	default:
		if (prop_id >= BODY_PROP_FIRST_FIELD && prop_id < N_BODY_PROPS)
			field_get (self, &body_rows[prop_id - BODY_PROP_FIRST_FIELD], value);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_physics_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgNodePhysics *self = LRG_NODE_PHYSICS (object);

	switch (prop_id)
	{
	case BODY_PROP_BODY_KIND:
		lrg_node_physics_set_body_kind (self, (LrgDynamicsBodyKind) g_value_get_enum (value));
		break;
	case BODY_PROP_LAYER:
		lrg_node_physics_set_layer (self, g_value_get_string (value));
		break;
	default:
		if (prop_id >= BODY_PROP_FIRST_FIELD && prop_id < N_BODY_PROPS)
			field_set (object, &body_rows[prop_id - BODY_PROP_FIRST_FIELD], value, pspec);
		else
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_physics_class_init (LrgNodePhysicsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	guint         i;

	object_class->finalize = lrg_node_physics_finalize;
	object_class->get_property = lrg_node_physics_get_property;
	object_class->set_property = lrg_node_physics_set_property;

	/**
	 * LrgNodePhysics:body-kind:
	 *
	 * Static, kinematic or dynamic.
	 */
	body_props[BODY_PROP_BODY_KIND] =
		g_param_spec_enum ("body-kind", NULL, "Body kind", LRG_TYPE_DYNAMICS_BODY_KIND,
		                   LRG_DYNAMICS_BODY_STATIC, rw);

	/**
	 * LrgNodePhysics:layer:
	 *
	 * Collision layer name, %NULL for the default layer.
	 */
	body_props[BODY_PROP_LAYER] = g_param_spec_string ("layer", NULL, "Collision layer", NULL, rw);

	/**
	 * LrgNodePhysics:mass:
	 *
	 * Mass in kg (dynamic bodies).
	 */
	/**
	 * LrgNodePhysics:friction:
	 *
	 * Friction coefficient.
	 */
	/**
	 * LrgNodePhysics:restitution:
	 *
	 * Restitution (bounciness).
	 */
	/**
	 * LrgNodePhysics:linear-damping:
	 *
	 * Linear damping per second.
	 */
	/**
	 * LrgNodePhysics:angular-damping:
	 *
	 * Angular damping per second.
	 */
	/**
	 * LrgNodePhysics:gravity-factor:
	 *
	 * Multiplier on the world gravity.
	 */
	/**
	 * LrgNodePhysics:ccd:
	 *
	 * Continuous collision detection.
	 */
	for (i = 0; i < N_BODY_ROWS; i++)
		body_props[BODY_PROP_FIRST_FIELD + i] = field_pspec (&body_rows[i]);

	g_object_class_install_properties (object_class, N_BODY_PROPS, body_props);
}

static void
lrg_node_physics_init (LrgNodePhysics *self)
{
	self->body_kind = LRG_DYNAMICS_BODY_STATIC;
	fields_init (self, body_rows, N_BODY_ROWS);
	self->shapes = g_ptr_array_new_with_free_func (g_object_unref);
	self->joints = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgNodePhysics *
lrg_node_physics_new (LrgDynamicsBodyKind kind)
{
	LrgNodePhysics *self = g_object_new (LRG_TYPE_NODE_PHYSICS, NULL);

	self->body_kind = kind;
	return self;
}

LrgNodePhysics *
lrg_node_physics_copy (LrgNodePhysics *self)
{
	LrgNodePhysics *copy;
	guint           i;

	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), NULL);

	copy = lrg_node_physics_new (self->body_kind);
	fields_copy (copy, self, body_rows, N_BODY_ROWS);
	copy->layer = g_strdup (self->layer);
	for (i = 0; i < self->shapes->len; i++)
		g_ptr_array_add (copy->shapes, lrg_node_physics_shape_copy (g_ptr_array_index (self->shapes, i)));
	for (i = 0; i < self->joints->len; i++)
		g_ptr_array_add (copy->joints, lrg_node_physics_joint_copy (g_ptr_array_index (self->joints, i)));
	return copy;
}

gboolean
lrg_node_physics_equal (LrgNodePhysics *a,
                        LrgNodePhysics *b)
{
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;
	if (a->body_kind != b->body_kind || g_strcmp0 (a->layer, b->layer) != 0 ||
	    !fields_equal (a, b, body_rows, N_BODY_ROWS) ||
	    a->shapes->len != b->shapes->len || a->joints->len != b->joints->len)
		return FALSE;
	for (i = 0; i < a->shapes->len; i++)
		if (!lrg_node_physics_shape_equal (g_ptr_array_index (a->shapes, i), g_ptr_array_index (b->shapes, i)))
			return FALSE;
	for (i = 0; i < a->joints->len; i++)
		if (!lrg_node_physics_joint_equal (g_ptr_array_index (a->joints, i), g_ptr_array_index (b->joints, i)))
			return FALSE;
	return TRUE;
}

LrgDynamicsBodyKind
lrg_node_physics_get_body_kind (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), LRG_DYNAMICS_BODY_STATIC);
	return self->body_kind;
}

void
lrg_node_physics_set_body_kind (LrgNodePhysics      *self,
                                LrgDynamicsBodyKind  kind)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	if (self->body_kind == kind)
		return;
	self->body_kind = kind;
	g_object_notify_by_pspec (G_OBJECT (self), body_props[BODY_PROP_BODY_KIND]);
}

gfloat
lrg_node_physics_get_mass (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), 1.0f);
	return self->mass;
}

void
lrg_node_physics_set_mass (LrgNodePhysics *self,
                           gfloat          mass)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	set_float (self, "mass", MAX (mass, 0.001f));
}

const gchar *
lrg_node_physics_get_layer (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), NULL);
	return self->layer;
}

void
lrg_node_physics_set_layer (LrgNodePhysics *self,
                            const gchar    *layer)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	if (replace_string (&self->layer, layer))
		g_object_notify_by_pspec (G_OBJECT (self), body_props[BODY_PROP_LAYER]);
}

gfloat
lrg_node_physics_get_friction (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), 0.5f);
	return self->friction;
}

void
lrg_node_physics_set_friction (LrgNodePhysics *self,
                               gfloat          friction)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	set_float (self, "friction", MAX (friction, 0.0f));
}

gfloat
lrg_node_physics_get_restitution (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), 0.0f);
	return self->restitution;
}

void
lrg_node_physics_set_restitution (LrgNodePhysics *self,
                                  gfloat          restitution)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	set_float (self, "restitution", CLAMP (restitution, 0.0f, 1.0f));
}

void
lrg_node_physics_get_damping (LrgNodePhysics *self,
                              gfloat         *linear,
                              gfloat         *angular)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	if (linear != NULL)
		*linear = self->linear_damping;
	if (angular != NULL)
		*angular = self->angular_damping;
}

void
lrg_node_physics_set_damping (LrgNodePhysics *self,
                              gfloat          linear,
                              gfloat          angular)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	set_float (self, "linear-damping", MAX (linear, 0.0f));
	set_float (self, "angular-damping", MAX (angular, 0.0f));
}

gfloat
lrg_node_physics_get_gravity_factor (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), 1.0f);
	return self->gravity_factor;
}

void
lrg_node_physics_set_gravity_factor (LrgNodePhysics *self,
                                     gfloat          factor)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	set_float (self, "gravity-factor", factor);
}

gboolean
lrg_node_physics_get_ccd (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), FALSE);
	return self->ccd;
}

void
lrg_node_physics_set_ccd (LrgNodePhysics *self,
                          gboolean        ccd)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	g_object_set (self, "ccd", ccd, NULL);
}

GPtrArray *
lrg_node_physics_get_shapes (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), NULL);
	return self->shapes;
}

void
lrg_node_physics_add_shape (LrgNodePhysics      *self,
                            LrgNodePhysicsShape *shape)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (shape));
	g_ptr_array_add (self->shapes, g_object_ref (shape));
}

gboolean
lrg_node_physics_remove_shape_at (LrgNodePhysics *self,
                                  guint           index)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), FALSE);
	if (index >= self->shapes->len)
		return FALSE;
	g_ptr_array_remove_index (self->shapes, index);
	return TRUE;
}

GPtrArray *
lrg_node_physics_get_joints (LrgNodePhysics *self)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), NULL);
	return self->joints;
}

void
lrg_node_physics_add_joint (LrgNodePhysics      *self,
                            LrgNodePhysicsJoint *joint)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	g_return_if_fail (LRG_IS_NODE_PHYSICS_JOINT (joint));
	g_ptr_array_add (self->joints, g_object_ref (joint));
}

gboolean
lrg_node_physics_remove_joint_at (LrgNodePhysics *self,
                                  guint           index)
{
	g_return_val_if_fail (LRG_IS_NODE_PHYSICS (self), FALSE);
	if (index >= self->joints->len)
		return FALSE;
	g_ptr_array_remove_index (self->joints, index);
	return TRUE;
}

void
lrg_node_physics_apply (LrgNodePhysics        *self,
                        LrgRigidBodyComponent *body)
{
	g_return_if_fail (LRG_IS_NODE_PHYSICS (self));
	g_return_if_fail (LRG_IS_RIGID_BODY_COMPONENT (body));

	lrg_rigid_body_component_set_body_kind (body, self->body_kind);
	lrg_rigid_body_component_set_mass (body, self->mass);
	lrg_rigid_body_component_set_layer (body, self->layer);
	lrg_rigid_body_component_set_friction (body, self->friction);
	lrg_rigid_body_component_set_restitution (body, self->restitution);
	lrg_rigid_body_component_set_damping (body, self->linear_damping, self->angular_damping);
	lrg_rigid_body_component_set_gravity_factor (body, self->gravity_factor);
	lrg_rigid_body_component_set_ccd (body, self->ccd);
}
