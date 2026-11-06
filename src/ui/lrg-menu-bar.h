/* lrg-menu-bar.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMenuBar: a horizontal bar of menus built from a GMenuModel.
 *
 * Each top-level submenu of the model is a title in the bar; clicking
 * a title opens its #LrgMenu below it. While one menu is open, hovering
 * another title switches to it, and Left / Right inside a menu move to
 * the neighbouring title. Top-level items without a submenu are plain
 * buttons that run their action.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-widget.h"
#include "lrg-menu.h"

G_BEGIN_DECLS

#define LRG_TYPE_MENU_BAR (lrg_menu_bar_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMenuBar, lrg_menu_bar, LRG, MENU_BAR, LrgWidget)

/**
 * lrg_menu_bar_new:
 *
 * Creates an empty menu bar. Give it menus with
 * lrg_menu_bar_set_model().
 *
 * Returns: (transfer full): a new #LrgMenuBar
 */
LRG_AVAILABLE_IN_ALL
LrgMenuBar * lrg_menu_bar_new (void);

/**
 * lrg_menu_bar_new_from_model:
 * @model: (nullable): the bar description: one item per title, each
 *   with a submenu link (or an action for a plain bar button)
 *
 * Returns: (transfer full): a new #LrgMenuBar
 */
LRG_AVAILABLE_IN_ALL
LrgMenuBar * lrg_menu_bar_new_from_model (GMenuModel *model);

/**
 * lrg_menu_bar_get_model:
 * @self: an #LrgMenuBar
 *
 * Returns: (transfer none) (nullable): the bar description
 */
LRG_AVAILABLE_IN_ALL
GMenuModel * lrg_menu_bar_get_model (LrgMenuBar *self);

/**
 * lrg_menu_bar_set_model:
 * @self: an #LrgMenuBar
 * @model: (nullable): the bar description
 *
 * Replaces the titles. An open menu is closed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_bar_set_model (LrgMenuBar *self,
                             GMenuModel *model);

/**
 * lrg_menu_bar_get_n_titles:
 * @self: an #LrgMenuBar
 *
 * Returns: number of titles in the bar
 */
LRG_AVAILABLE_IN_ALL
guint lrg_menu_bar_get_n_titles (LrgMenuBar *self);

/**
 * lrg_menu_bar_get_title:
 * @self: an #LrgMenuBar
 * @index: a title index
 *
 * Returns: (nullable): the title text without its mnemonic underscore
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_menu_bar_get_title (LrgMenuBar *self,
                                      guint       index);

/**
 * lrg_menu_bar_get_title_rect:
 * @self: an #LrgMenuBar
 * @index: a title index
 * @x: (out) (optional): left, world coordinates
 * @y: (out) (optional): top
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Returns: %TRUE if @index is a title
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_menu_bar_get_title_rect (LrgMenuBar *self,
                                      guint       index,
                                      gfloat     *x,
                                      gfloat     *y,
                                      gfloat     *width,
                                      gfloat     *height);

/**
 * lrg_menu_bar_get_menu:
 * @self: an #LrgMenuBar
 * @index: a title index
 *
 * The menu a title opens, created on first use and kept while the
 * model is unchanged.
 *
 * Returns: (transfer none) (nullable): the menu, %NULL for a plain
 *   bar button
 */
LRG_AVAILABLE_IN_ALL
LrgMenu * lrg_menu_bar_get_menu (LrgMenuBar *self,
                                 guint       index);

/**
 * lrg_menu_bar_open_menu:
 * @self: an #LrgMenuBar
 * @index: a title index
 * @keyboard: highlight the menu's first row (as when opened with keys)
 *
 * Opens title @index's menu below it (closing any other). A plain
 * bar button runs its action instead. The bar must be on a canvas.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_bar_open_menu (LrgMenuBar *self,
                             guint       index,
                             gboolean    keyboard);

/**
 * lrg_menu_bar_close:
 * @self: an #LrgMenuBar
 *
 * Closes the open menu, if any.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_bar_close (LrgMenuBar *self);

/**
 * lrg_menu_bar_get_open_index:
 * @self: an #LrgMenuBar
 *
 * Returns: the title whose menu is open, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_menu_bar_get_open_index (LrgMenuBar *self);

/**
 * lrg_menu_bar_get_highlighted:
 * @self: an #LrgMenuBar
 *
 * Returns: the title under the pointer or keyboard cursor, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_menu_bar_get_highlighted (LrgMenuBar *self);

/**
 * lrg_menu_bar_focus_titles:
 * @self: an #LrgMenuBar
 *
 * Starts keyboard navigation of the bar, the job of F10 in desktop
 * applications: focuses the bar, highlights the first title and shows
 * mnemonic underlines. Left / Right then move between titles and
 * Down / Enter opens one. Bind it to a shortcut through an action (see
 * the menu bar docs); the bar also reacts to F10 itself when it has
 * focus.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_bar_focus_titles (LrgMenuBar *self);

G_END_DECLS
