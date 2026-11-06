/* lrg-editor-tiles-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Tiles panel (the 0.8.0 tiles panel on the retained toolkit).
 *
 * Everything the panel changes goes through the shared state the
 * viewport also uses: the view state's tile-mode / tile-tool (through
 * the tilemap.* actions, so shortcuts, menus and IPC agree), the app's
 * LrgEditorTilePainter (tile, layer, brush size) and undoable node
 * snapshots (lrg_editor_snapshot_node + lrg_editor_commit_node_state)
 * for grid, layer and tileset edits.
 *
 * The widgets are refreshed from the model on the step after anything
 * changed (selection, an edit, an undo): values are copied with the
 * "syncing" guard so the widgets' change signals do not write back, and
 * a spin box the user is typing into is left alone. The layer rows are
 * rebuilt only when the layer list itself changed.
 *
 * The palette is an LrgCustomWidget: the tileset image sliced into tiles
 * of the grid's tile size (with a window; headless only its size is
 * read, from the image file), or numbered colour swatches without an
 * image, with hover and selection highlights.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-tiles-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-spin-box.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-drop-target.h"
#include "../../ui/lrg-drag.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include <raylib.h>
#include <math.h>

#define SWATCH_COLUMNS (8)
#define SWATCH_ROWS    (4)

static const gchar * const tool_ids[] = {
	"tilemap.tool-brush", "tilemap.tool-eraser", "tilemap.tool-rect", "tilemap.tool-fill", "tilemap.tool-pick"
};
static const gchar * const tool_labels[] = { "Brush", "Eraser", "Rect", "Fill", "Pick" };
static const LrgIcon tool_icons[] = { LRG_ICON_EDIT, LRG_ICON_CLOSE, LRG_ICON_MENU, LRG_ICON_DOWNLOAD, LRG_ICON_SEARCH };

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
	LrgWidget            *create_grid;
	LrgWidget            *grid_box;
	LrgSpinBox           *grid_w;
	LrgSpinBox           *grid_h;
	LrgSpinBox           *tile_w;
	LrgSpinBox           *tile_h;
	LrgWidget            *tileset;      /* drop zone (LrgCustomWidget) */
	LrgDropTarget        *drop;         /* borrowed, owned by the widget */
	LrgWidget            *layers;       /* LrgVBox of layer rows */
	LrgTextInput         *layer_name;
	LrgWidget            *palette;      /* LrgCustomWidget */
	LrgWidget            *tools_row;
	LrgWidget            *layer_row;

	/* Model mirror */
	LrgNode              *target;       /* weak (nodes die with the level) */
	gchar                *target_guid;
	gchar                *layers_sig;
	gboolean              dirty;
	gboolean              syncing;

	/* Palette */
	gchar                *palette_path; /* absolute image path the size / texture is for */
	GrlTexture           *palette_tex;  /* nullable (no window, or no image) */
	gint                  image_w;
	gint                  image_h;
	gint                  hover_tile;   /* 1-based id under the pointer, 0 = none */
};

G_DEFINE_FINAL_TYPE (LrgEditorTilesPanel, lrg_editor_tiles_panel, LRG_TYPE_CONTAINER)

static LrgNodeTilemap *
target_grid (LrgNode *node)
{
	return (node != NULL && lrg_node_get_visual (node) != NULL)
	       ? lrg_node_visual_get_tilemap (lrg_node_get_visual (node)) : NULL;
}

static LrgEditorTilePainter *
painter (LrgEditorTilesPanel *self)
{
	return self->app != NULL ? self->app->tile_painter : NULL;
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

/* ==========================================================================
 * Palette source
 * ========================================================================== */

/* Loads (once per path) the tileset image size, and its texture when a
 * window is open. */
static void
palette_load (LrgEditorTilesPanel *self)
{
	const gchar      *asset = NULL;
	g_autofree gchar *abs = NULL;

	if (self->target != NULL && lrg_node_get_visual (self->target) != NULL)
		asset = lrg_node_visual_get_asset (lrg_node_get_visual (self->target));
	if (asset != NULL && *asset != '\0' && self->app != NULL)
		abs = editor_app_project_path (self->app, asset);
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
	guint           tw = (grid != NULL) ? lrg_node_tilemap_get_tile_width (grid) : 16;
	guint           th = (grid != NULL) ? lrg_node_tilemap_get_tile_height (grid) : 16;

	if (self->image_w > 0 && self->image_h > 0)
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

/* Cell size in the widget for a given width */
static void
palette_cell (LrgEditorTilesPanel *self,
              gfloat               width,
              gfloat              *cw,
              gfloat              *ch)
{
	LrgNodeTilemap *grid = target_grid (self->target);
	guint           cols, rows;
	gfloat          aspect = 1.0f;

	palette_geometry (self, &cols, &rows);
	if (self->image_w > 0 && grid != NULL)
		aspect = (gfloat) lrg_node_tilemap_get_tile_height (grid) / (gfloat) MAX (1u, lrg_node_tilemap_get_tile_width (grid));
	*cw = MAX (8.0f, width / (gfloat) cols);
	*cw = MIN (*cw, 64.0f);
	*ch = *cw * aspect;
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
	guint                current = painter (self) != NULL ? lrg_editor_tile_painter_get_tile (painter (self)) : 0;
	guint                cols, rows, i;
	gfloat               cw, ch;
	GrlColor             c;

	palette_geometry (self, &cols, &rows);
	palette_cell (self, width, &cw, &ch);

	/* Backdrop (checker-ish dark surface shows transparent tiles) */
	lrg_paint_list_rect (list, x - 2.0f, y - 2.0f, cw * cols + 4.0f, ch * rows + 4.0f, 4.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	if (self->palette_tex != NULL)
	{
		GrlRectangle src = { 0.0f, 0.0f, (gfloat) (cols * (guint) (self->image_w / (gint) cols)),
		                     (gfloat) (rows * (guint) (self->image_h / (gint) rows)) };
		GrlColor     white = { 255, 255, 255, 255 };

		lrg_paint_list_image (list, self->palette_tex, &src, x, y, cw * cols, ch * rows, 0.0f, &white);
	}

	for (i = 0; i < cols * rows; i++)
	{
		gfloat cx = x + (gfloat) (i % cols) * cw;
		gfloat cy = y + (gfloat) (i / cols) * ch;

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
		else
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.5f, &c);
			lrg_paint_list_border (list, cx, cy, cw, ch, 0.0f, 1.0f, &c);
		}
		if ((gint) (i + 1) == self->hover_tile && i + 1 != current)
		{
			lrg_paint_color_with_alpha (accent, 0.25f, &c);
			lrg_paint_list_rect (list, cx, cy, cw, ch, 2.0f, &c);
		}
		if (i + 1 == current)
		{
			lrg_paint_list_border (list, cx - 1.0f, cy - 1.0f, cw + 2.0f, ch + 2.0f, 3.0f, 2.0f, accent);
		}
	}
}

/* 1-based tile under a canvas point, 0 outside */
static guint
palette_tile_at (LrgEditorTilesPanel *self,
                 gfloat               px,
                 gfloat               py)
{
	LrgWidget *w = self->palette;
	guint      cols, rows;
	gfloat     cw, ch, rx, ry;

	palette_geometry (self, &cols, &rows);
	palette_cell (self, lrg_widget_get_width (w), &cw, &ch);
	rx = px - lrg_widget_get_world_x (w);
	ry = py - lrg_widget_get_world_y (w);
	if (rx < 0.0f || ry < 0.0f || rx >= cw * cols || ry >= ch * rows)
		return 0;
	return (guint) (ry / ch) * cols + (guint) (rx / cw) + 1;
}

/* Picking a tile paints with it: eraser / eyedropper switch to brush */
static void
pick_tile (LrgEditorTilesPanel *self,
           guint                tile)
{
	LrgEditorTileTool tool;

	if (painter (self) == NULL || tile == 0)
		return;
	lrg_editor_tile_painter_set_tile (painter (self), tile);
	tool = lrg_editor_view_state_get_tile_tool (self->view_state);
	if (tool == LRG_EDITOR_TILE_TOOL_ERASER || tool == LRG_EDITOR_TILE_TOOL_PICK)
		lrg_editor_view_state_set_tile_tool (self->view_state, LRG_EDITOR_TILE_TOOL_BRUSH);
	if (self->target != NULL)
		lrg_editor_view_state_set_tile_mode (self->view_state, TRUE);
	self->dirty = TRUE;
}

static gboolean
palette_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorTilesPanel *self = LRG_EDITOR_TILES_PANEL (user_data);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_tile = (gint) palette_tile_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_tile = 0;
		return FALSE;
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		pick_tile (self, palette_tile_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)));
		return TRUE;
	default:
		return FALSE;
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
	text = (asset != NULL && *asset != '\0') ? asset : "Drop an image from Assets";

	/* Dashed-look well: a tinted fill and an accent border while hovered */
	lrg_paint_color_with_alpha (hot ? accent : lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT), hot ? 0.22f : 1.0f,
	                            &fill);
	lrg_paint_list_rect (list, x, y, width, height, radius, &fill);
	lrg_paint_list_border (list, x, y, width, height, radius, hot ? 2.0f : 1.0f,
	                       hot ? accent : lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	lrg_paint_list_icon (list, LRG_ICON_FOLDER, x + 8.0f, y + (height - icon) * 0.5f, icon, 0.0f, accent);
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

static void on_layer_select (LrgWidget *chip, gpointer user_data);
static void on_layer_eye (LrgWidget *chip, gpointer user_data);

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

/* Copies the model into the widgets (selection, edits, undo). */
static void
sync_widgets (LrgEditorTilesPanel *self)
{
	LrgNode          *node = (self->app != NULL) ? editor_tiles_target (self->app) : NULL;
	LrgNodeTilemap   *grid;
	LrgEditorTileTool tool = lrg_editor_view_state_get_tile_tool (self->view_state);
	guint             i;

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
	}

	self->syncing = TRUE;
	grid = target_grid (node);
	lrg_label_set_text (LRG_LABEL (self->title), lrg_node_get_name (node));
	editor_chip_set_checked (self->paint, lrg_editor_view_state_get_tile_mode (self->view_state));
	editor_chip_set_label (self->paint, lrg_editor_view_state_get_tile_mode (self->view_state) ? "Painting (B)"
	                                                                                           : "Paint (B)");
	for (i = 0; i < G_N_ELEMENTS (self->tools); i++)
		editor_chip_set_checked (self->tools[i], (guint) tool == i);
	if (painter (self) != NULL)
	{
		set_spin (self, self->brush, lrg_editor_tile_painter_get_brush_size (painter (self)));
		set_spin (self, self->tile, lrg_editor_tile_painter_get_tile (painter (self)));
	}

	lrg_widget_set_visible (self->create_grid, grid == NULL);
	lrg_widget_set_visible (self->grid_box, grid != NULL);
	if (grid != NULL)
	{
		set_spin (self, self->grid_w, lrg_node_tilemap_get_width (grid));
		set_spin (self, self->grid_h, lrg_node_tilemap_get_height (grid));
		set_spin (self, self->tile_w, lrg_node_tilemap_get_tile_width (grid));
		set_spin (self, self->tile_h, lrg_node_tilemap_get_tile_height (grid));
	}
	sync_layers (self, grid);
	palette_load (self);
	lrg_widget_queue_layout (self->palette);
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
		lrg_editor_tile_painter_set_tile (painter (self), (guint) MAX (0.0, value));
}

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

	/* Cheap checks for state other code changes without a signal */
	if (node != self->target ||
	    (node != NULL && (editor_chip_get_checked (self->paint) != lrg_editor_view_state_get_tile_mode (self->view_state) ||
	                      !editor_chip_get_checked (self->tools[MIN ((guint) tool, 4u)]) ||
	                      (painter (self) != NULL &&
	                       (lrg_spin_box_get_value (self->tile) != lrg_editor_tile_painter_get_tile (painter (self)) ||
	                        lrg_spin_box_get_value (self->brush) !=
	                        lrg_editor_tile_painter_get_brush_size (painter (self)))))))
		self->dirty = TRUE;
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
	g_free (self->layers_sig);
	g_free (self->palette_path);
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
static void
add_heading (LrgWidget   *box,
             const gchar *text)
{
	LrgLabel       *label = lrg_label_new (text);
	const LrgInsets ins = { 0.0f, 6.0f, 0.0f, 0.0f };

	lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	lrg_widget_set_margin (LRG_WIDGET (label), &ins);
	add_to (box, LRG_WIDGET (label));
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

static void
lrg_editor_tiles_panel_init (LrgEditorTilesPanel *self)
{
	static const LrgCustomWidgetFuncs palette_funcs = { palette_measure, palette_paint, NULL, palette_event, { NULL } };
	static const LrgCustomWidgetFuncs tileset_funcs = { NULL, tileset_paint, NULL, NULL, { NULL } };
	static const gchar * const        types[] = { LRG_EDITOR_TILES_ASSET_TYPE, NULL };
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

	/* Palette */
	add_heading (self->content, "Palette");
	self->palette = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->palette, "tiles-palette");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->palette), &palette_funcs, self, NULL);
	add_to (self->content, self->palette);

	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "tiles-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), self->content);
	g_object_unref (self->content);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);
}

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

LrgEditorTilesPanel *
lrg_editor_tiles_panel_new (LrgEditorApp *app)
{
	LrgEditorTilesPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_TILES_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->view_state = g_object_ref (lrg_editor_app_get_view_state (app));
	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (on_editor_changed), self);
	g_signal_connect_swapped (lrg_editor_get_selection (self->editor), "changed", G_CALLBACK (on_editor_changed), self);
	lrg_editor_actions_add (lrg_editor_app_get_actions (app), "tilemap.rename-layer", "Rename Tile Layer", "Tilemap",
	                        NULL, a_rename_layer, has_grid, NULL, self);
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
	LrgNode           *node;

	g_return_val_if_fail (LRG_IS_EDITOR_TILES_PANEL (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	node = lrg_editor_tiles_panel_get_target (self);
	if (node == NULL || self->app == NULL || lrg_node_get_visual (node) == NULL)
		return FALSE;
	rel = editor_app_relative_path (self->app, path);
	before = lrg_editor_snapshot_node (node);
	lrg_node_visual_set_asset (lrg_node_get_visual (node), rel);
	lrg_editor_commit_node_state (self->editor, node, before, "Set Tileset", FALSE);
	editor_app_set_status (self->app, "Tileset: %s", rel);
	self->dirty = TRUE;
	return TRUE;
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
