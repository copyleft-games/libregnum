/* lrg-editor-server-tools.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in IPC methods for the editor tools that work headless:
 *
 *   tilemap.get / configure / paint / rect / fill / pick
 *   timeline.list / get / add / remove / set / key / unkey / move-key /
 *            ease / sample
 *
 * Every mutation goes through LrgEditorTilePainter or the
 * lrg_editor_timeline_* API, so it is one undo step and raises the usual
 * "changed" event. See docs/editor/ipc-protocol.org for the reference.
 */

#include "lrg-editor-server-private.h"
#include "lrg-editor.h"
#include "lrg-editor-tile-painter.h"
#include "lrg-editor-timeline.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-node-tilemap.h"
#include "lrg-timeline.h"
#include "../lrg-enums.h"
#include <math.h>

/* ==========================================================================
 * Parameter helpers
 * ========================================================================== */

static gboolean
has (JsonObject  *p,
     const gchar *key)
{
	return p != NULL && json_object_has_member (p, key) && !JSON_NODE_HOLDS_NULL (json_object_get_member (p, key));
}

static const gchar *
p_str (JsonObject  *p,
       const gchar *key)
{
	JsonNode *n;

	if (!has (p, key))
		return NULL;
	n = json_object_get_member (p, key);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

static gboolean
p_is_number (JsonObject  *p,
             const gchar *key)
{
	JsonNode *n;

	if (!has (p, key))
		return FALSE;
	n = json_object_get_member (p, key);
	return JSON_NODE_HOLDS_VALUE (n) &&
	       (json_node_get_value_type (n) == G_TYPE_INT64 || json_node_get_value_type (n) == G_TYPE_DOUBLE);
}

static gdouble
p_num (JsonObject  *p,
       const gchar *key,
       gdouble      def)
{
	return p_is_number (p, key) ? json_node_get_double (json_object_get_member (p, key)) : def;
}

static gint
p_bool3 (JsonObject  *p,
         const gchar *key)
{
	JsonNode *n;

	if (!has (p, key))
		return -1;
	n = json_object_get_member (p, key);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return -1;
	return json_node_get_boolean (n) ? 1 : 0;
}

static gboolean invalid (GError **error, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

/* Sets an invalid-params error; always returns FALSE. */
static gboolean
invalid (GError      **error,
         const gchar  *format,
         ...)
{
	va_list           args;
	g_autofree gchar *msg = NULL;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, msg);
	return FALSE;
}

static gboolean
require_number (JsonObject   *p,
                const gchar  *key,
                gdouble      *out,
                GError      **error)
{
	if (!p_is_number (p, key))
		return invalid (error, "Missing number parameter '%s'", key);
	*out = json_node_get_double (json_object_get_member (p, key));
	if (!isfinite (*out))
		return invalid (error, "Parameter '%s' must be finite", key);
	return TRUE;
}

static const gchar *
require_str (JsonObject   *p,
             const gchar  *key,
             GError      **error)
{
	const gchar *s = p_str (p, key);

	if (s == NULL)
		invalid (error, "Missing string parameter '%s'", key);
	return s;
}

static LrgNode *
require_node (LrgEditorServer  *server,
              JsonObject       *p,
              GError          **error)
{
	const gchar *ref = require_str (p, "node", error);
	LrgNode     *node;

	if (ref == NULL)
		return NULL;
	node = lrg_editor_find_node (lrg_editor_server_get_editor (server), ref);
	if (node == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'", ref);
	return node;
}

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static JsonNode *
count_node (const gchar *key,
            guint        value)
{
	JsonObject *o = json_object_new ();

	json_object_set_int_member (o, key, value);
	return obj_node (o);
}

/* ==========================================================================
 * Tilemaps
 * ========================================================================== */

/* The node as a tilemap node, or an error. */
static LrgNode *
require_tilemap_node (LrgEditorServer  *server,
                      JsonObject       *p,
                      GError          **error)
{
	LrgNode       *node = require_node (server, p, error);
	LrgNodeVisual *visual;

	if (node == NULL)
		return NULL;
	visual = lrg_node_get_visual (node);
	if (visual == NULL || lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_TILEMAP)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Node '%s' is not a tilemap", lrg_node_get_name (node));
		return NULL;
	}
	return node;
}

/* "layer": index or name; absent -> 0. -1 with error on a bad name. */
static gint
resolve_layer (LrgNodeTilemap  *grid,
               JsonObject      *p,
               const gchar     *key,
               GError         **error)
{
	const gchar *name = p_str (p, key);
	gint         idx;

	if (name != NULL)
	{
		idx = (grid != NULL) ? lrg_node_tilemap_find_layer (grid, name) : -1;
		if (idx < 0)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No tile layer '%s'", name);
		return idx;
	}
	idx = (gint) p_num (p, key, 0.0);
	if (idx < 0 || (grid != NULL && (guint) idx >= lrg_node_tilemap_get_n_layers (grid)))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No tile layer %d", idx);
		return -1;
	}
	return idx;
}

static JsonNode *
tilemap_to_json (LrgNode        *node,
                 LrgNodeTilemap *grid,
                 gboolean        cells,
                 gint            only_layer)
{
	JsonObject *o = json_object_new ();
	JsonArray  *layers = json_array_new ();
	guint       w = lrg_node_tilemap_get_width (grid);
	guint       h = lrg_node_tilemap_get_height (grid);
	guint       i, x, y;

	json_object_set_string_member (o, "node", lrg_node_get_guid (node));
	json_object_set_int_member (o, "width", w);
	json_object_set_int_member (o, "height", h);
	json_object_set_int_member (o, "tile_width", lrg_node_tilemap_get_tile_width (grid));
	json_object_set_int_member (o, "tile_height", lrg_node_tilemap_get_tile_height (grid));
	json_object_set_double_member (o, "tile_size", lrg_editor_tile_painter_get_tile_size (node));
	if (lrg_node_visual_get_asset (lrg_node_get_visual (node)) != NULL)
		json_object_set_string_member (o, "tileset", lrg_node_visual_get_asset (lrg_node_get_visual (node)));
	else
		json_object_set_null_member (o, "tileset");

	for (i = 0; i < lrg_node_tilemap_get_n_layers (grid); i++)
	{
		JsonObject *l;

		if (only_layer >= 0 && (guint) only_layer != i)
			continue;
		l = json_object_new ();
		json_object_set_int_member (l, "index", i);
		json_object_set_string_member (l, "name", lrg_node_tilemap_get_layer_name (grid, i));
		json_object_set_boolean_member (l, "visible", lrg_node_tilemap_get_layer_visible (grid, i));
		json_object_set_int_member (l, "count", lrg_node_tilemap_count_tiles (grid, i));
		if (cells)
		{
			JsonArray *rows = json_array_new ();

			for (y = 0; y < h; y++)
			{
				JsonArray *row = json_array_new ();

				for (x = 0; x < w; x++)
					json_array_add_int_element (row, lrg_node_tilemap_get_tile (grid, i, (gint) x, (gint) y));
				json_array_add_array_element (rows, row);
			}
			json_object_set_array_member (l, "rows", rows);
		}
		json_array_add_object_element (layers, l);
	}
	json_object_set_array_member (o, "layers", layers);
	return obj_node (o);
}

static JsonNode *
m_tilemap_get (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgNode        *node = require_tilemap_node (server, params, error);
	LrgNodeTilemap *grid;
	gint            layer = -1;

	if (node == NULL)
		return NULL;
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	if (grid == NULL)
	{
		/* No grid yet: report the empty state instead of failing. */
		JsonObject *o = json_object_new ();

		json_object_set_string_member (o, "node", lrg_node_get_guid (node));
		json_object_set_null_member (o, "width");
		json_object_set_array_member (o, "layers", json_array_new ());
		return obj_node (o);
	}
	if (has (params, "layer") && (layer = resolve_layer (grid, params, "layer", error)) < 0)
		return NULL;
	return tilemap_to_json (node, grid, p_bool3 (params, "cells") != 0, layer);
}

/* Applies the "configure" fields to @grid/@visual in place (the caller
 * snapshots and commits). */
static gboolean
configure_grid (LrgNodeVisual   *visual,
                LrgNodeTilemap  *grid,
                JsonObject      *p,
                GError         **error)
{
	gint layer;

	if (has (p, "width") || has (p, "height"))
	{
		gdouble w = p_num (p, "width", lrg_node_tilemap_get_width (grid));
		gdouble h = p_num (p, "height", lrg_node_tilemap_get_height (grid));

		if (!lrg_node_tilemap_resize (grid, (guint) MAX (w, 0.0), (guint) MAX (h, 0.0)))
			return invalid (error, "Bad tilemap size (1 .. 4096): '%s'", "width/height");
	}
	if (has (p, "tile_width") || has (p, "tile_height"))
	{
		gdouble tw = p_num (p, "tile_width", lrg_node_tilemap_get_tile_width (grid));
		gdouble th = p_num (p, "tile_height", lrg_node_tilemap_get_tile_height (grid));

		if (tw < 1.0 || th < 1.0)
			return invalid (error, "'%s' must be >= 1", "tile_width/tile_height");
		lrg_node_tilemap_set_tile_size (grid, (guint) tw, (guint) th);
	}
	if (has (p, "tile_size"))
	{
		gdouble ts = p_num (p, "tile_size", 1.0);

		if (!(ts > 0.0))
			return invalid (error, "'%s' must be > 0", "tile_size");
		lrg_node_visual_set_param_double (visual, "tile_size", ts);
	}
	if (p_str (p, "tileset") != NULL)
		lrg_node_visual_set_asset (visual, p_str (p, "tileset"));
	if (p_str (p, "add_layer") != NULL)
		lrg_node_tilemap_add_layer (grid, p_str (p, "add_layer"));
	if (has (p, "remove_layer"))
	{
		if ((layer = resolve_layer (grid, p, "remove_layer", error)) < 0)
			return FALSE;
		if (!lrg_node_tilemap_remove_layer (grid, (guint) layer))
			return invalid (error, "Cannot remove the last layer (%s)", "remove_layer");
	}
	if (p_str (p, "rename") != NULL)
	{
		if ((layer = resolve_layer (grid, p, "layer", error)) < 0)
			return FALSE;
		lrg_node_tilemap_set_layer_name (grid, (guint) layer, p_str (p, "rename"));
	}
	if (p_bool3 (p, "visible") >= 0)
	{
		if ((layer = resolve_layer (grid, p, "layer", error)) < 0)
			return FALSE;
		lrg_node_tilemap_set_layer_visible (grid, (guint) layer, p_bool3 (p, "visible") == 1);
	}
	return TRUE;
}

static JsonNode *
m_tilemap_configure (LrgEditorServer  *server,
                     JsonObject       *params,
                     gpointer          user_data,
                     GError          **error)
{
	LrgEditor                     *editor = lrg_editor_server_get_editor (server);
	LrgNode                       *node = require_tilemap_node (server, params, error);
	g_autoptr(LrgNode)             before = NULL;
	g_autoptr(LrgEditorTilePainter) painter = NULL;
	LrgNodeTilemap                *grid;

	if (node == NULL)
		return NULL;

	/* Grid creation and the settings are one undo step. */
	painter = lrg_editor_tile_painter_new (editor);
	lrg_editor_begin_group (editor, "Configure Tilemap");
	grid = lrg_editor_tile_painter_ensure_tilemap (painter, node);
	before = lrg_editor_snapshot_node (node);
	if (!configure_grid (lrg_node_get_visual (node), grid, params, error))
	{
		/* Put the node back as it was before the partial edit. */
		lrg_node_copy_state (node, before);
		lrg_editor_end_group (editor);
		return NULL;
	}
	lrg_editor_commit_node_state (editor, node, before, "Configure Tilemap", FALSE);
	lrg_editor_end_group (editor);
	return tilemap_to_json (node, lrg_node_visual_get_tilemap (lrg_node_get_visual (node)), FALSE, -1);
}

/*
 * Sets up a throw-away painter from the common paint parameters:
 * tile (default 1), erase, layer, brush.
 */
static LrgEditorTilePainter *
painter_for (LrgEditorServer  *server,
             LrgNode          *node,
             JsonObject       *p,
             gboolean         *erase,
             GError          **error)
{
	LrgEditorTilePainter *painter = lrg_editor_tile_painter_new (lrg_editor_server_get_editor (server));
	LrgNodeTilemap       *grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	gdouble               tile = p_num (p, "tile", 1.0);
	gint                  layer = 0;

	if (has (p, "layer") && (layer = resolve_layer (grid, p, "layer", error)) < 0)
	{
		g_object_unref (painter);
		return NULL;
	}
	if (tile < 0.0)
		tile = 0.0;
	*erase = p_bool3 (p, "erase") == 1 || tile == 0.0;
	lrg_editor_tile_painter_set_tile (painter, (guint) tile);
	lrg_editor_tile_painter_set_layer (painter, (guint) layer);
	lrg_editor_tile_painter_set_brush_size (painter, (guint) p_num (p, "brush", 1.0));
	return painter;
}

static JsonNode *
m_tilemap_paint (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgNode                        *node = require_tilemap_node (server, params, error);
	g_autoptr(LrgEditorTilePainter) painter = NULL;
	g_autoptr(GArray)               xy = NULL;
	gboolean                        erase = FALSE;
	JsonNode                       *cells;
	guint                           i, changed = 0;

	if (node == NULL || (painter = painter_for (server, node, params, &erase, error)) == NULL)
		return NULL;

	/* Either an explicit cell list, or a brush stroke through "points". */
	cells = has (params, "cells") ? json_object_get_member (params, "cells") : NULL;
	if (cells == NULL && !has (params, "points"))
	{
		invalid (error, "Need '%s' ([[x, y], ...]) or 'points' (a brush stroke)", "cells");
		return NULL;
	}
	xy = g_array_new (FALSE, FALSE, sizeof (gint));
	if (cells == NULL)
		cells = json_object_get_member (params, "points");
	if (!JSON_NODE_HOLDS_ARRAY (cells))
	{
		invalid (error, "'%s' must be an array of [x, y] pairs", has (params, "cells") ? "cells" : "points");
		return NULL;
	}
	for (i = 0; i < json_array_get_length (json_node_get_array (cells)); i++)
	{
		JsonNode  *e = json_array_get_element (json_node_get_array (cells), i);
		JsonArray *pair;
		gint       x, y;

		if (!JSON_NODE_HOLDS_ARRAY (e) || json_array_get_length (pair = json_node_get_array (e)) != 2)
		{
			invalid (error, "'%s' entries must be [x, y] pairs", "cells");
			return NULL;
		}
		x = (gint) json_array_get_int_element (pair, 0);
		y = (gint) json_array_get_int_element (pair, 1);
		g_array_append_val (xy, x);
		g_array_append_val (xy, y);
	}

	if (has (params, "cells"))
		changed = lrg_editor_tile_painter_paint_cells (painter, node, (const gint *) xy->data, xy->len, erase);
	else if (xy->len > 0 && lrg_editor_tile_painter_begin_stroke (painter, node, erase))
	{
		/* Points are joined like pointer samples, with the brush size. */
		for (i = 0; i + 1 < xy->len; i += 2)
			lrg_editor_tile_painter_stroke_to (painter, g_array_index (xy, gint, i), g_array_index (xy, gint, i + 1));
		changed = lrg_editor_tile_painter_end_stroke (painter);
	}
	return count_node ("changed", changed);
}

static JsonNode *
m_tilemap_rect (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgNode                        *node = require_tilemap_node (server, params, error);
	g_autoptr(LrgEditorTilePainter) painter = NULL;
	gboolean                        erase = FALSE;
	gdouble                         x0, y0, x1, y1;

	if (node == NULL || !require_number (params, "x0", &x0, error) || !require_number (params, "y0", &y0, error) ||
	    !require_number (params, "x1", &x1, error) || !require_number (params, "y1", &y1, error) ||
	    (painter = painter_for (server, node, params, &erase, error)) == NULL)
		return NULL;
	return count_node ("changed", lrg_editor_tile_painter_paint_rect (painter, node, (gint) x0, (gint) y0,
	                                                                  (gint) x1, (gint) y1, erase));
}

static JsonNode *
m_tilemap_fill (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgNode                        *node = require_tilemap_node (server, params, error);
	g_autoptr(LrgEditorTilePainter) painter = NULL;
	gboolean                        erase = FALSE;
	gdouble                         x, y;

	if (node == NULL || !require_number (params, "x", &x, error) || !require_number (params, "y", &y, error) ||
	    (painter = painter_for (server, node, params, &erase, error)) == NULL)
		return NULL;
	return count_node ("changed", lrg_editor_tile_painter_fill (painter, node, (gint) x, (gint) y, erase));
}

static JsonNode *
m_tilemap_pick (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgNode                        *node = require_tilemap_node (server, params, error);
	g_autoptr(LrgEditorTilePainter) painter = NULL;
	gboolean                        erase = FALSE;
	gdouble                         x, y;

	if (node == NULL || !require_number (params, "x", &x, error) || !require_number (params, "y", &y, error) ||
	    (painter = painter_for (server, node, params, &erase, error)) == NULL)
		return NULL;
	return count_node ("tile", lrg_editor_tile_painter_pick (painter, node, (gint) x, (gint) y));
}

/* ==========================================================================
 * Timelines
 * ========================================================================== */

static LrgTimeline *
find_timeline (LrgEditorServer  *server,
               const gchar      *name,
               GError          **error)
{
	LrgLevel *level = lrg_editor_get_level (lrg_editor_server_get_editor (server));
	gint      idx;

	if (name == NULL)
		return NULL;
	idx = lrg_level_find_timeline (level, name);
	if (idx < 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No timeline '%s'", name);
		return NULL;
	}
	return lrg_level_get_timeline (level, (guint) idx);
}

static gboolean
parse_easing (JsonObject    *p,
              LrgEasingType *out,
              GError       **error)
{
	const gchar          *nick = p_str (p, "easing");
	g_autoptr(GEnumClass) klass = NULL;
	GEnumValue           *ev;

	*out = LRG_EASING_LINEAR;
	if (nick == NULL)
		return TRUE;
	klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	ev = g_enum_get_value_by_nick (klass, nick);
	if (ev == NULL)
		return invalid (error, "Unknown easing '%s' (use a nick such as linear, ease-in-quad)", nick);
	*out = ev->value;
	return TRUE;
}

static const gchar *
easing_nick (LrgEasingType e)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	GEnumValue           *ev = g_enum_get_value (klass, e);

	return (ev != NULL) ? ev->value_nick : "linear";
}

static JsonObject *
timeline_summary (LrgTimeline *tl)
{
	JsonObject *o = json_object_new ();

	json_object_set_string_member (o, "name", lrg_timeline_get_name (tl));
	json_object_set_double_member (o, "length", lrg_timeline_get_length (tl));
	json_object_set_boolean_member (o, "loop", lrg_timeline_get_loop (tl));
	json_object_set_boolean_member (o, "autoplay", lrg_timeline_get_autoplay (tl));
	json_object_set_int_member (o, "tracks", lrg_timeline_get_n_tracks (tl));
	return o;
}

static JsonNode *
m_timeline_list (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgLevel  *level = lrg_editor_get_level (lrg_editor_server_get_editor (server));
	JsonArray *arr = json_array_new ();
	JsonNode  *n;
	guint      i;

	for (i = 0; i < lrg_level_get_n_timelines (level); i++)
		json_array_add_object_element (arr, timeline_summary (lrg_level_get_timeline (level, i)));
	n = json_node_new (JSON_NODE_ARRAY);
	json_node_take_array (n, arr);
	return n;
}

static JsonNode *
m_timeline_get (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditor   *editor = lrg_editor_server_get_editor (server);
	LrgTimeline *tl = find_timeline (server, require_str (params, "name", error), error);
	JsonObject  *o;
	JsonArray   *tracks;
	guint        i, k;

	if (tl == NULL)
		return NULL;
	o = timeline_summary (tl);
	tracks = json_array_new ();
	for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
	{
		LrgTimelineTrack *t = lrg_timeline_get_track (tl, i);
		LrgNode          *node = lrg_editor_find_node (editor, lrg_timeline_track_get_node_guid (t));
		JsonObject       *to = json_object_new ();
		JsonArray        *keys = json_array_new ();

		json_object_set_string_member (to, "node", lrg_timeline_track_get_node_guid (t));
		if (node != NULL)
			json_object_set_string_member (to, "node_name", lrg_node_get_name (node));
		else
			json_object_set_null_member (to, "node_name");
		json_object_set_string_member (to, "channel", lrg_timeline_track_get_channel (t));
		for (k = 0; k < lrg_timeline_track_get_n_keys (t); k++)
		{
			JsonObject   *ko = json_object_new ();
			gfloat        time = 0.0f, value = 0.0f;
			LrgEasingType e = LRG_EASING_LINEAR;

			lrg_timeline_track_get_key (t, k, &time, &value, &e);
			json_object_set_double_member (ko, "time", time);
			json_object_set_double_member (ko, "value", value);
			json_object_set_string_member (ko, "easing", easing_nick (e));
			json_array_add_object_element (keys, ko);
		}
		json_object_set_array_member (to, "keys", keys);
		json_array_add_object_element (tracks, to);
	}
	json_object_set_array_member (o, "tracks", tracks);
	return obj_node (o);
}

static JsonNode *
m_timeline_add (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	const gchar *name = require_str (params, "name", error);

	if (name == NULL)
		return NULL;
	if (!lrg_editor_timeline_add (lrg_editor_server_get_editor (server), name, (gfloat) p_num (params, "length", 0.0),
	                              p_bool3 (params, "loop") == 1))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "A timeline '%s' already exists", name);
		return NULL;
	}
	return obj_node (timeline_summary (find_timeline (server, name, NULL)));
}

static JsonNode *
m_timeline_remove (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	const gchar *name = require_str (params, "name", error);
	JsonNode    *n;

	if (name == NULL || find_timeline (server, name, error) == NULL)
		return NULL;
	n = json_node_new (JSON_NODE_VALUE);
	json_node_set_boolean (n, lrg_editor_timeline_remove (lrg_editor_server_get_editor (server), name));
	return n;
}

static JsonNode *
m_timeline_set (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	const gchar *name = require_str (params, "name", error);
	const gchar *rename = p_str (params, "rename");

	if (name == NULL || find_timeline (server, name, error) == NULL)
		return NULL;
	if (has (params, "length") && !(p_num (params, "length", 0.0) > 0.0))
	{
		invalid (error, "'%s' must be > 0", "length");
		return NULL;
	}
	if (!lrg_editor_timeline_configure (lrg_editor_server_get_editor (server), name, rename,
	                                    (gfloat) p_num (params, "length", 0.0), p_bool3 (params, "loop"),
	                                    p_bool3 (params, "autoplay")))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "Cannot rename '%s' to '%s'", name, rename != NULL ? rename : "");
		return NULL;
	}
	return obj_node (timeline_summary (find_timeline (server, rename != NULL ? rename : name, NULL)));
}

/* Reads "channels": an array of strings into a NULL-terminated strv. */
static gchar **
p_channels (JsonObject  *p,
            GError     **error)
{
	JsonNode   *n = json_object_get_member (p, "channels");
	JsonArray  *arr;
	GPtrArray  *out;
	guint       i;

	if (!JSON_NODE_HOLDS_ARRAY (n))
	{
		invalid (error, "'%s' must be an array of channel names", "channels");
		return NULL;
	}
	arr = json_node_get_array (n);
	out = g_ptr_array_new ();
	for (i = 0; i < json_array_get_length (arr); i++)
	{
		JsonNode    *e = json_array_get_element (arr, i);
		const gchar *s = (JSON_NODE_HOLDS_VALUE (e) && json_node_get_value_type (e) == G_TYPE_STRING)
		                 ? json_node_get_string (e) : NULL;

		if (!lrg_timeline_channel_is_valid (s))
		{
			g_ptr_array_free (out, TRUE);
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown channel '%s'", s != NULL ? s : "?");
			return NULL;
		}
		g_ptr_array_add (out, g_strdup (s));
	}
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

static JsonNode *
m_timeline_key (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditor         *editor = lrg_editor_server_get_editor (server);
	const gchar       *name = require_str (params, "name", error);
	const gchar       *channel = p_str (params, "channel");
	LrgNode           *node;
	LrgEasingType      easing;
	gdouble            time;
	g_auto(GStrv)      channels = NULL;

	if (name == NULL || (node = require_node (server, params, error)) == NULL ||
	    !require_number (params, "time", &time, error) || !parse_easing (params, &easing, error))
		return NULL;

	if (channel != NULL)
	{
		gfloat value;

		if (!lrg_timeline_channel_is_valid (channel))
		{
			invalid (error, "Unknown channel '%s'", channel);
			return NULL;
		}
		/* Without a value, key the node's current value. */
		if (p_is_number (params, "value"))
			value = (gfloat) p_num (params, "value", 0.0);
		else if (!lrg_timeline_channel_read (node, channel, &value))
		{
			invalid (error, "Node has no value for channel '%s'", channel);
			return NULL;
		}
		if (!lrg_editor_timeline_set_key (editor, name, node, channel, (gfloat) time, value, easing, FALSE))
		{
			invalid (error, "Cannot key channel '%s'", channel);
			return NULL;
		}
		return count_node ("keys", 1);
	}

	if (has (params, "channels") && (channels = p_channels (params, error)) == NULL)
		return NULL;
	return count_node ("keys", lrg_editor_timeline_key_node (editor, name, node, (gfloat) time,
	                                                         (const gchar * const *) channels, easing));
}

static JsonNode *
m_timeline_unkey (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	const gchar *name = require_str (params, "name", error);
	LrgNode     *node;
	gdouble      time;

	if (name == NULL || find_timeline (server, name, error) == NULL ||
	    (node = require_node (server, params, error)) == NULL || !require_number (params, "time", &time, error))
		return NULL;
	return count_node ("removed", lrg_editor_timeline_remove_key (lrg_editor_server_get_editor (server), name, node,
	                                                              p_str (params, "channel"), (gfloat) time));
}

static JsonNode *
m_timeline_move_key (LrgEditorServer  *server,
                     JsonObject       *params,
                     gpointer          user_data,
                     GError          **error)
{
	const gchar *name = require_str (params, "name", error);
	LrgNode     *node;
	gdouble      from, to;

	if (name == NULL || find_timeline (server, name, error) == NULL ||
	    (node = require_node (server, params, error)) == NULL || !require_number (params, "from", &from, error) ||
	    !require_number (params, "to", &to, error))
		return NULL;
	return count_node ("moved", lrg_editor_timeline_move_key (lrg_editor_server_get_editor (server), name, node,
	                                                          p_str (params, "channel"), (gfloat) from, (gfloat) to,
	                                                          FALSE));
}

static JsonNode *
m_timeline_ease (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	const gchar  *name = require_str (params, "name", error);
	LrgNode      *node;
	gdouble       time;
	LrgEasingType easing;

	if (name == NULL || find_timeline (server, name, error) == NULL ||
	    (node = require_node (server, params, error)) == NULL || !require_number (params, "time", &time, error))
		return NULL;
	if (!has (params, "easing"))
	{
		invalid (error, "Missing string parameter '%s'", "easing");
		return NULL;
	}
	if (!parse_easing (params, &easing, error))
		return NULL;
	return count_node ("changed", lrg_editor_timeline_set_easing (lrg_editor_server_get_editor (server), name, node,
	                                                              p_str (params, "channel"), (gfloat) time, easing));
}

static JsonNode *
m_timeline_sample (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgTimeline *tl = find_timeline (server, require_str (params, "name", error), error);
	JsonArray   *values;
	JsonObject  *o;
	gdouble      time;
	gfloat       clip;
	guint        i;

	if (tl == NULL || !require_number (params, "time", &time, error))
		return NULL;

	/* Pure: reports what playback would write at @time, changes nothing. */
	clip = lrg_timeline_wrap_time (tl, (gfloat) time);
	values = json_array_new ();
	for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
	{
		LrgTimelineTrack *t = lrg_timeline_get_track (tl, i);
		JsonObject       *v;
		gfloat            value;

		if (!lrg_timeline_track_sample (t, clip, &value))
			continue;
		v = json_object_new ();
		json_object_set_string_member (v, "node", lrg_timeline_track_get_node_guid (t));
		json_object_set_string_member (v, "channel", lrg_timeline_track_get_channel (t));
		json_object_set_double_member (v, "value", value);
		json_array_add_object_element (values, v);
	}
	o = json_object_new ();
	json_object_set_double_member (o, "time", clip);
	json_object_set_array_member (o, "values", values);
	return obj_node (o);
}

/* ==========================================================================
 * Registration
 * ========================================================================== */

void
editor_server_register_tools (LrgEditorServer *self)
{
	static const struct
	{
		const gchar           *name;
		LrgEditorServerMethod  func;
	} table[] = {
		{ "tilemap.get", m_tilemap_get },
		{ "tilemap.configure", m_tilemap_configure },
		{ "tilemap.paint", m_tilemap_paint },
		{ "tilemap.rect", m_tilemap_rect },
		{ "tilemap.fill", m_tilemap_fill },
		{ "tilemap.pick", m_tilemap_pick },
		{ "timeline.list", m_timeline_list },
		{ "timeline.get", m_timeline_get },
		{ "timeline.add", m_timeline_add },
		{ "timeline.remove", m_timeline_remove },
		{ "timeline.set", m_timeline_set },
		{ "timeline.key", m_timeline_key },
		{ "timeline.unkey", m_timeline_unkey },
		{ "timeline.move-key", m_timeline_move_key },
		{ "timeline.ease", m_timeline_ease },
		{ "timeline.sample", m_timeline_sample },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (table); i++)
		lrg_editor_server_register_method (self, table[i].name, table[i].func, NULL, NULL);
}
