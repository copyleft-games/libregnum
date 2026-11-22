/* lrg-tiled-tmj.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Reads Tiled's JSON map format (.tmj) and tileset format (.tsj) with
 * json-glib into the importer's model (lrg-tiled-map-private.h).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tiled-map-private.h"
#include "../lrg-log.h"

#include <json-glib/json-glib.h>
#include <string.h>

/* ==========================================================================
 * Member readers that tolerate absent members and loose number types
 * ========================================================================== */

static gdouble
member_double (JsonObject  *object,
               const gchar *name,
               gdouble      fallback)
{
	JsonNode *node = json_object_get_member (object, name);

	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
		return fallback;
	switch (json_node_get_value_type (node))
	{
	case G_TYPE_INT64:
		return (gdouble) json_node_get_int (node);
	case G_TYPE_DOUBLE:
		return json_node_get_double (node);
	case G_TYPE_BOOLEAN:
		return json_node_get_boolean (node) ? 1.0 : 0.0;
	default:
		return fallback;
	}
}

static guint
member_uint (JsonObject  *object,
             const gchar *name,
             guint        fallback)
{
	gdouble v = member_double (object, name, -1.0);

	if (v < 0.0 || v > (gdouble) G_MAXUINT32)
		return fallback;
	return (guint) v;
}

static gboolean
member_bool (JsonObject  *object,
             const gchar *name,
             gboolean     fallback)
{
	JsonNode *node = json_object_get_member (object, name);

	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
		return fallback;
	if (json_node_get_value_type (node) == G_TYPE_BOOLEAN)
		return json_node_get_boolean (node);
	return member_double (object, name, fallback ? 1.0 : 0.0) != 0.0;
}

static const gchar *
member_string (JsonObject  *object,
               const gchar *name)
{
	JsonNode *node = json_object_get_member (object, name);

	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) ||
	    json_node_get_value_type (node) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (node);
}

static JsonArray *
member_array (JsonObject  *object,
              const gchar *name)
{
	JsonNode *node = json_object_get_member (object, name);

	if (node == NULL || !JSON_NODE_HOLDS_ARRAY (node))
		return NULL;
	return json_node_get_array (node);
}

static JsonObject *
member_object (JsonObject  *object,
               const gchar *name)
{
	JsonNode *node = json_object_get_member (object, name);

	if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
		return NULL;
	return json_node_get_object (node);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/* Text form of a JSON scalar, as Tiled would write it in XML. */
static gchar *
scalar_to_string (JsonNode *node)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
		return g_strdup ("");
	switch (json_node_get_value_type (node))
	{
	case G_TYPE_STRING:
		return g_strdup (json_node_get_string (node));
	case G_TYPE_INT64:
		return g_strdup_printf ("%" G_GINT64_FORMAT, json_node_get_int (node));
	case G_TYPE_DOUBLE:
		return g_strdup (g_ascii_dtostr (buf, sizeof (buf), json_node_get_double (node)));
	case G_TYPE_BOOLEAN:
		return g_strdup (json_node_get_boolean (node) ? "true" : "false");
	default:
		return g_strdup ("");
	}
}

/* Flattens a class value's members into "prefix.member" keys. */
static void
add_class_members (LrgTiledProperties *props,
                   const gchar        *prefix,
                   JsonObject         *members)
{
	GList *names;
	GList *l;

	names = json_object_get_members (members);
	for (l = names; l != NULL; l = l->next)
	{
		const gchar *name = l->data;
		JsonNode *node = json_object_get_member (members, name);
		g_autofree gchar *key = g_strdup_printf ("%s.%s", prefix, name);

		if (JSON_NODE_HOLDS_OBJECT (node))
		{
			add_class_members (props, key, json_node_get_object (node));
			continue;
		}
		if (!JSON_NODE_HOLDS_VALUE (node))
			continue;
		switch (json_node_get_value_type (node))
		{
		case G_TYPE_INT64:
			lrg_tiled_properties_take (props, key, LRG_TILED_PROPERTY_INT, scalar_to_string (node));
			break;
		case G_TYPE_DOUBLE:
			lrg_tiled_properties_take (props, key, LRG_TILED_PROPERTY_FLOAT, scalar_to_string (node));
			break;
		case G_TYPE_BOOLEAN:
			lrg_tiled_properties_take (props, key, LRG_TILED_PROPERTY_BOOL, scalar_to_string (node));
			break;
		default:
			lrg_tiled_properties_take (props, key, LRG_TILED_PROPERTY_STRING, scalar_to_string (node));
			break;
		}
	}
	g_list_free (names);
}

static void
read_properties (LrgTiledProperties *props,
                 JsonObject         *owner,
                 const gchar        *base_dir)
{
	JsonArray *array = member_array (owner, "properties");
	guint i;

	if (array == NULL)
		return;
	for (i = 0; i < json_array_get_length (array); i++)
	{
		JsonObject *p = json_array_get_object_element (array, i);
		const gchar *name;
		const gchar *type;
		JsonNode *value;
		LrgTiledPropertyType ptype;
		gchar *text;

		if (p == NULL || (name = member_string (p, "name")) == NULL)
			continue;
		type = member_string (p, "type");
		value = json_object_get_member (p, "value");

		if (g_strcmp0 (type, "class") == 0)
		{
			if (value != NULL && JSON_NODE_HOLDS_OBJECT (value))
				add_class_members (props, name, json_node_get_object (value));
			continue;
		}
		if (!lrg_tiled_property_type_parse (type, &ptype))
			ptype = LRG_TILED_PROPERTY_STRING;

		text = scalar_to_string (value);
		if (ptype == LRG_TILED_PROPERTY_FILE && *text != '\0')
		{
			gchar *resolved = lrg_tiled_resolve_path (base_dir, text);

			g_free (text);
			text = resolved;
		}
		lrg_tiled_properties_take (props, name, ptype, text);
	}
}

/* ==========================================================================
 * Objects
 * ========================================================================== */

static gboolean
read_points (LrgTiledObject  *object,
             JsonArray       *array)
{
	guint i;

	object->points = g_array_new (FALSE, FALSE, sizeof (gfloat));
	for (i = 0; i < json_array_get_length (array); i++)
	{
		JsonObject *pt = json_array_get_object_element (array, i);
		gfloat x;
		gfloat y;

		if (pt == NULL)
			return FALSE;
		x = (gfloat) member_double (pt, "x", 0.0);
		y = (gfloat) member_double (pt, "y", 0.0);
		g_array_append_val (object->points, x);
		g_array_append_val (object->points, y);
	}
	return TRUE;
}

static LrgTiledObject *
read_object (JsonObject   *o,
             const gchar  *base_dir,
             GError      **error)
{
	g_autoptr(LrgTiledObject) object = NULL;
	const gchar *s;
	JsonArray *poly;
	JsonObject *text;

	if (json_object_has_member (o, "template"))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
		             "object %u uses a template ('%s'); detach it in Tiled "
		             "(Replace with Template ... / Detach)", member_uint (o, "id", 0),
		             member_string (o, "template"));
		return NULL;
	}

	object = lrg_tiled_object_new_empty ();
	object->id = member_uint (o, "id", 0);
	if ((s = member_string (o, "name")) != NULL)
	{
		g_free (object->name);
		object->name = g_strdup (s);
	}
	/* "class" since Tiled 1.9, "type" before; either names the type */
	s = member_string (o, "class");
	if (s == NULL || *s == '\0')
		s = member_string (o, "type");
	if (s != NULL)
	{
		g_free (object->type);
		object->type = g_strdup (s);
	}
	object->x = member_double (o, "x", 0.0);
	object->y = member_double (o, "y", 0.0);
	object->width = member_double (o, "width", 0.0);
	object->height = member_double (o, "height", 0.0);
	object->rotation = member_double (o, "rotation", 0.0);
	object->visible = member_bool (o, "visible", TRUE);
	object->gid = member_uint (o, "gid", 0);

	if (object->gid != 0)
		object->shape = LRG_TILED_OBJECT_TILE;
	else if (member_bool (o, "point", FALSE))
		object->shape = LRG_TILED_OBJECT_POINT;
	else if (member_bool (o, "ellipse", FALSE))
		object->shape = LRG_TILED_OBJECT_ELLIPSE;
	else if ((poly = member_array (o, "polygon")) != NULL)
	{
		object->shape = LRG_TILED_OBJECT_POLYGON;
		if (!read_points (object, poly))
			goto bad_points;
	}
	else if ((poly = member_array (o, "polyline")) != NULL)
	{
		object->shape = LRG_TILED_OBJECT_POLYLINE;
		if (!read_points (object, poly))
			goto bad_points;
	}
	else if ((text = member_object (o, "text")) != NULL)
	{
		object->shape = LRG_TILED_OBJECT_TEXT;
		object->text = g_strdup (member_string (text, "text"));
	}

	read_properties (object->properties, o, base_dir);
	return g_steal_pointer (&object);

bad_points:
	g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
	             "object %u has a malformed point list", object->id);
	return NULL;
}

/* ==========================================================================
 * Tilesets
 * ========================================================================== */

static gboolean
read_tileset_body (LrgTiledTileset  *ts,
                   JsonObject       *o,
                   const gchar      *base_dir,
                   GError          **error)
{
	JsonObject *offset;
	JsonArray *tiles;
	const gchar *s;
	guint i;

	if ((s = member_string (o, "name")) != NULL)
	{
		g_free (ts->name);
		ts->name = g_strdup (s);
	}
	ts->tile_width = member_uint (o, "tilewidth", 0);
	ts->tile_height = member_uint (o, "tileheight", 0);
	ts->tile_count = member_uint (o, "tilecount", 0);
	ts->columns = member_uint (o, "columns", 0);
	ts->margin = member_uint (o, "margin", 0);
	ts->spacing = member_uint (o, "spacing", 0);
	ts->image_width = member_uint (o, "imagewidth", 0);
	ts->image_height = member_uint (o, "imageheight", 0);
	if ((s = member_string (o, "image")) != NULL)
	{
		g_free (ts->image_path);
		ts->image_path = lrg_tiled_resolve_path (base_dir, s);
	}
	if ((offset = member_object (o, "tileoffset")) != NULL)
	{
		ts->offset_x = (gint) member_double (offset, "x", 0.0);
		ts->offset_y = (gint) member_double (offset, "y", 0.0);
	}
	read_properties (ts->properties, o, base_dir);

	tiles = member_array (o, "tiles");
	for (i = 0; tiles != NULL && i < json_array_get_length (tiles); i++)
	{
		JsonObject *t = json_array_get_object_element (tiles, i);
		LrgTiledTile *tile;
		JsonArray *anim;
		JsonObject *group;
		guint local_id;

		if (t == NULL || !json_object_has_member (t, "id"))
			continue;
		local_id = member_uint (t, "id", 0);
		tile = lrg_tiled_tileset_ensure_tile (ts, local_id);

		s = member_string (t, "class");
		if (s == NULL || *s == '\0')
			s = member_string (t, "type");
		if (s != NULL && *s != '\0')
		{
			g_free (tile->type);
			tile->type = g_strdup (s);
		}
		read_properties (tile->properties, t, base_dir);

		if ((anim = member_array (t, "animation")) != NULL)
		{
			guint k;

			tile->anim_frames = g_array_new (FALSE, FALSE, sizeof (guint));
			tile->anim_ms = g_array_new (FALSE, FALSE, sizeof (guint));
			for (k = 0; k < json_array_get_length (anim); k++)
			{
				JsonObject *f = json_array_get_object_element (anim, k);
				guint frame;
				guint ms;

				if (f == NULL)
					continue;
				frame = member_uint (f, "tileid", 0);
				ms = MAX (member_uint (f, "duration", 100), 1u);
				g_array_append_val (tile->anim_frames, frame);
				g_array_append_val (tile->anim_ms, ms);
			}
		}

		if ((group = member_object (t, "objectgroup")) != NULL)
		{
			JsonArray *objects = member_array (group, "objects");
			guint k;

			tile->shapes = g_ptr_array_new_with_free_func (g_object_unref);
			for (k = 0; objects != NULL && k < json_array_get_length (objects); k++)
			{
				JsonObject *so = json_array_get_object_element (objects, k);
				LrgTiledObject *shape;

				if (so == NULL)
					continue;
				shape = read_object (so, base_dir, error);
				if (shape == NULL)
					return FALSE;
				g_ptr_array_add (tile->shapes, shape);
			}
		}
	}
	return TRUE;
}

static JsonNode *
parse_root (const gchar  *data,
            gsize         length,
            GError      **error)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GError) local = NULL;
	JsonNode *root;

	if (!json_parser_load_from_data (parser, data, (gssize) length, &local))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_PARSE,
		             "invalid JSON: %s", local->message);
		return NULL;
	}
	root = json_parser_get_root (parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_PARSE,
		             "the document is not a JSON object");
		return NULL;
	}
	return json_node_copy (root);
}

gboolean
lrg_tiled_parse_tsj (LrgTiledTileset  *tileset,
                     const gchar      *data,
                     gsize             length,
                     const gchar      *base_dir,
                     GError          **error)
{
	g_autoptr(JsonNode) root = parse_root (data, length, error);

	if (root == NULL)
		return FALSE;
	return read_tileset_body (tileset, json_node_get_object (root), base_dir, error);
}

/* ==========================================================================
 * Layers
 * ========================================================================== */

static gboolean read_layers (LrgTiledMap               *map,
                             JsonArray                 *array,
                             const LrgTiledGroupState  *group,
                             GError                   **error);

static void
read_layer_common (LrgTiledLayer *layer,
                   JsonObject    *o,
                   const gchar   *base_dir)
{
	layer->id = member_uint (o, "id", 0);
	layer->visible = member_bool (o, "visible", TRUE);
	layer->opacity = member_double (o, "opacity", 1.0);
	layer->offset_x = member_double (o, "offsetx", 0.0);
	layer->offset_y = member_double (o, "offsety", 0.0);
	layer->parallax_x = member_double (o, "parallaxx", 1.0);
	layer->parallax_y = member_double (o, "parallaxy", 1.0);
	read_properties (layer->properties, o, base_dir);
}

static gboolean
read_tile_data (LrgTiledLayer  *layer,
                JsonObject     *o,
                GError        **error)
{
	JsonNode *data = json_object_get_member (o, "data");
	guint n = layer->width * layer->height;

	if (json_object_has_member (o, "chunks"))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
		             "layer '%s' is chunked (infinite map); turn Infinite off in Map "
		             "Properties", layer->name);
		return FALSE;
	}
	if (n == 0 || data == NULL)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "tile layer '%s' has no size or no data", layer->name);
		return FALSE;
	}
	layer->gids = g_new0 (guint, n);

	if (JSON_NODE_HOLDS_ARRAY (data))
	{
		JsonArray *array = json_node_get_array (data);
		guint i;

		if (json_array_get_length (array) != n)
		{
			g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
			             "tile layer '%s' has %u cells, expected %u", layer->name,
			             json_array_get_length (array), n);
			return FALSE;
		}
		for (i = 0; i < n; i++)
		{
			JsonNode *cell = json_array_get_element (array, i);
			gdouble v = 0.0;

			if (JSON_NODE_HOLDS_VALUE (cell))
				v = (json_node_get_value_type (cell) == G_TYPE_INT64)
				    ? (gdouble) json_node_get_int (cell) : json_node_get_double (cell);
			if (v < 0.0 || v > (gdouble) G_MAXUINT32)
			{
				g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
				             "tile layer '%s' cell %u is out of range", layer->name, i);
				return FALSE;
			}
			layer->gids[i] = (guint) v;
		}
		return TRUE;
	}

	if (JSON_NODE_HOLDS_VALUE (data) && json_node_get_value_type (data) == G_TYPE_STRING)
	{
		const gchar *encoding = member_string (o, "encoding");

		return lrg_tiled_decode_data (json_node_get_string (data),
		                              encoding != NULL ? encoding : "base64",
		                              member_string (o, "compression"),
		                              layer->gids, n, error);
	}

	g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
	             "tile layer '%s' data is neither an array nor a string", layer->name);
	return FALSE;
}

static gboolean
read_layer (LrgTiledMap               *map,
            JsonObject                *o,
            const LrgTiledGroupState  *group,
            GError                   **error)
{
	g_autoptr(LrgTiledLayer) layer = NULL;
	const gchar *type = member_string (o, "type");
	const gchar *name = member_string (o, "name");

	if (g_strcmp0 (type, "group") == 0)
	{
		LrgTiledGroupState inner;
		g_autofree gchar *prefix = NULL;
		JsonArray *children = member_array (o, "layers");

		prefix = (group->prefix != NULL && *group->prefix != '\0')
		         ? g_strdup_printf ("%s/%s", group->prefix, name != NULL ? name : "")
		         : g_strdup (name != NULL ? name : "");
		inner.prefix = prefix;
		inner.visible = group->visible && member_bool (o, "visible", TRUE);
		inner.opacity = group->opacity * member_double (o, "opacity", 1.0);
		inner.offset_x = group->offset_x + member_double (o, "offsetx", 0.0);
		inner.offset_y = group->offset_y + member_double (o, "offsety", 0.0);
		inner.parallax_x = group->parallax_x * member_double (o, "parallaxx", 1.0);
		inner.parallax_y = group->parallax_y * member_double (o, "parallaxy", 1.0);
		return children == NULL || read_layers (map, children, &inner, error);
	}

	if (g_strcmp0 (type, "tilelayer") == 0)
	{
		layer = lrg_tiled_layer_new_empty (LRG_TILED_LAYER_TILES);
		read_layer_common (layer, o, map->base_dir);
		lrg_tiled_layer_apply_group (layer, name, group);
		layer->width = member_uint (o, "width", 0);
		layer->height = member_uint (o, "height", 0);
		if (!read_tile_data (layer, o, error))
			return FALSE;
	}
	else if (g_strcmp0 (type, "objectgroup") == 0)
	{
		JsonArray *objects = member_array (o, "objects");
		guint i;

		layer = lrg_tiled_layer_new_empty (LRG_TILED_LAYER_OBJECTS);
		read_layer_common (layer, o, map->base_dir);
		for (i = 0; objects != NULL && i < json_array_get_length (objects); i++)
		{
			JsonObject *jo = json_array_get_object_element (objects, i);
			LrgTiledObject *object;

			if (jo == NULL)
				continue;
			object = read_object (jo, map->base_dir, error);
			if (object == NULL)
				return FALSE;
			g_ptr_array_add (layer->objects, object);
		}
		/* after the objects: apply_group moves them by the total offset */
		lrg_tiled_layer_apply_group (layer, name, group);
	}
	else if (g_strcmp0 (type, "imagelayer") == 0)
	{
		layer = lrg_tiled_layer_new_empty (LRG_TILED_LAYER_IMAGE);
		read_layer_common (layer, o, map->base_dir);
		lrg_tiled_layer_apply_group (layer, name, group);
		layer->image_path = lrg_tiled_resolve_path (map->base_dir, member_string (o, "image"));
	}
	else
	{
		lrg_debug (LRG_LOG_DOMAIN_TILEMAP, "skipping Tiled layer of type '%s'",
		           type != NULL ? type : "(none)");
		return TRUE;
	}

	g_ptr_array_add (map->layers, g_steal_pointer (&layer));
	return TRUE;
}

static gboolean
read_layers (LrgTiledMap               *map,
             JsonArray                 *array,
             const LrgTiledGroupState  *group,
             GError                   **error)
{
	guint i;

	for (i = 0; i < json_array_get_length (array); i++)
	{
		JsonObject *o = json_array_get_object_element (array, i);

		if (o != NULL && !read_layer (map, o, group, error))
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Map
 * ========================================================================== */

LrgTiledMap *
lrg_tiled_parse_tmj (const gchar  *data,
                     gsize         length,
                     const gchar  *base_dir,
                     GError      **error)
{
	g_autoptr(JsonNode) root = NULL;
	g_autoptr(LrgTiledMap) map = NULL;
	LrgTiledGroupState top = { NULL, TRUE, 1.0, 0.0, 0.0, 1.0, 1.0 };
	JsonObject *o;
	JsonArray *array;
	const gchar *s;
	guint i;

	root = parse_root (data, length, error);
	if (root == NULL)
		return NULL;
	o = json_node_get_object (root);

	if (g_strcmp0 (member_string (o, "type"), "map") != 0 &&
	    !json_object_has_member (o, "layers"))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "the JSON document is not a Tiled map");
		return NULL;
	}
	if (member_bool (o, "infinite", FALSE))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
		             "infinite maps are not supported; turn Infinite off in Map Properties");
		return NULL;
	}

	map = lrg_tiled_map_new_empty (base_dir);
	map->width = member_uint (o, "width", 0);
	map->height = member_uint (o, "height", 0);
	map->tile_width = member_uint (o, "tilewidth", 0);
	map->tile_height = member_uint (o, "tileheight", 0);
	map->hex_side = member_uint (o, "hexsidelength", 0);

	s = member_string (o, "orientation");
	if (g_strcmp0 (s, "isometric") == 0)
		map->orientation = LRG_TILEMAP_ORIENTATION_ISOMETRIC;
	else if (g_strcmp0 (s, "staggered") == 0)
		map->orientation = LRG_TILEMAP_ORIENTATION_STAGGERED;
	else if (g_strcmp0 (s, "hexagonal") == 0)
		map->orientation = LRG_TILEMAP_ORIENTATION_HEXAGONAL;
	if (g_strcmp0 (member_string (o, "staggeraxis"), "x") == 0)
		map->stagger_axis = LRG_TILEMAP_STAGGER_AXIS_X;
	if (g_strcmp0 (member_string (o, "staggerindex"), "even") == 0)
		map->stagger_index = LRG_TILEMAP_STAGGER_INDEX_EVEN;
	map->background = g_strdup (member_string (o, "backgroundcolor"));
	read_properties (map->properties, o, base_dir);

	array = member_array (o, "tilesets");
	for (i = 0; array != NULL && i < json_array_get_length (array); i++)
	{
		JsonObject *t = json_array_get_object_element (array, i);
		g_autoptr(LrgTiledTileset) ts = NULL;
		const gchar *source;

		if (t == NULL)
			continue;
		ts = lrg_tiled_tileset_new_empty ();
		ts->first_gid = member_uint (t, "firstgid", 0);
		source = member_string (t, "source");
		if (source != NULL)
		{
			g_autofree gchar *path = lrg_tiled_resolve_path (base_dir, source);

			if (!lrg_tiled_load_external_tileset (ts, path, error))
				return NULL;
		}
		else if (!read_tileset_body (ts, t, base_dir, error))
			return NULL;
		g_ptr_array_add (map->tilesets, g_steal_pointer (&ts));
	}

	array = member_array (o, "layers");
	if (array != NULL && !read_layers (map, array, &top, error))
		return NULL;

	if (!lrg_tiled_map_finish (map, error))
		return NULL;
	return g_steal_pointer (&map);
}
