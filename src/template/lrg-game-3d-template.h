/* lrg-game-3d-template.h - 3D game template with camera management
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for 3D games with camera and rendering support.
 *
 * This template extends LrgGameTemplate with 3D-specific features:
 * - Integrated 3D camera with multiple projection modes
 * - Quaternion-based camera orientation
 * - View frustum management
 * - Layered rendering (skybox, world, effects, UI)
 * - Mouse look and first-person/third-person camera support
 *
 * Subclass this template for 3D games like first-person shooters,
 * third-person adventures, racing games, flight simulators, etc.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-game-template.h"
#include "../graphics/lrg-camera3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_GAME_3D_TEMPLATE (lrg_game_3d_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgGame3DTemplate, lrg_game_3d_template,
                          LRG, GAME_3D_TEMPLATE, LrgGameTemplate)

/**
 * LrgGame3DTemplateClass:
 * @parent_class: parent class
 * @draw_skybox: renders the skybox/background (before world)
 * @draw_world: renders the 3D world (with camera transform)
 * @draw_effects: renders post-world effects (particles, etc.)
 * @draw_ui: renders the 2D UI overlay (screen space)
 * @update_camera: updates camera position/orientation each frame
 * @on_mouse_look: handles mouse movement for camera rotation
 *
 * Class structure for #LrgGame3DTemplate.
 *
 * Subclasses should override the draw_* methods to render their content.
 * The rendering order is: skybox -> world -> effects -> UI.
 */
struct _LrgGame3DTemplateClass
{
    LrgGameTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgGame3DTemplateClass::draw_skybox:
     * @self: a #LrgGame3DTemplate
     *
     * Renders the skybox or background.
     *
     * Called before world rendering. The camera is active but
     * depth testing may be disabled for skybox rendering.
     */
    void (*draw_skybox) (LrgGame3DTemplate *self);

    /**
     * LrgGame3DTemplateClass::draw_world:
     * @self: a #LrgGame3DTemplate
     *
     * Renders the 3D world.
     *
     * Called while the camera transform is active.
     * Use for terrain, models, characters, and all 3D content.
     */
    void (*draw_world) (LrgGame3DTemplate *self);

    /**
     * LrgGame3DTemplateClass::draw_effects:
     * @self: a #LrgGame3DTemplate
     *
     * Renders visual effects.
     *
     * Called after world rendering, still with camera active.
     * Use for particles, transparent objects, post-effects.
     */
    void (*draw_effects) (LrgGame3DTemplate *self);

    /**
     * LrgGame3DTemplateClass::draw_ui:
     * @self: a #LrgGame3DTemplate
     *
     * Renders the 2D UI overlay.
     *
     * Called after 3D rendering ends.
     * Use for HUD, menus, crosshairs, and UI elements.
     * Coordinates are in screen space.
     */
    void (*draw_ui) (LrgGame3DTemplate *self);

    /**
     * LrgGame3DTemplateClass::update_camera:
     * @self: a #LrgGame3DTemplate
     * @delta: time since last frame in seconds
     *
     * Updates the camera each frame.
     *
     * Override for custom camera behavior (orbit, follow, etc.).
     */
    void (*update_camera) (LrgGame3DTemplate *self,
                           gdouble            delta);

    /**
     * LrgGame3DTemplateClass::on_mouse_look:
     * @self: a #LrgGame3DTemplate
     * @delta_x: mouse X movement
     * @delta_y: mouse Y movement
     *
     * Handles mouse movement for camera rotation.
     *
     * Called when mouse look is enabled and the mouse moves.
     * Default implementation rotates camera using yaw/pitch.
     */
    void (*on_mouse_look) (LrgGame3DTemplate *self,
                           gfloat             delta_x,
                           gfloat             delta_y);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_game_3d_template_new:
 *
 * Creates a new 3D game template with default settings.
 *
 * Returns: (transfer full): a new #LrgGame3DTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgGame3DTemplate * lrg_game_3d_template_new (void);

/* ==========================================================================
 * Camera
 * ========================================================================== */

/**
 * lrg_game_3d_template_get_camera:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the 3D camera used for world rendering.
 *
 * The camera is created automatically on first access.
 *
 * Returns: (transfer none): the #LrgCamera3D
 */
LRG_AVAILABLE_IN_ALL
LrgCamera3D * lrg_game_3d_template_get_camera (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_camera:
 * @self: a #LrgGame3DTemplate
 * @camera: (transfer none) (nullable): the camera to use
 *
 * Sets a custom 3D camera for world rendering.
 *
 * Pass %NULL to reset to the default camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_camera (LrgGame3DTemplate *self,
                                      LrgCamera3D       *camera);

/* ==========================================================================
 * Camera Configuration
 * ========================================================================== */

/**
 * lrg_game_3d_template_get_fov:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the camera field of view (vertical, in degrees).
 *
 * Returns: the field of view
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_game_3d_template_get_fov (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_fov:
 * @self: a #LrgGame3DTemplate
 * @fov: field of view in degrees (typically 60-90)
 *
 * Sets the camera field of view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_fov (LrgGame3DTemplate *self,
                                   gfloat             fov);

/**
 * lrg_game_3d_template_get_near_clip:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the near clipping plane distance.
 *
 * Returns: the near clip distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_game_3d_template_get_near_clip (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_near_clip:
 * @self: a #LrgGame3DTemplate
 * @distance: near clip distance (must be > 0)
 *
 * Sets the near clipping plane distance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_near_clip (LrgGame3DTemplate *self,
                                         gfloat             distance);

/**
 * lrg_game_3d_template_get_far_clip:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the far clipping plane distance.
 *
 * Returns: the far clip distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_game_3d_template_get_far_clip (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_far_clip:
 * @self: a #LrgGame3DTemplate
 * @distance: far clip distance (must be > near clip)
 *
 * Sets the far clipping plane distance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_far_clip (LrgGame3DTemplate *self,
                                        gfloat             distance);

/* ==========================================================================
 * Mouse Look
 * ========================================================================== */

/**
 * lrg_game_3d_template_get_mouse_look_enabled:
 * @self: a #LrgGame3DTemplate
 *
 * Gets whether mouse look is enabled.
 *
 * When enabled, mouse movement rotates the camera.
 *
 * Returns: %TRUE if mouse look is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_game_3d_template_get_mouse_look_enabled (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_mouse_look_enabled:
 * @self: a #LrgGame3DTemplate
 * @enabled: whether to enable mouse look
 *
 * Enables or disables mouse look.
 *
 * When enabled, the cursor is hidden and locked to the window,
 * and mouse movement rotates the camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_mouse_look_enabled (LrgGame3DTemplate *self,
                                                  gboolean           enabled);

/**
 * lrg_game_3d_template_get_mouse_sensitivity:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the mouse look sensitivity.
 *
 * Returns: the sensitivity multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_game_3d_template_get_mouse_sensitivity (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_mouse_sensitivity:
 * @self: a #LrgGame3DTemplate
 * @sensitivity: sensitivity multiplier (default 0.1)
 *
 * Sets the mouse look sensitivity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_mouse_sensitivity (LrgGame3DTemplate *self,
                                                 gfloat             sensitivity);

/**
 * lrg_game_3d_template_get_invert_y:
 * @self: a #LrgGame3DTemplate
 *
 * Gets whether Y-axis mouse look is inverted.
 *
 * Returns: %TRUE if Y is inverted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_game_3d_template_get_invert_y (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_invert_y:
 * @self: a #LrgGame3DTemplate
 * @invert: whether to invert Y-axis
 *
 * Sets whether Y-axis mouse look is inverted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_invert_y (LrgGame3DTemplate *self,
                                        gboolean           invert);

/* ==========================================================================
 * Camera Pitch Limits
 * ========================================================================== */

/**
 * lrg_game_3d_template_set_pitch_limits:
 * @self: a #LrgGame3DTemplate
 * @min_pitch: minimum pitch in degrees (e.g., -89)
 * @max_pitch: maximum pitch in degrees (e.g., +89)
 *
 * Sets the camera pitch (vertical rotation) limits.
 *
 * This prevents the camera from flipping over.
 * Default is -89 to +89 degrees.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_pitch_limits (LrgGame3DTemplate *self,
                                            gfloat             min_pitch,
                                            gfloat             max_pitch);

/**
 * lrg_game_3d_template_get_pitch_limits:
 * @self: a #LrgGame3DTemplate
 * @min_pitch: (out) (optional): location for minimum pitch
 * @max_pitch: (out) (optional): location for maximum pitch
 *
 * Gets the camera pitch limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_get_pitch_limits (LrgGame3DTemplate *self,
                                            gfloat            *min_pitch,
                                            gfloat            *max_pitch);

/* ==========================================================================
 * Camera Orientation
 * ========================================================================== */

/**
 * lrg_game_3d_template_get_yaw:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the camera yaw (horizontal rotation) in degrees.
 *
 * Returns: the yaw angle
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_game_3d_template_get_yaw (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_yaw:
 * @self: a #LrgGame3DTemplate
 * @yaw: yaw angle in degrees
 *
 * Sets the camera yaw (horizontal rotation).
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_yaw (LrgGame3DTemplate *self,
                                   gfloat             yaw);

/**
 * lrg_game_3d_template_get_pitch:
 * @self: a #LrgGame3DTemplate
 *
 * Gets the camera pitch (vertical rotation) in degrees.
 *
 * Returns: the pitch angle
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_game_3d_template_get_pitch (LrgGame3DTemplate *self);

/**
 * lrg_game_3d_template_set_pitch:
 * @self: a #LrgGame3DTemplate
 * @pitch: pitch angle in degrees (clamped to pitch limits)
 *
 * Sets the camera pitch (vertical rotation).
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_set_pitch (LrgGame3DTemplate *self,
                                     gfloat             pitch);

/**
 * lrg_game_3d_template_look_at:
 * @self: a #LrgGame3DTemplate
 * @target_x: target X position
 * @target_y: target Y position
 * @target_z: target Z position
 *
 * Points the camera at a target position.
 *
 * This updates the yaw and pitch to look at the target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_look_at (LrgGame3DTemplate *self,
                                   gfloat             target_x,
                                   gfloat             target_y,
                                   gfloat             target_z);

/* ==========================================================================
 * First-Person Camera Movement
 * ========================================================================== */

/**
 * lrg_game_3d_template_move_forward:
 * @self: a #LrgGame3DTemplate
 * @distance: distance to move (negative = backward)
 *
 * Moves the camera forward relative to its facing direction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_move_forward (LrgGame3DTemplate *self,
                                        gfloat             distance);

/**
 * lrg_game_3d_template_move_right:
 * @self: a #LrgGame3DTemplate
 * @distance: distance to move (negative = left)
 *
 * Moves the camera right relative to its facing direction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_move_right (LrgGame3DTemplate *self,
                                      gfloat             distance);

/**
 * lrg_game_3d_template_move_up:
 * @self: a #LrgGame3DTemplate
 * @distance: distance to move (negative = down)
 *
 * Moves the camera up in world space.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_move_up (LrgGame3DTemplate *self,
                                   gfloat             distance);

/* ==========================================================================
 * Coordinate Transformation
 * ========================================================================== */

/**
 * lrg_game_3d_template_world_to_screen:
 * @self: a #LrgGame3DTemplate
 * @world_x: world X coordinate
 * @world_y: world Y coordinate
 * @world_z: world Z coordinate
 * @screen_x: (out): location for screen X coordinate
 * @screen_y: (out): location for screen Y coordinate
 *
 * Projects a 3D world position to 2D screen coordinates.
 *
 * Returns screen coordinates where (0,0) is top-left.
 * Returns negative coordinates if the point is behind the camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_world_to_screen (LrgGame3DTemplate *self,
                                           gfloat             world_x,
                                           gfloat             world_y,
                                           gfloat             world_z,
                                           gfloat            *screen_x,
                                           gfloat            *screen_y);

/**
 * lrg_game_3d_template_screen_to_ray:
 * @self: a #LrgGame3DTemplate
 * @screen_x: screen X coordinate
 * @screen_y: screen Y coordinate
 * @ray_origin_x: (out): location for ray origin X
 * @ray_origin_y: (out): location for ray origin Y
 * @ray_origin_z: (out): location for ray origin Z
 * @ray_direction_x: (out): location for ray direction X
 * @ray_direction_y: (out): location for ray direction Y
 * @ray_direction_z: (out): location for ray direction Z
 *
 * Creates a ray from screen coordinates for picking/raycasting.
 *
 * The ray starts at the camera position and points into the world
 * at the direction corresponding to the screen coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_3d_template_screen_to_ray (LrgGame3DTemplate *self,
                                         gfloat             screen_x,
                                         gfloat             screen_y,
                                         gfloat            *ray_origin_x,
                                         gfloat            *ray_origin_y,
                                         gfloat            *ray_origin_z,
                                         gfloat            *ray_direction_x,
                                         gfloat            *ray_direction_y,
                                         gfloat            *ray_direction_z);

G_END_DECLS
