/* lrg-baked-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook for baked lighting (0.9.0 M3 contract): lightmaps and
 * SH probe grids.  (stub, being written)
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define LRG_BAKED_GLSL \
    "bool lrg_baked_active() { return false; }\n" \
    "vec3 lrg_baked_diffuse(vec3 p, vec3 n) { return vec3(0.0); }\n"

G_END_DECLS
