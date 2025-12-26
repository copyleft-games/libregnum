/* lrg-saveable.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that can be saved and loaded.
 *
 * Any object that needs to persist state to save files should
 * implement this interface. The save manager will call these
 * methods during save/load operations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SAVEABLE (lrg_saveable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgSaveable, lrg_saveable, LRG, SAVEABLE, GObject)

/**
 * LrgSaveableInterface:
 * @parent_iface: parent interface
 * @get_save_id: returns a unique identifier for this saveable object
 * @save: serializes the object's state to the save context
 * @load: deserializes the object's state from the save context
 *
 * Interface structure for #LrgSaveable.
 *
 * Implementors must provide all three methods. The save_id should be
 * unique across all saveable objects in the application.
 */
struct _LrgSaveableInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgSaveableInterface::get_save_id:
     * @self: a #LrgSaveable
     *
     * Gets the unique save identifier for this object.
     *
     * This ID is used to match saved data to objects during loading.
     * It should be stable across application runs and unique among
     * all saveable objects.
     *
     * Returns: (transfer none): the save ID string
     */
    const gchar * (*get_save_id) (LrgSaveable *self);

    /**
     * LrgSaveableInterface::save:
     * @self: a #LrgSaveable
     * @context: the #LrgSaveContext to save to
     * @error: (optional): return location for a #GError
     *
     * Saves the object's state to the save context.
     *
     * Implementations should serialize all relevant state using
     * the save context's serialization methods.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*save) (LrgSaveable      *self,
                      LrgSaveContext   *context,
                      GError          **error);

    /**
     * LrgSaveableInterface::load:
     * @self: a #LrgSaveable
     * @context: the #LrgSaveContext to load from
     * @error: (optional): return location for a #GError
     *
     * Loads the object's state from the save context.
     *
     * Implementations should deserialize state using the save
     * context's deserialization methods. The object should be
     * restored to the exact state it was in when saved.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*load) (LrgSaveable      *self,
                      LrgSaveContext   *context,
                      GError          **error);
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_saveable_get_save_id:
 * @self: a #LrgSaveable
 *
 * Gets the unique save identifier for this object.
 *
 * Returns: (transfer none): the save ID string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_saveable_get_save_id (LrgSaveable *self);

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
LRG_AVAILABLE_IN_ALL
gboolean lrg_saveable_save (LrgSaveable      *self,
                            LrgSaveContext   *context,
                            GError          **error);

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
LRG_AVAILABLE_IN_ALL
gboolean lrg_saveable_load (LrgSaveable      *self,
                            LrgSaveContext   *context,
                            GError          **error);

G_END_DECLS
