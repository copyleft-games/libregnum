/* lrg-save-context.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the save context for serialization.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SAVE

#include "lrg-save-context.h"
#include "../lrg-log.h"
#include <yaml-glib.h>
#include <gio/gio.h>

struct _LrgSaveContext
{
    GObject parent_instance;

    LrgSaveContextMode mode;
    guint              version;

    /* Save mode */
    YamlBuilder  *builder;

    /* Load mode */
    YamlParser   *parser;
    YamlDocument *document;
    YamlMapping  *root_mapping;
    YamlMapping  *current_section;

    /* Section stack for nested sections */
    GQueue       *section_stack;
};

G_DEFINE_FINAL_TYPE (LrgSaveContext, lrg_save_context, G_TYPE_OBJECT)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
lrg_save_context_finalize (GObject *object)
{
    LrgSaveContext *self = LRG_SAVE_CONTEXT (object);

    g_clear_object (&self->builder);
    g_clear_object (&self->parser);

    if (self->section_stack != NULL)
    {
        g_queue_free (self->section_stack);
        self->section_stack = NULL;
    }

    G_OBJECT_CLASS (lrg_save_context_parent_class)->finalize (object);
}

static void
lrg_save_context_class_init (LrgSaveContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_save_context_finalize;
}

static void
lrg_save_context_init (LrgSaveContext *self)
{
    self->mode = LRG_SAVE_CONTEXT_MODE_SAVE;
    self->version = 1;
    self->builder = NULL;
    self->parser = NULL;
    self->document = NULL;
    self->root_mapping = NULL;
    self->current_section = NULL;
    self->section_stack = g_queue_new ();
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_save_context_new_for_save:
 *
 * Creates a new save context for saving data.
 *
 * Returns: (transfer full): A new #LrgSaveContext in save mode
 */
LrgSaveContext *
lrg_save_context_new_for_save (void)
{
    LrgSaveContext *self;

    self = g_object_new (LRG_TYPE_SAVE_CONTEXT, NULL);
    self->mode = LRG_SAVE_CONTEXT_MODE_SAVE;
    self->builder = yaml_builder_new ();

    /* Start the root mapping */
    yaml_builder_begin_mapping (self->builder);

    lrg_log_debug ("Created save context for saving");

    return self;
}

/**
 * lrg_save_context_new_for_load:
 * @data: the YAML data to load from
 * @error: (optional): return location for a #GError
 *
 * Creates a new save context for loading data from a string.
 *
 * Returns: (transfer full) (nullable): A new #LrgSaveContext in load mode
 */
LrgSaveContext *
lrg_save_context_new_for_load (const gchar  *data,
                               GError      **error)
{
    LrgSaveContext *self;
    YamlNode       *root;

    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    self = g_object_new (LRG_TYPE_SAVE_CONTEXT, NULL);
    self->mode = LRG_SAVE_CONTEXT_MODE_LOAD;

    self->parser = yaml_parser_new ();

    if (!yaml_parser_load_from_data (self->parser, data, -1, error))
    {
        g_object_unref (self);
        return NULL;
    }

    self->document = yaml_parser_get_document (self->parser, 0);
    if (self->document == NULL)
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_CORRUPT,
                     "No YAML document found in save data");
        g_object_unref (self);
        return NULL;
    }

    root = yaml_document_get_root (self->document);
    if (root == NULL)
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_CORRUPT,
                     "Save data root is empty");
        g_object_unref (self);
        return NULL;
    }

    self->root_mapping = yaml_node_get_mapping (root);
    if (self->root_mapping == NULL)
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_CORRUPT,
                     "Save data root is not a mapping");
        g_object_unref (self);
        return NULL;
    }
    self->current_section = self->root_mapping;

    /* Read version if present */
    self->version = (guint) yaml_mapping_get_int_member (self->root_mapping, "version");
    if (self->version == 0)
    {
        self->version = 1;  /* Default to version 1 */
    }

    lrg_log_debug ("Created save context for loading (version %u)", self->version);

    return self;
}

/**
 * lrg_save_context_new_from_file:
 * @path: path to the YAML file to load
 * @error: (optional): return location for a #GError
 *
 * Creates a new save context for loading data from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgSaveContext in load mode
 */
LrgSaveContext *
lrg_save_context_new_from_file (const gchar  *path,
                                GError      **error)
{
    g_autofree gchar *contents = NULL;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_file_get_contents (path, &contents, NULL, error))
    {
        return NULL;
    }

    return lrg_save_context_new_for_load (contents, error);
}

/* ==========================================================================
 * Mode and Version
 * ========================================================================== */

/**
 * lrg_save_context_get_mode:
 * @self: a #LrgSaveContext
 *
 * Gets the mode of this context.
 *
 * Returns: the context mode
 */
LrgSaveContextMode
lrg_save_context_get_mode (LrgSaveContext *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), LRG_SAVE_CONTEXT_MODE_SAVE);

    return self->mode;
}

/**
 * lrg_save_context_get_version:
 * @self: a #LrgSaveContext
 *
 * Gets the save format version.
 *
 * Returns: the version number
 */
guint
lrg_save_context_get_version (LrgSaveContext *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), 0);

    return self->version;
}

/**
 * lrg_save_context_set_version:
 * @self: a #LrgSaveContext
 * @version: the version number
 *
 * Sets the save format version.
 */
void
lrg_save_context_set_version (LrgSaveContext *self,
                              guint           version)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);

    self->version = version;
}

/* ==========================================================================
 * Section Management
 * ========================================================================== */

/**
 * lrg_save_context_begin_section:
 * @self: a #LrgSaveContext
 * @name: the section name
 *
 * Begins a named section for saving.
 */
void
lrg_save_context_begin_section (LrgSaveContext *self,
                                const gchar    *name)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);
    g_return_if_fail (name != NULL);
    g_return_if_fail (self->builder != NULL);

    yaml_builder_set_member_name (self->builder, name);
    yaml_builder_begin_mapping (self->builder);

    lrg_log_debug ("Began section '%s'", name);
}

/**
 * lrg_save_context_end_section:
 * @self: a #LrgSaveContext
 *
 * Ends the current section.
 */
void
lrg_save_context_end_section (LrgSaveContext *self)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);
    g_return_if_fail (self->builder != NULL);

    yaml_builder_end_mapping (self->builder);

    lrg_log_debug ("Ended section");
}

/**
 * lrg_save_context_has_section:
 * @self: a #LrgSaveContext
 * @name: the section name to check
 *
 * Checks if a section exists in the loaded data.
 *
 * Returns: %TRUE if the section exists
 */
gboolean
lrg_save_context_has_section (LrgSaveContext *self,
                              const gchar    *name)
{
    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), FALSE);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (self->root_mapping != NULL, FALSE);

    return yaml_mapping_has_member (self->root_mapping, name);
}

/**
 * lrg_save_context_enter_section:
 * @self: a #LrgSaveContext
 * @name: the section name to enter
 *
 * Enters a named section for loading.
 *
 * Returns: %TRUE if the section was found and entered
 */
gboolean
lrg_save_context_enter_section (LrgSaveContext *self,
                                const gchar    *name)
{
    YamlMapping *section;

    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), FALSE);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (self->current_section != NULL, FALSE);

    section = yaml_mapping_get_mapping_member (self->current_section, name);
    if (section == NULL)
    {
        return FALSE;
    }

    /* Push current section onto stack and enter new section */
    g_queue_push_head (self->section_stack, self->current_section);
    self->current_section = section;

    lrg_log_debug ("Entered section '%s'", name);

    return TRUE;
}

/**
 * lrg_save_context_leave_section:
 * @self: a #LrgSaveContext
 *
 * Leaves the current section.
 */
void
lrg_save_context_leave_section (LrgSaveContext *self)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD);
    g_return_if_fail (!g_queue_is_empty (self->section_stack));

    self->current_section = g_queue_pop_head (self->section_stack);

    lrg_log_debug ("Left section");
}

/* ==========================================================================
 * Writing (Save Mode)
 * ========================================================================== */

/**
 * lrg_save_context_write_string:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the string value
 *
 * Writes a string value.
 */
void
lrg_save_context_write_string (LrgSaveContext *self,
                               const gchar    *key,
                               const gchar    *value)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);
    g_return_if_fail (key != NULL);
    g_return_if_fail (self->builder != NULL);

    yaml_builder_set_member_name (self->builder, key);
    yaml_builder_add_string_value (self->builder, value != NULL ? value : "");
}

/**
 * lrg_save_context_write_int:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the integer value
 *
 * Writes an integer value.
 */
void
lrg_save_context_write_int (LrgSaveContext *self,
                            const gchar    *key,
                            gint64          value)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);
    g_return_if_fail (key != NULL);
    g_return_if_fail (self->builder != NULL);

    yaml_builder_set_member_name (self->builder, key);
    yaml_builder_add_int_value (self->builder, value);
}

/**
 * lrg_save_context_write_uint:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the unsigned integer value
 *
 * Writes an unsigned integer value.
 */
void
lrg_save_context_write_uint (LrgSaveContext *self,
                             const gchar    *key,
                             guint64         value)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);
    g_return_if_fail (key != NULL);
    g_return_if_fail (self->builder != NULL);

    /* YAML doesn't distinguish signed/unsigned, so cast to signed */
    yaml_builder_set_member_name (self->builder, key);
    yaml_builder_add_int_value (self->builder, (gint64) value);
}

/**
 * lrg_save_context_write_double:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the double value
 *
 * Writes a double value.
 */
void
lrg_save_context_write_double (LrgSaveContext *self,
                               const gchar    *key,
                               gdouble         value)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);
    g_return_if_fail (key != NULL);
    g_return_if_fail (self->builder != NULL);

    yaml_builder_set_member_name (self->builder, key);
    yaml_builder_add_double_value (self->builder, value);
}

/**
 * lrg_save_context_write_boolean:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the boolean value
 *
 * Writes a boolean value.
 */
void
lrg_save_context_write_boolean (LrgSaveContext *self,
                                const gchar    *key,
                                gboolean        value)
{
    g_return_if_fail (LRG_IS_SAVE_CONTEXT (self));
    g_return_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE);
    g_return_if_fail (key != NULL);
    g_return_if_fail (self->builder != NULL);

    yaml_builder_set_member_name (self->builder, key);
    yaml_builder_add_boolean_value (self->builder, value);
}

/* ==========================================================================
 * Reading (Load Mode)
 * ========================================================================== */

/**
 * lrg_save_context_read_string:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads a string value.
 *
 * Returns: (transfer none) (nullable): the string value or @default_value
 */
const gchar *
lrg_save_context_read_string (LrgSaveContext *self,
                              const gchar    *key,
                              const gchar    *default_value)
{
    const gchar *value;

    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), default_value);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, default_value);
    g_return_val_if_fail (key != NULL, default_value);
    g_return_val_if_fail (self->current_section != NULL, default_value);

    if (!yaml_mapping_has_member (self->current_section, key))
    {
        return default_value;
    }

    value = yaml_mapping_get_string_member (self->current_section, key);
    return value != NULL ? value : default_value;
}

/**
 * lrg_save_context_read_int:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads an integer value.
 *
 * Returns: the integer value or @default_value
 */
gint64
lrg_save_context_read_int (LrgSaveContext *self,
                           const gchar    *key,
                           gint64          default_value)
{
    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), default_value);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, default_value);
    g_return_val_if_fail (key != NULL, default_value);
    g_return_val_if_fail (self->current_section != NULL, default_value);

    if (!yaml_mapping_has_member (self->current_section, key))
    {
        return default_value;
    }

    return yaml_mapping_get_int_member (self->current_section, key);
}

/**
 * lrg_save_context_read_uint:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads an unsigned integer value.
 *
 * Returns: the unsigned integer value or @default_value
 */
guint64
lrg_save_context_read_uint (LrgSaveContext *self,
                            const gchar    *key,
                            guint64         default_value)
{
    gint64 value;

    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), default_value);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, default_value);
    g_return_val_if_fail (key != NULL, default_value);
    g_return_val_if_fail (self->current_section != NULL, default_value);

    if (!yaml_mapping_has_member (self->current_section, key))
    {
        return default_value;
    }

    value = yaml_mapping_get_int_member (self->current_section, key);
    return (guint64) value;
}

/**
 * lrg_save_context_read_double:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads a double value.
 *
 * Returns: the double value or @default_value
 */
gdouble
lrg_save_context_read_double (LrgSaveContext *self,
                              const gchar    *key,
                              gdouble         default_value)
{
    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), default_value);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, default_value);
    g_return_val_if_fail (key != NULL, default_value);
    g_return_val_if_fail (self->current_section != NULL, default_value);

    if (!yaml_mapping_has_member (self->current_section, key))
    {
        return default_value;
    }

    return yaml_mapping_get_double_member (self->current_section, key);
}

/**
 * lrg_save_context_read_boolean:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads a boolean value.
 *
 * Returns: the boolean value or @default_value
 */
gboolean
lrg_save_context_read_boolean (LrgSaveContext *self,
                               const gchar    *key,
                               gboolean        default_value)
{
    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), default_value);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, default_value);
    g_return_val_if_fail (key != NULL, default_value);
    g_return_val_if_fail (self->current_section != NULL, default_value);

    if (!yaml_mapping_has_member (self->current_section, key))
    {
        return default_value;
    }

    return yaml_mapping_get_boolean_member (self->current_section, key);
}

/**
 * lrg_save_context_has_key:
 * @self: a #LrgSaveContext
 * @key: the key name to check
 *
 * Checks if a key exists in the current section.
 *
 * Returns: %TRUE if the key exists
 */
gboolean
lrg_save_context_has_key (LrgSaveContext *self,
                          const gchar    *key)
{
    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), FALSE);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_LOAD, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);
    g_return_val_if_fail (self->current_section != NULL, FALSE);

    return yaml_mapping_has_member (self->current_section, key);
}

/* ==========================================================================
 * Output (Save Mode)
 * ========================================================================== */

/**
 * lrg_save_context_to_string:
 * @self: a #LrgSaveContext
 * @error: (optional): return location for a #GError
 *
 * Generates the YAML string from the save context.
 *
 * Returns: (transfer full) (nullable): the YAML string
 */
gchar *
lrg_save_context_to_string (LrgSaveContext  *self,
                            GError         **error)
{
    g_autoptr(YamlGenerator) generator = NULL;
    YamlDocument             *doc;
    gchar                    *yaml_str;

    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), NULL);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE, NULL);
    g_return_val_if_fail (self->builder != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Add version at the start */
    yaml_builder_set_member_name (self->builder, "version");
    yaml_builder_add_int_value (self->builder, (gint64) self->version);

    /* End the root mapping */
    yaml_builder_end_mapping (self->builder);

    doc = yaml_builder_get_document (self->builder);
    if (doc == NULL)
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_FAILED,
                     "Failed to build YAML document");
        return NULL;
    }

    generator = yaml_generator_new ();
    yaml_generator_set_document (generator, doc);
    yaml_str = yaml_generator_to_data (generator, NULL, error);

    return yaml_str;
}

/**
 * lrg_save_context_to_file:
 * @self: a #LrgSaveContext
 * @path: the file path to write to
 * @error: (optional): return location for a #GError
 *
 * Writes the save context to a file.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_save_context_to_file (LrgSaveContext  *self,
                          const gchar     *path,
                          GError         **error)
{
    g_autofree gchar *yaml_str = NULL;

    g_return_val_if_fail (LRG_IS_SAVE_CONTEXT (self), FALSE);
    g_return_val_if_fail (self->mode == LRG_SAVE_CONTEXT_MODE_SAVE, FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    yaml_str = lrg_save_context_to_string (self, error);
    if (yaml_str == NULL)
    {
        return FALSE;
    }

    if (!g_file_set_contents (path, yaml_str, -1, error))
    {
        return FALSE;
    }

    lrg_log_info ("Saved context to %s", path);

    return TRUE;
}
