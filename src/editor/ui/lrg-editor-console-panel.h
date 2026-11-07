/* lrg-editor-console-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's Console, History and Problems panels.
 *
 * LrgEditorConsolePanel shows the editor log (#LrgEditorLog: GLib log
 * messages, the game's output when it runs with F6, IPC activity) as a
 * virtualized list with level filters and counts, text search,
 * auto-scroll, copy and clear, above a command line that runs actions,
 * IPC methods and raw JSON requests.
 *
 * LrgEditorHistoryPanel lists the undo stack (and the redo stack after
 * it); clicking a row jumps there. LrgEditorProblemsPanel lists the
 * level validation results; clicking a row selects the node, activating
 * it opens the file of a script problem or frames the node.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgEditorConsolePanel
 * ========================================================================== */

#define LRG_TYPE_EDITOR_CONSOLE_PANEL (lrg_editor_console_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorConsolePanel, lrg_editor_console_panel, LRG, EDITOR_CONSOLE_PANEL, LrgContainer)

/**
 * LrgEditorConsoleLevels:
 * @LRG_EDITOR_CONSOLE_LEVEL_NONE: no level
 * @LRG_EDITOR_CONSOLE_LEVEL_ERROR: errors and criticals
 * @LRG_EDITOR_CONSOLE_LEVEL_WARNING: warnings
 * @LRG_EDITOR_CONSOLE_LEVEL_MESSAGE: messages and info
 * @LRG_EDITOR_CONSOLE_LEVEL_DEBUG: debug output
 * @LRG_EDITOR_CONSOLE_LEVEL_ALL: every level
 *
 * The console's level filter groups (the four toggle chips).
 */
typedef enum /*< flags >*/
{
	LRG_EDITOR_CONSOLE_LEVEL_NONE    = 0,
	LRG_EDITOR_CONSOLE_LEVEL_ERROR   = 1 << 0,
	LRG_EDITOR_CONSOLE_LEVEL_WARNING = 1 << 1,
	LRG_EDITOR_CONSOLE_LEVEL_MESSAGE = 1 << 2,
	LRG_EDITOR_CONSOLE_LEVEL_DEBUG   = 1 << 3,
	LRG_EDITOR_CONSOLE_LEVEL_ALL     = 0x0F
} LrgEditorConsoleLevels;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_console_levels_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_CONSOLE_LEVELS (lrg_editor_console_levels_get_type ())

/**
 * lrg_editor_console_panel_new:
 * @app: the editor application it presents
 *
 * Creates the console. It registers the "console.copy",
 * "console.copy-all", "console.clear" and "console.focus" actions on
 * the application's registry.
 *
 * Returns: (transfer full): a new #LrgEditorConsolePanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorConsolePanel * lrg_editor_console_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_console_panel_get_app:
 * @self: an #LrgEditorConsolePanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_console_panel_get_app (LrgEditorConsolePanel *self);

/**
 * lrg_editor_console_panel_execute:
 * @self: an #LrgEditorConsolePanel
 * @line: a command line
 *
 * Runs @line as if typed into the command line (and adds it to the
 * command history): `help`, `clear`, `:action.id`, `::` (list
 * actions), `METHOD key=value ...` (an IPC call; values are parsed as
 * booleans, numbers, `[x,y,z]` arrays or strings) or a raw JSON request
 * `{"method": ...}`. The echo and the reply are appended to the log.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_console_panel_execute (LrgEditorConsolePanel *self,
                                       const gchar           *line);

/**
 * lrg_editor_console_panel_get_levels:
 * @self: an #LrgEditorConsolePanel
 *
 * Returns: the level groups shown
 */
LRG_AVAILABLE_IN_ALL
LrgEditorConsoleLevels lrg_editor_console_panel_get_levels (LrgEditorConsolePanel *self);

/**
 * lrg_editor_console_panel_set_levels:
 * @self: an #LrgEditorConsolePanel
 * @levels: the level groups to show
 *
 * Sets the level filter (the toggle chips follow).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_console_panel_set_levels (LrgEditorConsolePanel  *self,
                                          LrgEditorConsoleLevels  levels);

/**
 * lrg_editor_console_panel_get_search:
 * @self: an #LrgEditorConsolePanel
 *
 * Returns: (transfer none): the search text ("" for none)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_console_panel_get_search (LrgEditorConsolePanel *self);

/**
 * lrg_editor_console_panel_set_search:
 * @self: an #LrgEditorConsolePanel
 * @search: (nullable): case-insensitive text a row's message or domain
 *   must contain, or %NULL / "" for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_console_panel_set_search (LrgEditorConsolePanel *self,
                                          const gchar           *search);

/**
 * lrg_editor_console_panel_get_autoscroll:
 * @self: an #LrgEditorConsolePanel
 *
 * Returns: whether new records scroll the list to the end
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_console_panel_get_autoscroll (LrgEditorConsolePanel *self);

/**
 * lrg_editor_console_panel_set_autoscroll:
 * @self: an #LrgEditorConsolePanel
 * @autoscroll: follow new records
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_console_panel_set_autoscroll (LrgEditorConsolePanel *self,
                                              gboolean               autoscroll);

/**
 * lrg_editor_console_panel_get_n_shown:
 * @self: an #LrgEditorConsolePanel
 *
 * Brings the rows up to date with the log and returns how many pass
 * the filters.
 *
 * Returns: the number of rows shown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_console_panel_get_n_shown (LrgEditorConsolePanel *self);

/**
 * lrg_editor_console_panel_get_row_text:
 * @self: an #LrgEditorConsolePanel
 * @index: a shown row
 *
 * Returns: (transfer none) (nullable): the row as copied to the
 *   clipboard ("HH:MM:SS level domain: message"), %NULL past the end
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_console_panel_get_row_text (LrgEditorConsolePanel *self,
                                                     guint                  index);

/**
 * lrg_editor_console_panel_get_level_count:
 * @self: an #LrgEditorConsolePanel
 * @level: one level group
 *
 * Returns: the number of log records in @level (whatever the filter),
 *   as shown on its chip
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_console_panel_get_level_count (LrgEditorConsolePanel  *self,
                                                LrgEditorConsoleLevels  level);

/* ==========================================================================
 * LrgEditorHistoryPanel
 * ========================================================================== */

#define LRG_TYPE_EDITOR_HISTORY_PANEL (lrg_editor_history_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorHistoryPanel, lrg_editor_history_panel, LRG, EDITOR_HISTORY_PANEL, LrgContainer)

/**
 * lrg_editor_history_panel_new:
 * @app: the editor application it presents
 *
 * Creates the History panel: an "(initial state)" row, one row per
 * undo step (oldest first) and the undone steps after them, dimmed.
 * Clicking a row calls lrg_editor_jump_to_history().
 *
 * Returns: (transfer full): a new #LrgEditorHistoryPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorHistoryPanel * lrg_editor_history_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_history_panel_get_n_rows:
 * @self: an #LrgEditorHistoryPanel
 *
 * Returns: the rows listed (undo + redo steps + the initial state)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_history_panel_get_n_rows (LrgEditorHistoryPanel *self);

/**
 * lrg_editor_history_panel_get_row_label:
 * @self: an #LrgEditorHistoryPanel
 * @index: a row; 0 is the initial state
 *
 * Returns: (transfer none) (nullable): the row's label
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_history_panel_get_row_label (LrgEditorHistoryPanel *self,
                                                      guint                  index);

/**
 * lrg_editor_history_panel_activate_row:
 * @self: an #LrgEditorHistoryPanel
 * @index: a row; 0 is the initial state
 *
 * Jumps the document to the state after step @index (what a click on
 * the row does).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_history_panel_activate_row (LrgEditorHistoryPanel *self,
                                            guint                  index);

/* ==========================================================================
 * LrgEditorProblemsPanel
 * ========================================================================== */

#define LRG_TYPE_EDITOR_PROBLEMS_PANEL (lrg_editor_problems_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorProblemsPanel, lrg_editor_problems_panel, LRG, EDITOR_PROBLEMS_PANEL, LrgContainer)

/**
 * lrg_editor_problems_panel_new:
 * @app: the editor application it presents
 *
 * Creates the Problems panel: severity counts, "Validate Now" and the
 * list of validation results of the open level.
 *
 * Returns: (transfer full): a new #LrgEditorProblemsPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorProblemsPanel * lrg_editor_problems_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_problems_panel_get_n_rows:
 * @self: an #LrgEditorProblemsPanel
 *
 * Brings the list up to date with the last validation.
 *
 * Returns: the problems listed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_problems_panel_get_n_rows (LrgEditorProblemsPanel *self);

/**
 * lrg_editor_problems_panel_activate_row:
 * @self: an #LrgEditorProblemsPanel
 * @index: a listed problem
 * @open: %FALSE selects the node (a click); %TRUE also opens the
 *   problem's file (through the "code.open" IPC method when it is
 *   registered, else the external editor) or frames the node (a double
 *   click)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_problems_panel_activate_row (LrgEditorProblemsPanel *self,
                                             guint                   index,
                                             gboolean                open);

G_END_DECLS

/*
 * Helpers shared by the utility panels (console, profiler, timeline,
 * tiles); not part of the public API.
 */
#ifdef LRG_EDITOR_PANEL_INTERNAL
#include "../../paint/lrg-paint-icons.h"

typedef void (*EditorChipFunc) (LrgWidget *chip, gpointer user_data);

G_GNUC_INTERNAL LrgWidget *  editor_chip_new         (const gchar *name, LrgIcon icon, const gchar *label,
                                                      EditorChipFunc func, gpointer user_data);
G_GNUC_INTERNAL void         editor_chip_set_label   (LrgWidget *chip, const gchar *label);
G_GNUC_INTERNAL void         editor_chip_set_badge   (LrgWidget *chip, const gchar *badge);
G_GNUC_INTERNAL void         editor_chip_set_icon    (LrgWidget *chip, LrgIcon icon);
G_GNUC_INTERNAL void         editor_chip_set_tint    (LrgWidget *chip, LrgThemeColor role);
G_GNUC_INTERNAL void         editor_chip_set_checked (LrgWidget *chip, gboolean checked);
G_GNUC_INTERNAL gboolean     editor_chip_get_checked (LrgWidget *chip);
G_GNUC_INTERNAL void         editor_chip_activate    (LrgWidget *chip);
G_GNUC_INTERNAL void         editor_chip_set_compact (LrgWidget *chip, gboolean compact);
G_GNUC_INTERNAL void         editor_panel_fit_toolbar (LrgWidget *toolbar, gfloat width);
G_GNUC_INTERNAL LrgWidget *  editor_panel_hint_new   (const gchar *name, const gchar *text);
G_GNUC_INTERNAL GrlFont *    editor_panel_mono_font  (LrgWidget *widget);
G_GNUC_INTERNAL void         editor_panel_place_column (LrgContainer *self, LrgWidget *top, LrgWidget *fill,
                                                        LrgWidget *bottom);
#endif
