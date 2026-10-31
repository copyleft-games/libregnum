/* lrg-openxr-service.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * OpenXR implementation of #LrgVRService.  See lrg-openxr-service.h.
 *
 * Lifecycle, as OpenXR requires it:
 *
 *   initialize   instance -> system -> graphics requirements -> session bound
 *                to the CURRENT GL context -> LOCAL space -> swapchains ->
 *                action set.  The session starts IDLE; poll_events begins it
 *                when the runtime says READY.
 *   begin_frame  poll events; if running: xrWaitFrame (blocks to the
 *                runtime's pacing) + xrBeginFrame, locate views, sync input.
 *   begin_view   acquire + wait the eye's swapchain image, bind its FBO.
 *   end_view     release the image.
 *   end_frame    xrEndFrame with the projection layer, then any quad or
 *                cylinder layers submitted this frame (lrg_openxr_service_
 *                submit_layer_quad / _cylinder), in submission order.
 *
 * Composition layers: an application can make extra swapchains
 * (lrg_openxr_service_create_layer) for flat content -- a UI, a video, a
 * text panel -- and hand them to the compositor as quads (core OpenXR) or
 * cylinders (XR_KHR_composition_layer_cylinder, enabled when offered).  The
 * compositor samples them once, at their own resolution, straight into the
 * display; text stays far sharper than it does drawn into the eye images and
 * resampled by the lens warp.  Several placements may share one swapchain
 * through sub-image rectangles (e.g. a whole UI on a panel, and parts of it
 * on a wrist).
 *
 * Hands (XR_EXT_hand_tracking, when the runtime offers it): two hand
 * trackers are made with the session.  Their 26 joints are located at
 * each frame's predicted display time on the frame-wait thread, straight
 * after xrWaitFrame returns -- the thread that already blocks, so Emacs's
 * never does -- and handed over with the frame in begin_frame, which then
 * derives the aim ray and the pinch latch on the caller's thread (the
 * latch's history lives there).  XR_FB_hand_tracking_aim, where offered,
 * supplies the runtime's own aim pose and pinch strength; without it both
 * are computed from the joints (lrg-vr-hand.c).
 *
 * Every xrBeginFrame is matched by an xrEndFrame even when the runtime asks us
 * not to render (shouldRender == false) -- skipping it wedges the session.
 */

#include "config.h"
#include "lrg-openxr-service.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_VR
#include "../lrg-log.h"

#ifdef LRG_HAS_OPENXR

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <EGL/egl.h>
#include <rlgl.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_PLATFORM_EGL
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

/* XR_MNDX_egl_enable's getProcAddress: version 2 of the extension (the
 * OpenXR SDK 1.0.29+ headers) types it PFN_xrEglGetProcAddressMNDX,
 * version 1 (older distributions' headers, e.g. Ubuntu 24.04) types it
 * EGL's PFNEGLGETPROCADDRESSPROC */
#if defined(XR_MNDX_egl_enable_SPEC_VERSION) && XR_MNDX_egl_enable_SPEC_VERSION >= 2
typedef PFN_xrEglGetProcAddressMNDX LrgXrEglGetProcAddress;
#else
typedef PFNEGLGETPROCADDRESSPROC LrgXrEglGetProcAddress;
#endif

#define LRG_XR_MAX_VIEWS   2
#define LRG_XR_MAX_IMAGES  8
#define LRG_XR_MAX_LAYERS  4    /* layer swapchains */
#define LRG_XR_MAX_PLACED  15   /* quad/cylinder placements per frame (+1 projection) */

/* GL_SRGB8_ALPHA8 / GL_RGBA8: sRGB storage is preferred -- raylib writes
   display-referred values, and an sRGB swapchain makes the compositor read
   them as such (an RGBA8 one is taken as linear and comes out washed out). */
#define LRG_GL_SRGB8_ALPHA8 0x8C43
#define LRG_GL_RGBA8        0x8058

/* Trigger / grip are analog; report them as buttons with hysteresis so a
   finger resting near the threshold does not chatter. */
#define LRG_XR_PRESS   0.70f
#define LRG_XR_RELEASE 0.55f

typedef struct
{
    XrSwapchain swapchain;
    guint32     width;
    guint32     height;
    guint32     n_images;
    guint32     images[LRG_XR_MAX_IMAGES];   /* GL texture names (runtime-owned) */
    guint       fbos[LRG_XR_MAX_IMAGES];     /* one FBO (+depth) per image */
    guint32     acquired;                    /* image index while a view is open */
    gboolean    open;
} LrgXrView;

typedef struct
{
    XrPath   path;
    XrSpace  aim_space;
    XrSpace  grip_space;
    gboolean tracked;
    gboolean grip_tracked;
    gfloat   aim[16];
    gfloat   grip[16];
    guint    buttons;
    gfloat   axes[4];
} LrgXrHand;

/* A composition-layer swapchain (lrg_openxr_service_create_layer).  @valid
   is set once an image has been rendered and released: OpenXR rejects a
   layer whose swapchain has never released one. */
typedef LrgXrView LrgXrLayer;

/* One quad or cylinder placement queued for the current frame. */
typedef union
{
    XrCompositionLayerBaseHeader     base;
    XrCompositionLayerQuad           quad;
    XrCompositionLayerCylinderKHR    cylinder;
} LrgXrPlaced;

#endif /* LRG_HAS_OPENXR */

struct _LrgOpenXRService
{
    GObject parent_instance;

    gchar   *app_name;
    gchar   *runtime_name;
    gchar   *system_name;
    gboolean initialized;

#ifdef LRG_HAS_OPENXR
    XrInstance     instance;
    XrSystemId     system;
    XrSession      session;
    XrSpace        local_space;
    XrSpace        view_space;
    XrSessionState state;
    gboolean       running;
    gboolean       quit_requested;
    gboolean       has_refresh_rate_ext;
    gboolean       has_cylinder_ext;
    gfloat         refresh_rate;
    gboolean       recentered;      /* LOCAL space was recentred by the runtime */

    LrgXrLayer     layers[LRG_XR_MAX_LAYERS];
    gboolean       layer_valid[LRG_XR_MAX_LAYERS];
    guint          n_layers;
    LrgXrPlaced    placed[LRG_XR_MAX_PLACED];
    guint          n_placed;

    XrViewConfigurationView config[LRG_XR_MAX_VIEWS];
    LrgXrView               views[LRG_XR_MAX_VIEWS];
    XrView                  located[LRG_XR_MAX_VIEWS];
    XrCompositionLayerProjectionView layer_views[LRG_XR_MAX_VIEWS];
    guint32                 view_count;
    gint64                  format;

    XrFrameState frame_state;
    gboolean     frame_begun;

    /* Threaded frame wait (lrg_openxr_service_start_frame_thread).  The
       waiter calls xrWaitFrame -- which may be called from any thread -- and
       signals @frame_fd once a frame is due; begin_frame then never blocks.
       Everything below @frame_lock is shared with the waiter. */
    GThread     *waiter;
    gint         frame_fd;
    gfloat       resolution_scale; /* eye size / recommended, see setter */
    GMutex       frame_lock;
    GCond        frame_cond;
    gboolean     waiter_quit;
    gboolean     waiter_busy;      /* inside xrWaitFrame */
    gboolean     want_wait;        /* the previous waited frame was ended */
    gboolean     frame_ready;      /* waited, not yet begun */
    gboolean     end_pending;      /* xrEndSession deferred until idle */
    XrFrameState pending_state;
    gboolean     views_valid;
    guint        views_rendered;
    gfloat       hmd[16];

    XrActionSet action_set;
    XrAction    act_aim;
    XrAction    act_grip;
    XrAction    act_trigger;
    XrAction    act_squeeze;
    XrAction    act_stick;
    XrAction    act_stick_click;
    XrAction    act_primary;
    XrAction    act_secondary;
    XrAction    act_menu;
    XrAction    act_x;
    XrAction    act_y;
    XrAction    act_bumper;
    XrAction    act_dpad[4];      /* up, down, left, right */
    XrAction    act_view;
    XrAction    act_haptic;
    LrgXrHand   hands[2];

    /* Articulated hands.  @hand_state is the current frame's (caller's
       thread); @pending_hands is what the waiter located with the frame it
       waited for, under @frame_lock. */
    gboolean                    has_hand_ext;
    gboolean                    has_hand_aim_ext;
    gboolean                    hand_tracking;   /* trackers exist */
    XrHandTrackerEXT            hand_trackers[2];
    PFN_xrCreateHandTrackerEXT  create_hand_tracker;
    PFN_xrDestroyHandTrackerEXT destroy_hand_tracker;
    PFN_xrLocateHandJointsEXT   locate_hand_joints;
    LrgVRHandState              hand_state[2];
    LrgVRHandState              pending_hands[2];
    gboolean                    pending_hands_valid;
#endif
};

static void lrg_openxr_service_vr_service_init (LrgVRServiceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgOpenXRService, lrg_openxr_service, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_VR_SERVICE,
                                                      lrg_openxr_service_vr_service_init))

#ifdef LRG_HAS_OPENXR

/* ----------------------------------------------------------------- helpers - */

static void
identity (gfloat *m)
{
    gint i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static gboolean
xr_ok (LrgOpenXRService  *self,
       XrResult           r,
       const gchar       *what,
       GError           **error)
{
    gchar buf[XR_MAX_RESULT_STRING_SIZE];

    if (XR_SUCCEEDED (r))
        return TRUE;

    buf[0] = '\0';
    if (self->instance != XR_NULL_HANDLE)
        xrResultToString (self->instance, r, buf);
    if (buf[0] == '\0')
        g_snprintf (buf, sizeof buf, "XrResult %d", (gint) r);

    if (error != NULL)
        g_set_error (error, LRG_VR_ERROR,
                     r == XR_ERROR_FORM_FACTOR_UNAVAILABLE
                         ? LRG_VR_ERROR_HMD_NOT_FOUND
                         : LRG_VR_ERROR_FAILED,
                     "%s failed: %s", what, buf);
    else
        lrg_debug (LRG_LOG_DOMAIN_VR, "%s failed: %s", what, buf);
    return FALSE;
}

/* Column-major 4x4 from an OpenXR pose (rotation then translation). */
static void
pose_to_matrix (const XrPosef *p,
                gfloat        *m)
{
    gfloat x = p->orientation.x, y = p->orientation.y;
    gfloat z = p->orientation.z, w = p->orientation.w;

    m[0]  = 1.0f - 2.0f * (y * y + z * z);
    m[1]  = 2.0f * (x * y + z * w);
    m[2]  = 2.0f * (x * z - y * w);
    m[3]  = 0.0f;
    m[4]  = 2.0f * (x * y - z * w);
    m[5]  = 1.0f - 2.0f * (x * x + z * z);
    m[6]  = 2.0f * (y * z + x * w);
    m[7]  = 0.0f;
    m[8]  = 2.0f * (x * z + y * w);
    m[9]  = 2.0f * (y * z - x * w);
    m[10] = 1.0f - 2.0f * (x * x + y * y);
    m[11] = 0.0f;
    m[12] = p->position.x;
    m[13] = p->position.y;
    m[14] = p->position.z;
    m[15] = 1.0f;
}

/* OpenGL projection (column-major, clip z in -1..1) for an asymmetric FOV. */
static void
fov_to_projection (const XrFovf *fov,
                   gfloat        near_clip,
                   gfloat        far_clip,
                   gfloat       *m)
{
    gfloat l = tanf (fov->angleLeft);
    gfloat r = tanf (fov->angleRight);
    gfloat u = tanf (fov->angleUp);
    gfloat d = tanf (fov->angleDown);
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

static gboolean
has_extension (const XrExtensionProperties *props,
               guint32                      n,
               const gchar                 *name)
{
    guint32 i;

    for (i = 0; i < n; i++)
        if (g_strcmp0 (props[i].extensionName, name) == 0)
            return TRUE;
    return FALSE;
}

static XrPath
str_path (LrgOpenXRService *self,
          const gchar      *s)
{
    XrPath p = XR_NULL_PATH;

    xrStringToPath (self->instance, s, &p);
    return p;
}

static const gchar *
state_name (XrSessionState s)
{
    switch (s)
    {
    case XR_SESSION_STATE_IDLE:         return "idle";
    case XR_SESSION_STATE_READY:        return "ready";
    case XR_SESSION_STATE_SYNCHRONIZED: return "synchronized";
    case XR_SESSION_STATE_VISIBLE:      return "visible";
    case XR_SESSION_STATE_FOCUSED:      return "focused";
    case XR_SESSION_STATE_STOPPING:     return "stopping";
    case XR_SESSION_STATE_LOSS_PENDING: return "loss-pending";
    case XR_SESSION_STATE_EXITING:      return "exiting";
    default:                            return "none";
    }
}

/* ------------------------------------------------------- graphics binding - */

typedef EGLContext (*LrgEglGetCurrentContext) (void);
typedef EGLDisplay (*LrgEglGetCurrentDisplay) (void);
typedef EGLBoolean (*LrgEglQueryContext) (EGLDisplay, EGLContext, EGLint, EGLint *);
typedef EGLBoolean (*LrgEglChooseConfig) (EGLDisplay, const EGLint *, EGLConfig *,
                                         EGLint, EGLint *);

/* An EGL entry point from the libEGL the process already has.  GLFW opens
   libEGL itself with dlopen (RTLD_LOCAL), so on Wayland its symbols are
   not in the global scope and dlsym (RTLD_DEFAULT) misses them: ask the
   already-loaded library by name then (RTLD_NOLOAD never loads a second
   one).  NULL when no libEGL is loaded at all. */
static gpointer
egl_symbol (const gchar *name)
{
    static void *egl = NULL;
    gpointer sym;

    sym = dlsym (RTLD_DEFAULT, name);
    if (sym != NULL)
        return sym;
    if (egl == NULL)
        egl = dlopen ("libEGL.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (egl == NULL)
        egl = dlopen ("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    return egl != NULL ? dlsym (egl, name) : NULL;
}

/* Create the session against whichever GL context raylib made current: GLX
   (GLFW's default on X11, e.g. gamescope's Xwayland) through
   XR_KHR_opengl_enable, or EGL (GLFW on Wayland) through XR_MNDX_egl_enable.
   EGL is resolved with dlsym so libregnum gains no hard libEGL dependency. */
static gboolean
create_session (LrgOpenXRService  *self,
                gboolean           have_egl_ext,
                GError           **error)
{
    XrSessionCreateInfo sci = { .type = XR_TYPE_SESSION_CREATE_INFO };
    GLXContext glx_ctx = glXGetCurrentContext ();

    sci.systemId = self->system;

    if (glx_ctx != NULL)
    {
        XrGraphicsBindingOpenGLXlibKHR gb = { .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR };
        Display *dpy = glXGetCurrentDisplay ();
        gint fb_id = 0, n = 0;
        gint attrs[3];
        GLXFBConfig *cfgs;
        XVisualInfo *vi;

        glXQueryContext (dpy, glx_ctx, GLX_FBCONFIG_ID, &fb_id);
        attrs[0] = GLX_FBCONFIG_ID;
        attrs[1] = fb_id;
        attrs[2] = None;
        cfgs = glXChooseFBConfig (dpy, DefaultScreen (dpy), attrs, &n);
        if (cfgs == NULL || n < 1)
        {
            g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_FAILED,
                         "cannot find the GLX framebuffer config of the "
                         "current context");
            return FALSE;
        }
        vi = glXGetVisualFromFBConfig (dpy, cfgs[0]);
        gb.xDisplay = dpy;
        gb.visualid = vi != NULL ? (guint32) vi->visualid : 0;
        gb.glxFBConfig = cfgs[0];
        gb.glxDrawable = glXGetCurrentDrawable ();
        gb.glxContext = glx_ctx;
        sci.next = &gb;
        if (vi != NULL)
            XFree (vi);
        XFree (cfgs);
        lrg_debug (LRG_LOG_DOMAIN_VR, "binding the session to a GLX context");
        return xr_ok (self, xrCreateSession (self->instance, &sci, &self->session),
                      "xrCreateSession (GLX)", error);
    }
    else
    {
        LrgEglGetCurrentContext get_ctx =
            (LrgEglGetCurrentContext) egl_symbol ("eglGetCurrentContext");
        LrgEglGetCurrentDisplay get_dpy =
            (LrgEglGetCurrentDisplay) egl_symbol ("eglGetCurrentDisplay");
        LrgEglQueryContext query =
            (LrgEglQueryContext) egl_symbol ("eglQueryContext");
        LrgEglChooseConfig choose =
            (LrgEglChooseConfig) egl_symbol ("eglChooseConfig");
        LrgXrEglGetProcAddress gpa =
            (LrgXrEglGetProcAddress) egl_symbol ("eglGetProcAddress");
        XrGraphicsBindingEGLMNDX gb = { .type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX };
        EGLContext ctx;
        EGLDisplay dpy;
        EGLint cfg_id = 0, n = 0;
        EGLint attrs[3];
        EGLConfig cfg = NULL;

        if (get_ctx == NULL || (ctx = get_ctx ()) == EGL_NO_CONTEXT)
        {
            g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_FAILED,
                         "no current GL context (neither GLX nor EGL)");
            return FALSE;
        }
        if (!have_egl_ext || get_dpy == NULL || query == NULL || choose == NULL
            || gpa == NULL)
        {
            g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                         "the GL context is EGL, and the runtime lacks "
                         "XR_MNDX_egl_enable");
            return FALSE;
        }
        dpy = get_dpy ();
        query (dpy, ctx, EGL_CONFIG_ID, &cfg_id);
        attrs[0] = EGL_CONFIG_ID;
        attrs[1] = cfg_id;
        attrs[2] = EGL_NONE;
        choose (dpy, attrs, &cfg, 1, &n);
        gb.getProcAddress = gpa;
        gb.display = dpy;
        gb.config = cfg;
        gb.context = ctx;
        sci.next = &gb;
        lrg_debug (LRG_LOG_DOMAIN_VR, "binding the session to an EGL context");
        return xr_ok (self, xrCreateSession (self->instance, &sci, &self->session),
                      "xrCreateSession (EGL)", error);
    }
}

/* -------------------------------------------------------------- swapchains - */

/* The runtime's recommended eye size times @scale, within what it allows. */
static guint32
scaled_extent (guint32 recommended,
               guint32 max,
               gfloat  scale)
{
    gdouble want = (gdouble) recommended * (gdouble) scale;

    if (want < 16.0)
        want = 16.0;
    if (max > 0 && want > (gdouble) max)
        want = (gdouble) max;
    return (guint32) (want + 0.5);
}

static gboolean create_swapchain (LrgOpenXRService  *self,
                                  LrgXrView         *view,
                                  guint32            width,
                                  guint32            height,
                                  gboolean           depth,
                                  GError           **error);

static gboolean
create_swapchains (LrgOpenXRService  *self,
                   GError           **error)
{
    gint64 formats[64];
    guint32 n_formats = 0, i, v;

    if (!xr_ok (self, xrEnumerateViewConfigurationViews (self->instance, self->system,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &self->view_count, NULL),
                "xrEnumerateViewConfigurationViews", error))
        return FALSE;
    if (self->view_count > LRG_XR_MAX_VIEWS)
        self->view_count = LRG_XR_MAX_VIEWS;
    for (v = 0; v < self->view_count; v++)
        self->config[v].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    if (!xr_ok (self, xrEnumerateViewConfigurationViews (self->instance, self->system,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, self->view_count,
                    &self->view_count, self->config),
                "xrEnumerateViewConfigurationViews", error))
        return FALSE;

    if (!xr_ok (self, xrEnumerateSwapchainFormats (self->session, G_N_ELEMENTS (formats),
                                                   &n_formats, formats),
                "xrEnumerateSwapchainFormats", error))
        return FALSE;
    self->format = 0;
    for (i = 0; i < n_formats; i++)
        if (formats[i] == LRG_GL_SRGB8_ALPHA8)
            self->format = formats[i];
    if (self->format == 0)
        for (i = 0; i < n_formats; i++)
            if (formats[i] == LRG_GL_RGBA8)
                self->format = formats[i];
    if (self->format == 0 && n_formats > 0)
        self->format = formats[0];

    for (v = 0; v < self->view_count; v++)
    {
        if (!create_swapchain (self, &self->views[v],
                               scaled_extent (self->config[v].recommendedImageRectWidth,
                                              self->config[v].maxImageRectWidth,
                                              self->resolution_scale),
                               scaled_extent (self->config[v].recommendedImageRectHeight,
                                              self->config[v].maxImageRectHeight,
                                              self->resolution_scale),
                               TRUE, error))
            return FALSE;
        lrg_debug (LRG_LOG_DOMAIN_VR, "view %u: %ux%u, %u images", v,
                   self->views[v].width, self->views[v].height,
                   self->views[v].n_images);
    }
    return TRUE;
}

/* One swapchain of @width x @height in the chosen format, with an FBO per
   image (and a depth renderbuffer per FBO when @depth).  On failure the
   parts made so far stay in @view for shutdown to free. */
static gboolean
create_swapchain (LrgOpenXRService  *self,
                  LrgXrView         *view,
                  guint32            width,
                  guint32            height,
                  gboolean           depth,
                  GError           **error)
{
    {
        XrSwapchainCreateInfo c = { .type = XR_TYPE_SWAPCHAIN_CREATE_INFO };
        XrSwapchainImageOpenGLKHR imgs[LRG_XR_MAX_IMAGES];
        guint32 k;

        c.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
                       | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        c.format = self->format;
        c.sampleCount = 1;
        c.width = width;
        c.height = height;
        c.faceCount = 1;
        c.arraySize = 1;
        c.mipCount = 1;
        if (!xr_ok (self, xrCreateSwapchain (self->session, &c, &view->swapchain),
                    "xrCreateSwapchain", error))
            return FALSE;
        view->width = c.width;
        view->height = c.height;

        for (k = 0; k < LRG_XR_MAX_IMAGES; k++)
        {
            memset (&imgs[k], 0, sizeof imgs[k]);
            imgs[k].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        }
        if (!xr_ok (self, xrEnumerateSwapchainImages (view->swapchain, LRG_XR_MAX_IMAGES,
                        &view->n_images, (XrSwapchainImageBaseHeader *) imgs),
                    "xrEnumerateSwapchainImages", error))
            return FALSE;

        /* One FBO per swapchain image, each with its own depth renderbuffer
           (rlUnloadFramebuffer deletes the depth attachment, so sharing one
           would double-free it). */
        for (k = 0; k < view->n_images; k++)
        {
            guint fbo = rlLoadFramebuffer ();

            view->images[k] = imgs[k].image;
            rlFramebufferAttach (fbo, imgs[k].image, RL_ATTACHMENT_COLOR_CHANNEL0,
                                 RL_ATTACHMENT_TEXTURE2D, 0);
            if (depth)
            {
                guint rb = rlLoadTextureDepth ((gint) view->width,
                                               (gint) view->height, true);

                rlFramebufferAttach (fbo, rb, RL_ATTACHMENT_DEPTH,
                                     RL_ATTACHMENT_RENDERBUFFER, 0);
            }
            if (!rlFramebufferComplete (fbo))
            {
                g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_COMPOSITOR,
                             "swapchain framebuffer %u is incomplete", k);
                rlUnloadFramebuffer (fbo);
                return FALSE;
            }
            view->fbos[k] = fbo;
        }
    }
    return TRUE;
}

/* ----------------------------------------------------------------- actions - */

typedef struct
{
    const gchar *profile;
    /* {action-name, path} pairs; the action is looked up by name. */
    const gchar *bindings[32][2];
} LrgXrProfile;

static XrAction
action_by_name (LrgOpenXRService *self,
                const gchar      *name)
{
    if (g_strcmp0 (name, "aim") == 0)             return self->act_aim;
    if (g_strcmp0 (name, "grip") == 0)            return self->act_grip;
    if (g_strcmp0 (name, "trigger") == 0)         return self->act_trigger;
    if (g_strcmp0 (name, "squeeze") == 0)         return self->act_squeeze;
    if (g_strcmp0 (name, "thumbstick") == 0)      return self->act_stick;
    if (g_strcmp0 (name, "thumbstick-click") == 0) return self->act_stick_click;
    if (g_strcmp0 (name, "primary") == 0)         return self->act_primary;
    if (g_strcmp0 (name, "secondary") == 0)       return self->act_secondary;
    if (g_strcmp0 (name, "menu") == 0)            return self->act_menu;
    if (g_strcmp0 (name, "x") == 0)               return self->act_x;
    if (g_strcmp0 (name, "y") == 0)               return self->act_y;
    if (g_strcmp0 (name, "bumper") == 0)          return self->act_bumper;
    if (g_strcmp0 (name, "dpad-up") == 0)         return self->act_dpad[0];
    if (g_strcmp0 (name, "dpad-down") == 0)       return self->act_dpad[1];
    if (g_strcmp0 (name, "dpad-left") == 0)       return self->act_dpad[2];
    if (g_strcmp0 (name, "dpad-right") == 0)      return self->act_dpad[3];
    if (g_strcmp0 (name, "view") == 0)            return self->act_view;
    if (g_strcmp0 (name, "haptic") == 0)          return self->act_haptic;
    return XR_NULL_HANDLE;
}

/* Suggested bindings.  The runtime picks the profile matching the attached
   controllers, and SteamVR remaps these onto controllers it has no exact
   suggestion for (its binding UI can then rebind anything), so suggesting the
   common profiles covers hardware not listed here. */
static const LrgXrProfile profiles[] = {
    { "/interaction_profiles/valve/index_controller", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
        { "grip", "/user/hand/left/input/grip/pose" },
        { "grip", "/user/hand/right/input/grip/pose" },
        { "trigger", "/user/hand/left/input/trigger/value" },
        { "trigger", "/user/hand/right/input/trigger/value" },
        { "squeeze", "/user/hand/left/input/squeeze/value" },
        { "squeeze", "/user/hand/right/input/squeeze/value" },
        { "thumbstick", "/user/hand/left/input/thumbstick" },
        { "thumbstick", "/user/hand/right/input/thumbstick" },
        { "thumbstick-click", "/user/hand/left/input/thumbstick/click" },
        { "thumbstick-click", "/user/hand/right/input/thumbstick/click" },
        { "primary", "/user/hand/left/input/a/click" },
        { "primary", "/user/hand/right/input/a/click" },
        { "secondary", "/user/hand/left/input/b/click" },
        { "secondary", "/user/hand/right/input/b/click" },
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
    /* The Steam Frame's controllers are a split gamepad: the right one has
       A, B, X, Y and menu, the left a D-pad and view, both a bumper.  The
       profile needs XR_VALVE_frame_controller_interaction (enabled when the
       runtime has it); without it SteamVR offers the Frame as an Oculus
       Touch, whose two face buttons per hand turn the right X and Y into
       B.  Suggested twice, with the grip spelled both ways OpenXR profiles
       use; a rejected suggestion leaves the other in place. */
    { "/interaction_profiles/valve/frame_controller_valve", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
        { "grip", "/user/hand/left/input/grip/pose" },
        { "grip", "/user/hand/right/input/grip/pose" },
        { "trigger", "/user/hand/left/input/trigger/value" },
        { "trigger", "/user/hand/right/input/trigger/value" },
        { "squeeze", "/user/hand/left/input/squeeze/value" },
        { "squeeze", "/user/hand/right/input/squeeze/value" },
        { "thumbstick", "/user/hand/left/input/thumbstick" },
        { "thumbstick", "/user/hand/right/input/thumbstick" },
        { "thumbstick-click", "/user/hand/left/input/thumbstick/click" },
        { "thumbstick-click", "/user/hand/right/input/thumbstick/click" },
        { "primary", "/user/hand/right/input/a/click" },
        { "secondary", "/user/hand/right/input/b/click" },
        { "x", "/user/hand/right/input/x/click" },
        { "y", "/user/hand/right/input/y/click" },
        { "menu", "/user/hand/right/input/menu/click" },
        { "bumper", "/user/hand/left/input/bumper/click" },
        { "bumper", "/user/hand/right/input/bumper/click" },
        { "dpad-up", "/user/hand/left/input/dpad_up/click" },
        { "dpad-down", "/user/hand/left/input/dpad_down/click" },
        { "dpad-left", "/user/hand/left/input/dpad_left/click" },
        { "dpad-right", "/user/hand/left/input/dpad_right/click" },
        { "view", "/user/hand/left/input/view/click" },
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
    { "/interaction_profiles/valve/frame_controller_valve", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
        { "grip", "/user/hand/left/input/grip/pose" },
        { "grip", "/user/hand/right/input/grip/pose" },
        { "trigger", "/user/hand/left/input/trigger/value" },
        { "trigger", "/user/hand/right/input/trigger/value" },
        { "squeeze", "/user/hand/left/input/grip/value" },
        { "squeeze", "/user/hand/right/input/grip/value" },
        { "thumbstick", "/user/hand/left/input/thumbstick" },
        { "thumbstick", "/user/hand/right/input/thumbstick" },
        { "thumbstick-click", "/user/hand/left/input/thumbstick/click" },
        { "thumbstick-click", "/user/hand/right/input/thumbstick/click" },
        { "primary", "/user/hand/right/input/a/click" },
        { "secondary", "/user/hand/right/input/b/click" },
        { "x", "/user/hand/right/input/x/click" },
        { "y", "/user/hand/right/input/y/click" },
        { "menu", "/user/hand/right/input/menu/click" },
        { "bumper", "/user/hand/left/input/bumper/click" },
        { "bumper", "/user/hand/right/input/bumper/click" },
        { "dpad-up", "/user/hand/left/input/dpad_up/click" },
        { "dpad-down", "/user/hand/left/input/dpad_down/click" },
        { "dpad-left", "/user/hand/left/input/dpad_left/click" },
        { "dpad-right", "/user/hand/left/input/dpad_right/click" },
        { "view", "/user/hand/left/input/view/click" },
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
    { "/interaction_profiles/oculus/touch_controller", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
        { "grip", "/user/hand/left/input/grip/pose" },
        { "grip", "/user/hand/right/input/grip/pose" },
        { "trigger", "/user/hand/left/input/trigger/value" },
        { "trigger", "/user/hand/right/input/trigger/value" },
        { "squeeze", "/user/hand/left/input/squeeze/value" },
        { "squeeze", "/user/hand/right/input/squeeze/value" },
        { "thumbstick", "/user/hand/left/input/thumbstick" },
        { "thumbstick", "/user/hand/right/input/thumbstick" },
        { "thumbstick-click", "/user/hand/left/input/thumbstick/click" },
        { "thumbstick-click", "/user/hand/right/input/thumbstick/click" },
        { "primary", "/user/hand/left/input/x/click" },
        { "primary", "/user/hand/right/input/a/click" },
        { "secondary", "/user/hand/left/input/y/click" },
        { "secondary", "/user/hand/right/input/b/click" },
        { "menu", "/user/hand/left/input/menu/click" },
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
    { "/interaction_profiles/htc/vive_controller", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
        { "grip", "/user/hand/left/input/grip/pose" },
        { "grip", "/user/hand/right/input/grip/pose" },
        { "trigger", "/user/hand/left/input/trigger/value" },
        { "trigger", "/user/hand/right/input/trigger/value" },
        { "squeeze", "/user/hand/left/input/squeeze/click" },
        { "squeeze", "/user/hand/right/input/squeeze/click" },
        { "thumbstick", "/user/hand/left/input/trackpad" },
        { "thumbstick", "/user/hand/right/input/trackpad" },
        { "thumbstick-click", "/user/hand/left/input/trackpad/click" },
        { "thumbstick-click", "/user/hand/right/input/trackpad/click" },
        { "menu", "/user/hand/left/input/menu/click" },
        { "secondary", "/user/hand/right/input/menu/click" },
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
    { "/interaction_profiles/khr/simple_controller", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
        { "grip", "/user/hand/left/input/grip/pose" },
        { "grip", "/user/hand/right/input/grip/pose" },
        { "trigger", "/user/hand/left/input/select/click" },
        { "trigger", "/user/hand/right/input/select/click" },
        { "menu", "/user/hand/left/input/menu/click" },
        { "secondary", "/user/hand/right/input/menu/click" },
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
};

/* @app_name as an OpenXR path component: lower-case letters, digits, '-',
   '_' and '.', never empty (falls back to "libregnum"). */
static void
action_set_name (const gchar *app_name,
                 gchar       *out,
                 gsize        size)
{
    gsize n = 0;
    const gchar *p;

    for (p = app_name; p != NULL && *p != '\0' && n + 1 < size; p++)
    {
        gchar c = g_ascii_tolower (*p);

        if (g_ascii_isalnum (c) || c == '-' || c == '_' || c == '.')
            out[n++] = c;
        else if (c == ' ' && n > 0)
            out[n++] = '-';
    }
    out[n] = '\0';
    if (n == 0)
        g_strlcpy (out, "libregnum", size);
}

static gboolean
make_action (LrgOpenXRService  *self,
             const gchar       *name,
             const gchar       *localized,
             XrActionType       type,
             XrAction          *out,
             GError           **error)
{
    XrActionCreateInfo ai = { .type = XR_TYPE_ACTION_CREATE_INFO };
    XrPath subs[2];

    subs[0] = self->hands[0].path;
    subs[1] = self->hands[1].path;
    g_strlcpy (ai.actionName, name, sizeof ai.actionName);
    g_strlcpy (ai.localizedActionName, localized, sizeof ai.localizedActionName);
    ai.actionType = type;
    ai.countSubactionPaths = 2;
    ai.subactionPaths = subs;
    return xr_ok (self, xrCreateAction (self->action_set, &ai, out),
                  "xrCreateAction", error);
}

static gboolean
create_actions (LrgOpenXRService  *self,
                GError           **error)
{
    XrActionSetCreateInfo si = { .type = XR_TYPE_ACTION_SET_CREATE_INFO };
    XrSessionActionSetsAttachInfo att = { .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    guint i, h;

    /* The action set is what SteamVR's binding UI lists and keys saved
       bindings by: name it after the application ("cmacs", "librewow"), in
       the lower-case path-safe form OpenXR requires. */
    action_set_name (self->app_name, si.actionSetName, sizeof si.actionSetName);
    g_strlcpy (si.localizedActionSetName,
               self->app_name != NULL && self->app_name[0] != '\0'
                   ? self->app_name : "libregnum",
               sizeof si.localizedActionSetName);
    if (!xr_ok (self, xrCreateActionSet (self->instance, &si, &self->action_set),
                "xrCreateActionSet", error))
        return FALSE;

    self->hands[0].path = str_path (self, "/user/hand/left");
    self->hands[1].path = str_path (self, "/user/hand/right");

    if (!make_action (self, "aim", "Pointer", XR_ACTION_TYPE_POSE_INPUT,
                      &self->act_aim, error)
        || !make_action (self, "grip-pose", "Hand", XR_ACTION_TYPE_POSE_INPUT,
                         &self->act_grip, error)
        || !make_action (self, "trigger", "Click", XR_ACTION_TYPE_FLOAT_INPUT,
                         &self->act_trigger, error)
        || !make_action (self, "squeeze", "Grab", XR_ACTION_TYPE_FLOAT_INPUT,
                         &self->act_squeeze, error)
        || !make_action (self, "thumbstick", "Scroll / move",
                         XR_ACTION_TYPE_VECTOR2F_INPUT, &self->act_stick, error)
        || !make_action (self, "thumbstick-click", "Stick press",
                         XR_ACTION_TYPE_BOOLEAN_INPUT, &self->act_stick_click, error)
        || !make_action (self, "primary", "Primary button",
                         XR_ACTION_TYPE_BOOLEAN_INPUT, &self->act_primary, error)
        || !make_action (self, "secondary", "Secondary button",
                         XR_ACTION_TYPE_BOOLEAN_INPUT, &self->act_secondary, error)
        || !make_action (self, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_menu, error)
        || !make_action (self, "x", "X button", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_x, error)
        || !make_action (self, "y", "Y button", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_y, error)
        || !make_action (self, "bumper", "Bumper", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_bumper, error)
        || !make_action (self, "dpad-up", "D-pad up", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_dpad[0], error)
        || !make_action (self, "dpad-down", "D-pad down", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_dpad[1], error)
        || !make_action (self, "dpad-left", "D-pad left", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_dpad[2], error)
        || !make_action (self, "dpad-right", "D-pad right", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_dpad[3], error)
        || !make_action (self, "view", "View", XR_ACTION_TYPE_BOOLEAN_INPUT,
                         &self->act_view, error)
        || !make_action (self, "haptic", "Haptics", XR_ACTION_TYPE_VIBRATION_OUTPUT,
                         &self->act_haptic, error))
        return FALSE;

    for (i = 0; i < G_N_ELEMENTS (profiles); i++)
    {
        XrActionSuggestedBinding sb[32];
        XrInteractionProfileSuggestedBinding s =
            { .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        guint n, b, pass;
        XrResult r = XR_SUCCESS;

        /* Pass 0 suggests everything; a runtime that rejects one path
           rejects the whole suggestion, so pass 1 retries without the grip
           pose -- the newest addition, and the one a vendor profile is most
           likely to spell differently -- rather than lose the profile. */
        for (pass = 0; pass < 2; pass++)
        {
            n = 0;
            for (b = 0; profiles[i].bindings[b][0] != NULL; b++)
            {
                if (pass == 1 && g_strcmp0 (profiles[i].bindings[b][0], "grip") == 0)
                    continue;
                sb[n].action = action_by_name (self, profiles[i].bindings[b][0]);
                sb[n].binding = str_path (self, profiles[i].bindings[b][1]);
                if (sb[n].binding != XR_NULL_PATH)
                    n++;
            }
            s.interactionProfile = str_path (self, profiles[i].profile);
            s.countSuggestedBindings = n;
            s.suggestedBindings = sb;
            r = xrSuggestInteractionProfileBindings (self->instance, &s);
            if (XR_SUCCEEDED (r))
                break;
        }
        /* A profile this runtime does not know is not an error. */
        if (XR_FAILED (r))
            lrg_debug (LRG_LOG_DOMAIN_VR, "no bindings for %s (XrResult %d)",
                       profiles[i].profile, (gint) r);
    }

    for (h = 0; h < 2; h++)
    {
        XrActionSpaceCreateInfo asi = { .type = XR_TYPE_ACTION_SPACE_CREATE_INFO };

        asi.action = self->act_aim;
        asi.subactionPath = self->hands[h].path;
        asi.poseInActionSpace.orientation.w = 1.0f;
        if (!xr_ok (self, xrCreateActionSpace (self->session, &asi,
                                               &self->hands[h].aim_space),
                    "xrCreateActionSpace", error))
            return FALSE;
        identity (self->hands[h].aim);

        /* The grip pose: where the palm is, for things worn on the hand. */
        asi.action = self->act_grip;
        if (!xr_ok (self, xrCreateActionSpace (self->session, &asi,
                                               &self->hands[h].grip_space),
                    "xrCreateActionSpace (grip)", error))
            return FALSE;
        identity (self->hands[h].grip);
    }

    att.countActionSets = 1;
    att.actionSets = &self->action_set;
    return xr_ok (self, xrAttachSessionActionSets (self->session, &att),
                  "xrAttachSessionActionSets", error);
}

static gboolean
bool_action (LrgOpenXRService *self,
             XrAction          action,
             XrPath            sub)
{
    XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateBoolean st = { .type = XR_TYPE_ACTION_STATE_BOOLEAN };

    gi.action = action;
    gi.subactionPath = sub;
    if (XR_FAILED (xrGetActionStateBoolean (self->session, &gi, &st)))
        return FALSE;
    return st.isActive && st.currentState;
}

static gfloat
float_action (LrgOpenXRService *self,
              XrAction          action,
              XrPath            sub)
{
    XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateFloat st = { .type = XR_TYPE_ACTION_STATE_FLOAT };

    gi.action = action;
    gi.subactionPath = sub;
    if (XR_FAILED (xrGetActionStateFloat (self->session, &gi, &st)) || !st.isActive)
        return 0.0f;
    return st.currentState;
}

/* Latch an analog value as a button with hysteresis. */
static void
latch (guint  *buttons,
       guint   bit,
       gfloat  value)
{
    if (value >= LRG_XR_PRESS)
        *buttons |= bit;
    else if (value <= LRG_XR_RELEASE)
        *buttons &= ~bit;
}

static void
sync_input (LrgOpenXRService *self,
            XrTime            when)
{
    XrActiveActionSet active = { 0 };
    XrActionsSyncInfo sync = { .type = XR_TYPE_ACTIONS_SYNC_INFO };
    guint h;

    /* Input only flows to the focused session; outside focus every control
       reads as released, so nothing stays latched down across a dashboard
       visit. */
    if (self->state != XR_SESSION_STATE_FOCUSED)
    {
        for (h = 0; h < 2; h++)
        {
            self->hands[h].buttons = 0;
            memset (self->hands[h].axes, 0, sizeof self->hands[h].axes);
            self->hands[h].tracked = FALSE;
            self->hands[h].grip_tracked = FALSE;
        }
        return;
    }

    active.actionSet = self->action_set;
    active.subactionPath = XR_NULL_PATH;
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED (xrSyncActions (self->session, &sync)))
        return;

    for (h = 0; h < 2; h++)
    {
        LrgXrHand *hand = &self->hands[h];
        XrSpaceLocation loc = { .type = XR_TYPE_SPACE_LOCATION };
        XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO };
        XrActionStateVector2f stick = { .type = XR_TYPE_ACTION_STATE_VECTOR2F };
        gfloat trig, grip;

        hand->tracked = FALSE;
        if (XR_SUCCEEDED (xrLocateSpace (hand->aim_space, self->local_space, when, &loc))
            && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
            && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        {
            pose_to_matrix (&loc.pose, hand->aim);
            hand->tracked = TRUE;
        }
        hand->grip_tracked = FALSE;
        loc.locationFlags = 0;
        if (hand->grip_space != XR_NULL_HANDLE
            && XR_SUCCEEDED (xrLocateSpace (hand->grip_space, self->local_space, when, &loc))
            && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
            && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        {
            pose_to_matrix (&loc.pose, hand->grip);
            hand->grip_tracked = TRUE;
        }

        trig = float_action (self, self->act_trigger, hand->path);
        grip = float_action (self, self->act_squeeze, hand->path);
        hand->axes[LRG_VR_AXIS_TRIGGER] = trig;
        hand->axes[LRG_VR_AXIS_GRIP] = grip;

        gi.action = self->act_stick;
        gi.subactionPath = hand->path;
        if (XR_SUCCEEDED (xrGetActionStateVector2f (self->session, &gi, &stick))
            && stick.isActive)
        {
            hand->axes[LRG_VR_AXIS_THUMBSTICK_X] = stick.currentState.x;
            hand->axes[LRG_VR_AXIS_THUMBSTICK_Y] = stick.currentState.y;
        }
        else
        {
            hand->axes[LRG_VR_AXIS_THUMBSTICK_X] = 0.0f;
            hand->axes[LRG_VR_AXIS_THUMBSTICK_Y] = 0.0f;
        }

        latch (&hand->buttons, LRG_VR_CONTROLLER_BUTTON_TRIGGER, trig);
        latch (&hand->buttons, LRG_VR_CONTROLLER_BUTTON_GRIP, grip);
#define SETBIT(bit, on) \
        do { if (on) hand->buttons |= (bit); else hand->buttons &= ~(guint) (bit); } while (0)
        SETBIT (LRG_VR_CONTROLLER_BUTTON_THUMBSTICK,
                bool_action (self, self->act_stick_click, hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_A,
                bool_action (self, self->act_primary, hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_B,
                bool_action (self, self->act_secondary, hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_MENU,
                bool_action (self, self->act_menu, hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_X,
                bool_action (self, self->act_x, hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_Y,
                bool_action (self, self->act_y, hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_BUMPER,
                bool_action (self, self->act_bumper, hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_DPAD_UP,
                bool_action (self, self->act_dpad[0], hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_DPAD_DOWN,
                bool_action (self, self->act_dpad[1], hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_DPAD_LEFT,
                bool_action (self, self->act_dpad[2], hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_DPAD_RIGHT,
                bool_action (self, self->act_dpad[3], hand->path));
        SETBIT (LRG_VR_CONTROLLER_BUTTON_VIEW,
                bool_action (self, self->act_view, hand->path));
#undef SETBIT
    }
}

/* ------------------------------------------------------------------- hands - */

/* Make a tracker per hand.  Hand tracking is a bonus: any failure leaves
   it off and the service otherwise working. */
static void
create_hand_trackers (LrgOpenXRService *self)
{
    guint h;

    self->hand_tracking = FALSE;
    if (!self->has_hand_ext)
        return;
    if (XR_FAILED (xrGetInstanceProcAddr (self->instance, "xrCreateHandTrackerEXT",
                       (PFN_xrVoidFunction *) &self->create_hand_tracker))
        || XR_FAILED (xrGetInstanceProcAddr (self->instance, "xrDestroyHandTrackerEXT",
                          (PFN_xrVoidFunction *) &self->destroy_hand_tracker))
        || XR_FAILED (xrGetInstanceProcAddr (self->instance, "xrLocateHandJointsEXT",
                          (PFN_xrVoidFunction *) &self->locate_hand_joints))
        || self->create_hand_tracker == NULL || self->destroy_hand_tracker == NULL
        || self->locate_hand_joints == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_VR, "hand tracking: entry points missing");
        return;
    }
    for (h = 0; h < 2; h++)
    {
        XrHandTrackerCreateInfoEXT ci = { .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };

        ci.hand = h == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        if (!xr_ok (self, self->create_hand_tracker (self->session, &ci,
                                                     &self->hand_trackers[h]),
                    "xrCreateHandTrackerEXT", NULL))
        {
            if (h == 1 && self->hand_trackers[0] != XR_NULL_HANDLE)
                self->destroy_hand_tracker (self->hand_trackers[0]);
            self->hand_trackers[0] = self->hand_trackers[1] = XR_NULL_HANDLE;
            return;
        }
    }
    self->hand_tracking = TRUE;
    lrg_info (LRG_LOG_DOMAIN_VR, "OpenXR: hand tracking on%s",
              self->has_hand_aim_ext ? " (with XR_FB_hand_tracking_aim)" : "");
}

static void
destroy_hand_trackers (LrgOpenXRService *self)
{
    guint h;

    for (h = 0; h < 2; h++)
    {
        if (self->hand_trackers[h] != XR_NULL_HANDLE && self->destroy_hand_tracker != NULL)
            self->destroy_hand_tracker (self->hand_trackers[h]);
        self->hand_trackers[h] = XR_NULL_HANDLE;
        lrg_vr_hand_state_reset (&self->hand_state[h]);
        lrg_vr_hand_state_reset (&self->pending_hands[h]);
    }
    self->hand_tracking = FALSE;
    self->pending_hands_valid = FALSE;
}

/* Locate both hands' joints at WHEN into OUT -- raw joints, plus the
   runtime's aim and pinch strength where XR_FB_hand_tracking_aim gives
   them; aim and latch are finished later by finish_hands.  Called on the
   frame-wait thread (or begin_frame's, unthreaded); touches nothing in
   SELF but the immutable trackers. */
static void
locate_hands (LrgOpenXRService *self,
              XrTime            when,
              LrgVRHandState   *out)
{
    guint h, j;

    for (h = 0; h < 2; h++)
    {
        XrHandJointsLocateInfoEXT li = { .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
        XrHandJointLocationsEXT locs = { .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
        XrHandTrackingAimStateFB aim = { .type = XR_TYPE_HAND_TRACKING_AIM_STATE_FB };
        XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
        LrgVRHandState *st = &out[h];

        lrg_vr_hand_state_reset (st);
        if (self->hand_trackers[h] == XR_NULL_HANDLE)
            continue;

        memset (joints, 0, sizeof joints);
        li.baseSpace = self->local_space;
        li.time = when;
        locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locs.jointLocations = joints;
        if (self->has_hand_aim_ext)
            locs.next = &aim;
        if (XR_FAILED (self->locate_hand_joints (self->hand_trackers[h], &li, &locs))
            || !locs.isActive)
            continue;

        st->active = TRUE;
        for (j = 0; j < XR_HAND_JOINT_COUNT_EXT && j < LRG_VR_HAND_JOINT_COUNT; j++)
        {
            const XrHandJointLocationEXT *jl = &joints[j];

            if (jl->locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
            {
                st->valid |= 1u << j;
                st->positions[j * 3 + 0] = jl->pose.position.x;
                st->positions[j * 3 + 1] = jl->pose.position.y;
                st->positions[j * 3 + 2] = jl->pose.position.z;
            }
            if (jl->locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
            {
                st->orientations[j * 4 + 0] = jl->pose.orientation.x;
                st->orientations[j * 4 + 1] = jl->pose.orientation.y;
                st->orientations[j * 4 + 2] = jl->pose.orientation.z;
                st->orientations[j * 4 + 3] = jl->pose.orientation.w;
            }
            st->radii[j] = jl->radius;
        }
        if (self->has_hand_aim_ext && (aim.status & XR_HAND_TRACKING_AIM_COMPUTED_BIT_FB))
        {
            pose_to_matrix (&aim.aimPose, st->aim);
            st->aim_valid = (aim.status & XR_HAND_TRACKING_AIM_VALID_BIT_FB) != 0;
            st->aim_from_runtime = TRUE;
            st->pinch_strength = CLAMP (aim.pinchStrengthIndex, 0.0f, 1.0f);
        }
    }
}

/* Finish this frame's hands on the caller's thread: an unfocused session
   has no hands (as it has no controllers, see sync_input), and each hand
   gets its aim (from the head just located) and its pinch latch, whose
   history WAS holds. */
static void
finish_hands (LrgOpenXRService *self,
              const gboolean   *was)
{
    guint h;

    for (h = 0; h < 2; h++)
    {
        LrgVRHandState *st = &self->hand_state[h];

        if (self->state != XR_SESSION_STATE_FOCUSED)
            lrg_vr_hand_state_reset (st);
        lrg_vr_hand_state_compute_aim (st, (LrgVRHand) h, self->hmd);
        lrg_vr_hand_state_update_pinch (st, was[h]);
    }
}

/* ------------------------------------------------------------------ events - */

static void
handle_state (LrgOpenXRService *self,
              XrSessionState    state)
{
    self->state = state;
    lrg_debug (LRG_LOG_DOMAIN_VR, "session state: %s", state_name (state));

    switch (state)
    {
    case XR_SESSION_STATE_READY:
    {
        XrSessionBeginInfo bi = { .type = XR_TYPE_SESSION_BEGIN_INFO };

        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (xr_ok (self, xrBeginSession (self->session, &bi), "xrBeginSession", NULL))
        {
            g_mutex_lock (&self->frame_lock);
            self->running = TRUE;
            self->want_wait = TRUE;
            self->frame_ready = FALSE;
            g_cond_broadcast (&self->frame_cond);
            g_mutex_unlock (&self->frame_lock);
        }
        break;
    }
    case XR_SESSION_STATE_STOPPING:
        /* xrEndSession must not race the waiter's xrWaitFrame: defer it
           until the waiter is idle (try_end_session). */
        g_mutex_lock (&self->frame_lock);
        self->running = FALSE;
        self->end_pending = TRUE;
        g_mutex_unlock (&self->frame_lock);
        break;
    case XR_SESSION_STATE_EXITING:
    case XR_SESSION_STATE_LOSS_PENDING:
        self->running = FALSE;
        self->quit_requested = TRUE;
        break;
    default:
        break;
    }
}

static void
try_end_session (LrgOpenXRService *self)
{
    gboolean end = FALSE;

    g_mutex_lock (&self->frame_lock);
    if (self->end_pending && !self->waiter_busy && !self->frame_begun)
    {
        self->end_pending = FALSE;
        self->frame_ready = FALSE;
        end = TRUE;
    }
    g_mutex_unlock (&self->frame_lock);
    if (end)
        xr_ok (self, xrEndSession (self->session), "xrEndSession", NULL);
}

static void
poll_xr_events (LrgOpenXRService *self)
{
    XrEventDataBuffer ev;

    if (self->instance == XR_NULL_HANDLE)
        return;

    try_end_session (self);

    for (;;)
    {
        memset (&ev, 0, sizeof ev);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        if (xrPollEvent (self->instance, &ev) != XR_SUCCESS)
            break;

        switch (ev.type)
        {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            handle_state (self, ((XrEventDataSessionStateChanged *) &ev)->state);
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            self->running = FALSE;
            self->quit_requested = TRUE;
            break;
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            lrg_debug (LRG_LOG_DOMAIN_VR, "interaction profile changed");
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            /* SteamVR's "recenter" (a long press of the Steam button) moves
               the LOCAL origin under the app; anything the app anchored in
               LOCAL space (a panel, its own recentre) wants to follow. */
            if (((XrEventDataReferenceSpaceChangePending *) &ev)->referenceSpaceType
                == XR_REFERENCE_SPACE_TYPE_LOCAL)
            {
                lrg_debug (LRG_LOG_DOMAIN_VR, "LOCAL space recentred");
                self->recentered = TRUE;
            }
            break;
        default:
            break;
        }
    }
    try_end_session (self);
}

/* ------------------------------------------------------------ frame waiter */

static gpointer
frame_waiter (gpointer data)
{
    LrgOpenXRService *self = data;
    LrgVRHandState located[2];

    g_mutex_lock (&self->frame_lock);
    for (;;)
    {
        XrFrameWaitInfo wi = { .type = XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState fs = { .type = XR_TYPE_FRAME_STATE };
        XrResult r;

        while (!self->waiter_quit
               && !(self->running && self->want_wait && !self->end_pending))
            g_cond_wait (&self->frame_cond, &self->frame_lock);
        if (self->waiter_quit)
            break;

        self->want_wait = FALSE;
        self->waiter_busy = TRUE;
        g_mutex_unlock (&self->frame_lock);

        r = xrWaitFrame (self->session, &wi, &fs);
        /* The hands for the frame just waited for, still outside the lock
           and still "busy" so the session cannot end under the locate. */
        if (XR_SUCCEEDED (r) && self->hand_tracking)
            locate_hands (self, fs.predictedDisplayTime, located);

        g_mutex_lock (&self->frame_lock);
        self->waiter_busy = FALSE;
        if (XR_SUCCEEDED (r))
        {
            guint64 one = 1;

            self->pending_state = fs;
            if (self->hand_tracking)
            {
                memcpy (self->pending_hands, located, sizeof self->pending_hands);
                self->pending_hands_valid = TRUE;
            }
            self->frame_ready = TRUE;
            if (write (self->frame_fd, &one, sizeof one) < 0 && errno != EAGAIN)
                lrg_debug (LRG_LOG_DOMAIN_VR, "frame fd write: %s",
                           g_strerror (errno));
        }
        else
        {
            /* Not running any more (or lost): wait to be re-armed. */
            self->want_wait = self->running;
            g_mutex_unlock (&self->frame_lock);
            g_usleep (5000);
            g_mutex_lock (&self->frame_lock);
        }
        g_cond_broadcast (&self->frame_cond);
    }
    g_mutex_unlock (&self->frame_lock);
    return NULL;
}

static void
stop_frame_thread (LrgOpenXRService *self)
{
    if (self->waiter == NULL)
        return;
    g_mutex_lock (&self->frame_lock);
    self->waiter_quit = TRUE;
    g_cond_broadcast (&self->frame_cond);
    g_mutex_unlock (&self->frame_lock);
    g_thread_join (self->waiter);
    self->waiter = NULL;
    if (self->frame_fd >= 0)
        close (self->frame_fd);
    self->frame_fd = -1;
    self->waiter_quit = FALSE;
}

/* ---------------------------------------------------------- vfunc: lifecycle */

static void lrg_openxr_service_real_shutdown (LrgVRService *service);

static gboolean
lrg_openxr_service_real_initialize (LrgVRService  *service,
                                    GError       **error)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    XrExtensionProperties *props = NULL;
    guint32 n_props = 0, i;
    const gchar *exts[10];
    guint32 n_exts = 0;
    gboolean have_egl;
    XrInstanceCreateInfo ici = { .type = XR_TYPE_INSTANCE_CREATE_INFO };
    XrInstanceProperties ip = { .type = XR_TYPE_INSTANCE_PROPERTIES };
    XrSystemGetInfo sgi = { .type = XR_TYPE_SYSTEM_GET_INFO };
    XrSystemProperties sp = { .type = XR_TYPE_SYSTEM_PROPERTIES };
    XrSystemHandTrackingPropertiesEXT hand_props =
        { .type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
    PFN_xrGetOpenGLGraphicsRequirementsKHR get_req = NULL;
    XrGraphicsRequirementsOpenGLKHR req = { .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    XrReferenceSpaceCreateInfo rsci = { .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO };

    if (self->initialized)
        return TRUE;

    if (XR_FAILED (xrEnumerateInstanceExtensionProperties (NULL, 0, &n_props, NULL))
        || n_props == 0)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                     "no OpenXR runtime (is SteamVR or Monado installed and "
                     "set as the active runtime?)");
        return FALSE;
    }
    props = g_new0 (XrExtensionProperties, n_props);
    for (i = 0; i < n_props; i++)
        props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    xrEnumerateInstanceExtensionProperties (NULL, n_props, &n_props, props);

    if (!has_extension (props, n_props, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)
        && !has_extension (props, n_props, XR_MNDX_EGL_ENABLE_EXTENSION_NAME))
    {
        g_free (props);
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                     "the OpenXR runtime supports neither OpenGL nor EGL");
        return FALSE;
    }
    if (has_extension (props, n_props, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
        exts[n_exts++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
    have_egl = has_extension (props, n_props, XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
    if (have_egl)
        exts[n_exts++] = XR_MNDX_EGL_ENABLE_EXTENSION_NAME;
    self->has_refresh_rate_ext =
        has_extension (props, n_props, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (self->has_refresh_rate_ext)
        exts[n_exts++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    /* The Steam Frame's own controller profile (see profiles[]). */
    if (has_extension (props, n_props, "XR_VALVE_frame_controller_interaction"))
        exts[n_exts++] = "XR_VALVE_frame_controller_interaction";
    /* Articulated hands, and the runtime's own aim and pinch for them. */
    self->has_hand_ext = has_extension (props, n_props, XR_EXT_HAND_TRACKING_EXTENSION_NAME);
    if (self->has_hand_ext)
        exts[n_exts++] = XR_EXT_HAND_TRACKING_EXTENSION_NAME;
    self->has_hand_aim_ext = self->has_hand_ext
        && has_extension (props, n_props, XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
    if (self->has_hand_aim_ext)
        exts[n_exts++] = XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME;
    /* Curved panels (lrg_openxr_service_submit_layer_cylinder). */
    self->has_cylinder_ext =
        has_extension (props, n_props, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    if (self->has_cylinder_ext)
        exts[n_exts++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;
    g_free (props);

    g_strlcpy (ici.applicationInfo.applicationName,
               self->app_name != NULL ? self->app_name : "libregnum",
               sizeof ici.applicationInfo.applicationName);
    ici.applicationInfo.applicationVersion = 1;
    g_strlcpy (ici.applicationInfo.engineName, "libregnum",
               sizeof ici.applicationInfo.engineName);
    ici.applicationInfo.engineVersion = 1;
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION (1, 0, 0);
    ici.enabledExtensionCount = n_exts;
    ici.enabledExtensionNames = exts;
    if (!xr_ok (self, xrCreateInstance (&ici, &self->instance), "xrCreateInstance",
                error))
        goto fail;

    if (XR_SUCCEEDED (xrGetInstanceProperties (self->instance, &ip)))
        self->runtime_name = g_strdup_printf ("%s %u.%u.%u", ip.runtimeName,
                                              XR_VERSION_MAJOR (ip.runtimeVersion),
                                              XR_VERSION_MINOR (ip.runtimeVersion),
                                              XR_VERSION_PATCH (ip.runtimeVersion));

    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xr_ok (self, xrGetSystem (self->instance, &sgi, &self->system),
                "xrGetSystem", error))
        goto fail;
    if (self->has_hand_ext)
        sp.next = &hand_props;
    if (XR_SUCCEEDED (xrGetSystemProperties (self->instance, self->system, &sp)))
        self->system_name = g_strdup (sp.systemName);
    /* The extension can be present with no hand-tracking hardware. */
    if (self->has_hand_ext && !hand_props.supportsHandTracking)
    {
        lrg_debug (LRG_LOG_DOMAIN_VR, "runtime has %s, the system cannot track hands",
                   XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        self->has_hand_ext = FALSE;
        self->has_hand_aim_ext = FALSE;
    }

    /* Required before xrCreateSession, even though we accept any version --
       on the EGL path too: SteamVR answers xrCreateSession with
       XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING without it (seen on the
       Steam Frame, GLFW on gamescope's Wayland).  A runtime that offers
       EGL without XR_KHR_opengl_enable has nothing to call. */
    if (XR_SUCCEEDED (xrGetInstanceProcAddr (self->instance,
                          "xrGetOpenGLGraphicsRequirementsKHR",
                          (PFN_xrVoidFunction *) &get_req))
        && get_req != NULL)
    {
        if (!xr_ok (self, get_req (self->instance, self->system, &req),
                    "xrGetOpenGLGraphicsRequirementsKHR", error))
            goto fail;
    }
    else if (glXGetCurrentContext () != NULL)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_FAILED,
                     "runtime does not expose xrGetOpenGLGraphicsRequirementsKHR");
        goto fail;
    }

    if (!create_session (self, have_egl, error))
        goto fail;

    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (!xr_ok (self, xrCreateReferenceSpace (self->session, &rsci, &self->local_space),
                "xrCreateReferenceSpace (LOCAL)", error))
        goto fail;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!xr_ok (self, xrCreateReferenceSpace (self->session, &rsci, &self->view_space),
                "xrCreateReferenceSpace (VIEW)", error))
        goto fail;

    if (!create_swapchains (self, error) || !create_actions (self, error))
        goto fail;
    create_hand_trackers (self);

    self->refresh_rate = 90.0f;
    if (self->has_refresh_rate_ext)
    {
        PFN_xrGetDisplayRefreshRateFB get_rate = NULL;
        gfloat rate = 0.0f;

        if (XR_SUCCEEDED (xrGetInstanceProcAddr (self->instance,
                              "xrGetDisplayRefreshRateFB",
                              (PFN_xrVoidFunction *) &get_rate))
            && get_rate != NULL
            && XR_SUCCEEDED (get_rate (self->session, &rate)) && rate > 1.0f)
            self->refresh_rate = rate;
    }

    identity (self->hmd);
    self->initialized = TRUE;
    lrg_info (LRG_LOG_DOMAIN_VR, "OpenXR: %s on %s, %u views at %.0f Hz",
              self->runtime_name != NULL ? self->runtime_name : "?",
              self->system_name != NULL ? self->system_name : "?",
              self->view_count, self->refresh_rate);

    /* Pick up the IDLE -> READY transition now, so the session is already
       begun by the first frame. */
    poll_xr_events (self);
    return TRUE;

fail:
    lrg_openxr_service_real_shutdown (service);
    return FALSE;
}

static void
lrg_openxr_service_real_shutdown (LrgVRService *service)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    guint v, k, h;

    stop_frame_thread (self);
    if (self->session != XR_NULL_HANDLE && self->running)
    {
        xrRequestExitSession (self->session);
        self->running = FALSE;
    }

    for (v = 0; v < LRG_XR_MAX_VIEWS; v++)
    {
        LrgXrView *view = &self->views[v];

        for (k = 0; k < view->n_images; k++)
            if (view->fbos[k] != 0)
                rlUnloadFramebuffer (view->fbos[k]);
        if (view->swapchain != XR_NULL_HANDLE)
            xrDestroySwapchain (view->swapchain);
        memset (view, 0, sizeof *view);
    }
    for (v = 0; v < LRG_XR_MAX_LAYERS; v++)
    {
        LrgXrLayer *layer = &self->layers[v];

        for (k = 0; k < layer->n_images; k++)
            if (layer->fbos[k] != 0)
                rlUnloadFramebuffer (layer->fbos[k]);
        if (layer->swapchain != XR_NULL_HANDLE)
            xrDestroySwapchain (layer->swapchain);
        memset (layer, 0, sizeof *layer);
        self->layer_valid[v] = FALSE;
    }
    self->n_layers = 0;
    self->n_placed = 0;
    self->has_cylinder_ext = FALSE;
    for (h = 0; h < 2; h++)
    {
        if (self->hands[h].aim_space != XR_NULL_HANDLE)
            xrDestroySpace (self->hands[h].aim_space);
        if (self->hands[h].grip_space != XR_NULL_HANDLE)
            xrDestroySpace (self->hands[h].grip_space);
    }
    memset (self->hands, 0, sizeof self->hands);
    destroy_hand_trackers (self);
    self->has_hand_ext = self->has_hand_aim_ext = FALSE;

    if (self->action_set != XR_NULL_HANDLE)
        xrDestroyActionSet (self->action_set);   /* destroys its actions too */
    self->action_set = XR_NULL_HANDLE;
    if (self->view_space != XR_NULL_HANDLE)
        xrDestroySpace (self->view_space);
    if (self->local_space != XR_NULL_HANDLE)
        xrDestroySpace (self->local_space);
    self->view_space = self->local_space = XR_NULL_HANDLE;
    if (self->session != XR_NULL_HANDLE)
        xrDestroySession (self->session);
    self->session = XR_NULL_HANDLE;
    if (self->instance != XR_NULL_HANDLE)
        xrDestroyInstance (self->instance);
    self->instance = XR_NULL_HANDLE;

    self->frame_begun = FALSE;
    self->view_count = 0;
    self->initialized = FALSE;
}

static gboolean
lrg_openxr_service_real_is_available (LrgVRService *service)
{
    guint32 n = 0;

    (void) service;
    return XR_SUCCEEDED (xrEnumerateInstanceExtensionProperties (NULL, 0, &n, NULL))
           && n > 0;
}

static gboolean
lrg_openxr_service_real_is_hmd_present (LrgVRService *service)
{
    return LRG_OPENXR_SERVICE (service)->initialized;
}

static void
lrg_openxr_service_real_poll_events (LrgVRService *service)
{
    poll_xr_events (LRG_OPENXR_SERVICE (service));
}

/* --------------------------------------------------------- vfunc: rendering */

static void
lrg_openxr_service_real_get_recommended_render_size (LrgVRService *service,
                                                     guint        *width,
                                                     guint        *height)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);

    if (width != NULL)
        *width = self->view_count > 0 ? self->config[0].recommendedImageRectWidth : 0;
    if (height != NULL)
        *height = self->view_count > 0 ? self->config[0].recommendedImageRectHeight : 0;
}

static void
lrg_openxr_service_real_get_eye_projection (LrgVRService *service,
                                            LrgVREye      eye,
                                            gfloat        near_clip,
                                            gfloat        far_clip,
                                            gfloat       *matrix)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    guint v = (guint) eye;

    if (v >= self->view_count || !self->views_valid)
    {
        identity (matrix);
        return;
    }
    fov_to_projection (&self->located[v].fov, near_clip, far_clip, matrix);
}

static void
lrg_openxr_service_real_get_eye_to_head (LrgVRService *service,
                                         LrgVREye      eye,
                                         gfloat       *matrix)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    XrViewLocateInfo li = { .type = XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState vs = { .type = XR_TYPE_VIEW_STATE };
    XrView views[LRG_XR_MAX_VIEWS];
    guint32 n = 0, i;

    identity (matrix);
    if (!self->running || (guint) eye >= self->view_count)
        return;
    for (i = 0; i < LRG_XR_MAX_VIEWS; i++)
    {
        memset (&views[i], 0, sizeof views[i]);
        views[i].type = XR_TYPE_VIEW;
    }
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = self->frame_state.predictedDisplayTime;
    li.space = self->view_space;
    if (XR_SUCCEEDED (xrLocateViews (self->session, &li, &vs, LRG_XR_MAX_VIEWS, &n,
                                     views))
        && (guint) eye < n)
        pose_to_matrix (&views[eye].pose, matrix);
}

static void
lrg_openxr_service_real_get_hmd_pose (LrgVRService *service,
                                      gfloat       *matrix)
{
    memcpy (matrix, LRG_OPENXR_SERVICE (service)->hmd, 16 * sizeof (gfloat));
}

static gboolean
lrg_openxr_service_real_submit_frame (LrgVRService  *service,
                                      LrgVREye       eye,
                                      guint          texture_id,
                                      GError       **error)
{
    (void) service; (void) eye; (void) texture_id;
    g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_COMPOSITOR,
                 "OpenXR paces its own frames: render through begin_frame / "
                 "begin_view / end_view / end_frame");
    return FALSE;
}

static gboolean
lrg_openxr_service_real_begin_frame (LrgVRService *service)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    XrFrameWaitInfo wi = { .type = XR_TYPE_FRAME_WAIT_INFO };
    XrFrameBeginInfo bi = { .type = XR_TYPE_FRAME_BEGIN_INFO };
    XrViewLocateInfo li = { .type = XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState vs = { .type = XR_TYPE_VIEW_STATE };
    XrSpaceLocation head = { .type = XR_TYPE_SPACE_LOCATION };
    guint32 n = 0, i;
    gboolean was[2];

    if (!self->initialized)
        return FALSE;

    poll_xr_events (self);
    if (!self->running || self->frame_begun)
        return FALSE;

    if (self->waiter != NULL)
    {
        /* Threaded: take the frame the waiter already waited for, or return
           at once -- never block the caller. */
        guint64 drained;

        g_mutex_lock (&self->frame_lock);
        if (!self->frame_ready)
        {
            g_mutex_unlock (&self->frame_lock);
            return FALSE;
        }
        self->frame_state = self->pending_state;
        self->frame_ready = FALSE;
        was[0] = self->hand_state[0].pinched;
        was[1] = self->hand_state[1].pinched;
        if (self->pending_hands_valid)
        {
            memcpy (self->hand_state, self->pending_hands, sizeof self->hand_state);
            self->pending_hands_valid = FALSE;
        }
        g_mutex_unlock (&self->frame_lock);
        while (read (self->frame_fd, &drained, sizeof drained) > 0)
            ;
    }
    else
    {
        memset (&self->frame_state, 0, sizeof self->frame_state);
        self->frame_state.type = XR_TYPE_FRAME_STATE;
        if (!xr_ok (self, xrWaitFrame (self->session, &wi, &self->frame_state),
                    "xrWaitFrame", NULL))
            return FALSE;
        was[0] = self->hand_state[0].pinched;
        was[1] = self->hand_state[1].pinched;
        if (self->hand_tracking)
            locate_hands (self, self->frame_state.predictedDisplayTime,
                          self->hand_state);
    }
    if (!xr_ok (self, xrBeginFrame (self->session, &bi), "xrBeginFrame", NULL))
    {
        g_mutex_lock (&self->frame_lock);
        self->want_wait = TRUE;
        g_cond_broadcast (&self->frame_cond);
        g_mutex_unlock (&self->frame_lock);
        return FALSE;
    }
    self->frame_begun = TRUE;
    self->views_rendered = 0;
    self->n_placed = 0;

    for (i = 0; i < LRG_XR_MAX_VIEWS; i++)
    {
        memset (&self->located[i], 0, sizeof self->located[i]);
        self->located[i].type = XR_TYPE_VIEW;
    }
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = self->frame_state.predictedDisplayTime;
    li.space = self->local_space;
    self->views_valid =
        XR_SUCCEEDED (xrLocateViews (self->session, &li, &vs, LRG_XR_MAX_VIEWS, &n,
                                     self->located))
        && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)
        && n >= self->view_count;

    if (XR_SUCCEEDED (xrLocateSpace (self->view_space, self->local_space,
                                     self->frame_state.predictedDisplayTime, &head))
        && (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        pose_to_matrix (&head.pose, self->hmd);

    sync_input (self, self->frame_state.predictedDisplayTime);
    if (self->hand_tracking)
        finish_hands (self, was);
    return TRUE;
}

static guint
lrg_openxr_service_real_get_view_count (LrgVRService *service)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);

    if (!self->frame_begun)
        return self->view_count;
    return (self->frame_state.shouldRender && self->views_valid) ? self->view_count : 0;
}

static gboolean
lrg_openxr_service_real_begin_view (LrgVRService *service,
                                    guint         v,
                                    gfloat        near_clip,
                                    gfloat        far_clip,
                                    gfloat       *eye_pose,
                                    gfloat       *projection,
                                    guint        *framebuffer,
                                    guint        *width,
                                    guint        *height)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    XrSwapchainImageAcquireInfo ai = { .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    LrgXrView *view;

    if (!self->frame_begun || v >= self->view_count || !self->views_valid)
        return FALSE;
    view = &self->views[v];
    if (view->open)
        return FALSE;

    if (!xr_ok (self, xrAcquireSwapchainImage (view->swapchain, &ai, &view->acquired),
                "xrAcquireSwapchainImage", NULL))
        return FALSE;
    wi.timeout = XR_INFINITE_DURATION;
    if (!xr_ok (self, xrWaitSwapchainImage (view->swapchain, &wi),
                "xrWaitSwapchainImage", NULL))
    {
        XrSwapchainImageReleaseInfo ri = { .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage (view->swapchain, &ri);
        return FALSE;
    }
    view->open = TRUE;

    rlEnableFramebuffer (view->fbos[view->acquired]);
    rlViewport (0, 0, (gint) view->width, (gint) view->height);

    pose_to_matrix (&self->located[v].pose, eye_pose);
    fov_to_projection (&self->located[v].fov, near_clip, far_clip, projection);
    *framebuffer = view->fbos[view->acquired];
    *width = view->width;
    *height = view->height;
    return TRUE;
}

static void
lrg_openxr_service_real_end_view (LrgVRService *service,
                                  guint         v)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    XrSwapchainImageReleaseInfo ri = { .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XrCompositionLayerProjectionView *lv;
    LrgXrView *view;

    if (v >= self->view_count)
        return;
    view = &self->views[v];
    if (!view->open)
        return;

    rlDrawRenderBatchActive ();
    rlDisableFramebuffer ();
    xrReleaseSwapchainImage (view->swapchain, &ri);
    /* Flush AFTER the release: SteamVR's GL path records a semaphore signal
       on the GL queue inside xrReleaseSwapchainImage and waits on it in
       xrEndFrame.  A driver that defers submission until a flush or swap --
       zink does -- otherwise leaves that signal unsubmitted, xrEndFrame
       waits forever, and the editor hangs.  A flush before the release
       does not cover it. */
    glFlush ();
    view->open = FALSE;

    lv = &self->layer_views[v];
    memset (lv, 0, sizeof *lv);
    lv->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    lv->pose = self->located[v].pose;
    lv->fov = self->located[v].fov;
    lv->subImage.swapchain = view->swapchain;
    lv->subImage.imageRect.offset.x = 0;
    lv->subImage.imageRect.offset.y = 0;
    lv->subImage.imageRect.extent.width = (gint32) view->width;
    lv->subImage.imageRect.extent.height = (gint32) view->height;
    self->views_rendered |= 1u << v;
}

static void
lrg_openxr_service_real_end_frame (LrgVRService *service)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    XrFrameEndInfo ei = { .type = XR_TYPE_FRAME_END_INFO };
    XrCompositionLayerProjection layer = { .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    const XrCompositionLayerBaseHeader *layers[1 + LRG_XR_MAX_PLACED];
    guint all = (1u << self->view_count) - 1u;
    guint v, n = 0;

    if (!self->frame_begun)
        return;

    /* A view or layer begun but never ended still holds its image. */
    for (v = 0; v < self->view_count; v++)
        if (self->views[v].open)
            lrg_openxr_service_real_end_view (service, v);
    for (v = 0; v < self->n_layers; v++)
        if (self->layers[v].open)
            lrg_openxr_service_end_layer (self, v + 1);

    layer.space = self->local_space;
    layer.viewCount = self->view_count;
    layer.views = self->layer_views;

    /* The projection layer only if every view was drawn this frame; the
       flat layers on top of it, in submission order (and on their own when
       the views were not drawn -- a menu panel still shows over black). */
    if (self->frame_state.shouldRender)
    {
        if (self->views_rendered == all)
            layers[n++] = (const XrCompositionLayerBaseHeader *) &layer;
        for (v = 0; v < self->n_placed; v++)
            layers[n++] = &self->placed[v].base;
    }

    ei.displayTime = self->frame_state.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = n;
    ei.layers = layers;
    xr_ok (self, xrEndFrame (self->session, &ei), "xrEndFrame", NULL);
    self->frame_begun = FALSE;
    self->n_placed = 0;

    g_mutex_lock (&self->frame_lock);
    self->want_wait = TRUE;
    g_cond_broadcast (&self->frame_cond);
    g_mutex_unlock (&self->frame_lock);
}

static gboolean
lrg_openxr_service_real_is_session_running (LrgVRService *service)
{
    return LRG_OPENXR_SERVICE (service)->running;
}

static gboolean
lrg_openxr_service_real_should_quit (LrgVRService *service)
{
    return LRG_OPENXR_SERVICE (service)->quit_requested;
}

/* ------------------------------------------------------- vfunc: controllers */

static void
lrg_openxr_service_real_get_controller_pose (LrgVRService *service,
                                             LrgVRHand     hand,
                                             gfloat       *matrix)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);

    if ((guint) hand > 1)
    {
        identity (matrix);
        return;
    }
    memcpy (matrix, self->hands[hand].aim, 16 * sizeof (gfloat));
}

static gboolean
lrg_openxr_service_real_is_controller_tracked (LrgVRService *service,
                                               LrgVRHand     hand)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);

    return (guint) hand <= 1 && self->hands[hand].tracked;
}

static guint
lrg_openxr_service_real_get_controller_buttons (LrgVRService *service,
                                                LrgVRHand     hand)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);

    return (guint) hand <= 1 ? self->hands[hand].buttons : 0;
}

static gfloat
lrg_openxr_service_real_get_controller_axis (LrgVRService *service,
                                             LrgVRHand     hand,
                                             guint         axis)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);

    if ((guint) hand > 1 || axis > 3)
        return 0.0f;
    return self->hands[hand].axes[axis];
}

static gboolean
lrg_openxr_service_real_is_hand_tracking_supported (LrgVRService *service)
{
    return LRG_OPENXR_SERVICE (service)->hand_tracking;
}

static gboolean
lrg_openxr_service_real_get_hand_state (LrgVRService   *service,
                                        LrgVRHand       hand,
                                        LrgVRHandState *state)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);

    if (!self->hand_tracking || (guint) hand > 1)
        return FALSE;
    *state = self->hand_state[hand];
    return state->active;
}

static void
lrg_openxr_service_real_trigger_haptic (LrgVRService *service,
                                        LrgVRHand     hand,
                                        gfloat        duration,
                                        gfloat        amplitude)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (service);
    XrHapticActionInfo hi = { .type = XR_TYPE_HAPTIC_ACTION_INFO };
    XrHapticVibration vib = { .type = XR_TYPE_HAPTIC_VIBRATION };

    if ((guint) hand > 1 || self->state != XR_SESSION_STATE_FOCUSED)
        return;
    hi.action = self->act_haptic;
    hi.subactionPath = self->hands[hand].path;
    vib.duration = (XrDuration) (duration * 1e9f);
    vib.frequency = XR_FREQUENCY_UNSPECIFIED;
    vib.amplitude = CLAMP (amplitude, 0.0f, 1.0f);
    xrApplyHapticFeedback (self->session, &hi, (const XrHapticBaseHeader *) &vib);
}


gboolean
lrg_openxr_service_probe (const gchar  *app_name,
                          gchar       **runtime_name,
                          gchar       **system_name,
                          GError      **error)
{
    XrExtensionProperties *props = NULL;
    guint32 n_props = 0, i;
    gboolean graphics = FALSE;
    XrInstanceCreateInfo ici = { .type = XR_TYPE_INSTANCE_CREATE_INFO };
    XrInstanceProperties ip = { .type = XR_TYPE_INSTANCE_PROPERTIES };
    XrSystemGetInfo sgi = { .type = XR_TYPE_SYSTEM_GET_INFO };
    XrSystemProperties sp = { .type = XR_TYPE_SYSTEM_PROPERTIES };
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrResult result;

    if (runtime_name != NULL)
        *runtime_name = NULL;
    if (system_name != NULL)
        *system_name = NULL;

    /* --- the loader finds a runtime, and it can draw with GL or EGL --- */
    if (XR_FAILED (xrEnumerateInstanceExtensionProperties (NULL, 0, &n_props, NULL))
        || n_props == 0)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                     "no OpenXR runtime (is SteamVR or Monado installed and "
                     "set as the active runtime?)");
        return FALSE;
    }
    props = g_new0 (XrExtensionProperties, n_props);
    for (i = 0; i < n_props; i++)
        props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    xrEnumerateInstanceExtensionProperties (NULL, n_props, &n_props, props);
    graphics = has_extension (props, n_props, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)
               || has_extension (props, n_props, XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
    g_free (props);
    if (!graphics)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                     "the OpenXR runtime supports neither OpenGL nor EGL");
        return FALSE;
    }

    /* --- an instance without extensions: enough to ask for the HMD --- */
    g_strlcpy (ici.applicationInfo.applicationName,
               app_name != NULL ? app_name : "libregnum",
               sizeof ici.applicationInfo.applicationName);
    ici.applicationInfo.applicationVersion = 1;
    g_strlcpy (ici.applicationInfo.engineName, "libregnum",
               sizeof ici.applicationInfo.engineName);
    ici.applicationInfo.engineVersion = 1;
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION (1, 0, 0);
    result = xrCreateInstance (&ici, &instance);
    if (XR_FAILED (result))
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                     "the OpenXR runtime could not start (xrCreateInstance: %d)",
                     (gint) result);
        return FALSE;
    }
    if (runtime_name != NULL && XR_SUCCEEDED (xrGetInstanceProperties (instance, &ip)))
        *runtime_name = g_strdup_printf ("%s %u.%u.%u", ip.runtimeName,
                                         XR_VERSION_MAJOR (ip.runtimeVersion),
                                         XR_VERSION_MINOR (ip.runtimeVersion),
                                         XR_VERSION_PATCH (ip.runtimeVersion));

    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = xrGetSystem (instance, &sgi, &system);
    if (XR_FAILED (result))
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_HMD_NOT_FOUND,
                     "the OpenXR runtime has no headset (connect and switch on "
                     "the headset; xrGetSystem: %d)", (gint) result);
        xrDestroyInstance (instance);
        if (runtime_name != NULL)
            g_clear_pointer (runtime_name, g_free);
        return FALSE;
    }
    if (system_name != NULL && XR_SUCCEEDED (xrGetSystemProperties (instance, system, &sp)))
        *system_name = g_strdup (sp.systemName);
    xrDestroyInstance (instance);
    return TRUE;
}

#else /* !LRG_HAS_OPENXR */

static gboolean
lrg_openxr_service_real_initialize (LrgVRService  *service,
                                    GError       **error)
{
    (void) service;
    g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                 "libregnum was built without OpenXR (rebuild with OPENXR=1)");
    return FALSE;
}


gboolean
lrg_openxr_service_probe (const gchar  *app_name,
                          gchar       **runtime_name,
                          gchar       **system_name,
                          GError      **error)
{
    (void) app_name;
    if (runtime_name != NULL)
        *runtime_name = NULL;
    if (system_name != NULL)
        *system_name = NULL;
    g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                 "libregnum was built without OpenXR (install the OpenXR loader, "
                 "e.g. openxr-devel, and rebuild with OPENXR=1)");
    return FALSE;
}

#endif /* LRG_HAS_OPENXR */

/* ------------------------------------------------------------- public API -- */

gint
lrg_openxr_service_start_frame_thread (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), -1);
#ifdef LRG_HAS_OPENXR
    if (!self->initialized)
        return -1;
    if (self->waiter != NULL)
        return self->frame_fd;
    self->frame_fd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (self->frame_fd < 0)
        return -1;
    self->waiter_quit = FALSE;
    self->waiter = g_thread_new ("lrg-xr-wait", frame_waiter, self);
    return self->frame_fd;
#else
    return -1;
#endif
}

void
lrg_openxr_service_set_resolution_scale (LrgOpenXRService *self,
                                         gfloat            scale)
{
    g_return_if_fail (LRG_IS_OPENXR_SERVICE (self));
    g_return_if_fail (scale > 0.0f);

#ifdef LRG_HAS_OPENXR
    self->resolution_scale = scale;
#else
    (void) scale;
#endif
}

void
lrg_openxr_service_get_eye_size (LrgOpenXRService *self,
                                 guint            *width,
                                 guint            *height)
{
    guint w = 0;
    guint h = 0;

    g_return_if_fail (LRG_IS_OPENXR_SERVICE (self));

#ifdef LRG_HAS_OPENXR
    if (self->view_count > 0)
    {
        w = self->views[0].width;
        h = self->views[0].height;
    }
#endif
    if (width != NULL)
        *width = w;
    if (height != NULL)
        *height = h;
}

gint
lrg_openxr_service_get_frame_fd (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), -1);
#ifdef LRG_HAS_OPENXR
    return self->waiter != NULL ? self->frame_fd : -1;
#else
    return -1;
#endif
}

LrgOpenXRService *
lrg_openxr_service_new (const gchar *app_name)
{
    LrgOpenXRService *self = g_object_new (LRG_TYPE_OPENXR_SERVICE, NULL);

    self->app_name = g_strdup (app_name);
    return self;
}

gboolean
lrg_openxr_service_is_supported (void)
{
#ifdef LRG_HAS_OPENXR
    return TRUE;
#else
    return FALSE;
#endif
}

const gchar *
lrg_openxr_service_get_runtime_name (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), NULL);
    return self->runtime_name;
}

const gchar *
lrg_openxr_service_get_system_name (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), NULL);
    return self->system_name;
}

gfloat
lrg_openxr_service_get_display_refresh_rate (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), 90.0f);
#ifdef LRG_HAS_OPENXR
    return self->refresh_rate > 1.0f ? self->refresh_rate : 90.0f;
#else
    return 90.0f;
#endif
}

gboolean
lrg_openxr_service_is_focused (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
#ifdef LRG_HAS_OPENXR
    return self->state == XR_SESSION_STATE_FOCUSED;
#else
    return FALSE;
#endif
}

const gchar *
lrg_openxr_service_get_session_state_name (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), "none");
#ifdef LRG_HAS_OPENXR
    return state_name (self->state);
#else
    return "none";
#endif
}

gboolean
lrg_openxr_service_has_hand_aim (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
#ifdef LRG_HAS_OPENXR
    return self->hand_tracking && self->has_hand_aim_ext;
#else
    return FALSE;
#endif
}

gchar *
lrg_openxr_service_get_interaction_profile (LrgOpenXRService *self,
                                            LrgVRHand         hand)
{
#ifdef LRG_HAS_OPENXR
    XrInteractionProfileState st = { .type = XR_TYPE_INTERACTION_PROFILE_STATE };
    gchar buf[XR_MAX_PATH_LENGTH];
    guint32 n = 0;
#endif

    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), NULL);
#ifdef LRG_HAS_OPENXR
    if (self->session == XR_NULL_HANDLE || (guint) hand > 1)
        return NULL;
    if (XR_FAILED (xrGetCurrentInteractionProfile (self->session,
                                                   self->hands[hand].path, &st))
        || st.interactionProfile == XR_NULL_PATH)
        return NULL;
    if (XR_FAILED (xrPathToString (self->instance, st.interactionProfile,
                                   sizeof buf, &n, buf)))
        return NULL;
    return g_strdup (buf);
#else
    (void) hand;
    return NULL;
#endif
}

/* ---------------------------------------------------- composition layers -- */

#ifdef LRG_HAS_OPENXR
/* @id (1-based) as a layer, or NULL. */
static LrgXrLayer *
layer_by_id (LrgOpenXRService *self,
             guint             id)
{
    if (id == 0 || id > self->n_layers)
        return NULL;
    return &self->layers[id - 1];
}

/* The sub-image of @layer, @rect (x, y, w, h in pixels) or all of it. */
static void
layer_sub_image (LrgXrLayer           *layer,
                 const gint           *rect,
                 XrSwapchainSubImage  *sub)
{
    memset (sub, 0, sizeof *sub);
    sub->swapchain = layer->swapchain;
    if (rect != NULL)
    {
        gint x = CLAMP (rect[0], 0, (gint) layer->width - 1);
        gint y = CLAMP (rect[1], 0, (gint) layer->height - 1);

        sub->imageRect.offset.x = x;
        sub->imageRect.offset.y = y;
        sub->imageRect.extent.width = CLAMP (rect[2], 1, (gint) layer->width - x);
        sub->imageRect.extent.height = CLAMP (rect[3], 1, (gint) layer->height - y);
    }
    else
    {
        sub->imageRect.extent.width = (gint32) layer->width;
        sub->imageRect.extent.height = (gint32) layer->height;
    }
}

/* Column-major @m (rotation + translation, no scale) as an OpenXR pose. */
static void
matrix_to_pose (const gfloat *m,
                XrPosef      *p)
{
    gfloat tr = m[0] + m[5] + m[10];
    gfloat x, y, z, w, len;

    /* Shepperd's method: pick the largest diagonal term for stability. */
    if (tr > 0.0f)
    {
        gfloat r = sqrtf (1.0f + tr) * 2.0f;
        w = 0.25f * r;
        x = (m[6] - m[9]) / r;
        y = (m[8] - m[2]) / r;
        z = (m[1] - m[4]) / r;
    }
    else if (m[0] > m[5] && m[0] > m[10])
    {
        gfloat r = sqrtf (1.0f + m[0] - m[5] - m[10]) * 2.0f;
        w = (m[6] - m[9]) / r;
        x = 0.25f * r;
        y = (m[4] + m[1]) / r;
        z = (m[8] + m[2]) / r;
    }
    else if (m[5] > m[10])
    {
        gfloat r = sqrtf (1.0f + m[5] - m[0] - m[10]) * 2.0f;
        w = (m[8] - m[2]) / r;
        x = (m[4] + m[1]) / r;
        y = 0.25f * r;
        z = (m[9] + m[6]) / r;
    }
    else
    {
        gfloat r = sqrtf (1.0f + m[10] - m[0] - m[5]) * 2.0f;
        w = (m[1] - m[4]) / r;
        x = (m[8] + m[2]) / r;
        y = (m[9] + m[6]) / r;
        z = 0.25f * r;
    }
    len = sqrtf (x * x + y * y + z * z + w * w);
    if (len < 1e-6f)
    {
        x = y = z = 0.0f;
        w = len = 1.0f;
    }
    p->orientation.x = x / len;
    p->orientation.y = y / len;
    p->orientation.z = z / len;
    p->orientation.w = w / len;
    p->position.x = m[12];
    p->position.y = m[13];
    p->position.z = m[14];
}

/* The next free placement for this frame, or NULL (logged once). */
static LrgXrPlaced *
next_placement (LrgOpenXRService *self)
{
    if (!self->frame_begun)
        return NULL;
    if (self->n_placed >= LRG_XR_MAX_PLACED)
    {
        lrg_debug (LRG_LOG_DOMAIN_VR, "too many layers this frame; dropped one");
        return NULL;
    }
    memset (&self->placed[self->n_placed], 0, sizeof self->placed[0]);
    return &self->placed[self->n_placed++];
}

static XrCompositionLayerFlags
layer_flags (gboolean blend)
{
    /* Premultiplied alpha: content drawn over a transparent clear with
       separate alpha blending (colour SRC_ALPHA/ONE_MINUS_SRC_ALPHA, alpha
       ONE/ONE_MINUS_SRC_ALPHA) comes out premultiplied. */
    return blend ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
}
#endif

guint
lrg_openxr_service_create_layer (LrgOpenXRService  *self,
                                 guint              width,
                                 guint              height,
                                 GError           **error)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), 0);
    g_return_val_if_fail (width > 0 && height > 0, 0);
#ifdef LRG_HAS_OPENXR
    if (!self->initialized)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_FAILED,
                     "the OpenXR service is not initialised");
        return 0;
    }
    if (self->n_layers >= LRG_XR_MAX_LAYERS)
    {
        g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_FAILED,
                     "at most %d layer swapchains", LRG_XR_MAX_LAYERS);
        return 0;
    }
    /* with a depth buffer: a UI draws 3D models (portraits, a character
       on a menu) into its layer */
    if (!create_swapchain (self, &self->layers[self->n_layers], width, height,
                           TRUE, error))
    {
        /* free what was made; the slot stays unused */
        LrgXrLayer *layer = &self->layers[self->n_layers];
        guint k;

        for (k = 0; k < layer->n_images; k++)
            if (layer->fbos[k] != 0)
                rlUnloadFramebuffer (layer->fbos[k]);
        if (layer->swapchain != XR_NULL_HANDLE)
            xrDestroySwapchain (layer->swapchain);
        memset (layer, 0, sizeof *layer);
        return 0;
    }
    self->layer_valid[self->n_layers] = FALSE;
    self->n_layers++;
    lrg_debug (LRG_LOG_DOMAIN_VR, "layer %u: %ux%u", self->n_layers, width, height);
    return self->n_layers;
#else
    (void) width;
    (void) height;
    g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE,
                 "libregnum was built without OpenXR");
    return 0;
#endif
}

gboolean
lrg_openxr_service_begin_layer (LrgOpenXRService *self,
                                guint             id,
                                guint            *framebuffer,
                                guint            *width,
                                guint            *height)
{
#ifdef LRG_HAS_OPENXR
    XrSwapchainImageAcquireInfo ai = { .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    LrgXrLayer *layer;
#endif

    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
#ifdef LRG_HAS_OPENXR
    layer = layer_by_id (self, id);
    if (layer == NULL || layer->open || !self->running)
        return FALSE;
    if (!xr_ok (self, xrAcquireSwapchainImage (layer->swapchain, &ai, &layer->acquired),
                "xrAcquireSwapchainImage (layer)", NULL))
        return FALSE;
    wi.timeout = XR_INFINITE_DURATION;
    if (!xr_ok (self, xrWaitSwapchainImage (layer->swapchain, &wi),
                "xrWaitSwapchainImage (layer)", NULL))
    {
        XrSwapchainImageReleaseInfo ri = { .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage (layer->swapchain, &ri);
        return FALSE;
    }
    layer->open = TRUE;
    rlEnableFramebuffer (layer->fbos[layer->acquired]);
    rlViewport (0, 0, (gint) layer->width, (gint) layer->height);
    if (framebuffer != NULL)
        *framebuffer = layer->fbos[layer->acquired];
    if (width != NULL)
        *width = layer->width;
    if (height != NULL)
        *height = layer->height;
    return TRUE;
#else
    (void) id;
    (void) framebuffer;
    (void) width;
    (void) height;
    return FALSE;
#endif
}

void
lrg_openxr_service_end_layer (LrgOpenXRService *self,
                              guint             id)
{
#ifdef LRG_HAS_OPENXR
    XrSwapchainImageReleaseInfo ri = { .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    LrgXrLayer *layer;
#endif

    g_return_if_fail (LRG_IS_OPENXR_SERVICE (self));
#ifdef LRG_HAS_OPENXR
    layer = layer_by_id (self, id);
    if (layer == NULL || !layer->open)
        return;
    rlDrawRenderBatchActive ();
    rlDisableFramebuffer ();
    xrReleaseSwapchainImage (layer->swapchain, &ri);
    glFlush ();   /* after the release, as in end_view */
    layer->open = FALSE;
    self->layer_valid[id - 1] = TRUE;
#else
    (void) id;
#endif
}

gboolean
lrg_openxr_service_submit_layer_quad (LrgOpenXRService *self,
                                      guint             id,
                                      const gint       *rect,
                                      const gfloat     *pose,
                                      gfloat            width,
                                      gfloat            height,
                                      gboolean          blend)
{
#ifdef LRG_HAS_OPENXR
    LrgXrLayer *layer;
    LrgXrPlaced *pl;
#endif

    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
    g_return_val_if_fail (pose != NULL, FALSE);
#ifdef LRG_HAS_OPENXR
    layer = layer_by_id (self, id);
    if (layer == NULL || !self->layer_valid[id - 1] || width <= 0.0f || height <= 0.0f)
        return FALSE;
    pl = next_placement (self);
    if (pl == NULL)
        return FALSE;
    pl->quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    pl->quad.layerFlags = layer_flags (blend);
    pl->quad.space = self->local_space;
    pl->quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer_sub_image (layer, rect, &pl->quad.subImage);
    matrix_to_pose (pose, &pl->quad.pose);
    pl->quad.size.width = width;
    pl->quad.size.height = height;
    return TRUE;
#else
    (void) id;
    (void) rect;
    (void) width;
    (void) height;
    (void) blend;
    return FALSE;
#endif
}

gboolean
lrg_openxr_service_submit_layer_cylinder (LrgOpenXRService *self,
                                          guint             id,
                                          const gint       *rect,
                                          const gfloat     *pose,
                                          gfloat            radius,
                                          gfloat            central_angle,
                                          gfloat            aspect_ratio,
                                          gboolean          blend)
{
#ifdef LRG_HAS_OPENXR
    LrgXrLayer *layer;
    LrgXrPlaced *pl;
#endif

    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
    g_return_val_if_fail (pose != NULL, FALSE);
#ifdef LRG_HAS_OPENXR
    layer = layer_by_id (self, id);
    if (layer == NULL || !self->layer_valid[id - 1] || !self->has_cylinder_ext
        || radius <= 0.0f || central_angle <= 0.0f || aspect_ratio <= 0.0f)
        return FALSE;
    pl = next_placement (self);
    if (pl == NULL)
        return FALSE;
    pl->cylinder.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
    pl->cylinder.layerFlags = layer_flags (blend);
    pl->cylinder.space = self->local_space;
    pl->cylinder.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer_sub_image (layer, rect, &pl->cylinder.subImage);
    matrix_to_pose (pose, &pl->cylinder.pose);
    pl->cylinder.radius = radius;
    pl->cylinder.centralAngle = central_angle;
    pl->cylinder.aspectRatio = aspect_ratio;
    return TRUE;
#else
    (void) id;
    (void) rect;
    (void) radius;
    (void) central_angle;
    (void) aspect_ratio;
    (void) blend;
    return FALSE;
#endif
}

gboolean
lrg_openxr_service_has_cylinder_layers (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
#ifdef LRG_HAS_OPENXR
    return self->has_cylinder_ext;
#else
    return FALSE;
#endif
}

gboolean
lrg_openxr_service_get_grip_pose (LrgOpenXRService *self,
                                  LrgVRHand         hand,
                                  gfloat           *matrix)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
    g_return_val_if_fail (matrix != NULL, FALSE);
#ifdef LRG_HAS_OPENXR
    if ((guint) hand > 1)
    {
        identity (matrix);
        return FALSE;
    }
    /* never located (or shut down): identity, as for an untracked aim */
    if (self->hands[hand].grip_space == XR_NULL_HANDLE)
    {
        identity (matrix);
        return FALSE;
    }
    memcpy (matrix, self->hands[hand].grip, 16 * sizeof (gfloat));
    return self->hands[hand].grip_tracked;
#else
    {
        gint i;

        for (i = 0; i < 16; i++)
            matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    (void) hand;
    return FALSE;
#endif
}

gboolean
lrg_openxr_service_take_recentered (LrgOpenXRService *self)
{
    g_return_val_if_fail (LRG_IS_OPENXR_SERVICE (self), FALSE);
#ifdef LRG_HAS_OPENXR
    {
        gboolean was = self->recentered;

        self->recentered = FALSE;
        return was;
    }
#else
    return FALSE;
#endif
}

/* ------------------------------------------------------------- GObject ---- */

static void
lrg_openxr_service_finalize (GObject *object)
{
    LrgOpenXRService *self = LRG_OPENXR_SERVICE (object);

#ifdef LRG_HAS_OPENXR
    lrg_openxr_service_real_shutdown (LRG_VR_SERVICE (self));
    g_mutex_clear (&self->frame_lock);
    g_cond_clear (&self->frame_cond);
#endif
    g_free (self->app_name);
    g_free (self->runtime_name);
    g_free (self->system_name);

    G_OBJECT_CLASS (lrg_openxr_service_parent_class)->finalize (object);
}

static void
lrg_openxr_service_vr_service_init (LrgVRServiceInterface *iface)
{
    iface->initialize = lrg_openxr_service_real_initialize;
#ifdef LRG_HAS_OPENXR
    iface->shutdown = lrg_openxr_service_real_shutdown;
    iface->is_available = lrg_openxr_service_real_is_available;
    iface->is_hmd_present = lrg_openxr_service_real_is_hmd_present;
    iface->poll_events = lrg_openxr_service_real_poll_events;
    iface->get_recommended_render_size = lrg_openxr_service_real_get_recommended_render_size;
    iface->get_eye_projection = lrg_openxr_service_real_get_eye_projection;
    iface->get_eye_to_head = lrg_openxr_service_real_get_eye_to_head;
    iface->get_hmd_pose = lrg_openxr_service_real_get_hmd_pose;
    iface->submit_frame = lrg_openxr_service_real_submit_frame;
    iface->get_controller_pose = lrg_openxr_service_real_get_controller_pose;
    iface->get_controller_buttons = lrg_openxr_service_real_get_controller_buttons;
    iface->get_controller_axis = lrg_openxr_service_real_get_controller_axis;
    iface->trigger_haptic = lrg_openxr_service_real_trigger_haptic;
    iface->begin_frame = lrg_openxr_service_real_begin_frame;
    iface->get_view_count = lrg_openxr_service_real_get_view_count;
    iface->begin_view = lrg_openxr_service_real_begin_view;
    iface->end_view = lrg_openxr_service_real_end_view;
    iface->end_frame = lrg_openxr_service_real_end_frame;
    iface->is_session_running = lrg_openxr_service_real_is_session_running;
    iface->should_quit = lrg_openxr_service_real_should_quit;
    iface->is_controller_tracked = lrg_openxr_service_real_is_controller_tracked;
    iface->is_hand_tracking_supported = lrg_openxr_service_real_is_hand_tracking_supported;
    iface->get_hand_state = lrg_openxr_service_real_get_hand_state;
#endif
}

static void
lrg_openxr_service_class_init (LrgOpenXRServiceClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_openxr_service_finalize;
}

static void
lrg_openxr_service_init (LrgOpenXRService *self)
{
#ifdef LRG_HAS_OPENXR
    g_mutex_init (&self->frame_lock);
    g_cond_init (&self->frame_cond);
    self->frame_fd = -1;
    self->resolution_scale = 1.0f;
    lrg_vr_hand_state_reset (&self->hand_state[0]);
    lrg_vr_hand_state_reset (&self->hand_state[1]);
#else
    (void) self;
#endif
}
