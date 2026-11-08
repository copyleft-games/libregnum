/* lrg-scripting-graph.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptingGraph runs an #LrgVscriptProgram (a `.rvscript` file).
 * Hosts construct it directly; it is not a #LrgScriptLanguage value.
 * The same program text and the same inputs always produce the same
 * results: delays advance with the `dt` passed to `update`, and
 * `math.random` reads the program seed. One event dispatch stops with
 * %LRG_VSCRIPT_ERROR_BUDGET after #LrgVscriptProgram:step-budget node
 * entries instead of looping forever.
 *
 * A node breakpoint parks the instance. lrg_scripting_call_function()
 * returns %TRUE and lrg_scripting_graph_continue() resumes it.
 * lrg_script_debugger_report_node() is not used here: that call blocks
 * the thread inside a stop loop, which cannot express park-and-return.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-scripting.h"
#include "lrg-vscript-program.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_GRAPH (lrg_scripting_graph_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingGraph, lrg_scripting_graph, LRG, SCRIPTING_GRAPH, LrgScripting)

/**
 * lrg_scripting_graph_new:
 *
 * Returns: (transfer full): a new #LrgScriptingGraph
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingGraph * lrg_scripting_graph_new (void);

/**
 * lrg_scripting_graph_get_program:
 * @self: a graph scripting context
 *
 * Returns: (transfer none) (nullable): the loaded program, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptProgram * lrg_scripting_graph_get_program (LrgScriptingGraph *self);

/**
 * lrg_scripting_graph_dup_log:
 * @self: a graph scripting context
 *
 * Returns: (transfer full) (array zero-terminated=1): lines written by
 *   `debug.log` nodes, oldest first
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_scripting_graph_dup_log (LrgScriptingGraph *self);

/**
 * lrg_scripting_graph_get_steps:
 * @self: a graph scripting context
 *
 * Returns: how many node entries the last dispatch performed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_scripting_graph_get_steps (LrgScriptingGraph *self);

/**
 * lrg_scripting_graph_get_hit:
 * @self: a graph scripting context
 * @node: a node id
 *
 * Returns: how many times @node was entered during the last dispatch
 */
LRG_AVAILABLE_IN_ALL
guint lrg_scripting_graph_get_hit (LrgScriptingGraph *self,
                                   const gchar       *node);

/**
 * lrg_scripting_graph_dup_active:
 * @self: a graph scripting context
 *
 * Returns: (transfer full) (array zero-terminated=1): the node ids
 *   entered during the most recent dispatch, in order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_scripting_graph_dup_active (LrgScriptingGraph *self);

/**
 * lrg_scripting_graph_dup_instances:
 *
 * Returns: (transfer full) (element-type LrgScriptingGraph): every live
 *   context. Sandbox runs keep a %NULL path, so a path lookup skips them.
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_scripting_graph_dup_instances (void);

/**
 * lrg_scripting_graph_set_breakpoint:
 * @self: (nullable): a context (%NULL updates only the shared table)
 * @path: (nullable): a script path (%NULL: this context only)
 * @node: a node id
 * @enabled: %TRUE to set the breakpoint, %FALSE to clear it
 *
 * A path breakpoint applies to every context that loaded that path,
 * including contexts created later. Hitting one parks the context
 * before the node runs.
 *
 * Returns: %TRUE when the breakpoint is set afterwards
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_graph_set_breakpoint (LrgScriptingGraph *self,
                                             const gchar       *path,
                                             const gchar       *node,
                                             gboolean           enabled);

/**
 * lrg_scripting_graph_get_breakpoint:
 * @self: a graph scripting context
 * @node: a node id
 *
 * Returns: %TRUE when @node has a breakpoint on this context
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_graph_get_breakpoint (LrgScriptingGraph *self,
                                             const gchar       *node);

/**
 * lrg_scripting_graph_is_paused:
 * @self: a graph scripting context
 *
 * Returns: %TRUE when a breakpoint has parked this context
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_graph_is_paused (LrgScriptingGraph *self);

/**
 * lrg_scripting_graph_get_paused_node:
 * @self: a graph scripting context
 *
 * Returns: (transfer none) (nullable): the node the context is parked
 *   on, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scripting_graph_get_paused_node (LrgScriptingGraph *self);

/**
 * lrg_scripting_graph_continue:
 * @self: a graph scripting context
 * @error: (nullable): return location for a #GError
 *
 * Runs the node a breakpoint parked on, then the rest of that dispatch.
 * The parked frame is skipped once, so the same breakpoint does not
 * fire again until the node is entered on a later frame.
 *
 * Returns: %TRUE on completion or when another breakpoint parks again
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_graph_continue (LrgScriptingGraph  *self,
                                       GError            **error);

/**
 * lrg_scripting_graph_get_path:
 * @self: a graph scripting context
 *
 * The file path remembered by lrg_scripting_load_file(). A context
 * loaded from a string has none, so a path lookup skips sandboxes.
 *
 * Returns: (transfer none) (nullable): the script path, or %NULL
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scripting_graph_get_path (LrgScriptingGraph *self);

/**
 * lrg_scripting_graph_dup_variables:
 * @self: a graph scripting context
 *
 * The runtime variables of this context (program defaults, then any
 * writes a node made). Values the variant form cannot hold are left
 * out.
 *
 * Returns: (transfer full): an `a{sv}` dictionary
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_scripting_graph_dup_variables (LrgScriptingGraph *self);

G_END_DECLS
