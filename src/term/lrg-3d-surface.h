/* lrg-3d-surface.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Concrete #LrgFrameSurface that renders the Emacs frame as textured planes in a
 * real-time 3D scene (LRG_RENDER_MODE_3D).  It owns an internal #Lrg2DSurface
 * that rasterises the flat frame to the default framebuffer; between
 * lrg_frame_surface_begin_content()/end_content() that flat frame is captured to
 * a texture and sliced onto per-window #LrgScenePanel quads, which an
 * #LrgSceneArrangement lays out and an #LrgPanelEnvironment frames, viewed
 * through an #LrgSpatialCamera.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-frame-surface.h"
#include "lrg-spatial-camera.h"
#include "lrg-scene-arrangement.h"
#include "lrg-panel-environment.h"
#include "../vr/lrg-vr-service.h"

G_BEGIN_DECLS

#define LRG_TYPE_3D_SURFACE (lrg_3d_surface_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (Lrg3DSurface, lrg_3d_surface, LRG, 3D_SURFACE, LrgFrameSurface)

/**
 * lrg_3d_surface_new:
 * @width: initial window width in pixels
 * @height: initial window height in pixels
 * @title: (nullable): window title
 *
 * Creates the 3D surface and its #GrlWindow (via an internal #Lrg2DSurface;
 * raylib has a single process window). The default arrangement is single-panel
 * and the default environment is void.
 *
 * Returns: (transfer full): a new #Lrg3DSurface
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
Lrg3DSurface * lrg_3d_surface_new (gint         width,
                                   gint         height,
                                   const gchar *title);

/**
 * lrg_3d_surface_get_camera:
 * @self: a #Lrg3DSurface
 *
 * Returns: (transfer none): the scene camera (for orbit/zoom/reset control)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpatialCamera * lrg_3d_surface_get_camera (Lrg3DSurface *self);

/* --- Arrangement / environment ------------------------------------------- */

LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_arrangement (Lrg3DSurface        *self,
                                     LrgSceneArrangement *arrangement);

/**
 * lrg_3d_surface_set_arrangement_id:
 * @self: a #Lrg3DSurface
 * @id: a registered arrangement id (e.g. "single-panel", "per-window")
 *
 * Looks the id up in the default mode registry and applies it.
 *
 * Returns: %TRUE if @id was known and applied
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_set_arrangement_id (Lrg3DSurface *self,
                                            const gchar  *id);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_3d_surface_get_arrangement_id (Lrg3DSurface *self);

LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_environment (Lrg3DSurface        *self,
                                     LrgPanelEnvironment *environment);

/**
 * lrg_3d_surface_set_environment_id:
 * @self: a #Lrg3DSurface
 * @id: a registered environment id (e.g. "void", "workshop")
 *
 * Returns: %TRUE if @id was known and applied
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_set_environment_id (Lrg3DSurface *self,
                                            const gchar  *id);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_3d_surface_get_environment_id (Lrg3DSurface *self);

/**
 * lrg_3d_surface_get_environment:
 * @self: a #Lrg3DSurface
 *
 * Returns: (transfer none) (nullable): the current environment object (e.g. to
 *   feed a cockpit wall a live texture)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPanelEnvironment * lrg_3d_surface_get_environment (Lrg3DSurface *self);

/* --- Window-tree sync ----------------------------------------------------- */

/**
 * lrg_3d_surface_begin_window_sync:
 * @self: a #Lrg3DSurface
 *
 * Begins a window-tree sync pass. Mark every panel stale; lrg_3d_surface_sync_window()
 * un-marks the ones still present; lrg_3d_surface_end_window_sync() drops the rest
 * and re-runs the arrangement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_begin_window_sync (Lrg3DSurface *self);

/**
 * lrg_3d_surface_sync_window:
 * @self: a #Lrg3DSurface
 * @key: a stable identity for the window
 * @x: window left within the frame, in pixels
 * @y: window top, in pixels
 * @width: window width in pixels
 * @height: window height in pixels
 *
 * Adds or updates the panel for window @key with its current pixel geometry.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_sync_window (Lrg3DSurface *self,
                                 guint64       key,
                                 gint          x,
                                 gint          y,
                                 gint          width,
                                 gint          height);

LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_end_window_sync (Lrg3DSurface *self);

/**
 * lrg_3d_surface_set_focus_window:
 * @self: a #Lrg3DSurface
 * @key: the focused window's key
 *
 * Sets the depth-of-field focus: the panel for @key becomes fully focused
 * (crisp/bright), the others recede (dimmed).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_focus_window (Lrg3DSurface *self,
                                      guint64       key);

/* --- Interaction / manipulation ------------------------------------------ */

/**
 * lrg_3d_surface_pick_panel:
 * @self: a #Lrg3DSurface
 * @px: device pixel x
 * @py: device pixel y
 * @out_x: (out) (optional): remapped frame pixel x of the hit
 * @out_y: (out) (optional): remapped frame pixel y of the hit
 * @out_key: (out) (optional): key of the panel that was hit
 *
 * Like the #LrgFrameSurface pick vtable, but also reports which panel (window
 * key) the ray hit — needed to focus/grab a specific panel.
 *
 * Returns: %TRUE if the ray hit a panel
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_pick_panel (Lrg3DSurface *self,
                                    gfloat        px,
                                    gfloat        py,
                                    gfloat       *out_x,
                                    gfloat       *out_y,
                                    guint64      *out_key);

/**
 * lrg_3d_surface_focus_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel/window key to bring front-and-centre
 *
 * Makes @key the focus: sets the depth-of-field focus to it (others dim) and
 * eases the camera to a head-on framing of that panel ("front and centre").
 * Emits #Lrg3DSurface::panel-focused.
 *
 * Returns: %TRUE if a panel with @key exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_focus_panel (Lrg3DSurface *self,
                                     guint64       key);

/**
 * lrg_3d_surface_maximize_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel/window key to maximize (falls back to the whole-frame panel,
 *   then the first live panel, if @key has no panel)
 *
 * Frames panel @key head-on, level (0-deg tilt), filling the viewport edge-to-edge
 * — a flat, 2D-like view of the buffer within the 3D scene (as the 2D backend /
 * PGTK would show it).  Emits #Lrg3DSurface::panel-focused.  Reset the camera
 * (lrg_spatial_camera_reset()) to return to the 3D view.
 *
 * Returns: %TRUE if a panel was framed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_maximize_panel (Lrg3DSurface *self,
                                        guint64       key);

/**
 * lrg_3d_surface_orbit_room:
 * @self: a #Lrg3DSurface
 * @dyaw: azimuth delta in degrees
 * @dpitch: elevation delta in degrees
 *
 * Orbit the camera around the *whole scene* (the centroid of all panels, i.e.
 * the "room"), immediately — as opposed to orbiting a single focused panel.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_orbit_room (Lrg3DSurface *self,
                               gfloat        dyaw,
                               gfloat        dpitch);

/**
 * lrg_3d_surface_grab_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel/window key to grab
 * @px: device pixel x where the grab began
 * @py: device pixel y
 *
 * Begins a manual move of panel @key: records the panel and the camera-plane
 * drag basis at (@px, @py).  Follow with lrg_3d_surface_drag_panel().
 *
 * Returns: %TRUE if a panel with @key exists and was grabbed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_grab_panel (Lrg3DSurface *self,
                                    guint64       key,
                                    gfloat        px,
                                    gfloat        py);

/**
 * lrg_3d_surface_drag_panel:
 * @self: a #Lrg3DSurface
 * @px: current device pixel x
 * @py: current device pixel y
 *
 * Moves the grabbed panel so it tracks the pointer in the camera-facing plane at
 * its depth, pinning it there.  Emits #Lrg3DSurface::panel-moved.  No-op if no
 * panel is grabbed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_drag_panel (Lrg3DSurface *self,
                                gfloat        px,
                                gfloat        py);

/**
 * lrg_3d_surface_release_panel:
 * @self: a #Lrg3DSurface
 *
 * Ends a grab.  The panel stays pinned where it was dragged.  Emits
 * #Lrg3DSurface::panel-pinned.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_release_panel (Lrg3DSurface *self);

/**
 * lrg_3d_surface_unpin_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel/window key to release back to automatic layout
 *
 * Since: 1.0
 */
/**
 * lrg_3d_surface_pin_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel/window key
 *
 * Pins panel @key at its current transform (freeze in place), so the
 * arrangement no longer moves it.  Emits #Lrg3DSurface::panel-pinned.
 *
 * Returns: %TRUE if the panel exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_pin_panel (Lrg3DSurface *self,
                                   guint64       key);

/**
 * lrg_3d_surface_move_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel/window key
 * @dx: world-X delta
 * @dy: world-Y delta
 * @dz: world-Z delta
 *
 * Translates panel @key by (@dx, @dy, @dz) world units and pins it there
 * (a non-pointer move, e.g. from voice/AI).  Emits #Lrg3DSurface::panel-moved.
 *
 * Returns: %TRUE if the panel exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_move_panel (Lrg3DSurface *self,
                                    guint64       key,
                                    gfloat        dx,
                                    gfloat        dy,
                                    gfloat        dz);

LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_unpin_panel (Lrg3DSurface *self,
                                 guint64       key);

LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_unpin_all (Lrg3DSurface *self);

LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_is_panel_pinned (Lrg3DSurface *self,
                                         guint64       key);

/* --- Workspace panels (caller-textured, off-screen contents) -------------- */

/**
 * lrg_3d_surface_set_panel_image:
 * @self: a #Lrg3DSurface
 * @key: a stable identity for the panel (must not collide with a window key)
 * @image: the image to display on the panel
 *
 * Creates (if needed) a *static* panel for @key and uploads @image to it.  Unlike
 * window panels, a static panel keeps its own texture: it is excluded from the live
 * per-frame capture and from arrangement layout, and survives window syncs and
 * arrangement switches.  This is how an off-screen-rendered workspace shows its
 * real contents in the scene.  Position it with lrg_3d_surface_place_panel().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_panel_image (Lrg3DSurface *self,
                                     guint64       key,
                                     GrlImage     *image);

/**
 * lrg_3d_surface_place_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel key
 * @px: world x of the panel centre
 * @py: world y
 * @pz: world z
 * @yaw: yaw in degrees
 * @w: width in world units
 * @h: height in world units
 *
 * Creates (if needed) a static panel for @key and pins it at the given absolute
 * transform.  Used to lay workspace panels out on a curved arc.  Emits
 * #Lrg3DSurface::panel-moved.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_place_panel (Lrg3DSurface *self,
                                 guint64       key,
                                 gfloat        px,
                                 gfloat        py,
                                 gfloat        pz,
                                 gfloat        yaw,
                                 gfloat        w,
                                 gfloat        h);

/**
 * lrg_3d_surface_place_panel_eased:
 * @self: a #Lrg3DSurface
 * @key: the panel key
 * @px: world x of the panel centre
 * @py: world y
 * @pz: world z
 * @yaw: yaw in degrees
 * @w: width in world units
 * @h: height in world units
 *
 * Like lrg_3d_surface_place_panel(), but *eases* the panel to the new transform
 * instead of snapping (the first placement still snaps).  Used to animate the
 * workspace carousel shifting on a switch.  Emits #Lrg3DSurface::panel-moved.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_place_panel_eased (Lrg3DSurface *self,
                                       guint64       key,
                                       gfloat        px,
                                       gfloat        py,
                                       gfloat        pz,
                                       gfloat        yaw,
                                       gfloat        w,
                                       gfloat        h);

/**
 * lrg_3d_surface_rotate_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel key
 * @dyaw: yaw delta in degrees
 *
 * Rotates panel @key by @dyaw about world Y and pins it there (a relative rotate,
 * mirroring lrg_3d_surface_move_panel()).  Emits #Lrg3DSurface::panel-moved.
 *
 * Returns: %TRUE if the panel exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_rotate_panel (Lrg3DSurface *self,
                                      guint64       key,
                                      gfloat        dyaw);

/**
 * lrg_3d_surface_remove_panel:
 * @self: a #Lrg3DSurface
 * @key: the panel key
 *
 * Removes the panel for @key and forgets any pin for it.  Used when a workspace
 * is killed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_remove_panel (Lrg3DSurface *self,
                                  guint64       key);

/**
 * lrg_3d_surface_get_panel_geometry:
 * @self: a #Lrg3DSurface
 * @key: the panel key
 * @out_px: (out) (optional): world x
 * @out_py: (out) (optional): world y
 * @out_pz: (out) (optional): world z
 * @out_yaw: (out) (optional): yaw in degrees
 * @out_w: (out) (optional): width in world units
 * @out_h: (out) (optional): height in world units
 *
 * Reads the current transform of panel @key (e.g. to persist a user-adjusted
 * workspace placement).
 *
 * Returns: %TRUE if a panel with @key exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_get_panel_geometry (Lrg3DSurface *self,
                                            guint64       key,
                                            gfloat       *out_px,
                                            gfloat       *out_py,
                                            gfloat       *out_pz,
                                            gfloat       *out_yaw,
                                            gfloat       *out_w,
                                            gfloat       *out_h);

/**
 * lrg_3d_surface_get_panel_count:
 * @self: a #Lrg3DSurface
 *
 * Returns: the number of panels currently in the scene (window + static)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_3d_surface_get_panel_count (Lrg3DSurface *self);

/* --- Animation ------------------------------------------------------------ */

/**
 * lrg_3d_surface_step:
 * @self: a #Lrg3DSurface
 * @dt: elapsed seconds since the last step
 *
 * Advances the camera and panel eases.
 *
 * Returns: %TRUE while anything is still animating
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_step (Lrg3DSurface *self,
                              gfloat        dt);

LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_is_animating (Lrg3DSurface *self);

/* --- VR presentation ----------------------------------------------------- */

/**
 * Lrg3DSurfaceVROverlayFunc:
 * @self: the surface
 * @view: the view (eye) being drawn
 * @user_data: data passed to lrg_3d_surface_set_vr_overlay_func()
 *
 * Called inside each eye's 3D pass, after the panels, with that eye's
 * matrices loaded and depth testing on.  Draw in scene units (e.g. controller
 * rays, cursors, an in-world keyboard) with the ordinary 3D draw calls.
 *
 * Since: 1.0
 */
typedef void (*Lrg3DSurfaceVROverlayFunc) (Lrg3DSurface *self,
                                           guint         view,
                                           gpointer      user_data);

/**
 * lrg_3d_surface_set_vr_service:
 * @self: a #Lrg3DSurface
 * @service: (nullable): an initialised #LrgVRService, or %NULL to leave VR
 *
 * Presents the scene in a headset as well as the desktop window.  Each call
 * to lrg_3d_surface_render_vr() then renders one runtime-paced stereo frame
 * from the cached panel textures; redisplay keeps refreshing those textures
 * exactly as in plain 3D mode.  Sets the render mode to
 * %LRG_RENDER_MODE_3DVR (back to 3D when @service is %NULL).
 *
 * The viewer sits in a *seat* that follows the spatial camera: the seat is
 * at the camera's eye, at the height of what it looks at, facing the
 * camera's heading (its pitch is ignored, so the horizon stays level).
 * Every camera intent -- focus a panel, orbit, dolly -- therefore moves the
 * seat, and the headset's own tracking adds head motion on top.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_vr_service (Lrg3DSurface *self,
                                    LrgVRService *service);

/**
 * lrg_3d_surface_get_vr_service:
 * @self: a #Lrg3DSurface
 *
 * Returns: (transfer none) (nullable): the VR service, if presenting in VR
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRService * lrg_3d_surface_get_vr_service (Lrg3DSurface *self);

/**
 * lrg_3d_surface_render_vr:
 * @self: a #Lrg3DSurface
 *
 * Renders one VR frame: waits for the runtime's frame timing, draws the
 * scene once per eye (environment, panels, then the overlay function) and
 * submits it.  Blocks for at most one display interval.  Must not be called
 * while a content pass is open.
 *
 * Returns: %TRUE if a frame was begun (and submitted)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_render_vr (Lrg3DSurface *self);

/**
 * lrg_3d_surface_set_vr_overlay_func:
 * @self: a #Lrg3DSurface
 * @func: (nullable) (scope notified) (closure user_data) (destroy destroy): overlay drawer, or %NULL
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_vr_overlay_func (Lrg3DSurface              *self,
                                         Lrg3DSurfaceVROverlayFunc  func,
                                         gpointer                   user_data,
                                         GDestroyNotify             destroy);

/**
 * lrg_3d_surface_set_vr_scale:
 * @self: a #Lrg3DSurface
 * @metres_per_unit: how many metres one scene unit spans in the headset
 *
 * The scene is laid out in abstract units (a whole-frame panel is ~5 units
 * tall, viewed from ~6.6 units).  The default, 0.3, puts that panel 1.5 m
 * tall about 2 m away -- the same angular size as the desktop 3D view.
 * Larger makes everything bigger and farther; clamped to 0.02..5.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_vr_scale (Lrg3DSurface *self,
                                  gfloat        metres_per_unit);

LRG_AVAILABLE_IN_ALL
gfloat lrg_3d_surface_get_vr_scale (Lrg3DSurface *self);

/**
 * lrg_3d_surface_set_offscreen_size:
 * @self: a #Lrg3DSurface
 * @width: frame width in pixels, or 0 to follow the window
 * @height: frame height in pixels, or 0 to follow the window
 *
 * While a VR service is attached the flat frame is drawn off screen, and its
 * size need not be the window's: the window is usually hidden, and a host
 * compositor may clamp it to its display.  A non-zero size here is the
 * frame's size in VR -- the surface geometry, the content target and the
 * capture all use it.  Outside VR it has no effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_set_offscreen_size (Lrg3DSurface *self,
                                        gint          width,
                                        gint          height);

/**
 * lrg_3d_surface_get_offscreen_size:
 * @self: a #Lrg3DSurface
 * @width: (out) (optional): the VR frame width, 0 when following the window
 * @height: (out) (optional): the VR frame height, 0 when following the window
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_get_offscreen_size (Lrg3DSurface *self,
                                        gint         *width,
                                        gint         *height);

/**
 * lrg_3d_surface_begin_capture:
 * @self: a #Lrg3DSurface
 *
 * Starts an out-of-band capture of the flat frame -- e.g. an off-screen
 * workspace rendered into its own panel.  In VR this binds (and clears)
 * the offscreen content target, since the window is not drawn there;
 * otherwise draws go to the window as before.  Call after the frame has
 * been cleared, draw, then lrg_3d_surface_end_capture().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_begin_capture (Lrg3DSurface *self);

/**
 * lrg_3d_surface_end_capture:
 * @self: a #Lrg3DSurface
 *
 * Flushes what was drawn since lrg_3d_surface_begin_capture() and reads it
 * back.  Does not touch the live panels.
 *
 * Returns: (transfer full) (nullable): the captured flat frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_3d_surface_end_capture (Lrg3DSurface *self);

/**
 * lrg_3d_surface_export_content:
 * @self: a #Lrg3DSurface
 * @path: file to write; the format follows the extension (.png)
 *
 * Writes the flat frame as last captured onto the panels -- what the
 * headset's panels show.  Kept only while presenting in VR.
 *
 * Returns: %TRUE if an image was written
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_export_content (Lrg3DSurface *self,
                                        const gchar  *path);

/**
 * lrg_3d_surface_vr_recenter:
 * @self: a #Lrg3DSurface
 *
 * Makes the viewer's current head position and heading the seat's origin,
 * so the scene is straight ahead again.  Happens automatically on the first
 * tracked frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_vr_recenter (Lrg3DSurface *self);

/**
 * lrg_3d_surface_vr_turn:
 * @self: a #Lrg3DSurface
 * @degrees: yaw to turn the seat by (positive turns left)
 *
 * Rotates the seat in place (snap / smooth turning).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_vr_turn (Lrg3DSurface *self,
                             gfloat        degrees);

/**
 * lrg_3d_surface_vr_move:
 * @self: a #Lrg3DSurface
 * @forward: metres along the head's horizontal heading
 * @right: metres to the right of it
 * @up: metres up
 *
 * Moves the seat (stick locomotion), relative to where the viewer looks.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_vr_move (Lrg3DSurface *self,
                             gfloat        forward,
                             gfloat        right,
                             gfloat        up);

/**
 * lrg_3d_surface_vr_tracking_to_scene:
 * @self: a #Lrg3DSurface
 * @pose: (array fixed-size=16): a column-major pose in tracking space
 *   (e.g. lrg_vr_service_get_controller_pose())
 * @out_pos: (out) (array fixed-size=3): its position in scene units
 * @out_dir: (out) (array fixed-size=3): its forward (-Z) direction, unit length
 *
 * Maps a tracked pose into the scene, through the seat and scale, so a
 * controller can be ray-cast with lrg_3d_surface_pick_ray().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_3d_surface_vr_tracking_to_scene (Lrg3DSurface *self,
                                          const gfloat *pose,
                                          gfloat       *out_pos,
                                          gfloat       *out_dir);

/**
 * lrg_3d_surface_pick_ray:
 * @self: a #Lrg3DSurface
 * @ox: ray origin x (scene units)
 * @oy: ray origin y
 * @oz: ray origin z
 * @dx: ray direction x
 * @dy: ray direction y
 * @dz: ray direction z
 * @out_x: (out) (optional): frame pixel x of the hit
 * @out_y: (out) (optional): frame pixel y of the hit
 * @out_key: (out) (optional): key of the panel hit
 * @out_distance: (out) (optional): distance to the hit, in scene units
 *
 * The world-space twin of lrg_3d_surface_pick_panel(): casts an arbitrary
 * ray (a VR controller's) against the panels.
 *
 * Returns: %TRUE if the ray hit a panel
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_pick_ray (Lrg3DSurface *self,
                                  gfloat        ox,
                                  gfloat        oy,
                                  gfloat        oz,
                                  gfloat        dx,
                                  gfloat        dy,
                                  gfloat        dz,
                                  gfloat       *out_x,
                                  gfloat       *out_y,
                                  guint64      *out_key,
                                  gfloat       *out_distance);

/**
 * lrg_3d_surface_set_panel_transform:
 * @self: a #Lrg3DSurface
 * @key: the panel/window key
 * @px: world x of the panel centre
 * @py: world y
 * @pz: world z
 * @yaw: yaw in degrees
 * @w: width in world units
 * @h: height in world units
 *
 * Pins any panel -- window or static -- at an absolute transform (a
 * hand-held grab places panels this way).  Emits #Lrg3DSurface::panel-moved.
 *
 * Returns: %TRUE if the panel exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_3d_surface_set_panel_transform (Lrg3DSurface *self,
                                             guint64       key,
                                             gfloat        px,
                                             gfloat        py,
                                             gfloat        pz,
                                             gfloat        yaw,
                                             gfloat        w,
                                             gfloat        h);

G_END_DECLS
