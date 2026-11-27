/* lrg-shape-sheet.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The standard shape sheet: static name tables per category and the
 * viseme <-> shape name mapping.
 */

#include "config.h"

#include "animation/lrg-shape-sheet.h"

/* One NULL-terminated table per category, in sheet order; sheet_all is
 * their concatenation.  The visemes are in LrgViseme order so the enum
 * value indexes sheet_mouth directly. */
static const gchar * const sheet_mouth[] = {
	LRG_SHAPE_REST,
	LRG_SHAPE_A,
	LRG_SHAPE_E,
	LRG_SHAPE_I,
	LRG_SHAPE_O,
	LRG_SHAPE_U,
	LRG_SHAPE_MBP,
	LRG_SHAPE_FV,
	LRG_SHAPE_L,
	LRG_SHAPE_WQ,
	LRG_SHAPE_CDGKNRSTYZ,
	NULL
};

static const gchar * const sheet_eyes[] = {
	LRG_SHAPE_BLINK_L,
	LRG_SHAPE_BLINK_R,
	LRG_SHAPE_SQUINT,
	LRG_SHAPE_WIDE,
	LRG_SHAPE_LOOK_LEFT,
	LRG_SHAPE_LOOK_RIGHT,
	LRG_SHAPE_LOOK_UP,
	LRG_SHAPE_LOOK_DOWN,
	NULL
};

static const gchar * const sheet_brows[] = {
	LRG_SHAPE_BROW_UP,
	LRG_SHAPE_BROW_DOWN,
	LRG_SHAPE_BROW_ANGRY,
	LRG_SHAPE_BROW_WORRIED,
	NULL
};

static const gchar * const sheet_mood[] = {
	LRG_SHAPE_HAPPY,
	LRG_SHAPE_SAD,
	LRG_SHAPE_ANGRY,
	LRG_SHAPE_SURPRISED,
	LRG_SHAPE_SHOUT,
	NULL
};

static const gchar * const sheet_body[] = {
	LRG_SHAPE_SQUASH,
	LRG_SHAPE_STRETCH,
	LRG_SHAPE_BREATHE,
	NULL
};

static const gchar * const sheet_all[] = {
	LRG_SHAPE_REST,
	LRG_SHAPE_A,
	LRG_SHAPE_E,
	LRG_SHAPE_I,
	LRG_SHAPE_O,
	LRG_SHAPE_U,
	LRG_SHAPE_MBP,
	LRG_SHAPE_FV,
	LRG_SHAPE_L,
	LRG_SHAPE_WQ,
	LRG_SHAPE_CDGKNRSTYZ,
	LRG_SHAPE_BLINK_L,
	LRG_SHAPE_BLINK_R,
	LRG_SHAPE_SQUINT,
	LRG_SHAPE_WIDE,
	LRG_SHAPE_LOOK_LEFT,
	LRG_SHAPE_LOOK_RIGHT,
	LRG_SHAPE_LOOK_UP,
	LRG_SHAPE_LOOK_DOWN,
	LRG_SHAPE_BROW_UP,
	LRG_SHAPE_BROW_DOWN,
	LRG_SHAPE_BROW_ANGRY,
	LRG_SHAPE_BROW_WORRIED,
	LRG_SHAPE_HAPPY,
	LRG_SHAPE_SAD,
	LRG_SHAPE_ANGRY,
	LRG_SHAPE_SURPRISED,
	LRG_SHAPE_SHOUT,
	LRG_SHAPE_SQUASH,
	LRG_SHAPE_STRETCH,
	LRG_SHAPE_BREATHE,
	NULL
};

/* the tables indexed by LrgShapeCategory */
static const gchar * const * const sheet_by_category[] = {
	sheet_mouth,
	sheet_eyes,
	sheet_brows,
	sheet_mood,
	sheet_body
};

static const gchar * const sheet_empty[] = { NULL };

const gchar *
lrg_viseme_get_shape_name (LrgViseme viseme)
{
	/* LrgViseme follows the mouth table's order (the last entry is the
	 * NULL terminator) */
	if ((guint)viseme >= G_N_ELEMENTS (sheet_mouth) - 1)
		return NULL;
	return sheet_mouth[viseme];
}

gboolean
lrg_viseme_from_shape_name (const gchar *name,
                            LrgViseme   *viseme)
{
	guint i;

	for (i = 0; name != NULL && sheet_mouth[i] != NULL; i++)
	{
		if (g_str_equal (name, sheet_mouth[i]))
		{
			if (viseme != NULL)
				*viseme = (LrgViseme)i;
			return TRUE;
		}
	}
	return FALSE;
}

const gchar * const *
lrg_shape_sheet_list (LrgShapeCategory category)
{
	if ((guint)category >= G_N_ELEMENTS (sheet_by_category))
		return sheet_empty;
	return sheet_by_category[category];
}

const gchar * const *
lrg_shape_sheet_list_all (void)
{
	return sheet_all;
}

gboolean
lrg_shape_sheet_lookup (const gchar      *name,
                        LrgShapeCategory *category)
{
	guint c;
	guint i;

	if (name == NULL)
		return FALSE;
	for (c = 0; c < G_N_ELEMENTS (sheet_by_category); c++)
	{
		for (i = 0; sheet_by_category[c][i] != NULL; i++)
		{
			if (g_str_equal (name, sheet_by_category[c][i]))
			{
				if (category != NULL)
					*category = (LrgShapeCategory)c;
				return TRUE;
			}
		}
	}
	return FALSE;
}
