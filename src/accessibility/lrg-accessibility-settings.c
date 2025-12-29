/* lrg-accessibility-settings.c - Accessibility preferences container
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-accessibility-settings.h"
#include <gio/gio.h>

/**
 * SECTION:lrg-accessibility-settings
 * @title: LrgAccessibilitySettings
 * @short_description: Accessibility preferences container
 *
 * #LrgAccessibilitySettings is a settings group that contains
 * all accessibility-related preferences. It extends #LrgSettingsGroup
 * and can be added to the main #LrgSettings container.
 *
 * Features are organized into categories:
 *
 * - **Visual**: Colorblind modes, high contrast, UI scaling,
 *   reduced motion, screen shake intensity
 * - **Audio**: Subtitles, closed captions, visual audio cues,
 *   subtitle sizing and background
 * - **Motor**: Hold-to-toggle, auto-aim, input timing
 * - **Cognitive**: Objective reminders, skip/pause cutscenes
 * - **Screen Reader**: TTS enable, speech rate
 */

struct _LrgAccessibilitySettings
{
    LrgSettingsGroup  parent_instance;

    /* Visual */
    LrgColorblindMode colorblind_mode;
    gboolean          high_contrast;
    gfloat            ui_scale;
    gboolean          reduce_motion;
    gfloat            screen_shake_intensity;

    /* Audio */
    gboolean          subtitles_enabled;
    gboolean          closed_captions;
    gfloat            subtitle_size;
    gfloat            subtitle_background;
    gboolean          visual_audio_cues;

    /* Motor */
    gboolean          hold_to_toggle;
    gboolean          auto_aim;
    gfloat            input_timing_multiplier;

    /* Cognitive */
    gboolean          objective_reminders;
    gboolean          skip_cutscenes;
    gboolean          pause_during_cutscenes;

    /* Screen Reader */
    gboolean          screen_reader_enabled;
    gfloat            screen_reader_rate;
};

G_DEFINE_TYPE (LrgAccessibilitySettings, lrg_accessibility_settings, LRG_TYPE_SETTINGS_GROUP)

enum
{
    PROP_0,
    /* Visual */
    PROP_COLORBLIND_MODE,
    PROP_HIGH_CONTRAST,
    PROP_UI_SCALE,
    PROP_REDUCE_MOTION,
    PROP_SCREEN_SHAKE_INTENSITY,
    /* Audio */
    PROP_SUBTITLES_ENABLED,
    PROP_CLOSED_CAPTIONS,
    PROP_SUBTITLE_SIZE,
    PROP_SUBTITLE_BACKGROUND,
    PROP_VISUAL_AUDIO_CUES,
    /* Motor */
    PROP_HOLD_TO_TOGGLE,
    PROP_AUTO_AIM,
    PROP_INPUT_TIMING_MULTIPLIER,
    /* Cognitive */
    PROP_OBJECTIVE_REMINDERS,
    PROP_SKIP_CUTSCENES,
    PROP_PAUSE_DURING_CUTSCENES,
    /* Screen Reader */
    PROP_SCREEN_READER_ENABLED,
    PROP_SCREEN_READER_RATE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static const gchar *
lrg_accessibility_settings_get_group_name (LrgSettingsGroup *group)
{
    return "accessibility";
}

static void
lrg_accessibility_settings_reset (LrgSettingsGroup *group)
{
    LrgAccessibilitySettings *self = LRG_ACCESSIBILITY_SETTINGS (group);

    /* Visual defaults */
    self->colorblind_mode = LRG_COLORBLIND_NONE;
    self->high_contrast = FALSE;
    self->ui_scale = 1.0f;
    self->reduce_motion = FALSE;
    self->screen_shake_intensity = 1.0f;

    /* Audio defaults */
    self->subtitles_enabled = FALSE;
    self->closed_captions = FALSE;
    self->subtitle_size = 1.0f;
    self->subtitle_background = 0.75f;
    self->visual_audio_cues = FALSE;

    /* Motor defaults */
    self->hold_to_toggle = FALSE;
    self->auto_aim = FALSE;
    self->input_timing_multiplier = 1.0f;

    /* Cognitive defaults */
    self->objective_reminders = TRUE;
    self->skip_cutscenes = TRUE;
    self->pause_during_cutscenes = TRUE;

    /* Screen Reader defaults */
    self->screen_reader_enabled = FALSE;
    self->screen_reader_rate = 1.0f;

    lrg_settings_group_mark_dirty (group);
}

static void
lrg_accessibility_settings_apply (LrgSettingsGroup *group)
{
    /* Apply accessibility settings to the engine */
    /* This would configure color filters, screen reader, etc. */
}

static GVariant *
lrg_accessibility_settings_serialize (LrgSettingsGroup  *group,
                                      GError           **error)
{
    LrgAccessibilitySettings *self = LRG_ACCESSIBILITY_SETTINGS (group);
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    /* Visual */
    g_variant_builder_add (&builder, "{sv}", "colorblind-mode",
                           g_variant_new_int32 (self->colorblind_mode));
    g_variant_builder_add (&builder, "{sv}", "high-contrast",
                           g_variant_new_boolean (self->high_contrast));
    g_variant_builder_add (&builder, "{sv}", "ui-scale",
                           g_variant_new_double (self->ui_scale));
    g_variant_builder_add (&builder, "{sv}", "reduce-motion",
                           g_variant_new_boolean (self->reduce_motion));
    g_variant_builder_add (&builder, "{sv}", "screen-shake-intensity",
                           g_variant_new_double (self->screen_shake_intensity));

    /* Audio */
    g_variant_builder_add (&builder, "{sv}", "subtitles-enabled",
                           g_variant_new_boolean (self->subtitles_enabled));
    g_variant_builder_add (&builder, "{sv}", "closed-captions",
                           g_variant_new_boolean (self->closed_captions));
    g_variant_builder_add (&builder, "{sv}", "subtitle-size",
                           g_variant_new_double (self->subtitle_size));
    g_variant_builder_add (&builder, "{sv}", "subtitle-background",
                           g_variant_new_double (self->subtitle_background));
    g_variant_builder_add (&builder, "{sv}", "visual-audio-cues",
                           g_variant_new_boolean (self->visual_audio_cues));

    /* Motor */
    g_variant_builder_add (&builder, "{sv}", "hold-to-toggle",
                           g_variant_new_boolean (self->hold_to_toggle));
    g_variant_builder_add (&builder, "{sv}", "auto-aim",
                           g_variant_new_boolean (self->auto_aim));
    g_variant_builder_add (&builder, "{sv}", "input-timing-multiplier",
                           g_variant_new_double (self->input_timing_multiplier));

    /* Cognitive */
    g_variant_builder_add (&builder, "{sv}", "objective-reminders",
                           g_variant_new_boolean (self->objective_reminders));
    g_variant_builder_add (&builder, "{sv}", "skip-cutscenes",
                           g_variant_new_boolean (self->skip_cutscenes));
    g_variant_builder_add (&builder, "{sv}", "pause-during-cutscenes",
                           g_variant_new_boolean (self->pause_during_cutscenes));

    /* Screen Reader */
    g_variant_builder_add (&builder, "{sv}", "screen-reader-enabled",
                           g_variant_new_boolean (self->screen_reader_enabled));
    g_variant_builder_add (&builder, "{sv}", "screen-reader-rate",
                           g_variant_new_double (self->screen_reader_rate));

    return g_variant_builder_end (&builder);
}

static gboolean
lrg_accessibility_settings_deserialize (LrgSettingsGroup  *group,
                                        GVariant          *data,
                                        GError           **error)
{
    LrgAccessibilitySettings *self = LRG_ACCESSIBILITY_SETTINGS (group);
    GVariant *value;

    if (!g_variant_is_of_type (data, G_VARIANT_TYPE ("a{sv}")))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Expected a{sv} variant for accessibility settings");
        return FALSE;
    }

    /* Visual */
    value = g_variant_lookup_value (data, "colorblind-mode", G_VARIANT_TYPE_INT32);
    if (value)
    {
        self->colorblind_mode = g_variant_get_int32 (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "high-contrast", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->high_contrast = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "ui-scale", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->ui_scale = CLAMP (g_variant_get_double (value), 0.5, 2.0);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "reduce-motion", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->reduce_motion = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "screen-shake-intensity", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->screen_shake_intensity = CLAMP (g_variant_get_double (value), 0.0, 1.0);
        g_variant_unref (value);
    }

    /* Audio */
    value = g_variant_lookup_value (data, "subtitles-enabled", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->subtitles_enabled = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "closed-captions", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->closed_captions = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "subtitle-size", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->subtitle_size = CLAMP (g_variant_get_double (value), 0.5, 2.0);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "subtitle-background", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->subtitle_background = CLAMP (g_variant_get_double (value), 0.0, 1.0);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "visual-audio-cues", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->visual_audio_cues = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    /* Motor */
    value = g_variant_lookup_value (data, "hold-to-toggle", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->hold_to_toggle = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "auto-aim", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->auto_aim = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "input-timing-multiplier", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->input_timing_multiplier = CLAMP (g_variant_get_double (value), 1.0, 3.0);
        g_variant_unref (value);
    }

    /* Cognitive */
    value = g_variant_lookup_value (data, "objective-reminders", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->objective_reminders = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "skip-cutscenes", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->skip_cutscenes = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "pause-during-cutscenes", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->pause_during_cutscenes = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    /* Screen Reader */
    value = g_variant_lookup_value (data, "screen-reader-enabled", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->screen_reader_enabled = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "screen-reader-rate", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->screen_reader_rate = CLAMP (g_variant_get_double (value), 0.5, 2.0);
        g_variant_unref (value);
    }

    return TRUE;
}

static void
lrg_accessibility_settings_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
    LrgAccessibilitySettings *self = LRG_ACCESSIBILITY_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_COLORBLIND_MODE:
        lrg_accessibility_settings_set_colorblind_mode (self, g_value_get_enum (value));
        break;
    case PROP_HIGH_CONTRAST:
        lrg_accessibility_settings_set_high_contrast (self, g_value_get_boolean (value));
        break;
    case PROP_UI_SCALE:
        lrg_accessibility_settings_set_ui_scale (self, g_value_get_float (value));
        break;
    case PROP_REDUCE_MOTION:
        lrg_accessibility_settings_set_reduce_motion (self, g_value_get_boolean (value));
        break;
    case PROP_SCREEN_SHAKE_INTENSITY:
        lrg_accessibility_settings_set_screen_shake_intensity (self, g_value_get_float (value));
        break;
    case PROP_SUBTITLES_ENABLED:
        lrg_accessibility_settings_set_subtitles_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_CLOSED_CAPTIONS:
        lrg_accessibility_settings_set_closed_captions (self, g_value_get_boolean (value));
        break;
    case PROP_SUBTITLE_SIZE:
        lrg_accessibility_settings_set_subtitle_size (self, g_value_get_float (value));
        break;
    case PROP_SUBTITLE_BACKGROUND:
        lrg_accessibility_settings_set_subtitle_background (self, g_value_get_float (value));
        break;
    case PROP_VISUAL_AUDIO_CUES:
        lrg_accessibility_settings_set_visual_audio_cues (self, g_value_get_boolean (value));
        break;
    case PROP_HOLD_TO_TOGGLE:
        lrg_accessibility_settings_set_hold_to_toggle (self, g_value_get_boolean (value));
        break;
    case PROP_AUTO_AIM:
        lrg_accessibility_settings_set_auto_aim (self, g_value_get_boolean (value));
        break;
    case PROP_INPUT_TIMING_MULTIPLIER:
        lrg_accessibility_settings_set_input_timing_multiplier (self, g_value_get_float (value));
        break;
    case PROP_OBJECTIVE_REMINDERS:
        lrg_accessibility_settings_set_objective_reminders (self, g_value_get_boolean (value));
        break;
    case PROP_SKIP_CUTSCENES:
        lrg_accessibility_settings_set_skip_cutscenes (self, g_value_get_boolean (value));
        break;
    case PROP_PAUSE_DURING_CUTSCENES:
        lrg_accessibility_settings_set_pause_during_cutscenes (self, g_value_get_boolean (value));
        break;
    case PROP_SCREEN_READER_ENABLED:
        lrg_accessibility_settings_set_screen_reader_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_SCREEN_READER_RATE:
        lrg_accessibility_settings_set_screen_reader_rate (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_accessibility_settings_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
    LrgAccessibilitySettings *self = LRG_ACCESSIBILITY_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_COLORBLIND_MODE:
        g_value_set_enum (value, self->colorblind_mode);
        break;
    case PROP_HIGH_CONTRAST:
        g_value_set_boolean (value, self->high_contrast);
        break;
    case PROP_UI_SCALE:
        g_value_set_float (value, self->ui_scale);
        break;
    case PROP_REDUCE_MOTION:
        g_value_set_boolean (value, self->reduce_motion);
        break;
    case PROP_SCREEN_SHAKE_INTENSITY:
        g_value_set_float (value, self->screen_shake_intensity);
        break;
    case PROP_SUBTITLES_ENABLED:
        g_value_set_boolean (value, self->subtitles_enabled);
        break;
    case PROP_CLOSED_CAPTIONS:
        g_value_set_boolean (value, self->closed_captions);
        break;
    case PROP_SUBTITLE_SIZE:
        g_value_set_float (value, self->subtitle_size);
        break;
    case PROP_SUBTITLE_BACKGROUND:
        g_value_set_float (value, self->subtitle_background);
        break;
    case PROP_VISUAL_AUDIO_CUES:
        g_value_set_boolean (value, self->visual_audio_cues);
        break;
    case PROP_HOLD_TO_TOGGLE:
        g_value_set_boolean (value, self->hold_to_toggle);
        break;
    case PROP_AUTO_AIM:
        g_value_set_boolean (value, self->auto_aim);
        break;
    case PROP_INPUT_TIMING_MULTIPLIER:
        g_value_set_float (value, self->input_timing_multiplier);
        break;
    case PROP_OBJECTIVE_REMINDERS:
        g_value_set_boolean (value, self->objective_reminders);
        break;
    case PROP_SKIP_CUTSCENES:
        g_value_set_boolean (value, self->skip_cutscenes);
        break;
    case PROP_PAUSE_DURING_CUTSCENES:
        g_value_set_boolean (value, self->pause_during_cutscenes);
        break;
    case PROP_SCREEN_READER_ENABLED:
        g_value_set_boolean (value, self->screen_reader_enabled);
        break;
    case PROP_SCREEN_READER_RATE:
        g_value_set_float (value, self->screen_reader_rate);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_accessibility_settings_class_init (LrgAccessibilitySettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgSettingsGroupClass *group_class = LRG_SETTINGS_GROUP_CLASS (klass);

    object_class->set_property = lrg_accessibility_settings_set_property;
    object_class->get_property = lrg_accessibility_settings_get_property;

    group_class->get_group_name = lrg_accessibility_settings_get_group_name;
    group_class->apply = lrg_accessibility_settings_apply;
    group_class->reset = lrg_accessibility_settings_reset;
    group_class->serialize = lrg_accessibility_settings_serialize;
    group_class->deserialize = lrg_accessibility_settings_deserialize;

    /* Visual properties */
    properties[PROP_COLORBLIND_MODE] =
        g_param_spec_enum ("colorblind-mode",
                           "Colorblind Mode",
                           "Colorblind accessibility mode",
                           LRG_TYPE_COLORBLIND_MODE,
                           LRG_COLORBLIND_NONE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HIGH_CONTRAST] =
        g_param_spec_boolean ("high-contrast",
                              "High Contrast",
                              "Enable high contrast mode",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_UI_SCALE] =
        g_param_spec_float ("ui-scale",
                            "UI Scale",
                            "UI scale factor",
                            0.5f, 2.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_REDUCE_MOTION] =
        g_param_spec_boolean ("reduce-motion",
                              "Reduce Motion",
                              "Reduce motion effects",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SCREEN_SHAKE_INTENSITY] =
        g_param_spec_float ("screen-shake-intensity",
                            "Screen Shake Intensity",
                            "Screen shake intensity",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /* Audio properties */
    properties[PROP_SUBTITLES_ENABLED] =
        g_param_spec_boolean ("subtitles-enabled",
                              "Subtitles Enabled",
                              "Enable subtitles",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CLOSED_CAPTIONS] =
        g_param_spec_boolean ("closed-captions",
                              "Closed Captions",
                              "Enable closed captions",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SUBTITLE_SIZE] =
        g_param_spec_float ("subtitle-size",
                            "Subtitle Size",
                            "Subtitle size multiplier",
                            0.5f, 2.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SUBTITLE_BACKGROUND] =
        g_param_spec_float ("subtitle-background",
                            "Subtitle Background",
                            "Subtitle background opacity",
                            0.0f, 1.0f, 0.75f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VISUAL_AUDIO_CUES] =
        g_param_spec_boolean ("visual-audio-cues",
                              "Visual Audio Cues",
                              "Show visual indicators for audio",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /* Motor properties */
    properties[PROP_HOLD_TO_TOGGLE] =
        g_param_spec_boolean ("hold-to-toggle",
                              "Hold to Toggle",
                              "Convert hold actions to toggles",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AUTO_AIM] =
        g_param_spec_boolean ("auto-aim",
                              "Auto Aim",
                              "Enable auto-aim assistance",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_INPUT_TIMING_MULTIPLIER] =
        g_param_spec_float ("input-timing-multiplier",
                            "Input Timing Multiplier",
                            "Input timing window multiplier",
                            1.0f, 3.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /* Cognitive properties */
    properties[PROP_OBJECTIVE_REMINDERS] =
        g_param_spec_boolean ("objective-reminders",
                              "Objective Reminders",
                              "Show objective reminders",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SKIP_CUTSCENES] =
        g_param_spec_boolean ("skip-cutscenes",
                              "Skip Cutscenes",
                              "Allow skipping cutscenes",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PAUSE_DURING_CUTSCENES] =
        g_param_spec_boolean ("pause-during-cutscenes",
                              "Pause During Cutscenes",
                              "Allow pausing during cutscenes",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /* Screen Reader properties */
    properties[PROP_SCREEN_READER_ENABLED] =
        g_param_spec_boolean ("screen-reader-enabled",
                              "Screen Reader Enabled",
                              "Enable screen reader",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SCREEN_READER_RATE] =
        g_param_spec_float ("screen-reader-rate",
                            "Screen Reader Rate",
                            "Screen reader speech rate",
                            0.5f, 2.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_accessibility_settings_init (LrgAccessibilitySettings *self)
{
    /* Set defaults */
    lrg_accessibility_settings_reset (LRG_SETTINGS_GROUP (self));
    lrg_settings_group_mark_clean (LRG_SETTINGS_GROUP (self));
}

LrgAccessibilitySettings *
lrg_accessibility_settings_new (void)
{
    return g_object_new (LRG_TYPE_ACCESSIBILITY_SETTINGS, NULL);
}

/* Visual getters/setters */

LrgColorblindMode
lrg_accessibility_settings_get_colorblind_mode (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), LRG_COLORBLIND_NONE);
    return self->colorblind_mode;
}

void
lrg_accessibility_settings_set_colorblind_mode (LrgAccessibilitySettings *self,
                                                LrgColorblindMode         mode)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    if (self->colorblind_mode != mode)
    {
        self->colorblind_mode = mode;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLORBLIND_MODE]);
    }
}

gboolean
lrg_accessibility_settings_get_high_contrast (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->high_contrast;
}

void
lrg_accessibility_settings_set_high_contrast (LrgAccessibilitySettings *self,
                                              gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->high_contrast != enabled)
    {
        self->high_contrast = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIGH_CONTRAST]);
    }
}

gfloat
lrg_accessibility_settings_get_ui_scale (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), 1.0f);
    return self->ui_scale;
}

void
lrg_accessibility_settings_set_ui_scale (LrgAccessibilitySettings *self,
                                         gfloat                    scale)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    scale = CLAMP (scale, 0.5f, 2.0f);
    if (self->ui_scale != scale)
    {
        self->ui_scale = scale;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UI_SCALE]);
    }
}

gboolean
lrg_accessibility_settings_get_reduce_motion (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->reduce_motion;
}

void
lrg_accessibility_settings_set_reduce_motion (LrgAccessibilitySettings *self,
                                              gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->reduce_motion != enabled)
    {
        self->reduce_motion = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REDUCE_MOTION]);
    }
}

gfloat
lrg_accessibility_settings_get_screen_shake_intensity (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), 1.0f);
    return self->screen_shake_intensity;
}

void
lrg_accessibility_settings_set_screen_shake_intensity (LrgAccessibilitySettings *self,
                                                       gfloat                    intensity)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    intensity = CLAMP (intensity, 0.0f, 1.0f);
    if (self->screen_shake_intensity != intensity)
    {
        self->screen_shake_intensity = intensity;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCREEN_SHAKE_INTENSITY]);
    }
}

/* Audio getters/setters */

gboolean
lrg_accessibility_settings_get_subtitles_enabled (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->subtitles_enabled;
}

void
lrg_accessibility_settings_set_subtitles_enabled (LrgAccessibilitySettings *self,
                                                  gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->subtitles_enabled != enabled)
    {
        self->subtitles_enabled = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLES_ENABLED]);
    }
}

gboolean
lrg_accessibility_settings_get_closed_captions (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->closed_captions;
}

void
lrg_accessibility_settings_set_closed_captions (LrgAccessibilitySettings *self,
                                                gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->closed_captions != enabled)
    {
        self->closed_captions = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLOSED_CAPTIONS]);
    }
}

gfloat
lrg_accessibility_settings_get_subtitle_size (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), 1.0f);
    return self->subtitle_size;
}

void
lrg_accessibility_settings_set_subtitle_size (LrgAccessibilitySettings *self,
                                              gfloat                    size)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    size = CLAMP (size, 0.5f, 2.0f);
    if (self->subtitle_size != size)
    {
        self->subtitle_size = size;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE_SIZE]);
    }
}

gfloat
lrg_accessibility_settings_get_subtitle_background (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), 0.75f);
    return self->subtitle_background;
}

void
lrg_accessibility_settings_set_subtitle_background (LrgAccessibilitySettings *self,
                                                    gfloat                    opacity)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    opacity = CLAMP (opacity, 0.0f, 1.0f);
    if (self->subtitle_background != opacity)
    {
        self->subtitle_background = opacity;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE_BACKGROUND]);
    }
}

gboolean
lrg_accessibility_settings_get_visual_audio_cues (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->visual_audio_cues;
}

void
lrg_accessibility_settings_set_visual_audio_cues (LrgAccessibilitySettings *self,
                                                  gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->visual_audio_cues != enabled)
    {
        self->visual_audio_cues = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISUAL_AUDIO_CUES]);
    }
}

/* Motor getters/setters */

gboolean
lrg_accessibility_settings_get_hold_to_toggle (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->hold_to_toggle;
}

void
lrg_accessibility_settings_set_hold_to_toggle (LrgAccessibilitySettings *self,
                                               gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->hold_to_toggle != enabled)
    {
        self->hold_to_toggle = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOLD_TO_TOGGLE]);
    }
}

gboolean
lrg_accessibility_settings_get_auto_aim (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->auto_aim;
}

void
lrg_accessibility_settings_set_auto_aim (LrgAccessibilitySettings *self,
                                         gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->auto_aim != enabled)
    {
        self->auto_aim = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_AIM]);
    }
}

gfloat
lrg_accessibility_settings_get_input_timing_multiplier (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), 1.0f);
    return self->input_timing_multiplier;
}

void
lrg_accessibility_settings_set_input_timing_multiplier (LrgAccessibilitySettings *self,
                                                        gfloat                    multiplier)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    multiplier = CLAMP (multiplier, 1.0f, 3.0f);
    if (self->input_timing_multiplier != multiplier)
    {
        self->input_timing_multiplier = multiplier;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INPUT_TIMING_MULTIPLIER]);
    }
}

/* Cognitive getters/setters */

gboolean
lrg_accessibility_settings_get_objective_reminders (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), TRUE);
    return self->objective_reminders;
}

void
lrg_accessibility_settings_set_objective_reminders (LrgAccessibilitySettings *self,
                                                    gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->objective_reminders != enabled)
    {
        self->objective_reminders = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OBJECTIVE_REMINDERS]);
    }
}

gboolean
lrg_accessibility_settings_get_skip_cutscenes (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), TRUE);
    return self->skip_cutscenes;
}

void
lrg_accessibility_settings_set_skip_cutscenes (LrgAccessibilitySettings *self,
                                               gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->skip_cutscenes != enabled)
    {
        self->skip_cutscenes = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKIP_CUTSCENES]);
    }
}

gboolean
lrg_accessibility_settings_get_pause_during_cutscenes (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), TRUE);
    return self->pause_during_cutscenes;
}

void
lrg_accessibility_settings_set_pause_during_cutscenes (LrgAccessibilitySettings *self,
                                                       gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->pause_during_cutscenes != enabled)
    {
        self->pause_during_cutscenes = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAUSE_DURING_CUTSCENES]);
    }
}

/* Screen Reader getters/setters */

gboolean
lrg_accessibility_settings_get_screen_reader_enabled (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), FALSE);
    return self->screen_reader_enabled;
}

void
lrg_accessibility_settings_set_screen_reader_enabled (LrgAccessibilitySettings *self,
                                                      gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    enabled = !!enabled;
    if (self->screen_reader_enabled != enabled)
    {
        self->screen_reader_enabled = enabled;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCREEN_READER_ENABLED]);
    }
}

gfloat
lrg_accessibility_settings_get_screen_reader_rate (LrgAccessibilitySettings *self)
{
    g_return_val_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self), 1.0f);
    return self->screen_reader_rate;
}

void
lrg_accessibility_settings_set_screen_reader_rate (LrgAccessibilitySettings *self,
                                                   gfloat                    rate)
{
    g_return_if_fail (LRG_IS_ACCESSIBILITY_SETTINGS (self));
    rate = CLAMP (rate, 0.5f, 2.0f);
    if (self->screen_reader_rate != rate)
    {
        self->screen_reader_rate = rate;
        lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCREEN_READER_RATE]);
    }
}
