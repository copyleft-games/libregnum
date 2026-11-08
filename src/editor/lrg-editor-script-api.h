/* lrg-editor-script-api.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorScriptApi: the `editor` object of editor-time tool scripts.
 *
 * One object per editor session. It runs Lua, Python and Gjs code against
 * the open level (a console REPL per language, script files, the tool
 * scripts of a project's `editor_scripts/` folder), exposes itself to that
 * code as the global `editor`, turns every run into one undo step, lets
 * scripts register menu items / actions backed by script functions, and
 * ticks the level's run-in-editor script bindings while authoring.
 *
 * Every script-facing method exists twice with the same name: as a
 * GI-annotated C function (what Gjs and PyGObject call through the
 * typelib) and as an #LrgScriptable method (what the Lua and Python
 * bridges call). Lists come back as #GPtrArray / string arrays in C and as
 * Lua tables / Python lists in scripts.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-editor.h"
#include "lrg-editor-server.h"
#include "lrg-editor-actions.h"
#include "lrg-editor-log.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_SCRIPTS_DIR:
 *
 * The project folder whose scripts register their tools on project open
 * (`<project>/editor_scripts/`).
 */
#define LRG_EDITOR_SCRIPTS_DIR "editor_scripts"

/**
 * LRG_EDITOR_SCRIPT_TOOL_PREFIX:
 *
 * The id prefix of the actions tool scripts register
 * (`script.tool.<name>`).
 */
#define LRG_EDITOR_SCRIPT_TOOL_PREFIX "script.tool."

#define LRG_TYPE_EDITOR_SCRIPT_API (lrg_editor_script_api_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorScriptApi, lrg_editor_script_api, LRG, EDITOR_SCRIPT_API, GObject)

/* ==========================================================================
 * Construction and wiring
 * ========================================================================== */

/**
 * lrg_editor_script_api_new:
 * @editor: the editor whose level scripts edit
 *
 * Creates the script API for @editor. Without lrg_editor_script_api_set_server()
 * it dispatches lrg_editor_script_api_call() to a private, never-started
 * #LrgEditorServer (the built-in document methods only); without
 * lrg_editor_script_api_set_actions() lrg_editor_script_api_add_menu_item()
 * keeps the tools in its own list and menu but registers no actions.
 *
 * Returns: (transfer full): a new #LrgEditorScriptApi
 */
LRG_AVAILABLE_IN_ALL
LrgEditorScriptApi * lrg_editor_script_api_new (LrgEditor *editor);

/**
 * lrg_editor_script_api_get_editor:
 * @self: an #LrgEditorScriptApi
 *
 * Returns: (transfer none): the editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_script_api_get_editor (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_set_server:
 * @self: an #LrgEditorScriptApi
 * @server: (nullable): the server lrg_editor_script_api_call() dispatches to
 *
 * Uses the application's server (with its app, asset and tool methods)
 * for lrg_editor_script_api_call() and lrg_editor_script_api_toast().
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_set_server (LrgEditorScriptApi *self,
                                       LrgEditorServer    *server);

/**
 * lrg_editor_script_api_get_server:
 * @self: an #LrgEditorScriptApi
 *
 * Returns: (transfer none): the server calls go to (a private one when
 *   none was set)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorServer * lrg_editor_script_api_get_server (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_set_actions:
 * @self: an #LrgEditorScriptApi
 * @actions: (nullable): the registry tools register their actions on
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_set_actions (LrgEditorScriptApi *self,
                                        LrgEditorActions   *actions);

/**
 * lrg_editor_script_api_get_actions:
 * @self: an #LrgEditorScriptApi
 *
 * Returns: (transfer none) (nullable): the action registry
 */
LRG_AVAILABLE_IN_ALL
LrgEditorActions * lrg_editor_script_api_get_actions (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_set_log:
 * @self: an #LrgEditorScriptApi
 * @log: (nullable): where script output and errors are appended (domain
 *   "script")
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_set_log (LrgEditorScriptApi *self,
                                    LrgEditorLog       *log);

/**
 * lrg_editor_script_api_set_project_dir:
 * @self: an #LrgEditorScriptApi
 * @dir: (nullable) (type filename): the open project, %NULL for none
 *
 * Switches project. When the directory changes, the tools of the old
 * project are dropped and every script in `@dir/editor_scripts/` runs (in
 * name order, each in its own context) so it can register its menu items;
 * the folder is then watched and a changed, added or removed script
 * reloads the tools (hot reload). Setting the same directory again does
 * nothing, so hosts may call this every frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_set_project_dir (LrgEditorScriptApi *self,
                                            const gchar        *dir);

/**
 * lrg_editor_script_api_get_project_dir:
 * @self: an #LrgEditorScriptApi
 *
 * Returns: (transfer none) (nullable) (type filename): the project directory
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_script_api_get_project_dir (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_register_methods:
 * @self: an #LrgEditorScriptApi
 * @server: the server to add the `script.*` IPC methods to
 *
 * Registers script.run, script.languages, script.tools, script.run_tool,
 * script.reload, script.run_in_editor and script.editor_running (see
 * docs/editor/tool-scripts.org).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_register_methods (LrgEditorScriptApi *self,
                                             LrgEditorServer    *server);

/* ==========================================================================
 * Running code
 * ========================================================================== */

/**
 * lrg_editor_script_api_language_from_name:
 * @name: (nullable): "lua", "python" / "py", "gjs" / "js" / "javascript",
 *   matched case-insensitively
 *
 * Returns: the language, %LRG_SCRIPT_LANGUAGE_NONE when unknown
 */
LRG_AVAILABLE_IN_ALL
LrgScriptLanguage lrg_editor_script_api_language_from_name (const gchar *name);

/**
 * lrg_editor_script_api_language_is_supported:
 * @language: a language
 *
 * Returns: %TRUE when tool scripts can use @language in this build (Lua,
 *   Python and Gjs, when their backend is built)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_language_is_supported (LrgScriptLanguage language);

/**
 * lrg_editor_script_api_run_string:
 * @self: an #LrgEditorScriptApi
 * @language: the language of @code
 * @code: the code
 * @label: (nullable): the undo label, "Script" when %NULL
 * @result: (out) (optional) (nullable) (transfer full): the value of @code
 *   when it is an expression (Lua: every returned value, tab-separated;
 *   Python: repr()), %NULL for a statement or in Gjs
 * @error: return location for an error
 *
 * Runs @code in the console context of @language: one context per
 * language that lives as long as @self, so variables and functions defined
 * by one call are there for the next (a REPL). Everything the code changes
 * in the level is one undo step labelled @label; `print` output is emitted
 * through #LrgEditorScriptApi::output.
 *
 * Returns: %TRUE on success; on a script error %FALSE with the backend's
 *   message ("file:line: what")
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_run_string (LrgEditorScriptApi  *self,
                                           LrgScriptLanguage    language,
                                           const gchar         *code,
                                           const gchar         *label,
                                           gchar              **result,
                                           GError             **error);

/**
 * lrg_editor_script_api_run_file:
 * @self: an #LrgEditorScriptApi
 * @path: (type filename): a script; relative paths resolve against the
 *   project directory
 * @language: its language, or %LRG_SCRIPT_LANGUAGE_NONE to pick one from
 *   the file extension
 * @result: (out) (optional) (nullable) (transfer full): the value of the
 *   chunk when it returns one (Lua: every returned value, tab-separated;
 *   Python: repr()), %NULL for a statement or in Gjs
 * @error: return location for an error
 *
 * Runs a script file in a fresh context (the "Run Script..." command), as
 * one undo step labelled "Run <basename>". A file run again replaces its
 * previous context; menu items it registers stay until then. The return
 * value is captured the same way as lrg_editor_script_api_run_string().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_run_file (LrgEditorScriptApi  *self,
                                         const gchar         *path,
                                         LrgScriptLanguage    language,
                                         gchar              **result,
                                         GError             **error);

/**
 * lrg_editor_script_api_reload_tools:
 * @self: an #LrgEditorScriptApi
 *
 * Re-runs every script of the project's `editor_scripts/` folder in a
 * fresh context. Tools a script no longer registers disappear from the
 * menu (their actions stay registered but disabled). Errors go to the log
 * and #LrgEditorScriptApi::output.
 *
 * Returns: the number of scripts that loaded without error
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_script_api_reload_tools (LrgEditorScriptApi *self);

/* ==========================================================================
 * Tools (menu items backed by script functions)
 * ========================================================================== */

/**
 * lrg_editor_script_api_add_menu_item:
 * @self: an #LrgEditorScriptApi
 * @name: the tool's name ([a-z0-9_-]); its action id is
 *   `script.tool.<name>`
 * @label: the menu label; "Sub/Label" puts it in a submenu of Tools > Scripts
 * @function: the global function of the calling script to run
 * @shortcut: (nullable): a default shortcut ("Ctrl+Alt+G")
 *
 * Registers a tool. Only valid while a script runs (its top-level code or
 * a callback): the tool belongs to that script's context and calls
 * @function there, inside one undo step labelled @label. In Lua and
 * Python @function may also be the function itself.
 *
 * Returns: %TRUE when registered; %FALSE when no script is running or
 *   @name is invalid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_add_menu_item (LrgEditorScriptApi *self,
                                              const gchar        *name,
                                              const gchar        *label,
                                              const gchar        *function,
                                              const gchar        *shortcut);

/**
 * lrg_editor_script_api_get_tools:
 * @self: an #LrgEditorScriptApi
 *
 * Returns: (transfer full) (array zero-terminated=1): the action ids of the
 *   registered tools, in registration order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_script_api_get_tools (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_get_tool_label:
 * @self: an #LrgEditorScriptApi
 * @id: a tool's action id
 *
 * Returns: (transfer none) (nullable): its label, %NULL when unknown
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_script_api_get_tool_label (LrgEditorScriptApi *self,
                                                    const gchar        *id);

/**
 * lrg_editor_script_api_run_tool:
 * @self: an #LrgEditorScriptApi
 * @id: a tool's action id (or its bare name)
 * @error: return location for an error
 *
 * Calls the tool's function as one undo step (what its menu item does).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_run_tool (LrgEditorScriptApi  *self,
                                         const gchar         *id,
                                         GError             **error);

/**
 * lrg_editor_script_api_get_menu:
 * @self: an #LrgEditorScriptApi
 *
 * The registered tools as a menu (items target the `editor.script.tool.*`
 * actions; "Sub/Label" labels make submenus). It updates in place as
 * scripts register tools and reload, so a menu bar can show it as the
 * Tools > Scripts submenu.
 *
 * Returns: (transfer none): the tools menu
 */
LRG_AVAILABLE_IN_ALL
GMenuModel * lrg_editor_script_api_get_menu (LrgEditorScriptApi *self);

/* ==========================================================================
 * Run-in-editor script bindings
 * ========================================================================== */

/**
 * lrg_editor_script_api_tick:
 * @self: an #LrgEditorScriptApi
 * @delta: seconds since the last tick
 *
 * Runs the level's run-in-editor script bindings (enabled bindings with
 * #LrgScriptBinding:run-in-editor set): a binding seen for the first time
 * gets a context with the `editor` and `node` globals, its script is
 * loaded and `lrg_script_start()` called; every tick then calls
 * `lrg_script_update(delta)`; a binding that disappears, is disabled or
 * loses the flag gets `lrg_script_detach()`. Level edits a tick makes are
 * one undo step ("Run in Editor"); ticks that change nothing leave no
 * history. A script that fails stops with its error kept (see
 * script.editor_running) until its binding or file changes. Hosts call
 * this once per frame while not playing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_tick (LrgEditorScriptApi *self,
                                 gfloat              delta);

/**
 * lrg_editor_script_api_stop_editor_scripts:
 * @self: an #LrgEditorScriptApi
 *
 * Detaches every running run-in-editor script (hosts call it when play
 * starts; the next tick starts them again).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_stop_editor_scripts (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_get_n_editor_scripts:
 * @self: an #LrgEditorScriptApi
 *
 * Returns: the run-in-editor scripts currently started (failed ones
 *   included)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_script_api_get_n_editor_scripts (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_set_run_in_editor:
 * @self: an #LrgEditorScriptApi
 * @node: a node
 * @index: the index of the script binding on @node
 * @run_in_editor: the new flag
 * @error: return location for an error
 *
 * Sets #LrgScriptBinding:run-in-editor as one undo step.
 *
 * Returns: %TRUE on success; %FALSE when @node has no binding @index
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_set_run_in_editor (LrgEditorScriptApi  *self,
                                                  LrgNode             *node,
                                                  guint                index,
                                                  gboolean             run_in_editor,
                                                  GError             **error);

/* ==========================================================================
 * Script-facing methods (the `editor` global)
 * ========================================================================== */

/**
 * lrg_editor_script_api_find_node:
 * @self: an #LrgEditorScriptApi
 * @ref: a GUID or a slash-separated name path ("World/Crate")
 *
 * Returns: (transfer none) (nullable): the node
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_script_api_find_node (LrgEditorScriptApi *self,
                                           const gchar        *ref);

/**
 * lrg_editor_script_api_find_nodes:
 * @self: an #LrgEditorScriptApi
 * @query: (nullable): case-insensitive text a node's name must contain
 *   ("" or %NULL: every node)
 *
 * Returns: (transfer container) (element-type LrgNode): matching nodes in
 *   tree order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_script_api_find_nodes (LrgEditorScriptApi *self,
                                              const gchar        *query);

/**
 * lrg_editor_script_api_create_node:
 * @self: an #LrgEditorScriptApi
 * @name: (nullable): the node's name ("Node" when %NULL)
 * @primitive: (nullable): a primitive visual ("cube", "sphere", "plane",
 *   ...), %NULL for an empty group node
 * @parent: (nullable): the parent, %NULL for the level root
 *
 * Creates a node (one undoable command) and selects it.
 *
 * Returns: (transfer none) (nullable): the new node, %NULL when @primitive
 *   is unknown
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_script_api_create_node (LrgEditorScriptApi *self,
                                             const gchar        *name,
                                             const gchar        *primitive,
                                             LrgNode            *parent);

/**
 * lrg_editor_script_api_delete_node:
 * @self: an #LrgEditorScriptApi
 * @node: the node to delete (with its subtree)
 *
 * Returns: %TRUE when deleted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_delete_node (LrgEditorScriptApi *self,
                                            LrgNode            *node);

/**
 * lrg_editor_script_api_reparent_node:
 * @self: an #LrgEditorScriptApi
 * @node: the node to move
 * @parent: (nullable): the new parent, %NULL for the level root
 *
 * Returns: %TRUE when moved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_reparent_node (LrgEditorScriptApi *self,
                                              LrgNode            *node,
                                              LrgNode            *parent);

/**
 * lrg_editor_script_api_rename_node:
 * @self: an #LrgEditorScriptApi
 * @node: the node
 * @name: the new name
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_rename_node (LrgEditorScriptApi *self,
                                        LrgNode            *node,
                                        const gchar        *name);

/**
 * lrg_editor_script_api_set_value:
 * @self: an #LrgEditorScriptApi
 * @object: a node or another object of the document (visual, material,
 *   script binding, level)
 * @property: a property name
 * @value: the value (converted to the property type when possible)
 *
 * Sets a property as one undoable command (named set_value so it does not
 * shadow GObject's set_property in Gjs and PyGObject).
 *
 * Returns: %TRUE when the property exists and the value was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_set_value (LrgEditorScriptApi *self,
                                          GObject            *object,
                                          const gchar        *property,
                                          const GValue       *value);

/**
 * lrg_editor_script_api_set_location:
 * @self: an #LrgEditorScriptApi
 * @node: the node
 * @x: local X
 * @y: local Y
 * @z: local Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_set_location (LrgEditorScriptApi *self,
                                         LrgNode            *node,
                                         gdouble             x,
                                         gdouble             y,
                                         gdouble             z);

/**
 * lrg_editor_script_api_set_rotation_degrees:
 * @self: an #LrgEditorScriptApi
 * @node: the node
 * @x: local Euler X in degrees
 * @y: local Euler Y in degrees
 * @z: local Euler Z in degrees
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_set_rotation_degrees (LrgEditorScriptApi *self,
                                                 LrgNode            *node,
                                                 gdouble             x,
                                                 gdouble             y,
                                                 gdouble             z);

/**
 * lrg_editor_script_api_set_scale:
 * @self: an #LrgEditorScriptApi
 * @node: the node
 * @x: local X scale
 * @y: local Y scale
 * @z: local Z scale
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_set_scale (LrgEditorScriptApi *self,
                                      LrgNode            *node,
                                      gdouble             x,
                                      gdouble             y,
                                      gdouble             z);

/**
 * lrg_editor_script_api_get_selection:
 * @self: an #LrgEditorScriptApi
 *
 * Returns: (transfer container) (element-type LrgNode): the selected nodes
 *   (topmost roots, in tree order)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_script_api_get_selection (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_select:
 * @self: an #LrgEditorScriptApi
 * @node: a node
 * @add: %TRUE to add to the selection, %FALSE to replace it
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_select (LrgEditorScriptApi *self,
                                   LrgNode            *node,
                                   gboolean            add);

/**
 * lrg_editor_script_api_clear_selection:
 * @self: an #LrgEditorScriptApi
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_clear_selection (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_begin_undo:
 * @self: an #LrgEditorScriptApi
 * @label: (nullable): the history label
 *
 * Opens an undo group: everything until the matching
 * lrg_editor_script_api_end_undo() is one step. Groups nest and only the
 * outermost one is recorded; every run of a script already is a group, so
 * scripts need this only to split a callback's work by label. Groups a
 * script leaves open are closed when the run ends.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_begin_undo (LrgEditorScriptApi *self,
                                       const gchar        *label);

/**
 * lrg_editor_script_api_end_undo:
 * @self: an #LrgEditorScriptApi
 *
 * Closes the group of lrg_editor_script_api_begin_undo() (no-op when none
 * is open).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_end_undo (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_undo:
 * @self: an #LrgEditorScriptApi
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_undo (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_redo:
 * @self: an #LrgEditorScriptApi
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_redo (LrgEditorScriptApi *self);

/**
 * lrg_editor_script_api_list_assets:
 * @self: an #LrgEditorScriptApi
 * @pattern: (nullable): a glob such as "*.rmat", matched against the
 *   project-relative path ('*' also crosses folders), %NULL for all
 *
 * Returns: (transfer full) (array zero-terminated=1): project-relative
 *   asset paths, sorted (empty without a project)
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_script_api_list_assets (LrgEditorScriptApi *self,
                                         const gchar        *pattern);

/**
 * lrg_editor_script_api_import_asset:
 * @self: an #LrgEditorScriptApi
 * @source: (type filename): a file outside (or inside) the project
 * @dest: (nullable): the project-relative destination; %NULL keeps the
 *   basename at the project root
 * @error: return location for an error
 *
 * Copies a file into the project (creating folders; an existing
 * destination is an error).
 *
 * Returns: (transfer full) (nullable): the project-relative path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_script_api_import_asset (LrgEditorScriptApi  *self,
                                            const gchar         *source,
                                            const gchar         *dest,
                                            GError             **error);

/**
 * lrg_editor_script_api_create_asset:
 * @self: an #LrgEditorScriptApi
 * @path: the project-relative path of the new file
 * @contents: its text (YAML for .rmat, .rprefab and the other formats)
 * @error: return location for an error
 *
 * Writes a new text asset into the project (creating folders; an existing
 * file is an error). Paths may not leave the project.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_script_api_create_asset (LrgEditorScriptApi  *self,
                                             const gchar         *path,
                                             const gchar         *contents,
                                             GError             **error);

/**
 * lrg_editor_script_api_log:
 * @self: an #LrgEditorScriptApi
 * @message: a line for the console
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_log (LrgEditorScriptApi *self,
                                const gchar        *message);

/**
 * lrg_editor_script_api_warn:
 * @self: an #LrgEditorScriptApi
 * @message: a warning for the console
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_warn (LrgEditorScriptApi *self,
                                 const gchar        *message);

/**
 * lrg_editor_script_api_toast:
 * @self: an #LrgEditorScriptApi
 * @message: the text
 * @level: (nullable): "info", "success", "warning" or "error"
 *
 * Shows a toast (through the "ui.notify" IPC method; logged when the
 * server has none).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_script_api_toast (LrgEditorScriptApi *self,
                                  const gchar        *message,
                                  const gchar        *level);

/**
 * lrg_editor_script_api_call:
 * @self: an #LrgEditorScriptApi
 * @method: an IPC method ("node.create", "view.focus", ...)
 * @params_json: (nullable): its params as a JSON object text
 * @error: return location for an error (the IPC error message)
 *
 * Calls any IPC method in-process (see docs/editor/ipc-protocol.org).
 *
 * Returns: (transfer full) (nullable): the result as JSON text ("null" for
 *   none)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_script_api_call (LrgEditorScriptApi  *self,
                                    const gchar         *method,
                                    const gchar         *params_json,
                                    GError             **error);

G_END_DECLS
