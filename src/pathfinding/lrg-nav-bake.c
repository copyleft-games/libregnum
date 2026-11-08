/* lrg-nav-bake.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The `.rnav` bake settings and the bake from a world's static geometry.
 *
 * Loading keeps every key it does not know (top level, the four
 * sections, every area and link) in an "extra" mapping per place and
 * writes it back after the known keys, so a file from a newer build or
 * with tool annotations survives a load and save.  Numbers are written
 * with the shortest decimal text that reads back to the same float, so
 * save(load(text)) == text for any text this file wrote.
 *
 * Gathering geometry: mesh renderers and static colliders of active
 * entities pass the entity filters (static flag, render layer mask,
 * include / exclude tags, collider physics layers), their triangles are
 * moved to world space, clipped to the bounds (infinite planes to the
 * other geometry's extent) and staged on the LrgNavPolyMesh, which then
 * bakes through its cache.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <yaml-glib.h>
#include <graylib.h>
#include <raylib.h>

#include "lrg-nav-bake.h"
#include "../core/lrg-math3d.h"
#include "../ecs/lrg-component.h"
#include "../graphics/lrg-gltf-geometry.h"
#include "../graphics/lrg-uv-unwrap.h"
#include "../physics/lrg-physics-shape.h"
#include "../world3d/lrg-entity3d.h"
#include "../world3d/lrg-world3d.h"
#include "../world3d/components/lrg-mesh-renderer-component.h"
#include "../world3d/components/lrg-collider-component3d.h"
#include "../world3d/components/lrg-rigid-body-component.h"

/* an area type: tag, cost, boxes (6 floats each) */
typedef struct
{
	gchar       *name;
	guint32      tag;
	gfloat       cost;
	GArray      *boxes;
	YamlMapping *extra;
} Area;

/* an off-mesh link */
typedef struct
{
	gfloat       start[3];
	gfloat       end[3];
	gboolean     bidirectional;
	gfloat       cost;
	gchar       *area;
	YamlMapping *extra;
} Link;

struct _LrgNavBake
{
	GObject parent_instance;

	gchar   *name;

	/* agent */
	gfloat   agent_radius;
	gfloat   agent_height;
	gfloat   max_slope;
	gfloat   max_step;
	gfloat   max_drop;

	/* voxels and regions */
	gfloat   cell_size;
	gfloat   cell_height;
	gfloat   min_region_area;
	gfloat   corner_margin;

	/* sources */
	gboolean include_meshes;
	gboolean include_colliders;
	gboolean static_only;
	guint32  layer_mask;
	GStrv    include_tags;
	GStrv    exclude_tags;
	GStrv    physics_layers;
	gboolean has_bounds;
	gfloat   bounds_min[3];
	gfloat   bounds_max[3];

	GPtrArray *areas;   /* Area* */
	GPtrArray *links;   /* Link* */
	gchar     *output;

	/* unknown keys, per place (nullable) */
	YamlMapping *extra;
	YamlMapping *extra_agent;
	YamlMapping *extra_voxel;
	YamlMapping *extra_regions;
	YamlMapping *extra_sources;

	/* explicit triangles (not saved) */
	GArray   *triangles;  /* gfloat, 9 per triangle */
	GArray   *walkable;   /* guint8 per triangle */

	guint     last_triangles;
	gfloat    tile_extent;    /* 0 = one bake; otherwise X/Z tile size */
	guint     last_tiles;
};

G_DEFINE_FINAL_TYPE (LrgNavBake, lrg_nav_bake, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_AGENT_RADIUS,
	PROP_AGENT_HEIGHT,
	PROP_MAX_SLOPE,
	PROP_MAX_STEP,
	PROP_MAX_DROP,
	PROP_CELL_SIZE,
	PROP_CELL_HEIGHT,
	PROP_MIN_REGION_AREA,
	PROP_CORNER_MARGIN,
	PROP_INCLUDE_MESHES,
	PROP_INCLUDE_COLLIDERS,
	PROP_STATIC_ONLY,
	PROP_LAYER_MASK,
	PROP_OUTPUT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_QUARK (lrg-nav-bake-error-quark, lrg_nav_bake_error)

GType
lrg_nav_bake_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_NAV_BAKE_ERROR_PARSE, "LRG_NAV_BAKE_ERROR_PARSE", "parse" },
			{ LRG_NAV_BAKE_ERROR_IO, "LRG_NAV_BAKE_ERROR_IO", "io" },
			{ LRG_NAV_BAKE_ERROR_INVALID, "LRG_NAV_BAKE_ERROR_INVALID", "invalid" },
			{ LRG_NAV_BAKE_ERROR_NO_GEOMETRY, "LRG_NAV_BAKE_ERROR_NO_GEOMETRY", "no-geometry" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgNavBakeError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Areas and links (records)
 * ========================================================================== */

static void
area_free (gpointer data)
{
	Area *a = data;

	g_free (a->name);
	g_array_unref (a->boxes);
	if (a->extra != NULL)
		yaml_mapping_unref (a->extra);
	g_free (a);
}

static Area *
area_new (const gchar *name,
          guint32      tag,
          gfloat       cost)
{
	Area *a = g_new0 (Area, 1);

	a->name = g_strdup (name);
	a->tag = tag;
	a->cost = cost;
	a->boxes = g_array_new (FALSE, FALSE, sizeof (gfloat));
	return a;
}

static void
link_free (gpointer data)
{
	Link *l = data;

	g_free (l->area);
	if (l->extra != NULL)
		yaml_mapping_unref (l->extra);
	g_free (l);
}

static YamlMapping *
copy_mapping (YamlMapping *m)
{
	YamlMapping      *copy;
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	if (m == NULL)
		return NULL;
	copy = yaml_mapping_new ();
	keys = yaml_mapping_get_members (m);
	for (l = keys; l != NULL; l = l->next)
	{
		g_autoptr(YamlNode) node = yaml_node_copy (yaml_mapping_get_member (m, l->data));

		yaml_mapping_set_member (copy, l->data, node);
	}
	return copy;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_nav_bake_finalize (GObject *object)
{
	LrgNavBake *self = LRG_NAV_BAKE (object);

	g_free (self->name);
	g_free (self->output);
	g_strfreev (self->include_tags);
	g_strfreev (self->exclude_tags);
	g_strfreev (self->physics_layers);
	g_ptr_array_unref (self->areas);
	g_ptr_array_unref (self->links);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	g_clear_pointer (&self->extra_agent, yaml_mapping_unref);
	g_clear_pointer (&self->extra_voxel, yaml_mapping_unref);
	g_clear_pointer (&self->extra_regions, yaml_mapping_unref);
	g_clear_pointer (&self->extra_sources, yaml_mapping_unref);
	g_array_unref (self->triangles);
	g_array_unref (self->walkable);
	G_OBJECT_CLASS (lrg_nav_bake_parent_class)->finalize (object);
}

/* the float field behind a float property */
static gfloat *
float_field (LrgNavBake *self,
             guint       prop_id)
{
	switch (prop_id)
	{
	case PROP_AGENT_RADIUS:    return &self->agent_radius;
	case PROP_AGENT_HEIGHT:    return &self->agent_height;
	case PROP_MAX_SLOPE:       return &self->max_slope;
	case PROP_MAX_STEP:        return &self->max_step;
	case PROP_MAX_DROP:        return &self->max_drop;
	case PROP_CELL_SIZE:       return &self->cell_size;
	case PROP_CELL_HEIGHT:     return &self->cell_height;
	case PROP_MIN_REGION_AREA: return &self->min_region_area;
	case PROP_CORNER_MARGIN:   return &self->corner_margin;
	default:                   return NULL;
	}
}

static gboolean *
bool_field (LrgNavBake *self,
            guint       prop_id)
{
	switch (prop_id)
	{
	case PROP_INCLUDE_MESHES:    return &self->include_meshes;
	case PROP_INCLUDE_COLLIDERS: return &self->include_colliders;
	case PROP_STATIC_ONLY:       return &self->static_only;
	default:                     return NULL;
	}
}

static void
lrg_nav_bake_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgNavBake *self = LRG_NAV_BAKE (object);
	gfloat     *f = float_field (self, prop_id);
	gboolean   *b = bool_field (self, prop_id);

	if (f != NULL)
	{
		g_value_set_float (value, *f);
		return;
	}
	if (b != NULL)
	{
		g_value_set_boolean (value, *b);
		return;
	}
	switch (prop_id)
	{
	case PROP_NAME:       g_value_set_string (value, self->name); break;
	case PROP_LAYER_MASK: g_value_set_uint (value, self->layer_mask); break;
	case PROP_OUTPUT:     g_value_set_string (value, self->output); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_nav_bake_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgNavBake *self = LRG_NAV_BAKE (object);
	gfloat     *f = float_field (self, prop_id);
	gboolean   *b = bool_field (self, prop_id);

	if (f != NULL)
	{
		gfloat v = g_value_get_float (value);

		if (*f != v)
		{
			*f = v;
			g_object_notify_by_pspec (object, pspec);
		}
		return;
	}
	if (b != NULL)
	{
		gboolean v = g_value_get_boolean (value);

		if (*b != v)
		{
			*b = v;
			g_object_notify_by_pspec (object, pspec);
		}
		return;
	}
	switch (prop_id)
	{
	case PROP_NAME:
		lrg_nav_bake_set_name (self, g_value_get_string (value));
		break;
	case PROP_LAYER_MASK:
		if (self->layer_mask != g_value_get_uint (value))
		{
			self->layer_mask = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_OUTPUT:
		if (g_strcmp0 (self->output, g_value_get_string (value)) != 0)
		{
			g_free (self->output);
			self->output = g_strdup (g_value_get_string (value));
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static GParamSpec *
float_pspec (const gchar *name,
             const gchar *blurb,
             gfloat       min,
             gfloat       max,
             gfloat       def)
{
	return g_param_spec_float (name, NULL, blurb, min, max, def,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

static GParamSpec *
bool_pspec (const gchar *name,
            const gchar *blurb,
            gboolean     def)
{
	return g_param_spec_boolean (name, NULL, blurb, def,
	                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_nav_bake_class_init (LrgNavBakeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_nav_bake_finalize;
	object_class->get_property = lrg_nav_bake_get_property;
	object_class->set_property = lrg_nav_bake_set_property;

	/**
	 * LrgNavBake:name:
	 *
	 * A display name (`name:` in the file).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name", NULL, "Display name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNavBake:agent-radius:
	 *
	 * The agent's radius in metres: the walkable surface is eroded by it
	 * so an agent's centre on the mesh keeps it off walls (`agent.radius`).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_AGENT_RADIUS] = float_pspec ("agent-radius", "Agent radius", 0.0f, 100.0f, 0.3f);

	/**
	 * LrgNavBake:agent-height:
	 *
	 * The clearance an agent needs above the floor (`agent.height`).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_AGENT_HEIGHT] = float_pspec ("agent-height", "Agent height", 0.01f, 100.0f, 1.8f);

	/**
	 * LrgNavBake:max-slope:
	 *
	 * The steepest walkable slope in degrees (`agent.max_slope`).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MAX_SLOPE] = float_pspec ("max-slope", "Max slope", 0.0f, 89.0f, 50.0f);

	/**
	 * LrgNavBake:max-step:
	 *
	 * The highest step an agent climbs, in metres (`agent.max_step`):
	 * #LrgNavPolyMesh:max-climb.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MAX_STEP] = float_pspec ("max-step", "Max step", 0.0f, 100.0f, 0.45f);

	/**
	 * LrgNavBake:max-drop:
	 *
	 * The highest ledge an agent drops down, in metres; above
	 * #LrgNavBake:max-step the bake adds one-way drop links
	 * (`agent.max_drop`, 0 for none).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MAX_DROP] = float_pspec ("max-drop", "Max drop", 0.0f, 100.0f, 0.0f);

	/**
	 * LrgNavBake:cell-size:
	 *
	 * Horizontal voxel size in metres (`voxel.cell_size`); smaller is
	 * more precise and slower (memory grows with the square).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_CELL_SIZE] = float_pspec ("cell-size", "Cell size", 0.01f, 100.0f, 0.25f);

	/**
	 * LrgNavBake:cell-height:
	 *
	 * Vertical voxel size in metres (`voxel.cell_height`).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_CELL_HEIGHT] = float_pspec ("cell-height", "Cell height", 0.01f, 100.0f, 0.1f);

	/**
	 * LrgNavBake:min-region-area:
	 *
	 * Connected regions with less walkable area (square metres) are
	 * disabled after the bake: table tops and window sills an agent can
	 * never reach (`regions.min_area`, 0 keeps everything).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MIN_REGION_AREA] = float_pspec ("min-region-area", "Min region area", 0.0f, G_MAXFLOAT, 0.0f);

	/**
	 * LrgNavBake:corner-margin:
	 *
	 * #LrgNavPolyMesh:corner-margin: how far paths keep from the ends of
	 * the portals they pass (`regions.corner_margin`).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_CORNER_MARGIN] = float_pspec ("corner-margin", "Corner margin", 0.0f, 100.0f, 0.0f);

	/**
	 * LrgNavBake:include-meshes:
	 *
	 * Whether mesh renderers contribute geometry (`sources.meshes`).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_INCLUDE_MESHES] = bool_pspec ("include-meshes", "Include meshes", TRUE);

	/**
	 * LrgNavBake:include-colliders:
	 *
	 * Whether static physics colliders contribute geometry
	 * (`sources.colliders`).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_INCLUDE_COLLIDERS] = bool_pspec ("include-colliders", "Include colliders", TRUE);

	/**
	 * LrgNavBake:static-only:
	 *
	 * Only entities flagged static (lrg_entity3d_set_static())
	 * contribute (`sources.static_only`).  Dynamic and kinematic bodies
	 * never do.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_STATIC_ONLY] = bool_pspec ("static-only", "Static only", FALSE);

	/**
	 * LrgNavBake:layer-mask:
	 *
	 * Render layers (LRG_RENDER_LAYERS_BIT() of lrg_entity3d_get_layer())
	 * whose entities contribute (`sources.layer_mask`, all by default).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_LAYER_MASK] =
		g_param_spec_uint ("layer-mask", NULL, "Render layer mask", 0, G_MAXUINT32, G_MAXUINT32,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNavBake:output:
	 *
	 * Where a tool writes the baked mesh (lrg_nav_poly_mesh_save()), a
	 * path relative to the `.rnav` file (`output:`); the bake itself
	 * does not write it.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_OUTPUT] =
		g_param_spec_string ("output", NULL, "Baked mesh path", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_nav_bake_init (LrgNavBake *self)
{
	self->agent_radius = 0.3f;
	self->agent_height = 1.8f;
	self->max_slope = 50.0f;
	self->max_step = 0.45f;
	self->max_drop = 0.0f;
	self->cell_size = 0.25f;
	self->cell_height = 0.1f;
	self->tile_extent = 0.0f;
	self->include_meshes = TRUE;
	self->include_colliders = TRUE;
	self->layer_mask = G_MAXUINT32;
	self->include_tags = g_new0 (gchar *, 1);
	self->exclude_tags = g_new0 (gchar *, 1);
	self->physics_layers = g_new0 (gchar *, 1);
	self->areas = g_ptr_array_new_with_free_func (area_free);
	self->links = g_ptr_array_new_with_free_func (link_free);
	self->triangles = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->walkable = g_array_new (FALSE, FALSE, sizeof (guint8));
}

LrgNavBake *
lrg_nav_bake_new (void)
{
	return g_object_new (LRG_TYPE_NAV_BAKE, NULL);
}

/* ==========================================================================
 * Simple accessors
 * ========================================================================== */

const gchar *
lrg_nav_bake_get_name (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), NULL);
	return self->name;
}

void
lrg_nav_bake_set_name (LrgNavBake  *self,
                       const gchar *name)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));

	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

void
lrg_nav_bake_get_agent (LrgNavBake *self,
                        gfloat     *radius,
                        gfloat     *height,
                        gfloat     *max_slope,
                        gfloat     *max_step)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));

	if (radius != NULL)
		*radius = self->agent_radius;
	if (height != NULL)
		*height = self->agent_height;
	if (max_slope != NULL)
		*max_slope = self->max_slope;
	if (max_step != NULL)
		*max_step = self->max_step;
}

void
lrg_nav_bake_set_agent (LrgNavBake *self,
                        gfloat      radius,
                        gfloat      height,
                        gfloat      max_slope,
                        gfloat      max_step)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));

	g_object_freeze_notify (G_OBJECT (self));
	g_object_set (self,
	              "agent-radius", CLAMP (radius, 0.0f, 100.0f),
	              "agent-height", CLAMP (height, 0.01f, 100.0f),
	              "max-slope", CLAMP (max_slope, 0.0f, 89.0f),
	              "max-step", CLAMP (max_step, 0.0f, 100.0f),
	              NULL);
	g_object_thaw_notify (G_OBJECT (self));
}

gfloat
lrg_nav_bake_get_cell_size (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0.0f);
	return self->cell_size;
}

void
lrg_nav_bake_set_cell_size (LrgNavBake *self,
                            gfloat      cell_size)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	g_return_if_fail (cell_size > 0.0f);
	g_object_set (self, "cell-size", CLAMP (cell_size, 0.01f, 100.0f), NULL);
}

gfloat
lrg_nav_bake_get_cell_height (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0.0f);
	return self->cell_height;
}

void
lrg_nav_bake_set_cell_height (LrgNavBake *self,
                              gfloat      cell_height)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	g_return_if_fail (cell_height > 0.0f);
	g_object_set (self, "cell-height", CLAMP (cell_height, 0.01f, 100.0f), NULL);
}

void
lrg_nav_bake_set_bounds (LrgNavBake   *self,
                         const gfloat *min,
                         const gfloat *max)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));

	if (min == NULL || max == NULL)
	{
		self->has_bounds = FALSE;
		return;
	}
	self->has_bounds = TRUE;
	self->bounds_min[0] = MIN (min[0], max[0]);
	self->bounds_min[1] = MIN (min[1], max[1]);
	self->bounds_min[2] = MIN (min[2], max[2]);
	self->bounds_max[0] = MAX (min[0], max[0]);
	self->bounds_max[1] = MAX (min[1], max[1]);
	self->bounds_max[2] = MAX (min[2], max[2]);
}

gboolean
lrg_nav_bake_get_bounds (LrgNavBake *self,
                         gfloat     *min,
                         gfloat     *max)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (!self->has_bounds)
		return FALSE;
	if (min != NULL)
		memcpy (min, self->bounds_min, sizeof (self->bounds_min));
	if (max != NULL)
		memcpy (max, self->bounds_max, sizeof (self->bounds_max));
	return TRUE;
}

/* replaces a string vector with a copy of @tags without empty strings */
static void
set_strv (GStrv              *field,
          const gchar *const *tags)
{
	GPtrArray *a = g_ptr_array_new ();
	guint      i;

	for (i = 0; tags != NULL && tags[i] != NULL; i++)
		if (*tags[i] != '\0')
			g_ptr_array_add (a, g_strdup (tags[i]));
	g_ptr_array_add (a, NULL);
	g_strfreev (*field);
	*field = (GStrv) g_ptr_array_free (a, FALSE);
}

void
lrg_nav_bake_set_include_tags (LrgNavBake         *self,
                               const gchar *const *tags)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	set_strv (&self->include_tags, tags);
}

const gchar * const *
lrg_nav_bake_get_include_tags (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), NULL);
	return (const gchar * const *) self->include_tags;
}

void
lrg_nav_bake_set_exclude_tags (LrgNavBake         *self,
                               const gchar *const *tags)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	set_strv (&self->exclude_tags, tags);
}

const gchar * const *
lrg_nav_bake_get_exclude_tags (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), NULL);
	return (const gchar * const *) self->exclude_tags;
}

void
lrg_nav_bake_set_physics_layers (LrgNavBake         *self,
                                 const gchar *const *layers)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	set_strv (&self->physics_layers, layers);
}

const gchar * const *
lrg_nav_bake_get_physics_layers (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), NULL);
	return (const gchar * const *) self->physics_layers;
}

guint
lrg_nav_bake_add_triangles (LrgNavBake   *self,
                            const gfloat *vertices,
                            guint         n_floats,
                            gboolean      walkable)
{
	guint  n;
	guint  i;
	guint8 w = walkable ? 1 : 0;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0);
	g_return_val_if_fail (vertices != NULL || n_floats == 0, 0);

	n = n_floats / 9;
	g_array_append_vals (self->triangles, vertices, n * 9);
	for (i = 0; i < n; i++)
		g_array_append_val (self->walkable, w);
	return n;
}

void
lrg_nav_bake_clear_triangles (LrgNavBake *self)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	g_array_set_size (self->triangles, 0);
	g_array_set_size (self->walkable, 0);
}

guint
lrg_nav_bake_get_last_triangle_count (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0);
	return self->last_triangles;
}

void
lrg_nav_bake_set_tile_extent (LrgNavBake *self,
                              gfloat      extent)
{
	g_return_if_fail (LRG_IS_NAV_BAKE (self));

	/* 0 (and anything that is not a positive size) is one bake. */
	self->tile_extent = extent > 0.0f ? extent : 0.0f;
}

gfloat
lrg_nav_bake_get_tile_extent (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0.0f);
	return self->tile_extent;
}

guint
lrg_nav_bake_get_last_tile_count (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0);
	return self->last_tiles;
}

/* ==========================================================================
 * Areas
 * ========================================================================== */

static gint
find_area_by_tag (LrgNavBake *self,
                  guint32     tag)
{
	guint i;

	for (i = 0; i < self->areas->len; i++)
		if (((Area *) g_ptr_array_index (self->areas, i))->tag == tag)
			return (gint) i;
	return -1;
}

gint
lrg_nav_bake_add_area (LrgNavBake  *self,
                       const gchar *name,
                       guint32      tag,
                       gfloat       cost)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), -1);
	g_return_val_if_fail (name != NULL, -1);

	if (*name == '\0' || tag == 0 || !isfinite (cost) || cost <= 0.0f)
		return -1;
	if (lrg_nav_bake_find_area (self, name) >= 0 || find_area_by_tag (self, tag) >= 0)
		return -1;
	g_ptr_array_add (self->areas, area_new (name, tag, cost));
	return (gint) self->areas->len - 1;
}

guint
lrg_nav_bake_get_n_areas (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0);
	return self->areas->len;
}

gboolean
lrg_nav_bake_get_area (LrgNavBake   *self,
                       guint         index,
                       const gchar **name,
                       guint32      *tag,
                       gfloat       *cost)
{
	const Area *a;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (index >= self->areas->len)
		return FALSE;
	a = g_ptr_array_index (self->areas, index);
	if (name != NULL)
		*name = a->name;
	if (tag != NULL)
		*tag = a->tag;
	if (cost != NULL)
		*cost = a->cost;
	return TRUE;
}

gint
lrg_nav_bake_find_area (LrgNavBake  *self,
                        const gchar *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), -1);

	if (name == NULL)
		return -1;
	for (i = 0; i < self->areas->len; i++)
		if (g_strcmp0 (((Area *) g_ptr_array_index (self->areas, i))->name, name) == 0)
			return (gint) i;
	return -1;
}

gboolean
lrg_nav_bake_set_area_cost (LrgNavBake *self,
                            guint       index,
                            gfloat      cost)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (index >= self->areas->len || !isfinite (cost) || cost <= 0.0f)
		return FALSE;
	((Area *) g_ptr_array_index (self->areas, index))->cost = cost;
	return TRUE;
}

gboolean
lrg_nav_bake_remove_area (LrgNavBake *self,
                          guint       index)
{
	const gchar *name;
	guint        i;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (index >= self->areas->len)
		return FALSE;
	name = ((Area *) g_ptr_array_index (self->areas, index))->name;
	for (i = 0; i < self->links->len; i++)
	{
		Link *l = g_ptr_array_index (self->links, i);

		if (g_strcmp0 (l->area, name) == 0)
			g_clear_pointer (&l->area, g_free);
	}
	g_ptr_array_remove_index (self->areas, index);
	return TRUE;
}

static gboolean
box_valid (const gfloat *min,
           const gfloat *max)
{
	guint k;

	for (k = 0; k < 3; k++)
		if (!isfinite (min[k]) || !isfinite (max[k]) || max[k] < min[k])
			return FALSE;
	return max[0] > min[0] && max[2] > min[2];
}

gint
lrg_nav_bake_add_area_box (LrgNavBake   *self,
                           guint         area,
                           const gfloat *min,
                           const gfloat *max)
{
	Area *a;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), -1);
	g_return_val_if_fail (min != NULL && max != NULL, -1);

	if (area >= self->areas->len || !box_valid (min, max))
		return -1;
	a = g_ptr_array_index (self->areas, area);
	g_array_append_vals (a->boxes, min, 3);
	g_array_append_vals (a->boxes, max, 3);
	return (gint) (a->boxes->len / 6) - 1;
}

guint
lrg_nav_bake_get_n_area_boxes (LrgNavBake *self,
                               guint       area)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0);

	if (area >= self->areas->len)
		return 0;
	return ((Area *) g_ptr_array_index (self->areas, area))->boxes->len / 6;
}

gboolean
lrg_nav_bake_get_area_box (LrgNavBake *self,
                           guint       area,
                           guint       box,
                           gfloat     *min,
                           gfloat     *max)
{
	Area *a;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (area >= self->areas->len)
		return FALSE;
	a = g_ptr_array_index (self->areas, area);
	if (box >= a->boxes->len / 6)
		return FALSE;
	if (min != NULL)
		memcpy (min, &g_array_index (a->boxes, gfloat, box * 6), sizeof (gfloat) * 3);
	if (max != NULL)
		memcpy (max, &g_array_index (a->boxes, gfloat, box * 6 + 3), sizeof (gfloat) * 3);
	return TRUE;
}

gboolean
lrg_nav_bake_remove_area_box (LrgNavBake *self,
                              guint       area,
                              guint       box)
{
	Area *a;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (area >= self->areas->len)
		return FALSE;
	a = g_ptr_array_index (self->areas, area);
	if (box >= a->boxes->len / 6)
		return FALSE;
	g_array_remove_range (a->boxes, box * 6, 6);
	return TRUE;
}

/* ==========================================================================
 * Links
 * ========================================================================== */

static gboolean
vec_finite (const gfloat *v)
{
	return isfinite (v[0]) && isfinite (v[1]) && isfinite (v[2]);
}

gint
lrg_nav_bake_add_link (LrgNavBake   *self,
                       const gfloat *start,
                       const gfloat *end,
                       gboolean      bidirectional,
                       gfloat        cost,
                       const gchar  *area)
{
	Link *l;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), -1);
	g_return_val_if_fail (start != NULL && end != NULL, -1);

	if (!vec_finite (start) || !vec_finite (end) || !isfinite (cost) || cost < 0.0f)
		return -1;
	if (area != NULL && *area != '\0' && lrg_nav_bake_find_area (self, area) < 0)
		return -1;
	l = g_new0 (Link, 1);
	memcpy (l->start, start, sizeof (l->start));
	memcpy (l->end, end, sizeof (l->end));
	l->bidirectional = bidirectional ? TRUE : FALSE;
	l->cost = cost;
	l->area = (area != NULL && *area != '\0') ? g_strdup (area) : NULL;
	g_ptr_array_add (self->links, l);
	return (gint) self->links->len - 1;
}

guint
lrg_nav_bake_get_n_links (LrgNavBake *self)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0);
	return self->links->len;
}

gboolean
lrg_nav_bake_get_link (LrgNavBake   *self,
                       guint         index,
                       gfloat       *start,
                       gfloat       *end,
                       gboolean     *bidirectional,
                       gfloat       *cost,
                       const gchar **area)
{
	const Link *l;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (index >= self->links->len)
		return FALSE;
	l = g_ptr_array_index (self->links, index);
	if (start != NULL)
		memcpy (start, l->start, sizeof (l->start));
	if (end != NULL)
		memcpy (end, l->end, sizeof (l->end));
	if (bidirectional != NULL)
		*bidirectional = l->bidirectional;
	if (cost != NULL)
		*cost = l->cost;
	if (area != NULL)
		*area = l->area;
	return TRUE;
}

gboolean
lrg_nav_bake_remove_link (LrgNavBake *self,
                          guint       index)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);

	if (index >= self->links->len)
		return FALSE;
	g_ptr_array_remove_index (self->links, index);
	return TRUE;
}

/* ==========================================================================
 * YAML in
 * ========================================================================== */

static gboolean
fail (GError     **error,
      const gchar *path,
      const gchar *what)
{
	g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_PARSE, "%s: %s", path, what);
	return FALSE;
}

static const gchar *
scalar_of (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
read_float (YamlNode     *node,
            const gchar  *path,
            gfloat       *out,
            GError      **error)
{
	const gchar *s = scalar_of (node);
	gchar       *end = NULL;
	gdouble      v;

	if (s == NULL)
		return fail (error, path, "expected a number");
	v = g_ascii_strtod (s, &end);
	if (end == s || *end != '\0' || !isfinite (v))
		return fail (error, path, "expected a number");
	*out = (gfloat) v;
	return TRUE;
}

static gboolean
read_bool (YamlNode     *node,
           const gchar  *path,
           gboolean     *out,
           GError      **error)
{
	const gchar *s = scalar_of (node);

	if (s != NULL && (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0))
		*out = TRUE;
	else if (s != NULL && (g_ascii_strcasecmp (s, "false") == 0 || g_ascii_strcasecmp (s, "no") == 0))
		*out = FALSE;
	else
		return fail (error, path, "expected true or false");
	return TRUE;
}

static gboolean
read_uint (YamlNode     *node,
           const gchar  *path,
           guint64       max,
           guint64      *out,
           GError      **error)
{
	const gchar *s = scalar_of (node);
	gchar       *end = NULL;
	guint64      v;

	if (s == NULL || *s == '\0' || *s == '-')
		return fail (error, path, "expected a non-negative integer");
	v = g_ascii_strtoull (s, &end, 0);
	if (end == s || *end != '\0' || v > max)
		return fail (error, path, "expected a non-negative integer in range");
	*out = v;
	return TRUE;
}

static gboolean
read_string (YamlNode     *node,
             const gchar  *path,
             const gchar **out,
             GError      **error)
{
	const gchar *s = scalar_of (node);

	if (s == NULL)
		return fail (error, path, "expected a string");
	*out = s;
	return TRUE;
}

static gboolean
read_vec3 (YamlNode     *node,
           const gchar  *path,
           gfloat       *out,
           GError      **error)
{
	YamlSequence *seq;
	guint         k;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return fail (error, path, "expected a list of 3 numbers");
	seq = yaml_node_get_sequence (node);
	if (yaml_sequence_get_length (seq) != 3)
		return fail (error, path, "expected a list of 3 numbers");
	for (k = 0; k < 3; k++)
	{
		g_autofree gchar *sub = g_strdup_printf ("%s[%u]", path, k);

		if (!read_float (yaml_sequence_get_element (seq, k), sub, &out[k], error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
read_strv (YamlNode     *node,
           const gchar  *path,
           GStrv        *out,
           GError      **error)
{
	YamlSequence *seq;
	GPtrArray    *a;
	guint         i;
	guint         n;

	if (node == NULL || yaml_node_is_null (node))
	{
		set_strv (out, NULL);
		return TRUE;
	}
	if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return fail (error, path, "expected a list of strings");
	seq = yaml_node_get_sequence (node);
	n = yaml_sequence_get_length (seq);
	a = g_ptr_array_new ();
	for (i = 0; i < n; i++)
	{
		const gchar *s = scalar_of (yaml_sequence_get_element (seq, i));

		if (s == NULL || *s == '\0')
		{
			g_autofree gchar *sub = g_strdup_printf ("%s[%u]", path, i);

			g_ptr_array_free (a, TRUE);
			return fail (error, sub, "expected a non-empty string");
		}
		g_ptr_array_add (a, (gpointer) s);
	}
	g_ptr_array_add (a, NULL);
	set_strv (out, (const gchar *const *) a->pdata);
	g_ptr_array_free (a, TRUE);
	return TRUE;
}

/* the mapping of @node, or an error naming @path */
static YamlMapping *
mapping_of (YamlNode     *node,
            const gchar  *path,
            GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		fail (error, path, "expected a mapping");
		return NULL;
	}
	return yaml_node_get_mapping (node);
}

static void
keep_extra (YamlMapping **extra,
            const gchar  *key,
            YamlNode     *node)
{
	if (*extra == NULL)
		*extra = yaml_mapping_new ();
	yaml_mapping_set_member (*extra, key, node);
}

/* one float property from @map[@key] into @field (absent keys skipped) */
static gboolean
read_float_member (YamlNode     *node,
                   const gchar  *path,
                   gfloat        min,
                   gfloat        max,
                   gfloat       *field,
                   GError      **error)
{
	gfloat v;

	if (!read_float (node, path, &v, error))
		return FALSE;
	if (v < min || v > max)
	{
		g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_INVALID,
		             "%s: %g is out of range (%g .. %g)", path, (gdouble) v, (gdouble) min, (gdouble) max);
		return FALSE;
	}
	*field = v;
	return TRUE;
}

static gboolean
read_agent (LrgNavBake  *self,
            YamlNode    *node,
            GError     **error)
{
	YamlMapping      *map = mapping_of (node, "agent", error);
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *m = yaml_mapping_get_member (map, key);
		g_autofree gchar *path = g_strdup_printf ("agent.%s", key);
		gboolean          ok;

		if (g_strcmp0 (key, "radius") == 0)
			ok = read_float_member (m, path, 0.0f, 100.0f, &self->agent_radius, error);
		else if (g_strcmp0 (key, "height") == 0)
			ok = read_float_member (m, path, 0.01f, 100.0f, &self->agent_height, error);
		else if (g_strcmp0 (key, "max_slope") == 0)
			ok = read_float_member (m, path, 0.0f, 89.0f, &self->max_slope, error);
		else if (g_strcmp0 (key, "max_step") == 0)
			ok = read_float_member (m, path, 0.0f, 100.0f, &self->max_step, error);
		else if (g_strcmp0 (key, "max_drop") == 0)
			ok = read_float_member (m, path, 0.0f, 100.0f, &self->max_drop, error);
		else
		{
			keep_extra (&self->extra_agent, key, m);
			ok = TRUE;
		}
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

static gboolean
read_voxel (LrgNavBake  *self,
            YamlNode    *node,
            GError     **error)
{
	YamlMapping      *map = mapping_of (node, "voxel", error);
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *m = yaml_mapping_get_member (map, key);
		g_autofree gchar *path = g_strdup_printf ("voxel.%s", key);
		gboolean          ok;

		if (g_strcmp0 (key, "cell_size") == 0)
			ok = read_float_member (m, path, 0.01f, 100.0f, &self->cell_size, error);
		else if (g_strcmp0 (key, "cell_height") == 0)
			ok = read_float_member (m, path, 0.01f, 100.0f, &self->cell_height, error);
		else
		{
			keep_extra (&self->extra_voxel, key, m);
			ok = TRUE;
		}
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

static gboolean
read_regions (LrgNavBake  *self,
              YamlNode    *node,
              GError     **error)
{
	YamlMapping      *map = mapping_of (node, "regions", error);
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *m = yaml_mapping_get_member (map, key);
		g_autofree gchar *path = g_strdup_printf ("regions.%s", key);
		gboolean          ok;

		if (g_strcmp0 (key, "min_area") == 0)
			ok = read_float_member (m, path, 0.0f, G_MAXFLOAT, &self->min_region_area, error);
		else if (g_strcmp0 (key, "corner_margin") == 0)
			ok = read_float_member (m, path, 0.0f, 100.0f, &self->corner_margin, error);
		else
		{
			keep_extra (&self->extra_regions, key, m);
			ok = TRUE;
		}
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

static gboolean
read_sources (LrgNavBake  *self,
              YamlNode    *node,
              GError     **error)
{
	YamlMapping      *map = mapping_of (node, "sources", error);
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *m = yaml_mapping_get_member (map, key);
		g_autofree gchar *path = g_strdup_printf ("sources.%s", key);
		gboolean          ok = TRUE;

		if (g_strcmp0 (key, "meshes") == 0)
			ok = read_bool (m, path, &self->include_meshes, error);
		else if (g_strcmp0 (key, "colliders") == 0)
			ok = read_bool (m, path, &self->include_colliders, error);
		else if (g_strcmp0 (key, "static_only") == 0)
			ok = read_bool (m, path, &self->static_only, error);
		else if (g_strcmp0 (key, "layer_mask") == 0)
		{
			guint64 v = 0;

			ok = read_uint (m, path, G_MAXUINT32, &v, error);
			if (ok)
				self->layer_mask = (guint32) v;
		}
		else if (g_strcmp0 (key, "include_tags") == 0)
			ok = read_strv (m, path, &self->include_tags, error);
		else if (g_strcmp0 (key, "exclude_tags") == 0)
			ok = read_strv (m, path, &self->exclude_tags, error);
		else if (g_strcmp0 (key, "physics_layers") == 0)
			ok = read_strv (m, path, &self->physics_layers, error);
		else if (g_strcmp0 (key, "bounds") == 0)
		{
			YamlMapping *b = mapping_of (m, path, error);
			gfloat       mn[3];
			gfloat       mx[3];
			g_autofree gchar *pmin = g_strdup_printf ("%s.min", path);
			g_autofree gchar *pmax = g_strdup_printf ("%s.max", path);

			ok = b != NULL && read_vec3 (yaml_mapping_get_member (b, "min"), pmin, mn, error) &&
			     read_vec3 (yaml_mapping_get_member (b, "max"), pmax, mx, error);
			if (ok)
				lrg_nav_bake_set_bounds (self, mn, mx);
		}
		else
			keep_extra (&self->extra_sources, key, m);
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

static gboolean
read_area (LrgNavBake  *self,
           YamlNode    *node,
           guint        index,
           GError     **error)
{
	g_autofree gchar *path = g_strdup_printf ("areas[%u]", index);
	YamlMapping      *map = mapping_of (node, path, error);
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	const gchar      *name = NULL;
	guint64           tag = 0;
	gfloat            cost = 1.0f;
	YamlNode         *boxes = NULL;
	YamlMapping      *extra = NULL;
	Area             *area;

	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *m = yaml_mapping_get_member (map, key);
		g_autofree gchar *sub = g_strdup_printf ("%s.%s", path, key);
		gboolean          ok = TRUE;

		if (g_strcmp0 (key, "name") == 0)
			ok = read_string (m, sub, &name, error);
		else if (g_strcmp0 (key, "tag") == 0)
			ok = read_uint (m, sub, G_MAXUINT32, &tag, error);
		else if (g_strcmp0 (key, "cost") == 0)
			ok = read_float (m, sub, &cost, error);
		else if (g_strcmp0 (key, "boxes") == 0)
			boxes = m;
		else
			keep_extra (&extra, key, m);
		if (!ok)
		{
			g_clear_pointer (&extra, yaml_mapping_unref);
			return FALSE;
		}
	}
	if (name == NULL || *name == '\0' || tag == 0 || cost <= 0.0f)
	{
		g_clear_pointer (&extra, yaml_mapping_unref);
		g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_INVALID,
		             "%s: an area needs a name, a nonzero tag and a positive cost", path);
		return FALSE;
	}
	if (lrg_nav_bake_add_area (self, name, (guint32) tag, cost) < 0)
	{
		g_clear_pointer (&extra, yaml_mapping_unref);
		g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_INVALID,
		             "%s: area name '%s' or tag %u is used twice", path, name, (guint) tag);
		return FALSE;
	}
	area = g_ptr_array_index (self->areas, self->areas->len - 1);
	area->extra = extra;
	if (boxes != NULL && !yaml_node_is_null (boxes))
	{
		YamlSequence *seq;
		guint         i;
		guint         n;

		if (yaml_node_get_node_type (boxes) != YAML_NODE_SEQUENCE)
		{
			g_autofree gchar *sub = g_strdup_printf ("%s.boxes", path);

			return fail (error, sub, "expected a list of {min, max} boxes");
		}
		seq = yaml_node_get_sequence (boxes);
		n = yaml_sequence_get_length (seq);
		for (i = 0; i < n; i++)
		{
			g_autofree gchar *sub = g_strdup_printf ("%s.boxes[%u]", path, i);
			g_autofree gchar *pmin = g_strdup_printf ("%s.min", sub);
			g_autofree gchar *pmax = g_strdup_printf ("%s.max", sub);
			YamlMapping      *b = mapping_of (yaml_sequence_get_element (seq, i), sub, error);
			gfloat            mn[3];
			gfloat            mx[3];

			if (b == NULL || !read_vec3 (yaml_mapping_get_member (b, "min"), pmin, mn, error) ||
			    !read_vec3 (yaml_mapping_get_member (b, "max"), pmax, mx, error))
				return FALSE;
			if (lrg_nav_bake_add_area_box (self, self->areas->len - 1, mn, mx) < 0)
			{
				g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_INVALID,
				             "%s: the box is empty (max must exceed min in x and z)", sub);
				return FALSE;
			}
		}
	}
	return TRUE;
}

static gboolean
read_link (LrgNavBake  *self,
           YamlNode    *node,
           guint        index,
           GError     **error)
{
	g_autofree gchar *path = g_strdup_printf ("links[%u]", index);
	YamlMapping      *map = mapping_of (node, path, error);
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	gfloat            start[3];
	gfloat            end[3];
	gboolean          has_start = FALSE;
	gboolean          has_end = FALSE;
	gboolean          bidi = TRUE;
	gfloat            cost = 0.0f;
	const gchar      *area = NULL;
	YamlMapping      *extra = NULL;
	gint              at;

	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *m = yaml_mapping_get_member (map, key);
		g_autofree gchar *sub = g_strdup_printf ("%s.%s", path, key);
		gboolean          ok = TRUE;

		if (g_strcmp0 (key, "start") == 0)
			ok = has_start = read_vec3 (m, sub, start, error);
		else if (g_strcmp0 (key, "end") == 0)
			ok = has_end = read_vec3 (m, sub, end, error);
		else if (g_strcmp0 (key, "bidirectional") == 0)
			ok = read_bool (m, sub, &bidi, error);
		else if (g_strcmp0 (key, "cost") == 0)
			ok = read_float (m, sub, &cost, error);
		else if (g_strcmp0 (key, "area") == 0)
			ok = read_string (m, sub, &area, error);
		else
			keep_extra (&extra, key, m);
		if (!ok)
		{
			g_clear_pointer (&extra, yaml_mapping_unref);
			return FALSE;
		}
	}
	if (!has_start || !has_end)
	{
		g_clear_pointer (&extra, yaml_mapping_unref);
		return fail (error, path, "a link needs start and end");
	}
	at = lrg_nav_bake_add_link (self, start, end, bidi, cost, area);
	if (at < 0)
	{
		g_clear_pointer (&extra, yaml_mapping_unref);
		g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_INVALID,
		             "%s: negative cost or unknown area '%s' (areas come before links)", path,
		             area != NULL ? area : "");
		return FALSE;
	}
	((Link *) g_ptr_array_index (self->links, (guint) at))->extra = extra;
	return TRUE;
}

/* reads a list member with @reader per element */
static gboolean
read_list (LrgNavBake   *self,
           YamlNode     *node,
           const gchar  *path,
           gboolean    (*reader) (LrgNavBake *, YamlNode *, guint, GError **),
           GError      **error)
{
	YamlSequence *seq;
	guint         i;
	guint         n;

	if (node == NULL || yaml_node_is_null (node))
		return TRUE;
	if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return fail (error, path, "expected a list");
	seq = yaml_node_get_sequence (node);
	n = yaml_sequence_get_length (seq);
	for (i = 0; i < n; i++)
		if (!reader (self, yaml_sequence_get_element (seq, i), i, error))
			return FALSE;
	return TRUE;
}

static gboolean
read_root (LrgNavBake  *self,
           YamlNode    *root,
           GError     **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	YamlNode         *areas = NULL;
	YamlNode         *links = NULL;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_PARSE,
		                     "a .rnav file must be a YAML mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (root);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode    *m = yaml_mapping_get_member (map, key);
		gboolean     ok = TRUE;

		if (g_strcmp0 (key, "version") == 0)
		{
			guint64 v = 0;

			ok = read_uint (m, "version", G_MAXINT32, &v, error);
			if (ok && v > LRG_NAV_BAKE_FORMAT_VERSION)
			{
				g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_PARSE,
				             "version: %u is newer than this build reads (%d)", (guint) v,
				             LRG_NAV_BAKE_FORMAT_VERSION);
				return FALSE;
			}
		}
		else if (g_strcmp0 (key, "name") == 0)
		{
			const gchar *s = NULL;

			ok = read_string (m, "name", &s, error);
			if (ok)
				lrg_nav_bake_set_name (self, s);
		}
		else if (g_strcmp0 (key, "output") == 0)
		{
			const gchar *s = NULL;

			ok = read_string (m, "output", &s, error);
			if (ok)
			{
				g_free (self->output);
				self->output = g_strdup (s);
			}
		}
		else if (g_strcmp0 (key, "agent") == 0)
			ok = read_agent (self, m, error);
		else if (g_strcmp0 (key, "voxel") == 0)
			ok = read_voxel (self, m, error);
		else if (g_strcmp0 (key, "regions") == 0)
			ok = read_regions (self, m, error);
		else if (g_strcmp0 (key, "sources") == 0)
			ok = read_sources (self, m, error);
		else if (g_strcmp0 (key, "areas") == 0)
			areas = m;
		else if (g_strcmp0 (key, "links") == 0)
			links = m;
		else
			keep_extra (&self->extra, key, m);
		if (!ok)
			return FALSE;
	}
	/* areas before links, whatever the key order: links name areas */
	return read_list (self, areas, "areas", read_area, error) &&
	       read_list (self, links, "links", read_link, error);
}

LrgNavBake *
lrg_nav_bake_new_from_data (const gchar  *data,
                            gssize        length,
                            GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(LrgNavBake) self = NULL;
	g_autoptr(GError)     local = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_PARSE, "%s", local->message);
		return NULL;
	}
	self = lrg_nav_bake_new ();
	if (!read_root (self, yaml_parser_get_root (parser), error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgNavBake *
lrg_nav_bake_new_from_file (const gchar  *path,
                            GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	LrgNavBake       *self;
	gsize             len = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_IO, "%s", local->message);
		return NULL;
	}
	self = lrg_nav_bake_new_from_data (text, (gssize) len, &local);
	if (self == NULL)
	{
		g_set_error (error, LRG_NAV_BAKE_ERROR, local->code, "%s: %s", path, local->message);
		return NULL;
	}
	return self;
}

/* ==========================================================================
 * YAML out
 * ========================================================================== */

/* the shortest decimal text that reads back to exactly @v */
static const gchar *
format_float (gchar  *buf,
              gsize   size,
              gfloat  v)
{
	static const gchar *formats[] = { "%.6g", "%.7g", "%.8g", "%.9g" };
	guint i;

	for (i = 0; i < G_N_ELEMENTS (formats); i++)
	{
		g_ascii_formatd (buf, (gint) size, formats[i], (gdouble) v);
		if ((gfloat) g_ascii_strtod (buf, NULL) == v)
			break;
	}
	return buf;
}

static void
set_float (YamlMapping *map,
           const gchar *key,
           gfloat       v)
{
	gchar               buf[G_ASCII_DTOSTR_BUF_SIZE];
	g_autoptr(YamlNode) n = yaml_node_new_scalar (format_float (buf, sizeof buf, v), YAML_SCALAR_STYLE_PLAIN);

	yaml_mapping_set_member (map, key, n);
}

static void
set_vec3 (YamlMapping  *map,
          const gchar  *key,
          const gfloat *v)
{
	g_autoptr(YamlSequence) seq = yaml_sequence_new ();
	guint                   k;

	for (k = 0; k < 3; k++)
	{
		gchar               buf[G_ASCII_DTOSTR_BUF_SIZE];
		g_autoptr(YamlNode) n = yaml_node_new_scalar (format_float (buf, sizeof buf, v[k]),
		                                              YAML_SCALAR_STYLE_PLAIN);

		yaml_sequence_add_element (seq, n);
	}
	yaml_mapping_set_sequence_member (map, key, seq);
}

static void
set_strv_member (YamlMapping *map,
                 const gchar *key,
                 GStrv        v)
{
	g_autoptr(YamlSequence) seq = NULL;
	guint                   i;

	if (v == NULL || v[0] == NULL)
		return;
	seq = yaml_sequence_new ();
	for (i = 0; v[i] != NULL; i++)
		yaml_sequence_add_string_element (seq, v[i]);
	yaml_mapping_set_sequence_member (map, key, seq);
}

static void
copy_extra (YamlMapping *map,
            YamlMapping *extra)
{
	g_autoptr(GList) keys = NULL;
	GList           *l;

	if (extra == NULL)
		return;
	keys = yaml_mapping_get_members (extra);
	for (l = keys; l != NULL; l = l->next)
		yaml_mapping_set_member (map, l->data, yaml_mapping_get_member (extra, l->data));
}

gchar *
lrg_nav_bake_to_data (LrgNavBake *self)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlMapping)   agent = NULL;
	g_autoptr(YamlMapping)   voxel = NULL;
	g_autoptr(YamlMapping)   regions = NULL;
	g_autoptr(YamlMapping)   sources = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	YamlMapping             *map;
	guint                    i;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_NAV_BAKE_FORMAT_VERSION);
	if (self->name != NULL)
		yaml_mapping_set_string_member (map, "name", self->name);

	/* agent */
	agent = yaml_mapping_new ();
	set_float (agent, "radius", self->agent_radius);
	set_float (agent, "height", self->agent_height);
	set_float (agent, "max_slope", self->max_slope);
	set_float (agent, "max_step", self->max_step);
	set_float (agent, "max_drop", self->max_drop);
	copy_extra (agent, self->extra_agent);
	yaml_mapping_set_mapping_member (map, "agent", agent);

	/* voxel */
	voxel = yaml_mapping_new ();
	set_float (voxel, "cell_size", self->cell_size);
	set_float (voxel, "cell_height", self->cell_height);
	copy_extra (voxel, self->extra_voxel);
	yaml_mapping_set_mapping_member (map, "voxel", voxel);

	/* regions */
	regions = yaml_mapping_new ();
	set_float (regions, "min_area", self->min_region_area);
	set_float (regions, "corner_margin", self->corner_margin);
	copy_extra (regions, self->extra_regions);
	yaml_mapping_set_mapping_member (map, "regions", regions);

	/* sources */
	sources = yaml_mapping_new ();
	yaml_mapping_set_boolean_member (sources, "meshes", self->include_meshes);
	yaml_mapping_set_boolean_member (sources, "colliders", self->include_colliders);
	yaml_mapping_set_boolean_member (sources, "static_only", self->static_only);
	yaml_mapping_set_int_member (sources, "layer_mask", (gint64) self->layer_mask);
	set_strv_member (sources, "include_tags", self->include_tags);
	set_strv_member (sources, "exclude_tags", self->exclude_tags);
	set_strv_member (sources, "physics_layers", self->physics_layers);
	if (self->has_bounds)
	{
		g_autoptr(YamlMapping) b = yaml_mapping_new ();

		set_vec3 (b, "min", self->bounds_min);
		set_vec3 (b, "max", self->bounds_max);
		yaml_mapping_set_mapping_member (sources, "bounds", b);
	}
	copy_extra (sources, self->extra_sources);
	yaml_mapping_set_mapping_member (map, "sources", sources);

	/* areas */
	if (self->areas->len > 0)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		for (i = 0; i < self->areas->len; i++)
		{
			const Area             *a = g_ptr_array_index (self->areas, i);
			g_autoptr(YamlMapping)  am = yaml_mapping_new ();
			guint                   b;

			yaml_mapping_set_string_member (am, "name", a->name);
			yaml_mapping_set_int_member (am, "tag", (gint64) a->tag);
			set_float (am, "cost", a->cost);
			if (a->boxes->len > 0)
			{
				g_autoptr(YamlSequence) boxes = yaml_sequence_new ();

				for (b = 0; b < a->boxes->len / 6; b++)
				{
					g_autoptr(YamlMapping) bm = yaml_mapping_new ();

					set_vec3 (bm, "min", &g_array_index (a->boxes, gfloat, b * 6));
					set_vec3 (bm, "max", &g_array_index (a->boxes, gfloat, b * 6 + 3));
					yaml_sequence_add_mapping_element (boxes, bm);
				}
				yaml_mapping_set_sequence_member (am, "boxes", boxes);
			}
			copy_extra (am, a->extra);
			yaml_sequence_add_mapping_element (seq, am);
		}
		yaml_mapping_set_sequence_member (map, "areas", seq);
	}

	/* links */
	if (self->links->len > 0)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		for (i = 0; i < self->links->len; i++)
		{
			const Link             *l = g_ptr_array_index (self->links, i);
			g_autoptr(YamlMapping)  lm = yaml_mapping_new ();

			set_vec3 (lm, "start", l->start);
			set_vec3 (lm, "end", l->end);
			yaml_mapping_set_boolean_member (lm, "bidirectional", l->bidirectional);
			set_float (lm, "cost", l->cost);
			if (l->area != NULL)
				yaml_mapping_set_string_member (lm, "area", l->area);
			copy_extra (lm, l->extra);
			yaml_sequence_add_mapping_element (seq, lm);
		}
		yaml_mapping_set_sequence_member (map, "links", seq);
	}
	if (self->output != NULL)
		yaml_mapping_set_string_member (map, "output", self->output);
	copy_extra (map, self->extra);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, NULL);
}

gboolean
lrg_nav_bake_save_to_file (LrgNavBake   *self,
                           const gchar  *path,
                           GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_nav_bake_to_data (self);
	if (text == NULL || !g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_IO, "%s: %s", path,
		             local != NULL ? local->message : "could not write the YAML");
		return FALSE;
	}
	return TRUE;
}

LrgNavBake *
lrg_nav_bake_copy (LrgNavBake *self)
{
	LrgNavBake *copy;
	guint       i;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), NULL);

	copy = lrg_nav_bake_new ();
	copy->name = g_strdup (self->name);
	copy->output = g_strdup (self->output);
	copy->agent_radius = self->agent_radius;
	copy->agent_height = self->agent_height;
	copy->max_slope = self->max_slope;
	copy->max_step = self->max_step;
	copy->max_drop = self->max_drop;
	copy->cell_size = self->cell_size;
	copy->cell_height = self->cell_height;
	copy->min_region_area = self->min_region_area;
	copy->corner_margin = self->corner_margin;
	copy->include_meshes = self->include_meshes;
	copy->include_colliders = self->include_colliders;
	copy->static_only = self->static_only;
	copy->layer_mask = self->layer_mask;
	set_strv (&copy->include_tags, (const gchar *const *) self->include_tags);
	set_strv (&copy->exclude_tags, (const gchar *const *) self->exclude_tags);
	set_strv (&copy->physics_layers, (const gchar *const *) self->physics_layers);
	copy->has_bounds = self->has_bounds;
	copy->tile_extent = self->tile_extent;
	memcpy (copy->bounds_min, self->bounds_min, sizeof (self->bounds_min));
	memcpy (copy->bounds_max, self->bounds_max, sizeof (self->bounds_max));
	for (i = 0; i < self->areas->len; i++)
	{
		const Area *a = g_ptr_array_index (self->areas, i);
		Area       *b = area_new (a->name, a->tag, a->cost);

		g_array_append_vals (b->boxes, a->boxes->data, a->boxes->len);
		b->extra = copy_mapping (a->extra);
		g_ptr_array_add (copy->areas, b);
	}
	for (i = 0; i < self->links->len; i++)
	{
		const Link *l = g_ptr_array_index (self->links, i);
		Link       *m = g_new0 (Link, 1);

		*m = *l;
		m->area = g_strdup (l->area);
		m->extra = copy_mapping (l->extra);
		g_ptr_array_add (copy->links, m);
	}
	copy->extra = copy_mapping (self->extra);
	copy->extra_agent = copy_mapping (self->extra_agent);
	copy->extra_voxel = copy_mapping (self->extra_voxel);
	copy->extra_regions = copy_mapping (self->extra_regions);
	copy->extra_sources = copy_mapping (self->extra_sources);
	return copy;
}

/* ==========================================================================
 * Gathering geometry
 * ========================================================================== */

/* triangles collected before clipping */
typedef struct
{
	GArray  *tris;        /* gfloat, 9 per triangle, world space */
	GArray  *planes;      /* gfloat, 9 per triangle of infinite planes */
	GPtrArray *solids;    /* GArray of gfloat per closed convex collider */
	gboolean has_extent;
	gfloat   ext_min[3];
	gfloat   ext_max[3];
} Gather;

static void
gather_add (Gather       *g,
            GArray       *dest,
            const gfloat *local,
            guint         n_floats,
            const gfloat *matrix)
{
	guint i;

	for (i = 0; i + 9 <= n_floats; i += 9)
	{
		gfloat w[9];
		guint  k;

		for (k = 0; k < 3; k++)
		{
			if (matrix != NULL)
				lrg_math3d_transform_point (matrix, local + i + k * 3, w + k * 3);
			else
				memcpy (w + k * 3, local + i + k * 3, sizeof (gfloat) * 3);
		}
		if (!vec_finite (w) || !vec_finite (w + 3) || !vec_finite (w + 6))
			continue;
		g_array_append_vals (dest, w, 9);
		if (dest != g->planes)
		{
			for (k = 0; k < 9; k++)
			{
				guint axis = k % 3;

				if (!g->has_extent)
				{
					g->ext_min[axis] = g->ext_max[axis] = w[k];
					if (k == 8)
						g->has_extent = TRUE;
					continue;
				}
				g->ext_min[axis] = MIN (g->ext_min[axis], w[k]);
				g->ext_max[axis] = MAX (g->ext_max[axis], w[k]);
			}
		}
	}
}

/* indexed triangles into a flat soup */
static void
gather_indexed (Gather        *g,
                const gfloat  *pos,
                guint          n_vertices,
                const guint32 *idx,
                guint          n_indices,
                const gfloat  *matrix)
{
	g_autoptr(GArray) soup = g_array_new (FALSE, FALSE, sizeof (gfloat));
	guint             i;

	if (idx != NULL)
	{
		for (i = 0; i < n_indices - n_indices % 3u; i++)
		{
			if (idx[i] >= n_vertices)
				return;
			g_array_append_vals (soup, pos + (gsize) idx[i] * 3u, 3);
		}
	}
	else
		g_array_append_vals (soup, pos, (n_vertices - n_vertices % 3u) * 3u);
	gather_add (g, g->tris, (const gfloat *) (gpointer) soup->data, soup->len, matrix);
}

static gboolean
strv_has (GStrv        v,
          const gchar *s)
{
	return v != NULL && s != NULL && g_strv_contains ((const gchar *const *) v, s);
}

/* whether @ent passes the entity filters */
static gboolean
entity_passes (LrgNavBake  *self,
               LrgEntity3D *ent)
{
	guint i;

	if (ent == NULL || !lrg_entity3d_is_active_in_hierarchy (ent))
		return FALSE;
	if (self->static_only && !lrg_entity3d_get_static (ent))
		return FALSE;
	if ((lrg_entity3d_get_layer_mask (ent) & self->layer_mask) == 0)
		return FALSE;
	for (i = 0; self->exclude_tags[i] != NULL; i++)
		if (lrg_entity3d_has_tag (ent, self->exclude_tags[i]))
			return FALSE;
	if (self->include_tags[0] != NULL)
	{
		for (i = 0; self->include_tags[i] != NULL; i++)
			if (lrg_entity3d_has_tag (ent, self->include_tags[i]))
				return TRUE;
		return FALSE;
	}
	return TRUE;
}

/* a non-static rigid body on @ent: its geometry moves */
static gboolean
entity_moves (LrgEntity3D *ent)
{
	LrgComponent *rb = lrg_entity3d_get_component (ent, LRG_TYPE_RIGID_BODY_COMPONENT);

	return rb != NULL && lrg_component_get_enabled (rb) &&
	       lrg_rigid_body_component_get_body_kind (LRG_RIGID_BODY_COMPONENT (rb)) != LRG_DYNAMICS_BODY_STATIC;
}

/* whether @ent has a solid collider the collider pass picks up */
static gboolean
entity_has_solid_collider (LrgEntity3D *ent)
{
	g_autoptr(GPtrArray) cols = lrg_entity3d_get_components_of_type (ent, LRG_TYPE_COLLIDER_COMPONENT3D);
	guint                i;

	for (i = 0; i < cols->len; i++)
	{
		LrgColliderComponent3D *c = g_ptr_array_index (cols, i);

		if (lrg_component_get_enabled (LRG_COMPONENT (c)) && !lrg_collider_component3d_get_trigger (c))
			return TRUE;
	}
	return FALSE;
}

/* the render item parameters of a primitive (as the 3D renderer uses) */
static void
primitive_params (LrgMeshRendererComponent *mr,
                  LrgPrimitiveType          prim,
                  gfloat                   *params)
{
	gfloat sx = 1.0f, sy = 1.0f, sz = 1.0f, radius = 0.5f, height = 1.0f, major = 0.375f, minor = 0.125f;

	g_object_get (mr, "size-x", &sx, "size-y", &sy, "size-z", &sz, "radius", &radius, "height", &height,
	              "major-radius", &major, "minor-radius", &minor, NULL);
	memset (params, 0, 4 * sizeof (gfloat));
	switch (prim)
	{
	case LRG_PRIMITIVE_CUBE:
		params[0] = sx;
		params[1] = sy;
		params[2] = sz;
		break;
	case LRG_PRIMITIVE_UV_SPHERE:
	case LRG_PRIMITIVE_ICO_SPHERE:
	case LRG_PRIMITIVE_CIRCLE:
	case LRG_PRIMITIVE_CIRCLE_2D:
		params[0] = radius;
		break;
	case LRG_PRIMITIVE_CYLINDER:
	case LRG_PRIMITIVE_CONE:
		params[0] = radius;
		params[1] = height;
		break;
	case LRG_PRIMITIVE_TORUS:
		params[0] = major;
		params[1] = minor;
		break;
	case LRG_PRIMITIVE_PLANE:
		params[0] = sx;
		params[1] = sz;
		break;
	case LRG_PRIMITIVE_RECTANGLE_2D:
		params[0] = sx;
		params[1] = sy;
		break;
	default:
		break;
	}
}

/* a mesh renderer's triangles: model, headless glTF, or primitive */
static void
gather_mesh_renderer (Gather                   *g,
                      LrgMeshRendererComponent *mr,
                      const gfloat             *world)
{
	GrlModel    *grl = lrg_mesh_renderer_component_get_model (mr);
	const gchar *asset = lrg_mesh_renderer_component_get_mesh_asset (mr);

	/* a loaded model: every mesh under the model's own transform */
	if (grl != NULL && grl_model_get_handle (grl) != NULL)
	{
		const Model *model = (const Model *) grl_model_get_handle (grl);
		gfloat       mt[16];
		gfloat       full[16];
		gint         i;

		mt[0] = model->transform.m0;   mt[1] = model->transform.m1;
		mt[2] = model->transform.m2;   mt[3] = model->transform.m3;
		mt[4] = model->transform.m4;   mt[5] = model->transform.m5;
		mt[6] = model->transform.m6;   mt[7] = model->transform.m7;
		mt[8] = model->transform.m8;   mt[9] = model->transform.m9;
		mt[10] = model->transform.m10; mt[11] = model->transform.m11;
		mt[12] = model->transform.m12; mt[13] = model->transform.m13;
		mt[14] = model->transform.m14; mt[15] = model->transform.m15;
		lrg_math3d_multiply (world, mt, full);
		for (i = 0; i < model->meshCount; i++)
		{
			const Mesh         *me = &model->meshes[i];
			g_autofree guint32 *idx = NULL;
			gint                k;

			if (me->vertices == NULL || me->vertexCount <= 0)
				continue;
			if (me->indices != NULL)
			{
				idx = g_new (guint32, (gsize) MAX (me->triangleCount, 1) * 3u);
				for (k = 0; k < me->triangleCount * 3; k++)
					idx[k] = me->indices[k];
			}
			gather_indexed (g, me->vertices, (guint) me->vertexCount, idx,
			                (guint) MAX (me->triangleCount, 0) * 3u, full);
		}
		return;
	}

	/* a mesh asset readable without a window */
	if (asset != NULL && *asset != '\0')
	{
		g_autoptr(LrgGltfGeometry) geo = NULL;
		guint                      p;

		if (!g_file_test (asset, G_FILE_TEST_IS_REGULAR))
			return;
		geo = lrg_gltf_geometry_new_from_file (asset, NULL);
		if (geo == NULL)
			return;
		for (p = 0; p < lrg_gltf_geometry_get_primitive_count (geo); p++)
		{
			guint          nf = 0;
			guint          ni = 0;
			const gfloat  *pos = lrg_gltf_geometry_get_positions (geo, p, &nf);
			const guint32 *idx = lrg_gltf_geometry_get_indices (geo, p, &ni);

			if (pos != NULL && nf >= 9u)
				gather_indexed (g, pos, nf / 3u, idx, ni, world);
		}
		return;
	}

	/* the built-in primitive */
	{
		LrgPrimitiveType     prim = lrg_mesh_renderer_component_get_primitive (mr);
		gfloat               params[4];
		g_autoptr(LrgUvMesh) mesh = NULL;
		gsize                nf = 0;
		gsize                ni = 0;
		const gfloat        *pos;
		const guint32       *idx;

		primitive_params (mr, prim, params);
		mesh = lrg_uv_mesh_new_primitive (prim, params);
		if (mesh == NULL)
			return;
		pos = lrg_uv_mesh_get_positions (mesh, &nf);
		idx = lrg_uv_mesh_get_indices (mesh, &ni);
		if (pos == NULL)
			return;
		switch (prim)
		{
		case LRG_PRIMITIVE_CUBE:
		case LRG_PRIMITIVE_UV_SPHERE:
		case LRG_PRIMITIVE_ICO_SPHERE:
		case LRG_PRIMITIVE_CYLINDER:
		case LRG_PRIMITIVE_CONE:
			{
				/* closed convex primitives are filled: nothing walkable inside a wall */
				GArray *saved = g->tris;
				GArray *solid = g_array_new (FALSE, FALSE, sizeof (gfloat));

				g->tris = solid;
				gather_indexed (g, pos, (guint) (nf / 3u), idx, (guint) ni, world);
				g->tris = saved;
				g_ptr_array_add (g->solids, solid);
			}
			break;
		default:
			gather_indexed (g, pos, (guint) (nf / 3u), idx, (guint) ni, world);
			break;
		}
	}
}

/* a collider's triangles in world space (planes kept apart) */
static void
gather_collider (Gather                 *g,
                 LrgColliderComponent3D *col,
                 LrgEntity3D            *ent)
{
	g_autoptr(LrgPhysicsShape) shape = NULL;
	g_autoptr(GArray)          soup = NULL;
	gfloat                     scale[3];
	gfloat                     epos[3];
	gfloat                     erot[4];
	gfloat                     one[3] = { 1.0f, 1.0f, 1.0f };
	gfloat                     cpos[3];
	gfloat                     crot[4];
	gfloat                     em[16];
	gfloat                     cm[16];
	gfloat                     m[16];

	lrg_entity3d_get_world_scale (ent, scale);
	shape = lrg_collider_component3d_build_shape (col, scale);
	if (shape == NULL)
		return;
	soup = lrg_physics_shape_build_debug_mesh (shape, 16);

	/* the shape's frame: entity pose (scale baked into the shape) x offset */
	lrg_entity3d_get_world_position (ent, epos);
	lrg_entity3d_get_world_rotation (ent, erot);
	lrg_collider_component3d_get_local_transform (col, scale, cpos, crot);
	lrg_math3d_compose_quat (epos, erot, one, em);
	lrg_math3d_compose_quat (cpos, crot, one, cm);
	lrg_math3d_multiply (em, cm, m);
	switch (lrg_collider_component3d_get_kind (col))
	{
	case LRG_COLLIDER3D_PLANE:
		gather_add (g, g->planes, (const gfloat *) (gpointer) soup->data, soup->len, m);
		break;
	case LRG_COLLIDER3D_MESH:
		/* an open triangle mesh: its surface only */
		gather_add (g, g->tris, (const gfloat *) (gpointer) soup->data, soup->len, m);
		break;
	default:
		{
			/* a closed convex shape: filled, so nothing is walkable inside */
			GArray *solid = g_array_new (FALSE, FALSE, sizeof (gfloat));

			gather_add (g, solid, (const gfloat *) (gpointer) soup->data, soup->len, m);
			g_ptr_array_add (g->solids, solid);
		}
		break;
	}
}

/* Sutherland-Hodgman against one axis-aligned plane: keep v[axis] >= value (or <=) */
static guint
clip_axis (const gfloat *in,
           guint         n,
           guint         axis,
           gfloat        value,
           gboolean      keep_above,
           gfloat       *out)
{
	guint i;
	guint m = 0;

	for (i = 0; i < n; i++)
	{
		const gfloat *a = in + i * 3;
		const gfloat *b = in + ((i + 1) % n) * 3;
		gfloat        da = keep_above ? a[axis] - value : value - a[axis];
		gfloat        db = keep_above ? b[axis] - value : value - b[axis];

		if (da >= 0.0f)
		{
			memcpy (out + m * 3, a, sizeof (gfloat) * 3);
			m++;
		}
		if ((da >= 0.0f) != (db >= 0.0f))
		{
			gfloat t = da / (da - db);
			guint  k;

			for (k = 0; k < 3; k++)
				out[m * 3 + k] = a[k] + (b[k] - a[k]) * t;
			m++;
		}
	}
	return m;
}

/* stages @tris on @mesh, clipped to the X/Z box (and dropped outside Y) */
static guint
stage_clipped (LrgNavPolyMesh *mesh,
               GArray         *tris,
               gboolean        clip,
               const gfloat   *mn,
               const gfloat   *mx,
               gboolean        solid)
{
	g_autoptr(GArray) out = g_array_new (FALSE, FALSE, sizeof (gfloat));
	guint             i;

	for (i = 0; i + 9 <= tris->len; i += 9)
	{
		const gfloat *t = &g_array_index (tris, gfloat, i);
		gfloat        a[16 * 3];
		gfloat        b[16 * 3];
		guint         n = 3;
		guint         k;

		if (!clip)
		{
			g_array_append_vals (out, t, 9);
			continue;
		}
		if (MAX (MAX (t[1], t[4]), t[7]) < mn[1] || MIN (MIN (t[1], t[4]), t[7]) > mx[1])
			continue;
		memcpy (a, t, sizeof (gfloat) * 9);
		n = clip_axis (a, n, 0, mn[0], TRUE, b);
		n = n >= 3 ? clip_axis (b, n, 0, mx[0], FALSE, a) : 0;
		n = n >= 3 ? clip_axis (a, n, 2, mn[2], TRUE, b) : 0;
		n = n >= 3 ? clip_axis (b, n, 2, mx[2], FALSE, a) : 0;
		/* fan the clipped polygon (now in @a) */
		for (k = 1; n >= 3 && k + 1 < n; k++)
		{
			g_array_append_vals (out, a, 3);
			g_array_append_vals (out, a + k * 3, 3);
			g_array_append_vals (out, a + (k + 1) * 3, 3);
		}
	}
	if (out->len == 0)
		return 0;
	if (solid)
		return lrg_nav_poly_mesh_add_solid (mesh, (const gfloat *) (gpointer) out->data, out->len);
	return lrg_nav_poly_mesh_add_triangles (mesh, (const gfloat *) (gpointer) out->data, out->len, TRUE);
}

guint
lrg_nav_bake_collect_world (LrgNavBake     *self,
                            LrgWorld3D     *world,
                            LrgNavPolyMesh *mesh)
{
	Gather  g;
	gfloat  pmin[3];
	gfloat  pmax[3];
	guint   staged = 0;
	guint   i;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), 0);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), 0);
	g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (mesh), 0);

	memset (&g, 0, sizeof (g));
	g.tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g.planes = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g.solids = g_ptr_array_new_with_free_func ((GDestroyNotify) g_array_unref);

	/* --- mesh renderers --- */
	if (self->include_meshes)
	{
		g_autoptr(GPtrArray) comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_MESH_RENDERER_COMPONENT);

		for (i = 0; i < comps->len; i++)
		{
			LrgMeshRendererComponent *mr = g_ptr_array_index (comps, i);
			LrgEntity3D              *ent = lrg_component_get_entity (LRG_COMPONENT (mr));
			gfloat                    world_m[16];

			if (!lrg_component_get_enabled (LRG_COMPONENT (mr)) || !entity_passes (self, ent) ||
			    entity_moves (ent))
				continue;
			if (self->include_colliders && entity_has_solid_collider (ent))
				continue;
			lrg_entity3d_get_world_matrix (ent, world_m);
			gather_mesh_renderer (&g, mr, world_m);
		}
	}

	/* --- static colliders --- */
	if (self->include_colliders)
	{
		g_autoptr(GPtrArray) comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_COLLIDER_COMPONENT3D);

		for (i = 0; i < comps->len; i++)
		{
			LrgColliderComponent3D *col = g_ptr_array_index (comps, i);
			LrgEntity3D            *ent = lrg_component_get_entity (LRG_COMPONENT (col));
			const gchar            *layer;

			if (!lrg_component_get_enabled (LRG_COMPONENT (col)) || lrg_collider_component3d_get_trigger (col) ||
			    !entity_passes (self, ent) || entity_moves (ent))
				continue;
			layer = lrg_collider_component3d_get_layer (col);
			if (self->physics_layers[0] != NULL && !strv_has (self->physics_layers, layer != NULL ? layer : "default"))
				continue;
			gather_collider (&g, col, ent);
		}
	}

	/* --- clip and stage --- */
	if (self->has_bounds)
	{
		memcpy (pmin, self->bounds_min, sizeof (pmin));
		memcpy (pmax, self->bounds_max, sizeof (pmax));
	}
	else if (g.has_extent)
	{
		guint k;

		for (k = 0; k < 3; k++)
		{
			pmin[k] = g.ext_min[k] - 1.0f;
			pmax[k] = g.ext_max[k] + 1.0f;
		}
	}
	else
	{
		pmin[0] = pmin[2] = -50.0f;
		pmax[0] = pmax[2] = 50.0f;
		pmin[1] = -G_MAXFLOAT;
		pmax[1] = G_MAXFLOAT;
	}
	staged += stage_clipped (mesh, g.tris, self->has_bounds, self->bounds_min, self->bounds_max, FALSE);
	for (i = 0; i < g.solids->len; i++)
		staged += stage_clipped (mesh, g_ptr_array_index (g.solids, i), self->has_bounds, self->bounds_min,
		                         self->bounds_max, TRUE);
	if (!self->has_bounds && g.has_extent)
	{
		/* planes only need X/Z limits: keep every height */
		pmin[1] = -G_MAXFLOAT;
		pmax[1] = G_MAXFLOAT;
	}
	staged += stage_clipped (mesh, g.planes, TRUE, pmin, pmax, FALSE);
	g_array_unref (g.tris);
	g_array_unref (g.planes);
	g_ptr_array_unref (g.solids);
	return staged;
}

/* ==========================================================================
 * Baking
 * ========================================================================== */

void
lrg_nav_bake_apply_costs (LrgNavBake     *self,
                          LrgNavPolyMesh *mesh)
{
	guint i;

	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	g_return_if_fail (LRG_IS_NAV_POLY_MESH (mesh));

	for (i = 0; i < self->areas->len; i++)
	{
		const Area *a = g_ptr_array_index (self->areas, i);

		lrg_nav_poly_mesh_set_tag_cost (mesh, a->tag, a->cost);
	}
}

void
lrg_nav_bake_configure (LrgNavBake     *self,
                        LrgNavPolyMesh *mesh)
{
	guint i;

	g_return_if_fail (LRG_IS_NAV_BAKE (self));
	g_return_if_fail (LRG_IS_NAV_POLY_MESH (mesh));

	g_object_set (mesh,
	              "cell-size", self->cell_size,
	              "cell-height", self->cell_height,
	              "agent-radius", self->agent_radius,
	              "agent-height", self->agent_height,
	              "max-climb", self->max_step,
	              "max-slope", self->max_slope,
	              "max-drop", self->max_drop,
	              "corner-margin", self->corner_margin,
	              NULL);
	lrg_nav_poly_mesh_clear_input (mesh);

	/* area boxes, area order then box order (later ones win) */
	for (i = 0; i < self->areas->len; i++)
	{
		const Area *a = g_ptr_array_index (self->areas, i);
		guint       b;

		for (b = 0; b < a->boxes->len / 6; b++)
			lrg_nav_poly_mesh_add_area_box (mesh, &g_array_index (a->boxes, gfloat, b * 6),
			                                &g_array_index (a->boxes, gfloat, b * 6 + 3), a->tag);
	}

	/* links: the mesh keeps them across clear_input, add them once */
	if (lrg_nav_poly_mesh_get_link_count (mesh) - lrg_nav_poly_mesh_get_drop_link_count (mesh) == 0)
	{
		for (i = 0; i < self->links->len; i++)
		{
			const Link *l = g_ptr_array_index (self->links, i);
			gint        area = lrg_nav_bake_find_area (self, l->area);
			guint32     tag = area >= 0 ? ((Area *) g_ptr_array_index (self->areas, (guint) area))->tag : 0;

			lrg_nav_poly_mesh_add_link (mesh, l->start, l->end, l->bidirectional, l->cost, tag);
		}
	}
	lrg_nav_bake_apply_costs (self, mesh);
}

/* disables the regions below the minimum walkable area */
static void
filter_regions (LrgNavBake     *self,
                LrgNavPolyMesh *mesh)
{
	g_autofree gdouble *area = NULL;
	guint               n_regions;
	guint               n_polys;
	guint               i;

	if (self->min_region_area <= 0.0f)
		return;
	n_regions = lrg_nav_poly_mesh_get_region_count (mesh);
	n_polys = lrg_nav_poly_mesh_get_polygon_count (mesh);
	area = g_new0 (gdouble, n_regions + 1);
	for (i = 0; i < n_polys; i++)
	{
		gfloat mn[3];
		gfloat mx[3];
		guint  region = 0;

		lrg_nav_poly_mesh_get_polygon (mesh, i, mn, mx, NULL, &region);
		if (region <= n_regions)
			area[region] += (gdouble) (mx[0] - mn[0]) * (gdouble) (mx[2] - mn[2]);
	}
	for (i = 0; i < n_polys; i++)
	{
		guint region = lrg_nav_poly_mesh_get_polygon_region (mesh, i);

		if (region <= n_regions && area[region] < (gdouble) self->min_region_area)
			lrg_nav_poly_mesh_set_polygon_enabled (mesh, i, FALSE);
	}
}

/* How many tiles cover @span at @extent. 20 / 10 is exactly 2. */
static guint
tile_span_count (gfloat span,
                 gfloat extent)
{
	guint n;

	if (!(extent > 0.0f) || !(span > 0.0f))
		return 1;
	n = (guint) floor ((double) span / (double) extent + 1e-4);
	if (n < 1)
		n = 1;
	if ((gfloat) n * extent < span - 1e-3f)
		n++;
	return n;
}

/* One explicit triangle, clipped to the X/Z box and dropped outside Y. */
static void
append_clipped_triangle (GArray       *tris,
                         GArray       *walk,
                         const gfloat *t,
                         guint8        w,
                         const gfloat *mn,
                         const gfloat *mx)
{
	gfloat a[16 * 3];
	gfloat b[16 * 3];
	guint  n = 3;
	guint  k;

	if (MAX (MAX (t[1], t[4]), t[7]) < mn[1] || MIN (MIN (t[1], t[4]), t[7]) > mx[1])
		return;
	memcpy (a, t, sizeof (gfloat) * 9);
	n = clip_axis (a, n, 0, mn[0], TRUE, b);
	n = n >= 3 ? clip_axis (b, n, 0, mx[0], FALSE, a) : 0;
	n = n >= 3 ? clip_axis (a, n, 2, mn[2], TRUE, b) : 0;
	n = n >= 3 ? clip_axis (b, n, 2, mx[2], FALSE, a) : 0;
	for (k = 1; n >= 3 && k + 1 < n; k++)
	{
		g_array_append_vals (tris, a, 3);
		g_array_append_vals (tris, a + k * 3, 3);
		g_array_append_vals (tris, a + (k + 1) * 3, 3);
		g_array_append_val (walk, w);
	}
}

static void
clip_explicit (GArray       *src_tris,
               GArray       *src_walk,
               const gfloat *mn,
               const gfloat *mx,
               GArray       *out_tris,
               GArray       *out_walk)
{
	guint i;

	for (i = 0; i < src_walk->len; i++)
	{
		const gfloat *t = &g_array_index (src_tris, gfloat, i * 9);
		guint8        w = g_array_index (src_walk, guint8, i);

		append_clipped_triangle (out_tris, out_walk, t, w, mn, mx);
	}
}

/* The one-shot bake. tile_extent is ignored here. */
static gboolean
bake_one (LrgNavBake           *self,
          LrgNavPolyMesh       *mesh,
          LrgWorld3D           *world,
          LrgNavPolyMeshCache  *cache,
          GError              **error)
{
	guint i;
	guint staged = 0;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);
	g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (mesh), FALSE);
	g_return_val_if_fail (world == NULL || LRG_IS_WORLD3D (world), FALSE);
	g_return_val_if_fail (cache == NULL || LRG_IS_NAV_POLY_MESH_CACHE (cache), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	self->last_tiles = 1;
	lrg_nav_bake_configure (self, mesh);
	if (world != NULL)
		staged += lrg_nav_bake_collect_world (self, world, mesh);

	/* explicit triangles, walkable and obstructing runs kept apart */
	for (i = 0; i < self->walkable->len; )
	{
		guint8 w = g_array_index (self->walkable, guint8, i);
		guint  j = i;

		while (j < self->walkable->len && g_array_index (self->walkable, guint8, j) == w)
			j++;
		staged += lrg_nav_poly_mesh_add_triangles (mesh, &g_array_index (self->triangles, gfloat, i * 9),
		                                           (j - i) * 9, w != 0);
		i = j;
	}
	self->last_triangles = staged;
	if (staged == 0)
	{
		g_set_error_literal (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_NO_GEOMETRY,
		                     "navigation bake: no geometry (check the sources, filters and bounds)");
		return FALSE;
	}
	if (!lrg_nav_poly_mesh_bake_cached (mesh, cache, error))
		return FALSE;
	filter_regions (self, mesh);
	return TRUE;
}

/*
 * bake_tiled:
 *
 * Splits the X/Z bounds into squares, bakes each with tile_extent forced
 * to 0 (so this does not recurse) and unions the enabled polygons. The
 * destination mesh is not configured until that union is ready. Empty
 * tiles are skipped. On a hard error the caller's bounds, triangle
 * arrays and last-bake counters are put back.
 */
static gboolean
bake_tiled (LrgNavBake           *self,
            LrgNavPolyMesh       *mesh,
            LrgWorld3D           *world,
            LrgNavPolyMeshCache  *cache,
            GError              **error)
{
	gfloat  saved_min[3];
	gfloat  saved_max[3];
	gfloat  extent;
	guint   saved_last_triangles;
	guint   saved_last_tiles;
	guint   nx;
	guint   nz;
	guint   ix;
	guint   iz;
	GArray *orig_tris;
	GArray *orig_walk;
	GArray *union_tris;
	GArray *union_walk;

	extent = self->tile_extent;
	saved_last_triangles = self->last_triangles;
	saved_last_tiles = self->last_tiles;
	memcpy (saved_min, self->bounds_min, sizeof (saved_min));
	memcpy (saved_max, self->bounds_max, sizeof (saved_max));
	orig_tris = self->triangles;
	orig_walk = self->walkable;
	union_tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	union_walk = g_array_new (FALSE, FALSE, sizeof (guint8));
	nx = tile_span_count (saved_max[0] - saved_min[0], extent);
	nz = tile_span_count (saved_max[2] - saved_min[2], extent);
	self->tile_extent = 0.0f;

	for (iz = 0; iz < nz; iz++)
	{
		for (ix = 0; ix < nx; ix++)
		{
			g_autoptr(LrgNavPolyMesh) tile = lrg_nav_poly_mesh_new ();
			g_autoptr(GArray)         clipped = g_array_new (FALSE, FALSE, sizeof (gfloat));
			g_autoptr(GArray)         clipped_w = g_array_new (FALSE, FALSE, sizeof (guint8));
			g_autoptr(GError)         local = NULL;
			gfloat                    tmin[3];
			gfloat                    tmax[3];
			guint                     p;
			guint                     npoly;

			tmin[0] = saved_min[0] + (gfloat) ix * extent;
			tmin[1] = saved_min[1];
			tmin[2] = saved_min[2] + (gfloat) iz * extent;
			tmax[0] = tmin[0] + extent;
			tmax[1] = saved_max[1];
			tmax[2] = tmin[2] + extent;
			if (tmax[0] > saved_max[0])
				tmax[0] = saved_max[0];
			if (tmax[2] > saved_max[2])
				tmax[2] = saved_max[2];
			memcpy (self->bounds_min, tmin, sizeof (tmin));
			memcpy (self->bounds_max, tmax, sizeof (tmax));
			clip_explicit (orig_tris, orig_walk, tmin, tmax, clipped, clipped_w);
			self->triangles = clipped;
			self->walkable = clipped_w;
			if (!bake_one (self, tile, world, NULL, &local))
			{
				self->triangles = orig_tris;
				self->walkable = orig_walk;
				if (g_error_matches (local, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_NO_GEOMETRY))
				{
					g_clear_error (&local);
					continue;
				}
				g_propagate_error (error, g_steal_pointer (&local));
				goto fail;
			}
			self->triangles = orig_tris;
			self->walkable = orig_walk;

			npoly = lrg_nav_poly_mesh_get_polygon_count (tile);
			for (p = 0; p < npoly; p++)
			{
				gfloat c[12];
				guint8 one = 1;

				if (!lrg_nav_poly_mesh_get_polygon_enabled (tile, p))
					continue;
				if (!lrg_nav_poly_mesh_get_polygon_corners (tile, p, c))
					continue;
				g_array_append_vals (union_tris, c, 3);
				g_array_append_vals (union_tris, c + 3, 3);
				g_array_append_vals (union_tris, c + 6, 3);
				g_array_append_val (union_walk, one);
				g_array_append_vals (union_tris, c, 3);
				g_array_append_vals (union_tris, c + 6, 3);
				g_array_append_vals (union_tris, c + 9, 3);
				g_array_append_val (union_walk, one);
			}
		}
	}

	memcpy (self->bounds_min, saved_min, sizeof (saved_min));
	memcpy (self->bounds_max, saved_max, sizeof (saved_max));
	self->tile_extent = extent;
	self->triangles = orig_tris;
	self->walkable = orig_walk;

	if (union_tris->len == 0)
	{
		g_set_error_literal (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_NO_GEOMETRY,
		                     "navigation bake: no geometry (check the sources, filters and bounds)");
		goto fail_counts;
	}

	self->triangles = union_tris;
	self->walkable = union_walk;
	if (!bake_one (self, mesh, NULL, cache, error))
	{
		self->triangles = orig_tris;
		self->walkable = orig_walk;
		goto fail_counts;
	}
	self->triangles = orig_tris;
	self->walkable = orig_walk;
	self->last_tiles = nx * nz;
	g_array_unref (union_tris);
	g_array_unref (union_walk);
	return TRUE;

fail:
	memcpy (self->bounds_min, saved_min, sizeof (saved_min));
	memcpy (self->bounds_max, saved_max, sizeof (saved_max));
	self->tile_extent = extent;
	self->triangles = orig_tris;
	self->walkable = orig_walk;
fail_counts:
	self->last_triangles = saved_last_triangles;
	self->last_tiles = saved_last_tiles;
	g_array_unref (union_tris);
	g_array_unref (union_walk);
	return FALSE;
}

gboolean
lrg_nav_bake_bake_into (LrgNavBake           *self,
                        LrgNavPolyMesh       *mesh,
                        LrgWorld3D           *world,
                        LrgNavPolyMeshCache  *cache,
                        GError              **error)
{
	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), FALSE);
	g_return_val_if_fail (LRG_IS_NAV_POLY_MESH (mesh), FALSE);
	g_return_val_if_fail (world == NULL || LRG_IS_WORLD3D (world), FALSE);
	g_return_val_if_fail (cache == NULL || LRG_IS_NAV_POLY_MESH_CACHE (cache), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->tile_extent > 0.0f && self->has_bounds)
		return bake_tiled (self, mesh, world, cache, error);
	return bake_one (self, mesh, world, cache, error);
}

LrgNavPolyMesh *
lrg_nav_bake_bake (LrgNavBake           *self,
                   LrgWorld3D           *world,
                   LrgNavPolyMeshCache  *cache,
                   GError              **error)
{
	g_autoptr(LrgNavPolyMesh) mesh = NULL;

	g_return_val_if_fail (LRG_IS_NAV_BAKE (self), NULL);

	mesh = lrg_nav_poly_mesh_new ();
	if (!lrg_nav_bake_bake_into (self, mesh, world, cache, error))
		return NULL;
	return g_steal_pointer (&mesh);
}
