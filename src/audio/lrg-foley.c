/* lrg-foley.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The synthesized foley (see lrg-foley.h).  Since 0.10.0 each kind is a
 * recipe in lrg-foley-patch.c: lrg_foley_synthesize() builds the kind's
 * LrgFoleyPatch and renders it, which gives the same bytes 0.9 did.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <gio/gio.h>
#include <math.h>
#include <string.h>

#include "lrg-foley.h"
#include "lrg-foley-patch.h"

G_DEFINE_ENUM_TYPE (LrgFoleyKind, lrg_foley_kind,
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_DIRT, "footstep-dirt"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_STONE, "footstep-stone"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_WOOD, "footstep-wood"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_GRASS, "footstep-grass"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_GRAVEL, "footstep-gravel"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_MUD, "footstep-mud"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_SNOW, "footstep-snow"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_WATER, "footstep-water"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_ARMOR_CLINK, "armor-clink"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_RAIN_LOOP, "rain-loop"))

/* ==========================================================================
 * The kinds
 * ========================================================================== */

static const gdouble durations[] = {
    0.24, 0.22, 0.24, 0.26, 0.32, 0.36, 0.30, 0.42, 0.60, 4.0
};

gdouble
lrg_foley_get_duration (LrgFoleyKind kind)
{
    if ((guint)kind >= G_N_ELEMENTS (durations))
        return 0.0;
    return durations[kind];
}

GBytes *
lrg_foley_synthesize (LrgFoleyKind   kind,
                      guint          variant,
                      guint          sample_rate,
                      GError       **error)
{
    g_autoptr(LrgFoleyPatch) patch = NULL;

    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* the recipe of the kind as a patch (lrg-foley-patch.c), rendered */
    patch = lrg_foley_patch_new_from_kind (kind, variant, sample_rate, error);
    if (patch == NULL)
        return NULL;
    return lrg_foley_patch_render_wav (patch, error);
}
