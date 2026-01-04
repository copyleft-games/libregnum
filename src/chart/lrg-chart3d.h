/* lrg-chart3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChart3D - Intermediate class for 3D charts.
 *
 * LrgChart3D extends LrgChart with functionality specific to 3D charting:
 * - 3D camera control (rotation, zoom, pan)
 * - 3D axis rendering
 * - Coordinate transformations for 3D to 2D projection
 *
 * Concrete 3D chart types should extend this class.
 * Note: 3D charts render to a 2D surface using projection.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-chart.h"
#include "lrg-chart-axis-config.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART3D (lrg_chart3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgChart3D, lrg_chart3d, LRG, CHART3D, LrgChart)

/**
 * LrgChart3DClass:
 * @parent_class: The parent class
 * @draw_background: Virtual method to draw the chart background
 * @draw_axes_3d: Virtual method to draw the 3D axes
 * @draw_grid_3d: Virtual method to draw the 3D grid
 * @draw_data_3d: Virtual method to draw the 3D data
 * @draw_legend: Virtual method to draw the legend
 * @rotate_view: Virtual method to handle view rotation
 * @zoom_view: Virtual method to handle view zoom
 *
 * The class structure for #LrgChart3D.
 *
 * Subclasses must implement @draw_data_3d to render their specific chart type.
 */
struct _LrgChart3DClass
{
    LrgChartClass parent_class;

    /* Virtual methods */

    /**
     * LrgChart3DClass::draw_background:
     * @self: the chart
     *
     * Draws the chart background.
     */
    void (*draw_background) (LrgChart3D *self);

    /**
     * LrgChart3DClass::draw_axes_3d:
     * @self: the chart
     *
     * Draws the 3D X, Y, and Z axes with labels.
     */
    void (*draw_axes_3d) (LrgChart3D *self);

    /**
     * LrgChart3DClass::draw_grid_3d:
     * @self: the chart
     *
     * Draws the 3D grid on the base plane.
     */
    void (*draw_grid_3d) (LrgChart3D *self);

    /**
     * LrgChart3DClass::draw_data_3d:
     * @self: the chart
     *
     * Draws the 3D chart data. Subclasses must implement this.
     */
    void (*draw_data_3d) (LrgChart3D *self);

    /**
     * LrgChart3DClass::draw_legend:
     * @self: the chart
     *
     * Draws the chart legend.
     */
    void (*draw_legend) (LrgChart3D *self);

    /**
     * LrgChart3DClass::rotate_view:
     * @self: the chart
     * @delta_yaw: change in yaw angle (degrees)
     * @delta_pitch: change in pitch angle (degrees)
     *
     * Rotates the 3D view.
     */
    void (*rotate_view) (LrgChart3D *self,
                         gfloat      delta_yaw,
                         gfloat      delta_pitch);

    /**
     * LrgChart3DClass::zoom_view:
     * @self: the chart
     * @delta: zoom amount (positive = zoom in)
     *
     * Zooms the 3D view.
     */
    void (*zoom_view) (LrgChart3D *self,
                       gfloat      delta);

    /*< private >*/
    gpointer _reserved[4];
};

/* ==========================================================================
 * Camera Control
 * ========================================================================== */

/**
 * lrg_chart3d_get_camera_yaw:
 * @self: an #LrgChart3D
 *
 * Gets the camera yaw angle (horizontal rotation).
 *
 * Returns: yaw angle in degrees
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart3d_get_camera_yaw (LrgChart3D *self);

/**
 * lrg_chart3d_set_camera_yaw:
 * @self: an #LrgChart3D
 * @yaw: yaw angle in degrees
 *
 * Sets the camera yaw angle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_camera_yaw (LrgChart3D *self,
                            gfloat      yaw);

/**
 * lrg_chart3d_get_camera_pitch:
 * @self: an #LrgChart3D
 *
 * Gets the camera pitch angle (vertical rotation).
 *
 * Returns: pitch angle in degrees
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart3d_get_camera_pitch (LrgChart3D *self);

/**
 * lrg_chart3d_set_camera_pitch:
 * @self: an #LrgChart3D
 * @pitch: pitch angle in degrees
 *
 * Sets the camera pitch angle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_camera_pitch (LrgChart3D *self,
                              gfloat      pitch);

/**
 * lrg_chart3d_get_camera_distance:
 * @self: an #LrgChart3D
 *
 * Gets the camera distance from the center.
 *
 * Returns: distance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart3d_get_camera_distance (LrgChart3D *self);

/**
 * lrg_chart3d_set_camera_distance:
 * @self: an #LrgChart3D
 * @distance: distance from center
 *
 * Sets the camera distance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_camera_distance (LrgChart3D *self,
                                 gfloat      distance);

/**
 * lrg_chart3d_get_field_of_view:
 * @self: an #LrgChart3D
 *
 * Gets the camera field of view.
 *
 * Returns: FOV in degrees
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart3d_get_field_of_view (LrgChart3D *self);

/**
 * lrg_chart3d_set_field_of_view:
 * @self: an #LrgChart3D
 * @fov: field of view in degrees
 *
 * Sets the camera field of view.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_field_of_view (LrgChart3D *self,
                               gfloat      fov);

/**
 * lrg_chart3d_set_camera_angle:
 * @self: an #LrgChart3D
 * @yaw: yaw angle in degrees
 * @pitch: pitch angle in degrees
 *
 * Sets both camera angles at once.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_camera_angle (LrgChart3D *self,
                              gfloat      yaw,
                              gfloat      pitch);

/* ==========================================================================
 * Axis Configuration
 * ========================================================================== */

/**
 * lrg_chart3d_get_x_axis:
 * @self: an #LrgChart3D
 *
 * Gets the X axis configuration.
 *
 * Returns: (transfer none): the X axis config
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart3d_get_x_axis (LrgChart3D *self);

/**
 * lrg_chart3d_set_x_axis:
 * @self: an #LrgChart3D
 * @config: the X axis config
 *
 * Sets the X axis configuration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_x_axis (LrgChart3D         *self,
                        LrgChartAxisConfig *config);

/**
 * lrg_chart3d_get_y_axis:
 * @self: an #LrgChart3D
 *
 * Gets the Y axis configuration.
 *
 * Returns: (transfer none): the Y axis config
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart3d_get_y_axis (LrgChart3D *self);

/**
 * lrg_chart3d_set_y_axis:
 * @self: an #LrgChart3D
 * @config: the Y axis config
 *
 * Sets the Y axis configuration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_y_axis (LrgChart3D         *self,
                        LrgChartAxisConfig *config);

/**
 * lrg_chart3d_get_z_axis:
 * @self: an #LrgChart3D
 *
 * Gets the Z axis configuration.
 *
 * Returns: (transfer none): the Z axis config
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart3d_get_z_axis (LrgChart3D *self);

/**
 * lrg_chart3d_set_z_axis:
 * @self: an #LrgChart3D
 * @config: the Z axis config
 *
 * Sets the Z axis configuration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_z_axis (LrgChart3D         *self,
                        LrgChartAxisConfig *config);

/* ==========================================================================
 * Data Ranges
 * ========================================================================== */

/**
 * lrg_chart3d_get_x_range:
 * @self: an #LrgChart3D
 * @out_min: (out) (optional): minimum X value
 * @out_max: (out) (optional): maximum X value
 *
 * Gets the effective X data range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_get_x_range (LrgChart3D *self,
                         gdouble    *out_min,
                         gdouble    *out_max);

/**
 * lrg_chart3d_get_y_range:
 * @self: an #LrgChart3D
 * @out_min: (out) (optional): minimum Y value
 * @out_max: (out) (optional): maximum Y value
 *
 * Gets the effective Y data range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_get_y_range (LrgChart3D *self,
                         gdouble    *out_min,
                         gdouble    *out_max);

/**
 * lrg_chart3d_get_z_range:
 * @self: an #LrgChart3D
 * @out_min: (out) (optional): minimum Z value
 * @out_max: (out) (optional): maximum Z value
 *
 * Gets the effective Z data range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_get_z_range (LrgChart3D *self,
                         gdouble    *out_min,
                         gdouble    *out_max);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_chart3d_get_show_legend:
 * @self: an #LrgChart3D
 *
 * Gets whether the legend is shown.
 *
 * Returns: %TRUE if legend is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart3d_get_show_legend (LrgChart3D *self);

/**
 * lrg_chart3d_set_show_legend:
 * @self: an #LrgChart3D
 * @show: whether to show legend
 *
 * Sets whether the legend is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_show_legend (LrgChart3D *self,
                             gboolean    show);

/**
 * lrg_chart3d_get_show_axes:
 * @self: an #LrgChart3D
 *
 * Gets whether 3D axes are shown.
 *
 * Returns: %TRUE if axes are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart3d_get_show_axes (LrgChart3D *self);

/**
 * lrg_chart3d_set_show_axes:
 * @self: an #LrgChart3D
 * @show: whether to show axes
 *
 * Sets whether 3D axes are shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_show_axes (LrgChart3D *self,
                           gboolean    show);

/**
 * lrg_chart3d_get_show_grid:
 * @self: an #LrgChart3D
 *
 * Gets whether the base grid is shown.
 *
 * Returns: %TRUE if grid is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart3d_get_show_grid (LrgChart3D *self);

/**
 * lrg_chart3d_set_show_grid:
 * @self: an #LrgChart3D
 * @show: whether to show grid
 *
 * Sets whether the base grid is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_show_grid (LrgChart3D *self,
                           gboolean    show);

/**
 * lrg_chart3d_get_enable_rotation:
 * @self: an #LrgChart3D
 *
 * Gets whether interactive rotation is enabled.
 *
 * Returns: %TRUE if rotation is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart3d_get_enable_rotation (LrgChart3D *self);

/**
 * lrg_chart3d_set_enable_rotation:
 * @self: an #LrgChart3D
 * @enable: whether to enable rotation
 *
 * Sets whether interactive rotation is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_set_enable_rotation (LrgChart3D *self,
                                 gboolean    enable);

/* ==========================================================================
 * Coordinate Conversion
 * ========================================================================== */

/**
 * lrg_chart3d_data_to_screen:
 * @self: an #LrgChart3D
 * @data_x: X value in data coordinates
 * @data_y: Y value in data coordinates
 * @data_z: Z value in data coordinates
 * @screen_x: (out): X value in screen coordinates
 * @screen_y: (out): Y value in screen coordinates
 *
 * Projects 3D data coordinates to 2D screen coordinates.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_data_to_screen (LrgChart3D *self,
                            gdouble     data_x,
                            gdouble     data_y,
                            gdouble     data_z,
                            gfloat     *screen_x,
                            gfloat     *screen_y);

/**
 * lrg_chart3d_get_depth:
 * @self: an #LrgChart3D
 * @data_x: X value in data coordinates
 * @data_y: Y value in data coordinates
 * @data_z: Z value in data coordinates
 *
 * Gets the depth value for a 3D point (for sorting).
 *
 * Returns: depth value (larger = further from camera)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart3d_get_depth (LrgChart3D *self,
                       gdouble     data_x,
                       gdouble     data_y,
                       gdouble     data_z);

/* ==========================================================================
 * Drawing Helpers
 * ========================================================================== */

/**
 * lrg_chart3d_draw_background:
 * @self: an #LrgChart3D
 *
 * Draws the chart background.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_background (LrgChart3D *self);

/**
 * lrg_chart3d_draw_axes_3d:
 * @self: an #LrgChart3D
 *
 * Draws the 3D axes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_axes_3d (LrgChart3D *self);

/**
 * lrg_chart3d_draw_grid_3d:
 * @self: an #LrgChart3D
 *
 * Draws the 3D grid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_grid_3d (LrgChart3D *self);

/**
 * lrg_chart3d_draw_data_3d:
 * @self: an #LrgChart3D
 *
 * Draws the 3D data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_data_3d (LrgChart3D *self);

/**
 * lrg_chart3d_draw_legend:
 * @self: an #LrgChart3D
 *
 * Draws the legend.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_legend (LrgChart3D *self);

/**
 * lrg_chart3d_rotate_view:
 * @self: an #LrgChart3D
 * @delta_yaw: change in yaw (degrees)
 * @delta_pitch: change in pitch (degrees)
 *
 * Rotates the 3D view.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_rotate_view (LrgChart3D *self,
                         gfloat      delta_yaw,
                         gfloat      delta_pitch);

/**
 * lrg_chart3d_zoom_view:
 * @self: an #LrgChart3D
 * @delta: zoom delta (positive = zoom in)
 *
 * Zooms the 3D view.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_zoom_view (LrgChart3D *self,
                       gfloat      delta);

/* ==========================================================================
 * 3D Primitive Drawing (for subclasses)
 * ========================================================================== */

/**
 * lrg_chart3d_draw_line_3d:
 * @self: an #LrgChart3D
 * @x1: first point X
 * @y1: first point Y
 * @z1: first point Z
 * @x2: second point X
 * @y2: second point Y
 * @z2: second point Z
 * @color: line color
 * @width: line width
 *
 * Draws a 3D line projected to screen.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_line_3d (LrgChart3D *self,
                          gdouble     x1,
                          gdouble     y1,
                          gdouble     z1,
                          gdouble     x2,
                          gdouble     y2,
                          gdouble     z2,
                          GrlColor   *color,
                          gfloat      width);

/**
 * lrg_chart3d_draw_box_3d:
 * @self: an #LrgChart3D
 * @x: center X
 * @y: base Y (bottom)
 * @z: center Z
 * @width_x: width in X direction
 * @height: height in Y direction
 * @depth_z: depth in Z direction
 * @color: box color
 *
 * Draws a 3D box/bar.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_box_3d (LrgChart3D *self,
                         gdouble     x,
                         gdouble     y,
                         gdouble     z,
                         gdouble     width_x,
                         gdouble     height,
                         gdouble     depth_z,
                         GrlColor   *color);

/**
 * lrg_chart3d_draw_point_3d:
 * @self: an #LrgChart3D
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 * @radius: point radius in screen pixels
 * @color: point color
 *
 * Draws a 3D point (sphere projected to circle).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart3d_draw_point_3d (LrgChart3D *self,
                           gdouble     x,
                           gdouble     y,
                           gdouble     z,
                           gfloat      radius,
                           GrlColor   *color);

G_END_DECLS
