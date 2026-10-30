/* lrg-simulation.h - Deterministic fixed-step simulation container
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
#include "../core/lrg-random-stream.h"
#include "../input/lrg-input-map.h"
#include "lrg-state-buffer.h"
#include "lrg-input-snapshot.h"

G_BEGIN_DECLS

typedef struct _LrgReplay LrgReplay;

/**
 * LRG_SIMULATION_MAX_PLAYERS:
 *
 * The most players (input slots) one simulation takes.
 *
 * Since: 0.4
 */
#define LRG_SIMULATION_MAX_PLAYERS (8)

#define LRG_TYPE_SIMULATION (lrg_simulation_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSimulation, lrg_simulation, LRG, SIMULATION, GObject)

/**
 * lrg_simulation_new:
 * @seed: seed of the simulation random stream
 *
 * Creates a deterministic simulation: a tick counter advanced by
 * lrg_simulation_step(), a seeded random stream, a fixed action table and
 * per-player input slots, and the ordered list of #LrgSimStateful objects
 * whose state makes up the simulation (for checksums, rollback and replays).
 * The game advances its own state from the #LrgSimulation::tick signal.
 *
 * Returns: (transfer full): a new #LrgSimulation
 *
 * Since: 0.4
 */
LRG_AVAILABLE_IN_ALL
LrgSimulation *lrg_simulation_new (guint64 seed);

/**
 * lrg_simulation_get_seed:
 * @self: an #LrgSimulation
 *
 * Returns: the seed the simulation was created with
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_simulation_get_seed (LrgSimulation *self);

/**
 * lrg_simulation_get_tick:
 * @self: an #LrgSimulation
 *
 * Returns: the number of ticks simulated so far (0 before the first step;
 *   during #LrgSimulation::tick it is the index of the tick being simulated)
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_simulation_get_tick (LrgSimulation *self);

/**
 * lrg_simulation_get_fixed_step:
 * @self: an #LrgSimulation
 *
 * Returns: seconds of simulated time per tick
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_simulation_get_fixed_step (LrgSimulation *self);

/**
 * lrg_simulation_set_fixed_step:
 * @self: an #LrgSimulation
 * @step: seconds per tick, positive
 *
 * Sets the simulated time per tick (default 1/60 s). Part of the
 * determinism contract: a replay only plays back at the step it was
 * recorded with.
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_set_fixed_step (LrgSimulation *self,
                                    gdouble        step);

/**
 * lrg_simulation_get_n_players:
 * @self: an #LrgSimulation
 *
 * Returns: the number of input slots
 */
LRG_AVAILABLE_IN_ALL
guint lrg_simulation_get_n_players (LrgSimulation *self);

/**
 * lrg_simulation_set_n_players:
 * @self: an #LrgSimulation
 * @n_players: 1 to %LRG_SIMULATION_MAX_PLAYERS
 *
 * Sets the number of input slots (default 1). Clears the stored inputs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_set_n_players (LrgSimulation *self,
                                   guint          n_players);

/**
 * lrg_simulation_get_random:
 * @self: an #LrgSimulation
 *
 * The simulation random stream, seeded from #LrgSimulation:seed. Simulation
 * code draws all randomness from it (never g_random_*() or rand()); it is
 * part of the saved state, so a rollback rewinds it too.
 *
 * Returns: (transfer none): the random stream
 */
LRG_AVAILABLE_IN_ALL
LrgRandomStream *lrg_simulation_get_random (LrgSimulation *self);

/**
 * lrg_simulation_set_actions:
 * @self: an #LrgSimulation
 * @actions: (array zero-terminated=1): action names, at most
 *   %LRG_INPUT_SNAPSHOT_MAX_ACTIONS
 *
 * Sets the action table: snapshot bit N is the action @actions[N].
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_set_actions (LrgSimulation      *self,
                                 const gchar *const *actions);

/**
 * lrg_simulation_set_actions_from_map:
 * @self: an #LrgSimulation
 * @map: an input map
 *
 * Sets the action table to the actions of @map sorted by name (a stable
 * order that does not depend on hash-table layout).
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_set_actions_from_map (LrgSimulation *self,
                                          LrgInputMap   *map);

/**
 * lrg_simulation_get_actions:
 * @self: an #LrgSimulation
 *
 * Returns: (transfer none) (array zero-terminated=1): the action table
 */
LRG_AVAILABLE_IN_ALL
const gchar * const *lrg_simulation_get_actions (LrgSimulation *self);

/**
 * lrg_simulation_get_action_index:
 * @self: an #LrgSimulation
 * @action: an action name
 *
 * Returns: the bit index of @action, or -1 if it is not in the table
 */
LRG_AVAILABLE_IN_ALL
gint lrg_simulation_get_action_index (LrgSimulation *self,
                                      const gchar   *action);

/**
 * lrg_simulation_set_player_input_map:
 * @self: an #LrgSimulation
 * @player: player slot
 * @map: (nullable): the input map that receives this player's input
 *
 * When set, each step injects the player's snapshot into @map (see
 * lrg_input_snapshot_apply()), so game code reads it through the normal
 * lrg_input_map_is_down()/is_pressed() calls.
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_set_player_input_map (LrgSimulation *self,
                                          guint          player,
                                          LrgInputMap   *map);

/**
 * lrg_simulation_release_inputs:
 * @self: an #LrgSimulation
 *
 * Clears the injected state from every player input map, returning them to
 * live input.
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_release_inputs (LrgSimulation *self);

/**
 * lrg_simulation_add_object:
 * @self: an #LrgSimulation
 * @object: an #LrgSimStateful, or an #LrgSaveable (serialized through an
 *   #LrgSaveContext; slower, but works for existing saveable objects)
 *
 * Adds @object to the simulation state. Registration order is
 * serialization order, so every peer and every replay run must register
 * the same objects in the same order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_add_object (LrgSimulation *self,
                                GObject       *object);

/**
 * lrg_simulation_remove_object:
 * @self: an #LrgSimulation
 * @object: a registered object
 *
 * Returns: %TRUE if @object was registered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_simulation_remove_object (LrgSimulation *self,
                                       GObject       *object);

/**
 * lrg_simulation_step:
 * @self: an #LrgSimulation
 * @inputs: (array length=n_inputs) (nullable): one snapshot per player;
 *   a missing or %NULL entry repeats that player's previous snapshot
 * @n_inputs: length of @inputs
 *
 * Simulates one tick: stores and injects the inputs, emits
 * #LrgSimulation::tick, advances the tick counter, then records the tick
 * into an active recording and computes the checksum when the checksum
 * interval says so. Injected inputs stay on the player maps until the next
 * step or lrg_simulation_release_inputs().
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_step (LrgSimulation           *self,
                          LrgInputSnapshot *const *inputs,
                          guint                    n_inputs);

/**
 * lrg_simulation_get_input:
 * @self: an #LrgSimulation
 * @player: player slot
 *
 * Returns: (transfer none): the snapshot of @player for the current (or
 *   last simulated) tick
 */
LRG_AVAILABLE_IN_ALL
const LrgInputSnapshot *lrg_simulation_get_input (LrgSimulation *self,
                                                  guint          player);

/**
 * lrg_simulation_get_previous_input:
 * @self: an #LrgSimulation
 * @player: player slot
 *
 * Returns: (transfer none): the snapshot of @player for the tick before
 *   the current one
 */
LRG_AVAILABLE_IN_ALL
const LrgInputSnapshot *lrg_simulation_get_previous_input (LrgSimulation *self,
                                                           guint          player);

/**
 * lrg_simulation_is_down:
 * @self: an #LrgSimulation
 * @player: player slot
 * @action: action name
 *
 * Returns: whether @action is held by @player this tick
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_simulation_is_down (LrgSimulation *self,
                                 guint          player,
                                 const gchar   *action);

/**
 * lrg_simulation_is_pressed:
 * @self: an #LrgSimulation
 * @player: player slot
 * @action: action name
 *
 * Returns: whether @action went down for @player this tick
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_simulation_is_pressed (LrgSimulation *self,
                                    guint          player,
                                    const gchar   *action);

/**
 * lrg_simulation_get_axis:
 * @self: an #LrgSimulation
 * @player: player slot
 * @action: action name
 *
 * Returns: the signed analog value of @action for @player this tick
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_simulation_get_axis (LrgSimulation *self,
                                guint          player,
                                const gchar   *action);

/**
 * lrg_simulation_save_state:
 * @self: an #LrgSimulation
 * @buffer: the buffer to append to
 *
 * Serializes the complete simulation: tick, random stream, the last input
 * of each player and every registered object in registration order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_save_state (LrgSimulation  *self,
                                LrgStateBuffer *buffer);

/**
 * lrg_simulation_save_bytes:
 * @self: an #LrgSimulation
 *
 * Returns: (transfer full): the serialized simulation
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_simulation_save_bytes (LrgSimulation *self);

/**
 * lrg_simulation_load_state:
 * @self: an #LrgSimulation
 * @buffer: a buffer written by lrg_simulation_save_state()
 * @error: (nullable): return location for an error
 *
 * Restores a saved simulation. The same objects must be registered in the
 * same order as when it was saved.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_simulation_load_state (LrgSimulation   *self,
                                    LrgStateBuffer  *buffer,
                                    GError         **error);

/**
 * lrg_simulation_load_bytes:
 * @self: an #LrgSimulation
 * @bytes: bytes from lrg_simulation_save_bytes()
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_simulation_load_bytes (LrgSimulation  *self,
                                    GBytes         *bytes,
                                    GError        **error);

/**
 * lrg_simulation_checksum:
 * @self: an #LrgSimulation
 *
 * Returns: 64-bit FNV-1a of the serialized simulation
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_simulation_checksum (LrgSimulation *self);

/**
 * lrg_simulation_get_checksum_interval:
 * @self: an #LrgSimulation
 *
 * Returns: ticks between recorded checksums (0: off)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_simulation_get_checksum_interval (LrgSimulation *self);

/**
 * lrg_simulation_set_checksum_interval:
 * @self: an #LrgSimulation
 * @interval: ticks between checksums, 0 to turn the desync detector off
 *
 * Turns on the desync detector: after every step whose tick count is a
 * multiple of @interval, the state checksum is computed and kept in a
 * history ring (see lrg_simulation_lookup_checksum()) and written into an
 * active recording.
 */
LRG_AVAILABLE_IN_ALL
void lrg_simulation_set_checksum_interval (LrgSimulation *self,
                                           guint          interval);

/**
 * lrg_simulation_lookup_checksum:
 * @self: an #LrgSimulation
 * @tick: a tick count
 * @checksum: (out) (optional): return location for the checksum
 *
 * Looks up the checksum the detector computed after @tick ticks.
 *
 * Returns: %TRUE if one is still in the history
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_simulation_lookup_checksum (LrgSimulation *self,
                                         guint64        tick,
                                         guint64       *checksum);

/**
 * lrg_simulation_verify_checksum:
 * @self: an #LrgSimulation
 * @tick: a tick count
 * @expected: the checksum a reference (a recording, a peer) has for @tick
 *
 * Compares @expected with the local checksum for @tick and emits
 * #LrgSimulation::desync on a mismatch. The first mismatching tick is kept
 * in lrg_simulation_get_first_desync_tick().
 *
 * Returns: %FALSE only on a mismatch (an unknown tick is not a mismatch)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_simulation_verify_checksum (LrgSimulation *self,
                                         guint64        tick,
                                         guint64        expected);

/**
 * lrg_simulation_get_first_desync_tick:
 * @self: an #LrgSimulation
 *
 * Returns: the first tick a checksum mismatch was detected at, or -1
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_simulation_get_first_desync_tick (LrgSimulation *self);

/**
 * lrg_simulation_start_recording:
 * @self: an #LrgSimulation
 * @initial_state: (nullable): reference to the initial state (a level path,
 *   a save file, a scenario name) stored in the replay header
 *
 * Starts recording every following step into a new replay. The header
 * takes the seed, fixed step, players, action table, the current tick and
 * the checksum of the current state.
 *
 * Returns: (transfer none): the replay being recorded
 */
LRG_AVAILABLE_IN_ALL
LrgReplay *lrg_simulation_start_recording (LrgSimulation *self,
                                           const gchar   *initial_state);

/**
 * lrg_simulation_get_recording:
 * @self: an #LrgSimulation
 *
 * Returns: (transfer none) (nullable): the replay being recorded
 */
LRG_AVAILABLE_IN_ALL
LrgReplay *lrg_simulation_get_recording (LrgSimulation *self);

/**
 * lrg_simulation_stop_recording:
 * @self: an #LrgSimulation
 *
 * Returns: (transfer full) (nullable): the finished replay
 */
LRG_AVAILABLE_IN_ALL
LrgReplay *lrg_simulation_stop_recording (LrgSimulation *self);

G_END_DECLS
