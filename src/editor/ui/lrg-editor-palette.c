/* lrg-editor-palette.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPalette: command palette and quick open (see the header).
 *
 * The palette is a container on the canvas popup layer (non-modal: a
 * click outside closes it, the canvas tells us through
 * ::popup-dismissed). Its one child is the query LrgTextInput; the result
 * rows are painted by the palette itself below the field, which keeps
 * 20000 files cheap: only the visible window of rows is ever touched.
 *
 * Results are an array of Hit (score, text, label, detail, accel label,
 * enabled, node) rebuilt whenever the query, the mode or the file list
 * changes. Matched characters are found with a greedy case-insensitive
 * subsequence walk (the same shape lrg_editor_fuzzy_score() scores) and
 * painted in the accent colour.
 *
 * Keys arrive at the focused query field first; the field keeps typing,
 * Enter ("submitted" signal) and editing keys and lets Up / Down / Page
 * keys bubble to us. Escape becomes a CANCEL event that bubbles here too.
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "lrg-editor-palette.h"
#include "lrg-editor-code-panel.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-icons.h"
#include <math.h>
#include <string.h>

#define PALETTE_WIDTH   (680.0f)
#define PALETTE_ROWS    (12)
#define PALETTE_TOP_GAP (10.0f)
#define MAX_HITS        (2000)

/* ==========================================================================
 * Enum
 * ========================================================================== */

GType
lrg_editor_palette_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_PALETTE_MODE_COMMANDS, "LRG_EDITOR_PALETTE_MODE_COMMANDS", "commands" },
			{ LRG_EDITOR_PALETTE_MODE_FILES, "LRG_EDITOR_PALETTE_MODE_FILES", "files" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorPaletteMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Types
 * ========================================================================== */

typedef enum
{
	HIT_ACTION,
	HIT_FILE,
	HIT_NODE
} HitKind;

typedef struct
{
	HitKind      kind;
	gint         score;
	gchar       *text;     /* action id, relative path or node guid */
	gchar       *label;    /* main text (matched against the query) */
	gchar       *detail;   /* muted text after the label */
	gchar       *accel;    /* shortcut label (actions) */
	gboolean     enabled;
} Hit;

struct _LrgEditorPalette
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgTextInput         *query;        /* child */
	LrgEditorPaletteMode  mode;
	gboolean              open;
	GArray               *hits;         /* Hit */
	gint                  selected;
	gint                  first;        /* first visible row */
	gint                  hover;
	gboolean              updating;     /* setting the query text ourselves */

	/* Quick open */
	GPtrArray            *files;        /* gchar* relative paths, sorted */
	gchar                *files_root;
	GCancellable         *scan;
	gboolean              scanning;
	gulong                dismissed_id;
};

G_DEFINE_FINAL_TYPE (LrgEditorPalette, lrg_editor_palette, LRG_TYPE_CONTAINER)

static void rebuild (LrgEditorPalette *self);

static void
hit_clear (gpointer data)
{
	Hit *hit = data;

	g_free (hit->text);
	g_free (hit->label);
	g_free (hit->detail);
	g_free (hit->accel);
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static LrgTheme *
theme_of (LrgEditorPalette *self)
{
	return lrg_widget_get_theme (LRG_WIDGET (self));
}

static gfloat
metric (LrgEditorPalette *self,
        LrgThemeMetric    m)
{
	return lrg_theme_get_metric (theme_of (self), m);
}

static gfloat
pad (LrgEditorPalette *self)
{
	return roundf (metric (self, LRG_THEME_METRIC_PADDING_X));
}

static gfloat
row_height (LrgEditorPalette *self)
{
	return roundf (MAX (metric (self, LRG_THEME_METRIC_ROW_HEIGHT), metric (self, LRG_THEME_METRIC_FONT_SIZE) * 1.9f));
}

static gfloat
field_height (LrgEditorPalette *self)
{
	gfloat w = 0.0f;
	gfloat h = 0.0f;

	lrg_widget_get_preferred_size (LRG_WIDGET (self->query), &w, &h);
	return MAX (h, metric (self, LRG_THEME_METRIC_CONTROL_HEIGHT));
}

static gint
visible_rows (LrgEditorPalette *self)
{
	return CLAMP ((gint) self->hits->len, 1, PALETTE_ROWS);
}

/* Top of the first row, relative to the widget */
static gfloat
rows_top (LrgEditorPalette *self)
{
	return pad (self) + field_height (self) + pad (self) * 0.5f;
}

static gfloat
footer_height (LrgEditorPalette *self)
{
	return roundf (metric (self, LRG_THEME_METRIC_FONT_SIZE_SMALL) * 2.0f);
}

static void
lrg_editor_palette_measure (LrgWidget *widget,
                            gfloat    *width,
                            gfloat    *height)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (widget);

	*width = PALETTE_WIDTH;
	*height = rows_top (self) + row_height (self) * (gfloat) visible_rows (self) + footer_height (self) +
	          pad (self) * 0.5f;
}

static void
lrg_editor_palette_layout_children (LrgContainer *container)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (container);
	gfloat            p = pad (self);
	gfloat            icon = metric (self, LRG_THEME_METRIC_FONT_SIZE) * 1.3f;

	if (self->query == NULL)
		return;
	lrg_widget_place_in_slot (LRG_WIDGET (self->query), p + icon + p * 0.5f, p,
	                          MAX (lrg_widget_get_width (LRG_WIDGET (self)) - p * 2.5f - icon, 10.0f),
	                          field_height (self));
}

/* Places the open popup under the menu bar, centred, sized to its rows. */
static void
place (LrgEditorPalette *self)
{
	LrgCanvas *canvas;
	gfloat     cw;
	gfloat     w = 0.0f;
	gfloat     h = 0.0f;
	gfloat     top = 40.0f;

	if (self->app == NULL || !self->open)
		return;
	canvas = self->app->canvas;
	cw = lrg_widget_get_width (LRG_WIDGET (canvas));
	lrg_editor_palette_measure (LRG_WIDGET (self), &w, &h);
	w = MIN (w, MAX (cw - 32.0f, 200.0f));
	if (self->app->window != NULL)
	{
		LrgWidget *bar = LRG_WIDGET (lrg_editor_window_get_menu_bar (self->app->window));

		top = lrg_widget_get_world_y (bar) + lrg_widget_get_height (bar);
	}
	lrg_widget_allocate (LRG_WIDGET (self), roundf ((cw - w) * 0.5f), roundf (top + PALETTE_TOP_GAP), w, h);
}

/* ==========================================================================
 * Fuzzy highlight
 * ========================================================================== */

/* Marks in @mask (one byte per byte of @text) the characters a greedy
 * case-insensitive subsequence walk of @query matches; FALSE when it does
 * not match completely (nothing is highlighted then). */
static gboolean
match_mask (const gchar *query,
            const gchar *text,
            guint8      *mask)
{
	const gchar *q = query;
	gsize        i;
	gsize        n = strlen (text);

	memset (mask, 0, n);
	for (i = 0; i < n && *q != '\0'; i++)
	{
		if (*q == ' ')
		{
			q++;
			i--;
			continue;
		}
		if (g_ascii_tolower (text[i]) == g_ascii_tolower (*q))
		{
			mask[i] = 1;
			q++;
		}
	}
	while (*q == ' ')
		q++;
	return *q == '\0';
}

/* ==========================================================================
 * Results
 * ========================================================================== */

static gint
compare_hits (gconstpointer a,
              gconstpointer b)
{
	const Hit *x = a;
	const Hit *y = b;

	if (x->score != y->score)
		return (y->score > x->score) ? 1 : -1;
	if (strlen (x->text) != strlen (y->text))
		return (strlen (x->text) < strlen (y->text)) ? -1 : 1;
	return g_strcmp0 (x->text, y->text);
}

static void
add_hit (LrgEditorPalette *self,
         HitKind           kind,
         gint              score,
         const gchar      *text,
         gchar            *label,
         gchar            *detail,
         gchar            *accel,
         gboolean          enabled)
{
	Hit hit;

	hit.kind = kind;
	hit.score = score;
	hit.text = g_strdup (text);
	hit.label = label;
	hit.detail = detail;
	hit.accel = accel;
	hit.enabled = enabled;
	g_array_append_val (self->hits, hit);
}

static void
collect_actions (LrgEditorPalette *self,
                 const gchar      *query)
{
	LrgEditorActions *actions = self->app->actions;
	GList            *ids = lrg_editor_actions_search (actions, query);
	GList            *l;
	gint              rank = (gint) g_list_length (ids);

	/* Already ranked by the registry: keep its order */
	for (l = ids; l != NULL && self->hits->len < MAX_HITS; l = l->next, rank--)
	{
		const gchar      *id = l->data;
		g_auto(GStrv)     accels = lrg_editor_actions_get_accels (actions, id);
		gchar            *accel = NULL;

		if (accels != NULL && accels[0] != NULL && accels[0][0] != '\0')
			accel = lrg_editor_accel_to_label (accels[0]);
		add_hit (self, HIT_ACTION, rank, id, g_strdup (lrg_editor_actions_get_label (actions, id)),
		         g_strdup (lrg_editor_actions_get_category (actions, id)), accel,
		         lrg_editor_actions_is_enabled (actions, id));
	}
	g_list_free (ids);
}

typedef struct
{
	LrgEditorPalette *self;
	const gchar      *query;
} NodeWalk;

static gboolean
collect_node (LrgNode  *node,
              gpointer  user_data)
{
	NodeWalk    *walk = user_data;
	const gchar *name = lrg_node_get_name (node);
	gint         score;

	if (lrg_node_get_parent (node) == NULL || name == NULL || walk->self->hits->len >= MAX_HITS)
		return TRUE;
	score = (*walk->query == '\0') ? 1 : lrg_editor_fuzzy_score (walk->query, name);
	if (score <= 0)
		return TRUE;
	{
		g_autofree gchar *path = lrg_node_get_path (node);
		LrgNode          *parent = lrg_node_get_parent (node);
		gchar            *detail = NULL;

		if (parent != NULL && lrg_node_get_parent (parent) != NULL)
		{
			g_autofree gchar *ppath = lrg_node_get_path (parent);

			detail = g_strdup (ppath);
		}
		add_hit (walk->self, HIT_NODE, score, lrg_node_get_guid (node), g_strdup (name), detail, NULL, TRUE);
	}
	return TRUE;
}

static void
collect_files (LrgEditorPalette *self,
               const gchar      *query)
{
	guint i;

	if (self->files == NULL)
		return;
	for (i = 0; i < self->files->len; i++)
	{
		const gchar *rel = g_ptr_array_index (self->files, i);
		const gchar *base = strrchr (rel, '/');
		gint         score = (*query == '\0') ? 1 : lrg_editor_fuzzy_score (query, rel);

		if (score <= 0)
			continue;
		/* Name matches beat folder matches */
		if (*query != '\0' && base != NULL && lrg_editor_fuzzy_score (query, base + 1) > 0)
			score += 1000;
		add_hit (self, HIT_FILE, score, rel, g_strdup (rel), NULL, NULL, TRUE);
	}
	g_array_sort (self->hits, compare_hits);
	if (self->hits->len > MAX_HITS)
		g_array_set_size (self->hits, MAX_HITS);
}

static void
rebuild (LrgEditorPalette *self)
{
	const gchar *query = lrg_text_input_get_text (self->query);

	g_array_set_size (self->hits, 0);
	if (query == NULL)
		query = "";
	if (self->app != NULL)
	{
		if (self->mode == LRG_EDITOR_PALETTE_MODE_FILES)
			collect_files (self, query);
		else if (query[0] == '@')
		{
			NodeWalk walk = { self, query + 1 };
			LrgLevel *level = lrg_editor_get_level (self->app->editor);

			if (level != NULL)
				lrg_node_foreach (lrg_level_get_root (level), collect_node, &walk);
			g_array_sort (self->hits, compare_hits);
		}
		else
			collect_actions (self, query);
	}
	self->selected = (self->hits->len > 0) ? 0 : -1;
	self->first = 0;
	self->hover = -1;
	place (self);
}

/* ==========================================================================
 * Quick open scan
 * ========================================================================== */

static void
scan_dir (GPtrArray    *out,
          const gchar  *root,
          const gchar  *rel,
          GCancellable *cancel)
{
	g_autofree gchar *dir = (rel != NULL) ? g_build_filename (root, rel, NULL) : g_strdup (root);
	g_autoptr(GDir)   d = g_dir_open (dir, 0, NULL);
	g_autoptr(GPtrArray) subdirs = g_ptr_array_new_with_free_func (g_free);
	const gchar      *name;
	guint             i;

	if (d == NULL)
		return;
	while ((name = g_dir_read_name (d)) != NULL && out->len < LRG_EDITOR_PALETTE_FILE_LIMIT)
	{
		g_autofree gchar *child_rel = NULL;
		g_autofree gchar *child = NULL;

		if (g_cancellable_is_cancelled (cancel))
			return;
		/* Hidden entries (.git, .cache...), build trees, editor sidecars */
		if (name[0] == '.' || g_strcmp0 (name, "build") == 0 || g_strcmp0 (name, "node_modules") == 0 ||
		    g_strcmp0 (name, "trees") == 0 || g_str_has_suffix (name, ".meta") || g_str_has_suffix (name, "~"))
			continue;
		child_rel = (rel != NULL) ? g_build_filename (rel, name, NULL) : g_strdup (name);
		child = g_build_filename (root, child_rel, NULL);
		if (g_file_test (child, G_FILE_TEST_IS_SYMLINK))
			continue;
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
			g_ptr_array_add (subdirs, g_steal_pointer (&child_rel));
		else
			g_ptr_array_add (out, g_steal_pointer (&child_rel));
	}
	for (i = 0; i < subdirs->len && out->len < LRG_EDITOR_PALETTE_FILE_LIMIT; i++)
		scan_dir (out, root, g_ptr_array_index (subdirs, i), cancel);
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

static void
scan_thread (GTask        *task,
             gpointer      source,
             gpointer      task_data,
             GCancellable *cancel)
{
	GPtrArray *files = g_ptr_array_new_with_free_func (g_free);

	scan_dir (files, task_data, NULL, cancel);
	g_ptr_array_sort (files, compare_strings);
	g_task_return_pointer (task, files, (GDestroyNotify) g_ptr_array_unref);
}

static void
scan_done (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (source);
	GPtrArray        *files = g_task_propagate_pointer (G_TASK (result), NULL);

	if (files == NULL)
		return;   /* cancelled: a newer scan owns the state */
	g_clear_pointer (&self->files, g_ptr_array_unref);
	self->files = files;
	self->scanning = FALSE;
	g_clear_object (&self->scan);
	if (self->open && self->mode == LRG_EDITOR_PALETTE_MODE_FILES)
		rebuild (self);
}

static void
start_scan (LrgEditorPalette *self)
{
	const gchar      *root = (self->app != NULL) ? editor_app_base_dir (self->app) : NULL;
	g_autoptr(GTask)  task = NULL;

	if (root == NULL)
		return;
	if (self->scan != NULL)
		g_cancellable_cancel (self->scan);
	g_clear_object (&self->scan);
	if (g_strcmp0 (root, self->files_root) != 0)
	{
		g_clear_pointer (&self->files, g_ptr_array_unref);
		g_free (self->files_root);
		self->files_root = g_strdup (root);
	}
	self->scan = g_cancellable_new ();
	self->scanning = TRUE;
	task = g_task_new (self, self->scan, scan_done, NULL);
	g_task_set_task_data (task, g_strdup (root), g_free);
	g_task_set_check_cancellable (task, TRUE);
	g_task_run_in_thread (task, scan_thread);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/* Draws @text from @x with the characters @mask marks in @hi. */
static gfloat
paint_marked (LrgPaintList   *list,
              GrlFont        *font,
              const gchar    *text,
              const guint8   *mask,
              gfloat          x,
              gfloat          y,
              gfloat          size,
              const GrlColor *fg,
              const GrlColor *hi)
{
	gsize n = strlen (text);
	gsize i = 0;

	while (i < n)
	{
		gsize             j = i;
		gboolean          on = mask != NULL && mask[i];
		g_autofree gchar *run = NULL;
		gfloat            w = 0.0f;

		while (j < n && ((mask != NULL && mask[j]) != 0) == on)
			j++;
		/* Never split a UTF-8 sequence */
		while (j < n && ((guchar) text[j] & 0xC0) == 0x80)
			j++;
		run = g_strndup (text + i, j - i);
		lrg_paint_list_text (list, font, run, roundf (x), roundf (y), size, 1.0f, on ? hi : fg);
		lrg_paint_measure_text (font, run, size, 1.0f, &w, NULL);
		x += w;
		i = j;
	}
	return x;
}

static void
lrg_editor_palette_paint (LrgWidget    *widget,
                          LrgPaintList *list)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (widget);
	LrgTheme         *theme = theme_of (self);
	GrlFont          *font = lrg_theme_get_ui_font (theme);
	gfloat            x = lrg_widget_get_world_x (widget);
	gfloat            y = lrg_widget_get_world_y (widget);
	gfloat            w = lrg_widget_get_width (widget);
	gfloat            h = lrg_widget_get_height (widget);
	gfloat            p = pad (self);
	gfloat            radius = metric (self, LRG_THEME_METRIC_POPUP_RADIUS);
	gfloat            size = metric (self, LRG_THEME_METRIC_FONT_SIZE);
	gfloat            small = metric (self, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat            rh = row_height (self);
	gfloat            top = y + rows_top (self);
	gfloat            icon = size * 1.3f;
	const gchar      *query = lrg_text_input_get_text (self->query);
	gboolean          nodes = self->mode == LRG_EDITOR_PALETTE_MODE_COMMANDS && query != NULL && query[0] == '@';
	gint              rows = visible_rows (self);
	gint              i;

	if (query == NULL)
		query = "";

	/* Surface: shadow, popup body, hairline */
	lrg_paint_list_shadow (list, x, y, w, h, radius, metric (self, LRG_THEME_METRIC_SHADOW_BLUR) * 1.5f, 0.0f, 0.0f,
	                       metric (self, LRG_THEME_METRIC_SHADOW_OFFSET_Y) * 1.5f,
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW));
	lrg_paint_list_rect (list, x, y, w, h, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
	lrg_paint_list_border (list, x, y, w, h, radius, metric (self, LRG_THEME_METRIC_BORDER_WIDTH),
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	/* Mode glyph left of the field */
	lrg_paint_list_icon (list, self->mode == LRG_EDITOR_PALETTE_MODE_FILES ? LRG_ICON_FILE : LRG_ICON_SEARCH,
	                     x + p, y + p + (field_height (self) - icon) * 0.5f, icon, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
	lrg_widget_paint (LRG_WIDGET (self->query), list);
	lrg_paint_list_line (list, x + p, top - p * 0.25f, x + w - p, top - p * 0.25f, 1.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	lrg_paint_list_push_clip (list, x, top, w, rh * (gfloat) rows);
	if (self->hits->len == 0)
	{
		const gchar *msg = (self->mode == LRG_EDITOR_PALETTE_MODE_FILES && self->scanning) ? "Scanning the project..."
		                   : "No matches";

		lrg_paint_list_text (list, font, msg, x + p * 2.0f + icon, top + (rh - size) * 0.5f, size, 1.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	}
	for (i = 0; i < rows && self->first + i < (gint) self->hits->len; i++)
	{
		gint              idx = self->first + i;
		Hit              *hit = &g_array_index (self->hits, Hit, idx);
		gfloat            ry = top + rh * (gfloat) i;
		gfloat            tx = x + p * 1.5f + icon + p * 0.5f;
		gfloat            right = x + w - p * 1.5f;
		const GrlColor   *fg = lrg_theme_get_color (theme, hit->enabled ? LRG_THEME_COLOR_TEXT : LRG_THEME_COLOR_DISABLED);
		const GrlColor   *muted = lrg_theme_get_color (theme, hit->enabled ? LRG_THEME_COLOR_TEXT_MUTED
		                                                                    : LRG_THEME_COLOR_DISABLED);
		const gchar      *needle = nodes ? query + 1 : query;
		g_autofree guint8 *mask = g_malloc0 (strlen (hit->label) + 1);
		gboolean          marked;
		LrgIcon           glyph;

		/* Highlight: selection pill with an accent bar */
		if (idx == self->selected || idx == self->hover)
		{
			GrlColor sel = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);

			if (idx != self->selected)
				sel.a = (guint8) (sel.a / 2);
			lrg_paint_list_rect (list, x + p * 0.5f, ry + 1.0f, w - p, rh - 2.0f,
			                     metric (self, LRG_THEME_METRIC_CORNER_RADIUS), &sel);
			if (idx == self->selected)
				lrg_paint_list_rect (list, x + p * 0.5f, ry + rh * 0.25f, 3.0f, rh * 0.5f, 1.5f,
				                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		}

		glyph = (hit->kind == HIT_FILE) ? LRG_ICON_FILE : (hit->kind == HIT_NODE) ? LRG_ICON_CHEVRON_RIGHT
		        : LRG_ICON_PLAY;
		lrg_paint_list_icon (list, glyph, x + p * 1.5f, ry + (rh - icon) * 0.5f, icon, 0.0f,
		                     hit->enabled ? lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT) : muted);

		/* Shortcut chip on the right */
		if (hit->accel != NULL)
		{
			gfloat aw = 0.0f;
			gfloat ah = 0.0f;

			lrg_paint_measure_text (font, hit->accel, small, 1.0f, &aw, &ah);
			lrg_paint_list_rect (list, right - aw - p, ry + (rh - ah) * 0.5f - 3.0f, aw + p, ah + 6.0f,
			                     metric (self, LRG_THEME_METRIC_CORNER_RADIUS),
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
			lrg_paint_list_text (list, font, hit->accel, roundf (right - aw - p * 0.5f), roundf (ry + (rh - ah) * 0.5f),
			                     small, 1.0f, muted);
			right -= aw + p * 2.0f;
		}

		/* Category prefix (actions), then the label with matches lit */
		lrg_paint_list_push_clip (list, tx, ry, MAX (right - tx, 0.0f), rh);
		if (hit->kind == HIT_ACTION && hit->detail != NULL)
		{
			g_autofree gchar *cat = g_strconcat (hit->detail, ": ", NULL);

			tx = paint_marked (list, font, cat, NULL, tx, ry + (rh - size) * 0.5f, size, muted, muted);
		}
		marked = match_mask (needle, hit->label, mask);
		tx = paint_marked (list, font, hit->label, marked ? mask : NULL, tx, ry + (rh - size) * 0.5f, size, fg,
		                   lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		if (hit->kind != HIT_ACTION && hit->detail != NULL)
			paint_marked (list, font, hit->detail, NULL, tx + p, ry + (rh - small) * 0.5f, small, muted, muted);
		lrg_paint_list_pop_clip (list);
	}
	lrg_paint_list_pop_clip (list);

	/* Footer: counts and hints */
	{
		g_autofree gchar *foot = NULL;
		gfloat            fy = top + rh * (gfloat) rows + (footer_height (self) - small) * 0.5f;

		if (self->mode == LRG_EDITOR_PALETTE_MODE_FILES)
			foot = g_strdup_printf ("%u of %u files%s   \xe2\x86\x91\xe2\x86\x93 move   Enter open   Esc close",
			                        self->hits->len, self->files != NULL ? self->files->len : 0,
			                        self->scanning ? " (scanning)" : "");
		else if (nodes)
			foot = g_strdup_printf ("%u nodes   Enter select   Esc close", self->hits->len);
		else
			foot = g_strdup_printf ("%u commands   @ jumps to a node   Enter run   Esc close", self->hits->len);
		lrg_paint_list_text (list, font, foot, x + p * 1.5f, roundf (fy), small, 1.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	}
}

/* ==========================================================================
 * Input
 * ========================================================================== */

static void
move_selection (LrgEditorPalette *self,
                gint              delta)
{
	gint n = (gint) self->hits->len;

	if (n == 0)
		return;
	if (self->selected < 0)
		self->selected = 0;
	else if (delta == 1 || delta == -1)
		self->selected = (self->selected + delta + n) % n;   /* arrows wrap */
	else
		self->selected = CLAMP (self->selected + delta, 0, n - 1);
	lrg_editor_palette_set_selected (self, self->selected);
}

static gint
row_at (LrgEditorPalette *self,
        gfloat            y)
{
	gfloat top = lrg_widget_get_world_y (LRG_WIDGET (self)) + rows_top (self);
	gint   i;

	if (y < top)
		return -1;
	i = (gint) floorf ((y - top) / row_height (self));
	if (i >= visible_rows (self) || self->first + i >= (gint) self->hits->len)
		return -1;
	return self->first + i;
}

static gboolean
lrg_editor_palette_handle_event (LrgWidget        *widget,
                                 const LrgUIEvent *event)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (widget);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_CANCEL:
		lrg_editor_palette_dismiss (self);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_DOWN:      move_selection (self, 1); return TRUE;
		case GRL_KEY_UP:        move_selection (self, -1); return TRUE;
		case GRL_KEY_PAGE_DOWN: move_selection (self, PALETTE_ROWS - 1); return TRUE;
		case GRL_KEY_PAGE_UP:   move_selection (self, -(PALETTE_ROWS - 1)); return TRUE;
		case GRL_KEY_ENTER:
		case GRL_KEY_KP_ENTER:
			lrg_editor_palette_activate (self);
			return TRUE;
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover = row_at (self, lrg_ui_event_get_y (event));
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0 && row_at (self, lrg_ui_event_get_y (event)) >= 0;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) == 0 && lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
		                                                                         lrg_ui_event_get_y (event)))
		{
			gint row = row_at (self, lrg_ui_event_get_y (event));

			if (row >= 0)
			{
				self->selected = row;
				lrg_editor_palette_activate (self);
				return TRUE;
			}
		}
		return FALSE;

	case LRG_UI_EVENT_SCROLL:
		{
			gint max_first = MAX ((gint) self->hits->len - visible_rows (self), 0);

			self->first = CLAMP (self->first - (gint) roundf (lrg_ui_event_get_scroll_y (event) * 3.0f), 0, max_first);
			return TRUE;
		}

	default:
		return FALSE;
	}
}

static void
on_query_changed (LrgTextInput *input,
                  gpointer      user_data)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (user_data);

	if (!self->updating)
		rebuild (self);
}

static void
on_query_submitted (LrgTextInput *input,
                    gpointer      user_data)
{
	lrg_editor_palette_activate (LRG_EDITOR_PALETTE (user_data));
}

static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (user_data);

	if (popup != LRG_WIDGET (self) || !self->open)
		return;
	self->open = FALSE;
	if (self->app != NULL)
		lrg_editor_view_state_set_palette_open (self->app->view, FALSE);
}

/* A dialog opened (from the palette or anywhere): the palette goes */
static void
on_dialog_changed (GObject    *view,
                   GParamSpec *pspec,
                   gpointer    user_data)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (user_data);

	if (self->open && lrg_editor_view_state_get_dialog (LRG_EDITOR_VIEW_STATE (view)) != NULL)
		lrg_editor_palette_dismiss (self);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static LrgEditorPalette *
app_palette (gpointer data)
{
	return g_object_get_data (G_OBJECT (data), "lrg-editor-palette");
}

static void
a_palette (gpointer data)
{
	LrgEditorPalette *self = app_palette (data);

	if (self != NULL)
		lrg_editor_palette_present (self, LRG_EDITOR_PALETTE_MODE_COMMANDS, NULL);
}

static void
a_quick_open (gpointer data)
{
	LrgEditorPalette *self = app_palette (data);

	if (self != NULL)
		lrg_editor_palette_present (self, LRG_EDITOR_PALETTE_MODE_FILES, NULL);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_palette_dispose (GObject *object)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (object);

	if (self->scan != NULL)
		g_cancellable_cancel (self->scan);
	g_clear_object (&self->scan);
	if (self->app != NULL && self->app->canvas != NULL && self->dismissed_id != 0)
		g_signal_handler_disconnect (self->app->canvas, self->dismissed_id);
	self->dismissed_id = 0;
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_palette_parent_class)->dispose (object);
}

static void
lrg_editor_palette_finalize (GObject *object)
{
	LrgEditorPalette *self = LRG_EDITOR_PALETTE (object);

	g_clear_pointer (&self->hits, g_array_unref);
	g_clear_pointer (&self->files, g_ptr_array_unref);
	g_free (self->files_root);
	G_OBJECT_CLASS (lrg_editor_palette_parent_class)->finalize (object);
}

static void
lrg_editor_palette_class_init (LrgEditorPaletteClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_editor_palette_dispose;
	object_class->finalize = lrg_editor_palette_finalize;
	widget_class->measure = lrg_editor_palette_measure;
	widget_class->paint = lrg_editor_palette_paint;
	widget_class->handle_event = lrg_editor_palette_handle_event;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_palette_layout_children;
}

static void
lrg_editor_palette_init (LrgEditorPalette *self)
{
	self->hits = g_array_new (FALSE, TRUE, sizeof (Hit));
	g_array_set_clear_func (self->hits, hit_clear);
	self->selected = -1;
	self->hover = -1;
	lrg_widget_set_name (LRG_WIDGET (self), "palette");

	self->query = lrg_text_input_new ();
	lrg_widget_set_name (LRG_WIDGET (self->query), "palette-query");
	g_signal_connect_object (self->query, "text-changed", G_CALLBACK (on_query_changed), self, 0);
	g_signal_connect_object (self->query, "submitted", G_CALLBACK (on_query_submitted), self, 0);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->query));
	g_object_unref (self->query);
}

LrgEditorPalette *
lrg_editor_palette_new (LrgEditorApp *app)
{
	LrgEditorPalette *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_PALETTE, NULL);
	g_set_weak_pointer (&self->app, app);
	if (app->canvas != NULL)
		self->dismissed_id = g_signal_connect (app->canvas, "popup-dismissed", G_CALLBACK (on_popup_dismissed), self);
	g_signal_connect_object (app->view, "notify::dialog", G_CALLBACK (on_dialog_changed), self, 0);

	/* The actions find the palette through the app (object data set by
	 * whoever keeps it, see lrg_editor_code_panel_new()) */
	lrg_editor_actions_add (app->actions, "tools.palette", "Command Palette", "Tools", "Ctrl+Shift+P|F1", a_palette,
	                        NULL, NULL, app);
	lrg_editor_actions_add (app->actions, "tools.quick-open", "Quick Open File", "Tools", "Ctrl+P", a_quick_open,
	                        NULL, NULL, app);
	return self;
}

LrgEditorApp *
lrg_editor_palette_get_app (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), NULL);
	return self->app;
}

/* ==========================================================================
 * API
 * ========================================================================== */

void
lrg_editor_palette_present (LrgEditorPalette     *self,
                            LrgEditorPaletteMode  mode,
                            const gchar          *query)
{
	LrgCanvas *canvas;

	g_return_if_fail (LRG_IS_EDITOR_PALETTE (self));

	if (self->app == NULL || self->app->canvas == NULL ||
	    lrg_editor_view_state_get_dialog (self->app->view) != NULL)
		return;
	canvas = self->app->canvas;
	self->mode = mode;
	self->updating = TRUE;
	lrg_text_input_set_text (self->query, query != NULL ? query : "");
	self->updating = FALSE;
	lrg_text_input_set_placeholder (self->query, mode == LRG_EDITOR_PALETTE_MODE_FILES
	                                ? "Open a project file by name..."
	                                : "Run a command...  (@ jumps to a node)");
	if (mode == LRG_EDITOR_PALETTE_MODE_FILES)
		start_scan (self);

	if (!self->open)
	{
		self->open = TRUE;
		lrg_widget_set_appear (LRG_WIDGET (self), LRG_APPEAR_FLY_DOWN, 0.0f);
		lrg_widget_set_size (LRG_WIDGET (self), 0.0f, 0.0f);
		lrg_canvas_show_popup (canvas, LRG_WIDGET (self), NULL, LRG_POPUP_PLACEMENT_CENTER, FALSE);
		lrg_editor_view_state_set_palette_open (self->app->view, TRUE);
	}
	rebuild (self);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (self->query));
	lrg_text_input_set_cursor_position (self->query, (gint) g_utf8_strlen (lrg_text_input_get_text (self->query), -1));
}

void
lrg_editor_palette_dismiss (LrgEditorPalette *self)
{
	g_return_if_fail (LRG_IS_EDITOR_PALETTE (self));

	if (!self->open)
		return;
	if (self->app != NULL && self->app->canvas != NULL)
		lrg_canvas_dismiss_popup (self->app->canvas, LRG_WIDGET (self));
	/* popup-dismissed normally does this; be sure without a canvas */
	self->open = FALSE;
	if (self->app != NULL)
		lrg_editor_view_state_set_palette_open (self->app->view, FALSE);
}

gboolean
lrg_editor_palette_get_open (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), FALSE);
	return self->open;
}

LrgEditorPaletteMode
lrg_editor_palette_get_mode (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), LRG_EDITOR_PALETTE_MODE_COMMANDS);
	return self->mode;
}

const gchar *
lrg_editor_palette_get_query (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), NULL);
	return lrg_text_input_get_text (self->query) != NULL ? lrg_text_input_get_text (self->query) : "";
}

void
lrg_editor_palette_set_query (LrgEditorPalette *self,
                              const gchar      *query)
{
	g_return_if_fail (LRG_IS_EDITOR_PALETTE (self));

	self->updating = TRUE;
	lrg_text_input_set_text (self->query, query != NULL ? query : "");
	self->updating = FALSE;
	rebuild (self);
}

guint
lrg_editor_palette_get_n_results (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), 0);
	return self->hits->len;
}

const gchar *
lrg_editor_palette_get_result (LrgEditorPalette *self,
                               guint             index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), NULL);

	if (index >= self->hits->len)
		return NULL;
	return g_array_index (self->hits, Hit, index).text;
}

gint
lrg_editor_palette_get_selected (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), -1);
	return self->selected;
}

void
lrg_editor_palette_set_selected (LrgEditorPalette *self,
                                 gint              index)
{
	gint rows;

	g_return_if_fail (LRG_IS_EDITOR_PALETTE (self));

	if (self->hits->len == 0)
	{
		self->selected = -1;
		return;
	}
	self->selected = CLAMP (index, 0, (gint) self->hits->len - 1);
	rows = visible_rows (self);
	if (self->selected < self->first)
		self->first = self->selected;
	else if (self->selected >= self->first + rows)
		self->first = self->selected - rows + 1;
}

gboolean
lrg_editor_palette_activate (LrgEditorPalette *self)
{
	LrgEditorApp     *app;
	Hit              *hit;
	HitKind           kind;
	g_autofree gchar *text = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), FALSE);

	app = self->app;
	if (app == NULL || self->selected < 0 || self->selected >= (gint) self->hits->len)
		return FALSE;
	hit = &g_array_index (self->hits, Hit, self->selected);
	if (!hit->enabled)
	{
		editor_app_set_status (app, "%s is not available right now", hit->label);
		return FALSE;
	}
	kind = hit->kind;
	text = g_strdup (hit->text);

	/* Close first: what runs may open a dialog or another palette */
	lrg_editor_palette_dismiss (self);
	switch (kind)
	{
	case HIT_ACTION:
		return lrg_editor_actions_activate (app->actions, text);

	case HIT_NODE:
		{
			LrgNode *node = lrg_editor_find_node (app->editor, text);

			if (node == NULL)
				return FALSE;
			lrg_editor_select (app->editor, node, FALSE);
			lrg_editor_view_state_focus_selection (app->view);
			return TRUE;
		}

	case HIT_FILE:
	default:
		switch (lrg_asset_database_classify (text))
		{
		case LRG_ASSET_TYPE_LEVEL:
		case LRG_ASSET_TYPE_PREFAB:
		case LRG_ASSET_TYPE_TEXTURE:
		case LRG_ASSET_TYPE_AUDIO:
		case LRG_ASSET_TYPE_MODEL:
		case LRG_ASSET_TYPE_FONT:
			editor_app_open_asset (app, text);
			break;
		default:
			lrg_editor_code_panel_open_script (app, text, 0);
			break;
		}
		return TRUE;
	}
}

gboolean
lrg_editor_palette_get_scanning (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), FALSE);
	return self->scanning;
}

guint
lrg_editor_palette_get_n_files (LrgEditorPalette *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PALETTE (self), 0);
	return self->files != NULL ? self->files->len : 0;
}
