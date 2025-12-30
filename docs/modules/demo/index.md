# Demo Support

The Demo module provides functionality for creating trial/demo versions of games with content gating, time limits, and upgrade detection.

## Overview

The Demo module consists of:

- **LrgDemoGatable**: Interface for content that can be gated in demo mode
- **LrgDemoManager**: Singleton manager for demo mode operations

## Key Features

- **Content Gating**: Restrict access to specific content by ID
- **Time Limits**: Optional demo time limits with configurable warnings
- **Demo Saves**: Mark and track saves created in demo mode
- **Purchase Redirect**: Open purchase URL in system browser
- **Upgrade Detection**: Detect when user upgrades to full version

## Types

### LrgDemoGatable Interface

Interface for objects representing gatable content.

**Methods:**

| Method | Description |
|--------|-------------|
| get_content_id | Returns unique content identifier |
| is_demo_content | Returns TRUE if available in demo |
| get_unlock_message | Returns user-facing unlock message |

### LrgDemoManager

Singleton manager for demo mode functionality.

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| demo-mode | gboolean | Whether demo mode is active |
| time-limit | gfloat | Time limit in seconds (0 = unlimited) |
| time-elapsed | gfloat | Time elapsed in current session |
| time-remaining | gfloat | Time remaining (-1 if no limit) |
| purchase-url | string | URL for purchasing full version |

**Signals:**

| Signal | Parameters | Description |
|--------|------------|-------------|
| demo-ended | reason (LrgDemoEndReason) | Demo session ended |
| time-warning | seconds_remaining (gfloat) | Time is running low |
| content-blocked | content_id, unlock_message | Gated content accessed |

## Enums

### LrgDemoEndReason

Reasons why a demo session ended.

| Value | Description |
|-------|-------------|
| LRG_DEMO_END_REASON_TIME_LIMIT | Demo time limit reached |
| LRG_DEMO_END_REASON_CONTENT_COMPLETE | Demo content completed |
| LRG_DEMO_END_REASON_MANUAL | User manually ended demo |
| LRG_DEMO_END_REASON_UPGRADED | User upgraded to full version |

### LrgDemoError

Error codes for the Demo system.

| Value | Description |
|-------|-------------|
| LRG_DEMO_ERROR_FAILED | Generic demo error |
| LRG_DEMO_ERROR_CONTENT_GATED | Content is gated in demo |
| LRG_DEMO_ERROR_TIME_EXPIRED | Demo time has expired |
| LRG_DEMO_ERROR_SAVE_LOCKED | Demo save cannot be used |

## Usage Examples

### Basic Setup

```c
LrgDemoManager *demo = lrg_demo_manager_get_default ();

/* Enable demo mode */
lrg_demo_manager_set_demo_mode (demo, TRUE);

/* Set 30-minute time limit */
lrg_demo_manager_set_time_limit (demo, 1800.0f);

/* Set warning times (5 min and 1 min remaining) */
lrg_demo_manager_set_warning_times (demo, (gfloat[]){300.0f, 60.0f}, 2);

/* Set purchase URL */
lrg_demo_manager_set_purchase_url (demo, "https://store.steampowered.com/app/12345");

/* Gate content */
lrg_demo_manager_gate_content (demo, "level-5");
lrg_demo_manager_gate_content (demo, "level-6");
lrg_demo_manager_gate_content (demo, "boss-final");

/* Start demo session */
lrg_demo_manager_start (demo);
```

### Implementing LrgDemoGatable

```c
/* In your level class */
static const gchar *
my_level_get_content_id (LrgDemoGatable *gatable)
{
    MyLevel *self = MY_LEVEL (gatable);
    return self->level_id;
}

static gboolean
my_level_is_demo_content (LrgDemoGatable *gatable)
{
    MyLevel *self = MY_LEVEL (gatable);
    return self->is_tutorial || self->level_number <= 4;
}

static const gchar *
my_level_get_unlock_message (LrgDemoGatable *gatable)
{
    return "Purchase the full game to access all 20 levels!";
}

static void
my_level_demo_gatable_init (LrgDemoGatableInterface *iface)
{
    iface->get_content_id = my_level_get_content_id;
    iface->is_demo_content = my_level_is_demo_content;
    iface->get_unlock_message = my_level_get_unlock_message;
}

G_DEFINE_TYPE_WITH_CODE (MyLevel, my_level, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DEMO_GATABLE,
                                                my_level_demo_gatable_init))
```

### Checking Content Access

```c
gboolean
game_load_level (Game *game, LrgDemoGatable *level)
{
    LrgDemoManager *demo = lrg_demo_manager_get_default ();
    g_autoptr(GError) error = NULL;

    if (!lrg_demo_manager_check_access (demo, level, &error))
    {
        /* Show upgrade dialog to user */
        show_upgrade_dialog (game, lrg_demo_gatable_get_unlock_message (level));
        return FALSE;
    }

    /* Load the level */
    return do_load_level (game, level);
}
```

### Handling Signals

```c
static void
on_demo_ended (LrgDemoManager   *manager,
               LrgDemoEndReason  reason,
               gpointer          user_data)
{
    Game *game = (Game *)user_data;

    switch (reason)
    {
    case LRG_DEMO_END_REASON_TIME_LIMIT:
        show_time_expired_dialog (game);
        break;
    case LRG_DEMO_END_REASON_CONTENT_COMPLETE:
        show_demo_complete_dialog (game);
        break;
    case LRG_DEMO_END_REASON_UPGRADED:
        /* User bought the full game! */
        show_thank_you_dialog (game);
        break;
    default:
        break;
    }
}

static void
on_time_warning (LrgDemoManager *manager,
                 gfloat          seconds_remaining,
                 gpointer        user_data)
{
    Game *game = (Game *)user_data;
    gint minutes = (gint)(seconds_remaining / 60.0f);

    show_time_warning (game, minutes);
}

static void
on_content_blocked (LrgDemoManager *manager,
                    const gchar    *content_id,
                    const gchar    *unlock_message,
                    gpointer        user_data)
{
    Game *game = (Game *)user_data;
    show_locked_content_ui (game, unlock_message);
}

/* Connect signals */
g_signal_connect (demo, "demo-ended", G_CALLBACK (on_demo_ended), game);
g_signal_connect (demo, "time-warning", G_CALLBACK (on_time_warning), game);
g_signal_connect (demo, "content-blocked", G_CALLBACK (on_content_blocked), game);
```

### Game Loop Integration

```c
void
game_update (Game *game, gfloat delta_time)
{
    LrgDemoManager *demo = lrg_demo_manager_get_default ();

    /* Update demo time tracking */
    lrg_demo_manager_update (demo, delta_time);

    /* Periodically check for upgrade (e.g., Steam license) */
    if (game->frames % 600 == 0)  /* Every 10 seconds at 60fps */
    {
        lrg_demo_manager_check_upgrade (demo);
    }

    /* Rest of game update... */
}
```

### Demo Save Management

```c
void
game_save (Game *game, const gchar *slot)
{
    LrgDemoManager *demo = lrg_demo_manager_get_default ();

    /* Save game data... */
    save_game_data (game, slot);

    /* Mark as demo save if in demo mode */
    if (lrg_demo_manager_get_demo_mode (demo))
    {
        lrg_demo_manager_mark_save_as_demo (demo, slot);
    }
}

void
show_save_slots (Game *game)
{
    LrgDemoManager *demo = lrg_demo_manager_get_default ();
    GPtrArray *saves = get_all_saves ();

    for (guint i = 0; i < saves->len; i++)
    {
        SaveSlot *slot = g_ptr_array_index (saves, i);

        if (lrg_demo_manager_is_demo_save (demo, slot->id))
        {
            /* Show demo badge on save slot */
            slot->label = g_strdup_printf ("%s (Demo)", slot->name);
        }
    }
}
```

### Upgrade Detection with Steam

```c
static gboolean
check_steam_license (gpointer user_data)
{
    LrgSteamClient *steam = (LrgSteamClient *)user_data;

    /* Check if user now owns the full game */
    return lrg_steam_client_is_app_owned (steam, FULL_GAME_APP_ID);
}

void
setup_demo_with_steam (Game *game)
{
    LrgDemoManager *demo = lrg_demo_manager_get_default ();
    LrgSteamClient *steam = game->steam_client;

    /* Set upgrade check function */
    lrg_demo_manager_set_upgrade_check_func (demo, check_steam_license, steam);

    /* If user already owns full game, disable demo */
    if (lrg_steam_client_is_app_owned (steam, FULL_GAME_APP_ID))
    {
        lrg_demo_manager_set_demo_mode (demo, FALSE);
    }
}
```

## Best Practices

1. **Gate Content Thoughtfully**: Give players a good taste of your game while keeping compelling content locked.

2. **Warn Before Time Expires**: Multiple warnings (5 min, 1 min) let players save and wrap up.

3. **Allow Demo Saves**: Let demo progress carry over to the full game.

4. **Make Upgrade Easy**: Provide a direct link to purchase the full version.

5. **Handle Upgrade Gracefully**: When users upgrade, disable demo mode without interrupting gameplay.

## Error Handling

All demo operations return gracefully when not in demo mode:

```c
LrgDemoManager *demo = lrg_demo_manager_get_default ();

/* Safe to call even when demo mode is disabled */
lrg_demo_manager_update (demo, delta_time);

/* check_access always returns TRUE when not in demo mode */
if (!lrg_demo_manager_check_access (demo, gatable, &error))
{
    /* Only happens if demo_mode is TRUE and content is gated */
}
```

## See Also

- [Save System](../save/index.md) - For demo save management
- [Steam Module](../steam/index.md) - For Steam-based upgrade detection
- [UI Module](../ui/index.md) - For demo UI elements
