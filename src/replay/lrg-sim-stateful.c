/* lrg-sim-stateful.c - Interface for objects with rollback-able simulation state
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-sim-stateful.h"

#include <gio/gio.h>

G_DEFINE_INTERFACE (LrgSimStateful, lrg_sim_stateful, G_TYPE_OBJECT)

static void
lrg_sim_stateful_default_init (LrgSimStatefulInterface *iface)
{
    /* No default implementations: save_state and load_state are required */
}

const gchar *
lrg_sim_stateful_get_state_id (LrgSimStateful *self)
{
    LrgSimStatefulInterface *iface;

    g_return_val_if_fail (LRG_IS_SIM_STATEFUL (self), NULL);

    iface = LRG_SIM_STATEFUL_GET_IFACE (self);
    if (iface->get_state_id != NULL)
        return iface->get_state_id (self);

    /* Fall back to the type name: stable across runs of one build */
    return G_OBJECT_TYPE_NAME (self);
}

void
lrg_sim_stateful_save_state (LrgSimStateful *self,
                             LrgStateBuffer *buffer)
{
    LrgSimStatefulInterface *iface;

    g_return_if_fail (LRG_IS_SIM_STATEFUL (self));
    g_return_if_fail (LRG_IS_STATE_BUFFER (buffer));

    iface = LRG_SIM_STATEFUL_GET_IFACE (self);
    g_return_if_fail (iface->save_state != NULL);

    iface->save_state (self, buffer);
}

gboolean
lrg_sim_stateful_load_state (LrgSimStateful  *self,
                             LrgStateBuffer  *buffer,
                             GError         **error)
{
    LrgSimStatefulInterface *iface;

    g_return_val_if_fail (LRG_IS_SIM_STATEFUL (self), FALSE);
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (buffer), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_SIM_STATEFUL_GET_IFACE (self);
    if (iface->load_state == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                     "%s does not implement load_state", G_OBJECT_TYPE_NAME (self));
        return FALSE;
    }

    if (!iface->load_state (self, buffer, error))
        return FALSE;

    /* An implementation that read past its data loaded garbage */
    return lrg_state_buffer_check (buffer, error);
}

guint64
lrg_sim_stateful_checksum (LrgSimStateful *self)
{
    g_autoptr(LrgStateBuffer) buffer = NULL;

    g_return_val_if_fail (LRG_IS_SIM_STATEFUL (self), 0);

    buffer = lrg_state_buffer_new ();
    lrg_sim_stateful_save_state (self, buffer);
    return lrg_state_buffer_checksum (buffer);
}
