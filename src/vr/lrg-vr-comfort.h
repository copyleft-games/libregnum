/* lrg-vr-comfort.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * VR comfort settings for motion sickness mitigation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_VR_COMFORT_SETTINGS (lrg_vr_comfort_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVRComfortSettings, lrg_vr_comfort_settings, LRG, VR_COMFORT_SETTINGS, GObject)

/**
 * lrg_vr_comfort_settings_new:
 *
 * Creates new VR comfort settings with defaults.
 *
 * Returns: (transfer full): a new #LrgVRComfortSettings
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRComfortSettings * lrg_vr_comfort_settings_new (void);

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
LRG_AVAILABLE_IN_ALL
LrgVRTurnMode lrg_vr_comfort_settings_get_turn_mode (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_turn_mode:
 * @self: a #LrgVRComfortSettings
 * @mode: the #LrgVRTurnMode
 *
 * Sets the turning mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_turn_mode (LrgVRComfortSettings *self,
                                            LrgVRTurnMode         mode);

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
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_settings_get_snap_turn_angle (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_snap_turn_angle:
 * @self: a #LrgVRComfortSettings
 * @angle: snap turn angle in degrees
 *
 * Sets the snap turn angle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_snap_turn_angle (LrgVRComfortSettings *self,
                                                  gfloat                angle);

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
LRG_AVAILABLE_IN_ALL
LrgVRLocomotionMode lrg_vr_comfort_settings_get_locomotion_mode (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_locomotion_mode:
 * @self: a #LrgVRComfortSettings
 * @mode: the #LrgVRLocomotionMode
 *
 * Sets the locomotion mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_locomotion_mode (LrgVRComfortSettings *self,
                                                  LrgVRLocomotionMode   mode);

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
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_comfort_settings_get_vignette_enabled (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_vignette_enabled:
 * @self: a #LrgVRComfortSettings
 * @enabled: whether to enable vignette
 *
 * Sets whether comfort vignette is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_vignette_enabled (LrgVRComfortSettings *self,
                                                   gboolean              enabled);

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
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_settings_get_vignette_intensity (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_vignette_intensity:
 * @self: a #LrgVRComfortSettings
 * @intensity: vignette intensity (0.0 to 1.0)
 *
 * Sets the vignette intensity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_vignette_intensity (LrgVRComfortSettings *self,
                                                     gfloat                intensity);

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
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_settings_get_height_adjustment (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_height_adjustment:
 * @self: a #LrgVRComfortSettings
 * @adjustment: height adjustment in meters
 *
 * Sets the height adjustment offset.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_height_adjustment (LrgVRComfortSettings *self,
                                                    gfloat                adjustment);

G_END_DECLS
