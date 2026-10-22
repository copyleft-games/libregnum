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
#include "lrg-vr-hand.h"

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
 * @begin_frame: starts a runtime-paced frame; %TRUE when views must render
 * @get_view_count: number of views (2 for stereo)
 * @begin_view: binds a view's render target, reports its pose + projection
 * @end_view: releases a view's render target
 * @end_frame: submits the frame begun by @begin_frame
 * @is_session_running: whether frames are being accepted
 * @should_quit: whether the runtime asked the application to exit
 * @is_controller_tracked: whether a hand's controller pose is valid
 * @is_hand_tracking_supported: whether articulated hands can be tracked
 * @get_hand_state: fills a hand's #LrgVRHandState for the current frame
 *
 * Interface structure for #LrgVRService.
 *
 * Implementors should provide all methods. The stub implementation
 * returns no-op/failure for all methods.
 *
 * Matrices are 4x4, column-major (OpenGL order: element [12..14] is the
 * translation).  Poses are in the runtime's tracking space, in metres.
 *
 * The frame-loop methods (@begin_frame .. @end_frame) serve runtimes that
 * pace frames themselves (OpenXR): call begin_frame, then for each view
 * begin_view / draw into the bound framebuffer / end_view, then end_frame.
 * They supersede @submit_frame for such runtimes.
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

    /* Runtime-paced frame loop */
    gboolean (*begin_frame)        (LrgVRService *self);
    guint    (*get_view_count)     (LrgVRService *self);
    gboolean (*begin_view)         (LrgVRService *self,
                                    guint         view,
                                    gfloat        near_clip,
                                    gfloat        far_clip,
                                    gfloat       *eye_pose,
                                    gfloat       *projection,
                                    guint        *framebuffer,
                                    guint        *width,
                                    guint        *height);
    void     (*end_view)           (LrgVRService *self,
                                    guint         view);
    void     (*end_frame)          (LrgVRService *self);
    gboolean (*is_session_running) (LrgVRService *self);
    gboolean (*should_quit)        (LrgVRService *self);
    gboolean (*is_controller_tracked) (LrgVRService *self,
                                       LrgVRHand     hand);

    /* Articulated hands (optional; NULL means no hand tracking) */
    gboolean (*is_hand_tracking_supported) (LrgVRService *self);
    gboolean (*get_hand_state)             (LrgVRService   *self,
                                            LrgVRHand       hand,
                                            LrgVRHandState *state);
};

/**
 * LRG_VR_AXIS_THUMBSTICK_X:
 *
 * Axis index for lrg_vr_service_get_controller_axis(): thumbstick X, -1..1.
 */
#define LRG_VR_AXIS_THUMBSTICK_X 0
/**
 * LRG_VR_AXIS_THUMBSTICK_Y:
 *
 * Axis index: thumbstick Y, -1 (down) .. 1 (up).
 */
#define LRG_VR_AXIS_THUMBSTICK_Y 1
/**
 * LRG_VR_AXIS_TRIGGER:
 *
 * Axis index: trigger pull, 0..1.
 */
#define LRG_VR_AXIS_TRIGGER      2
/**
 * LRG_VR_AXIS_GRIP:
 *
 * Axis index: grip squeeze, 0..1.
 */
#define LRG_VR_AXIS_GRIP         3

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_vr_service_initialize:
 * @self: a #LrgVRService
 * @error: return location for error
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
 * @error: return location for error
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

/**
 * lrg_vr_service_begin_frame:
 * @self: a #LrgVRService
 *
 * Starts a runtime-paced frame.  For OpenXR this waits for the runtime's
 * frame timing (xrWaitFrame) and begins the frame, so it blocks until the
 * runtime wants the next frame -- unless the implementation waits on a
 * thread of its own (lrg_openxr_service_start_frame_thread()), in which case
 * it returns %FALSE at once when no frame is due.  Always pair with
 * lrg_vr_service_end_frame() when it returns %TRUE, and render the views
 * lrg_vr_service_get_view_count() then reports (none, when the runtime does
 * not want this frame drawn).
 *
 * Returns: %TRUE if a frame was begun
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_begin_frame (LrgVRService *self);

/**
 * lrg_vr_service_get_view_count:
 * @self: a #LrgVRService
 *
 * Returns: the number of views to render (2 for a stereo HMD, 0 if none)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vr_service_get_view_count (LrgVRService *self);

/**
 * lrg_vr_service_begin_view:
 * @self: a #LrgVRService
 * @view: view index, below lrg_vr_service_get_view_count()
 * @near_clip: near clip distance, in metres
 * @far_clip: far clip distance, in metres
 * @eye_pose: (out) (array fixed-size=16): the eye's pose in tracking space
 * @projection: (out) (array fixed-size=16): the view's projection matrix
 * @framebuffer: (out): GL framebuffer to draw into (already bound)
 * @width: (out): framebuffer width in pixels
 * @height: (out): framebuffer height in pixels
 *
 * Acquires and binds the render target for @view.
 *
 * Returns: %TRUE if the view's target was acquired
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_begin_view (LrgVRService *self,
                                    guint         view,
                                    gfloat        near_clip,
                                    gfloat        far_clip,
                                    gfloat       *eye_pose,
                                    gfloat       *projection,
                                    guint        *framebuffer,
                                    guint        *width,
                                    guint        *height);

/**
 * lrg_vr_service_end_view:
 * @self: a #LrgVRService
 * @view: the view begun by lrg_vr_service_begin_view()
 *
 * Releases @view's render target and unbinds its framebuffer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_end_view (LrgVRService *self,
                              guint         view);

/**
 * lrg_vr_service_end_frame:
 * @self: a #LrgVRService
 *
 * Submits the frame begun by lrg_vr_service_begin_frame().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_service_end_frame (LrgVRService *self);

/**
 * lrg_vr_service_is_session_running:
 * @self: a #LrgVRService
 *
 * Returns: %TRUE while the runtime accepts frames
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_is_session_running (LrgVRService *self);

/**
 * lrg_vr_service_should_quit:
 * @self: a #LrgVRService
 *
 * Returns: %TRUE once the runtime has asked the application to exit (the
 *   user quit it from the VR dashboard, or the runtime is shutting down)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_should_quit (LrgVRService *self);

/**
 * lrg_vr_service_is_controller_tracked:
 * @self: a #LrgVRService
 * @hand: which hand
 *
 * Returns: %TRUE when @hand's controller pose is currently valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_is_controller_tracked (LrgVRService *self,
                                               LrgVRHand     hand);

/**
 * lrg_vr_service_is_hand_tracking_supported:
 * @self: a #LrgVRService
 *
 * Whether this backend can track articulated hands: for OpenXR, the
 * runtime offers XR_EXT_hand_tracking and the system supports it.  A
 * supported backend still reports a hand inactive while it is not seen
 * (or is holding a controller the runtime prefers to track).
 *
 * Returns: %TRUE when lrg_vr_service_get_hand_state() can report hands
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_is_hand_tracking_supported (LrgVRService *self);

/**
 * lrg_vr_service_get_hand_state:
 * @self: a #LrgVRService
 * @hand: which hand
 * @state: (out caller-allocates): the hand's state this frame
 *
 * Copies @hand's tracking state as of the current frame: joints located
 * at the frame's predicted display time, the aim ray, and the pinch with
 * its hysteresis already applied.  Cheap -- a copy of state the backend
 * gathered when the frame began -- so call it as often as needed.
 *
 * Returns: %TRUE when @hand is tracked this frame (@state is reset to an
 *   inactive hand otherwise)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_service_get_hand_state (LrgVRService   *self,
                                        LrgVRHand       hand,
                                        LrgVRHandState *state);

G_END_DECLS
