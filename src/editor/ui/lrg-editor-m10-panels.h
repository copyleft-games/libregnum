/* lrg-editor-m10-panels.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Docked panels for the 0.9.0 editor workflows: version control, the
 * letterboxed game view and the open-scene bar.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ui/lrg-widget.h"

G_BEGIN_DECLS

/**
 * lrg_editor_vcs_panel_new:
 *
 * A panel named `vcs-panel` with the diff, log, lock and Magit labels,
 * a commit message and a commit button.
 *
 * Returns: (transfer full): the panel
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_vcs_panel_new (void);

/**
 * lrg_editor_vcs_panel_set_project:
 * @panel: a panel from lrg_editor_vcs_panel_new()
 * @project_dir: (type filename): the git working tree
 *
 * Points the panel at @project_dir and refreshes diff, log, locks and
 * the Magit handoff.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_vcs_panel_set_project (LrgWidget   *panel,
                                       const gchar *project_dir);

/**
 * lrg_editor_game_view_panel_new:
 *
 * A panel that letterboxes a resolution preset into the view.
 *
 * Returns: (transfer full): the panel
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_game_view_panel_new (void);

/**
 * lrg_editor_game_view_panel_set_preset:
 * @panel: a game-view panel
 * @preset: a preset name ("16:9", "steam-deck", ...)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_game_view_panel_set_preset (LrgWidget   *panel,
                                            const gchar *preset);

/**
 * lrg_editor_game_view_panel_get_rect:
 * @panel: a game-view panel
 * @view_w: panel width
 * @view_h: panel height
 * @x: (out): fitted origin
 * @y: (out): fitted origin
 * @w: (out): fitted width
 * @h: (out): fitted height
 *
 * The letterboxed rect of the panel's preset inside the view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_game_view_panel_get_rect (LrgWidget *panel,
                                          gint       view_w,
                                          gint       view_h,
                                          gint      *x,
                                          gint      *y,
                                          gint      *w,
                                          gint      *h);

/**
 * lrg_editor_scene_bar_new:
 * @editor: the editor whose level changes when the active scene does
 *
 * Returns: (transfer full): the scene bar
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_scene_bar_new (LrgEditor *editor);

/**
 * lrg_editor_scene_bar_open:
 * @panel: a scene bar
 * @path: (type filename): a level file
 * @error: (nullable): return location for a #GError
 *
 * Opens @path, makes it the active scene and loads it into the editor.
 *
 * Returns: %TRUE when the level loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_bar_open (LrgWidget    *panel,
                                    const gchar  *path,
                                    GError      **error);

/**
 * lrg_editor_scene_bar_set_active:
 * @panel: a scene bar
 * @index: which open scene becomes the editor's level
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE when that scene loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_scene_bar_set_active (LrgWidget  *panel,
                                          guint       index,
                                          GError    **error);

G_END_DECLS
