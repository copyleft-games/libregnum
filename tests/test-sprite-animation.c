/* test-sprite-animation.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for sprite frame animations:
 *
 * - LrgSpriteAnimation clips: durations, sampling for once / loop /
 *   ping-pong, events kept on their frames through edits;
 * - LrgSpriteSheet slicing: grid (margins, spacing, pivots) and alpha
 *   islands;
 * - the .rsprite format: byte-stable round trip, overrides, unknown keys,
 *   errors with key paths;
 * - LrgSpriteAnimationPlayer: frames, events across loops, finished,
 *   speed/pause, driving an LrgSpriteComponent on a game object;
 * - registering grid clips with LrgAnimatorComponent.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <string.h>

/* ==========================================================================
 * Clips
 * ========================================================================== */

static void
test_clip_sampling (void)
{
	g_autoptr(LrgSpriteAnimation) clip = lrg_sprite_animation_new ("walk");
	gboolean finished = TRUE;
	guint i;

	/* 4 frames at 10 fps; frame 2 holds 0.3 s */
	for (i = 0; i < 4; i++)
		lrg_sprite_animation_add_frame (clip, 10 + i, i == 2 ? 0.3f : 0.0f);
	g_assert_cmpuint (lrg_sprite_animation_get_n_frames (clip), ==, 4);
	g_assert_cmpfloat_with_epsilon (lrg_sprite_animation_get_frame_duration (clip, 0), 0.1, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_sprite_animation_get_duration (clip), 0.6, 1e-6);

	/* loop */
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.05f, &finished), ==, 0);
	g_assert_false (finished);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.25f, NULL), ==, 2);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.45f, NULL), ==, 2);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.55f, NULL), ==, 3);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.65f, NULL), ==, 0);

	/* once: holds the last frame */
	lrg_sprite_animation_set_loop_mode (clip, LRG_SPRITE_LOOP_ONCE);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 5.0f, &finished), ==, 3);
	g_assert_true (finished);

	/* ping-pong: 0 1 2 3 2 1, without doubled ends */
	lrg_sprite_animation_set_loop_mode (clip, LRG_SPRITE_LOOP_PING_PONG);
	lrg_sprite_animation_set_frame (clip, 2, 12, 0.0f);
	g_assert_cmpfloat_with_epsilon (lrg_sprite_animation_get_duration (clip), 0.6, 1e-6);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.35f, NULL), ==, 3);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.45f, NULL), ==, 2);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.55f, NULL), ==, 1);
	g_assert_cmpuint (lrg_sprite_animation_sample (clip, 0.65f, NULL), ==, 0);
}

static void
test_clip_events (void)
{
	g_autoptr(LrgSpriteAnimation) clip = lrg_sprite_animation_new ("attack");
	guint frame = 99;
	guint i;

	for (i = 0; i < 5; i++)
		lrg_sprite_animation_add_frame (clip, i, 0.0f);
	lrg_sprite_animation_add_event (clip, 3, "hit");
	lrg_sprite_animation_add_event (clip, 1, "swing");
	lrg_sprite_animation_add_event (clip, 3, "shake");

	/* sorted by frame, insertion order within a frame */
	g_assert_cmpstr (lrg_sprite_animation_get_event (clip, 0, &frame), ==, "swing");
	g_assert_cmpstr (lrg_sprite_animation_get_event (clip, 1, NULL), ==, "hit");
	g_assert_cmpstr (lrg_sprite_animation_get_event (clip, 2, NULL), ==, "shake");

	/* inserting before frame 3 moves its events along */
	lrg_sprite_animation_insert_frame (clip, 2, 7, 0.0f);
	lrg_sprite_animation_get_event (clip, 1, &frame);
	g_assert_cmpuint (frame, ==, 4);

	/* removing a frame drops its events, shifts later ones back */
	g_assert_true (lrg_sprite_animation_remove_frame (clip, 1));
	g_assert_cmpuint (lrg_sprite_animation_get_n_events (clip), ==, 2);
	lrg_sprite_animation_get_event (clip, 0, &frame);
	g_assert_cmpuint (frame, ==, 3);

	g_assert_true (lrg_sprite_animation_remove_event (clip, 0));
	g_assert_cmpuint (lrg_sprite_animation_get_n_events (clip), ==, 1);
}

/* ==========================================================================
 * Slicing
 * ========================================================================== */

static void
test_slice_grid (void)
{
	g_autoptr(LrgSpriteSheet) sheet = lrg_sprite_sheet_new ("hero");
	LrgAtlasRegion *frame;
	gint fw = 0;
	guint columns = 0;
	guint count = 0;

	g_assert_false (lrg_sprite_sheet_get_grid (sheet, NULL, NULL, NULL, NULL));
	g_assert_cmpuint (lrg_sprite_sheet_slice_grid (sheet, 32, 48, 4, 2, 7, 2, 3, 1, 4, 16.0f, 48.0f), ==, 7);
	g_assert_true (lrg_sprite_sheet_get_grid (sheet, &fw, NULL, &columns, &count));
	g_assert_cmpint (fw, ==, 32);
	g_assert_cmpuint (columns, ==, 4);
	g_assert_cmpuint (count, ==, 7);

	frame = lrg_sprite_sheet_get_frame (sheet, 5);
	g_assert_cmpstr (lrg_atlas_region_get_name (frame), ==, "frame_5");
	g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 2 + 1 * 33);
	g_assert_cmpint (lrg_atlas_region_get_y (frame), ==, 3 + 1 * 52);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_y (frame), ==, 48.0f);
}

static void
test_slice_alpha (void)
{
	g_autoptr(LrgSpriteSheet) sheet = lrg_sprite_sheet_new ("props");
	g_autofree guint8 *rgba = g_new0 (guint8, 32 * 32 * 4);
	LrgAtlasRegion *frame;
	gint x;
	gint y;

	/* three blobs: two on the top row, one below; a stray pixel too small */
	for (y = 2; y < 8; y++)
		for (x = 20; x < 26; x++)
			rgba[(y * 32 + x) * 4 + 3] = 255;
	for (y = 3; y < 10; y++)
		for (x = 2; x < 6; x++)
			rgba[(y * 32 + x) * 4 + 3] = 255;
	for (y = 20; y < 30; y++)
		for (x = 10; x < 12; x++)
			rgba[(y * 32 + x) * 4 + 3] = 255;
	/* a diagonal touch joins (8-connected) */
	rgba[(10 * 32 + 6) * 4 + 3] = 255;
	rgba[(31 * 32 + 31) * 4 + 3] = 255;

	g_assert_cmpuint (lrg_sprite_sheet_slice_alpha (sheet, rgba, 32 * 32 * 4, 32, 32, 0, 2, 0.5f, 1.0f), ==, 3);
	frame = lrg_sprite_sheet_get_frame (sheet, 0);
	g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 2);
	g_assert_cmpint (lrg_atlas_region_get_width (frame), ==, 5);
	g_assert_cmpint (lrg_atlas_region_get_height (frame), ==, 8);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_x (frame), ==, 2.5f);
	frame = lrg_sprite_sheet_get_frame (sheet, 1);
	g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 20);
	frame = lrg_sprite_sheet_get_frame (sheet, 2);
	g_assert_cmpint (lrg_atlas_region_get_y (frame), ==, 20);
	g_assert_false (lrg_sprite_sheet_get_grid (sheet, NULL, NULL, NULL, NULL));
}

/* ==========================================================================
 * .rsprite
 * ========================================================================== */

static const gchar *rsprite_fixture =
	"version: 1\n"
	"name: hero\n"
	"image: hero.png\n"
	"image_size:\n"
	"- 128\n"
	"- 96\n"
	"grid:\n"
	"  frame_size:\n"
	"  - 32\n"
	"  - 32\n"
	"  columns: 4\n"
	"  rows: 2\n"
	"  count: 6\n"
	"  pivot:\n"
	"  - 16\n"
	"  - 32\n"
	"frames:\n"
	"- index: 3\n"
	"  name: hero_hit\n"
	"  pivot:\n"
	"  - 12\n"
	"  - 32\n"
	"- name: sword\n"
	"  rect:\n"
	"  - 0\n"
	"  - 64\n"
	"  - 48\n"
	"  - 16\n"
	"  pivot:\n"
	"  - 0\n"
	"  - 8.5\n"
	"- rect:\n"
	"  - 48\n"
	"  - 64\n"
	"  - 16\n"
	"  - 16\n"
	"animations:\n"
	"- name: walk\n"
	"  fps: 12\n"
	"  frames:\n"
	"  - 0\n"
	"  - 1\n"
	"  - 2\n"
	"  events:\n"
	"  - frame: 1\n"
	"    name: footstep\n"
	"- name: hit\n"
	"  loop: once\n"
	"  frames:\n"
	"  - hero_hit\n"
	"  - frame: sword\n"
	"    duration: 0.25\n"
	"  - 7\n"
	"  blend: additive\n"
	"- name: idle\n"
	"  loop: ping-pong\n"
	"  frames:\n"
	"  - 4\n"
	"  - 5\n"
	"author: zach\n";

static void
test_rsprite_round_trip (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	g_autoptr(LrgSpriteSheet) again = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *text2 = NULL;
	LrgSpriteAnimation *clip;
	LrgAtlasRegion *frame;
	gfloat duration = 0.0f;

	sheet = lrg_sprite_sheet_new_from_rsprite_data (rsprite_fixture, -1, "hero.rsprite", &error);
	g_assert_no_error (error);
	g_assert_nonnull (sheet);

	g_assert_cmpstr (lrg_sprite_sheet_get_name (sheet), ==, "hero");
	g_assert_cmpstr (lrg_sprite_sheet_get_texture_path (sheet), ==, "hero.png");
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (sheet), ==, 8);

	frame = lrg_sprite_sheet_get_frame (sheet, 3);
	g_assert_cmpstr (lrg_atlas_region_get_name (frame), ==, "hero_hit");
	g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 96);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_x (frame), ==, 12.0f);
	frame = lrg_sprite_sheet_get_frame_by_name (sheet, "sword");
	g_assert_nonnull (frame);
	g_assert_cmpint (lrg_atlas_region_get_width (frame), ==, 48);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_y (frame), ==, 8.5f);
	g_assert_cmpstr (lrg_atlas_region_get_name (lrg_sprite_sheet_get_frame (sheet, 7)), ==, "frame_7");

	g_assert_cmpuint (lrg_sprite_sheet_get_n_clips (sheet), ==, 3);
	clip = lrg_sprite_sheet_get_clip (sheet, "walk");
	g_assert_cmpfloat (lrg_sprite_animation_get_fps (clip), ==, 12.0f);
	g_assert_cmpuint (lrg_sprite_animation_get_n_events (clip), ==, 1);
	clip = lrg_sprite_sheet_get_clip (sheet, "hit");
	g_assert_cmpint (lrg_sprite_animation_get_loop_mode (clip), ==, LRG_SPRITE_LOOP_ONCE);
	g_assert_cmpuint (lrg_sprite_animation_get_frame (clip, 0, NULL), ==, 3);
	g_assert_cmpuint (lrg_sprite_animation_get_frame (clip, 1, &duration), ==, 6);
	g_assert_cmpfloat (duration, ==, 0.25f);
	g_assert_true (lrg_sprite_sheet_get_clip_at (sheet, 2) == lrg_sprite_sheet_get_clip (sheet, "idle"));

	/* the fixture is the writer's layout, byte for byte */
	text = lrg_sprite_sheet_save_rsprite_to_data (sheet, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, rsprite_fixture);

	again = lrg_sprite_sheet_new_from_rsprite_data (text, -1, NULL, &error);
	g_assert_no_error (error);
	text2 = lrg_sprite_sheet_save_rsprite_to_data (again, NULL, &error);
	g_assert_cmpstr (text2, ==, text);
}

static void
check_error (const gchar *text,
             const gchar *expected)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgSpriteSheet) sheet = NULL;

	sheet = lrg_sprite_sheet_new_from_rsprite_data (text, -1, "bad.rsprite", &error);
	g_assert_null (sheet);
	g_assert_error (error, LRG_SPRITE_ANIMATION_ERROR, LRG_SPRITE_ANIMATION_ERROR_INVALID);
	if (strstr (error->message, expected) == NULL)
		g_error ("expected \"%s\" in \"%s\"", expected, error->message);
}

#define GRID "grid: {frame_size: [16, 16], columns: 2, rows: 2}\n"

static void
test_rsprite_errors (void)
{
	check_error ("grid: {frame_size: [16, 16], columns: 2}\n", "bad.rsprite: /grid: a grid needs frame_size, columns and rows");
	check_error ("grid: {frame_size: [16, 16], columns: 2, rows: 2, count: 5}\n", "/grid/count: count is larger");
	check_error ("grid: {frame_size: [16, 0], columns: 2, rows: 2}\n", "/grid/frame_size/1: expected a whole number from 1");
	check_error (GRID "frames:\n- index: 4\n", "/frames/0/index: expected a whole number from 0 to 3");
	check_error (GRID "frames:\n- index: 1\n  rect: [0, 0, 4, 4]\n", "/frames/0: a grid frame override cannot change rect");
	check_error (GRID "frames:\n- name: a\n", "/frames/0: missing key \"rect\"");
	check_error (GRID "frames:\n- name: frame_1\n  rect: [0, 0, 4, 4]\n", "/frames/0: frame name \"frame_1\" is used twice");
	check_error (GRID "animations:\n- name: a\n  frames: [0, 9]\n", "/animations/0/frames/1: frame 9 does not exist");
	check_error (GRID "animations:\n- name: a\n  frames: [nope]\n", "/animations/0/frames/0: no frame named \"nope\"");
	check_error (GRID "animations:\n- name: a\n  loop: forever\n  frames: [0]\n", "/animations/0/loop: expected once, loop or ping-pong");
	check_error (GRID "animations:\n- name: a\n  frames: [0]\n  events: [{frame: 1, name: x}]\n",
	             "/animations/0/events/0/frame: expected a whole number from 0 to 0");
	check_error (GRID "animations:\n- name: a\n  frames: [0]\n- name: a\n  frames: [1]\n",
	             "/animations/1: animation \"a\" is defined twice");
	check_error (GRID "animations:\n- name: a\n  fps: 0\n  frames: [0]\n", "/animations/0/fps: expected a number of at least");
}

static void
test_rsprite_file_and_edits (void)
{
	g_autoptr(LrgSpriteSheet) sheet = lrg_sprite_sheet_new ("coin");
	g_autoptr(LrgSpriteAnimation) spin = lrg_sprite_animation_new ("spin");
	g_autoptr(LrgSpriteSheet) loaded = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *text = NULL;

	lrg_sprite_sheet_slice_grid (sheet, 8, 8, 4, 1, 0, 0, 0, 0, 0, 4.0f, 4.0f);
	lrg_sprite_animation_add_frame (spin, 0, 0.0f);
	lrg_sprite_animation_add_frame (spin, 1, 0.0f);
	lrg_sprite_animation_set_fps (spin, 8.0f);
	lrg_sprite_sheet_add_clip (sheet, spin);

	dir = g_dir_make_tmp ("rsprite-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "coin.rsprite", NULL);
	g_assert_true (lrg_sprite_sheet_save_rsprite_to_file (sheet, path, &error));
	g_assert_no_error (error);
	loaded = lrg_sprite_sheet_new_from_rsprite_file (path, &error);
	g_assert_no_error (error);
	text = lrg_sprite_sheet_save_rsprite_to_data (loaded, NULL, &error);
	g_assert_cmpstr (text, ==,
	                 "version: 1\n"
	                 "name: coin\n"
	                 "grid:\n"
	                 "  frame_size:\n  - 8\n  - 8\n"
	                 "  columns: 4\n"
	                 "  rows: 1\n"
	                 "  pivot:\n  - 4\n  - 4\n"
	                 "animations:\n"
	                 "- name: spin\n"
	                 "  fps: 8\n"
	                 "  frames:\n  - 0\n  - 1\n");

	/* a clip with the same name replaces the old one in place */
	{
		g_autoptr(LrgSpriteAnimation) other = lrg_sprite_animation_new ("spin");

		lrg_sprite_sheet_add_clip (loaded, other);
		g_assert_cmpuint (lrg_sprite_sheet_get_n_clips (loaded), ==, 1);
		g_assert_true (lrg_sprite_sheet_get_clip (loaded, "spin") == other);
		g_assert_true (lrg_sprite_sheet_remove_clip (loaded, "spin"));
		g_assert_cmpuint (lrg_sprite_sheet_get_n_clips (loaded), ==, 0);
	}

	/* a missing file is an I/O error */
	g_clear_object (&loaded);
	g_unlink (path);
	loaded = lrg_sprite_sheet_new_from_rsprite_file (path, &error);
	g_assert_null (loaded);
	g_assert_error (error, LRG_SPRITE_ANIMATION_ERROR, LRG_SPRITE_ANIMATION_ERROR_IO);
	g_rmdir (dir);
}

/* ==========================================================================
 * Player
 * ========================================================================== */

typedef struct
{
	GString *events;
	guint    finished;
	guint    frame_changes;
} PlayerLog;

static void
on_event (LrgSpriteAnimationPlayer *player,
          const gchar              *name,
          guint                     clip_frame,
          PlayerLog                *log)
{
	(void) player;
	g_string_append_printf (log->events, "%s@%u ", name, clip_frame);
}

static void
on_finished (LrgSpriteAnimationPlayer *player,
             const gchar              *name,
             PlayerLog                *log)
{
	(void) player;
	(void) name;
	log->finished++;
}

static void
on_frame (LrgSpriteAnimationPlayer *player,
          guint                     frame,
          PlayerLog                *log)
{
	(void) player;
	(void) frame;
	log->frame_changes++;
}

static LrgSpriteSheet *
player_sheet (void)
{
	LrgSpriteSheet *sheet = lrg_sprite_sheet_new ("p");
	g_autoptr(LrgSpriteAnimation) run = lrg_sprite_animation_new ("run");
	g_autoptr(LrgSpriteAnimation) die = lrg_sprite_animation_new ("die");
	guint i;

	lrg_sprite_sheet_slice_grid (sheet, 16, 16, 4, 2, 0, 0, 0, 0, 0, 8.0f, 16.0f);
	for (i = 0; i < 4; i++)
		lrg_sprite_animation_add_frame (run, i, 0.0f);
	lrg_sprite_animation_add_event (run, 0, "start");
	lrg_sprite_animation_add_event (run, 2, "step");
	lrg_sprite_sheet_add_clip (sheet, run);

	lrg_sprite_animation_set_loop_mode (die, LRG_SPRITE_LOOP_ONCE);
	for (i = 4; i < 7; i++)
		lrg_sprite_animation_add_frame (die, i, 0.0f);
	lrg_sprite_sheet_add_clip (sheet, die);
	return sheet;
}

static void
test_player (void)
{
	g_autoptr(LrgSpriteSheet) sheet = player_sheet ();
	g_autoptr(LrgSpriteAnimationPlayer) player = lrg_sprite_animation_player_new (sheet);
	PlayerLog log = { g_string_new (NULL), 0, 0 };
	guint i;

	g_signal_connect (player, "event", G_CALLBACK (on_event), &log);
	g_signal_connect (player, "finished", G_CALLBACK (on_finished), &log);
	g_signal_connect (player, "frame-changed", G_CALLBACK (on_frame), &log);

	g_assert_false (lrg_sprite_animation_player_play (player, "nope"));
	g_assert_true (lrg_sprite_animation_player_play (player, "run"));
	g_assert_true (lrg_sprite_animation_player_is_playing (player));
	g_assert_cmpstr (log.events->str, ==, "start@0 ");

	/* 10 fps: 0.25 s lands on frame 2 */
	lrg_sprite_animation_player_advance (player, 0.25f);
	g_assert_cmpuint (lrg_sprite_animation_player_get_frame (player), ==, 2);
	g_assert_cmpint (lrg_atlas_region_get_x (lrg_sprite_animation_player_get_region (player)), ==, 32);
	g_assert_cmpstr (log.events->str, ==, "start@0 step@2 ");

	/* two whole loops in one hitch: every event of every pass fires */
	lrg_sprite_animation_player_advance (player, 0.8f);
	g_assert_cmpstr (log.events->str, ==, "start@0 step@2 start@0 step@2 start@0 step@2 ");
	g_assert_cmpuint (lrg_sprite_animation_player_get_frame (player), ==, 2);

	/* pause holds, speed scales */
	lrg_sprite_animation_player_set_paused (player, TRUE);
	lrg_sprite_animation_player_advance (player, 0.1f);
	g_assert_cmpuint (lrg_sprite_animation_player_get_frame (player), ==, 2);
	lrg_sprite_animation_player_set_paused (player, FALSE);
	lrg_sprite_animation_player_set_speed (player, 2.0f);
	lrg_sprite_animation_player_advance (player, 0.05f);
	g_assert_cmpuint (lrg_sprite_animation_player_get_frame (player), ==, 3);

	/* play_if_different keeps a running clip */
	lrg_sprite_animation_player_set_speed (player, 1.0f);
	g_assert_true (lrg_sprite_animation_player_play_if_different (player, "run"));
	g_assert_cmpuint (lrg_sprite_animation_player_get_frame (player), ==, 3);

	/* a once clip finishes once and holds its last frame */
	g_assert_true (lrg_sprite_animation_player_play (player, "die"));
	for (i = 0; i < 10; i++)
		lrg_sprite_animation_player_advance (player, 0.1f);
	g_assert_cmpuint (log.finished, ==, 1);
	g_assert_false (lrg_sprite_animation_player_is_playing (player));
	g_assert_cmpuint (lrg_sprite_animation_player_get_frame (player), ==, 6);
	g_assert_false (lrg_sprite_animation_player_play_if_different (player, "die"));
	g_assert_cmpuint (log.frame_changes, >=, 6);

	lrg_sprite_animation_player_stop (player);
	g_assert_cmpuint (lrg_sprite_animation_player_get_frame (player), ==, 4);
	g_string_free (log.events, TRUE);
}

static void
test_player_drives_sprite (void)
{
	g_autoptr(LrgSpriteSheet) sheet = player_sheet ();
	g_autoptr(LrgGameObject) object = lrg_game_object_new ();
	g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
	g_autoptr(LrgSpriteAnimationPlayer) player = lrg_sprite_animation_player_new (sheet);
	g_autoptr(GrlRectangle) source = NULL;

	lrg_game_object_add_component (object, LRG_COMPONENT (sprite));
	lrg_game_object_add_component (object, LRG_COMPONENT (player));
	g_assert_true (lrg_sprite_animation_player_play (player, "run"));

	/* the component update path advances and writes the source rect */
	lrg_component_update (LRG_COMPONENT (player), 0.15f);
	source = lrg_sprite_component_get_source (sprite);
	g_assert_nonnull (source);
	g_assert_cmpfloat (source->x, ==, 16.0f);
	g_assert_cmpfloat (source->width, ==, 16.0f);
}

static void
test_apply_to_animator (void)
{
	g_autoptr(LrgSpriteSheet) sheet = player_sheet ();
	g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
	g_autoptr(LrgSpriteAnimation) jumpy = lrg_sprite_animation_new ("jumpy");

	/* not consecutive: needs the player */
	lrg_sprite_animation_add_frame (jumpy, 0, 0.0f);
	lrg_sprite_animation_add_frame (jumpy, 5, 0.0f);
	lrg_sprite_sheet_add_clip (sheet, jumpy);

	g_assert_cmpuint (lrg_sprite_sheet_apply_to_animator (sheet, animator, NULL), ==, 2);
	g_assert_true (lrg_animator_component_has_animation (animator, "run"));
	g_assert_true (lrg_animator_component_has_animation (animator, "die"));
	g_assert_false (lrg_animator_component_has_animation (animator, "jumpy"));

	/* a sheet with spacing cannot be expressed */
	lrg_sprite_sheet_slice_grid (sheet, 16, 16, 4, 2, 0, 0, 0, 1, 0, 0.0f, 0.0f);
	g_assert_cmpuint (lrg_sprite_sheet_apply_to_animator (sheet, animator, NULL), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/sprite-animation/clip/sampling", test_clip_sampling);
	g_test_add_func ("/sprite-animation/clip/events", test_clip_events);
	g_test_add_func ("/sprite-animation/slice/grid", test_slice_grid);
	g_test_add_func ("/sprite-animation/slice/alpha", test_slice_alpha);
	g_test_add_func ("/sprite-animation/rsprite/round-trip", test_rsprite_round_trip);
	g_test_add_func ("/sprite-animation/rsprite/errors", test_rsprite_errors);
	g_test_add_func ("/sprite-animation/rsprite/file-and-edits", test_rsprite_file_and_edits);
	g_test_add_func ("/sprite-animation/player/playback", test_player);
	g_test_add_func ("/sprite-animation/player/drives-sprite", test_player_drives_sprite);
	g_test_add_func ("/sprite-animation/animator", test_apply_to_animator);

	return g_test_run ();
}
