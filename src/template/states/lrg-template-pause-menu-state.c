/* lrg-template-pause-menu-state.c - Pause menu state implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../../config.h"
#include "lrg-template-pause-menu-state.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-widget.h"
#include "../../ui/lrg-container.h"
#include "../../audio/lrg-audio-manager.h"
#include "../../core/lrg-engine.h"
#include "../../graphics/lrg-window.h"
#include "../../lrg-log.h"

/**
 * SECTION:lrg-template-pause-menu-state
 * @title: LrgTemplatePauseMenuState
 * @short_description: Pause menu state with audio ducking
 *
 * #LrgTemplatePauseMenuState provides a pause menu overlay with:
 * - Semi-transparent background overlay
 * - Audio ducking (reduces volume while paused)
 * - Resume, Settings, Main Menu, Exit buttons
 * - Optional confirmation dialogs
 *
 * This state is designed to be pushed on top of gameplay states,
 * which is why it sets `transparent = TRUE` by default.
 *
 * ## Audio Ducking
 *
 * When the pause menu is shown, audio volume is automatically reduced
 * (ducked) to a configurable level. When the menu is closed, the original
 * volume is restored.
 *
 * ```c
 * LrgTemplatePauseMenuState *pause = lrg_template_pause_menu_state_new ();
 * g_object_set (pause,
 *               "duck-factor", 0.3f,  // 30% volume while paused
 *               NULL);
 * ```
 */

typedef struct _LrgTemplatePauseMenuStatePrivate
{
    /* Audio ducking */
    gboolean       duck_audio;
    gfloat         duck_factor;
    gfloat         saved_master_volume;
    gfloat         saved_sfx_volume;
    gfloat         saved_music_volume;

    /* Overlay */
    GrlColor      *overlay_color;

    /* Button visibility */
    gboolean       show_settings;
    gboolean       show_main_menu;
    gboolean       show_exit;

    /* Confirmation */
    gboolean       confirm_main_menu;
    gboolean       confirm_exit;

    /* UI Components */
    LrgCanvas     *canvas;
    LrgVBox       *menu_container;
    LrgLabel      *title_label;
    LrgButton     *btn_resume;
    LrgButton     *btn_settings;
    LrgButton     *btn_main_menu;
    LrgButton     *btn_exit;

    /* Navigation */
    gint           selected_index;
    GPtrArray     *visible_buttons;

    /* State */
    gboolean       menu_built;
} LrgTemplatePauseMenuStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTemplatePauseMenuState, lrg_template_pause_menu_state, LRG_TYPE_GAME_STATE)

enum
{
    PROP_0,
    PROP_DUCK_AUDIO,
    PROP_DUCK_FACTOR,
    PROP_OVERLAY_COLOR,
    PROP_SHOW_SETTINGS,
    PROP_SHOW_MAIN_MENU,
    PROP_SHOW_EXIT,
    PROP_CONFIRM_MAIN_MENU,
    PROP_CONFIRM_EXIT,
    PROP_SELECTED_INDEX,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_RESUME,
    SIGNAL_SETTINGS,
    SIGNAL_MAIN_MENU,
    SIGNAL_EXIT_GAME,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default values */
#define DEFAULT_DUCK_FACTOR       (0.2f)
#define DEFAULT_BUTTON_WIDTH      (250.0f)
#define DEFAULT_BUTTON_HEIGHT     (45.0f)

/* ========================================================================== */
/* Private Helpers                                                            */
/* ========================================================================== */

static void
update_button_selection (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;
    guint i;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (priv->visible_buttons == NULL)
        return;

    for (i = 0; i < priv->visible_buttons->len; i++)
    {
        LrgButton *btn = g_ptr_array_index (priv->visible_buttons, i);

        if ((gint)i == priv->selected_index)
        {
            g_autoptr(GrlColor) hover_color = grl_color_new (100, 150, 255, 255);
            lrg_button_set_normal_color (btn, hover_color);
        }
        else
        {
            g_autoptr(GrlColor) normal_color = grl_color_new (80, 80, 80, 230);
            lrg_button_set_normal_color (btn, normal_color);
        }
    }
}

static void
navigate_menu (LrgTemplatePauseMenuState *self,
               gint                       direction)
{
    LrgTemplatePauseMenuStatePrivate *priv;
    gint new_index;
    gint count;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (priv->visible_buttons == NULL || priv->visible_buttons->len == 0)
        return;

    count = (gint)priv->visible_buttons->len;
    new_index = priv->selected_index + direction;

    /* Wrap around */
    if (new_index < 0)
        new_index = count - 1;
    else if (new_index >= count)
        new_index = 0;

    priv->selected_index = new_index;
    update_button_selection (self);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED_INDEX]);
}

static void
activate_selected (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;
    LrgTemplatePauseMenuStateClass *klass;
    LrgButton *btn;

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    klass = LRG_TEMPLATE_PAUSE_MENU_STATE_GET_CLASS (self);

    if (priv->visible_buttons == NULL ||
        priv->selected_index < 0 ||
        (guint)priv->selected_index >= priv->visible_buttons->len)
        return;

    btn = g_ptr_array_index (priv->visible_buttons, priv->selected_index);

    if (btn == priv->btn_resume)
    {
        if (klass->on_resume != NULL)
            klass->on_resume (self);
    }
    else if (btn == priv->btn_settings)
    {
        if (klass->on_settings != NULL)
            klass->on_settings (self);
    }
    else if (btn == priv->btn_main_menu)
    {
        if (klass->on_main_menu != NULL)
            klass->on_main_menu (self);
    }
    else if (btn == priv->btn_exit)
    {
        if (klass->on_exit != NULL)
            klass->on_exit (self);
    }
}

/* Button click callbacks */
static void
on_resume_clicked (LrgButton *button,
                   gpointer   user_data)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (user_data);
    LrgTemplatePauseMenuStateClass *klass;

    klass = LRG_TEMPLATE_PAUSE_MENU_STATE_GET_CLASS (self);

    if (klass->on_resume != NULL)
        klass->on_resume (self);
}

static void
on_settings_clicked (LrgButton *button,
                     gpointer   user_data)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (user_data);
    LrgTemplatePauseMenuStateClass *klass;

    klass = LRG_TEMPLATE_PAUSE_MENU_STATE_GET_CLASS (self);

    if (klass->on_settings != NULL)
        klass->on_settings (self);
}

static void
on_main_menu_clicked (LrgButton *button,
                      gpointer   user_data)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (user_data);
    LrgTemplatePauseMenuStateClass *klass;

    klass = LRG_TEMPLATE_PAUSE_MENU_STATE_GET_CLASS (self);

    if (klass->on_main_menu != NULL)
        klass->on_main_menu (self);
}

static void
on_exit_clicked (LrgButton *button,
                 gpointer   user_data)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (user_data);
    LrgTemplatePauseMenuStateClass *klass;

    klass = LRG_TEMPLATE_PAUSE_MENU_STATE_GET_CLASS (self);

    if (klass->on_exit != NULL)
        klass->on_exit (self);
}

static LrgButton *
create_menu_button (const gchar *text,
                    gfloat       width,
                    gfloat       height)
{
    LrgButton *btn;
    g_autoptr(GrlColor) normal_color = NULL;
    g_autoptr(GrlColor) hover_color = NULL;
    g_autoptr(GrlColor) pressed_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;

    btn = lrg_button_new (text);

    lrg_widget_set_width (LRG_WIDGET (btn), width);
    lrg_widget_set_height (LRG_WIDGET (btn), height);

    normal_color = grl_color_new (80, 80, 80, 230);
    hover_color = grl_color_new (100, 150, 255, 255);
    pressed_color = grl_color_new (60, 100, 200, 255);
    text_color = grl_color_new (255, 255, 255, 255);

    lrg_button_set_normal_color (btn, normal_color);
    lrg_button_set_hover_color (btn, hover_color);
    lrg_button_set_pressed_color (btn, pressed_color);
    lrg_button_set_text_color (btn, text_color);

    return btn;
}

static void
rebuild_visible_buttons (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    g_ptr_array_set_size (priv->visible_buttons, 0);

    if (priv->btn_resume != NULL)
        g_ptr_array_add (priv->visible_buttons, priv->btn_resume);

    if (priv->show_settings && priv->btn_settings != NULL)
        g_ptr_array_add (priv->visible_buttons, priv->btn_settings);

    if (priv->show_main_menu && priv->btn_main_menu != NULL)
        g_ptr_array_add (priv->visible_buttons, priv->btn_main_menu);

    if (priv->show_exit && priv->btn_exit != NULL)
        g_ptr_array_add (priv->visible_buttons, priv->btn_exit);

    /* Clamp selected index */
    if (priv->visible_buttons->len > 0)
    {
        if (priv->selected_index >= (gint)priv->visible_buttons->len)
            priv->selected_index = (gint)priv->visible_buttons->len - 1;
        if (priv->selected_index < 0)
            priv->selected_index = 0;
    }
    else
    {
        priv->selected_index = 0;
    }

    update_button_selection (self);
}

static void
apply_audio_ducking (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;
    LrgAudioManager *audio;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (!priv->duck_audio)
        return;

    audio = lrg_audio_manager_get_default ();
    if (audio == NULL)
        return;

    /* Save current volumes */
    priv->saved_master_volume = lrg_audio_manager_get_master_volume (audio);
    priv->saved_sfx_volume = lrg_audio_manager_get_sfx_volume (audio);
    priv->saved_music_volume = lrg_audio_manager_get_music_volume (audio);

    /* Apply ducking */
    lrg_audio_manager_set_sfx_volume (audio, priv->saved_sfx_volume * priv->duck_factor);
    lrg_audio_manager_set_music_volume (audio, priv->saved_music_volume * priv->duck_factor);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Audio ducked to %.0f%%", priv->duck_factor * 100.0f);
}

static void
restore_audio_ducking (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;
    LrgAudioManager *audio;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (!priv->duck_audio)
        return;

    audio = lrg_audio_manager_get_default ();
    if (audio == NULL)
        return;

    /* Restore original volumes */
    lrg_audio_manager_set_sfx_volume (audio, priv->saved_sfx_volume);
    lrg_audio_manager_set_music_volume (audio, priv->saved_music_volume);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Audio restored to original volume");
}

/* ========================================================================== */
/* Virtual Method Implementations                                             */
/* ========================================================================== */

static void
lrg_template_pause_menu_state_real_on_resume (LrgTemplatePauseMenuState *self)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu: Resume selected");
    g_signal_emit (self, signals[SIGNAL_RESUME], 0);
}

static void
lrg_template_pause_menu_state_real_on_settings (LrgTemplatePauseMenuState *self)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu: Settings selected");
    g_signal_emit (self, signals[SIGNAL_SETTINGS], 0);
}

static void
lrg_template_pause_menu_state_real_on_main_menu (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (priv->confirm_main_menu)
    {
        /* TODO: Push confirmation state */
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu: Main Menu selected (needs confirmation)");
    }

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu: Main Menu selected");
    g_signal_emit (self, signals[SIGNAL_MAIN_MENU], 0);
}

static void
lrg_template_pause_menu_state_real_on_exit (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (priv->confirm_exit)
    {
        /* TODO: Push confirmation state */
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu: Exit selected (needs confirmation)");
    }

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu: Exit selected");
    g_signal_emit (self, signals[SIGNAL_EXIT_GAME], 0);
}

/* ========================================================================== */
/* LrgGameState Interface Implementation                                      */
/* ========================================================================== */

static void
lrg_template_pause_menu_state_enter (LrgGameState *state)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (state);
    LrgTemplatePauseMenuStatePrivate *priv;
    g_autoptr(GrlColor) title_color = NULL;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu state entering");

    /* Apply audio ducking */
    apply_audio_ducking (self);

    /* Create UI */
    priv->canvas = lrg_canvas_new ();
    priv->menu_container = lrg_vbox_new ();

    /* Title */
    priv->title_label = lrg_label_new ("PAUSED");
    lrg_label_set_font_size (priv->title_label, 48.0f);
    title_color = grl_color_new (255, 255, 255, 255);
    lrg_label_set_color (priv->title_label, title_color);
    lrg_label_set_alignment (priv->title_label, LRG_TEXT_ALIGN_CENTER);

    /* Create buttons */
    priv->btn_resume = create_menu_button ("Resume", DEFAULT_BUTTON_WIDTH, DEFAULT_BUTTON_HEIGHT);
    g_signal_connect (priv->btn_resume, "clicked", G_CALLBACK (on_resume_clicked), self);

    priv->btn_settings = create_menu_button ("Settings", DEFAULT_BUTTON_WIDTH, DEFAULT_BUTTON_HEIGHT);
    g_signal_connect (priv->btn_settings, "clicked", G_CALLBACK (on_settings_clicked), self);

    priv->btn_main_menu = create_menu_button ("Main Menu", DEFAULT_BUTTON_WIDTH, DEFAULT_BUTTON_HEIGHT);
    g_signal_connect (priv->btn_main_menu, "clicked", G_CALLBACK (on_main_menu_clicked), self);

    priv->btn_exit = create_menu_button ("Exit", DEFAULT_BUTTON_WIDTH, DEFAULT_BUTTON_HEIGHT);
    g_signal_connect (priv->btn_exit, "clicked", G_CALLBACK (on_exit_clicked), self);

    /* Build visible buttons */
    rebuild_visible_buttons (self);

    /* Add to container */
    lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                             LRG_WIDGET (priv->title_label));

    /* Spacer */
    {
        LrgWidget *spacer = g_object_new (LRG_TYPE_WIDGET, NULL);
        lrg_widget_set_height (spacer, 30.0f);
        lrg_container_add_child (LRG_CONTAINER (priv->menu_container), spacer);
    }

    lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                             LRG_WIDGET (priv->btn_resume));

    if (priv->show_settings)
    {
        lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                                 LRG_WIDGET (priv->btn_settings));
    }

    if (priv->show_main_menu)
    {
        lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                                 LRG_WIDGET (priv->btn_main_menu));
    }

    if (priv->show_exit)
    {
        lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                                 LRG_WIDGET (priv->btn_exit));
    }

    lrg_container_add_child (LRG_CONTAINER (priv->canvas),
                             LRG_WIDGET (priv->menu_container));

    /* Center menu */
    {
        LrgEngine *engine = lrg_engine_get_default ();
        LrgWindow *window = lrg_engine_get_window (engine);
        gint screen_width = lrg_window_get_width (window);
        gint screen_height = lrg_window_get_height (window);
        gfloat menu_x;
        gfloat menu_y;

        menu_x = (screen_width - DEFAULT_BUTTON_WIDTH) / 2.0f;
        menu_y = screen_height * 0.3f;

        lrg_widget_set_x (LRG_WIDGET (priv->menu_container), menu_x);
        lrg_widget_set_y (LRG_WIDGET (priv->menu_container), menu_y);
    }

    priv->menu_built = TRUE;
    priv->selected_index = 0;
    update_button_selection (self);
}

static void
lrg_template_pause_menu_state_exit (LrgGameState *state)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (state);
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pause menu state exiting");

    /* Restore audio */
    restore_audio_ducking (self);

    /* Clear UI */
    g_clear_object (&priv->title_label);
    g_clear_object (&priv->btn_resume);
    g_clear_object (&priv->btn_settings);
    g_clear_object (&priv->btn_main_menu);
    g_clear_object (&priv->btn_exit);
    g_clear_object (&priv->menu_container);
    g_clear_object (&priv->canvas);

    g_ptr_array_set_size (priv->visible_buttons, 0);

    priv->menu_built = FALSE;
}

static void
lrg_template_pause_menu_state_update (LrgGameState *state,
                                      gdouble       delta)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (state);
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (priv->canvas == NULL)
        return;

    /* Handle keyboard/gamepad navigation (arrows + vim j/k) */
    if (grl_input_is_key_pressed (GRL_KEY_DOWN) ||
        grl_input_is_key_pressed (GRL_KEY_J) ||
        grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN))
    {
        navigate_menu (self, 1);
    }
    else if (grl_input_is_key_pressed (GRL_KEY_UP) ||
             grl_input_is_key_pressed (GRL_KEY_K) ||
             grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_UP))
    {
        navigate_menu (self, -1);
    }
    else if (grl_input_is_key_pressed (GRL_KEY_ENTER) ||
             grl_input_is_key_pressed (GRL_KEY_SPACE) ||
             grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
    {
        activate_selected (self);
    }
    else if (grl_input_is_key_pressed (GRL_KEY_ESCAPE) ||
             grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
    {
        /* Escape/B button = Resume */
        LrgTemplatePauseMenuStateClass *klass;
        klass = LRG_TEMPLATE_PAUSE_MENU_STATE_GET_CLASS (self);
        if (klass->on_resume != NULL)
            klass->on_resume (self);
    }

    lrg_canvas_handle_input (priv->canvas);
}

static void
lrg_template_pause_menu_state_draw (LrgGameState *state)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (state);
    LrgTemplatePauseMenuStatePrivate *priv;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    engine = lrg_engine_get_default ();
    window = lrg_engine_get_window (engine);
    screen_width = lrg_window_get_width (window);
    screen_height = lrg_window_get_height (window);

    /* Draw overlay */
    if (priv->overlay_color != NULL)
    {
        grl_draw_rectangle (0, 0, (gfloat)screen_width, (gfloat)screen_height, priv->overlay_color);
    }
    else
    {
        g_autoptr(GrlColor) default_overlay = grl_color_new (0, 0, 0, 180);
        grl_draw_rectangle (0, 0, (gfloat)screen_width, (gfloat)screen_height, default_overlay);
    }

    /* Render UI */
    if (priv->canvas != NULL)
        lrg_canvas_render (priv->canvas);
}

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
lrg_template_pause_menu_state_set_property (GObject      *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (object);

    switch (prop_id)
    {
    case PROP_DUCK_AUDIO:
        lrg_template_pause_menu_state_set_duck_audio (self, g_value_get_boolean (value));
        break;
    case PROP_DUCK_FACTOR:
        lrg_template_pause_menu_state_set_duck_factor (self, g_value_get_float (value));
        break;
    case PROP_OVERLAY_COLOR:
        lrg_template_pause_menu_state_set_overlay_color (self, g_value_get_boxed (value));
        break;
    case PROP_SHOW_SETTINGS:
        lrg_template_pause_menu_state_set_show_settings (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_MAIN_MENU:
        lrg_template_pause_menu_state_set_show_main_menu (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_EXIT:
        lrg_template_pause_menu_state_set_show_exit (self, g_value_get_boolean (value));
        break;
    case PROP_CONFIRM_MAIN_MENU:
        lrg_template_pause_menu_state_set_confirm_main_menu (self, g_value_get_boolean (value));
        break;
    case PROP_CONFIRM_EXIT:
        lrg_template_pause_menu_state_set_confirm_exit (self, g_value_get_boolean (value));
        break;
    case PROP_SELECTED_INDEX:
        lrg_template_pause_menu_state_set_selected_index (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_pause_menu_state_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (object);
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_DUCK_AUDIO:
        g_value_set_boolean (value, priv->duck_audio);
        break;
    case PROP_DUCK_FACTOR:
        g_value_set_float (value, priv->duck_factor);
        break;
    case PROP_OVERLAY_COLOR:
        g_value_set_boxed (value, priv->overlay_color);
        break;
    case PROP_SHOW_SETTINGS:
        g_value_set_boolean (value, priv->show_settings);
        break;
    case PROP_SHOW_MAIN_MENU:
        g_value_set_boolean (value, priv->show_main_menu);
        break;
    case PROP_SHOW_EXIT:
        g_value_set_boolean (value, priv->show_exit);
        break;
    case PROP_CONFIRM_MAIN_MENU:
        g_value_set_boolean (value, priv->confirm_main_menu);
        break;
    case PROP_CONFIRM_EXIT:
        g_value_set_boolean (value, priv->confirm_exit);
        break;
    case PROP_SELECTED_INDEX:
        g_value_set_int (value, priv->selected_index);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_pause_menu_state_finalize (GObject *object)
{
    LrgTemplatePauseMenuState *self = LRG_TEMPLATE_PAUSE_MENU_STATE (object);
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    g_clear_pointer (&priv->overlay_color, grl_color_free);
    g_clear_pointer (&priv->visible_buttons, g_ptr_array_unref);
    g_clear_object (&priv->canvas);

    G_OBJECT_CLASS (lrg_template_pause_menu_state_parent_class)->finalize (object);
}

static void
lrg_template_pause_menu_state_class_init (LrgTemplatePauseMenuStateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    object_class->set_property = lrg_template_pause_menu_state_set_property;
    object_class->get_property = lrg_template_pause_menu_state_get_property;
    object_class->finalize = lrg_template_pause_menu_state_finalize;

    state_class->enter = lrg_template_pause_menu_state_enter;
    state_class->exit = lrg_template_pause_menu_state_exit;
    state_class->update = lrg_template_pause_menu_state_update;
    state_class->draw = lrg_template_pause_menu_state_draw;

    klass->on_resume = lrg_template_pause_menu_state_real_on_resume;
    klass->on_settings = lrg_template_pause_menu_state_real_on_settings;
    klass->on_main_menu = lrg_template_pause_menu_state_real_on_main_menu;
    klass->on_exit = lrg_template_pause_menu_state_real_on_exit;

    /**
     * LrgTemplatePauseMenuState:duck-audio:
     *
     * Whether audio is ducked when the pause menu is shown.
     *
     * Since: 1.0
     */
    properties[PROP_DUCK_AUDIO] =
        g_param_spec_boolean ("duck-audio",
                              "Duck Audio",
                              "Whether to duck audio volume",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:duck-factor:
     *
     * The volume multiplier when audio is ducked (0.0 to 1.0).
     *
     * Since: 1.0
     */
    properties[PROP_DUCK_FACTOR] =
        g_param_spec_float ("duck-factor",
                            "Duck Factor",
                            "Volume multiplier for ducking",
                            0.0f, 1.0f, DEFAULT_DUCK_FACTOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:overlay-color:
     *
     * The color of the overlay drawn behind the menu.
     *
     * Since: 1.0
     */
    properties[PROP_OVERLAY_COLOR] =
        g_param_spec_boxed ("overlay-color",
                            "Overlay Color",
                            "Semi-transparent overlay color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:show-settings:
     *
     * Whether the Settings button is shown.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_SETTINGS] =
        g_param_spec_boolean ("show-settings",
                              "Show Settings",
                              "Whether Settings button is visible",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:show-main-menu:
     *
     * Whether the Main Menu button is shown.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_MAIN_MENU] =
        g_param_spec_boolean ("show-main-menu",
                              "Show Main Menu",
                              "Whether Main Menu button is visible",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:show-exit:
     *
     * Whether the Exit button is shown.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_EXIT] =
        g_param_spec_boolean ("show-exit",
                              "Show Exit",
                              "Whether Exit button is visible",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:confirm-main-menu:
     *
     * Whether returning to main menu requires confirmation.
     *
     * Since: 1.0
     */
    properties[PROP_CONFIRM_MAIN_MENU] =
        g_param_spec_boolean ("confirm-main-menu",
                              "Confirm Main Menu",
                              "Whether to confirm returning to main menu",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:confirm-exit:
     *
     * Whether exiting requires confirmation.
     *
     * Since: 1.0
     */
    properties[PROP_CONFIRM_EXIT] =
        g_param_spec_boolean ("confirm-exit",
                              "Confirm Exit",
                              "Whether to confirm exiting",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplatePauseMenuState:selected-index:
     *
     * The currently selected menu item index.
     *
     * Since: 1.0
     */
    properties[PROP_SELECTED_INDEX] =
        g_param_spec_int ("selected-index",
                          "Selected Index",
                          "Currently selected menu item",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTemplatePauseMenuState::resume:
     * @self: the #LrgTemplatePauseMenuState
     *
     * Emitted when the Resume button is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RESUME] =
        g_signal_new ("resume",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplatePauseMenuState::settings:
     * @self: the #LrgTemplatePauseMenuState
     *
     * Emitted when the Settings button is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SETTINGS] =
        g_signal_new ("settings",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplatePauseMenuState::main-menu:
     * @self: the #LrgTemplatePauseMenuState
     *
     * Emitted when the Main Menu button is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_MAIN_MENU] =
        g_signal_new ("main-menu",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplatePauseMenuState::exit-game:
     * @self: the #LrgTemplatePauseMenuState
     *
     * Emitted when the Exit button is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EXIT_GAME] =
        g_signal_new ("exit-game",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_template_pause_menu_state_init (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    priv->duck_audio = TRUE;
    priv->duck_factor = DEFAULT_DUCK_FACTOR;
    priv->saved_master_volume = 1.0f;
    priv->saved_sfx_volume = 1.0f;
    priv->saved_music_volume = 1.0f;

    priv->overlay_color = NULL;
    priv->show_settings = TRUE;
    priv->show_main_menu = TRUE;
    priv->show_exit = TRUE;
    priv->confirm_main_menu = FALSE;
    priv->confirm_exit = FALSE;

    priv->visible_buttons = g_ptr_array_new ();
    priv->selected_index = 0;
    priv->menu_built = FALSE;

    /* Pause menu is transparent (shows game behind) and blocking (pauses game) */
    lrg_game_state_set_name (LRG_GAME_STATE (self), "PauseMenu");
    lrg_game_state_set_transparent (LRG_GAME_STATE (self), TRUE);
    lrg_game_state_set_blocking (LRG_GAME_STATE (self), TRUE);
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * lrg_template_pause_menu_state_new:
 *
 * Creates a new pause menu state.
 *
 * Returns: (transfer full): A new #LrgTemplatePauseMenuState
 *
 * Since: 1.0
 */
LrgTemplatePauseMenuState *
lrg_template_pause_menu_state_new (void)
{
    return g_object_new (LRG_TYPE_TEMPLATE_PAUSE_MENU_STATE, NULL);
}

gboolean
lrg_template_pause_menu_state_get_duck_audio (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), TRUE);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->duck_audio;
}

void
lrg_template_pause_menu_state_set_duck_audio (LrgTemplatePauseMenuState *self,
                                              gboolean                   duck)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    duck = !!duck;

    if (priv->duck_audio != duck)
    {
        priv->duck_audio = duck;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DUCK_AUDIO]);
    }
}

gfloat
lrg_template_pause_menu_state_get_duck_factor (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), DEFAULT_DUCK_FACTOR);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->duck_factor;
}

void
lrg_template_pause_menu_state_set_duck_factor (LrgTemplatePauseMenuState *self,
                                               gfloat                     factor)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    factor = CLAMP (factor, 0.0f, 1.0f);

    if (priv->duck_factor != factor)
    {
        priv->duck_factor = factor;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DUCK_FACTOR]);
    }
}

const GrlColor *
lrg_template_pause_menu_state_get_overlay_color (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), NULL);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->overlay_color;
}

void
lrg_template_pause_menu_state_set_overlay_color (LrgTemplatePauseMenuState *self,
                                                 const GrlColor            *color)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    g_clear_pointer (&priv->overlay_color, grl_color_free);
    if (color != NULL)
        priv->overlay_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OVERLAY_COLOR]);
}

gboolean
lrg_template_pause_menu_state_get_confirm_main_menu (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), FALSE);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->confirm_main_menu;
}

void
lrg_template_pause_menu_state_set_confirm_main_menu (LrgTemplatePauseMenuState *self,
                                                     gboolean                   confirm)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    confirm = !!confirm;

    if (priv->confirm_main_menu != confirm)
    {
        priv->confirm_main_menu = confirm;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONFIRM_MAIN_MENU]);
    }
}

gboolean
lrg_template_pause_menu_state_get_confirm_exit (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), FALSE);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->confirm_exit;
}

void
lrg_template_pause_menu_state_set_confirm_exit (LrgTemplatePauseMenuState *self,
                                                gboolean                   confirm)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    confirm = !!confirm;

    if (priv->confirm_exit != confirm)
    {
        priv->confirm_exit = confirm;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONFIRM_EXIT]);
    }
}

gboolean
lrg_template_pause_menu_state_get_show_settings (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), TRUE);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->show_settings;
}

void
lrg_template_pause_menu_state_set_show_settings (LrgTemplatePauseMenuState *self,
                                                 gboolean                   show)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    show = !!show;

    if (priv->show_settings != show)
    {
        priv->show_settings = show;

        if (priv->menu_built)
            rebuild_visible_buttons (self);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_SETTINGS]);
    }
}

gboolean
lrg_template_pause_menu_state_get_show_main_menu (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), TRUE);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->show_main_menu;
}

void
lrg_template_pause_menu_state_set_show_main_menu (LrgTemplatePauseMenuState *self,
                                                  gboolean                   show)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    show = !!show;

    if (priv->show_main_menu != show)
    {
        priv->show_main_menu = show;

        if (priv->menu_built)
            rebuild_visible_buttons (self);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MAIN_MENU]);
    }
}

gboolean
lrg_template_pause_menu_state_get_show_exit (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), TRUE);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->show_exit;
}

void
lrg_template_pause_menu_state_set_show_exit (LrgTemplatePauseMenuState *self,
                                             gboolean                   show)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    show = !!show;

    if (priv->show_exit != show)
    {
        priv->show_exit = show;

        if (priv->menu_built)
            rebuild_visible_buttons (self);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_EXIT]);
    }
}

gint
lrg_template_pause_menu_state_get_selected_index (LrgTemplatePauseMenuState *self)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self), 0);

    priv = lrg_template_pause_menu_state_get_instance_private (self);
    return priv->selected_index;
}

void
lrg_template_pause_menu_state_set_selected_index (LrgTemplatePauseMenuState *self,
                                                  gint                       index)
{
    LrgTemplatePauseMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (self));

    priv = lrg_template_pause_menu_state_get_instance_private (self);

    if (index >= 0 && (guint)index < priv->visible_buttons->len)
    {
        priv->selected_index = index;
        update_button_selection (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED_INDEX]);
    }
}
