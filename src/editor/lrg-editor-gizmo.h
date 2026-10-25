/* lrg-editor-gizmo.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless transform gizmo controller.
 *
 * LrgEditorGizmo implements the interaction model of the translate, rotate
 * and scale gizmos for an #LrgEditor's current selection: it computes the
 * gizmo frame (pivot, axes, on-screen size), hit-tests the mouse against
 * the handles in viewport pixels, and turns a drag into new node
 * transforms.  While dragging, nodes are updated live; ending the drag
 * records a single undo step for the whole selection, and cancelling
 * (Escape / right click) restores the original transforms without touching
 * the history.
 *
 * Rendering is left to the host: it reads get_origin(), get_axis() and
 * get_size() and draws arrows / rings / boxes, highlighting
 * get_hovered() and get_active().  All math uses #LrgEditorView so the
 * controller is fully testable without a window.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-editor-math.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_GIZMO_SIZE_PIXELS:
 *
 * Length of a gizmo arm on screen, in pixels.
 */
#define LRG_EDITOR_GIZMO_SIZE_PIXELS (96.0f)

/**
 * LRG_EDITOR_GIZMO_PICK_PIXELS:
 *
 * Mouse distance, in pixels, within which a handle is considered hovered.
 */
#define LRG_EDITOR_GIZMO_PICK_PIXELS (8.0f)

#define LRG_TYPE_EDITOR_GIZMO (lrg_editor_gizmo_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorGizmo, lrg_editor_gizmo, LRG, EDITOR_GIZMO, GObject)

/**
 * lrg_editor_gizmo_new:
 * @editor: (transfer none): the editor whose selection, tool, space and
 *   snap settings drive the gizmo
 *
 * Returns: (transfer full): a new #LrgEditorGizmo
 */
LRG_AVAILABLE_IN_ALL
LrgEditorGizmo * lrg_editor_gizmo_new (LrgEditor *editor);

/**
 * lrg_editor_gizmo_is_visible:
 * @self: an #LrgEditorGizmo
 *
 * Returns: %TRUE when a manipulation tool is active and an unlocked node is
 *   selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_gizmo_is_visible (LrgEditorGizmo *self);

/**
 * lrg_editor_gizmo_get_origin:
 * @self: an #LrgEditorGizmo
 * @out: (out caller-allocates) (array fixed-size=3): the pivot in world space
 *
 * The pivot is the primary selection's world origin.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_get_origin (LrgEditorGizmo *self,
                                  gfloat         *out);

/**
 * lrg_editor_gizmo_get_axis:
 * @self: an #LrgEditorGizmo
 * @index: 0 = X, 1 = Y, 2 = Z
 * @out: (out caller-allocates) (array fixed-size=3): unit axis in world space
 *
 * World axes in world space; the primary node's rotated axes in local space.
 * The scale tool always uses local axes (scale is a local quantity).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_get_axis (LrgEditorGizmo *self,
                                guint           index,
                                gfloat         *out);

/**
 * lrg_editor_gizmo_get_size:
 * @self: an #LrgEditorGizmo
 * @view: the viewport view
 *
 * Returns: the world-space arm length that makes the gizmo
 *   %LRG_EDITOR_GIZMO_SIZE_PIXELS long on screen
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_gizmo_get_size (LrgEditorGizmo      *self,
                                  const LrgEditorView *view);

/**
 * lrg_editor_gizmo_hit_test:
 * @self: an #LrgEditorGizmo
 * @view: the viewport view
 * @x: mouse x in viewport pixels
 * @y: mouse y in viewport pixels
 *
 * Returns: the handle under the mouse, or %LRG_EDITOR_GIZMO_HANDLE_NONE
 */
LRG_AVAILABLE_IN_ALL
LrgEditorGizmoHandle lrg_editor_gizmo_hit_test (LrgEditorGizmo      *self,
                                                const LrgEditorView *view,
                                                gfloat               x,
                                                gfloat               y);

/**
 * lrg_editor_gizmo_update_hover:
 * @self: an #LrgEditorGizmo
 * @view: the viewport view
 * @x: mouse x in viewport pixels
 * @y: mouse y in viewport pixels
 *
 * Hit-tests and stores the result as the hovered handle (unchanged while
 * dragging).
 *
 * Returns: the hovered handle
 */
LRG_AVAILABLE_IN_ALL
LrgEditorGizmoHandle lrg_editor_gizmo_update_hover (LrgEditorGizmo      *self,
                                                    const LrgEditorView *view,
                                                    gfloat               x,
                                                    gfloat               y);

/**
 * lrg_editor_gizmo_get_hovered:
 * @self: an #LrgEditorGizmo
 *
 * Returns: the handle found by the last lrg_editor_gizmo_update_hover()
 */
LRG_AVAILABLE_IN_ALL
LrgEditorGizmoHandle lrg_editor_gizmo_get_hovered (LrgEditorGizmo *self);

/**
 * lrg_editor_gizmo_get_active:
 * @self: an #LrgEditorGizmo
 *
 * Returns: the handle being dragged, or %LRG_EDITOR_GIZMO_HANDLE_NONE
 */
LRG_AVAILABLE_IN_ALL
LrgEditorGizmoHandle lrg_editor_gizmo_get_active (LrgEditorGizmo *self);

/**
 * lrg_editor_gizmo_is_dragging:
 * @self: an #LrgEditorGizmo
 *
 * Returns: %TRUE between begin_drag() and end_drag()/cancel_drag()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_gizmo_is_dragging (LrgEditorGizmo *self);

/**
 * lrg_editor_gizmo_begin_drag:
 * @self: an #LrgEditorGizmo
 * @view: the viewport view
 * @x: mouse x in viewport pixels
 * @y: mouse y in viewport pixels
 *
 * Starts dragging the handle under the mouse.
 *
 * Returns: %TRUE if a handle was grabbed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_gizmo_begin_drag (LrgEditorGizmo      *self,
                                      const LrgEditorView *view,
                                      gfloat               x,
                                      gfloat               y);

/**
 * lrg_editor_gizmo_begin_drag_handle:
 * @self: an #LrgEditorGizmo
 * @view: the viewport view
 * @handle: the handle to drag (e.g. from a keyboard shortcut such as
 *   G then X)
 * @x: mouse x in viewport pixels
 * @y: mouse y in viewport pixels
 *
 * Starts dragging a specific handle regardless of what is under the mouse.
 *
 * Returns: %TRUE if a drag started
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_gizmo_begin_drag_handle (LrgEditorGizmo       *self,
                                             const LrgEditorView  *view,
                                             LrgEditorGizmoHandle  handle,
                                             gfloat                x,
                                             gfloat                y);

/**
 * lrg_editor_gizmo_drag:
 * @self: an #LrgEditorGizmo
 * @view: the viewport view
 * @x: mouse x in viewport pixels
 * @y: mouse y in viewport pixels
 * @invert_snap: %TRUE while the snap modifier (Ctrl) is held: toggles the
 *   editor's snap setting for this update
 *
 * Updates the selection transforms for the current mouse position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_drag (LrgEditorGizmo      *self,
                            const LrgEditorView *view,
                            gfloat               x,
                            gfloat               y,
                            gboolean             invert_snap);

/**
 * lrg_editor_gizmo_end_drag:
 * @self: an #LrgEditorGizmo
 *
 * Finishes the drag, recording one undo step if anything moved.
 *
 * Returns: %TRUE if a command was recorded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_gizmo_end_drag (LrgEditorGizmo *self);

/**
 * lrg_editor_gizmo_cancel_drag:
 * @self: an #LrgEditorGizmo
 *
 * Aborts the drag and restores every node's original transform.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_cancel_drag (LrgEditorGizmo *self);

/**
 * lrg_editor_gizmo_get_drag_delta:
 * @self: an #LrgEditorGizmo
 * @out: (out caller-allocates) (array fixed-size=3): current drag amount:
 *   translation in world units, rotation in radians (x only), or scale
 *   factors
 *
 * The current drag amount, for an on-screen readout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_get_drag_delta (LrgEditorGizmo *self,
                                      gfloat         *out);

G_END_DECLS
