/* game-touch-demo.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Touch controls in a small 3D scene: the virtual stick (lower left)
 * moves a cube, dragging on the right turns the camera, pinching zooms,
 * and two buttons jump (held) and change the cube's colour (tapped).
 * Taps elsewhere are the game's: they print where they landed.
 *
 * The game reads everything through an LrgInputMap ("move-x", "move-y",
 * "jump", "recolor") whose gamepad bindings the controls drive through
 * an LrgInputSoftware sink, so a keyboard (WASD, space, C) and a real
 * gamepad work at the same time.  The camera reads the controls' look and
 * zoom deltas directly.
 *
 * On a desktop without a touch screen the left mouse button stands in for
 * one finger.  --screenshot FILE draws a few frames with synthetic
 * touches on the stick and a button and saves the image.
 *
 *   ./game-touch-demo
 *   ./game-touch-demo --screenshot touch.png
 *   make ANDROID=1 apk APK_GAME=examples/game-touch-demo.c
 */

#include <math.h>
#include <raylib.h>
#include <libregnum.h>

#define WINDOW_WIDTH  (1280)
#define WINDOW_HEIGHT (720)
#define MOVE_SPEED    (4.0f)    /* units per second at a full push */
#define RUN_FACTOR    (1.8f)
#define GRAVITY       (20.0f)
#define JUMP_SPEED    (8.0f)
#define LOOK_RADIANS  (0.005f)  /* camera yaw/pitch per look pixel */

static const gchar *LAYOUT =
	"stick:\n"
	"  region: [0.0, 0.3, 0.45, 0.7]\n"
	"  radius: 70\n"
	"  deadzone: 0.12\n"
	"  run-threshold: 0.85\n"
	"look:\n"
	"  region: [0.45, 0.0, 0.55, 1.0]\n"
	"  sensitivity: 1.0\n"
	"buttons:\n"
	"  - id: jump\n"
	"    label: Jump\n"
	"    anchor: bottom-right\n"
	"    offset: [-80, -90]\n"
	"    size: [88, 88]\n"
	"    mode: hold\n"
	"  - id: recolor\n"
	"    label: Color\n"
	"    anchor: bottom-right\n"
	"    offset: [-190, -60]\n"
	"    size: [64, 64]\n"
	"    mode: tap\n";

typedef struct
{
	gfloat x;
	gfloat z;
	gfloat y;          /* height above the ground */
	gfloat vy;
	gfloat yaw;        /* camera around the cube */
	gfloat pitch;
	gfloat distance;
	guint  color;
} DemoState;

static const guint8 COLORS[][3] = {
	{ 230, 80, 70 }, { 80, 170, 240 }, { 120, 210, 90 }, { 240, 200, 60 }
};

/* make_map: the game's actions, each with a keyboard and a gamepad binding */
static LrgInputMap *
make_map (void)
{
	LrgInputMap *map = lrg_input_map_new ();
	struct
	{
		const gchar      *name;
		GrlKey            key;
		gboolean          is_axis;
		GrlGamepadAxis    axis;
		gboolean          positive;
		GrlGamepadButton  button;
	} defs[] = {
		{ "move-right", GRL_KEY_D, TRUE, GRL_GAMEPAD_AXIS_LEFT_X, TRUE, 0 },
		{ "move-left", GRL_KEY_A, TRUE, GRL_GAMEPAD_AXIS_LEFT_X, FALSE, 0 },
		{ "move-back", GRL_KEY_S, TRUE, GRL_GAMEPAD_AXIS_LEFT_Y, TRUE, 0 },
		{ "move-forward", GRL_KEY_W, TRUE, GRL_GAMEPAD_AXIS_LEFT_Y, FALSE, 0 },
		{ "jump", GRL_KEY_SPACE, FALSE, 0, FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN },
		{ "recolor", GRL_KEY_C, FALSE, 0, FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (defs); i++)
	{
		g_autoptr(LrgInputAction) action = lrg_input_action_new (defs[i].name);
		g_autoptr(LrgInputBinding) key = lrg_input_binding_new_keyboard (defs[i].key, LRG_INPUT_MODIFIER_NONE);
		g_autoptr(LrgInputBinding) pad = NULL;

		/* the keyboard binding first: the touch button would press it;
		 * the gamepad one second, for a real pad */
		if (defs[i].is_axis)
			pad = lrg_input_binding_new_gamepad_axis (0, defs[i].axis, 0.0001f, defs[i].positive);
		else
			pad = lrg_input_binding_new_gamepad_button (0, defs[i].button);
		lrg_input_action_add_binding (action, key);
		lrg_input_action_add_binding (action, pad);
		lrg_input_map_add_action (map, action);
	}
	return map;
}

/* signed axis from a pair of half-axis actions */
static gfloat
axis (LrgInputMap *map,
      const gchar *positive,
      const gchar *negative)
{
	return lrg_input_map_get_value (map, positive) - lrg_input_map_get_value (map, negative);
}

static void
on_pointer (LrgTouchControls      *controls,
            const LrgTouchGesture *gesture,
            gpointer               user_data)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_TOUCH_GESTURE_TYPE);
	GEnumValue *value = g_enum_get_value (klass, (gint) gesture->type);

	if (gesture->type == LRG_TOUCH_GESTURE_TAP || gesture->type == LRG_TOUCH_GESTURE_LONG_PRESS ||
	    gesture->type == LRG_TOUCH_GESTURE_DOUBLE_TAP || gesture->type == LRG_TOUCH_GESTURE_TWO_FINGER_TAP)
		g_print ("pointer %s at %.0f, %.0f\n", value != NULL ? value->value_nick : "?",
		         gesture->x, gesture->y);
}

/* update: one frame of game logic from the input map and the look deltas */
static void
update (DemoState        *state,
        LrgInputMap      *map,
        LrgTouchControls *controls,
        gfloat            delta)
{
	gfloat mx, my, len, speed, fx, fz, rx, rz, dx, dy;

	/* camera: drag turns, pinch zooms */
	lrg_touch_controls_get_look_delta (controls, &dx, &dy);
	state->yaw -= dx * LOOK_RADIANS;
	state->pitch = CLAMP (state->pitch + dy * LOOK_RADIANS, 0.1f, 1.3f);
	state->distance = CLAMP (state->distance / lrg_touch_controls_get_zoom_delta (controls), 4.0f, 30.0f);

	/* movement relative to the camera's facing */
	mx = axis (map, "move-right", "move-left");
	my = axis (map, "move-back", "move-forward");
	len = sqrtf (mx * mx + my * my);
	if (len > 1.0f)
	{
		mx /= len;
		my /= len;
	}
	speed = MOVE_SPEED * (lrg_touch_controls_is_running (controls) ? RUN_FACTOR : 1.0f);
	fx = -sinf (state->yaw);
	fz = -cosf (state->yaw);
	rx = -fz;
	rz = fx;
	state->x += (rx * mx - fx * my) * speed * delta;
	state->z += (rz * mx - fz * my) * speed * delta;

	/* jump and fall */
	if (lrg_input_map_is_pressed (map, "jump") && state->y <= 0.0f)
		state->vy = JUMP_SPEED;
	state->vy -= GRAVITY * delta;
	state->y = MAX (state->y + state->vy * delta, 0.0f);
	if (state->y <= 0.0f)
		state->vy = 0.0f;

	if (lrg_input_map_is_pressed (map, "recolor"))
		state->color = (state->color + 1) % G_N_ELEMENTS (COLORS);
}

static void
draw (DemoState        *state,
      GrlCamera3D      *camera,
      LrgTouchControls *controls)
{
	g_autoptr(GrlColor) sky = grl_color_new (30, 34, 48, 255);
	g_autoptr(GrlColor) cube = grl_color_new (COLORS[state->color][0], COLORS[state->color][1],
	                                          COLORS[state->color][2], 255);
	g_autoptr(GrlColor) edge = grl_color_new (20, 20, 20, 255);
	g_autoptr(GrlColor) text = grl_color_new (230, 230, 230, 255);
	g_autoptr(GrlVector3) position = grl_vector3_new (state->x, state->y + 0.5f, state->z);
	gfloat cx = state->x + sinf (state->yaw) * cosf (state->pitch) * state->distance;
	gfloat cy = state->y + sinf (state->pitch) * state->distance;
	gfloat cz = state->z + cosf (state->yaw) * cosf (state->pitch) * state->distance;

	grl_camera3d_set_position_xyz (camera, cx, cy, cz);
	grl_camera3d_set_target_xyz (camera, state->x, state->y + 0.5f, state->z);

	grl_draw_clear_background (sky);
	grl_camera3d_begin (camera);
	grl_draw_grid (40, 1.0f);
	grl_draw_cube (position, 1.0f, 1.0f, 1.0f, cube);
	grl_draw_cube_wires (position, 1.0f, 1.0f, 1.0f, edge);
	grl_camera3d_end (camera);

	lrg_touch_controls_draw (controls);
	grl_draw_text ("stick: move   drag right: look   pinch: zoom   Jump / Color", 16, 16, 20, text);
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GrlWindow) window = NULL;
	g_autoptr(GrlCamera3D) camera = NULL;
	g_autoptr(LrgInputMap) map = NULL;
	g_autoptr(LrgTouchControls) controls = NULL;
	g_autofree gchar *screenshot = NULL;
	LrgInputManager *manager;
	DemoState state = { 0.0f, 0.0f, 0.0f, 0.0f, 0.6f, 0.5f, 10.0f, 0 };
	guint frame = 0;
	GOptionEntry entries[] = {
		{ "screenshot", 0, 0, G_OPTION_ARG_FILENAME, &screenshot,
		  "Draw a few frames with synthetic touches and save them to FILE", "FILE" },
		G_OPTION_ENTRY_NULL
	};
	g_autoptr(GOptionContext) context = g_option_context_new ("- touch controls demo");

	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_set_description (context,
		"Examples:\n"
		"  game-touch-demo\n"
		"  game-touch-demo --screenshot touch.png\n");
	if (!g_option_context_parse (context, &argc, &argv, &error))
	{
		g_printerr ("game-touch-demo: %s\n", error->message);
		return 1;
	}

	/* Android: data directories, assets, logcat; elsewhere nothing */
	if (!lrg_platform_prepare (&error))
	{
		g_printerr ("game-touch-demo: %s\n", error->message);
		return 1;
	}

	window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "Touch controls demo");
	grl_window_set_target_fps (window, 60);
	camera = grl_camera3d_new ();
	grl_camera3d_set_fovy (camera, 50.0f);

	/* the controls write into a sink in the default manager; the map
	 * reads the manager */
	manager = lrg_input_manager_get_default ();
	map = make_map ();
	controls = lrg_touch_controls_new ();
	if (!lrg_touch_controls_load_from_data (controls, LAYOUT, -1, &error))
	{
		g_printerr ("game-touch-demo: %s\n", error->message);
		return 1;
	}
	g_object_set (controls, "input-map", map, NULL);
	lrg_touch_controls_attach (controls, manager);
	g_signal_connect (controls, "pointer-gesture", G_CALLBACK (on_pointer), NULL);
	if (screenshot != NULL)
		g_object_set (controls, "mode", LRG_TOUCH_CONTROLS_SHOWN, NULL);

	while (!grl_window_should_close (window))
	{
		gdouble now = grl_window_get_time (window);

		lrg_platform_poll ();
		if (screenshot != NULL)
		{
			/* synthetic: a finger pushing the stick up-right, one on Jump */
			LrgTouchPoint points[2];

			lrg_touch_controls_set_screen_size (controls, (gfloat) GetScreenWidth (), (gfloat) GetScreenHeight ());
			points[0].id = 1;
			points[0].x = 200.0f + MIN ((gfloat) frame * 8.0f, 50.0f);
			points[0].y = 520.0f - MIN ((gfloat) frame * 8.0f, 40.0f);
			points[1].id = 2;
			points[1].x = (gfloat) GetScreenWidth () - 80.0f;
			points[1].y = (gfloat) GetScreenHeight () - 90.0f;
			lrg_touch_controls_feed (controls, points, frame == 0 ? 1 : 2, now);
		}
		else if (grl_input_get_touch_point_count () == 0 && grl_input_is_mouse_button_down (GRL_MOUSE_BUTTON_LEFT))
		{
			/* desktop: the mouse is one finger */
			LrgTouchPoint point;

			lrg_touch_controls_set_screen_size (controls, (gfloat) GetScreenWidth (), (gfloat) GetScreenHeight ());
			point.id = 0;
			point.x = (gfloat) GetMouseX ();
			point.y = (gfloat) GetMouseY ();
			g_object_set (controls, "mode", LRG_TOUCH_CONTROLS_SHOWN, NULL);
			lrg_touch_controls_feed (controls, &point, 1, now);
		}
		else
		{
			lrg_touch_controls_update (controls, now);
		}
		lrg_input_manager_poll (manager);

		update (&state, map, controls, grl_window_get_frame_time (window));

		grl_window_begin_drawing (window);
		draw (&state, camera, controls);
		grl_window_end_drawing (window);

		frame++;
		if (screenshot != NULL && frame == 12)
		{
			TakeScreenshot (screenshot);
			break;
		}
	}
	return 0;
}
