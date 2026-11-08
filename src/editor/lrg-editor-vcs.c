/* lrg-editor-vcs.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorVcs: asynchronous, debounced git status of a project.
 *
 * Refresh state machine (all on the thread-default main context):
 *
 *   queue_refresh() --(debounce timeout)--> start_refresh()
 *   start_refresh():  repo root unknown -> "git rev-parse --show-toplevel"
 *                     known             -> "git status ..."
 *   rev-parse done:   ok -> remember root + prefix, run status
 *                     fails -> not a repo, finish
 *   status done:      parse, swap the table, finish
 *   finish():         count, notify, emit "changed", rerun if requested
 *
 * A request while git runs only sets a flag (rerun) so a burst of file
 * events produces at most two git runs. Callbacks never touch a disposed
 * object: dispose cancels the in-flight call and every callback checks
 * for G_IO_ERROR_CANCELLED before using its user data.
 */

#include "config.h"

#include "lrg-editor-vcs.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Status enum
 * ========================================================================== */

GType
lrg_editor_vcs_status_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_VCS_STATUS_UNMODIFIED, "LRG_EDITOR_VCS_STATUS_UNMODIFIED", "unmodified" },
			{ LRG_EDITOR_VCS_STATUS_MODIFIED, "LRG_EDITOR_VCS_STATUS_MODIFIED", "modified" },
			{ LRG_EDITOR_VCS_STATUS_ADDED, "LRG_EDITOR_VCS_STATUS_ADDED", "added" },
			{ LRG_EDITOR_VCS_STATUS_DELETED, "LRG_EDITOR_VCS_STATUS_DELETED", "deleted" },
			{ LRG_EDITOR_VCS_STATUS_RENAMED, "LRG_EDITOR_VCS_STATUS_RENAMED", "renamed" },
			{ LRG_EDITOR_VCS_STATUS_UNTRACKED, "LRG_EDITOR_VCS_STATUS_UNTRACKED", "untracked" },
			{ LRG_EDITOR_VCS_STATUS_IGNORED, "LRG_EDITOR_VCS_STATUS_IGNORED", "ignored" },
			{ LRG_EDITOR_VCS_STATUS_CONFLICTED, "LRG_EDITOR_VCS_STATUS_CONFLICTED", "conflicted" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorVcsStatus"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

const gchar *
lrg_editor_vcs_status_to_string (LrgEditorVcsStatus status)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EDITOR_VCS_STATUS);
	GEnumValue           *v = g_enum_get_value (klass, (gint) status);

	/* Nicks are static strings, so returning one past the unref is safe. */
	return (v != NULL) ? v->value_nick : "unmodified";
}

gchar
lrg_editor_vcs_status_to_letter (LrgEditorVcsStatus status)
{
	switch (status)
	{
	case LRG_EDITOR_VCS_STATUS_MODIFIED:   return 'M';
	case LRG_EDITOR_VCS_STATUS_ADDED:      return 'A';
	case LRG_EDITOR_VCS_STATUS_DELETED:    return 'D';
	case LRG_EDITOR_VCS_STATUS_RENAMED:    return 'R';
	case LRG_EDITOR_VCS_STATUS_UNTRACKED:  return 'U';
	case LRG_EDITOR_VCS_STATUS_IGNORED:    return 'I';
	case LRG_EDITOR_VCS_STATUS_CONFLICTED: return '!';
	case LRG_EDITOR_VCS_STATUS_UNMODIFIED:
	default:                               return ' ';
	}
}

/* Badge precedence when folding a folder's contents. */
static gint
status_rank (LrgEditorVcsStatus status)
{
	switch (status)
	{
	case LRG_EDITOR_VCS_STATUS_CONFLICTED: return 6;
	case LRG_EDITOR_VCS_STATUS_MODIFIED:   return 5;
	case LRG_EDITOR_VCS_STATUS_DELETED:    return 4;
	case LRG_EDITOR_VCS_STATUS_RENAMED:    return 3;
	case LRG_EDITOR_VCS_STATUS_ADDED:      return 2;
	case LRG_EDITOR_VCS_STATUS_UNTRACKED:  return 1;
	case LRG_EDITOR_VCS_STATUS_IGNORED:
	case LRG_EDITOR_VCS_STATUS_UNMODIFIED:
	default:                               return 0;
	}
}

/* ==========================================================================
 * Porcelain parsing
 * ========================================================================== */

/* Folds git's "XY" pair into one status (see git-status(1), "Short
 * Format"). Conflicts first, then renames, deletions, additions and
 * modifications. */
static LrgEditorVcsStatus
fold_xy (gchar x,
         gchar y)
{
	if (x == '?' && y == '?')
		return LRG_EDITOR_VCS_STATUS_UNTRACKED;
	if (x == '!' && y == '!')
		return LRG_EDITOR_VCS_STATUS_IGNORED;
	if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D'))
		return LRG_EDITOR_VCS_STATUS_CONFLICTED;
	if (x == 'R' || y == 'R')
		return LRG_EDITOR_VCS_STATUS_RENAMED;
	if (x == 'D' || y == 'D')
		return LRG_EDITOR_VCS_STATUS_DELETED;
	if (x == 'A' || x == 'C' || y == 'A' || y == 'C')
		return LRG_EDITOR_VCS_STATUS_ADDED;
	if (x == 'M' || y == 'M' || x == 'T' || y == 'T')
		return LRG_EDITOR_VCS_STATUS_MODIFIED;
	return LRG_EDITOR_VCS_STATUS_UNMODIFIED;
}

/* "## main...origin/main [ahead 1]" / "## No commits yet on main" /
 * "## HEAD (no branch)" -> branch name. */
static gchar *
parse_branch (const gchar *header)
{
	const gchar *dots;
	const gchar *space;

	if (g_str_has_prefix (header, "No commits yet on "))
		return g_strdup (header + strlen ("No commits yet on "));
	if (g_str_has_prefix (header, "Initial commit on "))
		return g_strdup (header + strlen ("Initial commit on "));
	if (g_str_has_prefix (header, "HEAD (no branch)"))
		return g_strdup ("HEAD");
	dots = strstr (header, "...");
	if (dots != NULL)
		return g_strndup (header, (gsize) (dots - header));
	space = strchr (header, ' ');
	return (space != NULL) ? g_strndup (header, (gsize) (space - header)) : g_strdup (header);
}

GHashTable *
lrg_editor_vcs_parse_porcelain (const gchar  *data,
                                gsize         length,
                                const gchar  *prefix,
                                gchar       **branch)
{
	GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	gsize       pos = 0;
	gsize       prefix_len = (prefix != NULL) ? strlen (prefix) : 0;

	if (branch != NULL)
		*branch = NULL;
	if (data == NULL)
		return table;

	/* Records are NUL-terminated; a rename/copy record is followed by an
	 * extra record holding the original path. */
	while (pos < length)
	{
		const gchar       *rec = data + pos;
		gsize              len = strnlen (rec, length - pos);
		LrgEditorVcsStatus status;
		const gchar       *path;
		gchar              x, y;

		pos += len + 1;
		if (len >= 3 && rec[0] == '#' && rec[1] == '#' && rec[2] == ' ')
		{
			if (branch != NULL && *branch == NULL)
			{
				g_autofree gchar *header = g_strndup (rec + 3, len - 3);

				*branch = parse_branch (header);
			}
			continue;
		}
		if (len < 4 || rec[2] != ' ')
			continue;
		x = rec[0];
		y = rec[1];
		path = rec + 3;
		if (x == 'R' || x == 'C' || y == 'R' || y == 'C')
		{
			/* Skip the "from" path record. */
			gsize orig = (pos < length) ? strnlen (data + pos, length - pos) : 0;

			pos += orig + 1;
		}

		status = fold_xy (x, y);
		if (status == LRG_EDITOR_VCS_STATUS_UNMODIFIED)
			continue;

		/* Keep only paths under the project (prefix), keyed relative to
		 * it. A reported folder that contains the whole project (an
		 * untracked/ignored ancestor) applies to the project root "". */
		if (prefix_len > 0)
		{
			gsize plen = len - 3;

			if (plen >= prefix_len && strncmp (path, prefix, prefix_len) == 0)
				path += prefix_len;
			else if (plen > 0 && path[plen - 1] == '/' && strncmp (prefix, path, plen) == 0)
				path = "";
			else
				continue;
		}
		g_hash_table_replace (table, g_strdup (path), GINT_TO_POINTER (status));
	}
	return table;
}

/* ==========================================================================
 * Object
 * ========================================================================== */

struct _LrgEditorVcs
{
	GObject parent_instance;

	gchar        *project_dir;
	gchar        *project_real;   /* realpath of project_dir (nullable) */
	gchar        *repo_root;      /* nullable */
	gchar        *prefix;         /* project dir relative to repo_root, "" or "a/b/" */
	gboolean      root_known;
	gboolean      is_repo;
	gchar        *branch;
	GHashTable   *statuses;       /* project-relative path -> status */

	guint         debounce_ms;
	guint         debounce_source;
	gboolean      running;
	gboolean      rerun;
	guint         refresh_count;
	GCancellable *cancellable;
	GSubprocess  *proc;           /* in-flight git (nullable) */
};

enum
{
	PROP_0,
	PROP_PROJECT_DIR,
	PROP_DEBOUNCE_MS,
	PROP_IS_REPO,
	PROP_BRANCH,
	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgEditorVcs, lrg_editor_vcs, G_TYPE_OBJECT)

static void start_refresh (LrgEditorVcs *self);

static void
lrg_editor_vcs_dispose (GObject *object)
{
	LrgEditorVcs *self = LRG_EDITOR_VCS (object);

	g_clear_handle_id (&self->debounce_source, g_source_remove);
	if (self->cancellable != NULL)
		g_cancellable_cancel (self->cancellable);
	if (self->proc != NULL)
		g_subprocess_force_exit (self->proc);
	g_clear_object (&self->proc);
	g_clear_object (&self->cancellable);

	G_OBJECT_CLASS (lrg_editor_vcs_parent_class)->dispose (object);
}

static void
lrg_editor_vcs_finalize (GObject *object)
{
	LrgEditorVcs *self = LRG_EDITOR_VCS (object);

	g_free (self->project_dir);
	g_free (self->project_real);
	g_free (self->repo_root);
	g_free (self->prefix);
	g_free (self->branch);
	g_hash_table_unref (self->statuses);

	G_OBJECT_CLASS (lrg_editor_vcs_parent_class)->finalize (object);
}

static void
lrg_editor_vcs_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgEditorVcs *self = LRG_EDITOR_VCS (object);

	switch (prop_id)
	{
	case PROP_PROJECT_DIR:
		g_value_set_string (value, self->project_dir);
		break;
	case PROP_DEBOUNCE_MS:
		g_value_set_uint (value, self->debounce_ms);
		break;
	case PROP_IS_REPO:
		g_value_set_boolean (value, self->is_repo);
		break;
	case PROP_BRANCH:
		g_value_set_string (value, self->branch);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* realpath(3) into a g_malloc'd string, NULL when it does not resolve. */
static gchar *
real_path (const gchar *path)
{
	char  *r = realpath (path, NULL);
	gchar *g;

	if (r == NULL)
		return NULL;
	g = g_strdup (r);
	free (r);
	return g;
}

/* realpath that tolerates missing tails (deleted files): resolves the
 * deepest existing ancestor and appends the rest lexically. */
static gchar *
real_path_lenient (const gchar *path)
{
	g_autofree gchar *canon = g_canonicalize_filename (path, NULL);
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *real_dir = NULL;
	gchar            *r = real_path (canon);

	if (r != NULL)
		return r;
	dir = g_path_get_dirname (canon);
	if (g_str_equal (dir, canon))
		return g_steal_pointer (&canon);
	base = g_path_get_basename (canon);
	real_dir = real_path_lenient (dir);
	return g_build_filename (real_dir, base, NULL);
}

static void
lrg_editor_vcs_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgEditorVcs *self = LRG_EDITOR_VCS (object);

	switch (prop_id)
	{
	case PROP_PROJECT_DIR:
		g_free (self->project_dir);
		self->project_dir = g_strdup (g_value_get_string (value) != NULL ? g_value_get_string (value) : ".");
		g_free (self->project_real);
		self->project_real = real_path_lenient (self->project_dir);
		break;
	case PROP_DEBOUNCE_MS:
		lrg_editor_vcs_set_debounce_ms (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_vcs_class_init (LrgEditorVcsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_editor_vcs_dispose;
	object_class->finalize = lrg_editor_vcs_finalize;
	object_class->get_property = lrg_editor_vcs_get_property;
	object_class->set_property = lrg_editor_vcs_set_property;

	/**
	 * LrgEditorVcs:project-dir:
	 *
	 * The project directory git runs in.
	 */
	properties[PROP_PROJECT_DIR] =
		g_param_spec_string ("project-dir", "Project dir", "Project directory", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorVcs:debounce-ms:
	 *
	 * Delay between the last refresh request and the git run.
	 */
	properties[PROP_DEBOUNCE_MS] =
		g_param_spec_uint ("debounce-ms", "Debounce", "Refresh debounce in milliseconds", 0, 60000,
		                   LRG_EDITOR_VCS_DEFAULT_DEBOUNCE_MS,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditorVcs:is-repo:
	 *
	 * Whether the project is inside a git work tree (as of the last
	 * refresh). %FALSE outside a repository or without git.
	 */
	properties[PROP_IS_REPO] =
		g_param_spec_boolean ("is-repo", "Is repo", "Inside a git work tree", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditorVcs:branch:
	 *
	 * The checked-out branch ("HEAD" when detached), %NULL outside a
	 * repository.
	 */
	properties[PROP_BRANCH] =
		g_param_spec_string ("branch", "Branch", "Current branch", NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditorVcs::changed:
	 * @self: the tracker
	 *
	 * Emitted when a refresh finishes inside a repository (and once when
	 * the project stops being one). Re-read statuses and the branch.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_editor_vcs_init (LrgEditorVcs *self)
{
	self->statuses = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->debounce_ms = LRG_EDITOR_VCS_DEFAULT_DEBOUNCE_MS;
	self->prefix = g_strdup ("");
}

LrgEditorVcs *
lrg_editor_vcs_new (const gchar *project_dir)
{
	g_return_val_if_fail (project_dir != NULL, NULL);
	return g_object_new (LRG_TYPE_EDITOR_VCS, "project-dir", project_dir, NULL);
}

const gchar *
lrg_editor_vcs_get_project_dir (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);
	return self->project_dir;
}

guint
lrg_editor_vcs_get_debounce_ms (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), 0);
	return self->debounce_ms;
}

void
lrg_editor_vcs_set_debounce_ms (LrgEditorVcs *self,
                                guint         debounce_ms)
{
	g_return_if_fail (LRG_IS_EDITOR_VCS (self));

	if (self->debounce_ms == debounce_ms)
		return;
	self->debounce_ms = debounce_ms;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEBOUNCE_MS]);
}

/* ==========================================================================
 * Running git
 * ========================================================================== */

static GSubprocessLauncher *
make_launcher (LrgEditorVcs *self)
{
	GSubprocessLauncher *launcher =
		g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);

	/* Discover the repository from the project dir, never from an
	 * inherited GIT_DIR (e.g. when launched from a git hook), and never
	 * take index.lock: a background status must not fight the user's own
	 * git commands. */
	g_subprocess_launcher_set_cwd (launcher, self->project_dir);
	g_subprocess_launcher_unsetenv (launcher, "GIT_DIR");
	g_subprocess_launcher_unsetenv (launcher, "GIT_WORK_TREE");
	g_subprocess_launcher_unsetenv (launcher, "GIT_INDEX_FILE");
	g_subprocess_launcher_setenv (launcher, "GIT_OPTIONAL_LOCKS", "0", TRUE);
	return launcher;
}

static const gchar * const toplevel_argv[] = { "git", "rev-parse", "--show-toplevel", NULL };
static const gchar * const status_argv[] = { "git", "--no-optional-locks", "status", "--porcelain=v1", "-z",
                                             "--ignored", "--branch", NULL };

/* Records the repository root and the project's prefix inside it. */
static gboolean
adopt_toplevel (LrgEditorVcs *self,
                GBytes       *out)
{
	gsize             len = 0;
	const gchar      *data = (out != NULL) ? g_bytes_get_data (out, &len) : NULL;
	g_autofree gchar *line = NULL;
	g_autofree gchar *root = NULL;
	gsize             n;

	if (data == NULL || len == 0)
		return FALSE;
	line = g_strstrip (g_strndup (data, len));
	root = real_path (line);
	if (root == NULL || self->project_real == NULL)
		return FALSE;

	g_free (self->repo_root);
	self->repo_root = g_steal_pointer (&root);
	g_free (self->prefix);
	n = strlen (self->repo_root);
	if (g_str_equal (self->project_real, self->repo_root))
		self->prefix = g_strdup ("");
	else if (strncmp (self->project_real, self->repo_root, n) == 0 && self->project_real[n] == G_DIR_SEPARATOR)
		self->prefix = g_strconcat (self->project_real + n + 1, "/", NULL);
	else
		self->prefix = g_strdup ("");
	self->root_known = TRUE;
	return TRUE;
}

/* Swaps in a parsed status table. */
static void
adopt_status (LrgEditorVcs *self,
              GBytes       *out)
{
	gsize             len = 0;
	const gchar      *data = (out != NULL) ? g_bytes_get_data (out, &len) : NULL;
	g_autofree gchar *branch = NULL;
	GHashTable       *table = lrg_editor_vcs_parse_porcelain (data, len, self->prefix, &branch);

	g_hash_table_unref (self->statuses);
	self->statuses = table;
	if (!self->is_repo)
	{
		self->is_repo = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_REPO]);
	}
	if (g_strcmp0 (branch, self->branch) != 0)
	{
		g_free (self->branch);
		self->branch = g_steal_pointer (&branch);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BRANCH]);
	}
}

/* Clears everything: not (or no longer) a repository. Returns whether
 * that changed anything. */
static gboolean
adopt_not_repo (LrgEditorVcs *self)
{
	gboolean was = self->is_repo;

	self->root_known = FALSE;
	g_clear_pointer (&self->repo_root, g_free);
	g_hash_table_remove_all (self->statuses);
	if (self->is_repo)
	{
		self->is_repo = FALSE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_REPO]);
	}
	if (self->branch != NULL)
	{
		g_clear_pointer (&self->branch, g_free);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BRANCH]);
	}
	return was;
}

/* End of an asynchronous refresh. */
static void
finish_refresh (LrgEditorVcs *self,
                gboolean      emit)
{
	self->running = FALSE;
	g_clear_object (&self->proc);
	self->refresh_count++;
	if (emit)
		g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
	if (self->rerun)
	{
		self->rerun = FALSE;
		lrg_editor_vcs_queue_refresh (self);
	}
}

/* Spawns @argv; NULL (no error kept) when git cannot be started. */
static GSubprocess *
spawn (LrgEditorVcs       *self,
       const gchar * const *argv)
{
	g_autoptr(GSubprocessLauncher) launcher = make_launcher (self);
	g_autoptr(GError)              error = NULL;
	GSubprocess                   *proc = g_subprocess_launcher_spawnv (launcher, argv, &error);

	if (proc == NULL)
		g_debug ("vcs: cannot run git in %s: %s", self->project_dir, error->message);
	return proc;
}

static void
on_status_done (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
	GSubprocess       *proc = G_SUBPROCESS (source);
	g_autoptr(GBytes)  out = NULL;
	g_autoptr(GError)  error = NULL;
	LrgEditorVcs      *self;

	if (!g_subprocess_communicate_finish (proc, result, &out, NULL, &error) &&
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return;   /* disposed: user_data is gone */
	self = LRG_EDITOR_VCS (user_data);

	if (error == NULL && g_subprocess_get_if_exited (proc) && g_subprocess_get_exit_status (proc) == 0)
	{
		adopt_status (self, out);
		finish_refresh (self, TRUE);
	}
	else
		finish_refresh (self, adopt_not_repo (self));
}

static void
run_status_async (LrgEditorVcs *self)
{
	g_clear_object (&self->proc);
	self->proc = spawn (self, status_argv);
	if (self->proc == NULL)
	{
		finish_refresh (self, adopt_not_repo (self));
		return;
	}
	g_subprocess_communicate_async (self->proc, NULL, self->cancellable, on_status_done, self);
}

static void
on_toplevel_done (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
	GSubprocess       *proc = G_SUBPROCESS (source);
	g_autoptr(GBytes)  out = NULL;
	g_autoptr(GError)  error = NULL;
	LrgEditorVcs      *self;

	if (!g_subprocess_communicate_finish (proc, result, &out, NULL, &error) &&
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return;
	self = LRG_EDITOR_VCS (user_data);

	if (error == NULL && g_subprocess_get_if_exited (proc) && g_subprocess_get_exit_status (proc) == 0 &&
	    adopt_toplevel (self, out))
		run_status_async (self);
	else
		finish_refresh (self, adopt_not_repo (self));
}

static void
start_refresh (LrgEditorVcs *self)
{
	self->running = TRUE;
	if (self->cancellable == NULL)
		self->cancellable = g_cancellable_new ();

	if (self->root_known)
	{
		run_status_async (self);
		return;
	}
	self->proc = spawn (self, toplevel_argv);
	if (self->proc == NULL)
	{
		finish_refresh (self, adopt_not_repo (self));
		return;
	}
	g_subprocess_communicate_async (self->proc, NULL, self->cancellable, on_toplevel_done, self);
}

static gboolean
on_debounce (gpointer user_data)
{
	LrgEditorVcs *self = LRG_EDITOR_VCS (user_data);

	self->debounce_source = 0;
	start_refresh (self);
	return G_SOURCE_REMOVE;
}

void
lrg_editor_vcs_queue_refresh (LrgEditorVcs *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VCS (self));

	if (self->running)
	{
		self->rerun = TRUE;
		return;
	}
	if (self->debounce_source != 0)
		return;   /* already scheduled: coalesce */

	if (self->debounce_ms == 0)
		self->debounce_source = g_idle_add (on_debounce, self);
	else
		self->debounce_source = g_timeout_add (self->debounce_ms, on_debounce, self);
}

/* Runs @argv to completion; returns stdout on exit status 0, NULL (and
 * no error) on a non-zero exit or when git is missing, NULL with @error
 * only on cancellation or I/O failure. */
static GBytes *
run_sync (LrgEditorVcs        *self,
          const gchar * const *argv,
          GCancellable        *cancellable,
          GError             **error)
{
	g_autoptr(GSubprocess) proc = spawn (self, argv);
	g_autoptr(GBytes)      out = NULL;

	if (proc == NULL)
		return NULL;
	if (!g_subprocess_communicate (proc, NULL, cancellable, &out, NULL, error))
		return NULL;
	if (!g_subprocess_get_if_exited (proc) || g_subprocess_get_exit_status (proc) != 0)
		return NULL;
	return g_steal_pointer (&out);
}

gboolean
lrg_editor_vcs_refresh_sync (LrgEditorVcs  *self,
                             GCancellable  *cancellable,
                             GError       **error)
{
	g_autoptr(GBytes) out = NULL;
	GError           *local = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!self->root_known)
	{
		g_autoptr(GBytes) top = run_sync (self, toplevel_argv, cancellable, &local);

		if (local != NULL)
		{
			g_propagate_error (error, local);
			return FALSE;
		}
		if (top == NULL || !adopt_toplevel (self, top))
		{
			gboolean was = adopt_not_repo (self);

			self->refresh_count++;
			if (was)
				g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
			return TRUE;
		}
	}
	out = run_sync (self, status_argv, cancellable, &local);
	if (local != NULL)
	{
		g_propagate_error (error, local);
		return FALSE;
	}
	self->refresh_count++;
	if (out == NULL)
	{
		/* The repository vanished since rev-parse. */
		if (adopt_not_repo (self))
			g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
		return TRUE;
	}
	adopt_status (self, out);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
	return TRUE;
}

gboolean
lrg_editor_vcs_is_refreshing (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), FALSE);
	return self->running || self->debounce_source != 0;
}

guint
lrg_editor_vcs_get_refresh_count (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), 0);
	return self->refresh_count;
}

gboolean
lrg_editor_vcs_get_is_repo (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), FALSE);
	return self->is_repo;
}

const gchar *
lrg_editor_vcs_get_branch (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);
	return self->branch;
}

const gchar *
lrg_editor_vcs_get_repo_root (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);
	return self->is_repo ? self->repo_root : NULL;
}

GHashTable *
lrg_editor_vcs_get_statuses (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);
	return self->statuses;
}

/* ==========================================================================
 * Lookups
 * ========================================================================== */

/* Normalizes a lookup path to a project-relative key without a leading
 * or trailing "/" ("" is the project root). NULL when an absolute path
 * lies outside the project. */
static gchar *
to_key (LrgEditorVcs *self,
        const gchar  *path)
{
	g_autofree gchar *canon = NULL;
	gsize             n;

	if (g_path_is_absolute (path))
	{
		g_autofree gchar *real = real_path_lenient (path);

		if (self->project_real == NULL)
			return NULL;
		n = strlen (self->project_real);
		if (g_str_equal (real, self->project_real))
			return g_strdup ("");
		if (strncmp (real, self->project_real, n) != 0 || real[n] != G_DIR_SEPARATOR)
			return NULL;
		return g_strdup (real + n + 1);
	}
	/* Relative: resolve "." and ".." lexically against a fake root. */
	canon = g_canonicalize_filename (path, "/");
	return g_strdup (canon + 1);
}

/* Status of an untracked/ignored folder containing @key, if any. */
static gboolean
inherited (LrgEditorVcs       *self,
           const gchar        *key,
           LrgEditorVcsStatus *out)
{
	g_autofree gchar *probe = g_strdup (key);
	gchar            *slash;
	gpointer          v;

	/* The whole project (an untracked/ignored ancestor of it). */
	if (g_hash_table_lookup_extended (self->statuses, "", NULL, &v))
	{
		*out = (LrgEditorVcsStatus) GPOINTER_TO_INT (v);
		return TRUE;
	}
	/* Walk up: "a/b/c.png" probes "a/b/" then "a/". */
	while ((slash = strrchr (probe, '/')) != NULL)
	{
		slash[1] = '\0';
		if (g_hash_table_lookup_extended (self->statuses, probe, NULL, &v))
		{
			*out = (LrgEditorVcsStatus) GPOINTER_TO_INT (v);
			return TRUE;
		}
		slash[0] = '\0';
	}
	return FALSE;
}

LrgEditorVcsStatus
lrg_editor_vcs_get_status (LrgEditorVcs *self,
                           const gchar  *path)
{
	g_autofree gchar  *key = NULL;
	g_autofree gchar  *dir_key = NULL;
	gpointer           v;
	LrgEditorVcsStatus status;

	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), LRG_EDITOR_VCS_STATUS_UNMODIFIED);
	g_return_val_if_fail (path != NULL, LRG_EDITOR_VCS_STATUS_UNMODIFIED);

	if (!self->is_repo || (key = to_key (self, path)) == NULL)
		return LRG_EDITOR_VCS_STATUS_UNMODIFIED;
	if (g_hash_table_lookup_extended (self->statuses, key, NULL, &v))
		return (LrgEditorVcsStatus) GPOINTER_TO_INT (v);
	/* A folder queried without its trailing slash. */
	dir_key = g_strconcat (key, "/", NULL);
	if (*key != '\0' && g_hash_table_lookup_extended (self->statuses, dir_key, NULL, &v))
		return (LrgEditorVcsStatus) GPOINTER_TO_INT (v);
	if (inherited (self, key, &status))
		return status;
	return LRG_EDITOR_VCS_STATUS_UNMODIFIED;
}

LrgEditorVcsStatus
lrg_editor_vcs_get_directory_status (LrgEditorVcs *self,
                                     const gchar  *path)
{
	g_autofree gchar  *key = NULL;
	g_autofree gchar  *prefix = NULL;
	GHashTableIter     iter;
	gpointer           k, v;
	LrgEditorVcsStatus best = LRG_EDITOR_VCS_STATUS_UNMODIFIED;
	LrgEditorVcsStatus status;

	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), LRG_EDITOR_VCS_STATUS_UNMODIFIED);
	g_return_val_if_fail (path != NULL, LRG_EDITOR_VCS_STATUS_UNMODIFIED);

	if (!self->is_repo || (key = to_key (self, path)) == NULL)
		return LRG_EDITOR_VCS_STATUS_UNMODIFIED;

	/* The folder itself (or an ancestor) untracked/ignored as a whole. */
	prefix = (*key != '\0') ? g_strconcat (key, "/", NULL) : g_strdup ("");
	if (*prefix != '\0' && g_hash_table_lookup_extended (self->statuses, prefix, NULL, &v))
		return (LrgEditorVcsStatus) GPOINTER_TO_INT (v);
	if (inherited (self, *prefix != '\0' ? prefix : "", &status))
		return status;

	/* Fold everything below it. */
	g_hash_table_iter_init (&iter, self->statuses);
	while (g_hash_table_iter_next (&iter, &k, &v))
	{
		LrgEditorVcsStatus s = (LrgEditorVcsStatus) GPOINTER_TO_INT (v);

		if (!g_str_has_prefix ((const gchar *) k, prefix))
			continue;
		if (status_rank (s) > status_rank (best))
			best = s;
	}
	return best;
}

gboolean
lrg_editor_vcs_directory_has_changes (LrgEditorVcs *self,
                                      const gchar  *path)
{
	LrgEditorVcsStatus s = lrg_editor_vcs_get_directory_status (self, path);

	return s != LRG_EDITOR_VCS_STATUS_UNMODIFIED && s != LRG_EDITOR_VCS_STATUS_IGNORED;
}

/* stdout of a finished git command, or an empty string when git was
 * missing or exited non-zero and no I/O error was set. */
static gchar *
vcs_text (LrgEditorVcs        *self,
          const gchar * const *argv,
          gboolean             empty_on_failure,
          GError             **error)
{
	g_autoptr(GBytes) out = NULL;
	gsize             n = 0;
	const gchar      *data;

	out = run_sync (self, argv, NULL, error);
	if (out == NULL)
	{
		if (!empty_on_failure || (error != NULL && *error != NULL))
			return NULL;
		return g_strdup ("");
	}
	data = g_bytes_get_data (out, &n);
	if (data == NULL)
		return g_strdup ("");
	return g_strndup (data, n);
}

gchar *
lrg_editor_vcs_diff (LrgEditorVcs  *self,
                     GError       **error)
{
	const gchar *argv[] = { "git", "diff", "HEAD", NULL };

	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);

	return vcs_text (self, argv, TRUE, error);
}

gchar *
lrg_editor_vcs_log (LrgEditorVcs  *self,
                    GError       **error)
{
	const gchar *argv[] = { "git", "log", "-n", "20", "--name-only",
	                        "--pretty=format:%s", NULL };

	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);

	return vcs_text (self, argv, TRUE, error);
}

gchar *
lrg_editor_vcs_commit (LrgEditorVcs  *self,
                       const gchar   *message,
                       GError       **error)
{
	const gchar *add_argv[] = { "git", "add", "-A", NULL };
	const gchar *commit_argv[9];
	g_autoptr(GBytes) added = NULL;
	gchar *text;

	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);

	added = run_sync (self, add_argv, NULL, error);
	if (added == NULL && error != NULL && *error != NULL)
		return NULL;

	commit_argv[0] = "git";
	commit_argv[1] = "-c";
	commit_argv[2] = "user.email=lrg@localhost";
	commit_argv[3] = "-c";
	commit_argv[4] = "user.name=libregnum";
	commit_argv[5] = "commit";
	commit_argv[6] = "-m";
	commit_argv[7] = (message != NULL) ? message : "commit";
	commit_argv[8] = NULL;

	text = vcs_text (self, commit_argv, FALSE, error);
	if (text == NULL && (error == NULL || *error == NULL))
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "nothing to commit");
	return text;
}

gchar *
lrg_editor_vcs_lfs_locks (LrgEditorVcs  *self,
                          gboolean      *available,
                          GError       **error)
{
	const gchar *argv[] = { "git", "lfs", "locks", NULL };
	g_autoptr(GBytes) out = NULL;
	gsize        n = 0;
	const gchar *data;

	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);

	if (available != NULL)
		*available = FALSE;
	out = run_sync (self, argv, NULL, error);
	if (out == NULL)
	{
		/* Missing git-lfs, or a repo that does not use it, is optional. */
		if (error != NULL && *error != NULL &&
		    !g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_clear_error (error);
		if (error != NULL && *error != NULL)
			return NULL;
		return g_strdup ("");
	}
	if (available != NULL)
		*available = TRUE;
	data = g_bytes_get_data (out, &n);
	if (data == NULL)
		return g_strdup ("");
	return g_strndup (data, n);
}

const gchar *
lrg_editor_vcs_magit_handoff (LrgEditorVcs *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VCS (self), NULL);

	return "(magit-status)";
}
