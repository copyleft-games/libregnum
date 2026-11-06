/* lrg-menu.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMenu: a popup menu built from a GMenuModel.
 *
 * A menu is shown on the canvas popup layer, either anchored to a
 * widget (a button, a menu bar title) or at a point (a context menu).
 * Its entries come from a #GMenuModel: items carry a label, a GIO
 * action name and target, an accelerator and an icon; sections become
 * separators (with an optional header) and submenu links open nested
 * menus beside their item. Activating an item runs its action through
 * lrg_widget_activate_action() on the widget the menu was opened for.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgMenuEntryKind
 * ========================================================================== */

/**
 * LrgMenuEntryKind:
 * @LRG_MENU_ENTRY_ITEM: a plain item that runs an action
 * @LRG_MENU_ENTRY_SUBMENU: an item that opens a nested menu
 * @LRG_MENU_ENTRY_SEPARATOR: a thin line between two sections
 * @LRG_MENU_ENTRY_HEADER: the muted label of a labelled section
 *
 * What one row of an #LrgMenu is. Separators and headers can never be
 * highlighted or activated.
 */
typedef enum
{
	LRG_MENU_ENTRY_ITEM,
	LRG_MENU_ENTRY_SUBMENU,
	LRG_MENU_ENTRY_SEPARATOR,
	LRG_MENU_ENTRY_HEADER
} LrgMenuEntryKind;

/**
 * lrg_menu_entry_kind_get_type:
 *
 * Returns: the #GType of #LrgMenuEntryKind
 */
LRG_AVAILABLE_IN_ALL
GType lrg_menu_entry_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MENU_ENTRY_KIND (lrg_menu_entry_kind_get_type ())

/* ==========================================================================
 * LrgMenu
 * ========================================================================== */

#define LRG_TYPE_MENU (lrg_menu_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMenu, lrg_menu, LRG, MENU, LrgWidget)

/**
 * lrg_menu_new:
 *
 * Creates an empty menu. Give it entries with lrg_menu_set_model().
 *
 * Returns: (transfer full): a new #LrgMenu
 */
LRG_AVAILABLE_IN_ALL
LrgMenu * lrg_menu_new (void);

/**
 * lrg_menu_new_from_model:
 * @model: (nullable): the menu description
 *
 * Creates a menu showing @model. The menu follows later changes to the
 * model (items-changed).
 *
 * Returns: (transfer full): a new #LrgMenu
 */
LRG_AVAILABLE_IN_ALL
LrgMenu * lrg_menu_new_from_model (GMenuModel *model);

/**
 * lrg_menu_get_model:
 * @self: an #LrgMenu
 *
 * Returns: (transfer none) (nullable): the menu description
 */
LRG_AVAILABLE_IN_ALL
GMenuModel * lrg_menu_get_model (LrgMenu *self);

/**
 * lrg_menu_set_model:
 * @self: an #LrgMenu
 * @model: (nullable): the menu description
 *
 * Replaces the entries. An open submenu is closed; an open menu is
 * resized to the new entries.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_set_model (LrgMenu    *self,
                         GMenuModel *model);

/**
 * lrg_menu_popup_at_widget:
 * @self: an #LrgMenu
 * @widget: the widget to open next to; actions are looked up from it
 * @placement: where to open relative to @widget (flips when it does
 *   not fit)
 *
 * Opens the menu on the canvas popup layer next to @widget (which
 * must be on a canvas) and gives it keyboard focus. Re-opening an
 * open menu moves it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_popup_at_widget (LrgMenu           *self,
                               LrgWidget         *widget,
                               LrgPopupPlacement  placement);

/**
 * lrg_menu_popup_at_point:
 * @self: an #LrgMenu
 * @owner: a widget on the canvas; actions are looked up from it
 * @x: point in canvas coordinates (usually the pointer)
 * @y: point in canvas coordinates
 *
 * Opens the menu as a context menu with its top-left corner at the
 * point (top-right in right-to-left layouts). It flips to the other
 * side of the point horizontally and vertically when it would leave
 * the canvas.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_popup_at_point (LrgMenu   *self,
                              LrgWidget *owner,
                              gfloat     x,
                              gfloat     y);

/**
 * lrg_menu_popup_at_rect:
 * @self: an #LrgMenu
 * @owner: a widget on the canvas; actions are looked up from it and
 *   it gets %LRG_WIDGET_STATE_OPEN while the menu is open
 * @x: anchor rectangle left, in canvas coordinates
 * @y: anchor rectangle top
 * @width: anchor rectangle width
 * @height: anchor rectangle height
 * @placement: where to open relative to the rectangle
 *
 * Opens the menu next to an arbitrary rectangle, e.g. one title of a
 * menu bar or a cell of a custom widget. %LRG_POPUP_PLACEMENT_BELOW
 * and %LRG_POPUP_PLACEMENT_ABOVE align the menu's leading edge with
 * the rectangle's (its right edge in right-to-left layouts).
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_popup_at_rect (LrgMenu           *self,
                             LrgWidget         *owner,
                             gfloat             x,
                             gfloat             y,
                             gfloat             width,
                             gfloat             height,
                             LrgPopupPlacement  placement);

/**
 * lrg_menu_close:
 * @self: an #LrgMenu
 *
 * Closes the menu and its open submenus. It fades out as a ghost.
 * Does nothing when it is not open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_close (LrgMenu *self);

/**
 * lrg_menu_get_open:
 * @self: an #LrgMenu
 *
 * Returns: whether the menu is showing on a canvas
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_menu_get_open (LrgMenu *self);

/**
 * lrg_menu_get_owner:
 * @self: an #LrgMenu
 *
 * The widget the menu was opened for. Submenus report the owner of
 * their top-level menu.
 *
 * Returns: (transfer none) (nullable): the owner while open
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_menu_get_owner (LrgMenu *self);

/**
 * lrg_menu_get_parent_menu:
 * @self: an #LrgMenu
 *
 * Returns: (transfer none) (nullable): the menu this submenu was
 *   opened from, %NULL for a top-level menu
 */
LRG_AVAILABLE_IN_ALL
LrgMenu * lrg_menu_get_parent_menu (LrgMenu *self);

/**
 * lrg_menu_get_open_submenu:
 * @self: an #LrgMenu
 *
 * Returns: (transfer none) (nullable): the submenu currently open
 *   from this menu
 */
LRG_AVAILABLE_IN_ALL
LrgMenu * lrg_menu_get_open_submenu (LrgMenu *self);

/**
 * lrg_menu_get_n_entries:
 * @self: an #LrgMenu
 *
 * Returns: number of rows, separators and headers included
 */
LRG_AVAILABLE_IN_ALL
guint lrg_menu_get_n_entries (LrgMenu *self);

/**
 * lrg_menu_get_entry_kind:
 * @self: an #LrgMenu
 * @index: a row index
 *
 * Returns: what row @index is (%LRG_MENU_ENTRY_SEPARATOR when out of
 *   range)
 */
LRG_AVAILABLE_IN_ALL
LrgMenuEntryKind lrg_menu_get_entry_kind (LrgMenu *self,
                                          guint    index);

/**
 * lrg_menu_get_entry_label:
 * @self: an #LrgMenu
 * @index: a row index
 *
 * Returns: (nullable): the row's label with the mnemonic underscore
 *   removed, %NULL for separators
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_menu_get_entry_label (LrgMenu *self,
                                        guint    index);

/**
 * lrg_menu_get_entry_action:
 * @self: an #LrgMenu
 * @index: a row index
 *
 * Returns: (nullable): the row's detailed action name ("app.quit")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_menu_get_entry_action (LrgMenu *self,
                                         guint    index);

/**
 * lrg_menu_get_entry_enabled:
 * @self: an #LrgMenu
 * @index: a row index
 *
 * An item is enabled when its action exists and is enabled, or when it
 * has no action at all (a submenu). Separators and headers are never
 * enabled.
 *
 * Returns: whether row @index can be highlighted and activated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_menu_get_entry_enabled (LrgMenu *self,
                                     guint    index);

/**
 * lrg_menu_get_entry_rect:
 * @self: an #LrgMenu
 * @index: a row index
 * @x: (out) (optional): row left, world coordinates
 * @y: (out) (optional): row top
 * @width: (out) (optional): row width
 * @height: (out) (optional): row height
 *
 * Returns: %TRUE if @index is a row
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_menu_get_entry_rect (LrgMenu *self,
                                  guint    index,
                                  gfloat  *x,
                                  gfloat  *y,
                                  gfloat  *width,
                                  gfloat  *height);

/**
 * lrg_menu_get_item_rect:
 * @self: an #LrgMenu
 * @index: a row index (as for lrg_menu_get_entry_kind())
 * @x: (out) (optional): row left, canvas (world) coordinates
 * @y: (out) (optional): row top, canvas coordinates
 * @width: (out) (optional): row width
 * @height: (out) (optional): row height
 *
 * The rectangle of row @index in canvas coordinates, the same space as
 * lrg_menu_bar_get_title_rect() and #LrgCanvasInput pointer positions,
 * so a test or an IPC client can click it. Identical to
 * lrg_menu_get_entry_rect(); the name matches the editor's `ui.state`
 * vocabulary. Meaningful while the menu is open (laid out).
 *
 * Returns: %TRUE if @index is a row
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_menu_get_item_rect (LrgMenu *self,
                                 guint    index,
                                 gfloat  *x,
                                 gfloat  *y,
                                 gfloat  *width,
                                 gfloat  *height);

/**
 * lrg_menu_get_row_height:
 * @self: an #LrgMenu
 *
 * The height of an item row: the theme's ROW_HEIGHT metric. Headers are
 * 0.8 of it and separators a thin band, so use lrg_menu_get_item_rect()
 * for exact positions.
 *
 * Returns: the height of an item row, in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_menu_get_row_height (LrgMenu *self);

/**
 * lrg_menu_get_highlighted:
 * @self: an #LrgMenu
 *
 * Returns: the highlighted row, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_menu_get_highlighted (LrgMenu *self);

/**
 * lrg_menu_set_highlighted:
 * @self: an #LrgMenu
 * @index: a row to highlight, or -1 for none
 *
 * Moves the highlight. Rows that cannot be highlighted (separators,
 * headers, disabled items) clear it instead. Does not open submenus.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_set_highlighted (LrgMenu *self,
                               gint     index);

/**
 * lrg_menu_activate_entry:
 * @self: an #LrgMenu
 * @index: a row index
 *
 * Does what clicking row @index does: an item closes the whole menu
 * chain, runs its action with its target on the owner and emits
 * #LrgMenu::activated; a submenu item opens its submenu.
 *
 * Returns: %TRUE if the row was enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_menu_activate_entry (LrgMenu *self,
                                  guint    index);

/**
 * lrg_menu_get_submenu_delay:
 * @self: an #LrgMenu
 *
 * Returns: seconds the pointer rests on a submenu item before it opens
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_menu_get_submenu_delay (LrgMenu *self);

/**
 * lrg_menu_set_submenu_delay:
 * @self: an #LrgMenu
 * @delay: seconds (0 opens at once); submenus inherit it
 *
 * Sets how long the pointer must rest on a submenu item before the
 * submenu opens, and how long it may cross other items on its way into
 * an open submenu.
 */
LRG_AVAILABLE_IN_ALL
void lrg_menu_set_submenu_delay (LrgMenu *self,
                                 gfloat   delay);

/**
 * lrg_menu_forward_pointer_event:
 * @self: an open top-level #LrgMenu
 * @event: a pointer event received by another widget
 *
 * Advanced: hands a pointer event another widget received to the
 * menu chain. Moves and releases over @self or one of its open
 * submenus are delivered there (highlight, submenu delay, activation
 * on release).
 *
 * Not needed for press-drag-release: when the widget holding the
 * pointer capture opens a menu during the press, the canvas itself
 * sends moves and the release to the menu chain while the pointer is
 * over it (see lrg_widget_set_context_menu() for menus with no code at
 * all). Use this only when a widget routes or synthesises pointer
 * events for a menu itself.
 *
 * Returns: %TRUE if a menu under the pointer took the event
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_menu_forward_pointer_event (LrgMenu          *self,
                                         const LrgUIEvent *event);

/**
 * lrg_menu_split_mnemonic:
 * @text: a label such as "_File" or "Save __As"
 * @mnemonic: (out) (optional): byte offset of the mnemonic character
 *   in the returned string, or -1
 *
 * Removes mnemonic markers: a single underscore marks the next
 * character, a double underscore stands for a literal one.
 *
 * Returns: (transfer full): the display text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_menu_split_mnemonic (const gchar *text,
                                 gint        *mnemonic);

G_END_DECLS
