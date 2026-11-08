/* lrg-script-reloader.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptReloader: hot reload of script files in running contexts.  A
 * GFileMonitor watches script files and the scripting search paths;
 * changed files are re-executed in every live #LrgScripting that loaded
 * them, with a state hand-over hook, and a failed reload keeps the old
 * code running.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-scripting.h"

G_BEGIN_DECLS

/**
 * LRG_SCRIPT_HOOK_BEFORE_RELOAD:
 *
 * Optional script function called before a reload.  Its return value is
 * the state handed to #LRG_SCRIPT_HOOK_RELOAD.  Lua and Python keep the
 * value native (tables, dicts, objects survive as they are); other
 * backends pass it as a #GValue.
 */
#define LRG_SCRIPT_HOOK_BEFORE_RELOAD "on_before_reload"

/**
 * LRG_SCRIPT_HOOK_RELOAD:
 *
 * Optional script function called after the new code ran, with one
 * argument: the value #LRG_SCRIPT_HOOK_BEFORE_RELOAD returned (nil/None
 * when the old code had no such hook).
 */
#define LRG_SCRIPT_HOOK_RELOAD "on_reload"

#define LRG_TYPE_SCRIPT_RELOADER (lrg_script_reloader_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptReloader, lrg_script_reloader, LRG, SCRIPT_RELOADER, GObject)

/**
 * lrg_script_reloader_new:
 *
 * Creates a reloader.  It watches nothing until paths or contexts are
 * added.  File monitor events are dispatched on the thread-default
 * #GMainContext of the thread that adds the first watch; applications
 * without a #GMainLoop call lrg_script_reloader_process_pending() once per
 * frame.
 *
 * Returns: (transfer full): a new #LrgScriptReloader
 */
LRG_AVAILABLE_IN_ALL
LrgScriptReloader * lrg_script_reloader_new (void);

/**
 * lrg_script_reloader_get_default:
 *
 * Returns the process-wide reloader the editor's play-in-editor and tool
 * scripts share.
 *
 * Returns: (transfer none): the default #LrgScriptReloader
 */
LRG_AVAILABLE_IN_ALL
LrgScriptReloader * lrg_script_reloader_get_default (void);

/**
 * lrg_script_reloader_watch_path:
 * @self: an #LrgScriptReloader
 * @path: (type filename): a script file or a directory (directories are
 *   watched recursively)
 * @error: (nullable): return location for an error
 *
 * Watches @path for changes.  A changed file triggers
 * #LrgScriptReloader::changed and a reload in every context that loaded it.
 *
 * Returns: %TRUE when the watch was installed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_reloader_watch_path (LrgScriptReloader  *self,
                                         const gchar        *path,
                                         GError            **error);

/**
 * lrg_script_reloader_unwatch_path:
 * @self: an #LrgScriptReloader
 * @path: (type filename): a path given to lrg_script_reloader_watch_path()
 *
 * Stops watching @path.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_reloader_unwatch_path (LrgScriptReloader *self,
                                       const gchar       *path);

/**
 * lrg_script_reloader_watch_scripting:
 * @self: an #LrgScriptReloader
 * @scripting: an #LrgScripting
 *
 * Watches every file @scripting loads (now and later) and its search
 * paths.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_reloader_watch_scripting (LrgScriptReloader *self,
                                          LrgScripting      *scripting);

/**
 * lrg_script_reloader_unwatch_scripting:
 * @self: an #LrgScriptReloader
 * @scripting: an #LrgScripting
 *
 * Stops watching @scripting's files (paths other watches share stay
 * watched).
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_reloader_unwatch_scripting (LrgScriptReloader *self,
                                            LrgScripting      *scripting);

/**
 * lrg_script_reloader_set_watch_all:
 * @self: an #LrgScriptReloader
 * @watch_all: whether to watch every live context
 *
 * With watch-all on, the reloader watches the files and search paths of
 * every #LrgScripting in the process (script components in running
 * worlds, AI scripts, mods), including contexts created later.  The
 * editor turns this on during play-in-editor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_reloader_set_watch_all (LrgScriptReloader *self,
                                        gboolean           watch_all);

/**
 * lrg_script_reloader_get_watch_all:
 * @self: an #LrgScriptReloader
 *
 * Returns: whether watch-all is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_reloader_get_watch_all (LrgScriptReloader *self);

/**
 * lrg_script_reloader_set_enabled:
 * @self: an #LrgScriptReloader
 * @enabled: whether file changes trigger reloads
 *
 * Disabled, changes are still noticed (and #LrgScriptReloader::changed is
 * emitted) but nothing is reloaded until it is enabled again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_reloader_set_enabled (LrgScriptReloader *self,
                                      gboolean           enabled);

/**
 * lrg_script_reloader_get_enabled:
 * @self: an #LrgScriptReloader
 *
 * Returns: whether reloading is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_reloader_get_enabled (LrgScriptReloader *self);

/**
 * lrg_script_reloader_set_debounce_ms:
 * @self: an #LrgScriptReloader
 * @debounce_ms: how long a file must stay quiet before it is reloaded
 *
 * Editors write files in several steps; changes inside the window coalesce
 * into one reload.  Default 100 ms.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_reloader_set_debounce_ms (LrgScriptReloader *self,
                                          guint              debounce_ms);

/**
 * lrg_script_reloader_get_debounce_ms:
 * @self: an #LrgScriptReloader
 *
 * Returns: the debounce window in milliseconds
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_reloader_get_debounce_ms (LrgScriptReloader *self);

/**
 * lrg_script_reloader_reload_file:
 * @self: an #LrgScriptReloader
 * @path: (type filename): a script file
 * @error: (nullable): return location for the first failure
 *
 * Reloads @path now in every live context that loaded it (watched or not),
 * emitting #LrgScriptReloader::reloaded or
 * #LrgScriptReloader::reload-failed per context.  The code panel calls this
 * on save.  A file no context loaded is not an error (0 reloads).
 *
 * Returns: %FALSE when any context failed to reload
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_reloader_reload_file (LrgScriptReloader  *self,
                                          const gchar        *path,
                                          GError            **error);

/**
 * lrg_script_reloader_process_pending:
 * @self: an #LrgScriptReloader
 *
 * Dispatches pending file-monitor events and reloads every changed file
 * whose debounce window passed.  Call once per frame when the application
 * does not run a #GMainLoop on the reloader's context (it is harmless when
 * it does).
 *
 * Returns: the number of files reloaded
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_reloader_process_pending (LrgScriptReloader *self);

/**
 * lrg_script_reloader_get_reload_count:
 * @self: an #LrgScriptReloader
 *
 * Returns: successful per-context reloads since creation
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_reloader_get_reload_count (LrgScriptReloader *self);

/**
 * lrg_script_reloader_get_failure_count:
 * @self: an #LrgScriptReloader
 *
 * Returns: failed per-context reloads since creation
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_reloader_get_failure_count (LrgScriptReloader *self);

/**
 * lrg_script_reloader_dup_last_error:
 * @self: an #LrgScriptReloader
 *
 * Returns: (transfer full) (nullable): "file:line: message" of the most
 *   recent failure, or %NULL when the last reload of every file succeeded
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_script_reloader_dup_last_error (LrgScriptReloader *self);

/**
 * lrg_script_reloader_get_watched_paths:
 * @self: an #LrgScriptReloader
 *
 * Returns: (transfer full) (array zero-terminated=1) (element-type filename):
 *   the canonical files and directories being watched, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_script_reloader_get_watched_paths (LrgScriptReloader *self);

G_END_DECLS
