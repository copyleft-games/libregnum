/* lrg-card-effect-registry.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEffectRegistry - Singleton registry implementation.
 */

#include "lrg-card-effect-registry.h"
#include "../lrg-log.h"

struct _LrgCardEffectRegistry
{
    GObject     parent_instance;

    GHashTable *executors;  /* effect_type string -> LrgCardEffectExecutor */
};

G_DEFINE_FINAL_TYPE (LrgCardEffectRegistry, lrg_card_effect_registry, G_TYPE_OBJECT)

static LrgCardEffectRegistry *default_registry = NULL;

static void
lrg_card_effect_registry_finalize (GObject *object)
{
    LrgCardEffectRegistry *self = LRG_CARD_EFFECT_REGISTRY (object);

    g_clear_pointer (&self->executors, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_card_effect_registry_parent_class)->finalize (object);
}

static void
lrg_card_effect_registry_class_init (LrgCardEffectRegistryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_card_effect_registry_finalize;
}

static void
lrg_card_effect_registry_init (LrgCardEffectRegistry *self)
{
    self->executors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, g_object_unref);
}

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_card_effect_registry_get_default:
 *
 * Gets the default effect registry singleton. The singleton is
 * created on first access and exists for the lifetime of the
 * application.
 *
 * Returns: (transfer none): the default #LrgCardEffectRegistry
 *
 * Since: 1.0
 */
LrgCardEffectRegistry *
lrg_card_effect_registry_get_default (void)
{
    if (g_once_init_enter (&default_registry))
    {
        LrgCardEffectRegistry *registry;
        registry = g_object_new (LRG_TYPE_CARD_EFFECT_REGISTRY, NULL);
        g_once_init_leave (&default_registry, registry);
    }

    return default_registry;
}

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_card_effect_registry_register:
 * @self: a #LrgCardEffectRegistry
 * @executor: (transfer none): the executor to register
 *
 * Registers an effect executor. The executor's effect type is
 * obtained via lrg_card_effect_executor_get_effect_type().
 * The registry takes a reference to the executor.
 *
 * If an executor for this effect type already exists, it will
 * be replaced with the new one.
 *
 * Since: 1.0
 */
void
lrg_card_effect_registry_register (LrgCardEffectRegistry *self,
                                   LrgCardEffectExecutor *executor)
{
    const gchar *effect_type;

    g_return_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (self));
    g_return_if_fail (LRG_IS_CARD_EFFECT_EXECUTOR (executor));

    effect_type = lrg_card_effect_executor_get_effect_type (executor);
    g_return_if_fail (effect_type != NULL);

    g_hash_table_insert (self->executors,
                         g_strdup (effect_type),
                         g_object_ref (executor));

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Registered effect executor for type: %s", effect_type);
}

/**
 * lrg_card_effect_registry_unregister:
 * @self: a #LrgCardEffectRegistry
 * @effect_type: the effect type to unregister
 *
 * Unregisters the executor for the given effect type.
 *
 * Returns: %TRUE if an executor was removed, %FALSE if not found
 *
 * Since: 1.0
 */
gboolean
lrg_card_effect_registry_unregister (LrgCardEffectRegistry *self,
                                     const gchar           *effect_type)
{
    g_return_val_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (self), FALSE);
    g_return_val_if_fail (effect_type != NULL, FALSE);

    return g_hash_table_remove (self->executors, effect_type);
}

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_card_effect_registry_lookup:
 * @self: a #LrgCardEffectRegistry
 * @effect_type: the effect type to look up
 *
 * Looks up the executor for the given effect type.
 *
 * Returns: (transfer none) (nullable): the executor, or %NULL if not found
 *
 * Since: 1.0
 */
LrgCardEffectExecutor *
lrg_card_effect_registry_lookup (LrgCardEffectRegistry *self,
                                 const gchar           *effect_type)
{
    g_return_val_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (self), NULL);
    g_return_val_if_fail (effect_type != NULL, NULL);

    return g_hash_table_lookup (self->executors, effect_type);
}

/**
 * lrg_card_effect_registry_has_executor:
 * @self: a #LrgCardEffectRegistry
 * @effect_type: the effect type to check
 *
 * Checks if an executor is registered for the given effect type.
 *
 * Returns: %TRUE if an executor is registered
 *
 * Since: 1.0
 */
gboolean
lrg_card_effect_registry_has_executor (LrgCardEffectRegistry *self,
                                       const gchar           *effect_type)
{
    g_return_val_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (self), FALSE);
    g_return_val_if_fail (effect_type != NULL, FALSE);

    return g_hash_table_contains (self->executors, effect_type);
}

/* ==========================================================================
 * Enumeration
 * ========================================================================== */

/**
 * lrg_card_effect_registry_get_effect_types:
 * @self: a #LrgCardEffectRegistry
 *
 * Gets a list of all registered effect types.
 *
 * Returns: (transfer full) (element-type utf8): array of effect type strings
 *
 * Since: 1.0
 */
GPtrArray *
lrg_card_effect_registry_get_effect_types (LrgCardEffectRegistry *self)
{
    GPtrArray *types;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (self), NULL);

    types = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->executors);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        g_ptr_array_add (types, g_strdup ((const gchar *)key));
    }

    return types;
}

/**
 * lrg_card_effect_registry_get_executor_count:
 * @self: a #LrgCardEffectRegistry
 *
 * Gets the number of registered executors.
 *
 * Returns: the number of registered executors
 *
 * Since: 1.0
 */
guint
lrg_card_effect_registry_get_executor_count (LrgCardEffectRegistry *self)
{
    g_return_val_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (self), 0);

    return g_hash_table_size (self->executors);
}

/* ==========================================================================
 * Effect Execution
 * ========================================================================== */

/**
 * lrg_card_effect_registry_execute:
 * @self: a #LrgCardEffectRegistry
 * @effect: the effect to execute
 * @context: the combat context
 * @source: (nullable): the source combatant
 * @target: (nullable): the target combatant
 * @error: (optional): return location for a #GError
 *
 * Convenience method to look up the executor for an effect and
 * execute it in one call. Returns an error if no executor is
 * registered for the effect's type.
 *
 * Returns: %TRUE on success, %FALSE with @error set on failure
 *
 * Since: 1.0
 */
gboolean
lrg_card_effect_registry_execute (LrgCardEffectRegistry  *self,
                                  LrgCardEffect          *effect,
                                  gpointer                context,
                                  gpointer                source,
                                  gpointer                target,
                                  GError                **error)
{
    LrgCardEffectExecutor *executor;
    const gchar *effect_type;

    g_return_val_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (self), FALSE);
    g_return_val_if_fail (effect != NULL, FALSE);

    effect_type = lrg_card_effect_get_effect_type (effect);
    executor = lrg_card_effect_registry_lookup (self, effect_type);

    if (executor == NULL)
    {
        g_set_error (error,
                     LRG_DECKBUILDER_ERROR,
                     LRG_DECKBUILDER_ERROR_EXECUTOR_NOT_FOUND,
                     "No executor registered for effect type: %s",
                     effect_type);
        return FALSE;
    }

    return lrg_card_effect_executor_execute (executor, effect,
                                              context, source, target, error);
}
