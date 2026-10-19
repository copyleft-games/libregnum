/* lrg-photo-look.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoLook - the colour look of a photo: exposure, contrast,
 * saturation, white balance, lift/gamma, an optional lookup table,
 * vignette and film grain, with presets.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../postprocess/lrg-color-lut.h"
#include "../postprocess/effects/lrg-color-grade.h"

G_BEGIN_DECLS

#define LRG_TYPE_PHOTO_LOOK (lrg_photo_look_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhotoLook, lrg_photo_look, LRG, PHOTO_LOOK, GObject)

/**
 * lrg_photo_look_new:
 *
 * Creates a neutral look (every adjustment at its identity, no table).
 *
 * Returns: (transfer full): a new #LrgPhotoLook
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoLook *
lrg_photo_look_new (void);

/**
 * lrg_photo_look_new_for_preset:
 * @preset: the preset to start from
 *
 * Creates a look with @preset applied.
 *
 * Returns: (transfer full): a new #LrgPhotoLook
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoLook *
lrg_photo_look_new_for_preset (LrgPhotoPreset preset);

/**
 * lrg_photo_look_get_preset:
 * @self: an #LrgPhotoLook
 *
 * Gets the preset last applied with lrg_photo_look_apply_preset().
 * Changing an adjustment afterwards does not change it (the look is the
 * preset "plus edits"); lrg_photo_look_reset() sets it to
 * %LRG_PHOTO_PRESET_NONE.
 *
 * Returns: the #LrgPhotoPreset
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoPreset
lrg_photo_look_get_preset (LrgPhotoLook *self);

/**
 * lrg_photo_look_apply_preset:
 * @self: an #LrgPhotoLook
 * @preset: the preset
 *
 * Replaces the colour adjustments (exposure, contrast, saturation,
 * white balance, lift, gamma) and the vignette and grain with the
 * values of @preset.  The lookup table and its strength are kept.
 * %LRG_PHOTO_PRESET_NONE is the neutral look.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_apply_preset (LrgPhotoLook  *self,
                             LrgPhotoPreset preset);

/**
 * lrg_photo_preset_get_label:
 * @preset: a preset
 *
 * Gets the display name of @preset ("Noir").
 *
 * Returns: (transfer none): the label, never %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_preset_get_label (LrgPhotoPreset preset);

/**
 * lrg_photo_look_reset:
 * @self: an #LrgPhotoLook
 *
 * Sets every adjustment to neutral and drops the lookup table.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_reset (LrgPhotoLook *self);

/**
 * lrg_photo_look_is_identity:
 * @self: an #LrgPhotoLook
 *
 * Whether the look changes nothing (no grade, table, vignette or
 * grain), so a host can skip the passes.
 *
 * Returns: %TRUE for the neutral look
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_look_is_identity (LrgPhotoLook *self);

/**
 * lrg_photo_look_has_finish:
 * @self: an #LrgPhotoLook
 *
 * Whether the vignette or the grain is on (the "finish" pass).
 *
 * Returns: %TRUE when lrg_photo_look_draw_finish() changes the picture
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_look_has_finish (LrgPhotoLook *self);

/**
 * lrg_photo_look_get_lut:
 * @self: an #LrgPhotoLook
 *
 * Gets the lookup table (colour grading preset from a .cube file or
 * M14's built-in looks).
 *
 * Returns: (transfer none) (nullable): the table or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_photo_look_get_lut (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_lut:
 * @self: an #LrgPhotoLook
 * @lut: (nullable): a lookup table, or %NULL for none
 *
 * Sets the lookup table; the look keeps its own reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_lut (LrgPhotoLook *self,
                        LrgColorLut  *lut);

/**
 * lrg_photo_look_configure_grade:
 * @self: an #LrgPhotoLook
 * @grade: the effect to configure
 *
 * Copies the look's colour adjustments and table into @grade (a
 * #LrgColorGrade drawn with lrg_color_grade_draw_texture(), or a
 * post processor's effect).  Hosts that already grade the view
 * (librefall's colour.* options) combine the two themselves.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_configure_grade (LrgPhotoLook *self,
                                LrgColorGrade *grade);

/**
 * lrg_photo_look_apply_rgb:
 * @self: an #LrgPhotoLook
 * @in: (array fixed-size=3): a colour, channels 0.0 to 1.0
 * @out: (out caller-allocates) (array fixed-size=3): the graded colour
 *   (may be @in); the grade and table only, not the finish
 *
 * The grade on the CPU: the same arithmetic as the shader of
 * #LrgColorGrade, the reference tests compare against.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_apply_rgb (LrgPhotoLook *self,
                          const gfloat *in,
                          gfloat       *out);

/**
 * lrg_photo_look_get_vignette_factor:
 * @self: an #LrgPhotoLook
 * @u: horizontal position, 0.0 (left) to 1.0 (right)
 * @v: vertical position, 0.0 (top) to 1.0 (bottom)
 * @aspect: width over height of the picture
 *
 * The factor the vignette multiplies a pixel by (1.0 in the middle, down
 * to 1 minus the strength at the far corners): the distance from the
 * centre is measured in the picture's own proportions, as a fraction of
 * the half diagonal, and the darkening grows smoothly from
 * the vignette radius to the corners.
 *
 * Returns: the factor, 0.0 to 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_vignette_factor (LrgPhotoLook *self,
                                    gfloat        u,
                                    gfloat        v,
                                    gfloat        aspect);

/**
 * lrg_photo_look_get_grain_value:
 * @self: an #LrgPhotoLook
 * @x: pixel column
 * @y: pixel row
 * @seed: the grain's seed (a host changes it per frame or per photo)
 *
 * The grain added to every channel of a pixel: a hash noise between
 * minus and plus half the grain amount times 0.4, identical on every call
 * with the same arguments.
 *
 * Returns: the offset to add
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_grain_value (LrgPhotoLook *self,
                                gint          x,
                                gint          y,
                                guint         seed);

/**
 * lrg_photo_look_apply_image:
 * @self: an #LrgPhotoLook
 * @image: the image to grade in place
 * @seed: the grain's seed
 *
 * Applies the whole look (grade, table, vignette, grain) to @image on
 * the CPU.  This is the display-free path (tests, tools, hosts without
 * shaders); a window host draws through the shaders instead.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_apply_image (LrgPhotoLook *self,
                            GrlImage     *image,
                            guint         seed);

/**
 * lrg_photo_look_draw_finish:
 * @self: an #LrgPhotoLook
 * @texture: the (already graded) texture to draw
 * @source: the part of @texture in texels (negative height flips)
 * @dest: where to draw it
 * @seed: the grain's seed
 *
 * Draws @texture into the current target through the vignette and
 * grain shader (the host runs the grade before it).  Without a shader
 * or window it draws plainly and returns %FALSE; with neither effect
 * on it draws plainly and returns %TRUE.
 *
 * Returns: %TRUE when the finish was applied (or not needed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_look_draw_finish (LrgPhotoLook       *self,
                            GrlTexture         *texture,
                            const GrlRectangle *source,
                            const GrlRectangle *dest,
                            guint               seed);

/**
 * lrg_photo_look_release_gpu:
 * @self: an #LrgPhotoLook
 *
 * Releases the shaders, call before the window closes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_release_gpu (LrgPhotoLook *self);

/**
 * lrg_photo_look_get_exposure:
 * @self: an #LrgPhotoLook
 *
 * Gets the exposure compensation in stops (EV) (range -3.0 to 3.0, default 0.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_exposure (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_exposure:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to -3.0 .. 3.0
 *
 * Sets the exposure compensation in stops (EV).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_exposure (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_contrast:
 * @self: an #LrgPhotoLook
 *
 * Gets the contrast around mid grey (1 is neutral) (range 0.25 to 2.5, default 1.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_contrast (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_contrast:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to 0.25 .. 2.5
 *
 * Sets the contrast around mid grey (1 is neutral).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_contrast (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_saturation:
 * @self: an #LrgPhotoLook
 *
 * Gets the colour saturation (0 is monochrome, 1 neutral) (range 0.0 to 2.5, default 1.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_saturation (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_saturation:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to 0.0 .. 2.5
 *
 * Sets the colour saturation (0 is monochrome, 1 neutral).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_saturation (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_temperature:
 * @self: an #LrgPhotoLook
 *
 * Gets the white balance (negative cool, positive warm) (range -1.0 to 1.0, default 0.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_temperature (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_temperature:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to -1.0 .. 1.0
 *
 * Sets the white balance (negative cool, positive warm).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_temperature (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_tint:
 * @self: an #LrgPhotoLook
 *
 * Gets the white balance tint (negative magenta, positive green) (range -1.0 to 1.0, default 0.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_tint (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_tint:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to -1.0 .. 1.0
 *
 * Sets the white balance tint (negative magenta, positive green).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_tint (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_lift:
 * @self: an #LrgPhotoLook
 *
 * Gets the shadow lift added to every channel (range -0.2 to 0.3, default 0.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_lift (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_lift:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to -0.2 .. 0.3
 *
 * Sets the shadow lift added to every channel.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_lift (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_gamma:
 * @self: an #LrgPhotoLook
 *
 * Gets the midtone gamma (above 1 brightens the midtones) (range 0.5 to 2.0, default 1.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_gamma (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_gamma:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to 0.5 .. 2.0
 *
 * Sets the midtone gamma (above 1 brightens the midtones).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_gamma (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_vignette:
 * @self: an #LrgPhotoLook
 *
 * Gets the vignette strength (0 off, 1 black corners) (range 0.0 to 1.0, default 0.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_vignette (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_vignette:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to 0.0 .. 1.0
 *
 * Sets the vignette strength (0 off, 1 black corners).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_vignette (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_vignette_radius:
 * @self: an #LrgPhotoLook
 *
 * Gets the radius where the vignette starts, as a fraction of the half diagonal (range 0.1 to 1.0, default 0.5).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_vignette_radius (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_vignette_radius:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to 0.1 .. 1.0
 *
 * Sets the radius where the vignette starts, as a fraction of the half diagonal.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_vignette_radius (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_grain:
 * @self: an #LrgPhotoLook
 *
 * Gets the film grain amount (0 off) (range 0.0 to 1.0, default 0.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_grain (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_grain:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to 0.0 .. 1.0
 *
 * Sets the film grain amount (0 off).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_grain (LrgPhotoLook *self,
                         gfloat        value);

/**
 * lrg_photo_look_get_lut_strength:
 * @self: an #LrgPhotoLook
 *
 * Gets the how strongly the lookup table is mixed in (range 0.0 to 1.0, default 1.0).
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_look_get_lut_strength (LrgPhotoLook *self);

/**
 * lrg_photo_look_set_lut_strength:
 * @self: an #LrgPhotoLook
 * @value: the new value, clamped to 0.0 .. 1.0
 *
 * Sets the how strongly the lookup table is mixed in.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_look_set_lut_strength (LrgPhotoLook *self,
                         gfloat        value);

G_END_DECLS
