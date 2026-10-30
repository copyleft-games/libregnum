/* lrg-replay.h - Recorded per-tick input with a versioned file format
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
#include "lrg-input-snapshot.h"
#include "lrg-simulation.h"

G_BEGIN_DECLS

/**
 * LRG_REPLAY_FORMAT_VERSION:
 *
 * The replay file format version this build writes and reads.
 *
 * Since: 0.4
 */
#define LRG_REPLAY_FORMAT_VERSION (1)

/**
 * LRG_REPLAY_ERROR:
 *
 * Error domain for replays, golden checks and rollback sessions.
 *
 * Since: 0.4
 */
#define LRG_REPLAY_ERROR (lrg_replay_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_replay_error_quark (void);

/**
 * LrgReplayError:
 * @LRG_REPLAY_ERROR_FAILED: generic failure
 * @LRG_REPLAY_ERROR_PARSE: the file is not a replay or is corrupt
 * @LRG_REPLAY_ERROR_VERSION: the file uses an unsupported format version
 * @LRG_REPLAY_ERROR_INCOMPATIBLE: the simulation does not match the replay
 *   (seed, fixed step, players or action table)
 * @LRG_REPLAY_ERROR_INITIAL_STATE: the simulation is not in the replay's
 *   initial state (tick or checksum)
 * @LRG_REPLAY_ERROR_DESYNC: a state checksum differs from the recorded one
 * @LRG_REPLAY_ERROR_GOLDEN_MISMATCH: checksums differ from a golden file
 * @LRG_REPLAY_ERROR_NOT_FOUND: a golden file does not exist
 *
 * Error codes in %LRG_REPLAY_ERROR.
 *
 * Since: 0.4
 */
typedef enum
{
    LRG_REPLAY_ERROR_FAILED,
    LRG_REPLAY_ERROR_PARSE,
    LRG_REPLAY_ERROR_VERSION,
    LRG_REPLAY_ERROR_INCOMPATIBLE,
    LRG_REPLAY_ERROR_INITIAL_STATE,
    LRG_REPLAY_ERROR_DESYNC,
    LRG_REPLAY_ERROR_GOLDEN_MISMATCH,
    LRG_REPLAY_ERROR_NOT_FOUND
} LrgReplayError;

LRG_AVAILABLE_IN_ALL
GType lrg_replay_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_REPLAY_ERROR (lrg_replay_error_get_type ())

#define LRG_TYPE_REPLAY (lrg_replay_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReplay, lrg_replay, LRG, REPLAY, GObject)

/**
 * lrg_replay_new:
 * @seed: simulation seed
 * @fixed_step: seconds per tick
 * @n_players: number of players
 * @actions: (array zero-terminated=1): the action table
 *
 * Creates an empty replay. Recordings normally come from
 * lrg_simulation_start_recording() instead.
 *
 * Returns: (transfer full): a new #LrgReplay
 */
LRG_AVAILABLE_IN_ALL
LrgReplay *lrg_replay_new (guint64             seed,
                           gdouble             fixed_step,
                           guint               n_players,
                           const gchar *const *actions);

/**
 * lrg_replay_new_for_simulation:
 * @simulation: an #LrgSimulation
 * @initial_state: (nullable): reference to the initial state
 *
 * Creates an empty replay whose header matches @simulation as it is now:
 * seed, fixed step, players, action table, start tick and state checksum.
 *
 * Returns: (transfer full): a new #LrgReplay
 */
LRG_AVAILABLE_IN_ALL
LrgReplay *lrg_replay_new_for_simulation (LrgSimulation *simulation,
                                          const gchar   *initial_state);

/**
 * lrg_replay_load_from_file:
 * @path: a replay file
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the replay, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgReplay *lrg_replay_load_from_file (const gchar  *path,
                                      GError      **error);

/**
 * lrg_replay_load_from_bytes:
 * @bytes: the contents of a replay file
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the replay, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgReplay *lrg_replay_load_from_bytes (GBytes  *bytes,
                                       GError **error);

/**
 * lrg_replay_to_bytes:
 * @self: an #LrgReplay
 *
 * Encodes the replay: a YAML header document, the `...` document end
 * marker, then the delta-encoded binary body.
 *
 * Returns: (transfer full): the file contents
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_replay_to_bytes (LrgReplay *self);

/**
 * lrg_replay_save_to_file:
 * @self: an #LrgReplay
 * @path: destination file
 * @error: (nullable): return location for an error
 *
 * Writes lrg_replay_to_bytes() to @path atomically.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_replay_save_to_file (LrgReplay    *self,
                                  const gchar  *path,
                                  GError      **error);

/**
 * lrg_replay_get_seed:
 * @self: an #LrgReplay
 *
 * Returns: the simulation seed
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_replay_get_seed (LrgReplay *self);

/**
 * lrg_replay_get_fixed_step:
 * @self: an #LrgReplay
 *
 * Returns: seconds per tick
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_replay_get_fixed_step (LrgReplay *self);

/**
 * lrg_replay_get_n_players:
 * @self: an #LrgReplay
 *
 * Returns: the number of players
 */
LRG_AVAILABLE_IN_ALL
guint lrg_replay_get_n_players (LrgReplay *self);

/**
 * lrg_replay_get_actions:
 * @self: an #LrgReplay
 *
 * Returns: (transfer none) (array zero-terminated=1): the action table
 */
LRG_AVAILABLE_IN_ALL
const gchar * const *lrg_replay_get_actions (LrgReplay *self);

/**
 * lrg_replay_get_start_tick:
 * @self: an #LrgReplay
 *
 * Returns: the simulation tick the recording started at
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_replay_get_start_tick (LrgReplay *self);

/**
 * lrg_replay_get_n_ticks:
 * @self: an #LrgReplay
 *
 * Returns: the number of recorded ticks
 */
LRG_AVAILABLE_IN_ALL
guint lrg_replay_get_n_ticks (LrgReplay *self);

/**
 * lrg_replay_get_initial_state:
 * @self: an #LrgReplay
 *
 * Returns: (transfer none) (nullable): the initial-state reference
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_replay_get_initial_state (LrgReplay *self);

/**
 * lrg_replay_set_initial_state:
 * @self: an #LrgReplay
 * @initial_state: (nullable): reference to the initial state
 *
 * Sets the initial-state reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_replay_set_initial_state (LrgReplay   *self,
                                   const gchar *initial_state);

/**
 * lrg_replay_get_initial_checksum:
 * @self: an #LrgReplay
 * @checksum: (out) (optional): return location for the checksum
 *
 * Returns: %TRUE if the replay stores the initial state checksum
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_replay_get_initial_checksum (LrgReplay *self,
                                          guint64   *checksum);

/**
 * lrg_replay_set_initial_checksum:
 * @self: an #LrgReplay
 * @checksum: checksum of the state at the start tick
 *
 * Sets the checksum playback verifies before the first tick.
 */
LRG_AVAILABLE_IN_ALL
void lrg_replay_set_initial_checksum (LrgReplay *self,
                                      guint64    checksum);

/**
 * lrg_replay_get_metadata:
 * @self: an #LrgReplay
 * @key: a metadata key
 *
 * Returns: (transfer none) (nullable): the value stored under @key
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_replay_get_metadata (LrgReplay   *self,
                                      const gchar *key);

/**
 * lrg_replay_set_metadata:
 * @self: an #LrgReplay
 * @key: a key (letters, digits, `-` and `_`)
 * @value: (nullable): the value, or %NULL to remove it
 *
 * Stores free-form text in the header `metadata` mapping (game name,
 * build id, player names...). Not used by playback.
 */
LRG_AVAILABLE_IN_ALL
void lrg_replay_set_metadata (LrgReplay   *self,
                              const gchar *key,
                              const gchar *value);

/**
 * lrg_replay_append_tick:
 * @self: an #LrgReplay
 * @inputs: (array length=n_inputs): one snapshot per player
 * @n_inputs: must equal the number of players
 * @has_checksum: whether @checksum is valid
 * @checksum: the state checksum after this tick
 *
 * Appends one tick.
 */
LRG_AVAILABLE_IN_ALL
void lrg_replay_append_tick (LrgReplay               *self,
                             LrgInputSnapshot *const *inputs,
                             guint                    n_inputs,
                             gboolean                 has_checksum,
                             guint64                  checksum);

/**
 * lrg_replay_get_input:
 * @self: an #LrgReplay
 * @index: tick index from the start of the recording
 * @player: player slot
 *
 * Returns: (transfer none) (nullable): the snapshot, or %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
const LrgInputSnapshot *lrg_replay_get_input (LrgReplay *self,
                                              guint      index,
                                              guint      player);

/**
 * lrg_replay_get_checksum:
 * @self: an #LrgReplay
 * @index: tick index from the start of the recording
 * @checksum: (out) (optional): return location for the checksum
 *
 * Returns: %TRUE if a checksum was recorded after that tick
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_replay_get_checksum (LrgReplay *self,
                                  guint      index,
                                  guint64   *checksum);

/**
 * lrg_replay_check_compatible:
 * @self: an #LrgReplay
 * @simulation: an #LrgSimulation
 * @error: (nullable): return location for an error
 *
 * Checks that @simulation can play @self back: same seed, fixed step,
 * player count and action table, at the start tick with the recorded
 * initial checksum.
 *
 * Returns: %TRUE if compatible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_replay_check_compatible (LrgReplay      *self,
                                      LrgSimulation  *simulation,
                                      GError        **error);

/**
 * lrg_replay_step:
 * @self: an #LrgReplay
 * @simulation: an #LrgSimulation positioned inside the replay
 * @error: (nullable): return location for an error
 *
 * Plays the tick at the simulation's current position (simulation tick
 * minus the start tick) and verifies the recorded checksum for it, if any.
 *
 * Returns: %TRUE on success, %FALSE at the end of the replay (no error) or
 *   on a desync (%LRG_REPLAY_ERROR_DESYNC)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_replay_step (LrgReplay      *self,
                          LrgSimulation  *simulation,
                          GError        **error);

/**
 * lrg_replay_play:
 * @self: an #LrgReplay
 * @simulation: an #LrgSimulation in the replay's initial state
 * @error: (nullable): return location for an error
 *
 * Runs the whole replay headless, as fast as the simulation steps: checks
 * compatibility, then steps every recorded tick, verifying recorded
 * checksums as it goes (the desync detector).
 *
 * Returns: %TRUE if every tick played and every checksum matched
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_replay_play (LrgReplay      *self,
                          LrgSimulation  *simulation,
                          GError        **error);

/**
 * lrg_replay_check_golden:
 * @self: an #LrgReplay
 * @simulation: an #LrgSimulation in the replay's initial state
 * @golden_path: a golden checksum file
 * @interval: ticks between compared checksums (0 compares only the final
 *   state)
 * @error: (nullable): return location for an error
 *
 * Golden-file regression check: plays @self headless and compares the
 * state checksum every @interval ticks and at the end with @golden_path.
 * When the environment variable `LRG_UPDATE_GOLDEN` is set to a non-empty
 * value other than `0`, the golden file is (re)written instead and the
 * check passes.
 *
 * Returns: %TRUE if the checksums match (or the file was updated);
 *   %FALSE with %LRG_REPLAY_ERROR_GOLDEN_MISMATCH naming the first
 *   differing tick, %LRG_REPLAY_ERROR_NOT_FOUND, or a playback error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_replay_check_golden (LrgReplay      *self,
                                  LrgSimulation  *simulation,
                                  const gchar    *golden_path,
                                  guint           interval,
                                  GError        **error);

G_END_DECLS
