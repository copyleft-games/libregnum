/* level-3d-runtime.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A standalone game that plays a level authored in lrgeditor, with no
 * editor objects at all: the .rlevel file is loaded and instantiated into
 * an LrgWorld3D (lrg_level_load_world3d(), one LrgEntity3D per node with
 * the node's GUID and full transform, a component per visual), ticked
 * with lrg_world3d_step() and drawn with LrgWorld3DRenderer, the same
 * renderer the editor viewport uses.
 *
 * The level (examples/data/levels/level-3d-runtime.rlevel) has a ground
 * plane, lit primitives, a mesh asset, a directional sun and a point
 * light, a camera, an audio emitter and two objects that spin through a
 * small game component ("spinner", defined below and registered in the
 * engine registry before the level loads).
 *
 * Controls: hold the right mouse button and move the mouse to look,
 * W/A/S/D to fly, Q/E down/up, Shift to go faster, Esc to quit.
 *
 * Run with --help for options.
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <rlgl.h>
#include <math.h>

#define WINDOW_W        (1280)
#define WINDOW_H        (720)
#define FLY_SPEED       (6.0f)    /* units per second */
#define FLY_FAST        (4.0f)    /* Shift multiplier */
#define LOOK_SPEED      (0.0035f) /* radians per pixel */
#define PITCH_LIMIT     (1.5f)    /* radians */
#define DEFAULT_LEVEL   "data/levels/level-3d-runtime.rlevel"

static const gchar *license_text =
	"level-3d-runtime - libregnum 3D level runtime example\n"
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

/* ==========================================================================
 * The "spinner" game component
 *
 * Turns its entity about its own Y axis at "speed" radians per second. It
 * is a plain LrgComponent: the level names it as `type: spinner` with a
 * `speed` prop, lrg_level_instantiate_3d() creates it through the engine
 * registry and sets the prop, and LrgWorld3D calls its update() every
 * frame.
 * ========================================================================== */

#define GAME_TYPE_SPINNER (game_spinner_get_type ())
G_DECLARE_FINAL_TYPE (GameSpinner, game_spinner, GAME, SPINNER, LrgComponent)

struct _GameSpinner
{
	LrgComponent parent_instance;
	gfloat       speed;      /* radians per second */
	gfloat       angle;      /* accumulated turn */
	gfloat       base[4];    /* the authored rotation, a quaternion */
	gboolean     have_base;
};

G_DEFINE_FINAL_TYPE (GameSpinner, game_spinner, LRG_TYPE_COMPONENT)

enum
{
	SPINNER_PROP_0,
	SPINNER_PROP_SPEED,
	SPINNER_N_PROPS
};

/* Remember the rotation the level gave the entity: the spin is applied on
 * top of it, about the entity's local Y. */
static void
game_spinner_attached_entity (LrgComponent *component,
                              LrgEntity3D  *entity)
{
	GameSpinner *self = GAME_SPINNER (component);

	lrg_entity3d_get_rotation (entity, self->base);
	self->have_base = TRUE;
}

static void
game_spinner_update (LrgComponent *component,
                     gfloat        delta)
{
	GameSpinner  *self = GAME_SPINNER (component);
	LrgEntity3D  *entity = lrg_component_get_entity (component);
	const gfloat  axis[3] = { 0.0f, 1.0f, 0.0f };
	gfloat        turn[4];
	gfloat        q[4];

	if (entity == NULL || !self->have_base)
		return;

	/* base * turn: rotate about local Y first, then by the base */
	self->angle = fmodf (self->angle + self->speed * delta, 2.0f * (gfloat) G_PI);
	lrg_math3d_quat_from_axis_angle (axis, self->angle, turn);
	lrg_math3d_quat_multiply (self->base, turn, q);
	lrg_entity3d_set_rotation (entity, q);
}

static void
game_spinner_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	if (prop_id == SPINNER_PROP_SPEED)
		g_value_set_float (value, GAME_SPINNER (object)->speed);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
game_spinner_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	if (prop_id == SPINNER_PROP_SPEED)
		GAME_SPINNER (object)->speed = g_value_get_float (value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
game_spinner_class_init (GameSpinnerClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->get_property = game_spinner_get_property;
	object_class->set_property = game_spinner_set_property;
	component_class->attached_entity = game_spinner_attached_entity;
	component_class->update = game_spinner_update;

	g_object_class_install_property (object_class, SPINNER_PROP_SPEED,
		g_param_spec_float ("speed", "Speed", "Radians per second about local Y",
		                    -50.0f, 50.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY));
}

static void
game_spinner_init (GameSpinner *self)
{
	self->speed = 1.0f;
}

/* ==========================================================================
 * Fly camera
 *
 * Drives the entity that carries the level's LrgCameraComponent: the
 * component keeps its LrgCamera3D on the entity's world transform, so
 * moving the entity is all a camera controller has to do.
 * ========================================================================== */

typedef struct
{
	LrgEntity3D *entity;  /* borrowed: owned by the world */
	gfloat       yaw;     /* radians about Y */
	gfloat       pitch;   /* radians about X */
} FlyCamera;

static void
fly_camera_init (FlyCamera   *fly,
                 LrgEntity3D *entity)
{
	gfloat euler[3];

	fly->entity = entity;
	lrg_entity3d_get_rotation_euler (entity, euler);
	fly->pitch = euler[0];
	fly->yaw = euler[1];
}

static void
fly_camera_update (FlyCamera *fly,
                   gfloat     delta)
{
	gfloat  euler[3];
	gfloat  pos[3], fwd[3], right[3];
	gfloat  speed = FLY_SPEED * delta;
	gfloat  f = 0.0f, r = 0.0f, u = 0.0f;
	guint   i;

	if (fly->entity == NULL)
		return;

	/* Look: right mouse button + mouse motion */
	if (IsMouseButtonDown (MOUSE_BUTTON_RIGHT))
	{
		Vector2 d = GetMouseDelta ();

		fly->yaw -= d.x * LOOK_SPEED;
		fly->pitch = CLAMP (fly->pitch - d.y * LOOK_SPEED, -PITCH_LIMIT, PITCH_LIMIT);
		euler[0] = fly->pitch;
		euler[1] = fly->yaw;
		euler[2] = 0.0f;
		lrg_entity3d_set_rotation_euler (fly->entity, euler);
	}

	/* Move: W/S along the view, A/D sideways, Q/E down/up */
	if (IsKeyDown (KEY_LEFT_SHIFT) || IsKeyDown (KEY_RIGHT_SHIFT))
		speed *= FLY_FAST;
	if (IsKeyDown (KEY_W)) f += 1.0f;
	if (IsKeyDown (KEY_S)) f -= 1.0f;
	if (IsKeyDown (KEY_D)) r += 1.0f;
	if (IsKeyDown (KEY_A)) r -= 1.0f;
	if (IsKeyDown (KEY_E)) u += 1.0f;
	if (IsKeyDown (KEY_Q)) u -= 1.0f;
	if (f == 0.0f && r == 0.0f && u == 0.0f)
		return;

	lrg_entity3d_get_position (fly->entity, pos);
	lrg_entity3d_get_forward (fly->entity, fwd);
	lrg_entity3d_get_right (fly->entity, right);
	for (i = 0; i < 3; i++)
		pos[i] += (fwd[i] * f + right[i] * r) * speed;
	pos[1] += u * speed;
	lrg_entity3d_set_position (fly->entity, pos);
}

/* ==========================================================================
 * Level loading
 * ========================================================================== */

/*
 * resolve_audio_paths:
 *
 * Level asset paths are relative to the level's directory. The renderer
 * resolves meshes and textures against its base directory; audio files
 * are opened by path, so the emitters get absolute paths here (and start
 * playing; only "autoplay" emitters should, see the pitfall in
 * docs/guides/load-a-level-in-your-game.org).
 */
static void
resolve_audio_paths (LrgWorld3D  *world,
                     const gchar *level_dir)
{
	g_autoptr(GPtrArray) sources = lrg_world3d_get_components_of_type (world, LRG_TYPE_AUDIO_SOURCE_COMPONENT);
	guint                i;

	for (i = 0; i < sources->len; i++)
	{
		LrgAudioSourceComponent *src = g_ptr_array_index (sources, i);
		const gchar             *path = lrg_audio_source_component_get_sound_path (src);
		g_autofree gchar        *abs = NULL;

		if (path == NULL || g_path_is_absolute (path))
			continue;
		abs = g_build_filename (level_dir, path, NULL);
		lrg_audio_source_component_set_sound_path (src, abs);

		/* "autoplay" fired when the emitter entered the world, with the
		 * relative path; start it again now that the file is found */
		if (!lrg_audio_source_component_is_playing (src))
			lrg_audio_source_component_play (src);
	}
}

/* The entity carrying the level's first camera, or NULL. */
static LrgEntity3D *
find_camera_entity (LrgWorld3D *world)
{
	g_autoptr(GPtrArray) cams = lrg_world3d_find_with_component (world, LRG_TYPE_CAMERA_COMPONENT);

	return (cams != NULL && cams->len > 0) ? g_ptr_array_index (cams, 0) : NULL;
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext)     ctx = NULL;
	g_autoptr(GError)             error = NULL;
	g_autoptr(GrlWindow)          window = NULL;
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgWorld3DRenderer) renderer = NULL;
	g_autoptr(LrgLevel)           level = NULL;
	g_autoptr(LrgAudioListenerComponent) listener = NULL;
	g_autofree gchar             *opt_level = NULL;
	g_autofree gchar             *opt_screenshot = NULL;
	g_autofree gchar             *level_dir = NULL;
	g_autofree gchar             *title = NULL;
	gint                          opt_frames = 90;
	gboolean                      opt_license = FALSE;
	LrgEngine                    *engine;
	FlyCamera                     fly = { NULL, 0.0f, 0.0f };
	GrlAudioDevice               *audio;
	gint                          frame = 0;

	GOptionEntry entries[] = {
		{ "level", 'l', 0, G_OPTION_ARG_FILENAME, &opt_level,
		  "The .rlevel file to play (default: " DEFAULT_LEVEL ")", "PATH" },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames,
		  "Frames simulated before --screenshot (default 90)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- play a level authored in lrgeditor");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Controls: right mouse button + mouse to look, W/A/S/D to fly, Q/E down/up,\n"
		"Shift to go faster, Esc to quit.\n\n"
		"Examples (from the examples/ directory):\n"
		"  level-3d-runtime\n"
		"  level-3d-runtime --level ~/game/levels/main.rlevel\n"
		"  level-3d-runtime --screenshot shot.png --frames 120\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("level-3d-runtime: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (opt_frames < 1)
		opt_frames = 1;

	/* 1. The engine registry must know every component type the level
	 *    names before it is instantiated. */
	engine = lrg_engine_get_default ();
	if (!lrg_engine_startup (engine, &error))
	{
		g_printerr ("level-3d-runtime: engine startup failed: %s\n", error->message);
		return 1;
	}
	lrg_registry_register (lrg_engine_get_registry (engine), "spinner", GAME_TYPE_SPINNER);

	/* 2. A window (and the audio device; without one nothing plays). */
	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	window = grl_window_new (WINDOW_W, WINDOW_H, "libregnum - level-3d-runtime");
	grl_window_set_target_fps (window, 60);
	audio = grl_audio_device_get_default ();
	if (audio != NULL && !grl_audio_device_is_ready (audio))
		grl_audio_device_init_audio (audio);

	/* 3. Load the level into a 3D world: one entity per node. */
	if (opt_level == NULL)
		opt_level = g_strdup (DEFAULT_LEVEL);
	world = lrg_world3d_new ();
	level = lrg_level_load_world3d (opt_level, world, &error);
	if (level == NULL)
	{
		g_printerr ("level-3d-runtime: cannot load %s: %s\n", opt_level, error->message);
		return 1;
	}
	level_dir = g_path_get_dirname (opt_level);
	resolve_audio_paths (world, level_dir);
	g_print ("level-3d-runtime: '%s': %u entities, %u lights\n", lrg_level_get_name (level),
	         lrg_world3d_get_entity_count (world), lrg_world3d_get_n_lights (world));

	/* 4. The camera: the level's camera entity, flown by the player and
	 *    carrying the audio listener. */
	fly_camera_init (&fly, find_camera_entity (world));
	if (fly.entity == NULL)
		g_printerr ("level-3d-runtime: the level has no camera node; nothing will be drawn\n");
	else
	{
		listener = lrg_audio_listener_component_new ();
		lrg_entity3d_add_component (fly.entity, LRG_COMPONENT (listener));
	}

	/* 5. The renderer: the same one the editor viewport draws with.
	 *    Relative asset paths resolve against the level's directory. */
	renderer = lrg_world3d_renderer_new ();
	lrg_world3d_renderer_set_base_dir (renderer, level_dir);

	title = g_strdup_printf ("%s  -  RMB look, WASD fly, Q/E down/up", lrg_level_get_name (level));
	while (!grl_window_should_close (window))
	{
		/* A fixed step when capturing keeps screenshots reproducible */
		gfloat delta = (opt_screenshot != NULL) ? 1.0f / 60.0f : GetFrameTime ();

		/* Simulate: input, then the world's fixed / update / late phases
		 * (late update moves the camera and registers the lights). */
		fly_camera_update (&fly, delta);
		lrg_world3d_step (world, delta);
		lrg_audio_manager_update (lrg_audio_manager_get_default ());

		grl_window_begin_drawing (window);
		lrg_world3d_renderer_render (renderer, world, NULL, NULL, 0, 0);
		DrawText (title, 12, 10, 20, RAYWHITE);
		DrawFPS (12, 36);

		if (opt_screenshot != NULL && frame + 1 >= opt_frames)
		{
			Image shot;

			/* Flush the queued 2D text before reading the pixels back */
			rlDrawRenderBatchActive ();
			shot = LoadImageFromScreen ();

			if (!ExportImage (shot, opt_screenshot))
				g_printerr ("level-3d-runtime: could not write %s\n", opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	/* GPU resources go before the window closes */
	lrg_world3d_renderer_release (renderer);
	g_clear_object (&renderer);
	g_clear_object (&world);
	return 0;
}
