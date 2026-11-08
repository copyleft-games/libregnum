/* test-lsp-client.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLspClient against tests/data/fake-lsp.py, plus the calls that must
 * stay quiet when no language server is installed.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

static gchar *
srcdir_file (const gchar *name)
{
	const gchar *root = g_getenv ("G_TEST_SRCDIR");

	if (root == NULL || root[0] == '\0')
		root = ".";
	return g_build_filename (root, name, NULL);
}

static void
pump (gint ms)
{
	gint64 end = g_get_monotonic_time () + (gint64) ms * G_TIME_SPAN_MILLISECOND;

	while (g_get_monotonic_time () < end)
	{
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2 * G_TIME_SPAN_MILLISECOND);
	}
}

static void
test_language_for_path (void)
{
	g_assert_cmpstr (lrg_lsp_language_for_path ("a.c"), ==, "c");
	g_assert_cmpstr (lrg_lsp_language_for_path ("a.h"), ==, "c");
	g_assert_cmpstr (lrg_lsp_language_for_path ("a.lua"), ==, "lua");
	g_assert_cmpstr (lrg_lsp_language_for_path ("a.py"), ==, "python");
	g_assert_cmpstr (lrg_lsp_language_for_path ("a.js"), ==, "javascript");
	g_assert_cmpstr (lrg_lsp_language_for_path ("a.ts"), ==, "typescript");
	g_assert_null (lrg_lsp_language_for_path ("a.txt"));
	g_assert_null (lrg_lsp_find_server ("not-a-language"));
}

static void
test_apply_edits (void)
{
	LrgLspTextEdit  edit;
	g_autoptr(GPtrArray) edits = NULL;
	g_autofree gchar *out = NULL;

	memset (&edit, 0, sizeof edit);
	edit.path = (gchar *) "x.lua";
	edit.line = 0;
	edit.column = 1;
	edit.end_line = 0;
	edit.end_column = 3;
	edit.new_text = (gchar *) "X";
	edits = g_ptr_array_new ();
	g_ptr_array_add (edits, &edit);
	out = lrg_lsp_apply_edits ("abcd", edits);
	g_assert_cmpstr (out, ==, "aXd");
}

static void
test_missing_server (void)
{
	const gchar *argv[] = { "/no/such/libregnum-language-server", NULL };
	g_autoptr(LrgLspClient) client = NULL;
	g_autoptr(GError) error = NULL;

	client = lrg_lsp_client_new ("lua", argv);
	g_assert_false (lrg_lsp_client_start (client, &error));
	g_assert_nonnull (error);
	g_assert_cmpint (lrg_lsp_client_get_state (client), ==, LRG_LSP_CLIENT_STATE_STOPPED);
}

static gint diag_hits = 0;

static void
on_diagnostics (LrgLspClient *client,
                const gchar  *path,
                gpointer      user_data)
{
	(void) client;
	(void) path;
	g_atomic_int_inc ((gint *) user_data);
}

static void
test_fake_server (void)
{
	g_autofree gchar *script = NULL;
	g_autofree gchar *python = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *text = NULL;
	const gchar *argv[3];
	g_autoptr(LrgLspClient) client = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) diags = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GPtrArray) locs = NULL;
	g_autoptr(GPtrArray) refs = NULL;
	g_autoptr(GPtrArray) edits = NULL;
	g_autoptr(GPtrArray) formatted = NULL;
	LrgLspDiagnostic *diag;
	LrgLspCompletionItem *item;
	LrgLspLocation *loc;
	LrgLspTextEdit *edit;
	gint i;

	script = srcdir_file ("data/fake-lsp.py");
	if (!g_file_test (script, G_FILE_TEST_EXISTS))
	{
		g_test_skip ("fake-lsp.py is not next to the tests");
		return;
	}
	python = g_find_program_in_path ("python3");
	if (python == NULL)
	{
		g_test_skip ("python3 is not installed");
		return;
	}

	argv[0] = python;
	argv[1] = script;
	argv[2] = NULL;
	dir = g_dir_make_tmp ("lrg-lsp-XXXXXX", NULL);
	path = g_build_filename (dir, "sample.lua", NULL);
	g_assert_true (g_file_set_contents (path, "local a = 1\n", -1, NULL));

	client = lrg_lsp_client_new ("lua", argv);
	lrg_lsp_client_set_root_path (client, dir);
	g_assert_true (lrg_lsp_client_start (client, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_lsp_client_wait_ready (client, 3000, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_lsp_client_get_server_name (client), ==, "fake-lsp");
	g_assert_cmpint (lrg_lsp_client_get_state (client), ==, LRG_LSP_CLIENT_STATE_READY);

	diag_hits = 0;
	g_signal_connect (client, "diagnostics-changed", G_CALLBACK (on_diagnostics), &diag_hits);
	lrg_lsp_client_did_open (client, path, "local a = 1\n");
	g_assert_true (lrg_lsp_client_is_open (client, path));

	for (i = 0; i < 100 && (diags == NULL || diags->len == 0); i++)
	{
		g_clear_pointer (&diags, g_ptr_array_unref);
		diags = lrg_lsp_client_get_diagnostics (client, path);
		pump (20);
	}
	g_assert_nonnull (diags);
	g_assert_cmpuint (diags->len, >=, 1);
	diag = g_ptr_array_index (diags, 0);
	g_assert_cmpuint (diag->line, ==, 0);
	g_assert_cmpstr (diag->message, ==, "boom");
	g_assert_cmpstr (diag->source, ==, "fake");
	g_assert_cmpstr (diag->code, ==, "E1");
	pump (50);
	g_assert_cmpint (g_atomic_int_get (&diag_hits), >=, 1);

	items = lrg_lsp_client_completion_sync (client, path, 0, 0, 2000, &error);
	g_assert_no_error (error);
	g_assert_nonnull (items);
	g_assert_cmpuint (items->len, ==, 1);
	item = g_ptr_array_index (items, 0);
	g_assert_cmpstr (item->label, ==, "alpha");
	g_assert_cmpint (item->kind, ==, 3);
	g_assert_cmpstr (item->insert_text, ==, "alpha");

	text = lrg_lsp_client_hover_sync (client, path, 0, 0, 2000, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, "a hover");

	locs = lrg_lsp_client_definition_sync (client, path, 0, 0, 2000, &error);
	g_assert_no_error (error);
	g_assert_nonnull (locs);
	g_assert_cmpuint (locs->len, ==, 1);
	loc = g_ptr_array_index (locs, 0);
	g_assert_cmpuint (loc->line, ==, 1);
	g_assert_cmpuint (loc->column, ==, 2);
	g_assert_true (g_strcmp0 (loc->path, path) == 0);

	refs = lrg_lsp_client_references_sync (client, path, 0, 0, TRUE, 2000, &error);
	g_assert_no_error (error);
	g_assert_nonnull (refs);
	g_assert_cmpuint (refs->len, ==, 1);

	edits = lrg_lsp_client_rename_sync (client, path, 0, 0, "beta", 2000, &error);
	g_assert_no_error (error);
	g_assert_nonnull (edits);
	g_assert_cmpuint (edits->len, >=, 1);
	edit = g_ptr_array_index (edits, 0);
	g_assert_cmpstr (edit->new_text, ==, "beta");

	formatted = lrg_lsp_client_formatting_sync (client, path, 4, TRUE, 2000, &error);
	g_assert_no_error (error);
	g_assert_nonnull (formatted);
	g_assert_cmpuint (formatted->len, >=, 1);
	edit = g_ptr_array_index (formatted, 0);
	g_assert_cmpstr (edit->new_text, ==, "X");

	lrg_lsp_client_did_change (client, path, "local b = 2\n");
	lrg_lsp_client_did_save (client, path, "local b = 2\n");
	lrg_lsp_client_did_close (client, path);
	g_assert_false (lrg_lsp_client_is_open (client, path));

	lrg_lsp_client_stop (client);
	g_assert_cmpint (lrg_lsp_client_get_state (client), ==, LRG_LSP_CLIENT_STATE_STOPPED);
	g_unlink (path);
	g_rmdir (dir);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/lsp/language-for-path", test_language_for_path);
	g_test_add_func ("/lsp/apply-edits", test_apply_edits);
	g_test_add_func ("/lsp/missing-server", test_missing_server);
	g_test_add_func ("/lsp/fake-server", test_fake_server);
	return g_test_run ();
}
