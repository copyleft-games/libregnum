/* lrg-saveable.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgSaveable interface.
 */

#include "lrg-saveable.h"
#include "../lrg-log.h"

G_DEFINE_INTERFACE (LrgSaveable, lrg_saveable, G_TYPE_OBJECT)

static void
lrg_saveable_default_init (LrgSaveableInterface *iface)
{
    /* No default implementations - all methods must be provided */
}

/**
 * lrg_saveable_get_save_id:
 * @self: a #LrgSaveable
 *
 * Gets the unique save identifier for this object.
 *
 * Returns: (transfer none): the save ID string
 */
const gchar *
lrg_saveable_get_save_id (LrgSaveable *self)
{
    LrgSaveableInterface *iface;

    g_return_val_if_fail (LRG_IS_SAVEABLE (self), NULL);

    iface = LRG_SAVEABLE_GET_IFACE (self);

    g_return_val_if_fail (iface->get_save_id != NULL, NULL);

    return iface->get_save_id (self);
}

/**
 * lrg_saveable_save:
 * @self: a #LrgSaveable
 * @context: the #LrgSaveContext to save to
 * @error: (optional): return location for a #GError
 *
 * Saves the object's state to the save context.
 *
 * Returns: %TRUE on success, %FALSE with @error set on failure
 */
gboolean
lrg_saveable_save (LrgSaveable      *self,
                   LrgSaveContext   *context,
                   GError          **error)
{
    LrgSaveableInterface *iface;

    g_return_val_if_fail (LRG_IS_SAVEABLE (self), FALSE);
    g_return_val_if_fail (context != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_SAVEABLE_GET_IFACE (self);

    if (iface->save == NULL)
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_FAILED,
                     "Type %s does not implement save()",
                     G_OBJECT_TYPE_NAME (self));
        return FALSE;
    }

    return iface->save (self, context, error);
}

/**
 * lrg_saveable_load:
 * @self: a #LrgSaveable
 * @context: the #LrgSaveContext to load from
 * @error: (optional): return location for a #GError
 *
 * Loads the object's state from the save context.
 *
 * Returns: %TRUE on success, %FALSE with @error set on failure
 */
gboolean
lrg_saveable_load (LrgSaveable      *self,
                   LrgSaveContext   *context,
                   GError          **error)
{
    LrgSaveableInterface *iface;

    g_return_val_if_fail (LRG_IS_SAVEABLE (self), FALSE);
    g_return_val_if_fail (context != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_SAVEABLE_GET_IFACE (self);

    if (iface->load == NULL)
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_FAILED,
                     "Type %s does not implement load()",
                     G_OBJECT_TYPE_NAME (self));
        return FALSE;
    }

    return iface->load (self, context, error);
}
