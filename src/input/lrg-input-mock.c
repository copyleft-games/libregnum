/* lrg-input-mock.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mock input source for testing and simulation.
 */

#include "lrg-input-mock.h"

#define MAX_KEYS           512
#define MAX_MOUSE_BUTTONS  8
#define MAX_GAMEPADS       4
#define MAX_GAMEPAD_BUTTONS 32
#define MAX_GAMEPAD_AXES   8

/**
 * LrgInputMock:
 *
 * Mock input source for testing.
 *
 * This class allows programmatically setting input state for unit
 * testing and integration testing.
 */
struct _LrgInputMock
{
	LrgInput parent_instance;

	/* Keyboard state */
	LrgKeyState key_states[MAX_KEYS];

	/* Mouse state */
	LrgKeyState mouse_button_states[MAX_MOUSE_BUTTONS];
	gfloat      mouse_x;
	gfloat      mouse_y;
	gfloat      mouse_dx;
	gfloat      mouse_dy;

	/* Gamepad state */
	gboolean    gamepad_available[MAX_GAMEPADS];
	LrgKeyState gamepad_button_states[MAX_GAMEPADS][MAX_GAMEPAD_BUTTONS];
	gfloat      gamepad_axes[MAX_GAMEPADS][MAX_GAMEPAD_AXES];
};

G_DEFINE_FINAL_TYPE (LrgInputMock, lrg_input_mock, LRG_TYPE_INPUT)

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_input_mock_poll (LrgInput *self)
{
	/* Mock doesn't need to poll real hardware */
}

static gboolean
lrg_input_mock_is_key_pressed (LrgInput *self,
                               GrlKey    key)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (key < 0 || key >= MAX_KEYS)
		return FALSE;

	return mock->key_states[key] == LRG_KEY_STATE_PRESSED;
}

static gboolean
lrg_input_mock_is_key_down (LrgInput *self,
                            GrlKey    key)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (key < 0 || key >= MAX_KEYS)
		return FALSE;

	return mock->key_states[key] == LRG_KEY_STATE_PRESSED ||
	       mock->key_states[key] == LRG_KEY_STATE_DOWN;
}

static gboolean
lrg_input_mock_is_key_released (LrgInput *self,
                                GrlKey    key)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (key < 0 || key >= MAX_KEYS)
		return FALSE;

	return mock->key_states[key] == LRG_KEY_STATE_RELEASED;
}

static gboolean
lrg_input_mock_is_mouse_button_pressed (LrgInput       *self,
                                        GrlMouseButton  button)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (button < 0 || button >= MAX_MOUSE_BUTTONS)
		return FALSE;

	return mock->mouse_button_states[button] == LRG_KEY_STATE_PRESSED;
}

static gboolean
lrg_input_mock_is_mouse_button_down (LrgInput       *self,
                                     GrlMouseButton  button)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (button < 0 || button >= MAX_MOUSE_BUTTONS)
		return FALSE;

	return mock->mouse_button_states[button] == LRG_KEY_STATE_PRESSED ||
	       mock->mouse_button_states[button] == LRG_KEY_STATE_DOWN;
}

static gboolean
lrg_input_mock_is_mouse_button_released (LrgInput       *self,
                                         GrlMouseButton  button)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (button < 0 || button >= MAX_MOUSE_BUTTONS)
		return FALSE;

	return mock->mouse_button_states[button] == LRG_KEY_STATE_RELEASED;
}

static void
lrg_input_mock_get_mouse_position (LrgInput *self,
                                   gfloat   *x,
                                   gfloat   *y)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (x != NULL)
		*x = mock->mouse_x;
	if (y != NULL)
		*y = mock->mouse_y;
}

static void
lrg_input_mock_get_mouse_delta (LrgInput *self,
                                gfloat   *dx,
                                gfloat   *dy)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (dx != NULL)
		*dx = mock->mouse_dx;
	if (dy != NULL)
		*dy = mock->mouse_dy;
}

static gboolean
lrg_input_mock_is_gamepad_available (LrgInput *self,
                                     gint      gamepad)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;

	return mock->gamepad_available[gamepad];
}

static gboolean
lrg_input_mock_is_gamepad_button_pressed (LrgInput         *self,
                                          gint              gamepad,
                                          GrlGamepadButton  button)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;
	if (button < 0 || button >= MAX_GAMEPAD_BUTTONS)
		return FALSE;

	return mock->gamepad_button_states[gamepad][button] == LRG_KEY_STATE_PRESSED;
}

static gboolean
lrg_input_mock_is_gamepad_button_down (LrgInput         *self,
                                       gint              gamepad,
                                       GrlGamepadButton  button)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;
	if (button < 0 || button >= MAX_GAMEPAD_BUTTONS)
		return FALSE;

	return mock->gamepad_button_states[gamepad][button] == LRG_KEY_STATE_PRESSED ||
	       mock->gamepad_button_states[gamepad][button] == LRG_KEY_STATE_DOWN;
}

static gboolean
lrg_input_mock_is_gamepad_button_released (LrgInput         *self,
                                           gint              gamepad,
                                           GrlGamepadButton  button)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return FALSE;
	if (button < 0 || button >= MAX_GAMEPAD_BUTTONS)
		return FALSE;

	return mock->gamepad_button_states[gamepad][button] == LRG_KEY_STATE_RELEASED;
}

static gfloat
lrg_input_mock_get_gamepad_axis (LrgInput       *self,
                                 gint            gamepad,
                                 GrlGamepadAxis  axis)
{
	LrgInputMock *mock = LRG_INPUT_MOCK (self);

	if (gamepad < 0 || gamepad >= MAX_GAMEPADS)
		return 0.0f;
	if (axis < 0 || axis >= MAX_GAMEPAD_AXES)
		return 0.0f;

	return mock->gamepad_axes[gamepad][axis];
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_mock_class_init (LrgInputMockClass *klass)
{
	LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

	/* Override all virtual methods */
	input_class->poll                      = lrg_input_mock_poll;
	input_class->is_key_pressed            = lrg_input_mock_is_key_pressed;
	input_class->is_key_down               = lrg_input_mock_is_key_down;
	input_class->is_key_released           = lrg_input_mock_is_key_released;
	input_class->is_mouse_button_pressed   = lrg_input_mock_is_mouse_button_pressed;
	input_class->is_mouse_button_down      = lrg_input_mock_is_mouse_button_down;
	input_class->is_mouse_button_released  = lrg_input_mock_is_mouse_button_released;
	input_class->get_mouse_position        = lrg_input_mock_get_mouse_position;
	input_class->get_mouse_delta           = lrg_input_mock_get_mouse_delta;
	input_class->is_gamepad_available      = lrg_input_mock_is_gamepad_available;
	input_class->is_gamepad_button_pressed = lrg_input_mock_is_gamepad_button_pressed;
	input_class->is_gamepad_button_down    = lrg_input_mock_is_gamepad_button_down;
	input_class->is_gamepad_button_released = lrg_input_mock_is_gamepad_button_released;
	input_class->get_gamepad_axis          = lrg_input_mock_get_gamepad_axis;
}

static void
lrg_input_mock_init (LrgInputMock *self)
{
	gint i;
	gint j;

	g_object_set (self, "name", "mock", NULL);

	/* Initialize all states to default */
	for (i = 0; i < MAX_KEYS; i++)
		self->key_states[i] = LRG_KEY_STATE_UP;

	for (i = 0; i < MAX_MOUSE_BUTTONS; i++)
		self->mouse_button_states[i] = LRG_KEY_STATE_UP;

	self->mouse_x  = 0.0f;
	self->mouse_y  = 0.0f;
	self->mouse_dx = 0.0f;
	self->mouse_dy = 0.0f;

	for (i = 0; i < MAX_GAMEPADS; i++)
	{
		self->gamepad_available[i] = FALSE;

		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++)
			self->gamepad_button_states[i][j] = LRG_KEY_STATE_UP;

		for (j = 0; j < MAX_GAMEPAD_AXES; j++)
			self->gamepad_axes[i][j] = 0.0f;
	}
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_input_mock_new:
 *
 * Creates a new mock input source.
 *
 * Returns: (transfer full): A new #LrgInputMock
 */
LrgInputMock *
lrg_input_mock_new (void)
{
	return g_object_new (LRG_TYPE_INPUT_MOCK, NULL);
}

/* Keyboard Control */

/**
 * lrg_input_mock_set_key_state:
 * @self: an #LrgInputMock
 * @key: the key to set
 * @state: the state to set
 *
 * Sets the state of a keyboard key.
 */
void
lrg_input_mock_set_key_state (LrgInputMock *self,
                              GrlKey        key,
                              LrgKeyState   state)
{
	g_return_if_fail (LRG_IS_INPUT_MOCK (self));
	g_return_if_fail (key >= 0 && key < MAX_KEYS);

	self->key_states[key] = state;
}

/**
 * lrg_input_mock_press_key:
 * @self: an #LrgInputMock
 * @key: the key to press
 *
 * Simulates pressing a key.
 */
void
lrg_input_mock_press_key (LrgInputMock *self,
                          GrlKey        key)
{
	lrg_input_mock_set_key_state (self, key, LRG_KEY_STATE_PRESSED);
}

/**
 * lrg_input_mock_release_key:
 * @self: an #LrgInputMock
 * @key: the key to release
 *
 * Simulates releasing a key.
 */
void
lrg_input_mock_release_key (LrgInputMock *self,
                            GrlKey        key)
{
	lrg_input_mock_set_key_state (self, key, LRG_KEY_STATE_RELEASED);
}

/**
 * lrg_input_mock_hold_key:
 * @self: an #LrgInputMock
 * @key: the key to hold
 *
 * Simulates holding a key.
 */
void
lrg_input_mock_hold_key (LrgInputMock *self,
                         GrlKey        key)
{
	lrg_input_mock_set_key_state (self, key, LRG_KEY_STATE_DOWN);
}

/* Mouse Control */

/**
 * lrg_input_mock_set_mouse_button_state:
 * @self: an #LrgInputMock
 * @button: the mouse button
 * @state: the state to set
 *
 * Sets the state of a mouse button.
 */
void
lrg_input_mock_set_mouse_button_state (LrgInputMock   *self,
                                       GrlMouseButton  button,
                                       LrgKeyState     state)
{
	g_return_if_fail (LRG_IS_INPUT_MOCK (self));
	g_return_if_fail (button >= 0 && button < MAX_MOUSE_BUTTONS);

	self->mouse_button_states[button] = state;
}

/**
 * lrg_input_mock_set_mouse_position:
 * @self: an #LrgInputMock
 * @x: the X coordinate
 * @y: the Y coordinate
 *
 * Sets the mock mouse position.
 */
void
lrg_input_mock_set_mouse_position (LrgInputMock *self,
                                   gfloat        x,
                                   gfloat        y)
{
	g_return_if_fail (LRG_IS_INPUT_MOCK (self));

	self->mouse_x = x;
	self->mouse_y = y;
}

/**
 * lrg_input_mock_set_mouse_delta:
 * @self: an #LrgInputMock
 * @dx: the X delta
 * @dy: the Y delta
 *
 * Sets the mock mouse delta.
 */
void
lrg_input_mock_set_mouse_delta (LrgInputMock *self,
                                gfloat        dx,
                                gfloat        dy)
{
	g_return_if_fail (LRG_IS_INPUT_MOCK (self));

	self->mouse_dx = dx;
	self->mouse_dy = dy;
}

/* Gamepad Control */

/**
 * lrg_input_mock_set_gamepad_available:
 * @self: an #LrgInputMock
 * @gamepad: the gamepad index (0-3)
 * @available: whether the gamepad is available
 *
 * Sets whether a gamepad is considered connected.
 */
void
lrg_input_mock_set_gamepad_available (LrgInputMock *self,
                                      gint          gamepad,
                                      gboolean      available)
{
	g_return_if_fail (LRG_IS_INPUT_MOCK (self));
	g_return_if_fail (gamepad >= 0 && gamepad < MAX_GAMEPADS);

	self->gamepad_available[gamepad] = available;
}

/**
 * lrg_input_mock_set_gamepad_button_state:
 * @self: an #LrgInputMock
 * @gamepad: the gamepad index (0-3)
 * @button: the button
 * @state: the state to set
 *
 * Sets the state of a gamepad button.
 */
void
lrg_input_mock_set_gamepad_button_state (LrgInputMock     *self,
                                         gint              gamepad,
                                         GrlGamepadButton  button,
                                         LrgKeyState       state)
{
	g_return_if_fail (LRG_IS_INPUT_MOCK (self));
	g_return_if_fail (gamepad >= 0 && gamepad < MAX_GAMEPADS);
	g_return_if_fail (button >= 0 && button < MAX_GAMEPAD_BUTTONS);

	self->gamepad_button_states[gamepad][button] = state;
}

/**
 * lrg_input_mock_set_gamepad_axis:
 * @self: an #LrgInputMock
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis
 * @value: the axis value (-1.0 to 1.0)
 *
 * Sets the value of a gamepad axis.
 */
void
lrg_input_mock_set_gamepad_axis (LrgInputMock   *self,
                                 gint            gamepad,
                                 GrlGamepadAxis  axis,
                                 gfloat          value)
{
	g_return_if_fail (LRG_IS_INPUT_MOCK (self));
	g_return_if_fail (gamepad >= 0 && gamepad < MAX_GAMEPADS);
	g_return_if_fail (axis >= 0 && axis < MAX_GAMEPAD_AXES);

	self->gamepad_axes[gamepad][axis] = CLAMP (value, -1.0f, 1.0f);
}

/* Utility */

/**
 * lrg_input_mock_reset:
 * @self: an #LrgInputMock
 *
 * Resets all input state to defaults.
 */
void
lrg_input_mock_reset (LrgInputMock *self)
{
	gint i;
	gint j;

	g_return_if_fail (LRG_IS_INPUT_MOCK (self));

	for (i = 0; i < MAX_KEYS; i++)
		self->key_states[i] = LRG_KEY_STATE_UP;

	for (i = 0; i < MAX_MOUSE_BUTTONS; i++)
		self->mouse_button_states[i] = LRG_KEY_STATE_UP;

	self->mouse_x  = 0.0f;
	self->mouse_y  = 0.0f;
	self->mouse_dx = 0.0f;
	self->mouse_dy = 0.0f;

	for (i = 0; i < MAX_GAMEPADS; i++)
	{
		self->gamepad_available[i] = FALSE;

		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++)
			self->gamepad_button_states[i][j] = LRG_KEY_STATE_UP;

		for (j = 0; j < MAX_GAMEPAD_AXES; j++)
			self->gamepad_axes[i][j] = 0.0f;
	}
}

/**
 * lrg_input_mock_advance_frame:
 * @self: an #LrgInputMock
 *
 * Advances the mock input by one frame.
 *
 * Transitions PRESSED to DOWN and RELEASED to UP.
 */
void
lrg_input_mock_advance_frame (LrgInputMock *self)
{
	gint i;
	gint j;

	g_return_if_fail (LRG_IS_INPUT_MOCK (self));

	/* Transition key states */
	for (i = 0; i < MAX_KEYS; i++)
	{
		if (self->key_states[i] == LRG_KEY_STATE_PRESSED)
			self->key_states[i] = LRG_KEY_STATE_DOWN;
		else if (self->key_states[i] == LRG_KEY_STATE_RELEASED)
			self->key_states[i] = LRG_KEY_STATE_UP;
	}

	/* Transition mouse button states */
	for (i = 0; i < MAX_MOUSE_BUTTONS; i++)
	{
		if (self->mouse_button_states[i] == LRG_KEY_STATE_PRESSED)
			self->mouse_button_states[i] = LRG_KEY_STATE_DOWN;
		else if (self->mouse_button_states[i] == LRG_KEY_STATE_RELEASED)
			self->mouse_button_states[i] = LRG_KEY_STATE_UP;
	}

	/* Clear mouse delta (it's per-frame) */
	self->mouse_dx = 0.0f;
	self->mouse_dy = 0.0f;

	/* Transition gamepad button states */
	for (i = 0; i < MAX_GAMEPADS; i++)
	{
		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++)
		{
			if (self->gamepad_button_states[i][j] == LRG_KEY_STATE_PRESSED)
				self->gamepad_button_states[i][j] = LRG_KEY_STATE_DOWN;
			else if (self->gamepad_button_states[i][j] == LRG_KEY_STATE_RELEASED)
				self->gamepad_button_states[i][j] = LRG_KEY_STATE_UP;
		}
	}
}
