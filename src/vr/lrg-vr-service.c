/* lrg-vr-service.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgVRService interface.
 */

#include "lrg-vr-service.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_VR
#include "../lrg-log.h"

/**
 * SECTION:lrg-vr-service
 * @title: LrgVRService
 * @short_description: Abstract VR backend interface
 *
 * The #LrgVRService interface provides a common API for VR functionality.
 * Different implementations can be used for different VR runtimes.
 *
 * Available implementations:
 * - #LrgVRStub: No-op implementation when VR is not available
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgVRService, lrg_vr_service, G_TYPE_OBJECT)

static void
lrg_vr_service_default_init (LrgVRServiceInterface *iface)
{
    /* No default implementations */
}

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
gboolean
lrg_vr_service_initialize (LrgVRService  *self,
                           GError       **error)
{
    LrgVRServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_VR_SERVICE (self), FALSE);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->initialize == NULL)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                     "VR initialization not implemented");
        return FALSE;
    }

    return iface->initialize (self, error);
}

/**
 * lrg_vr_service_shutdown:
 * @self: a #LrgVRService
 *
 * Shuts down the VR runtime.
 *
 * Since: 1.0
 */
void
lrg_vr_service_shutdown (LrgVRService *self)
{
    LrgVRServiceInterface *iface;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->shutdown != NULL)
        iface->shutdown (self);
}

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
gboolean
lrg_vr_service_is_available (LrgVRService *self)
{
    LrgVRServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_VR_SERVICE (self), FALSE);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->is_available == NULL)
        return FALSE;

    return iface->is_available (self);
}

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
gboolean
lrg_vr_service_is_hmd_present (LrgVRService *self)
{
    LrgVRServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_VR_SERVICE (self), FALSE);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->is_hmd_present == NULL)
        return FALSE;

    return iface->is_hmd_present (self);
}

/**
 * lrg_vr_service_poll_events:
 * @self: a #LrgVRService
 *
 * Polls and processes VR events.
 *
 * Since: 1.0
 */
void
lrg_vr_service_poll_events (LrgVRService *self)
{
    LrgVRServiceInterface *iface;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->poll_events != NULL)
        iface->poll_events (self);
}

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
void
lrg_vr_service_get_recommended_render_size (LrgVRService *self,
                                            guint        *width,
                                            guint        *height)
{
    LrgVRServiceInterface *iface;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->get_recommended_render_size != NULL)
    {
        iface->get_recommended_render_size (self, width, height);
    }
    else
    {
        if (width != NULL) *width = 1024;
        if (height != NULL) *height = 1024;
    }
}

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
void
lrg_vr_service_get_eye_projection (LrgVRService *self,
                                   LrgVREye      eye,
                                   gfloat        near_clip,
                                   gfloat        far_clip,
                                   gfloat       *matrix)
{
    LrgVRServiceInterface *iface;
    gint i;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));
    g_return_if_fail (matrix != NULL);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->get_eye_projection != NULL)
    {
        iface->get_eye_projection (self, eye, near_clip, far_clip, matrix);
    }
    else
    {
        /* Return identity matrix as fallback */
        for (i = 0; i < 16; i++)
            matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

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
void
lrg_vr_service_get_eye_to_head (LrgVRService *self,
                                LrgVREye      eye,
                                gfloat       *matrix)
{
    LrgVRServiceInterface *iface;
    gint i;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));
    g_return_if_fail (matrix != NULL);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->get_eye_to_head != NULL)
    {
        iface->get_eye_to_head (self, eye, matrix);
    }
    else
    {
        /* Return identity matrix as fallback */
        for (i = 0; i < 16; i++)
            matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

/**
 * lrg_vr_service_get_hmd_pose:
 * @self: a #LrgVRService
 * @matrix: (out) (array fixed-size=16): 4x4 HMD pose matrix
 *
 * Gets the current HMD pose.
 *
 * Since: 1.0
 */
void
lrg_vr_service_get_hmd_pose (LrgVRService *self,
                             gfloat       *matrix)
{
    LrgVRServiceInterface *iface;
    gint i;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));
    g_return_if_fail (matrix != NULL);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->get_hmd_pose != NULL)
    {
        iface->get_hmd_pose (self, matrix);
    }
    else
    {
        /* Return identity matrix as fallback */
        for (i = 0; i < 16; i++)
            matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

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
gboolean
lrg_vr_service_submit_frame (LrgVRService *self,
                             LrgVREye      eye,
                             guint         texture_id,
                             GError      **error)
{
    LrgVRServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_VR_SERVICE (self), FALSE);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->submit_frame == NULL)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_COMPOSITOR,
                     "Frame submission not implemented");
        return FALSE;
    }

    return iface->submit_frame (self, eye, texture_id, error);
}

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
void
lrg_vr_service_get_controller_pose (LrgVRService *self,
                                    LrgVRHand     hand,
                                    gfloat       *matrix)
{
    LrgVRServiceInterface *iface;
    gint i;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));
    g_return_if_fail (matrix != NULL);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->get_controller_pose != NULL)
    {
        iface->get_controller_pose (self, hand, matrix);
    }
    else
    {
        /* Return identity matrix as fallback */
        for (i = 0; i < 16; i++)
            matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

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
guint
lrg_vr_service_get_controller_buttons (LrgVRService *self,
                                       LrgVRHand     hand)
{
    LrgVRServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_VR_SERVICE (self), 0);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->get_controller_buttons == NULL)
        return 0;

    return iface->get_controller_buttons (self, hand);
}

/**
 * lrg_vr_service_get_controller_axis:
 * @self: a #LrgVRService
 * @hand: which hand
 * @axis: axis index
 *
 * Gets an axis value for a motion controller.
 *
 * Returns: axis value
 *
 * Since: 1.0
 */
gfloat
lrg_vr_service_get_controller_axis (LrgVRService *self,
                                    LrgVRHand     hand,
                                    guint         axis)
{
    LrgVRServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_VR_SERVICE (self), 0.0f);

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->get_controller_axis == NULL)
        return 0.0f;

    return iface->get_controller_axis (self, hand, axis);
}

/**
 * lrg_vr_service_trigger_haptic:
 * @self: a #LrgVRService
 * @hand: which hand
 * @duration: duration in seconds
 * @amplitude: vibration amplitude
 *
 * Triggers haptic feedback on a controller.
 *
 * Since: 1.0
 */
void
lrg_vr_service_trigger_haptic (LrgVRService *self,
                               LrgVRHand     hand,
                               gfloat        duration,
                               gfloat        amplitude)
{
    LrgVRServiceInterface *iface;

    g_return_if_fail (LRG_IS_VR_SERVICE (self));

    iface = LRG_VR_SERVICE_GET_IFACE (self);
    if (iface->trigger_haptic != NULL)
        iface->trigger_haptic (self, hand, duration, amplitude);
}
