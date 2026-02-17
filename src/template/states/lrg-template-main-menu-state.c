/* lrg-template-main-menu-state.c - Main menu state implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../../config.h"
#include "lrg-template-main-menu-state.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-widget.h"
#include "../../ui/lrg-container.h"
#include "../../core/lrg-engine.h"
#include "../../graphics/lrg-window.h"
#include "../../lrg-log.h"

/**
 * SECTION:lrg-template-main-menu-state
 * @title: LrgTemplateMainMenuState
 * @short_description: Main menu state for game templates
 *
 * #LrgTemplateMainMenuState provides a ready-to-use main menu screen
 * with title display, standard buttons (New Game, Continue, Settings, Exit),
 * and support for custom menu items.
 *
 * The menu supports both mouse/touch and keyboard/gamepad navigation.
 * When using keyboard or gamepad, use Up/Down to navigate and Enter/A
 * to select.
 *
 * ## Customization
 *
 * The menu can be customized through properties:
 * - Title text and font size
 * - Background color or texture
 * - Button sizing and spacing
 * - Show/hide Continue button
 *
 * For more advanced customization, subclass and override:
 * - `create_menu_items()` to change the button layout
 * - `on_new_game()`, `on_continue()`, etc. to handle button actions
 *
 * ## Signals
 *
 * The state emits signals for button activations:
 * - `new-game` - New Game button pressed
 * - `continue-game` - Continue button pressed
 * - `settings` - Settings button pressed
 * - `exit-game` - Exit button pressed
 * - `custom-item` - Custom menu item pressed (with item_id)
 */

/* Custom menu item structure */
typedef struct
{
    gchar     *item_id;
    gchar     *label;
    gint       position;
    LrgButton *button;
} CustomMenuItem;

typedef struct _LrgTemplateMainMenuStatePrivate
{
    /* Title */
    gchar         *title;
    gfloat         title_font_size;
    LrgLabel      *title_label;

    /* Background */
    GrlColor      *background_color;
    GrlTexture    *background_texture;

    /* Menu configuration */
    gboolean       show_continue;
    gfloat         button_spacing;
    gfloat         button_width;
    gfloat         button_height;

    /* UI Components */
    LrgCanvas     *canvas;
    LrgVBox       *menu_container;
    LrgButton     *btn_new_game;
    LrgButton     *btn_continue;
    LrgButton     *btn_settings;
    LrgButton     *btn_exit;

    /* Custom items */
    GPtrArray     *custom_items;

    /* Navigation state */
    gint           selected_index;
    GPtrArray     *visible_buttons;  /* Buttons in display order */

    /* State flags */
    gboolean       menu_built;
} LrgTemplateMainMenuStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTemplateMainMenuState, lrg_template_main_menu_state, LRG_TYPE_GAME_STATE)

enum
{
    PROP_0,
    PROP_TITLE,
    PROP_TITLE_FONT_SIZE,
    PROP_BACKGROUND_COLOR,
    PROP_BACKGROUND_TEXTURE,
    PROP_SHOW_CONTINUE,
    PROP_BUTTON_SPACING,
    PROP_BUTTON_WIDTH,
    PROP_BUTTON_HEIGHT,
    PROP_SELECTED_INDEX,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_NEW_GAME,
    SIGNAL_CONTINUE_GAME,
    SIGNAL_SETTINGS,
    SIGNAL_EXIT_GAME,
    SIGNAL_CUSTOM_ITEM,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default values */
#define DEFAULT_TITLE_FONT_SIZE   (64.0f)
#define DEFAULT_BUTTON_SPACING    (10.0f)
#define DEFAULT_BUTTON_WIDTH      (300.0f)
#define DEFAULT_BUTTON_HEIGHT     (50.0f)

/* ========================================================================== */
/* Private Helpers                                                            */
/* ========================================================================== */

static CustomMenuItem *
custom_menu_item_new (const gchar *item_id,
                      const gchar *label,
                      gint         position)
{
    CustomMenuItem *item;

    item = g_new0 (CustomMenuItem, 1);
    item->item_id = g_strdup (item_id);
    item->label = g_strdup (label);
    item->position = position;
    item->button = NULL;

    return item;
}

static void
custom_menu_item_free (CustomMenuItem *item)
{
    if (item == NULL)
        return;

    g_free (item->item_id);
    g_free (item->label);
    g_clear_object (&item->button);
    g_free (item);
}

static void
update_button_selection (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;
    guint i;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (priv->visible_buttons == NULL)
        return;

    /* Update visual selection state for all buttons */
    for (i = 0; i < priv->visible_buttons->len; i++)
    {
        LrgButton *btn = g_ptr_array_index (priv->visible_buttons, i);

        if ((gint)i == priv->selected_index)
        {
            /* Highlight selected: bright bg + white text */
            g_autoptr(GrlColor) sel_bg = grl_color_new (60, 120, 220, 255);
            g_autoptr(GrlColor) sel_text = grl_color_new (255, 255, 255, 255);
            lrg_button_set_normal_color (btn, sel_bg);
            lrg_button_set_text_color (btn, sel_text);
        }
        else
        {
            /* Dim unselected: transparent bg + muted text */
            g_autoptr(GrlColor) dim_bg = grl_color_new (40, 40, 45, 180);
            g_autoptr(GrlColor) dim_text = grl_color_new (140, 140, 140, 255);
            lrg_button_set_normal_color (btn, dim_bg);
            lrg_button_set_text_color (btn, dim_text);
        }
    }
}

static void
navigate_menu (LrgTemplateMainMenuState *self,
               gint                      direction)
{
    LrgTemplateMainMenuStatePrivate *priv;
    gint new_index;
    gint count;

    priv = lrg_template_main_menu_state_get_instance_private (self);

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
activate_selected (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;
    LrgTemplateMainMenuStateClass *klass;
    LrgButton *btn;

    priv = lrg_template_main_menu_state_get_instance_private (self);
    klass = LRG_TEMPLATE_MAIN_MENU_STATE_GET_CLASS (self);

    if (priv->visible_buttons == NULL ||
        priv->selected_index < 0 ||
        (guint)priv->selected_index >= priv->visible_buttons->len)
        return;

    btn = g_ptr_array_index (priv->visible_buttons, priv->selected_index);

    /* Determine which button was activated */
    if (btn == priv->btn_new_game)
    {
        if (klass->on_new_game != NULL)
            klass->on_new_game (self);
    }
    else if (btn == priv->btn_continue)
    {
        if (klass->on_continue != NULL)
            klass->on_continue (self);
    }
    else if (btn == priv->btn_settings)
    {
        if (klass->on_settings != NULL)
            klass->on_settings (self);
    }
    else if (btn == priv->btn_exit)
    {
        if (klass->on_exit != NULL)
            klass->on_exit (self);
    }
    else
    {
        /* Check custom items */
        guint i;

        for (i = 0; i < priv->custom_items->len; i++)
        {
            CustomMenuItem *item = g_ptr_array_index (priv->custom_items, i);

            if (item->button == btn)
            {
                if (klass->on_custom_item != NULL)
                    klass->on_custom_item (self, item->item_id);
                break;
            }
        }
    }
}

/* Button click callbacks */
static void
on_new_game_clicked (LrgButton *button,
                     gpointer   user_data)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (user_data);
    LrgTemplateMainMenuStateClass *klass;

    klass = LRG_TEMPLATE_MAIN_MENU_STATE_GET_CLASS (self);

    if (klass->on_new_game != NULL)
        klass->on_new_game (self);
}

static void
on_continue_clicked (LrgButton *button,
                     gpointer   user_data)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (user_data);
    LrgTemplateMainMenuStateClass *klass;

    klass = LRG_TEMPLATE_MAIN_MENU_STATE_GET_CLASS (self);

    if (klass->on_continue != NULL)
        klass->on_continue (self);
}

static void
on_settings_clicked (LrgButton *button,
                     gpointer   user_data)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (user_data);
    LrgTemplateMainMenuStateClass *klass;

    klass = LRG_TEMPLATE_MAIN_MENU_STATE_GET_CLASS (self);

    if (klass->on_settings != NULL)
        klass->on_settings (self);
}

static void
on_exit_clicked (LrgButton *button,
                 gpointer   user_data)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (user_data);
    LrgTemplateMainMenuStateClass *klass;

    klass = LRG_TEMPLATE_MAIN_MENU_STATE_GET_CLASS (self);

    if (klass->on_exit != NULL)
        klass->on_exit (self);
}

static void
on_custom_item_clicked (LrgButton *button,
                        gpointer   user_data)
{
    LrgTemplateMainMenuState *self;
    LrgTemplateMainMenuStatePrivate *priv;
    LrgTemplateMainMenuStateClass *klass;
    CustomMenuItem *found_item = NULL;
    guint i;

    /* user_data is the menu state */
    self = LRG_TEMPLATE_MAIN_MENU_STATE (user_data);
    priv = lrg_template_main_menu_state_get_instance_private (self);
    klass = LRG_TEMPLATE_MAIN_MENU_STATE_GET_CLASS (self);

    /* Find which custom item was clicked */
    for (i = 0; i < priv->custom_items->len; i++)
    {
        CustomMenuItem *item = g_ptr_array_index (priv->custom_items, i);

        if (item->button == button)
        {
            found_item = item;
            break;
        }
    }

    if (found_item != NULL && klass->on_custom_item != NULL)
        klass->on_custom_item (self, found_item->item_id);
}

static LrgButton *
create_menu_button (LrgTemplateMainMenuState *self,
                    const gchar              *text)
{
    LrgTemplateMainMenuStatePrivate *priv;
    LrgButton *btn;
    g_autoptr(GrlColor) normal_color = NULL;
    g_autoptr(GrlColor) hover_color = NULL;
    g_autoptr(GrlColor) pressed_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    btn = lrg_button_new (text);

    /* Set button size */
    lrg_widget_set_width (LRG_WIDGET (btn), priv->button_width);
    lrg_widget_set_height (LRG_WIDGET (btn), priv->button_height);

    /* Set button colors */
    normal_color = grl_color_new (80, 80, 80, 255);
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
rebuild_visible_buttons (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;
    guint i;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    /* Clear and rebuild the visible buttons array */
    g_ptr_array_set_size (priv->visible_buttons, 0);

    /* Add buttons in order: New Game, Continue (if shown), custom items, Settings, Exit */
    if (priv->btn_new_game != NULL)
        g_ptr_array_add (priv->visible_buttons, priv->btn_new_game);

    if (priv->show_continue && priv->btn_continue != NULL)
        g_ptr_array_add (priv->visible_buttons, priv->btn_continue);

    /* Add custom items sorted by position */
    for (i = 0; i < priv->custom_items->len; i++)
    {
        CustomMenuItem *item = g_ptr_array_index (priv->custom_items, i);

        if (item->button != NULL)
            g_ptr_array_add (priv->visible_buttons, item->button);
    }

    if (priv->btn_settings != NULL)
        g_ptr_array_add (priv->visible_buttons, priv->btn_settings);

    if (priv->btn_exit != NULL)
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

/* ========================================================================== */
/* Virtual Method Implementations                                             */
/* ========================================================================== */

static void
lrg_template_main_menu_state_real_create_menu_items (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;
    guint i;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    /* Create standard buttons */
    priv->btn_new_game = create_menu_button (self, "New Game");
    g_signal_connect (priv->btn_new_game, "clicked",
                      G_CALLBACK (on_new_game_clicked), self);

    priv->btn_continue = create_menu_button (self, "Continue");
    g_signal_connect (priv->btn_continue, "clicked",
                      G_CALLBACK (on_continue_clicked), self);

    priv->btn_settings = create_menu_button (self, "Settings");
    g_signal_connect (priv->btn_settings, "clicked",
                      G_CALLBACK (on_settings_clicked), self);

    priv->btn_exit = create_menu_button (self, "Exit");
    g_signal_connect (priv->btn_exit, "clicked",
                      G_CALLBACK (on_exit_clicked), self);

    /* Create custom item buttons */
    for (i = 0; i < priv->custom_items->len; i++)
    {
        CustomMenuItem *item = g_ptr_array_index (priv->custom_items, i);

        item->button = create_menu_button (self, item->label);
        g_signal_connect (item->button, "clicked",
                          G_CALLBACK (on_custom_item_clicked), self);
    }
}

static void
lrg_template_main_menu_state_real_on_new_game (LrgTemplateMainMenuState *self)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Main menu: New Game selected");
    g_signal_emit (self, signals[SIGNAL_NEW_GAME], 0);
}

static void
lrg_template_main_menu_state_real_on_continue (LrgTemplateMainMenuState *self)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Main menu: Continue selected");
    g_signal_emit (self, signals[SIGNAL_CONTINUE_GAME], 0);
}

static void
lrg_template_main_menu_state_real_on_settings (LrgTemplateMainMenuState *self)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Main menu: Settings selected");
    g_signal_emit (self, signals[SIGNAL_SETTINGS], 0);
}

static void
lrg_template_main_menu_state_real_on_exit (LrgTemplateMainMenuState *self)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Main menu: Exit selected");
    g_signal_emit (self, signals[SIGNAL_EXIT_GAME], 0);
}

static void
lrg_template_main_menu_state_real_on_custom_item (LrgTemplateMainMenuState *self,
                                                  const gchar              *item_id)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Main menu: Custom item '%s' selected", item_id);
    g_signal_emit (self, signals[SIGNAL_CUSTOM_ITEM], 0, item_id);
}

/* ========================================================================== */
/* LrgGameState Interface Implementation                                      */
/* ========================================================================== */

static void
lrg_template_main_menu_state_enter (LrgGameState *state)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (state);
    LrgTemplateMainMenuStatePrivate *priv;
    LrgTemplateMainMenuStateClass *klass;
    g_autoptr(GrlColor) title_color = NULL;
    guint i;

    priv = lrg_template_main_menu_state_get_instance_private (self);
    klass = LRG_TEMPLATE_MAIN_MENU_STATE_GET_CLASS (self);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Main menu state entering");

    /* Create canvas and containers */
    priv->canvas = lrg_canvas_new ();
    priv->menu_container = lrg_vbox_new ();

    /* Create title label */
    priv->title_label = lrg_label_new (priv->title);
    lrg_label_set_font_size (priv->title_label, priv->title_font_size);
    title_color = grl_color_new (255, 255, 255, 255);
    lrg_label_set_color (priv->title_label, title_color);
    lrg_label_set_alignment (priv->title_label, LRG_TEXT_ALIGN_CENTER);

    /* Call virtual method to create menu items */
    if (klass->create_menu_items != NULL)
        klass->create_menu_items (self);

    /* Build visible buttons list */
    rebuild_visible_buttons (self);

    /* Add title to container */
    lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                             LRG_WIDGET (priv->title_label));

    /* Add spacer after title */
    {
        LrgWidget *spacer = g_object_new (LRG_TYPE_WIDGET, NULL);
        lrg_widget_set_height (spacer, 50.0f);
        lrg_container_add_child (LRG_CONTAINER (priv->menu_container), spacer);
    }

    /* Add buttons to container in order */
    lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                             LRG_WIDGET (priv->btn_new_game));

    if (priv->show_continue)
    {
        lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                                 LRG_WIDGET (priv->btn_continue));
    }

    /* Add custom items */
    for (i = 0; i < priv->custom_items->len; i++)
    {
        CustomMenuItem *item = g_ptr_array_index (priv->custom_items, i);

        if (item->button != NULL)
        {
            lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                                     LRG_WIDGET (item->button));
        }
    }

    lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                             LRG_WIDGET (priv->btn_settings));

    lrg_container_add_child (LRG_CONTAINER (priv->menu_container),
                             LRG_WIDGET (priv->btn_exit));

    /* Add menu container to canvas */
    lrg_container_add_child (LRG_CONTAINER (priv->canvas),
                             LRG_WIDGET (priv->menu_container));

    /* Center the menu container */
    {
        LrgEngine *engine = lrg_engine_get_default ();
        LrgWindow *window = lrg_engine_get_window (engine);
        gint screen_width = lrg_window_get_width (window);
        gint screen_height = lrg_window_get_height (window);
        gfloat menu_x;
        gfloat menu_y;

        menu_x = (screen_width - priv->button_width) / 2.0f;
        menu_y = screen_height * 0.25f;  /* Start 25% from top */

        lrg_widget_set_x (LRG_WIDGET (priv->menu_container), menu_x);
        lrg_widget_set_y (LRG_WIDGET (priv->menu_container), menu_y);
    }

    priv->menu_built = TRUE;
    priv->selected_index = 0;
    update_button_selection (self);
}

static void
lrg_template_main_menu_state_exit (LrgGameState *state)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (state);
    LrgTemplateMainMenuStatePrivate *priv;
    guint i;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Main menu state exiting");

    /* Clear UI references */
    g_clear_object (&priv->title_label);
    g_clear_object (&priv->btn_new_game);
    g_clear_object (&priv->btn_continue);
    g_clear_object (&priv->btn_settings);
    g_clear_object (&priv->btn_exit);

    /* Clear custom item buttons */
    for (i = 0; i < priv->custom_items->len; i++)
    {
        CustomMenuItem *item = g_ptr_array_index (priv->custom_items, i);
        g_clear_object (&item->button);
    }

    g_clear_object (&priv->menu_container);
    g_clear_object (&priv->canvas);

    g_ptr_array_set_size (priv->visible_buttons, 0);

    priv->menu_built = FALSE;
}

static void
lrg_template_main_menu_state_update (LrgGameState *state,
                                     gdouble       delta)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (state);
    LrgTemplateMainMenuStatePrivate *priv;

    priv = lrg_template_main_menu_state_get_instance_private (self);

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

    /* Process canvas input (mouse/touch) */
    lrg_canvas_handle_input (priv->canvas);
}

static void
lrg_template_main_menu_state_draw (LrgGameState *state)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (state);
    LrgTemplateMainMenuStatePrivate *priv;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    engine = lrg_engine_get_default ();
    window = lrg_engine_get_window (engine);
    screen_width = lrg_window_get_width (window);
    screen_height = lrg_window_get_height (window);

    /* Draw background */
    if (priv->background_texture != NULL)
    {
        /* Scale texture to fill screen using texture pro */
        g_autoptr(GrlRectangle) source = NULL;
        g_autoptr(GrlRectangle) dest = NULL;
        g_autoptr(GrlVector2) origin = NULL;
        g_autoptr(GrlColor) white = NULL;
        gint tex_width;
        gint tex_height;

        tex_width = grl_texture_get_width (priv->background_texture);
        tex_height = grl_texture_get_height (priv->background_texture);

        source = grl_rectangle_new (0.0f, 0.0f, (gfloat) tex_width, (gfloat) tex_height);
        dest = grl_rectangle_new (0.0f, 0.0f, (gfloat) screen_width, (gfloat) screen_height);
        origin = grl_vector2_new (0.0f, 0.0f);
        white = grl_color_new_white ();

        grl_draw_texture_pro (priv->background_texture, source, dest, origin, 0.0f, white);
    }
    else if (priv->background_color != NULL)
    {
        grl_draw_clear_background (priv->background_color);
    }
    else
    {
        g_autoptr(GrlColor) default_bg = grl_color_new (30, 30, 40, 255);
        grl_draw_clear_background (default_bg);
    }

    /* Render UI */
    if (priv->canvas != NULL)
        lrg_canvas_render (priv->canvas);
}

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
lrg_template_main_menu_state_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (object);

    switch (prop_id)
    {
    case PROP_TITLE:
        lrg_template_main_menu_state_set_title (self, g_value_get_string (value));
        break;
    case PROP_TITLE_FONT_SIZE:
        lrg_template_main_menu_state_set_title_font_size (self, g_value_get_float (value));
        break;
    case PROP_BACKGROUND_COLOR:
        lrg_template_main_menu_state_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_BACKGROUND_TEXTURE:
        lrg_template_main_menu_state_set_background_texture (self, g_value_get_object (value));
        break;
    case PROP_SHOW_CONTINUE:
        lrg_template_main_menu_state_set_show_continue (self, g_value_get_boolean (value));
        break;
    case PROP_BUTTON_SPACING:
        lrg_template_main_menu_state_set_button_spacing (self, g_value_get_float (value));
        break;
    case PROP_BUTTON_WIDTH:
        lrg_template_main_menu_state_set_button_width (self, g_value_get_float (value));
        break;
    case PROP_BUTTON_HEIGHT:
        lrg_template_main_menu_state_set_button_height (self, g_value_get_float (value));
        break;
    case PROP_SELECTED_INDEX:
        lrg_template_main_menu_state_set_selected_index (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_main_menu_state_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (object);
    LrgTemplateMainMenuStatePrivate *priv;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TITLE:
        g_value_set_string (value, priv->title);
        break;
    case PROP_TITLE_FONT_SIZE:
        g_value_set_float (value, priv->title_font_size);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, priv->background_color);
        break;
    case PROP_BACKGROUND_TEXTURE:
        g_value_set_object (value, priv->background_texture);
        break;
    case PROP_SHOW_CONTINUE:
        g_value_set_boolean (value, priv->show_continue);
        break;
    case PROP_BUTTON_SPACING:
        g_value_set_float (value, priv->button_spacing);
        break;
    case PROP_BUTTON_WIDTH:
        g_value_set_float (value, priv->button_width);
        break;
    case PROP_BUTTON_HEIGHT:
        g_value_set_float (value, priv->button_height);
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
lrg_template_main_menu_state_finalize (GObject *object)
{
    LrgTemplateMainMenuState *self = LRG_TEMPLATE_MAIN_MENU_STATE (object);
    LrgTemplateMainMenuStatePrivate *priv;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    g_clear_pointer (&priv->title, g_free);
    g_clear_pointer (&priv->background_color, grl_color_free);
    g_clear_object (&priv->background_texture);

    g_clear_pointer (&priv->custom_items, g_ptr_array_unref);
    g_clear_pointer (&priv->visible_buttons, g_ptr_array_unref);

    /* UI objects should already be freed in exit() */
    g_clear_object (&priv->canvas);

    G_OBJECT_CLASS (lrg_template_main_menu_state_parent_class)->finalize (object);
}

static void
lrg_template_main_menu_state_class_init (LrgTemplateMainMenuStateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    object_class->set_property = lrg_template_main_menu_state_set_property;
    object_class->get_property = lrg_template_main_menu_state_get_property;
    object_class->finalize = lrg_template_main_menu_state_finalize;

    /* Override LrgGameState methods */
    state_class->enter = lrg_template_main_menu_state_enter;
    state_class->exit = lrg_template_main_menu_state_exit;
    state_class->update = lrg_template_main_menu_state_update;
    state_class->draw = lrg_template_main_menu_state_draw;

    /* Set default implementations for virtual methods */
    klass->create_menu_items = lrg_template_main_menu_state_real_create_menu_items;
    klass->on_new_game = lrg_template_main_menu_state_real_on_new_game;
    klass->on_continue = lrg_template_main_menu_state_real_on_continue;
    klass->on_settings = lrg_template_main_menu_state_real_on_settings;
    klass->on_exit = lrg_template_main_menu_state_real_on_exit;
    klass->on_custom_item = lrg_template_main_menu_state_real_on_custom_item;

    /**
     * LrgTemplateMainMenuState:title:
     *
     * The game title displayed at the top of the menu.
     *
     * Since: 1.0
     */
    properties[PROP_TITLE] =
        g_param_spec_string ("title",
                             "Title",
                             "Game title displayed in menu",
                             "Game Title",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:title-font-size:
     *
     * The font size for the title text.
     *
     * Since: 1.0
     */
    properties[PROP_TITLE_FONT_SIZE] =
        g_param_spec_float ("title-font-size",
                            "Title Font Size",
                            "Font size for title",
                            8.0f, 256.0f, DEFAULT_TITLE_FONT_SIZE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:background-color:
     *
     * The background color. Used if no background texture is set.
     *
     * Since: 1.0
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "Background color for menu",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:background-texture:
     *
     * The background texture. If set, drawn instead of background color.
     *
     * Since: 1.0
     */
    properties[PROP_BACKGROUND_TEXTURE] =
        g_param_spec_object ("background-texture",
                             "Background Texture",
                             "Background image for menu",
                             GRL_TYPE_TEXTURE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:show-continue:
     *
     * Whether the Continue button is shown. Set to %FALSE when
     * there is no save game available.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_CONTINUE] =
        g_param_spec_boolean ("show-continue",
                              "Show Continue",
                              "Whether Continue button is visible",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:button-spacing:
     *
     * Spacing between menu buttons.
     *
     * Since: 1.0
     */
    properties[PROP_BUTTON_SPACING] =
        g_param_spec_float ("button-spacing",
                            "Button Spacing",
                            "Spacing between buttons",
                            0.0f, 200.0f, DEFAULT_BUTTON_SPACING,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:button-width:
     *
     * Width of menu buttons.
     *
     * Since: 1.0
     */
    properties[PROP_BUTTON_WIDTH] =
        g_param_spec_float ("button-width",
                            "Button Width",
                            "Width of menu buttons",
                            50.0f, 1000.0f, DEFAULT_BUTTON_WIDTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:button-height:
     *
     * Height of menu buttons.
     *
     * Since: 1.0
     */
    properties[PROP_BUTTON_HEIGHT] =
        g_param_spec_float ("button-height",
                            "Button Height",
                            "Height of menu buttons",
                            20.0f, 200.0f, DEFAULT_BUTTON_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateMainMenuState:selected-index:
     *
     * The currently selected menu item index (for keyboard navigation).
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
     * LrgTemplateMainMenuState::new-game:
     * @self: the #LrgTemplateMainMenuState
     *
     * Emitted when the New Game button is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_NEW_GAME] =
        g_signal_new ("new-game",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateMainMenuState::continue-game:
     * @self: the #LrgTemplateMainMenuState
     *
     * Emitted when the Continue button is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CONTINUE_GAME] =
        g_signal_new ("continue-game",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateMainMenuState::settings:
     * @self: the #LrgTemplateMainMenuState
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
     * LrgTemplateMainMenuState::exit-game:
     * @self: the #LrgTemplateMainMenuState
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

    /**
     * LrgTemplateMainMenuState::custom-item:
     * @self: the #LrgTemplateMainMenuState
     * @item_id: the custom item identifier
     *
     * Emitted when a custom menu item is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CUSTOM_ITEM] =
        g_signal_new ("custom-item",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_template_main_menu_state_init (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    priv = lrg_template_main_menu_state_get_instance_private (self);

    priv->title = g_strdup ("Game Title");
    priv->title_font_size = DEFAULT_TITLE_FONT_SIZE;
    priv->background_color = NULL;
    priv->background_texture = NULL;
    priv->show_continue = TRUE;
    priv->button_spacing = DEFAULT_BUTTON_SPACING;
    priv->button_width = DEFAULT_BUTTON_WIDTH;
    priv->button_height = DEFAULT_BUTTON_HEIGHT;

    priv->custom_items = g_ptr_array_new_with_free_func ((GDestroyNotify)custom_menu_item_free);
    priv->visible_buttons = g_ptr_array_new ();

    priv->selected_index = 0;
    priv->menu_built = FALSE;

    /* Set state name for debugging */
    lrg_game_state_set_name (LRG_GAME_STATE (self), "MainMenu");
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * lrg_template_main_menu_state_new:
 *
 * Creates a new main menu state with default settings.
 *
 * Returns: (transfer full): A new #LrgTemplateMainMenuState
 *
 * Since: 1.0
 */
LrgTemplateMainMenuState *
lrg_template_main_menu_state_new (void)
{
    return g_object_new (LRG_TYPE_TEMPLATE_MAIN_MENU_STATE, NULL);
}

/**
 * lrg_template_main_menu_state_new_with_title:
 * @title: the game title to display
 *
 * Creates a new main menu state with the specified title.
 *
 * Returns: (transfer full): A new #LrgTemplateMainMenuState
 *
 * Since: 1.0
 */
LrgTemplateMainMenuState *
lrg_template_main_menu_state_new_with_title (const gchar *title)
{
    return g_object_new (LRG_TYPE_TEMPLATE_MAIN_MENU_STATE,
                         "title", title,
                         NULL);
}

const gchar *
lrg_template_main_menu_state_get_title (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), NULL);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->title;
}

void
lrg_template_main_menu_state_set_title (LrgTemplateMainMenuState *self,
                                        const gchar              *title)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (g_strcmp0 (priv->title, title) != 0)
    {
        g_free (priv->title);
        priv->title = g_strdup (title);

        if (priv->title_label != NULL)
            lrg_label_set_text (priv->title_label, title);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
    }
}

gfloat
lrg_template_main_menu_state_get_title_font_size (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), DEFAULT_TITLE_FONT_SIZE);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->title_font_size;
}

void
lrg_template_main_menu_state_set_title_font_size (LrgTemplateMainMenuState *self,
                                                  gfloat                    size)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (priv->title_font_size != size)
    {
        priv->title_font_size = size;

        if (priv->title_label != NULL)
            lrg_label_set_font_size (priv->title_label, size);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE_FONT_SIZE]);
    }
}

const GrlColor *
lrg_template_main_menu_state_get_background_color (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), NULL);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->background_color;
}

void
lrg_template_main_menu_state_set_background_color (LrgTemplateMainMenuState *self,
                                                   const GrlColor           *color)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    g_clear_pointer (&priv->background_color, grl_color_free);
    if (color != NULL)
        priv->background_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

GrlTexture *
lrg_template_main_menu_state_get_background_texture (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), NULL);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->background_texture;
}

void
lrg_template_main_menu_state_set_background_texture (LrgTemplateMainMenuState *self,
                                                     GrlTexture               *texture)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (g_set_object (&priv->background_texture, texture))
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_TEXTURE]);
}

gboolean
lrg_template_main_menu_state_get_show_continue (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), TRUE);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->show_continue;
}

void
lrg_template_main_menu_state_set_show_continue (LrgTemplateMainMenuState *self,
                                                gboolean                  show)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    show = !!show;

    if (priv->show_continue != show)
    {
        priv->show_continue = show;

        if (priv->menu_built)
            rebuild_visible_buttons (self);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_CONTINUE]);
    }
}

void
lrg_template_main_menu_state_add_custom_item (LrgTemplateMainMenuState *self,
                                              const gchar              *item_id,
                                              const gchar              *label,
                                              gint                      position)
{
    LrgTemplateMainMenuStatePrivate *priv;
    CustomMenuItem *item;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));
    g_return_if_fail (item_id != NULL);
    g_return_if_fail (label != NULL);

    priv = lrg_template_main_menu_state_get_instance_private (self);

    item = custom_menu_item_new (item_id, label, position);
    g_ptr_array_add (priv->custom_items, item);

    if (priv->menu_built)
        lrg_template_main_menu_state_rebuild_menu (self);
}

void
lrg_template_main_menu_state_remove_custom_item (LrgTemplateMainMenuState *self,
                                                 const gchar              *item_id)
{
    LrgTemplateMainMenuStatePrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));
    g_return_if_fail (item_id != NULL);

    priv = lrg_template_main_menu_state_get_instance_private (self);

    for (i = 0; i < priv->custom_items->len; i++)
    {
        CustomMenuItem *item = g_ptr_array_index (priv->custom_items, i);

        if (g_strcmp0 (item->item_id, item_id) == 0)
        {
            g_ptr_array_remove_index (priv->custom_items, i);

            if (priv->menu_built)
                lrg_template_main_menu_state_rebuild_menu (self);

            return;
        }
    }
}

gint
lrg_template_main_menu_state_get_selected_index (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), 0);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->selected_index;
}

void
lrg_template_main_menu_state_set_selected_index (LrgTemplateMainMenuState *self,
                                                 gint                      index)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (index >= 0 && (guint)index < priv->visible_buttons->len)
    {
        priv->selected_index = index;
        update_button_selection (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED_INDEX]);
    }
}

gint
lrg_template_main_menu_state_get_menu_item_count (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), 0);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return (gint)priv->visible_buttons->len;
}

gfloat
lrg_template_main_menu_state_get_button_spacing (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), DEFAULT_BUTTON_SPACING);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->button_spacing;
}

void
lrg_template_main_menu_state_set_button_spacing (LrgTemplateMainMenuState *self,
                                                 gfloat                    spacing)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (priv->button_spacing != spacing)
    {
        priv->button_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUTTON_SPACING]);
    }
}

gfloat
lrg_template_main_menu_state_get_button_width (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), DEFAULT_BUTTON_WIDTH);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->button_width;
}

void
lrg_template_main_menu_state_set_button_width (LrgTemplateMainMenuState *self,
                                               gfloat                    width)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (priv->button_width != width)
    {
        priv->button_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUTTON_WIDTH]);
    }
}

gfloat
lrg_template_main_menu_state_get_button_height (LrgTemplateMainMenuState *self)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self), DEFAULT_BUTTON_HEIGHT);

    priv = lrg_template_main_menu_state_get_instance_private (self);
    return priv->button_height;
}

void
lrg_template_main_menu_state_set_button_height (LrgTemplateMainMenuState *self,
                                                gfloat                    height)
{
    LrgTemplateMainMenuStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    priv = lrg_template_main_menu_state_get_instance_private (self);

    if (priv->button_height != height)
    {
        priv->button_height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUTTON_HEIGHT]);
    }
}

void
lrg_template_main_menu_state_rebuild_menu (LrgTemplateMainMenuState *self)
{
    g_return_if_fail (LRG_IS_TEMPLATE_MAIN_MENU_STATE (self));

    /* Exit and re-enter to rebuild */
    lrg_game_state_exit (LRG_GAME_STATE (self));
    lrg_game_state_enter (LRG_GAME_STATE (self));
}
