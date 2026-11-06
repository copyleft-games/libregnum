/* lrg-canvas-input.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One frame of input for an #LrgCanvas, plus the graylib poller.
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
}
