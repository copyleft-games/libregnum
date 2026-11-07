/* lrg-cloth-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgClothComponent: an #LrgCloth grid hanging from its entity (a flag,
 * a banner, a curtain, a cape), simulated in fixed_update and drawn as a
 * streamed mesh render item.  Pins follow the entity, bones follow an
 * optional skeleton, and the particles collide with the world's
 * #LrgDynamicsWorld.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"
#include "../../physics/lrg-cloth.h"

G_BEGIN_DECLS

/**
 * LrgClothPinEdges:
 * @LRG_CLOTH_PIN_NONE: nothing pinned (the cloth falls)
 * @LRG_CLOTH_PIN_TOP: the top row (a curtain, a banner)
 * @LRG_CLOTH_PIN_BOTTOM: the bottom row
 * @LRG_CLOTH_PIN_LEFT: the left column (a flag on its pole)
 * @LRG_CLOTH_PIN_RIGHT: the right column
 * @LRG_CLOTH_PIN_TOP_CORNERS: the two top corners only (a hammock, a
 *   washing line sheet)
 *
 * Which grid vertices #LrgClothComponent pins to its entity.
 */
typedef enum /*< flags >*/
{
	LRG_CLOTH_PIN_NONE        = 0,
	LRG_CLOTH_PIN_TOP         = 1 << 0,
	LRG_CLOTH_PIN_BOTTOM      = 1 << 1,
	LRG_CLOTH_PIN_LEFT        = 1 << 2,
	LRG_CLOTH_PIN_RIGHT       = 1 << 3,
	LRG_CLOTH_PIN_TOP_CORNERS = 1 << 4
} LrgClothPinEdges;

LRG_AVAILABLE_IN_ALL
GType lrg_cloth_pin_edges_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CLOTH_PIN_EDGES (lrg_cloth_pin_edges_get_type ())

#define LRG_TYPE_CLOTH_COMPONENT (lrg_cloth_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgClothComponent, lrg_cloth_component, LRG, CLOTH_COMPONENT, LrgComponent)

/**
 * lrg_cloth_component_new:
 *
 * Creates a 1 x 1 m, 16 x 16 vertex cloth pinned along its top edge.
 *
 * Returns: (transfer full): a new #LrgClothComponent
 */
LRG_AVAILABLE_IN_ALL
LrgClothComponent * lrg_cloth_component_new (void);

/**
 * lrg_cloth_component_new_flag:
 * @width: flag width in metres (along local +X)
 * @height: flag height in metres (down local -Y)
 *
 * Creates a flag: pinned along its left column, with resolution of about
 * 20 vertices per metre (at least 8 per side).
 *
 * Returns: (transfer full): a new #LrgClothComponent
 */
LRG_AVAILABLE_IN_ALL
LrgClothComponent * lrg_cloth_component_new_flag (gfloat width,
                                                  gfloat height);

/**
 * lrg_cloth_component_get_cloth:
 * @self: an #LrgClothComponent
 *
 * The simulated cloth, for everything the properties do not cover
 * (custom pins, bone attachments, colliders, a custom mesh with
 * lrg_cloth_set_mesh()).  Changing the grid properties rebuilds the grid
 * and drops such changes.
 *
 * Returns: (transfer none): the cloth
 */
LRG_AVAILABLE_IN_ALL
LrgCloth * lrg_cloth_component_get_cloth (LrgClothComponent *self);

/**
 * lrg_cloth_component_set_grid:
 * @self: an #LrgClothComponent
 * @columns: vertices across (2 .. 256)
 * @rows: vertices down (2 .. 256)
 * @width: size along local +X
 * @height: size down local -Y
 *
 * Sets the four grid properties at once (one rebuild).  The grid's top
 * left corner sits at the entity's origin.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_component_set_grid (LrgClothComponent *self,
                                   guint              columns,
                                   guint              rows,
                                   gfloat             width,
                                   gfloat             height);

/**
 * lrg_cloth_component_get_pin_edges:
 * @self: an #LrgClothComponent
 *
 * Returns: the pinned edges
 */
LRG_AVAILABLE_IN_ALL
LrgClothPinEdges lrg_cloth_component_get_pin_edges (LrgClothComponent *self);

/**
 * lrg_cloth_component_set_pin_edges:
 * @self: an #LrgClothComponent
 * @edges: the edges to pin
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_component_set_pin_edges (LrgClothComponent *self,
                                        LrgClothPinEdges   edges);

/**
 * lrg_cloth_component_set_wind:
 * @self: an #LrgClothComponent
 * @x: wind velocity along world X (m/s)
 * @y: along world Y
 * @z: along world Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_component_set_wind (LrgClothComponent *self,
                                   gfloat             x,
                                   gfloat             y,
                                   gfloat             z);

/**
 * lrg_cloth_component_set_color:
 * @self: an #LrgClothComponent
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @a: alpha, 0 .. 1 (below 1 draws the cloth transparent)
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_component_set_color (LrgClothComponent *self,
                                    gfloat             r,
                                    gfloat             g,
                                    gfloat             b,
                                    gfloat             a);

/**
 * lrg_cloth_component_set_skeleton:
 * @self: an #LrgClothComponent
 * @skeleton: (nullable): a skeleton whose model-space world poses drive
 *   the cloth's bone attachments, %NULL for none
 *
 * Every fixed update feeds lrg_cloth_set_bone_matrices_from_skeleton()
 * (the skeleton's poses must be current; an animator component updates
 * them).  Attach vertices with lrg_cloth_attach_to_bone() on
 * lrg_cloth_component_get_cloth().
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_component_set_skeleton (LrgClothComponent *self,
                                       LrgSkeleton       *skeleton);

/**
 * lrg_cloth_component_get_skeleton:
 * @self: an #LrgClothComponent
 *
 * Returns: (transfer none) (nullable): the driving skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton * lrg_cloth_component_get_skeleton (LrgClothComponent *self);

/**
 * lrg_cloth_component_reset:
 * @self: an #LrgClothComponent
 *
 * Puts the cloth back at its rest shape under the entity's current
 * transform (after a teleport).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_component_reset (LrgClothComponent *self);

/**
 * lrg_cloth_component_get_mesh:
 * @self: an #LrgClothComponent
 *
 * The streamed render mesh (created by collect_render() once a graphics
 * context exists).
 *
 * Returns: (transfer none) (nullable): the mesh, or %NULL headless
 */
LRG_AVAILABLE_IN_ALL
GrlMesh * lrg_cloth_component_get_mesh (LrgClothComponent *self);

G_END_DECLS
