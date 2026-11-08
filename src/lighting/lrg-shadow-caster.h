/* lrg-shadow-caster.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shadow caster interface.
 *
 * Interface for objects that can cast shadows in the 2D lighting system.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../tilemap/lrg-tilemap.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHADOW_CASTER (lrg_shadow_caster_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgShadowCaster, lrg_shadow_caster, LRG, SHADOW_CASTER, GObject)

/**
 * LrgShadowEdge:
 * @x1: start X coordinate
 * @y1: start Y coordinate
 * @x2: end X coordinate
 * @y2: end Y coordinate
 *
 * A shadow-casting edge.
 */
typedef struct
{
    gfloat x1, y1;
    gfloat x2, y2;
} LrgShadowEdge;

/**
 * LrgShadowCasterInterface:
 * @parent_iface: parent interface
 * @get_edges: virtual method to get shadow edges
 * @is_opaque: virtual method to check if object is fully opaque
 * @get_shadow_opacity: virtual method to get shadow opacity
 *
 * Interface for shadow-casting objects.
 */
struct _LrgShadowCasterInterface
{
    GTypeInterface parent_iface;

    /**
     * LrgShadowCasterInterface::get_edges:
     * @self: an #LrgShadowCaster
     *
     * Gets the edges that cast shadows.
     *
     * Returns: (transfer full) (element-type LrgShadowEdge): Array of shadow edges
     */
    GPtrArray * (*get_edges) (LrgShadowCaster *self);

    /**
     * LrgShadowCasterInterface::is_opaque:
     * @self: an #LrgShadowCaster
     *
     * Gets whether the object is fully opaque (blocks all light).
     *
     * Returns: %TRUE if fully opaque
     */
    gboolean (*is_opaque) (LrgShadowCaster *self);

    /**
     * LrgShadowCasterInterface::get_shadow_opacity:
     * @self: an #LrgShadowCaster
     *
     * Gets the opacity of shadows cast by this object.
     *
     * Returns: Shadow opacity (0.0 to 1.0)
     */
    gfloat (*get_shadow_opacity) (LrgShadowCaster *self);

    gpointer _reserved[8];
};

/**
 * lrg_shadow_edge_new:
 * @x1: start X
 * @y1: start Y
 * @x2: end X
 * @y2: end Y
 *
 * Creates a new shadow edge.
 *
 * Returns: (transfer full): A new #LrgShadowEdge
 */
LRG_AVAILABLE_IN_ALL
LrgShadowEdge * lrg_shadow_edge_new (gfloat x1,
                                     gfloat y1,
                                     gfloat x2,
                                     gfloat y2);

/**
 * lrg_shadow_edge_copy:
 * @edge: an #LrgShadowEdge
 *
 * Copies a shadow edge.
 *
 * Returns: (transfer full): A copy of @edge
 */
LRG_AVAILABLE_IN_ALL
LrgShadowEdge * lrg_shadow_edge_copy (const LrgShadowEdge *edge);

/**
 * lrg_shadow_edge_free:
 * @edge: an #LrgShadowEdge
 *
 * Frees a shadow edge.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_edge_free (LrgShadowEdge *edge);

LRG_AVAILABLE_IN_ALL
GType lrg_shadow_edge_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADOW_EDGE (lrg_shadow_edge_get_type ())

/**
 * lrg_shadow_caster_get_edges:
 * @self: an #LrgShadowCaster
 *
 * Gets the shadow-casting edges of this object.
 *
 * Returns: (transfer full) (element-type LrgShadowEdge): Array of shadow edges
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_shadow_caster_get_edges (LrgShadowCaster *self);

/**
 * lrg_shadow_caster_is_opaque:
 * @self: an #LrgShadowCaster
 *
 * Gets whether this object is fully opaque.
 *
 * Returns: %TRUE if fully opaque
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shadow_caster_is_opaque (LrgShadowCaster *self);

/**
 * lrg_shadow_caster_get_shadow_opacity:
 * @self: an #LrgShadowCaster
 *
 * Gets the shadow opacity.
 *
 * Returns: Shadow opacity (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shadow_caster_get_shadow_opacity (LrgShadowCaster *self);

/* ==========================================================================
 * LrgShadowOccluder2D (0.9.0)
 * ========================================================================== */

#define LRG_TYPE_SHADOW_OCCLUDER2D (lrg_shadow_occluder2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgShadowOccluder2D, lrg_shadow_occluder2d, LRG, SHADOW_OCCLUDER2D, GObject)

/**
 * lrg_shadow_occluder2d_new:
 *
 * Creates an empty occluder: a set of oriented edges that cast 2D shadows
 * (the counterpart of Godot's LightOccluder2D).  Fill it with polygons,
 * a tilemap's collision or a sprite's alpha outline, then add it to an
 * #LrgLightingManager with lrg_lighting_manager_add_shadow_caster().
 *
 * Edges are stored with the solid side known, so a light only projects
 * the edges facing away from it and the occluder itself stays lit (see
 * #LrgShadowOccluder2D:self-shadow).
 *
 * Returns: (transfer full): a new #LrgShadowOccluder2D
 */
LRG_AVAILABLE_IN_ALL
LrgShadowOccluder2D * lrg_shadow_occluder2d_new (void);

/**
 * lrg_shadow_occluder2d_add_polygon:
 * @self: an #LrgShadowOccluder2D
 * @coords: (array length=n_coords): x0, y0, x1, y1, ... of a closed
 *   polygon in occluder-local pixels, either winding
 * @n_coords: number of floats (twice the point count, at least 6)
 *
 * Adds a solid polygon.  Its edges become shadow edges; edges that
 * exactly cancel an opposite edge already in the occluder (two polygons
 * sharing a side) are removed, so adjacent shapes merge.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_add_polygon (LrgShadowOccluder2D *self,
                                        const gfloat        *coords,
                                        guint                n_coords);

/**
 * lrg_shadow_occluder2d_add_rect:
 * @self: an #LrgShadowOccluder2D
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 *
 * Adds a solid rectangle (see lrg_shadow_occluder2d_add_polygon()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_add_rect (LrgShadowOccluder2D *self,
                                     gfloat               x,
                                     gfloat               y,
                                     gfloat               width,
                                     gfloat               height);

/**
 * lrg_shadow_occluder2d_add_tilemap:
 * @self: an #LrgShadowOccluder2D
 * @tilemap: the tilemap
 * @layer_index: the layer, or -1 for every collision-enabled layer
 * @x: where the tilemap is drawn (world x of its origin)
 * @y: where the tilemap is drawn (world y)
 *
 * Adds the collision of a tilemap: every %LRG_TILE_PROPERTY_SOLID cell
 * without collision polygons as its cell outline (rectangle, diamond or
 * hexagon), every cell whose tile has collision polygons as those
 * polygons (transformed with the cell's flip/rotation and placed like
 * the tile image).  Shared sides cancel, so a wall of solid tiles casts
 * from its outline only.
 *
 * Returns: the number of tiles added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shadow_occluder2d_add_tilemap (LrgShadowOccluder2D *self,
                                         LrgTilemap          *tilemap,
                                         gint                 layer_index,
                                         gfloat               x,
                                         gfloat               y);

/**
 * lrg_shadow_occluder2d_add_alpha_outline:
 * @self: an #LrgShadowOccluder2D
 * @rgba: (array length=n_bytes): RGBA8 pixels, row-major
 * @n_bytes: size of @rgba
 * @width: image width in pixels
 * @height: image height in pixels
 * @stride: bytes per row (0: width * 4)
 * @alpha_threshold: pixels with alpha above this are solid
 * @tolerance: simplification tolerance in pixels (Douglas-Peucker; 0
 *   keeps every pixel corner)
 * @x: where the image's top-left corner lands, occluder-local x
 * @y: occluder-local y
 * @scale: image pixels to occluder pixels
 *
 * Traces the outline of the opaque pixels of an image (a sprite frame)
 * into shadow edges: outer boundaries and holes, simplified.  Runs on
 * the CPU, without a GL context.
 *
 * Returns: the number of outline loops added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shadow_occluder2d_add_alpha_outline (LrgShadowOccluder2D *self,
                                               const guint8        *rgba,
                                               gsize                n_bytes,
                                               guint                width,
                                               guint                height,
                                               guint                stride,
                                               guint8               alpha_threshold,
                                               gfloat               tolerance,
                                               gfloat               x,
                                               gfloat               y,
                                               gfloat               scale);

/**
 * lrg_shadow_occluder2d_add_image_outline:
 * @self: an #LrgShadowOccluder2D
 * @image: the image (converted to RGBA8 in place if needed)
 * @source: (nullable): the region of @image to trace (a sprite frame),
 *   or %NULL for the whole image
 * @alpha_threshold: pixels with alpha above this are solid
 * @tolerance: simplification tolerance in pixels
 * @x: where the region's top-left corner lands, occluder-local x
 * @y: occluder-local y
 * @scale: image pixels to occluder pixels
 *
 * lrg_shadow_occluder2d_add_alpha_outline() on an image or a region of
 * it.
 *
 * Returns: the number of outline loops added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shadow_occluder2d_add_image_outline (LrgShadowOccluder2D *self,
                                               GrlImage            *image,
                                               const GrlRectangle  *source,
                                               guint8               alpha_threshold,
                                               gfloat               tolerance,
                                               gfloat               x,
                                               gfloat               y,
                                               gfloat               scale);

/**
 * lrg_shadow_occluder2d_clear:
 * @self: an #LrgShadowOccluder2D
 *
 * Removes every edge.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_clear (LrgShadowOccluder2D *self);

/**
 * lrg_shadow_occluder2d_get_edge_count:
 * @self: an #LrgShadowOccluder2D
 *
 * Returns: the number of shadow edges
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shadow_occluder2d_get_edge_count (LrgShadowOccluder2D *self);

/**
 * lrg_shadow_occluder2d_get_edge:
 * @self: an #LrgShadowOccluder2D
 * @index: the edge index
 * @out_edge: (out caller-allocates): the edge in world pixels (position
 *   applied), running with the solid side on its right on screen
 *   (clockwise round solids, y down)
 *
 * Returns: %TRUE if @index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shadow_occluder2d_get_edge (LrgShadowOccluder2D *self,
                                         guint                index,
                                         LrgShadowEdge       *out_edge);

/**
 * lrg_shadow_occluder2d_get_position:
 * @self: an #LrgShadowOccluder2D
 * @x: (out) (optional): world x of the occluder's origin
 * @y: (out) (optional): world y
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_get_position (LrgShadowOccluder2D *self,
                                         gfloat              *x,
                                         gfloat              *y);

/**
 * lrg_shadow_occluder2d_set_position:
 * @self: an #LrgShadowOccluder2D
 * @x: world x of the occluder's origin
 * @y: world y
 *
 * Moves the occluder (a sprite's outline follows its sprite) without
 * rebuilding its edges.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_set_position (LrgShadowOccluder2D *self,
                                         gfloat               x,
                                         gfloat               y);

/**
 * lrg_shadow_occluder2d_get_opacity:
 * @self: an #LrgShadowOccluder2D
 *
 * Returns: how much light the shadows remove (0 to 1)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shadow_occluder2d_get_opacity (LrgShadowOccluder2D *self);

/**
 * lrg_shadow_occluder2d_set_opacity:
 * @self: an #LrgShadowOccluder2D
 * @opacity: how much light the shadows remove (0 to 1; glass, foliage)
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_set_opacity (LrgShadowOccluder2D *self,
                                        gfloat               opacity);

/**
 * lrg_shadow_occluder2d_get_self_shadow:
 * @self: an #LrgShadowOccluder2D
 *
 * Returns: whether the occluder's own area is shadowed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shadow_occluder2d_get_self_shadow (LrgShadowOccluder2D *self);

/**
 * lrg_shadow_occluder2d_set_self_shadow:
 * @self: an #LrgShadowOccluder2D
 * @self_shadow: %TRUE to project every edge, darkening the occluder's
 *   own area too (flat top-down walls); %FALSE (the default) projects
 *   only the edges facing away from the light, so the occluder stays lit
 *   (characters, props, walls seen from the side)
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_set_self_shadow (LrgShadowOccluder2D *self,
                                            gboolean             self_shadow);

/**
 * lrg_shadow_occluder2d_get_enabled:
 * @self: an #LrgShadowOccluder2D
 *
 * Returns: whether the occluder casts shadows
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shadow_occluder2d_get_enabled (LrgShadowOccluder2D *self);

/**
 * lrg_shadow_occluder2d_set_enabled:
 * @self: an #LrgShadowOccluder2D
 * @enabled: whether the occluder casts shadows
 */
LRG_AVAILABLE_IN_ALL
void lrg_shadow_occluder2d_set_enabled (LrgShadowOccluder2D *self,
                                        gboolean             enabled);

G_END_DECLS
