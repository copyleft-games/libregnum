/* lrg-particle-ribbon-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleRibbonRenderer: camera-facing strips for particle trails
 * (each particle drags the path it flew) and ribbons (one strip through
 * all particles in spawn order), with width curves, colour gradients and
 * three UV modes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../core/lrg-color-gradient.h"
#include "../tween/lrg-keyframe-curve.h"
#include "lrg-particle-system.h"

G_BEGIN_DECLS

/**
 * LRG_PARTICLE_RIBBON_FLOATS_PER_VERTEX:
 *
 * Floats per vertex written by lrg_particle_ribbon_renderer_build():
 * position (3), texture coordinate (2), RGBA colour (4).
 */
#define LRG_PARTICLE_RIBBON_FLOATS_PER_VERTEX (9)

/**
 * LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD:
 *
 * Floats per strip segment (a quad of four vertices).
 */
#define LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD (4 * LRG_PARTICLE_RIBBON_FLOATS_PER_VERTEX)

/**
 * LrgParticleRibbonMode:
 * @LRG_PARTICLE_RIBBON_TRAIL: every particle leaves a trail of the
 *   positions it passed (recorded by lrg_particle_ribbon_renderer_update())
 * @LRG_PARTICLE_RIBBON_CONNECT: one ribbon through all live particles in
 *   spawn order (beams, swooshes, lightning)
 *
 * What a #LrgParticleRibbonRenderer strings together.
 */
typedef enum
{
	LRG_PARTICLE_RIBBON_TRAIL,
	LRG_PARTICLE_RIBBON_CONNECT
} LrgParticleRibbonMode;

/**
 * lrg_particle_ribbon_mode_get_type:
 *
 * Returns: the #GType of #LrgParticleRibbonMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_ribbon_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_RIBBON_MODE (lrg_particle_ribbon_mode_get_type ())

/**
 * LrgParticleRibbonUvMode:
 * @LRG_PARTICLE_RIBBON_UV_STRETCH: u runs 0 (head) to 1 (tail) over the
 *   whole strip, so the texture stretches with it
 * @LRG_PARTICLE_RIBBON_UV_TILE: u is the distance from the head over the
 *   tile length, so the texture repeats every tile length (world units)
 * @LRG_PARTICLE_RIBBON_UV_PER_SEGMENT: every segment maps the full 0..1
 *
 * How the texture's u coordinate runs along a strip (v runs across).
 */
typedef enum
{
	LRG_PARTICLE_RIBBON_UV_STRETCH,
	LRG_PARTICLE_RIBBON_UV_TILE,
	LRG_PARTICLE_RIBBON_UV_PER_SEGMENT
} LrgParticleRibbonUvMode;

/**
 * lrg_particle_ribbon_uv_mode_get_type:
 *
 * Returns: the #GType of #LrgParticleRibbonUvMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_ribbon_uv_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_RIBBON_UV_MODE (lrg_particle_ribbon_uv_mode_get_type ())

#define LRG_TYPE_PARTICLE_RIBBON_RENDERER (lrg_particle_ribbon_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleRibbonRenderer, lrg_particle_ribbon_renderer,
                      LRG, PARTICLE_RIBBON_RENDERER, GObject)

/**
 * lrg_particle_ribbon_renderer_new:
 *
 * Creates a trail renderer: width 0.2, stretched UVs, trails of up to 32
 * points spaced at least 0.05 units apart that fade over 0.5 seconds.
 *
 * Returns: (transfer full): a new renderer
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRibbonRenderer * lrg_particle_ribbon_renderer_new (void);

/**
 * lrg_particle_ribbon_renderer_get_mode:
 * @self: a renderer
 *
 * Returns: trails or one connecting ribbon
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRibbonMode lrg_particle_ribbon_renderer_get_mode (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_mode:
 * @self: a renderer
 * @mode: trails or one connecting ribbon
 *
 * Sets #LrgParticleRibbonRenderer:mode; recorded trails are dropped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_mode (LrgParticleRibbonRenderer *self,
                                            LrgParticleRibbonMode      mode);

/**
 * lrg_particle_ribbon_renderer_get_uv_mode:
 * @self: a renderer
 *
 * Returns: how u runs along the strip
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRibbonUvMode lrg_particle_ribbon_renderer_get_uv_mode (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_uv_mode:
 * @self: a renderer
 * @mode: how u runs along the strip
 *
 * Sets #LrgParticleRibbonRenderer:uv-mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_uv_mode (LrgParticleRibbonRenderer *self,
                                               LrgParticleRibbonUvMode    mode);

/**
 * lrg_particle_ribbon_renderer_get_uv_tile_length:
 * @self: a renderer
 *
 * Returns: world units per texture repeat in %LRG_PARTICLE_RIBBON_UV_TILE
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_ribbon_renderer_get_uv_tile_length (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_uv_tile_length:
 * @self: a renderer
 * @length: world units per texture repeat (> 0)
 *
 * Sets #LrgParticleRibbonRenderer:uv-tile-length.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_uv_tile_length (LrgParticleRibbonRenderer *self,
                                                      gfloat                     length);

/**
 * lrg_particle_ribbon_renderer_get_width:
 * @self: a renderer
 *
 * Returns: the strip width in world units at particle size 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_ribbon_renderer_get_width (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_width:
 * @self: a renderer
 * @width: strip width in world units at particle size 1 (>= 0); a point's
 *   width is @width times the particle's size when it was recorded
 *   times the width curve
 *
 * Sets #LrgParticleRibbonRenderer:width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_width (LrgParticleRibbonRenderer *self,
                                             gfloat                     width);

/**
 * lrg_particle_ribbon_renderer_get_width_curve:
 * @self: a renderer
 *
 * Returns: (transfer none) (nullable): the width multiplier along the
 *   strip, sampled at 0 (head) .. 1 (tail)
 */
LRG_AVAILABLE_IN_ALL
LrgKeyframeCurve * lrg_particle_ribbon_renderer_get_width_curve (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_width_curve:
 * @self: a renderer
 * @curve: (nullable): width multiplier along the strip (head 0, tail 1);
 *   %NULL for a constant 1
 *
 * Sets #LrgParticleRibbonRenderer:width-curve (a reference is kept).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_width_curve (LrgParticleRibbonRenderer *self,
                                                   LrgKeyframeCurve          *curve);

/**
 * lrg_particle_ribbon_renderer_get_color_gradient:
 * @self: a renderer
 *
 * Returns: (transfer none) (nullable): the colour multiplier along the
 *   strip
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradient * lrg_particle_ribbon_renderer_get_color_gradient (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_color_gradient:
 * @self: a renderer
 * @gradient: (nullable): colour multiplier along the strip (head 0,
 *   tail 1); %NULL for white
 *
 * Sets #LrgParticleRibbonRenderer:color-gradient (a reference is kept).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_color_gradient (LrgParticleRibbonRenderer *self,
                                                      LrgColorGradient          *gradient);

/**
 * lrg_particle_ribbon_renderer_get_max_points:
 * @self: a renderer
 *
 * Returns: the most points one trail keeps
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_ribbon_renderer_get_max_points (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_max_points:
 * @self: a renderer
 * @max_points: the most points one trail keeps (2 .. 1024); older points
 *   are dropped first
 *
 * Sets #LrgParticleRibbonRenderer:max-points.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_max_points (LrgParticleRibbonRenderer *self,
                                                  guint                      max_points);

/**
 * lrg_particle_ribbon_renderer_get_min_distance:
 * @self: a renderer
 *
 * Returns: the spacing below which a trail records no new point
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_ribbon_renderer_get_min_distance (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_min_distance:
 * @self: a renderer
 * @distance: world units a particle must travel before a new trail point
 *   is recorded (>= 0)
 *
 * Sets #LrgParticleRibbonRenderer:min-distance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_min_distance (LrgParticleRibbonRenderer *self,
                                                    gfloat                     distance);

/**
 * lrg_particle_ribbon_renderer_get_trail_lifetime:
 * @self: a renderer
 *
 * Returns: seconds a trail point lives
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_ribbon_renderer_get_trail_lifetime (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_trail_lifetime:
 * @self: a renderer
 * @lifetime: seconds a trail point lives (> 0); a dead particle's trail
 *   stays until its last point expires
 *
 * Sets #LrgParticleRibbonRenderer:trail-lifetime.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_trail_lifetime (LrgParticleRibbonRenderer *self,
                                                      gfloat                     lifetime);

/**
 * lrg_particle_ribbon_renderer_get_texture:
 * @self: a renderer
 *
 * Returns: (transfer none) (nullable): the strip texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_particle_ribbon_renderer_get_texture (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_set_texture:
 * @self: a renderer
 * @texture: (nullable): the strip texture; %NULL draws a generated soft
 *   edge (opaque along the centre line, fading to the sides)
 *
 * Sets #LrgParticleRibbonRenderer:texture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_set_texture (LrgParticleRibbonRenderer *self,
                                               GrlTexture                *texture);

/**
 * lrg_particle_ribbon_renderer_update:
 * @self: a renderer
 * @system: the system it draws
 * @delta_time: seconds since the last update
 *
 * In trail mode, records each live particle's position (when it moved at
 * least the minimum distance since its last point), ages every point and
 * drops expired points and empty trails.  Call it once after every
 * lrg_particle_system_update().  Does nothing in connect mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_update (LrgParticleRibbonRenderer *self,
                                          LrgParticleSystem         *system,
                                          gfloat                     delta_time);

/**
 * lrg_particle_ribbon_renderer_clear:
 * @self: a renderer
 *
 * Drops every recorded trail (after lrg_particle_system_clear() or a
 * restart).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_clear (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_get_n_trails:
 * @self: a renderer
 *
 * Returns: trails currently recorded (live and fading)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_ribbon_renderer_get_n_trails (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_get_n_points:
 * @self: a renderer
 *
 * Returns: recorded trail points over all trails (excluding the live
 *   heads, which build() adds at the particles' current positions)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_ribbon_renderer_get_n_points (LrgParticleRibbonRenderer *self);

/**
 * lrg_particle_ribbon_renderer_build:
 * @self: a renderer
 * @system: the system to draw
 * @eye: (array fixed-size=3): the camera position; strips turn to face it
 * @out_vertices: (element-type gfloat): array the quads are appended to,
 *   %LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD floats each (head side first)
 *
 * Builds the strip geometry headless.  Each strip point's side vector is
 * the cross product of its tangent and the direction to the eye, scaled
 * to half the point's width; consecutive points make a quad.  Points
 * closer together than 1e-5 units are merged.
 *
 * Returns: the number of quads appended
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_ribbon_renderer_build (LrgParticleRibbonRenderer *self,
                                          LrgParticleSystem         *system,
                                          const gfloat              *eye,
                                          GArray                    *out_vertices);

/**
 * lrg_particle_ribbon_renderer_draw:
 * @self: a renderer
 * @system: the system to draw
 * @eye: (array fixed-size=3): the camera position
 *
 * Builds and submits the quads through rlgl's batch with the system's
 * blend mode, depth test on, depth writes and back-face culling off.
 * Call it inside a 3D mode (BeginMode3D(), or an LrgWorld3DRenderer
 * custom draw).  Draws nothing when the system's layer is hidden.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_draw (LrgParticleRibbonRenderer *self,
                                        LrgParticleSystem         *system,
                                        const gfloat              *eye);

/**
 * lrg_particle_ribbon_renderer_release_gpu:
 * @self: a renderer
 *
 * Unloads the generated texture while the GL context exists (it is made
 * again on the next draw).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_ribbon_renderer_release_gpu (LrgParticleRibbonRenderer *self);

G_END_DECLS
