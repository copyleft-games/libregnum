/* lrg-photo-overlay.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-photo-overlay.h"

/**
 * SECTION:lrg-photo-overlay
 * @Title: Photo overlays
 * @Short_description: Frames and composition grids for photo mode
 *
 * Frames are part of the photo: lrg_photo_overlay_apply_frame() paints
 * them into the captured image, lrg_photo_overlay_draw_frame() shows
 * the same borders in the preview.  Grids only guide the eye
 * (lrg_photo_overlay_draw_grid()) and are never in a photo.
 */

#define CINEMA_ASPECT (2.39f)

const gchar *
lrg_photo_frame_get_label (LrgPhotoFrame frame)
{
    switch (frame)
    {
    case LRG_PHOTO_FRAME_THIN:
        return "Thin border";
    case LRG_PHOTO_FRAME_WIDE:
        return "Wide border";
    case LRG_PHOTO_FRAME_POLAROID:
        return "Polaroid";
    case LRG_PHOTO_FRAME_CINEMA:
        return "Cinema bars";
    case LRG_PHOTO_FRAME_NONE:
    default:
        return "None";
    }
}

const gchar *
lrg_photo_grid_get_label (LrgPhotoGrid grid)
{
    switch (grid)
    {
    case LRG_PHOTO_GRID_THIRDS:
        return "Rule of thirds";
    case LRG_PHOTO_GRID_GOLDEN:
        return "Golden ratio";
    case LRG_PHOTO_GRID_CENTER:
        return "Centre cross";
    case LRG_PHOTO_GRID_DIAGONALS:
        return "Diagonals";
    case LRG_PHOTO_GRID_NONE:
    default:
        return "None";
    }
}

void
lrg_photo_overlay_get_frame_borders (LrgPhotoFrame frame,
                                     gint          width,
                                     gint          height,
                                     gint         *left,
                                     gint         *top,
                                     gint         *right,
                                     gint         *bottom)
{
    gint l = 0;
    gint t = 0;
    gint r = 0;
    gint b = 0;
    gint shorter;

    shorter = MIN (width, height);
    switch (frame)
    {
    case LRG_PHOTO_FRAME_THIN:
        l = t = r = b = (gint)floorf ((gfloat)shorter * 0.012f + 0.5f);
        break;
    case LRG_PHOTO_FRAME_WIDE:
        l = t = r = b = (gint)floorf ((gfloat)shorter * 0.05f + 0.5f);
        break;
    case LRG_PHOTO_FRAME_POLAROID:
        l = t = r = (gint)floorf ((gfloat)shorter * 0.05f + 0.5f);
        b = (gint)floorf ((gfloat)shorter * 0.16f + 0.5f);
        break;
    case LRG_PHOTO_FRAME_CINEMA:
        if ((gfloat)width / (gfloat)MAX (height, 1) < CINEMA_ASPECT)
        {
            /* narrower than 2.39:1: bars above and below */
            t = b = (height - (gint)floorf ((gfloat)width / CINEMA_ASPECT + 0.5f)) / 2;
        }
        else
        {
            /* wider: bars at the sides */
            l = r = (width - (gint)floorf ((gfloat)height * CINEMA_ASPECT + 0.5f)) / 2;
        }
        break;
    case LRG_PHOTO_FRAME_NONE:
    default:
        break;
    }
    /* never more than the picture */
    l = CLAMP (l, 0, MAX (width / 2 - 1, 0));
    r = CLAMP (r, 0, MAX (width / 2 - 1, 0));
    t = CLAMP (t, 0, MAX (height / 2 - 1, 0));
    b = CLAMP (b, 0, MAX (height / 2 - 1, 0));
    if (left != NULL)
        *left = l;
    if (top != NULL)
        *top = t;
    if (right != NULL)
        *right = r;
    if (bottom != NULL)
        *bottom = b;
}

void
lrg_photo_overlay_get_picture_rect (LrgPhotoFrame  frame,
                                    gint           width,
                                    gint           height,
                                    GrlRectangle  *out)
{
    gint l;
    gint t;
    gint r;
    gint b;

    g_return_if_fail (out != NULL);

    lrg_photo_overlay_get_frame_borders (frame, width, height, &l, &t, &r, &b);
    out->x = (gfloat)l;
    out->y = (gfloat)t;
    out->width = (gfloat)(width - l - r);
    out->height = (gfloat)(height - t - b);
}

guint
lrg_photo_overlay_get_grid_lines (LrgPhotoGrid  grid,
                                  gfloat        x,
                                  gfloat        y,
                                  gfloat        width,
                                  gfloat        height,
                                  gfloat       *out_lines,
                                  guint         max_lines)
{
    gfloat f1;
    gfloat f2;
    guint n = 0;

    g_return_val_if_fail (out_lines != NULL, 0);
    g_return_val_if_fail (max_lines >= LRG_PHOTO_OVERLAY_MAX_GRID_LINES, 0);

#define LINE(ax, ay, bx, by) \
    do { \
        out_lines[n * 4 + 0] = (ax); \
        out_lines[n * 4 + 1] = (ay); \
        out_lines[n * 4 + 2] = (bx); \
        out_lines[n * 4 + 3] = (by); \
        n++; \
    } while (0)

    switch (grid)
    {
    case LRG_PHOTO_GRID_THIRDS:
    case LRG_PHOTO_GRID_GOLDEN:
        f1 = grid == LRG_PHOTO_GRID_THIRDS ? 1.0f / 3.0f : 0.381966f;
        f2 = 1.0f - f1;
        LINE (x + width * f1, y, x + width * f1, y + height);
        LINE (x + width * f2, y, x + width * f2, y + height);
        LINE (x, y + height * f1, x + width, y + height * f1);
        LINE (x, y + height * f2, x + width, y + height * f2);
        break;
    case LRG_PHOTO_GRID_DIAGONALS:
        LINE (x, y, x + width, y + height);
        LINE (x + width, y, x, y + height);
        /* and the cross */
        LINE (x + width * 0.5f, y, x + width * 0.5f, y + height);
        LINE (x, y + height * 0.5f, x + width, y + height * 0.5f);
        break;
    case LRG_PHOTO_GRID_CENTER:
        LINE (x + width * 0.5f, y, x + width * 0.5f, y + height);
        LINE (x, y + height * 0.5f, x + width, y + height * 0.5f);
        break;
    case LRG_PHOTO_GRID_NONE:
    default:
        break;
    }
#undef LINE
    return n;
}

void
lrg_photo_overlay_draw_grid (LrgPhotoGrid       grid,
                             gint               width,
                             gint               height,
                             const GrlRectangle *area)
{
    g_autoptr(GrlColor) colour = NULL;
    gfloat lines[LRG_PHOTO_OVERLAY_MAX_GRID_LINES * 4];
    guint n;
    guint i;

    if (grid == LRG_PHOTO_GRID_NONE)
        return;

    colour = grl_color_new (255, 255, 255, 110);
    if (area != NULL)
        n = lrg_photo_overlay_get_grid_lines (grid, area->x, area->y, area->width, area->height,
                                              lines, LRG_PHOTO_OVERLAY_MAX_GRID_LINES);
    else
        n = lrg_photo_overlay_get_grid_lines (grid, 0.0f, 0.0f, (gfloat)width, (gfloat)height,
                                              lines, LRG_PHOTO_OVERLAY_MAX_GRID_LINES);
    for (i = 0; i < n; i++)
        grl_draw_line ((gint)lines[i * 4 + 0], (gint)lines[i * 4 + 1],
                       (gint)lines[i * 4 + 2], (gint)lines[i * 4 + 3], colour);
}

void
lrg_photo_overlay_draw_frame (LrgPhotoFrame frame,
                              gint          width,
                              gint          height,
                              guint8        red,
                              guint8        green,
                              guint8        blue)
{
    g_autoptr(GrlColor) colour = NULL;
    gint l;
    gint t;
    gint r;
    gint b;

    if (frame == LRG_PHOTO_FRAME_NONE)
        return;

    lrg_photo_overlay_get_frame_borders (frame, width, height, &l, &t, &r, &b);
    colour = grl_color_new (red, green, blue, 255);
    if (t > 0)
        grl_draw_rectangle (0, 0, width, t, colour);
    if (b > 0)
        grl_draw_rectangle (0, height - b, width, b, colour);
    if (l > 0)
        grl_draw_rectangle (0, t, l, height - t - b, colour);
    if (r > 0)
        grl_draw_rectangle (width - r, t, r, height - t - b, colour);
}

void
lrg_photo_overlay_apply_frame (LrgPhotoFrame frame,
                               GrlImage     *image,
                               guint8        red,
                               guint8        green,
                               guint8        blue)
{
    guint8 *pixels;
    gsize size;
    gint width;
    gint height;
    gint l;
    gint t;
    gint r;
    gint b;
    gint x;
    gint y;

    g_return_if_fail (GRL_IS_IMAGE (image));

    if (frame == LRG_PHOTO_FRAME_NONE)
        return;
    width = grl_image_get_width (image);
    height = grl_image_get_height (image);
    pixels = grl_image_get_pixels (image, &size);
    if (width <= 0 || height <= 0 || pixels == NULL ||
        size < (gsize)width * (gsize)height * 4)
        return;

    /* every pixel outside the open rectangle */
    lrg_photo_overlay_get_frame_borders (frame, width, height, &l, &t, &r, &b);
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            guint8 *px;

            if (x >= l && x < width - r && y >= t && y < height - b)
            {
                /* skip the open middle of the row */
                x = width - r - 1;
                continue;
            }
            px = pixels + ((gsize)y * (gsize)width + (gsize)x) * 4;
            px[0] = red;
            px[1] = green;
            px[2] = blue;
            px[3] = 255;
        }
    }
}
