/* lrg-mod-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod manager implementation.
 */

#include "config.h"
#include "lrg-mod-manager.h"
#include "lrg-modable.h"
#include "lrg-providers.h"
#include "../dlc/lrg-dlc.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

#include <gio/gio.h>
#include <string.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgModManager
{
    GObject       parent_instance;

    LrgModLoader *loader;
    GHashTable   *mods_by_id;    /* id -> LrgMod */
    GPtrArray    *all_mods;      /* LrgMod, in discovery order */
    GPtrArray    *loaded_mods;   /* LrgMod, in load order */
    GPtrArray    *load_order;    /* gchar*, computed order */
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgModManager, lrg_mod_manager, G_TYPE_OBJECT)
#pragma GCC visibility pop

static LrgModManager *default_manager = NULL;

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mod_manager_dispose (GObject *object)
{
    LrgModManager *self = LRG_MOD_MANAGER (object);

    lrg_mod_manager_unload_all (self);
    g_clear_object (&self->loader);

    G_OBJECT_CLASS (lrg_mod_manager_parent_class)->dispose (object);
}

static void
lrg_mod_manager_finalize (GObject *object)
{
    LrgModManager *self = LRG_MOD_MANAGER (object);

    g_hash_table_destroy (self->mods_by_id);
    g_ptr_array_unref (self->all_mods);
    g_ptr_array_unref (self->loaded_mods);
    g_ptr_array_unref (self->load_order);

    if (default_manager == self)
        default_manager = NULL;

    G_OBJECT_CLASS (lrg_mod_manager_parent_class)->finalize (object);
}

static void
lrg_mod_manager_class_init (LrgModManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_mod_manager_dispose;
    object_class->finalize = lrg_mod_manager_finalize;
}

static void
lrg_mod_manager_init (LrgModManager *self)
{
    self->loader = lrg_mod_loader_new ();
    self->mods_by_id = g_hash_table_new (g_str_hash, g_str_equal);
    self->all_mods = g_ptr_array_new_with_free_func (g_object_unref);
    self->loaded_mods = g_ptr_array_new ();
    self->load_order = g_ptr_array_new_with_free_func (g_free);

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Created mod manager");
}

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

LrgModManager *
lrg_mod_manager_get_default (void)
{
    if (default_manager == NULL)
        default_manager = lrg_mod_manager_new ();

    return default_manager;
}

LrgModManager *
lrg_mod_manager_new (void)
{
    return g_object_new (LRG_TYPE_MOD_MANAGER, NULL);
}

/* ==========================================================================
 * Loader Configuration
 * ========================================================================== */

LrgModLoader *
lrg_mod_manager_get_loader (LrgModManager *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);
    return self->loader;
}

void
lrg_mod_manager_add_search_path (LrgModManager *self,
                                 const gchar   *path)
{
    g_return_if_fail (LRG_IS_MOD_MANAGER (self));
    g_return_if_fail (path != NULL);

    lrg_mod_loader_add_search_path (self->loader, path);
}

/* ==========================================================================
 * Load Order Computation
 * ========================================================================== */

typedef struct
{
    LrgModManager *manager;
    GHashTable    *visiting;    /* id -> TRUE if currently visiting */
    GHashTable    *visited;     /* id -> TRUE if fully processed */
    GPtrArray     *result;      /* final order */
    gboolean       has_cycle;
} TopoSortContext;

static void
topo_visit (TopoSortContext *ctx,
            const gchar     *mod_id)
{
    LrgMod *mod;
    LrgModManifest *manifest;
    GPtrArray *deps;
    GPtrArray *load_after;
    guint i;

    if (ctx->has_cycle)
        return;

    /* Already fully processed */
    if (g_hash_table_contains (ctx->visited, mod_id))
        return;

    /* Cycle detection */
    if (g_hash_table_contains (ctx->visiting, mod_id))
    {
        lrg_warning (LRG_LOG_DOMAIN_MOD,
                     "Circular dependency detected at mod: %s", mod_id);
        ctx->has_cycle = TRUE;
        return;
    }

    mod = lrg_mod_manager_get_mod (ctx->manager, mod_id);
    if (mod == NULL)
    {
        /* Unknown mod, skip */
        return;
    }

    g_hash_table_insert (ctx->visiting, (gpointer)mod_id, GINT_TO_POINTER (TRUE));

    manifest = lrg_mod_get_manifest (mod);

    /* Visit dependencies first */
    deps = lrg_mod_manifest_get_dependencies (manifest);
    for (i = 0; i < deps->len; i++)
    {
        LrgModDependency *dep = g_ptr_array_index (deps, i);
        const gchar *dep_id = lrg_mod_dependency_get_mod_id (dep);
        topo_visit (ctx, dep_id);
    }

    /* Visit load_after mods */
    load_after = lrg_mod_manifest_get_load_after (manifest);
    for (i = 0; i < load_after->len; i++)
    {
        const gchar *after_id = g_ptr_array_index (load_after, i);
        topo_visit (ctx, after_id);
    }

    g_hash_table_remove (ctx->visiting, mod_id);
    g_hash_table_insert (ctx->visited, (gpointer)mod_id, GINT_TO_POINTER (TRUE));

    g_ptr_array_add (ctx->result, g_strdup (mod_id));
}

static gint
compare_mod_priority (gconstpointer a,
                      gconstpointer b,
                      gpointer      user_data)
{
    LrgModManager *manager = user_data;
    const gchar *id_a = *(const gchar **)a;
    const gchar *id_b = *(const gchar **)b;
    LrgMod *mod_a;
    LrgMod *mod_b;
    LrgModManifest *manifest_a;
    LrgModManifest *manifest_b;
    LrgModPriority prio_a;
    LrgModPriority prio_b;

    mod_a = lrg_mod_manager_get_mod (manager, id_a);
    mod_b = lrg_mod_manager_get_mod (manager, id_b);

    if (mod_a == NULL || mod_b == NULL)
        return 0;

    manifest_a = lrg_mod_get_manifest (mod_a);
    manifest_b = lrg_mod_get_manifest (mod_b);

    prio_a = lrg_mod_manifest_get_priority (manifest_a);
    prio_b = lrg_mod_manifest_get_priority (manifest_b);

    /* Lower priority values load first */
    return (gint)prio_a - (gint)prio_b;
}

static GPtrArray *
compute_load_order (LrgModManager *self)
{
    TopoSortContext ctx;
    guint i;

    ctx.manager = self;
    ctx.visiting = g_hash_table_new (g_str_hash, g_str_equal);
    ctx.visited = g_hash_table_new (g_str_hash, g_str_equal);
    ctx.result = g_ptr_array_new_with_free_func (g_free);
    ctx.has_cycle = FALSE;

    /* Visit all mods */
    for (i = 0; i < self->all_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->all_mods, i);
        const gchar *mod_id = lrg_mod_get_id (mod);

        if (!lrg_mod_is_enabled (mod))
            continue;

        topo_visit (&ctx, mod_id);
    }

    g_hash_table_destroy (ctx.visiting);
    g_hash_table_destroy (ctx.visited);

    /* Sort by priority within the topological order */
    g_ptr_array_sort_with_data (ctx.result, compare_mod_priority, self);

    return ctx.result;
}

/* ==========================================================================
 * Mod Management
 * ========================================================================== */

guint
lrg_mod_manager_discover (LrgModManager  *self,
                          GError        **error)
{
    g_autoptr(GPtrArray) discovered = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), 0);

    /* Clear existing mods */
    g_hash_table_remove_all (self->mods_by_id);
    g_ptr_array_set_size (self->all_mods, 0);
    g_ptr_array_set_size (self->load_order, 0);

    /* Discover mods */
    discovered = lrg_mod_loader_discover (self->loader, error);
    if (discovered == NULL)
        return 0;

    /* Register discovered mods */
    for (i = 0; i < discovered->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (discovered, i);
        const gchar *mod_id = lrg_mod_get_id (mod);

        if (g_hash_table_contains (self->mods_by_id, mod_id))
        {
            lrg_warning (LRG_LOG_DOMAIN_MOD,
                         "Duplicate mod ID ignored: %s", mod_id);
            continue;
        }

        g_ptr_array_add (self->all_mods, g_object_ref (mod));
        g_hash_table_insert (self->mods_by_id, (gpointer)mod_id, mod);
    }

    /* Compute load order */
    g_ptr_array_unref (self->load_order);
    self->load_order = compute_load_order (self);

    lrg_info (LRG_LOG_DOMAIN_MOD, "Discovered %u mods", self->all_mods->len);

    return self->all_mods->len;
}

gboolean
lrg_mod_manager_check_dependencies (LrgModManager  *self,
                                    LrgMod         *mod,
                                    GError        **error)
{
    LrgModManifest *manifest;
    GPtrArray *deps;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_MOD (mod), FALSE);

    manifest = lrg_mod_get_manifest (mod);
    deps = lrg_mod_manifest_get_dependencies (manifest);

    for (i = 0; i < deps->len; i++)
    {
        LrgModDependency *dep = g_ptr_array_index (deps, i);
        const gchar *dep_id = lrg_mod_dependency_get_mod_id (dep);
        gboolean optional = lrg_mod_dependency_is_optional (dep);
        LrgMod *dep_mod;

        dep_mod = lrg_mod_manager_get_mod (self, dep_id);

        if (dep_mod == NULL)
        {
            if (!optional)
            {
                g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_MISSING_DEPENDENCY,
                             "Missing required dependency: %s requires %s",
                             lrg_mod_get_id (mod), dep_id);
                return FALSE;
            }
            continue;
        }

        if (!lrg_mod_is_enabled (dep_mod))
        {
            if (!optional)
            {
                g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_MISSING_DEPENDENCY,
                             "Required dependency disabled: %s requires %s",
                             lrg_mod_get_id (mod), dep_id);
                return FALSE;
            }
        }
    }

    return TRUE;
}

gboolean
lrg_mod_manager_load_all (LrgModManager  *self,
                          GError        **error)
{
    guint i;
    gboolean all_success;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);

    g_ptr_array_set_size (self->loaded_mods, 0);
    all_success = TRUE;

    /* Load in computed order */
    for (i = 0; i < self->load_order->len; i++)
    {
        const gchar *mod_id = g_ptr_array_index (self->load_order, i);
        LrgMod *mod = lrg_mod_manager_get_mod (self, mod_id);
        g_autoptr(GError) mod_error = NULL;

        if (mod == NULL)
            continue;

        if (!lrg_mod_is_enabled (mod))
            continue;

        /* Check dependencies */
        if (!lrg_mod_manager_check_dependencies (self, mod, &mod_error))
        {
            lrg_warning (LRG_LOG_DOMAIN_MOD, "%s", mod_error->message);
            all_success = FALSE;
            continue;
        }

        /* Load mod */
        if (lrg_mod_load (mod, &mod_error))
        {
            g_ptr_array_add (self->loaded_mods, mod);
        }
        else
        {
            lrg_warning (LRG_LOG_DOMAIN_MOD, "Failed to load mod %s: %s",
                         mod_id, mod_error->message);
            all_success = FALSE;
        }
    }

    lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded %u of %u mods",
              self->loaded_mods->len, self->load_order->len);

    return all_success;
}

void
lrg_mod_manager_unload_all (LrgModManager *self)
{
    gint i;

    g_return_if_fail (LRG_IS_MOD_MANAGER (self));

    /* Unload in reverse order */
    for (i = (gint)self->loaded_mods->len - 1; i >= 0; i--)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, (guint)i);
        lrg_mod_unload (mod);
    }

    g_ptr_array_set_size (self->loaded_mods, 0);

    lrg_info (LRG_LOG_DOMAIN_MOD, "Unloaded all mods");
}

gboolean
lrg_mod_manager_reload (LrgModManager  *self,
                        GError        **error)
{
    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);

    lrg_mod_manager_unload_all (self);
    lrg_mod_manager_discover (self, error);

    return lrg_mod_manager_load_all (self, error);
}

/* ==========================================================================
 * Mod Queries
 * ========================================================================== */

GPtrArray *
lrg_mod_manager_get_mods (LrgModManager *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);
    return self->all_mods;
}

GPtrArray *
lrg_mod_manager_get_loaded_mods (LrgModManager *self)
{
    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);
    return self->loaded_mods;
}

LrgMod *
lrg_mod_manager_get_mod (LrgModManager *self,
                         const gchar   *mod_id)
{
    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);
    g_return_val_if_fail (mod_id != NULL, NULL);

    return g_hash_table_lookup (self->mods_by_id, mod_id);
}

gboolean
lrg_mod_manager_has_mod (LrgModManager *self,
                         const gchar   *mod_id)
{
    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);
    g_return_val_if_fail (mod_id != NULL, FALSE);

    return g_hash_table_contains (self->mods_by_id, mod_id);
}

gboolean
lrg_mod_manager_is_mod_loaded (LrgModManager *self,
                               const gchar   *mod_id)
{
    LrgMod *mod;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);
    g_return_val_if_fail (mod_id != NULL, FALSE);

    mod = lrg_mod_manager_get_mod (self, mod_id);
    if (mod == NULL)
        return FALSE;

    return lrg_mod_is_loaded (mod);
}

/* ==========================================================================
 * Individual Mod Control
 * ========================================================================== */

gboolean
lrg_mod_manager_enable_mod (LrgModManager *self,
                            const gchar   *mod_id)
{
    LrgMod *mod;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);
    g_return_val_if_fail (mod_id != NULL, FALSE);

    mod = lrg_mod_manager_get_mod (self, mod_id);
    if (mod == NULL)
        return FALSE;

    lrg_mod_set_enabled (mod, TRUE);
    return TRUE;
}

gboolean
lrg_mod_manager_disable_mod (LrgModManager *self,
                             const gchar   *mod_id)
{
    LrgMod *mod;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);
    g_return_val_if_fail (mod_id != NULL, FALSE);

    mod = lrg_mod_manager_get_mod (self, mod_id);
    if (mod == NULL)
        return FALSE;

    lrg_mod_set_enabled (mod, FALSE);
    return TRUE;
}

/* ==========================================================================
 * Load Order
 * ========================================================================== */

GPtrArray *
lrg_mod_manager_get_load_order (LrgModManager *self)
{
    GPtrArray *copy;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    copy = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < self->load_order->len; i++)
    {
        g_ptr_array_add (copy, g_strdup (g_ptr_array_index (self->load_order, i)));
    }

    return copy;
}

/* ==========================================================================
 * Resource Resolution
 * ========================================================================== */

gchar *
lrg_mod_manager_resolve_path (LrgModManager *self,
                              const gchar   *path)
{
    gint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    /* Check loaded mods in reverse order (later mods override earlier) */
    for (i = (gint)self->loaded_mods->len - 1; i >= 0; i--)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, (guint)i);
        g_autofree gchar *full_path = NULL;

        full_path = lrg_mod_resolve_path (mod, path);
        if (full_path != NULL && g_file_test (full_path, G_FILE_TEST_EXISTS))
        {
            return g_steal_pointer (&full_path);
        }
    }

    return NULL;
}

/* ==========================================================================
 * Provider Queries
 * ========================================================================== */

/**
 * lrg_mod_manager_collect_entity_types:
 * @self: a #LrgModManager
 *
 * Collects entity types from all loaded mods implementing #LrgEntityProvider.
 *
 * Returns: (transfer container) (element-type GType): list of entity GTypes
 */
GList *
lrg_mod_manager_collect_entity_types (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_ENTITY_PROVIDER (mod))
        {
            GList *types;

            types = lrg_entity_provider_get_entity_types (LRG_ENTITY_PROVIDER (mod));
            result = g_list_concat (result, types);
        }
    }

    return result;
}

/**
 * lrg_mod_manager_collect_item_defs:
 * @self: a #LrgModManager
 *
 * Collects item definitions from all loaded mods implementing #LrgItemProvider.
 *
 * Returns: (transfer container) (element-type LrgItemDef): list of #LrgItemDef
 */
GList *
lrg_mod_manager_collect_item_defs (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_ITEM_PROVIDER (mod))
        {
            GList *items;

            items = lrg_item_provider_get_item_defs (LRG_ITEM_PROVIDER (mod));
            result = g_list_concat (result, items);
        }
    }

    return result;
}

/**
 * lrg_mod_manager_collect_dialog_trees:
 * @self: a #LrgModManager
 *
 * Collects dialog trees from all loaded mods implementing #LrgDialogProvider.
 *
 * Returns: (transfer container) (element-type LrgDialogTree): list of #LrgDialogTree
 */
GList *
lrg_mod_manager_collect_dialog_trees (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_DIALOG_PROVIDER (mod))
        {
            GList *trees;

            trees = lrg_dialog_provider_get_dialog_trees (LRG_DIALOG_PROVIDER (mod));
            result = g_list_concat (result, trees);
        }
    }

    return result;
}

/**
 * lrg_mod_manager_collect_quest_defs:
 * @self: a #LrgModManager
 *
 * Collects quest definitions from all loaded mods implementing #LrgQuestProvider.
 *
 * Returns: (transfer container) (element-type LrgQuestDef): list of #LrgQuestDef
 */
GList *
lrg_mod_manager_collect_quest_defs (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_QUEST_PROVIDER (mod))
        {
            GList *quests;

            quests = lrg_quest_provider_get_quest_defs (LRG_QUEST_PROVIDER (mod));
            result = g_list_concat (result, quests);
        }
    }

    return result;
}

/**
 * lrg_mod_manager_collect_bt_node_types:
 * @self: a #LrgModManager
 *
 * Collects behavior tree node types from all loaded mods implementing #LrgAIProvider.
 *
 * Returns: (transfer container) (element-type GType): list of BT node GTypes
 */
GList *
lrg_mod_manager_collect_bt_node_types (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_AI_PROVIDER (mod))
        {
            GList *types;

            types = lrg_ai_provider_get_bt_node_types (LRG_AI_PROVIDER (mod));
            result = g_list_concat (result, types);
        }
    }

    return result;
}

/**
 * lrg_mod_manager_collect_commands:
 * @self: a #LrgModManager
 *
 * Collects console commands from all loaded mods implementing #LrgCommandProvider.
 *
 * Returns: (transfer container) (element-type LrgConsoleCommand): list of commands
 */
GList *
lrg_mod_manager_collect_commands (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_COMMAND_PROVIDER (mod))
        {
            GList *commands;

            commands = lrg_command_provider_get_commands (LRG_COMMAND_PROVIDER (mod));
            result = g_list_concat (result, commands);
        }
    }

    return result;
}

/**
 * lrg_mod_manager_collect_locales:
 * @self: a #LrgModManager
 *
 * Collects locales from all loaded mods implementing #LrgLocaleProvider.
 *
 * Returns: (transfer container) (element-type LrgLocale): list of #LrgLocale
 */
GList *
lrg_mod_manager_collect_locales (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_LOCALE_PROVIDER (mod))
        {
            GList *locales;

            locales = lrg_locale_provider_get_locales (LRG_LOCALE_PROVIDER (mod));
            result = g_list_concat (result, locales);
        }
    }

    return result;
}

/**
 * lrg_mod_manager_collect_scenes:
 * @self: a #LrgModManager
 *
 * Collects scenes from all loaded mods implementing #LrgSceneProvider.
 *
 * Returns: (transfer container) (element-type GObject): list of GrlScene
 */
GList *
lrg_mod_manager_collect_scenes (LrgModManager *self)
{
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = NULL;

    for (i = 0; i < self->loaded_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->loaded_mods, i);

        if (LRG_IS_SCENE_PROVIDER (mod))
        {
            GList *scenes;

            scenes = lrg_scene_provider_get_scenes (LRG_SCENE_PROVIDER (mod));
            result = g_list_concat (result, scenes);
        }
    }

    return result;
}

/* ==========================================================================
 * DLC Queries
 * ========================================================================== */

GPtrArray *
lrg_mod_manager_get_dlcs (LrgModManager *self)
{
    GPtrArray *dlcs;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    dlcs = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < self->all_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->all_mods, i);

        if (LRG_IS_DLC (mod))
            g_ptr_array_add (dlcs, g_object_ref (mod));
    }

    return dlcs;
}

LrgDlc *
lrg_mod_manager_get_dlc (LrgModManager *self,
                          const gchar   *dlc_id)
{
    LrgMod *mod;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);
    g_return_val_if_fail (dlc_id != NULL, NULL);

    mod = lrg_mod_manager_get_mod (self, dlc_id);

    if (mod != NULL && LRG_IS_DLC (mod))
        return LRG_DLC (mod);

    return NULL;
}

GPtrArray *
lrg_mod_manager_get_owned_dlcs (LrgModManager *self)
{
    GPtrArray *owned;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    owned = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < self->all_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->all_mods, i);

        if (LRG_IS_DLC (mod))
        {
            LrgDlc *dlc = LRG_DLC (mod);

            if (lrg_dlc_is_owned (dlc))
                g_ptr_array_add (owned, g_object_ref (dlc));
        }
    }

    return owned;
}

guint
lrg_mod_manager_verify_all_dlc_ownership (LrgModManager  *self,
                                           GError        **error)
{
    guint owned_count;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), 0);

    owned_count = 0;

    for (i = 0; i < self->all_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->all_mods, i);

        if (LRG_IS_DLC (mod))
        {
            LrgDlc *dlc = LRG_DLC (mod);
            LrgDlcOwnershipState state;
            g_autoptr(GError) local_error = NULL;

            state = lrg_dlc_verify_ownership (dlc, &local_error);

            if (state == LRG_DLC_OWNERSHIP_OWNED)
                owned_count++;
            else if (state == LRG_DLC_OWNERSHIP_ERROR && local_error != NULL)
            {
                lrg_warning (LRG_LOG_DOMAIN_MOD,
                             "Failed to verify ownership for DLC %s: %s",
                             lrg_mod_get_id (mod), local_error->message);
            }
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Verified DLC ownership: %u owned", owned_count);

    return owned_count;
}

GPtrArray *
lrg_mod_manager_get_dlcs_by_type (LrgModManager *self,
                                   LrgDlcType     dlc_type)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), NULL);

    result = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < self->all_mods->len; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->all_mods, i);

        if (LRG_IS_DLC (mod))
        {
            LrgDlc *dlc = LRG_DLC (mod);

            if (lrg_dlc_get_dlc_type (dlc) == dlc_type)
                g_ptr_array_add (result, g_object_ref (dlc));
        }
    }

    return result;
}
