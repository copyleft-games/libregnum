/* game-procgen-dungeon.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A procedurally generated dungeon crawl demonstrating the procgen module:
 *
 * - LrgProcgenDungeon: a YAML recipe (generator + post-processing + spawn
 *   rules + tile rules), the same one a game ships as dungeon.yaml
 * - LrgProcgenBsp / Caves / Wfc / Drunkard / Noise: the generators
 * - LrgProcgenTileRules: cell tags -> tile ids with cardinal autotiling
 *   (wall faces next to the floor are drawn lighter)
 * - lrg_procgen_grid_spawn_entities(): spawn points -> LrgGameObjects in
 *   an LrgWorld through a callback
 * - lrg_procgen_grid_to_nav_grid() + LrgPathfinder: the monsters' path to
 *   the player
 *
 * Walk with the arrow keys to the green exit; chests (yellow) are picked
 * up, monsters (red) step towards you along A* paths every few moves.
 * R makes a new map, 1-5 switch the generator, Esc quits.
 *
 * Usage examples:
 *   game-procgen-dungeon                       play, BSP recipe, seed 1
 *   game-procgen-dungeon --seed 42 -g caves    caves from seed 42
 *   game-procgen-dungeon --recipe dungeon.yaml your own recipe
 *   game-procgen-dungeon --print -g wfc        print one map as text, no window
 *   game-procgen-dungeon --dump-recipe         print the YAML recipe in use
 */

#include <libregnum.h>
#include <graylib.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_WIDTH   (1024)
#define WINDOW_HEIGHT  (764)
#define CELL           (16)
#define MAP_WIDTH      (64)
#define MAP_HEIGHT     (44)
#define HUD_Y          (MAP_HEIGHT * CELL + 4)

/* tile ids of the recipe's tile rules (tilemap ids are 1-based) */
#define TILE_FLOOR     (1)
#define TILE_CORRIDOR  (2)
#define TILE_DOOR      (3)
#define TILE_WATER     (4)
#define TILE_WALL      (16) /* 16 cardinal variants: 16..31 */

static const gchar *license_text =
	"game-procgen-dungeon - a libregnum procgen example\n"
	"Copyright (C) 2026 Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/*
 * The recipe: everything but the generator section is shared by the five
 * generators (see build_recipe()). This is exactly what a game would keep
 * in data/dungeon.yaml.
 */
static const gchar *recipe_common =
	"version: 1\n"
	"width: 64\n"
	"height: 44\n"
	"post:\n"
	"  cull-min-size: 6\n"
	"  connect: true\n"
	"  start-exit: true\n"
	"spawns:\n"
	"  - { kind: monster, count: 8, min-distance: 12 }\n"
	"  - { kind: chest, count: 6 }\n"
	"tiles:\n"
	"  default: 0\n"
	"  rules:\n"
	"    - { tag: floor, tile: 1 }\n"
	"    - { tag: corridor, tile: 2 }\n"
	"    - { tag: door, tile: 3 }\n"
	"    - { tag: water, tile: 4 }\n"
	"    - { tag: wall, tile: 16, autotile: cardinal }\n";

static const gchar *generator_sections[] = {
	"generator: bsp\nparams: { min-room-size: 4, max-room-size: 10, min-leaf-size: 9 }\n",
	"generator: caves\nparams: { fill-percent: 46, iterations: 5, min-region-size: 20 }\n",
	"generator: wfc\nparams: { border-tile: wall }\nwfc:\n  sample: |\n"
	"    ############\n"
	"    #....#.....#\n"
	"    #....+.....#\n"
	"    #....#.....#\n"
	"    ###+####+###\n"
	"    #,,,,,,,,,,#\n"
	"    #,########,#\n"
	"    ############\n",
	"generator: drunkard\nparams: { coverage-percent: 38, walk-length: 200 }\n",
	"generator: noise\nparams: { octaves: 4, scale: 16, water-percent: 30, wall-percent: 68 }\n",
};

static const gchar *generator_names[] = { "bsp", "caves", "wfc", "drunkard", "noise" };

/* =============================================================================
 * Game state
 * ========================================================================== */

typedef struct
{
	LrgProcgenDungeon *recipe;
	LrgProcgenGrid    *grid;
	LrgTilemapLayer   *layer;
	LrgNavGrid        *nav;
	LrgPathfinder     *pathfinder;
	LrgWorld          *world;
	guint64            seed;
	guint              generator;
	gint               player_x;
	gint               player_y;
	guint              chests;
	guint              moves;
	guint              depth;
	gboolean           caught;
	gchar             *message;
} Game;

/* builds the recipe of one generator from the shared YAML */
static LrgProcgenDungeon *
build_recipe (guint     generator,
              GError  **error)
{
	g_autofree gchar *yaml = g_strconcat (generator_sections[generator], recipe_common, NULL);

	return lrg_procgen_dungeon_new_from_data (yaml, -1, error);
}

/* the spawn hook: monsters and chests become tagged game objects */
static LrgGameObject *
spawn_entity (LrgProcgenGrid        *grid,
              const LrgProcgenSpawn *spawn,
              gfloat                 world_x,
              gfloat                 world_y,
              gpointer               user_data)
{
	LrgGameObject *object = lrg_game_object_new_at ((gfloat) spawn->x, (gfloat) spawn->y);

	/* positions are kept in cells; drawing scales them */
	grl_entity_set_tag (GRL_ENTITY (object), spawn->kind);
	return object;
}

/* generates the current map and fills the world */
static gboolean
game_generate (Game    *game,
               GError **error)
{
	g_autoptr(LrgProcgenGrid) grid = NULL;
	gint sx;
	gint sy;

	grid = lrg_procgen_dungeon_generate (game->recipe, game->seed, error);
	if (grid == NULL)
		return FALSE;

	g_clear_object (&game->grid);
	g_clear_object (&game->layer);
	g_clear_object (&game->pathfinder);
	g_clear_object (&game->nav);
	game->grid = g_steal_pointer (&grid);
	game->layer = lrg_procgen_dungeon_build_layer (game->recipe, game->grid);
	game->nav = lrg_procgen_grid_to_nav_grid (game->grid);
	lrg_nav_grid_set_allow_diagonal (game->nav, FALSE);
	game->pathfinder = lrg_pathfinder_new (game->nav);

	lrg_world_clear (game->world);
	lrg_procgen_grid_spawn_entities (game->grid, game->world, NULL, 1.0f, spawn_entity, NULL);

	lrg_procgen_grid_get_start (game->grid, &sx, &sy);
	game->player_x = sx;
	game->player_y = sy;
	game->caught = FALSE;
	g_clear_pointer (&game->message, g_free);
	game->message = g_strdup_printf ("%s, seed %" G_GUINT64_FORMAT ", %u rooms, %u spawns",
	                                 generator_names[game->generator], game->seed,
	                                 lrg_procgen_grid_get_room_count (game->grid),
	                                 lrg_procgen_grid_get_spawn_count (game->grid));
	return TRUE;
}

/* switches generator (rebuilding the recipe) and regenerates */
static void
game_set_generator (Game  *game,
                    guint  generator)
{
	g_autoptr(GError) error = NULL;
	LrgProcgenDungeon *recipe = build_recipe (generator, &error);

	if (recipe == NULL)
	{
		g_printerr ("recipe: %s\n", error->message);
		return;
	}
	g_clear_object (&game->recipe);
	game->recipe = recipe;
	game->generator = generator;
	if (!game_generate (game, &error))
		g_printerr ("generate: %s\n", error->message);
}

/* every third move, each monster takes one step along its A* path */
static void
move_monsters (Game *game)
{
	GList *objects = lrg_world_get_objects (game->world);
	GList *l;

	for (l = objects; l != NULL; l = l->next)
	{
		GrlEntity *entity = l->data;
		g_autoptr(LrgPath) path = NULL;
		gint mx;
		gint my;
		gint nx;
		gint ny;

		if (g_strcmp0 (grl_entity_get_tag (entity), "monster") != 0)
			continue;
		mx = (gint) grl_entity_get_x (entity);
		my = (gint) grl_entity_get_y (entity);
		if (ABS (mx - game->player_x) + ABS (my - game->player_y) > 14)
			continue; /* asleep */
		path = lrg_pathfinder_find_path (game->pathfinder, mx, my,
		                                 game->player_x, game->player_y, NULL);
		if (path == NULL || !lrg_path_get_point (path, 1, &nx, &ny))
			continue;
		grl_entity_set_position_xy (entity, (gfloat) nx, (gfloat) ny);
		if (nx == game->player_x && ny == game->player_y)
			game->caught = TRUE;
	}
	g_list_free (objects);
}

/* moves the player one cell; picks up chests, reaches the exit */
static void
game_step (Game *game,
           gint  dx,
           gint  dy)
{
	GList *objects;
	GList *l;
	gint nx = game->player_x + dx;
	gint ny = game->player_y + dy;
	gint ex;
	gint ey;

	if (game->caught || !lrg_procgen_grid_is_walkable (game->grid, nx, ny))
		return;
	game->player_x = nx;
	game->player_y = ny;
	game->moves++;

	/* entities on the new cell */
	objects = lrg_world_get_objects (game->world);
	for (l = objects; l != NULL; l = l->next)
	{
		GrlEntity *entity = l->data;

		if ((gint) grl_entity_get_x (entity) != nx || (gint) grl_entity_get_y (entity) != ny)
			continue;
		if (g_strcmp0 (grl_entity_get_tag (entity), "chest") == 0)
		{
			game->chests++;
			lrg_world_remove_object (game->world, LRG_GAME_OBJECT (entity));
		}
		else
			game->caught = TRUE;
	}
	g_list_free (objects);

	if (game->moves % 3 == 0)
		move_monsters (game);

	/* the exit: one level deeper, a new seed */
	lrg_procgen_grid_get_exit (game->grid, &ex, &ey);
	if (nx == ex && ny == ey)
	{
		g_autoptr(GError) error = NULL;

		game->depth++;
		game->seed++;
		if (!game_generate (game, &error))
			g_printerr ("generate: %s\n", error->message);
	}
}

/* =============================================================================
 * Drawing
 * ========================================================================== */

static void
draw_map (Game *game)
{
	g_autoptr(GrlColor) floor = grl_color_new (58, 52, 48, 255);
	g_autoptr(GrlColor) corridor = grl_color_new (48, 46, 52, 255);
	g_autoptr(GrlColor) door = grl_color_new (140, 90, 40, 255);
	g_autoptr(GrlColor) water = grl_color_new (30, 70, 140, 255);
	g_autoptr(GrlColor) rock = grl_color_new (16, 14, 20, 255);
	g_autoptr(GrlColor) face = grl_color_new (92, 84, 96, 255);
	guint x;
	guint y;

	for (y = 0; y < MAP_HEIGHT; y++)
		for (x = 0; x < MAP_WIDTH; x++)
		{
			guint tile = lrg_tilemap_layer_get_tile (game->layer, x, y);
			const GrlColor *color;

			/*
			 * Cardinal autotiling: TILE_WALL + 15 is a wall surrounded by
			 * walls (solid rock); any other variant has an open side and is
			 * drawn as a lit wall face.
			 */
			if (tile == TILE_FLOOR)
				color = floor;
			else if (tile == TILE_CORRIDOR)
				color = corridor;
			else if (tile == TILE_DOOR)
				color = door;
			else if (tile == TILE_WATER)
				color = water;
			else if (tile == TILE_WALL + 15)
				color = rock;
			else
				color = face;
			grl_draw_rectangle ((gint) x * CELL, (gint) y * CELL, CELL, CELL, color);
		}
}

static void
draw_entities (Game *game)
{
	g_autoptr(GrlColor) monster = grl_color_new (210, 60, 60, 255);
	g_autoptr(GrlColor) chest = grl_color_new (235, 200, 60, 255);
	g_autoptr(GrlColor) exit_color = grl_color_new (60, 220, 110, 255);
	g_autoptr(GrlColor) player = grl_color_new (240, 240, 250, 255);
	GList *objects = lrg_world_get_objects (game->world);
	GList *l;
	gint ex;
	gint ey;

	lrg_procgen_grid_get_exit (game->grid, &ex, &ey);
	grl_draw_rectangle (ex * CELL + 2, ey * CELL + 2, CELL - 4, CELL - 4, exit_color);
	for (l = objects; l != NULL; l = l->next)
	{
		GrlEntity *entity = l->data;
		gint x = (gint) grl_entity_get_x (entity) * CELL + CELL / 2;
		gint y = (gint) grl_entity_get_y (entity) * CELL + CELL / 2;

		if (g_strcmp0 (grl_entity_get_tag (entity), "chest") == 0)
			grl_draw_rectangle (x - 4, y - 4, 8, 8, chest);
		else
			grl_draw_circle (x, y, CELL * 0.4f, monster);
	}
	g_list_free (objects);
	grl_draw_circle (game->player_x * CELL + CELL / 2, game->player_y * CELL + CELL / 2,
	                 CELL * 0.4f, player);
}

static void
draw_hud (Game *game)
{
	g_autoptr(GrlColor) text = grl_color_new (220, 220, 220, 255);
	g_autoptr(GrlColor) warn = grl_color_new (240, 90, 90, 255);
	g_autofree gchar *status = NULL;

	status = g_strdup_printf ("depth %u  chests %u  |  %s", game->depth, game->chests, game->message);
	grl_draw_text (status, 8, HUD_Y, 18, text);
	grl_draw_text (game->caught ? "Caught! R for a new map"
	                            : "arrows move  R new map  1 bsp 2 caves 3 wfc 4 drunkard 5 noise",
	               8, HUD_Y + 24, 16, game->caught ? warn : text);
}

/* =============================================================================
 * Main
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *recipe_path = NULL;
	g_autofree gchar *generator_name = NULL;
	gint64 seed = 1;
	gboolean print = FALSE;
	gboolean dump = FALSE;
	gboolean license = FALSE;
	Game game;
	GrlWindow *window;
	guint i;
	GOptionEntry entries[] = {
		{ "seed", 's', 0, G_OPTION_ARG_INT64, &seed, "Map seed (default 1)", "N" },
		{ "generator", 'g', 0, G_OPTION_ARG_STRING, &generator_name,
		  "bsp, caves, wfc, drunkard or noise (default bsp)", "NAME" },
		{ "recipe", 'r', 0, G_OPTION_ARG_FILENAME, &recipe_path, "Load a YAML recipe", "FILE" },
		{ "print", 'p', 0, G_OPTION_ARG_NONE, &print, "Print one map as text and exit (no window)", NULL },
		{ "dump-recipe", 0, 0, G_OPTION_ARG_NONE, &dump, "Print the YAML recipe and exit", NULL },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &license, "Show the license and exit", NULL },
		{ NULL }
	};

	/* --- command line --- */
	context = g_option_context_new ("- a procedurally generated dungeon crawl");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_set_description (context,
		"Examples:\n"
		"  game-procgen-dungeon                       play, BSP recipe, seed 1\n"
		"  game-procgen-dungeon --seed 42 -g caves    caves from seed 42\n"
		"  game-procgen-dungeon --recipe dungeon.yaml your own recipe\n"
		"  game-procgen-dungeon --print -g wfc        print one map as text\n"
		"  game-procgen-dungeon --dump-recipe         print the recipe in use\n");
	if (!g_option_context_parse (context, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 1;
	}
	if (license)
	{
		g_print ("%s", license_text);
		return 0;
	}

	memset (&game, 0, sizeof game);
	game.seed = (guint64) seed;
	game.world = lrg_world_new ();
	if (generator_name != NULL)
	{
		for (i = 0; i < G_N_ELEMENTS (generator_names); i++)
			if (g_strcmp0 (generator_name, generator_names[i]) == 0)
				game.generator = i;
	}

	/* --- the recipe: a file, or the built-in one --- */
	if (recipe_path != NULL)
		game.recipe = lrg_procgen_dungeon_new_from_file (recipe_path, &error);
	else
		game.recipe = build_recipe (game.generator, &error);
	if (game.recipe == NULL)
	{
		g_printerr ("recipe: %s\n", error->message);
		return 1;
	}
	if (dump)
	{
		g_autofree gchar *yaml = lrg_procgen_dungeon_to_data (game.recipe, &error);

		g_print ("%s", yaml != NULL ? yaml : "");
		return yaml != NULL ? 0 : 1;
	}
	if (!game_generate (&game, &error))
	{
		g_printerr ("generate: %s\n", error->message);
		return 1;
	}
	if (print)
	{
		g_autofree gchar *text = lrg_procgen_grid_to_string (game.grid);

		g_print ("%s\n%s", game.message, text);
		return 0;
	}

	/* --- the game loop --- */
	window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "Libregnum - Procgen Dungeon");
	grl_window_set_target_fps (window, 60);
	while (!grl_window_should_close (window))
	{
		if (grl_input_is_key_pressed_repeat (GRL_KEY_UP) || grl_input_is_key_pressed (GRL_KEY_UP))
			game_step (&game, 0, -1);
		if (grl_input_is_key_pressed_repeat (GRL_KEY_DOWN) || grl_input_is_key_pressed (GRL_KEY_DOWN))
			game_step (&game, 0, 1);
		if (grl_input_is_key_pressed_repeat (GRL_KEY_LEFT) || grl_input_is_key_pressed (GRL_KEY_LEFT))
			game_step (&game, -1, 0);
		if (grl_input_is_key_pressed_repeat (GRL_KEY_RIGHT) || grl_input_is_key_pressed (GRL_KEY_RIGHT))
			game_step (&game, 1, 0);
		if (grl_input_is_key_pressed (GRL_KEY_R))
		{
			game.seed++;
			game.chests = 0;
			game.depth = 0;
			game_set_generator (&game, game.generator);
		}
		for (i = 0; i < G_N_ELEMENTS (generator_names); i++)
			if (grl_input_is_key_pressed ((GrlKey) (GRL_KEY_ONE + (gint) i)))
				game_set_generator (&game, i);

		grl_window_begin_drawing (window);
		{
			g_autoptr(GrlColor) background = grl_color_new (10, 10, 14, 255);

			grl_draw_clear_background (background);
		}
		draw_map (&game);
		draw_entities (&game);
		draw_hud (&game);
		grl_window_end_drawing (window);
	}

	g_object_unref (window);
	g_clear_object (&game.pathfinder);
	g_clear_object (&game.nav);
	g_clear_object (&game.layer);
	g_clear_object (&game.grid);
	g_clear_object (&game.recipe);
	g_clear_object (&game.world);
	g_free (game.message);
	return 0;
}
