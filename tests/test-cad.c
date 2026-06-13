/* test-cad.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for the CAD integration (CAD=1): manager caches,
 * baking into 16-bit-safe chunks, node param-bag override bridging,
 * the SET_VISUAL_PARAM editor command, and asset classification.
 */

#include <libregnum.h>
#include <math.h>
#include <glib/gstdio.h>
#include <unistd.h>

static const gchar *bracket_cad =
	"(defparam thickness 4.0 :min 2 :max 10)\n"
	"(defpart bracket\n"
	"  (difference\n"
	"    (box 40 20 thickness)\n"
	"    (translate (10 10 -1) (cylinder :r 2.5 :h (+ thickness 2)))))\n";

static gchar *
write_fixture (const gchar *contents)
{
	gchar *path = NULL;
	gint fd = g_file_open_tmp ("lrg-cad-XXXXXX.cad", &path, NULL);

	g_assert_cmpint (fd, >=, 0);
	close (fd);
	g_assert_true (g_file_set_contents (path, contents, -1, NULL));

	return path;
}

static void
test_manager_bake_and_cache (void)
{
	LrgCadManager *mgr = lrg_cad_manager_get_default ();
	GError *error = NULL;
	gchar *path = write_fixture (bracket_cad);
	LrgCadBakeResult *first;
	LrgCadBakeResult *second;
	GPtrArray *meshes;
	guint i;

	first = lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (first);

	meshes = lrg_cad_bake_result_get_meshes (first);
	g_assert_cmpuint (meshes->len, >=, 1);
	for (i = 0; i < meshes->len; i++)
	{
		CadMesh *chunk = g_ptr_array_index (meshes, i);

		g_assert_cmpuint (chunk->n_vertices, <=, 65535);
		g_assert_nonnull (chunk->tri_feature_ids);
	}
	g_assert_cmpuint (lrg_cad_bake_result_get_total_triangles (first),
	                  >, 0);
	g_assert_nonnull (lrg_cad_bake_result_get_solid (first));
	g_assert_nonnull (lrg_cad_bake_result_get_tree (first));

	/* Identical request: cache hit, same instance. */
	second = lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL, &error);
	g_assert_true (first == second);

	lrg_cad_manager_invalidate (mgr, path);
	g_unlink (path);
	g_free (path);
}

static void
test_bake_with_overrides (void)
{
	LrgCadManager *mgr = lrg_cad_manager_get_default ();
	GError *error = NULL;
	gchar *path = write_fixture (bracket_cad);
	LrgCadBakeResult *thin;
	LrgCadBakeResult *thick;
	GHashTable *overrides;
	gdouble eight = 8.0;
	gdouble v_thin, v_thick;

	thin = lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL, &error);
	g_assert_no_error (error);
	v_thin = cad_solid_get_volume (lrg_cad_bake_result_get_solid (thin),
	                               &error);

	overrides = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (overrides, (gpointer) "thickness", &eight);
	thick = lrg_cad_manager_bake (mgr, path, overrides, 0.0, NULL,
	                              &error);
	g_assert_no_error (error);
	g_assert_true (thick != thin);  /* distinct cache entry */
	v_thick = cad_solid_get_volume (lrg_cad_bake_result_get_solid (thick),
	                                &error);

	g_assert_cmpfloat (v_thick, >, v_thin * 1.5);

	g_hash_table_unref (overrides);
	lrg_cad_manager_invalidate (mgr, path);
	g_unlink (path);
	g_free (path);
}

static void
test_bake_error_reported (void)
{
	LrgCadManager *mgr = lrg_cad_manager_get_default ();
	GError *error = NULL;
	gchar *path = write_fixture ("(defpart broken (box -1 1 1))\n");
	LrgCadBakeResult *result;

	result = lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL, &error);
	g_assert_null (result);
	g_assert_nonnull (error);
	g_clear_error (&error);

	lrg_cad_manager_invalidate (mgr, path);
	g_unlink (path);
	g_free (path);
}

#ifdef LRG_BUILD_EDITOR
static void
test_node_overrides_bridge (void)
{
	LrgNode *node = lrg_node_new ("part");
	LrgNodeVisual *visual = lrg_node_visual_new (LRG_NODE_VISUAL_CAD_PART);
	GHashTable *overrides;
	gdouble *value;

	lrg_node_visual_set_asset (visual, "bracket.cad");
	lrg_node_visual_set_param_double (visual, "cad:thickness", 6.0);
	lrg_node_visual_set_param_double (visual, "cad:part", 1.0);
	lrg_node_visual_set_param_double (visual, "cad:deflection", 0.05);
	lrg_node_visual_set_param_double (visual, "unrelated", 9.0);
	lrg_node_set_visual (node, visual);

	overrides = lrg_cad_manager_overrides_for_node (node);
	g_assert_nonnull (overrides);
	g_assert_cmpuint (g_hash_table_size (overrides), ==, 1);
	value = g_hash_table_lookup (overrides, "thickness");
	g_assert_nonnull (value);
	g_assert_cmpfloat_with_epsilon (*value, 6.0, 1e-12);

	g_hash_table_unref (overrides);
	g_object_unref (node);
}

static void
test_set_visual_param_command (void)
{
	LrgNode *node = lrg_node_new ("part");
	LrgNodeVisual *visual = lrg_node_visual_new (LRG_NODE_VISUAL_CAD_PART);
	LrgEditorCommand *cmd;
	LrgEditorCommand *merge_cmd;

	lrg_node_visual_set_param_double (visual, "cad:thickness", 4.0);
	lrg_node_set_visual (node, visual);

	cmd = lrg_editor_command_new_set_visual_param (node, "cad:thickness",
	                                               4.0, 6.0);
	g_assert_cmpint (lrg_editor_command_get_kind (cmd), ==,
	                 LRG_EDITOR_CMD_SET_VISUAL_PARAM);

	lrg_editor_command_apply (cmd);
	g_assert_cmpfloat_with_epsilon (
		lrg_node_visual_get_param_double (lrg_node_get_visual (node),
		                                  "cad:thickness", 0.0),
		6.0, 1e-12);

	lrg_editor_command_undo (cmd);
	g_assert_cmpfloat_with_epsilon (
		lrg_node_visual_get_param_double (lrg_node_get_visual (node),
		                                  "cad:thickness", 0.0),
		4.0, 1e-12);

	/* Slider-drag coalescing: same node + param merges. */
	merge_cmd = lrg_editor_command_new_set_visual_param (node,
	                                                     "cad:thickness",
	                                                     6.0, 7.5);
	g_assert_true (lrg_editor_command_merge (cmd, merge_cmd));
	lrg_editor_command_apply (cmd);
	g_assert_cmpfloat_with_epsilon (
		lrg_node_visual_get_param_double (lrg_node_get_visual (node),
		                                  "cad:thickness", 0.0),
		7.5, 1e-12);

	/* Different param: no merge. */
	g_object_unref (merge_cmd);
	merge_cmd = lrg_editor_command_new_set_visual_param (node, "cad:other",
	                                                     0.0, 1.0);
	g_assert_false (lrg_editor_command_merge (cmd, merge_cmd));

	g_object_unref (merge_cmd);
	g_object_unref (cmd);
	g_object_unref (node);
}

static void
test_editor_set_visual_param_undoable (void)
{
	LrgEditor *editor = lrg_editor_new ();
	LrgLevel *level = lrg_level_new ("scratch");
	LrgNode *node = lrg_node_new ("part");
	LrgNodeVisual *visual = lrg_node_visual_new (LRG_NODE_VISUAL_CAD_PART);
	LrgNodeVisual *got;

	lrg_node_visual_set_asset (visual, "bracket.cad");
	lrg_node_visual_set_param_double (visual, "cad:thickness", 4.0);
	lrg_node_set_visual (node, visual);
	lrg_editor_set_level (editor, level);
	lrg_editor_add_node (editor, node, NULL);

	/* A fresh set (e.g. a field commit) is its own undo step. */
	lrg_editor_set_visual_param (editor, node, "cad:thickness", 6.0,
	                             FALSE);
	got = lrg_node_get_visual (node);
	g_assert_cmpfloat_with_epsilon (
		lrg_node_visual_get_param_double (got, "cad:thickness", 0.0),
		6.0, 1e-12);

	/* A slider drag: the FIRST move is a fresh step (merge=FALSE),
	 * continuation moves coalesce onto it (merge=TRUE) so the whole drag
	 * is one undo step. */
	lrg_editor_set_visual_param (editor, node, "cad:thickness", 7.0,
	                             FALSE);
	lrg_editor_set_visual_param (editor, node, "cad:thickness", 8.0, TRUE);
	lrg_editor_set_visual_param (editor, node, "cad:thickness", 8.5, TRUE);
	g_assert_cmpfloat_with_epsilon (
		lrg_node_visual_get_param_double (got, "cad:thickness", 0.0),
		8.5, 1e-12);

	/* One undo reverts the whole drag back to 6.0; a second reaches 4.0. */
	g_assert_true (lrg_editor_can_undo (editor));
	lrg_editor_undo (editor);
	g_assert_cmpfloat_with_epsilon (
		lrg_node_visual_get_param_double (got, "cad:thickness", 0.0),
		6.0, 1e-12);
	lrg_editor_undo (editor);
	g_assert_cmpfloat_with_epsilon (
		lrg_node_visual_get_param_double (got, "cad:thickness", 0.0),
		4.0, 1e-12);

	g_object_unref (node);
	g_object_unref (level);
	g_object_unref (editor);
}

static void
test_asset_classification (void)
{
	g_assert_cmpint (lrg_asset_database_classify ("part.cad"), ==,
	                 LRG_ASSET_TYPE_CAD);
	g_assert_cmpint (lrg_asset_database_classify ("part.CCAD"), ==,
	                 LRG_ASSET_TYPE_CAD);
	g_assert_cmpint (lrg_asset_database_classify ("part.stl"), ==,
	                 LRG_ASSET_TYPE_MODEL);
	g_assert_cmpint (lrg_asset_database_classify ("part.step"), ==,
	                 LRG_ASSET_TYPE_MODEL);
	g_assert_cmpint (lrg_asset_database_classify ("part.3mf"), ==,
	                 LRG_ASSET_TYPE_MODEL);
	g_assert_cmpint (lrg_asset_database_classify ("level.rlevel"), ==,
	                 LRG_ASSET_TYPE_LEVEL);
}
#endif /* LRG_BUILD_EDITOR */

static const gchar *bracket_thick_cad =
	"(defparam thickness 9.0 :min 2 :max 10)\n"
	"(defpart bracket\n"
	"  (difference\n"
	"    (box 40 20 thickness)\n"
	"    (translate (10 10 -1) (cylinder :r 2.5 :h (+ thickness 2)))))\n";

static void
test_set_source_rebakes (void)
{
	LrgCadManager *mgr = lrg_cad_manager_get_default ();
	GError *error = NULL;
	gchar *path = write_fixture (bracket_cad);
	LrgCadBakeResult *before;
	LrgCadBakeResult *after;
	gdouble v_before, v_after;

	/* set_source on a not-yet-loaded path loads the document first. */
	g_assert_true (lrg_cad_manager_set_source (mgr, path, bracket_cad,
	                                           &error));
	g_assert_no_error (error);

	before = lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL, &error);
	g_assert_no_error (error);
	v_before = cad_solid_get_volume (lrg_cad_bake_result_get_solid (before),
	                                 &error);

	/* Replacing the source (an unsaved editor buffer) drops the bakes,
	 * so the next bake reflects the new text, not the on-disk file. */
	g_assert_true (lrg_cad_manager_set_source (mgr, path,
	                                           bracket_thick_cad, &error));
	g_assert_no_error (error);
	after = lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (after != before);
	v_after = cad_solid_get_volume (lrg_cad_bake_result_get_solid (after),
	                                &error);
	g_assert_cmpfloat (v_after, >, v_before * 1.5);

	/* Broken source: the bake fails but the document recovers when good
	 * source is pushed again. */
	g_assert_true (lrg_cad_manager_set_source (mgr, path, "(defpart b",
	                                           &error));
	g_assert_no_error (error);
	g_assert_null (lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL,
	                                     &error));
	g_assert_nonnull (error);
	g_clear_error (&error);

	g_assert_true (lrg_cad_manager_set_source (mgr, path, bracket_cad,
	                                           &error));
	g_assert_no_error (error);
	g_assert_nonnull (lrg_cad_manager_bake (mgr, path, NULL, 0.0, NULL,
	                                        &error));
	g_assert_no_error (error);

	lrg_cad_manager_invalidate (mgr, path);
	g_unlink (path);
	g_free (path);
}

static void
test_document_changed_signal (void)
{
	LrgCadManager *mgr = lrg_cad_manager_get_default ();
	GError *error = NULL;
	gchar *path = write_fixture (bracket_cad);
	gboolean fired = FALSE;
	gulong handler;

	void on_changed (LrgCadManager *m, const gchar *p, gpointer data)
	{
		gboolean *flag = data;

		*flag = TRUE;
	}

	g_assert_nonnull (lrg_cad_manager_load (mgr, path, &error));
	g_assert_no_error (error);

	handler = g_signal_connect (mgr, "document-changed",
	                            G_CALLBACK (on_changed), &fired);

	/* Touch the file and pump the loop until the monitor reacts. */
	g_assert_true (g_file_set_contents (path, bracket_cad, -1, NULL));
	{
		gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

		while (!fired && g_get_monotonic_time () < deadline)
			g_main_context_iteration (NULL, FALSE);
	}
	g_assert_true (fired);

	g_signal_handler_disconnect (mgr, handler);
	lrg_cad_manager_invalidate (mgr, path);
	g_unlink (path);
	g_free (path);
}

static const gchar *hinge_cad =
	"(defpart post (cylinder :r 2 :h 20))\n"
	"(defpart arm (cylinder :r 1 :h 6))\n"
	"(defassembly hinge\n"
	"  (instance \"base\" post :grounded #t)\n"
	"  (instance \"lever\" arm)\n"
	"  (joint revolute \"base\" (cyl-largest) \"lever\" (cyl-largest)\n"
	"         :value 0))\n";

static void
test_bake_assembly (void)
{
	GError *error = NULL;
	gchar *path = write_fixture (hinge_cad);
	CadDocument *doc;
	GPtrArray *bakes;
	guint i;
	gboolean saw_lever = FALSE;

	doc = cad_document_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (doc);

	bakes = lrg_cad_bake_assembly (doc, NULL, 0.0, "hinge", &error);
	g_assert_no_error (error);
	g_assert_nonnull (bakes);
	/* two instances -> two per-instance bakes */
	g_assert_cmpuint (bakes->len, ==, 2);

	for (i = 0; i < bakes->len; i++)
	{
		LrgCadInstanceBake *b = g_ptr_array_index (bakes, i);
		GPtrArray *meshes = lrg_cad_instance_bake_get_meshes (b);
		gdouble m[12];

		g_assert_cmpuint (meshes->len, >=, 1);
		lrg_cad_instance_bake_get_transform (b, m);
		if (g_strcmp0 (lrg_cad_instance_bake_get_name (b), "lever") == 0)
			saw_lever = TRUE;
	}
	g_assert_true (saw_lever);

	g_ptr_array_unref (bakes);
	g_object_unref (doc);
	g_unlink (path);
	g_free (path);
}

static void
test_bake_assembly_joint_drive (void)
{
	GError *error = NULL;
	gchar *path = write_fixture (hinge_cad);
	CadDocument *doc;
	CadAssembly *asm_;
	CadJoint *joint;
	GPtrArray *bakes;
	guint i;

	doc = cad_document_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_true (cad_document_eval (doc, NULL, NULL, &error));
	g_assert_no_error (error);

	asm_ = cad_document_get_assembly (doc, "hinge");
	g_assert_nonnull (asm_);

	/* drive the hinge and re-bake from the solved assembly */
	joint = g_ptr_array_index (cad_assembly_get_joints (asm_), 0);
	cad_joint_set_value (joint, 90.0);
	g_assert_true (cad_assembly_solve (asm_, &error));

	bakes = lrg_cad_bake_assembly_solved (asm_, 0.0, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (bakes->len, ==, 2);

	for (i = 0; i < bakes->len; i++)
	{
		LrgCadInstanceBake *b = g_ptr_array_index (bakes, i);
		gdouble m[12];

		if (g_strcmp0 (lrg_cad_instance_bake_get_name (b), "lever") != 0)
			continue;
		lrg_cad_instance_bake_get_transform (b, m);
		/* lever rotated 90 deg about Z: R[0][0] ~ 0 */
		g_assert_cmpfloat (fabs (m[0]), <, 1e-6);
	}

	g_ptr_array_unref (bakes);
	g_object_unref (doc);
	g_unlink (path);
	g_free (path);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/cad/manager-bake-and-cache",
	                 test_manager_bake_and_cache);
	g_test_add_func ("/cad/bake-with-overrides",
	                 test_bake_with_overrides);
	g_test_add_func ("/cad/bake-error-reported",
	                 test_bake_error_reported);
#ifdef LRG_BUILD_EDITOR
	g_test_add_func ("/cad/node-overrides-bridge",
	                 test_node_overrides_bridge);
	g_test_add_func ("/cad/set-visual-param-command",
	                 test_set_visual_param_command);
	g_test_add_func ("/cad/editor-set-visual-param-undoable",
	                 test_editor_set_visual_param_undoable);
	g_test_add_func ("/cad/asset-classification",
	                 test_asset_classification);
#endif
	g_test_add_func ("/cad/set-source-rebakes",
	                 test_set_source_rebakes);
	g_test_add_func ("/cad/document-changed-signal",
	                 test_document_changed_signal);
	g_test_add_func ("/cad/bake-assembly", test_bake_assembly);
	g_test_add_func ("/cad/bake-assembly-joint-drive",
	                 test_bake_assembly_joint_drive);

	return g_test_run ();
}
