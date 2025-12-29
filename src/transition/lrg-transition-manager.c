/* lrg-transition-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manager for scene transitions.
 */

#include "lrg-transition-manager.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgTransitionManager:
 *
 * Manages the lifecycle of scene transitions.
 *
 * The transition manager provides a high-level interface for:
 *
 * - Starting and cancelling transitions
 * - Updating active transitions each frame
 * - Rendering transition effects
 * - Querying transition state for scene switching
 *
 * ## Integration with Game State Manager
 *
 * The transition manager is typically used alongside the game state
 * manager. When a state change with transition is requested:
 *
 * 1. Start the transition via the manager
 * 2. Connect to ::midpoint-reached to know when to switch states
 * 3. Connect to ::transition-completed to clean up
 *
 * ## Example usage
 *
 * ```c
 * // Setup
 * LrgTransitionManager *manager = lrg_transition_manager_new ();
 * lrg_transition_manager_initialize (manager, 1280, 720, NULL);
 *
 * // Start transition
 * LrgFadeTransition *fade = lrg_fade_transition_new ();
 * lrg_transition_manager_start (manager, LRG_TRANSITION (fade));
 * g_object_unref (fade);
 *
 * // Game loop
 * while (running) {
 *     lrg_transition_manager_update (manager, delta_time);
 *
 *     if (lrg_transition_manager_is_at_midpoint (manager)) {
 *         // Switch scenes here
 *     }
 *
 *     if (lrg_transition_manager_is_active (manager)) {
 *         lrg_transition_manager_render (manager, old_tex, new_tex);
 *     } else {
 *         // Render scene normally
 *     }
 * }
 * ```
 *
 * Since: 1.0
 */

struct _LrgTransitionManager
{
    GObject parent_instance;

    /* Current transition */
    LrgTransition *current;

    /* Viewport dimensions */
    guint viewport_width;
    guint viewport_height;

    /* State tracking */
    gboolean initialized;
    gboolean midpoint_fired;
};

enum
{
    PROP_0,
    PROP_VIEWPORT_WIDTH,
    PROP_VIEWPORT_HEIGHT,
    N_PROPS
};

enum
{
    SIGNAL_TRANSITION_STARTED,
    SIGNAL_TRANSITION_MIDPOINT,
    SIGNAL_TRANSITION_COMPLETED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgTransitionManager, lrg_transition_manager, G_TYPE_OBJECT)

/*
 * Signal handlers for current transition
 */

static void
on_transition_started (LrgTransition          *transition,
                       LrgTransitionManager   *self)
{
    (void) transition;

    g_signal_emit (self, signals[SIGNAL_TRANSITION_STARTED], 0, self->current);
}

static void
on_transition_midpoint (LrgTransition          *transition,
                        LrgTransitionManager   *self)
{
    (void) transition;

    if (!self->midpoint_fired)
    {
        self->midpoint_fired = TRUE;
        g_signal_emit (self, signals[SIGNAL_TRANSITION_MIDPOINT], 0, self->current);
    }
}

static void
on_transition_completed (LrgTransition          *transition,
                         LrgTransitionManager   *self)
{
    (void) transition;

    g_signal_emit (self, signals[SIGNAL_TRANSITION_COMPLETED], 0, self->current);

    /* Clean up the transition */
    if (self->current != NULL)
    {
        lrg_transition_shutdown (self->current);
        g_clear_object (&self->current);
    }

    self->midpoint_fired = FALSE;
}

/*
 * GObject virtual methods
 */

static void
lrg_transition_manager_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgTransitionManager *self;

    self = LRG_TRANSITION_MANAGER (object);

    switch (prop_id)
    {
    case PROP_VIEWPORT_WIDTH:
        g_value_set_uint (value, self->viewport_width);
        break;

    case PROP_VIEWPORT_HEIGHT:
        g_value_set_uint (value, self->viewport_height);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_transition_manager_dispose (GObject *object)
{
    LrgTransitionManager *self;

    self = LRG_TRANSITION_MANAGER (object);

    if (self->current != NULL)
    {
        lrg_transition_shutdown (self->current);
        g_clear_object (&self->current);
    }

    G_OBJECT_CLASS (lrg_transition_manager_parent_class)->dispose (object);
}

static void
lrg_transition_manager_class_init (LrgTransitionManagerClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_transition_manager_get_property;
    object_class->dispose = lrg_transition_manager_dispose;

    /**
     * LrgTransitionManager:viewport-width:
     *
     * Current viewport width.
     *
     * Since: 1.0
     */
    properties[PROP_VIEWPORT_WIDTH] =
        g_param_spec_uint ("viewport-width",
                           "Viewport Width",
                           "Current viewport width",
                           0,
                           G_MAXUINT,
                           0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTransitionManager:viewport-height:
     *
     * Current viewport height.
     *
     * Since: 1.0
     */
    properties[PROP_VIEWPORT_HEIGHT] =
        g_param_spec_uint ("viewport-height",
                           "Viewport Height",
                           "Current viewport height",
                           0,
                           G_MAXUINT,
                           0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTransitionManager::transition-started:
     * @self: The manager
     * @transition: The transition that started
     *
     * Emitted when a transition starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TRANSITION_STARTED] =
        g_signal_new ("transition-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TRANSITION);

    /**
     * LrgTransitionManager::transition-midpoint:
     * @self: The manager
     * @transition: The transition at midpoint
     *
     * Emitted when a transition reaches its midpoint.
     * This is the ideal time to switch scenes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TRANSITION_MIDPOINT] =
        g_signal_new ("transition-midpoint",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TRANSITION);

    /**
     * LrgTransitionManager::transition-completed:
     * @self: The manager
     * @transition: The transition that completed
     *
     * Emitted when a transition completes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TRANSITION_COMPLETED] =
        g_signal_new ("transition-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TRANSITION);
}

static void
lrg_transition_manager_init (LrgTransitionManager *self)
{
    self->current = NULL;
    self->viewport_width = 0;
    self->viewport_height = 0;
    self->initialized = FALSE;
    self->midpoint_fired = FALSE;
}

/*
 * Public API
 */

/**
 * lrg_transition_manager_new:
 *
 * Creates a new transition manager.
 *
 * Returns: (transfer full): A new #LrgTransitionManager
 *
 * Since: 1.0
 */
LrgTransitionManager *
lrg_transition_manager_new (void)
{
    return g_object_new (LRG_TYPE_TRANSITION_MANAGER, NULL);
}

/**
 * lrg_transition_manager_initialize:
 * @self: A #LrgTransitionManager
 * @width: Viewport width
 * @height: Viewport height
 * @error: (nullable): Return location for error
 *
 * Initializes the manager with viewport dimensions.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_transition_manager_initialize (LrgTransitionManager  *self,
                                   guint                  width,
                                   guint                  height,
                                   GError               **error)
{
    g_return_val_if_fail (LRG_IS_TRANSITION_MANAGER (self), FALSE);

    (void) error;

    self->viewport_width = width;
    self->viewport_height = height;
    self->initialized = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Transition manager initialized (viewport: %ux%u)", width, height);

    return TRUE;
}

/**
 * lrg_transition_manager_shutdown:
 * @self: A #LrgTransitionManager
 *
 * Shuts down the manager.
 *
 * Since: 1.0
 */
void
lrg_transition_manager_shutdown (LrgTransitionManager *self)
{
    g_return_if_fail (LRG_IS_TRANSITION_MANAGER (self));

    if (self->current != NULL)
    {
        lrg_transition_shutdown (self->current);
        g_clear_object (&self->current);
    }

    self->initialized = FALSE;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Transition manager shutdown");
}

/**
 * lrg_transition_manager_update:
 * @self: A #LrgTransitionManager
 * @delta_time: Time since last update
 *
 * Updates the active transition.
 *
 * Since: 1.0
 */
void
lrg_transition_manager_update (LrgTransitionManager *self,
                               gfloat                delta_time)
{
    g_return_if_fail (LRG_IS_TRANSITION_MANAGER (self));

    if (self->current == NULL)
    {
        return;
    }

    lrg_transition_update (self->current, delta_time);
}

/**
 * lrg_transition_manager_render:
 * @self: A #LrgTransitionManager
 * @old_scene_texture: Old scene texture ID
 * @new_scene_texture: New scene texture ID
 *
 * Renders the transition effect.
 *
 * Since: 1.0
 */
void
lrg_transition_manager_render (LrgTransitionManager *self,
                               guint                 old_scene_texture,
                               guint                 new_scene_texture)
{
    g_return_if_fail (LRG_IS_TRANSITION_MANAGER (self));

    if (self->current == NULL)
    {
        return;
    }

    lrg_transition_render (self->current,
                           old_scene_texture,
                           new_scene_texture,
                           self->viewport_width,
                           self->viewport_height);
}

/**
 * lrg_transition_manager_start:
 * @self: A #LrgTransitionManager
 * @transition: The transition to start
 *
 * Starts a transition.
 *
 * Since: 1.0
 */
void
lrg_transition_manager_start (LrgTransitionManager *self,
                              LrgTransition        *transition)
{
    g_autoptr(GError) error = NULL;

    g_return_if_fail (LRG_IS_TRANSITION_MANAGER (self));
    g_return_if_fail (LRG_IS_TRANSITION (transition));

    /* Cancel any existing transition */
    if (self->current != NULL)
    {
        lrg_transition_manager_cancel (self);
    }

    /* Take reference and initialize */
    self->current = g_object_ref (transition);
    self->midpoint_fired = FALSE;

    if (!lrg_transition_initialize (transition,
                                    self->viewport_width,
                                    self->viewport_height,
                                    &error))
    {
        lrg_warning (LRG_LOG_DOMAIN_TRANSITION, "Failed to initialize transition: %s",
                         error ? error->message : "Unknown error");
        g_clear_object (&self->current);
        return;
    }

    /* Connect to transition signals */
    g_signal_connect (transition, "started",
                      G_CALLBACK (on_transition_started), self);
    g_signal_connect (transition, "midpoint-reached",
                      G_CALLBACK (on_transition_midpoint), self);
    g_signal_connect (transition, "completed",
                      G_CALLBACK (on_transition_completed), self);

    /* Start the transition */
    lrg_transition_start (transition);
}

/**
 * lrg_transition_manager_cancel:
 * @self: A #LrgTransitionManager
 *
 * Cancels the current transition.
 *
 * Since: 1.0
 */
void
lrg_transition_manager_cancel (LrgTransitionManager *self)
{
    g_return_if_fail (LRG_IS_TRANSITION_MANAGER (self));

    if (self->current == NULL)
    {
        return;
    }

    /* Disconnect signals */
    g_signal_handlers_disconnect_by_data (self->current, self);

    /* Shutdown and release */
    lrg_transition_shutdown (self->current);
    g_clear_object (&self->current);
    self->midpoint_fired = FALSE;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Transition cancelled");
}

/**
 * lrg_transition_manager_is_active:
 * @self: A #LrgTransitionManager
 *
 * Checks if a transition is active.
 *
 * Returns: %TRUE if active
 *
 * Since: 1.0
 */
gboolean
lrg_transition_manager_is_active (LrgTransitionManager *self)
{
    LrgTransitionState state;

    g_return_val_if_fail (LRG_IS_TRANSITION_MANAGER (self), FALSE);

    if (self->current == NULL)
    {
        return FALSE;
    }

    state = lrg_transition_get_state (self->current);
    return state != LRG_TRANSITION_STATE_IDLE &&
           state != LRG_TRANSITION_STATE_COMPLETE;
}

/**
 * lrg_transition_manager_is_at_midpoint:
 * @self: A #LrgTransitionManager
 *
 * Checks if at midpoint.
 *
 * Returns: %TRUE if at midpoint
 *
 * Since: 1.0
 */
gboolean
lrg_transition_manager_is_at_midpoint (LrgTransitionManager *self)
{
    g_return_val_if_fail (LRG_IS_TRANSITION_MANAGER (self), FALSE);

    if (self->current == NULL)
    {
        return FALSE;
    }

    return lrg_transition_is_at_midpoint (self->current);
}

/**
 * lrg_transition_manager_get_current:
 * @self: A #LrgTransitionManager
 *
 * Gets the current transition.
 *
 * Returns: (transfer none) (nullable): The current transition
 *
 * Since: 1.0
 */
LrgTransition *
lrg_transition_manager_get_current (LrgTransitionManager *self)
{
    g_return_val_if_fail (LRG_IS_TRANSITION_MANAGER (self), NULL);

    return self->current;
}

/**
 * lrg_transition_manager_get_state:
 * @self: A #LrgTransitionManager
 *
 * Gets the current transition state.
 *
 * Returns: The transition state
 *
 * Since: 1.0
 */
LrgTransitionState
lrg_transition_manager_get_state (LrgTransitionManager *self)
{
    g_return_val_if_fail (LRG_IS_TRANSITION_MANAGER (self), LRG_TRANSITION_STATE_IDLE);

    if (self->current == NULL)
    {
        return LRG_TRANSITION_STATE_IDLE;
    }

    return lrg_transition_get_state (self->current);
}

/**
 * lrg_transition_manager_set_viewport:
 * @self: A #LrgTransitionManager
 * @width: New width
 * @height: New height
 *
 * Updates viewport dimensions.
 *
 * Since: 1.0
 */
void
lrg_transition_manager_set_viewport (LrgTransitionManager *self,
                                     guint                 width,
                                     guint                 height)
{
    g_return_if_fail (LRG_IS_TRANSITION_MANAGER (self));

    if (self->viewport_width != width)
    {
        self->viewport_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIEWPORT_WIDTH]);
    }

    if (self->viewport_height != height)
    {
        self->viewport_height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIEWPORT_HEIGHT]);
    }
}

/**
 * lrg_transition_manager_get_viewport_width:
 * @self: A #LrgTransitionManager
 *
 * Gets viewport width.
 *
 * Returns: The width
 *
 * Since: 1.0
 */
guint
lrg_transition_manager_get_viewport_width (LrgTransitionManager *self)
{
    g_return_val_if_fail (LRG_IS_TRANSITION_MANAGER (self), 0);

    return self->viewport_width;
}

/**
 * lrg_transition_manager_get_viewport_height:
 * @self: A #LrgTransitionManager
 *
 * Gets viewport height.
 *
 * Returns: The height
 *
 * Since: 1.0
 */
guint
lrg_transition_manager_get_viewport_height (LrgTransitionManager *self)
{
    g_return_val_if_fail (LRG_IS_TRANSITION_MANAGER (self), 0);

    return self->viewport_height;
}
