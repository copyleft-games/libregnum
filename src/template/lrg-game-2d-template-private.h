/* lrg-game-2d-template-private.h - Private data for 2D game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#include "lrg-game-2d-template.h"
#include "../graphics/lrg-camera2d.h"
#include <graylib.h>

G_BEGIN_DECLS

/* ==========================================================================
 * Private Data Structure
 * ========================================================================== */

/**
 * LrgGame2DTemplatePrivate:
 *
 * Private instance data for #LrgGame2DTemplate.
 */
typedef struct _LrgGame2DTemplatePrivate
{
    /* Virtual resolution */
    gint virtual_width;
    gint virtual_height;
    LrgScalingMode scaling_mode;
    gboolean pixel_perfect;

    /* Render target for virtual resolution */
    GrlRenderTexture *render_target;
    gboolean render_target_valid;

    /* Letterbox/pillarbox bar color */
    GrlColor *letterbox_color;

    /* Game background color (used inside render target) */
    GrlColor *background_color;

    /* Calculated scaling values (updated on resize) */
    gfloat scale_x;
    gfloat scale_y;
    gfloat offset_x;
    gfloat offset_y;
    gfloat viewport_width;
    gfloat viewport_height;

    /* Last window size (for detecting resizes) */
    gint last_window_width;
    gint last_window_height;

    /* Pending resize tracking (for async window managers) */
    gboolean resize_pending;
    gint     requested_window_width;
    gint     requested_window_height;

    /* Camera */
    LrgCamera2D *camera;
    gboolean camera_owned;   /* TRUE if we created it */

    /* Camera follow settings */
    gfloat camera_target_x;
    gfloat camera_target_y;
    gfloat camera_smoothing;

    /* Camera deadzone */
    gfloat deadzone_width;
    gfloat deadzone_height;

    /* Camera bounds */
    gboolean has_camera_bounds;
    gfloat bounds_min_x;
    gfloat bounds_min_y;
    gfloat bounds_max_x;
    gfloat bounds_max_y;

} LrgGame2DTemplatePrivate;

/* ==========================================================================
 * Private Functions (for subclass use)
 * ========================================================================== */

/**
 * lrg_game_2d_template_get_private:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the private data for the template.
 *
 * For use by subclasses only.
 *
 * Returns: (transfer none): the private data
 */
LrgGame2DTemplatePrivate *
lrg_game_2d_template_get_private (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_update_scaling:
 * @self: a #LrgGame2DTemplate
 * @window_width: current window width
 * @window_height: current window height
 *
 * Recalculates scaling factors based on current window size.
 *
 * Called automatically when the window is resized.
 */
void
lrg_game_2d_template_update_scaling (LrgGame2DTemplate *self,
                                     gint               window_width,
                                     gint               window_height);

/**
 * lrg_game_2d_template_ensure_render_target:
 * @self: a #LrgGame2DTemplate
 *
 * Creates or recreates the render target if needed.
 *
 * Call this after changing virtual resolution.
 */
void
lrg_game_2d_template_ensure_render_target (LrgGame2DTemplate *self);

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

/* Default virtual resolution (1080p) */
#define LRG_DEFAULT_VIRTUAL_WIDTH    1920
#define LRG_DEFAULT_VIRTUAL_HEIGHT   1080

/* Default camera smoothing (0 = instant) */
#define LRG_2D_TEMPLATE_DEFAULT_CAMERA_SMOOTHING 0.1f

G_END_DECLS
