/* lrg-editor-vcs.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorVcs: git status of a project directory for the editor.
 *
 * The editor shows a badge on every asset tile that differs from git's
 * HEAD and the current branch in its breadcrumb. LrgEditorVcs gathers
 * that information asynchronously:
 *
 *   git rev-parse --show-toplevel                (once, finds the repo)
 *   git --no-optional-locks status --porcelain=v1 -z --ignored --branch
 *
 * through #GSubprocess on the thread-default main context, so the UI
 * never blocks on git. Refresh requests are debounced
 * (lrg_editor_vcs_queue_refresh() several times in a burst runs git once)
 * and #LrgEditorVcs::changed fires when a refresh finishes.
 *
 * Outside a repository, or when git is not installed, every query
 * answers "unmodified", #LrgEditorVcs:is-repo is %FALSE and nothing is
 * logged: version control is optional.
 *
 * Paths: statuses are keyed by paths relative to the project directory
 * (which may be a subdirectory of the repository); lookups accept those
 * relative paths or absolute paths (symlinks resolved, so /home and
 * /var/home spellings both work).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgEditorVcsStatus:
 * @LRG_EDITOR_VCS_STATUS_UNMODIFIED: tracked and clean (or not in a repo)
 * @LRG_EDITOR_VCS_STATUS_MODIFIED: changed in the index or work tree
 *   (also type changes)
 * @LRG_EDITOR_VCS_STATUS_ADDED: staged as new (or copied)
 * @LRG_EDITOR_VCS_STATUS_DELETED: deleted in the index or work tree
 * @LRG_EDITOR_VCS_STATUS_RENAMED: staged rename (keyed by the new path)
 * @LRG_EDITOR_VCS_STATUS_UNTRACKED: not tracked and not ignored
 * @LRG_EDITOR_VCS_STATUS_IGNORED: matched by an ignore rule
 * @LRG_EDITOR_VCS_STATUS_CONFLICTED: unmerged (merge or rebase conflict)
 *
 * The status of one path, folded from git's two-letter "XY" code.
 */
typedef enum
{
	LRG_EDITOR_VCS_STATUS_UNMODIFIED,
	LRG_EDITOR_VCS_STATUS_MODIFIED,
	LRG_EDITOR_VCS_STATUS_ADDED,
	LRG_EDITOR_VCS_STATUS_DELETED,
	LRG_EDITOR_VCS_STATUS_RENAMED,
	LRG_EDITOR_VCS_STATUS_UNTRACKED,
	LRG_EDITOR_VCS_STATUS_IGNORED,
	LRG_EDITOR_VCS_STATUS_CONFLICTED
} LrgEditorVcsStatus;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_vcs_status_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_VCS_STATUS (lrg_editor_vcs_status_get_type ())

/**
 * LRG_EDITOR_VCS_DEFAULT_DEBOUNCE_MS:
 *
 * Default delay between the last lrg_editor_vcs_queue_refresh() and the
 * git run, in milliseconds.
 */
#define LRG_EDITOR_VCS_DEFAULT_DEBOUNCE_MS (250)

#define LRG_TYPE_EDITOR_VCS (lrg_editor_vcs_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorVcs, lrg_editor_vcs, LRG, EDITOR_VCS, GObject)

/**
 * lrg_editor_vcs_new:
 * @project_dir: the project directory (inside a git work tree or not)
 *
 * Creates a tracker for @project_dir. Nothing runs until the first
 * refresh.
 *
 * Returns: (transfer full): a new #LrgEditorVcs
 */
LRG_AVAILABLE_IN_ALL
LrgEditorVcs * lrg_editor_vcs_new (const gchar *project_dir);

/**
 * lrg_editor_vcs_get_project_dir:
 * @self: an #LrgEditorVcs
 *
 * Returns: (transfer none): the project directory
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_vcs_get_project_dir (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_status_to_string:
 * @status: a status
 *
 * Returns: (transfer none): the status nick ("modified", "untracked", ...)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_vcs_status_to_string (LrgEditorVcsStatus status);

/**
 * lrg_editor_vcs_status_to_letter:
 * @status: a status
 *
 * Returns: the one-letter badge for @status ('M', 'A', 'D', 'R', 'U' for
 *   untracked, 'I', '!' for conflicts, ' ' for unmodified)
 */
LRG_AVAILABLE_IN_ALL
gchar lrg_editor_vcs_status_to_letter (LrgEditorVcsStatus status);

/**
 * lrg_editor_vcs_parse_porcelain:
 * @data: (array length=length): the output of
 *   `git status --porcelain=v1 -z [--branch]`
 * @length: the length of @data in bytes
 * @prefix: (nullable): only keep paths under this repo-relative directory
 *   (with a trailing "/"), stripped from the keys; %NULL keeps all
 * @branch: (out) (optional) (nullable) (transfer full): the branch from a
 *   "## " header, %NULL if none (or detached)
 *
 * Parses porcelain v1 -z output. Exposed for tests and tools; the
 * tracker uses it internally.
 *
 * Returns: (transfer full) (element-type utf8 LrgEditorVcsStatus): path
 *   -> status (stored with GINT_TO_POINTER)
 */
LRG_AVAILABLE_IN_ALL
GHashTable * lrg_editor_vcs_parse_porcelain (const gchar  *data,
                                             gsize         length,
                                             const gchar  *prefix,
                                             gchar       **branch);

/**
 * lrg_editor_vcs_get_debounce_ms:
 * @self: an #LrgEditorVcs
 *
 * Returns: the refresh debounce delay in milliseconds
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_vcs_get_debounce_ms (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_set_debounce_ms:
 * @self: an #LrgEditorVcs
 * @debounce_ms: delay in milliseconds; 0 runs on the next main-loop idle
 *   (requests made before it still coalesce)
 *
 * Sets the refresh debounce delay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_vcs_set_debounce_ms (LrgEditorVcs *self,
                                     guint         debounce_ms);

/**
 * lrg_editor_vcs_queue_refresh:
 * @self: an #LrgEditorVcs
 *
 * Schedules an asynchronous refresh after the debounce delay; requests
 * during the delay restart nothing and coalesce into one git run. A
 * request while git is running schedules exactly one more run after it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_vcs_queue_refresh (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_refresh_sync:
 * @self: an #LrgEditorVcs
 * @cancellable: (nullable): a #GCancellable
 * @error: return location for a #GError
 *
 * Runs git now and blocks until it finishes (for headless tools and IPC
 * handlers that must answer with fresh data). Not being in a repository
 * is not an error.
 *
 * Returns: %TRUE unless git failed unexpectedly inside a repository
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_vcs_refresh_sync (LrgEditorVcs  *self,
                                      GCancellable  *cancellable,
                                      GError       **error);

/**
 * lrg_editor_vcs_is_refreshing:
 * @self: an #LrgEditorVcs
 *
 * Returns: whether a refresh is scheduled or git is running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_vcs_is_refreshing (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_get_refresh_count:
 * @self: an #LrgEditorVcs
 *
 * Returns: how many refreshes (git status runs) have completed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_vcs_get_refresh_count (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_get_is_repo:
 * @self: an #LrgEditorVcs
 *
 * Returns: whether the project is inside a git work tree (as of the last
 *   refresh)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_vcs_get_is_repo (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_get_branch:
 * @self: an #LrgEditorVcs
 *
 * Returns: (transfer none) (nullable): the current branch, "HEAD" when
 *   detached, %NULL outside a repository
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_vcs_get_branch (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_get_repo_root:
 * @self: an #LrgEditorVcs
 *
 * Returns: (transfer none) (nullable): the work tree's top-level
 *   directory, %NULL outside a repository
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_vcs_get_repo_root (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_get_statuses:
 * @self: an #LrgEditorVcs
 *
 * Returns every path git reported, relative to the project directory.
 * Clean tracked files are not listed; untracked or ignored folders
 * appear once with a trailing "/".
 *
 * Returns: (transfer none) (element-type utf8 LrgEditorVcsStatus): path
 *   -> status (GINT_TO_POINTER)
 */
LRG_AVAILABLE_IN_ALL
GHashTable * lrg_editor_vcs_get_statuses (LrgEditorVcs *self);

/**
 * lrg_editor_vcs_get_status:
 * @self: an #LrgEditorVcs
 * @path: a file path, relative to the project directory or absolute
 *
 * Returns the status of one file. Files inside an untracked or ignored
 * folder inherit the folder's status.
 *
 * Returns: the status (%LRG_EDITOR_VCS_STATUS_UNMODIFIED when unknown)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorVcsStatus lrg_editor_vcs_get_status (LrgEditorVcs *self,
                                              const gchar  *path);

/**
 * lrg_editor_vcs_get_directory_status:
 * @self: an #LrgEditorVcs
 * @path: a folder, relative to the project directory ("" for the
 *   project itself) or absolute
 *
 * Aggregates a folder for its badge: an untracked or ignored folder
 * reports that; otherwise the most significant status of anything below
 * it, in the order conflicted > modified > deleted > renamed > added >
 * untracked (ignored contents do not count), or unmodified.
 *
 * Returns: the folder's status
 */
LRG_AVAILABLE_IN_ALL
LrgEditorVcsStatus lrg_editor_vcs_get_directory_status (LrgEditorVcs *self,
                                                        const gchar  *path);

/**
 * lrg_editor_vcs_directory_has_changes:
 * @self: an #LrgEditorVcs
 * @path: a folder, relative to the project directory or absolute
 *
 * Returns: whether anything below @path is not unmodified or ignored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_vcs_directory_has_changes (LrgEditorVcs *self,
                                               const gchar  *path);

/**
 * lrg_editor_vcs_diff:
 * @self: an #LrgEditorVcs
 * @error: (nullable): return location for a #GError
 *
 * Runs `git diff HEAD` in the project directory. A clean tree is an
 * empty string. Cancellation is the only error.
 *
 * Returns: (transfer full) (nullable): the diff text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_vcs_diff (LrgEditorVcs  *self,
                             GError       **error);

/**
 * lrg_editor_vcs_log:
 * @self: an #LrgEditorVcs
 * @error: (nullable): return location for a #GError
 *
 * The last 20 commits, subject then the paths each commit touched.
 *
 * Returns: (transfer full) (nullable): the log text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_vcs_log (LrgEditorVcs  *self,
                            GError       **error);

/**
 * lrg_editor_vcs_commit:
 * @self: an #LrgEditorVcs
 * @message: commit subject
 * @error: (nullable): return location for a #GError
 *
 * Stages everything (`git add -A`) and commits it. The author is
 * libregnum, so a project without `user.name` still commits. Nothing
 * to commit is an error.
 *
 * Returns: (transfer full) (nullable): git's stdout, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_vcs_commit (LrgEditorVcs  *self,
                               const gchar   *message,
                               GError       **error);

/**
 * lrg_editor_vcs_lfs_locks:
 * @self: an #LrgEditorVcs
 * @available: (out) (optional): %TRUE when `git lfs locks` ran
 * @error: (nullable): return location for a #GError
 *
 * Runs `git lfs locks`. A missing `git-lfs` or a repository that does
 * not use LFS leaves @available %FALSE and returns an empty string.
 * That is not an error.
 *
 * Returns: (transfer full) (nullable): the lock listing
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_vcs_lfs_locks (LrgEditorVcs  *self,
                                  gboolean      *available,
                                  GError       **error);

/**
 * lrg_editor_vcs_magit_handoff:
 * @self: an #LrgEditorVcs
 *
 * The Emacs form a host can eval to open Magit on this project.
 * Libregnum does not start Emacs.
 *
 * Returns: (transfer none): the constant "(magit-status)"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_vcs_magit_handoff (LrgEditorVcs *self);

G_END_DECLS
