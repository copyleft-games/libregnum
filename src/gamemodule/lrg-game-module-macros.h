/* lrg-game-module-macros.h - One-line dual-target game entry point
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
#include <gmodule.h>
#include "../lrg-version.h"
#include "lrg-loaded-game.h"
#include "../template/lrg-game-template.h"

G_BEGIN_DECLS

/**
 * LRG_DEFINE_GAME_MODULE:
 * @GameType: the #GType macro of the top-level #LrgGameTemplate subclass
 *   (the same value passed to g_object_new(), e.g. MY_TYPE_GAME)
 * @id_: a stable string identifier, e.g. "com.example.game"
 * @name_: a human-readable display name string
 * @ver_: the game's semantic version string, e.g. "1.0.0"
 *
 * Emits the entry point for a libregnum game. Drop this once in the game's
 * main translation unit. The SAME source then builds two ways depending on a
 * single compile-time flag:
 *
 * - Module build (compile with -DLRG_GAME_BUILD_MODULE): emits the exported
 *   #LRG_GAME_MODULE_ENTRY_SYMBOL so the game can be loaded as a `.so` by the
 *   shim launcher or any embedding host.
 *
 * - Standalone build (default, no flag): emits a `main()` that runs the game
 *   with lrg_game_run_standalone().
 *
 * Since: 1.0
 */
#if defined(LRG_GAME_BUILD_MODULE)

# define LRG_DEFINE_GAME_MODULE(GameType, id_, name_, ver_)                    \
    static GType                                                               \
    _lrg_game_module_resolve_type (void)                                       \
    {                                                                          \
        return (GameType);                                                     \
    }                                                                          \
                                                                              \
    static const LrgGameModuleInfo _lrg_game_module_info = {                   \
        LRG_GAME_MODULE_ABI_VERSION,                                           \
        LRG_VERSION_MAJOR,                                                     \
        LRG_VERSION_MINOR,                                                     \
        LRG_VERSION_MICRO,                                                     \
        (id_),                                                                 \
        (name_),                                                               \
        (ver_),                                                                \
        _lrg_game_module_resolve_type                                          \
    };                                                                         \
                                                                              \
    G_MODULE_EXPORT const LrgGameModuleInfo *lrg_game_module_query (void);     \
    G_MODULE_EXPORT const LrgGameModuleInfo *                                  \
    lrg_game_module_query (void)                                               \
    {                                                                          \
        return &_lrg_game_module_info;                                         \
    }

#else /* standalone executable */

# define LRG_DEFINE_GAME_MODULE(GameType, id_, name_, ver_)                    \
    int                                                                        \
    main (int argc, char **argv)                                               \
    {                                                                          \
        GObject *_lrg_game = g_object_new ((GameType), NULL);                  \
        int      _lrg_rc;                                                      \
                                                                              \
        _lrg_rc = lrg_game_run_standalone (LRG_GAME_TEMPLATE (_lrg_game),      \
                                           argc, argv);                        \
        g_object_unref (_lrg_game);                                            \
        return _lrg_rc;                                                        \
    }

#endif /* LRG_GAME_BUILD_MODULE */

G_END_DECLS
