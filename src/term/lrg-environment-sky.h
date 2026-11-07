/* lrg-environment-sky.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "sky" environment: an open-air scene under an #LrgSkyDome (zenith to
 * horizon gradient with a sun that follows the time of day) above a faint
 * ground grid.  The panels are drawn unlit on top, so the sky sets the mood
 * without touching legibility.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../graphics/lrg-sky-dome.h"

G_BEGIN_DECLS

#define LRG_TYPE_ENVIRONMENT_SKY (lrg_environment_sky_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEnvironmentSky, lrg_environment_sky, LRG, ENVIRONMENT_SKY, GObject)

LRG_AVAILABLE_IN_ALL
LrgEnvironmentSky * lrg_environment_sky_new (void);

/**
 * lrg_environment_sky_set_time_of_day:
 * @self: a #LrgEnvironmentSky
 * @hours: time of day in hours, wrapped into [0, 24); defaults to 10.5
 *
 * Moves the sun and blends the sky colours (see
 * lrg_sky_dome_set_time_of_day()).  A host can feed it the wall clock.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_environment_sky_set_time_of_day (LrgEnvironmentSky *self,
                                          gfloat             hours);

LRG_AVAILABLE_IN_ALL
gfloat lrg_environment_sky_get_time_of_day (LrgEnvironmentSky *self);

/**
 * lrg_environment_sky_get_dome:
 * @self: a #LrgEnvironmentSky
 *
 * Returns: (transfer none): the sky dome, for custom keyframes or a horizon
 *   band texture
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSkyDome * lrg_environment_sky_get_dome (LrgEnvironmentSky *self);

G_END_DECLS
