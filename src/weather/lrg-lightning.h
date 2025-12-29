/* lrg-lightning.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lightning weather effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-weather-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIGHTNING (lrg_lightning_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightning, lrg_lightning, LRG, LIGHTNING, LrgWeatherEffect)

/**
 * lrg_lightning_new:
 *
 * Creates a new lightning effect.
 *
 * Returns: (transfer full): A new #LrgLightning
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLightning *      lrg_lightning_new                   (void);

/* Timing */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_min_interval      (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_min_interval      (LrgLightning *self,
                                                         gfloat        seconds);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_max_interval      (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_max_interval      (LrgLightning *self,
                                                         gfloat        seconds);

/* Flash properties */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_flash_duration    (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_flash_duration    (LrgLightning *self,
                                                         gfloat        duration);

LRG_AVAILABLE_IN_ALL
guint               lrg_lightning_get_flash_count       (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_flash_count       (LrgLightning *self,
                                                         guint         count);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_flash_intensity   (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_flash_intensity   (LrgLightning *self,
                                                         gfloat        intensity);

/* Thunder */

LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_get_thunder_enabled   (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_thunder_enabled   (LrgLightning *self,
                                                         gboolean      enabled);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_thunder_delay     (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_thunder_delay     (LrgLightning *self,
                                                         gfloat        delay);

/* Bolt rendering */

LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_get_bolts_enabled     (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_bolts_enabled     (LrgLightning *self,
                                                         gboolean      enabled);

/* Manual trigger */

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_trigger_flash         (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_is_flashing           (LrgLightning *self);

/* Color */

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_get_color             (LrgLightning *self,
                                                         guint8       *r,
                                                         guint8       *g,
                                                         guint8       *b);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_color             (LrgLightning *self,
                                                         guint8        r,
                                                         guint8        g,
                                                         guint8        b);

G_END_DECLS
