/* lrg-character-controller.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCharacterController - an upright capsule that moves and slides
 * through an LrgCollisionMesh, the bodies of an LrgDynamicsWorld (0.9.0)
 * and an optional terrain height field:
 * wall sliding, step climbing, a slope limit and ground snapping, with
 * the semantics of Unity's CharacterController.Move(). Gravity, jumping
 * and speeds belong to the caller.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-collision-mesh.h"
#include "lrg-dynamics.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHARACTER_CONTROLLER (lrg_character_controller_get_type ())

/**
 * LRG_CHARACTER_CONTROLLER_DEFAULT_STEP_HEIGHT:
 *
 * Default highest step climbed without jumping (world units).
 */
#define LRG_CHARACTER_CONTROLLER_DEFAULT_STEP_HEIGHT (0.3f)

/**
 * LRG_CHARACTER_CONTROLLER_DEFAULT_MAX_SLOPE:
 *
 * Default steepest walkable slope in degrees.
 */
#define LRG_CHARACTER_CONTROLLER_DEFAULT_MAX_SLOPE (45.0f)

/**
 * LRG_CHARACTER_CONTROLLER_DEFAULT_SKIN_WIDTH:
 *
 * Default gap the capsule keeps from geometry (world units).
 */
#define LRG_CHARACTER_CONTROLLER_DEFAULT_SKIN_WIDTH (0.01f)

/**
 * LrgHeightFunc:
 * @x: world X
 * @z: world Z
 * @height: (out): ground height under (@x, @z)
 * @user_data: (closure): data given to
 *   lrg_character_controller_set_height_func()
 *
 * A terrain height field: everything below the returned height is
 * solid.
 *
 * Returns: %TRUE when there is terrain under the point
 */
typedef gboolean (*LrgHeightFunc) (gfloat    x,
                                   gfloat    z,
                                   gfloat   *height,
                                   gpointer  user_data);

/**
 * LrgCharacterMove:
 * @position: (array fixed-size=3): feet (capsule bottom) after the move
 * @grounded: whether the capsule stands on walkable ground afterwards
 * @ground_normal: (array fixed-size=3): unit normal of that ground, +Y
 *   when not grounded
 * @ground_instance_id: instance id of the mesh instance stood on (a
 *   moving platform), 0 for the mesh's own triangles, terrain or air
 * @hit_wall: the horizontal part of the move was blocked or deflected
 *   by a surface steeper than the slope limit
 * @hit_ceiling: the upward part of the move was blocked
 * @stepped: the move climbed onto a step
 * @ground_body: the #LrgDynamicsWorld body stood on (a platform, a crate),
 *   %LRG_PHYSICS_NO_BODY when the ground is the collision mesh, terrain or
 *   air (0.9.0)
 *
 * Result of lrg_character_controller_move(). Plain value type.
 */
typedef struct
{
    gfloat   position[3];
    gboolean grounded;
    gfloat   ground_normal[3];
    guint64  ground_instance_id;
    gboolean hit_wall;
    gboolean hit_ceiling;
    gboolean stepped;
    guint    ground_body;
} LrgCharacterMove;

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCharacterController, lrg_character_controller, LRG, CHARACTER_CONTROLLER, GObject)

/**
 * lrg_character_controller_new:
 * @radius: capsule radius (> 0)
 * @height: capsule height from the feet to the top (>= 2 * @radius)
 *
 * Creates a controller with the default step height, slope limit and
 * skin width and no world: until lrg_character_controller_set_world()
 * the capsule moves freely.
 *
 * Returns: (transfer full): a new #LrgCharacterController
 */
LRG_AVAILABLE_IN_ALL
LrgCharacterController *
lrg_character_controller_new (gfloat radius,
                              gfloat height);

/**
 * lrg_character_controller_set_radius:
 * @self: an #LrgCharacterController
 * @radius: capsule radius; non-positive or non-finite values are ignored
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_radius (LrgCharacterController *self,
                                     gfloat                  radius);

/**
 * lrg_character_controller_get_radius:
 * @self: an #LrgCharacterController
 *
 * Returns: the capsule radius
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_character_controller_get_radius (LrgCharacterController *self);

/**
 * lrg_character_controller_set_height:
 * @self: an #LrgCharacterController
 * @height: capsule height (crouching lowers it); clamped up to
 *   2 * radius, non-finite values are ignored
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_height (LrgCharacterController *self,
                                     gfloat                  height);

/**
 * lrg_character_controller_get_height:
 * @self: an #LrgCharacterController
 *
 * Returns: the capsule height
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_character_controller_get_height (LrgCharacterController *self);

/**
 * lrg_character_controller_set_step_height:
 * @self: an #LrgCharacterController
 * @step_height: highest step climbed without jumping, 0 disables
 *   stepping and ground snapping; negative values are ignored
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_step_height (LrgCharacterController *self,
                                          gfloat                  step_height);

/**
 * lrg_character_controller_get_step_height:
 * @self: an #LrgCharacterController
 *
 * Returns: the step height
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_character_controller_get_step_height (LrgCharacterController *self);

/**
 * lrg_character_controller_set_max_slope:
 * @self: an #LrgCharacterController
 * @degrees: steepest walkable slope, clamped to 0..89.9
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_max_slope (LrgCharacterController *self,
                                        gfloat                  degrees);

/**
 * lrg_character_controller_get_max_slope:
 * @self: an #LrgCharacterController
 *
 * Returns: the slope limit in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_character_controller_get_max_slope (LrgCharacterController *self);

/**
 * lrg_character_controller_set_skin_width:
 * @self: an #LrgCharacterController
 * @skin: gap kept between the capsule and geometry, clamped to
 *   0.0001..radius/2
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_skin_width (LrgCharacterController *self,
                                         gfloat                  skin);

/**
 * lrg_character_controller_get_skin_width:
 * @self: an #LrgCharacterController
 *
 * Returns: the skin width
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_character_controller_get_skin_width (LrgCharacterController *self);

/**
 * lrg_character_controller_set_world:
 * @self: an #LrgCharacterController
 * @world: (nullable): the geometry to collide with, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_world (LrgCharacterController *self,
                                    LrgCollisionMesh       *world);

/**
 * lrg_character_controller_get_world:
 * @self: an #LrgCharacterController
 *
 * Returns: (transfer none) (nullable): the collision mesh
 */
LRG_AVAILABLE_IN_ALL
LrgCollisionMesh *
lrg_character_controller_get_world (LrgCharacterController *self);

/**
 * lrg_character_controller_set_physics:
 * @self: an #LrgCharacterController
 * @physics: (nullable): the physics world whose bodies the capsule collides
 *   with, %NULL for none
 * @layer_mask: object layers that block the capsule
 *   (%LRG_PHYSICS_LAYERS_ALL for every layer)
 *
 * Makes the capsule collide with the bodies of @physics (static level
 * geometry, kinematic platforms, dynamic crates) through
 * lrg_dynamics_world_shape_cast() and contact queries.  It can be combined
 * with lrg_character_controller_set_world(): each sweep takes the nearer
 * hit.  Sensors never block.  The controller does not push bodies; give the
 * character a kinematic body of its own (and pass it to
 * lrg_character_controller_set_ignore_body()) to push them.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_physics (LrgCharacterController *self,
                                      LrgDynamicsWorld       *physics,
                                      guint32                 layer_mask);

/**
 * lrg_character_controller_get_physics:
 * @self: an #LrgCharacterController
 *
 * Returns: (transfer none) (nullable): the physics world
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsWorld *
lrg_character_controller_get_physics (LrgCharacterController *self);

/**
 * lrg_character_controller_get_layer_mask:
 * @self: an #LrgCharacterController
 *
 * Returns: the layer mask given to lrg_character_controller_set_physics()
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_character_controller_get_layer_mask (LrgCharacterController *self);

/**
 * lrg_character_controller_set_ignore_body:
 * @self: an #LrgCharacterController
 * @body_id: a body the capsule never collides with (the character's own
 *   kinematic body), %LRG_PHYSICS_NO_BODY for none
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_ignore_body (LrgCharacterController *self,
                                          guint                   body_id);

/**
 * lrg_character_controller_get_ignore_body:
 * @self: an #LrgCharacterController
 *
 * Returns: the ignored body, %LRG_PHYSICS_NO_BODY when none
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_character_controller_get_ignore_body (LrgCharacterController *self);

/**
 * lrg_character_controller_set_height_func:
 * @self: an #LrgCharacterController
 * @func: (scope notified) (closure user_data) (destroy destroy) (nullable):
 *   terrain sampler, %NULL for none
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when replaced or finalized
 *
 * Adds a height field the capsule cannot go below (terrain). Terrain
 * ignores the slope limit: it is assumed smooth.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_set_height_func (LrgCharacterController *self,
                                          LrgHeightFunc           func,
                                          gpointer                user_data,
                                          GDestroyNotify          destroy);

/**
 * lrg_character_controller_move:
 * @self: an #LrgCharacterController
 * @feet: (array fixed-size=3): capsule bottom before the move
 * @motion: (array fixed-size=3): desired displacement
 * @snap_to_ground: keep contact with ground up to the step height below
 *   (walking down stairs and ramps); ignored while @motion goes up
 * @out_move: (out caller-allocates): where the capsule ended and why
 *
 * Moves the capsule:
 *
 * 1. pushes it out of geometry it already penetrates;
 * 2. the upward part of @motion, stopping at ceilings;
 * 3. the horizontal part with collide-and-slide (up to four contacts):
 *    walkable slopes deflect the motion along them, steeper surfaces
 *    act as vertical walls; when blocked, the same move is retried
 *    raised by the step height and dropped back down, and kept when it
 *    lands on walkable ground further along;
 * 4. the downward part, sliding down surfaces steeper than the limit
 *    and stopping on walkable ground;
 * 5. ground snapping, the terrain floor and the final ground probe.
 *
 * Without a world or physics world (and without a height function) the capsule moves by
 * exactly @motion and is never grounded. Non-finite input leaves the
 * capsule at @feet.
 *
 * Returns: %FALSE when the input was rejected (non-finite; @out_move
 *   then holds @feet), %TRUE when the move ran
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_character_controller_move (LrgCharacterController *self,
                               const gfloat           *feet,
                               const gfloat           *motion,
                               gboolean                snap_to_ground,
                               LrgCharacterMove       *out_move);

/**
 * lrg_character_controller_can_stand:
 * @self: an #LrgCharacterController
 * @feet: (array fixed-size=3): capsule bottom
 * @height: capsule height to test (standing up from a crouch)
 *
 * Returns: %TRUE when a capsule of @height at @feet overlaps no
 *   geometry (the skin width is allowed)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_character_controller_can_stand (LrgCharacterController *self,
                                    const gfloat           *feet,
                                    gfloat                  height);

/**
 * lrg_character_controller_debug_draw:
 * @self: an #LrgCharacterController
 * @draw: the debug draw batch to record into
 * @feet: (array fixed-size=3): capsule bottom
 * @grounded: whether the last move ended grounded (green, else orange)
 *
 * Records the capsule (and its skin as a thin outer capsule) for one frame
 * on %LRG_DEBUG_CATEGORY_PHYSICS.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_character_controller_debug_draw (LrgCharacterController *self,
                                     LrgDebugDraw           *draw,
                                     const gfloat           *feet,
                                     gboolean                grounded);

G_END_DECLS
