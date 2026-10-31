/* lrg-editor-validate.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Level validation ("Problems").
 *
 * lrg_editor_validate_level() checks a level for mistakes that would make
 * it misbehave at runtime or in other tools: duplicate GUIDs, missing
 * asset and script files, unregistered component types, empty or
 * ambiguous names, invalid transforms. The editor's Problems panel, the
 * `lrgeditor --validate` command and the "level.validate" IPC method all
 * use it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LrgEditorProblemSeverity:
 * @LRG_EDITOR_PROBLEM_INFO: a hint
 * @LRG_EDITOR_PROBLEM_WARNING: probably wrong
 * @LRG_EDITOR_PROBLEM_ERROR: will fail at runtime
 *
 * How serious a problem is.
 */
typedef enum
{
	LRG_EDITOR_PROBLEM_INFO,
	LRG_EDITOR_PROBLEM_WARNING,
	LRG_EDITOR_PROBLEM_ERROR
} LrgEditorProblemSeverity;

#define LRG_TYPE_EDITOR_PROBLEM (lrg_editor_problem_get_type ())

/**
 * LrgEditorProblem:
 * @severity: how serious it is
 * @code: stable machine-readable code, e.g. "missing-asset"
 * @node_guid: (nullable): the node it concerns
 * @node_path: (nullable): that node's name path (for display)
 * @message: human-readable description
 * @file: (nullable): a related file (missing asset, script)
 *
 * One validation finding.
 */
typedef struct
{
	LrgEditorProblemSeverity  severity;
	gchar                    *code;
	gchar                    *node_guid;
	gchar                    *node_path;
	gchar                    *message;
	gchar                    *file;
} LrgEditorProblem;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_problem_get_type (void) G_GNUC_CONST;

/**
 * lrg_editor_problem_copy:
 * @problem: a problem
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgEditorProblem * lrg_editor_problem_copy (const LrgEditorProblem *problem);

/**
 * lrg_editor_problem_free:
 * @problem: a problem
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_problem_free (LrgEditorProblem *problem);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEditorProblem, lrg_editor_problem_free)

/**
 * lrg_editor_problem_severity_to_string:
 * @severity: a severity
 *
 * Returns: (transfer none): "info", "warning" or "error"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_problem_severity_to_string (LrgEditorProblemSeverity severity);

/**
 * lrg_editor_validate_level:
 * @level: the level to check
 * @base_dir: (nullable): directory relative asset and script paths are
 *   resolved against (the project root); %NULL skips file checks
 * @registry: (nullable): registry used to check component types; %NULL
 *   skips that check
 *
 * Returns: (transfer container) (element-type LrgEditorProblem): problems,
 *   errors first, then in tree order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_validate_level (LrgLevel    *level,
                                       const gchar *base_dir,
                                       LrgRegistry *registry);

G_END_DECLS
