/* lrg-game-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-game-view.h"

typedef struct
{
	const gchar *name;
	gint         width;
	gint         height;
} Preset;

static const Preset presets[] = {
	{ "1920x1080", 1920, 1080 },
	{ "16:9",      1920, 1080 },
	{ "1280x720",  1280,  720 },
	{ "4:3",       1024,  768 },
	{ "21:9",      2560, 1080 },
	{ "1:1",       1080, 1080 },
	{ "steam-deck", 1280, 800 }
};

guint
lrg_game_view_preset_count (void)
{
	return G_N_ELEMENTS (presets);
}

gboolean
lrg_game_view_preset_get (guint         index,
                          const gchar **name,
                          gint         *width,
                          gint         *height)
{
	if (index >= G_N_ELEMENTS (presets))
		return FALSE;
	if (name != NULL)
		*name = presets[index].name;
	if (width != NULL)
		*width = presets[index].width;
	if (height != NULL)
		*height = presets[index].height;
	return TRUE;
}

gboolean
lrg_game_view_preset_find (const gchar *name,
                           gint        *width,
                           gint        *height)
{
	guint i;

	g_return_val_if_fail (name != NULL, FALSE);
	for (i = 0; i < G_N_ELEMENTS (presets); i++)
	{
		if (g_strcmp0 (presets[i].name, name) == 0)
			return lrg_game_view_preset_get (i, NULL, width, height);
	}
	return FALSE;
}

void
lrg_game_view_letterbox (const gchar *preset,
                         gint         view_w,
                         gint         view_h,
                         gint        *x,
                         gint        *y,
                         gint        *w,
                         gint        *h)
{
	gint   pw = 0;
	gint   ph = 0;
	gfloat sx;
	gfloat sy;
	gfloat scale;
	gint   fw;
	gint   fh;

	if (x != NULL)
		*x = 0;
	if (y != NULL)
		*y = 0;
	if (w != NULL)
		*w = view_w;
	if (h != NULL)
		*h = view_h;

	if (preset == NULL ||
	    !lrg_game_view_preset_find (preset, &pw, &ph) ||
	    pw <= 0 || ph <= 0 || view_w <= 0 || view_h <= 0)
		return;

	sx = (gfloat) view_w / (gfloat) pw;
	sy = (gfloat) view_h / (gfloat) ph;
	scale = (sx < sy) ? sx : sy;
	fw = (gint) ((gfloat) pw * scale);
	fh = (gint) ((gfloat) ph * scale);
	if (x != NULL)
		*x = (view_w - fw) / 2;
	if (y != NULL)
		*y = (view_h - fh) / 2;
	if (w != NULL)
		*w = fw;
	if (h != NULL)
		*h = fh;
}
