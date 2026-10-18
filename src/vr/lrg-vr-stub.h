/* lrg-vr-stub.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Stub VR implementation for when no VR runtime is available.
 *
 * This provides a no-op implementation of #LrgVRService that
 * returns appropriate failure states for all operations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-vr-service.h"

G_BEGIN_DECLS

#define LRG_TYPE_VR_STUB (lrg_vr_stub_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVRStub, lrg_vr_stub, LRG, VR_STUB, GObject)

/**
 * lrg_vr_stub_new:
 *
 * Creates a new VR stub instance.
 *
 * This is a no-op implementation that returns failure for all
 * VR operations. Use this as a fallback when no VR runtime is
 * available.
 *
 * Returns: (transfer full): a new #LrgVRStub
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRStub * lrg_vr_stub_new (void);

/**
 * lrg_vr_stub_get_default:
 *
 * Gets the default VR stub instance.
 *
 * Returns: (transfer none): the default #LrgVRStub
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRStub * lrg_vr_stub_get_default (void);

/**
 * lrg_vr_stub_new_simulated:
 *
 * Creates a simulated headset: a stub that initialises, reports an HMD,
 * runs the runtime-paced frame loop (two views, framebuffer 0, nothing
 * bound) and returns the poses, buttons and axes the caller sets with
 * the setters below.  Headless tests drive VR code through it exactly
 * as through a real runtime.  The eyes sit at +-IPD/2 along the head's
 * X axis; the projection comes from the field of view
 * (lrg_vr_stub_set_fov()), 45 degrees each way by default.
 *
 * Returns: (transfer full): a new simulated #LrgVRStub
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRStub * lrg_vr_stub_new_simulated (void);

/**
 * lrg_vr_stub_get_simulated:
 * @self: a #LrgVRStub
 *
 * Returns: %TRUE for a stub made by lrg_vr_stub_new_simulated()
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_stub_get_simulated (LrgVRStub *self);

/**
 * lrg_vr_stub_set_hmd_pose:
 * @self: a simulated #LrgVRStub
 * @matrix: (array fixed-size=16): the head pose in tracking space
 *   (column-major, metres)
 *
 * Sets the pose lrg_vr_service_get_hmd_pose() reports and the views of
 * the frame loop derive from.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_set_hmd_pose (LrgVRStub    *self,
                               const gfloat *matrix);

/**
 * lrg_vr_stub_set_ipd:
 * @self: a simulated #LrgVRStub
 * @ipd: the distance between the eyes, metres (default 0.064)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_set_ipd (LrgVRStub *self,
                          gfloat     ipd);

/**
 * lrg_vr_stub_get_ipd:
 * @self: a #LrgVRStub
 *
 * Returns: the distance between the eyes, metres
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_stub_get_ipd (LrgVRStub *self);

/**
 * lrg_vr_stub_set_fov:
 * @self: a simulated #LrgVRStub
 * @left: the left edge's angle, degrees (negative: to the left)
 * @right: the right edge's angle, degrees
 * @up: the top edge's angle, degrees
 * @down: the bottom edge's angle, degrees (negative: down)
 *
 * The field of view of both eyes, as an OpenXR XrFovf in degrees.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_set_fov (LrgVRStub *self,
                          gfloat     left,
                          gfloat     right,
                          gfloat     up,
                          gfloat     down);

/**
 * lrg_vr_stub_set_render_size:
 * @self: a simulated #LrgVRStub
 * @width: eye image width in pixels
 * @height: eye image height in pixels
 *
 * The size lrg_vr_service_get_recommended_render_size() and the views
 * report (default 1024 x 1024).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_set_render_size (LrgVRStub *self,
                                  guint      width,
                                  guint      height);

/**
 * lrg_vr_stub_set_controller:
 * @self: a simulated #LrgVRStub
 * @hand: which hand
 * @matrix: (array fixed-size=16) (nullable): the controller's aim pose in
 *   tracking space, %NULL for identity
 * @tracked: whether the pose is valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_set_controller (LrgVRStub    *self,
                                 LrgVRHand     hand,
                                 const gfloat *matrix,
                                 gboolean      tracked);

/**
 * lrg_vr_stub_set_controller_buttons:
 * @self: a simulated #LrgVRStub
 * @hand: which hand
 * @buttons: #LrgVRControllerButton flags held down
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_set_controller_buttons (LrgVRStub *self,
                                         LrgVRHand  hand,
                                         guint      buttons);

/**
 * lrg_vr_stub_set_controller_axis:
 * @self: a simulated #LrgVRStub
 * @hand: which hand
 * @axis: an axis index (%LRG_VR_AXIS_THUMBSTICK_X .. %LRG_VR_AXIS_GRIP)
 * @value: the axis value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_set_controller_axis (LrgVRStub *self,
                                      LrgVRHand  hand,
                                      guint      axis,
                                      gfloat     value);

/**
 * lrg_vr_stub_get_haptic_count:
 * @self: a #LrgVRStub
 * @hand: which hand
 *
 * Returns: how many haptic pulses lrg_vr_service_trigger_haptic() asked
 *   of @hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vr_stub_get_haptic_count (LrgVRStub *self,
                                    LrgVRHand  hand);

/**
 * lrg_vr_stub_get_frame_count:
 * @self: a #LrgVRStub
 *
 * Returns: how many frames lrg_vr_service_end_frame() submitted
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vr_stub_get_frame_count (LrgVRStub *self);

/**
 * lrg_vr_stub_request_quit:
 * @self: a simulated #LrgVRStub
 *
 * Makes lrg_vr_service_should_quit() report %TRUE, as a runtime does when
 * the user quits from its dashboard; the session stops running.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_stub_request_quit (LrgVRStub *self);

G_END_DECLS
