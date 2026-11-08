/* lrg-tilemap-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTilemapComponent: submits one custom render item per visible tile
 * layer; the item's draw callback emits the layer's textured quads with
 * rlgl, the same geometry the editor viewport draws for TILEMAP nodes.
 */

#include "config.h"

#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-tilemap-component.h"
#include "../lrg-entity3d.h"
#include "../../level/lrg-node-tilemap.h"
#include "../../tilemap/lrg-tilemap.h"
#include "../../tilemap/lrg-tilemap-layer.h"
#include "../../tilemap/lrg-tileset.h"
#include "../../core/lrg-asset-manager.h"

struct _LrgTilemapComponent
{
	LrgComponent     parent_instance;

	LrgNodeTilemap  *node_tilemap;    /* cell source, preferred */
	LrgTilemap      *tilemap;         /* explicit runtime tilemap */
	LrgTilemap      *built;           /* runtime tilemap built from node_tilemap */
	LrgTileset      *tileset;         /* explicit or loaded */
	gboolean         tileset_loaded;  /* tileset came from tileset_path */
	gboolean         load_failed;
	gchar           *tileset_path;
	LrgAssetManager *asset_manager;
	gfloat           tile_size;
	gboolean         cast_shadows;
	gint             layer;
};

G_DEFINE_FINAL_TYPE (LrgTilemapComponent, lrg_tilemap_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_NODE_TILEMAP,
	PROP_TILEMAP,
	PROP_TILESET,
	PROP_TILESET_PATH,
	PROP_ASSET_MANAGER,
	PROP_TILE_SIZE,
	PROP_CAST_SHADOWS,
	PROP_LAYER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* What a layer item's draw callback needs: the component (referenced, so
 * a list replayed after the component left still draws safely) and the
 * tile layer index. */
typedef struct
{
	LrgTilemapComponent *self;
	guint                layer;
} LayerDraw;

static void
layer_draw_free (gpointer data)
{
	LayerDraw *ld = (LayerDraw *) data;

	g_object_unref (ld->self);
	g_free (ld);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Colour of an untextured tile id: the editor's tile palette swatches. */
static void
tile_swatch (guint   id,
             guint8 *out)
{
	static const guint32 palette[] = {
		0xf38ba8ffu, 0xfab387ffu, 0xf9e2afffu, 0xa6e3a1ffu, 0x94e2d5ffu, 0x89dcebffu,
		0x74c7ecffu, 0x89b4faffu, 0xb4befeffu, 0xcba6f7ffu, 0xf5c2e7ffu, 0xeba0acffu
	};
	guint32 c = palette[(id - 1) % G_N_ELEMENTS (palette)];

	out[0] = (guint8) (c >> 24);
	out[1] = (guint8) (c >> 16);
	out[2] = (guint8) (c >> 8);
	out[3] = (guint8) c;
}

/* Drops the runtime tilemap built from the node tilemap. */
static void
invalidate_built (LrgTilemapComponent *self)
{
	g_clear_object (&self->built);
}

/* Loads the tileset image once: through the asset manager when set,
 * otherwise from the file when a window (GL context) exists.  It is
 * sliced by the node tilemap's tile pixel size. */
static void
resolve_tileset (LrgTilemapComponent *self)
{
	g_autoptr(GError) error = NULL;
	guint             tw, th;

	if (self->tileset != NULL || self->tileset_path == NULL || self->load_failed)
		return;
	if (self->node_tilemap == NULL)
		return;
	tw = lrg_node_tilemap_get_tile_width (self->node_tilemap);
	th = lrg_node_tilemap_get_tile_height (self->node_tilemap);
	if (tw == 0 || th == 0)
		return;

	if (g_str_has_suffix (self->tileset_path, ".rtileset"))
	{
		/*
		 * 0.9.0 tileset asset: the description (tile size, collision, rule
		 * tiles, animated tiles) comes from the file; the image is loaded
		 * through the asset manager (relative to the .rtileset) or from
		 * disk when a window exists.
		 */
		g_autoptr(LrgTileset) ts = lrg_tileset_new_from_asset_file (self->tileset_path, FALSE, &error);

		if (ts != NULL && lrg_tileset_get_image_path (ts) != NULL)
		{
			if (self->asset_manager != NULL)
			{
				g_autofree gchar *dir = g_path_get_dirname (self->tileset_path);
				g_autofree gchar *image = g_build_filename (dir, lrg_tileset_get_image_path (ts), NULL);
				GrlTexture *tex = lrg_asset_manager_load_texture (self->asset_manager, image, &error);

				if (tex != NULL)
					lrg_tileset_set_texture (ts, tex);
			}
			else if (IsWindowReady ())
				lrg_tileset_load_texture (ts, &error);
		}
		if (ts != NULL && lrg_tileset_get_texture (ts) != NULL)
			self->tileset = g_steal_pointer (&ts);
	}
	else if (self->asset_manager != NULL)
	{
		GrlTexture *tex = lrg_asset_manager_load_texture (self->asset_manager, self->tileset_path, &error);

		if (tex != NULL && grl_texture_get_width (tex) >= (gint) tw &&
		    grl_texture_get_height (tex) >= (gint) th)
			self->tileset = lrg_tileset_new (tex, tw, th);
	}
	else if (IsWindowReady ())
		self->tileset = lrg_tileset_new_from_file (self->tileset_path, tw, th, &error);

	if (self->tileset == NULL)
	{
		g_debug ("LrgTilemapComponent: cannot load tileset '%s': %s", self->tileset_path,
		         error != NULL ? error->message : "no graphics context or tile too large");
		self->load_failed = TRUE;
		return;
	}
	self->tileset_loaded = TRUE;
	invalidate_built (self);
}

/* The runtime layer @index of the explicit runtime tilemap. */
static LrgTilemapLayer *
runtime_layer (LrgTilemapComponent *self,
               guint                index)
{
	if (self->tilemap == NULL || index >= lrg_tilemap_get_layer_count (self->tilemap))
		return NULL;
	return lrg_tilemap_get_layer (self->tilemap, index);
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

/*
 * Emits one layer's quads in the entity's local space: cell (x, y) spans
 * x .. x+1 and -y .. -(y+1) tiles; the item's world matrix already holds
 * the layer's small +Z offset.  Textured from the tileset (tile id N uses
 * tile N - 1, wrapped like the editor), or flat swatches without one.
 */
static void
draw_layer (const LrgRenderItem *item,
            LrgRenderCategory    category,
            gpointer             pass_data)
{
	LayerDraw           *ld = (LayerDraw *) item->user_data;
	LrgTilemapComponent *self = ld->self;
	const guint         *cells;
	gboolean             visible = TRUE;
	GrlTexture          *grl_tex = NULL;
	Texture2D           *tex = NULL;
	guint                w = 0, h = 0, tw = 0, th = 0;
	guint                x, y;

	(void) category;
	(void) pass_data;

	cells = lrg_tilemap_component_get_layer_cells (self, ld->layer, &visible);
	lrg_tilemap_component_get_grid_size (self, &w, &h);
	if (cells == NULL || !visible || w == 0 || h == 0)
		return;

	/* The tileset is usable when it holds at least one whole tile */
	if (self->tileset != NULL)
	{
		grl_tex = lrg_tileset_get_texture (self->tileset);
		tw = lrg_tileset_get_tile_width (self->tileset);
		th = lrg_tileset_get_tile_height (self->tileset);
	}
	if (grl_tex != NULL)
		tex = (Texture2D *) grl_texture_get_handle (grl_tex);
	if (tex == NULL || tw == 0 || th == 0
	    || tex->width < (gint) tw || tex->height < (gint) th)
		tex = NULL;

	rlPushMatrix ();
	rlMultMatrixf (item->world_matrix);
	rlSetTexture (tex != NULL ? tex->id : rlGetTextureIdDefault ());
	for (y = 0; y < h; y++)
	{
		rlCheckRenderBatchLimit ((gint) (4 * w));
		rlBegin (RL_QUADS);
		for (x = 0; x < w; x++)
		{
			gfloat xy[8];
			gfloat uv[8];
			guint8 c[4] = { 255, 255, 255, 255 };
			guint  id;
			guint  k;

			if (!lrg_tilemap_component_get_cell_quad (self, ld->layer, x, y, xy, uv))
				continue;
			id = lrg_tile_get_id (cells[(gsize) y * w + x]);
			if (tex == NULL)
				tile_swatch (id, c);
			c[0] = (guint8) ((guint) c[0] * (guint) (item->color[0] * 255.0f) / 255u);
			c[1] = (guint8) ((guint) c[1] * (guint) (item->color[1] * 255.0f) / 255u);
			c[2] = (guint8) ((guint) c[2] * (guint) (item->color[2] * 255.0f) / 255u);
			c[3] = (guint8) ((guint) c[3] * (guint) (item->color[3] * 255.0f) / 255u);
			rlColor4ub (c[0], c[1], c[2], c[3]);
			rlNormal3f (0.0f, 0.0f, 1.0f);
			for (k = 0; k < 4; k++)
			{
				rlTexCoord2f (uv[2 * k], uv[2 * k + 1]);
				rlVertex3f (xy[2 * k], xy[2 * k + 1], 0.0f);
			}
		}
		rlEnd ();
	}
	rlSetTexture (0);
	rlPopMatrix ();
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static gboolean
lrg_tilemap_component_get_bounds (LrgComponent     *component,
                                  LrgBoundingBox3D *out_local)
{
	return lrg_tilemap_component_get_local_bounds (LRG_TILEMAP_COMPONENT (component), out_local);
}

static void
lrg_tilemap_component_collect_render (LrgComponent  *component,
                                      LrgRenderList *list)
{
	LrgTilemapComponent *self = LRG_TILEMAP_COMPONENT (component);
	LrgEntity3D         *entity = lrg_component_get_entity (component);
	LrgBoundingBox3D     local;
	guint                n, i;

	if (entity == NULL || !lrg_tilemap_component_get_local_bounds (self, &local))
		return;
	resolve_tileset (self);

	n = lrg_tilemap_component_get_n_layers (self);
	for (i = 0; i < n; i++)
	{
		LrgRenderItem  item;
		LayerDraw     *ld;
		gboolean       visible = TRUE;
		gfloat         z = LRG_TILEMAP_COMPONENT_LAYER_GAP * (gfloat) i;
		guint          flags;

		if (lrg_tilemap_component_get_layer_cells (self, i, &visible) == NULL || !visible)
			continue;

		lrg_render_item_init_for_entity (&item, entity, component);

		/* Layer i sits z along the local +Z axis: world * T(0, 0, z) */
		item.world_matrix[12] += item.world_matrix[8] * z;
		item.world_matrix[13] += item.world_matrix[9] * z;
		item.world_matrix[14] += item.world_matrix[10] * z;

		item.kind = LRG_RENDER_ITEM_CUSTOM;
		item.texture = (self->tileset != NULL) ? lrg_tileset_get_texture (self->tileset) : NULL;
		flags = LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED | LRG_RENDER_ITEM_FLAG_CUTOUT |
		        LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
		if (self->cast_shadows)
			flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
		item.flags = (LrgRenderItemFlags) flags;
		if (self->layer >= 0)
			item.layer = (guint) self->layer;
		item.sort_key = ((guint64) LRG_RENDER_ITEM_CUSTOM << 56) | (guint64) i;
		lrg_render_item_set_local_bounds (&item, &local);
		if (!lrg_render_list_accepts (list, &item))
			continue;

		/* The list owns the callback data from here on */
		ld = g_new0 (LayerDraw, 1);
		ld->self = (LrgTilemapComponent *) g_object_ref (self);
		ld->layer = i;
		item.draw_func = draw_layer;
		item.user_data = ld;
		item.user_data_destroy = layer_draw_free;
		lrg_render_list_add (list, &item);
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_tilemap_component_dispose (GObject *object)
{
	LrgTilemapComponent *self = LRG_TILEMAP_COMPONENT (object);

	g_clear_object (&self->node_tilemap);
	g_clear_object (&self->tilemap);
	g_clear_object (&self->built);
	g_clear_object (&self->tileset);
	g_clear_object (&self->asset_manager);

	G_OBJECT_CLASS (lrg_tilemap_component_parent_class)->dispose (object);
}

static void
lrg_tilemap_component_finalize (GObject *object)
{
	LrgTilemapComponent *self = LRG_TILEMAP_COMPONENT (object);

	g_free (self->tileset_path);

	G_OBJECT_CLASS (lrg_tilemap_component_parent_class)->finalize (object);
}

static void
lrg_tilemap_component_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgTilemapComponent *self = LRG_TILEMAP_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_NODE_TILEMAP:  g_value_set_object (value, self->node_tilemap); break;
	case PROP_TILEMAP:       g_value_set_object (value, self->tilemap); break;
	case PROP_TILESET:       g_value_set_object (value, self->tileset); break;
	case PROP_TILESET_PATH:  g_value_set_string (value, self->tileset_path); break;
	case PROP_ASSET_MANAGER: g_value_set_object (value, self->asset_manager); break;
	case PROP_TILE_SIZE:     g_value_set_float (value, self->tile_size); break;
	case PROP_CAST_SHADOWS:  g_value_set_boolean (value, self->cast_shadows); break;
	case PROP_LAYER:         g_value_set_int (value, self->layer); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tilemap_component_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgTilemapComponent *self = LRG_TILEMAP_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_NODE_TILEMAP:
		lrg_tilemap_component_set_node_tilemap (self, g_value_get_object (value));
		break;
	case PROP_TILEMAP:
		lrg_tilemap_component_set_tilemap (self, g_value_get_object (value));
		break;
	case PROP_TILESET:
		lrg_tilemap_component_set_tileset (self, g_value_get_object (value));
		break;
	case PROP_TILESET_PATH:
		lrg_tilemap_component_set_tileset_path (self, g_value_get_string (value));
		break;
	case PROP_ASSET_MANAGER:
		lrg_tilemap_component_set_asset_manager (self, g_value_get_object (value));
		break;
	case PROP_TILE_SIZE:
		lrg_tilemap_component_set_tile_size (self, g_value_get_float (value));
		break;
	case PROP_CAST_SHADOWS:
		if (self->cast_shadows != g_value_get_boolean (value))
		{
			self->cast_shadows = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_int (value))
		{
			self->layer = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tilemap_component_class_init (LrgTilemapComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_tilemap_component_dispose;
	object_class->finalize = lrg_tilemap_component_finalize;
	object_class->get_property = lrg_tilemap_component_get_property;
	object_class->set_property = lrg_tilemap_component_set_property;

	component_class->collect_render = lrg_tilemap_component_collect_render;
	component_class->get_bounds = lrg_tilemap_component_get_bounds;

	/**
	 * LrgTilemapComponent:node-tilemap:
	 *
	 * Level tile data (the editor TILEMAP visual's grid); the preferred
	 * cell source.
	 */
	properties[PROP_NODE_TILEMAP] =
		g_param_spec_object ("node-tilemap", NULL, "Level tile data", LRG_TYPE_NODE_TILEMAP, rw);

	/**
	 * LrgTilemapComponent:tilemap:
	 *
	 * An explicit runtime #LrgTilemap (used when no node tilemap is set).
	 */
	properties[PROP_TILEMAP] =
		g_param_spec_object ("tilemap", NULL, "Runtime tilemap", LRG_TYPE_TILEMAP, rw);

	/**
	 * LrgTilemapComponent:tileset:
	 *
	 * The tile images (explicit or loaded from the tileset path).
	 */
	properties[PROP_TILESET] =
		g_param_spec_object ("tileset", NULL, "Tileset", LRG_TYPE_TILESET, rw);

	/**
	 * LrgTilemapComponent:tileset-path:
	 *
	 * The tileset image path or GUID (the TILEMAP visual's asset).
	 */
	properties[PROP_TILESET_PATH] =
		g_param_spec_string ("tileset-path", NULL, "Tileset image", NULL, rw);

	/**
	 * LrgTilemapComponent:asset-manager:
	 *
	 * The #LrgAssetManager the tileset image loads through (optional).
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", NULL, "Asset manager", LRG_TYPE_ASSET_MANAGER, rw);

	/**
	 * LrgTilemapComponent:tile-size:
	 *
	 * World units per tile.
	 */
	properties[PROP_TILE_SIZE] =
		g_param_spec_float ("tile-size", NULL, "World units per tile", 0.0001f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgTilemapComponent:cast-shadows:
	 *
	 * Whether the tile layers are shadow casters.
	 */
	properties[PROP_CAST_SHADOWS] =
		g_param_spec_boolean ("cast-shadows", NULL, "Cast shadows", FALSE, rw);

	/**
	 * LrgTilemapComponent:layer:
	 *
	 * Render layer override 0 .. 31, or -1 to use the entity's layer.
	 */
	properties[PROP_LAYER] =
		g_param_spec_int ("layer", NULL, "Render layer (-1 entity)", -1, 31, -1, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_tilemap_component_init (LrgTilemapComponent *self)
{
	self->tile_size = 1.0f;
	self->layer = -1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTilemapComponent *
lrg_tilemap_component_new (void)
{
	return (LrgTilemapComponent *) g_object_new (LRG_TYPE_TILEMAP_COMPONENT, NULL);
}

LrgTilemapComponent *
lrg_tilemap_component_new_from_node_tilemap (LrgNodeTilemap *node_tilemap,
                                             gfloat          tile_size)
{
	g_autoptr(LrgNodeTilemap) copy = NULL;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (node_tilemap), NULL);

	copy = lrg_node_tilemap_copy (node_tilemap);
	return (LrgTilemapComponent *) g_object_new (LRG_TYPE_TILEMAP_COMPONENT,
	                                             "node-tilemap", copy,
	                                             "tile-size", (tile_size > 0.0f) ? tile_size : 1.0f,
	                                             NULL);
}

LrgNodeTilemap *
lrg_tilemap_component_get_node_tilemap (LrgTilemapComponent *self)
{
	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), NULL);

	return self->node_tilemap;
}

void
lrg_tilemap_component_set_node_tilemap (LrgTilemapComponent *self,
                                        LrgNodeTilemap      *node_tilemap)
{
	g_return_if_fail (LRG_IS_TILEMAP_COMPONENT (self));
	g_return_if_fail (node_tilemap == NULL || LRG_IS_NODE_TILEMAP (node_tilemap));

	if (!g_set_object (&self->node_tilemap, node_tilemap))
		return;
	invalidate_built (self);
	self->load_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NODE_TILEMAP]);
}

LrgTilemap *
lrg_tilemap_component_get_tilemap (LrgTilemapComponent *self)
{
	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), NULL);

	if (self->node_tilemap == NULL)
		return self->tilemap;

	/* Built lazily from the level data once there is a tileset */
	if (self->built == NULL && self->tileset != NULL)
		self->built = lrg_node_tilemap_to_runtime (self->node_tilemap, self->tileset);
	return self->built;
}

void
lrg_tilemap_component_set_tilemap (LrgTilemapComponent *self,
                                   LrgTilemap          *tilemap)
{
	g_return_if_fail (LRG_IS_TILEMAP_COMPONENT (self));
	g_return_if_fail (tilemap == NULL || LRG_IS_TILEMAP (tilemap));

	if (!g_set_object (&self->tilemap, tilemap))
		return;
	if (tilemap != NULL && self->tileset == NULL && lrg_tilemap_get_tileset (tilemap) != NULL)
		lrg_tilemap_component_set_tileset (self, lrg_tilemap_get_tileset (tilemap));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILEMAP]);
}

LrgTileset *
lrg_tilemap_component_get_tileset (LrgTilemapComponent *self)
{
	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), NULL);

	return self->tileset;
}

void
lrg_tilemap_component_set_tileset (LrgTilemapComponent *self,
                                   LrgTileset          *tileset)
{
	g_return_if_fail (LRG_IS_TILEMAP_COMPONENT (self));
	g_return_if_fail (tileset == NULL || LRG_IS_TILESET (tileset));

	if (!g_set_object (&self->tileset, tileset))
		return;
	self->tileset_loaded = FALSE;
	self->load_failed = FALSE;
	invalidate_built (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILESET]);
}

const gchar *
lrg_tilemap_component_get_tileset_path (LrgTilemapComponent *self)
{
	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), NULL);

	return self->tileset_path;
}

void
lrg_tilemap_component_set_tileset_path (LrgTilemapComponent *self,
                                        const gchar         *path)
{
	g_return_if_fail (LRG_IS_TILEMAP_COMPONENT (self));

	if (path != NULL && *path == '\0')
		path = NULL;
	if (g_strcmp0 (self->tileset_path, path) == 0)
		return;
	g_free (self->tileset_path);
	self->tileset_path = g_strdup (path);

	/* A tileset loaded from the old path goes; an explicit one stays */
	if (self->tileset_loaded)
	{
		g_clear_object (&self->tileset);
		self->tileset_loaded = FALSE;
		invalidate_built (self);
	}
	self->load_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILESET_PATH]);
}

void
lrg_tilemap_component_set_asset_manager (LrgTilemapComponent *self,
                                         LrgAssetManager     *manager)
{
	g_return_if_fail (LRG_IS_TILEMAP_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (!g_set_object (&self->asset_manager, manager))
		return;
	self->load_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

gfloat
lrg_tilemap_component_get_tile_size (LrgTilemapComponent *self)
{
	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), 1.0f);

	return self->tile_size;
}

void
lrg_tilemap_component_set_tile_size (LrgTilemapComponent *self,
                                     gfloat               tile_size)
{
	g_return_if_fail (LRG_IS_TILEMAP_COMPONENT (self));

	if (!(tile_size > 0.0f))
		tile_size = 1.0f;
	if (self->tile_size == tile_size)
		return;
	self->tile_size = tile_size;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILE_SIZE]);
}

void
lrg_tilemap_component_get_grid_size (LrgTilemapComponent *self,
                                     guint               *out_width,
                                     guint               *out_height)
{
	guint w = 0, h = 0;

	g_return_if_fail (LRG_IS_TILEMAP_COMPONENT (self));

	if (self->node_tilemap != NULL)
	{
		w = lrg_node_tilemap_get_width (self->node_tilemap);
		h = lrg_node_tilemap_get_height (self->node_tilemap);
	}
	else if (self->tilemap != NULL)
	{
		w = lrg_tilemap_get_width (self->tilemap);
		h = lrg_tilemap_get_height (self->tilemap);
	}
	if (out_width != NULL)
		*out_width = w;
	if (out_height != NULL)
		*out_height = h;
}

guint
lrg_tilemap_component_get_n_layers (LrgTilemapComponent *self)
{
	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), 0);

	if (self->node_tilemap != NULL)
		return lrg_node_tilemap_get_n_layers (self->node_tilemap);
	if (self->tilemap != NULL)
		return lrg_tilemap_get_layer_count (self->tilemap);
	return 0;
}

const guint *
lrg_tilemap_component_get_layer_cells (LrgTilemapComponent *self,
                                       guint                layer,
                                       gboolean            *out_visible)
{
	const guint *cells = NULL;
	gboolean     visible = FALSE;
	gsize        n = 0;

	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), NULL);

	if (self->node_tilemap != NULL)
	{
		if (layer < lrg_node_tilemap_get_n_layers (self->node_tilemap))
		{
			cells = lrg_node_tilemap_get_layer_tiles (self->node_tilemap, layer, &n);
			visible = lrg_node_tilemap_get_layer_visible (self->node_tilemap, layer);
		}
	}
	else
	{
		LrgTilemapLayer *rl = runtime_layer (self, layer);

		if (rl != NULL)
		{
			cells = lrg_tilemap_layer_get_tiles (rl, &n);
			visible = lrg_tilemap_layer_get_visible (rl);
		}
	}
	if (out_visible != NULL)
		*out_visible = visible;
	return cells;
}

/* cell_quad_uv:
 * Unit-square or atlas coordinates of the four image corners (TL, TR,
 * BR, BL) after a horizontal mirror. */
static void
cell_quad_uv (guint    raw,
              gfloat   u0,
              gfloat   v0,
              gfloat   u1,
              gfloat   v1,
              gfloat   uu[4],
              gfloat   vv[4])
{
	if (lrg_tile_get_mirrored (raw))
	{
		gfloat t = u0;

		u0 = u1;
		u1 = t;
	}
	uu[0] = u0; vv[0] = v0;
	uu[1] = u1; vv[1] = v0;
	uu[2] = u1; vv[2] = v1;
	uu[3] = u0; vv[3] = v1;
}

gboolean
lrg_tilemap_component_get_cell_quad (LrgTilemapComponent *self,
                                     guint                layer,
                                     guint                x,
                                     guint                y,
                                     gfloat              *out_xy,
                                     gfloat              *out_uv)
{
	const guint *cells;
	guint        gw = 0, gh = 0;
	guint        raw, id, rot, i, k;
	LrgTilemap  *map;
	GrlRectangle box;
	gfloat       s, hw, hh, cx, cy;
	gfloat       u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
	gfloat       px[4], py[4], uu[4], vv[4];
	guint        tw = 1, th = 1;
	gint         off_x = 0, off_y = 0;
	gboolean     textured = FALSE;
	static const guint order[4] = { 0, 3, 2, 1 };

	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), FALSE);

	cells = lrg_tilemap_component_get_layer_cells (self, layer, NULL);
	lrg_tilemap_component_get_grid_size (self, &gw, &gh);
	if (cells == NULL || x >= gw || y >= gh)
		return FALSE;
	raw = cells[(gsize) y * gw + x];
	id = lrg_tile_get_id (raw);
	if (id == 0)
		return FALSE;

	map = lrg_tilemap_component_get_tilemap (self);
	if (map != NULL)
	{
		guint cw = MAX (lrg_tilemap_get_cell_width (map), 1u);

		s = self->tile_size / (gfloat) cw;
		lrg_tilemap_get_cell_bounds (map, (gint) x, (gint) y, &box);
		tw = lrg_tilemap_get_cell_width (map);
		th = lrg_tilemap_get_cell_height (map);
	}
	else
	{
		/* No runtime map yet: one orthogonal cell per world tile. */
		s = self->tile_size;
		box.x = (gfloat) x;
		box.y = (gfloat) y;
		box.width = 1.0f;
		box.height = 1.0f;
	}

	if (self->tileset != NULL)
	{
		guint tsw = lrg_tileset_get_tile_width (self->tileset);
		guint tsh = lrg_tileset_get_tile_height (self->tileset);
		GrlTexture *grl_tex = lrg_tileset_get_texture (self->tileset);
		Texture2D *tex = NULL;

		if (tsw > 0 && tsh > 0)
		{
			tw = tsw;
			th = tsh;
		}
		lrg_tileset_get_draw_offset (self->tileset, &off_x, &off_y);
		if (grl_tex != NULL)
			tex = (Texture2D *) grl_texture_get_handle (grl_tex);
		if (tex != NULL && tex->id != 0 && tw > 0 && th > 0
		    && tex->width >= (gint) tw && tex->height >= (gint) th)
		{
			guint cols = (guint) tex->width / tw;
			guint count = cols * ((guint) tex->height / th);
			guint idx = (id - 1) % MAX (count, 1u);
			gfloat rx = (gfloat) ((idx % cols) * tw);
			gfloat ry = (gfloat) ((idx / cols) * th);

			textured = TRUE;
			u0 = rx / (gfloat) tex->width;
			v0 = ry / (gfloat) tex->height;
			u1 = (rx + (gfloat) tw) / (gfloat) tex->width;
			v1 = (ry + (gfloat) th) / (gfloat) tex->height;
		}
	}
	if (!textured)
	{
		u0 = 0.0f;
		v0 = 0.0f;
		u1 = 1.0f;
		v1 = 1.0f;
	}
	cell_quad_uv (raw, u0, v0, u1, v1, uu, vv);

	/* A texture sits on the image rectangle; a swatch fills the cell. */
	rot = textured ? lrg_tile_get_rotation (raw) : 0;
	if (textured)
	{
		hw = (gfloat) tw * 0.5f;
		hh = (gfloat) th * 0.5f;
		cx = box.x + (gfloat) off_x + hw;
		cy = box.y + box.height - (gfloat) th + (gfloat) off_y + hh;
	}
	else
	{
		hw = box.width * 0.5f;
		hh = box.height * 0.5f;
		cx = box.x + hw;
		cy = box.y + hh;
	}

	{
		const gfloat ox[4] = { -hw, hw, hw, -hw };
		const gfloat oy[4] = { -hh, -hh, hh, hh };

		for (i = 0; i < 4; i++)
		{
			gfloat ax = ox[i];
			gfloat ay = oy[i];

			for (k = 0; k < rot; k++)
			{
				gfloat t = ax;

				ax = -ay;
				ay = t;
			}
			px[i] = cx + ax;
			py[i] = cy + ay;
		}
	}

	/* Editor emit order: TL, BL, BR, TR, Y negated into local space. */
	for (i = 0; i < 4; i++)
	{
		if (out_xy != NULL)
		{
			out_xy[2 * i] = px[order[i]] * s;
			out_xy[2 * i + 1] = -py[order[i]] * s;
		}
		if (out_uv != NULL)
		{
			out_uv[2 * i] = uu[order[i]];
			out_uv[2 * i + 1] = vv[order[i]];
		}
	}
	return TRUE;
}

gboolean
lrg_tilemap_component_get_local_bounds (LrgTilemapComponent *self,
                                        LrgBoundingBox3D    *out)
{
	guint w = 0, h = 0, n;

	g_return_val_if_fail (LRG_IS_TILEMAP_COMPONENT (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	lrg_tilemap_component_get_grid_size (self, &w, &h);
	n = lrg_tilemap_component_get_n_layers (self);
	if (w == 0 || h == 0 || n == 0)
		return FALSE;

	out->min.x = 0.0f;
	out->min.y = -(gfloat) h * self->tile_size;
	out->min.z = 0.0f;
	out->max.x = (gfloat) w * self->tile_size;
	out->max.y = 0.0f;
	out->max.z = LRG_TILEMAP_COMPONENT_LAYER_GAP * (gfloat) (n - 1);
	return TRUE;
}
