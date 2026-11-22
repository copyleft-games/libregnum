/* lrg-tiled-map.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Tiled importer's model: properties, objects, layers, tilesets and
 * the map, the format sniffing, layer data decoding (CSV, base64, zlib,
 * gzip) shared by both readers, object geometry for zones, and the
 * conversion to LrgTileset / LrgTilemap.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tiled-map-private.h"
#include "lrg-tilemap-layer.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

G_DEFINE_QUARK (lrg-tiled-error-quark, lrg_tiled_error)

/* ==========================================================================
 * Enum types
 * ========================================================================== */

GType
lrg_tiled_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TILED_ERROR_IO, "LRG_TILED_ERROR_IO", "io" },
			{ LRG_TILED_ERROR_PARSE, "LRG_TILED_ERROR_PARSE", "parse" },
			{ LRG_TILED_ERROR_INVALID, "LRG_TILED_ERROR_INVALID", "invalid" },
			{ LRG_TILED_ERROR_UNSUPPORTED, "LRG_TILED_ERROR_UNSUPPORTED", "unsupported" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTiledError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_tiled_layer_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TILED_LAYER_TILES, "LRG_TILED_LAYER_TILES", "tiles" },
			{ LRG_TILED_LAYER_OBJECTS, "LRG_TILED_LAYER_OBJECTS", "objects" },
			{ LRG_TILED_LAYER_IMAGE, "LRG_TILED_LAYER_IMAGE", "image" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTiledLayerKind"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_tiled_object_shape_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TILED_OBJECT_RECTANGLE, "LRG_TILED_OBJECT_RECTANGLE", "rectangle" },
			{ LRG_TILED_OBJECT_POINT, "LRG_TILED_OBJECT_POINT", "point" },
			{ LRG_TILED_OBJECT_ELLIPSE, "LRG_TILED_OBJECT_ELLIPSE", "ellipse" },
			{ LRG_TILED_OBJECT_POLYGON, "LRG_TILED_OBJECT_POLYGON", "polygon" },
			{ LRG_TILED_OBJECT_POLYLINE, "LRG_TILED_OBJECT_POLYLINE", "polyline" },
			{ LRG_TILED_OBJECT_TILE, "LRG_TILED_OBJECT_TILE", "tile" },
			{ LRG_TILED_OBJECT_TEXT, "LRG_TILED_OBJECT_TEXT", "text" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTiledObjectShape"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_tiled_property_type_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TILED_PROPERTY_STRING, "LRG_TILED_PROPERTY_STRING", "string" },
			{ LRG_TILED_PROPERTY_INT, "LRG_TILED_PROPERTY_INT", "int" },
			{ LRG_TILED_PROPERTY_FLOAT, "LRG_TILED_PROPERTY_FLOAT", "float" },
			{ LRG_TILED_PROPERTY_BOOL, "LRG_TILED_PROPERTY_BOOL", "bool" },
			{ LRG_TILED_PROPERTY_COLOR, "LRG_TILED_PROPERTY_COLOR", "color" },
			{ LRG_TILED_PROPERTY_FILE, "LRG_TILED_PROPERTY_FILE", "file" },
			{ LRG_TILED_PROPERTY_OBJECT, "LRG_TILED_PROPERTY_OBJECT", "object" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTiledPropertyType"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgTiledProperties
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgTiledProperties, lrg_tiled_properties, G_TYPE_OBJECT)

static void
tiled_value_free (gpointer data)
{
	LrgTiledValue *value = data;

	g_free (value->string);
	g_free (value);
}

static void
lrg_tiled_properties_finalize (GObject *object)
{
	LrgTiledProperties *self = LRG_TILED_PROPERTIES (object);

	g_clear_pointer (&self->values, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_tiled_properties_parent_class)->finalize (object);
}

static void
lrg_tiled_properties_class_init (LrgTiledPropertiesClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_tiled_properties_finalize;
}

static void
lrg_tiled_properties_init (LrgTiledProperties *self)
{
	self->values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, tiled_value_free);
}

LrgTiledProperties *
lrg_tiled_properties_new_empty (void)
{
	return g_object_new (LRG_TYPE_TILED_PROPERTIES, NULL);
}

/* Stores (or replaces) a property; takes @string. */
void
lrg_tiled_properties_take (LrgTiledProperties   *self,
                           const gchar          *name,
                           LrgTiledPropertyType  type,
                           gchar                *string)
{
	LrgTiledValue *value;

	value = g_new0 (LrgTiledValue, 1);
	value->type = type;
	value->string = (string != NULL) ? string : g_strdup ("");
	g_hash_table_replace (self->values, g_strdup (name), value);
}

/* Maps Tiled's type names; "class" is not a value type (flattened). */
gboolean
lrg_tiled_property_type_parse (const gchar          *name,
                               LrgTiledPropertyType *out)
{
	static const struct { const gchar *name; LrgTiledPropertyType type; } table[] = {
		{ "string", LRG_TILED_PROPERTY_STRING },
		{ "int", LRG_TILED_PROPERTY_INT },
		{ "float", LRG_TILED_PROPERTY_FLOAT },
		{ "bool", LRG_TILED_PROPERTY_BOOL },
		{ "color", LRG_TILED_PROPERTY_COLOR },
		{ "file", LRG_TILED_PROPERTY_FILE },
		{ "object", LRG_TILED_PROPERTY_OBJECT }
	};
	guint i;

	if (name == NULL || *name == '\0')
	{
		*out = LRG_TILED_PROPERTY_STRING;
		return TRUE;
	}
	for (i = 0; i < G_N_ELEMENTS (table); i++)
	{
		if (g_strcmp0 (name, table[i].name) == 0)
		{
			*out = table[i].type;
			return TRUE;
		}
	}
	return FALSE;
}

guint
lrg_tiled_properties_get_n_properties (LrgTiledProperties *self)
{
	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), 0);
	return g_hash_table_size (self->values);
}

static gint
compare_key_strings (const void *a,
                     const void *b)
{
	return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

gchar **
lrg_tiled_properties_get_keys (LrgTiledProperties *self)
{
	g_autofree gpointer *keys = NULL;
	gchar **result;
	guint n = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), NULL);

	keys = g_hash_table_get_keys_as_array (self->values, &n);
	result = g_new0 (gchar *, n + 1);
	for (i = 0; i < n; i++)
		result[i] = g_strdup (keys[i]);
	/* hash order is not stable; callers iterate this, so sort it */
	qsort (result, n, sizeof (gchar *), compare_key_strings);
	return result;
}

gboolean
lrg_tiled_properties_has (LrgTiledProperties *self,
                          const gchar        *name)
{
	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	return g_hash_table_contains (self->values, name);
}

LrgTiledPropertyType
lrg_tiled_properties_get_value_type (LrgTiledProperties *self,
                                     const gchar        *name)
{
	LrgTiledValue *value;

	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), LRG_TILED_PROPERTY_STRING);
	g_return_val_if_fail (name != NULL, LRG_TILED_PROPERTY_STRING);

	value = g_hash_table_lookup (self->values, name);
	return (value != NULL) ? value->type : LRG_TILED_PROPERTY_STRING;
}

const gchar *
lrg_tiled_properties_get_string (LrgTiledProperties *self,
                                 const gchar        *name)
{
	LrgTiledValue *value;

	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	value = g_hash_table_lookup (self->values, name);
	return (value != NULL) ? value->string : NULL;
}

/* Parses a whole string as a double; FALSE on trailing junk or nothing. */
static gboolean
parse_double (const gchar *text,
              gdouble     *out)
{
	gchar *end = NULL;
	gdouble v;

	if (text == NULL || *text == '\0')
		return FALSE;
	v = g_ascii_strtod (text, &end);
	if (end == text)
		return FALSE;
	while (g_ascii_isspace (*end))
		end++;
	if (*end != '\0')
		return FALSE;
	*out = v;
	return TRUE;
}

static gboolean
parse_boolean (const gchar *text,
               gboolean    *out)
{
	gdouble v;

	if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0)
	{
		*out = TRUE;
		return TRUE;
	}
	if (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0)
	{
		*out = FALSE;
		return TRUE;
	}
	if (parse_double (text, &v))
	{
		*out = (v != 0.0);
		return TRUE;
	}
	return FALSE;
}

gint64
lrg_tiled_properties_get_int (LrgTiledProperties *self,
                              const gchar        *name,
                              gint64              fallback)
{
	const gchar *text;
	gboolean b;
	gdouble v;

	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), fallback);

	text = lrg_tiled_properties_get_string (self, name);
	if (text == NULL)
		return fallback;
	if (parse_double (text, &v))
		return (gint64) v;
	if (parse_boolean (text, &b))
		return b ? 1 : 0;
	return fallback;
}

gdouble
lrg_tiled_properties_get_double (LrgTiledProperties *self,
                                 const gchar        *name,
                                 gdouble             fallback)
{
	const gchar *text;
	gboolean b;
	gdouble v;

	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), fallback);

	text = lrg_tiled_properties_get_string (self, name);
	if (text == NULL)
		return fallback;
	if (parse_double (text, &v))
		return v;
	if (parse_boolean (text, &b))
		return b ? 1.0 : 0.0;
	return fallback;
}

gboolean
lrg_tiled_properties_get_boolean (LrgTiledProperties *self,
                                  const gchar        *name,
                                  gboolean            fallback)
{
	const gchar *text;
	gboolean b;

	g_return_val_if_fail (LRG_IS_TILED_PROPERTIES (self), fallback);

	text = lrg_tiled_properties_get_string (self, name);
	if (text == NULL)
		return fallback;
	if (parse_boolean (text, &b))
		return b;
	return fallback;
}

/* ==========================================================================
 * LrgTiledObject
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgTiledObject, lrg_tiled_object, G_TYPE_OBJECT)

static void
lrg_tiled_object_finalize (GObject *object)
{
	LrgTiledObject *self = LRG_TILED_OBJECT (object);

	g_free (self->name);
	g_free (self->type);
	g_free (self->text);
	g_clear_pointer (&self->points, g_array_unref);
	g_clear_object (&self->properties);
	G_OBJECT_CLASS (lrg_tiled_object_parent_class)->finalize (object);
}

static void
lrg_tiled_object_class_init (LrgTiledObjectClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_tiled_object_finalize;
}

static void
lrg_tiled_object_init (LrgTiledObject *self)
{
	self->name = g_strdup ("");
	self->type = g_strdup ("");
	self->visible = TRUE;
	self->shape = LRG_TILED_OBJECT_RECTANGLE;
	self->properties = lrg_tiled_properties_new_empty ();
}

LrgTiledObject *
lrg_tiled_object_new_empty (void)
{
	return g_object_new (LRG_TYPE_TILED_OBJECT, NULL);
}

guint
lrg_tiled_object_get_id (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), 0);
	return self->id;
}

const gchar *
lrg_tiled_object_get_name (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), NULL);
	return self->name;
}

const gchar *
lrg_tiled_object_get_object_type (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), NULL);
	return self->type;
}

LrgTiledObjectShape
lrg_tiled_object_get_shape (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), LRG_TILED_OBJECT_RECTANGLE);
	return self->shape;
}

void
lrg_tiled_object_get_bounds (LrgTiledObject *self,
                             gdouble        *x,
                             gdouble        *y,
                             gdouble        *width,
                             gdouble        *height)
{
	g_return_if_fail (LRG_IS_TILED_OBJECT (self));

	if (x != NULL)
		*x = self->x;
	if (y != NULL)
		*y = self->y;
	if (width != NULL)
		*width = self->width;
	if (height != NULL)
		*height = self->height;
}

gdouble
lrg_tiled_object_get_rotation (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), 0.0);
	return self->rotation;
}

gboolean
lrg_tiled_object_get_visible (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), FALSE);
	return self->visible;
}

guint
lrg_tiled_object_get_gid (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), 0);
	return self->gid;
}

const gfloat *
lrg_tiled_object_get_points (LrgTiledObject *self,
                             guint          *n_coords)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), NULL);
	g_return_val_if_fail (n_coords != NULL, NULL);

	if (self->points == NULL || self->points->len == 0)
	{
		*n_coords = 0;
		return NULL;
	}
	*n_coords = self->points->len;
	return (const gfloat *) self->points->data;
}

const gchar *
lrg_tiled_object_get_text (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), NULL);
	return self->text;
}

LrgTiledProperties *
lrg_tiled_object_get_properties (LrgTiledObject *self)
{
	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), NULL);
	return self->properties;
}

/*
 * Tiled rotates an object clockwise (screen y grows down) around its
 * own position.  Maps a point given relative to that position into map
 * space.
 */
static void
object_local_to_map (LrgTiledObject *self,
                     gdouble         lx,
                     gdouble         ly,
                     gdouble        *mx,
                     gdouble        *my)
{
	gdouble rad = self->rotation * G_PI / 180.0;
	gdouble c = cos (rad);
	gdouble s = sin (rad);

	*mx = self->x + lx * c - ly * s;
	*my = self->y + lx * s + ly * c;
}

static void
object_map_to_local (LrgTiledObject *self,
                     gdouble         mx,
                     gdouble         my,
                     gdouble        *lx,
                     gdouble        *ly)
{
	gdouble rad = self->rotation * G_PI / 180.0;
	gdouble c = cos (rad);
	gdouble s = sin (rad);
	gdouble dx = mx - self->x;
	gdouble dy = my - self->y;

	*lx = dx * c + dy * s;
	*ly = -dx * s + dy * c;
}

void
lrg_tiled_object_get_center (LrgTiledObject *self,
                             gdouble        *x,
                             gdouble        *y)
{
	g_return_if_fail (LRG_IS_TILED_OBJECT (self));
	g_return_if_fail (x != NULL && y != NULL);

	switch (self->shape)
	{
	case LRG_TILED_OBJECT_POINT:
		*x = self->x;
		*y = self->y;
		break;
	case LRG_TILED_OBJECT_POLYGON:
	case LRG_TILED_OBJECT_POLYLINE:
	{
		gdouble sx = 0.0;
		gdouble sy = 0.0;
		guint n;
		guint i;

		n = (self->points != NULL) ? self->points->len / 2 : 0;
		if (n == 0)
		{
			*x = self->x;
			*y = self->y;
			break;
		}
		for (i = 0; i < n; i++)
		{
			sx += g_array_index (self->points, gfloat, i * 2);
			sy += g_array_index (self->points, gfloat, i * 2 + 1);
		}
		object_local_to_map (self, sx / n, sy / n, x, y);
		break;
	}
	case LRG_TILED_OBJECT_TILE:
		/* a tile object's position is its bottom-left corner */
		object_local_to_map (self, self->width / 2.0, -self->height / 2.0, x, y);
		break;
	case LRG_TILED_OBJECT_RECTANGLE:
	case LRG_TILED_OBJECT_ELLIPSE:
	case LRG_TILED_OBJECT_TEXT:
	default:
		object_local_to_map (self, self->width / 2.0, self->height / 2.0, x, y);
		break;
	}
}

/* Even-odd point in polygon, edges inside (distance tolerance 1e-9). */
static gboolean
point_in_polygon (const gfloat *pts,
                  guint         n,
                  gdouble       px,
                  gdouble       py)
{
	gboolean inside = FALSE;
	guint i;
	guint j;

	for (i = 0, j = n - 1; i < n; j = i++)
	{
		gdouble xi = pts[i * 2];
		gdouble yi = pts[i * 2 + 1];
		gdouble xj = pts[j * 2];
		gdouble yj = pts[j * 2 + 1];
		gdouble cross;

		/* on the edge counts as inside */
		cross = (px - xi) * (yj - yi) - (py - yi) * (xj - xi);
		if (fabs (cross) < 1e-9 &&
		    px >= MIN (xi, xj) - 1e-9 && px <= MAX (xi, xj) + 1e-9 &&
		    py >= MIN (yi, yj) - 1e-9 && py <= MAX (yi, yj) + 1e-9)
			return TRUE;

		if (((yi > py) != (yj > py)) &&
		    (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
			inside = !inside;
	}
	return inside;
}

gboolean
lrg_tiled_object_contains_point (LrgTiledObject *self,
                                 gdouble         x,
                                 gdouble         y)
{
	gdouble lx;
	gdouble ly;

	g_return_val_if_fail (LRG_IS_TILED_OBJECT (self), FALSE);

	object_map_to_local (self, x, y, &lx, &ly);

	switch (self->shape)
	{
	case LRG_TILED_OBJECT_RECTANGLE:
	case LRG_TILED_OBJECT_TEXT:
		return lx >= 0.0 && ly >= 0.0 && lx <= self->width && ly <= self->height;
	case LRG_TILED_OBJECT_TILE:
		return lx >= 0.0 && ly <= 0.0 && lx <= self->width && ly >= -self->height;
	case LRG_TILED_OBJECT_ELLIPSE:
	{
		gdouble rx = self->width / 2.0;
		gdouble ry = self->height / 2.0;
		gdouble dx;
		gdouble dy;

		if (rx <= 0.0 || ry <= 0.0)
			return FALSE;
		dx = (lx - rx) / rx;
		dy = (ly - ry) / ry;
		return dx * dx + dy * dy <= 1.0 + 1e-9;
	}
	case LRG_TILED_OBJECT_POLYGON:
		if (self->points == NULL || self->points->len < 6)
			return FALSE;
		return point_in_polygon ((const gfloat *) self->points->data,
		                         self->points->len / 2, lx, ly);
	case LRG_TILED_OBJECT_POINT:
	case LRG_TILED_OBJECT_POLYLINE:
	default:
		return FALSE;
	}
}

static gboolean
object_is_area (LrgTiledObject *object)
{
	switch (object->shape)
	{
	case LRG_TILED_OBJECT_RECTANGLE:
	case LRG_TILED_OBJECT_ELLIPSE:
	case LRG_TILED_OBJECT_POLYGON:
	case LRG_TILED_OBJECT_TILE:
		return TRUE;
	case LRG_TILED_OBJECT_POINT:
	case LRG_TILED_OBJECT_POLYLINE:
	case LRG_TILED_OBJECT_TEXT:
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * LrgTiledLayer
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgTiledLayer, lrg_tiled_layer, G_TYPE_OBJECT)

static void
lrg_tiled_layer_finalize (GObject *object)
{
	LrgTiledLayer *self = LRG_TILED_LAYER (object);

	g_free (self->name);
	g_free (self->gids);
	g_free (self->image_path);
	g_clear_pointer (&self->objects, g_ptr_array_unref);
	g_clear_object (&self->properties);
	G_OBJECT_CLASS (lrg_tiled_layer_parent_class)->finalize (object);
}

static void
lrg_tiled_layer_class_init (LrgTiledLayerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_tiled_layer_finalize;
}

static void
lrg_tiled_layer_init (LrgTiledLayer *self)
{
	self->name = g_strdup ("");
	self->visible = TRUE;
	self->opacity = 1.0;
	self->parallax_x = 1.0;
	self->parallax_y = 1.0;
	self->properties = lrg_tiled_properties_new_empty ();
}

LrgTiledLayer *
lrg_tiled_layer_new_empty (LrgTiledLayerKind kind)
{
	LrgTiledLayer *self = g_object_new (LRG_TYPE_TILED_LAYER, NULL);

	self->kind = kind;
	if (kind == LRG_TILED_LAYER_OBJECTS)
		self->objects = g_ptr_array_new_with_free_func (g_object_unref);
	return self;
}

/*
 * Folds the enclosing groups into a layer: name path, visibility,
 * opacity, offset and parallax.  Object positions are moved by the
 * total offset so callers never add it themselves.
 */
void
lrg_tiled_layer_apply_group (LrgTiledLayer            *layer,
                             const gchar              *own_name,
                             const LrgTiledGroupState *group)
{
	guint i;

	g_free (layer->name);
	if (group->prefix != NULL && *group->prefix != '\0')
		layer->name = g_strdup_printf ("%s/%s", group->prefix, own_name != NULL ? own_name : "");
	else
		layer->name = g_strdup (own_name != NULL ? own_name : "");

	layer->visible = layer->visible && group->visible;
	layer->opacity *= group->opacity;
	layer->offset_x += group->offset_x;
	layer->offset_y += group->offset_y;
	layer->parallax_x *= group->parallax_x;
	layer->parallax_y *= group->parallax_y;

	if (layer->objects != NULL)
	{
		for (i = 0; i < layer->objects->len; i++)
		{
			LrgTiledObject *object = g_ptr_array_index (layer->objects, i);

			object->x += layer->offset_x;
			object->y += layer->offset_y;
		}
	}
}

LrgTiledLayerKind
lrg_tiled_layer_get_kind (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), LRG_TILED_LAYER_TILES);
	return self->kind;
}

guint
lrg_tiled_layer_get_id (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), 0);
	return self->id;
}

const gchar *
lrg_tiled_layer_get_name (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), NULL);
	return self->name;
}

gboolean
lrg_tiled_layer_get_visible (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), FALSE);
	return self->visible;
}

gdouble
lrg_tiled_layer_get_opacity (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), 0.0);
	return self->opacity;
}

void
lrg_tiled_layer_get_offset (LrgTiledLayer *self,
                            gdouble       *x,
                            gdouble       *y)
{
	g_return_if_fail (LRG_IS_TILED_LAYER (self));
	if (x != NULL)
		*x = self->offset_x;
	if (y != NULL)
		*y = self->offset_y;
}

void
lrg_tiled_layer_get_parallax (LrgTiledLayer *self,
                              gdouble       *x,
                              gdouble       *y)
{
	g_return_if_fail (LRG_IS_TILED_LAYER (self));
	if (x != NULL)
		*x = self->parallax_x;
	if (y != NULL)
		*y = self->parallax_y;
}

LrgTiledProperties *
lrg_tiled_layer_get_properties (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), NULL);
	return self->properties;
}

guint
lrg_tiled_layer_get_width (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), 0);
	return self->width;
}

guint
lrg_tiled_layer_get_height (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), 0);
	return self->height;
}

guint
lrg_tiled_layer_get_gid (LrgTiledLayer *self,
                         guint          x,
                         guint          y)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), 0);

	if (self->gids == NULL || x >= self->width || y >= self->height)
		return 0;
	return self->gids[(gsize) y * self->width + x];
}

const guint *
lrg_tiled_layer_get_gids (LrgTiledLayer *self,
                          guint         *n_gids)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), NULL);
	g_return_val_if_fail (n_gids != NULL, NULL);

	if (self->gids == NULL)
	{
		*n_gids = 0;
		return NULL;
	}
	*n_gids = self->width * self->height;
	return self->gids;
}

guint
lrg_tiled_layer_get_n_objects (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), 0);
	return (self->objects != NULL) ? self->objects->len : 0;
}

LrgTiledObject *
lrg_tiled_layer_get_object (LrgTiledLayer *self,
                            guint          index)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), NULL);

	if (self->objects == NULL || index >= self->objects->len)
		return NULL;
	return g_ptr_array_index (self->objects, index);
}

const gchar *
lrg_tiled_layer_get_image_path (LrgTiledLayer *self)
{
	g_return_val_if_fail (LRG_IS_TILED_LAYER (self), NULL);
	return self->image_path;
}

/* ==========================================================================
 * LrgTiledTileset
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgTiledTileset, lrg_tiled_tileset, G_TYPE_OBJECT)

static void
tiled_tile_free (gpointer data)
{
	LrgTiledTile *tile = data;

	g_free (tile->type);
	g_clear_object (&tile->properties);
	g_clear_pointer (&tile->anim_frames, g_array_unref);
	g_clear_pointer (&tile->anim_ms, g_array_unref);
	g_clear_pointer (&tile->shapes, g_ptr_array_unref);
	g_free (tile);
}

static void
lrg_tiled_tileset_finalize (GObject *object)
{
	LrgTiledTileset *self = LRG_TILED_TILESET (object);

	g_free (self->name);
	g_free (self->image_path);
	g_clear_object (&self->properties);
	g_clear_pointer (&self->tiles, g_hash_table_unref);
	g_clear_object (&self->built);
	G_OBJECT_CLASS (lrg_tiled_tileset_parent_class)->finalize (object);
}

static void
lrg_tiled_tileset_class_init (LrgTiledTilesetClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_tiled_tileset_finalize;
}

static void
lrg_tiled_tileset_init (LrgTiledTileset *self)
{
	self->name = g_strdup ("");
	self->properties = lrg_tiled_properties_new_empty ();
	self->tiles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, tiled_tile_free);
}

LrgTiledTileset *
lrg_tiled_tileset_new_empty (void)
{
	return g_object_new (LRG_TYPE_TILED_TILESET, NULL);
}

LrgTiledTile *
lrg_tiled_tileset_ensure_tile (LrgTiledTileset *self,
                               guint            local_id)
{
	LrgTiledTile *tile;

	tile = g_hash_table_lookup (self->tiles, GUINT_TO_POINTER (local_id));
	if (tile == NULL)
	{
		tile = g_new0 (LrgTiledTile, 1);
		tile->properties = lrg_tiled_properties_new_empty ();
		g_hash_table_insert (self->tiles, GUINT_TO_POINTER (local_id), tile);
	}
	return tile;
}

const gchar *
lrg_tiled_tileset_get_name (LrgTiledTileset *self)
{
	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), NULL);
	return self->name;
}

guint
lrg_tiled_tileset_get_first_gid (LrgTiledTileset *self)
{
	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), 0);
	return self->first_gid;
}

guint
lrg_tiled_tileset_get_tile_count (LrgTiledTileset *self)
{
	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), 0);
	return self->tile_count;
}

guint
lrg_tiled_tileset_get_columns (LrgTiledTileset *self)
{
	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), 0);
	return self->columns;
}

void
lrg_tiled_tileset_get_tile_size (LrgTiledTileset *self,
                                 guint           *width,
                                 guint           *height)
{
	g_return_if_fail (LRG_IS_TILED_TILESET (self));
	if (width != NULL)
		*width = self->tile_width;
	if (height != NULL)
		*height = self->tile_height;
}

const gchar *
lrg_tiled_tileset_get_image_path (LrgTiledTileset *self)
{
	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), NULL);
	return self->image_path;
}

LrgTiledProperties *
lrg_tiled_tileset_get_properties (LrgTiledTileset *self)
{
	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), NULL);
	return self->properties;
}

LrgTiledProperties *
lrg_tiled_tileset_get_tile_properties (LrgTiledTileset *self,
                                       guint            local_id)
{
	LrgTiledTile *tile;

	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), NULL);

	tile = g_hash_table_lookup (self->tiles, GUINT_TO_POINTER (local_id));
	if (tile == NULL || g_hash_table_size (tile->properties->values) == 0)
		return NULL;
	return tile->properties;
}

const gchar *
lrg_tiled_tileset_get_tile_type (LrgTiledTileset *self,
                                 guint            local_id)
{
	LrgTiledTile *tile;

	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), NULL);

	tile = g_hash_table_lookup (self->tiles, GUINT_TO_POINTER (local_id));
	return (tile != NULL) ? tile->type : NULL;
}

/* Collision shape of a tile -> the LrgTileset polygon list. */
static void
add_tile_shape (LrgTileset     *tileset,
                guint           local_id,
                LrgTiledObject *shape)
{
	g_autofree gfloat *coords = NULL;
	guint n = 0;
	guint i;

	if (shape->shape == LRG_TILED_OBJECT_RECTANGLE && shape->width > 0.0 && shape->height > 0.0)
	{
		gdouble lx[4] = { 0.0, shape->width, shape->width, 0.0 };
		gdouble ly[4] = { 0.0, 0.0, shape->height, shape->height };

		coords = g_new (gfloat, 8);
		for (i = 0; i < 4; i++)
		{
			gdouble mx;
			gdouble my;

			object_local_to_map (shape, lx[i], ly[i], &mx, &my);
			coords[i * 2] = (gfloat) mx;
			coords[i * 2 + 1] = (gfloat) my;
		}
		n = 8;
	}
	else if (shape->shape == LRG_TILED_OBJECT_POLYGON && shape->points != NULL &&
	         shape->points->len >= 6)
	{
		n = shape->points->len;
		coords = g_new (gfloat, n);
		for (i = 0; i < n / 2; i++)
		{
			gdouble mx;
			gdouble my;

			object_local_to_map (shape,
			                     g_array_index (shape->points, gfloat, i * 2),
			                     g_array_index (shape->points, gfloat, i * 2 + 1),
			                     &mx, &my);
			coords[i * 2] = (gfloat) mx;
			coords[i * 2 + 1] = (gfloat) my;
		}
	}
	if (n > 0)
		lrg_tileset_add_tile_collision_polygon (tileset, local_id, coords, n);
}

LrgTileset *
lrg_tiled_tileset_get_tileset (LrgTiledTileset *self)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	g_return_val_if_fail (LRG_IS_TILED_TILESET (self), NULL);

	if (self->built != NULL)
		return self->built;
	if (self->image_path == NULL || self->columns == 0 || self->tile_width == 0 ||
	    self->tile_height == 0)
		return NULL;

	self->built = lrg_tileset_new_empty (self->tile_width, self->tile_height,
	                                     self->columns, self->tile_count);
	lrg_tileset_set_name (self->built, self->name);
	lrg_tileset_set_image_path (self->built, self->image_path);
	lrg_tileset_set_margin_spacing (self->built, self->margin, self->spacing);
	lrg_tileset_set_draw_offset (self->built, self->offset_x, self->offset_y);

	g_hash_table_iter_init (&iter, self->tiles);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		guint local_id = GPOINTER_TO_UINT (key);
		LrgTiledTile *tile = value;
		GHashTableIter piter;
		gpointer pkey;
		gpointer pvalue;
		guint i;

		if (local_id >= self->tile_count)
			continue;
		if (tile->type != NULL && *tile->type != '\0')
			lrg_tileset_set_tile_data (self->built, local_id, "type", tile->type);

		g_hash_table_iter_init (&piter, tile->properties->values);
		while (g_hash_table_iter_next (&piter, &pkey, &pvalue))
			lrg_tileset_set_tile_data (self->built, local_id, pkey,
			                           ((LrgTiledValue *) pvalue)->string);

		if (tile->anim_frames != NULL && tile->anim_frames->len > 0)
			lrg_tileset_set_tile_animation (self->built, local_id,
			                                (const guint *) tile->anim_frames->data,
			                                (const guint *) tile->anim_ms->data,
			                                tile->anim_frames->len);

		if (tile->shapes != NULL)
		{
			for (i = 0; i < tile->shapes->len; i++)
				add_tile_shape (self->built, local_id, g_ptr_array_index (tile->shapes, i));
		}
	}
	return self->built;
}

/* ==========================================================================
 * Shared reader helpers
 * ========================================================================== */

gchar *
lrg_tiled_resolve_path (const gchar *base_dir,
                        const gchar *path)
{
	g_autofree gchar *joined = NULL;
	g_autoptr(GFile) file = NULL;

	if (path == NULL || *path == '\0')
		return NULL;
	if (g_path_is_absolute (path) || base_dir == NULL || *base_dir == '\0')
		joined = g_strdup (path);
	else
		joined = g_build_filename (base_dir, path, NULL);

	/* GFile canonicalises "..", so equal files compare equal */
	file = g_file_new_for_path (joined);
	return g_file_get_path (file);
}

gchar *
lrg_tiled_read_file (const gchar  *path,
                     gsize        *length,
                     GError      **error)
{
	g_autoptr(GError) local = NULL;
	gchar *data = NULL;

	if (!g_file_get_contents (path, &data, length, &local))
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_IO,
		             "cannot read '%s': %s", path, local->message);
		return NULL;
	}
	return data;
}

/* Inflates zlib or gzip bytes with GIO (no extra dependency). */
static GBytes *
inflate_bytes (GBytes                *input,
               GZlibCompressorFormat  format,
               GError               **error)
{
	g_autoptr(GZlibDecompressor) decompressor = NULL;
	g_autoptr(GInputStream) mem = NULL;
	g_autoptr(GInputStream) conv = NULL;
	g_autoptr(GOutputStream) out = NULL;
	g_autoptr(GError) local = NULL;

	decompressor = g_zlib_decompressor_new (format);
	mem = g_memory_input_stream_new_from_bytes (input);
	conv = g_converter_input_stream_new (mem, G_CONVERTER (decompressor));
	out = g_memory_output_stream_new_resizable ();
	if (g_output_stream_splice (out, conv,
	                            G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
	                            G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
	                            NULL, &local) < 0)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "cannot decompress layer data: %s", local->message);
		return NULL;
	}
	return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (out));
}

/*
 * Decodes one tile layer's data into @out (@n_cells gids).  @encoding is
 * NULL/"" for XML child elements (not handled here), "csv" or "base64";
 * @compression is NULL/"", "zlib", "gzip" or "zstd" (refused).  Base64
 * data is little-endian 32-bit gids, as Tiled writes it.
 */
gboolean
lrg_tiled_decode_data (const gchar  *text,
                       const gchar  *encoding,
                       const gchar  *compression,
                       guint        *out,
                       guint         n_cells,
                       GError      **error)
{
	if (g_strcmp0 (encoding, "csv") == 0)
	{
		const gchar *p = text;
		guint count = 0;

		while (*p != '\0')
		{
			gchar *end = NULL;
			guint64 v;

			while (*p == ',' || g_ascii_isspace (*p))
				p++;
			if (*p == '\0')
				break;
			v = g_ascii_strtoull (p, &end, 10);
			if (end == p || v > G_MAXUINT32)
			{
				g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
				             "bad CSV tile value near '%.16s'", p);
				return FALSE;
			}
			if (count >= n_cells)
			{
				g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
				             "layer data has more than %u cells", n_cells);
				return FALSE;
			}
			out[count++] = (guint) v;
			p = end;
		}
		if (count != n_cells)
		{
			g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
			             "layer data has %u cells, expected %u", count, n_cells);
			return FALSE;
		}
		return TRUE;
	}

	if (g_strcmp0 (encoding, "base64") == 0)
	{
		g_autofree gchar *trimmed = NULL;
		g_autofree guchar *raw = NULL;
		g_autoptr(GBytes) bytes = NULL;
		const guint8 *data;
		gsize raw_len = 0;
		gsize len;
		guint i;

		trimmed = g_strstrip (g_strdup (text));
		raw = g_base64_decode (trimmed, &raw_len);
		bytes = g_bytes_new_take (g_steal_pointer (&raw), raw_len);

		if (compression != NULL && *compression != '\0')
		{
			GBytes *inflated;

			if (g_strcmp0 (compression, "zlib") == 0)
				inflated = inflate_bytes (bytes, G_ZLIB_COMPRESSOR_FORMAT_ZLIB, error);
			else if (g_strcmp0 (compression, "gzip") == 0)
				inflated = inflate_bytes (bytes, G_ZLIB_COMPRESSOR_FORMAT_GZIP, error);
			else
			{
				g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
				             "layer compression '%s' is not supported (use zlib, gzip "
				             "or none)", compression);
				return FALSE;
			}
			if (inflated == NULL)
				return FALSE;
			g_bytes_unref (bytes);
			bytes = inflated;
		}

		data = g_bytes_get_data (bytes, &len);
		if (len != (gsize) n_cells * 4)
		{
			g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
			             "layer data is %" G_GSIZE_FORMAT " bytes, expected %u",
			             len, n_cells * 4);
			return FALSE;
		}
		for (i = 0; i < n_cells; i++)
		{
			out[i] = (guint) data[i * 4] |
			         ((guint) data[i * 4 + 1] << 8) |
			         ((guint) data[i * 4 + 2] << 16) |
			         ((guint) data[i * 4 + 3] << 24);
		}
		return TRUE;
	}

	g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
	             "unknown layer encoding '%s'", encoding != NULL ? encoding : "");
	return FALSE;
}

/* External tileset: sniff its format like a map, not by extension. */
gboolean
lrg_tiled_load_external_tileset (LrgTiledTileset  *tileset,
                                 const gchar      *path,
                                 GError          **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *dir = NULL;
	const gchar *p;
	gsize length = 0;

	data = lrg_tiled_read_file (path, &length, error);
	if (data == NULL)
		return FALSE;
	dir = g_path_get_dirname (path);

	p = data;
	while (p < data + length && g_ascii_isspace (*p))
		p++;
	if (p < data + length && *p == '{')
		return lrg_tiled_parse_tsj (tileset, data, length, dir, error);
	if (p < data + length && *p == '<')
		return lrg_tiled_parse_tsx (tileset, data, length, dir, error);

	g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_PARSE,
	             "'%s' is neither a JSON nor an XML tileset", path);
	return FALSE;
}

/* ==========================================================================
 * LrgTiledMap
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgTiledMap, lrg_tiled_map, G_TYPE_OBJECT)

static void
lrg_tiled_map_finalize (GObject *object)
{
	LrgTiledMap *self = LRG_TILED_MAP (object);

	g_free (self->base_dir);
	g_free (self->background);
	g_clear_object (&self->properties);
	g_clear_pointer (&self->layers, g_ptr_array_unref);
	g_clear_pointer (&self->tilesets, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_tiled_map_parent_class)->finalize (object);
}

static void
lrg_tiled_map_class_init (LrgTiledMapClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_tiled_map_finalize;
}

static void
lrg_tiled_map_init (LrgTiledMap *self)
{
	self->properties = lrg_tiled_properties_new_empty ();
	self->layers = g_ptr_array_new_with_free_func (g_object_unref);
	self->tilesets = g_ptr_array_new_with_free_func (g_object_unref);
	self->orientation = LRG_TILEMAP_ORIENTATION_ORTHOGONAL;
	self->stagger_axis = LRG_TILEMAP_STAGGER_AXIS_Y;
	self->stagger_index = LRG_TILEMAP_STAGGER_INDEX_ODD;
}

LrgTiledMap *
lrg_tiled_map_new_empty (const gchar *base_dir)
{
	LrgTiledMap *self = g_object_new (LRG_TYPE_TILED_MAP, NULL);

	self->base_dir = g_strdup (base_dir);
	return self;
}

static gint
compare_first_gid (gconstpointer a,
                   gconstpointer b)
{
	const LrgTiledTileset *ta = *(const LrgTiledTileset * const *) a;
	const LrgTiledTileset *tb = *(const LrgTiledTileset * const *) b;

	if (ta->first_gid < tb->first_gid)
		return -1;
	return (ta->first_gid > tb->first_gid) ? 1 : 0;
}

/* Validation both readers end with: sizes and tileset order. */
gboolean
lrg_tiled_map_finish (LrgTiledMap  *self,
                      GError      **error)
{
	guint i;

	if (self->width == 0 || self->height == 0 || self->tile_width == 0 || self->tile_height == 0)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "the map has no size (width %u, height %u, tiles %ux%u)",
		             self->width, self->height, self->tile_width, self->tile_height);
		return FALSE;
	}
	g_ptr_array_sort (self->tilesets, compare_first_gid);
	for (i = 0; i < self->tilesets->len; i++)
	{
		LrgTiledTileset *ts = g_ptr_array_index (self->tilesets, i);

		if (ts->first_gid == 0)
		{
			g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
			             "tileset '%s' has no firstgid", ts->name);
			return FALSE;
		}
		/* a tileset with an image but no columns: derive them as Tiled does */
		if (ts->columns == 0 && ts->image_width > 0 && ts->tile_width > 0)
			ts->columns = (ts->image_width - 2 * ts->margin + ts->spacing) /
			              (ts->tile_width + ts->spacing);
	}
	return TRUE;
}

LrgTiledMap *
lrg_tiled_map_new_from_data (const gchar  *data,
                             gssize        length,
                             const gchar  *base_dir,
                             GError      **error)
{
	gsize len;
	gsize i;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	len = (length < 0) ? strlen (data) : (gsize) length;
	for (i = 0; i < len && g_ascii_isspace (data[i]); i++)
		;
	/* skip a UTF-8 byte order mark */
	if (i + 2 < len && (guchar) data[i] == 0xEF && (guchar) data[i + 1] == 0xBB &&
	    (guchar) data[i + 2] == 0xBF)
		i += 3;

	if (i < len && data[i] == '{')
		return lrg_tiled_parse_tmj (data, len, base_dir, error);
	if (i < len && data[i] == '<')
		return lrg_tiled_parse_tmx (data, len, base_dir, error);

	g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_PARSE,
	             "not a Tiled map: expected a JSON object or an XML document");
	return NULL;
}

LrgTiledMap *
lrg_tiled_map_new_from_file (const gchar  *path,
                             GError      **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *abs = NULL;
	g_autoptr(GError) local = NULL;
	LrgTiledMap *map;
	gsize length = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	data = lrg_tiled_read_file (path, &length, error);
	if (data == NULL)
		return NULL;

	abs = lrg_tiled_resolve_path (NULL, path);
	dir = g_path_get_dirname (abs);
	map = lrg_tiled_map_new_from_data (data, (gssize) length, dir, &local);
	if (map == NULL)
	{
		g_propagate_prefixed_error (error, g_steal_pointer (&local), "%s: ", path);
		return NULL;
	}
	return map;
}

guint
lrg_tiled_map_get_width (LrgTiledMap *self)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), 0);
	return self->width;
}

guint
lrg_tiled_map_get_height (LrgTiledMap *self)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), 0);
	return self->height;
}

void
lrg_tiled_map_get_tile_size (LrgTiledMap *self,
                             guint       *width,
                             guint       *height)
{
	g_return_if_fail (LRG_IS_TILED_MAP (self));
	if (width != NULL)
		*width = self->tile_width;
	if (height != NULL)
		*height = self->tile_height;
}

LrgTilemapOrientation
lrg_tiled_map_get_orientation (LrgTiledMap *self)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), LRG_TILEMAP_ORIENTATION_ORTHOGONAL);
	return self->orientation;
}

const gchar *
lrg_tiled_map_get_background_color (LrgTiledMap *self)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);
	return self->background;
}

LrgTiledProperties *
lrg_tiled_map_get_properties (LrgTiledMap *self)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);
	return self->properties;
}

guint
lrg_tiled_map_get_n_layers (LrgTiledMap *self)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), 0);
	return self->layers->len;
}

LrgTiledLayer *
lrg_tiled_map_get_layer (LrgTiledMap *self,
                         guint        index)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);

	if (index >= self->layers->len)
		return NULL;
	return g_ptr_array_index (self->layers, index);
}

LrgTiledLayer *
lrg_tiled_map_get_layer_by_name (LrgTiledMap *self,
                                 const gchar *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (i = 0; i < self->layers->len; i++)
	{
		LrgTiledLayer *layer = g_ptr_array_index (self->layers, i);

		if (g_strcmp0 (layer->name, name) == 0)
			return layer;
	}
	return NULL;
}

guint
lrg_tiled_map_get_n_tilesets (LrgTiledMap *self)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), 0);
	return self->tilesets->len;
}

LrgTiledTileset *
lrg_tiled_map_get_tileset (LrgTiledMap *self,
                           guint        index)
{
	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);

	if (index >= self->tilesets->len)
		return NULL;
	return g_ptr_array_index (self->tilesets, index);
}

LrgTiledTileset *
lrg_tiled_map_get_tileset_for_gid (LrgTiledMap *self,
                                   guint        gid,
                                   guint       *local_id)
{
	LrgTiledTileset *found = NULL;
	guint id;
	guint i;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);

	id = gid & LRG_TILED_GID_MASK;
	if (id == 0)
		return NULL;
	for (i = 0; i < self->tilesets->len; i++)
	{
		LrgTiledTileset *ts = g_ptr_array_index (self->tilesets, i);

		if (ts->first_gid > id)
			break;
		found = ts;
	}
	if (found == NULL || id - found->first_gid >= MAX (found->tile_count, 1u))
		return NULL;
	if (local_id != NULL)
		*local_id = id - found->first_gid;
	return found;
}

LrgTiledObject *
lrg_tiled_map_find_object (LrgTiledMap *self,
                           const gchar *name)
{
	guint i;
	guint j;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (i = 0; i < self->layers->len; i++)
	{
		LrgTiledLayer *layer = g_ptr_array_index (self->layers, i);

		if (layer->objects == NULL)
			continue;
		for (j = 0; j < layer->objects->len; j++)
		{
			LrgTiledObject *object = g_ptr_array_index (layer->objects, j);

			if (g_strcmp0 (object->name, name) == 0)
				return object;
		}
	}
	return NULL;
}

LrgTiledObject *
lrg_tiled_map_find_object_by_id (LrgTiledMap *self,
                                 guint        id)
{
	guint i;
	guint j;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);

	if (id == 0)
		return NULL;
	for (i = 0; i < self->layers->len; i++)
	{
		LrgTiledLayer *layer = g_ptr_array_index (self->layers, i);

		if (layer->objects == NULL)
			continue;
		for (j = 0; j < layer->objects->len; j++)
		{
			LrgTiledObject *object = g_ptr_array_index (layer->objects, j);

			if (object->id == id)
				return object;
		}
	}
	return NULL;
}

GPtrArray *
lrg_tiled_map_get_objects (LrgTiledMap *self,
                           const gchar *object_type)
{
	GPtrArray *result;
	guint i;
	guint j;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);

	result = g_ptr_array_new ();
	for (i = 0; i < self->layers->len; i++)
	{
		LrgTiledLayer *layer = g_ptr_array_index (self->layers, i);

		if (layer->objects == NULL)
			continue;
		for (j = 0; j < layer->objects->len; j++)
		{
			LrgTiledObject *object = g_ptr_array_index (layer->objects, j);

			if (object_type == NULL || g_strcmp0 (object->type, object_type) == 0)
				g_ptr_array_add (result, object);
		}
	}
	return result;
}

LrgTiledObject *
lrg_tiled_map_find_zone_at (LrgTiledMap *self,
                            gdouble      x,
                            gdouble      y,
                            const gchar *object_type)
{
	LrgTiledObject *found = NULL;
	guint i;
	guint j;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);

	for (i = 0; i < self->layers->len; i++)
	{
		LrgTiledLayer *layer = g_ptr_array_index (self->layers, i);

		if (layer->objects == NULL)
			continue;
		for (j = 0; j < layer->objects->len; j++)
		{
			LrgTiledObject *object = g_ptr_array_index (layer->objects, j);

			if (!object_is_area (object))
				continue;
			if (object_type != NULL && g_strcmp0 (object->type, object_type) != 0)
				continue;
			/* later objects draw on top: keep the last hit */
			if (lrg_tiled_object_contains_point (object, x, y))
				found = object;
		}
	}
	return found;
}

LrgTilemap *
lrg_tiled_map_build_tilemap (LrgTiledMap  *self,
                             guint         tileset_index,
                             GError      **error)
{
	g_autoptr(LrgTilemap) tilemap = NULL;
	LrgTiledTileset *ts;
	LrgTileset *tileset;
	guint i;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	ts = lrg_tiled_map_get_tileset (self, tileset_index);
	if (ts == NULL)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID,
		             "the map has no tileset %u (it has %u)", tileset_index,
		             self->tilesets->len);
		return NULL;
	}
	tileset = lrg_tiled_tileset_get_tileset (ts);
	if (tileset == NULL)
	{
		g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_UNSUPPORTED,
		             "tileset '%s' is an image collection, which LrgTileset cannot "
		             "hold", ts->name);
		return NULL;
	}

	tilemap = lrg_tilemap_new (tileset);
	lrg_tilemap_set_orientation (tilemap, self->orientation);
	lrg_tilemap_set_stagger_axis (tilemap, self->stagger_axis);
	lrg_tilemap_set_stagger_index (tilemap, self->stagger_index);
	lrg_tilemap_set_hex_side_length (tilemap, self->hex_side);
	lrg_tilemap_set_cell_size (tilemap, self->tile_width, self->tile_height);

	for (i = 0; i < self->layers->len; i++)
	{
		LrgTiledLayer *src = g_ptr_array_index (self->layers, i);
		g_autoptr(LrgTilemapLayer) dst = NULL;
		g_autofree guint *cells = NULL;
		guint n;
		guint k;

		if (src->kind != LRG_TILED_LAYER_TILES || src->gids == NULL)
			continue;

		n = src->width * src->height;
		cells = g_new0 (guint, n);
		for (k = 0; k < n; k++)
		{
			guint raw = src->gids[k];
			guint id = raw & LRG_TILED_GID_MASK;

			/* the three flip bits are LrgTilemapLayer's own transform bits */
			if (id >= ts->first_gid && id - ts->first_gid < ts->tile_count)
				cells[k] = (id - ts->first_gid + 1) | (raw & LRG_TILE_FLAGS_MASK);
		}

		dst = lrg_tilemap_layer_new (src->width, src->height);
		lrg_tilemap_layer_set_name (dst, src->name);
		lrg_tilemap_layer_set_tiles (dst, cells, n);
		lrg_tilemap_layer_set_visible (dst, src->visible);
		lrg_tilemap_layer_set_opacity (dst, (gfloat) src->opacity);
		lrg_tilemap_layer_set_parallax_x (dst, (gfloat) src->parallax_x);
		lrg_tilemap_layer_set_parallax_y (dst, (gfloat) src->parallax_y);
		lrg_tilemap_layer_set_collision_enabled (
			dst,
			lrg_tiled_properties_get_boolean (src->properties, "collision", FALSE) ||
			lrg_tiled_properties_get_boolean (src->properties, "solid", FALSE));
		lrg_tilemap_add_layer (tilemap, dst);
	}
	return g_steal_pointer (&tilemap);
}

gboolean
lrg_tiled_map_load_textures (LrgTiledMap  *self,
                             GError      **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TILED_MAP (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	for (i = 0; i < self->tilesets->len; i++)
	{
		LrgTileset *tileset = lrg_tiled_tileset_get_tileset (g_ptr_array_index (self->tilesets, i));
		g_autoptr(GError) local = NULL;

		if (tileset == NULL || lrg_tileset_get_texture (tileset) != NULL)
			continue;
		if (!lrg_tileset_load_texture (tileset, &local))
		{
			g_set_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_IO,
			             "%s", local->message);
			return FALSE;
		}
	}
	return TRUE;
}
