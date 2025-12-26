/* lrg-input-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Input manager singleton - aggregates multiple input sources.
 */

#include "lrg-input-manager.h"
#include "lrg-input-keyboard.h"
#include "lrg-input-mouse.h"
#include "lrg-input-gamepad.h"

#include <math.h>

/**
 * LrgInputManager:
 *
 * Singleton that aggregates multiple input sources.
 *
 * The manager maintains a list of #LrgInput sources sorted by priority.
 * When querying input, it aggregates results from all enabled sources
 * according to type-specific rules (OR for buttons, SUM for deltas, etc.).
 */
struct _LrgInputManager
{
	GObject    parent_instance;

	GPtrArray *sources;
	gboolean   enabled;
};

G_DEFINE_FINAL_TYPE (LrgInputManager, lrg_input_manager, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_ENABLED,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static LrgInputManager *default_manager = NULL;

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

/*
 * compare_source_priority:
 *
 * Comparison function for sorting sources by priority (descending).
 * Higher priority sources come first.
 */
static gint
compare_source_priority (gconstpointer a,
                         gconstpointer b)
{
	LrgInput *source_a = *(LrgInput **)a;
	LrgInput *source_b = *(LrgInput **)b;

	gint priority_a = lrg_input_get_priority (source_a);
	gint priority_b = lrg_input_get_priority (source_b);

	/* Descending order: higher priority first */
	if (priority_a > priority_b)
		return -1;
	if (priority_a < priority_b)
		return 1;
	return 0;
}

/*
 * sort_sources:
 *
 * Re-sorts the source list by priority.
 */
static void
sort_sources (LrgInputManager *self)
{
	g_ptr_array_sort (self->sources, compare_source_priority);
}

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_input_manager_finalize (GObject *object)
{
	LrgInputManager *self = LRG_INPUT_MANAGER (object);

	g_clear_pointer (&self->sources, g_ptr_array_unref);

	if (default_manager == self)
		default_manager = NULL;

	G_OBJECT_CLASS (lrg_input_manager_parent_class)->finalize (object);
}

static void
lrg_input_manager_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgInputManager *self = LRG_INPUT_MANAGER (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		g_value_set_boolean (value, self->enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_input_manager_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgInputManager *self = LRG_INPUT_MANAGER (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		self->enabled = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_input_manager_class_init (LrgInputManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_input_manager_finalize;
	object_class->get_property = lrg_input_manager_get_property;
	object_class->set_property = lrg_input_manager_set_property;

	/**
	 * LrgInputManager:enabled:
	 *
	 * Whether the input manager is globally enabled.
	 *
	 * When disabled, all input queries return FALSE/0.
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled",
		                      "Enabled",
		                      "Whether input is globally enabled",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_input_manager_init (LrgInputManager *self)
{
	self->sources = g_ptr_array_new_with_free_func (g_object_unref);
	self->enabled = TRUE;
}

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_input_manager_get_default:
 *
 * Gets the default input manager instance.
 *
 * The default manager is created with keyboard, mouse, and gamepad
 * input sources pre-registered.
 *
 * Returns: (transfer none): The default #LrgInputManager instance
 */
LrgInputManager *
lrg_input_manager_get_default (void)
{
	if (default_manager == NULL)
	{
		LrgInput *keyboard;
		LrgInput *mouse;
		LrgInput *gamepad;

		default_manager = g_object_new (LRG_TYPE_INPUT_MANAGER, NULL);

		/* Add default input sources */
		keyboard = lrg_input_keyboard_new ();
		mouse    = lrg_input_mouse_new ();
		gamepad  = lrg_input_gamepad_new ();

		lrg_input_manager_add_source (default_manager, keyboard);
		lrg_input_manager_add_source (default_manager, mouse);
		lrg_input_manager_add_source (default_manager, gamepad);

		/* Manager owns them now */
		g_object_unref (keyboard);
		g_object_unref (mouse);
		g_object_unref (gamepad);
	}

	return default_manager;
}

/* ==========================================================================
 * Source Management
 * ========================================================================== */

/**
 * lrg_input_manager_add_source:
 * @self: an #LrgInputManager
 * @source: (transfer none): the input source to add
 *
 * Adds an input source to the manager.
 */
void
lrg_input_manager_add_source (LrgInputManager *self,
                              LrgInput        *source)
{
	g_return_if_fail (LRG_IS_INPUT_MANAGER (self));
	g_return_if_fail (LRG_IS_INPUT (source));

	g_ptr_array_add (self->sources, g_object_ref (source));
	sort_sources (self);
}

/**
 * lrg_input_manager_remove_source:
 * @self: an #LrgInputManager
 * @source: the input source to remove
 *
 * Removes an input source from the manager.
 *
 * Returns: %TRUE if the source was found and removed
 */
gboolean
lrg_input_manager_remove_source (LrgInputManager *self,
                                 LrgInput        *source)
{
	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);
	g_return_val_if_fail (LRG_IS_INPUT (source), FALSE);

	return g_ptr_array_remove (self->sources, source);
}

/**
 * lrg_input_manager_get_source:
 * @self: an #LrgInputManager
 * @name: the name of the source to find
 *
 * Gets an input source by name.
 *
 * Returns: (transfer none) (nullable): The source, or %NULL if not found
 */
LrgInput *
lrg_input_manager_get_source (LrgInputManager *self,
                              const gchar     *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput    *source      = g_ptr_array_index (self->sources, i);
		const gchar *source_name = lrg_input_get_name (source);

		if (g_strcmp0 (source_name, name) == 0)
			return source;
	}

	return NULL;
}

/**
 * lrg_input_manager_get_sources:
 * @self: an #LrgInputManager
 *
 * Gets all registered input sources.
 *
 * Returns: (transfer none) (element-type LrgInput): The list of sources
 */
GPtrArray *
lrg_input_manager_get_sources (LrgInputManager *self)
{
	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), NULL);

	return self->sources;
}

/* ==========================================================================
 * Polling
 * ========================================================================== */

/**
 * lrg_input_manager_poll:
 * @self: an #LrgInputManager
 *
 * Polls all input sources for updated state.
 */
void
lrg_input_manager_poll (LrgInputManager *self)
{
	guint i;

	g_return_if_fail (LRG_IS_INPUT_MANAGER (self));

	if (!self->enabled)
		return;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source))
			lrg_input_poll (source);
	}
}

/* ==========================================================================
 * Keyboard Input
 * ========================================================================== */

/**
 * lrg_input_manager_is_key_pressed:
 * @self: an #LrgInputManager
 * @key: the key to check
 *
 * Checks if a key was just pressed this frame.
 *
 * Returns: %TRUE if the key was just pressed
 */
gboolean
lrg_input_manager_is_key_pressed (LrgInputManager *self,
                                  GrlKey           key)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	/* OR aggregation: any source returning TRUE wins */
	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_key_pressed (source, key))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_is_key_down:
 * @self: an #LrgInputManager
 * @key: the key to check
 *
 * Checks if a key is currently held down.
 *
 * Returns: %TRUE if the key is held
 */
gboolean
lrg_input_manager_is_key_down (LrgInputManager *self,
                               GrlKey           key)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_key_down (source, key))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_is_key_released:
 * @self: an #LrgInputManager
 * @key: the key to check
 *
 * Checks if a key was just released this frame.
 *
 * Returns: %TRUE if the key was just released
 */
gboolean
lrg_input_manager_is_key_released (LrgInputManager *self,
                                   GrlKey           key)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_key_released (source, key))
			return TRUE;
	}

	return FALSE;
}

/* ==========================================================================
 * Mouse Input
 * ========================================================================== */

/**
 * lrg_input_manager_is_mouse_button_pressed:
 * @self: an #LrgInputManager
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
gboolean
lrg_input_manager_is_mouse_button_pressed (LrgInputManager *self,
                                           GrlMouseButton   button)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_mouse_button_pressed (source, button))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_is_mouse_button_down:
 * @self: an #LrgInputManager
 * @button: the mouse button to check
 *
 * Checks if a mouse button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
gboolean
lrg_input_manager_is_mouse_button_down (LrgInputManager *self,
                                        GrlMouseButton   button)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_mouse_button_down (source, button))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_is_mouse_button_released:
 * @self: an #LrgInputManager
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
gboolean
lrg_input_manager_is_mouse_button_released (LrgInputManager *self,
                                            GrlMouseButton   button)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_mouse_button_released (source, button))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_get_mouse_position:
 * @self: an #LrgInputManager
 * @x: (out) (optional): location to store X coordinate
 * @y: (out) (optional): location to store Y coordinate
 *
 * Gets the current mouse position.
 *
 * Returns the position from the highest-priority enabled source
 * that provides mouse position.
 */
void
lrg_input_manager_get_mouse_position (LrgInputManager *self,
                                      gfloat          *x,
                                      gfloat          *y)
{
	guint i;

	g_return_if_fail (LRG_IS_INPUT_MANAGER (self));

	/* Initialize outputs to 0 */
	if (x != NULL)
		*x = 0.0f;
	if (y != NULL)
		*y = 0.0f;

	if (!self->enabled)
		return;

	/*
	 * First-wins: return position from highest priority source.
	 * Sources are already sorted by priority (descending).
	 */
	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);
		gfloat    mx = 0.0f;
		gfloat    my = 0.0f;

		if (!lrg_input_get_enabled (source))
			continue;

		lrg_input_get_mouse_position (source, &mx, &my);

		/*
		 * If source reports non-zero position, use it.
		 * This assumes non-mouse sources return (0, 0).
		 */
		if (mx != 0.0f || my != 0.0f)
		{
			if (x != NULL)
				*x = mx;
			if (y != NULL)
				*y = my;
			return;
		}
	}
}

/**
 * lrg_input_manager_get_mouse_delta:
 * @self: an #LrgInputManager
 * @dx: (out) (optional): location to store X delta
 * @dy: (out) (optional): location to store Y delta
 *
 * Gets the mouse movement since the last frame.
 *
 * Returns the sum of deltas from all enabled sources.
 */
void
lrg_input_manager_get_mouse_delta (LrgInputManager *self,
                                   gfloat          *dx,
                                   gfloat          *dy)
{
	gfloat total_dx = 0.0f;
	gfloat total_dy = 0.0f;
	guint  i;

	g_return_if_fail (LRG_IS_INPUT_MANAGER (self));

	if (!self->enabled)
	{
		if (dx != NULL)
			*dx = 0.0f;
		if (dy != NULL)
			*dy = 0.0f;
		return;
	}

	/* SUM aggregation: add deltas from all sources */
	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);
		gfloat    sdx = 0.0f;
		gfloat    sdy = 0.0f;

		if (!lrg_input_get_enabled (source))
			continue;

		lrg_input_get_mouse_delta (source, &sdx, &sdy);
		total_dx += sdx;
		total_dy += sdy;
	}

	if (dx != NULL)
		*dx = total_dx;
	if (dy != NULL)
		*dy = total_dy;
}

/* ==========================================================================
 * Gamepad Input
 * ========================================================================== */

/**
 * lrg_input_manager_is_gamepad_available:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 *
 * Checks if a gamepad is connected.
 *
 * Returns: %TRUE if the gamepad is available
 */
gboolean
lrg_input_manager_is_gamepad_available (LrgInputManager *self,
                                        gint             gamepad)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_gamepad_available (source, gamepad))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_is_gamepad_button_pressed:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
gboolean
lrg_input_manager_is_gamepad_button_pressed (LrgInputManager  *self,
                                             gint              gamepad,
                                             GrlGamepadButton  button)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_gamepad_button_pressed (source, gamepad, button))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_is_gamepad_button_down:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
gboolean
lrg_input_manager_is_gamepad_button_down (LrgInputManager  *self,
                                          gint              gamepad,
                                          GrlGamepadButton  button)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_gamepad_button_down (source, gamepad, button))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_is_gamepad_button_released:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
gboolean
lrg_input_manager_is_gamepad_button_released (LrgInputManager  *self,
                                              gint              gamepad,
                                              GrlGamepadButton  button)
{
	guint i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	if (!self->enabled)
		return FALSE;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);

		if (lrg_input_get_enabled (source) &&
		    lrg_input_is_gamepad_button_released (source, gamepad, button))
			return TRUE;
	}

	return FALSE;
}

/**
 * lrg_input_manager_get_gamepad_axis:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis to query
 *
 * Gets the current value of a gamepad axis.
 *
 * Returns the value with maximum absolute magnitude from all sources.
 *
 * Returns: The axis value (-1.0 to 1.0)
 */
gfloat
lrg_input_manager_get_gamepad_axis (LrgInputManager *self,
                                    gint             gamepad,
                                    GrlGamepadAxis   axis)
{
	gfloat max_value    = 0.0f;
	gfloat max_abs      = 0.0f;
	guint  i;

	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), 0.0f);

	if (!self->enabled)
		return 0.0f;

	/* MAX absolute value aggregation: preserves sign */
	for (i = 0; i < self->sources->len; i++)
	{
		LrgInput *source = g_ptr_array_index (self->sources, i);
		gfloat    value;
		gfloat    abs_value;

		if (!lrg_input_get_enabled (source))
			continue;

		value     = lrg_input_get_gamepad_axis (source, gamepad, axis);
		abs_value = fabsf (value);

		if (abs_value > max_abs)
		{
			max_abs   = abs_value;
			max_value = value;
		}
	}

	return max_value;
}

/* ==========================================================================
 * Global Enable/Disable
 * ========================================================================== */

/**
 * lrg_input_manager_get_enabled:
 * @self: an #LrgInputManager
 *
 * Gets whether the input manager is globally enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_input_manager_get_enabled (LrgInputManager *self)
{
	g_return_val_if_fail (LRG_IS_INPUT_MANAGER (self), FALSE);

	return self->enabled;
}

/**
 * lrg_input_manager_set_enabled:
 * @self: an #LrgInputManager
 * @enabled: whether to enable input
 *
 * Sets whether the input manager is globally enabled.
 */
void
lrg_input_manager_set_enabled (LrgInputManager *self,
                               gboolean         enabled)
{
	g_return_if_fail (LRG_IS_INPUT_MANAGER (self));

	if (self->enabled != enabled)
	{
		self->enabled = enabled;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
	}
}
