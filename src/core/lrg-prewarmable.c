/* lrg-prewarmable.c - Interface for objects that build GPU resources ahead of use
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-prewarmable.h"

#include <raylib.h>

/**
 * SECTION:lrg-prewarmable
 * @title: LrgPrewarmable
 * @short_description: Objects that can build their GPU resources ahead of use
 *
 * Many renderers create their GPU resources lazily: the water surface
 * compiles its shader and uploads its patches on its first draw, a post
 * effect compiles its shader on the first frame it is not identity. On a
 * real driver that is a 5-100 ms freeze the moment the feature appears.
 * #LrgPrewarmable objects can do that work earlier, behind a loading
 * screen: lrg_prewarmable_prewarm() builds everything the first draw would
 * build, after which the first draw costs what every other draw costs.
 *
 * The engine's own renderers implement it (#LrgLightSet,
 * #LrgShadowRenderer, #LrgBillboardBatch, #LrgGrassField, #LrgWaterSurface,
 * #LrgCloudLayer, #LrgGroundOverlay, #LrgSkinnedMesh, #LrgModelAnimator,
 * #LrgStaticBatch, #LrgTerrain, the round 3D shapes, the post effects with
 * lazily compiled shaders and the photo-mode passes) and add themselves to
 * the default #LrgPrewarm queue when they are created, so a loading screen
 * that drains the queue warms them all.
 *
 * ## Implementing LrgPrewarmable
 *
 * |[<!-- language="C" -->
 * static gboolean
 * my_renderer_prewarm (LrgPrewarmable *prewarmable)
 * {
 *     MyRenderer *self = MY_RENDERER (prewarmable);
 *
 *     my_renderer_ensure_shader (self);
 *     return TRUE;
 * }
 *
 * static void
 * my_renderer_prewarmable_init (LrgPrewarmableInterface *iface)
 * {
 *     iface->prewarm = my_renderer_prewarm;
 * }
 *
 * static void
 * my_renderer_init (MyRenderer *self)
 * {
 *     lrg_prewarm_track (LRG_PREWARMABLE (self));
 * }
 * ]|
 */

G_DEFINE_INTERFACE (LrgPrewarmable, lrg_prewarmable, G_TYPE_OBJECT)

static void
lrg_prewarmable_default_init (LrgPrewarmableInterface *iface)
{
    /* no default: an implementation without prewarm() is warm already */
}

gboolean
lrg_prewarmable_prewarm (LrgPrewarmable *self)
{
    LrgPrewarmableInterface *iface;

    g_return_val_if_fail (LRG_IS_PREWARMABLE (self), TRUE);

    /* GPU work needs the context; without a window there is nothing to
     * build and nothing that could hitch */
    if (!IsWindowReady ())
        return TRUE;

    iface = LRG_PREWARMABLE_GET_IFACE (self);
    if (iface->prewarm == NULL)
        return TRUE;
    return iface->prewarm (self);
}
