/* game-settings-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Phase 1 Example: Steam-Ready Minimum
 * Demonstrates: GameState, Settings, Accessibility, CrashReporter
 *
 * A simple Pong game with full menu system and settings.
 *
 * Controls:
 *   W/S or UP/DOWN - Move paddle
 *   P or ESC       - Pause
 *   ENTER/SPACE    - Select
 */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

/* ===== Constants ===== */

#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600
#define PADDLE_WIDTH    15
#define PADDLE_HEIGHT   80
#define PADDLE_SPEED    400.0
#define BALL_SIZE       12
#define BALL_SPEED      350.0
#define AI_SPEED        280.0
#define WIN_SCORE       5

/* ===== Global State ===== */

static LrgGameStateManager *g_state_manager = NULL;
static LrgSettings *g_settings = NULL;
static GrlWindow *g_window = NULL;

/* ===== Gameplay State ===== */

#define DEMO_TYPE_GAMEPLAY_STATE (demo_gameplay_state_get_type())
G_DECLARE_FINAL_TYPE (DemoGameplayState, demo_gameplay_state, DEMO, GAMEPLAY_STATE, LrgGameState)

struct _DemoGameplayState
{
    LrgGameState parent_instance;
    gdouble player_y;
    gdouble ai_y;
    gdouble ball_x;
    gdouble ball_y;
    gdouble ball_vx;
    gdouble ball_vy;
    gint player_score;
    gint ai_score;
    gboolean ball_active;
    gdouble countdown;
};

G_DEFINE_TYPE (DemoGameplayState, demo_gameplay_state, LRG_TYPE_GAME_STATE)

static void gameplay_reset_ball (DemoGameplayState *self);

static void
demo_gameplay_state_enter (LrgGameState *state)
{
    DemoGameplayState *self = DEMO_GAMEPLAY_STATE (state);

    self->player_y = WINDOW_HEIGHT / 2.0 - PADDLE_HEIGHT / 2.0;
    self->ai_y = WINDOW_HEIGHT / 2.0 - PADDLE_HEIGHT / 2.0;
    self->player_score = 0;
    self->ai_score = 0;
    gameplay_reset_ball (self);
}

static void
gameplay_reset_ball (DemoGameplayState *self)
{
    gdouble angle;
    gdouble direction;

    self->ball_x = WINDOW_WIDTH / 2.0;
    self->ball_y = WINDOW_HEIGHT / 2.0;

    angle = ((gdouble)g_random_int_range (-45, 45)) * G_PI / 180.0;
    direction = g_random_boolean () ? 1.0 : -1.0;

    self->ball_vx = cos (angle) * BALL_SPEED * direction;
    self->ball_vy = sin (angle) * BALL_SPEED;
    self->ball_active = FALSE;
    self->countdown = 2.0;
}

static void
demo_gameplay_state_update (LrgGameState *state,
                            gdouble       delta)
{
    DemoGameplayState *self = DEMO_GAMEPLAY_STATE (state);
    gdouble ai_center;
    gdouble target;
    gdouble player_x;
    gdouble ai_x;
    gdouble hit_pos;

    /* Pause on ESC or P */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE) || grl_input_is_key_pressed (GRL_KEY_P))
    {
        lrg_game_state_manager_pop (g_state_manager);
        return;
    }

    /* Countdown */
    if (!self->ball_active)
    {
        self->countdown -= delta;
        if (self->countdown <= 0.0)
            self->ball_active = TRUE;
        return;
    }

    /* Player input */
    if (grl_input_is_key_down (GRL_KEY_W) || grl_input_is_key_down (GRL_KEY_UP))
        self->player_y -= PADDLE_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_S) || grl_input_is_key_down (GRL_KEY_DOWN))
        self->player_y += PADDLE_SPEED * delta;

    /* Clamp player */
    if (self->player_y < 0)
        self->player_y = 0;
    if (self->player_y > WINDOW_HEIGHT - PADDLE_HEIGHT)
        self->player_y = WINDOW_HEIGHT - PADDLE_HEIGHT;

    /* AI paddle */
    ai_center = self->ai_y + PADDLE_HEIGHT / 2.0;
    target = self->ball_y;

    if (ai_center < target - 10)
        self->ai_y += AI_SPEED * delta;
    else if (ai_center > target + 10)
        self->ai_y -= AI_SPEED * delta;

    if (self->ai_y < 0)
        self->ai_y = 0;
    if (self->ai_y > WINDOW_HEIGHT - PADDLE_HEIGHT)
        self->ai_y = WINDOW_HEIGHT - PADDLE_HEIGHT;

    /* Ball movement */
    self->ball_x += self->ball_vx * delta;
    self->ball_y += self->ball_vy * delta;

    /* Top/bottom bounce */
    if (self->ball_y <= 0 || self->ball_y >= WINDOW_HEIGHT - BALL_SIZE)
    {
        self->ball_vy = -self->ball_vy;
        if (self->ball_y < 0)
            self->ball_y = 0;
        if (self->ball_y > WINDOW_HEIGHT - BALL_SIZE)
            self->ball_y = WINDOW_HEIGHT - BALL_SIZE;
    }

    /* Player paddle collision */
    player_x = 30.0;
    if (self->ball_x <= player_x + PADDLE_WIDTH &&
        self->ball_x >= player_x &&
        self->ball_y + BALL_SIZE >= self->player_y &&
        self->ball_y <= self->player_y + PADDLE_HEIGHT)
    {
        self->ball_vx = fabs (self->ball_vx);
        hit_pos = (self->ball_y - self->player_y) / PADDLE_HEIGHT;
        self->ball_vy = (hit_pos - 0.5) * BALL_SPEED * 1.5;
    }

    /* AI paddle collision */
    ai_x = WINDOW_WIDTH - 30.0 - PADDLE_WIDTH;
    if (self->ball_x + BALL_SIZE >= ai_x &&
        self->ball_x <= ai_x + PADDLE_WIDTH &&
        self->ball_y + BALL_SIZE >= self->ai_y &&
        self->ball_y <= self->ai_y + PADDLE_HEIGHT)
    {
        self->ball_vx = -fabs (self->ball_vx);
        hit_pos = (self->ball_y - self->ai_y) / PADDLE_HEIGHT;
        self->ball_vy = (hit_pos - 0.5) * BALL_SPEED * 1.5;
    }

    /* Scoring */
    if (self->ball_x < 0)
    {
        self->ai_score++;
        if (self->ai_score >= WIN_SCORE)
        {
            lrg_game_state_manager_pop (g_state_manager);
            return;
        }
        gameplay_reset_ball (self);
    }
    else if (self->ball_x > WINDOW_WIDTH)
    {
        self->player_score++;
        if (self->player_score >= WIN_SCORE)
        {
            lrg_game_state_manager_pop (g_state_manager);
            return;
        }
        gameplay_reset_ball (self);
    }
}

static void
demo_gameplay_state_draw (LrgGameState *state)
{
    DemoGameplayState *self = DEMO_GAMEPLAY_STATE (state);
    gint y;
    gint width;
    g_autofree gchar *player_str = NULL;
    g_autofree gchar *ai_str = NULL;
    g_autofree gchar *countdown_str = NULL;
    g_autoptr (GrlColor) dim = grl_color_new (120, 120, 140, 255);
    g_autoptr (GrlColor) fg = grl_color_new (240, 240, 250, 255);
    g_autoptr (GrlColor) accent = grl_color_new (100, 180, 255, 255);
    g_autoptr (GrlColor) ball_color = grl_color_new (255, 100, 100, 255);

    /* Center line */
    for (y = 0; y < WINDOW_HEIGHT; y += 30)
    {
        grl_draw_rectangle (WINDOW_WIDTH / 2 - 2, y, 4, 15, dim);
    }

    /* Scores */
    player_str = g_strdup_printf ("%d", self->player_score);
    ai_str = g_strdup_printf ("%d", self->ai_score);
    grl_draw_text (player_str, WINDOW_WIDTH / 4, 30, 60, dim);
    grl_draw_text (ai_str, 3 * WINDOW_WIDTH / 4, 30, 60, dim);

    /* Player paddle */
    grl_draw_rectangle (30, (gint)self->player_y, PADDLE_WIDTH, PADDLE_HEIGHT, accent);

    /* AI paddle */
    grl_draw_rectangle (WINDOW_WIDTH - 30 - PADDLE_WIDTH, (gint)self->ai_y, PADDLE_WIDTH, PADDLE_HEIGHT, fg);

    /* Ball */
    grl_draw_rectangle ((gint)self->ball_x, (gint)self->ball_y, BALL_SIZE, BALL_SIZE, ball_color);

    /* Countdown */
    if (!self->ball_active)
    {
        countdown_str = g_strdup_printf ("%.0f", ceil (self->countdown));
        width = grl_measure_text (countdown_str, 80);
        grl_draw_text (countdown_str, (WINDOW_WIDTH - width) / 2, WINDOW_HEIGHT / 2 - 40, 80, fg);
    }

    /* Instructions */
    grl_draw_text ("W/S or UP/DOWN to move | ESC to quit", 180, WINDOW_HEIGHT - 30, 16, dim);
}

static void
demo_gameplay_state_class_init (DemoGameplayStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = demo_gameplay_state_enter;
    state_class->update = demo_gameplay_state_update;
    state_class->draw = demo_gameplay_state_draw;
}

static void
demo_gameplay_state_init (DemoGameplayState *self)
{
    self->player_y = 0;
    self->ai_y = 0;
    self->ball_x = 0;
    self->ball_y = 0;
    self->ball_vx = 0;
    self->ball_vy = 0;
    self->player_score = 0;
    self->ai_score = 0;
    self->ball_active = FALSE;
    self->countdown = 0;
}

/* ===== Settings State ===== */

#define DEMO_TYPE_SETTINGS_STATE (demo_settings_state_get_type())
G_DECLARE_FINAL_TYPE (DemoSettingsState, demo_settings_state, DEMO, SETTINGS_STATE, LrgGameState)

#define TAB_GRAPHICS      0
#define TAB_AUDIO         1
#define TAB_ACCESSIBILITY 2
#define TAB_COUNT         3

struct _DemoSettingsState
{
    LrgGameState parent_instance;
    gint current_tab;
    gint selected_row;

    /* Graphics */
    gint resolution_idx;
    gboolean fullscreen;
    gboolean vsync;
    gint quality_preset;

    /* Audio */
    gfloat master_volume;
    gfloat music_volume;
    gfloat sfx_volume;
    gboolean muted;

    /* Accessibility */
    gint colorblind_mode;
    gfloat ui_scale;
    gboolean subtitles;
    gboolean screen_shake;
};

static const gchar *tab_names[TAB_COUNT] = { "Graphics", "Audio", "Accessibility" };
static const gchar *resolution_options[] = { "800x600", "1280x720", "1920x1080", "2560x1440" };
static const gchar *quality_options[] = { "Low", "Medium", "High", "Ultra" };
static const gchar *colorblind_options[] = { "None", "Deuteranopia", "Protanopia", "Tritanopia" };

#define RESOLUTION_COUNT 4
#define QUALITY_COUNT    4
#define COLORBLIND_COUNT 4

G_DEFINE_TYPE (DemoSettingsState, demo_settings_state, LRG_TYPE_GAME_STATE)

static void
settings_load (DemoSettingsState *self)
{
    LrgGraphicsSettings *gfx = lrg_settings_get_graphics (g_settings);
    LrgAudioSettings *audio = lrg_settings_get_audio (g_settings);

    self->fullscreen = (lrg_graphics_settings_get_fullscreen_mode (gfx) != LRG_FULLSCREEN_WINDOWED);
    self->vsync = lrg_graphics_settings_get_vsync (gfx);
    self->quality_preset = lrg_graphics_settings_get_quality_preset (gfx);
    self->resolution_idx = 1;

    self->master_volume = lrg_audio_settings_get_master_volume (audio);
    self->music_volume = lrg_audio_settings_get_music_volume (audio);
    self->sfx_volume = lrg_audio_settings_get_sfx_volume (audio);
    self->muted = lrg_audio_settings_get_muted (audio);

    self->colorblind_mode = 0;
    self->ui_scale = 1.0f;
    self->subtitles = TRUE;
    self->screen_shake = TRUE;
}

static void
settings_save (DemoSettingsState *self)
{
    LrgGraphicsSettings *gfx = lrg_settings_get_graphics (g_settings);
    LrgAudioSettings *audio = lrg_settings_get_audio (g_settings);
    g_autoptr (GError) error = NULL;

    lrg_graphics_settings_set_fullscreen_mode (gfx, self->fullscreen ? LRG_FULLSCREEN_FULLSCREEN : LRG_FULLSCREEN_WINDOWED);
    lrg_graphics_settings_set_vsync (gfx, self->vsync);
    lrg_graphics_settings_set_quality_preset (gfx, self->quality_preset);

    lrg_audio_settings_set_master_volume (audio, self->master_volume);
    lrg_audio_settings_set_music_volume (audio, self->music_volume);
    lrg_audio_settings_set_sfx_volume (audio, self->sfx_volume);
    lrg_audio_settings_set_muted (audio, self->muted);

    if (!lrg_settings_save_default_path (g_settings, "settings-demo", &error))
        g_warning ("Failed to save settings: %s", error->message);
}

static void
demo_settings_state_enter (LrgGameState *state)
{
    DemoSettingsState *self = DEMO_SETTINGS_STATE (state);

    self->current_tab = TAB_GRAPHICS;
    self->selected_row = 0;
    settings_load (self);
}

static void
demo_settings_state_exit (LrgGameState *state)
{
    DemoSettingsState *self = DEMO_SETTINGS_STATE (state);

    settings_save (self);
}

static gint
get_row_count (DemoSettingsState *self)
{
    (void)self;
    return 4;
}

static void
demo_settings_state_update (LrgGameState *state,
                            gdouble       delta)
{
    DemoSettingsState *self = DEMO_SETTINGS_STATE (state);
    gint row_count;
    gboolean left_pressed;
    gboolean right_pressed;
    gboolean toggle;

    (void)delta;

    /* Tab navigation with A/D */
    if (grl_input_is_key_pressed (GRL_KEY_A))
    {
        self->current_tab = (self->current_tab - 1 + TAB_COUNT) % TAB_COUNT;
        self->selected_row = 0;
    }
    if (grl_input_is_key_pressed (GRL_KEY_D))
    {
        self->current_tab = (self->current_tab + 1) % TAB_COUNT;
        self->selected_row = 0;
    }

    /* Row navigation */
    row_count = get_row_count (self);
    if (grl_input_is_key_pressed (GRL_KEY_UP) || grl_input_is_key_pressed (GRL_KEY_W))
        self->selected_row = (self->selected_row - 1 + row_count) % row_count;
    if (grl_input_is_key_pressed (GRL_KEY_DOWN) || grl_input_is_key_pressed (GRL_KEY_S))
        self->selected_row = (self->selected_row + 1) % row_count;

    /* Value adjustment */
    left_pressed = grl_input_is_key_pressed (GRL_KEY_LEFT);
    right_pressed = grl_input_is_key_pressed (GRL_KEY_RIGHT);
    toggle = grl_input_is_key_pressed (GRL_KEY_ENTER) || grl_input_is_key_pressed (GRL_KEY_SPACE);

    switch (self->current_tab)
    {
    case TAB_GRAPHICS:
        if (self->selected_row == 0)
        {
            if (left_pressed) self->resolution_idx = (self->resolution_idx - 1 + RESOLUTION_COUNT) % RESOLUTION_COUNT;
            if (right_pressed) self->resolution_idx = (self->resolution_idx + 1) % RESOLUTION_COUNT;
        }
        else if (self->selected_row == 1 && (toggle || left_pressed || right_pressed))
            self->fullscreen = !self->fullscreen;
        else if (self->selected_row == 2 && (toggle || left_pressed || right_pressed))
            self->vsync = !self->vsync;
        else if (self->selected_row == 3)
        {
            if (left_pressed) self->quality_preset = (self->quality_preset - 1 + QUALITY_COUNT) % QUALITY_COUNT;
            if (right_pressed) self->quality_preset = (self->quality_preset + 1) % QUALITY_COUNT;
        }
        break;

    case TAB_AUDIO:
        if (self->selected_row == 0)
        {
            if (left_pressed) self->master_volume = fmaxf (0.0f, self->master_volume - 0.1f);
            if (right_pressed) self->master_volume = fminf (1.0f, self->master_volume + 0.1f);
        }
        else if (self->selected_row == 1)
        {
            if (left_pressed) self->music_volume = fmaxf (0.0f, self->music_volume - 0.1f);
            if (right_pressed) self->music_volume = fminf (1.0f, self->music_volume + 0.1f);
        }
        else if (self->selected_row == 2)
        {
            if (left_pressed) self->sfx_volume = fmaxf (0.0f, self->sfx_volume - 0.1f);
            if (right_pressed) self->sfx_volume = fminf (1.0f, self->sfx_volume + 0.1f);
        }
        else if (self->selected_row == 3 && (toggle || left_pressed || right_pressed))
            self->muted = !self->muted;
        break;

    case TAB_ACCESSIBILITY:
        if (self->selected_row == 0)
        {
            if (left_pressed) self->colorblind_mode = (self->colorblind_mode - 1 + COLORBLIND_COUNT) % COLORBLIND_COUNT;
            if (right_pressed) self->colorblind_mode = (self->colorblind_mode + 1) % COLORBLIND_COUNT;
        }
        else if (self->selected_row == 1)
        {
            if (left_pressed) self->ui_scale = fmaxf (0.5f, self->ui_scale - 0.1f);
            if (right_pressed) self->ui_scale = fminf (2.0f, self->ui_scale + 0.1f);
        }
        else if (self->selected_row == 2 && (toggle || left_pressed || right_pressed))
            self->subtitles = !self->subtitles;
        else if (self->selected_row == 3 && (toggle || left_pressed || right_pressed))
            self->screen_shake = !self->screen_shake;
        break;
    }

    /* Back */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
        lrg_game_state_manager_pop (g_state_manager);
}

static void
draw_slider (gint x, gint y, gint width, gfloat value)
{
    g_autoptr (GrlColor) track = grl_color_new (60, 60, 80, 255);
    g_autoptr (GrlColor) fill = grl_color_new (100, 180, 255, 255);

    grl_draw_rectangle (x, y, width, 8, track);
    grl_draw_rectangle (x, y, (gint)(width * value), 8, fill);
}

static void
demo_settings_state_draw (LrgGameState *state)
{
    DemoSettingsState *self = DEMO_SETTINGS_STATE (state);
    gint title_width;
    gint tab_x;
    gint i;
    gint content_y;
    gint label_x;
    gint value_x;
    gint row_height;
    const gchar *labels_gfx[] = { "Resolution:", "Fullscreen:", "VSync:", "Quality:" };
    const gchar *labels_audio[] = { "Master Volume:", "Music Volume:", "SFX Volume:", "Mute All:" };
    const gchar *labels_access[] = { "Colorblind Mode:", "UI Scale:", "Subtitles:", "Screen Shake:" };
    const gchar **labels;
    g_autofree gchar *pct_str = NULL;
    g_autoptr (GrlColor) fg = grl_color_new (240, 240, 250, 255);
    g_autoptr (GrlColor) dim = grl_color_new (120, 120, 140, 255);
    g_autoptr (GrlColor) accent = grl_color_new (100, 180, 255, 255);
    g_autoptr (GrlColor) selected = grl_color_new (255, 200, 100, 255);
    g_autoptr (GrlColor) line_color = grl_color_new (80, 80, 100, 255);

    /* Title */
    title_width = grl_measure_text ("SETTINGS", 40);
    grl_draw_text ("SETTINGS", (WINDOW_WIDTH - title_width) / 2, 40, 40, accent);

    /* Tabs */
    tab_x = 100;
    for (i = 0; i < TAB_COUNT; i++)
    {
        GrlColor *color = (i == self->current_tab) ? selected : dim;
        grl_draw_text (tab_names[i], tab_x, 100, 24, color);
        tab_x += grl_measure_text (tab_names[i], 24) + 50;
    }

    /* Separator */
    grl_draw_rectangle (50, 135, WINDOW_WIDTH - 100, 2, line_color);

    /* Content */
    content_y = 160;
    label_x = 100;
    value_x = 400;
    row_height = 45;

    switch (self->current_tab)
    {
    case TAB_GRAPHICS:
        labels = labels_gfx;
        break;
    case TAB_AUDIO:
        labels = labels_audio;
        break;
    case TAB_ACCESSIBILITY:
        labels = labels_access;
        break;
    default:
        labels = labels_gfx;
        break;
    }

    for (i = 0; i < 4; i++)
    {
        GrlColor *color = (i == self->selected_row) ? selected : fg;
        grl_draw_text (labels[i], label_x, content_y + i * row_height, 20, color);

        if (i == self->selected_row)
            grl_draw_text (">", label_x - 25, content_y + i * row_height, 20, selected);
    }

    /* Values */
    switch (self->current_tab)
    {
    case TAB_GRAPHICS:
        grl_draw_text (resolution_options[self->resolution_idx], value_x, content_y, 20, fg);
        grl_draw_text (self->fullscreen ? "ON" : "OFF", value_x, content_y + row_height, 20, fg);
        grl_draw_text (self->vsync ? "ON" : "OFF", value_x, content_y + 2 * row_height, 20, fg);
        grl_draw_text (quality_options[self->quality_preset], value_x, content_y + 3 * row_height, 20, fg);
        break;

    case TAB_AUDIO:
        draw_slider (value_x, content_y + 6, 200, self->master_volume);
        draw_slider (value_x, content_y + row_height + 6, 200, self->music_volume);
        draw_slider (value_x, content_y + 2 * row_height + 6, 200, self->sfx_volume);
        grl_draw_text (self->muted ? "YES" : "NO", value_x, content_y + 3 * row_height, 20, fg);

        pct_str = g_strdup_printf ("%.0f%%", self->master_volume * 100);
        grl_draw_text (pct_str, value_x + 220, content_y, 20, dim);
        g_free (pct_str);
        pct_str = g_strdup_printf ("%.0f%%", self->music_volume * 100);
        grl_draw_text (pct_str, value_x + 220, content_y + row_height, 20, dim);
        g_free (pct_str);
        pct_str = g_strdup_printf ("%.0f%%", self->sfx_volume * 100);
        grl_draw_text (pct_str, value_x + 220, content_y + 2 * row_height, 20, dim);
        pct_str = NULL;
        break;

    case TAB_ACCESSIBILITY:
        grl_draw_text (colorblind_options[self->colorblind_mode], value_x, content_y, 20, fg);
        pct_str = g_strdup_printf ("%.0f%%", self->ui_scale * 100);
        grl_draw_text (pct_str, value_x, content_y + row_height, 20, fg);
        g_free (pct_str);
        pct_str = NULL;
        grl_draw_text (self->subtitles ? "ON" : "OFF", value_x, content_y + 2 * row_height, 20, fg);
        grl_draw_text (self->screen_shake ? "ON" : "OFF", value_x, content_y + 3 * row_height, 20, fg);
        break;
    }

    /* Instructions */
    grl_draw_text ("A/D: Tabs | W/S: Navigate | LEFT/RIGHT: Adjust | ESC: Back", 100, WINDOW_HEIGHT - 50, 16, dim);
}

static void
demo_settings_state_class_init (DemoSettingsStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = demo_settings_state_enter;
    state_class->exit = demo_settings_state_exit;
    state_class->update = demo_settings_state_update;
    state_class->draw = demo_settings_state_draw;
}

static void
demo_settings_state_init (DemoSettingsState *self)
{
    self->current_tab = 0;
    self->selected_row = 0;
    self->resolution_idx = 1;
    self->fullscreen = FALSE;
    self->vsync = TRUE;
    self->quality_preset = 2;
    self->master_volume = 1.0f;
    self->music_volume = 0.8f;
    self->sfx_volume = 0.8f;
    self->muted = FALSE;
    self->colorblind_mode = 0;
    self->ui_scale = 1.0f;
    self->subtitles = TRUE;
    self->screen_shake = TRUE;
}

/* ===== Main Menu State ===== */

#define DEMO_TYPE_MAIN_MENU_STATE (demo_main_menu_state_get_type())
G_DECLARE_FINAL_TYPE (DemoMainMenuState, demo_main_menu_state, DEMO, MAIN_MENU_STATE, LrgGameState)

typedef enum {
    MENU_NEW_GAME,
    MENU_SETTINGS,
    MENU_QUIT,
    MENU_ITEM_COUNT
} MenuItem;

static const gchar *menu_labels[MENU_ITEM_COUNT] = { "New Game", "Settings", "Quit" };

struct _DemoMainMenuState
{
    LrgGameState parent_instance;
    gint selected;
    gdouble title_bob;
    gboolean quit_requested;
};

G_DEFINE_TYPE (DemoMainMenuState, demo_main_menu_state, LRG_TYPE_GAME_STATE)

static void
demo_main_menu_state_enter (LrgGameState *state)
{
    DemoMainMenuState *self = DEMO_MAIN_MENU_STATE (state);

    self->selected = MENU_NEW_GAME;
    self->title_bob = 0.0;
    self->quit_requested = FALSE;
}

static void
demo_main_menu_state_update (LrgGameState *state,
                             gdouble       delta)
{
    DemoMainMenuState *self = DEMO_MAIN_MENU_STATE (state);
    LrgGameState *new_state;

    self->title_bob += delta * 2.0;

    /* Navigate */
    if (grl_input_is_key_pressed (GRL_KEY_UP) || grl_input_is_key_pressed (GRL_KEY_W))
        self->selected = (self->selected - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
    if (grl_input_is_key_pressed (GRL_KEY_DOWN) || grl_input_is_key_pressed (GRL_KEY_S))
        self->selected = (self->selected + 1) % MENU_ITEM_COUNT;

    /* Select */
    if (grl_input_is_key_pressed (GRL_KEY_ENTER) || grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        switch (self->selected)
        {
        case MENU_NEW_GAME:
            /* Note: push takes ownership (transfer full), do NOT unref */
            new_state = g_object_new (DEMO_TYPE_GAMEPLAY_STATE, NULL);
            lrg_game_state_manager_push (g_state_manager, new_state);
            break;
        case MENU_SETTINGS:
            /* Note: push takes ownership (transfer full), do NOT unref */
            new_state = g_object_new (DEMO_TYPE_SETTINGS_STATE, NULL);
            lrg_game_state_manager_push (g_state_manager, new_state);
            break;
        case MENU_QUIT:
            self->quit_requested = TRUE;
            break;
        }
    }
}

static void
demo_main_menu_state_draw (LrgGameState *state)
{
    DemoMainMenuState *self = DEMO_MAIN_MENU_STATE (state);
    gint title_width;
    gint title_y;
    gint sub_width;
    gint i;
    gint item_y;
    gint item_width;
    gint item_x;
    g_autoptr (GrlColor) fg = grl_color_new (240, 240, 250, 255);
    g_autoptr (GrlColor) dim = grl_color_new (120, 120, 140, 255);
    g_autoptr (GrlColor) accent = grl_color_new (100, 180, 255, 255);
    g_autoptr (GrlColor) selected = grl_color_new (255, 200, 100, 255);
    GrlColor *color;

    /* Title */
    title_width = grl_measure_text ("PONG DEMO", 48);
    title_y = 100 + (gint)(sin (self->title_bob) * 5.0);
    grl_draw_text ("PONG DEMO", (WINDOW_WIDTH - title_width) / 2, title_y, 48, accent);

    /* Subtitle */
    sub_width = grl_measure_text ("Settings Demo - Phase 1", 20);
    grl_draw_text ("Settings Demo - Phase 1", (WINDOW_WIDTH - sub_width) / 2, 160, 20, dim);

    /* Menu */
    for (i = 0; i < MENU_ITEM_COUNT; i++)
    {
        item_y = 250 + i * 50;
        item_width = grl_measure_text (menu_labels[i], 28);
        item_x = (WINDOW_WIDTH - item_width) / 2;

        color = (i == self->selected) ? selected : fg;
        grl_draw_text (menu_labels[i], item_x, item_y, 28, color);

        if (i == self->selected)
        {
            grl_draw_text (">", item_x - 30, item_y, 28, selected);
            grl_draw_text ("<", item_x + item_width + 10, item_y, 28, selected);
        }
    }

    /* Instructions */
    grl_draw_text ("UP/DOWN to navigate, ENTER to select", 220, WINDOW_HEIGHT - 50, 16, dim);
}

static void
demo_main_menu_state_class_init (DemoMainMenuStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = demo_main_menu_state_enter;
    state_class->update = demo_main_menu_state_update;
    state_class->draw = demo_main_menu_state_draw;
}

static void
demo_main_menu_state_init (DemoMainMenuState *self)
{
    self->selected = 0;
    self->title_bob = 0.0;
    self->quit_requested = FALSE;
}

/* ===== Main ===== */

int
main (int    argc,
      char **argv)
{
    g_autoptr (GError) error = NULL;
    LrgCrashReporter *crash_reporter;
    LrgGameState *menu_state;
    LrgGameState *current;
    gfloat delta;
    g_autoptr (GrlColor) bg = NULL;

    (void)argc;
    (void)argv;

    /* Crash reporter */
    crash_reporter = lrg_crash_reporter_get_default ();
    lrg_crash_reporter_set_app_name (crash_reporter, "Settings Demo");
    lrg_crash_reporter_set_app_version (crash_reporter, "1.0.0");
    if (!lrg_crash_reporter_install (crash_reporter, &error))
    {
        g_warning ("Failed to install crash reporter: %s", error->message);
        g_clear_error (&error);
    }

    /* Settings */
    g_settings = lrg_settings_new ();
    if (!lrg_settings_load_default_path (g_settings, "settings-demo", &error))
    {
        g_message ("Using default settings: %s", error ? error->message : "first run");
        g_clear_error (&error);
    }

    /* Window */
    g_window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "Settings Demo - Phase 1");
    grl_window_set_target_fps (g_window, 60);

    /* Background color */
    bg = grl_color_new (20, 20, 30, 255);

    /* State manager - push takes ownership (transfer full), do NOT unref */
    g_state_manager = lrg_game_state_manager_new ();
    menu_state = g_object_new (DEMO_TYPE_MAIN_MENU_STATE, NULL);
    lrg_game_state_manager_push (g_state_manager, menu_state);

    /* Main loop */
    while (!grl_window_should_close (g_window))
    {
        delta = grl_window_get_frame_time (g_window);

        /* Check quit */
        current = lrg_game_state_manager_get_current (g_state_manager);
        if (DEMO_IS_MAIN_MENU_STATE (current))
        {
            DemoMainMenuState *menu = DEMO_MAIN_MENU_STATE (current);
            if (menu->quit_requested)
                break;
        }

        /* Update */
        lrg_game_state_manager_update (g_state_manager, (gdouble)delta);

        /* Draw */
        grl_window_begin_drawing (g_window);
        grl_draw_clear_background (bg);
        lrg_game_state_manager_draw (g_state_manager);
        grl_draw_fps (WINDOW_WIDTH - 80, 10);
        grl_window_end_drawing (g_window);
    }

    /* Cleanup */
    lrg_game_state_manager_clear (g_state_manager);
    g_clear_object (&g_state_manager);

    if (!lrg_settings_save_default_path (g_settings, "settings-demo", &error))
        g_warning ("Failed to save settings: %s", error->message);
    g_clear_object (&g_settings);

    lrg_crash_reporter_uninstall (crash_reporter);
    g_clear_object (&g_window);

    return 0;
}
