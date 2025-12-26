/* lrg-data-loader.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data loader implementation using yaml-glib.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "lrg-data-loader.h"
#include "lrg-registry.h"
#include "../lrg-log.h"

#include <yaml-glib.h>

struct _LrgDataLoader
{
    GObject      parent_instance;

    LrgRegistry *registry;
    gchar       *type_field_name;
    gchar      **file_extensions;
};

G_DEFINE_TYPE (LrgDataLoader, lrg_data_loader, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_REGISTRY,
    PROP_TYPE_FIELD_NAME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default file extensions for YAML files */
static const gchar *default_extensions[] = { ".yaml", ".yml", NULL };

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static gboolean
has_yaml_extension (LrgDataLoader *self,
                    const gchar   *filename)
{
    const gchar * const *ext;
    gsize                filename_len;

    filename_len = strlen (filename);

    for (ext = (const gchar * const *)self->file_extensions; *ext != NULL; ext++)
    {
        gsize ext_len = strlen (*ext);

        if (filename_len > ext_len &&
            g_ascii_strcasecmp (filename + filename_len - ext_len, *ext) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static GObject *
load_object_from_node (LrgDataLoader  *self,
                       YamlNode       *root,
                       const gchar    *source_name,
                       GError        **error)
{
    YamlMapping *mapping;
    const gchar *type_name;
    GType        type;
    GObject     *object;

    /* Root must be a mapping */
    if (yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_PARSE,
                     "%s: root node must be a mapping",
                     source_name);
        return NULL;
    }

    mapping = yaml_node_get_mapping (root);

    /* Get type field */
    type_name = yaml_mapping_get_string_member (mapping, self->type_field_name);
    if (type_name == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_TYPE,
                     "%s: missing '%s' field",
                     source_name,
                     self->type_field_name);
        return NULL;
    }

    /* Look up type in registry */
    if (self->registry == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_TYPE,
                     "%s: no registry set for type lookup",
                     source_name);
        return NULL;
    }

    type = lrg_registry_lookup (self->registry, type_name);
    if (type == G_TYPE_INVALID)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_TYPE,
                     "%s: unknown type '%s'",
                     source_name,
                     type_name);
        return NULL;
    }

    /* Deserialize using yaml-glib */
    object = yaml_gobject_deserialize (type, root);
    if (object == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_PROPERTY,
                     "%s: failed to deserialize %s",
                     source_name,
                     type_name);
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded %s from %s",
               type_name,
               source_name);

    return object;
}

static GObject *
load_typed_from_node (LrgDataLoader  *self,
                      GType           type,
                      YamlNode       *root,
                      const gchar    *source_name,
                      GError        **error)
{
    GObject *object;

    object = yaml_gobject_deserialize (type, root);
    if (object == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_PROPERTY,
                     "%s: failed to deserialize %s",
                     source_name,
                     g_type_name (type));
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Loaded %s from %s",
               g_type_name (type),
               source_name);

    return object;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_data_loader_finalize (GObject *object)
{
    LrgDataLoader *self = LRG_DATA_LOADER (object);

    g_clear_object (&self->registry);
    g_clear_pointer (&self->type_field_name, g_free);
    g_clear_pointer (&self->file_extensions, g_strfreev);

    G_OBJECT_CLASS (lrg_data_loader_parent_class)->finalize (object);
}

static void
lrg_data_loader_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgDataLoader *self = LRG_DATA_LOADER (object);

    switch (prop_id)
    {
    case PROP_REGISTRY:
        g_value_set_object (value, self->registry);
        break;
    case PROP_TYPE_FIELD_NAME:
        g_value_set_string (value, self->type_field_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_data_loader_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgDataLoader *self = LRG_DATA_LOADER (object);

    switch (prop_id)
    {
    case PROP_REGISTRY:
        lrg_data_loader_set_registry (self, g_value_get_object (value));
        break;
    case PROP_TYPE_FIELD_NAME:
        lrg_data_loader_set_type_field_name (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_data_loader_class_init (LrgDataLoaderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_data_loader_finalize;
    object_class->get_property = lrg_data_loader_get_property;
    object_class->set_property = lrg_data_loader_set_property;

    /**
     * LrgDataLoader:registry:
     *
     * The type registry used for type lookups.
     */
    properties[PROP_REGISTRY] =
        g_param_spec_object ("registry",
                             "Registry",
                             "The type registry for lookups",
                             LRG_TYPE_REGISTRY,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgDataLoader:type-field-name:
     *
     * The field name used to identify object types in YAML files.
     */
    properties[PROP_TYPE_FIELD_NAME] =
        g_param_spec_string ("type-field-name",
                             "Type Field Name",
                             "The field name for type identification",
                             "type",
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_data_loader_init (LrgDataLoader *self)
{
    self->registry = NULL;
    self->type_field_name = g_strdup ("type");
    self->file_extensions = g_strdupv ((gchar **)default_extensions);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_data_loader_new:
 *
 * Creates a new data loader.
 *
 * Returns: (transfer full): A new #LrgDataLoader
 */
LrgDataLoader *
lrg_data_loader_new (void)
{
    return g_object_new (LRG_TYPE_DATA_LOADER, NULL);
}

/* ==========================================================================
 * Public API - Registry
 * ========================================================================== */

/**
 * lrg_data_loader_set_registry:
 * @self: an #LrgDataLoader
 * @registry: (nullable): the #LrgRegistry to use for type lookups
 *
 * Sets the registry used to resolve type names in YAML files.
 */
void
lrg_data_loader_set_registry (LrgDataLoader *self,
                              LrgRegistry   *registry)
{
    g_return_if_fail (LRG_IS_DATA_LOADER (self));
    g_return_if_fail (registry == NULL || LRG_IS_REGISTRY (registry));

    if (g_set_object (&self->registry, registry))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REGISTRY]);
    }
}

/**
 * lrg_data_loader_get_registry:
 * @self: an #LrgDataLoader
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): The #LrgRegistry, or %NULL
 */
LrgRegistry *
lrg_data_loader_get_registry (LrgDataLoader *self)
{
    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);

    return self->registry;
}

/* ==========================================================================
 * Public API - Synchronous Loading
 * ========================================================================== */

/**
 * lrg_data_loader_load_file:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML file.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
GObject *
lrg_data_loader_load_file (LrgDataLoader  *self,
                           const gchar    *path,
                           GError        **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_file (parser, path, error))
    {
        return NULL;
    }

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_PARSE,
                     "%s: empty YAML file",
                     path);
        return NULL;
    }

    object = load_object_from_node (self, root, path, error);

    return object;
}

/**
 * lrg_data_loader_load_gfile:
 * @self: an #LrgDataLoader
 * @file: a #GFile to load
 * @cancellable: (nullable): a #GCancellable
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a #GFile.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
GObject *
lrg_data_loader_load_gfile (LrgDataLoader  *self,
                            GFile          *file,
                            GCancellable   *cancellable,
                            GError        **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autofree gchar     *path = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (G_IS_FILE (file), NULL);

    parser = yaml_parser_new ();
    path = g_file_get_path (file);

    if (!yaml_parser_load_from_gfile (parser, file, cancellable, error))
    {
        return NULL;
    }

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_PARSE,
                     "%s: empty YAML file",
                     path != NULL ? path : "(unknown)");
        return NULL;
    }

    object = load_object_from_node (self, root,
                                    path != NULL ? path : "(unknown)",
                                    error);

    return object;
}

/**
 * lrg_data_loader_load_data:
 * @self: an #LrgDataLoader
 * @data: the YAML string
 * @length: length of @data, or -1 if null-terminated
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML string.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
GObject *
lrg_data_loader_load_data (LrgDataLoader  *self,
                           const gchar    *data,
                           gssize          length,
                           GError        **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (data != NULL, NULL);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_data (parser, data, length, error))
    {
        return NULL;
    }

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_PARSE,
                     "empty YAML data");
        return NULL;
    }

    object = load_object_from_node (self, root, "(data)", error);

    return object;
}

/**
 * lrg_data_loader_load_typed:
 * @self: an #LrgDataLoader
 * @type: the #GType to deserialize as
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject of a specific type from a YAML file.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
GObject *
lrg_data_loader_load_typed (LrgDataLoader  *self,
                            GType           type,
                            const gchar    *path,
                            GError        **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (type != G_TYPE_INVALID, NULL);
    g_return_val_if_fail (path != NULL, NULL);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_file (parser, path, error))
    {
        return NULL;
    }

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error,
                     LRG_DATA_LOADER_ERROR,
                     LRG_DATA_LOADER_ERROR_PARSE,
                     "%s: empty YAML file",
                     path);
        return NULL;
    }

    object = load_typed_from_node (self, type, root, path, error);

    return object;
}

/* ==========================================================================
 * Public API - Batch Loading
 * ========================================================================== */

/**
 * lrg_data_loader_load_directory:
 * @self: an #LrgDataLoader
 * @directory: path to directory containing YAML files
 * @recursive: whether to recurse into subdirectories
 * @error: (nullable): return location for error
 *
 * Loads all YAML files from a directory.
 *
 * Returns: (transfer full) (element-type GObject): A #GList of loaded objects
 */
GList *
lrg_data_loader_load_directory (LrgDataLoader  *self,
                                const gchar    *directory,
                                gboolean        recursive,
                                GError        **error)
{
    g_autoptr(GFile)           dir = NULL;
    g_autoptr(GFileEnumerator) enumerator = NULL;
    GList                     *objects = NULL;
    GFileInfo                 *info;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (directory != NULL, NULL);

    dir = g_file_new_for_path (directory);
    enumerator = g_file_enumerate_children (dir,
                                            G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                            G_FILE_QUERY_INFO_NONE,
                                            NULL,
                                            error);
    if (enumerator == NULL)
    {
        return NULL;
    }

    while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
        const gchar *name = g_file_info_get_name (info);
        GFileType    ftype = g_file_info_get_file_type (info);

        if (ftype == G_FILE_TYPE_REGULAR && has_yaml_extension (self, name))
        {
            g_autoptr(GFile)   file = NULL;
            g_autoptr(GError)  load_error = NULL;
            g_autofree gchar  *path = NULL;
            GObject           *object;

            file = g_file_get_child (dir, name);
            path = g_file_get_path (file);

            object = lrg_data_loader_load_file (self, path, &load_error);
            if (object != NULL)
            {
                objects = g_list_prepend (objects, object);
            }
            else
            {
                lrg_warning (LRG_LOG_DOMAIN_CORE,
                             "Failed to load %s: %s",
                             path,
                             load_error->message);
            }
        }
        else if (ftype == G_FILE_TYPE_DIRECTORY && recursive)
        {
            g_autoptr(GError)  subdir_error = NULL;
            g_autofree gchar  *subdir_path = NULL;
            GList             *subdir_objects;

            subdir_path = g_build_filename (directory, name, NULL);
            subdir_objects = lrg_data_loader_load_directory (self,
                                                             subdir_path,
                                                             TRUE,
                                                             &subdir_error);
            if (subdir_objects != NULL)
            {
                objects = g_list_concat (objects, subdir_objects);
            }
            else if (subdir_error != NULL)
            {
                lrg_warning (LRG_LOG_DOMAIN_CORE,
                             "Failed to load directory %s: %s",
                             subdir_path,
                             subdir_error->message);
            }
        }

        g_object_unref (info);
    }

    return g_list_reverse (objects);
}

/**
 * lrg_data_loader_load_files:
 * @self: an #LrgDataLoader
 * @paths: (array zero-terminated=1): NULL-terminated array of file paths
 * @error: (nullable): return location for error
 *
 * Loads multiple YAML files.
 *
 * Returns: (transfer full) (element-type GObject): A #GList of loaded objects
 */
GList *
lrg_data_loader_load_files (LrgDataLoader   *self,
                            const gchar    **paths,
                            GError         **error)
{
    GList        *objects = NULL;
    const gchar **path;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (paths != NULL, NULL);

    for (path = paths; *path != NULL; path++)
    {
        g_autoptr(GError) load_error = NULL;
        GObject          *object;

        object = lrg_data_loader_load_file (self, *path, &load_error);
        if (object != NULL)
        {
            objects = g_list_prepend (objects, object);
        }
        else
        {
            lrg_warning (LRG_LOG_DOMAIN_CORE,
                         "Failed to load %s: %s",
                         *path,
                         load_error->message);
        }
    }

    return g_list_reverse (objects);
}

/* ==========================================================================
 * Async Loading - Private Data Structures
 * ========================================================================== */

typedef struct
{
    LrgDataLoader *loader;
    gchar         *path;
} LoadFileData;

static void
load_file_data_free (LoadFileData *data)
{
    g_clear_object (&data->loader);
    g_free (data->path);
    g_free (data);
}

typedef struct
{
    LrgDataLoader *loader;
    GFile         *file;
} LoadGFileData;

static void
load_gfile_data_free (LoadGFileData *data)
{
    g_clear_object (&data->loader);
    g_clear_object (&data->file);
    g_free (data);
}

typedef struct
{
    LrgDataLoader *loader;
    gchar         *directory;
    gboolean       recursive;
} LoadDirectoryData;

static void
load_directory_data_free (LoadDirectoryData *data)
{
    g_clear_object (&data->loader);
    g_free (data->directory);
    g_free (data);
}

/* ==========================================================================
 * Async Loading - Fiber Functions
 * ========================================================================== */

static DexFuture *
load_file_fiber (gpointer user_data)
{
    LoadFileData     *data = user_data;
    g_autoptr(GError) error = NULL;
    GObject          *object;

    object = lrg_data_loader_load_file (data->loader, data->path, &error);
    if (object == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (object);
}

static DexFuture *
load_gfile_fiber (gpointer user_data)
{
    LoadGFileData    *data = user_data;
    g_autoptr(GError) error = NULL;
    GObject          *object;

    object = lrg_data_loader_load_gfile (data->loader, data->file, NULL, &error);
    if (object == NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (object);
}

static DexFuture *
load_directory_fiber (gpointer user_data)
{
    LoadDirectoryData *data = user_data;
    g_autoptr(GError)  error = NULL;
    GList             *objects;

    objects = lrg_data_loader_load_directory (data->loader,
                                              data->directory,
                                              data->recursive,
                                              &error);
    if (error != NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_pointer (objects);
}

/* ==========================================================================
 * Public API - Async Loading
 * ========================================================================== */

/**
 * lrg_data_loader_load_file_async:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 *
 * Asynchronously loads a GObject from a YAML file.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GObject
 */
DexFuture *
lrg_data_loader_load_file_async (LrgDataLoader *self,
                                 const gchar   *path)
{
    LoadFileData *data;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    data = g_new0 (LoadFileData, 1);
    data->loader = g_object_ref (self);
    data->path = g_strdup (path);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_file_fiber,
                                data,
                                (GDestroyNotify)load_file_data_free);
}

/**
 * lrg_data_loader_load_gfile_async:
 * @self: an #LrgDataLoader
 * @file: a #GFile to load
 *
 * Asynchronously loads a GObject from a #GFile.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GObject
 */
DexFuture *
lrg_data_loader_load_gfile_async (LrgDataLoader *self,
                                  GFile         *file)
{
    LoadGFileData *data;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (G_IS_FILE (file), NULL);

    data = g_new0 (LoadGFileData, 1);
    data->loader = g_object_ref (self);
    data->file = g_object_ref (file);

    return dex_scheduler_spawn (NULL,
                                0,
                                load_gfile_fiber,
                                data,
                                (GDestroyNotify)load_gfile_data_free);
}

/**
 * lrg_data_loader_load_directory_async:
 * @self: an #LrgDataLoader
 * @directory: path to directory containing YAML files
 * @recursive: whether to recurse into subdirectories
 *
 * Asynchronously loads all YAML files from a directory.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GList
 */
DexFuture *
lrg_data_loader_load_directory_async (LrgDataLoader *self,
                                      const gchar   *directory,
                                      gboolean       recursive)
{
    LoadDirectoryData *data;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (directory != NULL, NULL);

    data = g_new0 (LoadDirectoryData, 1);
    data->loader = g_object_ref (self);
    data->directory = g_strdup (directory);
    data->recursive = recursive;

    return dex_scheduler_spawn (NULL,
                                0,
                                load_directory_fiber,
                                data,
                                (GDestroyNotify)load_directory_data_free);
}

/* ==========================================================================
 * Public API - Utility
 * ========================================================================== */

/**
 * lrg_data_loader_get_type_field_name:
 * @self: an #LrgDataLoader
 *
 * Gets the field name used to identify object types in YAML files.
 *
 * Returns: (transfer none): The type field name
 */
const gchar *
lrg_data_loader_get_type_field_name (LrgDataLoader *self)
{
    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);

    return self->type_field_name;
}

/**
 * lrg_data_loader_set_type_field_name:
 * @self: an #LrgDataLoader
 * @field_name: the field name to use
 *
 * Sets the field name used to identify object types in YAML files.
 */
void
lrg_data_loader_set_type_field_name (LrgDataLoader *self,
                                     const gchar   *field_name)
{
    g_return_if_fail (LRG_IS_DATA_LOADER (self));
    g_return_if_fail (field_name != NULL && field_name[0] != '\0');

    if (g_strcmp0 (self->type_field_name, field_name) != 0)
    {
        g_free (self->type_field_name);
        self->type_field_name = g_strdup (field_name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TYPE_FIELD_NAME]);
    }
}

/**
 * lrg_data_loader_get_file_extensions:
 * @self: an #LrgDataLoader
 *
 * Gets the file extensions recognized by directory loading.
 *
 * Returns: (transfer none) (array zero-terminated=1): The extensions array
 */
const gchar * const *
lrg_data_loader_get_file_extensions (LrgDataLoader *self)
{
    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);

    return (const gchar * const *)self->file_extensions;
}

/**
 * lrg_data_loader_set_file_extensions:
 * @self: an #LrgDataLoader
 * @extensions: (array zero-terminated=1): NULL-terminated array of extensions
 *
 * Sets the file extensions recognized by directory loading.
 */
void
lrg_data_loader_set_file_extensions (LrgDataLoader   *self,
                                     const gchar    **extensions)
{
    g_return_if_fail (LRG_IS_DATA_LOADER (self));
    g_return_if_fail (extensions != NULL);

    g_strfreev (self->file_extensions);
    self->file_extensions = g_strdupv ((gchar **)extensions);
}
