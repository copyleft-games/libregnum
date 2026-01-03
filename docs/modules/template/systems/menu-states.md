# Menu State System

The template system provides six pre-built game states for common menu and UI scenarios. All states are derivable, allowing customization through virtual method overrides.

## State Types

| State | Description |
|-------|-------------|
| `LrgTemplateMainMenuState` | Title screen / main menu |
| `LrgTemplatePauseMenuState` | In-game pause menu overlay |
| `LrgTemplateSettingsMenuState` | Tabbed settings/options menu |
| `LrgTemplateLoadingState` | Loading screen with progress |
| `LrgTemplateErrorState` | Error display with recovery options |
| `LrgTemplateConfirmationState` | Confirmation dialog |

---

## LrgTemplateMainMenuState

The main menu state provides a customizable title screen with standard menu items.

### Construction

```c
LrgTemplateMainMenuState *menu = lrg_template_main_menu_state_new ();
```

### Virtual Methods

```c
struct _LrgTemplateMainMenuStateClass
{
    LrgGameStateClass parent_class;

    /* Called when New Game is selected */
    void (*on_new_game)  (LrgTemplateMainMenuState *self);

    /* Called when Continue is selected */
    void (*on_continue)  (LrgTemplateMainMenuState *self);

    /* Called when Settings is selected */
    void (*on_settings)  (LrgTemplateMainMenuState *self);

    /* Called when Exit is selected */
    void (*on_exit)      (LrgTemplateMainMenuState *self);
};
```

### Menu Item Configuration

```c
/* Show/hide standard menu items */
lrg_template_main_menu_state_set_show_new_game (menu, TRUE);
lrg_template_main_menu_state_set_show_continue (menu, has_save_game);
lrg_template_main_menu_state_set_show_settings (menu, TRUE);
lrg_template_main_menu_state_set_show_exit (menu, TRUE);

/* Custom labels */
lrg_template_main_menu_state_set_new_game_label (menu, "Start Journey");
lrg_template_main_menu_state_set_continue_label (menu, "Resume");
```

### Adding Custom Menu Items

```c
/* Add custom items */
lrg_template_main_menu_state_add_item (menu, "credits", "Credits");
lrg_template_main_menu_state_add_item (menu, "extras", "Bonus Content");

/* Handle custom items via signal */
g_signal_connect (menu, "item-activated",
                  G_CALLBACK (on_menu_item_activated), game);

static void
on_menu_item_activated (LrgTemplateMainMenuState *menu,
                        const gchar              *item_id,
                        gpointer                  user_data)
{
    if (g_str_equal (item_id, "credits"))
        show_credits_screen (user_data);
    else if (g_str_equal (item_id, "extras"))
        show_extras_screen (user_data);
}
```

### Appearance

```c
/* Set title displayed at top */
lrg_template_main_menu_state_set_title (menu, "My Awesome Game");

/* Background image or color */
lrg_template_main_menu_state_set_background_texture (menu, bg_texture);
lrg_template_main_menu_state_set_background_color (menu, color);

/* Title position */
lrg_template_main_menu_state_set_title_position (menu, 0.5f, 0.2f);  /* Centered at 20% from top */

/* Menu position */
lrg_template_main_menu_state_set_menu_position (menu, 0.5f, 0.6f);  /* Centered at 60% from top */
```

---

## LrgTemplatePauseMenuState

The pause menu state provides an in-game pause overlay with resume, settings, and exit options.

### Construction

```c
LrgTemplatePauseMenuState *pause = lrg_template_pause_menu_state_new ();
```

### Virtual Methods

```c
struct _LrgTemplatePauseMenuStateClass
{
    LrgGameStateClass parent_class;

    /* Called when Resume is selected */
    void (*on_resume)    (LrgTemplatePauseMenuState *self);

    /* Called when Settings is selected */
    void (*on_settings)  (LrgTemplatePauseMenuState *self);

    /* Called when Main Menu is selected */
    void (*on_main_menu) (LrgTemplatePauseMenuState *self);

    /* Called when Exit is selected */
    void (*on_exit)      (LrgTemplatePauseMenuState *self);
};
```

### Menu Configuration

```c
/* Show/hide menu items */
lrg_template_pause_menu_state_set_show_resume (pause, TRUE);
lrg_template_pause_menu_state_set_show_settings (pause, TRUE);
lrg_template_pause_menu_state_set_show_main_menu (pause, TRUE);
lrg_template_pause_menu_state_set_show_exit (pause, TRUE);
```

### Overlay Appearance

```c
/* Overlay color (semi-transparent) */
g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 180);
lrg_template_pause_menu_state_set_overlay_color (pause, overlay);

/* Show game behind pause menu */
lrg_template_pause_menu_state_set_draw_game_behind (pause, TRUE);

/* Apply blur effect to game behind */
lrg_template_pause_menu_state_set_blur_background (pause, TRUE);
lrg_template_pause_menu_state_set_blur_strength (pause, 0.5f);
```

### Audio Ducking

```c
/* Lower game audio while paused */
lrg_template_pause_menu_state_set_duck_audio (pause, TRUE);
lrg_template_pause_menu_state_set_duck_amount (pause, 0.3f);  /* 30% volume */
```

### Quick Resume

```c
/* Allow quick resume via same key that opened pause menu */
lrg_template_pause_menu_state_set_toggle_key_resumes (pause, TRUE);
```

---

## LrgTemplateSettingsMenuState

A tabbed settings menu with built-in support for graphics, audio, and controls settings.

### Construction

```c
LrgTemplateSettingsMenuState *settings = lrg_template_settings_menu_state_new ();
```

### Virtual Methods

```c
struct _LrgTemplateSettingsMenuStateClass
{
    LrgGameStateClass parent_class;

    /* Create tab content (return NULL to skip tab) */
    LrgWidget * (*create_graphics_tab) (LrgTemplateSettingsMenuState *self);
    LrgWidget * (*create_audio_tab)    (LrgTemplateSettingsMenuState *self);
    LrgWidget * (*create_controls_tab) (LrgTemplateSettingsMenuState *self);
    LrgWidget * (*create_custom_tab)   (LrgTemplateSettingsMenuState *self,
                                        const gchar                  *tab_name);

    /* Button callbacks */
    void (*on_apply)  (LrgTemplateSettingsMenuState *self);
    void (*on_cancel) (LrgTemplateSettingsMenuState *self);
    void (*on_reset)  (LrgTemplateSettingsMenuState *self);
};
```

### Tab Visibility

```c
/* Show/hide built-in tabs */
lrg_template_settings_menu_state_set_show_graphics_tab (settings, TRUE);
lrg_template_settings_menu_state_set_show_audio_tab (settings, TRUE);
lrg_template_settings_menu_state_set_show_controls_tab (settings, TRUE);
```

### Custom Tabs

```c
/* Add custom tabs */
lrg_template_settings_menu_state_add_custom_tab (settings, "gameplay", "Gameplay");
lrg_template_settings_menu_state_add_custom_tab (settings, "accessibility", "Accessibility");

/* Override create_custom_tab to provide content */
static LrgWidget *
my_settings_create_custom_tab (LrgTemplateSettingsMenuState *state,
                               const gchar                  *tab_name)
{
    if (g_str_equal (tab_name, "gameplay"))
        return create_gameplay_settings_widget ();
    else if (g_str_equal (tab_name, "accessibility"))
        return create_accessibility_settings_widget ();

    return NULL;
}
```

### Button Configuration

```c
/* Show/hide Reset to Defaults button */
lrg_template_settings_menu_state_set_show_reset_button (settings, TRUE);

/* Require confirmation for destructive actions */
lrg_template_settings_menu_state_set_confirm_cancel (settings, TRUE);
lrg_template_settings_menu_state_set_confirm_reset (settings, TRUE);
```

### Dirty State

```c
/* Check for unsaved changes */
if (lrg_template_settings_menu_state_has_unsaved_changes (settings))
{
    /* Show confirmation before leaving */
}
```

### Active Tab

```c
/* Get/set current tab */
guint tab = lrg_template_settings_menu_state_get_active_tab (settings);
lrg_template_settings_menu_state_set_active_tab (settings, 2);  /* Controls tab */
```

---

## LrgTemplateLoadingState

A loading screen with progress tracking, task management, and visual feedback.

### Construction

```c
LrgTemplateLoadingState *loading = lrg_template_loading_state_new ();
```

### Virtual Methods

```c
struct _LrgTemplateLoadingStateClass
{
    LrgGameStateClass parent_class;

    /* Called when loading completes successfully */
    void (*on_complete) (LrgTemplateLoadingState *self);

    /* Called when a loading task fails */
    void (*on_failed)   (LrgTemplateLoadingState *self,
                         GError                  *error);
};
```

### Adding Loading Tasks

```c
/* Add individual tasks */
lrg_template_loading_state_add_task (loading, "Loading textures",
                                      load_textures_task, game, NULL);
lrg_template_loading_state_add_task (loading, "Loading sounds",
                                      load_sounds_task, game, NULL);
lrg_template_loading_state_add_task (loading, "Loading level",
                                      load_level_task, game, NULL);

/* Add asset to load via asset manager */
lrg_template_loading_state_add_asset (loading, "textures/player.png");
lrg_template_loading_state_add_asset (loading, "sounds/bgm.ogg");
```

### Task Callback

```c
typedef gboolean (*LrgLoadingTask) (gpointer   user_data,
                                    GError   **error);

static gboolean
load_textures_task (gpointer user_data, GError **error)
{
    Game *game = user_data;

    game->player_tex = grl_texture_new ("player.png");
    if (game->player_tex == NULL)
    {
        g_set_error (error, GAME_ERROR, GAME_ERROR_LOAD_FAILED,
                     "Failed to load player texture");
        return FALSE;
    }

    return TRUE;
}
```

### Progress Tracking

```c
/* Get progress (0.0 to 1.0) */
gdouble progress = lrg_template_loading_state_get_progress (loading);

/* Get current task name */
const gchar *current = lrg_template_loading_state_get_current_task_name (loading);

/* Check if complete */
if (lrg_template_loading_state_is_complete (loading))
{
    /* Transition to game state */
}

/* Task counts */
guint total = lrg_template_loading_state_get_task_count (loading);
guint done = lrg_template_loading_state_get_completed_count (loading);
```

### Minimum Display Time

```c
/* Prevent loading screen from flashing */
lrg_template_loading_state_set_minimum_display_time (loading, 2.0);  /* 2 seconds */
```

### Appearance

```c
/* Background color */
g_autoptr(GrlColor) bg = grl_color_new (20, 20, 30, 255);
lrg_template_loading_state_set_background_color (loading, bg);

/* Status text above progress bar */
lrg_template_loading_state_set_status_text (loading, "Preparing your adventure...");

/* Show/hide progress indicators */
lrg_template_loading_state_set_show_progress_bar (loading, TRUE);
lrg_template_loading_state_set_show_percentage (loading, TRUE);
```

---

## LrgTemplateErrorState

An error recovery screen with retry, main menu, and exit options.

### Construction

```c
/* Create empty */
LrgTemplateErrorState *error_state = lrg_template_error_state_new ();

/* Create with GError */
LrgTemplateErrorState *error_state = lrg_template_error_state_new_with_error (error);
```

### Virtual Methods

```c
struct _LrgTemplateErrorStateClass
{
    LrgGameStateClass parent_class;

    /* Called when Retry is selected */
    void (*on_retry)     (LrgTemplateErrorState *self);

    /* Called when Main Menu is selected */
    void (*on_main_menu) (LrgTemplateErrorState *self);

    /* Called when Exit is selected */
    void (*on_exit)      (LrgTemplateErrorState *self);
};
```

### Error Information

```c
/* Set error message */
lrg_template_error_state_set_error_message (error_state, "Failed to load save file");

/* Set from GError */
lrg_template_error_state_set_error (error_state, gerror);

/* Set title */
lrg_template_error_state_set_title (error_state, "Load Error");
```

### Button Visibility

```c
/* Configure which buttons are shown */
lrg_template_error_state_set_allow_retry (error_state, TRUE);
lrg_template_error_state_set_show_main_menu (error_state, TRUE);
lrg_template_error_state_set_show_exit (error_state, TRUE);
```

### Usage Example

```c
static void
on_loading_failed (LrgTemplateLoadingState *loading,
                   GError                  *error,
                   gpointer                 user_data)
{
    Game *game = user_data;
    LrgGameStateManager *manager = lrg_engine_get_state_manager (game->engine);

    /* Create error state */
    LrgTemplateErrorState *error_state = lrg_template_error_state_new_with_error (error);
    lrg_template_error_state_set_title (error_state, "Loading Failed");
    lrg_template_error_state_set_allow_retry (error_state, TRUE);

    /* Connect retry handler */
    g_signal_connect (error_state, "retry",
                      G_CALLBACK (on_retry_loading), game);

    /* Replace loading state with error state */
    lrg_game_state_manager_replace (manager, LRG_GAME_STATE (error_state));
}
```

---

## LrgTemplateConfirmationState

A generic confirmation dialog for destructive actions or important decisions.

### Construction

```c
/* Create empty */
LrgTemplateConfirmationState *confirm = lrg_template_confirmation_state_new ();

/* Create with message */
LrgTemplateConfirmationState *confirm =
    lrg_template_confirmation_state_new_with_message (
        "Delete Save?",
        "Are you sure you want to delete this save file? This cannot be undone.");
```

### Virtual Methods

```c
struct _LrgTemplateConfirmationStateClass
{
    LrgGameStateClass parent_class;

    /* Called when confirm is selected */
    void (*on_confirm) (LrgTemplateConfirmationState *self);

    /* Called when cancel is selected */
    void (*on_cancel)  (LrgTemplateConfirmationState *self);
};
```

### Text Content

```c
/* Set dialog text */
lrg_template_confirmation_state_set_title (confirm, "Quit Game?");
lrg_template_confirmation_state_set_message (confirm,
    "Any unsaved progress will be lost.");
```

### Button Labels

```c
/* Customize button text */
lrg_template_confirmation_state_set_confirm_label (confirm, "Delete");
lrg_template_confirmation_state_set_cancel_label (confirm, "Keep");
```

### Appearance

```c
/* Overlay behind dialog */
g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 200);
lrg_template_confirmation_state_set_overlay_color (confirm, overlay);

/* Dialog background */
g_autoptr(GrlColor) dialog_bg = grl_color_new (40, 40, 50, 255);
lrg_template_confirmation_state_set_dialog_color (confirm, dialog_bg);
```

### Default Selection

```c
/* Default to Cancel for destructive actions */
lrg_template_confirmation_state_set_default_selection (confirm, 1);  /* 0=confirm, 1=cancel */
```

### Usage Example

```c
static void
on_delete_save_clicked (Game *game, guint slot)
{
    LrgGameStateManager *manager = lrg_engine_get_state_manager (game->engine);

    LrgTemplateConfirmationState *confirm =
        lrg_template_confirmation_state_new_with_message (
            "Delete Save?",
            "This save file will be permanently deleted.");

    lrg_template_confirmation_state_set_confirm_label (confirm, "Delete");
    lrg_template_confirmation_state_set_default_selection (confirm, 1);

    g_object_set_data (G_OBJECT (confirm), "slot", GUINT_TO_POINTER (slot));
    g_signal_connect (confirm, "confirmed",
                      G_CALLBACK (on_delete_confirmed), game);

    lrg_game_state_manager_push (manager, LRG_GAME_STATE (confirm));
}

static void
on_delete_confirmed (LrgTemplateConfirmationState *confirm, gpointer user_data)
{
    Game *game = user_data;
    guint slot = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (confirm), "slot"));

    delete_save_file (game, slot);

    /* Pop confirmation dialog */
    LrgGameStateManager *manager = lrg_engine_get_state_manager (game->engine);
    lrg_game_state_manager_pop (manager);
}
```

---

## State Flow Example

```c
static void
start_new_game (Game *game)
{
    LrgGameStateManager *manager = lrg_engine_get_state_manager (game->engine);

    /* Create loading state */
    LrgTemplateLoadingState *loading = lrg_template_loading_state_new ();
    lrg_template_loading_state_add_task (loading, "Loading world",
                                          load_world_task, game, NULL);
    lrg_template_loading_state_set_minimum_display_time (loading, 1.0);

    /* Handle completion */
    g_signal_connect (loading, "complete",
                      G_CALLBACK (on_loading_complete), game);
    g_signal_connect (loading, "failed",
                      G_CALLBACK (on_loading_failed), game);

    /* Push loading state (main menu remains underneath) */
    lrg_game_state_manager_push (manager, LRG_GAME_STATE (loading));
}

static void
on_loading_complete (LrgTemplateLoadingState *loading, gpointer user_data)
{
    Game *game = user_data;
    LrgGameStateManager *manager = lrg_engine_get_state_manager (game->engine);

    /* Replace everything with game state */
    lrg_game_state_manager_clear (manager);
    lrg_game_state_manager_push (manager, game->gameplay_state);
}
```

## Related Documentation

- [LrgGameState](../../gamestate/index.md) - Base game state
- [LrgGameStateManager](../../gamestate/index.md) - State stack management
- [Settings System](../../settings/index.md) - Persisted settings
