/* lrg-mod-manifest.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod manifest implementation.
 */

#include "config.h"
#include "lrg-mod-manifest.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

#include <yaml-glib.h>
#include <string.h>

/* ==========================================================================
 * Mod Dependency
 * ========================================================================== */

struct _LrgModDependency
{
    gchar    *mod_id;
    gchar    *min_version;
    gboolean  optional;
};

LrgModDependency *
lrg_mod_dependency_new (const gchar *mod_id,
                        const gchar *min_version,
                        gboolean     optional)
{
    LrgModDependency *dep;

    g_return_val_if_fail (mod_id != NULL, NULL);

    dep = g_slice_new0 (LrgModDependency);
    dep->mod_id = g_strdup (mod_id);
    dep->min_version = g_strdup (min_version);
    dep->optional = optional;

    return dep;
}

LrgModDependency *
lrg_mod_dependency_copy (const LrgModDependency *self)
{
    if (self == NULL)
        return NULL;

    return lrg_mod_dependency_new (self->mod_id,
                                   self->min_version,
                                   self->optional);
}

void
lrg_mod_dependency_free (LrgModDependency *self)
{
    if (self == NULL)
        return;

    g_free (self->mod_id);
    g_free (self->min_version);
    g_slice_free (LrgModDependency, self);
}

const gchar *
lrg_mod_dependency_get_mod_id (const LrgModDependency *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->mod_id;
}

const gchar *
lrg_mod_dependency_get_min_version (const LrgModDependency *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->min_version;
}

gboolean
lrg_mod_dependency_is_optional (const LrgModDependency *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->optional;
}

G_DEFINE_BOXED_TYPE (LrgModDependency, lrg_mod_dependency,
                     lrg_mod_dependency_copy,
                     lrg_mod_dependency_free)

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgModManifest
{
    GObject        parent_instance;

    /* Identity */
    gchar         *id;
    gchar         *name;
    gchar         *version;
    gchar         *description;
    gchar         *author;

    /* Type and priority */
    LrgModType     type;
    LrgModPriority priority;

    /* Dependencies and load order */
    GPtrArray     *dependencies;   /* LrgModDependency */
    GPtrArray     *load_after;     /* gchar* */
    GPtrArray     *load_before;    /* gchar* */

    /* Paths */
    gchar         *data_path;
    gchar         *entry_point;
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgModManifest, lrg_mod_manifest, G_TYPE_OBJECT)
#pragma GCC visibility pop

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mod_manifest_finalize (GObject *object)
{
    LrgModManifest *self = LRG_MOD_MANIFEST (object);

    g_free (self->id);
    g_free (self->name);
    g_free (self->version);
    g_free (self->description);
    g_free (self->author);
    g_free (self->data_path);
    g_free (self->entry_point);

    g_ptr_array_unref (self->dependencies);
    g_ptr_array_unref (self->load_after);
    g_ptr_array_unref (self->load_before);

    G_OBJECT_CLASS (lrg_mod_manifest_parent_class)->finalize (object);
}

static void
lrg_mod_manifest_class_init (LrgModManifestClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_mod_manifest_finalize;
}

static void
lrg_mod_manifest_init (LrgModManifest *self)
{
    self->type = LRG_MOD_TYPE_DATA;
    self->priority = LRG_MOD_PRIORITY_NORMAL;

    self->dependencies = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_mod_dependency_free);
    self->load_after = g_ptr_array_new_with_free_func (g_free);
    self->load_before = g_ptr_array_new_with_free_func (g_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgModManifest *
lrg_mod_manifest_new (const gchar *mod_id)
{
    LrgModManifest *manifest;

    g_return_val_if_fail (mod_id != NULL, NULL);

    manifest = g_object_new (LRG_TYPE_MOD_MANIFEST, NULL);
    manifest->id = g_strdup (mod_id);

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Created manifest for mod: %s", mod_id);

    return manifest;
}

static gboolean
parse_manifest_yaml (LrgModManifest *manifest,
                     YamlNode       *root,
                     GError        **error)
{
    YamlMapping *root_map;
    YamlNode *node;
    YamlNode *deps_node;
    YamlNode *order_node;
    guint i;

    if (yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_INVALID_MANIFEST,
                     "Manifest root must be a mapping");
        return FALSE;
    }

    root_map = yaml_node_get_mapping (root);

    /* Required: id */
    node = yaml_mapping_get_member (root_map, "id");
    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_INVALID_MANIFEST,
                     "Manifest must have 'id' field");
        return FALSE;
    }
    manifest->id = g_strdup (yaml_node_get_string (node));

    /* Optional fields */
    node = yaml_mapping_get_member (root_map, "name");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
        manifest->name = g_strdup (yaml_node_get_string (node));

    node = yaml_mapping_get_member (root_map, "version");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
        manifest->version = g_strdup (yaml_node_get_string (node));

    node = yaml_mapping_get_member (root_map, "description");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
        manifest->description = g_strdup (yaml_node_get_string (node));

    node = yaml_mapping_get_member (root_map, "author");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
        manifest->author = g_strdup (yaml_node_get_string (node));

    /* Type */
    node = yaml_mapping_get_member (root_map, "type");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
    {
        const gchar *type_str = yaml_node_get_string (node);
        if (g_strcmp0 (type_str, "data") == 0)
            manifest->type = LRG_MOD_TYPE_DATA;
        else if (g_strcmp0 (type_str, "script") == 0)
            manifest->type = LRG_MOD_TYPE_SCRIPT;
        else if (g_strcmp0 (type_str, "native") == 0)
            manifest->type = LRG_MOD_TYPE_NATIVE;
    }

    /* Priority */
    node = yaml_mapping_get_member (root_map, "priority");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
    {
        const gchar *prio_str = yaml_node_get_string (node);
        if (g_strcmp0 (prio_str, "lowest") == 0)
            manifest->priority = LRG_MOD_PRIORITY_LOWEST;
        else if (g_strcmp0 (prio_str, "low") == 0)
            manifest->priority = LRG_MOD_PRIORITY_LOW;
        else if (g_strcmp0 (prio_str, "normal") == 0)
            manifest->priority = LRG_MOD_PRIORITY_NORMAL;
        else if (g_strcmp0 (prio_str, "high") == 0)
            manifest->priority = LRG_MOD_PRIORITY_HIGH;
        else if (g_strcmp0 (prio_str, "highest") == 0)
            manifest->priority = LRG_MOD_PRIORITY_HIGHEST;
        else
            manifest->priority = (LrgModPriority)g_ascii_strtoll (prio_str, NULL, 10);
    }

    /* Paths */
    node = yaml_mapping_get_member (root_map, "data_path");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
        manifest->data_path = g_strdup (yaml_node_get_string (node));

    node = yaml_mapping_get_member (root_map, "entry_point");
    if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
        manifest->entry_point = g_strdup (yaml_node_get_string (node));

    /* Dependencies */
    deps_node = yaml_mapping_get_member (root_map, "dependencies");
    if (deps_node != NULL && yaml_node_get_node_type (deps_node) == YAML_NODE_SEQUENCE)
    {
        YamlSequence *deps_seq = yaml_node_get_sequence (deps_node);
        guint len = yaml_sequence_get_length (deps_seq);

        for (i = 0; i < len; i++)
        {
            YamlNode *dep_node = yaml_sequence_get_element (deps_seq, i);
            const gchar *mod_id = NULL;
            const gchar *min_version = NULL;
            gboolean optional = FALSE;

            if (yaml_node_get_node_type (dep_node) == YAML_NODE_SCALAR)
            {
                mod_id = yaml_node_get_string (dep_node);
            }
            else if (yaml_node_get_node_type (dep_node) == YAML_NODE_MAPPING)
            {
                YamlMapping *dep_map = yaml_node_get_mapping (dep_node);
                YamlNode *val;

                val = yaml_mapping_get_member (dep_map, "id");
                if (val != NULL && yaml_node_get_node_type (val) == YAML_NODE_SCALAR)
                    mod_id = yaml_node_get_string (val);

                val = yaml_mapping_get_member (dep_map, "min_version");
                if (val != NULL && yaml_node_get_node_type (val) == YAML_NODE_SCALAR)
                    min_version = yaml_node_get_string (val);

                val = yaml_mapping_get_member (dep_map, "optional");
                if (val != NULL && yaml_node_get_node_type (val) == YAML_NODE_SCALAR)
                    optional = g_strcmp0 (yaml_node_get_string (val), "true") == 0;
            }

            if (mod_id != NULL)
            {
                lrg_mod_manifest_add_dependency (manifest, mod_id,
                                                  min_version, optional);
            }
        }
    }

    /* Load order */
    order_node = yaml_mapping_get_member (root_map, "load_after");
    if (order_node != NULL && yaml_node_get_node_type (order_node) == YAML_NODE_SEQUENCE)
    {
        YamlSequence *order_seq = yaml_node_get_sequence (order_node);
        guint len = yaml_sequence_get_length (order_seq);

        for (i = 0; i < len; i++)
        {
            YamlNode *item = yaml_sequence_get_element (order_seq, i);
            if (yaml_node_get_node_type (item) == YAML_NODE_SCALAR)
                lrg_mod_manifest_add_load_after (manifest, yaml_node_get_string (item));
        }
    }

    order_node = yaml_mapping_get_member (root_map, "load_before");
    if (order_node != NULL && yaml_node_get_node_type (order_node) == YAML_NODE_SEQUENCE)
    {
        YamlSequence *order_seq = yaml_node_get_sequence (order_node);
        guint len = yaml_sequence_get_length (order_seq);

        for (i = 0; i < len; i++)
        {
            YamlNode *item = yaml_sequence_get_element (order_seq, i);
            if (yaml_node_get_node_type (item) == YAML_NODE_SCALAR)
                lrg_mod_manifest_add_load_before (manifest, yaml_node_get_string (item));
        }
    }

    return TRUE;
}

LrgModManifest *
lrg_mod_manifest_new_from_file (const gchar  *path,
                                GError      **error)
{
    g_autoptr(LrgModManifest) manifest = NULL;
    g_autoptr(YamlParser) parser = NULL;
    YamlNode *root;

    g_return_val_if_fail (path != NULL, NULL);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, error))
        return NULL;

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_INVALID_MANIFEST,
                     "Empty manifest file: %s", path);
        return NULL;
    }

    manifest = g_object_new (LRG_TYPE_MOD_MANIFEST, NULL);

    if (!parse_manifest_yaml (manifest, root, error))
        return NULL;

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Loaded manifest from: %s", path);

    return g_steal_pointer (&manifest);
}

/* ==========================================================================
 * Identity
 * ========================================================================== */

const gchar *
lrg_mod_manifest_get_id (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->id;
}

const gchar *
lrg_mod_manifest_get_name (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->name;
}

void
lrg_mod_manifest_set_name (LrgModManifest *self,
                           const gchar    *name)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_free (self->name);
    self->name = g_strdup (name);
}

const gchar *
lrg_mod_manifest_get_version (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->version;
}

void
lrg_mod_manifest_set_version (LrgModManifest *self,
                              const gchar    *version)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_free (self->version);
    self->version = g_strdup (version);
}

const gchar *
lrg_mod_manifest_get_description (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->description;
}

void
lrg_mod_manifest_set_description (LrgModManifest *self,
                                  const gchar    *description)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_free (self->description);
    self->description = g_strdup (description);
}

const gchar *
lrg_mod_manifest_get_author (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->author;
}

void
lrg_mod_manifest_set_author (LrgModManifest *self,
                             const gchar    *author)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_free (self->author);
    self->author = g_strdup (author);
}

/* ==========================================================================
 * Type and Priority
 * ========================================================================== */

LrgModType
lrg_mod_manifest_get_mod_type (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), LRG_MOD_TYPE_DATA);
    return self->type;
}

void
lrg_mod_manifest_set_mod_type (LrgModManifest *self,
                               LrgModType      type)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    self->type = type;
}

LrgModPriority
lrg_mod_manifest_get_priority (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), LRG_MOD_PRIORITY_NORMAL);
    return self->priority;
}

void
lrg_mod_manifest_set_priority (LrgModManifest *self,
                               LrgModPriority  priority)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    self->priority = priority;
}

/* ==========================================================================
 * Dependencies
 * ========================================================================== */

GPtrArray *
lrg_mod_manifest_get_dependencies (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->dependencies;
}

void
lrg_mod_manifest_add_dependency (LrgModManifest *self,
                                 const gchar    *mod_id,
                                 const gchar    *min_version,
                                 gboolean        optional)
{
    LrgModDependency *dep;

    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_return_if_fail (mod_id != NULL);

    dep = lrg_mod_dependency_new (mod_id, min_version, optional);
    g_ptr_array_add (self->dependencies, dep);
}

gboolean
lrg_mod_manifest_has_dependency (LrgModManifest *self,
                                 const gchar    *mod_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), FALSE);
    g_return_val_if_fail (mod_id != NULL, FALSE);

    for (i = 0; i < self->dependencies->len; i++)
    {
        LrgModDependency *dep = g_ptr_array_index (self->dependencies, i);
        if (g_strcmp0 (dep->mod_id, mod_id) == 0)
            return TRUE;
    }

    return FALSE;
}

/* ==========================================================================
 * Load Order
 * ========================================================================== */

GPtrArray *
lrg_mod_manifest_get_load_after (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->load_after;
}

void
lrg_mod_manifest_add_load_after (LrgModManifest *self,
                                 const gchar    *mod_id)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_return_if_fail (mod_id != NULL);

    g_ptr_array_add (self->load_after, g_strdup (mod_id));
}

GPtrArray *
lrg_mod_manifest_get_load_before (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->load_before;
}

void
lrg_mod_manifest_add_load_before (LrgModManifest *self,
                                  const gchar    *mod_id)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_return_if_fail (mod_id != NULL);

    g_ptr_array_add (self->load_before, g_strdup (mod_id));
}

/* ==========================================================================
 * Paths
 * ========================================================================== */

const gchar *
lrg_mod_manifest_get_data_path (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->data_path;
}

void
lrg_mod_manifest_set_data_path (LrgModManifest *self,
                                const gchar    *path)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_free (self->data_path);
    self->data_path = g_strdup (path);
}

const gchar *
lrg_mod_manifest_get_entry_point (LrgModManifest *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), NULL);
    return self->entry_point;
}

void
lrg_mod_manifest_set_entry_point (LrgModManifest *self,
                                  const gchar    *entry_point)
{
    g_return_if_fail (LRG_IS_MOD_MANIFEST (self));
    g_free (self->entry_point);
    self->entry_point = g_strdup (entry_point);
}

/* ==========================================================================
 * Serialization
 * ========================================================================== */

gboolean
lrg_mod_manifest_save_to_file (LrgModManifest  *self,
                               const gchar     *path,
                               GError         **error)
{
    GString *yaml;
    gboolean success;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    yaml = g_string_new ("# Mod Manifest\n");

    /* Identity */
    g_string_append_printf (yaml, "id: %s\n", self->id);

    if (self->name != NULL)
        g_string_append_printf (yaml, "name: %s\n", self->name);

    if (self->version != NULL)
        g_string_append_printf (yaml, "version: %s\n", self->version);

    if (self->description != NULL)
        g_string_append_printf (yaml, "description: %s\n", self->description);

    if (self->author != NULL)
        g_string_append_printf (yaml, "author: %s\n", self->author);

    /* Type */
    switch (self->type)
    {
    case LRG_MOD_TYPE_DATA:
        g_string_append (yaml, "type: data\n");
        break;
    case LRG_MOD_TYPE_SCRIPT:
        g_string_append (yaml, "type: script\n");
        break;
    case LRG_MOD_TYPE_NATIVE:
        g_string_append (yaml, "type: native\n");
        break;
    }

    /* Priority */
    if (self->priority != LRG_MOD_PRIORITY_NORMAL)
    {
        switch (self->priority)
        {
        case LRG_MOD_PRIORITY_LOWEST:
            g_string_append (yaml, "priority: lowest\n");
            break;
        case LRG_MOD_PRIORITY_LOW:
            g_string_append (yaml, "priority: low\n");
            break;
        case LRG_MOD_PRIORITY_HIGH:
            g_string_append (yaml, "priority: high\n");
            break;
        case LRG_MOD_PRIORITY_HIGHEST:
            g_string_append (yaml, "priority: highest\n");
            break;
        default:
            g_string_append_printf (yaml, "priority: %d\n", (gint)self->priority);
            break;
        }
    }

    /* Paths */
    if (self->data_path != NULL)
        g_string_append_printf (yaml, "data_path: %s\n", self->data_path);

    if (self->entry_point != NULL)
        g_string_append_printf (yaml, "entry_point: %s\n", self->entry_point);

    /* Dependencies */
    if (self->dependencies->len > 0)
    {
        g_string_append (yaml, "dependencies:\n");
        for (i = 0; i < self->dependencies->len; i++)
        {
            LrgModDependency *dep = g_ptr_array_index (self->dependencies, i);

            if (dep->min_version != NULL || dep->optional)
            {
                g_string_append_printf (yaml, "  - id: %s\n", dep->mod_id);
                if (dep->min_version != NULL)
                    g_string_append_printf (yaml, "    min_version: %s\n", dep->min_version);
                if (dep->optional)
                    g_string_append (yaml, "    optional: true\n");
            }
            else
            {
                g_string_append_printf (yaml, "  - %s\n", dep->mod_id);
            }
        }
    }

    /* Load order */
    if (self->load_after->len > 0)
    {
        g_string_append (yaml, "load_after:\n");
        for (i = 0; i < self->load_after->len; i++)
        {
            g_string_append_printf (yaml, "  - %s\n",
                                    (const gchar *)g_ptr_array_index (self->load_after, i));
        }
    }

    if (self->load_before->len > 0)
    {
        g_string_append (yaml, "load_before:\n");
        for (i = 0; i < self->load_before->len; i++)
        {
            g_string_append_printf (yaml, "  - %s\n",
                                    (const gchar *)g_ptr_array_index (self->load_before, i));
        }
    }

    success = g_file_set_contents (path, yaml->str, yaml->len, error);
    g_string_free (yaml, TRUE);

    if (success)
        lrg_debug (LRG_LOG_DOMAIN_MOD, "Saved manifest to: %s", path);

    return success;
}
