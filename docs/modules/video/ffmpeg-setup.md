# FFmpeg Setup

The video module requires FFmpeg for decoding. This guide covers installation and configuration.

## Required Libraries

| Library | Purpose |
|---------|---------|
| `libavformat` | Container parsing (MP4, WebM, etc.) |
| `libavcodec` | Video/audio decoding |
| `libavutil` | Utility functions |
| `libswscale` | Pixel format conversion |
| `libswresample` | Audio resampling |

## Installation

### Fedora

```bash
sudo dnf install ffmpeg-libs ffmpeg-devel
```

### Ubuntu/Debian

```bash
sudo apt install libavformat-dev libavcodec-dev libavutil-dev \
                 libswscale-dev libswresample-dev
```

### Arch Linux

```bash
sudo pacman -S ffmpeg
```

### Building from Source

```bash
git clone https://git.ffmpeg.org/ffmpeg.git
cd ffmpeg
./configure --enable-shared --enable-gpl --enable-libx264 --enable-libvpx
make -j$(nproc)
sudo make install
```

## Codec Support

### Common Codecs

| Codec | Library | Notes |
|-------|---------|-------|
| H.264 | libx264 | Most common video codec |
| H.265/HEVC | libx265 | Newer, better compression |
| VP8/VP9 | libvpx | WebM format |
| AAC | Native | Common audio codec |
| MP3 | Native | Audio codec |
| Vorbis | libvorbis | OGG audio |
| Opus | libopus | Modern audio codec |

### Check Available Codecs

```bash
ffmpeg -codecs | grep -E "(h264|hevc|vp[89]|aac|mp3)"
```

## Build Configuration

### pkg-config

Libregnum uses pkg-config to find FFmpeg:

```makefile
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil libswscale libswresample)
FFMPEG_LIBS := $(shell pkg-config --libs libavformat libavcodec libavutil libswscale libswresample)
```

### Verifying Installation

```bash
pkg-config --modversion libavformat
pkg-config --modversion libavcodec
```

## Optional: Hardware Acceleration

### VAAPI (Linux/Intel/AMD)

```bash
# Fedora
sudo dnf install ffmpeg-libs libva-devel

# Check VAAPI support
vainfo
```

### NVENC/NVDEC (NVIDIA)

```bash
# Install NVIDIA drivers with CUDA
# FFmpeg must be compiled with --enable-nvenc --enable-cuda
```

### VideoToolbox (macOS)

Built into macOS, no additional setup needed.

## Minimum Versions

| Library | Minimum Version |
|---------|-----------------|
| libavformat | 58.0 |
| libavcodec | 58.0 |
| libavutil | 56.0 |
| libswscale | 5.0 |
| libswresample | 3.0 |

Check versions:

```bash
ffmpeg -version
```

## Troubleshooting

### "Codec not found"

Install the required codec library:

```bash
# H.264 support
sudo dnf install x264-libs

# VP9 support
sudo dnf install libvpx
```

### "Format not recognized"

Ensure container format is supported:

```bash
ffmpeg -formats | grep -i mp4
```

### Performance Issues

1. **Enable hardware acceleration** if available
2. **Use appropriate resolution** - scale down 4K videos if not needed
3. **Pre-decode** - convert videos to a simpler format during asset build

### Audio Not Playing

Check audio device configuration:

```bash
# List audio devices
aplay -l

# Test with FFmpeg directly
ffplay -nodisp audio.mp3
```

## Video Format Recommendations

For games, use these settings for best compatibility:

### Cutscenes

```bash
ffmpeg -i input.mov \
    -c:v libx264 -preset medium -crf 23 \
    -c:a aac -b:a 192k \
    -movflags +faststart \
    output.mp4
```

### Background Videos (Loop)

```bash
ffmpeg -i input.mov \
    -c:v libvpx-vp9 -crf 30 -b:v 0 \
    -c:a libopus -b:a 128k \
    output.webm
```

### Small UI Videos

```bash
ffmpeg -i input.mov \
    -c:v libx264 -preset fast -crf 28 \
    -vf scale=640:-2 \
    -an \
    output.mp4
```

## Conditional Compilation

The video module can be disabled if FFmpeg is not available:

```makefile
ifeq ($(HAVE_FFMPEG),1)
    MODULES += video
    CFLAGS += -DLRG_HAVE_VIDEO
endif
```

In code:

```c
#ifdef LRG_HAVE_VIDEO
#include <libregnum/video/lrg-video-player.h>
#endif
```
