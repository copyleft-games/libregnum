/* lrg-engine.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Engine singleton implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "lrg-engine.h"
#include "lrg-registry.h"
#include "lrg-data-loader.h"
#include "lrg-asset-manager.h"
#include "../lrg-log.h"
#include "../graphics/lrg-window.h"
#include "../graphics/lrg-renderer.h"
#include "../scripting/lrg-scripting.h"
#ifdef LRG_HAS_LUAJIT
#include "../scripting/lrg-scripting-lua.h"
#endif

typedef struct
{
    LrgEngineState    state;
    LrgRegistry      *registry;
    LrgDataLoader    *data_loader;
    LrgAssetManager  *asset_manager;
    LrgScripting     *scripting;
    LrgWindow        *window;
    LrgRenderer      *renderer;
} LrgEnginePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgEngine, lrg_engine, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_STATE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STARTUP,
    SIGNAL_SHUTDOWN,
    SIGNAL_PRE_UPDATE,
    SIGNAL_POST_UPDATE,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Singleton instance */
static LrgEngine *default_engine = NULL;

/* ==========================================================================
 * Private Functions
 * ========================================================================== */

static void
lrg_engine_real_startup (LrgEngine *self)
{
    LrgEnginePrivate *priv = lrg_engine_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Engine startup");

    /* Create subsystems */
    priv->registry = lrg_registry_new ();
    priv->data_loader = lrg_data_loader_new ();
    priv->asset_manager = lrg_asset_manager_new ();

    /* Connect data loader to registry */
    lrg_data_loader_set_registry (priv->data_loader, priv->registry);

    priv->state = LRG_ENGINE_STATE_RUNNING;
}

static void
lrg_engine_real_shutdown (LrgEngine *self)
{
    LrgEnginePrivate *priv = lrg_engine_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Engine shutdown");

    /* Clean up scripting first (may reference other subsystems) */
    g_clear_object (&priv->scripting);

    /* Clean up graphics subsystems */
    g_clear_object (&priv->renderer);
    /* Note: We don't clear the window here - user manages window lifecycle */

    /* Clean up subsystems */
    g_clear_object (&priv->asset_manager);
    g_clear_object (&priv->data_loader);
    g_clear_object (&priv->registry);

    priv->state = LRG_ENGINE_STATE_TERMINATED;
}

static void
lrg_engine_real_update (LrgEngine *self,
                        gfloat     delta)
{
#ifdef LRG_HAS_LUAJIT
    LrgEnginePrivate *priv = lrg_engine_get_instance_private (self);

    /* Call scripting update hooks if scripting is attached */
    if (priv->scripting != NULL && LRG_IS_SCRIPTING_LUA (priv->scripting))
    {
        lrg_scripting_lua_update (LRG_SCRIPTING_LUA (priv->scripting), delta);
    }
#else
    (void)self;
    (void)delta;
#endif
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_engine_finalize (GObject *object)
{
    LrgEngine        *self = LRG_ENGINE (object);
    LrgEnginePrivate *priv = lrg_engine_get_instance_private (self);

    /* Ensure we're shut down */
    if (priv->state == LRG_ENGINE_STATE_RUNNING ||
        priv->state == LRG_ENGINE_STATE_PAUSED)
    {
        lrg_engine_shutdown (self);
    }

    /* Clean up scripting */
    g_clear_object (&priv->scripting);

    /* Clean up graphics */
    g_clear_object (&priv->renderer);
    g_clear_object (&priv->window);

    /* Clean up subsystems */
    g_clear_object (&priv->asset_manager);
    g_clear_object (&priv->data_loader);
    g_clear_object (&priv->registry);

    /* Clear singleton reference */
    if (default_engine == self)
    {
        default_engine = NULL;
    }

    G_OBJECT_CLASS (lrg_engine_parent_class)->finalize (object);
}

static void
lrg_engine_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LrgEngine        *self = LRG_ENGINE (object);
    LrgEnginePrivate *priv = lrg_engine_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_STATE:
        g_value_set_enum (value, priv->state);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_engine_class_init (LrgEngineClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_engine_finalize;
    object_class->get_property = lrg_engine_get_property;

    /* Virtual methods */
    klass->startup = lrg_engine_real_startup;
    klass->shutdown = lrg_engine_real_shutdown;
    klass->update = lrg_engine_real_update;

    /**
     * LrgEngine:state:
     *
     * The current state of the engine.
     */
    properties[PROP_STATE] =
        g_param_spec_enum ("state",
                           "State",
                           "The current engine state",
                           LRG_TYPE_ENGINE_STATE,
                           LRG_ENGINE_STATE_UNINITIALIZED,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgEngine::startup:
     * @engine: the engine that emitted the signal
     *
     * Emitted after the engine has been initialized.
     */
    signals[SIGNAL_STARTUP] =
        g_signal_new ("startup",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgEngine::shutdown:
     * @engine: the engine that emitted the signal
     *
     * Emitted before the engine shuts down.
     */
    signals[SIGNAL_SHUTDOWN] =
        g_signal_new ("shutdown",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgEngine::pre-update:
     * @engine: the engine that emitted the signal
     * @delta: the time since last frame in seconds
     *
     * Emitted before the engine update.
     */
    signals[SIGNAL_PRE_UPDATE] =
        g_signal_new ("pre-update",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_FLOAT);

    /**
     * LrgEngine::post-update:
     * @engine: the engine that emitted the signal
     * @delta: the time since last frame in seconds
     *
     * Emitted after the engine update.
     */
    signals[SIGNAL_POST_UPDATE] =
        g_signal_new ("post-update",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_FLOAT);
}

static void
lrg_engine_init (LrgEngine *self)
{
    LrgEnginePrivate *priv = lrg_engine_get_instance_private (self);

    priv->state = LRG_ENGINE_STATE_UNINITIALIZED;
    priv->registry = NULL;
    priv->data_loader = NULL;
    priv->asset_manager = NULL;
    priv->scripting = NULL;
    priv->window = NULL;
    priv->renderer = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_engine_get_default:
 *
 * Gets the default engine instance. Creates it if it doesn't exist.
 *
 * Returns: (transfer none): The default #LrgEngine instance
 */
LrgEngine *
lrg_engine_get_default (void)
{
    if (default_engine == NULL)
    {
        default_engine = g_object_new (LRG_TYPE_ENGINE, NULL);
    }

    return default_engine;
}

/**
 * lrg_engine_startup:
 * @self: an #LrgEngine
 * @error: (nullable): return location for error
 *
 * Starts up the engine and all subsystems.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_engine_startup (LrgEngine  *self,
                    GError    **error)
{
    LrgEnginePrivate *priv;
    LrgEngineClass   *klass;

    g_return_val_if_fail (LRG_IS_ENGINE (self), FALSE);

    priv = lrg_engine_get_instance_private (self);

    if (priv->state != LRG_ENGINE_STATE_UNINITIALIZED &&
        priv->state != LRG_ENGINE_STATE_TERMINATED)
    {
        g_set_error (error,
                     LRG_ENGINE_ERROR,
                     LRG_ENGINE_ERROR_STATE,
                     "Engine is already started");
        return FALSE;
    }

    priv->state = LRG_ENGINE_STATE_INITIALIZING;

    /* Call virtual method */
    klass = LRG_ENGINE_GET_CLASS (self);
    if (klass->startup != NULL)
    {
        klass->startup (self);
    }

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_STARTUP], 0);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);

    lrg_info (LRG_LOG_DOMAIN_CORE, "Libregnum engine v%s started", LRG_VERSION_STRING);

    return TRUE;
}

/**
 * lrg_engine_shutdown:
 * @self: an #LrgEngine
 *
 * Shuts down the engine and all subsystems.
 */
void
lrg_engine_shutdown (LrgEngine *self)
{
    LrgEnginePrivate *priv;
    LrgEngineClass   *klass;

    g_return_if_fail (LRG_IS_ENGINE (self));

    priv = lrg_engine_get_instance_private (self);

    if (priv->state != LRG_ENGINE_STATE_RUNNING &&
        priv->state != LRG_ENGINE_STATE_PAUSED)
    {
        return;
    }

    priv->state = LRG_ENGINE_STATE_SHUTTING_DOWN;

    /* Emit signal first so handlers can clean up */
    g_signal_emit (self, signals[SIGNAL_SHUTDOWN], 0);

    /* Call virtual method */
    klass = LRG_ENGINE_GET_CLASS (self);
    if (klass->shutdown != NULL)
    {
        klass->shutdown (self);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);

    lrg_info (LRG_LOG_DOMAIN_CORE, "Libregnum engine shutdown complete");
}

/**
 * lrg_engine_update:
 * @self: an #LrgEngine
 * @delta: time since last frame in seconds
 *
 * Updates the engine for one frame.
 */
void
lrg_engine_update (LrgEngine *self,
                   gfloat     delta)
{
    LrgEnginePrivate *priv;
    LrgEngineClass   *klass;

    g_return_if_fail (LRG_IS_ENGINE (self));

    priv = lrg_engine_get_instance_private (self);

    if (priv->state != LRG_ENGINE_STATE_RUNNING)
    {
        return;
    }

    /* Pre-update signal */
    g_signal_emit (self, signals[SIGNAL_PRE_UPDATE], 0, delta);

    /* Call virtual method */
    klass = LRG_ENGINE_GET_CLASS (self);
    if (klass->update != NULL)
    {
        klass->update (self, delta);
    }

    /* Post-update signal */
    g_signal_emit (self, signals[SIGNAL_POST_UPDATE], 0, delta);
}

/**
 * lrg_engine_get_state:
 * @self: an #LrgEngine
 *
 * Gets the current engine state.
 *
 * Returns: The current #LrgEngineState
 */
LrgEngineState
lrg_engine_get_state (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), LRG_ENGINE_STATE_UNINITIALIZED);

    priv = lrg_engine_get_instance_private (self);

    return priv->state;
}

/**
 * lrg_engine_is_running:
 * @self: an #LrgEngine
 *
 * Checks if the engine is in the running state.
 *
 * Returns: %TRUE if the engine is running
 */
gboolean
lrg_engine_is_running (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), FALSE);

    priv = lrg_engine_get_instance_private (self);

    return priv->state == LRG_ENGINE_STATE_RUNNING;
}

/**
 * lrg_engine_get_registry:
 * @self: an #LrgEngine
 *
 * Gets the engine's type registry.
 *
 * Returns: (transfer none): The #LrgRegistry
 */
LrgRegistry *
lrg_engine_get_registry (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), NULL);

    priv = lrg_engine_get_instance_private (self);

    return priv->registry;
}

/**
 * lrg_engine_get_data_loader:
 * @self: an #LrgEngine
 *
 * Gets the engine's data loader.
 *
 * Returns: (transfer none): The #LrgDataLoader
 */
LrgDataLoader *
lrg_engine_get_data_loader (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), NULL);

    priv = lrg_engine_get_instance_private (self);

    return priv->data_loader;
}

/**
 * lrg_engine_get_asset_manager:
 * @self: an #LrgEngine
 *
 * Gets the engine's asset manager.
 *
 * Returns: (transfer none): The #LrgAssetManager
 */
LrgAssetManager *
lrg_engine_get_asset_manager (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), NULL);

    priv = lrg_engine_get_instance_private (self);

    return priv->asset_manager;
}

/**
 * lrg_engine_get_scripting:
 * @self: an #LrgEngine
 *
 * Gets the engine's scripting subsystem.
 *
 * Returns: (transfer none) (nullable): The #LrgScripting, or %NULL if not set
 */
LrgScripting *
lrg_engine_get_scripting (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), NULL);

    priv = lrg_engine_get_instance_private (self);

    return priv->scripting;
}

/**
 * lrg_engine_set_scripting:
 * @self: an #LrgEngine
 * @scripting: (transfer none) (nullable): the scripting subsystem to use
 *
 * Sets the scripting subsystem for the engine.
 *
 * If the scripting is an #LrgScriptingLua instance, it is automatically
 * connected to the engine's registry for type lookups.
 */
void
lrg_engine_set_scripting (LrgEngine    *self,
                          LrgScripting *scripting)
{
    LrgEnginePrivate *priv;

    g_return_if_fail (LRG_IS_ENGINE (self));
    g_return_if_fail (scripting == NULL || LRG_IS_SCRIPTING (scripting));

    priv = lrg_engine_get_instance_private (self);

    /* Same scripting, nothing to do */
    if (priv->scripting == scripting)
        return;

    /* Clear existing scripting */
    g_clear_object (&priv->scripting);

    /* Set new scripting and connect to registry */
    if (scripting != NULL)
    {
        priv->scripting = g_object_ref (scripting);

        /* If it's a Lua scripting instance, connect to the registry */
#ifdef LRG_HAS_LUAJIT
        if (LRG_IS_SCRIPTING_LUA (scripting) && priv->registry != NULL)
        {
            lrg_scripting_lua_set_registry (LRG_SCRIPTING_LUA (scripting),
                                            priv->registry);
        }
#endif

        lrg_debug (LRG_LOG_DOMAIN_CORE, "Scripting subsystem attached");
    }
    else
    {
        lrg_debug (LRG_LOG_DOMAIN_CORE, "Scripting subsystem detached");
    }
}

/* ==========================================================================
 * Graphics Subsystem Access
 * ========================================================================== */

/**
 * lrg_engine_set_window:
 * @self: an #LrgEngine
 * @window: (transfer none) (nullable): the window to use for rendering
 *
 * Sets the window for the engine. When a window is set, a renderer
 * is automatically created. Pass %NULL to disconnect the window.
 */
void
lrg_engine_set_window (LrgEngine *self,
                       LrgWindow *window)
{
    LrgEnginePrivate *priv;

    g_return_if_fail (LRG_IS_ENGINE (self));
    g_return_if_fail (window == NULL || LRG_IS_WINDOW (window));

    priv = lrg_engine_get_instance_private (self);

    /* Same window, nothing to do */
    if (priv->window == window)
        return;

    /* Clean up existing renderer */
    g_clear_object (&priv->renderer);
    g_clear_object (&priv->window);

    /* Set new window and create renderer */
    if (window != NULL)
    {
        priv->window = g_object_ref (window);
        priv->renderer = lrg_renderer_new (window);

        lrg_debug (LRG_LOG_DOMAIN_CORE, "Window and renderer created");
    }
    else
    {
        lrg_debug (LRG_LOG_DOMAIN_CORE, "Window disconnected, running headless");
    }
}

/**
 * lrg_engine_get_window:
 * @self: an #LrgEngine
 *
 * Gets the engine's window.
 *
 * Returns: (transfer none) (nullable): The #LrgWindow, or %NULL if headless
 */
LrgWindow *
lrg_engine_get_window (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), NULL);

    priv = lrg_engine_get_instance_private (self);

    return priv->window;
}

/**
 * lrg_engine_get_renderer:
 * @self: an #LrgEngine
 *
 * Gets the engine's renderer.
 *
 * Returns: (transfer none) (nullable): The #LrgRenderer, or %NULL if headless
 */
LrgRenderer *
lrg_engine_get_renderer (LrgEngine *self)
{
    LrgEnginePrivate *priv;

    g_return_val_if_fail (LRG_IS_ENGINE (self), NULL);

    priv = lrg_engine_get_instance_private (self);

    return priv->renderer;
}

/* ==========================================================================
 * Version Functions
 * ========================================================================== */

/**
 * lrg_get_major_version:
 *
 * Gets the major version of the Libregnum library at runtime.
 *
 * Returns: The major version
 */
guint
lrg_get_major_version (void)
{
    return LRG_VERSION_MAJOR;
}

/**
 * lrg_get_minor_version:
 *
 * Gets the minor version of the Libregnum library at runtime.
 *
 * Returns: The minor version
 */
guint
lrg_get_minor_version (void)
{
    return LRG_VERSION_MINOR;
}

/**
 * lrg_get_micro_version:
 *
 * Gets the micro version of the Libregnum library at runtime.
 *
 * Returns: The micro version
 */
guint
lrg_get_micro_version (void)
{
    return LRG_VERSION_MICRO;
}

/**
 * lrg_check_version:
 * @required_major: the required major version
 * @required_minor: the required minor version
 * @required_micro: the required micro version
 *
 * Checks at runtime if the Libregnum library is at least the
 * specified version.
 *
 * Returns: %TRUE if the version is compatible
 */
gboolean
lrg_check_version (guint required_major,
                   guint required_minor,
                   guint required_micro)
{
    /*
     * Runtime version check using signed comparisons to avoid
     * warnings when version constants are 0
     */
    gint lib_major = (gint)LRG_VERSION_MAJOR;
    gint lib_minor = (gint)LRG_VERSION_MINOR;
    gint lib_micro = (gint)LRG_VERSION_MICRO;
    gint req_major = (gint)required_major;
    gint req_minor = (gint)required_minor;
    gint req_micro = (gint)required_micro;

    if (lib_major != req_major)
        return lib_major > req_major;

    if (lib_minor != req_minor)
        return lib_minor > req_minor;

    return lib_micro >= req_micro;
}
