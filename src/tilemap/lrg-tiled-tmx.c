/* lrg-tiled-tmx.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Reads Tiled's XML map format (.tmx) and tileset format (.tsx).  The
 * document is parsed with GLib's GMarkup into a small element tree,
 * which is then walked into the importer's model; Tiled files are small
 * and the tree keeps the walk as plain as the JSON reader's.  GMarkup
 * accepts the XML Tiled writes (no DTD, no entities beyond the five
 * predefined ones).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tiled-map-private.h"
#include "../lrg-log.h"

#include <string.h>

/* ==========================================================================
 * A minimal element tree
 * ========================================================================== */

typedef struct _XmlNode XmlNode;
struct _XmlNode
{
	gchar     *name;
	gchar    **attr_names;
	gchar    **attr_values;
	GString   *text;
	GPtrArray *children;    /* XmlNode */
};

static void
xml_node_free (gpointer data)
{
	XmlNode *node = data;

	if (node == NULL)
		return;
	g_free (node->name);
	g_strfreev (node->attr_names);
	g_strfreev (node->attr_values);
	g_string_free (node->text, TRUE);
	g_ptr_array_unref (node->children);
	g_free (node);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (XmlNode, xml_node_free)

typedef struct
{
	XmlNode   *root;
	GPtrArray *stack;       /* borrowed XmlNode, innermost last */
} XmlBuild;

static void
on_start (GMarkupParseContext  *context,
          const gchar          *element_name,
          const gchar         **attribute_names,
          const gchar         **attribute_values,
          gpointer              user_data,
          GError              **error)
{
	XmlBuild *build = user_data;
	XmlNode *node;

	node = g_new0 (XmlNode, 1);
	node->name = g_strdup (element_name);
	node->attr_names = g_strdupv ((gchar **) attribute_names);
	node->attr_values = g_strdupv ((gchar **) attribute_values);
	node->text = g_string_new (NULL);
	node->children = g_ptr_array_new_with_free_func (xml_node_free);

	if (build->stack->len == 0)
	{
		if (build->root != NULL)
		{
			xml_node_free (node);
			g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
			             "more than one root element");
			return;
		}
		build->root = node;
	}
	else
	{
		XmlNode *parent = g_ptr_array_index (build->stack, build->stack->len - 1);

		g_ptr_array_add (parent->children, node);
	}
	g_ptr_array_add (build->stack, node);
}

static void
on_end (GMarkupParseContext  *context,
        const gchar          *element_name,
        gpointer              user_data,
        GError              **error)
{
	XmlBuild *build = user_data;

	if (build->stack->len > 0)
		g_ptr_array_remove_index (build->stack, build->stack->len - 1);
}

static void
on_text (GMarkupParseContext  *context,
         const gchar          *text,
         gsize                 text_len,
         gpointer              user_data,
         GError              **error)
{
	XmlBuild *build = user_data;

	if (build->stack->len > 0)
	{
		XmlNode *node = g_ptr_array_index (build->stack, build->stack->len - 1);

		g_string_append_len (node->text, text, (gssize) text_len);
	}
}

static XmlNode *
xml_parse (const gchar  *data,
           gsize         length,
           GError      **error)
{
	static const GMarkupParser parser = { on_start, on_end, on_text, NULL, NULL };
	GMarkupParseContext *context;
	g_autoptr(GError) local = NULL;
	XmlBuild build = { NULL, NULL };
	gboolean ok;

	build.stack = g_ptr_array_new ();
	context = g_markup_parse_context_new (&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &build, NULL);
	ok = g_markup_parse_context_parse (context, data, (gssize) length, &local) &&
	     g_markup_parse_context_end_parse (context, &local);
	g_markup_parse_context_free (context);
	g_ptr_array_unref (build.stack);

	if (!ok)
	{
		xml_node_free (build.root);
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_PARSE,
		             "invalid XML: %s", local->message);
		return NULL;
	}
	if (build.root == NULL)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_PARSE, "empty XML document");
		return NULL;
	}
	return build.root;
}

static const gchar *
attr (XmlNode     *node,
      const gchar *name)
{
	guint i;

	for (i = 0; node->attr_names[i] != NULL; i++)
	{
		if (strcmp (node->attr_names[i], name) == 0)
			return node->attr_values[i];
	}
	return NULL;
}

static gdouble
attr_double (XmlNode     *node,
             const gchar *name,
             gdouble      fallback)
{
	const gchar *v = attr (node, name);
	gchar *end = NULL;
	gdouble d;

	if (v == NULL || *v == '\0')
		return fallback;
	d = g_ascii_strtod (v, &end);
	return (end == v) ? fallback : d;
}

static guint
attr_uint (XmlNode     *node,
           const gchar *name,
           guint        fallback)
{
	gdouble d = attr_double (node, name, -1.0);

	if (d < 0.0 || d > (gdouble) G_MAXUINT32)
		return fallback;
	return (guint) d;
}

static gboolean
attr_bool (XmlNode     *node,
           const gchar *name,
           gboolean     fallback)
{
	const gchar *v = attr (node, name);

	if (v == NULL || *v == '\0')
		return fallback;
	if (g_strcmp0 (v, "true") == 0)
		return TRUE;
	if (g_strcmp0 (v, "false") == 0)
		return FALSE;
	return attr_double (node, name, fallback ? 1.0 : 0.0) != 0.0;
}

static XmlNode *
child (XmlNode     *node,
       const gchar *name)
{
	guint i;

	for (i = 0; i < node->children->len; i++)
	{
		XmlNode *c = g_ptr_array_index (node->children, i);

		if (strcmp (c->name, name) == 0)
			return c;
	}
	return NULL;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

static void
read_property_list (LrgTiledProperties *props,
                    XmlNode            *list,
                    const gchar        *prefix,
                    const gchar        *base_dir)
{
	guint i;

	for (i = 0; i < list->children->len; i++)
	{
		XmlNode *p = g_ptr_array_index (list->children, i);
		const gchar *name;
		const gchar *type;
		const gchar *value;
		g_autofree gchar *key = NULL;
		LrgTiledPropertyType ptype;
		gchar *text;

		if (strcmp (p->name, "property") != 0 || (name = attr (p, "name")) == NULL)
			continue;
		key = (prefix != NULL) ? g_strdup_printf ("%s.%s", prefix, name) : g_strdup (name);
		type = attr (p, "type");

		if (g_strcmp0 (type, "class") == 0)
		{
			XmlNode *members = child (p, "properties");

			if (members != NULL)
				read_property_list (props, members, key, base_dir);
			continue;
		}
		if (!lrg_tiled_property_type_parse (type, &ptype))
			ptype = LRG_TILED_PROPERTY_STRING;

		/* a multi-line string is the element's text, not an attribute */
		value = attr (p, "value");
		text = g_strdup (value != NULL ? value : p->text->str);
		if (ptype == LRG_TILED_PROPERTY_FILE && *text != '\0')
		{
			gchar *resolved = lrg_tiled_resolve_path (base_dir, text);

			g_free (text);
			text = resolved;
		}
		lrg_tiled_properties_take (props, key, ptype, text);
	}
}

static void
read_properties (LrgTiledProperties *props,
                 XmlNode            *owner,
                 const gchar        *base_dir)
{
	XmlNode *list = child (owner, "properties");

	if (list != NULL)
		read_property_list (props, list, NULL, base_dir);
}

/* ==========================================================================
 * Objects
 * ========================================================================== */

static gboolean
read_points (LrgTiledObject *object,
             const gchar    *text)
{
	g_auto(GStrv) pairs = NULL;
	guint i;

	object->points = g_array_new (FALSE, FALSE, sizeof (gfloat));
	if (text == NULL)
		return FALSE;
	pairs = g_strsplit_set (text, " \t\n", -1);
	for (i = 0; pairs[i] != NULL; i++)
	{
		gchar *comma;
		gfloat x;
		gfloat y;

		if (*pairs[i] == '\0')
			continue;
		comma = strchr (pairs[i], ',');
		if (comma == NULL)
			return FALSE;
		x = (gfloat) g_ascii_strtod (pairs[i], NULL);
		y = (gfloat) g_ascii_strtod (comma + 1, NULL);
		g_array_append_val (object->points, x);
		g_array_append_val (object->points, y);
	}
	return TRUE;
}

static LrgTiledObject *
read_object (XmlNode      *node,
             const gchar  *base_dir,
             GError      **error)
{
	g_autoptr(LrgTiledObject) object = NULL;
	const gchar *s;
	XmlNode *c;

	if (attr (node, "template") != NULL)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
		             "object %u uses a template ('%s'); detach it in Tiled",
		             attr_uint (node, "id", 0), attr (node, "template"));
		return NULL;
	}

	object = lrg_tiled_object_new_empty ();
	object->id = attr_uint (node, "id", 0);
	if ((s = attr (node, "name")) != NULL)
	{
		g_free (object->name);
		object->name = g_strdup (s);
	}
	s = attr (node, "class");
	if (s == NULL || *s == '\0')
		s = attr (node, "type");
	if (s != NULL)
	{
		g_free (object->type);
		object->type = g_strdup (s);
	}
	object->x = attr_double (node, "x", 0.0);
	object->y = attr_double (node, "y", 0.0);
	object->width = attr_double (node, "width", 0.0);
	object->height = attr_double (node, "height", 0.0);
	object->rotation = attr_double (node, "rotation", 0.0);
	object->visible = attr_bool (node, "visible", TRUE);
	object->gid = attr_uint (node, "gid", 0);

	if (object->gid != 0)
		object->shape = LRG_TILED_OBJECT_TILE;
	else if (child (node, "point") != NULL)
		object->shape = LRG_TILED_OBJECT_POINT;
	else if (child (node, "ellipse") != NULL)
		object->shape = LRG_TILED_OBJECT_ELLIPSE;
	else if ((c = child (node, "polygon")) != NULL)
	{
		object->shape = LRG_TILED_OBJECT_POLYGON;
		if (!read_points (object, attr (c, "points")))
			goto bad_points;
	}
	else if ((c = child (node, "polyline")) != NULL)
	{
		object->shape = LRG_TILED_OBJECT_POLYLINE;
		if (!read_points (object, attr (c, "points")))
			goto bad_points;
	}
	else if ((c = child (node, "text")) != NULL)
	{
		object->shape = LRG_TILED_OBJECT_TEXT;
		object->text = g_strdup (c->text->str);
	}

	read_properties (object->properties, node, base_dir);
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
                   XmlNode          *node,
                   const gchar      *base_dir,
                   GError          **error)
{
	XmlNode *c;
	const gchar *s;
	guint i;

	if ((s = attr (node, "name")) != NULL)
	{
		g_free (ts->name);
		ts->name = g_strdup (s);
	}
	ts->tile_width = attr_uint (node, "tilewidth", 0);
	ts->tile_height = attr_uint (node, "tileheight", 0);
	ts->tile_count = attr_uint (node, "tilecount", 0);
	ts->columns = attr_uint (node, "columns", 0);
	ts->margin = attr_uint (node, "margin", 0);
	ts->spacing = attr_uint (node, "spacing", 0);

	if ((c = child (node, "image")) != NULL)
	{
		g_free (ts->image_path);
		ts->image_path = lrg_tiled_resolve_path (base_dir, attr (c, "source"));
		ts->image_width = attr_uint (c, "width", 0);
		ts->image_height = attr_uint (c, "height", 0);
	}
	if ((c = child (node, "tileoffset")) != NULL)
	{
		ts->offset_x = (gint) attr_double (c, "x", 0.0);
		ts->offset_y = (gint) attr_double (c, "y", 0.0);
	}
	read_properties (ts->properties, node, base_dir);

	for (i = 0; i < node->children->len; i++)
	{
		XmlNode *t = g_ptr_array_index (node->children, i);
		LrgTiledTile *tile;
		XmlNode *anim;
		XmlNode *group;

		if (strcmp (t->name, "tile") != 0 || attr (t, "id") == NULL)
			continue;
		tile = lrg_tiled_tileset_ensure_tile (ts, attr_uint (t, "id", 0));

		s = attr (t, "class");
		if (s == NULL || *s == '\0')
			s = attr (t, "type");
		if (s != NULL && *s != '\0')
		{
			g_free (tile->type);
			tile->type = g_strdup (s);
		}
		read_properties (tile->properties, t, base_dir);

		if ((anim = child (t, "animation")) != NULL)
		{
			guint k;

			tile->anim_frames = g_array_new (FALSE, FALSE, sizeof (guint));
			tile->anim_ms = g_array_new (FALSE, FALSE, sizeof (guint));
			for (k = 0; k < anim->children->len; k++)
			{
				XmlNode *f = g_ptr_array_index (anim->children, k);
				guint frame;
				guint ms;

				if (strcmp (f->name, "frame") != 0)
					continue;
				frame = attr_uint (f, "tileid", 0);
				ms = MAX (attr_uint (f, "duration", 100), 1u);
				g_array_append_val (tile->anim_frames, frame);
				g_array_append_val (tile->anim_ms, ms);
			}
		}

		if ((group = child (t, "objectgroup")) != NULL)
		{
			guint k;

			tile->shapes = g_ptr_array_new_with_free_func (g_object_unref);
			for (k = 0; k < group->children->len; k++)
			{
				XmlNode *o = g_ptr_array_index (group->children, k);
				LrgTiledObject *shape;

				if (strcmp (o->name, "object") != 0)
					continue;
				shape = read_object (o, base_dir, error);
				if (shape == NULL)
					return FALSE;
				g_ptr_array_add (tile->shapes, shape);
			}
		}
	}
	return TRUE;
}

gboolean
lrg_tiled_parse_tsx (LrgTiledTileset  *tileset,
                     const gchar      *data,
                     gsize             length,
                     const gchar      *base_dir,
                     GError          **error)
{
	g_autoptr(XmlNode) root = xml_parse (data, length, error);

	if (root == NULL)
		return FALSE;
	if (strcmp (root->name, "tileset") != 0)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "the XML document is not a Tiled tileset (root <%s>)", root->name);
		return FALSE;
	}
	return read_tileset_body (tileset, root, base_dir, error);
}

/* ==========================================================================
 * Layers
 * ========================================================================== */

static gboolean read_layers (LrgTiledMap               *map,
                             XmlNode                   *parent,
                             const LrgTiledGroupState  *group,
                             GError                   **error);

static void
read_layer_common (LrgTiledLayer *layer,
                   XmlNode       *node,
                   const gchar   *base_dir)
{
	layer->id = attr_uint (node, "id", 0);
	layer->visible = attr_bool (node, "visible", TRUE);
	layer->opacity = attr_double (node, "opacity", 1.0);
	layer->offset_x = attr_double (node, "offsetx", 0.0);
	layer->offset_y = attr_double (node, "offsety", 0.0);
	layer->parallax_x = attr_double (node, "parallaxx", 1.0);
	layer->parallax_y = attr_double (node, "parallaxy", 1.0);
	read_properties (layer->properties, node, base_dir);
}

static gboolean
read_tile_data (LrgTiledLayer  *layer,
                XmlNode        *node,
                GError        **error)
{
	XmlNode *data = child (node, "data");
	const gchar *encoding;
	guint n = layer->width * layer->height;

	if (n == 0 || data == NULL)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "tile layer '%s' has no size or no data", layer->name);
		return FALSE;
	}
	if (child (data, "chunk") != NULL)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
		             "layer '%s' is chunked (infinite map); turn Infinite off in Map "
		             "Properties", layer->name);
		return FALSE;
	}
	layer->gids = g_new0 (guint, n);
	encoding = attr (data, "encoding");

	if (encoding == NULL || *encoding == '\0')
	{
		/* the oldest form: one <tile gid=".."/> element per cell */
		guint count = 0;
		guint i;

		for (i = 0; i < data->children->len; i++)
		{
			XmlNode *t = g_ptr_array_index (data->children, i);

			if (strcmp (t->name, "tile") != 0)
				continue;
			if (count >= n)
				break;
			layer->gids[count++] = attr_uint (t, "gid", 0);
		}
		if (count != n)
		{
			g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
			             "tile layer '%s' has %u <tile> cells, expected %u",
			             layer->name, count, n);
			return FALSE;
		}
		return TRUE;
	}
	return lrg_tiled_decode_data (data->text->str, encoding, attr (data, "compression"),
	                              layer->gids, n, error);
}

static gboolean
read_layer (LrgTiledMap               *map,
            XmlNode                   *node,
            const LrgTiledGroupState  *group,
            GError                   **error)
{
	g_autoptr(LrgTiledLayer) layer = NULL;
	const gchar *name = attr (node, "name");

	if (strcmp (node->name, "group") == 0)
	{
		LrgTiledGroupState inner;
		g_autofree gchar *prefix = NULL;

		prefix = (group->prefix != NULL && *group->prefix != '\0')
		         ? g_strdup_printf ("%s/%s", group->prefix, name != NULL ? name : "")
		         : g_strdup (name != NULL ? name : "");
		inner.prefix = prefix;
		inner.visible = group->visible && attr_bool (node, "visible", TRUE);
		inner.opacity = group->opacity * attr_double (node, "opacity", 1.0);
		inner.offset_x = group->offset_x + attr_double (node, "offsetx", 0.0);
		inner.offset_y = group->offset_y + attr_double (node, "offsety", 0.0);
		inner.parallax_x = group->parallax_x * attr_double (node, "parallaxx", 1.0);
		inner.parallax_y = group->parallax_y * attr_double (node, "parallaxy", 1.0);
		return read_layers (map, node, &inner, error);
	}

	if (strcmp (node->name, "layer") == 0)
	{
		layer = lrg_tiled_layer_new_empty (LRG_TILED_LAYER_TILES);
		read_layer_common (layer, node, map->base_dir);
		lrg_tiled_layer_apply_group (layer, name, group);
		layer->width = attr_uint (node, "width", 0);
		layer->height = attr_uint (node, "height", 0);
		if (!read_tile_data (layer, node, error))
			return FALSE;
	}
	else if (strcmp (node->name, "objectgroup") == 0)
	{
		guint i;

		layer = lrg_tiled_layer_new_empty (LRG_TILED_LAYER_OBJECTS);
		read_layer_common (layer, node, map->base_dir);
		for (i = 0; i < node->children->len; i++)
		{
			XmlNode *o = g_ptr_array_index (node->children, i);
			LrgTiledObject *object;

			if (strcmp (o->name, "object") != 0)
				continue;
			object = read_object (o, map->base_dir, error);
			if (object == NULL)
				return FALSE;
			g_ptr_array_add (layer->objects, object);
		}
		lrg_tiled_layer_apply_group (layer, name, group);
	}
	else if (strcmp (node->name, "imagelayer") == 0)
	{
		XmlNode *image = child (node, "image");

		layer = lrg_tiled_layer_new_empty (LRG_TILED_LAYER_IMAGE);
		read_layer_common (layer, node, map->base_dir);
		lrg_tiled_layer_apply_group (layer, name, group);
		if (image != NULL)
			layer->image_path = lrg_tiled_resolve_path (map->base_dir, attr (image, "source"));
	}
	else
		return TRUE;    /* properties, tilesets, editor settings: not layers */

	g_ptr_array_add (map->layers, g_steal_pointer (&layer));
	return TRUE;
}

static gboolean
read_layers (LrgTiledMap               *map,
             XmlNode                   *parent,
             const LrgTiledGroupState  *group,
             GError                   **error)
{
	guint i;

	for (i = 0; i < parent->children->len; i++)
	{
		if (!read_layer (map, g_ptr_array_index (parent->children, i), group, error))
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Map
 * ========================================================================== */

LrgTiledMap *
lrg_tiled_parse_tmx (const gchar  *data,
                     gsize         length,
                     const gchar  *base_dir,
                     GError      **error)
{
	g_autoptr(XmlNode) root = NULL;
	g_autoptr(LrgTiledMap) map = NULL;
	LrgTiledGroupState top = { NULL, TRUE, 1.0, 0.0, 0.0, 1.0, 1.0 };
	const gchar *s;
	guint i;

	root = xml_parse (data, length, error);
	if (root == NULL)
		return NULL;
	if (strcmp (root->name, "map") != 0)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "the XML document is not a Tiled map (root <%s>)", root->name);
		return NULL;
	}
	if (attr_bool (root, "infinite", FALSE))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
		             "infinite maps are not supported; turn Infinite off in Map Properties");
		return NULL;
	}

	map = lrg_tiled_map_new_empty (base_dir);
	map->width = attr_uint (root, "width", 0);
	map->height = attr_uint (root, "height", 0);
	map->tile_width = attr_uint (root, "tilewidth", 0);
	map->tile_height = attr_uint (root, "tileheight", 0);
	map->hex_side = attr_uint (root, "hexsidelength", 0);

	s = attr (root, "orientation");
	if (g_strcmp0 (s, "isometric") == 0)
		map->orientation = LRG_TILEMAP_ORIENTATION_ISOMETRIC;
	else if (g_strcmp0 (s, "staggered") == 0)
		map->orientation = LRG_TILEMAP_ORIENTATION_STAGGERED;
	else if (g_strcmp0 (s, "hexagonal") == 0)
		map->orientation = LRG_TILEMAP_ORIENTATION_HEXAGONAL;
	if (g_strcmp0 (attr (root, "staggeraxis"), "x") == 0)
		map->stagger_axis = LRG_TILEMAP_STAGGER_AXIS_X;
	if (g_strcmp0 (attr (root, "staggerindex"), "even") == 0)
		map->stagger_index = LRG_TILEMAP_STAGGER_INDEX_EVEN;
	map->background = g_strdup (attr (root, "backgroundcolor"));
	read_properties (map->properties, root, base_dir);

	for (i = 0; i < root->children->len; i++)
	{
		XmlNode *t = g_ptr_array_index (root->children, i);
		g_autoptr(LrgTiledTileset) ts = NULL;
		const gchar *source;

		if (strcmp (t->name, "tileset") != 0)
			continue;
		ts = lrg_tiled_tileset_new_empty ();
		ts->first_gid = attr_uint (t, "firstgid", 0);
		source = attr (t, "source");
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

	if (!read_layers (map, root, &top, error))
		return NULL;
	if (!lrg_tiled_map_finish (map, error))
		return NULL;
	return g_steal_pointer (&map);
}
