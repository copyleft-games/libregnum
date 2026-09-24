/* lrg-input-software.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Software input source for AI and programmatic control.
 */

#include "lrg-input-software.h"

#define MAX_KEYS           512
#define MAX_MOUSE_BUTTONS  8
#define MAX_GAMEPADS       4
#define MAX_GAMEPAD_BUTTONS 32
#define MAX_GAMEPAD_AXES   8

/*
 * Internal key state tracking.
 * We need to track both current state and whether the key was
 * just pressed/released this frame.
 */
typedef struct
{
	gboolean down;             /* Currently held */
	gboolean pressed_frame;    /* Just pressed this frame */
	gboolean released_frame;   /* Just released this frame */
	gboolean tap_pending;      /* Release after the press has been polled */
	gboolean pressed_pending;  /* Injected press for the next poll */
	gboolean released_pending; /* Injected release for the next poll */
} SoftKeyState;

/**
 * LrgInputSoftware:
 *
 * Software input source for AI and programmatic control.
 */
struct _LrgInputSoftware
{
	LrgInput parent_instance;

	/* Keyboard state */
	SoftKeyState key_states[MAX_KEYS];

	/* Mouse state */
	SoftKeyState mouse_button_states[MAX_MOUSE_BUTTONS];
	gfloat       mouse_x;
	gfloat       mouse_y;
	gfloat       mouse_dx;
	gfloat       mouse_dy;
	gfloat       pending_dx;
	gfloat       pending_dy;

	/* Gamepad state */
	SoftKeyState gamepad_button_states[MAX_GAMEPADS][MAX_GAMEPAD_BUTTONS];
	gfloat       gamepad_axes[MAX_GAMEPADS][MAX_GAMEPAD_AXES];
};

G_DEFINE_FINAL_TYPE (LrgInputSoftware, lrg_input_software, LRG_TYPE_INPUT)

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_input_software_poll (LrgInput *self)
{
	/* Update state transitions */
	lrg_input_software_update (LRG_INPUT_SOFTWARE (self));
}

static gboolean
lrg_input_software_is_key_pressed (LrgInput *self,
                                   GrlKey    key)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (key < 0 || key >= MAX_KEYS)
		return FALSE;

	return sw->key_states[key].pressed_frame;
}

static gboolean
lrg_input_software_is_key_down (LrgInput *self,
                                GrlKey    key)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (key < 0 || key >= MAX_KEYS)
		return FALSE;

	return sw->key_states[key].down;
}

static gboolean
lrg_input_software_is_key_released (LrgInput *self,
                                    GrlKey    key)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (key < 0 || key >= MAX_KEYS)
		return FALSE;

	return sw->key_states[key].released_frame;
}

static gboolean
lrg_input_software_is_mouse_button_pressed (LrgInput       *self,
                                            GrlMouseButton  button)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (button < 0 || button >= MAX_MOUSE_BUTTONS)
		return FALSE;

	return sw->mouse_button_states[button].pressed_frame;
}

static gboolean
lrg_input_software_is_mouse_button_down (LrgInput       *self,
                                         GrlMouseButton  button)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (button < 0 || button >= MAX_MOUSE_BUTTONS)
		return FALSE;

	return sw->mouse_button_states[button].down;
}

static gboolean
lrg_input_software_is_mouse_button_released (LrgInput       *self,
                                             GrlMouseButton  button)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (button < 0 || button >= MAX_MOUSE_BUTTONS)
		return FALSE;

	return sw->mouse_button_states[button].released_frame;
}

static void
lrg_input_software_get_mouse_position (LrgInput *self,
                                       gfloat   *x,
                                       gfloat   *y)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (x != NULL)
		*x = sw->mouse_x;
	if (y != NULL)
		*y = sw->mouse_y;
}

static void
lrg_input_software_get_mouse_delta (LrgInput *self,
                                    gfloat   *dx,
                                    gfloat   *dy)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (dx != NULL)
		*dx = sw->mouse_dx;
	if (dy != NULL)
		*dy = sw->mouse_dy;
}

static gboolean
lrg_input_software_is_gamepad_available (LrgInput *self,
                                         gint      gamepad)
{
	/* Software gamepads are always "available" */
	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;

	return TRUE;
}

static gboolean
lrg_input_software_is_gamepad_button_pressed (LrgInput         *self,
                                              gint              gamepad,
                                              GrlGamepadButton  button)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;
	if (button < 0 || button >= MAX_GAMEPAD_BUTTONS)
		return FALSE;

	return sw->gamepad_button_states[gamepad][button].pressed_frame;
}

static gboolean
lrg_input_software_is_gamepad_button_down (LrgInput         *self,
                                           gint              gamepad,
                                           GrlGamepadButton  button)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;
	if (button < 0 || button >= MAX_GAMEPAD_BUTTONS)
		return FALSE;

	return sw->gamepad_button_states[gamepad][button].down;
}

static gboolean
lrg_input_software_is_gamepad_button_released (LrgInput         *self,
                                               gint              gamepad,
                                               GrlGamepadButton  button)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;
	if (button < 0 || button >= MAX_GAMEPAD_BUTTONS)
		return FALSE;

	return sw->gamepad_button_states[gamepad][button].released_frame;
}

static gfloat
lrg_input_software_get_gamepad_axis (LrgInput       *self,
                                     gint            gamepad,
                                     GrlGamepadAxis  axis)
{
	LrgInputSoftware *sw = LRG_INPUT_SOFTWARE (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return 0.0f;
	if (axis < 0 || axis >= MAX_GAMEPAD_AXES)
		return 0.0f;

	return sw->gamepad_axes[gamepad][axis];
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_software_class_init (LrgInputSoftwareClass *klass)
{
	LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

	/* Override all virtual methods */
	input_class->poll                      = lrg_input_software_poll;
	input_class->is_key_pressed            = lrg_input_software_is_key_pressed;
	input_class->is_key_down               = lrg_input_software_is_key_down;
	input_class->is_key_released           = lrg_input_software_is_key_released;
	input_class->is_mouse_button_pressed   = lrg_input_software_is_mouse_button_pressed;
	input_class->is_mouse_button_down      = lrg_input_software_is_mouse_button_down;
	input_class->is_mouse_button_released  = lrg_input_software_is_mouse_button_released;
	input_class->get_mouse_position        = lrg_input_software_get_mouse_position;
	input_class->get_mouse_delta           = lrg_input_software_get_mouse_delta;
	input_class->is_gamepad_available      = lrg_input_software_is_gamepad_available;
	input_class->is_gamepad_button_pressed = lrg_input_software_is_gamepad_button_pressed;
	input_class->is_gamepad_button_down    = lrg_input_software_is_gamepad_button_down;
	input_class->is_gamepad_button_released = lrg_input_software_is_gamepad_button_released;
	input_class->get_gamepad_axis          = lrg_input_software_get_gamepad_axis;
}

static void
lrg_input_software_init (LrgInputSoftware *self)
{
	g_object_set (self, "name", "software", NULL);

	/* All state starts at zero/false which is correct */
	self->mouse_x = 0.0f;
	self->mouse_y = 0.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
 * lrg_input_software_new:
 *
 * Creates a new software input source.
 *
 * Returns: (transfer full): A new #LrgInputSoftware
 */
LrgInputSoftware *
lrg_input_software_new (void)
{
	return g_object_new (LRG_TYPE_INPUT_SOFTWARE, NULL);
}

/* Keyboard Control */

/*
 * lrg_input_software_press_key:
 * @self: an #LrgInputSoftware
 * @key: the key to press
 *
 * Injects a key press event.
 */
void
lrg_input_software_press_key (LrgInputSoftware *self,
                              GrlKey            key)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (key >= 0 && key < MAX_KEYS);

	if (!self->key_states[key].down)
	{
		self->key_states[key].down            = TRUE;
		self->key_states[key].pressed_frame   = TRUE;
		self->key_states[key].pressed_pending = TRUE;
	}
}

/*
 * lrg_input_software_release_key:
 * @self: an #LrgInputSoftware
 * @key: the key to release
 *
 * Injects a key release event.
 */
void
lrg_input_software_release_key (LrgInputSoftware *self,
                                GrlKey            key)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (key >= 0 && key < MAX_KEYS);

	self->key_states[key].tap_pending = FALSE;

	if (self->key_states[key].down)
	{
		self->key_states[key].down             = FALSE;
		self->key_states[key].released_frame   = TRUE;
		self->key_states[key].released_pending = TRUE;
	}
}

/*
 * lrg_input_software_tap_key:
 * @self: an #LrgInputSoftware
 * @key: the key to tap
 *
 * Injects a quick key press and release.
 */
void
lrg_input_software_tap_key (LrgInputSoftware *self,
                            GrlKey            key)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (key >= 0 && key < MAX_KEYS);

	self->key_states[key].down            = TRUE;
	self->key_states[key].pressed_frame   = TRUE;
	self->key_states[key].pressed_pending = TRUE;
	self->key_states[key].tap_pending     = TRUE;
}

/* Mouse Control */

/*
 * lrg_input_software_press_mouse_button:
 * @self: an #LrgInputSoftware
 * @button: the mouse button to press
 *
 * Injects a mouse button press.
 */
void
lrg_input_software_press_mouse_button (LrgInputSoftware *self,
                                       GrlMouseButton    button)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (button >= 0 && button < MAX_MOUSE_BUTTONS);

	if (!self->mouse_button_states[button].down)
	{
		self->mouse_button_states[button].down            = TRUE;
		self->mouse_button_states[button].pressed_frame   = TRUE;
		self->mouse_button_states[button].pressed_pending = TRUE;
	}
}

/*
 * lrg_input_software_release_mouse_button:
 * @self: an #LrgInputSoftware
 * @button: the mouse button to release
 *
 * Injects a mouse button release.
 */
void
lrg_input_software_release_mouse_button (LrgInputSoftware *self,
                                         GrlMouseButton    button)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (button >= 0 && button < MAX_MOUSE_BUTTONS);

	if (self->mouse_button_states[button].down)
	{
		self->mouse_button_states[button].down             = FALSE;
		self->mouse_button_states[button].released_frame   = TRUE;
		self->mouse_button_states[button].released_pending = TRUE;
	}
}

/*
 * lrg_input_software_move_mouse_to:
 * @self: an #LrgInputSoftware
 * @x: the target X coordinate
 * @y: the target Y coordinate
 *
 * Moves the virtual mouse to an absolute position.
 */
void
lrg_input_software_move_mouse_to (LrgInputSoftware *self,
                                  gfloat            x,
                                  gfloat            y)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));

	/* Calculate delta from current position */
	self->pending_dx += x - self->mouse_x;
	self->pending_dy += y - self->mouse_y;

	self->mouse_x = x;
	self->mouse_y = y;
}

/*
 * lrg_input_software_move_mouse_by:
 * @self: an #LrgInputSoftware
 * @dx: the X delta
 * @dy: the Y delta
 *
 * Moves the virtual mouse by a relative amount.
 */
void
lrg_input_software_move_mouse_by (LrgInputSoftware *self,
                                  gfloat            dx,
                                  gfloat            dy)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));

	self->mouse_x    += dx;
	self->mouse_y    += dy;
	self->pending_dx += dx;
	self->pending_dy += dy;
}

/* Gamepad Control */

/*
 * lrg_input_software_press_gamepad_button:
 * @self: an #LrgInputSoftware
 * @gamepad: the gamepad index (0-3)
 * @button: the button to press
 *
 * Injects a gamepad button press.
 */
void
lrg_input_software_press_gamepad_button (LrgInputSoftware *self,
                                         gint              gamepad,
                                         GrlGamepadButton  button)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (gamepad >= 0 && gamepad < MAX_GAMEPADS);
	g_return_if_fail (button >= 0 && button < MAX_GAMEPAD_BUTTONS);

	if (!self->gamepad_button_states[gamepad][button].down)
	{
		self->gamepad_button_states[gamepad][button].down            = TRUE;
		self->gamepad_button_states[gamepad][button].pressed_frame   = TRUE;
		self->gamepad_button_states[gamepad][button].pressed_pending = TRUE;
	}
}

/*
 * lrg_input_software_release_gamepad_button:
 * @self: an #LrgInputSoftware
 * @gamepad: the gamepad index (0-3)
 * @button: the button to release
 *
 * Injects a gamepad button release.
 */
void
lrg_input_software_release_gamepad_button (LrgInputSoftware *self,
                                           gint              gamepad,
                                           GrlGamepadButton  button)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (gamepad >= 0 && gamepad < MAX_GAMEPADS);
	g_return_if_fail (button >= 0 && button < MAX_GAMEPAD_BUTTONS);

	if (self->gamepad_button_states[gamepad][button].down)
	{
		self->gamepad_button_states[gamepad][button].down             = FALSE;
		self->gamepad_button_states[gamepad][button].released_frame   = TRUE;
		self->gamepad_button_states[gamepad][button].released_pending = TRUE;
	}
}

/*
 * lrg_input_software_set_gamepad_axis:
 * @self: an #LrgInputSoftware
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis
 * @value: the axis value (-1.0 to 1.0)
 *
 * Sets a virtual gamepad axis value.
 */
void
lrg_input_software_set_gamepad_axis (LrgInputSoftware *self,
                                     gint              gamepad,
                                     GrlGamepadAxis    axis,
                                     gfloat            value)
{
	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));
	g_return_if_fail (gamepad >= 0 && gamepad < MAX_GAMEPADS);
	g_return_if_fail (axis >= 0 && axis < MAX_GAMEPAD_AXES);

	self->gamepad_axes[gamepad][axis] = CLAMP (value, -1.0f, 1.0f);
}

/* Frame Management */

/* Publish injected edges for one polled frame. A tap remains held until its
 * press has reached a frame, then publishes a release on the following poll. */
static void
advance_key_state (SoftKeyState *state)
{
	state->pressed_frame = state->pressed_pending;
	state->released_frame = state->released_pending;

	if (state->tap_pending && !state->pressed_pending)
	{
		state->down = FALSE;
		state->released_frame = TRUE;
		state->tap_pending = FALSE;
	}

	state->pressed_pending = FALSE;
	state->released_pending = FALSE;
}

/*
 * lrg_input_software_update:
 * @self: an #LrgInputSoftware
 *
 * Begins a new software input frame. Injected press and release events remain
 * visible through the next poll, then clear on the following poll. A tap is held
 * for one polled frame and releases on the next. The input manager calls this
 * through the source's poll method; do not also update it manually in that frame.
 */
void
lrg_input_software_update (LrgInputSoftware *self)
{
	gint i;
	gint j;

	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));

	/* Transfer pending mouse delta to current */
	self->mouse_dx   = self->pending_dx;
	self->mouse_dy   = self->pending_dy;
	self->pending_dx = 0.0f;
	self->pending_dy = 0.0f;

	for (i = 0; i < MAX_KEYS; i++)
		advance_key_state (&self->key_states[i]);

	for (i = 0; i < MAX_MOUSE_BUTTONS; i++)
		advance_key_state (&self->mouse_button_states[i]);

	for (i = 0; i < MAX_GAMEPADS; i++)
	{
		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++)
			advance_key_state (&self->gamepad_button_states[i][j]);
	}
}

/*
 * lrg_input_software_clear_all:
 * @self: an #LrgInputSoftware
 *
 * Releases all currently held keys, buttons, and resets axes.
 */
void
lrg_input_software_clear_all (LrgInputSoftware *self)
{
	gint i;
	gint j;

	g_return_if_fail (LRG_IS_INPUT_SOFTWARE (self));

	/* Release all keys */
	for (i = 0; i < MAX_KEYS; i++)
	{
		if (self->key_states[i].down)
		{
			self->key_states[i].down             = FALSE;
			self->key_states[i].released_frame   = TRUE;
			self->key_states[i].released_pending = TRUE;
		}
		self->key_states[i].tap_pending = FALSE;
	}

	/* Release all mouse buttons */
	for (i = 0; i < MAX_MOUSE_BUTTONS; i++)
	{
		if (self->mouse_button_states[i].down)
		{
			self->mouse_button_states[i].down             = FALSE;
			self->mouse_button_states[i].released_frame   = TRUE;
			self->mouse_button_states[i].released_pending = TRUE;
		}
	}

	/* Clear mouse delta */
	self->mouse_dx   = 0.0f;
	self->mouse_dy   = 0.0f;
	self->pending_dx = 0.0f;
	self->pending_dy = 0.0f;

	/* Release all gamepad buttons and reset axes */
	for (i = 0; i < MAX_GAMEPADS; i++)
	{
		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++)
		{
			if (self->gamepad_button_states[i][j].down)
			{
				self->gamepad_button_states[i][j].down             = FALSE;
				self->gamepad_button_states[i][j].released_frame   = TRUE;
				self->gamepad_button_states[i][j].released_pending = TRUE;
			}
		}

		for (j = 0; j < MAX_GAMEPAD_AXES; j++)
		{
			self->gamepad_axes[i][j] = 0.0f;
		}
	}
}
