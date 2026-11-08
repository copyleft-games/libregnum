/* lrg-particle-mesh-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleMeshRenderer: draws every particle of a system as an
 * instance of one mesh (debris, leaves, shell casings) in a single
 * instanced draw call.
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
 * LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE:
 *
 * Floats per instance written by lrg_particle_mesh_renderer_build(): a
 * column-major 4x4 world matrix (16) and an RGBA colour (4).
 */
#define LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE (20)

/**
 * LrgParticleMeshAlign:
 * @LRG_PARTICLE_MESH_ALIGN_NONE: the particle's rotation turns the mesh
 *   about the world Y axis
 * @LRG_PARTICLE_MESH_ALIGN_VELOCITY: the mesh's +Y axis points along the
 *   velocity; the rotation spins it about that axis
 * @LRG_PARTICLE_MESH_ALIGN_TUMBLE: the rotation turns the mesh about a
 *   per-particle axis chosen from the particle's random number (debris)
 *
 * How a particle's orientation is derived.
 */
typedef enum
{
	LRG_PARTICLE_MESH_ALIGN_NONE,
	LRG_PARTICLE_MESH_ALIGN_VELOCITY,
	LRG_PARTICLE_MESH_ALIGN_TUMBLE
} LrgParticleMeshAlign;

/**
 * lrg_particle_mesh_align_get_type:
 *
 * Returns: the #GType of #LrgParticleMeshAlign
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_mesh_align_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_MESH_ALIGN (lrg_particle_mesh_align_get_type ())

/**
 * LrgParticleMeshPrimitive:
 * @LRG_PARTICLE_MESH_PRIMITIVE_CUBE: a unit cube
 * @LRG_PARTICLE_MESH_PRIMITIVE_SPHERE: a sphere of diameter 1
 * @LRG_PARTICLE_MESH_PRIMITIVE_CYLINDER: a cylinder of diameter and
 *   height 1, base on the origin
 * @LRG_PARTICLE_MESH_PRIMITIVE_CONE: a cone of base diameter and height 1,
 *   base on the origin
 * @LRG_PARTICLE_MESH_PRIMITIVE_PLANE: a unit square in the XZ plane
 *
 * The built-in meshes a #LrgParticleMeshRenderer generates when no mesh
 * is set (raylib's generators: the cube, sphere and plane are centred on
 * the origin).
 */
typedef enum
{
	LRG_PARTICLE_MESH_PRIMITIVE_CUBE,
	LRG_PARTICLE_MESH_PRIMITIVE_SPHERE,
	LRG_PARTICLE_MESH_PRIMITIVE_CYLINDER,
	LRG_PARTICLE_MESH_PRIMITIVE_CONE,
	LRG_PARTICLE_MESH_PRIMITIVE_PLANE
} LrgParticleMeshPrimitive;

/**
 * lrg_particle_mesh_primitive_get_type:
 *
 * Returns: the #GType of #LrgParticleMeshPrimitive
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_mesh_primitive_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_MESH_PRIMITIVE (lrg_particle_mesh_primitive_get_type ())

#define LRG_TYPE_PARTICLE_MESH_RENDERER (lrg_particle_mesh_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleMeshRenderer, lrg_particle_mesh_renderer,
                      LRG, PARTICLE_MESH_RENDERER, GObject)

/**
 * lrg_particle_mesh_renderer_new:
 *
 * Creates a renderer drawing unit cubes, tumbling, lit from above.
 *
 * Returns: (transfer full): a new renderer
 */
LRG_AVAILABLE_IN_ALL
LrgParticleMeshRenderer * lrg_particle_mesh_renderer_new (void);

/**
 * lrg_particle_mesh_renderer_get_mesh:
 * @self: a renderer
 *
 * Returns: (transfer none) (nullable): the mesh set with
 *   lrg_particle_mesh_renderer_set_mesh()
 */
LRG_AVAILABLE_IN_ALL
GrlMesh * lrg_particle_mesh_renderer_get_mesh (LrgParticleMeshRenderer *self);

/**
 * lrg_particle_mesh_renderer_set_mesh:
 * @self: a renderer
 * @mesh: (nullable): an uploaded mesh to instance (a reference is kept);
 *   %NULL uses the primitive
 *
 * Sets #LrgParticleMeshRenderer:mesh.  The renderer adds its instance
 * attributes (locations 10 .. 14) to the mesh's vertex array object;
 * other draws of the mesh are unaffected.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_mesh (LrgParticleMeshRenderer *self,
                                          GrlMesh                 *mesh);

/**
 * lrg_particle_mesh_renderer_set_model:
 * @self: a renderer
 * @model: (nullable): a loaded model whose first mesh is instanced (a
 *   reference is kept); %NULL uses the mesh or the primitive
 *
 * Instances a model's first mesh (what the effect does for a `mesh:`
 * asset reference).  A model wins over lrg_particle_mesh_renderer_set_mesh().
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_model (LrgParticleMeshRenderer *self,
                                           GrlModel                *model);

/**
 * lrg_particle_mesh_renderer_get_primitive:
 * @self: a renderer
 *
 * Returns: the built-in mesh used without a mesh
 */
LRG_AVAILABLE_IN_ALL
LrgParticleMeshPrimitive lrg_particle_mesh_renderer_get_primitive (LrgParticleMeshRenderer *self);

/**
 * lrg_particle_mesh_renderer_set_primitive:
 * @self: a renderer
 * @primitive: the built-in mesh used without a mesh
 *
 * Sets #LrgParticleMeshRenderer:primitive.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_primitive (LrgParticleMeshRenderer  *self,
                                               LrgParticleMeshPrimitive  primitive);

/**
 * lrg_particle_mesh_renderer_get_scale:
 * @self: a renderer
 * @x: (out) (optional): X scale
 * @y: (out) (optional): Y scale
 * @z: (out) (optional): Z scale
 *
 * Reads the mesh scale at particle size 1.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_get_scale (LrgParticleMeshRenderer *self,
                                           gfloat                  *x,
                                           gfloat                  *y,
                                           gfloat                  *z);

/**
 * lrg_particle_mesh_renderer_set_scale:
 * @self: a renderer
 * @x: X scale
 * @y: Y scale
 * @z: Z scale
 *
 * Sets the mesh scale at particle size 1 (an instance is scaled by this
 * times the particle's size).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_scale (LrgParticleMeshRenderer *self,
                                           gfloat                   x,
                                           gfloat                   y,
                                           gfloat                   z);

/**
 * lrg_particle_mesh_renderer_get_align:
 * @self: a renderer
 *
 * Returns: how particles are oriented
 */
LRG_AVAILABLE_IN_ALL
LrgParticleMeshAlign lrg_particle_mesh_renderer_get_align (LrgParticleMeshRenderer *self);

/**
 * lrg_particle_mesh_renderer_set_align:
 * @self: a renderer
 * @align: how particles are oriented
 *
 * Sets #LrgParticleMeshRenderer:align.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_align (LrgParticleMeshRenderer *self,
                                           LrgParticleMeshAlign     align);

/**
 * lrg_particle_mesh_renderer_get_lit:
 * @self: a renderer
 *
 * Returns: whether instances are shaded by the light direction
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_mesh_renderer_get_lit (LrgParticleMeshRenderer *self);

/**
 * lrg_particle_mesh_renderer_set_lit:
 * @self: a renderer
 * @lit: %TRUE for half-Lambert shading by the light direction plus
 *   ambient, %FALSE for flat colour
 *
 * Sets #LrgParticleMeshRenderer:lit.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_lit (LrgParticleMeshRenderer *self,
                                         gboolean                 lit);

/**
 * lrg_particle_mesh_renderer_set_light_direction:
 * @self: a renderer
 * @x: X of the direction light travels
 * @y: Y of the direction light travels
 * @z: Z of the direction light travels
 *
 * Sets the light direction of the lit shading (default straight down,
 * slightly slanted).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_light_direction (LrgParticleMeshRenderer *self,
                                                     gfloat                   x,
                                                     gfloat                   y,
                                                     gfloat                   z);

/**
 * lrg_particle_mesh_renderer_get_texture:
 * @self: a renderer
 *
 * Returns: (transfer none) (nullable): the albedo texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_particle_mesh_renderer_get_texture (LrgParticleMeshRenderer *self);

/**
 * lrg_particle_mesh_renderer_set_texture:
 * @self: a renderer
 * @texture: (nullable): the albedo texture; %NULL for white
 *
 * Sets #LrgParticleMeshRenderer:texture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_set_texture (LrgParticleMeshRenderer *self,
                                             GrlTexture              *texture);

/**
 * lrg_particle_mesh_renderer_build:
 * @self: a renderer
 * @system: the system to draw
 * @instances: (out caller-allocates) (array): room for @max_instances
 *   times %LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE floats
 * @max_instances: capacity of @instances
 *
 * Builds the per-instance data headless: for every live particle (oldest
 * first) its world matrix (translation, orientation by the align mode,
 * scale by size times the renderer scale) and its colour.
 *
 * Returns: the number of instances written
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_mesh_renderer_build (LrgParticleMeshRenderer *self,
                                        LrgParticleSystem       *system,
                                        gfloat                  *instances,
                                        guint                    max_instances);

/**
 * lrg_particle_mesh_renderer_draw:
 * @self: a renderer
 * @system: the system to draw
 *
 * Builds the instances and draws them in one instanced call with the
 * renderer's GLSL 330 shader, using rlgl's current view and projection
 * matrices (call it inside a 3D mode).  Additive systems draw with
 * depth writes off; others write depth (mesh particles are usually
 * solid).  Draws nothing without a
 * window or when the system's layer is hidden.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_draw (LrgParticleMeshRenderer *self,
                                      LrgParticleSystem       *system);

/**
 * lrg_particle_mesh_renderer_release_gpu:
 * @self: a renderer
 *
 * Unloads the shader, the instance buffer and the generated primitive
 * while the GL context exists (they are made again on the next draw).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_mesh_renderer_release_gpu (LrgParticleMeshRenderer *self);

G_END_DECLS
