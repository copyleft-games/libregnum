/* lrg-chart.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart.h"
#include "lrg-chart-private.h"
#include "../tween/lrg-tween.h"
#include "../tween/lrg-tween-manager.h"

/* ==========================================================================
 * Default Colors
 * ========================================================================== */

static const GrlColor DEFAULT_BG_COLOR = { 30, 30, 30, 255 };
static const GrlColor DEFAULT_TEXT_COLOR = { 220, 220, 220, 255 };

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    gchar                *title;
    GPtrArray            *series;  /* Array of LrgChartDataSeries* */

    /* Margins */
    gfloat                margin_top;
    gfloat                margin_right;
    gfloat                margin_bottom;
    gfloat                margin_left;

    /* Colors */
    GrlColor              background_color;
    GrlColor              text_color;

    /* Animation */
    LrgChartAnimationType animation_type;
    gfloat                animation_duration;
    gfloat                animation_progress;
    gboolean              animating;
    LrgTween             *active_tween;
    gfloat                anim_target;  /* Internal target for tween (0 to 1) */

    /* Interactivity */
    gboolean              hover_enabled;
    LrgChartHitInfo      *current_hover;

    /* Cached content bounds */
    GrlRectangle          content_bounds;
    gboolean              layout_dirty;
} LrgChartPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgChart, lrg_chart, LRG_TYPE_WIDGET)

/* ==========================================================================
 * Signals
 * ========================================================================== */

enum
{
    SIGNAL_DATA_CLICKED,
    SIGNAL_HOVER_CHANGED,
    SIGNAL_DATA_CHANGED,
    SIGNAL_ANIMATION_FINISHED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_TITLE,
    PROP_SERIES_COUNT,
    PROP_MARGIN_TOP,
    PROP_MARGIN_RIGHT,
    PROP_MARGIN_BOTTOM,
    PROP_MARGIN_LEFT,
    PROP_BACKGROUND_COLOR,
    PROP_TEXT_COLOR,
    PROP_ANIMATION_TYPE,
    PROP_ANIMATION_DURATION,
    PROP_ANIMATION_PROGRESS,
    PROP_HOVER_ENABLED,
    PROP_ANIM_TARGET,  /* Internal, used by tween */
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
on_series_changed (LrgChartDataSeries *series,
                   gpointer            user_data)
{
    LrgChart *self = LRG_CHART (user_data);
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    priv->layout_dirty = TRUE;
    lrg_chart_update_data (self);
    g_signal_emit (self, signals[SIGNAL_DATA_CHANGED], 0);
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_chart_real_update_data (LrgChart *self)
{
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    /* Mark layout as dirty - subclasses will rebuild */
    priv->layout_dirty = TRUE;
}

static void
lrg_chart_real_rebuild_layout (LrgChart *self)
{
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);
    gfloat x, y, width, height;

    /* Calculate content bounds from widget size minus margins */
    x = lrg_widget_get_world_x (LRG_WIDGET (self)) + priv->margin_left;
    y = lrg_widget_get_world_y (LRG_WIDGET (self)) + priv->margin_top;
    width = lrg_widget_get_width (LRG_WIDGET (self)) - priv->margin_left - priv->margin_right;
    height = lrg_widget_get_height (LRG_WIDGET (self)) - priv->margin_top - priv->margin_bottom;

    priv->content_bounds.x = x;
    priv->content_bounds.y = y;
    priv->content_bounds.width = (width > 0) ? width : 0;
    priv->content_bounds.height = (height > 0) ? height : 0;

    priv->layout_dirty = FALSE;
}

static gboolean
lrg_chart_real_hit_test (LrgChart        *self,
                         gfloat           x,
                         gfloat           y,
                         LrgChartHitInfo *out_hit)
{
    /* Default implementation: no hit testing */
    if (out_hit != NULL)
        lrg_chart_hit_info_clear (out_hit);

    return FALSE;
}

static void
lrg_chart_real_calculate_bounds (LrgChart *self)
{
    /* Default: just rebuild layout which calculates content bounds */
    lrg_chart_rebuild_layout (self);
}

static void
on_animation_finished (LrgTweenBase *tween,
                       gpointer      user_data)
{
    LrgChart *self = LRG_CHART (user_data);
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    priv->animating = FALSE;
    priv->animation_progress = 1.0f;
    g_clear_object (&priv->active_tween);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_PROGRESS]);

    /* Emit animation-finished signal */
    g_signal_emit (self, signals[SIGNAL_ANIMATION_FINISHED], 0);
}

static void
lrg_chart_real_animate_to_data (LrgChart              *self,
                                 LrgChartAnimationType  animation_type,
                                 gfloat                 duration)
{
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    /* Cancel any existing animation */
    if (priv->active_tween != NULL)
    {
        lrg_tween_base_stop (LRG_TWEEN_BASE (priv->active_tween));
        g_clear_object (&priv->active_tween);
    }

    /* If animation is disabled, just set to complete */
    if (animation_type == LRG_CHART_ANIM_NONE || duration <= 0.0f)
    {
        priv->animation_progress = 1.0f;
        priv->animating = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_PROGRESS]);
        return;
    }

    /* Start animation from 0 */
    priv->animation_progress = 0.0f;
    priv->anim_target = 0.0f;
    priv->animating = TRUE;
    priv->animation_duration = duration;
    priv->animation_type = animation_type;

    /* Create tween to animate anim-target from 0 to 1 */
    priv->active_tween = lrg_tween_new (G_OBJECT (self), "anim-target", duration);
    lrg_tween_set_from_float (priv->active_tween, 0.0f);
    lrg_tween_set_to_float (priv->active_tween, 1.0f);
    lrg_tween_base_set_easing (LRG_TWEEN_BASE (priv->active_tween), LRG_EASING_EASE_OUT_QUAD);

    /* Connect to finished signal */
    g_signal_connect (priv->active_tween, "finished",
                      G_CALLBACK (on_animation_finished), self);

    /* Start the tween - it will be updated manually in the chart's update */
    lrg_tween_base_start (LRG_TWEEN_BASE (priv->active_tween));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_PROGRESS]);
}

/* ==========================================================================
 * LrgWidget Virtual Method Overrides
 * ========================================================================== */

static gboolean
lrg_chart_handle_event (LrgWidget        *widget,
                        const LrgUIEvent *event)
{
    LrgChart *self = LRG_CHART (widget);
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);
    LrgUIEventType event_type;
    gfloat local_x, local_y;
    g_autoptr(LrgChartHitInfo) hit_info = NULL;

    if (!priv->hover_enabled)
        return FALSE;

    /* Convert to widget-local coordinates */
    local_x = lrg_ui_event_get_x (event) - lrg_widget_get_world_x (widget);
    local_y = lrg_ui_event_get_y (event) - lrg_widget_get_world_y (widget);
    event_type = lrg_ui_event_get_event_type (event);

    switch (event_type)
    {
    case LRG_UI_EVENT_MOUSE_MOVE:
        /* Perform hit test for hover */
        hit_info = lrg_chart_hit_info_new ();
        if (lrg_chart_hit_test (self, local_x, local_y, hit_info))
        {
            /* Check if hover changed */
            gboolean changed = FALSE;

            if (priv->current_hover == NULL)
            {
                priv->current_hover = lrg_chart_hit_info_copy (hit_info);
                changed = TRUE;
            }
            else if (lrg_chart_hit_info_get_series_index (priv->current_hover) !=
                     lrg_chart_hit_info_get_series_index (hit_info) ||
                     lrg_chart_hit_info_get_point_index (priv->current_hover) !=
                     lrg_chart_hit_info_get_point_index (hit_info))
            {
                lrg_chart_hit_info_free (priv->current_hover);
                priv->current_hover = lrg_chart_hit_info_copy (hit_info);
                changed = TRUE;
            }

            if (changed)
            {
                g_signal_emit (self, signals[SIGNAL_HOVER_CHANGED], 0, priv->current_hover);
            }
            return TRUE;
        }
        else if (priv->current_hover != NULL)
        {
            /* No longer hovering anything */
            lrg_chart_hit_info_free (priv->current_hover);
            priv->current_hover = NULL;
            g_signal_emit (self, signals[SIGNAL_HOVER_CHANGED], 0, NULL);
        }
        break;

    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        /* Check for click on data */
        hit_info = lrg_chart_hit_info_new ();
        if (lrg_chart_hit_test (self, local_x, local_y, hit_info))
        {
            g_signal_emit (self, signals[SIGNAL_DATA_CLICKED], 0, hit_info);
            return TRUE;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_chart_finalize (GObject *object)
{
    LrgChart *self = LRG_CHART (object);
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    g_free (priv->title);
    g_ptr_array_unref (priv->series);

    if (priv->current_hover != NULL)
        lrg_chart_hit_info_free (priv->current_hover);

    /* Clean up any active tween */
    if (priv->active_tween != NULL)
    {
        lrg_tween_base_stop (LRG_TWEEN_BASE (priv->active_tween));
        g_clear_object (&priv->active_tween);
    }

    G_OBJECT_CLASS (lrg_chart_parent_class)->finalize (object);
}

static void
lrg_chart_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgChart *self = LRG_CHART (object);
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TITLE:
        g_value_set_string (value, priv->title);
        break;
    case PROP_SERIES_COUNT:
        g_value_set_uint (value, priv->series->len);
        break;
    case PROP_MARGIN_TOP:
        g_value_set_float (value, priv->margin_top);
        break;
    case PROP_MARGIN_RIGHT:
        g_value_set_float (value, priv->margin_right);
        break;
    case PROP_MARGIN_BOTTOM:
        g_value_set_float (value, priv->margin_bottom);
        break;
    case PROP_MARGIN_LEFT:
        g_value_set_float (value, priv->margin_left);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, &priv->background_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &priv->text_color);
        break;
    case PROP_ANIMATION_TYPE:
        g_value_set_enum (value, priv->animation_type);
        break;
    case PROP_ANIMATION_DURATION:
        g_value_set_float (value, priv->animation_duration);
        break;
    case PROP_ANIMATION_PROGRESS:
        g_value_set_float (value, priv->animation_progress);
        break;
    case PROP_HOVER_ENABLED:
        g_value_set_boolean (value, priv->hover_enabled);
        break;
    case PROP_ANIM_TARGET:
        g_value_set_float (value, priv->anim_target);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgChart *self = LRG_CHART (object);
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TITLE:
        lrg_chart_set_title (self, g_value_get_string (value));
        break;
    case PROP_MARGIN_TOP:
        priv->margin_top = g_value_get_float (value);
        priv->layout_dirty = TRUE;
        break;
    case PROP_MARGIN_RIGHT:
        priv->margin_right = g_value_get_float (value);
        priv->layout_dirty = TRUE;
        break;
    case PROP_MARGIN_BOTTOM:
        priv->margin_bottom = g_value_get_float (value);
        priv->layout_dirty = TRUE;
        break;
    case PROP_MARGIN_LEFT:
        priv->margin_left = g_value_get_float (value);
        priv->layout_dirty = TRUE;
        break;
    case PROP_BACKGROUND_COLOR:
        lrg_chart_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_chart_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_ANIMATION_TYPE:
        lrg_chart_set_animation_type (self, g_value_get_enum (value));
        break;
    case PROP_ANIMATION_DURATION:
        lrg_chart_set_animation_duration (self, g_value_get_float (value));
        break;
    case PROP_HOVER_ENABLED:
        lrg_chart_set_hover_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_ANIM_TARGET:
        {
            gfloat new_value = CLAMP (g_value_get_float (value), 0.0f, 1.0f);
            if (priv->anim_target != new_value)
            {
                priv->anim_target = new_value;
                priv->animation_progress = new_value;
                g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_PROGRESS]);
            }
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_class_init (LrgChartClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_chart_finalize;
    object_class->get_property = lrg_chart_get_property;
    object_class->set_property = lrg_chart_set_property;

    /* Override widget handle_event for hover/click */
    widget_class->handle_event = lrg_chart_handle_event;

    /* Default virtual method implementations */
    klass->update_data = lrg_chart_real_update_data;
    klass->rebuild_layout = lrg_chart_real_rebuild_layout;
    klass->hit_test = lrg_chart_real_hit_test;
    klass->calculate_bounds = lrg_chart_real_calculate_bounds;
    klass->animate_to_data = lrg_chart_real_animate_to_data;

    /* Properties */
    properties[PROP_TITLE] =
        g_param_spec_string ("title",
                             "Title",
                             "Chart title",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SERIES_COUNT] =
        g_param_spec_uint ("series-count",
                           "Series Count",
                           "Number of data series",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARGIN_TOP] =
        g_param_spec_float ("margin-top",
                            "Margin Top",
                            "Top margin",
                            0.0f, G_MAXFLOAT, 40.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARGIN_RIGHT] =
        g_param_spec_float ("margin-right",
                            "Margin Right",
                            "Right margin",
                            0.0f, G_MAXFLOAT, 20.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARGIN_BOTTOM] =
        g_param_spec_float ("margin-bottom",
                            "Margin Bottom",
                            "Bottom margin",
                            0.0f, G_MAXFLOAT, 40.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARGIN_LEFT] =
        g_param_spec_float ("margin-left",
                            "Margin Left",
                            "Left margin",
                            0.0f, G_MAXFLOAT, 50.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "Chart background color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "Default text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_ANIMATION_TYPE] =
        g_param_spec_enum ("animation-type",
                           "Animation Type",
                           "Default animation type",
                           LRG_TYPE_CHART_ANIMATION_TYPE,
                           LRG_CHART_ANIM_NONE,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_ANIMATION_DURATION] =
        g_param_spec_float ("animation-duration",
                            "Animation Duration",
                            "Animation duration in seconds",
                            0.0f, 10.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_ANIMATION_PROGRESS] =
        g_param_spec_float ("animation-progress",
                            "Animation Progress",
                            "Current animation progress (0.0 to 1.0)",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HOVER_ENABLED] =
        g_param_spec_boolean ("hover-enabled",
                              "Hover Enabled",
                              "Enable hover highlighting",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgChart:anim-target: (skip)
     *
     * Internal property used by the tween system for animation.
     * Do not set this directly - use lrg_chart_animate_to_data() instead.
     */
    properties[PROP_ANIM_TARGET] =
        g_param_spec_float ("anim-target",
                            "Animation Target",
                            "Internal animation target (0.0 to 1.0)",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    /**
     * LrgChart::data-clicked:
     * @self: the chart
     * @hit_info: information about what was clicked
     *
     * Emitted when a chart element (bar, point, slice, etc.) is clicked.
     */
    signals[SIGNAL_DATA_CLICKED] =
        g_signal_new ("data-clicked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_CHART_HIT_INFO);

    /**
     * LrgChart::hover-changed:
     * @self: the chart
     * @hit_info: (nullable): information about what is hovered, or %NULL
     *
     * Emitted when the hovered element changes.
     */
    signals[SIGNAL_HOVER_CHANGED] =
        g_signal_new ("hover-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_CHART_HIT_INFO);

    /**
     * LrgChart::data-changed:
     * @self: the chart
     *
     * Emitted when the underlying data changes.
     */
    signals[SIGNAL_DATA_CHANGED] =
        g_signal_new ("data-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgChart::animation-finished:
     * @self: the chart
     *
     * Emitted when a chart animation completes.
     */
    signals[SIGNAL_ANIMATION_FINISHED] =
        g_signal_new ("animation-finished",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_chart_init (LrgChart *self)
{
    LrgChartPrivate *priv = lrg_chart_get_instance_private (self);

    priv->title = NULL;
    priv->series = g_ptr_array_new_with_free_func (g_object_unref);

    priv->margin_top = 40.0f;
    priv->margin_right = 20.0f;
    priv->margin_bottom = 40.0f;
    priv->margin_left = 50.0f;

    priv->background_color = DEFAULT_BG_COLOR;
    priv->text_color = DEFAULT_TEXT_COLOR;

    priv->animation_type = LRG_CHART_ANIM_NONE;
    priv->animation_duration = 0.5f;
    priv->animation_progress = 1.0f;  /* Start fully animated in */
    priv->animating = FALSE;
    priv->active_tween = NULL;
    priv->anim_target = 1.0f;

    priv->hover_enabled = TRUE;
    priv->current_hover = NULL;

    priv->content_bounds.x = 0;
    priv->content_bounds.y = 0;
    priv->content_bounds.width = 0;
    priv->content_bounds.height = 0;
    priv->layout_dirty = TRUE;
}

/* ==========================================================================
 * Title
 * ========================================================================== */

const gchar *
lrg_chart_get_title (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), NULL);

    priv = lrg_chart_get_instance_private (self);
    return priv->title;
}

void
lrg_chart_set_title (LrgChart    *self,
                     const gchar *title)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);

    if (g_strcmp0 (priv->title, title) == 0)
        return;

    g_free (priv->title);
    priv->title = g_strdup (title);
    priv->layout_dirty = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

/* ==========================================================================
 * Series Management
 * ========================================================================== */

guint
lrg_chart_get_series_count (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), 0);

    priv = lrg_chart_get_instance_private (self);
    return priv->series->len;
}

LrgChartDataSeries *
lrg_chart_get_series (LrgChart *self,
                      guint     index)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), NULL);

    priv = lrg_chart_get_instance_private (self);

    g_return_val_if_fail (index < priv->series->len, NULL);

    return g_ptr_array_index (priv->series, index);
}

GPtrArray *
lrg_chart_get_series_list (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), NULL);

    priv = lrg_chart_get_instance_private (self);
    return priv->series;
}

guint
lrg_chart_add_series (LrgChart           *self,
                      LrgChartDataSeries *series)
{
    LrgChartPrivate *priv;
    guint index;

    g_return_val_if_fail (LRG_IS_CHART (self), 0);
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (series), 0);

    priv = lrg_chart_get_instance_private (self);

    /* Connect to series changed signal */
    g_signal_connect (series, "changed",
                      G_CALLBACK (on_series_changed), self);

    /* Takes ownership */
    g_ptr_array_add (priv->series, series);
    index = priv->series->len - 1;

    priv->layout_dirty = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES_COUNT]);

    lrg_chart_update_data (self);
    g_signal_emit (self, signals[SIGNAL_DATA_CHANGED], 0);

    return index;
}

gboolean
lrg_chart_remove_series (LrgChart *self,
                         guint     index)
{
    LrgChartPrivate *priv;
    LrgChartDataSeries *series;

    g_return_val_if_fail (LRG_IS_CHART (self), FALSE);

    priv = lrg_chart_get_instance_private (self);

    if (index >= priv->series->len)
        return FALSE;

    series = g_ptr_array_index (priv->series, index);
    g_signal_handlers_disconnect_by_func (series, on_series_changed, self);

    g_ptr_array_remove_index (priv->series, index);

    priv->layout_dirty = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES_COUNT]);

    lrg_chart_update_data (self);
    g_signal_emit (self, signals[SIGNAL_DATA_CHANGED], 0);

    return TRUE;
}

void
lrg_chart_clear_series (LrgChart *self)
{
    LrgChartPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);

    if (priv->series->len == 0)
        return;

    /* Disconnect all signals */
    for (i = 0; i < priv->series->len; i++)
    {
        LrgChartDataSeries *series = g_ptr_array_index (priv->series, i);
        g_signal_handlers_disconnect_by_func (series, on_series_changed, self);
    }

    g_ptr_array_set_size (priv->series, 0);

    priv->layout_dirty = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SERIES_COUNT]);

    lrg_chart_update_data (self);
    g_signal_emit (self, signals[SIGNAL_DATA_CHANGED], 0);
}

/* ==========================================================================
 * Margins
 * ========================================================================== */

gfloat
lrg_chart_get_margin_top (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), 0.0f);

    priv = lrg_chart_get_instance_private (self);
    return priv->margin_top;
}

gfloat
lrg_chart_get_margin_right (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), 0.0f);

    priv = lrg_chart_get_instance_private (self);
    return priv->margin_right;
}

gfloat
lrg_chart_get_margin_bottom (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), 0.0f);

    priv = lrg_chart_get_instance_private (self);
    return priv->margin_bottom;
}

gfloat
lrg_chart_get_margin_left (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), 0.0f);

    priv = lrg_chart_get_instance_private (self);
    return priv->margin_left;
}

void
lrg_chart_set_margins (LrgChart *self,
                       gfloat    top,
                       gfloat    right,
                       gfloat    bottom,
                       gfloat    left)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);

    priv->margin_top = top;
    priv->margin_right = right;
    priv->margin_bottom = bottom;
    priv->margin_left = left;
    priv->layout_dirty = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN_TOP]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN_RIGHT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN_BOTTOM]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN_LEFT]);
}

/* ==========================================================================
 * Colors
 * ========================================================================== */

const GrlColor *
lrg_chart_get_background_color (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), NULL);

    priv = lrg_chart_get_instance_private (self);
    return &priv->background_color;
}

void
lrg_chart_set_background_color (LrgChart       *self,
                                const GrlColor *color)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));
    g_return_if_fail (color != NULL);

    priv = lrg_chart_get_instance_private (self);

    if (priv->background_color.r == color->r &&
        priv->background_color.g == color->g &&
        priv->background_color.b == color->b &&
        priv->background_color.a == color->a)
        return;

    priv->background_color = *color;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

const GrlColor *
lrg_chart_get_text_color (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), NULL);

    priv = lrg_chart_get_instance_private (self);
    return &priv->text_color;
}

void
lrg_chart_set_text_color (LrgChart       *self,
                          const GrlColor *color)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));
    g_return_if_fail (color != NULL);

    priv = lrg_chart_get_instance_private (self);

    if (priv->text_color.r == color->r &&
        priv->text_color.g == color->g &&
        priv->text_color.b == color->b &&
        priv->text_color.a == color->a)
        return;

    priv->text_color = *color;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

LrgChartAnimationType
lrg_chart_get_animation_type (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), LRG_CHART_ANIM_NONE);

    priv = lrg_chart_get_instance_private (self);
    return priv->animation_type;
}

void
lrg_chart_set_animation_type (LrgChart              *self,
                              LrgChartAnimationType  type)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);

    if (priv->animation_type == type)
        return;

    priv->animation_type = type;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_TYPE]);
}

gfloat
lrg_chart_get_animation_duration (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), 0.0f);

    priv = lrg_chart_get_instance_private (self);
    return priv->animation_duration;
}

void
lrg_chart_set_animation_duration (LrgChart *self,
                                  gfloat    duration)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);

    if (priv->animation_duration == duration)
        return;

    priv->animation_duration = duration;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_DURATION]);
}

gfloat
lrg_chart_get_animation_progress (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), 0.0f);

    priv = lrg_chart_get_instance_private (self);
    return priv->animation_progress;
}

/* ==========================================================================
 * Interactivity
 * ========================================================================== */

gboolean
lrg_chart_get_hover_enabled (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), FALSE);

    priv = lrg_chart_get_instance_private (self);
    return priv->hover_enabled;
}

void
lrg_chart_set_hover_enabled (LrgChart *self,
                             gboolean  enabled)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);

    enabled = !!enabled;

    if (priv->hover_enabled == enabled)
        return;

    priv->hover_enabled = enabled;

    /* Clear hover when disabled */
    if (!enabled && priv->current_hover != NULL)
    {
        lrg_chart_hit_info_free (priv->current_hover);
        priv->current_hover = NULL;
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOVER_ENABLED]);
}

const LrgChartHitInfo *
lrg_chart_get_current_hover (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), NULL);

    priv = lrg_chart_get_instance_private (self);
    return priv->current_hover;
}

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

void
lrg_chart_update_data (LrgChart *self)
{
    LrgChartClass *klass;

    g_return_if_fail (LRG_IS_CHART (self));

    klass = LRG_CHART_GET_CLASS (self);

    if (klass->update_data != NULL)
        klass->update_data (self);
}

void
lrg_chart_rebuild_layout (LrgChart *self)
{
    LrgChartClass *klass;

    g_return_if_fail (LRG_IS_CHART (self));

    klass = LRG_CHART_GET_CLASS (self);

    if (klass->rebuild_layout != NULL)
        klass->rebuild_layout (self);
}

gboolean
lrg_chart_hit_test (LrgChart        *self,
                    gfloat           x,
                    gfloat           y,
                    LrgChartHitInfo *out_hit)
{
    LrgChartClass *klass;

    g_return_val_if_fail (LRG_IS_CHART (self), FALSE);

    klass = LRG_CHART_GET_CLASS (self);

    if (klass->hit_test != NULL)
        return klass->hit_test (self, x, y, out_hit);

    return FALSE;
}

void
lrg_chart_animate_to_data (LrgChart              *self,
                           LrgChartAnimationType  animation_type,
                           gfloat                 duration)
{
    LrgChartClass *klass;

    g_return_if_fail (LRG_IS_CHART (self));

    klass = LRG_CHART_GET_CLASS (self);

    if (klass->animate_to_data != NULL)
        klass->animate_to_data (self, animation_type, duration);
}

/* ==========================================================================
 * Content Bounds
 * ========================================================================== */

void
lrg_chart_get_content_bounds (LrgChart     *self,
                              GrlRectangle *out_bounds)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));
    g_return_if_fail (out_bounds != NULL);

    priv = lrg_chart_get_instance_private (self);

    /* Rebuild layout if needed */
    if (priv->layout_dirty)
        lrg_chart_rebuild_layout (self);

    *out_bounds = priv->content_bounds;
}

/* ==========================================================================
 * Internal Functions (for subclasses)
 * ========================================================================== */

/**
 * lrg_chart_set_animation_progress:
 * @self: an #LrgChart
 * @progress: the animation progress (0.0 to 1.0)
 *
 * Sets the animation progress. This is called internally during animation.
 * Subclasses may also call this when implementing custom animation.
 */
void
lrg_chart_set_animation_progress (LrgChart *self,
                                  gfloat    progress)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);

    progress = CLAMP (progress, 0.0f, 1.0f);

    if (priv->animation_progress == progress)
        return;

    priv->animation_progress = progress;

    if (progress >= 1.0f)
        priv->animating = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATION_PROGRESS]);
}

/**
 * lrg_chart_is_animating:
 * @self: an #LrgChart
 *
 * Checks if the chart is currently animating.
 *
 * Returns: %TRUE if animating
 */
gboolean
lrg_chart_is_animating (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), FALSE);

    priv = lrg_chart_get_instance_private (self);
    return priv->animating;
}

/**
 * lrg_chart_is_layout_dirty:
 * @self: an #LrgChart
 *
 * Checks if the layout needs to be rebuilt.
 *
 * Returns: %TRUE if layout is dirty
 */
gboolean
lrg_chart_is_layout_dirty (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART (self), FALSE);

    priv = lrg_chart_get_instance_private (self);
    return priv->layout_dirty;
}

/**
 * lrg_chart_mark_layout_dirty:
 * @self: an #LrgChart
 *
 * Marks the layout as needing rebuild.
 */
void
lrg_chart_mark_layout_dirty (LrgChart *self)
{
    LrgChartPrivate *priv;

    g_return_if_fail (LRG_IS_CHART (self));

    priv = lrg_chart_get_instance_private (self);
    priv->layout_dirty = TRUE;
}
