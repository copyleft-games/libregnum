/* lrg-render-mode.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-render-mode.h"

GType
lrg_render_mode_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_RENDER_MODE_2D, "LRG_RENDER_MODE_2D", "2d" },
            { LRG_RENDER_MODE_3D, "LRG_RENDER_MODE_3D", "3d" },
            { LRG_RENDER_MODE_3DVR, "LRG_RENDER_MODE_3DVR", "3dvr" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgRenderMode"),
                                    values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

const gchar *
lrg_render_mode_to_string (LrgRenderMode mode)
{
    switch (mode)
    {
    case LRG_RENDER_MODE_3D:
        return "3d";
    case LRG_RENDER_MODE_3DVR:
        return "3dvr";
    case LRG_RENDER_MODE_2D:
    default:
        return "2d";
    }
}

gboolean
lrg_render_mode_from_string (const gchar   *str,
                             LrgRenderMode *out_mode)
{
    LrgRenderMode mode = LRG_RENDER_MODE_2D;
    gboolean known = FALSE;

    if (str == NULL || *str == '\0' || g_ascii_strcasecmp (str, "2d") == 0)
    {
        mode = LRG_RENDER_MODE_2D;
        known = (str != NULL && g_ascii_strcasecmp (str, "2d") == 0);
    }
    else if (g_ascii_strcasecmp (str, "3d") == 0)
    {
        mode = LRG_RENDER_MODE_3D;
        known = TRUE;
    }
    else if (g_ascii_strcasecmp (str, "3dvr") == 0
             || g_ascii_strcasecmp (str, "vr") == 0)
    {
        mode = LRG_RENDER_MODE_3DVR;
        known = TRUE;
    }

    if (out_mode != NULL)
        *out_mode = mode;

    return known;
}
