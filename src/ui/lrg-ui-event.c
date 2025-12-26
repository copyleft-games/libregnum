/* lrg-ui-event.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * UI event representing user interaction with widgets.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-ui-event.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgUIEvent
{
    LrgUIEventType type;
    gfloat         x;
    gfloat         y;
    gint           button;
    GrlKey         key;
    gfloat         scroll_x;
    gfloat         scroll_y;
    gboolean       consumed;
};

G_DEFINE_BOXED_TYPE (LrgUIEvent, lrg_ui_event,
                     lrg_ui_event_copy, lrg_ui_event_free)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_ui_event_new:
 * @type: the event type
 *
 * Creates a new UI event with the specified type.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new (LrgUIEventType type)
{
    LrgUIEvent *self;

    self = g_slice_new0 (LrgUIEvent);
    self->type = type;
    self->x = 0.0f;
    self->y = 0.0f;
    self->button = 0;
    self->key = GRL_KEY_NULL;
    self->scroll_x = 0.0f;
    self->scroll_y = 0.0f;
    self->consumed = FALSE;

    return self;
}

/**
 * lrg_ui_event_new_mouse_move:
 * @x: the mouse X position
 * @y: the mouse Y position
 *
 * Creates a new mouse move event.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new_mouse_move (gfloat x,
                             gfloat y)
{
    LrgUIEvent *self;

    self = lrg_ui_event_new (LRG_UI_EVENT_MOUSE_MOVE);
    self->x = x;
    self->y = y;

    return self;
}

/**
 * lrg_ui_event_new_mouse_button:
 * @type: %LRG_UI_EVENT_MOUSE_BUTTON_DOWN or %LRG_UI_EVENT_MOUSE_BUTTON_UP
 * @button: the mouse button (0=left, 1=right, 2=middle)
 * @x: the mouse X position
 * @y: the mouse Y position
 *
 * Creates a new mouse button event.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new_mouse_button (LrgUIEventType type,
                               gint           button,
                               gfloat         x,
                               gfloat         y)
{
    LrgUIEvent *self;

    g_return_val_if_fail (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN ||
                          type == LRG_UI_EVENT_MOUSE_BUTTON_UP, NULL);

    self = lrg_ui_event_new (type);
    self->button = button;
    self->x = x;
    self->y = y;

    return self;
}

/**
 * lrg_ui_event_new_key:
 * @type: %LRG_UI_EVENT_KEY_DOWN or %LRG_UI_EVENT_KEY_UP
 * @key: the keyboard key
 *
 * Creates a new keyboard event.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new_key (LrgUIEventType type,
                      GrlKey         key)
{
    LrgUIEvent *self;

    g_return_val_if_fail (type == LRG_UI_EVENT_KEY_DOWN ||
                          type == LRG_UI_EVENT_KEY_UP, NULL);

    self = lrg_ui_event_new (type);
    self->key = key;

    return self;
}

/**
 * lrg_ui_event_new_scroll:
 * @x: the mouse X position
 * @y: the mouse Y position
 * @scroll_x: horizontal scroll amount
 * @scroll_y: vertical scroll amount
 *
 * Creates a new scroll event.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new_scroll (gfloat x,
                         gfloat y,
                         gfloat scroll_x,
                         gfloat scroll_y)
{
    LrgUIEvent *self;

    self = lrg_ui_event_new (LRG_UI_EVENT_SCROLL);
    self->x = x;
    self->y = y;
    self->scroll_x = scroll_x;
    self->scroll_y = scroll_y;

    return self;
}

/**
 * lrg_ui_event_new_focus_in:
 *
 * Creates a new focus-in event.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new_focus_in (void)
{
    return lrg_ui_event_new (LRG_UI_EVENT_FOCUS_IN);
}

/**
 * lrg_ui_event_new_focus_out:
 *
 * Creates a new focus-out event.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new_focus_out (void)
{
    return lrg_ui_event_new (LRG_UI_EVENT_FOCUS_OUT);
}

/* ==========================================================================
 * Copy/Free
 * ========================================================================== */

/**
 * lrg_ui_event_copy:
 * @self: an #LrgUIEvent
 *
 * Creates a copy of the UI event.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_copy (const LrgUIEvent *self)
{
    LrgUIEvent *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new (LrgUIEvent);
    copy->type = self->type;
    copy->x = self->x;
    copy->y = self->y;
    copy->button = self->button;
    copy->key = self->key;
    copy->scroll_x = self->scroll_x;
    copy->scroll_y = self->scroll_y;
    copy->consumed = self->consumed;

    return copy;
}

/**
 * lrg_ui_event_free:
 * @self: an #LrgUIEvent
 *
 * Frees the UI event.
 */
void
lrg_ui_event_free (LrgUIEvent *self)
{
    g_return_if_fail (self != NULL);

    g_slice_free (LrgUIEvent, self);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_ui_event_get_event_type:
 * @self: an #LrgUIEvent
 *
 * Gets the type of this event.
 *
 * Returns: The event type
 */
LrgUIEventType
lrg_ui_event_get_event_type (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, LRG_UI_EVENT_NONE);

    return self->type;
}

/**
 * lrg_ui_event_get_x:
 * @self: an #LrgUIEvent
 *
 * Gets the mouse X position.
 *
 * Returns: The X position
 */
gfloat
lrg_ui_event_get_x (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    return self->x;
}

/**
 * lrg_ui_event_get_y:
 * @self: an #LrgUIEvent
 *
 * Gets the mouse Y position.
 *
 * Returns: The Y position
 */
gfloat
lrg_ui_event_get_y (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    return self->y;
}

/**
 * lrg_ui_event_get_button:
 * @self: an #LrgUIEvent
 *
 * Gets the mouse button for mouse button events.
 *
 * Returns: The mouse button (0=left, 1=right, 2=middle)
 */
gint
lrg_ui_event_get_button (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->button;
}

/**
 * lrg_ui_event_get_key:
 * @self: an #LrgUIEvent
 *
 * Gets the keyboard key for key events.
 *
 * Returns: The key
 */
GrlKey
lrg_ui_event_get_key (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, GRL_KEY_NULL);

    return self->key;
}

/**
 * lrg_ui_event_get_scroll_x:
 * @self: an #LrgUIEvent
 *
 * Gets the horizontal scroll amount.
 *
 * Returns: The horizontal scroll amount
 */
gfloat
lrg_ui_event_get_scroll_x (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    return self->scroll_x;
}

/**
 * lrg_ui_event_get_scroll_y:
 * @self: an #LrgUIEvent
 *
 * Gets the vertical scroll amount.
 *
 * Returns: The vertical scroll amount
 */
gfloat
lrg_ui_event_get_scroll_y (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    return self->scroll_y;
}

/* ==========================================================================
 * Event State
 * ========================================================================== */

/**
 * lrg_ui_event_get_consumed:
 * @self: an #LrgUIEvent
 *
 * Checks if the event has been consumed by a widget.
 *
 * Returns: %TRUE if consumed
 */
gboolean
lrg_ui_event_get_consumed (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->consumed;
}

/**
 * lrg_ui_event_set_consumed:
 * @self: an #LrgUIEvent
 * @consumed: whether the event is consumed
 *
 * Sets whether the event has been consumed.
 */
void
lrg_ui_event_set_consumed (LrgUIEvent *self,
                           gboolean    consumed)
{
    g_return_if_fail (self != NULL);

    self->consumed = consumed;
}

/**
 * lrg_ui_event_consume:
 * @self: an #LrgUIEvent
 *
 * Marks the event as consumed.
 */
void
lrg_ui_event_consume (LrgUIEvent *self)
{
    g_return_if_fail (self != NULL);

    self->consumed = TRUE;
}
