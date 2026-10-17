/* lrg-action-bar.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgActionBar - one bar of buttons, World of Warcraft style: a number
 * of slots on each of several pages (the main bar pages through them),
 * each slot holding an #LrgActionSlot or nothing.  The bar keeps the
 * contents and the page; an #LrgActionSlotProvider says what they look
 * like and uses them.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "lrg-action-slot.h"
#include "lrg-action-slot-provider.h"

G_BEGIN_DECLS

/**
 * LRG_ACTION_BAR_MAX_SLOTS:
 *
 * Slots a page may have.
 */
#define LRG_ACTION_BAR_MAX_SLOTS (48)

/**
 * LRG_ACTION_BAR_MAX_PAGES:
 *
 * Pages a bar may have.
 */
#define LRG_ACTION_BAR_MAX_PAGES (12)

/**
 * LRG_ACTION_BAR_VARIANT_TYPE:
 *
 * The #GVariant type of lrg_action_bar_to_variant(): slots per page,
 * pages, the current page, visible, and the filled slots as (page,
 * index, slot).
 */
#define LRG_ACTION_BAR_VARIANT_TYPE "(uuuba(uu(ssss)))"

#define LRG_TYPE_ACTION_BAR (lrg_action_bar_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgActionBar, lrg_action_bar, LRG, ACTION_BAR, GObject)
#pragma GCC visibility pop

/**
 * LrgActionBar:
 *
 * Properties: #LrgActionBar:name (construct only),
 * #LrgActionBar:slots (per page), #LrgActionBar:pages,
 * #LrgActionBar:page (the current one, 0-based), #LrgActionBar:visible.
 * Slot indices are 0-based; the functions without a page act on the
 * current page.
 */

/**
 * LrgActionBar::slot-changed:
 * @self: the bar
 * @page: the page
 * @index: the slot
 *
 * A slot's content was set, cleared or swapped.
 */

/**
 * lrg_action_bar_new:
 * @name: (nullable): a name ("main", "bar-2")
 * @slots: slots per page (1 .. %LRG_ACTION_BAR_MAX_SLOTS)
 * @pages: pages (1 .. %LRG_ACTION_BAR_MAX_PAGES)
 *
 * Returns: (transfer full): an empty bar on page 0, visible
 */
LRG_AVAILABLE_IN_ALL
LrgActionBar *
lrg_action_bar_new (const gchar *name,
                    guint        slots,
                    guint        pages);

/**
 * lrg_action_bar_get_name:
 * @self: an #LrgActionBar
 *
 * Returns: (transfer none) (nullable): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_action_bar_get_name (LrgActionBar *self);

/**
 * lrg_action_bar_get_slot_count:
 * @self: an #LrgActionBar
 *
 * Returns: slots per page
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_action_bar_get_slot_count (LrgActionBar *self);

/**
 * lrg_action_bar_get_page_count:
 * @self: an #LrgActionBar
 *
 * Returns: pages
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_action_bar_get_page_count (LrgActionBar *self);

/**
 * lrg_action_bar_resize:
 * @self: an #LrgActionBar
 * @slots: slots per page (clamped)
 * @pages: pages (clamped)
 *
 * Changes the layout; contents outside the new layout are dropped, the
 * page is clamped.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_action_bar_resize (LrgActionBar *self,
                       guint         slots,
                       guint         pages);

/* --- pages --- */

/**
 * lrg_action_bar_get_page:
 * @self: an #LrgActionBar
 *
 * Returns: the current page
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_action_bar_get_page (LrgActionBar *self);

/**
 * lrg_action_bar_set_page:
 * @self: an #LrgActionBar
 * @page: a page (clamped)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_action_bar_set_page (LrgActionBar *self,
                         guint         page);

/**
 * lrg_action_bar_next_page:
 * @self: an #LrgActionBar
 *
 * Turns to the next page, after the last back to the first.
 *
 * Returns: the new page
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_action_bar_next_page (LrgActionBar *self);

/**
 * lrg_action_bar_previous_page:
 * @self: an #LrgActionBar
 *
 * Turns to the previous page, before the first to the last.
 *
 * Returns: the new page
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_action_bar_previous_page (LrgActionBar *self);

/**
 * lrg_action_bar_get_visible:
 * @self: an #LrgActionBar
 *
 * Returns: whether the bar is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_get_visible (LrgActionBar *self);

/**
 * lrg_action_bar_set_visible:
 * @self: an #LrgActionBar
 * @visible: whether the bar is shown
 */
LRG_AVAILABLE_IN_ALL
void
lrg_action_bar_set_visible (LrgActionBar *self,
                            gboolean      visible);

/* --- slots --- */

/**
 * lrg_action_bar_get_slot_at:
 * @self: an #LrgActionBar
 * @page: a page
 * @index: a slot
 *
 * Returns: (transfer none) (nullable): the content, %NULL when empty or
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
LrgActionSlot *
lrg_action_bar_get_slot_at (LrgActionBar *self,
                            guint         page,
                            guint         index);

/**
 * lrg_action_bar_get_slot:
 * @self: an #LrgActionBar
 * @index: a slot of the current page
 *
 * Returns: (transfer none) (nullable): the content
 */
LRG_AVAILABLE_IN_ALL
LrgActionSlot *
lrg_action_bar_get_slot (LrgActionBar *self,
                         guint         index);

/**
 * lrg_action_bar_set_slot_at:
 * @self: an #LrgActionBar
 * @page: a page
 * @index: a slot
 * @slot: (nullable): the content, %NULL to clear
 *
 * Returns: %FALSE when @page or @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_set_slot_at (LrgActionBar  *self,
                            guint          page,
                            guint          index,
                            LrgActionSlot *slot);

/**
 * lrg_action_bar_set_slot:
 * @self: an #LrgActionBar
 * @index: a slot of the current page
 * @slot: (nullable): the content, %NULL to clear
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_set_slot (LrgActionBar  *self,
                         guint          index,
                         LrgActionSlot *slot);

/**
 * lrg_action_bar_swap_slots:
 * @self: an #LrgActionBar
 * @a: a slot of the current page
 * @b: another slot of the current page
 *
 * Exchanges two slots' contents (dragging one button onto another).
 *
 * Returns: %FALSE when an index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_swap_slots (LrgActionBar *self,
                           guint         a,
                           guint         b);

/**
 * lrg_action_bar_find:
 * @self: an #LrgActionBar
 * @kind: a kind
 * @id: an id
 * @out_page: (out) (optional): its page
 * @out_index: (out) (optional): its slot
 *
 * Finds the first slot holding the content, the current page first.
 *
 * Returns: %TRUE when found
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_find (LrgActionBar *self,
                     const gchar  *kind,
                     const gchar  *id,
                     guint        *out_page,
                     guint        *out_index);

/**
 * lrg_action_bar_clear:
 * @self: an #LrgActionBar
 *
 * Empties every slot of every page.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_action_bar_clear (LrgActionBar *self);

/* --- with a provider --- */

/**
 * lrg_action_bar_query:
 * @self: an #LrgActionBar
 * @index: a slot of the current page
 * @provider: the game
 * @state: (out caller-allocates): the slot's state
 *
 * Returns: %FALSE for an empty slot (@state reset)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_query (LrgActionBar          *self,
                      guint                  index,
                      LrgActionSlotProvider *provider,
                      LrgActionSlotState    *state);

/**
 * lrg_action_bar_activate:
 * @self: an #LrgActionBar
 * @index: a slot of the current page
 * @provider: the game
 * @error: return location for a #GError
 *
 * Uses the slot's content through @provider.  An empty slot does
 * nothing (%FALSE, no error).
 *
 * Returns: %TRUE when the content was used
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_activate (LrgActionBar           *self,
                         guint                   index,
                         LrgActionSlotProvider  *provider,
                         GError                **error);

/* --- saving --- */

/**
 * lrg_action_bar_to_variant:
 * @self: an #LrgActionBar
 *
 * Returns: (transfer floating): a %LRG_ACTION_BAR_VARIANT_TYPE value
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_action_bar_to_variant (LrgActionBar *self);

/**
 * lrg_action_bar_restore:
 * @self: an #LrgActionBar
 * @variant: a %LRG_ACTION_BAR_VARIANT_TYPE value
 * @error: return location for a #GError
 *
 * Restores the layout, page, visibility and contents.  A value of
 * another type, a layout out of range or a slot outside the layout is
 * refused with %G_IO_ERROR_INVALID_DATA and leaves the bar unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_bar_restore (LrgActionBar  *self,
                        GVariant      *variant,
                        GError       **error);

G_END_DECLS
