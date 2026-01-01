/* lrg-effect-stack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEffectStack - Effect resolution stack implementation.
 */

#include "lrg-effect-stack.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Effect Stack Entry Implementation
 * ========================================================================== */

struct _LrgEffectStackEntry
{
    LrgCardEffect *effect;
    gpointer       source;
    gpointer       target;
};

G_DEFINE_BOXED_TYPE (LrgEffectStackEntry, lrg_effect_stack_entry,
                     lrg_effect_stack_entry_copy, lrg_effect_stack_entry_free)

/**
 * lrg_effect_stack_entry_new:
 * @effect: the effect to queue
 * @source: (nullable): the source combatant
 * @target: (nullable): the target combatant
 *
 * Creates a new stack entry. The entry takes a copy of the effect.
 *
 * Returns: (transfer full): a new #LrgEffectStackEntry
 *
 * Since: 1.0
 */
LrgEffectStackEntry *
lrg_effect_stack_entry_new (LrgCardEffect *effect,
                            gpointer       source,
                            gpointer       target)
{
    LrgEffectStackEntry *entry;

    g_return_val_if_fail (effect != NULL, NULL);

    entry = g_new0 (LrgEffectStackEntry, 1);
    entry->effect = lrg_card_effect_copy (effect);
    entry->source = source;
    entry->target = target;

    return entry;
}

/**
 * lrg_effect_stack_entry_copy:
 * @entry: a #LrgEffectStackEntry
 *
 * Creates a copy of the entry.
 *
 * Returns: (transfer full): a copy of @entry
 *
 * Since: 1.0
 */
LrgEffectStackEntry *
lrg_effect_stack_entry_copy (LrgEffectStackEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);

    return lrg_effect_stack_entry_new (entry->effect,
                                        entry->source,
                                        entry->target);
}

/**
 * lrg_effect_stack_entry_free:
 * @entry: a #LrgEffectStackEntry
 *
 * Frees the entry and its associated effect.
 *
 * Since: 1.0
 */
void
lrg_effect_stack_entry_free (LrgEffectStackEntry *entry)
{
    if (entry == NULL)
        return;

    lrg_card_effect_free (entry->effect);
    g_free (entry);
}

/**
 * lrg_effect_stack_entry_get_effect:
 * @entry: a #LrgEffectStackEntry
 *
 * Gets the effect from the entry.
 *
 * Returns: (transfer none): the effect
 *
 * Since: 1.0
 */
LrgCardEffect *
lrg_effect_stack_entry_get_effect (LrgEffectStackEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);
    return entry->effect;
}

/**
 * lrg_effect_stack_entry_get_source:
 * @entry: a #LrgEffectStackEntry
 *
 * Gets the source combatant from the entry.
 *
 * Returns: (transfer none) (nullable): the source
 *
 * Since: 1.0
 */
gpointer
lrg_effect_stack_entry_get_source (LrgEffectStackEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);
    return entry->source;
}

/**
 * lrg_effect_stack_entry_get_target:
 * @entry: a #LrgEffectStackEntry
 *
 * Gets the target combatant from the entry.
 *
 * Returns: (transfer none) (nullable): the target
 *
 * Since: 1.0
 */
gpointer
lrg_effect_stack_entry_get_target (LrgEffectStackEntry *entry)
{
    g_return_val_if_fail (entry != NULL, NULL);
    return entry->target;
}

/* ==========================================================================
 * Effect Stack Implementation
 * ========================================================================== */

struct _LrgEffectStack
{
    GObject                parent_instance;

    LrgCardEffectRegistry *registry;
    GPtrArray             *entries;  /* Array of LrgEffectStackEntry */
    gboolean               sorted;   /* TRUE if entries are sorted by priority */
};

G_DEFINE_FINAL_TYPE (LrgEffectStack, lrg_effect_stack, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_REGISTRY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_EFFECT_RESOLVED,
    SIGNAL_EFFECT_FAILED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_effect_stack_finalize (GObject *object)
{
    LrgEffectStack *self = LRG_EFFECT_STACK (object);

    g_clear_pointer (&self->entries, g_ptr_array_unref);
    g_clear_object (&self->registry);

    G_OBJECT_CLASS (lrg_effect_stack_parent_class)->finalize (object);
}

static void
lrg_effect_stack_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgEffectStack *self = LRG_EFFECT_STACK (object);

    switch (prop_id)
    {
    case PROP_REGISTRY:
        self->registry = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_effect_stack_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgEffectStack *self = LRG_EFFECT_STACK (object);

    switch (prop_id)
    {
    case PROP_REGISTRY:
        g_value_set_object (value, self->registry);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_effect_stack_class_init (LrgEffectStackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_effect_stack_finalize;
    object_class->set_property = lrg_effect_stack_set_property;
    object_class->get_property = lrg_effect_stack_get_property;

    /**
     * LrgEffectStack:registry:
     *
     * The effect registry used for executing effects.
     *
     * Since: 1.0
     */
    properties[PROP_REGISTRY] =
        g_param_spec_object ("registry",
                             "Registry",
                             "The effect registry",
                             LRG_TYPE_CARD_EFFECT_REGISTRY,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgEffectStack::effect-resolved:
     * @self: the #LrgEffectStack
     * @effect: the effect that was resolved
     *
     * Emitted when an effect is successfully resolved.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EFFECT_RESOLVED] =
        g_signal_new ("effect-resolved",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_CARD_EFFECT);

    /**
     * LrgEffectStack::effect-failed:
     * @self: the #LrgEffectStack
     * @effect: the effect that failed
     * @error: the error that occurred
     *
     * Emitted when an effect fails to resolve.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EFFECT_FAILED] =
        g_signal_new ("effect-failed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_CARD_EFFECT,
                      G_TYPE_ERROR);
}

static void
lrg_effect_stack_init (LrgEffectStack *self)
{
    self->entries = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_effect_stack_entry_free);
    self->sorted = TRUE;  /* Empty array is sorted */
}

/**
 * lrg_effect_stack_new:
 * @registry: the effect registry to use for execution
 *
 * Creates a new effect stack.
 *
 * Returns: (transfer full): a new #LrgEffectStack
 *
 * Since: 1.0
 */
LrgEffectStack *
lrg_effect_stack_new (LrgCardEffectRegistry *registry)
{
    g_return_val_if_fail (LRG_IS_CARD_EFFECT_REGISTRY (registry), NULL);

    return g_object_new (LRG_TYPE_EFFECT_STACK,
                         "registry", registry,
                         NULL);
}

/* Sort comparison: higher priority comes first */
static gint
compare_entry_priority (gconstpointer a, gconstpointer b)
{
    const LrgEffectStackEntry *entry_a = *(const LrgEffectStackEntry **)a;
    const LrgEffectStackEntry *entry_b = *(const LrgEffectStackEntry **)b;
    gint priority_a = lrg_card_effect_get_priority (entry_a->effect);
    gint priority_b = lrg_card_effect_get_priority (entry_b->effect);

    /* Descending order (higher priority first) */
    return priority_b - priority_a;
}

static void
ensure_sorted (LrgEffectStack *self)
{
    if (!self->sorted && self->entries->len > 1)
    {
        g_ptr_array_sort (self->entries, compare_entry_priority);
        self->sorted = TRUE;
    }
}

/* ==========================================================================
 * Stack Operations
 * ========================================================================== */

/**
 * lrg_effect_stack_push:
 * @self: a #LrgEffectStack
 * @entry: (transfer full): the entry to push
 *
 * Pushes an effect entry onto the stack. The stack takes
 * ownership of the entry.
 *
 * Since: 1.0
 */
void
lrg_effect_stack_push (LrgEffectStack      *self,
                       LrgEffectStackEntry *entry)
{
    g_return_if_fail (LRG_IS_EFFECT_STACK (self));
    g_return_if_fail (entry != NULL);

    g_ptr_array_add (self->entries, entry);
    self->sorted = FALSE;
}

/**
 * lrg_effect_stack_push_effect:
 * @self: a #LrgEffectStack
 * @effect: the effect to push
 * @source: (nullable): the source combatant
 * @target: (nullable): the target combatant
 *
 * Convenience method to create an entry and push it in one call.
 *
 * Since: 1.0
 */
void
lrg_effect_stack_push_effect (LrgEffectStack *self,
                              LrgCardEffect  *effect,
                              gpointer        source,
                              gpointer        target)
{
    LrgEffectStackEntry *entry;

    g_return_if_fail (LRG_IS_EFFECT_STACK (self));
    g_return_if_fail (effect != NULL);

    entry = lrg_effect_stack_entry_new (effect, source, target);
    lrg_effect_stack_push (self, entry);
}

/**
 * lrg_effect_stack_peek:
 * @self: a #LrgEffectStack
 *
 * Peeks at the next effect to be resolved (highest priority).
 * Does not remove the entry from the stack.
 *
 * Returns: (transfer none) (nullable): the next entry, or %NULL if empty
 *
 * Since: 1.0
 */
LrgEffectStackEntry *
lrg_effect_stack_peek (LrgEffectStack *self)
{
    g_return_val_if_fail (LRG_IS_EFFECT_STACK (self), NULL);

    if (self->entries->len == 0)
        return NULL;

    ensure_sorted (self);
    return g_ptr_array_index (self->entries, 0);
}

/**
 * lrg_effect_stack_pop:
 * @self: a #LrgEffectStack
 *
 * Pops the next effect entry from the stack (highest priority).
 * The caller takes ownership of the returned entry.
 *
 * Returns: (transfer full) (nullable): the entry, or %NULL if empty
 *
 * Since: 1.0
 */
LrgEffectStackEntry *
lrg_effect_stack_pop (LrgEffectStack *self)
{
    LrgEffectStackEntry *entry;

    g_return_val_if_fail (LRG_IS_EFFECT_STACK (self), NULL);

    if (self->entries->len == 0)
        return NULL;

    ensure_sorted (self);
    entry = g_ptr_array_steal_index (self->entries, 0);

    return entry;
}

/**
 * lrg_effect_stack_is_empty:
 * @self: a #LrgEffectStack
 *
 * Checks if the stack is empty.
 *
 * Returns: %TRUE if empty
 *
 * Since: 1.0
 */
gboolean
lrg_effect_stack_is_empty (LrgEffectStack *self)
{
    g_return_val_if_fail (LRG_IS_EFFECT_STACK (self), TRUE);
    return self->entries->len == 0;
}

/**
 * lrg_effect_stack_get_count:
 * @self: a #LrgEffectStack
 *
 * Gets the number of entries on the stack.
 *
 * Returns: the entry count
 *
 * Since: 1.0
 */
guint
lrg_effect_stack_get_count (LrgEffectStack *self)
{
    g_return_val_if_fail (LRG_IS_EFFECT_STACK (self), 0);
    return self->entries->len;
}

/**
 * lrg_effect_stack_clear:
 * @self: a #LrgEffectStack
 *
 * Clears all entries from the stack.
 *
 * Since: 1.0
 */
void
lrg_effect_stack_clear (LrgEffectStack *self)
{
    g_return_if_fail (LRG_IS_EFFECT_STACK (self));

    g_ptr_array_set_size (self->entries, 0);
    self->sorted = TRUE;
}

/* ==========================================================================
 * Resolution
 * ========================================================================== */

/**
 * lrg_effect_stack_resolve_one:
 * @self: a #LrgEffectStack
 * @context: the combat context
 * @error: (optional): return location for a #GError
 *
 * Resolves the next effect on the stack (highest priority).
 * Emits effect-resolved or effect-failed signal as appropriate.
 *
 * Returns: %TRUE if an effect was resolved, %FALSE if stack
 *          was empty or error occurred
 *
 * Since: 1.0
 */
gboolean
lrg_effect_stack_resolve_one (LrgEffectStack  *self,
                              gpointer         context,
                              GError         **error)
{
    g_autoptr(LrgEffectStackEntry) entry = NULL;
    g_autoptr(GError) local_error = NULL;
    gboolean success;

    g_return_val_if_fail (LRG_IS_EFFECT_STACK (self), FALSE);

    entry = lrg_effect_stack_pop (self);
    if (entry == NULL)
        return FALSE;

    success = lrg_card_effect_registry_execute (self->registry,
                                                 entry->effect,
                                                 context,
                                                 entry->source,
                                                 entry->target,
                                                 &local_error);

    if (success)
    {
        g_signal_emit (self, signals[SIGNAL_EFFECT_RESOLVED], 0, entry->effect);
    }
    else
    {
        g_signal_emit (self, signals[SIGNAL_EFFECT_FAILED], 0,
                       entry->effect, local_error);

        if (error != NULL)
            *error = g_steal_pointer (&local_error);
    }

    return success;
}

/**
 * lrg_effect_stack_resolve_all:
 * @self: a #LrgEffectStack
 * @context: the combat context
 * @error: (optional): return location for a #GError
 *
 * Resolves all effects on the stack in priority order.
 * Stops on first error and returns FALSE.
 *
 * Returns: %TRUE if all effects resolved successfully,
 *          %FALSE on error
 *
 * Since: 1.0
 */
gboolean
lrg_effect_stack_resolve_all (LrgEffectStack  *self,
                              gpointer         context,
                              GError         **error)
{
    g_return_val_if_fail (LRG_IS_EFFECT_STACK (self), FALSE);

    while (!lrg_effect_stack_is_empty (self))
    {
        if (!lrg_effect_stack_resolve_one (self, context, error))
            return FALSE;
    }

    return TRUE;
}
