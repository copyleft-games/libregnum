/* lrg-audio-effect.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The abstract DSP effect and the effect chain (see lrg-audio-effect.h
 * and docs/modules/audio/effects.org).
 *
 * The chain's hook into the device mix is raylib's mixed-audio processor
 * (AttachAudioMixedProcessor): a plain C callback without user data, run
 * on miniaudio's thread with raudio's own lock held.  So there is one
 * process-wide slot (mix_chain) guarded by mix_lock, and raudio is only
 * ever called with mix_lock released: the callback takes raudio's lock
 * first and mix_lock second, attaching in the other order would
 * deadlock.
 *
 * The same callback mixes the attached submix buses (lrg-audio-bus.c)
 * into the device output before the chain runs, so the processor is on
 * raylib's list while a chain or any bus is attached (sync_processor()).
 * Lock order on the device thread: raudio's lock, mix_lock, a bus's
 * lock, a chain's lock.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include <raylib.h>

#include "lrg-audio-effect.h"
#include "lrg-audio-bus.h"
#include "lrg-audio-mix-private.h"
#include "../lrg-log.h"

/* ==========================================================================
 * LrgAudioEffect
 * ========================================================================== */

typedef struct
{
    gboolean enabled;
} LrgAudioEffectPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgAudioEffect, lrg_audio_effect, G_TYPE_OBJECT)

enum
{
    PROP_EFFECT_0,
    PROP_EFFECT_ENABLED,
    N_EFFECT_PROPS
};

static GParamSpec *effect_properties[N_EFFECT_PROPS];

static void
lrg_audio_effect_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgAudioEffect *self = LRG_AUDIO_EFFECT (object);

    switch (prop_id)
    {
    case PROP_EFFECT_ENABLED:
        g_value_set_boolean (value, lrg_audio_effect_get_enabled (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_audio_effect_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgAudioEffect *self = LRG_AUDIO_EFFECT (object);

    switch (prop_id)
    {
    case PROP_EFFECT_ENABLED:
        lrg_audio_effect_set_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* the default reset: an effect without history has nothing to forget */
static void
lrg_audio_effect_real_reset (LrgAudioEffect *self)
{
    (void)self;
}

static void
lrg_audio_effect_class_init (LrgAudioEffectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_audio_effect_get_property;
    object_class->set_property = lrg_audio_effect_set_property;
    klass->reset = lrg_audio_effect_real_reset;

    /**
     * LrgAudioEffect:enabled:
     *
     * Whether the effect changes the samples it is given.
     */
    effect_properties[PROP_EFFECT_ENABLED] =
        g_param_spec_boolean ("enabled", "Enabled", "Whether the effect processes",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_EFFECT_PROPS, effect_properties);
}

static void
lrg_audio_effect_init (LrgAudioEffect *self)
{
    LrgAudioEffectPrivate *priv = lrg_audio_effect_get_instance_private (self);

    priv->enabled = TRUE;
}

/*
 * lrg_audio_effect_process:
 * Validates the layout before the subclass sees it, so no subclass has
 * to bound-check channels or rates itself.
 */
void
lrg_audio_effect_process (LrgAudioEffect *self,
                          gfloat         *frames,
                          guint           n_frames,
                          guint           channels,
                          guint           sample_rate)
{
    LrgAudioEffectPrivate *priv;
    LrgAudioEffectClass   *klass;

    g_return_if_fail (LRG_IS_AUDIO_EFFECT (self));

    priv = lrg_audio_effect_get_instance_private (self);
    klass = LRG_AUDIO_EFFECT_GET_CLASS (self);
    if (!priv->enabled || frames == NULL || n_frames == 0 || klass->process == NULL)
        return;
    if (channels == 0 || channels > LRG_AUDIO_EFFECT_MAX_CHANNELS)
        return;
    if (sample_rate < 8000 || sample_rate > 192000)
        return;
    klass->process (self, frames, n_frames, channels, sample_rate);
}

void
lrg_audio_effect_reset (LrgAudioEffect *self)
{
    g_return_if_fail (LRG_IS_AUDIO_EFFECT (self));

    LRG_AUDIO_EFFECT_GET_CLASS (self)->reset (self);
}

void
lrg_audio_effect_set_enabled (LrgAudioEffect *self,
                              gboolean        enabled)
{
    LrgAudioEffectPrivate *priv;

    g_return_if_fail (LRG_IS_AUDIO_EFFECT (self));

    priv = lrg_audio_effect_get_instance_private (self);
    enabled = !!enabled;
    if (priv->enabled == enabled)
        return;
    priv->enabled = enabled;
    if (!enabled)
        lrg_audio_effect_reset (self);
    g_object_notify_by_pspec (G_OBJECT (self), effect_properties[PROP_EFFECT_ENABLED]);
}

gboolean
lrg_audio_effect_get_enabled (LrgAudioEffect *self)
{
    LrgAudioEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT (self), FALSE);

    priv = lrg_audio_effect_get_instance_private (self);
    return priv->enabled;
}

/* ==========================================================================
 * LrgAudioEffectChain
 * ========================================================================== */

struct _LrgAudioEffectChain
{
    GObject    parent_instance;

    GMutex     lock;      /* guards effects against the audio thread */
    GPtrArray *effects;   /* LrgAudioEffect, owned */
    gboolean   attached;  /* this chain sits in mix_chain */
};

G_DEFINE_FINAL_TYPE (LrgAudioEffectChain, lrg_audio_effect_chain, G_TYPE_OBJECT)

/* the device mix: one chain at a time (raylib's callback has no user data) */
static GMutex               mix_lock;
static LrgAudioEffectChain *mix_chain;      /* borrowed: cleared by detach */
static guint                mix_rate;
static gboolean             mix_processor;  /* our callback is attached */
static GPtrArray           *mix_buses;      /* LrgAudioBus *, borrowed: removed on detach */

/* the measured device rate: raylib opens the device at the system's rate
 * and does not report it, so when the caller did not name a rate the
 * callback counts the frames it is handed against the monotonic clock */
static gboolean             mix_auto;        /* attached with rate 0: measure */
static gboolean             mix_measured;    /* mix_rate came from a measurement */
static gint64               measure_start;   /* us, 0 = no window open */
static guint64              measure_frames;  /* frames since measure_start */
static guint                measure_last;    /* the previous window's snapped rate */

#define MEASURE_WINDOW_US (2 * G_USEC_PER_SEC)

guint
lrg_audio_snap_sample_rate (gdouble measured)
{
    static const guint standard[] = {
        8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000
    };
    guint best = 0;
    gdouble best_error = 1.0e30;
    guint i;

    if (!(measured > 0.0))
        return 0;
    for (i = 0; i < G_N_ELEMENTS (standard); i++)
    {
        gdouble error = fabs (measured - (gdouble)standard[i]) / (gdouble)standard[i];

        if (error < best_error)
        {
            best_error = error;
            best = standard[i];
        }
    }
    /* a rate no standard one is close to is not believed */
    return best_error <= 0.06 ? best : 0;
}

/* measure_block: called with mix_lock held for every device block */
static void
measure_block (guint frames)
{
    gint64 now = g_get_monotonic_time ();

    if (measure_start == 0)
    {
        measure_start = now;
        measure_frames = 0;
        return;
    }
    measure_frames += frames;
    if (now - measure_start >= MEASURE_WINDOW_US)
    {
        guint snapped = lrg_audio_snap_sample_rate ((gdouble)measure_frames * 1.0e6 /
                                                    (gdouble)(now - measure_start));

        /* two windows in a row must agree (a burst of prefilled buffers
         * at the start would overestimate once) */
        if (snapped != 0 && snapped == measure_last && snapped != mix_rate)
        {
            mix_rate = snapped;
            mix_measured = TRUE;
        }
        else if (snapped != 0 && snapped == measure_last)
        {
            mix_measured = TRUE;
        }
        measure_last = snapped;
        measure_start = now;
        measure_frames = 0;
    }
}

/* raylib's mixed-audio processor: frames x 2 floats, the device mix */
static void
mix_process (void         *buffer,
             unsigned int  frames)
{
    guint i;

    g_mutex_lock (&mix_lock);
    if (mix_auto)
        measure_block ((guint)frames);
    /* the buses' returns join the mix first, the chain sees them too */
    if (mix_buses != NULL)
    {
        for (i = 0; i < mix_buses->len; i++)
            lrg_audio_bus_mix_device_private (g_ptr_array_index (mix_buses, i),
                                              (gfloat *)buffer, (guint)frames, mix_rate);
    }
    if (mix_chain != NULL)
        lrg_audio_effect_chain_process (mix_chain, (gfloat *)buffer, (guint)frames, 2, mix_rate);
    g_mutex_unlock (&mix_lock);
}

static void
lrg_audio_effect_chain_dispose (GObject *object)
{
    LrgAudioEffectChain *self = LRG_AUDIO_EFFECT_CHAIN (object);

    lrg_audio_effect_chain_detach_mix (self);
    g_mutex_lock (&self->lock);
    g_ptr_array_set_size (self->effects, 0);
    g_mutex_unlock (&self->lock);

    G_OBJECT_CLASS (lrg_audio_effect_chain_parent_class)->dispose (object);
}

static void
lrg_audio_effect_chain_finalize (GObject *object)
{
    LrgAudioEffectChain *self = LRG_AUDIO_EFFECT_CHAIN (object);

    g_ptr_array_unref (self->effects);
    g_mutex_clear (&self->lock);

    G_OBJECT_CLASS (lrg_audio_effect_chain_parent_class)->finalize (object);
}

static void
lrg_audio_effect_chain_class_init (LrgAudioEffectChainClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_audio_effect_chain_dispose;
    object_class->finalize = lrg_audio_effect_chain_finalize;
}

static void
lrg_audio_effect_chain_init (LrgAudioEffectChain *self)
{
    g_mutex_init (&self->lock);
    self->effects = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgAudioEffectChain *
lrg_audio_effect_chain_new (void)
{
    return g_object_new (LRG_TYPE_AUDIO_EFFECT_CHAIN, NULL);
}

void
lrg_audio_effect_chain_add (LrgAudioEffectChain *self,
                            LrgAudioEffect      *effect)
{
    g_return_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self));
    g_return_if_fail (LRG_IS_AUDIO_EFFECT (effect));

    g_mutex_lock (&self->lock);
    if (!g_ptr_array_find (self->effects, effect, NULL))
        g_ptr_array_add (self->effects, g_object_ref (effect));
    g_mutex_unlock (&self->lock);
}

gboolean
lrg_audio_effect_chain_remove (LrgAudioEffectChain *self,
                               LrgAudioEffect      *effect)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self), FALSE);
    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT (effect), FALSE);

    g_mutex_lock (&self->lock);
    removed = g_ptr_array_remove (self->effects, effect);
    g_mutex_unlock (&self->lock);
    return removed;
}

guint
lrg_audio_effect_chain_get_count (LrgAudioEffectChain *self)
{
    guint count;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self), 0);

    g_mutex_lock (&self->lock);
    count = self->effects->len;
    g_mutex_unlock (&self->lock);
    return count;
}

LrgAudioEffect *
lrg_audio_effect_chain_get_effect (LrgAudioEffectChain *self,
                                   guint                index)
{
    LrgAudioEffect *effect;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self), NULL);

    g_mutex_lock (&self->lock);
    effect = index < self->effects->len ? g_ptr_array_index (self->effects, index) : NULL;
    g_mutex_unlock (&self->lock);
    return effect;
}

void
lrg_audio_effect_chain_process (LrgAudioEffectChain *self,
                                gfloat              *frames,
                                guint                n_frames,
                                guint                channels,
                                guint                sample_rate)
{
    guint i;

    g_return_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self));

    g_mutex_lock (&self->lock);
    for (i = 0; i < self->effects->len; i++)
        lrg_audio_effect_process (g_ptr_array_index (self->effects, i),
                                  frames, n_frames, channels, sample_rate);
    g_mutex_unlock (&self->lock);
}

void
lrg_audio_effect_chain_reset (LrgAudioEffectChain *self)
{
    guint i;

    g_return_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self));

    g_mutex_lock (&self->lock);
    for (i = 0; i < self->effects->len; i++)
        lrg_audio_effect_reset (g_ptr_array_index (self->effects, i));
    g_mutex_unlock (&self->lock);
}

/*
 * sync_processor:
 * Puts raylib's mixed processor on (or off) its list to match whether a
 * chain or a bus needs it; raudio is called with mix_lock released.
 */
static void
sync_processor (void)
{
    gboolean attach;
    gboolean detach;
    gboolean want;

    g_mutex_lock (&mix_lock);
    want = mix_chain != NULL || (mix_buses != NULL && mix_buses->len > 0);
    attach = want && !mix_processor;
    detach = !want && mix_processor;
    mix_processor = want;
    g_mutex_unlock (&mix_lock);

    if (attach)
        AttachAudioMixedProcessor (mix_process);
    /* the device may have closed since (raylib then holds no processor) */
    if (detach && IsAudioDeviceReady ())
        DetachAudioMixedProcessor (mix_process);
}

/*
 * the rate for a new user of the mix: a named rate wins; 0 assumes the
 * default and measures, unless the mix already runs measuring
 */
static void
claim_rate (guint sample_rate)
{
    if (sample_rate != 0)
    {
        mix_auto = FALSE;
        mix_measured = FALSE;
        mix_rate = sample_rate;
        return;
    }
    if (mix_processor && mix_auto)
        return;
    mix_auto = TRUE;
    mix_measured = FALSE;
    measure_start = 0;
    measure_last = 0;
    mix_rate = LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE;
}

gboolean
lrg_audio_mix_add_bus_private (LrgAudioBus *bus,
                               guint        sample_rate)
{
    if (!IsAudioDeviceReady ())
        return FALSE;
    g_mutex_lock (&mix_lock);
    claim_rate (sample_rate);
    if (mix_buses == NULL)
        mix_buses = g_ptr_array_new ();
    if (!g_ptr_array_find (mix_buses, bus, NULL))
        g_ptr_array_add (mix_buses, bus);
    g_mutex_unlock (&mix_lock);
    sync_processor ();
    lrg_debug (LRG_LOG_DOMAIN_AUDIO, "audio bus: on the device mix at %u Hz", mix_rate);
    return TRUE;
}

void
lrg_audio_mix_remove_bus_private (LrgAudioBus *bus)
{
    gboolean removed;

    g_mutex_lock (&mix_lock);
    removed = mix_buses != NULL && g_ptr_array_remove (mix_buses, bus);
    g_mutex_unlock (&mix_lock);
    /* once mix_lock was taken and released the callback no longer runs it */
    if (removed)
        sync_processor ();
}

guint
lrg_audio_mix_get_rate_private (gboolean *out_measured)
{
    guint rate;

    g_mutex_lock (&mix_lock);
    rate = mix_processor ? mix_rate : 0;
    if (out_measured != NULL)
        *out_measured = mix_processor && mix_measured;
    g_mutex_unlock (&mix_lock);
    return rate;
}

/*
 * lrg_audio_effect_chain_attach_mix:
 * raylib's processor list is attached to once (mix_processor) and the
 * slot switched under mix_lock afterwards, with raudio never called
 * while mix_lock is held (see the file comment).
 */
gboolean
lrg_audio_effect_chain_attach_mix (LrgAudioEffectChain *self,
                                   guint                sample_rate)
{
    LrgAudioEffectChain *previous;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self), FALSE);

    if (!IsAudioDeviceReady ())
        return FALSE;

    g_mutex_lock (&mix_lock);
    /* no rate named: assume the default and let the callback measure it */
    mix_auto = sample_rate == 0;
    mix_measured = FALSE;
    measure_start = 0;
    measure_last = 0;
    mix_rate = sample_rate != 0 ? sample_rate : LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE;
    previous = mix_chain;
    mix_chain = self;
    self->attached = TRUE;
    if (previous != NULL && previous != self)
        previous->attached = FALSE;
    g_mutex_unlock (&mix_lock);
    sync_processor ();

    lrg_debug (LRG_LOG_DOMAIN_AUDIO, "effect chain: on the device mix at %u Hz",
               sample_rate != 0 ? sample_rate : LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE);
    return TRUE;
}

void
lrg_audio_effect_chain_detach_mix (LrgAudioEffectChain *self)
{
    gboolean was_slot;

    g_return_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self));

    g_mutex_lock (&mix_lock);
    was_slot = mix_chain == self;
    if (was_slot)
        mix_chain = NULL;
    self->attached = FALSE;
    g_mutex_unlock (&mix_lock);

    if (was_slot)
        sync_processor ();
}

gboolean
lrg_audio_effect_chain_is_attached (LrgAudioEffectChain *self)
{
    gboolean attached;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self), FALSE);

    g_mutex_lock (&mix_lock);
    attached = self->attached;
    g_mutex_unlock (&mix_lock);
    return attached;
}

guint
lrg_audio_effect_chain_get_mix_rate (LrgAudioEffectChain *self,
                                     gboolean            *out_measured)
{
    guint rate;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self), 0);

    g_mutex_lock (&mix_lock);
    rate = self->attached ? mix_rate : 0;
    if (out_measured != NULL)
        *out_measured = self->attached && mix_measured;
    g_mutex_unlock (&mix_lock);
    return rate;
}
