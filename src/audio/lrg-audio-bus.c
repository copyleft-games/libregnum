/* lrg-audio-bus.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The submix bus (see lrg-audio-bus.h and docs/modules/audio/buses.org).
 *
 * A route is a graylib processor on the voice (grl_sound_add_processor()
 * and its siblings) whose data is a BusRoute.  raylib runs it on the
 * device thread while it reads the voice for one device block, possibly
 * in several chunks, before it applies the voice's volume and pan; the
 * processor adds the chunk, times the voice's volume and pan (raylib's
 * pan law) and the route's level, into the bus's input at the route's
 * cursor, and for an insert zeroes the chunk so raylib mixes silence.
 *
 * The device mix (lrg-audio-effect.c) then calls
 * lrg_audio_bus_mix_device_private() once per block, after every voice
 * was read: the input runs through the chain, is added to the device
 * block times the return level and is cleared, and the generation
 * counter moves on, which sends every route's cursor back to the start
 * of the next block.
 *
 * Ownership: graylib owns the BusRoute (its destroy notify frees it);
 * the bus lists its routes without owning them.  A route ends three ways:
 * lrg_audio_bus_unroute() and the bus's dispose remove the processor,
 * and a disposed voice drops it; each runs route_destroy(), which takes
 * the route off the bus's list.  The route holds no reference on the
 * voice: graylib drops the processor while the voice is disposed, before
 * it can be freed.
 *
 * Threads: lock guards the input, the cursors and the route list;
 * raylib (through graylib) is never called with it held.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include <graylib.h>

#include "lrg-audio-bus.h"
#include "lrg-audio-mix-private.h"
#include "../lrg-log.h"

#define BUS_CHANNELS (2)

typedef enum
{
    VOICE_SOUND,
    VOICE_MUSIC,
    VOICE_STREAM
} VoiceKind;

typedef struct
{
    LrgAudioBus   *bus;         /* NULL once the bus let go of it */
    GObject       *voice;       /* borrowed, see the file comment */
    VoiceKind      kind;
    guint          processor;   /* graylib's id */
    LrgAudioRoute  route;
    gfloat         level;
    guint          cursor;      /* frames written into this block */
    guint          generation;  /* the block the cursor belongs to */
} BusRoute;

struct _LrgAudioBus
{
    GObject              parent_instance;

    GMutex               lock;
    LrgAudioEffectChain *chain;
    gfloat               level;
    gfloat              *input;       /* LRG_AUDIO_BUS_MAX_FRAMES x 2 */
    guint                generation;
    GPtrArray           *routes;      /* BusRoute *, borrowed */
    gboolean             attached;
};

G_DEFINE_FINAL_TYPE (LrgAudioBus, lrg_audio_bus, G_TYPE_OBJECT)

G_DEFINE_ENUM_TYPE (LrgAudioRoute, lrg_audio_route,
    G_DEFINE_ENUM_VALUE (LRG_AUDIO_ROUTE_SEND, "send"),
    G_DEFINE_ENUM_VALUE (LRG_AUDIO_ROUTE_INSERT, "insert"))

/* ==========================================================================
 * The device thread
 * ========================================================================== */

/*
 * The voice's left and right gains as raylib applies them after the
 * processors: volume x the sine approximation 0.5 x (3 - x^2) of the pan
 * law on each side's share.
 */
static void
voice_gains (BusRoute *route,
             gfloat   *out_left,
             gfloat   *out_right)
{
    gfloat volume;
    gfloat pan;
    gfloat right;
    gfloat left;

    switch (route->kind)
    {
    case VOICE_SOUND:
        volume = grl_sound_get_volume ((GrlSound *)route->voice);
        pan = grl_sound_get_pan ((GrlSound *)route->voice);
        break;
    case VOICE_MUSIC:
        volume = grl_music_get_volume ((GrlMusic *)route->voice);
        pan = grl_music_get_pan ((GrlMusic *)route->voice);
        break;
    case VOICE_STREAM:
    default:
        volume = grl_audio_stream_get_volume ((GrlAudioStream *)route->voice);
        pan = grl_audio_stream_get_pan ((GrlAudioStream *)route->voice);
        break;
    }
    right = (CLAMP (pan, -1.0f, 1.0f) + 1.0f) * 0.5f;
    left = 1.0f - right;
    *out_left = volume * 0.5f * left * (3.0f - left * left);
    *out_right = volume * 0.5f * right * (3.0f - right * right);
}

/* the graylib processor of one route */
static void
route_process (gfloat   *frames,
               guint     n_frames,
               guint     channels,
               gpointer  user_data)
{
    BusRoute    *route;
    LrgAudioBus *bus;
    gfloat       gain_left;
    gfloat       gain_right;
    gboolean     insert;
    guint        room;
    guint        n;
    guint        f;

    route = (BusRoute *)user_data;
    bus = route->bus;
    if (bus == NULL || channels != BUS_CHANNELS)
        return;

    voice_gains (route, &gain_left, &gain_right);

    g_mutex_lock (&bus->lock);
    gain_left *= route->level;
    gain_right *= route->level;
    insert = route->route == LRG_AUDIO_ROUTE_INSERT;
    if (route->generation != bus->generation)
    {
        route->generation = bus->generation;
        route->cursor = 0;
    }
    room = route->cursor < LRG_AUDIO_BUS_MAX_FRAMES ? LRG_AUDIO_BUS_MAX_FRAMES - route->cursor : 0;
    n = MIN (n_frames, room);
    for (f = 0; f < n; f++)
    {
        gfloat *in;

        in = bus->input + (gsize)(route->cursor + f) * BUS_CHANNELS;
        in[0] += frames[f * BUS_CHANNELS] * gain_left;
        in[1] += frames[f * BUS_CHANNELS + 1] * gain_right;
    }
    route->cursor += n;
    g_mutex_unlock (&bus->lock);

    /* an insert: the voice is heard only through the bus */
    if (insert)
        memset (frames, 0, (gsize)n_frames * BUS_CHANNELS * sizeof (gfloat));
}

/* one block of the bus; the caller holds the bus's lock */
static void
mix_locked (LrgAudioBus *self,
            gfloat      *out,
            guint        n_frames,
            guint        sample_rate)
{
    guint done;

    done = 0;
    while (done < n_frames)
    {
        guint   block;
        gsize   i;
        gfloat  level;
        gfloat *block_out;

        /* blocks past the input's size see no input (and run the chain) */
        block = MIN (n_frames - done, (guint)LRG_AUDIO_BUS_MAX_FRAMES);
        if (done > 0)
            memset (self->input, 0, (gsize)block * BUS_CHANNELS * sizeof (gfloat));
        lrg_audio_effect_chain_process (self->chain, self->input, block, BUS_CHANNELS, sample_rate);

        level = self->level;
        block_out = out + (gsize)done * BUS_CHANNELS;
        for (i = 0; i < (gsize)block * BUS_CHANNELS; i++)
            block_out[i] += self->input[i] * level;
        done += block;
    }

    /* the next block starts empty, every cursor back at its start */
    memset (self->input, 0, (gsize)LRG_AUDIO_BUS_MAX_FRAMES * BUS_CHANNELS * sizeof (gfloat));
    self->generation++;
}

void
lrg_audio_bus_mix_device_private (LrgAudioBus *bus,
                                  gfloat      *out,
                                  guint        n_frames,
                                  guint        sample_rate)
{
    if (out == NULL || n_frames == 0 || sample_rate == 0)
        return;
    g_mutex_lock (&bus->lock);
    mix_locked (bus, out, n_frames, sample_rate);
    g_mutex_unlock (&bus->lock);
}

/* ==========================================================================
 * Routes
 * ========================================================================== */

/* graylib's destroy notify: the route leaves the bus's list */
static void
route_destroy (gpointer user_data)
{
    BusRoute    *route;
    LrgAudioBus *bus;

    route = (BusRoute *)user_data;
    bus = route->bus;
    if (bus != NULL)
    {
        g_mutex_lock (&bus->lock);
        g_ptr_array_remove (bus->routes, route);
        g_mutex_unlock (&bus->lock);
    }
    g_free (route);
}

static gboolean
voice_kind (GObject   *voice,
            VoiceKind *out_kind)
{
    if (GRL_IS_SOUND (voice))
        *out_kind = VOICE_SOUND;
    else if (GRL_IS_MUSIC (voice))
        *out_kind = VOICE_MUSIC;
    else if (GRL_IS_AUDIO_STREAM (voice))
        *out_kind = VOICE_STREAM;
    else
        return FALSE;
    return TRUE;
}

static BusRoute *
find_route (LrgAudioBus *self,
            GObject     *voice)
{
    guint i;

    for (i = 0; i < self->routes->len; i++)
    {
        BusRoute *route;

        route = (BusRoute *)g_ptr_array_index (self->routes, i);
        if (route->voice == voice)
            return route;
    }
    return NULL;
}

/* removes the route's processor from its voice (graylib then frees it) */
static gboolean
drop_processor (BusRoute *route)
{
    switch (route->kind)
    {
    case VOICE_SOUND:
        return grl_sound_remove_processor ((GrlSound *)route->voice, route->processor);
    case VOICE_MUSIC:
        return grl_music_remove_processor ((GrlMusic *)route->voice, route->processor);
    case VOICE_STREAM:
    default:
        return grl_audio_stream_remove_processor ((GrlAudioStream *)route->voice, route->processor);
    }
}

gboolean
lrg_audio_bus_route (LrgAudioBus   *self,
                     GObject       *voice,
                     LrgAudioRoute  route,
                     gfloat         level)
{
    BusRoute *entry;
    VoiceKind kind;
    guint     id;

    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), FALSE);
    g_return_val_if_fail (G_IS_OBJECT (voice), FALSE);

    if (!voice_kind (voice, &kind))
        return FALSE;
    level = CLAMP (level, 0.0f, LRG_AUDIO_BUS_MAX_LEVEL);

    /* already here: a new kind and level, read by the next block */
    g_mutex_lock (&self->lock);
    entry = find_route (self, voice);
    if (entry != NULL)
    {
        entry->route = route;
        entry->level = level;
        g_mutex_unlock (&self->lock);
        return TRUE;
    }
    g_mutex_unlock (&self->lock);

    entry = g_new0 (BusRoute, 1);
    entry->bus = self;
    entry->voice = voice;
    entry->kind = kind;
    entry->route = route;
    entry->level = level;

    /* listed before graylib can run it (the processor takes the lock) */
    g_mutex_lock (&self->lock);
    g_ptr_array_add (self->routes, entry);
    g_mutex_unlock (&self->lock);

    switch (kind)
    {
    case VOICE_SOUND:
        id = grl_sound_add_processor ((GrlSound *)voice, route_process, entry, route_destroy);
        break;
    case VOICE_MUSIC:
        id = grl_music_add_processor ((GrlMusic *)voice, route_process, entry, route_destroy);
        break;
    case VOICE_STREAM:
    default:
        id = grl_audio_stream_add_processor ((GrlAudioStream *)voice, route_process, entry, route_destroy);
        break;
    }
    /* no slot: graylib already ran route_destroy */
    if (id == 0)
        return FALSE;
    entry->processor = id;
    return TRUE;
}

gboolean
lrg_audio_bus_unroute (LrgAudioBus *self,
                       GObject     *voice)
{
    BusRoute *entry;

    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), FALSE);

    g_mutex_lock (&self->lock);
    entry = voice != NULL ? find_route (self, voice) : NULL;
    g_mutex_unlock (&self->lock);
    if (entry == NULL)
        return FALSE;

    if (!drop_processor (entry))
    {
        /* graylib no longer knew it: forget it here */
        g_mutex_lock (&self->lock);
        g_ptr_array_remove (self->routes, entry);
        entry->bus = NULL;
        g_mutex_unlock (&self->lock);
    }
    return TRUE;
}

gboolean
lrg_audio_bus_is_routed (LrgAudioBus *self,
                         GObject     *voice)
{
    gboolean routed;

    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), FALSE);

    g_mutex_lock (&self->lock);
    routed = voice != NULL && find_route (self, voice) != NULL;
    g_mutex_unlock (&self->lock);
    return routed;
}

gfloat
lrg_audio_bus_get_route_level (LrgAudioBus   *self,
                               GObject       *voice,
                               LrgAudioRoute *out_route)
{
    BusRoute *entry;
    gfloat    level;

    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), -1.0f);

    g_mutex_lock (&self->lock);
    entry = voice != NULL ? find_route (self, voice) : NULL;
    level = entry != NULL ? entry->level : -1.0f;
    if (entry != NULL && out_route != NULL)
        *out_route = entry->route;
    g_mutex_unlock (&self->lock);
    return level;
}

guint
lrg_audio_bus_get_n_routes (LrgAudioBus *self)
{
    guint n;

    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), 0);

    g_mutex_lock (&self->lock);
    n = self->routes->len;
    g_mutex_unlock (&self->lock);
    return n;
}

/* ==========================================================================
 * The bus
 * ========================================================================== */

static void
lrg_audio_bus_dispose (GObject *object)
{
    LrgAudioBus *self = LRG_AUDIO_BUS (object);

    lrg_audio_bus_detach_mix (self);

    /* every route's processor off its voice (each run takes it off the list) */
    for (;;)
    {
        BusRoute *entry;

        g_mutex_lock (&self->lock);
        entry = self->routes->len > 0 ? g_ptr_array_index (self->routes, self->routes->len - 1) : NULL;
        g_mutex_unlock (&self->lock);
        if (entry == NULL)
            break;
        if (!drop_processor (entry))
        {
            g_mutex_lock (&self->lock);
            g_ptr_array_remove (self->routes, entry);
            entry->bus = NULL;
            g_mutex_unlock (&self->lock);
        }
    }

    G_OBJECT_CLASS (lrg_audio_bus_parent_class)->dispose (object);
}

static void
lrg_audio_bus_finalize (GObject *object)
{
    LrgAudioBus *self = LRG_AUDIO_BUS (object);

    g_clear_object (&self->chain);
    g_ptr_array_unref (self->routes);
    g_free (self->input);
    g_mutex_clear (&self->lock);

    G_OBJECT_CLASS (lrg_audio_bus_parent_class)->finalize (object);
}

static void
lrg_audio_bus_class_init (LrgAudioBusClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_audio_bus_dispose;
    object_class->finalize = lrg_audio_bus_finalize;
}

static void
lrg_audio_bus_init (LrgAudioBus *self)
{
    g_mutex_init (&self->lock);
    self->chain = lrg_audio_effect_chain_new ();
    self->level = 1.0f;
    self->input = g_new0 (gfloat, (gsize)LRG_AUDIO_BUS_MAX_FRAMES * BUS_CHANNELS);
    self->routes = g_ptr_array_new ();
}

LrgAudioBus *
lrg_audio_bus_new (void)
{
    return g_object_new (LRG_TYPE_AUDIO_BUS, NULL);
}

LrgAudioEffectChain *
lrg_audio_bus_get_chain (LrgAudioBus *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), NULL);

    return self->chain;
}

void
lrg_audio_bus_add_effect (LrgAudioBus    *self,
                          LrgAudioEffect *effect)
{
    g_return_if_fail (LRG_IS_AUDIO_BUS (self));

    lrg_audio_effect_chain_add (self->chain, effect);
}

void
lrg_audio_bus_set_level (LrgAudioBus *self,
                         gfloat       level)
{
    g_return_if_fail (LRG_IS_AUDIO_BUS (self));

    self->level = CLAMP (level, 0.0f, LRG_AUDIO_BUS_MAX_LEVEL);
}

gfloat
lrg_audio_bus_get_level (LrgAudioBus *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), 0.0f);

    return self->level;
}

void
lrg_audio_bus_mix (LrgAudioBus *self,
                   gfloat      *out,
                   guint        n_frames,
                   guint        sample_rate)
{
    g_return_if_fail (LRG_IS_AUDIO_BUS (self));
    g_return_if_fail (out != NULL || n_frames == 0);

    lrg_audio_bus_mix_device_private (self, out, n_frames, sample_rate);
}

void
lrg_audio_bus_reset (LrgAudioBus *self)
{
    g_return_if_fail (LRG_IS_AUDIO_BUS (self));

    g_mutex_lock (&self->lock);
    memset (self->input, 0, (gsize)LRG_AUDIO_BUS_MAX_FRAMES * BUS_CHANNELS * sizeof (gfloat));
    self->generation++;
    lrg_audio_effect_chain_reset (self->chain);
    g_mutex_unlock (&self->lock);
}

gboolean
lrg_audio_bus_attach_mix (LrgAudioBus *self,
                          guint        sample_rate)
{
    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), FALSE);

    if (!lrg_audio_mix_add_bus_private (self, sample_rate))
        return FALSE;
    g_mutex_lock (&self->lock);
    self->attached = TRUE;
    g_mutex_unlock (&self->lock);
    return TRUE;
}

void
lrg_audio_bus_detach_mix (LrgAudioBus *self)
{
    g_return_if_fail (LRG_IS_AUDIO_BUS (self));

    lrg_audio_mix_remove_bus_private (self);
    g_mutex_lock (&self->lock);
    self->attached = FALSE;
    g_mutex_unlock (&self->lock);
}

gboolean
lrg_audio_bus_is_attached (LrgAudioBus *self)
{
    gboolean attached;

    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), FALSE);

    g_mutex_lock (&self->lock);
    attached = self->attached;
    g_mutex_unlock (&self->lock);
    return attached;
}

guint
lrg_audio_bus_get_mix_rate (LrgAudioBus *self,
                            gboolean    *out_measured)
{
    g_return_val_if_fail (LRG_IS_AUDIO_BUS (self), 0);

    if (!lrg_audio_bus_is_attached (self))
    {
        if (out_measured != NULL)
            *out_measured = FALSE;
        return 0;
    }
    return lrg_audio_mix_get_rate_private (out_measured);
}
