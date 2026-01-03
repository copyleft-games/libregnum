/* lrg-template-error-state.c - Error recovery state for game templates
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-template-error-state.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../core/lrg-engine.h"
#include "../../graphics/lrg-window.h"
#include "../../lrg-log.h"

/**
 * SECTION:lrg-template-error-state
 * @title: LrgTemplateErrorState
 * @short_description: Error recovery state for game templates
 *
 * #LrgTemplateErrorState provides a standard error display screen with
 * recovery options. It displays an error message and offers buttons for
 * retry, returning to the main menu, or exiting the game.
 *
 * ## Features
 *
 * - Customizable title and error message
 * - Optional Retry button for recoverable errors
 * - Main Menu button for returning to a safe state
 * - Exit button for graceful shutdown
 * - GError integration for easy error display
 *
 * ## Signals
 *
 * The state emits signals when buttons are activated:
 * - #LrgTemplateErrorState::retry - Retry button activated
 * - #LrgTemplateErrorState::main-menu - Main Menu button activated
 * - #LrgTemplateErrorState::exit-game - Exit button activated
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * // Create error state from a GError
 * GError *error = NULL;
 * if (!load_game_data (&error))
 * {
 *     LrgGameState *error_state = (LrgGameState *)
 *         lrg_template_error_state_new_with_error (error);
 *     lrg_game_state_manager_push (manager, error_state);
 *     g_error_free (error);
 * }
 * ]|
 */

typedef struct _LrgTemplateErrorStatePrivate LrgTemplateErrorStatePrivate;

struct _LrgTemplateErrorStatePrivate
{
    /* UI Elements */
    LrgCanvas *canvas;
    LrgVBox   *content_box;
    LrgLabel  *title_label;
    LrgLabel  *message_label;
    LrgButton *retry_button;
    LrgButton *main_menu_button;
    LrgButton *exit_button;

    /* Configuration */
    gchar    *error_message;
    gchar    *title;
    gboolean  allow_retry;
    gboolean  show_main_menu;
    gboolean  show_exit;

    /* Navigation */
    gint      selected_index;
    gint      visible_button_count;

    /* Colors */
    GrlColor *background_color;
    GrlColor *error_color;
};

G_DEFINE_TYPE_WITH_PRIVATE (LrgTemplateErrorState, lrg_template_error_state,
                            LRG_TYPE_GAME_STATE)

enum
{
    PROP_0,
    PROP_ERROR_MESSAGE,
    PROP_TITLE,
    PROP_ALLOW_RETRY,
    PROP_SHOW_MAIN_MENU,
    PROP_SHOW_EXIT,
    N_PROPS
};

static GParamSpec *props[N_PROPS];

enum
{
    SIGNAL_RETRY,
    SIGNAL_MAIN_MENU,
    SIGNAL_EXIT_GAME,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static void update_button_visibility (LrgTemplateErrorState *self);
static void update_button_selection  (LrgTemplateErrorState *self);
static void navigate_up              (LrgTemplateErrorState *self);
static void navigate_down            (LrgTemplateErrorState *self);
static void activate_selected        (LrgTemplateErrorState *self);

/* ==========================================================================
 * Button Callbacks
 * ========================================================================== */

static void
on_retry_clicked (LrgButton                 *button,
                  LrgTemplateErrorState *self)
{
    LrgTemplateErrorStateClass *klass;

    klass = LRG_TEMPLATE_ERROR_STATE_GET_CLASS (self);

    if (klass->on_retry != NULL)
        klass->on_retry (self);
}

static void
on_main_menu_clicked (LrgButton                 *button,
                      LrgTemplateErrorState *self)
{
    LrgTemplateErrorStateClass *klass;

    klass = LRG_TEMPLATE_ERROR_STATE_GET_CLASS (self);

    if (klass->on_main_menu != NULL)
        klass->on_main_menu (self);
}

static void
on_exit_clicked (LrgButton                 *button,
                 LrgTemplateErrorState *self)
{
    LrgTemplateErrorStateClass *klass;

    klass = LRG_TEMPLATE_ERROR_STATE_GET_CLASS (self);

    if (klass->on_exit != NULL)
        klass->on_exit (self);
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_template_error_state_real_on_retry (LrgTemplateErrorState *self)
{
    g_signal_emit (self, signals[SIGNAL_RETRY], 0);
}

static void
lrg_template_error_state_real_on_main_menu (LrgTemplateErrorState *self)
{
    g_signal_emit (self, signals[SIGNAL_MAIN_MENU], 0);
}

static void
lrg_template_error_state_real_on_exit (LrgTemplateErrorState *self)
{
    g_signal_emit (self, signals[SIGNAL_EXIT_GAME], 0);
}

/* ==========================================================================
 * Navigation Helpers
 * ========================================================================== */

static void
count_visible_buttons (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    priv = lrg_template_error_state_get_instance_private (self);

    priv->visible_button_count = 0;

    if (priv->allow_retry)
        priv->visible_button_count++;

    if (priv->show_main_menu)
        priv->visible_button_count++;

    if (priv->show_exit)
        priv->visible_button_count++;
}

static LrgButton *
get_button_at_index (LrgTemplateErrorState *self,
                     gint                       index)
{
    LrgTemplateErrorStatePrivate *priv;
    gint current;

    priv = lrg_template_error_state_get_instance_private (self);

    current = 0;

    if (priv->allow_retry)
    {
        if (current == index)
            return priv->retry_button;
        current++;
    }

    if (priv->show_main_menu)
    {
        if (current == index)
            return priv->main_menu_button;
        current++;
    }

    if (priv->show_exit)
    {
        if (current == index)
            return priv->exit_button;
        current++;
    }

    return NULL;
}

static void
update_button_selection (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;
    LrgButton *button;
    gint i;

    priv = lrg_template_error_state_get_instance_private (self);

    /* Update all buttons to show selection state using colors */
    for (i = 0; i < priv->visible_button_count; i++)
    {
        button = get_button_at_index (self, i);
        if (button != NULL)
        {
            if (i == priv->selected_index)
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
navigate_up (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    priv = lrg_template_error_state_get_instance_private (self);

    if (priv->visible_button_count == 0)
        return;

    priv->selected_index--;
    if (priv->selected_index < 0)
        priv->selected_index = priv->visible_button_count - 1;

    update_button_selection (self);
}

static void
navigate_down (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    priv = lrg_template_error_state_get_instance_private (self);

    if (priv->visible_button_count == 0)
        return;

    priv->selected_index++;
    if (priv->selected_index >= priv->visible_button_count)
        priv->selected_index = 0;

    update_button_selection (self);
}

static void
activate_selected (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;
    LrgButton *button;

    priv = lrg_template_error_state_get_instance_private (self);

    button = get_button_at_index (self, priv->selected_index);
    if (button != NULL)
    {
        g_signal_emit_by_name (button, "clicked");
    }
}

static void
update_button_visibility (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    priv = lrg_template_error_state_get_instance_private (self);

    if (priv->retry_button != NULL)
        lrg_widget_set_visible (LRG_WIDGET (priv->retry_button), priv->allow_retry);

    if (priv->main_menu_button != NULL)
        lrg_widget_set_visible (LRG_WIDGET (priv->main_menu_button), priv->show_main_menu);

    if (priv->exit_button != NULL)
        lrg_widget_set_visible (LRG_WIDGET (priv->exit_button), priv->show_exit);

    count_visible_buttons (self);

    /* Clamp selection index */
    if (priv->selected_index >= priv->visible_button_count)
        priv->selected_index = priv->visible_button_count > 0 ? priv->visible_button_count - 1 : 0;

    update_button_selection (self);
}

/* ==========================================================================
 * UI Creation
 * ========================================================================== */

static void
create_ui (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;

    priv = lrg_template_error_state_get_instance_private (self);

    engine = lrg_engine_get_default ();
    window = lrg_engine_get_window (engine);
    screen_width = lrg_window_get_width (window);
    screen_height = lrg_window_get_height (window);

    /* Create canvas */
    priv->canvas = lrg_canvas_new ();
    lrg_widget_set_size (LRG_WIDGET (priv->canvas),
                         (gfloat) screen_width,
                         (gfloat) screen_height);

    /* Create content container */
    priv->content_box = lrg_vbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (priv->content_box), 20.0f);
    lrg_widget_set_position (LRG_WIDGET (priv->content_box),
                             (gfloat) screen_width / 2.0f - 200.0f,
                             (gfloat) screen_height / 3.0f);
    lrg_widget_set_size (LRG_WIDGET (priv->content_box), 400.0f, 300.0f);
    lrg_container_add_child (LRG_CONTAINER (priv->canvas), LRG_WIDGET (priv->content_box));

    /* Create title label */
    priv->title_label = lrg_label_new (priv->title != NULL ? priv->title : "Error");
    lrg_label_set_font_size (priv->title_label, 48.0f);
    lrg_label_set_color (priv->title_label, priv->error_color);
    lrg_label_set_alignment (priv->title_label, LRG_TEXT_ALIGN_CENTER);
    lrg_container_add_child (LRG_CONTAINER (priv->content_box),
                             LRG_WIDGET (priv->title_label));

    /* Create error message label */
    priv->message_label = lrg_label_new (priv->error_message != NULL ?
                                         priv->error_message : "An error occurred");
    lrg_label_set_font_size (priv->message_label, 20.0f);
    lrg_label_set_alignment (priv->message_label, LRG_TEXT_ALIGN_CENTER);
    /* Word wrap would be enabled here if supported */
    lrg_widget_set_size (LRG_WIDGET (priv->message_label), 400.0f, 100.0f);
    lrg_container_add_child (LRG_CONTAINER (priv->content_box),
                             LRG_WIDGET (priv->message_label));

    /* Create retry button */
    priv->retry_button = lrg_button_new ("Retry");
    lrg_widget_set_size (LRG_WIDGET (priv->retry_button), 200.0f, 50.0f);
    g_signal_connect (priv->retry_button, "clicked",
                      G_CALLBACK (on_retry_clicked), self);
    lrg_container_add_child (LRG_CONTAINER (priv->content_box),
                             LRG_WIDGET (priv->retry_button));

    /* Create main menu button */
    priv->main_menu_button = lrg_button_new ("Main Menu");
    lrg_widget_set_size (LRG_WIDGET (priv->main_menu_button), 200.0f, 50.0f);
    g_signal_connect (priv->main_menu_button, "clicked",
                      G_CALLBACK (on_main_menu_clicked), self);
    lrg_container_add_child (LRG_CONTAINER (priv->content_box),
                             LRG_WIDGET (priv->main_menu_button));

    /* Create exit button */
    priv->exit_button = lrg_button_new ("Exit");
    lrg_widget_set_size (LRG_WIDGET (priv->exit_button), 200.0f, 50.0f);
    g_signal_connect (priv->exit_button, "clicked",
                      G_CALLBACK (on_exit_clicked), self);
    lrg_container_add_child (LRG_CONTAINER (priv->content_box),
                             LRG_WIDGET (priv->exit_button));

    /* Apply visibility settings */
    update_button_visibility (self);
}

/* ==========================================================================
 * LrgGameState Virtual Method Overrides
 * ========================================================================== */

static void
lrg_template_error_state_enter (LrgGameState *state)
{
    LrgTemplateErrorState *self;

    self = LRG_TEMPLATE_ERROR_STATE (state);

    create_ui (self);

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_error_state_parent_class)->enter (state);
}

static void
lrg_template_error_state_exit (LrgGameState *state)
{
    LrgTemplateErrorState *self;
    LrgTemplateErrorStatePrivate *priv;

    self = LRG_TEMPLATE_ERROR_STATE (state);
    priv = lrg_template_error_state_get_instance_private (self);

    /* Clean up UI */
    g_clear_object (&priv->canvas);
    priv->content_box = NULL;
    priv->title_label = NULL;
    priv->message_label = NULL;
    priv->retry_button = NULL;
    priv->main_menu_button = NULL;
    priv->exit_button = NULL;

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_error_state_parent_class)->exit (state);
}

static void
lrg_template_error_state_update (LrgGameState *state,
                                 gdouble       delta)
{
    /* Error state doesn't need regular updates */
    LRG_GAME_STATE_CLASS (lrg_template_error_state_parent_class)->update (state, delta);
}

static void
lrg_template_error_state_draw (LrgGameState *state)
{
    LrgTemplateErrorState *self;
    LrgTemplateErrorStatePrivate *priv;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;

    self = LRG_TEMPLATE_ERROR_STATE (state);
    priv = lrg_template_error_state_get_instance_private (self);

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
    LRG_GAME_STATE_CLASS (lrg_template_error_state_parent_class)->draw (state);
}

static gboolean
lrg_template_error_state_handle_input (LrgGameState *state,
                                       gpointer      event)
{
    LrgTemplateErrorState *self;
    LrgTemplateErrorStatePrivate *priv;
    /* Key handling is done inline with grl_input_is_key_pressed */

    self = LRG_TEMPLATE_ERROR_STATE (state);
    priv = lrg_template_error_state_get_instance_private (self);

    /* Handle keyboard navigation */
    if (grl_input_is_key_pressed (GRL_KEY_UP) || grl_input_is_key_pressed (GRL_KEY_W))
    {
        navigate_up (self);
        return TRUE;
    }

    if (grl_input_is_key_pressed (GRL_KEY_DOWN) || grl_input_is_key_pressed (GRL_KEY_S))
    {
        navigate_down (self);
        return TRUE;
    }

    if (grl_input_is_key_pressed (GRL_KEY_ENTER) || grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        activate_selected (self);
        return TRUE;
    }

    /* Handle gamepad navigation */
    if (grl_input_is_gamepad_available (0))
    {
        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_UP))
        {
            navigate_up (self);
            return TRUE;
        }

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN))
        {
            navigate_down (self);
            return TRUE;
        }

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
        {
            activate_selected (self);
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
lrg_template_error_state_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    LrgTemplateErrorState *self;
    LrgTemplateErrorStatePrivate *priv;

    self = LRG_TEMPLATE_ERROR_STATE (object);
    priv = lrg_template_error_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ERROR_MESSAGE:
        g_free (priv->error_message);
        priv->error_message = g_value_dup_string (value);
        if (priv->message_label != NULL)
            lrg_label_set_text (priv->message_label, priv->error_message);
        break;

    case PROP_TITLE:
        g_free (priv->title);
        priv->title = g_value_dup_string (value);
        if (priv->title_label != NULL)
            lrg_label_set_text (priv->title_label, priv->title);
        break;

    case PROP_ALLOW_RETRY:
        priv->allow_retry = g_value_get_boolean (value);
        update_button_visibility (self);
        break;

    case PROP_SHOW_MAIN_MENU:
        priv->show_main_menu = g_value_get_boolean (value);
        update_button_visibility (self);
        break;

    case PROP_SHOW_EXIT:
        priv->show_exit = g_value_get_boolean (value);
        update_button_visibility (self);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_error_state_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    LrgTemplateErrorState *self;
    LrgTemplateErrorStatePrivate *priv;

    self = LRG_TEMPLATE_ERROR_STATE (object);
    priv = lrg_template_error_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ERROR_MESSAGE:
        g_value_set_string (value, priv->error_message);
        break;

    case PROP_TITLE:
        g_value_set_string (value, priv->title);
        break;

    case PROP_ALLOW_RETRY:
        g_value_set_boolean (value, priv->allow_retry);
        break;

    case PROP_SHOW_MAIN_MENU:
        g_value_set_boolean (value, priv->show_main_menu);
        break;

    case PROP_SHOW_EXIT:
        g_value_set_boolean (value, priv->show_exit);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_error_state_finalize (GObject *object)
{
    LrgTemplateErrorState *self;
    LrgTemplateErrorStatePrivate *priv;

    self = LRG_TEMPLATE_ERROR_STATE (object);
    priv = lrg_template_error_state_get_instance_private (self);

    g_free (priv->error_message);
    g_free (priv->title);
    g_clear_pointer (&priv->background_color, grl_color_free);
    g_clear_pointer (&priv->error_color, grl_color_free);
    g_clear_object (&priv->canvas);

    G_OBJECT_CLASS (lrg_template_error_state_parent_class)->finalize (object);
}

static void
lrg_template_error_state_class_init (LrgTemplateErrorStateClass *klass)
{
    GObjectClass *object_class;
    LrgGameStateClass *state_class;

    object_class = G_OBJECT_CLASS (klass);
    state_class = LRG_GAME_STATE_CLASS (klass);

    /* GObject methods */
    object_class->set_property = lrg_template_error_state_set_property;
    object_class->get_property = lrg_template_error_state_get_property;
    object_class->finalize = lrg_template_error_state_finalize;

    /* LrgGameState methods */
    state_class->enter = lrg_template_error_state_enter;
    state_class->exit = lrg_template_error_state_exit;
    state_class->update = lrg_template_error_state_update;
    state_class->draw = lrg_template_error_state_draw;
    state_class->handle_input = lrg_template_error_state_handle_input;

    /* LrgTemplateErrorState methods */
    klass->on_retry = lrg_template_error_state_real_on_retry;
    klass->on_main_menu = lrg_template_error_state_real_on_main_menu;
    klass->on_exit = lrg_template_error_state_real_on_exit;

    /**
     * LrgTemplateErrorState:error-message:
     *
     * The error message to display.
     */
    props[PROP_ERROR_MESSAGE] =
        g_param_spec_string ("error-message",
                             "Error Message",
                             "The error message to display",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateErrorState:title:
     *
     * The title text displayed above the error message.
     */
    props[PROP_TITLE] =
        g_param_spec_string ("title",
                             "Title",
                             "The title text",
                             "Error",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateErrorState:allow-retry:
     *
     * Whether the Retry button is shown.
     */
    props[PROP_ALLOW_RETRY] =
        g_param_spec_boolean ("allow-retry",
                              "Allow Retry",
                              "Whether the Retry button is shown",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateErrorState:show-main-menu:
     *
     * Whether the Main Menu button is shown.
     */
    props[PROP_SHOW_MAIN_MENU] =
        g_param_spec_boolean ("show-main-menu",
                              "Show Main Menu",
                              "Whether the Main Menu button is shown",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateErrorState:show-exit:
     *
     * Whether the Exit button is shown.
     */
    props[PROP_SHOW_EXIT] =
        g_param_spec_boolean ("show-exit",
                              "Show Exit",
                              "Whether the Exit button is shown",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, props);

    /**
     * LrgTemplateErrorState::retry:
     * @self: the error state
     *
     * Emitted when the Retry button is activated.
     */
    signals[SIGNAL_RETRY] =
        g_signal_new ("retry",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateErrorState::main-menu:
     * @self: the error state
     *
     * Emitted when the Main Menu button is activated.
     */
    signals[SIGNAL_MAIN_MENU] =
        g_signal_new ("main-menu",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateErrorState::exit-game:
     * @self: the error state
     *
     * Emitted when the Exit button is activated.
     */
    signals[SIGNAL_EXIT_GAME] =
        g_signal_new ("exit-game",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_template_error_state_init (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    priv = lrg_template_error_state_get_instance_private (self);

    /* Default configuration */
    priv->title = g_strdup ("Error");
    priv->error_message = NULL;
    priv->allow_retry = TRUE;
    priv->show_main_menu = TRUE;
    priv->show_exit = TRUE;
    priv->selected_index = 0;
    priv->visible_button_count = 3;

    /* Default colors */
    priv->background_color = grl_color_new (40, 40, 50, 255);
    priv->error_color = grl_color_new (255, 100, 100, 255);

    /* Set state to be blocking (not transparent) */
    g_object_set (self, "blocking", TRUE, NULL);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_template_error_state_new:
 *
 * Creates a new error state.
 *
 * Returns: (transfer full): A new #LrgTemplateErrorState
 */
LrgTemplateErrorState *
lrg_template_error_state_new (void)
{
    return g_object_new (LRG_TYPE_TEMPLATE_ERROR_STATE, NULL);
}

/**
 * lrg_template_error_state_new_with_error:
 * @error: the error to display
 *
 * Creates a new error state displaying the given error.
 *
 * Returns: (transfer full): A new #LrgTemplateErrorState
 */
LrgTemplateErrorState *
lrg_template_error_state_new_with_error (GError *error)
{
    LrgTemplateErrorState *self;

    self = g_object_new (LRG_TYPE_TEMPLATE_ERROR_STATE, NULL);

    if (error != NULL)
    {
        lrg_template_error_state_set_error (self, error);
    }

    return self;
}

/* ==========================================================================
 * Public API - Error Information
 * ========================================================================== */

/**
 * lrg_template_error_state_get_error_message:
 * @self: an #LrgTemplateErrorState
 *
 * Gets the error message.
 *
 * Returns: (transfer none) (nullable): The error message
 */
const gchar *
lrg_template_error_state_get_error_message (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self), NULL);

    priv = lrg_template_error_state_get_instance_private (self);

    return priv->error_message;
}

/**
 * lrg_template_error_state_set_error_message:
 * @self: an #LrgTemplateErrorState
 * @message: (nullable): the error message
 *
 * Sets the error message to display.
 */
void
lrg_template_error_state_set_error_message (LrgTemplateErrorState *self,
                                            const gchar           *message)
{
    g_return_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self));

    g_object_set (self, "error-message", message, NULL);
}

/**
 * lrg_template_error_state_set_error:
 * @self: an #LrgTemplateErrorState
 * @error: (nullable): the GError to display
 *
 * Sets the error to display from a GError.
 */
void
lrg_template_error_state_set_error (LrgTemplateErrorState *self,
                                    GError                *error)
{
    g_return_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self));

    if (error != NULL)
    {
        lrg_template_error_state_set_error_message (self, error->message);
    }
    else
    {
        lrg_template_error_state_set_error_message (self, NULL);
    }
}

/**
 * lrg_template_error_state_get_title:
 * @self: an #LrgTemplateErrorState
 *
 * Gets the title text.
 *
 * Returns: (transfer none): The title
 */
const gchar *
lrg_template_error_state_get_title (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self), NULL);

    priv = lrg_template_error_state_get_instance_private (self);

    return priv->title;
}

/**
 * lrg_template_error_state_set_title:
 * @self: an #LrgTemplateErrorState
 * @title: the title text
 *
 * Sets the title text.
 */
void
lrg_template_error_state_set_title (LrgTemplateErrorState *self,
                                    const gchar           *title)
{
    g_return_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self));

    g_object_set (self, "title", title, NULL);
}

/* ==========================================================================
 * Public API - Button Visibility
 * ========================================================================== */

/**
 * lrg_template_error_state_get_allow_retry:
 * @self: an #LrgTemplateErrorState
 *
 * Gets whether the Retry button is shown.
 *
 * Returns: %TRUE if shown
 */
gboolean
lrg_template_error_state_get_allow_retry (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self), FALSE);

    priv = lrg_template_error_state_get_instance_private (self);

    return priv->allow_retry;
}

/**
 * lrg_template_error_state_set_allow_retry:
 * @self: an #LrgTemplateErrorState
 * @allow: whether to show Retry button
 *
 * Sets whether the Retry button is shown.
 */
void
lrg_template_error_state_set_allow_retry (LrgTemplateErrorState *self,
                                          gboolean               allow)
{
    g_return_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self));

    g_object_set (self, "allow-retry", allow, NULL);
}

/**
 * lrg_template_error_state_get_show_main_menu:
 * @self: an #LrgTemplateErrorState
 *
 * Gets whether the Main Menu button is shown.
 *
 * Returns: %TRUE if shown
 */
gboolean
lrg_template_error_state_get_show_main_menu (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self), FALSE);

    priv = lrg_template_error_state_get_instance_private (self);

    return priv->show_main_menu;
}

/**
 * lrg_template_error_state_set_show_main_menu:
 * @self: an #LrgTemplateErrorState
 * @show: whether to show Main Menu button
 *
 * Sets whether the Main Menu button is shown.
 */
void
lrg_template_error_state_set_show_main_menu (LrgTemplateErrorState *self,
                                             gboolean               show)
{
    g_return_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self));

    g_object_set (self, "show-main-menu", show, NULL);
}

/**
 * lrg_template_error_state_get_show_exit:
 * @self: an #LrgTemplateErrorState
 *
 * Gets whether the Exit button is shown.
 *
 * Returns: %TRUE if shown
 */
gboolean
lrg_template_error_state_get_show_exit (LrgTemplateErrorState *self)
{
    LrgTemplateErrorStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self), FALSE);

    priv = lrg_template_error_state_get_instance_private (self);

    return priv->show_exit;
}

/**
 * lrg_template_error_state_set_show_exit:
 * @self: an #LrgTemplateErrorState
 * @show: whether to show Exit button
 *
 * Sets whether the Exit button is shown.
 */
void
lrg_template_error_state_set_show_exit (LrgTemplateErrorState *self,
                                        gboolean               show)
{
    g_return_if_fail (LRG_IS_TEMPLATE_ERROR_STATE (self));

    g_object_set (self, "show-exit", show, NULL);
}
