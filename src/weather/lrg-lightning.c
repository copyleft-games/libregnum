/* lrg-lightning.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lightning weather effect implementation.
 */

#include "lrg-lightning.h"
#include "../lrg-log.h"
#include <math.h>
#include <stdlib.h>

struct _LrgLightning
{
    LrgWeatherEffect parent_instance;

    gfloat   min_interval;
    gfloat   max_interval;
    gfloat   flash_duration;
    guint    flash_count;
    gfloat   flash_intensity;

    gboolean thunder_enabled;
    gfloat   thunder_delay;

    gboolean bolts_enabled;

    guint8   color_r, color_g, color_b;

    /* Internal state */
    gfloat   time_to_next_flash;
    gfloat   current_flash_time;
    guint    flashes_remaining;
    gboolean flashing;
    gfloat   thunder_countdown;
};

G_DEFINE_FINAL_TYPE (LrgLightning, lrg_lightning, LRG_TYPE_WEATHER_EFFECT)

enum
{
    PROP_0,
    PROP_MIN_INTERVAL,
    PROP_MAX_INTERVAL,
    PROP_FLASH_DURATION,
    PROP_FLASH_COUNT,
    PROP_FLASH_INTENSITY,
    PROP_THUNDER_ENABLED,
    PROP_THUNDER_DELAY,
    PROP_BOLTS_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_FLASH_STARTED,
    SIGNAL_FLASH_ENDED,
    SIGNAL_THUNDER,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static gfloat
random_interval (LrgLightning *self)
{
    gfloat range = self->max_interval - self->min_interval;
    return self->min_interval + (gfloat)rand () / RAND_MAX * range;
}

static void
lrg_lightning_activate (LrgWeatherEffect *effect)
{
    LrgLightning *self = LRG_LIGHTNING (effect);
    self->time_to_next_flash = random_interval (self);
    self->flashing = FALSE;
    self->flashes_remaining = 0;
    self->thunder_countdown = -1.0f;
    LRG_WEATHER_EFFECT_CLASS (lrg_lightning_parent_class)->activate (effect);
}

static void
lrg_lightning_update (LrgWeatherEffect *effect,
                      gfloat            delta_time)
{
    LrgLightning *self = LRG_LIGHTNING (effect);
    gfloat intensity;

    LRG_WEATHER_EFFECT_CLASS (lrg_lightning_parent_class)->update (effect, delta_time);

    if (!lrg_weather_effect_is_active (effect))
        return;

    intensity = lrg_weather_effect_get_intensity (effect);

    /* Update thunder countdown */
    if (self->thunder_countdown > 0.0f)
    {
        self->thunder_countdown -= delta_time;
        if (self->thunder_countdown <= 0.0f)
        {
            g_signal_emit (self, signals[SIGNAL_THUNDER], 0);
            self->thunder_countdown = -1.0f;
        }
    }

    /* Handle ongoing flash */
    if (self->flashing)
    {
        self->current_flash_time -= delta_time;
        if (self->current_flash_time <= 0.0f)
        {
            self->flashes_remaining--;
            if (self->flashes_remaining > 0)
            {
                self->current_flash_time = self->flash_duration;
            }
            else
            {
                self->flashing = FALSE;
                g_signal_emit (self, signals[SIGNAL_FLASH_ENDED], 0);
                self->time_to_next_flash = random_interval (self);
            }
        }
        return;
    }

    /* Check for next flash */
    self->time_to_next_flash -= delta_time;
    if (self->time_to_next_flash <= 0.0f && intensity > 0.0f)
    {
        /* Higher intensity = more flashes */
        guint extra_flashes = (guint)(intensity * 2);
        self->flashes_remaining = self->flash_count + extra_flashes;
        self->current_flash_time = self->flash_duration;
        self->flashing = TRUE;

        g_signal_emit (self, signals[SIGNAL_FLASH_STARTED], 0);

        if (self->thunder_enabled)
        {
            self->thunder_countdown = self->thunder_delay + (gfloat)rand () / RAND_MAX * 2.0f;
        }
    }
}

static void
lrg_lightning_render (LrgWeatherEffect *effect)
{
    LrgLightning *self = LRG_LIGHTNING (effect);

    if (!lrg_weather_effect_is_active (effect) || !self->flashing)
        return;

    /*
     * Flash rendering:
     * - Draw screen overlay with flash color and intensity
     * - Optionally draw bolt geometry if bolts_enabled
     *
     * gfloat alpha = self->flash_intensity * (self->current_flash_time / self->flash_duration);
     * GrlColor color = { self->color_r, self->color_g, self->color_b, (guint8)(alpha * 255) };
     * grl_draw_rectangle (0, 0, screen_width, screen_height, &color);
     */
}

static void
lrg_lightning_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgLightning *self = LRG_LIGHTNING (object);
    switch (prop_id)
    {
    case PROP_MIN_INTERVAL: g_value_set_float (value, self->min_interval); break;
    case PROP_MAX_INTERVAL: g_value_set_float (value, self->max_interval); break;
    case PROP_FLASH_DURATION: g_value_set_float (value, self->flash_duration); break;
    case PROP_FLASH_COUNT: g_value_set_uint (value, self->flash_count); break;
    case PROP_FLASH_INTENSITY: g_value_set_float (value, self->flash_intensity); break;
    case PROP_THUNDER_ENABLED: g_value_set_boolean (value, self->thunder_enabled); break;
    case PROP_THUNDER_DELAY: g_value_set_float (value, self->thunder_delay); break;
    case PROP_BOLTS_ENABLED: g_value_set_boolean (value, self->bolts_enabled); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_lightning_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgLightning *self = LRG_LIGHTNING (object);
    switch (prop_id)
    {
    case PROP_MIN_INTERVAL: self->min_interval = g_value_get_float (value); break;
    case PROP_MAX_INTERVAL: self->max_interval = g_value_get_float (value); break;
    case PROP_FLASH_DURATION: self->flash_duration = g_value_get_float (value); break;
    case PROP_FLASH_COUNT: self->flash_count = g_value_get_uint (value); break;
    case PROP_FLASH_INTENSITY: self->flash_intensity = g_value_get_float (value); break;
    case PROP_THUNDER_ENABLED: self->thunder_enabled = g_value_get_boolean (value); break;
    case PROP_THUNDER_DELAY: self->thunder_delay = g_value_get_float (value); break;
    case PROP_BOLTS_ENABLED: self->bolts_enabled = g_value_get_boolean (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_lightning_class_init (LrgLightningClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWeatherEffectClass *effect_class = LRG_WEATHER_EFFECT_CLASS (klass);

    object_class->get_property = lrg_lightning_get_property;
    object_class->set_property = lrg_lightning_set_property;

    effect_class->activate = lrg_lightning_activate;
    effect_class->update = lrg_lightning_update;
    effect_class->render = lrg_lightning_render;

    properties[PROP_MIN_INTERVAL] = g_param_spec_float ("min-interval", "Min Interval", "Min seconds between flashes", 1, 300, 5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_MAX_INTERVAL] = g_param_spec_float ("max-interval", "Max Interval", "Max seconds between flashes", 1, 300, 30, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLASH_DURATION] = g_param_spec_float ("flash-duration", "Flash Duration", "Duration of each flash", 0.01f, 1, 0.1f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLASH_COUNT] = g_param_spec_uint ("flash-count", "Flash Count", "Number of flashes per strike", 1, 10, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLASH_INTENSITY] = g_param_spec_float ("flash-intensity", "Flash Intensity", "Flash brightness", 0, 1, 0.8f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_THUNDER_ENABLED] = g_param_spec_boolean ("thunder-enabled", "Thunder Enabled", "Enable thunder sound", TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_THUNDER_DELAY] = g_param_spec_float ("thunder-delay", "Thunder Delay", "Delay before thunder sound", 0, 30, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_BOLTS_ENABLED] = g_param_spec_boolean ("bolts-enabled", "Bolts Enabled", "Render lightning bolts", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_FLASH_STARTED] = g_signal_new ("flash-started", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_FLASH_ENDED] = g_signal_new ("flash-ended", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_THUNDER] = g_signal_new ("thunder", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_lightning_init (LrgLightning *self)
{
    self->min_interval = 5.0f;
    self->max_interval = 30.0f;
    self->flash_duration = 0.1f;
    self->flash_count = 2;
    self->flash_intensity = 0.8f;
    self->thunder_enabled = TRUE;
    self->thunder_delay = 1.0f;
    self->bolts_enabled = FALSE;
    self->color_r = 255; self->color_g = 255; self->color_b = 255;
    self->time_to_next_flash = 10.0f;
    self->flashing = FALSE;
    self->flashes_remaining = 0;
    self->thunder_countdown = -1.0f;
}

/* Public API */

LrgLightning * lrg_lightning_new (void) { return g_object_new (LRG_TYPE_LIGHTNING, NULL); }

gfloat lrg_lightning_get_min_interval (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), 0); return self->min_interval; }
void lrg_lightning_set_min_interval (LrgLightning *self, gfloat seconds) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->min_interval = seconds; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_INTERVAL]); }

gfloat lrg_lightning_get_max_interval (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), 0); return self->max_interval; }
void lrg_lightning_set_max_interval (LrgLightning *self, gfloat seconds) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->max_interval = seconds; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_INTERVAL]); }

gfloat lrg_lightning_get_flash_duration (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), 0); return self->flash_duration; }
void lrg_lightning_set_flash_duration (LrgLightning *self, gfloat duration) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->flash_duration = duration; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLASH_DURATION]); }

guint lrg_lightning_get_flash_count (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), 0); return self->flash_count; }
void lrg_lightning_set_flash_count (LrgLightning *self, guint count) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->flash_count = count; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLASH_COUNT]); }

gfloat lrg_lightning_get_flash_intensity (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), 0); return self->flash_intensity; }
void lrg_lightning_set_flash_intensity (LrgLightning *self, gfloat intensity) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->flash_intensity = CLAMP (intensity, 0, 1); g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLASH_INTENSITY]); }

gboolean lrg_lightning_get_thunder_enabled (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), FALSE); return self->thunder_enabled; }
void lrg_lightning_set_thunder_enabled (LrgLightning *self, gboolean enabled) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->thunder_enabled = enabled; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THUNDER_ENABLED]); }

gfloat lrg_lightning_get_thunder_delay (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), 0); return self->thunder_delay; }
void lrg_lightning_set_thunder_delay (LrgLightning *self, gfloat delay) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->thunder_delay = delay; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THUNDER_DELAY]); }

gboolean lrg_lightning_get_bolts_enabled (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), FALSE); return self->bolts_enabled; }
void lrg_lightning_set_bolts_enabled (LrgLightning *self, gboolean enabled) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->bolts_enabled = enabled; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOLTS_ENABLED]); }

void lrg_lightning_trigger_flash (LrgLightning *self) { g_return_if_fail (LRG_IS_LIGHTNING (self)); if (!self->flashing) { self->flashes_remaining = self->flash_count; self->current_flash_time = self->flash_duration; self->flashing = TRUE; g_signal_emit (self, signals[SIGNAL_FLASH_STARTED], 0); if (self->thunder_enabled) self->thunder_countdown = self->thunder_delay; } }

gboolean lrg_lightning_is_flashing (LrgLightning *self) { g_return_val_if_fail (LRG_IS_LIGHTNING (self), FALSE); return self->flashing; }

void lrg_lightning_get_color (LrgLightning *self, guint8 *r, guint8 *g, guint8 *b) { g_return_if_fail (LRG_IS_LIGHTNING (self)); if (r) *r = self->color_r; if (g) *g = self->color_g; if (b) *b = self->color_b; }
void lrg_lightning_set_color (LrgLightning *self, guint8 r, guint8 g, guint8 b) { g_return_if_fail (LRG_IS_LIGHTNING (self)); self->color_r = r; self->color_g = g; self->color_b = b; }
