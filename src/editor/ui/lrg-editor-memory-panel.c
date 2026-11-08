/* lrg-editor-memory-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Memory panel: a sparkline of instance counts and GPU bytes, a type
 * table, and an asset-residency table. A canvas overlay records one
 * history sample a second while the panel is shown or play is running,
 * and refreshes the tables four times a second while the panel is shown.
 * IPC and actions are registered from lrg_editor_memory_panel_new().
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-memory-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "../lrg-editor-play-session.h"
#include "../../core/lrg-engine.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-table.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <string.h>

#define PANEL_KEY         "lrg-editor-memory-panel"
#define REFRESH_INTERVAL  (0.25f)
#define SAMPLE_INTERVAL   (1.0f)
#define GRAPH_HEIGHT      (110.0f)
#define TYPE_LIMIT_DEFAULT (200)
#define TYPE_LIMIT_MAX     (100000)

/* ==========================================================================
 * Rows
 * ========================================================================== */

#define LRG_TYPE_EDITOR_MEMORY_TYPE_ROW (lrg_editor_memory_type_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorMemoryTypeRow, lrg_editor_memory_type_row, LRG, EDITOR_MEMORY_TYPE_ROW, GObject)

struct _LrgEditorMemoryTypeRow
{
	GObject  parent_instance;
	gchar   *type_name;
	guint64  count;
	guint    instance_size;
	guint64  bytes;
	gchar   *source;
};

G_DEFINE_FINAL_TYPE (LrgEditorMemoryTypeRow, lrg_editor_memory_type_row, G_TYPE_OBJECT)

static void
lrg_editor_memory_type_row_finalize (GObject *object)
{
	LrgEditorMemoryTypeRow *self = LRG_EDITOR_MEMORY_TYPE_ROW (object);

	g_free (self->type_name);
	g_free (self->source);
	G_OBJECT_CLASS (lrg_editor_memory_type_row_parent_class)->finalize (object);
}

static void
lrg_editor_memory_type_row_class_init (LrgEditorMemoryTypeRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_memory_type_row_finalize;
}

static void
lrg_editor_memory_type_row_init (LrgEditorMemoryTypeRow *self)
{
}

#define LRG_TYPE_EDITOR_MEMORY_RES_ROW (lrg_editor_memory_res_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorMemoryResRow, lrg_editor_memory_res_row, LRG, EDITOR_MEMORY_RES_ROW, GObject)

struct _LrgEditorMemoryResRow
{
	GObject  parent_instance;
	gchar   *name;
	gchar   *kind;
	guint    refs;
	guint64  cpu_bytes;
	guint64  gpu_bytes;
};

G_DEFINE_FINAL_TYPE (LrgEditorMemoryResRow, lrg_editor_memory_res_row, G_TYPE_OBJECT)

static void
lrg_editor_memory_res_row_finalize (GObject *object)
{
	LrgEditorMemoryResRow *self = LRG_EDITOR_MEMORY_RES_ROW (object);

	g_free (self->name);
	g_free (self->kind);
	G_OBJECT_CLASS (lrg_editor_memory_res_row_parent_class)->finalize (object);
}

static void
lrg_editor_memory_res_row_class_init (LrgEditorMemoryResRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_memory_res_row_finalize;
}

static void
lrg_editor_memory_res_row_init (LrgEditorMemoryResRow *self)
{
}

/* ==========================================================================
 * Panel
 * ========================================================================== */

struct _LrgEditorMemoryPanel
{
	LrgContainer       parent_instance;

	LrgEditorApp      *app;
	LrgMemoryProfiler *profiler;
	LrgCanvas         *canvas;
	LrgWidget         *tap;

	LrgWidget         *toolbar;
	LrgWidget         *readout;
	LrgWidget         *graph;
	LrgTable          *types;
	LrgTable          *resources;
	GListStore        *type_rows;
	GListStore        *res_rows;

	gfloat             since_sample;
	gfloat             since_refresh;
};

G_DEFINE_FINAL_TYPE (LrgEditorMemoryPanel, lrg_editor_memory_panel, LRG_TYPE_CONTAINER)

static gboolean
panel_shown (LrgEditorMemoryPanel *self)
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

static gint
cmp_u64 (guint64 a,
         guint64 b)
{
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}

static gchar *
cell_type (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	LrgEditorMemoryTypeRow *row = LRG_EDITOR_MEMORY_TYPE_ROW (item);

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->count);
	case 2:
		return g_strdup_printf ("%u", row->instance_size);
	case 3:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->bytes);
	case 4:
		return g_strdup (row->source);
	default:
		return g_strdup (row->type_name);
	}
}

static gint
sort_type (gconstpointer a,
           gconstpointer b,
           gpointer      user_data)
{
	LrgEditorMemoryTypeRow *ra = LRG_EDITOR_MEMORY_TYPE_ROW ((gpointer) a);
	LrgEditorMemoryTypeRow *rb = LRG_EDITOR_MEMORY_TYPE_ROW ((gpointer) b);
	gint                    c;

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		c = cmp_u64 (ra->count, rb->count);
		break;
	case 2:
		c = (ra->instance_size < rb->instance_size) ? -1 : (ra->instance_size > rb->instance_size);
		break;
	case 3:
		c = cmp_u64 (ra->bytes, rb->bytes);
		break;
	case 4:
		c = g_strcmp0 (ra->source, rb->source);
		break;
	default:
		c = g_strcmp0 (ra->type_name, rb->type_name);
		break;
	}
	if (c != 0)
		return c;
	return g_strcmp0 (ra->type_name, rb->type_name);
}

static gchar *
cell_res (LrgTable  *table,
          LrgWidget *cell,
          GObject   *item,
          guint      position,
          gpointer   user_data)
{
	LrgEditorMemoryResRow *row = LRG_EDITOR_MEMORY_RES_ROW (item);

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		return g_strdup (row->kind);
	case 2:
		return g_strdup_printf ("%u", row->refs);
	case 3:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->cpu_bytes);
	case 4:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->gpu_bytes);
	default:
		return g_strdup (row->name);
	}
}

static gint
sort_res (gconstpointer a,
          gconstpointer b,
          gpointer      user_data)
{
	LrgEditorMemoryResRow *ra = LRG_EDITOR_MEMORY_RES_ROW ((gpointer) a);
	LrgEditorMemoryResRow *rb = LRG_EDITOR_MEMORY_RES_ROW ((gpointer) b);
	gint                   c;

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		c = g_strcmp0 (ra->kind, rb->kind);
		break;
	case 2:
		c = (ra->refs < rb->refs) ? -1 : (ra->refs > rb->refs);
		break;
	case 3:
		c = cmp_u64 (ra->cpu_bytes, rb->cpu_bytes);
		break;
	case 4:
		c = cmp_u64 (ra->gpu_bytes, rb->gpu_bytes);
		break;
	default:
		c = g_strcmp0 (ra->name, rb->name);
		break;
	}
	if (c != 0)
		return c;
	return g_strcmp0 (ra->name, rb->name);
}

static void
refresh_types (LrgEditorMemoryPanel *self)
{
	g_autoptr(GPtrArray) counts = lrg_memory_profiler_get_type_counts (self->profiler);
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	gint                 col = lrg_table_get_sort_column (self->types);
	guint                i;

	for (i = 0; i < counts->len; i++)
	{
		LrgMemoryTypeCount     *src = g_ptr_array_index (counts, i);
		LrgEditorMemoryTypeRow *row = g_object_new (LRG_TYPE_EDITOR_MEMORY_TYPE_ROW, NULL);

		row->type_name = g_strdup (src->type_name);
		row->count = src->count;
		row->instance_size = src->instance_size;
		row->bytes = src->bytes;
		row->source = g_strdup (src->source);
		g_ptr_array_add (rows, row);
	}
	g_list_store_splice (self->type_rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->type_rows)),
	                     rows->pdata, rows->len);
	if (col >= 0)
		lrg_table_set_sort (self->types, col, lrg_table_get_sort_ascending (self->types));
}

static void
refresh_resources (LrgEditorMemoryPanel *self)
{
	g_autoptr(GPtrArray) resources = lrg_memory_profiler_get_resources (self->profiler);
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	gint                 col = lrg_table_get_sort_column (self->resources);
	guint                i;

	for (i = 0; i < resources->len; i++)
	{
		LrgMemoryResource     *src = g_ptr_array_index (resources, i);
		LrgEditorMemoryResRow *row = g_object_new (LRG_TYPE_EDITOR_MEMORY_RES_ROW, NULL);

		row->name = g_strdup (src->name);
		row->kind = g_strdup (src->kind);
		row->refs = src->refs;
		row->cpu_bytes = src->cpu_bytes;
		row->gpu_bytes = src->gpu_bytes;
		g_ptr_array_add (rows, row);
	}
	g_list_store_splice (self->res_rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->res_rows)),
	                     rows->pdata, rows->len);
	if (col >= 0)
		lrg_table_set_sort (self->resources, col, lrg_table_get_sort_ascending (self->resources));
}

static void
refresh_all (LrgEditorMemoryPanel *self)
{
	g_autoptr(GPtrArray) snaps = NULL;
	LrgMemoryTotals      totals;
	g_autofree gchar    *text = NULL;

	if (self->profiler == NULL || self->readout == NULL)
		return;
	refresh_types (self);
	refresh_resources (self);
	lrg_memory_profiler_get_totals (self->profiler, &totals);
	snaps = lrg_memory_profiler_list_snapshots (self->profiler);
	text = g_strdup_printf ("%" G_GUINT64_FORMAT " instances   cpu %" G_GUINT64_FORMAT
	                        "   gpu %" G_GUINT64_FORMAT "   %u snapshots",
	                        totals.instances, totals.cpu_bytes, totals.gpu_bytes,
	                        snaps->len);
	lrg_label_set_text (LRG_LABEL (self->readout), text);
}

static gfloat
series_max (const guint64 *values,
            guint          n)
{
	guint64 m;
	guint   i;

	m = 1;
	for (i = 0; i < n; i++)
	{
		if (values[i] > m)
			m = values[i];
	}
	return (gfloat) m;
}

static void
draw_series (LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             const guint64   *values,
             guint            n,
             const GrlColor  *color)
{
	gfloat scale;
	guint  i;

	if (values == NULL || n == 0)
		return;
	scale = series_max (values, n);
	for (i = 1; i < n; i++)
	{
		gfloat x0 = x + (width * (gfloat) (i - 1) / (gfloat) (n - 1));
		gfloat x1 = x + (width * (gfloat) i / (gfloat) (n - 1));
		gfloat y0 = y + height - ((gfloat) values[i - 1] / scale) * (height - 4.0f) - 2.0f;
		gfloat y1 = y + height - ((gfloat) values[i] / scale) * (height - 4.0f) - 2.0f;

		lrg_paint_list_line (list, x0, y0, x1, y1, 1.5f, color);
	}
	if (n == 1)
	{
		gfloat yy = y + height - ((gfloat) values[0] / scale) * (height - 4.0f) - 2.0f;

		lrg_paint_list_line (list, x + 4.0f, yy, x + width - 4.0f, yy, 1.5f, color);
	}
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
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	LrgTheme             *theme;
	GrlFont              *font;
	gfloat                fs;
	gfloat                radius;
	const GrlColor       *muted;
	const GrlColor       *accent;
	const GrlColor       *warn;
	g_autofree guint64   *inst = NULL;
	g_autofree guint64   *gpu = NULL;
	guint                 n = 0;

	if (self->profiler == NULL)
		return;
	theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	font = lrg_theme_get_ui_font (theme);
	fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	lrg_memory_profiler_get_history (self->profiler, NULL, 0, &inst, NULL, &gpu, NULL, &n);
	lrg_paint_list_rect (list, x, y, width, height, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_border (list, x, y, width, height, radius, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	lrg_paint_list_push_clip (list, x, y, width, height);
	if (n == 0)
	{
		const gchar *hint = "Instance counts (accent) and GPU bytes (warn) once a second";
		gfloat       tw = 0.0f;
		gfloat       th = 0.0f;

		lrg_paint_measure_text (font, hint, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, hint, x + (width - tw) * 0.5f, y + (height - fs) * 0.5f, fs, 0.0f, muted);
	}
	else
	{
		draw_series (list, x, y, width, height, inst, n, accent);
		draw_series (list, x, y, width, height, gpu, n, warn);
		lrg_paint_list_text (list, font, "instances", x + 8.0f, y + 4.0f, fs, 0.0f, accent);
		lrg_paint_list_text (list, font, "gpu", x + 8.0f, y + fs + 6.0f, fs, 0.0f, warn);
	}
	lrg_paint_list_pop_clip (list);
}

static void bind_assets (LrgEditorMemoryPanel *self);

static gboolean
tap_step (LrgCustomWidget *tap,
          gfloat           delta_time,
          gpointer         user_data)
{
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	gboolean              shown;
	gboolean              playing;

	if (self->profiler == NULL)
		return FALSE;
	/* Construction is often before the engine starts, so the manager
	 * does not exist yet. A manager a caller set is left alone. */
	if (lrg_memory_profiler_get_asset_manager (self->profiler) == NULL)
		bind_assets (self);
	shown = panel_shown (self);
	playing = (self->app != NULL && lrg_editor_app_is_playing (self->app));

	if ((shown || playing) && delta_time > 0.0f)
	{
		self->since_sample += delta_time;
		if (self->since_sample >= SAMPLE_INTERVAL)
		{
			self->since_sample = 0.0f;
			lrg_memory_profiler_record_sample (self->profiler);
		}
	}
	self->since_refresh += delta_time;
	if (shown && self->since_refresh >= REFRESH_INTERVAL)
	{
		self->since_refresh = 0.0f;
		refresh_all (self);
	}
	return FALSE;
}

static void
bind_assets (LrgEditorMemoryPanel *self)
{
	LrgEditorPlaySession *session;
	LrgEngine            *engine;
	LrgAssetManager      *manager;

	if (self->app == NULL)
		return;
	engine = NULL;
	session = lrg_editor_app_get_play_session (self->app);
	if (session != NULL)
		engine = lrg_editor_play_session_get_engine (session);
	if (engine == NULL)
		engine = lrg_engine_get_default ();
	if (engine == NULL)
		return;
	manager = lrg_engine_get_asset_manager (engine);
	if (manager != NULL)
		lrg_memory_profiler_set_asset_manager (self->profiler, manager);
}

static void
on_snapshot (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autofree gchar     *id = NULL;

	id = lrg_memory_profiler_snapshot (self->profiler, NULL);
	refresh_all (self);
}

static void
on_clear (LrgWidget *chip,
          gpointer   user_data)
{
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (user_data);

	lrg_memory_profiler_clear_snapshots (self->profiler);
	refresh_all (self);
}

static void
a_memory_snapshot (gpointer data)
{
	on_snapshot (NULL, data);
}

static void
a_memory_show (gpointer data)
{
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_MEMORY_PANEL_ID);
}

/* --------------------------------------------------------------------------
 * IPC
 * -------------------------------------------------------------------------- */

static void
json_add_u64 (JsonBuilder *b,
              const gchar *name,
              guint64      value)
{
	gint64 n;

	if (value > (guint64) G_MAXINT64)
		n = G_MAXINT64;
	else
		n = (gint64) value;
	json_builder_set_member_name (b, name);
	json_builder_add_int_value (b, n);
}

static void
add_totals (JsonBuilder           *b,
            const LrgMemoryTotals *totals)
{
	json_builder_set_member_name (b, "totals");
	json_builder_begin_object (b);
	json_add_u64 (b, "instances", totals->instances);
	json_add_u64 (b, "types", totals->types);
	json_add_u64 (b, "type_bytes", totals->type_bytes);
	json_add_u64 (b, "resources", totals->resources);
	json_add_u64 (b, "cpu_bytes", totals->cpu_bytes);
	json_add_u64 (b, "gpu_bytes", totals->gpu_bytes);
	json_builder_end_object (b);
}

static gboolean
param_string (JsonObject   *params,
              const gchar  *name,
              const gchar **out,
              GError      **error)
{
	JsonNode *node;

	*out = NULL;
	if (params == NULL || !json_object_has_member (params, name))
		return TRUE;
	node = json_object_get_member (params, name);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "%s must be a string", name);
		return FALSE;
	}
	*out = json_node_get_string (node);
	return TRUE;
}

static gboolean
param_int (JsonObject  *params,
           const gchar *name,
           gboolean    *present,
           gint64      *out,
           GError     **error)
{
	JsonNode *node;

	*present = FALSE;
	*out = 0;
	if (params == NULL || !json_object_has_member (params, name))
		return TRUE;
	node = json_object_get_member (params, name);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_INT64)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "%s must be an integer", name);
		return FALSE;
	}
	*present = TRUE;
	*out = json_node_get_int (node);
	return TRUE;
}

static gboolean
limit_param (JsonObject *params,
             gint64     *limit,
             GError    **error)
{
	gboolean present;

	if (!param_int (params, "limit", &present, limit, error))
		return FALSE;
	if (!present)
		*limit = TYPE_LIMIT_DEFAULT;
	else if (*limit < 0 || *limit > TYPE_LIMIT_MAX)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "limit must be 0 .. %d", TYPE_LIMIT_MAX);
		return FALSE;
	}
	return TRUE;
}

static void
map_io_error (GError  *local,
              GError **error)
{
	if (g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s", local->message);
	else if (g_error_matches (local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s", local->message);
	else
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s", local->message);
}

static gint
cmp_count_desc (gconstpointer a,
                gconstpointer b)
{
	const LrgMemoryTypeCount *ta = *(LrgMemoryTypeCount * const *) a;
	const LrgMemoryTypeCount *tb = *(LrgMemoryTypeCount * const *) b;
	gint                      c = cmp_u64 (tb->count, ta->count);

	if (c != 0)
		return c;
	return g_strcmp0 (ta->type_name, tb->type_name);
}

static gint
cmp_bytes_desc (gconstpointer a,
                gconstpointer b)
{
	const LrgMemoryTypeCount *ta = *(LrgMemoryTypeCount * const *) a;
	const LrgMemoryTypeCount *tb = *(LrgMemoryTypeCount * const *) b;
	gint                      c = cmp_u64 (tb->bytes, ta->bytes);

	if (c != 0)
		return c;
	return g_strcmp0 (ta->type_name, tb->type_name);
}

static gint
cmp_type_name (gconstpointer a,
               gconstpointer b)
{
	const LrgMemoryTypeCount *ta = *(LrgMemoryTypeCount * const *) a;
	const LrgMemoryTypeCount *tb = *(LrgMemoryTypeCount * const *) b;

	return g_strcmp0 (ta->type_name, tb->type_name);
}

static JsonNode *
m_memory_status (LrgEditorServer *server,
                 JsonObject      *params,
                 gpointer         user_data,
                 GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	gboolean               counts = lrg_memory_profiler_instance_counts_available ();
	const gchar           *hint;

	if (counts)
		hint = "GOBJECT_DEBUG=instance-count is active";
	else
		hint = "Instance counts are off. Set GOBJECT_DEBUG=instance-count before the process starts. "
		       "A library constructor is too late. watch_type() counts types without that variable.";

	json_builder_begin_object (b);
	json_builder_set_member_name (b, "instance_counts");
	json_builder_add_boolean_value (b, counts);
	json_builder_set_member_name (b, "tracked");
	json_builder_add_int_value (b, lrg_memory_profiler_get_tracked_instances (self->profiler));
	json_builder_set_member_name (b, "asset_manager");
	json_builder_add_boolean_value (b, lrg_memory_profiler_get_asset_manager (self->profiler) != NULL);
	json_builder_set_member_name (b, "hint");
	json_builder_add_string_value (b, hint);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_memory_types (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(GPtrArray)   counts = NULL;
	g_autoptr(GPtrArray)   ordered = NULL;
	g_autoptr(JsonBuilder) b = json_builder_new ();
	LrgMemoryTotals        totals;
	const gchar           *filter = NULL;
	const gchar           *sort = NULL;
	gint64                 limit = TYPE_LIMIT_DEFAULT;
	guint                  emitted;
	guint                  i;
	const gchar           *source;

	if (!param_string (params, "filter", &filter, error))
		return NULL;
	if (!param_string (params, "sort", &sort, error))
		return NULL;
	if (!limit_param (params, &limit, error))
		return NULL;
	if (sort != NULL && g_strcmp0 (sort, "count") != 0 && g_strcmp0 (sort, "bytes") != 0 &&
	    g_strcmp0 (sort, "name") != 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "sort must be count, bytes or name");
		return NULL;
	}
	if (sort == NULL)
		sort = "count";

	counts = lrg_memory_profiler_get_type_counts (self->profiler);
	lrg_memory_profiler_get_totals (self->profiler, &totals);

	ordered = g_ptr_array_new ();
	for (i = 0; i < counts->len; i++)
		g_ptr_array_add (ordered, g_ptr_array_index (counts, i));
	if (g_strcmp0 (sort, "bytes") == 0)
		g_ptr_array_sort (ordered, cmp_bytes_desc);
	else if (g_strcmp0 (sort, "count") == 0)
		g_ptr_array_sort (ordered, cmp_count_desc);
	else
		g_ptr_array_sort (ordered, cmp_type_name);

	source = lrg_memory_profiler_instance_counts_available () ? LRG_MEMORY_SOURCE_INSTANCE : LRG_MEMORY_SOURCE_TRACKED;
	if (counts->len > 0)
		source = ((LrgMemoryTypeCount *) g_ptr_array_index (counts, 0))->source;

	json_builder_begin_object (b);
	json_builder_set_member_name (b, "source");
	json_builder_add_string_value (b, source != NULL ? source : LRG_MEMORY_SOURCE_TRACKED);
	json_builder_set_member_name (b, "types");
	json_builder_begin_array (b);
	emitted = 0;
	for (i = 0; i < ordered->len && emitted < (guint) limit; i++)
	{
		LrgMemoryTypeCount *row = g_ptr_array_index (ordered, i);

		if (filter != NULL && filter[0] != '\0' &&
		    (row->type_name == NULL || strstr (row->type_name, filter) == NULL))
			continue;
		json_builder_begin_object (b);
		json_builder_set_member_name (b, "type");
		json_builder_add_string_value (b, row->type_name != NULL ? row->type_name : "");
		json_add_u64 (b, "count", row->count);
		json_add_u64 (b, "instance_size", row->instance_size);
		json_add_u64 (b, "bytes", row->bytes);
		json_builder_set_member_name (b, "source");
		json_builder_add_string_value (b, row->source != NULL ? row->source : "");
		json_builder_end_object (b);
		emitted++;
	}
	json_builder_end_array (b);
	add_totals (b, &totals);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static const gchar * const RES_KINDS[] = {
	"texture", "model", "mesh", "sound", "music", "font", "material", "animation", "object", "other", NULL
};

static gboolean
kind_known (const gchar *kind)
{
	guint i;

	if (kind == NULL)
		return TRUE;
	for (i = 0; RES_KINDS[i] != NULL; i++)
	{
		if (g_strcmp0 (kind, RES_KINDS[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

/* --------------------------------------------------------------------------
 * IPC: resources, snapshots, diff, history, show
 * -------------------------------------------------------------------------- */

static void
add_snap_object (JsonBuilder           *b,
                 LrgMemorySnapshotInfo *info)
{
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "id");
	json_builder_add_string_value (b, info->id != NULL ? info->id : "");
	json_builder_set_member_name (b, "label");
	json_builder_add_string_value (b, info->label != NULL ? info->label : "");
	json_builder_set_member_name (b, "time");
	json_builder_add_int_value (b, info->time_us);
	add_totals (b, &info->totals);
	json_builder_end_object (b);
}

static LrgMemorySnapshotInfo *
find_snap (GPtrArray   *snaps,
           const gchar *id)
{
	guint i;

	for (i = 0; i < snaps->len; i++)
	{
		LrgMemorySnapshotInfo *info = g_ptr_array_index (snaps, i);

		if (g_strcmp0 (info->id, id) == 0)
			return info;
	}
	return NULL;
}

static JsonNode *
m_memory_resources (LrgEditorServer *server,
                    JsonObject      *params,
                    gpointer         user_data,
                    GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(GPtrArray)   resources = NULL;
	g_autoptr(JsonBuilder) b = json_builder_new ();
	LrgMemoryTotals        totals;
	const gchar           *kind = NULL;
	guint                  i;

	if (!param_string (params, "kind", &kind, error))
		return NULL;
	if (kind != NULL && !kind_known (kind))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "unknown resource kind");
		return NULL;
	}
	resources = lrg_memory_profiler_get_resources (self->profiler);
	lrg_memory_profiler_get_totals (self->profiler, &totals);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "resources");
	json_builder_begin_array (b);
	for (i = 0; i < resources->len; i++)
	{
		LrgMemoryResource *row = g_ptr_array_index (resources, i);

		if (kind != NULL && g_strcmp0 (row->kind, kind) != 0)
			continue;
		json_builder_begin_object (b);
		json_builder_set_member_name (b, "name");
		json_builder_add_string_value (b, row->name != NULL ? row->name : "");
		json_builder_set_member_name (b, "kind");
		json_builder_add_string_value (b, row->kind != NULL ? row->kind : "");
		json_add_u64 (b, "refs", row->refs);
		json_add_u64 (b, "cpu_bytes", row->cpu_bytes);
		json_add_u64 (b, "gpu_bytes", row->gpu_bytes);
		json_builder_set_member_name (b, "details");
		json_builder_add_string_value (b, row->details != NULL ? row->details : "");
		json_builder_end_object (b);
	}
	json_builder_end_array (b);
	add_totals (b, &totals);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_memory_snapshot (LrgEditorServer *server,
                   JsonObject      *params,
                   gpointer         user_data,
                   GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	g_autoptr(GPtrArray)   snaps = NULL;
	g_autofree gchar      *id = NULL;
	LrgMemorySnapshotInfo *info;
	const gchar           *label = NULL;

	if (!param_string (params, "label", &label, error))
		return NULL;
	id = lrg_memory_profiler_snapshot (self->profiler, label);
	snaps = lrg_memory_profiler_list_snapshots (self->profiler);
	info = find_snap (snaps, id);
	refresh_all (self);
	if (info == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "snapshot %s was not stored", id);
		return NULL;
	}
	add_snap_object (b, info);
	return json_builder_get_root (b);
}

static JsonNode *
m_memory_snapshots (LrgEditorServer *server,
                    JsonObject      *params,
                    gpointer         user_data,
                    GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(GPtrArray)   snaps = lrg_memory_profiler_list_snapshots (self->profiler);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	guint                  i;

	json_builder_begin_object (b);
	json_builder_set_member_name (b, "snapshots");
	json_builder_begin_array (b);
	for (i = 0; i < snaps->len; i++)
		add_snap_object (b, g_ptr_array_index (snaps, i));
	json_builder_end_array (b);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static gint
cmp_delta_name (gconstpointer a,
                gconstpointer b)
{
	const LrgMemoryTypeDelta *ta = *(LrgMemoryTypeDelta * const *) a;
	const LrgMemoryTypeDelta *tb = *(LrgMemoryTypeDelta * const *) b;

	return g_strcmp0 (ta->type_name, tb->type_name);
}

static gint
cmp_res_delta_name (gconstpointer a,
                    gconstpointer b)
{
	const LrgMemoryResourceDelta *ta = *(LrgMemoryResourceDelta * const *) a;
	const LrgMemoryResourceDelta *tb = *(LrgMemoryResourceDelta * const *) b;
	gint                          c = g_strcmp0 (ta->name, tb->name);

	if (c != 0)
		return c;
	return g_strcmp0 (ta->kind, tb->kind);
}

static void
emit_type_deltas (JsonBuilder *b,
                  GPtrArray   *deltas,
                  guint        limit)
{
	g_autoptr(GPtrArray) ordered = g_ptr_array_new ();
	guint                i;
	guint                n;

	for (i = 0; i < deltas->len; i++)
		g_ptr_array_add (ordered, g_ptr_array_index (deltas, i));
	g_ptr_array_sort (ordered, cmp_delta_name);
	n = MIN (ordered->len, limit);
	json_builder_set_member_name (b, "types");
	json_builder_begin_array (b);
	for (i = 0; i < n; i++)
	{
		LrgMemoryTypeDelta *row = g_ptr_array_index (ordered, i);

		json_builder_begin_object (b);
		json_builder_set_member_name (b, "type");
		json_builder_add_string_value (b, row->type_name != NULL ? row->type_name : "");
		json_add_u64 (b, "before", row->before);
		json_add_u64 (b, "after", row->after);
		json_builder_set_member_name (b, "delta");
		json_builder_add_int_value (b, row->delta);
		json_builder_set_member_name (b, "bytes_delta");
		json_builder_add_int_value (b, row->bytes_delta);
		json_builder_end_object (b);
	}
	json_builder_end_array (b);
}

static void
emit_res_deltas (JsonBuilder *b,
                 GPtrArray   *deltas,
                 guint        limit)
{
	g_autoptr(GPtrArray) ordered = g_ptr_array_new ();
	guint                i;
	guint                n;

	for (i = 0; i < deltas->len; i++)
		g_ptr_array_add (ordered, g_ptr_array_index (deltas, i));
	g_ptr_array_sort (ordered, cmp_res_delta_name);
	n = MIN (ordered->len, limit);
	json_builder_set_member_name (b, "resources");
	json_builder_begin_array (b);
	for (i = 0; i < n; i++)
	{
		LrgMemoryResourceDelta *row = g_ptr_array_index (ordered, i);

		json_builder_begin_object (b);
		json_builder_set_member_name (b, "name");
		json_builder_add_string_value (b, row->name != NULL ? row->name : "");
		json_builder_set_member_name (b, "kind");
		json_builder_add_string_value (b, row->kind != NULL ? row->kind : "");
		json_builder_set_member_name (b, "change");
		json_builder_add_string_value (b, row->change != NULL ? row->change : "");
		json_builder_set_member_name (b, "bytes_delta");
		json_builder_add_int_value (b, row->bytes_delta);
		json_builder_end_object (b);
	}
	json_builder_end_array (b);
}

static JsonNode *
m_memory_diff (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(GPtrArray)   type_deltas = NULL;
	g_autoptr(GPtrArray)   res_deltas = NULL;
	g_autoptr(GError)      local = NULL;
	g_autoptr(JsonBuilder) b = json_builder_new ();
	const gchar           *from_id = NULL;
	const gchar           *to_id = NULL;
	gint64                 limit = TYPE_LIMIT_DEFAULT;

	if (!param_string (params, "from", &from_id, error))
		return NULL;
	if (!param_string (params, "to", &to_id, error))
		return NULL;
	if (!limit_param (params, &limit, error))
		return NULL;
	if (from_id == NULL || from_id[0] == '\0')
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "from is required");
		return NULL;
	}
	if (!lrg_memory_profiler_diff (self->profiler, from_id, to_id, &type_deltas, &res_deltas, &local))
	{
		map_io_error (local, error);
		return NULL;
	}
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "from");
	json_builder_add_string_value (b, from_id);
	json_builder_set_member_name (b, "to");
	json_builder_add_string_value (b, to_id != NULL ? to_id : "live");
	emit_type_deltas (b, type_deltas, (guint) limit);
	emit_res_deltas (b, res_deltas, (guint) limit);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_memory_clear (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	guint                  removed;

	removed = lrg_memory_profiler_clear_snapshots (self->profiler);
	refresh_all (self);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "removed");
	json_builder_add_int_value (b, (gint64) removed);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static void
add_u64_array (JsonBuilder   *b,
               const gchar   *name,
               const guint64 *values,
               guint          n)
{
	guint i;

	if (values == NULL)
		n = 0;
	json_builder_set_member_name (b, name);
	json_builder_begin_array (b);
	for (i = 0; i < n; i++)
	{
		gint64 v;

		if (values[i] > (guint64) G_MAXINT64)
			v = G_MAXINT64;
		else
			v = (gint64) values[i];
		json_builder_add_int_value (b, v);
	}
	json_builder_end_array (b);
}

static JsonNode *
m_memory_history (LrgEditorServer *server,
                  JsonObject      *params,
                  gpointer         user_data,
                  GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	g_autofree guint64    *instances = NULL;
	g_autofree guint64    *cpu = NULL;
	g_autofree guint64    *gpu = NULL;
	g_autofree guint64    *type_counts = NULL;
	const gchar           *type_name = NULL;
	gboolean               present = FALSE;
	gint64                 samples = 0;
	guint                  n = 0;
	guint                  max_samples = 0;

	if (!param_string (params, "type", &type_name, error))
		return NULL;
	if (!param_int (params, "samples", &present, &samples, error))
		return NULL;
	if (present && (samples < 0 || samples > G_MAXINT))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "samples must be >= 0");
		return NULL;
	}
	if (type_name != NULL && type_name[0] == '\0')
		type_name = NULL;
	/* samples 0 is an empty series. The library treats 0 as "all". */
	if (!(present && samples == 0))
	{
		if (present)
			max_samples = (guint) samples;
		lrg_memory_profiler_get_history (self->profiler, type_name, max_samples,
		                                 &instances, &cpu, &gpu,
		                                 type_name != NULL ? &type_counts : NULL, &n);
	}
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "interval");
	json_builder_add_int_value (b, 1);
	add_u64_array (b, "instances", instances, n);
	add_u64_array (b, "cpu_bytes", cpu, n);
	add_u64_array (b, "gpu_bytes", gpu, n);
	if (type_name != NULL)
		add_u64_array (b, "type", type_counts, n);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_memory_show (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	LrgEditorMemoryPanel  *self = LRG_EDITOR_MEMORY_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	gboolean               shown = FALSE;

	a_memory_show (self);
	if (self->app != NULL)
		shown = editor_app_panel_is_shown (self->app, LRG_EDITOR_MEMORY_PANEL_ID);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "panel");
	json_builder_add_string_value (b, LRG_EDITOR_MEMORY_PANEL_ID);
	json_builder_set_member_name (b, "shown");
	json_builder_add_boolean_value (b, shown);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

/* --------------------------------------------------------------------------
 * Widget
 * -------------------------------------------------------------------------- */

typedef struct
{
	const gchar *title;
	gfloat       width;
} MemColumn;

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
lrg_editor_memory_panel_layout_children (LrgContainer *container)
{
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (container);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat                w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                pad = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y));
	gfloat                gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);
	gfloat                pw;
	gfloat                ph;
	gfloat                y = pad;
	gfloat                gh;
	gfloat                rest;
	gfloat                types_h;

	if (self->toolbar == NULL || self->graph == NULL || self->types == NULL || self->resources == NULL)
		return;
	editor_panel_fit_toolbar (self->toolbar, w - pad * 2.0f);
	lrg_widget_get_preferred_size (self->toolbar, &pw, &ph);
	lrg_widget_place_in_slot (self->toolbar, pad, y, MAX (0.0f, w - pad * 2.0f), ph);
	y += ph + gap;
	gh = MIN (GRAPH_HEIGHT, MAX (40.0f, (h - y) * 0.35f));
	lrg_widget_place_in_slot (self->graph, pad, y, MAX (0.0f, w - pad * 2.0f), gh);
	y += gh + gap;
	rest = MAX (0.0f, h - y - pad);
	types_h = rest * 0.55f;
	lrg_widget_place_in_slot (LRG_WIDGET (self->types), pad, y, MAX (0.0f, w - pad * 2.0f), types_h);
	y += types_h + gap;
	lrg_widget_place_in_slot (LRG_WIDGET (self->resources), pad, y, MAX (0.0f, w - pad * 2.0f),
	                          MAX (0.0f, h - y - pad));
}

static void
lrg_editor_memory_panel_dispose (GObject *object)
{
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (object);

	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	if (self->canvas != NULL && self->tap != NULL)
	{
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), NULL, NULL, NULL);
		lrg_canvas_remove_overlay (self->canvas, self->tap);
	}
	self->tap = NULL;
	g_clear_weak_pointer (&self->canvas);
	g_clear_object (&self->profiler);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_memory_panel_parent_class)->dispose (object);
}

static void
lrg_editor_memory_panel_finalize (GObject *object)
{
	LrgEditorMemoryPanel *self = LRG_EDITOR_MEMORY_PANEL (object);

	g_clear_object (&self->type_rows);
	g_clear_object (&self->res_rows);
	G_OBJECT_CLASS (lrg_editor_memory_panel_parent_class)->finalize (object);
}

static void
lrg_editor_memory_panel_class_init (LrgEditorMemoryPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_memory_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_memory_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_memory_panel_layout_children;
}

static void
add_columns (LrgTable         *table,
             const MemColumn *columns,
             guint            n,
             LrgTableCellFunc cell,
             GCompareDataFunc sort)
{
	guint i;

	for (i = 0; i < n; i++)
	{
		guint col = lrg_table_add_column (table, columns[i].title, cell, GINT_TO_POINTER ((gint) i), NULL,
		                                  columns[i].width,
		                                  LRG_TABLE_COLUMN_SORTABLE | LRG_TABLE_COLUMN_RESIZABLE |
		                                  (i == 0 ? LRG_TABLE_COLUMN_EXPAND : LRG_TABLE_COLUMN_NONE));

		lrg_table_set_sort_func (table, col, sort, GINT_TO_POINTER ((gint) i), NULL);
	}
}

static void
lrg_editor_memory_panel_init (LrgEditorMemoryPanel *self)
{
	static const LrgCustomWidgetFuncs graph_funcs = { NULL, graph_paint, NULL, NULL, { NULL } };
	static const MemColumn type_cols[5] = {
		{ "Type", 180.0f }, { "Count", 70.0f }, { "Size", 70.0f }, { "Bytes", 90.0f }, { "Source", 110.0f }
	};
	static const MemColumn res_cols[5] = {
		{ "Name", 180.0f }, { "Kind", 90.0f }, { "Refs", 60.0f }, { "CPU", 90.0f }, { "GPU", 90.0f }
	};
	LrgWidget *spacer;
	LrgWidget *chip;

	self->type_rows = g_list_store_new (LRG_TYPE_EDITOR_MEMORY_TYPE_ROW);
	self->res_rows = g_list_store_new (LRG_TYPE_EDITOR_MEMORY_RES_ROW);

	self->toolbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->toolbar, "memory-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	chip = editor_chip_new ("memory-snapshot", LRG_ICON_DOWNLOAD, "Snapshot", on_snapshot, self);
	add_to (self->toolbar, chip);
	chip = editor_chip_new ("memory-clear", LRG_ICON_TRASH, "Clear", on_clear, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (self->toolbar, chip);
	spacer = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_fill_weight (spacer, 1.0f);
	add_to (self->toolbar, spacer);
	self->readout = LRG_WIDGET (lrg_label_new ("No sample yet"));
	lrg_widget_set_name (self->readout, "memory-readout");
	lrg_widget_set_valign (self->readout, LRG_ALIGN_CENTER);
	add_to (self->toolbar, self->readout);
	lrg_container_add_child (LRG_CONTAINER (self), self->toolbar);
	g_object_unref (self->toolbar);

	self->graph = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->graph, "memory-graph");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->graph), &graph_funcs, self, NULL);
	lrg_container_add_child (LRG_CONTAINER (self), self->graph);
	g_object_unref (self->graph);

	self->types = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->types), "memory-types");
	lrg_table_set_striped (self->types, TRUE);
	add_columns (self->types, type_cols, G_N_ELEMENTS (type_cols), cell_type, sort_type);
	lrg_table_set_model (self->types, G_LIST_MODEL (self->type_rows));
	lrg_table_set_sort (self->types, 1, FALSE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->types));
	g_object_unref (self->types);

	self->resources = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->resources), "memory-resources");
	lrg_table_set_striped (self->resources, TRUE);
	add_columns (self->resources, res_cols, G_N_ELEMENTS (res_cols), cell_res, sort_res);
	lrg_table_set_model (self->resources, G_LIST_MODEL (self->res_rows));
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->resources));
	g_object_unref (self->resources);
}

static void
register_memory (LrgEditorMemoryPanel *self)
{
	LrgEditorServer  *server = lrg_editor_app_get_server (self->app);
	LrgEditorActions *actions = lrg_editor_app_get_actions (self->app);

	lrg_editor_actions_add (actions, "memory.show-panel", "Memory Panel", "Memory", NULL,
	                        a_memory_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "memory.snapshot", "Memory Snapshot", "Memory", NULL,
	                        a_memory_snapshot, NULL, NULL, self);
	lrg_editor_server_register_method (server, "memory.status", m_memory_status, self, NULL);
	lrg_editor_server_register_method (server, "memory.types", m_memory_types, self, NULL);
	lrg_editor_server_register_method (server, "memory.resources", m_memory_resources, self, NULL);
	lrg_editor_server_register_method (server, "memory.snapshot", m_memory_snapshot, self, NULL);
	lrg_editor_server_register_method (server, "memory.snapshots", m_memory_snapshots, self, NULL);
	lrg_editor_server_register_method (server, "memory.diff", m_memory_diff, self, NULL);
	lrg_editor_server_register_method (server, "memory.clear_snapshots", m_memory_clear, self, NULL);
	lrg_editor_server_register_method (server, "memory.history", m_memory_history, self, NULL);
	lrg_editor_server_register_method (server, "memory.show", m_memory_show, self, NULL);
}

LrgEditorMemoryPanel *
lrg_editor_memory_panel_new (LrgEditorApp *app)
{
	static const LrgCustomWidgetFuncs tap_funcs = { NULL, NULL, tap_step, NULL, { NULL } };
	LrgEditorMemoryPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_get_data (G_OBJECT (app), PANEL_KEY);
	if (self != NULL)
		return g_object_ref (self);

	self = g_object_new (LRG_TYPE_EDITOR_MEMORY_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->profiler = lrg_memory_profiler_new ();
	bind_assets (self);
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	g_set_weak_pointer (&self->canvas, lrg_editor_app_get_canvas (app));
	self->tap = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->tap, "memory-frame-tap");
	lrg_widget_set_pointer_transparent (self->tap, TRUE);
	lrg_widget_set_size (self->tap, 0.0f, 0.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), &tap_funcs, self, NULL);
	if (self->canvas != NULL)
	{
		lrg_canvas_add_overlay (self->canvas, self->tap);
		g_object_unref (self->tap);
	}
	register_memory (self);
	return self;
}

LrgEditorMemoryPanel *
lrg_editor_memory_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}

LrgMemoryProfiler *
lrg_editor_memory_panel_get_profiler (LrgEditorMemoryPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MEMORY_PANEL (self), NULL);
	return self->profiler;
}
