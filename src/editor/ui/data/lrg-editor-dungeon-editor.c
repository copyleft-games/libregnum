/* lrg-editor-dungeon-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The procgen dungeon editor: parameter form | map preview.
 *
 * The form's rows are built from the recipe format (generator, size,
 * seed, post-processing, spawn rules) plus the writable properties of the
 * generator class named by `generator:`, cloned into rows named
 * "param-<property>" so they never clash with the post rows (caves has
 * its own `connect` and `keep-largest`). The rows are rebuilt when the
 * generator or the number of spawn rules changes, and only refreshed
 * otherwise, so typing in a field keeps focus.
 *
 * Generation runs the engine's loader and pipeline on the serialized
 * document, so the preview is exactly the map a game gets for the seed.
 * Automatic regeneration is debounced on the canvas clock (the preview
 * widget's step function), so dragging a spin box does not generate a map
 * per frame.
 */

#include "config.h"

#define LRG_EDITOR_DATA_INTERNAL
#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-dungeon-editor.h"
#include "lrg-editor-hud-layout-editor.h"
#include "../lrg-editor-console-panel.h"
#include "../../../procgen/lrg-procgen-dungeon.h"
#include "../../../procgen/lrg-procgen-generator.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-canvas.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../ui/lrg-theme.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-icons.h"
#include "../../../lrg-enums.h"
#include <math.h>
#include <string.h>

#define AUTO_DELAY      (0.25f)    /* seconds of quiet before regenerating */
#define PREVIEW_PAD     (14.0f)
#define FOOTER_H        (26.0f)

static const gchar *generator_names = "enum-strings:bsp,caves,wfc,drunkard,noise";

struct _LrgEditorDungeonEditor
{
	LrgEditorDataEditor  parent_instance;

	/* Widgets */
	LrgSplitter         *split;
	LrgPropertyGrid     *form;
	LrgWidget           *preview;        /* LrgCustomWidget */
	LrgWidget           *auto_chip;

	/* Form shape */
	gchar               *shown_generator;
	gint                 shown_spawns;

	/* Map */
	LrgProcgenGrid      *grid;
	gchar               *error;
	guint64              grid_seed;
	gboolean             auto_generate;
	gboolean             pending;
	gfloat               pending_time;
	gint                 hover_x;        /* hovered cell, -1 none */
	gint                 hover_y;
};

G_DEFINE_FINAL_TYPE (LrgEditorDungeonEditor, lrg_editor_dungeon_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static LrgEditorDocument *
doc_of (LrgEditorDungeonEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static const gchar *
doc_generator (LrgEditorDungeonEditor *self)
{
	const gchar *name = lrg_editor_document_get_scalar (doc_of (self), "/generator");

	return name != NULL ? name : "bsp";
}

static gint
doc_spawns (LrgEditorDungeonEditor *self)
{
	return lrg_editor_document_get_kind (doc_of (self), "/spawns") == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE
	       ? lrg_editor_document_get_length (doc_of (self), "/spawns") : 0;
}

/* --------------------------------------------------------------------------
 * Form
 * -------------------------------------------------------------------------- */

/* A copy of a generator property under another name (label = the
 * property's own name), or NULL for a type the form does not edit. */
static GParamSpec *
clone_pspec (GParamSpec  *pspec,
             const gchar *name)
{
	const gchar      *nick = g_param_spec_get_name (pspec);
	const gchar      *blurb = g_param_spec_get_blurb (pspec);
	const GParamFlags flags = G_PARAM_READWRITE;

	if (G_IS_PARAM_SPEC_UINT (pspec))
	{
		GParamSpecUInt *p = G_PARAM_SPEC_UINT (pspec);

		return g_param_spec_uint (name, nick, blurb, p->minimum, p->maximum, p->default_value, flags);
	}
	if (G_IS_PARAM_SPEC_INT (pspec))
	{
		GParamSpecInt *p = G_PARAM_SPEC_INT (pspec);

		return g_param_spec_int (name, nick, blurb, p->minimum, p->maximum, p->default_value, flags);
	}
	if (G_IS_PARAM_SPEC_INT64 (pspec))
	{
		GParamSpecInt64 *p = G_PARAM_SPEC_INT64 (pspec);

		return g_param_spec_int64 (name, nick, blurb, p->minimum, p->maximum, p->default_value, flags);
	}
	if (G_IS_PARAM_SPEC_UINT64 (pspec))
	{
		GParamSpecUInt64 *p = G_PARAM_SPEC_UINT64 (pspec);

		return g_param_spec_uint64 (name, nick, blurb, p->minimum, p->maximum, p->default_value, flags);
	}
	if (G_IS_PARAM_SPEC_DOUBLE (pspec))
	{
		GParamSpecDouble *p = G_PARAM_SPEC_DOUBLE (pspec);

		return g_param_spec_double (name, nick, blurb, p->minimum, p->maximum, p->default_value, flags);
	}
	if (G_IS_PARAM_SPEC_FLOAT (pspec))
	{
		GParamSpecFloat *p = G_PARAM_SPEC_FLOAT (pspec);

		return g_param_spec_double (name, nick, blurb, p->minimum, p->maximum, p->default_value, flags);
	}
	if (G_IS_PARAM_SPEC_BOOLEAN (pspec))
		return g_param_spec_boolean (name, nick, blurb, G_PARAM_SPEC_BOOLEAN (pspec)->default_value, flags);
	if (G_IS_PARAM_SPEC_STRING (pspec))
		return g_param_spec_string (name, nick, blurb, G_PARAM_SPEC_STRING (pspec)->default_value, flags);
	if (G_IS_PARAM_SPEC_ENUM (pspec))
		return g_param_spec_enum (name, nick, blurb, G_PARAM_SPEC_VALUE_TYPE (pspec),
		                          G_PARAM_SPEC_ENUM (pspec)->default_value, flags);
	return NULL;
}

/* The generator's own parameters, as rows under `params:`. */
static void
add_generator_rows (LrgEditorDungeonEditor *self,
                    EditorDataFields       *fields)
{
	GType             type = lrg_procgen_generator_type_from_name (doc_generator (self));
	GObjectClass     *klass;
	GParamSpec      **pspecs;
	guint             n, i;
	g_autofree gchar *group = NULL;

	if (type == G_TYPE_INVALID || G_TYPE_IS_ABSTRACT (type))
		return;
	klass = g_type_class_ref (type);
	pspecs = g_object_class_list_properties (klass, &n);
	group = g_strdup_printf ("Generator: %s", lrg_procgen_generator_type_to_name (type));
	for (i = 0; i < n; i++)
	{
		GParamSpec       *pspec = pspecs[i];
		g_autofree gchar *name = NULL;
		g_autofree gchar *path = NULL;
		GParamSpec       *row;

		/* width / height are top-level keys; skip read-only and construct-only */
		if (pspec->owner_type == LRG_TYPE_PROCGEN_GENERATOR || !(pspec->flags & G_PARAM_WRITABLE) ||
		    (pspec->flags & G_PARAM_CONSTRUCT_ONLY))
			continue;
		name = g_strconcat ("param-", g_param_spec_get_name (pspec), NULL);
		row = clone_pspec (pspec, name);
		if (row == NULL)
			continue;
		path = lrg_editor_document_path_join ("/params", g_param_spec_get_name (pspec));
		editor_data_fields_add (fields, row, path, group, NULL);
	}
	g_free (pspecs);
	g_type_class_unref (klass);
}

static void
rebuild_form (LrgEditorDungeonEditor *self)
{
	g_autoptr(EditorDataFields) fields = editor_data_fields_new (LRG_EDITOR_DATA_EDITOR (self));
	const GParamFlags           flags = G_PARAM_READWRITE;
	gint                        n, i;

	if (self->form == NULL)
		return;

	/* --- recipe --- */
	editor_data_fields_add (fields, g_param_spec_string ("generator", "Generator", NULL, "bsp", flags),
	                        "/generator", "Recipe", generator_names);
	editor_data_fields_add (fields, g_param_spec_int ("width", "Width", "Cells", 1, 4096, 64, flags), "/width",
	                        "Recipe", NULL);
	editor_data_fields_add (fields, g_param_spec_int ("height", "Height", "Cells", 1, 4096, 48, flags), "/height",
	                        "Recipe", NULL);
	editor_data_fields_add (fields, g_param_spec_uint64 ("seed", "Seed", "The recipe's default seed", 0, G_MAXUINT64,
	                                                     0, flags),
	                        "/seed", "Recipe", NULL);

	/* --- generator parameters --- */
	add_generator_rows (self, fields);

	/* --- post-processing --- */
	editor_data_fields_add (fields, g_param_spec_int ("post-cull-min-size", "Cull regions under", "0 = off", 0,
	                                                  1000000, 0, flags),
	                        "/post/cull-min-size", "Post-processing", NULL);
	editor_data_fields_add (fields, g_param_spec_boolean ("post-keep-largest", "Keep largest region", NULL, FALSE,
	                                                      flags),
	                        "/post/keep-largest", "Post-processing", NULL);
	editor_data_fields_add (fields, g_param_spec_boolean ("post-connect", "Connect regions", NULL, TRUE, flags),
	                        "/post/connect", "Post-processing", NULL);
	editor_data_fields_add (fields, g_param_spec_boolean ("post-start-exit", "Place start and exit", NULL, TRUE, flags),
	                        "/post/start-exit", "Post-processing", NULL);

	/* --- spawn rules --- */
	n = doc_spawns (self);
	for (i = 0; i < n; i++)
	{
		static const struct { const gchar *key; const gchar *label; } keys[] = {
			{ "kind", "Kind" }, { "count", "Count" }, { "min-distance", "Min distance" }, { "rooms-only", "Rooms only" }
		};
		g_autofree gchar *group = g_strdup_printf ("Spawn %d", i + 1);
		guint             k;

		for (k = 0; k < G_N_ELEMENTS (keys); k++)
		{
			g_autofree gchar *name = g_strdup_printf ("spawn-%d-%s", i + 1, keys[k].key);
			g_autofree gchar *path = g_strdup_printf ("/spawns/%d/%s", i, keys[k].key);
			GParamSpec       *pspec;

			if (k == 0)
				pspec = g_param_spec_string (name, keys[k].label, NULL, NULL, flags);
			else if (k == 3)
				pspec = g_param_spec_boolean (name, keys[k].label, NULL, FALSE, flags);
			else
				pspec = g_param_spec_int (name, keys[k].label, NULL, 0, 100000, k == 1 ? 1 : 0, flags);
			editor_data_fields_add (fields, pspec, path, group, NULL);
		}
	}

	g_free (self->shown_generator);
	self->shown_generator = g_strdup (doc_generator (self));
	self->shown_spawns = n;
	editor_data_fields_attach (fields, self->form);
}

/* --------------------------------------------------------------------------
 * Generation
 * -------------------------------------------------------------------------- */

static guint64
doc_seed (LrgEditorDungeonEditor *self)
{
	const gchar *text = lrg_editor_document_get_scalar (doc_of (self), "/seed");

	return text != NULL ? g_ascii_strtoull (text, NULL, 10) : 0;
}

static gboolean
generate (LrgEditorDungeonEditor  *self,
          GError                 **error)
{
	g_autofree gchar             *data = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(LrgProcgenDungeon)  dungeon = NULL;
	LrgProcgenGrid               *grid;
	GError                       *local = NULL;
	guint64                       seed = doc_seed (self);

	self->pending = FALSE;
	self->pending_time = 0.0f;
	g_clear_pointer (&self->error, g_free);

	dungeon = lrg_procgen_dungeon_new_from_data (data, -1, &local);
	grid = dungeon != NULL ? lrg_procgen_dungeon_generate (dungeon, seed, &local) : NULL;
	if (grid == NULL)
	{
		self->error = g_strdup (local != NULL ? local->message : "the recipe did not generate");
		if (local != NULL)
			g_propagate_error (error, local);
		return FALSE;
	}
	lrg_procgen_grid_label_regions (grid);
	g_clear_object (&self->grid);
	self->grid = grid;
	self->grid_seed = seed;
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Preview
 * -------------------------------------------------------------------------- */

/* @top blended over @base by @alpha, opaque (no seams between runs). */
static GrlColor
blend (const GrlColor *base,
       const GrlColor *top,
       gfloat          alpha)
{
	GrlColor out;

	out.r = (guint8) ((gfloat) base->r + ((gfloat) top->r - (gfloat) base->r) * alpha);
	out.g = (guint8) ((gfloat) base->g + ((gfloat) top->g - (gfloat) base->g) * alpha);
	out.b = (guint8) ((gfloat) base->b + ((gfloat) top->b - (gfloat) base->b) * alpha);
	out.a = 255;
	return out;
}

/* The colour of a cell tag (NULL: draw nothing). */
static const GrlColor *
tag_color (LrgTheme *theme,
           guint8    tag,
           GrlColor *scratch)
{
	switch (tag)
	{
	case LRG_PROCGEN_CELL_WALL:
		return lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE);
	case LRG_PROCGEN_CELL_FLOOR:
		*scratch = blend (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.30f);
		return scratch;
	case LRG_PROCGEN_CELL_CORRIDOR:
		*scratch = blend (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.55f);
		return scratch;
	case LRG_PROCGEN_CELL_DOOR:
		return lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	case LRG_PROCGEN_CELL_WATER:
		*scratch = blend (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK), 0.75f);
		return scratch;
	case LRG_PROCGEN_CELL_VOID:
		return NULL;
	default:
		*scratch = blend (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS), 0.6f);
		return scratch;
	}
}

static const gchar *
tag_name (guint8 tag)
{
	static const gchar *names[] = { "wall", "floor", "corridor", "door", "water", "void" };

	return tag < G_N_ELEMENTS (names) ? names[tag] : "custom";
}

/* Map cell size and origin inside the widget (square cells, centred). */
static void
map_geometry (LrgEditorDungeonEditor *self,
              gfloat                  x,
              gfloat                  y,
              gfloat                  width,
              gfloat                  height,
              gfloat                 *cell,
              gfloat                 *ox,
              gfloat                 *oy)
{
	guint gw = lrg_procgen_grid_get_width (self->grid);
	guint gh = lrg_procgen_grid_get_height (self->grid);
	gfloat aw = MAX (1.0f, width - PREVIEW_PAD * 2.0f);
	gfloat ah = MAX (1.0f, height - PREVIEW_PAD * 2.0f - FOOTER_H);

	*cell = MAX (0.25f, MIN (aw / (gfloat) MAX (gw, 1u), ah / (gfloat) MAX (gh, 1u)));
	*ox = x + (width - *cell * (gfloat) gw) * 0.5f;
	*oy = y + PREVIEW_PAD + (ah - *cell * (gfloat) gh) * 0.5f;
}

static void
preview_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (user_data);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                *font = lrg_theme_get_ui_font (theme);
	gfloat                  fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                  radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	const GrlColor         *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor         *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	static const LrgThemeColor spawn_roles[] = {
		LRG_THEME_COLOR_DANGER, LRG_THEME_COLOR_WARNING, LRG_THEME_COLOR_LINK, LRG_THEME_COLOR_SUCCESS,
		LRG_THEME_COLOR_ACCENT_HOVER
	};
	g_autofree gchar *stats = NULL;
	gfloat            cell, ox, oy;
	guint             gw, gh, cx, cy, i;
	gint              sx, sy;

	lrg_paint_list_rect (list, x, y, width, height, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	if (self->grid == NULL)
	{
		lrg_paint_list_text (list, font, self->error != NULL ? self->error : "Press Generate", x + PREVIEW_PAD,
		                     y + PREVIEW_PAD, fs, 0.0f, muted);
		return;
	}

	gw = lrg_procgen_grid_get_width (self->grid);
	gh = lrg_procgen_grid_get_height (self->grid);
	map_geometry (self, x, y, width, height, &cell, &ox, &oy);
	lrg_paint_list_rect (list, ox - 4.0f, oy - 4.0f, cell * (gfloat) gw + 8.0f, cell * (gfloat) gh + 8.0f, 4.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW));
	/* walls are the map's ground: one rectangle, so no seams between runs */
	lrg_paint_list_rect (list, ox, oy, cell * (gfloat) gw, cell * (gfloat) gh, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));

	/* --- cells, one rectangle per run of equal tags in a row --- */
	for (cy = 0; cy < gh; cy++)
	{
		cx = 0;
		while (cx < gw)
		{
			guint8          tag = lrg_procgen_grid_get_cell (self->grid, (gint) cx, (gint) cy);
			guint           run = 1;
			GrlColor        scratch;
			const GrlColor *color;

			while (cx + run < gw && lrg_procgen_grid_get_cell (self->grid, (gint) (cx + run), (gint) cy) == tag)
				run++;
			color = tag != LRG_PROCGEN_CELL_WALL ? tag_color (theme, tag, &scratch) : NULL;
			if (color != NULL)
			{
				/* whole pixels, so neighbouring rows meet without seams */
				gfloat x0 = floorf (ox + (gfloat) cx * cell);
				gfloat x1 = floorf (ox + (gfloat) (cx + run) * cell);
				gfloat y0 = floorf (oy + (gfloat) cy * cell);
				gfloat y1 = floorf (oy + (gfloat) (cy + 1) * cell);

				lrg_paint_list_rect (list, x0, y0, MAX (1.0f, x1 - x0), MAX (1.0f, y1 - y0), 0.0f, color);
			}
			cx += run;
		}
	}

	/* --- rooms --- */
	for (i = 0; i < lrg_procgen_grid_get_room_count (self->grid); i++)
	{
		const LrgProcgenRoom *room = lrg_procgen_grid_get_room (self->grid, i);
		GrlColor              edge = editor_data_color_alpha (accent, 0.8f);

		lrg_paint_list_border (list, ox + (gfloat) room->x * cell, oy + (gfloat) room->y * cell,
		                       (gfloat) room->width * cell, (gfloat) room->height * cell, MIN (3.0f, cell * 0.5f),
		                       1.0f, &edge);
	}

	/* --- spawns, coloured by kind --- */
	for (i = 0; i < lrg_procgen_grid_get_spawn_count (self->grid); i++)
	{
		const LrgProcgenSpawn *spawn = lrg_procgen_grid_get_spawn (self->grid, i);
		const GrlColor        *c = lrg_theme_get_color (theme, spawn_roles[g_str_hash (spawn->kind) %
		                                                                   G_N_ELEMENTS (spawn_roles)]);

		lrg_paint_list_circle (list, ox + ((gfloat) spawn->x + 0.5f) * cell, oy + ((gfloat) spawn->y + 0.5f) * cell,
		                       MAX (1.5f, cell * 0.35f), c);
	}

	/* --- start and exit --- */
	if (lrg_procgen_grid_get_start (self->grid, &sx, &sy))
	{
		lrg_paint_list_circle (list, ox + ((gfloat) sx + 0.5f) * cell, oy + ((gfloat) sy + 0.5f) * cell,
		                       MAX (3.0f, cell * 0.6f), lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
	}
	if (lrg_procgen_grid_get_exit (self->grid, &sx, &sy))
	{
		gfloat ex = ox + (gfloat) sx * cell, ey = oy + (gfloat) sy * cell, es = MAX (5.0f, cell);

		lrg_paint_list_rect (list, ex + (cell - es) * 0.5f, ey + (cell - es) * 0.5f, es, es, 1.5f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
	}

	/* --- hovered cell --- */
	if (self->hover_x >= 0 && self->hover_y >= 0 && (guint) self->hover_x < gw && (guint) self->hover_y < gh)
		lrg_paint_list_border (list, ox + (gfloat) self->hover_x * cell, oy + (gfloat) self->hover_y * cell, cell, cell,
		                       0.0f, 1.5f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));

	/* --- statistics (or the hovered cell) --- */
	if (self->hover_x >= 0 && (guint) self->hover_x < gw && self->hover_y >= 0 && (guint) self->hover_y < gh)
	{
		gint room = lrg_procgen_grid_room_at (self->grid, self->hover_x, self->hover_y);

		stats = g_strdup_printf ("cell %d, %d · %s%s%s", self->hover_x, self->hover_y,
		                         tag_name (lrg_procgen_grid_get_cell (self->grid, self->hover_x, self->hover_y)),
		                         room >= 0 ? " · room " : "",
		                         room >= 0 ? "#" : "");
		if (room >= 0)
		{
			g_autofree gchar *with_room = g_strdup_printf ("%s%d", stats, room + 1);

			g_free (stats);
			stats = g_steal_pointer (&with_room);
		}
	}
	else
		stats = g_strdup_printf ("%u × %u · seed %" G_GUINT64_FORMAT " · %u rooms · %u regions "
		                         "· %u%% walkable · %u spawns",
		                         gw, gh, self->grid_seed, lrg_procgen_grid_get_room_count (self->grid),
		                         lrg_procgen_grid_get_region_count (self->grid),
		                         (guint) (100u * lrg_procgen_grid_count_walkable (self->grid) / MAX (1u, gw * gh)),
		                         lrg_procgen_grid_get_spawn_count (self->grid));
	lrg_paint_list_text (list, font, stats, x + PREVIEW_PAD, y + height - FOOTER_H + 4.0f, fs, 0.0f, muted);

	/* --- a recipe error over the last good map --- */
	if (self->error != NULL)
	{
		GrlColor fill = editor_data_color_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP), 0.95f);
		gfloat   bh = fs * 1.4f + 14.0f;
		gfloat   by = y + height - FOOTER_H - bh - 6.0f;

		lrg_paint_list_rect (list, x + PREVIEW_PAD, by, width - PREVIEW_PAD * 2.0f, bh, radius, &fill);
		lrg_paint_list_rect (list, x + PREVIEW_PAD, by, 4.0f, bh, 2.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		lrg_paint_list_icon (list, LRG_ICON_WARNING, x + PREVIEW_PAD + 12.0f, by + 7.0f, fs + 2.0f, 1.5f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		lrg_paint_list_push_clip (list, x + PREVIEW_PAD, by, width - PREVIEW_PAD * 2.0f, bh);
		lrg_paint_list_text (list, font, self->error, x + PREVIEW_PAD + 34.0f, by + 7.0f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_paint_list_pop_clip (list);
	}
}

/* The debounced regeneration, on the canvas clock. */
static gboolean
preview_step (LrgCustomWidget *widget,
              gfloat           delta_time,
              gpointer         user_data)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (user_data);

	if (!self->pending)
		return FALSE;
	self->pending_time += delta_time;
	if (self->pending_time >= AUTO_DELAY)
		generate (self, NULL);
	return self->pending;
}

static gboolean
preview_event (LrgCustomWidget  *widget,
               const LrgUIEvent *event,
               gpointer          user_data)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (user_data);
	LrgWidget              *w = LRG_WIDGET (widget);
	gfloat                  cell, ox, oy;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->grid == NULL)
			return FALSE;
		map_geometry (self, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w), lrg_widget_get_width (w),
		              lrg_widget_get_height (w), &cell, &ox, &oy);
		self->hover_x = (gint) floorf ((lrg_ui_event_get_x (event) - ox) / cell);
		self->hover_y = (gint) floorf ((lrg_ui_event_get_y (event) - oy) / cell);
		if (lrg_ui_event_get_x (event) < ox || lrg_ui_event_get_y (event) < oy)
			self->hover_x = self->hover_y = -1;
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_x = self->hover_y = -1;
		return FALSE;
	default:
		return FALSE;
	}
}

/* --------------------------------------------------------------------------
 * Toolbar
 * -------------------------------------------------------------------------- */

static void
on_generate (LrgWidget *chip,
             gpointer   user_data)
{
	lrg_editor_dungeon_editor_generate (LRG_EDITOR_DUNGEON_EDITOR (user_data), NULL);
}

static void
on_randomize (LrgWidget *chip,
              gpointer   user_data)
{
	lrg_editor_dungeon_editor_randomize (LRG_EDITOR_DUNGEON_EDITOR (user_data));
}

static void
on_add_spawn (LrgWidget *chip,
              gpointer   user_data)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_editor_dungeon_editor_add_spawn (LRG_EDITOR_DUNGEON_EDITOR (user_data), "enemy", 4, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (user_data), error);
}

static void
on_auto (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (user_data);

	lrg_editor_dungeon_editor_set_auto (self, !self->auto_generate);
}

/* --------------------------------------------------------------------------
 * LrgEditorDataEditor
 * -------------------------------------------------------------------------- */

static void
dungeon_refresh (LrgEditorDataEditor *editor,
                 const gchar         *path)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (editor);
	LrgPropertySource      *source = self->form != NULL ? lrg_property_grid_get_source (self->form, 0) : NULL;

	if (source == NULL || g_strcmp0 (doc_generator (self), self->shown_generator) != 0 ||
	    doc_spawns (self) != self->shown_spawns)
		rebuild_form (self);
	else
		lrg_property_source_emit_changed (source, NULL);
	if (self->auto_generate)
	{
		self->pending = TRUE;
		self->pending_time = 0.0f;
	}
}

/* Scrolls the form to the row editing @path. */
static void
dungeon_reveal (LrgEditorDataEditor *editor,
                const gchar         *path,
                guint                line)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (editor);
	LrgPropertySource      *source = lrg_property_grid_get_source (self->form, 0);
	g_autoptr(GPtrArray)    props = NULL;
	guint                   i;

	if (source == NULL || path == NULL)
		return;
	props = lrg_property_source_list_properties (source);
	for (i = 0; i < props->len; i++)
	{
		const gchar *name = lrg_property_info_get_name (g_ptr_array_index (props, i));
		const gchar *fpath = editor_data_fields_get_path (EDITOR_DATA_FIELDS (source), name);
		LrgPropertyRow *row;

		if (fpath == NULL || !g_str_has_prefix (path, fpath))
			continue;
		row = lrg_property_grid_get_row (self->form, name);
		if (row != NULL)
		{
			LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (row));

			if (LRG_IS_CANVAS (root))
				lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (row));
		}
		return;
	}
}

static gchar *
dungeon_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (editor);

	return g_strdup_printf ("generator=%s seed=%" G_GUINT64_FORMAT " map=%ux%u rooms=%u auto=%s error=%s",
	                        doc_generator (self), self->grid_seed,
	                        self->grid != NULL ? lrg_procgen_grid_get_width (self->grid) : 0u,
	                        self->grid != NULL ? lrg_procgen_grid_get_height (self->grid) : 0u,
	                        self->grid != NULL ? lrg_procgen_grid_get_room_count (self->grid) : 0u,
	                        self->auto_generate ? "true" : "false", self->error != NULL ? self->error : "");
}

static void
lrg_editor_dungeon_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs preview_funcs = { NULL, preview_paint, preview_step, preview_event, { NULL } };
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (object);
	LrgEditorDataEditor    *editor = LRG_EDITOR_DATA_EDITOR (object);
	LrgWidget              *chip;

	G_OBJECT_CLASS (lrg_editor_dungeon_editor_parent_class)->constructed (object);

	/* --- toolbar --- */
	chip = editor_chip_new ("dungeon-generate", LRG_ICON_PLAY, "Generate", on_generate, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_SUCCESS);
	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);
	chip = editor_chip_new ("dungeon-randomize", LRG_ICON_REFRESH, "Randomize", on_randomize, self);
	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);
	chip = editor_chip_new ("dungeon-add-spawn", LRG_ICON_PLUS, "Spawn", on_add_spawn, self);
	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);
	self->auto_chip = editor_chip_new ("dungeon-auto", LRG_ICON_CLOCK, "Auto", on_auto, self);
	editor_chip_set_checked (self->auto_chip, TRUE);
	lrg_editor_data_editor_add_tool (editor, self->auto_chip);
	g_object_unref (self->auto_chip);

	/* --- form | preview --- */
	self->form = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->form), "dungeon-form");
	self->preview = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->preview, "dungeon-preview");
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->preview), 320.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->preview), 240.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->preview), &preview_funcs, self, NULL);

	self->split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (self->split), "dungeon-split");
	lrg_splitter_set_start_child (self->split, LRG_WIDGET (self->form));
	g_object_unref (self->form);
	lrg_splitter_set_end_child (self->split, self->preview);
	g_object_unref (self->preview);
	lrg_splitter_set_fraction (self->split, 0.36f, FALSE);
	lrg_splitter_set_min_sizes (self->split, 220.0f, 200.0f);
	lrg_editor_data_editor_set_content (editor, LRG_WIDGET (self->split));
	g_object_unref (self->split);

	rebuild_form (self);
	generate (self, NULL);
}

static void
lrg_editor_dungeon_editor_dispose (GObject *object)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (object);

	if (self->preview != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->preview), NULL, NULL, NULL);
	self->preview = NULL;
	self->form = NULL;
	self->split = NULL;
	self->auto_chip = NULL;
	g_clear_object (&self->grid);
	G_OBJECT_CLASS (lrg_editor_dungeon_editor_parent_class)->dispose (object);
}

static void
lrg_editor_dungeon_editor_finalize (GObject *object)
{
	LrgEditorDungeonEditor *self = LRG_EDITOR_DUNGEON_EDITOR (object);

	g_free (self->shown_generator);
	g_free (self->error);
	G_OBJECT_CLASS (lrg_editor_dungeon_editor_parent_class)->finalize (object);
}

static void
lrg_editor_dungeon_editor_class_init (LrgEditorDungeonEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *editor_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_dungeon_editor_constructed;
	object_class->dispose = lrg_editor_dungeon_editor_dispose;
	object_class->finalize = lrg_editor_dungeon_editor_finalize;
	editor_class->refresh = dungeon_refresh;
	editor_class->reveal = dungeon_reveal;
	editor_class->get_state = dungeon_get_state;
}

static void
lrg_editor_dungeon_editor_init (LrgEditorDungeonEditor *self)
{
	self->auto_generate = TRUE;
	self->hover_x = self->hover_y = -1;
	self->shown_spawns = -1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorDungeonEditor *
lrg_editor_dungeon_editor_new (LrgEditorApp      *app,
                               LrgEditorDocument *document)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);

	return g_object_new (LRG_TYPE_EDITOR_DUNGEON_EDITOR, "app", app, "document", document, NULL);
}

LrgPropertyGrid *
lrg_editor_dungeon_editor_get_form (LrgEditorDungeonEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), NULL);
	return self->form;
}

gboolean
lrg_editor_dungeon_editor_generate (LrgEditorDungeonEditor  *self,
                                    GError                 **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), FALSE);
	return generate (self, error);
}

guint64
lrg_editor_dungeon_editor_randomize (LrgEditorDungeonEditor *self)
{
	g_autoptr(GError) error = NULL;
	guint64           seed;

	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), 0);

	/* 31 bits keep the seed exact in every YAML reader */
	do
		seed = (guint64) g_random_int_range (1, G_MAXINT32);
	while (seed == doc_seed (self));
	if (!lrg_editor_document_set_int (doc_of (self), "/seed", (gint64) seed, 0, &error))
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return doc_seed (self);
	}
	generate (self, NULL);
	return seed;
}

guint64
lrg_editor_dungeon_editor_get_seed (LrgEditorDungeonEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), 0);
	return doc_seed (self);
}

LrgProcgenGrid *
lrg_editor_dungeon_editor_get_grid (LrgEditorDungeonEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), NULL);
	return self->grid;
}

const gchar *
lrg_editor_dungeon_editor_get_error (LrgEditorDungeonEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), NULL);
	return self->error;
}

gboolean
lrg_editor_dungeon_editor_get_pending (LrgEditorDungeonEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), FALSE);
	return self->pending;
}

gboolean
lrg_editor_dungeon_editor_get_auto (LrgEditorDungeonEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), FALSE);
	return self->auto_generate;
}

void
lrg_editor_dungeon_editor_set_auto (LrgEditorDungeonEditor *self,
                                    gboolean                auto_generate)
{
	g_return_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self));

	self->auto_generate = auto_generate;
	if (!auto_generate)
		self->pending = FALSE;
	if (self->auto_chip != NULL)
		editor_chip_set_checked (self->auto_chip, auto_generate);
}

gboolean
lrg_editor_dungeon_editor_add_spawn (LrgEditorDungeonEditor  *self,
                                     const gchar             *kind,
                                     guint                    count,
                                     GError                 **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *yaml = NULL;
	g_autofree gchar  *quoted = NULL;
	gboolean           ok = TRUE;

	g_return_val_if_fail (LRG_IS_EDITOR_DUNGEON_EDITOR (self), FALSE);
	g_return_val_if_fail (kind != NULL, FALSE);

	doc = doc_of (self);
	quoted = g_strescape (kind, NULL);
	yaml = g_strdup_printf ("{ kind: \"%s\", count: %u }", quoted, count);
	lrg_editor_document_begin_group (doc, "Add spawn rule");
	if (lrg_editor_document_get_kind (doc, "/spawns") != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		ok = lrg_editor_document_replace (doc, "/spawns", "[]", error);
	if (ok)
		ok = lrg_editor_document_insert_item (doc, "/spawns", -1, yaml, error);
	lrg_editor_document_end_group (doc);
	return ok;
}
