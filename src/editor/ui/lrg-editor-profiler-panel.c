/* lrg-editor-profiler-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Profiler panel: a toolbar (pause, clear, live readout), a bar
 * graph of the last 300 frame times with 60 / 30 fps guides and a hover
 * readout, and a sortable table of LrgProfiler sections.
 *
 * Frames are counted by a "frame tap": an invisible, pointer-transparent
 * LrgCustomWidget added as a canvas overlay. Overlays are stepped by
 * every lrg_canvas_update() (every lrg_editor_app_process_frame()),
 * also while the panel is a hidden tab, so the tap records the frame's
 * delta time, decides whether the profiler should run (panel shown,
 * play running, or pinned through IPC) and refreshes the table four
 * times a second. Clocks come only from the canvas: tests drive frames
 * deterministically.
 *
 * Enabling, disabling and clearing the profiler are deferred to an idle
 * (the next main-context iteration, which the app runs between frames)
 * so they never happen while a section is open: a section begun with
 * the profiler off and ended with it on would be reported as unmatched.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-profiler-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-table.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <math.h>

#define GUIDE_60          (1000.0f / 60.0f)
#define GUIDE_30          (1000.0f / 30.0f)
#define REFRESH_INTERVAL  (0.25f)     /* table and readout, seconds */
#define GRAPH_HEIGHT      (140.0f)
#define SAMPLE_DEFAULT    (60)        /* profiler.sample frames */

/* ==========================================================================
 * Section rows
 * ========================================================================== */

#define LRG_TYPE_EDITOR_PROFILER_ROW (lrg_editor_profiler_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorProfilerRow, lrg_editor_profiler_row, LRG, EDITOR_PROFILER_ROW, GObject)

struct _LrgEditorProfilerRow
{
	GObject  parent_instance;

	gchar   *name;
	gdouble  last_ms;
	gdouble  avg_ms;
	gdouble  min_ms;
	gdouble  max_ms;
	guint    count;
};

G_DEFINE_FINAL_TYPE (LrgEditorProfilerRow, lrg_editor_profiler_row, G_TYPE_OBJECT)

static void
lrg_editor_profiler_row_finalize (GObject *object)
{
	g_free (LRG_EDITOR_PROFILER_ROW (object)->name);
	G_OBJECT_CLASS (lrg_editor_profiler_row_parent_class)->finalize (object);
}

static void
lrg_editor_profiler_row_class_init (LrgEditorProfilerRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_profiler_row_finalize;
}

static void
lrg_editor_profiler_row_init (LrgEditorProfilerRow *self)
{
}

/* ==========================================================================
 * Panel
 * ========================================================================== */

struct _LrgEditorProfilerPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgProfiler          *profiler;     /* ref (the default profiler) */
	LrgCanvas            *canvas;       /* weak, owns the tap overlay */
	LrgWidget            *tap;          /* the frame tap (canvas overlay) */

	/* Widgets */
	LrgWidget            *toolbar;
	LrgWidget            *pause;
	LrgWidget            *status;       /* recording indicator chip */
	LrgWidget            *readout;      /* LrgLabel */
	LrgWidget            *graph;        /* LrgCustomWidget */
	LrgTable             *table;
	GListStore           *rows;

	/* Frame ring (milliseconds) */
	gfloat                frames[LRG_EDITOR_PROFILER_FRAMES];
	guint                 head;         /* next write */
	guint                 n_frames;
	gint                  hovered;      /* ring index under the pointer, -1 */

	/* State */
	gboolean              paused;
	gboolean              pinned;       /* profiler.sample enable=true */
	gboolean              we_enabled;   /* the panel turned the profiler on */
	gboolean              want;         /* last decision of the tap */
	gboolean              clear_pending;
	guint                 apply_id;     /* idle applying want / clear */
	gfloat                since_refresh;
};

G_DEFINE_FINAL_TYPE (LrgEditorProfilerPanel, lrg_editor_profiler_panel, LRG_TYPE_CONTAINER)

/* Index into the ring of the @i-th recorded frame (0 = oldest). */
static guint
ring_index (LrgEditorProfilerPanel *self,
            guint                   i)
{
	return (self->head + LRG_EDITOR_PROFILER_FRAMES - self->n_frames + i) % LRG_EDITOR_PROFILER_FRAMES;
}

/* Whether the panel can be seen: every ancestor visible up to a canvas. */
static gboolean
panel_shown (LrgEditorProfilerPanel *self)
{
	LrgWidget *w = (LrgWidget *) self;

	while (lrg_widget_get_visible (w))
	{
		LrgContainer *parent;

		if (LRG_IS_CANVAS (w))
			return TRUE;
		parent = lrg_widget_get_parent (w);
		if (parent == NULL)
			return FALSE;
		w = (LrgWidget *) parent;
	}
	return FALSE;
}

/* --------------------------------------------------------------------------
 * Table
 * -------------------------------------------------------------------------- */

/* Rebuilds the rows from the profiler's sections (keeps the sort). */
static void
refresh_table (LrgEditorProfilerPanel *self)
{
	GList               *names = lrg_profiler_get_section_names (self->profiler);
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	GList               *l;
	gint                 col = lrg_table_get_sort_column (self->table);

	for (l = names; l != NULL; l = l->next)
	{
		const gchar          *name = l->data;
		LrgEditorProfilerRow *row = g_object_new (LRG_TYPE_EDITOR_PROFILER_ROW, NULL);
		LrgProfilerSample    *last = lrg_profiler_get_last_sample (self->profiler, name);

		row->name = g_strdup (name);
		row->last_ms = (last != NULL) ? lrg_profiler_sample_get_duration_ms (last) : 0.0;
		row->avg_ms = lrg_profiler_get_average_ms (self->profiler, name);
		row->min_ms = lrg_profiler_get_min_ms (self->profiler, name);
		row->max_ms = lrg_profiler_get_max_ms (self->profiler, name);
		row->count = lrg_profiler_get_sample_count (self->profiler, name);
		g_clear_pointer (&last, lrg_profiler_sample_free);
		g_ptr_array_add (rows, row);
	}
	/* lrg_profiler_get_section_names() returns the table's own keys
	 * (g_hash_table_get_keys) despite its "transfer full" annotation:
	 * free the list only */
	g_list_free (names);

	g_list_store_splice (self->rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->rows)),
	                     rows->pdata, rows->len);
	if (col >= 0)
		lrg_table_set_sort (self->table, col, lrg_table_get_sort_ascending (self->table));
}

/* The readout next to the toolbar: last frame, fps, average and peak. */
static void
refresh_readout (LrgEditorProfilerPanel *self)
{
	g_autofree gchar *text = NULL;
	gfloat            sum = 0.0f, peak = 0.0f, last;
	guint             i;

	if (self->n_frames == 0)
	{
		lrg_label_set_text (LRG_LABEL (self->readout), "No frames recorded");
		return;
	}
	for (i = 0; i < self->n_frames; i++)
	{
		gfloat v = self->frames[ring_index (self, i)];

		sum += v;
		peak = MAX (peak, v);
	}
	last = self->frames[ring_index (self, self->n_frames - 1)];
	text = g_strdup_printf ("%.2f ms  %.0f fps   avg %.2f   max %.2f", last, last > 0.0f ? 1000.0f / last : 0.0f,
	                        sum / (gfloat) self->n_frames, peak);
	lrg_label_set_text (LRG_LABEL (self->readout), text);
}

static gchar *
cell_name (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	return g_strdup (LRG_EDITOR_PROFILER_ROW (item)->name);
}

/* Columns 1..4: last, avg, max (ms) and calls; @user_data is the column */
static gchar *
cell_value (LrgTable  *table,
            LrgWidget *cell,
            GObject   *item,
            guint      position,
            gpointer   user_data)
{
	LrgEditorProfilerRow *row = LRG_EDITOR_PROFILER_ROW (item);

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		return g_strdup_printf ("%.3f", row->last_ms);
	case 2:
		return g_strdup_printf ("%.3f", row->avg_ms);
	case 3:
		return g_strdup_printf ("%.3f", row->max_ms);
	default:
		return g_strdup_printf ("%u", row->count);
	}
}

static gdouble
row_value (LrgEditorProfilerRow *row,
           gint                  column)
{
	switch (column)
	{
	case 1:
		return row->last_ms;
	case 2:
		return row->avg_ms;
	case 3:
		return row->max_ms;
	default:
		return (gdouble) row->count;
	}
}

static gint
sort_name (gconstpointer a,
           gconstpointer b,
           gpointer      user_data)
{
	return g_strcmp0 (LRG_EDITOR_PROFILER_ROW ((gpointer) a)->name, LRG_EDITOR_PROFILER_ROW ((gpointer) b)->name);
}

static gint
sort_value (gconstpointer a,
            gconstpointer b,
            gpointer      user_data)
{
	gdouble va = row_value (LRG_EDITOR_PROFILER_ROW ((gpointer) a), GPOINTER_TO_INT (user_data));
	gdouble vb = row_value (LRG_EDITOR_PROFILER_ROW ((gpointer) b), GPOINTER_TO_INT (user_data));

	return (va < vb) ? -1 : (va > vb) ? 1 : 0;
}

/* --------------------------------------------------------------------------
 * Enabling (deferred to between frames)
 * -------------------------------------------------------------------------- */

static gboolean
apply_idle (gpointer user_data)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (user_data);

	self->apply_id = 0;
	if (self->clear_pending)
	{
		self->clear_pending = FALSE;
		lrg_profiler_clear (self->profiler);
		refresh_table (self);
	}
	if (self->want && !lrg_profiler_is_enabled (self->profiler))
	{
		lrg_profiler_set_enabled (self->profiler, TRUE);
		self->we_enabled = TRUE;
	}
	else if (!self->want && self->we_enabled && lrg_profiler_is_enabled (self->profiler))
	{
		lrg_profiler_set_enabled (self->profiler, FALSE);
		self->we_enabled = FALSE;
	}
	editor_chip_set_checked (self->status, lrg_profiler_is_enabled (self->profiler));
	editor_chip_set_label (self->status, lrg_profiler_is_enabled (self->profiler) ? "Recording" : "Idle");
	return G_SOURCE_REMOVE;
}

static void
schedule_apply (LrgEditorProfilerPanel *self)
{
	if (self->apply_id == 0)
		self->apply_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE, apply_idle, self, NULL);
}

/*
 * tap_step:
 *
 * Runs once per canvas update, whether the panel is shown or not:
 * decides whether the profiler should run, records the frame time and
 * refreshes the table and readout on a timer.
 */
static gboolean
tap_step (LrgCustomWidget *tap,
          gfloat           delta_time,
          gpointer         user_data)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (user_data);
	gboolean                shown = panel_shown (self);
	gboolean                playing = (self->app != NULL && lrg_editor_app_is_playing (self->app));
	gboolean                want = shown || playing || self->pinned;

	if (want != self->want || (want && !lrg_profiler_is_enabled (self->profiler)) ||
	    (!want && self->we_enabled))
	{
		self->want = want;
		schedule_apply (self);
	}

	if (!want || self->paused || delta_time <= 0.0f)
		return FALSE;

	/* Record the frame */
	self->frames[self->head] = delta_time * 1000.0f;
	self->head = (self->head + 1) % LRG_EDITOR_PROFILER_FRAMES;
	self->n_frames = MIN (self->n_frames + 1, (guint) LRG_EDITOR_PROFILER_FRAMES);

	/* Table and readout four times a second (shown panels only) */
	self->since_refresh += delta_time;
	if (shown && self->since_refresh >= REFRESH_INTERVAL)
	{
		self->since_refresh = 0.0f;
		refresh_table (self);
		refresh_readout (self);
	}
	return FALSE;
}

/* --------------------------------------------------------------------------
 * Graph
 * -------------------------------------------------------------------------- */

/* The graph's vertical scale: at least 40 ms, else the peak + 10% */
static gfloat
graph_scale (LrgEditorProfilerPanel *self)
{
	gfloat peak = 0.0f;
	guint  i;

	for (i = 0; i < self->n_frames; i++)
		peak = MAX (peak, self->frames[ring_index (self, i)]);
	return MAX (40.0f, peak * 1.1f);
}

static void
graph_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (user_data);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                *font = lrg_theme_get_ui_font (theme);
	gfloat                  fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                  radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat                  scale = graph_scale (self);
	gfloat                  bar = width / (gfloat) LRG_EDITOR_PROFILER_FRAMES;
	const GrlColor         *ok = lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS);
	const GrlColor         *warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	const GrlColor         *bad = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
	const GrlColor         *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	GrlColor                c;
	guint                   i;

	/* Surface */
	lrg_paint_list_rect (list, x, y, width, height, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_border (list, x, y, width, height, radius, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	lrg_paint_list_push_clip (list, x, y, width, height);

	/* Bars, newest on the right, coloured by budget */
	for (i = 0; i < self->n_frames; i++)
	{
		gfloat          v = self->frames[ring_index (self, i)];
		gfloat          h = MIN (height - 4.0f, v / scale * (height - 4.0f));
		gfloat          bx = x + width - (gfloat) (self->n_frames - i) * bar;
		const GrlColor *col = v <= GUIDE_60 + 0.5f ? ok : v <= GUIDE_30 + 0.5f ? warn : bad;

		lrg_paint_color_with_alpha (col, (gint) i == self->hovered ? 1.0f : 0.75f, &c);
		lrg_paint_list_rect (list, bx, y + height - 2.0f - h, MAX (1.0f, bar - 1.0f), h, 0.0f, &c);
	}

	/* Budget guides; their labels sit right of the newest bars' gap and
	 * merge into one when the scale squeezes the lines together */
	{
		gfloat           gy60 = y + height - 2.0f - GUIDE_60 / scale * (height - 4.0f);
		gfloat           gy30 = y + height - 2.0f - GUIDE_30 / scale * (height - 4.0f);
		g_autofree gchar *top = g_strdup_printf ("%.0f ms", scale);
		gfloat           tw = 0.0f, th = 0.0f;

		lrg_paint_color_with_alpha (ok, 0.55f, &c);
		lrg_paint_list_line (list, x + 4.0f, gy60, x + width - 4.0f, gy60, 1.0f, &c);
		lrg_paint_color_with_alpha (warn, 0.55f, &c);
		lrg_paint_list_line (list, x + 4.0f, gy30, x + width - 4.0f, gy30, 1.0f, &c);
		if (gy60 - gy30 >= fs + 2.0f)
		{
			lrg_paint_list_text (list, font, "16.7 ms  60 fps", x + 8.0f, gy60 - fs - 2.0f, fs, 0.0f, muted);
			lrg_paint_list_text (list, font, "33.3 ms  30 fps", x + 8.0f, gy30 - fs - 2.0f, fs, 0.0f, muted);
		}
		else
			lrg_paint_list_text (list, font, "60 / 30 fps", x + 8.0f, gy30 - fs - 2.0f, fs, 0.0f, muted);

		/* The scale, top left */
		lrg_paint_measure_text (font, top, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, top, x + 8.0f, y + 4.0f, fs, 0.0f, muted);
	}

	/* Hover: a marker line and a readout bubble */
	if (self->hovered >= 0 && (guint) self->hovered < self->n_frames)
	{
		gfloat            v = self->frames[ring_index (self, (guint) self->hovered)];
		gfloat            hx = x + width - (gfloat) (self->n_frames - (guint) self->hovered) * bar + bar * 0.5f;
		g_autofree gchar *text = g_strdup_printf ("%.2f ms  (%.0f fps)  frame -%u", v, v > 0.0f ? 1000.0f / v : 0.0f,
		                                          self->n_frames - 1 - (guint) self->hovered);
		gfloat            tw = 0.0f, th = 0.0f, bx;

		lrg_paint_list_line (list, hx, y + 2.0f, hx, y + height - 2.0f, 1.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_paint_measure_text (font, text, fs, 0.0f, &tw, &th);
		bx = CLAMP (hx + 8.0f, x + 4.0f, x + width - tw - 16.0f);
		lrg_paint_list_rect (list, bx, y + 6.0f, tw + 12.0f, fs + 8.0f, radius,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TOOLTIP));
		lrg_paint_list_text (list, font, text, bx + 6.0f, y + 10.0f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	}
	else if (self->n_frames == 0)
	{
		const gchar *hint = "Frame times appear while this panel is shown or the game plays";
		gfloat       tw = 0.0f, th = 0.0f;

		lrg_paint_measure_text (font, hint, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, hint, x + (width - tw) * 0.5f, y + (height - fs) * 0.5f, fs, 0.0f, muted);
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
graph_event (LrgCustomWidget  *widget,
             const LrgUIEvent *event,
             gpointer          user_data)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (user_data);
	LrgWidget              *w = LRG_WIDGET (widget);
	gfloat                  width = lrg_widget_get_width (w);
	gfloat                  bar = width / (gfloat) LRG_EDITOR_PROFILER_FRAMES;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		{
			gfloat rx = lrg_ui_event_get_x (event) - lrg_widget_get_world_x (w);
			gint   from_right = (gint) floorf ((width - rx) / MAX (bar, 0.001f));

			self->hovered = (gint) self->n_frames - 1 - from_right;
			if (self->hovered < 0 || rx < 0.0f || rx > width)
				self->hovered = -1;
		}
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hovered = -1;
		return FALSE;
	default:
		return FALSE;
	}
}

/* --------------------------------------------------------------------------
 * Toolbar
 * -------------------------------------------------------------------------- */

static void
on_pause (LrgWidget *chip,
          gpointer   user_data)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (user_data);

	lrg_editor_profiler_panel_set_paused (self, !self->paused);
}

static void
on_clear (LrgWidget *chip,
          gpointer   user_data)
{
	lrg_editor_profiler_panel_clear (LRG_EDITOR_PROFILER_PANEL (user_data));
}

static void a_profiler_pause (gpointer d) { on_pause (NULL, d); }
static void a_profiler_clear (gpointer d) { on_clear (NULL, d); }

static void
a_profiler_show (gpointer data)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, "profiler");
}

static gboolean
profiler_paused (gpointer data)
{
	return LRG_EDITOR_PROFILER_PANEL (data)->paused;
}

/* --------------------------------------------------------------------------
 * IPC: profiler.sample {frames?, enable?}
 * -------------------------------------------------------------------------- */

static JsonNode *
m_profiler_sample (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (user_data);
	g_autoptr(JsonBuilder)  b = json_builder_new ();
	GList                  *names, *l;
	gint64                  want_frames = SAMPLE_DEFAULT;
	guint                   i, n;

	if (params != NULL && json_object_has_member (params, "frames"))
		want_frames = json_object_get_int_member (params, "frames");
	if (params != NULL && json_object_has_member (params, "enable"))
	{
		self->pinned = json_object_get_boolean_member (params, "enable");
		self->want = self->pinned || self->want;
		schedule_apply (self);
	}
	n = (guint) CLAMP (want_frames, 0, (gint64) self->n_frames);

	json_builder_begin_object (b);
	json_builder_set_member_name (b, "enabled");
	json_builder_add_boolean_value (b, lrg_profiler_is_enabled (self->profiler));
	json_builder_set_member_name (b, "pinned");
	json_builder_add_boolean_value (b, self->pinned);
	json_builder_set_member_name (b, "paused");
	json_builder_add_boolean_value (b, self->paused);
	json_builder_set_member_name (b, "frame_ms");
	json_builder_add_double_value (b, self->n_frames > 0 ? self->frames[ring_index (self, self->n_frames - 1)] : 0.0);

	/* Recent frame times, oldest first */
	json_builder_set_member_name (b, "frames");
	json_builder_begin_array (b);
	for (i = self->n_frames - n; i < self->n_frames; i++)
		json_builder_add_double_value (b, self->frames[ring_index (self, i)]);
	json_builder_end_array (b);

	/* Sections, by name */
	json_builder_set_member_name (b, "sections");
	json_builder_begin_array (b);
	names = g_list_sort (lrg_profiler_get_section_names (self->profiler), (GCompareFunc) g_strcmp0);
	for (l = names; l != NULL; l = l->next)
	{
		const gchar       *name = l->data;
		LrgProfilerSample *last = lrg_profiler_get_last_sample (self->profiler, name);

		json_builder_begin_object (b);
		json_builder_set_member_name (b, "name");
		json_builder_add_string_value (b, name);
		json_builder_set_member_name (b, "last_ms");
		json_builder_add_double_value (b, last != NULL ? lrg_profiler_sample_get_duration_ms (last) : 0.0);
		json_builder_set_member_name (b, "avg_ms");
		json_builder_add_double_value (b, lrg_profiler_get_average_ms (self->profiler, name));
		json_builder_set_member_name (b, "min_ms");
		json_builder_add_double_value (b, lrg_profiler_get_min_ms (self->profiler, name));
		json_builder_set_member_name (b, "max_ms");
		json_builder_add_double_value (b, lrg_profiler_get_max_ms (self->profiler, name));
		json_builder_set_member_name (b, "count");
		json_builder_add_int_value (b, lrg_profiler_get_sample_count (self->profiler, name));
		json_builder_end_object (b);
		g_clear_pointer (&last, lrg_profiler_sample_free);
	}
	/* lrg_profiler_get_section_names() returns the table's own keys
	 * (g_hash_table_get_keys) despite its "transfer full" annotation:
	 * free the list only */
	g_list_free (names);
	json_builder_end_array (b);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

/* --------------------------------------------------------------------------
 * Widget
 * -------------------------------------------------------------------------- */

static void
lrg_editor_profiler_panel_layout_children (LrgContainer *container)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (container);
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat                  w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                  h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                  pad = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y));
	gfloat                  gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);
	gfloat                  pw, ph, y = pad, gh;

	/* Children are added one by one during construction */
	if (self->toolbar == NULL || self->graph == NULL || self->table == NULL)
		return;

	/* toolbar | graph (up to GRAPH_HEIGHT, at most 45%) | table */
	editor_panel_fit_toolbar (self->toolbar, w - pad * 2.0f);
	lrg_widget_get_preferred_size (self->toolbar, &pw, &ph);
	lrg_widget_place_in_slot (self->toolbar, pad, y, MAX (0.0f, w - pad * 2.0f), ph);
	y += ph + gap;
	gh = MIN (GRAPH_HEIGHT, MAX (40.0f, (h - y) * 0.45f));
	lrg_widget_place_in_slot (self->graph, pad, y, MAX (0.0f, w - pad * 2.0f), gh);
	y += gh + gap;
	lrg_widget_place_in_slot (LRG_WIDGET (self->table), pad, y, MAX (0.0f, w - pad * 2.0f), MAX (0.0f, h - y - pad));
}

static void
lrg_editor_profiler_panel_dispose (GObject *object)
{
	LrgEditorProfilerPanel *self = LRG_EDITOR_PROFILER_PANEL (object);

	g_clear_handle_id (&self->apply_id, g_source_remove);
	if (self->canvas != NULL && self->tap != NULL)
	{
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), NULL, NULL, NULL);
		lrg_canvas_remove_overlay (self->canvas, self->tap);
	}
	self->tap = NULL;
	g_clear_weak_pointer (&self->canvas);

	/* Leave the default profiler as the panel found it */
	if (self->profiler != NULL && self->we_enabled)
		lrg_profiler_set_enabled (self->profiler, FALSE);
	self->we_enabled = FALSE;
	g_clear_object (&self->profiler);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_profiler_panel_parent_class)->dispose (object);
}

static void
lrg_editor_profiler_panel_finalize (GObject *object)
{
	g_clear_object (&LRG_EDITOR_PROFILER_PANEL (object)->rows);
	G_OBJECT_CLASS (lrg_editor_profiler_panel_parent_class)->finalize (object);
}

static void
lrg_editor_profiler_panel_class_init (LrgEditorProfilerPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_profiler_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_profiler_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_profiler_panel_layout_children;
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
lrg_editor_profiler_panel_init (LrgEditorProfilerPanel *self)
{
	static const LrgCustomWidgetFuncs graph_funcs = { NULL, graph_paint, NULL, graph_event, { NULL } };
	static const struct { const gchar *title; gfloat width; } columns[5] = {
		{ "Section", 200.0f }, { "Last (ms)", 90.0f }, { "Avg (ms)", 90.0f }, { "Max (ms)", 90.0f },
		{ "Calls", 70.0f }
	};
	LrgWidget *spacer, *chip;
	guint      i;

	self->hovered = -1;
	self->rows = g_list_store_new (LRG_TYPE_EDITOR_PROFILER_ROW);

	/* Toolbar: status | pause | clear | readout */
	self->toolbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->toolbar, "profiler-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	self->status = editor_chip_new ("profiler-status", LRG_ICON_CLOCK, "Idle", NULL, NULL);
	editor_chip_set_tint (self->status, LRG_THEME_COLOR_SUCCESS);
	lrg_widget_set_focusable (self->status, FALSE);
	add_to (self->toolbar, self->status);
	self->pause = editor_chip_new ("profiler-pause", LRG_ICON_PAUSE, "Pause", on_pause, self);
	add_to (self->toolbar, self->pause);
	chip = editor_chip_new ("profiler-clear", LRG_ICON_TRASH, "Clear", on_clear, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (self->toolbar, chip);
	spacer = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_fill_weight (spacer, 1.0f);
	add_to (self->toolbar, spacer);
	self->readout = LRG_WIDGET (lrg_label_new ("No frames recorded"));
	lrg_widget_set_name (self->readout, "profiler-readout");
	lrg_widget_set_valign (self->readout, LRG_ALIGN_CENTER);
	add_to (self->toolbar, self->readout);
	lrg_container_add_child (LRG_CONTAINER (self), self->toolbar);
	g_object_unref (self->toolbar);

	/* Graph */
	self->graph = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->graph, "profiler-graph");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->graph), &graph_funcs, self, NULL);
	lrg_container_add_child (LRG_CONTAINER (self), self->graph);
	g_object_unref (self->graph);

	/* Sections */
	self->table = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->table), "profiler-table");
	lrg_table_set_striped (self->table, TRUE);
	for (i = 0; i < G_N_ELEMENTS (columns); i++)
	{
		guint col = lrg_table_add_column (self->table, columns[i].title, i == 0 ? cell_name : cell_value,
		                                  GINT_TO_POINTER ((gint) i), NULL, columns[i].width,
		                                  LRG_TABLE_COLUMN_SORTABLE | LRG_TABLE_COLUMN_RESIZABLE |
		                                  (i == 0 ? LRG_TABLE_COLUMN_EXPAND : LRG_TABLE_COLUMN_NONE));

		lrg_table_set_sort_func (self->table, col, i == 0 ? sort_name : sort_value, GINT_TO_POINTER ((gint) i), NULL);
	}
	lrg_table_set_model (self->table, G_LIST_MODEL (self->rows));
	lrg_table_set_sort (self->table, 2, FALSE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->table));
	g_object_unref (self->table);
}

LrgEditorProfilerPanel *
lrg_editor_profiler_panel_new (LrgEditorApp *app)
{
	static const LrgCustomWidgetFuncs tap_funcs = { NULL, NULL, tap_step, NULL, { NULL } };
	LrgEditorProfilerPanel *self;
	LrgEditorActions       *actions;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_PROFILER_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->profiler = g_object_ref (lrg_profiler_get_default ());

	/* play.tick is timed by the session itself */
	lrg_editor_play_session_set_profiler (lrg_editor_app_get_play_session (app), self->profiler);

	/* The frame tap: stepped with the canvas, never drawn or hit */
	g_set_weak_pointer (&self->canvas, lrg_editor_app_get_canvas (app));
	self->tap = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->tap, "profiler-frame-tap");
	lrg_widget_set_pointer_transparent (self->tap, TRUE);
	lrg_widget_set_size (self->tap, 0.0f, 0.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), &tap_funcs, self, NULL);
	lrg_canvas_add_overlay (self->canvas, self->tap);
	g_object_unref (self->tap);

	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "profiler.pause", "Pause Profiler", "Profiler", NULL, a_profiler_pause, NULL,
	                        profiler_paused, self);
	lrg_editor_actions_add (actions, "profiler.clear", "Clear Profiler", "Profiler", NULL, a_profiler_clear, NULL,
	                        NULL, self);
	lrg_editor_actions_add (actions, "profiler.show-panel", "Profiler Panel", "Profiler", NULL, a_profiler_show, NULL,
	                        NULL, self);
	lrg_editor_server_register_method (lrg_editor_app_get_server (app), "profiler.sample", m_profiler_sample, self,
	                                   NULL);
	return self;
}

LrgProfiler *
lrg_editor_profiler_panel_get_profiler (LrgEditorProfilerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PROFILER_PANEL (self), NULL);
	return self->profiler;
}

gboolean
lrg_editor_profiler_panel_get_paused (LrgEditorProfilerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PROFILER_PANEL (self), FALSE);
	return self->paused;
}

void
lrg_editor_profiler_panel_set_paused (LrgEditorProfilerPanel *self,
                                      gboolean                paused)
{
	g_return_if_fail (LRG_IS_EDITOR_PROFILER_PANEL (self));

	self->paused = !!paused;
	editor_chip_set_checked (self->pause, self->paused);
	editor_chip_set_icon (self->pause, self->paused ? LRG_ICON_PLAY : LRG_ICON_PAUSE);
	editor_chip_set_label (self->pause, self->paused ? "Resume" : "Pause");
}

void
lrg_editor_profiler_panel_clear (LrgEditorProfilerPanel *self)
{
	g_return_if_fail (LRG_IS_EDITOR_PROFILER_PANEL (self));

	self->head = 0;
	self->n_frames = 0;
	self->hovered = -1;
	g_list_store_remove_all (self->rows);
	refresh_readout (self);
	self->clear_pending = TRUE;
	schedule_apply (self);
}

gfloat *
lrg_editor_profiler_panel_get_frames (LrgEditorProfilerPanel *self,
                                      guint                  *n_frames)
{
	gfloat *out;
	guint   i;

	g_return_val_if_fail (LRG_IS_EDITOR_PROFILER_PANEL (self), NULL);
	g_return_val_if_fail (n_frames != NULL, NULL);

	*n_frames = self->n_frames;
	out = g_new (gfloat, MAX (self->n_frames, 1u));
	for (i = 0; i < self->n_frames; i++)
		out[i] = self->frames[ring_index (self, i)];
	return out;
}

guint
lrg_editor_profiler_panel_get_n_sections (LrgEditorProfilerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PROFILER_PANEL (self), 0);
	refresh_table (self);
	return g_list_model_get_n_items (G_LIST_MODEL (self->rows));
}

gint
lrg_editor_profiler_panel_get_hovered_frame (LrgEditorProfilerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PROFILER_PANEL (self), -1);
	return self->hovered;
}
