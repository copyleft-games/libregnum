/* lrg-aura.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAura - A timed buff or debuff instance (boxed).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_AURA_MAX_SECONDS:
 *
 * Longest aura duration and tick period in seconds (one day).
 */
#define LRG_AURA_MAX_SECONDS (86400.0)

/**
 * LRG_AURA_MAX_STACKS:
 *
 * Largest accepted @max_stacks of an #LrgAura.
 */
#define LRG_AURA_MAX_STACKS (999)

#define LRG_TYPE_AURA (lrg_aura_get_type ())

/**
 * LrgAura:
 * @id: aura identifier (1-128 bytes of UTF-8)
 * @source: caller-defined source entity id, 0 for none
 * @kind: buff or debuff
 * @dispel: (nullable): dispel type such as "magic" or "poison"; %NULL means
 *   the aura cannot be dispelled
 * @stat: (nullable): stat or effect key the aura modifies
 * @magnitude: effect per stack
 * @duration: total seconds, 0 for permanent until removed
 * @remaining: seconds left (0 for permanent auras)
 * @period: seconds between periodic ticks, 0 for none
 * @tick_left: seconds until the next periodic tick
 * @stacks: current stacks, 1 to @max_stacks
 * @max_stacks: stack limit, 1 to %LRG_AURA_MAX_STACKS
 *
 * An aura instance. Identity inside an #LrgAuraSet is (@id, @source).
 */
struct _LrgAura
{
    gchar       *id;
    guint        source;
    LrgAuraKind  kind;
    gchar       *dispel;
    gchar       *stat;
    gdouble      magnitude;
    gdouble      duration;
    gdouble      remaining;
    gdouble      period;
    gdouble      tick_left;
    guint        stacks;
    guint        max_stacks;
};

LRG_AVAILABLE_IN_ALL
GType lrg_aura_get_type (void) G_GNUC_CONST;

/**
 * lrg_aura_new:
 * @id: aura identifier
 * @kind: buff or debuff
 * @duration: total seconds, 0 for permanent
 *
 * Creates an aura with @remaining equal to @duration, one stack, a stack
 * limit of one, source 0 and no periodic tick.
 *
 * Returns: (transfer full): a new #LrgAura
 */
LRG_AVAILABLE_IN_ALL
LrgAura *lrg_aura_new (const gchar *id,
                       LrgAuraKind  kind,
                       gdouble      duration);

/**
 * lrg_aura_copy:
 * @self: an #LrgAura
 *
 * Returns: (transfer full): a deep copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgAura *lrg_aura_copy (const LrgAura *self);

/**
 * lrg_aura_free:
 * @self: (nullable): an #LrgAura
 *
 * Frees an aura and its strings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_aura_free (LrgAura *self);

/**
 * lrg_aura_get_total:
 * @self: an #LrgAura
 *
 * Returns: @magnitude multiplied by @stacks
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_aura_get_total (const LrgAura *self);

/**
 * lrg_aura_is_valid:
 * @self: an #LrgAura
 *
 * Checks every field against the limits that #LrgAuraSet enforces: a valid
 * id, optional dispel/stat keys of at most 128 bytes, a known @kind, a finite
 * @magnitude, @duration and @period in 0..%LRG_AURA_MAX_SECONDS, @remaining
 * in 0..@duration (0 when permanent), @tick_left in 0..@period (0 without a
 * period), and 1 <= @stacks <= @max_stacks <= %LRG_AURA_MAX_STACKS.
 *
 * Returns: %TRUE when the aura is well formed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_aura_is_valid (const LrgAura *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAura, lrg_aura_free)

G_END_DECLS
