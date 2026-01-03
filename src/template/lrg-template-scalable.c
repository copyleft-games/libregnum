/* lrg-template-scalable.c - Resolution scaling interface implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-template-scalable.h"
#include "lrg-game-template.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <math.h>

G_DEFINE_INTERFACE (LrgTemplateScalable, lrg_template_scalable, G_TYPE_OBJECT)

/* ========================================================================== */
/* Interface Default Implementations                                          */
/* ========================================================================== */

static gint
lrg_template_scalable_default_get_virtual_width (LrgTemplateScalable *self)
{
    /* Default to 1920 (1080p width) */
    return 1920;
}

static gint
lrg_template_scalable_default_get_virtual_height (LrgTemplateScalable *self)
{
    /* Default to 1080 (1080p height) */
    return 1080;
}

static LrgScalingMode
lrg_template_scalable_default_get_scaling_mode (LrgTemplateScalable *self)
{
    return LRG_SCALING_MODE_LETTERBOX;
}

static void
lrg_template_scalable_default_world_to_screen (LrgTemplateScalable *self,
                                               gfloat               world_x,
                                               gfloat               world_y,
                                               gfloat              *screen_x,
                                               gfloat              *screen_y)
{
    /* Default: identity transform */
    if (screen_x != NULL)
        *screen_x = world_x;
    if (screen_y != NULL)
        *screen_y = world_y;
}

static void
lrg_template_scalable_default_screen_to_world (LrgTemplateScalable *self,
                                               gfloat               screen_x,
                                               gfloat               screen_y,
                                               gfloat              *world_x,
                                               gfloat              *world_y)
{
    /* Default: identity transform */
    if (world_x != NULL)
        *world_x = screen_x;
    if (world_y != NULL)
        *world_y = screen_y;
}

/* ========================================================================== */
/* Interface Initialization                                                   */
/* ========================================================================== */

static void
lrg_template_scalable_default_init (LrgTemplateScalableInterface *iface)
{
    iface->get_virtual_width = lrg_template_scalable_default_get_virtual_width;
    iface->get_virtual_height = lrg_template_scalable_default_get_virtual_height;
    iface->get_scaling_mode = lrg_template_scalable_default_get_scaling_mode;
    iface->world_to_screen = lrg_template_scalable_default_world_to_screen;
    iface->screen_to_world = lrg_template_scalable_default_screen_to_world;
}

/* ========================================================================== */
/* Public Interface Methods                                                   */
/* ========================================================================== */

/**
 * lrg_template_scalable_get_virtual_width:
 * @self: a #LrgTemplateScalable
 *
 * Gets the virtual (game) resolution width.
 *
 * Returns: the virtual width in pixels
 */
gint
lrg_template_scalable_get_virtual_width (LrgTemplateScalable *self)
{
    LrgTemplateScalableInterface *iface;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SCALABLE (self), 1920);

    iface = LRG_TEMPLATE_SCALABLE_GET_IFACE (self);
    g_return_val_if_fail (iface->get_virtual_width != NULL, 1920);

    return iface->get_virtual_width (self);
}

/**
 * lrg_template_scalable_get_virtual_height:
 * @self: a #LrgTemplateScalable
 *
 * Gets the virtual (game) resolution height.
 *
 * Returns: the virtual height in pixels
 */
gint
lrg_template_scalable_get_virtual_height (LrgTemplateScalable *self)
{
    LrgTemplateScalableInterface *iface;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SCALABLE (self), 1080);

    iface = LRG_TEMPLATE_SCALABLE_GET_IFACE (self);
    g_return_val_if_fail (iface->get_virtual_height != NULL, 1080);

    return iface->get_virtual_height (self);
}

/**
 * lrg_template_scalable_get_scaling_mode:
 * @self: a #LrgTemplateScalable
 *
 * Gets the current scaling mode.
 *
 * Returns: the current #LrgScalingMode
 */
LrgScalingMode
lrg_template_scalable_get_scaling_mode (LrgTemplateScalable *self)
{
    LrgTemplateScalableInterface *iface;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SCALABLE (self),
                          LRG_SCALING_MODE_LETTERBOX);

    iface = LRG_TEMPLATE_SCALABLE_GET_IFACE (self);
    g_return_val_if_fail (iface->get_scaling_mode != NULL,
                          LRG_SCALING_MODE_LETTERBOX);

    return iface->get_scaling_mode (self);
}

/**
 * lrg_template_scalable_world_to_screen:
 * @self: a #LrgTemplateScalable
 * @world_x: world X coordinate
 * @world_y: world Y coordinate
 * @screen_x: (out): location for screen X coordinate
 * @screen_y: (out): location for screen Y coordinate
 *
 * Transforms world (virtual) coordinates to screen coordinates.
 */
void
lrg_template_scalable_world_to_screen (LrgTemplateScalable *self,
                                       gfloat               world_x,
                                       gfloat               world_y,
                                       gfloat              *screen_x,
                                       gfloat              *screen_y)
{
    LrgTemplateScalableInterface *iface;

    g_return_if_fail (LRG_IS_TEMPLATE_SCALABLE (self));

    iface = LRG_TEMPLATE_SCALABLE_GET_IFACE (self);
    g_return_if_fail (iface->world_to_screen != NULL);

    iface->world_to_screen (self, world_x, world_y, screen_x, screen_y);
}

/**
 * lrg_template_scalable_screen_to_world:
 * @self: a #LrgTemplateScalable
 * @screen_x: screen X coordinate
 * @screen_y: screen Y coordinate
 * @world_x: (out): location for world X coordinate
 * @world_y: (out): location for world Y coordinate
 *
 * Transforms screen coordinates to world (virtual) coordinates.
 */
void
lrg_template_scalable_screen_to_world (LrgTemplateScalable *self,
                                       gfloat               screen_x,
                                       gfloat               screen_y,
                                       gfloat              *world_x,
                                       gfloat              *world_y)
{
    LrgTemplateScalableInterface *iface;

    g_return_if_fail (LRG_IS_TEMPLATE_SCALABLE (self));

    iface = LRG_TEMPLATE_SCALABLE_GET_IFACE (self);
    g_return_if_fail (iface->screen_to_world != NULL);

    iface->screen_to_world (self, screen_x, screen_y, world_x, world_y);
}

/* ========================================================================== */
/* Utility Functions                                                          */
/* ========================================================================== */

/*
 * Helper to calculate scale factors based on scaling mode.
 * This is used by multiple utility functions.
 */
static void
calculate_scaling_factors (LrgTemplateScalable *self,
                           gint                 window_width,
                           gint                 window_height,
                           gfloat              *scale_x,
                           gfloat              *scale_y,
                           gfloat              *offset_x,
                           gfloat              *offset_y)
{
    LrgScalingMode mode;
    gint virtual_width;
    gint virtual_height;
    gfloat sx;
    gfloat sy;
    gfloat ox;
    gfloat oy;

    virtual_width = lrg_template_scalable_get_virtual_width (self);
    virtual_height = lrg_template_scalable_get_virtual_height (self);
    mode = lrg_template_scalable_get_scaling_mode (self);

    /* Calculate base scale factors */
    sx = (gfloat) window_width / (gfloat) virtual_width;
    sy = (gfloat) window_height / (gfloat) virtual_height;
    ox = 0.0f;
    oy = 0.0f;

    switch (mode)
    {
        case LRG_SCALING_MODE_STRETCH:
            /* Non-uniform scaling - use separate X/Y scales */
            break;

        case LRG_SCALING_MODE_LETTERBOX:
            {
                /* Fit width, add bars top/bottom if needed */
                gfloat uniform_scale;

                uniform_scale = fminf (sx, sy);
                sx = sy = uniform_scale;
                ox = (window_width - virtual_width * uniform_scale) / 2.0f;
                oy = (window_height - virtual_height * uniform_scale) / 2.0f;
            }
            break;

        case LRG_SCALING_MODE_PILLARBOX:
            {
                /* Fit height, add bars left/right if needed */
                gfloat uniform_scale;

                uniform_scale = fminf (sx, sy);
                sx = sy = uniform_scale;
                ox = (window_width - virtual_width * uniform_scale) / 2.0f;
                oy = (window_height - virtual_height * uniform_scale) / 2.0f;
            }
            break;

        case LRG_SCALING_MODE_CROP:
            {
                /* Fill window, crop edges */
                gfloat uniform_scale;

                uniform_scale = fmaxf (sx, sy);
                sx = sy = uniform_scale;
                ox = (window_width - virtual_width * uniform_scale) / 2.0f;
                oy = (window_height - virtual_height * uniform_scale) / 2.0f;
            }
            break;

        case LRG_SCALING_MODE_PIXEL_PERFECT:
            {
                /* Integer scaling only */
                gint int_scale;

                int_scale = (gint) fminf (sx, sy);
                if (int_scale < 1)
                    int_scale = 1;

                sx = sy = (gfloat) int_scale;
                ox = (window_width - virtual_width * int_scale) / 2.0f;
                oy = (window_height - virtual_height * int_scale) / 2.0f;
            }
            break;
    }

    if (scale_x != NULL)
        *scale_x = sx;
    if (scale_y != NULL)
        *scale_y = sy;
    if (offset_x != NULL)
        *offset_x = ox;
    if (offset_y != NULL)
        *offset_y = oy;
}

/*
 * Helper to get window dimensions from a template.
 * Uses LrgGameTemplate's window size function if applicable.
 */
static gboolean
get_window_dimensions (LrgTemplateScalable *self,
                       gint                *width,
                       gint                *height)
{
    gint w;
    gint h;

    w = 0;
    h = 0;

    /* Try to get window size from game template if applicable */
    if (LRG_IS_GAME_TEMPLATE (self))
    {
        lrg_game_template_get_window_size (LRG_GAME_TEMPLATE (self),
                                           &w, &h);
    }

    /*
     * If not a game template or window size is zero, use default.
     * In practice, LrgTemplateScalable is always implemented by
     * LrgGame2DTemplate which IS a LrgGameTemplate.
     */
    if (w == 0 || h == 0)
    {
        w = lrg_template_scalable_get_virtual_width (self);
        h = lrg_template_scalable_get_virtual_height (self);
    }

    if (width != NULL)
        *width = w;
    if (height != NULL)
        *height = h;

    return TRUE;
}

/**
 * lrg_template_scalable_get_scale_factor:
 * @self: a #LrgTemplateScalable
 *
 * Gets the current scale factor between virtual and screen resolution.
 *
 * For non-uniform scaling modes (stretch), this returns the average
 * of X and Y scale factors. For pixel-perfect mode, this returns
 * an integer scale factor.
 *
 * Returns: the scale factor (>= 1.0 means magnified)
 */
gfloat
lrg_template_scalable_get_scale_factor (LrgTemplateScalable *self)
{
    gint window_width;
    gint window_height;
    gfloat scale_x;
    gfloat scale_y;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SCALABLE (self), 1.0f);

    if (!get_window_dimensions (self, &window_width, &window_height))
        return 1.0f;

    calculate_scaling_factors (self, window_width, window_height,
                               &scale_x, &scale_y, NULL, NULL);

    /* Return average for non-uniform modes */
    return (scale_x + scale_y) / 2.0f;
}

/**
 * lrg_template_scalable_get_viewport_offset:
 * @self: a #LrgTemplateScalable
 * @offset_x: (out) (optional): location for X offset
 * @offset_y: (out) (optional): location for Y offset
 *
 * Gets the viewport offset for letterbox/pillarbox bars.
 */
void
lrg_template_scalable_get_viewport_offset (LrgTemplateScalable *self,
                                           gfloat              *offset_x,
                                           gfloat              *offset_y)
{
    gint window_width;
    gint window_height;

    g_return_if_fail (LRG_IS_TEMPLATE_SCALABLE (self));

    if (!get_window_dimensions (self, &window_width, &window_height))
    {
        if (offset_x != NULL)
            *offset_x = 0.0f;
        if (offset_y != NULL)
            *offset_y = 0.0f;
        return;
    }

    calculate_scaling_factors (self, window_width, window_height,
                               NULL, NULL, offset_x, offset_y);
}

/**
 * lrg_template_scalable_get_viewport_size:
 * @self: a #LrgTemplateScalable
 * @width: (out) (optional): location for viewport width
 * @height: (out) (optional): location for viewport height
 *
 * Gets the actual rendered viewport size.
 */
void
lrg_template_scalable_get_viewport_size (LrgTemplateScalable *self,
                                         gfloat              *width,
                                         gfloat              *height)
{
    gint window_width;
    gint window_height;
    gint virtual_width;
    gint virtual_height;
    gfloat scale_x;
    gfloat scale_y;

    g_return_if_fail (LRG_IS_TEMPLATE_SCALABLE (self));

    if (!get_window_dimensions (self, &window_width, &window_height))
    {
        if (width != NULL)
            *width = 0.0f;
        if (height != NULL)
            *height = 0.0f;
        return;
    }

    virtual_width = lrg_template_scalable_get_virtual_width (self);
    virtual_height = lrg_template_scalable_get_virtual_height (self);

    calculate_scaling_factors (self, window_width, window_height,
                               &scale_x, &scale_y, NULL, NULL);

    if (width != NULL)
        *width = virtual_width * scale_x;
    if (height != NULL)
        *height = virtual_height * scale_y;
}

/**
 * lrg_template_scalable_is_point_in_viewport:
 * @self: a #LrgTemplateScalable
 * @screen_x: screen X coordinate
 * @screen_y: screen Y coordinate
 *
 * Checks if a screen point is within the rendered viewport.
 *
 * Returns: %TRUE if the point is in the viewport
 */
gboolean
lrg_template_scalable_is_point_in_viewport (LrgTemplateScalable *self,
                                            gfloat               screen_x,
                                            gfloat               screen_y)
{
    gint window_width;
    gint window_height;
    gfloat offset_x;
    gfloat offset_y;
    gfloat viewport_width;
    gfloat viewport_height;

    g_return_val_if_fail (LRG_IS_TEMPLATE_SCALABLE (self), FALSE);

    if (!get_window_dimensions (self, &window_width, &window_height))
        return FALSE;

    lrg_template_scalable_get_viewport_offset (self, &offset_x, &offset_y);
    lrg_template_scalable_get_viewport_size (self, &viewport_width, &viewport_height);

    return (screen_x >= offset_x &&
            screen_x < offset_x + viewport_width &&
            screen_y >= offset_y &&
            screen_y < offset_y + viewport_height);
}
