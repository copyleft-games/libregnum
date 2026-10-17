/* lrg-action-slot-provider.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgActionSlotProvider - the game behind an action bar: it tells how a
 * slot's content looks now (usable, count, cooldown) and uses it when
 * the button is pressed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "lrg-action-slot.h"

G_BEGIN_DECLS

#define LRG_TYPE_ACTION_SLOT_PROVIDER (lrg_action_slot_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgActionSlotProvider, lrg_action_slot_provider, LRG, ACTION_SLOT_PROVIDER, GObject)

/**
 * LrgActionSlotProviderInterface:
 * @parent_iface: the parent interface
 * @query: fills @state for @slot (the state comes initialised by
 *   lrg_action_slot_state_init()); returns %FALSE when the provider does
 *   not know the slot's kind
 * @activate: uses @slot; returns %FALSE with @error set when it was
 *   refused (the reason is for the player)
 *
 * Implemented by the game for its slot kinds.
 */
struct _LrgActionSlotProviderInterface
{
    GTypeInterface parent_iface;

    gboolean (*query)    (LrgActionSlotProvider  *self,
                          const LrgActionSlot    *slot,
                          LrgActionSlotState     *state);
    gboolean (*activate) (LrgActionSlotProvider  *self,
                          const LrgActionSlot    *slot,
                          GError                **error);

    /*< private >*/
    gpointer _reserved[6];
};

/**
 * lrg_action_slot_provider_query:
 * @self: an #LrgActionSlotProvider
 * @slot: a slot content
 * @state: (out caller-allocates): its state now
 *
 * Returns: %FALSE when the provider does not know @slot's kind (@state
 *   then says usable, available, no count, no cooldown)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_slot_provider_query (LrgActionSlotProvider *self,
                                const LrgActionSlot   *slot,
                                LrgActionSlotState    *state);

/**
 * lrg_action_slot_provider_activate:
 * @self: an #LrgActionSlotProvider
 * @slot: a slot content
 * @error: return location for a #GError
 *
 * Returns: %TRUE when @slot was used
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_action_slot_provider_activate (LrgActionSlotProvider  *self,
                                   const LrgActionSlot    *slot,
                                   GError                **error);

G_END_DECLS
