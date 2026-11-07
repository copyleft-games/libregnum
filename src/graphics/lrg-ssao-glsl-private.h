/* lrg-ssao-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook (0.9.0 M2 contract, see the light set): the GLSL
 * functions LRG_SSAO_GLSL defines are pasted into the light set's fragment
 * shader, and lrg_ssao_bind_private() binds their uniforms and textures before a draw.
 * A NULL object binds the inactive state.  (stub, being written)
 */

#pragma once

#include <glib.h>
#include "lrg-ssao.h"

G_BEGIN_DECLS

#define LRG_SSAO_GLSL \
    "float lrg_ssao() { return 1.0; }\n"

void lrg_ssao_bind_private (LrgSsao *ssao,
                       guint shader_id,
                       gint *texture_unit);

G_END_DECLS
