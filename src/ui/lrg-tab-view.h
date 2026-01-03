/* lrg-tab-view.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tab view container widget that displays content in tabbed pages.
 *
 * LrgTabView presents multiple content widgets in a tabbed interface,
 * with a tab bar that allows switching between pages. Only one tab's
 * content is visible at a time.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_TAB_VIEW (lrg_tab_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTabView, lrg_tab_view, LRG, TAB_VIEW, LrgContainer)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_tab_view_new:
 *
 * Creates a new tab view widget.
 *
 * Returns: (transfer full): A new #LrgTabView
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTabView * lrg_tab_view_new (void);

/* ==========================================================================
 * Tab Management
 * ========================================================================== */

/**
 * lrg_tab_view_add_tab:
 * @self: an #LrgTabView
 * @label: the tab label text
 * @content: the widget to display when this tab is active
 *
 * Adds a new tab with the specified label and content widget.
 * The tab view takes a reference to the content widget.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tab_view_add_tab (LrgTabView  *self,
                           const gchar *label,
                           LrgWidget   *content);

/**
 * lrg_tab_view_remove_tab:
 * @self: an #LrgTabView
 * @index: the tab index to remove
 *
 * Removes the tab at the specified index.
 * The content widget's reference is released.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tab_view_remove_tab (LrgTabView *self,
                              guint       index);

/**
 * lrg_tab_view_get_tab_count:
 * @self: an #LrgTabView
 *
 * Gets the number of tabs in the view.
 *
 * Returns: The tab count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tab_view_get_tab_count (LrgTabView *self);

/* ==========================================================================
 * Active Tab
 * ========================================================================== */

/**
 * lrg_tab_view_get_active_tab:
 * @self: an #LrgTabView
 *
 * Gets the index of the currently active tab.
 *
 * Returns: The active tab index
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tab_view_get_active_tab (LrgTabView *self);

/**
 * lrg_tab_view_set_active_tab:
 * @self: an #LrgTabView
 * @index: the tab index to activate
 *
 * Sets which tab is currently active and visible.
 * Emits the "tab-changed" signal if the tab changes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tab_view_set_active_tab (LrgTabView *self,
                                  guint       index);

/* ==========================================================================
 * Tab Position
 * ========================================================================== */

/**
 * lrg_tab_view_get_tab_position:
 * @self: an #LrgTabView
 *
 * Gets the position of the tab bar (top or bottom).
 *
 * Returns: The tab bar position
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTabPosition lrg_tab_view_get_tab_position (LrgTabView *self);

/**
 * lrg_tab_view_set_tab_position:
 * @self: an #LrgTabView
 * @position: the tab bar position
 *
 * Sets whether the tab bar appears at the top or bottom
 * of the content area.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tab_view_set_tab_position (LrgTabView     *self,
                                    LrgTabPosition  position);

/* ==========================================================================
 * Tab Bar Height
 * ========================================================================== */

/**
 * lrg_tab_view_get_tab_height:
 * @self: an #LrgTabView
 *
 * Gets the height of the tab bar in pixels.
 *
 * Returns: The tab bar height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tab_view_get_tab_height (LrgTabView *self);

/**
 * lrg_tab_view_set_tab_height:
 * @self: an #LrgTabView
 * @height: the tab bar height in pixels
 *
 * Sets the height of the tab bar.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tab_view_set_tab_height (LrgTabView *self,
                                  gfloat      height);

/* ==========================================================================
 * Tab Access
 * ========================================================================== */

/**
 * lrg_tab_view_get_tab_content:
 * @self: an #LrgTabView
 * @index: the tab index
 *
 * Gets the content widget for the tab at the specified index.
 *
 * Returns: (transfer none) (nullable): The content widget, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_tab_view_get_tab_content (LrgTabView *self,
                                          guint       index);

/**
 * lrg_tab_view_get_tab_label:
 * @self: an #LrgTabView
 * @index: the tab index
 *
 * Gets the label text for the tab at the specified index.
 *
 * Returns: (transfer none) (nullable): The tab label, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tab_view_get_tab_label (LrgTabView *self,
                                          guint       index);

/**
 * lrg_tab_view_set_tab_label:
 * @self: an #LrgTabView
 * @index: the tab index
 * @label: the new label text
 *
 * Sets the label text for the tab at the specified index.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tab_view_set_tab_label (LrgTabView  *self,
                                 guint        index,
                                 const gchar *label);

G_END_DECLS
