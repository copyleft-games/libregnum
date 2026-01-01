/* lrg-effect-stack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEffectStack - Effect resolution stack.
 *
 * The effect stack manages pending effects waiting to be resolved.
 * Effects are pushed onto the stack and resolved in priority order.
 * This enables proper effect ordering, interrupts, and triggered
 * effects to be inserted during resolution.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-card-effect.h"
#include "lrg-card-effect-registry.h"

G_BEGIN_DECLS

#define LRG_TYPE_EFFECT_STACK (lrg_effect_stack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEffectStack, lrg_effect_stack,
                      LRG, EFFECT_STACK, GObject)

/* ==========================================================================
 * Effect Stack Entry (boxed type)
 * ========================================================================== */

#define LRG_TYPE_EFFECT_STACK_ENTRY (lrg_effect_stack_entry_get_type ())

/**
 * LrgEffectStackEntry:
 *
 * An entry in the effect stack containing an effect and its
 * execution context (source and target combatants).
 *
 * Since: 1.0
 */
typedef struct _LrgEffectStackEntry LrgEffectStackEntry;

LRG_AVAILABLE_IN_ALL
GType lrg_effect_stack_entry_get_type (void) G_GNUC_CONST;

/**
 * lrg_effect_stack_entry_new:
 * @effect: the effect to queue
 * @source: (nullable): the source combatant
 * @target: (nullable): the target combatant
 *
 * Creates a new stack entry.
 *
 * Returns: (transfer full): a new #LrgEffectStackEntry
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEffectStackEntry * lrg_effect_stack_entry_new (LrgCardEffect *effect,
                                                  gpointer       source,
                                                  gpointer       target);

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
LRG_AVAILABLE_IN_ALL
LrgEffectStackEntry * lrg_effect_stack_entry_copy (LrgEffectStackEntry *entry);

/**
 * lrg_effect_stack_entry_free:
 * @entry: a #LrgEffectStackEntry
 *
 * Frees the entry.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_effect_stack_entry_free (LrgEffectStackEntry *entry);

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
LRG_AVAILABLE_IN_ALL
LrgCardEffect * lrg_effect_stack_entry_get_effect (LrgEffectStackEntry *entry);

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
LRG_AVAILABLE_IN_ALL
gpointer lrg_effect_stack_entry_get_source (LrgEffectStackEntry *entry);

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
LRG_AVAILABLE_IN_ALL
gpointer lrg_effect_stack_entry_get_target (LrgEffectStackEntry *entry);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEffectStackEntry, lrg_effect_stack_entry_free)

/* ==========================================================================
 * Effect Stack
 * ========================================================================== */

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
LRG_AVAILABLE_IN_ALL
LrgEffectStack * lrg_effect_stack_new (LrgCardEffectRegistry *registry);

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
LRG_AVAILABLE_IN_ALL
void lrg_effect_stack_push (LrgEffectStack      *self,
                            LrgEffectStackEntry *entry);

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
LRG_AVAILABLE_IN_ALL
void lrg_effect_stack_push_effect (LrgEffectStack *self,
                                   LrgCardEffect  *effect,
                                   gpointer        source,
                                   gpointer        target);

/**
 * lrg_effect_stack_peek:
 * @self: a #LrgEffectStack
 *
 * Peeks at the next effect to be resolved (highest priority).
 *
 * Returns: (transfer none) (nullable): the next entry, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEffectStackEntry * lrg_effect_stack_peek (LrgEffectStack *self);

/**
 * lrg_effect_stack_pop:
 * @self: a #LrgEffectStack
 *
 * Pops the next effect entry from the stack.
 *
 * Returns: (transfer full) (nullable): the entry, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEffectStackEntry * lrg_effect_stack_pop (LrgEffectStack *self);

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
LRG_AVAILABLE_IN_ALL
gboolean lrg_effect_stack_is_empty (LrgEffectStack *self);

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
LRG_AVAILABLE_IN_ALL
guint lrg_effect_stack_get_count (LrgEffectStack *self);

/**
 * lrg_effect_stack_clear:
 * @self: a #LrgEffectStack
 *
 * Clears all entries from the stack.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_effect_stack_clear (LrgEffectStack *self);

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
 *
 * Returns: %TRUE if an effect was resolved, %FALSE if stack
 *          was empty or error occurred
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_effect_stack_resolve_one (LrgEffectStack  *self,
                                       gpointer         context,
                                       GError         **error);

/**
 * lrg_effect_stack_resolve_all:
 * @self: a #LrgEffectStack
 * @context: the combat context
 * @error: (optional): return location for a #GError
 *
 * Resolves all effects on the stack in priority order.
 * Stops on first error.
 *
 * Returns: %TRUE if all effects resolved successfully,
 *          %FALSE on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_effect_stack_resolve_all (LrgEffectStack  *self,
                                       gpointer         context,
                                       GError         **error);

G_END_DECLS
