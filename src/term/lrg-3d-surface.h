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

G_END_DECLS
