/* lrg-shadow-caster.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shadow caster interface implementation.
 */

#include "lrg-shadow-caster.h"

G_DEFINE_INTERFACE (LrgShadowCaster, lrg_shadow_caster, G_TYPE_OBJECT)

static void
lrg_shadow_caster_default_init (LrgShadowCasterInterface *iface)
{
    /* Default implementations are NULL */
}

/* LrgShadowEdge boxed type */

G_DEFINE_BOXED_TYPE (LrgShadowEdge, lrg_shadow_edge,
                     lrg_shadow_edge_copy, lrg_shadow_edge_free)

LrgShadowEdge *
lrg_shadow_edge_new (gfloat x1,
                     gfloat y1,
                     gfloat x2,
                     gfloat y2)
{
    LrgShadowEdge *edge;

    edge = g_new (LrgShadowEdge, 1);
    edge->x1 = x1;
    edge->y1 = y1;
    edge->x2 = x2;
    edge->y2 = y2;

    return edge;
}

LrgShadowEdge *
lrg_shadow_edge_copy (const LrgShadowEdge *edge)
{
    g_return_val_if_fail (edge != NULL, NULL);
    return lrg_shadow_edge_new (edge->x1, edge->y1, edge->x2, edge->y2);
}

void
lrg_shadow_edge_free (LrgShadowEdge *edge)
{
    g_free (edge);
}

/* Interface method implementations */

GPtrArray *
lrg_shadow_caster_get_edges (LrgShadowCaster *self)
{
    LrgShadowCasterInterface *iface;

    g_return_val_if_fail (LRG_IS_SHADOW_CASTER (self), NULL);

    iface = LRG_SHADOW_CASTER_GET_IFACE (self);

    if (iface->get_edges != NULL)
        return iface->get_edges (self);

    return g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_shadow_edge_free);
}

gboolean
lrg_shadow_caster_is_opaque (LrgShadowCaster *self)
{
    LrgShadowCasterInterface *iface;

    g_return_val_if_fail (LRG_IS_SHADOW_CASTER (self), TRUE);

    iface = LRG_SHADOW_CASTER_GET_IFACE (self);

    if (iface->is_opaque != NULL)
        return iface->is_opaque (self);

    return TRUE;
}

gfloat
lrg_shadow_caster_get_shadow_opacity (LrgShadowCaster *self)
{
    LrgShadowCasterInterface *iface;

    g_return_val_if_fail (LRG_IS_SHADOW_CASTER (self), 1.0f);

    iface = LRG_SHADOW_CASTER_GET_IFACE (self);

    if (iface->get_shadow_opacity != NULL)
        return iface->get_shadow_opacity (self);

    return 1.0f;
}
