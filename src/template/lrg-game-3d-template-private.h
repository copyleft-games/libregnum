/* lrg-game-3d-template-private.h - Private data for 3D game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#include "lrg-game-3d-template.h"
#include "../graphics/lrg-camera3d.h"
#include <graylib.h>

G_BEGIN_DECLS

/* ==========================================================================
 * Private Data Structure
 * ========================================================================== */

/**
 * LrgGame3DTemplatePrivate:
 *
 * Private instance data for #LrgGame3DTemplate.
 */
typedef struct _LrgGame3DTemplatePrivate
{
    /* Camera */
    LrgCamera3D *camera;
    gboolean camera_owned;

    /* Field of view (vertical, degrees) */
    gfloat fov;

    /* Clipping planes */
    gfloat near_clip;
    gfloat far_clip;

    /* Mouse look */
    gboolean mouse_look_enabled;
    gfloat mouse_sensitivity;
    gboolean invert_y;

    /* Camera orientation (euler angles in degrees) */
    gfloat yaw;
    gfloat pitch;

    /* Pitch limits */
    gfloat min_pitch;
    gfloat max_pitch;

    /* Camera position (updated from orientation) */
    gfloat position_x;
    gfloat position_y;
    gfloat position_z;

} LrgGame3DTemplatePrivate;

/* ==========================================================================
 * Private Functions (for subclass use)
 * ========================================================================== */

/**
 * lrg_game_3d_template_get_private:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the private data for the template.
 *
 * For use by subclasses only.
 *
 * Returns: (transfer none): the private data
 */
LrgGame3DTemplatePrivate *
lrg_game_3d_template_get_private (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_update_camera_orientation:
 * @self: a #LrgGame3DTemplate
 *
 * Updates the camera's target based on yaw/pitch.
 *
 * Called automatically when yaw or pitch changes.
 */
void
lrg_game_3d_template_update_camera_orientation (LrgGame3DTemplate *self);

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

/* Default field of view (degrees) */
#define LRG_DEFAULT_3D_FOV           60.0f

/* Default clipping planes */
#define LRG_DEFAULT_NEAR_CLIP        0.1f
#define LRG_DEFAULT_FAR_CLIP         1000.0f

/* Default mouse sensitivity */
#define LRG_DEFAULT_MOUSE_SENSITIVITY 0.1f

/* Default pitch limits (degrees) */
#define LRG_DEFAULT_MIN_PITCH        -89.0f
#define LRG_DEFAULT_MAX_PITCH         89.0f

/* Default camera position */
#define LRG_DEFAULT_CAMERA_Y         2.0f
#define LRG_DEFAULT_CAMERA_Z         10.0f

G_END_DECLS
