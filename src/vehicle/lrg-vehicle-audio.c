/* lrg-vehicle-audio.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-vehicle-audio.h"

/* Default values */
#define DEFAULT_MIN_PITCH       0.8f
#define DEFAULT_MAX_PITCH       2.0f
#define DEFAULT_IDLE_RPM        800.0f
#define DEFAULT_MAX_RPM         7000.0f
#define DEFAULT_VOLUME          1.0f
#define SLIP_SCREECH_THRESHOLD  0.2f

struct _LrgVehicleAudio
{
    GObject parent_instance;

    /* Target vehicle */
    LrgVehicle *vehicle;

    /* Sound asset IDs */
    gchar *engine_sound_id;
    gchar *tire_screech_sound_id;
    gchar *horn_sound_id;
    gchar *collision_sound_id;

    /* Engine pitch tuning */
    gfloat min_pitch;
    gfloat max_pitch;
    gfloat idle_rpm;
    gfloat max_rpm;

    /* Volume levels */
    gfloat master_volume;
    gfloat engine_volume;
    gfloat effects_volume;

    /* Current state */
    gboolean is_playing;
    gboolean horn_playing;
    gfloat current_engine_pitch;
    gfloat current_screech_volume;

    /* Smoothing */
    gfloat smoothed_rpm;
};

G_DEFINE_TYPE (LrgVehicleAudio, lrg_vehicle_audio, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_VEHICLE,
    PROP_MASTER_VOLUME,
    PROP_ENGINE_VOLUME,
    PROP_EFFECTS_VOLUME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_vehicle_audio_dispose (GObject *object)
{
    LrgVehicleAudio *self;

    self = LRG_VEHICLE_AUDIO (object);

    g_clear_object (&self->vehicle);

    G_OBJECT_CLASS (lrg_vehicle_audio_parent_class)->dispose (object);
}

static void
lrg_vehicle_audio_finalize (GObject *object)
{
    LrgVehicleAudio *self;

    self = LRG_VEHICLE_AUDIO (object);

    g_clear_pointer (&self->engine_sound_id, g_free);
    g_clear_pointer (&self->tire_screech_sound_id, g_free);
    g_clear_pointer (&self->horn_sound_id, g_free);
    g_clear_pointer (&self->collision_sound_id, g_free);

    G_OBJECT_CLASS (lrg_vehicle_audio_parent_class)->finalize (object);
}

static void
lrg_vehicle_audio_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgVehicleAudio *self;

    self = LRG_VEHICLE_AUDIO (object);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        g_value_set_object (value, self->vehicle);
        break;

    case PROP_MASTER_VOLUME:
        g_value_set_float (value, self->master_volume);
        break;

    case PROP_ENGINE_VOLUME:
        g_value_set_float (value, self->engine_volume);
        break;

    case PROP_EFFECTS_VOLUME:
        g_value_set_float (value, self->effects_volume);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_audio_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgVehicleAudio *self;

    self = LRG_VEHICLE_AUDIO (object);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        lrg_vehicle_audio_set_vehicle (self, g_value_get_object (value));
        break;

    case PROP_MASTER_VOLUME:
        lrg_vehicle_audio_set_master_volume (self, g_value_get_float (value));
        break;

    case PROP_ENGINE_VOLUME:
        lrg_vehicle_audio_set_engine_volume (self, g_value_get_float (value));
        break;

    case PROP_EFFECTS_VOLUME:
        lrg_vehicle_audio_set_effects_volume (self, g_value_get_float (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_audio_class_init (LrgVehicleAudioClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_vehicle_audio_dispose;
    object_class->finalize = lrg_vehicle_audio_finalize;
    object_class->get_property = lrg_vehicle_audio_get_property;
    object_class->set_property = lrg_vehicle_audio_set_property;

    properties[PROP_VEHICLE] =
        g_param_spec_object ("vehicle",
                             "Vehicle",
                             "Vehicle to sync audio with",
                             LRG_TYPE_VEHICLE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_MASTER_VOLUME] =
        g_param_spec_float ("master-volume",
                            "Master Volume",
                            "Master volume for all sounds",
                            0.0f, 1.0f, DEFAULT_VOLUME,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ENGINE_VOLUME] =
        g_param_spec_float ("engine-volume",
                            "Engine Volume",
                            "Engine sound volume",
                            0.0f, 1.0f, DEFAULT_VOLUME,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_EFFECTS_VOLUME] =
        g_param_spec_float ("effects-volume",
                            "Effects Volume",
                            "Sound effects volume",
                            0.0f, 1.0f, DEFAULT_VOLUME,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_vehicle_audio_init (LrgVehicleAudio *self)
{
    self->vehicle = NULL;

    self->engine_sound_id = NULL;
    self->tire_screech_sound_id = NULL;
    self->horn_sound_id = NULL;
    self->collision_sound_id = NULL;

    self->min_pitch = DEFAULT_MIN_PITCH;
    self->max_pitch = DEFAULT_MAX_PITCH;
    self->idle_rpm = DEFAULT_IDLE_RPM;
    self->max_rpm = DEFAULT_MAX_RPM;

    self->master_volume = DEFAULT_VOLUME;
    self->engine_volume = DEFAULT_VOLUME;
    self->effects_volume = DEFAULT_VOLUME;

    self->is_playing = FALSE;
    self->horn_playing = FALSE;
    self->current_engine_pitch = DEFAULT_MIN_PITCH;
    self->current_screech_volume = 0.0f;

    self->smoothed_rpm = DEFAULT_IDLE_RPM;
}

/*
 * Public API
 */

LrgVehicleAudio *
lrg_vehicle_audio_new (void)
{
    return g_object_new (LRG_TYPE_VEHICLE_AUDIO, NULL);
}

void
lrg_vehicle_audio_set_vehicle (LrgVehicleAudio *self,
                               LrgVehicle      *vehicle)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));
    g_return_if_fail (vehicle == NULL || LRG_IS_VEHICLE (vehicle));

    if (self->vehicle == vehicle)
        return;

    /* Stop audio when changing vehicles */
    if (self->is_playing)
        lrg_vehicle_audio_stop (self);

    g_clear_object (&self->vehicle);
    if (vehicle != NULL)
        self->vehicle = g_object_ref (vehicle);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VEHICLE]);
}

LrgVehicle *
lrg_vehicle_audio_get_vehicle (LrgVehicleAudio *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_AUDIO (self), NULL);

    return self->vehicle;
}

void
lrg_vehicle_audio_set_engine_sound (LrgVehicleAudio *self,
                                    const gchar     *sound_id)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    g_free (self->engine_sound_id);
    self->engine_sound_id = g_strdup (sound_id);
}

void
lrg_vehicle_audio_set_tire_screech_sound (LrgVehicleAudio *self,
                                          const gchar     *sound_id)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    g_free (self->tire_screech_sound_id);
    self->tire_screech_sound_id = g_strdup (sound_id);
}

void
lrg_vehicle_audio_set_horn_sound (LrgVehicleAudio *self,
                                  const gchar     *sound_id)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    g_free (self->horn_sound_id);
    self->horn_sound_id = g_strdup (sound_id);
}

void
lrg_vehicle_audio_set_collision_sound (LrgVehicleAudio *self,
                                       const gchar     *sound_id)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    g_free (self->collision_sound_id);
    self->collision_sound_id = g_strdup (sound_id);
}

void
lrg_vehicle_audio_set_engine_pitch_range (LrgVehicleAudio *self,
                                          gfloat           min_pitch,
                                          gfloat           max_pitch)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));
    g_return_if_fail (min_pitch > 0.0f);
    g_return_if_fail (max_pitch >= min_pitch);

    self->min_pitch = min_pitch;
    self->max_pitch = max_pitch;
}

void
lrg_vehicle_audio_set_engine_rpm_range (LrgVehicleAudio *self,
                                        gfloat           idle_rpm,
                                        gfloat           max_rpm)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));
    g_return_if_fail (idle_rpm > 0.0f);
    g_return_if_fail (max_rpm > idle_rpm);

    self->idle_rpm = idle_rpm;
    self->max_rpm = max_rpm;
}

void
lrg_vehicle_audio_set_master_volume (LrgVehicleAudio *self,
                                     gfloat           volume)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    self->master_volume = CLAMP (volume, 0.0f, 1.0f);
}

gfloat
lrg_vehicle_audio_get_master_volume (LrgVehicleAudio *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_AUDIO (self), DEFAULT_VOLUME);

    return self->master_volume;
}

void
lrg_vehicle_audio_set_engine_volume (LrgVehicleAudio *self,
                                     gfloat           volume)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    self->engine_volume = CLAMP (volume, 0.0f, 1.0f);
}

void
lrg_vehicle_audio_set_effects_volume (LrgVehicleAudio *self,
                                      gfloat           volume)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    self->effects_volume = CLAMP (volume, 0.0f, 1.0f);
}

void
lrg_vehicle_audio_start (LrgVehicleAudio *self)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    if (self->is_playing)
        return;

    self->is_playing = TRUE;
    self->smoothed_rpm = self->idle_rpm;

    /*
     * TODO: Integrate with LrgAudioManager to start engine loop
     *
     * if (self->engine_sound_id != NULL)
     * {
     *     LrgAudioManager *audio = lrg_engine_get_audio_manager (...);
     *     lrg_audio_manager_play_looped (audio, self->engine_sound_id, ...);
     * }
     */
}

void
lrg_vehicle_audio_stop (LrgVehicleAudio *self)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    if (!self->is_playing)
        return;

    self->is_playing = FALSE;
    self->horn_playing = FALSE;

    /*
     * TODO: Integrate with LrgAudioManager to stop sounds
     */
}

gboolean
lrg_vehicle_audio_is_playing (LrgVehicleAudio *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_AUDIO (self), FALSE);

    return self->is_playing;
}

void
lrg_vehicle_audio_play_horn (LrgVehicleAudio *self)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    if (self->horn_playing)
        return;

    self->horn_playing = TRUE;

    /*
     * TODO: Integrate with LrgAudioManager
     *
     * if (self->horn_sound_id != NULL)
     * {
     *     gfloat volume = self->master_volume * self->effects_volume;
     *     LrgAudioManager *audio = lrg_engine_get_audio_manager (...);
     *     lrg_audio_manager_play (audio, self->horn_sound_id, volume);
     * }
     */
}

void
lrg_vehicle_audio_stop_horn (LrgVehicleAudio *self)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    self->horn_playing = FALSE;

    /*
     * TODO: Stop horn sound
     */
}

void
lrg_vehicle_audio_play_collision (LrgVehicleAudio *self,
                                  gfloat           intensity)
{
    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));

    /*
     * TODO: Integrate with LrgAudioManager
     *
     * if (self->collision_sound_id != NULL)
     * {
     *     gfloat volume = self->master_volume * self->effects_volume * intensity;
     *     LrgAudioManager *audio = lrg_engine_get_audio_manager (...);
     *     lrg_audio_manager_play (audio, self->collision_sound_id, volume);
     * }
     */
    (void)intensity;
}

void
lrg_vehicle_audio_update (LrgVehicleAudio *self,
                          gfloat           delta)
{
    gfloat target_rpm;
    gfloat rpm_t;
    gfloat target_pitch;
    gfloat slip_amount;
    gfloat target_screech;
    guint wheel_count;
    guint i;

    g_return_if_fail (LRG_IS_VEHICLE_AUDIO (self));
    g_return_if_fail (delta > 0.0f);

    if (!self->is_playing || self->vehicle == NULL)
        return;

    /*
     * Update engine sound
     */

    target_rpm = lrg_vehicle_get_rpm (self->vehicle);

    /* Smooth RPM changes */
    self->smoothed_rpm += (target_rpm - self->smoothed_rpm) * 5.0f * delta;

    /* Calculate pitch from RPM */
    rpm_t = (self->smoothed_rpm - self->idle_rpm) / (self->max_rpm - self->idle_rpm);
    rpm_t = CLAMP (rpm_t, 0.0f, 1.0f);
    target_pitch = self->min_pitch + rpm_t * (self->max_pitch - self->min_pitch);

    /* Smooth pitch changes */
    self->current_engine_pitch += (target_pitch - self->current_engine_pitch) * 10.0f * delta;

    /*
     * TODO: Update engine sound pitch
     *
     * if (self->engine_sound_id != NULL)
     * {
     *     gfloat volume = self->master_volume * self->engine_volume;
     *     LrgAudioManager *audio = lrg_engine_get_audio_manager (...);
     *     lrg_audio_manager_set_pitch (audio, ..., self->current_engine_pitch);
     *     lrg_audio_manager_set_volume (audio, ..., volume);
     * }
     */

    /*
     * Update tire screech based on wheel slip
     */

    slip_amount = 0.0f;
    wheel_count = lrg_vehicle_get_wheel_count (self->vehicle);

    for (i = 0; i < wheel_count; i++)
    {
        LrgWheel *wheel;

        wheel = lrg_vehicle_get_wheel (self->vehicle, i);
        if (wheel != NULL && lrg_wheel_is_slipping (wheel))
        {
            gfloat wheel_slip = sqrtf (wheel->slip_ratio * wheel->slip_ratio +
                                       wheel->slip_angle * wheel->slip_angle);
            if (wheel_slip > slip_amount)
                slip_amount = wheel_slip;
        }
    }

    /* Calculate target screech volume */
    target_screech = 0.0f;
    if (slip_amount > SLIP_SCREECH_THRESHOLD)
    {
        target_screech = (slip_amount - SLIP_SCREECH_THRESHOLD) /
                         (1.0f - SLIP_SCREECH_THRESHOLD);
        target_screech = CLAMP (target_screech, 0.0f, 1.0f);
    }

    /* Smooth screech volume */
    self->current_screech_volume += (target_screech - self->current_screech_volume) * 8.0f * delta;

    /*
     * TODO: Update tire screech sound
     *
     * if (self->tire_screech_sound_id != NULL)
     * {
     *     gfloat volume = self->master_volume * self->effects_volume *
     *                     self->current_screech_volume;
     *     if (volume > 0.01f)
     *     {
     *         // Start or update screech sound
     *     }
     *     else
     *     {
     *         // Stop screech sound
     *     }
     * }
     */
}
