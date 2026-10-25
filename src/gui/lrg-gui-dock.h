/* lrg-gui-dock.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dockable panel layout for the immediate-mode GUI.
 *
 * LrgGuiDock arranges named panels in a tree of resizable splits whose
 * leaves are tab stacks (the Godot/Unreal/Unity editor layout model):
 *
 *   - drag a splitter to resize;
 *   - click a tab to show that panel;
 *   - drag a tab onto another stack's centre to join it, or onto one of
 *     its edges to split it;
 *   - close a tab with its "x"; reopen panels with lrg_gui_dock_open_panel()
 *     (the editor's View menu does this).
 *
 * The layout serializes to a compact string (lrg_gui_dock_to_string()) so
 * the application can store it in its settings, e.g.
 *
 *   split(v,0.20,tabs(0,hierarchy),split(h,0.72,tabs(0,viewport),tabs(0,console,assets)))
 *
 * Usage per frame:
 *
 *   lrg_gui_dock_begin (dock, ctx, &rect);
 *   if (lrg_gui_dock_begin_panel (dock, ctx, "hierarchy", "Hierarchy"))
 *     {
 *       ... widgets ...
 *       lrg_gui_dock_end_panel (dock, ctx);
 *     }
 *   ...
 *   lrg_gui_dock_end (dock, ctx);
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-gui-context.h"

G_BEGIN_DECLS

/**
 * LrgGuiDockSide:
 * @LRG_GUI_DOCK_CENTER: join the target stack as a tab
 * @LRG_GUI_DOCK_LEFT: split the target, new panel on the left
 * @LRG_GUI_DOCK_RIGHT: split the target, new panel on the right
 * @LRG_GUI_DOCK_TOP: split the target, new panel on top
 * @LRG_GUI_DOCK_BOTTOM: split the target, new panel below
 *
 * Where a panel is docked relative to a target panel.
 */
typedef enum
{
	LRG_GUI_DOCK_CENTER,
	LRG_GUI_DOCK_LEFT,
	LRG_GUI_DOCK_RIGHT,
	LRG_GUI_DOCK_TOP,
	LRG_GUI_DOCK_BOTTOM
} LrgGuiDockSide;

#define LRG_TYPE_GUI_DOCK (lrg_gui_dock_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGuiDock, lrg_gui_dock, LRG, GUI_DOCK, GObject)

/**
 * lrg_gui_dock_new:
 *
 * Returns: (transfer full): an empty dock (one empty tab stack)
 */
LRG_AVAILABLE_IN_ALL
LrgGuiDock * lrg_gui_dock_new (void);

/**
 * lrg_gui_dock_load_string:
 * @self: an #LrgGuiDock
 * @layout: a layout string from lrg_gui_dock_to_string()
 * @error: (nullable): return location for an error
 *
 * Replaces the layout. On a parse error the current layout is kept.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_dock_load_string (LrgGuiDock   *self,
                                   const gchar  *layout,
                                   GError      **error);

/**
 * lrg_gui_dock_to_string:
 * @self: an #LrgGuiDock
 *
 * Returns: (transfer full): the layout as a string
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_gui_dock_to_string (LrgGuiDock *self);

/**
 * lrg_gui_dock_dock_panel:
 * @self: an #LrgGuiDock
 * @panel: the panel id to place (removed from wherever it is first)
 * @target: (nullable): a panel already in the layout, or %NULL for the
 *   whole dock
 * @side: where to put @panel relative to @target
 * @ratio: share of the split given to @panel (ignored for the centre)
 *
 * Programmatic docking, used to build default layouts.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_dock_panel (LrgGuiDock     *self,
                              const gchar    *panel,
                              const gchar    *target,
                              LrgGuiDockSide  side,
                              gfloat          ratio);

/**
 * lrg_gui_dock_open_panel:
 * @self: an #LrgGuiDock
 * @panel: a panel id
 *
 * Makes @panel visible: activates its tab if docked, otherwise adds it to
 * the largest stack.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_open_panel (LrgGuiDock  *self,
                              const gchar *panel);

/**
 * lrg_gui_dock_close_panel:
 * @self: an #LrgGuiDock
 * @panel: a panel id
 *
 * Removes @panel from the layout (empty stacks collapse).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_close_panel (LrgGuiDock  *self,
                               const gchar *panel);

/**
 * lrg_gui_dock_has_panel:
 * @self: an #LrgGuiDock
 * @panel: a panel id
 *
 * Returns: %TRUE if @panel is in the layout (shown or behind another tab)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_dock_has_panel (LrgGuiDock  *self,
                                 const gchar *panel);

/**
 * lrg_gui_dock_is_panel_visible:
 * @self: an #LrgGuiDock
 * @panel: a panel id
 *
 * Returns: %TRUE if @panel is the active tab of its stack
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_dock_is_panel_visible (LrgGuiDock  *self,
                                        const gchar *panel);

/**
 * lrg_gui_dock_get_panel_rect:
 * @self: an #LrgGuiDock
 * @panel: a panel id
 * @rect: (out caller-allocates): the panel's content rectangle as of the
 *   last lrg_gui_dock_begin()
 *
 * Returns: %TRUE if the panel is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_dock_get_panel_rect (LrgGuiDock  *self,
                                      const gchar *panel,
                                      LrgGuiRect  *rect);

/**
 * lrg_gui_dock_set_closable:
 * @self: an #LrgGuiDock
 * @panel: a panel id
 * @closable: whether the tab shows a close button (default %TRUE)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_set_closable (LrgGuiDock  *self,
                                const gchar *panel,
                                gboolean     closable);

/**
 * lrg_gui_dock_set_panel_flags:
 * @self: an #LrgGuiDock
 * @panel: a panel id
 * @flags: region flags for the panel's content (default: scrolling with
 *   background and padding)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_set_panel_flags (LrgGuiDock        *self,
                                   const gchar       *panel,
                                   LrgGuiRegionFlags  flags);

/**
 * lrg_gui_dock_begin:
 * @self: an #LrgGuiDock
 * @ctx: the GUI context
 * @rect: the area the dock fills
 *
 * Lays out the tree for this frame and draws splitters and tab strips.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_begin (LrgGuiDock       *self,
                         LrgGuiContext    *ctx,
                         const LrgGuiRect *rect);

/**
 * lrg_gui_dock_begin_panel:
 * @self: an #LrgGuiDock
 * @ctx: the GUI context
 * @panel: the panel id
 * @title: tab title ("###" rules apply for dynamic titles)
 *
 * Opens the panel's content region if it is visible. Call
 * lrg_gui_dock_end_panel() when this returns %TRUE.
 *
 * Returns: %TRUE if the panel's content should be built
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_dock_begin_panel (LrgGuiDock    *self,
                                   LrgGuiContext *ctx,
                                   const gchar   *panel,
                                   const gchar   *title);

/**
 * lrg_gui_dock_end_panel:
 * @self: an #LrgGuiDock
 * @ctx: the GUI context
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_end_panel (LrgGuiDock    *self,
                             LrgGuiContext *ctx);

/**
 * lrg_gui_dock_end:
 * @self: an #LrgGuiDock
 * @ctx: the GUI context
 *
 * Finishes the frame: drop-zone overlays and tab drag completion.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dock_end (LrgGuiDock    *self,
                       LrgGuiContext *ctx);

G_END_DECLS
