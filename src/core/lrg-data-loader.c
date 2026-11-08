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
#include <yaml.h>
#include <errno.h>
#include <math.h>
#include <string.h>

struct _LrgDataLoader
{
    GObject      parent_instance;

    LrgRegistry *registry;
    gchar       *type_field_name;
    gchar      **file_extensions;

    /* composite documents */
    gboolean            strict;
    gchar              *children_key;
    LrgDataResolveFunc  resolve_func;
    gpointer            resolve_data;
    GDestroyNotify      resolve_destroy;
};

G_DEFINE_TYPE (LrgDataLoader, lrg_data_loader, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_REGISTRY,
    PROP_TYPE_FIELD_NAME,
    PROP_STRICT,
    PROP_CHILDREN_KEY,
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
    g_clear_pointer (&self->children_key, g_free);
    if (self->resolve_destroy != NULL)
        self->resolve_destroy (self->resolve_data);
    self->resolve_func = NULL;
    self->resolve_data = NULL;
    self->resolve_destroy = NULL;

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
    case PROP_STRICT:
        g_value_set_boolean (value, self->strict);
        break;
    case PROP_CHILDREN_KEY:
        g_value_set_string (value, self->children_key);
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
    case PROP_STRICT:
        lrg_data_loader_set_strict (self, g_value_get_boolean (value));
        break;
    case PROP_CHILDREN_KEY:
        lrg_data_loader_set_children_key (self, g_value_get_string (value));
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

    /**
     * LrgDataLoader:strict:
     *
     * Whether composite loading rejects unknown keys (%TRUE, the default)
     * or logs and skips them. Wrong values, unknown types and unresolved
     * references are errors either way.
     */
    properties[PROP_STRICT] =
        g_param_spec_boolean ("strict",
                              "Strict",
                              "Whether composite loading rejects unknown keys",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgDataLoader:children-key:
     *
     * The key holding an #LrgDataContainer's ordered child list in
     * composite documents.
     */
    properties[PROP_CHILDREN_KEY] =
        g_param_spec_string ("children-key",
                             "Children Key",
                             "The key holding a container's child list",
                             "children",
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
    self->strict = TRUE;
    self->children_key = g_strdup ("children");
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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
    g_autoptr(GError) parse_error = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_file (parser, path, &parse_error))
    {
        g_propagate_error (error, g_steal_pointer (&parse_error));
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

/* Strict validation is opt-in: existing load APIs retain permissive behavior. */
static gboolean
validate_property_node (GParamSpec *pspec,
                        YamlNode   *node)
{
    GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);
    YamlNodeType node_type = yaml_node_get_node_type (node);
    GValue value = G_VALUE_INIT;
    const gchar *text;
    gchar *end = NULL;
    gboolean valid = FALSE;
    gint64 integer;
    gdouble number;

    if (type == YAML_TYPE_NODE)
        return TRUE;
    if (node_type == YAML_NODE_NULL)
        return type == G_TYPE_STRING || type == G_TYPE_STRV;
    if (type == G_TYPE_STRV)
    {
        YamlSequence *sequence;
        guint i;

        if (node_type != YAML_NODE_SEQUENCE)
            return FALSE;
        sequence = yaml_node_get_sequence (node);
        for (i = 0; i < yaml_sequence_get_length (sequence); i++)
        {
            if (yaml_node_get_node_type (yaml_sequence_get_element (sequence, i)) != YAML_NODE_SCALAR)
                return FALSE;
        }
        return TRUE;
    }
    if (G_TYPE_IS_FLAGS (type))
    {
        GFlagsClass *klass = g_type_class_ref (type);
        guint flags = 0;

        if (node_type == YAML_NODE_SEQUENCE)
        {
            YamlSequence *sequence = yaml_node_get_sequence (node);
            guint i;

            valid = TRUE;
            for (i = 0; i < yaml_sequence_get_length (sequence); i++)
            {
                YamlNode *element = yaml_sequence_get_element (sequence, i);
                GFlagsValue *flag;

                if (yaml_node_get_node_type (element) != YAML_NODE_SCALAR)
                {
                    valid = FALSE;
                    break;
                }
                text = yaml_node_get_scalar (element);
                flag = g_flags_get_value_by_nick (klass, text);
                if (flag == NULL)
                    flag = g_flags_get_value_by_name (klass, text);
                if (flag == NULL)
                {
                    valid = FALSE;
                    break;
                }
            }
        }
        else if (node_type == YAML_NODE_SCALAR)
        {
            text = yaml_node_get_scalar (node);
            errno = 0;
            integer = g_ascii_strtoll (text, &end, 10);
            flags = (guint)integer;
            valid = errno == 0 && end != text && *end == '\0' &&
                    integer >= 0 && integer <= G_MAXUINT && (flags & ~klass->mask) == 0;
        }
        g_type_class_unref (klass);
        return valid;
    }
    if (node_type != YAML_NODE_SCALAR)
        return FALSE;
    text = yaml_node_get_scalar (node);
    if (type == G_TYPE_STRING)
        return TRUE;
    if (type == G_TYPE_BOOLEAN)
        return g_ascii_strcasecmp (text, "true") == 0 ||
               g_ascii_strcasecmp (text, "false") == 0;
    if (G_TYPE_IS_ENUM (type))
    {
        GEnumClass *klass = g_type_class_ref (type);
        GEnumValue *entry = g_enum_get_value_by_nick (klass, text);

        if (entry == NULL)
            entry = g_enum_get_value_by_name (klass, text);
        if (entry == NULL)
        {
            errno = 0;
            integer = g_ascii_strtoll (text, &end, 10);
            if (errno == 0 && end != text && *end == '\0' &&
                integer >= G_MININT && integer <= G_MAXINT)
                entry = g_enum_get_value (klass, (gint)integer);
        }
        valid = entry != NULL;
        g_type_class_unref (klass);
        return valid;
    }
    g_value_init (&value, type);
    if (type == G_TYPE_FLOAT || type == G_TYPE_DOUBLE)
    {
        errno = 0;
        number = g_ascii_strtod (text, &end);
        if (errno != 0 || end == text || *end != '\0' || !isfinite (number) ||
            (type == G_TYPE_FLOAT && fabs (number) > G_MAXFLOAT))
            goto out;
        if (type == G_TYPE_FLOAT)
            g_value_set_float (&value, (gfloat)number);
        else
            g_value_set_double (&value, number);
    }
    else
    {
        errno = 0;
        integer = g_ascii_strtoll (text, &end, 10);
        if (errno != 0 || end == text || *end != '\0')
            goto out;
        switch (G_TYPE_FUNDAMENTAL (type))
        {
        case G_TYPE_CHAR:
            if (integer < G_MININT8 || integer > G_MAXINT8) goto out;
            g_value_set_schar (&value, integer);
            break;
        case G_TYPE_UCHAR:
            if (integer < 0 || integer > G_MAXUINT8) goto out;
            g_value_set_uchar (&value, integer);
            break;
        case G_TYPE_INT:
            if (integer < G_MININT || integer > G_MAXINT) goto out;
            g_value_set_int (&value, integer);
            break;
        case G_TYPE_UINT:
            if (integer < 0 || integer > G_MAXUINT) goto out;
            g_value_set_uint (&value, integer);
            break;
        case G_TYPE_LONG:
            if (integer < G_MINLONG || integer > G_MAXLONG) goto out;
            g_value_set_long (&value, integer);
            break;
        case G_TYPE_ULONG:
            if (integer < 0 || (guint64)integer > G_MAXULONG) goto out;
            g_value_set_ulong (&value, integer);
            break;
        case G_TYPE_INT64:
            g_value_set_int64 (&value, integer);
            break;
        case G_TYPE_UINT64:
            if (integer < 0) goto out;
            g_value_set_uint64 (&value, integer);
            break;
        default:
            goto out;
        }
    }
    valid = !g_param_value_validate (pspec, &value);
out:
    g_value_unset (&value);
    return valid;
}

static gboolean
is_standard_property (GParamSpec *pspec)
{
    GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);

    return G_TYPE_IS_FUNDAMENTAL (type) || G_TYPE_IS_ENUM (type) ||
           G_TYPE_IS_FLAGS (type) || type == G_TYPE_STRV || type == YAML_TYPE_NODE;
}

static gboolean
validated_default_deserialize (GValue     *value,
                               GParamSpec *pspec,
                               YamlNode   *node)
{
    if (G_PARAM_SPEC_VALUE_TYPE (pspec) == YAML_TYPE_NODE)
    {
        g_value_set_boxed (value, node);
        return TRUE;
    }
    if (yaml_node_get_node_type (node) == YAML_NODE_NULL)
    {
        /* Only nullable standard properties reach this point. */
        g_value_reset (value);
        return TRUE;
    }
    return yaml_serializable_default_deserialize_property (NULL, pspec->name,
                                                            value, pspec, node);
}

GObject *
lrg_data_loader_load_file_validated (LrgDataLoader  *self,
                                     const gchar    *path,
                                     GError        **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(GError) parse_error = NULL;
    g_autoptr(GObject) object = NULL;
    g_autofree const gchar **construct_names = NULL;
    g_autofree GValue *construct_values = NULL;
    GObjectClass *klass;
    guint n_construct = 0;
    gboolean valid = TRUE;
    YamlNode *root;
    YamlMapping *mapping;
    const gchar *type_name;
    GType type;
    guint i;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, &parse_error))
    {
        g_propagate_error (error, g_steal_pointer (&parse_error));
        return NULL;
    }
    root = yaml_parser_get_root (parser);
    if (yaml_parser_get_n_documents (parser) != 1 || root == NULL ||
        yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PARSE,
                     "%s: expected one YAML document with a mapping root", path);
        return NULL;
    }
    mapping = yaml_node_get_mapping (root);
    type_name = yaml_mapping_get_string_member (mapping, self->type_field_name);
    type = type_name != NULL && self->registry != NULL ?
           lrg_registry_lookup (self->registry, type_name) : G_TYPE_INVALID;
    if (type == G_TYPE_INVALID || !g_type_is_a (type, G_TYPE_OBJECT) || G_TYPE_IS_ABSTRACT (type))
    {
        g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE,
                     "%s: missing, unknown or non-instantiable type '%s'", path,
                     type_name != NULL ? type_name : "");
        return NULL;
    }
    klass = g_type_class_ref (type);
    for (i = 0; i < yaml_mapping_get_size (mapping); i++)
    {
        const gchar *name = yaml_mapping_get_key (mapping, i);
        GParamSpec *pspec;
        YamlNode *node = yaml_mapping_get_value (mapping, i);

        if (g_str_equal (name, self->type_field_name))
            continue;
        pspec = g_object_class_find_property (klass, name);
        if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE) ||
            (is_standard_property (pspec) && !validate_property_node (pspec, node)) ||
            (!is_standard_property (pspec) &&
             (!g_type_is_a (type, YAML_TYPE_SERIALIZABLE) || (pspec->flags & G_PARAM_CONSTRUCT_ONLY))))
        {
            g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY,
                         "%s: unknown, unwritable or invalid property '%s'", path, name);
            g_type_class_unref (klass);
            return NULL;
        }
    }
    /* Decode construction values before constructing the candidate object. */
    construct_names = g_new0 (const gchar *, yaml_mapping_get_size (mapping));
    construct_values = g_new0 (GValue, yaml_mapping_get_size (mapping));
    for (i = 0; i < yaml_mapping_get_size (mapping); i++)
    {
        const gchar *name = yaml_mapping_get_key (mapping, i);
        GParamSpec *pspec;
        GValue *value;

        if (g_str_equal (name, self->type_field_name))
            continue;
        pspec = g_object_class_find_property (klass, name);
        if (!(pspec->flags & G_PARAM_CONSTRUCT_ONLY))
            continue;
        construct_names[n_construct] = pspec->name;
        value = &construct_values[n_construct++];
        g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));
        valid = validated_default_deserialize (value, pspec, yaml_mapping_get_value (mapping, i));
        valid = valid && !g_param_value_validate (pspec, value);
        if (!valid)
        {
            g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY,
                         "%s: invalid construct property '%s'", path, name);
            break;
        }
    }
    if (valid)
        object = g_object_new_with_properties (type, n_construct, construct_names, construct_values);
    for (i = 0; i < n_construct; i++)
        g_value_unset (&construct_values[i]);

    /* Decode each non-construct property exactly once, then validate the value
     * before invoking its setter. The candidate is never published on failure. */
    if (object != NULL)
    {
        for (i = 0; i < yaml_mapping_get_size (mapping); i++)
        {
            const gchar *name = yaml_mapping_get_key (mapping, i);
            GParamSpec *pspec;
            GValue value = G_VALUE_INIT;
            YamlNode *node = yaml_mapping_get_value (mapping, i);

            if (g_str_equal (name, self->type_field_name))
                continue;
            pspec = g_object_class_find_property (klass, name);
            if (pspec->flags & G_PARAM_CONSTRUCT_ONLY)
                continue;
            g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
            if (YAML_IS_SERIALIZABLE (object))
                valid = yaml_serializable_deserialize_property (YAML_SERIALIZABLE (object),
                           pspec->name, &value, pspec, node);
            else
                valid = validated_default_deserialize (&value, pspec, node);
            valid = valid && !g_param_value_validate (pspec, &value);
            if (valid)
            {
                if (YAML_IS_SERIALIZABLE (object))
                    valid = yaml_serializable_set_property (YAML_SERIALIZABLE (object), pspec, &value);
                else
                    g_object_set_property (object, pspec->name, &value);
            }
            g_value_unset (&value);
            if (!valid)
            {
                g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PROPERTY,
                             "%s: deserializer or setter rejected property '%s'", path, name);
                g_clear_object (&object);
                break;
            }
        }
    }
    g_type_class_unref (klass);
    return g_steal_pointer (&object);
}

/*
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
    g_autoptr(GError) parse_error = NULL;
    g_autofree gchar     *path = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (G_IS_FILE (file), NULL);

    parser = yaml_parser_new ();
    path = g_file_get_path (file);

    if (!yaml_parser_load_from_gfile (parser, file, cancellable, &parse_error))
    {
        g_propagate_error (error, g_steal_pointer (&parse_error));
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

/*
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
    g_autoptr(GError) parse_error = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (data != NULL, NULL);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_data (parser, data, length, &parse_error))
    {
        g_propagate_error (error, g_steal_pointer (&parse_error));
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

/*
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
    g_autoptr(GError) parse_error = NULL;
    YamlNode             *root;
    GObject              *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (type != G_TYPE_INVALID, NULL);
    g_return_val_if_fail (path != NULL, NULL);

    parser = yaml_parser_new ();

    if (!yaml_parser_load_from_file (parser, path, &parse_error))
    {
        g_propagate_error (error, g_steal_pointer (&parse_error));
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

/*
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

/*
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

#ifdef LRG_HAS_LIBDEX
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
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Public API - Utility
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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

/* ==========================================================================
 * LrgDataContainer
 * ========================================================================== */

G_DEFINE_INTERFACE (LrgDataContainer, lrg_data_container, G_TYPE_OBJECT)

static void
lrg_data_container_default_init (LrgDataContainerInterface *iface)
{
    (void)iface;
}

gboolean
lrg_data_container_add_child (LrgDataContainer  *self,
                              GObject           *child,
                              GError           **error)
{
    LrgDataContainerInterface *iface;

    g_return_val_if_fail (LRG_IS_DATA_CONTAINER (self), FALSE);
    g_return_val_if_fail (G_IS_OBJECT (child), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_DATA_CONTAINER_GET_IFACE (self);
    if (iface->add_child == NULL)
    {
        g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_FAILED,
                     "%s does not accept children", G_OBJECT_TYPE_NAME (self));
        return FALSE;
    }
    return iface->add_child (self, child, error);
}

guint
lrg_data_container_get_n_children (LrgDataContainer *self)
{
    LrgDataContainerInterface *iface;

    g_return_val_if_fail (LRG_IS_DATA_CONTAINER (self), 0);

    iface = LRG_DATA_CONTAINER_GET_IFACE (self);
    return iface->get_n_children != NULL ? iface->get_n_children (self) : 0;
}

GObject *
lrg_data_container_get_child (LrgDataContainer *self,
                              guint             index)
{
    LrgDataContainerInterface *iface;

    g_return_val_if_fail (LRG_IS_DATA_CONTAINER (self), NULL);

    iface = LRG_DATA_CONTAINER_GET_IFACE (self);
    if (iface->get_child == NULL || index >= lrg_data_container_get_n_children (self))
        return NULL;
    return iface->get_child (self, index);
}

GType
lrg_data_container_get_child_type (LrgDataContainer *self)
{
    LrgDataContainerInterface *iface;

    g_return_val_if_fail (LRG_IS_DATA_CONTAINER (self), G_TYPE_INVALID);

    iface = LRG_DATA_CONTAINER_GET_IFACE (self);
    return iface->get_child_type != NULL ? iface->get_child_type (self) : G_TYPE_INVALID;
}

/* ==========================================================================
 * Composite documents: settings, hints, references
 * ========================================================================== */

/* qdata keys: a pspec's declared list item type, an object's reference */
static GQuark
lrg_data_item_type_quark (void)
{
    return g_quark_from_static_string ("lrg-data-loader-item-type");
}

static GQuark
lrg_data_object_ref_quark (void)
{
    return g_quark_from_static_string ("lrg-data-loader-object-ref");
}

void
lrg_data_loader_set_param_item_type (GParamSpec *pspec,
                                     GType       item_type)
{
    g_return_if_fail (G_IS_PARAM_SPEC (pspec));
    g_return_if_fail (item_type == G_TYPE_INVALID || g_type_is_a (item_type, G_TYPE_OBJECT));

    /* a GType fits a pointer on every platform GLib supports */
    g_param_spec_set_qdata (pspec, lrg_data_item_type_quark (), GSIZE_TO_POINTER ((gsize)item_type));
}

GType
lrg_data_loader_get_param_item_type (GParamSpec *pspec)
{
    g_return_val_if_fail (G_IS_PARAM_SPEC (pspec), G_TYPE_INVALID);

    return (GType)GPOINTER_TO_SIZE (g_param_spec_get_qdata (pspec, lrg_data_item_type_quark ()));
}

const gchar *
lrg_data_loader_get_object_ref (GObject *object)
{
    g_return_val_if_fail (G_IS_OBJECT (object), NULL);

    return (const gchar *)g_object_get_qdata (object, lrg_data_object_ref_quark ());
}

void
lrg_data_loader_set_object_ref (GObject     *object,
                                const gchar *ref)
{
    g_return_if_fail (G_IS_OBJECT (object));

    g_object_set_qdata_full (object, lrg_data_object_ref_quark (),
                             ref != NULL ? g_strdup (ref) : NULL, g_free);
}

void
lrg_data_loader_set_resolver (LrgDataLoader      *self,
                              LrgDataResolveFunc  func,
                              gpointer            user_data,
                              GDestroyNotify      destroy)
{
    g_return_if_fail (LRG_IS_DATA_LOADER (self));

    if (self->resolve_destroy != NULL)
        self->resolve_destroy (self->resolve_data);
    self->resolve_func = func;
    self->resolve_data = user_data;
    self->resolve_destroy = destroy;
}

gboolean
lrg_data_loader_get_strict (LrgDataLoader *self)
{
    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), TRUE);

    return self->strict;
}

void
lrg_data_loader_set_strict (LrgDataLoader *self,
                            gboolean       strict)
{
    g_return_if_fail (LRG_IS_DATA_LOADER (self));

    strict = !!strict;
    if (self->strict != strict)
    {
        self->strict = strict;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STRICT]);
    }
}

const gchar *
lrg_data_loader_get_children_key (LrgDataLoader *self)
{
    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);

    return self->children_key;
}

void
lrg_data_loader_set_children_key (LrgDataLoader *self,
                                  const gchar   *key)
{
    g_return_if_fail (LRG_IS_DATA_LOADER (self));
    g_return_if_fail (key != NULL && key[0] != '\0');

    if (g_strcmp0 (self->children_key, key) != 0)
    {
        g_free (self->children_key);
        self->children_key = g_strdup (key);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHILDREN_KEY]);
    }
}

/* ==========================================================================
 * YAML helpers shared by the in-tree format writers
 * ========================================================================== */

/*
 * A string scalar that reads back as the same string: text yaml-glib would
 * read as null ("", "~", "null") is double-quoted; everything else is left
 * to libyaml, which quotes whatever a plain scalar cannot hold.
 */
YamlNode *
lrg_data_yaml_string (const gchar *text)
{
    YamlNode *node;

    if (text == NULL)
        return yaml_node_new_null ();

    node = yaml_node_new_string (text);
    if (text[0] == '\0' || g_str_equal (text, "~") || g_ascii_strcasecmp (text, "null") == 0)
        yaml_node_set_scalar_style (node, YAML_SCALAR_STYLE_DOUBLE_QUOTED);
    return node;
}

/*
 * The shortest "%.Ng" text that parses back to exactly @value (as a float
 * when @single). yaml-glib's own double nodes use "%g", which loses digits.
 */
static YamlNode *
shortest_number (gdouble  value,
                 gboolean single)
{
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    gchar format[8];
    gint digits;

    if (!isfinite (value))
        return yaml_node_new_scalar (isnan (value) ? ".nan" : value > 0 ? ".inf" : "-.inf",
                                     YAML_SCALAR_STYLE_PLAIN);

    /* Whole numbers print as integers: "%.1g" would turn 200 into
     * "2e+02", which round-trips but is unreadable in hand-edited YAML.
     * Below 1e15 every whole double is exact in "%.0f". */
    if (value == floor (value) && fabs (value) < 1e15)
    {
        g_snprintf (buf, sizeof buf, "%.0f", value);
        return yaml_node_new_scalar (buf, YAML_SCALAR_STYLE_PLAIN);
    }

    for (digits = 1; digits <= 17; digits++)
    {
        gdouble back;

        g_snprintf (format, sizeof format, "%%.%dg", digits);
        g_ascii_formatd (buf, sizeof buf, format, value);
        back = g_ascii_strtod (buf, NULL);
        if (single ? (gfloat)back == (gfloat)value : back == value)
            break;
    }
    return yaml_node_new_scalar (buf, YAML_SCALAR_STYLE_PLAIN);
}

YamlNode *
lrg_data_yaml_double (gdouble value)
{
    return shortest_number (value, FALSE);
}

YamlNode *
lrg_data_yaml_float (gfloat value)
{
    return shortest_number ((gdouble)value, TRUE);
}

YamlNode *
lrg_data_yaml_int (gint64 value)
{
    g_autofree gchar *text = g_strdup_printf ("%" G_GINT64_FORMAT, value);

    return yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN);
}

YamlNode *
lrg_data_yaml_boolean (gboolean value)
{
    return yaml_node_new_scalar (value ? "true" : "false", YAML_SCALAR_STYLE_PLAIN);
}

/* the text of @root with the writers' common settings (2-space block style) */
gchar *
lrg_data_yaml_to_data (YamlNode  *root,
                       gsize     *length,
                       GError   **error)
{
    g_autoptr(YamlGenerator) generator = NULL;

    generator = yaml_generator_new ();
    yaml_generator_set_indent (generator, 2);
    yaml_generator_set_unicode (generator, TRUE);
    yaml_generator_set_root (generator, root);
    return yaml_generator_to_data (generator, length, error);
}

gboolean
lrg_data_yaml_to_file (YamlNode     *root,
                       const gchar  *path,
                       GError      **error)
{
    g_autofree gchar *text = NULL;
    gsize length = 0;

    text = lrg_data_yaml_to_data (root, &length, error);
    if (text == NULL)
        return FALSE;
    return g_file_set_contents (path, text, (gssize)length, error);
}

/* ==========================================================================
 * Composite documents: paths and source positions
 *
 * Paths are JSON pointers: "" is the root, "/children/0/material" a value
 * below it; '~' and '/' inside a key are written "~0" and "~1". Positions
 * come from a libyaml event walk of the source text (yaml-glib nodes carry
 * none) and are only computed when an error needs one.
 * ========================================================================== */

static gchar *
path_join (const gchar *parent,
           const gchar *key)
{
    GString *path;
    const gchar *p;

    path = g_string_new (parent);
    g_string_append_c (path, '/');
    for (p = key; *p != '\0'; p++)
    {
        if (*p == '~')
            g_string_append (path, "~0");
        else if (*p == '/')
            g_string_append (path, "~1");
        else
            g_string_append_c (path, *p);
    }
    return g_string_free (path, FALSE);
}

static gchar *
path_index (const gchar *parent,
            guint        index)
{
    return g_strdup_printf ("%s/%u", parent, index);
}

typedef struct
{
    guint line;
    guint column;
} Position;

typedef struct
{
    gboolean  is_map;
    gboolean  want_key;
    gchar    *path;        /* NULL inside a complex key: nothing recorded */
    gchar    *key;
    guint     key_line;
    guint     key_column;
    guint     index;
} PosFrame;

static void
pos_frame_free (gpointer data)
{
    PosFrame *frame = (PosFrame *)data;

    g_free (frame->path);
    g_free (frame->key);
    g_free (frame);
}

static void
record_position (GHashTable *positions,
                 const gchar *path,
                 guint        line,
                 guint        column)
{
    Position *pos;

    if (g_hash_table_contains (positions, path))
        return;     /* a duplicate key: keep the first position */
    pos = g_new (Position, 1);
    pos->line = line;
    pos->column = column;
    g_hash_table_insert (positions, g_strdup (path), pos);
}

/*
 * Walks the first document of @text and records where each value starts.
 * A mapping value is recorded at its key, which is where a reader looks.
 */
static GHashTable *
build_positions (const gchar *text,
                 gsize        length)
{
    yaml_parser_t parser;
    GHashTable *positions;
    g_autoptr(GPtrArray) stack = NULL;
    gboolean done = FALSE;

    positions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    stack = g_ptr_array_new_with_free_func (pos_frame_free);

    if (!yaml_parser_initialize (&parser))
        return positions;
    yaml_parser_set_input_string (&parser, (const unsigned char *)text, length);

    while (!done)
    {
        yaml_event_t event;
        PosFrame *top;
        gboolean is_node;

        if (!yaml_parser_parse (&parser, &event))
            break;

        top = stack->len > 0 ? (PosFrame *)g_ptr_array_index (stack, stack->len - 1) : NULL;
        is_node = event.type == YAML_SCALAR_EVENT || event.type == YAML_SEQUENCE_START_EVENT ||
                  event.type == YAML_MAPPING_START_EVENT || event.type == YAML_ALIAS_EVENT;

        if (event.type == YAML_DOCUMENT_END_EVENT || event.type == YAML_STREAM_END_EVENT)
        {
            done = TRUE;
        }
        else if (event.type == YAML_SEQUENCE_END_EVENT || event.type == YAML_MAPPING_END_EVENT)
        {
            if (stack->len > 0)
                g_ptr_array_remove_index (stack, stack->len - 1);
        }
        else if (is_node && top != NULL && top->is_map && top->want_key)
        {
            /* --- a mapping key: remember it and where it is --- */
            g_clear_pointer (&top->key, g_free);
            top->key_line = (guint)event.start_mark.line + 1;
            top->key_column = (guint)event.start_mark.column + 1;
            top->want_key = FALSE;
            if (event.type == YAML_SCALAR_EVENT)
            {
                top->key = g_strdup ((const gchar *)event.data.scalar.value);
            }
            else if (event.type != YAML_ALIAS_EVENT)
            {
                /* a complex key: walk it without recording anything */
                PosFrame *frame = g_new0 (PosFrame, 1);

                frame->is_map = event.type == YAML_MAPPING_START_EVENT;
                frame->want_key = frame->is_map;
                g_ptr_array_add (stack, frame);
            }
        }
        else if (is_node)
        {
            /* --- a value: record it, then descend into collections --- */
            g_autofree gchar *path = NULL;
            guint line = (guint)event.start_mark.line + 1;
            guint column = (guint)event.start_mark.column + 1;

            if (top == NULL)
            {
                path = g_strdup ("");
            }
            else if (top->path == NULL)
            {
                path = NULL;
            }
            else if (top->is_map)
            {
                path = path_join (top->path, top->key != NULL ? top->key : "");
                line = top->key_line;
                column = top->key_column;
            }
            else
            {
                path = path_index (top->path, top->index);
            }

            if (top != NULL)
            {
                if (top->is_map)
                    top->want_key = TRUE;
                else
                    top->index++;
            }

            if (path != NULL)
                record_position (positions, path, line, column);

            if (event.type == YAML_SEQUENCE_START_EVENT || event.type == YAML_MAPPING_START_EVENT)
            {
                PosFrame *frame = g_new0 (PosFrame, 1);

                frame->is_map = event.type == YAML_MAPPING_START_EVENT;
                frame->want_key = frame->is_map;
                frame->path = g_steal_pointer (&path);
                g_ptr_array_add (stack, frame);
            }
        }

        yaml_event_delete (&event);
    }

    yaml_parser_delete (&parser);
    return positions;
}

/* ==========================================================================
 * Composite documents: the build context and its diagnostics
 * ========================================================================== */

/* at most this many problems are spelled out in one GError */
#define MAX_REPORTED_ERRORS 32

typedef enum
{
    KIND_SCALAR,
    KIND_OBJECT,
    KIND_LIST_MODEL,
    KIND_PTR_ARRAY,
    KIND_CHILDREN
} SlotKind;

/* one element of a value being built: an object or a reference to resolve */
typedef struct
{
    GObject *object;
    gchar   *ref;
    gchar   *path;
} Slot;

static void
slot_free (gpointer data)
{
    Slot *slot = (Slot *)data;

    g_clear_object (&slot->object);
    g_free (slot->ref);
    g_free (slot->path);
    g_free (slot);
}

/* a value holding references: assigned once the whole document is built */
typedef struct
{
    GObject    *owner;
    GParamSpec *pspec;     /* NULL for KIND_CHILDREN */
    SlotKind    kind;
    GType       item_type;
    GPtrArray  *slots;     /* Slot* */
    gchar      *path;
} Pending;

static void
pending_free (gpointer data)
{
    Pending *pending = (Pending *)data;

    g_clear_object (&pending->owner);
    g_clear_pointer (&pending->slots, g_ptr_array_unref);
    g_free (pending->path);
    g_free (pending);
}

typedef struct
{
    LrgDataLoader *self;
    const gchar   *source;
    const gchar   *text;        /* NULL: no positions */
    gsize          length;
    GHashTable    *positions;   /* path -> Position*, built lazily */
    GString       *messages;
    guint          n_errors;
    gint           first_code;
    GPtrArray     *pending;     /* Pending*, document order */
    GHashTable    *by_path;     /* path -> GObject (kept alive by keep) */
    GHashTable    *by_id;       /* guid/id -> GObject */
    GHashTable    *ambiguous;   /* ids several objects share */
    GPtrArray     *keep;        /* a reference to every built object */
} Build;

static void
build_init (Build         *b,
            LrgDataLoader *self,
            const gchar   *source,
            const gchar   *text,
            gsize          length)
{
    memset (b, 0, sizeof *b);
    b->self = self;
    b->source = source;
    b->text = text;
    b->length = length;
    b->messages = g_string_new (NULL);
    b->first_code = LRG_DATA_LOADER_ERROR_FAILED;
    b->pending = g_ptr_array_new_with_free_func (pending_free);
    b->by_path = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    b->by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    b->ambiguous = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    b->keep = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
build_clear (Build *b)
{
    g_clear_pointer (&b->positions, g_hash_table_unref);
    if (b->messages != NULL)
        g_string_free (b->messages, TRUE);
    b->messages = NULL;
    g_clear_pointer (&b->pending, g_ptr_array_unref);
    g_clear_pointer (&b->by_path, g_hash_table_unref);
    g_clear_pointer (&b->by_id, g_hash_table_unref);
    g_clear_pointer (&b->ambiguous, g_hash_table_unref);
    g_clear_pointer (&b->keep, g_ptr_array_unref);
}

/* the position of @path, or of its nearest ancestor with one */
static const Position *
lookup_position (Build       *b,
                 const gchar *path)
{
    g_autofree gchar *probe = NULL;

    if (b->text == NULL)
        return NULL;
    if (b->positions == NULL)
        b->positions = build_positions (b->text, b->length);

    probe = g_strdup (path);
    for (;;)
    {
        const Position *pos = g_hash_table_lookup (b->positions, probe);
        gchar *slash;

        if (pos != NULL)
            return pos;
        slash = strrchr (probe, '/');
        if (slash == NULL)
            return NULL;
        *slash = '\0';
    }
}

/* records one problem as "source:line:column: /path: message" */
static void build_error (Build       *b,
                         gint         code,
                         const gchar *path,
                         const gchar *format,
                         ...) G_GNUC_PRINTF (4, 5);

static void
build_error (Build       *b,
             gint         code,
             const gchar *path,
             const gchar *format,
             ...)
{
    g_autofree gchar *text = NULL;
    const Position *pos;
    va_list args;

    if (b->n_errors == 0)
        b->first_code = code;
    b->n_errors++;
    if (b->n_errors > MAX_REPORTED_ERRORS)
        return;

    va_start (args, format);
    text = g_strdup_vprintf (format, args);
    va_end (args);

    if (b->messages->len > 0)
        g_string_append_c (b->messages, '\n');
    pos = lookup_position (b, path);
    if (pos != NULL)
        g_string_append_printf (b->messages, "%s:%u:%u: ", b->source, pos->line, pos->column);
    else
        g_string_append_printf (b->messages, "%s: ", b->source);
    g_string_append_printf (b->messages, "%s: %s", path[0] != '\0' ? path : "/", text);
}

static void
build_take_error (Build   *b,
                  GError **error)
{
    if (b->n_errors > MAX_REPORTED_ERRORS)
        g_string_append_printf (b->messages, "\n(%u more problems not shown)",
                                b->n_errors - MAX_REPORTED_ERRORS);
    g_set_error_literal (error, LRG_DATA_LOADER_ERROR, b->first_code, b->messages->str);
}

/* ==========================================================================
 * Composite documents: property classification and scalar values
 * ========================================================================== */

/* how a property takes its value; @item_type gets the object/item type */
static SlotKind
property_kind (GParamSpec *pspec,
               GType      *item_type)
{
    GType value_type = G_PARAM_SPEC_VALUE_TYPE (pspec);
    GType hint = lrg_data_loader_get_param_item_type (pspec);

    if (g_type_is_a (value_type, G_TYPE_LIST_MODEL))
    {
        *item_type = hint;
        return KIND_LIST_MODEL;
    }
    if (value_type == G_TYPE_PTR_ARRAY && hint != G_TYPE_INVALID)
    {
        *item_type = hint;
        return KIND_PTR_ARRAY;
    }
    if (g_type_is_a (value_type, G_TYPE_OBJECT))
    {
        *item_type = value_type;
        return KIND_OBJECT;
    }
    *item_type = G_TYPE_INVALID;
    return KIND_SCALAR;
}

/* whether a mapping may leave out `type:` in a slot of @type */
static gboolean
type_is_implied (GType type)
{
    return type != G_TYPE_INVALID && type != G_TYPE_OBJECT &&
           g_type_is_a (type, G_TYPE_OBJECT) &&
           !G_TYPE_IS_ABSTRACT (type) && !G_TYPE_IS_INTERFACE (type);
}

/* what a scalar property accepts, for error messages */
static gchar *
describe_property (GParamSpec *pspec)
{
    GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);

    if (type == G_TYPE_BOOLEAN)
        return g_strdup ("true or false");
    if (type == G_TYPE_STRING)
        return g_strdup ("a string");
    if (type == G_TYPE_STRV)
        return g_strdup ("a list of strings");
    if (G_TYPE_IS_ENUM (type))
    {
        GEnumClass *klass = g_type_class_ref (type);
        GString *text = g_string_new ("one of ");
        guint i;

        for (i = 0; i < klass->n_values; i++)
            g_string_append_printf (text, "%s%s", i > 0 ? ", " : "", klass->values[i].value_nick);
        g_type_class_unref (klass);
        return g_string_free (text, FALSE);
    }
    if (G_TYPE_IS_FLAGS (type))
    {
        GFlagsClass *klass = g_type_class_ref (type);
        GString *text = g_string_new ("a list of ");
        guint i;

        for (i = 0; i < klass->n_values; i++)
            g_string_append_printf (text, "%s%s", i > 0 ? ", " : "", klass->values[i].value_nick);
        g_type_class_unref (klass);
        return g_string_free (text, FALSE);
    }
    if (G_IS_PARAM_SPEC_INT (pspec))
        return g_strdup_printf ("an integer from %d to %d",
                                G_PARAM_SPEC_INT (pspec)->minimum, G_PARAM_SPEC_INT (pspec)->maximum);
    if (G_IS_PARAM_SPEC_UINT (pspec))
        return g_strdup_printf ("an integer from %u to %u",
                                G_PARAM_SPEC_UINT (pspec)->minimum, G_PARAM_SPEC_UINT (pspec)->maximum);
    if (G_IS_PARAM_SPEC_INT64 (pspec))
        return g_strdup_printf ("an integer from %" G_GINT64_FORMAT " to %" G_GINT64_FORMAT,
                                G_PARAM_SPEC_INT64 (pspec)->minimum, G_PARAM_SPEC_INT64 (pspec)->maximum);
    if (G_IS_PARAM_SPEC_FLOAT (pspec))
        return g_strdup_printf ("a number from %g to %g",
                                (gdouble)G_PARAM_SPEC_FLOAT (pspec)->minimum,
                                (gdouble)G_PARAM_SPEC_FLOAT (pspec)->maximum);
    if (G_IS_PARAM_SPEC_DOUBLE (pspec))
        return g_strdup_printf ("a number from %g to %g",
                                G_PARAM_SPEC_DOUBLE (pspec)->minimum, G_PARAM_SPEC_DOUBLE (pspec)->maximum);
    if (G_TYPE_IS_FUNDAMENTAL (type))
        return g_strdup ("a number in range");
    return g_strdup_printf ("a %s value", g_type_name (type));
}

/* a short excerpt of a node for messages */
static gchar *
describe_node (YamlNode *node)
{
    switch (yaml_node_get_node_type (node))
    {
    case YAML_NODE_MAPPING:
        return g_strdup ("a mapping");
    case YAML_NODE_SEQUENCE:
        return g_strdup ("a list");
    case YAML_NODE_NULL:
        return g_strdup ("null");
    case YAML_NODE_SCALAR:
    default:
        return g_strdup_printf ("'%s'", yaml_node_get_scalar (node));
    }
}

/*
 * Decodes a scalar-kind property value into @value (initialised here).
 * @owner is the object when it exists (its YamlSerializable decoder then
 * applies), %NULL for construct-only values.
 */
static gboolean
decode_scalar (Build       *b,
               GObject     *owner,
               GParamSpec  *pspec,
               YamlNode    *node,
               const gchar *path,
               GValue      *value)
{
    GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);
    gboolean ok = FALSE;

    g_value_init (value, type);

    if (is_standard_property (pspec))
    {
        if (!validate_property_node (pspec, node))
        {
            g_autofree gchar *got = describe_node (node);
            g_autofree gchar *want = describe_property (pspec);

            build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                         "invalid value %s for property '%s' (expected %s)", got, pspec->name, want);
            return FALSE;
        }
        if (owner != NULL && YAML_IS_SERIALIZABLE (owner))
            ok = yaml_serializable_deserialize_property (YAML_SERIALIZABLE (owner), pspec->name,
                                                         value, pspec, node);
        else
            ok = validated_default_deserialize (value, pspec, node);
    }
    else if (owner != NULL && YAML_IS_SERIALIZABLE (owner))
    {
        ok = yaml_serializable_deserialize_property (YAML_SERIALIZABLE (owner), pspec->name,
                                                     value, pspec, node);
    }
    else if (G_TYPE_IS_BOXED (type) && yaml_boxed_can_deserialize (type))
    {
        gpointer boxed = yaml_boxed_deserialize (type, node);

        ok = boxed != NULL;
        if (ok)
            g_value_take_boxed (value, boxed);
    }
    else
    {
        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "property '%s' has type %s, which a document cannot hold",
                     pspec->name, g_type_name (type));
        return FALSE;
    }

    if (!ok || g_param_value_validate (pspec, value))
    {
        g_autofree gchar *got = describe_node (node);

        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "invalid value %s for property '%s'", got, pspec->name);
        return FALSE;
    }
    return TRUE;
}

/* the text of a `ref:` mapping, or %NULL; a malformed one is reported */
static const gchar *
reference_of (Build       *b,
              YamlNode    *node,
              const gchar *path,
              gboolean    *malformed)
{
    YamlMapping *mapping;
    YamlNode *value;

    *malformed = FALSE;
    if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
        return NULL;
    mapping = yaml_node_get_mapping (node);
    if (!yaml_mapping_has_member (mapping, LRG_DATA_LOADER_REF_KEY))
        return NULL;
    value = yaml_mapping_get_member (mapping, LRG_DATA_LOADER_REF_KEY);
    if (yaml_mapping_get_size (mapping) != 1 || value == NULL ||
        yaml_node_get_node_type (value) != YAML_NODE_SCALAR ||
        yaml_node_get_scalar (value)[0] == '\0')
    {
        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "a reference is a mapping with a single non-empty '%s' key",
                     LRG_DATA_LOADER_REF_KEY);
        *malformed = TRUE;
        return NULL;
    }
    return yaml_node_get_scalar (value);
}

/* indexes @object by its `guid` and `id` string properties */
static void
index_ids (Build   *b,
           GObject *object)
{
    static const gchar * const id_names[] = { "guid", "id", NULL };
    guint i;

    for (i = 0; id_names[i] != NULL; i++)
    {
        GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), id_names[i]);
        g_autofree gchar *id = NULL;
        GObject *other;

        if (pspec == NULL || G_PARAM_SPEC_VALUE_TYPE (pspec) != G_TYPE_STRING ||
            !(pspec->flags & G_PARAM_READABLE))
            continue;
        g_object_get (object, id_names[i], &id, NULL);
        if (id == NULL || id[0] == '\0')
            continue;
        other = g_hash_table_lookup (b->by_id, id);
        if (other == NULL)
            g_hash_table_insert (b->by_id, g_strdup (id), object);
        else if (other != object)
            g_hash_table_add (b->ambiguous, g_strdup (id));
    }
}

/* ==========================================================================
 * Composite documents: building
 * ========================================================================== */

static GObject *build_object (Build       *b,
                              YamlNode    *node,
                              GType        constraint,
                              GType        fallback,
                              const gchar *path);

/* fills a list property of @owner with @objects (all non-NULL) */
static gboolean
assign_list (Build       *b,
             GObject     *owner,
             GParamSpec  *pspec,
             SlotKind     kind,
             GType        item_type,
             GPtrArray   *objects,
             const gchar *path)
{
    guint i;

    if (kind == KIND_PTR_ARRAY)
    {
        g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func (g_object_unref);

        for (i = 0; i < objects->len; i++)
            g_ptr_array_add (array, g_object_ref (g_ptr_array_index (objects, i)));
        g_object_set (owner, pspec->name, array, NULL);
        return TRUE;
    }

    /* KIND_LIST_MODEL: refill the store the owner holds, else give it one */
    {
        g_autoptr(GObject) current = NULL;

        if (pspec->flags & G_PARAM_READABLE)
            g_object_get (owner, pspec->name, &current, NULL);
        if (current != NULL && G_IS_LIST_STORE (current))
        {
            g_list_store_remove_all (G_LIST_STORE (current));
            for (i = 0; i < objects->len; i++)
                g_list_store_append (G_LIST_STORE (current), g_ptr_array_index (objects, i));
            return TRUE;
        }
        if ((pspec->flags & G_PARAM_WRITABLE) && !(pspec->flags & G_PARAM_CONSTRUCT_ONLY) &&
            g_type_is_a (G_TYPE_LIST_STORE, G_PARAM_SPEC_VALUE_TYPE (pspec)))
        {
            g_autoptr(GListStore) store = NULL;

            store = g_list_store_new (item_type != G_TYPE_INVALID ? item_type : G_TYPE_OBJECT);
            for (i = 0; i < objects->len; i++)
                g_list_store_append (store, g_ptr_array_index (objects, i));
            g_object_set (owner, pspec->name, store, NULL);
            return TRUE;
        }
    }
    build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                 "list property '%s' holds no GListStore and cannot be given one", pspec->name);
    return FALSE;
}

/* appends @objects to the container @owner */
static gboolean
assign_children (Build       *b,
                 GObject     *owner,
                 GPtrArray   *objects,
                 GPtrArray   *paths)
{
    gboolean ok = TRUE;
    guint i;

    for (i = 0; i < objects->len; i++)
    {
        g_autoptr(GError) local = NULL;

        if (!lrg_data_container_add_child (LRG_DATA_CONTAINER (owner),
                                           g_ptr_array_index (objects, i), &local))
        {
            build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, g_ptr_array_index (paths, i),
                         "child rejected: %s",
                         local != NULL ? local->message : "no reason given");
            ok = FALSE;
        }
    }
    return ok;
}

/*
 * Builds the slots of a sequence (each a mapping or a `ref:`). Returns
 * them, with *@has_refs set when any is a reference; a failed element
 * leaves a slot with neither object nor ref.
 */
static GPtrArray *
build_slots (Build       *b,
             YamlNode    *node,
             GType        constraint,
             GType        fallback,
             const gchar *path,
             gboolean    *has_refs)
{
    GPtrArray *slots = g_ptr_array_new_with_free_func (slot_free);
    YamlSequence *sequence = yaml_node_get_sequence (node);
    guint i;

    *has_refs = FALSE;
    for (i = 0; i < yaml_sequence_get_length (sequence); i++)
    {
        YamlNode *element = yaml_sequence_get_element (sequence, i);
        Slot *slot = g_new0 (Slot, 1);
        const gchar *ref;
        gboolean malformed;

        slot->path = path_index (path, i);
        ref = reference_of (b, element, slot->path, &malformed);
        if (ref != NULL)
        {
            slot->ref = g_strdup (ref);
            *has_refs = TRUE;
        }
        else if (!malformed)
        {
            slot->object = build_object (b, element, constraint, fallback, slot->path);
        }
        g_ptr_array_add (slots, slot);
    }
    return slots;
}

/* queues a value with references for the resolve pass */
static void
add_pending (Build       *b,
             GObject     *owner,
             GParamSpec  *pspec,
             SlotKind     kind,
             GType        item_type,
             GPtrArray   *slots,
             const gchar *path)
{
    Pending *pending = g_new0 (Pending, 1);

    pending->owner = g_object_ref (owner);
    pending->pspec = pspec;
    pending->kind = kind;
    pending->item_type = item_type;
    pending->slots = slots;
    pending->path = g_strdup (path);
    g_ptr_array_add (b->pending, pending);
}

/* the built objects of @slots, or %NULL when one failed */
static GPtrArray *
slot_objects (GPtrArray *slots)
{
    GPtrArray *objects = g_ptr_array_new ();
    guint i;

    for (i = 0; i < slots->len; i++)
    {
        Slot *slot = g_ptr_array_index (slots, i);

        if (slot->object == NULL)
        {
            g_ptr_array_unref (objects);
            return NULL;
        }
        g_ptr_array_add (objects, slot->object);
    }
    return objects;
}

/* the item type of a list property: the hint, else the held store's */
static GType
list_item_type (GObject    *owner,
                GParamSpec *pspec,
                SlotKind    kind,
                GType       hint)
{
    g_autoptr(GObject) current = NULL;

    if (hint != G_TYPE_INVALID || kind != KIND_LIST_MODEL || !(pspec->flags & G_PARAM_READABLE))
        return hint;
    g_object_get (owner, pspec->name, &current, NULL);
    if (current != NULL && G_IS_LIST_MODEL (current))
        return g_list_model_get_item_type (G_LIST_MODEL (current));
    return G_TYPE_INVALID;
}

/* gives @owner the value of a composite (object or list) property */
static void
build_composite (Build       *b,
                 GObject     *owner,
                 GParamSpec  *pspec,
                 SlotKind     kind,
                 GType        item_type,
                 YamlNode    *node,
                 const gchar *path)
{
    YamlNodeType node_type = yaml_node_get_node_type (node);

    if (kind == KIND_OBJECT)
    {
        const gchar *ref;
        gboolean malformed;

        if (node_type == YAML_NODE_NULL)
        {
            g_object_set (owner, pspec->name, NULL, NULL);
            return;
        }
        ref = reference_of (b, node, path, &malformed);
        if (malformed)
            return;
        if (ref != NULL)
        {
            GPtrArray *slots = g_ptr_array_new_with_free_func (slot_free);
            Slot *slot = g_new0 (Slot, 1);

            slot->ref = g_strdup (ref);
            slot->path = g_strdup (path);
            g_ptr_array_add (slots, slot);
            add_pending (b, owner, pspec, kind, item_type, slots, path);
        }
        else
        {
            g_autoptr(GObject) child = NULL;

            child = build_object (b, node, item_type,
                                  type_is_implied (item_type) ? item_type : G_TYPE_INVALID, path);
            if (child != NULL)
                g_object_set (owner, pspec->name, child, NULL);
        }
        return;
    }

    /* --- lists --- */
    item_type = list_item_type (owner, pspec, kind, item_type);
    if (node_type == YAML_NODE_NULL)
    {
        g_autoptr(GPtrArray) none = g_ptr_array_new ();

        assign_list (b, owner, pspec, kind, item_type, none, path);
        return;
    }
    if (node_type != YAML_NODE_SEQUENCE)
    {
        g_autofree gchar *got = describe_node (node);

        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "invalid value %s for property '%s' (expected a list of %s)", got,
                     pspec->name, item_type != G_TYPE_INVALID ? g_type_name (item_type) : "objects");
        return;
    }
    {
        g_autoptr(GPtrArray) slots = NULL;
        gboolean has_refs;

        slots = build_slots (b, node, item_type != G_TYPE_INVALID ? item_type : G_TYPE_OBJECT,
                             type_is_implied (item_type) ? item_type : G_TYPE_INVALID, path, &has_refs);
        if (has_refs)
        {
            add_pending (b, owner, pspec, kind, item_type, g_steal_pointer (&slots), path);
        }
        else
        {
            g_autoptr(GPtrArray) objects = slot_objects (slots);

            if (objects != NULL)
                assign_list (b, owner, pspec, kind, item_type, objects, path);
        }
    }
}

/* builds the `children:` list of the container @owner */
static void
build_children (Build       *b,
                GObject     *owner,
                YamlNode    *node,
                const gchar *path)
{
    g_autoptr(GPtrArray) slots = NULL;
    GType child_type;
    gboolean has_refs;

    if (yaml_node_get_node_type (node) == YAML_NODE_NULL)
        return;
    if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
    {
        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "'%s' must be a list of child objects", b->self->children_key);
        return;
    }

    child_type = lrg_data_container_get_child_type (LRG_DATA_CONTAINER (owner));
    slots = build_slots (b, node, G_TYPE_OBJECT,
                         type_is_implied (child_type) ? child_type : G_TYPE_INVALID, path, &has_refs);
    if (has_refs)
    {
        add_pending (b, owner, NULL, KIND_CHILDREN, child_type, g_steal_pointer (&slots), path);
    }
    else
    {
        g_autoptr(GPtrArray) objects = slot_objects (slots);
        g_autoptr(GPtrArray) paths = g_ptr_array_new ();
        guint i;

        if (objects == NULL)
            return;
        for (i = 0; i < slots->len; i++)
            g_ptr_array_add (paths, ((Slot *)g_ptr_array_index (slots, i))->path);
        assign_children (b, owner, objects, paths);
    }
}

/* the type a mapping is built as, or G_TYPE_INVALID (reported) */
static GType
mapping_type (Build       *b,
              YamlMapping *mapping,
              GType        constraint,
              GType        fallback,
              const gchar *path)
{
    LrgDataLoader *self = b->self;
    g_autofree gchar *type_path = path_join (path, self->type_field_name);
    YamlNode *type_node;
    const gchar *type_name;
    GType type;

    if (!yaml_mapping_has_member (mapping, self->type_field_name))
    {
        if (fallback != G_TYPE_INVALID)
            return fallback;
        if (constraint != G_TYPE_INVALID && constraint != G_TYPE_OBJECT)
            build_error (b, LRG_DATA_LOADER_ERROR_TYPE, path,
                         "missing '%s' (a %s is expected here)", self->type_field_name,
                         g_type_name (constraint));
        else
            build_error (b, LRG_DATA_LOADER_ERROR_TYPE, path, "missing '%s'", self->type_field_name);
        return G_TYPE_INVALID;
    }

    type_node = yaml_mapping_get_member (mapping, self->type_field_name);
    if (type_node == NULL || yaml_node_get_node_type (type_node) != YAML_NODE_SCALAR)
    {
        build_error (b, LRG_DATA_LOADER_ERROR_TYPE, type_path, "'%s' must be a type name",
                     self->type_field_name);
        return G_TYPE_INVALID;
    }
    type_name = yaml_node_get_scalar (type_node);
    if (self->registry == NULL)
    {
        build_error (b, LRG_DATA_LOADER_ERROR_TYPE, type_path,
                     "no registry set to look up type '%s'", type_name);
        return G_TYPE_INVALID;
    }
    type = lrg_registry_lookup (self->registry, type_name);
    if (type == G_TYPE_INVALID)
    {
        build_error (b, LRG_DATA_LOADER_ERROR_TYPE, type_path, "unknown type '%s'", type_name);
        return G_TYPE_INVALID;
    }
    if (!g_type_is_a (type, G_TYPE_OBJECT) || G_TYPE_IS_ABSTRACT (type))
    {
        build_error (b, LRG_DATA_LOADER_ERROR_TYPE, type_path,
                     "type '%s' (%s) is not an instantiable object type", type_name, g_type_name (type));
        return G_TYPE_INVALID;
    }
    if (constraint != G_TYPE_INVALID && !g_type_is_a (type, constraint))
    {
        build_error (b, LRG_DATA_LOADER_ERROR_TYPE, type_path,
                     "type '%s' (%s) is not a %s", type_name, g_type_name (type),
                     g_type_name (constraint));
        return G_TYPE_INVALID;
    }
    return type;
}

/* reports or skips a key the type does not know */
static void
unknown_key (Build       *b,
             const gchar *name,
             GType        type,
             const gchar *path,
             gboolean     writable_problem)
{
    if (writable_problem)
    {
        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "property '%s' of %s is not writable", name, g_type_name (type));
        return;
    }
    if (b->self->strict)
        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "unknown property '%s' of %s", name, g_type_name (type));
    else
        lrg_warning (LRG_LOG_DOMAIN_CORE, "%s: %s: unknown property '%s' of %s skipped",
                     b->source, path, name, g_type_name (type));
}

/*
 * Builds one object from a mapping: its type (from `type:` or @fallback,
 * checked against @constraint), construct-only properties, the object,
 * its other properties in document order, its children; then indexes it.
 * Problems are recorded in @b; the object is returned whenever it could
 * be constructed, so building goes on and reports as much as it can.
 */
static GObject *
build_object (Build       *b,
              YamlNode    *node,
              GType        constraint,
              GType        fallback,
              const gchar *path)
{
    LrgDataLoader *self = b->self;
    g_autofree const gchar **construct_names = NULL;
    g_autofree GValue *construct_values = NULL;
    g_autofree GParamSpec **pspecs = NULL;
    GObjectClass *klass;
    GObject *object = NULL;
    YamlMapping *mapping;
    gboolean is_container;
    guint n_construct = 0;
    guint n;
    guint i;
    GType type;

    if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
    {
        g_autofree gchar *got = describe_node (node);

        build_error (b, LRG_DATA_LOADER_ERROR_TYPE, path, "expected an object mapping, got %s", got);
        return NULL;
    }
    mapping = yaml_node_get_mapping (node);
    type = mapping_type (b, mapping, constraint, fallback, path);
    if (type == G_TYPE_INVALID)
        return NULL;

    klass = g_type_class_ref (type);
    is_container = g_type_is_a (type, LRG_TYPE_DATA_CONTAINER);
    n = yaml_mapping_get_size (mapping);
    pspecs = g_new0 (GParamSpec *, n);
    construct_names = g_new0 (const gchar *, n);
    construct_values = g_new0 (GValue, n);

    /* --- pass 1: classify every key, decode construct-only values --- */
    for (i = 0; i < n; i++)
    {
        const gchar *name = yaml_mapping_get_key (mapping, i);
        YamlNode *value = yaml_mapping_get_value (mapping, i);
        g_autofree gchar *key_path = path_join (path, name);
        GParamSpec *pspec;
        SlotKind kind;
        GType item_type;

        if (g_str_equal (name, self->type_field_name))
            continue;
        if (is_container && g_str_equal (name, self->children_key))
            continue;
        if (g_str_equal (name, LRG_DATA_LOADER_REF_KEY))
        {
            build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, key_path,
                         "'%s' cannot be mixed with other keys", LRG_DATA_LOADER_REF_KEY);
            continue;
        }

        pspec = g_object_class_find_property (klass, name);
        if (pspec == NULL)
        {
            unknown_key (b, name, type, key_path, FALSE);
            continue;
        }
        kind = property_kind (pspec, &item_type);
        if (!(pspec->flags & G_PARAM_WRITABLE) &&
            !(kind == KIND_LIST_MODEL && (pspec->flags & G_PARAM_READABLE)))
        {
            unknown_key (b, name, type, key_path, TRUE);
            continue;
        }
        pspecs[i] = pspec;
        if (!(pspec->flags & G_PARAM_CONSTRUCT_ONLY))
            continue;

        /* construct-only: decoded now, no references possible */
        if (kind == KIND_SCALAR)
        {
            if (decode_scalar (b, NULL, pspec, value, key_path, &construct_values[n_construct]))
                construct_names[n_construct++] = pspec->name;
            else
                g_value_unset (&construct_values[n_construct]);
        }
        else if (kind == KIND_OBJECT)
        {
            gboolean malformed;

            if (reference_of (b, value, key_path, &malformed) != NULL)
            {
                build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, key_path,
                             "construct-only property '%s' cannot hold a reference", name);
            }
            else if (!malformed)
            {
                GObject *child = NULL;

                if (yaml_node_get_node_type (value) != YAML_NODE_NULL)
                    child = build_object (b, value, item_type,
                                          type_is_implied (item_type) ? item_type : G_TYPE_INVALID,
                                          key_path);
                if (child != NULL || yaml_node_get_node_type (value) == YAML_NODE_NULL)
                {
                    g_value_init (&construct_values[n_construct], G_PARAM_SPEC_VALUE_TYPE (pspec));
                    g_value_take_object (&construct_values[n_construct], child);
                    construct_names[n_construct++] = pspec->name;
                }
            }
        }
        else
        {
            build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, key_path,
                         "construct-only list property '%s' is not supported", name);
        }
    }

    /* --- the object --- */
    object = g_object_new_with_properties (type, n_construct, construct_names, construct_values);
    for (i = 0; i < n_construct; i++)
        g_value_unset (&construct_values[i]);

    /* --- pass 2: every other property, in document order --- */
    for (i = 0; i < n; i++)
    {
        GParamSpec *pspec = pspecs[i];
        const gchar *name = yaml_mapping_get_key (mapping, i);
        YamlNode *value = yaml_mapping_get_value (mapping, i);
        g_autofree gchar *key_path = NULL;
        SlotKind kind;
        GType item_type;

        if (pspec == NULL || (pspec->flags & G_PARAM_CONSTRUCT_ONLY))
            continue;
        key_path = path_join (path, name);
        kind = property_kind (pspec, &item_type);
        if (kind == KIND_SCALAR)
        {
            GValue decoded = G_VALUE_INIT;

            if (decode_scalar (b, object, pspec, value, key_path, &decoded))
            {
                if (YAML_IS_SERIALIZABLE (object))
                {
                    if (!yaml_serializable_set_property (YAML_SERIALIZABLE (object), pspec, &decoded))
                        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, key_path,
                                     "%s rejected property '%s'", g_type_name (type), name);
                }
                else
                {
                    g_object_set_property (object, pspec->name, &decoded);
                }
            }
            g_value_unset (&decoded);
        }
        else
        {
            build_composite (b, object, pspec, kind, item_type, value, key_path);
        }
    }

    /* --- children --- */
    if (is_container && yaml_mapping_has_member (mapping, self->children_key))
    {
        g_autofree gchar *children_path = path_join (path, self->children_key);

        build_children (b, object, yaml_mapping_get_member (mapping, self->children_key), children_path);
    }

    g_type_class_unref (klass);

    /* --- index it for references --- */
    g_ptr_array_add (b->keep, g_object_ref (object));
    g_hash_table_insert (b->by_path, g_strdup (path), object);
    index_ids (b, object);
    return object;
}

/* ==========================================================================
 * Composite documents: resolving references
 * ========================================================================== */

/* the object @ref names, or %NULL (reported) */
static GObject *
resolve_ref (Build       *b,
             const gchar *ref,
             GType        expected,
             const gchar *path)
{
    LrgDataLoader *self = b->self;
    GObject *found = NULL;

    if (ref[0] == '#')
    {
        /* "#" is the root, "#/children/0" a pointer into the document */
        found = g_hash_table_lookup (b->by_path, ref + 1);
        if (found != NULL)
            g_object_ref (found);
    }
    else if (g_hash_table_contains (b->ambiguous, ref))
    {
        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path,
                     "ambiguous reference '%s': several objects have that guid or id", ref);
        return NULL;
    }
    else
    {
        found = g_hash_table_lookup (b->by_id, ref);
        if (found != NULL)
            g_object_ref (found);
    }

    if (found == NULL && ref[0] != '#' && self->resolve_func != NULL)
    {
        g_autoptr(GError) local = NULL;

        found = self->resolve_func (self, ref, expected, self->resolve_data, &local);
        if (local != NULL)
        {
            build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path, "reference '%s': %s", ref, local->message);
            g_clear_object (&found);
            return NULL;
        }
        if (found != NULL && lrg_data_loader_get_object_ref (found) == NULL)
            lrg_data_loader_set_object_ref (found, ref);
    }

    if (found == NULL)
    {
        build_error (b, LRG_DATA_LOADER_ERROR_PROPERTY, path, "unresolved reference '%s'", ref);
        return NULL;
    }
    if (expected != G_TYPE_INVALID && !g_type_is_a (G_OBJECT_TYPE (found), expected))
    {
        build_error (b, LRG_DATA_LOADER_ERROR_TYPE, path, "reference '%s' is a %s, not a %s",
                     ref, G_OBJECT_TYPE_NAME (found), g_type_name (expected));
        g_object_unref (found);
        return NULL;
    }
    return found;
}

/* resolves every queued value, in document order, and assigns it */
static void
resolve_pending (Build *b)
{
    guint p;

    for (p = 0; p < b->pending->len; p++)
    {
        Pending *pending = g_ptr_array_index (b->pending, p);
        g_autoptr(GPtrArray) objects = NULL;
        g_autoptr(GPtrArray) paths = g_ptr_array_new ();
        gboolean complete = TRUE;
        GType expected;
        guint i;

        expected = pending->kind == KIND_CHILDREN ? G_TYPE_OBJECT :
                   pending->item_type != G_TYPE_INVALID ? pending->item_type : G_TYPE_OBJECT;
        for (i = 0; i < pending->slots->len; i++)
        {
            Slot *slot = g_ptr_array_index (pending->slots, i);

            if (slot->ref != NULL)
                slot->object = resolve_ref (b, slot->ref, expected, slot->path);
            if (slot->object == NULL)
                complete = FALSE;
            g_ptr_array_add (paths, slot->path);
        }
        if (!complete)
            continue;

        objects = slot_objects (pending->slots);
        switch (pending->kind)
        {
        case KIND_OBJECT:
            g_object_set (pending->owner, pending->pspec->name, g_ptr_array_index (objects, 0), NULL);
            break;
        case KIND_CHILDREN:
            assign_children (b, pending->owner, objects, paths);
            break;
        case KIND_LIST_MODEL:
        case KIND_PTR_ARRAY:
            assign_list (b, pending->owner, pending->pspec, pending->kind, pending->item_type,
                         objects, pending->path);
            break;
        case KIND_SCALAR:
        default:
            g_assert_not_reached ();
        }
    }
}

/* builds, resolves and returns the root, or %NULL with @error set */
static GObject *
build_document (Build    *b,
                YamlNode *root,
                GType     root_type,
                GError  **error)
{
    g_autoptr(GObject) object = NULL;

    if (root_type != G_TYPE_INVALID && !type_is_implied (root_type))
    {
        g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE,
                     "%s: root type %s is not an instantiable object type",
                     b->source, g_type_name (root_type));
        return NULL;
    }

    object = build_object (b, root, root_type, root_type, "");
    if (b->n_errors == 0)
        resolve_pending (b);
    if (b->n_errors > 0)
    {
        build_take_error (b, error);
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Loaded composite %s from %s",
               G_OBJECT_TYPE_NAME (object), b->source);
    return g_steal_pointer (&object);
}

GObject *
lrg_data_loader_load_composite_node (LrgDataLoader  *self,
                                     YamlNode       *root,
                                     GType           root_type,
                                     const gchar    *source_name,
                                     GError        **error)
{
    Build b;
    GObject *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (root != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    build_init (&b, self, source_name != NULL ? source_name : "(data)", NULL, 0);
    object = build_document (&b, root, root_type, error);
    build_clear (&b);
    return object;
}

GObject *
lrg_data_loader_load_composite_data (LrgDataLoader  *self,
                                     const gchar    *data,
                                     gssize          length,
                                     GType           root_type,
                                     const gchar    *source_name,
                                     GError        **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(GError) local = NULL;
    const gchar *source;
    YamlNode *root;
    Build b;
    GObject *object;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    source = source_name != NULL ? source_name : "(data)";
    if (length < 0)
        length = (gssize)strlen (data);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_data (parser, data, length, &local))
    {
        g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PARSE,
                     "%s: %s", source, local->message);
        return NULL;
    }
    root = yaml_parser_get_root (parser);
    if (yaml_parser_get_n_documents (parser) != 1 || root == NULL ||
        yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        g_set_error (error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_PARSE,
                     "%s: expected one YAML document with a mapping root", source);
        return NULL;
    }

    build_init (&b, self, source, data, (gsize)length);
    object = build_document (&b, root, root_type, error);
    build_clear (&b);
    return object;
}

GObject *
lrg_data_loader_load_composite_file (LrgDataLoader  *self,
                                     const gchar    *path,
                                     GType           root_type,
                                     GError        **error)
{
    g_autofree gchar *text = NULL;
    gsize length;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_file_get_contents (path, &text, &length, error))
        return NULL;
    return lrg_data_loader_load_composite_data (self, text, (gssize)length, root_type, path, error);
}

/* ==========================================================================
 * Composite documents: saving
 * ========================================================================== */

/* a `ref:` written before the document's ids are known */
typedef struct
{
    YamlNode *value;      /* the scalar to fill in */
    GObject  *target;
} RefFix;

static void
ref_fix_free (gpointer data)
{
    RefFix *fix = (RefFix *)data;

    yaml_node_unref (fix->value);
    g_free (fix);
}

typedef struct
{
    LrgDataLoader *self;
    GHashTable    *emitted;    /* GObject* -> path of its inline copy */
    GHashTable    *pristine;   /* GType -> GObject, fresh instances */
    GHashTable    *id_count;   /* guid/id text -> number of objects */
    GPtrArray     *fixes;      /* RefFix* */
    GError        *error;
} Save;

/* a freshly constructed instance of @type, to leave out unchanged values */
static GObject *
pristine_of (Save  *sv,
             GType  type)
{
    GObject *object = g_hash_table_lookup (sv->pristine, GSIZE_TO_POINTER ((gsize)type));

    if (object == NULL)
    {
        object = g_object_new (type, NULL);
        g_hash_table_insert (sv->pristine, GSIZE_TO_POINTER ((gsize)type), object);
    }
    return object;
}

/* the guid/id values of @object (at most two), for ref by id */
static void
object_ids (GObject  *object,
            gchar   **guid,
            gchar   **id)
{
    GObjectClass *klass = G_OBJECT_GET_CLASS (object);
    GParamSpec *pspec;

    *guid = NULL;
    *id = NULL;
    pspec = g_object_class_find_property (klass, "guid");
    if (pspec != NULL && G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_STRING && (pspec->flags & G_PARAM_READABLE))
        g_object_get (object, "guid", guid, NULL);
    pspec = g_object_class_find_property (klass, "id");
    if (pspec != NULL && G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_STRING && (pspec->flags & G_PARAM_READABLE))
        g_object_get (object, "id", id, NULL);
    if (*guid != NULL && (*guid)[0] == '\0')
        g_clear_pointer (guid, g_free);
    if (*id != NULL && (*id)[0] == '\0')
        g_clear_pointer (id, g_free);
}

static void
count_id (Save        *sv,
          const gchar *id)
{
    guint count;

    if (id == NULL)
        return;
    count = GPOINTER_TO_UINT (g_hash_table_lookup (sv->id_count, id));
    g_hash_table_insert (sv->id_count, g_strdup (id), GUINT_TO_POINTER (count + 1));
}

/* whether two values of @pspec are the same (exactly: no epsilon) */
static gboolean
values_equal (GParamSpec   *pspec,
              const GValue *a,
              const GValue *b)
{
    GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);

    if (type == G_TYPE_FLOAT)
        return g_value_get_float (a) == g_value_get_float (b);
    if (type == G_TYPE_DOUBLE)
        return g_value_get_double (a) == g_value_get_double (b);
    if (type == G_TYPE_STRV)
    {
        const gchar * const *sa = g_value_get_boxed (a);
        const gchar * const *sb = g_value_get_boxed (b);

        if (sa == NULL || sb == NULL)
            return sa == sb;
        return g_strv_equal (sa, sb);
    }
    if (type == YAML_TYPE_NODE)
    {
        YamlNode *na = g_value_get_boxed (a);
        YamlNode *nb = g_value_get_boxed (b);

        if (na == NULL || nb == NULL)
            return na == nb;
        return yaml_node_equal (na, nb);
    }
    if (G_TYPE_IS_BOXED (type))
        return g_value_get_boxed (a) == g_value_get_boxed (b);
    return g_param_values_cmp (pspec, a, b) == 0;
}

/* a scalar-kind value as YAML, or %NULL when it cannot be written */
static YamlNode *
serialize_scalar (GObject      *object,
                  GParamSpec   *pspec,
                  const GValue *value)
{
    GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);

    if (type == YAML_TYPE_NODE)
    {
        YamlNode *node = g_value_get_boxed (value);

        return node != NULL ? yaml_node_copy (node) : yaml_node_new_null ();
    }
    if (type == G_TYPE_STRV)
    {
        const gchar * const *strv = g_value_get_boxed (value);
        g_autoptr(YamlSequence) sequence = NULL;
        guint i;

        if (strv == NULL)
            return yaml_node_new_null ();
        sequence = yaml_sequence_new ();
        for (i = 0; strv[i] != NULL; i++)
        {
            g_autoptr(YamlNode) element = lrg_data_yaml_string (strv[i]);

            yaml_sequence_add_element (sequence, element);
        }
        return yaml_node_new_sequence (sequence);
    }
    if (G_TYPE_IS_ENUM (type))
    {
        GEnumClass *klass = g_type_class_ref (type);
        GEnumValue *entry = g_enum_get_value (klass, g_value_get_enum (value));
        YamlNode *node;

        node = entry != NULL ? lrg_data_yaml_string (entry->value_nick)
                             : lrg_data_yaml_int (g_value_get_enum (value));
        g_type_class_unref (klass);
        return node;
    }
    if (G_TYPE_IS_FLAGS (type))
    {
        GFlagsClass *klass = g_type_class_ref (type);
        g_autoptr(YamlSequence) sequence = yaml_sequence_new ();
        guint flags = g_value_get_flags (value);

        /* canonical: the first value covering the remaining bits, repeatedly */
        while (flags != 0)
        {
            GFlagsValue *entry = g_flags_get_first_value (klass, flags);
            g_autoptr(YamlNode) element = NULL;

            if (entry == NULL || entry->value == 0)
                break;
            element = lrg_data_yaml_string (entry->value_nick);
            yaml_sequence_add_element (sequence, element);
            flags &= ~entry->value;
        }
        g_type_class_unref (klass);
        return yaml_node_new_sequence (sequence);
    }

    switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_BOOLEAN:
        return lrg_data_yaml_boolean (g_value_get_boolean (value));
    case G_TYPE_CHAR:
        return lrg_data_yaml_int (g_value_get_schar (value));
    case G_TYPE_UCHAR:
        return lrg_data_yaml_int (g_value_get_uchar (value));
    case G_TYPE_INT:
        return lrg_data_yaml_int (g_value_get_int (value));
    case G_TYPE_UINT:
        return lrg_data_yaml_int (g_value_get_uint (value));
    case G_TYPE_LONG:
        return lrg_data_yaml_int (g_value_get_long (value));
    case G_TYPE_ULONG:
        return lrg_data_yaml_int ((gint64)g_value_get_ulong (value));
    case G_TYPE_INT64:
        return lrg_data_yaml_int (g_value_get_int64 (value));
    case G_TYPE_UINT64:
        return lrg_data_yaml_int ((gint64)g_value_get_uint64 (value));
    case G_TYPE_FLOAT:
        return lrg_data_yaml_float (g_value_get_float (value));
    case G_TYPE_DOUBLE:
        return lrg_data_yaml_double (g_value_get_double (value));
    case G_TYPE_STRING:
        return lrg_data_yaml_string (g_value_get_string (value));
    default:
        break;
    }

    if (YAML_IS_SERIALIZABLE (object))
        return yaml_serializable_serialize_property (YAML_SERIALIZABLE (object), pspec->name,
                                                     value, pspec);
    if (G_TYPE_IS_BOXED (type) && yaml_boxed_can_serialize (type))
    {
        gconstpointer boxed = g_value_get_boxed (value);

        return boxed != NULL ? yaml_boxed_serialize (type, boxed) : yaml_node_new_null ();
    }
    return NULL;
}

static YamlNode *emit_object (Save        *sv,
                              GObject     *object,
                              GType        implied,
                              const gchar *path);

/* a sequence of the objects of a list value */
static YamlNode *
emit_list (Save        *sv,
           GPtrArray   *items,
           GType        item_type,
           const gchar *path)
{
    g_autoptr(YamlSequence) sequence = yaml_sequence_new ();
    guint i;

    for (i = 0; i < items->len && sv->error == NULL; i++)
    {
        g_autofree gchar *item_path = path_index (path, i);
        g_autoptr(YamlNode) element = NULL;

        element = emit_object (sv, g_ptr_array_index (items, i),
                               type_is_implied (item_type) ? item_type : G_TYPE_INVALID, item_path);
        if (element != NULL)
            yaml_sequence_add_element (sequence, element);
    }
    return yaml_node_new_sequence (sequence);
}

/* the objects of a list-kind property value (borrowed), or %NULL */
static GPtrArray *
list_items (const GValue *value,
            SlotKind      kind,
            GType        *item_type)
{
    GPtrArray *items = g_ptr_array_new_with_free_func (g_object_unref);

    if (kind == KIND_PTR_ARRAY)
    {
        GPtrArray *array = g_value_get_boxed (value);
        guint i;

        for (i = 0; array != NULL && i < array->len; i++)
        {
            gpointer item = g_ptr_array_index (array, i);

            if (G_IS_OBJECT (item))
                g_ptr_array_add (items, g_object_ref (item));
        }
    }
    else
    {
        GListModel *model = g_value_get_object (value);
        guint i;

        if (model != NULL)
        {
            if (*item_type == G_TYPE_INVALID)
                *item_type = g_list_model_get_item_type (model);
            for (i = 0; i < g_list_model_get_n_items (model); i++)
                g_ptr_array_add (items, g_list_model_get_item (model, i));
        }
    }
    return items;
}

/* the mapping for one property, appended to @mapping (or nothing) */
static void
emit_property (Save        *sv,
               GObject     *object,
               GObject     *pristine,
               GParamSpec  *pspec,
               YamlMapping *mapping,
               const gchar *path)
{
    g_autofree gchar *key_path = path_join (path, pspec->name);
    g_auto(GValue) value = G_VALUE_INIT;
    g_auto(GValue) initial = G_VALUE_INIT;
    g_autoptr(YamlNode) node = NULL;
    gboolean writable = (pspec->flags & G_PARAM_WRITABLE) != 0;
    SlotKind kind;
    GType item_type;

    kind = property_kind (pspec, &item_type);
    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_value_init (&initial, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (object, pspec->name, &value);
    g_object_get_property (pristine, pspec->name, &initial);

    switch (kind)
    {
    case KIND_SCALAR:
        if (!writable || values_equal (pspec, &value, &initial))
            return;
        node = serialize_scalar (object, pspec, &value);
        break;

    case KIND_OBJECT:
        if (!writable)
            return;
        if (g_value_get_object (&value) == NULL)
        {
            if (g_value_get_object (&initial) == NULL)
                return;
            node = yaml_node_new_null ();
        }
        else
        {
            node = emit_object (sv, g_value_get_object (&value),
                                type_is_implied (item_type) ? item_type : G_TYPE_INVALID, key_path);
        }
        break;

    case KIND_LIST_MODEL:
    case KIND_PTR_ARRAY:
        {
            g_autoptr(GPtrArray) items = NULL;
            g_autoptr(GPtrArray) initial_items = NULL;
            GType initial_type = item_type;

            /* a read-only list is only written when the loader can refill it */
            if (!writable && !(kind == KIND_LIST_MODEL && G_IS_LIST_STORE (g_value_get_object (&value))))
                return;
            items = list_items (&value, kind, &item_type);
            initial_items = list_items (&initial, kind, &initial_type);
            if (items->len == 0 && initial_items->len == 0)
                return;
            node = emit_list (sv, items, item_type, key_path);
        }
        break;

    case KIND_CHILDREN:
    default:
        return;
    }

    if (node != NULL && sv->error == NULL)
        yaml_mapping_set_member (mapping, pspec->name, node);
}

/*
 * One object: a `ref:` when it lives elsewhere or was written already,
 * else a mapping with `type:` (unless @implied is its exact type), its
 * changed properties in class order and its children.
 */
static YamlNode *
emit_object (Save        *sv,
             GObject     *object,
             GType        implied,
             const gchar *path)
{
    LrgDataLoader *self = sv->self;
    g_autoptr(YamlMapping) mapping = NULL;
    g_autofree GParamSpec **pspecs = NULL;
    g_autofree gchar *guid = NULL;
    g_autofree gchar *id = NULL;
    const gchar *external;
    GObject *pristine;
    gboolean is_container;
    guint n_pspecs;
    guint i;
    GType type;

    if (sv->error != NULL)
        return NULL;

    /* --- references --- */
    external = lrg_data_loader_get_object_ref (object);
    if (external != NULL || g_hash_table_contains (sv->emitted, object))
    {
        g_autoptr(YamlMapping) ref = yaml_mapping_new ();
        g_autoptr(YamlNode) value = lrg_data_yaml_string (external != NULL ? external : "#");

        if (external == NULL)
        {
            RefFix *fix = g_new0 (RefFix, 1);

            fix->value = yaml_node_ref (value);
            fix->target = object;
            g_ptr_array_add (sv->fixes, fix);
        }
        yaml_mapping_set_member (ref, LRG_DATA_LOADER_REF_KEY, value);
        return yaml_node_new_mapping (ref);
    }
    g_hash_table_insert (sv->emitted, object, g_strdup (path));
    object_ids (object, &guid, &id);
    count_id (sv, guid);
    if (g_strcmp0 (guid, id) != 0)
        count_id (sv, id);

    /* --- type --- */
    mapping = yaml_mapping_new ();
    type = G_OBJECT_TYPE (object);
    if (type != implied)
    {
        const gchar *name = self->registry != NULL ? lrg_registry_lookup_name (self->registry, type) : NULL;
        g_autoptr(YamlNode) type_node = NULL;

        if (name == NULL)
        {
            g_set_error (&sv->error, LRG_DATA_LOADER_ERROR, LRG_DATA_LOADER_ERROR_TYPE,
                         "%s: type %s is not registered, so it cannot be written",
                         path[0] != '\0' ? path : "/", g_type_name (type));
            return NULL;
        }
        type_node = lrg_data_yaml_string (name);
        yaml_mapping_set_member (mapping, self->type_field_name, type_node);
    }

    /* --- properties, in class order --- */
    pristine = pristine_of (sv, type);
    is_container = LRG_IS_DATA_CONTAINER (object);
    pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &n_pspecs);
    for (i = 0; i < n_pspecs && sv->error == NULL; i++)
    {
        GParamSpec *pspec = pspecs[i];

        if (!(pspec->flags & G_PARAM_READABLE) || (pspec->flags & G_PARAM_DEPRECATED))
            continue;
        if (g_str_equal (pspec->name, self->type_field_name) ||
            g_str_equal (pspec->name, LRG_DATA_LOADER_REF_KEY) ||
            (is_container && g_str_equal (pspec->name, self->children_key)))
            continue;
        emit_property (sv, object, pristine, pspec, mapping, path);
    }

    /* --- children --- */
    if (is_container && sv->error == NULL)
    {
        LrgDataContainer *container = LRG_DATA_CONTAINER (object);
        guint n = lrg_data_container_get_n_children (container);

        if (n > 0)
        {
            g_autoptr(GPtrArray) children = g_ptr_array_new_with_free_func (g_object_unref);
            g_autofree gchar *children_path = path_join (path, self->children_key);
            g_autoptr(YamlNode) list = NULL;

            for (i = 0; i < n; i++)
            {
                GObject *child = lrg_data_container_get_child (container, i);

                if (child != NULL)
                    g_ptr_array_add (children, g_object_ref (child));
            }
            list = emit_list (sv, children, lrg_data_container_get_child_type (container), children_path);
            if (sv->error == NULL)
                yaml_mapping_set_member (mapping, self->children_key, list);
        }
    }

    if (sv->error != NULL)
        return NULL;
    return yaml_node_new_mapping (mapping);
}

/* fills each repeated object's `ref:` with its unique id, else a pointer */
static void
apply_fixes (Save *sv)
{
    guint i;

    for (i = 0; i < sv->fixes->len; i++)
    {
        RefFix *fix = g_ptr_array_index (sv->fixes, i);
        g_autofree gchar *guid = NULL;
        g_autofree gchar *id = NULL;
        g_autofree gchar *pointer = NULL;
        const gchar *text;

        object_ids (fix->target, &guid, &id);
        if (guid != NULL && GPOINTER_TO_UINT (g_hash_table_lookup (sv->id_count, guid)) == 1)
            text = guid;
        else if (id != NULL && GPOINTER_TO_UINT (g_hash_table_lookup (sv->id_count, id)) == 1)
            text = id;
        else
            text = pointer = g_strconcat ("#", (const gchar *)g_hash_table_lookup (sv->emitted, fix->target), NULL);
        yaml_node_set_string (fix->value, text);
        yaml_node_set_scalar_style (fix->value, YAML_SCALAR_STYLE_ANY);
        if (g_str_equal (text, "~") || g_ascii_strcasecmp (text, "null") == 0)
            yaml_node_set_scalar_style (fix->value, YAML_SCALAR_STYLE_DOUBLE_QUOTED);
    }
}

YamlNode *
lrg_data_loader_save_to_node (LrgDataLoader  *self,
                              GObject        *object,
                              GError        **error)
{
    Save sv;
    YamlNode *node;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (G_IS_OBJECT (object), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    memset (&sv, 0, sizeof sv);
    sv.self = self;
    sv.emitted = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
    sv.pristine = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
    sv.id_count = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    sv.fixes = g_ptr_array_new_with_free_func (ref_fix_free);

    /* the root always names its type, so a plain load can read it back */
    node = emit_object (&sv, object, G_TYPE_INVALID, "");
    if (sv.error == NULL)
        apply_fixes (&sv);
    else
        g_clear_pointer (&node, yaml_node_unref);
    if (sv.error != NULL)
        g_propagate_error (error, sv.error);

    g_hash_table_unref (sv.emitted);
    g_hash_table_unref (sv.pristine);
    g_hash_table_unref (sv.id_count);
    g_ptr_array_unref (sv.fixes);
    return node;
}

gchar *
lrg_data_loader_save_to_data (LrgDataLoader  *self,
                              GObject        *object,
                              gsize          *length,
                              GError        **error)
{
    g_autoptr(YamlNode) node = NULL;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), NULL);
    g_return_val_if_fail (G_IS_OBJECT (object), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    node = lrg_data_loader_save_to_node (self, object, error);
    if (node == NULL)
        return NULL;
    return lrg_data_yaml_to_data (node, length, error);
}

gboolean
lrg_data_loader_save_to_file (LrgDataLoader  *self,
                              GObject        *object,
                              const gchar    *path,
                              GError        **error)
{
    g_autoptr(YamlNode) node = NULL;

    g_return_val_if_fail (LRG_IS_DATA_LOADER (self), FALSE);
    g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    node = lrg_data_loader_save_to_node (self, object, error);
    if (node == NULL)
        return FALSE;
    return lrg_data_yaml_to_file (node, path, error);
}
