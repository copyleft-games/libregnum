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
 *   asset.meta.get / set     (".meta" sidecars, see lrg-asset-meta.h)
 *   vcs.status / diff / log / commit / locks / magit
 *   prefab.overrides / apply / revert / sync / relink / unlink
 *                            (linked prefab instances, lrg-prefab.h)
 *   po.extract / pseudo / import / export
 *   gizmo.apply / pivot / cursor / origin
 *   view.letterbox
 *   level.merge
 *   collab.apply / get / lock / unlock / cursor / presence
 *   scenes.list / open / active
 *
 * Every level mutation goes through LrgEditorTilePainter, the
 * lrg_editor_timeline_* or the lrg_editor_prefab_* API, so it is one undo
 * step and raises the usual "changed" event. asset.meta.set writes the
 * sidecar file directly (not undoable, like a file save). See docs/editor/ipc-protocol.org for the reference.
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
#include "lrg-asset-meta.h"
#include "lrg-editor-vcs.h"
#include "lrg-prefab.h"
#include "lrg-editor-gizmo.h"
#include "lrg-transform-tools.h"
#include "lrg-level-merge.h"
#include "lrg-collab.h"
#include "lrg-editor-scenes.h"
#include "lrg-game-view.h"
#include "../i18n/lrg-po.h"
#include "../i18n/lrg-locale.h"
#include "../lrg-enums.h"
#include <math.h>
#include <string.h>

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
 * Shared helpers for the asset, VCS and prefab methods
 * ========================================================================== */

/* Sets a FAILED error from @cause (keeps its message); returns NULL. */
static JsonNode *
failed_from (GError **error,
             GError  *cause)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
	                     (cause != NULL && cause->message != NULL) ? cause->message : "Operation failed");
	return NULL;
}

/* A string member, or JSON null for %NULL. */
static void
set_str_or_null (JsonObject  *o,
                 const gchar *key,
                 const gchar *value)
{
	if (value != NULL)
		json_object_set_string_member (o, key, value);
	else
		json_object_set_null_member (o, key);
}

/*
 * The project directory the server resolves relative paths against: the
 * one set with lrg_editor_server_set_project_dir(), else the current
 * directory (the same fallback lrg-editor-server.c's resolve_path() uses).
 */
static gchar *
project_dir_of (LrgEditorServer *server)
{
	const gchar *dir = lrg_editor_server_get_project_dir (server);

	return (dir != NULL) ? g_canonicalize_filename (dir, NULL) : g_get_current_dir ();
}

/* @path made absolute against the project directory, canonicalized. */
static gchar *
tools_resolve_path (LrgEditorServer *server,
                    const gchar     *path)
{
	g_autofree gchar *base = NULL;

	if (g_path_is_absolute (path))
		return g_canonicalize_filename (path, NULL);
	base = project_dir_of (server);
	return g_canonicalize_filename (path, base);
}

/* A number, or a decimal string (lrgeditorctl positional args are strings). */
static gboolean
p_num_flex (JsonObject  *p,
            const gchar *key,
            gdouble     *out)
{
	JsonNode *n;

	if (!has (p, key))
		return FALSE;
	n = json_object_get_member (p, key);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	if (json_node_get_value_type (n) == G_TYPE_STRING)
	{
		*out = g_ascii_strtod (json_node_get_string (n), NULL);
		return TRUE;
	}
	if (json_node_get_value_type (n) == G_TYPE_INT64 ||
	    json_node_get_value_type (n) == G_TYPE_DOUBLE)
	{
		*out = json_node_get_double (n);
		return TRUE;
	}
	return FALSE;
}

static gboolean
p_u64 (JsonObject  *p,
       const gchar *key,
       guint64     *out)
{
	JsonNode *n;

	if (!has (p, key))
		return FALSE;
	n = json_object_get_member (p, key);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	if (json_node_get_value_type (n) == G_TYPE_STRING)
	{
		*out = g_ascii_strtoull (json_node_get_string (n), NULL, 10);
		return TRUE;
	}
	if (json_node_get_value_type (n) == G_TYPE_INT64)
	{
		gint64 v = json_node_get_int (n);

		if (v < 0)
			return FALSE;
		*out = (guint64) v;
		return TRUE;
	}
	if (json_node_get_value_type (n) == G_TYPE_DOUBLE)
	{
		*out = (guint64) json_node_get_double (n);
		return TRUE;
	}
	return FALSE;
}

static JsonNode *
text_node (const gchar *key,
           const gchar *text)
{
	JsonObject *o = json_object_new ();

	json_object_set_string_member (o, key, text != NULL ? text : "");
	return obj_node (o);
}

/*
 * Reads "keys" (an optional array of strings) into a NULL-terminated strv.
 * Absent or null -> TRUE with *out = NULL ("all"); anything that is not
 * an array of strings is an invalid-params error.
 */
static gboolean
p_keys (JsonObject   *p,
        GStrv        *out,
        GError      **error)
{
	JsonNode  *n;
	JsonArray *arr;
	GPtrArray *keys;
	guint      i;

	*out = NULL;
	if (!has (p, "keys"))
		return TRUE;
	n = json_object_get_member (p, "keys");
	if (!JSON_NODE_HOLDS_ARRAY (n))
		return invalid (error, "'%s' must be an array of override keys", "keys");

	arr = json_node_get_array (n);
	keys = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < json_array_get_length (arr); i++)
	{
		JsonNode *e = json_array_get_element (arr, i);

		if (!JSON_NODE_HOLDS_VALUE (e) || json_node_get_value_type (e) != G_TYPE_STRING)
		{
			g_ptr_array_free (keys, TRUE);
			return invalid (error, "'%s' entries must be strings", "keys");
		}
		g_ptr_array_add (keys, g_strdup (json_node_get_string (e)));
	}
	g_ptr_array_add (keys, NULL);
	g_ptr_array_set_free_func (keys, NULL);
	*out = (GStrv) g_ptr_array_free (keys, FALSE);
	return TRUE;
}

/* ==========================================================================
 * Asset sidecars (asset.meta.get / asset.meta.set)
 * ========================================================================== */

/*
 * Resolves the "path" parameter to the absolute ASSET path. A path that
 * names the sidecar itself ("foo.png.meta") is accepted and mapped back
 * to its asset, so callers can pass either.
 */
static gchar *
require_asset_path (LrgEditorServer  *server,
                    JsonObject       *p,
                    GError          **error)
{
	const gchar      *path = require_str (p, "path", error);
	g_autofree gchar *full = NULL;

	if (path == NULL)
		return NULL;
	full = tools_resolve_path (server, path);
	if (lrg_asset_meta_is_meta_path (full))
		full[strlen (full) - strlen (".meta")] = '\0';
	return g_steal_pointer (&full);
}

/*
 * The 0.8.0 identifier of @full: the SHA-256 of its path relative to the
 * project directory. Assets outside the project fall back to their file
 * name, which is what a flat search directory would have produced.
 */
static gchar *
legacy_guid_for (LrgEditorServer *server,
                 const gchar     *full)
{
	g_autofree gchar *base = project_dir_of (server);
	g_autofree gchar *prefix = g_strconcat (base, G_DIR_SEPARATOR_S, NULL);
	g_autofree gchar *name = NULL;

	if (g_str_has_prefix (full, prefix))
		return lrg_asset_meta_compute_legacy_guid (full + strlen (prefix));
	name = g_path_get_basename (full);
	return lrg_asset_meta_compute_legacy_guid (name);
}

/*
 * Loads the sidecar of @full, or describes the one the asset database
 * would create in memory (importer from the extension, legacy guid). A
 * hand-written sidecar without a guid adopts the legacy guid, like the
 * database does. An unreadable sidecar is an error: it may be in the
 * middle of a merge conflict and must never be replaced.
 */
static LrgAssetMeta *
load_meta (LrgEditorServer  *server,
           const gchar      *full,
           gboolean         *exists,
           GError          **error)
{
	g_autofree gchar       *meta_path = lrg_asset_meta_path_for_asset (full);
	g_autofree gchar       *legacy = NULL;
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autoptr(GError)       local = NULL;

	*exists = g_file_test (meta_path, G_FILE_TEST_IS_REGULAR);
	if (!*exists && !g_file_test (full, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No asset '%s'", full);
		return NULL;
	}

	legacy = legacy_guid_for (server, full);
	if (*exists)
	{
		meta = lrg_asset_meta_new_from_file (meta_path, &local);
		if (meta == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
			             "Cannot read sidecar '%s': %s", meta_path, local->message);
			return NULL;
		}
		if (lrg_asset_meta_get_guid (meta) == NULL)
			lrg_asset_meta_set_guid (meta, legacy);
	}
	else
	{
		meta = lrg_asset_meta_new_for_asset (full);
		lrg_asset_meta_set_guid (meta, legacy);
		lrg_asset_meta_set_dirty (meta, FALSE);
	}
	return g_steal_pointer (&meta);
}

/* {guid, importer, settings} plus "path" (the asset) and "exists". */
static JsonNode *
meta_to_json (LrgAssetMeta *meta,
              const gchar  *full,
              gboolean      exists)
{
	g_autoptr(GVariant) v = lrg_asset_meta_to_variant (meta);
	JsonNode           *n = json_gvariant_serialize (v);
	JsonObject         *o = json_node_get_object (n);

	json_object_set_string_member (o, "path", full);
	json_object_set_boolean_member (o, "exists", exists);
	return n;
}

static JsonNode *
m_asset_meta_get (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	g_autofree gchar       *full = require_asset_path (server, params, error);
	g_autoptr(LrgAssetMeta) meta = NULL;
	gboolean                exists = FALSE;

	/* Read-only: a missing sidecar is described, never written. */
	if (full == NULL || (meta = load_meta (server, full, &exists, error)) == NULL)
		return NULL;
	return meta_to_json (meta, full, exists);
}

static JsonNode *
m_asset_meta_set (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	g_autofree gchar       *full = require_asset_path (server, params, error);
	g_autofree gchar       *meta_path = NULL;
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autoptr(GError)       local = NULL;
	g_autoptr(GList)        members = NULL;
	JsonObject             *settings = NULL;
	const gchar            *guid = p_str (params, "guid");
	gboolean                exists = FALSE;
	GList                  *l;

	if (full == NULL)
		return NULL;

	/* Validate the shape of the request before touching anything. */
	if (has (params, "settings"))
	{
		JsonNode *s = json_object_get_member (params, "settings");

		if (!JSON_NODE_HOLDS_OBJECT (s))
		{
			invalid (error, "'%s' must be an object of key: value (null resets)", "settings");
			return NULL;
		}
		settings = json_node_get_object (s);
	}
	if (has (params, "guid") && (guid == NULL || *guid == '\0'))
	{
		invalid (error, "'%s' must be a non-empty string", "guid");
		return NULL;
	}
	if (settings == NULL && guid == NULL)
	{
		invalid (error, "Need '%s' and/or 'guid'", "settings");
		return NULL;
	}

	if ((meta = load_meta (server, full, &exists, error)) == NULL)
		return NULL;

	/*
	 * All or nothing: every value is validated on the freshly loaded
	 * in-memory sidecar, and the file is written only after all of them
	 * were accepted. A failure simply drops @meta.
	 */
	if (settings != NULL)
		members = json_object_get_members (settings);
	for (l = members; l != NULL; l = l->next)
	{
		const gchar        *key = (const gchar *) l->data;
		JsonNode           *jv = json_object_get_member (settings, key);
		g_autoptr(GVariant) value = NULL;

		if (!JSON_NODE_HOLDS_NULL (jv))
		{
			/* Scalars map to b / x / d / s, objects to a{sv}, arrays to
			 * av; the setter converts numbers and checks ranges. */
			value = json_gvariant_deserialize (jv, NULL, &local);
			if (value == NULL)
			{
				g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
				             "Bad value for setting '%s': %s", key, local->message);
				return NULL;
			}
			value = g_variant_take_ref (value);
		}
		if (!lrg_asset_meta_set_setting_variant (meta, key, value, &local))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Invalid setting '%s': %s", key, local->message);
			return NULL;
		}
	}
	if (guid != NULL)
		lrg_asset_meta_set_guid (meta, guid);

	meta_path = lrg_asset_meta_path_for_asset (full);
	if (!lrg_asset_meta_save_to_file (meta, meta_path, &local))
		return failed_from (error, local);
	return meta_to_json (meta, full, TRUE);
}

/* ==========================================================================
 * Version control (vcs.status)
 * ========================================================================== */

#define TOOLS_VCS_KEY "lrg-editor-server-tools-vcs"

/*
 * The server's git status tracker, kept as object data so repeated calls
 * reuse it; it is replaced when the project directory changed since it
 * was created.
 */
static LrgEditorVcs *
server_vcs (LrgEditorServer *server)
{
	g_autofree gchar *dir = project_dir_of (server);
	LrgEditorVcs     *vcs = g_object_get_data (G_OBJECT (server), TOOLS_VCS_KEY);

	if (vcs == NULL || g_strcmp0 (lrg_editor_vcs_get_project_dir (vcs), dir) != 0)
	{
		vcs = lrg_editor_vcs_new (dir);
		g_object_set_data_full (G_OBJECT (server), TOOLS_VCS_KEY, vcs, g_object_unref);
	}
	return vcs;
}

/* g_ptr_array_sort() comparator for an array of strings. */
static gint
compare_str_ptr (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (*(const gchar *const *) a, *(const gchar *const *) b);
}

static JsonObject *
vcs_entry (const gchar        *path,
           LrgEditorVcsStatus  status)
{
	JsonObject *o = json_object_new ();
	gchar       letter[2];

	letter[0] = lrg_editor_vcs_status_to_letter (status);
	letter[1] = '\0';
	json_object_set_string_member (o, "path", path);
	json_object_set_string_member (o, "status", lrg_editor_vcs_status_to_string (status));
	json_object_set_string_member (o, "letter", letter);
	return o;
}

static JsonNode *
m_vcs_status (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorVcs         *vcs = server_vcs (server);
	const gchar          *path = p_str (params, "path");
	g_autoptr(GError)     local = NULL;
	g_autoptr(GPtrArray)  paths = NULL;
	GHashTable           *statuses;
	GHashTableIter        it;
	gpointer              key, value;
	JsonObject           *o;
	JsonArray            *files;
	guint                 i;

	if (has (params, "path") && path == NULL)
	{
		invalid (error, "'%s' must be a string", "path");
		return NULL;
	}

	/* Fresh data: IPC answers must not depend on a pending debounce. */
	if (!lrg_editor_vcs_refresh_sync (vcs, NULL, &local))
		return failed_from (error, local);

	if (path != NULL)
	{
		g_autofree gchar   *full = tools_resolve_path (server, path);
		LrgEditorVcsStatus  status;

		/* Folders aggregate (the asset browser's badge rule). */
		if (g_file_test (full, G_FILE_TEST_IS_DIR))
			status = lrg_editor_vcs_get_directory_status (vcs, full);
		else
			status = lrg_editor_vcs_get_status (vcs, full);
		return obj_node (vcs_entry (path, status));
	}

	o = json_object_new ();
	files = json_array_new ();
	json_object_set_boolean_member (o, "repository", lrg_editor_vcs_get_is_repo (vcs));
	set_str_or_null (o, "branch", lrg_editor_vcs_get_branch (vcs));

	/* Every reported path except clean ones, sorted for stable output. */
	statuses = lrg_editor_vcs_get_statuses (vcs);
	paths = g_ptr_array_new ();
	if (statuses != NULL)
	{
		g_hash_table_iter_init (&it, statuses);
		while (g_hash_table_iter_next (&it, &key, &value))
			if ((LrgEditorVcsStatus) GPOINTER_TO_INT (value) != LRG_EDITOR_VCS_STATUS_UNMODIFIED)
				g_ptr_array_add (paths, key);
	}
	g_ptr_array_sort (paths, compare_str_ptr);
	for (i = 0; i < paths->len; i++)
	{
		const gchar *rel = g_ptr_array_index (paths, i);

		json_array_add_object_element (files, vcs_entry (rel, (LrgEditorVcsStatus)
		                                                 GPOINTER_TO_INT (g_hash_table_lookup (statuses, rel))));
	}
	json_object_set_array_member (o, "files", files);
	return obj_node (o);
}

/* ==========================================================================
 * Linked prefabs (prefab.overrides / apply / revert / sync / relink /
 * unlink)
 * ========================================================================== */

/*
 * The instance root a request about @node acts on: @node itself when it
 * carries a link (an instance root, possibly nested), else the instance
 * that governs it (lrg_prefab_find_owner()), else @node (unlinked).
 */
static LrgNode *
instance_for (LrgNode *node)
{
	LrgNode *owner;

	if (lrg_node_get_prefab_link (node) != NULL)
		return node;
	owner = lrg_prefab_find_owner (node);
	return (owner != NULL) ? owner : node;
}

/* Like require_node(), then the instance root; must be linked. */
static LrgNode *
require_linked_instance (LrgEditorServer  *server,
                         JsonObject       *p,
                         GError          **error)
{
	LrgNode       *node = require_node (server, p, error);
	LrgNode       *root;
	LrgPrefabLink *link;

	if (node == NULL)
		return NULL;
	root = instance_for (node);
	link = lrg_node_get_prefab_link (root);
	if (link == NULL || lrg_prefab_link_get_source_path (link) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "Node '%s' is not part of a linked prefab instance",
		             lrg_node_get_name (node) != NULL ? lrg_node_get_name (node) : lrg_node_get_guid (node));
		return NULL;
	}
	return root;
}

/*
 * Checks that every key in @keys is in the instance's override set
 * (exact match), so apply/revert either act on all of them or on none.
 */
static gboolean
check_keys (LrgNode      *root,
            gchar *const *keys,
            GError      **error)
{
	LrgPrefabLink *link = lrg_node_get_prefab_link (root);
	guint          i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		if (!lrg_prefab_link_contains_override (link, keys[i]))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No override '%s' on instance '%s'", keys[i], lrg_node_get_guid (root));
			return FALSE;
		}
	return TRUE;
}

/* {node, name, source, source_guid, linked, overrides} for @root. */
static JsonObject *
instance_json (LrgNode *root)
{
	LrgPrefabLink *link = lrg_node_get_prefab_link (root);
	const gchar   *source = (link != NULL) ? lrg_prefab_link_get_source_path (link) : NULL;
	gboolean       linked = (source != NULL);
	JsonObject    *o = json_object_new ();
	JsonArray     *arr = json_array_new ();
	g_auto(GStrv)  keys = NULL;
	guint          i;

	json_object_set_string_member (o, "node", lrg_node_get_guid (root));
	set_str_or_null (o, "name", lrg_node_get_name (root));
	/* Unlinked: the 0.8.0 marker, i.e. what prefab.relink would use. */
	set_str_or_null (o, "source", linked ? source : lrg_prefab_get_marker_source (root));
	set_str_or_null (o, "source_guid", linked ? lrg_prefab_link_get_source_guid (link) : NULL);
	json_object_set_boolean_member (o, "linked", linked);
	if (linked)
		keys = lrg_prefab_link_get_overrides (link);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		json_array_add_string_element (arr, keys[i]);
	json_object_set_array_member (o, "overrides", arr);
	return o;
}

static JsonNode *
m_prefab_overrides (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgNode *node = require_node (server, params, error);

	/* An unlinked node is not an error: "linked": false says it all. */
	return (node != NULL) ? obj_node (instance_json (instance_for (node))) : NULL;
}

/* Adds each LrgEditor::prefab-synced count to a guint. */
static void
on_prefab_synced (LrgEditor   *editor,
                  const gchar *path,
                  guint        n_instances,
                  gpointer     user_data)
{
	*(guint *) user_data += n_instances;
}

static JsonNode *
m_prefab_apply (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditor         *editor = lrg_editor_server_get_editor (server);
	LrgNode           *root = require_linked_instance (server, params, error);
	g_auto(GStrv)      keys = NULL;
	g_autoptr(GError)  local = NULL;
	JsonObject        *o;
	guint              synced = 0;
	guint              applied;
	gulong             handler;
	gboolean           ok;

	if (root == NULL || !p_keys (params, &keys, error) || !check_keys (root, keys, error))
		return NULL;

	/* Each listed key is applied; NULL applies every override. */
	applied = (keys != NULL) ? g_strv_length (keys)
	                         : lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (root));

	/* The editor syncs the dependents itself; count them via the signal. */
	handler = g_signal_connect (editor, "prefab-synced", G_CALLBACK (on_prefab_synced), &synced);
	ok = lrg_editor_prefab_apply (editor, root, (const gchar *const *) keys, &local);
	g_signal_handler_disconnect (editor, handler);
	if (!ok)
		return failed_from (error, local);

	o = instance_json (root);
	json_object_set_int_member (o, "applied", applied);
	json_object_set_int_member (o, "synced", synced);
	return obj_node (o);
}

static JsonNode *
m_prefab_revert (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditor         *editor = lrg_editor_server_get_editor (server);
	LrgNode           *root = require_linked_instance (server, params, error);
	g_auto(GStrv)      keys = NULL;
	g_autoptr(GError)  local = NULL;
	gboolean           ok = TRUE;
	guint              i;

	if (root == NULL || !p_keys (params, &keys, error) || !check_keys (root, keys, error))
		return NULL;

	if (keys == NULL)
		ok = lrg_editor_prefab_revert (editor, root, NULL, &local);
	else
	{
		/* Several keys are one undo step. A key below an earlier one
		 * may already be gone; reverting it again is a no-op. */
		lrg_editor_begin_group (editor, "Revert Overrides");
		for (i = 0; ok && keys[i] != NULL; i++)
			ok = lrg_editor_prefab_revert (editor, root, keys[i], &local);
		lrg_editor_end_group (editor);
	}
	if (!ok)
		return failed_from (error, local);
	return obj_node (instance_json (root));
}

/* lrg_node_foreach(): distinct canonical sources of outermost instances. */
static gboolean
collect_sources (LrgNode  *node,
                 gpointer  user_data)
{
	GPtrArray     *sources = user_data;
	LrgPrefabLink *link = lrg_node_get_prefab_link (node);
	gchar         *canon;

	if (link == NULL || lrg_prefab_link_get_source_path (link) == NULL || lrg_prefab_find_owner (node) != node)
		return TRUE;
	canon = g_canonicalize_filename (lrg_prefab_link_get_source_path (link), NULL);
	if (g_ptr_array_find_with_equal_func (sources, canon, g_str_equal, NULL))
		g_free (canon);
	else
		g_ptr_array_add (sources, canon);
	return TRUE;
}

static JsonNode *
m_prefab_sync (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditor            *editor = lrg_editor_server_get_editor (server);
	const gchar          *path = p_str (params, "path");
	g_autoptr(GPtrArray)  sources = NULL;
	JsonObject           *o;
	guint                 synced = 0;
	guint                 i;

	if (has (params, "path") && path == NULL)
	{
		invalid (error, "'%s' must be a string", "path");
		return NULL;
	}

	sources = g_ptr_array_new_with_free_func (g_free);
	if (path != NULL)
	{
		gchar *full = tools_resolve_path (server, path);

		if (!g_file_test (full, G_FILE_TEST_IS_REGULAR))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No prefab '%s'", full);
			g_free (full);
			return NULL;
		}
		g_ptr_array_add (sources, full);
	}
	else
		lrg_node_foreach (lrg_level_get_root (lrg_editor_get_level (editor)), collect_sources, sources);

	/*
	 * lrg_editor_prefab_changed() also syncs instances of prefabs that
	 * nest the path. Several sources are one undo step; an instance that
	 * is already current records nothing and is not counted twice.
	 */
	lrg_editor_begin_group (editor, "Sync Prefabs");
	for (i = 0; i < sources->len; i++)
		synced += lrg_editor_prefab_changed (editor, g_ptr_array_index (sources, i));
	lrg_editor_end_group (editor);

	o = json_object_new ();
	json_object_set_int_member (o, "synced", synced);
	json_object_set_int_member (o, "prefabs", sources->len);
	return obj_node (o);
}

static JsonNode *
m_prefab_relink (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgNode           *node = require_node (server, params, error);
	const gchar       *path = p_str (params, "path");
	g_autofree gchar  *full = NULL;
	g_autoptr(GError)  local = NULL;

	if (node == NULL)
		return NULL;
	if (has (params, "path") && path == NULL)
	{
		invalid (error, "'%s' must be a string", "path");
		return NULL;
	}

	/* Without "path" the 0.8.0 marker on the node names the source. */
	if (path != NULL)
	{
		full = tools_resolve_path (server, path);
		if (!g_file_test (full, G_FILE_TEST_IS_REGULAR))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No prefab '%s'", full);
			return NULL;
		}
	}
	if (!lrg_editor_prefab_relink (lrg_editor_server_get_editor (server), node, full, &local))
		return failed_from (error, local);
	return obj_node (instance_json (node));
}

static JsonNode *
m_prefab_unlink (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgNode    *node = require_node (server, params, error);
	LrgNode    *root;
	JsonObject *o;

	if (node == NULL)
		return NULL;
	root = instance_for (node);

	/* Idempotent: "unlinked" tells whether there was a link to break. */
	o = json_object_new ();
	json_object_set_boolean_member (o, "unlinked",
	                                lrg_editor_prefab_unlink (lrg_editor_server_get_editor (server), root));
	json_object_set_string_member (o, "node", lrg_node_get_guid (root));
	json_object_set_boolean_member (o, "linked", FALSE);
	return obj_node (o);
}

/* ==========================================================================
 * PO, gizmo, letterbox, merge, collab, scenes, VCS panel ops
 * ========================================================================== */

#define TOOLS_GIZMO_KEY  "lrg-editor-server-tools-gizmo"
#define TOOLS_COLLAB_KEY "lrg-editor-server-tools-collab"

static LrgEditorGizmo *
server_gizmo (LrgEditorServer *server)
{
	LrgEditorGizmo *gizmo = g_object_get_data (G_OBJECT (server), TOOLS_GIZMO_KEY);

	if (gizmo == NULL)
	{
		gizmo = lrg_editor_gizmo_new (lrg_editor_server_get_editor (server));
		g_object_set_data_full (G_OBJECT (server), TOOLS_GIZMO_KEY, gizmo, g_object_unref);
	}
	return gizmo;
}

static LrgCollabSession *
server_collab (LrgEditorServer *server)
{
	LrgCollabSession *session = g_object_get_data (G_OBJECT (server), TOOLS_COLLAB_KEY);

	if (session == NULL)
	{
		session = lrg_collab_session_new ();
		g_object_set_data_full (G_OBJECT (server), TOOLS_COLLAB_KEY, session, g_object_unref);
	}
	return session;
}

static gint
pivot_from_name (const gchar *name)
{
	if (g_strcmp0 (name, "median") == 0)
		return LRG_PIVOT_MEDIAN;
	if (g_strcmp0 (name, "bounds") == 0)
		return LRG_PIVOT_BOUNDS;
	if (g_strcmp0 (name, "cursor") == 0)
		return LRG_PIVOT_CURSOR;
	if (g_strcmp0 (name, "active") == 0)
		return LRG_PIVOT_ACTIVE;
	return -1;
}

static JsonNode *
m_po_extract (LrgEditorServer *server,
              JsonObject      *params,
              gpointer         user_data,
              GError         **error)
{
	const gchar   *text = require_str (params, "text", error);
	g_auto(GStrv)  keys = NULL;
	JsonArray     *arr;
	JsonObject    *o;
	guint          i;

	(void) server;
	(void) user_data;
	if (text == NULL)
		return NULL;
	keys = lrg_po_extract (text);
	arr = json_array_new ();
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		json_array_add_string_element (arr, keys[i]);
	o = json_object_new ();
	json_object_set_array_member (o, "keys", arr);
	return obj_node (o);
}

static JsonNode *
m_po_pseudo (LrgEditorServer *server,
             JsonObject      *params,
             gpointer         user_data,
             GError         **error)
{
	const gchar          *text = require_str (params, "text", error);
	g_autoptr(LrgLocale)  src = NULL;
	g_autoptr(LrgLocale)  pseudo = NULL;

	(void) server;
	(void) user_data;
	if (text == NULL)
		return NULL;
	src = lrg_locale_new ("en", "English");
	lrg_locale_set_string (src, "s", text);
	pseudo = lrg_po_pseudolocalize (src);
	return text_node ("text", lrg_locale_get_string (pseudo, "s"));
}

static JsonNode *
m_po_import (LrgEditorServer *server,
             JsonObject      *params,
             gpointer         user_data,
             GError         **error)
{
	const gchar          *path = require_str (params, "path", error);
	const gchar          *id = require_str (params, "id", error);
	g_autofree gchar     *full = NULL;
	g_autoptr(LrgLocale)  locale = NULL;
	g_autoptr(GError)     local = NULL;
	gint                  count;
	JsonObject           *o;

	(void) user_data;
	if (path == NULL || id == NULL)
		return NULL;
	full = tools_resolve_path (server, path);
	locale = lrg_locale_new ("en", "English");
	count = lrg_po_import_file (locale, full, &local);
	if (count < 0)
		return failed_from (error, local);
	o = json_object_new ();
	json_object_set_int_member (o, "count", count);
	set_str_or_null (o, "value", lrg_locale_get_string (locale, id));
	return obj_node (o);
}

static JsonNode *
m_po_export (LrgEditorServer *server,
             JsonObject      *params,
             gpointer         user_data,
             GError         **error)
{
	const gchar          *path = require_str (params, "path", error);
	const gchar          *id = require_str (params, "id", error);
	const gchar          *text = require_str (params, "text", error);
	g_autofree gchar     *full = NULL;
	g_autoptr(LrgLocale)  locale = NULL;
	g_autoptr(GError)     local = NULL;

	(void) user_data;
	if (path == NULL || id == NULL || text == NULL)
		return NULL;
	full = tools_resolve_path (server, path);
	locale = lrg_locale_new ("en", "English");
	lrg_locale_set_string (locale, id, text);
	if (!lrg_po_export_file (locale, full, &local))
		return failed_from (error, local);
	return text_node ("path", full);
}

static JsonNode *
m_gizmo_apply (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	const gchar *op = require_str (params, "op", error);
	gdouble      axis = 0.0;
	gdouble      value = 0.0;
	JsonObject  *o;
	gboolean     ok;

	(void) user_data;
	if (op == NULL)
		return NULL;
	if (has (params, "axis") && !p_num_flex (params, "axis", &axis))
	{
		invalid (error, "'%s' must be a number", "axis");
		return NULL;
	}
	if (has (params, "value") && !p_num_flex (params, "value", &value))
	{
		invalid (error, "'%s' must be a number", "value");
		return NULL;
	}
	ok = lrg_editor_gizmo_apply (server_gizmo (server), op, (gint) axis, (gfloat) value);
	o = json_object_new ();
	json_object_set_boolean_member (o, "ok", ok);
	return obj_node (o);
}

static JsonNode *
m_gizmo_pivot (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	const gchar *mode = require_str (params, "mode", error);
	gint         pivot;
	gfloat       origin[3];
	JsonObject  *o;

	(void) user_data;
	if (mode == NULL)
		return NULL;
	pivot = pivot_from_name (mode);
	if (pivot < 0)
	{
		invalid (error, "Unknown pivot '%s'", mode);
		return NULL;
	}
	lrg_editor_gizmo_set_pivot_mode (server_gizmo (server), pivot);
	lrg_editor_gizmo_get_origin (server_gizmo (server), origin);
	o = json_object_new ();
	json_object_set_string_member (o, "mode", mode);
	json_object_set_double_member (o, "x", origin[0]);
	json_object_set_double_member (o, "y", origin[1]);
	json_object_set_double_member (o, "z", origin[2]);
	return obj_node (o);
}

static JsonNode *
m_gizmo_cursor (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	gdouble x = 0.0;
	gdouble y = 0.0;
	gdouble z = 0.0;

	(void) user_data;
	if (!p_num_flex (params, "x", &x) || !p_num_flex (params, "y", &y) || !p_num_flex (params, "z", &z))
	{
		invalid (error, "Need numbers x, y and z");
		return NULL;
	}
	lrg_editor_gizmo_set_cursor (server_gizmo (server), (gfloat) x, (gfloat) y, (gfloat) z);
	return text_node ("mode", "cursor");
}

static JsonNode *
m_gizmo_origin (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	gfloat      origin[3];
	JsonObject *o;

	(void) params;
	(void) user_data;
	(void) error;
	lrg_editor_gizmo_get_origin (server_gizmo (server), origin);
	o = json_object_new ();
	json_object_set_double_member (o, "x", origin[0]);
	json_object_set_double_member (o, "y", origin[1]);
	json_object_set_double_member (o, "z", origin[2]);
	return obj_node (o);
}

static JsonNode *
m_view_letterbox (LrgEditorServer *server,
                  JsonObject      *params,
                  gpointer         user_data,
                  GError         **error)
{
	const gchar *preset = require_str (params, "preset", error);
	gdouble      w = 0.0;
	gdouble      h = 0.0;
	gint         x = 0;
	gint         y = 0;
	gint         rw = 0;
	gint         rh = 0;
	JsonObject  *o;

	(void) server;
	(void) user_data;
	if (preset == NULL)
		return NULL;
	if (!p_num_flex (params, "w", &w) || !p_num_flex (params, "h", &h))
	{
		invalid (error, "Need numbers w and h");
		return NULL;
	}
	lrg_game_view_letterbox (preset, (gint) w, (gint) h, &x, &y, &rw, &rh);
	o = json_object_new ();
	json_object_set_int_member (o, "x", x);
	json_object_set_int_member (o, "y", y);
	json_object_set_int_member (o, "w", rw);
	json_object_set_int_member (o, "h", rh);
	return obj_node (o);
}

static JsonNode *
m_level_merge (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	const gchar       *base = require_str (params, "base", error);
	const gchar       *ours = require_str (params, "ours", error);
	const gchar       *theirs = require_str (params, "theirs", error);
	const gchar       *out = require_str (params, "out", error);
	g_autofree gchar  *base_full = NULL;
	g_autofree gchar  *ours_full = NULL;
	g_autofree gchar  *theirs_full = NULL;
	g_autofree gchar  *out_full = NULL;
	g_auto(GStrv)      conflicts = NULL;
	g_autoptr(GError)  local = NULL;
	JsonArray         *arr;
	JsonObject        *o;
	guint              i;

	(void) user_data;
	if (base == NULL || ours == NULL || theirs == NULL || out == NULL)
		return NULL;
	base_full = tools_resolve_path (server, base);
	ours_full = tools_resolve_path (server, ours);
	theirs_full = tools_resolve_path (server, theirs);
	out_full = tools_resolve_path (server, out);
	if (!lrg_level_merge_files (base_full, ours_full, theirs_full, out_full, &conflicts, &local))
		return failed_from (error, local);
	arr = json_array_new ();
	for (i = 0; conflicts != NULL && conflicts[i] != NULL; i++)
		json_array_add_string_element (arr, conflicts[i]);
	o = json_object_new ();
	json_object_set_string_member (o, "out", out_full);
	json_object_set_array_member (o, "conflicts", arr);
	return obj_node (o);
}

static JsonNode *
m_collab_apply (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	const gchar       *user = require_str (params, "user", error);
	const gchar       *guid = require_str (params, "guid", error);
	const gchar       *prop = require_str (params, "prop", error);
	const gchar       *value = require_str (params, "value", error);
	guint64            revision = 0;
	g_autoptr(GError)  local = NULL;

	(void) user_data;
	if (user == NULL || guid == NULL || prop == NULL || value == NULL)
		return NULL;
	if (!p_u64 (params, "revision", &revision))
	{
		invalid (error, "Missing number parameter '%s'", "revision");
		return NULL;
	}
	if (!lrg_collab_apply (server_collab (server), user, guid, revision, prop, value, &local))
		return failed_from (error, local);
	return text_node ("value", value);
}

static JsonNode *
m_collab_get (LrgEditorServer *server,
              JsonObject      *params,
              gpointer         user_data,
              GError         **error)
{
	const gchar *guid = require_str (params, "guid", error);
	const gchar *prop = require_str (params, "prop", error);
	JsonObject  *o;

	(void) user_data;
	if (guid == NULL || prop == NULL)
		return NULL;
	o = json_object_new ();
	set_str_or_null (o, "value", lrg_collab_get (server_collab (server), guid, prop));
	return obj_node (o);
}

static JsonNode *
m_collab_lock (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	const gchar       *user = require_str (params, "user", error);
	const gchar       *guid = require_str (params, "guid", error);
	g_autoptr(GError)  local = NULL;
	gboolean           unlock;

	if (user == NULL || guid == NULL)
		return NULL;
	/* collab.unlock is this function with user_data set. */
	unlock = user_data != NULL;
	if (unlock)
	{
		if (!lrg_collab_unlock (server_collab (server), user, guid, &local))
			return failed_from (error, local);
	}
	else if (!lrg_collab_lock (server_collab (server), user, guid, &local))
		return failed_from (error, local);
	return text_node ("holder", lrg_collab_lock_holder (server_collab (server), guid));
}

static JsonNode *
m_collab_cursor (LrgEditorServer *server,
                 JsonObject      *params,
                 gpointer         user_data,
                 GError         **error)
{
	const gchar *user = require_str (params, "user", error);
	gdouble      x = 0.0;
	gdouble      y = 0.0;
	gdouble      z = 0.0;

	(void) user_data;
	if (user == NULL)
		return NULL;
	if (!p_num_flex (params, "x", &x) || !p_num_flex (params, "y", &y) || !p_num_flex (params, "z", &z))
	{
		invalid (error, "Need numbers x, y and z");
		return NULL;
	}
	lrg_collab_set_cursor (server_collab (server), user, (gfloat) x, (gfloat) y, (gfloat) z);
	return count_node ("count", lrg_collab_presence_count (server_collab (server)));
}

static JsonNode *
m_collab_presence (LrgEditorServer *server,
                   JsonObject      *params,
                   gpointer         user_data,
                   GError         **error)
{
	LrgCollabSession *session = server_collab (server);
	JsonArray        *users;
	JsonObject       *o;
	guint             i;
	guint             n;

	(void) params;
	(void) user_data;
	(void) error;
	users = json_array_new ();
	n = lrg_collab_presence_count (session);
	for (i = 0; i < n; i++)
	{
		const gchar *user = NULL;
		gfloat       x = 0.0f;
		gfloat       y = 0.0f;
		gfloat       z = 0.0f;
		JsonObject  *row;

		if (!lrg_collab_presence_get (session, i, &user, &x, &y, &z))
			continue;
		row = json_object_new ();
		json_object_set_string_member (row, "user", user != NULL ? user : "");
		json_object_set_double_member (row, "x", x);
		json_object_set_double_member (row, "y", y);
		json_object_set_double_member (row, "z", z);
		json_array_add_object_element (users, row);
	}
	o = json_object_new ();
	json_object_set_array_member (o, "users", users);
	return obj_node (o);
}

static JsonNode *
m_scenes_list (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	LrgEditorScenes *scenes;
	JsonArray       *paths;
	JsonObject      *o;
	guint            i;
	guint            n;

	(void) params;
	(void) user_data;
	(void) error;
	scenes = lrg_editor_scenes_ensure (G_OBJECT (lrg_editor_server_get_editor (server)));
	paths = json_array_new ();
	n = lrg_editor_scenes_get_count (scenes);
	for (i = 0; i < n; i++)
	{
		const gchar *path = lrg_editor_scenes_get_path (scenes, i);

		json_array_add_string_element (paths, path != NULL ? path : "");
	}
	o = json_object_new ();
	json_object_set_array_member (o, "paths", paths);
	json_object_set_int_member (o, "active", lrg_editor_scenes_get_active (scenes));
	return obj_node (o);
}

static JsonNode *
m_scenes_open (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	const gchar       *path = require_str (params, "path", error);
	g_autofree gchar  *full = NULL;
	g_autoptr(GError)  local = NULL;
	LrgEditor         *editor;
	LrgEditorScenes   *scenes;
	guint              index;

	(void) user_data;
	if (path == NULL)
		return NULL;
	full = tools_resolve_path (server, path);
	editor = lrg_editor_server_get_editor (server);
	if (!lrg_editor_load_level (editor, full, &local))
		return failed_from (error, local);
	scenes = lrg_editor_scenes_ensure (G_OBJECT (editor));
	index = lrg_editor_scenes_add (scenes, full);
	return count_node ("index", index);
}

static JsonNode *
m_scenes_active (LrgEditorServer *server,
                 JsonObject      *params,
                 gpointer         user_data,
                 GError         **error)
{
	gdouble            index = 0.0;
	LrgEditor         *editor;
	LrgEditorScenes   *scenes;
	const gchar       *path;
	g_autoptr(GError)  local = NULL;

	(void) user_data;
	if (!p_num_flex (params, "index", &index))
	{
		invalid (error, "Missing number parameter '%s'", "index");
		return NULL;
	}
	editor = lrg_editor_server_get_editor (server);
	scenes = lrg_editor_scenes_ensure (G_OBJECT (editor));
	path = lrg_editor_scenes_get_path (scenes, (guint) index);
	if (path == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No scene %d", (gint) index);
		return NULL;
	}
	if (!lrg_editor_load_level (editor, path, &local))
		return failed_from (error, local);
	lrg_editor_scenes_set_active (scenes, (guint) index);
	return text_node ("path", path);
}

static JsonNode *
m_vcs_text (LrgEditorServer *server,
            JsonObject      *params,
            gpointer         user_data,
            GError         **error)
{
	const gchar       *which = user_data;
	LrgEditorVcs      *vcs = server_vcs (server);
	g_autoptr(GError)  local = NULL;
	g_autofree gchar  *text = NULL;

	(void) params;
	if (g_strcmp0 (which, "diff") == 0)
		text = lrg_editor_vcs_diff (vcs, &local);
	else if (g_strcmp0 (which, "log") == 0)
		text = lrg_editor_vcs_log (vcs, &local);
	else if (g_strcmp0 (which, "magit") == 0)
		return text_node ("form", lrg_editor_vcs_magit_handoff (vcs));
	else if (g_strcmp0 (which, "locks") == 0)
	{
		gboolean    available = FALSE;
		JsonObject *o;

		text = lrg_editor_vcs_lfs_locks (vcs, &available, &local);
		if (text == NULL)
			return failed_from (error, local);
		o = json_object_new ();
		json_object_set_boolean_member (o, "available", available);
		json_object_set_string_member (o, "text", text);
		return obj_node (o);
	}
	if (text == NULL)
		return failed_from (error, local);
	return text_node ("text", text);
}

static JsonNode *
m_vcs_commit (LrgEditorServer *server,
              JsonObject      *params,
              gpointer         user_data,
              GError         **error)
{
	const gchar       *message = require_str (params, "message", error);
	g_autoptr(GError)  local = NULL;
	g_autofree gchar  *text = NULL;

	(void) user_data;
	if (message == NULL)
		return NULL;
	text = lrg_editor_vcs_commit (server_vcs (server), message, &local);
	if (text == NULL)
		return failed_from (error, local);
	return text_node ("text", text);
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
		{ "asset.meta.get", m_asset_meta_get },
		{ "asset.meta.set", m_asset_meta_set },
		{ "vcs.status", m_vcs_status },
		{ "prefab.overrides", m_prefab_overrides },
		{ "prefab.apply", m_prefab_apply },
		{ "prefab.revert", m_prefab_revert },
		{ "prefab.sync", m_prefab_sync },
		{ "prefab.relink", m_prefab_relink },
		{ "prefab.unlink", m_prefab_unlink },
		{ "po.extract", m_po_extract },
		{ "po.pseudo", m_po_pseudo },
		{ "po.import", m_po_import },
		{ "po.export", m_po_export },
		{ "gizmo.apply", m_gizmo_apply },
		{ "gizmo.pivot", m_gizmo_pivot },
		{ "gizmo.cursor", m_gizmo_cursor },
		{ "gizmo.origin", m_gizmo_origin },
		{ "view.letterbox", m_view_letterbox },
		{ "level.merge", m_level_merge },
		{ "collab.apply", m_collab_apply },
		{ "collab.get", m_collab_get },
		{ "collab.lock", m_collab_lock },
		{ "collab.unlock", m_collab_lock },
		{ "collab.cursor", m_collab_cursor },
		{ "collab.presence", m_collab_presence },
		{ "scenes.list", m_scenes_list },
		{ "scenes.open", m_scenes_open },
		{ "scenes.active", m_scenes_active },
		{ "vcs.diff", m_vcs_text },
		{ "vcs.log", m_vcs_text },
		{ "vcs.locks", m_vcs_text },
		{ "vcs.magit", m_vcs_text },
		{ "vcs.commit", m_vcs_commit },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (table); i++)
	{
		gpointer data = NULL;

		if (g_strcmp0 (table[i].name, "vcs.diff") == 0)
			data = (gpointer) "diff";
		else if (g_strcmp0 (table[i].name, "vcs.log") == 0)
			data = (gpointer) "log";
		else if (g_strcmp0 (table[i].name, "vcs.locks") == 0)
			data = (gpointer) "locks";
		else if (g_strcmp0 (table[i].name, "vcs.magit") == 0)
			data = (gpointer) "magit";
		else if (g_strcmp0 (table[i].name, "collab.unlock") == 0)
			data = (gpointer) "unlock";
		lrg_editor_server_register_method (self, table[i].name, table[i].func, data, NULL);
	}
}
