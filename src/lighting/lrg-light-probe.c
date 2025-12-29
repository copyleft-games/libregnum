/* lrg-light-probe.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Light probe implementation.
 */

#include "lrg-light-probe.h"
#include "lrg-light2d.h"
#include "../lrg-log.h"
#include <math.h>

struct _LrgLightProbe
{
    GObject parent_instance;

    gfloat x, y;
    gfloat radius;
    guint8 sampled_r, sampled_g, sampled_b;
    gfloat sampled_intensity;
};

G_DEFINE_FINAL_TYPE (LrgLightProbe, lrg_light_probe, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_X,
    PROP_Y,
    PROP_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_light_probe_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgLightProbe *self = LRG_LIGHT_PROBE (object);
    switch (prop_id)
    {
    case PROP_X: g_value_set_float (value, self->x); break;
    case PROP_Y: g_value_set_float (value, self->y); break;
    case PROP_RADIUS: g_value_set_float (value, self->radius); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_light_probe_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgLightProbe *self = LRG_LIGHT_PROBE (object);
    switch (prop_id)
    {
    case PROP_X: self->x = g_value_get_float (value); break;
    case PROP_Y: self->y = g_value_get_float (value); break;
    case PROP_RADIUS: self->radius = g_value_get_float (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_light_probe_class_init (LrgLightProbeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_light_probe_get_property;
    object_class->set_property = lrg_light_probe_set_property;

    properties[PROP_X] = g_param_spec_float ("x", "X", "X position", -G_MAXFLOAT, G_MAXFLOAT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_Y] = g_param_spec_float ("y", "Y", "Y position", -G_MAXFLOAT, G_MAXFLOAT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_RADIUS] = g_param_spec_float ("radius", "Radius", "Sample radius", 0, G_MAXFLOAT, 50, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_light_probe_init (LrgLightProbe *self)
{
    self->x = 0.0f;
    self->y = 0.0f;
    self->radius = 50.0f;
    self->sampled_r = 0;
    self->sampled_g = 0;
    self->sampled_b = 0;
    self->sampled_intensity = 0.0f;
}

/* Public API */

LrgLightProbe * lrg_light_probe_new (void) { return g_object_new (LRG_TYPE_LIGHT_PROBE, NULL); }

void lrg_light_probe_get_position (LrgLightProbe *self, gfloat *x, gfloat *y) { g_return_if_fail (LRG_IS_LIGHT_PROBE (self)); if (x) *x = self->x; if (y) *y = self->y; }
void lrg_light_probe_set_position (LrgLightProbe *self, gfloat x, gfloat y) { g_return_if_fail (LRG_IS_LIGHT_PROBE (self)); self->x = x; self->y = y; }

gfloat lrg_light_probe_get_radius (LrgLightProbe *self) { g_return_val_if_fail (LRG_IS_LIGHT_PROBE (self), 0); return self->radius; }
void lrg_light_probe_set_radius (LrgLightProbe *self, gfloat radius) { g_return_if_fail (LRG_IS_LIGHT_PROBE (self)); self->radius = radius; }

void lrg_light_probe_get_color (LrgLightProbe *self, guint8 *r, guint8 *g, guint8 *b) { g_return_if_fail (LRG_IS_LIGHT_PROBE (self)); if (r) *r = self->sampled_r; if (g) *g = self->sampled_g; if (b) *b = self->sampled_b; }
gfloat lrg_light_probe_get_intensity (LrgLightProbe *self) { g_return_val_if_fail (LRG_IS_LIGHT_PROBE (self), 0); return self->sampled_intensity; }

void
lrg_light_probe_sample (LrgLightProbe *self,
                        GPtrArray     *lights)
{
    gfloat total_r = 0, total_g = 0, total_b = 0;
    gfloat total_intensity = 0;
    gfloat total_weight = 0;
    guint i;

    g_return_if_fail (LRG_IS_LIGHT_PROBE (self));
    g_return_if_fail (lights != NULL);

    for (i = 0; i < lights->len; i++)
    {
        LrgLight2D *light = g_ptr_array_index (lights, i);
        gfloat lx, ly;
        gfloat dx, dy, dist;
        gfloat weight;
        guint8 lr, lg, lb;
        gfloat intensity;

        if (!lrg_light2d_get_enabled (light))
            continue;

        lrg_light2d_get_position (light, &lx, &ly);

        dx = lx - self->x;
        dy = ly - self->y;
        dist = sqrtf (dx * dx + dy * dy);

        if (dist > self->radius)
            continue;

        weight = 1.0f - (dist / self->radius);
        lrg_light2d_get_color (light, &lr, &lg, &lb);
        intensity = lrg_light2d_get_intensity (light);

        total_r += lr * weight * intensity;
        total_g += lg * weight * intensity;
        total_b += lb * weight * intensity;
        total_intensity += intensity * weight;
        total_weight += weight;
    }

    if (total_weight > 0.0f)
    {
        self->sampled_r = (guint8)CLAMP (total_r / total_weight, 0, 255);
        self->sampled_g = (guint8)CLAMP (total_g / total_weight, 0, 255);
        self->sampled_b = (guint8)CLAMP (total_b / total_weight, 0, 255);
        self->sampled_intensity = total_intensity / total_weight;
    }
    else
    {
        self->sampled_r = 0;
        self->sampled_g = 0;
        self->sampled_b = 0;
        self->sampled_intensity = 0.0f;
    }
}
