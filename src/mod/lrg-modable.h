/* lrg-modable.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for modable objects.
 *
 * This is the base interface that all mods should implement to integrate
 * with the engine lifecycle.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_MODABLE (lrg_modable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgModable, lrg_modable, LRG, MODABLE, GObject)

/**
 * LrgModableInterface:
 * @g_iface: parent interface
 * @mod_init: called when the mod is initialized
 * @mod_shutdown: called when the mod is shutting down
 * @mod_get_info: gets the mod manifest
 *
 * Interface for modable objects.
 */
struct _LrgModableInterface
{
    GTypeInterface g_iface;

    /**
     * LrgModableInterface::mod_init:
     * @self: the modable object
     * @engine: the engine instance
     * @error: (nullable): return location for error
     *
     * Called when the mod is being initialized.
     *
     * Returns: %TRUE on success
     */
    gboolean       (*mod_init)      (LrgModable  *self,
                                     LrgEngine   *engine,
                                     GError     **error);

    /**
     * LrgModableInterface::mod_shutdown:
     * @self: the modable object
     *
     * Called when the mod is shutting down.
     */
    void           (*mod_shutdown)  (LrgModable *self);

    /**
     * LrgModableInterface::mod_get_info:
     * @self: the modable object
     *
     * Gets the mod manifest containing metadata.
     *
     * Returns: (transfer none) (nullable): the mod manifest
     */
    LrgModManifest *(*mod_get_info) (LrgModable *self);
};

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
LRG_AVAILABLE_IN_ALL
gboolean        lrg_modable_init      (LrgModable  *self,
                                       LrgEngine   *engine,
                                       GError     **error);

/**
 * lrg_modable_shutdown:
 * @self: a #LrgModable
 *
 * Shuts down the mod.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_modable_shutdown  (LrgModable *self);

/**
 * lrg_modable_get_info:
 * @self: a #LrgModable
 *
 * Gets the mod manifest.
 *
 * Returns: (transfer none) (nullable): the #LrgModManifest
 */
LRG_AVAILABLE_IN_ALL
LrgModManifest *lrg_modable_get_info  (LrgModable *self);

G_END_DECLS
