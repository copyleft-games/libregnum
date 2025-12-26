/* lrg-save-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manager for save/load operations.
 *
 * The save manager coordinates saving and loading of game state.
 * It maintains a registry of saveable objects and manages save slots.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <libdex.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-saveable.h"
#include "lrg-save-context.h"
#include "lrg-save-game.h"

G_BEGIN_DECLS

#define LRG_TYPE_SAVE_MANAGER (lrg_save_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSaveManager, lrg_save_manager, LRG, SAVE_MANAGER, GObject)

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

/**
 * lrg_save_manager_get_default:
 *
 * Gets the default save manager instance.
 *
 * Returns: (transfer none): The default #LrgSaveManager
 */
LRG_AVAILABLE_IN_ALL
LrgSaveManager * lrg_save_manager_get_default (void);

/**
 * lrg_save_manager_new:
 *
 * Creates a new save manager.
 *
 * In most cases you should use lrg_save_manager_get_default() instead.
 *
 * Returns: (transfer full): A new #LrgSaveManager
 */
LRG_AVAILABLE_IN_ALL
LrgSaveManager * lrg_save_manager_new (void);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_save_manager_get_save_directory:
 * @self: a #LrgSaveManager
 *
 * Gets the directory where save files are stored.
 *
 * Returns: (transfer none): the save directory path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_save_manager_get_save_directory (LrgSaveManager *self);

/**
 * lrg_save_manager_set_save_directory:
 * @self: a #LrgSaveManager
 * @directory: the save directory path
 *
 * Sets the directory where save files are stored.
 *
 * The directory will be created if it doesn't exist.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_manager_set_save_directory (LrgSaveManager *self,
                                          const gchar    *directory);

/**
 * lrg_save_manager_get_save_version:
 * @self: a #LrgSaveManager
 *
 * Gets the current save format version.
 *
 * Returns: the version number
 */
LRG_AVAILABLE_IN_ALL
guint lrg_save_manager_get_save_version (LrgSaveManager *self);

/**
 * lrg_save_manager_set_save_version:
 * @self: a #LrgSaveManager
 * @version: the version number
 *
 * Sets the current save format version.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_manager_set_save_version (LrgSaveManager *self,
                                        guint           version);

/* ==========================================================================
 * Saveable Registration
 * ========================================================================== */

/**
 * lrg_save_manager_register:
 * @self: a #LrgSaveManager
 * @saveable: the #LrgSaveable to register
 *
 * Registers a saveable object with the manager.
 *
 * Registered objects will have their save/load methods called
 * during save and load operations.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_manager_register (LrgSaveManager *self,
                                LrgSaveable    *saveable);

/**
 * lrg_save_manager_unregister:
 * @self: a #LrgSaveManager
 * @saveable: the #LrgSaveable to unregister
 *
 * Unregisters a saveable object from the manager.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_manager_unregister (LrgSaveManager *self,
                                  LrgSaveable    *saveable);

/**
 * lrg_save_manager_unregister_all:
 * @self: a #LrgSaveManager
 *
 * Unregisters all saveable objects.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_manager_unregister_all (LrgSaveManager *self);

/* ==========================================================================
 * Synchronous Save/Load
 * ========================================================================== */

/**
 * lrg_save_manager_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: (optional): return location for a #GError
 *
 * Saves the game state to the specified slot.
 *
 * This calls the save method on all registered saveable objects.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_manager_save (LrgSaveManager  *self,
                                const gchar     *slot_name,
                                GError         **error);

/**
 * lrg_save_manager_load:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: (optional): return location for a #GError
 *
 * Loads the game state from the specified slot.
 *
 * This calls the load method on all registered saveable objects.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_manager_load (LrgSaveManager  *self,
                                const gchar     *slot_name,
                                GError         **error);

/* ==========================================================================
 * Asynchronous Save/Load
 * ========================================================================== */

/**
 * lrg_save_manager_save_async:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Saves the game state asynchronously.
 *
 * Returns: (transfer full): A #DexFuture that resolves to %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_save_manager_save_async (LrgSaveManager *self,
                                         const gchar    *slot_name);

/**
 * lrg_save_manager_load_async:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Loads the game state asynchronously.
 *
 * Returns: (transfer full): A #DexFuture that resolves to %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_save_manager_load_async (LrgSaveManager *self,
                                         const gchar    *slot_name);

/* ==========================================================================
 * Save Slot Management
 * ========================================================================== */

/**
 * lrg_save_manager_list_saves:
 * @self: a #LrgSaveManager
 *
 * Lists all available save games.
 *
 * Returns: (transfer full) (element-type LrgSaveGame): A list of save games
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_save_manager_list_saves (LrgSaveManager *self);

/**
 * lrg_save_manager_get_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Gets the save game for a specific slot.
 *
 * Returns: (transfer full) (nullable): The #LrgSaveGame, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgSaveGame * lrg_save_manager_get_save (LrgSaveManager *self,
                                         const gchar    *slot_name);

/**
 * lrg_save_manager_delete_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: (optional): return location for a #GError
 *
 * Deletes a save game.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_manager_delete_save (LrgSaveManager  *self,
                                       const gchar     *slot_name,
                                       GError         **error);

/**
 * lrg_save_manager_slot_exists:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Checks if a save slot exists.
 *
 * Returns: %TRUE if the slot exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_manager_slot_exists (LrgSaveManager *self,
                                       const gchar    *slot_name);

/* ==========================================================================
 * Signals
 * ========================================================================== */

/**
 * LrgSaveManager::save-started:
 * @self: the #LrgSaveManager
 * @slot_name: the slot being saved to
 *
 * Emitted when a save operation begins.
 */

/**
 * LrgSaveManager::save-completed:
 * @self: the #LrgSaveManager
 * @slot_name: the slot that was saved
 * @success: whether the save succeeded
 *
 * Emitted when a save operation completes.
 */

/**
 * LrgSaveManager::load-started:
 * @self: the #LrgSaveManager
 * @slot_name: the slot being loaded from
 *
 * Emitted when a load operation begins.
 */

/**
 * LrgSaveManager::load-completed:
 * @self: the #LrgSaveManager
 * @slot_name: the slot that was loaded
 * @success: whether the load succeeded
 *
 * Emitted when a load operation completes.
 */

G_END_DECLS
