/* lrg-photo-dof.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoDof - a thin lens depth of field for photos: focus distance,
 * aperture (f-number) and field of view give a circle of confusion per
 * depth, applied by a gather shader (or on the CPU as a reference).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-photo-depth-target.h"

G_BEGIN_DECLS

#define LRG_TYPE_PHOTO_DOF (lrg_photo_dof_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhotoDof, lrg_photo_dof, LRG, PHOTO_DOF, GObject)

/**
 * lrg_photo_dof_new:
 *
 * Creates a depth of field (off; focus 10 units, f/4, 50 degree field
 * of view, one unit per metre).
 *
 * Returns: (transfer full): a new #LrgPhotoDof
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoDof *
lrg_photo_dof_new (void);

/**
 * lrg_photo_dof_get_enabled:
 * @self: an #LrgPhotoDof
 *
 * Returns: whether the blur is applied
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_dof_get_enabled (LrgPhotoDof *self);

/**
 * lrg_photo_dof_set_enabled:
 * @self: an #LrgPhotoDof
 * @enabled: whether to apply the blur
 *
 * Turns the effect on or off.  Off draws the picture plainly, and a
 * host need not render a depth target at all.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_enabled (LrgPhotoDof *self,
                           gboolean     enabled);

/**
 * lrg_photo_dof_get_focus_distance:
 * @self: an #LrgPhotoDof
 *
 * Returns: the distance in world units from the camera that is sharp
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_focus_distance (LrgPhotoDof *self);

/**
 * lrg_photo_dof_set_focus_distance:
 * @self: an #LrgPhotoDof
 * @distance: world units from the camera, clamped to 0.1 .. 100000
 *
 * Sets the focus distance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_focus_distance (LrgPhotoDof *self,
                                  gfloat       distance);

/**
 * lrg_photo_dof_get_aperture:
 * @self: an #LrgPhotoDof
 *
 * Returns: the f-number (small is a wide opening and a strong blur)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_aperture (LrgPhotoDof *self);

/**
 * lrg_photo_dof_set_aperture:
 * @self: an #LrgPhotoDof
 * @aperture: the f-number, clamped to 1.0 .. 32.0
 *
 * Sets the aperture.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_aperture (LrgPhotoDof *self,
                            gfloat       aperture);

/**
 * lrg_photo_dof_get_fov:
 * @self: an #LrgPhotoDof
 *
 * Returns: the vertical field of view in degrees the focal length comes from
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_fov (LrgPhotoDof *self);

/**
 * lrg_photo_dof_set_fov:
 * @self: an #LrgPhotoDof
 * @fov: the vertical field of view in degrees, clamped to 5 .. 150
 *
 * Sets the field of view; the focal length of the lens follows from it
 * (a 24 mm tall sensor), so a narrow view blurs more, like a telephoto.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_fov (LrgPhotoDof *self,
                       gfloat       fov);

/**
 * lrg_photo_dof_get_meters_per_unit:
 * @self: an #LrgPhotoDof
 *
 * Returns: how many metres one world unit is
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_meters_per_unit (LrgPhotoDof *self);

/**
 * lrg_photo_dof_set_meters_per_unit:
 * @self: an #LrgPhotoDof
 * @meters: metres per world unit, clamped to 0.001 .. 1000
 *
 * Sets the scale of the world: the blur follows real lens maths, so
 * a world whose unit is a centimetre needs 0.01.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_meters_per_unit (LrgPhotoDof *self,
                                   gfloat       meters);

/**
 * lrg_photo_dof_get_max_blur:
 * @self: an #LrgPhotoDof
 *
 * Returns: the largest blur radius in pixels of a 1080 pixel tall picture
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_max_blur (LrgPhotoDof *self);

/**
 * lrg_photo_dof_set_max_blur:
 * @self: an #LrgPhotoDof
 * @pixels: the radius, clamped to 1 .. 64; scales with the picture's height
 *
 * Limits the blur so a very open aperture stays usable.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_max_blur (LrgPhotoDof *self,
                            gfloat       pixels);

/**
 * lrg_photo_dof_set_clip:
 * @self: an #LrgPhotoDof
 * @near_clip: the camera's near plane
 * @far_clip: the camera's far plane
 *
 * Tells the shader how to turn the depth buffer back into distances.
 * The host sets it from its camera (raylib: rlGetCullDistanceNear()).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_clip (LrgPhotoDof *self,
                        gfloat       near_clip,
                        gfloat       far_clip);

/**
 * lrg_photo_dof_get_focal_length:
 * @self: an #LrgPhotoDof
 *
 * Returns: the focal length in millimetres (12 / tan (fov / 2))
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_focal_length (LrgPhotoDof *self);

/**
 * lrg_photo_dof_get_coc:
 * @self: an #LrgPhotoDof
 * @distance: the distance of a point from the camera, world units
 * @image_height: the height of the picture in pixels
 *
 * The blur radius in pixels of a point at @distance, from the thin lens
 * equation: the aperture diameter (focal length / f-number) times
 * the relative defocus |d - focus| / d times f / (focus - f), measured on
 * the 24 mm sensor, scaled to the picture and clamped to the maximum
 * blur.  It is 0 at the focus distance and grows on both sides.
 *
 * Returns: the radius in pixels, 0 or more
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_coc (LrgPhotoDof *self,
                       gfloat       distance,
                       gint         image_height);

/**
 * lrg_photo_dof_apply_image:
 * @self: an #LrgPhotoDof
 * @image: the picture, blurred in place
 * @distances: (array): one distance in world units per pixel, row by row
 *   from the top (width x height values)
 * @n_distances: the number of values in @distances
 * @error: (nullable): a location for an error
 *
 * The depth of field on the CPU: each pixel is the average of a ring of
 * taps over its blur radius, counting only taps whose own blur reaches
 * back to the pixel.  The reference the shader follows, and the path for
 * tools without a window.  Does nothing when the effect is off.
 *
 * Returns: %TRUE on success (also when off)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_dof_apply_image (LrgPhotoDof  *self,
                           GrlImage     *image,
                           const gfloat *distances,
                           gsize         n_distances,
                           GError      **error);

/**
 * lrg_photo_dof_draw_target:
 * @self: an #LrgPhotoDof
 * @target: the target the scene was drawn into
 * @dest: where to draw it in the current render target or the screen
 *
 * Draws the target's colour into the current target through the depth
 * of field shader, flipped (targets are stored bottom up).  Off, or
 * without a shader, it draws plainly and returns %FALSE when the shader
 * was needed.
 *
 * Returns: %TRUE when the blur was applied (or not needed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_dof_draw_target (LrgPhotoDof         *self,
                           LrgPhotoDepthTarget *target,
                           const GrlRectangle  *dest);

/**
 * lrg_photo_dof_release_gpu:
 * @self: an #LrgPhotoDof
 *
 * Releases the shader, call before the window closes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_release_gpu (LrgPhotoDof *self);

/**
 * lrg_photo_dof_get_strength:
 * @self: an #LrgPhotoDof
 *
 * Returns: the multiplier on the physical blur
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_dof_get_strength (LrgPhotoDof *self);

/**
 * lrg_photo_dof_set_strength:
 * @self: an #LrgPhotoDof
 * @strength: the multiplier, clamped to 0.1 .. 64
 *
 * A real 24 mm sensor at a game's wide field of view blurs very little,
 * so photo modes exaggerate: the physical blur radius is multiplied by
 * @strength (default 8).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_dof_set_strength (LrgPhotoDof *self,
                            gfloat       strength);

G_END_DECLS
