# Audio Module

The Audio module provides centralized audio management for games, including sound effects and music playback with volume control, crossfading, and sound banking.

## Overview

The Audio module consists of three core classes:

- **LrgAudioManager** - Singleton manager for all game audio
- **LrgSoundBank** - Organized collection of sound effects
- **LrgMusicTrack** - Music with optional custom loop points

## Key Features

- **Sound banking** - Organize sounds by category (UI, player, enemy, etc.)
- **Multiple channels** - Separate volume control for master, SFX, music, voice
- **Music crossfading** - Smooth transitions between tracks
- **Custom loop points** - Set specific loop regions in tracks
- **Manifests** - Load sound banks from YAML files
- **Playback control** - Play, stop, pause, resume, seek
- **Volume management** - Per-channel and global volume control
- **Singleton manager** - Easy access from anywhere in game

## Quick Start

```c
/* Get audio manager */
LrgAudioManager *audio = lrg_audio_manager_get_default();

/* Create sound bank */
g_autoptr(LrgSoundBank) sfx = lrg_sound_bank_new("ui");
lrg_sound_bank_set_base_path(sfx, "assets/sounds/ui/");
lrg_sound_bank_load(sfx, "click", "click.wav", NULL);
lrg_sound_bank_load(sfx, "error", "error.wav", NULL);

/* Add bank to manager */
lrg_audio_manager_add_bank(audio, sfx);

/* Play sound */
lrg_audio_manager_play_sound(audio, "ui", "click");

/* Load and play music */
g_autoptr(LrgMusicTrack) music = lrg_music_track_new_from_file(
    "assets/music/boss_theme.ogg",
    NULL
);
lrg_music_track_set_looping(music, TRUE);
lrg_audio_manager_play_music(audio, music);

/* Control volume */
lrg_audio_manager_set_master_volume(audio, 0.8);
lrg_audio_manager_set_music_volume(audio, 0.6);
lrg_audio_manager_set_sfx_volume(audio, 0.9);

/* In game loop */
lrg_audio_manager_update(audio);
```

## Sound Banking

Organize sounds into logical groups via manifests:

```yaml
# assets/sounds/player.yaml
name: player
base_path: assets/sounds/player/
sounds:
  jump: jump.wav
  land: land.ogg
  hurt: hurt.wav
  pickup: item_pickup.wav
```

Load the bank:

```c
if (lrg_audio_manager_load_bank(audio, "assets/sounds/player.yaml", NULL)) {
    lrg_audio_manager_play_sound(audio, "player", "jump");
}
```

## Music Crossfading

Smooth transitions between tracks:

```c
g_autoptr(LrgMusicTrack) boss_music = lrg_music_track_new_from_file(
    "assets/music/boss_battle.ogg", NULL
);

/* Crossfade over 2 seconds */
lrg_audio_manager_crossfade_to(audio, boss_music, 2.0);
```

## Volume Channels

Control audio independently:

```c
/* Settings menu */
lrg_audio_manager_set_master_volume(audio, master_slider);
lrg_audio_manager_set_music_volume(audio, music_slider);
lrg_audio_manager_set_sfx_volume(audio, sfx_slider);
lrg_audio_manager_set_voice_volume(audio, voice_slider);

/* Pause all audio */
lrg_audio_manager_set_muted(audio, TRUE);
```

## Music Loop Points

Set custom loop regions:

```c
g_autoptr(LrgMusicTrack) track = lrg_music_track_new_from_file(
    "boss_music.ogg", NULL
);

/* Loop from 30 seconds to 120 seconds */
lrg_music_track_set_loop_points(track, 30.0, 120.0);
lrg_music_track_set_looping(track, TRUE);

lrg_audio_manager_play_music(audio, track);
```

## API Reference

See the individual class documentation:

- [LrgAudioManager](audio-manager.md) - Audio system manager
- [LrgSoundBank](sound-bank.md) - Sound effect collection
- [LrgMusicTrack](music-track.md) - Music playback control
