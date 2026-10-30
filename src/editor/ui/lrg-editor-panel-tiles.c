/* lrg-editor-panel-tiles.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tilemap painting in the editor UI:
 *
 *  - the "Tiles" panel: target node, paint mode, tools, brush size,
 *    layers (select / show / add / remove), grid settings and the tileset
 *    palette (the tileset image sliced into tiles; numbered swatches when
 *    there is no image or no window);
 *  - the viewport side: while paint mode is on and the primary selection
 *    is a tilemap node, the left button paints instead of picking.
 *
 * All edits go through the shared LrgEditorTilePainter, so strokes are
 * one undo group each and IPC/tests drive the exact same code. Works
 * headless (app tests inject input); only the palette texture needs a
 * window.
 */

#include "lrg-editor-app-private.h"
#include <math.h>

static const gchar * const tool_names[EDITOR_TILE_N_TOOLS] = { "brush", "eraser", "rect", "fill", "pick" };
static const gchar * const tool_labels[EDITOR_TILE_N_TOOLS] = { "Brush", "Eraser", "Rect", "Fill", "Pick" };

const gchar *
editor_tile_tool_name (EditorTileTool tool)
{
	return (tool < EDITOR_TILE_N_TOOLS) ? tool_names[tool] : "brush";
}

gboolean
editor_tile_tool_from_name (const gchar    *name,
                            EditorTileTool *out)
{
	gint i;

	for (i = 0; name != NULL && i < EDITOR_TILE_N_TOOLS; i++)
		if (g_strcmp0 (name, tool_names[i]) == 0)
		{
			*out = (EditorTileTool) i;
			return TRUE;
		}
	return FALSE;
}

void
editor_tiles_init (LrgEditorApp *self)
{
	self->tile_painter = lrg_editor_tile_painter_new (self->editor);
	self->tile_tool = EDITOR_TILE_BRUSH;
	self->tile_hover_x = self->tile_hover_y = -1;
}

void
editor_tiles_dispose (LrgEditorApp *self)
{
	if (self->tile_painter != NULL && lrg_editor_tile_painter_is_stroking (self->tile_painter))
		lrg_editor_tile_painter_end_stroke (self->tile_painter);
	g_clear_object (&self->tile_painter);
	g_clear_object (&self->tile_palette_tex);
	g_clear_pointer (&self->tile_palette_path, g_free);
}

/* The primary selection when it is a tilemap node. */
LrgNode *
editor_tiles_target (LrgEditorApp *self)
{
	LrgNode       *node = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
	LrgNodeVisual *visual = (node != NULL) ? lrg_node_get_visual (node) : NULL;

	if (visual == NULL || lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_TILEMAP)
		return NULL;
	return node;
}

static LrgNodeTilemap *
target_grid (LrgNode *node)
{
	return (node != NULL) ? lrg_node_visual_get_tilemap (lrg_node_get_visual (node)) : NULL;
}

/* A stable, distinct colour per tile id (swatches and untextured tiles). */
static LrgGuiColor
tile_color (guint id)
{
	static const LrgGuiColor palette[] = {
		0xf38ba8ff, 0xfab387ff, 0xf9e2afff, 0xa6e3a1ff, 0x94e2d5ff, 0x89dcebff,
		0x74c7ecff, 0x89b4faff, 0xb4befeff, 0xcba6f7ff, 0xf5c2e7ff, 0xeba0acff
	};

	return (id == 0) ? 0x00000000u : palette[(id - 1) % G_N_ELEMENTS (palette)];
}

/* ==========================================================================
 * Viewport
 * ========================================================================== */

/* Cell under a viewport pixel: the ray meets the node's local XY plane. */
static gboolean
cell_under (LrgEditorApp *self,
            LrgNode      *node,
            gfloat        lx,
            gfloat        ly,
            gint         *x,
            gint         *y)
{
	gfloat       world[16], o[3], d[3], n[3], p[3], t;
	const gfloat z[3] = { 0.0f, 0.0f, 1.0f };
	gfloat       len;

	*x = *y = -1;
	if (target_grid (node) == NULL)
		return FALSE;

	lrg_editor_node_get_world_matrix (node, world);
	lrg_editor_math_transform_direction (world, z, n);
	len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (len < 1e-6f)
		return FALSE;
	n[0] /= len;
	n[1] /= len;
	n[2] /= len;

	lrg_editor_view_get_ray (&self->view, lx, ly, o, d);
	if (!lrg_editor_ray_plane (o, d, world + 12, n, &t) || t < 0.0f)
		return FALSE;
	p[0] = o[0] + d[0] * t;
	p[1] = o[1] + d[1] * t;
	p[2] = o[2] + d[2] * t;
	lrg_editor_tile_painter_cell_at (node, p, x, y);
	return TRUE;
}

static void
finish_drag (LrgEditorApp *self,
             LrgNode      *node,
             gboolean      commit)
{
	if (lrg_editor_tile_painter_is_stroking (self->tile_painter))
	{
		guint n = lrg_editor_tile_painter_end_stroke (self->tile_painter);

		editor_app_set_status (self, "%s %u tile(s)", self->tile_tool == EDITOR_TILE_ERASER ? "Erased" : "Painted", n);
	}
	if (self->tile_rect_active && commit && node != NULL)
	{
		guint n = lrg_editor_tile_painter_paint_rect (self->tile_painter, node, self->tile_rect_x0, self->tile_rect_y0,
		                                              self->tile_hover_x, self->tile_hover_y,
		                                              (lrg_gui_get_mods (self->gui) & LRG_GUI_MOD_SHIFT) != 0);

		editor_app_set_status (self, "Rectangle: %u tile(s)", n);
	}
	self->tile_rect_active = FALSE;
	self->tile_drag = FALSE;
}

/*
 * Returns TRUE when the tile tool owns the left button this frame, so the
 * viewport skips gizmo, picking and box selection.
 */
gboolean
editor_tiles_viewport (LrgEditorApp     *self,
                       const LrgGuiRect *rect,
                       gfloat            lx,
                       gfloat            ly,
                       gboolean          hovered)
{
	LrgNode        *node = editor_tiles_target (self);
	LrgGuiModifiers mods;
	gboolean        in_grid;

	if (!self->tile_mode || node == NULL)
	{
		if (self->tile_drag)
			finish_drag (self, NULL, FALSE);
		self->tile_hover_valid = FALSE;
		return FALSE;
	}

	mods = lrg_gui_get_mods (self->gui);
	in_grid = cell_under (self, node, lx, ly, &self->tile_hover_x, &self->tile_hover_y) &&
	          lrg_node_tilemap_in_bounds (target_grid (node), self->tile_hover_x, self->tile_hover_y);
	self->tile_hover_valid = hovered && in_grid;

	/* Escape cancels a rectangle, otherwise leaves paint mode. */
	if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_ESCAPE, FALSE) && !lrg_gui_context_wants_keyboard (self->gui))
	{
		if (self->tile_rect_active)
			finish_drag (self, node, FALSE);
		else
		{
			finish_drag (self, node, FALSE);
			self->tile_mode = FALSE;
			editor_app_set_status (self, "Tile painting off");
			return FALSE;
		}
	}

	/* Press: start the active tool (Ctrl + click is always the eyedropper). */
	if (!self->tile_drag && hovered && lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_LEFT) &&
	    !(mods & LRG_GUI_MOD_ALT))
	{
		EditorTileTool tool = (mods & LRG_GUI_MOD_CTRL) ? EDITOR_TILE_PICK : self->tile_tool;

		/* A node that was never painted gets its grid first. */
		if (target_grid (node) == NULL && tool != EDITOR_TILE_PICK && tool != EDITOR_TILE_BRUSH &&
		    tool != EDITOR_TILE_ERASER)
		{
			lrg_editor_tile_painter_ensure_tilemap (self->tile_painter, node);
			cell_under (self, node, lx, ly, &self->tile_hover_x, &self->tile_hover_y);
		}

		switch (tool)
		{
		case EDITOR_TILE_BRUSH:
		case EDITOR_TILE_ERASER:
			if (lrg_editor_tile_painter_begin_stroke (self->tile_painter, node,
			                                          tool == EDITOR_TILE_ERASER || (mods & LRG_GUI_MOD_SHIFT)))
			{
				/* The stroke may have just created the grid. */
				cell_under (self, node, lx, ly, &self->tile_hover_x, &self->tile_hover_y);
				lrg_editor_tile_painter_stroke_to (self->tile_painter, self->tile_hover_x, self->tile_hover_y);
				self->tile_drag = TRUE;
			}
			break;
		case EDITOR_TILE_RECT:
			self->tile_rect_active = TRUE;
			self->tile_rect_x0 = self->tile_hover_x;
			self->tile_rect_y0 = self->tile_hover_y;
			self->tile_drag = TRUE;
			break;
		case EDITOR_TILE_FILL:
			editor_app_set_status (self, "Filled %u tile(s)",
			                       lrg_editor_tile_painter_fill (self->tile_painter, node, self->tile_hover_x,
			                                                     self->tile_hover_y, (mods & LRG_GUI_MOD_SHIFT) != 0));
			break;
		case EDITOR_TILE_PICK:
		default:
			{
				guint tile = lrg_editor_tile_painter_pick (self->tile_painter, node, self->tile_hover_x,
				                                           self->tile_hover_y);

				if (tile != 0)
					editor_app_set_status (self, "Picked tile %u", tile);
			}
			break;
		}
		return TRUE;
	}

	/* Drag: brush strokes follow the pointer; rectangles just track it. */
	if (self->tile_drag)
	{
		/* The release frame still carries the last pointer motion (slow
		 * frames batch several events), so paint up to it first. */
		if (lrg_editor_tile_painter_is_stroking (self->tile_painter))
			lrg_editor_tile_painter_stroke_to (self->tile_painter, self->tile_hover_x, self->tile_hover_y);
		if (!lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT))
			finish_drag (self, node, TRUE);
		return TRUE;
	}

	/* Paint mode swallows plain clicks even outside the grid. */
	return hovered;
}

/* Projects a node-local point on the grid plane to panel coordinates. */
static gboolean
local_to_screen (LrgEditorApp     *self,
                 const gfloat     *world,
                 const LrgGuiRect *rect,
                 gfloat            lx,
                 gfloat            ly,
                 gfloat           *sx,
                 gfloat           *sy)
{
	gfloat local[3] = { lx, ly, 0.0f };
	gfloat p[3];

	lrg_editor_math_transform_point (world, local, p);
	if (!lrg_editor_view_project (&self->view, p, sx, sy, NULL))
		return FALSE;
	*sx += rect->x;
	*sy += rect->y;
	return TRUE;
}

/* Outlines cells [x0, x1] x [y0, y1] in the viewport. */
static void
outline_cells (LrgEditorApp     *self,
               LrgNode          *node,
               const LrgGuiRect *rect,
               gint              x0,
               gint              y0,
               gint              x1,
               gint              y1,
               LrgGuiColor       color)
{
	LrgGuiDrawList *dl = lrg_gui_get_draw_list (self->gui);
	gfloat          world[16], ts, sx[4], sy[4];
	gfloat          ax, ay, bx, by;
	gint            i;

	lrg_editor_node_get_world_matrix (node, world);
	ts = lrg_editor_tile_painter_get_tile_size (node);
	ax = MIN (x0, x1) * ts;
	bx = (MAX (x0, x1) + 1) * ts;
	ay = -MIN (y0, y1) * ts;
	by = -(MAX (y0, y1) + 1) * ts;
	if (!local_to_screen (self, world, rect, ax, ay, &sx[0], &sy[0]) ||
	    !local_to_screen (self, world, rect, bx, ay, &sx[1], &sy[1]) ||
	    !local_to_screen (self, world, rect, bx, by, &sx[2], &sy[2]) ||
	    !local_to_screen (self, world, rect, ax, by, &sx[3], &sy[3]))
		return;
	for (i = 0; i < 4; i++)
		lrg_gui_draw_list_line (dl, sx[i], sy[i], sx[(i + 1) % 4], sy[(i + 1) % 4], color, 2.0f);
}

void
editor_tiles_overlay (LrgEditorApp     *self,
                      const LrgGuiRect *rect)
{
	LrgGuiStyle    *style = lrg_gui_context_get_style (self->gui);
	LrgNode        *node = editor_tiles_target (self);
	LrgNodeTilemap *grid = target_grid (node);

	if (!self->tile_mode || node == NULL)
		return;

	/* Tool readout in the corner. */
	{
		g_autofree gchar *text = g_strdup_printf ("Tiles: %s  tile %u  layer %u  brush %u", tool_labels[self->tile_tool],
		                                          lrg_editor_tile_painter_get_tile (self->tile_painter),
		                                          lrg_editor_tile_painter_get_layer (self->tile_painter),
		                                          lrg_editor_tile_painter_get_brush_size (self->tile_painter));

		lrg_gui_draw_list_text (lrg_gui_get_draw_list (self->gui), rect->x + 10.0f,
		                        rect->y + rect->h - style->font_size - 10.0f, text, -1, style->font_size,
		                        style->colors[LRG_GUI_COL_ACCENT], TRUE);
	}
	if (grid == NULL)
		return;

	if (self->tile_rect_active)
		outline_cells (self, node, rect, self->tile_rect_x0, self->tile_rect_y0, self->tile_hover_x,
		               self->tile_hover_y, style->colors[LRG_GUI_COL_ACCENT]);
	else if (self->tile_hover_valid)
	{
		gint b = (self->tile_tool == EDITOR_TILE_BRUSH || self->tile_tool == EDITOR_TILE_ERASER)
		         ? (gint) lrg_editor_tile_painter_get_brush_size (self->tile_painter) : 1;
		gint ox = self->tile_hover_x - (b - 1) / 2;
		gint oy = self->tile_hover_y - (b - 1) / 2;

		outline_cells (self, node, rect, ox, oy, ox + b - 1, oy + b - 1,
		               self->tile_tool == EDITOR_TILE_ERASER ? style->colors[LRG_GUI_COL_ERROR]
		                                                     : style->colors[LRG_GUI_COL_ACCENT]);
	}
}

/* ==========================================================================
 * Panel
 * ========================================================================== */

/* Loads (once per path) the tileset image as a texture for the palette. */
static GObject *
palette_texture (LrgEditorApp *self,
                 LrgNode      *node)
{
	const gchar      *asset = lrg_node_visual_get_asset (lrg_node_get_visual (node));
	g_autofree gchar *abs = NULL;

	if (asset == NULL || self->headless)
		return NULL;
	abs = editor_app_project_path (self, asset);
	if (g_strcmp0 (abs, self->tile_palette_path) != 0)
	{
		g_clear_object (&self->tile_palette_tex);
		g_free (self->tile_palette_path);
		self->tile_palette_path = g_strdup (abs);
		if (g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		{
			GrlTexture *tex = grl_texture_new_from_file (abs);

			if (tex != NULL && grl_texture_is_valid (tex))
				self->tile_palette_tex = G_OBJECT (tex);
			else
				g_clear_object (&tex);
		}
	}
	return self->tile_palette_tex;
}

/* Snapshot-edit-commit helper for grid settings (drags merge). */
static void
edit_grid (LrgEditorApp *self,
           LrgNode      *node,
           const gchar  *label,
           gboolean      merge,
           gint          new_w,
           gint          new_h,
           gint          tile_w,
           gint          tile_h)
{
	g_autoptr(LrgNode) before = lrg_editor_snapshot_node (node);
	LrgNodeTilemap    *grid = target_grid (node);

	if (new_w > 0 && new_h > 0)
		lrg_node_tilemap_resize (grid, (guint) new_w, (guint) new_h);
	if (tile_w > 0 && tile_h > 0)
		lrg_node_tilemap_set_tile_size (grid, (guint) tile_w, (guint) tile_h);
	lrg_editor_commit_node_state (self->editor, node, before, label, merge);
}

static void
layers_section (LrgEditorApp   *self,
                LrgNode        *node,
                LrgNodeTilemap *grid)
{
	guint i, current = lrg_editor_tile_painter_get_layer (self->tile_painter);
	guint n = lrg_node_tilemap_get_n_layers (grid);

	if (current >= n)
		lrg_editor_tile_painter_set_layer (self->tile_painter, current = n - 1);

	lrg_gui_separator_text (self->gui, "Layers");
	/* Top layer first, like an image editor. */
	for (i = n; i-- > 0;)
	{
		gboolean visible = lrg_node_tilemap_get_layer_visible (grid, i);

		lrg_gui_push_id_int (self->gui, (gint) i);
		if (lrg_gui_checkbox (self->gui, "##visible", &visible))
		{
			g_autoptr(LrgNode) before = lrg_editor_snapshot_node (node);

			lrg_node_tilemap_set_layer_visible (grid, i, visible);
			lrg_editor_commit_node_state (self->editor, node, before, "Toggle Tile Layer", FALSE);
			grid = target_grid (node);
		}
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_selectable (self->gui, lrg_node_tilemap_get_layer_name (grid, i), i == current, 0))
			lrg_editor_tile_painter_set_layer (self->tile_painter, i);
		lrg_gui_pop_id (self->gui);
	}
	if (lrg_gui_button (self->gui, "+ Layer"))
		lrg_editor_actions_activate (self->actions, "tilemap.add-layer");
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "- Layer") && n > 1)
		lrg_editor_actions_activate (self->actions, "tilemap.remove-layer");
}

static void
palette_section (LrgEditorApp   *self,
                 LrgNode        *node,
                 LrgNodeTilemap *grid)
{
	LrgGuiStyle    *style = lrg_gui_context_get_style (self->gui);
	LrgGuiDrawList *dl;
	GObject        *tex = palette_texture (self, node);
	guint           current = lrg_editor_tile_painter_get_tile (self->tile_painter);
	gfloat          avail_w, x0, y0;
	guint           cols, rows, count, i;
	gfloat          cell_w, cell_h;
	guint           tile_w = (grid != NULL) ? lrg_node_tilemap_get_tile_width (grid) : 16;
	guint           tile_h = (grid != NULL) ? lrg_node_tilemap_get_tile_height (grid) : 16;

	lrg_gui_separator_text (self->gui, "Palette");
	lrg_gui_get_content_avail (self->gui, &avail_w, NULL);
	avail_w = MAX (avail_w, 64.0f);

	if (tex != NULL)
	{
		gint tw = grl_texture_get_width (GRL_TEXTURE (tex));
		gint th = grl_texture_get_height (GRL_TEXTURE (tex));

		cols = MAX (1u, (guint) tw / tile_w);
		rows = MAX (1u, (guint) th / tile_h);
		cell_w = avail_w / cols;
		cell_h = cell_w * ((gfloat) tile_h / (gfloat) tile_w);
	}
	else
	{
		/* No image (or no window): numbered swatches. */
		cols = 8;
		rows = 4;
		cell_w = cell_h = avail_w / cols;
	}
	count = cols * rows;

	lrg_gui_get_cursor (self->gui, &x0, &y0);
	lrg_gui_invisible_button (self->gui, "##palette", cell_w * cols, cell_h * rows);
	dl = lrg_gui_get_draw_list (self->gui);

	if (tex != NULL)
	{
		LrgGuiRect img = { x0, y0, cell_w * cols, cell_h * rows };

		lrg_gui_draw_list_image (dl, &img, tex, 0xffffffffu);
	}
	for (i = 0; i < count; i++)
	{
		LrgGuiRect cell = { x0 + (i % cols) * cell_w, y0 + (i / cols) * cell_h, cell_w, cell_h };

		if (tex == NULL)
		{
			g_autofree gchar *num = g_strdup_printf ("%u", i + 1);
			LrgGuiRect        inner = { cell.x + 1.0f, cell.y + 1.0f, cell.w - 2.0f, cell.h - 2.0f };

			lrg_gui_draw_list_rect_filled (dl, &inner, tile_color (i + 1), 2.0f);
			lrg_gui_draw_list_text (dl, cell.x + 3.0f, cell.y + 2.0f, num, -1, style->font_size * 0.8f,
			                        0x11111bffu, FALSE);
		}
		else
			lrg_gui_draw_list_rect_outline (dl, &cell, 0x45475a80u, 1.0f, 0.0f);
		if (i + 1 == current)
			lrg_gui_draw_list_rect_outline (dl, &cell, style->colors[LRG_GUI_COL_ACCENT], 2.0f, 0.0f);
	}

	/* Click a tile to paint with it (switches back from eraser/pick). */
	if (lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_LEFT))
	{
		gfloat mx, my;
		guint  cx, cy;

		lrg_gui_get_mouse (self->gui, &mx, &my);
		cx = (guint) CLAMP ((mx - x0) / cell_w, 0.0f, cols - 1.0f);
		cy = (guint) CLAMP ((my - y0) / cell_h, 0.0f, rows - 1.0f);
		lrg_editor_tile_painter_set_tile (self->tile_painter, cy * cols + cx + 1);
		if (self->tile_tool == EDITOR_TILE_ERASER || self->tile_tool == EDITOR_TILE_PICK)
			self->tile_tool = EDITOR_TILE_BRUSH;
		self->tile_mode = TRUE;
	}
	if (lrg_gui_is_item_hovered (self->gui))
		lrg_gui_set_item_tooltip (self->gui, "Click a tile to paint with it");
}

void
editor_panel_tiles (LrgEditorApp *self)
{
	LrgNode        *node = editor_tiles_target (self);
	LrgNodeTilemap *grid;
	gint            i, brush, tile;
	const gchar    *tileset;

	if (node == NULL)
	{
		lrg_gui_text_wrapped (self->gui, "Select a tilemap node to paint it (Create > Tilemap).");
		return;
	}

	lrg_gui_textf (self->gui, "%s", lrg_node_get_name (node));
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_toggle_button (self->gui, self->tile_mode ? "Painting (B)" : "Paint (B)", &self->tile_mode))
		editor_app_set_status (self, self->tile_mode ? "Tile painting on" : "Tile painting off");

	/* Tools. */
	for (i = 0; i < EDITOR_TILE_N_TOOLS; i++)
	{
		gboolean active = (self->tile_tool == (EditorTileTool) i);
		g_autofree gchar *id = g_strdup_printf ("tilemap.tool-%s", tool_names[i]);

		if (i > 0)
			lrg_gui_same_line (self->gui, -1.0f);
		lrg_gui_push_id_int (self->gui, i);
		if (lrg_gui_toggle_button (self->gui, tool_labels[i], &active))
			lrg_editor_actions_activate (self->actions, id);
		lrg_gui_pop_id (self->gui);
	}

	brush = (gint) lrg_editor_tile_painter_get_brush_size (self->tile_painter);
	if (lrg_gui_drag_int (self->gui, "Brush", &brush, 0.1f, 1, 16))
		lrg_editor_tile_painter_set_brush_size (self->tile_painter, (guint) brush);
	tile = (gint) lrg_editor_tile_painter_get_tile (self->tile_painter);
	if (lrg_gui_drag_int (self->gui, "Tile", &tile, 0.1f, 0, 65535))
		lrg_editor_tile_painter_set_tile (self->tile_painter, (guint) tile);

	grid = target_grid (node);
	if (grid == NULL)
	{
		if (lrg_gui_button (self->gui, "Create Grid"))
			lrg_editor_tile_painter_ensure_tilemap (self->tile_painter, node);
		lrg_gui_text_disabled (self->gui, "The first stroke creates a 16x16 grid.");
		palette_section (self, node, NULL);
		return;
	}

	/* Grid settings. */
	lrg_gui_separator_text (self->gui, "Grid");
	{
		gint w = (gint) lrg_node_tilemap_get_width (grid);
		gint h = (gint) lrg_node_tilemap_get_height (grid);
		gint tw = (gint) lrg_node_tilemap_get_tile_width (grid);
		gint th = (gint) lrg_node_tilemap_get_tile_height (grid);

		if (lrg_gui_drag_int (self->gui, "Width", &w, 0.2f, 1, LRG_NODE_TILEMAP_MAX_SIZE))
			edit_grid (self, node, "Resize Tilemap", lrg_gui_is_item_active (self->gui), w, h, 0, 0);
		if (lrg_gui_drag_int (self->gui, "Height", &h, 0.2f, 1, LRG_NODE_TILEMAP_MAX_SIZE))
			edit_grid (self, node, "Resize Tilemap", lrg_gui_is_item_active (self->gui), w, h, 0, 0);
		if (lrg_gui_drag_int (self->gui, "Tile px W", &tw, 0.2f, 1, 1024))
			edit_grid (self, node, "Tile Size", lrg_gui_is_item_active (self->gui), 0, 0, tw, th);
		if (lrg_gui_drag_int (self->gui, "Tile px H", &th, 0.2f, 1, 1024))
			edit_grid (self, node, "Tile Size", lrg_gui_is_item_active (self->gui), 0, 0, tw, th);
		grid = target_grid (node);
	}

	/* Tileset: the visual's asset; drop an image from Assets to set it. */
	tileset = lrg_node_visual_get_asset (lrg_node_get_visual (node));
	lrg_gui_textf (self->gui, "Tileset: %s", tileset != NULL ? tileset : "(drop an image here)");
	{
		const gchar *dropped = lrg_gui_accept_drop (self->gui, "asset");

		if (dropped != NULL)
		{
			g_autofree gchar   *copy = g_strdup (dropped);
			g_autoptr(LrgNode)  before = lrg_editor_snapshot_node (node);

			lrg_node_visual_set_asset (lrg_node_get_visual (node), copy);
			lrg_editor_commit_node_state (self->editor, node, before, "Set Tileset", FALSE);
			grid = target_grid (node);
		}
	}

	layers_section (self, node, grid);
	palette_section (self, node, target_grid (node));
}
