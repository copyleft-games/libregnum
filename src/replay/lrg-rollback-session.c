/* lrg-rollback-session.c - GGPO-style rollback netcode session
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Frame bookkeeping, for every player P:
 *
 *   slots     ring of known inputs, by frame
 *   confirmed every frame <= confirmed is known (contiguous)
 *
 * and for the session:
 *
 *   frame       the next frame to simulate
 *   records     per simulated frame F: the simulation state before F and
 *               the inputs F was simulated with (real or predicted)
 *   rollback_to the earliest frame whose real input differs from the one
 *               it was simulated with
 *
 * Wire format (little-endian, one datagram per peer per frame):
 *
 *   u32 magic "LRBK", u8 version, u8 sender player, u16 reserved
 *   i64 sender frame       (for frames-ahead estimation)
 *   i64 ack                (last contiguous input of the receiver we have)
 *   i64 checksum frame     (-1: none), u64 checksum
 *   i64 first input frame, u8 count, u32 length + count snapshots
 *
 * Inputs are re-sent until acknowledged, so a lost datagram costs nothing
 * but latency, and duplicates or reordering are ignored by frame number.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_REPLAY

#include "lrg-rollback-session.h"
#include "lrg-replay.h"
#include "../lrg-log.h"

#include <gio/gio.h>

/* Ring sizes: well beyond the worst-case span between the oldest frame a
 * peer may still need and the newest one (delay + 2 * max rollback) */
#define INPUT_RING     (256)
#define CHECKSUM_RING  (64)

#define PACKET_MAGIC          (0x4b42524cu)   /* "LRBK" */
#define PACKET_VERSION        (1)
#define PACKET_MAX_INPUTS     (64)
#define PACKET_INPUT_BUDGET   (960)           /* bytes of snapshots per datagram */

#define NO_ROLLBACK           (G_MAXINT64)

typedef struct
{
    gint64            frame;     /* -1: empty */
    LrgInputSnapshot *input;
} InputSlot;

typedef struct
{
    gint64   frame;              /* -1: empty */
    guint64  checksum;
    gboolean compared;
} ChecksumSlot;

typedef struct
{
    InputSlot    slots[INPUT_RING];
    gint64       confirmed;      /* all inputs up to here are known */
    gint64       acked;          /* remote: our inputs it has, contiguous */
    gint64       reported;       /* remote: frame it said it was on */
    ChecksumSlot sums[CHECKSUM_RING];  /* remote: checksums it sent */
} Player;

typedef struct
{
    gint64            frame;     /* -1: empty */
    GBytes           *state;     /* simulation before this frame */
    LrgInputSnapshot *used[LRG_SIMULATION_MAX_PLAYERS];
} FrameRecord;

struct _LrgRollbackSession
{
    GObject               parent_instance;

    LrgSimulation        *simulation;
    LrgRollbackTransport *transport;
    guint                 local_player;
    guint                 n_players;

    guint                 input_delay;
    guint                 max_rollback;
    guint                 checksum_interval;
    gboolean              started;

    gint64                frame;
    gint64                rollback_to;
    Player               *players;   /* LRG_SIMULATION_MAX_PLAYERS, heap: */
    FrameRecord          *records;   /* INPUT_RING; too big for an instance */
    LrgInputSnapshot     *empty;

    /* Desync detection */
    ChecksumSlot          local_sums[CHECKSUM_RING];
    gint64                last_checksummed;
    gint64                latest_checksum_frame;
    guint64               latest_checksum;
    gint64                last_verified;
    gint64                desync_frame;

    /* Statistics */
    guint                 rollbacks;
    guint                 resimulated;
};

G_DEFINE_FINAL_TYPE (LrgRollbackSession, lrg_rollback_session, G_TYPE_OBJECT)

enum
{
    SIGNAL_DESYNC,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_rollback_session_finalize (GObject *object)
{
    LrgRollbackSession *self = LRG_ROLLBACK_SESSION (object);
    guint i;
    guint p;

    for (i = 0; i < INPUT_RING; i++)
    {
        g_clear_pointer (&self->records[i].state, g_bytes_unref);
        for (p = 0; p < LRG_SIMULATION_MAX_PLAYERS; p++)
        {
            g_clear_pointer (&self->records[i].used[p], lrg_input_snapshot_free);
            g_clear_pointer (&self->players[p].slots[i].input, lrg_input_snapshot_free);
        }
    }
    g_clear_pointer (&self->players, g_free);
    g_clear_pointer (&self->records, g_free);
    g_clear_pointer (&self->empty, lrg_input_snapshot_free);
    g_clear_object (&self->simulation);
    g_clear_object (&self->transport);

    G_OBJECT_CLASS (lrg_rollback_session_parent_class)->finalize (object);
}

static void
lrg_rollback_session_class_init (LrgRollbackSessionClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_rollback_session_finalize;

    /**
     * LrgRollbackSession::desync:
     * @self: the session
     * @frame: the frame whose checksums differ
     * @peer: the player whose checksum differs from the local one
     * @local: the local checksum
     * @remote: the peer's checksum
     *
     * Emitted once per frame and peer when a confirmed frame's state
     * checksum differs between this peer and @peer: the simulations have
     * diverged (a determinism-contract violation).
     *
     * Since: 0.4
     */
    signals[SIGNAL_DESYNC] =
        g_signal_new ("desync", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 4, G_TYPE_INT64, G_TYPE_UINT,
                      G_TYPE_UINT64, G_TYPE_UINT64);
}

static void
lrg_rollback_session_init (LrgRollbackSession *self)
{
    guint i;
    guint p;

    self->input_delay = 2;
    self->max_rollback = 8;
    self->checksum_interval = 10;
    self->rollback_to = NO_ROLLBACK;
    self->last_checksummed = 0;
    self->latest_checksum_frame = -1;
    self->last_verified = -1;
    self->desync_frame = -1;
    self->empty = lrg_input_snapshot_new ();
    self->players = g_new0 (Player, LRG_SIMULATION_MAX_PLAYERS);
    self->records = g_new0 (FrameRecord, INPUT_RING);

    for (i = 0; i < INPUT_RING; i++)
        self->records[i].frame = -1;
    for (i = 0; i < CHECKSUM_RING; i++)
        self->local_sums[i].frame = -1;
    for (p = 0; p < LRG_SIMULATION_MAX_PLAYERS; p++)
    {
        for (i = 0; i < INPUT_RING; i++)
            self->players[p].slots[i].frame = -1;
        for (i = 0; i < CHECKSUM_RING; i++)
            self->players[p].sums[i].frame = -1;
    }
}

LrgRollbackSession *
lrg_rollback_session_new (LrgSimulation        *simulation,
                          LrgRollbackTransport *transport,
                          guint                 local_player)
{
    LrgRollbackSession *self;

    g_return_val_if_fail (LRG_IS_SIMULATION (simulation), NULL);
    g_return_val_if_fail (LRG_IS_ROLLBACK_TRANSPORT (transport), NULL);
    g_return_val_if_fail (local_player < lrg_simulation_get_n_players (simulation), NULL);

    self = g_object_new (LRG_TYPE_ROLLBACK_SESSION, NULL);
    self->simulation = g_object_ref (simulation);
    self->transport = g_object_ref (transport);
    self->local_player = local_player;
    self->n_players = lrg_simulation_get_n_players (simulation);
    return self;
}

LrgSimulation *
lrg_rollback_session_get_simulation (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), NULL);
    return self->simulation;
}

void
lrg_rollback_session_set_input_delay (LrgRollbackSession *self,
                                      guint               frames)
{
    g_return_if_fail (LRG_IS_ROLLBACK_SESSION (self));
    g_return_if_fail (frames <= LRG_ROLLBACK_MAX_INPUT_DELAY);
    g_return_if_fail (!self->started);

    self->input_delay = frames;
}

guint
lrg_rollback_session_get_input_delay (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), 0);
    return self->input_delay;
}

void
lrg_rollback_session_set_max_rollback (LrgRollbackSession *self,
                                       guint               frames)
{
    g_return_if_fail (LRG_IS_ROLLBACK_SESSION (self));
    g_return_if_fail (frames >= 1 && frames <= LRG_ROLLBACK_MAX_FRAMES);

    self->max_rollback = frames;
}

guint
lrg_rollback_session_get_max_rollback (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), 0);
    return self->max_rollback;
}

void
lrg_rollback_session_set_checksum_interval (LrgRollbackSession *self,
                                            guint               frames)
{
    g_return_if_fail (LRG_IS_ROLLBACK_SESSION (self));
    g_return_if_fail (!self->started);

    self->checksum_interval = frames;
}

/* ==========================================================================
 * Input bookkeeping
 * ========================================================================== */

static const LrgInputSnapshot *
known_input (LrgRollbackSession *self,
             guint               player,
             gint64              frame)
{
    const InputSlot *slot;

    if (frame < 0)
        return NULL;
    slot = &self->players[player].slots[frame % INPUT_RING];
    return slot->frame == frame ? slot->input : NULL;
}

static void
store_input (LrgRollbackSession     *self,
             guint                   player,
             gint64                  frame,
             const LrgInputSnapshot *input)
{
    Player *state = &self->players[player];
    InputSlot *slot = &state->slots[frame % INPUT_RING];

    slot->frame = frame;
    if (slot->input == NULL)
        slot->input = lrg_input_snapshot_copy (input);
    else
        lrg_input_snapshot_assign (slot->input, input);

    /* Extend the contiguous run of known inputs */
    while (known_input (self, player, state->confirmed + 1) != NULL)
        state->confirmed++;
}

/*
 * input_for_frame:
 *
 * The real input when known, otherwise the prediction: the player's last
 * confirmed input repeated (held buttons stay held, nothing is re-pressed).
 */
static const LrgInputSnapshot *
input_for_frame (LrgRollbackSession *self,
                 guint               player,
                 gint64              frame)
{
    const LrgInputSnapshot *input;

    input = known_input (self, player, frame);
    if (input != NULL)
        return input;
    input = known_input (self, player, self->players[player].confirmed);
    return input != NULL ? input : self->empty;
}

/*
 * ensure_started:
 *
 * The first input-delay frames have no input on any peer: every peer
 * treats them as known and empty, so nobody waits for them.
 */
static void
ensure_started (LrgRollbackSession *self)
{
    guint p;
    gint64 f;

    if (self->started)
        return;
    self->started = TRUE;

    for (p = 0; p < self->n_players; p++)
    {
        self->players[p].confirmed = -1;
        for (f = 0; f < (gint64) self->input_delay; f++)
            store_input (self, p, f, self->empty);
        self->players[p].acked = (gint64) self->input_delay - 1;
        self->players[p].reported = 0;
    }
}

static gint64
min_confirmed (LrgRollbackSession *self)
{
    gint64 result = G_MAXINT64;
    guint p;

    for (p = 0; p < self->n_players; p++)
        result = MIN (result, self->players[p].confirmed);
    return result;
}

/* ==========================================================================
 * Simulation
 * ========================================================================== */

/*
 * simulate_frame:
 *
 * Saves the state before @frame, picks every player's input (real or
 * predicted), remembers it and steps the simulation.
 */
static void
simulate_frame (LrgRollbackSession *self,
                gint64              frame)
{
    FrameRecord *record = &self->records[frame % INPUT_RING];
    guint p;

    record->frame = frame;
    g_clear_pointer (&record->state, g_bytes_unref);
    record->state = lrg_simulation_save_bytes (self->simulation);

    for (p = 0; p < self->n_players; p++)
    {
        const LrgInputSnapshot *input = input_for_frame (self, p, frame);

        if (record->used[p] == NULL)
            record->used[p] = lrg_input_snapshot_copy (input);
        else
            lrg_input_snapshot_assign (record->used[p], input);
    }

    lrg_simulation_step (self->simulation, record->used, self->n_players);
}

/*
 * apply_rollback:
 *
 * Loads the state before the earliest mispredicted frame and simulates
 * forward to the present with the inputs now known.
 */
static gboolean
apply_rollback (LrgRollbackSession  *self,
                GError             **error)
{
    FrameRecord *record;
    gint64 target;
    gint64 f;

    if (self->rollback_to >= self->frame)
    {
        self->rollback_to = NO_ROLLBACK;
        return TRUE;
    }

    target = self->rollback_to;
    self->rollback_to = NO_ROLLBACK;
    record = &self->records[target % INPUT_RING];
    if (record->frame != target || record->state == NULL)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_FAILED,
                     "Cannot roll back to frame %" G_GINT64_FORMAT ": state no longer kept",
                     target);
        return FALSE;
    }

    if (!lrg_simulation_load_bytes (self->simulation, record->state, error))
        return FALSE;

    for (f = target; f < self->frame; f++)
    {
        simulate_frame (self, f);
        self->resimulated++;
    }
    self->rollbacks++;
    return TRUE;
}

/* ==========================================================================
 * Desync detection
 * ========================================================================== */

static void
compare_checksum (LrgRollbackSession *self,
                  guint               peer,
                  ChecksumSlot       *remote)
{
    const ChecksumSlot *local;

    if (remote->compared || remote->frame < 0)
        return;
    local = &self->local_sums[(remote->frame / MAX (self->checksum_interval, 1)) % CHECKSUM_RING];
    if (local->frame != remote->frame)
        return;

    remote->compared = TRUE;
    if (local->checksum == remote->checksum)
    {
        self->last_verified = MAX (self->last_verified, remote->frame);
        return;
    }

    if (self->desync_frame < 0 || remote->frame < self->desync_frame)
        self->desync_frame = remote->frame;
    lrg_log_debug ("Desync with player %u at frame %" G_GINT64_FORMAT, peer, remote->frame);
    g_signal_emit (self, signals[SIGNAL_DESYNC], 0, remote->frame, peer,
                   local->checksum, remote->checksum);
}

/*
 * update_checksums:
 *
 * Checksums every interval-th state that can no longer change: state
 * after F frames is final once every input of frames below F is known.
 */
static void
update_checksums (LrgRollbackSession *self)
{
    gint64 final_limit;
    gint64 next;
    guint p;

    if (self->checksum_interval == 0)
        return;

    final_limit = MIN (min_confirmed (self) + 1, self->frame);
    for (next = self->last_checksummed + self->checksum_interval;
         next <= final_limit;
         next += self->checksum_interval)
    {
        ChecksumSlot *slot;
        guint64 checksum;

        if (next == self->frame)
            checksum = lrg_simulation_checksum (self->simulation);
        else
        {
            const FrameRecord *record = &self->records[next % INPUT_RING];
            gconstpointer data;
            gsize size;

            if (record->frame != next || record->state == NULL)
                break;
            data = g_bytes_get_data (record->state, &size);
            checksum = lrg_state_hash_data (data, size, 0);
        }

        slot = &self->local_sums[(next / self->checksum_interval) % CHECKSUM_RING];
        slot->frame = next;
        slot->checksum = checksum;
        self->last_checksummed = next;
        self->latest_checksum_frame = next;
        self->latest_checksum = checksum;

        for (p = 0; p < self->n_players; p++)
        {
            if (p != self->local_player)
                compare_checksum (self, p,
                                  &self->players[p].sums[(next / self->checksum_interval) % CHECKSUM_RING]);
        }
    }
}

/* ==========================================================================
 * Network
 * ========================================================================== */

static void
send_inputs (LrgRollbackSession *self)
{
    g_autoptr(LrgStateBuffer) packet = NULL;
    g_autoptr(LrgStateBuffer) inputs = NULL;
    gint64 last_local;
    guint p;

    packet = lrg_state_buffer_new ();
    inputs = lrg_state_buffer_new ();
    last_local = self->players[self->local_player].confirmed;

    for (p = 0; p < self->n_players; p++)
    {
        g_autoptr(GBytes) bytes = NULL;
        const guint8 *data;
        gsize size;
        gint64 first;
        gint64 f;
        guint count = 0;

        if (p == self->local_player)
            continue;

        /* Everything this peer has not acknowledged, oldest first */
        first = self->players[p].acked + 1;
        lrg_state_buffer_reset (inputs);
        for (f = first; f <= last_local && count < PACKET_MAX_INPUTS; f++)
        {
            const LrgInputSnapshot *input = known_input (self, self->local_player, f);

            if (input == NULL || lrg_state_buffer_get_size (inputs) > PACKET_INPUT_BUDGET)
                break;
            lrg_input_snapshot_write (input, inputs);
            count++;
        }

        lrg_state_buffer_reset (packet);
        lrg_state_buffer_write_uint32 (packet, PACKET_MAGIC);
        lrg_state_buffer_write_uint8 (packet, PACKET_VERSION);
        lrg_state_buffer_write_uint8 (packet, (guint8) self->local_player);
        lrg_state_buffer_write_uint8 (packet, 0);
        lrg_state_buffer_write_uint8 (packet, 0);
        lrg_state_buffer_write_int64 (packet, self->frame);
        lrg_state_buffer_write_int64 (packet, self->players[p].confirmed);
        lrg_state_buffer_write_int64 (packet, self->latest_checksum_frame);
        lrg_state_buffer_write_uint64 (packet, self->latest_checksum);
        lrg_state_buffer_write_int64 (packet, first);
        lrg_state_buffer_write_uint8 (packet, (guint8) count);
        data = lrg_state_buffer_get_data (inputs, &size);
        lrg_state_buffer_write_data (packet, data, size);
        bytes = lrg_state_buffer_to_bytes (packet);
        lrg_rollback_transport_send (self->transport, p, bytes);
    }
}

static void
handle_packet (LrgRollbackSession *self,
               guint               peer,
               GBytes             *bytes)
{
    g_autoptr(LrgStateBuffer) packet = NULL;
    g_autoptr(LrgStateBuffer) inputs = NULL;
    g_autoptr(GBytes) block = NULL;
    g_autoptr(LrgInputSnapshot) input = NULL;
    Player *state;
    gint64 sender_frame;
    gint64 ack;
    gint64 checksum_frame;
    guint64 checksum;
    gint64 first;
    guint count;
    guint i;

    if (peer >= self->n_players || peer == self->local_player)
        return;

    /* Header */
    packet = lrg_state_buffer_new_from_bytes (bytes);
    if (lrg_state_buffer_read_uint32 (packet) != PACKET_MAGIC ||
        lrg_state_buffer_read_uint8 (packet) != PACKET_VERSION ||
        lrg_state_buffer_read_uint8 (packet) != peer)
        return;
    lrg_state_buffer_read_uint8 (packet);
    lrg_state_buffer_read_uint8 (packet);
    sender_frame = lrg_state_buffer_read_int64 (packet);
    ack = lrg_state_buffer_read_int64 (packet);
    checksum_frame = lrg_state_buffer_read_int64 (packet);
    checksum = lrg_state_buffer_read_uint64 (packet);
    first = lrg_state_buffer_read_int64 (packet);
    count = lrg_state_buffer_read_uint8 (packet);
    block = lrg_state_buffer_read_data (packet);
    if (!lrg_state_buffer_check (packet, NULL) || block == NULL || first < 0)
        return;
    inputs = lrg_state_buffer_new_from_bytes (block);

    state = &self->players[peer];
    state->reported = MAX (state->reported, sender_frame);
    state->acked = MAX (state->acked, ack);

    /* The peer's latest confirmed checksum */
    if (checksum_frame > 0 && self->checksum_interval > 0 &&
        checksum_frame % self->checksum_interval == 0)
    {
        ChecksumSlot *slot = &state->sums[(checksum_frame / self->checksum_interval) % CHECKSUM_RING];

        if (slot->frame != checksum_frame)
        {
            slot->frame = checksum_frame;
            slot->checksum = checksum;
            slot->compared = FALSE;
            compare_checksum (self, peer, slot);
        }
    }

    /* Inputs: keep new ones, note the earliest misprediction */
    input = lrg_input_snapshot_new ();
    for (i = 0; i < count; i++)
    {
        gint64 frame = first + i;
        const FrameRecord *record;

        if (!lrg_input_snapshot_read (input, inputs))
            return;
        if (frame <= state->confirmed || known_input (self, peer, frame) != NULL ||
            frame >= state->confirmed + INPUT_RING / 2)
            continue;

        store_input (self, peer, frame, input);

        record = &self->records[frame % INPUT_RING];
        if (frame < self->frame && record->frame == frame &&
            !lrg_input_snapshot_equal (record->used[peer], input))
            self->rollback_to = MIN (self->rollback_to, frame);
    }
}

static void
receive_packets (LrgRollbackSession *self)
{
    for (;;)
    {
        g_autoptr(GBytes) bytes = NULL;
        guint peer = 0;

        bytes = lrg_rollback_transport_receive (self->transport, &peer);
        if (bytes == NULL)
            break;
        handle_packet (self, peer, bytes);
    }
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

gboolean
lrg_rollback_session_poll (LrgRollbackSession  *self,
                           GError             **error)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    ensure_started (self);
    receive_packets (self);
    if (!apply_rollback (self, error))
        return FALSE;
    update_checksums (self);
    send_inputs (self);
    return TRUE;
}

gboolean
lrg_rollback_session_advance_frame (LrgRollbackSession      *self,
                                    const LrgInputSnapshot  *local_input,
                                    GError                 **error)
{
    guint p;

    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), FALSE);
    g_return_val_if_fail (local_input != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* 1. Network in, corrections applied */
    ensure_started (self);
    receive_packets (self);
    if (!apply_rollback (self, error))
        return FALSE;

    /* 2. Too far ahead of a peer's confirmed input: wait for it */
    for (p = 0; p < self->n_players; p++)
    {
        if (p != self->local_player &&
            self->frame - self->players[p].confirmed > (gint64) self->max_rollback)
        {
            update_checksums (self);
            send_inputs (self);
            return FALSE;
        }
    }

    /* 3. Schedule the local input input-delay frames ahead, simulate */
    store_input (self, self->local_player, self->frame + self->input_delay, local_input);
    simulate_frame (self, self->frame);
    self->frame++;

    /* 4. Checksums of newly final frames, network out */
    update_checksums (self);
    send_inputs (self);
    return TRUE;
}

gint64
lrg_rollback_session_get_current_frame (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), 0);
    return self->frame;
}

gint64
lrg_rollback_session_get_confirmed_frame (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), -1);

    if (!self->started)
        return -1;
    return MIN (min_confirmed (self), self->frame - 1);
}

gint64
lrg_rollback_session_get_frames_ahead (LrgRollbackSession *self)
{
    gint64 oldest = G_MAXINT64;
    guint p;

    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), 0);

    for (p = 0; p < self->n_players; p++)
    {
        if (p != self->local_player)
            oldest = MIN (oldest, self->players[p].reported);
    }
    return oldest == G_MAXINT64 ? 0 : self->frame - oldest;
}

guint
lrg_rollback_session_get_rollback_count (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), 0);
    return self->rollbacks;
}

guint
lrg_rollback_session_get_resimulated_frames (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), 0);
    return self->resimulated;
}

gboolean
lrg_rollback_session_lookup_checksum (LrgRollbackSession *self,
                                      gint64              frame,
                                      guint64            *checksum)
{
    const ChecksumSlot *slot;

    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), FALSE);

    if (self->checksum_interval == 0 || frame <= 0 || frame % self->checksum_interval != 0)
        return FALSE;
    slot = &self->local_sums[(frame / self->checksum_interval) % CHECKSUM_RING];
    if (slot->frame != frame)
        return FALSE;
    if (checksum != NULL)
        *checksum = slot->checksum;
    return TRUE;
}

gint64
lrg_rollback_session_get_last_verified_frame (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), -1);
    return self->last_verified;
}

gint64
lrg_rollback_session_get_desync_frame (LrgRollbackSession *self)
{
    g_return_val_if_fail (LRG_IS_ROLLBACK_SESSION (self), -1);
    return self->desync_frame;
}
