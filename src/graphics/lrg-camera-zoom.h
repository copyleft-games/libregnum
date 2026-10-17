/* lrg-camera-zoom.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCameraZoom - the zoom of a third-person camera's spring arm, from
 * its farthest distance into first person: the mouse wheel (or keys)
 * moves a target distance in steps, the arm eases to it, and zooming in
 * past the arm's near end crosses into the eye once the arm got there;
 * zooming out of first person starts the arm at its near end.  Pure
 * state: the game's camera rig reads the distance and the mode.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_CAMERA_ZOOM_SNAP:
 *
 * When the arm is this close to its target it stands there.
 */
#define LRG_CAMERA_ZOOM_SNAP (0.005f)

#define LRG_TYPE_CAMERA_ZOOM (lrg_camera_zoom_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgCameraZoom, lrg_camera_zoom, LRG, CAMERA_ZOOM, GObject)
#pragma GCC visibility pop

/**
 * LrgCameraZoom:
 *
 * The arm's length runs from #LrgCameraZoom:min-distance (the near end)
 * to #LrgCameraZoom:max-distance; in first person the distance is 0.
 * One notch moves the target by #LrgCameraZoom:step; the arm follows
 * exponentially at #LrgCameraZoom:speed (the share of the gap closed
 * per second is 1 - e^-speed).
 *
 * Signals: #LrgCameraZoom::crossed when the zoom enters or leaves first
 * person.
 */

/**
 * LrgCameraZoom::crossed:
 * @self: the zoom
 * @first_person: %TRUE when it entered first person
 */

/**
 * lrg_camera_zoom_new:
 * @min_distance: the arm's near end (> 0)
 * @max_distance: its far end (raised to @min_distance when lower)
 *
 * Returns: (transfer full): a zoom in third person at @max_distance,
 *   step 0.75, speed 10
 */
LRG_AVAILABLE_IN_ALL
LrgCameraZoom *
lrg_camera_zoom_new (gfloat min_distance,
                     gfloat max_distance);

/**
 * lrg_camera_zoom_set_limits:
 * @self: an #LrgCameraZoom
 * @min_distance: the near end (> 0; non-positive keeps the old one)
 * @max_distance: the far end (raised to @min_distance when lower)
 *
 * The distance and target are kept inside the new limits.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_camera_zoom_set_limits (LrgCameraZoom *self,
                            gfloat         min_distance,
                            gfloat         max_distance);

/**
 * lrg_camera_zoom_get_min_distance:
 * @self: an #LrgCameraZoom
 *
 * Returns: the near end
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_camera_zoom_get_min_distance (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_get_max_distance:
 * @self: an #LrgCameraZoom
 *
 * Returns: the far end
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_camera_zoom_get_max_distance (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_set_step:
 * @self: an #LrgCameraZoom
 * @step: distance per notch (non-positive keeps the old one)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_camera_zoom_set_step (LrgCameraZoom *self,
                          gfloat         step);

/**
 * lrg_camera_zoom_get_step:
 * @self: an #LrgCameraZoom
 *
 * Returns: the distance per notch
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_camera_zoom_get_step (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_set_speed:
 * @self: an #LrgCameraZoom
 * @speed: the easing rate per second; 0 or less jumps at once
 */
LRG_AVAILABLE_IN_ALL
void
lrg_camera_zoom_set_speed (LrgCameraZoom *self,
                           gfloat         speed);

/**
 * lrg_camera_zoom_get_speed:
 * @self: an #LrgCameraZoom
 *
 * Returns: the easing rate
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_camera_zoom_get_speed (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_scroll:
 * @self: an #LrgCameraZoom
 * @notches: positive zooms in (towards first person), negative out
 *
 * In third person moves the target by @notches steps within the limits;
 * zooming in past the near end (a notch while the target is at the near
 * end, or notches that overshoot it by half a step or more; a notch that
 * only reaches it stops there) makes the arm ease to the near end and
 * cross into first person when it gets there
 * (lrg_camera_zoom_is_crossing()).  Zooming out cancels a pending
 * crossing.  In first person zooming out leaves it at once: the arm
 * starts at the near end and its target is one step less than the
 * notches beyond it.  Zooming in in first person does nothing.
 *
 * Returns: %TRUE when anything changed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_camera_zoom_scroll (LrgCameraZoom *self,
                        gfloat         notches);

/**
 * lrg_camera_zoom_update:
 * @self: an #LrgCameraZoom
 * @delta: seconds passed
 *
 * Eases the arm towards its target and crosses into first person when a
 * pending crossing reached the near end.
 *
 * Returns: %TRUE when the zoom crossed into first person in this update
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_camera_zoom_update (LrgCameraZoom *self,
                        gfloat         delta);

/**
 * lrg_camera_zoom_set_distance:
 * @self: an #LrgCameraZoom
 * @distance: the arm's length; 0 or less enters first person
 *
 * Jumps there (no easing); a positive distance leaves first person and
 * is kept within the limits.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_camera_zoom_set_distance (LrgCameraZoom *self,
                              gfloat         distance);

/**
 * lrg_camera_zoom_get_distance:
 * @self: an #LrgCameraZoom
 *
 * Returns: the arm's length now, 0 in first person
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_camera_zoom_get_distance (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_get_target:
 * @self: an #LrgCameraZoom
 *
 * Returns: the length the arm eases to, 0 in first person
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_camera_zoom_get_target (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_get_first_person:
 * @self: an #LrgCameraZoom
 *
 * Returns: %TRUE in first person
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_camera_zoom_get_first_person (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_is_crossing:
 * @self: an #LrgCameraZoom
 *
 * Returns: %TRUE while the arm eases to its near end to cross into
 *   first person
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_camera_zoom_is_crossing (LrgCameraZoom *self);

/**
 * lrg_camera_zoom_set_first_person:
 * @self: an #LrgCameraZoom
 * @first_person: the mode another control chose (a first / third person
 *   key)
 *
 * Follows a mode change made elsewhere: entering first person
 * remembers the target to come back to; leaving it returns the arm to
 * the remembered distance (lrg_camera_zoom_set_resume_distance()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_camera_zoom_set_first_person (LrgCameraZoom *self,
                                  gboolean       first_person);

/**
 * lrg_camera_zoom_set_resume_distance:
 * @self: an #LrgCameraZoom
 * @distance: where lrg_camera_zoom_set_first_person() (%FALSE) puts the
 *   arm (kept within the limits)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_camera_zoom_set_resume_distance (LrgCameraZoom *self,
                                     gfloat         distance);

/**
 * lrg_camera_zoom_get_resume_distance:
 * @self: an #LrgCameraZoom
 *
 * Returns: the remembered third-person distance
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_camera_zoom_get_resume_distance (LrgCameraZoom *self);

G_END_DECLS
