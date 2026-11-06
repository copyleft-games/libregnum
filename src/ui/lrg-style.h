/* lrg-style.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Part looks and style rules.
 *
 * An #LrgPartLook is everything that decides how one part of a widget
 * looks: colours, border, radii, shadow, gradient, opacity and size.
 * An #LrgStyleRule says "for widgets of this type, with this style
 * class, this part, in these states, override these look fields".
 * Rules live in an #LrgTheme and are matched with CSS-like
 * specificity.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../paint/lrg-paint-types.h"
#include "../paint/lrg-paint-fx.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgPartLook
 * ========================================================================== */

/**
 * LrgPartLookFields:
 * @LRG_PART_LOOK_NONE: no field set
 * @LRG_PART_LOOK_BACKGROUND: @background is set
 * @LRG_PART_LOOK_FOREGROUND: @foreground is set
 * @LRG_PART_LOOK_BORDER: @border is set
 * @LRG_PART_LOOK_BORDER_WIDTH: @border_width is set
 * @LRG_PART_LOOK_RADII: @radii is set
 * @LRG_PART_LOOK_SHADOW: @shadow is set
 * @LRG_PART_LOOK_SHADOW_BLUR: @shadow_blur is set
 * @LRG_PART_LOOK_SHADOW_OFFSET: @shadow_offset_y is set
 * @LRG_PART_LOOK_GRADIENT: @gradient is set
 * @LRG_PART_LOOK_OPACITY: @opacity is set
 * @LRG_PART_LOOK_SIZE: @size is set
 * @LRG_PART_LOOK_GLOW: @glow is set
 * @LRG_PART_LOOK_GLOW_SIZE: @glow_size is set
 * @LRG_PART_LOOK_MATERIAL: @material and @material_opacity are set
 * @LRG_PART_LOOK_GLASS: @glass is set
 * @LRG_PART_LOOK_EFFECT: @effect is set
 * @LRG_PART_LOOK_EFFECT_COLORS: the three colours of @effect_params
 *   are set
 * @LRG_PART_LOOK_EFFECT_INTENSITY: the intensity of @effect_params is
 *   set
 * @LRG_PART_LOOK_ALL: every field
 *
 * Which fields of an #LrgPartLook carry a value. Merging copies only
 * the fields that are set, so a rule can change one colour and leave
 * the rest of the look alone.
 */
typedef enum /*< flags >*/
{
	LRG_PART_LOOK_NONE          = 0,
	LRG_PART_LOOK_BACKGROUND    = 1 << 0,
	LRG_PART_LOOK_FOREGROUND    = 1 << 1,
	LRG_PART_LOOK_BORDER        = 1 << 2,
	LRG_PART_LOOK_BORDER_WIDTH  = 1 << 3,
	LRG_PART_LOOK_RADII         = 1 << 4,
	LRG_PART_LOOK_SHADOW        = 1 << 5,
	LRG_PART_LOOK_SHADOW_BLUR   = 1 << 6,
	LRG_PART_LOOK_SHADOW_OFFSET = 1 << 7,
	LRG_PART_LOOK_GRADIENT      = 1 << 8,
	LRG_PART_LOOK_OPACITY       = 1 << 9,
	LRG_PART_LOOK_SIZE          = 1 << 10,
	LRG_PART_LOOK_GLOW          = 1 << 11,
	LRG_PART_LOOK_GLOW_SIZE     = 1 << 12,
	LRG_PART_LOOK_MATERIAL      = 1 << 13,
	LRG_PART_LOOK_GLASS         = 1 << 14,
	LRG_PART_LOOK_EFFECT        = 1 << 15,
	LRG_PART_LOOK_EFFECT_COLORS = 1 << 16,
	LRG_PART_LOOK_EFFECT_INTENSITY = 1 << 17,
	LRG_PART_LOOK_ALL           = (1 << 18) - 1
} LrgPartLookFields;

LRG_AVAILABLE_IN_ALL
GType lrg_part_look_fields_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PART_LOOK_FIELDS (lrg_part_look_fields_get_type ())

#define LRG_TYPE_PART_LOOK (lrg_part_look_get_type ())

/**
 * LrgPartLook:
 * @set: which of the fields below carry a value
 * @background: fill colour
 * @foreground: text / icon / mark colour
 * @border: border colour
 * @border_width: border thickness (drawn inside the part)
 * @radii: corner radii
 * @shadow: drop-shadow colour (alpha sets the strength)
 * @shadow_blur: drop-shadow softness
 * @shadow_offset_y: drop-shadow vertical offset
 * @gradient: gradient fill, used instead of @background when its kind
 *   is not %LRG_PAINT_GRADIENT_NONE
 * @opacity: opacity of the whole part, 0..1
 * @size: part-specific size (thumb diameter, check size, indicator
 *   thickness ...)
 * @glow: colour of a soft outer glow (a shadow with no offset)
 * @glow_size: glow radius
 * @material: surface material; anything but %LRG_PAINT_MATERIAL_NONE
 *   draws the part as glass or blur over what is behind it instead of
 *   the solid fill
 * @material_opacity: how strongly the material shows, 0..1; below 1
 *   the solid fill is cross-faded over it (this is how a look fades
 *   between a material and a plain fill)
 * @glass: glass parameters for the material; when not set the
 *   material's defaults apply (see lrg_part_look_get_glass())
 * @effect: quark of the effect name drawn over the fill (see
 *   lrg_paint_effect_lookup()), 0 for none
 * @effect_params: effect palette and intensity (hover, press, speed
 *   and scale are filled in when painting)
 *
 * The visual description of one widget part. A plain value struct.
 */
typedef struct
{
	LrgPartLookFields set;
	GrlColor          background;
	GrlColor          foreground;
	GrlColor          border;
	gfloat            border_width;
	LrgCornerRadii    radii;
	GrlColor          shadow;
	gfloat            shadow_blur;
	gfloat            shadow_offset_y;
	LrgPaintGradient  gradient;
	gfloat            opacity;
	gfloat            size;
	GrlColor          glow;
	gfloat            glow_size;
	LrgPaintMaterial  material;
	gfloat            material_opacity;
	LrgPaintGlass     glass;
	GQuark            effect;
	LrgPaintEffectParams effect_params;
} LrgPartLook;

LRG_AVAILABLE_IN_ALL
GType lrg_part_look_get_type (void) G_GNUC_CONST;

/**
 * lrg_part_look_new:
 *
 * Returns: (transfer full): a new, empty #LrgPartLook
 */
LRG_AVAILABLE_IN_ALL
LrgPartLook * lrg_part_look_new (void);

/**
 * lrg_part_look_copy:
 * @self: an #LrgPartLook
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPartLook * lrg_part_look_copy (const LrgPartLook *self);

/**
 * lrg_part_look_free:
 * @self: (nullable): an #LrgPartLook
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_free (LrgPartLook *self);

/**
 * lrg_part_look_init:
 * @self: (out caller-allocates): look to reset
 *
 * Clears every field: transparent colours, zero sizes, opacity 1, no
 * gradient, and @set empty.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_init (LrgPartLook *self);

/**
 * lrg_part_look_merge:
 * @self: look to update
 * @overlay: look whose set fields win
 *
 * Copies every field that is set in @overlay into @self and marks it
 * set there too.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_merge (LrgPartLook       *self,
                          const LrgPartLook *overlay);

/**
 * lrg_part_look_lerp:
 * @a: look at t = 0
 * @b: look at t = 1
 * @t: blend factor
 * @out: (out caller-allocates): blended look
 *
 * Blends two complete looks field by field: colours mix, sizes and
 * radii interpolate, and a gradient fades in from the solid
 * background. This is how state changes cross-fade while a widget's
 * hover and press springs move.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_lerp (const LrgPartLook *a,
                         const LrgPartLook *b,
                         gfloat             t,
                         LrgPartLook       *out);

/**
 * lrg_part_look_set_background:
 * @self: an #LrgPartLook
 * @color: fill colour
 *
 * Sets @background and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_background (LrgPartLook    *self,
                                   const GrlColor *color);

/**
 * lrg_part_look_set_foreground:
 * @self: an #LrgPartLook
 * @color: text / mark colour
 *
 * Sets @foreground and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_foreground (LrgPartLook    *self,
                                   const GrlColor *color);

/**
 * lrg_part_look_set_border:
 * @self: an #LrgPartLook
 * @color: border colour
 * @width: border width
 *
 * Sets @border and @border_width and marks both set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_border (LrgPartLook    *self,
                               const GrlColor *color,
                               gfloat          width);

/**
 * lrg_part_look_set_radius:
 * @self: an #LrgPartLook
 * @radius: radius for all four corners
 *
 * Sets @radii and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_radius (LrgPartLook *self,
                               gfloat       radius);

/**
 * lrg_part_look_set_shadow:
 * @self: an #LrgPartLook
 * @color: shadow colour
 * @blur: softness
 * @offset_y: vertical offset
 *
 * Sets the drop shadow and marks its fields set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_shadow (LrgPartLook    *self,
                               const GrlColor *color,
                               gfloat          blur,
                               gfloat          offset_y);

/**
 * lrg_part_look_set_gradient:
 * @self: an #LrgPartLook
 * @gradient: gradient fill
 *
 * Sets @gradient and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_gradient (LrgPartLook            *self,
                                 const LrgPaintGradient *gradient);

/**
 * lrg_part_look_set_opacity:
 * @self: an #LrgPartLook
 * @opacity: 0..1
 *
 * Sets @opacity and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_opacity (LrgPartLook *self,
                                gfloat       opacity);

/**
 * lrg_part_look_set_size:
 * @self: an #LrgPartLook
 * @size: part-specific size
 *
 * Sets @size and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_size (LrgPartLook *self,
                             gfloat       size);

/**
 * lrg_part_look_set_glow:
 * @self: an #LrgPartLook
 * @color: glow colour
 * @size: glow radius
 *
 * Sets the outer glow and marks its fields set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_glow (LrgPartLook    *self,
                             const GrlColor *color,
                             gfloat          size);

/**
 * lrg_part_look_set_material:
 * @self: an #LrgPartLook
 * @material: the surface material
 *
 * Sets @material (with @material_opacity 1) and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_material (LrgPartLook      *self,
                                 LrgPaintMaterial  material);

/**
 * lrg_part_look_set_glass:
 * @self: an #LrgPartLook
 * @glass: glass parameters
 *
 * Sets @glass and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_glass (LrgPartLook         *self,
                              const LrgPaintGlass *glass);

/**
 * lrg_part_look_get_glass:
 * @self: an #LrgPartLook
 * @out: (out caller-allocates): the glass the material draws with
 *
 * The glass parameters @self's material paints with: @glass when set,
 * else lrg_paint_glass_init_default(), adjusted by the material. BLUR
 * keeps only the blur (full frost, no refraction, rim or dispersion,
 * no tint unless @glass is set), FROSTED forces full frost, TINTED
 * forces full frost and uses @background as the tint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_get_glass (const LrgPartLook *self,
                              LrgPaintGlass     *out);

/**
 * lrg_part_look_set_effect:
 * @self: an #LrgPartLook
 * @name: (nullable): effect name ("aurora", a registered user effect
 *   ...), %NULL for none
 *
 * Sets @effect and marks it set. The name is resolved when painting,
 * so an effect registered later still works.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_effect (LrgPartLook *self,
                               const gchar *name);

/**
 * lrg_part_look_get_effect_name:
 * @self: an #LrgPartLook
 *
 * Returns: (nullable): the effect name, or %NULL for none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_part_look_get_effect_name (const LrgPartLook *self);

/**
 * lrg_part_look_set_effect_colors:
 * @self: an #LrgPartLook
 * @color1: first palette colour
 * @color2: (nullable): second colour, %NULL to repeat @color1
 * @color3: (nullable): third colour, %NULL to repeat @color2
 *
 * Sets the effect palette and marks it set. Without it the effect
 * paints with the part's background, the theme accent and the part's
 * foreground.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_effect_colors (LrgPartLook    *self,
                                      const GrlColor *color1,
                                      const GrlColor *color2,
                                      const GrlColor *color3);

/**
 * lrg_part_look_set_effect_intensity:
 * @self: an #LrgPartLook
 * @intensity: effect strength, 0..1 (1 when unset)
 *
 * Sets the effect intensity and marks it set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_part_look_set_effect_intensity (LrgPartLook *self,
                                         gfloat       intensity);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPartLook, lrg_part_look_free)

/* ==========================================================================
 * LrgStyleRule
 * ========================================================================== */

#define LRG_TYPE_STYLE_RULE (lrg_style_rule_get_type ())

typedef struct _LrgStyleRule LrgStyleRule;

LRG_AVAILABLE_IN_ALL
GType lrg_style_rule_get_type (void) G_GNUC_CONST;

/**
 * lrg_style_rule_new:
 * @widget_type: (nullable): type name the rule applies to, e.g.
 *   "LrgButton"; subclasses match too. %NULL matches every widget.
 * @style_class: (nullable): style class the widget must carry, %NULL
 *   for any
 * @part: the part the rule styles
 * @state: states that must all be active; %LRG_WIDGET_STATE_NONE
 *   matches every state
 *
 * Creates a rule with an empty look; fill it through
 * lrg_style_rule_get_look(). The type is stored by name and resolved
 * on first use, so rules can be loaded before the widget types are
 * registered.
 *
 * Returns: (transfer full): a new #LrgStyleRule
 */
LRG_AVAILABLE_IN_ALL
LrgStyleRule * lrg_style_rule_new (const gchar    *widget_type,
                                   const gchar    *style_class,
                                   LrgUIPart       part,
                                   LrgWidgetState  state);

/**
 * lrg_style_rule_ref:
 * @self: an #LrgStyleRule
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgStyleRule * lrg_style_rule_ref (LrgStyleRule *self);

/**
 * lrg_style_rule_unref:
 * @self: (transfer full): an #LrgStyleRule
 */
LRG_AVAILABLE_IN_ALL
void lrg_style_rule_unref (LrgStyleRule *self);

/**
 * lrg_style_rule_get_look:
 * @self: an #LrgStyleRule
 *
 * Returns: (transfer none): the look the rule applies; edit it in
 *   place before adding the rule to a theme
 */
LRG_AVAILABLE_IN_ALL
LrgPartLook * lrg_style_rule_get_look (LrgStyleRule *self);

/**
 * lrg_style_rule_get_widget_type:
 * @self: an #LrgStyleRule
 *
 * Returns: (nullable): the widget type name, or %NULL for any
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_style_rule_get_widget_type (LrgStyleRule *self);

/**
 * lrg_style_rule_get_style_class:
 * @self: an #LrgStyleRule
 *
 * Returns: (nullable): the style class, or %NULL for any
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_style_rule_get_style_class (LrgStyleRule *self);

/**
 * lrg_style_rule_get_part:
 * @self: an #LrgStyleRule
 *
 * Returns: the part the rule styles
 */
LRG_AVAILABLE_IN_ALL
LrgUIPart lrg_style_rule_get_part (LrgStyleRule *self);

/**
 * lrg_style_rule_get_state:
 * @self: an #LrgStyleRule
 *
 * Returns: the states the rule requires
 */
LRG_AVAILABLE_IN_ALL
LrgWidgetState lrg_style_rule_get_state (LrgStyleRule *self);

/**
 * lrg_style_rule_get_specificity:
 * @self: an #LrgStyleRule
 *
 * How specific the rule is: 1 for a widget type, 2 for a style class
 * and 2 per required state. Matching rules apply from least to most
 * specific; ties go to the rule added last.
 *
 * Returns: the specificity
 */
LRG_AVAILABLE_IN_ALL
guint lrg_style_rule_get_specificity (LrgStyleRule *self);

/**
 * lrg_style_rule_matches:
 * @self: an #LrgStyleRule
 * @widget_type: the widget's #GType
 * @classes: (array length=n_classes) (nullable): the widget's style
 *   classes as quarks
 * @n_classes: number of entries in @classes
 * @part: the part being styled
 * @state: the widget's current state
 *
 * Returns: %TRUE if the rule applies
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_style_rule_matches (LrgStyleRule   *self,
                                 GType           widget_type,
                                 const GQuark   *classes,
                                 guint           n_classes,
                                 LrgUIPart       part,
                                 LrgWidgetState  state);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgStyleRule, lrg_style_rule_unref)

/* ==========================================================================
 * Colour parsing
 * ========================================================================== */

/**
 * lrg_color_parse:
 * @text: "#rgb", "#rgba", "#rrggbb" or "#rrggbbaa" (the '#' is optional)
 * @out: (out caller-allocates): the parsed colour
 *
 * Parses a hex colour, as used in theme files.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_parse (const gchar *text,
                          GrlColor    *out);

/**
 * lrg_color_to_string:
 * @color: a colour
 *
 * Formats a colour as "#rrggbb", or "#rrggbbaa" when not opaque.
 *
 * Returns: (transfer full): the hex string
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_color_to_string (const GrlColor *color);

G_END_DECLS
