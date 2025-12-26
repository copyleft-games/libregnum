/* lrg-mod-loader.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod loader implementation.
 */

#include "config.h"
#include "lrg-mod-loader.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

#include <gio/gio.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgModLoader
{
    GObject    parent_instance;

    GPtrArray *search_paths;     /* gchar* */
    gchar     *manifest_filename;
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgModLoader, lrg_mod_loader, G_TYPE_OBJECT)
#pragma GCC visibility pop

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mod_loader_finalize (GObject *object)
{
    LrgModLoader *self = LRG_MOD_LOADER (object);

    g_ptr_array_unref (self->search_paths);
    g_free (self->manifest_filename);

    G_OBJECT_CLASS (lrg_mod_loader_parent_class)->finalize (object);
}

static void
lrg_mod_loader_class_init (LrgModLoaderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_mod_loader_finalize;
}

static void
lrg_mod_loader_init (LrgModLoader *self)
{
    self->search_paths = g_ptr_array_new_with_free_func (g_free);
    self->manifest_filename = g_strdup ("mod.yaml");
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgModLoader *
lrg_mod_loader_new (void)
{
    return g_object_new (LRG_TYPE_MOD_LOADER, NULL);
}

/* ==========================================================================
 * Search Paths
 * ========================================================================== */

void
lrg_mod_loader_add_search_path (LrgModLoader *self,
                                const gchar  *path)
{
    g_return_if_fail (LRG_IS_MOD_LOADER (self));
    g_return_if_fail (path != NULL);

    g_ptr_array_add (self->search_paths, g_strdup (path));
    lrg_debug (LRG_LOG_DOMAIN_MOD, "Added mod search path: %s", path);
}

GPtrArray *
lrg_mod_loader_get_search_paths (LrgModLoader *self)
{
    g_return_val_if_fail (LRG_IS_MOD_LOADER (self), NULL);
    return self->search_paths;
}

void
lrg_mod_loader_clear_search_paths (LrgModLoader *self)
{
    g_return_if_fail (LRG_IS_MOD_LOADER (self));
    g_ptr_array_set_size (self->search_paths, 0);
}

/* ==========================================================================
 * Configuration
 * ========================================================================== */

const gchar *
lrg_mod_loader_get_manifest_filename (LrgModLoader *self)
{
    g_return_val_if_fail (LRG_IS_MOD_LOADER (self), NULL);
    return self->manifest_filename;
}

void
lrg_mod_loader_set_manifest_filename (LrgModLoader *self,
                                      const gchar  *filename)
{
    g_return_if_fail (LRG_IS_MOD_LOADER (self));
    g_return_if_fail (filename != NULL);

    g_free (self->manifest_filename);
    self->manifest_filename = g_strdup (filename);
}

/* ==========================================================================
 * Discovery
 * ========================================================================== */

LrgMod *
lrg_mod_loader_load_mod (LrgModLoader  *self,
                         const gchar   *path,
                         GError       **error)
{
    g_autofree gchar *manifest_path = NULL;
    g_autoptr(LrgModManifest) manifest = NULL;
    LrgMod *mod;

    g_return_val_if_fail (LRG_IS_MOD_LOADER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    /* Build manifest path */
    manifest_path = g_build_filename (path, self->manifest_filename, NULL);

    /* Check if manifest exists */
    if (!g_file_test (manifest_path, G_FILE_TEST_EXISTS))
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_NOT_FOUND,
                     "Manifest not found: %s", manifest_path);
        return NULL;
    }

    /* Load manifest */
    manifest = lrg_mod_manifest_new_from_file (manifest_path, error);
    if (manifest == NULL)
        return NULL;

    /* Create mod */
    mod = lrg_mod_new (manifest, path);

    lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded mod: %s from %s",
              lrg_mod_get_id (mod), path);

    return mod;
}

GPtrArray *
lrg_mod_loader_discover_at (LrgModLoader  *self,
                            const gchar   *path,
                            GError       **error)
{
    GPtrArray *mods;
    GDir *dir;
    const gchar *name;
    g_autoptr(GError) local_error = NULL;

    g_return_val_if_fail (LRG_IS_MOD_LOADER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    mods = g_ptr_array_new_with_free_func (g_object_unref);

    /* Check if path exists */
    if (!g_file_test (path, G_FILE_TEST_IS_DIR))
    {
        lrg_debug (LRG_LOG_DOMAIN_MOD, "Search path does not exist: %s", path);
        return mods;
    }

    dir = g_dir_open (path, 0, &local_error);
    if (dir == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_MOD, "Cannot open mod directory: %s - %s",
                     path, local_error->message);
        return mods;
    }

    while ((name = g_dir_read_name (dir)) != NULL)
    {
        g_autofree gchar *mod_path = NULL;
        g_autofree gchar *manifest_path = NULL;
        LrgMod *mod;
        g_autoptr(GError) mod_error = NULL;

        mod_path = g_build_filename (path, name, NULL);

        /* Skip non-directories */
        if (!g_file_test (mod_path, G_FILE_TEST_IS_DIR))
            continue;

        /* Check for manifest */
        manifest_path = g_build_filename (mod_path, self->manifest_filename, NULL);
        if (!g_file_test (manifest_path, G_FILE_TEST_EXISTS))
            continue;

        /* Load mod */
        mod = lrg_mod_loader_load_mod (self, mod_path, &mod_error);
        if (mod != NULL)
        {
            g_ptr_array_add (mods, mod);
        }
        else
        {
            lrg_warning (LRG_LOG_DOMAIN_MOD, "Failed to load mod at %s: %s",
                         mod_path, mod_error->message);
        }
    }

    g_dir_close (dir);

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Discovered %u mods at %s", mods->len, path);

    return mods;
}

GPtrArray *
lrg_mod_loader_discover (LrgModLoader  *self,
                         GError       **error)
{
    GPtrArray *all_mods;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_LOADER (self), NULL);

    all_mods = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < self->search_paths->len; i++)
    {
        const gchar *path = g_ptr_array_index (self->search_paths, i);
        g_autoptr(GPtrArray) mods = NULL;
        guint j;

        mods = lrg_mod_loader_discover_at (self, path, error);
        if (mods == NULL)
            continue;

        /* Transfer mods to result array */
        for (j = 0; j < mods->len; j++)
        {
            LrgMod *mod = g_ptr_array_index (mods, j);
            g_ptr_array_add (all_mods, g_object_ref (mod));
        }
    }

    lrg_info (LRG_LOG_DOMAIN_MOD, "Discovered %u total mods", all_mods->len);

    return all_mods;
}
