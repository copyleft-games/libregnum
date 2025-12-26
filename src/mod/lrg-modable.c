/* lrg-modable.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Modable interface implementation.
 */

#include "config.h"
#include "lrg-modable.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

/* ==========================================================================
 * Interface Definition
 * ========================================================================== */

#pragma GCC visibility push(default)
G_DEFINE_INTERFACE (LrgModable, lrg_modable, G_TYPE_OBJECT)
#pragma GCC visibility pop

static void
lrg_modable_default_init (LrgModableInterface *iface)
{
    /* Default implementations are NULL - subclasses must implement */
}

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_modable_init:
 * @self: a #LrgModable
 * @engine: the engine instance
 * @error: (nullable): return location for error
 *
 * Initializes the mod with the engine.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_modable_init (LrgModable  *self,
                  LrgEngine   *engine,
                  GError     **error)
{
    LrgModableInterface *iface;

    g_return_val_if_fail (LRG_IS_MODABLE (self), FALSE);

    iface = LRG_MODABLE_GET_IFACE (self);

    if (iface->mod_init == NULL)
        return TRUE;

    return iface->mod_init (self, engine, error);
}

/**
 * lrg_modable_shutdown:
 * @self: a #LrgModable
 *
 * Shuts down the mod.
 */
void
lrg_modable_shutdown (LrgModable *self)
{
    LrgModableInterface *iface;

    g_return_if_fail (LRG_IS_MODABLE (self));

    iface = LRG_MODABLE_GET_IFACE (self);

    if (iface->mod_shutdown != NULL)
        iface->mod_shutdown (self);
}

/**
 * lrg_modable_get_info:
 * @self: a #LrgModable
 *
 * Gets the mod manifest.
 *
 * Returns: (transfer none) (nullable): the #LrgModManifest
 */
LrgModManifest *
lrg_modable_get_info (LrgModable *self)
{
    LrgModableInterface *iface;

    g_return_val_if_fail (LRG_IS_MODABLE (self), NULL);

    iface = LRG_MODABLE_GET_IFACE (self);

    if (iface->mod_get_info == NULL)
        return NULL;

    return iface->mod_get_info (self);
}
