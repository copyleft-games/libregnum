# Settings System

The Settings module provides a comprehensive system for managing game settings with YAML persistence, automatic serialization, and a modular group-based architecture.

## Overview

- **LrgSettings**: Main settings container managing all settings groups
- **LrgSettingsGroup**: Abstract base class for settings groups
- **LrgGraphicsSettings**: Graphics settings (resolution, vsync, fps limit, etc.)
- **LrgAudioSettings**: Audio settings (volume levels, mute)
- **LrgAccessibilitySettings**: Accessibility options (see [Accessibility](../accessibility/index.md))

## Architecture

```
LrgSettings (container)
├── LrgGraphicsSettings (group_name: "graphics")
├── LrgAudioSettings (group_name: "audio")
├── LrgAccessibilitySettings (group_name: "accessibility")
└── Custom groups via lrg_settings_add_group()
```

## Basic Usage

```c
#include <libregnum.h>

/* Get or create settings */
LrgSettings *settings = lrg_settings_get_default ();

/* Access graphics settings */
LrgGraphicsSettings *graphics = lrg_settings_get_graphics (settings);
lrg_graphics_settings_set_resolution (graphics, 1920, 1080);
lrg_graphics_settings_set_vsync (graphics, TRUE);
lrg_graphics_settings_set_fps_limit (graphics, 60);

/* Access audio settings */
LrgAudioSettings *audio = lrg_settings_get_audio (settings);
lrg_audio_settings_set_master_volume (audio, 0.8f);
lrg_audio_settings_set_music_volume (audio, 0.6f);

/* Save to file */
g_autoptr(GError) error = NULL;
if (!lrg_settings_save (settings, "settings.yaml", &error))
{
    g_warning ("Failed to save settings: %s", error->message);
}
```

## Persistence

Settings are saved to YAML format:

```yaml
graphics:
  resolution-width: 1920
  resolution-height: 1080
  fullscreen-mode: borderless
  vsync: true
  fps-limit: 60

audio:
  master-volume: 0.8
  music-volume: 0.6
  sfx-volume: 1.0
  mute: false

accessibility:
  colorblind-mode: none
  ui-scale: 1.0
  subtitles-enabled: false
```

### Default Path

Use the default XDG config path:

```c
/* Loads from ~/.config/my-game/settings.yaml */
lrg_settings_load_default_path (settings, "my-game", &error);

/* Saves to ~/.config/my-game/settings.yaml */
lrg_settings_save_default_path (settings, "my-game", &error);
```

## Custom Settings Groups

Create custom settings groups by subclassing `LrgSettingsGroup`:

```c
#define MY_TYPE_GAMEPLAY_SETTINGS (my_gameplay_settings_get_type ())
G_DECLARE_FINAL_TYPE (MyGameplaySettings, my_gameplay_settings, MY, GAMEPLAY_SETTINGS, LrgSettingsGroup)

struct _MyGameplaySettings
{
    LrgSettingsGroup parent_instance;

    gint   difficulty;
    gfloat camera_sensitivity;
    gboolean auto_save;
};

/* Implement virtual methods */
static void
my_gameplay_settings_apply (LrgSettingsGroup *group)
{
    MyGameplaySettings *self = MY_GAMEPLAY_SETTINGS (group);
    /* Apply difficulty, camera settings, etc. */
}

static void
my_gameplay_settings_reset (LrgSettingsGroup *group)
{
    MyGameplaySettings *self = MY_GAMEPLAY_SETTINGS (group);
    self->difficulty = 1;  /* Normal */
    self->camera_sensitivity = 1.0f;
    self->auto_save = TRUE;
}

static const gchar *
my_gameplay_settings_get_group_name (LrgSettingsGroup *group)
{
    return "gameplay";
}

static void
my_gameplay_settings_class_init (MyGameplaySettingsClass *klass)
{
    LrgSettingsGroupClass *group_class = LRG_SETTINGS_GROUP_CLASS (klass);

    group_class->apply = my_gameplay_settings_apply;
    group_class->reset = my_gameplay_settings_reset;
    group_class->get_group_name = my_gameplay_settings_get_group_name;
    group_class->serialize = my_gameplay_settings_serialize;
    group_class->deserialize = my_gameplay_settings_deserialize;
}

/* Register with LrgSettings */
MyGameplaySettings *gameplay = my_gameplay_settings_new ();
lrg_settings_add_group (settings, LRG_SETTINGS_GROUP (gameplay));
```

## API Reference

### LrgSettings

| Method | Description |
|--------|-------------|
| `lrg_settings_new()` | Create new settings container |
| `lrg_settings_get_default()` | Get singleton instance |
| `lrg_settings_get_graphics()` | Get graphics settings group |
| `lrg_settings_get_audio()` | Get audio settings group |
| `lrg_settings_get_group()` | Get group by name |
| `lrg_settings_add_group()` | Add custom settings group |
| `lrg_settings_list_groups()` | List all group names |
| `lrg_settings_load()` | Load from YAML file |
| `lrg_settings_save()` | Save to YAML file |
| `lrg_settings_apply_all()` | Apply all groups to engine |
| `lrg_settings_reset_all()` | Reset all groups to defaults |
| `lrg_settings_is_dirty()` | Check if any group modified |

### LrgSettingsGroup

| Method | Description |
|--------|-------------|
| `lrg_settings_group_apply()` | Apply settings to engine |
| `lrg_settings_group_reset()` | Reset to defaults |
| `lrg_settings_group_get_group_name()` | Get serialization name |
| `lrg_settings_group_serialize()` | Serialize to GVariant |
| `lrg_settings_group_deserialize()` | Deserialize from GVariant |
| `lrg_settings_group_is_dirty()` | Check if modified |
| `lrg_settings_group_mark_dirty()` | Mark as modified |
| `lrg_settings_group_mark_clean()` | Mark as saved |

### LrgGraphicsSettings

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `resolution-width` | int | 1920 | Screen width |
| `resolution-height` | int | 1080 | Screen height |
| `fullscreen-mode` | enum | WINDOWED | Window mode |
| `vsync` | bool | TRUE | Vertical sync |
| `fps-limit` | int | 0 | FPS cap (0 = unlimited) |

### LrgAudioSettings

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `master-volume` | float | 1.0 | Master volume (0.0-1.0) |
| `music-volume` | float | 1.0 | Music volume |
| `sfx-volume` | float | 1.0 | Sound effects volume |
| `voice-volume` | float | 1.0 | Voice/dialog volume |
| `mute` | bool | FALSE | Mute all audio |
