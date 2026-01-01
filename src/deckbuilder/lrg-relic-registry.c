/* lrg-relic-registry.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRelicRegistry - Registry for relic definitions implementation.
 */

#include "lrg-relic-registry.h"
#include "../lrg-log.h"

struct _LrgRelicRegistry
{
    GObject     parent_instance;

    GHashTable *relics;  /* id -> LrgRelicDef */
};

G_DEFINE_FINAL_TYPE (LrgRelicRegistry, lrg_relic_registry, G_TYPE_OBJECT)

enum
{
    SIGNAL_RELIC_REGISTERED,
    SIGNAL_RELIC_UNREGISTERED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Singleton
 * ========================================================================== */

static LrgRelicRegistry *default_registry = NULL;

/**
 * lrg_relic_registry_get_default:
 *
 * Gets the default relic registry singleton.
 *
 * Returns: (transfer none): the default #LrgRelicRegistry
 *
 * Since: 1.0
 */
LrgRelicRegistry *
lrg_relic_registry_get_default (void)
{
    static gsize init = 0;

    if (g_once_init_enter (&init))
    {
        default_registry = g_object_new (LRG_TYPE_RELIC_REGISTRY, NULL);
        g_once_init_leave (&init, 1);
    }

    return default_registry;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_relic_registry_finalize (GObject *object)
{
    LrgRelicRegistry *self = LRG_RELIC_REGISTRY (object);

    g_clear_pointer (&self->relics, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_relic_registry_parent_class)->finalize (object);
}

static void
lrg_relic_registry_class_init (LrgRelicRegistryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_relic_registry_finalize;

    /**
     * LrgRelicRegistry::relic-registered:
     * @self: the #LrgRelicRegistry
     * @relic: the registered relic
     *
     * Emitted when a relic is registered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RELIC_REGISTERED] =
        g_signal_new ("relic-registered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_RELIC_DEF);

    /**
     * LrgRelicRegistry::relic-unregistered:
     * @self: the #LrgRelicRegistry
     * @id: the unregistered relic ID
     *
     * Emitted when a relic is unregistered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RELIC_UNREGISTERED] =
        g_signal_new ("relic-unregistered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_relic_registry_init (LrgRelicRegistry *self)
{
    self->relics = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_object_unref);
}

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_relic_registry_register:
 * @self: a #LrgRelicRegistry
 * @def: (transfer none): the relic definition to register
 *
 * Registers a relic definition.
 *
 * Returns: %TRUE if registered successfully, %FALSE if ID already exists
 *
 * Since: 1.0
 */
gboolean
lrg_relic_registry_register (LrgRelicRegistry *self,
                              LrgRelicDef      *def)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), FALSE);
    g_return_val_if_fail (LRG_IS_RELIC_DEF (def), FALSE);

    id = lrg_relic_def_get_id (def);
    g_return_val_if_fail (id != NULL, FALSE);

    if (g_hash_table_contains (self->relics, id))
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Relic '%s' is already registered", id);
        return FALSE;
    }

    g_hash_table_insert (self->relics,
                         g_strdup (id),
                         g_object_ref (def));

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Registered relic '%s'", id);

    g_signal_emit (self, signals[SIGNAL_RELIC_REGISTERED], 0, def);

    return TRUE;
}

/**
 * lrg_relic_registry_unregister:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID to unregister
 *
 * Unregisters a relic definition.
 *
 * Returns: %TRUE if unregistered, %FALSE if not found
 *
 * Since: 1.0
 */
gboolean
lrg_relic_registry_unregister (LrgRelicRegistry *self,
                                const gchar      *id)
{
    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    if (!g_hash_table_remove (self->relics, id))
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Relic '%s' not found for unregistration", id);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Unregistered relic '%s'", id);

    g_signal_emit (self, signals[SIGNAL_RELIC_UNREGISTERED], 0, id);

    return TRUE;
}

/**
 * lrg_relic_registry_is_registered:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID to check
 *
 * Checks if a relic is registered.
 *
 * Returns: %TRUE if registered
 *
 * Since: 1.0
 */
gboolean
lrg_relic_registry_is_registered (LrgRelicRegistry *self,
                                   const gchar      *id)
{
    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    return g_hash_table_contains (self->relics, id);
}

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_relic_registry_lookup:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID
 *
 * Looks up a relic definition by ID.
 *
 * Returns: (transfer none) (nullable): the relic definition, or %NULL
 *
 * Since: 1.0
 */
LrgRelicDef *
lrg_relic_registry_lookup (LrgRelicRegistry *self,
                            const gchar      *id)
{
    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->relics, id);
}

/**
 * lrg_relic_registry_create_instance:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID
 *
 * Creates a new relic instance from a registered definition.
 *
 * Returns: (transfer full) (nullable): a new instance, or %NULL if not found
 *
 * Since: 1.0
 */
LrgRelicInstance *
lrg_relic_registry_create_instance (LrgRelicRegistry *self,
                                     const gchar      *id)
{
    LrgRelicDef *def;

    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    def = lrg_relic_registry_lookup (self, id);
    if (def == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Cannot create instance: relic '%s' not found", id);
        return NULL;
    }

    return lrg_relic_instance_new (def);
}

/**
 * lrg_relic_registry_get_all:
 * @self: a #LrgRelicRegistry
 *
 * Gets all registered relic definitions.
 *
 * Returns: (transfer container) (element-type LrgRelicDef): list of definitions
 *
 * Since: 1.0
 */
GList *
lrg_relic_registry_get_all (LrgRelicRegistry *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), NULL);

    return g_hash_table_get_values (self->relics);
}

/**
 * lrg_relic_registry_get_by_rarity:
 * @self: a #LrgRelicRegistry
 * @rarity: the rarity to filter by
 *
 * Gets all relics of a specific rarity.
 *
 * Returns: (transfer container) (element-type LrgRelicDef): list of relics
 *
 * Since: 1.0
 */
GList *
lrg_relic_registry_get_by_rarity (LrgRelicRegistry *self,
                                   LrgRelicRarity    rarity)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), NULL);

    g_hash_table_iter_init (&iter, self->relics);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgRelicDef *def = LRG_RELIC_DEF (value);
        if (lrg_relic_def_get_rarity (def) == rarity)
            result = g_list_prepend (result, def);
    }

    return result;
}

/**
 * lrg_relic_registry_get_count:
 * @self: a #LrgRelicRegistry
 *
 * Gets the number of registered relics.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
guint
lrg_relic_registry_get_count (LrgRelicRegistry *self)
{
    g_return_val_if_fail (LRG_IS_RELIC_REGISTRY (self), 0);

    return g_hash_table_size (self->relics);
}

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_relic_registry_clear:
 * @self: a #LrgRelicRegistry
 *
 * Removes all registered relics.
 *
 * Since: 1.0
 */
void
lrg_relic_registry_clear (LrgRelicRegistry *self)
{
    g_return_if_fail (LRG_IS_RELIC_REGISTRY (self));

    g_hash_table_remove_all (self->relics);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Cleared all relics from registry");
}

static void
foreach_callback (gpointer key,
                  gpointer value,
                  gpointer user_data)
{
    GFunc func = ((gpointer *)user_data)[0];
    gpointer data = ((gpointer *)user_data)[1];

    func (value, data);
}

/**
 * lrg_relic_registry_foreach:
 * @self: a #LrgRelicRegistry
 * @func: (scope call): callback for each relic
 * @user_data: (closure): data to pass to callback
 *
 * Calls a function for each registered relic.
 *
 * Since: 1.0
 */
void
lrg_relic_registry_foreach (LrgRelicRegistry *self,
                             GFunc             func,
                             gpointer          user_data)
{
    gpointer data[2];

    g_return_if_fail (LRG_IS_RELIC_REGISTRY (self));
    g_return_if_fail (func != NULL);

    data[0] = func;
    data[1] = user_data;

    g_hash_table_foreach (self->relics, foreach_callback, data);
}
