/* lrg-locale.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of locale object for internationalization.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_I18N

#include "lrg-locale.h"
#include "lrg-log.h"
#include <yaml-glib.h>

typedef struct
{
    gchar       *code;
    gchar       *name;
    GHashTable  *strings;   /* key -> string */
    GHashTable  *plurals;   /* key -> GHashTable<form_key, string> */
} LrgLocalePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgLocale, lrg_locale, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CODE,
    PROP_NAME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ========================================================================== */
/* Private Helpers                                                            */
/* ========================================================================== */

/**
 * plural_table_free:
 * @data: a plural form hash table
 *
 * Frees a plural form hash table.
 */
static void
plural_table_free (gpointer data)
{
    if (data != NULL)
        g_hash_table_unref ((GHashTable *)data);
}

/**
 * get_plural_form_key:
 * @form: a plural form
 *
 * Gets the string key for a plural form.
 *
 * Returns: the form key string
 */
static const gchar *
get_plural_form_key (LrgPluralForm form)
{
    switch (form)
    {
    case LRG_PLURAL_ZERO:
        return "zero";
    case LRG_PLURAL_ONE:
        return "one";
    case LRG_PLURAL_TWO:
        return "two";
    case LRG_PLURAL_FEW:
        return "few";
    case LRG_PLURAL_MANY:
        return "many";
    case LRG_PLURAL_OTHER:
    default:
        return "other";
    }
}

/**
 * parse_plural_form:
 * @key: the form key string
 *
 * Parses a plural form from a string key.
 *
 * Returns: the plural form, or LRG_PLURAL_OTHER for unknown
 */
static LrgPluralForm
parse_plural_form (const gchar *key)
{
    if (g_str_equal (key, "zero"))
        return LRG_PLURAL_ZERO;
    if (g_str_equal (key, "one"))
        return LRG_PLURAL_ONE;
    if (g_str_equal (key, "two"))
        return LRG_PLURAL_TWO;
    if (g_str_equal (key, "few"))
        return LRG_PLURAL_FEW;
    if (g_str_equal (key, "many"))
        return LRG_PLURAL_MANY;
    return LRG_PLURAL_OTHER;
}

/**
 * default_get_plural_form:
 * @self: the locale
 * @count: the count
 *
 * Default English plural rules (one vs other).
 *
 * Returns: the plural form
 */
static LrgPluralForm
default_get_plural_form (LrgLocale *self,
                         gint       count)
{
    (void)self;

    if (count == 1 || count == -1)
        return LRG_PLURAL_ONE;
    return LRG_PLURAL_OTHER;
}

/* ========================================================================== */
/* YAML Loading                                                               */
/* ========================================================================== */

/**
 * load_strings_from_mapping:
 * @self: the locale
 * @mapping: the YAML mapping containing strings
 *
 * Loads strings from a YAML mapping.
 * Handles both simple strings and plural form mappings.
 */
static void
load_strings_from_mapping (LrgLocale   *self,
                           YamlMapping *mapping)
{
    guint size;
    guint i;

    g_return_if_fail (LRG_IS_LOCALE (self));
    g_return_if_fail (mapping != NULL);

    size = yaml_mapping_get_size (mapping);

    for (i = 0; i < size; i++)
    {
        const gchar *key;
        YamlNode *value_node;
        YamlNodeType value_type;

        key = yaml_mapping_get_key (mapping, i);
        value_node = yaml_mapping_get_value (mapping, i);

        if (key == NULL || value_node == NULL)
            continue;

        value_type = yaml_node_get_node_type (value_node);

        if (value_type == YAML_NODE_SCALAR)
        {
            /* Simple string value */
            const gchar *value;
            value = yaml_node_get_string (value_node);
            if (value != NULL)
                lrg_locale_set_string (self, key, value);
        }
        else if (value_type == YAML_NODE_MAPPING)
        {
            /* Plural forms mapping */
            YamlMapping *plural_mapping;
            guint plural_size;
            guint j;

            plural_mapping = yaml_node_get_mapping (value_node);
            if (plural_mapping == NULL)
                continue;

            plural_size = yaml_mapping_get_size (plural_mapping);

            for (j = 0; j < plural_size; j++)
            {
                const gchar *form_key;
                YamlNode *form_node;
                const gchar *form_value;
                LrgPluralForm form;

                form_key = yaml_mapping_get_key (plural_mapping, j);
                form_node = yaml_mapping_get_value (plural_mapping, j);

                if (form_key == NULL || form_node == NULL)
                    continue;

                if (yaml_node_get_node_type (form_node) != YAML_NODE_SCALAR)
                    continue;

                form_value = yaml_node_get_string (form_node);
                if (form_value == NULL)
                    continue;

                form = parse_plural_form (form_key);
                lrg_locale_set_plural (self, key, form, form_value);
            }
        }
    }
}

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
lrg_locale_finalize (GObject *object)
{
    LrgLocale *self;
    LrgLocalePrivate *priv;

    self = LRG_LOCALE (object);
    priv = lrg_locale_get_instance_private (self);

    g_clear_pointer (&priv->code, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->strings, g_hash_table_unref);
    g_clear_pointer (&priv->plurals, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_locale_parent_class)->finalize (object);
}

static void
lrg_locale_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LrgLocale *self;
    LrgLocalePrivate *priv;

    self = LRG_LOCALE (object);
    priv = lrg_locale_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CODE:
        g_value_set_string (value, priv->code);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_locale_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LrgLocale *self;
    LrgLocalePrivate *priv;

    self = LRG_LOCALE (object);
    priv = lrg_locale_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CODE:
        g_free (priv->code);
        priv->code = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_locale_class_init (LrgLocaleClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_locale_finalize;
    object_class->get_property = lrg_locale_get_property;
    object_class->set_property = lrg_locale_set_property;

    /* Virtual method default */
    klass->get_plural_form = default_get_plural_form;

    /**
     * LrgLocale:code:
     *
     * The locale code (e.g., "en", "en_US", "de_DE").
     */
    properties[PROP_CODE] =
        g_param_spec_string ("code",
                             "Code",
                             "Locale code",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgLocale:name:
     *
     * The human-readable locale name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Human-readable locale name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_locale_init (LrgLocale *self)
{
    LrgLocalePrivate *priv;

    priv = lrg_locale_get_instance_private (self);

    priv->strings = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_free);
    priv->plurals = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, plural_table_free);
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * lrg_locale_new:
 * @code: locale code (e.g., "en", "en_US", "de_DE")
 * @name: human-readable name (e.g., "English", "Deutsch")
 *
 * Creates a new empty locale.
 *
 * Returns: (transfer full): a new #LrgLocale
 */
LrgLocale *
lrg_locale_new (const gchar *code,
                const gchar *name)
{
    g_return_val_if_fail (code != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_LOCALE,
                         "code", code,
                         "name", name,
                         NULL);
}

/**
 * lrg_locale_new_from_file:
 * @path: path to YAML locale file
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new locale by loading strings from a YAML file.
 *
 * Returns: (transfer full) (nullable): a new #LrgLocale, or %NULL on error
 */
LrgLocale *
lrg_locale_new_from_file (const gchar  *path,
                          GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlNode *root = NULL;
    YamlMapping *root_mapping = NULL;
    const gchar *code = NULL;
    const gchar *name = NULL;
    YamlNode *strings_node = NULL;
    LrgLocale *locale = NULL;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Create parser and load file */
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, error))
        return NULL;

    root = yaml_parser_get_root (parser);
    if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        g_set_error (error, LRG_I18N_ERROR, LRG_I18N_ERROR_PARSE,
                     "Locale file must contain a YAML mapping");
        return NULL;
    }

    root_mapping = yaml_node_get_mapping (root);
    if (root_mapping == NULL)
    {
        g_set_error (error, LRG_I18N_ERROR, LRG_I18N_ERROR_PARSE,
                     "Failed to get root mapping");
        return NULL;
    }

    /* Get required fields */
    code = yaml_mapping_get_string_member (root_mapping, "code");
    if (code == NULL)
    {
        g_set_error (error, LRG_I18N_ERROR, LRG_I18N_ERROR_PARSE,
                     "Locale file missing 'code' field");
        return NULL;
    }

    name = yaml_mapping_get_string_member (root_mapping, "name");
    if (name == NULL)
    {
        g_set_error (error, LRG_I18N_ERROR, LRG_I18N_ERROR_PARSE,
                     "Locale file missing 'name' field");
        return NULL;
    }

    /* Create locale */
    locale = lrg_locale_new (code, name);

    /* Load strings if present */
    strings_node = yaml_mapping_get_member (root_mapping, "strings");
    if (strings_node != NULL && yaml_node_get_node_type (strings_node) == YAML_NODE_MAPPING)
    {
        YamlMapping *strings_mapping;
        strings_mapping = yaml_node_get_mapping (strings_node);
        if (strings_mapping != NULL)
            load_strings_from_mapping (locale, strings_mapping);
    }

    lrg_log_debug ("Loaded locale '%s' (%s) with %u strings from %s",
                   code, name, lrg_locale_get_string_count (locale), path);

    return locale;
}

/**
 * lrg_locale_get_code:
 * @self: a #LrgLocale
 *
 * Gets the locale code.
 *
 * Returns: (transfer none): the locale code
 */
const gchar *
lrg_locale_get_code (LrgLocale *self)
{
    LrgLocalePrivate *priv;

    g_return_val_if_fail (LRG_IS_LOCALE (self), NULL);

    priv = lrg_locale_get_instance_private (self);
    return priv->code;
}

/**
 * lrg_locale_get_name:
 * @self: a #LrgLocale
 *
 * Gets the human-readable locale name.
 *
 * Returns: (transfer none): the locale name
 */
const gchar *
lrg_locale_get_name (LrgLocale *self)
{
    LrgLocalePrivate *priv;

    g_return_val_if_fail (LRG_IS_LOCALE (self), NULL);

    priv = lrg_locale_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_locale_set_string:
 * @self: a #LrgLocale
 * @key: the string key
 * @value: the localized string value
 *
 * Sets a localized string for the given key.
 */
void
lrg_locale_set_string (LrgLocale   *self,
                       const gchar *key,
                       const gchar *value)
{
    LrgLocalePrivate *priv;

    g_return_if_fail (LRG_IS_LOCALE (self));
    g_return_if_fail (key != NULL);
    g_return_if_fail (value != NULL);

    priv = lrg_locale_get_instance_private (self);
    g_hash_table_replace (priv->strings, g_strdup (key), g_strdup (value));
}

/**
 * lrg_locale_get_string:
 * @self: a #LrgLocale
 * @key: the string key to look up
 *
 * Gets a localized string by key.
 *
 * Returns: (transfer none) (nullable): the localized string, or %NULL if not found
 */
const gchar *
lrg_locale_get_string (LrgLocale   *self,
                       const gchar *key)
{
    LrgLocalePrivate *priv;

    g_return_val_if_fail (LRG_IS_LOCALE (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    priv = lrg_locale_get_instance_private (self);
    return (const gchar *)g_hash_table_lookup (priv->strings, key);
}

/**
 * lrg_locale_set_plural:
 * @self: a #LrgLocale
 * @key: the base string key
 * @form: the plural form
 * @value: the localized string for this form
 *
 * Sets a pluralized string for the given key and form.
 */
void
lrg_locale_set_plural (LrgLocale     *self,
                       const gchar   *key,
                       LrgPluralForm  form,
                       const gchar   *value)
{
    LrgLocalePrivate *priv;
    GHashTable *forms;
    const gchar *form_key;

    g_return_if_fail (LRG_IS_LOCALE (self));
    g_return_if_fail (key != NULL);
    g_return_if_fail (value != NULL);

    priv = lrg_locale_get_instance_private (self);

    /* Get or create forms table for this key */
    forms = (GHashTable *)g_hash_table_lookup (priv->plurals, key);
    if (forms == NULL)
    {
        forms = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
        g_hash_table_insert (priv->plurals, g_strdup (key), forms);
    }

    form_key = get_plural_form_key (form);
    g_hash_table_replace (forms, (gpointer)form_key, g_strdup (value));
}

/**
 * lrg_locale_get_plural:
 * @self: a #LrgLocale
 * @key: the base string key
 * @count: the count to pluralize for
 *
 * Gets the appropriate pluralized string for the given count.
 * Falls back to "other" form if specific form not found.
 *
 * Returns: (transfer none) (nullable): the pluralized string, or %NULL if not found
 */
const gchar *
lrg_locale_get_plural (LrgLocale   *self,
                       const gchar *key,
                       gint         count)
{
    LrgLocalePrivate *priv;
    GHashTable *forms;
    LrgPluralForm form;
    const gchar *form_key;
    const gchar *result;

    g_return_val_if_fail (LRG_IS_LOCALE (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    priv = lrg_locale_get_instance_private (self);

    forms = (GHashTable *)g_hash_table_lookup (priv->plurals, key);
    if (forms == NULL)
        return NULL;

    /* Get the appropriate plural form */
    form = lrg_locale_get_plural_form (self, count);
    form_key = get_plural_form_key (form);

    result = (const gchar *)g_hash_table_lookup (forms, form_key);
    if (result == NULL)
    {
        /* Fall back to "other" form */
        result = (const gchar *)g_hash_table_lookup (forms, "other");
    }

    return result;
}

/**
 * lrg_locale_has_string:
 * @self: a #LrgLocale
 * @key: the string key to check
 *
 * Checks if the locale has a string for the given key.
 *
 * Returns: %TRUE if the key exists
 */
gboolean
lrg_locale_has_string (LrgLocale   *self,
                       const gchar *key)
{
    LrgLocalePrivate *priv;

    g_return_val_if_fail (LRG_IS_LOCALE (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    priv = lrg_locale_get_instance_private (self);

    /* Check both regular strings and plural keys */
    return g_hash_table_contains (priv->strings, key) ||
           g_hash_table_contains (priv->plurals, key);
}

/**
 * lrg_locale_get_plural_form:
 * @self: a #LrgLocale
 * @count: the count
 *
 * Gets the plural form to use for a given count.
 * Uses English plural rules by default (one vs other).
 *
 * Returns: the plural form
 */
LrgPluralForm
lrg_locale_get_plural_form (LrgLocale *self,
                            gint       count)
{
    LrgLocaleClass *klass;

    g_return_val_if_fail (LRG_IS_LOCALE (self), LRG_PLURAL_OTHER);

    klass = LRG_LOCALE_GET_CLASS (self);
    if (klass->get_plural_form != NULL)
        return klass->get_plural_form (self, count);

    return default_get_plural_form (self, count);
}

/**
 * lrg_locale_get_string_count:
 * @self: a #LrgLocale
 *
 * Gets the number of strings in the locale.
 *
 * Returns: the number of strings
 */
guint
lrg_locale_get_string_count (LrgLocale *self)
{
    LrgLocalePrivate *priv;

    g_return_val_if_fail (LRG_IS_LOCALE (self), 0);

    priv = lrg_locale_get_instance_private (self);
    return g_hash_table_size (priv->strings) + g_hash_table_size (priv->plurals);
}

/**
 * lrg_locale_get_keys:
 * @self: a #LrgLocale
 *
 * Gets all string keys in the locale.
 *
 * Returns: (transfer container) (element-type utf8): array of keys
 */
GPtrArray *
lrg_locale_get_keys (LrgLocale *self)
{
    LrgLocalePrivate *priv;
    GPtrArray *keys;
    GHashTableIter iter;
    gpointer key_ptr;

    g_return_val_if_fail (LRG_IS_LOCALE (self), NULL);

    priv = lrg_locale_get_instance_private (self);
    keys = g_ptr_array_new ();

    /* Add regular string keys */
    g_hash_table_iter_init (&iter, priv->strings);
    while (g_hash_table_iter_next (&iter, &key_ptr, NULL))
    {
        g_ptr_array_add (keys, key_ptr);
    }

    /* Add plural keys */
    g_hash_table_iter_init (&iter, priv->plurals);
    while (g_hash_table_iter_next (&iter, &key_ptr, NULL))
    {
        g_ptr_array_add (keys, key_ptr);
    }

    return keys;
}
