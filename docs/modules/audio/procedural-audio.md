---
title: LrgProceduralAudio
module: audio
type: Class
parent: GObject
since: 1.0
---

# LrgProceduralAudio

Derivable base class for real-time procedural audio generation.

> **[Home](../../index.md)** > **[Modules](../index.md)** > **[Audio](index.md)** > LrgProceduralAudio

## Overview

LrgProceduralAudio wraps GrlAudioStream to provide real-time audio synthesis. Subclass it and override the `generate()` virtual method to create custom synthesizers, sound effects generators, or audio analyzers.

```c
/* Create custom synthesizer by subclassing */
struct _MySynth {
    LrgProceduralAudio parent_instance;
    gfloat frequency;
    gfloat phase;
};

G_DEFINE_TYPE (MySynth, my_synth, LRG_TYPE_PROCEDURAL_AUDIO)

static void
my_synth_generate (LrgProceduralAudio *audio,
                   gfloat             *buffer,
                   gint                frame_count)
{
    MySynth *self = MY_SYNTH (audio);
    guint sample_rate = lrg_procedural_audio_get_sample_rate (audio);

    for (gint i = 0; i < frame_count; i++) {
        buffer[i] = sinf (self->phase);
        self->phase += 2.0f * G_PI * self->frequency / sample_rate;
    }
}
```

## Creating a Subclass

### Step 1: Define Type

```c
#define MY_TYPE_SYNTH (my_synth_get_type ())
G_DECLARE_FINAL_TYPE (MySynth, my_synth, MY, SYNTH, LrgProceduralAudio)

struct _MySynth {
    LrgProceduralAudio parent_instance;
    gfloat frequency;
    gfloat phase;
};
```

### Step 2: Implement generate()

```c
static void
my_synth_generate (LrgProceduralAudio *audio,
                   gfloat             *buffer,
                   gint                frame_count)
{
    MySynth *self = MY_SYNTH (audio);
    guint sample_rate = lrg_procedural_audio_get_sample_rate (audio);
    gfloat phase_inc = 2.0f * G_PI * self->frequency / sample_rate;

    for (gint i = 0; i < frame_count; i++) {
        buffer[i] = sinf (self->phase);
        self->phase += phase_inc;
        if (self->phase >= 2.0f * G_PI)
            self->phase -= 2.0f * G_PI;
    }
}

static void
my_synth_class_init (MySynthClass *klass)
{
    LrgProceduralAudioClass *audio_class = LRG_PROCEDURAL_AUDIO_CLASS (klass);
    audio_class->generate = my_synth_generate;
}

static void
my_synth_init (MySynth *self)
{
    self->frequency = 440.0f;
    self->phase = 0.0f;
}
```

### Step 3: Constructor

```c
MySynth *
my_synth_new (guint sample_rate, gfloat frequency)
{
    MySynth *self = g_object_new (MY_TYPE_SYNTH,
                                   "sample-rate", sample_rate,
                                   "channels", 1,
                                   NULL);
    self->frequency = frequency;
    return self;
}
```

## Key Methods

### Construction

```c
/* Base class constructor (produces silence) */
g_autoptr(LrgProceduralAudio) audio = lrg_procedural_audio_new (44100, 1);
```

### Playback Control

```c
lrg_procedural_audio_play (audio);    /* Start playback */
lrg_procedural_audio_stop (audio);    /* Stop playback */
lrg_procedural_audio_pause (audio);   /* Pause */
lrg_procedural_audio_resume (audio);  /* Resume from pause */

if (lrg_procedural_audio_is_playing (audio)) {
    /* Audio is active */
}
```

### Update (Required)

Call every frame to keep the audio buffer filled:

```c
/* In game loop */
lrg_procedural_audio_update (audio);
```

### Volume and Effects

```c
lrg_procedural_audio_set_volume (audio, 0.5f);  /* 0.0 to 1.0 */
lrg_procedural_audio_set_pitch (audio, 1.5f);   /* Pitch multiplier */
lrg_procedural_audio_set_pan (audio, -0.5f);    /* -1.0 (left) to 1.0 (right) */
```

### Properties (Read-Only)

```c
guint rate = lrg_procedural_audio_get_sample_rate (audio);
guint channels = lrg_procedural_audio_get_channels (audio);
gfloat volume = lrg_procedural_audio_get_volume (audio);
```

## Using with LrgAudioManager

Register procedural audio sources with the manager for automatic updating:

```c
LrgAudioManager *manager = lrg_audio_manager_get_default ();

/* Create and register */
g_autoptr(MySynth) synth = my_synth_new (44100, 440.0f);
lrg_audio_manager_add_procedural (manager, "lead-synth", LRG_PROCEDURAL_AUDIO (synth));

/* Play via manager */
lrg_audio_manager_play_procedural (manager, "lead-synth");

/* Manager calls update() automatically in lrg_audio_manager_update() */

/* Stop and remove when done */
lrg_audio_manager_stop_procedural (manager, "lead-synth");
lrg_audio_manager_remove_procedural (manager, "lead-synth");
```

## Example: Complete Synthesizer

```c
#include <libregnum.h>
#include <math.h>

/* ADSR Envelope */
typedef struct {
    gfloat attack;
    gfloat decay;
    gfloat sustain;
    gfloat release;
    gfloat time;
    gboolean released;
} Envelope;

/* Simple Synth */
#define MY_TYPE_SIMPLE_SYNTH (my_simple_synth_get_type ())
G_DECLARE_FINAL_TYPE (MySimpleSynth, my_simple_synth, MY, SIMPLE_SYNTH, LrgProceduralAudio)

struct _MySimpleSynth {
    LrgProceduralAudio parent_instance;
    gfloat frequency;
    gfloat phase;
    Envelope env;
};

static gfloat
envelope_value (Envelope *env, guint sample_rate)
{
    gfloat value = 0.0f;
    gfloat dt = 1.0f / sample_rate;

    if (env->time < env->attack) {
        value = env->time / env->attack;
    } else if (env->time < env->attack + env->decay) {
        gfloat t = (env->time - env->attack) / env->decay;
        value = 1.0f - t * (1.0f - env->sustain);
    } else if (!env->released) {
        value = env->sustain;
    } else {
        gfloat release_time = env->time - env->attack - env->decay;
        value = env->sustain * (1.0f - release_time / env->release);
        if (value < 0.0f) value = 0.0f;
    }

    env->time += dt;
    return value;
}

static void
my_simple_synth_generate (LrgProceduralAudio *audio,
                           gfloat             *buffer,
                           gint                frame_count)
{
    MySimpleSynth *self = MY_SIMPLE_SYNTH (audio);
    guint sample_rate = lrg_procedural_audio_get_sample_rate (audio);
    gfloat phase_inc = 2.0f * G_PI * self->frequency / sample_rate;

    for (gint i = 0; i < frame_count; i++) {
        gfloat osc = sinf (self->phase);
        gfloat env = envelope_value (&self->env, sample_rate);
        buffer[i] = osc * env;

        self->phase += phase_inc;
        if (self->phase >= 2.0f * G_PI)
            self->phase -= 2.0f * G_PI;
    }
}

G_DEFINE_TYPE (MySimpleSynth, my_simple_synth, LRG_TYPE_PROCEDURAL_AUDIO)

static void
my_simple_synth_class_init (MySimpleSynthClass *klass)
{
    LRG_PROCEDURAL_AUDIO_CLASS (klass)->generate = my_simple_synth_generate;
}

static void
my_simple_synth_init (MySimpleSynth *self)
{
    self->frequency = 440.0f;
    self->phase = 0.0f;
    self->env = (Envelope){ 0.01f, 0.1f, 0.7f, 0.3f, 0.0f, FALSE };
}

void
my_simple_synth_note_on (MySimpleSynth *self, gfloat frequency)
{
    self->frequency = frequency;
    self->phase = 0.0f;
    self->env.time = 0.0f;
    self->env.released = FALSE;
}

void
my_simple_synth_note_off (MySimpleSynth *self)
{
    self->env.released = TRUE;
}
```

## Stereo Output

For stereo, set channels to 2 and fill interleaved samples:

```c
static void
stereo_generate (LrgProceduralAudio *audio,
                 gfloat             *buffer,
                 gint                frame_count)
{
    for (gint i = 0; i < frame_count; i++) {
        gfloat sample = /* generate sample */;

        /* Interleaved stereo: L, R, L, R, ... */
        buffer[i * 2 + 0] = sample;      /* Left */
        buffer[i * 2 + 1] = sample;      /* Right */
    }
}
```

## See Also

- [LrgWaveData](wave-data.md) - Raw wave data manipulation
- [LrgAudioManager](audio-manager.md) - Audio system manager
- [LrgSoundBank](sound-bank.md) - Sound effect collection
