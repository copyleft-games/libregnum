/* lrg-post-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-post-effect.h"

/**
 * SECTION:lrg-post-effect
 * @Title: LrgPostEffect
 * @Short_description: Base class for post-processing effects
 *
 * #LrgPostEffect is an abstract base class for implementing post-processing
 * effects. Subclasses implement specific effects like bloom, vignette,
 * color grading, etc.
 *
 * Effects have a lifecycle managed by the #LrgPostProcessor:
 * 1. Construction (g_object_new)
 * 2. Initialization (lrg_post_effect_initialize) - create GPU resources
 * 3. Application (lrg_post_effect_apply) - called each frame
 * 4. Resize (lrg_post_effect_resize) - called when render target changes
 * 5. Shutdown (lrg_post_effect_shutdown) - release GPU resources
 *
 * Example of a custom effect:
 * |[<!-- language="C" -->
 * struct _MyEffect
 * {
 *     LrgPostEffect parent_instance;
 *     GrlShader *shader;
 * };
 *
 * G_DEFINE_TYPE (MyEffect, my_effect, LRG_TYPE_POST_EFFECT)
 *
 * static gboolean
 * my_effect_initialize (LrgPostEffect *self, guint w, guint h, GError **error)
 * {
 *     MyEffect *effect = MY_EFFECT (self);
 *     effect->shader = grl_shader_new_from_files ("my_effect.vs", "my_effect.fs", error);
 *     return effect->shader != NULL;
 * }
 *
 * static void
 * my_effect_apply (LrgPostEffect *self, guint src, guint dst, guint w, guint h, gfloat dt)
 * {
 *     MyEffect *effect = MY_EFFECT (self);
 *     // Bind shader, set uniforms, render fullscreen quad
 * }
 * ]|
 */

typedef struct
{
    gboolean enabled;
    gboolean initialized;
    gfloat   intensity;
    gint     priority;
    guint    width;
    guint    height;
} LrgPostEffectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgPostEffect, lrg_post_effect, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ENABLED,
    PROP_INTENSITY,
    PROP_PRIORITY,
    PROP_INITIALIZED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default implementations */

static gboolean
lrg_post_effect_real_initialize (LrgPostEffect  *self,
                                 guint           width,
                                 guint           height,
                                 GError        **error)
{
    LrgPostEffectPrivate *priv = lrg_post_effect_get_instance_private (self);

    priv->width = width;
    priv->height = height;
    priv->initialized = TRUE;

    return TRUE;
}

static void
lrg_post_effect_real_shutdown (LrgPostEffect *self)
{
    LrgPostEffectPrivate *priv = lrg_post_effect_get_instance_private (self);

    priv->initialized = FALSE;
}

static void
lrg_post_effect_real_apply (LrgPostEffect *self,
                            guint          source_texture_id,
                            guint          target_texture_id,
                            guint          width,
                            guint          height,
                            gfloat         delta_time)
{
    /* Default implementation does nothing - subclasses must override */
}

static void
lrg_post_effect_real_resize (LrgPostEffect *self,
                             guint          width,
                             guint          height)
{
    LrgPostEffectPrivate *priv = lrg_post_effect_get_instance_private (self);

    priv->width = width;
    priv->height = height;
}

static const gchar *
lrg_post_effect_real_get_name (LrgPostEffect *self)
{
    return "Unknown Effect";
}

static void
lrg_post_effect_finalize (GObject *object)
{
    LrgPostEffect        *self = LRG_POST_EFFECT (object);
    LrgPostEffectPrivate *priv = lrg_post_effect_get_instance_private (self);

    if (priv->initialized)
        lrg_post_effect_shutdown (self);

    G_OBJECT_CLASS (lrg_post_effect_parent_class)->finalize (object);
}

static void
lrg_post_effect_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgPostEffect        *self = LRG_POST_EFFECT (object);
    LrgPostEffectPrivate *priv = lrg_post_effect_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_INTENSITY:
        g_value_set_float (value, priv->intensity);
        break;
    case PROP_PRIORITY:
        g_value_set_int (value, priv->priority);
        break;
    case PROP_INITIALIZED:
        g_value_set_boolean (value, priv->initialized);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_post_effect_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgPostEffect        *self = LRG_POST_EFFECT (object);
    LrgPostEffectPrivate *priv = lrg_post_effect_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ENABLED:
        priv->enabled = g_value_get_boolean (value);
        break;
    case PROP_INTENSITY:
        priv->intensity = CLAMP (g_value_get_float (value), 0.0f, 1.0f);
        break;
    case PROP_PRIORITY:
        priv->priority = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_post_effect_class_init (LrgPostEffectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_post_effect_finalize;
    object_class->get_property = lrg_post_effect_get_property;
    object_class->set_property = lrg_post_effect_set_property;

    /* Virtual methods */
    klass->initialize = lrg_post_effect_real_initialize;
    klass->shutdown = lrg_post_effect_real_shutdown;
    klass->apply = lrg_post_effect_real_apply;
    klass->resize = lrg_post_effect_real_resize;
    klass->get_name = lrg_post_effect_real_get_name;

    /**
     * LrgPostEffect:enabled:
     *
     * Whether the effect is enabled.
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether the effect is enabled",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPostEffect:intensity:
     *
     * Effect intensity (0.0 = off, 1.0 = full).
     */
    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity",
                            "Intensity",
                            "Effect intensity (0.0 - 1.0)",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPostEffect:priority:
     *
     * Effect priority (higher = applied later).
     */
    properties[PROP_PRIORITY] =
        g_param_spec_int ("priority",
                          "Priority",
                          "Effect priority (higher = later in chain)",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPostEffect:initialized:
     *
     * Whether the effect has been initialized.
     */
    properties[PROP_INITIALIZED] =
        g_param_spec_boolean ("initialized",
                              "Initialized",
                              "Whether the effect is initialized",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_post_effect_init (LrgPostEffect *self)
{
    LrgPostEffectPrivate *priv = lrg_post_effect_get_instance_private (self);

    priv->enabled = TRUE;
    priv->initialized = FALSE;
    priv->intensity = 1.0f;
    priv->priority = 0;
    priv->width = 0;
    priv->height = 0;
}

/**
 * lrg_post_effect_initialize:
 * @self: A #LrgPostEffect
 * @width: Render target width
 * @height: Render target height
 * @error: (nullable): Return location for error
 *
 * Initializes the effect's GPU resources.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_post_effect_initialize (LrgPostEffect  *self,
                            guint           width,
                            guint           height,
                            GError        **error)
{
    LrgPostEffectClass *klass;

    g_return_val_if_fail (LRG_IS_POST_EFFECT (self), FALSE);
    g_return_val_if_fail (width > 0 && height > 0, FALSE);

    klass = LRG_POST_EFFECT_GET_CLASS (self);

    if (klass->initialize != NULL)
        return klass->initialize (self, width, height, error);

    return TRUE;
}

/**
 * lrg_post_effect_shutdown:
 * @self: A #LrgPostEffect
 *
 * Releases the effect's GPU resources.
 */
void
lrg_post_effect_shutdown (LrgPostEffect *self)
{
    LrgPostEffectClass *klass;

    g_return_if_fail (LRG_IS_POST_EFFECT (self));

    klass = LRG_POST_EFFECT_GET_CLASS (self);

    if (klass->shutdown != NULL)
        klass->shutdown (self);
}

/**
 * lrg_post_effect_apply:
 * @self: A #LrgPostEffect
 * @source_texture_id: OpenGL texture ID of the input
 * @target_texture_id: OpenGL texture ID of the output
 * @width: Texture width
 * @height: Texture height
 * @delta_time: Time since last frame
 *
 * Applies the effect.
 */
void
lrg_post_effect_apply (LrgPostEffect *self,
                       guint          source_texture_id,
                       guint          target_texture_id,
                       guint          width,
                       guint          height,
                       gfloat         delta_time)
{
    LrgPostEffectClass   *klass;
    LrgPostEffectPrivate *priv;

    g_return_if_fail (LRG_IS_POST_EFFECT (self));

    priv = lrg_post_effect_get_instance_private (self);

    if (!priv->enabled || !priv->initialized)
        return;

    klass = LRG_POST_EFFECT_GET_CLASS (self);

    if (klass->apply != NULL)
        klass->apply (self, source_texture_id, target_texture_id,
                      width, height, delta_time);
}

/**
 * lrg_post_effect_resize:
 * @self: A #LrgPostEffect
 * @width: New width
 * @height: New height
 *
 * Notifies the effect of a render target size change.
 */
void
lrg_post_effect_resize (LrgPostEffect *self,
                        guint          width,
                        guint          height)
{
    LrgPostEffectClass *klass;

    g_return_if_fail (LRG_IS_POST_EFFECT (self));

    klass = LRG_POST_EFFECT_GET_CLASS (self);

    if (klass->resize != NULL)
        klass->resize (self, width, height);
}

/**
 * lrg_post_effect_get_name:
 * @self: A #LrgPostEffect
 *
 * Gets the display name of the effect.
 *
 * Returns: (transfer none): The effect name
 */
const gchar *
lrg_post_effect_get_name (LrgPostEffect *self)
{
    LrgPostEffectClass *klass;

    g_return_val_if_fail (LRG_IS_POST_EFFECT (self), NULL);

    klass = LRG_POST_EFFECT_GET_CLASS (self);

    if (klass->get_name != NULL)
        return klass->get_name (self);

    return "Unknown Effect";
}

/**
 * lrg_post_effect_is_enabled:
 * @self: A #LrgPostEffect
 *
 * Checks if the effect is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_post_effect_is_enabled (LrgPostEffect *self)
{
    LrgPostEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_EFFECT (self), FALSE);

    priv = lrg_post_effect_get_instance_private (self);

    return priv->enabled;
}

/**
 * lrg_post_effect_set_enabled:
 * @self: A #LrgPostEffect
 * @enabled: Whether to enable the effect
 *
 * Enables or disables the effect.
 */
void
lrg_post_effect_set_enabled (LrgPostEffect *self,
                             gboolean       enabled)
{
    LrgPostEffectPrivate *priv;

    g_return_if_fail (LRG_IS_POST_EFFECT (self));

    priv = lrg_post_effect_get_instance_private (self);

    if (priv->enabled == enabled)
        return;

    priv->enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

/**
 * lrg_post_effect_get_intensity:
 * @self: A #LrgPostEffect
 *
 * Gets the effect intensity.
 *
 * Returns: The intensity value
 */
gfloat
lrg_post_effect_get_intensity (LrgPostEffect *self)
{
    LrgPostEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_EFFECT (self), 0.0f);

    priv = lrg_post_effect_get_instance_private (self);

    return priv->intensity;
}

/**
 * lrg_post_effect_set_intensity:
 * @self: A #LrgPostEffect
 * @intensity: Intensity value (0.0 - 1.0)
 *
 * Sets the effect intensity.
 */
void
lrg_post_effect_set_intensity (LrgPostEffect *self,
                               gfloat         intensity)
{
    LrgPostEffectPrivate *priv;

    g_return_if_fail (LRG_IS_POST_EFFECT (self));

    priv = lrg_post_effect_get_instance_private (self);

    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (priv->intensity == intensity)
        return;

    priv->intensity = intensity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

/**
 * lrg_post_effect_is_initialized:
 * @self: A #LrgPostEffect
 *
 * Checks if the effect has been initialized.
 *
 * Returns: %TRUE if initialized
 */
gboolean
lrg_post_effect_is_initialized (LrgPostEffect *self)
{
    LrgPostEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_EFFECT (self), FALSE);

    priv = lrg_post_effect_get_instance_private (self);

    return priv->initialized;
}

/**
 * lrg_post_effect_get_priority:
 * @self: A #LrgPostEffect
 *
 * Gets the effect's priority.
 *
 * Returns: The priority value
 */
gint
lrg_post_effect_get_priority (LrgPostEffect *self)
{
    LrgPostEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_POST_EFFECT (self), 0);

    priv = lrg_post_effect_get_instance_private (self);

    return priv->priority;
}

/**
 * lrg_post_effect_set_priority:
 * @self: A #LrgPostEffect
 * @priority: Priority value
 *
 * Sets the effect's priority.
 */
void
lrg_post_effect_set_priority (LrgPostEffect *self,
                              gint           priority)
{
    LrgPostEffectPrivate *priv;

    g_return_if_fail (LRG_IS_POST_EFFECT (self));

    priv = lrg_post_effect_get_instance_private (self);

    if (priv->priority == priority)
        return;

    priv->priority = priority;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIORITY]);
}
