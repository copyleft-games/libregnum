/* lrg-input-action.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Named input action with multiple bindings.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INPUT

#include "lrg-input-action.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    gchar *name;       /* Action name (e.g., "jump", "attack") */
    GPtrArray *bindings; /* Array of LrgInputBinding* (owned) */
} LrgInputActionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgInputAction, lrg_input_action, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_action_finalize (GObject *object)
{
    LrgInputAction        *self = LRG_INPUT_ACTION (object);
    LrgInputActionPrivate *priv = lrg_input_action_get_instance_private (self);

    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->bindings, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_input_action_parent_class)->finalize (object);
}

static void
lrg_input_action_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgInputAction        *self = LRG_INPUT_ACTION (object);
    LrgInputActionPrivate *priv = lrg_input_action_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_input_action_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgInputAction        *self = LRG_INPUT_ACTION (object);
    LrgInputActionPrivate *priv = lrg_input_action_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_input_action_class_init (LrgInputActionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_input_action_finalize;
    object_class->get_property = lrg_input_action_get_property;
    object_class->set_property = lrg_input_action_set_property;

    /**
     * LrgInputAction:name:
     *
     * The action name (e.g., "jump", "attack").
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "The action name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_input_action_init (LrgInputAction *self)
{
    LrgInputActionPrivate *priv = lrg_input_action_get_instance_private (self);

    priv->name = NULL;
    priv->bindings = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_input_binding_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_action_new:
 * @name: the action name (e.g., "jump", "attack")
 *
 * Creates a new input action with the given name.
 *
 * Returns: (transfer full): A new #LrgInputAction
 */
LrgInputAction *
lrg_input_action_new (const gchar *name)
{
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_INPUT_ACTION,
                         "name", name,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_input_action_get_name:
 * @self: an #LrgInputAction
 *
 * Gets the action name.
 *
 * Returns: (transfer none): The action name
 */
const gchar *
lrg_input_action_get_name (LrgInputAction *self)
{
    LrgInputActionPrivate *priv;

    g_return_val_if_fail (LRG_IS_INPUT_ACTION (self), NULL);

    priv = lrg_input_action_get_instance_private (self);
    return priv->name;
}

/* ==========================================================================
 * Binding Management
 * ========================================================================== */

/**
 * lrg_input_action_add_binding:
 * @self: an #LrgInputAction
 * @binding: the binding to add
 *
 * Adds an input binding to this action.
 *
 * The binding is copied, so the caller retains ownership of the original.
 */
void
lrg_input_action_add_binding (LrgInputAction        *self,
                              const LrgInputBinding *binding)
{
    LrgInputActionPrivate *priv;
    LrgInputBinding       *copy;

    g_return_if_fail (LRG_IS_INPUT_ACTION (self));
    g_return_if_fail (binding != NULL);

    priv = lrg_input_action_get_instance_private (self);
    copy = lrg_input_binding_copy (binding);
    g_ptr_array_add (priv->bindings, copy);

    lrg_debug (LRG_LOG_DOMAIN_INPUT,
               "Added binding to action '%s' (count: %u)",
               priv->name, priv->bindings->len);
}

/**
 * lrg_input_action_remove_binding:
 * @self: an #LrgInputAction
 * @index: the index of the binding to remove
 *
 * Removes a binding from this action by index.
 */
void
lrg_input_action_remove_binding (LrgInputAction *self,
                                 guint           index)
{
    LrgInputActionPrivate *priv;

    g_return_if_fail (LRG_IS_INPUT_ACTION (self));

    priv = lrg_input_action_get_instance_private (self);

    if (index >= priv->bindings->len)
    {
        lrg_warning (LRG_LOG_DOMAIN_INPUT,
                     "Binding index %u out of range for action '%s' (count: %u)",
                     index, priv->name, priv->bindings->len);
        return;
    }

    g_ptr_array_remove_index (priv->bindings, index);

    lrg_debug (LRG_LOG_DOMAIN_INPUT,
               "Removed binding from action '%s' (count: %u)",
               priv->name, priv->bindings->len);
}

/**
 * lrg_input_action_clear_bindings:
 * @self: an #LrgInputAction
 *
 * Removes all bindings from this action.
 */
void
lrg_input_action_clear_bindings (LrgInputAction *self)
{
    LrgInputActionPrivate *priv;

    g_return_if_fail (LRG_IS_INPUT_ACTION (self));

    priv = lrg_input_action_get_instance_private (self);

    if (priv->bindings->len > 0)
    {
        g_ptr_array_set_size (priv->bindings, 0);
        lrg_debug (LRG_LOG_DOMAIN_INPUT,
                   "Cleared all bindings from action '%s'",
                   priv->name);
    }
}

/**
 * lrg_input_action_get_binding_count:
 * @self: an #LrgInputAction
 *
 * Gets the number of bindings in this action.
 *
 * Returns: The binding count
 */
guint
lrg_input_action_get_binding_count (LrgInputAction *self)
{
    LrgInputActionPrivate *priv;

    g_return_val_if_fail (LRG_IS_INPUT_ACTION (self), 0);

    priv = lrg_input_action_get_instance_private (self);
    return priv->bindings->len;
}

/**
 * lrg_input_action_get_binding:
 * @self: an #LrgInputAction
 * @index: the binding index
 *
 * Gets a binding by index.
 *
 * Returns: (transfer none) (nullable): The binding, or %NULL if index is out of range
 */
const LrgInputBinding *
lrg_input_action_get_binding (LrgInputAction *self,
                              guint           index)
{
    LrgInputActionPrivate *priv;

    g_return_val_if_fail (LRG_IS_INPUT_ACTION (self), NULL);

    priv = lrg_input_action_get_instance_private (self);

    if (index >= priv->bindings->len)
    {
        return NULL;
    }

    return g_ptr_array_index (priv->bindings, index);
}

/* ==========================================================================
 * State Query
 * ========================================================================== */

/**
 * lrg_input_action_is_pressed:
 * @self: an #LrgInputAction
 *
 * Checks if any binding was just pressed this frame.
 *
 * Returns: %TRUE if any binding was just pressed
 */
gboolean
lrg_input_action_is_pressed (LrgInputAction *self)
{
    LrgInputActionPrivate *priv;
    guint                  i;

    g_return_val_if_fail (LRG_IS_INPUT_ACTION (self), FALSE);

    priv = lrg_input_action_get_instance_private (self);

    for (i = 0; i < priv->bindings->len; i++)
    {
        LrgInputBinding *binding = g_ptr_array_index (priv->bindings, i);
        if (lrg_input_binding_is_pressed (binding))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_input_action_is_down:
 * @self: an #LrgInputAction
 *
 * Checks if any binding is currently held down.
 *
 * Returns: %TRUE if any binding is held down
 */
gboolean
lrg_input_action_is_down (LrgInputAction *self)
{
    LrgInputActionPrivate *priv;
    guint                  i;

    g_return_val_if_fail (LRG_IS_INPUT_ACTION (self), FALSE);

    priv = lrg_input_action_get_instance_private (self);

    for (i = 0; i < priv->bindings->len; i++)
    {
        LrgInputBinding *binding = g_ptr_array_index (priv->bindings, i);
        if (lrg_input_binding_is_down (binding))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_input_action_is_released:
 * @self: an #LrgInputAction
 *
 * Checks if any binding was just released this frame.
 *
 * Returns: %TRUE if any binding was just released
 */
gboolean
lrg_input_action_is_released (LrgInputAction *self)
{
    LrgInputActionPrivate *priv;
    guint                  i;

    g_return_val_if_fail (LRG_IS_INPUT_ACTION (self), FALSE);

    priv = lrg_input_action_get_instance_private (self);

    for (i = 0; i < priv->bindings->len; i++)
    {
        LrgInputBinding *binding = g_ptr_array_index (priv->bindings, i);
        if (lrg_input_binding_is_released (binding))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_input_action_get_value:
 * @self: an #LrgInputAction
 *
 * Gets the maximum axis value from all bindings.
 *
 * For digital bindings, returns 1.0 if any is down, 0.0 otherwise.
 * For axis bindings, returns the maximum absolute value.
 *
 * Returns: The axis value (0.0 to 1.0 typically)
 */
gfloat
lrg_input_action_get_value (LrgInputAction *self)
{
    LrgInputActionPrivate *priv;
    gfloat                 max_value;
    guint                  i;

    g_return_val_if_fail (LRG_IS_INPUT_ACTION (self), 0.0f);

    priv = lrg_input_action_get_instance_private (self);
    max_value = 0.0f;

    for (i = 0; i < priv->bindings->len; i++)
    {
        LrgInputBinding *binding = g_ptr_array_index (priv->bindings, i);
        gfloat           value = lrg_input_binding_get_axis_value (binding);
        gfloat           abs_value = value < 0.0f ? -value : value;

        if (abs_value > max_value)
        {
            max_value = abs_value;
        }
    }

    return max_value;
}
