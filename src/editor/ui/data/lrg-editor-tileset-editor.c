/* lrg-editor-tileset-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTilesetEditor: the `.rtileset` editor (see the header).
 *
 * Layout:
 *
 *   +-----------------------------------+------------------------------+
 *   | SHEET (wheel zooms, middle pans)  | TILE 5 (index 4)             |
 *   | +-------------------------------+ | [Solid][Animated][Hazard]    |
 *   | | the image cut into its tiles, | | COLLISION [Box][Draw][Clear] |
 *   | | the selected tile outlined,   | | +--------------------------+ |
 *   | | badges: collision, data,      | | | the tile magnified, its  | |
 *   | | animation, rule ownership     | | | polygons, draggable pts  | |
 *   | +-------------------------------+ | +--------------------------+ |
 *   | LAYOUT name image size cols count | DATA key = value rows, add   |
 *   |        margin spacing offset      | ANIMATION frame chips, add   |
 *   |                                   | RULE TILES [combo] new, 16/47|
 *   |                                   | rules: [3x3][outputs][xf]^v- |
 *   |                                   | PREVIEW 8x8 grid (click)     |
 *   +-----------------------------------+------------------------------+
 *
 * Model. The document text is parsed with lrg_tileset_new_from_asset_data()
 * after every change (self->ts; a parse error empties the views). Edits
 * call edit_begin() for a fresh parse, change it through the engine API
 * and edit_commit() it: lrg_tileset_save_to_data() written over the whole
 * document in one labelled undo group (gesture ids merge drags). The
 * layout keys the engine has no setters for (tile size, columns, count)
 * are edited as document paths instead. Rule tiles are addressed by
 * name, rules and outputs by index, so they survive the re-parse.
 *
 * Widgets mirror the model on the next step after a change; rows are
 * rebuilt only when their signature (what they show) changed, so a text
 * input being typed into keeps its focus.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-tileset-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../tilemap/lrg-tilemap-layer.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-spin-box.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <raylib.h>
#include <math.h>
#include <string.h>

#define INSTALLED_KEY   "lrg-editor-tileset-installed"
#define PREVIEW_SIZE    (8)
#define POINT_RADIUS    (5.0f)
#define POINT_HIT       (8.0f)

/* The 3x3 pattern cells in reading order; the centre is the cell itself */
static const LrgTileNeighbor mask_cells[9] = {
	LRG_TILE_NEIGHBOR_NW, LRG_TILE_NEIGHBOR_N, LRG_TILE_NEIGHBOR_NE,
	LRG_TILE_NEIGHBOR_W, LRG_TILE_NEIGHBOR_NONE, LRG_TILE_NEIGHBOR_E,
	LRG_TILE_NEIGHBOR_SW, LRG_TILE_NEIGHBOR_S, LRG_TILE_NEIGHBOR_SE
};
static const gchar * const transform_labels[] = { "Fixed", "Rotated", "Mirror X", "Mirror Y", "Mirror XY" };
static const gchar * const transform_nicks[] = { "fixed", "rotated", "mirror-x", "mirror-y", "mirror-xy" };

struct _LrgEditorTilesetEditor
{
	LrgEditorDataEditor  parent_instance;

	/* The model */
	LrgTileset          *ts;             /* parsed from the document */
	gchar               *parse_error;
	gint                 selected;       /* tile index, -1 */
	gchar               *rule_name;      /* selected rule tile */
	gint                 rule_index;     /* selected rule, -1 */
	gint                 output_index;   /* selected output of the rule, -1 */

	/* The image */
	gchar               *image_path;     /* absolute */
	GrlTexture          *tex;            /* with a window */
	gint                 img_w;
	gint                 img_h;
	gint64               img_mtime;

	/* Sheet view */
	LrgWidget           *sheet;
	gfloat               zoom;
	gfloat               pan_x;
	gfloat               pan_y;
	gboolean             panning;
	gfloat               last_x;
	gfloat               last_y;
	gint                 hover;

	/* Layout row */
	LrgTextInput        *name_in;
	LrgTextInput        *image_in;
	LrgSpinBox          *tile_w;
	LrgSpinBox          *tile_h;
	LrgSpinBox          *columns;
	LrgSpinBox          *count;
	LrgSpinBox          *margin;
	LrgSpinBox          *spacing;
	LrgSpinBox          *off_x;
	LrgSpinBox          *off_y;

	/* Pages */
	LrgWidget           *page_chips[2];
	LrgWidget           *page_tile;
	LrgWidget           *page_rules;

	/* Tile section */
	LrgWidget           *tile_title;
	LrgWidget           *flags[3];
	LrgWidget           *poly;
	LrgWidget           *draw_chip;
	gboolean             draw_mode;
	GArray              *pending;        /* gfloat x, y of the polygon being drawn */
	gint                 drag_poly;
	gint                 drag_point;
	guint                drag_gesture;
	LrgWidget           *data_rows;
	LrgTextInput        *data_key;
	LrgTextInput        *data_value;
	LrgWidget           *anim_rows;
	LrgSpinBox          *anim_ms;

	/* Rule tiles */
	LrgComboBox         *rule_combo;
	LrgTextInput        *rule_new;
	LrgSpinBox          *rule_seed;
	LrgWidget           *border_chip;
	LrgWidget           *rules_box;
	LrgSpinBox          *weight;
	LrgWidget           *preview;
	gboolean             preview_mask[PREVIEW_SIZE * PREVIEW_SIZE];

	/* Bookkeeping */
	gboolean             syncing;
	gboolean             stale;
	gchar               *sig_tile;
	gchar               *sig_rules;
	gchar               *sig_combo;
};

G_DEFINE_FINAL_TYPE (LrgEditorTilesetEditor, lrg_editor_tileset_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_rules (LrgEditorTilesetEditor *self);
static void set_page (LrgEditorTilesetEditor *self, gboolean rules);

static LrgEditorDocument *
doc_of (LrgEditorTilesetEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

/* The directory relative image paths resolve against. */
static gchar *
base_dir (LrgEditorTilesetEditor *self)
{
	const gchar *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	const gchar *dir = lrg_editor_document_get_base_dir (doc_of (self));

	if (path != NULL)
		return g_path_get_dirname (path);
	return g_strdup (dir != NULL ? dir : ".");
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

/* A stable swatch colour per tile id (the tiles panel uses the same). */
static void
swatch (guint     id,
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

/* ==========================================================================
 * The model: parse, edit, commit
 * ========================================================================== */

/* A fresh tileset parsed from the document text. */
static LrgTileset *
parse_doc (LrgEditorTilesetEditor  *self,
           GError                 **error)
{
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autofree gchar *dir = base_dir (self);
	const gchar      *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autofree gchar *source = (path != NULL) ? g_path_get_basename (path) : g_strdup ("tileset");

	return lrg_tileset_new_from_asset_data (text != NULL ? text : "", -1, dir, source, FALSE, error);
}

/* Loads the image (texture with a window, size headless) when its path
 * or file changed. */
static void
sync_image (LrgEditorTilesetEditor *self)
{
	const gchar      *image = (self->ts != NULL) ? lrg_tileset_get_image_path (self->ts) : NULL;
	g_autofree gchar *abs = NULL;
	GStatBuf          st;
	gint64            mtime = 0;

	if (image != NULL && *image != '\0')
	{
		g_autofree gchar *dir = base_dir (self);

		abs = g_path_is_absolute (image) ? g_strdup (image) : g_build_filename (dir, image, NULL);
	}
	if (abs != NULL && g_stat (abs, &st) == 0)
		mtime = (gint64) st.st_mtime;
	if (g_strcmp0 (abs, self->image_path) == 0 && mtime == self->img_mtime &&
	    (self->tex != NULL || !IsWindowReady () || abs == NULL))
		return;
	g_clear_object (&self->tex);
	g_free (self->image_path);
	self->image_path = g_strdup (abs);
	self->img_mtime = mtime;
	self->img_w = self->img_h = 0;
	if (abs == NULL || mtime == 0)
		return;
	if (IsWindowReady ())
	{
		GrlTexture *tex = grl_texture_new_from_file (abs);

		if (tex != NULL && grl_texture_is_valid (tex))
		{
			self->tex = tex;
			self->img_w = grl_texture_get_width (tex);
			self->img_h = grl_texture_get_height (tex);
			return;
		}
		g_clear_object (&tex);
	}
	{
		Image img = LoadImage (abs);

		self->img_w = img.width;
		self->img_h = img.height;
		UnloadImage (img);
	}
}

static void
load_model (LrgEditorTilesetEditor *self)
{
	g_autoptr(GError) error = NULL;

	g_clear_object (&self->ts);
	g_clear_pointer (&self->parse_error, g_free);
	self->ts = parse_doc (self, &error);
	if (self->ts == NULL)
		self->parse_error = g_strdup (error != NULL ? error->message : "the tileset does not load");
	if (self->ts != NULL && self->selected >= (gint) lrg_tileset_get_tile_count (self->ts))
		self->selected = (gint) lrg_tileset_get_tile_count (self->ts) - 1;
	if (self->ts != NULL && self->rule_name != NULL && lrg_tileset_get_rule_tile_by_name (self->ts, self->rule_name) == NULL)
		g_clear_pointer (&self->rule_name, g_free);
	if (self->ts != NULL && self->rule_name == NULL && lrg_tileset_get_n_rule_tiles (self->ts) > 0)
		self->rule_name = g_strdup (lrg_rule_tile_get_name (lrg_tileset_get_rule_tile (self->ts, 0)));
	{
		LrgRuleTile *rt = (self->ts != NULL && self->rule_name != NULL)
		                  ? lrg_tileset_get_rule_tile_by_name (self->ts, self->rule_name) : NULL;

		if (rt == NULL || self->rule_index >= (gint) lrg_rule_tile_get_n_rules (rt))
			self->rule_index = (rt != NULL && lrg_rule_tile_get_n_rules (rt) > 0) ? 0 : -1;
	}
	sync_image (self);
	self->stale = TRUE;
}

/* A fresh copy to edit, or NULL with @error set. */
static LrgTileset *
edit_begin (LrgEditorTilesetEditor  *self,
            GError                 **error)
{
	return parse_doc (self, error);
}

/* Writes @ts as the whole document: one undo step labelled @label
 * (merged with the previous one for the same non-zero @gesture). */
static gboolean
edit_commit (LrgEditorTilesetEditor  *self,
             LrgTileset              *ts,
             const gchar             *label,
             guint                    gesture,
             GError                 **error)
{
	g_autofree gchar *text = lrg_tileset_save_to_data (ts, NULL, error);
	g_autofree gchar *now = NULL;
	gboolean          ok;

	if (text == NULL)
		return FALSE;
	now = lrg_editor_document_to_data (doc_of (self), NULL);
	if (g_strcmp0 (now, text) == 0)
		return TRUE;
	/* a gesture's replaces merge into one step on their own; a group
	 * around each would keep them apart */
	if (gesture != 0)
		return lrg_editor_document_replace_gesture (doc_of (self), "", text, gesture, error);
	lrg_editor_document_begin_group (doc_of (self), label);
	ok = lrg_editor_document_replace_gesture (doc_of (self), "", text, 0, error);
	lrg_editor_document_end_group (doc_of (self));
	return ok;
}

static gboolean
check_tile (LrgTileset  *ts,
            guint        index,
            GError     **error)
{
	if (index >= lrg_tileset_get_tile_count (ts))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "tile %u does not exist (the tileset has %u)", index, lrg_tileset_get_tile_count (ts));
		return FALSE;
	}
	return TRUE;
}

static LrgRuleTile *
find_rule_tile (LrgTileset   *ts,
                const gchar  *name,
                GError      **error)
{
	LrgRuleTile *rt = (name != NULL) ? lrg_tileset_get_rule_tile_by_name (ts, name) : NULL;

	if (rt == NULL)
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no rule tile named \"%s\"",
		             name != NULL ? name : "");
	return rt;
}

static LrgTileRule *
find_rule (LrgTileset   *ts,
           const gchar  *name,
           guint         index,
           LrgRuleTile **out_rt,
           GError      **error)
{
	LrgRuleTile *rt = find_rule_tile (ts, name, error);

	if (rt == NULL)
		return NULL;
	if (index >= lrg_rule_tile_get_n_rules (rt))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "rule %u does not exist (\"%s\" has %u)", index, name, lrg_rule_tile_get_n_rules (rt));
		return NULL;
	}
	if (out_rt != NULL)
		*out_rt = rt;
	return lrg_rule_tile_get_rule (rt, index);
}

/* ==========================================================================
 * Public edits
 * ========================================================================== */

gboolean
lrg_editor_tileset_editor_set_layout (LrgEditorTilesetEditor  *self,
                                      const gchar             *name,
                                      const gchar             *image,
                                      guint                    tile_width,
                                      guint                    tile_height,
                                      guint                    columns,
                                      gint                     tile_count,
                                      GError                 **error)
{
	LrgEditorDocument *doc;
	gboolean           ok = TRUE;
	gint               n, i;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);

	if (self->ts == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE,
		                     self->parse_error != NULL ? self->parse_error : "the tileset does not load");
		return FALSE;
	}
	doc = doc_of (self);
	lrg_editor_document_begin_group (doc, "Tileset Layout");
	if (ok && name != NULL)
		ok = lrg_editor_document_set_string (doc, "/name", name, 0, error);
	if (ok && image != NULL)
		ok = (*image == '\0') ? (!lrg_editor_document_has_path (doc, "/image") ||
		                         lrg_editor_document_remove (doc, "/image", error))
		                      : lrg_editor_document_set_string (doc, "/image", image, 0, error);
	if (ok && (tile_width > 0 || tile_height > 0))
	{
		g_autofree gchar *yaml = g_strdup_printf ("[%u, %u]",
		                                          tile_width > 0 ? tile_width : lrg_tileset_get_tile_width (self->ts),
		                                          tile_height > 0 ? tile_height : lrg_tileset_get_tile_height (self->ts));

		ok = lrg_editor_document_replace (doc, "/tile_size", yaml, error);
	}
	if (ok && columns > 0)
		ok = lrg_editor_document_set_int (doc, "/columns", columns, 0, error);
	if (ok && tile_count >= 0)
	{
		ok = lrg_editor_document_set_int (doc, "/tile_count", tile_count, 0, error);
		/* tile entries past the new count would not load: drop them */
		n = lrg_editor_document_get_length (doc, "/tiles");
		for (i = n - 1; ok && i >= 0; i--)
		{
			g_autofree gchar *path = g_strdup_printf ("/tiles/%d/id", i);
			g_autofree gchar *item = g_strdup_printf ("/tiles/%d", i);
			const gchar      *id = lrg_editor_document_get_scalar (doc, path);

			if (id != NULL && g_ascii_strtoll (id, NULL, 10) >= tile_count)
				ok = lrg_editor_document_remove (doc, item, error);
		}
	}
	lrg_editor_document_end_group (doc);
	return ok;
}

gboolean
lrg_editor_tileset_editor_set_spacing (LrgEditorTilesetEditor  *self,
                                       guint                    margin,
                                       guint                    spacing,
                                       gint                     offset_x,
                                       gint                     offset_y,
                                       GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL)
		return FALSE;
	lrg_tileset_set_margin_spacing (ts, margin, spacing);
	lrg_tileset_set_draw_offset (ts, offset_x, offset_y);
	return edit_commit (self, ts, "Tileset Spacing", 0, error);
}

gboolean
lrg_editor_tileset_editor_set_tile_flags (LrgEditorTilesetEditor  *self,
                                          guint                    index,
                                          LrgTileProperty          flags,
                                          GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || !check_tile (ts, index, error))
		return FALSE;
	lrg_tileset_set_tile_properties (ts, index, flags);
	return edit_commit (self, ts, "Tile Flags", 0, error);
}

gboolean
lrg_editor_tileset_editor_set_tile_data (LrgEditorTilesetEditor  *self,
                                         guint                    index,
                                         const gchar             *key,
                                         const gchar             *value,
                                         GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	if (*key == '\0')
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a data key cannot be empty");
		return FALSE;
	}
	if ((ts = edit_begin (self, error)) == NULL || !check_tile (ts, index, error))
		return FALSE;
	lrg_tileset_set_tile_data (ts, index, key, value);
	return edit_commit (self, ts, value != NULL ? "Tile Data" : "Remove Tile Data", 0, error);
}

gboolean
lrg_editor_tileset_editor_set_tile_animation (LrgEditorTilesetEditor  *self,
                                              guint                    index,
                                              const guint             *frames,
                                              const guint             *durations_ms,
                                              guint                    n_frames,
                                              GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	g_return_val_if_fail (n_frames == 0 || (frames != NULL && durations_ms != NULL), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || !check_tile (ts, index, error))
		return FALSE;
	for (i = 0; i < n_frames; i++)
		if (!check_tile (ts, frames[i], error))
			return FALSE;
	lrg_tileset_set_tile_animation (ts, index, frames, durations_ms, n_frames);
	return edit_commit (self, ts, "Tile Animation", 0, error);
}

gboolean
lrg_editor_tileset_editor_add_collision (LrgEditorTilesetEditor  *self,
                                         guint                    index,
                                         const gfloat            *coords,
                                         guint                    n_coords,
                                         GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if (coords == NULL || n_coords < 6 || n_coords % 2 != 0)
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a collision polygon needs at least three x, y points");
		return FALSE;
	}
	if ((ts = edit_begin (self, error)) == NULL || !check_tile (ts, index, error))
		return FALSE;
	lrg_tileset_add_tile_collision_polygon (ts, index, coords, n_coords);
	return edit_commit (self, ts, "Add Collision", 0, error);
}

/*
 * rewrite_polygons:
 * Rebuilds a tile's polygon list from @ts's polygons with one change:
 * @edit is called with each polygon's coordinates (a GArray of gfloat it
 * may change) and returns FALSE to drop the polygon.
 */
typedef gboolean (*PolyEdit) (guint polygon, GArray *coords, gpointer data);

static void
rewrite_polygons (LrgTileset *ts,
                  guint       index,
                  PolyEdit    edit,
                  gpointer    data)
{
	GPtrArray *polys = g_ptr_array_new_with_free_func ((GDestroyNotify) g_array_unref);
	guint      p, n, i;

	for (p = 0; p < lrg_tileset_get_tile_collision_polygon_count (ts, index); p++)
	{
		const gfloat *pts = lrg_tileset_get_tile_collision_polygon (ts, index, p, &n);
		GArray       *a = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), n);

		g_array_append_vals (a, pts, n);
		g_ptr_array_add (polys, a);
	}
	lrg_tileset_clear_tile_collision (ts, index);
	for (i = 0; i < polys->len; i++)
	{
		GArray *a = g_ptr_array_index (polys, i);

		if (edit (i, a, data) && a->len >= 6)
			lrg_tileset_add_tile_collision_polygon (ts, index, (const gfloat *) a->data, a->len);
	}
	g_ptr_array_unref (polys);
}

typedef struct { gint polygon; gint point; gfloat x, y; gint op; } PolyOp;   /* op: 0 drop, 1 move, 2 insert, 3 remove pt */

static gboolean
poly_op (guint    polygon,
         GArray  *coords,
         gpointer data)
{
	PolyOp *op = data;
	guint   n = coords->len / 2;

	if (op->polygon >= 0 && (guint) op->polygon != polygon)
		return TRUE;
	switch (op->op)
	{
	case 0:
		return FALSE;
	case 1:
		if ((guint) op->point < n)
		{
			g_array_index (coords, gfloat, 2 * op->point) = op->x;
			g_array_index (coords, gfloat, 2 * op->point + 1) = op->y;
		}
		return TRUE;
	case 2:
		{
			gfloat xy[2] = { op->x, op->y };

			g_array_insert_vals (coords, 2 * MIN ((guint) op->point, n), xy, 2);
		}
		return TRUE;
	case 3:
		if ((guint) op->point < n)
			g_array_remove_range (coords, 2 * op->point, 2);
		return coords->len >= 6;
	default:
		return TRUE;
	}
}

static gboolean
polygon_edit (LrgEditorTilesetEditor  *self,
              guint                    index,
              PolyOp                  *op,
              const gchar             *label,
              guint                    gesture,
              GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;

	if ((ts = edit_begin (self, error)) == NULL || !check_tile (ts, index, error))
		return FALSE;
	if (op->polygon >= (gint) lrg_tileset_get_tile_collision_polygon_count (ts, index))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "tile %u has no polygon %d", index, op->polygon);
		return FALSE;
	}
	rewrite_polygons (ts, index, poly_op, op);
	return edit_commit (self, ts, label, gesture, error);
}

gboolean
lrg_editor_tileset_editor_remove_collision (LrgEditorTilesetEditor  *self,
                                            guint                    index,
                                            gint                     polygon,
                                            GError                 **error)
{
	PolyOp op = { polygon, 0, 0.0f, 0.0f, 0 };

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	return polygon_edit (self, index, &op, polygon < 0 ? "Clear Collision" : "Remove Collision", 0, error);
}

gboolean
lrg_editor_tileset_editor_move_collision_point (LrgEditorTilesetEditor  *self,
                                                guint                    index,
                                                guint                    polygon,
                                                guint                    point,
                                                gfloat                   x,
                                                gfloat                   y,
                                                guint                    gesture,
                                                GError                 **error)
{
	PolyOp op = { (gint) polygon, (gint) point, x, y, 1 };

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	return polygon_edit (self, index, &op, "Move Collision Point", gesture, error);
}

gboolean
lrg_editor_tileset_editor_insert_collision_point (LrgEditorTilesetEditor  *self,
                                                  guint                    index,
                                                  guint                    polygon,
                                                  guint                    point,
                                                  gfloat                   x,
                                                  gfloat                   y,
                                                  GError                 **error)
{
	PolyOp op = { (gint) polygon, (gint) point, x, y, 2 };

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	return polygon_edit (self, index, &op, "Insert Collision Point", 0, error);
}

gboolean
lrg_editor_tileset_editor_remove_collision_point (LrgEditorTilesetEditor  *self,
                                                  guint                    index,
                                                  guint                    polygon,
                                                  guint                    point,
                                                  GError                 **error)
{
	PolyOp op = { (gint) polygon, (gint) point, 0.0f, 0.0f, 3 };

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	return polygon_edit (self, index, &op, "Remove Collision Point", 0, error);
}

/* --- rule tiles --- */

gboolean
lrg_editor_tileset_editor_add_rule_tile (LrgEditorTilesetEditor  *self,
                                         const gchar             *name,
                                         const gchar             *preset,
                                         guint                    first_index,
                                         GError                 **error)
{
	g_autoptr(LrgTileset)  ts = NULL;
	g_autoptr(LrgRuleTile) rt = NULL;
	guint                  need = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if (name == NULL || *name == '\0')
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a rule tile needs a name");
		return FALSE;
	}
	if ((ts = edit_begin (self, error)) == NULL)
		return FALSE;
	if (lrg_tileset_get_rule_tile_by_name (ts, name) != NULL)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
		             "a rule tile named \"%s\" exists", name);
		return FALSE;
	}
	if (g_strcmp0 (preset, "edges16") == 0)
	{
		need = LRG_RULE_TILE_EDGES16_COUNT;
		rt = lrg_rule_tile_new_edges16 (name, first_index + 1);
	}
	else if (g_strcmp0 (preset, "blob47") == 0)
	{
		need = LRG_RULE_TILE_BLOB47_COUNT;
		rt = lrg_rule_tile_new_blob47 (name, first_index + 1);
	}
	else if (preset == NULL || *preset == '\0')
		rt = lrg_rule_tile_new (name);
	else
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		             "unknown preset \"%s\" (edges16 or blob47)", preset);
		return FALSE;
	}
	if (first_index + need > lrg_tileset_get_tile_count (ts))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "the %s preset needs tiles %u..%u (the tileset has %u)", preset, first_index,
		             first_index + need - 1, lrg_tileset_get_tile_count (ts));
		return FALSE;
	}
	lrg_tileset_add_rule_tile (ts, rt);
	if (!edit_commit (self, ts, "Add Rule Tile", 0, error))
		return FALSE;
	g_free (self->rule_name);
	self->rule_name = g_strdup (name);
	self->rule_index = lrg_rule_tile_get_n_rules (rt) > 0 ? 0 : -1;
	self->stale = TRUE;
	set_page (self, TRUE);
	return TRUE;
}

gboolean
lrg_editor_tileset_editor_remove_rule_tile (LrgEditorTilesetEditor  *self,
                                            const gchar             *name,
                                            GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || (rt = find_rule_tile (ts, name, error)) == NULL)
		return FALSE;
	lrg_tileset_remove_rule_tile (ts, rt);
	return edit_commit (self, ts, "Remove Rule Tile", 0, error);
}

gboolean
lrg_editor_tileset_editor_set_rule_tile (LrgEditorTilesetEditor  *self,
                                         const gchar             *name,
                                         const gchar             *new_name,
                                         gint64                   default_tile,
                                         gint64                   seed,
                                         gint                     border_is_this,
                                         GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || (rt = find_rule_tile (ts, name, error)) == NULL)
		return FALSE;
	if (new_name != NULL && g_strcmp0 (new_name, name) != 0)
	{
		if (*new_name == '\0' || lrg_tileset_get_rule_tile_by_name (ts, new_name) != NULL)
		{
			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
			             "cannot rename to \"%s\" (empty or taken)", new_name);
			return FALSE;
		}
		lrg_rule_tile_set_name (rt, new_name);
	}
	if (default_tile >= 0)
		lrg_rule_tile_set_default_tile (rt, (guint) default_tile);
	if (seed >= 0)
		lrg_rule_tile_set_seed (rt, (guint) seed);
	if (border_is_this >= 0)
		lrg_rule_tile_set_border_is_this (rt, border_is_this != 0);
	if (!edit_commit (self, ts, "Rule Tile", 0, error))
		return FALSE;
	if (new_name != NULL && g_strcmp0 (self->rule_name, name) == 0)
	{
		g_free (self->rule_name);
		self->rule_name = g_strdup (new_name);
	}
	return TRUE;
}

gboolean
lrg_editor_tileset_editor_add_rule (LrgEditorTilesetEditor  *self,
                                    const gchar             *rule_tile,
                                    gint                     index,
                                    guint                    output,
                                    GError                 **error)
{
	g_autoptr(LrgTileset)  ts = NULL;
	g_autoptr(LrgTileRule) rule = NULL;
	LrgRuleTile           *rt;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || (rt = find_rule_tile (ts, rule_tile, error)) == NULL)
		return FALSE;
	if (output == 0)
		output = self->selected >= 0 ? (guint) self->selected + 1 : 1;
	if (!check_tile (ts, lrg_tile_get_id (output) - 1, error))
		return FALSE;
	rule = lrg_tile_rule_new ();
	lrg_tile_rule_add_output (rule, output, 1.0f);
	if (index < 0 || (guint) index >= lrg_rule_tile_get_n_rules (rt))
		index = (gint) lrg_rule_tile_get_n_rules (rt);
	lrg_rule_tile_insert_rule (rt, (guint) index, rule);
	if (!edit_commit (self, ts, "Add Rule", 0, error))
		return FALSE;
	self->rule_index = index;
	self->output_index = 0;
	return TRUE;
}

gboolean
lrg_editor_tileset_editor_remove_rule (LrgEditorTilesetEditor  *self,
                                       const gchar             *rule_tile,
                                       guint                    index,
                                       GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || find_rule (ts, rule_tile, index, &rt, error) == NULL)
		return FALSE;
	lrg_rule_tile_remove_rule (rt, index);
	return edit_commit (self, ts, "Remove Rule", 0, error);
}

gboolean
lrg_editor_tileset_editor_move_rule (LrgEditorTilesetEditor  *self,
                                     const gchar             *rule_tile,
                                     guint                    from,
                                     guint                    to,
                                     GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || find_rule (ts, rule_tile, from, &rt, error) == NULL)
		return FALSE;
	if (!lrg_rule_tile_move_rule (rt, from, MIN (to, lrg_rule_tile_get_n_rules (rt) - 1)))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "bad rule index");
		return FALSE;
	}
	if (!edit_commit (self, ts, "Move Rule", 0, error))
		return FALSE;
	if (self->rule_index == (gint) from)
		self->rule_index = (gint) MIN (to, lrg_rule_tile_get_n_rules (rt) - 1);
	return TRUE;
}

gboolean
lrg_editor_tileset_editor_set_rule_neighbor (LrgEditorTilesetEditor  *self,
                                             const gchar             *rule_tile,
                                             guint                    index,
                                             LrgTileNeighbor          direction,
                                             LrgTileRuleMatch         match,
                                             GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt = NULL;
	LrgTileRule          *rule;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || (rule = find_rule (ts, rule_tile, index, &rt, error)) == NULL)
		return FALSE;
	lrg_tile_rule_set_neighbor (rule, direction, match);
	lrg_rule_tile_changed (rt);
	return edit_commit (self, ts, "Rule Pattern", 0, error);
}

gboolean
lrg_editor_tileset_editor_cycle_rule_neighbor (LrgEditorTilesetEditor  *self,
                                               const gchar             *rule_tile,
                                               guint                    index,
                                               LrgTileNeighbor          direction,
                                               gboolean                 backwards,
                                               GError                 **error)
{
	LrgTileRule     *rule;
	LrgTileRuleMatch m;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if (self->ts == NULL || (rule = find_rule (self->ts, rule_tile, index, NULL, error)) == NULL)
		return FALSE;
	m = lrg_tile_rule_get_neighbor (rule, direction);
	m = (LrgTileRuleMatch) (((gint) m + (backwards ? 2 : 1)) % 3);
	return lrg_editor_tileset_editor_set_rule_neighbor (self, rule_tile, index, direction, m, error);
}

gboolean
lrg_editor_tileset_editor_set_rule_transform (LrgEditorTilesetEditor  *self,
                                              const gchar             *rule_tile,
                                              guint                    index,
                                              LrgTileRuleTransform     transform,
                                              GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt = NULL;
	LrgTileRule          *rule;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || (rule = find_rule (ts, rule_tile, index, &rt, error)) == NULL)
		return FALSE;
	lrg_tile_rule_set_transform (rule, transform);
	lrg_rule_tile_changed (rt);
	return edit_commit (self, ts, "Rule Transform", 0, error);
}

gboolean
lrg_editor_tileset_editor_add_rule_output (LrgEditorTilesetEditor  *self,
                                           const gchar             *rule_tile,
                                           guint                    index,
                                           guint                    value,
                                           gfloat                   weight,
                                           GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt = NULL;
	LrgTileRule          *rule;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || (rule = find_rule (ts, rule_tile, index, &rt, error)) == NULL)
		return FALSE;
	if (lrg_tile_get_id (value) == 0 || !check_tile (ts, lrg_tile_get_id (value) - 1, error))
	{
		if (error != NULL && *error == NULL)
			g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
			                     "an output needs a tile");
		return FALSE;
	}
	lrg_tile_rule_add_output (rule, value, weight > 0.0f ? weight : 1.0f);
	lrg_rule_tile_changed (rt);
	if (!edit_commit (self, ts, "Add Rule Output", 0, error))
		return FALSE;
	self->output_index = (gint) lrg_tile_rule_get_n_outputs (rule) - 1;
	return TRUE;
}

gboolean
lrg_editor_tileset_editor_remove_rule_output (LrgEditorTilesetEditor  *self,
                                              const gchar             *rule_tile,
                                              guint                    index,
                                              guint                    output,
                                              GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt = NULL;
	LrgTileRule          *rule;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if ((ts = edit_begin (self, error)) == NULL || (rule = find_rule (ts, rule_tile, index, &rt, error)) == NULL)
		return FALSE;
	if (output >= lrg_tile_rule_get_n_outputs (rule) || lrg_tile_rule_get_n_outputs (rule) < 2)
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		                     "no such output (a rule keeps at least one)");
		return FALSE;
	}
	lrg_tile_rule_remove_output (rule, output);
	lrg_rule_tile_changed (rt);
	if (!edit_commit (self, ts, "Remove Rule Output", 0, error))
		return FALSE;
	self->output_index = -1;
	return TRUE;
}

gboolean
lrg_editor_tileset_editor_set_rule_output_weight (LrgEditorTilesetEditor  *self,
                                                  const gchar             *rule_tile,
                                                  guint                    index,
                                                  guint                    output,
                                                  gfloat                   weight,
                                                  GError                 **error)
{
	g_autoptr(LrgTileset) ts = NULL;
	LrgRuleTile          *rt = NULL;
	LrgTileRule          *rule;
	g_autoptr(GArray)     values = NULL;
	g_autoptr(GArray)     weights = NULL;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);
	if (!(weight > 0.0f))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		                     "a weight must be > 0");
		return FALSE;
	}
	if ((ts = edit_begin (self, error)) == NULL || (rule = find_rule (ts, rule_tile, index, &rt, error)) == NULL)
		return FALSE;
	if (output >= lrg_tile_rule_get_n_outputs (rule))
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no such output");
		return FALSE;
	}
	/* outputs have no setter: rebuild them in order */
	values = g_array_new (FALSE, FALSE, sizeof (guint));
	weights = g_array_new (FALSE, FALSE, sizeof (gfloat));
	for (i = 0; i < lrg_tile_rule_get_n_outputs (rule); i++)
	{
		gfloat w;
		guint  v = lrg_tile_rule_get_output (rule, i, &w);

		if (i == output)
			w = weight;
		g_array_append_val (values, v);
		g_array_append_val (weights, w);
	}
	lrg_tile_rule_clear_outputs (rule);
	for (i = 0; i < values->len; i++)
		lrg_tile_rule_add_output (rule, g_array_index (values, guint, i), g_array_index (weights, gfloat, i));
	lrg_rule_tile_changed (rt);
	return edit_commit (self, ts, "Output Weight", 0, error);
}

guint *
lrg_editor_tileset_editor_preview (LrgEditorTilesetEditor *self,
                                   const gchar            *rule_tile,
                                   const gint             *cells,
                                   gsize                   n_values,
                                   guint                   width,
                                   guint                   height)
{
	LrgRuleTile *rt;
	guint       *map;
	gsize        i;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), NULL);
	g_return_val_if_fail (cells != NULL || n_values == 0, NULL);

	if (self->ts == NULL || width == 0 || height == 0 ||
	    (rt = lrg_tileset_get_rule_tile_by_name (self->ts, rule_tile)) == NULL)
		return NULL;
	map = g_new0 (guint, (gsize) width * height);
	for (i = 0; i + 1 < n_values; i += 2)
		lrg_rule_tile_paint (rt, map, (gsize) width * height, width, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, cells[i],
		                     cells[i + 1]);
	return map;
}

/* ==========================================================================
 * Getters and selection
 * ========================================================================== */

LrgTileset *
lrg_editor_tileset_editor_get_tileset (LrgEditorTilesetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), NULL);
	return self->ts;
}

const gchar *
lrg_editor_tileset_editor_get_parse_error (LrgEditorTilesetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), NULL);
	return self->parse_error;
}

gint
lrg_editor_tileset_editor_get_selected_tile (LrgEditorTilesetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), -1);
	return self->selected;
}

void
lrg_editor_tileset_editor_set_selected_tile (LrgEditorTilesetEditor *self,
                                             gint                    index)
{
	g_return_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self));

	if (self->ts == NULL || index < 0)
		index = -1;
	else
		index = MIN (index, (gint) lrg_tileset_get_tile_count (self->ts) - 1);
	if (index != self->selected)
	{
		self->selected = index;
		self->drag_poly = -1;
		g_array_set_size (self->pending, 0);
		self->stale = TRUE;
	}
}

const gchar *
lrg_editor_tileset_editor_get_selected_rule_tile (LrgEditorTilesetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), NULL);
	return self->rule_name;
}

gboolean
lrg_editor_tileset_editor_select_rule_tile (LrgEditorTilesetEditor *self,
                                            const gchar            *name)
{
	LrgRuleTile *rt;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), FALSE);

	if (name == NULL)
	{
		g_clear_pointer (&self->rule_name, g_free);
		self->rule_index = -1;
		self->stale = TRUE;
		return TRUE;
	}
	if (self->ts == NULL || (rt = lrg_tileset_get_rule_tile_by_name (self->ts, name)) == NULL)
		return FALSE;
	if (g_strcmp0 (name, self->rule_name) != 0)
	{
		g_free (self->rule_name);
		self->rule_name = g_strdup (name);
		self->rule_index = lrg_rule_tile_get_n_rules (rt) > 0 ? 0 : -1;
		self->output_index = -1;
		memset (self->preview_mask, 0, sizeof self->preview_mask);
	}
	self->stale = TRUE;
	set_page (self, TRUE);
	return TRUE;
}

gint
lrg_editor_tileset_editor_get_selected_rule (LrgEditorTilesetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), -1);
	return self->rule_index;
}

void
lrg_editor_tileset_editor_select_rule (LrgEditorTilesetEditor *self,
                                       gint                    index)
{
	LrgRuleTile *rt;

	g_return_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self));

	rt = (self->ts != NULL && self->rule_name != NULL) ? lrg_tileset_get_rule_tile_by_name (self->ts, self->rule_name)
	                                                   : NULL;
	if (rt == NULL || index < 0 || (guint) index >= lrg_rule_tile_get_n_rules (rt))
		index = -1;
	if (index != self->rule_index)
	{
		self->rule_index = index;
		self->output_index = -1;
		self->stale = TRUE;
	}
}

/* ==========================================================================
 * The sheet view
 * ========================================================================== */

/* Sheet origin and pixels per image pixel for the widget's rect. */
static void
sheet_transform (LrgEditorTilesetEditor *self,
                 gfloat                  x,
                 gfloat                  y,
                 gfloat                  width,
                 gfloat                  height,
                 gfloat                 *ox,
                 gfloat                 *oy,
                 gfloat                 *scale)
{
	gfloat iw, ih, fit;

	if (self->img_w > 0 && self->img_h > 0)
	{
		iw = (gfloat) self->img_w;
		ih = (gfloat) self->img_h;
	}
	else if (self->ts != NULL)
	{
		guint cols = MAX (1u, lrg_tileset_get_columns (self->ts));
		guint rows = MAX (1u, (lrg_tileset_get_tile_count (self->ts) + cols - 1) / cols);

		iw = (gfloat) (cols * lrg_tileset_get_tile_width (self->ts));
		ih = (gfloat) (rows * lrg_tileset_get_tile_height (self->ts));
	}
	else
	{
		iw = ih = 64.0f;
	}
	fit = MIN ((width - 24.0f) / MAX (1.0f, iw), (height - 24.0f) / MAX (1.0f, ih));
	*scale = MAX (0.05f, fit * self->zoom);
	*ox = x + (width - iw * *scale) * 0.5f + self->pan_x;
	*oy = y + (height - ih * *scale) * 0.5f + self->pan_y;
}

/* The image rect of tile @index (headless sheets without image use the
 * grid layout). */
static gboolean
tile_rect (LrgEditorTilesetEditor *self,
           guint                   index,
           GrlRectangle           *r)
{
	return self->ts != NULL && lrg_tileset_get_tile_rect_to (self->ts, index, r);
}

static void
sheet_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                *font = lrg_theme_get_ui_font (theme);
	gfloat                  fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor         *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	LrgRuleTile            *sel_rt = (self->ts != NULL && self->rule_name != NULL)
	                                 ? lrg_tileset_get_rule_tile_by_name (self->ts, self->rule_name) : NULL;
	gfloat                  ox, oy, s;
	guint                   i, n;
	GrlColor                c;

	lrg_paint_list_rect (list, x, y, width, height, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	if (self->ts == NULL)
	{
		lrg_paint_list_text (list, font, self->parse_error != NULL ? self->parse_error : "Nothing to show",
		                     x + 12.0f, y + 12.0f, fs, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		return;
	}
	sheet_transform (self, x, y, width, height, &ox, &oy, &s);
	lrg_paint_list_push_clip (list, x, y, width, height);
	if (self->tex != NULL)
	{
		GrlRectangle all = { 0.0f, 0.0f, (gfloat) self->img_w, (gfloat) self->img_h };
		GrlColor     white = { 255, 255, 255, 255 };

		lrg_paint_list_image (list, self->tex, &all, ox, oy, (gfloat) self->img_w * s, (gfloat) self->img_h * s, 0.0f,
		                      &white);
	}

	n = lrg_tileset_get_tile_count (self->ts);
	for (i = 0; i < n; i++)
	{
		GrlRectangle r;
		gfloat       rx, ry, rw, rh;
		g_auto(GStrv) keys = NULL;
		LrgRuleTile *owner;
		gfloat       d;

		if (!tile_rect (self, i, &r))
			continue;
		rx = ox + r.x * s;
		ry = oy + r.y * s;
		rw = r.width * s;
		rh = r.height * s;
		if (self->tex == NULL)
		{
			swatch (i + 1, &c);
			lrg_paint_list_rect (list, rx + 1.0f, ry + 1.0f, rw - 2.0f, rh - 2.0f, 2.0f, &c);
		}
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.55f, &c);
		lrg_paint_list_border (list, rx, ry, rw, rh, 0.0f, 1.0f, &c);
		if (rw >= 26.0f)
		{
			g_autofree gchar *num = g_strdup_printf ("%u", i);
			GrlColor          ink = { 17, 17, 27, 200 };
			GrlColor          paper = { 255, 255, 255, 150 };

			lrg_paint_list_rect (list, rx + 1.0f, ry + 1.0f, fs * 0.6f * (gfloat) strlen (num) + 4.0f, fs + 2.0f, 2.0f,
			                     &paper);
			lrg_paint_list_text (list, font, num, rx + 3.0f, ry + 2.0f, fs * 0.9f, 0.0f, &ink);
		}

		/* badges: collision, data, animation; rule ownership triangle */
		d = MAX (2.5f, MIN (rw, rh) * 0.07f);
		{
			gfloat bx = rx + rw - d - 3.0f;

			keys = lrg_tileset_get_tile_data_keys (self->ts, i);
			if (lrg_tileset_tile_has_collision (self->ts, i))
			{
				lrg_paint_list_circle (list, bx, ry + d + 3.0f, d, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
				bx -= 2.0f * d + 2.0f;
			}
			if (keys != NULL && keys[0] != NULL)
			{
				lrg_paint_list_circle (list, bx, ry + d + 3.0f, d, lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK));
				bx -= 2.0f * d + 2.0f;
			}
			if (lrg_tileset_get_tile_animation_length (self->ts, i) > 0)
				lrg_paint_list_circle (list, bx, ry + d + 3.0f, d, lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
		}
		owner = lrg_tileset_find_rule_tile_for (self->ts, i + 1);
		if (owner != NULL)
		{
			gfloat t = MIN (rw, rh) * 0.28f;

			lrg_paint_color_with_alpha (accent, owner == sel_rt ? 0.95f : 0.45f, &c);
			lrg_paint_list_triangle (list, rx, ry + rh, rx + t, ry + rh, rx, ry + rh - t, &c);
		}
		if ((gint) i == self->hover && (gint) i != self->selected)
		{
			lrg_paint_color_with_alpha (accent, 0.2f, &c);
			lrg_paint_list_rect (list, rx, ry, rw, rh, 0.0f, &c);
		}
	}
	if (self->selected >= 0)
	{
		GrlRectangle r;

		if (tile_rect (self, (guint) self->selected, &r))
			lrg_paint_list_border (list, ox + r.x * s - 2.0f, oy + r.y * s - 2.0f, r.width * s + 4.0f, r.height * s + 4.0f,
			                       3.0f, 2.5f, accent);
	}
	lrg_paint_list_pop_clip (list);
}

gint
lrg_editor_tileset_editor_tile_at (LrgEditorTilesetEditor *self,
                                   gfloat                  x,
                                   gfloat                  y)
{
	LrgWidget *w;
	gfloat     ox, oy, s, px, py;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR_TILESET_EDITOR (self), -1);

	w = self->sheet;
	if (self->ts == NULL || w == NULL)
		return -1;
	sheet_transform (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
	                 lrg_widget_get_height (w), &ox, &oy, &s);
	px = (x - ox) / s;
	py = (y - oy) / s;
	for (i = 0; i < lrg_tileset_get_tile_count (self->ts); i++)
	{
		GrlRectangle r;

		if (tile_rect (self, i, &r) && px >= r.x && py >= r.y && px < r.x + r.width && py < r.y + r.height)
			return (gint) i;
	}
	return -1;
}

static gboolean
sheet_event (LrgCustomWidget  *widget,
             const LrgUIEvent *event,
             gpointer          user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	gfloat                  ex = lrg_ui_event_get_x (event), ey = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->panning)
		{
			self->pan_x += ex - self->last_x;
			self->pan_y += ey - self->last_y;
			self->last_x = ex;
			self->last_y = ey;
			return TRUE;
		}
		self->hover = lrg_editor_tileset_editor_tile_at (self, ex, ey);
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover = -1;
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) == 2)
		{
			self->panning = TRUE;
			self->last_x = ex;
			self->last_y = ey;
			return TRUE;
		}
		if (lrg_ui_event_get_button (event) == 0)
		{
			gint t = lrg_editor_tileset_editor_tile_at (self, ex, ey);

			if (t >= 0)
				lrg_editor_tileset_editor_set_selected_tile (self, t);
			return TRUE;
		}
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		self->panning = FALSE;
		return FALSE;
	case LRG_UI_EVENT_SCROLL:
		self->zoom = CLAMP (self->zoom * (lrg_ui_event_get_scroll_y (event) > 0.0f ? 1.15f : 1.0f / 1.15f), 0.25f, 16.0f);
		return TRUE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * The collision polygon canvas
 * ========================================================================== */

static void
poly_transform (LrgEditorTilesetEditor *self,
                gfloat                  x,
                gfloat                  y,
                gfloat                  width,
                gfloat                  height,
                gfloat                 *ox,
                gfloat                 *oy,
                gfloat                 *sx,
                gfloat                 *sy)
{
	gfloat tw = (self->ts != NULL) ? (gfloat) MAX (1u, lrg_tileset_get_tile_width (self->ts)) : 16.0f;
	gfloat th = (self->ts != NULL) ? (gfloat) MAX (1u, lrg_tileset_get_tile_height (self->ts)) : 16.0f;
	gfloat side = MIN (width, height) - 16.0f;
	gfloat s = MAX (1.0f, side / MAX (tw, th));

	*sx = *sy = s;
	*ox = x + (width - tw * s) * 0.5f;
	*oy = y + (height - th * s) * 0.5f;
}

static void
poly_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                *font = lrg_theme_get_ui_font (theme);
	gfloat                  fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor         *danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
	const GrlColor         *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	gfloat                  ox, oy, sx, sy, tw, th;
	guint                   p, n, k;
	GrlRectangle            r;
	GrlColor                c;

	lrg_paint_list_rect (list, x, y, width, height, 4.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	if (self->ts == NULL || self->selected < 0)
	{
		lrg_paint_list_text (list, font, "Select a tile in the sheet", x + 10.0f, y + 10.0f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		return;
	}
	poly_transform (self, x, y, width, height, &ox, &oy, &sx, &sy);
	tw = (gfloat) lrg_tileset_get_tile_width (self->ts);
	th = (gfloat) lrg_tileset_get_tile_height (self->ts);

	/* the tile, a pixel grid when big enough, the tile frame */
	if (self->tex != NULL && tile_rect (self, (guint) self->selected, &r))
	{
		GrlColor white = { 255, 255, 255, 255 };

		lrg_paint_list_image (list, self->tex, &r, ox, oy, tw * sx, th * sy, 0.0f, &white);
	}
	else
	{
		swatch ((guint) self->selected + 1, &c);
		lrg_paint_color_with_alpha (&c, 0.6f, &c);
		lrg_paint_list_rect (list, ox, oy, tw * sx, th * sy, 0.0f, &c);
	}
	if (sx >= 8.0f && tw <= 64.0f)
	{
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.25f, &c);
		for (k = 1; k < (guint) tw; k++)
			lrg_paint_list_line (list, ox + (gfloat) k * sx, oy, ox + (gfloat) k * sx, oy + th * sy, 1.0f, &c);
		for (k = 1; k < (guint) th; k++)
			lrg_paint_list_line (list, ox, oy + (gfloat) k * sy, ox + tw * sx, oy + (gfloat) k * sy, 1.0f, &c);
	}
	lrg_paint_list_border (list, ox, oy, tw * sx, th * sy, 0.0f, 1.5f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	/* the polygons: filled, outlined, handles */
	for (p = 0; p < lrg_tileset_get_tile_collision_polygon_count (self->ts, (guint) self->selected); p++)
	{
		const gfloat *pts = lrg_tileset_get_tile_collision_polygon (self->ts, (guint) self->selected, p, &n);

		lrg_paint_color_with_alpha (danger, 0.22f, &c);
		for (k = 2; k + 3 < n + 2 && k + 1 < n; k += 2)
			lrg_paint_list_triangle (list, ox + pts[0] * sx, oy + pts[1] * sy, ox + pts[k] * sx, oy + pts[k + 1] * sy,
			                         ox + pts[k + 2 < n ? k + 2 : 0] * sx, oy + pts[k + 3 < n ? k + 3 : 1] * sy, &c);
		for (k = 0; k + 1 < n; k += 2)
		{
			guint j = (k + 2) % n;

			lrg_paint_list_line (list, ox + pts[k] * sx, oy + pts[k + 1] * sy, ox + pts[j] * sx, oy + pts[j + 1] * sy,
			                     2.0f, danger);
		}
		for (k = 0; k + 1 < n; k += 2)
		{
			gboolean hot = (gint) p == self->drag_poly && (gint) (k / 2) == self->drag_point;

			lrg_paint_list_circle (list, ox + pts[k] * sx, oy + pts[k + 1] * sy, hot ? POINT_RADIUS + 2.0f : POINT_RADIUS,
			                       hot ? accent : danger);
		}
	}
	if ((lrg_tileset_get_tile_properties (self->ts, (guint) self->selected) & LRG_TILE_PROPERTY_SOLID) != 0 &&
	    lrg_tileset_get_tile_collision_polygon_count (self->ts, (guint) self->selected) == 0)
		lrg_paint_list_border (list, ox + 2.0f, oy + 2.0f, tw * sx - 4.0f, th * sy - 4.0f, 0.0f, 2.0f, danger);

	/* the polygon being drawn */
	for (k = 0; k + 1 < self->pending->len; k += 2)
	{
		gfloat px = ox + g_array_index (self->pending, gfloat, k) * sx;
		gfloat py = oy + g_array_index (self->pending, gfloat, k + 1) * sy;

		if (k + 3 < self->pending->len)
			lrg_paint_list_line (list, px, py, ox + g_array_index (self->pending, gfloat, k + 2) * sx,
			                     oy + g_array_index (self->pending, gfloat, k + 3) * sy, 2.0f, accent);
		lrg_paint_list_circle (list, px, py, k == 0 ? POINT_RADIUS + 1.5f : POINT_RADIUS, accent);
	}
	if (self->draw_mode)
		lrg_paint_list_text (list, font, self->pending->len >= 6 ? "Click the first point to close"
		                                                         : "Click to add points", x + 8.0f, y + height - fs - 6.0f,
		                     fs, 0.0f, accent);
}

/* The polygon point under a canvas point: polygon and point indices. */
static gboolean
poly_hit (LrgEditorTilesetEditor *self,
          gfloat                  ex,
          gfloat                  ey,
          gint                   *out_poly,
          gint                   *out_point)
{
	LrgWidget *w = self->poly;
	gfloat     ox, oy, sx, sy;
	guint      p, n, k;

	if (self->ts == NULL || self->selected < 0)
		return FALSE;
	poly_transform (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
	                lrg_widget_get_height (w), &ox, &oy, &sx, &sy);
	for (p = 0; p < lrg_tileset_get_tile_collision_polygon_count (self->ts, (guint) self->selected); p++)
	{
		const gfloat *pts = lrg_tileset_get_tile_collision_polygon (self->ts, (guint) self->selected, p, &n);

		for (k = 0; k + 1 < n; k += 2)
			if (fabsf (ox + pts[k] * sx - ex) <= POINT_HIT && fabsf (oy + pts[k + 1] * sy - ey) <= POINT_HIT)
			{
				*out_poly = (gint) p;
				*out_point = (gint) (k / 2);
				return TRUE;
			}
	}
	return FALSE;
}

/* Canvas point -> tile pixel, rounded and clamped to the tile. */
static void
poly_to_tile (LrgEditorTilesetEditor *self,
              gfloat                  ex,
              gfloat                  ey,
              gfloat                 *tx,
              gfloat                 *ty)
{
	LrgWidget *w = self->poly;
	gfloat     ox, oy, sx, sy;

	poly_transform (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
	                lrg_widget_get_height (w), &ox, &oy, &sx, &sy);
	*tx = CLAMP (roundf ((ex - ox) / sx), 0.0f, (gfloat) lrg_tileset_get_tile_width (self->ts));
	*ty = CLAMP (roundf ((ey - oy) / sy), 0.0f, (gfloat) lrg_tileset_get_tile_height (self->ts));
}

static void
report (LrgEditorTilesetEditor *self,
        GError                 *error)
{
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static gboolean
poly_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	gfloat                  ex = lrg_ui_event_get_x (event), ey = lrg_ui_event_get_y (event);
	g_autoptr(GError)       error = NULL;
	gint                    p, k;
	gfloat                  tx, ty;

	if (self->ts == NULL || self->selected < 0)
		return FALSE;
	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) == 1 && poly_hit (self, ex, ey, &p, &k))
		{
			/* right click removes a point */
			lrg_editor_tileset_editor_remove_collision_point (self, (guint) self->selected, (guint) p, (guint) k, &error);
			report (self, error);
			return TRUE;
		}
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		poly_to_tile (self, ex, ey, &tx, &ty);
		if (self->draw_mode)
		{
			/* closing click on the first point commits the polygon */
			if (self->pending->len >= 6)
			{
				LrgWidget *w = self->poly;
				gfloat     ox, oy, sx, sy;

				poly_transform (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
				                lrg_widget_get_height (w), &ox, &oy, &sx, &sy);
				if (fabsf (ox + g_array_index (self->pending, gfloat, 0) * sx - ex) <= POINT_HIT &&
				    fabsf (oy + g_array_index (self->pending, gfloat, 1) * sy - ey) <= POINT_HIT)
				{
					lrg_editor_tileset_editor_add_collision (self, (guint) self->selected,
					                                         (const gfloat *) self->pending->data, self->pending->len,
					                                         &error);
					report (self, error);
					g_array_set_size (self->pending, 0);
					self->draw_mode = FALSE;
					editor_chip_set_checked (self->draw_chip, FALSE);
					return TRUE;
				}
			}
			g_array_append_val (self->pending, tx);
			g_array_append_val (self->pending, ty);
			return TRUE;
		}
		if (poly_hit (self, ex, ey, &p, &k))
		{
			self->drag_poly = p;
			self->drag_point = k;
			self->drag_gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
			return TRUE;
		}
		return TRUE;
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->drag_poly >= 0 && self->drag_gesture != 0)
		{
			poly_to_tile (self, ex, ey, &tx, &ty);
			lrg_editor_tileset_editor_move_collision_point (self, (guint) self->selected, (guint) self->drag_poly,
			                                                (guint) self->drag_point, tx, ty, self->drag_gesture, &error);
			report (self, error);
			return TRUE;
		}
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		self->drag_gesture = 0;
		self->drag_poly = -1;
		return FALSE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * The rule rows: 3x3 masks and outputs
 * ========================================================================== */

/* Paints a tile (index) into a box: the image slice or a swatch. */
static void
paint_tile (LrgEditorTilesetEditor *self,
            LrgPaintList           *list,
            guint                   value,
            gfloat                  x,
            gfloat                  y,
            gfloat                  size)
{
	guint        id = lrg_tile_get_id (value);
	GrlRectangle r;
	GrlColor     c;

	if (id == 0)
		return;
	if (self->tex != NULL && tile_rect (self, id - 1, &r))
	{
		GrlColor white = { 255, 255, 255, 255 };

		if (lrg_tile_get_mirrored (value))
		{
			r.x += r.width;
			r.width = -r.width;
		}
		lrg_paint_list_image (list, self->tex, &r, x, y, size, size, 2.0f, &white);
	}
	else
	{
		swatch (id, &c);
		lrg_paint_list_rect (list, x, y, size, size, 2.0f, &c);
	}
}

static LrgTileRule *
row_rule (LrgEditorTilesetEditor *self,
          LrgWidget              *widget)
{
	guint        index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "lrg-rule-index"));
	LrgRuleTile *rt = (self->ts != NULL && self->rule_name != NULL)
	                  ? lrg_tileset_get_rule_tile_by_name (self->ts, self->rule_name) : NULL;

	return (rt != NULL && index < lrg_rule_tile_get_n_rules (rt)) ? lrg_rule_tile_get_rule (rt, index) : NULL;
}

static void
mask_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	LrgTileRule            *rule = row_rule (self, LRG_WIDGET (widget));
	gfloat                  cell = MIN (width, height) / 3.0f;
	guint                   i;
	GrlColor                c;

	if (rule == NULL)
		return;
	/* the selected rule's mask is framed in the accent colour */
	if ((gint) GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "lrg-rule-index")) == self->rule_index)
		lrg_paint_list_border (list, x - 2.0f, y - 2.0f, cell * 3.0f + 4.0f, cell * 3.0f + 4.0f, 4.0f, 2.0f,
		                       lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
	for (i = 0; i < 9; i++)
	{
		gfloat           cx = x + (gfloat) (i % 3) * cell, cy = y + (gfloat) (i / 3) * cell;
		LrgTileRuleMatch m;

		if (i == 4)
		{
			lrg_paint_list_rect (list, cx + 1.0f, cy + 1.0f, cell - 2.0f, cell - 2.0f, 2.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
			continue;
		}
		m = lrg_tile_rule_get_neighbor (rule, mask_cells[i]);
		lrg_paint_list_rect (list, cx + 1.0f, cy + 1.0f, cell - 2.0f, cell - 2.0f, 2.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		if (m == LRG_TILE_RULE_MATCH_THIS)
		{
			lrg_paint_list_rect (list, cx + 3.0f, cy + 3.0f, cell - 6.0f, cell - 6.0f, 2.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
		}
		else if (m == LRG_TILE_RULE_MATCH_NOT_THIS)
		{
			const GrlColor *d = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);

			lrg_paint_list_line (list, cx + 4.0f, cy + 4.0f, cx + cell - 4.0f, cy + cell - 4.0f, 2.0f, d);
			lrg_paint_list_line (list, cx + cell - 4.0f, cy + 4.0f, cx + 4.0f, cy + cell - 4.0f, 2.0f, d);
		}
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.6f, &c);
		lrg_paint_list_border (list, cx + 1.0f, cy + 1.0f, cell - 2.0f, cell - 2.0f, 2.0f, 1.0f, &c);
	}
}

static gboolean
mask_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	guint                   index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "lrg-rule-index"));
	gfloat                  cell, rx, ry;
	guint                   i;
	g_autoptr(GError)       error = NULL;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) > 1)
		return FALSE;
	cell = MIN (lrg_widget_get_width (LRG_WIDGET (widget)), lrg_widget_get_height (LRG_WIDGET (widget))) / 3.0f;
	rx = lrg_ui_event_get_x (event) - lrg_widget_get_world_x (LRG_WIDGET (widget));
	ry = lrg_ui_event_get_y (event) - lrg_widget_get_world_y (LRG_WIDGET (widget));
	lrg_editor_tileset_editor_select_rule (self, (gint) index);
	if (rx < 0.0f || ry < 0.0f || rx >= 3.0f * cell || ry >= 3.0f * cell)
		return TRUE;
	i = (guint) (ry / cell) * 3u + (guint) (rx / cell);
	if (i == 4)
		return TRUE;
	lrg_editor_tileset_editor_cycle_rule_neighbor (self, self->rule_name, index, mask_cells[i],
	                                               lrg_ui_event_get_button (event) == 1, &error);
	report (self, error);
	return TRUE;
}

static void
outputs_measure (LrgCustomWidget     *widget,
                 gfloat               avail_width,
                 gfloat               avail_height,
                 LrgCustomWidgetSize *size,
                 gpointer             user_data)
{
	size->width = avail_width > 0.0f ? avail_width : 160.0f;
	size->height = 54.0f;
}

static void
outputs_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                *font = lrg_theme_get_ui_font (theme);
	gfloat                  fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	LrgTileRule            *rule = row_rule (self, LRG_WIDGET (widget));
	guint                   index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "lrg-rule-index"));
	gfloat                  box = height - 12.0f, total = 0.0f;
	guint                   i;

	if (rule == NULL)
		return;
	for (i = 0; i < lrg_tile_rule_get_n_outputs (rule); i++)
	{
		gfloat w;

		lrg_tile_rule_get_output (rule, i, &w);
		total += w;
	}
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; i < lrg_tile_rule_get_n_outputs (rule); i++)
	{
		gfloat            w;
		guint             v = lrg_tile_rule_get_output (rule, i, &w);
		gfloat            bx = x + (gfloat) i * (box + 8.0f);
		g_autofree gchar *pct = g_strdup_printf ("%.0f%%", total > 0.0f ? 100.0f * w / total : 0.0f);

		paint_tile (self, list, v, bx, y, box);
		if (lrg_tile_get_rotation (v) != 0)
			lrg_paint_list_icon (list, LRG_ICON_REFRESH, bx + box - 10.0f, y, 10.0f, 0.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
		if ((gint) index == self->rule_index && (gint) i == self->output_index)
			lrg_paint_list_border (list, bx - 2.0f, y - 2.0f, box + 4.0f, box + 4.0f, 3.0f, 2.0f,
			                       lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_paint_list_text (list, font, pct, bx, y + box + 1.0f, fs * 0.8f, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
outputs_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgTileRule            *rule = row_rule (self, LRG_WIDGET (widget));
	guint                   index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (widget), "lrg-rule-index"));
	gfloat                  box = lrg_widget_get_height (LRG_WIDGET (widget)) - 12.0f;
	gfloat                  rx;
	guint                   i;
	g_autoptr(GError)       error = NULL;

	if (rule == NULL || lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
		return FALSE;
	rx = lrg_ui_event_get_x (event) - lrg_widget_get_world_x (LRG_WIDGET (widget));
	i = (guint) MAX (0.0f, rx / (box + 8.0f));
	lrg_editor_tileset_editor_select_rule (self, (gint) index);
	if (i >= lrg_tile_rule_get_n_outputs (rule))
		return TRUE;
	if (lrg_ui_event_get_button (event) == 1)
	{
		lrg_editor_tileset_editor_remove_rule_output (self, self->rule_name, index, i, &error);
		report (self, error);
		return TRUE;
	}
	self->output_index = (gint) i;
	{
		gfloat w;

		lrg_tile_rule_get_output (rule, i, &w);
		self->syncing = TRUE;
		lrg_spin_box_set_value (self->weight, w);
		self->syncing = FALSE;
	}
	self->stale = TRUE;
	return TRUE;
}

/* ==========================================================================
 * The rule preview grid
 * ========================================================================== */

static void
preview_measure (LrgCustomWidget     *widget,
                 gfloat               avail_width,
                 gfloat               avail_height,
                 LrgCustomWidgetSize *size,
                 gpointer             user_data)
{
	gfloat w = avail_width > 0.0f ? MIN (avail_width, 320.0f) : 240.0f;

	size->width = w;
	size->height = w;
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
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                  cell = MIN (width, height) / (gfloat) PREVIEW_SIZE;
	GArray                 *cells = g_array_new (FALSE, FALSE, sizeof (gint));
	g_autofree guint       *map = NULL;
	guint                   i;
	GrlColor                c;

	lrg_paint_list_rect (list, x, y, cell * PREVIEW_SIZE, cell * PREVIEW_SIZE, 4.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	for (i = 0; i < PREVIEW_SIZE * PREVIEW_SIZE; i++)
		if (self->preview_mask[i])
		{
			gint xy[2] = { (gint) (i % PREVIEW_SIZE), (gint) (i / PREVIEW_SIZE) };

			g_array_append_vals (cells, xy, 2);
		}
	if (self->rule_name != NULL)
		map = lrg_editor_tileset_editor_preview (self, self->rule_name, (const gint *) cells->data, cells->len,
		                                         PREVIEW_SIZE, PREVIEW_SIZE);
	g_array_unref (cells);
	for (i = 0; i < PREVIEW_SIZE * PREVIEW_SIZE; i++)
	{
		gfloat cx = x + (gfloat) (i % PREVIEW_SIZE) * cell, cy = y + (gfloat) (i / PREVIEW_SIZE) * cell;

		if (map != NULL && map[i] != 0)
			paint_tile (self, list, map[i], cx, cy, cell);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.35f, &c);
		lrg_paint_list_border (list, cx, cy, cell, cell, 0.0f, 1.0f, &c);
	}
}

static gboolean
preview_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	gfloat                  cell = MIN (lrg_widget_get_width (LRG_WIDGET (widget)),
	                                    lrg_widget_get_height (LRG_WIDGET (widget))) / (gfloat) PREVIEW_SIZE;
	gfloat                  rx, ry;
	guint                   i;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || cell <= 0.0f)
		return FALSE;
	rx = lrg_ui_event_get_x (event) - lrg_widget_get_world_x (LRG_WIDGET (widget));
	ry = lrg_ui_event_get_y (event) - lrg_widget_get_world_y (LRG_WIDGET (widget));
	if (rx < 0.0f || ry < 0.0f || rx >= cell * PREVIEW_SIZE || ry >= cell * PREVIEW_SIZE)
		return FALSE;
	i = (guint) (ry / cell) * PREVIEW_SIZE + (guint) (rx / cell);
	self->preview_mask[i] = !self->preview_mask[i];
	return TRUE;
}

/* ==========================================================================
 * Pages
 * ========================================================================== */

/* Shows the Tile page or the Rule tiles page of the right pane. */
static void
set_page (LrgEditorTilesetEditor *self,
          gboolean                rules)
{
	if (self->page_tile == NULL)
		return;
	lrg_widget_set_visible (self->page_tile, !rules);
	lrg_widget_set_visible (self->page_rules, rules);
	editor_chip_set_checked (self->page_chips[0], !rules);
	editor_chip_set_checked (self->page_chips[1], rules);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_page (LrgWidget *chip,
         gpointer   user_data)
{
	set_page (LRG_EDITOR_TILESET_EDITOR (user_data),
	          GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (chip), "lrg-page")) == 1);
}

/* ==========================================================================
 * Widgets -> model
 * ========================================================================== */

static void
on_layout (LrgSpinBox *spin,
           gdouble     value,
           gpointer    user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->syncing || self->ts == NULL)
		return;
	if (spin == self->margin || spin == self->spacing || spin == self->off_x || spin == self->off_y)
		lrg_editor_tileset_editor_set_spacing (self, (guint) lrg_spin_box_get_value (self->margin),
		                                       (guint) lrg_spin_box_get_value (self->spacing),
		                                       (gint) lrg_spin_box_get_value (self->off_x),
		                                       (gint) lrg_spin_box_get_value (self->off_y), &error);
	else
		lrg_editor_tileset_editor_set_layout (self, NULL, NULL, (guint) lrg_spin_box_get_value (self->tile_w),
		                                      (guint) lrg_spin_box_get_value (self->tile_h),
		                                      (guint) lrg_spin_box_get_value (self->columns),
		                                      (gint) lrg_spin_box_get_value (self->count), &error);
	report (self, error);
}

static void
on_text_layout (LrgTextInput *input,
                gpointer      user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->syncing || self->ts == NULL)
		return;
	if (input == self->name_in)
		lrg_editor_tileset_editor_set_layout (self, lrg_text_input_get_text (input), NULL, 0, 0, 0, -1, &error);
	else
		lrg_editor_tileset_editor_set_layout (self, NULL, lrg_text_input_get_text (input), 0, 0, 0, -1, &error);
	report (self, error);
}

static void
on_flag (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgTileProperty         bit = (LrgTileProperty) GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (chip), "lrg-flag"));
	g_autoptr(GError)       error = NULL;

	if (self->ts == NULL || self->selected < 0)
		return;
	lrg_editor_tileset_editor_set_tile_flags (self, (guint) self->selected,
	                                          lrg_tileset_get_tile_properties (self->ts, (guint) self->selected) ^ bit,
	                                          &error);
	report (self, error);
}

static void
on_box (LrgWidget *chip,
        gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;
	gfloat                  w, h, box[8];

	if (self->ts == NULL || self->selected < 0)
		return;
	w = (gfloat) lrg_tileset_get_tile_width (self->ts);
	h = (gfloat) lrg_tileset_get_tile_height (self->ts);
	box[0] = 0.0f; box[1] = 0.0f;
	box[2] = w;    box[3] = 0.0f;
	box[4] = w;    box[5] = h;
	box[6] = 0.0f; box[7] = h;
	lrg_editor_tileset_editor_add_collision (self, (guint) self->selected, box, 8, &error);
	report (self, error);
}

static void
on_draw (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);

	self->draw_mode = !self->draw_mode;
	g_array_set_size (self->pending, 0);
	editor_chip_set_checked (chip, self->draw_mode);
}

static void
on_clear (LrgWidget *chip,
          gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->ts == NULL || self->selected < 0)
		return;
	lrg_editor_tileset_editor_remove_collision (self, (guint) self->selected, -1, &error);
	report (self, error);
}

static void
on_data_add (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;
	const gchar            *key = lrg_text_input_get_text (self->data_key);

	if (self->ts == NULL || self->selected < 0 || key == NULL || *key == '\0')
		return;
	if (lrg_editor_tileset_editor_set_tile_data (self, (guint) self->selected, key,
	                                             lrg_text_input_get_text (self->data_value) != NULL
	                                             ? lrg_text_input_get_text (self->data_value) : "", &error))
	{
		lrg_text_input_set_text (self->data_key, "");
		lrg_text_input_set_text (self->data_value, "");
	}
	report (self, error);
}

static void
on_data_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	on_data_add (NULL, user_data);
}

static void
on_data_remove (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	const gchar            *key = g_object_get_data (G_OBJECT (chip), "lrg-key");
	g_autoptr(GError)       error = NULL;

	if (self->selected >= 0 && key != NULL)
		lrg_editor_tileset_editor_set_tile_data (self, (guint) self->selected, key, NULL, &error);
	report (self, error);
}

/* The selected tile's frames as arrays (caller frees). */
static guint
current_frames (LrgEditorTilesetEditor  *self,
                guint                  **frames,
                guint                  **ms)
{
	guint n = lrg_tileset_get_tile_animation_length (self->ts, (guint) self->selected);
	guint i;

	*frames = g_new0 (guint, n + 1);
	*ms = g_new0 (guint, n + 1);
	for (i = 0; i < n; i++)
		lrg_tileset_get_tile_animation_frame (self->ts, (guint) self->selected, i, &(*frames)[i], &(*ms)[i]);
	return n;
}

static void
on_anim_add (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autofree guint       *frames = NULL;
	g_autofree guint       *ms = NULL;
	g_autoptr(GError)       error = NULL;
	guint                   n;

	if (self->ts == NULL || self->selected < 0)
		return;
	n = current_frames (self, &frames, &ms);
	/* a new animation starts with the tile itself; later frames follow
	 * the last one (the next tile of the sheet) */
	frames[n] = (n == 0) ? (guint) self->selected
	                     : MIN (frames[n - 1] + 1, lrg_tileset_get_tile_count (self->ts) - 1);
	ms[n] = (guint) MAX (1.0, lrg_spin_box_get_value (self->anim_ms));
	lrg_editor_tileset_editor_set_tile_animation (self, (guint) self->selected, frames, ms, n + 1, &error);
	report (self, error);
}

static void
on_anim_remove (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	guint                   which = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (chip), "lrg-frame"));
	g_autofree guint       *frames = NULL;
	g_autofree guint       *ms = NULL;
	g_autoptr(GError)       error = NULL;
	guint                   n, i;

	if (self->ts == NULL || self->selected < 0)
		return;
	n = current_frames (self, &frames, &ms);
	if (which >= n)
		return;
	for (i = which; i + 1 < n; i++)
	{
		frames[i] = frames[i + 1];
		ms[i] = ms[i + 1];
	}
	lrg_editor_tileset_editor_set_tile_animation (self, (guint) self->selected, frames, ms, n - 1, &error);
	report (self, error);
}

static void
on_rule_combo (LrgComboBox *combo,
               gpointer     user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);

	if (!self->syncing && lrg_combo_box_get_selected_text (combo) != NULL)
		lrg_editor_tileset_editor_select_rule_tile (self, lrg_combo_box_get_selected_text (combo));
}

/* New rule tile: "name" from the input, preset from the chip's data,
 * first tile = the sheet selection. */
static void
on_rule_new (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	const gchar            *preset = g_object_get_data (G_OBJECT (chip), "lrg-preset");
	const gchar            *text = lrg_text_input_get_text (self->rule_new);
	g_autofree gchar       *name = NULL;
	g_autoptr(GError)       error = NULL;
	guint                   i = 1;

	if (self->ts == NULL)
		return;
	if (text != NULL && *text != '\0')
		name = g_strdup (text);
	else
		do
			name = (g_free (name), g_strdup_printf ("rule%u", i++));
		while (lrg_tileset_get_rule_tile_by_name (self->ts, name) != NULL);
	if (lrg_editor_tileset_editor_add_rule_tile (self, name, preset, self->selected >= 0 ? (guint) self->selected : 0,
	                                             &error))
		lrg_text_input_set_text (self->rule_new, "");
	report (self, error);
}

static void
on_rule_remove (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->rule_name != NULL)
		lrg_editor_tileset_editor_remove_rule_tile (self, self->rule_name, &error);
	report (self, error);
}

static void
on_rule_seed (LrgSpinBox *spin,
              gdouble     value,
              gpointer    user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (!self->syncing && self->rule_name != NULL)
		lrg_editor_tileset_editor_set_rule_tile (self, self->rule_name, NULL, -1, (gint64) value, -1, &error);
	report (self, error);
}

static void
on_border (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	LrgRuleTile            *rt;
	g_autoptr(GError)       error = NULL;

	if (self->ts == NULL || self->rule_name == NULL ||
	    (rt = lrg_tileset_get_rule_tile_by_name (self->ts, self->rule_name)) == NULL)
		return;
	lrg_editor_tileset_editor_set_rule_tile (self, self->rule_name, NULL, -1, -1,
	                                         lrg_rule_tile_get_border_is_this (rt) ? 0 : 1, &error);
	report (self, error);
}

static void
on_default (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->rule_name != NULL && self->selected >= 0)
		lrg_editor_tileset_editor_set_rule_tile (self, self->rule_name, NULL, self->selected + 1, -1, -1, &error);
	report (self, error);
}

static void
on_rule_add (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->rule_name != NULL)
		lrg_editor_tileset_editor_add_rule (self, self->rule_name, self->rule_index >= 0 ? self->rule_index + 1 : -1, 0,
		                                    &error);
	report (self, error);
}

static void
on_output_add (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->rule_name != NULL && self->rule_index >= 0 && self->selected >= 0)
		lrg_editor_tileset_editor_add_rule_output (self, self->rule_name, (guint) self->rule_index,
		                                           (guint) self->selected + 1, (gfloat) lrg_spin_box_get_value (self->weight),
		                                           &error);
	report (self, error);
}

static void
on_weight (LrgSpinBox *spin,
           gdouble     value,
           gpointer    user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	g_autoptr(GError)       error = NULL;

	if (self->syncing || self->rule_name == NULL || self->rule_index < 0 || self->output_index < 0)
		return;
	lrg_editor_tileset_editor_set_rule_output_weight (self, self->rule_name, (guint) self->rule_index,
	                                                  (guint) self->output_index, (gfloat) value, &error);
	report (self, error);
}

static void
on_rule_row_action (LrgWidget *chip,
                    gpointer   user_data)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (user_data);
	guint                   index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (chip), "lrg-rule-index"));
	const gchar            *what = g_object_get_data (G_OBJECT (chip), "lrg-what");
	g_autoptr(GError)       error = NULL;

	if (self->rule_name == NULL || what == NULL)
		return;
	if (g_strcmp0 (what, "up") == 0 && index > 0)
		lrg_editor_tileset_editor_move_rule (self, self->rule_name, index, index - 1, &error);
	else if (g_strcmp0 (what, "down") == 0)
		lrg_editor_tileset_editor_move_rule (self, self->rule_name, index, index + 1, &error);
	else if (g_strcmp0 (what, "remove") == 0)
		lrg_editor_tileset_editor_remove_rule (self, self->rule_name, index, &error);
	else if (g_strcmp0 (what, "transform") == 0)
	{
		LrgTileRule *rule = row_rule (self, chip);

		if (rule != NULL)
			lrg_editor_tileset_editor_set_rule_transform (self, self->rule_name, index,
			                                              (LrgTileRuleTransform) (((gint) lrg_tile_rule_get_transform (rule) + 1) % 5),
			                                              &error);
	}
	report (self, error);
}

/* ==========================================================================
 * Model -> widgets
 * ========================================================================== */

static void
set_spin (LrgSpinBox *spin,
          gdouble     value)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (spin));
	LrgWidget *focus = LRG_IS_CANVAS (root) ? lrg_canvas_get_focused_widget (LRG_CANVAS (root)) : NULL;

	if (focus != NULL && (focus == LRG_WIDGET (spin) || lrg_widget_is_ancestor (focus, LRG_WIDGET (spin))))
		return;
	if (fabs (lrg_spin_box_get_value (spin) - value) > 1e-6)
		lrg_spin_box_set_value (spin, value);
}

static void
set_text (LrgTextInput *input,
          const gchar  *text)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (input));
	LrgWidget *focus = LRG_IS_CANVAS (root) ? lrg_canvas_get_focused_widget (LRG_CANVAS (root)) : NULL;

	if (focus == LRG_WIDGET (input))
		return;
	if (g_strcmp0 (lrg_text_input_get_text (input), text != NULL ? text : "") != 0)
		lrg_text_input_set_text (input, text != NULL ? text : "");
}

/* The tile section: title, flags, data rows, animation chips. */
static void
rebuild_tile (LrgEditorTilesetEditor *self)
{
	GString         *sig = g_string_new (NULL);
	g_auto(GStrv)    keys = NULL;
	guint            i, n, frame, ms;
	LrgTileProperty  props = LRG_TILE_PROPERTY_NONE;
	static const LrgTileProperty bits[3] = { LRG_TILE_PROPERTY_SOLID, LRG_TILE_PROPERTY_ANIMATED, LRG_TILE_PROPERTY_HAZARD };

	if (self->ts != NULL && self->selected >= 0)
	{
		props = lrg_tileset_get_tile_properties (self->ts, (guint) self->selected);
		keys = lrg_tileset_get_tile_data_keys (self->ts, (guint) self->selected);
		g_string_append_printf (sig, "%d|%u|", self->selected, (guint) props);
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			g_string_append_printf (sig, "%s=%s|", keys[i], lrg_tileset_get_tile_data (self->ts, (guint) self->selected, keys[i]));
		n = lrg_tileset_get_tile_animation_length (self->ts, (guint) self->selected);
		for (i = 0; i < n; i++)
			if (lrg_tileset_get_tile_animation_frame (self->ts, (guint) self->selected, i, &frame, &ms))
				g_string_append_printf (sig, "f%u:%u|", frame, ms);
	}
	for (i = 0; i < 3; i++)
		editor_chip_set_checked (self->flags[i], (props & bits[i]) != 0);
	{
		g_autofree gchar *title = (self->selected >= 0) ? g_strdup_printf ("Tile %d  ·  id %d in maps", self->selected,
		                                                                   self->selected + 1)
		                                                : g_strdup ("No tile selected");

		lrg_label_set_text (LRG_LABEL (self->tile_title), title);
	}
	if (g_strcmp0 (sig->str, self->sig_tile) == 0)
	{
		g_string_free (sig, TRUE);
		return;
	}
	g_free (self->sig_tile);
	self->sig_tile = g_string_free (sig, FALSE);

	/* data rows */
	lrg_container_remove_all (LRG_CONTAINER (self->data_rows));
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		g_autofree gchar *text = g_strdup_printf ("%s = %s", keys[i],
		                                          lrg_tileset_get_tile_data (self->ts, (guint) self->selected, keys[i]));
		g_autofree gchar *name = g_strdup_printf ("tileset-data-remove-%s", keys[i]);
		LrgWidget        *label = LRG_WIDGET (lrg_label_new (text));
		LrgWidget        *chip = editor_chip_new (name, LRG_ICON_TRASH, NULL, on_data_remove, self);

		lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
		lrg_widget_set_fill_weight (label, 1.0f);
		lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
		add_to (row, label);
		g_object_set_data_full (G_OBJECT (chip), "lrg-key", g_strdup (keys[i]), g_free);
		editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
		add_to (row, chip);
		add_to (self->data_rows, row);
	}

	/* animation chips */
	lrg_container_remove_all (LRG_CONTAINER (self->anim_rows));
	n = (self->ts != NULL && self->selected >= 0) ? lrg_tileset_get_tile_animation_length (self->ts, (guint) self->selected)
	                                              : 0;
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("tileset-frame-%u", i);
		g_autofree gchar *label = NULL;
		LrgWidget        *chip;

		lrg_tileset_get_tile_animation_frame (self->ts, (guint) self->selected, i, &frame, &ms);
		label = g_strdup_printf ("%u · %u ms", frame, ms);
		chip = editor_chip_new (name, LRG_ICON_CLOSE, label, on_anim_remove, self);
		g_object_set_data (G_OBJECT (chip), "lrg-frame", GUINT_TO_POINTER (i));
		add_to (self->anim_rows, chip);
	}
	if (n == 0)
		add_to (self->anim_rows, LRG_WIDGET (lrg_label_new ("not animated")));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* The rule tile combo and the rule rows. */
static void
rebuild_rules (LrgEditorTilesetEditor *self)
{
	LrgRuleTile *rt = (self->ts != NULL && self->rule_name != NULL)
	                  ? lrg_tileset_get_rule_tile_by_name (self->ts, self->rule_name) : NULL;
	GString     *sig = g_string_new (NULL);
	guint        i, n = (self->ts != NULL) ? lrg_tileset_get_n_rule_tiles (self->ts) : 0;

	/* the combo */
	for (i = 0; i < n; i++)
		g_string_append_printf (sig, "%s|", lrg_rule_tile_get_name (lrg_tileset_get_rule_tile (self->ts, i)));
	if (g_strcmp0 (sig->str, self->sig_combo) != 0)
	{
		g_free (self->sig_combo);
		self->sig_combo = g_strdup (sig->str);
		self->syncing = TRUE;
		lrg_combo_box_remove_all (self->rule_combo);
		for (i = 0; i < n; i++)
			lrg_combo_box_append (self->rule_combo, lrg_rule_tile_get_name (lrg_tileset_get_rule_tile (self->ts, i)));
		self->syncing = FALSE;
	}
	self->syncing = TRUE;
	for (i = 0; i < n; i++)
		if (g_strcmp0 (lrg_rule_tile_get_name (lrg_tileset_get_rule_tile (self->ts, i)), self->rule_name) == 0 &&
		    lrg_combo_box_get_selected (self->rule_combo) != (gint) i)
			lrg_combo_box_set_selected (self->rule_combo, (gint) i);
	if (rt != NULL)
		set_spin (self->rule_seed, lrg_rule_tile_get_seed (rt));
	editor_chip_set_checked (self->border_chip, rt != NULL && lrg_rule_tile_get_border_is_this (rt));
	self->syncing = FALSE;

	/* the rows: a signature of the rule tile's rule count and selection */
	g_string_truncate (sig, 0);
	g_string_append_printf (sig, "%s|%u|%d", self->rule_name != NULL ? self->rule_name : "",
	                        rt != NULL ? lrg_rule_tile_get_n_rules (rt) : 0, self->rule_index);
	if (g_strcmp0 (sig->str, self->sig_rules) == 0)
	{
		g_string_free (sig, TRUE);
		return;
	}
	g_free (self->sig_rules);
	self->sig_rules = g_string_free (sig, FALSE);
	lrg_container_remove_all (LRG_CONTAINER (self->rules_box));
	for (i = 0; rt != NULL && i < lrg_rule_tile_get_n_rules (rt); i++)
	{
		static const LrgCustomWidgetFuncs mask_funcs = { NULL, mask_paint, NULL, mask_event, { NULL } };
		static const LrgCustomWidgetFuncs out_funcs = { outputs_measure, outputs_paint, NULL, outputs_event, { NULL } };
		static const struct { const gchar *what; LrgIcon icon; } acts[] = {
			{ "transform", LRG_ICON_REFRESH }, { "up", LRG_ICON_CHEVRON_UP }, { "down", LRG_ICON_CHEVRON_DOWN },
			{ "remove", LRG_ICON_TRASH }
		};
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		LrgWidget        *mask = LRG_WIDGET (lrg_custom_widget_new ());
		LrgWidget        *outs = LRG_WIDGET (lrg_custom_widget_new ());
		g_autofree gchar *mname = g_strdup_printf ("tileset-rule-mask-%u", i);
		g_autofree gchar *oname = g_strdup_printf ("tileset-rule-outputs-%u", i);
		guint             a;

		lrg_container_set_spacing (LRG_CONTAINER (row), 8.0f);
		lrg_widget_set_name (mask, mname);
		lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (mask), 54.0f);
		lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (mask), 54.0f);
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (mask), &mask_funcs, self, NULL);
		g_object_set_data (G_OBJECT (mask), "lrg-rule-index", GUINT_TO_POINTER (i));
		add_to (row, mask);
		lrg_widget_set_name (outs, oname);
		lrg_widget_set_fill_weight (outs, 1.0f);
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (outs), &out_funcs, self, NULL);
		g_object_set_data (G_OBJECT (outs), "lrg-rule-index", GUINT_TO_POINTER (i));
		add_to (row, outs);
		for (a = 0; a < G_N_ELEMENTS (acts); a++)
		{
			g_autofree gchar *cname = g_strdup_printf ("tileset-rule-%s-%u", acts[a].what, i);
			LrgWidget        *chip;

			if (a == 0)
			{
				LrgTileRule *rule = lrg_rule_tile_get_rule (rt, i);

				chip = editor_chip_new (cname, acts[a].icon,
				                        transform_labels[MIN ((guint) lrg_tile_rule_get_transform (rule), 4u)],
				                        on_rule_row_action, self);
			}
			else
				chip = editor_chip_new (cname, acts[a].icon, NULL, on_rule_row_action, self);
			g_object_set_data (G_OBJECT (chip), "lrg-rule-index", GUINT_TO_POINTER (i));
			g_object_set_data (G_OBJECT (chip), "lrg-what", (gpointer) acts[a].what);
			if (a == 3)
				editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
			lrg_widget_set_valign (chip, LRG_ALIGN_CENTER);
			add_to (row, chip);
		}
		if ((gint) i == self->rule_index)
			lrg_widget_add_class (row, "selected");
		add_to (self->rules_box, row);
	}
	if (rt == NULL)
		add_to (self->rules_box, editor_panel_hint_new ("tileset-rules-empty",
		                                                "No rule tile: name one and press New, 16 or 47"));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
sync_widgets (LrgEditorTilesetEditor *self)
{
	self->stale = FALSE;
	self->syncing = TRUE;
	if (self->ts != NULL)
	{
		gint ox, oy;

		set_text (self->name_in, lrg_tileset_get_name (self->ts));
		set_text (self->image_in, lrg_tileset_get_image_path (self->ts));
		set_spin (self->tile_w, lrg_tileset_get_tile_width (self->ts));
		set_spin (self->tile_h, lrg_tileset_get_tile_height (self->ts));
		set_spin (self->columns, lrg_tileset_get_columns (self->ts));
		set_spin (self->count, lrg_tileset_get_tile_count (self->ts));
		set_spin (self->margin, lrg_tileset_get_margin (self->ts));
		set_spin (self->spacing, lrg_tileset_get_spacing (self->ts));
		lrg_tileset_get_draw_offset (self->ts, &ox, &oy);
		set_spin (self->off_x, ox);
		set_spin (self->off_y, oy);
	}
	self->syncing = FALSE;
	rebuild_tile (self);
	rebuild_rules (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * LrgEditorDataEditor vfuncs
 * ========================================================================== */

static void
tileset_refresh (LrgEditorDataEditor *editor,
                 const gchar         *path)
{
	load_model (LRG_EDITOR_TILESET_EDITOR (editor));
}

static void
tileset_validate (LrgEditorDataEditor *editor,
                  GPtrArray           *issues)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (editor);
	guint                   i;

	if (self->ts == NULL)
	{
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 0, 0,
		                                                        self->parse_error != NULL ? self->parse_error
		                                                                                  : "the tileset does not load"));
		return;
	}
	if (self->image_path != NULL && !g_file_test (self->image_path, G_FILE_TEST_IS_REGULAR))
	{
		g_autofree gchar *msg = g_strdup_printf ("the image %s does not exist", lrg_tileset_get_image_path (self->ts));

		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, "/image", 0, 0, msg));
	}
	for (i = 0; i < lrg_tileset_get_n_rule_tiles (self->ts); i++)
	{
		LrgRuleTile *rt = lrg_tileset_get_rule_tile (self->ts, i);

		if (lrg_rule_tile_get_n_rules (rt) == 0)
		{
			g_autofree gchar *msg = g_strdup_printf ("rule tile \"%s\" has no rules and paints nothing",
			                                         lrg_rule_tile_get_name (rt));
			g_autofree gchar *path = g_strdup_printf ("/rule_tiles/%u", i);

			g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, 0, 0, msg));
		}
	}
}

static gchar *
tileset_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (editor);

	return g_strdup_printf ("tile %d, rule tile %s, rule %d", self->selected,
	                        self->rule_name != NULL ? self->rule_name : "-", self->rule_index);
}

static void
tileset_reveal (LrgEditorDataEditor *editor,
                const gchar         *path,
                guint                line)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (editor);
	guint                   index;

	if (path != NULL && sscanf (path, "/rule_tiles/%u", &index) == 1 && self->ts != NULL &&
	    index < lrg_tileset_get_n_rule_tiles (self->ts))
		lrg_editor_tileset_editor_select_rule_tile (self, lrg_rule_tile_get_name (lrg_tileset_get_rule_tile (self->ts, index)));
	else if (path != NULL && sscanf (path, "/tiles/%u", &index) == 1)
	{
		g_autofree gchar *id_path = g_strdup_printf ("/tiles/%u/id", index);
		const gchar      *id = lrg_editor_document_get_scalar (doc_of (self), id_path);

		if (id != NULL)
			lrg_editor_tileset_editor_set_selected_tile (self, (gint) g_ascii_strtoll (id, NULL, 10));
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gboolean
lrg_editor_tileset_editor_step (LrgWidget *widget,
                                gfloat     delta_time)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (widget);
	gboolean                busy = LRG_WIDGET_CLASS (lrg_editor_tileset_editor_parent_class)->step (widget, delta_time);

	if (self->ts != NULL && self->tex == NULL && IsWindowReady () && self->image_path != NULL)
		self->img_mtime = -1;   /* a window opened: load the texture */
	if (self->ts != NULL)
		sync_image (self);
	if (self->stale)
		sync_widgets (self);
	return busy;
}

static LrgWidget *
heading (const gchar *text)
{
	LrgLabel       *label = lrg_label_new (text);
	const LrgInsets ins = { 0.0f, 8.0f, 0.0f, 2.0f };

	lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	lrg_widget_set_margin (LRG_WIDGET (label), &ins);
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
spin_in (LrgEditorTilesetEditor *self,
         LrgWidget              *row,
         const gchar            *label,
         const gchar            *name,
         gdouble                 min,
         gdouble                 max,
         GCallback               callback)
{
	LrgSpinBox *spin = lrg_spin_box_new_with_range (min, max, 1.0);

	if (label != NULL)
	{
		LrgWidget *l = LRG_WIDGET (lrg_label_new (label));

		lrg_widget_set_valign (l, LRG_ALIGN_CENTER);
		add_to (row, l);
	}
	lrg_spin_box_set_digits (spin, 0);
	lrg_widget_set_name (LRG_WIDGET (spin), name);
	lrg_widget_set_fill_weight (LRG_WIDGET (spin), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (spin), 56.0f, 0.0f);
	if (callback != NULL)
		g_signal_connect (spin, "value-changed", callback, self);
	add_to (row, LRG_WIDGET (spin));
	return spin;
}

static LrgTextInput *
text_in (LrgEditorTilesetEditor *self,
         LrgWidget              *row,
         const gchar            *placeholder,
         const gchar            *name,
         GCallback               submitted)
{
	LrgTextInput *input = lrg_text_input_new_with_placeholder (placeholder);

	lrg_widget_set_name (LRG_WIDGET (input), name);
	lrg_widget_set_fill_weight (LRG_WIDGET (input), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (input), 70.0f, 0.0f);
	if (submitted != NULL)
		g_signal_connect (input, "submitted", submitted, self);
	add_to (row, LRG_WIDGET (input));
	return input;
}

static void
lrg_editor_tileset_editor_constructed (GObject *object)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (object);
	LrgEditorDataEditor    *base = LRG_EDITOR_DATA_EDITOR (object);
	static const LrgCustomWidgetFuncs sheet_funcs = { NULL, sheet_paint, NULL, sheet_event, { NULL } };
	static const LrgCustomWidgetFuncs poly_funcs = { NULL, poly_paint, NULL, poly_event, { NULL } };
	static const LrgCustomWidgetFuncs prev_funcs = { preview_measure, preview_paint, NULL, preview_event, { NULL } };
	static const gchar * const flag_names[3] = { "Solid", "Animated", "Hazard" };
	static const LrgTileProperty flag_bits[3] = { LRG_TILE_PROPERTY_SOLID, LRG_TILE_PROPERTY_ANIMATED,
	                                              LRG_TILE_PROPERTY_HAZARD };
	LrgSplitter            *split;
	LrgWidget              *left, *right, *scroll, *row, *chip;
	guint                   i;

	G_OBJECT_CLASS (lrg_editor_tileset_editor_parent_class)->constructed (object);

	/* Left: the sheet and the layout */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	self->sheet = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->sheet, "tileset-sheet");
	lrg_widget_set_fill_weight (self->sheet, 1.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->sheet), 240.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->sheet), &sheet_funcs, self, NULL);
	add_to (left, self->sheet);
	add_to (left, heading ("Layout"));
	row = hrow ();
	self->name_in = text_in (self, row, "name", "tileset-name", G_CALLBACK (on_text_layout));
	self->image_in = text_in (self, row, "image.png", "tileset-image", G_CALLBACK (on_text_layout));
	add_to (left, row);
	row = hrow ();
	self->tile_w = spin_in (self, row, "Tile", "tileset-tile-width", 1.0, 65536.0, G_CALLBACK (on_layout));
	self->tile_h = spin_in (self, row, "x", "tileset-tile-height", 1.0, 65536.0, G_CALLBACK (on_layout));
	self->columns = spin_in (self, row, "Columns", "tileset-columns", 1.0, 65536.0, G_CALLBACK (on_layout));
	add_to (left, row);
	row = hrow ();
	self->count = spin_in (self, row, "Count", "tileset-count", 0.0, 16777216.0, G_CALLBACK (on_layout));
	self->margin = spin_in (self, row, "Margin", "tileset-margin", 0.0, 4096.0, G_CALLBACK (on_layout));
	self->spacing = spin_in (self, row, "Spacing", "tileset-spacing", 0.0, 4096.0, G_CALLBACK (on_layout));
	add_to (left, row);
	row = hrow ();
	self->off_x = spin_in (self, row, "Draw offset", "tileset-offset-x", -4096.0, 4096.0, G_CALLBACK (on_layout));
	self->off_y = spin_in (self, row, "x", "tileset-offset-y", -4096.0, 4096.0, G_CALLBACK (on_layout));
	add_to (left, row);

	/* Right: the tile's properties and the rule tiles */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (right, "tileset-properties");
	lrg_container_set_spacing (LRG_CONTAINER (right), 6.0f);
	/* Two pages: the selected tile's properties and the rule tiles */
	row = hrow ();
	self->page_chips[0] = editor_chip_new ("tileset-page-tile", LRG_ICON_EDIT, "Tile", on_page, self);
	g_object_set_data (G_OBJECT (self->page_chips[0]), "lrg-page", GUINT_TO_POINTER (0));
	add_to (row, self->page_chips[0]);
	self->page_chips[1] = editor_chip_new ("tileset-page-rules", LRG_ICON_STAR, "Rule tiles", on_page, self);
	g_object_set_data (G_OBJECT (self->page_chips[1]), "lrg-page", GUINT_TO_POINTER (1));
	add_to (row, self->page_chips[1]);
	add_to (right, row);
	self->page_tile = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->page_tile, "tileset-page-tile-box");
	lrg_container_set_spacing (LRG_CONTAINER (self->page_tile), 6.0f);
	self->page_rules = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->page_rules, "tileset-page-rules-box");
	lrg_container_set_spacing (LRG_CONTAINER (self->page_rules), 6.0f);
	self->tile_title = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->tile_title, "tileset-tile-title");
	add_to (self->page_tile, self->tile_title);
	row = hrow ();
	for (i = 0; i < 3; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("tileset-flag-%s", flag_names[i]);

		self->flags[i] = editor_chip_new (name, LRG_ICON_CHECK, flag_names[i], on_flag, self);
		g_object_set_data (G_OBJECT (self->flags[i]), "lrg-flag", GUINT_TO_POINTER ((guint) flag_bits[i]));
		add_to (row, self->flags[i]);
	}
	add_to (self->page_tile, row);

	add_to (self->page_tile, heading ("Collision"));
	row = hrow ();
	add_to (row, editor_chip_new ("tileset-collision-box", LRG_ICON_PLUS, "Box", on_box, self));
	self->draw_chip = editor_chip_new ("tileset-collision-draw", LRG_ICON_EDIT, "Draw", on_draw, self);
	add_to (row, self->draw_chip);
	chip = editor_chip_new ("tileset-collision-clear", LRG_ICON_TRASH, "Clear", on_clear, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (row, chip);
	add_to (self->page_tile, row);
	self->poly = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->poly, "tileset-collision");
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->poly), 220.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->poly), &poly_funcs, self, NULL);
	add_to (self->page_tile, self->poly);

	add_to (self->page_tile, heading ("Custom data"));
	self->data_rows = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->data_rows, "tileset-data");
	lrg_container_set_spacing (LRG_CONTAINER (self->data_rows), 3.0f);
	add_to (self->page_tile, self->data_rows);
	row = hrow ();
	self->data_key = text_in (self, row, "key", "tileset-data-key", G_CALLBACK (on_data_submitted));
	self->data_value = text_in (self, row, "value", "tileset-data-value", G_CALLBACK (on_data_submitted));
	add_to (row, editor_chip_new ("tileset-data-add", LRG_ICON_PLUS, "Set", on_data_add, self));
	add_to (self->page_tile, row);

	add_to (self->page_tile, heading ("Animation"));
	self->anim_rows = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->anim_rows, "tileset-animation");
	lrg_container_set_spacing (LRG_CONTAINER (self->anim_rows), 4.0f);
	add_to (self->page_tile, self->anim_rows);
	row = hrow ();
	add_to (row, editor_chip_new ("tileset-frame-add", LRG_ICON_PLUS, "Frame", on_anim_add, self));
	self->anim_ms = spin_in (self, row, "ms", "tileset-frame-ms", 1.0, 60000.0, NULL);
	lrg_spin_box_set_value (self->anim_ms, 150.0);
	add_to (self->page_tile, row);

	add_to (self->page_rules, heading ("Rule tiles"));
	row = hrow ();
	self->rule_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->rule_combo), "tileset-rule-tile");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->rule_combo), 1.0f);
	lrg_widget_set_min_size (LRG_WIDGET (self->rule_combo), 90.0f, 0.0f);
	lrg_combo_box_set_placeholder (self->rule_combo, "(none)");
	g_signal_connect (self->rule_combo, "changed", G_CALLBACK (on_rule_combo), self);
	add_to (row, LRG_WIDGET (self->rule_combo));
	chip = editor_chip_new ("tileset-rule-tile-remove", LRG_ICON_TRASH, NULL, on_rule_remove, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (row, chip);
	add_to (self->page_rules, row);
	row = hrow ();
	self->rule_new = text_in (self, row, "new rule tile name", "tileset-rule-tile-name", NULL);
	add_to (row, editor_chip_new ("tileset-rule-tile-new", LRG_ICON_PLUS, "New", on_rule_new, self));
	chip = editor_chip_new ("tileset-rule-tile-edges16", LRG_ICON_STAR, "16", on_rule_new, self);
	g_object_set_data (G_OBJECT (chip), "lrg-preset", (gpointer) "edges16");
	add_to (row, chip);
	chip = editor_chip_new ("tileset-rule-tile-blob47", LRG_ICON_STAR, "47", on_rule_new, self);
	g_object_set_data (G_OBJECT (chip), "lrg-preset", (gpointer) "blob47");
	add_to (row, chip);
	add_to (self->page_rules, row);
	row = hrow ();
	self->rule_seed = spin_in (self, row, "Seed", "tileset-rule-seed", 0.0, 4294967295.0, G_CALLBACK (on_rule_seed));
	self->border_chip = editor_chip_new ("tileset-rule-border", LRG_ICON_CHECK, "Border is this", on_border, self);
	add_to (row, self->border_chip);
	add_to (row, editor_chip_new ("tileset-rule-default", LRG_ICON_STAR, "Default = tile", on_default, self));
	add_to (self->page_rules, row);
	self->rules_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->rules_box, "tileset-rules");
	lrg_container_set_spacing (LRG_CONTAINER (self->rules_box), 4.0f);
	add_to (self->page_rules, self->rules_box);
	row = hrow ();
	add_to (row, editor_chip_new ("tileset-rule-add", LRG_ICON_PLUS, "Rule", on_rule_add, self));
	add_to (row, editor_chip_new ("tileset-output-add", LRG_ICON_PLUS, "Output = tile", on_output_add, self));
	self->weight = spin_in (self, row, "Weight", "tileset-output-weight", 0.01, 1000.0, G_CALLBACK (on_weight));
	lrg_spin_box_set_digits (self->weight, 2);
	lrg_spin_box_set_step (self->weight, 0.05);
	lrg_spin_box_set_value (self->weight, 1.0);
	add_to (self->page_rules, row);
	add_to (self->page_rules, heading ("Preview (click cells to paint)"));
	self->preview = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->preview, "tileset-preview");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->preview), &prev_funcs, self, NULL);
	add_to (self->page_rules, self->preview);

	add_to (right, self->page_tile);
	add_to (right, self->page_rules);
	set_page (self, FALSE);

	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "tileset-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), right);
	g_object_unref (right);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (split), "tileset-split");
	lrg_splitter_set_start_child (split, left);
	g_object_unref (left);
	lrg_splitter_set_end_child (split, scroll);
	g_object_unref (scroll);
	lrg_splitter_set_default_fraction (split, 0.55f);
	lrg_splitter_set_fraction (split, 0.55f, FALSE);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));
	g_object_unref (split);

	load_model (self);
	if (self->ts != NULL && lrg_tileset_get_tile_count (self->ts) > 0)
		self->selected = 0;
	sync_widgets (self);
}

static void
lrg_editor_tileset_editor_dispose (GObject *object)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (object);

	g_clear_object (&self->ts);
	g_clear_object (&self->tex);
	G_OBJECT_CLASS (lrg_editor_tileset_editor_parent_class)->dispose (object);
}

static void
lrg_editor_tileset_editor_finalize (GObject *object)
{
	LrgEditorTilesetEditor *self = LRG_EDITOR_TILESET_EDITOR (object);

	g_free (self->parse_error);
	g_free (self->rule_name);
	g_free (self->image_path);
	g_free (self->sig_tile);
	g_free (self->sig_rules);
	g_free (self->sig_combo);
	g_array_unref (self->pending);
	G_OBJECT_CLASS (lrg_editor_tileset_editor_parent_class)->finalize (object);
}

static void
lrg_editor_tileset_editor_class_init (LrgEditorTilesetEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_tileset_editor_constructed;
	object_class->dispose = lrg_editor_tileset_editor_dispose;
	object_class->finalize = lrg_editor_tileset_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_tileset_editor_step;
	data_class->refresh = tileset_refresh;
	data_class->validate = tileset_validate;
	data_class->get_state = tileset_get_state;
	data_class->reveal = tileset_reveal;
}

static void
lrg_editor_tileset_editor_init (LrgEditorTilesetEditor *self)
{
	self->selected = -1;
	self->rule_index = -1;
	self->output_index = -1;
	self->drag_poly = -1;
	self->hover = -1;
	self->zoom = 1.0f;
	self->pending = g_array_new (FALSE, FALSE, sizeof (gfloat));
}

/* ==========================================================================
 * Construction, opening, creating
 * ========================================================================== */

LrgEditorTilesetEditor *
lrg_editor_tileset_editor_new (LrgEditorApp      *app,
                               LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_TILESET_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorTilesetEditor *
lrg_editor_tileset_editor_open (LrgEditorApp  *app,
                                const gchar   *path,
                                GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_TILESET_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a tileset (.rtileset)", path);
		return NULL;
	}
	return LRG_EDITOR_TILESET_EDITOR (e);
}

gboolean
lrg_editor_tileset_editor_create (LrgEditorApp  *app,
                                  const gchar   *path,
                                  const gchar   *image,
                                  guint          tile_width,
                                  guint          tile_height,
                                  const gchar   *name,
                                  GError       **error)
{
	g_autofree gchar      *abs = NULL;
	g_autofree gchar      *dir = NULL;
	g_autofree gchar      *base = NULL;
	g_autofree gchar      *rel_image = NULL;
	g_autoptr(LrgTileset)  ts = NULL;
	guint                  cols = 8, rows = 8;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	tile_width = tile_width > 0 ? tile_width : 16;
	tile_height = tile_height > 0 ? tile_height : 16;
	abs = editor_app_project_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	g_mkdir_with_parents (dir, 0755);
	base = g_path_get_basename (abs);
	if (strrchr (base, '.') != NULL && strrchr (base, '.') != base)
		*strrchr (base, '.') = '\0';

	/* the grid from the image's size */
	if (image != NULL && *image != '\0')
	{
		g_autofree gchar *img_abs = editor_app_project_path (app, image);
		g_autoptr(GFile)  from = g_file_new_for_path (dir);
		g_autoptr(GFile)  to = g_file_new_for_path (img_abs);
		Image             img;

		rel_image = g_file_get_relative_path (from, to);
		if (rel_image == NULL)
			rel_image = g_strdup (img_abs);
		img = LoadImage (img_abs);
		if (img.width > 0 && img.height > 0)
		{
			cols = MAX (1u, (guint) img.width / tile_width);
			rows = MAX (1u, (guint) img.height / tile_height);
		}
		UnloadImage (img);
	}
	ts = lrg_tileset_new_empty (tile_width, tile_height, cols, cols * rows);
	lrg_tileset_set_name (ts, name != NULL && *name != '\0' ? name : base);
	if (rel_image != NULL)
		lrg_tileset_set_image_path (ts, rel_image);
	return lrg_tileset_save_to_file (ts, abs, error);
}

/* ==========================================================================
 * IPC: tileset.*
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
fail (GError **error,
      GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && cause->domain == LRG_EDITOR_DOCUMENT_ERROR && cause->code == LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND)
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
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

static JsonArray *
param_array (JsonObject  *params,
             const gchar *name)
{
	JsonNode *n = (params != NULL) ? json_object_get_member (params, name) : NULL;

	return (n != NULL && JSON_NODE_HOLDS_ARRAY (n)) ? json_node_get_array (n) : NULL;
}

static JsonArray *
pair (gdouble a,
      gdouble b)
{
	JsonArray *arr = json_array_new ();

	json_array_add_double_element (arr, a);
	json_array_add_double_element (arr, b);
	return arr;
}

/* The editor for "path" (opened if needed), else the focused tileset
 * editor, else the first open one. */
static LrgEditorTilesetEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar            *path = param_string (params, "path");
	LrgEditorTilesetEditor *self = NULL;
	g_autoptr(GError)       err = NULL;

	if (path != NULL)
	{
		self = lrg_editor_tileset_editor_open (app, path, &err);
		if (self == NULL)
		{
			fail (error, err);
			return NULL;
		}
	}
	else
	{
		LrgEditorDataEditor *focus = lrg_editor_data_editor_find (app, NULL);
		GList               *all, *l;

		if (focus != NULL && LRG_IS_EDITOR_TILESET_EDITOR (focus))
			self = LRG_EDITOR_TILESET_EDITOR (focus);
		all = lrg_editor_data_editor_list (app);
		for (l = all; self == NULL && l != NULL; l = l->next)
			if (LRG_IS_EDITOR_TILESET_EDITOR (l->data))
				self = LRG_EDITOR_TILESET_EDITOR (l->data);
		g_list_free (all);
		if (self == NULL)
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			                     "No tileset editor is open (pass \"path\")");
			return NULL;
		}
	}
	return self;
}

/* ipc_editor() and the parsed tileset (edits need it to load). */
static LrgEditorTilesetEditor *
ipc_loaded (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	LrgEditorTilesetEditor *self = ipc_editor (app, params, error);

	if (self != NULL && self->ts == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "the tileset does not load: %s",
		             self->parse_error != NULL ? self->parse_error : "unknown error");
		return NULL;
	}
	return self;
}

static JsonObject *
tile_json (LrgTileset *ts,
           guint       index)
{
	JsonObject     *o = json_object_new ();
	JsonArray      *flags = json_array_new ();
	JsonArray      *coll = json_array_new ();
	JsonObject     *data = json_object_new ();
	JsonArray      *anim = json_array_new ();
	LrgTileProperty props = lrg_tileset_get_tile_properties (ts, index);
	g_auto(GStrv)   keys = lrg_tileset_get_tile_data_keys (ts, index);
	guint           p, k, n, frame, ms;

	json_object_set_int_member (o, "id", index);
	if (props & LRG_TILE_PROPERTY_SOLID)
		json_array_add_string_element (flags, "solid");
	if (props & LRG_TILE_PROPERTY_ANIMATED)
		json_array_add_string_element (flags, "animated");
	if (props & LRG_TILE_PROPERTY_HAZARD)
		json_array_add_string_element (flags, "hazard");
	for (p = 0; p < lrg_tileset_get_tile_collision_polygon_count (ts, index); p++)
	{
		const gfloat *pts = lrg_tileset_get_tile_collision_polygon (ts, index, p, &n);
		JsonArray    *poly = json_array_new ();

		for (k = 0; k < n; k++)
			json_array_add_double_element (poly, pts[k]);
		json_array_add_array_element (coll, poly);
	}
	for (k = 0; keys != NULL && keys[k] != NULL; k++)
		json_object_set_string_member (data, keys[k], lrg_tileset_get_tile_data (ts, index, keys[k]));
	for (k = 0; k < lrg_tileset_get_tile_animation_length (ts, index); k++)
		if (lrg_tileset_get_tile_animation_frame (ts, index, k, &frame, &ms))
		{
			JsonObject *f = json_object_new ();

			json_object_set_int_member (f, "tile", frame);
			json_object_set_int_member (f, "duration", ms);
			json_array_add_object_element (anim, f);
		}
	json_object_set_array_member (o, "flags", flags);
	json_object_set_array_member (o, "collision", coll);
	json_object_set_object_member (o, "data", data);
	json_object_set_array_member (o, "animation", anim);
	return o;
}

static gboolean
tile_has_something (LrgTileset *ts,
                    guint       index)
{
	g_auto(GStrv) keys = lrg_tileset_get_tile_data_keys (ts, index);

	return lrg_tileset_get_tile_properties (ts, index) != LRG_TILE_PROPERTY_NONE ||
	       lrg_tileset_get_tile_collision_polygon_count (ts, index) > 0 ||
	       lrg_tileset_get_tile_animation_length (ts, index) > 0 || (keys != NULL && keys[0] != NULL);
}

static JsonObject *
output_json (guint  value,
             gfloat weight)
{
	JsonObject *o = json_object_new ();

	json_object_set_int_member (o, "tile", lrg_tile_get_id (value) > 0 ? (gint64) lrg_tile_get_id (value) - 1 : -1);
	json_object_set_double_member (o, "weight", weight);
	json_object_set_int_member (o, "rotate", lrg_tile_get_rotation (value));
	json_object_set_boolean_member (o, "flip", lrg_tile_get_mirrored (value));
	return o;
}

static JsonObject *
rule_tile_json (LrgRuleTile *rt)
{
	JsonObject   *o = json_object_new ();
	JsonArray    *rules = json_array_new ();
	JsonArray    *match = json_array_new ();
	const guint  *mt;
	guint         n_mt = 0, i, k;

	json_object_set_string_member (o, "name", lrg_rule_tile_get_name (rt));
	json_object_set_int_member (o, "default", lrg_rule_tile_get_default_tile (rt) > 0
	                                          ? (gint64) lrg_tile_get_id (lrg_rule_tile_get_default_tile (rt)) - 1 : -1);
	json_object_set_int_member (o, "seed", lrg_rule_tile_get_seed (rt));
	json_object_set_boolean_member (o, "border_is_this", lrg_rule_tile_get_border_is_this (rt));
	mt = lrg_rule_tile_get_match_tiles (rt, &n_mt);
	for (i = 0; i < n_mt; i++)
		json_array_add_int_element (match, (gint64) lrg_tile_get_id (mt[i]) - 1);
	json_object_set_array_member (o, "match_tiles", match);
	for (i = 0; i < lrg_rule_tile_get_n_rules (rt); i++)
	{
		LrgTileRule *rule = lrg_rule_tile_get_rule (rt, i);
		JsonObject  *r = json_object_new ();
		JsonArray   *pattern = json_array_new ();
		JsonArray   *tiles = json_array_new ();
		guint        row;

		for (row = 0; row < 3; row++)
		{
			gchar text[4] = { 0 };

			for (k = 0; k < 3; k++)
			{
				LrgTileRuleMatch m;

				if (row == 1 && k == 1)
				{
					text[k] = '@';
					continue;
				}
				m = lrg_tile_rule_get_neighbor (rule, mask_cells[row * 3 + k]);
				text[k] = (m == LRG_TILE_RULE_MATCH_THIS) ? '#' : (m == LRG_TILE_RULE_MATCH_NOT_THIS) ? '-' : '.';
			}
			json_array_add_string_element (pattern, text);
		}
		json_object_set_array_member (r, "pattern", pattern);
		json_object_set_string_member (r, "transform", transform_nicks[MIN ((guint) lrg_tile_rule_get_transform (rule), 4u)]);
		for (k = 0; k < lrg_tile_rule_get_n_outputs (rule); k++)
		{
			gfloat w;
			guint  v = lrg_tile_rule_get_output (rule, k, &w);

			json_array_add_object_element (tiles, output_json (v, w));
		}
		json_object_set_array_member (r, "tiles", tiles);
		json_array_add_object_element (rules, r);
	}
	json_object_set_array_member (o, "rules", rules);
	return o;
}

static JsonObject *
editor_json (LrgEditorTilesetEditor *self)
{
	JsonObject *o = json_object_new ();
	JsonArray  *tiles = json_array_new ();
	JsonArray  *rts = json_array_new ();
	JsonArray  *issues = json_array_new ();
	GPtrArray  *list = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (self));
	guint       i;
	gint        ox, oy;

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_int_member (o, "selected", self->selected);
	if (self->rule_name != NULL)
		json_object_set_string_member (o, "selected_rule_tile", self->rule_name);
	else
		json_object_set_null_member (o, "selected_rule_tile");
	json_object_set_int_member (o, "selected_rule", self->rule_index);
	if (self->parse_error != NULL)
		json_object_set_string_member (o, "error", self->parse_error);
	for (i = 0; list != NULL && i < list->len; i++)
	{
		const LrgEditorDocumentIssue *issue = g_ptr_array_index (list, i);

		json_array_add_string_element (issues, issue->message);
	}
	json_object_set_array_member (o, "issues", issues);
	if (self->ts == NULL)
		return o;
	json_object_set_string_member (o, "name", lrg_tileset_get_name (self->ts) != NULL ? lrg_tileset_get_name (self->ts) : "");
	json_object_set_string_member (o, "image", lrg_tileset_get_image_path (self->ts) != NULL
	                                           ? lrg_tileset_get_image_path (self->ts) : "");
	json_object_set_array_member (o, "tile_size", pair (lrg_tileset_get_tile_width (self->ts),
	                                                    lrg_tileset_get_tile_height (self->ts)));
	json_object_set_int_member (o, "columns", lrg_tileset_get_columns (self->ts));
	json_object_set_int_member (o, "tile_count", lrg_tileset_get_tile_count (self->ts));
	json_object_set_int_member (o, "margin", lrg_tileset_get_margin (self->ts));
	json_object_set_int_member (o, "spacing", lrg_tileset_get_spacing (self->ts));
	lrg_tileset_get_draw_offset (self->ts, &ox, &oy);
	json_object_set_array_member (o, "offset", pair (ox, oy));
	for (i = 0; i < lrg_tileset_get_tile_count (self->ts); i++)
		if (tile_has_something (self->ts, i))
			json_array_add_object_element (tiles, tile_json (self->ts, i));
	json_object_set_array_member (o, "tiles", tiles);
	for (i = 0; i < lrg_tileset_get_n_rule_tiles (self->ts); i++)
		json_array_add_object_element (rts, rule_tile_json (lrg_tileset_get_rule_tile (self->ts, i)));
	json_object_set_array_member (o, "rule_tiles", rts);
	return o;
}

static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = param_string (params, "path");
	JsonArray         *size = param_array (params, "tile_size");
	g_autoptr(GError)  err = NULL;
	g_autofree gchar  *abs = NULL;
	JsonObject        *o;
	guint              tw = 16, th = 16;

	if (path == NULL)
	{
		invalid (error, "Missing string parameter 'path'");
		return NULL;
	}
	if (size != NULL && json_array_get_length (size) == 2)
	{
		tw = (guint) MAX (1.0, json_array_get_double_element (size, 0));
		th = (guint) MAX (1.0, json_array_get_double_element (size, 1));
	}
	if (!lrg_editor_tileset_editor_create (app, path, param_string (params, "image"), tw, th,
	                                       param_string (params, "name"), &err))
	{
		fail (error, err);
		return NULL;
	}
	abs = editor_app_project_path (app, path);
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	return object_node (o);
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? object_node (editor_json (self)) : NULL;
}

static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	JsonArray              *size, *offset;
	gdouble                 v;
	guint                   tw = 0, th = 0, cols = 0;
	gint                    count = -1;
	gboolean                spacing = FALSE;
	guint                   margin, spc;
	gint                    ox, oy;

	if (self == NULL)
		return NULL;
	size = param_array (params, "tile_size");
	if (size != NULL && json_array_get_length (size) == 2)
	{
		tw = (guint) MAX (1.0, json_array_get_double_element (size, 0));
		th = (guint) MAX (1.0, json_array_get_double_element (size, 1));
	}
	if (param_number (params, "columns", &v))
		cols = (guint) MAX (1.0, v);
	if (param_number (params, "tile_count", &v))
		count = (gint) MAX (0.0, v);
	if ((param_string (params, "name") != NULL || param_string (params, "image") != NULL || tw > 0 || cols > 0 ||
	     count >= 0) &&
	    !lrg_editor_tileset_editor_set_layout (self, param_string (params, "name"), param_string (params, "image"), tw, th,
	                                           cols, count, &err))
	{
		fail (error, err);
		return NULL;
	}
	if (self->ts == NULL)
		return object_node (editor_json (self));
	margin = lrg_tileset_get_margin (self->ts);
	spc = lrg_tileset_get_spacing (self->ts);
	lrg_tileset_get_draw_offset (self->ts, &ox, &oy);
	if (param_number (params, "margin", &v))
	{
		margin = (guint) MAX (0.0, v);
		spacing = TRUE;
	}
	if (param_number (params, "spacing", &v))
	{
		spc = (guint) MAX (0.0, v);
		spacing = TRUE;
	}
	offset = param_array (params, "offset");
	if (offset != NULL && json_array_get_length (offset) == 2)
	{
		ox = (gint) json_array_get_double_element (offset, 0);
		oy = (gint) json_array_get_double_element (offset, 1);
		spacing = TRUE;
	}
	if (spacing && !lrg_editor_tileset_editor_set_spacing (self, margin, spc, ox, oy, &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (editor_json (self));
}

static JsonNode *
m_select (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	gdouble                 v;
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	if (param_number (params, "tile", &v))
		lrg_editor_tileset_editor_set_selected_tile (self, (gint) v);
	if (param_string (params, "rule_tile") != NULL &&
	    !lrg_editor_tileset_editor_select_rule_tile (self, param_string (params, "rule_tile")))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such rule tile");
		return NULL;
	}
	if (param_number (params, "rule", &v))
		lrg_editor_tileset_editor_select_rule (self, (gint) v);
	if (param_string (params, "page") != NULL)
		set_page (self, g_strcmp0 (param_string (params, "page"), "rules") == 0);
	o = json_object_new ();
	json_object_set_int_member (o, "selected", self->selected);
	if (self->rule_name != NULL)
		json_object_set_string_member (o, "selected_rule_tile", self->rule_name);
	else
		json_object_set_null_member (o, "selected_rule_tile");
	json_object_set_int_member (o, "selected_rule", self->rule_index);
	return object_node (o);
}

static gboolean
parse_flag (const gchar     *nick,
            LrgTileProperty *out)
{
	if (g_strcmp0 (nick, "solid") == 0)
		*out = LRG_TILE_PROPERTY_SOLID;
	else if (g_strcmp0 (nick, "animated") == 0)
		*out = LRG_TILE_PROPERTY_ANIMATED;
	else if (g_strcmp0 (nick, "hazard") == 0)
		*out = LRG_TILE_PROPERTY_HAZARD;
	else
		return FALSE;
	return TRUE;
}

static JsonNode *
m_tile (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	gdouble                 v;
	guint                   index, i;
	JsonArray              *arr;

	if (self == NULL)
		return NULL;
	if (!param_number (params, "tile", &v) || v < 0.0 || v >= (gdouble) lrg_tileset_get_tile_count (self->ts))
	{
		invalid (error, "tile: expected a tile index of the tileset");
		return NULL;
	}
	index = (guint) v;
	if ((arr = param_array (params, "flags")) != NULL)
	{
		LrgTileProperty flags = LRG_TILE_PROPERTY_NONE, bit;

		for (i = 0; i < json_array_get_length (arr); i++)
		{
			if (!parse_flag (json_array_get_string_element (arr, i), &bit))
			{
				invalid (error, "flags: expected solid, animated or hazard");
				return NULL;
			}
			flags |= bit;
		}
		if (!lrg_editor_tileset_editor_set_tile_flags (self, index, flags, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	if (json_object_has_member (params, "data") && JSON_NODE_HOLDS_OBJECT (json_object_get_member (params, "data")))
	{
		JsonObject *data = json_object_get_object_member (params, "data");
		GList      *keys = json_object_get_members (data), *l;
		gboolean    ok = TRUE;

		lrg_editor_document_begin_group (doc_of (self), "Tile Data");
		for (l = keys; ok && l != NULL; l = l->next)
		{
			JsonNode         *n = json_object_get_member (data, l->data);
			g_autofree gchar *value = NULL;

			if (JSON_NODE_HOLDS_NULL (n))
				value = NULL;
			else if (json_node_get_value_type (n) == G_TYPE_STRING)
				value = g_strdup (json_node_get_string (n));
			else
				value = json_to_string (n, FALSE);
			ok = lrg_editor_tileset_editor_set_tile_data (self, index, l->data, value, &err);
		}
		lrg_editor_document_end_group (doc_of (self));
		g_list_free (keys);
		if (!ok)
		{
			fail (error, err);
			return NULL;
		}
	}
	if ((arr = param_array (params, "animation")) != NULL)
	{
		guint             n = json_array_get_length (arr);
		g_autofree guint *frames = g_new0 (guint, n + 1);
		g_autofree guint *ms = g_new0 (guint, n + 1);

		for (i = 0; i < n; i++)
		{
			JsonObject *f = json_array_get_object_element (arr, i);

			if (f == NULL || !json_object_has_member (f, "tile"))
			{
				invalid (error, "animation: expected [{tile, duration}, ...]");
				return NULL;
			}
			frames[i] = (guint) json_object_get_int_member (f, "tile");
			ms[i] = json_object_has_member (f, "duration") ? (guint) MAX (1, json_object_get_int_member (f, "duration")) : 100;
		}
		if (!lrg_editor_tileset_editor_set_tile_animation (self, index, frames, ms, n, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	return object_node (self->ts != NULL ? tile_json (self->ts, index) : json_object_new ());
}

static JsonNode *
m_collision (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	const gchar            *op = param_string (params, "op");
	gdouble                 v, x = 0.0, y = 0.0, pv = 0.0, ptv = 0.0;
	guint                   index;
	gboolean                ok;
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	if (!param_number (params, "tile", &v) || v < 0.0)
	{
		invalid (error, "tile: expected a tile index");
		return NULL;
	}
	index = (guint) v;
	param_number (params, "x", &x);
	param_number (params, "y", &y);
	param_number (params, "index", &pv);
	param_number (params, "point", &ptv);
	if (g_strcmp0 (op, "add") == 0)
	{
		JsonArray          *poly = param_array (params, "polygon");
		guint               n = (poly != NULL) ? json_array_get_length (poly) : 0, i;
		g_autofree gfloat  *coords = g_new0 (gfloat, n + 1);

		for (i = 0; i < n; i++)
			coords[i] = (gfloat) json_array_get_double_element (poly, i);
		ok = lrg_editor_tileset_editor_add_collision (self, index, coords, n, &err);
	}
	else if (g_strcmp0 (op, "clear") == 0)
		ok = lrg_editor_tileset_editor_remove_collision (self, index, -1, &err);
	else if (g_strcmp0 (op, "remove") == 0)
		ok = lrg_editor_tileset_editor_remove_collision (self, index, (gint) pv, &err);
	else if (g_strcmp0 (op, "move_point") == 0)
		ok = lrg_editor_tileset_editor_move_collision_point (self, index, (guint) pv, (guint) ptv, (gfloat) x, (gfloat) y,
		                                                     0, &err);
	else if (g_strcmp0 (op, "insert_point") == 0)
		ok = lrg_editor_tileset_editor_insert_collision_point (self, index, (guint) pv, (guint) ptv, (gfloat) x, (gfloat) y,
		                                                       &err);
	else if (g_strcmp0 (op, "remove_point") == 0)
		ok = lrg_editor_tileset_editor_remove_collision_point (self, index, (guint) pv, (guint) ptv, &err);
	else
	{
		invalid (error, "op: expected add, clear, remove, move_point, insert_point or remove_point");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	o = (self->ts != NULL) ? tile_json (self->ts, index) : json_object_new ();
	return object_node (o);
}

/* A cell value from a tile index or {tile, rotate, flip}. */
static gboolean
parse_output (JsonNode *n,
              guint    *value,
              gfloat   *weight)
{
	*weight = 1.0f;
	if (JSON_NODE_HOLDS_VALUE (n))
	{
		*value = (guint) json_node_get_int (n) + 1;
		return TRUE;
	}
	if (JSON_NODE_HOLDS_OBJECT (n))
	{
		JsonObject *o = json_node_get_object (n);

		if (!json_object_has_member (o, "tile"))
			return FALSE;
		*value = lrg_tile_make ((guint) json_object_get_int_member (o, "tile") + 1,
		                        json_object_has_member (o, "flip") && json_object_get_boolean_member (o, "flip"),
		                        json_object_has_member (o, "rotate") ? (guint) json_object_get_int_member (o, "rotate") % 4u
		                                                             : 0);
		if (json_object_has_member (o, "weight"))
			*weight = (gfloat) json_object_get_double_member (o, "weight");
		return TRUE;
	}
	return FALSE;
}

static JsonNode *
m_rule_tile (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	const gchar            *op = param_string (params, "op");
	const gchar            *name = param_string (params, "name");
	gdouble                 v;
	gboolean                ok;
	LrgRuleTile            *rt;

	if (self == NULL)
		return NULL;
	if (name == NULL)
	{
		invalid (error, "Missing string parameter 'name'");
		return NULL;
	}
	if (g_strcmp0 (op, "add") == 0 || g_strcmp0 (op, "preset") == 0)
	{
		guint first = param_number (params, "first", &v) ? (guint) MAX (0.0, v) : 0;

		ok = lrg_editor_tileset_editor_add_rule_tile (self, name, param_string (params, "preset"), first, &err);
	}
	else if (g_strcmp0 (op, "remove") == 0)
	{
		ok = lrg_editor_tileset_editor_remove_rule_tile (self, name, &err);
		if (ok)
		{
			JsonObject *o = json_object_new ();

			json_object_set_boolean_member (o, "removed", TRUE);
			return object_node (o);
		}
	}
	else if (g_strcmp0 (op, "rename") == 0 || g_strcmp0 (op, "set") == 0)
	{
		gint64 def = -1, seed = -1;
		gint   border = -1;

		if (json_object_has_member (params, "default"))
		{
			guint  value;
			gfloat w;

			if (JSON_NODE_HOLDS_NULL (json_object_get_member (params, "default")))
				def = 0;
			else if (!parse_output (json_object_get_member (params, "default"), &value, &w))
			{
				invalid (error, "default: expected a tile index or {tile, rotate, flip}");
				return NULL;
			}
			else
				def = value;
		}
		if (param_number (params, "seed", &v))
			seed = (gint64) MAX (0.0, v);
		if (param_number (params, "border_is_this", &v))
			border = v != 0.0 ? 1 : 0;
		ok = lrg_editor_tileset_editor_set_rule_tile (self, name, param_string (params, "new_name"), def, seed, border,
		                                              &err);
		if (ok && param_string (params, "new_name") != NULL)
			name = param_string (params, "new_name");
	}
	else
	{
		invalid (error, "op: expected add, preset, remove, rename or set");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	rt = (self->ts != NULL) ? lrg_tileset_get_rule_tile_by_name (self->ts, name) : NULL;
	return object_node (rt != NULL ? rule_tile_json (rt) : json_object_new ());
}

static gboolean
parse_direction (const gchar     *nick,
                 LrgTileNeighbor *out)
{
	static const struct { const gchar *nick; LrgTileNeighbor bit; } dirs[] = {
		{ "n", LRG_TILE_NEIGHBOR_N }, { "ne", LRG_TILE_NEIGHBOR_NE }, { "e", LRG_TILE_NEIGHBOR_E },
		{ "se", LRG_TILE_NEIGHBOR_SE }, { "s", LRG_TILE_NEIGHBOR_S }, { "sw", LRG_TILE_NEIGHBOR_SW },
		{ "w", LRG_TILE_NEIGHBOR_W }, { "nw", LRG_TILE_NEIGHBOR_NW }, { "all", LRG_TILE_NEIGHBOR_ALL }
	};
	guint i;

	for (i = 0; nick != NULL && i < G_N_ELEMENTS (dirs); i++)
		if (g_ascii_strcasecmp (nick, dirs[i].nick) == 0)
		{
			*out = dirs[i].bit;
			return TRUE;
		}
	return FALSE;
}

static JsonNode *
m_rule (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	const gchar            *op = param_string (params, "op");
	const gchar            *name = param_string (params, "rule_tile");
	gdouble                 v;
	guint                   index = 0;
	gboolean                ok = TRUE;
	LrgRuleTile            *rt;

	if (self == NULL)
		return NULL;
	if (name == NULL)
	{
		invalid (error, "Missing string parameter 'rule_tile'");
		return NULL;
	}
	if (param_number (params, "index", &v))
		index = (guint) MAX (0.0, v);
	if (g_strcmp0 (op, "add") == 0)
	{
		JsonArray *tiles = param_array (params, "tiles");
		guint      value = 0;
		gfloat     w = 1.0f;

		if (tiles != NULL && json_array_get_length (tiles) > 0 &&
		    !parse_output (json_array_get_element (tiles, 0), &value, &w))
		{
			invalid (error, "tiles: expected tile indices or {tile, weight, rotate, flip}");
			return NULL;
		}
		ok = lrg_editor_tileset_editor_add_rule (self, name, param_number (params, "index", &v) ? (gint) v : -1, value,
		                                         &err);
		if (ok)
			index = (guint) self->rule_index;
		if (ok && tiles != NULL && json_array_get_length (tiles) > 0)
		{
			guint i;

			/* the first output was added with weight 1: set it, then add the rest */
			if (w != 1.0f)
				ok = lrg_editor_tileset_editor_set_rule_output_weight (self, name, index, 0, w, &err);
			for (i = 1; ok && i < json_array_get_length (tiles); i++)
			{
				if (!parse_output (json_array_get_element (tiles, i), &value, &w))
				{
					invalid (error, "tiles: expected tile indices or {tile, weight, rotate, flip}");
					return NULL;
				}
				ok = lrg_editor_tileset_editor_add_rule_output (self, name, index, value, w, &err);
			}
		}
	}
	else if (g_strcmp0 (op, "remove") == 0)
		ok = lrg_editor_tileset_editor_remove_rule (self, name, index, &err);
	else if (g_strcmp0 (op, "move") == 0)
	{
		if (!param_number (params, "to", &v))
		{
			invalid (error, "Missing number parameter 'to'");
			return NULL;
		}
		ok = lrg_editor_tileset_editor_move_rule (self, name, index, (guint) MAX (0.0, v), &err);
	}
	else if (g_strcmp0 (op, "set") == 0)
	{
		JsonArray   *pattern = param_array (params, "pattern");
		const gchar *s;

		lrg_editor_document_begin_group (doc_of (self), "Edit Rule");
		if (ok && pattern != NULL)
		{
			guint row, k;

			if (json_array_get_length (pattern) != 3)
			{
				lrg_editor_document_end_group (doc_of (self));
				invalid (error, "pattern: expected three rows of three characters ('#', '-', '.', '@' in the centre)");
				return NULL;
			}
			for (row = 0; ok && row < 3; row++)
			{
				const gchar *text = json_array_get_string_element (pattern, row);

				for (k = 0; ok && k < 3; k++)
				{
					LrgTileRuleMatch m;

					if (row == 1 && k == 1)
						continue;
					m = (text != NULL && strlen (text) > k && text[k] == '#') ? LRG_TILE_RULE_MATCH_THIS
					    : (text != NULL && strlen (text) > k && text[k] == '-') ? LRG_TILE_RULE_MATCH_NOT_THIS
					    : LRG_TILE_RULE_MATCH_ANY;
					ok = lrg_editor_tileset_editor_set_rule_neighbor (self, name, index, mask_cells[row * 3 + k], m, &err);
				}
			}
		}
		if (ok && (s = param_string (params, "neighbor")) != NULL)
		{
			LrgTileNeighbor dir;
			const gchar    *match = param_string (params, "match");
			LrgTileRuleMatch m = g_strcmp0 (match, "this") == 0 ? LRG_TILE_RULE_MATCH_THIS
			                     : g_strcmp0 (match, "not-this") == 0 ? LRG_TILE_RULE_MATCH_NOT_THIS
			                     : LRG_TILE_RULE_MATCH_ANY;

			if (!parse_direction (s, &dir))
			{
				lrg_editor_document_end_group (doc_of (self));
				invalid (error, "neighbor: expected n, ne, e, se, s, sw, w, nw or all");
				return NULL;
			}
			ok = lrg_editor_tileset_editor_set_rule_neighbor (self, name, index, dir, m, &err);
		}
		if (ok && (s = param_string (params, "transform")) != NULL)
		{
			guint t;

			for (t = 0; t < G_N_ELEMENTS (transform_nicks) && g_strcmp0 (s, transform_nicks[t]) != 0; t++)
				;
			if (t == G_N_ELEMENTS (transform_nicks))
			{
				lrg_editor_document_end_group (doc_of (self));
				invalid (error, "transform: expected fixed, rotated, mirror-x, mirror-y or mirror-xy");
				return NULL;
			}
			ok = lrg_editor_tileset_editor_set_rule_transform (self, name, index, (LrgTileRuleTransform) t, &err);
		}
		if (ok && param_array (params, "tiles") != NULL)
		{
			JsonArray   *tiles = param_array (params, "tiles");
			LrgTileRule *rule = (self->ts != NULL) ? find_rule (self->ts, name, index, NULL, &err) : NULL;
			guint        i, old = (rule != NULL) ? lrg_tile_rule_get_n_outputs (rule) : 0;

			ok = rule != NULL && json_array_get_length (tiles) > 0;
			/* add the new outputs first, then drop the old ones (a rule
			 * keeps at least one output in between) */
			for (i = 0; ok && i < json_array_get_length (tiles); i++)
			{
				guint  value;
				gfloat w;

				if (!parse_output (json_array_get_element (tiles, i), &value, &w))
					ok = FALSE;
				else
					ok = lrg_editor_tileset_editor_add_rule_output (self, name, index, value, w, &err);
			}
			for (i = 0; ok && i < old; i++)
				ok = lrg_editor_tileset_editor_remove_rule_output (self, name, index, 0, &err);
			if (!ok && err == NULL)
				g_set_error_literal (&err, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
				                     "tiles: expected a non-empty list of tile indices or {tile, weight, rotate, flip}");
		}
		lrg_editor_document_end_group (doc_of (self));
	}
	else
	{
		invalid (error, "op: expected add, remove, move or set");
		return NULL;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	rt = (self->ts != NULL) ? lrg_tileset_get_rule_tile_by_name (self->ts, name) : NULL;
	return object_node (rt != NULL ? rule_tile_json (rt) : json_object_new ());
}

static JsonNode *
m_preview (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_loaded (LRG_EDITOR_APP (user_data), params, error);
	JsonArray              *cells = param_array (params, "cells");
	JsonArray              *size = param_array (params, "size");
	JsonArray              *out;
	JsonObject             *o;
	g_autofree gint        *xy = NULL;
	g_autofree guint       *map = NULL;
	guint                   w = PREVIEW_SIZE, h = PREVIEW_SIZE, n, i;

	if (self == NULL)
		return NULL;
	if (param_string (params, "rule_tile") == NULL)
	{
		invalid (error, "Missing string parameter 'rule_tile'");
		return NULL;
	}
	if (size != NULL && json_array_get_length (size) == 2)
	{
		w = (guint) CLAMP (json_array_get_double_element (size, 0), 1.0, 256.0);
		h = (guint) CLAMP (json_array_get_double_element (size, 1), 1.0, 256.0);
	}
	n = (cells != NULL) ? json_array_get_length (cells) : 0;
	xy = g_new0 (gint, n + 1);
	for (i = 0; i < n; i++)
		xy[i] = (gint) json_array_get_int_element (cells, i);
	map = lrg_editor_tileset_editor_preview (self, param_string (params, "rule_tile"), xy, n, w, h);
	if (map == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such rule tile");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_array_member (o, "size", pair (w, h));
	out = json_array_new ();
	for (i = 0; i < w * h; i++)
		json_array_add_int_element (out, map[i]);
	json_object_set_array_member (o, "tiles", out);
	return object_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorTilesetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	JsonObject             *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

void
lrg_editor_tileset_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "tileset.create", m_create },
		{ "tileset.open", m_open },
		{ "tileset.get", m_get },
		{ "tileset.set", m_set },
		{ "tileset.select", m_select },
		{ "tileset.tile", m_tile },
		{ "tileset.collision", m_collision },
		{ "tileset.rule_tile", m_rule_tile },
		{ "tileset.rule", m_rule },
		{ "tileset.preview", m_preview },
		{ "tileset.save", m_save }
	};
	LrgEditorServer *server;
	guint            i;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));
	if (g_object_get_data (G_OBJECT (app), INSTALLED_KEY) != NULL)
		return;
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALLED_KEY, GINT_TO_POINTER (1));
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
