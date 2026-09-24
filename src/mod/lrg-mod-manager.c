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
    GHashTable   *disabled_ids;  /* ids disabled by the host; survives discover */
    GHashTable   *cycle_ids;     /* ids caught in a dependency cycle */
};

enum
{
    SIGNAL_MOD_LOADED,
    SIGNAL_MOD_FAILED,
    SIGNAL_MOD_UNLOADED,
    SIGNAL_MOD_PREPARE_SCRIPTING,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

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
    g_hash_table_destroy (self->disabled_ids);
    g_hash_table_destroy (self->cycle_ids);

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

    /**
     * LrgModManager::mod-loaded:
     * @self: the manager
     * @mod: the mod that finished loading
     *
     * Emitted after a mod loads successfully.
     *
     * Since: 0.2
     */
    signals[SIGNAL_MOD_LOADED] =
        g_signal_new ("mod-loaded", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_MOD);

    /**
     * LrgModManager::mod-failed:
     * @self: the manager
     * @mod: the mod that could not load; see lrg_mod_get_error()
     *
     * Emitted when a mod fails to load, including unmet dependencies and
     * dependency cycles.
     *
     * Since: 0.2
     */
    signals[SIGNAL_MOD_FAILED] =
        g_signal_new ("mod-failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_MOD);

    /**
     * LrgModManager::mod-unloaded:
     * @self: the manager
     * @mod: the mod that was unloaded
     *
     * Emitted after a loaded mod is unloaded.
     *
     * Since: 0.2
     */
    signals[SIGNAL_MOD_UNLOADED] =
        g_signal_new ("mod-unloaded", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_MOD);

    /**
     * LrgModManager::mod-prepare-scripting:
     * @self: the manager
     * @mod: the script mod being loaded
     * @scripting: its new context, before the entry point runs
     *
     * Re-emits #LrgMod::prepare-scripting for every managed mod, so a host
     * can publish its API to all script mods with one handler.
     *
     * Since: 0.2
     */
    signals[SIGNAL_MOD_PREPARE_SCRIPTING] =
        g_signal_new ("mod-prepare-scripting", G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, LRG_TYPE_MOD, LRG_TYPE_SCRIPTING);
}

static void
lrg_mod_manager_init (LrgModManager *self)
{
    self->loader = lrg_mod_loader_new ();
    self->mods_by_id = g_hash_table_new (g_str_hash, g_str_equal);
    self->all_mods = g_ptr_array_new_with_free_func (g_object_unref);
    self->loaded_mods = g_ptr_array_new ();
    self->load_order = g_ptr_array_new_with_free_func (g_free);
    self->disabled_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    self->cycle_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

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

/* One node of the ordering graph */
typedef struct
{
    LrgMod    *mod;
    guint      index;     /* discovery position, the final tiebreak */
    gint       priority;  /* lower loads earlier among ready mods */
    guint      in_degree; /* unplaced mods that must load first */
    GPtrArray *before;    /* indices of mods that must load after this one */
    gboolean   placed;
} OrderNode;

/* Adds the edge "first loads before second" (ignoring duplicates) */
static void
order_add_edge (OrderNode *nodes,
                guint      first,
                guint      second)
{
    guint i;

    if (first == second)
        return;

    for (i = 0; i < nodes[first].before->len; i++)
        if (GPOINTER_TO_UINT (g_ptr_array_index (nodes[first].before, i)) == second)
            return;

    g_ptr_array_add (nodes[first].before, GUINT_TO_POINTER (second));
    nodes[second].in_degree++;
}

/* Finds a node index by mod id, or G_MAXUINT */
static guint
order_find (GHashTable  *index_by_id,
            const gchar *mod_id)
{
    gpointer value;

    if (!g_hash_table_lookup_extended (index_by_id, mod_id, NULL, &value))
        return G_MAXUINT;
    return GPOINTER_TO_UINT (value);
}

/*
 * Picks the next node among @candidates: lowest priority value first, then
 * discovery order.  With @require_ready only nodes whose prerequisites are
 * placed qualify.
 */
static guint
order_pick (OrderNode *nodes,
            guint      n_nodes,
            gboolean   require_ready)
{
    guint best;
    guint i;

    best = G_MAXUINT;
    for (i = 0; i < n_nodes; i++)
    {
        if (nodes[i].placed || (require_ready && nodes[i].in_degree > 0))
            continue;
        if (best == G_MAXUINT ||
            nodes[i].priority < nodes[best].priority ||
            (nodes[i].priority == nodes[best].priority && nodes[i].index < nodes[best].index))
            best = i;
    }
    return best;
}

/*
 * Computes the load order over every discovered mod (enabled or not, so
 * toggling a mod later needs no recomputation).  Dependencies (required and
 * optional, when present), load_after and load_before are hard edges; among
 * mods that are free to load, priority and then discovery order decide.  A
 * cycle cannot be ordered: its members are recorded in cycle_ids and placed
 * last by the same tiebreak, and load_all() fails them.
 */
static GPtrArray *
compute_load_order (LrgModManager *self)
{
    GHashTable *index_by_id;
    OrderNode  *nodes;
    GPtrArray  *result;
    guint       n_nodes;
    guint       i;
    guint       j;

    n_nodes = self->all_mods->len;
    nodes = g_new0 (OrderNode, n_nodes > 0 ? n_nodes : 1);
    index_by_id = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_remove_all (self->cycle_ids);

    for (i = 0; i < n_nodes; i++)
    {
        LrgMod *mod = g_ptr_array_index (self->all_mods, i);

        nodes[i].mod = mod;
        nodes[i].index = i;
        nodes[i].priority = (gint)lrg_mod_manifest_get_priority (lrg_mod_get_manifest (mod));
        nodes[i].before = g_ptr_array_new ();
        g_hash_table_insert (index_by_id, (gpointer)lrg_mod_get_id (mod), GUINT_TO_POINTER (i));
    }

    /* Edges from dependencies, load_after and load_before */
    for (i = 0; i < n_nodes; i++)
    {
        LrgModManifest *manifest = lrg_mod_get_manifest (nodes[i].mod);
        GPtrArray      *deps = lrg_mod_manifest_get_dependencies (manifest);
        GPtrArray      *after = lrg_mod_manifest_get_load_after (manifest);
        GPtrArray      *before = lrg_mod_manifest_get_load_before (manifest);

        for (j = 0; j < deps->len; j++)
        {
            guint other = order_find (index_by_id,
                lrg_mod_dependency_get_mod_id (g_ptr_array_index (deps, j)));

            if (other != G_MAXUINT)
                order_add_edge (nodes, other, i);
        }
        for (j = 0; j < after->len; j++)
        {
            guint other = order_find (index_by_id, g_ptr_array_index (after, j));

            if (other != G_MAXUINT)
                order_add_edge (nodes, other, i);
        }
        for (j = 0; j < before->len; j++)
        {
            guint other = order_find (index_by_id, g_ptr_array_index (before, j));

            if (other != G_MAXUINT)
                order_add_edge (nodes, i, other);
        }
    }

    /* Kahn's algorithm with a deterministic choice among ready mods */
    result = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < n_nodes; i++)
    {
        guint next = order_pick (nodes, n_nodes, TRUE);

        if (next == G_MAXUINT)
        {
            /* Only cycle members remain unplaced */
            next = order_pick (nodes, n_nodes, FALSE);
            g_hash_table_add (self->cycle_ids, g_strdup (lrg_mod_get_id (nodes[next].mod)));
            lrg_info (LRG_LOG_DOMAIN_MOD, "Mod %s is part of a dependency cycle",
                      lrg_mod_get_id (nodes[next].mod));
        }

        nodes[next].placed = TRUE;
        for (j = 0; j < nodes[next].before->len; j++)
        {
            guint later = GPOINTER_TO_UINT (g_ptr_array_index (nodes[next].before, j));

            if (nodes[later].in_degree > 0)
                nodes[later].in_degree--;
        }
        g_ptr_array_add (result, g_strdup (lrg_mod_get_id (nodes[next].mod)));
    }

    for (i = 0; i < n_nodes; i++)
        g_ptr_array_unref (nodes[i].before);
    g_free (nodes);
    g_hash_table_destroy (index_by_id);

    return result;
}

/* Re-emits a mod's prepare-scripting on the manager */
static void
on_mod_prepare_scripting (LrgMod        *mod,
                          LrgScripting  *scripting,
                          LrgModManager *self)
{
    g_signal_emit (self, signals[SIGNAL_MOD_PREPARE_SCRIPTING], 0, mod, scripting);
}

/* Records a failure on @mod and tells listeners */
static void
fail_mod (LrgModManager *self,
          LrgMod        *mod,
          const gchar   *message)
{
    lrg_mod_mark_failed (mod, message);
    lrg_info (LRG_LOG_DOMAIN_MOD, "Mod %s failed: %s", lrg_mod_get_id (mod), message);
    g_signal_emit (self, signals[SIGNAL_MOD_FAILED], 0, mod);
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

        /* Host choices survive rediscovery */
        if (g_hash_table_contains (self->disabled_ids, mod_id))
            lrg_mod_set_enabled (mod, FALSE);

        g_signal_connect_object (mod, "prepare-scripting",
                                 G_CALLBACK (on_mod_prepare_scripting), self, 0);
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

/*
 * Loads one mod whose prerequisites were already processed (the load order
 * guarantees that): cycle members and mods with a missing, disabled or
 * failed required dependency fail without being loaded.
 */
static gboolean
load_one (LrgModManager *self,
          LrgMod        *mod)
{
    GPtrArray        *deps;
    g_autoptr(GError) mod_error = NULL;
    guint             i;

    if (g_hash_table_contains (self->cycle_ids, lrg_mod_get_id (mod)))
    {
        fail_mod (self, mod, "Circular dependency between mods");
        return FALSE;
    }

    deps = lrg_mod_manifest_get_dependencies (lrg_mod_get_manifest (mod));
    for (i = 0; i < deps->len; i++)
    {
        LrgModDependency *dep = g_ptr_array_index (deps, i);
        const gchar      *dep_id = lrg_mod_dependency_get_mod_id (dep);
        LrgMod           *dep_mod = lrg_mod_manager_get_mod (self, dep_id);
        g_autofree gchar *message = NULL;

        if (lrg_mod_dependency_is_optional (dep))
            continue;

        if (dep_mod == NULL)
            message = g_strdup_printf ("Missing required dependency: %s", dep_id);
        else if (!lrg_mod_is_enabled (dep_mod))
            message = g_strdup_printf ("Required dependency is disabled: %s", dep_id);
        else if (!lrg_mod_is_loaded (dep_mod))
            message = g_strdup_printf ("Required dependency failed to load: %s", dep_id);

        if (message != NULL)
        {
            fail_mod (self, mod, message);
            return FALSE;
        }
    }

    if (!lrg_mod_load (mod, &mod_error))
    {
        lrg_mod_mark_failed (mod, mod_error != NULL ? mod_error->message : "load failed");
        g_signal_emit (self, signals[SIGNAL_MOD_FAILED], 0, mod);
        return FALSE;
    }

    g_ptr_array_add (self->loaded_mods, mod);
    g_signal_emit (self, signals[SIGNAL_MOD_LOADED], 0, mod);
    return TRUE;
}

gboolean
lrg_mod_manager_load_all (LrgModManager  *self,
                          GError        **error)
{
    guint    i;
    guint    failures;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);

    g_ptr_array_set_size (self->loaded_mods, 0);
    failures = 0;

    /* Load in computed order; dependencies are always processed first */
    for (i = 0; i < self->load_order->len; i++)
    {
        const gchar *mod_id = g_ptr_array_index (self->load_order, i);
        LrgMod      *mod = lrg_mod_manager_get_mod (self, mod_id);

        if (mod == NULL || !lrg_mod_is_enabled (mod))
            continue;

        /* Already loaded mods stay loaded and keep their place */
        if (lrg_mod_is_loaded (mod))
        {
            g_ptr_array_add (self->loaded_mods, mod);
            continue;
        }

        /*
         * Failed mods (invalid manifests, earlier failures, mods a host marked
         * failed) stay failed until rediscovered or reloaded individually.
         */
        if (lrg_mod_get_state (mod) == LRG_MOD_STATE_FAILED)
        {
            failures++;
            continue;
        }

        if (!load_one (self, mod))
            failures++;
    }

    lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded %u of %u mods",
              self->loaded_mods->len, self->load_order->len);

    if (failures > 0)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "%u mod(s) failed to load", failures);
        return FALSE;
    }

    return TRUE;
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
        g_signal_emit (self, signals[SIGNAL_MOD_UNLOADED], 0, mod);
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

    /* Remember the choice even for mods not discovered yet */
    g_hash_table_remove (self->disabled_ids, mod_id);

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

    /* Remember the choice even for mods not discovered yet */
    g_hash_table_add (self->disabled_ids, g_strdup (mod_id));

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

/* ==========================================================================
 * Single-mod reload
 * ========================================================================== */

/**
 * lrg_mod_manager_reload_mod:
 * @self: an #LrgModManager
 * @mod_id: the mod to reload
 * @error: (nullable): return location for error
 *
 * Unloads @mod_id if it is loaded and loads it again (for example after its
 * files changed), emitting #LrgModManager::mod-unloaded and then
 * #LrgModManager::mod-loaded or #LrgModManager::mod-failed.  Mods that
 * depend on it keep running.  A disabled mod is only unloaded.
 *
 * Returns: %TRUE when the mod is loaded afterwards (or was disabled)
 */
gboolean
lrg_mod_manager_reload_mod (LrgModManager  *self,
                            const gchar    *mod_id,
                            GError        **error)
{
    LrgMod *mod;

    g_return_val_if_fail (LRG_IS_MOD_MANAGER (self), FALSE);
    g_return_val_if_fail (mod_id != NULL, FALSE);

    mod = lrg_mod_manager_get_mod (self, mod_id);
    if (mod == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_NOT_FOUND,
                     "Unknown mod: %s", mod_id);
        return FALSE;
    }

    if (lrg_mod_is_loaded (mod))
    {
        lrg_mod_unload (mod);
        g_ptr_array_remove (self->loaded_mods, mod);
        g_signal_emit (self, signals[SIGNAL_MOD_UNLOADED], 0, mod);
    }

    if (!lrg_mod_is_enabled (mod))
        return TRUE;

    if (!load_one (self, mod))
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "%s", lrg_mod_get_error (mod) != NULL ? lrg_mod_get_error (mod)
                                                           : "load failed");
        return FALSE;
    }

    return TRUE;
}

/**
 * lrg_mod_manager_unload_mod:
 * @self: an #LrgModManager
 * @mod_id: the mod to unload
 *
 * Unloads one loaded mod, emitting #LrgModManager::mod-unloaded.  Nothing
 * happens for unknown or unloaded mods.
 */
void
lrg_mod_manager_unload_mod (LrgModManager *self,
                            const gchar   *mod_id)
{
    LrgMod *mod;

    g_return_if_fail (LRG_IS_MOD_MANAGER (self));
    g_return_if_fail (mod_id != NULL);

    mod = lrg_mod_manager_get_mod (self, mod_id);
    if (mod == NULL || !lrg_mod_is_loaded (mod))
        return;

    lrg_mod_unload (mod);
    g_ptr_array_remove (self->loaded_mods, mod);
    g_signal_emit (self, signals[SIGNAL_MOD_UNLOADED], 0, mod);
}
