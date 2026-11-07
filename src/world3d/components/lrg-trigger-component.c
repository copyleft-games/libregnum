/* lrg-trigger-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTriggerComponent: overlap tests of entity positions against a box or
 * sphere volume every fixed step, with enter/exit signals.
 */

#include "config.h"

#include <math.h>

#include "lrg-trigger-component.h"
#include "../lrg-entity3d.h"

GType
lrg_trigger_shape_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TRIGGER_SHAPE_BOX, "LRG_TRIGGER_SHAPE_BOX", "box" },
			{ LRG_TRIGGER_SHAPE_SPHERE, "LRG_TRIGGER_SHAPE_SPHERE", "sphere" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTriggerShape"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

struct _LrgTriggerComponent
{
	LrgComponent     parent_instance;

	LrgTriggerShape  shape;
	gfloat           size[3];
	gfloat           radius;
	gchar           *filter_tag;
	gchar           *filter_component;
	gboolean         one_shot;
	gboolean         fired;

	GPtrArray       *inside;    /* LrgEntity3D, owned refs */
};

G_DEFINE_FINAL_TYPE (LrgTriggerComponent, lrg_trigger_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_SHAPE,
	PROP_SIZE_X,
	PROP_SIZE_Y,
	PROP_SIZE_Z,
	PROP_RADIUS,
	PROP_FILTER_TAG,
	PROP_FILTER_COMPONENT,
	PROP_ONE_SHOT,
	N_PROPS
};

enum
{
	SIGNAL_ENTERED,
	SIGNAL_EXITED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* The volume's local half extents (a sphere's are its radius). */
static void
local_half_extents (LrgTriggerComponent *self,
                    gfloat              *out)
{
	if (self->shape == LRG_TRIGGER_SHAPE_SPHERE)
	{
		out[0] = out[1] = out[2] = self->radius;
		return;
	}
	out[0] = self->size[0] * 0.5f;
	out[1] = self->size[1] * 0.5f;
	out[2] = self->size[2] * 0.5f;
}

/* World AABB of the volume: centre +- |M3x3| * half extents. */
static void
world_aabb (LrgTriggerComponent *self,
            LrgEntity3D         *entity,
            LrgBoundingBox3D    *out)
{
	const gfloat *m = lrg_entity3d_peek_world_matrix (entity);
	gfloat        h[3];
	gfloat        e[3];
	guint         row;

	local_half_extents (self, h);
	for (row = 0; row < 3; row++)
		e[row] = fabsf (m[0 + row]) * h[0] + fabsf (m[4 + row]) * h[1] + fabsf (m[8 + row]) * h[2];
	out->min.x = m[12] - e[0];
	out->min.y = m[13] - e[1];
	out->min.z = m[14] - e[2];
	out->max.x = m[12] + e[0];
	out->max.y = m[13] + e[1];
	out->max.z = m[14] + e[2];
}

/* Index of @entity in the inside set, or -1. */
static gint
find_inside (LrgTriggerComponent *self,
             LrgEntity3D         *entity)
{
	guint i;

	for (i = 0; i < self->inside->len; i++)
		if (g_ptr_array_index (self->inside, i) == entity)
			return (gint) i;
	return -1;
}

/* The candidates to test this step: by tag, by component type, or every
 * entity whose bounds touch the volume's world box. */
static GPtrArray *
gather_candidates (LrgTriggerComponent *self,
                   LrgWorld3D          *world,
                   LrgEntity3D         *own)
{
	GPtrArray *candidates;
	GType      filter_type = G_TYPE_INVALID;
	guint      i;

	if (self->filter_component != NULL)
	{
		filter_type = g_type_from_name (self->filter_component);
		if (filter_type == G_TYPE_INVALID || !g_type_is_a (filter_type, LRG_TYPE_COMPONENT))
			return g_ptr_array_new ();
	}

	if (self->filter_tag != NULL)
		candidates = lrg_world3d_find_by_tag (world, self->filter_tag);
	else if (filter_type != G_TYPE_INVALID)
		candidates = lrg_world3d_find_with_component (world, filter_type);
	else
	{
		LrgBoundingBox3D box;

		world_aabb (self, own, &box);
		candidates = lrg_world3d_query_box (world, &box);
	}

	/* Both filters: the tag list further needs the component */
	for (i = candidates->len; i > 0; i--)
	{
		LrgEntity3D *e = (LrgEntity3D *) g_ptr_array_index (candidates, i - 1);

		if (e == own || !lrg_entity3d_is_active_in_hierarchy (e) ||
		    (filter_type != G_TYPE_INVALID && !lrg_entity3d_has_component (e, filter_type)))
			g_ptr_array_remove_index (candidates, i - 1);
	}
	return candidates;
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static void
lrg_trigger_component_fixed_update (LrgComponent *component,
                                    gfloat        step)
{
	(void) step;
	lrg_trigger_component_evaluate (LRG_TRIGGER_COMPONENT (component));
}

/* Leaving the world forgets the occupants (no exit signals: the trigger
 * itself is gone, not the entities). */
static void
lrg_trigger_component_world_exited (LrgComponent *component,
                                    LrgWorld3D   *world)
{
	(void) world;
	g_ptr_array_set_size (LRG_TRIGGER_COMPONENT (component)->inside, 0);
}

static gboolean
lrg_trigger_component_get_bounds (LrgComponent     *component,
                                  LrgBoundingBox3D *out_local)
{
	gfloat h[3];

	local_half_extents (LRG_TRIGGER_COMPONENT (component), h);
	out_local->min.x = -h[0];
	out_local->min.y = -h[1];
	out_local->min.z = -h[2];
	out_local->max.x = h[0];
	out_local->max.y = h[1];
	out_local->max.z = h[2];
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_trigger_component_dispose (GObject *object)
{
	LrgTriggerComponent *self = LRG_TRIGGER_COMPONENT (object);

	if (self->inside != NULL)
		g_ptr_array_set_size (self->inside, 0);

	G_OBJECT_CLASS (lrg_trigger_component_parent_class)->dispose (object);
}

static void
lrg_trigger_component_finalize (GObject *object)
{
	LrgTriggerComponent *self = LRG_TRIGGER_COMPONENT (object);

	g_free (self->filter_tag);
	g_free (self->filter_component);
	g_ptr_array_unref (self->inside);

	G_OBJECT_CLASS (lrg_trigger_component_parent_class)->finalize (object);
}

/* Stores a float and notifies when it changed. */
static void
set_float (LrgTriggerComponent *self,
           gfloat              *slot,
           gfloat               value,
           guint                prop)
{
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* Replaces a string and notifies when it changed. */
static void
set_string (LrgTriggerComponent  *self,
            gchar               **slot,
            const gchar          *value,
            guint                 prop)
{
	if (value != NULL && *value == '\0')
		value = NULL;
	if (g_strcmp0 (*slot, value) == 0)
		return;
	g_free (*slot);
	*slot = g_strdup (value);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_trigger_component_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgTriggerComponent *self = LRG_TRIGGER_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SHAPE:            g_value_set_enum (value, (gint) self->shape); break;
	case PROP_SIZE_X:           g_value_set_float (value, self->size[0]); break;
	case PROP_SIZE_Y:           g_value_set_float (value, self->size[1]); break;
	case PROP_SIZE_Z:           g_value_set_float (value, self->size[2]); break;
	case PROP_RADIUS:           g_value_set_float (value, self->radius); break;
	case PROP_FILTER_TAG:       g_value_set_string (value, self->filter_tag); break;
	case PROP_FILTER_COMPONENT: g_value_set_string (value, self->filter_component); break;
	case PROP_ONE_SHOT:         g_value_set_boolean (value, self->one_shot); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_trigger_component_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgTriggerComponent *self = LRG_TRIGGER_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SHAPE:
		lrg_trigger_component_set_shape (self, (LrgTriggerShape) g_value_get_enum (value));
		break;
	case PROP_SIZE_X: set_float (self, &self->size[0], g_value_get_float (value), prop_id); break;
	case PROP_SIZE_Y: set_float (self, &self->size[1], g_value_get_float (value), prop_id); break;
	case PROP_SIZE_Z: set_float (self, &self->size[2], g_value_get_float (value), prop_id); break;
	case PROP_RADIUS: set_float (self, &self->radius, g_value_get_float (value), prop_id); break;
	case PROP_FILTER_TAG:
		set_string (self, &self->filter_tag, g_value_get_string (value), prop_id);
		break;
	case PROP_FILTER_COMPONENT:
		set_string (self, &self->filter_component, g_value_get_string (value), prop_id);
		break;
	case PROP_ONE_SHOT:
		if (self->one_shot != g_value_get_boolean (value))
		{
			self->one_shot = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_trigger_component_class_init (LrgTriggerComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_trigger_component_dispose;
	object_class->finalize = lrg_trigger_component_finalize;
	object_class->get_property = lrg_trigger_component_get_property;
	object_class->set_property = lrg_trigger_component_set_property;

	component_class->fixed_update = lrg_trigger_component_fixed_update;
	component_class->world_exited = lrg_trigger_component_world_exited;
	component_class->get_bounds = lrg_trigger_component_get_bounds;

	/**
	 * LrgTriggerComponent:shape:
	 *
	 * Box or sphere.
	 */
	properties[PROP_SHAPE] =
		g_param_spec_enum ("shape", NULL, "Volume shape", LRG_TYPE_TRIGGER_SHAPE,
		                   LRG_TRIGGER_SHAPE_BOX, rw);

	/**
	 * LrgTriggerComponent:size-x:
	 *
	 * Box width (local units).
	 */
	properties[PROP_SIZE_X] = g_param_spec_float ("size-x", NULL, "Box width", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgTriggerComponent:size-y:
	 *
	 * Box height (local units).
	 */
	properties[PROP_SIZE_Y] = g_param_spec_float ("size-y", NULL, "Box height", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgTriggerComponent:size-z:
	 *
	 * Box depth (local units).
	 */
	properties[PROP_SIZE_Z] = g_param_spec_float ("size-z", NULL, "Box depth", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgTriggerComponent:radius:
	 *
	 * Sphere radius (local units).
	 */
	properties[PROP_RADIUS] = g_param_spec_float ("radius", NULL, "Sphere radius", 0.0f, G_MAXFLOAT,
	                                              0.5f, rw);

	/**
	 * LrgTriggerComponent:filter-tag:
	 *
	 * Only entities with this tag trigger (%NULL: no tag filter).
	 */
	properties[PROP_FILTER_TAG] =
		g_param_spec_string ("filter-tag", NULL, "Required entity tag", NULL, rw);

	/**
	 * LrgTriggerComponent:filter-component:
	 *
	 * Only entities with a component of this type name trigger (%NULL: no
	 * component filter).
	 */
	properties[PROP_FILTER_COMPONENT] =
		g_param_spec_string ("filter-component", NULL, "Required component type name", NULL, rw);

	/**
	 * LrgTriggerComponent:one-shot:
	 *
	 * Stops testing after the first entry (until
	 * lrg_trigger_component_reset()).
	 */
	properties[PROP_ONE_SHOT] = g_param_spec_boolean ("one-shot", NULL, "Fire once", FALSE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTriggerComponent::entered:
	 * @self: the trigger
	 * @entity: the entity whose world position entered the volume
	 *
	 * Emitted from the fixed update in which @entity is first found inside.
	 */
	signals[SIGNAL_ENTERED] =
		g_signal_new ("entered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);

	/**
	 * LrgTriggerComponent::exited:
	 * @self: the trigger
	 * @entity: the entity that left the volume (or stopped matching the
	 *   filters, or left the world)
	 *
	 * Emitted from the fixed update in which @entity is no longer inside.
	 */
	signals[SIGNAL_EXITED] =
		g_signal_new ("exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);
}

static void
lrg_trigger_component_init (LrgTriggerComponent *self)
{
	self->shape = LRG_TRIGGER_SHAPE_BOX;
	self->size[0] = self->size[1] = self->size[2] = 1.0f;
	self->radius = 0.5f;
	self->inside = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTriggerComponent *
lrg_trigger_component_new (void)
{
	return (LrgTriggerComponent *) g_object_new (LRG_TYPE_TRIGGER_COMPONENT, NULL);
}

LrgTriggerComponent *
lrg_trigger_component_new_box (gfloat size_x,
                               gfloat size_y,
                               gfloat size_z)
{
	return (LrgTriggerComponent *) g_object_new (LRG_TYPE_TRIGGER_COMPONENT,
	                                             "shape", LRG_TRIGGER_SHAPE_BOX,
	                                             "size-x", MAX (size_x, 0.0f),
	                                             "size-y", MAX (size_y, 0.0f),
	                                             "size-z", MAX (size_z, 0.0f), NULL);
}

LrgTriggerComponent *
lrg_trigger_component_new_sphere (gfloat radius)
{
	return (LrgTriggerComponent *) g_object_new (LRG_TYPE_TRIGGER_COMPONENT,
	                                             "shape", LRG_TRIGGER_SHAPE_SPHERE,
	                                             "radius", MAX (radius, 0.0f), NULL);
}

LrgTriggerShape
lrg_trigger_component_get_shape (LrgTriggerComponent *self)
{
	g_return_val_if_fail (LRG_IS_TRIGGER_COMPONENT (self), LRG_TRIGGER_SHAPE_BOX);

	return self->shape;
}

void
lrg_trigger_component_set_shape (LrgTriggerComponent *self,
                                 LrgTriggerShape      shape)
{
	g_return_if_fail (LRG_IS_TRIGGER_COMPONENT (self));

	if (self->shape == shape)
		return;
	self->shape = shape;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE]);
}

void
lrg_trigger_component_set_size (LrgTriggerComponent *self,
                                gfloat               x,
                                gfloat               y,
                                gfloat               z)
{
	g_return_if_fail (LRG_IS_TRIGGER_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->size[0], MAX (x, 0.0f), PROP_SIZE_X);
	set_float (self, &self->size[1], MAX (y, 0.0f), PROP_SIZE_Y);
	set_float (self, &self->size[2], MAX (z, 0.0f), PROP_SIZE_Z);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_trigger_component_set_radius (LrgTriggerComponent *self,
                                  gfloat               radius)
{
	g_return_if_fail (LRG_IS_TRIGGER_COMPONENT (self));

	set_float (self, &self->radius, MAX (radius, 0.0f), PROP_RADIUS);
}

const gchar *
lrg_trigger_component_get_filter_tag (LrgTriggerComponent *self)
{
	g_return_val_if_fail (LRG_IS_TRIGGER_COMPONENT (self), NULL);

	return self->filter_tag;
}

void
lrg_trigger_component_set_filter_tag (LrgTriggerComponent *self,
                                      const gchar         *tag)
{
	g_return_if_fail (LRG_IS_TRIGGER_COMPONENT (self));

	set_string (self, &self->filter_tag, tag, PROP_FILTER_TAG);
}

const gchar *
lrg_trigger_component_get_filter_component (LrgTriggerComponent *self)
{
	g_return_val_if_fail (LRG_IS_TRIGGER_COMPONENT (self), NULL);

	return self->filter_component;
}

void
lrg_trigger_component_set_filter_component (LrgTriggerComponent *self,
                                            const gchar         *type_name)
{
	g_return_if_fail (LRG_IS_TRIGGER_COMPONENT (self));

	set_string (self, &self->filter_component, type_name, PROP_FILTER_COMPONENT);
}

gboolean
lrg_trigger_component_contains_point (LrgTriggerComponent *self,
                                      const gfloat        *world_point)
{
	LrgEntity3D *entity;

	g_return_val_if_fail (LRG_IS_TRIGGER_COMPONENT (self), FALSE);
	g_return_val_if_fail (world_point != NULL, FALSE);

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity == NULL)
		return FALSE;

	if (self->shape == LRG_TRIGGER_SHAPE_SPHERE)
	{
		/* Distance from the entity, radius scaled by the largest axis */
		gfloat c[3], s[3], d[3], r;

		lrg_entity3d_get_world_position (entity, c);
		lrg_entity3d_get_world_scale (entity, s);
		r = self->radius * MAX (fabsf (s[0]), MAX (fabsf (s[1]), fabsf (s[2])));
		d[0] = world_point[0] - c[0];
		d[1] = world_point[1] - c[1];
		d[2] = world_point[2] - c[2];
		return d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= r * r;
	}
	else
	{
		/* The box in the entity's local space: rotation and scale follow */
		gfloat local[3];

		lrg_entity3d_inverse_transform_point (entity, world_point, local);
		return fabsf (local[0]) <= self->size[0] * 0.5f &&
		       fabsf (local[1]) <= self->size[1] * 0.5f &&
		       fabsf (local[2]) <= self->size[2] * 0.5f;
	}
}

gboolean
lrg_trigger_component_is_inside (LrgTriggerComponent *self,
                                 LrgEntity3D         *entity)
{
	g_return_val_if_fail (LRG_IS_TRIGGER_COMPONENT (self), FALSE);

	return find_inside (self, entity) >= 0;
}

guint
lrg_trigger_component_get_n_inside (LrgTriggerComponent *self)
{
	g_return_val_if_fail (LRG_IS_TRIGGER_COMPONENT (self), 0);

	return self->inside->len;
}

void
lrg_trigger_component_evaluate (LrgTriggerComponent *self)
{
	LrgEntity3D         *own;
	LrgWorld3D          *world;
	g_autoptr(GPtrArray) candidates = NULL;
	g_autoptr(GPtrArray) now = NULL;
	g_autoptr(GPtrArray) exited = NULL;
	g_autoptr(GPtrArray) entered = NULL;
	guint                i;

	g_return_if_fail (LRG_IS_TRIGGER_COMPONENT (self));

	own = lrg_component_get_entity (LRG_COMPONENT (self));
	world = lrg_component_get_world3d (LRG_COMPONENT (self));
	if (own == NULL || world == NULL || !lrg_component_get_enabled (LRG_COMPONENT (self)) ||
	    (self->one_shot && self->fired))
		return;

	/* 1. Who is inside now (by world position) */
	candidates = gather_candidates (self, world, own);
	now = g_ptr_array_new ();
	for (i = 0; i < candidates->len; i++)
	{
		LrgEntity3D *e = (LrgEntity3D *) g_ptr_array_index (candidates, i);
		gfloat       p[3];

		lrg_entity3d_get_world_position (e, p);
		if (lrg_trigger_component_contains_point (self, p))
			g_ptr_array_add (now, e);
	}

	/* 2. Exits: in the old set, not in the new one */
	exited = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = self->inside->len; i > 0; i--)
	{
		LrgEntity3D *e = (LrgEntity3D *) g_ptr_array_index (self->inside, i - 1);
		guint        idx;

		if (!g_ptr_array_find (now, e, &idx))
			g_ptr_array_add (exited, g_ptr_array_steal_index (self->inside, i - 1));
	}

	/* 3. Entries: in the new set, not in the old one */
	entered = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < now->len; i++)
	{
		LrgEntity3D *e = (LrgEntity3D *) g_ptr_array_index (now, i);

		if (find_inside (self, e) < 0)
		{
			g_ptr_array_add (self->inside, g_object_ref (e));
			g_ptr_array_add (entered, g_object_ref (e));
		}
	}

	/* 4. Signals last, from stable copies (handlers may edit the world) */
	g_object_ref (self);
	for (i = exited->len; i > 0; i--)
		g_signal_emit (self, signals[SIGNAL_EXITED], 0, g_ptr_array_index (exited, i - 1));
	for (i = 0; i < entered->len; i++)
		g_signal_emit (self, signals[SIGNAL_ENTERED], 0, g_ptr_array_index (entered, i));
	if (entered->len > 0 && self->one_shot)
		self->fired = TRUE;
	g_object_unref (self);
}

void
lrg_trigger_component_reset (LrgTriggerComponent *self)
{
	g_return_if_fail (LRG_IS_TRIGGER_COMPONENT (self));

	g_ptr_array_set_size (self->inside, 0);
	self->fired = FALSE;
}
