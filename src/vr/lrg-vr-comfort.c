/* lrg-vr-comfort.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * VR comfort settings implementation.
 */

#include "lrg-vr-comfort.h"

#include <math.h>

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
    gfloat smooth_turn_speed;
    gboolean seated;
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
    PROP_SMOOTH_TURN_SPEED,
    PROP_SEATED,
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
    case PROP_SMOOTH_TURN_SPEED:
        g_value_set_float (value, self->smooth_turn_speed);
        break;
    case PROP_SEATED:
        g_value_set_boolean (value, self->seated);
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
    case PROP_SMOOTH_TURN_SPEED:
        self->smooth_turn_speed = g_value_get_float (value);
        break;
    case PROP_SEATED:
        self->seated = g_value_get_boolean (value);
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

    properties[PROP_SMOOTH_TURN_SPEED] =
        g_param_spec_float ("smooth-turn-speed",
                            "Smooth Turn Speed",
                            "Smooth turn rate at full stick, degrees per second",
                            10.0f, 360.0f, 120.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SEATED] =
        g_param_spec_boolean ("seated",
                              "Seated",
                              "Seated play: the eye height comes from a calibration",
                              FALSE,
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
    self->smooth_turn_speed = 120.0f;
    self->seated = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

gfloat
lrg_vr_comfort_settings_get_smooth_turn_speed (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), 120.0f);

    return self->smooth_turn_speed;
}

void
lrg_vr_comfort_settings_set_smooth_turn_speed (LrgVRComfortSettings *self,
                                               gfloat                speed)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    speed = CLAMP (speed, 10.0f, 360.0f);
    if (self->smooth_turn_speed != speed)
    {
        self->smooth_turn_speed = speed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTH_TURN_SPEED]);
    }
}

gboolean
lrg_vr_comfort_settings_get_seated (LrgVRComfortSettings *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self), FALSE);

    return self->seated;
}

void
lrg_vr_comfort_settings_set_seated (LrgVRComfortSettings *self,
                                    gboolean              seated)
{
    g_return_if_fail (LRG_IS_VR_COMFORT_SETTINGS (self));

    seated = !!seated;
    if (self->seated != seated)
    {
        self->seated = seated;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEATED]);
    }
}

/* ==========================================================================
 * LrgVRComfort
 * ========================================================================== */

/**
 * SECTION:lrg-vr-comfort-state
 * @title: LrgVRComfort
 * @short_description: VR comfort state machine
 *
 * #LrgVRComfort applies an #LrgVRComfortSettings frame by frame: the turn
 * the stick asks for (snap with hysteresis, or smooth), the vignette's
 * current opening and the seated calibration.
 *
 * Since: 1.0
 */

struct _LrgVRComfort
{
    GObject parent_instance;

    LrgVRComfortSettings *settings;   /* owned */
    gboolean snap_armed;
    gfloat   vignette;
    gfloat   blink;                   /* seconds of closed vignette left */
    gboolean calibrated;
};

G_DEFINE_TYPE (LrgVRComfort, lrg_vr_comfort, G_TYPE_OBJECT)

static void
lrg_vr_comfort_finalize (GObject *object)
{
    LrgVRComfort *self = LRG_VR_COMFORT (object);

    g_clear_object (&self->settings);
    G_OBJECT_CLASS (lrg_vr_comfort_parent_class)->finalize (object);
}

static void
lrg_vr_comfort_class_init (LrgVRComfortClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_vr_comfort_finalize;
}

static void
lrg_vr_comfort_init (LrgVRComfort *self)
{
    self->snap_armed = TRUE;
}

LrgVRComfort *
lrg_vr_comfort_new (LrgVRComfortSettings *settings)
{
    LrgVRComfort *self;

    g_return_val_if_fail (settings == NULL || LRG_IS_VR_COMFORT_SETTINGS (settings), NULL);

    self = g_object_new (LRG_TYPE_VR_COMFORT, NULL);
    self->settings = settings != NULL ? g_object_ref (settings) : lrg_vr_comfort_settings_new ();
    return self;
}

LrgVRComfortSettings *
lrg_vr_comfort_get_settings (LrgVRComfort *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT (self), NULL);

    return self->settings;
}

gfloat
lrg_vr_comfort_turn (LrgVRComfort *self,
                     gfloat        stick_x,
                     gfloat        delta)
{
    gfloat magnitude;

    g_return_val_if_fail (LRG_IS_VR_COMFORT (self), 0.0f);

    stick_x = CLAMP (stick_x, -1.0f, 1.0f);
    magnitude = fabsf (stick_x);

    if (self->settings->turn_mode == LRG_VR_TURN_MODE_SNAP)
    {
        /* fire once past the threshold, re-arm only near the centre, so a
           held stick never turns twice */
        if (magnitude < LRG_VR_COMFORT_SNAP_REARM)
            self->snap_armed = TRUE;
        if (self->snap_armed && magnitude >= LRG_VR_COMFORT_SNAP_THRESHOLD)
        {
            self->snap_armed = FALSE;
            self->blink = LRG_VR_COMFORT_SNAP_BLINK;
            return stick_x > 0.0f ? self->settings->snap_turn_angle
                                  : -self->settings->snap_turn_angle;
        }
        return 0.0f;
    }

    /* smooth: proportional past the dead zone, rescaled to 0 .. 1 */
    self->snap_armed = TRUE;
    if (magnitude <= LRG_VR_COMFORT_STICK_DEADZONE || delta <= 0.0f)
        return 0.0f;
    magnitude = (magnitude - LRG_VR_COMFORT_STICK_DEADZONE) / (1.0f - LRG_VR_COMFORT_STICK_DEADZONE);
    return (stick_x > 0.0f ? 1.0f : -1.0f) * magnitude * self->settings->smooth_turn_speed * delta;
}

gboolean
lrg_vr_comfort_get_snap_armed (LrgVRComfort *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT (self), FALSE);

    return self->snap_armed;
}

gfloat
lrg_vr_comfort_update_vignette (LrgVRComfort *self,
                                gfloat        linear_speed,
                                gfloat        turn_rate,
                                gfloat        delta)
{
    gfloat target;
    gfloat motion;

    g_return_val_if_fail (LRG_IS_VR_COMFORT (self), 0.0f);

    if (delta < 0.0f)
        delta = 0.0f;

    /* the target: off, a snap blink, or proportional to the motion */
    target = 0.0f;
    if (self->settings->vignette_enabled)
    {
        motion = MAX (fabsf (linear_speed) / LRG_VR_COMFORT_VIGNETTE_SPEED,
                      fabsf (turn_rate) / LRG_VR_COMFORT_VIGNETTE_TURN);
        target = self->settings->vignette_intensity * CLAMP (motion, 0.0f, 1.0f);
        if (self->blink > 0.0f)
            target = self->settings->vignette_intensity;
    }

    /* a snap blink closes at once; motion closes and opens at the rates */
    if (self->blink > 0.0f && target > self->vignette)
        self->vignette = target;
    else if (target > self->vignette)
        self->vignette = MIN (target, self->vignette + LRG_VR_COMFORT_VIGNETTE_RISE * delta);
    else
        self->vignette = MAX (target, self->vignette - LRG_VR_COMFORT_VIGNETTE_FALL * delta);
    if (self->blink > 0.0f)
        self->blink = MAX (self->blink - delta, 0.0f);
    return self->vignette;
}

gfloat
lrg_vr_comfort_get_vignette (LrgVRComfort *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT (self), 0.0f);

    return self->vignette;
}

void
lrg_vr_comfort_calibrate_height (LrgVRComfort *self,
                                 gfloat        tracked_eye_height,
                                 gfloat        target_eye_height)
{
    g_return_if_fail (LRG_IS_VR_COMFORT (self));

    lrg_vr_comfort_settings_set_height_adjustment (self->settings,
        CLAMP (target_eye_height - tracked_eye_height, -2.0f, 2.0f));
    self->calibrated = TRUE;
}

gboolean
lrg_vr_comfort_needs_calibration (LrgVRComfort *self)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT (self), FALSE);

    return self->settings->seated && !self->calibrated;
}

gfloat
lrg_vr_comfort_get_eye_height (LrgVRComfort *self,
                               gfloat        tracked_eye_height)
{
    g_return_val_if_fail (LRG_IS_VR_COMFORT (self), tracked_eye_height);

    return MAX (tracked_eye_height + self->settings->height_adjustment, 0.1f);
}

void
lrg_vr_comfort_reset (LrgVRComfort *self)
{
    g_return_if_fail (LRG_IS_VR_COMFORT (self));

    self->vignette = 0.0f;
    self->blink = 0.0f;
    self->snap_armed = TRUE;
    self->calibrated = FALSE;
}
