/* lrg-drop-target.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drag and drop, drop side: the controller that lets a widget receive
 * dragged payloads (#LrgDropTarget). See lrg-drag.h for the source
 * side; the #LrgCanvas runs the session between them.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"
#include "lrg-drag.h"

G_BEGIN_DECLS

#define LRG_TYPE_DROP_TARGET (lrg_drop_target_get_type ())

/**
 * LrgDropTarget:
 *
 * A controller that lets a widget receive drops. Attach it with
 * lrg_widget_add_drop_target(). While a drag is over the widget (or a
 * child of it that has no matching target of its own) the canvas emits
 * #LrgDropTarget::accept once, then #LrgDropTarget::enter,
 * #LrgDropTarget::motion on every move, and finally either
 * #LrgDropTarget::drop followed by #LrgDropTarget::leave, or just
 * #LrgDropTarget::leave. Coordinates are canvas (world) coordinates,
 * like lrg_ui_event_get_x().
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDropTarget, lrg_drop_target, LRG, DROP_TARGET, GObject)

/**
 * lrg_drop_target_new:
 * @types: (nullable) (array zero-terminated=1): content types the target
 *   takes ("major/&ast;" and "&ast;" are wildcards); %NULL takes anything
 * @actions: actions the target supports
 *
 * Creates a drop target.
 *
 * Returns: (transfer full): a new #LrgDropTarget
 */
LRG_AVAILABLE_IN_ALL
LrgDropTarget * lrg_drop_target_new (const gchar * const *types,
                                     LrgDragAction        actions);

/**
 * lrg_drop_target_get_types:
 * @self: an #LrgDropTarget
 *
 * Returns: (transfer none) (array zero-terminated=1) (nullable): the
 *   accepted content types, %NULL for any
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_drop_target_get_types (LrgDropTarget *self);

/**
 * lrg_drop_target_set_types:
 * @self: an #LrgDropTarget
 * @types: (nullable) (array zero-terminated=1): accepted content types,
 *   %NULL for any
 *
 * Replaces the accepted content types.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drop_target_set_types (LrgDropTarget       *self,
                                const gchar * const *types);

/**
 * lrg_drop_target_get_actions:
 * @self: an #LrgDropTarget
 *
 * Returns: the supported actions
 */
LRG_AVAILABLE_IN_ALL
LrgDragAction lrg_drop_target_get_actions (LrgDropTarget *self);

/**
 * lrg_drop_target_set_actions:
 * @self: an #LrgDropTarget
 * @actions: the supported actions
 *
 * Sets the supported actions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drop_target_set_actions (LrgDropTarget *self,
                                  LrgDragAction  actions);

/**
 * lrg_drop_target_get_preferred_action:
 * @self: an #LrgDropTarget
 *
 * Returns: the action used without modifier keys
 */
LRG_AVAILABLE_IN_ALL
LrgDragAction lrg_drop_target_get_preferred_action (LrgDropTarget *self);

/**
 * lrg_drop_target_set_preferred_action:
 * @self: an #LrgDropTarget
 * @action: the action used when no modifier key is held (a single
 *   action), or %LRG_DRAG_ACTION_NONE for the built-in order (MOVE,
 *   COPY, LINK)
 *
 * Sets the default action, e.g. LINK for an object-reference field so
 * a plain drop links instead of moving.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drop_target_set_preferred_action (LrgDropTarget *self,
                                           LrgDragAction  action);

/**
 * lrg_drop_target_get_paint_highlight:
 * @self: an #LrgDropTarget
 *
 * Returns: whether the canvas paints the default highlight
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_drop_target_get_paint_highlight (LrgDropTarget *self);

/**
 * lrg_drop_target_set_paint_highlight:
 * @self: an #LrgDropTarget
 * @paint_highlight: %FALSE when the widget paints its own feedback
 *   (from #LrgDropTarget:current-drop)
 *
 * Whether the canvas outlines the widget in the accent colour while a
 * drop onto it would be accepted. Default %TRUE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drop_target_set_paint_highlight (LrgDropTarget *self,
                                          gboolean       paint_highlight);

/**
 * lrg_drop_target_get_current_drop:
 * @self: an #LrgDropTarget
 *
 * The payload being dragged over the target, between
 * #LrgDropTarget::enter and #LrgDropTarget::leave. Widgets read it in
 * paint() to draw their own highlight.
 *
 * Returns: (transfer none) (nullable): the hovering payload, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgDragPayload * lrg_drop_target_get_current_drop (LrgDropTarget *self);

/**
 * lrg_drop_target_get_current_action:
 * @self: an #LrgDropTarget
 *
 * Returns: the action the last #LrgDropTarget::motion settled on while
 *   a payload hovers, %LRG_DRAG_ACTION_NONE otherwise
 */
LRG_AVAILABLE_IN_ALL
LrgDragAction lrg_drop_target_get_current_action (LrgDropTarget *self);

/**
 * lrg_drop_target_get_proposed_action:
 * @self: an #LrgDropTarget
 *
 * The action the session proposes for the current move, worked out
 * from the held modifier keys, #LrgDropTarget:preferred-action and the
 * actions both sides allow (see lrg_drag_action_from_modifiers()).
 * A #LrgDropTarget::motion handler returns it when it has no reason to
 * pick something else.
 *
 * Returns: the proposed action, %LRG_DRAG_ACTION_NONE outside a drag
 */
LRG_AVAILABLE_IN_ALL
LrgDragAction lrg_drop_target_get_proposed_action (LrgDropTarget *self);

/**
 * lrg_drop_target_get_widget:
 * @self: an #LrgDropTarget
 *
 * Returns: (transfer none) (nullable): the widget the target is
 *   attached to
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_drop_target_get_widget (LrgDropTarget *self);

/**
 * lrg_drop_target_matches:
 * @self: an #LrgDropTarget
 * @payload: a payload
 *
 * Whether @payload's content type is one of the accepted types and
 * the payload and target share at least one action. It does not emit
 * #LrgDropTarget::accept.
 *
 * Returns: %TRUE when @payload could be dropped here
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_drop_target_matches (LrgDropTarget  *self,
                                  LrgDragPayload *payload);

/* ==========================================================================
 * Attaching to widgets
 * ========================================================================== */

/**
 * lrg_widget_add_drop_target:
 * @self: an #LrgWidget
 * @target: (transfer full): a drop target not attached anywhere yet
 *
 * Attaches @target to @self; the widget owns it from now on. A drag
 * over a child of @self reaches @target unless a closer ancestor (or
 * the child) has a matching target that accepts first.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_add_drop_target (LrgWidget     *self,
                                 LrgDropTarget *target);

/**
 * lrg_widget_remove_drop_target:
 * @self: an #LrgWidget
 * @target: a drop target attached to @self
 *
 * Detaches @target. A drag hovering it gets #LrgDropTarget::leave at
 * its next move.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_remove_drop_target (LrgWidget     *self,
                                    LrgDropTarget *target);

/**
 * lrg_widget_list_drop_targets:
 * @self: an #LrgWidget
 *
 * Returns: (transfer container) (element-type LrgDropTarget): the drop
 *   targets attached to @self, in the order they were added
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_widget_list_drop_targets (LrgWidget *self);

G_END_DECLS
