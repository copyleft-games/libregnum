/* lrg-audio-settings.c - Audio settings group implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-audio-settings.h"
#include <gio/gio.h>

/**
 * SECTION:lrg-audio-settings
 * @title: LrgAudioSettings
 * @short_description: Audio settings group
 *
 * #LrgAudioSettings manages all audio-related settings including
 * volume levels for different audio categories, mute state,
 * mono audio for accessibility, and subtitle preferences.
 */

/* Default values */
#define DEFAULT_MASTER_VOLUME   0.8
#define DEFAULT_MUSIC_VOLUME    0.6
#define DEFAULT_SFX_VOLUME      1.0
#define DEFAULT_VOICE_VOLUME    1.0
#define DEFAULT_MUTED           FALSE
#define DEFAULT_MONO_AUDIO      FALSE
#define DEFAULT_SUBTITLES       FALSE

struct _LrgAudioSettings
{
    LrgSettingsGroup parent_instance;

    /* Volume levels (0.0 - 1.0) */
    gdouble master_volume;
    gdouble music_volume;
    gdouble sfx_volume;
    gdouble voice_volume;

    /* Flags */
    gboolean muted;
    gboolean mono_audio;
    gboolean subtitles_enabled;

    /* Device */
    gchar *audio_device;
};

G_DEFINE_TYPE (LrgAudioSettings, lrg_audio_settings, LRG_TYPE_SETTINGS_GROUP)

enum
{
    PROP_0,
    PROP_MASTER_VOLUME,
    PROP_MUSIC_VOLUME,
    PROP_SFX_VOLUME,
    PROP_VOICE_VOLUME,
    PROP_MUTED,
    PROP_MONO_AUDIO,
    PROP_SUBTITLES_ENABLED,
    PROP_AUDIO_DEVICE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Helper to emit changed signal and mark dirty.
 */
static void
emit_changed (LrgAudioSettings *self,
              const gchar      *property_name)
{
    lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
    g_signal_emit_by_name (self, "changed", property_name);
}

/* Virtual method implementations */

static void
lrg_audio_settings_apply (LrgSettingsGroup *group)
{
    /*
     * TODO: Apply settings to the audio system.
     * This would integrate with graylib audio systems.
     */
    g_debug ("LrgAudioSettings: apply() called - would apply to audio system");
}

static void
lrg_audio_settings_reset (LrgSettingsGroup *group)
{
    LrgAudioSettings *self = LRG_AUDIO_SETTINGS (group);

    self->master_volume = DEFAULT_MASTER_VOLUME;
    self->music_volume = DEFAULT_MUSIC_VOLUME;
    self->sfx_volume = DEFAULT_SFX_VOLUME;
    self->voice_volume = DEFAULT_VOICE_VOLUME;
    self->muted = DEFAULT_MUTED;
    self->mono_audio = DEFAULT_MONO_AUDIO;
    self->subtitles_enabled = DEFAULT_SUBTITLES;
    g_clear_pointer (&self->audio_device, g_free);

    emit_changed (self, NULL);
}

static const gchar *
lrg_audio_settings_get_group_name (LrgSettingsGroup *group)
{
    return "audio";
}

static GVariant *
lrg_audio_settings_serialize (LrgSettingsGroup  *group,
                              GError           **error)
{
    LrgAudioSettings *self = LRG_AUDIO_SETTINGS (group);
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_add (&builder, "{sv}", "master_volume",
                           g_variant_new_double (self->master_volume));
    g_variant_builder_add (&builder, "{sv}", "music_volume",
                           g_variant_new_double (self->music_volume));
    g_variant_builder_add (&builder, "{sv}", "sfx_volume",
                           g_variant_new_double (self->sfx_volume));
    g_variant_builder_add (&builder, "{sv}", "voice_volume",
                           g_variant_new_double (self->voice_volume));
    g_variant_builder_add (&builder, "{sv}", "muted",
                           g_variant_new_boolean (self->muted));
    g_variant_builder_add (&builder, "{sv}", "mono_audio",
                           g_variant_new_boolean (self->mono_audio));
    g_variant_builder_add (&builder, "{sv}", "subtitles_enabled",
                           g_variant_new_boolean (self->subtitles_enabled));

    if (self->audio_device)
    {
        g_variant_builder_add (&builder, "{sv}", "audio_device",
                               g_variant_new_string (self->audio_device));
    }

    return g_variant_builder_end (&builder);
}

static gboolean
lrg_audio_settings_deserialize (LrgSettingsGroup  *group,
                                GVariant          *data,
                                GError           **error)
{
    LrgAudioSettings *self = LRG_AUDIO_SETTINGS (group);
    GVariant *value;

    if (!g_variant_is_of_type (data, G_VARIANT_TYPE ("a{sv}")))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Expected a{sv} variant for audio settings");
        return FALSE;
    }

    value = g_variant_lookup_value (data, "master_volume", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->master_volume = CLAMP (g_variant_get_double (value), 0.0, 1.0);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "music_volume", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->music_volume = CLAMP (g_variant_get_double (value), 0.0, 1.0);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "sfx_volume", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->sfx_volume = CLAMP (g_variant_get_double (value), 0.0, 1.0);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "voice_volume", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->voice_volume = CLAMP (g_variant_get_double (value), 0.0, 1.0);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "muted", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->muted = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "mono_audio", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->mono_audio = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "subtitles_enabled", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->subtitles_enabled = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "audio_device", G_VARIANT_TYPE_STRING);
    if (value)
    {
        g_clear_pointer (&self->audio_device, g_free);
        self->audio_device = g_variant_dup_string (value, NULL);
        g_variant_unref (value);
    }

    return TRUE;
}

/* GObject methods */

static void
lrg_audio_settings_finalize (GObject *object)
{
    LrgAudioSettings *self = LRG_AUDIO_SETTINGS (object);

    g_clear_pointer (&self->audio_device, g_free);

    G_OBJECT_CLASS (lrg_audio_settings_parent_class)->finalize (object);
}

static void
lrg_audio_settings_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgAudioSettings *self = LRG_AUDIO_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_MASTER_VOLUME:
        g_value_set_double (value, self->master_volume);
        break;
    case PROP_MUSIC_VOLUME:
        g_value_set_double (value, self->music_volume);
        break;
    case PROP_SFX_VOLUME:
        g_value_set_double (value, self->sfx_volume);
        break;
    case PROP_VOICE_VOLUME:
        g_value_set_double (value, self->voice_volume);
        break;
    case PROP_MUTED:
        g_value_set_boolean (value, self->muted);
        break;
    case PROP_MONO_AUDIO:
        g_value_set_boolean (value, self->mono_audio);
        break;
    case PROP_SUBTITLES_ENABLED:
        g_value_set_boolean (value, self->subtitles_enabled);
        break;
    case PROP_AUDIO_DEVICE:
        g_value_set_string (value, self->audio_device);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_audio_settings_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgAudioSettings *self = LRG_AUDIO_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_MASTER_VOLUME:
        lrg_audio_settings_set_master_volume (self, g_value_get_double (value));
        break;
    case PROP_MUSIC_VOLUME:
        lrg_audio_settings_set_music_volume (self, g_value_get_double (value));
        break;
    case PROP_SFX_VOLUME:
        lrg_audio_settings_set_sfx_volume (self, g_value_get_double (value));
        break;
    case PROP_VOICE_VOLUME:
        lrg_audio_settings_set_voice_volume (self, g_value_get_double (value));
        break;
    case PROP_MUTED:
        lrg_audio_settings_set_muted (self, g_value_get_boolean (value));
        break;
    case PROP_MONO_AUDIO:
        lrg_audio_settings_set_mono_audio (self, g_value_get_boolean (value));
        break;
    case PROP_SUBTITLES_ENABLED:
        lrg_audio_settings_set_subtitles_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_AUDIO_DEVICE:
        lrg_audio_settings_set_audio_device (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_audio_settings_class_init (LrgAudioSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgSettingsGroupClass *group_class = LRG_SETTINGS_GROUP_CLASS (klass);

    object_class->finalize = lrg_audio_settings_finalize;
    object_class->get_property = lrg_audio_settings_get_property;
    object_class->set_property = lrg_audio_settings_set_property;

    /* Override virtual methods */
    group_class->apply = lrg_audio_settings_apply;
    group_class->reset = lrg_audio_settings_reset;
    group_class->get_group_name = lrg_audio_settings_get_group_name;
    group_class->serialize = lrg_audio_settings_serialize;
    group_class->deserialize = lrg_audio_settings_deserialize;

    /* Install properties */
    properties[PROP_MASTER_VOLUME] =
        g_param_spec_double ("master-volume", "Master Volume",
                             "Master volume level (0.0-1.0)",
                             0.0, 1.0, DEFAULT_MASTER_VOLUME,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MUSIC_VOLUME] =
        g_param_spec_double ("music-volume", "Music Volume",
                             "Music volume level (0.0-1.0)",
                             0.0, 1.0, DEFAULT_MUSIC_VOLUME,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SFX_VOLUME] =
        g_param_spec_double ("sfx-volume", "SFX Volume",
                             "Sound effects volume level (0.0-1.0)",
                             0.0, 1.0, DEFAULT_SFX_VOLUME,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VOICE_VOLUME] =
        g_param_spec_double ("voice-volume", "Voice Volume",
                             "Voice/dialogue volume level (0.0-1.0)",
                             0.0, 1.0, DEFAULT_VOICE_VOLUME,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MUTED] =
        g_param_spec_boolean ("muted", "Muted", "Whether audio is muted",
                              DEFAULT_MUTED,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MONO_AUDIO] =
        g_param_spec_boolean ("mono-audio", "Mono Audio",
                              "Enable mono audio (accessibility)",
                              DEFAULT_MONO_AUDIO,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SUBTITLES_ENABLED] =
        g_param_spec_boolean ("subtitles-enabled", "Subtitles Enabled",
                              "Enable subtitles",
                              DEFAULT_SUBTITLES,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AUDIO_DEVICE] =
        g_param_spec_string ("audio-device", "Audio Device",
                             "Selected audio output device",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_audio_settings_init (LrgAudioSettings *self)
{
    self->master_volume = DEFAULT_MASTER_VOLUME;
    self->music_volume = DEFAULT_MUSIC_VOLUME;
    self->sfx_volume = DEFAULT_SFX_VOLUME;
    self->voice_volume = DEFAULT_VOICE_VOLUME;
    self->muted = DEFAULT_MUTED;
    self->mono_audio = DEFAULT_MONO_AUDIO;
    self->subtitles_enabled = DEFAULT_SUBTITLES;
    self->audio_device = NULL;
}

/* Public API */

LrgAudioSettings *
lrg_audio_settings_new (void)
{
    return g_object_new (LRG_TYPE_AUDIO_SETTINGS, NULL);
}

gdouble
lrg_audio_settings_get_master_volume (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), 0.0);
    return self->master_volume;
}

void
lrg_audio_settings_set_master_volume (LrgAudioSettings *self,
                                      gdouble           volume)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    volume = CLAMP (volume, 0.0, 1.0);

    if (self->master_volume != volume)
    {
        self->master_volume = volume;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MASTER_VOLUME]);
        emit_changed (self, "master-volume");
    }
}

gdouble
lrg_audio_settings_get_music_volume (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), 0.0);
    return self->music_volume;
}

void
lrg_audio_settings_set_music_volume (LrgAudioSettings *self,
                                     gdouble           volume)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    volume = CLAMP (volume, 0.0, 1.0);

    if (self->music_volume != volume)
    {
        self->music_volume = volume;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MUSIC_VOLUME]);
        emit_changed (self, "music-volume");
    }
}

gdouble
lrg_audio_settings_get_sfx_volume (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), 0.0);
    return self->sfx_volume;
}

void
lrg_audio_settings_set_sfx_volume (LrgAudioSettings *self,
                                   gdouble           volume)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    volume = CLAMP (volume, 0.0, 1.0);

    if (self->sfx_volume != volume)
    {
        self->sfx_volume = volume;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SFX_VOLUME]);
        emit_changed (self, "sfx-volume");
    }
}

gdouble
lrg_audio_settings_get_voice_volume (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), 0.0);
    return self->voice_volume;
}

void
lrg_audio_settings_set_voice_volume (LrgAudioSettings *self,
                                     gdouble           volume)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    volume = CLAMP (volume, 0.0, 1.0);

    if (self->voice_volume != volume)
    {
        self->voice_volume = volume;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOICE_VOLUME]);
        emit_changed (self, "voice-volume");
    }
}

gboolean
lrg_audio_settings_get_muted (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), FALSE);
    return self->muted;
}

void
lrg_audio_settings_set_muted (LrgAudioSettings *self,
                              gboolean          muted)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    muted = !!muted;

    if (self->muted != muted)
    {
        self->muted = muted;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MUTED]);
        emit_changed (self, "muted");
    }
}

gboolean
lrg_audio_settings_get_mono_audio (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), FALSE);
    return self->mono_audio;
}

void
lrg_audio_settings_set_mono_audio (LrgAudioSettings *self,
                                   gboolean          mono)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    mono = !!mono;

    if (self->mono_audio != mono)
    {
        self->mono_audio = mono;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MONO_AUDIO]);
        emit_changed (self, "mono-audio");
    }
}

gboolean
lrg_audio_settings_get_subtitles_enabled (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), FALSE);
    return self->subtitles_enabled;
}

void
lrg_audio_settings_set_subtitles_enabled (LrgAudioSettings *self,
                                          gboolean          enabled)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    enabled = !!enabled;

    if (self->subtitles_enabled != enabled)
    {
        self->subtitles_enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLES_ENABLED]);
        emit_changed (self, "subtitles-enabled");
    }
}

const gchar *
lrg_audio_settings_get_audio_device (LrgAudioSettings *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_SETTINGS (self), NULL);
    return self->audio_device;
}

void
lrg_audio_settings_set_audio_device (LrgAudioSettings *self,
                                     const gchar      *device)
{
    g_return_if_fail (LRG_IS_AUDIO_SETTINGS (self));

    if (g_strcmp0 (self->audio_device, device) != 0)
    {
        g_clear_pointer (&self->audio_device, g_free);
        self->audio_device = g_strdup (device);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUDIO_DEVICE]);
        emit_changed (self, "audio-device");
    }
}
