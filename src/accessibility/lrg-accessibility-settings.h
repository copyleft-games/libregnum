/* lrg-accessibility-settings.h - Accessibility preferences container
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_ACCESSIBILITY_SETTINGS_H
#define LRG_ACCESSIBILITY_SETTINGS_H

#include <glib-object.h>
#include "../settings/lrg-settings-group.h"
#include "lrg-color-filter.h"
#include "../lrg-enums.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ACCESSIBILITY_SETTINGS (lrg_accessibility_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAccessibilitySettings, lrg_accessibility_settings, LRG, ACCESSIBILITY_SETTINGS, LrgSettingsGroup)

/* LrgColorblindMode is defined in lrg-enums.h */

/**
 * lrg_accessibility_settings_new:
 *
 * Creates a new #LrgAccessibilitySettings with default values.
 *
 * Returns: (transfer full): A new #LrgAccessibilitySettings
 */
LRG_AVAILABLE_IN_ALL
LrgAccessibilitySettings *
lrg_accessibility_settings_new (void);

/* Visual */

/**
 * lrg_accessibility_settings_get_colorblind_mode:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets the colorblind mode.
 *
 * Returns: The current #LrgColorblindMode
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindMode
lrg_accessibility_settings_get_colorblind_mode (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_colorblind_mode:
 * @self: an #LrgAccessibilitySettings
 * @mode: the #LrgColorblindMode to set
 *
 * Sets the colorblind mode.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_colorblind_mode (LrgAccessibilitySettings *self,
                                                LrgColorblindMode         mode);

/**
 * lrg_accessibility_settings_get_high_contrast:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether high contrast mode is enabled.
 *
 * Returns: %TRUE if high contrast is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_high_contrast (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_high_contrast:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable high contrast
 *
 * Sets high contrast mode.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_high_contrast (LrgAccessibilitySettings *self,
                                              gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_ui_scale:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets the UI scale factor (0.5 to 2.0).
 *
 * Returns: The UI scale factor
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_accessibility_settings_get_ui_scale (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_ui_scale:
 * @self: an #LrgAccessibilitySettings
 * @scale: the UI scale factor (0.5 to 2.0)
 *
 * Sets the UI scale factor.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_ui_scale (LrgAccessibilitySettings *self,
                                         gfloat                    scale);

/**
 * lrg_accessibility_settings_get_reduce_motion:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether reduced motion is enabled.
 *
 * Returns: %TRUE if reduced motion is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_reduce_motion (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_reduce_motion:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable reduced motion
 *
 * Sets reduced motion mode for photosensitivity.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_reduce_motion (LrgAccessibilitySettings *self,
                                              gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_screen_shake_intensity:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets the screen shake intensity (0.0 to 1.0).
 *
 * Returns: The screen shake intensity
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_accessibility_settings_get_screen_shake_intensity (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_screen_shake_intensity:
 * @self: an #LrgAccessibilitySettings
 * @intensity: the intensity (0.0 to 1.0)
 *
 * Sets the screen shake intensity.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_screen_shake_intensity (LrgAccessibilitySettings *self,
                                                       gfloat                    intensity);

/* Audio */

/**
 * lrg_accessibility_settings_get_subtitles_enabled:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether subtitles are enabled.
 *
 * Returns: %TRUE if subtitles are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_subtitles_enabled (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_subtitles_enabled:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable subtitles
 *
 * Sets whether subtitles are enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_subtitles_enabled (LrgAccessibilitySettings *self,
                                                  gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_closed_captions:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether closed captions (sound descriptions) are enabled.
 *
 * Returns: %TRUE if closed captions are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_closed_captions (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_closed_captions:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable closed captions
 *
 * Sets whether closed captions are enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_closed_captions (LrgAccessibilitySettings *self,
                                                gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_subtitle_size:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets the subtitle font size multiplier (0.5 to 2.0).
 *
 * Returns: The subtitle size multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_accessibility_settings_get_subtitle_size (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_subtitle_size:
 * @self: an #LrgAccessibilitySettings
 * @size: the size multiplier (0.5 to 2.0)
 *
 * Sets the subtitle font size multiplier.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_subtitle_size (LrgAccessibilitySettings *self,
                                              gfloat                    size);

/**
 * lrg_accessibility_settings_get_subtitle_background:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets the subtitle background opacity (0.0 to 1.0).
 *
 * Returns: The background opacity
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_accessibility_settings_get_subtitle_background (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_subtitle_background:
 * @self: an #LrgAccessibilitySettings
 * @opacity: the background opacity (0.0 to 1.0)
 *
 * Sets the subtitle background opacity.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_subtitle_background (LrgAccessibilitySettings *self,
                                                    gfloat                    opacity);

/**
 * lrg_accessibility_settings_get_visual_audio_cues:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether visual audio cues are enabled.
 *
 * Returns: %TRUE if visual audio cues are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_visual_audio_cues (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_visual_audio_cues:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable visual audio cues
 *
 * Sets whether visual audio cues are enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_visual_audio_cues (LrgAccessibilitySettings *self,
                                                  gboolean                  enabled);

/* Motor */

/**
 * lrg_accessibility_settings_get_hold_to_toggle:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether hold actions become toggle actions.
 *
 * Returns: %TRUE if hold-to-toggle is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_hold_to_toggle (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_hold_to_toggle:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable hold-to-toggle
 *
 * Sets whether hold actions become toggle actions.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_hold_to_toggle (LrgAccessibilitySettings *self,
                                               gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_auto_aim:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether auto-aim is enabled.
 *
 * Returns: %TRUE if auto-aim is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_auto_aim (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_auto_aim:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable auto-aim
 *
 * Sets whether auto-aim is enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_auto_aim (LrgAccessibilitySettings *self,
                                         gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_input_timing_multiplier:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets the input timing window multiplier (1.0 to 3.0).
 *
 * Returns: The timing multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_accessibility_settings_get_input_timing_multiplier (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_input_timing_multiplier:
 * @self: an #LrgAccessibilitySettings
 * @multiplier: the timing multiplier (1.0 to 3.0)
 *
 * Sets the input timing window multiplier.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_input_timing_multiplier (LrgAccessibilitySettings *self,
                                                        gfloat                    multiplier);

/* Cognitive */

/**
 * lrg_accessibility_settings_get_objective_reminders:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether objective reminders are enabled.
 *
 * Returns: %TRUE if objective reminders are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_objective_reminders (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_objective_reminders:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable objective reminders
 *
 * Sets whether objective reminders are enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_objective_reminders (LrgAccessibilitySettings *self,
                                                    gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_skip_cutscenes:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether cutscenes can be skipped.
 *
 * Returns: %TRUE if skip cutscenes is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_skip_cutscenes (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_skip_cutscenes:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable skip cutscenes
 *
 * Sets whether cutscenes can be skipped.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_skip_cutscenes (LrgAccessibilitySettings *self,
                                               gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_pause_during_cutscenes:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether pausing during cutscenes is allowed.
 *
 * Returns: %TRUE if pause during cutscenes is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_pause_during_cutscenes (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_pause_during_cutscenes:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to allow pausing during cutscenes
 *
 * Sets whether pausing during cutscenes is allowed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_pause_during_cutscenes (LrgAccessibilitySettings *self,
                                                       gboolean                  enabled);

/* Screen Reader */

/**
 * lrg_accessibility_settings_get_screen_reader_enabled:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets whether screen reader is enabled.
 *
 * Returns: %TRUE if screen reader is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_accessibility_settings_get_screen_reader_enabled (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_screen_reader_enabled:
 * @self: an #LrgAccessibilitySettings
 * @enabled: whether to enable screen reader
 *
 * Sets whether screen reader is enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_screen_reader_enabled (LrgAccessibilitySettings *self,
                                                      gboolean                  enabled);

/**
 * lrg_accessibility_settings_get_screen_reader_rate:
 * @self: an #LrgAccessibilitySettings
 *
 * Gets the screen reader speech rate (0.5 to 2.0).
 *
 * Returns: The speech rate
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_accessibility_settings_get_screen_reader_rate (LrgAccessibilitySettings *self);

/**
 * lrg_accessibility_settings_set_screen_reader_rate:
 * @self: an #LrgAccessibilitySettings
 * @rate: the speech rate (0.5 to 2.0)
 *
 * Sets the screen reader speech rate.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_accessibility_settings_set_screen_reader_rate (LrgAccessibilitySettings *self,
                                                   gfloat                    rate);

G_END_DECLS

#endif /* LRG_ACCESSIBILITY_SETTINGS_H */
