/* lrg-save-game.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Represents a single save game slot.
 *
 * A save game contains metadata (name, timestamp, playtime) and
 * references the actual save file on disk.
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

#define LRG_TYPE_SAVE_GAME (lrg_save_game_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSaveGame, lrg_save_game, LRG, SAVE_GAME, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_save_game_new:
 * @slot_name: the slot identifier (e.g., "slot1", "autosave")
 *
 * Creates a new save game with the given slot name.
 *
 * Returns: (transfer full): A new #LrgSaveGame
 */
LRG_AVAILABLE_IN_ALL
LrgSaveGame * lrg_save_game_new (const gchar *slot_name);

/**
 * lrg_save_game_new_from_file:
 * @path: path to the save file
 * @error: (optional): return location for a #GError
 *
 * Loads save game metadata from a file.
 *
 * This only loads the metadata (name, timestamp, playtime), not
 * the full save data. Use the save manager to perform a full load.
 *
 * Returns: (transfer full) (nullable): A new #LrgSaveGame, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSaveGame * lrg_save_game_new_from_file (const gchar  *path,
                                            GError      **error);

/* ==========================================================================
 * Metadata
 * ========================================================================== */

/**
 * lrg_save_game_get_slot_name:
 * @self: a #LrgSaveGame
 *
 * Gets the slot identifier for this save.
 *
 * Returns: (transfer none): the slot name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_save_game_get_slot_name (LrgSaveGame *self);

/**
 * lrg_save_game_get_display_name:
 * @self: a #LrgSaveGame
 *
 * Gets the display name for this save.
 *
 * This is the user-visible name, which may be different from the slot name.
 *
 * Returns: (transfer none) (nullable): the display name, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_save_game_get_display_name (LrgSaveGame *self);

/**
 * lrg_save_game_set_display_name:
 * @self: a #LrgSaveGame
 * @name: (nullable): the display name to set
 *
 * Sets the display name for this save.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_set_display_name (LrgSaveGame *self,
                                     const gchar *name);

/**
 * lrg_save_game_get_timestamp:
 * @self: a #LrgSaveGame
 *
 * Gets the timestamp when this save was created or last modified.
 *
 * Returns: (transfer none) (nullable): the timestamp, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_save_game_get_timestamp (LrgSaveGame *self);

/**
 * lrg_save_game_set_timestamp:
 * @self: a #LrgSaveGame
 * @timestamp: (nullable): the timestamp to set
 *
 * Sets the timestamp for this save.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_set_timestamp (LrgSaveGame *self,
                                  GDateTime   *timestamp);

/**
 * lrg_save_game_update_timestamp:
 * @self: a #LrgSaveGame
 *
 * Updates the timestamp to the current time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_update_timestamp (LrgSaveGame *self);

/**
 * lrg_save_game_get_playtime:
 * @self: a #LrgSaveGame
 *
 * Gets the total playtime in seconds.
 *
 * Returns: the playtime in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_save_game_get_playtime (LrgSaveGame *self);

/**
 * lrg_save_game_set_playtime:
 * @self: a #LrgSaveGame
 * @playtime: the playtime in seconds
 *
 * Sets the total playtime.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_set_playtime (LrgSaveGame *self,
                                 gdouble      playtime);

/**
 * lrg_save_game_add_playtime:
 * @self: a #LrgSaveGame
 * @seconds: seconds to add
 *
 * Adds to the total playtime.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_add_playtime (LrgSaveGame *self,
                                 gdouble      seconds);

/* ==========================================================================
 * File Path
 * ========================================================================== */

/**
 * lrg_save_game_get_path:
 * @self: a #LrgSaveGame
 *
 * Gets the file path for this save.
 *
 * Returns: (transfer none) (nullable): the file path, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_save_game_get_path (LrgSaveGame *self);

/**
 * lrg_save_game_set_path:
 * @self: a #LrgSaveGame
 * @path: (nullable): the file path to set
 *
 * Sets the file path for this save.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_set_path (LrgSaveGame *self,
                             const gchar *path);

/**
 * lrg_save_game_exists:
 * @self: a #LrgSaveGame
 *
 * Checks if the save file exists on disk.
 *
 * Returns: %TRUE if the file exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_game_exists (LrgSaveGame *self);

/* ==========================================================================
 * Version
 * ========================================================================== */

/**
 * lrg_save_game_get_version:
 * @self: a #LrgSaveGame
 *
 * Gets the save format version.
 *
 * Returns: the version number
 */
LRG_AVAILABLE_IN_ALL
guint lrg_save_game_get_version (LrgSaveGame *self);

/**
 * lrg_save_game_set_version:
 * @self: a #LrgSaveGame
 * @version: the version number
 *
 * Sets the save format version.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_set_version (LrgSaveGame *self,
                                guint        version);

/* ==========================================================================
 * Custom Data
 * ========================================================================== */

/**
 * lrg_save_game_set_custom_string:
 * @self: a #LrgSaveGame
 * @key: the key name
 * @value: (nullable): the string value
 *
 * Sets a custom string value in the save metadata.
 *
 * This can be used to store additional game-specific information
 * like current level, character name, etc.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_set_custom_string (LrgSaveGame *self,
                                      const gchar *key,
                                      const gchar *value);

/**
 * lrg_save_game_get_custom_string:
 * @self: a #LrgSaveGame
 * @key: the key name
 *
 * Gets a custom string value from the save metadata.
 *
 * Returns: (transfer none) (nullable): the string value, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_save_game_get_custom_string (LrgSaveGame *self,
                                               const gchar *key);

/**
 * lrg_save_game_set_custom_int:
 * @self: a #LrgSaveGame
 * @key: the key name
 * @value: the integer value
 *
 * Sets a custom integer value in the save metadata.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_game_set_custom_int (LrgSaveGame *self,
                                   const gchar *key,
                                   gint64       value);

/**
 * lrg_save_game_get_custom_int:
 * @self: a #LrgSaveGame
 * @key: the key name
 * @default_value: default if not found
 *
 * Gets a custom integer value from the save metadata.
 *
 * Returns: the integer value, or @default_value if not set
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_save_game_get_custom_int (LrgSaveGame *self,
                                     const gchar *key,
                                     gint64       default_value);

G_END_DECLS
