/* lrg-external-editor.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Opening files in the user's code editor.
 *
 * LrgExternalEditor turns "open this file at this line" into the right
 * command for the editor the user picked in #LrgEditorSettings:
 *
 *  - "auto" (the default) follows `$VISUAL`, then `$EDITOR`. The command's
 *    basename is matched against the built-in presets (so
 *    `EDITOR=emacsclient_tty` still gets emacsclient's `+LINE:COL`
 *    arguments while keeping the user's own wrapper as the program).
 *    Without either variable the desktop default handler is used.
 *  - A preset id ("emacsclient", "nvim", "codium", "helix", ...).
 *  - "custom": #LrgEditorSettings:external-editor-command.
 *
 * Templates are split shell-style first and the placeholders `{file}`,
 * `{line}`, `{col}`, `{project}` and `{dir}` are substituted inside each
 * argument afterwards, so paths with spaces never break the command line.
 * The placeholders match Godot's external editor flags, so existing
 * configurations carry over.
 *
 * Terminal editors (vim, nvim, nano, helix, kakoune, micro) are wrapped in
 * #LrgEditorSettings:terminal-command, `$TERMINAL`, or the first terminal
 * found on `PATH`. Inside a running Neovim (`$NVIM` set) the file is sent
 * to that instance with `--remote-send` instead.
 *
 * Emacs gets first-party treatment: when Emacs integration is enabled and
 * the resolved editor is Emacs, the #LrgExternalEditor::open-file signal
 * is emitted first; the editor server answers it by asking a connected
 * Emacs (libregnum.el) to visit the file in-process. Only when no handler
 * takes it is emacsclient launched, falling back to a new `emacs` if no
 * Emacs server is running.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_EXTERNAL_EDITOR (lrg_external_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgExternalEditor, lrg_external_editor, LRG, EXTERNAL_EDITOR, GObject)

/* ==========================================================================
 * Presets
 * ========================================================================== */

/**
 * lrg_external_editor_list_presets:
 *
 * Returns: (transfer none) (array zero-terminated=1): the built-in preset
 *   ids, in display order ("auto" and "custom" included)
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_external_editor_list_presets (void);

/**
 * lrg_external_editor_preset_get_label:
 * @preset: a preset id
 *
 * Returns: (transfer none) (nullable): a human-readable name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_external_editor_preset_get_label (const gchar *preset);

/**
 * lrg_external_editor_preset_get_template:
 * @preset: a preset id
 *
 * Returns: (transfer none) (nullable): the preset's command template, or
 *   %NULL for "auto"/"custom"/unknown ids
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_external_editor_preset_get_template (const gchar *preset);

/**
 * lrg_external_editor_preset_is_terminal:
 * @preset: a preset id
 *
 * Returns: %TRUE if the preset's editor runs inside a terminal
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_external_editor_preset_is_terminal (const gchar *preset);

/**
 * lrg_external_editor_preset_for_command:
 * @command: a program name or path, e.g. "/usr/bin/nvim" or
 *   "emacsclient_tty"
 *
 * Maps an executable to the preset whose argument conventions it uses.
 *
 * Returns: (transfer none) (nullable): the preset id, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_external_editor_preset_for_command (const gchar *command);

/**
 * lrg_external_editor_preset_is_emacs:
 * @preset: (nullable): a preset id
 *
 * Returns: %TRUE for the Emacs family ("emacs", "emacsclient")
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_external_editor_preset_is_emacs (const gchar *preset);

/* ==========================================================================
 * Launcher
 * ========================================================================== */

/**
 * lrg_external_editor_new:
 * @settings: (nullable) (transfer none): settings to follow; %NULL uses
 *   defaults (auto)
 *
 * Returns: (transfer full): a new #LrgExternalEditor
 */
LRG_AVAILABLE_IN_ALL
LrgExternalEditor * lrg_external_editor_new (LrgEditorSettings *settings);

/**
 * lrg_external_editor_get_settings:
 * @self: an #LrgExternalEditor
 *
 * Returns: (transfer none): the settings in use
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSettings * lrg_external_editor_get_settings (LrgExternalEditor *self);

/**
 * lrg_external_editor_set_project_dir:
 * @self: an #LrgExternalEditor
 * @dir: (nullable): the project root, substituted for `{project}`
 */
LRG_AVAILABLE_IN_ALL
void lrg_external_editor_set_project_dir (LrgExternalEditor *self,
                                          const gchar       *dir);

/**
 * lrg_external_editor_resolve_preset:
 * @self: an #LrgExternalEditor
 * @source: (out) (optional) (transfer full): where the choice came from
 *   ("settings", "$VISUAL", "$EDITOR", "desktop default")
 *
 * Returns: (transfer full): the preset id that will be used (never "auto")
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_external_editor_resolve_preset (LrgExternalEditor  *self,
                                            gchar             **source);

/**
 * lrg_external_editor_build_argv:
 * @self: an #LrgExternalEditor
 * @file: the file to open
 * @line: 1-based line (<= 0 for "start of file")
 * @column: 1-based column (<= 0 for the first column)
 * @error: (nullable): return location for an error
 *
 * Builds the full command line without running it (wrapped in a terminal
 * where needed). Useful for previews in the preferences dialog and tests.
 *
 * Returns: (transfer full) (array zero-terminated=1) (nullable): the argv
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_external_editor_build_argv (LrgExternalEditor  *self,
                                         const gchar        *file,
                                         gint                line,
                                         gint                column,
                                         GError            **error);

/**
 * lrg_external_editor_open:
 * @self: an #LrgExternalEditor
 * @file: the file to open
 * @line: 1-based line (<= 0 for "start of file")
 * @column: 1-based column (<= 0 for the first column)
 * @error: (nullable): return location for an error
 *
 * Opens @file. Emacs integration is tried first (see the class
 * description); otherwise the command is spawned detached. The child is
 * reaped asynchronously; a failing emacsclient falls back to `emacs`.
 *
 * Returns: %TRUE if a handler took the request or the process started
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_external_editor_open (LrgExternalEditor  *self,
                                   const gchar        *file,
                                   gint                line,
                                   gint                column,
                                   GError            **error);

/**
 * lrg_external_editor_find_terminal:
 * @self: an #LrgExternalEditor
 *
 * Returns: (transfer full) (array zero-terminated=1) (nullable): the
 *   terminal wrapper argv (e.g. {"gst", "-e"}), or %NULL if none is found
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_external_editor_find_terminal (LrgExternalEditor *self);

/**
 * lrg_external_editor_expand_template:
 * @template_str: a command template
 * @file: value for `{file}`
 * @line: value for `{line}`
 * @column: value for `{col}`
 * @project: (nullable): value for `{project}`
 * @error: (nullable): return location for an error
 *
 * Splits @template_str shell-style and substitutes the placeholders in each
 * argument. An argument that is exactly `{project}` is dropped when there
 * is no project. If the template has no `{file}`, the file is appended.
 *
 * Returns: (transfer full) (array zero-terminated=1) (nullable): the argv
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_external_editor_expand_template (const gchar  *template_str,
                                              const gchar  *file,
                                              gint          line,
                                              gint          column,
                                              const gchar  *project,
                                              GError      **error);

G_END_DECLS
