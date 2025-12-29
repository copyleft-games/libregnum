/* lrg-post-processor.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-post-processor.h"

/**
 * SECTION:lrg-post-processor
 * @Title: LrgPostProcessor
 * @Short_description: Post-processing pipeline manager
 *
 * #LrgPostProcessor manages a chain of post-processing effects.
 * The scene is rendered to an offscreen texture, then each effect
 * is applied in sequence, with the final result rendered to screen.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * g_autoptr(LrgPostProcessor) processor = lrg_post_processor_new (800, 600);
 *
 * // Add effects
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (bloom));
 * lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (vignette));
 *
 * // In render loop
 * lrg_post_processor_begin_capture (processor);
 * // ... render scene normally ...
 * lrg_post_processor_end_capture (processor);
 * lrg_post_processor_render (processor, delta_time);
 * ]|
 */

typedef struct
{
    GList    *effects;          /* List of LrgPostEffect */
    guint     width;
    guint     height;
    gboolean  enabled;
    gboolean  capturing;
    gboolean  initialized;

    /* Ping-pong render textures for effect chain */
    guint     render_texture_a;     /* OpenGL texture ID */
    guint     render_texture_b;     /* OpenGL texture ID */
    guint     framebuffer_a;        /* OpenGL FBO ID */
    guint     framebuffer_b;        /* OpenGL FBO ID */
    guint     current_source;       /* Which texture is current source (0 = A, 1 = B) */
} LrgPostProcessorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgPostProcessor, lrg_post_processor, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_ENABLED,
    PROP_EFFECT_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gint
compare_effect_priority (gconstpointer a,
                         gconstpointer b)
{
    LrgPostEffect *effect_a = LRG_POST_EFFECT ((gpointer)a);
    LrgPostEffect *effect_b = LRG_POST_EFFECT ((gpointer)b);

    return lrg_post_effect_get_priority (effect_a) -
           lrg_post_effect_get_priority (effect_b);
}

/* Default virtual method implementations */

static void
lrg_post_processor_real_begin_capture (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

    if (!priv->enabled || !priv->initialized)
        return;

    priv->capturing = TRUE;
    priv->current_source = 0;

    /*
     * NOTE: Actual OpenGL binding would happen here:
     * glBindFramebuffer(GL_FRAMEBUFFER, priv->framebuffer_a);
     * glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
     *
     * This is left as a stub since graylib handles the actual rendering.
     * In practice, this would use GrlRenderTexture.
     */
}

static void
lrg_post_processor_real_end_capture (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

    if (!priv->capturing)
        return;

    /*
     * NOTE: Unbind the framebuffer:
     * glBindFramebuffer(GL_FRAMEBUFFER, 0);
     */
}

static void
lrg_post_processor_real_render (LrgPostProcessor *self,
                                gfloat            delta_time)
{
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);
    GList *l;
    guint  source_tex;
    guint  target_tex;
    guint  target_fbo;

    if (!priv->enabled || !priv->initialized)
        return;

    priv->capturing = FALSE;

    /* Apply each enabled effect in priority order */
    for (l = priv->effects; l != NULL; l = l->next)
    {
        LrgPostEffect *effect = LRG_POST_EFFECT (l->data);

        if (!lrg_post_effect_is_enabled (effect))
            continue;

        /* Determine source and target based on ping-pong state */
        if (priv->current_source == 0)
        {
            source_tex = priv->render_texture_a;
            target_tex = priv->render_texture_b;
            target_fbo = priv->framebuffer_b;
        }
        else
        {
            source_tex = priv->render_texture_b;
            target_tex = priv->render_texture_a;
            target_fbo = priv->framebuffer_a;
        }

        /*
         * NOTE: Actual rendering would happen here:
         * glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
         * ... apply effect ...
         */
        (void)target_fbo;  /* Suppress unused variable warning */

        lrg_post_effect_apply (effect, source_tex, target_tex,
                               priv->width, priv->height, delta_time);

        /* Swap source/target for next effect */
        priv->current_source = (priv->current_source == 0) ? 1 : 0;
    }

    /*
     * NOTE: Final blit to screen would happen here:
     * glBindFramebuffer(GL_FRAMEBUFFER, 0);
     * ... render fullscreen quad with final texture ...
     */
}

static void
lrg_post_processor_finalize (GObject *object)
{
    LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

    /* Shutdown all effects */
    if (priv->effects != NULL)
    {
        GList *l;

        for (l = priv->effects; l != NULL; l = l->next)
        {
            LrgPostEffect *effect = LRG_POST_EFFECT (l->data);
            lrg_post_effect_shutdown (effect);
        }

        g_list_free_full (priv->effects, g_object_unref);
        priv->effects = NULL;
    }

    /*
     * NOTE: Clean up OpenGL resources:
     * glDeleteTextures(1, &priv->render_texture_a);
     * glDeleteTextures(1, &priv->render_texture_b);
     * glDeleteFramebuffers(1, &priv->framebuffer_a);
     * glDeleteFramebuffers(1, &priv->framebuffer_b);
     */

    G_OBJECT_CLASS (lrg_post_processor_parent_class)->finalize (object);
}

static void
lrg_post_processor_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_WIDTH:
        g_value_set_uint (value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint (value, priv->height);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_EFFECT_COUNT:
        g_value_set_uint (value, g_list_length (priv->effects));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_post_processor_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_WIDTH:
        priv->width = g_value_get_uint (value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint (value);
        break;
    case PROP_ENABLED:
        priv->enabled = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_post_processor_constructed (GObject *object)
{
    LrgPostProcessor        *self = LRG_POST_PROCESSOR (object);
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

    G_OBJECT_CLASS (lrg_post_processor_parent_class)->constructed (object);

    /*
     * NOTE: Create OpenGL resources here:
     * - Create render textures A and B
     * - Create framebuffers A and B
     * - Attach textures to framebuffers
     */

    priv->initialized = TRUE;
}

static void
lrg_post_processor_class_init (LrgPostProcessorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_post_processor_finalize;
    object_class->get_property = lrg_post_processor_get_property;
    object_class->set_property = lrg_post_processor_set_property;
    object_class->constructed = lrg_post_processor_constructed;

    /* Virtual methods */
    klass->begin_capture = lrg_post_processor_real_begin_capture;
    klass->end_capture = lrg_post_processor_real_end_capture;
    klass->render = lrg_post_processor_real_render;

    /**
     * LrgPostProcessor:width:
     *
     * Render target width.
     */
    properties[PROP_WIDTH] =
        g_param_spec_uint ("width",
                           "Width",
                           "Render target width",
                           1, G_MAXUINT, 800,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgPostProcessor:height:
     *
     * Render target height.
     */
    properties[PROP_HEIGHT] =
        g_param_spec_uint ("height",
                           "Height",
                           "Render target height",
                           1, G_MAXUINT, 600,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgPostProcessor:enabled:
     *
     * Whether post-processing is enabled.
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether post-processing is enabled",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPostProcessor:effect-count:
     *
     * Number of effects in the chain.
     */
    properties[PROP_EFFECT_COUNT] =
        g_param_spec_uint ("effect-count",
                           "Effect Count",
                           "Number of effects in the chain",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_post_processor_init (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv = lrg_post_processor_get_instance_private (self);

    priv->effects = NULL;
    priv->width = 800;
    priv->height = 600;
    priv->enabled = TRUE;
    priv->capturing = FALSE;
    priv->initialized = FALSE;
    priv->render_texture_a = 0;
    priv->render_texture_b = 0;
    priv->framebuffer_a = 0;
    priv->framebuffer_b = 0;
    priv->current_source = 0;
}

/**
 * lrg_post_processor_new:
 * @width: Initial render target width
 * @height: Initial render target height
 *
 * Creates a new post-processor.
 *
 * Returns: (transfer full): A new #LrgPostProcessor
 */
LrgPostProcessor *
lrg_post_processor_new (guint width,
                        guint height)
{
    return g_object_new (LRG_TYPE_POST_PROCESSOR,
                         "width", width,
                         "height", height,
                         NULL);
}

/**
 * lrg_post_processor_add_effect:
 * @self: A #LrgPostProcessor
 * @effect: (transfer none): Effect to add
 *
 * Adds an effect to the processing chain.
 */
void
lrg_post_processor_add_effect (LrgPostProcessor *self,
                               LrgPostEffect    *effect)
{
    LrgPostProcessorPrivate *priv;
    g_autoptr(GError) error = NULL;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));
    g_return_if_fail (LRG_IS_POST_EFFECT (effect));

    priv = lrg_post_processor_get_instance_private (self);

    /* Initialize the effect if processor is already initialized */
    if (priv->initialized && !lrg_post_effect_is_initialized (effect))
    {
        if (!lrg_post_effect_initialize (effect, priv->width, priv->height, &error))
        {
            g_warning ("Failed to initialize effect: %s",
                       error ? error->message : "Unknown error");
            return;
        }
    }

    priv->effects = g_list_insert_sorted (priv->effects,
                                          g_object_ref (effect),
                                          compare_effect_priority);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECT_COUNT]);
}

/**
 * lrg_post_processor_remove_effect:
 * @self: A #LrgPostProcessor
 * @effect: Effect to remove
 *
 * Removes an effect from the processing chain.
 */
void
lrg_post_processor_remove_effect (LrgPostProcessor *self,
                                  LrgPostEffect    *effect)
{
    LrgPostProcessorPrivate *priv;
    GList *l;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));
    g_return_if_fail (LRG_IS_POST_EFFECT (effect));

    priv = lrg_post_processor_get_instance_private (self);

    l = g_list_find (priv->effects, effect);
    if (l != NULL)
    {
        lrg_post_effect_shutdown (effect);
        priv->effects = g_list_delete_link (priv->effects, l);
        g_object_unref (effect);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECT_COUNT]);
    }
}

/**
 * lrg_post_processor_get_effect:
 * @self: A #LrgPostProcessor
 * @name: Effect name to find
 *
 * Finds an effect by name.
 *
 * Returns: (transfer none) (nullable): The effect, or %NULL
 */
LrgPostEffect *
lrg_post_processor_get_effect (LrgPostProcessor *self,
                               const gchar      *name)
{
    LrgPostProcessorPrivate *priv;
    GList *l;

    g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_post_processor_get_instance_private (self);

    for (l = priv->effects; l != NULL; l = l->next)
    {
        LrgPostEffect *effect = LRG_POST_EFFECT (l->data);
        const gchar *effect_name = lrg_post_effect_get_name (effect);

        if (g_strcmp0 (effect_name, name) == 0)
            return effect;
    }

    return NULL;
}

/**
 * lrg_post_processor_get_effects:
 * @self: A #LrgPostProcessor
 *
 * Gets all effects in the chain.
 *
 * Returns: (transfer none) (element-type LrgPostEffect): The effect list
 */
GList *
lrg_post_processor_get_effects (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), NULL);

    priv = lrg_post_processor_get_instance_private (self);

    return priv->effects;
}

/**
 * lrg_post_processor_get_effect_count:
 * @self: A #LrgPostProcessor
 *
 * Gets the number of effects.
 *
 * Returns: The effect count
 */
guint
lrg_post_processor_get_effect_count (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

    priv = lrg_post_processor_get_instance_private (self);

    return g_list_length (priv->effects);
}

/**
 * lrg_post_processor_clear_effects:
 * @self: A #LrgPostProcessor
 *
 * Removes all effects.
 */
void
lrg_post_processor_clear_effects (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;
    GList *l;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

    priv = lrg_post_processor_get_instance_private (self);

    for (l = priv->effects; l != NULL; l = l->next)
    {
        LrgPostEffect *effect = LRG_POST_EFFECT (l->data);
        lrg_post_effect_shutdown (effect);
    }

    g_list_free_full (priv->effects, g_object_unref);
    priv->effects = NULL;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECT_COUNT]);
}

/**
 * lrg_post_processor_begin_capture:
 * @self: A #LrgPostProcessor
 *
 * Begins scene capture.
 */
void
lrg_post_processor_begin_capture (LrgPostProcessor *self)
{
    LrgPostProcessorClass *klass;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

    klass = LRG_POST_PROCESSOR_GET_CLASS (self);

    if (klass->begin_capture != NULL)
        klass->begin_capture (self);
}

/**
 * lrg_post_processor_end_capture:
 * @self: A #LrgPostProcessor
 *
 * Ends scene capture.
 */
void
lrg_post_processor_end_capture (LrgPostProcessor *self)
{
    LrgPostProcessorClass *klass;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

    klass = LRG_POST_PROCESSOR_GET_CLASS (self);

    if (klass->end_capture != NULL)
        klass->end_capture (self);
}

/**
 * lrg_post_processor_render:
 * @self: A #LrgPostProcessor
 * @delta_time: Time since last frame
 *
 * Applies the effect chain and renders to screen.
 */
void
lrg_post_processor_render (LrgPostProcessor *self,
                           gfloat            delta_time)
{
    LrgPostProcessorClass *klass;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

    klass = LRG_POST_PROCESSOR_GET_CLASS (self);

    if (klass->render != NULL)
        klass->render (self, delta_time);
}

/**
 * lrg_post_processor_resize:
 * @self: A #LrgPostProcessor
 * @width: New width
 * @height: New height
 *
 * Resizes the render targets.
 */
void
lrg_post_processor_resize (LrgPostProcessor *self,
                           guint             width,
                           guint             height)
{
    LrgPostProcessorPrivate *priv;
    GList *l;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));
    g_return_if_fail (width > 0 && height > 0);

    priv = lrg_post_processor_get_instance_private (self);

    if (priv->width == width && priv->height == height)
        return;

    priv->width = width;
    priv->height = height;

    /*
     * NOTE: Recreate render textures with new size:
     * glDeleteTextures / glGenTextures / glTexImage2D
     */

    /* Notify all effects of resize */
    for (l = priv->effects; l != NULL; l = l->next)
    {
        LrgPostEffect *effect = LRG_POST_EFFECT (l->data);
        lrg_post_effect_resize (effect, width, height);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
}

/**
 * lrg_post_processor_get_width:
 * @self: A #LrgPostProcessor
 *
 * Gets the render target width.
 *
 * Returns: The width
 */
guint
lrg_post_processor_get_width (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

    priv = lrg_post_processor_get_instance_private (self);

    return priv->width;
}

/**
 * lrg_post_processor_get_height:
 * @self: A #LrgPostProcessor
 *
 * Gets the render target height.
 *
 * Returns: The height
 */
guint
lrg_post_processor_get_height (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), 0);

    priv = lrg_post_processor_get_instance_private (self);

    return priv->height;
}

/**
 * lrg_post_processor_is_enabled:
 * @self: A #LrgPostProcessor
 *
 * Checks if post-processing is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_post_processor_is_enabled (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), FALSE);

    priv = lrg_post_processor_get_instance_private (self);

    return priv->enabled;
}

/**
 * lrg_post_processor_set_enabled:
 * @self: A #LrgPostProcessor
 * @enabled: Whether to enable
 *
 * Enables or disables post-processing.
 */
void
lrg_post_processor_set_enabled (LrgPostProcessor *self,
                                gboolean          enabled)
{
    LrgPostProcessorPrivate *priv;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

    priv = lrg_post_processor_get_instance_private (self);

    if (priv->enabled == enabled)
        return;

    priv->enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

/**
 * lrg_post_processor_is_capturing:
 * @self: A #LrgPostProcessor
 *
 * Checks if currently capturing.
 *
 * Returns: %TRUE if capturing
 */
gboolean
lrg_post_processor_is_capturing (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_PROCESSOR (self), FALSE);

    priv = lrg_post_processor_get_instance_private (self);

    return priv->capturing;
}

/**
 * lrg_post_processor_sort_effects:
 * @self: A #LrgPostProcessor
 *
 * Re-sorts effects by priority.
 */
void
lrg_post_processor_sort_effects (LrgPostProcessor *self)
{
    LrgPostProcessorPrivate *priv;

    g_return_if_fail (LRG_IS_POST_PROCESSOR (self));

    priv = lrg_post_processor_get_instance_private (self);

    priv->effects = g_list_sort (priv->effects, compare_effect_priority);
}
