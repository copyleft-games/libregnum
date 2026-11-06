/* lrg-rating.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRating: A row of stars (or other shapes) for a 0..N rating, with halves.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/**
 * LrgRatingShape:
 * @LRG_RATING_SHAPE_STAR: five-pointed stars (default), filled with the
 *   theme's WARNING colour
 * @LRG_RATING_SHAPE_HEART: hearts, filled with the theme's DANGER colour
 * @LRG_RATING_SHAPE_CIRCLE: dots, filled with the theme's ACCENT colour
 *
 * The symbol an #LrgRating repeats.
 */
typedef enum
{
	LRG_RATING_SHAPE_STAR,
	LRG_RATING_SHAPE_HEART,
	LRG_RATING_SHAPE_CIRCLE
} LrgRatingShape;

#define LRG_TYPE_RATING_SHAPE (lrg_rating_shape_get_type ())

/**
 * lrg_rating_shape_get_type:
 *
 * Returns: the #GType of #LrgRatingShape
 */
LRG_AVAILABLE_IN_ALL
GType lrg_rating_shape_get_type (void) G_GNUC_CONST;

#define LRG_TYPE_RATING (lrg_rating_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRating, lrg_rating, LRG, RATING, LrgWidget)

/**
 * lrg_rating_new:
 *
 * Creates a five-star rating with value 0.
 *
 * Returns: (transfer full): a new #LrgRating
 */
LRG_AVAILABLE_IN_ALL
LrgRating * lrg_rating_new (void);

/**
 * lrg_rating_get_value:
 * @self: an #LrgRating
 *
 * Returns: the rating, 0 to #LrgRating:max
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_rating_get_value (LrgRating *self);

/**
 * lrg_rating_set_value:
 * @self: an #LrgRating
 * @value: the new rating
 *
 * Sets the rating. The value is clamped to 0..#LrgRating:max and
 * rounded to whole symbols, or to halves when #LrgRating:allow-half is
 * set. Emits #LrgRating::value-changed when it changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rating_set_value (LrgRating *self,
                           gdouble    value);

/**
 * lrg_rating_get_max:
 * @self: an #LrgRating
 *
 * Returns: the number of symbols
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rating_get_max (LrgRating *self);

/**
 * lrg_rating_set_max:
 * @self: an #LrgRating
 * @max: number of symbols, at least 1
 *
 * Sets how many symbols are shown; the value is clamped to fit.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rating_set_max (LrgRating *self,
                         guint      max);

/**
 * lrg_rating_get_allow_half:
 * @self: an #LrgRating
 *
 * Returns: whether half symbols can be picked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rating_get_allow_half (LrgRating *self);

/**
 * lrg_rating_set_allow_half:
 * @self: an #LrgRating
 * @allow_half: whether half symbols can be picked
 *
 * With halves, clicking the left half of a symbol picks n - 0.5 and
 * the keyboard steps by 0.5.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rating_set_allow_half (LrgRating *self,
                                gboolean   allow_half);

/**
 * lrg_rating_get_read_only:
 * @self: an #LrgRating
 *
 * Returns: whether the rating ignores input
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rating_get_read_only (LrgRating *self);

/**
 * lrg_rating_set_read_only:
 * @self: an #LrgRating
 * @read_only: %TRUE to display only
 *
 * A read-only rating is not focusable, shows no hover preview and
 * ignores clicks and keys.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rating_set_read_only (LrgRating *self,
                               gboolean   read_only);

/**
 * lrg_rating_get_shape:
 * @self: an #LrgRating
 *
 * Returns: the symbol shape
 */
LRG_AVAILABLE_IN_ALL
LrgRatingShape lrg_rating_get_shape (LrgRating *self);

/**
 * lrg_rating_set_shape:
 * @self: an #LrgRating
 * @shape: the symbol shape
 *
 * Sets the symbol drawn for each step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rating_set_shape (LrgRating      *self,
                           LrgRatingShape  shape);

/**
 * lrg_rating_get_symbol_size:
 * @self: an #LrgRating
 *
 * Returns: the explicit symbol size, or 0 when it follows the theme
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rating_get_symbol_size (LrgRating *self);

/**
 * lrg_rating_set_symbol_size:
 * @self: an #LrgRating
 * @size: edge length of one symbol, 0 to follow the theme
 *   (CONTROL_HEIGHT x 0.75)
 *
 * Sets the size of each symbol.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rating_set_symbol_size (LrgRating *self,
                                 gfloat     size);

/**
 * lrg_rating_get_fill_color:
 * @self: an #LrgRating
 *
 * Returns: (nullable) (transfer none): the explicit fill colour, or
 *   %NULL when the theme colour for the shape is used
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_rating_get_fill_color (LrgRating *self);

/**
 * lrg_rating_set_fill_color:
 * @self: an #LrgRating
 * @color: (nullable): fill colour of set symbols, %NULL for the theme's
 *
 * Overrides the MARK part's colour. Explicit colours beat theme style
 * rules.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rating_set_fill_color (LrgRating      *self,
                                const GrlColor *color);

/**
 * lrg_rating_get_hover_value:
 * @self: an #LrgRating
 *
 * The value the pointer is previewing, as painted over the symbols.
 *
 * Returns: the previewed value, or -1 when the pointer is not over it
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_rating_get_hover_value (LrgRating *self);

G_END_DECLS
