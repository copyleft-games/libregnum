/* lrg-input.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for input sources.
 *
 * This file provides default implementations for all virtual methods
 * that return FALSE/0. Subclasses override only the methods relevant
 * to their input type.
 */

#include "lrg-input.h"

/**
 * LrgInputPrivate:
 * @name: The name of this input source
 * @enabled: Whether this input source is enabled
 * @priority: Priority for query ordering (higher = first)
 *
 * Private data for #LrgInput.
 */
typedef struct
{
	gchar    *name;
	gboolean  enabled;
	gint      priority;
} LrgInputPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgInput, lrg_input, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_ENABLED,
	PROP_PRIORITY,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_input_real_poll (LrgInput *self)
{
	/* Default: do nothing */
}

static gboolean
lrg_input_real_is_key_pressed (LrgInput *self,
                               GrlKey    key)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_key_down (LrgInput *self,
                            GrlKey    key)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_key_released (LrgInput *self,
                                GrlKey    key)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_mouse_button_pressed (LrgInput       *self,
                                        GrlMouseButton  button)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_mouse_button_down (LrgInput       *self,
                                     GrlMouseButton  button)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_mouse_button_released (LrgInput       *self,
                                         GrlMouseButton  button)
{
	return FALSE;
}

static void
lrg_input_real_get_mouse_position (LrgInput *self,
                                   gfloat   *x,
                                   gfloat   *y)
{
	if (x != NULL)
		*x = 0.0f;
	if (y != NULL)
		*y = 0.0f;
}

static void
lrg_input_real_get_mouse_delta (LrgInput *self,
                                gfloat   *dx,
                                gfloat   *dy)
{
	if (dx != NULL)
		*dx = 0.0f;
	if (dy != NULL)
		*dy = 0.0f;
}

static gboolean
lrg_input_real_is_gamepad_available (LrgInput *self,
                                     gint      gamepad)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_gamepad_button_pressed (LrgInput         *self,
                                          gint              gamepad,
                                          GrlGamepadButton  button)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_gamepad_button_down (LrgInput         *self,
                                       gint              gamepad,
                                       GrlGamepadButton  button)
{
	return FALSE;
}

static gboolean
lrg_input_real_is_gamepad_button_released (LrgInput         *self,
                                           gint              gamepad,
                                           GrlGamepadButton  button)
{
	return FALSE;
}

static gfloat
lrg_input_real_get_gamepad_axis (LrgInput       *self,
                                 gint            gamepad,
                                 GrlGamepadAxis  axis)
{
	return 0.0f;
}

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_input_finalize (GObject *object)
{
	LrgInput        *self = LRG_INPUT (object);
	LrgInputPrivate *priv = lrg_input_get_instance_private (self);

	g_clear_pointer (&priv->name, g_free);

	G_OBJECT_CLASS (lrg_input_parent_class)->finalize (object);
}

static void
lrg_input_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgInput        *self = LRG_INPUT (object);
	LrgInputPrivate *priv = lrg_input_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_ENABLED:
		g_value_set_boolean (value, priv->enabled);
		break;
	case PROP_PRIORITY:
		g_value_set_int (value, priv->priority);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_input_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgInput        *self = LRG_INPUT (object);
	LrgInputPrivate *priv = lrg_input_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_value_dup_string (value);
		break;
	case PROP_ENABLED:
		priv->enabled = g_value_get_boolean (value);
		break;
	case PROP_PRIORITY:
		priv->priority = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_input_class_init (LrgInputClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_input_finalize;
	object_class->get_property = lrg_input_get_property;
	object_class->set_property = lrg_input_set_property;

	/* Install default virtual method implementations */
	klass->poll                      = lrg_input_real_poll;
	klass->is_key_pressed            = lrg_input_real_is_key_pressed;
	klass->is_key_down               = lrg_input_real_is_key_down;
	klass->is_key_released           = lrg_input_real_is_key_released;
	klass->is_mouse_button_pressed   = lrg_input_real_is_mouse_button_pressed;
	klass->is_mouse_button_down      = lrg_input_real_is_mouse_button_down;
	klass->is_mouse_button_released  = lrg_input_real_is_mouse_button_released;
	klass->get_mouse_position        = lrg_input_real_get_mouse_position;
	klass->get_mouse_delta           = lrg_input_real_get_mouse_delta;
	klass->is_gamepad_available      = lrg_input_real_is_gamepad_available;
	klass->is_gamepad_button_pressed = lrg_input_real_is_gamepad_button_pressed;
	klass->is_gamepad_button_down    = lrg_input_real_is_gamepad_button_down;
	klass->is_gamepad_button_released = lrg_input_real_is_gamepad_button_released;
	klass->get_gamepad_axis          = lrg_input_real_get_gamepad_axis;

	/**
	 * LrgInput:name:
	 *
	 * The name of this input source.
	 *
	 * This is used for identification and debugging purposes.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name",
		                     "Name",
		                     "The name of this input source",
		                     "unnamed",
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgInput:enabled:
	 *
	 * Whether this input source is enabled.
	 *
	 * Disabled input sources are skipped during input queries.
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled",
		                      "Enabled",
		                      "Whether this input source is enabled",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	/**
	 * LrgInput:priority:
	 *
	 * The priority of this input source.
	 *
	 * Higher priority sources are queried first. For position queries,
	 * the highest-priority enabled source wins.
	 */
	properties[PROP_PRIORITY] =
		g_param_spec_int ("priority",
		                  "Priority",
		                  "Priority for query ordering (higher = first)",
		                  G_MININT, G_MAXINT, 0,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_input_init (LrgInput *self)
{
	LrgInputPrivate *priv = lrg_input_get_instance_private (self);

	priv->name     = g_strdup ("unnamed");
	priv->enabled  = TRUE;
	priv->priority = 0;
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_input_get_name:
 * @self: an #LrgInput
 *
 * Gets the name of this input source.
 *
 * Returns: (transfer none): The input source name
 */
const gchar *
lrg_input_get_name (LrgInput *self)
{
	LrgInputPrivate *priv;

	g_return_val_if_fail (LRG_IS_INPUT (self), NULL);

	priv = lrg_input_get_instance_private (self);
	return priv->name;
}

/**
 * lrg_input_get_enabled:
 * @self: an #LrgInput
 *
 * Gets whether this input source is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_input_get_enabled (LrgInput *self)
{
	LrgInputPrivate *priv;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	priv = lrg_input_get_instance_private (self);
	return priv->enabled;
}

/**
 * lrg_input_set_enabled:
 * @self: an #LrgInput
 * @enabled: whether to enable this input source
 *
 * Sets whether this input source is enabled.
 */
void
lrg_input_set_enabled (LrgInput *self,
                       gboolean  enabled)
{
	LrgInputPrivate *priv;

	g_return_if_fail (LRG_IS_INPUT (self));

	priv = lrg_input_get_instance_private (self);

	if (priv->enabled != enabled)
	{
		priv->enabled = enabled;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
	}
}

/**
 * lrg_input_get_priority:
 * @self: an #LrgInput
 *
 * Gets the priority of this input source.
 *
 * Returns: The priority value
 */
gint
lrg_input_get_priority (LrgInput *self)
{
	LrgInputPrivate *priv;

	g_return_val_if_fail (LRG_IS_INPUT (self), 0);

	priv = lrg_input_get_instance_private (self);
	return priv->priority;
}

/**
 * lrg_input_set_priority:
 * @self: an #LrgInput
 * @priority: the priority value (higher = queried first)
 *
 * Sets the priority of this input source.
 */
void
lrg_input_set_priority (LrgInput *self,
                        gint      priority)
{
	LrgInputPrivate *priv;

	g_return_if_fail (LRG_IS_INPUT (self));

	priv = lrg_input_get_instance_private (self);

	if (priv->priority != priority)
	{
		priv->priority = priority;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIORITY]);
	}
}

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_input_poll:
 * @self: an #LrgInput
 *
 * Updates the input source state.
 *
 * This should be called once per frame before querying input state.
 */
void
lrg_input_poll (LrgInput *self)
{
	LrgInputClass *klass;

	g_return_if_fail (LRG_IS_INPUT (self));

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->poll != NULL)
		klass->poll (self);
}

/* Keyboard */

/**
 * lrg_input_is_key_pressed:
 * @self: an #LrgInput
 * @key: the key to check
 *
 * Checks if a key was just pressed this frame.
 *
 * Returns: %TRUE if the key was just pressed
 */
gboolean
lrg_input_is_key_pressed (LrgInput *self,
                          GrlKey    key)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_key_pressed != NULL)
		return klass->is_key_pressed (self, key);

	return FALSE;
}

/**
 * lrg_input_is_key_down:
 * @self: an #LrgInput
 * @key: the key to check
 *
 * Checks if a key is currently held down.
 *
 * Returns: %TRUE if the key is held
 */
gboolean
lrg_input_is_key_down (LrgInput *self,
                       GrlKey    key)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_key_down != NULL)
		return klass->is_key_down (self, key);

	return FALSE;
}

/**
 * lrg_input_is_key_released:
 * @self: an #LrgInput
 * @key: the key to check
 *
 * Checks if a key was just released this frame.
 *
 * Returns: %TRUE if the key was just released
 */
gboolean
lrg_input_is_key_released (LrgInput *self,
                           GrlKey    key)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_key_released != NULL)
		return klass->is_key_released (self, key);

	return FALSE;
}

/* Mouse */

/**
 * lrg_input_is_mouse_button_pressed:
 * @self: an #LrgInput
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
gboolean
lrg_input_is_mouse_button_pressed (LrgInput       *self,
                                   GrlMouseButton  button)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_mouse_button_pressed != NULL)
		return klass->is_mouse_button_pressed (self, button);

	return FALSE;
}

/**
 * lrg_input_is_mouse_button_down:
 * @self: an #LrgInput
 * @button: the mouse button to check
 *
 * Checks if a mouse button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
gboolean
lrg_input_is_mouse_button_down (LrgInput       *self,
                                GrlMouseButton  button)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_mouse_button_down != NULL)
		return klass->is_mouse_button_down (self, button);

	return FALSE;
}

/**
 * lrg_input_is_mouse_button_released:
 * @self: an #LrgInput
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
gboolean
lrg_input_is_mouse_button_released (LrgInput       *self,
                                    GrlMouseButton  button)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_mouse_button_released != NULL)
		return klass->is_mouse_button_released (self, button);

	return FALSE;
}

/**
 * lrg_input_get_mouse_position:
 * @self: an #LrgInput
 * @x: (out) (optional): location to store X coordinate
 * @y: (out) (optional): location to store Y coordinate
 *
 * Gets the current mouse position.
 */
void
lrg_input_get_mouse_position (LrgInput *self,
                              gfloat   *x,
                              gfloat   *y)
{
	LrgInputClass *klass;

	g_return_if_fail (LRG_IS_INPUT (self));

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->get_mouse_position != NULL)
		klass->get_mouse_position (self, x, y);
	else
	{
		if (x != NULL)
			*x = 0.0f;
		if (y != NULL)
			*y = 0.0f;
	}
}

/**
 * lrg_input_get_mouse_delta:
 * @self: an #LrgInput
 * @dx: (out) (optional): location to store X delta
 * @dy: (out) (optional): location to store Y delta
 *
 * Gets the mouse movement since the last frame.
 */
void
lrg_input_get_mouse_delta (LrgInput *self,
                           gfloat   *dx,
                           gfloat   *dy)
{
	LrgInputClass *klass;

	g_return_if_fail (LRG_IS_INPUT (self));

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->get_mouse_delta != NULL)
		klass->get_mouse_delta (self, dx, dy);
	else
	{
		if (dx != NULL)
			*dx = 0.0f;
		if (dy != NULL)
			*dy = 0.0f;
	}
}

/* Gamepad */

/**
 * lrg_input_is_gamepad_available:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 *
 * Checks if a gamepad is connected.
 *
 * Returns: %TRUE if the gamepad is available
 */
gboolean
lrg_input_is_gamepad_available (LrgInput *self,
                                gint      gamepad)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_gamepad_available != NULL)
		return klass->is_gamepad_available (self, gamepad);

	return FALSE;
}

/**
 * lrg_input_is_gamepad_button_pressed:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
gboolean
lrg_input_is_gamepad_button_pressed (LrgInput         *self,
                                     gint              gamepad,
                                     GrlGamepadButton  button)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_gamepad_button_pressed != NULL)
		return klass->is_gamepad_button_pressed (self, gamepad, button);

	return FALSE;
}

/**
 * lrg_input_is_gamepad_button_down:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
gboolean
lrg_input_is_gamepad_button_down (LrgInput         *self,
                                  gint              gamepad,
                                  GrlGamepadButton  button)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_gamepad_button_down != NULL)
		return klass->is_gamepad_button_down (self, gamepad, button);

	return FALSE;
}

/**
 * lrg_input_is_gamepad_button_released:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
gboolean
lrg_input_is_gamepad_button_released (LrgInput         *self,
                                      gint              gamepad,
                                      GrlGamepadButton  button)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), FALSE);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->is_gamepad_button_released != NULL)
		return klass->is_gamepad_button_released (self, gamepad, button);

	return FALSE;
}

/**
 * lrg_input_get_gamepad_axis:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis to query
 *
 * Gets the current value of a gamepad axis.
 *
 * Returns: The axis value (-1.0 to 1.0)
 */
gfloat
lrg_input_get_gamepad_axis (LrgInput       *self,
                            gint            gamepad,
                            GrlGamepadAxis  axis)
{
	LrgInputClass *klass;

	g_return_val_if_fail (LRG_IS_INPUT (self), 0.0f);

	klass = LRG_INPUT_GET_CLASS (self);
	if (klass->get_gamepad_axis != NULL)
		return klass->get_gamepad_axis (self, gamepad, axis);

	return 0.0f;
}
