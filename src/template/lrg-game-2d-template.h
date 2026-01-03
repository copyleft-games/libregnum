/* lrg-game-2d-template.h - 2D game template with virtual resolution scaling
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for 2D games with virtual resolution support.
 *
 * This template extends LrgGameTemplate with 2D-specific features:
 * - Virtual resolution with automatic scaling
 * - Multiple scaling modes (letterbox, pillarbox, stretch, crop, pixel-perfect)
 * - Integrated 2D camera with follow, deadzone, and smoothing
 * - Layered rendering (background, world, UI)
 * - Coordinate transformation between virtual and screen space
 *
 * Subclass this template for 2D games like platformers, top-down RPGs,
 * shoot-em-ups, puzzle games, etc.
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
#include "lrg-template-scalable.h"
#include "../graphics/lrg-camera2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_GAME_2D_TEMPLATE (lrg_game_2d_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgGame2DTemplate, lrg_game_2d_template,
                          LRG, GAME_2D_TEMPLATE, LrgGameTemplate)

/**
 * LrgGame2DTemplateClass:
 * @parent_class: parent class
 * @on_resolution_changed: called when window resolution changes
 * @draw_background: renders background layer (behind camera transform)
 * @draw_world: renders world layer (affected by camera transform)
 * @draw_ui: renders UI layer (screen-space, after camera transform)
 * @update_camera: updates camera position/state each frame
 *
 * Class structure for #LrgGame2DTemplate.
 *
 * Subclasses should override the draw_* methods to render their content.
 * The rendering order is: background -> world (with camera) -> UI.
 *
 * The #on_resolution_changed virtual is called whenever the window
 * is resized, allowing subclasses to adjust their rendering.
 */
struct _LrgGame2DTemplateClass
{
    LrgGameTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgGame2DTemplateClass::on_resolution_changed:
     * @self: a #LrgGame2DTemplate
     * @new_width: new window width in pixels
     * @new_height: new window height in pixels
     *
     * Called when the window resolution changes.
     *
     * Subclasses can override this to recalculate UI layouts,
     * adjust render targets, or perform other resolution-dependent
     * updates.
     */
    void (*on_resolution_changed) (LrgGame2DTemplate *self,
                                   gint               new_width,
                                   gint               new_height);

    /**
     * LrgGame2DTemplateClass::draw_background:
     * @self: a #LrgGame2DTemplate
     *
     * Renders the background layer.
     *
     * This is called before the camera transform is applied.
     * Use for static backgrounds, parallax layers, or anything
     * that should not be affected by camera movement.
     *
     * Coordinates are in virtual resolution space (0,0 to virtual_width, virtual_height).
     */
    void (*draw_background) (LrgGame2DTemplate *self);

    /**
     * LrgGame2DTemplateClass::draw_world:
     * @self: a #LrgGame2DTemplate
     *
     * Renders the world layer.
     *
     * This is called while the camera transform is active.
     * Use for game entities, tilemaps, particles, and anything
     * that should move with the camera.
     *
     * Coordinates are in world space.
     */
    void (*draw_world) (LrgGame2DTemplate *self);

    /**
     * LrgGame2DTemplateClass::draw_ui:
     * @self: a #LrgGame2DTemplate
     *
     * Renders the UI layer.
     *
     * This is called after the camera transform ends.
     * Use for HUD elements, menus, dialogs, and anything
     * that should remain fixed on screen.
     *
     * Coordinates are in virtual resolution space (0,0 to virtual_width, virtual_height).
     */
    void (*draw_ui) (LrgGame2DTemplate *self);

    /**
     * LrgGame2DTemplateClass::update_camera:
     * @self: a #LrgGame2DTemplate
     * @delta: time since last frame in seconds
     *
     * Updates the camera each frame.
     *
     * The default implementation handles camera follow with
     * deadzone and smoothing based on the template properties.
     * Override for custom camera behavior.
     */
    void (*update_camera) (LrgGame2DTemplate *self,
                           gdouble            delta);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_game_2d_template_new:
 *
 * Creates a new 2D game template with default settings.
 *
 * Returns: (transfer full): a new #LrgGame2DTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgGame2DTemplate * lrg_game_2d_template_new (void);

/* ==========================================================================
 * Virtual Resolution
 * ========================================================================== */

/**
 * lrg_game_2d_template_get_virtual_width:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the virtual (design) resolution width.
 *
 * Returns: the virtual width in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_game_2d_template_get_virtual_width (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_set_virtual_width:
 * @self: a #LrgGame2DTemplate
 * @width: the virtual width in pixels (minimum 1)
 *
 * Sets the virtual (design) resolution width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_virtual_width (LrgGame2DTemplate *self,
                                             gint               width);

/**
 * lrg_game_2d_template_get_virtual_height:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the virtual (design) resolution height.
 *
 * Returns: the virtual height in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_game_2d_template_get_virtual_height (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_set_virtual_height:
 * @self: a #LrgGame2DTemplate
 * @height: the virtual height in pixels (minimum 1)
 *
 * Sets the virtual (design) resolution height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_virtual_height (LrgGame2DTemplate *self,
                                              gint               height);

/**
 * lrg_game_2d_template_set_virtual_resolution:
 * @self: a #LrgGame2DTemplate
 * @width: the virtual width in pixels
 * @height: the virtual height in pixels
 *
 * Sets both virtual width and height at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_virtual_resolution (LrgGame2DTemplate *self,
                                                  gint               width,
                                                  gint               height);

/* ==========================================================================
 * Scaling Mode
 * ========================================================================== */

/**
 * lrg_game_2d_template_get_scaling_mode:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the current scaling mode.
 *
 * Returns: the #LrgScalingMode
 */
LRG_AVAILABLE_IN_ALL
LrgScalingMode lrg_game_2d_template_get_scaling_mode (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_set_scaling_mode:
 * @self: a #LrgGame2DTemplate
 * @mode: the scaling mode to use
 *
 * Sets the scaling mode for virtual resolution.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_scaling_mode (LrgGame2DTemplate *self,
                                            LrgScalingMode     mode);

/**
 * lrg_game_2d_template_get_pixel_perfect:
 * @self: a #LrgGame2DTemplate
 *
 * Gets whether pixel-perfect rendering is enabled.
 *
 * When enabled, the virtual resolution is scaled using integer
 * factors only, avoiding sub-pixel artifacts in pixel art.
 *
 * Returns: %TRUE if pixel-perfect is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_game_2d_template_get_pixel_perfect (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_set_pixel_perfect:
 * @self: a #LrgGame2DTemplate
 * @pixel_perfect: whether to enable pixel-perfect rendering
 *
 * Enables or disables pixel-perfect rendering.
 *
 * When enabled, this overrides the scaling mode to use
 * integer scaling factors only.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_pixel_perfect (LrgGame2DTemplate *self,
                                             gboolean           pixel_perfect);

/* ==========================================================================
 * Camera
 * ========================================================================== */

/**
 * lrg_game_2d_template_get_camera:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the 2D camera used for world rendering.
 *
 * The camera is created automatically on first access.
 *
 * Returns: (transfer none): the #LrgCamera2D
 */
LRG_AVAILABLE_IN_ALL
LrgCamera2D * lrg_game_2d_template_get_camera (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_set_camera:
 * @self: a #LrgGame2DTemplate
 * @camera: (transfer none) (nullable): the camera to use
 *
 * Sets a custom 2D camera for world rendering.
 *
 * Pass %NULL to reset to the default camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_camera (LrgGame2DTemplate *self,
                                      LrgCamera2D       *camera);

/* ==========================================================================
 * Camera Follow
 * ========================================================================== */

/**
 * lrg_game_2d_template_set_camera_target:
 * @self: a #LrgGame2DTemplate
 * @x: target world X coordinate
 * @y: target world Y coordinate
 *
 * Sets the position the camera should follow.
 *
 * If smoothing is enabled (default), the camera will smoothly
 * move toward this position. If deadzone is configured, the
 * camera won't move until the target exits the deadzone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_camera_target (LrgGame2DTemplate *self,
                                             gfloat             x,
                                             gfloat             y);

/**
 * lrg_game_2d_template_get_camera_smoothing:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the camera follow smoothing factor.
 *
 * Returns: smoothing factor (0.0 = instant, higher = slower)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_game_2d_template_get_camera_smoothing (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_set_camera_smoothing:
 * @self: a #LrgGame2DTemplate
 * @smoothing: smoothing factor (0.0 = instant, higher = slower)
 *
 * Sets the camera follow smoothing factor.
 *
 * A value of 0.0 means the camera instantly snaps to the target.
 * Higher values create smoother, slower camera movement.
 * Typical values are between 0.1 and 0.3.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_camera_smoothing (LrgGame2DTemplate *self,
                                                gfloat             smoothing);

/**
 * lrg_game_2d_template_set_camera_deadzone:
 * @self: a #LrgGame2DTemplate
 * @width: deadzone width in virtual pixels
 * @height: deadzone height in virtual pixels
 *
 * Sets the camera deadzone size.
 *
 * The camera won't move while the target is within the deadzone
 * (centered on screen). This prevents camera jitter from small
 * player movements.
 *
 * Set both to 0 to disable the deadzone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_camera_deadzone (LrgGame2DTemplate *self,
                                               gfloat             width,
                                               gfloat             height);

/**
 * lrg_game_2d_template_get_camera_deadzone:
 * @self: a #LrgGame2DTemplate
 * @width: (out) (optional): location for deadzone width
 * @height: (out) (optional): location for deadzone height
 *
 * Gets the camera deadzone size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_get_camera_deadzone (LrgGame2DTemplate *self,
                                               gfloat            *width,
                                               gfloat            *height);

/**
 * lrg_game_2d_template_set_camera_bounds:
 * @self: a #LrgGame2DTemplate
 * @min_x: minimum X coordinate the camera can show
 * @min_y: minimum Y coordinate the camera can show
 * @max_x: maximum X coordinate the camera can show
 * @max_y: maximum Y coordinate the camera can show
 *
 * Sets world bounds to constrain camera movement.
 *
 * The camera will not scroll to show areas outside these bounds.
 * Useful for preventing the camera from showing empty space
 * beyond the level edges.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_camera_bounds (LrgGame2DTemplate *self,
                                             gfloat             min_x,
                                             gfloat             min_y,
                                             gfloat             max_x,
                                             gfloat             max_y);

/**
 * lrg_game_2d_template_clear_camera_bounds:
 * @self: a #LrgGame2DTemplate
 *
 * Removes camera bounds, allowing unlimited scrolling.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_clear_camera_bounds (LrgGame2DTemplate *self);

/* ==========================================================================
 * Coordinate Transformation
 * ========================================================================== */

/**
 * lrg_game_2d_template_world_to_screen:
 * @self: a #LrgGame2DTemplate
 * @world_x: world X coordinate
 * @world_y: world Y coordinate
 * @screen_x: (out): location for screen X coordinate
 * @screen_y: (out): location for screen Y coordinate
 *
 * Transforms world coordinates to screen coordinates.
 *
 * This accounts for camera position, zoom, rotation, and the
 * virtual resolution scaling.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_world_to_screen (LrgGame2DTemplate *self,
                                           gfloat             world_x,
                                           gfloat             world_y,
                                           gfloat            *screen_x,
                                           gfloat            *screen_y);

/**
 * lrg_game_2d_template_screen_to_world:
 * @self: a #LrgGame2DTemplate
 * @screen_x: screen X coordinate
 * @screen_y: screen Y coordinate
 * @world_x: (out): location for world X coordinate
 * @world_y: (out): location for world Y coordinate
 *
 * Transforms screen coordinates to world coordinates.
 *
 * This accounts for camera position, zoom, rotation, and the
 * virtual resolution scaling. Use for converting mouse clicks
 * to world positions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_screen_to_world (LrgGame2DTemplate *self,
                                           gfloat             screen_x,
                                           gfloat             screen_y,
                                           gfloat            *world_x,
                                           gfloat            *world_y);

/**
 * lrg_game_2d_template_virtual_to_screen:
 * @self: a #LrgGame2DTemplate
 * @virtual_x: virtual resolution X coordinate
 * @virtual_y: virtual resolution Y coordinate
 * @screen_x: (out): location for screen X coordinate
 * @screen_y: (out): location for screen Y coordinate
 *
 * Transforms virtual resolution coordinates to screen coordinates.
 *
 * This does NOT apply camera transform - use for UI positioning.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_virtual_to_screen (LrgGame2DTemplate *self,
                                             gfloat             virtual_x,
                                             gfloat             virtual_y,
                                             gfloat            *screen_x,
                                             gfloat            *screen_y);

/**
 * lrg_game_2d_template_screen_to_virtual:
 * @self: a #LrgGame2DTemplate
 * @screen_x: screen X coordinate
 * @screen_y: screen Y coordinate
 * @virtual_x: (out): location for virtual resolution X coordinate
 * @virtual_y: (out): location for virtual resolution Y coordinate
 *
 * Transforms screen coordinates to virtual resolution coordinates.
 *
 * This does NOT apply camera transform - use for UI hit testing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_screen_to_virtual (LrgGame2DTemplate *self,
                                             gfloat             screen_x,
                                             gfloat             screen_y,
                                             gfloat            *virtual_x,
                                             gfloat            *virtual_y);

/* ==========================================================================
 * Render Target
 * ========================================================================== */

/**
 * lrg_game_2d_template_get_render_texture:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the render target texture used for virtual resolution.
 *
 * This is the texture that receives all game rendering before
 * being scaled to the window. Can be used for post-processing.
 *
 * Returns: (transfer none) (nullable): the render texture, or %NULL if not initialized
 */
LRG_AVAILABLE_IN_ALL
GrlRenderTexture * lrg_game_2d_template_get_render_texture (LrgGame2DTemplate *self);

/* ==========================================================================
 * Letterbox Color
 * ========================================================================== */

/**
 * lrg_game_2d_template_get_letterbox_color:
 * @self: a #LrgGame2DTemplate
 *
 * Gets the color used for letterbox/pillarbox bars.
 *
 * Returns: (transfer full): the letterbox color
 */
LRG_AVAILABLE_IN_ALL
GrlColor * lrg_game_2d_template_get_letterbox_color (LrgGame2DTemplate *self);

/**
 * lrg_game_2d_template_set_letterbox_color:
 * @self: a #LrgGame2DTemplate
 * @color: the color to use for bars
 *
 * Sets the color used for letterbox/pillarbox bars.
 *
 * Default is black.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_2d_template_set_letterbox_color (LrgGame2DTemplate *self,
                                               GrlColor          *color);

G_END_DECLS
