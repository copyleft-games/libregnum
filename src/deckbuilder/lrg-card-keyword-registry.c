/* lrg-card-keyword-registry.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardKeywordRegistry - Registry for custom keywords implementation.
 */

#include "lrg-card-keyword-registry.h"
#include "../lrg-log.h"

struct _LrgCardKeywordRegistry
{
    GObject     parent_instance;

    GHashTable *keywords;  /* id -> LrgCardKeywordDef */
};

G_DEFINE_FINAL_TYPE (LrgCardKeywordRegistry, lrg_card_keyword_registry, G_TYPE_OBJECT)

enum
{
    SIGNAL_KEYWORD_REGISTERED,
    SIGNAL_KEYWORD_UNREGISTERED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Singleton
 * ========================================================================== */

static LrgCardKeywordRegistry *default_registry = NULL;

/**
 * lrg_card_keyword_registry_get_default:
 *
 * Gets the default keyword registry singleton.
 *
 * Returns: (transfer none): the default #LrgCardKeywordRegistry
 *
 * Since: 1.0
 */
LrgCardKeywordRegistry *
lrg_card_keyword_registry_get_default (void)
{
    static gsize init = 0;

    if (g_once_init_enter (&init))
    {
        default_registry = g_object_new (LRG_TYPE_CARD_KEYWORD_REGISTRY, NULL);
        g_once_init_leave (&init, 1);
    }

    return default_registry;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_card_keyword_registry_finalize (GObject *object)
{
    LrgCardKeywordRegistry *self = LRG_CARD_KEYWORD_REGISTRY (object);

    g_clear_pointer (&self->keywords, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_card_keyword_registry_parent_class)->finalize (object);
}

static void
lrg_card_keyword_registry_class_init (LrgCardKeywordRegistryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_card_keyword_registry_finalize;

    /**
     * LrgCardKeywordRegistry::keyword-registered:
     * @self: the #LrgCardKeywordRegistry
     * @keyword: the registered keyword
     *
     * Emitted when a keyword is registered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_KEYWORD_REGISTERED] =
        g_signal_new ("keyword-registered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_CARD_KEYWORD_DEF);

    /**
     * LrgCardKeywordRegistry::keyword-unregistered:
     * @self: the #LrgCardKeywordRegistry
     * @id: the unregistered keyword ID
     *
     * Emitted when a keyword is unregistered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_KEYWORD_UNREGISTERED] =
        g_signal_new ("keyword-unregistered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_card_keyword_registry_init (LrgCardKeywordRegistry *self)
{
    self->keywords = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_object_unref);
}

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_card_keyword_registry_register:
 * @self: a #LrgCardKeywordRegistry
 * @keyword: (transfer none): the keyword definition to register
 *
 * Registers a custom keyword definition.
 *
 * Returns: %TRUE if registered successfully, %FALSE if ID already exists
 *
 * Since: 1.0
 */
gboolean
lrg_card_keyword_registry_register (LrgCardKeywordRegistry *self,
                                    LrgCardKeywordDef      *keyword)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_DEF (keyword), FALSE);

    id = lrg_card_keyword_def_get_id (keyword);
    g_return_val_if_fail (id != NULL, FALSE);

    if (g_hash_table_contains (self->keywords, id))
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Keyword '%s' is already registered", id);
        return FALSE;
    }

    g_hash_table_insert (self->keywords,
                         g_strdup (id),
                         g_object_ref (keyword));

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Registered custom keyword '%s'", id);

    g_signal_emit (self, signals[SIGNAL_KEYWORD_REGISTERED], 0, keyword);

    return TRUE;
}

/**
 * lrg_card_keyword_registry_unregister:
 * @self: a #LrgCardKeywordRegistry
 * @id: the keyword ID to unregister
 *
 * Unregisters a custom keyword definition.
 *
 * Returns: %TRUE if unregistered, %FALSE if not found
 *
 * Since: 1.0
 */
gboolean
lrg_card_keyword_registry_unregister (LrgCardKeywordRegistry *self,
                                      const gchar            *id)
{
    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    if (!g_hash_table_remove (self->keywords, id))
    {
        lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                       "Keyword '%s' not found for unregistration", id);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Unregistered custom keyword '%s'", id);

    g_signal_emit (self, signals[SIGNAL_KEYWORD_UNREGISTERED], 0, id);

    return TRUE;
}

/**
 * lrg_card_keyword_registry_is_registered:
 * @self: a #LrgCardKeywordRegistry
 * @id: the keyword ID to check
 *
 * Checks if a custom keyword is registered.
 *
 * Returns: %TRUE if registered
 *
 * Since: 1.0
 */
gboolean
lrg_card_keyword_registry_is_registered (LrgCardKeywordRegistry *self,
                                         const gchar            *id)
{
    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    return g_hash_table_contains (self->keywords, id);
}

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_card_keyword_registry_lookup:
 * @self: a #LrgCardKeywordRegistry
 * @id: the keyword ID
 *
 * Looks up a custom keyword definition by ID.
 *
 * Returns: (transfer none) (nullable): the keyword definition, or %NULL
 *
 * Since: 1.0
 */
LrgCardKeywordDef *
lrg_card_keyword_registry_lookup (LrgCardKeywordRegistry *self,
                                  const gchar            *id)
{
    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->keywords, id);
}

/**
 * lrg_card_keyword_registry_get_all:
 * @self: a #LrgCardKeywordRegistry
 *
 * Gets all registered custom keyword definitions.
 *
 * Returns: (transfer container) (element-type LrgCardKeywordDef): list of keywords
 *
 * Since: 1.0
 */
GList *
lrg_card_keyword_registry_get_all (LrgCardKeywordRegistry *self)
{
    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self), NULL);

    return g_hash_table_get_values (self->keywords);
}

/**
 * lrg_card_keyword_registry_get_count:
 * @self: a #LrgCardKeywordRegistry
 *
 * Gets the number of registered custom keywords.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
guint
lrg_card_keyword_registry_get_count (LrgCardKeywordRegistry *self)
{
    g_return_val_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self), 0);

    return g_hash_table_size (self->keywords);
}

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_card_keyword_registry_clear:
 * @self: a #LrgCardKeywordRegistry
 *
 * Removes all registered custom keywords.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_registry_clear (LrgCardKeywordRegistry *self)
{
    g_return_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self));

    g_hash_table_remove_all (self->keywords);

    lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER,
                   "Cleared all custom keywords from registry");
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
 * lrg_card_keyword_registry_foreach:
 * @self: a #LrgCardKeywordRegistry
 * @func: (scope call): callback for each keyword
 * @user_data: (closure): data to pass to callback
 *
 * Calls a function for each registered keyword.
 *
 * Since: 1.0
 */
void
lrg_card_keyword_registry_foreach (LrgCardKeywordRegistry *self,
                                   GFunc                   func,
                                   gpointer                user_data)
{
    gpointer data[2];

    g_return_if_fail (LRG_IS_CARD_KEYWORD_REGISTRY (self));
    g_return_if_fail (func != NULL);

    data[0] = func;
    data[1] = user_data;

    g_hash_table_foreach (self->keywords, foreach_callback, data);
}
