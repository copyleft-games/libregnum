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

G_END_DECLS
