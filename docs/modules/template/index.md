---
title: Template Module
---

# Template Module

The Template module provides ready-to-use game state implementations for common menu patterns. These derivable states can be used directly or subclassed for customization.

> **[Home](../../index.md)** > **[Modules](../index.md)** > Template

## Documentation

### Game Templates

Base templates for different game types:

| Template | Description |
|----------|-------------|
| [LrgGameTemplate](templates/game-template.md) | Base template with lifecycle, input, and game feel |
| [LrgGame2DTemplate](templates/game-2d-template.md) | 2D games with virtual resolution and cameras |
| [LrgGame3DTemplate](templates/game-3d-template.md) | 3D games with first/third-person cameras |

### Genre Templates

Pre-built templates for specific game genres:

| Template | Description |
|----------|-------------|
| [LrgIdleTemplate](templates/idle-template.md) | Idle/incremental games with big numbers |
| [LrgDeckbuilderTemplate](templates/deckbuilder-template.md) | Card games (combat and poker variants) |
| [LrgPlatformerTemplate](templates/platformer-template.md) | 2D platformers with physics |
| [LrgTopDownTemplate](templates/top-down-template.md) | Top-down games (RPG, action) |
| [LrgTwinStickTemplate / LrgShmupTemplate](templates/shooter-template.md) | 2D shooter variants |
| [LrgFPSTemplate](templates/fps-template.md) | First-person shooters |
| [LrgThirdPersonTemplate](templates/third-person-template.md) | Third-person action games |
| [LrgRacing2DTemplate / LrgRacing3DTemplate](templates/racing-template.md) | Racing games |
| [LrgTycoonTemplate](templates/tycoon-template.md) | Tycoon/management games |

### Systems

Core systems available to all templates:

| System | Description |
|--------|-------------|
| [Menu States](systems/menu-states.md) | Main menu, pause, settings, loading, error, confirmation |
| [Input Buffer](systems/input-buffer.md) | Frame-perfect input buffering for action games |
| [Object Pool](systems/object-pool.md) | Object pooling for performance |
| [Game Feel](systems/game-feel.md) | Hit stop, screen shake, time scale |
| [Engagement](systems/engagement.md) | Statistics, daily rewards, difficulty |
| [Mixins](systems/mixins.md) | LrgIdleMixin, LrgDeckMixin interfaces |

### Examples

Complete working examples:

| Example | Description |
|---------|-------------|
| [Minimal Game](examples/minimal-game.md) | Simplest possible template usage |
| [Idle Game](examples/idle-game.md) | Cookie clicker-style game |
| [Deckbuilder](examples/deckbuilder.md) | Roguelike card combat |
| [Platformer](examples/platformer.md) | 2D platformer with wall mechanics |

### Guides

| Guide | Description |
|-------|-------------|
| [Best Practices](../../guides/template-best-practices.md) | Coordinate spaces, async resize, common pitfalls |

---

## Overview

The template module contains pre-built implementations of game states that handle common UI patterns in games:

### Menu States

| Type | Purpose |
|------|---------|
| `LrgTemplateMainMenuState` | Title screen with menu buttons |
| `LrgTemplatePauseMenuState` | Pause overlay with audio ducking |
| `LrgTemplateSettingsMenuState` | Tabbed settings (graphics/audio/controls) |
| `LrgTemplateLoadingState` | Loading screen with progress bar |
| `LrgTemplateErrorState` | Error recovery screen |
| `LrgTemplateConfirmationState` | Generic confirmation dialog |

All template states are **derivable types**, allowing subclassing for customization while providing sensible defaults out of the box.

### Engagement Systems

| Type | Purpose |
|------|---------|
| `LrgTemplateStatistics` | Track game stats (counters, maximums, minimums, timers) |
| `LrgTemplateDailyRewards` | Interface for daily login rewards with streaks |
| `LrgTemplateDifficulty` | Interface for dynamic difficulty adjustment |

## Quick Start

### Basic Main Menu

```c
/* Create a main menu */
LrgTemplateMainMenuState *menu = lrg_template_main_menu_state_new ();

/* Set title */
lrg_template_main_menu_state_set_title (menu, "My Game");

/* Add buttons */
lrg_template_main_menu_state_add_button (menu, "new-game", "New Game");
lrg_template_main_menu_state_add_button (menu, "settings", "Settings");
lrg_template_main_menu_state_add_button (menu, "quit", "Quit");

/* Connect to button activation */
g_signal_connect (menu, "button-activated",
                  G_CALLBACK (on_menu_button), NULL);

/* Push onto state stack */
lrg_game_state_manager_push (manager, LRG_GAME_STATE (menu));
```

### Pause Menu with Audio Ducking

```c
/* Create pause menu */
LrgTemplatePauseMenuState *pause = lrg_template_pause_menu_state_new ();

/* Configure audio ducking */
lrg_template_pause_menu_state_set_audio_ducking (pause, TRUE);
lrg_template_pause_menu_state_set_duck_factor (pause, 0.3f);

/* Push as overlay (transparent, blocking) */
lrg_game_state_manager_push (manager, LRG_GAME_STATE (pause));
```

### Confirmation Dialog

```c
/* Create confirmation for exit */
LrgTemplateConfirmationState *confirm =
    lrg_template_confirmation_state_new_with_message (
        "Exit Game",
        "Are you sure you want to quit?");

/* Default to "No" for safety */
lrg_template_confirmation_state_set_default_selection (confirm, 1);

/* Handle response */
g_signal_connect (confirm, "confirmed", G_CALLBACK (on_quit), NULL);
g_signal_connect (confirm, "cancelled", G_CALLBACK (on_cancel), NULL);

lrg_game_state_manager_push (manager, LRG_GAME_STATE (confirm));
```

## Main Menu State

`LrgTemplateMainMenuState` provides a title screen with customizable buttons.

### Features

- Centered title with customizable text and logo
- Vertical button list with keyboard/gamepad navigation
- Optional subtitle text
- Background color/image support
- Version display in corner

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `title` | string | Main title text |
| `subtitle` | string | Optional subtitle |
| `show-version` | boolean | Show version in corner |

### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `button-activated` | button_id (string) | Button was clicked |

### Virtual Methods

Override these for custom behavior:

```c
struct _LrgTemplateMainMenuStateClass
{
    LrgGameStateClass parent_class;

    /* Creates the title widget area */
    LrgWidget * (*create_title_area)  (LrgTemplateMainMenuState *self);

    /* Creates the button menu area */
    LrgWidget * (*create_button_area) (LrgTemplateMainMenuState *self);

    /* Called when a button is activated */
    void (*on_button_activated) (LrgTemplateMainMenuState *self,
                                 const gchar              *button_id);
};
```

### Example: Custom Main Menu

```c
static void
my_menu_on_button_activated (LrgTemplateMainMenuState *self,
                              const gchar              *button_id)
{
    if (g_strcmp0 (button_id, "new-game") == 0)
    {
        /* Start new game */
        start_new_game ();
    }
    else if (g_strcmp0 (button_id, "settings") == 0)
    {
        /* Push settings state */
        LrgGameStateManager *mgr = get_state_manager ();
        LrgTemplateSettingsMenuState *settings =
            lrg_template_settings_menu_state_new ();
        lrg_game_state_manager_push (mgr, LRG_GAME_STATE (settings));
    }
    else if (g_strcmp0 (button_id, "quit") == 0)
    {
        /* Show confirmation */
        show_quit_confirmation ();
    }
}
```

## Pause Menu State

`LrgTemplatePauseMenuState` provides an overlay pause screen.

### Features

- Semi-transparent overlay
- Audio volume ducking
- Resume/Settings/Quit buttons
- Keyboard/gamepad navigation
- ESC to resume

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `audio-ducking` | boolean | TRUE | Reduce audio when paused |
| `duck-factor` | float | 0.3 | Volume multiplier (0.0-1.0) |
| `show-resume` | boolean | TRUE | Show Resume button |
| `show-settings` | boolean | TRUE | Show Settings button |
| `show-quit` | boolean | TRUE | Show Quit to Menu button |

### Signals

| Signal | Description |
|--------|-------------|
| `resume` | Resume button activated |
| `settings` | Settings button activated |
| `quit` | Quit button activated |

### Example: Custom Pause Behavior

```c
static void
on_pause_resume (LrgTemplatePauseMenuState *pause,
                 gpointer                   user_data)
{
    LrgGameStateManager *mgr = user_data;
    lrg_game_state_manager_pop (mgr);
}

static void
on_pause_quit (LrgTemplatePauseMenuState *pause,
               gpointer                   user_data)
{
    /* Show confirmation before quitting */
    LrgTemplateConfirmationState *confirm =
        lrg_template_confirmation_state_new_with_message (
            "Quit to Menu",
            "Unsaved progress will be lost!");

    g_signal_connect (confirm, "confirmed",
                      G_CALLBACK (quit_to_menu), NULL);

    lrg_game_state_manager_push (mgr, LRG_GAME_STATE (confirm));
}
```

## Settings Menu State

`LrgTemplateSettingsMenuState` provides a tabbed settings interface.

### Features

- Tabbed interface (Graphics, Audio, Controls)
- Customizable tab visibility
- Apply/Cancel/Reset buttons
- Unsaved changes detection
- Optional confirmation dialogs

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `show-graphics-tab` | boolean | TRUE | Show Graphics tab |
| `show-audio-tab` | boolean | TRUE | Show Audio tab |
| `show-controls-tab` | boolean | TRUE | Show Controls tab |
| `show-reset-button` | boolean | TRUE | Show Reset button |
| `confirm-cancel` | boolean | TRUE | Confirm unsaved changes |
| `confirm-reset` | boolean | TRUE | Confirm reset to defaults |

### Virtual Methods

Override to customize tab content:

```c
struct _LrgTemplateSettingsMenuStateClass
{
    LrgGameStateClass parent_class;

    /* Create tab content */
    LrgWidget * (*create_graphics_tab) (LrgTemplateSettingsMenuState *self);
    LrgWidget * (*create_audio_tab)    (LrgTemplateSettingsMenuState *self);
    LrgWidget * (*create_controls_tab) (LrgTemplateSettingsMenuState *self);
    LrgWidget * (*create_custom_tab)   (LrgTemplateSettingsMenuState *self,
                                        const gchar                  *tab_name);

    /* Button handlers */
    void (*on_apply)  (LrgTemplateSettingsMenuState *self);
    void (*on_cancel) (LrgTemplateSettingsMenuState *self);
    void (*on_reset)  (LrgTemplateSettingsMenuState *self);
};
```

### Adding Custom Tabs

```c
/* Add a gameplay settings tab */
lrg_template_settings_menu_state_add_custom_tab (settings,
                                                  "gameplay",
                                                  "Gameplay");

/* Override create_custom_tab to provide content */
static LrgWidget *
my_settings_create_custom_tab (LrgTemplateSettingsMenuState *self,
                                const gchar                  *tab_name)
{
    if (g_strcmp0 (tab_name, "gameplay") == 0)
    {
        return create_gameplay_settings_ui ();
    }
    return NULL;
}
```

## Loading State

`LrgTemplateLoadingState` provides a loading screen with progress indication.

### Features

- Progress bar (0.0 to 1.0)
- Loading text with optional details
- Spinner animation
- Minimum display time option
- Background task support

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `progress` | float | 0.0 | Loading progress (0.0-1.0) |
| `message` | string | "Loading..." | Main loading text |
| `detail` | string | NULL | Detail text below progress |
| `show-progress-bar` | boolean | TRUE | Show progress bar |
| `show-spinner` | boolean | TRUE | Show loading spinner |
| `min-display-time` | float | 0.5 | Minimum seconds to display |

### Example: Asset Loading

```c
/* Create loading screen */
LrgTemplateLoadingState *loading = lrg_template_loading_state_new ();
lrg_template_loading_state_set_message (loading, "Loading Level...");

/* Push state */
lrg_game_state_manager_push (manager, LRG_GAME_STATE (loading));

/* Update progress as assets load */
for (gint i = 0; i < asset_count; i++)
{
    load_asset (assets[i]);
    gfloat progress = (gfloat) (i + 1) / (gfloat) asset_count;
    lrg_template_loading_state_set_progress (loading, progress);
    lrg_template_loading_state_set_detail (loading, assets[i]->name);
}

/* Pop when done */
lrg_game_state_manager_pop (manager);
```

## Error State

`LrgTemplateErrorState` provides error recovery UI.

### Features

- Error title and message display
- Optional technical details (expandable)
- Retry and Quit buttons
- Error severity levels

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `error-title` | string | Error heading |
| `error-message` | string | User-friendly error message |
| `technical-details` | string | Technical info (collapsible) |
| `severity` | LrgTemplateErrorSeverity | Warning, Error, or Fatal |
| `show-retry` | boolean | Show Retry button |
| `show-quit` | boolean | Show Quit button |

### Signals

| Signal | Description |
|--------|-------------|
| `retry` | User clicked Retry |
| `quit` | User clicked Quit |

### Example: Error Handling

```c
static void
on_load_error (GError *error)
{
    LrgTemplateErrorState *error_state =
        lrg_template_error_state_new_with_error (error);

    /* Set severity based on error code */
    if (g_error_matches (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPTED))
    {
        lrg_template_error_state_set_severity (error_state,
                                                LRG_TEMPLATE_ERROR_FATAL);
        lrg_template_error_state_set_show_retry (error_state, FALSE);
    }

    g_signal_connect (error_state, "retry", G_CALLBACK (retry_load), NULL);
    g_signal_connect (error_state, "quit", G_CALLBACK (quit_game), NULL);

    lrg_game_state_manager_push (manager, LRG_GAME_STATE (error_state));
}
```

## Confirmation State

`LrgTemplateConfirmationState` provides modal confirmation dialogs.

### Features

- Customizable title and message
- Confirm/Cancel buttons with custom labels
- Default selection (safe option)
- Semi-transparent overlay
- Keyboard/gamepad navigation

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `title` | string | "Confirm" | Dialog title |
| `message` | string | "Are you sure?" | Dialog message |
| `confirm-label` | string | "Yes" | Confirm button text |
| `cancel-label` | string | "No" | Cancel button text |
| `default-selection` | int | 1 | Default button (0=confirm, 1=cancel) |

### Signals

| Signal | Description |
|--------|-------------|
| `confirmed` | User confirmed action |
| `cancelled` | User cancelled action |

### Example: Delete Confirmation

```c
LrgTemplateConfirmationState *confirm =
    lrg_template_confirmation_state_new_with_message (
        "Delete Save",
        "This will permanently delete your save file.");

/* Customize buttons */
lrg_template_confirmation_state_set_confirm_label (confirm, "Delete");
lrg_template_confirmation_state_set_cancel_label (confirm, "Keep");

/* Default to cancel (safer) */
lrg_template_confirmation_state_set_default_selection (confirm, 1);

/* Handle response */
g_signal_connect (confirm, "confirmed",
                  G_CALLBACK (delete_save_file), save_slot);
g_signal_connect (confirm, "cancelled",
                  G_CALLBACK (on_dialog_close), NULL);

lrg_game_state_manager_push (manager, LRG_GAME_STATE (confirm));
```

## Customization

All template states support customization through:

1. **Properties** - Configure behavior without subclassing
2. **Signals** - React to user actions
3. **Virtual Methods** - Override for custom behavior
4. **Subclassing** - Full control over implementation

### Subclassing Example

```c
/* Define custom main menu */
G_DECLARE_FINAL_TYPE (MyMainMenu, my_main_menu, MY, MAIN_MENU,
                      LrgTemplateMainMenuState)

struct _MyMainMenu
{
    LrgTemplateMainMenuState parent_instance;
    GrlTexture *logo;
};

static LrgWidget *
my_main_menu_create_title_area (LrgTemplateMainMenuState *self)
{
    MyMainMenu *menu = MY_MAIN_MENU (self);

    /* Create custom title with logo */
    LrgVBox *box = lrg_vbox_new ();

    /* Add logo image */
    LrgImage *logo_widget = lrg_image_new_from_texture (menu->logo);
    lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (logo_widget));

    /* Add title label */
    LrgLabel *title = lrg_label_new ("MY AWESOME GAME");
    lrg_label_set_font_size (title, 48.0f);
    lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (title));

    return LRG_WIDGET (box);
}

static void
my_main_menu_class_init (MyMainMenuClass *klass)
{
    LrgTemplateMainMenuStateClass *menu_class =
        LRG_TEMPLATE_MAIN_MENU_STATE_CLASS (klass);

    menu_class->create_title_area = my_main_menu_create_title_area;
}
```

## Statistics Tracking

`LrgTemplateStatistics` provides a comprehensive statistics tracking system implementing the `LrgSaveable` interface for persistence.

### Stat Types

| Type | Purpose | Example |
|------|---------|---------|
| Counter | Incrementable values | Enemies defeated, items collected |
| Maximum | Tracks highest value | High score, longest combo |
| Minimum | Tracks lowest value | Fastest time, fewest deaths |
| Timer | Accumulated time | Total play time, time in level |

### Example: Using Statistics

```c
/* Create statistics tracker */
LrgTemplateStatistics *stats = lrg_template_statistics_new ("player-stats");

/* Track counters */
lrg_template_statistics_track_counter (stats, "enemies_defeated", 1);
lrg_template_statistics_track_counter (stats, "gold_collected", 50);

/* Track maximums (high scores) */
lrg_template_statistics_track_maximum (stats, "high_score", 15000.0);
lrg_template_statistics_track_maximum (stats, "longest_combo", 25.0);

/* Track minimums (fastest times) */
lrg_template_statistics_track_minimum (stats, "fastest_level_1", 45.7);

/* Timer tracking */
lrg_template_statistics_timer_start (stats, "session_time");
/* ... gameplay ... */
lrg_template_statistics_timer_stop (stats, "session_time");
gdouble total_time = lrg_template_statistics_get_timer (stats, "session_time");

/* Query stats */
gint64 kills = lrg_template_statistics_get_counter (stats, "enemies_defeated");
gdouble high = lrg_template_statistics_get_maximum (stats, "high_score");

/* List all stat names */
GList *all_names = lrg_template_statistics_get_all_names (stats);
GList *counter_names = lrg_template_statistics_get_counter_names (stats);
```

## Daily Rewards Interface

`LrgTemplateDailyRewards` is an interface for implementing daily login rewards with streak bonuses.

### Features

- 24-hour cooldown between claims
- Streak tracking with bonus multipliers
- 48-hour streak expiration window
- Anti-cheat measures (clock rollback detection, HMAC validation)

### Implementing Daily Rewards

```c
struct _MyGameState
{
    LrgGameState parent_instance;
    LrgDailyRewardState *daily_state;
};

static LrgDailyRewardState *
my_game_state_get_daily_reward_state (LrgTemplateDailyRewards *rewards)
{
    MyGameState *self = MY_GAME_STATE (rewards);
    return self->daily_state;
}

static void
my_game_state_on_daily_reward_claimed (LrgTemplateDailyRewards *rewards,
                                        gint                     streak_day)
{
    MyGameState *self = MY_GAME_STATE (rewards);

    /* Grant reward based on streak day */
    gint coins = 100 * streak_day;
    add_player_coins (self->player, coins);
}

static void
daily_rewards_iface_init (LrgTemplateDailyRewardsInterface *iface)
{
    iface->get_daily_reward_state = my_game_state_get_daily_reward_state;
    iface->on_daily_reward_claimed = my_game_state_on_daily_reward_claimed;
}

G_DEFINE_TYPE_WITH_CODE (MyGameState, my_game_state, LRG_TYPE_GAME_STATE,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_TEMPLATE_DAILY_REWARDS,
                                                 daily_rewards_iface_init))
```

### Using Daily Rewards

```c
/* On session start */
lrg_template_daily_rewards_session_start (LRG_TEMPLATE_DAILY_REWARDS (state));

/* Check if claim available */
if (lrg_template_daily_rewards_can_claim (LRG_TEMPLATE_DAILY_REWARDS (state)))
{
    show_daily_reward_popup ();
}

/* Claim the reward */
gint streak_day = lrg_template_daily_rewards_claim (
    LRG_TEMPLATE_DAILY_REWARDS (state));

/* Get streak bonus for other rewards */
gdouble bonus = lrg_template_daily_rewards_get_streak_bonus_multiplier (
    LRG_TEMPLATE_DAILY_REWARDS (state));

/* Show time until next claim */
gint64 seconds = lrg_template_daily_rewards_get_time_until_claim (
    LRG_TEMPLATE_DAILY_REWARDS (state));
```

## Dynamic Difficulty Interface

`LrgTemplateDifficulty` is an interface for implementing dynamic difficulty adjustment (DDA).

### Performance Score

The system uses a performance score (0.0 to 1.0):
- 0.0 = Struggling (dying frequently)
- 0.5 = Balanced (appropriate challenge)
- 1.0 = Dominating (never dying)

### Difficulty Modifier

Based on performance, a difficulty modifier is calculated:
- Below 0.5: Game gets easier (modifier < 1.0)
- At 0.5: No change (modifier = 1.0)
- Above 0.5: Game gets harder (modifier > 1.0)

### Implementing Dynamic Difficulty

```c
struct _MyGameState
{
    LrgGameState parent_instance;

    gdouble current_modifier;
    gdouble success_sum;
    gdouble failure_sum;
    gdouble total_weight;
};

static gdouble
my_game_state_get_performance_score (LrgTemplateDifficulty *difficulty)
{
    MyGameState *self = MY_GAME_STATE (difficulty);
    if (self->total_weight <= 0.0)
        return 0.5;  /* Neutral if no data */
    return self->success_sum / self->total_weight;
}

static gdouble
my_game_state_get_difficulty_modifier (LrgTemplateDifficulty *difficulty)
{
    MyGameState *self = MY_GAME_STATE (difficulty);
    return self->current_modifier;
}

static void
my_game_state_record_player_success (LrgTemplateDifficulty *difficulty,
                                      gdouble                weight)
{
    MyGameState *self = MY_GAME_STATE (difficulty);
    self->success_sum += weight;
    self->total_weight += weight;
    update_modifier (self);
}

static void
difficulty_iface_init (LrgTemplateDifficultyInterface *iface)
{
    iface->get_performance_score = my_game_state_get_performance_score;
    iface->get_difficulty_modifier = my_game_state_get_difficulty_modifier;
    iface->record_player_success = my_game_state_record_player_success;
    iface->record_player_failure = my_game_state_record_player_failure;
}
```

### Using Dynamic Difficulty

```c
/* Record player events with weights */
lrg_template_difficulty_record_player_success (
    LRG_TEMPLATE_DIFFICULTY (state), 1.0);  /* Killed enemy */

lrg_template_difficulty_record_player_success (
    LRG_TEMPLATE_DIFFICULTY (state), 5.0);  /* Killed boss */

lrg_template_difficulty_record_player_failure (
    LRG_TEMPLATE_DIFFICULTY (state), 3.0);  /* Player died */

lrg_template_difficulty_record_player_failure (
    LRG_TEMPLATE_DIFFICULTY (state), 0.5);  /* Took damage */

/* Apply modifier to game parameters */
gdouble modifier = lrg_template_difficulty_get_difficulty_modifier (
    LRG_TEMPLATE_DIFFICULTY (state));

enemy->health *= modifier;
enemy->damage *= modifier;
player->regen_rate *= (2.0 - modifier);  /* Inverse for player benefits */

/* Check player state */
if (lrg_template_difficulty_is_player_struggling (
        LRG_TEMPLATE_DIFFICULTY (state)))
{
    show_hint_popup ("Try using your special ability!");
}

/* Reset on level change */
lrg_template_difficulty_reset_performance_window (
    LRG_TEMPLATE_DIFFICULTY (state));
```

## Genre-Specific Templates (Phase 3)

The template module also provides genre-specific templates that combine multiple Libregnum systems into cohesive game loops for specific genres.

### Genre Template Summary

| Type | Genre | Parent | Description |
|------|-------|--------|-------------|
| `LrgIdleMixin` | Idle/Clicker | Interface | Offline progress, prestige, multipliers |
| `LrgIdleTemplate` | Idle/Clicker | LrgGameTemplate | Complete idle game template |
| `LrgDeckMixin` | Deckbuilder | Interface | Deck, hand, and discard pile management |
| `LrgDeckbuilderTemplate` | Deckbuilder | LrgGameTemplate | Base deckbuilder with energy and turns |
| `LrgDeckbuilderCombatTemplate` | Deckbuilder/Combat | LrgDeckbuilderTemplate | Slay the Spire-style combat |
| `LrgDeckbuilderPokerTemplate` | Deckbuilder/Poker | LrgDeckbuilderTemplate | Balatro-style poker scoring |

---

## Idle Game Templates

### LrgIdleMixin Interface

`LrgIdleMixin` is an interface providing idle game functionality that can be mixed into any `LrgGameTemplate` subclass.

#### Features

- **Offline Progress**: Calculate earnings while the game was closed
- **Prestige System**: Reset progress for permanent multipliers
- **Base Production**: Passive resource generation
- **Click Value**: Active click/tap multipliers
- **Rate Multipliers**: Global production boosts

#### Interface Methods

```c
struct _LrgIdleMixinInterface
{
    GTypeInterface parent_iface;

    /* Calculate offline progress when game resumes */
    gdouble (*calculate_offline_progress) (LrgIdleMixin *self,
                                            gdouble       seconds_offline);

    /* Get base production rate per second */
    gdouble (*get_base_production_rate) (LrgIdleMixin *self);

    /* Get value per click/tap */
    gdouble (*get_click_value) (LrgIdleMixin *self);

    /* Get global multiplier */
    gdouble (*get_rate_multiplier) (LrgIdleMixin *self);

    /* Prestige operations */
    gdouble (*calculate_prestige_bonus) (LrgIdleMixin *self);
    void    (*apply_prestige)           (LrgIdleMixin *self);
    gboolean (*can_prestige)            (LrgIdleMixin *self);
};
```

#### Example: Implementing LrgIdleMixin

```c
static gdouble
my_game_calculate_offline_progress (LrgIdleMixin *self,
                                     gdouble       seconds_offline)
{
    MyGame *game = MY_GAME (self);
    gdouble rate = lrg_idle_mixin_get_base_production_rate (self);
    gdouble multiplier = lrg_idle_mixin_get_rate_multiplier (self);

    /* Cap at 8 hours of offline progress */
    seconds_offline = MIN (seconds_offline, 8 * 3600.0);

    return rate * multiplier * seconds_offline;
}

static void
idle_mixin_iface_init (LrgIdleMixinInterface *iface)
{
    iface->calculate_offline_progress = my_game_calculate_offline_progress;
    iface->get_base_production_rate = my_game_get_base_production_rate;
    iface->get_click_value = my_game_get_click_value;
    iface->get_rate_multiplier = my_game_get_rate_multiplier;
    iface->calculate_prestige_bonus = my_game_calculate_prestige_bonus;
    iface->apply_prestige = my_game_apply_prestige;
    iface->can_prestige = my_game_can_prestige;
}
```

### LrgIdleTemplate

`LrgIdleTemplate` is a complete idle game template that implements `LrgIdleMixin` with sensible defaults.

#### Features

- Default offline progress calculation
- Built-in prestige with configurable thresholds
- Automatic idle tick updates
- BigNumber integration for large values
- Automation hook support

#### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `base-production-rate` | gdouble | 1.0 | Base rate per second |
| `click-value` | gdouble | 1.0 | Value per active click |
| `rate-multiplier` | gdouble | 1.0 | Global production multiplier |
| `prestige-count` | gint | 0 | Number of prestiges |
| `prestige-bonus` | gdouble | 0.0 | Current prestige bonus |
| `prestige-threshold` | gdouble | 1e6 | Threshold to prestige |
| `offline-progress-cap` | gdouble | 28800.0 | Max offline seconds (8h) |

#### Example: Basic Idle Game

```c
/* Create idle game template */
LrgIdleTemplate *idle = lrg_idle_template_new ();

/* Configure production */
lrg_idle_template_set_base_production_rate (idle, 1.0);
lrg_idle_template_set_click_value (idle, 1.0);

/* Configure prestige */
lrg_idle_template_set_prestige_threshold (idle, 1000000.0);

/* On game resume, apply offline progress */
gdouble offline_seconds = calculate_time_since_last_session ();
gdouble earnings = lrg_idle_mixin_calculate_offline_progress (
    LRG_IDLE_MIXIN (idle), offline_seconds);

/* Show welcome back popup */
show_offline_earnings_popup (earnings);

/* Check for prestige */
if (lrg_idle_mixin_can_prestige (LRG_IDLE_MIXIN (idle)))
{
    gdouble bonus = lrg_idle_mixin_calculate_prestige_bonus (
        LRG_IDLE_MIXIN (idle));
    show_prestige_available_popup (bonus);
}
```

---

## Deckbuilder Templates

### LrgDeckMixin Interface

`LrgDeckMixin` is an interface providing deck, hand, and discard pile management.

#### Features

- Deck instance management
- Hand operations (draw, discard, play)
- Discard pile shuffling
- Card query operations

#### Interface Methods

```c
struct _LrgDeckMixinInterface
{
    GTypeInterface parent_iface;

    /* Deck management */
    LrgDeckInstance * (*get_deck_instance) (LrgDeckMixin *self);
    void              (*set_deck_instance) (LrgDeckMixin   *self,
                                            LrgDeckInstance *deck);

    /* Hand operations */
    void (*draw_cards)    (LrgDeckMixin *self, guint count);
    void (*discard_card)  (LrgDeckMixin *self, LrgCardInstance *card);
    void (*discard_hand)  (LrgDeckMixin *self);
    void (*shuffle_discard_into_deck) (LrgDeckMixin *self);
};
```

### LrgDeckbuilderTemplate

`LrgDeckbuilderTemplate` is the base template for deckbuilder games, implementing `LrgDeckMixin`.

#### Features

- Energy system (mana/action points)
- Turn-based gameplay
- Card cost evaluation
- Hand size management
- Draw/discard operations

#### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `current-energy` | gint | 0 | Current available energy |
| `max-energy` | gint | 3 | Maximum energy per turn |
| `current-turn` | gint | 0 | Current turn number |
| `base-hand-size` | gint | 5 | Cards drawn per turn |

#### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `turn-started` | turn (int) | New turn began |
| `turn-ended` | turn (int) | Turn completed |
| `card-played` | card (LrgCardInstance) | Card was played |
| `card-drawn` | card (LrgCardInstance) | Card was drawn |
| `energy-changed` | old, new (int) | Energy amount changed |

#### Virtual Methods

```c
struct _LrgDeckbuilderTemplateClass
{
    LrgGameTemplateClass parent_class;

    /* Turn management */
    void (*start_turn) (LrgDeckbuilderTemplate *self);
    void (*end_turn)   (LrgDeckbuilderTemplate *self);

    /* Card operations */
    gboolean (*can_play_card) (LrgDeckbuilderTemplate *self,
                               LrgCardInstance        *card);
    void     (*play_card)     (LrgDeckbuilderTemplate *self,
                               LrgCardInstance        *card,
                               gpointer                target);
    gint     (*get_card_cost) (LrgDeckbuilderTemplate *self,
                               LrgCardInstance        *card);

    /* Deck creation */
    LrgDeckInstance * (*create_default_deck) (LrgDeckbuilderTemplate *self);
};
```

#### Example: Basic Deckbuilder

```c
/* Create deckbuilder template */
LrgDeckbuilderTemplate *game = g_object_new (MY_TYPE_DECKBUILDER, NULL);

/* Configure energy */
lrg_deckbuilder_template_set_max_energy (game, 3);
lrg_deckbuilder_template_set_base_hand_size (game, 5);

/* Start first turn */
lrg_deckbuilder_template_start_turn (game);

/* Play a card */
LrgCardInstance *card = get_selected_card ();
if (lrg_deckbuilder_template_can_play_card (game, card))
{
    lrg_deckbuilder_template_play_card (game, card, target);
}

/* End turn */
lrg_deckbuilder_template_end_turn (game);
```

### LrgDeckbuilderCombatTemplate

`LrgDeckbuilderCombatTemplate` extends `LrgDeckbuilderTemplate` for **Slay the Spire-style** combat.

#### Features

- Combat context integration
- Player health, block, and status effects
- Enemy management (add, remove, target)
- Turn phases (player → enemy)
- Victory/defeat detection

#### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `in-combat` | gboolean | FALSE | Currently in combat |
| `combat-result` | LrgCombatResult | ONGOING | Combat outcome |
| `max-health` | gint | 80 | Player maximum health |
| `current-health` | gint | 80 | Player current health |
| `block` | gint | 0 | Player damage shield |
| `enemy-count` | gint | 0 | Number of enemies |

#### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `combat-started` | - | Combat began |
| `combat-ended` | result (LrgCombatResult) | Combat ended |
| `player-damaged` | damage, blocked (int) | Player took damage |
| `enemy-defeated` | enemy (LrgEnemyInstance) | Enemy died |
| `player-turn-ended` | - | Player passed turn |
| `enemy-turn-started` | index (int) | Enemy's turn began |

#### Example: Slay the Spire Combat

```c
/* Create combat template */
LrgDeckbuilderCombatTemplate *combat =
    lrg_deckbuilder_combat_template_new ();

/* Set up player */
lrg_deckbuilder_combat_template_set_max_health (combat, 80);

/* Start combat */
lrg_deckbuilder_combat_template_start_combat (combat);

/* Add enemies */
LrgEnemyDef *cultist = load_enemy_def ("enemies/cultist.yaml");
lrg_deckbuilder_combat_template_add_enemy_from_def (combat, cultist);

/* Player turn - play cards */
LrgCardInstance *strike = get_card_from_hand ("Strike");
LrgEnemyInstance *target = lrg_deckbuilder_combat_template_get_selected_target (combat);
lrg_deckbuilder_template_play_card (LRG_DECKBUILDER_TEMPLATE (combat),
                                     strike, target);

/* End player turn (triggers enemy turns) */
lrg_deckbuilder_combat_template_end_player_turn (combat);

/* Check for combat end */
LrgCombatResult result = lrg_deckbuilder_combat_template_get_combat_result (combat);
if (result == LRG_COMBAT_RESULT_VICTORY)
{
    show_rewards_screen ();
}
else if (result == LRG_COMBAT_RESULT_DEFEAT)
{
    show_game_over ();
}
```

#### Enemy Management

```c
/* Add enemies */
lrg_deckbuilder_combat_template_add_enemy_from_def (combat, def);

/* Target selection */
LrgEnemyInstance *enemy = lrg_deckbuilder_combat_template_get_enemy (combat, 0);
lrg_deckbuilder_combat_template_set_selected_target (combat, enemy);

/* Damage enemies */
lrg_deckbuilder_combat_template_damage_enemy (combat, enemy, 10);
lrg_deckbuilder_combat_template_damage_all_enemies (combat, 5);

/* Status effects */
lrg_deckbuilder_combat_template_apply_status_to_enemy (combat, enemy, "Vulnerable", 2);
lrg_deckbuilder_combat_template_apply_status_to_player (combat, "Strength", 3);
```

### LrgDeckbuilderPokerTemplate

`LrgDeckbuilderPokerTemplate` extends `LrgDeckbuilderTemplate` for **Balatro-style** poker games.

#### Features

- Poker hand evaluation (high card to royal flush)
- Chips × Mult scoring system
- Joker management
- Ante/blind progression
- Hands and discards per round

#### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `score` | gint64 | 0 | Current round score |
| `blind-score` | gint64 | 300 | Score needed to beat blind |
| `ante` | guint | 1 | Current ante level |
| `money` | gint64 | 0 | Player currency |
| `hands-remaining` | guint | 4 | Hands left this round |
| `discards-remaining` | guint | 3 | Discards left this round |
| `max-hands` | guint | 4 | Hands per round |
| `max-discards` | guint | 3 | Discards per round |
| `max-jokers` | guint | 5 | Maximum joker slots |
| `last-hand-type` | LrgHandType | NONE | Last played hand type |
| `last-hand-score` | gint64 | 0 | Last hand's score |

#### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `round-started` | ante (int) | New round began |
| `round-ended` | won (bool) | Round completed |
| `hand-played` | type, score (LrgHandType, int64) | Poker hand scored |
| `cards-discarded` | count (int) | Cards were discarded |
| `joker-added` | joker (LrgJokerInstance) | Joker was added |
| `joker-removed` | joker (LrgJokerInstance) | Joker was removed |

#### Default Hand Scoring

| Hand Type | Base Chips | Base Mult |
|-----------|------------|-----------|
| High Card | 5 | 1 |
| Pair | 10 | 2 |
| Two Pair | 20 | 2 |
| Three of a Kind | 30 | 3 |
| Straight | 30 | 4 |
| Flush | 35 | 4 |
| Full House | 40 | 4 |
| Four of a Kind | 60 | 7 |
| Straight Flush | 100 | 8 |
| Royal Flush | 100 | 8 |

#### Example: Balatro-style Poker

```c
/* Create poker template */
LrgDeckbuilderPokerTemplate *poker = lrg_deckbuilder_poker_template_new ();

/* Start a round */
lrg_deckbuilder_poker_template_start_round (poker);

/* Select cards and play a poker hand */
GPtrArray *selected = get_selected_cards (); /* 5 cards max */
lrg_deckbuilder_poker_template_play_hand (poker, selected);

/* Check result */
LrgHandType hand_type = lrg_deckbuilder_poker_template_get_last_hand_type (poker);
gint64 hand_score = lrg_deckbuilder_poker_template_get_last_hand_score (poker);

g_print ("Played %s for %ld points!\n",
         lrg_hand_type_to_string (hand_type), hand_score);

/* Or discard cards to draw new ones */
GPtrArray *to_discard = get_cards_to_discard ();
lrg_deckbuilder_poker_template_discard_cards (poker, to_discard);

/* Check round status */
if (lrg_deckbuilder_poker_template_is_round_won (poker))
{
    show_round_complete_screen ();
}
else if (lrg_deckbuilder_poker_template_is_round_lost (poker))
{
    show_game_over ();
}
```

#### Joker Management

```c
/* Add a joker */
LrgJokerDef *jolly = load_joker_def ("jokers/jolly.yaml");
LrgJokerInstance *joker = lrg_joker_instance_new_from_def (jolly);
lrg_deckbuilder_poker_template_add_joker (poker, joker);

/* Check joker count */
guint count = lrg_deckbuilder_poker_template_get_joker_count (poker);
guint max = lrg_deckbuilder_poker_template_get_max_jokers (poker);

if (count < max)
{
    /* Can add more jokers */
}

/* Remove a joker */
lrg_deckbuilder_poker_template_remove_joker (poker, joker);

/* Clear all jokers */
lrg_deckbuilder_poker_template_clear_jokers (poker);
```

#### Previewing Scores

```c
/* Preview score before playing */
GPtrArray *selected = get_selected_cards ();
gint64 preview = lrg_deckbuilder_poker_template_preview_score (poker, selected);
g_print ("This hand would score: %ld\n", preview);

/* Evaluate hand type without playing */
LrgHandType type = lrg_deckbuilder_poker_template_evaluate_hand (poker, selected);
g_print ("Hand type: %s\n", lrg_hand_type_to_string (type));
```

---

## Object Pooling (Phase 3.5)

The template module provides an efficient object pooling system for high-performance scenarios where frequent allocation/deallocation would cause GC pressure or performance issues.

### Overview

| Type | Purpose |
|------|---------|
| `LrgPoolable` | Interface for objects that can be managed by a pool |
| `LrgObjectPool` | Generic object pool with acquire/release semantics |

### LrgPoolable Interface

`LrgPoolable` is an interface that objects must implement to be managed by an `LrgObjectPool`.

#### Interface Methods

```c
struct _LrgPoolableInterface
{
    GTypeInterface parent_iface;

    /* Reset object to initial state (called when released back to pool) */
    void (*reset) (LrgPoolable *self);

    /* Query active/inactive state */
    gboolean (*is_active) (LrgPoolable *self);

    /* Set active/inactive state */
    void (*set_active) (LrgPoolable *self, gboolean active);

    /* Get owning pool (optional, can return NULL) */
    LrgObjectPool * (*get_pool) (LrgPoolable *self);

    gpointer _reserved[8];
};
```

#### Implementing LrgPoolable

```c
struct _MyPoolableObject
{
    GObject parent_instance;

    gboolean active;
    LrgObjectPool *pool;

    /* Object state */
    gfloat x;
    gfloat y;
    gfloat velocity_x;
    gfloat velocity_y;
};

static void
my_poolable_object_reset (LrgPoolable *self)
{
    MyPoolableObject *obj = MY_POOLABLE_OBJECT (self);

    /* Reset to initial state */
    obj->x = 0.0f;
    obj->y = 0.0f;
    obj->velocity_x = 0.0f;
    obj->velocity_y = 0.0f;
}

static gboolean
my_poolable_object_is_active (LrgPoolable *self)
{
    return MY_POOLABLE_OBJECT (self)->active;
}

static void
my_poolable_object_set_active (LrgPoolable *self,
                                gboolean     active)
{
    MY_POOLABLE_OBJECT (self)->active = active;
}

static LrgObjectPool *
my_poolable_object_get_pool (LrgPoolable *self)
{
    return MY_POOLABLE_OBJECT (self)->pool;
}

static void
poolable_iface_init (LrgPoolableInterface *iface)
{
    iface->reset = my_poolable_object_reset;
    iface->is_active = my_poolable_object_is_active;
    iface->set_active = my_poolable_object_set_active;
    iface->get_pool = my_poolable_object_get_pool;
}

G_DEFINE_TYPE_WITH_CODE (MyPoolableObject, my_poolable_object, G_TYPE_OBJECT,
                          G_IMPLEMENT_INTERFACE (LRG_TYPE_POOLABLE,
                                                  poolable_iface_init))
```

### LrgObjectPool

`LrgObjectPool` manages a collection of `LrgPoolable` objects with configurable growth policies.

#### Features

- **Acquire/Release Semantics**: Objects are acquired from the pool and released back when no longer needed
- **Growth Policies**: Control how the pool grows when exhausted
- **Prewarming**: Pre-allocate objects before they're needed
- **Shrinking**: Reclaim unused memory
- **Iteration**: Iterate over active objects with callbacks

#### Growth Policies

| Policy | Description |
|--------|-------------|
| `LRG_POOL_GROWTH_FIXED` | Never grow beyond initial size |
| `LRG_POOL_GROWTH_LINEAR` | Grow by initial size each time |
| `LRG_POOL_GROWTH_DOUBLE` | Double capacity each time |
| `LRG_POOL_GROWTH_EXPONENTIAL` | Grow exponentially (faster than double) |

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `object-type` | GType | Type of objects in the pool |
| `initial-size` | guint | Starting pool size |
| `max-size` | guint | Maximum pool size (0 = unlimited) |
| `growth-policy` | LrgPoolGrowthPolicy | How pool grows when exhausted |
| `active-count` | guint | Number of currently active objects (read-only) |
| `available-count` | guint | Number of available objects (read-only) |
| `total-size` | guint | Total objects in pool (read-only) |

#### Example: Bullet Pool for Shoot-em-up

```c
/* Create a pool for bullets */
LrgObjectPool *bullet_pool = lrg_object_pool_new (
    MY_TYPE_BULLET,        /* Object type (must implement LrgPoolable) */
    100,                   /* Initial size */
    LRG_POOL_GROWTH_DOUBLE /* Growth policy */
);

/* Optional: Set maximum size */
lrg_object_pool_set_max_size (bullet_pool, 1000);

/* Prewarm the pool at level start */
lrg_object_pool_prewarm (bullet_pool, 50);

/* Fire a bullet */
static void
fire_bullet (gfloat x, gfloat y, gfloat angle)
{
    MyBullet *bullet = MY_BULLET (lrg_object_pool_acquire (bullet_pool));

    if (bullet == NULL)
    {
        /* Pool exhausted and can't grow */
        return;
    }

    /* Initialize bullet */
    bullet->x = x;
    bullet->y = y;
    bullet->velocity_x = cosf (angle) * BULLET_SPEED;
    bullet->velocity_y = sinf (angle) * BULLET_SPEED;
}

/* Update all active bullets */
static void
update_bullets (gfloat delta)
{
    lrg_object_pool_foreach_active (bullet_pool, update_bullet_cb, &delta);
}

static gboolean
update_bullet_cb (LrgPoolable *poolable,
                  gpointer     user_data)
{
    MyBullet *bullet = MY_BULLET (poolable);
    gfloat delta = *(gfloat *) user_data;

    /* Move bullet */
    bullet->x += bullet->velocity_x * delta;
    bullet->y += bullet->velocity_y * delta;

    /* Check if off-screen */
    if (bullet->x < 0 || bullet->x > SCREEN_WIDTH ||
        bullet->y < 0 || bullet->y > SCREEN_HEIGHT)
    {
        /* Release back to pool */
        lrg_object_pool_release (bullet_pool, LRG_POOLABLE (bullet));
    }

    return TRUE; /* Continue iteration */
}

/* Or use convenience method on poolable */
static void
on_bullet_hit (MyBullet *bullet)
{
    lrg_poolable_release (LRG_POOLABLE (bullet));
}
```

#### Pool Management

```c
/* Check pool statistics */
guint active = lrg_object_pool_get_active_count (pool);
guint available = lrg_object_pool_get_available_count (pool);
guint total = lrg_object_pool_get_total_size (pool);

g_print ("Pool: %u active, %u available, %u total\n",
         active, available, total);

/* Shrink pool to reclaim memory (releases all inactive objects) */
lrg_object_pool_shrink (pool);

/* Clear all objects (releases all, resets to empty) */
lrg_object_pool_clear (pool);

/* Check if pool is exhausted */
if (lrg_object_pool_get_available_count (pool) == 0)
{
    g_print ("Pool exhausted!\n");
}
```

#### Particle System Example

```c
/* Create particle pool */
LrgObjectPool *particle_pool = lrg_object_pool_new (
    MY_TYPE_PARTICLE,
    500,
    LRG_POOL_GROWTH_LINEAR
);

/* Spawn particles for explosion */
static void
spawn_explosion (gfloat x, gfloat y, guint count)
{
    guint i;

    for (i = 0; i < count; i++)
    {
        MyParticle *particle = MY_PARTICLE (lrg_object_pool_acquire (particle_pool));

        if (particle == NULL)
            break;

        particle->x = x;
        particle->y = y;
        particle->lifetime = g_random_double_range (0.5, 1.5);
        particle->angle = g_random_double_range (0, 2.0 * G_PI);
        particle->speed = g_random_double_range (50.0, 200.0);
    }
}

/* Update particles, release dead ones */
static gboolean
update_particle_cb (LrgPoolable *poolable,
                    gpointer     user_data)
{
    MyParticle *particle = MY_PARTICLE (poolable);
    gfloat delta = *(gfloat *) user_data;

    particle->lifetime -= delta;

    if (particle->lifetime <= 0)
    {
        lrg_poolable_release (poolable);
    }
    else
    {
        particle->x += cosf (particle->angle) * particle->speed * delta;
        particle->y += sinf (particle->angle) * particle->speed * delta;
    }

    return TRUE;
}
```

---

## Genre Templates (Phase 5)

Phase 5 introduces genre-specific game templates that build on the core template system. These provide ready-to-use implementations of common game genre mechanics.

### Template Hierarchy

```
LrgGameTemplate (base)
├── LrgGame2DTemplate (2D base)
│   ├── LrgShooter2DTemplate (derivable)
│   │   ├── LrgTwinStickTemplate (final)
│   │   └── LrgShmupTemplate (final)
│   ├── LrgPlatformerTemplate (derivable)
│   ├── LrgTopDownTemplate (derivable)
│   ├── LrgTycoonTemplate (derivable)
│   └── LrgRacing2DTemplate (derivable)
│
└── LrgGame3DTemplate (3D base)
    ├── LrgFPSTemplate (derivable)
    ├── LrgThirdPersonTemplate (derivable)
    └── LrgRacing3DTemplate (derivable)
```

### 2D Templates

#### LrgGame2DTemplate

Base template for all 2D games. Provides virtual resolution scaling and camera management.

```c
/* Create a custom 2D game by subclassing */
G_DECLARE_FINAL_TYPE (MyGame, my_game, MY, GAME, LrgGame2DTemplate)

/* Use 1920x1080 virtual resolution */
LrgGame2DTemplate *game = g_object_new (MY_TYPE_GAME,
                                         "virtual-width", 1920,
                                         "virtual-height", 1080,
                                         NULL);
```

**Features:**
- Automatic letterboxing/pillarboxing for aspect ratio preservation
- 2D camera with smooth follow and deadzone
- Virtual resolution scaling

#### LrgShooter2DTemplate

Base template for 2D shooter games. Extends `LrgGame2DTemplate`.

```c
/* Subclass for custom shooter behavior */
G_DECLARE_FINAL_TYPE (MyShooter, my_shooter, MY, SHOOTER, LrgShooter2DTemplate)

/* Override spawn_projectile vfunc */
static void
my_shooter_spawn_projectile (LrgShooter2DTemplate *shooter,
                              gfloat                x,
                              gfloat                y,
                              gfloat                dir_x,
                              gfloat                dir_y,
                              gfloat                speed)
{
    /* Custom projectile spawning logic */
}
```

**Features:**
- Player health and lives system
- Projectile spawning with direction
- Enemy wave management
- Score tracking with high score
- Fire cooldown with rate limiting

**Properties:**
- `max-health` - Maximum player health
- `fire-rate` - Shots per second
- `projectile-speed` - Default projectile speed

#### LrgTwinStickTemplate

Final type for twin-stick shooter games. Extends `LrgShooter2DTemplate`.

```c
/* Create directly - no subclassing needed */
LrgTwinStickTemplate *game = lrg_twin_stick_template_new ();

/* Configure controls */
lrg_twin_stick_template_set_move_deadzone (game, 0.15f);
lrg_twin_stick_template_set_aim_deadzone (game, 0.2f);
lrg_twin_stick_template_set_fire_threshold (game, 0.5f);
```

**Features:**
- Dual analog stick input (left = move, right = aim)
- Auto-fire when aim stick magnitude exceeds threshold
- 360-degree movement and shooting
- Gamepad and keyboard/mouse support

#### LrgShmupTemplate

Final type for shoot-em-up (shmup) games. Extends `LrgShooter2DTemplate`.

```c
/* Create a vertical or horizontal shooter */
LrgShmupTemplate *game = g_object_new (LRG_TYPE_SHMUP_TEMPLATE,
                                        "scroll-mode", LRG_SHMUP_SCROLL_VERTICAL,
                                        "scroll-speed", 60.0f,
                                        NULL);
```

**Features:**
- Vertical or horizontal scrolling
- Configurable scroll speed
- Bullet pattern system
- Bomb/special weapon with limited uses
- Focus mode for precise movement

**Scroll Modes:**
- `LRG_SHMUP_SCROLL_VERTICAL` - Top-down scrolling
- `LRG_SHMUP_SCROLL_HORIZONTAL` - Side-scrolling

#### LrgPlatformerTemplate

Derivable template for platformer games. Extends `LrgGame2DTemplate`.

```c
/* Create a platformer with custom physics */
LrgPlatformerTemplate *game = g_object_new (LRG_TYPE_PLATFORMER_TEMPLATE,
                                             "gravity", 980.0f,
                                             "jump-force", 400.0f,
                                             "max-jumps", 2,
                                             NULL);
```

**Features:**
- Gravity and jump physics
- Multi-jump (configurable)
- Coyote time for forgiving jumps
- Variable jump height
- Wall sliding and wall jumping
- Ground detection

**Properties:**
- `gravity` - Downward acceleration
- `jump-force` - Initial jump velocity
- `max-jumps` - Maximum air jumps (1 = single, 2 = double)
- `coyote-time` - Grace period after leaving platform
- `wall-slide-speed` - Descend speed when wall sliding

**Signals:**
- `::landed` - Player touched ground
- `::jumped` - Player jumped
- `::wall-slide-started` - Started wall sliding
- `::wall-jumped` - Performed wall jump

#### LrgTopDownTemplate

Derivable template for top-down games (action RPGs, adventure). Extends `LrgGame2DTemplate`.

```c
/* Create a top-down adventure game */
LrgTopDownTemplate *game = g_object_new (LRG_TYPE_TOP_DOWN_TEMPLATE,
                                          "movement-mode", LRG_TOP_DOWN_8_WAY,
                                          "player-speed", 200.0f,
                                          NULL);
```

**Features:**
- 4-way or 8-way movement
- Dash/dodge with stamina cost
- Interaction system (NPCs, objects)
- Player facing direction tracking
- Health and stamina bars

**Movement Modes:**
- `LRG_TOP_DOWN_4_WAY` - Cardinal directions only
- `LRG_TOP_DOWN_8_WAY` - Cardinal and diagonal
- `LRG_TOP_DOWN_FREE` - Analog movement

#### LrgTycoonTemplate

Derivable template for tycoon/management games. Extends `LrgGame2DTemplate`.

```c
/* Create a tycoon game with economy */
LrgTycoonTemplate *game = g_object_new (LRG_TYPE_TYCOON_TEMPLATE,
                                         "grid-size", 32.0f,
                                         "starting-money", 10000.0,
                                         NULL);

/* Toggle build mode */
lrg_tycoon_template_set_build_mode (game, LRG_TYCOON_BUILD_PLACE);
```

**Features:**
- Grid-based building placement
- Panning camera with mouse/keyboard
- Zoom in/out
- Build mode system
- Time speed controls (pause, 1x, 2x, 4x)
- Money/resource tracking

**Build Modes:**
- `LRG_TYCOON_BUILD_NONE` - Normal mode
- `LRG_TYCOON_BUILD_PLACE` - Placing new buildings
- `LRG_TYCOON_BUILD_DEMOLISH` - Removing buildings
- `LRG_TYCOON_BUILD_ROAD` - Building roads/paths

#### LrgRacing2DTemplate

Derivable template for top-down racing games. Extends `LrgGame2DTemplate`.

```c
/* Create a racing game */
LrgRacing2DTemplate *game = g_object_new (LRG_TYPE_RACING_2D_TEMPLATE,
                                           "max-speed", 300.0f,
                                           "acceleration", 150.0f,
                                           "total-laps", 3,
                                           NULL);

/* Start the race */
lrg_racing_2d_template_start_race (game);
```

**Features:**
- Vehicle physics (acceleration, braking, steering)
- Boost/nitro system
- Lap and checkpoint tracking
- Race state machine (countdown, racing, finished)
- Chase camera with look-ahead
- Minimap display
- Speedometer HUD

**Race States:**
- `LRG_RACE_STATE_WAITING` - Pre-race
- `LRG_RACE_STATE_COUNTDOWN` - 3-2-1 countdown
- `LRG_RACE_STATE_RACING` - Active racing
- `LRG_RACE_STATE_FINISHED` - Race complete
- `LRG_RACE_STATE_PAUSED` - Paused

**Signals:**
- `::race-started` - Race countdown began
- `::race-finished` - Race completed
- `::lap-complete` - Crossed finish line
- `::checkpoint-reached` - Passed checkpoint
- `::boost-started` - Boost activated
- `::boost-ended` - Boost depleted

### 3D Templates

#### LrgGame3DTemplate

Base template for all 3D games. Provides 3D camera and mouse look controls.

```c
/* Create a custom 3D game by subclassing */
G_DECLARE_FINAL_TYPE (My3DGame, my_3d_game, MY, 3D_GAME, LrgGame3DTemplate)

/* Configure mouse sensitivity */
lrg_game_3d_template_set_mouse_sensitivity (game, 0.002f);
lrg_game_3d_template_set_pitch_limit (game, 85.0f);
```

**Features:**
- First-person camera with mouse look
- Configurable yaw/pitch limits
- Mouse sensitivity settings
- 3D rendering with depth buffer

#### LrgFPSTemplate

Derivable template for first-person shooter games. Extends `LrgGame3DTemplate`.

```c
/* Create an FPS game */
LrgFPSTemplate *game = g_object_new (LRG_TYPE_FPS_TEMPLATE,
                                      "move-speed", 5.0f,
                                      "sprint-multiplier", 1.8f,
                                      "jump-force", 8.0f,
                                      NULL);
```

**Features:**
- WASD movement with sprint
- Jump with gravity
- Ground detection
- Weapon system with fire rate
- Ammo management
- Health system
- Head bob effect
- Crosshair rendering

**Properties:**
- `move-speed` - Base walking speed
- `sprint-multiplier` - Speed multiplier when sprinting
- `jump-force` - Initial jump velocity
- `gravity` - Downward acceleration
- `fire-rate` - Shots per second
- `max-ammo` - Maximum ammunition
- `head-bob-intensity` - Head bob effect strength

**Signals:**
- `::weapon-fired` - Player fired weapon
- `::weapon-reloaded` - Reload complete
- `::damage-taken` - Player took damage
- `::player-died` - Player health reached zero

#### LrgThirdPersonTemplate

Derivable template for third-person games. Extends `LrgGame3DTemplate`.

```c
/* Create a third-person action game */
LrgThirdPersonTemplate *game = g_object_new (LRG_TYPE_THIRD_PERSON_TEMPLATE,
                                              "camera-distance", 5.0f,
                                              "camera-height", 2.0f,
                                              "aim-mode", LRG_THIRD_PERSON_AIM_FREE,
                                              NULL);
```

**Features:**
- Orbiting camera around player
- Multiple aim modes
- Shoulder offset for over-the-shoulder aiming
- Lock-on target system
- Dodge/roll with stamina cost
- Health and stamina systems
- Smooth camera follow with lerp

**Aim Modes:**
- `LRG_THIRD_PERSON_AIM_FREE` - Player faces movement direction
- `LRG_THIRD_PERSON_AIM_STRAFE` - Player strafes, faces camera direction
- `LRG_THIRD_PERSON_AIM_AIM` - Aiming down sights, shoulder offset
- `LRG_THIRD_PERSON_AIM_LOCK_ON` - Camera tracks locked target

**Properties:**
- `camera-distance` - Distance from player
- `camera-height` - Height offset
- `camera-pitch-min/max` - Vertical camera limits
- `shoulder-offset` - Horizontal offset for aiming
- `dodge-distance` - Roll distance
- `dodge-stamina-cost` - Stamina per dodge

**Signals:**
- `::aim-mode-changed` - Aim mode switched
- `::target-locked` - Locked onto enemy
- `::target-lost` - Lost lock-on
- `::dodged` - Player performed dodge
- `::attacked` - Player performed attack

#### LrgRacing3DTemplate

Derivable template for 3D racing games. Extends `LrgGame3DTemplate`.

```c
/* Create a 3D racing game */
LrgRacing3DTemplate *game = g_object_new (LRG_TYPE_RACING_3D_TEMPLATE,
                                           "max-speed", 150.0f,
                                           "acceleration", 40.0f,
                                           "total-laps", 3,
                                           NULL);

/* Set camera mode */
lrg_racing_3d_template_set_camera_mode (game, LRG_RACING_3D_CAMERA_CHASE);
```

**Features:**
- 3D vehicle physics
- Multiple camera modes
- Boost/nitro with gauge
- Lap and checkpoint system
- Race state machine
- Speedometer and minimap
- Collision detection

**Camera Modes:**
- `LRG_RACING_3D_CAMERA_CHASE` - Behind vehicle
- `LRG_RACING_3D_CAMERA_HOOD` - Hood/bonnet view
- `LRG_RACING_3D_CAMERA_BUMPER` - Low bumper view
- `LRG_RACING_3D_CAMERA_COCKPIT` - Interior view
- `LRG_RACING_3D_CAMERA_ORBIT` - Free orbit camera

**Race States:**
Same as `LrgRacing2DTemplate` (WAITING, COUNTDOWN, RACING, FINISHED, PAUSED)

**Signals:**
- `::race-state-changed` - Race state transition
- `::lap-complete` - Crossed finish line
- `::checkpoint-reached` - Passed checkpoint
- `::collision` - Vehicle collision
- `::boost-activated` - Boost started

### Creating Custom Genre Templates

To create a custom genre template, subclass the appropriate base:

```c
/* Custom puzzle game based on 2D template */
G_DECLARE_DERIVABLE_TYPE (MyPuzzleTemplate, my_puzzle_template,
                           MY, PUZZLE_TEMPLATE, LrgGame2DTemplate)

struct _MyPuzzleTemplateClass
{
    LrgGame2DTemplateClass parent_class;

    /* Custom virtual methods */
    void (*on_tile_selected) (MyPuzzleTemplate *self, gint x, gint y);
    void (*on_puzzle_solved) (MyPuzzleTemplate *self);
    gboolean (*check_solution) (MyPuzzleTemplate *self);

    gpointer _reserved[8];
};

/* Override parent vfuncs */
static void
my_puzzle_template_class_init (MyPuzzleTemplateClass *klass)
{
    LrgGame2DTemplateClass *template_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    template_class->pre_update = my_puzzle_template_pre_update;
    template_class->draw_game = my_puzzle_template_draw_game;

    /* Register custom signals */
    signals[SIGNAL_TILE_SELECTED] = g_signal_new ("tile-selected", ...);
}
```

---

## See Also

- **[Game State Module](../gamestate/index.md)** - State management system
- **[UI Module](../ui/index.md)** - Widget system
- **[Settings Module](../settings/index.md)** - Settings persistence
- **[Audio Module](../audio/index.md)** - Audio ducking
- **[Save Module](../save/index.md)** - Save/load persistence
- **[Deckbuilder Module](../deckbuilder/index.md)** - Card, deck, and combat systems
- **[Idle Module](../idle/index.md)** - BigNumber, prestige, and automation
- **[Economy Module](../economy/index.md)** - Resource production and consumption
