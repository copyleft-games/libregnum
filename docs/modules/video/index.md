# Video Playback Module

The video module provides FFmpeg-based video playback with subtitle support.

## Overview

```
┌─────────────────────────────────────────────────────────┐
│                    LrgVideoPlayer                       │
│  ┌──────────────┐  ┌────────────────┐  ┌─────────────┐ │
│  │ Video Stream │  │  Audio Stream  │  │  Subtitles  │ │
│  │   (FFmpeg)   │  │   (FFmpeg)     │  │ (SRT/VTT)   │ │
│  └──────┬───────┘  └───────┬────────┘  └──────┬──────┘ │
│         ↓                  ↓                  ↓        │
│  ┌──────────────┐  ┌────────────────┐  ┌─────────────┐ │
│  │ VideoTexture │  │  Audio Output  │  │ Subtitle    │ │
│  │   (RGBA)     │  │                │  │  Renderer   │ │
│  └──────────────┘  └────────────────┘  └─────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## Key Components

| Type | Description |
|------|-------------|
| `LrgVideoPlayer` | Main video player with state management |
| `LrgVideoTexture` | RGBA texture buffer for video frames |
| `LrgVideoSubtitleTrack` | Collection of subtitle cues |
| `LrgVideoSubtitles` | Subtitle rendering |
| `LrgSubtitleCue` | Single subtitle entry (GBoxed) |

## Quick Start

```c
#include <libregnum/video/lrg-video-player.h>

g_autoptr(GError) error = NULL;
g_autoptr(LrgVideoPlayer) player = lrg_video_player_new ();

/* Open video file */
if (!lrg_video_player_open (player, "video/intro.mp4", &error))
{
    g_warning ("Failed to open video: %s", error->message);
    return;
}

/* Load subtitles */
lrg_video_player_load_subtitles (player, "video/intro.srt", NULL);

/* Start playback */
lrg_video_player_play (player);

/* In game loop */
void
update (gfloat delta_time)
{
    lrg_video_player_update (player, delta_time);
}

void
draw (void)
{
    lrg_video_player_draw (player, 0, 0, 800, 600);
}
```

## Video States

```
┌─────────┐  open()   ┌─────────┐  ready   ┌─────────┐
│ STOPPED │ ───────→  │ LOADING │ ───────→ │ STOPPED │
└─────────┘           └─────────┘          └────┬────┘
     ↑                                          │ play()
     │ stop()                                   ↓
     │                ┌─────────┐          ┌─────────┐
     └────────────────│ PAUSED  │ ←───────→│ PLAYING │
                      └─────────┘ pause()  └────┬────┘
                           ↑                    │ end
                           │                    ↓
                      ┌─────────┐          ┌──────────┐
                      │  ERROR  │          │ FINISHED │
                      └─────────┘          └──────────┘
```

## Supported Formats

| Format | Video Codecs | Audio Codecs |
|--------|--------------|--------------|
| MP4 | H.264, H.265 | AAC, MP3 |
| WebM | VP8, VP9 | Vorbis, Opus |
| OGV | Theora | Vorbis |
| AVI | Various | Various |

## Subtitle Formats

| Format | Extension | Notes |
|--------|-----------|-------|
| SubRip | `.srt` | Most common format |
| WebVTT | `.vtt` | Web standard with styling |

## Documentation

- [Video Player](video-player.md) - Playback control and rendering
- [Subtitles](subtitles.md) - Subtitle tracks and rendering
- [FFmpeg Setup](ffmpeg-setup.md) - Dependency installation
