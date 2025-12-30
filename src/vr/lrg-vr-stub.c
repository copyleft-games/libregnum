/* lrg-vr-stub.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Stub VR implementation.
 */

#include "lrg-vr-stub.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_VR
#include "../lrg-log.h"

/**
 * SECTION:lrg-vr-stub
 * @title: LrgVRStub
 * @short_description: Stub VR implementation
 *
 * The #LrgVRStub provides a no-op implementation of #LrgVRService
 * for use when no VR runtime is available.
 *
 * All operations return appropriate failure states:
 * - is_available() returns FALSE
 * - is_hmd_present() returns FALSE
 * - initialize() returns FALSE with error
 * - Controller queries return no input
 *
 * Since: 1.0
 */

struct _LrgVRStub
{
    GObject parent_instance;
};

static void lrg_vr_stub_vr_service_init (LrgVRServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgVRStub, lrg_vr_stub, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_VR_SERVICE,
                                                lrg_vr_stub_vr_service_init))

/* Singleton instance */
static LrgVRStub *default_stub = NULL;

/* ==========================================================================
 * LrgVRService Interface Implementation
 * ========================================================================== */

static gboolean
lrg_vr_stub_initialize (LrgVRService  *self,
                        GError       **error)
{
    g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                 "VR runtime not available (stub implementation)");
    return FALSE;
}

static void
lrg_vr_stub_shutdown (LrgVRService *self)
{
    /* No-op */
}

static gboolean
lrg_vr_stub_is_available (LrgVRService *self)
{
    return FALSE;
}

static gboolean
lrg_vr_stub_is_hmd_present (LrgVRService *self)
{
    return FALSE;
}

static void
lrg_vr_stub_poll_events (LrgVRService *self)
{
    /* No-op */
}

static void
lrg_vr_stub_get_recommended_render_size (LrgVRService *self,
                                         guint        *width,
                                         guint        *height)
{
    /* Return a reasonable default */
    if (width != NULL) *width = 1024;
    if (height != NULL) *height = 1024;
}

static void
lrg_vr_stub_get_eye_projection (LrgVRService *self,
                                LrgVREye      eye,
                                gfloat        near_clip,
                                gfloat        far_clip,
                                gfloat       *matrix)
{
    gint i;

    /* Return identity matrix */
    for (i = 0; i < 16; i++)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static void
lrg_vr_stub_get_eye_to_head (LrgVRService *self,
                             LrgVREye      eye,
                             gfloat       *matrix)
{
    gint i;

    /* Return identity matrix */
    for (i = 0; i < 16; i++)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static void
lrg_vr_stub_get_hmd_pose (LrgVRService *self,
                          gfloat       *matrix)
{
    gint i;

    /* Return identity matrix */
    for (i = 0; i < 16; i++)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static gboolean
lrg_vr_stub_submit_frame (LrgVRService *self,
                          LrgVREye      eye,
                          guint         texture_id,
                          GError      **error)
{
    g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_COMPOSITOR,
                 "VR compositor not available (stub implementation)");
    return FALSE;
}

static void
lrg_vr_stub_get_controller_pose (LrgVRService *self,
                                 LrgVRHand     hand,
                                 gfloat       *matrix)
{
    gint i;

    /* Return identity matrix */
    for (i = 0; i < 16; i++)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static guint
lrg_vr_stub_get_controller_buttons (LrgVRService *self,
                                    LrgVRHand     hand)
{
    return 0;
}

static gfloat
lrg_vr_stub_get_controller_axis (LrgVRService *self,
                                 LrgVRHand     hand,
                                 guint         axis)
{
    return 0.0f;
}

static void
lrg_vr_stub_trigger_haptic (LrgVRService *self,
                            LrgVRHand     hand,
                            gfloat        duration,
                            gfloat        amplitude)
{
    /* No-op */
}

static void
lrg_vr_stub_vr_service_init (LrgVRServiceInterface *iface)
{
    iface->initialize = lrg_vr_stub_initialize;
    iface->shutdown = lrg_vr_stub_shutdown;
    iface->is_available = lrg_vr_stub_is_available;
    iface->is_hmd_present = lrg_vr_stub_is_hmd_present;
    iface->poll_events = lrg_vr_stub_poll_events;
    iface->get_recommended_render_size = lrg_vr_stub_get_recommended_render_size;
    iface->get_eye_projection = lrg_vr_stub_get_eye_projection;
    iface->get_eye_to_head = lrg_vr_stub_get_eye_to_head;
    iface->get_hmd_pose = lrg_vr_stub_get_hmd_pose;
    iface->submit_frame = lrg_vr_stub_submit_frame;
    iface->get_controller_pose = lrg_vr_stub_get_controller_pose;
    iface->get_controller_buttons = lrg_vr_stub_get_controller_buttons;
    iface->get_controller_axis = lrg_vr_stub_get_controller_axis;
    iface->trigger_haptic = lrg_vr_stub_trigger_haptic;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_vr_stub_class_init (LrgVRStubClass *klass)
{
    /* Nothing to do */
}

static void
lrg_vr_stub_init (LrgVRStub *self)
{
    /* Nothing to do */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_vr_stub_new:
 *
 * Creates a new VR stub instance.
 *
 * Returns: (transfer full): a new #LrgVRStub
 *
 * Since: 1.0
 */
LrgVRStub *
lrg_vr_stub_new (void)
{
    return g_object_new (LRG_TYPE_VR_STUB, NULL);
}

/**
 * lrg_vr_stub_get_default:
 *
 * Gets the default VR stub instance.
 *
 * Returns: (transfer none): the default #LrgVRStub
 *
 * Since: 1.0
 */
LrgVRStub *
lrg_vr_stub_get_default (void)
{
    if (default_stub == NULL)
        default_stub = lrg_vr_stub_new ();

    return default_stub;
}
