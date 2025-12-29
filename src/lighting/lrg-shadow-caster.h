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
#include "../lrg-version.h"

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

G_END_DECLS
