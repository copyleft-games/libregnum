/* lrg-gltf-geometry.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless glTF 2.0 static geometry reader written from the Khronos glTF
 * 2.0 specification with json-glib. Everything is decoded and validated
 * once, when the object is created: buffers (GLB BIN chunk, data: URIs,
 * files next to the .gltf), buffer views and accessors (every element
 * must lie inside its view and buffer), mesh primitives (attribute counts
 * agree, indices stay below the vertex count) and the scene's node tree
 * (no node is reached twice, so cycles cannot recurse). Images are kept
 * as references and decoded on request.
 *
 * Reads go through memcpy and explicit little-endian conversion, so
 * unaligned or hostile data never faults.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <json-glib/json-glib.h>
#include <graylib.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-gltf-geometry.h"

#define GLB_MAGIC         (0x46546C67u)  /* "glTF" */
#define GLB_CHUNK_JSON    (0x4E4F534Au)  /* "JSON" */
#define GLB_CHUNK_BIN     (0x004E4942u)  /* "BIN\0" */
#define GLB_HEADER_SIZE   (12)
#define GLB_CHUNK_HEADER  (8)

#define COMPONENT_BYTE           (5120)
#define COMPONENT_UNSIGNED_BYTE  (5121)
#define COMPONENT_SHORT          (5122)
#define COMPONENT_UNSIGNED_SHORT (5123)
#define COMPONENT_UNSIGNED_INT   (5125)
#define COMPONENT_FLOAT          (5126)

#define MODE_POINTS          (0)
#define MODE_TRIANGLES       (4)
#define MODE_TRIANGLE_STRIP  (5)
#define MODE_TRIANGLE_FAN    (6)

#define MAX_NODE_DEPTH (1024)

/*
 * Primitive:
 *
 * One triangle primitive placed by one node, already in scene space.
 */
typedef struct
{
    gfloat  *positions;   /* 3 per vertex */
    gfloat  *normals;     /* 3 per vertex, nullable */
    gfloat  *texcoords;   /* 2 per vertex, nullable */
    guint8  *colors;      /* 4 per vertex, nullable */
    guint32 *indices;     /* triangle list */
    guint    n_vertices;
    guint    n_indices;
    gint     material;
    gchar   *node_name;
} Primitive;

typedef struct
{
    gchar            *name;
    gfloat            color[4];
    gint              image;
    LrgGltfAlphaMode  alpha_mode;
    gfloat            cutoff;
    gboolean          double_sided;
} Material;

typedef struct
{
    gint   view;      /* buffer view, or -1 */
    gchar *uri;       /* nullable */
    gchar *mime;      /* nullable */
} Image;

typedef struct
{
    gint  buffer;
    gsize offset;
    gsize length;
    gsize stride;     /* 0 = tightly packed */
} View;

struct _LrgGltfGeometry
{
    GObject    parent_instance;

    GPtrArray *buffers;      /* GBytes*, sliced to byteLength */
    GArray    *views;        /* View */
    GPtrArray *primitives;   /* Primitive* */
    GArray    *materials;    /* Material */
    GArray    *images;       /* Image */
    gchar     *base_dir;     /* nullable */
    guint      skipped;
    guint      triangles;
    gboolean   has_bounds;
    gfloat     min[3];
    gfloat     max[3];
};

G_DEFINE_TYPE (LrgGltfGeometry, lrg_gltf_geometry, G_TYPE_OBJECT)

static void
primitive_free (gpointer data)
{
    Primitive *p = data;

    g_free (p->positions);
    g_free (p->normals);
    g_free (p->texcoords);
    g_free (p->colors);
    g_free (p->indices);
    g_free (p->node_name);
    g_free (p);
}

static void
material_clear (gpointer data)
{
    Material *m = data;

    g_free (m->name);
}

static void
image_clear (gpointer data)
{
    Image *image = data;

    g_free (image->uri);
    g_free (image->mime);
}

static void
lrg_gltf_geometry_finalize (GObject *object)
{
    LrgGltfGeometry *self = LRG_GLTF_GEOMETRY (object);

    g_clear_pointer (&self->buffers, g_ptr_array_unref);
    g_clear_pointer (&self->views, g_array_unref);
    g_clear_pointer (&self->primitives, g_ptr_array_unref);
    g_clear_pointer (&self->materials, g_array_unref);
    g_clear_pointer (&self->images, g_array_unref);
    g_clear_pointer (&self->base_dir, g_free);

    G_OBJECT_CLASS (lrg_gltf_geometry_parent_class)->finalize (object);
}

static void
lrg_gltf_geometry_class_init (LrgGltfGeometryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_gltf_geometry_finalize;
}

static void
lrg_gltf_geometry_init (LrgGltfGeometry *self)
{
    self->buffers = g_ptr_array_new_with_free_func ((GDestroyNotify)g_bytes_unref);
    self->views = g_array_new (FALSE, TRUE, sizeof (View));
    self->primitives = g_ptr_array_new_with_free_func (primitive_free);
    self->materials = g_array_new (FALSE, TRUE, sizeof (Material));
    g_array_set_clear_func (self->materials, material_clear);
    self->images = g_array_new (FALSE, TRUE, sizeof (Image));
    g_array_set_clear_func (self->images, image_clear);
}

/* ------------------------------------------------------------------------ */
/* Container                                                                */
/* ------------------------------------------------------------------------ */

static guint32
read_u32 (const guint8 *p)
{
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

/* split_container:
 * Finds the JSON text and, for GLB, the optional BIN chunk that follows
 * it. The GLB header, both chunk headers and their lengths are checked
 * against the data; a plain .gltf must start (after a BOM and blanks)
 * with '{'. */
static gboolean
split_container (GBytes        *data,
                 const gchar  **json,
                 gsize         *json_length,
                 GBytes       **bin,
                 GError       **error)
{
    const guint8 *bytes;
    gsize         size = 0;
    gsize         i;

    *bin = NULL;
    bytes = g_bytes_get_data (data, &size);
    if (bytes == NULL || size == 0)
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                             "Empty glTF data");
        return FALSE;
    }

    if (size >= 4 && read_u32 (bytes) == GLB_MAGIC)
    {
        guint32 declared;
        guint32 json_chunk;
        gsize   next;

        if (size < GLB_HEADER_SIZE + GLB_CHUNK_HEADER)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                         "GLB data is truncated (%" G_GSIZE_FORMAT " bytes)", size);
            return FALSE;
        }
        if (read_u32 (bytes + 4) != 2)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED_VERSION,
                         "Unsupported GLB version %u", read_u32 (bytes + 4));
            return FALSE;
        }
        declared = read_u32 (bytes + 8);
        if (declared > size || declared < GLB_HEADER_SIZE + GLB_CHUNK_HEADER)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                         "GLB declares %u bytes but %" G_GSIZE_FORMAT " are available",
                         declared, size);
            return FALSE;
        }
        json_chunk = read_u32 (bytes + GLB_HEADER_SIZE);
        if (read_u32 (bytes + GLB_HEADER_SIZE + 4) != GLB_CHUNK_JSON)
        {
            g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                                 "First GLB chunk is not JSON");
            return FALSE;
        }
        if ((gsize)json_chunk > (gsize)declared - GLB_HEADER_SIZE - GLB_CHUNK_HEADER)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                         "GLB JSON chunk (%u bytes) overruns the container", json_chunk);
            return FALSE;
        }
        *json = (const gchar *)bytes + GLB_HEADER_SIZE + GLB_CHUNK_HEADER;
        *json_length = strnlen (*json, json_chunk);

        /* an optional BIN chunk right after the JSON chunk */
        next = GLB_HEADER_SIZE + GLB_CHUNK_HEADER + (gsize)json_chunk;
        if ((gsize)declared - next >= GLB_CHUNK_HEADER &&
            read_u32 (bytes + next + 4) == GLB_CHUNK_BIN)
        {
            guint32 bin_length = read_u32 (bytes + next);

            if ((gsize)bin_length > (gsize)declared - next - GLB_CHUNK_HEADER)
            {
                g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                             "GLB BIN chunk (%u bytes) overruns the container", bin_length);
                return FALSE;
            }
            *bin = g_bytes_new_from_bytes (data, next + GLB_CHUNK_HEADER, bin_length);
        }
        return TRUE;
    }

    i = 0;
    if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        i = 3;
    while (i < size && g_ascii_isspace (bytes[i]))
        i++;
    if (i >= size || bytes[i] != '{')
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                             "Not a glTF file (no GLB magic and no JSON object)");
        return FALSE;
    }
    *json = (const gchar *)bytes + i;
    *json_length = strnlen (*json, size - i);
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* JSON helpers                                                             */
/* ------------------------------------------------------------------------ */

static gboolean
get_array (JsonObject  *object,
           const gchar *member,
           JsonArray  **out,
           GError     **error)
{
    JsonNode *node;

    *out = NULL;
    node = json_object_get_member (object, member);
    if (node == NULL || JSON_NODE_HOLDS_NULL (node))
        return TRUE;
    if (!JSON_NODE_HOLDS_ARRAY (node))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "\"%s\" is not an array", member);
        return FALSE;
    }
    *out = json_node_get_array (node);
    return TRUE;
}

static gboolean
get_object (JsonObject  *object,
            const gchar *member,
            JsonObject **out,
            GError     **error)
{
    JsonNode *node;

    *out = NULL;
    node = json_object_get_member (object, member);
    if (node == NULL || JSON_NODE_HOLDS_NULL (node))
        return TRUE;
    if (!JSON_NODE_HOLDS_OBJECT (node))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "\"%s\" is not an object", member);
        return FALSE;
    }
    *out = json_node_get_object (node);
    return TRUE;
}

static gboolean
node_is_number (JsonNode *node)
{
    return node != NULL && JSON_NODE_HOLDS_VALUE (node) &&
        (json_node_get_value_type (node) == G_TYPE_INT64 ||
         json_node_get_value_type (node) == G_TYPE_DOUBLE);
}

static gdouble
node_number (JsonNode *node)
{
    return json_node_get_value_type (node) == G_TYPE_INT64
        ? (gdouble)json_node_get_int (node) : json_node_get_double (node);
}

/* get_count:
 * Optional non-negative integral number below @limit; @fallback when
 * missing. Integral floating-point values are accepted. */
static gboolean
get_count (JsonObject  *object,
           const gchar *member,
           gdouble      limit,
           gint64       fallback,
           gint64      *out,
           GError     **error)
{
    JsonNode *node;
    gdouble   value;

    *out = fallback;
    node = json_object_get_member (object, member);
    if (node == NULL || JSON_NODE_HOLDS_NULL (node))
        return TRUE;
    if (!node_is_number (node))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "\"%s\" is not a number", member);
        return FALSE;
    }
    value = node_number (node);
    if (value < 0 || value >= limit || value != floor (value))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "\"%s\" value %g is out of range", member, value);
        return FALSE;
    }
    *out = (gint64)value;
    return TRUE;
}

/* get_index:
 * Optional index into an array of @limit elements; -1 when missing. */
static gboolean
get_index (JsonObject  *object,
           const gchar *member,
           guint        limit,
           gint        *out,
           GError     **error)
{
    JsonNode *node;
    gdouble   value;

    *out = -1;
    node = json_object_get_member (object, member);
    if (node == NULL || JSON_NODE_HOLDS_NULL (node))
        return TRUE;
    if (!node_is_number (node))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "\"%s\" is not an index", member);
        return FALSE;
    }
    value = node_number (node);
    if (value < 0 || value >= (gdouble)limit || value != floor (value))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "\"%s\" index %g is out of range (0..%u)", member, value, limit);
        return FALSE;
    }
    *out = (gint)value;
    return TRUE;
}

/* element_index:
 * Element @i of an index list, checked against @limit. */
static gboolean
element_index (JsonArray   *array,
               guint        i,
               guint        limit,
               const gchar *what,
               gint        *out,
               GError     **error)
{
    JsonNode *element = json_array_get_element (array, i);
    gdouble   value;

    if (!node_is_number (element))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "%s %u is not an index", what, i);
        return FALSE;
    }
    value = node_number (element);
    if (value < 0 || value >= (gdouble)limit || value != floor (value))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "%s index %g is out of range (0..%u)", what, value, limit);
        return FALSE;
    }
    *out = (gint)value;
    return TRUE;
}

static gboolean
get_string (JsonObject  *object,
            const gchar *member,
            gchar      **out,
            GError     **error)
{
    JsonNode *node;

    *out = NULL;
    node = json_object_get_member (object, member);
    if (node == NULL || JSON_NODE_HOLDS_NULL (node))
        return TRUE;
    if (!JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "\"%s\" is not a string", member);
        return FALSE;
    }
    *out = g_strdup (json_node_get_string (node));
    return TRUE;
}

static gboolean
get_floats (JsonObject  *object,
            const gchar *member,
            guint        count,
            gfloat      *out,
            GError     **error)
{
    JsonArray *array = NULL;
    guint      i;

    if (!get_array (object, member, &array, error))
        return FALSE;
    if (array == NULL)
        return TRUE;
    if (json_array_get_length (array) != count)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "\"%s\" must have %u elements", member, count);
        return FALSE;
    }
    for (i = 0; i < count; i++)
    {
        JsonNode *element = json_array_get_element (array, i);

        if (!node_is_number (element))
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                         "\"%s\" element %u is not a number", member, i);
            return FALSE;
        }
        out[i] = (gfloat)node_number (element);
    }
    return TRUE;
}

static JsonObject *
array_object (JsonArray   *array,
              guint        index,
              const gchar *what,
              GError     **error)
{
    JsonNode *node = array != NULL ? json_array_get_element (array, index) : NULL;

    if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "%s %u is not an object", what, index);
        return NULL;
    }
    return json_node_get_object (node);
}

static guint
array_length (JsonArray *array)
{
    return array != NULL ? json_array_get_length (array) : 0;
}

static gboolean
has_extension (JsonObject  *object,
               const gchar *name)
{
    JsonNode *node = json_object_get_member (object, "extensions");

    return node != NULL && JSON_NODE_HOLDS_OBJECT (node) &&
        json_object_has_member (json_node_get_object (node), name);
}

static gboolean
string_array_contains (JsonObject  *object,
                       const gchar *member,
                       const gchar *value)
{
    JsonNode  *node = json_object_get_member (object, member);
    JsonArray *array;
    guint      i;

    if (node == NULL || !JSON_NODE_HOLDS_ARRAY (node))
        return FALSE;
    array = json_node_get_array (node);
    for (i = 0; i < json_array_get_length (array); i++)
    {
        JsonNode *element = json_array_get_element (array, i);

        if (JSON_NODE_HOLDS_VALUE (element) &&
            json_node_get_value_type (element) == G_TYPE_STRING &&
            g_strcmp0 (json_node_get_string (element), value) == 0)
            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------ */
/* URIs and buffers                                                         */
/* ------------------------------------------------------------------------ */

/* decode_data_uri:
 * "data:[<mime>][;base64],<payload>". Returns the payload bytes and, when
 * given, the mime type. */
static GBytes *
decode_data_uri (const gchar  *uri,
                 gchar       **mime,
                 GError      **error)
{
    const gchar *comma = strchr (uri, ',');
    const gchar *header = uri + 5;
    gboolean     base64;
    gsize        header_length;

    if (comma == NULL)
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                             "data: URI without a ',' separator");
        return NULL;
    }
    header_length = (gsize)(comma - header);
    base64 = header_length >= 7 && strncmp (comma - 7, ";base64", 7) == 0;
    if (mime != NULL)
    {
        const gchar *semicolon = memchr (header, ';', header_length);
        gsize        mime_length = semicolon != NULL ? (gsize)(semicolon - header) : header_length;

        *mime = mime_length > 0 ? g_strndup (header, mime_length) : NULL;
    }

    if (base64)
    {
        guchar *decoded;
        gsize   length = 0;

        decoded = g_base64_decode (comma + 1, &length);
        return g_bytes_new_take (decoded, length);
    }
    else
    {
        gchar *text = g_uri_unescape_string (comma + 1, NULL);

        if (text == NULL)
        {
            g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                                 "data: URI has an invalid escape");
            return NULL;
        }
        return g_bytes_new_take (text, strlen (text));
    }
}

/* resolve_uri:
 * Reads the bytes a "uri" names: a data: URI, or a relative file below
 * the base directory. Other schemes, absolute paths and ".." components
 * are refused so a mod's model cannot read files outside its folder. */
static GBytes *
resolve_uri (LrgGltfGeometry  *self,
             const gchar      *uri,
             gchar           **mime,
             GError          **error)
{
    g_autofree gchar  *relative = NULL;
    g_autofree gchar  *path = NULL;
    g_auto(GStrv)      parts = NULL;
    gchar             *contents = NULL;
    gsize              length = 0;
    guint              i;

    if (mime != NULL)
        *mime = NULL;
    if (g_str_has_prefix (uri, "data:"))
        return decode_data_uri (uri, mime, error);

    if (strstr (uri, "://") != NULL || g_str_has_prefix (uri, "file:"))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
                     "URI scheme not supported: %s", uri);
        return NULL;
    }
    if (self->base_dir == NULL)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
                     "External file %s needs a base directory", uri);
        return NULL;
    }
    relative = g_uri_unescape_string (uri, NULL);
    if (relative == NULL || relative[0] == '\0' || g_path_is_absolute (relative) ||
        relative[0] == '/' || relative[0] == '\\')
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "External file URI is not a relative path: %s", uri);
        return NULL;
    }
    parts = g_strsplit_set (relative, "/\\", -1);
    for (i = 0; parts[i] != NULL; i++)
    {
        if (strcmp (parts[i], "..") == 0)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                         "External file URI leaves the model's directory: %s", uri);
            return NULL;
        }
    }

    path = g_build_filename (self->base_dir, relative, NULL);
    if (!g_file_get_contents (path, &contents, &length, error))
        return NULL;
    return g_bytes_new_take (contents, length);
}

static gboolean
parse_buffers (LrgGltfGeometry *self,
               JsonArray       *buffers,
               GBytes          *bin,
               GError         **error)
{
    guint i;

    for (i = 0; i < array_length (buffers); i++)
    {
        JsonObject        *buffer = array_object (buffers, i, "buffer", error);
        g_autofree gchar  *uri = NULL;
        g_autoptr(GBytes)  bytes = NULL;
        gint64             declared;

        if (buffer == NULL ||
            !get_count (buffer, "byteLength", (gdouble)G_MAXUINT32 + 1.0, -1, &declared, error) ||
            !get_string (buffer, "uri", &uri, error))
            return FALSE;
        if (declared < 0)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                         "buffer %u has no byteLength", i);
            return FALSE;
        }

        if (uri == NULL)
        {
            /* the GLB binary chunk */
            if (bin == NULL)
            {
                g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                             "buffer %u has no uri and the file has no BIN chunk", i);
                return FALSE;
            }
            bytes = g_bytes_ref (bin);
        }
        else
        {
            bytes = resolve_uri (self, uri, NULL, error);
            if (bytes == NULL)
            {
                g_prefix_error (error, "buffer %u: ", i);
                return FALSE;
            }
        }

        if (g_bytes_get_size (bytes) < (gsize)declared)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                         "buffer %u holds %" G_GSIZE_FORMAT " bytes, %" G_GINT64_FORMAT
                         " declared", i, g_bytes_get_size (bytes), declared);
            return FALSE;
        }
        g_ptr_array_add (self->buffers, g_bytes_new_from_bytes (bytes, 0, (gsize)declared));
    }
    return TRUE;
}

static gboolean
parse_views (LrgGltfGeometry *self,
             JsonArray       *views,
             GError         **error)
{
    guint i;

    for (i = 0; i < array_length (views); i++)
    {
        JsonObject *object = array_object (views, i, "bufferView", error);
        View        view;
        gint64      offset;
        gint64      length;
        gint64      stride;
        gsize       size;

        if (object == NULL ||
            !get_index (object, "buffer", self->buffers->len, &view.buffer, error) ||
            !get_count (object, "byteOffset", (gdouble)G_MAXUINT32 + 1.0, 0, &offset, error) ||
            !get_count (object, "byteLength", (gdouble)G_MAXUINT32 + 1.0, -1, &length, error) ||
            !get_count (object, "byteStride", 253.0, 0, &stride, error))
            return FALSE;
        if (view.buffer < 0 || length < 0)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                         "bufferView %u needs a buffer and a byteLength", i);
            return FALSE;
        }
        size = g_bytes_get_size (g_ptr_array_index (self->buffers, view.buffer));
        if ((guint64)offset + (guint64)length > (guint64)size)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                         "bufferView %u (%" G_GINT64_FORMAT " + %" G_GINT64_FORMAT
                         ") overruns buffer %d (%" G_GSIZE_FORMAT " bytes)",
                         i, offset, length, view.buffer, size);
            return FALSE;
        }
        view.offset = (gsize)offset;
        view.length = (gsize)length;
        view.stride = (gsize)stride;
        g_array_append_val (self->views, view);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Accessors                                                                */
/* ------------------------------------------------------------------------ */

/*
 * Accessor:
 *
 * A validated accessor: @data is NULL when the accessor has no buffer
 * view (every element is zero, as the specification says).
 */
typedef struct
{
    const guint8 *data;
    gsize         stride;
    guint         count;
    guint         components;
    gint          component_type;
    gboolean      normalized;
} Accessor;

static guint
component_size (gint component_type)
{
    switch (component_type)
    {
    case COMPONENT_BYTE:
    case COMPONENT_UNSIGNED_BYTE:
        return 1;
    case COMPONENT_SHORT:
    case COMPONENT_UNSIGNED_SHORT:
        return 2;
    case COMPONENT_UNSIGNED_INT:
    case COMPONENT_FLOAT:
        return 4;
    default:
        return 0;
    }
}

static guint
type_components (const gchar *type)
{
    if (g_strcmp0 (type, "SCALAR") == 0)
        return 1;
    if (g_strcmp0 (type, "VEC2") == 0)
        return 2;
    if (g_strcmp0 (type, "VEC3") == 0)
        return 3;
    if (g_strcmp0 (type, "VEC4") == 0)
        return 4;
    return 0;
}

/* open_accessor:
 * Validates accessor @index and checks that every element lies inside
 * its buffer view: offset + stride * (count - 1) + element size. */
static gboolean
open_accessor (LrgGltfGeometry *self,
               JsonArray       *accessors,
               gint             index,
               Accessor        *out,
               GError         **error)
{
    JsonObject       *object;
    g_autofree gchar *type = NULL;
    gint              view_index;
    gint64            offset;
    gint64            count;
    gint64            component_type;
    JsonNode         *normalized;
    guint             element;
    const View       *view;
    const guint8     *base;

    object = array_object (accessors, (guint)index, "accessor", error);
    if (object == NULL ||
        !get_index (object, "bufferView", self->views->len, &view_index, error) ||
        !get_count (object, "byteOffset", (gdouble)G_MAXUINT32 + 1.0, 0, &offset, error) ||
        !get_count (object, "count", (gdouble)LRG_GLTF_GEOMETRY_MAX_VERTICES + 1.0, -1,
                    &count, error) ||
        !get_count (object, "componentType", 6000.0, -1, &component_type, error) ||
        !get_string (object, "type", &type, error))
    {
        g_prefix_error (error, "accessor %d: ", index);
        return FALSE;
    }
    if (json_object_has_member (object, "sparse"))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
                     "accessor %d is sparse", index);
        return FALSE;
    }
    if (count < 0 || component_size ((gint)component_type) == 0 || type_components (type) == 0)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "accessor %d has no count, or an unsupported componentType or type",
                     index);
        return FALSE;
    }

    out->count = (guint)count;
    out->components = type_components (type);
    out->component_type = (gint)component_type;
    normalized = json_object_get_member (object, "normalized");
    out->normalized = normalized != NULL && JSON_NODE_HOLDS_VALUE (normalized) &&
        json_node_get_value_type (normalized) == G_TYPE_BOOLEAN &&
        json_node_get_boolean (normalized);
    element = component_size (out->component_type) * out->components;
    out->data = NULL;
    out->stride = element;
    if (view_index < 0)
        return TRUE;

    view = &g_array_index (self->views, View, view_index);
    out->stride = view->stride != 0 ? view->stride : element;
    if (out->stride < element)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "accessor %d: byteStride %" G_GSIZE_FORMAT " is smaller than an element",
                     index, out->stride);
        return FALSE;
    }
    if (out->count > 0 &&
        (guint64)offset + (guint64)out->stride * (out->count - 1) + element > view->length)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "accessor %d (%u elements) overruns bufferView %d", index, out->count,
                     view_index);
        return FALSE;
    }
    base = g_bytes_get_data (g_ptr_array_index (self->buffers, view->buffer), NULL);
    out->data = base + view->offset + (gsize)offset;
    return TRUE;
}

/* read_component:
 * Component @c of element @i, as a float; normalized integers map to
 * [0, 1] or [-1, 1] as the specification defines. */
static gdouble
read_component (const Accessor *a,
                guint           i,
                guint           c)
{
    const guint8 *p;
    guint8        u8;
    guint16       u16;
    guint32       u32;
    gfloat        f;

    if (a->data == NULL)
        return 0.0;
    p = a->data + a->stride * i + component_size (a->component_type) * c;
    switch (a->component_type)
    {
    case COMPONENT_BYTE:
        u8 = p[0];
        return a->normalized ? MAX ((gdouble)(gint8)u8 / 127.0, -1.0) : (gdouble)(gint8)u8;
    case COMPONENT_UNSIGNED_BYTE:
        return a->normalized ? (gdouble)p[0] / 255.0 : (gdouble)p[0];
    case COMPONENT_SHORT:
        memcpy (&u16, p, 2);
        u16 = GUINT16_FROM_LE (u16);
        return a->normalized ? MAX ((gdouble)(gint16)u16 / 32767.0, -1.0) : (gdouble)(gint16)u16;
    case COMPONENT_UNSIGNED_SHORT:
        memcpy (&u16, p, 2);
        u16 = GUINT16_FROM_LE (u16);
        return a->normalized ? (gdouble)u16 / 65535.0 : (gdouble)u16;
    case COMPONENT_UNSIGNED_INT:
        memcpy (&u32, p, 4);
        return (gdouble)GUINT32_FROM_LE (u32);
    case COMPONENT_FLOAT:
    default:
        memcpy (&u32, p, 4);
        u32 = GUINT32_FROM_LE (u32);
        memcpy (&f, &u32, 4);
        return (gdouble)f;
    }
}

/* read_floats:
 * An attribute as @components floats per element; the accessor must
 * have exactly @components components and @count elements. Non-finite
 * values are refused. */
static gfloat *
read_floats (LrgGltfGeometry *self,
             JsonArray       *accessors,
             gint             index,
             guint            components,
             guint            count,
             const gchar     *what,
             GError         **error)
{
    Accessor a;
    gfloat  *out;
    guint    i;
    guint    c;

    if (!open_accessor (self, accessors, index, &a, error))
        return NULL;
    if (a.components != components || a.count != count)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "%s accessor %d has %u x %u components, %u x %u expected",
                     what, index, a.count, a.components, count, components);
        return NULL;
    }
    out = g_new (gfloat, (gsize)MAX (count, 1u) * components);
    for (i = 0; i < count; i++)
    {
        for (c = 0; c < components; c++)
        {
            gdouble v = read_component (&a, i, c);

            if (!isfinite (v))
            {
                g_free (out);
                g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                             "%s accessor %d holds a non-finite value", what, index);
                return NULL;
            }
            out[i * components + c] = (gfloat)v;
        }
    }
    return out;
}

/* read_colors:
 * COLOR_0 (VEC3 or VEC4, float or normalized integers) as RGBA bytes. */
static guint8 *
read_colors (LrgGltfGeometry *self,
             JsonArray       *accessors,
             gint             index,
             guint            count,
             GError         **error)
{
    Accessor a;
    guint8  *out;
    guint    i;
    guint    c;

    if (!open_accessor (self, accessors, index, &a, error))
        return NULL;
    if ((a.components != 3 && a.components != 4) || a.count != count)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "COLOR_0 accessor %d must be VEC3 or VEC4 with %u elements", index, count);
        return NULL;
    }
    if (a.component_type != COMPONENT_FLOAT)
        a.normalized = TRUE;
    out = g_new (guint8, (gsize)MAX (count, 1u) * 4);
    for (i = 0; i < count; i++)
    {
        for (c = 0; c < 4; c++)
        {
            gdouble v = c < a.components ? read_component (&a, i, c) : 1.0;

            if (!isfinite (v))
                v = 1.0;
            out[i * 4 + c] = (guint8)(CLAMP (v, 0.0, 1.0) * 255.0 + 0.5);
        }
    }
    return out;
}

/* read_indices:
 * The "indices" accessor (unsigned SCALAR), every value below
 * @n_vertices; or 0 .. n_vertices - 1 when the primitive has none. */
static guint32 *
read_indices (LrgGltfGeometry *self,
              JsonArray       *accessors,
              gint             index,
              guint            n_vertices,
              guint           *n_out,
              GError         **error)
{
    Accessor  a;
    guint32  *out;
    guint     i;

    if (index < 0)
    {
        out = g_new (guint32, MAX (n_vertices, 1u));
        for (i = 0; i < n_vertices; i++)
            out[i] = i;
        *n_out = n_vertices;
        return out;
    }

    if (!open_accessor (self, accessors, index, &a, error))
        return NULL;
    if (a.components != 1 ||
        (a.component_type != COMPONENT_UNSIGNED_BYTE &&
         a.component_type != COMPONENT_UNSIGNED_SHORT &&
         a.component_type != COMPONENT_UNSIGNED_INT))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "indices accessor %d is not an unsigned SCALAR", index);
        return NULL;
    }
    a.normalized = FALSE;
    out = g_new (guint32, MAX (a.count, 1u));
    for (i = 0; i < a.count; i++)
    {
        gdouble v = read_component (&a, i, 0);

        if (v >= (gdouble)n_vertices)
        {
            g_free (out);
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                         "index %g at %u is not below the vertex count %u", v, i, n_vertices);
            return NULL;
        }
        out[i] = (guint32)v;
    }
    *n_out = a.count;
    return out;
}

/* to_triangle_list:
 * Unrolls strips and fans with the specification's vertex order; lists
 * drop a trailing partial triangle. */
static guint32 *
to_triangle_list (const guint32 *in,
                  guint          n_in,
                  gint           mode,
                  guint         *n_out)
{
    guint32 *out;
    guint    n_triangles;
    guint    i;

    if (mode == MODE_TRIANGLES)
        n_triangles = n_in / 3;
    else
        n_triangles = n_in >= 3 ? n_in - 2 : 0;

    out = g_new (guint32, (gsize)MAX (n_triangles, 1u) * 3);
    for (i = 0; i < n_triangles; i++)
    {
        guint32 *t = &out[i * 3];

        if (mode == MODE_TRIANGLES)
        {
            t[0] = in[i * 3];
            t[1] = in[i * 3 + 1];
            t[2] = in[i * 3 + 2];
        }
        else if (mode == MODE_TRIANGLE_STRIP)
        {
            t[0] = in[i];
            t[1] = in[i + 1 + (i % 2)];
            t[2] = in[i + 2 - (i % 2)];
        }
        else
        {
            t[0] = in[i + 1];
            t[1] = in[i + 2];
            t[2] = in[0];
        }
    }
    *n_out = n_triangles * 3;
    return out;
}

/* ------------------------------------------------------------------------ */
/* Transforms                                                               */
/* ------------------------------------------------------------------------ */

/* node_local_matrix:
 * Column-major local matrix: "matrix" verbatim, otherwise T * R * S. */
static gboolean
node_local_matrix (JsonObject *node,
                   gfloat     *m,
                   GError    **error)
{
    gfloat t[3] = { 0.0f, 0.0f, 0.0f };
    gfloat q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    gfloat s[3] = { 1.0f, 1.0f, 1.0f };

    if (json_object_has_member (node, "matrix"))
        return get_floats (node, "matrix", 16, m, error);
    if (!get_floats (node, "translation", 3, t, error) ||
        !get_floats (node, "rotation", 4, q, error) ||
        !get_floats (node, "scale", 3, s, error))
        return FALSE;

    m[0] = (1 - 2 * q[1] * q[1] - 2 * q[2] * q[2]) * s[0];
    m[1] = (2 * q[0] * q[1] + 2 * q[2] * q[3]) * s[0];
    m[2] = (2 * q[0] * q[2] - 2 * q[1] * q[3]) * s[0];
    m[3] = 0.0f;
    m[4] = (2 * q[0] * q[1] - 2 * q[2] * q[3]) * s[1];
    m[5] = (1 - 2 * q[0] * q[0] - 2 * q[2] * q[2]) * s[1];
    m[6] = (2 * q[1] * q[2] + 2 * q[0] * q[3]) * s[1];
    m[7] = 0.0f;
    m[8] = (2 * q[0] * q[2] + 2 * q[1] * q[3]) * s[2];
    m[9] = (2 * q[1] * q[2] - 2 * q[0] * q[3]) * s[2];
    m[10] = (1 - 2 * q[0] * q[0] - 2 * q[1] * q[1]) * s[2];
    m[11] = 0.0f;
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    m[15] = 1.0f;
    return TRUE;
}

/* mat_mul: out = a * b, column-major 4x4; @out may not alias. */
static void
mat_mul (const gfloat *a,
         const gfloat *b,
         gfloat       *out)
{
    guint r;
    guint c;
    guint k;

    for (c = 0; c < 4; c++)
    {
        for (r = 0; r < 4; r++)
        {
            gfloat sum = 0.0f;

            for (k = 0; k < 4; k++)
                sum += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = sum;
        }
    }
}

/* normal_matrix:
 * The inverse transpose of the upper 3x3 (row-major into @n), which is
 * the cofactor matrix divided by the determinant. Returns the
 * determinant; a singular matrix leaves @n as the identity. */
static gfloat
normal_matrix (const gfloat *m,
               gfloat       *n)
{
    gfloat a[3][3];
    gfloat det;
    guint  r;
    guint  c;

    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            a[r][c] = m[c * 4 + r];

    n[0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    n[1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    n[2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    n[3] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
    n[4] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
    n[5] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
    n[6] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
    n[7] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
    n[8] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
    det = a[0][0] * n[0] + a[0][1] * n[1] + a[0][2] * n[2];

    if (fabsf (det) < 1e-12f)
    {
        memset (n, 0, sizeof (gfloat) * 9);
        n[0] = n[4] = n[8] = 1.0f;
        return det;
    }
    for (r = 0; r < 9; r++)
        n[r] /= det;
    return det;
}

/* ------------------------------------------------------------------------ */
/* Meshes and the scene                                                     */
/* ------------------------------------------------------------------------ */

typedef struct
{
    JsonArray *nodes;
    JsonArray *meshes;
    JsonArray *accessors;
    guint      n_materials;
    guint8    *visited;   /* one flag per node */
} SceneContext;

static void
grow_bounds (LrgGltfGeometry *self,
             const gfloat    *p)
{
    guint axis;

    if (!self->has_bounds)
    {
        memcpy (self->min, p, sizeof (self->min));
        memcpy (self->max, p, sizeof (self->max));
        self->has_bounds = TRUE;
        return;
    }
    for (axis = 0; axis < 3; axis++)
    {
        self->min[axis] = MIN (self->min[axis], p[axis]);
        self->max[axis] = MAX (self->max[axis], p[axis]);
    }
}

/* add_primitive:
 * Reads one primitive of a mesh placed with @world and appends it in
 * scene space. Point and line primitives are counted and skipped. */
static gboolean
add_primitive (LrgGltfGeometry *self,
               SceneContext    *ctx,
               JsonObject      *object,
               const gfloat    *world,
               const gchar     *node_name,
               GError         **error)
{
    JsonObject        *attributes = NULL;
    g_autofree guint32 *raw = NULL;
    Primitive         *p;
    gint64             mode;
    gint               position;
    gint               normal;
    gint               texcoord;
    gint               color;
    gint               indices;
    guint              n_raw = 0;
    guint              n_accessors;
    gfloat             nm[9];
    gfloat             det;
    Accessor           probe;
    guint              i;

    if (!get_count (object, "mode", 7.0, MODE_TRIANGLES, &mode, error))
        return FALSE;
    if (has_extension (object, "KHR_draco_mesh_compression"))
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
                             "Draco-compressed primitives are not supported");
        return FALSE;
    }
    if (mode < MODE_TRIANGLES)
    {
        self->skipped++;
        return TRUE;
    }

    n_accessors = array_length (ctx->accessors);
    if (!get_object (object, "attributes", &attributes, error))
        return FALSE;
    if (attributes == NULL)
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                             "primitive has no attributes");
        return FALSE;
    }
    if (!get_index (attributes, "POSITION", n_accessors, &position, error) ||
        !get_index (attributes, "NORMAL", n_accessors, &normal, error) ||
        !get_index (attributes, "TEXCOORD_0", n_accessors, &texcoord, error) ||
        !get_index (attributes, "COLOR_0", n_accessors, &color, error) ||
        !get_index (object, "indices", n_accessors, &indices, error))
        return FALSE;
    if (position < 0)
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                             "primitive has no POSITION");
        return FALSE;
    }
    if (!open_accessor (self, ctx->accessors, position, &probe, error))
        return FALSE;

    p = g_new0 (Primitive, 1);
    g_ptr_array_add (self->primitives, p);   /* freed with the object on error */
    p->n_vertices = probe.count;
    p->node_name = g_strdup (node_name);
    if (!get_index (object, "material", ctx->n_materials, &p->material, error))
        return FALSE;

    p->positions = read_floats (self, ctx->accessors, position, 3, p->n_vertices, "POSITION",
                                error);
    if (p->positions == NULL)
        return FALSE;
    if (normal >= 0)
    {
        p->normals = read_floats (self, ctx->accessors, normal, 3, p->n_vertices, "NORMAL",
                                  error);
        if (p->normals == NULL)
            return FALSE;
    }
    if (texcoord >= 0)
    {
        p->texcoords = read_floats (self, ctx->accessors, texcoord, 2, p->n_vertices,
                                    "TEXCOORD_0", error);
        if (p->texcoords == NULL)
            return FALSE;
    }
    if (color >= 0)
    {
        p->colors = read_colors (self, ctx->accessors, color, p->n_vertices, error);
        if (p->colors == NULL)
            return FALSE;
    }
    raw = read_indices (self, ctx->accessors, indices, p->n_vertices, &n_raw, error);
    if (raw == NULL)
        return FALSE;
    p->indices = to_triangle_list (raw, n_raw, (gint)mode, &p->n_indices);

    /* place into scene space */
    det = normal_matrix (world, nm);
    for (i = 0; i < p->n_vertices; i++)
    {
        gfloat *v = &p->positions[i * 3];
        gfloat  x = v[0];
        gfloat  y = v[1];
        gfloat  z = v[2];

        v[0] = world[0] * x + world[4] * y + world[8] * z + world[12];
        v[1] = world[1] * x + world[5] * y + world[9] * z + world[13];
        v[2] = world[2] * x + world[6] * y + world[10] * z + world[14];
        grow_bounds (self, v);

        if (p->normals != NULL)
        {
            gfloat *n = &p->normals[i * 3];
            gfloat  nx = n[0];
            gfloat  ny = n[1];
            gfloat  nz = n[2];
            gfloat  length;

            n[0] = nm[0] * nx + nm[1] * ny + nm[2] * nz;
            n[1] = nm[3] * nx + nm[4] * ny + nm[5] * nz;
            n[2] = nm[6] * nx + nm[7] * ny + nm[8] * nz;
            length = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length > 1e-20f)
            {
                n[0] /= length;
                n[1] /= length;
                n[2] /= length;
            }
        }
    }

    /* a mirroring transform turns counter-clockwise faces clockwise */
    if (det < 0.0f)
    {
        for (i = 0; i + 2 < p->n_indices; i += 3)
        {
            guint32 swap = p->indices[i + 1];

            p->indices[i + 1] = p->indices[i + 2];
            p->indices[i + 2] = swap;
        }
    }
    self->triangles += p->n_indices / 3;
    return TRUE;
}

/* visit_node:
 * Places node @index (and its children) under @parent. A node reached a
 * second time (two parents, a cycle, a root listed twice) is an error. */
static gboolean
visit_node (LrgGltfGeometry *self,
            SceneContext    *ctx,
            gint             index,
            const gfloat    *parent,
            guint            depth,
            GError         **error)
{
    JsonObject        *node;
    JsonArray         *children = NULL;
    g_autofree gchar  *name = NULL;
    gfloat             local[16];
    gfloat             world[16];
    gint               mesh;
    guint              i;

    if (ctx->visited[index] || depth > MAX_NODE_DEPTH)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "node %d is reached twice (a cycle or two parents)", index);
        return FALSE;
    }
    ctx->visited[index] = 1;

    node = array_object (ctx->nodes, (guint)index, "node", error);
    if (node == NULL ||
        !get_string (node, "name", &name, error) ||
        !node_local_matrix (node, local, error) ||
        !get_index (node, "mesh", array_length (ctx->meshes), &mesh, error) ||
        !get_array (node, "children", &children, error))
        return FALSE;
    mat_mul (parent, local, world);

    if (mesh >= 0)
    {
        JsonObject *object = array_object (ctx->meshes, (guint)mesh, "mesh", error);
        JsonArray  *primitives = NULL;

        if (object == NULL || !get_array (object, "primitives", &primitives, error))
            return FALSE;
        for (i = 0; i < array_length (primitives); i++)
        {
            JsonObject *primitive = array_object (primitives, i, "primitive", error);

            if (primitive == NULL ||
                !add_primitive (self, ctx, primitive, world, name, error))
            {
                g_prefix_error (error, "mesh %d primitive %u: ", mesh, i);
                return FALSE;
            }
        }
    }

    for (i = 0; i < array_length (children); i++)
    {
        gint child;

        if (!element_index (children, i, array_length (ctx->nodes), "child node", &child, error))
            return FALSE;
        if (!visit_node (self, ctx, child, world, depth + 1, error))
            return FALSE;
    }
    return TRUE;
}

static gboolean
node_has_parent (JsonArray *nodes,
                 guint      index)
{
    guint i;
    guint j;

    for (i = 0; i < array_length (nodes); i++)
    {
        JsonNode  *element = json_array_get_element (nodes, i);
        JsonNode  *member;
        JsonArray *children;

        if (!JSON_NODE_HOLDS_OBJECT (element))
            continue;
        member = json_object_get_member (json_node_get_object (element), "children");
        if (member == NULL || !JSON_NODE_HOLDS_ARRAY (member))
            continue;
        children = json_node_get_array (member);
        for (j = 0; j < json_array_get_length (children); j++)
        {
            JsonNode *child = json_array_get_element (children, j);

            if (node_is_number (child) && node_number (child) == (gdouble)index)
                return TRUE;
        }
    }
    return FALSE;
}

static gboolean
parse_scene (LrgGltfGeometry *self,
             JsonObject      *root,
             SceneContext    *ctx,
             GError         **error)
{
    static const gfloat identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    JsonArray *scenes = NULL;
    guint      n_nodes = array_length (ctx->nodes);
    g_autofree guint8 *visited = NULL;
    gint       scene;
    guint      i;

    if (!get_array (root, "scenes", &scenes, error) ||
        !get_index (root, "scene", array_length (scenes), &scene, error))
        return FALSE;
    visited = g_new0 (guint8, MAX (n_nodes, 1u));
    ctx->visited = visited;

    if (array_length (scenes) > 0)
    {
        JsonObject *object = array_object (scenes, (guint)MAX (scene, 0), "scene", error);
        JsonArray  *roots = NULL;

        if (object == NULL || !get_array (object, "nodes", &roots, error))
            return FALSE;
        for (i = 0; i < array_length (roots); i++)
        {
            gint root_node;

            if (!element_index (roots, i, n_nodes, "scene node", &root_node, error))
                return FALSE;
            if (!visit_node (self, ctx, root_node, identity, 0, error))
                return FALSE;
        }
        return TRUE;
    }

    /* no scene: every root node */
    for (i = 0; i < n_nodes; i++)
    {
        if (node_has_parent (ctx->nodes, i) || visited[i])
            continue;
        if (!visit_node (self, ctx, (gint)i, identity, 0, error))
            return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Materials and images                                                     */
/* ------------------------------------------------------------------------ */

static gboolean
parse_materials (LrgGltfGeometry *self,
                 JsonArray       *materials,
                 JsonArray       *textures,
                 GError         **error)
{
    guint i;

    for (i = 0; i < array_length (materials); i++)
    {
        JsonObject       *object = array_object (materials, i, "material", error);
        JsonObject       *pbr = NULL;
        JsonObject       *base = NULL;
        g_autofree gchar *mode = NULL;
        JsonNode         *member;
        Material          m;

        memset (&m, 0, sizeof (m));
        m.color[0] = m.color[1] = m.color[2] = m.color[3] = 1.0f;
        m.image = -1;
        m.cutoff = 0.5f;
        g_array_append_val (self->materials, m);   /* cleared with the array */
        if (object == NULL ||
            !get_string (object, "name", &g_array_index (self->materials, Material, i).name,
                         error) ||
            !get_object (object, "pbrMetallicRoughness", &pbr, error) ||
            !get_string (object, "alphaMode", &mode, error))
            return FALSE;

        if (pbr != NULL)
        {
            gint texture = -1;

            if (!get_floats (pbr, "baseColorFactor", 4,
                             g_array_index (self->materials, Material, i).color, error) ||
                !get_object (pbr, "baseColorTexture", &base, error))
                return FALSE;
            if (base != NULL && !get_index (base, "index", array_length (textures), &texture,
                                            error))
                return FALSE;
            if (texture >= 0)
            {
                JsonObject *tex = array_object (textures, (guint)texture, "texture", error);

                if (tex == NULL ||
                    !get_index (tex, "source", self->images->len,
                                &g_array_index (self->materials, Material, i).image, error))
                    return FALSE;
            }
        }

        if (g_strcmp0 (mode, "MASK") == 0)
            g_array_index (self->materials, Material, i).alpha_mode = LRG_GLTF_ALPHA_MODE_MASK;
        else if (g_strcmp0 (mode, "BLEND") == 0)
            g_array_index (self->materials, Material, i).alpha_mode = LRG_GLTF_ALPHA_MODE_BLEND;
        member = json_object_get_member (object, "alphaCutoff");
        if (node_is_number (member))
            g_array_index (self->materials, Material, i).cutoff = (gfloat)node_number (member);
        member = json_object_get_member (object, "doubleSided");
        if (member != NULL && JSON_NODE_HOLDS_VALUE (member) &&
            json_node_get_value_type (member) == G_TYPE_BOOLEAN)
            g_array_index (self->materials, Material, i).double_sided =
                json_node_get_boolean (member);
    }
    return TRUE;
}

static gboolean
parse_images (LrgGltfGeometry *self,
              JsonArray       *images,
              GError         **error)
{
    guint i;

    for (i = 0; i < array_length (images); i++)
    {
        JsonObject *object = array_object (images, i, "image", error);
        Image       image;

        memset (&image, 0, sizeof (image));
        image.view = -1;
        g_array_append_val (self->images, image);
        if (object == NULL ||
            !get_string (object, "uri", &g_array_index (self->images, Image, i).uri, error) ||
            !get_string (object, "mimeType", &g_array_index (self->images, Image, i).mime,
                         error) ||
            !get_index (object, "bufferView", self->views->len,
                        &g_array_index (self->images, Image, i).view, error))
            return FALSE;
        if (g_array_index (self->images, Image, i).uri == NULL &&
            g_array_index (self->images, Image, i).view < 0)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                         "image %u has neither a uri nor a bufferView", i);
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
parse_document (LrgGltfGeometry *self,
                JsonObject      *root,
                GBytes          *bin,
                GError         **error)
{
    JsonArray   *buffers = NULL;
    JsonArray   *views = NULL;
    JsonArray   *materials = NULL;
    JsonArray   *textures = NULL;
    JsonArray   *images = NULL;
    SceneContext ctx;

    memset (&ctx, 0, sizeof (ctx));
    if (string_array_contains (root, "extensionsRequired", "KHR_draco_mesh_compression") ||
        string_array_contains (root, "extensionsRequired", "EXT_meshopt_compression"))
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
                             "The file requires mesh compression (Draco or meshopt)");
        return FALSE;
    }

    if (!get_array (root, "buffers", &buffers, error) ||
        !get_array (root, "bufferViews", &views, error) ||
        !get_array (root, "accessors", &ctx.accessors, error) ||
        !get_array (root, "meshes", &ctx.meshes, error) ||
        !get_array (root, "nodes", &ctx.nodes, error) ||
        !get_array (root, "materials", &materials, error) ||
        !get_array (root, "textures", &textures, error) ||
        !get_array (root, "images", &images, error))
        return FALSE;

    ctx.n_materials = array_length (materials);
    return parse_buffers (self, buffers, bin, error) &&
        parse_views (self, views, error) &&
        parse_images (self, images, error) &&
        parse_materials (self, materials, textures, error) &&
        parse_scene (self, root, &ctx, error);
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

LrgGltfGeometry *
lrg_gltf_geometry_new_from_bytes (GBytes       *data,
                                  const gchar  *base_dir,
                                  GError      **error)
{
    g_autoptr(LrgGltfGeometry) self = NULL;
    g_autoptr(JsonParser)      parser = NULL;
    g_autoptr(GError)          parse_error = NULL;
    g_autoptr(GBytes)          bin = NULL;
    const gchar               *json = NULL;
    gsize                      json_length = 0;
    JsonNode                  *root;

    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!split_container (data, &json, &json_length, &bin, error))
        return NULL;

    parser = json_parser_new_immutable ();
    if (!json_parser_load_from_data (parser, json, (gssize)json_length, &parse_error))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "Invalid glTF JSON: %s", parse_error->message);
        return NULL;
    }
    root = json_parser_get_root (parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                             "glTF JSON root is not an object");
        return NULL;
    }

    self = g_object_new (LRG_TYPE_GLTF_GEOMETRY, NULL);
    self->base_dir = g_strdup (base_dir);
    if (!parse_document (self, json_node_get_object (root), bin, error))
        return NULL;
    return g_steal_pointer (&self);
}

LrgGltfGeometry *
lrg_gltf_geometry_new_from_file (const gchar  *path,
                                 GError      **error)
{
    g_autoptr(GBytes)  bytes = NULL;
    g_autofree gchar  *dir = NULL;
    gchar             *contents = NULL;
    gsize              length = 0;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_file_get_contents (path, &contents, &length, error))
        return NULL;
    bytes = g_bytes_new_take (contents, length);
    dir = g_path_get_dirname (path);
    return lrg_gltf_geometry_new_from_bytes (bytes, dir, error);
}

static Primitive *
primitive_at (LrgGltfGeometry *self,
              guint            index)
{
    return index < self->primitives->len ? g_ptr_array_index (self->primitives, index) : NULL;
}

guint
lrg_gltf_geometry_get_primitive_count (LrgGltfGeometry *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), 0);
    return self->primitives->len;
}

guint
lrg_gltf_geometry_get_skipped_count (LrgGltfGeometry *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), 0);
    return self->skipped;
}

const gfloat *
lrg_gltf_geometry_get_positions (LrgGltfGeometry *self,
                                 guint            primitive,
                                 guint           *n_vertices)
{
    Primitive *p;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    p = primitive_at (self, primitive);
    if (n_vertices != NULL)
        *n_vertices = p != NULL ? p->n_vertices : 0;
    return p != NULL ? p->positions : NULL;
}

const gfloat *
lrg_gltf_geometry_get_normals (LrgGltfGeometry *self,
                               guint            primitive)
{
    Primitive *p;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    p = primitive_at (self, primitive);
    return p != NULL ? p->normals : NULL;
}

const gfloat *
lrg_gltf_geometry_get_texcoords (LrgGltfGeometry *self,
                                 guint            primitive)
{
    Primitive *p;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    p = primitive_at (self, primitive);
    return p != NULL ? p->texcoords : NULL;
}

const guint8 *
lrg_gltf_geometry_get_colors (LrgGltfGeometry *self,
                              guint            primitive)
{
    Primitive *p;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    p = primitive_at (self, primitive);
    return p != NULL ? p->colors : NULL;
}

const guint32 *
lrg_gltf_geometry_get_indices (LrgGltfGeometry *self,
                               guint            primitive,
                               guint           *n_indices)
{
    Primitive *p;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    p = primitive_at (self, primitive);
    if (n_indices != NULL)
        *n_indices = p != NULL ? p->n_indices : 0;
    return p != NULL ? p->indices : NULL;
}

gint
lrg_gltf_geometry_get_primitive_material (LrgGltfGeometry *self,
                                          guint            primitive)
{
    Primitive *p;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), -1);
    p = primitive_at (self, primitive);
    return p != NULL ? p->material : -1;
}

const gchar *
lrg_gltf_geometry_get_primitive_node_name (LrgGltfGeometry *self,
                                           guint            primitive)
{
    Primitive *p;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    p = primitive_at (self, primitive);
    return p != NULL ? p->node_name : NULL;
}

guint
lrg_gltf_geometry_get_triangle_count (LrgGltfGeometry *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), 0);
    return self->triangles;
}

gboolean
lrg_gltf_geometry_get_bounds (LrgGltfGeometry *self,
                              gfloat          *min,
                              gfloat          *max)
{
    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), FALSE);
    g_return_val_if_fail (min != NULL && max != NULL, FALSE);

    if (!self->has_bounds)
    {
        memset (min, 0, sizeof (gfloat) * 3);
        memset (max, 0, sizeof (gfloat) * 3);
        return FALSE;
    }
    memcpy (min, self->min, sizeof (self->min));
    memcpy (max, self->max, sizeof (self->max));
    return TRUE;
}

static Material *
material_at (LrgGltfGeometry *self,
             guint            index)
{
    return index < self->materials->len ? &g_array_index (self->materials, Material, index)
                                        : NULL;
}

guint
lrg_gltf_geometry_get_material_count (LrgGltfGeometry *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), 0);
    return self->materials->len;
}

const gchar *
lrg_gltf_geometry_get_material_name (LrgGltfGeometry *self,
                                     guint            material)
{
    Material *m;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    m = material_at (self, material);
    return m != NULL ? m->name : NULL;
}

gboolean
lrg_gltf_geometry_get_material_color (LrgGltfGeometry *self,
                                      guint            material,
                                      gfloat          *rgba)
{
    Material *m;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), FALSE);
    g_return_val_if_fail (rgba != NULL, FALSE);

    m = material_at (self, material);
    if (m == NULL)
    {
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 1.0f;
        return FALSE;
    }
    memcpy (rgba, m->color, sizeof (m->color));
    return TRUE;
}

gint
lrg_gltf_geometry_get_material_image (LrgGltfGeometry *self,
                                      guint            material)
{
    Material *m;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), -1);
    m = material_at (self, material);
    return m != NULL ? m->image : -1;
}

LrgGltfAlphaMode
lrg_gltf_geometry_get_material_alpha_mode (LrgGltfGeometry *self,
                                           guint            material,
                                           gfloat          *cutoff)
{
    Material *m;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), LRG_GLTF_ALPHA_MODE_OPAQUE);
    m = material_at (self, material);
    if (cutoff != NULL)
        *cutoff = m != NULL ? m->cutoff : 0.5f;
    return m != NULL ? m->alpha_mode : LRG_GLTF_ALPHA_MODE_OPAQUE;
}

gboolean
lrg_gltf_geometry_get_material_double_sided (LrgGltfGeometry *self,
                                             guint            material)
{
    Material *m;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), FALSE);
    m = material_at (self, material);
    return m != NULL && m->double_sided;
}

guint
lrg_gltf_geometry_get_image_count (LrgGltfGeometry *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), 0);
    return self->images->len;
}

/* sniff_mime: PNG and JPEG signatures, then the file extension. */
static const gchar *
sniff_mime (GBytes      *bytes,
            const gchar *uri)
{
    gsize         size = 0;
    const guint8 *p = g_bytes_get_data (bytes, &size);

    if (size >= 8 && memcmp (p, "\x89PNG\r\n\x1a\n", 8) == 0)
        return "image/png";
    if (size >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF)
        return "image/jpeg";
    if (uri != NULL && !g_str_has_prefix (uri, "data:"))
    {
        g_autofree gchar *lower = g_ascii_strdown (uri, -1);

        if (g_str_has_suffix (lower, ".png"))
            return "image/png";
        if (g_str_has_suffix (lower, ".jpg") || g_str_has_suffix (lower, ".jpeg"))
            return "image/jpeg";
    }
    return NULL;
}

GBytes *
lrg_gltf_geometry_get_image_data (LrgGltfGeometry  *self,
                                  guint             image,
                                  const gchar     **mime_type,
                                  GError          **error)
{
    Image             *entry;
    g_autoptr(GBytes)  bytes = NULL;
    g_autofree gchar  *uri_mime = NULL;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (mime_type != NULL)
        *mime_type = NULL;
    if (image >= self->images->len)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "image %u is out of range (0..%u)", image, self->images->len);
        return NULL;
    }
    entry = &g_array_index (self->images, Image, image);

    if (entry->view >= 0)
    {
        const View *view = &g_array_index (self->views, View, entry->view);

        bytes = g_bytes_new_from_bytes (g_ptr_array_index (self->buffers, view->buffer),
                                        view->offset, view->length);
    }
    else
    {
        bytes = resolve_uri (self, entry->uri, &uri_mime, error);
        if (bytes == NULL)
        {
            g_prefix_error (error, "image %u: ", image);
            return NULL;
        }
    }

    /* the stored type wins; a data: URI's own type next; then sniffing.
     * Interned so the (transfer none) string outlives this call. */
    if (mime_type != NULL)
    {
        if (entry->mime != NULL)
            *mime_type = g_intern_string (entry->mime);
        else if (uri_mime != NULL)
            *mime_type = g_intern_string (uri_mime);
        else
            *mime_type = sniff_mime (bytes, entry->uri);
    }
    return g_steal_pointer (&bytes);
}

GrlImage *
lrg_gltf_geometry_load_image (LrgGltfGeometry  *self,
                              guint             image,
                              GError          **error)
{
    g_autoptr(GBytes)   bytes = NULL;
    g_autoptr(GrlImage) decoded = NULL;
    const gchar        *mime = NULL;
    const gchar        *file_type;
    const guint8       *data;
    gsize               size = 0;

    g_return_val_if_fail (LRG_IS_GLTF_GEOMETRY (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    bytes = lrg_gltf_geometry_get_image_data (self, image, &mime, error);
    if (bytes == NULL)
        return NULL;
    data = g_bytes_get_data (bytes, &size);
    if (g_strcmp0 (mime, "image/jpeg") == 0)
        file_type = ".jpg";
    else if (g_strcmp0 (mime, "image/png") == 0 || mime == NULL)
        file_type = ".png";
    else
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
                     "image %u has an unsupported type %s", image, mime);
        return NULL;
    }

    if (data != NULL && size > 0)
        decoded = grl_image_new_from_memory (file_type, data, size);
    if (decoded == NULL || !grl_image_is_valid (decoded))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
                     "image %u does not decode as %s", image, file_type);
        return NULL;
    }
    return g_steal_pointer (&decoded);
}
