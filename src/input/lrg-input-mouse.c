/* lrg-input-mouse.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mouse input source - wraps graylib mouse functions.
 */

#include "lrg-input-mouse.h"

#include <graylib.h>

/**
 * LrgInputMouse:
 *
 * Mouse input source.
 *
 * This class provides mouse input by wrapping graylib's mouse
 * functions. It implements the mouse-related virtual methods of
 * #LrgInput.
 */
struct _LrgInputMouse
{
	LrgInput parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgInputMouse, lrg_input_mouse, LRG_TYPE_INPUT)

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_input_mouse_is_mouse_button_pressed (LrgInput       *self,
                                         GrlMouseButton  button)
{
	return grl_input_is_mouse_button_pressed (button);
}

static gboolean
lrg_input_mouse_is_mouse_button_down (LrgInput       *self,
                                      GrlMouseButton  button)
{
	return grl_input_is_mouse_button_down (button);
}

static gboolean
lrg_input_mouse_is_mouse_button_released (LrgInput       *self,
                                          GrlMouseButton  button)
{
	return grl_input_is_mouse_button_released (button);
}

static void
lrg_input_mouse_get_mouse_position (LrgInput *self,
                                    gfloat   *x,
                                    gfloat   *y)
{
	g_autoptr(GrlVector2) pos = grl_input_get_mouse_position ();

	if (x != NULL)
		*x = grl_vector2_get_x (pos);
	if (y != NULL)
		*y = grl_vector2_get_y (pos);
}

static void
lrg_input_mouse_get_mouse_delta (LrgInput *self,
                                 gfloat   *dx,
                                 gfloat   *dy)
{
	g_autoptr(GrlVector2) delta = grl_input_get_mouse_delta ();

	if (dx != NULL)
		*dx = grl_vector2_get_x (delta);
	if (dy != NULL)
		*dy = grl_vector2_get_y (delta);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_mouse_class_init (LrgInputMouseClass *klass)
{
	LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

	/* Override mouse-related virtual methods */
	input_class->is_mouse_button_pressed  = lrg_input_mouse_is_mouse_button_pressed;
	input_class->is_mouse_button_down     = lrg_input_mouse_is_mouse_button_down;
	input_class->is_mouse_button_released = lrg_input_mouse_is_mouse_button_released;
	input_class->get_mouse_position       = lrg_input_mouse_get_mouse_position;
	input_class->get_mouse_delta          = lrg_input_mouse_get_mouse_delta;
}

static void
lrg_input_mouse_init (LrgInputMouse *self)
{
	/* Set default name for this input source */
	g_object_set (self, "name", "mouse", NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_input_mouse_new:
 *
 * Creates a new mouse input source.
 *
 * Returns: (transfer full): A new #LrgInputMouse
 */
LrgInput *
lrg_input_mouse_new (void)
{
	return g_object_new (LRG_TYPE_INPUT_MOUSE, NULL);
}
