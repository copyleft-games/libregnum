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
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>

#include <raylib.h>

#include "lrg-audio-effect.h"
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

/* raylib's mixed-audio processor: frames x 2 floats, the device mix */
static void
mix_process (void         *buffer,
             unsigned int  frames)
{
    g_mutex_lock (&mix_lock);
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
    gboolean             need_processor;

    g_return_val_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self), FALSE);

    if (!IsAudioDeviceReady ())
        return FALSE;
    if (sample_rate == 0)
        sample_rate = LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE;

    g_mutex_lock (&mix_lock);
    need_processor = !mix_processor;
    mix_processor = TRUE;
    g_mutex_unlock (&mix_lock);
    if (need_processor)
        AttachAudioMixedProcessor (mix_process);

    g_mutex_lock (&mix_lock);
    previous = mix_chain;
    mix_chain = self;
    mix_rate = sample_rate;
    self->attached = TRUE;
    if (previous != NULL && previous != self)
        previous->attached = FALSE;
    g_mutex_unlock (&mix_lock);

    lrg_debug (LRG_LOG_DOMAIN_AUDIO, "effect chain: on the device mix at %u Hz", sample_rate);
    return TRUE;
}

void
lrg_audio_effect_chain_detach_mix (LrgAudioEffectChain *self)
{
    gboolean drop_processor;

    g_return_if_fail (LRG_IS_AUDIO_EFFECT_CHAIN (self));

    g_mutex_lock (&mix_lock);
    drop_processor = FALSE;
    if (mix_chain == self)
    {
        mix_chain = NULL;
        drop_processor = mix_processor;
        mix_processor = FALSE;
    }
    self->attached = FALSE;
    g_mutex_unlock (&mix_lock);

    /* the device may have closed since (raylib then holds no processor) */
    if (drop_processor && IsAudioDeviceReady ())
        DetachAudioMixedProcessor (mix_process);
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
