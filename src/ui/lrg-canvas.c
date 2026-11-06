/* lrg-canvas.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Root UI container that handles rendering and input dispatch.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-canvas.h"
#include "lrg-canvas-private.h"
#include "../lrg-log.h"
#include <graylib.h>
#include <raylib.h>

struct _LrgCanvas
{
    LrgContainer  parent_instance;
    LrgWidget    *focused_widget;
    LrgWidget    *hovered_widget;
    gfloat        last_mouse_x;
    gfloat        last_mouse_y;
};

G_DEFINE_TYPE (LrgCanvas, lrg_canvas, LRG_TYPE_CONTAINER)

enum
{
    PROP_0,
    PROP_FOCUSED_WIDGET,
    PROP_HOVERED_WIDGET,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Process-wide pointer transform (window pixels -> canvas space);
 * registered by whoever owns the scaled presentation, see
 * lrg_canvas_set_default_pointer_transform() */
static LrgCanvasPointerTransform default_pointer_transform = NULL;
static gpointer default_pointer_transform_data = NULL;

/*
 * Recursive helper to find widget at point.
 * Searches depth-first, returning the deepest matching widget.
 */
static LrgWidget *
find_widget_at_point_recursive (LrgWidget *widget,
                                gfloat     x,
                                gfloat     y)
{
    LrgWidget *found = NULL;

    if (!lrg_widget_get_visible (widget) || !lrg_widget_get_enabled (widget))
    {
        return NULL;
    }

    /* Check if point is within this widget */
    if (!lrg_widget_contains_point (widget, x, y))
    {
        return NULL;
    }

    /* If this is a container, search children (reverse order for z-order) */
    if (LRG_IS_CONTAINER (widget))
    {
        LrgContainer *container = LRG_CONTAINER (widget);
        GList        *children;
        GList        *l;

        children = lrg_container_get_children (container);

        /* Iterate in reverse to get topmost widget first */
        for (l = g_list_last (children); l != NULL; l = l->prev)
        {
            LrgWidget *child = LRG_WIDGET (l->data);
            LrgWidget *child_hit;

            child_hit = find_widget_at_point_recursive (child, x, y);
            if (child_hit != NULL)
            {
                found = child_hit;
                break;
            }
        }
    }

    /* Return deepest hit, or this widget if no children matched */
    return (found != NULL) ? found : widget;
}

/*
 * Dispatch an event to a widget and optionally bubble up to parents.
 */
static gboolean
dispatch_event (LrgWidget         *widget,
                const LrgUIEvent  *event)
{
    gboolean handled = FALSE;

    if (widget == NULL)
    {
        return FALSE;
    }

    /* Let the widget handle the event */
    handled = lrg_widget_handle_event (widget, event);

    return handled;
}

static void
lrg_canvas_draw (LrgWidget *widget)
{
    LrgContainer *container = LRG_CONTAINER (widget);
    GList        *children;
    GList        *l;

    /*
     * Canvas doesn't have its own visual representation,
     * just draw all children.
     */
    children = lrg_container_get_children (container);

    for (l = children; l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);

        if (lrg_widget_get_visible (child))
        {
            lrg_widget_draw (child);
        }
    }
}

static void
lrg_canvas_finalize (GObject *object)
{
    LrgCanvas *self = LRG_CANVAS (object);

    /* Clear weak references */
    self->focused_widget = NULL;
    self->hovered_widget = NULL;

    G_OBJECT_CLASS (lrg_canvas_parent_class)->finalize (object);
}

static void
lrg_canvas_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LrgCanvas *self = LRG_CANVAS (object);

    switch (prop_id)
    {
    case PROP_FOCUSED_WIDGET:
        g_value_set_object (value, self->focused_widget);
        break;
    case PROP_HOVERED_WIDGET:
        g_value_set_object (value, self->hovered_widget);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_canvas_class_init (LrgCanvasClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_canvas_finalize;
    object_class->get_property = lrg_canvas_get_property;

    widget_class->draw = lrg_canvas_draw;

    /**
     * LrgCanvas:focused-widget:
     *
     * The widget that currently has keyboard focus.
     */
    properties[PROP_FOCUSED_WIDGET] =
        g_param_spec_object ("focused-widget",
                             "Focused Widget",
                             "The widget with keyboard focus",
                             LRG_TYPE_WIDGET,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgCanvas:hovered-widget:
     *
     * The widget currently under the mouse cursor.
     */
    properties[PROP_HOVERED_WIDGET] =
        g_param_spec_object ("hovered-widget",
                             "Hovered Widget",
                             "The widget under the mouse cursor",
                             LRG_TYPE_WIDGET,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_canvas_init (LrgCanvas *self)
{
    self->focused_widget = NULL;
    self->hovered_widget = NULL;
    self->last_mouse_x = 0.0f;
    self->last_mouse_y = 0.0f;
}

/*
 * lrg_canvas_new:
 *
 * Creates a new canvas - the root container for UI widgets.
 *
 * Returns: (transfer full): A new #LrgCanvas
 */
LrgCanvas *
lrg_canvas_new (void)
{
    return g_object_new (LRG_TYPE_CANVAS, NULL);
}

/*
 * lrg_canvas_set_default_pointer_transform:
 * @transform: (nullable) (scope notified): transform applied to pointer
 *   coordinates before hit-testing, or %NULL to restore raw coordinates
 * @user_data: user data passed to @transform
 *
 * Installs a process-wide pointer transform used by every canvas in
 * lrg_canvas_handle_input().
 */
void
lrg_canvas_set_default_pointer_transform (LrgCanvasPointerTransform transform,
                                          gpointer                  user_data)
{
    default_pointer_transform = transform;
    default_pointer_transform_data = user_data;
}

/*
 * lrg_canvas_render:
 * @self: an #LrgCanvas
 *
 * Renders the entire widget tree starting from this canvas.
 */
void
lrg_canvas_render (LrgCanvas *self)
{
    g_return_if_fail (LRG_IS_CANVAS (self));

    if (lrg_widget_get_visible (LRG_WIDGET (self)))
    {
        lrg_widget_draw (LRG_WIDGET (self));
    }
}

/*
 * _lrg_canvas_apply_pointer_transform:
 *
 * See lrg-canvas-private.h.
 */
void
_lrg_canvas_apply_pointer_transform (gfloat *x,
                                     gfloat *y)
{
    if (default_pointer_transform != NULL)
        default_pointer_transform (x, y, default_pointer_transform_data);
}

/*
 * lrg_canvas_handle_input:
 * @self: an #LrgCanvas
 *
 * Polls graylib into an #LrgCanvasInput and processes it.
 */
void
lrg_canvas_handle_input (LrgCanvas *self)
{
    LrgCanvasInput input;

    g_return_if_fail (LRG_IS_CANVAS (self));

    lrg_canvas_input_poll (&input, GetFrameTime ());
    lrg_canvas_process_input (self, &input);
}

/*
 * dispatch_button:
 *
 * Sends one button-down or button-up event for every button set in
 * @mask. The event carries (button, x, y) in that order, matching
 * lrg_ui_event_new_mouse_button().
 */
static void
dispatch_button (LrgWidget      *target,
                 LrgUIEventType  type,
                 guint           mask,
                 gfloat          x,
                 gfloat          y)
{
    static const GrlMouseButton buttons[] = {
        GRL_MOUSE_BUTTON_LEFT,
        GRL_MOUSE_BUTTON_RIGHT,
        GRL_MOUSE_BUTTON_MIDDLE
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (buttons); i++)
    {
        g_autoptr(LrgUIEvent) event = NULL;

        if ((mask & LRG_CANVAS_BUTTON_MASK (buttons[i])) == 0)
            continue;

        event = lrg_ui_event_new_mouse_button (type, (gint) buttons[i], x, y);
        dispatch_event (target, event);
    }
}

/*
 * lrg_canvas_process_input:
 * @self: an #LrgCanvas
 * @input: one frame of input
 *
 * Turns an input frame into UI events and dispatches them.
 */
void
lrg_canvas_process_input (LrgCanvas            *self,
                          const LrgCanvasInput *input)
{
    gfloat       mouse_x;
    gfloat       mouse_y;
    LrgWidget   *target;
    LrgUIEvent  *event;
    guint        i;

    g_return_if_fail (LRG_IS_CANVAS (self));
    g_return_if_fail (input != NULL);

    mouse_x = input->pointer_x;
    mouse_y = input->pointer_y;

    /* Find widget under mouse */
    target = lrg_canvas_widget_at_point (self, mouse_x, mouse_y);

    /* Handle hover changes */
    if (target != self->hovered_widget)
    {
        /* Send focus out to old widget */
        if (self->hovered_widget != NULL)
        {
            event = lrg_ui_event_new_focus_out ();
            dispatch_event (self->hovered_widget, event);
            lrg_ui_event_free (event);
        }

        self->hovered_widget = target;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOVERED_WIDGET]);

        /* Send focus in to new widget */
        if (target != NULL)
        {
            event = lrg_ui_event_new_focus_in ();
            dispatch_event (target, event);
            lrg_ui_event_free (event);
        }
    }

    /* Handle mouse movement */
    if (mouse_x != self->last_mouse_x || mouse_y != self->last_mouse_y)
    {
        event = lrg_ui_event_new_mouse_move (mouse_x, mouse_y);
        dispatch_event (target, event);
        lrg_ui_event_free (event);

        self->last_mouse_x = mouse_x;
        self->last_mouse_y = mouse_y;
    }

    /* Button presses */
    if (input->buttons_pressed != 0)
    {
        /* Pointer debugging: run with G_MESSAGES_DEBUG=all to trace
         * why a click did or didn't reach a widget */
        g_debug ("canvas click at ui(%.1f, %.1f) -> %s",
                 mouse_x, mouse_y,
                 target != NULL ? G_OBJECT_TYPE_NAME (target) : "(nothing)");

        /* Set focus on left click */
        if ((input->buttons_pressed & LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT)) &&
            target != NULL && target != self->focused_widget)
        {
            lrg_canvas_set_focused_widget (self, target);
        }

        dispatch_button (target, LRG_UI_EVENT_MOUSE_BUTTON_DOWN,
                         input->buttons_pressed, mouse_x, mouse_y);
    }

    /* Button releases */
    if (input->buttons_released != 0)
    {
        dispatch_button (target, LRG_UI_EVENT_MOUSE_BUTTON_UP,
                         input->buttons_released, mouse_x, mouse_y);
    }

    /* Mouse wheel */
    if (input->wheel_x != 0.0f || input->wheel_y != 0.0f)
    {
        event = lrg_ui_event_new_scroll (mouse_x, mouse_y,
                                         input->wheel_x, input->wheel_y);
        dispatch_event (target, event);
        lrg_ui_event_free (event);
    }

    /* Key presses go to the focused widget */
    for (i = 0; i < input->n_keys; i++)
    {
        event = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN, input->keys[i]);
        dispatch_event (self->focused_widget, event);
        lrg_ui_event_free (event);
    }

    /* Typed characters go to the focused widget */
    for (i = 0; i < input->n_chars; i++)
    {
        event = lrg_ui_event_new_text_input (input->chars[i]);
        dispatch_event (self->focused_widget, event);
        lrg_ui_event_free (event);
    }
}

/*
 * lrg_canvas_widget_at_point:
 * @self: an #LrgCanvas
 * @x: the x coordinate
 * @y: the y coordinate
 *
 * Finds the topmost visible widget at the given screen coordinates.
 *
 * Returns: (transfer none) (nullable): The widget at the point, or %NULL
 */
LrgWidget *
lrg_canvas_widget_at_point (LrgCanvas *self,
                            gfloat     x,
                            gfloat     y)
{
    LrgWidget *hit;

    g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);

    hit = find_widget_at_point_recursive (LRG_WIDGET (self), x, y);

    /* Don't return the canvas itself */
    if (hit == LRG_WIDGET (self))
    {
        return NULL;
    }

    return hit;
}

/*
 * lrg_canvas_get_focused_widget:
 * @self: an #LrgCanvas
 *
 * Gets the currently focused widget.
 *
 * Returns: (transfer none) (nullable): The focused widget, or %NULL
 */
LrgWidget *
lrg_canvas_get_focused_widget (LrgCanvas *self)
{
    g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);
    return self->focused_widget;
}

/*
 * lrg_canvas_set_focused_widget:
 * @self: an #LrgCanvas
 * @widget: (nullable): the widget to focus, or %NULL to clear focus
 *
 * Sets the focused widget.
 */
void
lrg_canvas_set_focused_widget (LrgCanvas *self,
                               LrgWidget *widget)
{
    LrgUIEvent *event;

    g_return_if_fail (LRG_IS_CANVAS (self));
    g_return_if_fail (widget == NULL || LRG_IS_WIDGET (widget));

    if (self->focused_widget == widget)
    {
        return;
    }

    /* Send focus out to old widget */
    if (self->focused_widget != NULL)
    {
        event = lrg_ui_event_new_focus_out ();
        dispatch_event (self->focused_widget, event);
        lrg_ui_event_free (event);
    }

    self->focused_widget = widget;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED_WIDGET]);

    /* Send focus in to new widget */
    if (widget != NULL)
    {
        event = lrg_ui_event_new_focus_in ();
        dispatch_event (widget, event);
        lrg_ui_event_free (event);
    }
}

/*
 * lrg_canvas_get_hovered_widget:
 * @self: an #LrgCanvas
 *
 * Gets the widget currently under the mouse cursor.
 *
 * Returns: (transfer none) (nullable): The hovered widget, or %NULL
 */
LrgWidget *
lrg_canvas_get_hovered_widget (LrgCanvas *self)
{
    g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);
    return self->hovered_widget;
}
