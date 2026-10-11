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
 *   end_frame    xrEndFrame with one projection layer.
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

#define LRG_XR_MAX_VIEWS   2
#define LRG_XR_MAX_IMAGES  8

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
    gboolean tracked;
    gfloat   aim[16];
    guint    buttons;
    gfloat   axes[4];
} LrgXrHand;

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
    gfloat         refresh_rate;

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
    XrAction    act_trigger;
    XrAction    act_squeeze;
    XrAction    act_stick;
    XrAction    act_stick_click;
    XrAction    act_primary;
    XrAction    act_secondary;
    XrAction    act_menu;
    XrAction    act_haptic;
    LrgXrHand   hands[2];
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
            (LrgEglGetCurrentContext) dlsym (RTLD_DEFAULT, "eglGetCurrentContext");
        LrgEglGetCurrentDisplay get_dpy =
            (LrgEglGetCurrentDisplay) dlsym (RTLD_DEFAULT, "eglGetCurrentDisplay");
        LrgEglQueryContext query =
            (LrgEglQueryContext) dlsym (RTLD_DEFAULT, "eglQueryContext");
        LrgEglChooseConfig choose =
            (LrgEglChooseConfig) dlsym (RTLD_DEFAULT, "eglChooseConfig");
        PFN_xrEglGetProcAddressMNDX gpa =
            (PFN_xrEglGetProcAddressMNDX) dlsym (RTLD_DEFAULT, "eglGetProcAddress");
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
        LrgXrView *view = &self->views[v];
        XrSwapchainCreateInfo c = { .type = XR_TYPE_SWAPCHAIN_CREATE_INFO };
        XrSwapchainImageOpenGLKHR imgs[LRG_XR_MAX_IMAGES];
        guint32 k;

        c.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
                       | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        c.format = self->format;
        c.sampleCount = 1;
        c.width = scaled_extent (self->config[v].recommendedImageRectWidth,
                                 self->config[v].maxImageRectWidth,
                                 self->resolution_scale);
        c.height = scaled_extent (self->config[v].recommendedImageRectHeight,
                                  self->config[v].maxImageRectHeight,
                                  self->resolution_scale);
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
            guint depth = rlLoadTextureDepth ((gint) view->width,
                                              (gint) view->height, true);

            view->images[k] = imgs[k].image;
            rlFramebufferAttach (fbo, imgs[k].image, RL_ATTACHMENT_COLOR_CHANNEL0,
                                 RL_ATTACHMENT_TEXTURE2D, 0);
            rlFramebufferAttach (fbo, depth, RL_ATTACHMENT_DEPTH,
                                 RL_ATTACHMENT_RENDERBUFFER, 0);
            if (!rlFramebufferComplete (fbo))
            {
                g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_COMPOSITOR,
                             "swapchain framebuffer %u/%u is incomplete", v, k);
                rlUnloadFramebuffer (fbo);
                return FALSE;
            }
            view->fbos[k] = fbo;
        }
        lrg_debug (LRG_LOG_DOMAIN_VR, "view %u: %ux%u, %u images", v,
                   view->width, view->height, view->n_images);
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
    if (g_strcmp0 (name, "trigger") == 0)         return self->act_trigger;
    if (g_strcmp0 (name, "squeeze") == 0)         return self->act_squeeze;
    if (g_strcmp0 (name, "thumbstick") == 0)      return self->act_stick;
    if (g_strcmp0 (name, "thumbstick-click") == 0) return self->act_stick_click;
    if (g_strcmp0 (name, "primary") == 0)         return self->act_primary;
    if (g_strcmp0 (name, "secondary") == 0)       return self->act_secondary;
    if (g_strcmp0 (name, "menu") == 0)            return self->act_menu;
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
    { "/interaction_profiles/valve/frame_controller", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
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
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
    { "/interaction_profiles/oculus/touch_controller", {
        { "aim", "/user/hand/left/input/aim/pose" },
        { "aim", "/user/hand/right/input/aim/pose" },
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
        { "trigger", "/user/hand/left/input/select/click" },
        { "trigger", "/user/hand/right/input/select/click" },
        { "menu", "/user/hand/left/input/menu/click" },
        { "secondary", "/user/hand/right/input/menu/click" },
        { "haptic", "/user/hand/left/output/haptic" },
        { "haptic", "/user/hand/right/output/haptic" },
        { NULL, NULL } } },
};

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

    g_strlcpy (si.actionSetName, "cmacs", sizeof si.actionSetName);
    g_strlcpy (si.localizedActionSetName, "Editor", sizeof si.localizedActionSetName);
    if (!xr_ok (self, xrCreateActionSet (self->instance, &si, &self->action_set),
                "xrCreateActionSet", error))
        return FALSE;

    self->hands[0].path = str_path (self, "/user/hand/left");
    self->hands[1].path = str_path (self, "/user/hand/right");

    if (!make_action (self, "aim", "Pointer", XR_ACTION_TYPE_POSE_INPUT,
                      &self->act_aim, error)
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
        || !make_action (self, "haptic", "Haptics", XR_ACTION_TYPE_VIBRATION_OUTPUT,
                         &self->act_haptic, error))
        return FALSE;

    for (i = 0; i < G_N_ELEMENTS (profiles); i++)
    {
        XrActionSuggestedBinding sb[32];
        XrInteractionProfileSuggestedBinding s =
            { .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        guint n = 0, b;
        XrResult r;

        for (b = 0; profiles[i].bindings[b][0] != NULL; b++)
        {
            sb[n].action = action_by_name (self, profiles[i].bindings[b][0]);
            sb[n].binding = str_path (self, profiles[i].bindings[b][1]);
            if (sb[n].binding != XR_NULL_PATH)
                n++;
        }
        s.interactionProfile = str_path (self, profiles[i].profile);
        s.countSuggestedBindings = n;
        s.suggestedBindings = sb;
        r = xrSuggestInteractionProfileBindings (self->instance, &s);
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
#undef SETBIT
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

        g_mutex_lock (&self->frame_lock);
        self->waiter_busy = FALSE;
        if (XR_SUCCEEDED (r))
        {
            guint64 one = 1;

            self->pending_state = fs;
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
    const gchar *exts[4];
    guint32 n_exts = 0;
    gboolean have_egl;
    XrInstanceCreateInfo ici = { .type = XR_TYPE_INSTANCE_CREATE_INFO };
    XrInstanceProperties ip = { .type = XR_TYPE_INSTANCE_PROPERTIES };
    XrSystemGetInfo sgi = { .type = XR_TYPE_SYSTEM_GET_INFO };
    XrSystemProperties sp = { .type = XR_TYPE_SYSTEM_PROPERTIES };
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
    if (XR_SUCCEEDED (xrGetSystemProperties (self->instance, self->system, &sp)))
        self->system_name = g_strdup (sp.systemName);

    /* Required before xrCreateSession, even though we accept any version. */
    if (glXGetCurrentContext () != NULL)
    {
        if (XR_FAILED (xrGetInstanceProcAddr (self->instance,
                           "xrGetOpenGLGraphicsRequirementsKHR",
                           (PFN_xrVoidFunction *) &get_req))
            || get_req == NULL)
        {
            g_set_error (error, LRG_VR_ERROR, LRG_VR_ERROR_FAILED,
                         "runtime does not expose xrGetOpenGLGraphicsRequirementsKHR");
            goto fail;
        }
        if (!xr_ok (self, get_req (self->instance, self->system, &req),
                    "xrGetOpenGLGraphicsRequirementsKHR", error))
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
    for (h = 0; h < 2; h++)
        if (self->hands[h].aim_space != XR_NULL_HANDLE)
            xrDestroySpace (self->hands[h].aim_space);
    memset (self->hands, 0, sizeof self->hands);

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
    const XrCompositionLayerBaseHeader *layers[1];
    guint all = (1u << self->view_count) - 1u;
    guint v;

    if (!self->frame_begun)
        return;

    /* A view begun but never ended still holds its image. */
    for (v = 0; v < self->view_count; v++)
        if (self->views[v].open)
            lrg_openxr_service_real_end_view (service, v);

    layer.space = self->local_space;
    layer.viewCount = self->view_count;
    layer.views = self->layer_views;
    layers[0] = (const XrCompositionLayerBaseHeader *) &layer;

    ei.displayTime = self->frame_state.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    /* Submit the layer only if every view was drawn this frame. */
    ei.layerCount = (self->frame_state.shouldRender && self->views_rendered == all)
                        ? 1 : 0;
    ei.layers = layers;
    xr_ok (self, xrEndFrame (self->session, &ei), "xrEndFrame", NULL);
    self->frame_begun = FALSE;

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
#else
    (void) self;
#endif
}
