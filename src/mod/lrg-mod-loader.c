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
#include "../scripting/lrg-scripting-manager.h"
#include "../dlc/lrg-dlc.h"
#include "../dlc/lrg-expansion-pack.h"
#include "../dlc/lrg-cosmetic-pack.h"
#include "../dlc/lrg-quest-pack.h"
#include "../dlc/lrg-item-pack.h"
#include "../dlc/lrg-character-pack.h"
#include "../dlc/lrg-map-pack.h"
#include "../dlc/lrg-dlc-ownership.h"
#include "../dlc/lrg-dlc-ownership-steam.h"
#include "../dlc/lrg-dlc-ownership-license.h"
#include "../dlc/lrg-dlc-ownership-manifest.h"

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
    gboolean   implicit_entries; /* folders without a manifest may load */
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
 * DLC Creation Helpers
 * ========================================================================== */

/**
 * create_dlc_from_manifest:
 * @manifest: the mod manifest
 * @path: base path for the mod
 *
 * Creates the appropriate DLC subclass based on the manifest's DLC type.
 *
 * Returns: (transfer full): a new #LrgDlc subclass instance
 */
static LrgDlc *
create_dlc_from_manifest (LrgModManifest *manifest,
                          const gchar    *path)
{
    LrgDlcType dlc_type;
    LrgDlc *dlc = NULL;

    dlc_type = lrg_mod_manifest_get_dlc_type (manifest);

    switch (dlc_type)
    {
    case LRG_DLC_TYPE_EXPANSION:
        dlc = LRG_DLC (lrg_expansion_pack_new (manifest, path));
        break;
    case LRG_DLC_TYPE_COSMETIC:
        dlc = LRG_DLC (lrg_cosmetic_pack_new (manifest, path));
        break;
    case LRG_DLC_TYPE_QUEST:
        dlc = LRG_DLC (lrg_quest_pack_new (manifest, path));
        break;
    case LRG_DLC_TYPE_ITEM:
        dlc = LRG_DLC (lrg_item_pack_new (manifest, path));
        break;
    case LRG_DLC_TYPE_CHARACTER:
        dlc = LRG_DLC (lrg_character_pack_new (manifest, path));
        break;
    case LRG_DLC_TYPE_MAP:
        dlc = LRG_DLC (lrg_map_pack_new (manifest, path));
        break;
    default:
        /* Unknown type - create base DLC */
        dlc = g_object_new (LRG_TYPE_DLC,
                            "manifest", manifest,
                            "base-path", path,
                            "dlc-type", dlc_type,
                            NULL);
        break;
    }

    return dlc;
}

/**
 * setup_dlc_ownership:
 * @dlc: the DLC to configure
 * @manifest: the mod manifest
 * @path: base path for the mod (for license file lookup)
 *
 * Sets up the ownership checker based on manifest configuration.
 */
static void
setup_dlc_ownership (LrgDlc         *dlc,
                     LrgModManifest *manifest,
                     const gchar    *path)
{
    const gchar *ownership_method;
    LrgDlcOwnership *checker = NULL;

    ownership_method = lrg_mod_manifest_get_ownership_method (manifest);

    if (ownership_method == NULL || g_strcmp0 (ownership_method, "none") == 0)
    {
        /* No ownership check - assume owned */
        return;
    }
    else if (g_strcmp0 (ownership_method, "steam") == 0)
    {
        guint32 steam_app_id;

        steam_app_id = lrg_mod_manifest_get_steam_app_id (manifest);
        checker = LRG_DLC_OWNERSHIP (lrg_dlc_ownership_steam_new ());

        if (steam_app_id != 0)
            lrg_dlc_ownership_steam_register_dlc (LRG_DLC_OWNERSHIP_STEAM (checker),
                                                   lrg_mod_manifest_get_id (manifest),
                                                   steam_app_id);
    }
    else if (g_strcmp0 (ownership_method, "license") == 0)
    {
        g_autofree gchar *license_path = NULL;

        license_path = g_build_filename (path, "license.dat", NULL);
        checker = LRG_DLC_OWNERSHIP (lrg_dlc_ownership_license_new (license_path));
    }
    else if (g_strcmp0 (ownership_method, "manifest") == 0)
    {
        checker = LRG_DLC_OWNERSHIP (lrg_dlc_ownership_manifest_new ());

        /* Manifest-based: mark as owned if listed */
        lrg_dlc_ownership_manifest_set_owned (LRG_DLC_OWNERSHIP_MANIFEST (checker),
                                               lrg_mod_manifest_get_id (manifest),
                                               TRUE);
    }

    if (checker != NULL)
    {
        lrg_dlc_set_ownership_checker (dlc, checker);
        g_object_unref (checker);
    }
}

/**
 * setup_dlc_from_manifest:
 * @dlc: the DLC to configure
 * @manifest: the mod manifest
 *
 * Configures DLC properties from the manifest.
 */
static void
setup_dlc_from_manifest (LrgDlc         *dlc,
                         LrgModManifest *manifest)
{
    const gchar *price_string;
    const gchar *store_id;
    const gchar *min_game_version;
    GDateTime *release_date;
    guint32 steam_app_id;
    gboolean trial_enabled;
    GPtrArray *trial_content_ids;
    guint i;

    /* Set pricing info */
    price_string = lrg_mod_manifest_get_price_string (manifest);
    if (price_string != NULL)
        lrg_dlc_set_price_string (dlc, price_string);

    /* Set store info */
    store_id = lrg_mod_manifest_get_store_id (manifest);
    if (store_id != NULL)
        lrg_dlc_set_store_id (dlc, store_id);

    steam_app_id = lrg_mod_manifest_get_steam_app_id (manifest);
    if (steam_app_id != 0)
        lrg_dlc_set_steam_app_id (dlc, steam_app_id);

    /* Set release info */
    release_date = lrg_mod_manifest_get_release_date (manifest);
    if (release_date != NULL)
        lrg_dlc_set_release_date (dlc, release_date);

    min_game_version = lrg_mod_manifest_get_min_game_version (manifest);
    if (min_game_version != NULL)
        lrg_dlc_set_min_game_version (dlc, min_game_version);

    /* Set trial info */
    trial_enabled = lrg_mod_manifest_get_trial_enabled (manifest);
    lrg_dlc_set_trial_enabled (dlc, trial_enabled);

    trial_content_ids = lrg_mod_manifest_get_trial_content_ids (manifest);
    if (trial_content_ids != NULL)
    {
        for (i = 0; i < trial_content_ids->len; i++)
        {
            const gchar *content_id = g_ptr_array_index (trial_content_ids, i);
            lrg_dlc_add_trial_content_id (dlc, content_id);
        }
    }
}

/* ==========================================================================
 * Discovery
 * ========================================================================== */

/*
 * Folders without a manifest (when implicit entries are enabled): exactly one
 * file whose extension a scripting backend claims becomes a script mod with
 * the folder name as its id.  Zero or several candidates skip the folder.
 */
static LrgMod *
load_implicit_mod (const gchar *mod_path,
                   const gchar *folder_name)
{
    LrgScriptingManager *manager;
    g_autoptr(GDir)      dir = NULL;
    g_autofree gchar    *entry = NULL;
    const gchar         *file;
    guint                candidates;
    LrgModManifest      *manifest;
    LrgMod              *mod;

    dir = g_dir_open (mod_path, 0, NULL);
    if (dir == NULL)
        return NULL;

    manager = lrg_scripting_manager_get_default ();
    candidates = 0;
    while ((file = g_dir_read_name (dir)) != NULL)
    {
        g_autofree gchar *full = g_build_filename (mod_path, file, NULL);

        if (file[0] == '.' || !g_file_test (full, G_FILE_TEST_IS_REGULAR))
            continue;
        if (lrg_scripting_manager_language_for_path (manager, file) == LRG_SCRIPT_LANGUAGE_NONE)
            continue;
        candidates++;
        g_free (entry);
        entry = g_strdup (file);
    }

    if (candidates != 1)
    {
        lrg_debug (LRG_LOG_DOMAIN_MOD,
                   "Skipping %s: no manifest and %u script files", mod_path, candidates);
        return NULL;
    }

    manifest = lrg_mod_manifest_new (folder_name);
    lrg_mod_manifest_set_mod_type (manifest, LRG_MOD_TYPE_SCRIPT);
    lrg_mod_manifest_set_entry_point (manifest, entry);
    mod = lrg_mod_new (manifest, mod_path);
    g_object_unref (manifest);

    lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded implicit mod %s (%s)", folder_name, entry);
    return mod;
}

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

    /* Check if this is a DLC */
    if (lrg_mod_manifest_is_dlc (manifest))
    {
        LrgDlc *dlc;

        /* Create appropriate DLC subclass */
        dlc = create_dlc_from_manifest (manifest, path);

        /* Configure DLC from manifest */
        setup_dlc_from_manifest (dlc, manifest);
        setup_dlc_ownership (dlc, manifest, path);

        lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded DLC: %s (%s) from %s",
                  lrg_mod_get_id (LRG_MOD (dlc)),
                  g_enum_get_value (g_type_class_peek (LRG_TYPE_DLC_TYPE),
                                    lrg_dlc_get_dlc_type (dlc))->value_nick,
                  path);

        mod = LRG_MOD (dlc);
    }
    else
    {
        /* Create regular mod */
        mod = lrg_mod_new (manifest, path);

        lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded mod: %s from %s",
                  lrg_mod_get_id (mod), path);
    }

    return mod;
}

GPtrArray *
lrg_mod_loader_discover_at (LrgModLoader  *self,
                            const gchar   *path,
                            GError       **error)
{
    GPtrArray *mods;
    GPtrArray *names;
    GDir *dir;
    const gchar *name;
    guint n;
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

    /* Sorted names make discovery (and so tie-breaking) deterministic */
    names = g_ptr_array_new_with_free_func (g_free);
    while ((name = g_dir_read_name (dir)) != NULL)
    {
        /* Hidden folders (".git", editor backups) are never mods */
        if (name[0] != '.')
            g_ptr_array_add (names, g_strdup (name));
    }
    g_ptr_array_sort_values (names, (GCompareFunc)g_strcmp0);

    for (n = 0; n < names->len; n++)
    {
        g_autofree gchar *mod_path = NULL;
        g_autofree gchar *manifest_path = NULL;
        LrgMod *mod;
        g_autoptr(GError) mod_error = NULL;

        name = g_ptr_array_index (names, n);
        mod_path = g_build_filename (path, name, NULL);

        /* Skip non-directories */
        if (!g_file_test (mod_path, G_FILE_TEST_IS_DIR))
            continue;

        /* No manifest: optionally treat a lone script file as the entry */
        manifest_path = g_build_filename (mod_path, self->manifest_filename, NULL);
        if (!g_file_test (manifest_path, G_FILE_TEST_EXISTS))
        {
            if (self->implicit_entries)
            {
                mod = load_implicit_mod (mod_path, name);
                if (mod != NULL)
                    g_ptr_array_add (mods, mod);
            }
            continue;
        }

        /*
         * A broken manifest still yields a failed placeholder mod (id = folder
         * name) so hosts can show the user what is wrong.
         */
        mod = lrg_mod_loader_load_mod (self, mod_path, &mod_error);
        if (mod == NULL)
        {
            g_autoptr(LrgModManifest) placeholder = lrg_mod_manifest_new (name);

            lrg_info (LRG_LOG_DOMAIN_MOD, "Invalid mod at %s: %s",
                      mod_path, mod_error->message);
            mod = lrg_mod_new (placeholder, mod_path);
            lrg_mod_mark_failed (mod, mod_error->message);
        }
        g_ptr_array_add (mods, mod);
    }

    g_ptr_array_unref (names);
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

/*
 * lrg_mod_loader_set_implicit_entries:
 * @self: a #LrgModLoader
 * @implicit: whether folders without a manifest may load
 *
 * When enabled, a folder with no manifest but exactly one script file (any
 * extension a scripting backend claims: lua, py, js, c, so, ...) is
 * discovered as a script mod whose id is the folder name.  This supports
 * "drop a file in a folder" extensions.  Disabled by default.
 */
void
lrg_mod_loader_set_implicit_entries (LrgModLoader *self,
                                     gboolean      implicit)
{
    g_return_if_fail (LRG_IS_MOD_LOADER (self));

    self->implicit_entries = implicit;
}

/*
 * lrg_mod_loader_get_implicit_entries:
 * @self: a #LrgModLoader
 *
 * Returns: whether folders without a manifest may load
 */
gboolean
lrg_mod_loader_get_implicit_entries (LrgModLoader *self)
{
    g_return_val_if_fail (LRG_IS_MOD_LOADER (self), FALSE);

    return self->implicit_entries;
}
