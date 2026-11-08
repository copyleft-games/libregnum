/* lrg-skinned-sprite.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedSprite: a deformable 2D sprite mesh for skeletal animation.
 *
 * A skinned sprite is a small triangle mesh cut out of a texture (or a
 * region of an atlas).  Every vertex has a bind position in skeleton
 * space (the space of the skeleton's setup pose), a texture coordinate
 * inside the region and up to four (bone, weight) influences.  Given a
 * palette of 2D affine matrices (one per bone: world(bone) *
 * inverse_bind(bone), as #LrgSkeleton2D computes it) the mesh is
 * deformed on the CPU with linear blend skinning and drawn as textured
 * triangles through rlgl.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_SKINNED_SPRITE_MAX_WEIGHTS:
 *
 * The largest number of bone influences a vertex of an
 * #LrgSkinnedSprite can carry.
 */
#define LRG_SKINNED_SPRITE_MAX_WEIGHTS (4)

/**
 * LRG_SKINNED_SPRITE_MAX_VERTICES:
 *
 * The largest number of vertices one #LrgSkinnedSprite can hold
 * (triangle indices are 16-bit).
 */
#define LRG_SKINNED_SPRITE_MAX_VERTICES (65535)

#define LRG_TYPE_SKINNED_SPRITE (lrg_skinned_sprite_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkinnedSprite, lrg_skinned_sprite, LRG, SKINNED_SPRITE, GObject)

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

/**
 * lrg_skinned_sprite_new:
 *
 * Creates an empty skinned sprite: no vertices, no triangles, no
 * texture.
 *
 * Returns: (transfer full): a new #LrgSkinnedSprite
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedSprite * lrg_skinned_sprite_new (void);

/**
 * lrg_skinned_sprite_new_quad:
 * @x: left edge of the quad in skeleton setup space
 * @y: top edge of the quad in skeleton setup space
 * @width: width of the quad
 * @height: height of the quad
 * @bone: skeleton bone index every vertex is fully weighted to, or -1
 *   for no weights (the slot's bone then carries the quad rigidly)
 *
 * Creates a four-vertex, two-triangle mesh covering the rectangle with
 * texture coordinates (0, 0) at the top left and (1, 1) at the bottom
 * right of the region.  This is the "region attachment" of other 2D
 * skeletal tools: a rigid image on one bone.
 *
 * Returns: (transfer full): a new #LrgSkinnedSprite
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedSprite * lrg_skinned_sprite_new_quad (gfloat x,
                                                gfloat y,
                                                gfloat width,
                                                gfloat height,
                                                gint   bone);

/**
 * lrg_skinned_sprite_new_grid:
 * @x: left edge of the grid in skeleton setup space
 * @y: top edge of the grid in skeleton setup space
 * @width: width of the grid
 * @height: height of the grid
 * @columns: number of cells across (at least 1)
 * @rows: number of cells down (at least 1)
 *
 * Creates a regular grid of (@columns + 1) * (@rows + 1) vertices and
 * 2 * @columns * @rows triangles over the rectangle, with texture
 * coordinates spanning the region.  Vertices are numbered row by row
 * from the top left.  The grid has no weights: assign them with
 * lrg_skinned_sprite_set_weights() or compute them with
 * lrg_skinned_sprite_compute_auto_weights() (or the skeleton's
 * lrg_skeleton2d_compute_auto_weights()).  A long strip
 * (@rows = 1) is the usual mesh of a bending limb.
 *
 * Returns: (transfer full): a new #LrgSkinnedSprite
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedSprite * lrg_skinned_sprite_new_grid (gfloat x,
                                                gfloat y,
                                                gfloat width,
                                                gfloat height,
                                                guint  columns,
                                                guint  rows);

/**
 * lrg_skinned_sprite_copy:
 * @self: an #LrgSkinnedSprite
 *
 * Copies the mesh (vertices, weights, triangles), the image path, the
 * region and a reference to the texture.  The deformed positions are
 * not copied.
 *
 * Returns: (transfer full): a new #LrgSkinnedSprite
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedSprite * lrg_skinned_sprite_copy (LrgSkinnedSprite *self);

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/**
 * lrg_skinned_sprite_add_vertex:
 * @self: an #LrgSkinnedSprite
 * @x: bind position X in skeleton setup space
 * @y: bind position Y in skeleton setup space
 * @u: texture coordinate across the region (0 .. 1)
 * @v: texture coordinate down the region (0 .. 1)
 *
 * Appends a vertex without weights.
 *
 * Returns: the new vertex index, or -1 when the mesh already holds
 *   %LRG_SKINNED_SPRITE_MAX_VERTICES vertices
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skinned_sprite_add_vertex (LrgSkinnedSprite *self,
                                    gfloat            x,
                                    gfloat            y,
                                    gfloat            u,
                                    gfloat            v);

/**
 * lrg_skinned_sprite_get_vertex_count:
 * @self: an #LrgSkinnedSprite
 *
 * Returns: the number of vertices
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skinned_sprite_get_vertex_count (LrgSkinnedSprite *self);

/**
 * lrg_skinned_sprite_get_vertex:
 * @self: an #LrgSkinnedSprite
 * @index: a vertex index
 * @x: (out) (optional): bind position X
 * @y: (out) (optional): bind position Y
 * @u: (out) (optional): texture coordinate U
 * @v: (out) (optional): texture coordinate V
 *
 * Reads a vertex.
 *
 * Returns: %TRUE if @index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_get_vertex (LrgSkinnedSprite *self,
                                        guint             index,
                                        gfloat           *x,
                                        gfloat           *y,
                                        gfloat           *u,
                                        gfloat           *v);

/**
 * lrg_skinned_sprite_set_vertex:
 * @self: an #LrgSkinnedSprite
 * @index: a vertex index
 * @x: bind position X
 * @y: bind position Y
 * @u: texture coordinate U
 * @v: texture coordinate V
 *
 * Moves a vertex and changes its texture coordinate; its weights are
 * kept.  This is the hook the rigging tool uses to edit a mesh.
 *
 * Returns: %TRUE if @index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_set_vertex (LrgSkinnedSprite *self,
                                        guint             index,
                                        gfloat            x,
                                        gfloat            y,
                                        gfloat            u,
                                        gfloat            v);

/**
 * lrg_skinned_sprite_set_weights:
 * @self: an #LrgSkinnedSprite
 * @index: a vertex index
 * @n_weights: number of influences, 0 .. %LRG_SKINNED_SPRITE_MAX_WEIGHTS
 * @bones: (array length=n_weights) (nullable): skeleton bone indices
 * @weights: (array length=n_weights) (nullable): weights, any positive
 *   scale
 *
 * Replaces the influences of one vertex.  Weights are normalised to sum
 * to 1; influences with a weight of 0 or less are dropped, and the
 * remaining ones are kept heaviest first.  Zero influences make the
 * vertex follow the default bone given to lrg_skinned_sprite_deform()
 * (the slot's bone when drawn by a skeleton).
 *
 * Returns: %TRUE on success, %FALSE if @index or @n_weights is out of
 *   range or a bone index is negative
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_set_weights (LrgSkinnedSprite *self,
                                         guint             index,
                                         guint             n_weights,
                                         const gint       *bones,
                                         const gfloat     *weights);

/**
 * lrg_skinned_sprite_get_weights:
 * @self: an #LrgSkinnedSprite
 * @index: a vertex index
 * @bones: (out caller-allocates) (array fixed-size=4) (optional): the
 *   bone indices, heaviest first; unused entries are -1
 * @weights: (out caller-allocates) (array fixed-size=4) (optional): the
 *   weights (summing to 1); unused entries are 0
 *
 * Reads the influences of one vertex.
 *
 * Returns: the number of influences (0 for an invalid @index)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skinned_sprite_get_weights (LrgSkinnedSprite *self,
                                      guint             index,
                                      gint             *bones,
                                      gfloat           *weights);

/**
 * lrg_skinned_sprite_clear_weights:
 * @self: an #LrgSkinnedSprite
 *
 * Removes every influence from every vertex.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_clear_weights (LrgSkinnedSprite *self);

/**
 * lrg_skinned_sprite_has_weights:
 * @self: an #LrgSkinnedSprite
 *
 * Returns: %TRUE if at least one vertex has an influence
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_has_weights (LrgSkinnedSprite *self);

/**
 * lrg_skinned_sprite_get_max_bone:
 * @self: an #LrgSkinnedSprite
 *
 * Returns: the largest bone index any vertex is weighted to, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_skinned_sprite_get_max_bone (LrgSkinnedSprite *self);

/**
 * lrg_skinned_sprite_add_triangle:
 * @self: an #LrgSkinnedSprite
 * @a: first vertex index
 * @b: second vertex index
 * @c: third vertex index
 *
 * Appends a triangle.  The winding does not matter:
 * lrg_skinned_sprite_draw() emits every triangle front-facing (raylib
 * culls back faces), even when deformation or mirroring flips it.
 *
 * Returns: %TRUE if every index names an existing vertex
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_add_triangle (LrgSkinnedSprite *self,
                                          guint             a,
                                          guint             b,
                                          guint             c);

/**
 * lrg_skinned_sprite_get_triangle_count:
 * @self: an #LrgSkinnedSprite
 *
 * Returns: the number of triangles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skinned_sprite_get_triangle_count (LrgSkinnedSprite *self);

/**
 * lrg_skinned_sprite_get_triangle:
 * @self: an #LrgSkinnedSprite
 * @index: a triangle index
 * @a: (out) (optional): first vertex index
 * @b: (out) (optional): second vertex index
 * @c: (out) (optional): third vertex index
 *
 * Reads a triangle.
 *
 * Returns: %TRUE if @index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_get_triangle (LrgSkinnedSprite *self,
                                          guint             index,
                                          guint            *a,
                                          guint            *b,
                                          guint            *c);

/**
 * lrg_skinned_sprite_clear:
 * @self: an #LrgSkinnedSprite
 *
 * Removes every vertex and triangle (the texture, image and region
 * stay).
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_clear (LrgSkinnedSprite *self);

/* ------------------------------------------------------------------ */
/* Texture                                                             */
/* ------------------------------------------------------------------ */

/**
 * lrg_skinned_sprite_set_texture:
 * @self: an #LrgSkinnedSprite
 * @texture: (nullable): the texture, or %NULL to draw untextured
 *   (vertex colour only)
 *
 * Sets the texture the region is cut from.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_set_texture (LrgSkinnedSprite *self,
                                     GrlTexture       *texture);

/**
 * lrg_skinned_sprite_get_texture:
 * @self: an #LrgSkinnedSprite
 *
 * Returns: (transfer none) (nullable): the texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_skinned_sprite_get_texture (LrgSkinnedSprite *self);

/**
 * lrg_skinned_sprite_set_image:
 * @self: an #LrgSkinnedSprite
 * @image: (nullable): image path (relative to the `.rskel2d` file that
 *   holds the sprite, or absolute), or %NULL
 *
 * Sets the image path the texture is loaded from by
 * lrg_skinned_sprite_load_texture().  It is what `.rskel2d` files store;
 * changing it does not drop an already set texture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_set_image (LrgSkinnedSprite *self,
                                   const gchar      *image);

/**
 * lrg_skinned_sprite_get_image:
 * @self: an #LrgSkinnedSprite
 *
 * Returns: (nullable): the image path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_skinned_sprite_get_image (LrgSkinnedSprite *self);

/**
 * lrg_skinned_sprite_load_texture:
 * @self: an #LrgSkinnedSprite
 * @base_dir: (nullable): directory a relative image path is resolved
 *   against, or %NULL for the current directory
 * @error: return location for a #GError
 *
 * Loads #LrgSkinnedSprite:image into #LrgSkinnedSprite:texture.  Needs
 * an open window (a GL context).  Does nothing and succeeds when there
 * is no image or a texture is already set.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_load_texture (LrgSkinnedSprite  *self,
                                          const gchar       *base_dir,
                                          GError           **error);

/**
 * lrg_skinned_sprite_set_region:
 * @self: an #LrgSkinnedSprite
 * @x: left of the region in texture pixels
 * @y: top of the region in texture pixels
 * @width: region width in pixels, or 0 for the whole texture
 * @height: region height in pixels, or 0 for the whole texture
 *
 * Selects the part of the texture (an atlas region) the vertices' U, V
 * coordinates span.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_set_region (LrgSkinnedSprite *self,
                                    gfloat            x,
                                    gfloat            y,
                                    gfloat            width,
                                    gfloat            height);

/**
 * lrg_skinned_sprite_get_region:
 * @self: an #LrgSkinnedSprite
 * @x: (out) (optional): left in pixels
 * @y: (out) (optional): top in pixels
 * @width: (out) (optional): width in pixels (0 = whole texture)
 * @height: (out) (optional): height in pixels (0 = whole texture)
 *
 * Reads the texture region.
 *
 * Returns: %TRUE if a region is set (a width and height above 0)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_get_region (LrgSkinnedSprite *self,
                                        gfloat           *x,
                                        gfloat           *y,
                                        gfloat           *width,
                                        gfloat           *height);

/* ------------------------------------------------------------------ */
/* Skinning                                                            */
/* ------------------------------------------------------------------ */

/**
 * lrg_skinned_sprite_deform:
 * @self: an #LrgSkinnedSprite
 * @palette: (array length=n_floats) (nullable): 2D affine bone
 *   matrices, six floats per bone `(a, b, c, d, tx, ty)` mapping
 *   `x' = a*x + c*y + tx`, `y' = b*x + d*y + ty`
 * @n_floats: length of @palette (6 * number of bones)
 * @default_bone: bone whose matrix moves vertices without influences,
 *   or -1 to leave them at their bind position
 *
 * Skins the mesh on the CPU (linear blend skinning) into the sprite's
 * deformed-position buffer, read back with
 * lrg_skinned_sprite_get_deformed() and drawn by
 * lrg_skinned_sprite_draw().  Influences naming a bone outside the
 * palette are skipped and the rest renormalised.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_deform (LrgSkinnedSprite *self,
                                const gfloat     *palette,
                                guint             n_floats,
                                gint              default_bone);

/**
 * lrg_skinned_sprite_get_deformed:
 * @self: an #LrgSkinnedSprite
 * @n_floats: (out) (optional): number of floats returned (2 per vertex)
 *
 * The positions of the last lrg_skinned_sprite_deform(), as (x, y)
 * pairs per vertex.  Before the first deform they are the bind
 * positions.
 *
 * Returns: (array length=n_floats) (transfer none): the deformed
 *   positions, valid until the mesh changes
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_skinned_sprite_get_deformed (LrgSkinnedSprite *self,
                                                guint            *n_floats);

/**
 * lrg_skinned_sprite_get_deformed_bounds:
 * @self: an #LrgSkinnedSprite
 * @min_x: (out) (optional): smallest X
 * @min_y: (out) (optional): smallest Y
 * @max_x: (out) (optional): largest X
 * @max_y: (out) (optional): largest Y
 *
 * The axis-aligned bounds of the deformed positions (for picking and
 * culling).
 *
 * Returns: %FALSE when the mesh has no vertices
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_get_deformed_bounds (LrgSkinnedSprite *self,
                                                 gfloat           *min_x,
                                                 gfloat           *min_y,
                                                 gfloat           *max_x,
                                                 gfloat           *max_y);

/**
 * lrg_skinned_sprite_draw:
 * @self: an #LrgSkinnedSprite
 * @tint: (nullable): colour multiplied into the texture, or %NULL for
 *   white
 *
 * Draws the deformed mesh as textured triangles in the current 2D mode
 * (inside BeginDrawing(), optionally BeginMode2D()).  Untextured meshes
 * draw in the tint colour.  Needs a window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_draw (LrgSkinnedSprite *self,
                              const GrlColor   *tint);

/**
 * lrg_skinned_sprite_draw_wireframe:
 * @self: an #LrgSkinnedSprite
 * @color: (nullable): line colour, or %NULL for white
 *
 * Draws the triangle edges of the deformed mesh (the rigging tool's mesh
 * overlay).  Needs a window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_skinned_sprite_draw_wireframe (LrgSkinnedSprite *self,
                                        const GrlColor   *color);

/* ------------------------------------------------------------------ */
/* Automatic weights                                                   */
/* ------------------------------------------------------------------ */

/**
 * lrg_skinned_sprite_compute_auto_weights:
 * @self: an #LrgSkinnedSprite
 * @segments: (array length=n_floats): bone segments in skeleton setup
 *   space, four floats per bone `(x0, y0, x1, y1)` from the bone's
 *   origin to its tip (a zero-length bone is a point)
 * @n_floats: length of @segments (4 * number of segments)
 * @bones: (array length=n_bones) (nullable): skeleton bone index of
 *   each segment, or %NULL when segment i is bone i
 * @n_bones: length of @bones (ignored when @bones is %NULL)
 * @falloff: distance exponent (1 .. 8; 2 to 4 is typical); higher keeps
 *   each vertex closer to its nearest bone
 * @max_influences: influences kept per vertex (1 ..
 *   %LRG_SKINNED_SPRITE_MAX_WEIGHTS)
 * @smooth_iterations: passes of neighbour averaging over the triangle
 *   graph (0 = none), which spreads weights along the surface the way
 *   heat diffusion does
 *
 * Computes automatic weights: each vertex gets `1 / (d + e)^falloff`
 * for its distance `d` to each bone segment (`e` is 1% of the mesh
 * size, so a vertex on a bone does not divide by zero), the weights are
 * optionally smoothed over the mesh, the heaviest @max_influences are
 * kept and normalised to sum to 1.  The nearest bone always dominates a
 * vertex lying on it.  Existing weights are replaced.
 *
 * Returns: %TRUE on success, %FALSE if there are no segments or the
 *   arguments are inconsistent
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skinned_sprite_compute_auto_weights (LrgSkinnedSprite *self,
                                                  const gfloat     *segments,
                                                  guint             n_floats,
                                                  const gint       *bones,
                                                  guint             n_bones,
                                                  gfloat            falloff,
                                                  guint             max_influences,
                                                  guint             smooth_iterations);

G_END_DECLS
