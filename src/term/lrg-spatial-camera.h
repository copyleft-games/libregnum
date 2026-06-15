/* lrg-spatial-camera.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 3D scene camera for #Lrg3DSurface.  Wraps a #GrlCamera3D and eases its
 * pose (#LrgPose) toward a target each frame, so arrangement/environment changes
 * and focus moves animate smoothly.  Drives the BeginMode3D/EndMode3D scope via
 * lrg_spatial_camera_begin()/_end().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-pose.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPATIAL_CAMERA (lrg_spatial_camera_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpatialCamera, lrg_spatial_camera, LRG, SPATIAL_CAMERA, GObject)

/**
 * lrg_spatial_camera_new:
 *
 * Creates a camera positioned to face the origin head-on (a sensible default
 * for the single-panel arrangement).
 *
 * Returns: (transfer full): a new #LrgSpatialCamera
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpatialCamera * lrg_spatial_camera_new (void);

/**
 * lrg_spatial_camera_set_target_pose:
 * @self: a #LrgSpatialCamera
 * @pose: the pose to ease toward
 *
 * Sets the goal pose; subsequent lrg_spatial_camera_step() calls interpolate the
 * current pose toward it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_set_target_pose (LrgSpatialCamera *self,
                                         const LrgPose    *pose);

/**
 * lrg_spatial_camera_set_pose:
 * @self: a #LrgSpatialCamera
 * @pose: the pose to snap to
 *
 * Sets both the current and target pose immediately (no animation).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_set_pose (LrgSpatialCamera *self,
                                  const LrgPose    *pose);

/**
 * lrg_spatial_camera_get_pose:
 * @self: a #LrgSpatialCamera
 *
 * Returns: (transfer full): a copy of the current (interpolated) pose
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPose * lrg_spatial_camera_get_pose (LrgSpatialCamera *self);

/**
 * lrg_spatial_camera_step:
 * @self: a #LrgSpatialCamera
 * @dt: elapsed seconds since the last step
 *
 * Eases the current pose toward the target.
 *
 * Returns: %TRUE while still animating, %FALSE once converged
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spatial_camera_step (LrgSpatialCamera *self,
                                  gfloat            dt);

LRG_AVAILABLE_IN_ALL
gboolean lrg_spatial_camera_is_animating (LrgSpatialCamera *self);

/**
 * lrg_spatial_camera_begin:
 * @self: a #LrgSpatialCamera
 *
 * Applies the current pose and enters 3D mode (everything drawn until
 * lrg_spatial_camera_end() is in the camera's 3D space).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_begin (LrgSpatialCamera *self);

LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_end (LrgSpatialCamera *self);

/* Interactive controls (snap to the new pose immediately). */

/**
 * lrg_spatial_camera_reset:
 * @self: a #LrgSpatialCamera
 *
 * Snap back to the default head-on pose.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_reset (LrgSpatialCamera *self);

/**
 * lrg_spatial_camera_zoom:
 * @self: a #LrgSpatialCamera
 * @factor: distance multiplier (<1 moves toward the target, >1 away)
 *
 * Scale the eye-to-target distance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_zoom (LrgSpatialCamera *self,
                             gfloat            factor);

/**
 * lrg_spatial_camera_orbit:
 * @self: a #LrgSpatialCamera
 * @dyaw: azimuth delta in degrees (around world Y)
 * @dpitch: elevation delta in degrees (clamped near the poles)
 *
 * Orbit the eye around the target.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_orbit (LrgSpatialCamera *self,
                              gfloat            dyaw,
                              gfloat            dpitch);

/**
 * lrg_spatial_camera_orbit_drag:
 * @self: a #LrgSpatialCamera
 * @dyaw: azimuth delta in degrees
 * @dpitch: elevation delta in degrees (clamped near the poles)
 *
 * Like lrg_spatial_camera_orbit() but applied to the current pose *immediately*
 * (no easing), so the view tracks a live pointer/device drag crisply.  Intended
 * for per-motion deltas at frame rate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_orbit_drag (LrgSpatialCamera *self,
                                    gfloat            dyaw,
                                    gfloat            dpitch);

/**
 * lrg_spatial_camera_look_drag:
 * @self: a #LrgSpatialCamera
 * @dyaw: azimuth delta in degrees
 * @dpitch: elevation delta in degrees (clamped near the poles)
 *
 * First-person "look": rotates the view direction about the *eye* (the camera's
 * own position), keeping the eye fixed and swinging the target around it
 * (immediate).  This is the opposite of orbit_drag / orbit_around_drag, which
 * move the eye around a pivot — here you turn in place, like turning your head.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_look_drag (LrgSpatialCamera *self,
                                   gfloat            dyaw,
                                   gfloat            dpitch);

/**
 * lrg_spatial_camera_dolly_drag:
 * @self: a #LrgSpatialCamera
 * @factor: eye-to-target distance multiplier (<1 toward, >1 away)
 *
 * Like lrg_spatial_camera_zoom() but applied to the current pose *immediately*
 * (no easing), so it composes with the other immediate drag operations
 * (orbit_drag / orbit_around_drag / pan_drag) within one frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_dolly_drag (LrgSpatialCamera *self,
                                    gfloat            factor);

/**
 * lrg_spatial_camera_orbit_around_drag:
 * @self: a #LrgSpatialCamera
 * @px: pivot world x
 * @py: pivot world y
 * @pz: pivot world z
 * @dyaw: azimuth delta in degrees
 * @dpitch: elevation delta in degrees (clamped near the poles)
 *
 * Orbit the eye around an arbitrary pivot point (immediate), looking at it.
 * Unlike lrg_spatial_camera_orbit_drag() — which pivots on the current target —
 * this turntables around @p, so a caller can orbit the whole scene/room rather
 * than a single focused panel.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_orbit_around_drag (LrgSpatialCamera *self,
                                           gfloat            px,
                                           gfloat            py,
                                           gfloat            pz,
                                           gfloat            dyaw,
                                           gfloat            dpitch);

/**
 * lrg_spatial_camera_pan_drag:
 * @self: a #LrgSpatialCamera
 * @dx: rightward screen delta in world units (eye + target move together)
 * @dy: upward screen delta in world units
 *
 * Slides the eye and target together in the camera's right/up plane
 * (immediate), so a drag pans the whole scene.  Callers scale pixel deltas to
 * world units (e.g. by the view height at the target distance).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_spatial_camera_pan_drag (LrgSpatialCamera *self,
                                  gfloat            dx,
                                  gfloat            dy);

G_END_DECLS
