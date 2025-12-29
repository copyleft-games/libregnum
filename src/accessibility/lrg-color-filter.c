/* lrg-color-filter.c - Abstract base class for color filters
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-color-filter.h"

/**
 * SECTION:lrg-color-filter
 * @title: LrgColorFilter
 * @short_description: Abstract base class for color filters
 *
 * #LrgColorFilter is an abstract base class for color transformation
 * filters used for accessibility features like colorblind modes.
 *
 * Subclasses provide a 4x4 color transformation matrix that is applied
 * to rendered colors. The matrix is in row-major order and transforms
 * RGBA color vectors.
 *
 * Built-in filters include:
 * - #LrgColorFilterDeuteranopia: Red-green (deutan) colorblind mode
 * - #LrgColorFilterProtanopia: Red-green (protan) colorblind mode
 * - #LrgColorFilterTritanopia: Blue-yellow colorblind mode
 * - #LrgColorFilterHighContrast: High contrast mode
 */

typedef struct
{
    gfloat   intensity;
    gboolean enabled;
} LrgColorFilterPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgColorFilter, lrg_color_filter, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_INTENSITY,
    PROP_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_color_filter_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgColorFilter *self = LRG_COLOR_FILTER (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        lrg_color_filter_set_intensity (self, g_value_get_float (value));
        break;
    case PROP_ENABLED:
        lrg_color_filter_set_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_color_filter_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgColorFilter *self = LRG_COLOR_FILTER (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        g_value_set_float (value, lrg_color_filter_get_intensity (self));
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, lrg_color_filter_is_enabled (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_color_filter_class_init (LrgColorFilterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_color_filter_set_property;
    object_class->get_property = lrg_color_filter_get_property;

    /**
     * LrgColorFilter:intensity:
     *
     * The filter intensity from 0.0 (off) to 1.0 (full effect).
     */
    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity",
                            "Intensity",
                            "Filter intensity (0.0 to 1.0)",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgColorFilter:enabled:
     *
     * Whether the filter is enabled.
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether the filter is enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_color_filter_init (LrgColorFilter *self)
{
    LrgColorFilterPrivate *priv = lrg_color_filter_get_instance_private (self);

    priv->intensity = 1.0f;
    priv->enabled = TRUE;
}

/**
 * lrg_color_filter_get_matrix:
 * @self: an #LrgColorFilter
 * @matrix: (out) (array fixed-size=16): output 4x4 matrix
 *
 * Gets the color transformation matrix.
 */
void
lrg_color_filter_get_matrix (LrgColorFilter *self,
                             gfloat          matrix[16])
{
    LrgColorFilterClass *klass;

    g_return_if_fail (LRG_IS_COLOR_FILTER (self));
    g_return_if_fail (matrix != NULL);

    klass = LRG_COLOR_FILTER_GET_CLASS (self);
    g_return_if_fail (klass->get_matrix != NULL);

    klass->get_matrix (self, matrix);
}

/**
 * lrg_color_filter_get_name:
 * @self: an #LrgColorFilter
 *
 * Gets the display name of this filter.
 *
 * Returns: (transfer none): The filter name
 */
const gchar *
lrg_color_filter_get_name (LrgColorFilter *self)
{
    LrgColorFilterClass *klass;

    g_return_val_if_fail (LRG_IS_COLOR_FILTER (self), NULL);

    klass = LRG_COLOR_FILTER_GET_CLASS (self);
    if (klass->get_name != NULL)
        return klass->get_name (self);

    return "Unknown Filter";
}

/**
 * lrg_color_filter_get_intensity:
 * @self: an #LrgColorFilter
 *
 * Gets the filter intensity.
 *
 * Returns: The intensity value
 */
gfloat
lrg_color_filter_get_intensity (LrgColorFilter *self)
{
    LrgColorFilterPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLOR_FILTER (self), 0.0f);

    priv = lrg_color_filter_get_instance_private (self);
    return priv->intensity;
}

/**
 * lrg_color_filter_set_intensity:
 * @self: an #LrgColorFilter
 * @intensity: the intensity (0.0 to 1.0)
 *
 * Sets the filter intensity.
 */
void
lrg_color_filter_set_intensity (LrgColorFilter *self,
                                gfloat          intensity)
{
    LrgColorFilterPrivate *priv;

    g_return_if_fail (LRG_IS_COLOR_FILTER (self));

    priv = lrg_color_filter_get_instance_private (self);

    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (priv->intensity != intensity)
    {
        priv->intensity = intensity;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
    }
}

/**
 * lrg_color_filter_is_enabled:
 * @self: an #LrgColorFilter
 *
 * Gets whether the filter is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
lrg_color_filter_is_enabled (LrgColorFilter *self)
{
    LrgColorFilterPrivate *priv;

    g_return_val_if_fail (LRG_IS_COLOR_FILTER (self), FALSE);

    priv = lrg_color_filter_get_instance_private (self);
    return priv->enabled;
}

/**
 * lrg_color_filter_set_enabled:
 * @self: an #LrgColorFilter
 * @enabled: whether to enable the filter
 *
 * Sets whether the filter is enabled.
 */
void
lrg_color_filter_set_enabled (LrgColorFilter *self,
                              gboolean        enabled)
{
    LrgColorFilterPrivate *priv;

    g_return_if_fail (LRG_IS_COLOR_FILTER (self));

    priv = lrg_color_filter_get_instance_private (self);

    enabled = !!enabled;

    if (priv->enabled != enabled)
    {
        priv->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}
