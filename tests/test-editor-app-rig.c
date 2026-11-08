/* test-editor-app-rig.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 2D rigging and animation editor (.rskel2d,
 * LrgEditorSkeleton2DEditor) in a headless LrgEditorApp
 * (tests/editor-app-fixture.h): opening through the data editor registry,
 * the bone tool and setup-pose drags on the canvas, slots, attachments
 * and image drops, the mesh tool (vertices + automatic triangulation),
 * automatic weights and the weight brush, skins, renaming and removing
 * with references fixed up, comments surviving edits, animation mode
 * (auto-key drags, key pose, dope sheet, key moves keeping Bézier
 * control points, curve edits, play/loop), undo of every edit as one
 * document step, and every skel2d.* IPC method. Rigs are built
 * procedurally; images are generated PNGs.
 *
 * The /gpu/ cases need a display (only ever the isolated Xvfb of
 * xrun.sh): they render the textured rig into an image.
 */

#include "editor-app-fixture.h"
#include <raylib.h>

/* ==========================================================================
 * Fixtures and helpers
 * ========================================================================== */

static const gchar *ARM_RIG =
	"rskel2d: 1\n"
	"name: arm\n"
	"# the bones of the arm\n"
	"bones:\n"
	"  - name: upper\n"
	"    length: 50\n"
	"  - name: fore\n"
	"    parent: upper\n"
	"    x: 50\n"
	"    length: 50\n"
	"slots:\n"
	"  - name: arm\n"
	"    bone: upper\n"
	"    attachment: arm\n"
	"skins:\n"
	"  - name: default\n"
	"    attachments:\n"
	"      - slot: arm\n"
	"        name: arm\n"
	"        vertices:\n"
	"          - [0, -8, 0, 0]\n"
	"          - [100, -8, 1, 0]\n"
	"          - [0, 8, 0, 1]\n"
	"          - [100, 8, 1, 1]\n"
	"        triangles:\n"
	"          - [0, 1, 2]\n"
	"          - [1, 3, 2]\n"
	"animations:\n"
	"  - name: wave\n"
	"    duration: 1\n"
	"    loop: true\n"
	"    bones:\n"
	"      - bone: fore\n"
	"        rotate:\n"
	"          - {time: 0, angle: 0, curve: [0.42, 0, 0.58, 1]}\n"
	"          - {time: 1, angle: 45}\n";

static gchar *
write_project_file (EditorAppFixture *f,
                    const gchar      *rel,
                    const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	gchar            *path;
	g_autofree gchar *dir = NULL;

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

/* A solid-colour PNG in the project. */
static gchar *
write_png (EditorAppFixture *f,
           const gchar      *rel,
           gint              w,
           gint              h,
           guint8            r,
           guint8            g,
           guint8            b)
{
	g_autofree gchar   *path = write_project_file (f, rel, "");
	GrlColor            c = { r, g, b, 255 };
	g_autoptr(GrlImage) img = grl_image_new_color (w, h, &c);

	g_unlink (path);
	g_assert_true (grl_image_export (img, path));
	return g_steal_pointer (&path);
}

static gchar *
doc_text (LrgEditorSkeleton2DEditor *ed)
{
	return lrg_editor_document_to_data (lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed)), NULL);
}

static LrgEditorDocument *
doc_of (LrgEditorSkeleton2DEditor *ed)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));
}

/* Opens the arm rig with a fixed view (2 px per unit, origin at the canvas
 * centre) so canvas positions are computable. */
static LrgEditorSkeleton2DEditor *
open_arm (EditorAppFixture *f)
{
	g_autofree gchar          *path = write_project_file (f, "rigs/arm.rskel2d", ARM_RIG);
	g_autoptr(GError)          error = NULL;
	LrgEditorSkeleton2DEditor *ed = lrg_editor_skeleton2d_editor_open (f->app, "rigs/arm.rskel2d", &error);

	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 3);
	lrg_editor_skeleton2d_editor_set_view (ed, 2.0f, -60.0f, 0.0f);
	efx_frame (f);
	return ed;
}

static void
canvas_at (LrgEditorSkeleton2DEditor *ed,
           gfloat                     x,
           gfloat                     y,
           gfloat                    *cx,
           gfloat                    *cy)
{
	lrg_editor_skeleton2d_editor_setup_to_canvas (ed, x, y, cx, cy);
}

/* Drags on the canvas between two setup-space points. */
static void
setup_drag (EditorAppFixture          *f,
            LrgEditorSkeleton2DEditor *ed,
            gfloat                     x0,
            gfloat                     y0,
            gfloat                     x1,
            gfloat                     y1)
{
	gfloat ax, ay, bx, by;

	canvas_at (ed, x0, y0, &ax, &ay);
	canvas_at (ed, x1, y1, &bx, &by);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, ax, ay, bx, by, 6);
	efx_frame (f);
}

static void
setup_click (EditorAppFixture          *f,
             LrgEditorSkeleton2DEditor *ed,
             gfloat                     x,
             gfloat                     y)
{
	gfloat cx, cy;

	canvas_at (ed, x, y, &cx, &cy);
	efx_click (f, cx, cy);
	efx_frame (f);
}

static gboolean
loader_warnings_ok (const gchar    *domain,
                    GLogLevelFlags  level,
                    const gchar    *message,
                    gpointer        user_data)
{
	return !(domain != NULL && g_str_has_prefix (domain, "Libregnum-") && (level & G_LOG_LEVEL_WARNING) &&
	         g_strcmp0 (domain, "Libregnum-Editor") != 0);
}

/* ==========================================================================
 * Opening and the model
 * ========================================================================== */

/* .rskel2d opens in the rig editor through the data-editor registry, the
 * working skeleton loads and the state line reports it. */
static void
test_rig_open (EditorAppFixture *f,
               gconstpointer     data)
{
	g_autofree gchar          *path = write_project_file (f, "rigs/arm.rskel2d", ARM_RIG);
	g_autoptr(GError)          error = NULL;
	LrgEditorDataEditor       *e;
	LrgEditorSkeleton2DEditor *ed;
	LrgSkeleton2D             *skel;
	g_autofree gchar          *state = NULL;

	g_assert_true (lrg_editor_data_editor_can_open (path));
	e = lrg_editor_data_editor_open (f->app, "rigs/arm.rskel2d", &error);
	g_assert_no_error (error);
	g_assert_true (LRG_IS_EDITOR_SKELETON2D_EDITOR (e));
	ed = LRG_EDITOR_SKELETON2D_EDITOR (e);
	efx_frames (f, 2);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_nonnull (skel);
	g_assert_null (lrg_editor_skeleton2d_editor_get_parse_error (ed));
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (skel), ==, 2);
	g_assert_cmpuint (lrg_skeleton2d_get_slot_count (skel), ==, 1);
	g_assert_cmpuint (lrg_skeleton2d_get_clip_count (skel), ==, 1);
	state = lrg_editor_data_editor_get_state (e);
	g_assert_nonnull (strstr (state, "mode=setup"));
	g_assert_nonnull (strstr (state, "bones=2"));
	g_assert_cmpuint (lrg_editor_data_editor_validate (e), ==, 0);
	g_assert_nonnull (efx_find (f, "skel-canvas"));
	g_assert_nonnull (efx_find (f, "skel-outline"));
	g_assert_cmpfloat (lrg_widget_get_width (efx_find (f, "skel-canvas")), >, 100.0f);
	/* the frame fitted the rig on the first paint */
	{
		gfloat zoom;

		lrg_editor_skeleton2d_editor_get_view (ed, &zoom, NULL, NULL);
		g_assert_cmpfloat (zoom, >, 0.5f);
	}
}

/* A broken document shows its error at the node path and refuses edits;
 * fixing the text recovers. */
static void
test_rig_parse_error (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	LrgEditorDocument         *doc = doc_of (ed);
	g_autoptr(GError)          error = NULL;
	GPtrArray                 *issues;
	LrgEditorDocumentIssue    *issue;

	g_assert_true (lrg_editor_document_set_string (doc, "/bones/1/parent", "nobody", 0, NULL));
	efx_frame (f);
	g_assert_nonnull (lrg_editor_skeleton2d_editor_get_parse_error (ed));
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), ==, 1);
	issues = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (ed));
	issue = g_ptr_array_index (issues, 0);
	g_assert_true (g_str_has_prefix (issue->path, "/bones/1"));
	g_assert_cmpuint (issue->line, >, 0);
	g_assert_cmpint (lrg_editor_skeleton2d_editor_add_bone (ed, "x", -1, 0, 0, 0, 10, &error), ==, -1);
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frame (f);
	g_assert_null (lrg_editor_skeleton2d_editor_get_parse_error (ed));
}

/* ==========================================================================
 * Bones on the canvas
 * ========================================================================== */

/* The bone tool: a drag adds a child of the selected bone whose origin,
 * direction and length follow the drag; a click picks the parent. One
 * undo step per bone. */
static void
test_rig_bone_tool (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	LrgSkeleton2D             *skel;
	gfloat                     x, y, r, sx, sy;
	gint                       b;

	lrg_editor_skeleton2d_editor_set_tool (ed, LRG_EDITOR_SKELETON2D_TOOL_BONE);
	/* a click on "fore" selects it as the parent */
	setup_click (f, ed, 75.0f, 0.0f);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_cmpstr (lrg_skeleton2d_get_bone_name (skel, lrg_editor_skeleton2d_editor_get_selected_bone (ed)), ==,
	                 "fore");

	/* drag from the fore tip straight down 40 units: a child pointing down */
	setup_drag (f, ed, 100.0f, 0.0f, 100.0f, 40.0f);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (skel), ==, 3);
	b = lrg_skeleton2d_find_bone (skel, "bone1");
	g_assert_cmpint (b, ==, 2);
	g_assert_cmpint (lrg_skeleton2d_get_bone_parent (skel, b), ==, lrg_skeleton2d_find_bone (skel, "fore"));
	lrg_skeleton2d_get_bone_setup (skel, b, &x, &y, &r, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (x, 50.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (y, 0.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (r, 90.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_bone_length (skel, b), 40.0f, 0.5f);
	g_assert_cmpint (lrg_editor_skeleton2d_editor_get_selected_bone (ed), ==, b);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Add bone");

	/* undo removes it, redo brings it back */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frame (f);
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (lrg_editor_skeleton2d_editor_get_skeleton (ed)), ==, 2);
	g_assert_true (lrg_editor_data_editor_redo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frame (f);
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (lrg_editor_skeleton2d_editor_get_skeleton (ed)), ==, 3);

	/* B / Q switch tools from the keyboard */
	efx_key (f, GRL_KEY_Q, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpint (lrg_editor_skeleton2d_editor_get_tool (ed), ==, LRG_EDITOR_SKELETON2D_TOOL_SELECT);
	efx_key (f, GRL_KEY_B, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpint (lrg_editor_skeleton2d_editor_get_tool (ed), ==, LRG_EDITOR_SKELETON2D_TOOL_BONE);
}

/* The select tool: dragging a joint moves the bone's setup translation,
 * dragging its body rotates it; each drag is one undo step; Escape
 * cancels a drag. */
static void
test_rig_setup_drags (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	LrgSkeleton2D             *skel;
	gfloat                     x, y, r, sx, sy, cx, cy;
	gint                       fore;

	/* move fore's joint from (50, 0) to (60, 10) */
	setup_drag (f, ed, 50.0f, 0.0f, 60.0f, 10.0f);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	fore = lrg_skeleton2d_find_bone (skel, "fore");
	g_assert_cmpint (lrg_editor_skeleton2d_editor_get_selected_bone (ed), ==, fore);
	lrg_skeleton2d_get_bone_setup (skel, fore, &x, &y, &r, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (x, 60.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (y, 10.0f, 0.5f);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Move bone");

	/* rotate fore by dragging its body: from its middle to straight below
	 * its joint (+90 degrees) */
	setup_drag (f, ed, 85.0f, 10.0f, 60.0f, 40.0f);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	lrg_skeleton2d_get_bone_setup (skel, fore, &x, &y, &r, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (r, 90.0f, 1.0f);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Rotate bone");

	/* one undo per drag */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frame (f);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	lrg_skeleton2d_get_bone_setup (skel, fore, &x, &y, &r, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (r, 0.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (x, 60.0f, 0.5f);

	/* Escape mid-drag restores */
	canvas_at (ed, 60.0f, 10.0f, &cx, &cy);
	efx_move (f, cx, cy);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_move (f, cx + 40.0f, cy + 40.0f);
	efx_key (f, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE);
	efx_button (f, cx + 40.0f, cy + 40.0f, GRL_MOUSE_BUTTON_LEFT, FALSE);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	lrg_skeleton2d_get_bone_setup (skel, fore, &x, &y, &r, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (x, 60.0f, 0.5f);

	/* the property grid edits the selected bone as a document path */
	g_assert_nonnull (lrg_property_grid_get_row (LRG_PROPERTY_GRID (efx_find (f, "skel-grid")), "bone-x"));
}

/* Comments in untouched sections survive edits (only changed nodes are
 * written back). */
static void
test_rig_comments (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *text = NULL;
	const gfloat               red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	g_assert_true (lrg_editor_skeleton2d_editor_set_slot_color (ed, 0, red, &error));
	g_assert_no_error (error);
	text = doc_text (ed);
	g_assert_nonnull (strstr (text, "# the bones of the arm"));
	g_assert_nonnull (strstr (text, "color:"));
	g_assert_true (lrg_editor_document_get_dirty (doc_of (ed)));
}

/* ==========================================================================
 * Slots, attachments, skins
 * ========================================================================== */

/* Dropping an image adds a quad attachment named after the file (path
 * relative to the rig) on the selected slot, or on a new slot of the
 * selected bone; the outline and grid follow. */
static void
test_rig_attachments (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	g_autofree gchar          *png = write_png (f, "art/hand.png", 32, 16, 200, 40, 40);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *text = NULL;
	LrgSkeleton2D             *skel;
	LrgSkinnedSprite          *sp;
	gint                       slot;

	/* no slot selected, bone "fore" selected: a new slot "hand" on fore */
	lrg_editor_skeleton2d_editor_set_selected_bone (ed, 1);
	g_assert_true (lrg_editor_skeleton2d_editor_drop_image (ed, "art/hand.png", 110.0f, 0.0f, &error));
	g_assert_no_error (error);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	slot = lrg_skeleton2d_find_slot (skel, "hand");
	g_assert_cmpint (slot, ==, 1);
	g_assert_cmpint (lrg_skeleton2d_get_slot_bone (skel, slot), ==, 1);
	g_assert_cmpstr (lrg_skeleton2d_get_slot_setup_attachment (skel, slot), ==, "hand");
	sp = lrg_skeleton2d_get_attachment (skel, slot, "hand");
	g_assert_nonnull (sp);
	g_assert_cmpuint (lrg_skinned_sprite_get_vertex_count (sp), ==, 4);
	g_assert_cmpstr (lrg_skinned_sprite_get_image (sp), ==, "../art/hand.png");
	{
		gfloat x, y;

		lrg_skinned_sprite_get_vertex (sp, 0, &x, &y, NULL, NULL);
		g_assert_cmpfloat_with_epsilon (x, 94.0f, 0.01f);
		g_assert_cmpfloat_with_epsilon (y, -8.0f, 0.01f);
	}
	g_assert_cmpstr (lrg_editor_skeleton2d_editor_get_selected_attachment (ed), ==, "hand");
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Attach image");
	text = doc_text (ed);
	g_assert_nonnull (strstr (text, "../art/hand.png"));
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), ==, 0);

	/* a grid attachment through the API: 3x1 cells, 8 vertices, 6 triangles */
	g_assert_true (lrg_editor_skeleton2d_editor_add_attachment (ed, 0, "strip", NULL, 0.0f, -6.0f, 90.0f, 12.0f, 3, 1,
	                                                            &error));
	g_assert_no_error (error);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "strip");
	g_assert_cmpuint (lrg_skinned_sprite_get_vertex_count (sp), ==, 8);
	g_assert_cmpuint (lrg_skinned_sprite_get_triangle_count (sp), ==, 6);
	/* arm keeps showing "arm" */
	g_assert_cmpstr (lrg_skeleton2d_get_slot_setup_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0), ==,
	                 "arm");
	g_assert_true (lrg_editor_skeleton2d_editor_set_slot_attachment (ed, 0, "strip", &error));
	g_assert_cmpstr (lrg_skeleton2d_get_slot_setup_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0), ==,
	                 "strip");
	g_assert_true (lrg_editor_skeleton2d_editor_remove_attachment (ed, 0, "strip", &error));
	g_assert_null (lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "strip"));
	g_assert_null (lrg_skeleton2d_get_slot_setup_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0));

	/* draw order: hand moves behind arm */
	g_assert_true (lrg_editor_skeleton2d_editor_move_slot (ed, 1, -1, &error));
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_cmpstr (lrg_skeleton2d_get_slot_name (skel, 0), ==, "hand");
	{
		const gchar *order[] = { "arm", "hand", NULL };

		g_assert_true (lrg_editor_skeleton2d_editor_set_draw_order (ed, order, &error));
		g_assert_cmpstr (lrg_skeleton2d_get_slot_name (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0), ==, "arm");
	}
}

/* Skins: a variant skin holds its own attachment; switching shows it and
 * falls back to default for the rest. */
static void
test_rig_skins (EditorAppFixture *f,
                gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	g_autoptr(GError)          error = NULL;
	LrgSkeleton2D             *skel;

	g_assert_true (lrg_editor_skeleton2d_editor_add_skin (ed, "gold", &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_skeleton2d_editor_get_skin (ed), ==, "gold");
	g_assert_true (lrg_editor_skeleton2d_editor_add_attachment (ed, 0, "arm", NULL, 0.0f, -10.0f, 100.0f, 20.0f, 1, 1,
	                                                            &error));
	g_assert_no_error (error);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_nonnull (lrg_skeleton2d_get_skin_attachment (skel, "gold", 0, "arm"));
	g_assert_true (lrg_editor_skeleton2d_editor_set_skin (ed, NULL));
	g_assert_cmpstr (lrg_editor_skeleton2d_editor_get_skin (ed), ==, "default");
	g_assert_true (lrg_editor_skeleton2d_editor_set_skin (ed, "gold"));
	g_assert_false (lrg_editor_skeleton2d_editor_set_skin (ed, "missing"));
	g_assert_false (lrg_editor_skeleton2d_editor_remove_skin (ed, "default", &error));
	g_clear_error (&error);
	g_assert_true (lrg_editor_skeleton2d_editor_remove_skin (ed, "gold", &error));
	g_assert_cmpuint (lrg_skeleton2d_get_skin_count (lrg_editor_skeleton2d_editor_get_skeleton (ed)), ==, 1);
	g_assert_cmpstr (lrg_editor_skeleton2d_editor_get_skin (ed), ==, "default");
}

/* ==========================================================================
 * Mesh and weights
 * ========================================================================== */

/* The mesh tool: a click adds a vertex (UV fitted, mesh triangulated),
 * dragging a vertex moves it with its UV, right click removes it. */
static void
test_rig_mesh_tool (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	g_autoptr(GError)          error = NULL;
	LrgSkinnedSprite          *sp;
	gfloat                     x, y, u, v, cx, cy;

	g_assert_true (lrg_editor_skeleton2d_editor_select_attachment (ed, 0, "arm"));
	lrg_editor_skeleton2d_editor_set_tool (ed, LRG_EDITOR_SKELETON2D_TOOL_MESH);
	efx_frame (f);

	/* add a vertex in the middle of the strip */
	setup_click (f, ed, 50.0f, 0.0f);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	g_assert_cmpuint (lrg_skinned_sprite_get_vertex_count (sp), ==, 5);
	g_assert_cmpuint (lrg_skinned_sprite_get_triangle_count (sp), ==, 4);
	lrg_skinned_sprite_get_vertex (sp, 4, &x, &y, &u, &v);
	g_assert_cmpfloat_with_epsilon (x, 50.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (u, 0.5f, 0.01f);
	g_assert_cmpfloat_with_epsilon (v, 0.5f, 0.05f);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Add vertex");

	/* drag it up a little: u follows the position */
	setup_drag (f, ed, 50.0f, 0.0f, 60.0f, -4.0f);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	lrg_skinned_sprite_get_vertex (sp, 4, &x, &y, &u, &v);
	g_assert_cmpfloat_with_epsilon (x, 60.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (y, -4.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (u, 0.6f, 0.01f);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Move vertex");

	/* right click removes it */
	canvas_at (ed, 60.0f, -4.0f, &cx, &cy);
	efx_move (f, cx, cy);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_RIGHT, TRUE);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_RIGHT, FALSE);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	g_assert_cmpuint (lrg_skinned_sprite_get_vertex_count (sp), ==, 4);
	g_assert_cmpuint (lrg_skinned_sprite_get_triangle_count (sp), ==, 2);

	/* a point outside the image: no triangle has its centre off the image */
	g_assert_cmpint (lrg_editor_skeleton2d_editor_add_vertex (ed, 50.0f, -40.0f, &error), ==, 4);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	g_assert_cmpuint (lrg_skinned_sprite_get_triangle_count (sp), >=, 1);
	{
		guint i;

		for (i = 0; i < lrg_skinned_sprite_get_triangle_count (sp); i++)
		{
			guint  a, b, c;
			gfloat va, vb, vc;

			lrg_skinned_sprite_get_triangle (sp, i, &a, &b, &c);
			lrg_skinned_sprite_get_vertex (sp, a, NULL, NULL, NULL, &va);
			lrg_skinned_sprite_get_vertex (sp, b, NULL, NULL, NULL, &vb);
			lrg_skinned_sprite_get_vertex (sp, c, NULL, NULL, NULL, &vc);
			g_assert_cmpfloat ((va + vb + vc) / 3.0f, >=, -0.02f);
		}
	}
	g_assert_true (lrg_editor_skeleton2d_editor_triangulate (ed, &error));
	g_assert_true (lrg_editor_skeleton2d_editor_remove_vertex (ed, 4, &error));
	g_assert_no_error (error);
}

/* Automatic weights bind the strip to both bones; the brush on the canvas
 * pushes the selected bone's weight up, Shift erases; each stroke is one
 * undo step. */
static void
test_rig_weights (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	g_autoptr(GError)          error = NULL;
	LrgSkinnedSprite          *sp;
	gint                       b[4];
	gfloat                     w[4], before, after;
	guint                      n, k;

	g_assert_true (lrg_editor_skeleton2d_editor_select_attachment (ed, 0, "arm"));
	g_assert_true (lrg_editor_skeleton2d_editor_auto_weights (ed, NULL, 0, 4.0f, 2, 1, &error));
	g_assert_no_error (error);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	g_assert_true (lrg_skinned_sprite_has_weights (sp));
	/* vertex 1 (x = 100) is mostly on fore */
	n = lrg_skinned_sprite_get_weights (sp, 1, b, w);
	g_assert_cmpuint (n, >=, 1);
	g_assert_cmpint (b[0], ==, 1);

	/* paint "upper" at the fore end with the brush */
	lrg_editor_skeleton2d_editor_set_selected_bone (ed, 0);
	lrg_editor_skeleton2d_editor_set_tool (ed, LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS);
	efx_frame (f);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	n = lrg_skinned_sprite_get_weights (sp, 1, b, w);
	before = 0.0f;
	for (k = 0; k < n; k++)
		if (b[k] == 0)
			before = w[k];
	g_assert_nonnull (lrg_property_grid_get_row (LRG_PROPERTY_GRID (efx_find (f, "skel-grid")), "brush-radius"));
	setup_drag (f, ed, 98.0f, -6.0f, 100.0f, -8.0f);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	n = lrg_skinned_sprite_get_weights (sp, 1, b, w);
	after = 0.0f;
	for (k = 0; k < n; k++)
		if (b[k] == 0)
			after = w[k];
	g_assert_cmpfloat (after, >, before + 0.05f);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Paint weights");

	/* erase through the API: back down */
	g_assert_cmpint (lrg_editor_skeleton2d_editor_paint_weights (ed, 0, 100.0f, -8.0f, 10.0f, -1.0f, &error), >, 0);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	n = lrg_skinned_sprite_get_weights (sp, 1, b, w);
	for (k = 0; k < n; k++)
		g_assert_true (b[k] != 0 || w[k] < 0.01f);

	/* undo the erase and the stroke */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frame (f);
	sp = lrg_skeleton2d_get_attachment (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, "arm");
	n = lrg_skinned_sprite_get_weights (sp, 1, b, w);
	after = 0.0f;
	for (k = 0; k < n; k++)
		if (b[k] == 0)
			after = w[k];
	g_assert_cmpfloat_with_epsilon (after, before, 1e-3f);
}

/* ==========================================================================
 * Renaming and removing
 * ========================================================================== */

/* Renames reach every reference (parents, slots, weights, timelines,
 * draw-order keys); removing a bone takes its descendants, their slots,
 * attachments and timelines, and the file still loads. */
static void
test_rig_rename_remove (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *text = NULL;
	LrgSkeleton2D             *skel;

	g_assert_true (lrg_editor_skeleton2d_editor_select_attachment (ed, 0, "arm"));
	g_assert_true (lrg_editor_skeleton2d_editor_auto_weights (ed, NULL, 0, 4.0f, 2, 0, &error));
	g_assert_cmpint (lrg_editor_skeleton2d_editor_add_bone (ed, "hand", 1, 50.0f, 0.0f, 0.0f, 20.0f, &error), ==, 2);
	g_assert_cmpint (lrg_editor_skeleton2d_editor_add_slot (ed, "glove", 2, NULL, &error), ==, 1);
	g_assert_no_error (error);

	g_assert_true (lrg_editor_skeleton2d_editor_rename_bone (ed, 1, "forearm", &error));
	g_assert_no_error (error);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_cmpint (lrg_skeleton2d_find_bone (skel, "fore"), ==, -1);
	g_assert_cmpint (lrg_skeleton2d_get_bone_parent (skel, 2), ==, 1);
	g_assert_nonnull (lrg_skeleton2d_get_clip (skel, "wave"));
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (lrg_skeleton2d_get_clip (skel, "wave"), "forearm",
	                                                     LRG_SKELETON2D_CHANNEL_ROTATE), ==, 2);
	text = doc_text (ed);
	g_assert_null (strstr (text, "fore\""));
	g_assert_null (strstr (text, " fore\n"));
	g_assert_false (lrg_editor_skeleton2d_editor_rename_bone (ed, 1, "upper", &error));
	g_assert_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS);
	g_clear_error (&error);

	g_assert_true (lrg_editor_skeleton2d_editor_rename_slot (ed, 0, "sleeve", &error));
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_nonnull (lrg_skeleton2d_get_skin_attachment (skel, "default", 0, "arm"));
	g_assert_cmpstr (lrg_skeleton2d_get_slot_name (skel, 0), ==, "sleeve");

	/* removing forearm takes hand and the glove slot, and the wave timeline */
	g_assert_true (lrg_editor_skeleton2d_editor_remove_bone (ed, 1, &error));
	g_assert_no_error (error);
	skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (skel), ==, 1);
	g_assert_cmpuint (lrg_skeleton2d_get_slot_count (skel), ==, 1);
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (lrg_skeleton2d_get_clip (skel, "wave"), "forearm",
	                                                     LRG_SKELETON2D_CHANNEL_ROTATE), ==, 0);
	{
		LrgSkinnedSprite *sp = lrg_skeleton2d_get_attachment (skel, 0, "arm");
		gint              b[4];
		gfloat            w[4];
		guint             i;

		for (i = 0; i < lrg_skinned_sprite_get_vertex_count (sp); i++)
		{
			guint n = lrg_skinned_sprite_get_weights (sp, i, b, w);

			g_assert_true (n == 0 || b[0] == 0);
		}
	}
	g_assert_false (lrg_editor_skeleton2d_editor_remove_bone (ed, 0, &error));
	g_clear_error (&error);
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Remove bone");
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frame (f);
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (lrg_editor_skeleton2d_editor_get_skeleton (ed)), ==, 3);

	/* Delete in the canvas removes the selected bone */
	lrg_editor_skeleton2d_editor_set_selected_slot (ed, -1);
	lrg_editor_skeleton2d_editor_set_selected_bone (ed, 2);
	lrg_widget_set_focusable (efx_find (f, "skel-canvas"), TRUE);
	lrg_canvas_set_focused_widget (lrg_editor_app_get_canvas (f->app), efx_find (f, "skel-canvas"));
	efx_key (f, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (lrg_editor_skeleton2d_editor_get_skeleton (ed)), ==, 2);
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

/* Animation mode: the clip poses the rig at the playhead, a bone drag on
 * the canvas keys it there, K keys the pose, the dope sheet lists the
 * timelines, moving a key keeps its Bézier curve, the curve view rewrites
 * a timeline, play advances and loops. */
static void
test_rig_animate (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed = open_arm (f);
	g_autoptr(GError)          error = NULL;
	LrgSkeleton2DClip         *clip;
	LrgTimelineView           *dope;
	g_autofree gchar          *text = NULL;
	gfloat                     t, v[4];
	LrgSkeleton2DCurve         curve;
	gfloat                     x0, y0;

	lrg_editor_skeleton2d_editor_set_mode (ed, LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_skeleton2d_editor_get_clip (ed), ==, "wave");
	g_assert_true (lrg_widget_get_visible (efx_find (f, "skel-anim")));
	dope = LRG_TIMELINE_VIEW (efx_find (f, "skel-dope"));
	g_assert_cmpuint (lrg_timeline_view_get_n_keyframes (dope, NULL), ==, 2);

	/* drag upper's body at t = 0.5: keys a rotation on upper */
	lrg_editor_skeleton2d_editor_set_time (ed, 0.5f);
	efx_frame (f);
	setup_drag (f, ed, 25.0f, 0.0f, 0.0f, 25.0f);
	clip = lrg_skeleton2d_get_clip (lrg_editor_skeleton2d_editor_get_skeleton (ed), "wave");
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (clip, "upper", LRG_SKELETON2D_CHANNEL_ROTATE), ==, 1);
	lrg_skeleton2d_clip_get_key (clip, "upper", LRG_SKELETON2D_CHANNEL_ROTATE, 0, &t, v, &curve);
	g_assert_cmpfloat_with_epsilon (t, 0.5f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (v[0], 90.0f, 1.0f);
	/* the setup pose is untouched */
	{
		gfloat r;

		lrg_skeleton2d_get_bone_setup (lrg_editor_skeleton2d_editor_get_skeleton (ed), 0, NULL, NULL, &r, NULL, NULL);
		g_assert_cmpfloat_with_epsilon (r, 0.0f, 1e-4f);
	}
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Key rotate");
	g_assert_cmpuint (lrg_timeline_view_get_n_keyframes (dope, NULL), ==, 3);

	/* K keys the whole pose of the selected bone */
	lrg_editor_skeleton2d_editor_set_selected_bone (ed, 1);
	g_assert_true (lrg_editor_skeleton2d_editor_key_pose (ed, 1, &error));
	clip = lrg_skeleton2d_get_clip (lrg_editor_skeleton2d_editor_get_skeleton (ed), "wave");
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (clip, "fore", LRG_SKELETON2D_CHANNEL_TRANSLATE), ==, 1);
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (clip, "fore", LRG_SKELETON2D_CHANNEL_ROTATE), ==, 3);

	/* moving the first fore rotate key keeps its Bézier control points */
	g_assert_true (lrg_editor_skeleton2d_editor_move_key (ed, "fore", LRG_SKELETON2D_CHANNEL_ROTATE, 0, 0.25f, &error));
	g_assert_no_error (error);
	text = doc_text (ed);
	g_assert_nonnull (strstr (text, "time: 0.25"));
	g_assert_nonnull (strstr (text, "0.42"));
	clip = lrg_skeleton2d_get_clip (lrg_editor_skeleton2d_editor_get_skeleton (ed), "wave");
	lrg_skeleton2d_clip_get_key (clip, "fore", LRG_SKELETON2D_CHANNEL_ROTATE, 0, &t, v, &curve);
	g_assert_cmpfloat_with_epsilon (t, 0.25f, 1e-4f);
	g_assert_cmpint (curve, ==, LRG_SKELETON2D_CURVE_BEZIER);

	/* the IPC-style key API: a stepped colour key on the slot */
	{
		const gfloat c[4] = { 1.0f, 0.5f, 0.5f, 1.0f };

		g_assert_true (lrg_editor_skeleton2d_editor_set_key (ed, "arm", LRG_SKELETON2D_CHANNEL_COLOR, 0.75f, c, 4, NULL,
		                                                     LRG_SKELETON2D_CURVE_STEPPED, NULL, &error));
		clip = lrg_skeleton2d_get_clip (lrg_editor_skeleton2d_editor_get_skeleton (ed), "wave");
		g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (clip, "arm", LRG_SKELETON2D_CHANNEL_COLOR), ==, 1);
		g_assert_true (lrg_editor_skeleton2d_editor_remove_key (ed, "arm", LRG_SKELETON2D_CHANNEL_COLOR, 0, &error));
	}

	/* play: time advances with the frames and wraps (loop on) */
	lrg_editor_skeleton2d_editor_set_time (ed, 0.9f);
	lrg_editor_skeleton2d_editor_set_playing (ed, TRUE);
	efx_frames (f, 12);
	g_assert_true (lrg_editor_skeleton2d_editor_get_playing (ed));
	g_assert_cmpfloat (lrg_editor_skeleton2d_editor_get_time (ed), <, 0.5f);
	lrg_editor_skeleton2d_editor_set_loop (ed, FALSE);
	lrg_editor_skeleton2d_editor_set_time (ed, 0.95f);
	efx_frames (f, 6);
	g_assert_false (lrg_editor_skeleton2d_editor_get_playing (ed));
	g_assert_cmpfloat_with_epsilon (lrg_editor_skeleton2d_editor_get_time (ed), 1.0f, 1e-4f);

	/* the pose at t = 1 shows fore turned 45 degrees from setup */
	lrg_editor_skeleton2d_editor_setup_to_canvas (ed, 0.0f, 0.0f, &x0, &y0);
	efx_frame (f);
	{
		LrgSkeleton2D *skel = lrg_editor_skeleton2d_editor_get_skeleton (ed);
		gfloat         r = lrg_skeleton2d_get_bone_world_rotation (skel, 1) -
		                   lrg_skeleton2d_get_bone_world_rotation (skel, 0);

		g_assert_cmpfloat_with_epsilon (r, 45.0f, 0.5f);
	}

	/* Tab goes back to setup */
	lrg_canvas_set_focused_widget (lrg_editor_app_get_canvas (f->app), efx_find (f, "skel-canvas"));
	efx_key (f, GRL_KEY_TAB, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpint (lrg_editor_skeleton2d_editor_get_mode (ed), ==, LRG_EDITOR_SKELETON2D_MODE_SETUP);
	g_assert_false (lrg_widget_get_visible (efx_find (f, "skel-anim")));
}

/* The curve view shows the selected timeline; an edit through it is one
 * undo step that keeps untouched keys' curves. */
static void
test_rig_curves (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorSkeleton2DEditor  *ed = open_arm (f);
	LrgTimelineView            *dope;
	LrgCurveEdit               *curves;
	LrgKeyframeCurve           *kc;
	LrgSkeleton2DClip          *clip;
	gfloat                      t, v[4];
	LrgSkeleton2DCurve          curve;
	guint                       id;

	lrg_editor_skeleton2d_editor_set_mode (ed, LRG_EDITOR_SKELETON2D_MODE_ANIMATE);
	efx_frames (f, 2);
	dope = LRG_TIMELINE_VIEW (efx_find (f, "skel-dope"));
	curves = LRG_CURVE_EDIT (efx_find (f, "skel-curves"));
	/* select the first fore rotate key in the dope sheet */
	{
		LrgTimelineViewTrack *group = lrg_timeline_view_find_track (dope, "fore");
		LrgTimelineViewTrack *track = NULL;
		guint                 i;

		g_assert_nonnull (group);
		for (i = 0; i < lrg_timeline_view_get_n_tracks (dope); i++)
			if (lrg_timeline_view_track_get_parent (lrg_timeline_view_get_track (dope, i)) == group)
				track = lrg_timeline_view_get_track (dope, i);
		g_assert_nonnull (track);
		id = lrg_timeline_view_get_keyframe (dope, track, 1);
		lrg_timeline_view_select_keyframe (dope, id, FALSE);
	}
	kc = lrg_curve_edit_get_curve (curves);
	g_assert_cmpuint (lrg_keyframe_curve_get_key_count (kc), ==, 2);

	/* raise the last key to 90 degrees, as a curve-view drag would */
	g_signal_emit_by_name (curves, "edit-begin");
	lrg_keyframe_curve_add_key (kc, 1.0f, 90.0f, LRG_EASING_LINEAR);
	g_signal_emit_by_name (curves, "edit-end", TRUE);
	efx_frame (f);
	clip = lrg_skeleton2d_get_clip (lrg_editor_skeleton2d_editor_get_skeleton (ed), "wave");
	lrg_skeleton2d_clip_get_key (clip, "fore", LRG_SKELETON2D_CHANNEL_ROTATE, 1, &t, v, &curve);
	g_assert_cmpfloat_with_epsilon (v[0], 90.0f, 1e-3f);
	lrg_skeleton2d_clip_get_key (clip, "fore", LRG_SKELETON2D_CHANNEL_ROTATE, 0, &t, v, &curve);
	g_assert_cmpint (curve, ==, LRG_SKELETON2D_CURVE_BEZIER);
	{
		g_autofree gchar *text = doc_text (ed);

		g_assert_nonnull (strstr (text, "0.42"));
	}
	g_assert_cmpstr (lrg_editor_document_get_undo_label (doc_of (ed)), ==, "Edit curve");
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static JsonObject *
result_object (JsonNode *n)
{
	return json_node_get_object (n);
}

/* Every skel2d.* method, building a rig from nothing. */
static void
test_rig_ipc (EditorAppFixture *f,
              gconstpointer     data)
{
	g_autofree gchar *png = NULL;
	g_autofree gchar *reply = NULL;
	JsonObject       *o;

	write_project_file (f, "readme.txt", "x");
	png = write_png (f, "art/body.png", 40, 60, 40, 120, 220);

	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'skel2d.create','params':{'path':'rigs/hero.rskel2d','name':'hero'}}");

		g_assert_true (g_str_has_suffix (json_object_get_string_member (result_object (r), "path"), "rigs/hero.rskel2d"));
	}
	reply = efx_ipc (f, "{'id':2,'method':'skel2d.create','params':{'path':'rigs/hero.rskel2d'}}");
	g_assert_nonnull (strstr (reply, "error"));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'skel2d.open','params':{'path':'rigs/hero.rskel2d'}}");

		g_assert_true (g_str_has_prefix (json_object_get_string_member (result_object (r), "panel"), "data:"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':4,'method':'skel2d.add_bone','params':{'path':'rigs/hero.rskel2d','name':'spine','parent':'root','y':-20,'rotation':-90,'length':40}}");

		g_assert_cmpstr (json_object_get_string_member (result_object (r), "bone"), ==, "spine");
		g_assert_cmpint (json_object_get_int_member (result_object (r), "index"), ==, 1);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':5,'method':'skel2d.set_bone','params':{'path':'rigs/hero.rskel2d','bone':'spine','length':45,'scale_x':1.5}}");

		g_assert_cmpfloat (json_object_get_double_member (result_object (r), "length"), ==, 45.0);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':6,'method':'skel2d.add_slot','params':{'path':'rigs/hero.rskel2d','name':'body','bone':'spine'}}");

		g_assert_cmpint (json_object_get_int_member (result_object (r), "index"), ==, 0);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':7,'method':'skel2d.attach','params':{'path':'rigs/hero.rskel2d','slot':'body','image':'art/body.png','x':-20,'y':-60,'columns':1,'rows':3}}");

		o = result_object (r);
		g_assert_cmpstr (json_object_get_string_member (o, "name"), ==, "body");
		g_assert_cmpint (json_object_get_int_member (o, "vertices"), ==, 8);
	}
	efx_ipc_result (f, "{'id':8,'method':'skel2d.set_slot','params':{'path':'rigs/hero.rskel2d','slot':'body','attachment':'body','color':[1,0.9,0.9]}}");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':9,'method':'skel2d.mesh','params':{'path':'rigs/hero.rskel2d','slot':'body','name':'body','add':[[0,-30]],'triangulate':true}}");

		g_assert_cmpint (json_object_get_int_member (result_object (r), "vertices"), ==, 9);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':10,'method':'skel2d.auto_weights','params':{'path':'rigs/hero.rskel2d','slot':'body','name':'body','bones':['root','spine']}}");

		g_assert_true (json_object_get_boolean_member (result_object (r), "weighted"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':11,'method':'skel2d.paint_weights','params':{'path':'rigs/hero.rskel2d','slot':'body','name':'body','bone':'spine','x':0,'y':-60,'radius':30,'strength':0.8}}");

		g_assert_cmpint (json_object_get_int_member (result_object (r), "changed"), >, 0);
	}
	efx_ipc_result (f, "{'id':12,'method':'skel2d.skin','params':{'path':'rigs/hero.rskel2d','name':'armor','op':'add'}}");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':13,'method':'skel2d.skin','params':{'path':'rigs/hero.rskel2d','name':'default'}}");

		g_assert_cmpstr (json_object_get_string_member (result_object (r), "active"), ==, "default");
	}
	efx_ipc_result (f, "{'id':14,'method':'skel2d.clip','params':{'path':'rigs/hero.rskel2d','name':'idle','op':'add','duration':2,'loop':true}}");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':15,'method':'skel2d.key','params':{'path':'rigs/hero.rskel2d','target':'spine','channel':'rotate','time':1,'values':[10],'curve':[0.42,0,0.58,1]}}");
		JsonArray          *keys = json_object_get_array_member (result_object (r), "keys");

		g_assert_cmpuint (json_array_get_length (keys), ==, 1);
		g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (keys, 0), "curve"), ==, "bezier");
	}
	efx_ipc_result (f, "{'id':16,'method':'skel2d.key','params':{'path':'rigs/hero.rskel2d','target':'body','channel':'attachment','time':0.5,'attachment':null}}");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':17,'method':'skel2d.play','params':{'path':'rigs/hero.rskel2d','clip':'idle','time':0.5,'loop':true}}");

		g_assert_cmpfloat (json_object_get_double_member (result_object (r), "time"), ==, 0.5);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':18,'method':'skel2d.mode','params':{'path':'rigs/hero.rskel2d','mode':'setup','tool':'mesh','slot':'body'}}");

		g_assert_cmpstr (json_object_get_string_member (result_object (r), "tool"), ==, "mesh");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':19,'method':'skel2d.pick','params':{'path':'rigs/hero.rskel2d','x':0,'y':-40}}");

		g_assert_cmpstr (json_object_get_string_member (result_object (r), "bone"), ==, "spine");
	}
	efx_ipc_result (f, "{'id':20,'method':'skel2d.add_slot','params':{'path':'rigs/hero.rskel2d','name':'cape','bone':'root'}}");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':21,'method':'skel2d.draw_order','params':{'path':'rigs/hero.rskel2d','order':['cape','body']}}");
		JsonArray          *order = json_object_get_array_member (result_object (r), "order");

		g_assert_cmpstr (json_array_get_string_element (order, 0), ==, "cape");
	}
	efx_ipc_result (f, "{'id':22,'method':'skel2d.remove_slot','params':{'path':'rigs/hero.rskel2d','slot':'cape'}}");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':23,'method':'skel2d.get','params':{'path':'rigs/hero.rskel2d'}}");

		o = result_object (r);
		g_assert_cmpstr (json_object_get_string_member (o, "name"), ==, "hero");
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "bones")), ==, 2);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "slots")), ==, 1);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "clips")), ==, 1);
		g_assert_true (json_object_get_boolean_member (o, "dirty"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':24,'method':'skel2d.detach','params':{'path':'rigs/hero.rskel2d','slot':'body','name':'body'}}");

		g_assert_cmpstr (json_object_get_string_member (result_object (r), "removed"), ==, "body");
	}
	efx_ipc_result (f, "{'id':25,'method':'skel2d.remove_bone','params':{'path':'rigs/hero.rskel2d','bone':'spine'}}");
	efx_ipc_result (f, "{'id':26,'method':'skel2d.save','params':{'path':'rigs/hero.rskel2d'}}");
	{
		g_autofree gchar       *abs = g_build_filename (f->project, "rigs/hero.rskel2d", NULL);
		g_autoptr(GError)       error = NULL;
		g_autoptr(LrgSkeleton2D) saved = lrg_skeleton2d_new_from_file (abs, &error);

		g_assert_no_error (error);
		g_assert_cmpuint (lrg_skeleton2d_get_bone_count (saved), ==, 1);
		g_assert_cmpuint (lrg_skeleton2d_get_slot_count (saved), ==, 0);
	}
	/* errors: unknown bone, bad channel */
	g_free (reply);
	reply = efx_ipc (f, "{'id':27,'method':'skel2d.set_bone','params':{'path':'rigs/hero.rskel2d','bone':'nope','x':1}}");
	g_assert_nonnull (strstr (reply, "error"));
	g_free (reply);
	reply = efx_ipc (f, "{'id':28,'method':'skel2d.key','params':{'path':'rigs/hero.rskel2d','target':'root','channel':'spin','time':0}}");
	g_assert_nonnull (strstr (reply, "error"));
}

/* ==========================================================================
 * GPU (only under the isolated Xvfb)
 * ========================================================================== */

static gboolean gpu_ready = FALSE;

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-rig");
	return IsWindowReady ();
}

/* The textured attachment shows in its image's colour at the centre of
 * the render; posing the clip moves it. */
static void
test_gpu_render (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorSkeleton2DEditor *ed;
	g_autofree gchar          *png = NULL;
	g_autoptr(GError)          error = NULL;
	g_autoptr(GrlImage)        img = NULL;
	GrlColor                  *c;

	if (!gpu_ready)
	{
		g_test_skip ("needs a display (run under xrun.sh)");
		return;
	}
	ed = open_arm (f);
	png = write_png (f, "art/red.png", 16, 16, 230, 30, 30);
	lrg_editor_skeleton2d_editor_set_selected_bone (ed, 0);
	g_assert_true (lrg_editor_skeleton2d_editor_add_attachment (ed, 0, "red", "art/red.png", 0.0f, -8.0f, 100.0f, 16.0f,
	                                                            4, 1, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_skeleton2d_editor_set_slot_attachment (ed, 0, "red", &error));
	efx_frames (f, 2);
	img = lrg_editor_skeleton2d_editor_render_image (ed, 200, 100);
	g_assert_nonnull (img);
	c = grl_image_get_pixel (img, 100, 57);  /* below the bone overlay line */
	g_assert_cmpuint (c->r, >, 150);
	g_assert_cmpuint (c->g, <, 90);
	g_boxed_free (GRL_TYPE_COLOR, c);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_log_set_fatal_handler (loader_warnings_ok, NULL);
	gpu_ready = gpu_init ();

#define ADD(name, fn) g_test_add ("/editor-app-rig/" name, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("open", test_rig_open);
	ADD ("parse-error", test_rig_parse_error);
	ADD ("bone-tool", test_rig_bone_tool);
	ADD ("setup-drags", test_rig_setup_drags);
	ADD ("comments", test_rig_comments);
	ADD ("attachments", test_rig_attachments);
	ADD ("skins", test_rig_skins);
	ADD ("mesh-tool", test_rig_mesh_tool);
	ADD ("weights", test_rig_weights);
	ADD ("rename-remove", test_rig_rename_remove);
	ADD ("animate", test_rig_animate);
	ADD ("curves", test_rig_curves);
	ADD ("ipc", test_rig_ipc);
	ADD ("gpu/render", test_gpu_render);
#undef ADD

	{
		gint rc = g_test_run ();

		if (gpu_ready)
			CloseWindow ();
		return rc;
	}
}
