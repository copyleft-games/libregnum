/* lrg-canvas-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal canvas helpers shared within src/ui.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * _lrg_canvas_apply_pointer_transform:
 * @x: (inout): pointer X in window pixels, replaced by canvas units
 * @y: (inout): pointer Y in window pixels, replaced by canvas units
 *
 * Runs the process-wide pointer transform, if one is installed.
 */
void _lrg_canvas_apply_pointer_transform (gfloat *x,
                                          gfloat *y);

/*
 * Hooks for the UI inspector (lrg-ui-inspector.c). @self is an
 * LrgCanvas; the struct tag keeps this header free of lrg-canvas.h.
 *
 * _lrg_canvas_set_timing_enabled: measure step / layout / record
 *   durations with g_get_monotonic_time() (off by default).
 * _lrg_canvas_get_timings: the last measured durations in microseconds
 *   (record excludes layout).
 * _lrg_canvas_get_layers: appends the open popups (oldest first) and the
 *   overlay layer (bottom first) to the arrays, borrowed (no refs).
 */
struct _LrgCanvas;

void _lrg_canvas_set_timing_enabled (struct _LrgCanvas *self,
                                     gboolean           enabled);
void _lrg_canvas_get_timings        (struct _LrgCanvas *self,
                                     gint64            *step_us,
                                     gint64            *layout_us,
                                     gint64            *record_us);
void _lrg_canvas_get_layers         (struct _LrgCanvas *self,
                                     GPtrArray         *popups,
                                     GPtrArray         *overlays);

/*
 * _lrg_canvas_get_popup_anchor:
 *
 * The anchor @popup was shown with (open or closing), or NULL. Action
 * and direction lookups continue from it so a popup sees the scope of
 * the widget that opened it.
 */
struct _LrgWidget;

struct _LrgWidget * _lrg_canvas_get_popup_anchor (struct _LrgCanvas *self,
                                                  struct _LrgWidget *popup);

/*
 * _lrg_canvas_find_widget:
 *
 * lrg_widget_find() over the canvas tree, then its open popups and the
 * overlay layer (used by the built-in "ui" actions).
 */
struct _LrgWidget * _lrg_canvas_find_widget (struct _LrgCanvas *self,
                                             const gchar       *name);

/*
 * _lrg_canvas_focus_widget:
 *
 * Moves focus to @widget with the focus ring visible (keyboard style).
 */
void _lrg_canvas_focus_widget (struct _LrgCanvas *self,
                               struct _LrgWidget *widget);

/*
 * _lrg_canvas_emit_quit_requested:
 *
 * Emits LrgCanvas::quit-requested (the built-in "app.quit" action).
 */
void _lrg_canvas_emit_quit_requested (struct _LrgCanvas *self);

/*
 * Global key hooks (lrg-menu-bar.c). A widget that wants keys from
 * anywhere in the canvas (a menu bar's F10 and Alt+mnemonic) registers
 * a hook while it is in the canvas tree. Hooks run for KEY_DOWNs that
 * neither the focused widget nor a shortcut handled, oldest first,
 * until one returns TRUE; @key is a GrlKey and @mods LrgInputModifiers.
 * The canvas holds the owner weakly, skips it while hidden or disabled
 * and drops the hook once the owner left the canvas.
 */
typedef gboolean (*LrgCanvasKeyHook) (struct _LrgWidget *owner,
                                      gint               key,
                                      guint              mods);

void  _lrg_canvas_add_key_hook     (struct _LrgCanvas  *self,
                                    struct _LrgWidget  *owner,
                                    LrgCanvasKeyHook    func);
void  _lrg_canvas_remove_key_hook  (struct _LrgCanvas  *self,
                                    struct _LrgWidget  *owner);
guint _lrg_canvas_get_n_key_hooks  (struct _LrgCanvas  *self);

/*
 * Built-in action groups (lrg-ui-actions.c), installed by every canvas
 * under "app" and "ui". They hold a weak reference to @canvas.
 */
GActionGroup * _lrg_ui_actions_new_app_group (struct _LrgCanvas *canvas);
GActionGroup * _lrg_ui_actions_new_ui_group  (struct _LrgCanvas *canvas);

/*
 * Drag and drop session hooks (lrg-drag.c, lrg-drop-target.c). The
 * canvas runs the session; the controllers store their configuration
 * and emit their signals through these.
 *
 * _lrg_widget_peek_drag_sources / _lrg_widget_peek_drop_targets: the
 *   controllers attached to @widget (borrowed GPtrArray, or NULL).
 * _lrg_drag_source_prepare: emits ::prepare; returns a new reference
 *   to the payload or NULL.
 * _lrg_drag_source_begin / _end: set LrgDragSource:dragging and emit
 *   ::drag-begin / ::drag-end.
 * _lrg_drop_target_accept: emits ::accept (types were matched already).
 * _lrg_drop_target_enter: sets current-drop, emits ::enter.
 * _lrg_drop_target_motion: stores @proposed for the default handler,
 *   emits ::motion, clamps the result to the allowed actions and
 *   records it as current-action.
 * _lrg_drop_target_leave: emits ::leave, then clears current-drop.
 * _lrg_drop_target_drop: emits ::drop.
 */
struct _LrgDragSource;
struct _LrgDropTarget;
struct _LrgDragPayload;

GPtrArray *              _lrg_widget_peek_drag_sources  (struct _LrgWidget      *widget);
GPtrArray *              _lrg_widget_peek_drop_targets  (struct _LrgWidget      *widget);
struct _LrgDragPayload * _lrg_drag_source_prepare       (struct _LrgDragSource  *self,
                                                         gfloat                  x,
                                                         gfloat                  y);
void                     _lrg_drag_source_begin         (struct _LrgDragSource  *self,
                                                         struct _LrgDragPayload *payload);
void                     _lrg_drag_source_end           (struct _LrgDragSource  *self,
                                                         struct _LrgDragPayload *payload,
                                                         gboolean                dropped,
                                                         guint                   action);
gboolean                 _lrg_drop_target_accept        (struct _LrgDropTarget  *self,
                                                         struct _LrgDragPayload *payload);
void                     _lrg_drop_target_enter         (struct _LrgDropTarget  *self,
                                                         struct _LrgDragPayload *payload,
                                                         gfloat                  x,
                                                         gfloat                  y);
guint                    _lrg_drop_target_motion        (struct _LrgDropTarget  *self,
                                                         struct _LrgDragPayload *payload,
                                                         gfloat                  x,
                                                         gfloat                  y,
                                                         guint                   proposed);
void                     _lrg_drop_target_leave         (struct _LrgDropTarget  *self);
gboolean                 _lrg_drop_target_drop          (struct _LrgDropTarget  *self,
                                                         struct _LrgDragPayload *payload,
                                                         gfloat                  x,
                                                         gfloat                  y);

G_END_DECLS
