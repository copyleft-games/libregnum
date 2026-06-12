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
	g_test_add_func ("/cad/asset-classification",
	                 test_asset_classification);
#endif
	g_test_add_func ("/cad/document-changed-signal",
	                 test_document_changed_signal);

	return g_test_run ();
}
