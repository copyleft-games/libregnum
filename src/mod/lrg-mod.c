/* lrg-mod.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod implementation.
 */

#include "config.h"
#include "lrg-mod.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

#include <gio/gio.h>
#include <string.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgMod
{
    GObject          parent_instance;

    LrgModManifest  *manifest;
    gchar           *base_path;
    gchar           *data_path;

    LrgModState      state;
    gboolean         enabled;
    gchar           *error_message;

    /* For native mods */
    GModule         *module;
    gpointer         user_data;
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgMod, lrg_mod, G_TYPE_OBJECT)
#pragma GCC visibility pop

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mod_dispose (GObject *object)
{
    LrgMod *self = LRG_MOD (object);

    if (self->state == LRG_MOD_STATE_LOADED)
        lrg_mod_unload (self);

    g_clear_object (&self->manifest);

    G_OBJECT_CLASS (lrg_mod_parent_class)->dispose (object);
}

static void
lrg_mod_finalize (GObject *object)
{
    LrgMod *self = LRG_MOD (object);

    g_free (self->base_path);
    g_free (self->data_path);
    g_free (self->error_message);

    G_OBJECT_CLASS (lrg_mod_parent_class)->finalize (object);
}

static void
lrg_mod_class_init (LrgModClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_mod_dispose;
    object_class->finalize = lrg_mod_finalize;
}

static void
lrg_mod_init (LrgMod *self)
{
    self->state = LRG_MOD_STATE_UNLOADED;
    self->enabled = TRUE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgMod *
lrg_mod_new (LrgModManifest *manifest,
             const gchar    *base_path)
{
    LrgMod *mod;
    const gchar *data_subpath;

    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (manifest), NULL);
    g_return_val_if_fail (base_path != NULL, NULL);

    mod = g_object_new (LRG_TYPE_MOD, NULL);
    mod->manifest = g_object_ref (manifest);
    mod->base_path = g_strdup (base_path);

    /* Build full data path if specified in manifest */
    data_subpath = lrg_mod_manifest_get_data_path (manifest);
    if (data_subpath != NULL)
        mod->data_path = g_build_filename (base_path, data_subpath, NULL);
    else
        mod->data_path = NULL;  /* No data directory required */

    mod->state = LRG_MOD_STATE_DISCOVERED;

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Created mod: %s at %s",
               lrg_mod_manifest_get_id (manifest), base_path);

    return mod;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgModManifest *
lrg_mod_get_manifest (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), NULL);
    return self->manifest;
}

const gchar *
lrg_mod_get_id (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), NULL);
    return lrg_mod_manifest_get_id (self->manifest);
}

const gchar *
lrg_mod_get_base_path (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), NULL);
    return self->base_path;
}

const gchar *
lrg_mod_get_data_path (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), NULL);
    return self->data_path;
}

/* ==========================================================================
 * State
 * ========================================================================== */

LrgModState
lrg_mod_get_state (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), LRG_MOD_STATE_UNLOADED);
    return self->state;
}

gboolean
lrg_mod_is_loaded (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), FALSE);
    return self->state == LRG_MOD_STATE_LOADED;
}

gboolean
lrg_mod_is_enabled (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), FALSE);
    return self->enabled;
}

void
lrg_mod_set_enabled (LrgMod   *self,
                     gboolean  enabled)
{
    g_return_if_fail (LRG_IS_MOD (self));

    if (self->enabled == enabled)
        return;

    self->enabled = enabled;

    if (!enabled && self->state == LRG_MOD_STATE_LOADED)
    {
        self->state = LRG_MOD_STATE_DISABLED;
        lrg_debug (LRG_LOG_DOMAIN_MOD, "Mod disabled: %s", lrg_mod_get_id (self));
    }
    else if (enabled && self->state == LRG_MOD_STATE_DISABLED)
    {
        self->state = LRG_MOD_STATE_LOADED;
        lrg_debug (LRG_LOG_DOMAIN_MOD, "Mod enabled: %s", lrg_mod_get_id (self));
    }
}

const gchar *
lrg_mod_get_error (LrgMod *self)
{
    g_return_val_if_fail (LRG_IS_MOD (self), NULL);
    return self->error_message;
}

/* ==========================================================================
 * Loading
 * ========================================================================== */

static gboolean
load_data_mod (LrgMod   *self,
               GError  **error)
{
    GFile *data_dir;
    gboolean exists;

    /* If no data_path specified, nothing to load */
    if (self->data_path == NULL)
        return TRUE;

    /* Verify the data directory exists */
    data_dir = g_file_new_for_path (self->data_path);
    exists = g_file_query_exists (data_dir, NULL);
    g_object_unref (data_dir);

    if (!exists)
    {
        /* Data directory specified but doesn't exist - log debug message */
        lrg_debug (LRG_LOG_DOMAIN_MOD,
                   "Mod data directory does not exist: %s", self->data_path);
    }

    return TRUE;
}

static gboolean
load_script_mod (LrgMod   *self,
                 GError  **error)
{
    /* Script mods would require a scripting engine integration */
    lrg_info (LRG_LOG_DOMAIN_MOD,
              "Script mods not yet implemented: %s", lrg_mod_get_id (self));
    return TRUE;
}

static gboolean
load_native_mod (LrgMod   *self,
                 GError  **error)
{
    const gchar *entry_point;
    g_autofree gchar *module_path = NULL;
    gpointer init_func;

    entry_point = lrg_mod_manifest_get_entry_point (self->manifest);
    if (entry_point == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Native mod has no entry_point: %s", lrg_mod_get_id (self));
        return FALSE;
    }

    module_path = g_build_filename (self->base_path, entry_point, NULL);

    self->module = g_module_open (module_path, G_MODULE_BIND_LAZY);
    if (self->module == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Failed to load native module: %s", g_module_error ());
        return FALSE;
    }

    /* Look for lrg_mod_init symbol */
    if (g_module_symbol (self->module, "lrg_mod_init", &init_func))
    {
        typedef gboolean (*LrgModInitFunc) (LrgMod *mod, gpointer *user_data);
        LrgModInitFunc init = (LrgModInitFunc)init_func;

        if (!init (self, &self->user_data))
        {
            g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                         "Mod init function returned FALSE: %s", lrg_mod_get_id (self));
            g_module_close (self->module);
            self->module = NULL;
            return FALSE;
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Loaded native module: %s", module_path);

    return TRUE;
}

gboolean
lrg_mod_load (LrgMod   *self,
              GError  **error)
{
    LrgModType type;
    gboolean success;

    g_return_val_if_fail (LRG_IS_MOD (self), FALSE);

    if (self->state == LRG_MOD_STATE_LOADED)
        return TRUE;

    if (!self->enabled)
    {
        self->state = LRG_MOD_STATE_DISABLED;
        return TRUE;
    }

    self->state = LRG_MOD_STATE_LOADING;
    g_clear_pointer (&self->error_message, g_free);

    type = lrg_mod_manifest_get_mod_type (self->manifest);

    switch (type)
    {
    case LRG_MOD_TYPE_DATA:
        success = load_data_mod (self, error);
        break;

    case LRG_MOD_TYPE_SCRIPT:
        success = load_script_mod (self, error);
        break;

    case LRG_MOD_TYPE_NATIVE:
        success = load_native_mod (self, error);
        break;

    default:
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Unknown mod type: %d", type);
        success = FALSE;
        break;
    }

    if (success)
    {
        self->state = LRG_MOD_STATE_LOADED;
        lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded mod: %s", lrg_mod_get_id (self));
    }
    else
    {
        self->state = LRG_MOD_STATE_FAILED;
        if (error != NULL && *error != NULL)
            self->error_message = g_strdup ((*error)->message);
        lrg_warning (LRG_LOG_DOMAIN_MOD, "Failed to load mod: %s - %s",
                     lrg_mod_get_id (self), self->error_message);
    }

    return success;
}

void
lrg_mod_unload (LrgMod *self)
{
    g_return_if_fail (LRG_IS_MOD (self));

    if (self->state != LRG_MOD_STATE_LOADED &&
        self->state != LRG_MOD_STATE_DISABLED)
        return;

    /* For native mods, call shutdown and close module */
    if (self->module != NULL)
    {
        gpointer shutdown_func;

        if (g_module_symbol (self->module, "lrg_mod_shutdown", &shutdown_func))
        {
            typedef void (*LrgModShutdownFunc) (LrgMod *mod, gpointer user_data);
            LrgModShutdownFunc shutdown = (LrgModShutdownFunc)shutdown_func;
            shutdown (self, self->user_data);
        }

        g_module_close (self->module);
        self->module = NULL;
        self->user_data = NULL;
    }

    self->state = LRG_MOD_STATE_UNLOADED;
    lrg_info (LRG_LOG_DOMAIN_MOD, "Unloaded mod: %s", lrg_mod_get_id (self));
}

/* ==========================================================================
 * Resources
 * ========================================================================== */

gchar *
lrg_mod_resolve_path (LrgMod      *self,
                      const gchar *relative_path)
{
    g_return_val_if_fail (LRG_IS_MOD (self), NULL);
    g_return_val_if_fail (relative_path != NULL, NULL);

    if (self->data_path == NULL)
        return NULL;

    return g_build_filename (self->data_path, relative_path, NULL);
}

GPtrArray *
lrg_mod_list_files (LrgMod      *self,
                    const gchar *subdir,
                    const gchar *pattern)
{
    GPtrArray *files;
    g_autofree gchar *search_path = NULL;
    GDir *dir;
    const gchar *name;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    files = g_ptr_array_new_with_free_func (g_free);

    if (self->data_path == NULL)
        return files;

    if (subdir != NULL)
        search_path = g_build_filename (self->data_path, subdir, NULL);
    else
        search_path = g_strdup (self->data_path);

    dir = g_dir_open (search_path, 0, NULL);
    if (dir == NULL)
        return files;

    while ((name = g_dir_read_name (dir)) != NULL)
    {
        gboolean match;

        match = TRUE;
        if (pattern != NULL)
            match = g_pattern_match_simple (pattern, name);

        if (match)
            g_ptr_array_add (files, g_build_filename (search_path, name, NULL));
    }

    g_dir_close (dir);

    return files;
}
