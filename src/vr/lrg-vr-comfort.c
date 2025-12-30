/* lrg-vr-comfort.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * VR comfort settings implementation.
 */

#include "lrg-vr-comfort.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_VR
#include "../lrg-log.h"

/**
 * SECTION:lrg-vr-comfort
 * @title: LrgVRComfortSettings
 * @short_description: VR comfort settings
 *
 * The #LrgVRComfortSettings provides comfort options for VR
 * to help reduce motion sickness:
 *
 * - **Snap turning**: Discrete turn angles instead of smooth rotation
 * - **Teleport locomotion**: Point-and-teleport instead of smooth movement
 * - **Vignette**: Darkens peripheral vision during movement
 * - **Height adjustment**: Adjust player height for seated play
 *
 * Since: 1.0
 */

struct _LrgVRComfortSettings
{
    GObject parent_instance;

    LrgVRTurnMode turn_mode;
    gfloat snap_turn_angle;
    LrgVRLocomotionMode locomotion_mode;
    gboolean vignette_enabled;
    gfloat vignette_intensity;
    gfloat height_adjustment;
};

G_DEFINE_TYPE (LrgVRComfortSettings, lrg_vr_comfort_settings, G_TYPE_OBJECT)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_TURN_MODE,
    PROP_SNAP_TURN_ANGLE,
    PROP_LOCOMOTION_MODE,
    PROP_VIGNETTE_ENABLED,
    PROP_VIGNETTE_INTENSITY,
    PROP_HEIGHT_ADJUSTMENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_vr_comfort_settings_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    LrgVRComfortSettings *self = LRG_VR_COMFORT_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_TURN_MODE:
        g_value_set_enum (value, self->turn_mode);
        break;
    case PROP_SNAP_TURN_ANGLE:
        g_value_set_float (value, self->snap_turn_angle);
        break;
    case PROP_LOCOMOTION_MODE:
        g_value_set_enum (value, self->locomotion_mode);
        break;
    case PROP_VIGNETTE_ENABLED:
        g_value_set_boolean (value, self->vignette_enabled);
        break;
    case PROP_VIGNETTE_INTENSITY:
        g_value_set_float (value, self->vignette_intensity);
        break;
    case PROP_HEIGHT_ADJUSTMENT:
        g_value_set_float (value, self->height_adjustment);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vr_comfort_settings_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    LrgVRComfortSettings *self = LRG_VR_COMFORT_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_TURN_MODE:
        self->turn_mode = g_value_get_enum (value);
        break;
    case PROP_SNAP_TURN_ANGLE:
        self->snap_turn_angle = g_value_get_float (value);
        break;
    case PROP_LOCOMOTION_MODE:
        self->locomotion_mode = g_value_get_enum (value);
        break;
    case PROP_VIGNETTE_ENABLED:
        self->vignette_enabled = g_value_get_boolean (value);
        break;
    case PROP_VIGNETTE_INTENSITY:
        self->vignette_intensity = CLAMP (g_value_get_float (value), 0.0f, 1.0f);
        break;
    case PROP_HEIGHT_ADJUSTMENT:
        self->height_adjustment = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vr_comfort_settings_class_init (LrgVRComfortSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_vr_comfort_settings_get_property;
    object_class->set_property = lrg_vr_comfort_settings_set_property;

    properties[PROP_TURN_MODE] =
        g_param_spec_enum ("turn-mode",
                           "Turn Mode",
                           "Turning mode (smooth or snap)",
                           LRG_TYPE_VR_TURN_MODE,
                           LRG_VR_TURN_MODE_SMOOTH,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SNAP_TURN_ANGLE] =
        g_param_spec_float ("snap-turn-angle",
                            "Snap Turn Angle",
                            "Angle for snap turning in degrees",
                            15.0f, 90.0f, 45.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOCOMOTION_MODE] =
        g_param_spec_enum ("locomotion-mode",
                           "Locomotion Mode",
                           "Locomotion mode (smooth or teleport)",
                           LRG_TYPE_VR_LOCOMOTION_MODE,
                           LRG_VR_LOCOMOTION_SMOOTH,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VIGNETTE_ENABLED] =
        g_param_spec_boolean ("vignette-enabled",
                              "Vignette Enabled",
                              "Whether comfort vignette is enabled",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VIGNETTE_INTENSITY] =
        g_param_spec_float ("vignette-intensity",
                            "Vignette Intensity",
                            "Intensity of comfort vignette",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEIGHT_ADJUSTMENT] =
        g_param_spec_float ("height-adjustment",
                            "Height Adjustment",
                            "Height adjustment in meters",
                            -2.0f, 2.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_vr_comfort_settings_init (LrgVRComfortSettings *self)
{
    self->turn_mode = LRG_VR_TURN_MODE_SMOOTH;
    self->snap_turn_angle = 45.0f;
    self->locomotion_mode = LRG_VR_LOCOMOTION_SMOOTH;
    self->vignette_enabled = FALSE;
    self->vignette_intensity = 0.5f;
    self->height_adjustment = 0.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_vr_comfort_settings_new:
 *
 * Creates new VR comfort settings with defaults.
 *
 * Returns: (transfer full): a new #LrgVRComfortSettings
 *
 * Since: 1.0
 */
LrgVRComfortSettings *
lrg_vr_comfort_settings_new (void)
{
    return g_object_new (LRG_TYPE_VR_COMFORT_SETTINGS, NULL);
}

/**
 * lrg_vr_comfort_settings_get_turn_mode:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the turning mode.
 *
 * Returns: the #LrgVRTurnMode
 *
 * Since: 1.0
 */
LrgVRTurnMode
lrg_vr_comfort_settings_get_turn_mode (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), LRG_VR_TURN_MODE_SMOOTH);

    return self->turn_mode;
}

/**
 * lrg_vr_comfort_settings_set_turn_mode:
 * @self: a #LrgVRComfortSettings
 * @mode: the #LrgVRTurnMode
 *
 * Sets the turning mode.
 *
 * Since: 1.0
 */
void
lrg_vr_comfort_settings_set_turn_mode (LrgVRComfortSettings *self,
                                       LrgVRTurnMode         mode)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    if (self->turn_mode != mode)
    {
        self->turn_mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TURN_MODE]);
    }
}

/**
 * lrg_vr_comfort_settings_get_snap_turn_angle:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the snap turn angle in degrees.
 *
 * Returns: snap turn angle
 *
 * Since: 1.0
 */
gfloat
lrg_vr_comfort_settings_get_snap_turn_angle (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), 45.0f);

    return self->snap_turn_angle;
}

/**
 * lrg_vr_comfort_settings_set_snap_turn_angle:
 * @self: a #LrgVRComfortSettings
 * @angle: snap turn angle in degrees
 *
 * Sets the snap turn angle.
 *
 * Since: 1.0
 */
void
lrg_vr_comfort_settings_set_snap_turn_angle (LrgVRComfortSettings *self,
                                             gfloat                angle)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    angle = CLAMP (angle, 15.0f, 90.0f);

    if (self->snap_turn_angle != angle)
    {
        self->snap_turn_angle = angle;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAP_TURN_ANGLE]);
    }
}

/**
 * lrg_vr_comfort_settings_get_locomotion_mode:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the locomotion mode.
 *
 * Returns: the #LrgVRLocomotionMode
 *
 * Since: 1.0
 */
LrgVRLocomotionMode
lrg_vr_comfort_settings_get_locomotion_mode (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), LRG_VR_LOCOMOTION_SMOOTH);

    return self->locomotion_mode;
}

/**
 * lrg_vr_comfort_settings_set_locomotion_mode:
 * @self: a #LrgVRComfortSettings
 * @mode: the #LrgVRLocomotionMode
 *
 * Sets the locomotion mode.
 *
 * Since: 1.0
 */
void
lrg_vr_comfort_settings_set_locomotion_mode (LrgVRComfortSettings *self,
                                             LrgVRLocomotionMode   mode)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    if (self->locomotion_mode != mode)
    {
        self->locomotion_mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCOMOTION_MODE]);
    }
}

/**
 * lrg_vr_comfort_settings_get_vignette_enabled:
 * @self: a #LrgVRComfortSettings
 *
 * Gets whether comfort vignette is enabled.
 *
 * Returns: %TRUE if vignette is enabled
 *
 * Since: 1.0
 */
gboolean
lrg_vr_comfort_settings_get_vignette_enabled (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), FALSE);

    return self->vignette_enabled;
}

/**
 * lrg_vr_comfort_settings_set_vignette_enabled:
 * @self: a #LrgVRComfortSettings
 * @enabled: whether to enable vignette
 *
 * Sets whether comfort vignette is enabled.
 *
 * Since: 1.0
 */
void
lrg_vr_comfort_settings_set_vignette_enabled (LrgVRComfortSettings *self,
                                              gboolean              enabled)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    if (self->vignette_enabled != enabled)
    {
        self->vignette_enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIGNETTE_ENABLED]);
    }
}

/**
 * lrg_vr_comfort_settings_get_vignette_intensity:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the vignette intensity.
 *
 * Returns: vignette intensity (0.0 to 1.0)
 *
 * Since: 1.0
 */
gfloat
lrg_vr_comfort_settings_get_vignette_intensity (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), 0.5f);

    return self->vignette_intensity;
}

/**
 * lrg_vr_comfort_settings_set_vignette_intensity:
 * @self: a #LrgVRComfortSettings
 * @intensity: vignette intensity (0.0 to 1.0)
 *
 * Sets the vignette intensity.
 *
 * Since: 1.0
 */
void
lrg_vr_comfort_settings_set_vignette_intensity (LrgVRComfortSettings *self,
                                                gfloat                intensity)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (self->vignette_intensity != intensity)
    {
        self->vignette_intensity = intensity;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIGNETTE_INTENSITY]);
    }
}

/**
 * lrg_vr_comfort_settings_get_height_adjustment:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the height adjustment offset.
 *
 * Returns: height adjustment in meters
 *
 * Since: 1.0
 */
gfloat
lrg_vr_comfort_settings_get_height_adjustment (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), 0.0f);

    return self->height_adjustment;
}

/**
 * lrg_vr_comfort_settings_set_height_adjustment:
 * @self: a #LrgVRComfortSettings
 * @adjustment: height adjustment in meters
 *
 * Sets the height adjustment offset.
 *
 * Since: 1.0
 */
void
lrg_vr_comfort_settings_set_height_adjustment (LrgVRComfortSettings *self,
                                               gfloat                adjustment)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    adjustment = CLAMP (adjustment, -2.0f, 2.0f);

    if (self->height_adjustment != adjustment)
    {
        self->height_adjustment = adjustment;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT_ADJUSTMENT]);
    }
}
