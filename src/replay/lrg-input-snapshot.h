/* lrg-input-snapshot.h - One tick of action input for one player
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../input/lrg-input-map.h"
#include "lrg-state-buffer.h"

G_BEGIN_DECLS

/**
 * LRG_INPUT_SNAPSHOT_MAX_ACTIONS:
 *
 * The number of actions a snapshot can hold (one bit each).
 *
 * Since: 0.4
 */
#define LRG_INPUT_SNAPSHOT_MAX_ACTIONS (64)

#define LRG_TYPE_INPUT_SNAPSHOT (lrg_input_snapshot_get_type ())

/**
 * LrgInputSnapshot:
 *
 * The action state of one player for one simulation tick: a held bit per
 * action plus the analog value of the actions whose value is not implied by
 * the bit (anything other than 1.0 when held and 0.0 when not).
 *
 * Snapshots are indexed by action, not by name: the action table (an ordered
 * list of action names) is fixed for a simulation, stored in replay headers
 * and agreed between rollback peers. "Pressed" and "released" are not stored;
 * they are derived from the previous tick's snapshot when one is applied, so
 * repeating a snapshot (rollback prediction) holds a button rather than
 * pressing it again.
 *
 * Since: 0.4
 */
typedef struct _LrgInputSnapshot LrgInputSnapshot;

LRG_AVAILABLE_IN_ALL
GType lrg_input_snapshot_get_type (void) G_GNUC_CONST;

/**
 * lrg_input_snapshot_new:
 *
 * Returns: (transfer full): a new snapshot with nothing held
 */
LRG_AVAILABLE_IN_ALL
LrgInputSnapshot *lrg_input_snapshot_new (void);

/**
 * lrg_input_snapshot_copy:
 * @self: a snapshot
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgInputSnapshot *lrg_input_snapshot_copy (const LrgInputSnapshot *self);

/**
 * lrg_input_snapshot_free:
 * @self: (nullable): a snapshot
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_free (LrgInputSnapshot *self);

/**
 * lrg_input_snapshot_assign:
 * @self: the destination snapshot
 * @other: the source snapshot
 *
 * Copies @other into @self without allocating.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_assign (LrgInputSnapshot       *self,
                                const LrgInputSnapshot *other);

/**
 * lrg_input_snapshot_clear:
 * @self: a snapshot
 *
 * Releases every action and zeroes every analog value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_clear (LrgInputSnapshot *self);

/**
 * lrg_input_snapshot_equal:
 * @a: a snapshot
 * @b: another snapshot
 *
 * Returns: %TRUE if both hold the same actions with bit-identical analog values
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_snapshot_equal (const LrgInputSnapshot *a,
                                   const LrgInputSnapshot *b);

/**
 * lrg_input_snapshot_get_down_mask:
 * @self: a snapshot
 *
 * Returns: the held bits, bit N for action N
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_input_snapshot_get_down_mask (const LrgInputSnapshot *self);

/**
 * lrg_input_snapshot_set_down_mask:
 * @self: a snapshot
 * @mask: the held bits, bit N for action N
 *
 * Sets every held bit at once (analog values are kept).
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_set_down_mask (LrgInputSnapshot *self,
                                       guint64           mask);

/**
 * lrg_input_snapshot_get_down:
 * @self: a snapshot
 * @action: action index, below %LRG_INPUT_SNAPSHOT_MAX_ACTIONS
 *
 * Returns: whether @action is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_snapshot_get_down (const LrgInputSnapshot *self,
                                      guint                   action);

/**
 * lrg_input_snapshot_set_down:
 * @self: a snapshot
 * @action: action index, below %LRG_INPUT_SNAPSHOT_MAX_ACTIONS
 * @down: whether the action is held
 *
 * Sets one held bit.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_set_down (LrgInputSnapshot *self,
                                  guint             action,
                                  gboolean          down);

/**
 * lrg_input_snapshot_get_axis:
 * @self: a snapshot
 * @action: action index, below %LRG_INPUT_SNAPSHOT_MAX_ACTIONS
 *
 * Returns: the signed analog value of @action (1.0 for a held digital
 *   action unless an explicit value was set)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_snapshot_get_axis (const LrgInputSnapshot *self,
                                    guint                   action);

/**
 * lrg_input_snapshot_set_axis:
 * @self: a snapshot
 * @action: action index, below %LRG_INPUT_SNAPSHOT_MAX_ACTIONS
 * @value: the signed analog value
 *
 * Sets the analog value of @action. A value equal to what the held bit
 * implies (1.0 when held, 0.0 when not) is not stored separately.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_set_axis (LrgInputSnapshot *self,
                                  guint             action,
                                  gfloat            value);

/**
 * lrg_input_snapshot_capture:
 * @map: the input map to read
 * @actions: (array zero-terminated=1): the action table
 *
 * Reads the live state of each action in @actions from @map: held is
 * lrg_input_action_is_down() or lrg_input_action_is_pressed() (a tap that
 * starts and ends between two polls still counts for one tick), the analog
 * value is lrg_input_action_get_axis(). Actions missing from @map read as
 * released. Injected overrides are read like live input, so clear them
 * first (lrg_input_map_clear_overrides()) when recording.
 *
 * Returns: (transfer full): the captured snapshot
 */
LRG_AVAILABLE_IN_ALL
LrgInputSnapshot *lrg_input_snapshot_capture (LrgInputMap        *map,
                                              const gchar *const *actions);

/**
 * lrg_input_snapshot_apply:
 * @self: the snapshot for this tick
 * @previous: (nullable): the snapshot of the previous tick, or %NULL
 *   (nothing held)
 * @map: the input map to drive
 * @actions: (array zero-terminated=1): the action table
 *
 * Injects @self into @map with lrg_input_action_set_override(): held and
 * analog values come from @self, pressed/released from the change against
 * @previous. Actions in @actions that @map lacks are skipped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_apply (const LrgInputSnapshot *self,
                               const LrgInputSnapshot *previous,
                               LrgInputMap            *map,
                               const gchar *const     *actions);

/**
 * lrg_input_snapshot_write:
 * @self: a snapshot
 * @buffer: the buffer to append to
 *
 * Appends the compact encoding of @self: the held mask, the mask of
 * explicit analog values, then those values.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_snapshot_write (const LrgInputSnapshot *self,
                               LrgStateBuffer         *buffer);

/**
 * lrg_input_snapshot_read:
 * @self: the snapshot to fill
 * @buffer: the buffer to read from
 *
 * Reads what lrg_input_snapshot_write() wrote.
 *
 * Returns: %FALSE if @buffer ran out
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_snapshot_read (LrgInputSnapshot *self,
                                  LrgStateBuffer   *buffer);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgInputSnapshot, lrg_input_snapshot_free)

G_END_DECLS
