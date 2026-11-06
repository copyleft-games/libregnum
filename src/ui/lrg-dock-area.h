/* lrg-dock-area.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDockArea: a retained dock of titled panels arranged in a tree of
 * resizable splits and tab stacks, rearranged by dragging tabs onto a
 * drop compass, with floating panels and YAML layout persistence.
 *
 * LrgDockPanel: one dockable panel (an id, a title, an icon, one
 * content child).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgDockPosition
 * ========================================================================== */

/**
 * LrgDockPosition:
 * @LRG_DOCK_POSITION_LEFT: to the left of the reference (a new split)
 * @LRG_DOCK_POSITION_RIGHT: to the right of the reference
 * @LRG_DOCK_POSITION_TOP: above the reference
 * @LRG_DOCK_POSITION_BOTTOM: below the reference
 * @LRG_DOCK_POSITION_CENTER: as a new tab in the reference's stack
 *
 * Where lrg_dock_area_add_panel() and lrg_dock_area_move_panel() put
 * a panel relative to another panel's tab stack, or to the whole area
 * when there is no reference panel. Positions are physical: LEFT is
 * the left side in right-to-left layouts too.
 */
typedef enum
{
	LRG_DOCK_POSITION_LEFT,
	LRG_DOCK_POSITION_RIGHT,
	LRG_DOCK_POSITION_TOP,
	LRG_DOCK_POSITION_BOTTOM,
	LRG_DOCK_POSITION_CENTER
} LrgDockPosition;

/**
 * lrg_dock_position_get_type:
 *
 * Returns: the #GType of #LrgDockPosition
 */
LRG_AVAILABLE_IN_ALL
GType lrg_dock_position_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DOCK_POSITION (lrg_dock_position_get_type ())

/* ==========================================================================
 * LrgDockPanel
 * ========================================================================== */

#define LRG_TYPE_DOCK_PANEL (lrg_dock_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDockPanel, lrg_dock_panel, LRG, DOCK_PANEL, LrgContainer)

/**
 * lrg_dock_panel_new:
 * @id: (nullable): stable identifier used by layout persistence
 * @title: (nullable): text shown on the panel's tab
 *
 * Creates a closable panel without content. Add the content with
 * lrg_dock_panel_set_child() (or lrg_container_add_child()).
 *
 * Returns: (transfer full): a new #LrgDockPanel
 */
LRG_AVAILABLE_IN_ALL
LrgDockPanel * lrg_dock_panel_new (const gchar *id,
                                   const gchar *title);

/**
 * lrg_dock_panel_get_id:
 * @self: an #LrgDockPanel
 *
 * Returns: (nullable): the panel's identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dock_panel_get_id (LrgDockPanel *self);

/**
 * lrg_dock_panel_set_id:
 * @self: an #LrgDockPanel
 * @id: (nullable): stable identifier, unique within one dock area
 *
 * Sets the identifier lrg_dock_area_save_layout() writes and
 * lrg_dock_area_load_layout() matches panels by.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_panel_set_id (LrgDockPanel *self,
                            const gchar  *id);

/**
 * lrg_dock_panel_get_title:
 * @self: an #LrgDockPanel
 *
 * Returns: (nullable): the tab text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dock_panel_get_title (LrgDockPanel *self);

/**
 * lrg_dock_panel_set_title:
 * @self: an #LrgDockPanel
 * @title: (nullable): text shown on the tab and floating title bar
 *
 * Sets the panel's title.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_panel_set_title (LrgDockPanel *self,
                               const gchar  *title);

/**
 * lrg_dock_panel_get_icon:
 * @self: an #LrgDockPanel
 *
 * Returns: (nullable): the icon nick (see lrg_paint_icon_from_name())
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dock_panel_get_icon (LrgDockPanel *self);

/**
 * lrg_dock_panel_set_icon:
 * @self: an #LrgDockPanel
 * @icon: (nullable): a vector icon nick such as "folder" or "gear",
 *   %NULL for none
 *
 * Sets the icon drawn before the title.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_panel_set_icon (LrgDockPanel *self,
                              const gchar  *icon);

/**
 * lrg_dock_panel_get_closable:
 * @self: an #LrgDockPanel
 *
 * Returns: %TRUE if the panel's tab has a close button
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_panel_get_closable (LrgDockPanel *self);

/**
 * lrg_dock_panel_set_closable:
 * @self: an #LrgDockPanel
 * @closable: whether the user can close the panel (close button,
 *   middle click)
 *
 * Sets whether the panel can be closed by the user.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_panel_set_closable (LrgDockPanel *self,
                                  gboolean      closable);

/**
 * lrg_dock_panel_get_child:
 * @self: an #LrgDockPanel
 *
 * Returns: (transfer none) (nullable): the content widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_dock_panel_get_child (LrgDockPanel *self);

/**
 * lrg_dock_panel_set_child:
 * @self: an #LrgDockPanel
 * @child: (nullable): the content widget; replaces any previous one
 *
 * Sets the panel's content. The panel takes a reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_panel_set_child (LrgDockPanel *self,
                               LrgWidget    *child);

/* ==========================================================================
 * LrgDockArea
 * ========================================================================== */

#define LRG_TYPE_DOCK_AREA (lrg_dock_area_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDockArea, lrg_dock_area, LRG, DOCK_AREA, LrgContainer)

/**
 * lrg_dock_panel_get_dock_area:
 * @self: an #LrgDockPanel
 *
 * The dock area the panel belongs to, docked or floating.
 *
 * Returns: (transfer none) (nullable): the owning #LrgDockArea
 */
LRG_AVAILABLE_IN_ALL
LrgDockArea * lrg_dock_panel_get_dock_area (LrgDockPanel *self);

/**
 * lrg_dock_area_new:
 *
 * Creates an empty dock area.
 *
 * Returns: (transfer full): a new #LrgDockArea
 */
LRG_AVAILABLE_IN_ALL
LrgDockArea * lrg_dock_area_new (void);

/**
 * lrg_dock_area_add_panel:
 * @self: an #LrgDockArea
 * @panel: (transfer none): a panel without a parent
 * @relative_to: (nullable): a docked panel of @self to place @panel
 *   against, or %NULL for the whole area
 * @position: where to put @panel
 *
 * Docks @panel. With @relative_to, LEFT / RIGHT / TOP / BOTTOM split
 * that panel's tab stack in half and CENTER adds a tab to it. Without
 * it, the sides split the whole area (the new panel gets a quarter)
 * and CENTER adds a tab to the stack of the active panel (or the first
 * stack; the first panel of an empty area fills it). The new panel
 * becomes the active tab of its stack.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_add_panel (LrgDockArea     *self,
                              LrgDockPanel    *panel,
                              LrgDockPanel    *relative_to,
                              LrgDockPosition  position);

/**
 * lrg_dock_area_remove_panel:
 * @self: an #LrgDockArea
 * @panel: a panel of @self, docked or floating
 *
 * Removes @panel without asking (no #LrgDockArea::close-request, no
 * #LrgDockArea::panel-closed). A tab stack left empty collapses and
 * its split is replaced by the remaining side.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_remove_panel (LrgDockArea  *self,
                                 LrgDockPanel *panel);

/**
 * lrg_dock_area_close_panel:
 * @self: an #LrgDockArea
 * @panel: a panel of @self
 *
 * Closes @panel the way its close button does: emits
 * #LrgDockArea::close-request (a handler returning %TRUE vetoes),
 * then removes it and emits #LrgDockArea::panel-closed.
 *
 * Returns: %TRUE if the panel was closed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_close_panel (LrgDockArea  *self,
                                    LrgDockPanel *panel);

/**
 * lrg_dock_area_move_panel:
 * @self: an #LrgDockArea
 * @panel: a panel of @self, docked or floating
 * @relative_to: (nullable): reference panel, as for
 *   lrg_dock_area_add_panel()
 * @position: where to put @panel
 *
 * Moves @panel to a new place in the tree, exactly as dropping its
 * dragged tab on the matching compass target would.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_move_panel (LrgDockArea     *self,
                               LrgDockPanel    *panel,
                               LrgDockPanel    *relative_to,
                               LrgDockPosition  position);

/**
 * lrg_dock_area_float_panel:
 * @self: an #LrgDockArea
 * @panel: a panel of @self
 * @x: left edge of the floating frame, in canvas units
 * @y: top edge of the floating frame
 * @width: frame width
 * @height: frame height (title bar included)
 *
 * Detaches @panel into a floating frame in the canvas overlay layer.
 * Needs the area to be on an #LrgCanvas and #LrgDockArea:allow-floating.
 * An already floating panel is moved and resized.
 *
 * Returns: %TRUE if the panel floats
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_float_panel (LrgDockArea  *self,
                                    LrgDockPanel *panel,
                                    gfloat        x,
                                    gfloat        y,
                                    gfloat        width,
                                    gfloat        height);

/**
 * lrg_dock_area_is_panel_floating:
 * @self: an #LrgDockArea
 * @panel: a panel
 *
 * Returns: %TRUE if @panel is a floating panel of @self
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_is_panel_floating (LrgDockArea  *self,
                                          LrgDockPanel *panel);

/**
 * lrg_dock_area_present_panel:
 * @self: an #LrgDockArea
 * @panel: a panel of @self
 *
 * Brings @panel forward: makes it the active tab of its stack (or
 * raises its floating frame) and the area's active panel, emitting
 * #LrgDockArea::panel-activated when the visible tab or the active
 * panel changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_present_panel (LrgDockArea  *self,
                                  LrgDockPanel *panel);

/**
 * lrg_dock_area_get_active_panel:
 * @self: an #LrgDockArea
 *
 * Returns: (transfer none) (nullable): the most recently activated
 *   panel
 */
LRG_AVAILABLE_IN_ALL
LrgDockPanel * lrg_dock_area_get_active_panel (LrgDockArea *self);

/**
 * lrg_dock_area_get_panels:
 * @self: an #LrgDockArea
 *
 * Every panel of the area: docked ones in tree order (depth first,
 * tabs left to right), then floating ones.
 *
 * Returns: (transfer container) (element-type LrgDockPanel): the panels
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_dock_area_get_panels (LrgDockArea *self);

/**
 * lrg_dock_area_get_panel:
 * @self: an #LrgDockArea
 * @id: a panel identifier
 *
 * Returns: (transfer none) (nullable): the panel with @id
 */
LRG_AVAILABLE_IN_ALL
LrgDockPanel * lrg_dock_area_get_panel (LrgDockArea *self,
                                        const gchar *id);

/**
 * lrg_dock_area_get_stack_panels:
 * @self: an #LrgDockArea
 * @panel: a docked panel
 *
 * Returns: (transfer container) (element-type LrgDockPanel): the panels
 *   sharing @panel's tab stack, in tab order (empty when floating)
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_dock_area_get_stack_panels (LrgDockArea  *self,
                                        LrgDockPanel *panel);

/**
 * lrg_dock_area_get_stack_rect:
 * @self: an #LrgDockArea
 * @panel: a docked panel
 * @x: (out) (optional): world left edge of its tab stack
 * @y: (out) (optional): world top edge (the tab strip's top)
 * @width: (out) (optional): stack width
 * @height: (out) (optional): stack height
 *
 * Returns: %TRUE if @panel is docked in @self
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_get_stack_rect (LrgDockArea  *self,
                                       LrgDockPanel *panel,
                                       gfloat       *x,
                                       gfloat       *y,
                                       gfloat       *width,
                                       gfloat       *height);

/**
 * lrg_dock_area_get_tab_rect:
 * @self: an #LrgDockArea
 * @panel: a docked panel
 * @x: (out) (optional): world left edge of the panel's tab
 * @y: (out) (optional): world top edge
 * @width: (out) (optional): tab width
 * @height: (out) (optional): tab height
 *
 * Returns: %TRUE if @panel is docked in @self
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_get_tab_rect (LrgDockArea  *self,
                                     LrgDockPanel *panel,
                                     gfloat       *x,
                                     gfloat       *y,
                                     gfloat       *width,
                                     gfloat       *height);

/**
 * lrg_dock_area_get_panel_share:
 * @self: an #LrgDockArea
 * @panel: a docked panel
 *
 * Returns: the share (0..1) of its parent split that @panel's stack
 *   takes, or 1 when the stack is the whole tree
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_dock_area_get_panel_share (LrgDockArea  *self,
                                      LrgDockPanel *panel);

/**
 * lrg_dock_area_set_panel_share:
 * @self: an #LrgDockArea
 * @panel: a docked panel
 * @share: wanted share (0..1) of the parent split for @panel's stack
 *
 * Moves the divider of the split directly holding @panel's tab stack.
 * Minimum sizes still apply when laying out.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_set_panel_share (LrgDockArea  *self,
                                    LrgDockPanel *panel,
                                    gfloat        share);

/**
 * lrg_dock_area_describe:
 * @self: an #LrgDockArea
 *
 * A compact text form of the tree, for tests and debugging: splits
 * are "H(ratio,first,second)" (side by side) or "V(...)" (stacked),
 * tab stacks are "[id,id*]" with "*" on the active tab, floating
 * panels follow as "+float[id]". An empty area is "()".
 *
 * Returns: (transfer full): the description
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_dock_area_describe (LrgDockArea *self);

/**
 * lrg_dock_area_get_dragging:
 * @self: an #LrgDockArea
 *
 * Returns: %TRUE while a tab or floating frame is being dragged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_get_dragging (LrgDockArea *self);

/**
 * lrg_dock_area_save_layout:
 * @self: an #LrgDockArea
 *
 * Serialises the arrangement (tree, split ratios, tab order, active
 * tabs, floating frames) as YAML, referring to panels by id.
 *
 * Returns: (transfer full): the YAML document
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_dock_area_save_layout (LrgDockArea *self);

/**
 * lrg_dock_area_load_layout:
 * @self: an #LrgDockArea
 * @yaml: a document from lrg_dock_area_save_layout()
 * @error: return location for a #GError
 *
 * Rearranges the area's existing panels by @yaml. Panels are matched
 * by id: ids the area does not have are skipped (stacks left empty
 * collapse), panels the layout does not mention are added as tabs to
 * the first stack. On a parse error nothing changes.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_load_layout (LrgDockArea  *self,
                                    const gchar  *yaml,
                                    GError      **error);

/**
 * lrg_dock_area_get_divider_size:
 * @self: an #LrgDockArea
 *
 * Returns: the thickness of the dividers between stacks
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_dock_area_get_divider_size (LrgDockArea *self);

/**
 * lrg_dock_area_set_divider_size:
 * @self: an #LrgDockArea
 * @size: divider thickness (also its grab zone), at least 1
 *
 * Sets the gap between neighbouring stacks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_set_divider_size (LrgDockArea *self,
                                     gfloat       size);

/**
 * lrg_dock_area_get_tab_height:
 * @self: an #LrgDockArea
 *
 * Returns: the tab strip height set, 0 for the theme's control height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_dock_area_get_tab_height (LrgDockArea *self);

/**
 * lrg_dock_area_set_tab_height:
 * @self: an #LrgDockArea
 * @height: height of every tab strip, 0 for the theme's control height
 *
 * Sets the tab strip height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_set_tab_height (LrgDockArea *self,
                                   gfloat       height);

/**
 * lrg_dock_area_get_min_panel_size:
 * @self: an #LrgDockArea
 *
 * Returns: the smallest width and content height of a tab stack
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_dock_area_get_min_panel_size (LrgDockArea *self);

/**
 * lrg_dock_area_set_min_panel_size:
 * @self: an #LrgDockArea
 * @size: smallest stack width and content height; a panel's own
 *   minimum size (lrg_widget_set_min_size()) raises it for its stack
 *
 * Sets the floor dividers clamp against.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_set_min_panel_size (LrgDockArea *self,
                                       gfloat       size);

/**
 * lrg_dock_area_get_allow_floating:
 * @self: an #LrgDockArea
 *
 * Returns: %TRUE if panels may be torn off into floating frames
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dock_area_get_allow_floating (LrgDockArea *self);

/**
 * lrg_dock_area_set_allow_floating:
 * @self: an #LrgDockArea
 * @allow: whether dropping a tab away from every target floats it
 *
 * Without floating such a drop is cancelled instead.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dock_area_set_allow_floating (LrgDockArea *self,
                                       gboolean     allow);

G_END_DECLS
