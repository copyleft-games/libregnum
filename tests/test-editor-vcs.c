/* test-editor-vcs.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgEditorVcs. A throwaway git repository is built with real
 * git commands (through GSubprocess) in a temp dir; every test skips
 * cleanly when git is not installed. The porcelain parser is also tested
 * on hand-written bytes, which needs no git at all.
 *
 * The user's git configuration is ignored (GIT_CONFIG_GLOBAL=/dev/null,
 * GIT_CONFIG_NOSYSTEM=1) so hooks, signing or default-branch settings
 * cannot change the results, and GIT_CEILING_DIRECTORIES stops discovery
 * at the temp root so "outside a repository" really is outside one.
 *
 * Asynchronous refreshes are driven by iterating the default main
 * context until lrg_editor_vcs_is_refreshing() turns FALSE (debounce 0,
 * so no wall-clock waits are involved).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <libregnum.h>

static gchar *test_root = NULL;

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		if (g_file_test (p, G_FILE_TEST_IS_DIR) && !g_file_test (p, G_FILE_TEST_IS_SYMLINK))
			remove_tree (p);
		else
			g_unlink (p);
	}
	g_rmdir (dir);
}

static gboolean
have_git (void)
{
	g_autofree gchar *git = g_find_program_in_path ("git");

	if (git == NULL)
	{
		g_test_skip ("git is not installed");
		return FALSE;
	}
	return TRUE;
}

/* Runs "git <args...>" in @dir; returns whether it exited 0. */
static gboolean
git (const gchar *dir,
     ...)
{
	g_autoptr(GPtrArray)           argv = g_ptr_array_new ();
	g_autoptr(GSubprocessLauncher) launcher = NULL;
	g_autoptr(GSubprocess)         proc = NULL;
	g_autoptr(GError)              error = NULL;
	va_list                        ap;
	const gchar                   *arg;

	g_ptr_array_add (argv, (gpointer) "git");
	g_ptr_array_add (argv, (gpointer) "-c");
	g_ptr_array_add (argv, (gpointer) "user.name=Test");
	g_ptr_array_add (argv, (gpointer) "-c");
	g_ptr_array_add (argv, (gpointer) "user.email=test@example.invalid");
	g_ptr_array_add (argv, (gpointer) "-c");
	g_ptr_array_add (argv, (gpointer) "commit.gpgsign=false");
	g_ptr_array_add (argv, (gpointer) "-c");
	g_ptr_array_add (argv, (gpointer) "init.defaultBranch=main");
	va_start (ap, dir);
	while ((arg = va_arg (ap, const gchar *)) != NULL)
		g_ptr_array_add (argv, (gpointer) arg);
	va_end (ap);
	g_ptr_array_add (argv, NULL);

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
	g_subprocess_launcher_set_cwd (launcher, dir);
	proc = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv->pdata, &error);
	g_assert_no_error (error);
	g_assert_true (g_subprocess_wait (proc, NULL, &error));
	return g_subprocess_get_if_exited (proc) && g_subprocess_get_exit_status (proc) == 0;
}

static gchar *
put (const gchar *dir,
     const gchar *rel,
     const gchar *text)
{
	gchar            *path = g_build_filename (dir, rel, NULL);
	g_autofree gchar *parent = g_path_get_dirname (path);

	g_assert_cmpint (g_mkdir_with_parents (parent, 0755), ==, 0);
	g_assert_true (g_file_set_contents (path, text, -1, NULL));
	return path;
}

static gchar *
make_dir (void)
{
	g_autofree gchar *tmpl = g_build_filename (test_root, "repo-XXXXXX", NULL);

	return g_mkdtemp (g_steal_pointer (&tmpl));
}

/* Runs the main loop until the tracker is idle. */
static void
wait_idle (LrgEditorVcs *vcs)
{
	while (lrg_editor_vcs_is_refreshing (vcs))
		g_main_context_iteration (NULL, TRUE);
}

static void
count_changed (LrgEditorVcs *vcs,
               gpointer      user_data)
{
	(*(guint *) user_data)++;
}

/* Builds a repository with one path in every state:
 *   x.txt        conflicted (merge of two branches editing it)
 *   a.txt        modified           b.txt   deleted
 *   c2.txt       renamed from c.txt f.txt   added
 *   g.txt        untracked          h.log   ignored ("*.log")
 *   build/       ignored folder     newdir/ untracked folder
 *   sub/s.txt    modified (a folder with changes)
 *   clean.txt    unmodified */
static gchar *
make_repo (void)
{
	gchar *dir = make_dir ();

	g_assert_true (git (dir, "init", "-q", NULL));
	g_free (put (dir, "x.txt", "base\n"));
	g_free (put (dir, "a.txt", "a\n"));
	g_free (put (dir, "b.txt", "b\n"));
	g_free (put (dir, "c.txt", "c is long enough to be detected as a rename\n"));
	g_free (put (dir, "clean.txt", "clean\n"));
	g_free (put (dir, "sub/s.txt", "s\n"));
	g_free (put (dir, "sub/t.txt", "t\n"));
	g_free (put (dir, ".gitignore", "*.log\nbuild/\n"));
	g_assert_true (git (dir, "add", "-A", NULL));
	g_assert_true (git (dir, "commit", "-q", "-m", "base", NULL));

	/* Conflict on x.txt. */
	g_assert_true (git (dir, "checkout", "-q", "-b", "other", NULL));
	g_free (put (dir, "x.txt", "other\n"));
	g_assert_true (git (dir, "commit", "-q", "-am", "other", NULL));
	g_assert_true (git (dir, "checkout", "-q", "main", NULL));
	g_free (put (dir, "x.txt", "main\n"));
	g_assert_true (git (dir, "commit", "-q", "-am", "main", NULL));
	g_assert_false (git (dir, "merge", "-q", "other", NULL));

	/* Every other state. */
	g_free (put (dir, "a.txt", "a changed\n"));
	{
		g_autofree gchar *b = g_build_filename (dir, "b.txt", NULL);

		g_assert_cmpint (g_unlink (b), ==, 0);
	}
	g_assert_true (git (dir, "mv", "c.txt", "c2.txt", NULL));
	g_free (put (dir, "f.txt", "f\n"));
	g_assert_true (git (dir, "add", "f.txt", NULL));
	g_free (put (dir, "g.txt", "g\n"));
	g_free (put (dir, "h.log", "h\n"));
	g_free (put (dir, "build/out.bin", "o\n"));
	g_free (put (dir, "newdir/n.txt", "n\n"));
	g_free (put (dir, "sub/s.txt", "s changed\n"));
	return dir;
}

static void
test_parse_porcelain (void)
{
	static const gchar data[] =
		"## main...origin/main [ahead 1]\0"
		" M a.txt\0"
		"R  new.txt\0old.txt\0"
		"?? dir/\0"
		"UU c.txt\0"
		"A  proj/x.txt\0"
		"!! proj/build/\0";
	g_autofree gchar     *branch = NULL;
	g_autofree gchar     *branch2 = NULL;
	g_autoptr(GHashTable) t = lrg_editor_vcs_parse_porcelain (data, sizeof (data) - 1, NULL, &branch);
	g_autoptr(GHashTable) p = NULL;
	static const gchar    unborn[] = "## No commits yet on trunk\0?? proj/\0";

	g_assert_cmpstr (branch, ==, "main");
	g_assert_cmpuint (g_hash_table_size (t), ==, 6);
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (t, "a.txt")), ==, LRG_EDITOR_VCS_STATUS_MODIFIED);
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (t, "new.txt")), ==, LRG_EDITOR_VCS_STATUS_RENAMED);
	g_assert_false (g_hash_table_contains (t, "old.txt"));
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (t, "dir/")), ==, LRG_EDITOR_VCS_STATUS_UNTRACKED);
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (t, "c.txt")), ==, LRG_EDITOR_VCS_STATUS_CONFLICTED);

	/* Prefix filtering re-keys paths under the project. */
	p = lrg_editor_vcs_parse_porcelain (data, sizeof (data) - 1, "proj/", NULL);
	g_assert_cmpuint (g_hash_table_size (p), ==, 2);
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (p, "x.txt")), ==, LRG_EDITOR_VCS_STATUS_ADDED);
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (p, "build/")), ==, LRG_EDITOR_VCS_STATUS_IGNORED);
	g_clear_pointer (&p, g_hash_table_unref);

	/* An untracked ancestor covers the whole project (key ""). */
	p = lrg_editor_vcs_parse_porcelain (unborn, sizeof (unborn) - 1, "proj/sub/", &branch2);
	g_assert_cmpstr (branch2, ==, "trunk");
	g_assert_cmpint (GPOINTER_TO_INT (g_hash_table_lookup (p, "")), ==, LRG_EDITOR_VCS_STATUS_UNTRACKED);

	g_assert_cmpstr (lrg_editor_vcs_status_to_string (LRG_EDITOR_VCS_STATUS_IGNORED), ==, "ignored");
	g_assert_cmpint (lrg_editor_vcs_status_to_letter (LRG_EDITOR_VCS_STATUS_MODIFIED), ==, 'M');
}

static void
test_statuses (void)
{
	g_autofree gchar        *dir = NULL;
	g_autoptr(LrgEditorVcs)  vcs = NULL;
	g_autofree gchar        *abs_a = NULL;
	guint                    changed = 0;

	if (!have_git ())
		return;
	dir = make_repo ();
	vcs = lrg_editor_vcs_new (dir);
	lrg_editor_vcs_set_debounce_ms (vcs, 0);
	g_signal_connect (vcs, "changed", G_CALLBACK (count_changed), &changed);
	g_assert_false (lrg_editor_vcs_get_is_repo (vcs));

	lrg_editor_vcs_queue_refresh (vcs);
	g_assert_true (lrg_editor_vcs_is_refreshing (vcs));
	wait_idle (vcs);
	g_assert_cmpuint (changed, ==, 1);
	g_assert_true (lrg_editor_vcs_get_is_repo (vcs));
	g_assert_cmpstr (lrg_editor_vcs_get_branch (vcs), ==, "main");
	g_assert_nonnull (lrg_editor_vcs_get_repo_root (vcs));

	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "x.txt"), ==, LRG_EDITOR_VCS_STATUS_CONFLICTED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "a.txt"), ==, LRG_EDITOR_VCS_STATUS_MODIFIED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "b.txt"), ==, LRG_EDITOR_VCS_STATUS_DELETED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "c2.txt"), ==, LRG_EDITOR_VCS_STATUS_RENAMED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "f.txt"), ==, LRG_EDITOR_VCS_STATUS_ADDED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "g.txt"), ==, LRG_EDITOR_VCS_STATUS_UNTRACKED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "h.log"), ==, LRG_EDITOR_VCS_STATUS_IGNORED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "clean.txt"), ==, LRG_EDITOR_VCS_STATUS_UNMODIFIED);
	/* Inside untracked / ignored folders. */
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "build/out.bin"), ==, LRG_EDITOR_VCS_STATUS_IGNORED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "newdir/n.txt"), ==, LRG_EDITOR_VCS_STATUS_UNTRACKED);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "./sub/../a.txt"), ==, LRG_EDITOR_VCS_STATUS_MODIFIED);

	/* Absolute lookups, also for files that no longer exist. */
	abs_a = g_build_filename (dir, "a.txt", NULL);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, abs_a), ==, LRG_EDITOR_VCS_STATUS_MODIFIED);
	{
		g_autofree gchar *abs_b = g_build_filename (dir, "b.txt", NULL);

		g_assert_cmpint (lrg_editor_vcs_get_status (vcs, abs_b), ==, LRG_EDITOR_VCS_STATUS_DELETED);
	}
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "/somewhere/else.txt"), ==, LRG_EDITOR_VCS_STATUS_UNMODIFIED);

	/* Folder aggregation. */
	g_assert_cmpint (lrg_editor_vcs_get_directory_status (vcs, "sub"), ==, LRG_EDITOR_VCS_STATUS_MODIFIED);
	g_assert_true (lrg_editor_vcs_directory_has_changes (vcs, "sub"));
	g_assert_cmpint (lrg_editor_vcs_get_directory_status (vcs, "build"), ==, LRG_EDITOR_VCS_STATUS_IGNORED);
	g_assert_false (lrg_editor_vcs_directory_has_changes (vcs, "build"));
	g_assert_cmpint (lrg_editor_vcs_get_directory_status (vcs, "newdir"), ==, LRG_EDITOR_VCS_STATUS_UNTRACKED);
	g_assert_cmpint (lrg_editor_vcs_get_directory_status (vcs, ""), ==, LRG_EDITOR_VCS_STATUS_CONFLICTED);
	g_assert_cmpint (lrg_editor_vcs_get_directory_status (vcs, dir), ==, LRG_EDITOR_VCS_STATUS_CONFLICTED);

	/* Synchronous refresh gives the same answers. */
	g_assert_true (lrg_editor_vcs_refresh_sync (vcs, NULL, NULL));
	g_assert_cmpuint (changed, ==, 2);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "c2.txt"), ==, LRG_EDITOR_VCS_STATUS_RENAMED);

	g_clear_object (&vcs);
	remove_tree (dir);
}

static void
test_subdir_project (void)
{
	g_autofree gchar        *dir = NULL;
	g_autofree gchar        *proj = NULL;
	g_autoptr(LrgEditorVcs)  vcs = NULL;
	GHashTable              *all;

	if (!have_git ())
		return;
	dir = make_dir ();
	g_assert_true (git (dir, "init", "-q", NULL));
	g_free (put (dir, "outside.txt", "o\n"));
	g_free (put (dir, "game/assets/hero.png", "h\n"));
	g_assert_true (git (dir, "add", "-A", NULL));
	g_assert_true (git (dir, "commit", "-q", "-m", "init", NULL));
	g_free (put (dir, "outside.txt", "changed\n"));
	g_free (put (dir, "game/assets/hero.png", "changed\n"));
	g_assert_true (git (dir, "checkout", "-q", "-b", "feature/art", NULL));

	proj = g_build_filename (dir, "game", NULL);
	vcs = lrg_editor_vcs_new (proj);
	g_assert_true (lrg_editor_vcs_refresh_sync (vcs, NULL, NULL));
	g_assert_cmpstr (lrg_editor_vcs_get_branch (vcs), ==, "feature/art");

	/* Keys are relative to the project; paths outside it are dropped. */
	all = lrg_editor_vcs_get_statuses (vcs);
	g_assert_cmpuint (g_hash_table_size (all), ==, 1);
	g_assert_true (g_hash_table_contains (all, "assets/hero.png"));
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "assets/hero.png"), ==, LRG_EDITOR_VCS_STATUS_MODIFIED);
	g_assert_true (lrg_editor_vcs_directory_has_changes (vcs, "assets"));

	g_clear_object (&vcs);
	remove_tree (dir);
}

static void
test_unborn_branch (void)
{
	g_autofree gchar        *dir = NULL;
	g_autoptr(LrgEditorVcs)  vcs = NULL;

	if (!have_git ())
		return;
	dir = make_dir ();
	g_assert_true (git (dir, "init", "-q", NULL));
	g_free (put (dir, "new.txt", "n\n"));
	vcs = lrg_editor_vcs_new (dir);
	g_assert_true (lrg_editor_vcs_refresh_sync (vcs, NULL, NULL));
	g_assert_true (lrg_editor_vcs_get_is_repo (vcs));
	g_assert_cmpstr (lrg_editor_vcs_get_branch (vcs), ==, "main");
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "new.txt"), ==, LRG_EDITOR_VCS_STATUS_UNTRACKED);

	g_clear_object (&vcs);
	remove_tree (dir);
}

static void
test_outside_repo (void)
{
	g_autofree gchar        *dir = NULL;
	g_autoptr(LrgEditorVcs)  vcs = NULL;
	guint                    changed = 0;

	if (!have_git ())
		return;
	dir = make_dir ();
	g_free (put (dir, "a.txt", "a\n"));
	vcs = lrg_editor_vcs_new (dir);
	lrg_editor_vcs_set_debounce_ms (vcs, 0);
	g_signal_connect (vcs, "changed", G_CALLBACK (count_changed), &changed);

	lrg_editor_vcs_queue_refresh (vcs);
	wait_idle (vcs);
	g_assert_cmpuint (lrg_editor_vcs_get_refresh_count (vcs), ==, 1);
	g_assert_false (lrg_editor_vcs_get_is_repo (vcs));
	g_assert_null (lrg_editor_vcs_get_branch (vcs));
	g_assert_null (lrg_editor_vcs_get_repo_root (vcs));
	g_assert_cmpuint (changed, ==, 0);
	g_assert_cmpint (lrg_editor_vcs_get_status (vcs, "a.txt"), ==, LRG_EDITOR_VCS_STATUS_UNMODIFIED);
	g_assert_false (lrg_editor_vcs_directory_has_changes (vcs, ""));
	g_assert_true (lrg_editor_vcs_refresh_sync (vcs, NULL, NULL));
	g_assert_false (lrg_editor_vcs_get_is_repo (vcs));

	/* Becoming a repository is picked up by the next refresh. */
	g_assert_true (git (dir, "init", "-q", NULL));
	lrg_editor_vcs_queue_refresh (vcs);
	wait_idle (vcs);
	g_assert_true (lrg_editor_vcs_get_is_repo (vcs));
	g_assert_cmpuint (changed, ==, 1);

	g_clear_object (&vcs);
	remove_tree (dir);
}

static void
test_debounce (void)
{
	g_autofree gchar        *dir = NULL;
	g_autoptr(LrgEditorVcs)  vcs = NULL;
	guint                    changed = 0;
	guint                    i;

	if (!have_git ())
		return;
	dir = make_dir ();
	g_assert_true (git (dir, "init", "-q", NULL));
	vcs = lrg_editor_vcs_new (dir);
	g_assert_cmpuint (lrg_editor_vcs_get_debounce_ms (vcs), ==, LRG_EDITOR_VCS_DEFAULT_DEBOUNCE_MS);
	lrg_editor_vcs_set_debounce_ms (vcs, 0);
	g_signal_connect (vcs, "changed", G_CALLBACK (count_changed), &changed);

	/* A burst coalesces into one git run. */
	for (i = 0; i < 10; i++)
		lrg_editor_vcs_queue_refresh (vcs);
	wait_idle (vcs);
	g_assert_cmpuint (lrg_editor_vcs_get_refresh_count (vcs), ==, 1);
	g_assert_cmpuint (changed, ==, 1);

	/* Requests while git runs schedule exactly one more run: the run
	 * started here (2) plus a single rerun (3), however many requests
	 * arrive meanwhile. */
	lrg_editor_vcs_queue_refresh (vcs);
	while (lrg_editor_vcs_is_refreshing (vcs) && lrg_editor_vcs_get_refresh_count (vcs) == 1)
	{
		g_main_context_iteration (NULL, TRUE);
		for (i = 0; i < 5; i++)
			lrg_editor_vcs_queue_refresh (vcs);
	}
	wait_idle (vcs);
	g_assert_cmpuint (lrg_editor_vcs_get_refresh_count (vcs), ==, 3);

	/* Disposing with a refresh in flight is safe. */
	lrg_editor_vcs_queue_refresh (vcs);
	g_main_context_iteration (NULL, FALSE);
	g_clear_object (&vcs);
	for (i = 0; i < 10; i++)
		g_main_context_iteration (NULL, FALSE);

	remove_tree (dir);
}

int
main (int   argc,
      char *argv[])
{
	g_autofree gchar *ceiling = NULL;
	int               status;

	test_root = g_dir_make_tmp ("lrg-vcs-XXXXXX", NULL);
	g_assert_nonnull (test_root);
	ceiling = g_path_get_dirname (test_root);
	g_setenv ("GIT_CONFIG_GLOBAL", "/dev/null", TRUE);
	g_setenv ("GIT_CONFIG_NOSYSTEM", "1", TRUE);
	g_setenv ("GIT_CEILING_DIRECTORIES", ceiling, TRUE);
	g_unsetenv ("GIT_DIR");
	g_unsetenv ("GIT_WORK_TREE");

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor-vcs/parse-porcelain", test_parse_porcelain);
	g_test_add_func ("/editor-vcs/statuses", test_statuses);
	g_test_add_func ("/editor-vcs/subdir-project", test_subdir_project);
	g_test_add_func ("/editor-vcs/unborn-branch", test_unborn_branch);
	g_test_add_func ("/editor-vcs/outside-repo", test_outside_repo);
	g_test_add_func ("/editor-vcs/debounce", test_debounce);

	status = g_test_run ();
	remove_tree (test_root);
	g_free (test_root);
	return status;
}
