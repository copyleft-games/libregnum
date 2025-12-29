# Steam Integration

The Steam module provides integration with the Steamworks SDK for publishing games on Steam. It wraps the Steam flat C API in GObject types for consistent integration with the rest of Libregnum.

## Overview

Steam integration is **opt-in** and requires building with `STEAM=1`:

```bash
make STEAM=1
```

Without `STEAM=1`, stub implementations are used that allow games to run without Steam.

## Components

| Type | Description |
|------|-------------|
| `LrgSteamService` | Interface for Steam service abstraction |
| `LrgSteamStub` | Stub implementation when Steam unavailable |
| `LrgSteamClient` | Steam initialization and user info |
| `LrgSteamAchievements` | Achievement unlocking |
| `LrgSteamCloud` | Remote save file sync |
| `LrgSteamStats` | Persistent player statistics |
| `LrgSteamPresence` | Rich presence for friends list |

## Prerequisites

### Steamworks SDK

The Steamworks SDK is included as a git submodule:

```bash
git submodule update --init --recursive
```

This pulls from [rlabrecque/SteamworksSDK](https://github.com/rlabrecque/SteamworksSDK), a community mirror of the official SDK.

### Build Requirements

```bash
# Fedora
sudo dnf install glib2-devel

# Build with Steam support
make STEAM=1
```

## Basic Usage

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgSteamClient) client = NULL;

    /* Create the Steam client */
    client = lrg_steam_client_new ();

    /* Initialize Steam with your app ID */
    if (!lrg_steam_service_init (LRG_STEAM_SERVICE (client), 480, &error))
    {
        /* Steam not running or not available */
        g_warning ("Steam init failed: %s", error->message);
        /* Game can still run without Steam features */
    }

    /* Check if Steam is available */
    if (lrg_steam_service_is_available (LRG_STEAM_SERVICE (client)))
    {
        g_print ("Logged in as: %s\n",
                 lrg_steam_client_get_persona_name (client));
    }

    /* In game loop - process Steam callbacks */
    while (running)
    {
        lrg_steam_service_run_callbacks (LRG_STEAM_SERVICE (client));

        /* ... game logic ... */
    }

    /* Shutdown Steam */
    lrg_steam_service_shutdown (LRG_STEAM_SERVICE (client));

    return 0;
}
```

## Achievements

```c
g_autoptr(LrgSteamAchievements) achievements = NULL;

achievements = lrg_steam_achievements_new (client);

/* Unlock an achievement */
if (lrg_steam_achievements_unlock (achievements, "ACH_FIRST_KILL"))
{
    g_print ("Achievement unlocked!\n");
}

/* Check if achievement is unlocked */
if (lrg_steam_achievements_is_unlocked (achievements, "ACH_FIRST_KILL"))
{
    g_print ("Already unlocked\n");
}

/* Store changes to Steam */
lrg_steam_achievements_store (achievements);
```

### Achievement Best Practices

- Define achievements in Steamworks partner portal first
- Use meaningful API names (e.g., `ACH_BEAT_LEVEL_5`)
- Call `store()` periodically or at save points
- Don't unlock achievements too frequently (rate limited by Steam)

## Cloud Saves

```c
g_autoptr(LrgSteamCloud) cloud = NULL;

cloud = lrg_steam_cloud_new (client);

/* Write a save file */
g_autoptr(GBytes) save_data = serialize_game_state ();
if (lrg_steam_cloud_write (cloud, "save1.dat", save_data))
{
    g_print ("Save uploaded to Steam Cloud\n");
}

/* Read a save file */
g_autoptr(GBytes) loaded = lrg_steam_cloud_read (cloud, "save1.dat");
if (loaded != NULL)
{
    deserialize_game_state (loaded);
}

/* Check if file exists */
if (lrg_steam_cloud_exists (cloud, "save1.dat"))
{
    gsize size = lrg_steam_cloud_get_file_size (cloud, "save1.dat");
    g_print ("Save file exists: %zu bytes\n", size);
}

/* Delete a cloud file */
lrg_steam_cloud_delete (cloud, "old_save.dat");
```

### Cloud Storage Limits

Configure cloud storage limits in Steamworks:
- Maximum file size (recommend 10-100 MB)
- Maximum total storage (recommend 100 MB - 1 GB)

## Statistics

```c
g_autoptr(LrgSteamStats) stats = NULL;
gint32 kills;
gfloat playtime;

stats = lrg_steam_stats_new (client);

/* Get stats */
if (lrg_steam_stats_get_int (stats, "STAT_KILLS", &kills))
{
    g_print ("Total kills: %d\n", kills);
}

if (lrg_steam_stats_get_float (stats, "STAT_PLAYTIME", &playtime))
{
    g_print ("Playtime: %.2f hours\n", playtime);
}

/* Set stats */
lrg_steam_stats_set_int (stats, "STAT_KILLS", kills + 1);
lrg_steam_stats_set_float (stats, "STAT_PLAYTIME", playtime + 0.1f);

/* Store changes */
lrg_steam_stats_store (stats);
```

## Rich Presence

Show game status on friends list:

```c
g_autoptr(LrgSteamPresence) presence = NULL;

presence = lrg_steam_presence_new (client);

/* Set simple status */
lrg_steam_presence_set_status (presence, "In Main Menu");

/* Set custom key-value pairs */
lrg_steam_presence_set (presence, "status", "Playing Level 5");
lrg_steam_presence_set (presence, "score", "12345");

/* Clear presence on quit */
lrg_steam_presence_clear (presence);
```

### Localization

Configure localized presence strings in Steamworks partner portal, then use tokens:

```c
lrg_steam_presence_set (presence, "steam_display", "#StatusPlaying");
lrg_steam_presence_set (presence, "level", "5");
```

## Running Without Steam

When `STEAM=0` (default) or Steam isn't running:

| Method | Behavior |
|--------|----------|
| `init()` | Returns TRUE (stub) or FALSE with error (real) |
| `is_available()` | Returns FALSE |
| `run_callbacks()` | No-op |
| Achievement methods | Return FALSE |
| Cloud methods | Return FALSE/NULL |
| Stats methods | Return FALSE (stub returns TRUE for sets) |
| Presence methods | Return TRUE (stub, no-op) |

This allows development and testing without Steam running.

## API Reference

### LrgSteamService (Interface)

| Method | Description |
|--------|-------------|
| `is_available()` | Check if Steam is running |
| `init(app_id)` | Initialize with Steam App ID |
| `shutdown()` | Shutdown Steam API |
| `run_callbacks()` | Process Steam callbacks (call each frame) |

### LrgSteamClient

| Method | Description |
|--------|-------------|
| `is_logged_on()` | Check if user is logged in |
| `get_steam_id()` | Get user's 64-bit Steam ID |
| `get_persona_name()` | Get user's display name |
| `get_app_id()` | Get the app's Steam App ID |

### LrgSteamAchievements

| Method | Description |
|--------|-------------|
| `unlock(name)` | Unlock an achievement |
| `is_unlocked(name)` | Check if achievement unlocked |
| `clear(name)` | Clear an achievement (dev only) |
| `store()` | Upload changes to Steam |
| `get_count()` | Get total achievement count |
| `get_name(index)` | Get achievement API name by index |

### LrgSteamCloud

| Method | Description |
|--------|-------------|
| `write(filename, data)` | Write file to cloud |
| `read(filename)` | Read file from cloud |
| `delete(filename)` | Delete cloud file |
| `exists(filename)` | Check if file exists |
| `get_file_size(filename)` | Get file size in bytes |
| `get_file_count()` | Get number of cloud files |
| `get_file_name(index)` | Get filename by index |

### LrgSteamStats

| Method | Description |
|--------|-------------|
| `get_int(name, out)` | Get integer stat |
| `set_int(name, value)` | Set integer stat |
| `get_float(name, out)` | Get float stat |
| `set_float(name, value)` | Set float stat |
| `store()` | Upload changes to Steam |

### LrgSteamPresence

| Method | Description |
|--------|-------------|
| `set(key, value)` | Set rich presence key-value |
| `set_status(status)` | Set "status" key (convenience) |
| `clear()` | Clear all rich presence |

## Error Codes

### LrgSteamClientError

| Error | Description |
|-------|-------------|
| `NOT_SUPPORTED` | Built without STEAM=1 |
| `NO_STEAM_CLIENT` | Steam client not running |
| `VERSION_MISMATCH` | SDK version mismatch |
| `INIT_FAILED` | Generic initialization failure |

### LrgSteamAchievementsError

| Error | Description |
|-------|-------------|
| `NOT_INITIALIZED` | Steam not initialized |
| `INVALID_NAME` | Achievement doesn't exist |

### LrgSteamCloudError

| Error | Description |
|-------|-------------|
| `NOT_ENABLED` | Cloud not enabled for app |
| `QUOTA_EXCEEDED` | Cloud storage full |
| `WRITE_FAILED` | Failed to write file |
