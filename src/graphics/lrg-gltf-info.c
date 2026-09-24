/* lrg-gltf-info.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * glTF inspection with json-glib. The mesh list mirrors raylib 6.0
 * rmodels.c LoadGLTF(): it walks data->nodes in array order (scenes are
 * ignored), and for every node with a mesh emits one raylib Mesh per
 * primitive whose type is triangles, in primitive order.
 */

#include "config.h"

#include <string.h>
#include <json-glib/json-glib.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-gltf-info.h"

#define GLB_MAGIC        (0x46546C67u)  /* "glTF" */
#define GLB_CHUNK_JSON   (0x4E4F534Au)  /* "JSON" */
#define GLB_HEADER_SIZE  (12)
#define GLB_CHUNK_HEADER (8)
#define GLTF_MODE_TRIANGLES (4)
#define DRACO_EXTENSION  "KHR_draco_mesh_compression"

/*
 * GltfMesh:
 *
 * One raylib mesh (one triangle primitive reached through one node).
 */
typedef struct
{
    gchar *name;          /* display name */
    gchar *source_name;   /* glTF mesh name, nullable */
    gchar *node_name;     /* referencing node name, nullable */
    gint   node;
    gint   source;
    gint   primitive;
    guint  vertex_count;
    gint   material;
} GltfMesh;

struct _LrgGltfInfo
{
    GObject    parent_instance;

    GPtrArray *meshes;       /* GltfMesh* */
    GPtrArray *animations;   /* gchar*, "" when unnamed */
    guint      node_count;
    guint      skin_count;
    gboolean   uses_draco;
    gboolean   is_binary;
};

G_DEFINE_TYPE (LrgGltfInfo, lrg_gltf_info, G_TYPE_OBJECT)

static void
gltf_mesh_free (gpointer data)
{
    GltfMesh *mesh = data;

    g_free (mesh->name);
    g_free (mesh->source_name);
    g_free (mesh->node_name);
    g_free (mesh);
}

static void
lrg_gltf_info_finalize (GObject *object)
{
    LrgGltfInfo *self = LRG_GLTF_INFO (object);

    g_clear_pointer (&self->meshes, g_ptr_array_unref);
    g_clear_pointer (&self->animations, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_gltf_info_parent_class)->finalize (object);
}

static void
lrg_gltf_info_class_init (LrgGltfInfoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_gltf_info_finalize;
}

static void
lrg_gltf_info_init (LrgGltfInfo *self)
{
    self->meshes = g_ptr_array_new_with_free_func (gltf_mesh_free);
    self->animations = g_ptr_array_new_with_free_func (g_free);
}

/* ------------------------------------------------------------------------ */
/* Container                                                                */
/* ------------------------------------------------------------------------ */

static guint32
read_u32 (const guint8 *p)
{
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

/* locate_json:
 * Finds the JSON text inside a GLB container or a plain .gltf file. GLB
 * checks follow cgltf: the header must fit, the declared length may not
 * exceed the data, the first chunk must be JSON and must fit. */
static gboolean
locate_json (const guint8  *data,
             gsize          size,
             const gchar  **json,
             gsize         *json_length,
             gboolean      *is_binary,
             GError       **error)
{
    guint32 declared;
    guint32 chunk_length;
    gsize   i;

    if (size >= 4 && read_u32 (data) == GLB_MAGIC)
    {
        if (size < GLB_HEADER_SIZE + GLB_CHUNK_HEADER)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                         "GLB data is truncated (%" G_GSIZE_FORMAT " bytes)", size);
            return FALSE;
        }
        if (read_u32 (data + 4) != 2)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED_VERSION,
                         "Unsupported GLB version %u", read_u32 (data + 4));
            return FALSE;
        }
        declared = read_u32 (data + 8);
        if (declared > size || declared < GLB_HEADER_SIZE + GLB_CHUNK_HEADER)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                         "GLB declares %u bytes but %" G_GSIZE_FORMAT " are available",
                         declared, size);
            return FALSE;
        }
        chunk_length = read_u32 (data + GLB_HEADER_SIZE);
        if (read_u32 (data + GLB_HEADER_SIZE + 4) != GLB_CHUNK_JSON)
        {
            g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                                 "First GLB chunk is not JSON");
            return FALSE;
        }
        if ((gsize)chunk_length > (gsize)declared - GLB_HEADER_SIZE - GLB_CHUNK_HEADER)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                         "GLB JSON chunk (%u bytes) overruns the container", chunk_length);
            return FALSE;
        }

        *json = (const gchar *)data + GLB_HEADER_SIZE + GLB_CHUNK_HEADER;
        /* Stop at an embedded NUL; padding is spaces by spec. */
        *json_length = strnlen (*json, chunk_length);
        *is_binary = TRUE;
        return TRUE;
    }

    /* Plain glTF: JSON text, optionally after a UTF-8 BOM and whitespace. */
    i = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        i = 3;
    while (i < size && g_ascii_isspace (data[i]))
        i++;
    if (i >= size || data[i] != '{')
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                             "Not a glTF file (no GLB magic and no JSON object)");
        return FALSE;
    }
    *json = (const gchar *)data + i;
    *json_length = strnlen (*json, size - i);
    *is_binary = FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* JSON helpers                                                             */
/* ------------------------------------------------------------------------ */

/* get_array:
 * Optional top-level or member array. Missing is fine (empty); present
 * with another type is an error. */
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

/* get_index:
 * Reads an optional non-negative integer index member and checks it
 * against @limit. Integral floating-point values are accepted, as cgltf
 * accepts them. */
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
    if (!JSON_NODE_HOLDS_VALUE (node) ||
        (json_node_get_value_type (node) != G_TYPE_INT64 &&
         json_node_get_value_type (node) != G_TYPE_DOUBLE))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "\"%s\" is not an index", member);
        return FALSE;
    }
    value = json_node_get_value_type (node) == G_TYPE_INT64
        ? (gdouble)json_node_get_int (node) : json_node_get_double (node);
    if (value < 0 || value >= (gdouble)limit || value != (gdouble)(gint64)value)
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                     "\"%s\" index %g is out of range (0..%u)", member, value, limit);
        return FALSE;
    }
    *out = (gint)value;
    return TRUE;
}

/* get_string:
 * Optional string member; returns a copy or NULL. A non-string is an
 * error. */
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

static JsonObject *
array_object (JsonArray   *array,
              guint        index,
              const gchar *what,
              GError     **error)
{
    JsonNode *node = json_array_get_element (array, index);

    if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
        g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                     "%s %u is not an object", what, index);
        return NULL;
    }
    return json_node_get_object (node);
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
/* Document walk                                                            */
/* ------------------------------------------------------------------------ */

/* add_node_meshes:
 * Emits the raylib meshes of one node: every triangle primitive of its
 * mesh in primitive order. */
static gboolean
add_node_meshes (LrgGltfInfo *self,
                 JsonObject  *node,
                 guint        node_index,
                 JsonArray   *meshes,
                 JsonArray   *accessors,
                 guint        n_materials,
                 GError     **error)
{
    g_autofree gchar *node_name = NULL;
    g_autofree gchar *mesh_name = NULL;
    JsonObject       *mesh;
    JsonArray        *primitives = NULL;
    gint              mesh_index;
    guint             n_primitives;
    guint             p;

    if (!get_index (node, "mesh", meshes != NULL ? json_array_get_length (meshes) : 0,
                    &mesh_index, error))
        return FALSE;
    if (mesh_index < 0)
        return TRUE;
    if (!get_string (node, "name", &node_name, error))
        return FALSE;

    mesh = array_object (meshes, (guint)mesh_index, "mesh", error);
    if (mesh == NULL || !get_string (mesh, "name", &mesh_name, error) ||
        !get_array (mesh, "primitives", &primitives, error))
        return FALSE;
    n_primitives = primitives != NULL ? json_array_get_length (primitives) : 0;

    for (p = 0; p < n_primitives; p++)
    {
        JsonObject *primitive;
        JsonNode   *attributes_node;
        JsonNode   *extensions_node;
        GltfMesh   *entry;
        gint        mode;
        gint        position = -1;
        gint        material;

        primitive = array_object (primitives, p, "primitive", error);
        if (primitive == NULL)
            return FALSE;

        /* Draco-compressed primitives make raylib reject the whole file,
         * but are still listed here. */
        extensions_node = json_object_get_member (primitive, "extensions");
        if (extensions_node != NULL && JSON_NODE_HOLDS_OBJECT (extensions_node) &&
            json_object_has_member (json_node_get_object (extensions_node), DRACO_EXTENSION))
            self->uses_draco = TRUE;

        /* cgltf maps mode 4 (the default) to triangles; raylib skips the
         * rest. Any integer is allowed here, only 4 is kept. */
        if (!get_index (primitive, "mode", G_MAXINT, &mode, error))
            return FALSE;
        if (mode >= 0 && mode != GLTF_MODE_TRIANGLES)
            continue;

        attributes_node = json_object_get_member (primitive, "attributes");
        if (attributes_node != NULL && JSON_NODE_HOLDS_OBJECT (attributes_node) &&
            !get_index (json_node_get_object (attributes_node), "POSITION",
                        accessors != NULL ? json_array_get_length (accessors) : 0,
                        &position, error))
            return FALSE;
        if (!get_index (primitive, "material", n_materials, &material, error))
            return FALSE;

        entry = g_new0 (GltfMesh, 1);
        entry->node = (gint)node_index;
        entry->source = mesh_index;
        entry->primitive = (gint)p;
        entry->material = material;
        entry->node_name = g_strdup (node_name);
        entry->source_name = g_strdup (mesh_name);
        if (mesh_name != NULL && n_primitives == 1)
            entry->name = g_strdup (mesh_name);
        else if (mesh_name != NULL)
            entry->name = g_strdup_printf ("%s#%u", mesh_name, p);
        else if (n_primitives == 1)
            entry->name = g_strdup_printf ("mesh%d", mesh_index);
        else
            entry->name = g_strdup_printf ("mesh%d#%u", mesh_index, p);

        if (position >= 0)
        {
            JsonObject *accessor = array_object (accessors, (guint)position, "accessor", error);
            JsonNode   *count;

            if (accessor == NULL)
            {
                gltf_mesh_free (entry);
                return FALSE;
            }
            count = json_object_get_member (accessor, "count");
            if (count != NULL && JSON_NODE_HOLDS_VALUE (count) &&
                json_node_get_value_type (count) == G_TYPE_INT64 &&
                json_node_get_int (count) >= 0 && json_node_get_int (count) <= G_MAXUINT)
                entry->vertex_count = (guint)json_node_get_int (count);
        }

        g_ptr_array_add (self->meshes, entry);
    }
    return TRUE;
}

/* check_index_list:
 * Every element of an optional integer array member must index into
 * [0, limit). */
static gboolean
check_index_list (JsonObject  *object,
                  const gchar *member,
                  guint        limit,
                  GError     **error)
{
    JsonArray *array = NULL;
    guint      i;

    if (!get_array (object, member, &array, error))
        return FALSE;
    for (i = 0; array != NULL && i < json_array_get_length (array); i++)
    {
        g_autoptr(JsonObject) wrapper = json_object_new ();
        gint                  index;

        json_object_set_member (wrapper, member, json_node_copy (json_array_get_element (array, i)));
        if (!get_index (wrapper, member, limit, &index, error))
            return FALSE;
    }
    return TRUE;
}

/* check_count:
 * Length of an optional top-level array (0 when absent or not an array;
 * type errors are reported by parse_document()). */
static guint
check_count (JsonObject  *root,
             const gchar *member)
{
    JsonNode *node = json_object_get_member (root, member);

    return node != NULL && JSON_NODE_HOLDS_ARRAY (node)
        ? json_array_get_length (json_node_get_array (node)) : 0;
}

/* validate_structure:
 * Mirrors the cgltf pointer fix-ups that make cgltf_parse() (and so
 * raylib) reject a file: out-of-range references among meshes,
 * accessors, buffer views, skins, nodes and scenes, a node with two
 * parents, and a scene root that has a parent. Not every cgltf check is
 * reproduced (materials, textures and extensions are not inspected). */
static gboolean
validate_structure (JsonObject  *root,
                    GError     **error)
{
    guint              n_nodes = check_count (root, "nodes");
    guint              n_meshes = check_count (root, "meshes");
    guint              n_accessors = check_count (root, "accessors");
    guint              n_views = check_count (root, "bufferViews");
    guint              n_buffers = check_count (root, "buffers");
    guint              n_skins = check_count (root, "skins");
    guint              n_scenes = check_count (root, "scenes");
    guint              n_materials = check_count (root, "materials");
    g_autofree gint   *parent = NULL;
    JsonArray         *array = NULL;
    guint              i, j;
    gint               index;

    /* Accessors -> buffer views -> buffers. */
    if (!get_array (root, "accessors", &array, error))
        return FALSE;
    for (i = 0; i < n_accessors; i++)
    {
        JsonObject *accessor = array_object (array, i, "accessor", error);

        if (accessor == NULL || !get_index (accessor, "bufferView", n_views, &index, error))
            return FALSE;
    }
    if (!get_array (root, "bufferViews", &array, error))
        return FALSE;
    for (i = 0; i < n_views; i++)
    {
        JsonObject *view = array_object (array, i, "bufferView", error);

        if (view == NULL || !get_index (view, "buffer", n_buffers, &index, error))
            return FALSE;
        if (index < 0)
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                         "bufferView %u has no buffer", i);
            return FALSE;
        }
    }

    /* Mesh primitives: indices, material and every attribute accessor. */
    if (!get_array (root, "meshes", &array, error))
        return FALSE;
    for (i = 0; i < n_meshes; i++)
    {
        JsonObject *mesh = array_object (array, i, "mesh", error);
        JsonArray  *primitives = NULL;

        if (mesh == NULL || !get_array (mesh, "primitives", &primitives, error))
            return FALSE;
        for (j = 0; primitives != NULL && j < json_array_get_length (primitives); j++)
        {
            JsonObject *primitive = array_object (primitives, j, "primitive", error);
            JsonNode   *attributes;

            if (primitive == NULL ||
                !get_index (primitive, "indices", n_accessors, &index, error) ||
                !get_index (primitive, "material", n_materials, &index, error))
                return FALSE;
            attributes = json_object_get_member (primitive, "attributes");
            if (attributes != NULL && JSON_NODE_HOLDS_OBJECT (attributes))
            {
                JsonObject *object = json_node_get_object (attributes);
                GList      *names = json_object_get_members (object);
                GList      *l;
                gboolean    ok = TRUE;

                for (l = names; l != NULL && ok; l = l->next)
                    ok = get_index (object, l->data, n_accessors, &index, error);
                g_list_free (names);
                if (!ok)
                    return FALSE;
            }
        }
    }

    /* Skins. */
    if (!get_array (root, "skins", &array, error))
        return FALSE;
    for (i = 0; i < n_skins; i++)
    {
        JsonObject *skin = array_object (array, i, "skin", error);

        if (skin == NULL || !check_index_list (skin, "joints", n_nodes, error) ||
            !get_index (skin, "skeleton", n_nodes, &index, error) ||
            !get_index (skin, "inverseBindMatrices", n_accessors, &index, error))
            return FALSE;
    }

    /* Nodes: references and a single parent per node. */
    parent = g_new (gint, MAX (1u, n_nodes));
    for (i = 0; i < n_nodes; i++)
        parent[i] = -1;
    if (!get_array (root, "nodes", &array, error))
        return FALSE;
    for (i = 0; i < n_nodes; i++)
    {
        JsonObject *node = array_object (array, i, "node", error);
        JsonArray  *children = NULL;

        if (node == NULL || !check_index_list (node, "children", n_nodes, error) ||
            !get_index (node, "mesh", n_meshes, &index, error) ||
            !get_index (node, "skin", n_skins, &index, error) ||
            !get_array (node, "children", &children, error))
            return FALSE;
        for (j = 0; children != NULL && j < json_array_get_length (children); j++)
        {
            gint child = (gint)json_array_get_int_element (children, j);

            if (parent[child] >= 0)
            {
                g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                             "node %d has more than one parent", child);
                return FALSE;
            }
            parent[child] = (gint)i;
        }
    }

    /* Scenes: root nodes may not have a parent. */
    if (!get_array (root, "scenes", &array, error))
        return FALSE;
    for (i = 0; i < n_scenes; i++)
    {
        JsonObject *scene = array_object (array, i, "scene", error);
        JsonArray  *roots = NULL;

        if (scene == NULL || !check_index_list (scene, "nodes", n_nodes, error) ||
            !get_array (scene, "nodes", &roots, error))
            return FALSE;
        for (j = 0; roots != NULL && j < json_array_get_length (roots); j++)
        {
            gint root_node = (gint)json_array_get_int_element (roots, j);

            if (parent[root_node] >= 0)
            {
                g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                             "scene %u lists node %d, which has a parent", i, root_node);
                return FALSE;
            }
        }
    }
    return get_index (root, "scene", n_scenes, &index, error);
}

static gboolean
parse_document (LrgGltfInfo *self,
                JsonObject  *root,
                GError     **error)
{
    JsonArray *nodes = NULL;
    JsonArray *meshes = NULL;
    JsonArray *accessors = NULL;
    JsonArray *materials = NULL;
    JsonArray *skins = NULL;
    JsonArray *animations = NULL;
    guint      i;

    if (!validate_structure (root, error) ||
        !get_array (root, "nodes", &nodes, error) ||
        !get_array (root, "meshes", &meshes, error) ||
        !get_array (root, "accessors", &accessors, error) ||
        !get_array (root, "materials", &materials, error) ||
        !get_array (root, "skins", &skins, error) ||
        !get_array (root, "animations", &animations, error))
        return FALSE;

    if (string_array_contains (root, "extensionsUsed", DRACO_EXTENSION) ||
        string_array_contains (root, "extensionsRequired", DRACO_EXTENSION))
        self->uses_draco = TRUE;

    self->node_count = nodes != NULL ? json_array_get_length (nodes) : 0;
    self->skin_count = skins != NULL ? json_array_get_length (skins) : 0;

    /* raylib visits every node in array order, ignoring scenes. */
    for (i = 0; i < self->node_count; i++)
    {
        JsonObject *node = array_object (nodes, i, "node", error);

        if (node == NULL ||
            !add_node_meshes (self, node, i, meshes, accessors,
                              materials != NULL ? json_array_get_length (materials) : 0,
                              error))
            return FALSE;
    }

    for (i = 0; animations != NULL && i < json_array_get_length (animations); i++)
    {
        JsonObject *animation = array_object (animations, i, "animation", error);
        gchar      *name = NULL;

        if (animation == NULL || !get_string (animation, "name", &name, error))
            return FALSE;
        g_ptr_array_add (self->animations, name != NULL ? name : g_strdup (""));
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

LrgGltfInfo *
lrg_gltf_info_new_from_bytes (GBytes  *data,
                              GError **error)
{
    g_autoptr(LrgGltfInfo) self = NULL;
    g_autoptr(JsonParser)  parser = NULL;
    g_autoptr(GError)      parse_error = NULL;
    const guint8          *bytes;
    gsize                  size = 0;
    const gchar           *json = NULL;
    gsize                  json_length = 0;
    gboolean               is_binary = FALSE;
    JsonNode              *root;

    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    bytes = g_bytes_get_data (data, &size);
    if (bytes == NULL || size == 0)
    {
        g_set_error_literal (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER,
                             "Empty glTF data");
        return NULL;
    }
    if (!locate_json (bytes, size, &json, &json_length, &is_binary, error))
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

    self = g_object_new (LRG_TYPE_GLTF_INFO, NULL);
    self->is_binary = is_binary;
    if (!parse_document (self, json_node_get_object (root), error))
        return NULL;
    return g_steal_pointer (&self);
}

LrgGltfInfo *
lrg_gltf_info_new_from_file (const gchar  *path,
                             GError      **error)
{
    g_autoptr(GBytes) bytes = NULL;
    gchar            *contents = NULL;
    gsize             length = 0;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_file_get_contents (path, &contents, &length, error))
        return NULL;
    bytes = g_bytes_new_take (contents, length);
    return lrg_gltf_info_new_from_bytes (bytes, error);
}

gchar **
lrg_gltf_read_mesh_names (const gchar  *path,
                          GError      **error)
{
    g_autoptr(LrgGltfInfo) info = NULL;
    GStrvBuilder          *builder;
    gchar                **names;
    guint                  i;

    info = lrg_gltf_info_new_from_file (path, error);
    if (info == NULL)
        return NULL;

    builder = g_strv_builder_new ();
    for (i = 0; i < info->meshes->len; i++)
        g_strv_builder_add (builder, ((GltfMesh *)g_ptr_array_index (info->meshes, i))->name);
    names = g_strv_builder_end (builder);
    g_strv_builder_unref (builder);
    return names;
}

static GltfMesh *
mesh_at (LrgGltfInfo *self,
         guint        index)
{
    return index < self->meshes->len ? g_ptr_array_index (self->meshes, index) : NULL;
}

guint
lrg_gltf_info_get_mesh_count (LrgGltfInfo *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), 0);

    return self->meshes->len;
}

const gchar *
lrg_gltf_info_get_mesh_name (LrgGltfInfo *self,
                             guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), NULL);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->name : NULL;
}

const gchar *
lrg_gltf_info_get_mesh_source_name (LrgGltfInfo *self,
                                    guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), NULL);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->source_name : NULL;
}

const gchar *
lrg_gltf_info_get_mesh_node_name (LrgGltfInfo *self,
                                  guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), NULL);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->node_name : NULL;
}

gint
lrg_gltf_info_get_mesh_node_index (LrgGltfInfo *self,
                                   guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), -1);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->node : -1;
}

gint
lrg_gltf_info_get_mesh_source_index (LrgGltfInfo *self,
                                     guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), -1);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->source : -1;
}

gint
lrg_gltf_info_get_mesh_primitive (LrgGltfInfo *self,
                                  guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), -1);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->primitive : -1;
}

guint
lrg_gltf_info_get_mesh_vertex_count (LrgGltfInfo *self,
                                     guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), 0);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->vertex_count : 0;
}

gint
lrg_gltf_info_get_mesh_material (LrgGltfInfo *self,
                                 guint        index)
{
    GltfMesh *mesh;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), -1);

    mesh = mesh_at (self, index);
    return mesh != NULL ? mesh->material : -1;
}

gint
lrg_gltf_info_find_mesh_by_node (LrgGltfInfo *self,
                                 const gchar *node_name)
{
    guint i;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), -1);
    g_return_val_if_fail (node_name != NULL, -1);

    for (i = 0; i < self->meshes->len; i++)
        if (g_strcmp0 (((GltfMesh *)g_ptr_array_index (self->meshes, i))->node_name, node_name) == 0)
            return (gint)i;
    return -1;
}

GBytes *
lrg_gltf_info_build_node_mask (LrgGltfInfo        *self,
                               const gchar *const *hidden_nodes)
{
    guint8 *mask;
    guint   i;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), NULL);

    mask = g_malloc (MAX (1u, self->meshes->len));
    for (i = 0; i < self->meshes->len; i++)
    {
        const gchar *node_name = ((GltfMesh *)g_ptr_array_index (self->meshes, i))->node_name;

        mask[i] = (node_name != NULL && hidden_nodes != NULL &&
                   g_strv_contains (hidden_nodes, node_name)) ? 0 : 1;
    }
    return g_bytes_new_take (mask, self->meshes->len);
}

guint
lrg_gltf_info_get_node_count (LrgGltfInfo *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), 0);

    return self->node_count;
}

guint
lrg_gltf_info_get_skin_count (LrgGltfInfo *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), 0);

    return self->skin_count;
}

guint
lrg_gltf_info_get_animation_count (LrgGltfInfo *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), 0);

    return self->animations->len;
}

const gchar *
lrg_gltf_info_get_animation_name (LrgGltfInfo *self,
                                  guint        index)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), NULL);

    return index < self->animations->len ? g_ptr_array_index (self->animations, index) : NULL;
}

gchar *
lrg_gltf_info_get_animation_raylib_name (LrgGltfInfo *self,
                                         guint        index)
{
    const gchar *name;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), NULL);

    name = lrg_gltf_info_get_animation_name (self, index);
    return name != NULL ? g_strndup (name, LRG_GLTF_RAYLIB_NAME_MAX) : NULL;
}

gboolean
lrg_gltf_info_get_uses_draco (LrgGltfInfo *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), FALSE);

    return self->uses_draco;
}

gboolean
lrg_gltf_info_get_is_binary (LrgGltfInfo *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), FALSE);

    return self->is_binary;
}
