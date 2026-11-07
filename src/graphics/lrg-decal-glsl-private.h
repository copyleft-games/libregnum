/* lrg-decal-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook (0.9.0 M2 contract, see the light set): the GLSL
 * functions LRG_DECAL_GLSL defines are pasted into the light set's fragment
 * shader, and lrg_decal_set_bind_private() binds their uniforms and textures before a draw.
 * A NULL object binds the inactive state.  (stub, being written)
 */

#pragma once

#include <glib.h>
#include "lrg-decal.h"

G_BEGIN_DECLS

#define LRG_DECAL_GLSL \
    "void lrg_decals_apply(vec3 p, vec3 ng, inout vec4 albedo, inout vec3 n, inout float rough, inout float metal) { }\n"

void lrg_decal_set_bind_private (LrgDecalSet *decals,
                            guint shader_id,
                            gint *texture_unit);

G_END_DECLS
