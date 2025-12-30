/* game-creature-collector.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Example demonstrating native GModule (shared object) loading for DLC.
 * Run with --no-mods to see the difference between base game and DLC-enhanced.
 *
 * Base game: 3 creatures (Red Blob, Blue Blob, Green Blob)
 * With DLC:  5 creatures (base + Golden Slime, Shadow Beast from DLC)
 */

#include <glib.h>
#include <libregnum.h>
#include <graylib.h>

#include "creature-registry.h"

/* Window dimensions */
#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600

/* Maximum creatures on screen */
#define MAX_CREATURES 20

/*
 * Creature instance - a spawned creature with position and velocity.
 */
typedef struct
{
    CreatureType *type;
    gfloat        x;
    gfloat        y;
    gfloat        vx;
    gfloat        vy;
    gfloat        radius;
} Creature;

/* Global creature registry */
static GPtrArray *g_creature_types = NULL;

/* Active creatures on screen */
static Creature g_creatures[MAX_CREATURES];
static gint g_creature_count = 0;

/* Mod loading state */
static gboolean g_mods_enabled = TRUE;
static gboolean g_dlc_loaded = FALSE;
static gint g_base_creature_count = 0;
static gint g_dlc_creature_count = 0;

/* Show detailed mod info overlay */
static gboolean g_show_mod_info = FALSE;

/*
 * register_creature_type:
 *
 * Registers a new creature type with the game.
 * This function is exported and can be called by DLC modules.
 */
G_MODULE_EXPORT void
register_creature_type (const gchar *name,
                        guint8       r,
                        guint8       g,
                        guint8       b,
                        gfloat       speed,
                        gboolean     from_mod)
{
    CreatureType *type;

    if (g_creature_types == NULL)
        g_creature_types = g_ptr_array_new_with_free_func (g_free);

    type = g_new0 (CreatureType, 1);
    type->name = g_strdup (name);
    type->r = r;
    type->g = g;
    type->b = b;
    type->speed = speed;
    type->from_mod = from_mod;

    g_ptr_array_add (g_creature_types, type);

    if (from_mod)
        g_dlc_creature_count++;
    else
        g_base_creature_count++;

    g_print ("Registered creature: %s (%s)\n",
             name, from_mod ? "DLC" : "base");
}

/*
 * get_creature_types:
 *
 * Gets all registered creature types.
 *
 * Returns: (transfer none): Array of creature types
 */
G_MODULE_EXPORT GPtrArray *
get_creature_types (void)
{
    return g_creature_types;
}

/*
 * register_base_creatures:
 *
 * Registers the base game creatures.
 */
static void
register_base_creatures (void)
{
    /* Red Blob - slow and steady */
    register_creature_type ("Red Blob", 255, 80, 80, 1.0f, FALSE);

    /* Blue Blob - medium speed */
    register_creature_type ("Blue Blob", 80, 80, 255, 1.5f, FALSE);

    /* Green Blob - fast */
    register_creature_type ("Green Blob", 80, 255, 80, 2.0f, FALSE);
}

/*
 * spawn_creature:
 *
 * Spawns a creature of the given type at a random position.
 */
static void
spawn_creature (CreatureType *type)
{
    Creature *creature;

    if (g_creature_count >= MAX_CREATURES)
        return;

    creature = &g_creatures[g_creature_count++];
    creature->type = type;
    creature->radius = 20.0f;

    /* Random position within bounds */
    creature->x = creature->radius + g_random_double_range (0, WINDOW_WIDTH - creature->radius * 2);
    creature->y = creature->radius + 80 + g_random_double_range (0, WINDOW_HEIGHT - creature->radius * 2 - 160);

    /* Random velocity based on creature speed */
    creature->vx = (g_random_double_range (-1, 1)) * type->speed * 100.0f;
    creature->vy = (g_random_double_range (-1, 1)) * type->speed * 100.0f;
}

/*
 * spawn_all_creatures:
 *
 * Spawns one creature of each registered type.
 */
static void
spawn_all_creatures (void)
{
    guint i;

    g_creature_count = 0;

    if (g_creature_types == NULL)
        return;

    for (i = 0; i < g_creature_types->len; i++)
    {
        CreatureType *type = g_ptr_array_index (g_creature_types, i);
        spawn_creature (type);
    }
}

/*
 * update_creatures:
 *
 * Updates creature positions and handles bouncing.
 */
static void
update_creatures (gfloat delta)
{
    gint i;

    for (i = 0; i < g_creature_count; i++)
    {
        Creature *c = &g_creatures[i];

        /* Update position */
        c->x += c->vx * delta;
        c->y += c->vy * delta;

        /* Bounce off walls */
        if (c->x - c->radius < 0)
        {
            c->x = c->radius;
            c->vx = -c->vx;
        }
        else if (c->x + c->radius > WINDOW_WIDTH)
        {
            c->x = WINDOW_WIDTH - c->radius;
            c->vx = -c->vx;
        }

        /* Bounce off top/bottom (leaving room for UI) */
        if (c->y - c->radius < 60)
        {
            c->y = 60 + c->radius;
            c->vy = -c->vy;
        }
        else if (c->y + c->radius > WINDOW_HEIGHT - 80)
        {
            c->y = WINDOW_HEIGHT - 80 - c->radius;
            c->vy = -c->vy;
        }
    }
}

/*
 * draw_creatures:
 *
 * Draws all creatures.
 */
static void
draw_creatures (void)
{
    gint i;

    for (i = 0; i < g_creature_count; i++)
    {
        Creature *c = &g_creatures[i];
        g_autoptr(GrlColor) color = NULL;
        g_autoptr(GrlColor) outline_color = NULL;

        color = grl_color_new (c->type->r, c->type->g, c->type->b, 255);

        /* Draw creature body */
        grl_draw_circle ((gint)c->x, (gint)c->y, c->radius, color);

        /* Draw outline - white for base, gold for DLC */
        if (c->type->from_mod)
            outline_color = grl_color_new (255, 215, 0, 255);
        else
            outline_color = grl_color_new (255, 255, 255, 255);

        grl_draw_circle_lines ((gint)c->x, (gint)c->y, c->radius, outline_color);

        /* Draw eyes */
        {
            g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
            g_autoptr(GrlColor) black = grl_color_new (0, 0, 0, 255);

            /* Left eye */
            grl_draw_circle ((gint)(c->x - 6), (gint)(c->y - 4), 5, white);
            grl_draw_circle ((gint)(c->x - 6), (gint)(c->y - 4), 2, black);

            /* Right eye */
            grl_draw_circle ((gint)(c->x + 6), (gint)(c->y - 4), 5, white);
            grl_draw_circle ((gint)(c->x + 6), (gint)(c->y - 4), 2, black);
        }
    }
}

/*
 * draw_creature_labels:
 *
 * Draws labels under each creature.
 */
static void
draw_creature_labels (void)
{
    gint i;
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) gold = grl_color_new (255, 215, 0, 255);

    for (i = 0; i < g_creature_count; i++)
    {
        Creature *c = &g_creatures[i];
        const gchar *label;
        gint text_width;
        GrlColor *label_color;

        if (c->type->from_mod)
        {
            label = g_strdup_printf ("%s (DLC)", c->type->name);
            label_color = gold;
        }
        else
        {
            label = c->type->name;
            label_color = white;
        }

        text_width = grl_measure_text (label, 10);
        grl_draw_text (label, (gint)(c->x - text_width / 2),
                       (gint)(c->y + c->radius + 5), 10, label_color);

        if (c->type->from_mod)
            g_free ((gchar *)label);
    }
}

/*
 * draw_ui:
 *
 * Draws the UI overlay.
 */
static void
draw_ui (void)
{
    g_autoptr(GrlColor) bg_color = grl_color_new (40, 40, 60, 255);
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) gold = grl_color_new (255, 215, 0, 255);
    g_autoptr(GrlColor) gray = grl_color_new (128, 128, 128, 255);
    g_autoptr(GrlColor) green = grl_color_new (80, 255, 80, 255);
    g_autoptr(GrlColor) red = grl_color_new (255, 80, 80, 255);
    gchar *status_text;
    gchar *count_text;

    /* Header background */
    grl_draw_rectangle (0, 0, WINDOW_WIDTH, 50, bg_color);

    /* Title */
    grl_draw_text ("CREATURE COLLECTOR", 20, 15, 20, white);

    /* DLC status indicator */
    if (g_dlc_loaded)
    {
        grl_draw_text ("[DLC LOADED]", WINDOW_WIDTH - 140, 15, 20, gold);
    }
    else if (!g_mods_enabled)
    {
        grl_draw_text ("[NO MODS]", WINDOW_WIDTH - 120, 15, 20, gray);
    }
    else
    {
        grl_draw_text ("[BASE ONLY]", WINDOW_WIDTH - 130, 15, 20, gray);
    }

    /* Footer background */
    grl_draw_rectangle (0, WINDOW_HEIGHT - 70, WINDOW_WIDTH, 70, bg_color);

    /* Creature count */
    if (g_dlc_creature_count > 0)
    {
        count_text = g_strdup_printf ("Creatures: %d (%d base + %d DLC)",
                                       g_creature_count,
                                       g_base_creature_count,
                                       g_dlc_creature_count);
    }
    else
    {
        count_text = g_strdup_printf ("Creatures: %d (base only)",
                                       g_creature_count);
    }
    grl_draw_text (count_text, 20, WINDOW_HEIGHT - 55, 16, white);
    g_free (count_text);

    /* Controls */
    grl_draw_text ("Press M to toggle mod info | Press R to respawn | Press ESC to exit",
                   20, WINDOW_HEIGHT - 30, 14, gray);

    /* Mod info overlay */
    if (g_show_mod_info)
    {
        g_autoptr(GrlColor) overlay_bg = grl_color_new (0, 0, 0, 200);
        guint i;
        gint y_offset = 100;

        grl_draw_rectangle (50, 70, WINDOW_WIDTH - 100, WINDOW_HEIGHT - 180, overlay_bg);

        grl_draw_text ("=== Mod Information ===", 70, 85, 18, white);

        status_text = g_strdup_printf ("Mod loading: %s",
                                        g_mods_enabled ? "ENABLED" : "DISABLED");
        grl_draw_text (status_text, 70, y_offset, 14,
                       g_mods_enabled ? green : red);
        g_free (status_text);
        y_offset += 25;

        status_text = g_strdup_printf ("DLC detected: %s",
                                        g_dlc_loaded ? "YES" : "NO");
        grl_draw_text (status_text, 70, y_offset, 14,
                       g_dlc_loaded ? green : gray);
        g_free (status_text);
        y_offset += 35;

        grl_draw_text ("Registered Creature Types:", 70, y_offset, 16, white);
        y_offset += 25;

        if (g_creature_types != NULL)
        {
            for (i = 0; i < g_creature_types->len; i++)
            {
                CreatureType *type = g_ptr_array_index (g_creature_types, i);
                g_autoptr(GrlColor) type_color = grl_color_new (type->r, type->g, type->b, 255);

                status_text = g_strdup_printf ("  %s - Speed: %.1fx (%s)",
                                                type->name, type->speed,
                                                type->from_mod ? "DLC" : "base");
                grl_draw_text (status_text, 70, y_offset, 14,
                               type->from_mod ? gold : white);
                g_free (status_text);

                /* Color preview */
                grl_draw_rectangle (WINDOW_WIDTH - 120, y_offset - 2, 40, 16, type_color);

                y_offset += 22;
            }
        }
    }
}

/*
 * load_mods:
 *
 * Discovers and loads mods from the mods directory.
 */
static void
load_mods (void)
{
    g_autoptr(LrgModManager) mod_manager = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *mods_path = NULL;
    GPtrArray *mods;
    guint i;

    if (!g_mods_enabled)
    {
        g_print ("Mod loading disabled (--no-mods)\n");
        return;
    }

    mod_manager = lrg_mod_manager_new ();

    /* Set up mod search path - look in examples/mods/ */
    mods_path = g_build_filename (g_get_current_dir (), "mods", NULL);
    g_print ("Searching for mods in: %s\n", mods_path);

    lrg_mod_manager_add_search_path (mod_manager, mods_path);

    /* Discover mods */
    if (!lrg_mod_manager_discover (mod_manager, &error))
    {
        g_print ("Mod discovery failed: %s\n", error->message);
        return;
    }

    /* Load all discovered mods */
    if (!lrg_mod_manager_load_all (mod_manager, &error))
    {
        g_print ("Mod loading failed: %s\n", error->message);
        /* Continue anyway - some mods may have loaded */
    }

    /* Check what loaded */
    mods = lrg_mod_manager_get_loaded_mods (mod_manager);
    if (mods != NULL && mods->len > 0)
    {
        g_dlc_loaded = TRUE;
        g_print ("Loaded %u mod(s):\n", mods->len);

        for (i = 0; i < mods->len; i++)
        {
            LrgMod *mod = g_ptr_array_index (mods, i);
            LrgModManifest *manifest = lrg_mod_get_manifest (mod);
            g_print ("  - %s v%s\n",
                     lrg_mod_manifest_get_name (manifest),
                     lrg_mod_manifest_get_version (manifest));
        }
    }
    else
    {
        g_print ("No mods found or loaded.\n");
    }
}

/*
 * cleanup_creature_types:
 *
 * Frees all registered creature types.
 */
static void
cleanup_creature_types (void)
{
    if (g_creature_types != NULL)
    {
        guint i;
        for (i = 0; i < g_creature_types->len; i++)
        {
            CreatureType *type = g_ptr_array_index (g_creature_types, i);
            g_free (type->name);
        }
        g_ptr_array_free (g_creature_types, TRUE);
        g_creature_types = NULL;
    }
}

/*
 * print_help:
 *
 * Prints usage information.
 */
static void
print_help (const gchar *program_name)
{
    g_print ("Usage: %s [OPTIONS]\n\n", program_name);
    g_print ("Creature Collector - Native GModule DLC Demo\n\n");
    g_print ("Options:\n");
    g_print ("  --no-mods    Run without loading any mods/DLC\n");
    g_print ("  --help       Show this help message\n");
    g_print ("  --license    Show license information\n\n");
    g_print ("Controls:\n");
    g_print ("  M            Toggle mod information overlay\n");
    g_print ("  R            Respawn all creatures\n");
    g_print ("  ESC          Exit the game\n");
}

/*
 * print_license:
 *
 * Prints license information.
 */
static void
print_license (void)
{
    g_print ("Creature Collector - Native GModule DLC Demo\n");
    g_print ("Copyright 2025 Zach Podbielniak\n\n");
    g_print ("SPDX-License-Identifier: AGPL-3.0-or-later\n\n");
    g_print ("This program is free software: you can redistribute it and/or modify\n");
    g_print ("it under the terms of the GNU Affero General Public License as published\n");
    g_print ("by the Free Software Foundation, either version 3 of the License, or\n");
    g_print ("(at your option) any later version.\n");
}

int
main (int   argc,
      char *argv[])
{
    g_autoptr(GrlWindow) window = NULL;
    g_autoptr(GrlColor) bg_color = NULL;
    gint i;

    /* Parse command line arguments */
    for (i = 1; i < argc; i++)
    {
        if (g_strcmp0 (argv[i], "--no-mods") == 0)
        {
            g_mods_enabled = FALSE;
        }
        else if (g_strcmp0 (argv[i], "--help") == 0 ||
                 g_strcmp0 (argv[i], "-h") == 0)
        {
            print_help (argv[0]);
            return 0;
        }
        else if (g_strcmp0 (argv[i], "--license") == 0)
        {
            print_license ();
            return 0;
        }
    }

    /* Initialize window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "Creature Collector - GModule DLC Demo");
    grl_window_set_target_fps (window, 60);

    bg_color = grl_color_new (30, 30, 40, 255);

    /* Register base creatures first */
    g_print ("=== Registering base creatures ===\n");
    register_base_creatures ();

    /* Load mods (which may register additional creatures) */
    g_print ("=== Loading mods ===\n");
    load_mods ();

    /* Spawn creatures */
    g_print ("=== Spawning creatures ===\n");
    spawn_all_creatures ();

    g_print ("=== Starting game loop ===\n");
    g_print ("Total creatures: %d (%d base + %d DLC)\n",
             g_creature_count, g_base_creature_count, g_dlc_creature_count);

    /* Main game loop */
    while (!grl_window_should_close (window))
    {
        gfloat delta = grl_window_get_frame_time (window);

        /* Handle input */
        if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
            break;

        if (grl_input_is_key_pressed (GRL_KEY_M))
            g_show_mod_info = !g_show_mod_info;

        if (grl_input_is_key_pressed (GRL_KEY_R))
            spawn_all_creatures ();

        /* Update */
        update_creatures (delta);

        /* Draw */
        grl_window_begin_drawing (window);
        grl_draw_clear_background (bg_color);

        draw_creatures ();
        draw_creature_labels ();
        draw_ui ();

        grl_window_end_drawing (window);
    }

    /* Cleanup */
    cleanup_creature_types ();

    g_print ("Goodbye!\n");
    return 0;
}
