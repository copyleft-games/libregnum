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

G_END_DECLS
