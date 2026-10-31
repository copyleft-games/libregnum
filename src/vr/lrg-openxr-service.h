/* lrg-openxr-service.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * #LrgVRService over OpenXR.  Binds to the GL context current at
 * lrg_vr_service_initialize() time (GLX through XR_KHR_opengl_enable, or EGL
 * through XR_MNDX_egl_enable), renders through one swapchain per eye,
 * reads motion controllers through an action set, and tracks articulated
 * hands through XR_EXT_hand_tracking (XR_FB_hand_tracking_aim for the
 * runtime's aim and pinch) when the runtime offers it.
 *
 * Built only with OPENXR=1 (pkg-config openxr).  Without it the type still
 * exists and lrg_vr_service_initialize() fails with
 * %LRG_VR_ERROR_NOT_AVAILABLE, so callers need no conditional compilation.
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

#define LRG_TYPE_OPENXR_SERVICE (lrg_openxr_service_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgOpenXRService, lrg_openxr_service, LRG, OPENXR_SERVICE, GObject)

/**
 * lrg_openxr_service_new:
 * @app_name: (nullable): application name reported to the runtime
 *
 * Creates an uninitialised OpenXR service.  Make the GL context current, then
 * call lrg_vr_service_initialize().
 *
 * Returns: (transfer full): a new #LrgOpenXRService
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgOpenXRService * lrg_openxr_service_new (const gchar *app_name);

/**
 * lrg_openxr_service_is_supported:
 *
 * Returns: %TRUE when libregnum was built with OpenXR support
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_is_supported (void);

/**
 * lrg_openxr_service_get_runtime_name:
 * @self: a #LrgOpenXRService
 *
 * Returns: (nullable): the runtime's name and version once initialised
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_openxr_service_get_runtime_name (LrgOpenXRService *self);

/**
 * lrg_openxr_service_get_system_name:
 * @self: a #LrgOpenXRService
 *
 * Returns: (nullable): the HMD system's name once initialised
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_openxr_service_get_system_name (LrgOpenXRService *self);

/**
 * lrg_openxr_service_get_display_refresh_rate:
 * @self: a #LrgOpenXRService
 *
 * Returns: the HMD refresh rate in Hz, or 90 when the runtime does not say
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_openxr_service_get_display_refresh_rate (LrgOpenXRService *self);

/**
 * lrg_openxr_service_is_focused:
 * @self: a #LrgOpenXRService
 *
 * Controller input is only delivered to the focused session.
 *
 * Returns: %TRUE while the session has input focus
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_is_focused (LrgOpenXRService *self);

/**
 * lrg_openxr_service_get_session_state_name:
 * @self: a #LrgOpenXRService
 *
 * Returns: the session state ("idle", "ready", "synchronized", "visible",
 *   "focused", "stopping", "loss-pending", "exiting", or "none")
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_openxr_service_get_session_state_name (LrgOpenXRService *self);

/**
 * lrg_openxr_service_get_interaction_profile:
 * @self: a #LrgOpenXRService
 * @hand: which hand
 *
 * Returns: (transfer full) (nullable): the interaction profile the runtime
 *   chose for @hand (e.g. "/interaction_profiles/valve/index_controller"),
 *   or %NULL when none is bound yet
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_openxr_service_get_interaction_profile (LrgOpenXRService *self,
                                                    LrgVRHand         hand);

/**
 * lrg_openxr_service_has_hand_aim:
 * @self: a #LrgOpenXRService
 *
 * Whether hands are tracked with the runtime's own aim pose and pinch
 * strength (XR_FB_hand_tracking_aim), rather than ones derived from the
 * joints.  See lrg_vr_service_get_hand_state().
 *
 * Returns: %TRUE while hand tracking runs with XR_FB_hand_tracking_aim
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_has_hand_aim (LrgOpenXRService *self);

/**
 * lrg_openxr_service_set_resolution_scale:
 * @self: a #LrgOpenXRService, not yet initialised
 * @scale: eye image size relative to the runtime's recommendation
 *
 * Renders each eye at @scale times the recommended size, clamped to the
 * runtime's maximum.  The recommendation already reflects the runtime's
 * own render-resolution choice, which on a standalone headset is often
 * well below the display's native resolution; text, whose detail is
 * finer than anything else in a scene, loses most to that.  Takes effect
 * when the swapchains are created, so call it before initialising.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_openxr_service_set_resolution_scale (LrgOpenXRService *self,
                                              gfloat            scale);

/**
 * lrg_openxr_service_get_eye_size:
 * @self: a #LrgOpenXRService
 * @width: (out) (optional): eye image width in pixels
 * @height: (out) (optional): eye image height in pixels
 *
 * The size each eye is actually rendered at -- the runtime's
 * recommendation times the resolution scale, clamped -- or 0 x 0 before
 * the swapchains exist.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_openxr_service_get_eye_size (LrgOpenXRService *self,
                                      guint            *width,
                                      guint            *height);

/**
 * lrg_openxr_service_start_frame_thread:
 * @self: an initialised #LrgOpenXRService
 *
 * Moves the frame wait (xrWaitFrame) to a background thread so
 * lrg_vr_service_begin_frame() never blocks: it returns %FALSE at once when
 * no frame is due yet.  The returned file descriptor becomes readable when
 * one is -- watch it from an event loop (it is drained by begin_frame).
 * Idempotent.
 *
 * Returns: the frame-ready file descriptor, or -1 on failure
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_openxr_service_start_frame_thread (LrgOpenXRService *self);

/**
 * lrg_openxr_service_get_frame_fd:
 * @self: a #LrgOpenXRService
 *
 * Returns: the frame-ready descriptor of the frame thread, or -1 when it is
 *   not running
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_openxr_service_get_frame_fd (LrgOpenXRService *self);

/**
 * lrg_openxr_service_probe:
 * @app_name: (nullable): application name reported to the runtime
 * @runtime_name: (out) (optional) (transfer full) (nullable): the runtime's
 *   name and version
 * @system_name: (out) (optional) (transfer full) (nullable): the headset's
 *   name
 * @error: return location for error
 *
 * Checks, without a GL context and without starting a session, that a
 * headset can be used: libregnum was built with OpenXR, the loader finds
 * an active runtime, an instance can be created and the runtime has a
 * head-mounted display system.  The instance is destroyed again, so a
 * program can call this before it opens a window and fail early with a
 * clear message.
 *
 * Returns: %TRUE when a headset is ready; otherwise %FALSE with
 *   %LRG_VR_ERROR_NOT_AVAILABLE (no OpenXR build, no runtime, no OpenGL
 *   or EGL support) or %LRG_VR_ERROR_HMD_NOT_FOUND (a runtime without a
 *   connected headset)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_probe (const gchar  *app_name,
                                   gchar       **runtime_name,
                                   gchar       **system_name,
                                   GError      **error);

/**
 * lrg_openxr_service_create_layer:
 * @self: an initialised #LrgOpenXRService
 * @width: image width in pixels
 * @height: image height in pixels
 * @error: return location for a #GError
 *
 * Makes a swapchain for a flat composition layer -- a UI, a text panel, a
 * video -- in the eye swapchains' format (sRGB when offered), with a depth
 * buffer per image (for 3D models drawn into it), at most four per
 * service.  Render into it with lrg_openxr_service_begin_layer() /
 * lrg_openxr_service_end_layer() (once is enough for static content), and
 * show it each frame with lrg_openxr_service_submit_layer_quad() or
 * lrg_openxr_service_submit_layer_cylinder().  The compositor samples the
 * layer straight into the display at its own resolution, which keeps text
 * much sharper than drawing it into the eye images.
 *
 * Returns: the layer's id (1 or more), or 0 on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_openxr_service_create_layer (LrgOpenXRService  *self,
                                       guint              width,
                                       guint              height,
                                       GError           **error);

/**
 * lrg_openxr_service_begin_layer:
 * @self: a #LrgOpenXRService
 * @id: a layer id from lrg_openxr_service_create_layer()
 * @framebuffer: (out) (optional): the bound FBO
 * @width: (out) (optional): its width
 * @height: (out) (optional): its height
 *
 * Acquires the layer's next image and binds its framebuffer (with the
 * viewport set to all of it).  Draw, then call
 * lrg_openxr_service_end_layer().  Needs a running session.
 *
 * Returns: %TRUE when an image is bound
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_begin_layer (LrgOpenXRService *self,
                                         guint             id,
                                         guint            *framebuffer,
                                         guint            *width,
                                         guint            *height);

/**
 * lrg_openxr_service_end_layer:
 * @self: a #LrgOpenXRService
 * @id: the layer begun with lrg_openxr_service_begin_layer()
 *
 * Flushes, unbinds and releases the layer's image.  From then on the layer
 * can be submitted, and keeps showing that image until it is drawn again.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_openxr_service_end_layer (LrgOpenXRService *self,
                                   guint             id);

/**
 * lrg_openxr_service_submit_layer_quad:
 * @self: a #LrgOpenXRService
 * @id: a layer that has been drawn at least once
 * @rect: (nullable) (array fixed-size=4): the part of the layer to show,
 *   x, y, width, height in pixels from the image's top-left, or %NULL for
 *   all of it
 * @pose: (array fixed-size=16): the quad's centre and orientation in
 *   tracking space, column-major, no scale; it faces +Z
 * @width: the quad's width in metres
 * @height: the quad's height in metres
 * @blend: blend by the image's (premultiplied) alpha instead of covering
 *
 * Shows @rect of the layer as a flat quad for the current frame (between
 * lrg_vr_service_begin_frame() and lrg_vr_service_end_frame()).  Several
 * placements may use one layer.  Layers composite above the projection
 * layer in submission order, with no depth test against the scene.
 *
 * Returns: %TRUE when queued
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_submit_layer_quad (LrgOpenXRService *self,
                                               guint             id,
                                               const gint       *rect,
                                               const gfloat     *pose,
                                               gfloat            width,
                                               gfloat            height,
                                               gboolean          blend);

/**
 * lrg_openxr_service_submit_layer_cylinder:
 * @self: a #LrgOpenXRService
 * @id: a layer that has been drawn at least once
 * @rect: (nullable) (array fixed-size=4): the part of the layer to show, as
 *   for lrg_openxr_service_submit_layer_quad()
 * @pose: (array fixed-size=16): the cylinder's centre (the viewer's spot)
 *   and orientation; the panel sits @radius along -Z, its axis along +Y
 * @radius: the cylinder's radius in metres
 * @central_angle: the arc the panel covers, in radians
 * @aspect_ratio: the panel's width over its height
 * @blend: blend by the image's (premultiplied) alpha
 *
 * Shows @rect of the layer on the inside of a cylinder for the current
 * frame -- a curved panel at a constant distance from the viewer, the
 * comfortable way to read a wide UI.  Needs
 * XR_KHR_composition_layer_cylinder (lrg_openxr_service_has_cylinder_layers());
 * without it this returns %FALSE and the caller falls back to a quad.
 *
 * Returns: %TRUE when queued
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_submit_layer_cylinder (LrgOpenXRService *self,
                                                   guint             id,
                                                   const gint       *rect,
                                                   const gfloat     *pose,
                                                   gfloat            radius,
                                                   gfloat            central_angle,
                                                   gfloat            aspect_ratio,
                                                   gboolean          blend);

/**
 * lrg_openxr_service_has_cylinder_layers:
 * @self: an initialised #LrgOpenXRService
 *
 * Returns: %TRUE when the runtime composites cylinder layers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_has_cylinder_layers (LrgOpenXRService *self);

/**
 * lrg_openxr_service_get_grip_pose:
 * @self: a #LrgOpenXRService
 * @hand: which hand
 * @matrix: (out caller-allocates) (array fixed-size=16): the grip pose,
 *   column-major, in tracking space
 *
 * The controller's grip pose -- the palm, where something held or worn on
 * the hand sits -- as opposed to the aim pose lrg_vr_service_get_controller_pose()
 * gives (the pointing ray).  Updated in each begin_frame while focused.
 *
 * Returns: %TRUE when the pose is tracked this frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_get_grip_pose (LrgOpenXRService *self,
                                           LrgVRHand         hand,
                                           gfloat           *matrix);

/**
 * lrg_openxr_service_take_recentered:
 * @self: a #LrgOpenXRService
 *
 * Whether the runtime has moved the tracking origin since the last call --
 * the user recentred (on SteamVR, a long press of the Steam button) -- and
 * clears the flag.  Content anchored in tracking space may want to follow.
 *
 * Returns: %TRUE once per recentre
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_openxr_service_take_recentered (LrgOpenXRService *self);

G_END_DECLS
