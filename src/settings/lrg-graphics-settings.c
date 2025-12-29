/* lrg-graphics-settings.c - Graphics settings group implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-graphics-settings.h"
#include <gio/gio.h>

/**
 * SECTION:lrg-graphics-settings
 * @title: LrgGraphicsSettings
 * @short_description: Graphics settings group
 *
 * #LrgGraphicsSettings manages all graphics-related settings including
 * resolution, fullscreen mode, quality presets, anti-aliasing, and
 * various visual effects.
 *
 * Quality presets automatically configure individual settings:
 * - Low: Minimal effects, lowest texture/shadow quality
 * - Medium: Balanced settings
 * - High: Most effects enabled, high quality textures/shadows
 * - Ultra: Maximum quality, all effects enabled
 * - Custom: User-defined (set automatically when individual settings change)
 */

/* Default values */
#define DEFAULT_WIDTH           1920
#define DEFAULT_HEIGHT          1080
#define DEFAULT_FULLSCREEN_MODE LRG_FULLSCREEN_WINDOWED
#define DEFAULT_VSYNC           TRUE
#define DEFAULT_FPS_LIMIT       0
#define DEFAULT_QUALITY_PRESET  LRG_QUALITY_HIGH
#define DEFAULT_ANTI_ALIASING   LRG_AA_FXAA
#define DEFAULT_TEXTURE_QUALITY 2
#define DEFAULT_SHADOW_QUALITY  2
#define DEFAULT_BLOOM           TRUE
#define DEFAULT_MOTION_BLUR     FALSE
#define DEFAULT_AMBIENT_OCC     TRUE
#define DEFAULT_VIEW_DISTANCE   1.0

struct _LrgGraphicsSettings
{
    LrgSettingsGroup parent_instance;

    /* Resolution */
    gint width;
    gint height;

    /* Display */
    LrgFullscreenMode fullscreen_mode;
    gboolean vsync;
    gint fps_limit;

    /* Quality */
    LrgQualityPreset quality_preset;
    LrgAntiAliasMode anti_aliasing;
    gint texture_quality;
    gint shadow_quality;

    /* Effects */
    gboolean bloom_enabled;
    gboolean motion_blur_enabled;
    gboolean ambient_occlusion_enabled;

    /* Distance */
    gdouble view_distance;
};

G_DEFINE_TYPE (LrgGraphicsSettings, lrg_graphics_settings, LRG_TYPE_SETTINGS_GROUP)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FULLSCREEN_MODE,
    PROP_VSYNC,
    PROP_FPS_LIMIT,
    PROP_QUALITY_PRESET,
    PROP_ANTI_ALIASING,
    PROP_TEXTURE_QUALITY,
    PROP_SHADOW_QUALITY,
    PROP_BLOOM_ENABLED,
    PROP_MOTION_BLUR_ENABLED,
    PROP_AMBIENT_OCCLUSION_ENABLED,
    PROP_VIEW_DISTANCE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Helper to emit changed signal and mark dirty.
 * Called when any property changes.
 */
static void
emit_changed (LrgGraphicsSettings *self,
              const gchar         *property_name)
{
    lrg_settings_group_mark_dirty (LRG_SETTINGS_GROUP (self));
    g_signal_emit_by_name (self, "changed", property_name);
}

/*
 * Apply quality preset to individual settings.
 * Does NOT emit changed signals - caller should do that.
 */
static void
apply_quality_preset_internal (LrgGraphicsSettings *self,
                               LrgQualityPreset     preset)
{
    switch (preset)
    {
    case LRG_QUALITY_LOW:
        self->anti_aliasing = LRG_AA_NONE;
        self->texture_quality = 0;
        self->shadow_quality = 0;
        self->bloom_enabled = FALSE;
        self->motion_blur_enabled = FALSE;
        self->ambient_occlusion_enabled = FALSE;
        self->view_distance = 0.5;
        break;

    case LRG_QUALITY_MEDIUM:
        self->anti_aliasing = LRG_AA_FXAA;
        self->texture_quality = 1;
        self->shadow_quality = 1;
        self->bloom_enabled = FALSE;
        self->motion_blur_enabled = FALSE;
        self->ambient_occlusion_enabled = FALSE;
        self->view_distance = 0.75;
        break;

    case LRG_QUALITY_HIGH:
        self->anti_aliasing = LRG_AA_FXAA;
        self->texture_quality = 2;
        self->shadow_quality = 2;
        self->bloom_enabled = TRUE;
        self->motion_blur_enabled = FALSE;
        self->ambient_occlusion_enabled = TRUE;
        self->view_distance = 1.0;
        break;

    case LRG_QUALITY_ULTRA:
        self->anti_aliasing = LRG_AA_MSAA_4X;
        self->texture_quality = 3;
        self->shadow_quality = 3;
        self->bloom_enabled = TRUE;
        self->motion_blur_enabled = TRUE;
        self->ambient_occlusion_enabled = TRUE;
        self->view_distance = 2.0;
        break;

    case LRG_QUALITY_CUSTOM:
        /* Custom - don't change individual settings */
        break;
    }
}

/* Virtual method implementations */

static void
lrg_graphics_settings_apply (LrgSettingsGroup *group)
{
    /*
     * TODO: Apply settings to the renderer/engine.
     * This would integrate with graylib/engine systems.
     * For now, just log that we would apply.
     */
    g_debug ("LrgGraphicsSettings: apply() called - would apply to renderer");
}

static void
lrg_graphics_settings_reset (LrgSettingsGroup *group)
{
    LrgGraphicsSettings *self = LRG_GRAPHICS_SETTINGS (group);

    self->width = DEFAULT_WIDTH;
    self->height = DEFAULT_HEIGHT;
    self->fullscreen_mode = DEFAULT_FULLSCREEN_MODE;
    self->vsync = DEFAULT_VSYNC;
    self->fps_limit = DEFAULT_FPS_LIMIT;
    self->quality_preset = DEFAULT_QUALITY_PRESET;
    self->anti_aliasing = DEFAULT_ANTI_ALIASING;
    self->texture_quality = DEFAULT_TEXTURE_QUALITY;
    self->shadow_quality = DEFAULT_SHADOW_QUALITY;
    self->bloom_enabled = DEFAULT_BLOOM;
    self->motion_blur_enabled = DEFAULT_MOTION_BLUR;
    self->ambient_occlusion_enabled = DEFAULT_AMBIENT_OCC;
    self->view_distance = DEFAULT_VIEW_DISTANCE;

    emit_changed (self, NULL);
}

static const gchar *
lrg_graphics_settings_get_group_name (LrgSettingsGroup *group)
{
    return "graphics";
}

static GVariant *
lrg_graphics_settings_serialize (LrgSettingsGroup  *group,
                                 GError           **error)
{
    LrgGraphicsSettings *self = LRG_GRAPHICS_SETTINGS (group);
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_add (&builder, "{sv}", "width",
                           g_variant_new_int32 (self->width));
    g_variant_builder_add (&builder, "{sv}", "height",
                           g_variant_new_int32 (self->height));
    g_variant_builder_add (&builder, "{sv}", "fullscreen_mode",
                           g_variant_new_int32 (self->fullscreen_mode));
    g_variant_builder_add (&builder, "{sv}", "vsync",
                           g_variant_new_boolean (self->vsync));
    g_variant_builder_add (&builder, "{sv}", "fps_limit",
                           g_variant_new_int32 (self->fps_limit));
    g_variant_builder_add (&builder, "{sv}", "quality_preset",
                           g_variant_new_int32 (self->quality_preset));
    g_variant_builder_add (&builder, "{sv}", "anti_aliasing",
                           g_variant_new_int32 (self->anti_aliasing));
    g_variant_builder_add (&builder, "{sv}", "texture_quality",
                           g_variant_new_int32 (self->texture_quality));
    g_variant_builder_add (&builder, "{sv}", "shadow_quality",
                           g_variant_new_int32 (self->shadow_quality));
    g_variant_builder_add (&builder, "{sv}", "bloom_enabled",
                           g_variant_new_boolean (self->bloom_enabled));
    g_variant_builder_add (&builder, "{sv}", "motion_blur_enabled",
                           g_variant_new_boolean (self->motion_blur_enabled));
    g_variant_builder_add (&builder, "{sv}", "ambient_occlusion_enabled",
                           g_variant_new_boolean (self->ambient_occlusion_enabled));
    g_variant_builder_add (&builder, "{sv}", "view_distance",
                           g_variant_new_double (self->view_distance));

    return g_variant_builder_end (&builder);
}

/*
 * Helper to get an integer from a GVariant, handling both int32 and int64.
 * JSON-GLib deserializes integers as int64, but we serialize as int32.
 */
static gint
get_variant_int (GVariant    *dict,
                 const gchar *key,
                 gint         default_value)
{
    GVariant *value;

    /* Try int64 first (JSON-GLib format) */
    value = g_variant_lookup_value (dict, key, G_VARIANT_TYPE_INT64);
    if (value)
    {
        gint result = (gint) g_variant_get_int64 (value);
        g_variant_unref (value);
        return result;
    }

    /* Fall back to int32 (native GVariant format) */
    value = g_variant_lookup_value (dict, key, G_VARIANT_TYPE_INT32);
    if (value)
    {
        gint result = g_variant_get_int32 (value);
        g_variant_unref (value);
        return result;
    }

    return default_value;
}

static gboolean
lrg_graphics_settings_deserialize (LrgSettingsGroup  *group,
                                   GVariant          *data,
                                   GError           **error)
{
    LrgGraphicsSettings *self = LRG_GRAPHICS_SETTINGS (group);
    GVariant *value;

    if (!g_variant_is_of_type (data, G_VARIANT_TYPE ("a{sv}")))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Expected a{sv} variant for graphics settings");
        return FALSE;
    }

    /* Extract integer values with int32/int64 handling */
    self->width = get_variant_int (data, "width", self->width);
    self->height = get_variant_int (data, "height", self->height);
    self->fullscreen_mode = get_variant_int (data, "fullscreen_mode", self->fullscreen_mode);
    self->fps_limit = get_variant_int (data, "fps_limit", self->fps_limit);
    self->quality_preset = get_variant_int (data, "quality_preset", self->quality_preset);
    self->anti_aliasing = get_variant_int (data, "anti_aliasing", self->anti_aliasing);
    self->texture_quality = CLAMP (get_variant_int (data, "texture_quality", self->texture_quality), 0, 3);
    self->shadow_quality = CLAMP (get_variant_int (data, "shadow_quality", self->shadow_quality), 0, 3);

    /* Boolean values */
    value = g_variant_lookup_value (data, "vsync", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->vsync = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "bloom_enabled", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->bloom_enabled = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "motion_blur_enabled", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->motion_blur_enabled = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "ambient_occlusion_enabled", G_VARIANT_TYPE_BOOLEAN);
    if (value)
    {
        self->ambient_occlusion_enabled = g_variant_get_boolean (value);
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "view_distance", G_VARIANT_TYPE_DOUBLE);
    if (value)
    {
        self->view_distance = CLAMP (g_variant_get_double (value), 0.5, 2.0);
        g_variant_unref (value);
    }

    return TRUE;
}

/* GObject property methods */

static void
lrg_graphics_settings_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgGraphicsSettings *self = LRG_GRAPHICS_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_WIDTH:
        g_value_set_int (value, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int (value, self->height);
        break;
    case PROP_FULLSCREEN_MODE:
        g_value_set_int (value, self->fullscreen_mode);
        break;
    case PROP_VSYNC:
        g_value_set_boolean (value, self->vsync);
        break;
    case PROP_FPS_LIMIT:
        g_value_set_int (value, self->fps_limit);
        break;
    case PROP_QUALITY_PRESET:
        g_value_set_int (value, self->quality_preset);
        break;
    case PROP_ANTI_ALIASING:
        g_value_set_int (value, self->anti_aliasing);
        break;
    case PROP_TEXTURE_QUALITY:
        g_value_set_int (value, self->texture_quality);
        break;
    case PROP_SHADOW_QUALITY:
        g_value_set_int (value, self->shadow_quality);
        break;
    case PROP_BLOOM_ENABLED:
        g_value_set_boolean (value, self->bloom_enabled);
        break;
    case PROP_MOTION_BLUR_ENABLED:
        g_value_set_boolean (value, self->motion_blur_enabled);
        break;
    case PROP_AMBIENT_OCCLUSION_ENABLED:
        g_value_set_boolean (value, self->ambient_occlusion_enabled);
        break;
    case PROP_VIEW_DISTANCE:
        g_value_set_double (value, self->view_distance);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_graphics_settings_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgGraphicsSettings *self = LRG_GRAPHICS_SETTINGS (object);

    switch (prop_id)
    {
    case PROP_WIDTH:
        lrg_graphics_settings_set_resolution (self, g_value_get_int (value), self->height);
        break;
    case PROP_HEIGHT:
        lrg_graphics_settings_set_resolution (self, self->width, g_value_get_int (value));
        break;
    case PROP_FULLSCREEN_MODE:
        lrg_graphics_settings_set_fullscreen_mode (self, g_value_get_int (value));
        break;
    case PROP_VSYNC:
        lrg_graphics_settings_set_vsync (self, g_value_get_boolean (value));
        break;
    case PROP_FPS_LIMIT:
        lrg_graphics_settings_set_fps_limit (self, g_value_get_int (value));
        break;
    case PROP_QUALITY_PRESET:
        lrg_graphics_settings_set_quality_preset (self, g_value_get_int (value));
        break;
    case PROP_ANTI_ALIASING:
        lrg_graphics_settings_set_anti_aliasing (self, g_value_get_int (value));
        break;
    case PROP_TEXTURE_QUALITY:
        lrg_graphics_settings_set_texture_quality (self, g_value_get_int (value));
        break;
    case PROP_SHADOW_QUALITY:
        lrg_graphics_settings_set_shadow_quality (self, g_value_get_int (value));
        break;
    case PROP_BLOOM_ENABLED:
        lrg_graphics_settings_set_bloom_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_MOTION_BLUR_ENABLED:
        lrg_graphics_settings_set_motion_blur_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_AMBIENT_OCCLUSION_ENABLED:
        lrg_graphics_settings_set_ambient_occlusion_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_VIEW_DISTANCE:
        lrg_graphics_settings_set_view_distance (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_graphics_settings_class_init (LrgGraphicsSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgSettingsGroupClass *group_class = LRG_SETTINGS_GROUP_CLASS (klass);

    object_class->get_property = lrg_graphics_settings_get_property;
    object_class->set_property = lrg_graphics_settings_set_property;

    /* Override virtual methods */
    group_class->apply = lrg_graphics_settings_apply;
    group_class->reset = lrg_graphics_settings_reset;
    group_class->get_group_name = lrg_graphics_settings_get_group_name;
    group_class->serialize = lrg_graphics_settings_serialize;
    group_class->deserialize = lrg_graphics_settings_deserialize;

    /* Install properties */
    properties[PROP_WIDTH] =
        g_param_spec_int ("width", "Width", "Screen width in pixels",
                          320, 7680, DEFAULT_WIDTH,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEIGHT] =
        g_param_spec_int ("height", "Height", "Screen height in pixels",
                          240, 4320, DEFAULT_HEIGHT,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FULLSCREEN_MODE] =
        g_param_spec_int ("fullscreen-mode", "Fullscreen Mode",
                          "Fullscreen display mode",
                          LRG_FULLSCREEN_WINDOWED, LRG_FULLSCREEN_BORDERLESS,
                          DEFAULT_FULLSCREEN_MODE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VSYNC] =
        g_param_spec_boolean ("vsync", "VSync", "Vertical sync enabled",
                              DEFAULT_VSYNC,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FPS_LIMIT] =
        g_param_spec_int ("fps-limit", "FPS Limit",
                          "Maximum frames per second (0 = unlimited)",
                          0, 300, DEFAULT_FPS_LIMIT,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_QUALITY_PRESET] =
        g_param_spec_int ("quality-preset", "Quality Preset",
                          "Graphics quality preset",
                          LRG_QUALITY_LOW, LRG_QUALITY_CUSTOM,
                          DEFAULT_QUALITY_PRESET,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ANTI_ALIASING] =
        g_param_spec_int ("anti-aliasing", "Anti-Aliasing",
                          "Anti-aliasing mode",
                          LRG_AA_NONE, LRG_AA_MSAA_8X,
                          DEFAULT_ANTI_ALIASING,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TEXTURE_QUALITY] =
        g_param_spec_int ("texture-quality", "Texture Quality",
                          "Texture quality level (0-3)",
                          0, 3, DEFAULT_TEXTURE_QUALITY,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHADOW_QUALITY] =
        g_param_spec_int ("shadow-quality", "Shadow Quality",
                          "Shadow quality level (0-3)",
                          0, 3, DEFAULT_SHADOW_QUALITY,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BLOOM_ENABLED] =
        g_param_spec_boolean ("bloom-enabled", "Bloom Enabled",
                              "Enable bloom effect",
                              DEFAULT_BLOOM,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MOTION_BLUR_ENABLED] =
        g_param_spec_boolean ("motion-blur-enabled", "Motion Blur Enabled",
                              "Enable motion blur effect",
                              DEFAULT_MOTION_BLUR,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AMBIENT_OCCLUSION_ENABLED] =
        g_param_spec_boolean ("ambient-occlusion-enabled",
                              "Ambient Occlusion Enabled",
                              "Enable ambient occlusion",
                              DEFAULT_AMBIENT_OCC,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VIEW_DISTANCE] =
        g_param_spec_double ("view-distance", "View Distance",
                             "View distance multiplier (0.5-2.0)",
                             0.5, 2.0, DEFAULT_VIEW_DISTANCE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_graphics_settings_init (LrgGraphicsSettings *self)
{
    /* Set defaults */
    self->width = DEFAULT_WIDTH;
    self->height = DEFAULT_HEIGHT;
    self->fullscreen_mode = DEFAULT_FULLSCREEN_MODE;
    self->vsync = DEFAULT_VSYNC;
    self->fps_limit = DEFAULT_FPS_LIMIT;
    self->quality_preset = DEFAULT_QUALITY_PRESET;
    self->anti_aliasing = DEFAULT_ANTI_ALIASING;
    self->texture_quality = DEFAULT_TEXTURE_QUALITY;
    self->shadow_quality = DEFAULT_SHADOW_QUALITY;
    self->bloom_enabled = DEFAULT_BLOOM;
    self->motion_blur_enabled = DEFAULT_MOTION_BLUR;
    self->ambient_occlusion_enabled = DEFAULT_AMBIENT_OCC;
    self->view_distance = DEFAULT_VIEW_DISTANCE;
}

/* Public API */

/**
 * lrg_graphics_settings_new:
 *
 * Creates a new #LrgGraphicsSettings with default values.
 *
 * Returns: (transfer full): A new #LrgGraphicsSettings
 */
LrgGraphicsSettings *
lrg_graphics_settings_new (void)
{
    return g_object_new (LRG_TYPE_GRAPHICS_SETTINGS, NULL);
}

void
lrg_graphics_settings_get_resolution (LrgGraphicsSettings *self,
                                      gint                *width,
                                      gint                *height)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    if (width)
        *width = self->width;
    if (height)
        *height = self->height;
}

void
lrg_graphics_settings_set_resolution (LrgGraphicsSettings *self,
                                      gint                 width,
                                      gint                 height)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    width = CLAMP (width, 320, 7680);
    height = CLAMP (height, 240, 4320);

    if (self->width != width || self->height != height)
    {
        self->width = width;
        self->height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
        emit_changed (self, "resolution");
    }
}

LrgFullscreenMode
lrg_graphics_settings_get_fullscreen_mode (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), LRG_FULLSCREEN_WINDOWED);
    return self->fullscreen_mode;
}

void
lrg_graphics_settings_set_fullscreen_mode (LrgGraphicsSettings *self,
                                           LrgFullscreenMode    mode)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    if (self->fullscreen_mode != mode)
    {
        self->fullscreen_mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FULLSCREEN_MODE]);
        emit_changed (self, "fullscreen-mode");
    }
}

gboolean
lrg_graphics_settings_get_vsync (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), FALSE);
    return self->vsync;
}

void
lrg_graphics_settings_set_vsync (LrgGraphicsSettings *self,
                                 gboolean             vsync)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    vsync = !!vsync;

    if (self->vsync != vsync)
    {
        self->vsync = vsync;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VSYNC]);
        emit_changed (self, "vsync");
    }
}

gint
lrg_graphics_settings_get_fps_limit (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), 0);
    return self->fps_limit;
}

void
lrg_graphics_settings_set_fps_limit (LrgGraphicsSettings *self,
                                     gint                 fps_limit)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    fps_limit = CLAMP (fps_limit, 0, 300);

    if (self->fps_limit != fps_limit)
    {
        self->fps_limit = fps_limit;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FPS_LIMIT]);
        emit_changed (self, "fps-limit");
    }
}

LrgQualityPreset
lrg_graphics_settings_get_quality_preset (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), LRG_QUALITY_MEDIUM);
    return self->quality_preset;
}

void
lrg_graphics_settings_set_quality_preset (LrgGraphicsSettings *self,
                                          LrgQualityPreset     preset)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    if (self->quality_preset != preset)
    {
        self->quality_preset = preset;
        apply_quality_preset_internal (self, preset);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "quality-preset");
    }
}

LrgAntiAliasMode
lrg_graphics_settings_get_anti_aliasing (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), LRG_AA_NONE);
    return self->anti_aliasing;
}

void
lrg_graphics_settings_set_anti_aliasing (LrgGraphicsSettings *self,
                                         LrgAntiAliasMode     mode)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    if (self->anti_aliasing != mode)
    {
        self->anti_aliasing = mode;
        self->quality_preset = LRG_QUALITY_CUSTOM;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANTI_ALIASING]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "anti-aliasing");
    }
}

gint
lrg_graphics_settings_get_texture_quality (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), 0);
    return self->texture_quality;
}

void
lrg_graphics_settings_set_texture_quality (LrgGraphicsSettings *self,
                                           gint                 quality)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    quality = CLAMP (quality, 0, 3);

    if (self->texture_quality != quality)
    {
        self->texture_quality = quality;
        self->quality_preset = LRG_QUALITY_CUSTOM;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE_QUALITY]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "texture-quality");
    }
}

gint
lrg_graphics_settings_get_shadow_quality (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), 0);
    return self->shadow_quality;
}

void
lrg_graphics_settings_set_shadow_quality (LrgGraphicsSettings *self,
                                          gint                 quality)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    quality = CLAMP (quality, 0, 3);

    if (self->shadow_quality != quality)
    {
        self->shadow_quality = quality;
        self->quality_preset = LRG_QUALITY_CUSTOM;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_QUALITY]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "shadow-quality");
    }
}

gboolean
lrg_graphics_settings_get_bloom_enabled (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), FALSE);
    return self->bloom_enabled;
}

void
lrg_graphics_settings_set_bloom_enabled (LrgGraphicsSettings *self,
                                         gboolean             enabled)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    enabled = !!enabled;

    if (self->bloom_enabled != enabled)
    {
        self->bloom_enabled = enabled;
        self->quality_preset = LRG_QUALITY_CUSTOM;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLOOM_ENABLED]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "bloom-enabled");
    }
}

gboolean
lrg_graphics_settings_get_motion_blur_enabled (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), FALSE);
    return self->motion_blur_enabled;
}

void
lrg_graphics_settings_set_motion_blur_enabled (LrgGraphicsSettings *self,
                                               gboolean             enabled)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    enabled = !!enabled;

    if (self->motion_blur_enabled != enabled)
    {
        self->motion_blur_enabled = enabled;
        self->quality_preset = LRG_QUALITY_CUSTOM;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOTION_BLUR_ENABLED]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "motion-blur-enabled");
    }
}

gboolean
lrg_graphics_settings_get_ambient_occlusion_enabled (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), FALSE);
    return self->ambient_occlusion_enabled;
}

void
lrg_graphics_settings_set_ambient_occlusion_enabled (LrgGraphicsSettings *self,
                                                     gboolean             enabled)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    enabled = !!enabled;

    if (self->ambient_occlusion_enabled != enabled)
    {
        self->ambient_occlusion_enabled = enabled;
        self->quality_preset = LRG_QUALITY_CUSTOM;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AMBIENT_OCCLUSION_ENABLED]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "ambient-occlusion-enabled");
    }
}

gdouble
lrg_graphics_settings_get_view_distance (LrgGraphicsSettings *self)
{
    g_return_val_if_fail (LRG_IS_GRAPHICS_SETTINGS (self), 1.0);
    return self->view_distance;
}

void
lrg_graphics_settings_set_view_distance (LrgGraphicsSettings *self,
                                         gdouble              distance)
{
    g_return_if_fail (LRG_IS_GRAPHICS_SETTINGS (self));

    distance = CLAMP (distance, 0.5, 2.0);

    if (self->view_distance != distance)
    {
        self->view_distance = distance;
        self->quality_preset = LRG_QUALITY_CUSTOM;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIEW_DISTANCE]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUALITY_PRESET]);
        emit_changed (self, "view-distance");
    }
}
