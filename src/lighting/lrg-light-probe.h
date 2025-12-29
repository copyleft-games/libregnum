/* lrg-light-probe.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Light probe for sampling lighting at a point.
 *
 * Samples ambient lighting from nearby lights for objects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIGHT_PROBE (lrg_light_probe_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightProbe, lrg_light_probe, LRG, LIGHT_PROBE, GObject)

LRG_AVAILABLE_IN_ALL
LrgLightProbe * lrg_light_probe_new (void);

LRG_AVAILABLE_IN_ALL
void lrg_light_probe_get_position (LrgLightProbe *self, gfloat *x, gfloat *y);

LRG_AVAILABLE_IN_ALL
void lrg_light_probe_set_position (LrgLightProbe *self, gfloat x, gfloat y);

LRG_AVAILABLE_IN_ALL
gfloat lrg_light_probe_get_radius (LrgLightProbe *self);

LRG_AVAILABLE_IN_ALL
void lrg_light_probe_set_radius (LrgLightProbe *self, gfloat radius);

LRG_AVAILABLE_IN_ALL
void lrg_light_probe_get_color (LrgLightProbe *self, guint8 *r, guint8 *g, guint8 *b);

LRG_AVAILABLE_IN_ALL
gfloat lrg_light_probe_get_intensity (LrgLightProbe *self);

LRG_AVAILABLE_IN_ALL
void lrg_light_probe_sample (LrgLightProbe *self, GPtrArray *lights);

G_END_DECLS
