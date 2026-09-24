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
 *
 * Node transforms, parents and the first skin's joints are also kept so
 * that lrg_gltf_info_fix_animation_roots() can correct raylib 6.0's
 * multi-root skeleton animation bug (see that function).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <json-glib/json-glib.h>
#include <graylib.h>
#include <raylib.h>

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
#define ROOTS_FIXED_KEY  "lrg-gltf-info-roots-fixed"

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

/*
 * GltfNode:
 *
 * One glTF node: its local transform as a column-major 4x4 matrix built
 * exactly like cgltf_node_transform_local() (from "matrix", or from
 * translation/rotation/scale), its parent node index (-1 for none) and its
 * name.
 */
typedef struct
{
    gfloat  local[16];
    gint    parent;
    gchar  *name;
} GltfNode;

struct _LrgGltfInfo
{
    GObject    parent_instance;

    GPtrArray *meshes;       /* GltfMesh* */
    GPtrArray *animations;   /* gchar*, "" when unnamed */
    GltfNode  *nodes;        /* node_count entries */
    GArray    *joints;       /* gint node index per joint of skins[0] */
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
    guint        i;

    g_clear_pointer (&self->meshes, g_ptr_array_unref);
    g_clear_pointer (&self->animations, g_ptr_array_unref);
    g_clear_pointer (&self->joints, g_array_unref);
    for (i = 0; self->nodes != NULL && i < self->node_count; i++)
        g_free (self->nodes[i].name);
    g_clear_pointer (&self->nodes, g_free);

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
    self->joints = g_array_new (FALSE, FALSE, sizeof (gint));
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

            /* A node that is its own child would make every parent walk
             * (and the joint root fix-up) loop forever. */
            if (child == (gint)i)
            {
                g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
                             "node %d lists itself as a child", child);
                return FALSE;
            }
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

/* get_float_array:
 * Reads an optional member holding exactly @count numbers into @out.
 * Missing leaves @out untouched and sets *@present to FALSE; any other
 * type or length is an error, as cgltf rejects it too. */
static gboolean
get_float_array (JsonObject  *object,
                 const gchar *member,
                 guint        count,
                 gfloat      *out,
                 gboolean    *present,
                 GError     **error)
{
    JsonArray *array = NULL;
    guint      i;

    *present = FALSE;
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

        if (!JSON_NODE_HOLDS_VALUE (element) ||
            (json_node_get_value_type (element) != G_TYPE_INT64 &&
             json_node_get_value_type (element) != G_TYPE_DOUBLE))
        {
            g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON,
                         "\"%s\" element %u is not a number", member, i);
            return FALSE;
        }
        out[i] = json_node_get_value_type (element) == G_TYPE_INT64
            ? (gfloat)json_node_get_int (element) : (gfloat)json_node_get_double (element);
    }
    *present = TRUE;
    return TRUE;
}

/* parse_node_transform:
 * Builds the node's local column-major matrix the way
 * cgltf_node_transform_local() does: "matrix" verbatim when present,
 * otherwise T * R * S from translation (default 0), rotation (default
 * identity quaternion, x y z w) and scale (default 1). */
static gboolean
parse_node_transform (JsonObject  *node,
                      gfloat      *lm,
                      GError     **error)
{
    gfloat   t[3] = { 0.0f, 0.0f, 0.0f };
    gfloat   q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    gfloat   sc[3] = { 1.0f, 1.0f, 1.0f };
    gboolean has_matrix = FALSE;
    gboolean present = FALSE;

    if (!get_float_array (node, "matrix", 16, lm, &has_matrix, error) ||
        !get_float_array (node, "translation", 3, t, &present, error) ||
        !get_float_array (node, "rotation", 4, q, &present, error) ||
        !get_float_array (node, "scale", 3, sc, &present, error))
        return FALSE;
    if (has_matrix)
        return TRUE;

    lm[0] = (1 - 2 * q[1] * q[1] - 2 * q[2] * q[2]) * sc[0];
    lm[1] = (2 * q[0] * q[1] + 2 * q[2] * q[3]) * sc[0];
    lm[2] = (2 * q[0] * q[2] - 2 * q[1] * q[3]) * sc[0];
    lm[3] = 0.0f;

    lm[4] = (2 * q[0] * q[1] - 2 * q[2] * q[3]) * sc[1];
    lm[5] = (1 - 2 * q[0] * q[0] - 2 * q[2] * q[2]) * sc[1];
    lm[6] = (2 * q[1] * q[2] + 2 * q[0] * q[3]) * sc[1];
    lm[7] = 0.0f;

    lm[8] = (2 * q[0] * q[2] + 2 * q[1] * q[3]) * sc[2];
    lm[9] = (2 * q[1] * q[2] - 2 * q[0] * q[3]) * sc[2];
    lm[10] = (1 - 2 * q[0] * q[0] - 2 * q[1] * q[1]) * sc[2];
    lm[11] = 0.0f;

    lm[12] = t[0];
    lm[13] = t[1];
    lm[14] = t[2];
    lm[15] = 1.0f;
    return TRUE;
}

/* parse_nodes:
 * Records every node's name, local transform and parent (from the
 * "children" lists, already validated to give one parent per node), and
 * the joint list of the first skin, the only one raylib loads. */
static gboolean
parse_nodes (LrgGltfInfo *self,
             JsonArray   *nodes,
             JsonArray   *skins,
             GError     **error)
{
    guint i, j;

    self->nodes = g_new0 (GltfNode, MAX (1u, self->node_count));
    for (i = 0; i < self->node_count; i++)
        self->nodes[i].parent = -1;

    for (i = 0; i < self->node_count; i++)
    {
        JsonObject *node = array_object (nodes, i, "node", error);
        JsonArray  *children = NULL;

        if (node == NULL ||
            !get_string (node, "name", &self->nodes[i].name, error) ||
            !parse_node_transform (node, self->nodes[i].local, error) ||
            !get_array (node, "children", &children, error))
            return FALSE;
        for (j = 0; children != NULL && j < json_array_get_length (children); j++)
            self->nodes[json_array_get_int_element (children, j)].parent = (gint)i;
    }

    if (self->skin_count > 0)
    {
        JsonObject *skin = array_object (skins, 0, "skin", error);
        JsonArray  *joints = NULL;

        if (skin == NULL || !get_array (skin, "joints", &joints, error))
            return FALSE;
        for (j = 0; joints != NULL && j < json_array_get_length (joints); j++)
        {
            gint node_index = (gint)json_array_get_int_element (joints, j);

            g_array_append_val (self->joints, node_index);
        }
    }
    return TRUE;
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

    if (!parse_nodes (self, nodes, skins, error))
        return FALSE;

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

/* ------------------------------------------------------------------------ */
/* Skin joints and the raylib multi-root animation fix                       */
/* ------------------------------------------------------------------------ */

/*
 * raymath ports
 *
 * raylib 6.0's raymath.h needs C99 (loop declarations), so the few
 * functions the fix needs are ported here to gnu89 with the same
 * operation order, keeping results bit-compatible with raylib's own
 * handling of joint 0.
 */

static gfloat
v3_dot (Vector3 a,
        Vector3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vector3
v3_scale (Vector3 v,
          gfloat  s)
{
    return (Vector3){ v.x * s, v.y * s, v.z * s };
}

static Vector3
v3_sub (Vector3 a,
        Vector3 b)
{
    return (Vector3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static Vector3
v3_mul (Vector3 a,
        Vector3 b)
{
    return (Vector3){ a.x * b.x, a.y * b.y, a.z * b.z };
}

static Vector3
v3_cross (Vector3 a,
          Vector3 b)
{
    return (Vector3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

/* QuaternionMultiply(). */
static Quaternion
quat_multiply (Quaternion a,
               Quaternion b)
{
    Quaternion r;

    r.x = a.x * b.w + a.w * b.x + a.y * b.z - a.z * b.y;
    r.y = a.y * b.w + a.w * b.y + a.z * b.x - a.x * b.z;
    r.z = a.z * b.w + a.w * b.z + a.x * b.y - a.y * b.x;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return r;
}

/* Vector3RotateByQuaternion(). */
static Vector3
v3_rotate (Vector3    v,
           Quaternion q)
{
    Vector3 r;

    r.x = v.x * (q.x * q.x + q.w * q.w - q.y * q.y - q.z * q.z) +
          v.y * (2 * q.x * q.y - 2 * q.w * q.z) + v.z * (2 * q.x * q.z + 2 * q.w * q.y);
    r.y = v.x * (2 * q.w * q.z + 2 * q.x * q.y) +
          v.y * (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) +
          v.z * (-2 * q.w * q.x + 2 * q.y * q.z);
    r.z = v.x * (-2 * q.w * q.y + 2 * q.x * q.z) + v.y * (2 * q.w * q.x + 2 * q.y * q.z) +
          v.z * (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z);
    return r;
}

/* QuaternionFromMatrix(), reading only the rotation part. */
static Quaternion
quat_from_matrix (Matrix m)
{
    Quaternion r = { 0.0f, 0.0f, 0.0f, 0.0f };
    gfloat     four_w = m.m0 + m.m5 + m.m10;
    gfloat     four_x = m.m0 - m.m5 - m.m10;
    gfloat     four_y = m.m5 - m.m0 - m.m10;
    gfloat     four_z = m.m10 - m.m0 - m.m5;
    gfloat     biggest = four_w;
    gint       index = 0;
    gfloat     value;
    gfloat     mult;

    if (four_x > biggest)
    {
        biggest = four_x;
        index = 1;
    }
    if (four_y > biggest)
    {
        biggest = four_y;
        index = 2;
    }
    if (four_z > biggest)
    {
        biggest = four_z;
        index = 3;
    }
    value = sqrtf (biggest + 1.0f) * 0.5f;
    mult = 0.25f / value;

    switch (index)
    {
    case 0:
        r.w = value;
        r.x = (m.m6 - m.m9) * mult;
        r.y = (m.m8 - m.m2) * mult;
        r.z = (m.m1 - m.m4) * mult;
        break;
    case 1:
        r.x = value;
        r.w = (m.m6 - m.m9) * mult;
        r.y = (m.m1 + m.m4) * mult;
        r.z = (m.m8 + m.m2) * mult;
        break;
    case 2:
        r.y = value;
        r.w = (m.m8 - m.m2) * mult;
        r.x = (m.m1 + m.m4) * mult;
        r.z = (m.m6 + m.m9) * mult;
        break;
    default:
        r.z = value;
        r.w = (m.m1 - m.m4) * mult;
        r.x = (m.m8 + m.m2) * mult;
        r.y = (m.m6 + m.m9) * mult;
        break;
    }
    return r;
}

/* MatrixDecompose(): translation, then Gram-Schmidt on the (stabilized)
 * basis to get scale (shear discarded), a sign flip for mirrored bases,
 * and the rotation of the orthonormal basis. */
static void
matrix_decompose (Matrix     mat,
                  Transform *out)
{
    const gfloat eps = 1e-9f;
    Vector3      col[3];
    Vector3      scl = { 0.0f, 0.0f, 0.0f };
    gfloat       stabilizer = eps;
    gfloat       shear;
    Matrix       rotation;
    gint         i;

    out->translation = (Vector3){ mat.m12, mat.m13, mat.m14 };

    col[0] = (Vector3){ mat.m0, mat.m4, mat.m8 };
    col[1] = (Vector3){ mat.m1, mat.m5, mat.m9 };
    col[2] = (Vector3){ mat.m2, mat.m6, mat.m10 };

    /* Max-normalizing helps numerical stability. */
    for (i = 0; i < 3; i++)
    {
        stabilizer = fmaxf (stabilizer, fabsf (col[i].x));
        stabilizer = fmaxf (stabilizer, fabsf (col[i].y));
        stabilizer = fmaxf (stabilizer, fabsf (col[i].z));
    }
    for (i = 0; i < 3; i++)
        col[i] = v3_scale (col[i], 1.0f / stabilizer);

    scl.x = sqrtf (v3_dot (col[0], col[0]));
    if (scl.x > eps)
        col[0] = v3_scale (col[0], 1.0f / scl.x);

    shear = v3_dot (col[0], col[1]);
    col[1] = v3_sub (col[1], v3_scale (col[0], shear));
    scl.y = sqrtf (v3_dot (col[1], col[1]));
    if (scl.y > eps)
        col[1] = v3_scale (col[1], 1.0f / scl.y);

    shear = v3_dot (col[0], col[2]);
    col[2] = v3_sub (col[2], v3_scale (col[0], shear));
    shear = v3_dot (col[1], col[2]);
    col[2] = v3_sub (col[2], v3_scale (col[1], shear));
    scl.z = sqrtf (v3_dot (col[2], col[2]));
    if (scl.z > eps)
        col[2] = v3_scale (col[2], 1.0f / scl.z);

    /* Orthonormal in O(3); force SO(3) by flipping a mirrored basis. */
    if (v3_dot (col[0], v3_cross (col[1], col[2])) < 0)
    {
        scl = v3_scale (scl, -1.0f);
        for (i = 0; i < 3; i++)
            col[i] = v3_scale (col[i], -1.0f);
    }
    out->scale = v3_scale (scl, stabilizer);

    rotation = (Matrix){
        col[0].x, col[0].y, col[0].z, 0.0f,
        col[1].x, col[1].y, col[1].z, 0.0f,
        col[2].x, col[2].y, col[2].z, 0.0f,
        0.0f,     0.0f,     0.0f,     1.0f
    };
    out->rotation = quat_from_matrix (rotation);
}

/* joint_node:
 * Node index of joint @joint of the first skin, or -1 when out of range. */
static gint
joint_node (LrgGltfInfo *self,
            guint        joint)
{
    return joint < self->joints->len ? g_array_index (self->joints, gint, joint) : -1;
}

guint
lrg_gltf_info_get_joint_count (LrgGltfInfo *self)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), 0);

    return self->joints->len;
}

gint
lrg_gltf_info_get_joint_node_index (LrgGltfInfo *self,
                                    guint        joint)
{
    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), -1);

    return joint_node (self, joint);
}

const gchar *
lrg_gltf_info_get_joint_name (LrgGltfInfo *self,
                              guint        joint)
{
    gint node;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), NULL);

    node = joint_node (self, joint);
    return node >= 0 ? self->nodes[node].name : NULL;
}

gint
lrg_gltf_info_get_joint_parent (LrgGltfInfo *self,
                                guint        joint)
{
    gint  node;
    gint  parent;
    guint j;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), -1);

    /* Same search as raylib's LoadBoneInfoGLTF(): the first joint whose
     * node is this joint's parent node. */
    node = joint_node (self, joint);
    if (node < 0)
        return -1;
    parent = self->nodes[node].parent;
    if (parent < 0)
        return -1;
    for (j = 0; j < self->joints->len; j++)
        if (g_array_index (self->joints, gint, j) == parent)
            return (gint)j;
    return -1;
}

/* node_world_matrix:
 * World matrix of @node, composed up the parent chain exactly like
 * cgltf_node_transform_world() (affine, column-major). The walk is
 * bounded by the node count so a parent cycle cannot hang. */
static void
node_world_matrix (LrgGltfInfo *self,
                   gint         node,
                   gfloat      *lm)
{
    gint  parent;
    guint steps = 0;

    memcpy (lm, self->nodes[node].local, sizeof (gfloat) * 16);
    parent = self->nodes[node].parent;
    while (parent >= 0 && steps++ < self->node_count)
    {
        const gfloat *pm = self->nodes[parent].local;
        gint          i;

        for (i = 0; i < 4; ++i)
        {
            gfloat l0 = lm[i * 4 + 0];
            gfloat l1 = lm[i * 4 + 1];
            gfloat l2 = lm[i * 4 + 2];

            lm[i * 4 + 0] = l0 * pm[0] + l1 * pm[4] + l2 * pm[8];
            lm[i * 4 + 1] = l0 * pm[1] + l1 * pm[5] + l2 * pm[9];
            lm[i * 4 + 2] = l0 * pm[2] + l1 * pm[6] + l2 * pm[10];
        }
        lm[12] += pm[12];
        lm[13] += pm[13];
        lm[14] += pm[14];

        parent = self->nodes[parent].parent;
    }
}

/* joint_root_transform:
 * The decomposed world transform of joint @joint's parent node, as raylib
 * computes it for joint 0 (cgltf world matrix, then MatrixDecompose()).
 * Returns FALSE when the joint has no parent node (identity). */
static gboolean
joint_root_transform (LrgGltfInfo *self,
                      guint        joint,
                      Transform   *out)
{
    gfloat m[16];
    gint   node = joint_node (self, joint);
    Matrix world;

    out->translation = (Vector3){ 0.0f, 0.0f, 0.0f };
    out->rotation = (Quaternion){ 0.0f, 0.0f, 0.0f, 1.0f };
    out->scale = (Vector3){ 1.0f, 1.0f, 1.0f };
    if (node < 0 || self->nodes[node].parent < 0)
        return FALSE;

    node_world_matrix (self, self->nodes[node].parent, m);
    world = (Matrix){
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]
    };
    matrix_decompose (world, out);
    return TRUE;
}

gboolean
lrg_gltf_info_get_joint_root_transform (LrgGltfInfo *self,
                                        guint        joint,
                                        gfloat      *out_translation,
                                        gfloat      *out_rotation,
                                        gfloat      *out_scale)
{
    Transform transform;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), FALSE);

    if (joint >= self->joints->len)
        return FALSE;

    joint_root_transform (self, joint, &transform);
    if (out_translation != NULL)
    {
        out_translation[0] = transform.translation.x;
        out_translation[1] = transform.translation.y;
        out_translation[2] = transform.translation.z;
    }
    if (out_rotation != NULL)
    {
        out_rotation[0] = transform.rotation.x;
        out_rotation[1] = transform.rotation.y;
        out_rotation[2] = transform.rotation.z;
        out_rotation[3] = transform.rotation.w;
    }
    if (out_scale != NULL)
    {
        out_scale[0] = transform.scale.x;
        out_scale[1] = transform.scale.y;
        out_scale[2] = transform.scale.z;
    }
    return TRUE;
}

/* effective_root:
 * The bone whose transform raylib's BuildPoseFromParentJoints() actually
 * rooted bone @bone's model-space pose at. raylib composes a bone with its
 * parent only when the parent index is smaller; a larger index breaks the
 * chain (the bone keeps its local pose). Returns the root bone when the
 * chain ends at a bone without a parent joint, or -1 when it breaks. */
static gint
effective_root (const gint *parents,
                gint        bone)
{
    /* Each step moves to a strictly smaller index, so this terminates;
     * a bone that is its own parent breaks the chain like a larger one. */
    while (parents[bone] >= 0)
    {
        if (parents[bone] >= bone)
            return -1;
        bone = parents[bone];
    }
    return bone;
}

guint
lrg_gltf_info_fix_animation_roots (LrgGltfInfo *self,
                                   GPtrArray   *clips)
{
    g_autofree gint      *parents = NULL;
    g_autofree gint      *roots = NULL;
    g_autofree Transform *root_world = NULL;
    g_autofree gboolean  *needs_fix = NULL;
    GQuark                fixed_quark;
    gboolean              any = FALSE;
    guint                 n_bones;
    guint                 patched = 0;
    guint                 b;
    guint                 c;

    g_return_val_if_fail (LRG_IS_GLTF_INFO (self), 0);
    g_return_val_if_fail (clips != NULL, 0);

    fixed_quark = g_quark_from_static_string (ROOTS_FIXED_KEY);
    n_bones = self->joints->len;
    if (n_bones == 0)
        return 0;

    /* Per bone: raylib's parent index, the effective root it was posed
     * under and, per root, the world transform of that root joint's
     * parent node. Root 0 already received its transform from raylib;
     * every other parentless root with a parent node needs it. */
    parents = g_new (gint, n_bones);
    roots = g_new (gint, n_bones);
    root_world = g_new (Transform, n_bones);
    needs_fix = g_new0 (gboolean, n_bones);
    for (b = 0; b < n_bones; b++)
        parents[b] = lrg_gltf_info_get_joint_parent (self, b);
    for (b = 0; b < n_bones; b++)
    {
        roots[b] = effective_root (parents, (gint)b);
        if (b != 0 && parents[b] < 0)
            needs_fix[b] = joint_root_transform (self, b, &root_world[b]);
        any = any || needs_fix[b];
    }

    for (c = 0; c < clips->len; c++)
    {
        GrlModelAnimation *clip = g_ptr_array_index (clips, c);
        ModelAnimation    *anim;
        gboolean           changed = FALSE;
        gint               frame;

        if (!GRL_IS_MODEL_ANIMATION (clip) ||
            g_object_get_qdata (G_OBJECT (clip), fixed_quark) != NULL)
            continue;
        anim = grl_model_animation_get_handle (clip);
        if (anim == NULL || anim->boneCount != (gint)n_bones || anim->keyframePoses == NULL)
            continue;

        /* raylib has already turned the keyframes into model-space poses
         * with BuildPoseFromParentJoints(), so left-compose the missing
         * root transform W onto every bone of each affected subtree:
         * rot = W.rot * rot, t = W.rot * (W.scale . t) + W.t,
         * scale = W.scale . scale. */
        for (frame = 0; any && frame < anim->keyframeCount; frame++)
        {
            Transform *pose = anim->keyframePoses[frame];

            for (b = 0; pose != NULL && b < n_bones; b++)
            {
                const Transform *w;
                Vector3          t;

                if (roots[b] < 0 || !needs_fix[roots[b]])
                    continue;
                w = &root_world[roots[b]];
                t = v3_rotate (v3_mul (pose[b].translation, w->scale), w->rotation);
                pose[b].rotation = quat_multiply (w->rotation, pose[b].rotation);
                pose[b].translation = (Vector3){ t.x + w->translation.x,
                                                 t.y + w->translation.y,
                                                 t.z + w->translation.z };
                pose[b].scale = v3_mul (pose[b].scale, w->scale);
                changed = TRUE;
            }
        }

        g_object_set_qdata (G_OBJECT (clip), fixed_quark, GINT_TO_POINTER (1));
        if (changed)
            patched++;
    }
    return patched;
}
