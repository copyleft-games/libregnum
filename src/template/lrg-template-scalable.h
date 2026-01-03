/* lrg-template-scalable.h - Resolution scaling interface
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for templates that support virtual resolution scaling.
 *
 * This interface provides coordinate transformation between virtual
 * (game world) coordinates and screen (physical) coordinates. It
 * supports multiple scaling modes for different display scenarios.
 *
 * Templates implementing this interface can render to a virtual
 * resolution and have it automatically scaled to fit the window
 * using the configured scaling mode.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_SCALABLE (lrg_template_scalable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgTemplateScalable, lrg_template_scalable,
                     LRG, TEMPLATE_SCALABLE, GObject)

/**
 * LrgTemplateScalableInterface:
 * @parent_iface: parent interface
 * @get_virtual_width: returns the virtual (game) resolution width
 * @get_virtual_height: returns the virtual (game) resolution height
 * @get_scaling_mode: returns the current scaling mode
 * @world_to_screen: transforms world coordinates to screen coordinates
 * @screen_to_world: transforms screen coordinates to world coordinates
 *
 * Interface structure for #LrgTemplateScalable.
 *
 * Implementors should provide all methods. The coordinate transformation
 * methods must account for the current scaling mode, viewport offset,
 * and any camera transformations applied.
 */
struct _LrgTemplateScalableInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgTemplateScalableInterface::get_virtual_width:
     * @self: a #LrgTemplateScalable
     *
     * Gets the virtual (game) resolution width.
     *
     * This is the logical width that the game renders to, regardless
     * of the actual window size. The virtual resolution is scaled
     * to fit the window according to the scaling mode.
     *
     * Returns: the virtual width in pixels
     */
    gint (*get_virtual_width) (LrgTemplateScalable *self);

    /**
     * LrgTemplateScalableInterface::get_virtual_height:
     * @self: a #LrgTemplateScalable
     *
     * Gets the virtual (game) resolution height.
     *
     * This is the logical height that the game renders to, regardless
     * of the actual window size. The virtual resolution is scaled
     * to fit the window according to the scaling mode.
     *
     * Returns: the virtual height in pixels
     */
    gint (*get_virtual_height) (LrgTemplateScalable *self);

    /**
     * LrgTemplateScalableInterface::get_scaling_mode:
     * @self: a #LrgTemplateScalable
     *
     * Gets the current scaling mode.
     *
     * The scaling mode determines how the virtual resolution is
     * mapped to the actual window size:
     * - %LRG_SCALING_MODE_STRETCH: Fill window, may distort aspect ratio
     * - %LRG_SCALING_MODE_LETTERBOX: Fit width, add bars top/bottom
     * - %LRG_SCALING_MODE_PILLARBOX: Fit height, add bars left/right
     * - %LRG_SCALING_MODE_CROP: Fill window, may crop edges
     * - %LRG_SCALING_MODE_PIXEL_PERFECT: Integer scaling only
     *
     * Returns: the current #LrgScalingMode
     */
    LrgScalingMode (*get_scaling_mode) (LrgTemplateScalable *self);

    /**
     * LrgTemplateScalableInterface::world_to_screen:
     * @self: a #LrgTemplateScalable
     * @world_x: world X coordinate
     * @world_y: world Y coordinate
     * @screen_x: (out): location for screen X coordinate
     * @screen_y: (out): location for screen Y coordinate
     *
     * Transforms world (virtual) coordinates to screen coordinates.
     *
     * This accounts for the virtual resolution, scaling mode, viewport
     * offset (letterbox/pillarbox bars), and any active camera transform.
     *
     * Use this to position UI elements or determine where world objects
     * appear on screen.
     */
    void (*world_to_screen) (LrgTemplateScalable *self,
                             gfloat               world_x,
                             gfloat               world_y,
                             gfloat              *screen_x,
                             gfloat              *screen_y);

    /**
     * LrgTemplateScalableInterface::screen_to_world:
     * @self: a #LrgTemplateScalable
     * @screen_x: screen X coordinate
     * @screen_y: screen Y coordinate
     * @world_x: (out): location for world X coordinate
     * @world_y: (out): location for world Y coordinate
     *
     * Transforms screen coordinates to world (virtual) coordinates.
     *
     * This accounts for the virtual resolution, scaling mode, viewport
     * offset (letterbox/pillarbox bars), and any active camera transform.
     *
     * Use this to convert mouse/touch input positions to game world
     * coordinates for interaction detection.
     */
    void (*screen_to_world) (LrgTemplateScalable *self,
                             gfloat               screen_x,
                             gfloat               screen_y,
                             gfloat              *world_x,
                             gfloat              *world_y);
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_template_scalable_get_virtual_width:
 * @self: a #LrgTemplateScalable
 *
 * Gets the virtual (game) resolution width.
 *
 * Returns: the virtual width in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_template_scalable_get_virtual_width (LrgTemplateScalable *self);

/**
 * lrg_template_scalable_get_virtual_height:
 * @self: a #LrgTemplateScalable
 *
 * Gets the virtual (game) resolution height.
 *
 * Returns: the virtual height in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_template_scalable_get_virtual_height (LrgTemplateScalable *self);

/**
 * lrg_template_scalable_get_scaling_mode:
 * @self: a #LrgTemplateScalable
 *
 * Gets the current scaling mode.
 *
 * Returns: the current #LrgScalingMode
 */
LRG_AVAILABLE_IN_ALL
LrgScalingMode lrg_template_scalable_get_scaling_mode (LrgTemplateScalable *self);

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
LRG_AVAILABLE_IN_ALL
void lrg_template_scalable_world_to_screen (LrgTemplateScalable *self,
                                            gfloat               world_x,
                                            gfloat               world_y,
                                            gfloat              *screen_x,
                                            gfloat              *screen_y);

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
LRG_AVAILABLE_IN_ALL
void lrg_template_scalable_screen_to_world (LrgTemplateScalable *self,
                                            gfloat               screen_x,
                                            gfloat               screen_y,
                                            gfloat              *world_x,
                                            gfloat              *world_y);

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

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
LRG_AVAILABLE_IN_ALL
gfloat lrg_template_scalable_get_scale_factor (LrgTemplateScalable *self);

/**
 * lrg_template_scalable_get_viewport_offset:
 * @self: a #LrgTemplateScalable
 * @offset_x: (out) (optional): location for X offset
 * @offset_y: (out) (optional): location for Y offset
 *
 * Gets the viewport offset for letterbox/pillarbox bars.
 *
 * When the scaling mode produces bars (letterbox or pillarbox),
 * this returns the offset from the window origin to the actual
 * rendered content.
 */
LRG_AVAILABLE_IN_ALL
void lrg_template_scalable_get_viewport_offset (LrgTemplateScalable *self,
                                                gfloat              *offset_x,
                                                gfloat              *offset_y);

/**
 * lrg_template_scalable_get_viewport_size:
 * @self: a #LrgTemplateScalable
 * @width: (out) (optional): location for viewport width
 * @height: (out) (optional): location for viewport height
 *
 * Gets the actual rendered viewport size.
 *
 * This is the size of the area where game content is rendered,
 * excluding any letterbox/pillarbox bars.
 */
LRG_AVAILABLE_IN_ALL
void lrg_template_scalable_get_viewport_size (LrgTemplateScalable *self,
                                              gfloat              *width,
                                              gfloat              *height);

/**
 * lrg_template_scalable_is_point_in_viewport:
 * @self: a #LrgTemplateScalable
 * @screen_x: screen X coordinate
 * @screen_y: screen Y coordinate
 *
 * Checks if a screen point is within the rendered viewport.
 *
 * Points in letterbox/pillarbox bars return %FALSE.
 *
 * Returns: %TRUE if the point is in the viewport
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_template_scalable_is_point_in_viewport (LrgTemplateScalable *self,
                                                     gfloat               screen_x,
                                                     gfloat               screen_y);

G_END_DECLS
