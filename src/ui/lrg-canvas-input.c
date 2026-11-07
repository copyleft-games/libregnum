/* lrg-canvas-input.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One frame of input for an #LrgCanvas, plus the graylib poller and the
 * event latch that keeps clicks and chords shorter than a frame.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <string.h>

#include <raylib.h>

#include "lrg-canvas-input.h"
#include "lrg-canvas-private.h"
#include "../lrg-log.h"

/* Stick deflection that counts as a navigation push, and the
 * deflection it must fall back under before the next push fires */
#define NAV_STICK_PRESS   (0.6f)
#define NAV_STICK_RELEASE (0.3f)

/* Desktop raylib reports the mouse as touch 0; opt in for touch screens */
static gboolean poll_touch = FALSE;

/* ==========================================================================
 * Event latch state
 *
 * raylib samples the mouse buttons and the modifier keys once per frame
 * (in EndDrawing's event poll it only records the latest state), so a
 * press and release that both happen between two frames never show up
 * as edges, and a chord typed faster than a frame (Ctrl down, S, Ctrl
 * up) reaches the frame without its Ctrl. The latch counts the events
 * themselves, as the platform delivered them, and
 * lrg_canvas_input_apply_latch() turns them into the missing edges.
 *
 * Everything is process-wide because raylib's input state is, and all
 * of it runs on the thread that polls events (the main thread).
 * ========================================================================== */

#define LATCH_BUTTONS      (3)     /* left, right, middle (GrlMouseButton 0..2) */
#define LATCH_KEY_LIMIT    (512)   /* raylib / GLFW key codes are below 350 */
#define LATCH_MAX_TOUCHED  (32)    /* distinct keys remembered per frame */

/* Last latched event of a key in this frame */
#define LATCH_KEY_NONE     (0)
#define LATCH_KEY_PRESSED  (1)
#define LATCH_KEY_RELEASED (2)

static gboolean          latch_enabled = TRUE;
static guint             latch_button_presses[LATCH_BUTTONS];
static guint8            latch_key_presses[LATCH_KEY_LIMIT];
static guint8            latch_key_last[LATCH_KEY_LIMIT];
static gint              latch_touched[LATCH_MAX_TOUCHED];
static guint             latch_n_touched = 0;
static LrgInputModifiers latch_mods = LRG_INPUT_MODIFIER_NONE;

G_DEFINE_BOXED_TYPE (LrgCanvasInput, lrg_canvas_input,
                     lrg_canvas_input_copy, lrg_canvas_input_free)

LrgCanvasInput *
lrg_canvas_input_new (void)
{
    LrgCanvasInput *self;

    self = g_new (LrgCanvasInput, 1);
    lrg_canvas_input_init (self);

    return self;
}

LrgCanvasInput *
lrg_canvas_input_copy (const LrgCanvasInput *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return (LrgCanvasInput *) g_memdup2 (self, sizeof (LrgCanvasInput));
}

void
lrg_canvas_input_free (LrgCanvasInput *self)
{
    g_free (self);
}

void
lrg_canvas_input_init (LrgCanvasInput *self)
{
    g_return_if_fail (self != NULL);

    memset (self, 0, sizeof (LrgCanvasInput));
    self->modifiers = LRG_INPUT_MODIFIER_NONE;
    self->nav = LRG_NAV_DIRECTION_NONE;
}

void
lrg_canvas_input_add_key (LrgCanvasInput *self,
                          GrlKey          key)
{
    g_return_if_fail (self != NULL);

    if (self->n_keys >= LRG_CANVAS_INPUT_MAX_KEYS)
        return;

    self->keys[self->n_keys++] = key;
}

void
lrg_canvas_input_add_key_repeat (LrgCanvasInput *self,
                                 GrlKey          key)
{
    g_return_if_fail (self != NULL);

    if (self->n_keys >= LRG_CANVAS_INPUT_MAX_KEYS)
        return;

    self->keys_repeat |= 1u << self->n_keys;
    self->keys[self->n_keys++] = key;
}

/* is_modifier_key: Shift / Ctrl / Alt / Super never auto-repeat */
static gboolean
is_modifier_key (gint key)
{
    return key == (gint) GRL_KEY_LEFT_SHIFT || key == (gint) GRL_KEY_RIGHT_SHIFT ||
           key == (gint) GRL_KEY_LEFT_CONTROL || key == (gint) GRL_KEY_RIGHT_CONTROL ||
           key == (gint) GRL_KEY_LEFT_ALT || key == (gint) GRL_KEY_RIGHT_ALT ||
           key == (gint) GRL_KEY_LEFT_SUPER || key == (gint) GRL_KEY_RIGHT_SUPER;
}

void
lrg_canvas_input_add_key_release (LrgCanvasInput *self,
                                  GrlKey          key)
{
    g_return_if_fail (self != NULL);

    if (self->n_keys_released >= LRG_CANVAS_INPUT_MAX_KEYS)
        return;

    self->keys_released[self->n_keys_released++] = key;
}

void
lrg_canvas_input_add_char (LrgCanvasInput *self,
                           gunichar        codepoint)
{
    g_return_if_fail (self != NULL);

    if (self->n_chars >= LRG_CANVAS_INPUT_MAX_KEYS)
        return;

    self->chars[self->n_chars++] = codepoint;
}

void
lrg_canvas_input_add_touch (LrgCanvasInput *self,
                            gint            id,
                            gfloat          x,
                            gfloat          y)
{
	g_return_if_fail (self != NULL);

	if (self->n_touches >= LRG_CANVAS_INPUT_MAX_TOUCHES)
		return;

	self->touches[self->n_touches].id = id;
	self->touches[self->n_touches].x = x;
	self->touches[self->n_touches].y = y;
	self->n_touches++;
}

void
lrg_canvas_input_set_poll_touch (gboolean enabled)
{
	poll_touch = !!enabled;
}

gboolean
lrg_canvas_input_get_poll_touch (void)
{
	return poll_touch;
}

void
lrg_canvas_input_add_text (LrgCanvasInput *self,
                           const gchar    *utf8)
{
    const gchar *p;

    g_return_if_fail (self != NULL);
    g_return_if_fail (utf8 != NULL);
    g_return_if_fail (g_utf8_validate (utf8, -1, NULL));

    for (p = utf8; *p != '\0'; p = g_utf8_next_char (p))
        lrg_canvas_input_add_char (self, g_utf8_get_char (p));
}

/* ==========================================================================
 * Event latch
 * ========================================================================== */

void
lrg_canvas_input_latch_button (GrlMouseButton button,
                               gboolean       pressed)
{
	/* Releases need no bookkeeping: whether the button is still down is
	 * read from the frame's buttons_down when the latch is applied */
	if ((gint) button < 0 || (gint) button >= LATCH_BUTTONS || !pressed)
		return;
	latch_button_presses[button]++;
}

void
lrg_canvas_input_latch_key (GrlKey            key,
                            gboolean          pressed,
                            LrgInputModifiers modifiers)
{
	gint code = (gint) key;
	guint i;

	if (code <= 0 || code >= LATCH_KEY_LIMIT)
		return;

	/* Remember which keys were touched so applying does not scan the
	 * whole table; a key past the limit is dropped (its press is still
	 * in raylib's own queue) */
	if (latch_key_last[code] == LATCH_KEY_NONE && latch_key_presses[code] == 0)
	{
		for (i = 0; i < latch_n_touched; i++)
			if (latch_touched[i] == code)
				break;
		if (i == latch_n_touched)
		{
			if (latch_n_touched >= LATCH_MAX_TOUCHED)
				return;
			latch_touched[latch_n_touched++] = code;
		}
	}

	if (pressed)
	{
		if (latch_key_presses[code] < G_MAXUINT8)
			latch_key_presses[code]++;
		latch_key_last[code] = LATCH_KEY_PRESSED;
		latch_mods = (LrgInputModifiers) (latch_mods | modifiers);
	}
	else
		latch_key_last[code] = LATCH_KEY_RELEASED;
}

void
lrg_canvas_input_clear_latch (void)
{
	guint i;

	memset (latch_button_presses, 0, sizeof latch_button_presses);
	for (i = 0; i < latch_n_touched; i++)
	{
		latch_key_presses[latch_touched[i]] = 0;
		latch_key_last[latch_touched[i]] = LATCH_KEY_NONE;
	}
	latch_n_touched = 0;
	latch_mods = LRG_INPUT_MODIFIER_NONE;
}

/* count_key: fresh (non-repeat) presses of @key already in the frame */
static guint
count_key (const LrgCanvasInput *self,
           gint                  code,
           gboolean              released)
{
	guint i, n = 0;

	if (released)
	{
		for (i = 0; i < self->n_keys_released; i++)
			if ((gint) self->keys_released[i] == code)
				n++;
		return n;
	}
	for (i = 0; i < self->n_keys; i++)
		if ((gint) self->keys[i] == code && !(self->keys_repeat & (1u << i)))
			n++;
	return n;
}

void
lrg_canvas_input_apply_latch (LrgCanvasInput *self)
{
	static const GrlMouseButton buttons[LATCH_BUTTONS] = {
		GRL_MOUSE_BUTTON_LEFT, GRL_MOUSE_BUTTON_RIGHT, GRL_MOUSE_BUTTON_MIDDLE
	};
	guint i;

	g_return_if_fail (self != NULL);

	/* Buttons: a latched press the frame did not see becomes a press
	 * edge, and a release edge too when the button is already up */
	for (i = 0; i < LATCH_BUTTONS; i++)
	{
		guint bit = LRG_CANVAS_BUTTON_MASK (buttons[i]);

		if (latch_button_presses[i] == 0 || (self->buttons_pressed & bit))
			continue;
		self->buttons_pressed |= bit;
		if (!(self->buttons_down & bit))
			self->buttons_released |= bit;
	}

	/* Keys: presses the queue missed, then releases of keys that went
	 * down and up within the frame (raylib compares states, so it never
	 * reports those) */
	for (i = 0; i < latch_n_touched; i++)
	{
		gint  code = latch_touched[i];
		guint have = count_key (self, code, FALSE);

		while (have < latch_key_presses[code] && self->n_keys < LRG_CANVAS_INPUT_MAX_KEYS)
		{
			lrg_canvas_input_add_key (self, (GrlKey) code);
			have++;
		}
		if (latch_key_last[code] == LATCH_KEY_RELEASED && latch_key_presses[code] > 0 &&
		    count_key (self, code, TRUE) == 0)
			lrg_canvas_input_add_key_release (self, (GrlKey) code);
	}

	/* Modifiers held when this frame's keys were pressed (a chord whose
	 * modifier was already released by the time the frame was sampled) */
	if (self->n_keys > 0)
		self->modifiers = (LrgInputModifiers) (self->modifiers | latch_mods);

	lrg_canvas_input_clear_latch ();
}

/* --------------------------------------------------------------------------
 * GLFW feed (desktop raylib)
 *
 * GLFW is linked into libregnum through raylib's desktop platform; only
 * the three entry points used here are declared. The callbacks are
 * chained in front of raylib's, which keep running unchanged. GLFW
 * numbers the mouse buttons left / right / middle 0 / 1 / 2 like
 * GrlMouseButton, and raylib's key codes are GLFW's.
 * -------------------------------------------------------------------------- */

typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWmousebuttonfun) (GLFWwindow *window, int button, int action, int mods);
typedef void (*GLFWkeyfun) (GLFWwindow *window, int key, int scancode, int action, int mods);

GLFWwindow *       glfwGetCurrentContext      (void);
GLFWmousebuttonfun glfwSetMouseButtonCallback (GLFWwindow *window, GLFWmousebuttonfun callback);
GLFWkeyfun         glfwSetKeyCallback         (GLFWwindow *window, GLFWkeyfun callback);

#define LATCH_GLFW_RELEASE   (0)
#define LATCH_GLFW_PRESS     (1)
#define LATCH_GLFW_MOD_SHIFT (0x0001)
#define LATCH_GLFW_MOD_CTRL  (0x0002)
#define LATCH_GLFW_MOD_ALT   (0x0004)
#define LATCH_GLFW_MOD_SUPER (0x0008)

static GLFWwindow         *latch_window = NULL;
static GLFWmousebuttonfun  latch_button_chain = NULL;
static GLFWkeyfun          latch_key_chain = NULL;

static void
latch_glfw_button (GLFWwindow *window,
                   int         button,
                   int         action,
                   int         mods)
{
	if (action == LATCH_GLFW_PRESS || action == LATCH_GLFW_RELEASE)
		lrg_canvas_input_latch_button ((GrlMouseButton) button, action == LATCH_GLFW_PRESS);
	if (latch_button_chain != NULL)
		latch_button_chain (window, button, action, mods);
}

static void
latch_glfw_key (GLFWwindow *window,
                int         key,
                int         scancode,
                int         action,
                int         mods)
{
	LrgInputModifiers m = LRG_INPUT_MODIFIER_NONE;

	if (mods & LATCH_GLFW_MOD_SHIFT)
		m |= LRG_INPUT_MODIFIER_SHIFT;
	if (mods & LATCH_GLFW_MOD_CTRL)
		m |= LRG_INPUT_MODIFIER_CTRL;
	if (mods & LATCH_GLFW_MOD_ALT)
		m |= LRG_INPUT_MODIFIER_ALT;
	if (mods & LATCH_GLFW_MOD_SUPER)
		m |= LRG_INPUT_MODIFIER_SUPER;

	/* Repeats are found by raylib's scan in lrg_canvas_input_poll() */
	if (action == LATCH_GLFW_PRESS || action == LATCH_GLFW_RELEASE)
		lrg_canvas_input_latch_key ((GrlKey) key, action == LATCH_GLFW_PRESS, m);
	if (latch_key_chain != NULL)
		latch_key_chain (window, key, scancode, action, mods);
}

/*
 * latch_attach:
 *
 * Chains the callbacks in front of raylib's on the current window, once
 * per window. Events delivered before the first poll on a window are
 * raylib's alone, which is what raylib alone would have reported.
 */
static void
latch_attach (void)
{
	GLFWwindow *window;

	if (!latch_enabled || !IsWindowReady ())
		return;
	window = glfwGetCurrentContext ();
	if (window == NULL || window == latch_window)
		return;
	latch_window = window;
	latch_button_chain = glfwSetMouseButtonCallback (window, latch_glfw_button);
	latch_key_chain = glfwSetKeyCallback (window, latch_glfw_key);
	lrg_canvas_input_clear_latch ();
}

void
lrg_canvas_input_detach_latch (void)
{
	if (latch_window != NULL && IsWindowReady () && glfwGetCurrentContext () == latch_window)
	{
		glfwSetMouseButtonCallback (latch_window, latch_button_chain);
		glfwSetKeyCallback (latch_window, latch_key_chain);
	}
	latch_window = NULL;
	latch_button_chain = NULL;
	latch_key_chain = NULL;
	lrg_canvas_input_clear_latch ();
}

void
lrg_canvas_input_set_latch_enabled (gboolean enabled)
{
	enabled = !!enabled;
	if (enabled == latch_enabled)
		return;
	latch_enabled = enabled;
	if (!enabled)
		lrg_canvas_input_detach_latch ();
}

gboolean
lrg_canvas_input_get_latch_enabled (void)
{
	return latch_enabled;
}

/*
 * poll_nav_stick:
 *
 * Turns the first gamepad's left stick into one-shot navigation
 * pushes with hysteresis, so holding the stick moves focus once rather
 * than every frame. The latch is process-wide because raylib's
 * gamepad state is too.
 */
static LrgNavDirection
poll_nav_stick (void)
{
    static gboolean latched = FALSE;
    gfloat          ax;
    gfloat          ay;
    gfloat          mag;

    if (!grl_input_is_gamepad_available (0))
    {
        latched = FALSE;
        return LRG_NAV_DIRECTION_NONE;
    }

    ax = grl_input_get_gamepad_axis_movement (0, GRL_GAMEPAD_AXIS_LEFT_X);
    ay = grl_input_get_gamepad_axis_movement (0, GRL_GAMEPAD_AXIS_LEFT_Y);
    mag = MAX (ABS (ax), ABS (ay));

    if (latched)
    {
        if (mag < NAV_STICK_RELEASE)
            latched = FALSE;
        return LRG_NAV_DIRECTION_NONE;
    }

    if (mag < NAV_STICK_PRESS)
        return LRG_NAV_DIRECTION_NONE;

    latched = TRUE;
    if (ABS (ax) > ABS (ay))
        return ax > 0.0f ? LRG_NAV_DIRECTION_RIGHT : LRG_NAV_DIRECTION_LEFT;
    return ay > 0.0f ? LRG_NAV_DIRECTION_DOWN : LRG_NAV_DIRECTION_UP;
}

void
lrg_canvas_input_poll (LrgCanvasInput *self,
                       gfloat          delta_time)
{
    static const GrlMouseButton buttons[] = {
        GRL_MOUSE_BUTTON_LEFT,
        GRL_MOUSE_BUTTON_RIGHT,
        GRL_MOUSE_BUTTON_MIDDLE
    };
    guint  i;
    GrlKey key;
    gint   ch;
    gint   k;

    g_return_if_fail (self != NULL);

    /* Chain the event latch in front of raylib on a new window */
    latch_attach ();

    lrg_canvas_input_init (self);
    self->delta_time = delta_time;

    /* Pointer, mapped into canvas space */
    self->pointer_x = (gfloat) grl_input_get_mouse_x ();
    self->pointer_y = (gfloat) grl_input_get_mouse_y ();
    _lrg_canvas_apply_pointer_transform (&self->pointer_x, &self->pointer_y);

    /* Buttons */
    for (i = 0; i < G_N_ELEMENTS (buttons); i++)
    {
        guint bit = LRG_CANVAS_BUTTON_MASK (buttons[i]);

        if (grl_input_is_mouse_button_down (buttons[i]))
            self->buttons_down |= bit;
        if (grl_input_is_mouse_button_pressed (buttons[i]))
            self->buttons_pressed |= bit;
        if (grl_input_is_mouse_button_released (buttons[i]))
            self->buttons_released |= bit;
    }

    /* Wheel */
    {
        g_autoptr(GrlVector2) wheel = grl_input_get_mouse_wheel_move_v ();

        self->wheel_x = wheel->x;
        self->wheel_y = wheel->y;
    }

    /* Modifiers */
    if (grl_input_is_key_down (GRL_KEY_LEFT_SHIFT) ||
        grl_input_is_key_down (GRL_KEY_RIGHT_SHIFT))
        self->modifiers |= LRG_INPUT_MODIFIER_SHIFT;
    if (grl_input_is_key_down (GRL_KEY_LEFT_CONTROL) ||
        grl_input_is_key_down (GRL_KEY_RIGHT_CONTROL))
        self->modifiers |= LRG_INPUT_MODIFIER_CTRL;
    if (grl_input_is_key_down (GRL_KEY_LEFT_ALT) ||
        grl_input_is_key_down (GRL_KEY_RIGHT_ALT))
        self->modifiers |= LRG_INPUT_MODIFIER_ALT;
    if (grl_input_is_key_down (GRL_KEY_LEFT_SUPER) ||
        grl_input_is_key_down (GRL_KEY_RIGHT_SUPER))
        self->modifiers |= LRG_INPUT_MODIFIER_SUPER;

    /* Key presses, in order. Tab is queued too: the canvas decides
     * whether the focused widget takes it or focus moves. */
    for (key = grl_input_get_key_pressed ();
         key != GRL_KEY_NULL;
         key = grl_input_get_key_pressed ())
    {
        lrg_canvas_input_add_key (self, key);
    }

    /*
     * Auto-repeat and releases: graylib has no queue for either, so
     * scan the key range (cheap: a few hundred table lookups). Repeats
     * follow the operating system's delay and rate (GLFW_REPEAT);
     * modifiers are skipped so holding Shift does not flood the frame.
     */
    for (k = (gint) GRL_KEY_SPACE; k <= (gint) GRL_KEY_KB_MENU; k++)
    {
        if (!is_modifier_key (k) && grl_input_is_key_pressed_repeat ((GrlKey) k))
            lrg_canvas_input_add_key_repeat (self, (GrlKey) k);
        if (grl_input_is_key_released ((GrlKey) k))
            lrg_canvas_input_add_key_release (self, (GrlKey) k);
    }

    /* Composed characters (layout, Shift and dead keys applied) */
    for (ch = grl_input_get_char_pressed ();
         ch > 0;
         ch = grl_input_get_char_pressed ())
    {
        lrg_canvas_input_add_char (self, (gunichar) ch);
    }

    /* Gamepad: D-pad and stick navigate, A activates, B cancels */
    if (grl_input_is_gamepad_available (0))
    {
        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_UP))
            self->nav = LRG_NAV_DIRECTION_UP;
        else if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN))
            self->nav = LRG_NAV_DIRECTION_DOWN;
        else if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT))
            self->nav = LRG_NAV_DIRECTION_LEFT;
        else if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
            self->nav = LRG_NAV_DIRECTION_RIGHT;

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            self->activate = TRUE;
        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
            self->cancel = TRUE;
    }

    if (self->nav == LRG_NAV_DIRECTION_NONE)
        self->nav = poll_nav_stick ();

    /* Edges and chord modifiers shorter than a frame */
    if (latch_enabled && latch_window != NULL)
        lrg_canvas_input_apply_latch (self);
}
