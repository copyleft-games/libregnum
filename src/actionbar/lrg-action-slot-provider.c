/* lrg-action-slot-provider.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The provider interface's dispatch.
 */

#include "config.h"

#include "lrg-action-slot-provider.h"
#include "../lrg-enums.h"

#include <gio/gio.h>

G_DEFINE_INTERFACE (LrgActionSlotProvider, lrg_action_slot_provider, G_TYPE_OBJECT)

static void
lrg_action_slot_provider_default_init (LrgActionSlotProviderInterface *iface)
{
    (void)iface;
}

gboolean
lrg_action_slot_provider_query (LrgActionSlotProvider *self,
                                const LrgActionSlot   *slot,
                                LrgActionSlotState    *state)
{
    LrgActionSlotProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_ACTION_SLOT_PROVIDER (self), FALSE);
    g_return_val_if_fail (slot != NULL, FALSE);
    g_return_val_if_fail (state != NULL, FALSE);

    lrg_action_slot_state_init (state);
    iface = LRG_ACTION_SLOT_PROVIDER_GET_IFACE (self);
    if (iface->query == NULL)
        return FALSE;
    return iface->query (self, slot, state);
}

gboolean
lrg_action_slot_provider_activate (LrgActionSlotProvider  *self,
                                   const LrgActionSlot    *slot,
                                   GError                **error)
{
    LrgActionSlotProviderInterface *iface;

    g_return_val_if_fail (LRG_IS_ACTION_SLOT_PROVIDER (self), FALSE);
    g_return_val_if_fail (slot != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_ACTION_SLOT_PROVIDER_GET_IFACE (self);
    if (iface->activate == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                     "%s cannot use '%s' slots", G_OBJECT_TYPE_NAME (self),
                     lrg_action_slot_get_kind (slot));
        return FALSE;
    }
    return iface->activate (self, slot, error);
}
