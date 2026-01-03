/* lrg-template-loading-state.c - Loading screen state implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../../config.h"
#include "lrg-template-loading-state.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-progress-bar.h"
#include "../../ui/lrg-widget.h"
#include "../../ui/lrg-container.h"
#include "../../core/lrg-engine.h"
#include "../../graphics/lrg-window.h"
#include "../../lrg-log.h"

/**
 * SECTION:lrg-template-loading-state
 * @title: LrgTemplateLoadingState
 * @short_description: Loading screen with progress bar
 *
 * #LrgTemplateLoadingState provides a loading screen that:
 * - Displays a progress bar
 * - Shows current task name
 * - Executes one loading task per frame (non-blocking)
 * - Supports minimum display time
 * - Emits signals on completion/failure
 *
 * ## Task Execution
 *
 * Loading tasks are executed one per frame to avoid blocking the UI.
 * This keeps the progress bar responsive.
 *
 * ## Minimum Display Time
 *
 * If assets load very quickly, the loading screen can flash by too fast.
 * Set a minimum display time to ensure users see it:
 *
 * ```c
 * lrg_template_loading_state_set_minimum_display_time (loading, 1.0);
 * ```
 */

typedef struct
{
    gchar           *name;
    LrgLoadingTask   task;
    gpointer         user_data;
    GDestroyNotify   destroy;
    gboolean         completed;
} LoadingTaskEntry;

typedef struct _LrgTemplateLoadingStatePrivate
{
    /* Tasks */
    GPtrArray       *tasks;
    guint            current_task_index;
    guint            completed_count;

    /* Timing */
    gdouble          minimum_display_time;
    gdouble          elapsed_time;
    gboolean         loading_complete;
    gboolean         minimum_time_reached;

    /* Appearance */
    GrlColor        *background_color;
    gchar           *status_text;
    gboolean         show_progress_bar;
    gboolean         show_percentage;

    /* UI */
    LrgCanvas       *canvas;
    LrgVBox         *container;
    LrgLabel        *status_label;
    LrgLabel        *task_label;
    LrgProgressBar  *progress_bar;
    LrgLabel        *percent_label;
} LrgTemplateLoadingStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTemplateLoadingState, lrg_template_loading_state, LRG_TYPE_GAME_STATE)

enum
{
    PROP_0,
    PROP_MINIMUM_DISPLAY_TIME,
    PROP_BACKGROUND_COLOR,
    PROP_STATUS_TEXT,
    PROP_SHOW_PROGRESS_BAR,
    PROP_SHOW_PERCENTAGE,
    PROP_PROGRESS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_PROGRESS,
    SIGNAL_COMPLETE,
    SIGNAL_FAILED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default values */
#define DEFAULT_MINIMUM_DISPLAY_TIME (0.5)
#define DEFAULT_PROGRESS_BAR_WIDTH   (400.0f)
#define DEFAULT_PROGRESS_BAR_HEIGHT  (30.0f)

/* ========================================================================== */
/* Private Helpers                                                            */
/* ========================================================================== */

static LoadingTaskEntry *
loading_task_entry_new (const gchar    *name,
                        LrgLoadingTask  task,
                        gpointer        user_data,
                        GDestroyNotify  destroy)
{
    LoadingTaskEntry *entry;

    entry = g_new0 (LoadingTaskEntry, 1);
    entry->name = g_strdup (name);
    entry->task = task;
    entry->user_data = user_data;
    entry->destroy = destroy;
    entry->completed = FALSE;

    return entry;
}

static void
loading_task_entry_free (LoadingTaskEntry *entry)
{
    if (entry == NULL)
        return;

    g_free (entry->name);
    if (entry->destroy != NULL && entry->user_data != NULL)
        entry->destroy (entry->user_data);
    g_free (entry);
}

static void
update_ui (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;
    gdouble progress;
    g_autofree gchar *percent_text = NULL;

    priv = lrg_template_loading_state_get_instance_private (self);

    if (priv->canvas == NULL)
        return;

    progress = lrg_template_loading_state_get_progress (self);

    /* Update progress bar */
    if (priv->progress_bar != NULL)
        lrg_progress_bar_set_value (priv->progress_bar, progress);

    /* Update percentage label */
    if (priv->percent_label != NULL)
    {
        percent_text = g_strdup_printf ("%.0f%%", progress * 100.0);
        lrg_label_set_text (priv->percent_label, percent_text);
    }

    /* Update task label */
    if (priv->task_label != NULL)
    {
        const gchar *task_name = lrg_template_loading_state_get_current_task_name (self);
        if (task_name != NULL)
            lrg_label_set_text (priv->task_label, task_name);
        else
            lrg_label_set_text (priv->task_label, "");
    }
}

static gboolean
execute_next_task (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;
    LrgTemplateLoadingStateClass *klass;
    LoadingTaskEntry *entry;
    g_autoptr(GError) error = NULL;
    gboolean success;

    priv = lrg_template_loading_state_get_instance_private (self);
    klass = LRG_TEMPLATE_LOADING_STATE_GET_CLASS (self);

    if (priv->current_task_index >= priv->tasks->len)
    {
        /* All tasks complete */
        priv->loading_complete = TRUE;
        return TRUE;
    }

    entry = g_ptr_array_index (priv->tasks, priv->current_task_index);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Loading: executing task '%s' (%u/%u)",
                   entry->name, priv->current_task_index + 1, priv->tasks->len);

    /* Execute the task */
    if (entry->task != NULL)
    {
        success = entry->task (entry->user_data, &error);
    }
    else
    {
        success = TRUE;  /* NULL task = no-op */
    }

    if (!success)
    {
        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE, "Loading task '%s' failed: %s",
                         entry->name, error ? error->message : "Unknown error");

        if (klass->on_failed != NULL)
            klass->on_failed (self, error);

        return FALSE;
    }

    entry->completed = TRUE;
    priv->completed_count++;
    priv->current_task_index++;

    /* Emit progress signal */
    g_signal_emit (self, signals[SIGNAL_PROGRESS], 0,
                   lrg_template_loading_state_get_progress (self));

    update_ui (self);

    if (priv->current_task_index >= priv->tasks->len)
    {
        priv->loading_complete = TRUE;
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Loading: all tasks complete");
    }

    return TRUE;
}

/* ========================================================================== */
/* Virtual Method Implementations                                             */
/* ========================================================================== */

static void
lrg_template_loading_state_real_on_complete (LrgTemplateLoadingState *self)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Loading complete");
    g_signal_emit (self, signals[SIGNAL_COMPLETE], 0);
}

static void
lrg_template_loading_state_real_on_failed (LrgTemplateLoadingState *self,
                                           GError                  *error)
{
    lrg_warning (LRG_LOG_DOMAIN_TEMPLATE, "Loading failed: %s",
                     error ? error->message : "Unknown error");
    g_signal_emit (self, signals[SIGNAL_FAILED], 0, error);
}

/* ========================================================================== */
/* LrgGameState Interface Implementation                                      */
/* ========================================================================== */

static void
lrg_template_loading_state_enter (LrgGameState *state)
{
    LrgTemplateLoadingState *self = LRG_TEMPLATE_LOADING_STATE (state);
    LrgTemplateLoadingStatePrivate *priv;
    g_autoptr(GrlColor) text_color = NULL;
    g_autoptr(GrlColor) bar_bg_color = NULL;
    g_autoptr(GrlColor) bar_fill_color = NULL;

    priv = lrg_template_loading_state_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Loading state entering with %u tasks",
                   priv->tasks->len);

    /* Reset state */
    priv->current_task_index = 0;
    priv->completed_count = 0;
    priv->elapsed_time = 0.0;
    priv->loading_complete = FALSE;
    priv->minimum_time_reached = FALSE;

    /* Create UI */
    priv->canvas = lrg_canvas_new ();
    priv->container = lrg_vbox_new ();

    text_color = grl_color_new (255, 255, 255, 255);

    /* Status label */
    priv->status_label = lrg_label_new (priv->status_text ? priv->status_text : "Loading...");
    lrg_label_set_font_size (priv->status_label, 32.0f);
    lrg_label_set_color (priv->status_label, text_color);
    lrg_label_set_alignment (priv->status_label, LRG_TEXT_ALIGN_CENTER);
    lrg_widget_set_width (LRG_WIDGET (priv->status_label), DEFAULT_PROGRESS_BAR_WIDTH);

    /* Task name label */
    priv->task_label = lrg_label_new ("");
    lrg_label_set_font_size (priv->task_label, 18.0f);
    lrg_label_set_color (priv->task_label, text_color);
    lrg_label_set_alignment (priv->task_label, LRG_TEXT_ALIGN_CENTER);
    lrg_widget_set_width (LRG_WIDGET (priv->task_label), DEFAULT_PROGRESS_BAR_WIDTH);

    /* Progress bar */
    priv->progress_bar = lrg_progress_bar_new ();
    lrg_widget_set_width (LRG_WIDGET (priv->progress_bar), DEFAULT_PROGRESS_BAR_WIDTH);
    lrg_widget_set_height (LRG_WIDGET (priv->progress_bar), DEFAULT_PROGRESS_BAR_HEIGHT);
    lrg_progress_bar_set_max (priv->progress_bar, 1.0);
    lrg_progress_bar_set_value (priv->progress_bar, 0.0);
    lrg_progress_bar_set_show_text (priv->progress_bar, FALSE);

    bar_bg_color = grl_color_new (60, 60, 60, 255);
    bar_fill_color = grl_color_new (100, 180, 255, 255);
    lrg_progress_bar_set_background_color (priv->progress_bar, bar_bg_color);
    lrg_progress_bar_set_fill_color (priv->progress_bar, bar_fill_color);

    /* Percentage label */
    priv->percent_label = lrg_label_new ("0%");
    lrg_label_set_font_size (priv->percent_label, 20.0f);
    lrg_label_set_color (priv->percent_label, text_color);
    lrg_label_set_alignment (priv->percent_label, LRG_TEXT_ALIGN_CENTER);
    lrg_widget_set_width (LRG_WIDGET (priv->percent_label), DEFAULT_PROGRESS_BAR_WIDTH);

    /* Build layout */
    lrg_container_add_child (LRG_CONTAINER (priv->container),
                             LRG_WIDGET (priv->status_label));

    /* Spacer */
    {
        LrgWidget *spacer = g_object_new (LRG_TYPE_WIDGET, NULL);
        lrg_widget_set_height (spacer, 20.0f);
        lrg_container_add_child (LRG_CONTAINER (priv->container), spacer);
    }

    if (priv->show_progress_bar)
    {
        lrg_container_add_child (LRG_CONTAINER (priv->container),
                                 LRG_WIDGET (priv->progress_bar));
    }

    /* Spacer */
    {
        LrgWidget *spacer = g_object_new (LRG_TYPE_WIDGET, NULL);
        lrg_widget_set_height (spacer, 10.0f);
        lrg_container_add_child (LRG_CONTAINER (priv->container), spacer);
    }

    if (priv->show_percentage)
    {
        lrg_container_add_child (LRG_CONTAINER (priv->container),
                                 LRG_WIDGET (priv->percent_label));
    }

    /* Spacer */
    {
        LrgWidget *spacer = g_object_new (LRG_TYPE_WIDGET, NULL);
        lrg_widget_set_height (spacer, 10.0f);
        lrg_container_add_child (LRG_CONTAINER (priv->container), spacer);
    }

    lrg_container_add_child (LRG_CONTAINER (priv->container),
                             LRG_WIDGET (priv->task_label));

    lrg_container_add_child (LRG_CONTAINER (priv->canvas),
                             LRG_WIDGET (priv->container));

    /* Center container */
    {
        LrgEngine *engine = lrg_engine_get_default ();
        LrgWindow *window = lrg_engine_get_window (engine);
        gint screen_width = lrg_window_get_width (window);
        gint screen_height = lrg_window_get_height (window);
        gfloat x;
        gfloat y;

        x = (screen_width - DEFAULT_PROGRESS_BAR_WIDTH) / 2.0f;
        y = screen_height * 0.4f;

        lrg_widget_set_x (LRG_WIDGET (priv->container), x);
        lrg_widget_set_y (LRG_WIDGET (priv->container), y);
    }

    update_ui (self);
}

static void
lrg_template_loading_state_exit (LrgGameState *state)
{
    LrgTemplateLoadingState *self = LRG_TEMPLATE_LOADING_STATE (state);
    LrgTemplateLoadingStatePrivate *priv;

    priv = lrg_template_loading_state_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Loading state exiting");

    g_clear_object (&priv->status_label);
    g_clear_object (&priv->task_label);
    g_clear_object (&priv->progress_bar);
    g_clear_object (&priv->percent_label);
    g_clear_object (&priv->container);
    g_clear_object (&priv->canvas);
}

static void
lrg_template_loading_state_update (LrgGameState *state,
                                   gdouble       delta)
{
    LrgTemplateLoadingState *self = LRG_TEMPLATE_LOADING_STATE (state);
    LrgTemplateLoadingStatePrivate *priv;
    LrgTemplateLoadingStateClass *klass;

    priv = lrg_template_loading_state_get_instance_private (self);
    klass = LRG_TEMPLATE_LOADING_STATE_GET_CLASS (self);

    priv->elapsed_time += delta;

    /* Check minimum display time */
    if (priv->elapsed_time >= priv->minimum_display_time)
        priv->minimum_time_reached = TRUE;

    /* Execute one task per frame */
    if (!priv->loading_complete)
    {
        execute_next_task (self);
    }

    /* Check if we can signal completion */
    if (priv->loading_complete && priv->minimum_time_reached)
    {
        if (klass->on_complete != NULL)
            klass->on_complete (self);
    }

    /* Update canvas input (not really needed for loading, but for consistency) */
    if (priv->canvas != NULL)
        lrg_canvas_handle_input (priv->canvas);
}

static void
lrg_template_loading_state_draw (LrgGameState *state)
{
    LrgTemplateLoadingState *self = LRG_TEMPLATE_LOADING_STATE (state);
    LrgTemplateLoadingStatePrivate *priv;

    priv = lrg_template_loading_state_get_instance_private (self);

    /* Draw background */
    if (priv->background_color != NULL)
    {
        grl_draw_clear_background (priv->background_color);
    }
    else
    {
        g_autoptr(GrlColor) default_bg = grl_color_new (20, 20, 30, 255);
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
lrg_template_loading_state_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
    LrgTemplateLoadingState *self = LRG_TEMPLATE_LOADING_STATE (object);

    switch (prop_id)
    {
    case PROP_MINIMUM_DISPLAY_TIME:
        lrg_template_loading_state_set_minimum_display_time (self, g_value_get_double (value));
        break;
    case PROP_BACKGROUND_COLOR:
        lrg_template_loading_state_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_STATUS_TEXT:
        lrg_template_loading_state_set_status_text (self, g_value_get_string (value));
        break;
    case PROP_SHOW_PROGRESS_BAR:
        lrg_template_loading_state_set_show_progress_bar (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_PERCENTAGE:
        lrg_template_loading_state_set_show_percentage (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_loading_state_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
    LrgTemplateLoadingState *self = LRG_TEMPLATE_LOADING_STATE (object);
    LrgTemplateLoadingStatePrivate *priv;

    priv = lrg_template_loading_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_MINIMUM_DISPLAY_TIME:
        g_value_set_double (value, priv->minimum_display_time);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, priv->background_color);
        break;
    case PROP_STATUS_TEXT:
        g_value_set_string (value, priv->status_text);
        break;
    case PROP_SHOW_PROGRESS_BAR:
        g_value_set_boolean (value, priv->show_progress_bar);
        break;
    case PROP_SHOW_PERCENTAGE:
        g_value_set_boolean (value, priv->show_percentage);
        break;
    case PROP_PROGRESS:
        g_value_set_double (value, lrg_template_loading_state_get_progress (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_loading_state_finalize (GObject *object)
{
    LrgTemplateLoadingState *self = LRG_TEMPLATE_LOADING_STATE (object);
    LrgTemplateLoadingStatePrivate *priv;

    priv = lrg_template_loading_state_get_instance_private (self);

    g_clear_pointer (&priv->tasks, g_ptr_array_unref);
    g_clear_pointer (&priv->background_color, grl_color_free);
    g_clear_pointer (&priv->status_text, g_free);
    g_clear_object (&priv->canvas);

    G_OBJECT_CLASS (lrg_template_loading_state_parent_class)->finalize (object);
}

static void
lrg_template_loading_state_class_init (LrgTemplateLoadingStateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    object_class->set_property = lrg_template_loading_state_set_property;
    object_class->get_property = lrg_template_loading_state_get_property;
    object_class->finalize = lrg_template_loading_state_finalize;

    state_class->enter = lrg_template_loading_state_enter;
    state_class->exit = lrg_template_loading_state_exit;
    state_class->update = lrg_template_loading_state_update;
    state_class->draw = lrg_template_loading_state_draw;

    klass->on_complete = lrg_template_loading_state_real_on_complete;
    klass->on_failed = lrg_template_loading_state_real_on_failed;

    /**
     * LrgTemplateLoadingState:minimum-display-time:
     *
     * Minimum time the loading screen is displayed, even if loading
     * completes faster.
     *
     * Since: 1.0
     */
    properties[PROP_MINIMUM_DISPLAY_TIME] =
        g_param_spec_double ("minimum-display-time",
                             "Minimum Display Time",
                             "Minimum display time in seconds",
                             0.0, 60.0, DEFAULT_MINIMUM_DISPLAY_TIME,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateLoadingState:background-color:
     *
     * The background color.
     *
     * Since: 1.0
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "Background color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateLoadingState:status-text:
     *
     * The status text displayed above the progress bar.
     *
     * Since: 1.0
     */
    properties[PROP_STATUS_TEXT] =
        g_param_spec_string ("status-text",
                             "Status Text",
                             "Status text above progress bar",
                             "Loading...",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateLoadingState:show-progress-bar:
     *
     * Whether the progress bar is shown.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_PROGRESS_BAR] =
        g_param_spec_boolean ("show-progress-bar",
                              "Show Progress Bar",
                              "Whether to show progress bar",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateLoadingState:show-percentage:
     *
     * Whether the percentage text is shown.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_PERCENTAGE] =
        g_param_spec_boolean ("show-percentage",
                              "Show Percentage",
                              "Whether to show percentage",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTemplateLoadingState:progress:
     *
     * The current loading progress (0.0 to 1.0). Read-only.
     *
     * Since: 1.0
     */
    properties[PROP_PROGRESS] =
        g_param_spec_double ("progress",
                             "Progress",
                             "Current loading progress",
                             0.0, 1.0, 0.0,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTemplateLoadingState::progress:
     * @self: the #LrgTemplateLoadingState
     * @fraction: the progress fraction (0.0 to 1.0)
     *
     * Emitted when loading progress changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PROGRESS] =
        g_signal_new ("progress",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_DOUBLE);

    /**
     * LrgTemplateLoadingState::complete:
     * @self: the #LrgTemplateLoadingState
     *
     * Emitted when all loading tasks complete successfully.
     *
     * Since: 1.0
     */
    signals[SIGNAL_COMPLETE] =
        g_signal_new ("complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTemplateLoadingState::failed:
     * @self: the #LrgTemplateLoadingState
     * @error: the error that occurred
     *
     * Emitted when a loading task fails.
     *
     * Since: 1.0
     */
    signals[SIGNAL_FAILED] =
        g_signal_new ("failed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_ERROR);
}

static void
lrg_template_loading_state_init (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    priv = lrg_template_loading_state_get_instance_private (self);

    priv->tasks = g_ptr_array_new_with_free_func ((GDestroyNotify)loading_task_entry_free);
    priv->current_task_index = 0;
    priv->completed_count = 0;

    priv->minimum_display_time = DEFAULT_MINIMUM_DISPLAY_TIME;
    priv->elapsed_time = 0.0;
    priv->loading_complete = FALSE;
    priv->minimum_time_reached = FALSE;

    priv->background_color = NULL;
    priv->status_text = g_strdup ("Loading...");
    priv->show_progress_bar = TRUE;
    priv->show_percentage = TRUE;

    lrg_game_state_set_name (LRG_GAME_STATE (self), "Loading");
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * lrg_template_loading_state_new:
 *
 * Creates a new loading state.
 *
 * Returns: (transfer full): A new #LrgTemplateLoadingState
 *
 * Since: 1.0
 */
LrgTemplateLoadingState *
lrg_template_loading_state_new (void)
{
    return g_object_new (LRG_TYPE_TEMPLATE_LOADING_STATE, NULL);
}

void
lrg_template_loading_state_add_task (LrgTemplateLoadingState *self,
                                     const gchar             *name,
                                     LrgLoadingTask           task,
                                     gpointer                 user_data,
                                     GDestroyNotify           destroy)
{
    LrgTemplateLoadingStatePrivate *priv;
    LoadingTaskEntry *entry;

    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));
    g_return_if_fail (name != NULL);

    priv = lrg_template_loading_state_get_instance_private (self);

    entry = loading_task_entry_new (name, task, user_data, destroy);
    g_ptr_array_add (priv->tasks, entry);

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Added loading task: %s", name);
}

void
lrg_template_loading_state_add_asset (LrgTemplateLoadingState *self,
                                      const gchar             *asset_path)
{
    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));
    g_return_if_fail (asset_path != NULL);

    /* For now, just add a placeholder task */
    /* TODO: Integrate with asset manager */
    lrg_template_loading_state_add_task (self,
                                         asset_path,
                                         NULL,  /* NULL task = immediate success */
                                         NULL,
                                         NULL);
}

void
lrg_template_loading_state_clear_tasks (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));

    priv = lrg_template_loading_state_get_instance_private (self);

    g_ptr_array_set_size (priv->tasks, 0);
    priv->current_task_index = 0;
    priv->completed_count = 0;
}

guint
lrg_template_loading_state_get_task_count (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), 0);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->tasks->len;
}

guint
lrg_template_loading_state_get_completed_count (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), 0);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->completed_count;
}

gdouble
lrg_template_loading_state_get_progress (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), 0.0);

    priv = lrg_template_loading_state_get_instance_private (self);

    if (priv->tasks->len == 0)
        return 1.0;

    return (gdouble)priv->completed_count / (gdouble)priv->tasks->len;
}

const gchar *
lrg_template_loading_state_get_current_task_name (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;
    LoadingTaskEntry *entry;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), NULL);

    priv = lrg_template_loading_state_get_instance_private (self);

    if (priv->current_task_index >= priv->tasks->len)
        return NULL;

    entry = g_ptr_array_index (priv->tasks, priv->current_task_index);
    return entry->name;
}

gboolean
lrg_template_loading_state_is_complete (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), FALSE);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->loading_complete && priv->minimum_time_reached;
}

gdouble
lrg_template_loading_state_get_minimum_display_time (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), DEFAULT_MINIMUM_DISPLAY_TIME);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->minimum_display_time;
}

void
lrg_template_loading_state_set_minimum_display_time (LrgTemplateLoadingState *self,
                                                     gdouble                  time)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));

    priv = lrg_template_loading_state_get_instance_private (self);

    if (priv->minimum_display_time != time)
    {
        priv->minimum_display_time = time;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MINIMUM_DISPLAY_TIME]);
    }
}

const GrlColor *
lrg_template_loading_state_get_background_color (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), NULL);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->background_color;
}

void
lrg_template_loading_state_set_background_color (LrgTemplateLoadingState *self,
                                                 const GrlColor          *color)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));

    priv = lrg_template_loading_state_get_instance_private (self);

    g_clear_pointer (&priv->background_color, grl_color_free);
    if (color != NULL)
        priv->background_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

const gchar *
lrg_template_loading_state_get_status_text (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), NULL);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->status_text;
}

void
lrg_template_loading_state_set_status_text (LrgTemplateLoadingState *self,
                                            const gchar             *text)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));

    priv = lrg_template_loading_state_get_instance_private (self);

    if (g_strcmp0 (priv->status_text, text) != 0)
    {
        g_free (priv->status_text);
        priv->status_text = g_strdup (text);

        if (priv->status_label != NULL)
            lrg_label_set_text (priv->status_label, text);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS_TEXT]);
    }
}

gboolean
lrg_template_loading_state_get_show_progress_bar (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), TRUE);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->show_progress_bar;
}

void
lrg_template_loading_state_set_show_progress_bar (LrgTemplateLoadingState *self,
                                                  gboolean                 show)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));

    priv = lrg_template_loading_state_get_instance_private (self);

    show = !!show;

    if (priv->show_progress_bar != show)
    {
        priv->show_progress_bar = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_PROGRESS_BAR]);
    }
}

gboolean
lrg_template_loading_state_get_show_percentage (LrgTemplateLoadingState *self)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self), TRUE);

    priv = lrg_template_loading_state_get_instance_private (self);
    return priv->show_percentage;
}

void
lrg_template_loading_state_set_show_percentage (LrgTemplateLoadingState *self,
                                                gboolean                 show)
{
    LrgTemplateLoadingStatePrivate *priv;

    g_return_if_fail (LRG_IS_TEMPLATE_LOADING_STATE (self));

    priv = lrg_template_loading_state_get_instance_private (self);

    show = !!show;

    if (priv->show_percentage != show)
    {
        priv->show_percentage = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_PERCENTAGE]);
    }
}
