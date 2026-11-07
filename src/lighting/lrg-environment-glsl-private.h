/* lrg-environment-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook (0.9.0 M2 contract, see the light set): the GLSL
 * functions LRG_ENV_GLSL defines are pasted into the light set's fragment
 * shader, and lrg_environment_bind_private() binds their uniforms and textures before a draw.
 * A NULL object binds the inactive state.  (stub, being written)
 */

#pragma once

#include <glib.h>
#include "lrg-environment.h"

G_BEGIN_DECLS

#define LRG_ENV_GLSL \
    "bool lrg_env_active() { return false; }\n" \
    "vec3 lrg_env_diffuse(vec3 n) { return vec3(0.0); }\n" \
    "vec3 lrg_env_specular(vec3 n, vec3 v, float rough) { return vec3(0.0); }\n" \
    "vec2 lrg_env_brdf(float nv, float rough) { return vec2(1.0, 0.0); }\n"

void lrg_environment_bind_private (LrgEnvironment *env,
                              guint shader_id,
                              gint *texture_unit);

G_END_DECLS
