/* lrg-shape-sheet.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The standard shape sheet: one documented set of blend-shape names for
 * 2D characters (mouth visemes, eyes, brows, moods, body), so one clip,
 * one lip-sync track or one procedural layer drives a whole cast.  A
 * character implements whichever shapes it has; weights for the others
 * are ignored.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Shape names                                                         */
/* ------------------------------------------------------------------ */

/**
 * LRG_SHAPE_REST:
 *
 * Standard shape `rest`: mouth closed and relaxed; the neutral shape every viseme fades back to.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_REST "rest"

/**
 * LRG_SHAPE_A:
 *
 * Standard shape `A`: open mouth, jaw dropped ("father", "cat").
 *
 * Since: 0.14
 */
#define LRG_SHAPE_A "A"

/**
 * LRG_SHAPE_E:
 *
 * Standard shape `E`: mid-open mouth, lips spread ("bed", "say").
 *
 * Since: 0.14
 */
#define LRG_SHAPE_E "E"

/**
 * LRG_SHAPE_I:
 *
 * Standard shape `I`: narrow opening, lips spread wide, teeth showing ("see", "sit").
 *
 * Since: 0.14
 */
#define LRG_SHAPE_I "I"

/**
 * LRG_SHAPE_O:
 *
 * Standard shape `O`: rounded open lips ("go", "law").
 *
 * Since: 0.14
 */
#define LRG_SHAPE_O "O"

/**
 * LRG_SHAPE_U:
 *
 * Standard shape `U`: small rounded lips pushed forward ("boot", "book").
 *
 * Since: 0.14
 */
#define LRG_SHAPE_U "U"

/**
 * LRG_SHAPE_MBP:
 *
 * Standard shape `MBP`: lips pressed together (m, b, p).
 *
 * Since: 0.14
 */
#define LRG_SHAPE_MBP "MBP"

/**
 * LRG_SHAPE_FV:
 *
 * Standard shape `FV`: lower lip tucked under the upper teeth (f, v).
 *
 * Since: 0.14
 */
#define LRG_SHAPE_FV "FV"

/**
 * LRG_SHAPE_L:
 *
 * Standard shape `L`: mouth open, tongue tip up behind the upper teeth (l, th).
 *
 * Since: 0.14
 */
#define LRG_SHAPE_L "L"

/**
 * LRG_SHAPE_WQ:
 *
 * Standard shape `WQ`: small tight pucker (w, "queen").
 *
 * Since: 0.14
 */
#define LRG_SHAPE_WQ "WQ"

/**
 * LRG_SHAPE_CDGKNRSTYZ:
 *
 * Standard shape `CDGKNRSTYZ`: teeth almost closed, lips relaxed (most consonants).
 *
 * Since: 0.14
 */
#define LRG_SHAPE_CDGKNRSTYZ "CDGKNRSTYZ"

/**
 * LRG_SHAPE_BLINK_L:
 *
 * Standard shape `blink_l`: left eye closed.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_BLINK_L "blink_l"

/**
 * LRG_SHAPE_BLINK_R:
 *
 * Standard shape `blink_r`: right eye closed.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_BLINK_R "blink_r"

/**
 * LRG_SHAPE_SQUINT:
 *
 * Standard shape `squint`: both eyes narrowed, lower lids raised.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_SQUINT "squint"

/**
 * LRG_SHAPE_WIDE:
 *
 * Standard shape `wide`: both eyes opened wide, whites showing.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_WIDE "wide"

/**
 * LRG_SHAPE_LOOK_LEFT:
 *
 * Standard shape `look_left`: pupils moved to the character's left.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_LOOK_LEFT "look_left"

/**
 * LRG_SHAPE_LOOK_RIGHT:
 *
 * Standard shape `look_right`: pupils moved to the character's right.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_LOOK_RIGHT "look_right"

/**
 * LRG_SHAPE_LOOK_UP:
 *
 * Standard shape `look_up`: pupils moved up.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_LOOK_UP "look_up"

/**
 * LRG_SHAPE_LOOK_DOWN:
 *
 * Standard shape `look_down`: pupils moved down.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_LOOK_DOWN "look_down"

/**
 * LRG_SHAPE_BROW_UP:
 *
 * Standard shape `brow_up`: both brows raised.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_BROW_UP "brow_up"

/**
 * LRG_SHAPE_BROW_DOWN:
 *
 * Standard shape `brow_down`: both brows lowered.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_BROW_DOWN "brow_down"

/**
 * LRG_SHAPE_BROW_ANGRY:
 *
 * Standard shape `brow_angry`: inner ends of the brows pulled down and together.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_BROW_ANGRY "brow_angry"

/**
 * LRG_SHAPE_BROW_WORRIED:
 *
 * Standard shape `brow_worried`: inner ends of the brows raised.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_BROW_WORRIED "brow_worried"

/**
 * LRG_SHAPE_HAPPY:
 *
 * Standard shape `happy`: smile: mouth corners up, cheeks raised.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_HAPPY "happy"

/**
 * LRG_SHAPE_SAD:
 *
 * Standard shape `sad`: mouth corners down, lower lip pushed up.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_SAD "sad"

/**
 * LRG_SHAPE_ANGRY:
 *
 * Standard shape `angry`: lips tight, mouth corners pulled down and out.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_ANGRY "angry"

/**
 * LRG_SHAPE_SURPRISED:
 *
 * Standard shape `surprised`: mouth dropped open in an oval.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_SURPRISED "surprised"

/**
 * LRG_SHAPE_SHOUT:
 *
 * Standard shape `shout`: mouth wide open and stretched, teeth showing.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_SHOUT "shout"

/**
 * LRG_SHAPE_SQUASH:
 *
 * Standard shape `squash`: the body squashed down and widened (landing, impact).
 *
 * Since: 0.14
 */
#define LRG_SHAPE_SQUASH "squash"

/**
 * LRG_SHAPE_STRETCH:
 *
 * Standard shape `stretch`: the body stretched up and narrowed (jump, anticipation).
 *
 * Since: 0.14
 */
#define LRG_SHAPE_STRETCH "stretch"

/**
 * LRG_SHAPE_BREATHE:
 *
 * Standard shape `breathe`: chest and shoulders at the top of a breath in.
 *
 * Since: 0.14
 */
#define LRG_SHAPE_BREATHE "breathe"

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

/**
 * lrg_viseme_get_shape_name:
 * @viseme: a viseme
 *
 * The blend-shape name a viseme drives (`LRG_VISEME_MBP` drives
 * %LRG_SHAPE_MBP, "MBP").
 *
 * Returns: (nullable): the shape name, or %NULL for a value outside the
 *   enumeration
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_viseme_get_shape_name (LrgViseme viseme);

/**
 * lrg_viseme_from_shape_name:
 * @name: a shape name
 * @viseme: (out) (optional): the viseme
 *
 * The inverse of lrg_viseme_get_shape_name().  Names match exactly
 * (visemes are upper case, "rest" is lower case).
 *
 * Returns: %TRUE if @name is a viseme of the standard sheet
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viseme_from_shape_name (const gchar *name,
                                     LrgViseme   *viseme);

/**
 * lrg_shape_sheet_list:
 * @category: a category of the sheet
 *
 * The standard shape names of one category in sheet order.
 *
 * Returns: (transfer none) (array zero-terminated=1): the names (static
 *   strings), empty for a value outside the enumeration
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_shape_sheet_list (LrgShapeCategory category);

/**
 * lrg_shape_sheet_list_all:
 *
 * Every standard shape name in sheet order (mouth, eyes, brows, moods,
 * body).
 *
 * Returns: (transfer none) (array zero-terminated=1): the names (static
 *   strings)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_shape_sheet_list_all (void);

/**
 * lrg_shape_sheet_lookup:
 * @name: a shape name
 * @category: (out) (optional): its category
 *
 * Tells whether @name is on the standard sheet (exact match).
 *
 * Returns: %TRUE if it is
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shape_sheet_lookup (const gchar      *name,
                                 LrgShapeCategory *category);

G_END_DECLS
