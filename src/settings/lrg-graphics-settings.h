/* lrg-graphics-settings.h - Graphics settings group
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_GRAPHICS_SETTINGS_H
#define LRG_GRAPHICS_SETTINGS_H

#include "lrg-settings-group.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_GRAPHICS_SETTINGS (lrg_graphics_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGraphicsSettings, lrg_graphics_settings, LRG, GRAPHICS_SETTINGS, LrgSettingsGroup)

/**
 * LrgFullscreenMode:
 * @LRG_FULLSCREEN_WINDOWED: Regular windowed mode
 * @LRG_FULLSCREEN_FULLSCREEN: Exclusive fullscreen
 * @LRG_FULLSCREEN_BORDERLESS: Borderless windowed (fake fullscreen)
 *
 * Display fullscreen modes.
 */
typedef enum
{
    LRG_FULLSCREEN_WINDOWED = 0,
    LRG_FULLSCREEN_FULLSCREEN,
    LRG_FULLSCREEN_BORDERLESS
} LrgFullscreenMode;

/**
 * LrgQualityPreset:
 * @LRG_QUALITY_LOW: Low quality for older hardware
 * @LRG_QUALITY_MEDIUM: Medium quality (balanced)
 * @LRG_QUALITY_HIGH: High quality for modern hardware
 * @LRG_QUALITY_ULTRA: Maximum quality
 * @LRG_QUALITY_CUSTOM: User-defined settings
 *
 * Quality presets for graphics settings.
 */
typedef enum
{
    LRG_QUALITY_LOW = 0,
    LRG_QUALITY_MEDIUM,
    LRG_QUALITY_HIGH,
    LRG_QUALITY_ULTRA,
    LRG_QUALITY_CUSTOM
} LrgQualityPreset;

/**
 * LrgAntiAliasMode:
 * @LRG_AA_NONE: No anti-aliasing
 * @LRG_AA_FXAA: Fast approximate anti-aliasing
 * @LRG_AA_MSAA_2X: 2x multisample anti-aliasing
 * @LRG_AA_MSAA_4X: 4x multisample anti-aliasing
 * @LRG_AA_MSAA_8X: 8x multisample anti-aliasing
 *
 * Anti-aliasing modes.
 */
typedef enum
{
    LRG_AA_NONE = 0,
    LRG_AA_FXAA,
    LRG_AA_MSAA_2X,
    LRG_AA_MSAA_4X,
    LRG_AA_MSAA_8X
} LrgAntiAliasMode;

/**
 * lrg_graphics_settings_new:
 *
 * Creates a new #LrgGraphicsSettings with default values.
 *
 * Returns: (transfer full): A new #LrgGraphicsSettings
 */
LRG_AVAILABLE_IN_ALL
LrgGraphicsSettings *
lrg_graphics_settings_new (void);

/* Resolution */

/**
 * lrg_graphics_settings_get_resolution:
 * @self: an #LrgGraphicsSettings
 * @width: (out): return location for width
 * @height: (out): return location for height
 *
 * Gets the current resolution setting.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_get_resolution (LrgGraphicsSettings *self,
                                      gint                *width,
                                      gint                *height);

/**
 * lrg_graphics_settings_set_resolution:
 * @self: an #LrgGraphicsSettings
 * @width: the width in pixels
 * @height: the height in pixels
 *
 * Sets the resolution.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_resolution (LrgGraphicsSettings *self,
                                      gint                 width,
                                      gint                 height);

/* Fullscreen mode */

/**
 * lrg_graphics_settings_get_fullscreen_mode:
 * @self: an #LrgGraphicsSettings
 *
 * Gets the fullscreen mode.
 *
 * Returns: The current #LrgFullscreenMode
 */
LRG_AVAILABLE_IN_ALL
LrgFullscreenMode
lrg_graphics_settings_get_fullscreen_mode (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_fullscreen_mode:
 * @self: an #LrgGraphicsSettings
 * @mode: the #LrgFullscreenMode to set
 *
 * Sets the fullscreen mode.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_fullscreen_mode (LrgGraphicsSettings *self,
                                           LrgFullscreenMode    mode);

/* VSync */

/**
 * lrg_graphics_settings_get_vsync:
 * @self: an #LrgGraphicsSettings
 *
 * Gets whether VSync is enabled.
 *
 * Returns: %TRUE if VSync is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_graphics_settings_get_vsync (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_vsync:
 * @self: an #LrgGraphicsSettings
 * @vsync: whether to enable VSync
 *
 * Sets VSync enabled/disabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_vsync (LrgGraphicsSettings *self,
                                 gboolean             vsync);

/* FPS Limit */

/**
 * lrg_graphics_settings_get_fps_limit:
 * @self: an #LrgGraphicsSettings
 *
 * Gets the FPS limit (0 = unlimited).
 *
 * Returns: The FPS limit
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_graphics_settings_get_fps_limit (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_fps_limit:
 * @self: an #LrgGraphicsSettings
 * @fps_limit: the FPS limit (0 = unlimited)
 *
 * Sets the FPS limit.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_fps_limit (LrgGraphicsSettings *self,
                                     gint                 fps_limit);

/* Quality Preset */

/**
 * lrg_graphics_settings_get_quality_preset:
 * @self: an #LrgGraphicsSettings
 *
 * Gets the quality preset.
 *
 * Returns: The current #LrgQualityPreset
 */
LRG_AVAILABLE_IN_ALL
LrgQualityPreset
lrg_graphics_settings_get_quality_preset (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_quality_preset:
 * @self: an #LrgGraphicsSettings
 * @preset: the #LrgQualityPreset to set
 *
 * Sets the quality preset. This also updates individual
 * settings to match the preset.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_quality_preset (LrgGraphicsSettings *self,
                                          LrgQualityPreset     preset);

/* Anti-aliasing */

/**
 * lrg_graphics_settings_get_anti_aliasing:
 * @self: an #LrgGraphicsSettings
 *
 * Gets the anti-aliasing mode.
 *
 * Returns: The current #LrgAntiAliasMode
 */
LRG_AVAILABLE_IN_ALL
LrgAntiAliasMode
lrg_graphics_settings_get_anti_aliasing (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_anti_aliasing:
 * @self: an #LrgGraphicsSettings
 * @mode: the #LrgAntiAliasMode to set
 *
 * Sets the anti-aliasing mode.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_anti_aliasing (LrgGraphicsSettings *self,
                                         LrgAntiAliasMode     mode);

/* Texture Quality (0-3) */

/**
 * lrg_graphics_settings_get_texture_quality:
 * @self: an #LrgGraphicsSettings
 *
 * Gets the texture quality level (0 = low, 3 = ultra).
 *
 * Returns: The texture quality level
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_graphics_settings_get_texture_quality (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_texture_quality:
 * @self: an #LrgGraphicsSettings
 * @quality: texture quality level (0-3)
 *
 * Sets the texture quality level.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_texture_quality (LrgGraphicsSettings *self,
                                           gint                 quality);

/* Shadow Quality (0-3) */

/**
 * lrg_graphics_settings_get_shadow_quality:
 * @self: an #LrgGraphicsSettings
 *
 * Gets the shadow quality level (0 = off, 3 = ultra).
 *
 * Returns: The shadow quality level
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_graphics_settings_get_shadow_quality (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_shadow_quality:
 * @self: an #LrgGraphicsSettings
 * @quality: shadow quality level (0-3)
 *
 * Sets the shadow quality level.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_shadow_quality (LrgGraphicsSettings *self,
                                          gint                 quality);

/* Effects */

/**
 * lrg_graphics_settings_get_bloom_enabled:
 * @self: an #LrgGraphicsSettings
 *
 * Gets whether bloom effect is enabled.
 *
 * Returns: %TRUE if bloom is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_graphics_settings_get_bloom_enabled (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_bloom_enabled:
 * @self: an #LrgGraphicsSettings
 * @enabled: whether to enable bloom
 *
 * Sets bloom effect enabled/disabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_bloom_enabled (LrgGraphicsSettings *self,
                                         gboolean             enabled);

/**
 * lrg_graphics_settings_get_motion_blur_enabled:
 * @self: an #LrgGraphicsSettings
 *
 * Gets whether motion blur is enabled.
 *
 * Returns: %TRUE if motion blur is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_graphics_settings_get_motion_blur_enabled (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_motion_blur_enabled:
 * @self: an #LrgGraphicsSettings
 * @enabled: whether to enable motion blur
 *
 * Sets motion blur enabled/disabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_motion_blur_enabled (LrgGraphicsSettings *self,
                                               gboolean             enabled);

/**
 * lrg_graphics_settings_get_ambient_occlusion_enabled:
 * @self: an #LrgGraphicsSettings
 *
 * Gets whether ambient occlusion is enabled.
 *
 * Returns: %TRUE if ambient occlusion is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_graphics_settings_get_ambient_occlusion_enabled (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_ambient_occlusion_enabled:
 * @self: an #LrgGraphicsSettings
 * @enabled: whether to enable ambient occlusion
 *
 * Sets ambient occlusion enabled/disabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_ambient_occlusion_enabled (LrgGraphicsSettings *self,
                                                     gboolean             enabled);

/* View Distance */

/**
 * lrg_graphics_settings_get_view_distance:
 * @self: an #LrgGraphicsSettings
 *
 * Gets the view distance multiplier (0.5 to 2.0).
 *
 * Returns: The view distance multiplier
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_graphics_settings_get_view_distance (LrgGraphicsSettings *self);

/**
 * lrg_graphics_settings_set_view_distance:
 * @self: an #LrgGraphicsSettings
 * @distance: the view distance multiplier (0.5 to 2.0)
 *
 * Sets the view distance multiplier.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_graphics_settings_set_view_distance (LrgGraphicsSettings *self,
                                         gdouble              distance);

G_END_DECLS

#endif /* LRG_GRAPHICS_SETTINGS_H */
