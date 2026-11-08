/* lrg-editor-tiles-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Tiles panel (the 0.8.0 tiles panel on the retained toolkit, grown
 * in 0.9.0 with rule tiles, tile transforms, iso/hex grids and tile
 * properties).
 *
 * Everything the panel changes goes through the shared state the
 * viewport also uses: the view state's tile-mode / tile-tool (through
 * the tilemap.* actions, so shortcuts, menus and IPC agree), the app's
 * LrgEditorTilePainter (tile with its transform bits, layer, brush size)
 * and undoable node snapshots (lrg_editor_snapshot_node +
 * lrg_editor_commit_node_state) for grid, layer and tileset edits.
 *
 * The widgets are refreshed from the model on the step after anything
 * changed (selection, an edit, an undo): values are copied with the
 * "syncing" guard so the widgets' change signals do not write back, and
 * a spin box the user is typing into is left alone. The layer and rule
 * tile rows are rebuilt only when their lists changed.
 *
 * Tilesets. A tilemap node's visual asset is either an image (sliced by
 * the grid's tile size, as in 0.8.0) or, since 0.9.0, an `.rtileset`
 * (lrg_tileset_new_from_asset_file() without a texture, cached process
 * wide by path and reloaded when the file's mtime changes). The `.rtileset`
 * brings the image, the slicing (margin, spacing), the per-tile
 * properties the panel shows and the rule tiles it paints with.
 *
 * Rule tiles. With a rule tile brush the viewport wraps every tile tool
 * gesture in lrg_editor_tiles_panel_begin/update/end_gesture(): begin
 * opens an undo group and points the painter at the rule tile's default
 * tile; after each painter step, update diffs the layer against the last
 * state, runs lrg_rule_tile_apply() over the changed cells' box grown by
 * two cells (staggered layouts reach two rows) on a copy and records the
 * resolved cells as one more TILES command inside the group; end closes
 * the group. One gesture stays one undo step and the painter keeps all
 * its tools (stroke, rectangle, fill, erase) without knowing about rules.
 *
 * Grid geometry. LrgNodeTilemap stores the layout (orientation, stagger,
 * hex side, cell size) and the runtime LrgTilemap has the math (exact
 * picking, cell polygons, draw order, rule tile neighbour layouts). The
 * helpers here keep one geometry-only LrgTilemap per grid (object data on
 * the LrgNodeTilemap, rebuilt when the layout changes) and map its pixel
 * space onto the node's local plane: (px, py) -> (px * s, -py * s) with
 * s = tile_size / cell width, which is exactly the 0.8.0 layout for
 * orthogonal grids. The viewport, the scene renderer and the gizmo
 * painter call the public helpers (patches in their files).
 *
 * The palette is an LrgCustomWidget: the tileset sliced into tiles (with
 * a window; headless only its size is read), or numbered colour swatches
 * without an image, with hover and selection highlights and badges for
 * tiles that collide, carry data, animate or belong to a rule tile.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-tiles-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "data/lrg-editor-tileset-editor.h"
#include "data/lrg-editor-sprite-editor.h"
#include "../lrg-editor-server.h"
#include "../lrg-editor-command.h"
#include "../../tilemap/lrg-rule-tile.h"
#include "../../tilemap/lrg-tilemap-layer.h"
#include "../../ui/lrg-ui-actions.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-spin-box.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-drop-target.h"
#include "../../ui/lrg-drag.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <raylib.h>
#include <rlgl.h>
#include <math.h>
#include <string.h>

#define SWATCH_COLUMNS   (8)
#define SWATCH_ROWS      (4)
#define PANEL_KEY        "lrg-editor-tiles-panel"
#define GEOMETRY_KEY     "lrg-editor-tiles-geometry"
#define GRID_LINE_LIMIT  (128u * 128u)

static const gchar * const tool_ids[] = {
	"tilemap.tool-brush", "tilemap.tool-eraser", "tilemap.tool-rect", "tilemap.tool-fill", "tilemap.tool-pick"
};
static const gchar * const tool_labels[] = { "Brush", "Eraser", "Rect", "Fill", "Pick" };
static const LrgIcon tool_icons[] = { LRG_ICON_EDIT, LRG_ICON_CLOSE, LRG_ICON_MENU, LRG_ICON_DOWNLOAD, LRG_ICON_SEARCH };
static const gchar * const orientation_labels[] = { "Orthogonal", "Isometric", "Staggered", "Hexagonal" };
static const gchar * const orientation_nicks[] = { "orthogonal", "isometric", "staggered", "hexagonal" };

struct _LrgEditorTilesPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditor            *editor;       /* ref */
	LrgEditorViewState   *view_state;   /* ref */

	/* Widgets */
	LrgWidget            *hint;         /* no tilemap selected */
	LrgWidget            *scroll;       /* LrgScrollBox around content */
	LrgWidget            *content;      /* LrgVBox */
	LrgWidget            *title;        /* node name */
	LrgWidget            *paint;        /* paint-mode chip */
	LrgWidget            *tools[5];
	LrgSpinBox           *brush;
	LrgSpinBox           *tile;
	LrgWidget            *xform_row;    /* flip / rotate chips and the preview */
	LrgWidget            *flip_x;
	LrgWidget            *flip_y;
	LrgWidget            *rotate;
	LrgWidget            *preview;      /* LrgCustomWidget: the transformed brush */
	LrgWidget            *create_grid;
	LrgWidget            *grid_box;
	LrgSpinBox           *grid_w;
	LrgSpinBox           *grid_h;
	LrgSpinBox           *tile_w;
	LrgSpinBox           *tile_h;
	LrgComboBox          *orientation;
	LrgWidget            *stagger_row;
	LrgComboBox          *stagger_axis;
	LrgComboBox          *stagger_index;
	LrgWidget            *cell_row;
	LrgSpinBox           *cell_w;
	LrgSpinBox           *cell_h;
	LrgWidget            *hex_row;
	LrgSpinBox           *hex_side;
	LrgWidget            *tileset;      /* drop zone (LrgCustomWidget) */
	LrgDropTarget        *drop;         /* borrowed, owned by the widget */
	LrgWidget            *layers;       /* LrgVBox of layer rows */
	LrgTextInput         *layer_name;
	LrgWidget            *rules_heading;
	LrgWidget            *rules;        /* LrgVBox of rule tile rows */
	LrgWidget            *palette;      /* LrgCustomWidget */
	LrgWidget            *info;         /* LrgCustomWidget: tile properties */
	LrgWidget            *tools_row;
	LrgWidget            *layer_row;

	/* Model mirror */
	LrgNode              *target;       /* weak (nodes die with the level) */
	gchar                *target_guid;
	gchar                *layers_sig;
	gchar                *rules_sig;
	gboolean              dirty;
	gboolean              syncing;

	/* The brush */
	gchar                *rule_tile;    /* rule tile brush, NULL for plain tiles */

	/* Palette */
	gchar                *palette_path; /* absolute image path the size / texture is for */
	GrlTexture           *palette_tex;  /* nullable (no window, or no image) */
	gint                  image_w;
	gint                  image_h;
	gint                  hover_tile;   /* 1-based id under the pointer, 0 = none */

	/* The rule tile gesture around a viewport tool */
	gboolean              gesture;
	LrgNode              *gesture_node; /* ref while active */
	guint                 gesture_layer;
	guint                *gesture_cells;
	gsize                 gesture_n;

	/* editor.tilemap.flip-y shares Y with tool.toggle-snap. The window
	 * rebinds every action with lrg_canvas_add_shortcut(), which drops
	 * the other binding of the chord, so the panel puts flip-y back. */

	gchar               **flip_accels;  /* chords this panel last ensured */
};

G_DEFINE_FINAL_TYPE (LrgEditorTilesPanel, lrg_editor_tiles_panel, LRG_TYPE_CONTAINER)

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static LrgNodeTilemap *
target_grid (LrgNode *node)
{
	return (node != NULL && lrg_node_get_visual (node) != NULL)
	       ? lrg_node_visual_get_tilemap (lrg_node_get_visual (node)) : NULL;
}

static gboolean
is_tilemap_node (LrgNode *node)
{
	LrgNodeVisual *visual = (node != NULL && LRG_IS_NODE (node)) ? lrg_node_get_visual (node) : NULL;

	return visual != NULL && lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_TILEMAP;
}

static LrgEditorTilePainter *
painter (LrgEditorTilesPanel *self)
{
	return self->app != NULL ? self->app->tile_painter : NULL;
}

static gfloat
node_tile_size (LrgNode *node)
{
	LrgNodeVisual *visual = lrg_node_get_visual (node);
	gdouble        ts = (visual != NULL) ? lrg_node_visual_get_param_double (visual, "tile_size", 1.0) : 1.0;

	return (ts > 0.0) ? (gfloat) ts : 1.0f;
}

/* A stable, distinct colour per tile id (the viewport uses the same). */
static void
tile_color (guint     id,
            GrlColor *out)
{
	static const guint32 palette[] = {
		0xf38ba8ff, 0xfab387ff, 0xf9e2afff, 0xa6e3a1ff, 0x94e2d5ff, 0x89dcebff,
		0x74c7ecff, 0x89b4faff, 0xb4befeff, 0xcba6f7ff, 0xf5c2e7ff, 0xeba0acff
	};
	guint32 c = (id == 0) ? 0u : palette[(id - 1) % G_N_ELEMENTS (palette)];

	out->r = (guint8) (c >> 24);
	out->g = (guint8) (c >> 16);
	out->b = (guint8) (c >> 8);
	out->a = (guint8) c;
}

static gboolean
is_rtileset_path (const gchar *path)
{
	return path != NULL && g_str_has_suffix (path, ".rtileset");
}

/* ==========================================================================
 * The .rtileset cache (process wide, reloaded on mtime change)
 * ========================================================================== */

typedef struct
{
	LrgTileset *tileset;   /* NULL when the file failed to load */
	gint64      mtime;
	goffset     size;
} CachedTileset;

static GHashTable *tileset_cache = NULL;   /* absolute path -> CachedTileset */

static void
cached_tileset_free (gpointer data)
{
	CachedTileset *c = data;

	g_clear_object (&c->tileset);
	g_free (c);
}

/*
 * load_tileset:
 * The tileset of an absolute `.rtileset` path, parsed without a texture.
 * The file is stat()ed on every call (cheap) and parsed again when its
 * mtime or size changed, so saving it in the tileset editor shows in the
 * palette and the viewport on the next frame.
 */
static LrgTileset *
load_tileset (const gchar *abs_path)
{
	CachedTileset    *c;
	GStatBuf          st;
	g_autoptr(GError) error = NULL;

	if (abs_path == NULL || g_stat (abs_path, &st) != 0)
		return NULL;
	if (tileset_cache == NULL)
		tileset_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, cached_tileset_free);

	c = g_hash_table_lookup (tileset_cache, abs_path);
	if (c != NULL && c->mtime == (gint64) st.st_mtime && c->size == (goffset) st.st_size)
		return c->tileset;
	if (c == NULL)
	{
		c = g_new0 (CachedTileset, 1);
		g_hash_table_insert (tileset_cache, g_strdup (abs_path), c);
	}
	g_clear_object (&c->tileset);
	c->mtime = (gint64) st.st_mtime;
	c->size = (goffset) st.st_size;
	c->tileset = lrg_tileset_new_from_asset_file (abs_path, FALSE, &error);
	if (c->tileset == NULL)
		g_debug ("tiles panel: %s", error->message);
	return c->tileset;
}

/* The tileset image's absolute path (relative to the .rtileset), or NULL. */
static gchar *
tileset_image_path (LrgTileset  *tileset,
                    const gchar *tileset_path)
{
	const gchar      *image = (tileset != NULL) ? lrg_tileset_get_image_path (tileset) : NULL;
	g_autofree gchar *dir = NULL;

	if (image == NULL || *image == '\0')
		return NULL;
	if (g_path_is_absolute (image))
		return g_strdup (image);
	dir = g_path_get_dirname (tileset_path);
	return g_build_filename (dir, image, NULL);
}

/* The node's asset made absolute against @base_dir (NULL: as is). */
static gchar *
node_asset_path (LrgNode     *node,
                 const gchar *base_dir)
{
	const gchar *asset = (lrg_node_get_visual (node) != NULL) ? lrg_node_visual_get_asset (lrg_node_get_visual (node))
	                                                          : NULL;

	if (asset == NULL || *asset == '\0')
		return NULL;
	if (g_path_is_absolute (asset) || base_dir == NULL || *base_dir == '\0')
		return g_strdup (asset);
	return g_build_filename (base_dir, asset, NULL);
}

/* The node's .rtileset, resolved against the app's project. */
static LrgTileset *
node_tileset (LrgEditorApp *app,
              LrgNode      *node)
{
	g_autofree gchar *abs = NULL;

	if (node == NULL || lrg_node_get_visual (node) == NULL ||
	    !is_rtileset_path (lrg_node_visual_get_asset (lrg_node_get_visual (node))))
		return NULL;
	abs = (app != NULL) ? editor_app_project_path (app, lrg_node_visual_get_asset (lrg_node_get_visual (node)))
	                    : node_asset_path (node, NULL);
	return load_tileset (abs);
}

/* ==========================================================================
 * Grid geometry
 * ========================================================================== */

typedef struct
{
	LrgTilemap             *map;
	guint                   w, h, tw, th, hex, cw, ch;
	LrgTilemapOrientation   orientation;
	LrgTilemapStaggerAxis   axis;
	LrgTilemapStaggerIndex  index;
} Geometry;

static void
geometry_free (gpointer data)
{
	Geometry *g = data;

	g_clear_object (&g->map);
	g_free (g);
}

/*
 * grid_geometry:
 * The geometry LrgTilemap of a node grid: a tileset of the grid's tile
 * size, one empty layer of the grid's size and the grid's layout. Cached
 * on the grid object and rebuilt when any of those changes.
 */
static LrgTilemap *
grid_geometry (LrgNodeTilemap *grid)
{
	Geometry               *g = g_object_get_data (G_OBJECT (grid), GEOMETRY_KEY);
	LrgTilemapOrientation   o;
	LrgTilemapStaggerAxis   axis;
	LrgTilemapStaggerIndex  index;
	guint                   hex, cw, ch;
	guint                   w = lrg_node_tilemap_get_width (grid);
	guint                   h = lrg_node_tilemap_get_height (grid);
	guint                   tw = MAX (1u, lrg_node_tilemap_get_tile_width (grid));
	guint                   th = MAX (1u, lrg_node_tilemap_get_tile_height (grid));

	lrg_node_tilemap_get_grid (grid, &o, &axis, &index, &hex, &cw, &ch);
	if (g != NULL && g->w == w && g->h == h && g->tw == tw && g->th == th && g->orientation == o &&
	    g->axis == axis && g->index == index && g->hex == hex && g->cw == cw && g->ch == ch)
		return g->map;

	g = g_new0 (Geometry, 1);
	g->w = w;
	g->h = h;
	g->tw = tw;
	g->th = th;
	g->orientation = o;
	g->axis = axis;
	g->index = index;
	g->hex = hex;
	g->cw = cw;
	g->ch = ch;
	{
		g_autoptr(LrgTileset)      ts = lrg_tileset_new_empty (tw, th, 1, 1);
		g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new (MAX (1u, w), MAX (1u, h));

		g->map = lrg_tilemap_new (ts);
		lrg_tilemap_set_orientation (g->map, o);
		lrg_tilemap_set_stagger_axis (g->map, axis);
		lrg_tilemap_set_stagger_index (g->map, index);
		lrg_tilemap_set_hex_side_length (g->map, hex);
		lrg_tilemap_set_cell_size (g->map, cw, ch);
		lrg_tilemap_add_layer (g->map, layer);
	}
	g_object_set_data_full (G_OBJECT (grid), GEOMETRY_KEY, g, geometry_free);
	return g->map;
}

LrgTilemap *
lrg_editor_tiles_panel_get_geometry (LrgNode *node,
                                     gfloat  *scale)
{
	LrgNodeTilemap *grid;
	LrgTilemap     *map;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	if (scale != NULL)
		*scale = 1.0f;
	if (!is_tilemap_node (node) || (grid = target_grid (node)) == NULL)
		return NULL;
	map = grid_geometry (grid);
	if (scale != NULL)
		*scale = node_tile_size (node) / (gfloat) MAX (1u, lrg_tilemap_get_cell_width (map));
	return map;
}

static gboolean
is_orthogonal (LrgNodeTilemap *grid)
{
	LrgTilemapOrientation o;

	lrg_node_tilemap_get_grid (grid, &o, NULL, NULL, NULL, NULL, NULL);
	return o == LRG_TILEMAP_ORIENTATION_ORTHOGONAL;
}

gboolean
lrg_editor_tiles_panel_local_to_cell (LrgNode *node,
                                      gfloat   local_x,
                                      gfloat   local_y,
                                      gint    *out_x,
                                      gint    *out_y)
{
	LrgNodeTilemap *grid;
	LrgTilemap     *map;
	gfloat          s;

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (out_x != NULL && out_y != NULL, FALSE);

	*out_x = *out_y = -1;
	if ((grid = target_grid (node)) == NULL)
		return FALSE;
	/* Orthogonal grids keep the 0.8.0 arithmetic bit for bit */
	if (is_orthogonal (grid))
		return lrg_node_tilemap_local_to_cell (grid, node_tile_size (node), local_x, local_y, out_x, out_y);
	map = lrg_editor_tiles_panel_get_geometry (node, &s);
	if (map == NULL || !(s > 0.0f))
		return FALSE;
	lrg_tilemap_world_to_cell (map, local_x / s, -local_y / s, out_x, out_y);
	return lrg_node_tilemap_in_bounds (grid, *out_x, *out_y);
}

gboolean
lrg_editor_tiles_panel_cell_at (LrgNode      *node,
                                const gfloat *world_point,
                                gint         *out_x,
                                gint         *out_y)
{
	gfloat world[16], inv[16], local[3];

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (world_point != NULL && out_x != NULL && out_y != NULL, FALSE);

	*out_x = *out_y = -1;
	if (target_grid (node) == NULL)
		return FALSE;
	lrg_editor_node_get_world_matrix (node, world);
	if (!lrg_editor_math_invert (world, inv))
		return FALSE;
	lrg_editor_math_transform_point (inv, world_point, local);
	return lrg_editor_tiles_panel_local_to_cell (node, local[0], local[1], out_x, out_y);
}

guint
lrg_editor_tiles_panel_get_cell_polygon (LrgNode *node,
                                         gint     x,
                                         gint     y,
                                         gfloat  *out_local)
{
	LrgTilemap *map;
	gfloat      s;
	guint       n, i;

	g_return_val_if_fail (LRG_IS_NODE (node), 0);
	g_return_val_if_fail (out_local != NULL, 0);

	map = lrg_editor_tiles_panel_get_geometry (node, &s);
	if (map == NULL)
		return 0;
	n = lrg_tilemap_get_cell_polygon (map, x, y, out_local);
	for (i = 0; i < n; i++)
	{
		out_local[2 * i] *= s;
		out_local[2 * i + 1] *= -s;
	}
	return n;
}

/* ==========================================================================
 * Drawing a tilemap node (scene renderer hook)
 * ========================================================================== */

/* What the tiles of a node are cut from. */
typedef struct
{
	LrgTileset *tileset;     /* .rtileset (nullable) */
	Texture2D  *tex;         /* nullable: swatches */
	guint       tw, th;      /* tile image size */
	guint       cols;        /* image tilesets: tiles per row */
	guint       count;       /* tiles in the image */
	gint        off_x, off_y;
	guint64     time_ms;     /* animation clock */
} TileSource;

/* Source rectangle of tile index @index, FALSE when out of range. */
static gboolean
source_rect (const TileSource *src,
             guint             index,
             GrlRectangle     *rect)
{
	if (src->tileset != NULL)
	{
		index = lrg_tileset_get_animated_tile (src->tileset, index, src->time_ms);
		return lrg_tileset_get_tile_rect_to (src->tileset, index, rect);
	}
	if (src->count == 0 || src->cols == 0)
		return FALSE;
	index %= src->count;
	rect->x = (gfloat) ((index % src->cols) * src->tw);
	rect->y = (gfloat) ((index / src->cols) * src->th);
	rect->width = (gfloat) src->tw;
	rect->height = (gfloat) src->th;
	return TRUE;
}

/*
 * emit_tile:
 * One textured (or swatch) quad for a cell value, placed like
 * LrgTilemap's draw_cell: the image's bottom-left on the cell box's
 * bottom-left plus the draw offset, mirrored then rotated about its
 * centre. Pixel coordinates map to local ones with (px * s, -py * s).
 */
static void
emit_tile (const TileSource   *src,
           guint               raw,
           const GrlRectangle *box,
           gfloat              s,
           gfloat              z)
{
	guint        id = lrg_tile_get_id (raw);
	GrlRectangle r;
	gfloat       u[4], v[4], px[4], py[4];
	gfloat       cx, cy, hw, hh;
	guint        rot = lrg_tile_get_rotation (raw);
	gint         i;
	Color        c = WHITE;

	if (id == 0)
		return;
	hw = (gfloat) src->tw * 0.5f;
	hh = (gfloat) src->th * 0.5f;
	cx = box->x + (gfloat) src->off_x + hw;
	cy = box->y + box->height - (gfloat) src->th + (gfloat) src->off_y + hh;

	if (src->tex != NULL && source_rect (src, id - 1, &r))
	{
		gfloat u0 = r.x / (gfloat) src->tex->width, u1 = (r.x + r.width) / (gfloat) src->tex->width;
		gfloat v0 = r.y / (gfloat) src->tex->height, v1 = (r.y + r.height) / (gfloat) src->tex->height;

		if (lrg_tile_get_mirrored (raw))
		{
			gfloat t = u0;

			u0 = u1;
			u1 = t;
		}
		/* image corners TL, TR, BR, BL */
		u[0] = u0; v[0] = v0;
		u[1] = u1; v[1] = v0;
		u[2] = u1; v[2] = v1;
		u[3] = u0; v[3] = v1;
	}
	else
	{
		GrlColor sw;

		tile_color (id, &sw);
		c = (Color) { sw.r, sw.g, sw.b, sw.a };
		for (i = 0; i < 4; i++)
			u[i] = v[i] = 0.0f;
		/* swatches fill the cell box, not the image */
		hw = box->width * 0.5f;
		hh = box->height * 0.5f;
		cx = box->x + hw;
		cy = box->y + hh;
		rot = 0;
	}

	/* Corner offsets, turned clockwise on screen (y down) by rot * 90 */
	{
		const gfloat ox[4] = { -hw, hw, hw, -hw };
		const gfloat oy[4] = { -hh, -hh, hh, hh };

		for (i = 0; i < 4; i++)
		{
			gfloat ax = ox[i], ay = oy[i], t;
			guint  k;

			for (k = 0; k < rot; k++)
			{
				t = ax;
				ax = -ay;
				ay = t;
			}
			px[i] = cx + ax;
			py[i] = cy + ay;
		}
	}

	rlColor4ub (c.r, c.g, c.b, c.a);
	rlNormal3f (0.0f, 0.0f, 1.0f);
	/* counter-clockwise in local space (y flipped): TL, BL, BR, TR */
	rlTexCoord2f (u[0], v[0]);
	rlVertex3f (px[0] * s, -py[0] * s, z);
	rlTexCoord2f (u[3], v[3]);
	rlVertex3f (px[3] * s, -py[3] * s, z);
	rlTexCoord2f (u[2], v[2]);
	rlVertex3f (px[2] * s, -py[2] * s, z);
	rlTexCoord2f (u[1], v[1]);
	rlVertex3f (px[1] * s, -py[1] * s, z);
}

void
lrg_editor_tiles_panel_draw_tilemap (LrgWorld3DRenderer *renderer,
                                     LrgNode            *node,
                                     const gfloat       *world,
                                     gboolean            show_grid)
{
	LrgNodeTilemap   *grid;
	LrgTilemap       *map;
	TileSource        src;
	gfloat            s;
	guint             w, h, layer, i;
	g_autofree gchar *asset = NULL;
	g_autoptr(GArray) order = NULL;
	gboolean          overlap;
	const gchar      *base;

	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (world != NULL);

	if ((grid = target_grid (node)) == NULL || (map = lrg_editor_tiles_panel_get_geometry (node, &s)) == NULL)
		return;
	w = lrg_node_tilemap_get_width (grid);
	h = lrg_node_tilemap_get_height (grid);

	/* Where the tiles come from: an .rtileset or a plain image */
	memset (&src, 0, sizeof src);
	src.tw = MAX (1u, lrg_node_tilemap_get_tile_width (grid));
	src.th = MAX (1u, lrg_node_tilemap_get_tile_height (grid));
	src.time_ms = (guint64) (g_get_monotonic_time () / 1000);
	base = (renderer != NULL) ? lrg_world3d_renderer_get_base_dir (renderer) : NULL;
	asset = node_asset_path (node, base);
	if (is_rtileset_path (asset))
	{
		src.tileset = load_tileset (asset);
		if (src.tileset != NULL)
		{
			g_autofree gchar *image = tileset_image_path (src.tileset, asset);
			GrlTexture       *gtex = (image != NULL && renderer != NULL)
			                         ? lrg_world3d_renderer_get_texture (renderer, image) : NULL;

			src.tw = lrg_tileset_get_tile_width (src.tileset);
			src.th = lrg_tileset_get_tile_height (src.tileset);
			lrg_tileset_get_draw_offset (src.tileset, &src.off_x, &src.off_y);
			src.tex = (gtex != NULL) ? (Texture2D *) grl_texture_get_handle (gtex) : NULL;
		}
	}
	else if (asset != NULL && renderer != NULL)
	{
		GrlTexture *gtex = lrg_world3d_renderer_get_texture (renderer, lrg_node_visual_get_asset (lrg_node_get_visual (node)));

		src.tex = (gtex != NULL) ? (Texture2D *) grl_texture_get_handle (gtex) : NULL;
		if (src.tex != NULL && src.tex->width >= (gint) src.tw && src.tex->height >= (gint) src.th)
		{
			src.cols = (guint) src.tex->width / src.tw;
			src.count = src.cols * ((guint) src.tex->height / src.th);
		}
		else
			src.tex = NULL;
	}
	if (src.tex != NULL && (src.tex->id == 0 || src.tex->width <= 0 || src.tex->height <= 0))
		src.tex = NULL;

	/* Tiles taller or wider than their cell (and every non-orthogonal
	 * grid) overlap: draw them back to front without depth writes */
	overlap = !is_orthogonal (grid) || src.tw != lrg_tilemap_get_cell_width (map) ||
	          src.th != lrg_tilemap_get_cell_height (map) || src.off_x != 0 || src.off_y != 0;
	order = lrg_tilemap_get_draw_order (map, 0, 0, w, h);

	rlPushMatrix ();
	rlMultMatrixf (world);
	rlDisableBackfaceCulling ();
	if (overlap)
	{
		rlDrawRenderBatchActive ();   /* the depth mask is GL state: flush first */
		rlDisableDepthMask ();
	}
	for (layer = 0; layer < lrg_node_tilemap_get_n_layers (grid); layer++)
	{
		gfloat       z = 0.002f * (gfloat) layer;
		gsize        n = 0;
		const guint *cells = lrg_node_tilemap_get_layer_tiles (grid, layer, &n);

		if (!lrg_node_tilemap_get_layer_visible (grid, layer) || cells == NULL || n < (gsize) w * h)
			continue;
		rlSetTexture (src.tex != NULL ? src.tex->id : rlGetTextureIdDefault ());
		rlBegin (RL_QUADS);
		for (i = 0; i + 1 < order->len; i += 2)
		{
			gint         cx = g_array_index (order, gint, i);
			gint         cy = g_array_index (order, gint, i + 1);
			guint        raw = cells[(gsize) cy * w + (gsize) cx];
			GrlRectangle box;

			if (raw == 0)
				continue;
			rlCheckRenderBatchLimit (4);
			lrg_tilemap_get_cell_bounds (map, cx, cy, &box);
			emit_tile (&src, raw, &box, s, z);
		}
		rlEnd ();
		rlSetTexture (0);
	}
	if (overlap)
	{
		rlDrawRenderBatchActive ();
		rlEnableDepthMask ();
	}
	rlEnableBackfaceCulling ();

	/* The bounds always; the cell outlines while painting this node */
	{
		Color edge = { 120, 200, 140, 200 };
		Color line = { 120, 200, 140, 70 };

		if (is_orthogonal (grid))
		{
			gfloat ts = node_tile_size (node);
			gfloat gw = (gfloat) w * ts, gh = -(gfloat) h * ts;
			guint  x, y;

			DrawLine3D ((Vector3) { 0.0f, 0.0f, 0.0f }, (Vector3) { gw, 0.0f, 0.0f }, edge);
			DrawLine3D ((Vector3) { gw, 0.0f, 0.0f }, (Vector3) { gw, gh, 0.0f }, edge);
			DrawLine3D ((Vector3) { gw, gh, 0.0f }, (Vector3) { 0.0f, gh, 0.0f }, edge);
			DrawLine3D ((Vector3) { 0.0f, gh, 0.0f }, (Vector3) { 0.0f, 0.0f, 0.0f }, edge);
			if (show_grid && w * h <= 256u * 256u)
			{
				for (x = 1; x < w; x++)
					DrawLine3D ((Vector3) { (gfloat) x * ts, 0.0f, 0.001f }, (Vector3) { (gfloat) x * ts, gh, 0.001f },
					            line);
				for (y = 1; y < h; y++)
					DrawLine3D ((Vector3) { 0.0f, -(gfloat) y * ts, 0.001f }, (Vector3) { gw, -(gfloat) y * ts, 0.001f },
					            line);
			}
		}
		else
		{
			/* Outer cells always (the map's silhouette), all with @show_grid */
			guint x, y;

			for (y = 0; y < h; y++)
				for (x = 0; x < w; x++)
				{
					gfloat   poly[12];
					guint    np, k;
					gboolean border = (x == 0 || y == 0 || x + 1 == w || y + 1 == h);

					if (!(show_grid && w * h <= GRID_LINE_LIMIT) && !border)
						continue;
					np = lrg_tilemap_get_cell_polygon (map, (gint) x, (gint) y, poly);
					for (k = 0; k < np; k++)
					{
						guint  j = (k + 1) % np;
						Vector3 a = { poly[2 * k] * s, -poly[2 * k + 1] * s, 0.001f };
						Vector3 b = { poly[2 * j] * s, -poly[2 * j + 1] * s, 0.001f };

						DrawLine3D (a, b, show_grid ? line : edge);
					}
				}
		}
	}
	rlPopMatrix ();
}

/* ==========================================================================
 * Hover outline (gizmo painter hook)
 * ========================================================================== */

gboolean
lrg_editor_tiles_panel_paint_hover (LrgPaintList        *list,
                                    LrgTheme            *theme,
                                    const LrgEditorView *view,
                                    LrgNode             *node,
                                    gint                 cell_x,
                                    gint                 cell_y,
                                    gboolean             erase,
                                    gfloat               origin_x,
                                    gfloat               origin_y,
                                    gfloat               scale)
{
	LrgNodeTilemap *grid;
	gfloat          poly[12], world[16], qx[6], qy[6];
	guint           n, i;
	GrlColor        edge, fill;
	const GrlColor *role;

	g_return_val_if_fail (LRG_IS_PAINT_LIST (list), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	if ((grid = target_grid (node)) == NULL || is_orthogonal (grid))
		return FALSE;
	n = lrg_editor_tiles_panel_get_cell_polygon (node, cell_x, cell_y, poly);
	if (n < 3)
		return FALSE;
	lrg_editor_node_get_world_matrix (node, world);
	for (i = 0; i < n; i++)
	{
		gfloat local[3] = { poly[2 * i], poly[2 * i + 1], 0.003f };
		gfloat p[3], u, v;

		lrg_editor_math_transform_point (world, local, p);
		if (!lrg_editor_view_project (view, p, &u, &v, NULL))
			return TRUE;   /* behind the camera: nothing to draw, but handled */
		qx[i] = origin_x + u * scale;
		qy[i] = origin_y + v * scale;
	}

	role = (theme != NULL) ? lrg_theme_get_color (theme, erase ? LRG_THEME_COLOR_DANGER : LRG_THEME_COLOR_ACCENT) : NULL;
	if (role != NULL)
		edge = *role;
	else
		edge = erase ? (GrlColor) { 0xf3, 0x8b, 0xa8, 0xff } : (GrlColor) { 0x89, 0xb4, 0xfa, 0xff };
	fill = edge;
	fill.a = 0x40;
	for (i = 1; i + 1 < n; i++)
		lrg_paint_list_triangle (list, qx[0], qy[0], qx[i], qy[i], qx[i + 1], qy[i + 1], &fill);
	for (i = 0; i < n; i++)
		lrg_paint_list_line (list, qx[i], qy[i], qx[(i + 1) % n], qy[(i + 1) % n], 2.0f, &edge);
	return TRUE;
}

/* ==========================================================================
 * Rule tiles
 * ========================================================================== */

/* The rule tile @name of @node's tileset, or NULL. */
static LrgRuleTile *
node_rule_tile (LrgEditorApp *app,
                LrgNode      *node,
                const gchar  *name)
{
	LrgTileset *ts = node_tileset (app, node);

	return (ts != NULL && name != NULL) ? lrg_tileset_get_rule_tile_by_name (ts, name) : NULL;
}

/* The cell value a rule tile paints before it resolves: its default
 * tile, else its first rule's first output. */
static guint
rule_seed_value (LrgRuleTile *rule)
{
	guint v = lrg_rule_tile_get_default_tile (rule);

	if (v == 0 && lrg_rule_tile_get_n_rules (rule) > 0)
		v = lrg_tile_rule_get_output (lrg_rule_tile_get_rule (rule, 0), 0, NULL);
	return v;
}

static LrgTileNeighborLayout
node_layout (LrgNode *node)
{
	LrgTilemap *map = lrg_editor_tiles_panel_get_geometry (node, NULL);

	return (map != NULL) ? lrg_tilemap_get_neighbor_layout (map) : LRG_TILE_NEIGHBOR_LAYOUT_SQUARE;
}

/* The painter's layer clamped to @grid's layers. */
static guint
effective_layer (LrgEditorTilesPanel *self,
                 LrgNodeTilemap      *grid)
{
	guint n = lrg_node_tilemap_get_n_layers (grid);
	guint l = painter (self) != NULL ? lrg_editor_tile_painter_get_layer (painter (self)) : 0;

	return MIN (l, n > 0 ? n - 1 : 0);
}

/*
 * commit_cells:
 * Records the difference between @before and @after (one layer) as one
 * TILES command. Returns the number of changed cells.
 */
static guint
commit_cells (LrgEditor   *editor,
              LrgNode     *node,
              guint        layer,
              guint        width,
              const guint *before,
              const guint *after,
              gsize        n,
              const gchar *label)
{
	LrgEditorCommand *cmd = lrg_editor_command_new_tiles (node, layer, label);
	gsize             i;
	guint             changed;

	for (i = 0; i < n; i++)
		if (before[i] != after[i])
			lrg_editor_command_tiles_add (cmd, (gint) (i % width), (gint) (i / width), before[i], after[i]);
	changed = lrg_editor_command_tiles_get_n_changes (cmd);
	if (changed == 0)
	{
		g_object_unref (cmd);
		return 0;
	}
	lrg_editor_execute (editor, cmd);   /* transfer full */
	return changed;
}

/*
 * paint_rule_cells:
 * Paints (or erases) cells with a rule tile on one layer of @node and
 * resolves them and their neighbours on a copy, then commits the
 * difference as one TILES command.
 */
static guint
paint_rule_cells (LrgEditor   *editor,
                  LrgNode     *node,
                  guint        layer,
                  LrgRuleTile *rule,
                  const gint  *cells,
                  gsize        n_values,
                  gboolean     erase)
{
	LrgNodeTilemap        *grid = target_grid (node);
	LrgTileNeighborLayout  layout = node_layout (node);
	const guint           *now;
	g_autofree guint      *copy = NULL;
	gsize                  n = 0, i;
	guint                  w;

	if (grid == NULL || layer >= lrg_node_tilemap_get_n_layers (grid))
		return 0;
	now = lrg_node_tilemap_get_layer_tiles (grid, layer, &n);
	w = lrg_node_tilemap_get_width (grid);
	copy = g_memdup2 (now, n * sizeof (guint));
	for (i = 0; i + 1 < n_values; i += 2)
	{
		if (erase)
			lrg_rule_tile_erase (rule, copy, n, w, layout, cells[i], cells[i + 1]);
		else
			lrg_rule_tile_paint (rule, copy, n, w, layout, cells[i], cells[i + 1]);
	}
	return commit_cells (editor, node, layer, w, now, copy, n, erase ? "Erase Rule Tile" : "Paint Rule Tile");
}

/* Plain cells with one value (the brush with its transform bits). */
static guint
paint_plain_cells (LrgEditor  *editor,
                   LrgNode    *node,
                   guint       layer,
                   guint       value,
                   const gint *cells,
                   gsize       n_values)
{
	LrgNodeTilemap   *grid = target_grid (node);
	const guint      *now;
	g_autofree guint *copy = NULL;
	gsize             n = 0, i;
	guint             w;

	if (grid == NULL || layer >= lrg_node_tilemap_get_n_layers (grid))
		return 0;
	now = lrg_node_tilemap_get_layer_tiles (grid, layer, &n);
	w = lrg_node_tilemap_get_width (grid);
	copy = g_memdup2 (now, n * sizeof (guint));
	for (i = 0; i + 1 < n_values; i += 2)
		if (lrg_node_tilemap_in_bounds (grid, cells[i], cells[i + 1]))
			copy[(gsize) cells[i + 1] * w + (gsize) cells[i]] = value;
	return commit_cells (editor, node, layer, w, now, copy, n, value == 0 ? "Erase Tiles" : "Paint Tiles");
}

/* Refresh: every rule tile of @node's tileset over @layer (-1: all). */
static guint
refresh_node (LrgEditorTilesPanel *self,
              LrgNode             *node,
              gint                 layer)
{
	LrgNodeTilemap *grid = target_grid (node);
	LrgTileset     *ts = node_tileset (self->app, node);
	guint           l, total = 0;

	if (grid == NULL || ts == NULL || lrg_tileset_get_n_rule_tiles (ts) == 0)
		return 0;
	lrg_editor_begin_group (self->editor, "Refresh Rule Tiles");
	for (l = 0; l < lrg_node_tilemap_get_n_layers (grid); l++)
	{
		gsize             n = 0;
		const guint      *now;
		g_autofree guint *copy = NULL;

		if (layer >= 0 && (guint) layer != l)
			continue;
		now = lrg_node_tilemap_get_layer_tiles (grid, l, &n);
		copy = g_memdup2 (now, n * sizeof (guint));
		lrg_tileset_apply_rule_tiles (ts, copy, n, lrg_node_tilemap_get_width (grid), node_layout (node), 0, 0,
		                              G_MAXUINT, G_MAXUINT);
		total += commit_cells (self->editor, node, l, lrg_node_tilemap_get_width (grid), now, copy, n,
		                       "Refresh Rule Tiles");
	}
	lrg_editor_end_group (self->editor);
	self->dirty = TRUE;
	return total;
}

/* ==========================================================================
 * The rule tile gesture (viewport hooks)
 * ========================================================================== */

static void
gesture_clear (LrgEditorTilesPanel *self)
{
	self->gesture = FALSE;
	g_clear_object (&self->gesture_node);
	g_clear_pointer (&self->gesture_cells, g_free);
	self->gesture_n = 0;
}

LrgEditorTilesPanel *
lrg_editor_tiles_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}

void
lrg_editor_tiles_panel_begin_gesture (LrgEditorApp *app,
                                      LrgNode      *node)
{
	LrgEditorTilesPanel *self;
	LrgRuleTile         *rule;
	LrgNodeTilemap      *grid;
	const guint         *cells;
	gsize                n = 0;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	self = lrg_editor_tiles_panel_find (app);
	if (self == NULL || self->gesture || self->rule_tile == NULL || !is_tilemap_node (node))
		return;
	if (lrg_editor_view_state_get_tile_tool (self->view_state) == LRG_EDITOR_TILE_TOOL_PICK)
		return;
	rule = node_rule_tile (app, node, self->rule_tile);
	if (rule == NULL || (grid = target_grid (node)) == NULL || lrg_node_tilemap_get_n_layers (grid) == 0)
		return;

	/* The painter writes the rule tile's seed value; update resolves it */
	if (painter (self) != NULL && rule_seed_value (rule) != 0)
		lrg_editor_tile_painter_set_tile (painter (self), rule_seed_value (rule));
	lrg_editor_begin_group (self->editor, "Paint Rule Tile");
	self->gesture = TRUE;
	self->gesture_node = g_object_ref (node);
	self->gesture_layer = effective_layer (self, grid);
	cells = lrg_node_tilemap_get_layer_tiles (grid, self->gesture_layer, &n);
	self->gesture_cells = g_memdup2 (cells, n * sizeof (guint));
	self->gesture_n = n;
}

void
lrg_editor_tiles_panel_update_gesture (LrgEditorApp *app)
{
	LrgEditorTilesPanel *self;
	LrgNodeTilemap      *grid;
	LrgRuleTile         *rule;
	const guint         *now;
	g_autofree guint    *copy = NULL;
	gsize                n = 0, i;
	guint                w;
	gint                 x0 = G_MAXINT, y0 = G_MAXINT, x1 = -1, y1 = -1;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	self = lrg_editor_tiles_panel_find (app);
	if (self == NULL || !self->gesture || (grid = target_grid (self->gesture_node)) == NULL ||
	    (rule = node_rule_tile (app, self->gesture_node, self->rule_tile)) == NULL ||
	    self->gesture_layer >= lrg_node_tilemap_get_n_layers (grid))
		return;
	now = lrg_node_tilemap_get_layer_tiles (grid, self->gesture_layer, &n);
	if (n != self->gesture_n)
	{
		/* The grid was resized under the gesture: start over from here */
		g_free (self->gesture_cells);
		self->gesture_cells = g_memdup2 (now, n * sizeof (guint));
		self->gesture_n = n;
		return;
	}

	/* The box of what the painter changed since the last update */
	w = lrg_node_tilemap_get_width (grid);
	for (i = 0; i < n; i++)
		if (now[i] != self->gesture_cells[i])
		{
			gint x = (gint) (i % w), y = (gint) (i / w);

			x0 = MIN (x0, x);
			y0 = MIN (y0, y);
			x1 = MAX (x1, x);
			y1 = MAX (y1, y);
		}
	if (x1 < 0)
		return;

	/* Resolve it and two cells around it on a copy */
	copy = g_memdup2 (now, n * sizeof (guint));
	x0 = MAX (0, x0 - 2);
	y0 = MAX (0, y0 - 2);
	lrg_rule_tile_apply (rule, copy, n, w, node_layout (self->gesture_node), x0, y0, (guint) (x1 + 3 - x0),
	                     (guint) (y1 + 3 - y0));
	commit_cells (self->editor, self->gesture_node, self->gesture_layer, w, now, copy, n, "Autotile");
	now = lrg_node_tilemap_get_layer_tiles (grid, self->gesture_layer, &n);
	memcpy (self->gesture_cells, now, n * sizeof (guint));
	self->dirty = TRUE;
}

void
lrg_editor_tiles_panel_end_gesture (LrgEditorApp *app)
{
	LrgEditorTilesPanel *self;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	self = lrg_editor_tiles_panel_find (app);
	if (self == NULL || !self->gesture)
		return;
	lrg_editor_tiles_panel_update_gesture (app);
	gesture_clear (self);
	lrg_editor_end_group (self->editor);
}

/* ==========================================================================
 * Palette source
 * ========================================================================== */

/* The target's .rtileset (nullable). */
static LrgTileset *
panel_tileset (LrgEditorTilesPanel *self)
{
	return node_tileset (self->app, self->target);
}

/* Loads (once per path) the palette image size, and its texture when a
 * window is open: the .rtileset's image or the plain image asset. */
static void
palette_load (LrgEditorTilesPanel *self)
{
	const gchar      *asset = NULL;
	g_autofree gchar *abs = NULL;

	if (self->target != NULL && lrg_node_get_visual (self->target) != NULL)
		asset = lrg_node_visual_get_asset (lrg_node_get_visual (self->target));
	if (asset != NULL && *asset != '\0' && self->app != NULL)
		abs = editor_app_project_path (self->app, asset);
	if (is_rtileset_path (abs))
	{
		LrgTileset *ts = load_tileset (abs);
		gchar      *image = tileset_image_path (ts, abs);

		g_free (abs);
		abs = image;
	}
	if (g_strcmp0 (abs, self->palette_path) == 0 &&
	    (self->palette_tex != NULL || !IsWindowReady () || abs == NULL))
		return;

	g_clear_object (&self->palette_tex);
	g_free (self->palette_path);
	self->palette_path = g_strdup (abs);
	self->image_w = self->image_h = 0;
	if (abs == NULL || !g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		return;

	if (IsWindowReady ())
	{
		GrlTexture *tex = grl_texture_new_from_file (abs);

		if (tex != NULL && grl_texture_is_valid (tex))
		{
			self->palette_tex = tex;
			self->image_w = grl_texture_get_width (tex);
			self->image_h = grl_texture_get_height (tex);
			return;
		}
		g_clear_object (&tex);
	}

	/* Headless: the size only (CPU decode, no GL) */
	{
		Image img = LoadImage (abs);

		self->image_w = img.width;
		self->image_h = img.height;
		UnloadImage (img);
	}
}

static void
palette_geometry (LrgEditorTilesPanel *self,
                  guint               *cols,
                  guint               *rows)
{
	LrgNodeTilemap *grid = target_grid (self->target);
	LrgTileset     *ts = panel_tileset (self);
	guint           tw = (grid != NULL) ? lrg_node_tilemap_get_tile_width (grid) : 16;
	guint           th = (grid != NULL) ? lrg_node_tilemap_get_tile_height (grid) : 16;

	if (ts != NULL && lrg_tileset_get_columns (ts) > 0 && lrg_tileset_get_tile_count (ts) > 0)
	{
		*cols = lrg_tileset_get_columns (ts);
		*rows = MAX (1u, (lrg_tileset_get_tile_count (ts) + *cols - 1) / *cols);
	}
	else if (self->image_w > 0 && self->image_h > 0)
	{
		*cols = MAX (1u, (guint) self->image_w / MAX (tw, 1u));
		*rows = MAX (1u, (guint) self->image_h / MAX (th, 1u));
	}
	else
	{
		*cols = SWATCH_COLUMNS;
		*rows = SWATCH_ROWS;
	}
}

/* Tiles in the palette (an .rtileset may end before its last row). */
static guint
palette_count (LrgEditorTilesPanel *self)
{
	LrgTileset *ts = panel_tileset (self);
	guint       cols, rows;

	if (ts != NULL && lrg_tileset_get_tile_count (ts) > 0)
		return lrg_tileset_get_tile_count (ts);
	palette_geometry (self, &cols, &rows);
	return cols * rows;
}

/* Cell size in the widget for a given width */
static void
palette_cell (LrgEditorTilesPanel *self,
              gfloat               width,
              gfloat              *cw,
              gfloat              *ch)
{
	LrgNodeTilemap *grid = target_grid (self->target);
	LrgTileset     *ts = panel_tileset (self);
	guint           cols, rows;
	gfloat          aspect = 1.0f;

	palette_geometry (self, &cols, &rows);
	if (ts != NULL)
		aspect = (gfloat) lrg_tileset_get_tile_height (ts) / (gfloat) MAX (1u, lrg_tileset_get_tile_width (ts));
	else if (self->image_w > 0 && grid != NULL)
		aspect = (gfloat) lrg_node_tilemap_get_tile_height (grid) / (gfloat) MAX (1u, lrg_node_tilemap_get_tile_width (grid));
	*cw = MAX (8.0f, width / (gfloat) cols);
	*cw = MIN (*cw, 64.0f);
	*ch = *cw * aspect;
}

/* The image rectangle of palette tile @index (0-based). */
static gboolean
palette_source (LrgEditorTilesPanel *self,
                guint                index,
                GrlRectangle        *rect)
{
	LrgTileset     *ts = panel_tileset (self);
	LrgNodeTilemap *grid = target_grid (self->target);
	guint           cols, rows, tw, th;

	if (ts != NULL)
		return lrg_tileset_get_tile_rect_to (ts, index, rect);
	palette_geometry (self, &cols, &rows);
	tw = (grid != NULL) ? MAX (1u, lrg_node_tilemap_get_tile_width (grid)) : 16;
	th = (grid != NULL) ? MAX (1u, lrg_node_tilemap_get_tile_height (grid)) : 16;
	rect->x = (gfloat) ((index % cols) * tw);
	rect->y = (gfloat) ((index / cols) * th);
	rect->width = (gfloat) tw;
	rect->height = (gfloat) th;
	return TRUE;
}

static void
palette_measure (LrgCustomWidget     *widget,
                 gfloat               avail_width,
                 gfloat               avail_height,
                 LrgCustomWidgetSize *size,
                 gpointer             user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	guint                cols, rows;
	gfloat               cw, ch, w = avail_width > 0.0f ? avail_width : 240.0f;

	palette_geometry (self, &cols, &rows);
	palette_cell (self, w, &cw, &ch);
	size->width = cw * (gfloat) cols;
	size->height = ch * (gfloat) rows;
}

static void
palette_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor      *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	LrgTileset          *ts = panel_tileset (self);
	LrgRuleTile         *brush_rule = node_rule_tile (self->app, self->target, self->rule_tile);
	guint                current = painter (self) != NULL ? lrg_tile_get_id (lrg_editor_tile_painter_get_tile (painter (self)))
	                                                      : 0;
	guint                cols, rows, i, count;
	gfloat               cw, ch;
	GrlColor             c;

	palette_geometry (self, &cols, &rows);
	palette_cell (self, width, &cw, &ch);
	count = palette_count (self);

	/* Backdrop (a dark well shows transparent tiles) */
	lrg_paint_list_rect (list, x - 2.0f, y - 2.0f, cw * cols + 4.0f, ch * rows + 4.0f, 4.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));

	for (i = 0; i < MIN (count, cols * rows); i++)
	{
		gfloat       cx = x + (gfloat) (i % cols) * cw;
		gfloat       cy = y + (gfloat) (i / cols) * ch;
		GrlRectangle src;

		if (self->palette_tex == NULL)
		{
			/* Numbered swatches in the viewport's colours */
			g_autofree gchar *num = g_strdup_printf ("%u", i + 1);
			GrlColor          ink = { 17, 17, 27, 255 };

			tile_color (i + 1, &c);
			lrg_paint_list_rect (list, cx + 1.0f, cy + 1.0f, cw - 2.0f, ch - 2.0f, 3.0f, &c);
			if (cw >= 18.0f)
				lrg_paint_list_text (list, font, num, cx + 3.0f, cy + 2.0f, MIN (fs, ch * 0.45f), 0.0f, &ink);
		}
		else if (palette_source (self, i, &src))
		{
			GrlColor white = { 255, 255, 255, 255 };

			lrg_paint_list_image (list, self->palette_tex, &src, cx, cy, cw, ch, 0.0f, &white);
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.5f, &c);
			lrg_paint_list_border (list, cx, cy, cw, ch, 0.0f, 1.0f, &c);
		}

		/* Badges of an .rtileset: collision, data, animation, rule tile */
		if (ts != NULL && cw >= 14.0f)
		{
			gfloat       d = MAX (3.0f, cw * 0.09f);
			gfloat       bx = cx + cw - d - 2.0f;
			LrgRuleTile *owner = lrg_tileset_find_rule_tile_for (ts, i + 1);
			g_auto(GStrv) keys = lrg_tileset_get_tile_data_keys (ts, i);

			if (lrg_tileset_tile_has_collision (ts, i))
			{
				lrg_paint_list_circle (list, bx, cy + d + 2.0f, d, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
				bx -= 2.0f * d + 2.0f;
			}
			if (keys != NULL && keys[0] != NULL)
			{
				lrg_paint_list_circle (list, bx, cy + d + 2.0f, d, lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK));
				bx -= 2.0f * d + 2.0f;
			}
			if (lrg_tileset_get_tile_animation_length (ts, i) > 0)
				lrg_paint_list_circle (list, bx, cy + d + 2.0f, d, lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
			if (owner != NULL)
			{
				lrg_paint_color_with_alpha (accent, owner == brush_rule ? 0.95f : 0.55f, &c);
				lrg_paint_list_triangle (list, cx, cy + ch, cx + MIN (cw, ch) * 0.3f, cy + ch, cx,
				                         cy + ch - MIN (cw, ch) * 0.3f, &c);
			}
		}

		if ((gint) (i + 1) == self->hover_tile && i + 1 != current)
		{
			lrg_paint_color_with_alpha (accent, 0.25f, &c);
			lrg_paint_list_rect (list, cx, cy, cw, ch, 2.0f, &c);
		}
		if (i + 1 == current && self->rule_tile == NULL)
			lrg_paint_list_border (list, cx - 1.0f, cy - 1.0f, cw + 2.0f, ch + 2.0f, 3.0f, 2.0f, accent);
	}
}

/* 1-based tile under a canvas point, 0 outside */
static guint
palette_tile_at (LrgEditorTilesPanel *self,
                 gfloat               px,
                 gfloat               py)
{
	LrgWidget *w = self->palette;
	guint      cols, rows, tile;
	gfloat     cw, ch, rx, ry;

	palette_geometry (self, &cols, &rows);
	palette_cell (self, lrg_widget_get_width (w), &cw, &ch);
	rx = px - lrg_widget_get_world_x (w);
	ry = py - lrg_widget_get_world_y (w);
	if (rx < 0.0f || ry < 0.0f || rx >= cw * cols || ry >= ch * rows)
		return 0;
	tile = (guint) (ry / ch) * cols + (guint) (rx / cw) + 1;
	return tile <= palette_count (self) ? tile : 0;
}

static gboolean
palette_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	guint                tile;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_tile = (gint) palette_tile_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
		lrg_widget_queue_layout (self->info);
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_tile = 0;
		lrg_widget_queue_layout (self->info);
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		tile = palette_tile_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
		if (tile != 0)
		{
			lrg_editor_tiles_panel_set_brush_tile (self, tile);
			if (self->target != NULL)
				lrg_editor_view_state_set_tile_mode (self->view_state, TRUE);
		}
		return TRUE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * Tile properties (the info box under the palette)
 * ========================================================================== */

/* The rows the info box shows for @tile (1-based), for its height. */
static guint
info_rows (LrgEditorTilesPanel *self,
           guint                tile)
{
	LrgTileset   *ts = panel_tileset (self);
	g_auto(GStrv) keys = NULL;
	guint         rows = 2;

	if (tile == 0)
		return 1;
	if (ts == NULL || tile > lrg_tileset_get_tile_count (ts))
		return 2;
	keys = lrg_tileset_get_tile_data_keys (ts, tile - 1);
	rows += (keys != NULL) ? g_strv_length (keys) : 0;
	if (lrg_tileset_get_tile_animation_length (ts, tile - 1) > 0)
		rows++;
	if (lrg_tileset_find_rule_tile_for (ts, tile) != NULL)
		rows++;
	return rows;
}

static void
info_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	guint                rows = info_rows (self, lrg_editor_tiles_panel_get_info_tile (self));

	size->width = avail_width > 0.0f ? avail_width : 240.0f;
	size->height = MAX (72.0f, (gfloat) rows * (fs + 6.0f) + 14.0f);
}

/* Draws the tile magnified with its collision polygons. */
static void
info_paint_tile (LrgEditorTilesPanel *self,
                 LrgPaintList        *list,
                 LrgTheme            *theme,
                 guint                tile,
                 gfloat               x,
                 gfloat               y,
                 gfloat               size)
{
	LrgTileset  *ts = panel_tileset (self);
	GrlRectangle src;
	GrlColor     c;
	gfloat       sx, sy, tw, th;
	guint        p, n, k;

	lrg_paint_list_rect (list, x, y, size, size, 4.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	if (self->palette_tex != NULL && palette_source (self, tile - 1, &src))
	{
		GrlColor white = { 255, 255, 255, 255 };

		lrg_paint_list_image (list, self->palette_tex, &src, x, y, size, size, 4.0f, &white);
	}
	else
	{
		tile_color (tile, &c);
		lrg_paint_list_rect (list, x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, 3.0f, &c);
	}
	if (ts == NULL || tile > lrg_tileset_get_tile_count (ts))
		return;

	/* Collision polygons in tile pixels, scaled to the box */
	tw = (gfloat) MAX (1u, lrg_tileset_get_tile_width (ts));
	th = (gfloat) MAX (1u, lrg_tileset_get_tile_height (ts));
	sx = size / tw;
	sy = size / th;
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER), 1.0f, &c);
	for (p = 0; p < lrg_tileset_get_tile_collision_polygon_count (ts, tile - 1); p++)
	{
		const gfloat *pts = lrg_tileset_get_tile_collision_polygon (ts, tile - 1, p, &n);

		for (k = 0; k + 1 < n; k += 2)
		{
			guint j = (k + 2) % n;

			lrg_paint_list_line (list, x + pts[k] * sx, y + pts[k + 1] * sy, x + pts[j] * sx, y + pts[j + 1] * sy,
			                     2.0f, &c);
		}
	}
	if ((lrg_tileset_get_tile_properties (ts, tile - 1) & LRG_TILE_PROPERTY_SOLID) != 0 &&
	    lrg_tileset_get_tile_collision_polygon_count (ts, tile - 1) == 0)
		lrg_paint_list_border (list, x + 1.0f, y + 1.0f, size - 2.0f, size - 2.0f, 3.0f, 2.0f, &c);
}

static void
info_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor      *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor      *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	LrgTileset          *ts = panel_tileset (self);
	guint                tile = lrg_editor_tiles_panel_get_info_tile (self);
	gfloat               thumb = MIN (64.0f, height - 12.0f);
	gfloat               tx = x + thumb + 18.0f, ty = y + 6.0f, line = fs + 6.0f;
	g_autofree gchar    *head = NULL;
	GrlColor             well;

	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT), 0.6f, &well);
	lrg_paint_list_rect (list, x, y, width, height, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS), &well);
	if (tile == 0)
	{
		lrg_paint_list_text (list, font, "Hover a palette tile to see its properties", x + 8.0f, y + 8.0f, fs, 0.0f,
		                     muted);
		return;
	}
	info_paint_tile (self, list, theme, tile, x + 6.0f, y + 6.0f, thumb);
	lrg_paint_list_push_clip (list, tx, y, MAX (0.0f, x + width - tx - 4.0f), height);

	head = g_strdup_printf ("Tile %u  (index %u)", tile, tile - 1);
	lrg_paint_list_text (list, font, head, tx, ty, fs, 0.0f, text);
	ty += line;
	if (ts == NULL || tile > lrg_tileset_get_tile_count (ts))
	{
		lrg_paint_list_text (list, font, ts == NULL ? "Plain image: use an .rtileset for tile properties"
		                                            : "Outside the tileset", tx, ty, fs, 0.0f, muted);
		lrg_paint_list_pop_clip (list);
		return;
	}

	/* Flags and collision */
	{
		LrgTileProperty   props = lrg_tileset_get_tile_properties (ts, tile - 1);
		g_autofree gchar *flags = g_strdup_printf ("%s%s%s%s · %u polygon(s)",
		                                           (props & LRG_TILE_PROPERTY_SOLID) ? "solid " : "",
		                                           (props & LRG_TILE_PROPERTY_ANIMATED) ? "animated " : "",
		                                           (props & LRG_TILE_PROPERTY_HAZARD) ? "hazard " : "",
		                                           props == LRG_TILE_PROPERTY_NONE ? "no flags" : "",
		                                           lrg_tileset_get_tile_collision_polygon_count (ts, tile - 1));

		lrg_paint_list_text (list, font, flags, tx, ty, fs, 0.0f, muted);
		ty += line;
	}

	/* Custom data */
	{
		g_auto(GStrv) keys = lrg_tileset_get_tile_data_keys (ts, tile - 1);
		guint         k;

		for (k = 0; keys != NULL && keys[k] != NULL; k++)
		{
			g_autofree gchar *row = g_strdup_printf ("%s = %s", keys[k],
			                                         lrg_tileset_get_tile_data (ts, tile - 1, keys[k]));

			lrg_paint_list_text (list, font, row, tx, ty, fs, 0.0f, text);
			ty += line;
		}
	}

	/* Animation */
	if (lrg_tileset_get_tile_animation_length (ts, tile - 1) > 0)
	{
		guint             k, total = 0, frame, ms;
		GString          *row = g_string_new ("animation:");

		for (k = 0; k < lrg_tileset_get_tile_animation_length (ts, tile - 1); k++)
			if (lrg_tileset_get_tile_animation_frame (ts, tile - 1, k, &frame, &ms))
			{
				g_string_append_printf (row, " %u", frame);
				total += ms;
			}
		g_string_append_printf (row, " (%u ms)", total);
		lrg_paint_list_text (list, font, row->str, tx, ty, fs, 0.0f, muted);
		g_string_free (row, TRUE);
		ty += line;
	}

	/* Rule tile ownership */
	{
		LrgRuleTile *owner = lrg_tileset_find_rule_tile_for (ts, tile);

		if (owner != NULL)
		{
			g_autofree gchar *row = g_strdup_printf ("rule tile: %s", lrg_rule_tile_get_name (owner));

			lrg_paint_list_text (list, font, row, tx, ty, fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		}
	}
	lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * Brush preview (the brush tile with its flip / rotation)
 * ========================================================================== */

typedef struct
{
	GrlTexture  *texture;   /* ref */
	GrlRectangle source;
	gfloat       x, y, size;
	guint        value;
} PreviewDraw;

static void
preview_draw_free (gpointer data)
{
	PreviewDraw *d = data;

	g_clear_object (&d->texture);
	g_free (d);
}

/* Immediate-mode draw: DrawTexturePro handles the mirror (negative
 * source width) and the quarter turns about the centre. */
static void
preview_draw (gpointer user_data)
{
	PreviewDraw *d = user_data;
	Texture2D   *tex = (Texture2D *) grl_texture_get_handle (d->texture);
	Rectangle    src = { d->source.x, d->source.y, d->source.width, d->source.height };
	Rectangle    dst = { d->x + d->size * 0.5f, d->y + d->size * 0.5f, d->size, d->size };

	if (tex == NULL || tex->id == 0)
		return;
	if (lrg_tile_get_mirrored (d->value))
		src.width = -src.width;
	DrawTexturePro (*tex, src, dst, (Vector2) { d->size * 0.5f, d->size * 0.5f },
	                (gfloat) lrg_tile_get_rotation (d->value) * 90.0f, WHITE);
}

static void
preview_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	guint                value = lrg_editor_tiles_panel_get_brush_value (self);
	guint                id = lrg_tile_get_id (value);
	gfloat               size = MIN (width, height);
	GrlRectangle         src;
	GrlColor             c;

	lrg_paint_list_rect (list, x, y, size, size, 4.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	if (self->rule_tile != NULL)
	{
		lrg_paint_list_icon (list, LRG_ICON_STAR, x + size * 0.2f, y + size * 0.2f, size * 0.6f, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		return;
	}
	if (id == 0)
		return;
	if (self->palette_tex != NULL && palette_source (self, id - 1, &src))
	{
		PreviewDraw *d = g_new0 (PreviewDraw, 1);

		d->texture = g_object_ref (self->palette_tex);
		d->source = src;
		d->x = x + 2.0f;
		d->y = y + 2.0f;
		d->size = size - 4.0f;
		d->value = value;
		lrg_paint_list_callback (list, preview_draw, d, preview_draw_free);
	}
	else
	{
		gfloat cx = x + size * 0.5f, cy = y + size * 0.5f, r = size * 0.32f;
		gfloat ax = 0.0f, ay = -r, t;
		guint  k;

		tile_color (id, &c);
		lrg_paint_list_rect (list, x + 3.0f, y + 3.0f, size - 6.0f, size - 6.0f, 3.0f, &c);
		/* an arrow shows the transform on a swatch */
		if (lrg_tile_get_mirrored (value))
			ax = -ax;
		for (k = 0; k < lrg_tile_get_rotation (value); k++)
		{
			t = ax;
			ax = -ay;
			ay = t;
		}
		c = (GrlColor) { 17, 17, 27, 255 };
		lrg_paint_list_line (list, cx, cy, cx + ax, cy + ay, 2.5f, &c);
		lrg_paint_list_circle (list, cx + ax, cy + ay, 3.0f, &c);
	}
}

/* ==========================================================================
 * Tileset drop zone
 * ========================================================================== */

static void
tileset_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat               radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gboolean             hot = self->drop != NULL && lrg_drop_target_get_current_drop (self->drop) != NULL;
	const GrlColor      *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const gchar         *asset = NULL;
	const gchar         *text;
	gfloat               icon = fs + 4.0f;
	GrlColor             fill;

	if (self->target != NULL && lrg_node_get_visual (self->target) != NULL)
		asset = lrg_node_visual_get_asset (lrg_node_get_visual (self->target));
	text = (asset != NULL && *asset != '\0') ? asset : "Drop an image or .rtileset from Assets";

	/* Dashed-look well: a tinted fill and an accent border while hovered */
	lrg_paint_color_with_alpha (hot ? accent : lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT), hot ? 0.22f : 1.0f,
	                            &fill);
	lrg_paint_list_rect (list, x, y, width, height, radius, &fill);
	lrg_paint_list_border (list, x, y, width, height, radius, hot ? 2.0f : 1.0f,
	                       hot ? accent : lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	lrg_paint_list_icon (list, is_rtileset_path (asset) ? LRG_ICON_MENU : LRG_ICON_FOLDER, x + 8.0f,
	                     y + (height - icon) * 0.5f, icon, 0.0f, accent);
	lrg_paint_list_push_clip (list, x + icon + 14.0f, y, MAX (0.0f, width - icon - 20.0f), height);
	lrg_paint_list_text (list, font, text, x + icon + 14.0f, y + (height - fs) * 0.5f, fs, 0.0f,
	                     lrg_theme_get_color (theme, asset != NULL ? LRG_THEME_COLOR_TEXT : LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

static gboolean
on_tileset_drop (LrgDropTarget  *target,
                 LrgDragPayload *payload,
                 gfloat          x,
                 gfloat          y,
                 gpointer        user_data)
{
	const gchar *path = lrg_drag_payload_get_string (payload);

	return path != NULL && lrg_editor_tiles_panel_set_tileset (LRG_EDITOR_TILES_PANEL (user_data), path);
}

/* ==========================================================================
 * Model -> widgets
 * ========================================================================== */

static void
set_spin (LrgEditorTilesPanel *self,
          LrgSpinBox          *spin,
          gdouble              value)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));
	LrgWidget *focus = LRG_IS_CANVAS (root) ? lrg_canvas_get_focused_widget (LRG_CANVAS (root)) : NULL;

	/* Leave the spin box the user is typing into alone */
	if (focus != NULL && (focus == LRG_WIDGET (spin) || lrg_widget_is_ancestor (focus, LRG_WIDGET (spin))))
		return;
	if (fabs (lrg_spin_box_get_value (spin) - value) > 1e-6)
		lrg_spin_box_set_value (spin, value);
}

static void
set_combo (LrgComboBox *combo,
           gint         index)
{
	if (lrg_combo_box_get_selected (combo) != index)
		lrg_combo_box_set_selected (combo, index);
}

static void on_layer_select (LrgWidget *chip, gpointer user_data);
static void on_layer_eye (LrgWidget *chip, gpointer user_data);
static void on_rule_chip (LrgWidget *chip, gpointer user_data);
static void on_refresh_rules (LrgWidget *chip, gpointer user_data);
static void on_edit_tileset (LrgWidget *chip, gpointer user_data);

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

/* Layer rows, top layer first like an image editor. */
static void
sync_layers (LrgEditorTilesPanel *self,
             LrgNodeTilemap      *grid)
{
	GString *sig = g_string_new (NULL);
	guint    n = (grid != NULL) ? lrg_node_tilemap_get_n_layers (grid) : 0;
	guint    current = painter (self) != NULL ? lrg_editor_tile_painter_get_layer (painter (self)) : 0;
	guint    i;

	if (n > 0 && current >= n && painter (self) != NULL)
		lrg_editor_tile_painter_set_layer (painter (self), current = n - 1);

	for (i = 0; i < n; i++)
		g_string_append_printf (sig, "%s|%d|%d\n", lrg_node_tilemap_get_layer_name (grid, i),
		                        lrg_node_tilemap_get_layer_visible (grid, i), i == current);
	if (g_strcmp0 (sig->str, self->layers_sig) == 0)
	{
		g_string_free (sig, TRUE);
		return;
	}
	g_free (self->layers_sig);
	self->layers_sig = g_string_free (sig, FALSE);

	lrg_container_remove_all (LRG_CONTAINER (self->layers));
	for (i = n; i-- > 0;)
	{
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		g_autofree gchar *eye_name = g_strdup_printf ("tiles-layer-eye-%u", i);
		g_autofree gchar *row_name = g_strdup_printf ("tiles-layer-%u", i);
		LrgWidget        *eye, *name;
		gboolean          visible = lrg_node_tilemap_get_layer_visible (grid, i);

		lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
		eye = editor_chip_new (eye_name, visible ? LRG_ICON_EYE : LRG_ICON_MINUS, NULL, on_layer_eye, self);
		editor_chip_set_checked (eye, visible);
		g_object_set_data (G_OBJECT (eye), "lrg-tiles-layer", GUINT_TO_POINTER (i));
		add_to (row, eye);
		name = editor_chip_new (row_name, LRG_ICON_NONE, lrg_node_tilemap_get_layer_name (grid, i), on_layer_select,
		                        self);
		editor_chip_set_checked (name, i == current);
		lrg_widget_set_fill_weight (name, 1.0f);
		g_object_set_data (G_OBJECT (name), "lrg-tiles-layer", GUINT_TO_POINTER (i));
		add_to (row, name);
		add_to (self->layers, row);
	}
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* Rule tile chips (one row each: the brush toggle with its rule count). */
static void
sync_rules (LrgEditorTilesPanel *self)
{
	LrgTileset *ts = panel_tileset (self);
	GString    *sig = g_string_new (NULL);
	guint       n = (ts != NULL) ? lrg_tileset_get_n_rule_tiles (ts) : 0;
	guint       i;

	g_string_append_printf (sig, "%p|%s|", (gpointer) ts, self->rule_tile != NULL ? self->rule_tile : "");
	for (i = 0; i < n; i++)
	{
		LrgRuleTile *rt = lrg_tileset_get_rule_tile (ts, i);

		g_string_append_printf (sig, "%s:%u\n", lrg_rule_tile_get_name (rt), lrg_rule_tile_get_n_rules (rt));
	}
	if (g_strcmp0 (sig->str, self->rules_sig) == 0)
	{
		g_string_free (sig, TRUE);
		return;
	}
	g_free (self->rules_sig);
	self->rules_sig = g_string_free (sig, FALSE);

	lrg_container_remove_all (LRG_CONTAINER (self->rules));
	lrg_widget_set_visible (self->rules_heading, is_rtileset_path (self->target != NULL &&
	                                                                lrg_node_get_visual (self->target) != NULL
	                                                                ? lrg_node_visual_get_asset (lrg_node_get_visual (self->target))
	                                                                : NULL));
	if (ts == NULL)
	{
		lrg_widget_queue_layout (LRG_WIDGET (self));
		return;
	}
	for (i = 0; i < n; i++)
	{
		LrgRuleTile      *rt = lrg_tileset_get_rule_tile (ts, i);
		g_autofree gchar *name = g_strdup_printf ("tiles-rule-%s", lrg_rule_tile_get_name (rt));
		LrgWidget        *chip = editor_chip_new (name, LRG_ICON_STAR, lrg_rule_tile_get_name (rt), on_rule_chip, self);
		g_autofree gchar *badge = g_strdup_printf ("%u", lrg_rule_tile_get_n_rules (rt));

		editor_chip_set_badge (chip, badge);
		editor_chip_set_checked (chip, g_strcmp0 (self->rule_tile, lrg_rule_tile_get_name (rt)) == 0);
		g_object_set_data_full (G_OBJECT (chip), "lrg-tiles-rule", g_strdup (lrg_rule_tile_get_name (rt)), g_free);
		lrg_widget_set_halign (chip, LRG_ALIGN_START);
		add_to (self->rules, chip);
	}
	{
		LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

		lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
		add_to (row, editor_chip_new ("tiles-refresh-rules", LRG_ICON_REFRESH, "Refresh all", on_refresh_rules, self));
		add_to (row, editor_chip_new ("tiles-edit-tileset", LRG_ICON_EDIT, "Edit tileset", on_edit_tileset, self));
		add_to (self->rules, row);
	}
	if (n == 0)
		add_to (self->rules, editor_panel_hint_new ("tiles-rules-empty", "No rule tiles yet: add them in the tileset editor"));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* Copies the model into the widgets (selection, edits, undo). */
static void
sync_widgets (LrgEditorTilesPanel *self)
{
	LrgNode          *node = (self->app != NULL) ? editor_tiles_target (self->app) : NULL;
	LrgNodeTilemap   *grid;
	LrgEditorTileTool tool = lrg_editor_view_state_get_tile_tool (self->view_state);
	guint             i, value;

	self->dirty = FALSE;
	g_set_weak_pointer (&self->target, node);
	lrg_widget_set_visible (self->hint, node == NULL);
	lrg_widget_set_visible (self->scroll, node != NULL);
	if (node == NULL)
	{
		g_clear_pointer (&self->target_guid, g_free);
		return;
	}
	if (g_strcmp0 (lrg_node_get_guid (node), self->target_guid) != 0)
	{
		g_free (self->target_guid);
		self->target_guid = g_strdup (lrg_node_get_guid (node));
		g_clear_pointer (&self->layers_sig, g_free);
		g_clear_pointer (&self->rules_sig, g_free);
	}
	/* A rule brush that the new tileset does not have is dropped */
	if (self->rule_tile != NULL && node_rule_tile (self->app, node, self->rule_tile) == NULL)
		g_clear_pointer (&self->rule_tile, g_free);

	self->syncing = TRUE;
	grid = target_grid (node);
	value = lrg_editor_tiles_panel_get_brush_value (self);
	lrg_label_set_text (LRG_LABEL (self->title), lrg_node_get_name (node));
	editor_chip_set_checked (self->paint, lrg_editor_view_state_get_tile_mode (self->view_state));
	editor_chip_set_label (self->paint, lrg_editor_view_state_get_tile_mode (self->view_state) ? "Painting (B)"
	                                                                                           : "Paint (B)");
	for (i = 0; i < G_N_ELEMENTS (self->tools); i++)
		editor_chip_set_checked (self->tools[i], (guint) tool == i);
	if (painter (self) != NULL)
	{
		set_spin (self, self->brush, lrg_editor_tile_painter_get_brush_size (painter (self)));
		set_spin (self, self->tile, lrg_tile_get_id (value));
	}
	editor_chip_set_checked (self->flip_x, lrg_tile_get_mirrored (value));
	editor_chip_set_checked (self->flip_y, (value & LRG_TILE_FLIP_VERTICAL) != 0 && !lrg_tile_get_mirrored (value));
	{
		g_autofree gchar *rot = g_strdup_printf ("%u°", lrg_tile_get_rotation (value) * 90u);

		editor_chip_set_label (self->rotate, rot);
		editor_chip_set_checked (self->rotate, lrg_tile_get_rotation (value) != 0);
	}

	lrg_widget_set_visible (self->create_grid, grid == NULL);
	lrg_widget_set_visible (self->grid_box, grid != NULL);
	if (grid != NULL)
	{
		LrgTilemapOrientation  o;
		LrgTilemapStaggerAxis  axis;
		LrgTilemapStaggerIndex index;
		guint                  hex, cw, ch;

		lrg_node_tilemap_get_grid (grid, &o, &axis, &index, &hex, &cw, &ch);
		set_spin (self, self->grid_w, lrg_node_tilemap_get_width (grid));
		set_spin (self, self->grid_h, lrg_node_tilemap_get_height (grid));
		set_spin (self, self->tile_w, lrg_node_tilemap_get_tile_width (grid));
		set_spin (self, self->tile_h, lrg_node_tilemap_get_tile_height (grid));
		set_combo (self->orientation, (gint) o);
		set_combo (self->stagger_axis, (gint) axis);
		set_combo (self->stagger_index, (gint) index);
		set_spin (self, self->cell_w, cw);
		set_spin (self, self->cell_h, ch);
		set_spin (self, self->hex_side, hex);
		lrg_widget_set_visible (self->stagger_row, o == LRG_TILEMAP_ORIENTATION_STAGGERED ||
		                                           o == LRG_TILEMAP_ORIENTATION_HEXAGONAL);
		lrg_widget_set_visible (self->hex_row, o == LRG_TILEMAP_ORIENTATION_HEXAGONAL);
		lrg_widget_set_visible (self->cell_row, o != LRG_TILEMAP_ORIENTATION_ORTHOGONAL);
	}
	sync_layers (self, grid);
	sync_rules (self);
	palette_load (self);
	lrg_widget_queue_layout (self->palette);
	lrg_widget_queue_layout (self->info);
	self->syncing = FALSE;
}

/* ==========================================================================
 * Widgets -> model
 * ========================================================================== */

static void
run_action (LrgEditorTilesPanel *self,
            const gchar         *id)
{
	if (self->app != NULL && lrg_editor_actions_is_enabled (self->app->actions, id))
		lrg_editor_actions_activate (self->app->actions, id);
	self->dirty = TRUE;
}

static void
on_paint (LrgWidget *chip,
          gpointer   user_data)
{
	run_action (LRG_EDITOR_TILES_PANEL (user_data), "tilemap.paint-mode");
}

static void
on_tool (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	guint                i;

	for (i = 0; i < G_N_ELEMENTS (self->tools); i++)
		if (self->tools[i] == chip)
			run_action (self, tool_ids[i]);
}

static void
on_brush (LrgSpinBox *spin,
          gdouble     value,
          gpointer    user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);

	if (!self->syncing && painter (self) != NULL)
		lrg_editor_tile_painter_set_brush_size (painter (self), (guint) MAX (1.0, value));
}

static void
on_tile (LrgSpinBox *spin,
         gdouble     value,
         gpointer    user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);

	if (!self->syncing && painter (self) != NULL)
		lrg_editor_tiles_panel_set_brush_tile (self, (guint) MAX (0.0, value));
}

static void on_flip_x (LrgWidget *c, gpointer d) { lrg_editor_tiles_panel_flip_brush (LRG_EDITOR_TILES_PANEL (d), FALSE); }
static void on_flip_y (LrgWidget *c, gpointer d) { lrg_editor_tiles_panel_flip_brush (LRG_EDITOR_TILES_PANEL (d), TRUE); }
static void on_rotate (LrgWidget *c, gpointer d) { lrg_editor_tiles_panel_rotate_brush (LRG_EDITOR_TILES_PANEL (d), 1); }

static void
on_create_grid (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);

	if (self->target != NULL && painter (self) != NULL)
		lrg_editor_tile_painter_ensure_tilemap (painter (self), self->target);
	self->dirty = TRUE;
}

/* Grid size and slicing: one undo step per change */
static void
on_grid_changed (LrgSpinBox *spin,
                 gdouble     value,
                 gpointer    user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	LrgNodeTilemap      *grid = target_grid (self->target);
	g_autoptr(LrgNode)   before = NULL;
	gboolean             size = (spin == self->grid_w || spin == self->grid_h);

	if (self->syncing || grid == NULL)
		return;
	before = lrg_editor_snapshot_node (self->target);
	if (size)
		lrg_node_tilemap_resize (grid, (guint) MAX (1.0, lrg_spin_box_get_value (self->grid_w)),
		                         (guint) MAX (1.0, lrg_spin_box_get_value (self->grid_h)));
	else
		lrg_node_tilemap_set_tile_size (grid, (guint) MAX (1.0, lrg_spin_box_get_value (self->tile_w)),
		                                (guint) MAX (1.0, lrg_spin_box_get_value (self->tile_h)));
	lrg_editor_commit_node_state (self->editor, self->target, before, size ? "Resize Tilemap" : "Tile Size", FALSE);
	self->dirty = TRUE;
}

/* Orientation, stagger, hex side and cell size: one undo step each */
static void
apply_layout_widgets (LrgEditorTilesPanel *self)
{
	if (self->syncing || target_grid (self->target) == NULL)
		return;
	lrg_editor_tiles_panel_set_grid (self, (LrgTilemapOrientation) MAX (0, lrg_combo_box_get_selected (self->orientation)),
	                                 (LrgTilemapStaggerAxis) MAX (0, lrg_combo_box_get_selected (self->stagger_axis)),
	                                 (LrgTilemapStaggerIndex) MAX (0, lrg_combo_box_get_selected (self->stagger_index)),
	                                 (guint) MAX (0.0, lrg_spin_box_get_value (self->hex_side)),
	                                 (guint) MAX (0.0, lrg_spin_box_get_value (self->cell_w)),
	                                 (guint) MAX (0.0, lrg_spin_box_get_value (self->cell_h)));
}

static void
on_layout_combo (LrgComboBox *combo,
                 gpointer     user_data)
{
	apply_layout_widgets (LRG_EDITOR_TILES_PANEL (user_data));
}

static void
on_layout_spin (LrgSpinBox *spin,
                gdouble     value,
                gpointer    user_data)
{
	apply_layout_widgets (LRG_EDITOR_TILES_PANEL (user_data));
}

static void
on_layer_select (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);

	if (painter (self) != NULL)
		lrg_editor_tile_painter_set_layer (painter (self),
		                                   GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (chip), "lrg-tiles-layer")));
	self->dirty = TRUE;
}

static void
on_layer_eye (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	guint                layer = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (chip), "lrg-tiles-layer"));
	LrgNodeTilemap      *grid = target_grid (self->target);

	if (grid != NULL && layer < lrg_node_tilemap_get_n_layers (grid))
		lrg_editor_tiles_panel_set_layer_visible (self, layer, !lrg_node_tilemap_get_layer_visible (grid, layer));
}

static void on_add_layer (LrgWidget *c, gpointer d)    { run_action (LRG_EDITOR_TILES_PANEL (d), "tilemap.add-layer"); }
static void on_remove_layer (LrgWidget *c, gpointer d) { run_action (LRG_EDITOR_TILES_PANEL (d), "tilemap.remove-layer"); }

/* A rule tile chip toggles the rule brush */
static void
on_rule_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	const gchar         *name = g_object_get_data (G_OBJECT (chip), "lrg-tiles-rule");

	if (g_strcmp0 (name, self->rule_tile) == 0)
		lrg_editor_tiles_panel_set_rule_tile (self, NULL);
	else
	{
		lrg_editor_tiles_panel_set_rule_tile (self, name);
		if (self->target != NULL)
			lrg_editor_view_state_set_tile_mode (self->view_state, TRUE);
	}
}

static void
on_refresh_rules (LrgWidget *chip,
                  gpointer   user_data)
{
	run_action (LRG_EDITOR_TILES_PANEL (user_data), "tilemap.refresh-rules");
}

/* Opens the target's .rtileset in its editor tab. */
static void
on_edit_tileset (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *abs = NULL;

	if (self->app == NULL || self->target == NULL || lrg_node_get_visual (self->target) == NULL)
		return;
	abs = editor_app_project_path (self->app, lrg_node_visual_get_asset (lrg_node_get_visual (self->target)));
	if (lrg_editor_tileset_editor_open (self->app, abs, &error) == NULL)
		editor_app_notify (self->app, 2, "Cannot open the tileset: %s", error->message);
}

/* Renames the current layer to the name field's text. */
static void
rename_current_layer (LrgEditorTilesPanel *self)
{
	const gchar *text = lrg_text_input_get_text (self->layer_name);

	if (text == NULL || *text == '\0' || painter (self) == NULL)
		return;
	if (lrg_editor_tiles_panel_rename_layer (self, lrg_editor_tile_painter_get_layer (painter (self)), text))
		lrg_text_input_set_text (self->layer_name, "");
}

static void on_rename_layer (LrgWidget *c, gpointer d) { rename_current_layer (LRG_EDITOR_TILES_PANEL (d)); }

static void
on_layer_name_submitted (LrgTextInput *input,
                         gpointer      user_data)
{
	rename_current_layer (LRG_EDITOR_TILES_PANEL (user_data));
}

/* Connected swapped: the panel comes first, the emitter is ignored */
static void
on_editor_changed (LrgEditorTilesPanel *self)
{
	self->dirty = TRUE;
}

/* The action the window binds as "editor." + the registry id */
#define FLIP_Y_ACTION "editor.tilemap.flip-y"

/* Tried before tool.toggle-snap, which the window binds at priority 0 */
#define FLIP_Y_PRIORITY (10)

/* chord_equal: two accelerator spellings of the same key and modifiers */
static gboolean
chord_equal (const gchar *a,
             const gchar *b)
{
	GrlKey            ka;
	GrlKey            kb;
	LrgInputModifiers ma;
	LrgInputModifiers mb;

	if (a == NULL || b == NULL)
		return a == b;
	if (!lrg_accel_parse (a, &ka, &ma) || !lrg_accel_parse (b, &kb, &mb))
		return g_strcmp0 (a, b) == 0;
	return ka == kb && ma == mb;
}

/* list_has: @action is one of the names in a NULL-terminated list */
static gboolean
list_has (gchar       **names,
          const gchar  *action)
{
	guint i;

	if (names == NULL)
		return FALSE;
	for (i = 0; names[i] != NULL; i++)
		if (g_strcmp0 (names[i], action) == 0)
			return TRUE;
	return FALSE;
}

/*
 * tiles_ensure_flip_shortcut:
 *
 * Puts editor.tilemap.flip-y back on every chord the registry currently
 * gives it, at FLIP_Y_PRIORITY, without disturbing another action that
 * already shares the key. Chords this panel added and the user has since
 * unbound are removed. The window's shortcut sync uses
 * lrg_canvas_add_shortcut(), which replaces the whole chord, so this runs
 * from an idle (the run loop iterates the main context before the frame
 * that reads keys) and again from step.
 */
static void
tiles_ensure_flip_shortcut (LrgEditorTilesPanel *self)
{
	LrgCanvas        *canvas;
	LrgEditorActions *actions;
	g_auto(GStrv)     accels = NULL;
	guint             i;

	if (self->app == NULL)
		return;
	canvas = lrg_editor_app_get_canvas (self->app);
	actions = lrg_editor_app_get_actions (self->app);
	if (canvas == NULL || actions == NULL)
		return;

	accels = lrg_editor_actions_get_accels (actions, "tilemap.flip-y");

	if (self->flip_accels != NULL)
	{
		for (i = 0; self->flip_accels[i] != NULL; i++)
		{
			gboolean still = FALSE;
			guint    j;

			if (accels != NULL)
			{
				for (j = 0; accels[j] != NULL; j++)
					if (chord_equal (self->flip_accels[i], accels[j]))
						still = TRUE;
			}
			if (!still)
				lrg_canvas_remove_shortcut_for_action (canvas, self->flip_accels[i],
				                                       FLIP_Y_ACTION, NULL);
		}
	}

	if (accels != NULL)
	{
		for (i = 0; accels[i] != NULL; i++)
		{
			g_auto(GStrv) bound = NULL;

			if (accels[i][0] == '\0')
				continue;
			bound = lrg_canvas_list_shortcut_actions (canvas, accels[i]);
			if (!list_has (bound, FLIP_Y_ACTION))
				lrg_canvas_add_shortcut_full (canvas, accels[i], FLIP_Y_ACTION, NULL,
				                              FLIP_Y_PRIORITY);
		}
	}

	g_strfreev (self->flip_accels);
	self->flip_accels = g_strdupv (accels);
}

/* ==========================================================================
 * Widget
 * ========================================================================== */

static gboolean
lrg_editor_tiles_panel_step (LrgWidget *widget,
                             gfloat     delta_time)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (widget);
	LrgNode             *node = (self->app != NULL) ? editor_tiles_target (self->app) : NULL;
	LrgEditorTileTool    tool = lrg_editor_view_state_get_tile_tool (self->view_state);

	/* The window sync stacks shared keys. This puts flip-y back if a
	 * rebind replaced the chord. A permanent idle cannot do it: the
	 * frame loop drains the main context until it is empty, and an
	 * idle that stays ready never lets a frame run. */
	tiles_ensure_flip_shortcut (self);

	/* Cheap checks for state other code changes without a signal */
	if (node != self->target ||
	    (node != NULL && (editor_chip_get_checked (self->paint) != lrg_editor_view_state_get_tile_mode (self->view_state) ||
	                      !editor_chip_get_checked (self->tools[MIN ((guint) tool, 4u)]) ||
	                      (painter (self) != NULL &&
	                       (lrg_spin_box_get_value (self->tile) !=
	                        lrg_tile_get_id (lrg_editor_tile_painter_get_tile (painter (self))) ||
	                        lrg_spin_box_get_value (self->brush) !=
	                        lrg_editor_tile_painter_get_brush_size (painter (self)))))))
		self->dirty = TRUE;
	/* The .rtileset changed on disk (the tileset editor saved it) */
	if (node != NULL && !self->dirty)
	{
		LrgTileset       *ts = panel_tileset (self);
		g_autofree gchar *sig = g_strdup_printf ("%p|%s|", (gpointer) ts, self->rule_tile != NULL ? self->rule_tile : "");

		if (self->rules_sig == NULL || !g_str_has_prefix (self->rules_sig, sig))
			self->dirty = TRUE;
	}
	if (self->dirty)
		sync_widgets (self);
	return FALSE;
}

static void
lrg_editor_tiles_panel_layout_children (LrgContainer *container)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (container);

	/* Chip rows drop their labels when the panel is narrow (the scroll
	 * box keeps room for its scroll bar) */
	editor_panel_fit_toolbar (self->tools_row, lrg_widget_get_width (LRG_WIDGET (self)) - 28.0f);
	editor_panel_fit_toolbar (self->layer_row, lrg_widget_get_width (LRG_WIDGET (self)) - 28.0f);
	editor_panel_place_column (container, NULL, self->scroll, NULL);
	editor_panel_place_column (container, NULL, self->hint, NULL);
}

static void
lrg_editor_tiles_panel_dispose (GObject *object)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (object);

	if (self->gesture && self->editor != NULL)
		lrg_editor_end_group (self->editor);
	gesture_clear (self);
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	if (self->editor != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->editor, self);
		g_signal_handlers_disconnect_by_data (lrg_editor_get_selection (self->editor), self);
	}
	g_clear_object (&self->editor);
	g_clear_object (&self->view_state);
	g_clear_object (&self->palette_tex);
	g_clear_weak_pointer (&self->target);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_tiles_panel_parent_class)->dispose (object);
}

static void
lrg_editor_tiles_panel_finalize (GObject *object)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (object);

	g_free (self->target_guid);
	g_strfreev (self->flip_accels);
	g_free (self->layers_sig);
	g_free (self->rules_sig);
	g_free (self->palette_path);
	g_free (self->rule_tile);
	G_OBJECT_CLASS (lrg_editor_tiles_panel_parent_class)->finalize (object);
}

static void
lrg_editor_tiles_panel_class_init (LrgEditorTilesPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_tiles_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_tiles_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_tiles_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_tiles_panel_layout_children;
}

/* A muted section title */
static LrgWidget *
add_heading (LrgWidget   *box,
             const gchar *text)
{
	LrgLabel       *label = lrg_label_new (text);
	const LrgInsets ins = { 0.0f, 6.0f, 0.0f, 0.0f };

	lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	lrg_widget_set_margin (LRG_WIDGET (label), &ins);
	add_to (box, LRG_WIDGET (label));
	return LRG_WIDGET (label);
}

static LrgWidget *
hrow (void)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	return row;
}

static LrgSpinBox *
add_spin (LrgEditorTilesPanel *self,
          LrgWidget           *row,
          const gchar         *label,
          const gchar         *name,
          gdouble              min,
          gdouble              max,
          GCallback            callback)
{
	LrgLabel   *l = lrg_label_new (label);
	LrgSpinBox *spin = lrg_spin_box_new_with_range (min, max, 1.0);

	lrg_widget_set_valign (LRG_WIDGET (l), LRG_ALIGN_CENTER);
	add_to (row, LRG_WIDGET (l));
	lrg_spin_box_set_digits (spin, 0);
	lrg_widget_set_name (LRG_WIDGET (spin), name);
	lrg_widget_set_fill_weight (LRG_WIDGET (spin), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (spin), 60.0f, 0.0f);
	g_signal_connect (spin, "value-changed", callback, self);
	add_to (row, LRG_WIDGET (spin));
	return spin;
}

static LrgComboBox *
add_combo (LrgEditorTilesPanel *self,
           LrgWidget           *row,
           const gchar         *name,
           const gchar * const *items,
           guint                n_items)
{
	LrgComboBox *combo = lrg_combo_box_new ();
	guint        i;

	for (i = 0; i < n_items; i++)
		lrg_combo_box_append (combo, items[i]);
	lrg_combo_box_set_selected (combo, 0);
	lrg_widget_set_name (LRG_WIDGET (combo), name);
	lrg_widget_set_fill_weight (LRG_WIDGET (combo), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (combo), 80.0f, 0.0f);
	g_signal_connect (combo, "changed", G_CALLBACK (on_layout_combo), self);
	add_to (row, LRG_WIDGET (combo));
	return combo;
}

static void
lrg_editor_tiles_panel_init (LrgEditorTilesPanel *self)
{
	static const LrgCustomWidgetFuncs palette_funcs = { palette_measure, palette_paint, NULL, palette_event, { NULL } };
	static const LrgCustomWidgetFuncs tileset_funcs = { NULL, tileset_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs info_funcs = { info_measure, info_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs preview_funcs = { NULL, preview_paint, NULL, NULL, { NULL } };
	static const gchar * const        types[] = { LRG_EDITOR_TILES_ASSET_TYPE, NULL };
	static const gchar * const        axes[] = { "Stagger X", "Stagger Y" };
	static const gchar * const        indices[] = { "Odd", "Even" };
	LrgWidget                        *row, *chip;
	guint                             i;

	self->dirty = TRUE;

	self->hint = editor_panel_hint_new ("tiles-empty", "Select a tilemap node to paint it (Create > Tilemap)");
	lrg_container_add_child (LRG_CONTAINER (self), self->hint);
	g_object_unref (self->hint);

	self->content = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->content, "tiles-content");
	lrg_container_set_spacing (LRG_CONTAINER (self->content), 6.0f);

	/* Title and paint mode */
	row = hrow ();
	self->title = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->title, "tiles-node");
	lrg_widget_set_valign (self->title, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (self->title, 1.0f);
	add_to (row, self->title);
	self->paint = editor_chip_new ("tiles-paint", LRG_ICON_EDIT, "Paint (B)", on_paint, self);
	editor_chip_set_tint (self->paint, LRG_THEME_COLOR_SUCCESS);
	add_to (row, self->paint);
	add_to (self->content, row);

	/* Tools */
	add_heading (self->content, "Tools");
	row = hrow ();
	for (i = 0; i < G_N_ELEMENTS (self->tools); i++)
	{
		g_autofree gchar *name = g_strdup_printf ("tiles-tool-%s", lrg_editor_tile_tool_to_string ((LrgEditorTileTool) i));

		self->tools[i] = editor_chip_new (name, tool_icons[i], tool_labels[i], on_tool, self);
		if (i == LRG_EDITOR_TILE_TOOL_ERASER)
			editor_chip_set_tint (self->tools[i], LRG_THEME_COLOR_DANGER);
		add_to (row, self->tools[i]);
	}
	self->tools_row = row;
	add_to (self->content, row);
	row = hrow ();
	self->brush = add_spin (self, row, "Brush", "tiles-brush", 1.0, 16.0, G_CALLBACK (on_brush));
	self->tile = add_spin (self, row, "Tile", "tiles-tile", 0.0, 65535.0, G_CALLBACK (on_tile));
	add_to (self->content, row);

	/* Brush transform (X / Y / Z while painting) with a preview */
	row = hrow ();
	self->preview = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->preview, "tiles-brush-preview");
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->preview), 30.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->preview), 30.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->preview), &preview_funcs, self, NULL);
	add_to (row, self->preview);
	self->flip_x = editor_chip_new ("tiles-flip-x", LRG_ICON_ARROW_RIGHT, "Flip X", on_flip_x, self);
	add_to (row, self->flip_x);
	self->flip_y = editor_chip_new ("tiles-flip-y", LRG_ICON_CHEVRON_DOWN, "Flip Y", on_flip_y, self);
	add_to (row, self->flip_y);
	self->rotate = editor_chip_new ("tiles-rotate", LRG_ICON_REFRESH, "0°", on_rotate, self);
	add_to (row, self->rotate);
	self->xform_row = row;
	add_to (self->content, row);

	/* Grid */
	add_heading (self->content, "Grid");
	self->create_grid = editor_chip_new ("tiles-create-grid", LRG_ICON_PLUS, "Create Grid (16 x 16)", on_create_grid,
	                                     self);
	lrg_widget_set_halign (self->create_grid, LRG_ALIGN_START);
	add_to (self->content, self->create_grid);
	self->grid_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (self->grid_box), 4.0f);
	row = hrow ();
	self->grid_w = add_spin (self, row, "Size", "tiles-width", 1.0, LRG_NODE_TILEMAP_MAX_SIZE, G_CALLBACK (on_grid_changed));
	self->grid_h = add_spin (self, row, "x", "tiles-height", 1.0, LRG_NODE_TILEMAP_MAX_SIZE,
	                         G_CALLBACK (on_grid_changed));
	add_to (self->grid_box, row);
	row = hrow ();
	self->tile_w = add_spin (self, row, "Tile px", "tiles-tile-width", 1.0, 1024.0, G_CALLBACK (on_grid_changed));
	self->tile_h = add_spin (self, row, "x", "tiles-tile-height", 1.0, 1024.0, G_CALLBACK (on_grid_changed));
	add_to (self->grid_box, row);
	row = hrow ();
	{
		LrgLabel *l = lrg_label_new ("Layout");

		lrg_widget_set_valign (LRG_WIDGET (l), LRG_ALIGN_CENTER);
		add_to (row, LRG_WIDGET (l));
	}
	self->orientation = add_combo (self, row, "tiles-orientation", orientation_labels, G_N_ELEMENTS (orientation_labels));
	add_to (self->grid_box, row);
	self->stagger_row = hrow ();
	lrg_widget_set_name (self->stagger_row, "tiles-stagger-row");
	self->stagger_axis = add_combo (self, self->stagger_row, "tiles-stagger-axis", axes, 2);
	self->stagger_index = add_combo (self, self->stagger_row, "tiles-stagger-index", indices, 2);
	add_to (self->grid_box, self->stagger_row);
	self->cell_row = hrow ();
	lrg_widget_set_name (self->cell_row, "tiles-cell-row");
	self->cell_w = add_spin (self, self->cell_row, "Cell px", "tiles-cell-width", 0.0, 1024.0, G_CALLBACK (on_layout_spin));
	self->cell_h = add_spin (self, self->cell_row, "x", "tiles-cell-height", 0.0, 1024.0, G_CALLBACK (on_layout_spin));
	add_to (self->grid_box, self->cell_row);
	self->hex_row = hrow ();
	lrg_widget_set_name (self->hex_row, "tiles-hex-row");
	self->hex_side = add_spin (self, self->hex_row, "Hex side px", "tiles-hex-side", 0.0, 1024.0, G_CALLBACK (on_layout_spin));
	add_to (self->grid_box, self->hex_row);
	add_to (self->content, self->grid_box);

	/* Tileset drop zone */
	add_heading (self->content, "Tileset");
	self->tileset = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->tileset, "tiles-tileset");
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->tileset), 34.0f);
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->tileset), 160.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tileset), &tileset_funcs, self, NULL);
	{
		LrgDropTarget *drop = lrg_drop_target_new (types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);

		lrg_drop_target_set_paint_highlight (drop, FALSE);
		g_signal_connect (drop, "drop", G_CALLBACK (on_tileset_drop), self);
		lrg_widget_add_drop_target (self->tileset, drop);   /* the widget owns it */
		self->drop = drop;
	}
	add_to (self->content, self->tileset);

	/* Layers */
	add_heading (self->content, "Layers");
	self->layers = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->layers, "tiles-layers");
	lrg_container_set_spacing (LRG_CONTAINER (self->layers), 3.0f);
	add_to (self->content, self->layers);
	row = hrow ();
	add_to (row, editor_chip_new ("tiles-add-layer", LRG_ICON_PLUS, "Layer", on_add_layer, self));
	chip = editor_chip_new ("tiles-remove-layer", LRG_ICON_MINUS, "Layer", on_remove_layer, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (row, chip);
	self->layer_name = lrg_text_input_new_with_placeholder ("New layer name");
	lrg_widget_set_name (LRG_WIDGET (self->layer_name), "tiles-layer-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->layer_name), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (self->layer_name), 80.0f, 0.0f);
	g_signal_connect (self->layer_name, "submitted", G_CALLBACK (on_layer_name_submitted), self);
	add_to (row, LRG_WIDGET (self->layer_name));
	add_to (row, editor_chip_new ("tiles-rename-layer", LRG_ICON_EDIT, "Rename", on_rename_layer, self));
	self->layer_row = row;
	add_to (self->content, row);

	/* Rule tiles (an .rtileset only) */
	self->rules_heading = add_heading (self->content, "Rule tiles");
	self->rules = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->rules, "tiles-rules");
	lrg_container_set_spacing (LRG_CONTAINER (self->rules), 3.0f);
	add_to (self->content, self->rules);

	/* Palette and the properties of the hovered tile */
	add_heading (self->content, "Palette");
	self->palette = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->palette, "tiles-palette");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->palette), &palette_funcs, self, NULL);
	add_to (self->content, self->palette);
	self->info = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->info, "tiles-tile-info");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->info), &info_funcs, self, NULL);
	add_to (self->content, self->info);

	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "tiles-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), self->content);
	g_object_unref (self->content);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_rename_layer (gpointer data)
{
	rename_current_layer (LRG_EDITOR_TILES_PANEL (data));
}

static gboolean
has_grid (gpointer data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (data);

	return self->app != NULL && target_grid (editor_tiles_target (self->app)) != NULL;
}

/* The transform keys act while painting a tilemap */
static gboolean
painting (gpointer data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (data);

	return self->app != NULL && lrg_editor_view_state_get_tile_mode (self->view_state) &&
	       editor_tiles_target (self->app) != NULL;
}

static gboolean
has_rules (gpointer data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (data);
	LrgTileset          *ts = (self->app != NULL) ? node_tileset (self->app, editor_tiles_target (self->app)) : NULL;

	return ts != NULL && lrg_tileset_get_n_rule_tiles (ts) > 0;
}

static void a_flip_x (gpointer d)      { lrg_editor_tiles_panel_flip_brush (LRG_EDITOR_TILES_PANEL (d), FALSE); }
static void a_flip_y (gpointer d)      { lrg_editor_tiles_panel_flip_brush (LRG_EDITOR_TILES_PANEL (d), TRUE); }
static void a_rotate (gpointer d)      { lrg_editor_tiles_panel_rotate_brush (LRG_EDITOR_TILES_PANEL (d), 1); }
static void a_rotate_back (gpointer d) { lrg_editor_tiles_panel_rotate_brush (LRG_EDITOR_TILES_PANEL (d), -1); }
static void a_reset_xform (gpointer d) { lrg_editor_tiles_panel_set_brush_transform (LRG_EDITOR_TILES_PANEL (d), FALSE, 0); }

static void
a_refresh_rules (gpointer data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (data);
	guint                n = lrg_editor_tiles_panel_refresh_rule_tiles (self, -1);

	if (self->app != NULL)
		editor_app_set_status (self->app, "Refreshed rule tiles: %u cell(s) changed", n);
}

/* ==========================================================================
 * IPC: tiles.*
 * ========================================================================== */

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *node = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (node, o);
	return node;
}

static void
invalid (GError     **error,
         const gchar *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, message);
}

static void
not_found (GError     **error,
           const gchar *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, message);
}

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n = (params != NULL) ? json_object_get_member (params, name) : NULL;

	return (n != NULL && JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

static gboolean
param_number (JsonObject  *params,
              const gchar *name,
              gdouble     *out)
{
	JsonNode *n = (params != NULL) ? json_object_get_member (params, name) : NULL;

	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	switch (json_node_get_value_type (n))
	{
	case G_TYPE_INT64:   *out = (gdouble) json_node_get_int (n); return TRUE;
	case G_TYPE_DOUBLE:  *out = json_node_get_double (n); return TRUE;
	case G_TYPE_BOOLEAN: *out = json_node_get_boolean (n) ? 1.0 : 0.0; return TRUE;
	default:             return FALSE;
	}
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name,
            gboolean     fallback)
{
	gdouble v;

	return param_number (params, name, &v) ? v != 0.0 : fallback;
}

/* The "node" param, else the tiles target; a tilemap node either way. */
static LrgNode *
ipc_node (LrgEditorTilesPanel  *self,
          JsonObject           *params,
          GError              **error)
{
	const gchar *ref = param_string (params, "node");
	LrgNode     *node = (ref != NULL) ? lrg_editor_find_node (self->editor, ref)
	                                  : (self->app != NULL ? editor_tiles_target (self->app) : NULL);

	if (node == NULL)
	{
		not_found (error, ref != NULL ? "No such node" : "No tilemap node selected (pass \"node\")");
		return NULL;
	}
	if (!is_tilemap_node (node))
	{
		not_found (error, "The node is not a tilemap node");
		return NULL;
	}
	return node;
}

static JsonArray *
pair_array (gdouble a,
            gdouble b)
{
	JsonArray *arr = json_array_new ();

	json_array_add_double_element (arr, a);
	json_array_add_double_element (arr, b);
	return arr;
}

/* Properties of tile @tile (1-based) of @ts as JSON. */
static JsonObject *
tile_info_json (LrgTileset *ts,
                guint       tile)
{
	JsonObject   *o = json_object_new ();
	JsonArray    *flags = json_array_new ();
	JsonArray    *coll = json_array_new ();
	JsonObject   *data = json_object_new ();
	JsonArray    *anim = json_array_new ();
	g_auto(GStrv) keys = NULL;
	guint         p, k, n, frame, ms;
	LrgRuleTile  *owner;

	json_object_set_int_member (o, "tile", tile);
	json_object_set_int_member (o, "index", tile > 0 ? (gint64) tile - 1 : -1);
	if (ts != NULL && tile > 0 && tile <= lrg_tileset_get_tile_count (ts))
	{
		LrgTileProperty props = lrg_tileset_get_tile_properties (ts, tile - 1);

		if (props & LRG_TILE_PROPERTY_SOLID)
			json_array_add_string_element (flags, "solid");
		if (props & LRG_TILE_PROPERTY_ANIMATED)
			json_array_add_string_element (flags, "animated");
		if (props & LRG_TILE_PROPERTY_HAZARD)
			json_array_add_string_element (flags, "hazard");
		for (p = 0; p < lrg_tileset_get_tile_collision_polygon_count (ts, tile - 1); p++)
		{
			const gfloat *pts = lrg_tileset_get_tile_collision_polygon (ts, tile - 1, p, &n);
			JsonArray    *poly = json_array_new ();

			for (k = 0; k < n; k++)
				json_array_add_double_element (poly, pts[k]);
			json_array_add_array_element (coll, poly);
		}
		keys = lrg_tileset_get_tile_data_keys (ts, tile - 1);
		for (k = 0; keys != NULL && keys[k] != NULL; k++)
			json_object_set_string_member (data, keys[k], lrg_tileset_get_tile_data (ts, tile - 1, keys[k]));
		for (k = 0; k < lrg_tileset_get_tile_animation_length (ts, tile - 1); k++)
			if (lrg_tileset_get_tile_animation_frame (ts, tile - 1, k, &frame, &ms))
			{
				JsonObject *f = json_object_new ();

				json_object_set_int_member (f, "tile", frame);
				json_object_set_int_member (f, "duration", ms);
				json_array_add_object_element (anim, f);
			}
	}
	json_object_set_array_member (o, "flags", flags);
	json_object_set_array_member (o, "collision", coll);
	json_object_set_object_member (o, "data", data);
	json_object_set_array_member (o, "animation", anim);
	owner = (ts != NULL && tile > 0) ? lrg_tileset_find_rule_tile_for (ts, tile) : NULL;
	if (owner != NULL)
		json_object_set_string_member (o, "rule_tile", lrg_rule_tile_get_name (owner));
	else
		json_object_set_null_member (o, "rule_tile");
	return o;
}

static JsonObject *
brush_json (LrgEditorTilesPanel *self)
{
	JsonObject *o = json_object_new ();
	guint       value = lrg_editor_tiles_panel_get_brush_value (self);

	json_object_set_int_member (o, "tile", value);
	json_object_set_int_member (o, "id", lrg_tile_get_id (value));
	json_object_set_boolean_member (o, "mirrored", lrg_tile_get_mirrored (value));
	json_object_set_int_member (o, "rotation", lrg_tile_get_rotation (value));
	if (self->rule_tile != NULL)
		json_object_set_string_member (o, "rule_tile", self->rule_tile);
	else
		json_object_set_null_member (o, "rule_tile");
	return o;
}

static const gchar *
layout_nick (LrgTileNeighborLayout layout)
{
	GEnumClass *klass = g_type_class_ref (LRG_TYPE_TILE_NEIGHBOR_LAYOUT);
	GEnumValue *v = g_enum_get_value (klass, (gint) layout);
	const gchar *nick = (v != NULL) ? v->value_nick : "square";

	g_type_class_unref (klass);
	return nick;
}

static JsonObject *
node_json (LrgEditorTilesPanel *self,
           LrgNode             *node)
{
	JsonObject     *o = json_object_new ();
	LrgNodeTilemap *grid = target_grid (node);
	LrgTileset     *ts = node_tileset (self->app, node);
	const gchar    *asset = lrg_node_visual_get_asset (lrg_node_get_visual (node));
	JsonArray      *rules = json_array_new ();
	guint           i;

	json_object_set_string_member (o, "node", lrg_node_get_name (node));
	json_object_set_string_member (o, "tileset", asset != NULL ? asset : "");
	json_object_set_string_member (o, "tileset_kind", (asset == NULL || *asset == '\0') ? "none"
	                                                  : is_rtileset_path (asset) ? "rtileset" : "image");
	if (grid != NULL)
	{
		JsonObject            *g = json_object_new ();
		LrgTilemapOrientation  orientation;
		LrgTilemapStaggerAxis  axis;
		LrgTilemapStaggerIndex index;
		guint                  hex, cw, ch;

		lrg_node_tilemap_get_grid (grid, &orientation, &axis, &index, &hex, &cw, &ch);
		json_object_set_array_member (o, "size", pair_array (lrg_node_tilemap_get_width (grid),
		                                                     lrg_node_tilemap_get_height (grid)));
		json_object_set_array_member (o, "tile_size", pair_array (lrg_node_tilemap_get_tile_width (grid),
		                                                          lrg_node_tilemap_get_tile_height (grid)));
		json_object_set_string_member (g, "orientation", orientation_nicks[MIN ((guint) orientation, 3u)]);
		json_object_set_string_member (g, "stagger_axis", axis == LRG_TILEMAP_STAGGER_AXIS_X ? "x" : "y");
		json_object_set_string_member (g, "stagger_index", index == LRG_TILEMAP_STAGGER_INDEX_ODD ? "odd" : "even");
		json_object_set_int_member (g, "hex_side", hex);
		json_object_set_array_member (g, "cell_size", pair_array (cw, ch));
		json_object_set_object_member (o, "grid", g);
		json_object_set_string_member (o, "layout", layout_nick (node_layout (node)));
		json_object_set_int_member (o, "layers", lrg_node_tilemap_get_n_layers (grid));
	}
	else
		json_object_set_null_member (o, "grid");
	json_object_set_object_member (o, "brush", brush_json (self));
	for (i = 0; ts != NULL && i < lrg_tileset_get_n_rule_tiles (ts); i++)
	{
		LrgRuleTile *rt = lrg_tileset_get_rule_tile (ts, i);
		JsonObject  *r = json_object_new ();

		json_object_set_string_member (r, "name", lrg_rule_tile_get_name (rt));
		json_object_set_int_member (r, "default", rule_seed_value (rt));
		json_object_set_int_member (r, "n_rules", lrg_rule_tile_get_n_rules (rt));
		json_array_add_object_element (rules, r);
	}
	json_object_set_array_member (o, "rule_tiles", rules);
	json_object_set_object_member (o, "tile", tile_info_json (ts, lrg_tile_get_id (lrg_editor_tiles_panel_get_brush_value (self))));
	return o;
}

static LrgEditorTilesPanel *
ipc_panel (gpointer user_data)
{
	return LRG_EDITOR_TILES_PANEL (user_data);
}

static JsonNode *
m_tiles_get (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorTilesPanel *self = ipc_panel (user_data);
	LrgNode             *node = ipc_node (self, params, error);

	return node != NULL ? object_node (node_json (self, node)) : NULL;
}

/* An enum value from its nick among @nicks, or -1 */
static gint
nick_index (const gchar         *nick,
            const gchar * const *nicks,
            guint                n)
{
	guint i;

	for (i = 0; nick != NULL && i < n; i++)
		if (g_ascii_strcasecmp (nick, nicks[i]) == 0)
			return (gint) i;
	return -1;
}

static JsonNode *
m_tiles_grid (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	static const gchar * const axes[] = { "x", "y" };
	static const gchar * const indices[] = { "odd", "even" };
	LrgEditorTilesPanel       *self = ipc_panel (user_data);
	LrgNode                   *node = ipc_node (self, params, error);
	LrgNodeTilemap            *grid;
	LrgTilemapOrientation      o;
	LrgTilemapStaggerAxis      axis;
	LrgTilemapStaggerIndex     index;
	guint                      hex, cw, ch;
	g_autoptr(LrgNode)         before = NULL;
	const gchar               *s;
	gdouble                    v;

	if (node == NULL)
		return NULL;
	if ((grid = target_grid (node)) == NULL)
	{
		not_found (error, "The tilemap node has no grid yet (tilemap.configure)");
		return NULL;
	}
	lrg_node_tilemap_get_grid (grid, &o, &axis, &index, &hex, &cw, &ch);
	if ((s = param_string (params, "orientation")) != NULL)
	{
		gint i = nick_index (s, orientation_nicks, G_N_ELEMENTS (orientation_nicks));

		if (i < 0)
		{
			invalid (error, "orientation: expected orthogonal, isometric, staggered or hexagonal");
			return NULL;
		}
		o = (LrgTilemapOrientation) i;
	}
	if ((s = param_string (params, "stagger_axis")) != NULL)
	{
		gint i = nick_index (s, axes, 2);

		if (i < 0)
		{
			invalid (error, "stagger_axis: expected x or y");
			return NULL;
		}
		axis = (LrgTilemapStaggerAxis) i;
	}
	if ((s = param_string (params, "stagger_index")) != NULL)
	{
		gint i = nick_index (s, indices, 2);

		if (i < 0)
		{
			invalid (error, "stagger_index: expected odd or even");
			return NULL;
		}
		index = (LrgTilemapStaggerIndex) i;
	}
	if (param_number (params, "hex_side", &v))
		hex = (guint) MAX (0.0, v);
	if (json_object_has_member (params, "cell_size"))
	{
		JsonArray *a = json_object_get_array_member (params, "cell_size");

		if (a == NULL || json_array_get_length (a) != 2)
		{
			invalid (error, "cell_size: expected [width, height]");
			return NULL;
		}
		cw = (guint) MAX (0.0, json_array_get_double_element (a, 0));
		ch = (guint) MAX (0.0, json_array_get_double_element (a, 1));
	}
	before = lrg_editor_snapshot_node (node);
	lrg_node_tilemap_set_grid (grid, o, axis, index, hex, cw, ch);
	lrg_editor_commit_node_state (self->editor, node, before, "Tilemap Layout", FALSE);
	self->dirty = TRUE;
	return object_node (node_json (self, node));
}

static JsonNode *
m_tiles_brush (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorTilesPanel *self = ipc_panel (user_data);
	gdouble              v;

	if (param_number (params, "tile", &v))
		lrg_editor_tiles_panel_set_brush_tile (self, lrg_tile_get_id ((guint) MAX (0.0, v)));
	if (json_object_has_member (params, "rule_tile"))
	{
		const gchar *name = param_string (params, "rule_tile");

		if (!lrg_editor_tiles_panel_set_rule_tile (self, name))
		{
			not_found (error, "No such rule tile in the target's tileset");
			return NULL;
		}
	}
	if (json_object_has_member (params, "mirrored") || json_object_has_member (params, "rotation"))
	{
		guint value = lrg_editor_tiles_panel_get_brush_value (self);
		gboolean mirrored = param_bool (params, "mirrored", lrg_tile_get_mirrored (value));
		guint    rotation = lrg_tile_get_rotation (value);

		if (param_number (params, "rotation", &v))
			rotation = ((guint) MAX (0.0, v)) % 4u;
		lrg_editor_tiles_panel_set_brush_transform (self, mirrored, rotation);
	}
	if (param_number (params, "rotate", &v))
		lrg_editor_tiles_panel_rotate_brush (self, (gint) v);
	if (param_bool (params, "flip_x", FALSE))
		lrg_editor_tiles_panel_flip_brush (self, FALSE);
	if (param_bool (params, "flip_y", FALSE))
		lrg_editor_tiles_panel_flip_brush (self, TRUE);
	return object_node (brush_json (self));
}

/* Paints @cells on @node with the brush or the "rule_tile" param. */
static JsonNode *
m_tiles_paint (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorTilesPanel *self = ipc_panel (user_data);
	LrgNode             *node = ipc_node (self, params, error);
	JsonArray           *arr;
	g_autofree gint     *cells = NULL;
	guint                n, i, layer, changed;
	LrgNodeTilemap      *grid;
	const gchar         *rule_name;
	gboolean             erase = param_bool (params, "erase", FALSE);
	JsonObject          *o;
	gdouble              v;

	if (node == NULL)
		return NULL;
	arr = json_object_has_member (params, "cells") ? json_object_get_array_member (params, "cells") : NULL;
	if (arr == NULL || json_array_get_length (arr) % 2 != 0)
	{
		invalid (error, "cells: expected [x, y, x, y, ...]");
		return NULL;
	}
	if (painter (self) != NULL)
		lrg_editor_tile_painter_ensure_tilemap (painter (self), node);
	if ((grid = target_grid (node)) == NULL || lrg_node_tilemap_get_n_layers (grid) == 0)
	{
		not_found (error, "The tilemap node has no layers");
		return NULL;
	}
	n = json_array_get_length (arr);
	cells = g_new0 (gint, MAX (1u, n));
	for (i = 0; i < n; i++)
		cells[i] = (gint) json_array_get_int_element (arr, i);
	layer = effective_layer (self, grid);
	if (param_number (params, "layer", &v))
		layer = (guint) MAX (0.0, v);
	if (layer >= lrg_node_tilemap_get_n_layers (grid))
	{
		not_found (error, "No such layer");
		return NULL;
	}
	rule_name = json_object_has_member (params, "rule_tile") ? param_string (params, "rule_tile") : self->rule_tile;
	if (rule_name != NULL)
	{
		LrgRuleTile *rule = node_rule_tile (self->app, node, rule_name);

		if (rule == NULL)
		{
			not_found (error, "No such rule tile in the node's tileset");
			return NULL;
		}
		changed = paint_rule_cells (self->editor, node, layer, rule, cells, n, erase);
	}
	else
		changed = paint_plain_cells (self->editor, node, layer, erase ? 0 : lrg_editor_tiles_panel_get_brush_value (self),
		                             cells, n);
	self->dirty = TRUE;
	o = json_object_new ();
	json_object_set_int_member (o, "changed", changed);
	return object_node (o);
}

static JsonNode *
m_tiles_refresh (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorTilesPanel *self = ipc_panel (user_data);
	LrgNode             *node = ipc_node (self, params, error);
	gint                 layer = -1;
	gdouble              v;
	JsonObject          *o;

	if (node == NULL)
		return NULL;
	if (node_tileset (self->app, node) == NULL)
	{
		not_found (error, "The node's tileset is not an .rtileset");
		return NULL;
	}
	if (param_number (params, "layer", &v))
		layer = (gint) v;
	o = json_object_new ();
	json_object_set_int_member (o, "changed", refresh_node (self, node, layer));
	return object_node (o);
}

static JsonNode *
m_tiles_cell_at (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorTilesPanel *self = ipc_panel (user_data);
	LrgNode             *node = ipc_node (self, params, error);
	JsonArray           *local, *poly;
	JsonObject          *o;
	gint                 x, y;
	gboolean             inside;
	gfloat               pts[12];
	guint                n, i;

	if (node == NULL)
		return NULL;
	local = json_object_has_member (params, "local") ? json_object_get_array_member (params, "local") : NULL;
	if (local == NULL || json_array_get_length (local) < 2)
	{
		invalid (error, "local: expected [x, y] in the node's local plane");
		return NULL;
	}
	if (target_grid (node) == NULL)
	{
		not_found (error, "The tilemap node has no grid yet");
		return NULL;
	}
	inside = lrg_editor_tiles_panel_local_to_cell (node, (gfloat) json_array_get_double_element (local, 0),
	                                               (gfloat) json_array_get_double_element (local, 1), &x, &y);
	o = json_object_new ();
	json_object_set_array_member (o, "cell", pair_array (x, y));
	json_object_set_boolean_member (o, "inside", inside);
	poly = json_array_new ();
	n = lrg_editor_tiles_panel_get_cell_polygon (node, x, y, pts);
	for (i = 0; i < n; i++)
		json_array_add_array_element (poly, pair_array (pts[2 * i], pts[2 * i + 1]));
	json_object_set_array_member (o, "polygon", poly);
	return object_node (o);
}

static JsonNode *
m_tiles_tile_info (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorTilesPanel *self = ipc_panel (user_data);
	LrgNode             *node = ipc_node (self, params, error);
	gdouble              v;

	if (node == NULL)
		return NULL;
	if (!param_number (params, "tile", &v) || v < 1.0)
	{
		invalid (error, "tile: expected a 1-based tile id");
		return NULL;
	}
	return object_node (tile_info_json (node_tileset (self->app, node), lrg_tile_get_id ((guint) v)));
}

static void
register_methods (LrgEditorTilesPanel *self)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "tiles.get", m_tiles_get },
		{ "tiles.grid", m_tiles_grid },
		{ "tiles.brush", m_tiles_brush },
		{ "tiles.paint", m_tiles_paint },
		{ "tiles.refresh", m_tiles_refresh },
		{ "tiles.cell_at", m_tiles_cell_at },
		{ "tiles.tile_info", m_tiles_tile_info }
	};
	LrgEditorServer *server = lrg_editor_app_get_server (self->app);
	guint            i;

	if (server == NULL)
		return;
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, self, NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorTilesPanel *
lrg_editor_tiles_panel_new (LrgEditorApp *app)
{
	LrgEditorTilesPanel *self;
	LrgEditorActions    *actions;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_TILES_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->view_state = g_object_ref (lrg_editor_app_get_view_state (app));
	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (on_editor_changed), self);
	g_signal_connect_swapped (lrg_editor_get_selection (self->editor), "changed", G_CALLBACK (on_editor_changed), self);

	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "tilemap.rename-layer", "Rename Tile Layer", "Tilemap", NULL, a_rename_layer,
	                        has_grid, NULL, self);
	lrg_editor_actions_add (actions, "tilemap.flip-x", "Flip Tile Horizontally", "Tilemap", "X", a_flip_x, painting,
	                        NULL, self);
	/* Y is shared with tool.toggle-snap. The canvas tries the bindings
	 * of a key in priority order and fires the first ENABLED one, and
	 * this one is only enabled while painting tiles, so Y flips the
	 * brush then and toggles snapping everywhere else. Shift+X stays
	 * as an alternative. The window sync stacks both at priority 0;
	 * step() also re-asserts this one at a higher priority. */
	lrg_editor_actions_add (actions, "tilemap.flip-y", "Flip Tile Vertically", "Tilemap", "Y|Shift+X", a_flip_y,
	                        painting, NULL, self);
	lrg_editor_actions_add (actions, "tilemap.rotate", "Rotate Tile Clockwise", "Tilemap", "Z", a_rotate, painting,
	                        NULL, self);
	lrg_editor_actions_add (actions, "tilemap.rotate-back", "Rotate Tile Counter-Clockwise", "Tilemap", NULL,
	                        a_rotate_back, painting, NULL, self);
	lrg_editor_actions_add (actions, "tilemap.reset-transform", "Reset Tile Transform", "Tilemap", NULL,
	                        a_reset_xform, has_grid, NULL, self);
	lrg_editor_actions_add (actions, "tilemap.refresh-rules", "Refresh Rule Tiles", "Tilemap", NULL, a_refresh_rules,
	                        has_rules, NULL, self);
	register_methods (self);
	lrg_editor_tileset_editor_install (app);
	lrg_editor_sprite_editor_install (app);
	sync_widgets (self);
	return self;
}

LrgNode *
lrg_editor_tiles_panel_get_target (LrgEditorTilesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), NULL);
	if (self->dirty || (self->app != NULL && editor_tiles_target (self->app) != self->target))
		sync_widgets (self);
	return self->target;
}

gboolean
lrg_editor_tiles_panel_set_tileset (LrgEditorTilesPanel *self,
                                    const gchar         *path)
{
	g_autoptr(LrgNode) before = NULL;
	g_autofree gchar  *rel = NULL;
	g_autofree gchar  *abs = NULL;
	LrgNode           *node;
	LrgNodeTilemap    *grid;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	node = lrg_editor_tiles_panel_get_target (self);
	if (node == NULL || self->app == NULL || lrg_node_get_visual (node) == NULL)
		return FALSE;
	rel = editor_app_relative_path (self->app, path);
	abs = editor_app_project_path (self->app, path);
	before = lrg_editor_snapshot_node (node);
	lrg_node_visual_set_asset (lrg_node_get_visual (node), rel);
	/* An .rtileset brings its own tile size */
	if (is_rtileset_path (abs) && (grid = target_grid (node)) != NULL)
	{
		LrgTileset *ts = load_tileset (abs);

		if (ts != NULL)
			lrg_node_tilemap_set_tile_size (grid, lrg_tileset_get_tile_width (ts), lrg_tileset_get_tile_height (ts));
	}
	lrg_editor_commit_node_state (self->editor, node, before, "Set Tileset", FALSE);
	editor_app_set_status (self->app, "Tileset: %s", rel);
	g_clear_pointer (&self->rule_tile, g_free);
	self->dirty = TRUE;
	return TRUE;
}

LrgTileset *
lrg_editor_tiles_panel_get_tileset (LrgEditorTilesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), NULL);
	return node_tileset (self->app, lrg_editor_tiles_panel_get_target (self));
}

gboolean
lrg_editor_tiles_panel_rename_layer (LrgEditorTilesPanel *self,
                                     guint                layer,
                                     const gchar         *name)
{
	g_autoptr(LrgNode) before = NULL;
	LrgNode           *node;
	LrgNodeTilemap    *grid;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	node = lrg_editor_tiles_panel_get_target (self);
	grid = target_grid (node);
	if (grid == NULL || layer >= lrg_node_tilemap_get_n_layers (grid) || *name == '\0')
		return FALSE;
	before = lrg_editor_snapshot_node (node);
	lrg_node_tilemap_set_layer_name (grid, layer, name);
	lrg_editor_commit_node_state (self->editor, node, before, "Rename Tile Layer", FALSE);
	self->dirty = TRUE;
	return TRUE;
}

gboolean
lrg_editor_tiles_panel_set_layer_visible (LrgEditorTilesPanel *self,
                                          guint                layer,
                                          gboolean             visible)
{
	g_autoptr(LrgNode) before = NULL;
	LrgNode           *node;
	LrgNodeTilemap    *grid;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), FALSE);

	node = lrg_editor_tiles_panel_get_target (self);
	grid = target_grid (node);
	if (grid == NULL || layer >= lrg_node_tilemap_get_n_layers (grid))
		return FALSE;
	if (lrg_node_tilemap_get_layer_visible (grid, layer) == !!visible)
		return TRUE;
	before = lrg_editor_snapshot_node (node);
	lrg_node_tilemap_set_layer_visible (grid, layer, visible);
	lrg_editor_commit_node_state (self->editor, node, before, "Toggle Tile Layer", FALSE);
	self->dirty = TRUE;
	return TRUE;
}

void
lrg_editor_tiles_panel_get_palette_size (LrgEditorTilesPanel *self,
                                         guint               *columns,
                                         guint               *rows)
{
	guint c, r;

	g_return_if_fail (LRG_IS_EDITOR_TILES_PANEL (self));

	lrg_editor_tiles_panel_get_target (self);
	palette_load (self);
	palette_geometry (self, &c, &r);
	if (columns != NULL)
		*columns = c;
	if (rows != NULL)
		*rows = r;
}

gboolean
lrg_editor_tiles_panel_set_grid (LrgEditorTilesPanel    *self,
                                 LrgTilemapOrientation   orientation,
                                 LrgTilemapStaggerAxis   stagger_axis,
                                 LrgTilemapStaggerIndex  stagger_index,
                                 guint                   hex_side_length,
                                 guint                   cell_width,
                                 guint                   cell_height)
{
	g_autoptr(LrgNode)     before = NULL;
	LrgNode               *node;
	LrgNodeTilemap        *grid;
	LrgTilemapOrientation  o;
	LrgTilemapStaggerAxis  axis;
	LrgTilemapStaggerIndex index;
	guint                  hex, cw, ch;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), FALSE);
	g_return_val_if_fail (orientation <= LRG_TILEMAP_ORIENTATION_HEXAGONAL, FALSE);

	node = lrg_editor_tiles_panel_get_target (self);
	if ((grid = target_grid (node)) == NULL)
		return FALSE;
	lrg_node_tilemap_get_grid (grid, &o, &axis, &index, &hex, &cw, &ch);
	if (o == orientation && axis == stagger_axis && index == stagger_index && hex == hex_side_length &&
	    cw == cell_width && ch == cell_height)
		return TRUE;
	before = lrg_editor_snapshot_node (node);
	lrg_node_tilemap_set_grid (grid, orientation, stagger_axis, stagger_index, hex_side_length, cell_width,
	                           cell_height);
	lrg_editor_commit_node_state (self->editor, node, before, "Tilemap Layout", FALSE);
	self->dirty = TRUE;
	return TRUE;
}

guint
lrg_editor_tiles_panel_get_brush_value (LrgEditorTilesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), 0);
	return painter (self) != NULL ? lrg_editor_tile_painter_get_tile (painter (self)) : 0;
}

/* Writes a new brush value into the painter. */
static void
set_brush_value (LrgEditorTilesPanel *self,
                 guint                value)
{
	if (painter (self) == NULL)
		return;
	lrg_editor_tile_painter_set_tile (painter (self), value);
	self->dirty = TRUE;
	if (self->preview != NULL)
		lrg_widget_queue_layout (self->preview);
}

void
lrg_editor_tiles_panel_set_brush_tile (LrgEditorTilesPanel *self,
                                       guint                tile_id)
{
	guint             value;
	LrgEditorTileTool tool;

	g_return_if_fail (LRG_IS_EDITOR_TILES_PANEL (self));

	value = lrg_editor_tiles_panel_get_brush_value (self);
	g_clear_pointer (&self->rule_tile, g_free);
	tile_id = lrg_tile_get_id (tile_id);
	set_brush_value (self, tile_id == 0 ? 0 : lrg_tile_make (tile_id, lrg_tile_get_mirrored (value),
	                                                        lrg_tile_get_rotation (value)));
	/* Picking a tile paints with it: eraser / eyedropper switch to brush */
	tool = lrg_editor_view_state_get_tile_tool (self->view_state);
	if (tile_id != 0 && (tool == LRG_EDITOR_TILE_TOOL_ERASER || tool == LRG_EDITOR_TILE_TOOL_PICK))
		lrg_editor_view_state_set_tile_tool (self->view_state, LRG_EDITOR_TILE_TOOL_BRUSH);
}

void
lrg_editor_tiles_panel_set_brush_transform (LrgEditorTilesPanel *self,
                                            gboolean             mirrored,
                                            guint                rotation)
{
	guint id;

	g_return_if_fail (LRG_IS_EDITOR_TILES_PANEL (self));

	id = lrg_tile_get_id (lrg_editor_tiles_panel_get_brush_value (self));
	if (id != 0)
		set_brush_value (self, lrg_tile_make (id, mirrored, rotation % 4u));
}

void
lrg_editor_tiles_panel_flip_brush (LrgEditorTilesPanel *self,
                                   gboolean             vertical)
{
	guint value;

	g_return_if_fail (LRG_IS_EDITOR_TILES_PANEL (self));

	value = lrg_editor_tiles_panel_get_brush_value (self);
	if (lrg_tile_get_id (value) == 0)
		return;
	set_brush_value (self, vertical ? lrg_tile_flip_vertically (value) : lrg_tile_flip_horizontally (value));
	if (self->app != NULL)
		editor_app_set_status (self->app, "Tile brush: %s, %u°", lrg_tile_get_mirrored (lrg_editor_tiles_panel_get_brush_value (self))
		                                                       ? "mirrored" : "not mirrored",
		                       lrg_tile_get_rotation (lrg_editor_tiles_panel_get_brush_value (self)) * 90u);
}

void
lrg_editor_tiles_panel_rotate_brush (LrgEditorTilesPanel *self,
                                     gint                 quarter_turns)
{
	guint value;

	g_return_if_fail (LRG_IS_EDITOR_TILES_PANEL (self));

	value = lrg_editor_tiles_panel_get_brush_value (self);
	if (lrg_tile_get_id (value) == 0)
		return;
	set_brush_value (self, lrg_tile_rotate (value, quarter_turns));
	if (self->app != NULL)
		editor_app_set_status (self->app, "Tile brush rotated to %u°",
		                       lrg_tile_get_rotation (lrg_editor_tiles_panel_get_brush_value (self)) * 90u);
}

const gchar *
lrg_editor_tiles_panel_get_rule_tile (LrgEditorTilesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), NULL);
	return self->rule_tile;
}

gboolean
lrg_editor_tiles_panel_set_rule_tile (LrgEditorTilesPanel *self,
                                      const gchar         *name)
{
	LrgRuleTile *rule;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), FALSE);

	if (name == NULL || *name == '\0')
	{
		g_clear_pointer (&self->rule_tile, g_free);
		self->dirty = TRUE;
		return TRUE;
	}
	rule = node_rule_tile (self->app, lrg_editor_tiles_panel_get_target (self), name);
	if (rule == NULL)
		return FALSE;
	g_free (self->rule_tile);
	self->rule_tile = g_strdup (name);
	/* The painter paints the seed value; the gesture resolves it */
	if (painter (self) != NULL && rule_seed_value (rule) != 0)
		lrg_editor_tile_painter_set_tile (painter (self), rule_seed_value (rule));
	if (lrg_editor_view_state_get_tile_tool (self->view_state) == LRG_EDITOR_TILE_TOOL_PICK)
		lrg_editor_view_state_set_tile_tool (self->view_state, LRG_EDITOR_TILE_TOOL_BRUSH);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Painting with rule tile '%s'", name);
	self->dirty = TRUE;
	return TRUE;
}

guint
lrg_editor_tiles_panel_paint_cells (LrgEditorTilesPanel *self,
                                    const gint          *cells,
                                    gsize                n_values,
                                    gboolean             erase)
{
	LrgNode        *node;
	LrgNodeTilemap *grid;
	LrgRuleTile    *rule;
	guint           changed;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), 0);
	g_return_val_if_fail (cells != NULL || n_values == 0, 0);

	node = lrg_editor_tiles_panel_get_target (self);
	if (node == NULL)
		return 0;
	if (painter (self) != NULL)
		lrg_editor_tile_painter_ensure_tilemap (painter (self), node);
	if ((grid = target_grid (node)) == NULL || lrg_node_tilemap_get_n_layers (grid) == 0)
		return 0;
	rule = node_rule_tile (self->app, node, self->rule_tile);
	if (rule != NULL)
		changed = paint_rule_cells (self->editor, node, effective_layer (self, grid), rule, cells, n_values, erase);
	else
		changed = paint_plain_cells (self->editor, node, effective_layer (self, grid),
		                             erase ? 0 : lrg_editor_tiles_panel_get_brush_value (self), cells, n_values);
	self->dirty = TRUE;
	return changed;
}

guint
lrg_editor_tiles_panel_refresh_rule_tiles (LrgEditorTilesPanel *self,
                                           gint                 layer)
{
	LrgNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), 0);

	node = lrg_editor_tiles_panel_get_target (self);
	return node != NULL ? refresh_node (self, node, layer) : 0;
}

guint
lrg_editor_tiles_panel_get_info_tile (LrgEditorTilesPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), 0);

	if (self->hover_tile > 0)
		return (guint) self->hover_tile;
	if (self->rule_tile != NULL)
	{
		LrgRuleTile *rule = node_rule_tile (self->app, self->target, self->rule_tile);

		return rule != NULL ? lrg_tile_get_id (rule_seed_value (rule)) : 0;
	}
	return lrg_tile_get_id (lrg_editor_tiles_panel_get_brush_value (self));
}
