/* lrg-vr-draw.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drawing helpers for VR views.
 */

#include "config.h"
#include "lrg-vr-draw.h"

#include <raylib.h>
#include <rlgl.h>

/**
 * SECTION:lrg-vr-draw
 * @title: VR drawing
 * @short_description: World-space textured quads for VR interfaces
 *
 * lrg_vr_draw_quad() stands a texture in the world: the panel a VR game
 * shows its 2D interface on.
 *
 * Since: 1.0
 */

void
lrg_vr_draw_quad (GrlTexture     *texture,
                  const gfloat   *centre,
                  const gfloat   *right,
                  const gfloat   *up,
                  gboolean        flip_v,
                  const GrlColor *tint)
{
    Texture2D *handle;
    gfloat top, bottom;
    guchar r = 255, g = 255, b = 255, a = 255;

    g_return_if_fail (GRL_IS_TEXTURE (texture));
    g_return_if_fail (centre != NULL && right != NULL && up != NULL);

    handle = (Texture2D *) grl_texture_get_handle (texture);
    if (handle == NULL || handle->id == 0)
        return;
    if (tint != NULL)
    {
        r = tint->r;
        g = tint->g;
        b = tint->b;
        a = tint->a;
    }
    /* a render texture's first row is its bottom */
    top = flip_v ? 1.0f : 0.0f;
    bottom = flip_v ? 0.0f : 1.0f;

    rlDrawRenderBatchActive ();
    rlDisableBackfaceCulling ();
    rlSetTexture (handle->id);
    rlBegin (RL_QUADS);
    rlColor4ub (r, g, b, a);
    /* counter-clockwise as the viewer sees it: TL, BL, BR, TR */
    rlTexCoord2f (0.0f, top);
    rlVertex3f (centre[0] - right[0] + up[0], centre[1] - right[1] + up[1],
                centre[2] - right[2] + up[2]);
    rlTexCoord2f (0.0f, bottom);
    rlVertex3f (centre[0] - right[0] - up[0], centre[1] - right[1] - up[1],
                centre[2] - right[2] - up[2]);
    rlTexCoord2f (1.0f, bottom);
    rlVertex3f (centre[0] + right[0] - up[0], centre[1] + right[1] - up[1],
                centre[2] + right[2] - up[2]);
    rlTexCoord2f (1.0f, top);
    rlVertex3f (centre[0] + right[0] + up[0], centre[1] + right[1] + up[1],
                centre[2] + right[2] + up[2]);
    rlEnd ();
    rlSetTexture (0);
    rlDrawRenderBatchActive ();
    rlEnableBackfaceCulling ();
}
