/* lrg-template-settings-menu-state.c - Settings menu state for game templates
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-template-settings-menu-state.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-slider.h"
#include "../../ui/lrg-tab-view.h"
#include "../../settings/lrg-settings.h"
#include "../../audio/lrg-audio-manager.h"
#include "../../core/lrg-engine.h"
#include "../../graphics/lrg-window.h"
#include "../../lrg-log.h"

/**
 * SECTION:lrg-template-settings-menu-state
 * @title: LrgTemplateSettingsMenuState
 * @short_description: Tabbed settings menu state for game templates
 *
 * #LrgTemplateSettingsMenuState provides a standard tabbed settings menu
 * with built-in support for graphics, audio, and controls tabs.
 *
 * ## Features
 *
 * - Tab-based layout using #LrgTabView
 * - Built-in graphics settings (resolution, fullscreen, vsync)
 * - Built-in audio settings (master, music, SFX volume)
 * - Built-in controls tab (keybind display)
 * - Apply, Cancel, and Reset to Defaults buttons
 * - Custom tab support via virtual methods
 *
 * ## Signals
 *
 * The state emits signals when buttons are activated:
 * - #LrgTemplateSettingsMenuState::apply - Apply button activated
 * - #LrgTemplateSettingsMenuState::cancel - Cancel button activated
 * - #LrgTemplateSettingsMenuState::reset - Reset button activated
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * // Create settings menu state
 * LrgGameState *settings = (LrgGameState *)
 *     lrg_template_settings_menu_state_new ();
 *
 * // Hide controls tab if not using keybinds
 * lrg_template_settings_menu_state_set_show_controls_tab (
 *     LRG_TEMPLATE_SETTINGS_MENU_STATE (settings), FALSE);
 *
 * // Push onto state stack
 * lrg_game_state_manager_push (manager, settings);
 * ]|
 */

typedef struct _CustomTabEntry CustomTabEntry;

struct _CustomTabEntry
{
    gchar *name;
    gchar *label;
};

typedef struct _LrgTemplateSettingsMenuStatePrivate LrgTemplateSettingsMenuStatePrivate;

struct _LrgTemplateSettingsMenuStatePrivate
{
    /* UI Elements */
    LrgCanvas   *canvas;
    LrgTabView  *tab_view;
    LrgVBox     *main_box;
    LrgHBox     *button_box;
    LrgButton   *apply_button;
    LrgButton   *cancel_button;
    LrgButton   *reset_button;

    /* Audio sliders (for tracking changes) */
    LrgSlider   *master_volume_slider;
    LrgSlider   *music_volume_slider;
    LrgSlider   *sfx_volume_slider;

    /* Saved volumes (for cancel/reset) */
    gfloat       saved_master_volume;
    gfloat       saved_music_volume;
    gfloat       saved_sfx_volume;

    /* Tab visibility */
    gboolean     show_graphics_tab;
    gboolean     show_audio_tab;
    gboolean     show_controls_tab;

    /* Button visibility */
    gboolean     show_reset_button;

    /* Confirmation settings */
    gboolean     confirm_cancel;
    gboolean     confirm_reset;

    /* Custom tabs */
    GPtrArray   *custom_tabs;

    /* Dirty tracking */
    gboolean     has_changes;

    /* Colors */
    GrlColor    *background_color;

    /* Navigation */
    gint         selected_button;
    gint         visible_button_count;
};

G_DEFINE_TYPE_WITH_PRIVATE (LrgTemplateSettingsMenuState,
                            lrg_template_settings_menu_state,
                            LRG_TYPE_GAME_STATE)

enum
{
    PROP_0,
    PROP_SHOW_GRAPHICS_TAB,
    PROP_SHOW_AUDIO_TAB,
    PROP_SHOW_CONTROLS_TAB,
    PROP_SHOW_RESET_BUTTON,
    PROP_CONFIRM_CANCEL,
    PROP_CONFIRM_RESET,
    N_PROPS
};

static GParamSpec *props[N_PROPS];

enum
{
    SIGNAL_APPLY,
    SIGNAL_CANCEL,
    SIGNAL_RESET,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static void create_ui                (LrgTemplateSettingsMenuState *self);
static void update_button_selection  (LrgTemplateSettingsMenuState *self);
static void navigate_left            (LrgTemplateSettingsMenuState *self);
static void navigate_right           (LrgTemplateSettingsMenuState *self);
static void activate_selected_button (LrgTemplateSettingsMenuState *self);
static void save_current_settings    (LrgTemplateSettingsMenuState *self);
static void restore_saved_settings   (LrgTemplateSettingsMenuState *self);

/* ==========================================================================
 * Custom Tab Entry Helpers
 * ========================================================================== */

static CustomTabEntry *
custom_tab_entry_new (const gchar *name,
                      const gchar *label)
{
    CustomTabEntry *entry;

    entry = g_new0 (CustomTabEntry, 1);
    entry->name = g_strdup (name);
    entry->label = g_strdup (label);

    return entry;
}

static void
custom_tab_entry_free (gpointer data)
{
    CustomTabEntry *entry = data;

    if (entry != NULL)
    {
        g_free (entry->name);
        g_free (entry->label);
        g_free (entry);
    }
}

/* ==========================================================================
 * Button Callbacks
 * ========================================================================== */

static void
on_apply_clicked (LrgButton                    *button,
                  LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStateClass *klass;

    klass = LRG_TEMPLATE_SETTINGS_MENU_STATE_GET_CLASS (self);

    if (klass->on_apply != NULL)
        klass->on_apply (self);
}

static void
on_cancel_clicked (LrgButton                    *button,
                   LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStateClass *klass;

    klass = LRG_TEMPLATE_SETTINGS_MENU_STATE_GET_CLASS (self);

    if (klass->on_cancel != NULL)
        klass->on_cancel (self);
}

static void
on_reset_clicked (LrgButton                    *button,
                  LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStateClass *klass;

    klass = LRG_TEMPLATE_SETTINGS_MENU_STATE_GET_CLASS (self);

    if (klass->on_reset != NULL)
        klass->on_reset (self);
}

/* ==========================================================================
 * Slider Callbacks
 * ========================================================================== */

static void
on_master_volume_changed (LrgSlider                    *slider,
                          GParamSpec                   *pspec,
                          LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgAudioManager *audio;
    gdouble value;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    audio = lrg_audio_manager_get_default ();
    value = lrg_slider_get_value (slider);

    lrg_audio_manager_set_master_volume (audio, (gfloat) value / 100.0f);
    priv->has_changes = TRUE;
}

static void
on_music_volume_changed (LrgSlider                    *slider,
                         GParamSpec                   *pspec,
                         LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgAudioManager *audio;
    gdouble value;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    audio = lrg_audio_manager_get_default ();
    value = lrg_slider_get_value (slider);

    lrg_audio_manager_set_music_volume (audio,
                                          (gfloat) value / 100.0f);
    priv->has_changes = TRUE;
}

static void
on_sfx_volume_changed (LrgSlider                    *slider,
                       GParamSpec                   *pspec,
                       LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgAudioManager *audio;
    gdouble value;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    audio = lrg_audio_manager_get_default ();
    value = lrg_slider_get_value (slider);

    lrg_audio_manager_set_sfx_volume (audio,
                                          (gfloat) value / 100.0f);
    priv->has_changes = TRUE;
}

/* ==========================================================================
 * Default Tab Creation
 * ========================================================================== */

static LrgWidget *
lrg_template_settings_menu_state_real_create_graphics_tab (LrgTemplateSettingsMenuState *self)
{
    LrgVBox *content;
    LrgLabel *label;

    content = lrg_vbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (content), 15.0f);
    lrg_widget_set_size (LRG_WIDGET (content), 500.0f, 300.0f);

    /* Placeholder text - subclasses should override with actual graphics options */
    label = lrg_label_new ("Graphics Settings");
    lrg_label_set_font_size (label, 24.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    label = lrg_label_new ("Resolution, display mode, and quality options");
    lrg_label_set_font_size (label, 16.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    label = lrg_label_new ("(Override create_graphics_tab for custom content)");
    lrg_label_set_font_size (label, 14.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    return LRG_WIDGET (content);
}

static LrgWidget *
lrg_template_settings_menu_state_real_create_audio_tab (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgAudioManager *audio;
    LrgVBox *content;
    LrgHBox *row;
    LrgLabel *label;

    priv = lrg_template_settings_menu_state_get_instance_private (self);
    audio = lrg_audio_manager_get_default ();

    content = lrg_vbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (content), 20.0f);
    lrg_widget_set_size (LRG_WIDGET (content), 500.0f, 300.0f);

    /* Master Volume */
    row = lrg_hbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (row), 20.0f);

    label = lrg_label_new ("Master Volume");
    lrg_widget_set_size (LRG_WIDGET (label), 150.0f, 30.0f);
    lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (label));

    priv->master_volume_slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);
    lrg_slider_set_value (priv->master_volume_slider,
                          (gdouble) lrg_audio_manager_get_master_volume (audio) * 100.0);
    lrg_widget_set_size (LRG_WIDGET (priv->master_volume_slider), 250.0f, 30.0f);
    g_signal_connect (priv->master_volume_slider, "notify::value",
                      G_CALLBACK (on_master_volume_changed), self);
    lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (priv->master_volume_slider));

    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (row));

    /* Music Volume */
    row = lrg_hbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (row), 20.0f);

    label = lrg_label_new ("Music Volume");
    lrg_widget_set_size (LRG_WIDGET (label), 150.0f, 30.0f);
    lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (label));

    priv->music_volume_slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);
    lrg_slider_set_value (priv->music_volume_slider,
                          (gdouble) lrg_audio_manager_get_music_volume (audio) * 100.0);
    lrg_widget_set_size (LRG_WIDGET (priv->music_volume_slider), 250.0f, 30.0f);
    g_signal_connect (priv->music_volume_slider, "notify::value",
                      G_CALLBACK (on_music_volume_changed), self);
    lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (priv->music_volume_slider));

    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (row));

    /* SFX Volume */
    row = lrg_hbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (row), 20.0f);

    label = lrg_label_new ("SFX Volume");
    lrg_widget_set_size (LRG_WIDGET (label), 150.0f, 30.0f);
    lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (label));

    priv->sfx_volume_slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);
    lrg_slider_set_value (priv->sfx_volume_slider,
                          (gdouble) lrg_audio_manager_get_sfx_volume (audio) * 100.0);
    lrg_widget_set_size (LRG_WIDGET (priv->sfx_volume_slider), 250.0f, 30.0f);
    g_signal_connect (priv->sfx_volume_slider, "notify::value",
                      G_CALLBACK (on_sfx_volume_changed), self);
    lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (priv->sfx_volume_slider));

    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (row));

    return LRG_WIDGET (content);
}

static LrgWidget *
lrg_template_settings_menu_state_real_create_controls_tab (LrgTemplateSettingsMenuState *self)
{
    LrgVBox *content;
    LrgLabel *label;

    content = lrg_vbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (content), 15.0f);
    lrg_widget_set_size (LRG_WIDGET (content), 500.0f, 300.0f);

    /* Placeholder text - subclasses should override with actual keybind editor */
    label = lrg_label_new ("Controls Settings");
    lrg_label_set_font_size (label, 24.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    label = lrg_label_new ("Keyboard, mouse, and controller bindings");
    lrg_label_set_font_size (label, 16.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    label = lrg_label_new ("(Override create_controls_tab for custom content)");
    lrg_label_set_font_size (label, 14.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    return LRG_WIDGET (content);
}

static LrgWidget *
lrg_template_settings_menu_state_real_create_custom_tab (LrgTemplateSettingsMenuState *self,
                                                         const gchar                  *tab_name)
{
    LrgVBox *content;
    LrgLabel *label;
    gchar *message;

    content = lrg_vbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (content), 15.0f);
    lrg_widget_set_size (LRG_WIDGET (content), 500.0f, 300.0f);

    message = g_strdup_printf ("Custom Tab: %s", tab_name);
    label = lrg_label_new (message);
    g_free (message);
    lrg_label_set_font_size (label, 24.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    label = lrg_label_new ("(Override create_custom_tab for content)");
    lrg_label_set_font_size (label, 14.0f);
    lrg_container_add_child (LRG_CONTAINER (content), LRG_WIDGET (label));

    return LRG_WIDGET (content);
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_template_settings_menu_state_real_on_apply (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgSettings *settings;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    /* Apply settings to the system */
    settings = lrg_settings_get_default ();
    lrg_settings_apply_all (settings);

    /* Update saved values */
    save_current_settings (self);
    priv->has_changes = FALSE;

    g_signal_emit (self, signals[SIGNAL_APPLY], 0);
}

static void
lrg_template_settings_menu_state_real_on_cancel (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    /* Restore previous settings */
    if (priv->has_changes)
    {
        restore_saved_settings (self);
    }

    priv->has_changes = FALSE;

    g_signal_emit (self, signals[SIGNAL_CANCEL], 0);
}

static void
lrg_template_settings_menu_state_real_on_reset (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgSettings *settings;
    LrgAudioManager *audio;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    /* Reset all settings to defaults */
    settings = lrg_settings_get_default ();
    lrg_settings_reset_all (settings);
    lrg_settings_apply_all (settings);

    /* Update audio sliders to reflect reset values */
    audio = lrg_audio_manager_get_default ();
    if (priv->master_volume_slider != NULL)
    {
        lrg_slider_set_value (priv->master_volume_slider,
                              (gdouble) lrg_audio_manager_get_master_volume (audio) * 100.0);
    }
    if (priv->music_volume_slider != NULL)
    {
        lrg_slider_set_value (priv->music_volume_slider,
                              (gdouble) lrg_audio_manager_get_music_volume (audio) * 100.0);
    }
    if (priv->sfx_volume_slider != NULL)
    {
        lrg_slider_set_value (priv->sfx_volume_slider,
                              (gdouble) lrg_audio_manager_get_sfx_volume (audio) * 100.0);
    }

    priv->has_changes = TRUE;

    g_signal_emit (self, signals[SIGNAL_RESET], 0);
}

/* ==========================================================================
 * Settings Helpers
 * ========================================================================== */

static void
save_current_settings (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgAudioManager *audio;

    priv = lrg_template_settings_menu_state_get_instance_private (self);
    audio = lrg_audio_manager_get_default ();

    priv->saved_master_volume = lrg_audio_manager_get_master_volume (audio);
    priv->saved_music_volume = lrg_audio_manager_get_music_volume (audio);
    priv->saved_sfx_volume = lrg_audio_manager_get_sfx_volume (audio);
}

static void
restore_saved_settings (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgAudioManager *audio;

    priv = lrg_template_settings_menu_state_get_instance_private (self);
    audio = lrg_audio_manager_get_default ();

    lrg_audio_manager_set_master_volume (audio, priv->saved_master_volume);
    lrg_audio_manager_set_music_volume (audio, priv->saved_music_volume);
    lrg_audio_manager_set_sfx_volume (audio, priv->saved_sfx_volume);

    /* Update sliders */
    if (priv->master_volume_slider != NULL)
        lrg_slider_set_value (priv->master_volume_slider, (gdouble) priv->saved_master_volume * 100.0);
    if (priv->music_volume_slider != NULL)
        lrg_slider_set_value (priv->music_volume_slider, (gdouble) priv->saved_music_volume * 100.0);
    if (priv->sfx_volume_slider != NULL)
        lrg_slider_set_value (priv->sfx_volume_slider, (gdouble) priv->saved_sfx_volume * 100.0);
}

/* ==========================================================================
 * Navigation Helpers
 * ========================================================================== */

static void
count_visible_buttons (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    /* Apply and Cancel are always visible */
    priv->visible_button_count = 2;

    if (priv->show_reset_button)
        priv->visible_button_count++;
}

static LrgButton *
get_button_at_index (LrgTemplateSettingsMenuState *self,
                     gint                          index)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    switch (index)
    {
    case 0:
        return priv->apply_button;
    case 1:
        return priv->cancel_button;
    case 2:
        if (priv->show_reset_button)
            return priv->reset_button;
        return NULL;
    default:
        return NULL;
    }
}

static void
update_button_selection (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgButton *button;
    gint i;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    for (i = 0; i < priv->visible_button_count; i++)
    {
        button = get_button_at_index (self, i);
        if (button != NULL)
        {
            /* Use color to indicate selection */
            if (i == priv->selected_button)
            {
                g_autoptr(GrlColor) selected_color = grl_color_new (100, 150, 220, 255);
                lrg_button_set_normal_color (button, selected_color);
            }
            else
            {
                g_autoptr(GrlColor) normal_color = grl_color_new (80, 80, 90, 255);
                lrg_button_set_normal_color (button, normal_color);
            }
        }
    }
}

static void
navigate_left (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    if (priv->visible_button_count == 0)
        return;

    priv->selected_button--;
    if (priv->selected_button < 0)
        priv->selected_button = priv->visible_button_count - 1;

    update_button_selection (self);
}

static void
navigate_right (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    if (priv->visible_button_count == 0)
        return;

    priv->selected_button++;
    if (priv->selected_button >= priv->visible_button_count)
        priv->selected_button = 0;

    update_button_selection (self);
}

static void
activate_selected_button (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgButton *button;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    button = get_button_at_index (self, priv->selected_button);
    if (button != NULL)
    {
        g_signal_emit_by_name (button, "clicked");
    }
}

/* ==========================================================================
 * UI Creation
 * ========================================================================== */

static void
create_ui (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgTemplateSettingsMenuStateClass *klass;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;
    LrgWidget *tab_content;
    CustomTabEntry *custom_entry;
    guint i;

    priv = lrg_template_settings_menu_state_get_instance_private (self);
    klass = LRG_TEMPLATE_SETTINGS_MENU_STATE_GET_CLASS (self);

    engine = lrg_engine_get_default ();
    window = lrg_engine_get_window (engine);
    screen_width = lrg_window_get_width (window);
    screen_height = lrg_window_get_height (window);

    /* Create canvas */
    priv->canvas = lrg_canvas_new ();
    lrg_widget_set_size (LRG_WIDGET (priv->canvas),
                         (gfloat) screen_width,
                         (gfloat) screen_height);

    /* Create main container */
    priv->main_box = lrg_vbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (priv->main_box), 20.0f);
    lrg_widget_set_position (LRG_WIDGET (priv->main_box),
                             (gfloat) screen_width / 2.0f - 300.0f,
                             50.0f);
    lrg_widget_set_size (LRG_WIDGET (priv->main_box), 600.0f, 500.0f);
    lrg_container_add_child (LRG_CONTAINER (priv->canvas), LRG_WIDGET (priv->main_box));

    /* Create tab view */
    priv->tab_view = lrg_tab_view_new ();
    lrg_widget_set_size (LRG_WIDGET (priv->tab_view), 600.0f, 400.0f);
    lrg_container_add_child (LRG_CONTAINER (priv->main_box),
                             LRG_WIDGET (priv->tab_view));

    /* Add built-in tabs */
    if (priv->show_graphics_tab && klass->create_graphics_tab != NULL)
    {
        tab_content = klass->create_graphics_tab (self);
        if (tab_content != NULL)
        {
            lrg_tab_view_add_tab (priv->tab_view, "Graphics", tab_content);
        }
    }

    if (priv->show_audio_tab && klass->create_audio_tab != NULL)
    {
        tab_content = klass->create_audio_tab (self);
        if (tab_content != NULL)
        {
            lrg_tab_view_add_tab (priv->tab_view, "Audio", tab_content);
        }
    }

    if (priv->show_controls_tab && klass->create_controls_tab != NULL)
    {
        tab_content = klass->create_controls_tab (self);
        if (tab_content != NULL)
        {
            lrg_tab_view_add_tab (priv->tab_view, "Controls", tab_content);
        }
    }

    /* Add custom tabs */
    if (priv->custom_tabs != NULL && klass->create_custom_tab != NULL)
    {
        for (i = 0; i < priv->custom_tabs->len; i++)
        {
            custom_entry = g_ptr_array_index (priv->custom_tabs, i);
            tab_content = klass->create_custom_tab (self, custom_entry->name);
            if (tab_content != NULL)
            {
                lrg_tab_view_add_tab (priv->tab_view, custom_entry->label, tab_content);
            }
        }
    }

    /* Create button box */
    priv->button_box = lrg_hbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (priv->button_box), 20.0f);
    lrg_container_add_child (LRG_CONTAINER (priv->main_box),
                             LRG_WIDGET (priv->button_box));

    /* Create Apply button */
    priv->apply_button = lrg_button_new ("Apply");
    lrg_widget_set_size (LRG_WIDGET (priv->apply_button), 150.0f, 50.0f);
    g_signal_connect (priv->apply_button, "clicked",
                      G_CALLBACK (on_apply_clicked), self);
    lrg_container_add_child (LRG_CONTAINER (priv->button_box),
                             LRG_WIDGET (priv->apply_button));

    /* Create Cancel button */
    priv->cancel_button = lrg_button_new ("Cancel");
    lrg_widget_set_size (LRG_WIDGET (priv->cancel_button), 150.0f, 50.0f);
    g_signal_connect (priv->cancel_button, "clicked",
                      G_CALLBACK (on_cancel_clicked), self);
    lrg_container_add_child (LRG_CONTAINER (priv->button_box),
                             LRG_WIDGET (priv->cancel_button));

    /* Create Reset button (optional) */
    if (priv->show_reset_button)
    {
        priv->reset_button = lrg_button_new ("Reset Defaults");
        lrg_widget_set_size (LRG_WIDGET (priv->reset_button), 150.0f, 50.0f);
        g_signal_connect (priv->reset_button, "clicked",
                          G_CALLBACK (on_reset_clicked), self);
        lrg_container_add_child (LRG_CONTAINER (priv->button_box),
                                 LRG_WIDGET (priv->reset_button));
    }

    count_visible_buttons (self);
    update_button_selection (self);
}

/* ==========================================================================
 * LrgGameState Virtual Method Overrides
 * ========================================================================== */

static void
lrg_template_settings_menu_state_enter (LrgGameState *state)
{
    LrgTemplateSettingsMenuState *self;

    self = LRG_TEMPLATE_SETTINGS_MENU_STATE (state);

    /* Save current settings for cancel */
    save_current_settings (self);

    /* Create UI */
    create_ui (self);

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_settings_menu_state_parent_class)->enter (state);
}

static void
lrg_template_settings_menu_state_exit (LrgGameState *state)
{
    LrgTemplateSettingsMenuState *self;
    LrgTemplateSettingsMenuStatePrivate *priv;

    self = LRG_TEMPLATE_SETTINGS_MENU_STATE (state);
    priv = lrg_template_settings_menu_state_get_instance_private (self);

    /* Clean up UI */
    g_clear_object (&priv->canvas);
    priv->tab_view = NULL;
    priv->main_box = NULL;
    priv->button_box = NULL;
    priv->apply_button = NULL;
    priv->cancel_button = NULL;
    priv->reset_button = NULL;
    priv->master_volume_slider = NULL;
    priv->music_volume_slider = NULL;
    priv->sfx_volume_slider = NULL;

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_settings_menu_state_parent_class)->exit (state);
}

static void
lrg_template_settings_menu_state_update (LrgGameState *state,
                                         gdouble       delta)
{
    /* Settings menu doesn't need regular updates */
    LRG_GAME_STATE_CLASS (lrg_template_settings_menu_state_parent_class)->update (state, delta);
}

static void
lrg_template_settings_menu_state_draw (LrgGameState *state)
{
    LrgTemplateSettingsMenuState *self;
    LrgTemplateSettingsMenuStatePrivate *priv;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;

    self = LRG_TEMPLATE_SETTINGS_MENU_STATE (state);
    priv = lrg_template_settings_menu_state_get_instance_private (self);

    engine = lrg_engine_get_default ();
    window = lrg_engine_get_window (engine);
    screen_width = lrg_window_get_width (window);
    screen_height = lrg_window_get_height (window);

    /* Draw background */
    grl_draw_rectangle (0, 0, screen_width, screen_height, priv->background_color);

    /* Draw UI */
    if (priv->canvas != NULL)
    {
        lrg_widget_draw (LRG_WIDGET (priv->canvas));
    }

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_settings_menu_state_parent_class)->draw (state);
}

static gboolean
lrg_template_settings_menu_state_handle_input (LrgGameState *state,
                                               gpointer      event)
{
    LrgTemplateSettingsMenuState *self;
    LrgTemplateSettingsMenuStatePrivate *priv;

    self = LRG_TEMPLATE_SETTINGS_MENU_STATE (state);
    priv = lrg_template_settings_menu_state_get_instance_private (self);

    /* Handle keyboard navigation */
    if (grl_input_is_key_pressed (GRL_KEY_LEFT) || grl_input_is_key_pressed (GRL_KEY_A))
    {
        navigate_left (self);
        return TRUE;
    }

    if (grl_input_is_key_pressed (GRL_KEY_RIGHT) || grl_input_is_key_pressed (GRL_KEY_D))
    {
        navigate_right (self);
        return TRUE;
    }

    if (grl_input_is_key_pressed (GRL_KEY_ENTER) || grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        activate_selected_button (self);
        return TRUE;
    }

    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        on_cancel_clicked (NULL, self);
        return TRUE;
    }

    /* Handle tab switching */
    if (grl_input_is_key_pressed (GRL_KEY_TAB))
    {
        guint active_tab;
        guint tab_count;

        if (priv->tab_view != NULL)
        {
            active_tab = lrg_tab_view_get_active_tab (priv->tab_view);
            tab_count = lrg_tab_view_get_tab_count (priv->tab_view);

            if (tab_count > 0)
            {
                active_tab = (active_tab + 1) % tab_count;
                lrg_tab_view_set_active_tab (priv->tab_view, active_tab);
            }
        }
        return TRUE;
    }

    /* Handle gamepad navigation */
    if (grl_input_is_gamepad_available (0))
    {
        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT))
        {
            navigate_left (self);
            return TRUE;
        }

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
        {
            navigate_right (self);
            return TRUE;
        }

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
        {
            activate_selected_button (self);
            return TRUE;
        }

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
        {
            on_cancel_clicked (NULL, self);
            return TRUE;
        }

        /* LB/RB for tab switching */
        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1))
        {
            guint active_tab;
            guint tab_count;

            if (priv->tab_view != NULL)
            {
                active_tab = lrg_tab_view_get_active_tab (priv->tab_view);
                tab_count = lrg_tab_view_get_tab_count (priv->tab_view);

                if (tab_count > 0 && active_tab > 0)
                {
                    lrg_tab_view_set_active_tab (priv->tab_view, active_tab - 1);
                }
                else if (tab_count > 0)
                {
                    lrg_tab_view_set_active_tab (priv->tab_view, tab_count - 1);
                }
            }
            return TRUE;
        }

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1))
        {
            guint active_tab;
            guint tab_count;

            if (priv->tab_view != NULL)
            {
                active_tab = lrg_tab_view_get_active_tab (priv->tab_view);
                tab_count = lrg_tab_view_get_tab_count (priv->tab_view);

                if (tab_count > 0)
                {
                    active_tab = (active_tab + 1) % tab_count;
                    lrg_tab_view_set_active_tab (priv->tab_view, active_tab);
                }
            }
            return TRUE;
        }
    }

    /* Pass to UI for mouse handling */
    if (priv->canvas != NULL)
    {
        return lrg_widget_handle_event (LRG_WIDGET (priv->canvas), event);
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Virtual Methods
 * ========================================================================== */

static void
lrg_template_settings_menu_state_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
    LrgTemplateSettingsMenuState *self;
    LrgTemplateSettingsMenuStatePrivate *priv;

    self = LRG_TEMPLATE_SETTINGS_MENU_STATE (object);
    priv = lrg_template_settings_menu_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_SHOW_GRAPHICS_TAB:
        priv->show_graphics_tab = g_value_get_boolean (value);
        break;

    case PROP_SHOW_AUDIO_TAB:
        priv->show_audio_tab = g_value_get_boolean (value);
        break;

    case PROP_SHOW_CONTROLS_TAB:
        priv->show_controls_tab = g_value_get_boolean (value);
        break;

    case PROP_SHOW_RESET_BUTTON:
        priv->show_reset_button = g_value_get_boolean (value);
        break;

    case PROP_CONFIRM_CANCEL:
        priv->confirm_cancel = g_value_get_boolean (value);
        break;

    case PROP_CONFIRM_RESET:
        priv->confirm_reset = g_value_get_boolean (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_settings_menu_state_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
    LrgTemplateSettingsMenuState *self;
    LrgTemplateSettingsMenuStatePrivate *priv;

    self = LRG_TEMPLATE_SETTINGS_MENU_STATE (object);
    priv = lrg_template_settings_menu_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_SHOW_GRAPHICS_TAB:
        g_value_set_boolean (value, priv->show_graphics_tab);
        break;

    case PROP_SHOW_AUDIO_TAB:
        g_value_set_boolean (value, priv->show_audio_tab);
        break;

    case PROP_SHOW_CONTROLS_TAB:
        g_value_set_boolean (value, priv->show_controls_tab);
        break;

    case PROP_SHOW_RESET_BUTTON:
        g_value_set_boolean (value, priv->show_reset_button);
        break;

    case PROP_CONFIRM_CANCEL:
        g_value_set_boolean (value, priv->confirm_cancel);
        break;

    case PROP_CONFIRM_RESET:
        g_value_set_boolean (value, priv->confirm_reset);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_settings_menu_state_finalize (GObject *object)
{
    LrgTemplateSettingsMenuState *self;
    LrgTemplateSettingsMenuStatePrivate *priv;

    self = LRG_TEMPLATE_SETTINGS_MENU_STATE (object);
    priv = lrg_template_settings_menu_state_get_instance_private (self);

    g_clear_pointer (&priv->background_color, grl_color_free);
    g_clear_object (&priv->canvas);

    if (priv->custom_tabs != NULL)
    {
        g_ptr_array_unref (priv->custom_tabs);
        priv->custom_tabs = NULL;
    }

    G_OBJECT_CLASS (lrg_template_settings_menu_state_parent_class)->finalize (object);
}

static void
lrg_template_settings_menu_state_class_init (LrgTemplateSettingsMenuStateClass *klass)
{
    GObjectClass *object_class;
    LrgGameStateClass *state_class;

    object_class = G_OBJECT_CLASS (klass);
    state_class = LRG_GAME_STATE_CLASS (klass);

    /* GObject methods */
    object_class->set_property = lrg_template_settings_menu_state_set_property;
    object_class->get_property = lrg_template_settings_menu_state_get_property;
    object_class->finalize = lrg_template_settings_menu_state_finalize;

    /* LrgGameState methods */
    state_class->enter = lrg_template_settings_menu_state_enter;
    state_class->exit = lrg_template_settings_menu_state_exit;
    state_class->update = lrg_template_settings_menu_state_update;
    state_class->draw = lrg_template_settings_menu_state_draw;
    state_class->handle_input = lrg_template_settings_menu_state_handle_input;

    /* LrgTemplateSettingsMenuState methods */
    klass->create_graphics_tab = lrg_template_settings_menu_state_real_create_graphics_tab;
    klass->create_audio_tab = lrg_template_settings_menu_state_real_create_audio_tab;
    klass->create_controls_tab = lrg_template_settings_menu_state_real_create_controls_tab;
    klass->create_custom_tab = lrg_template_settings_menu_state_real_create_custom_tab;
    klass->on_apply = lrg_template_settings_menu_state_real_on_apply;
    klass->on_cancel = lrg_template_settings_menu_state_real_on_cancel;
    klass->on_reset = lrg_template_settings_menu_state_real_on_reset;

    /**
     * LrgTemplateSettingsMenuState:show-graphics-tab:
     *
     * Whether the Graphics tab is shown.
     */
    props[PROP_SHOW_GRAPHICS_TAB] =
        g_param_spec_boolean ("show-graphics-tab",
                              "Show Graphics Tab",
                              "Whether the Graphics tab is shown",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateSettingsMenuState:show-audio-tab:
     *
     * Whether the Audio tab is shown.
     */
    props[PROP_SHOW_AUDIO_TAB] =
        g_param_spec_boolean ("show-audio-tab",
                              "Show Audio Tab",
                              "Whether the Audio tab is shown",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateSettingsMenuState:show-controls-tab:
     *
     * Whether the Controls tab is shown.
     */
    props[PROP_SHOW_CONTROLS_TAB] =
        g_param_spec_boolean ("show-controls-tab",
                              "Show Controls Tab",
                              "Whether the Controls tab is shown",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateSettingsMenuState:show-reset-button:
     *
     * Whether the Reset to Defaults button is shown.
     */
    props[PROP_SHOW_RESET_BUTTON] =
        g_param_spec_boolean ("show-reset-button",
                              "Show Reset Button",
                              "Whether the Reset button is shown",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateSettingsMenuState:confirm-cancel:
     *
     * Whether canceling with unsaved changes requires confirmation.
     */
    props[PROP_CONFIRM_CANCEL] =
        g_param_spec_boolean ("confirm-cancel",
                              "Confirm Cancel",
                              "Whether to confirm before canceling",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateSettingsMenuState:confirm-reset:
     *
     * Whether reset to defaults requires confirmation.
     */
    props[PROP_CONFIRM_RESET] =
        g_param_spec_boolean ("confirm-reset",
                              "Confirm Reset",
                              "Whether to confirm before resetting",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, props);

    /**
     * LrgTemplateSettingsMenuState::apply:
     * @self: the settings menu state
     *
     * Emitted when the Apply button is activated.
     */
    signals[SIGNAL_APPLY] =
        g_signal_new ("apply",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateSettingsMenuState::cancel:
     * @self: the settings menu state
     *
     * Emitted when the Cancel button is activated.
     */
    signals[SIGNAL_CANCEL] =
        g_signal_new ("cancel",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateSettingsMenuState::reset:
     * @self: the settings menu state
     *
     * Emitted when the Reset to Defaults button is activated.
     */
    signals[SIGNAL_RESET] =
        g_signal_new ("reset",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_template_settings_menu_state_init (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    /* Default configuration */
    priv->show_graphics_tab = TRUE;
    priv->show_audio_tab = TRUE;
    priv->show_controls_tab = TRUE;
    priv->show_reset_button = TRUE;
    priv->confirm_cancel = FALSE;
    priv->confirm_reset = TRUE;
    priv->has_changes = FALSE;
    priv->selected_button = 0;
    priv->visible_button_count = 3;

    priv->custom_tabs = g_ptr_array_new_with_free_func (custom_tab_entry_free);

    /* Default colors */
    priv->background_color = grl_color_new (30, 30, 40, 255);

    /* Set state to be blocking (not transparent) */
    g_object_set (self, "blocking", TRUE, NULL);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_new:
 *
 * Creates a new settings menu state.
 *
 * Returns: (transfer full): A new #LrgTemplateSettingsMenuState
 */
LrgTemplateSettingsMenuState *
lrg_template_settings_menu_state_new (void)
{
    return g_object_new (LRG_TYPE_TEMPLATE_SETTINGS_MENU_STATE, NULL);
}

/* ==========================================================================
 * Public API - Tab Visibility
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_show_graphics_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Graphics tab is shown.
 *
 * Returns: %TRUE if shown
 */
gboolean
lrg_template_settings_menu_state_get_show_graphics_tab (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), FALSE);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    return priv->show_graphics_tab;
}

/**
 * lrg_template_settings_menu_state_set_show_graphics_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Graphics tab
 *
 * Sets whether the Graphics tab is shown.
 */
void
lrg_template_settings_menu_state_set_show_graphics_tab (LrgTemplateSettingsMenuState *self,
                                                        gboolean                      show)
{
    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));

    g_object_set (self, "show-graphics-tab", show, NULL);
}

/**
 * lrg_template_settings_menu_state_get_show_audio_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Audio tab is shown.
 *
 * Returns: %TRUE if shown
 */
gboolean
lrg_template_settings_menu_state_get_show_audio_tab (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), FALSE);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    return priv->show_audio_tab;
}

/**
 * lrg_template_settings_menu_state_set_show_audio_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Audio tab
 *
 * Sets whether the Audio tab is shown.
 */
void
lrg_template_settings_menu_state_set_show_audio_tab (LrgTemplateSettingsMenuState *self,
                                                     gboolean                      show)
{
    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));

    g_object_set (self, "show-audio-tab", show, NULL);
}

/**
 * lrg_template_settings_menu_state_get_show_controls_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Controls tab is shown.
 *
 * Returns: %TRUE if shown
 */
gboolean
lrg_template_settings_menu_state_get_show_controls_tab (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), FALSE);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    return priv->show_controls_tab;
}

/**
 * lrg_template_settings_menu_state_set_show_controls_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Controls tab
 *
 * Sets whether the Controls tab is shown.
 */
void
lrg_template_settings_menu_state_set_show_controls_tab (LrgTemplateSettingsMenuState *self,
                                                        gboolean                      show)
{
    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));

    g_object_set (self, "show-controls-tab", show, NULL);
}

/* ==========================================================================
 * Public API - Custom Tabs
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_add_custom_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @name: the internal name for the tab
 * @label: the display label for the tab
 *
 * Adds a custom tab. Override create_custom_tab() to provide content.
 */
void
lrg_template_settings_menu_state_add_custom_tab (LrgTemplateSettingsMenuState *self,
                                                 const gchar                  *name,
                                                 const gchar                  *label)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    CustomTabEntry *entry;

    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (label != NULL);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    entry = custom_tab_entry_new (name, label);
    g_ptr_array_add (priv->custom_tabs, entry);
}

/**
 * lrg_template_settings_menu_state_remove_custom_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @name: the internal name for the tab
 *
 * Removes a custom tab.
 */
void
lrg_template_settings_menu_state_remove_custom_tab (LrgTemplateSettingsMenuState *self,
                                                    const gchar                  *name)
{
    LrgTemplateSettingsMenuStatePrivate *priv;
    CustomTabEntry *entry;
    guint i;

    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));
    g_return_if_fail (name != NULL);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    for (i = 0; i < priv->custom_tabs->len; i++)
    {
        entry = g_ptr_array_index (priv->custom_tabs, i);
        if (g_strcmp0 (entry->name, name) == 0)
        {
            g_ptr_array_remove_index (priv->custom_tabs, i);
            return;
        }
    }
}

/* ==========================================================================
 * Public API - Active Tab
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_active_tab:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets the index of the currently active tab.
 *
 * Returns: The active tab index
 */
guint
lrg_template_settings_menu_state_get_active_tab (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), 0);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    if (priv->tab_view != NULL)
        return lrg_tab_view_get_active_tab (priv->tab_view);

    return 0;
}

/**
 * lrg_template_settings_menu_state_set_active_tab:
 * @self: an #LrgTemplateSettingsMenuState
 * @index: the tab index to activate
 *
 * Sets which tab is currently active.
 */
void
lrg_template_settings_menu_state_set_active_tab (LrgTemplateSettingsMenuState *self,
                                                 guint                         index)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    if (priv->tab_view != NULL)
        lrg_tab_view_set_active_tab (priv->tab_view, index);
}

/* ==========================================================================
 * Public API - Button Visibility
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_show_reset_button:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether the Reset to Defaults button is shown.
 *
 * Returns: %TRUE if shown
 */
gboolean
lrg_template_settings_menu_state_get_show_reset_button (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), FALSE);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    return priv->show_reset_button;
}

/**
 * lrg_template_settings_menu_state_set_show_reset_button:
 * @self: an #LrgTemplateSettingsMenuState
 * @show: whether to show the Reset button
 *
 * Sets whether the Reset to Defaults button is shown.
 */
void
lrg_template_settings_menu_state_set_show_reset_button (LrgTemplateSettingsMenuState *self,
                                                        gboolean                      show)
{
    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));

    g_object_set (self, "show-reset-button", show, NULL);
}

/* ==========================================================================
 * Public API - Confirmation
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_get_confirm_cancel:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether canceling with unsaved changes requires confirmation.
 *
 * Returns: %TRUE if confirmation is required
 */
gboolean
lrg_template_settings_menu_state_get_confirm_cancel (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), FALSE);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    return priv->confirm_cancel;
}

/**
 * lrg_template_settings_menu_state_set_confirm_cancel:
 * @self: an #LrgTemplateSettingsMenuState
 * @confirm: whether to confirm before canceling
 *
 * Sets whether canceling with unsaved changes requires confirmation.
 */
void
lrg_template_settings_menu_state_set_confirm_cancel (LrgTemplateSettingsMenuState *self,
                                                     gboolean                      confirm)
{
    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));

    g_object_set (self, "confirm-cancel", confirm, NULL);
}

/**
 * lrg_template_settings_menu_state_get_confirm_reset:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Gets whether reset to defaults requires confirmation.
 *
 * Returns: %TRUE if confirmation is required
 */
gboolean
lrg_template_settings_menu_state_get_confirm_reset (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), FALSE);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    return priv->confirm_reset;
}

/**
 * lrg_template_settings_menu_state_set_confirm_reset:
 * @self: an #LrgTemplateSettingsMenuState
 * @confirm: whether to confirm before resetting
 *
 * Sets whether reset to defaults requires confirmation.
 */
void
lrg_template_settings_menu_state_set_confirm_reset (LrgTemplateSettingsMenuState *self,
                                                    gboolean                      confirm)
{
    g_return_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self));

    g_object_set (self, "confirm-reset", confirm, NULL);
}

/* ==========================================================================
 * Public API - Dirty State
 * ========================================================================== */

/**
 * lrg_template_settings_menu_state_has_unsaved_changes:
 * @self: an #LrgTemplateSettingsMenuState
 *
 * Checks if there are unsaved settings changes.
 *
 * Returns: %TRUE if there are unsaved changes
 */
gboolean
lrg_template_settings_menu_state_has_unsaved_changes (LrgTemplateSettingsMenuState *self)
{
    LrgTemplateSettingsMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (self), FALSE);

    priv = lrg_template_settings_menu_state_get_instance_private (self);

    return priv->has_changes;
}
