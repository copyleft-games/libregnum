/* lrg-vr-service.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract interface for VR backend implementations.
 *
 * This interface provides a common API for VR functionality,
 * allowing for different backend implementations (OpenVR, OpenXR, stub).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_VR_SERVICE (lrg_vr_service_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgVRService, lrg_vr_service, LRG, VR_SERVICE, GObject)

/**
 * LrgVRServiceInterface:
 * @parent_iface: parent interface
 * @initialize: initializes the VR runtime
 * @shutdown: shuts down the VR runtime
 * @is_available: checks if VR is available
 * @is_hmd_present: checks if an HMD is connected
 * @poll_events: processes VR events
 * @get_recommended_render_size: gets recommended render target size
 * @get_eye_projection: gets projection matrix for an eye
 * @get_eye_to_head: gets eye-to-head transform matrix
 * @get_hmd_pose: gets current HMD pose
 * @get_controller_pose: gets controller pose
 * @get_controller_buttons: gets controller button state
 * @trigger_haptic: triggers haptic feedback
 * @submit_frame: submits rendered frame to compositor
 *
 * Interface structure for #LrgVRService.
 *
 * Implementors should provide all methods. The stub implementation
 * returns no-op/failure for all methods.
 */
struct _LrgVRServiceInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /* Lifecycle */
    gboolean (*initialize)      (LrgVRService  *self,
                                 GError       **error);
    void     (*shutdown)        (LrgVRService  *self);
    gboolean (*is_available)    (LrgVRService  *self);
    gboolean (*is_hmd_present)  (LrgVRService  *self);
    void     (*poll_events)     (LrgVRService  *self);

    /* Rendering */
    void     (*get_recommended_render_size) (LrgVRService *self,
                                             guint        *width,
                                             guint        *height);
    void     (*get_eye_projection) (LrgVRService *self,
                                    LrgVREye      eye,
                                    gfloat        near_clip,
                                    gfloat        far_clip,
                                    gfloat       *matrix);
    void     (*get_eye_to_head)    (LrgVRService *self,
                                    LrgVREye      eye,
                                    gfloat       *matrix);
    void     (*get_hmd_pose)       (LrgVRService *self,
                                    gfloat       *matrix);
    gboolean (*submit_frame)       (LrgVRService *self,
                                    LrgVREye      eye,
                                    guint         texture_id,
                                    GError      **error);

    /* Controllers */
    void     (*get_controller_pose)    (LrgVRService *self,
                                        LrgVRHand     hand,
                                        gfloat       *matrix);
    guint    (*get_controller_buttons) (LrgVRService *self,
                                        LrgVRHand     hand);
    gfloat   (*get_controller_axis)    (LrgVRService *self,
                                        LrgVRHand     hand,
                                        guint         axis);
    void     (*trigger_haptic)         (LrgVRService *self,
                                        LrgVRHand     hand,
                                        gfloat        duration,
                                        gfloat        amplitude);
};

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_vr_service_initialize:
 * @self: a #LrgVRService
 * @error: (optional): return location for error
 *
 * Initializes the VR runtime.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_initialize (LrgVRService  *self,
                                    GError       **error);

/**
 * lrg_vr_service_shutdown:
 * @self: a #LrgVRService
 *
 * Shuts down the VR runtime.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_shutdown (LrgVRService *self);

/**
 * lrg_vr_service_is_available:
 * @self: a #LrgVRService
 *
 * Checks if VR runtime is available.
 *
 * Returns: %TRUE if VR is available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_is_available (LrgVRService *self);

/**
 * lrg_vr_service_is_hmd_present:
 * @self: a #LrgVRService
 *
 * Checks if an HMD is connected.
 *
 * Returns: %TRUE if HMD is present
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_is_hmd_present (LrgVRService *self);

/**
 * lrg_vr_service_poll_events:
 * @self: a #LrgVRService
 *
 * Polls and processes VR events.
 *
 * Should be called each frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_poll_events (LrgVRService *self);

/**
 * lrg_vr_service_get_recommended_render_size:
 * @self: a #LrgVRService
 * @width: (out): render target width
 * @height: (out): render target height
 *
 * Gets the recommended render target size per eye.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_get_recommended_render_size (LrgVRService *self,
                                                  guint        *width,
                                                  guint        *height);

/**
 * lrg_vr_service_get_eye_projection:
 * @self: a #LrgVRService
 * @eye: which eye
 * @near_clip: near clipping plane
 * @far_clip: far clipping plane
 * @matrix: (out) (array fixed-size=16): 4x4 projection matrix
 *
 * Gets the projection matrix for the specified eye.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_get_eye_projection (LrgVRService *self,
                                        LrgVREye      eye,
                                        gfloat        near_clip,
                                        gfloat        far_clip,
                                        gfloat       *matrix);

/**
 * lrg_vr_service_get_eye_to_head:
 * @self: a #LrgVRService
 * @eye: which eye
 * @matrix: (out) (array fixed-size=16): 4x4 eye-to-head transform
 *
 * Gets the eye-to-head transform matrix.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_get_eye_to_head (LrgVRService *self,
                                     LrgVREye      eye,
                                     gfloat       *matrix);

/**
 * lrg_vr_service_get_hmd_pose:
 * @self: a #LrgVRService
 * @matrix: (out) (array fixed-size=16): 4x4 HMD pose matrix
 *
 * Gets the current HMD pose.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_get_hmd_pose (LrgVRService *self,
                                  gfloat       *matrix);

/**
 * lrg_vr_service_submit_frame:
 * @self: a #LrgVRService
 * @eye: which eye
 * @texture_id: OpenGL texture ID
 * @error: (optional): return location for error
 *
 * Submits a rendered frame to the VR compositor.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_submit_frame (LrgVRService *self,
                                      LrgVREye      eye,
                                      guint         texture_id,
                                      GError      **error);

/**
 * lrg_vr_service_get_controller_pose:
 * @self: a #LrgVRService
 * @hand: which hand
 * @matrix: (out) (array fixed-size=16): 4x4 controller pose
 *
 * Gets the pose for a motion controller.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_get_controller_pose (LrgVRService *self,
                                         LrgVRHand     hand,
                                         gfloat       *matrix);

/**
 * lrg_vr_service_get_controller_buttons:
 * @self: a #LrgVRService
 * @hand: which hand
 *
 * Gets the button state for a motion controller.
 *
 * Returns: #LrgVRControllerButton flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vr_service_get_controller_buttons (LrgVRService *self,
                                             LrgVRHand     hand);

/**
 * lrg_vr_service_get_controller_axis:
 * @self: a #LrgVRService
 * @hand: which hand
 * @axis: axis index (0=touchpad/thumbstick X, 1=Y, 2=trigger)
 *
 * Gets an axis value for a motion controller.
 *
 * Returns: axis value (-1.0 to 1.0, or 0.0 to 1.0 for trigger)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_service_get_controller_axis (LrgVRService *self,
                                           LrgVRHand     hand,
                                           guint         axis);

/**
 * lrg_vr_service_trigger_haptic:
 * @self: a #LrgVRService
 * @hand: which hand
 * @duration: duration in seconds
 * @amplitude: vibration amplitude (0.0 to 1.0)
 *
 * Triggers haptic feedback on a controller.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_trigger_haptic (LrgVRService *self,
                                    LrgVRHand     hand,
                                    gfloat        duration,
                                    gfloat        amplitude);

G_END_DECLS
