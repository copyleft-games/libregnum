/* lrg-template-confirmation-state.c - Generic confirmation dialog state
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-template-confirmation-state.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../core/lrg-engine.h"
#include "../../graphics/lrg-window.h"
#include "../../lrg-log.h"

/**
 * SECTION:lrg-template-confirmation-state
 * @title: LrgTemplateConfirmationState
 * @short_description: Generic confirmation dialog state
 *
 * #LrgTemplateConfirmationState provides a modal confirmation dialog
 * that can be used for confirming destructive actions like exiting
 * the game, returning to the main menu, or resetting settings.
 *
 * ## Features
 *
 * - Customizable title and message
 * - Customizable button labels
 * - Semi-transparent overlay behind the dialog
 * - Keyboard and gamepad navigation
 * - Configurable default selection (safe option)
 *
 * ## Signals
 *
 * The state emits signals when buttons are activated:
 * - #LrgTemplateConfirmationState::confirmed - Confirm button activated
 * - #LrgTemplateConfirmationState::cancelled - Cancel button activated
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * // Create confirmation dialog
 * LrgGameState *confirm = (LrgGameState *)
 *     lrg_template_confirmation_state_new_with_message (
 *         "Exit Game",
 *         "Are you sure you want to exit?");
 *
 * // Set cancel as default (safer option)
 * lrg_template_confirmation_state_set_default_selection (
 *     LRG_TEMPLATE_CONFIRMATION_STATE (confirm), 1);
 *
 * // Connect to signals
 * g_signal_connect (confirm, "confirmed",
 *                   G_CALLBACK (on_exit_confirmed), NULL);
 * g_signal_connect (confirm, "cancelled",
 *                   G_CALLBACK (on_exit_cancelled), NULL);
 *
 * // Push onto state stack
 * lrg_game_state_manager_push (manager, confirm);
 * ]|
 */

typedef struct _LrgTemplateConfirmationStatePrivate LrgTemplateConfirmationStatePrivate;

struct _LrgTemplateConfirmationStatePrivate
{
    /* UI Elements */
    LrgCanvas *canvas;
    LrgVBox   *dialog_box;
    LrgLabel  *title_label;
    LrgLabel  *message_label;
    LrgHBox   *button_box;
    LrgButton *confirm_button;
    LrgButton *cancel_button;

    /* Text content */
    gchar     *title;
    gchar     *message;
    gchar     *confirm_label;
    gchar     *cancel_label;

    /* Appearance */
    GrlColor  *overlay_color;
    GrlColor  *dialog_color;

    /* Navigation */
    gint       selected_button;
    gint       default_selection;
};

G_DEFINE_TYPE_WITH_PRIVATE (LrgTemplateConfirmationState,
                            lrg_template_confirmation_state,
                            LRG_TYPE_GAME_STATE)

enum
{
    PROP_0,
    PROP_TITLE,
    PROP_MESSAGE,
    PROP_CONFIRM_LABEL,
    PROP_CANCEL_LABEL,
    PROP_DEFAULT_SELECTION,
    N_PROPS
};

static GParamSpec *props[N_PROPS];

enum
{
    SIGNAL_CONFIRMED,
    SIGNAL_CANCELLED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static void update_button_selection (LrgTemplateConfirmationState *self);
static void navigate_left           (LrgTemplateConfirmationState *self);
static void navigate_right          (LrgTemplateConfirmationState *self);
static void activate_selected       (LrgTemplateConfirmationState *self);

/* ==========================================================================
 * Button Callbacks
 * ========================================================================== */

static void
on_confirm_clicked (LrgButton                    *button,
                    LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStateClass *klass;

    klass = LRG_TEMPLATE_CONFIRMATION_STATE_GET_CLASS (self);

    if (klass->on_confirm != NULL)
        klass->on_confirm (self);
}

static void
on_cancel_clicked (LrgButton                    *button,
                   LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStateClass *klass;

    klass = LRG_TEMPLATE_CONFIRMATION_STATE_GET_CLASS (self);

    if (klass->on_cancel != NULL)
        klass->on_cancel (self);
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_template_confirmation_state_real_on_confirm (LrgTemplateConfirmationState *self)
{
    g_signal_emit (self, signals[SIGNAL_CONFIRMED], 0);
}

static void
lrg_template_confirmation_state_real_on_cancel (LrgTemplateConfirmationState *self)
{
    g_signal_emit (self, signals[SIGNAL_CANCELLED], 0);
}

/* ==========================================================================
 * Navigation Helpers
 * ========================================================================== */

static void
update_button_selection (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;
    g_autoptr(GrlColor) selected_color = NULL;
    g_autoptr(GrlColor) normal_color = NULL;

    priv = lrg_template_confirmation_state_get_instance_private (self);

    selected_color = grl_color_new (100, 150, 220, 255);
    normal_color = grl_color_new (80, 80, 90, 255);

    if (priv->confirm_button != NULL)
    {
        if (priv->selected_button == 0)
            lrg_button_set_normal_color (priv->confirm_button, selected_color);
        else
            lrg_button_set_normal_color (priv->confirm_button, normal_color);
    }

    if (priv->cancel_button != NULL)
    {
        if (priv->selected_button == 1)
            lrg_button_set_normal_color (priv->cancel_button, selected_color);
        else
            lrg_button_set_normal_color (priv->cancel_button, normal_color);
    }
}

static void
navigate_left (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    priv = lrg_template_confirmation_state_get_instance_private (self);

    priv->selected_button--;
    if (priv->selected_button < 0)
        priv->selected_button = 1;

    update_button_selection (self);
}

static void
navigate_right (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    priv = lrg_template_confirmation_state_get_instance_private (self);

    priv->selected_button++;
    if (priv->selected_button > 1)
        priv->selected_button = 0;

    update_button_selection (self);
}

static void
activate_selected (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    priv = lrg_template_confirmation_state_get_instance_private (self);

    if (priv->selected_button == 0)
    {
        on_confirm_clicked (NULL, self);
    }
    else
    {
        on_cancel_clicked (NULL, self);
    }
}

/* ==========================================================================
 * UI Creation
 * ========================================================================== */

static void
create_ui (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;
    gfloat dialog_width;
    gfloat dialog_height;
    gfloat dialog_x;
    gfloat dialog_y;

    priv = lrg_template_confirmation_state_get_instance_private (self);

    engine = lrg_engine_get_default ();
    window = lrg_engine_get_window (engine);
    screen_width = lrg_window_get_width (window);
    screen_height = lrg_window_get_height (window);

    dialog_width = 400.0f;
    dialog_height = 200.0f;
    dialog_x = (gfloat) screen_width / 2.0f - dialog_width / 2.0f;
    dialog_y = (gfloat) screen_height / 2.0f - dialog_height / 2.0f;

    /* Create canvas */
    priv->canvas = lrg_canvas_new ();
    lrg_widget_set_size (LRG_WIDGET (priv->canvas),
                         (gfloat) screen_width,
                         (gfloat) screen_height);

    /* Create dialog box */
    priv->dialog_box = lrg_vbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (priv->dialog_box), 20.0f);
    lrg_widget_set_position (LRG_WIDGET (priv->dialog_box), dialog_x, dialog_y);
    lrg_widget_set_size (LRG_WIDGET (priv->dialog_box), dialog_width, dialog_height);
    lrg_container_add_child (LRG_CONTAINER (priv->canvas), LRG_WIDGET (priv->dialog_box));

    /* Create title label */
    priv->title_label = lrg_label_new (priv->title != NULL ? priv->title : "Confirm");
    lrg_label_set_font_size (priv->title_label, 28.0f);
    lrg_label_set_alignment (priv->title_label, LRG_TEXT_ALIGN_CENTER);
    lrg_container_add_child (LRG_CONTAINER (priv->dialog_box),
                             LRG_WIDGET (priv->title_label));

    /* Create message label */
    priv->message_label = lrg_label_new (priv->message != NULL ?
                                         priv->message : "Are you sure?");
    lrg_label_set_font_size (priv->message_label, 18.0f);
    lrg_label_set_alignment (priv->message_label, LRG_TEXT_ALIGN_CENTER);
    /* Word wrap would be enabled here if supported */
    lrg_widget_set_size (LRG_WIDGET (priv->message_label), dialog_width - 40.0f, 60.0f);
    lrg_container_add_child (LRG_CONTAINER (priv->dialog_box),
                             LRG_WIDGET (priv->message_label));

    /* Create button box */
    priv->button_box = lrg_hbox_new ();
    lrg_container_set_spacing (LRG_CONTAINER (priv->button_box), 30.0f);
    lrg_container_add_child (LRG_CONTAINER (priv->dialog_box),
                             LRG_WIDGET (priv->button_box));

    /* Create confirm button */
    priv->confirm_button = lrg_button_new (priv->confirm_label != NULL ?
                                           priv->confirm_label : "Yes");
    lrg_widget_set_size (LRG_WIDGET (priv->confirm_button), 120.0f, 45.0f);
    g_signal_connect (priv->confirm_button, "clicked",
                      G_CALLBACK (on_confirm_clicked), self);
    lrg_container_add_child (LRG_CONTAINER (priv->button_box),
                             LRG_WIDGET (priv->confirm_button));

    /* Create cancel button */
    priv->cancel_button = lrg_button_new (priv->cancel_label != NULL ?
                                          priv->cancel_label : "No");
    lrg_widget_set_size (LRG_WIDGET (priv->cancel_button), 120.0f, 45.0f);
    g_signal_connect (priv->cancel_button, "clicked",
                      G_CALLBACK (on_cancel_clicked), self);
    lrg_container_add_child (LRG_CONTAINER (priv->button_box),
                             LRG_WIDGET (priv->cancel_button));

    /* Set initial selection */
    priv->selected_button = priv->default_selection;
    update_button_selection (self);
}

/* ==========================================================================
 * LrgGameState Virtual Method Overrides
 * ========================================================================== */

static void
lrg_template_confirmation_state_enter (LrgGameState *state)
{
    LrgTemplateConfirmationState *self;

    self = LRG_TEMPLATE_CONFIRMATION_STATE (state);

    create_ui (self);

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_confirmation_state_parent_class)->enter (state);
}

static void
lrg_template_confirmation_state_exit (LrgGameState *state)
{
    LrgTemplateConfirmationState *self;
    LrgTemplateConfirmationStatePrivate *priv;

    self = LRG_TEMPLATE_CONFIRMATION_STATE (state);
    priv = lrg_template_confirmation_state_get_instance_private (self);

    /* Clean up UI */
    g_clear_object (&priv->canvas);
    priv->dialog_box = NULL;
    priv->title_label = NULL;
    priv->message_label = NULL;
    priv->button_box = NULL;
    priv->confirm_button = NULL;
    priv->cancel_button = NULL;

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_confirmation_state_parent_class)->exit (state);
}

static void
lrg_template_confirmation_state_update (LrgGameState *state,
                                        gdouble       delta)
{
    /* Confirmation dialog doesn't need regular updates */
    LRG_GAME_STATE_CLASS (lrg_template_confirmation_state_parent_class)->update (state, delta);
}

static void
lrg_template_confirmation_state_draw (LrgGameState *state)
{
    LrgTemplateConfirmationState *self;
    LrgTemplateConfirmationStatePrivate *priv;
    LrgEngine *engine;
    LrgWindow *window;
    gint screen_width;
    gint screen_height;
    gfloat dialog_width;
    gfloat dialog_height;
    gfloat dialog_x;
    gfloat dialog_y;

    self = LRG_TEMPLATE_CONFIRMATION_STATE (state);
    priv = lrg_template_confirmation_state_get_instance_private (self);

    engine = lrg_engine_get_default ();
    window = lrg_engine_get_window (engine);
    screen_width = lrg_window_get_width (window);
    screen_height = lrg_window_get_height (window);

    dialog_width = 400.0f;
    dialog_height = 200.0f;
    dialog_x = (gfloat) screen_width / 2.0f - dialog_width / 2.0f;
    dialog_y = (gfloat) screen_height / 2.0f - dialog_height / 2.0f;

    /* Draw semi-transparent overlay */
    grl_draw_rectangle (0, 0, screen_width, screen_height, priv->overlay_color);

    /* Draw dialog background */
    grl_draw_rectangle ((gint) dialog_x - 10, (gint) dialog_y - 10,
                        (gint) dialog_width + 20, (gint) dialog_height + 20,
                        priv->dialog_color);

    /* Draw UI */
    if (priv->canvas != NULL)
    {
        lrg_widget_draw (LRG_WIDGET (priv->canvas));
    }

    /* Chain up */
    LRG_GAME_STATE_CLASS (lrg_template_confirmation_state_parent_class)->draw (state);
}

static gboolean
lrg_template_confirmation_state_handle_input (LrgGameState *state,
                                              gpointer      event)
{
    LrgTemplateConfirmationState *self;
    LrgTemplateConfirmationStatePrivate *priv;

    self = LRG_TEMPLATE_CONFIRMATION_STATE (state);
    priv = lrg_template_confirmation_state_get_instance_private (self);

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
        activate_selected (self);
        return TRUE;
    }

    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        on_cancel_clicked (NULL, self);
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
            activate_selected (self);
            return TRUE;
        }

        if (grl_input_is_gamepad_button_pressed (0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
        {
            on_cancel_clicked (NULL, self);
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
lrg_template_confirmation_state_set_property (GObject      *object,
                                              guint         prop_id,
                                              const GValue *value,
                                              GParamSpec   *pspec)
{
    LrgTemplateConfirmationState *self;
    LrgTemplateConfirmationStatePrivate *priv;

    self = LRG_TEMPLATE_CONFIRMATION_STATE (object);
    priv = lrg_template_confirmation_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TITLE:
        g_free (priv->title);
        priv->title = g_value_dup_string (value);
        if (priv->title_label != NULL)
            lrg_label_set_text (priv->title_label, priv->title);
        break;

    case PROP_MESSAGE:
        g_free (priv->message);
        priv->message = g_value_dup_string (value);
        if (priv->message_label != NULL)
            lrg_label_set_text (priv->message_label, priv->message);
        break;

    case PROP_CONFIRM_LABEL:
        g_free (priv->confirm_label);
        priv->confirm_label = g_value_dup_string (value);
        if (priv->confirm_button != NULL)
            lrg_button_set_text (priv->confirm_button, priv->confirm_label);
        break;

    case PROP_CANCEL_LABEL:
        g_free (priv->cancel_label);
        priv->cancel_label = g_value_dup_string (value);
        if (priv->cancel_button != NULL)
            lrg_button_set_text (priv->cancel_button, priv->cancel_label);
        break;

    case PROP_DEFAULT_SELECTION:
        priv->default_selection = g_value_get_int (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_confirmation_state_get_property (GObject    *object,
                                              guint       prop_id,
                                              GValue     *value,
                                              GParamSpec *pspec)
{
    LrgTemplateConfirmationState *self;
    LrgTemplateConfirmationStatePrivate *priv;

    self = LRG_TEMPLATE_CONFIRMATION_STATE (object);
    priv = lrg_template_confirmation_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TITLE:
        g_value_set_string (value, priv->title);
        break;

    case PROP_MESSAGE:
        g_value_set_string (value, priv->message);
        break;

    case PROP_CONFIRM_LABEL:
        g_value_set_string (value, priv->confirm_label);
        break;

    case PROP_CANCEL_LABEL:
        g_value_set_string (value, priv->cancel_label);
        break;

    case PROP_DEFAULT_SELECTION:
        g_value_set_int (value, priv->default_selection);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_confirmation_state_finalize (GObject *object)
{
    LrgTemplateConfirmationState *self;
    LrgTemplateConfirmationStatePrivate *priv;

    self = LRG_TEMPLATE_CONFIRMATION_STATE (object);
    priv = lrg_template_confirmation_state_get_instance_private (self);

    g_free (priv->title);
    g_free (priv->message);
    g_free (priv->confirm_label);
    g_free (priv->cancel_label);
    g_clear_pointer (&priv->overlay_color, grl_color_free);
    g_clear_pointer (&priv->dialog_color, grl_color_free);
    g_clear_object (&priv->canvas);

    G_OBJECT_CLASS (lrg_template_confirmation_state_parent_class)->finalize (object);
}

static void
lrg_template_confirmation_state_class_init (LrgTemplateConfirmationStateClass *klass)
{
    GObjectClass *object_class;
    LrgGameStateClass *state_class;

    object_class = G_OBJECT_CLASS (klass);
    state_class = LRG_GAME_STATE_CLASS (klass);

    /* GObject methods */
    object_class->set_property = lrg_template_confirmation_state_set_property;
    object_class->get_property = lrg_template_confirmation_state_get_property;
    object_class->finalize = lrg_template_confirmation_state_finalize;

    /* LrgGameState methods */
    state_class->enter = lrg_template_confirmation_state_enter;
    state_class->exit = lrg_template_confirmation_state_exit;
    state_class->update = lrg_template_confirmation_state_update;
    state_class->draw = lrg_template_confirmation_state_draw;
    state_class->handle_input = lrg_template_confirmation_state_handle_input;

    /* LrgTemplateConfirmationState methods */
    klass->on_confirm = lrg_template_confirmation_state_real_on_confirm;
    klass->on_cancel = lrg_template_confirmation_state_real_on_cancel;

    /**
     * LrgTemplateConfirmationState:title:
     *
     * The title text displayed in the dialog.
     */
    props[PROP_TITLE] =
        g_param_spec_string ("title",
                             "Title",
                             "The title text",
                             "Confirm",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateConfirmationState:message:
     *
     * The message text displayed in the dialog.
     */
    props[PROP_MESSAGE] =
        g_param_spec_string ("message",
                             "Message",
                             "The message text",
                             "Are you sure?",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateConfirmationState:confirm-label:
     *
     * The label for the confirm button.
     */
    props[PROP_CONFIRM_LABEL] =
        g_param_spec_string ("confirm-label",
                             "Confirm Label",
                             "The confirm button label",
                             "Yes",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateConfirmationState:cancel-label:
     *
     * The label for the cancel button.
     */
    props[PROP_CANCEL_LABEL] =
        g_param_spec_string ("cancel-label",
                             "Cancel Label",
                             "The cancel button label",
                             "No",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateConfirmationState:default-selection:
     *
     * Which button is selected by default (0 = confirm, 1 = cancel).
     */
    props[PROP_DEFAULT_SELECTION] =
        g_param_spec_int ("default-selection",
                          "Default Selection",
                          "Which button is selected by default",
                          0, 1, 1,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, props);

    /**
     * LrgTemplateConfirmationState::confirmed:
     * @self: the confirmation state
     *
     * Emitted when the confirm button is activated.
     */
    signals[SIGNAL_CONFIRMED] =
        g_signal_new ("confirmed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateConfirmationState::cancelled:
     * @self: the confirmation state
     *
     * Emitted when the cancel button is activated.
     */
    signals[SIGNAL_CANCELLED] =
        g_signal_new ("cancelled",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_template_confirmation_state_init (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    priv = lrg_template_confirmation_state_get_instance_private (self);

    /* Default text */
    priv->title = g_strdup ("Confirm");
    priv->message = g_strdup ("Are you sure?");
    priv->confirm_label = g_strdup ("Yes");
    priv->cancel_label = g_strdup ("No");

    /* Default to cancel selected (safer) */
    priv->default_selection = 1;
    priv->selected_button = 1;

    /* Default colors - semi-transparent overlay and dark dialog */
    priv->overlay_color = grl_color_new (0, 0, 0, 180);
    priv->dialog_color = grl_color_new (40, 40, 50, 255);

    /* Set state to be transparent and blocking */
    g_object_set (self,
                  "transparent", TRUE,
                  "blocking", TRUE,
                  NULL);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_new:
 *
 * Creates a new confirmation state.
 *
 * Returns: (transfer full): A new #LrgTemplateConfirmationState
 */
LrgTemplateConfirmationState *
lrg_template_confirmation_state_new (void)
{
    return g_object_new (LRG_TYPE_TEMPLATE_CONFIRMATION_STATE, NULL);
}

/**
 * lrg_template_confirmation_state_new_with_message:
 * @title: the title text
 * @message: the message text
 *
 * Creates a new confirmation state with the given title and message.
 *
 * Returns: (transfer full): A new #LrgTemplateConfirmationState
 */
LrgTemplateConfirmationState *
lrg_template_confirmation_state_new_with_message (const gchar *title,
                                                   const gchar *message)
{
    return g_object_new (LRG_TYPE_TEMPLATE_CONFIRMATION_STATE,
                         "title", title,
                         "message", message,
                         NULL);
}

/* ==========================================================================
 * Public API - Text Content
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_title:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the title text.
 *
 * Returns: (transfer none): The title
 */
const gchar *
lrg_template_confirmation_state_get_title (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self), NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    return priv->title;
}

/**
 * lrg_template_confirmation_state_set_title:
 * @self: an #LrgTemplateConfirmationState
 * @title: the title text
 *
 * Sets the title text.
 */
void
lrg_template_confirmation_state_set_title (LrgTemplateConfirmationState *self,
                                            const gchar                  *title)
{
    g_return_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self));

    g_object_set (self, "title", title, NULL);
}

/**
 * lrg_template_confirmation_state_get_message:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the message text.
 *
 * Returns: (transfer none): The message
 */
const gchar *
lrg_template_confirmation_state_get_message (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self), NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    return priv->message;
}

/**
 * lrg_template_confirmation_state_set_message:
 * @self: an #LrgTemplateConfirmationState
 * @message: the message text
 *
 * Sets the message text.
 */
void
lrg_template_confirmation_state_set_message (LrgTemplateConfirmationState *self,
                                              const gchar                  *message)
{
    g_return_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self));

    g_object_set (self, "message", message, NULL);
}

/* ==========================================================================
 * Public API - Button Labels
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_confirm_label:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the confirm button label.
 *
 * Returns: (transfer none): The label
 */
const gchar *
lrg_template_confirmation_state_get_confirm_label (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self), NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    return priv->confirm_label;
}

/**
 * lrg_template_confirmation_state_set_confirm_label:
 * @self: an #LrgTemplateConfirmationState
 * @label: the button label
 *
 * Sets the confirm button label.
 */
void
lrg_template_confirmation_state_set_confirm_label (LrgTemplateConfirmationState *self,
                                                    const gchar                  *label)
{
    g_return_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self));

    g_object_set (self, "confirm-label", label, NULL);
}

/**
 * lrg_template_confirmation_state_get_cancel_label:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the cancel button label.
 *
 * Returns: (transfer none): The label
 */
const gchar *
lrg_template_confirmation_state_get_cancel_label (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self), NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    return priv->cancel_label;
}

/**
 * lrg_template_confirmation_state_set_cancel_label:
 * @self: an #LrgTemplateConfirmationState
 * @label: the button label
 *
 * Sets the cancel button label.
 */
void
lrg_template_confirmation_state_set_cancel_label (LrgTemplateConfirmationState *self,
                                                   const gchar                  *label)
{
    g_return_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self));

    g_object_set (self, "cancel-label", label, NULL);
}

/* ==========================================================================
 * Public API - Appearance
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_overlay_color:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the overlay color drawn behind the dialog.
 *
 * Returns: (transfer none): The overlay color
 */
const GrlColor *
lrg_template_confirmation_state_get_overlay_color (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self), NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    return priv->overlay_color;
}

/**
 * lrg_template_confirmation_state_set_overlay_color:
 * @self: an #LrgTemplateConfirmationState
 * @color: the overlay color
 *
 * Sets the overlay color. The alpha channel controls transparency.
 */
void
lrg_template_confirmation_state_set_overlay_color (LrgTemplateConfirmationState *self,
                                                    const GrlColor               *color)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self));
    g_return_if_fail (color != NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    g_clear_pointer (&priv->overlay_color, grl_color_free);
    priv->overlay_color = grl_color_copy (color);
}

/**
 * lrg_template_confirmation_state_get_dialog_color:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets the dialog background color.
 *
 * Returns: (transfer none): The dialog color
 */
const GrlColor *
lrg_template_confirmation_state_get_dialog_color (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self), NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    return priv->dialog_color;
}

/**
 * lrg_template_confirmation_state_set_dialog_color:
 * @self: an #LrgTemplateConfirmationState
 * @color: the dialog background color
 *
 * Sets the dialog background color.
 */
void
lrg_template_confirmation_state_set_dialog_color (LrgTemplateConfirmationState *self,
                                                   const GrlColor               *color)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self));
    g_return_if_fail (color != NULL);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    g_clear_pointer (&priv->dialog_color, grl_color_free);
    priv->dialog_color = grl_color_copy (color);
}

/* ==========================================================================
 * Public API - Configuration
 * ========================================================================== */

/**
 * lrg_template_confirmation_state_get_default_selection:
 * @self: an #LrgTemplateConfirmationState
 *
 * Gets which button is selected by default (0 = confirm, 1 = cancel).
 *
 * Returns: The default selection index
 */
gint
lrg_template_confirmation_state_get_default_selection (LrgTemplateConfirmationState *self)
{
    LrgTemplateConfirmationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self), 1);

    priv = lrg_template_confirmation_state_get_instance_private (self);

    return priv->default_selection;
}

/**
 * lrg_template_confirmation_state_set_default_selection:
 * @self: an #LrgTemplateConfirmationState
 * @selection: the default selection (0 = confirm, 1 = cancel)
 *
 * Sets which button is selected by default. Setting to 1 (cancel)
 * is recommended for destructive actions.
 */
void
lrg_template_confirmation_state_set_default_selection (LrgTemplateConfirmationState *self,
                                                        gint                          selection)
{
    g_return_if_fail (LRG_IS_TEMPLATE_CONFIRMATION_STATE (self));
    g_return_if_fail (selection >= 0 && selection <= 1);

    g_object_set (self, "default-selection", selection, NULL);
}
