/* test-editor-app-data-2.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The data editors, part 2: the HUD layout, UI layout, dialog tree and
 * procgen dungeon editors, opened through lrg_editor_data_editor_open() in
 * a headless LrgEditorApp (tests/editor-app-fixture.h) and driven through
 * its canvas: drags on the HUD stage, clicks in the UI preview and the
 * outline, typing in the YAML, wires between graph ports, the play-test
 * buttons, the dungeon toolbar and form rows. Every edit is checked in the
 * document, undone through the document's own stack and, where the format
 * has one, loaded back with the engine loader.
 */

#include "editor-app-fixture.h"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static const gchar *HUD_YAML =
	"version: 1\n"
	"pieces:\n"
	"  health: { anchor: top-left, x: 16, y: 16 }\n"
	"  minimap: { anchor: top-right, x: -16, y: 16 }\n"
	"  quest: { hidden: true }\n"
	"  compass: { anchor: top, x: 0, y: 8, locked: true }\n";

static const gchar *UI_YAML =
	"type: vbox\n"
	"children:\n"
	"  - type: label\n"
	"    text: Hello\n"
	"  - type: button\n"
	"    id: ok\n"
	"    text: OK\n"
	"    on: {clicked: on_ok}\n";

static const gchar *DIALOG_YAML =
	"id: intro\n"
	"start: hello\n"
	"nodes:\n"
	"  - id: hello\n"
	"    speaker: Guard\n"
	"    text: Halt! Who goes there?\n"
	"    responses:\n"
	"      - { text: Friend, next: friend }\n"
	"      - { text: Foe, next: foe }\n"
	"  - id: friend\n"
	"    text: Pass.\n"
	"  - id: foe\n"
	"    text: To arms!\n";

static const gchar *DUNGEON_YAML =
	"generator: bsp\n"
	"width: 40\n"
	"height: 30\n"
	"seed: 7\n"
	"post:\n"
	"  connect: true\n"
	"spawns:\n"
	"  - { kind: goblin, count: 3 }\n";

static gchar *
write_project_file (EditorAppFixture *f,
                    const gchar      *rel,
                    const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	gchar            *path;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
	return path;
}

/* Opens @rel (written with @text) in its data editor and lays it out. */
static LrgEditorDataEditor *
open_editor (EditorAppFixture *f,
             const gchar      *rel,
             const gchar      *text,
             GType             expected)
{
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *path = write_project_file (f, rel, text);
	LrgEditorDataEditor *e = lrg_editor_data_editor_open (f->app, path, &error);

	g_assert_no_error (error);
	g_assert_nonnull (e);
	g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (e, expected));
	efx_frames (f, 4);
	g_assert_cmpfloat (lrg_widget_get_width (LRG_WIDGET (e)), >, 200.0f);
	return e;
}

static LrgWidget *
find_in (gpointer     root,
         const gchar *name)
{
	LrgWidget *w = lrg_widget_find (LRG_WIDGET (root), name);

	if (w == NULL)
		g_error ("no widget named %s", name);
	return w;
}

static void
click_in (EditorAppFixture *f,
          gpointer          root,
          const gchar      *name)
{
	efx_click_widget (f, find_in (root, name));
	efx_frames (f, 2);
}

static const gchar *
scalar (LrgEditorDataEditor *e,
        const gchar         *path)
{
	return lrg_editor_document_get_scalar (lrg_editor_data_editor_get_document (e), path);
}

/* Types @text a few characters per frame (the canvas input carries a
 * short text buffer per frame). */
static void
type_text (EditorAppFixture *f,
           const gchar      *text)
{
	const gchar *p = text;

	while (*p != '\0')
	{
		g_autofree gchar *chunk = NULL;
		const gchar      *end = p;
		guint             n;

		for (n = 0; n < 8 && *end != '\0'; n++)
			end = g_utf8_next_char (end);
		chunk = g_strndup (p, (gsize) (end - p));
		efx_type (f, chunk);
		p = end;
	}
}

/* Applies @value to the grid row @name (one edit gesture, like a commit
 * in the row's own editor). */
static void
grid_apply (EditorAppFixture *f,
            LrgPropertyGrid  *grid,
            const gchar      *name,
            const GValue     *value)
{
	LrgPropertyRow *row = lrg_property_grid_get_row (grid, name);

	if (row == NULL)
		g_error ("no grid row %s", name);
	lrg_property_row_apply_value (row, value);
	efx_frames (f, 2);
}

static void
grid_apply_string (EditorAppFixture *f,
                   LrgPropertyGrid  *grid,
                   const gchar      *name,
                   const gchar      *text)
{
	GValue v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, text);
	grid_apply (f, grid, name, &v);
	g_value_unset (&v);
}

/* ==========================================================================
 * HUD layout editor
 * ========================================================================== */

/* Opening shows every piece; clicking one selects it; dragging the
 * top-left piece into the bottom-right third re-anchors it there (the
 * engine's place_rect rule) in one undo step; the saved file loads in
 * LrgHudLayout with the new placement. */
static void
test_hud_drag (EditorAppFixture *f,
               gconstpointer     data)
{
	LrgEditorDataEditor      *e = open_editor (f, "data/game.hud", HUD_YAML, LRG_TYPE_EDITOR_HUD_LAYOUT_EDITOR);
	LrgEditorHudLayoutEditor *hud = LRG_EDITOR_HUD_LAYOUT_EDITOR (e);
	LrgEditorDocument        *doc = lrg_editor_data_editor_get_document (e);
	LrgWidget                *stage = find_in (e, "hud-stage");
	g_autoptr(LrgHudLayout)   layout = NULL;
	g_autoptr(GError)         error = NULL;
	gfloat                    px, py, pw, ph, sx, sy, sw, sh, tx, ty;
	LrgHudAnchor              anchor;
	gfloat                    ox, oy;

	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "health", &px, &py, &pw, &ph));
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "quest", NULL, NULL, NULL, NULL));
	g_assert_cmpfloat (pw, >, 4.0f);

	/* a click selects */
	efx_click (f, px + pw * 0.5f, py + ph * 0.5f);
	g_assert_cmpstr (lrg_editor_hud_layout_editor_get_selected (hud), ==, "health");
	g_assert_false (lrg_editor_document_get_dirty (doc));

	/* drag into the bottom-right third of the virtual screen */
	efx_rect_of (stage, &sx, &sy, &sw, &sh);
	tx = sx + sw * 0.5f + sw * 0.30f;
	ty = sy + sh * 0.5f + sh * 0.25f;
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, px + pw * 0.5f, py + ph * 0.5f, tx, ty, 6);
	efx_frames (f, 2);
	g_assert_cmpstr (scalar (e, "/pieces/health/anchor"), ==, "bottom-right");
	g_assert_true (lrg_editor_document_get_dirty (doc));

	/* the piece is drawn where it was dropped */
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "health", &px, &py, &pw, &ph));
	g_assert_cmpfloat (fabsf (px + pw * 0.5f - tx), <, 2.0f);
	g_assert_cmpfloat (fabsf (py + ph * 0.5f - ty), <, 2.0f);

	/* one undo step */
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_cmpstr (scalar (e, "/pieces/health/anchor"), ==, "top-left");
	g_assert_cmpstr (scalar (e, "/pieces/health/x"), ==, "16");
	g_assert_true (lrg_editor_data_editor_redo (e));
	g_assert_cmpstr (scalar (e, "/pieces/health/anchor"), ==, "bottom-right");

	/* the locked piece does not move */
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "compass", &px, &py, &pw, &ph));
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, px + pw * 0.5f, py + ph * 0.5f, px + pw * 0.5f, py + 200.0f, 4);
	g_assert_cmpstr (lrg_editor_hud_layout_editor_get_selected (hud), ==, "compass");
	g_assert_cmpstr (scalar (e, "/pieces/compass/y"), ==, "8");

	/* save; the engine reads the placement back */
	g_assert_true (lrg_editor_data_editor_save (e, &error));
	g_assert_no_error (error);
	layout = lrg_hud_layout_new ();
	g_assert_true (lrg_hud_layout_load_from_file (layout, lrg_editor_document_get_path (doc), &error));
	g_assert_no_error (error);
	lrg_hud_layout_add_piece (layout, "health", NULL, LRG_HUD_ANCHOR_TOP_LEFT, 0.0f, 0.0f);
	g_assert_true (lrg_hud_layout_get_position (layout, "health", &anchor, &ox, &oy));
	g_assert_cmpint (anchor, ==, LRG_HUD_ANCHOR_BOTTOM_RIGHT);
	g_assert_cmpfloat (ox, ==, (gfloat) g_ascii_strtod (scalar (e, "/pieces/health/x"), NULL));
	g_assert_cmpuint (lrg_editor_data_editor_validate (e), ==, 0);
}

/* Corner handles resize: the anchor stays, so a top-left piece keeps its
 * offset and a top-right piece grown to the right moves its offset with
 * the right edge. The grid's anchor row converts the offset so the piece
 * does not jump. */
static void
test_hud_resize_and_grid (EditorAppFixture *f,
                          gconstpointer     data)
{
	LrgEditorDataEditor      *e = open_editor (f, "data/game.hud", HUD_YAML, LRG_TYPE_EDITOR_HUD_LAYOUT_EDITOR);
	LrgEditorHudLayoutEditor *hud = LRG_EDITOR_HUD_LAYOUT_EDITOR (e);
	LrgPropertyGrid          *grid = LRG_PROPERTY_GRID (find_in (e, "hud-grid"));
	gfloat                    px, py, pw, ph, w, h, bx, by;
	gfloat                    x0, y0;
	GValue                    v = G_VALUE_INIT;

	/* select, then pull the bottom-right handle out */
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "health", &px, &py, &pw, &ph));
	efx_click (f, px + pw * 0.5f, py + ph * 0.5f);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, px + pw, py + ph, px + pw * 1.5f, py + ph * 2.0f, 5);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_size (hud, "health", &w, &h));
	g_assert_cmpfloat (w, >, 200.0f);
	g_assert_cmpfloat (h, >, 80.0f);
	g_assert_cmpstr (scalar (e, "/pieces/health/anchor"), ==, "top-left");
	g_assert_cmpstr (scalar (e, "/pieces/health/x"), ==, "16");

	/* the top-right piece: its right edge follows the handle */
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "minimap", &px, &py, &pw, &ph));
	efx_click (f, px + pw * 0.5f, py + ph * 0.5f);
	g_assert_cmpstr (lrg_editor_hud_layout_editor_get_selected (hud), ==, "minimap");
	bx = px + pw;
	by = py + ph;
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, bx, by, bx - pw * 0.5f, by, 5);
	efx_frames (f, 2);
	g_assert_cmpstr (scalar (e, "/pieces/minimap/anchor"), ==, "top-right");
	g_assert_cmpfloat (g_ascii_strtod (scalar (e, "/pieces/minimap/x"), NULL), <, -40.0);

	/* grid: anchor top-right -> center keeps the rectangle */
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "minimap", &x0, &y0, NULL, NULL));
	g_value_init (&v, LRG_TYPE_HUD_ANCHOR);
	g_value_set_enum (&v, LRG_HUD_ANCHOR_CENTER);
	grid_apply (f, grid, "anchor", &v);
	g_value_unset (&v);
	g_assert_cmpstr (scalar (e, "/pieces/minimap/anchor"), ==, "center");
	g_assert_true (lrg_editor_hud_layout_editor_get_piece_rect (hud, "minimap", &px, &py, NULL, NULL));
	g_assert_cmpfloat (fabsf (px - x0), <, 2.0f);
	g_assert_cmpfloat (fabsf (py - y0), <, 2.0f);

	/* grid: hidden is a plain path row */
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, TRUE);
	grid_apply (f, grid, "hidden", &v);
	g_value_unset (&v);
	g_assert_cmpstr (scalar (e, "/pieces/minimap/hidden"), ==, "true");

	/* toolbar: add a piece, then remove it */
	click_in (f, e, "hud-add");
	g_assert_cmpstr (lrg_editor_hud_layout_editor_get_selected (hud), ==, "piece-5");
	g_assert_cmpstr (scalar (e, "/pieces/piece-5/anchor"), ==, "top-left");
	click_in (f, e, "hud-remove");
	g_assert_false (lrg_editor_document_has_path (lrg_editor_data_editor_get_document (e), "/pieces/piece-5"));
	g_assert_null (lrg_editor_hud_layout_editor_get_selected (hud));

	/* an issue's path selects its piece */
	lrg_editor_data_editor_reveal (e, "/pieces/quest/hidden", 0);
	g_assert_cmpstr (lrg_editor_hud_layout_editor_get_selected (hud), ==, "quest");
}

/* ==========================================================================
 * UI layout editor
 * ========================================================================== */

/* The preview builds with handlers stubbed, the outline lists the nodes,
 * clicking a preview widget selects its YAML path and puts the caret on
 * its line, and clicking an outline row selects the preview widget. */
static void
test_ui_preview_select (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorDataEditor     *e = open_editor (f, "ui/menu.ui", UI_YAML, LRG_TYPE_EDITOR_UI_LAYOUT_EDITOR);
	LrgEditorUiLayoutEditor *ui = LRG_EDITOR_UI_LAYOUT_EDITOR (e);
	LrgUILoader             *loader = lrg_editor_ui_layout_editor_get_loader (ui);
	LrgTextEditor           *text = lrg_editor_ui_layout_editor_get_text_editor (ui);
	LrgTreeView             *tree = LRG_TREE_VIEW (find_in (e, "ui-outline"));
	LrgWidget               *button;
	guint                    line = 0, col = 0, cl = 0, i;
	gfloat                   rx, ry, rw, rh;
	GObject                 *label_item = NULL;

	g_assert_null (lrg_editor_ui_layout_editor_get_error (ui, NULL));
	g_assert_nonnull (lrg_editor_ui_layout_editor_get_preview (ui));
	g_assert_cmpuint (lrg_editor_ui_layout_editor_get_n_outline_rows (ui), ==, 3);
	g_assert_true (lrg_editor_ui_layout_editor_get_pick_mode (ui));

	/* click the button in the preview */
	button = lrg_ui_loader_get_widget_for_path (loader, "/children/1");
	g_assert_nonnull (button);
	efx_click_widget (f, button);
	efx_frame (f);
	g_assert_cmpstr (lrg_editor_ui_layout_editor_get_selected_path (ui), ==, "/children/1");
	g_assert_true (lrg_ui_loader_get_source_location (loader, button, &line, &col));
	lrg_text_editor_get_cursor (text, &cl, NULL);
	g_assert_cmpuint (cl + 1, ==, line);
	g_assert_false (lrg_editor_document_get_dirty (lrg_editor_data_editor_get_document (e)));

	/* click the label's outline row */
	for (i = 0; i < lrg_tree_view_get_n_visible (tree); i++)
	{
		GObject          *item = lrg_tree_view_get_item (tree, i);
		g_autofree gchar *label = NULL;

		g_object_get (item, "label", &label, NULL);
		if (g_str_has_prefix (label, "LrgLabel"))
			label_item = item;
	}
	g_assert_nonnull (label_item);
	g_assert_true (lrg_tree_view_get_row_rect (tree, label_item, &rx, &ry, &rw, &rh));
	efx_click (f, rx + rw * 0.5f, ry + rh * 0.5f);
	efx_frame (f);
	g_assert_cmpstr (lrg_editor_ui_layout_editor_get_selected_path (ui), ==, "/children/0");
	lrg_text_editor_get_cursor (text, &cl, NULL);
	g_assert_cmpuint (cl, ==, 2);

	/* interact mode: clicks reach the widgets, the selection stays */
	lrg_editor_ui_layout_editor_set_pick_mode (ui, FALSE);
	efx_frame (f);
	efx_click_widget (f, button);
	g_assert_cmpstr (lrg_editor_ui_layout_editor_get_selected_path (ui), ==, "/children/0");
	click_in (f, e, "ui-pick");
	g_assert_true (lrg_editor_ui_layout_editor_get_pick_mode (ui));

	/* caret into the button's node selects it */
	lrg_canvas_set_focused_widget (lrg_editor_app_get_canvas (f->app), LRG_WIDGET (text));
	lrg_text_editor_set_cursor (text, 6, 2);
	efx_frame (f);
	g_assert_cmpstr (lrg_editor_ui_layout_editor_get_selected_path (ui), ==, "/children/1");
}

/* Typing applies after the debounce as one document edit; broken YAML
 * keeps the last preview and marks the line without touching the
 * document; a loader error (valid YAML) is written and shown; undo puts
 * the document's text back in the pane. */
static void
test_ui_typing_errors (EditorAppFixture *f,
                       gconstpointer     data)
{
	LrgEditorDataEditor     *e = open_editor (f, "ui/menu.ui", UI_YAML, LRG_TYPE_EDITOR_UI_LAYOUT_EDITOR);
	LrgEditorUiLayoutEditor *ui = LRG_EDITOR_UI_LAYOUT_EDITOR (e);
	LrgEditorDocument       *doc = lrg_editor_data_editor_get_document (e);
	LrgTextEditor           *text = lrg_editor_ui_layout_editor_get_text_editor (ui);
	LrgWidget               *before = lrg_editor_ui_layout_editor_get_preview (ui);
	g_autofree gchar        *typed = NULL;
	guint                    line = 0;

	/* type a third child at the end */
	lrg_canvas_set_focused_widget (lrg_editor_app_get_canvas (f->app), LRG_WIDGET (text));
	lrg_text_editor_set_cursor (text, lrg_text_editor_get_line_count (text), 0);
	efx_frame (f);
	type_text (f, "  - {type: label, text: Bye}\n");
	g_assert_true (lrg_editor_ui_layout_editor_get_pending (ui));
	g_assert_false (lrg_editor_document_get_dirty (doc));
	efx_frames (f, 30);
	g_assert_false (lrg_editor_ui_layout_editor_get_pending (ui));
	g_assert_null (lrg_editor_ui_layout_editor_get_error (ui, NULL));
	g_assert_true (lrg_editor_document_get_dirty (doc));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/children/2/text"), ==, "Bye");
	g_assert_cmpuint (lrg_editor_ui_layout_editor_get_n_outline_rows (ui), ==, 4);
	g_assert_true (lrg_editor_ui_layout_editor_get_preview (ui) != before);
	before = lrg_editor_ui_layout_editor_get_preview (ui);

	/* broken YAML: preview and document stay, the line is marked */
	lrg_text_editor_set_text (text, "type: vbox\nchildren: [\n  {type: label\n");
	efx_frames (f, 30);
	g_assert_nonnull (lrg_editor_ui_layout_editor_get_error (ui, &line));
	g_assert_cmpuint (line, >, 0);
	g_assert_true (lrg_editor_ui_layout_editor_get_preview (ui) == before);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/children/2/text"), ==, "Bye");

	/* valid YAML the loader refuses: written, error shown with its line */
	lrg_text_editor_set_text (text, "type: vbox\nchildren:\n  - type: label\n    colour: red\n");
	g_assert_false (lrg_editor_ui_layout_editor_apply (ui));
	g_assert_nonnull (strstr (lrg_editor_ui_layout_editor_get_error (ui, &line), "colour"));
	g_assert_cmpuint (line, ==, 4);   /* the offending key */
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/children/0/colour"), ==, "red");

	/* undo: the document's previous text comes back into the pane */
	g_assert_true (lrg_editor_data_editor_undo (e));
	efx_frame (f);
	typed = lrg_text_editor_get_text (text);
	g_assert_nonnull (strstr (typed, "Bye"));
	g_assert_null (lrg_editor_ui_layout_editor_get_error (ui, NULL));
	g_assert_cmpuint (lrg_editor_ui_layout_editor_get_n_outline_rows (ui), ==, 4);
}

/* ==========================================================================
 * Dialog tree editor
 * ========================================================================== */

/* Canvas point of a port. */
static void
port_point (LrgNodeGraph          *graph,
            const gchar           *node,
            LrgGraphPortDirection  dir,
            guint                  index,
            gfloat                *x,
            gfloat                *y)
{
	LrgGraphNode *card = lrg_node_graph_get_node (graph, node);
	LrgGraphPort *port;
	gfloat        gx, gy;

	g_assert_nonnull (card);
	port = lrg_graph_node_get_port (card, dir, index);
	g_assert_nonnull (port);
	lrg_node_graph_get_port_position (graph, port, &gx, &gy);
	lrg_node_graph_graph_to_screen (graph, gx, gy, x, y);
}

/* Cards and links mirror the document; wiring a port in the graph writes
 * `next` (one undo step); dragging a card stores its position; a missing
 * target shows as a "missing:" card and as a validation error whose issue
 * selects the node. */
static void
test_dialog_graph (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorDataEditor       *e = open_editor (f, "data/intro.dialog", DIALOG_YAML,
	                                            LRG_TYPE_EDITOR_DIALOG_TREE_EDITOR);
	LrgEditorDialogTreeEditor *dlg = LRG_EDITOR_DIALOG_TREE_EDITOR (e);
	LrgEditorDocument         *doc = lrg_editor_data_editor_get_document (e);
	LrgNodeGraph              *graph = lrg_editor_dialog_tree_editor_get_graph (dlg);
	GPtrArray                 *issues;
	gfloat                     x0, y0, x1, y1, bx, by, bw, bh;
	g_autofree gchar          *id = NULL;
	g_autoptr(GError)          error = NULL;
	guint                      i;
	gboolean                   found = FALSE;

	efx_frames (f, 2);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (graph), ==, 3);
	g_assert_cmpuint (lrg_node_graph_get_n_links (graph), ==, 3);   /* 2 responses + friend->foe chain */
	g_assert_cmpuint (lrg_graph_node_get_n_ports (lrg_node_graph_get_node (graph, "hello"), LRG_GRAPH_PORT_OUTPUT),
	                  ==, 2);

	/* wire friend.next -> hello.in on the canvas */
	port_point (graph, "friend", LRG_GRAPH_PORT_OUTPUT, 0, &x0, &y0);
	port_point (graph, "hello", LRG_GRAPH_PORT_INPUT, 0, &x1, &y1);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x0, y0, x1, y1, 8);
	efx_frames (f, 2);
	g_assert_cmpstr (scalar (e, "/nodes/1/next"), ==, "hello");
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc), ==, "Edit dialog graph");
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_false (lrg_editor_document_has_path (doc, "/nodes/1/next"));
	g_assert_true (lrg_editor_data_editor_redo (e));

	/* drag the foe card by its header: its position is stored */
	efx_frames (f, 2);
	lrg_node_graph_get_node_bounds (graph, lrg_node_graph_get_node (graph, "foe"), &bx, &by, &bw, &bh);
	lrg_node_graph_graph_to_screen (graph, bx + bw * 0.5f, by + 8.0f, &x0, &y0);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x0, y0, x0 + 60.0f, y0 + 40.0f, 6);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_document_has_path (doc, "/nodes/2/editor/x"));
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_selected (dlg), ==, "foe");

	/* a dangling reference: a missing card and an error that reveals */
	g_assert_true (lrg_editor_dialog_tree_editor_link (dlg, "foe", -1, "ghost", &error));
	g_assert_no_error (error);
	efx_frame (f);
	g_assert_nonnull (lrg_node_graph_get_node (graph, "missing:ghost"));
	g_assert_cmpuint (lrg_editor_data_editor_validate (e), >, 0);
	lrg_editor_dialog_tree_editor_select (dlg, NULL);
	issues = lrg_editor_data_editor_get_issues (e);
	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

		if (issue->path != NULL && strstr (issue->message, "ghost") != NULL)
		{
			g_assert_true (lrg_editor_data_editor_activate_issue (e, i));
			found = TRUE;
		}
	}
	g_assert_true (found);
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_selected (dlg), ==, "foe");

	/* toolbar: a new node; the graph and the file follow */
	click_in (f, e, "dialog-add-node");
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_selected (dlg), ==, "node");
	g_assert_cmpstr (scalar (e, "/nodes/3/id"), ==, "node");
	g_assert_nonnull (lrg_node_graph_get_node (graph, "node"));
	id = lrg_editor_dialog_tree_editor_add_node (dlg, NULL, 0.0f, 400.0f, &error);
	g_assert_cmpstr (id, ==, "node-2");
}

/* The grid renames a node and every reference; Add Response moves the
 * node's own next onto the response; a node deleted in the graph is
 * removed from the file. */
static void
test_dialog_grid (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorDataEditor       *e = open_editor (f, "data/intro.dialog", DIALOG_YAML,
	                                            LRG_TYPE_EDITOR_DIALOG_TREE_EDITOR);
	LrgEditorDialogTreeEditor *dlg = LRG_EDITOR_DIALOG_TREE_EDITOR (e);
	LrgEditorDocument         *doc = lrg_editor_data_editor_get_document (e);
	LrgNodeGraph              *graph = lrg_editor_dialog_tree_editor_get_graph (dlg);
	LrgPropertyGrid           *grid = LRG_PROPERTY_GRID (find_in (e, "dialog-grid"));
	g_autoptr(GError)          error = NULL;
	gfloat                     bx, by, bw, bh, x, y;

	/* click the friend card */
	efx_frames (f, 2);
	lrg_node_graph_get_node_bounds (graph, lrg_node_graph_get_node (graph, "friend"), &bx, &by, &bw, &bh);
	lrg_node_graph_graph_to_screen (graph, bx + bw * 0.5f, by + 8.0f, &x, &y);
	efx_click (f, x, y);
	efx_frame (f);
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_selected (dlg), ==, "friend");
	g_assert_nonnull (lrg_property_grid_get_row (grid, "text"));

	/* rename friend -> ally: hello's response follows */
	grid_apply_string (f, grid, "id", "ally");
	g_assert_cmpstr (scalar (e, "/nodes/1/id"), ==, "ally");
	g_assert_cmpstr (scalar (e, "/nodes/0/responses/0/next"), ==, "ally");
	g_assert_nonnull (lrg_node_graph_get_node (graph, "ally"));
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_selected (dlg), ==, "ally");
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_cmpstr (scalar (e, "/nodes/0/responses/0/next"), ==, "friend");

	/* text through the grid */
	lrg_editor_dialog_tree_editor_select (dlg, "friend");
	grid_apply_string (f, grid, "text", "You may pass.");
	g_assert_cmpstr (scalar (e, "/nodes/1/text"), ==, "You may pass.");

	/* the start node row with nothing selected */
	lrg_editor_dialog_tree_editor_select (dlg, NULL);
	efx_frame (f);
	grid_apply_string (f, grid, "start", "foe");
	g_assert_cmpstr (scalar (e, "/start"), ==, "foe");

	/* responses: friend gets one, the grid grows its rows */
	g_assert_true (lrg_editor_dialog_tree_editor_link (dlg, "friend", -1, "foe", &error));
	lrg_editor_dialog_tree_editor_select (dlg, "friend");
	click_in (f, e, "dialog-add-response");
	g_assert_cmpstr (scalar (e, "/nodes/1/responses/0/next"), ==, "foe");
	g_assert_false (lrg_editor_document_has_path (doc, "/nodes/1/next"));
	efx_frame (f);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "response-1-text"));
	g_assert_cmpuint (lrg_graph_node_get_n_ports (lrg_node_graph_get_node (graph, "friend"), LRG_GRAPH_PORT_OUTPUT),
	                  ==, 1);

	/* delete the foe card in the graph (Delete key on the selection) */
	lrg_node_graph_select_node (graph, lrg_node_graph_get_node (graph, "foe"), FALSE);
	lrg_canvas_set_focused_widget (lrg_editor_app_get_canvas (f->app), LRG_WIDGET (graph));
	efx_key (f, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE);
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/nodes"), ==, 2);
	g_assert_cmpstr (scalar (e, "/nodes/1/responses/0/next"), ==, "foe");
	g_assert_nonnull (lrg_node_graph_get_node (graph, "missing:foe"));
}

/* Play test: the engine runner walks the document; buttons choose. */
static void
test_dialog_play (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorDataEditor       *e = open_editor (f, "data/intro.dialog", DIALOG_YAML,
	                                            LRG_TYPE_EDITOR_DIALOG_TREE_EDITOR);
	LrgEditorDialogTreeEditor *dlg = LRG_EDITOR_DIALOG_TREE_EDITOR (e);
	LrgNodeGraph              *graph = lrg_editor_dialog_tree_editor_get_graph (dlg);
	GList                     *sel;

	g_assert_null (lrg_editor_dialog_tree_editor_get_play_node (dlg));
	click_in (f, e, "dialog-play-start");
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_play_node (dlg), ==, "hello");
	sel = lrg_node_graph_get_selected_nodes (graph);
	g_assert_cmpstr (lrg_graph_node_get_id (sel->data), ==, "hello");
	g_list_free (sel);

	/* "1. Friend" */
	click_in (f, e, "dialog-choice-1");
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_play_node (dlg), ==, "friend");
	/* friend chains to foe (no next, no responses): Continue */
	click_in (f, e, "dialog-choice-0");
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_play_node (dlg), ==, "foe");
	/* the last node: End */
	click_in (f, e, "dialog-choice-0");
	g_assert_null (lrg_editor_dialog_tree_editor_get_play_node (dlg));

	/* from the selected node */
	lrg_editor_dialog_tree_editor_select (dlg, "foe");
	click_in (f, e, "dialog-play-here");
	g_assert_cmpstr (lrg_editor_dialog_tree_editor_get_play_node (dlg), ==, "foe");
}

/* ==========================================================================
 * Procgen dungeon editor
 * ========================================================================== */

static guint64
engine_hash (const gchar *yaml,
             guint64      seed)
{
	g_autoptr(GError)            error = NULL;
	g_autoptr(LrgProcgenDungeon) dungeon = lrg_procgen_dungeon_new_from_data (yaml, -1, &error);
	g_autoptr(LrgProcgenGrid)    grid = NULL;

	g_assert_no_error (error);
	grid = lrg_procgen_dungeon_generate (dungeon, seed, &error);
	g_assert_no_error (error);
	return lrg_procgen_grid_hash (grid);
}

/* The preview is the engine's map for the seed; Randomize writes a new
 * seed (undoable, the preview follows after the pause); form rows edit
 * the recipe; a recipe the loader rejects keeps the map and shows why. */
static void
test_dungeon (EditorAppFixture *f,
              gconstpointer     data)
{
	LrgEditorDataEditor    *e = open_editor (f, "data/cave.dungeon", DUNGEON_YAML, LRG_TYPE_EDITOR_DUNGEON_EDITOR);
	LrgEditorDungeonEditor *dun = LRG_EDITOR_DUNGEON_EDITOR (e);
	LrgEditorDocument      *doc = lrg_editor_data_editor_get_document (e);
	LrgPropertyGrid        *form = lrg_editor_dungeon_editor_get_form (dun);
	LrgProcgenGrid         *grid = lrg_editor_dungeon_editor_get_grid (dun);
	guint64                 h7 = engine_hash (DUNGEON_YAML, 7);
	guint64                 seed;
	GValue                  v = G_VALUE_INIT;
	g_autoptr(GError)       error = NULL;

	g_assert_nonnull (grid);
	g_assert_cmpuint (lrg_procgen_grid_get_width (grid), ==, 40);
	g_assert_cmpuint (lrg_procgen_grid_hash (grid), ==, h7);
	g_assert_cmpuint (lrg_procgen_grid_get_spawn_count (grid), ==, 3);
	g_assert_nonnull (lrg_property_grid_get_row (form, "param-min-room-size"));
	g_assert_nonnull (lrg_property_grid_get_row (form, "spawn-1-kind"));

	/* Randomize: a new seed in the file and a new map */
	click_in (f, e, "dungeon-randomize");
	seed = lrg_editor_dungeon_editor_get_seed (dun);
	g_assert_cmpuint (seed, !=, 7);
	g_assert_cmpuint (g_ascii_strtoull (lrg_editor_document_get_scalar (doc, "/seed"), NULL, 10), ==, seed);
	g_assert_cmpuint (lrg_procgen_grid_hash (lrg_editor_dungeon_editor_get_grid (dun)), ==,
	                  engine_hash (DUNGEON_YAML, seed));

	/* undo: seed 7 again; the preview regenerates after the pause */
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_true (lrg_editor_dungeon_editor_get_pending (dun));
	efx_frames (f, 30);
	g_assert_false (lrg_editor_dungeon_editor_get_pending (dun));
	g_assert_cmpuint (lrg_procgen_grid_hash (lrg_editor_dungeon_editor_get_grid (dun)), ==, h7);

	/* the form: width, and the generator switches the parameter rows */
	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, 50);
	grid_apply (f, form, "width", &v);
	g_value_unset (&v);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/width"), ==, "50");
	efx_frames (f, 30);
	g_assert_cmpuint (lrg_procgen_grid_get_width (lrg_editor_dungeon_editor_get_grid (dun)), ==, 50);
	grid_apply_string (f, form, "generator", "caves");
	efx_frames (f, 30);
	g_assert_nonnull (lrg_property_grid_get_row (form, "param-fill-percent"));
	g_assert_null (lrg_property_grid_get_row (form, "param-min-room-size"));

	/* a stale bsp parameter makes the caves recipe invalid: map kept */
	grid = lrg_editor_dungeon_editor_get_grid (dun);
	g_assert_true (lrg_editor_document_replace (doc, "/params", "{ min-room-size: 4 }", &error));
	g_assert_no_error (error);
	g_assert_false (lrg_editor_dungeon_editor_generate (dun, NULL));
	g_assert_nonnull (lrg_editor_dungeon_editor_get_error (dun));
	g_assert_true (lrg_editor_dungeon_editor_get_grid (dun) == grid);
	g_assert_cmpuint (lrg_editor_data_editor_validate (e), >, 0);

	/* Add Spawn grows the spawn rows */
	g_assert_true (lrg_editor_data_editor_undo (e));
	click_in (f, e, "dungeon-add-spawn");
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/spawns"), ==, 2);
	g_assert_nonnull (lrg_property_grid_get_row (form, "spawn-2-count"));
	efx_frames (f, 30);
	g_assert_null (lrg_editor_dungeon_editor_get_error (dun));
}

/* ==========================================================================
 * Rendering smoke: every editor records paint in every theme preset
 * ========================================================================== */

static void
test_paint_all (EditorAppFixture *f,
                gconstpointer     data)
{
	static const struct { const gchar *rel; const gchar **text; GType (*type) (void); } files[] = {
		{ "data/game.hud", &HUD_YAML, lrg_editor_hud_layout_editor_get_type },
		{ "ui/menu.ui", &UI_YAML, lrg_editor_ui_layout_editor_get_type },
		{ "data/intro.dialog", &DIALOG_YAML, lrg_editor_dialog_tree_editor_get_type },
		{ "data/cave.dungeon", &DUNGEON_YAML, lrg_editor_dungeon_editor_get_type }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (files); i++)
	{
		LrgEditorDataEditor   *e = open_editor (f, files[i].rel, *files[i].text, files[i].type ());
		g_autofree gchar      *state = lrg_editor_data_editor_get_state (e);
		LrgPaintList          *list = lrg_canvas_record (lrg_editor_app_get_canvas (f->app));

		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 50);
		g_assert_nonnull (state);
	}
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */

#define ADD(path, fn) g_test_add (path, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("/editor-app/data-2/hud/drag", test_hud_drag);
	ADD ("/editor-app/data-2/hud/resize-grid", test_hud_resize_and_grid);
	ADD ("/editor-app/data-2/ui/preview-select", test_ui_preview_select);
	ADD ("/editor-app/data-2/ui/typing-errors", test_ui_typing_errors);
	ADD ("/editor-app/data-2/dialog/graph", test_dialog_graph);
	ADD ("/editor-app/data-2/dialog/grid", test_dialog_grid);
	ADD ("/editor-app/data-2/dialog/play", test_dialog_play);
	ADD ("/editor-app/data-2/dungeon", test_dungeon);
	ADD ("/editor-app/data-2/paint", test_paint_all);
#undef ADD

	return g_test_run ();
}
