/* lrg-mesh-renderer-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshRendererComponent: submits one render item per frame for a
 * built-in primitive or a mesh asset at the owning entity's world matrix.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-mesh-renderer-component.h"
#include "../lrg-entity3d.h"
#include "../../scene/lrg-material3d.h"
#include "../../core/lrg-asset-manager.h"

struct _LrgMeshRendererComponent
{
	LrgComponent      parent_instance;

	/* Geometry */
	LrgPrimitiveType  primitive;
	gchar            *mesh_asset;
	GrlModel         *model;           /* explicit or resolved; owned ref */
	gboolean          model_explicit;  /* set_model() rather than resolved */
	gboolean          load_failed;     /* do not retry until asset/manager change */
	LrgAssetManager  *asset_manager;
	gint              mesh_index;
	gfloat            size[3];
	gfloat            radius;
	gfloat            height;
	gfloat            major_radius;
	gfloat            minor_radius;

	/* Material */
	gfloat            color[4];
	gfloat            roughness;
	gfloat            metallic;
	gfloat            emission[3];
	gfloat            emission_strength;
	GObject          *material;        /* the slot (nullable) */
	LrgMaterial3D    *scalar_material; /* synced from the scalars */
	GPtrArray        *submesh;         /* LrgMaterial* or NULL per mesh index */

	/* Render state */
	gboolean          cast_shadows;
	gboolean          receive_shadows;
	gboolean          transparent;
	gboolean          double_sided;
	gboolean          unlit;
	gint              layer;

	/* Baked lighting (0.9.0 M3) */
	gboolean          static_lighting;
	gfloat            lightmap_scale;
};

G_DEFINE_FINAL_TYPE (LrgMeshRendererComponent, lrg_mesh_renderer_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_PRIMITIVE,
	PROP_MESH_ASSET,
	PROP_MODEL,
	PROP_ASSET_MANAGER,
	PROP_MESH_INDEX,
	PROP_SIZE_X,
	PROP_SIZE_Y,
	PROP_SIZE_Z,
	PROP_RADIUS,
	PROP_HEIGHT,
	PROP_MAJOR_RADIUS,
	PROP_MINOR_RADIUS,
	PROP_COLOR_R,
	PROP_COLOR_G,
	PROP_COLOR_B,
	PROP_COLOR_A,
	PROP_ROUGHNESS,
	PROP_METALLIC,
	PROP_EMISSION_R,
	PROP_EMISSION_G,
	PROP_EMISSION_B,
	PROP_EMISSION_STRENGTH,
	PROP_MATERIAL,
	PROP_CAST_SHADOWS,
	PROP_RECEIVE_SHADOWS,
	PROP_TRANSPARENT,
	PROP_DOUBLE_SIDED,
	PROP_UNLIT,
	PROP_LAYER,
	PROP_STATIC_LIGHTING,
	PROP_LIGHTMAP_SCALE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Copies the scalar material properties into the internal LrgMaterial3D
 * the items carry when the material slot is empty. */
static void
sync_scalar_material (LrgMeshRendererComponent *self)
{
	lrg_material3d_set_color (self->scalar_material, self->color[0], self->color[1],
	                          self->color[2], self->color[3]);
	lrg_material3d_set_roughness (self->scalar_material, self->roughness);
	lrg_material3d_set_metallic (self->scalar_material, self->metallic);
	lrg_material3d_set_emission_color (self->scalar_material, self->emission[0],
	                                   self->emission[1], self->emission[2], 1.0f);
	lrg_material3d_set_emission_strength (self->scalar_material, self->emission_strength);
}

/* Drops a resolved (not explicit) model so the asset resolves again. */
static void
drop_resolved_model (LrgMeshRendererComponent *self)
{
	if (!self->model_explicit)
		g_clear_object (&self->model);
	self->load_failed = FALSE;
}

/* Resolves the mesh asset into a model through the asset manager, once.
 * Without a manager (or without a graphics context, which makes the load
 * fail) the item goes out with model == NULL and the renderer resolves
 * the asset itself from lrg_mesh_renderer_component_get_mesh_asset(). */
static void
resolve_model (LrgMeshRendererComponent *self)
{
	g_autoptr(GError) error = NULL;
	GrlModel         *model;

	if (self->model != NULL || self->mesh_asset == NULL || self->asset_manager == NULL ||
	    self->load_failed)
		return;

	model = lrg_asset_manager_load_model (self->asset_manager, self->mesh_asset, &error);
	if (model == NULL)
	{
		g_debug ("LrgMeshRendererComponent: cannot load '%s': %s", self->mesh_asset,
		         error != NULL ? error->message : "unknown error");
		self->load_failed = TRUE;
		return;
	}
	self->model = (GrlModel *) g_object_ref (model);
}

/* Sets @out to the box [-hx,hx] x [-hy,hy] x [-hz,hz]. */
static void
set_box (LrgBoundingBox3D *out,
         gfloat            hx,
         gfloat            hy,
         gfloat            hz)
{
	out->min.x = -hx;
	out->min.y = -hy;
	out->min.z = -hz;
	out->max.x = hx;
	out->max.y = hy;
	out->max.z = hz;
}

/* A key grouping items that share a mesh or shape, so the opaque list
 * draws them back to back: kind in the top byte, then the shape or the
 * model pointer. */
static guint64
make_sort_key (LrgRenderItemKind kind,
               guint64           detail)
{
	return ((guint64) kind << 56) | (detail & G_GUINT64_CONSTANT (0x00ffffffffffffff));
}

/* Free function of the submesh array, whose holes are NULL. */
static void
unref_nullable (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}

/* The flags an LrgMaterial adds (queue, culling, shading model). */
static guint
material_flags (LrgMaterial *material)
{
	guint flags = 0;

	switch (lrg_material_resolve_render_queue (material))
	{
	case LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT:
		flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		break;
	case LRG_MATERIAL_RENDER_QUEUE_CUTOUT:
		flags |= LRG_RENDER_ITEM_FLAG_CUTOUT;
		break;
	case LRG_MATERIAL_RENDER_QUEUE_AUTO:
	case LRG_MATERIAL_RENDER_QUEUE_OPAQUE:
	default:
		break;
	}
	if (lrg_material_get_cull_mode (material) == LRG_MATERIAL_CULL_NONE)
		flags |= LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED;
	if (lrg_material_get_shading_model (material) == LRG_SHADING_MODEL_UNLIT)
		flags |= LRG_RENDER_ITEM_FLAG_UNLIT;
	return flags;
}

/* Colour and flags of an item drawn with @material: an LrgMaterial's
 * base colour and flags, otherwise the component's colour and flags. */
static void
item_set_material (LrgMeshRendererComponent *self,
                   LrgRenderItem            *item,
                   GObject                  *material)
{
	guint flags;

	if (material != NULL && LRG_IS_MATERIAL (material))
	{
		lrg_material_get_base_color (LRG_MATERIAL (material), item->color);
		flags = (guint) lrg_mesh_renderer_component_get_flags (self) &
		        ~(guint) (LRG_RENDER_ITEM_FLAG_TRANSPARENT | LRG_RENDER_ITEM_FLAG_CUTOUT);
		/* the component's own switches still add */
		if (self->transparent)
			flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		item->flags = (LrgRenderItemFlags) (flags | material_flags (LRG_MATERIAL (material)));
		return;
	}
	memcpy (item->color, self->color, sizeof item->color);
	flags = (guint) lrg_mesh_renderer_component_get_flags (self);
	if (self->material != NULL && LRG_IS_MATERIAL (self->material))
	{
		/* a legacy submesh material under an LrgMaterial slot: the
		 * component's own flags without the slot's */
		flags = 0;
		if (self->cast_shadows)
			flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
		if (self->receive_shadows)
			flags |= LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
		if (self->transparent || self->color[3] < 1.0f)
			flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		if (self->double_sided)
			flags |= LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED;
		if (self->unlit)
			flags |= LRG_RENDER_ITEM_FLAG_UNLIT;
	}
	item->flags = (LrgRenderItemFlags) flags;
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static gboolean
lrg_mesh_renderer_component_get_bounds (LrgComponent     *component,
                                        LrgBoundingBox3D *out_local)
{
	return lrg_mesh_renderer_component_get_local_bounds (LRG_MESH_RENDERER_COMPONENT (component),
	                                                     out_local);
}

/* Submits one item: PRIMITIVE for a built-in shape, MODEL for a mesh
 * asset (with the resolved model, or NULL for the renderer to resolve). */
static void
lrg_mesh_renderer_component_collect_render (LrgComponent  *component,
                                            LrgRenderList *list)
{
	LrgMeshRendererComponent *self = LRG_MESH_RENDERER_COMPONENT (component);
	LrgEntity3D              *entity = lrg_component_get_entity (component);
	LrgRenderItem             item;
	LrgBoundingBox3D          local;

	if (entity == NULL)
		return;

	lrg_render_item_init_for_entity (&item, entity, component);

	if (self->mesh_asset != NULL || self->model_explicit)
	{
		/* A mesh asset (or an explicitly assigned model) */
		resolve_model (self);
		item.kind = LRG_RENDER_ITEM_MODEL;
		item.model = self->model;
		item.mesh_index = self->mesh_index;
		item.sort_key = make_sort_key (LRG_RENDER_ITEM_MODEL, (guint64) (guintptr) self->model);
	}
	else
	{
		/* A built-in shape: the params follow the LrgRenderItem contract */
		item.kind = LRG_RENDER_ITEM_PRIMITIVE;
		item.primitive = self->primitive;
		memset (item.params, 0, sizeof item.params);
		switch (self->primitive)
		{
		case LRG_PRIMITIVE_CUBE:
			item.params[0] = self->size[0];
			item.params[1] = self->size[1];
			item.params[2] = self->size[2];
			break;
		case LRG_PRIMITIVE_UV_SPHERE:
		case LRG_PRIMITIVE_ICO_SPHERE:
		case LRG_PRIMITIVE_CIRCLE:
		case LRG_PRIMITIVE_CIRCLE_2D:
			item.params[0] = self->radius;
			break;
		case LRG_PRIMITIVE_CYLINDER:
		case LRG_PRIMITIVE_CONE:
			item.params[0] = self->radius;
			item.params[1] = self->height;
			break;
		case LRG_PRIMITIVE_TORUS:
			item.params[0] = self->major_radius;
			item.params[1] = self->minor_radius;
			break;
		case LRG_PRIMITIVE_PLANE:
		case LRG_PRIMITIVE_GRID:
			item.params[0] = self->size[0];
			item.params[1] = self->size[2];
			break;
		case LRG_PRIMITIVE_RECTANGLE_2D:
			item.params[0] = self->size[0];
			item.params[1] = self->size[1];
			break;
		case LRG_PRIMITIVE_MESH:
		default:
			break;
		}
		item.sort_key = make_sort_key (LRG_RENDER_ITEM_PRIMITIVE, (guint64) self->primitive);
	}

	/* Material, colour, state and layer */
	item.material = lrg_mesh_renderer_component_get_effective_material (self);
	item_set_material (self, &item, item.material);
	if (self->layer >= 0)
		item.layer = (guint) self->layer;

	if (lrg_mesh_renderer_component_get_local_bounds (self, &local))
		lrg_render_item_set_local_bounds (&item, &local);

	/* Submesh materials: one item per mesh of a resolved model. */
	if (item.kind == LRG_RENDER_ITEM_MODEL && self->model != NULL && self->mesh_index < 0 &&
	    lrg_mesh_renderer_component_get_n_submesh_materials (self) > 0)
	{
		gint n = grl_model_get_mesh_count (self->model);
		gint i;

		for (i = 0; i < n; i++)
		{
			LrgRenderItem sub = item;
			LrgMaterial  *m = lrg_mesh_renderer_component_get_submesh_material (self, (guint) i);

			sub.mesh_index = i;
			sub.material = m != NULL ? G_OBJECT (m) : item.material;
			item_set_material (self, &sub, sub.material);
			if (lrg_render_list_accepts (list, &sub))
				lrg_render_list_add (list, &sub);
		}
		return;
	}

	if (lrg_render_list_accepts (list, &item))
		lrg_render_list_add (list, &item);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_mesh_renderer_component_dispose (GObject *object)
{
	LrgMeshRendererComponent *self = LRG_MESH_RENDERER_COMPONENT (object);

	g_clear_object (&self->model);
	g_clear_object (&self->asset_manager);
	g_clear_object (&self->material);
	g_clear_object (&self->scalar_material);
	g_clear_pointer (&self->submesh, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_mesh_renderer_component_parent_class)->dispose (object);
}

static void
lrg_mesh_renderer_component_finalize (GObject *object)
{
	LrgMeshRendererComponent *self = LRG_MESH_RENDERER_COMPONENT (object);

	g_free (self->mesh_asset);

	G_OBJECT_CLASS (lrg_mesh_renderer_component_parent_class)->finalize (object);
}

/* Stores @value into the float at @slot and notifies @prop when it changed.
 * Returns TRUE on a change. */
static gboolean
set_float (LrgMeshRendererComponent *self,
           gfloat                   *slot,
           gfloat                    value,
           guint                     prop)
{
	if (*slot == value)
		return FALSE;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
	return TRUE;
}

/* Stores a boolean and notifies on change. */
static void
set_bool (LrgMeshRendererComponent *self,
          gboolean                 *slot,
          gboolean                  value,
          guint                     prop)
{
	value = !!value;
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_mesh_renderer_component_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
	LrgMeshRendererComponent *self = LRG_MESH_RENDERER_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_PRIMITIVE:         g_value_set_enum (value, (gint) self->primitive); break;
	case PROP_MESH_ASSET:        g_value_set_string (value, self->mesh_asset); break;
	case PROP_MODEL:             g_value_set_object (value, self->model); break;
	case PROP_ASSET_MANAGER:     g_value_set_object (value, self->asset_manager); break;
	case PROP_MESH_INDEX:        g_value_set_int (value, self->mesh_index); break;
	case PROP_SIZE_X:            g_value_set_float (value, self->size[0]); break;
	case PROP_SIZE_Y:            g_value_set_float (value, self->size[1]); break;
	case PROP_SIZE_Z:            g_value_set_float (value, self->size[2]); break;
	case PROP_RADIUS:            g_value_set_float (value, self->radius); break;
	case PROP_HEIGHT:            g_value_set_float (value, self->height); break;
	case PROP_MAJOR_RADIUS:      g_value_set_float (value, self->major_radius); break;
	case PROP_MINOR_RADIUS:      g_value_set_float (value, self->minor_radius); break;
	case PROP_COLOR_R:           g_value_set_float (value, self->color[0]); break;
	case PROP_COLOR_G:           g_value_set_float (value, self->color[1]); break;
	case PROP_COLOR_B:           g_value_set_float (value, self->color[2]); break;
	case PROP_COLOR_A:           g_value_set_float (value, self->color[3]); break;
	case PROP_ROUGHNESS:         g_value_set_float (value, self->roughness); break;
	case PROP_METALLIC:          g_value_set_float (value, self->metallic); break;
	case PROP_EMISSION_R:        g_value_set_float (value, self->emission[0]); break;
	case PROP_EMISSION_G:        g_value_set_float (value, self->emission[1]); break;
	case PROP_EMISSION_B:        g_value_set_float (value, self->emission[2]); break;
	case PROP_EMISSION_STRENGTH: g_value_set_float (value, self->emission_strength); break;
	case PROP_MATERIAL:          g_value_set_object (value, self->material); break;
	case PROP_CAST_SHADOWS:      g_value_set_boolean (value, self->cast_shadows); break;
	case PROP_RECEIVE_SHADOWS:   g_value_set_boolean (value, self->receive_shadows); break;
	case PROP_TRANSPARENT:       g_value_set_boolean (value, self->transparent); break;
	case PROP_DOUBLE_SIDED:      g_value_set_boolean (value, self->double_sided); break;
	case PROP_UNLIT:             g_value_set_boolean (value, self->unlit); break;
	case PROP_LAYER:             g_value_set_int (value, self->layer); break;
	case PROP_STATIC_LIGHTING:   g_value_set_boolean (value, self->static_lighting); break;
	case PROP_LIGHTMAP_SCALE:    g_value_set_float (value, self->lightmap_scale); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_mesh_renderer_component_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
	LrgMeshRendererComponent *self = LRG_MESH_RENDERER_COMPONENT (object);
	gboolean                  material_changed = FALSE;

	switch (prop_id)
	{
	case PROP_PRIMITIVE:
		lrg_mesh_renderer_component_set_primitive (self, (LrgPrimitiveType) g_value_get_enum (value));
		break;
	case PROP_MESH_ASSET:
		lrg_mesh_renderer_component_set_mesh_asset (self, g_value_get_string (value));
		break;
	case PROP_MODEL:
		lrg_mesh_renderer_component_set_model (self, g_value_get_object (value));
		break;
	case PROP_ASSET_MANAGER:
		lrg_mesh_renderer_component_set_asset_manager (self, g_value_get_object (value));
		break;
	case PROP_MESH_INDEX:
		if (self->mesh_index != g_value_get_int (value))
		{
			self->mesh_index = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SIZE_X: set_float (self, &self->size[0], g_value_get_float (value), prop_id); break;
	case PROP_SIZE_Y: set_float (self, &self->size[1], g_value_get_float (value), prop_id); break;
	case PROP_SIZE_Z: set_float (self, &self->size[2], g_value_get_float (value), prop_id); break;
	case PROP_RADIUS: set_float (self, &self->radius, g_value_get_float (value), prop_id); break;
	case PROP_HEIGHT: set_float (self, &self->height, g_value_get_float (value), prop_id); break;
	case PROP_MAJOR_RADIUS:
		set_float (self, &self->major_radius, g_value_get_float (value), prop_id);
		break;
	case PROP_MINOR_RADIUS:
		set_float (self, &self->minor_radius, g_value_get_float (value), prop_id);
		break;
	case PROP_COLOR_R:
		material_changed = set_float (self, &self->color[0], g_value_get_float (value), prop_id);
		break;
	case PROP_COLOR_G:
		material_changed = set_float (self, &self->color[1], g_value_get_float (value), prop_id);
		break;
	case PROP_COLOR_B:
		material_changed = set_float (self, &self->color[2], g_value_get_float (value), prop_id);
		break;
	case PROP_COLOR_A:
		material_changed = set_float (self, &self->color[3], g_value_get_float (value), prop_id);
		break;
	case PROP_ROUGHNESS:
		material_changed = set_float (self, &self->roughness, g_value_get_float (value), prop_id);
		break;
	case PROP_METALLIC:
		material_changed = set_float (self, &self->metallic, g_value_get_float (value), prop_id);
		break;
	case PROP_EMISSION_R:
		material_changed = set_float (self, &self->emission[0], g_value_get_float (value), prop_id);
		break;
	case PROP_EMISSION_G:
		material_changed = set_float (self, &self->emission[1], g_value_get_float (value), prop_id);
		break;
	case PROP_EMISSION_B:
		material_changed = set_float (self, &self->emission[2], g_value_get_float (value), prop_id);
		break;
	case PROP_EMISSION_STRENGTH:
		material_changed = set_float (self, &self->emission_strength, g_value_get_float (value),
		                              prop_id);
		break;
	case PROP_MATERIAL:
		lrg_mesh_renderer_component_set_material (self, g_value_get_object (value));
		break;
	case PROP_CAST_SHADOWS:
		set_bool (self, &self->cast_shadows, g_value_get_boolean (value), prop_id);
		break;
	case PROP_RECEIVE_SHADOWS:
		set_bool (self, &self->receive_shadows, g_value_get_boolean (value), prop_id);
		break;
	case PROP_TRANSPARENT:
		set_bool (self, &self->transparent, g_value_get_boolean (value), prop_id);
		break;
	case PROP_DOUBLE_SIDED:
		set_bool (self, &self->double_sided, g_value_get_boolean (value), prop_id);
		break;
	case PROP_UNLIT:
		set_bool (self, &self->unlit, g_value_get_boolean (value), prop_id);
		break;
	case PROP_LAYER:
		lrg_mesh_renderer_component_set_layer (self, g_value_get_int (value));
		break;
	case PROP_STATIC_LIGHTING:
		lrg_mesh_renderer_component_set_static_lighting (self, g_value_get_boolean (value));
		break;
	case PROP_LIGHTMAP_SCALE:
		lrg_mesh_renderer_component_set_lightmap_scale (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}

	if (material_changed)
		sync_scalar_material (self);
}

/* Declares a float property in [min, max] with default @def. */
static GParamSpec *
float_prop (const gchar *name,
            const gchar *blurb,
            gfloat       min,
            gfloat       max,
            gfloat       def)
{
	return g_param_spec_float (name, NULL, blurb, min, max, def,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

/* Declares a boolean property with default @def. */
static GParamSpec *
bool_prop (const gchar *name,
           const gchar *blurb,
           gboolean     def)
{
	return g_param_spec_boolean (name, NULL, blurb, def,
	                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_mesh_renderer_component_class_init (LrgMeshRendererComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_mesh_renderer_component_dispose;
	object_class->finalize = lrg_mesh_renderer_component_finalize;
	object_class->get_property = lrg_mesh_renderer_component_get_property;
	object_class->set_property = lrg_mesh_renderer_component_set_property;

	component_class->collect_render = lrg_mesh_renderer_component_collect_render;
	component_class->get_bounds = lrg_mesh_renderer_component_get_bounds;

	/**
	 * LrgMeshRendererComponent:primitive:
	 *
	 * The built-in shape drawn when no mesh asset is set (the editor's
	 * PRIMITIVE visual).
	 */
	properties[PROP_PRIMITIVE] =
		g_param_spec_enum ("primitive", NULL, "Built-in shape", LRG_TYPE_PRIMITIVE_TYPE,
		                   LRG_PRIMITIVE_CUBE, rw);

	/**
	 * LrgMeshRendererComponent:mesh-asset:
	 *
	 * A mesh asset path or GUID (the editor's MESH_ASSET visual); wins over
	 * #LrgMeshRendererComponent:primitive.
	 */
	properties[PROP_MESH_ASSET] =
		g_param_spec_string ("mesh-asset", NULL, "Mesh asset path or GUID", NULL, rw);

	/**
	 * LrgMeshRendererComponent:model:
	 *
	 * The #GrlModel drawn for the mesh asset: explicit, or resolved through
	 * #LrgMeshRendererComponent:asset-manager.
	 */
	properties[PROP_MODEL] =
		g_param_spec_object ("model", NULL, "Loaded model", GRL_TYPE_MODEL, rw);

	/**
	 * LrgMeshRendererComponent:asset-manager:
	 *
	 * The #LrgAssetManager mesh assets resolve through (optional).
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", NULL, "Asset manager", LRG_TYPE_ASSET_MANAGER, rw);

	/**
	 * LrgMeshRendererComponent:mesh-index:
	 *
	 * The model mesh to draw, -1 for the whole model.
	 */
	properties[PROP_MESH_INDEX] =
		g_param_spec_int ("mesh-index", NULL, "Model mesh index (-1 all)", -1, G_MAXINT, -1, rw);

	/**
	 * LrgMeshRendererComponent:size-x:
	 *
	 * Width of cube, plane, grid and rectangle primitives.
	 */
	properties[PROP_SIZE_X] = float_prop ("size-x", "Box width", 0.0f, G_MAXFLOAT, 1.0f);

	/**
	 * LrgMeshRendererComponent:size-y:
	 *
	 * Height of cube and rectangle primitives.
	 */
	properties[PROP_SIZE_Y] = float_prop ("size-y", "Box height", 0.0f, G_MAXFLOAT, 1.0f);

	/**
	 * LrgMeshRendererComponent:size-z:
	 *
	 * Depth of cube, plane and grid primitives.
	 */
	properties[PROP_SIZE_Z] = float_prop ("size-z", "Box depth", 0.0f, G_MAXFLOAT, 1.0f);

	/**
	 * LrgMeshRendererComponent:radius:
	 *
	 * Radius of spheres, circles, cylinders and cones.
	 */
	properties[PROP_RADIUS] = float_prop ("radius", "Radius", 0.0f, G_MAXFLOAT, 0.5f);

	/**
	 * LrgMeshRendererComponent:height:
	 *
	 * Height of cylinders and cones.
	 */
	properties[PROP_HEIGHT] = float_prop ("height", "Cylinder/cone height", 0.0f, G_MAXFLOAT, 1.0f);

	/**
	 * LrgMeshRendererComponent:major-radius:
	 *
	 * Ring radius of a torus (centre to tube centre).
	 */
	properties[PROP_MAJOR_RADIUS] = float_prop ("major-radius", "Torus ring radius", 0.0f,
	                                            G_MAXFLOAT, 0.375f);

	/**
	 * LrgMeshRendererComponent:minor-radius:
	 *
	 * Tube radius of a torus.
	 */
	properties[PROP_MINOR_RADIUS] = float_prop ("minor-radius", "Torus tube radius", 0.0f,
	                                            G_MAXFLOAT, 0.125f);

	/**
	 * LrgMeshRendererComponent:color-r:
	 *
	 * Base colour red, 0 .. 1.
	 */
	properties[PROP_COLOR_R] = float_prop ("color-r", "Base red", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMeshRendererComponent:color-g:
	 *
	 * Base colour green, 0 .. 1.
	 */
	properties[PROP_COLOR_G] = float_prop ("color-g", "Base green", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMeshRendererComponent:color-b:
	 *
	 * Base colour blue, 0 .. 1.
	 */
	properties[PROP_COLOR_B] = float_prop ("color-b", "Base blue", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMeshRendererComponent:color-a:
	 *
	 * Base colour alpha, 0 .. 1; below 1 the item is transparent.
	 */
	properties[PROP_COLOR_A] = float_prop ("color-a", "Base alpha", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMeshRendererComponent:roughness:
	 *
	 * PBR roughness, 0 (mirror) .. 1 (matte).
	 */
	properties[PROP_ROUGHNESS] = float_prop ("roughness", "Roughness", 0.0f, 1.0f, 0.5f);

	/**
	 * LrgMeshRendererComponent:metallic:
	 *
	 * PBR metalness, 0 .. 1.
	 */
	properties[PROP_METALLIC] = float_prop ("metallic", "Metallic", 0.0f, 1.0f, 0.0f);

	/**
	 * LrgMeshRendererComponent:emission-r:
	 *
	 * Emissive red, 0 .. 1.
	 */
	properties[PROP_EMISSION_R] = float_prop ("emission-r", "Emission red", 0.0f, 1.0f, 0.0f);

	/**
	 * LrgMeshRendererComponent:emission-g:
	 *
	 * Emissive green, 0 .. 1.
	 */
	properties[PROP_EMISSION_G] = float_prop ("emission-g", "Emission green", 0.0f, 1.0f, 0.0f);

	/**
	 * LrgMeshRendererComponent:emission-b:
	 *
	 * Emissive blue, 0 .. 1.
	 */
	properties[PROP_EMISSION_B] = float_prop ("emission-b", "Emission blue", 0.0f, 1.0f, 0.0f);

	/**
	 * LrgMeshRendererComponent:emission-strength:
	 *
	 * Emission multiplier, 0 for none.
	 */
	properties[PROP_EMISSION_STRENGTH] = float_prop ("emission-strength", "Emission strength",
	                                                 0.0f, G_MAXFLOAT, 0.0f);

	/**
	 * LrgMeshRendererComponent:material:
	 *
	 * The material slot: an explicit material object (an #LrgMaterial, or
	 * a legacy #LrgMaterial3D) replacing the scalar material properties.
	 */
	properties[PROP_MATERIAL] =
		g_param_spec_object ("material", NULL, "Material slot", G_TYPE_OBJECT, rw);

	/**
	 * LrgMeshRendererComponent:cast-shadows:
	 *
	 * Whether the items are shadow casters.
	 */
	properties[PROP_CAST_SHADOWS] = bool_prop ("cast-shadows", "Cast shadows", TRUE);

	/**
	 * LrgMeshRendererComponent:receive-shadows:
	 *
	 * Whether the items are lit with shadow lookups.
	 */
	properties[PROP_RECEIVE_SHADOWS] = bool_prop ("receive-shadows", "Receive shadows", TRUE);

	/**
	 * LrgMeshRendererComponent:transparent:
	 *
	 * Forces alpha blending (also implied by an alpha below 1).
	 */
	properties[PROP_TRANSPARENT] = bool_prop ("transparent", "Alpha blended", FALSE);

	/**
	 * LrgMeshRendererComponent:double-sided:
	 *
	 * Disables back-face culling.
	 */
	properties[PROP_DOUBLE_SIDED] = bool_prop ("double-sided", "No back-face culling", FALSE);

	/**
	 * LrgMeshRendererComponent:unlit:
	 *
	 * Draws without lighting.
	 */
	properties[PROP_UNLIT] = bool_prop ("unlit", "No lighting", FALSE);

	/**
	 * LrgMeshRendererComponent:layer:
	 *
	 * Render layer override 0 .. 31, or -1 to use the entity's layer.
	 */
	properties[PROP_LAYER] =
		g_param_spec_int ("layer", NULL, "Render layer (-1 entity)", -1, 31, -1, rw);

	/**
	 * LrgMeshRendererComponent:static-lighting:
	 *
	 * Whether the mesh takes part in the lighting bake (since 0.9.0): it
	 * casts and bounces light for the baker and receives a lightmap
	 * entry (keyed by its entity's GUID).  Only meaningful for meshes
	 * that never move.  Node parameter `static_lighting`.
	 */
	properties[PROP_STATIC_LIGHTING] = bool_prop ("static-lighting", "Contributes to and receives the lighting bake", FALSE);

	/**
	 * LrgMeshRendererComponent:lightmap-scale:
	 *
	 * Multiplier on the bake's texels per unit for this mesh (since
	 * 0.9.0): 2 gives it twice the lightmap resolution, 0.5 half; 0
	 * keeps it out of the lightmaps (it still contributes to the bake
	 * and is lit by the probe grids).  Node parameter `lightmap_scale`.
	 */
	properties[PROP_LIGHTMAP_SCALE] =
		g_param_spec_float ("lightmap-scale", NULL, "Lightmap resolution multiplier", 0.0f, 16.0f, 1.0f, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_mesh_renderer_component_init (LrgMeshRendererComponent *self)
{
	self->primitive = LRG_PRIMITIVE_CUBE;
	self->mesh_index = -1;
	self->size[0] = self->size[1] = self->size[2] = 1.0f;
	self->radius = 0.5f;
	self->height = 1.0f;
	self->major_radius = 0.375f;
	self->minor_radius = 0.125f;
	self->color[0] = self->color[1] = self->color[2] = self->color[3] = 1.0f;
	self->roughness = 0.5f;
	self->cast_shadows = TRUE;
	self->receive_shadows = TRUE;
	self->layer = -1;
	self->lightmap_scale = 1.0f;
	self->scalar_material = lrg_material3d_new ();
	sync_scalar_material (self);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgMeshRendererComponent *
lrg_mesh_renderer_component_new (void)
{
	return (LrgMeshRendererComponent *) g_object_new (LRG_TYPE_MESH_RENDERER_COMPONENT, NULL);
}

LrgMeshRendererComponent *
lrg_mesh_renderer_component_new_primitive (LrgPrimitiveType primitive)
{
	return (LrgMeshRendererComponent *) g_object_new (LRG_TYPE_MESH_RENDERER_COMPONENT,
	                                                  "primitive", primitive, NULL);
}

LrgMeshRendererComponent *
lrg_mesh_renderer_component_new_mesh (const gchar *mesh_asset)
{
	return (LrgMeshRendererComponent *) g_object_new (LRG_TYPE_MESH_RENDERER_COMPONENT,
	                                                  "mesh-asset", mesh_asset, NULL);
}

LrgPrimitiveType
lrg_mesh_renderer_component_get_primitive (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), LRG_PRIMITIVE_CUBE);

	return self->primitive;
}

void
lrg_mesh_renderer_component_set_primitive (LrgMeshRendererComponent *self,
                                           LrgPrimitiveType          primitive)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	if (self->primitive == primitive)
		return;
	self->primitive = primitive;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMITIVE]);
}

const gchar *
lrg_mesh_renderer_component_get_mesh_asset (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), NULL);

	return self->mesh_asset;
}

void
lrg_mesh_renderer_component_set_mesh_asset (LrgMeshRendererComponent *self,
                                            const gchar              *mesh_asset)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	if (mesh_asset != NULL && *mesh_asset == '\0')
		mesh_asset = NULL;
	if (g_strcmp0 (self->mesh_asset, mesh_asset) == 0)
		return;
	g_free (self->mesh_asset);
	self->mesh_asset = g_strdup (mesh_asset);
	drop_resolved_model (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESH_ASSET]);
}

GrlModel *
lrg_mesh_renderer_component_get_model (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), NULL);

	return self->model;
}

void
lrg_mesh_renderer_component_set_model (LrgMeshRendererComponent *self,
                                       GrlModel                 *model)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));
	g_return_if_fail (model == NULL || GRL_IS_MODEL (model));

	if (self->model == model && self->model_explicit == (model != NULL))
		return;
	g_set_object (&self->model, model);
	self->model_explicit = (model != NULL);
	self->load_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

LrgAssetManager *
lrg_mesh_renderer_component_get_asset_manager (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), NULL);

	return self->asset_manager;
}

void
lrg_mesh_renderer_component_set_asset_manager (LrgMeshRendererComponent *self,
                                               LrgAssetManager          *manager)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (!g_set_object (&self->asset_manager, manager))
		return;
	drop_resolved_model (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

void
lrg_mesh_renderer_component_get_size (LrgMeshRendererComponent *self,
                                      gfloat                   *out)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));
	g_return_if_fail (out != NULL);

	memcpy (out, self->size, sizeof self->size);
}

void
lrg_mesh_renderer_component_set_size (LrgMeshRendererComponent *self,
                                      gfloat                    x,
                                      gfloat                    y,
                                      gfloat                    z)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->size[0], MAX (x, 0.0f), PROP_SIZE_X);
	set_float (self, &self->size[1], MAX (y, 0.0f), PROP_SIZE_Y);
	set_float (self, &self->size[2], MAX (z, 0.0f), PROP_SIZE_Z);
	g_object_thaw_notify (G_OBJECT (self));
}

gboolean
lrg_mesh_renderer_component_get_local_bounds (LrgMeshRendererComponent *self,
                                              LrgBoundingBox3D         *out)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	/* A model reports its own bounds; an unresolved asset has none */
	if (self->mesh_asset != NULL || self->model_explicit)
	{
		GrlBoundingBox *box;

		if (self->model == NULL)
			return FALSE;
		box = grl_model_get_bounding_box (self->model);
		if (box == NULL)
			return FALSE;
		out->min = box->min;
		out->max = box->max;
		grl_bounding_box_free (box);
		return TRUE;
	}

	/* Primitives are centred on the entity, matching the editor viewport */
	switch (self->primitive)
	{
	case LRG_PRIMITIVE_CUBE:
		set_box (out, self->size[0] * 0.5f, self->size[1] * 0.5f, self->size[2] * 0.5f);
		break;
	case LRG_PRIMITIVE_UV_SPHERE:
	case LRG_PRIMITIVE_ICO_SPHERE:
		set_box (out, self->radius, self->radius, self->radius);
		break;
	case LRG_PRIMITIVE_CIRCLE:
		set_box (out, self->radius, 0.0f, self->radius);
		break;
	case LRG_PRIMITIVE_CIRCLE_2D:
		set_box (out, self->radius, self->radius, 0.0f);
		break;
	case LRG_PRIMITIVE_CYLINDER:
	case LRG_PRIMITIVE_CONE:
		set_box (out, self->radius, self->height * 0.5f, self->radius);
		break;
	case LRG_PRIMITIVE_TORUS:
		set_box (out, self->major_radius + self->minor_radius, self->minor_radius,
		         self->major_radius + self->minor_radius);
		break;
	case LRG_PRIMITIVE_PLANE:
	case LRG_PRIMITIVE_GRID:
		set_box (out, self->size[0] * 0.5f, 0.0f, self->size[2] * 0.5f);
		break;
	case LRG_PRIMITIVE_RECTANGLE_2D:
		set_box (out, self->size[0] * 0.5f, self->size[1] * 0.5f, 0.0f);
		break;
	case LRG_PRIMITIVE_MESH:
	default:
		set_box (out, 0.5f, 0.5f, 0.5f);
		break;
	}
	return TRUE;
}

void
lrg_mesh_renderer_component_get_color (LrgMeshRendererComponent *self,
                                       gfloat                   *out)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));
	g_return_if_fail (out != NULL);

	memcpy (out, self->color, sizeof self->color);
}

void
lrg_mesh_renderer_component_set_color (LrgMeshRendererComponent *self,
                                       gfloat                    r,
                                       gfloat                    g,
                                       gfloat                    b,
                                       gfloat                    a)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->color[0], CLAMP (r, 0.0f, 1.0f), PROP_COLOR_R);
	set_float (self, &self->color[1], CLAMP (g, 0.0f, 1.0f), PROP_COLOR_G);
	set_float (self, &self->color[2], CLAMP (b, 0.0f, 1.0f), PROP_COLOR_B);
	set_float (self, &self->color[3], CLAMP (a, 0.0f, 1.0f), PROP_COLOR_A);
	sync_scalar_material (self);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_mesh_renderer_component_set_emission (LrgMeshRendererComponent *self,
                                          gfloat                    r,
                                          gfloat                    g,
                                          gfloat                    b,
                                          gfloat                    strength)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->emission[0], CLAMP (r, 0.0f, 1.0f), PROP_EMISSION_R);
	set_float (self, &self->emission[1], CLAMP (g, 0.0f, 1.0f), PROP_EMISSION_G);
	set_float (self, &self->emission[2], CLAMP (b, 0.0f, 1.0f), PROP_EMISSION_B);
	set_float (self, &self->emission_strength, MAX (strength, 0.0f), PROP_EMISSION_STRENGTH);
	sync_scalar_material (self);
	g_object_thaw_notify (G_OBJECT (self));
}

GObject *
lrg_mesh_renderer_component_get_material (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), NULL);

	return self->material;
}

void
lrg_mesh_renderer_component_set_material (LrgMeshRendererComponent *self,
                                          GObject                  *material)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));
	g_return_if_fail (material == NULL || G_IS_OBJECT (material));

	if (g_set_object (&self->material, material))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MATERIAL]);
}

GObject *
lrg_mesh_renderer_component_get_effective_material (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), NULL);

	return (self->material != NULL) ? self->material : G_OBJECT (self->scalar_material);
}

gboolean
lrg_mesh_renderer_component_get_cast_shadows (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), FALSE);

	return self->cast_shadows;
}

void
lrg_mesh_renderer_component_set_cast_shadows (LrgMeshRendererComponent *self,
                                              gboolean                  cast_shadows)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	set_bool (self, &self->cast_shadows, cast_shadows, PROP_CAST_SHADOWS);
}

gboolean
lrg_mesh_renderer_component_get_receive_shadows (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), FALSE);

	return self->receive_shadows;
}

void
lrg_mesh_renderer_component_set_receive_shadows (LrgMeshRendererComponent *self,
                                                 gboolean                  receive_shadows)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	set_bool (self, &self->receive_shadows, receive_shadows, PROP_RECEIVE_SHADOWS);
}

gint
lrg_mesh_renderer_component_get_layer (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), -1);

	return self->layer;
}

void
lrg_mesh_renderer_component_set_layer (LrgMeshRendererComponent *self,
                                       gint                      layer)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	layer = CLAMP (layer, -1, 31);
	if (self->layer == layer)
		return;
	self->layer = layer;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

LrgRenderItemFlags
lrg_mesh_renderer_component_get_flags (LrgMeshRendererComponent *self)
{
	guint flags = LRG_RENDER_ITEM_FLAG_NONE;

	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), LRG_RENDER_ITEM_FLAG_NONE);

	if (self->cast_shadows)
		flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
	if (self->receive_shadows)
		flags |= LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
	if (self->transparent || self->color[3] < 1.0f)
		flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
	if (self->double_sided)
		flags |= LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED;
	if (self->unlit)
		flags |= LRG_RENDER_ITEM_FLAG_UNLIT;
	if (self->material != NULL && LRG_IS_MATERIAL (self->material))
	{
		/* the material decides transparency, not the scalar alpha */
		flags &= ~(guint) LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		if (self->transparent)
			flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
		flags |= material_flags (LRG_MATERIAL (self->material));
	}
	return (LrgRenderItemFlags) flags;
}

void
lrg_mesh_renderer_component_set_submesh_material (LrgMeshRendererComponent *self,
                                                  guint                     index,
                                                  LrgMaterial              *material)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));
	g_return_if_fail (material == NULL || LRG_IS_MATERIAL (material));
	g_return_if_fail (index < 4096);

	if (self->submesh == NULL)
	{
		if (material == NULL)
			return;
		self->submesh = g_ptr_array_new_with_free_func (unref_nullable);
	}
	while (self->submesh->len <= index)
		g_ptr_array_add (self->submesh, NULL);
	unref_nullable (g_ptr_array_index (self->submesh, index));
	g_ptr_array_index (self->submesh, index) = material != NULL ? g_object_ref (material) : NULL;
	/* trailing empty entries do not count */
	while (self->submesh->len > 0 && g_ptr_array_index (self->submesh, self->submesh->len - 1) == NULL)
		g_ptr_array_set_size (self->submesh, self->submesh->len - 1);
}

LrgMaterial *
lrg_mesh_renderer_component_get_submesh_material (LrgMeshRendererComponent *self,
                                                  guint                     index)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), NULL);

	if (self->submesh == NULL || index >= self->submesh->len)
		return NULL;
	return g_ptr_array_index (self->submesh, index);
}

guint
lrg_mesh_renderer_component_get_n_submesh_materials (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), 0);
	return self->submesh != NULL ? self->submesh->len : 0;
}

void
lrg_mesh_renderer_component_clear_submesh_materials (LrgMeshRendererComponent *self)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));
	g_clear_pointer (&self->submesh, g_ptr_array_unref);
}

gboolean
lrg_mesh_renderer_component_get_static_lighting (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), FALSE);

	return self->static_lighting;
}

void
lrg_mesh_renderer_component_set_static_lighting (LrgMeshRendererComponent *self,
                                                 gboolean                  static_lighting)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	static_lighting = static_lighting != FALSE;
	if (self->static_lighting == static_lighting)
		return;
	self->static_lighting = static_lighting;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATIC_LIGHTING]);
}

gfloat
lrg_mesh_renderer_component_get_lightmap_scale (LrgMeshRendererComponent *self)
{
	g_return_val_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self), 1.0f);

	return self->lightmap_scale;
}

void
lrg_mesh_renderer_component_set_lightmap_scale (LrgMeshRendererComponent *self,
                                                gfloat                    scale)
{
	g_return_if_fail (LRG_IS_MESH_RENDERER_COMPONENT (self));

	scale = isfinite (scale) ? CLAMP (scale, 0.0f, 16.0f) : 1.0f;
	if (self->lightmap_scale == scale)
		return;
	self->lightmap_scale = scale;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIGHTMAP_SCALE]);
}
