/* lrg-drag.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drag and drop, source side: the drag actions, the payload that
 * travels with the pointer (#LrgDragPayload) and the controller that
 * makes a widget draggable (#LrgDragSource). The drop side lives in
 * lrg-drop-target.h; the #LrgCanvas runs the session between them.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgDragAction
 * ========================================================================== */

/**
 * LrgDragAction:
 * @LRG_DRAG_ACTION_NONE: no action: the drop is refused
 * @LRG_DRAG_ACTION_COPY: the target receives a copy, the source keeps its item
 * @LRG_DRAG_ACTION_MOVE: the item leaves the source and goes to the target
 * @LRG_DRAG_ACTION_LINK: the target receives a reference to the item
 *   (an instance of a prefab, a shortcut, a node reference field)
 *
 * What a drop does with the dragged item. A payload lists the actions
 * its source allows, a drop target the actions it supports; the
 * session picks one action from the intersection (see the modifier
 * rules in lrg_drag_action_from_modifiers()).
 */
typedef enum /*< flags >*/
{
	LRG_DRAG_ACTION_NONE = 0,
	LRG_DRAG_ACTION_COPY = 1 << 0,
	LRG_DRAG_ACTION_MOVE = 1 << 1,
	LRG_DRAG_ACTION_LINK = 1 << 2
} LrgDragAction;

/**
 * LRG_DRAG_ACTION_ALL:
 *
 * Every drag action: COPY, MOVE and LINK.
 */
#define LRG_DRAG_ACTION_ALL ((LrgDragAction) (LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_MOVE | LRG_DRAG_ACTION_LINK))

LRG_AVAILABLE_IN_ALL
GType lrg_drag_action_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DRAG_ACTION (lrg_drag_action_get_type ())

/**
 * lrg_drag_action_from_modifiers:
 * @modifiers: modifier keys held during the drag
 * @allowed: actions both sides support
 * @preferred: the action to use without modifiers, or
 *   %LRG_DRAG_ACTION_NONE for the built-in order
 *
 * Picks the action of a drag from the held modifier keys, like a file
 * manager: Ctrl+Shift or Alt is LINK, Ctrl is COPY, Shift is MOVE.
 * Without modifiers (or when the requested action is not in @allowed)
 * the result is @preferred when @allowed has it, else the first of
 * MOVE, COPY and LINK that @allowed has.
 *
 * Returns: a single action out of @allowed, or %LRG_DRAG_ACTION_NONE
 *   when @allowed is empty
 */
LRG_AVAILABLE_IN_ALL
LrgDragAction lrg_drag_action_from_modifiers (LrgInputModifiers modifiers,
                                              LrgDragAction     allowed,
                                              LrgDragAction     preferred);

/* ==========================================================================
 * LrgDragPayload
 * ========================================================================== */

/**
 * LrgDragPayload:
 *
 * What a drag carries: a content type (a MIME-like id such as
 * "application/x-lrg-node" or "text/plain"), a #GValue with the data,
 * the widget the drag started from, the actions the source allows and
 * a preview (a label and an optional icon) the canvas shows under the
 * pointer. Reference counted; immutable in practice once the drag has
 * started (the setters exist for #LrgDragSource::prepare handlers that
 * build it up).
 */
typedef struct _LrgDragPayload LrgDragPayload;

#define LRG_TYPE_DRAG_PAYLOAD (lrg_drag_payload_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_drag_payload_get_type (void) G_GNUC_CONST;

/**
 * lrg_drag_payload_new:
 * @content_type: the content type, e.g. "application/x-lrg-asset"
 * @value: (nullable): the data, copied; %NULL for a payload that only
 *   carries its type
 * @actions: the actions the source allows
 *
 * Creates a payload. Set the preview with lrg_drag_payload_set_label()
 * and lrg_drag_payload_set_icon().
 *
 * Returns: (transfer full): a new #LrgDragPayload
 */
LRG_AVAILABLE_IN_ALL
LrgDragPayload * lrg_drag_payload_new (const gchar   *content_type,
                                       const GValue  *value,
                                       LrgDragAction  actions);

/**
 * lrg_drag_payload_new_string:
 * @content_type: the content type, e.g. "text/plain" or "application/x-lrg-asset"
 * @string: (nullable): the string to carry (copied), e.g. an asset path
 * @actions: the actions the source allows
 *
 * Creates a payload holding a string. The label defaults to @string.
 *
 * Returns: (transfer full): a new #LrgDragPayload
 */
LRG_AVAILABLE_IN_ALL
LrgDragPayload * lrg_drag_payload_new_string (const gchar   *content_type,
                                              const gchar   *string,
                                              LrgDragAction  actions);

/**
 * lrg_drag_payload_new_object:
 * @content_type: the content type, e.g. "application/x-lrg-node"
 * @object: (type GObject.Object) (nullable): the object to carry; the payload
 *   holds a reference
 * @actions: the actions the source allows
 *
 * Creates a payload holding a #GObject (a node, an item, a list row).
 *
 * Returns: (transfer full): a new #LrgDragPayload
 */
LRG_AVAILABLE_IN_ALL
LrgDragPayload * lrg_drag_payload_new_object (const gchar   *content_type,
                                              gpointer       object,
                                              LrgDragAction  actions);

/**
 * lrg_drag_payload_new_pointer: (skip)
 * @content_type: the content type
 * @pointer: (nullable): any pointer, e.g. an inventory slot struct
 * @destroy: (nullable): frees @pointer when the payload is finalized
 * @actions: the actions the source allows
 *
 * Creates a payload holding a raw pointer (C only). The value is a
 * %G_TYPE_POINTER #GValue; @destroy runs when the last reference goes.
 *
 * Returns: (transfer full): a new #LrgDragPayload
 */
LRG_AVAILABLE_IN_ALL
LrgDragPayload * lrg_drag_payload_new_pointer (const gchar    *content_type,
                                               gpointer        pointer,
                                               GDestroyNotify  destroy,
                                               LrgDragAction   actions);

/**
 * lrg_drag_payload_ref:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer full): @self with one more reference
 */
LRG_AVAILABLE_IN_ALL
LrgDragPayload * lrg_drag_payload_ref (LrgDragPayload *self);

/**
 * lrg_drag_payload_unref:
 * @self: (transfer full): an #LrgDragPayload
 *
 * Drops a reference; the last one frees the payload, its value and
 * the pointer of a lrg_drag_payload_new_pointer() payload.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_payload_unref (LrgDragPayload *self);

/**
 * lrg_drag_payload_get_content_type:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer none): the content type
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_drag_payload_get_content_type (LrgDragPayload *self);

/**
 * lrg_drag_payload_matches:
 * @self: an #LrgDragPayload
 * @pattern: a content type, "major/&ast;" for any subtype or "&ast;" for anything
 *
 * Whether the payload's content type matches @pattern. Matching is
 * exact (case sensitive) apart from the two wildcard forms.
 *
 * Returns: %TRUE on a match
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_drag_payload_matches (LrgDragPayload *self,
                                   const gchar    *pattern);

/**
 * lrg_drag_payload_get_value:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer none): the data; an unset (%G_TYPE_INVALID) value
 *   when the payload was created without one
 */
LRG_AVAILABLE_IN_ALL
const GValue * lrg_drag_payload_get_value (LrgDragPayload *self);

/**
 * lrg_drag_payload_get_string:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer none) (nullable): the string, or %NULL when the
 *   value does not hold a string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_drag_payload_get_string (LrgDragPayload *self);

/**
 * lrg_drag_payload_get_object:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer none) (nullable) (type GObject.Object): the
 *   object, or %NULL when the value does not hold one
 */
LRG_AVAILABLE_IN_ALL
gpointer lrg_drag_payload_get_object (LrgDragPayload *self);

/**
 * lrg_drag_payload_get_pointer:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer none) (nullable): the pointer of a
 *   lrg_drag_payload_new_pointer() payload, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gpointer lrg_drag_payload_get_pointer (LrgDragPayload *self);

/**
 * lrg_drag_payload_get_actions:
 * @self: an #LrgDragPayload
 *
 * Returns: the actions the source allows
 */
LRG_AVAILABLE_IN_ALL
LrgDragAction lrg_drag_payload_get_actions (LrgDragPayload *self);

/**
 * lrg_drag_payload_set_actions:
 * @self: an #LrgDragPayload
 * @actions: the actions the source allows
 *
 * Changes the allowed actions (only meaningful before the drag starts).
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_payload_set_actions (LrgDragPayload *self,
                                   LrgDragAction   actions);

/**
 * lrg_drag_payload_get_source:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer none) (nullable): the widget the drag started
 *   from, or %NULL once it was destroyed (held weakly)
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_drag_payload_get_source (LrgDragPayload *self);

/**
 * lrg_drag_payload_set_source:
 * @self: an #LrgDragPayload
 * @source: (nullable): the widget the drag starts from
 *
 * Records the source widget (weakly). The canvas fills it in when a
 * drag starts from an #LrgDragSource and the payload has none yet.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_payload_set_source (LrgDragPayload *self,
                                  LrgWidget      *source);

/**
 * lrg_drag_payload_get_label:
 * @self: an #LrgDragPayload
 *
 * Returns: (transfer none) (nullable): the preview label
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_drag_payload_get_label (LrgDragPayload *self);

/**
 * lrg_drag_payload_set_label:
 * @self: an #LrgDragPayload
 * @label: (nullable): text of the preview chip, e.g. "Player" or "3 nodes"
 *
 * Sets the preview label. Without one the chip shows the content type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_payload_set_label (LrgDragPayload *self,
                                 const gchar    *label);

/**
 * lrg_drag_payload_get_icon:
 * @self: an #LrgDragPayload
 *
 * Returns: the preview icon, %LRG_ICON_NONE for none
 */
LRG_AVAILABLE_IN_ALL
LrgIcon lrg_drag_payload_get_icon (LrgDragPayload *self);

/**
 * lrg_drag_payload_set_icon:
 * @self: an #LrgDragPayload
 * @icon: icon drawn before the label, %LRG_ICON_NONE for none
 *
 * Sets the preview icon.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_payload_set_icon (LrgDragPayload *self,
                                LrgIcon         icon);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgDragPayload, lrg_drag_payload_unref)

/* ==========================================================================
 * LrgDragSource
 * ========================================================================== */

#define LRG_TYPE_DRAG_SOURCE (lrg_drag_source_get_type ())

/**
 * LrgDragSource:
 *
 * A controller that makes a widget draggable. Attach it with
 * lrg_widget_add_drag_source(). When a press on the widget (or on a
 * child of it) moves past #LrgDragSource:threshold, the canvas emits
 * #LrgDragSource::prepare; a non-%NULL payload starts the drag.
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDragSource, lrg_drag_source, LRG, DRAG_SOURCE, GObject)

/**
 * LRG_DRAG_DEFAULT_THRESHOLD:
 *
 * Default distance in canvas units the pointer has to travel with the
 * button held before a press becomes a drag.
 */
#define LRG_DRAG_DEFAULT_THRESHOLD (4.0f)

/**
 * lrg_drag_source_new:
 *
 * Creates a drag source for the left button with the default
 * threshold. Connect #LrgDragSource::prepare (or set a fixed payload
 * with lrg_drag_source_set_payload()) and attach it with
 * lrg_widget_add_drag_source().
 *
 * Returns: (transfer full): a new #LrgDragSource
 */
LRG_AVAILABLE_IN_ALL
LrgDragSource * lrg_drag_source_new (void);

/**
 * lrg_drag_source_get_threshold:
 * @self: an #LrgDragSource
 *
 * Returns: the drag threshold in canvas units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_drag_source_get_threshold (LrgDragSource *self);

/**
 * lrg_drag_source_set_threshold:
 * @self: an #LrgDragSource
 * @threshold: distance the pointer must move with the button held
 *   before the drag starts (0 starts on the first move)
 *
 * Sets the drag threshold.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_source_set_threshold (LrgDragSource *self,
                                    gfloat         threshold);

/**
 * lrg_drag_source_get_button:
 * @self: an #LrgDragSource
 *
 * Returns: the mouse button that starts drags
 */
LRG_AVAILABLE_IN_ALL
GrlMouseButton lrg_drag_source_get_button (LrgDragSource *self);

/**
 * lrg_drag_source_set_button:
 * @self: an #LrgDragSource
 * @button: the mouse button that starts drags (%GRL_MOUSE_BUTTON_LEFT,
 *   %GRL_MOUSE_BUTTON_RIGHT or %GRL_MOUSE_BUTTON_MIDDLE)
 *
 * Sets the button that starts drags.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_source_set_button (LrgDragSource  *self,
                                 GrlMouseButton  button);

/**
 * lrg_drag_source_get_payload:
 * @self: an #LrgDragSource
 *
 * Returns: (transfer none) (nullable): the fixed payload, if one is set
 */
LRG_AVAILABLE_IN_ALL
LrgDragPayload * lrg_drag_source_get_payload (LrgDragSource *self);

/**
 * lrg_drag_source_set_payload:
 * @self: an #LrgDragSource
 * @payload: (nullable): a payload every drag from this source carries
 *
 * Sets a fixed payload. #LrgDragSource::prepare returns it when no
 * handler returned one of its own, so simple sources need no handler.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drag_source_set_payload (LrgDragSource  *self,
                                  LrgDragPayload *payload);

/**
 * lrg_drag_source_get_widget:
 * @self: an #LrgDragSource
 *
 * Returns: (transfer none) (nullable): the widget the source is
 *   attached to
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_drag_source_get_widget (LrgDragSource *self);

/**
 * lrg_drag_source_get_dragging:
 * @self: an #LrgDragSource
 *
 * Returns: %TRUE between #LrgDragSource::drag-begin and
 *   #LrgDragSource::drag-end
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_drag_source_get_dragging (LrgDragSource *self);

/* ==========================================================================
 * Attaching to widgets
 * ========================================================================== */

/**
 * lrg_widget_add_drag_source:
 * @self: an #LrgWidget
 * @source: (transfer full): a drag source not attached anywhere yet
 *
 * Attaches @source to @self; the widget owns it from now on. A widget
 * can have several sources (e.g. one per button). Presses on any
 * descendant of @self arm it too, so a row's label does not need its
 * own source.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_add_drag_source (LrgWidget     *self,
                                 LrgDragSource *source);

/**
 * lrg_widget_remove_drag_source:
 * @self: an #LrgWidget
 * @source: a drag source attached to @self
 *
 * Detaches @source and drops the widget's reference to it. A drag in
 * progress from @source keeps running to its end.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_remove_drag_source (LrgWidget     *self,
                                    LrgDragSource *source);

/**
 * lrg_widget_list_drag_sources:
 * @self: an #LrgWidget
 *
 * Returns: (transfer container) (element-type LrgDragSource): the drag
 *   sources attached to @self, in the order they were added
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_widget_list_drag_sources (LrgWidget *self);

G_END_DECLS
