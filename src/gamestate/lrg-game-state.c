/* lrg-game-state.c - Abstract base class for game states
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-game-state.h"

/**
 * SECTION:lrg-game-state
 * @title: LrgGameState
 * @short_description: Abstract base class for game states
 *
 * #LrgGameState is an abstract base class for implementing game states
 * in a state machine pattern. States can be pushed onto a stack managed
 * by #LrgGameStateManager, allowing for overlays like pause menus.
 *
 * Subclasses must implement the enter(), exit(), update(), and draw()
 * virtual methods. The pause(), resume(), and handle_input() methods
 * have default implementations that do nothing.
 *
 * Properties like #LrgGameState:transparent and #LrgGameState:blocking
 * control how states interact with each other on the stack:
 *
 * - A transparent state allows states below it to render
 * - A blocking state prevents states below it from updating
 */

typedef struct
{
    gchar    *name;
    gboolean  transparent;
    gboolean  blocking;
} LrgGameStatePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgGameState, lrg_game_state, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_TRANSPARENT,
    PROP_BLOCKING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Default implementations for optional virtual methods.
 * These do nothing by default.
 */

static void
lrg_game_state_real_pause (LrgGameState *self)
{
    /* Default implementation does nothing */
}

static void
lrg_game_state_real_resume (LrgGameState *self)
{
    /* Default implementation does nothing */
}

static gboolean
lrg_game_state_real_handle_input (LrgGameState *self,
                                  gpointer      event)
{
    /* Default implementation does not handle input */
    return FALSE;
}

static gboolean
lrg_game_state_real_update_safe (LrgGameState  *self,
                                 gdouble        delta,
                                 GError       **error)
{
    /* Default implementation delegates to regular update(), no error possible */
    lrg_game_state_update (self, delta);
    return TRUE;
}

static void
lrg_game_state_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgGameState *self = LRG_GAME_STATE (object);

    switch (prop_id)
    {
    case PROP_NAME:
        lrg_game_state_set_name (self, g_value_get_string (value));
        break;
    case PROP_TRANSPARENT:
        lrg_game_state_set_transparent (self, g_value_get_boolean (value));
        break;
    case PROP_BLOCKING:
        lrg_game_state_set_blocking (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_game_state_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgGameState *self = LRG_GAME_STATE (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, lrg_game_state_get_name (self));
        break;
    case PROP_TRANSPARENT:
        g_value_set_boolean (value, lrg_game_state_is_transparent (self));
        break;
    case PROP_BLOCKING:
        g_value_set_boolean (value, lrg_game_state_is_blocking (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_game_state_finalize (GObject *object)
{
    LrgGameState *self = LRG_GAME_STATE (object);
    LrgGameStatePrivate *priv = lrg_game_state_get_instance_private (self);

    g_clear_pointer (&priv->name, g_free);

    G_OBJECT_CLASS (lrg_game_state_parent_class)->finalize (object);
}

static void
lrg_game_state_class_init (LrgGameStateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_game_state_set_property;
    object_class->get_property = lrg_game_state_get_property;
    object_class->finalize = lrg_game_state_finalize;

    /* Set default implementations for optional virtual methods */
    klass->pause = lrg_game_state_real_pause;
    klass->resume = lrg_game_state_real_resume;
    klass->handle_input = lrg_game_state_real_handle_input;
    klass->update_safe = lrg_game_state_real_update_safe;

    /**
     * LrgGameState:name:
     *
     * A display name for this state, useful for debugging.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name for debugging",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgGameState:transparent:
     *
     * Whether this state allows states below it to render.
     * Useful for pause menus that show the game behind them.
     */
    properties[PROP_TRANSPARENT] =
        g_param_spec_boolean ("transparent",
                              "Transparent",
                              "Whether states below can render",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgGameState:blocking:
     *
     * Whether this state blocks updates to states below it.
     * Most states should be blocking (TRUE by default).
     */
    properties[PROP_BLOCKING] =
        g_param_spec_boolean ("blocking",
                              "Blocking",
                              "Whether states below are updated",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_game_state_init (LrgGameState *self)
{
    LrgGameStatePrivate *priv = lrg_game_state_get_instance_private (self);

    priv->name = NULL;
    priv->transparent = FALSE;
    priv->blocking = TRUE;
}

/**
 * lrg_game_state_enter:
 * @self: an #LrgGameState
 *
 * Called when this state becomes the active state.
 * Subclasses must implement this method.
 */
void
lrg_game_state_enter (LrgGameState *self)
{
    LrgGameStateClass *klass;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    klass = LRG_GAME_STATE_GET_CLASS (self);
    g_return_if_fail (klass->enter != NULL);

    klass->enter (self);
}

/**
 * lrg_game_state_exit:
 * @self: an #LrgGameState
 *
 * Called when this state is being removed from the stack.
 * Subclasses must implement this method.
 */
void
lrg_game_state_exit (LrgGameState *self)
{
    LrgGameStateClass *klass;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    klass = LRG_GAME_STATE_GET_CLASS (self);
    g_return_if_fail (klass->exit != NULL);

    klass->exit (self);
}

/**
 * lrg_game_state_pause:
 * @self: an #LrgGameState
 *
 * Called when another state is pushed on top of this one.
 * Default implementation does nothing.
 */
void
lrg_game_state_pause (LrgGameState *self)
{
    LrgGameStateClass *klass;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    klass = LRG_GAME_STATE_GET_CLASS (self);
    if (klass->pause != NULL)
        klass->pause (self);
}

/**
 * lrg_game_state_resume:
 * @self: an #LrgGameState
 *
 * Called when the state above this one is popped.
 * Default implementation does nothing.
 */
void
lrg_game_state_resume (LrgGameState *self)
{
    LrgGameStateClass *klass;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    klass = LRG_GAME_STATE_GET_CLASS (self);
    if (klass->resume != NULL)
        klass->resume (self);
}

/**
 * lrg_game_state_update:
 * @self: an #LrgGameState
 * @delta: time since last frame in seconds
 *
 * Called each frame to update game logic.
 * Subclasses must implement this method.
 */
void
lrg_game_state_update (LrgGameState *self,
                       gdouble       delta)
{
    LrgGameStateClass *klass;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    klass = LRG_GAME_STATE_GET_CLASS (self);
    g_return_if_fail (klass->update != NULL);

    klass->update (self, delta);
}

/**
 * lrg_game_state_draw:
 * @self: an #LrgGameState
 *
 * Called each frame to render the state.
 * Subclasses must implement this method.
 */
void
lrg_game_state_draw (LrgGameState *self)
{
    LrgGameStateClass *klass;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    klass = LRG_GAME_STATE_GET_CLASS (self);
    g_return_if_fail (klass->draw != NULL);

    klass->draw (self);
}

/**
 * lrg_game_state_handle_input:
 * @self: an #LrgGameState
 * @event: (transfer none): the input event to handle
 *
 * Processes an input event.
 * Default implementation does nothing and returns %FALSE.
 *
 * Returns: %TRUE if the event was handled, %FALSE to propagate
 */
gboolean
lrg_game_state_handle_input (LrgGameState *self,
                             gpointer      event)
{
    LrgGameStateClass *klass;

    g_return_val_if_fail (LRG_IS_GAME_STATE (self), FALSE);

    klass = LRG_GAME_STATE_GET_CLASS (self);
    if (klass->handle_input != NULL)
        return klass->handle_input (self, event);

    return FALSE;
}

/**
 * lrg_game_state_update_safe:
 * @self: an #LrgGameState
 * @delta: time since last frame in seconds
 * @error: (nullable): return location for error
 *
 * Updates game logic with error handling. If the state does not
 * implement this method, falls back to regular update().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 1.0
 */
gboolean
lrg_game_state_update_safe (LrgGameState  *self,
                            gdouble        delta,
                            GError       **error)
{
    LrgGameStateClass *klass;

    g_return_val_if_fail (LRG_IS_GAME_STATE (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    klass = LRG_GAME_STATE_GET_CLASS (self);

    if (klass->update_safe != NULL)
    {
        return klass->update_safe (self, delta, error);
    }

    /* Fallback if somehow NULL (shouldn't happen with default) */
    lrg_game_state_update (self, delta);
    return TRUE;
}

/**
 * lrg_game_state_get_name:
 * @self: an #LrgGameState
 *
 * Gets the display name of this state.
 *
 * Returns: (transfer none) (nullable): The state name, or %NULL
 */
const gchar *
lrg_game_state_get_name (LrgGameState *self)
{
    LrgGameStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_STATE (self), NULL);

    priv = lrg_game_state_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_game_state_set_name:
 * @self: an #LrgGameState
 * @name: (nullable): the display name
 *
 * Sets the display name of this state.
 */
void
lrg_game_state_set_name (LrgGameState *self,
                         const gchar  *name)
{
    LrgGameStatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    priv = lrg_game_state_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) != 0)
    {
        g_free (priv->name);
        priv->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

/**
 * lrg_game_state_is_transparent:
 * @self: an #LrgGameState
 *
 * Gets whether this state allows states below to render.
 *
 * Returns: %TRUE if transparent
 */
gboolean
lrg_game_state_is_transparent (LrgGameState *self)
{
    LrgGameStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_STATE (self), FALSE);

    priv = lrg_game_state_get_instance_private (self);
    return priv->transparent;
}

/**
 * lrg_game_state_set_transparent:
 * @self: an #LrgGameState
 * @transparent: whether state is transparent
 *
 * Sets whether this state allows states below to render.
 */
void
lrg_game_state_set_transparent (LrgGameState *self,
                                gboolean      transparent)
{
    LrgGameStatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    priv = lrg_game_state_get_instance_private (self);

    transparent = !!transparent;

    if (priv->transparent != transparent)
    {
        priv->transparent = transparent;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSPARENT]);
    }
}

/**
 * lrg_game_state_is_blocking:
 * @self: an #LrgGameState
 *
 * Gets whether this state blocks updates to states below.
 *
 * Returns: %TRUE if blocking
 */
gboolean
lrg_game_state_is_blocking (LrgGameState *self)
{
    LrgGameStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_STATE (self), TRUE);

    priv = lrg_game_state_get_instance_private (self);
    return priv->blocking;
}

/**
 * lrg_game_state_set_blocking:
 * @self: an #LrgGameState
 * @blocking: whether state blocks updates
 *
 * Sets whether this state blocks updates to states below.
 */
void
lrg_game_state_set_blocking (LrgGameState *self,
                             gboolean      blocking)
{
    LrgGameStatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_STATE (self));

    priv = lrg_game_state_get_instance_private (self);

    blocking = !!blocking;

    if (priv->blocking != blocking)
    {
        priv->blocking = blocking;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLOCKING]);
    }
}
