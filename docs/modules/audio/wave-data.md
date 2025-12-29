---
title: LrgWaveData
module: audio
type: Class
parent: GObject
since: 1.0
---

# LrgWaveData

GObject wrapper for raw audio wave data with manipulation support.

> **[Home](../../index.md)** > **[Modules](../index.md)** > **[Audio](index.md)** > LrgWaveData

## Overview

LrgWaveData wraps GrlWave to provide a GObject interface for working with raw audio data. It supports loading from files, creating procedural audio, and manipulating samples.

```c
/* Create procedural wave (1 second, 44100 Hz, mono) */
g_autoptr(LrgWaveData) wave = lrg_wave_data_new_procedural (44100, 1, 1.0f);

/* Get samples and modify them */
gsize count;
gfloat *samples = lrg_wave_data_get_samples (wave, &count);
for (gsize i = 0; i < count; i++) {
    samples[i] = sinf (2.0f * G_PI * 440.0f * i / 44100.0f);
}
lrg_wave_data_set_samples (wave, samples, count);
g_free (samples);

/* Convert to playable sound */
g_autoptr(GrlSound) sound = lrg_wave_data_to_sound (wave);
```

## Construction

### From File

```c
g_autoptr(GError) error = NULL;
g_autoptr(LrgWaveData) wave = lrg_wave_data_new_from_file ("sound.wav", &error);
if (wave == NULL) {
    g_warning ("Failed: %s", error->message);
}
```

### From Memory

```c
guint8 *data = /* audio file data */;
gsize size = /* data size */;

g_autoptr(LrgWaveData) wave = lrg_wave_data_new_from_memory (
    ".wav", data, size, &error
);
```

### From Raw Samples

```c
/* Create from raw PCM samples */
guint8 *pcm_data = /* raw PCM */;
gsize pcm_size = /* size in bytes */;

g_autoptr(LrgWaveData) wave = lrg_wave_data_new_from_samples (
    44100,  /* sample rate */
    16,     /* bits per sample */
    1,      /* channels */
    pcm_data,
    pcm_size
);
```

### Procedural (Empty)

```c
/* Create empty wave for procedural generation */
g_autoptr(LrgWaveData) wave = lrg_wave_data_new_procedural (
    44100,  /* sample rate */
    2,      /* channels (stereo) */
    2.0f    /* duration in seconds */
);
```

## Key Methods

### Properties (Read-Only)

| Method | Returns | Description |
|--------|---------|-------------|
| `lrg_wave_data_get_frame_count()` | `guint` | Number of audio frames |
| `lrg_wave_data_get_sample_rate()` | `guint` | Sample rate (Hz) |
| `lrg_wave_data_get_sample_size()` | `guint` | Bits per sample |
| `lrg_wave_data_get_channels()` | `guint` | Channel count |
| `lrg_wave_data_get_duration()` | `gfloat` | Duration in seconds |
| `lrg_wave_data_is_valid()` | `gboolean` | Wave has valid data |

### Sample Access

```c
/* Get normalized float samples (-1.0 to 1.0) */
gsize count;
gfloat *samples = lrg_wave_data_get_samples (wave, &count);

/* Modify and set back */
lrg_wave_data_set_samples (wave, samples, count);
g_free (samples);
```

### Manipulation (Non-Destructive)

All manipulation methods return new LrgWaveData instances:

```c
/* Crop audio */
g_autoptr(LrgWaveData) cropped = lrg_wave_data_crop (wave, 0.5f, 2.0f);

/* Resample to different rate */
g_autoptr(LrgWaveData) resampled = lrg_wave_data_resample (wave, 22050);

/* Convert format */
g_autoptr(LrgWaveData) converted = lrg_wave_data_convert (
    wave,
    48000,  /* new sample rate */
    16,     /* new sample size */
    2       /* new channel count */
);

/* Copy */
g_autoptr(LrgWaveData) copy = lrg_wave_data_copy (wave);
```

### Conversion

```c
/* Convert to GrlSound for playback */
g_autoptr(GrlSound) sound = lrg_wave_data_to_sound (wave);

/* Get underlying GrlWave (transfer none) */
GrlWave *grl_wave = lrg_wave_data_get_grl_wave (wave);
```

### Export

```c
/* Export to WAV file */
if (!lrg_wave_data_export_wav (wave, "output.wav", &error)) {
    g_warning ("Export failed: %s", error->message);
}
```

## Example: Procedural Sound Effect

```c
#include <libregnum.h>
#include <math.h>

static LrgWaveData *
create_beep (gfloat frequency, gfloat duration)
{
    guint sample_rate = 44100;
    g_autoptr(LrgWaveData) wave = lrg_wave_data_new_procedural (
        sample_rate, 1, duration
    );

    gsize count;
    gfloat *samples = lrg_wave_data_get_samples (wave, &count);

    for (gsize i = 0; i < count; i++) {
        gfloat t = (gfloat)i / sample_rate;
        gfloat envelope = 1.0f - (t / duration);  /* Fade out */
        samples[i] = sinf (2.0f * G_PI * frequency * t) * envelope;
    }

    lrg_wave_data_set_samples (wave, samples, count);
    g_free (samples);

    return g_steal_pointer (&wave);
}

int main (void)
{
    /* Create 440 Hz beep for 0.5 seconds */
    g_autoptr(LrgWaveData) beep = create_beep (440.0f, 0.5f);

    /* Add to sound bank */
    g_autoptr(LrgSoundBank) bank = lrg_sound_bank_new ("effects");
    lrg_sound_bank_add_from_wave (bank, "beep", beep);

    /* Play it */
    lrg_sound_bank_play (bank, "beep");

    return 0;
}
```

## Use with LrgSoundBank

```c
g_autoptr(LrgWaveData) wave = lrg_wave_data_new_from_file ("beep.wav", NULL);

/* Add to sound bank */
g_autoptr(LrgSoundBank) bank = lrg_sound_bank_new ("sfx");
lrg_sound_bank_add_from_wave (bank, "beep", wave);

/* Play via bank */
lrg_sound_bank_play (bank, "beep");
```

## See Also

- [LrgSoundBank](sound-bank.md) - Sound effect collection
- [LrgProceduralAudio](procedural-audio.md) - Real-time audio generation
- [LrgAudioManager](audio-manager.md) - Audio system manager
