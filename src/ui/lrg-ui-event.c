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
    gunichar       codepoint;   /* TEXT_INPUT: the Unicode character */
    gboolean       consumed;
    gboolean       bubbling;    /* set while travelling up from the target */
    LrgWidget     *target;      /* widget the event was aimed at (unowned) */
    LrgInputModifiers modifiers;
    gfloat         dx;          /* PAN: movement; PAN_END: velocity */
    gfloat         dy;
    gfloat         scale;       /* ZOOM: factor since the last ZOOM */
};

G_DEFINE_BOXED_TYPE (LrgUIEvent, lrg_ui_event,
                     lrg_ui_event_copy, lrg_ui_event_free)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/*
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
    self->codepoint = 0;
    self->consumed = FALSE;

    return self;
}

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
 * lrg_ui_event_new_text_input:
 * @codepoint: the Unicode character that was typed
 *
 * Creates a text-input event carrying one already-composed character
 * (keyboard layout, Shift and dead keys applied). Text widgets insert
 * this instead of translating raw key codes.
 *
 * Returns: (transfer full): A new #LrgUIEvent
 */
LrgUIEvent *
lrg_ui_event_new_text_input (gunichar codepoint)
{
    LrgUIEvent *self;

    self = lrg_ui_event_new (LRG_UI_EVENT_TEXT_INPUT);
    self->codepoint = codepoint;

    return self;
}

/*
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

    /* plain value struct: a field-wise copy keeps every member,
     * including ones added after this function was written */
    copy = g_slice_new (LrgUIEvent);
    *copy = *self;

    return copy;
}

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
 * lrg_ui_event_get_codepoint:
 * @self: an #LrgUIEvent
 *
 * Gets the character carried by a %LRG_UI_EVENT_TEXT_INPUT event.
 *
 * Returns: The Unicode character, or 0 for other event types
 */
gunichar
lrg_ui_event_get_codepoint (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->codepoint;
}

/*
 * lrg_ui_event_get_bubbling:
 * @self: an #LrgUIEvent
 *
 * Returns: %TRUE while the canvas is bubbling the event up the tree
 */
gboolean
lrg_ui_event_get_bubbling (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->bubbling;
}

/*
 * lrg_ui_event_set_bubbling:
 * @self: an #LrgUIEvent
 * @bubbling: whether the event is bubbling
 */
void
lrg_ui_event_set_bubbling (LrgUIEvent *self,
                           gboolean    bubbling)
{
    g_return_if_fail (self != NULL);

    self->bubbling = bubbling;
}

/*
 * lrg_ui_event_get_target:
 * @self: an #LrgUIEvent
 *
 * Returns: (transfer none) (nullable): the widget the event was
 *   originally delivered to
 */
LrgWidget *
lrg_ui_event_get_target (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->target;
}

/*
 * lrg_ui_event_set_target:
 * @self: an #LrgUIEvent
 * @target: (nullable): the original target
 */
void
lrg_ui_event_set_target (LrgUIEvent *self,
                         LrgWidget  *target)
{
    g_return_if_fail (self != NULL);

    self->target = target;
}

/*
 * lrg_ui_event_get_modifiers:
 * @self: an #LrgUIEvent
 *
 * Returns: modifier keys held when the event happened
 */
LrgInputModifiers
lrg_ui_event_get_modifiers (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, LRG_INPUT_MODIFIER_NONE);

    return self->modifiers;
}

/*
 * lrg_ui_event_set_modifiers:
 * @self: an #LrgUIEvent
 * @modifiers: modifier keys
 */
void
lrg_ui_event_set_modifiers (LrgUIEvent        *self,
                            LrgInputModifiers  modifiers)
{
    g_return_if_fail (self != NULL);

    self->modifiers = modifiers;
}

/*
 * lrg_ui_event_new_pan:
 *
 * See the header.
 */
LrgUIEvent *
lrg_ui_event_new_pan (gfloat x,
                      gfloat y,
                      gfloat dx,
                      gfloat dy)
{
    LrgUIEvent *self = lrg_ui_event_new (LRG_UI_EVENT_PAN);

    self->x = x;
    self->y = y;
    self->dx = dx;
    self->dy = dy;
    return self;
}

LrgUIEvent *
lrg_ui_event_new_pan_end (gfloat x,
                          gfloat y,
                          gfloat velocity_x,
                          gfloat velocity_y)
{
    LrgUIEvent *self = lrg_ui_event_new (LRG_UI_EVENT_PAN_END);

    self->x = x;
    self->y = y;
    self->dx = velocity_x;
    self->dy = velocity_y;
    return self;
}

LrgUIEvent *
lrg_ui_event_new_zoom (gfloat x,
                       gfloat y,
                       gfloat scale)
{
    LrgUIEvent *self = lrg_ui_event_new (LRG_UI_EVENT_ZOOM);

    self->x = x;
    self->y = y;
    self->scale = scale;
    return self;
}

gfloat
lrg_ui_event_get_delta_x (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->type == LRG_UI_EVENT_PAN ? self->dx : 0.0f;
}

gfloat
lrg_ui_event_get_delta_y (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->type == LRG_UI_EVENT_PAN ? self->dy : 0.0f;
}

gfloat
lrg_ui_event_get_velocity_x (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->type == LRG_UI_EVENT_PAN_END ? self->dx : 0.0f;
}

gfloat
lrg_ui_event_get_velocity_y (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->type == LRG_UI_EVENT_PAN_END ? self->dy : 0.0f;
}

gfloat
lrg_ui_event_get_scale (const LrgUIEvent *self)
{
    g_return_val_if_fail (self != NULL, 1.0f);
    return self->type == LRG_UI_EVENT_ZOOM ? self->scale : 1.0f;
}
