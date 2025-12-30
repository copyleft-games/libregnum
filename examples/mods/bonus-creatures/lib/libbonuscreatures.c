/* libbonuscreatures.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Native DLC module that adds bonus creatures to the Creature Collector game.
 * This shared library is loaded via GModule by the mod system.
 *
 * Exported symbols:
 *   - lrg_mod_init:     Called when the mod is loaded
 *   - lrg_mod_shutdown: Called when the mod is unloaded
 */

#include <glib.h>
#include <gmodule.h>
#include <libregnum.h>

/* Include the shared creature registry API */
#include "../../../creature-registry.h"

/* Forward declarations for exported mod entry points */
G_MODULE_EXPORT gboolean
lrg_mod_init (LrgMod   *mod,
              gpointer *user_data);

G_MODULE_EXPORT void
lrg_mod_shutdown (LrgMod  *mod,
                  gpointer user_data);

/*
 * lrg_mod_init:
 * @mod: The LrgMod instance for this mod
 * @user_data: (out): Optional user data to store for later use
 *
 * Called when the mod is loaded by the mod manager.
 * Registers bonus creatures with the game's creature registry.
 *
 * Returns: TRUE on success, FALSE on failure
 */
G_MODULE_EXPORT gboolean
lrg_mod_init (LrgMod   *mod,
              gpointer *user_data)
{
    g_print ("=== Bonus Creatures Pack Initializing ===\n");

    /*
     * Register bonus creatures with the game's creature registry.
     * The register_creature_type() function is exported by the main game
     * executable and resolved at runtime via GModule.
     */

    /* Golden Slime - fast, shimmering gold creature */
    register_creature_type ("Golden Slime",
                            255,    /* r - gold */
                            215,    /* g */
                            0,      /* b */
                            2.5f,   /* speed - faster than base creatures */
                            TRUE);  /* from_mod = TRUE */

    /* Shadow Beast - mysterious purple creature */
    register_creature_type ("Shadow Beast",
                            128,    /* r - dark purple */
                            0,      /* g */
                            200,    /* b */
                            3.0f,   /* speed - fastest creature */
                            TRUE);  /* from_mod = TRUE */

    g_print ("Bonus Creatures Pack loaded successfully!\n");
    g_print ("  - Added: Golden Slime (speed 2.5x)\n");
    g_print ("  - Added: Shadow Beast (speed 3.0x)\n");

    /* No user data needed for this simple mod */
    *user_data = NULL;

    return TRUE;
}

/*
 * lrg_mod_shutdown:
 * @mod: The LrgMod instance for this mod
 * @user_data: User data that was stored during init
 *
 * Called when the mod is unloaded.
 * Performs any necessary cleanup.
 */
G_MODULE_EXPORT void
lrg_mod_shutdown (LrgMod  *mod,
                  gpointer user_data)
{
    g_print ("Bonus Creatures Pack unloaded.\n");

    /* Nothing to clean up - creatures are managed by the game */
}
