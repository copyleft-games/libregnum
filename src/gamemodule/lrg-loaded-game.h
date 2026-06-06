/* lrg-loaded-game.h - Load a libregnum game packaged as a shared module
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_GAME_MODULE_ABI_VERSION:
 *
 * The ABI version of the game-module contract. A module records the value it
 * was built against in its #LrgGameModuleInfo; the loader refuses a module
 * whose ABI version does not match the running libregnum.
 *
 * Since: 1.0
 */
#define LRG_GAME_MODULE_ABI_VERSION 1

/**
 * LRG_GAME_MODULE_ENTRY_SYMBOL:
 *
 * The name of the well-known symbol a game module exports. It resolves to a
 * #LrgGameModuleQueryFunc returning the module's #LrgGameModuleInfo.
 *
 * Since: 1.0
 */
#define LRG_GAME_MODULE_ENTRY_SYMBOL "lrg_game_module_query"

/**
 * LrgGameModuleInfo:
 * @abi_version: the #LRG_GAME_MODULE_ABI_VERSION the module was built against
 * @lrg_major: the libregnum major version the module was built against
 * @lrg_minor: the libregnum minor version the module was built against
 * @lrg_micro: the libregnum micro version the module was built against
 * @game_id: a stable identifier, e.g. "com.example.game"
 * @game_name: a human-readable display name
 * @game_version: the game's own semantic version string
 * @get_type: returns the #GType of the top-level #LrgGameTemplate subclass
 *
 * Static metadata a game module exposes through its entry symbol so the loader
 * can validate compatibility before instantiating the game.
 *
 * Since: 1.0
 */
typedef struct _LrgGameModuleInfo
{
    guint32      abi_version;
    guint16      lrg_major;
    guint16      lrg_minor;
    guint16      lrg_micro;
    const gchar *game_id;
    const gchar *game_name;
    const gchar *game_version;
    GType      (*get_type) (void);
} LrgGameModuleInfo;

/**
 * LrgGameModuleQueryFunc:
 *
 * The signature of the #LRG_GAME_MODULE_ENTRY_SYMBOL exported by a game module.
 *
 * Returns: (transfer none): the module's static #LrgGameModuleInfo
 *
 * Since: 1.0
 */
typedef const LrgGameModuleInfo *(*LrgGameModuleQueryFunc) (void);

/**
 * LRG_LOADED_GAME_ERROR:
 *
 * Error domain for loading game modules.
 *
 * Since: 1.0
 */
#define LRG_LOADED_GAME_ERROR (lrg_loaded_game_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_loaded_game_error_quark (void);

/**
 * LrgLoadedGameError:
 * @LRG_LOADED_GAME_ERROR_OPEN_FAILED: the shared object could not be opened
 * @LRG_LOADED_GAME_ERROR_NO_ENTRY: the entry symbol was not found
 * @LRG_LOADED_GAME_ERROR_ABI_MISMATCH: the module's ABI version is incompatible
 * @LRG_LOADED_GAME_ERROR_VERSION_MISMATCH: the module needs a newer libregnum
 * @LRG_LOADED_GAME_ERROR_BAD_TYPE: the module's type is not an #LrgGameTemplate
 *
 * Errors that can occur while loading a game module.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_LOADED_GAME_ERROR_OPEN_FAILED,
    LRG_LOADED_GAME_ERROR_NO_ENTRY,
    LRG_LOADED_GAME_ERROR_ABI_MISMATCH,
    LRG_LOADED_GAME_ERROR_VERSION_MISMATCH,
    LRG_LOADED_GAME_ERROR_BAD_TYPE
} LrgLoadedGameError;

#define LRG_TYPE_LOADED_GAME (lrg_loaded_game_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLoadedGame, lrg_loaded_game, LRG, LOADED_GAME, GObject)

/**
 * lrg_loaded_game_load:
 * @so_path: path to the game module shared object
 * @error: (nullable): return location for error
 *
 * Opens @so_path, validates its ABI and libregnum version, and instantiates
 * its top-level game object. The module stays open for the lifetime of the
 * returned #LrgLoadedGame.
 *
 * Returns: (transfer full) (nullable): a new #LrgLoadedGame, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLoadedGame *
lrg_loaded_game_load (const gchar  *so_path,
                      GError      **error);

/**
 * lrg_loaded_game_load_from_manifest:
 * @manifest: an #LrgModManifest describing the game module
 * @base_path: the directory the manifest's entry_point is relative to
 * @error: (nullable): return location for error
 *
 * Resolves the manifest's entry_point against @base_path and loads it with
 * lrg_loaded_game_load().
 *
 * Returns: (transfer full) (nullable): a new #LrgLoadedGame, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLoadedGame *
lrg_loaded_game_load_from_manifest (LrgModManifest  *manifest,
                                    const gchar     *base_path,
                                    GError         **error);

/**
 * lrg_loaded_game_get_game:
 * @self: an #LrgLoadedGame
 *
 * Gets the instantiated game object.
 *
 * Returns: (transfer none): the #LrgGameTemplate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGameTemplate *
lrg_loaded_game_get_game (LrgLoadedGame *self);

/**
 * lrg_loaded_game_get_info:
 * @self: an #LrgLoadedGame
 *
 * Gets the module's static metadata.
 *
 * Returns: (transfer none): the #LrgGameModuleInfo
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgGameModuleInfo *
lrg_loaded_game_get_info (LrgLoadedGame *self);

/**
 * lrg_loaded_game_unload:
 * @self: an #LrgLoadedGame
 *
 * Releases the instantiated game and closes the module. Called automatically
 * when the last reference is dropped; provided for explicit teardown. After
 * this, lrg_loaded_game_get_game() returns %NULL.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_loaded_game_unload (LrgLoadedGame *self);

G_END_DECLS
