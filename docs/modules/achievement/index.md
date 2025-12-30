# Achievement Module

The Achievement module provides a local achievement system for tracking player accomplishments, with optional Steam sync support.

## Overview

This module includes:

- **LrgAchievementProgress** - Boxed type for tracking progress toward achievements
- **LrgAchievement** - Derivable type for achievement definitions with virtual unlock checking
- **LrgAchievementManager** - Singleton manager implementing `LrgSaveable` for persistence
- **LrgAchievementNotification** - UI popup widget for displaying achievement unlocks

## Key Features

- Derivable `LrgAchievement` allows custom unlock logic via virtual methods
- Manager implements `LrgSaveable` interface for automatic save/load integration
- Local statistics (integer and float) storage for tracking game metrics
- Optional Steam sync via `LrgSteamAchievements` reference
- UI notification widget with configurable duration, fade, and position
- Signals for achievement unlocks and progress updates

## Quick Start

### Basic Usage

```c
/* Get the singleton manager */
LrgAchievementManager *manager = lrg_achievement_manager_get_default ();

/* Create and register achievements */
LrgAchievement *first_blood = lrg_achievement_new ("first_blood", "First Blood");
lrg_achievement_set_description (first_blood, "Defeat your first enemy");
lrg_achievement_set_points (first_blood, 10);
lrg_achievement_manager_register (manager, first_blood);

/* Unlock an achievement */
lrg_achievement_manager_unlock (manager, "first_blood");

/* Track statistics */
lrg_achievement_manager_set_stat_int (manager, "kills", 1);
lrg_achievement_manager_increment_stat (manager, "kills", 1);
```

### Custom Achievement with Unlock Logic

```c
/* Define a custom achievement type */
#define MY_TYPE_KILL_ACHIEVEMENT (my_kill_achievement_get_type ())
G_DECLARE_FINAL_TYPE (MyKillAchievement, my_kill_achievement, MY, KILL_ACHIEVEMENT, LrgAchievement)

struct _MyKillAchievement
{
    LrgAchievement parent_instance;
    gint64 required_kills;
};

G_DEFINE_TYPE (MyKillAchievement, my_kill_achievement, LRG_TYPE_ACHIEVEMENT)

static gboolean
my_kill_achievement_check_unlock (LrgAchievement *achievement)
{
    MyKillAchievement *self = MY_KILL_ACHIEVEMENT (achievement);
    LrgAchievementManager *manager = lrg_achievement_manager_get_default ();
    gint64 kills = lrg_achievement_manager_get_stat_int (manager, "kills");

    return kills >= self->required_kills;
}

static void
my_kill_achievement_class_init (MyKillAchievementClass *klass)
{
    LrgAchievementClass *achievement_class = LRG_ACHIEVEMENT_CLASS (klass);
    achievement_class->check_unlock = my_kill_achievement_check_unlock;
}

/* Usage */
MyKillAchievement *ach = g_object_new (MY_TYPE_KILL_ACHIEVEMENT,
                                        "id", "100_kills",
                                        "name", "Century of Kills",
                                        NULL);
ach->required_kills = 100;
lrg_achievement_manager_register (manager, LRG_ACHIEVEMENT (ach));
```

### Progress Tracking

```c
/* Update progress on an achievement */
lrg_achievement_manager_update_progress (manager, "collect_100_coins", 45, 100);

/* Get progress */
LrgAchievement *ach = lrg_achievement_manager_get (manager, "collect_100_coins");
LrgAchievementProgress *progress = lrg_achievement_get_progress (ach);

g_print ("Progress: %ld/%ld (%.0f%%)\n",
         lrg_achievement_progress_get_current (progress),
         lrg_achievement_progress_get_target (progress),
         lrg_achievement_progress_get_percentage (progress) * 100.0f);
```

### Achievement Notification

```c
/* Create notification widget */
LrgAchievementNotification *notification = lrg_achievement_notification_new ();
lrg_achievement_notification_set_duration (notification, 5.0f);
lrg_achievement_notification_set_position (notification, LRG_NOTIFICATION_POSITION_TOP_RIGHT);

/* Show when achievement unlocks */
g_signal_connect (manager, "achievement-unlocked",
                  G_CALLBACK (on_achievement_unlocked), notification);

static void
on_achievement_unlocked (LrgAchievementManager      *manager,
                         LrgAchievement             *achievement,
                         LrgAchievementNotification *notification)
{
    lrg_achievement_notification_show (notification, achievement);
}

/* Update in game loop */
lrg_achievement_notification_update (notification, delta_time);
```

## Types

### LrgAchievementProgress

Boxed type for tracking progress toward an achievement.

```c
/* Create progress */
LrgAchievementProgress *progress = lrg_achievement_progress_new (current, target);

/* Query progress */
gint64 current = lrg_achievement_progress_get_current (progress);
gint64 target = lrg_achievement_progress_get_target (progress);
gfloat percentage = lrg_achievement_progress_get_percentage (progress);
gboolean complete = lrg_achievement_progress_is_complete (progress);

/* Modify progress */
lrg_achievement_progress_set_current (progress, new_current);

/* Cleanup */
lrg_achievement_progress_free (progress);
```

### LrgAchievement

Derivable type for achievement definitions.

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| `id` | string | Unique identifier |
| `name` | string | Display name |
| `description` | string | Achievement description |
| `hidden` | boolean | Whether hidden until unlocked |
| `points` | int | Point value |
| `unlocked` | boolean | Current unlock status (read-only) |
| `icon` | string | Icon path |

**Virtual Methods:**

| Method | Description |
|--------|-------------|
| `check_unlock` | Override to provide custom unlock logic |
| `on_unlocked` | Called when achievement is unlocked |

**Key Functions:**

```c
LrgAchievement *lrg_achievement_new (const gchar *id, const gchar *name);
void lrg_achievement_unlock (LrgAchievement *self);
gboolean lrg_achievement_check_unlock (LrgAchievement *self);
void lrg_achievement_set_progress (LrgAchievement *self, gint64 current, gint64 target);
LrgAchievementProgress *lrg_achievement_get_progress (LrgAchievement *self);
```

### LrgAchievementManager

Singleton manager for all achievements.

**Signals:**

| Signal | Parameters | Description |
|--------|------------|-------------|
| `achievement-unlocked` | `LrgAchievement *achievement` | Emitted when an achievement is unlocked |
| `achievement-progress` | `LrgAchievement *achievement, LrgAchievementProgress *progress` | Emitted when progress updates |

**Key Functions:**

```c
LrgAchievementManager *lrg_achievement_manager_get_default (void);

/* Registration */
void lrg_achievement_manager_register (LrgAchievementManager *self, LrgAchievement *achievement);
gboolean lrg_achievement_manager_is_registered (LrgAchievementManager *self, const gchar *id);
LrgAchievement *lrg_achievement_manager_get (LrgAchievementManager *self, const gchar *id);

/* Unlock */
void lrg_achievement_manager_unlock (LrgAchievementManager *self, const gchar *id);
void lrg_achievement_manager_update_progress (LrgAchievementManager *self, const gchar *id, gint64 current, gint64 target);

/* Queries */
guint lrg_achievement_manager_get_count (LrgAchievementManager *self);
guint lrg_achievement_manager_get_unlocked_count (LrgAchievementManager *self);
GList *lrg_achievement_manager_get_all (LrgAchievementManager *self);
GList *lrg_achievement_manager_get_unlocked (LrgAchievementManager *self);

/* Statistics */
void lrg_achievement_manager_set_stat_int (LrgAchievementManager *self, const gchar *name, gint64 value);
gint64 lrg_achievement_manager_get_stat_int (LrgAchievementManager *self, const gchar *name);
void lrg_achievement_manager_set_stat_float (LrgAchievementManager *self, const gchar *name, gfloat value);
gfloat lrg_achievement_manager_get_stat_float (LrgAchievementManager *self, const gchar *name);
void lrg_achievement_manager_increment_stat (LrgAchievementManager *self, const gchar *name, gint64 amount);

/* Reset */
void lrg_achievement_manager_reset (LrgAchievementManager *self);
```

### LrgAchievementNotification

UI popup widget for displaying achievement unlocks. Extends `LrgPanel`.

**Properties:**

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `duration` | float | 5.0 | Display duration in seconds |
| `fade-duration` | float | 0.5 | Fade in/out duration |
| `position` | LrgNotificationPosition | TOP_RIGHT | Screen position |
| `margin` | int | 20 | Margin from screen edge |

**Key Functions:**

```c
LrgAchievementNotification *lrg_achievement_notification_new (void);
void lrg_achievement_notification_show (LrgAchievementNotification *self, LrgAchievement *achievement);
void lrg_achievement_notification_hide (LrgAchievementNotification *self);
gboolean lrg_achievement_notification_is_visible (LrgAchievementNotification *self);
void lrg_achievement_notification_update (LrgAchievementNotification *self, gfloat delta);
```

## Notification Positions

```c
typedef enum {
    LRG_NOTIFICATION_POSITION_TOP_LEFT,
    LRG_NOTIFICATION_POSITION_TOP_CENTER,
    LRG_NOTIFICATION_POSITION_TOP_RIGHT,
    LRG_NOTIFICATION_POSITION_BOTTOM_LEFT,
    LRG_NOTIFICATION_POSITION_BOTTOM_CENTER,
    LRG_NOTIFICATION_POSITION_BOTTOM_RIGHT
} LrgNotificationPosition;
```

## Save System Integration

The `LrgAchievementManager` implements `LrgSaveable`, allowing automatic integration with the save system:

```c
/* The manager is automatically saved/loaded with the save system */
LrgSaveManager *save_manager = lrg_save_manager_get_default ();
lrg_save_manager_register_saveable (save_manager,
                                     LRG_SAVEABLE (lrg_achievement_manager_get_default ()),
                                     "achievements");
```

The saved data includes:
- Achievement unlock states and times
- Statistics (int and float values)
- Progress for tracked achievements

## Steam Integration

The manager can optionally sync with Steam achievements:

```c
/* Set Steam achievements reference for sync */
LrgSteamAchievements *steam = lrg_steam_service_get_achievements (steam_service);
lrg_achievement_manager_set_steam_achievements (manager, steam);

/* Achievements will automatically sync to Steam when unlocked */
```

## Error Handling

```c
#define LRG_ACHIEVEMENT_ERROR (lrg_achievement_error_quark ())

typedef enum {
    LRG_ACHIEVEMENT_ERROR_NOT_FOUND,
    LRG_ACHIEVEMENT_ERROR_ALREADY_UNLOCKED,
    LRG_ACHIEVEMENT_ERROR_SAVE_FAILED,
    LRG_ACHIEVEMENT_ERROR_LOAD_FAILED
} LrgAchievementError;
```

## Thread Safety

The `LrgAchievementManager` is NOT thread-safe. All access should be from the main thread. If you need to unlock achievements from worker threads, use `g_idle_add()` to schedule the unlock on the main thread.

## Best Practices

1. **Register achievements early** - Register all achievements during game initialization
2. **Use custom types for complex unlock logic** - Subclass `LrgAchievement` for achievements that depend on multiple conditions
3. **Track statistics** - Use the built-in stat system rather than querying game state directly
4. **Handle notification visibility** - Make sure to call `update()` each frame and add the notification widget to your UI hierarchy
5. **Reset on new game** - Call `reset()` when starting a new game (but not when loading a save)
