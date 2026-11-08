/* game-showcase-3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One 0.9.0 program that ties the pieces a shipped 3D game uses:
 * a level authored in lrgeditor (the PBR showcase .rlevel: materials,
 * lights, environment), a dynamics world, a navmesh agent, a skeletal
 * clip sample, a streaming cell and a UI animation sample.
 *
 * --self-test runs that check with no window and exits. With a display,
 * the same check runs and then the level is drawn until Esc.
 *
 * The default level is examples/data/pbr-showcase/pbr-showcase.rlevel.
 * A run from the repository root or from examples/ finds it. --self-test
 * loads that level into an LrgWorld3D and exits.
 *
 *   ./examples/build/release/examples/game-showcase-3d --self-test
 *   cd examples && ./build/release/examples/game-showcase-3d
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_W        (1280)
#define WINDOW_H        (720)
#define DEFAULT_LEVEL   "data/pbr-showcase/pbr-showcase.rlevel"

static const gchar *license_text =
	"game-showcase-3d - libregnum 0.9.0 showcase\n"
	"Copyright (C) 2026  Zach Podbielniak\n\n"
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

static gboolean
near (gfloat a, gfloat b)
{
	return fabsf (a - b) < 0.05f;
}

/* The authored showcase, whether cwd is the repository root, examples/,
 * or the directory that holds this binary. */
static gchar *
default_level_path (void)
{
	static const gchar *candidates[] = {
		"examples/data/pbr-showcase/pbr-showcase.rlevel",
		"data/pbr-showcase/pbr-showcase.rlevel"
	};
	g_autofree gchar *exe = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *beside = NULL;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (candidates); i++)
	{
		if (g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
			return g_canonicalize_filename (candidates[i], NULL);
	}

	/* examples/build/release/examples/<exe> -> examples/data/... */
	exe = g_file_read_link ("/proc/self/exe", NULL);
	if (exe != NULL)
	{
		dir = g_path_get_dirname (exe);
		beside = g_build_filename (dir, "..", "..", "..",
		                           "data", "pbr-showcase",
		                           "pbr-showcase.rlevel", NULL);
		if (g_file_test (beside, G_FILE_TEST_IS_REGULAR))
			return g_canonicalize_filename (beside, NULL);
	}
	return g_strdup (DEFAULT_LEVEL);
}

/* Headless checks. The showcase level is part of the check: a missing
 * file fails, it does not skip. */
static gboolean
self_test (const gchar *level_path,
           GError     **error)
{
	g_autoptr(LrgDynamicsWorld) dynamics = NULL;
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(LrgNavAgent) agent = NULL;
	g_autoptr(LrgAnimationClip) clip = NULL;
	LrgAnimationKeyframe *key_a;
	LrgAnimationKeyframe *key_b;
	g_autoptr(LrgUiAnimation) ui_anim = NULL;
	g_autoptr(LrgLevelStream) stream = NULL;
	g_autoptr(LrgNode) tagged = NULL;
	g_autoptr(LrgWorld3D) world = NULL;
	g_autoptr(LrgLevel) level = NULL;
	g_auto(GStrv) load_ids = NULL;
	g_auto(GStrv) unload_ids = NULL;
	const gfloat gravity[3] = { 0.0f, -10.0f, 0.0f };
	const gfloat up[3] = { 0.0f, 1.0f, 0.0f };
	const gfloat drop_at[3] = { 0.0f, 3.0f, 0.0f };
	const gfloat start[3] = { 0.0f, 0.2f, 0.0f };
	const gfloat dest[3] = { 6.0f, 0.0f, 0.0f };
	/* Two triangles, a 40 m floor on y = 0. */
	const gfloat floor_tris[18] = {
		-20.0f, 0.0f, -20.0f,  20.0f, 0.0f, -20.0f,  20.0f, 0.0f, 20.0f,
		-20.0f, 0.0f, -20.0f,  20.0f, 0.0f,  20.0f, -20.0f, 0.0f, 20.0f
	};
	LrgBonePose pose;
	gfloat position[3];
	gfloat sample;
	gint view_w;
	gint view_h;
	guint body;
	guint plane;
	guint track;
	guint substeps;
	guint entities;
	gint i;

	key_a = NULL;
	key_b = NULL;
	memset (&pose, 0, sizeof (pose));
	body = 0;
	plane = 0;
	track = 0;
	substeps = 0;
	entities = 0;
	sample = 0.0f;
	view_w = 0;
	view_h = 0;

	tagged = lrg_node_new ("walker");
	lrg_node_add_tag (tagged, "quest");
	lrg_node_set_level_path (tagged, "levels/side.rlevel");
	if (!lrg_node_has_tag (tagged, "quest")
	    || g_strcmp0 (lrg_node_get_level_path (tagged), "levels/side.rlevel") != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "node tag or level path was not stored");
		return FALSE;
	}

	stream = lrg_level_stream_new ();
	if (!lrg_level_stream_add_cell (stream, "courtyard", level_path,
	                                0.0f, 0.0f, 0.0f, 10.0f, 4.0f, 10.0f)
	    || !lrg_level_stream_update (stream, 5.0f, 1.0f, 5.0f, 0.0f,
	                                 &load_ids, &unload_ids)
	    || load_ids == NULL || g_strcmp0 (load_ids[0], "courtyard") != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "streaming cell did not load");
		return FALSE;
	}

	if (!lrg_game_view_preset_find ("16:9", &view_w, &view_h)
	    || view_w != 1920 || view_h != 1080)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "game view preset 16:9 is %dx%d", view_w, view_h);
		return FALSE;
	}

	ui_anim = lrg_ui_animation_new ();
	lrg_ui_animation_add_key (ui_anim, "hud", "x", 0.0f, 0.0f);
	lrg_ui_animation_add_key (ui_anim, "hud", "x", 2.0f, 10.0f);
	if (!lrg_ui_animation_sample (ui_anim, "hud", "x", 1.0f, &sample)
	    || !near (sample, 5.0f))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "UI animation sample was %g", sample);
		return FALSE;
	}

	clip = lrg_animation_clip_new ("walk");
	lrg_animation_clip_set_duration (clip, 1.0f);
	track = lrg_animation_clip_add_track (clip, "root");
	key_a = lrg_animation_keyframe_new (0.0f);
	key_b = lrg_animation_keyframe_new (1.0f);
	lrg_bone_pose_set_position (&key_a->pose, 0.0f, 0.0f, 0.0f);
	lrg_bone_pose_set_position (&key_b->pose, 10.0f, 0.0f, 0.0f);
	lrg_animation_clip_add_keyframe (clip, track, key_a);
	lrg_animation_clip_add_keyframe (clip, track, key_b);
	lrg_animation_keyframe_free (key_a);
	lrg_animation_keyframe_free (key_b);
	key_a = NULL;
	key_b = NULL;
	lrg_animation_clip_sample_track (clip, track, 0.0f, &pose);
	if (!near (pose.position_x, 0.0f))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "clip sample at 0 was %g", pose.position_x);
		return FALSE;
	}
	lrg_animation_clip_sample_track (clip, track, 1.0f, &pose);
	if (!near (pose.position_x, 10.0f))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "clip sample at 1 was %g", pose.position_x);
		return FALSE;
	}

	dynamics = lrg_dynamics_world_new ();
	if (!lrg_dynamics_world_set_gravity (dynamics, gravity, error)
	    || !lrg_dynamics_world_add_plane (dynamics, up, 0.0f, &plane, error)
	    || !lrg_dynamics_world_add_sphere (dynamics, LRG_DYNAMICS_BODY_DYNAMIC,
	                                       1.0f, 0.5f, drop_at, NULL, &body, error))
		return FALSE;
	for (i = 0; i < 90; i++)
	{
		if (!lrg_dynamics_world_step (dynamics, 1.0f / 60.0f, &substeps, error))
			return FALSE;
	}
	if (!lrg_dynamics_world_get_position (dynamics, body, position, error))
		return FALSE;
	if (substeps == 0 || !(position[1] < 2.0f))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "sphere did not fall (y = %g, substeps %u)",
		             position[1], substeps);
		return FALSE;
	}

	mesh = lrg_nav_poly_mesh_new ();
	g_object_set (mesh, "agent-radius", 0.2f, "cell-size", 0.3f, NULL);
	lrg_nav_poly_mesh_add_triangles (mesh, floor_tris, 18, TRUE);
	if (!lrg_nav_poly_mesh_bake (mesh, error))
		return FALSE;
	agent = lrg_nav_agent_new ();
	lrg_nav_agent_set_mesh (agent, mesh);
	lrg_nav_agent_set_max_speed (agent, 4.0f);
	lrg_nav_agent_set_radius (agent, 0.2f);
	if (!lrg_nav_agent_warp (agent, start)
	    || !lrg_nav_agent_set_destination (agent, dest))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "nav agent could not path across the floor");
		return FALSE;
	}
	for (i = 0; i < 60; i++)
		lrg_nav_agent_update (agent, 1.0f / 30.0f);
	lrg_nav_agent_get_position (agent, position);
	if (!(position[0] > 0.5f))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "nav agent did not move (x = %g)", position[0]);
		return FALSE;
	}

	if (level_path == NULL || !g_file_test (level_path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "no level file at %s",
		             level_path != NULL ? level_path : "(none)");
		return FALSE;
	}

	world = lrg_world3d_new ();
	level = lrg_level_load_world3d (level_path, world, error);
	if (level == NULL)
		return FALSE;
	entities = lrg_world3d_get_entity_count (world);
	if (entities == 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "%s instantiated no entities", level_path);
		return FALSE;
	}

	g_print ("game-showcase-3d: self-test ok (level %s, %u entities; tags, stream, view, ui anim, clip, physics, nav)\n",
	         level_path, entities);
	return TRUE;
}

static gboolean
have_display (void)
{
	return g_getenv ("DISPLAY") != NULL || g_getenv ("WAYLAND_DISPLAY") != NULL;
}

int
main (int argc, char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *opt_level = NULL;
	g_autofree gchar *level_dir = NULL;
	gboolean opt_license = FALSE;
	gboolean opt_self_test = FALSE;
	gint opt_frames = 0;
	LrgEngine *engine;
	GrlWindow *window;
	LrgWorld3D *world;
	LrgLevel *level;
	LrgWorld3DRenderer *renderer;
	gint frame;

	GOptionEntry entries[] = {
		{ "level", 'l', 0, G_OPTION_ARG_FILENAME, &opt_level,
		  "The .rlevel to load (default: " DEFAULT_LEVEL ")", "PATH" },
		{ "self-test", 0, 0, G_OPTION_ARG_NONE, &opt_self_test,
		  "Run the headless checks and exit", NULL },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames,
		  "Quit the window after this many frames (0 = until Esc)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license,
		  "Show the license and exit", NULL },
		{ NULL }
	};

	opt_level = NULL;
	engine = NULL;
	window = NULL;
	world = NULL;
	level = NULL;
	renderer = NULL;
	frame = 0;

	ctx = g_option_context_new ("- 0.9.0 level, PBR, physics, nav and animation");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_summary (ctx,
		"Headless check, then (with a display) the PBR showcase level.");
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  game-showcase-3d --self-test\n"
		"  game-showcase-3d\n"
		"  game-showcase-3d --level data/levels/level-3d-runtime.rlevel --frames 120\n"
		"\n"
		"The default level is examples/data/pbr-showcase/pbr-showcase.rlevel\n"
		"from the repository root, or data/pbr-showcase/pbr-showcase.rlevel\n"
		"from examples/.\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("game-showcase-3d: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (opt_level == NULL)
		opt_level = default_level_path ();

	engine = lrg_engine_get_default ();
	if (!lrg_engine_startup (engine, &error))
	{
		g_printerr ("game-showcase-3d: engine startup failed: %s\n", error->message);
		return 1;
	}
	if (!self_test (opt_level, &error))
	{
		g_printerr ("game-showcase-3d: %s\n", error->message);
		lrg_engine_shutdown (engine);
		return 1;
	}
	if (opt_self_test || !have_display ())
	{
		if (!opt_self_test)
			g_print ("game-showcase-3d: no display, skipped the window\n");
		lrg_engine_shutdown (engine);
		return 0;
	}

	level_dir = g_path_get_dirname (opt_level);
	lrg_asset_manager_add_search_path (lrg_engine_get_asset_manager (engine), level_dir);

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	window = grl_window_new (WINDOW_W, WINDOW_H, "libregnum - game-showcase-3d");
	grl_window_set_target_fps (window, 60);

	world = lrg_world3d_new ();
	level = lrg_level_load_world3d (opt_level, world, &error);
	if (level == NULL)
	{
		g_printerr ("game-showcase-3d: cannot load %s: %s\n", opt_level, error->message);
		lrg_engine_shutdown (engine);
		return 1;
	}

	renderer = lrg_world3d_renderer_new ();
	lrg_world3d_renderer_set_base_dir (renderer, level_dir);
	lrg_world3d_renderer_set_flags (renderer,
	                                lrg_world3d_renderer_get_flags (renderer)
	                                | LRG_WORLD3D_RENDER_FLAG_POST);
	lrg_world3d_step (world, 1.0f / 60.0f);
	if (!lrg_world3d_renderer_bake_environment (renderer, &error))
	{
		g_printerr ("game-showcase-3d: environment bake: %s\n", error->message);
		g_clear_error (&error);
	}

	while (!grl_window_should_close (window))
	{
		lrg_world3d_step (world, 1.0f / 60.0f);
		grl_window_begin_drawing (window);
		lrg_world3d_renderer_render (renderer, world, NULL, NULL, 0, 0);
		grl_window_end_drawing (window);
		frame++;
		if (opt_frames > 0 && frame >= opt_frames)
			break;
	}

	lrg_world3d_renderer_release (renderer);
	g_object_unref (renderer);
	g_object_unref (level);
	g_object_unref (world);
	lrg_engine_shutdown (engine);
	return 0;
}
