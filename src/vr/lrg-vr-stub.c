/* lrg-vr-stub.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Stub VR implementation, and the simulated headset headless tests use.
 */

#include "lrg-vr-stub.h"

#include <math.h>
#include <string.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_VR
#include "../lrg-log.h"

/**
 * SECTION:lrg-vr-stub
 * @title: LrgVRStub
 * @short_description: Stub VR implementation and simulated headset
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
 * A stub made with lrg_vr_stub_new_simulated() is a simulated headset
 * instead: it initialises, runs the runtime-paced frame loop with two
 * views and reports the poses, buttons and axes its setters give it, so
 * headless tests exercise VR code end to end without a runtime.
 *
 * Since: 1.0
 */

#define LRG_VR_STUB_DEFAULT_IPD  (0.064f)
#define LRG_VR_STUB_DEFAULT_FOV  (45.0f)
#define LRG_VR_STUB_DEFAULT_SIZE (1024u)

struct _LrgVRStub
{
    GObject parent_instance;

    /* simulated headset (lrg_vr_stub_new_simulated) */
    gboolean simulated;
    gboolean initialized;
    gboolean quit;
    gboolean in_frame;
    gfloat   hmd[16];
    gfloat   ipd;
    gfloat   fov[4];                /* left, right, up, down; radians */
    guint    width;
    guint    height;
    gfloat   controller[2][16];
    gboolean tracked[2];
    guint    buttons[2];
    gfloat   axes[2][4];
    guint    haptics[2];
    guint    frames;
};

static void lrg_vr_stub_vr_service_init (LrgVRServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgVRStub, lrg_vr_stub, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_VR_SERVICE,
                                                lrg_vr_stub_vr_service_init))

/* Singleton instance */
static LrgVRStub *default_stub = NULL;

/* ==========================================================================
 * Matrix helpers
 * ========================================================================== */

static void
identity (gfloat *m)
{
    gint i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

/* out = a * b, column-major 4x4; out may not alias a or b */
static void
multiply (const gfloat *a,
          const gfloat *b,
          gfloat       *out)
{
    gint c, r, k;

    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
        {
            gfloat sum = 0.0f;

            for (k = 0; k < 4; k++)
                sum += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = sum;
        }
}

/* the eye's offset from the head: half the IPD along the head's X axis */
static void
eye_to_head (LrgVRStub *self,
             guint      eye,
             gfloat    *m)
{
    identity (m);
    m[12] = (eye == LRG_VR_EYE_LEFT ? -0.5f : 0.5f) * self->ipd;
}

/* OpenGL projection (column-major, clip z in -1..1) for an asymmetric
   field of view, as LrgOpenXRService builds it from an XrFovf */
static void
projection (LrgVRStub *self,
            gfloat     near_clip,
            gfloat     far_clip,
            gfloat    *m)
{
    gfloat l = tanf (self->fov[0]);
    gfloat r = tanf (self->fov[1]);
    gfloat u = tanf (self->fov[2]);
    gfloat d = tanf (self->fov[3]);
    gfloat w = r - l;
    gfloat h = u - d;

    memset (m, 0, 16 * sizeof (gfloat));
    m[0]  = 2.0f / w;
    m[5]  = 2.0f / h;
    m[8]  = (r + l) / w;
    m[9]  = (u + d) / h;
    m[10] = -(far_clip + near_clip) / (far_clip - near_clip);
    m[11] = -1.0f;
    m[14] = -(2.0f * far_clip * near_clip) / (far_clip - near_clip);
}

/* ==========================================================================
 * LrgVRService Interface Implementation
 * ========================================================================== */

static gboolean
lrg_vr_stub_initialize (LrgVRService  *service,
                        GError       **error)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (self->simulated)
    {
        self->initialized = TRUE;
        self->quit = FALSE;
        return TRUE;
    }
    g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                 "VR runtime not available (stub implementation)");
    return FALSE;
}

static void
lrg_vr_stub_shutdown (LrgVRService *service)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    self->initialized = FALSE;
    self->in_frame = FALSE;
}

static gboolean
lrg_vr_stub_is_available (LrgVRService *service)
{
    return LRG_VR_STUB (service)->simulated;
}

static gboolean
lrg_vr_stub_is_hmd_present (LrgVRService *service)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    return self->simulated && self->initialized;
}

static void
lrg_vr_stub_poll_events (LrgVRService *self)
{
    /* No-op: the simulated headset's state changes only through setters */
}

static void
lrg_vr_stub_get_recommended_render_size (LrgVRService *service,
                                         guint        *width,
                                         guint        *height)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (width != NULL) *width = self->width;
    if (height != NULL) *height = self->height;
}

static void
lrg_vr_stub_get_eye_projection (LrgVRService *service,
                                LrgVREye      eye,
                                gfloat        near_clip,
                                gfloat        far_clip,
                                gfloat       *matrix)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    /* the plain stub keeps returning identity, as it always did */
    if (!self->simulated || far_clip <= near_clip)
    {
        identity (matrix);
        return;
    }
    projection (self, near_clip, far_clip, matrix);
}

static void
lrg_vr_stub_get_eye_to_head (LrgVRService *service,
                             LrgVREye      eye,
                             gfloat       *matrix)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (!self->simulated)
    {
        identity (matrix);
        return;
    }
    eye_to_head (self, eye, matrix);
}

static void
lrg_vr_stub_get_hmd_pose (LrgVRService *service,
                          gfloat       *matrix)
{
    memcpy (matrix, LRG_VR_STUB (service)->hmd, 16 * sizeof (gfloat));
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
lrg_vr_stub_get_controller_pose (LrgVRService *service,
                                 LrgVRHand     hand,
                                 gfloat       *matrix)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (hand != LRG_VR_HAND_LEFT && hand != LRG_VR_HAND_RIGHT)
    {
        identity (matrix);
        return;
    }
    memcpy (matrix, self->controller[hand], 16 * sizeof (gfloat));
}

static guint
lrg_vr_stub_get_controller_buttons (LrgVRService *service,
                                    LrgVRHand     hand)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (hand != LRG_VR_HAND_LEFT && hand != LRG_VR_HAND_RIGHT)
        return 0;
    return self->buttons[hand];
}

static gfloat
lrg_vr_stub_get_controller_axis (LrgVRService *service,
                                 LrgVRHand     hand,
                                 guint         axis)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if ((hand != LRG_VR_HAND_LEFT && hand != LRG_VR_HAND_RIGHT) || axis > 3)
        return 0.0f;
    return self->axes[hand][axis];
}

static void
lrg_vr_stub_trigger_haptic (LrgVRService *service,
                            LrgVRHand     hand,
                            gfloat        duration,
                            gfloat        amplitude)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    /* counted, so tests can see the feedback was asked for */
    if (hand == LRG_VR_HAND_LEFT || hand == LRG_VR_HAND_RIGHT)
        self->haptics[hand]++;
}

static gboolean
lrg_vr_stub_begin_frame (LrgVRService *service)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (!self->simulated || !self->initialized || self->quit)
        return FALSE;
    self->in_frame = TRUE;
    return TRUE;
}

static guint
lrg_vr_stub_get_view_count (LrgVRService *service)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    return self->in_frame ? 2 : 0;
}

static gboolean
lrg_vr_stub_begin_view (LrgVRService *service,
                        guint         view,
                        gfloat        near_clip,
                        gfloat        far_clip,
                        gfloat       *eye_pose,
                        gfloat       *proj,
                        guint        *framebuffer,
                        guint        *width,
                        guint        *height)
{
    LrgVRStub *self = LRG_VR_STUB (service);
    gfloat offset[16];

    if (!self->in_frame || view > 1)
        return FALSE;

    /* the eye in tracking space: the head pose times the eye's offset */
    eye_to_head (self, view, offset);
    if (eye_pose != NULL)
        multiply (self->hmd, offset, eye_pose);
    if (proj != NULL)
    {
        if (far_clip > near_clip)
            projection (self, near_clip, far_clip, proj);
        else
            identity (proj);
    }
    /* nothing is bound: the caller draws into the current target */
    if (framebuffer != NULL) *framebuffer = 0;
    if (width != NULL) *width = self->width;
    if (height != NULL) *height = self->height;
    return TRUE;
}

static void
lrg_vr_stub_end_view (LrgVRService *service,
                      guint         view)
{
    /* No-op: no swapchain image to release */
}

static void
lrg_vr_stub_end_frame (LrgVRService *service)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (!self->in_frame)
        return;
    self->in_frame = FALSE;
    self->frames++;
}

static gboolean
lrg_vr_stub_is_session_running (LrgVRService *service)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    return self->simulated && self->initialized && !self->quit;
}

static gboolean
lrg_vr_stub_should_quit (LrgVRService *service)
{
    return LRG_VR_STUB (service)->quit;
}

static gboolean
lrg_vr_stub_is_controller_tracked (LrgVRService *service,
                                   LrgVRHand     hand)
{
    LrgVRStub *self = LRG_VR_STUB (service);

    if (hand != LRG_VR_HAND_LEFT && hand != LRG_VR_HAND_RIGHT)
        return FALSE;
    return self->simulated && self->tracked[hand];
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
    iface->begin_frame = lrg_vr_stub_begin_frame;
    iface->get_view_count = lrg_vr_stub_get_view_count;
    iface->begin_view = lrg_vr_stub_begin_view;
    iface->end_view = lrg_vr_stub_end_view;
    iface->end_frame = lrg_vr_stub_end_frame;
    iface->is_session_running = lrg_vr_stub_is_session_running;
    iface->should_quit = lrg_vr_stub_should_quit;
    iface->is_controller_tracked = lrg_vr_stub_is_controller_tracked;
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
    gfloat half;

    identity (self->hmd);
    identity (self->controller[0]);
    identity (self->controller[1]);
    self->ipd = LRG_VR_STUB_DEFAULT_IPD;
    half = LRG_VR_STUB_DEFAULT_FOV * (gfloat) G_PI / 180.0f;
    self->fov[0] = -half;
    self->fov[1] = half;
    self->fov[2] = half;
    self->fov[3] = -half;
    self->width = LRG_VR_STUB_DEFAULT_SIZE;
    self->height = LRG_VR_STUB_DEFAULT_SIZE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgVRStub *
lrg_vr_stub_new (void)
{
    return g_object_new (LRG_TYPE_VR_STUB, NULL);
}

LrgVRStub *
lrg_vr_stub_get_default (void)
{
    if (default_stub == NULL)
        default_stub = lrg_vr_stub_new ();

    return default_stub;
}

LrgVRStub *
lrg_vr_stub_new_simulated (void)
{
    LrgVRStub *self = lrg_vr_stub_new ();

    self->simulated = TRUE;
    return self;
}

gboolean
lrg_vr_stub_get_simulated (LrgVRStub *self)
{
    g_return_val_if_fail (LRG_IS_VR_STUB (self), FALSE);

    return self->simulated;
}

void
lrg_vr_stub_set_hmd_pose (LrgVRStub    *self,
                          const gfloat *matrix)
{
    g_return_if_fail (LRG_IS_VR_STUB (self));
    g_return_if_fail (matrix != NULL);

    memcpy (self->hmd, matrix, 16 * sizeof (gfloat));
}

void
lrg_vr_stub_set_ipd (LrgVRStub *self,
                     gfloat     ipd)
{
    g_return_if_fail (LRG_IS_VR_STUB (self));

    self->ipd = CLAMP (ipd, 0.0f, 0.2f);
}

gfloat
lrg_vr_stub_get_ipd (LrgVRStub *self)
{
    g_return_val_if_fail (LRG_IS_VR_STUB (self), 0.0f);

    return self->ipd;
}

void
lrg_vr_stub_set_fov (LrgVRStub *self,
                     gfloat     left,
                     gfloat     right,
                     gfloat     up,
                     gfloat     down)
{
    gfloat k = (gfloat) G_PI / 180.0f;

    g_return_if_fail (LRG_IS_VR_STUB (self));
    g_return_if_fail (right > left && up > down);
    g_return_if_fail (left > -90.0f && right < 90.0f && down > -90.0f && up < 90.0f);

    self->fov[0] = left * k;
    self->fov[1] = right * k;
    self->fov[2] = up * k;
    self->fov[3] = down * k;
}

void
lrg_vr_stub_set_render_size (LrgVRStub *self,
                             guint      width,
                             guint      height)
{
    g_return_if_fail (LRG_IS_VR_STUB (self));
    g_return_if_fail (width > 0 && height > 0);

    self->width = width;
    self->height = height;
}

void
lrg_vr_stub_set_controller (LrgVRStub    *self,
                            LrgVRHand     hand,
                            const gfloat *matrix,
                            gboolean      tracked)
{
    g_return_if_fail (LRG_IS_VR_STUB (self));
    g_return_if_fail (hand == LRG_VR_HAND_LEFT || hand == LRG_VR_HAND_RIGHT);

    if (matrix != NULL)
        memcpy (self->controller[hand], matrix, 16 * sizeof (gfloat));
    else
        identity (self->controller[hand]);
    self->tracked[hand] = tracked;
}

void
lrg_vr_stub_set_controller_buttons (LrgVRStub *self,
                                    LrgVRHand  hand,
                                    guint      buttons)
{
    g_return_if_fail (LRG_IS_VR_STUB (self));
    g_return_if_fail (hand == LRG_VR_HAND_LEFT || hand == LRG_VR_HAND_RIGHT);

    self->buttons[hand] = buttons;
}

void
lrg_vr_stub_set_controller_axis (LrgVRStub *self,
                                 LrgVRHand  hand,
                                 guint      axis,
                                 gfloat     value)
{
    g_return_if_fail (LRG_IS_VR_STUB (self));
    g_return_if_fail (hand == LRG_VR_HAND_LEFT || hand == LRG_VR_HAND_RIGHT);
    g_return_if_fail (axis <= LRG_VR_AXIS_GRIP);

    self->axes[hand][axis] = value;
}

guint
lrg_vr_stub_get_haptic_count (LrgVRStub *self,
                              LrgVRHand  hand)
{
    g_return_val_if_fail (LRG_IS_VR_STUB (self), 0);
    g_return_val_if_fail (hand == LRG_VR_HAND_LEFT || hand == LRG_VR_HAND_RIGHT, 0);

    return self->haptics[hand];
}

guint
lrg_vr_stub_get_frame_count (LrgVRStub *self)
{
    g_return_val_if_fail (LRG_IS_VR_STUB (self), 0);

    return self->frames;
}

void
lrg_vr_stub_request_quit (LrgVRStub *self)
{
    g_return_if_fail (LRG_IS_VR_STUB (self));

    self->quit = TRUE;
}
