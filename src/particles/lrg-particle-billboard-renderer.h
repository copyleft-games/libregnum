/* lrg-particle-billboard-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Draws the active particles of an #LrgParticleSystem as camera-facing
 * textured quads (billboards), with an optional flipbook atlas.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-particle-system.h"

G_BEGIN_DECLS

/**
 * LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX:
 *
 * Floats of one vertex written by lrg_particle_billboard_renderer_build():
 * position (x, y, z), texture coordinate (u, v) and colour (r, g, b, a).
 *
 * Since: 1.0
 */
#define LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX (9)

/**
 * LRG_PARTICLE_BILLBOARD_FLOATS_PER_QUAD:
 *
 * Floats of one quad: four vertices of
 * %LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX floats.
 *
 * Since: 1.0
 */
#define LRG_PARTICLE_BILLBOARD_FLOATS_PER_QUAD (4 * LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX)

#define LRG_TYPE_PARTICLE_BILLBOARD_RENDERER (lrg_particle_billboard_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleBillboardRenderer, lrg_particle_billboard_renderer,
                      LRG, PARTICLE_BILLBOARD_RENDERER, GObject)

/**
 * lrg_particle_billboard_renderer_new:
 *
 * Creates a renderer without a texture (particles draw as a soft round
 * dot) and a single 1x1 frame.  Creating it needs no GPU context.
 *
 * Returns: (transfer full): a new #LrgParticleBillboardRenderer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgParticleBillboardRenderer *
lrg_particle_billboard_renderer_new (void);

/**
 * lrg_particle_billboard_renderer_set_texture:
 * @self: an #LrgParticleBillboardRenderer
 * @texture: (nullable): the particle texture or flipbook atlas; %NULL
 *   draws a soft round dot generated on the first draw
 *
 * Sets the texture every particle samples.  The renderer keeps a
 * reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_particle_billboard_renderer_set_texture (LrgParticleBillboardRenderer *self,
                                             GrlTexture                   *texture);

/**
 * lrg_particle_billboard_renderer_get_texture:
 * @self: an #LrgParticleBillboardRenderer
 *
 * Returns: (transfer none) (nullable): the texture, %NULL for the dot
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_particle_billboard_renderer_get_texture (LrgParticleBillboardRenderer *self);

/**
 * lrg_particle_billboard_renderer_set_frames:
 * @self: an #LrgParticleBillboardRenderer
 * @columns: atlas columns (>= 1)
 * @rows: atlas rows (>= 1)
 * @fps: flipbook frames per second of a particle's age; 0 or less spreads
 *   the frames evenly over each particle's lifetime instead
 *
 * Splits the texture into a @columns x @rows flipbook atlas, read left to
 * right, top to bottom.  A particle shows frame floor (age x @fps) modulo
 * the frame count; 1x1 (the default) maps the whole texture.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_particle_billboard_renderer_set_frames (LrgParticleBillboardRenderer *self,
                                            guint                         columns,
                                            guint                         rows,
                                            gfloat                        fps);

/**
 * lrg_particle_billboard_renderer_get_frames:
 * @self: an #LrgParticleBillboardRenderer
 * @columns: (out) (optional): atlas columns
 * @rows: (out) (optional): atlas rows
 * @fps: (out) (optional): flipbook frames per second
 *
 * Reads the flipbook layout set by
 * lrg_particle_billboard_renderer_set_frames().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_particle_billboard_renderer_get_frames (LrgParticleBillboardRenderer *self,
                                            guint                        *columns,
                                            guint                        *rows,
                                            gfloat                       *fps);

/**
 * lrg_particle_billboard_renderer_build:
 * @self: an #LrgParticleBillboardRenderer
 * @system: the particle system to read
 * @eye: (array fixed-size=3): the camera position in world space
 * @up: (array fixed-size=3): the camera up vector in world space
 * @vertices: (array) (nullable): output buffer of at least @max_quads x
 *   %LRG_PARTICLE_BILLBOARD_FLOATS_PER_QUAD floats; %NULL only counts
 * @max_quads: capacity of @vertices in quads
 *
 * Builds one camera-facing quad per active particle on the CPU, exactly
 * what lrg_particle_billboard_renderer_draw() submits.  Each quad is
 * centred on the particle, faces @eye, is the particle's size wide and
 * high, turned by its rotation (degrees, counter-clockwise as seen from
 * the eye) and coloured with its RGBA.  Vertices run bottom-left,
 * bottom-right, top-right, top-left (u, v with v = 0 at the top of the
 * texture) and quads are sorted back to front (farthest from @eye first)
 * for alpha blending.  With more particles than @max_quads the nearest
 * @max_quads are kept.  Headless.
 *
 * Returns: the number of quads written (or that would be, for a %NULL
 *   @vertices)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_particle_billboard_renderer_build (LrgParticleBillboardRenderer *self,
                                       LrgParticleSystem            *system,
                                       const gfloat                 *eye,
                                       const gfloat                 *up,
                                       gfloat                       *vertices,
                                       guint                         max_quads);

/**
 * lrg_particle_billboard_renderer_draw:
 * @self: an #LrgParticleBillboardRenderer
 * @system: the particle system to draw
 * @camera: the camera the scene is drawn with; the caller is already
 *   inside its 3D mode (grl_camera3d_begin())
 *
 * Draws the system's active particles through rlgl's batch with the
 * system's blend mode (additive, alpha or multiply), depth testing on and
 * depth writes off.  Presentation only: needs a window.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_particle_billboard_renderer_draw (LrgParticleBillboardRenderer *self,
                                      LrgParticleSystem            *system,
                                      GrlCamera3D                  *camera);

/**
 * lrg_particle_billboard_renderer_release_gpu:
 * @self: an #LrgParticleBillboardRenderer
 *
 * Frees the generated dot texture while the GPU context still exists
 * (before the window closes).  The next draw regenerates it.  Safe
 * without a window.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_particle_billboard_renderer_release_gpu (LrgParticleBillboardRenderer *self);

G_END_DECLS
