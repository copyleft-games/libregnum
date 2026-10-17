/* lrg-radial-menu.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRadialMenu - the model of a radial selector (a "wheel"): items
 * around a circle, one sector each, picked by the direction the pointer
 * or a stick points from the centre.  The menu says which item a
 * direction selects and where each item sits; drawing is the game's.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_RADIAL_MENU_MAX_ITEMS:
 *
 * The most items a radial menu holds.
 */
#define LRG_RADIAL_MENU_MAX_ITEMS (16)

/**
 * LRG_RADIAL_MENU_DEFAULT_DEAD_ZONE:
 *
 * How far from the centre (in the caller's units) a direction must
 * reach before it selects anything, by default.
 */
#define LRG_RADIAL_MENU_DEFAULT_DEAD_ZONE (8.0f)

#define LRG_TYPE_RADIAL_MENU (lrg_radial_menu_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgRadialMenu, lrg_radial_menu, LRG, RADIAL_MENU, GObject)
#pragma GCC visibility pop

/**
 * LrgRadialMenu:
 *
 * Items in clockwise order starting at #LrgRadialMenu:start-angle
 * (degrees clockwise from straight up; the first item's sector is
 * centred there).  Directions are screen vectors: +X right, +Y down.
 *
 * Signals: #LrgRadialMenu::selection-changed, #LrgRadialMenu::activated.
 */

/**
 * LrgRadialMenu::selection-changed:
 * @self: the menu
 * @index: the selected item, -1 for none
 */

/**
 * LrgRadialMenu::activated:
 * @self: the menu
 * @index: the item chosen
 * @item_id: its id
 */

/**
 * lrg_radial_menu_new:
 *
 * Returns: (transfer full): an empty, closed menu
 */
LRG_AVAILABLE_IN_ALL
LrgRadialMenu *
lrg_radial_menu_new (void);

/**
 * lrg_radial_menu_add_item:
 * @self: an #LrgRadialMenu
 * @item_id: the item's id (non-empty, unique)
 * @label: (nullable): its label, %NULL for the id
 *
 * Returns: the item's index, -1 for a duplicate, an empty id or a full
 *   menu
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radial_menu_add_item (LrgRadialMenu *self,
                          const gchar   *item_id,
                          const gchar   *label);

/**
 * lrg_radial_menu_get_count:
 * @self: an #LrgRadialMenu
 *
 * Returns: the items
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_radial_menu_get_count (LrgRadialMenu *self);

/**
 * lrg_radial_menu_get_item_id:
 * @self: an #LrgRadialMenu
 * @index: an item
 *
 * Returns: (transfer none) (nullable): the id, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_radial_menu_get_item_id (LrgRadialMenu *self,
                             guint          index);

/**
 * lrg_radial_menu_get_item_label:
 * @self: an #LrgRadialMenu
 * @index: an item
 *
 * Returns: (transfer none) (nullable): the label, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_radial_menu_get_item_label (LrgRadialMenu *self,
                                guint          index);

/**
 * lrg_radial_menu_find:
 * @self: an #LrgRadialMenu
 * @item_id: an id
 *
 * Returns: the item's index, -1 when unknown
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radial_menu_find (LrgRadialMenu *self,
                      const gchar   *item_id);

/**
 * lrg_radial_menu_set_start_angle:
 * @self: an #LrgRadialMenu
 * @degrees: where the first item's sector is centred, clockwise from up
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radial_menu_set_start_angle (LrgRadialMenu *self,
                                 gfloat         degrees);

/**
 * lrg_radial_menu_get_start_angle:
 * @self: an #LrgRadialMenu
 *
 * Returns: the start angle, degrees in [0, 360)
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_radial_menu_get_start_angle (LrgRadialMenu *self);

/**
 * lrg_radial_menu_set_dead_zone:
 * @self: an #LrgRadialMenu
 * @radius: directions shorter than this select nothing (negative is 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radial_menu_set_dead_zone (LrgRadialMenu *self,
                               gfloat         radius);

/**
 * lrg_radial_menu_get_dead_zone:
 * @self: an #LrgRadialMenu
 *
 * Returns: the dead zone's radius
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_radial_menu_get_dead_zone (LrgRadialMenu *self);

/**
 * lrg_radial_menu_index_at:
 * @self: an #LrgRadialMenu
 * @dx: the direction's X (+ right)
 * @dy: the direction's Y (+ down)
 *
 * The item whose sector the direction points into; changes nothing.
 *
 * Returns: the index, -1 inside the dead zone or without items
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radial_menu_index_at (LrgRadialMenu *self,
                          gfloat         dx,
                          gfloat         dy);

/**
 * lrg_radial_menu_open:
 * @self: an #LrgRadialMenu
 * @selected: the item selected at first, -1 for none
 *
 * Opens the menu.  An open menu is opened again with the new selection.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radial_menu_open (LrgRadialMenu *self,
                      gint           selected);

/**
 * lrg_radial_menu_is_open:
 * @self: an #LrgRadialMenu
 *
 * Returns: %TRUE while open
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_radial_menu_is_open (LrgRadialMenu *self);

/**
 * lrg_radial_menu_point:
 * @self: an #LrgRadialMenu
 * @dx: the direction's X from the centre
 * @dy: the direction's Y from the centre
 *
 * Selects the item the direction points at while the menu is open; a
 * direction inside the dead zone keeps the selection (a stick let go
 * keeps the choice).
 *
 * Returns: the selection after the call, -1 for none or a closed menu
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radial_menu_point (LrgRadialMenu *self,
                       gfloat         dx,
                       gfloat         dy);

/**
 * lrg_radial_menu_step:
 * @self: an #LrgRadialMenu
 * @steps: items to move clockwise (negative: counter-clockwise)
 *
 * Moves the selection around the wheel (keys, the mouse wheel); with no
 * selection the first step lands on item 0.
 *
 * Returns: the selection after the call, -1 for a closed or empty menu
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radial_menu_step (LrgRadialMenu *self,
                      gint           steps);

/**
 * lrg_radial_menu_get_selected:
 * @self: an #LrgRadialMenu
 *
 * Returns: the selected item, -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radial_menu_get_selected (LrgRadialMenu *self);

/**
 * lrg_radial_menu_close:
 * @self: an #LrgRadialMenu
 * @activate: %TRUE to choose the selected item
 *
 * Closes the menu; with @activate and a selection
 * #LrgRadialMenu::activated fires.
 *
 * Returns: the item chosen, -1 when none was (cancelled, nothing
 *   selected, already closed)
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radial_menu_close (LrgRadialMenu *self,
                       gboolean       activate);

/**
 * lrg_radial_menu_get_item_position:
 * @self: an #LrgRadialMenu
 * @index: an item
 * @radius: the circle's radius
 * @out_x: (out): the item's centre, X from the menu's centre
 * @out_y: (out): the item's centre, Y from the menu's centre (+ down)
 *
 * Where an item sits on a circle of @radius: its sector's middle.
 *
 * Returns: %TRUE for an index in range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_radial_menu_get_item_position (LrgRadialMenu *self,
                                   guint          index,
                                   gfloat         radius,
                                   gfloat        *out_x,
                                   gfloat        *out_y);

G_END_DECLS
