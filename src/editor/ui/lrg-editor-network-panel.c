/* lrg-editor-network-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Network panel: a sparkline of bytes in and out, a peer table and a
 * message-type table. The canvas overlay enables the default
 * LrgNetStats while the panel is shown or pinned, unless the user
 * turned collection off. IPC and actions are registered from
 * lrg_editor_network_panel_new().
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-network-panel.h"
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-table.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <string.h>

#define PANEL_KEY        "lrg-editor-network-panel"
#define REFRESH_INTERVAL (0.25f)
#define GRAPH_HEIGHT     (110.0f)
#define WINDOW_DEFAULT   (10)

/* ==========================================================================
 * Rows
 * ========================================================================== */

#define LRG_TYPE_EDITOR_NETWORK_PEER_ROW (lrg_editor_network_peer_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorNetworkPeerRow, lrg_editor_network_peer_row, LRG, EDITOR_NETWORK_PEER_ROW, GObject)

struct _LrgEditorNetworkPeerRow
{
	GObject  parent_instance;
	gchar   *peer;
	gdouble  rtt;
	gdouble  loss;
	guint64  bytes_in;
	guint64  bytes_out;
	guint64  lost;
};

G_DEFINE_FINAL_TYPE (LrgEditorNetworkPeerRow, lrg_editor_network_peer_row, G_TYPE_OBJECT)

static void
lrg_editor_network_peer_row_finalize (GObject *object)
{
	g_free (LRG_EDITOR_NETWORK_PEER_ROW (object)->peer);
	G_OBJECT_CLASS (lrg_editor_network_peer_row_parent_class)->finalize (object);
}

static void
lrg_editor_network_peer_row_class_init (LrgEditorNetworkPeerRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_network_peer_row_finalize;
}

static void
lrg_editor_network_peer_row_init (LrgEditorNetworkPeerRow *self)
{
}

#define LRG_TYPE_EDITOR_NETWORK_MSG_ROW (lrg_editor_network_msg_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorNetworkMsgRow, lrg_editor_network_msg_row, LRG, EDITOR_NETWORK_MSG_ROW, GObject)

struct _LrgEditorNetworkMsgRow
{
	GObject  parent_instance;
	gchar   *type;
	guint64  packets_in;
	guint64  packets_out;
	guint64  bytes_in;
	guint64  bytes_out;
};

G_DEFINE_FINAL_TYPE (LrgEditorNetworkMsgRow, lrg_editor_network_msg_row, G_TYPE_OBJECT)

static void
lrg_editor_network_msg_row_finalize (GObject *object)
{
	g_free (LRG_EDITOR_NETWORK_MSG_ROW (object)->type);
	G_OBJECT_CLASS (lrg_editor_network_msg_row_parent_class)->finalize (object);
}

static void
lrg_editor_network_msg_row_class_init (LrgEditorNetworkMsgRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_network_msg_row_finalize;
}

static void
lrg_editor_network_msg_row_init (LrgEditorNetworkMsgRow *self)
{
}

/* ==========================================================================
 * Panel
 * ========================================================================== */

struct _LrgEditorNetworkPanel
{
	LrgContainer   parent_instance;

	LrgEditorApp  *app;
	LrgNetStats   *stats;
	LrgCanvas     *canvas;
	LrgWidget     *tap;

	LrgWidget     *toolbar;
	LrgWidget     *toggle;
	LrgWidget     *readout;
	LrgWidget     *graph;
	LrgTable      *peers;
	LrgTable      *messages;
	GListStore    *peer_rows;
	GListStore    *message_rows;

	gboolean       pinned;
	gboolean       suppressed;
	gboolean       we_enabled;
	gfloat         since_refresh;
};

G_DEFINE_FINAL_TYPE (LrgEditorNetworkPanel, lrg_editor_network_panel, LRG_TYPE_CONTAINER)

typedef struct
{
	const gchar *title;
	gfloat       width;
} NetColumn;

static gboolean
panel_shown (LrgEditorNetworkPanel *self)
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
param_bool (JsonObject  *params,
            const gchar *name,
            gboolean    *present,
            gboolean    *out,
            GError     **error)
{
	JsonNode *node;

	*present = FALSE;
	*out = FALSE;
	if (params == NULL || !json_object_has_member (params, name))
		return TRUE;
	node = json_object_get_member (params, name);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_BOOLEAN)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "%s must be a boolean", name);
		return FALSE;
	}
	*present = TRUE;
	*out = json_node_get_boolean (node);
	return TRUE;
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
cell_peer (LrgTable  *table,
           LrgWidget *cell,
           GObject   *item,
           guint      position,
           gpointer   user_data)
{
	LrgEditorNetworkPeerRow *row = LRG_EDITOR_NETWORK_PEER_ROW (item);

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		return g_strdup_printf ("%.1f", row->rtt);
	case 2:
		return g_strdup_printf ("%.3f", row->loss);
	case 3:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->bytes_in);
	case 4:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->bytes_out);
	default:
		return g_strdup (row->peer);
	}
}

static gint
sort_peer (gconstpointer a,
           gconstpointer b,
           gpointer      user_data)
{
	LrgEditorNetworkPeerRow *ra = LRG_EDITOR_NETWORK_PEER_ROW ((gpointer) a);
	LrgEditorNetworkPeerRow *rb = LRG_EDITOR_NETWORK_PEER_ROW ((gpointer) b);
	gint                     c = 0;

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		c = (ra->rtt < rb->rtt) ? -1 : (ra->rtt > rb->rtt);
		break;
	case 2:
		c = (ra->loss < rb->loss) ? -1 : (ra->loss > rb->loss);
		break;
	case 3:
		c = cmp_u64 (ra->bytes_in, rb->bytes_in);
		break;
	case 4:
		c = cmp_u64 (ra->bytes_out, rb->bytes_out);
		break;
	default:
		c = g_strcmp0 (ra->peer, rb->peer);
		break;
	}
	if (c != 0)
		return c;
	return g_strcmp0 (ra->peer, rb->peer);
}

static gchar *
cell_msg (LrgTable  *table,
          LrgWidget *cell,
          GObject   *item,
          guint      position,
          gpointer   user_data)
{
	LrgEditorNetworkMsgRow *row = LRG_EDITOR_NETWORK_MSG_ROW (item);

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->packets_in);
	case 2:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->packets_out);
	case 3:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->bytes_in);
	case 4:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, row->bytes_out);
	default:
		return g_strdup (row->type);
	}
}

static gint
sort_msg (gconstpointer a,
          gconstpointer b,
          gpointer      user_data)
{
	LrgEditorNetworkMsgRow *ra = LRG_EDITOR_NETWORK_MSG_ROW ((gpointer) a);
	LrgEditorNetworkMsgRow *rb = LRG_EDITOR_NETWORK_MSG_ROW ((gpointer) b);
	gint                    c = 0;

	switch (GPOINTER_TO_INT (user_data))
	{
	case 1:
		c = cmp_u64 (ra->packets_in, rb->packets_in);
		break;
	case 2:
		c = cmp_u64 (ra->packets_out, rb->packets_out);
		break;
	case 3:
		c = cmp_u64 (ra->bytes_in, rb->bytes_in);
		break;
	case 4:
		c = cmp_u64 (ra->bytes_out, rb->bytes_out);
		break;
	default:
		c = g_strcmp0 (ra->type, rb->type);
		break;
	}
	if (c != 0)
		return c;
	return g_strcmp0 (ra->type, rb->type);
}

static void
refresh_peers (LrgEditorNetworkPanel *self)
{
	g_auto(GStrv)    peers = lrg_net_stats_get_peers (self->stats);
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	gint             col = lrg_table_get_sort_column (self->peers);
	guint            i;

	for (i = 0; peers != NULL && peers[i] != NULL; i++)
	{
		LrgEditorNetworkPeerRow *row = g_object_new (LRG_TYPE_EDITOR_NETWORK_PEER_ROW, NULL);
		LrgNetCounters           counters;
		gdouble                  rtt = 0.0;

		row->peer = g_strdup (peers[i]);
		lrg_net_stats_get_totals (self->stats, peers[i], NULL, &counters);
		lrg_net_stats_get_rtt (self->stats, peers[i], &rtt, NULL, NULL, NULL);
		row->rtt = rtt;
		row->loss = lrg_net_stats_get_loss (self->stats, peers[i]);
		row->bytes_in = counters.bytes_in;
		row->bytes_out = counters.bytes_out;
		row->lost = counters.lost;
		g_ptr_array_add (rows, row);
	}
	g_list_store_splice (self->peer_rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->peer_rows)),
	                     rows->pdata, rows->len);
	if (col >= 0)
		lrg_table_set_sort (self->peers, col, lrg_table_get_sort_ascending (self->peers));
}

static void
refresh_messages (LrgEditorNetworkPanel *self)
{
	g_auto(GStrv)    types = lrg_net_stats_get_message_types (self->stats, NULL);
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	gint             col = lrg_table_get_sort_column (self->messages);
	guint            i;

	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		LrgEditorNetworkMsgRow *row = g_object_new (LRG_TYPE_EDITOR_NETWORK_MSG_ROW, NULL);
		LrgNetCounters          counters;

		row->type = g_strdup (types[i]);
		lrg_net_stats_get_totals (self->stats, NULL, types[i], &counters);
		row->packets_in = counters.packets_in;
		row->packets_out = counters.packets_out;
		row->bytes_in = counters.bytes_in;
		row->bytes_out = counters.bytes_out;
		g_ptr_array_add (rows, row);
	}
	g_list_store_splice (self->message_rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->message_rows)),
	                     rows->pdata, rows->len);
	if (col >= 0)
		lrg_table_set_sort (self->messages, col, lrg_table_get_sort_ascending (self->messages));
}

static void
refresh_all (LrgEditorNetworkPanel *self)
{
	LrgNetCounters    totals;
	g_autofree gchar *text = NULL;

	if (self->stats == NULL || self->readout == NULL)
		return;
	refresh_peers (self);
	refresh_messages (self);
	lrg_net_stats_get_totals (self->stats, NULL, NULL, &totals);
	text = g_strdup_printf ("in %" G_GUINT64_FORMAT "   out %" G_GUINT64_FORMAT "   lost %" G_GUINT64_FORMAT,
	                        totals.bytes_in, totals.bytes_out, totals.lost);
	lrg_label_set_text (LRG_LABEL (self->readout), text);
	if (self->toggle != NULL)
	{
		gboolean on = lrg_net_stats_get_enabled (self->stats);

		editor_chip_set_checked (self->toggle, on);
		editor_chip_set_label (self->toggle, on ? "Recording" : "Idle");
	}
}

static void
apply_collect (LrgEditorNetworkPanel *self)
{
	gboolean shown;
	gboolean want;
	gboolean on;

	if (self->stats == NULL)
		return;
	shown = panel_shown (self);
	want = (shown || self->pinned) && !self->suppressed;
	on = lrg_net_stats_get_enabled (self->stats);
	if (want && !on)
	{
		lrg_net_stats_set_enabled (self->stats, TRUE);
		self->we_enabled = TRUE;
	}
	else if (!want && self->we_enabled && on)
	{
		lrg_net_stats_set_enabled (self->stats, FALSE);
		self->we_enabled = FALSE;
	}
}

static void
set_collect (LrgEditorNetworkPanel *self,
             gboolean               enabled)
{
	self->pinned = enabled;
	self->suppressed = !enabled;
	lrg_net_stats_set_enabled (self->stats, enabled);
	self->we_enabled = enabled;
	if (self->toggle != NULL)
	{
		editor_chip_set_checked (self->toggle, enabled);
		editor_chip_set_label (self->toggle, enabled ? "Recording" : "Idle");
	}
}

static void
graph_scope (LrgEditorNetworkPanel *self,
             gchar                **peer,
             gchar                **type)
{
	guint sel;

	*peer = NULL;
	*type = NULL;
	if (self->peers == NULL || self->messages == NULL)
		return;
	sel = lrg_table_get_selected (self->peers);
	if (sel != LRG_TABLE_INVALID_POSITION &&
	    sel < g_list_model_get_n_items (G_LIST_MODEL (self->peer_rows)))
	{
		g_autoptr(LrgEditorNetworkPeerRow) row = g_list_model_get_item (G_LIST_MODEL (self->peer_rows), sel);

		*peer = g_strdup (row->peer);
		return;
	}
	sel = lrg_table_get_selected (self->messages);
	if (sel != LRG_TABLE_INVALID_POSITION &&
	    sel < g_list_model_get_n_items (G_LIST_MODEL (self->message_rows)))
	{
		g_autoptr(LrgEditorNetworkMsgRow) row = g_list_model_get_item (G_LIST_MODEL (self->message_rows), sel);

		*type = g_strdup (row->type);
	}
}

static gdouble
series_max_d (const gdouble *values,
              guint          n)
{
	gdouble m;
	guint   i;

	m = 1.0;
	for (i = 0; i < n; i++)
	{
		if (values[i] > m)
			m = values[i];
	}
	return m;
}

static void
draw_series_d (LrgPaintList   *list,
               gfloat          x,
               gfloat          y,
               gfloat          width,
               gfloat          height,
               const gdouble  *values,
               guint           n,
               const GrlColor *color)
{
	gdouble scale;
	guint   i;

	if (values == NULL || n == 0)
		return;
	scale = series_max_d (values, n);
	for (i = 1; i < n; i++)
	{
		gfloat x0 = x + (width * (gfloat) (i - 1) / (gfloat) (n - 1));
		gfloat x1 = x + (width * (gfloat) i / (gfloat) (n - 1));
		gfloat y0 = y + height - ((gfloat) (values[i - 1] / scale)) * (height - 4.0f) - 2.0f;
		gfloat y1 = y + height - ((gfloat) (values[i] / scale)) * (height - 4.0f) - 2.0f;

		lrg_paint_list_line (list, x0, y0, x1, y1, 1.5f, color);
	}
	if (n == 1)
	{
		gfloat yy = y + height - ((gfloat) (values[0] / scale)) * (height - 4.0f) - 2.0f;

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
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);
	LrgTheme              *theme;
	GrlFont               *font;
	gfloat                 fs;
	gfloat                 radius;
	const GrlColor        *muted;
	const GrlColor        *accent;
	const GrlColor        *warn;
	g_autofree gchar      *peer = NULL;
	g_autofree gchar      *type = NULL;
	g_autofree gdouble    *in_values = NULL;
	g_autofree gdouble    *out_values = NULL;
	guint                  n_in = 0;
	guint                  n_out = 0;

	if (self->stats == NULL)
		return;
	theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	font = lrg_theme_get_ui_font (theme);
	fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	graph_scope (self, &peer, &type);
	in_values = lrg_net_stats_get_history (self->stats, peer, peer != NULL ? NULL : type,
	                                       LRG_NET_STATS_METRIC_BYTES_IN, &n_in);
	out_values = lrg_net_stats_get_history (self->stats, peer, peer != NULL ? NULL : type,
	                                        LRG_NET_STATS_METRIC_BYTES_OUT, &n_out);
	lrg_paint_list_rect (list, x, y, width, height, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_border (list, x, y, width, height, radius, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	lrg_paint_list_push_clip (list, x, y, width, height);
	if (n_in == 0 && n_out == 0)
	{
		const gchar *hint = "Bytes in (accent) and out (warn), one point per second";
		gfloat       tw = 0.0f;
		gfloat       th = 0.0f;

		lrg_paint_measure_text (font, hint, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, hint, x + (width - tw) * 0.5f, y + (height - fs) * 0.5f, fs, 0.0f, muted);
	}
	else
	{
		draw_series_d (list, x, y, width, height, in_values, n_in, accent);
		draw_series_d (list, x, y, width, height, out_values, n_out, warn);
		lrg_paint_list_text (list, font, "in", x + 8.0f, y + 4.0f, fs, 0.0f, accent);
		lrg_paint_list_text (list, font, "out", x + 8.0f, y + fs + 6.0f, fs, 0.0f, warn);
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
tap_step (LrgCustomWidget *tap,
          gfloat           delta_time,
          gpointer         user_data)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);
	gboolean               shown;

	if (self->stats == NULL)
		return FALSE;
	apply_collect (self);
	shown = panel_shown (self);
	self->since_refresh += delta_time;
	if (shown && self->since_refresh >= REFRESH_INTERVAL)
	{
		self->since_refresh = 0.0f;
		refresh_all (self);
	}
	return FALSE;
}

static void
on_toggle (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);

	set_collect (self, !lrg_net_stats_get_enabled (self->stats));
}

static void
on_reset (LrgWidget *chip,
          gpointer   user_data)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);

	lrg_net_stats_reset (self->stats);
	refresh_all (self);
}

static void
a_network_toggle (gpointer data)
{
	on_toggle (NULL, data);
}

static void
a_network_reset (gpointer data)
{
	on_reset (NULL, data);
}

static void
a_network_show (gpointer data)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_NETWORK_PANEL_ID);
}

static gboolean
network_enabled (gpointer data)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (data);

	return self->stats != NULL && lrg_net_stats_get_enabled (self->stats);
}

/* --------------------------------------------------------------------------
 * IPC
 * -------------------------------------------------------------------------- */

static void
add_counter_object (JsonBuilder           *b,
                    const gchar           *name,
                    const LrgNetCounters  *counters)
{
	json_builder_set_member_name (b, name);
	json_builder_begin_object (b);
	json_add_u64 (b, "packets_in", counters->packets_in);
	json_add_u64 (b, "packets_out", counters->packets_out);
	json_add_u64 (b, "bytes_in", counters->bytes_in);
	json_add_u64 (b, "bytes_out", counters->bytes_out);
	json_add_u64 (b, "lost", counters->lost);
	json_builder_end_object (b);
}

static void
add_rates (JsonBuilder          *b,
           const LrgNetCounters *counters,
           guint                 seconds)
{
	gdouble d = (gdouble) MAX (seconds, 1u);

	json_builder_set_member_name (b, "rates");
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "bytes_in");
	json_builder_add_double_value (b, (gdouble) counters->bytes_in / d);
	json_builder_set_member_name (b, "bytes_out");
	json_builder_add_double_value (b, (gdouble) counters->bytes_out / d);
	json_builder_set_member_name (b, "packets_in");
	json_builder_add_double_value (b, (gdouble) counters->packets_in / d);
	json_builder_set_member_name (b, "packets_out");
	json_builder_add_double_value (b, (gdouble) counters->packets_out / d);
	json_builder_end_object (b);
}

static void
add_peer_object (JsonBuilder *b,
                 LrgNetStats *stats,
                 const gchar *peer)
{
	LrgNetCounters counters;
	gdouble        last = 0.0;
	gdouble        avg = 0.0;
	gdouble        min_ms = 0.0;
	gdouble        max_ms = 0.0;

	lrg_net_stats_get_totals (stats, peer, NULL, &counters);
	lrg_net_stats_get_rtt (stats, peer, &last, &avg, &min_ms, &max_ms);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "peer");
	json_builder_add_string_value (b, peer != NULL ? peer : "");
	json_builder_set_member_name (b, "connected");
	json_builder_add_boolean_value (b, lrg_net_stats_get_peer_connected (stats, peer));
	json_builder_set_member_name (b, "rtt_ms");
	json_builder_add_double_value (b, last);
	json_builder_set_member_name (b, "rtt_avg_ms");
	json_builder_add_double_value (b, avg);
	json_builder_set_member_name (b, "rtt_min_ms");
	json_builder_add_double_value (b, min_ms);
	json_builder_set_member_name (b, "rtt_max_ms");
	json_builder_add_double_value (b, max_ms);
	json_builder_set_member_name (b, "loss");
	json_builder_add_double_value (b, lrg_net_stats_get_loss (stats, peer));
	json_add_u64 (b, "packets_in", counters.packets_in);
	json_add_u64 (b, "packets_out", counters.packets_out);
	json_add_u64 (b, "bytes_in", counters.bytes_in);
	json_add_u64 (b, "bytes_out", counters.bytes_out);
	json_add_u64 (b, "lost", counters.lost);
	json_builder_end_object (b);
}

static void
add_message_object (JsonBuilder          *b,
                    const gchar          *type,
                    const LrgNetCounters *counters)
{
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "type");
	json_builder_add_string_value (b, type != NULL ? type : "");
	json_add_u64 (b, "packets_in", counters->packets_in);
	json_add_u64 (b, "packets_out", counters->packets_out);
	json_add_u64 (b, "bytes_in", counters->bytes_in);
	json_add_u64 (b, "bytes_out", counters->bytes_out);
	json_builder_end_object (b);
}

static JsonNode *
m_network_stats (LrgEditorServer *server,
                 JsonObject      *params,
                 gpointer         user_data,
                 GError         **error)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	LrgNetCounters         totals;
	LrgNetCounters         windowed;
	const gchar           *peer = NULL;
	gboolean               present = FALSE;
	gint64                 window = WINDOW_DEFAULT;
	guint                  seconds;

	if (!param_string (params, "peer", &peer, error))
		return NULL;
	if (!param_int (params, "window", &present, &window, error))
		return NULL;
	if (!present)
		window = WINDOW_DEFAULT;
	else if (window < 1 || window > G_MAXINT)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "window must be >= 1");
		return NULL;
	}
	if (peer != NULL && !lrg_net_stats_get_totals (self->stats, peer, NULL, &totals))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "Unknown peer '%s'", peer);
		return NULL;
	}
	seconds = (guint) window;
	if (peer == NULL)
		lrg_net_stats_get_totals (self->stats, NULL, NULL, &totals);
	lrg_net_stats_get_window_counters (self->stats, peer, NULL, seconds, &windowed);

	json_builder_begin_object (b);
	json_builder_set_member_name (b, "enabled");
	json_builder_add_boolean_value (b, lrg_net_stats_get_enabled (self->stats));
	json_builder_set_member_name (b, "window");
	json_builder_add_int_value (b, (gint64) seconds);
	add_counter_object (b, "totals", &totals);
	add_rates (b, &windowed, seconds);
	json_builder_set_member_name (b, "peers");
	json_builder_begin_array (b);
	if (peer != NULL)
		add_peer_object (b, self->stats, peer);
	else
	{
		g_auto(GStrv) peers = lrg_net_stats_get_peers (self->stats);
		guint         i;

		for (i = 0; peers != NULL && peers[i] != NULL; i++)
			add_peer_object (b, self->stats, peers[i]);
	}
	json_builder_end_array (b);
	json_builder_set_member_name (b, "messages");
	json_builder_begin_array (b);
	{
		g_auto(GStrv) types = lrg_net_stats_get_message_types (self->stats, peer);
		guint         i;

		for (i = 0; types != NULL && types[i] != NULL; i++)
		{
			LrgNetCounters counters;

			lrg_net_stats_get_totals (self->stats, peer, types[i], &counters);
			add_message_object (b, types[i], &counters);
		}
	}
	json_builder_end_array (b);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static gboolean
metric_from_name (const gchar       *name,
                  LrgNetStatsMetric *out)
{
	g_autofree gchar *dashed = NULL;
	gchar            *p;

	if (name == NULL)
		return FALSE;
	dashed = g_strdup (name);
	for (p = dashed; *p != '\0'; p++)
	{
		if (*p == '_')
			*p = '-';
	}
	if (g_strcmp0 (dashed, "bytes-in") == 0)
		*out = LRG_NET_STATS_METRIC_BYTES_IN;
	else if (g_strcmp0 (dashed, "bytes-out") == 0)
		*out = LRG_NET_STATS_METRIC_BYTES_OUT;
	else if (g_strcmp0 (dashed, "packets-in") == 0)
		*out = LRG_NET_STATS_METRIC_PACKETS_IN;
	else if (g_strcmp0 (dashed, "packets-out") == 0)
		*out = LRG_NET_STATS_METRIC_PACKETS_OUT;
	else if (g_strcmp0 (dashed, "rtt") == 0)
		*out = LRG_NET_STATS_METRIC_RTT;
	else if (g_strcmp0 (dashed, "lost") == 0)
		*out = LRG_NET_STATS_METRIC_LOST;
	else
		return FALSE;
	return TRUE;
}

static const gchar *
metric_nick (LrgNetStatsMetric metric)
{
	switch (metric)
	{
	case LRG_NET_STATS_METRIC_BYTES_IN:
		return "bytes-in";
	case LRG_NET_STATS_METRIC_BYTES_OUT:
		return "bytes-out";
	case LRG_NET_STATS_METRIC_PACKETS_IN:
		return "packets-in";
	case LRG_NET_STATS_METRIC_PACKETS_OUT:
		return "packets-out";
	case LRG_NET_STATS_METRIC_RTT:
		return "rtt";
	case LRG_NET_STATS_METRIC_LOST:
		return "lost";
	default:
		return "bytes-in";
	}
}

static JsonNode *
m_network_history (LrgEditorServer *server,
                   JsonObject      *params,
                   gpointer         user_data,
                   GError         **error)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	g_autofree gdouble    *values = NULL;
	const gchar           *peer = NULL;
	const gchar           *type = NULL;
	const gchar           *metric_name = NULL;
	LrgNetStatsMetric      metric = LRG_NET_STATS_METRIC_BYTES_IN;
	gboolean               present = FALSE;
	gint64                 seconds = 0;
	guint                  n = 0;
	guint                  start = 0;
	guint                  i;
	LrgNetCounters         probe;

	if (!param_string (params, "peer", &peer, error))
		return NULL;
	if (!param_string (params, "type", &type, error))
		return NULL;
	if (!param_string (params, "metric", &metric_name, error))
		return NULL;
	if (!param_int (params, "seconds", &present, &seconds, error))
		return NULL;
	if (metric_name == NULL || !metric_from_name (metric_name, &metric))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "metric is required");
		return NULL;
	}
	if (present && (seconds < 1 || seconds > G_MAXINT))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "seconds must be >= 1");
		return NULL;
	}
	if (peer != NULL && !lrg_net_stats_get_totals (self->stats, peer, NULL, &probe))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "Unknown peer '%s'", peer);
		return NULL;
	}
	/* A peer history has no per-type buckets. */
	values = lrg_net_stats_get_history (self->stats, peer, peer != NULL ? NULL : type, metric, &n);
	if (present && n > (guint) seconds)
		start = n - (guint) seconds;
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "metric");
	json_builder_add_string_value (b, metric_nick (metric));
	json_builder_set_member_name (b, "interval");
	json_builder_add_int_value (b, 1);
	json_builder_set_member_name (b, "values");
	json_builder_begin_array (b);
	for (i = start; values != NULL && i < n; i++)
		json_builder_add_double_value (b, values[i]);
	json_builder_end_array (b);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_network_enable (LrgEditorServer *server,
                  JsonObject      *params,
                  gpointer         user_data,
                  GError         **error)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	gboolean               present = FALSE;
	gboolean               enabled = FALSE;

	if (!param_bool (params, "enabled", &present, &enabled, error))
		return NULL;
	if (!present)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "enabled is required");
		return NULL;
	}
	set_collect (self, enabled);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "enabled");
	json_builder_add_boolean_value (b, lrg_net_stats_get_enabled (self->stats));
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_network_reset (LrgEditorServer *server,
                 JsonObject      *params,
                 gpointer         user_data,
                 GError         **error)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();

	lrg_net_stats_reset (self->stats);
	refresh_all (self);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "reset");
	json_builder_add_boolean_value (b, TRUE);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_network_show (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (user_data);
	g_autoptr(JsonBuilder) b = json_builder_new ();
	gboolean               shown = FALSE;

	a_network_show (self);
	if (self->app != NULL)
		shown = editor_app_panel_is_shown (self->app, LRG_EDITOR_NETWORK_PANEL_ID);
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "panel");
	json_builder_add_string_value (b, LRG_EDITOR_NETWORK_PANEL_ID);
	json_builder_set_member_name (b, "shown");
	json_builder_add_boolean_value (b, shown);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

/* --------------------------------------------------------------------------
 * Widget
 * -------------------------------------------------------------------------- */

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
lrg_editor_network_panel_layout_children (LrgContainer *container)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (container);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat                 w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                 h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                 pad = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y));
	gfloat                 gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);
	gfloat                 pw;
	gfloat                 ph;
	gfloat                 y = pad;
	gfloat                 gh;
	gfloat                 rest;
	gfloat                 peers_h;

	if (self->toolbar == NULL || self->graph == NULL || self->peers == NULL || self->messages == NULL)
		return;
	editor_panel_fit_toolbar (self->toolbar, w - pad * 2.0f);
	lrg_widget_get_preferred_size (self->toolbar, &pw, &ph);
	lrg_widget_place_in_slot (self->toolbar, pad, y, MAX (0.0f, w - pad * 2.0f), ph);
	y += ph + gap;
	gh = MIN (GRAPH_HEIGHT, MAX (40.0f, (h - y) * 0.35f));
	lrg_widget_place_in_slot (self->graph, pad, y, MAX (0.0f, w - pad * 2.0f), gh);
	y += gh + gap;
	rest = MAX (0.0f, h - y - pad);
	peers_h = rest * 0.55f;
	lrg_widget_place_in_slot (LRG_WIDGET (self->peers), pad, y, MAX (0.0f, w - pad * 2.0f), peers_h);
	y += peers_h + gap;
	lrg_widget_place_in_slot (LRG_WIDGET (self->messages), pad, y, MAX (0.0f, w - pad * 2.0f),
	                          MAX (0.0f, h - y - pad));
}

static void
lrg_editor_network_panel_dispose (GObject *object)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (object);

	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	if (self->canvas != NULL && self->tap != NULL)
	{
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), NULL, NULL, NULL);
		lrg_canvas_remove_overlay (self->canvas, self->tap);
	}
	self->tap = NULL;
	g_clear_weak_pointer (&self->canvas);
	if (self->stats != NULL && self->we_enabled)
		lrg_net_stats_set_enabled (self->stats, FALSE);
	self->we_enabled = FALSE;
	self->stats = NULL;
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_network_panel_parent_class)->dispose (object);
}

static void
lrg_editor_network_panel_finalize (GObject *object)
{
	LrgEditorNetworkPanel *self = LRG_EDITOR_NETWORK_PANEL (object);

	g_clear_object (&self->peer_rows);
	g_clear_object (&self->message_rows);
	G_OBJECT_CLASS (lrg_editor_network_panel_parent_class)->finalize (object);
}

static void
lrg_editor_network_panel_class_init (LrgEditorNetworkPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_network_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_network_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_network_panel_layout_children;
}

static void
add_columns (LrgTable         *table,
             const NetColumn *columns,
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
lrg_editor_network_panel_init (LrgEditorNetworkPanel *self)
{
	static const LrgCustomWidgetFuncs graph_funcs = { NULL, graph_paint, NULL, NULL, { NULL } };
	static const NetColumn peer_cols[5] = {
		{ "Peer", 160.0f }, { "RTT", 70.0f }, { "Loss", 70.0f }, { "In", 80.0f }, { "Out", 80.0f }
	};
	static const NetColumn msg_cols[5] = {
		{ "Type", 140.0f }, { "In", 70.0f }, { "Out", 70.0f }, { "Bytes in", 90.0f }, { "Bytes out", 90.0f }
	};
	LrgWidget *spacer;
	LrgWidget *chip;

	self->peer_rows = g_list_store_new (LRG_TYPE_EDITOR_NETWORK_PEER_ROW);
	self->message_rows = g_list_store_new (LRG_TYPE_EDITOR_NETWORK_MSG_ROW);

	self->toolbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->toolbar, "network-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	self->toggle = editor_chip_new ("network-toggle", LRG_ICON_UPLOAD, "Idle", on_toggle, self);
	add_to (self->toolbar, self->toggle);
	chip = editor_chip_new ("network-reset", LRG_ICON_REFRESH, "Reset", on_reset, self);
	add_to (self->toolbar, chip);
	spacer = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_fill_weight (spacer, 1.0f);
	add_to (self->toolbar, spacer);
	self->readout = LRG_WIDGET (lrg_label_new ("No traffic"));
	lrg_widget_set_name (self->readout, "network-readout");
	lrg_widget_set_valign (self->readout, LRG_ALIGN_CENTER);
	add_to (self->toolbar, self->readout);
	lrg_container_add_child (LRG_CONTAINER (self), self->toolbar);
	g_object_unref (self->toolbar);

	self->graph = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->graph, "network-graph");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->graph), &graph_funcs, self, NULL);
	lrg_container_add_child (LRG_CONTAINER (self), self->graph);
	g_object_unref (self->graph);

	self->peers = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->peers), "network-peers");
	lrg_table_set_striped (self->peers, TRUE);
	add_columns (self->peers, peer_cols, G_N_ELEMENTS (peer_cols), cell_peer, sort_peer);
	lrg_table_set_model (self->peers, G_LIST_MODEL (self->peer_rows));
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->peers));
	g_object_unref (self->peers);

	self->messages = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->messages), "network-messages");
	lrg_table_set_striped (self->messages, TRUE);
	add_columns (self->messages, msg_cols, G_N_ELEMENTS (msg_cols), cell_msg, sort_msg);
	lrg_table_set_model (self->messages, G_LIST_MODEL (self->message_rows));
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->messages));
	g_object_unref (self->messages);
}

static void
register_network (LrgEditorNetworkPanel *self)
{
	LrgEditorServer  *server = lrg_editor_app_get_server (self->app);
	LrgEditorActions *actions = lrg_editor_app_get_actions (self->app);

	lrg_editor_actions_add (actions, "network.show-panel", "Network Panel", "Network", NULL,
	                        a_network_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "network.toggle", "Network Recording", "Network", NULL,
	                        a_network_toggle, NULL, network_enabled, self);
	lrg_editor_actions_add (actions, "network.reset", "Reset Network Stats", "Network", NULL,
	                        a_network_reset, NULL, NULL, self);
	lrg_editor_server_register_method (server, "network.stats", m_network_stats, self, NULL);
	lrg_editor_server_register_method (server, "network.history", m_network_history, self, NULL);
	lrg_editor_server_register_method (server, "network.enable", m_network_enable, self, NULL);
	lrg_editor_server_register_method (server, "network.reset", m_network_reset, self, NULL);
	lrg_editor_server_register_method (server, "network.show", m_network_show, self, NULL);
}

LrgEditorNetworkPanel *
lrg_editor_network_panel_new (LrgEditorApp *app)
{
	static const LrgCustomWidgetFuncs tap_funcs = { NULL, NULL, tap_step, NULL, { NULL } };
	LrgEditorNetworkPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_get_data (G_OBJECT (app), PANEL_KEY);
	if (self != NULL)
		return g_object_ref (self);

	self = g_object_new (LRG_TYPE_EDITOR_NETWORK_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->stats = lrg_net_stats_get_default ();
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	g_set_weak_pointer (&self->canvas, lrg_editor_app_get_canvas (app));
	self->tap = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->tap, "network-frame-tap");
	lrg_widget_set_pointer_transparent (self->tap, TRUE);
	lrg_widget_set_size (self->tap, 0.0f, 0.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->tap), &tap_funcs, self, NULL);
	if (self->canvas != NULL)
	{
		lrg_canvas_add_overlay (self->canvas, self->tap);
		g_object_unref (self->tap);
	}
	register_network (self);
	return self;
}

LrgEditorNetworkPanel *
lrg_editor_network_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}

LrgNetStats *
lrg_editor_network_panel_get_stats (LrgEditorNetworkPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NETWORK_PANEL (self), NULL);
	return self->stats;
}
