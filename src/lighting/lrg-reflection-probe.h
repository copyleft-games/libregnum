/* lrg-reflection-probe.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbe - a box-projected local reflection cubemap.
 *
 * A probe captures the scene around its position into a cubemap (six
 * 90-degree views drawn by a caller-supplied callback), prefilters it
 * like the environment's sky (GGX, one roughness per mip) and is then
 * blended into the specular light of the lit shader for surfaces inside
 * its influence box.  The box is also the projection volume: with box
 * projection on, the reflection vector is intersected with the box and
 * the cubemap is looked up from the capture point toward the hit, so
 * reflections of the room's walls line up with the walls.  Probes are
 * registered with an #LrgEnvironment (lrg_environment_add_probe()),
 * whose binder selects the two strongest per draw.
 *
 * The box maths is headless; capture needs GL.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_REFLECTION_PROBE_DEFAULT_RESOLUTION:
 *
 * Default cubemap edge of a probe in pixels.
 */
#define LRG_REFLECTION_PROBE_DEFAULT_RESOLUTION (128)

#define LRG_TYPE_REFLECTION_PROBE (lrg_reflection_probe_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReflectionProbe, lrg_reflection_probe, LRG, REFLECTION_PROBE, GObject)

/**
 * LrgReflectionProbeDrawFunc:
 * @probe: the capturing probe
 * @face: 0 .. 5 for +X, -X, +Y, -Y, +Z, -Z
 * @view: (array fixed-size=16): the face's view matrix (column-major)
 * @projection: (array fixed-size=16): the 90-degree projection
 * @user_data: data handed to lrg_reflection_probe_capture()
 *
 * Draws the scene for one face.  The face's framebuffer is bound and
 * cleared, the rlgl projection and modelview matrices already hold
 * @projection and @view: draw with rlgl / raylib calls (or the light
 * set's shader) directly, without BeginMode3D() or BeginDrawing().
 * Things that should not reflect (the player, the probe gizmo) are
 * simply not drawn.
 */
typedef void (*LrgReflectionProbeDrawFunc) (LrgReflectionProbe *probe,
                                            guint               face,
                                            const gfloat       *view,
                                            const gfloat       *projection,
                                            gpointer            user_data);

/**
 * lrg_reflection_probe_new:
 *
 * Creates a probe at the origin with a 10 x 10 x 10 box (extents 5), a
 * blend distance of 1, intensity 1, box projection on and the default
 * resolution.
 *
 * Returns: (transfer full): a new #LrgReflectionProbe
 */
LRG_AVAILABLE_IN_ALL
LrgReflectionProbe *
lrg_reflection_probe_new (void);

/**
 * lrg_reflection_probe_set_position:
 * @self: an #LrgReflectionProbe
 * @x: world X of the capture point and box centre
 * @y: world Y
 * @z: world Z
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_position (LrgReflectionProbe *self,
                                   gfloat              x,
                                   gfloat              y,
                                   gfloat              z);

/**
 * lrg_reflection_probe_get_position:
 * @self: an #LrgReflectionProbe
 * @out_position: (out caller-allocates) (array fixed-size=3): the position
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_get_position (LrgReflectionProbe *self,
                                   gfloat             *out_position);

/**
 * lrg_reflection_probe_set_extents:
 * @self: an #LrgReflectionProbe
 * @x: half size of the box along X (> 0)
 * @y: half size along Y (> 0)
 * @z: half size along Z (> 0)
 *
 * The axis-aligned influence and projection box, centred on the
 * position.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_extents (LrgReflectionProbe *self,
                                  gfloat              x,
                                  gfloat              y,
                                  gfloat              z);

/**
 * lrg_reflection_probe_get_extents:
 * @self: an #LrgReflectionProbe
 * @out_extents: (out caller-allocates) (array fixed-size=3): half sizes
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_get_extents (LrgReflectionProbe *self,
                                  gfloat             *out_extents);

/**
 * lrg_reflection_probe_set_blend_distance:
 * @self: an #LrgReflectionProbe
 * @distance: width of the band inside the box faces over which the
 *   influence fades from 1 to 0 (>= 0; 0 = a hard edge)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_blend_distance (LrgReflectionProbe *self,
                                         gfloat              distance);

/**
 * lrg_reflection_probe_get_blend_distance:
 * @self: an #LrgReflectionProbe
 *
 * Returns: the blend distance
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_reflection_probe_get_blend_distance (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_set_intensity:
 * @self: an #LrgReflectionProbe
 * @intensity: multiplier of the captured light (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_intensity (LrgReflectionProbe *self,
                                    gfloat              intensity);

/**
 * lrg_reflection_probe_get_intensity:
 * @self: an #LrgReflectionProbe
 *
 * Returns: the intensity
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_reflection_probe_get_intensity (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_set_box_projection:
 * @self: an #LrgReflectionProbe
 * @enabled: %TRUE to parallax-correct lookups against the box (indoor
 *   rooms), %FALSE for an infinitely distant capture (open areas)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_box_projection (LrgReflectionProbe *self,
                                         gboolean            enabled);

/**
 * lrg_reflection_probe_get_box_projection:
 * @self: an #LrgReflectionProbe
 *
 * Returns: whether box projection is on
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reflection_probe_get_box_projection (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_set_resolution:
 * @self: an #LrgReflectionProbe
 * @size: cubemap edge, rounded down to a power of two in 16 .. 1024;
 *   applies to the next capture
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_resolution (LrgReflectionProbe *self,
                                     guint               size);

/**
 * lrg_reflection_probe_get_resolution:
 * @self: an #LrgReflectionProbe
 *
 * Returns: the cubemap edge
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reflection_probe_get_resolution (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_set_clip:
 * @self: an #LrgReflectionProbe
 * @near_plane: capture near plane (> 0, default 0.05)
 * @far_plane: capture far plane (> @near_plane, default 1000)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_clip (LrgReflectionProbe *self,
                               gfloat              near_plane,
                               gfloat              far_plane);

/**
 * lrg_reflection_probe_set_clear_color:
 * @self: an #LrgReflectionProbe
 * @rgb: (array fixed-size=3): linear colour each face is cleared to
 *   before the callback draws (default black)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_set_clear_color (LrgReflectionProbe *self,
                                      const gfloat       *rgb);

/**
 * lrg_reflection_probe_contains:
 * @self: an #LrgReflectionProbe
 * @point: (array fixed-size=3): a world point
 *
 * Returns: whether @point is inside the box
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reflection_probe_contains (LrgReflectionProbe *self,
                               const gfloat       *point);

/**
 * lrg_reflection_probe_get_influence:
 * @self: an #LrgReflectionProbe
 * @point: (array fixed-size=3): a world point
 *
 * The weight the shader gives the probe at @point: 0 outside the box,
 * rising linearly over the blend distance inside each face to 1 (the
 * minimum over the three axes), times nothing else.
 *
 * Returns: 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_reflection_probe_get_influence (LrgReflectionProbe *self,
                                    const gfloat       *point);

/**
 * lrg_reflection_probe_box_project:
 * @self: an #LrgReflectionProbe
 * @point: (array fixed-size=3): the shaded world point
 * @direction: (array fixed-size=3): the reflection direction
 * @out_direction: (out caller-allocates) (array fixed-size=3): the
 *   cubemap lookup direction (unit length)
 *
 * The shader's parallax correction: intersects the ray from @point
 * along @direction with the box (from inside) and returns the direction
 * from the capture point to the hit.  With box projection off, or
 * @point outside the box, @direction normalized.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_box_project (LrgReflectionProbe *self,
                                  const gfloat       *point,
                                  const gfloat       *direction,
                                  gfloat             *out_direction);

/**
 * lrg_reflection_probe_capture:
 * @self: an #LrgReflectionProbe
 * @draw_func: (scope call) (closure user_data): draws the scene for each face
 * @user_data: data for @draw_func
 * @error: return location for a #GError
 *
 * Renders the six faces (each cleared to the clear colour with a fresh
 * depth buffer, then drawn by @draw_func) and prefilters them.  Needs a
 * window; call outside BeginDrawing().  The rlgl matrices, viewport and
 * framebuffer are restored afterwards.
 *
 * Returns: %TRUE on success, else %FALSE with an
 *   %LRG_ENVIRONMENT_ERROR_GPU
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reflection_probe_capture (LrgReflectionProbe          *self,
                              LrgReflectionProbeDrawFunc   draw_func,
                              gpointer                     user_data,
                              GError                     **error);

/**
 * lrg_reflection_probe_is_ready:
 * @self: an #LrgReflectionProbe
 *
 * Returns: whether a capture exists (only ready probes are bound)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reflection_probe_is_ready (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_get_texture_id:
 * @self: an #LrgReflectionProbe
 *
 * Returns: the GL id of the prefiltered cubemap, 0 before a capture
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reflection_probe_get_texture_id (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_get_mip_count:
 * @self: an #LrgReflectionProbe
 *
 * Returns: mip levels of the prefiltered cubemap, 0 before a capture
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reflection_probe_get_mip_count (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_get_capture_count:
 * @self: an #LrgReflectionProbe
 *
 * Returns: how many captures succeeded so far
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reflection_probe_get_capture_count (LrgReflectionProbe *self);

/**
 * lrg_reflection_probe_read_face:
 * @self: a captured #LrgReflectionProbe
 * @mip: mip level
 * @face: 0 .. 5
 * @out_size: (out) (optional): the face edge at @mip
 *
 * Reads one face back from the GPU (tests, previews; slow).
 *
 * Returns: (transfer full) (nullable) (array) (element-type gfloat): size x
 *   size linear RGB floats, %NULL before a capture or out of range
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_reflection_probe_read_face (LrgReflectionProbe *self,
                                guint               mip,
                                guint               face,
                                guint              *out_size);

/**
 * lrg_reflection_probe_release_gpu:
 * @self: an #LrgReflectionProbe
 *
 * Frees the cubemap (call before CloseWindow()); the probe becomes
 * not ready.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reflection_probe_release_gpu (LrgReflectionProbe *self);

G_END_DECLS
