/* lrg-status-effect-registry.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgStatusEffectRegistry - Registry for status effect definitions implementation.
 */

#include "lrg-status-effect-registry.h"
#include "../lrg-log.h"

struct _LrgStatusEffectRegistry
{
    GObject     parent_instance;

    GHashTable *effects;  /* id -> LrgStatusEffectDef */
};

G_DEFINE_FINAL_TYPE (LrgStatusEffectRegistry, lrg_status_effect_registry, G_TYPE_OBJECT)

enum
{
    SIGNAL_EFFECT_REGISTERED,
    SIGNAL_EFFECT_UNREGISTERED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Singleton
 * ========================================================================== */

static LrgStatusEffectRegistry *default_registry = NULL;

/**
 * lrg_status_effect_registry_get_default:
 *
 * Gets the default status effect registry singleton.
 *
 * Returns: (transfer none): the default #LrgStatusEffectRegistry
 *
 * Since: 1.0
 */
LrgStatusEffectRegistry *
lrg_status_effect_registry_get_default (void)
{
    static gsize init = 0;

    if (g_once_init_enter (&init))
    {
        default_registry = g_object_new (LRG_TYPE_STATUS_EFFECT_REGISTRY, NULL);
        g_once_init_leave (&init, 1);
    }

    return default_registry;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_status_effect_registry_finalize (GObject *object)
{
    LrgStatusEffectRegistry *self = LRG_STATUS_EFFECT_REGISTRY (object);

    g_clear_pointer (&self->effects, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_status_effect_registry_parent_class)->finalize (object);
}

static void
lrg_status_effect_registry_class_init (LrgStatusEffectRegistryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_status_effect_registry_finalize;

    /**
     * LrgStatusEffectRegistry::effect-registered:
     * @self: the #LrgStatusEffectRegistry
     * @effect: the registered status effect
     *
     * Emitted when a status effect is registered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EFFECT_REGISTERED] =
        g_signal_new ("effect-registered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_STATUS_EFFECT_DEF);

    /**
     * LrgStatusEffectRegistry::effect-unregistered:
     * @self: the #LrgStatusEffectRegistry
     * @id: the unregistered status effect ID
     *
     * Emitted when a status effect is unregistered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EFFECT_UNREGISTERED] =
        g_signal_new ("effect-unregistered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_status_effect_registry_init (LrgStatusEffectRegistry *self)
{
    self->effects = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_object_unref);
}

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_status_effect_registry_register:
 * @self: a #LrgStatusEffectRegistry
 * @def: (transfer none): the status effect definition to register
 *
 * Registers a status effect definition.
 *
 * Returns: %TRUE if registered successfully, %FALSE if ID already exists
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_registry_register (LrgStatusEffectRegistry *self,
                                      LrgStatusEffectDef      *def)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), FALSE);
    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_DEF (def), FALSE);

    id = lrg_status_effect_def_get_id (def);
    g_return_val_if_fail (id != NULL, FALSE);

    if (g_hash_table_contains (self->effects, id))
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Status effect '%s' is already registered", id);
        return FALSE;
    }

    g_hash_table_insert (self->effects,
                         g_strdup (id),
                         g_object_ref (def));

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Registered status effect '%s'", id);

    g_signal_emit (self, signals[SIGNAL_EFFECT_REGISTERED], 0, def);

    return TRUE;
}

/**
 * lrg_status_effect_registry_unregister:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID to unregister
 *
 * Unregisters a status effect definition.
 *
 * Returns: %TRUE if unregistered, %FALSE if not found
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_registry_unregister (LrgStatusEffectRegistry *self,
                                        const gchar             *id)
{
    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    if (!g_hash_table_remove (self->effects, id))
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Status effect '%s' not found for unregistration", id);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Unregistered status effect '%s'", id);

    g_signal_emit (self, signals[SIGNAL_EFFECT_UNREGISTERED], 0, id);

    return TRUE;
}

/**
 * lrg_status_effect_registry_is_registered:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID to check
 *
 * Checks if a status effect is registered.
 *
 * Returns: %TRUE if registered
 *
 * Since: 1.0
 */
gboolean
lrg_status_effect_registry_is_registered (LrgStatusEffectRegistry *self,
                                           const gchar             *id)
{
    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    return g_hash_table_contains (self->effects, id);
}

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_status_effect_registry_lookup:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID
 *
 * Looks up a status effect definition by ID.
 *
 * Returns: (transfer none) (nullable): the status effect definition, or %NULL
 *
 * Since: 1.0
 */
LrgStatusEffectDef *
lrg_status_effect_registry_lookup (LrgStatusEffectRegistry *self,
                                    const gchar             *id)
{
    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->effects, id);
}

/**
 * lrg_status_effect_registry_create_instance:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID
 * @stacks: initial stack count
 *
 * Creates a new status effect instance from a registered definition.
 *
 * Returns: (transfer full) (nullable): a new instance, or %NULL if not found
 *
 * Since: 1.0
 */
LrgStatusEffectInstance *
lrg_status_effect_registry_create_instance (LrgStatusEffectRegistry *self,
                                             const gchar             *id,
                                             gint                     stacks)
{
    LrgStatusEffectDef *def;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (stacks > 0, NULL);

    def = lrg_status_effect_registry_lookup (self, id);
    if (def == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Cannot create instance: status effect '%s' not found", id);
        return NULL;
    }

    return lrg_status_effect_instance_new (def, stacks);
}

/**
 * lrg_status_effect_registry_get_all:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets all registered status effect definitions.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef): list of definitions
 *
 * Since: 1.0
 */
GList *
lrg_status_effect_registry_get_all (LrgStatusEffectRegistry *self)
{
    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), NULL);

    return g_hash_table_get_values (self->effects);
}

/**
 * lrg_status_effect_registry_get_buffs:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets all registered buff definitions.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef): list of buffs
 *
 * Since: 1.0
 */
GList *
lrg_status_effect_registry_get_buffs (LrgStatusEffectRegistry *self)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), NULL);

    g_hash_table_iter_init (&iter, self->effects);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgStatusEffectDef *def = LRG_STATUS_EFFECT_DEF (value);
        if (lrg_status_effect_def_is_buff (def))
            result = g_list_prepend (result, def);
    }

    return result;
}

/**
 * lrg_status_effect_registry_get_debuffs:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets all registered debuff definitions.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef): list of debuffs
 *
 * Since: 1.0
 */
GList *
lrg_status_effect_registry_get_debuffs (LrgStatusEffectRegistry *self)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), NULL);

    g_hash_table_iter_init (&iter, self->effects);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgStatusEffectDef *def = LRG_STATUS_EFFECT_DEF (value);
        if (lrg_status_effect_def_is_debuff (def))
            result = g_list_prepend (result, def);
    }

    return result;
}

/**
 * lrg_status_effect_registry_get_count:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets the number of registered status effects.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
guint
lrg_status_effect_registry_get_count (LrgStatusEffectRegistry *self)
{
    g_return_val_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self), 0);

    return g_hash_table_size (self->effects);
}

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_status_effect_registry_clear:
 * @self: a #LrgStatusEffectRegistry
 *
 * Removes all registered status effects.
 *
 * Since: 1.0
 */
void
lrg_status_effect_registry_clear (LrgStatusEffectRegistry *self)
{
    g_return_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self));

    g_hash_table_remove_all (self->effects);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
               "Cleared all status effects from registry");
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
 * lrg_status_effect_registry_foreach:
 * @self: a #LrgStatusEffectRegistry
 * @func: (scope call): callback for each status effect
 * @user_data: (closure): data to pass to callback
 *
 * Calls a function for each registered status effect.
 *
 * Since: 1.0
 */
void
lrg_status_effect_registry_foreach (LrgStatusEffectRegistry *self,
                                     GFunc                    func,
                                     gpointer                 user_data)
{
    gpointer data[2];

    g_return_if_fail (LRG_IS_STATUS_EFFECT_REGISTRY (self));
    g_return_if_fail (func != NULL);

    data[0] = func;
    data[1] = user_data;

    g_hash_table_foreach (self->effects, foreach_callback, data);
}
