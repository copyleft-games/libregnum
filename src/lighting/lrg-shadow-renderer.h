/* lrg-shadow-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgShadowRenderer - 3D shadow maps for LrgLightSet: cascaded shadow
 * maps for the directional light (sun or moon) and cube shadow maps for
 * a budget of point lights (a torch, spell lights, key lights), all in
 * two depth atlases. The game draws its shadow casters once per pass
 * between lrg_shadow_renderer_begin_pass() and
 * lrg_shadow_renderer_end_pass() with lrg_shadow_renderer_get_caster_shader();
 * a light set given the renderer (lrg_light_set_set_shadow_renderer())
 * then darkens what the casters hide. The cascade splits, the light
 * matrices and the choice of shadowed point lights are headless; only
 * the passes and the textures need GL.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../graphics/lrg-light-set.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHADOW_RENDERER (lrg_shadow_renderer_get_type ())

/**
 * LRG_SHADOW_MAX_CASCADES:
 *
 * Most cascades of the directional light's shadow map.
 */
#define LRG_SHADOW_MAX_CASCADES (4)

/**
 * LRG_SHADOW_MAX_POINT_LIGHTS:
 *
 * Most point lights that cast shadows at once.
 */
#define LRG_SHADOW_MAX_POINT_LIGHTS (8)

/**
 * LRG_SHADOW_NO_SLOT:
 *
 * The shadow slot of a light that casts no shadow this frame.
 */
#define LRG_SHADOW_NO_SLOT (-1)

/**
 * LRG_SHADOW_SUN_UNIT:
 *
 * Texture unit of the directional light's shadow atlas while a light
 * set with a renderer draws (above raylib's material maps).
 */
#define LRG_SHADOW_SUN_UNIT (12)

/**
 * LRG_SHADOW_POINT_UNIT:
 *
 * Texture unit of the point lights' shadow atlas.
 */
#define LRG_SHADOW_POINT_UNIT (13)

/**
 * LrgShadowPassKind:
 * @LRG_SHADOW_PASS_NONE: no such pass
 * @LRG_SHADOW_PASS_CASCADE: a cascade of the directional light
 * @LRG_SHADOW_PASS_CUBE_FACE: one face of a point light's cube
 *
 * What a shadow pass renders.
 */
typedef enum
{
    LRG_SHADOW_PASS_NONE,
    LRG_SHADOW_PASS_CASCADE,
    LRG_SHADOW_PASS_CUBE_FACE
} LrgShadowPassKind;

LRG_AVAILABLE_IN_ALL
GType lrg_shadow_pass_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADOW_PASS_KIND (lrg_shadow_pass_kind_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgShadowRenderer, lrg_shadow_renderer, LRG, SHADOW_RENDERER, GObject)

/* ------------------------------------------------------------------------ */
/* Matrix helpers (column-major 4x4, as OpenGL and raylib's Matrix)         */
/* ------------------------------------------------------------------------ */

/**
 * lrg_shadow_look_at:
 * @eye: (array fixed-size=3): the viewer
 * @target: (array fixed-size=3): the point looked at
 * @up: (array fixed-size=3): the up hint (not parallel to the view)
 * @out_view: (out caller-allocates) (array fixed-size=16): the view matrix
 *
 * A right-handed view matrix (raylib's MatrixLookAt()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_look_at (const gfloat *eye,
                    const gfloat *target,
                    const gfloat *up,
                    gfloat       *out_view);

/**
 * lrg_shadow_ortho:
 * @left: left plane
 * @right: right plane
 * @bottom: bottom plane
 * @top: top plane
 * @near: near plane
 * @far: far plane
 * @out_proj: (out caller-allocates) (array fixed-size=16): the projection
 *
 * An orthographic projection (glOrtho()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_ortho (gfloat  left,
                  gfloat  right,
                  gfloat  bottom,
                  gfloat  top,
                  gfloat  near,
                  gfloat  far,
                  gfloat *out_proj);

/**
 * lrg_shadow_perspective:
 * @fovy_degrees: vertical field of view
 * @aspect: width over height
 * @near: near plane (> 0)
 * @far: far plane (> @near)
 * @out_proj: (out caller-allocates) (array fixed-size=16): the projection
 *
 * A perspective projection (gluPerspective()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_perspective (gfloat  fovy_degrees,
                        gfloat  aspect,
                        gfloat  near,
                        gfloat  far,
                        gfloat *out_proj);

/**
 * lrg_shadow_multiply:
 * @a: (array fixed-size=16): left matrix
 * @b: (array fixed-size=16): right matrix
 * @out: (out caller-allocates) (array fixed-size=16): @a x @b (may alias)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_multiply (const gfloat *a,
                     const gfloat *b,
                     gfloat       *out);

/**
 * lrg_shadow_transform_point:
 * @m: (array fixed-size=16): a matrix
 * @point: (array fixed-size=3): a point
 * @out: (out caller-allocates) (array fixed-size=3): @m x (@point, 1)
 *   divided by w
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_transform_point (const gfloat *m,
                            const gfloat *point,
                            gfloat       *out);

/**
 * lrg_shadow_split_distances:
 * @near: the camera's near plane (> 0)
 * @far: the shadow distance (> @near)
 * @count: cascades, 1 .. %LRG_SHADOW_MAX_CASCADES
 * @lambda: 0 (uniform splits) .. 1 (logarithmic splits)
 * @out_splits: (out caller-allocates) (array length=count): the far end
 *   of each cascade; the last is @far
 *
 * The practical split scheme: each split blends the logarithmic and the
 * uniform split of @near .. @far by @lambda, so near cascades are small
 * (sharp) and far ones large.
 *
 * Returns: %FALSE for bad arguments (@out_splits untouched)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_shadow_split_distances (gfloat  near,
                            gfloat  far,
                            guint   count,
                            gfloat  lambda,
                            gfloat *out_splits);

/**
 * lrg_shadow_cube_face_matrices:
 * @face: 0 .. 5: +X, -X, +Y, -Y, +Z, -Z
 * @light: (array fixed-size=3): the light, in draw space
 * @radius: the light's reach (the far plane)
 * @out_view: (out caller-allocates) (array fixed-size=16): view matrix
 * @out_proj: (out caller-allocates) (array fixed-size=16): 90 degree
 *   projection
 *
 * The camera of one face of a point light's cube shadow map. The light
 * shader finds the face of a direction with the same axes.
 *
 * Returns: %FALSE for a face outside 0 .. 5
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_shadow_cube_face_matrices (guint         face,
                               const gfloat *light,
                               gfloat        radius,
                               gfloat       *out_view,
                               gfloat       *out_proj);

/* ------------------------------------------------------------------------ */
/* The renderer                                                             */
/* ------------------------------------------------------------------------ */

/**
 * lrg_shadow_renderer_new:
 *
 * Creates a renderer with one 2048-texel cascade reaching 80 units, no
 * point shadows, a depth bias of 0.0015, a normal offset of 1.5 texels
 * and a 3 x 3 filter.
 *
 * Returns: (transfer full): a new #LrgShadowRenderer
 */
LRG_AVAILABLE_IN_ALL
LrgShadowRenderer *
lrg_shadow_renderer_new (void);

/**
 * lrg_shadow_renderer_set_sun:
 * @self: an #LrgShadowRenderer
 * @resolution: texels of one cascade's side (64 .. 4096), or 0 for no
 *   directional shadow
 * @cascades: 1 .. %LRG_SHADOW_MAX_CASCADES
 * @distance: how far from the camera the directional shadow reaches (> 0)
 * @split_lambda: 0 .. 1, see lrg_shadow_split_distances()
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_set_sun (LrgShadowRenderer *self,
                             guint              resolution,
                             guint              cascades,
                             gfloat             distance,
                             gfloat             split_lambda);

/**
 * lrg_shadow_renderer_get_sun:
 * @self: an #LrgShadowRenderer
 * @out_resolution: (out) (optional): cascade side, 0 when off
 * @out_cascades: (out) (optional): cascades
 * @out_distance: (out) (optional): reach
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_get_sun (LrgShadowRenderer *self,
                             guint             *out_resolution,
                             guint             *out_cascades,
                             gfloat            *out_distance);

/**
 * lrg_shadow_renderer_set_points:
 * @self: an #LrgShadowRenderer
 * @resolution: texels of a cube face's side (32 .. 2048)
 * @max_lights: point lights with a cube per frame, 0 ..
 *   %LRG_SHADOW_MAX_POINT_LIGHTS (0: none)
 * @max_distance: a light whose sphere lies further than this from the
 *   camera gets no cube; 0 for no limit
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_set_points (LrgShadowRenderer *self,
                                guint              resolution,
                                guint              max_lights,
                                gfloat             max_distance);

/**
 * lrg_shadow_renderer_get_points:
 * @self: an #LrgShadowRenderer
 * @out_resolution: (out) (optional): face side
 * @out_max_lights: (out) (optional): the budget
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_get_points (LrgShadowRenderer *self,
                                guint             *out_resolution,
                                guint             *out_max_lights);

/**
 * lrg_shadow_renderer_set_bias:
 * @self: an #LrgShadowRenderer
 * @depth_bias: subtracted from a fragment's depth before the test (in
 *   0 .. 1 depth for cascades, in radii for point lights)
 * @normal_offset: how far a fragment is pushed along its normal before
 *   the lookup, in shadow texels (against acne on slopes)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_set_bias (LrgShadowRenderer *self,
                              gfloat             depth_bias,
                              gfloat             normal_offset);

/**
 * lrg_shadow_renderer_set_filter:
 * @self: an #LrgShadowRenderer
 * @radius: percentage-closer filter radius in texels, 0 (one tap) .. 2
 *   (5 x 5 taps)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_set_filter (LrgShadowRenderer *self,
                                guint              radius);

/**
 * lrg_shadow_renderer_set_strength:
 * @self: an #LrgShadowRenderer
 * @strength: 0 .. 1: how much of the light a shadow removes (1: all)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_set_strength (LrgShadowRenderer *self,
                                  gfloat             strength);

/**
 * lrg_shadow_renderer_get_strength:
 * @self: an #LrgShadowRenderer
 *
 * Returns: the shadow strength
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_shadow_renderer_get_strength (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_update:
 * @self: an #LrgShadowRenderer
 * @lights: the light set the shadows are for
 * @camera: (array fixed-size=3): the camera in world space
 * @forward: (array fixed-size=3): the view direction
 * @up: (array fixed-size=3): the camera's up
 * @fovy_degrees: vertical field of view
 * @aspect: the view's width over height
 * @near: the camera's near plane (> 0)
 *
 * Plans this frame's shadows: the cascades of @lights' directional light
 * around the camera's view (none without a directional light), and the
 * point lights with a shadow priority (lrg_light_set_set_point_light_shadow())
 * that get a cube: highest priority first, then nearest the camera, up
 * to the budget. Everything is in @lights' draw space (world minus its
 * origin). Headless. Call it before the passes and before
 * lrg_light_set_apply().
 *
 * Returns: the number of passes (lrg_shadow_renderer_get_pass_count())
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_shadow_renderer_update (LrgShadowRenderer *self,
                            LrgLightSet       *lights,
                            const gfloat      *camera,
                            const gfloat      *forward,
                            const gfloat      *up,
                            gfloat             fovy_degrees,
                            gfloat             aspect,
                            gfloat             near);

/**
 * lrg_shadow_renderer_get_cascade_count:
 * @self: an #LrgShadowRenderer
 *
 * Returns: cascades planned by the last update (0: no directional shadow)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_shadow_renderer_get_cascade_count (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_get_cascade:
 * @self: an #LrgShadowRenderer
 * @index: 0 .. cascade count - 1
 * @out_near: (out) (optional): where the cascade begins (view distance)
 * @out_far: (out) (optional): where it ends
 * @out_matrix: (out caller-allocates) (optional) (array fixed-size=16):
 *   its light view-projection matrix (draw space to clip space)
 *
 * Returns: %FALSE for an index out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_shadow_renderer_get_cascade (LrgShadowRenderer *self,
                                 guint              index,
                                 gfloat            *out_near,
                                 gfloat            *out_far,
                                 gfloat            *out_matrix);

/**
 * lrg_shadow_renderer_get_point_count:
 * @self: an #LrgShadowRenderer
 *
 * Returns: point lights with a cube after the last update
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_shadow_renderer_get_point_count (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_get_point_light:
 * @self: an #LrgShadowRenderer
 * @slot: 0 .. point count - 1
 *
 * Returns: the light set's id of the light in @slot, or %G_MAXUINT
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_shadow_renderer_get_point_light (LrgShadowRenderer *self,
                                     guint              slot);

/**
 * lrg_shadow_renderer_get_light_slot:
 * @self: an #LrgShadowRenderer
 * @light: a light id of the light set of the last update
 *
 * Returns: the shadow slot of @light, or %LRG_SHADOW_NO_SLOT
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_shadow_renderer_get_light_slot (LrgShadowRenderer *self,
                                    guint              light);

/**
 * lrg_shadow_renderer_get_pass_count:
 * @self: an #LrgShadowRenderer
 *
 * Returns: the passes of this frame: one per cascade, then six per
 *   shadowed point light
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_shadow_renderer_get_pass_count (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_get_pass:
 * @self: an #LrgShadowRenderer
 * @pass: a pass index
 * @out_index: (out) (optional): the cascade, or the point slot
 * @out_face: (out) (optional): the cube face of a point pass
 * @out_view_proj: (out caller-allocates) (optional) (array fixed-size=16):
 *   the pass's view-projection matrix (draw space)
 *
 * Returns: what @pass renders, %LRG_SHADOW_PASS_NONE out of range
 */
LRG_AVAILABLE_IN_ALL
LrgShadowPassKind
lrg_shadow_renderer_get_pass (LrgShadowRenderer *self,
                              guint              pass,
                              guint             *out_index,
                              guint             *out_face,
                              gfloat            *out_view_proj);

/**
 * lrg_shadow_renderer_begin_pass:
 * @self: an #LrgShadowRenderer
 * @pass: a pass index
 *
 * Binds the pass's part of its atlas (clearing the atlas at its first
 * pass of the frame), loads the pass's view and projection matrices into
 * rlgl (so DrawMesh() and frustum culling read them), turns depth testing
 * on and face culling off, and points the caster shader at the pass.
 * Must be called outside BeginMode3D() and BeginTextureMode(), with the
 * screen as the target. Draw the casters with
 * lrg_shadow_renderer_get_caster_shader(), then end the pass.
 *
 * Returns: %FALSE (nothing bound) without a context, a shader or such a
 *   pass
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_shadow_renderer_begin_pass (LrgShadowRenderer *self,
                                guint              pass);

/**
 * lrg_shadow_renderer_get_pass_state:
 * @self: an #LrgShadowRenderer
 * @out_mode: (out) (optional): 0 for a cascade pass, 1 for a cube face
 * @out_light: (out caller-allocates) (optional) (array fixed-size=4): the
 *   cube's light position and radius (zeros for a cascade)
 * @out_direction: (out caller-allocates) (optional) (array fixed-size=3):
 *   the way the light travels in a cascade pass (unit, draw space)
 *
 * What the open pass renders, for casters that bring their own program
 * (a billboard batch faces its quads to the light with it) and must
 * write the caster shader's numbers: the window depth for mode 0, the
 * distance to the light over its radius for mode 1.
 *
 * Returns: %FALSE outside a pass (nothing written)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_shadow_renderer_get_pass_state (LrgShadowRenderer *self,
                                    gint              *out_mode,
                                    gfloat            *out_light,
                                    gfloat            *out_direction);

/**
 * lrg_shadow_renderer_mark_rendered:
 * @self: an #LrgShadowRenderer
 * @pass: a pass index of the last lrg_shadow_renderer_update()
 *
 * Records @pass as rendered with the view-projection and light the last
 * update planned: what lrg_shadow_renderer_end_pass() does for the pass it
 * closes, for games that fill a tile by other means and for headless
 * tests of the pass cache (lrg_shadow_renderer_pass_is_current(), the
 * refresh and its staggering).  Out-of-range passes are ignored.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_mark_rendered (LrgShadowRenderer *self,
                                   guint              pass);

/**
 * lrg_shadow_renderer_end_pass:
 * @self: an #LrgShadowRenderer
 *
 * Flushes the casters and restores the screen target, its viewport and
 * 2D matrices and face culling (as EndTextureMode()). Does nothing
 * outside a pass.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_end_pass (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_get_caster_shader:
 * @self: an #LrgShadowRenderer
 *
 * The depth shader casters draw with during a pass (raylib's attribute
 * and uniform names; texels with alpha below 0.5 cast nothing), built on
 * first use with a context.
 *
 * Returns: (transfer none) (nullable): the shader, %NULL without a
 *   context or GLSL 330
 */
LRG_AVAILABLE_IN_ALL
GrlShader *
lrg_shadow_renderer_get_caster_shader (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_is_ready:
 * @self: an #LrgShadowRenderer
 *
 * Returns: %TRUE when every pass of the last update was rendered (or is
 *   current, see lrg_shadow_renderer_set_refresh()), so a light set may
 *   sample the atlases
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_shadow_renderer_is_ready (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_release_gpu:
 * @self: an #LrgShadowRenderer
 *
 * Frees the atlases and the caster shader (call before the window
 * closes); they are made again on the next pass.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_release_gpu (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_set_refresh:
 * @self: an #LrgShadowRenderer
 * @frames: 0 (the default) to render every pass every frame; else a
 *   pass whose camera and light have not changed since its tile was
 *   rendered is reused for up to @frames updates
 *
 * The pass cache: a still torch, a key light or a still camera's
 * cascades cost nothing until they move or @frames updates passed (so
 * moving casters, such as doors, show up within @frames frames).
 * Skip the passes lrg_shadow_renderer_pass_is_current() names.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_set_refresh (LrgShadowRenderer *self,
                                 guint              frames);

/**
 * lrg_shadow_renderer_get_refresh:
 * @self: an #LrgShadowRenderer
 *
 * Returns: the cache's refresh in frames, 0 when off
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_shadow_renderer_get_refresh (LrgShadowRenderer *self);

/**
 * lrg_shadow_renderer_pass_is_current:
 * @self: an #LrgShadowRenderer
 * @pass: a pass index
 *
 * Returns: %TRUE when the last update found @pass's tile still good (the
 *   same camera and light, younger than the refresh): it need not be
 *   rendered this frame
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_shadow_renderer_pass_is_current (LrgShadowRenderer *self,
                                     guint              pass);

/**
 * lrg_shadow_renderer_invalidate:
 * @self: an #LrgShadowRenderer
 *
 * Forgets every cached tile (the casters changed: a new space, streamed
 * geometry); the next update renders every pass.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_shadow_renderer_invalidate (LrgShadowRenderer *self);

G_END_DECLS
