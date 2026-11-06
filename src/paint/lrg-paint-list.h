/* lrg-paint-list.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A recorded frame of 2D UI drawing.
 *
 * Widgets record shapes into an #LrgPaintList instead of drawing
 * immediately. Every shape, including each glyph of text, becomes one
 * #LrgPaintInstance; consecutive instances that share at most one
 * texture merge into a single batch, so a whole UI is usually one or
 * two draw calls. #LrgPaintRenderer draws the list.
 *
 * Recording is pure CPU work and needs no window, which keeps widget
 * painting testable headless.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-paint-types.h"
#include "lrg-paint-fx.h"

G_BEGIN_DECLS

#define LRG_TYPE_PAINT_LIST (lrg_paint_list_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPaintList, lrg_paint_list, LRG, PAINT_LIST, GObject)

/**
 * LrgPaintCallback:
 * @user_data: data passed to lrg_paint_list_callback()
 *
 * Draws with raw graylib/raylib calls at this point of the paint
 * order. The renderer flushes its own batches first and applies the
 * current clip as a scissor rectangle around the call.
 */
typedef void (*LrgPaintCallback) (gpointer user_data);

/**
 * lrg_paint_list_new:
 *
 * Returns: (transfer full): a new, empty #LrgPaintList
 */
LRG_AVAILABLE_IN_ALL
LrgPaintList * lrg_paint_list_new (void);

/**
 * lrg_paint_list_clear:
 * @self: an #LrgPaintList
 *
 * Drops every recorded shape and command, resets the clip and opacity
 * stacks and releases texture/font references, keeping the allocated
 * memory for the next frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_clear (LrgPaintList *self);

/* ==========================================================================
 * State stacks
 * ========================================================================== */

/**
 * lrg_paint_list_push_clip:
 * @self: an #LrgPaintList
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 *
 * Intersects the current clip rectangle with the given one. Shapes
 * recorded until the matching lrg_paint_list_pop_clip() are cut to it
 * per pixel, without splitting batches.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_push_clip (LrgPaintList *self,
                               gfloat        x,
                               gfloat        y,
                               gfloat        width,
                               gfloat        height);

/**
 * lrg_paint_list_pop_clip:
 * @self: an #LrgPaintList
 *
 * Restores the clip rectangle from before the last push.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_pop_clip (LrgPaintList *self);

/**
 * lrg_paint_list_get_clip:
 * @self: an #LrgPaintList
 * @x: (out) (optional): left edge
 * @y: (out) (optional): top edge
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Gets the current clip rectangle. With nothing pushed it is very
 * large (effectively unclipped).
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_get_clip (LrgPaintList *self,
                              gfloat       *x,
                              gfloat       *y,
                              gfloat       *width,
                              gfloat       *height);

/**
 * lrg_paint_list_push_opacity:
 * @self: an #LrgPaintList
 * @opacity: 0..1, multiplied with the current opacity
 *
 * Fades everything recorded until the matching pop. Each shape's
 * alpha is multiplied individually, so overlapping children of a
 * faded widget show through each other slightly.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_push_opacity (LrgPaintList *self,
                                  gfloat        opacity);

/**
 * lrg_paint_list_pop_opacity:
 * @self: an #LrgPaintList
 *
 * Restores the opacity from before the last push.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_pop_opacity (LrgPaintList *self);

/**
 * lrg_paint_list_get_opacity:
 * @self: an #LrgPaintList
 *
 * Returns: the current accumulated opacity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_list_get_opacity (LrgPaintList *self);

/* ==========================================================================
 * Shapes
 * ========================================================================== */

/**
 * lrg_paint_list_rect:
 * @self: an #LrgPaintList
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @radius: corner radius for all corners
 * @color: fill colour
 *
 * Records a filled, anti-aliased rounded rectangle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_rect (LrgPaintList   *self,
                          gfloat          x,
                          gfloat          y,
                          gfloat          width,
                          gfloat          height,
                          gfloat          radius,
                          const GrlColor *color);

/**
 * lrg_paint_list_rect_ex:
 * @self: an #LrgPaintList
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @radii: (nullable): per-corner radii, %NULL for square corners
 * @fill: (nullable): fill colour, %NULL for no fill
 * @gradient: (nullable): gradient that replaces @fill
 * @border_width: inner border thickness, 0 for none
 * @border: (nullable): border colour
 *
 * Records a rounded rectangle with every option: per-corner radii,
 * a solid or gradient fill and an inner border. The border is drawn
 * inside the rectangle, so it never changes the widget's footprint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_rect_ex (LrgPaintList           *self,
                             gfloat                  x,
                             gfloat                  y,
                             gfloat                  width,
                             gfloat                  height,
                             const LrgCornerRadii   *radii,
                             const GrlColor         *fill,
                             const LrgPaintGradient *gradient,
                             gfloat                  border_width,
                             const GrlColor         *border);

/**
 * lrg_paint_list_border:
 * @self: an #LrgPaintList
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @radius: corner radius
 * @border_width: thickness
 * @color: border colour
 *
 * Records an unfilled rounded outline drawn inside the rectangle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_border (LrgPaintList   *self,
                            gfloat          x,
                            gfloat          y,
                            gfloat          width,
                            gfloat          height,
                            gfloat          radius,
                            gfloat          border_width,
                            const GrlColor *color);

/**
 * lrg_paint_list_shadow:
 * @self: an #LrgPaintList
 * @x: left edge of the casting shape
 * @y: top edge of the casting shape
 * @width: width of the casting shape
 * @height: height of the casting shape
 * @radius: corner radius of the casting shape
 * @blur: softness in canvas units
 * @spread: grows (positive) or shrinks (negative) the shadow
 * @offset_x: horizontal shift
 * @offset_y: vertical shift (positive is down, like a light from above)
 * @color: shadow colour; its alpha sets the strength
 *
 * Records a soft drop shadow. Record it before the shape that casts
 * it. The shadow is an analytic blur of the rounded rectangle, so it
 * costs the same as any other instance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_shadow (LrgPaintList   *self,
                            gfloat          x,
                            gfloat          y,
                            gfloat          width,
                            gfloat          height,
                            gfloat          radius,
                            gfloat          blur,
                            gfloat          spread,
                            gfloat          offset_x,
                            gfloat          offset_y,
                            const GrlColor *color);

/**
 * lrg_paint_list_circle:
 * @self: an #LrgPaintList
 * @center_x: centre X
 * @center_y: centre Y
 * @radius: radius
 * @color: fill colour
 *
 * Records an anti-aliased filled circle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_circle (LrgPaintList   *self,
                            gfloat          center_x,
                            gfloat          center_y,
                            gfloat          radius,
                            const GrlColor *color);

/**
 * lrg_paint_list_line:
 * @self: an #LrgPaintList
 * @x0: start X
 * @y0: start Y
 * @x1: end X
 * @y1: end Y
 * @thickness: line width
 * @color: line colour
 *
 * Records an anti-aliased line segment with round caps.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_line (LrgPaintList   *self,
                          gfloat          x0,
                          gfloat          y0,
                          gfloat          x1,
                          gfloat          y1,
                          gfloat          thickness,
                          const GrlColor *color);

/**
 * lrg_paint_list_triangle:
 * @self: an #LrgPaintList
 * @x0: first vertex X
 * @y0: first vertex Y
 * @x1: second vertex X
 * @y1: second vertex Y
 * @x2: third vertex X
 * @y2: third vertex Y
 * @color: fill colour
 *
 * Records an anti-aliased filled triangle (any winding), for arrows,
 * chevrons and carets.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_triangle (LrgPaintList   *self,
                              gfloat          x0,
                              gfloat          y0,
                              gfloat          x1,
                              gfloat          y1,
                              gfloat          x2,
                              gfloat          y2,
                              const GrlColor *color);

/**
 * lrg_paint_list_image:
 * @self: an #LrgPaintList
 * @texture: texture to draw
 * @source: (nullable): region of @texture in pixels, %NULL for all of it
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @radius: corner radius (rounded images and avatars)
 * @tint: (nullable): colour multiplied with the texture, %NULL for white
 *
 * Records a textured rectangle. The list keeps a reference to
 * @texture until it is cleared.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_image (LrgPaintList       *self,
                           GrlTexture         *texture,
                           const GrlRectangle *source,
                           gfloat              x,
                           gfloat              y,
                           gfloat              width,
                           gfloat              height,
                           gfloat              radius,
                           const GrlColor     *tint);

/* ==========================================================================
 * Text
 * ========================================================================== */

/**
 * lrg_paint_list_text:
 * @self: an #LrgPaintList
 * @font: (nullable): font, %NULL for the built-in default font
 * @text: UTF-8 text; '\n' starts a new line
 * @x: left edge
 * @y: top edge
 * @size: font size in canvas units
 * @spacing: extra space between characters
 * @color: text colour
 *
 * Records text as one instance per glyph. When an #LrgPaintFont is
 * bound to @font (or, for %NULL, set as the default with
 * lrg_paint_font_set_default()) the glyphs are SDF glyph instances
 * from the shared atlas, crisp at any scale, with fallback fonts and
 * bidi reordering; otherwise they are image instances sampling the
 * bitmap font's atlas. Either way text and shapes share batches as
 * long as a single atlas texture is in use. The origin is snapped to
 * whole units. The list keeps a reference to @font until cleared.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_text (LrgPaintList   *self,
                          GrlFont        *font,
                          const gchar    *text,
                          gfloat          x,
                          gfloat          y,
                          gfloat          size,
                          gfloat          spacing,
                          const GrlColor *color);

/**
 * lrg_paint_list_text_styled:
 * @self: an #LrgPaintList
 * @font: (nullable): font, %NULL for the default font
 * @text: UTF-8 text; '\n' starts a new line
 * @x: left edge
 * @y: top edge
 * @size: font size in canvas units
 * @style: colour, spacing, outline, glow, shadow and direction
 *
 * lrg_paint_list_text() with a full #LrgPaintTextStyle. When an
 * #LrgPaintFont is bound to @font (see lrg_paint_font_bind()) the text
 * is drawn from its distance-field atlas with every effect of @style;
 * bitmap fonts only use the colour and spacing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_text_styled (LrgPaintList            *self,
                                 GrlFont                 *font,
                                 const gchar             *text,
                                 gfloat                   x,
                                 gfloat                   y,
                                 gfloat                   size,
                                 const LrgPaintTextStyle *style);

/**
 * lrg_paint_measure_text:
 * @font: (nullable): font, %NULL for the built-in default font
 * @text: UTF-8 text
 * @size: font size
 * @spacing: extra space between characters
 * @width: (out) (optional): width of the widest line
 * @height: (out) (optional): total height of all lines
 *
 * Measures text exactly as lrg_paint_list_text() lays it out, through
 * the bound #LrgPaintFont when there is one (no window needed). For
 * bitmap fonts it needs the font's glyph data only; for %NULL fonts
 * without a default #LrgPaintFont it needs a window (raylib's default
 * font is created with the GL context) and reports an estimate
 * without one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_measure_text (GrlFont     *font,
                             const gchar *text,
                             gfloat       size,
                             gfloat       spacing,
                             gfloat      *width,
                             gfloat      *height);

/* ==========================================================================
 * Materials, effects and curves
 * ========================================================================== */

/**
 * lrg_paint_list_set_time:
 * @self: an #LrgPaintList
 * @seconds: animation time in seconds
 *
 * Sets the clock that animated primitives (effects, curve pulses) read
 * when the list is drawn. Paint never reads a clock itself: whoever
 * records the frame sets the time, so painting stays deterministic.
 * The value survives lrg_paint_list_clear().
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_set_time (LrgPaintList *self,
                              gdouble       seconds);

/**
 * lrg_paint_list_get_time:
 * @self: an #LrgPaintList
 *
 * Returns: the time set with lrg_paint_list_set_time(), 0 by default
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_paint_list_get_time (LrgPaintList *self);

/**
 * lrg_paint_list_set_pointer:
 * @self: an #LrgPaintList
 * @x: pointer x in canvas units
 * @y: pointer y in canvas units
 *
 * Sets the pointer position pointer-following effects (spotlight,
 * ripple, user effects) read when the list is drawn. #LrgCanvas sets it
 * each frame. Survives lrg_paint_list_clear().
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_set_pointer (LrgPaintList *self,
                                 gfloat        x,
                                 gfloat        y);

/**
 * lrg_paint_list_unset_pointer:
 * @self: an #LrgPaintList
 *
 * Marks the pointer as absent (outside the window): pointer-following
 * effects then show no pointer response.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_unset_pointer (LrgPaintList *self);

/**
 * lrg_paint_list_get_pointer:
 * @self: an #LrgPaintList
 * @x: (out) (optional): pointer x
 * @y: (out) (optional): pointer y
 *
 * Returns: %TRUE when a pointer position is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_paint_list_get_pointer (LrgPaintList *self,
                                     gfloat       *x,
                                     gfloat       *y);

/**
 * lrg_paint_list_blur:
 * @self: an #LrgPaintList
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @radius: corner radius
 * @blur_radius: blur radius in canvas units
 * @tint: (nullable): colour laid over the blur; alpha is its strength
 *
 * Records a rounded rectangle that shows what was drawn before it,
 * blurred (a backdrop blur, as behind frosted sidebars and sheets).
 * The renderer captures the target before drawing it; see the paint
 * module docs for the cost model.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_blur (LrgPaintList   *self,
                          gfloat          x,
                          gfloat          y,
                          gfloat          width,
                          gfloat          height,
                          gfloat          radius,
                          gfloat          blur_radius,
                          const GrlColor *tint);

/**
 * lrg_paint_list_glass:
 * @self: an #LrgPaintList
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @radii: (nullable): corner radii, %NULL for square corners
 * @params: (nullable): the glass look, %NULL for
 *   lrg_paint_glass_init_default()
 *
 * Records a glass slab: the backdrop seen through it is frosted,
 * saturated, tinted, bent along the rim (refraction, with optional
 * chromatic dispersion) and lit by a specular rim from the top-left.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_glass (LrgPaintList          *self,
                           gfloat                 x,
                           gfloat                 y,
                           gfloat                 width,
                           gfloat                 height,
                           const LrgCornerRadii  *radii,
                           const LrgPaintGlass   *params);

/**
 * lrg_paint_list_effect:
 * @self: an #LrgPaintList
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @radii: (nullable): corner radii, %NULL for square corners
 * @effect_id: an id from lrg_paint_effect_lookup()
 * @params: (nullable): effect inputs, %NULL for
 *   lrg_paint_effect_params_init_default()
 *
 * Records a rounded rectangle filled by an animated effect. Effects
 * batch with every other instance (one shader, a switch on the id).
 * Unknown ids draw nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_effect (LrgPaintList               *self,
                            gfloat                      x,
                            gfloat                      y,
                            gfloat                      width,
                            gfloat                      height,
                            const LrgCornerRadii       *radii,
                            gint                        effect_id,
                            const LrgPaintEffectParams *params);

/**
 * lrg_paint_list_curve:
 * @self: an #LrgPaintList
 * @x0: start point x
 * @y0: start point y
 * @x1: first control point x
 * @y1: first control point y
 * @x2: second control point x
 * @y2: second control point y
 * @x3: end point x
 * @y3: end point y
 * @thickness: stroke width
 * @style: the stroke look
 *
 * Records a cubic Bezier stroke (a node-graph wire): a colour gradient
 * from start to end, an optional glow halo and optional pulses that
 * travel along it with the list time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_curve (LrgPaintList             *self,
                           gfloat                    x0,
                           gfloat                    y0,
                           gfloat                    x1,
                           gfloat                    y1,
                           gfloat                    x2,
                           gfloat                    y2,
                           gfloat                    x3,
                           gfloat                    y3,
                           gfloat                    thickness,
                           const LrgPaintCurveStyle *style);

/* ==========================================================================
 * Escape hatch
 * ========================================================================== */

/**
 * lrg_paint_list_callback:
 * @self: an #LrgPaintList
 * @func: (scope notified) (closure user_data) (destroy destroy): function that draws with raw graylib
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when the list is cleared
 *
 * Records a call into immediate-mode drawing at this point of the
 * paint order. Used to keep legacy widget draw() code working and for
 * anything the instance pipeline cannot express (3D viewports, custom
 * shaders). Each callback ends the current batch.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_callback (LrgPaintList     *self,
                              LrgPaintCallback  func,
                              gpointer          user_data,
                              GDestroyNotify    destroy);

/* ==========================================================================
 * Groups (post-layout transforms)
 * ========================================================================== */

/**
 * lrg_paint_list_begin_group:
 * @self: an #LrgPaintList
 *
 * Marks the start of a range of shapes that may be transformed as a
 * whole once recorded, e.g. a widget's appear animation.
 *
 * Returns: a mark to pass to lrg_paint_list_transform_group()
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_list_begin_group (LrgPaintList *self);

/**
 * lrg_paint_list_transform_group:
 * @self: an #LrgPaintList
 * @mark: value returned by lrg_paint_list_begin_group()
 * @scale: uniform scale around the pivot
 * @pivot_x: scale pivot X
 * @pivot_y: scale pivot Y
 * @translate_x: shift applied after scaling
 * @translate_y: shift applied after scaling
 * @opacity: alpha multiplier, 0..1
 *
 * Scales, moves and fades every instance recorded since @mark. Clip
 * rectangles are left alone, so a widget sliding in from outside its
 * parent is still cut by the parent. Callbacks in the range are not
 * transformed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_transform_group (LrgPaintList *self,
                                     guint         mark,
                                     gfloat        scale,
                                     gfloat        pivot_x,
                                     gfloat        pivot_y,
                                     gfloat        translate_x,
                                     gfloat        translate_y,
                                     gfloat        opacity);

/* ==========================================================================
 * Inspection
 * ========================================================================== */

/**
 * lrg_paint_list_get_n_instances:
 * @self: an #LrgPaintList
 *
 * Returns: number of recorded instances
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_list_get_n_instances (LrgPaintList *self);

/**
 * lrg_paint_list_get_instance:
 * @self: an #LrgPaintList
 * @index: instance index
 *
 * Returns: (transfer none): the instance, valid until the list changes
 */
LRG_AVAILABLE_IN_ALL
const LrgPaintInstance * lrg_paint_list_get_instance (LrgPaintList *self,
                                                      guint         index);

/**
 * lrg_paint_list_get_instances:
 * @self: an #LrgPaintList
 * @n_instances: (out) (optional): number of instances
 *
 * Returns: (transfer none) (array length=n_instances): the instance
 *   array, valid until the list changes
 */
LRG_AVAILABLE_IN_ALL
const LrgPaintInstance * lrg_paint_list_get_instances (LrgPaintList *self,
                                                       guint        *n_instances);

/**
 * lrg_paint_list_get_n_commands:
 * @self: an #LrgPaintList
 *
 * Returns: number of commands (instance batches and callbacks)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_list_get_n_commands (LrgPaintList *self);

/**
 * lrg_paint_list_get_command:
 * @self: an #LrgPaintList
 * @index: command index
 * @first: (out) (optional): first instance of a batch
 * @count: (out) (optional): number of instances in a batch
 * @texture: (out) (optional): GL texture id the batch samples (0 for none)
 *
 * Describes one command.
 *
 * Returns: the command kind
 */
LRG_AVAILABLE_IN_ALL
LrgPaintCommandKind lrg_paint_list_get_command (LrgPaintList *self,
                                                guint         index,
                                                guint        *first,
                                                guint        *count,
                                                guint        *texture);

/**
 * lrg_paint_list_get_command_clip:
 * @self: an #LrgPaintList
 * @index: command index
 * @x0: (out) (optional): left edge of the clip
 * @y0: (out) (optional): top edge of the clip
 * @x1: (out) (optional): right edge of the clip
 * @y1: (out) (optional): bottom edge of the clip
 *
 * Gets the clip that was active when a callback was recorded.
 *
 * Returns: %TRUE if the callback is clipped, %FALSE if it was recorded
 *   with no clip pushed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_paint_list_get_command_clip (LrgPaintList *self,
                                          guint         index,
                                          gfloat       *x0,
                                          gfloat       *y0,
                                          gfloat       *x1,
                                          gfloat       *y1);

/**
 * lrg_paint_list_run_callback:
 * @self: an #LrgPaintList
 * @index: index of a %LRG_PAINT_COMMAND_CALLBACK command
 *
 * Invokes a recorded callback. Used by renderers.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_run_callback (LrgPaintList *self,
                                  guint         index);

G_END_DECLS
