/* test-editor-app-anim.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 animation tools of lrgeditor, in a headless LrgEditorApp
 * (tests/editor-app-fixture.h):
 *
 *  - the four document formats (.ranim, .rbonemap, .rseq, .rskel2d):
 *    detection by extension and content, new-document skeletons that the
 *    engine loaders accept, the data editor registry;
 *  - the `.ranim` animation graph editor: cards and wires from the
 *    document, canvas gestures (wire, move, delete) as undo steps, states,
 *    transitions, conditions and parameters through the API, layers,
 *    nested machines with breadcrumbs, blend trees, validation, the live
 *    preview (parameters drive transitions, the active state is
 *    highlighted on its card), every anim.* IPC method;
 *  - the `.rbonemap` bone map editor: models, rule and explicit pairs,
 *    unmapped bones, auto map, live retargeting, every bonemap.* method;
 *  - the assets panel: New Animation Graph / Bone Map / Sequence / 2D
 *    Skeleton, the Animations dialog of glTF models (clips, scrub, morph
 *    sliders) and the clip.* methods;
 *  - the inspector's animator section and the viewport's bone overlay
 *    and IK target gizmos;
 *  - /gpu/: the previews render (only under the isolated Xvfb of
 *    xrun.sh); with LRG_TEST_SHOT_DIR set the frames are saved.
 *
 * Every model is written by the test itself (tests/lrg-test-glb.h): a
 * skinned tube with a chain of bones and named clips, and a one-triangle
 * mesh with two morph targets. Nothing is downloaded.
 */

#include "editor-app-fixture.h"
#include "lrg-test-glb.h"
#include <raylib.h>

/* ==========================================================================
 * Fixtures and helpers
 * ========================================================================== */

static const gchar * const TUBE_CLIPS[] = { "Idle", "Walk" };

/* Opens a project (once) and returns the absolute path of @rel. */
static gchar *
project_path (EditorAppFixture *f,
              const gchar      *rel)
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
	return path;
}

static gchar *
write_project_file (EditorAppFixture *f,
                    const gchar      *rel,
                    const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	gchar            *path = project_path (f, rel);

	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
	return path;
}

/* A skinned tube character (@bones joints, clips Idle and Walk). */
static gchar *
write_tube (EditorAppFixture *f,
            const gchar      *rel,
            guint             bones)
{
	gchar *path = project_path (f, rel);

	test_glb_write_skinned_tube (path, 8, 8, bones, TUBE_CLIPS, G_N_ELEMENTS (TUBE_CLIPS));
	return path;
}

/* One triangle with two morph targets ("Smile", "Blink"), no skin. */
static gchar *
write_face (EditorAppFixture *f,
            const gchar      *rel)
{
	static const gfloat  pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
	static const gfloat  d0[9] = { 0, 0, 1, 0, 0, 0, 0, 0, 0 };
	static const gfloat  d1[9] = { 0, 0, 0, 1, 0, 0, 0, 2, 0 };
	static const guint16 idx[4] = { 0, 1, 2, 0 };
	static const gchar   json[] =
		"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
		"\"nodes\":[{\"name\":\"Face\",\"mesh\":0}],"
		"\"meshes\":[{\"name\":\"FaceMesh\",\"extras\":{\"targetNames\":[\"Smile\",\"Blink\"]},"
		"\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":3,"
		"\"targets\":[{\"POSITION\":1},{\"POSITION\":2}]}]}],"
		"\"buffers\":[{\"byteLength\":116}],"
		"\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
		"{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
		"{\"buffer\":0,\"byteOffset\":72,\"byteLength\":36},"
		"{\"buffer\":0,\"byteOffset\":108,\"byteLength\":6}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
		"\"min\":[0,0,0],\"max\":[1,1,0]},"
		"{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[0,0,1]},"
		"{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,2,0]},"
		"{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";
	guint8 bin[116];
	gchar *path = project_path (f, rel);

	memset (bin, 0, sizeof bin);
	memcpy (bin, pos, 36);
	memcpy (bin + 36, d0, 36);
	memcpy (bin + 72, d1, 36);
	memcpy (bin + 108, idx, 8);
	test_glb_write (path, json, bin, sizeof bin);
	return path;
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

static gchar *
doc_text (LrgEditorDataEditor *editor)
{
	return lrg_editor_document_to_data (lrg_editor_data_editor_get_document (editor), NULL);
}

static LrgEditorAnimGraphEditor *
open_graph (EditorAppFixture *f,
            const gchar      *path)
{
	g_autoptr(GError)         error = NULL;
	LrgEditorAnimGraphEditor *ed = lrg_editor_anim_graph_editor_open (f->app, path, &error);

	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 2);
	return ed;
}

/* A hero.ranim made from the tube with its two clips. */
static LrgEditorAnimGraphEditor *
hero_graph (EditorAppFixture *f)
{
	g_autofree gchar *glb = write_tube (f, "chars/hero.glb", 4);
	g_autofree gchar *ranim = project_path (f, "chars/hero.ranim");
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_anim_graph_editor_create (f->app, ranim, glb, &error));
	g_assert_no_error (error);
	return open_graph (f, ranim);
}

static gboolean
canvas_has_link (LrgNodeGraph *canvas,
                 const gchar  *from,
                 const gchar  *to)
{
	g_autoptr(GList) links = lrg_node_graph_get_links (canvas);
	GList           *l;

	for (l = links; l != NULL; l = l->next)
	{
		LrgGraphLink *link = l->data;

		if (g_strcmp0 (lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link))), from) == 0 &&
		    g_strcmp0 (lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link))), to) == 0)
			return TRUE;
	}
	return FALSE;
}

/* ==========================================================================
 * Formats
 * ========================================================================== */

static void
test_formats (EditorAppFixture *f,
              gconstpointer     data)
{
	static const struct { const gchar *file; LrgEditorDocumentFormat format; const gchar *nick; } cases[] = {
		{ "hero.ranim", LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH, "anim-graph" },
		{ "map.rbonemap", LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP, "bone-map" },
		{ "intro.rseq", LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE, "sequence" },
		{ "arm.rskel2d", LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D, "skeleton2d" }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(LrgEditorDocument) doc = lrg_editor_document_new (cases[i].format);
		g_autofree gchar            *text = lrg_editor_document_to_data (doc, NULL);
		g_autoptr(GError)            error = NULL;

		/* the extension decides, before any sniffing */
		g_assert_cmpint (lrg_editor_document_detect_format (cases[i].file, "actions: {}\n", -1), ==, cases[i].format);
		g_assert_cmpstr (lrg_editor_document_format_to_string (cases[i].format), ==, cases[i].nick);
		g_assert_cmpint (lrg_editor_document_format_from_string (cases[i].nick), ==, cases[i].format);

		/* the new-document skeleton loads in the engine */
		switch (cases[i].format)
		{
		case LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH:
		{
			g_autoptr(LrgAnimGraph) g = lrg_anim_graph_new_from_data (text, -1, &error);

			g_assert_no_error (error);
			g_assert_cmpuint (lrg_anim_graph_get_layer_count (g), ==, 1);
			break;
		}
		case LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP:
		{
			g_autoptr(LrgBoneMap) m = lrg_bone_map_new_from_data (text, -1, &error);

			g_assert_no_error (error);
			g_assert_true (lrg_bone_map_get_ignore_case (m));
			break;
		}
		case LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE:
		{
			g_autoptr(LrgSequence) s = lrg_sequence_new_from_data (text, -1, NULL, &error);

			g_assert_no_error (error);
			g_assert_nonnull (s);
			break;
		}
		case LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D:
		{
			g_autoptr(LrgSkeleton2D) s = lrg_skeleton2d_new_from_data (text, -1, &error);

			g_assert_no_error (error);
			g_assert_nonnull (s);
			break;
		}
		default:
			g_assert_not_reached ();
		}
	}

	/* content sniffing for the two formats with a marker key */
	g_assert_cmpint (lrg_editor_document_detect_format ("x.yaml", "rskel2d: 1\nbones: []\n", -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D);
	g_assert_cmpint (lrg_editor_document_detect_format ("x.yaml", "type: sequence\nversion: 1\n", -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE);
	g_assert_cmpint (lrg_editor_document_detect_format ("x.yaml", "type: vbox\nchildren: []\n", -1), ==,
	                 LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT);

	/* the data editor registry knows the editors */
	g_assert_true (lrg_editor_data_editor_lookup_type (LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH) ==
	               LRG_TYPE_EDITOR_ANIM_GRAPH_EDITOR);
	g_assert_true (lrg_editor_data_editor_lookup_type (LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP) ==
	               LRG_TYPE_EDITOR_BONE_MAP_EDITOR);
	g_assert_true (lrg_editor_data_editor_lookup_type (LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE) == G_TYPE_INVALID);
	{
		g_autofree gchar *p = write_project_file (f, "anims/x.ranim", "version: 1\nlayers: [{name: base, states: "
		                                                              "[{name: a}]}]\n");

		g_assert_true (lrg_editor_data_editor_can_open (p));
	}
}

/* ==========================================================================
 * Animation graph editor
 * ========================================================================== */

/* A model's clips become slots and states; the canvas shows them. */
static void
test_graph_open (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorAnimGraphEditor *ed = hero_graph (f);
	LrgNodeGraph             *canvas = lrg_editor_anim_graph_editor_get_canvas (ed);
	LrgAnimGraph             *graph = lrg_editor_anim_graph_editor_get_graph (ed);
	g_auto(GStrv)             missing = NULL;
	g_auto(GStrv)             crumbs = NULL;

	g_assert_nonnull (graph);
	g_assert_null (lrg_editor_anim_graph_editor_get_load_error (ed));
	g_assert_cmpuint (lrg_anim_graph_get_clip_count (graph), ==, 2);
	missing = lrg_anim_graph_get_missing_clips (graph);
	g_assert_true (missing == NULL || missing[0] == NULL);
	g_assert_nonnull (lrg_node_graph_get_node (canvas, "Idle"));
	g_assert_nonnull (lrg_node_graph_get_node (canvas, "Walk"));
	g_assert_nonnull (lrg_node_graph_get_node (canvas, LRG_EDITOR_ANIM_GRAPH_ANY_STATE));
	g_assert_cmpstr (lrg_graph_node_get_icon (lrg_node_graph_get_node (canvas, "Idle")), ==, "star");
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_machine_path (ed), ==, "/layers/0");
	crumbs = lrg_editor_anim_graph_editor_get_breadcrumbs (ed);
	g_assert_cmpstr (crumbs[0], ==, "base");
	g_assert_null (crumbs[1]);

	/* the preview loaded the model and plays the default state */
	g_assert_true (g_str_has_suffix (lrg_editor_anim_graph_editor_get_preview_model (ed), "hero.glb"));
	g_assert_nonnull (lrg_editor_anim_graph_editor_get_preview_skeleton (ed));
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_active_state (ed, "base"), ==, "Idle");
	g_assert_nonnull (efx_find (f, "anim-preview"));
	g_assert_nonnull (efx_find (f, "anim-grid"));
}

/* Parameters drive transitions in the preview; the active card glows. */
static void
test_graph_live (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorAnimGraphEditor *ed = hero_graph (f);
	LrgNodeGraph             *canvas = lrg_editor_anim_graph_editor_get_canvas (ed);
	g_autoptr(GError)         error = NULL;
	g_autoptr(GVariant)       v = NULL;

	g_assert_true (lrg_editor_anim_graph_editor_add_parameter (ed, "speed", "float", &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_anim_graph_editor_add_transition (ed, "Idle", "Walk", 0.1f, &error));
	g_assert_true (lrg_editor_anim_graph_editor_add_condition (ed, "Idle", "Walk", "speed", ">", "0.5", &error));
	g_assert_true (lrg_editor_anim_graph_editor_add_transition (ed, "Walk", "Idle", 0.1f, &error));
	g_assert_true (lrg_editor_anim_graph_editor_add_condition (ed, "Walk", "Idle", "speed", "<=", "0.5", &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_true (canvas_has_link (canvas, "Idle", "Walk"));
	g_assert_true (canvas_has_link (canvas, "Walk", "Idle"));
	g_assert_null (lrg_editor_anim_graph_editor_get_load_error (ed));

	/* nothing happens until the parameter moves */
	lrg_editor_anim_graph_editor_advance (ed, 0.3f);
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_active_state (ed, NULL), ==, "Idle");
	g_assert_true (lrg_editor_anim_graph_editor_set_parameter_value (ed, "speed", g_variant_new_double (1.0)));
	g_assert_false (lrg_editor_anim_graph_editor_set_parameter_value (ed, "nope", g_variant_new_double (1.0)));
	lrg_editor_anim_graph_editor_advance (ed, 0.5f);
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_active_state (ed, NULL), ==, "Walk");
	v = lrg_editor_anim_graph_editor_get_parameter_value (ed, "speed");
	g_assert_cmpfloat_with_epsilon (g_variant_get_double (v), 1.0, 1e-5);

	/* the active state is highlighted on the next frame */
	efx_frame (f);
	g_assert_true (lrg_graph_node_get_accent (lrg_node_graph_get_node (canvas, "Walk"), NULL));
	g_assert_false (lrg_graph_node_get_accent (lrg_node_graph_get_node (canvas, "Idle"), NULL));

	/* live values survive an edit of the document (the graph is reparsed) */
	g_assert_true (lrg_editor_anim_graph_editor_set_state_position (ed, "Walk", 300.0f, 40.0f));
	g_clear_pointer (&v, g_variant_unref);
	v = lrg_editor_anim_graph_editor_get_parameter_value (ed, "speed");
	g_assert_cmpfloat_with_epsilon (g_variant_get_double (v), 1.0, 1e-5);

	/* restart: back to the default state; scrub pauses */
	lrg_editor_anim_graph_editor_restart (ed);
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_active_state (ed, NULL), ==, "Idle");
	lrg_editor_anim_graph_editor_scrub (ed, 0.5f);
	g_assert_false (lrg_editor_anim_graph_editor_get_playing (ed));
	lrg_editor_anim_graph_editor_set_playing (ed, TRUE);
	g_assert_true (lrg_editor_anim_graph_editor_get_playing (ed));
}

/* Canvas gestures write the document and undo as single steps. */
static void
test_graph_canvas_undo (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorAnimGraphEditor *ed = hero_graph (f);
	LrgEditorDataEditor      *base = LRG_EDITOR_DATA_EDITOR (ed);
	LrgNodeGraph             *canvas = lrg_editor_anim_graph_editor_get_canvas (ed);
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *tp = NULL;
	g_autofree gchar         *text = NULL;

	/* a wire dragged on the canvas is a transition */
	g_assert_nonnull (lrg_node_graph_connect_by_name (canvas, "Walk", "out", "Idle", "in", &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	tp = lrg_editor_anim_graph_editor_get_transition_path (ed, "Walk", "Idle");
	g_assert_cmpstr (tp, ==, "/layers/0/transitions/0");
	g_assert_true (lrg_editor_data_editor_undo (base));
	efx_frames (f, 2);
	g_clear_pointer (&tp, g_free);
	tp = lrg_editor_anim_graph_editor_get_transition_path (ed, "Walk", "Idle");
	g_assert_null (tp);
	g_assert_false (canvas_has_link (canvas, "Walk", "Idle"));
	g_assert_true (lrg_editor_data_editor_redo (base));
	efx_frames (f, 2);
	g_assert_true (canvas_has_link (canvas, "Walk", "Idle"));

	/* an any-state wire lands under any_state: */
	g_assert_nonnull (lrg_node_graph_connect_by_name (canvas, LRG_EDITOR_ANIM_GRAPH_ANY_STATE, "out", "Walk", "in", NULL));
	efx_frames (f, 2);
	g_clear_pointer (&tp, g_free);
	tp = lrg_editor_anim_graph_editor_get_transition_path (ed, LRG_EDITOR_ANIM_GRAPH_ANY_STATE, "Walk");
	g_assert_cmpstr (tp, ==, "/layers/0/any_state/0");

	/* moving a card writes editor_pos */
	lrg_graph_node_set_position (lrg_node_graph_get_node (canvas, "Walk"), 512.0f, 96.0f);
	g_signal_emit_by_name (canvas, "node-moved", lrg_node_graph_get_node (canvas, "Walk"));
	efx_frames (f, 2);
	text = doc_text (base);
	g_assert_nonnull (strstr (text, "editor_pos:"));
	g_assert_nonnull (strstr (text, "512"));

	/* Delete on a card removes the state and its wires: one undo step */
	lrg_node_graph_unselect_all (canvas);
	lrg_node_graph_select_node (canvas, lrg_node_graph_get_node (canvas, "Walk"), FALSE);
	g_assert_cmpuint (lrg_node_graph_delete_selection (canvas), >, 0);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_anim_graph_editor_get_state_path (ed, "Walk"));
	g_clear_pointer (&tp, g_free);
	tp = lrg_editor_anim_graph_editor_get_transition_path (ed, "Walk", "Idle");
	g_assert_null (tp);
	g_assert_true (lrg_editor_data_editor_undo (base));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_node_graph_get_node (canvas, "Walk"));
	g_assert_true (canvas_has_link (canvas, "Walk", "Idle"));
	g_assert_null (lrg_editor_anim_graph_editor_get_load_error (ed));
}

/* Layers, nested machines with breadcrumbs, rename, default, blend trees. */
static void
test_graph_structure (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorAnimGraphEditor *ed = hero_graph (f);
	LrgNodeGraph             *canvas = lrg_editor_anim_graph_editor_get_canvas (ed);
	g_autoptr(GError)         error = NULL;
	g_auto(GStrv)             crumbs = NULL;
	g_autofree gchar         *tp = NULL;

	/* a nested machine */
	g_assert_true (lrg_editor_anim_graph_editor_add_state (ed, "air", "machine", NULL, 0.0f, 200.0f, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_anim_graph_editor_enter_state (ed, "air"));
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_machine_path (ed), ==, "/layers/0/states/2/machine");
	crumbs = lrg_editor_anim_graph_editor_get_breadcrumbs (ed);
	g_assert_cmpstr (crumbs[0], ==, "base");
	g_assert_cmpstr (crumbs[1], ==, "air");
	g_assert_nonnull (lrg_node_graph_get_node (canvas, "entry"));
	g_assert_true (lrg_editor_anim_graph_editor_add_state (ed, "fall", "clip", "Walk", 250.0f, 0.0f, &error));
	g_assert_true (lrg_editor_anim_graph_editor_add_transition (ed, "entry", "fall", 0.1f, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_anim_graph_editor_go_up (ed));
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_machine_path (ed), ==, "/layers/0");
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_selected_state (ed), ==, "air");
	g_assert_false (lrg_editor_anim_graph_editor_go_up (ed));
	g_assert_null (lrg_editor_anim_graph_editor_get_load_error (ed));

	/* rename follows transitions and default */
	g_assert_true (lrg_editor_anim_graph_editor_add_transition (ed, "Idle", "air", 0.2f, &error));
	g_assert_true (lrg_editor_anim_graph_editor_rename_state (ed, "Idle", "rest", &error));
	g_assert_no_error (error);
	tp = lrg_editor_anim_graph_editor_get_transition_path (ed, "rest", "air");
	g_assert_nonnull (tp);
	g_assert_cmpstr (lrg_graph_node_get_icon (lrg_node_graph_get_node (canvas, "rest")), ==, "star");
	g_assert_false (lrg_editor_anim_graph_editor_rename_state (ed, "rest", "Walk", NULL));
	g_assert_true (lrg_editor_anim_graph_editor_set_default_state (ed, "Walk"));
	efx_frame (f);
	g_assert_cmpstr (lrg_graph_node_get_icon (lrg_node_graph_get_node (canvas, "Walk")), ==, "star");

	/* a blend tree with a 1D blend space */
	g_assert_true (lrg_editor_anim_graph_editor_add_state (ed, "move", "blend_tree", NULL, 0, 400, &error));
	g_assert_no_error (error);
	lrg_editor_anim_graph_editor_select_state (ed, "move");
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "anim-blend-space")));
	g_assert_null (lrg_editor_anim_graph_editor_get_load_error (ed));

	/* a second layer, switched to by chip */
	g_assert_true (lrg_editor_anim_graph_editor_add_layer (ed, "upper", &error));
	g_assert_false (lrg_editor_anim_graph_editor_add_layer (ed, "upper", NULL));
	g_assert_true (lrg_editor_anim_graph_editor_set_layer (ed, 1));
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_machine_path (ed), ==, "/layers/1");
	efx_frames (f, 2);
	g_assert_nonnull (efx_find (f, "anim-layer-1"));
	g_assert_nonnull (lrg_node_graph_get_node (canvas, "idle"));
	g_assert_true (lrg_editor_anim_graph_editor_remove_layer (ed, "upper"));
	g_assert_cmpstr (lrg_editor_anim_graph_editor_get_machine_path (ed), ==, "/layers/0");
	g_assert_false (lrg_editor_anim_graph_editor_remove_layer (ed, "base"));

	/* parameters: duplicates and bad types refused */
	g_assert_false (lrg_editor_anim_graph_editor_add_parameter (ed, "blend", "float", NULL));
	g_assert_false (lrg_editor_anim_graph_editor_add_parameter (ed, "x", "vector", NULL));
	g_assert_true (lrg_editor_anim_graph_editor_remove_parameter (ed, "blend"));
	/* the blend tree's parameter is gone: the loader reports it */
	g_assert_nonnull (lrg_editor_anim_graph_editor_get_load_error (ed));
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), >, 0);
}

/* Every anim.* method. */
static void
test_graph_ipc (EditorAppFixture *f,
                gconstpointer     data)
{
	g_autofree gchar    *glb = write_tube (f, "chars/hero.glb", 4);
	g_autoptr(JsonNode)  r = NULL;
	JsonObject          *o;
	JsonArray           *a;
	g_autofree gchar    *saved = NULL;
	g_autofree gchar    *reply = NULL;

	r = efx_ipc_result (f, "{'id':1,'method':'anim.create','params':{'path':'chars/hero.ranim','model':'chars/hero.glb'}}");
	g_assert_true (g_str_has_suffix (json_object_get_string_member (json_node_get_object (r), "path"), "hero.ranim"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':2,'method':'anim.open','params':{'path':'chars/hero.ranim'}}");
	g_assert_true (g_str_has_prefix (json_object_get_string_member (json_node_get_object (r), "panel"), "data:"));
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 2);

	r = efx_ipc_result (f, "{'id':3,'method':'anim.add_parameter','params':{'path':'chars/hero.ranim','name':'speed',"
	                       "'type':'float','default':0}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':4,'method':'anim.add_parameter','params':{'path':'chars/hero.ranim','name':'jump',"
	                       "'type':'trigger'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':5,'method':'anim.add_state','params':{'path':'chars/hero.ranim','name':'locomotion',"
	                       "'kind':'blend_tree','x':0,'y':300}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':6,'method':'anim.set_blend_tree','params':{'path':'chars/hero.ranim','state':'locomotion',"
	                       "'type':'1d','parameter':'speed','children':[{'clip':'Idle','threshold':0},"
	                       "{'clip':'Walk','threshold':1.5}]}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'anim.add_transition','params':{'path':'chars/hero.ranim','from':'Idle',"
	                       "'to':'locomotion','duration':0.2,'conditions':[{'parameter':'speed','op':'>','value':0.1}]}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':8,'method':'anim.add_transition','params':{'path':'chars/hero.ranim','from':'*',"
	                       "'to':'Walk','conditions':[{'parameter':'jump'}]}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':9,'method':'anim.set_transition','params':{'path':'chars/hero.ranim','from':'Idle',"
	                       "'to':'locomotion','exit_time':0.5,'interruption':'destination'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':10,'method':'anim.set_state','params':{'path':'chars/hero.ranim','name':'Walk',"
	                       "'speed':1.5,'mirror':true,'x':600,'y':0}}");
	g_clear_pointer (&r, json_node_unref);

	/* the graph loads and the get reflects it */
	r = efx_ipc_result (f, "{'id':11,'method':'anim.get','params':{'path':'chars/hero.ranim'}}");
	o = json_node_get_object (r);
	g_assert_false (json_object_has_member (o, "load_error"));
	a = json_object_get_array_member (o, "states");
	g_assert_cmpuint (json_array_get_length (a), ==, 3);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, 2), "kind"), ==, "blend_tree");
	a = json_object_get_array_member (o, "transitions");
	g_assert_cmpuint (json_array_get_length (a), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, 0), "from"), ==, "*");
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_array_get_object_element (a, 1), "exit_time"),
	                                0.5, 1e-6);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_array_get_object_element (a, 1),
	                                                                       "conditions")), ==, 1);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "parameters")), ==, 2);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "clips")), ==, 2);
	g_clear_pointer (&r, json_node_unref);

	/* live preview: speed up, the base layer blends into locomotion */
	r = efx_ipc_result (f, "{'id':12,'method':'anim.set_parameter','params':{'path':'chars/hero.ranim','name':'speed',"
	                       "'value':1.0}}");
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_node_get_object (r), "value"), 1.0, 1e-6);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':13,'method':'anim.preview','params':{'path':'chars/hero.ranim','advance':1.0}}");
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (json_node_get_object (r), "states"),
	                                                "base"), ==, "locomotion");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':14,'method':'anim.set_parameter','params':{'path':'chars/hero.ranim','name':'jump',"
	                       "'value':true}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':15,'method':'anim.preview','params':{'path':'chars/hero.ranim','advance':0.5,"
	                       "'play':false}}");
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (json_node_get_object (r), "states"),
	                                                "base"), ==, "Walk");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	g_clear_pointer (&r, json_node_unref);

	/* layers and nested machines */
	r = efx_ipc_result (f, "{'id':16,'method':'anim.add_layer','params':{'path':'chars/hero.ranim','name':'upper'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':17,'method':'anim.enter','params':{'path':'chars/hero.ranim','layer':'upper'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "machine"), ==, "/layers/1");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':18,'method':'anim.remove_layer','params':{'path':'chars/hero.ranim','name':'upper'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "removed"));
	g_clear_pointer (&r, json_node_unref);

	/* validation, errors, removal, save */
	r = efx_ipc_result (f, "{'id':19,'method':'anim.validate','params':{'path':'chars/hero.ranim'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "ok"));
	g_clear_pointer (&r, json_node_unref);
	reply = efx_ipc (f, "{'id':20,'method':'anim.add_state','params':{'path':'chars/hero.ranim','name':'Walk'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':21,'method':'anim.set_parameter','params':{'path':'chars/hero.ranim','name':'nope',"
	                    "'value':1}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	r = efx_ipc_result (f, "{'id':22,'method':'anim.remove_transition','params':{'path':'chars/hero.ranim','from':'*',"
	                       "'to':'Walk'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "removed"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':23,'method':'anim.remove_parameter','params':{'path':'chars/hero.ranim','name':'jump'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':24,'method':'anim.save','params':{'path':'chars/hero.ranim'}}");
	g_clear_pointer (&r, json_node_unref);
	{
		g_autofree gchar         *path = g_build_filename (f->project, "chars/hero.ranim", NULL);
		g_autoptr(GError)         error = NULL;
		g_autoptr(LrgAnimGraph)   g = NULL;

		g_assert_true (g_file_get_contents (path, &saved, NULL, NULL));
		g = lrg_anim_graph_new_from_data (saved, -1, &error);
		g_assert_no_error (error);
		g_assert_nonnull (strstr (saved, "editor_preview:"));
		g_assert_nonnull (strstr (saved, "locomotion"));
		g_assert_cmpuint (lrg_anim_graph_get_parameter_count (g), ==, 1);
	}
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':25,'method':'anim.remove_state','params':{'path':'chars/hero.ranim','name':'locomotion'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "removed"));
}

/* ==========================================================================
 * Bone map editor
 * ========================================================================== */

static void
test_bone_map (EditorAppFixture *f,
               gconstpointer     data)
{
	g_autofree gchar        *src = write_tube (f, "anims/mocap.glb", 4);
	g_autofree gchar        *tgt = write_tube (f, "chars/hero.glb", 3);
	g_autofree gchar        *path = project_path (f, "chars/hero.rbonemap");
	g_autoptr(GError)        error = NULL;
	LrgEditorBoneMapEditor  *ed;
	g_auto(GStrv)            un_t = NULL;
	g_auto(GStrv)            un_s = NULL;
	gboolean                 exp = TRUE;
	guint                    added = 99;
	g_autofree gchar        *text = NULL;

	g_assert_true (lrg_editor_bone_map_editor_create (f->app, path, NULL, NULL, FALSE, &added, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (added, ==, 0);
	g_assert_false (lrg_editor_bone_map_editor_create (f->app, path, NULL, NULL, FALSE, NULL, NULL));
	ed = lrg_editor_bone_map_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_bone_map_editor_get_retargeter (ed));

	/* pick the skeletons: the rules pair Bone0..Bone2 by name */
	g_assert_true (lrg_editor_bone_map_editor_set_models (ed, "anims/mocap.glb", "chars/hero.glb", &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_nonnull (lrg_editor_bone_map_editor_get_retargeter (ed));
	g_assert_cmpstr (lrg_editor_bone_map_editor_get_driver (ed, "Bone1", &exp), ==, "Bone1");
	g_assert_false (exp);
	un_s = lrg_editor_bone_map_editor_get_unmapped_sources (ed);
	g_assert_true (g_strv_contains ((const gchar * const *) un_s, "Bone3"));
	g_assert_nonnull (efx_find (f, "bonemap-row-Bone1"));

	/* an explicit pair wins; a second pair onto the same target replaces it */
	g_assert_true (lrg_editor_bone_map_editor_set_pair (ed, "Bone3", "Bone2", &error));
	g_assert_cmpstr (lrg_editor_bone_map_editor_get_driver (ed, "Bone2", &exp), ==, "Bone3");
	g_assert_true (exp);
	g_assert_true (lrg_editor_bone_map_editor_set_pair (ed, "Bone2", "Bone2", &error));
	text = doc_text (LRG_EDITOR_DATA_EDITOR (ed));
	g_assert_null (strstr (text, "Bone3: Bone2"));
	g_assert_nonnull (strstr (text, "Bone2: Bone2"));
	g_assert_true (lrg_editor_bone_map_editor_set_pair (ed, "Bone2", NULL, &error));
	un_t = lrg_editor_bone_map_editor_get_unmapped_targets (ed);
	g_assert_cmpuint (g_strv_length (un_t), ==, 0);

	/* auto map turns rule matches into pairs, one undo step */
	g_assert_cmpuint (lrg_editor_bone_map_editor_auto_fill (ed), >=, 3);
	g_assert_cmpuint (lrg_bone_map_get_size (lrg_editor_bone_map_editor_get_map (ed)), >=, 3);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_cmpuint (lrg_bone_map_get_size (lrg_editor_bone_map_editor_get_map (ed)), ==, 0);

	/* the live preview: the target follows the source pose */
	g_assert_true (lrg_editor_bone_map_editor_set_preview_clip (ed, "Walk"));
	g_assert_false (lrg_editor_bone_map_editor_set_preview_clip (ed, "Run"));
	lrg_editor_bone_map_editor_set_preview_time (ed, 0.5f);
	g_assert_false (lrg_editor_bone_map_editor_get_playing (ed));
	{
		LrgSkeleton       *s = lrg_editor_bone_map_editor_get_source_skeleton (ed);
		LrgSkeleton       *t = lrg_editor_bone_map_editor_get_target_skeleton (ed);
		const LrgBonePose *ws = lrg_bone_get_world_pose (lrg_skeleton_get_bone_by_name (s, "Bone1"));
		const LrgBonePose *wt = lrg_bone_get_world_pose (lrg_skeleton_get_bone_by_name (t, "Bone1"));
		gfloat             dot = ws->rotation_x * wt->rotation_x + ws->rotation_y * wt->rotation_y +
		                         ws->rotation_z * wt->rotation_z + ws->rotation_w * wt->rotation_w;

		/* same world rotation change from the same (identity) rest */
		g_assert_cmpfloat (fabsf (dot), >, 0.999f);
		g_assert_cmpfloat (fabsf (ws->rotation_w), <, 0.9999f);
	}

	/* rules through the grid's source: prefixes as one field */
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), ==, 0);
	g_assert_true (lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (ed), &error));
	g_assert_no_error (error);
	{
		g_autoptr(LrgBoneMap) m = lrg_bone_map_new_from_file (path, &error);

		g_assert_no_error (error);
		g_assert_nonnull (m);
	}
}

static void
test_bone_map_ipc (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autofree gchar   *src = write_tube (f, "anims/mocap.glb", 4);
	g_autofree gchar   *tgt = write_tube (f, "chars/hero.glb", 4);
	g_autoptr(JsonNode) r = NULL;
	JsonObject         *o;
	g_autofree gchar   *reply = NULL;

	r = efx_ipc_result (f, "{'id':1,'method':'bonemap.create','params':{'path':'chars/hero.rbonemap',"
	                       "'source':'anims/mocap.glb','target':'chars/hero.glb','auto_fill':true}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "added"), ==, 4);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':2,'method':'bonemap.get','params':{'path':'chars/hero.rbonemap'}}");
	o = json_node_get_object (r);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "pairs")), ==, 4);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "unmapped_target")), ==, 0);
	g_assert_true (g_str_has_suffix (json_object_get_string_member (o, "target_model"), "hero.glb"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':3,'method':'bonemap.set','params':{'path':'chars/hero.rbonemap','source':'Bone3',"
	                       "'target':null}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':4,'method':'bonemap.set_rules','params':{'path':'chars/hero.rbonemap',"
	                       "'ignore_case':false,'strip_prefixes':['mixamorig:','DEF-']}}");
	o = json_object_get_object_member (json_node_get_object (r), "rules");
	g_assert_false (json_object_get_boolean_member (o, "ignore_case"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "strip_prefixes")), ==, 2);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':5,'method':'bonemap.auto_fill','params':{'path':'chars/hero.rbonemap'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "added"), ==, 1);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':6,'method':'bonemap.set_models','params':{'path':'chars/hero.rbonemap','clip':'Walk'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "clip"), ==, "Walk");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'bonemap.preview','params':{'path':'chars/hero.rbonemap','time':0.25}}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':8,'method':'bonemap.open','params':{'path':'chars/hero.rbonemap'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':9,'method':'bonemap.save','params':{'path':'chars/hero.rbonemap'}}");
	g_clear_pointer (&r, json_node_unref);
	reply = efx_ipc (f, "{'id':10,'method':'bonemap.set_models','params':{'path':'chars/hero.rbonemap',"
	                    "'source':'missing.glb'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	{
		g_autofree gchar *path = g_build_filename (f->project, "chars/hero.rbonemap", NULL);
		g_autofree gchar *text = NULL;

		g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
		g_assert_nonnull (strstr (text, "mixamorig:"));
		g_assert_nonnull (strstr (text, "clip: Walk"));
	}
}

/* ==========================================================================
 * Assets panel: New entries, clip preview, clip.*
 * ========================================================================== */

static LrgEditorAssetsPanel *
assets_panel (EditorAppFixture *f)
{
	LrgWidget *w = efx_find (f, "assets-panel");

	g_assert_nonnull (w);
	g_assert_true (LRG_IS_EDITOR_ASSETS_PANEL (w));
	return LRG_EDITOR_ASSETS_PANEL (w);
}

static void
test_assets (EditorAppFixture *f,
             gconstpointer     data)
{
	g_autofree gchar     *glb = write_tube (f, "hero.glb", 4);
	g_autofree gchar     *face = write_face (f, "face.glb");
	LrgEditorAssetsPanel *panel;
	g_autoptr(JsonNode)   r = NULL;
	JsonObject           *o;
	JsonArray            *a;
	static const struct { const gchar *action; const gchar *file; } news[] = {
		{ "assets.new-anim-graph", "new_anim_graph.ranim" },
		{ "assets.new-bone-map", "new_bone_map.rbonemap" },
		{ "assets.new-sequence", "new_sequence.rseq" },
		{ "assets.new-skeleton2d", "new_skeleton.rskel2d" }
	};
	guint i;

	efx_frames (f, 3);
	panel = assets_panel (f);
	lrg_editor_assets_panel_set_directory (panel, f->project);
	efx_frames (f, 2);

	/* New entries write files the editors open */
	for (i = 0; i < G_N_ELEMENTS (news); i++)
	{
		g_autofree gchar *p = g_build_filename (f->project, news[i].file, NULL);

		lrg_widget_activate_action (LRG_WIDGET (panel), news[i].action, NULL);
		efx_frames (f, 2);
		g_assert_true (g_file_test (p, G_FILE_TEST_IS_REGULAR));
		if (i < 2 || i == 3)
		{
			g_autoptr(GError) error = NULL;

			g_assert_nonnull (lrg_editor_data_editor_open (f->app, p, &error));
			g_assert_no_error (error);
		}
	}

	/* clip.list / clip.info without UI */
	r = efx_ipc_result (f, "{'id':1,'method':'clip.list','params':{'model':'hero.glb'}}");
	o = json_node_get_object (r);
	a = json_object_get_array_member (o, "clips");
	g_assert_cmpuint (json_array_get_length (a), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, 1), "name"), ==, "Walk");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "bones")), >=, 4);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':2,'method':'clip.info','params':{'model':'hero.glb','clip':'Walk'}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "duration"), 1.0, 1e-4);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "tracks")), >=, 4);
	g_clear_pointer (&r, json_node_unref);

	/* the Animations dialog */
	r = efx_ipc_result (f, "{'id':3,'method':'clip.preview','params':{'model':'hero.glb','clip':'Walk','time':0.25}}");
	o = json_node_get_object (r);
	g_assert_cmpstr (json_object_get_string_member (o, "clip"), ==, "Walk");
	g_assert_false (json_object_get_boolean_member (o, "playing"));
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 2);
	{
		LrgWidget *top = lrg_canvas_get_top_popup (lrg_editor_app_get_canvas (f->app));

		g_assert_true (LRG_IS_DIALOG (top));
		g_assert_cmpstr (lrg_widget_get_name (top), ==, "assets-clip-dialog");
		g_assert_nonnull (lrg_widget_find (top, "assets-clip-list"));
	}
	g_assert_true (g_str_has_suffix (lrg_editor_assets_panel_get_clip_preview_model (panel), "hero.glb"));

	/* morph sliders of a blend-shape face */
	r = efx_ipc_result (f, "{'id':4,'method':'clip.list','params':{'model':'face.glb'}}");
	a = json_object_get_array_member (json_node_get_object (r), "morph_targets");
	g_assert_cmpuint (json_array_get_length (a), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, 1), "name"), ==, "Blink");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':5,'method':'clip.set_morph','params':{'model':'face.glb','target':'Smile',"
	                       "'weight':0.75}}");
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_node_get_object (r), "weight"), 0.75, 1e-6);
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 2);
	g_assert_nonnull (lrg_widget_find (lrg_canvas_get_top_popup (lrg_editor_app_get_canvas (f->app)),
	                                   "assets-clip-morph-1"));
	{
		g_autofree gchar *reply = efx_ipc (f, "{'id':6,'method':'clip.info','params':{'model':'hero.glb','clip':'Run'}}");

		g_assert_nonnull (strstr (reply, "\"error\""));
	}
	g_assert_true (lrg_editor_assets_panel_preview_clips (panel, "hero.glb", "Idle"));
	g_assert_false (lrg_editor_assets_panel_preview_clips (panel, "nothing.glb", NULL));
}

/* ==========================================================================
 * Inspector animator section, viewport bones and IK overlay
 * ========================================================================== */

/* A character node: the tube as a mesh asset with an animator3d. */
static LrgNode *
add_character (EditorAppFixture *f,
               const gchar      *graph_abs)
{
	g_autoptr(LrgNode)          node = lrg_node_new ("Hero");
	g_autoptr(LrgNodeVisual)    visual = lrg_node_visual_new (LRG_NODE_VISUAL_MESH_ASSET);
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("animator3d");

	lrg_node_visual_set_asset (visual, "chars/hero.glb");
	lrg_node_set_visual (node, visual);
	lrg_component_desc_set_string (desc, "graph-path", graph_abs);
	lrg_node_add_component (node, desc);
	lrg_editor_add_node (f->editor, node, NULL);
	return node;
}

static void
select_one (EditorAppFixture *f,
            LrgNode          *node)
{
	g_autoptr(GPtrArray) nodes = g_ptr_array_new ();

	g_ptr_array_add (nodes, node);
	lrg_editor_select_nodes (f->editor, nodes, FALSE);
	efx_frames (f, 3);
}

static LrgEditorInspectorPanel *
inspector (EditorAppFixture *f)
{
	LrgDockPanel *panel = lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "inspector");

	g_assert_nonnull (panel);
	return LRG_EDITOR_INSPECTOR_PANEL (lrg_dock_panel_get_child (panel));
}

static LrgEditorViewportPanel *
viewport0 (EditorAppFixture *f)
{
	LrgWidget *split = efx_find (f, "viewport-split");

	if (split != NULL)
		return lrg_editor_viewport_split_get_panel (LRG_EDITOR_VIEWPORT_SPLIT (split), 0);
	{
		LrgDockPanel *dock = lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "viewport");

		return LRG_EDITOR_VIEWPORT_PANEL (lrg_dock_panel_get_child (dock));
	}
}

static void
test_inspector_animator (EditorAppFixture *f,
                         gconstpointer     data)
{
	g_autofree gchar *glb = write_tube (f, "chars/hero.glb", 4);
	g_autofree gchar *ranim = write_project_file (f, "chars/hero.ranim",
	                                              "version: 1\n"
	                                              "clips:\n  Walk: {source: hero.glb, animation: Walk}\n"
	                                              "parameters:\n  - {name: speed, type: float, default: 0.5}\n"
	                                              "  - {name: armed, type: bool}\n"
	                                              "layers:\n  - name: base\n    default: walk\n"
	                                              "    states:\n      - {name: walk, clip: Walk}\n"
	                                              "ik:\n  - {name: reach, type: ccd, bones: [Bone1, Bone2, Bone3]}\n");
	LrgNode          *hero;
	LrgNode          *other;
	g_auto(GStrv)     rows = NULL;
	guint             bones = 0, targets = 0;

	efx_frames (f, 2);
	hero = add_character (f, ranim);
	other = lrg_node_new ("Plain");
	lrg_editor_add_node (f->editor, other, NULL);
	g_object_unref (other);

	/* the section lists the file's parameters, layers and IK goals */
	select_one (f, hero);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-section-animator")));
	rows = lrg_editor_inspector_panel_get_animator_rows (inspector (f));
	g_assert_cmpuint (g_strv_length (rows), ==, 4);
	g_assert_cmpstr (rows[0], ==, "param speed float 0.5");
	g_assert_cmpstr (rows[1], ==, "param armed bool false");
	g_assert_cmpstr (rows[2], ==, "layer base 1.00 walk");
	g_assert_true (g_str_has_prefix (rows[3], "ik reach ccd Bone1>Bone2>Bone3"));
	g_clear_pointer (&rows, g_strfreev);

	/* a node without an animator hides the section */
	select_one (f, other);
	g_assert_false (lrg_widget_get_visible (efx_find (f, "inspector-section-animator")));
	rows = lrg_editor_inspector_panel_get_animator_rows (inspector (f));
	g_assert_cmpuint (g_strv_length (rows), ==, 0);
	g_clear_pointer (&rows, g_strfreev);

	/* viewport: the bind-pose skeleton of the selected character */
	{
		LrgEditorViewportPanel *vp = viewport0 (f);

		g_assert_false (lrg_editor_viewport_panel_get_show_bones (vp));
		g_assert_true (lrg_editor_viewport_panel_get_show_ik_targets (vp));
		lrg_editor_viewport_panel_set_show_bones (vp, TRUE);
		select_one (f, hero);
		efx_frames (f, 2);
		lrg_editor_viewport_panel_get_anim_overlay_counts (vp, &bones, &targets);
		g_assert_cmpuint (bones, >=, 4);
		g_assert_cmpuint (targets, ==, 0);
		lrg_editor_viewport_panel_set_show_bones (vp, FALSE);
		efx_frames (f, 2);
		lrg_editor_viewport_panel_get_anim_overlay_counts (vp, &bones, NULL);
		g_assert_cmpuint (bones, ==, 0);
	}

	/* while playing the section follows the live animator */
	if (lrg_editor_app_activate_action (f->app, "play.start"))
	{
		efx_frames (f, 4);
		select_one (f, hero);
		rows = lrg_editor_inspector_panel_get_animator_rows (inspector (f));
		g_assert_cmpuint (g_strv_length (rows), >=, 3);
		g_assert_true (g_str_has_prefix (rows[0], "param speed float"));
		g_clear_pointer (&rows, g_strfreev);
		lrg_editor_app_activate_action (f->app, "play.stop");
		efx_frames (f, 2);
	}

	/* Open Graph opens the .ranim editor */
	select_one (f, hero);
	g_signal_emit_by_name (efx_find (f, "inspector-animator-open"), "clicked");
	efx_frames (f, 2);
	g_assert_nonnull (lrg_editor_data_editor_find (f->app, ranim));
}

/* ==========================================================================
 * GPU (only under the isolated Xvfb)
 * ========================================================================== */

static gboolean gpu_ready = FALSE;

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-anim");
	return IsWindowReady ();
}

static gboolean
gpu_skip (void)
{
	if (!gpu_ready)
		g_test_skip ("needs a display (run under xrun.sh)");
	return !gpu_ready;
}

/* Counts pixels that differ from the clear colour. */
static guint
lit_pixels (GrlImage *image)
{
	const guint8 *px = grl_image_get_pixels (image, NULL);
	gint          w = grl_image_get_width (image), h = grl_image_get_height (image);
	guint         n = 0;
	gint          i;

	for (i = 0; i < w * h; i++)
		if (abs ((gint) px[4 * i] - 26) > 30 || abs ((gint) px[4 * i + 1] - 28) > 30 || abs ((gint) px[4 * i + 2] - 38) > 30)
			n++;
	return n;
}

static void
save_shot (GrlImage    *image,
           const gchar *name)
{
	const gchar *dir = g_getenv ("LRG_TEST_SHOT_DIR");

	if (dir != NULL && *dir != '\0')
	{
		g_autofree gchar *file = g_build_filename (dir, name, NULL);

		grl_image_export (image, file);
	}
}

static void
test_gpu_previews (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorAnimGraphEditor *ed;
	g_autoptr(GrlImage)       a = NULL;
	g_autoptr(GrlImage)       b = NULL;

	if (gpu_skip ())
		return;
	ed = hero_graph (f);
	lrg_editor_anim_graph_editor_set_playing (ed, FALSE);
	lrg_editor_anim_graph_editor_set_show_bones (ed, TRUE);
	g_assert_true (lrg_editor_anim_graph_editor_get_show_bones (ed));
	a = lrg_editor_anim_graph_editor_render_preview (ed, 256);
	g_assert_nonnull (a);
	g_assert_cmpuint (lit_pixels (a), >, 500);
	save_shot (a, "m34e-edanim-preview.png");
	/* another clip pose draws differently */
	lrg_editor_anim_graph_editor_set_default_state (ed, "Walk");
	lrg_editor_anim_graph_editor_restart (ed);
	lrg_editor_anim_graph_editor_advance (ed, 0.25f);
	b = lrg_editor_anim_graph_editor_render_preview (ed, 256);
	g_assert_nonnull (b);
	g_assert_cmpuint (lit_pixels (b), >, 500);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_log_set_fatal_handler (loader_warnings_ok, NULL);
	gpu_ready = gpu_init ();

#define ADD(name, fn) g_test_add ("/editor-app-anim/" name, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("formats", test_formats);
	ADD ("graph/open", test_graph_open);
	ADD ("graph/live", test_graph_live);
	ADD ("graph/canvas-undo", test_graph_canvas_undo);
	ADD ("graph/structure", test_graph_structure);
	ADD ("graph/ipc", test_graph_ipc);
	ADD ("bonemap/edit", test_bone_map);
	ADD ("bonemap/ipc", test_bone_map_ipc);
	ADD ("assets/anim-assets", test_assets);
	ADD ("inspector/animator", test_inspector_animator);
	ADD ("gpu/previews", test_gpu_previews);
#undef ADD

	{
		gint rc = g_test_run ();

		if (gpu_ready)
			CloseWindow ();
		return rc;
	}
}
