/* lrg-simulation.c - Deterministic fixed-step simulation container
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_REPLAY

#include "lrg-simulation.h"
#include "lrg-sim-stateful.h"
#include "lrg-replay.h"
#include "../input/lrg-input-action.h"
#include "../save/lrg-saveable.h"
#include "../save/lrg-save-context.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <string.h>

/* "LRGS": tag at the start of a serialized simulation */
#define LRG_SIM_STATE_MAGIC   (0x5347524cu)
#define LRG_SIM_STATE_VERSION (1u)

/* Checksums kept for lrg_simulation_lookup_checksum() */
#define LRG_SIM_CHECKSUM_HISTORY (1024)

typedef struct
{
    guint64  tick;
    guint64  checksum;
    gboolean valid;
} ChecksumSlot;

struct _LrgSimulation
{
    GObject           parent_instance;

    guint64           seed;
    guint64           tick;
    gdouble           fixed_step;
    guint             n_players;
    gboolean          stepping;

    LrgRandomStream  *random;
    GStrv             actions;
    GPtrArray        *objects;                 /* GObject, registration order */

    LrgInputMap      *maps[LRG_SIMULATION_MAX_PLAYERS];
    LrgInputSnapshot *current[LRG_SIMULATION_MAX_PLAYERS];
    LrgInputSnapshot *previous[LRG_SIMULATION_MAX_PLAYERS];

    /* Desync detector */
    guint             checksum_interval;
    ChecksumSlot     *history;                 /* LRG_SIM_CHECKSUM_HISTORY */
    gint64            first_desync_tick;

    LrgReplay        *recording;
    LrgStateBuffer   *scratch;                 /* reused for checksums */
};

G_DEFINE_FINAL_TYPE (LrgSimulation, lrg_simulation, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SEED,
    PROP_TICK,
    PROP_FIXED_STEP,
    PROP_N_PLAYERS,
    PROP_CHECKSUM_INTERVAL,
    N_PROPS
};

enum
{
    SIGNAL_TICK,
    SIGNAL_DESYNC,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void
lrg_simulation_finalize (GObject *object)
{
    LrgSimulation *self = LRG_SIMULATION (object);
    guint i;

    for (i = 0; i < LRG_SIMULATION_MAX_PLAYERS; i++)
    {
        g_clear_object (&self->maps[i]);
        g_clear_pointer (&self->current[i], lrg_input_snapshot_free);
        g_clear_pointer (&self->previous[i], lrg_input_snapshot_free);
    }
    g_clear_object (&self->random);
    g_clear_pointer (&self->actions, g_strfreev);
    g_clear_pointer (&self->objects, g_ptr_array_unref);
    g_clear_object (&self->recording);
    g_clear_object (&self->scratch);
    g_clear_pointer (&self->history, g_free);

    G_OBJECT_CLASS (lrg_simulation_parent_class)->finalize (object);
}

static void
lrg_simulation_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgSimulation *self = LRG_SIMULATION (object);

    switch (prop_id)
    {
    case PROP_SEED:
        g_value_set_uint64 (value, self->seed);
        break;
    case PROP_TICK:
        g_value_set_uint64 (value, self->tick);
        break;
    case PROP_FIXED_STEP:
        g_value_set_double (value, self->fixed_step);
        break;
    case PROP_N_PLAYERS:
        g_value_set_uint (value, self->n_players);
        break;
    case PROP_CHECKSUM_INTERVAL:
        g_value_set_uint (value, self->checksum_interval);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_simulation_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgSimulation *self = LRG_SIMULATION (object);

    switch (prop_id)
    {
    case PROP_SEED:
        /* Construct-only: the random stream follows the seed */
        self->seed = g_value_get_uint64 (value);
        g_clear_object (&self->random);
        self->random = lrg_random_stream_new (self->seed, 0);
        break;
    case PROP_FIXED_STEP:
        lrg_simulation_set_fixed_step (self, g_value_get_double (value));
        break;
    case PROP_N_PLAYERS:
        lrg_simulation_set_n_players (self, g_value_get_uint (value));
        break;
    case PROP_CHECKSUM_INTERVAL:
        lrg_simulation_set_checksum_interval (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_simulation_class_init (LrgSimulationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_simulation_finalize;
    object_class->get_property = lrg_simulation_get_property;
    object_class->set_property = lrg_simulation_set_property;

    /**
     * LrgSimulation:seed:
     *
     * Seed of the simulation random stream.
     *
     * Since: 0.4
     */
    properties[PROP_SEED] =
        g_param_spec_uint64 ("seed", NULL, NULL,
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgSimulation:tick:
     *
     * Number of ticks simulated.
     *
     * Since: 0.4
     */
    properties[PROP_TICK] =
        g_param_spec_uint64 ("tick", NULL, NULL,
                             0, G_MAXUINT64, 0,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSimulation:fixed-step:
     *
     * Simulated seconds per tick.
     *
     * Since: 0.4
     */
    properties[PROP_FIXED_STEP] =
        g_param_spec_double ("fixed-step", NULL, NULL,
                             1e-6, 10.0, 1.0 / 60.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgSimulation:n-players:
     *
     * Number of player input slots.
     *
     * Since: 0.4
     */
    properties[PROP_N_PLAYERS] =
        g_param_spec_uint ("n-players", NULL, NULL,
                           1, LRG_SIMULATION_MAX_PLAYERS, 1,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgSimulation:checksum-interval:
     *
     * Ticks between desync-detector checksums; 0 turns it off.
     *
     * Since: 0.4
     */
    properties[PROP_CHECKSUM_INTERVAL] =
        g_param_spec_uint ("checksum-interval", NULL, NULL,
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgSimulation::tick:
     * @self: the simulation
     * @tick: index of the tick being simulated
     * @step: simulated seconds in the tick
     *
     * Emitted once per lrg_simulation_step(), after the inputs are stored
     * and injected. Simulation code advances the game state here.
     *
     * Since: 0.4
     */
    signals[SIGNAL_TICK] =
        g_signal_new ("tick", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_UINT64, G_TYPE_DOUBLE);

    /**
     * LrgSimulation::desync:
     * @self: the simulation
     * @tick: the tick count the checksums are for
     * @local: the local checksum
     * @expected: the reference checksum
     *
     * Emitted by lrg_simulation_verify_checksum() on a mismatch.
     *
     * Since: 0.4
     */
    signals[SIGNAL_DESYNC] =
        g_signal_new ("desync", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 3, G_TYPE_UINT64, G_TYPE_UINT64, G_TYPE_UINT64);
}

static void
lrg_simulation_init (LrgSimulation *self)
{
    guint i;

    self->fixed_step = 1.0 / 60.0;
    self->n_players = 1;
    self->actions = g_new0 (gchar *, 1);
    self->objects = g_ptr_array_new_with_free_func (g_object_unref);
    self->first_desync_tick = -1;
    self->scratch = lrg_state_buffer_new ();
    self->history = g_new0 (ChecksumSlot, LRG_SIM_CHECKSUM_HISTORY);
    for (i = 0; i < LRG_SIMULATION_MAX_PLAYERS; i++)
    {
        self->current[i] = lrg_input_snapshot_new ();
        self->previous[i] = lrg_input_snapshot_new ();
    }
}

LrgSimulation *
lrg_simulation_new (guint64 seed)
{
    return g_object_new (LRG_TYPE_SIMULATION, "seed", seed, NULL);
}

guint64
lrg_simulation_get_seed (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), 0);

    return self->seed;
}

guint64
lrg_simulation_get_tick (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), 0);

    return self->tick;
}

gdouble
lrg_simulation_get_fixed_step (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), 0.0);

    return self->fixed_step;
}

void
lrg_simulation_set_fixed_step (LrgSimulation *self,
                               gdouble        step)
{
    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (step > 0.0);

    if (self->fixed_step == step)
        return;
    self->fixed_step = step;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIXED_STEP]);
}

guint
lrg_simulation_get_n_players (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), 0);

    return self->n_players;
}

void
lrg_simulation_set_n_players (LrgSimulation *self,
                              guint          n_players)
{
    guint i;

    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (n_players >= 1 && n_players <= LRG_SIMULATION_MAX_PLAYERS);

    for (i = 0; i < LRG_SIMULATION_MAX_PLAYERS; i++)
    {
        lrg_input_snapshot_clear (self->current[i]);
        lrg_input_snapshot_clear (self->previous[i]);
    }
    if (self->n_players == n_players)
        return;
    self->n_players = n_players;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PLAYERS]);
}

LrgRandomStream *
lrg_simulation_get_random (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);

    return self->random;
}

void
lrg_simulation_set_actions (LrgSimulation      *self,
                            const gchar *const *actions)
{
    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (actions != NULL);
    g_return_if_fail (g_strv_length ((gchar **) actions) <= LRG_INPUT_SNAPSHOT_MAX_ACTIONS);

    g_strfreev (self->actions);
    self->actions = g_strdupv ((gchar **) actions);
}

static gint
compare_names (gconstpointer a,
               gconstpointer b)
{
    return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

void
lrg_simulation_set_actions_from_map (LrgSimulation *self,
                                     LrgInputMap   *map)
{
    g_autoptr(GPtrArray) names = NULL;
    GList *actions;
    GList *l;

    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (LRG_IS_INPUT_MAP (map));

    /* Hash-table order differs between runs; sort by name for a stable
     * table every peer and every replay agrees on */
    names = g_ptr_array_new ();
    actions = lrg_input_map_get_actions (map);
    for (l = actions; l != NULL; l = l->next)
        g_ptr_array_add (names, (gpointer) lrg_input_action_get_name (LRG_INPUT_ACTION (l->data)));
    g_list_free (actions);

    g_ptr_array_sort (names, compare_names);
    if (names->len > LRG_INPUT_SNAPSHOT_MAX_ACTIONS)
    {
        lrg_log_warning ("Input map has %u actions; only the first %u (by name) are simulated",
                         names->len, (guint) LRG_INPUT_SNAPSHOT_MAX_ACTIONS);
        g_ptr_array_set_size (names, LRG_INPUT_SNAPSHOT_MAX_ACTIONS);
    }
    g_ptr_array_add (names, NULL);
    lrg_simulation_set_actions (self, (const gchar * const *) names->pdata);
}

const gchar * const *
lrg_simulation_get_actions (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);

    return (const gchar * const *) self->actions;
}

gint
lrg_simulation_get_action_index (LrgSimulation *self,
                                 const gchar   *action)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), -1);
    g_return_val_if_fail (action != NULL, -1);

    for (i = 0; self->actions[i] != NULL; i++)
    {
        if (strcmp (self->actions[i], action) == 0)
            return (gint) i;
    }
    return -1;
}

void
lrg_simulation_set_player_input_map (LrgSimulation *self,
                                     guint          player,
                                     LrgInputMap   *map)
{
    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (player < LRG_SIMULATION_MAX_PLAYERS);
    g_return_if_fail (map == NULL || LRG_IS_INPUT_MAP (map));

    if (self->maps[player] != NULL)
        lrg_input_map_clear_overrides (self->maps[player]);
    g_set_object (&self->maps[player], map);
}

void
lrg_simulation_release_inputs (LrgSimulation *self)
{
    guint i;

    g_return_if_fail (LRG_IS_SIMULATION (self));

    for (i = 0; i < LRG_SIMULATION_MAX_PLAYERS; i++)
    {
        if (self->maps[i] != NULL)
            lrg_input_map_clear_overrides (self->maps[i]);
    }
}

void
lrg_simulation_add_object (LrgSimulation *self,
                           GObject       *object)
{
    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (LRG_IS_SIM_STATEFUL (object) || LRG_IS_SAVEABLE (object));

    g_ptr_array_add (self->objects, g_object_ref (object));
}

gboolean
lrg_simulation_remove_object (LrgSimulation *self,
                              GObject       *object)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), FALSE);

    /* Keeps the order of the remaining objects */
    return g_ptr_array_remove (self->objects, object);
}

/* ==========================================================================
 * Stepping
 * ========================================================================== */

static void
store_checksum (LrgSimulation *self)
{
    ChecksumSlot *slot;

    slot = &self->history[(self->tick / self->checksum_interval) % LRG_SIM_CHECKSUM_HISTORY];
    slot->tick = self->tick;
    slot->checksum = lrg_simulation_checksum (self);
    slot->valid = TRUE;
}

void
lrg_simulation_step (LrgSimulation           *self,
                     LrgInputSnapshot *const *inputs,
                     guint                    n_inputs)
{
    gboolean has_checksum;
    guint i;

    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (inputs != NULL || n_inputs == 0);

    if (self->stepping)
    {
        g_critical ("lrg_simulation_step() called from inside a tick");
        return;
    }
    self->stepping = TRUE;

    /* 1. Rotate the input slots and inject this tick's input. A missing
     *    entry repeats the player's last snapshot (held, never re-pressed) */
    for (i = 0; i < self->n_players; i++)
    {
        lrg_input_snapshot_assign (self->previous[i], self->current[i]);
        if (i < n_inputs && inputs[i] != NULL)
            lrg_input_snapshot_assign (self->current[i], inputs[i]);

        if (self->maps[i] != NULL)
            lrg_input_snapshot_apply (self->current[i], self->previous[i],
                                      self->maps[i],
                                      (const gchar * const *) self->actions);
    }

    /* 2. Let the game simulate the tick */
    g_signal_emit (self, signals[SIGNAL_TICK], 0, self->tick, self->fixed_step);
    self->tick++;

    /* 3. Desync detector: checksum after every Nth tick */
    has_checksum = self->checksum_interval > 0 &&
                   self->tick % self->checksum_interval == 0;
    if (has_checksum)
        store_checksum (self);

    /* 4. Record the inputs that were actually simulated */
    if (self->recording != NULL)
    {
        guint64 checksum = 0;

        if (has_checksum)
            lrg_simulation_lookup_checksum (self, self->tick, &checksum);
        lrg_replay_append_tick (self->recording, self->current, self->n_players,
                                has_checksum, checksum);
    }

    self->stepping = FALSE;
}

const LrgInputSnapshot *
lrg_simulation_get_input (LrgSimulation *self,
                          guint          player)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);
    g_return_val_if_fail (player < LRG_SIMULATION_MAX_PLAYERS, NULL);

    return self->current[player];
}

const LrgInputSnapshot *
lrg_simulation_get_previous_input (LrgSimulation *self,
                                   guint          player)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);
    g_return_val_if_fail (player < LRG_SIMULATION_MAX_PLAYERS, NULL);

    return self->previous[player];
}

gboolean
lrg_simulation_is_down (LrgSimulation *self,
                        guint          player,
                        const gchar   *action)
{
    gint index;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), FALSE);
    g_return_val_if_fail (player < LRG_SIMULATION_MAX_PLAYERS, FALSE);

    index = lrg_simulation_get_action_index (self, action);
    if (index < 0)
        return FALSE;
    return lrg_input_snapshot_get_down (self->current[player], (guint) index);
}

gboolean
lrg_simulation_is_pressed (LrgSimulation *self,
                           guint          player,
                           const gchar   *action)
{
    gint index;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), FALSE);
    g_return_val_if_fail (player < LRG_SIMULATION_MAX_PLAYERS, FALSE);

    index = lrg_simulation_get_action_index (self, action);
    if (index < 0)
        return FALSE;
    return lrg_input_snapshot_get_down (self->current[player], (guint) index) &&
           !lrg_input_snapshot_get_down (self->previous[player], (guint) index);
}

gfloat
lrg_simulation_get_axis (LrgSimulation *self,
                         guint          player,
                         const gchar   *action)
{
    gint index;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), 0.0f);
    g_return_val_if_fail (player < LRG_SIMULATION_MAX_PLAYERS, 0.0f);

    index = lrg_simulation_get_action_index (self, action);
    if (index < 0)
        return 0.0f;
    return lrg_input_snapshot_get_axis (self->current[player], (guint) index);
}

/* ==========================================================================
 * State
 * ========================================================================== */

static const gchar *
object_state_id (GObject *object)
{
    if (LRG_IS_SIM_STATEFUL (object))
        return lrg_sim_stateful_get_state_id (LRG_SIM_STATEFUL (object));
    return lrg_saveable_get_save_id (LRG_SAVEABLE (object));
}

/*
 * save_object:
 *
 * Serializes one object into @out: LrgSimStateful objects directly,
 * LrgSaveable ones through a save context rendered to YAML text.
 */
static void
save_object (GObject        *object,
             LrgStateBuffer *out)
{
    g_autoptr(LrgSaveContext) context = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *text = NULL;

    if (LRG_IS_SIM_STATEFUL (object))
    {
        lrg_sim_stateful_save_state (LRG_SIM_STATEFUL (object), out);
        return;
    }

    context = lrg_save_context_new_for_save ();
    if (!lrg_saveable_save (LRG_SAVEABLE (object), context, &error) ||
        (text = lrg_save_context_to_string (context, &error)) == NULL)
    {
        lrg_log_warning ("Saving simulation object '%s' failed: %s",
                         object_state_id (object), error->message);
        lrg_state_buffer_write_string (out, NULL);
        return;
    }
    lrg_state_buffer_write_string (out, text);
}

static gboolean
load_object (GObject         *object,
             LrgStateBuffer  *in,
             GError         **error)
{
    g_autoptr(LrgSaveContext) context = NULL;
    g_autofree gchar *text = NULL;

    if (LRG_IS_SIM_STATEFUL (object))
        return lrg_sim_stateful_load_state (LRG_SIM_STATEFUL (object), in, error);

    text = lrg_state_buffer_read_string (in);
    if (text == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "No saved state for '%s'", object_state_id (object));
        return FALSE;
    }
    context = lrg_save_context_new_for_load (text, error);
    if (context == NULL)
        return FALSE;
    return lrg_saveable_load (LRG_SAVEABLE (object), context, error);
}

void
lrg_simulation_save_state (LrgSimulation  *self,
                           LrgStateBuffer *buffer)
{
    g_autoptr(LrgStateBuffer) object_buffer = NULL;
    g_autofree gchar *random_state = NULL;
    guint i;

    g_return_if_fail (LRG_IS_SIMULATION (self));
    g_return_if_fail (LRG_IS_STATE_BUFFER (buffer));

    /* Header */
    lrg_state_buffer_write_uint32 (buffer, LRG_SIM_STATE_MAGIC);
    lrg_state_buffer_write_uint32 (buffer, LRG_SIM_STATE_VERSION);
    lrg_state_buffer_write_uint64 (buffer, self->tick);

    /* Random stream */
    random_state = lrg_random_stream_snapshot (self->random);
    lrg_state_buffer_write_string (buffer, random_state);

    /* Last simulated input per player: the next tick derives its
     * pressed/released edges from it */
    lrg_state_buffer_write_uint32 (buffer, self->n_players);
    for (i = 0; i < self->n_players; i++)
        lrg_input_snapshot_write (self->current[i], buffer);

    /* Objects, each as a length-prefixed block so one object cannot read
     * into the next */
    object_buffer = lrg_state_buffer_new ();
    lrg_state_buffer_write_uint32 (buffer, self->objects->len);
    for (i = 0; i < self->objects->len; i++)
    {
        GObject *object = g_ptr_array_index (self->objects, i);
        const guint8 *data;
        gsize size;

        lrg_state_buffer_reset (object_buffer);
        save_object (object, object_buffer);
        data = lrg_state_buffer_get_data (object_buffer, &size);
        lrg_state_buffer_write_string (buffer, object_state_id (object));
        lrg_state_buffer_write_data (buffer, data, size);
    }
}

GBytes *
lrg_simulation_save_bytes (LrgSimulation *self)
{
    g_autoptr(LrgStateBuffer) buffer = NULL;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);

    buffer = lrg_state_buffer_new ();
    lrg_simulation_save_state (self, buffer);
    return lrg_state_buffer_to_bytes (buffer);
}

gboolean
lrg_simulation_load_state (LrgSimulation   *self,
                           LrgStateBuffer  *buffer,
                           GError         **error)
{
    g_autofree gchar *random_state = NULL;
    guint64 tick;
    guint n_players;
    guint n_objects;
    guint i;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), FALSE);
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (buffer), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Header */
    if (lrg_state_buffer_read_uint32 (buffer) != LRG_SIM_STATE_MAGIC ||
        lrg_state_buffer_read_uint32 (buffer) != LRG_SIM_STATE_VERSION)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Not a simulation state (bad magic or version)");
        return FALSE;
    }
    tick = lrg_state_buffer_read_uint64 (buffer);

    /* Random stream */
    random_state = lrg_state_buffer_read_string (buffer);
    if (!lrg_state_buffer_check (buffer, error) ||
        !lrg_random_stream_restore (self->random, random_state, error))
        return FALSE;

    /* Inputs */
    n_players = lrg_state_buffer_read_uint32 (buffer);
    if (n_players != self->n_players)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "State has %u players, simulation has %u",
                     n_players, self->n_players);
        return FALSE;
    }
    for (i = 0; i < n_players; i++)
    {
        if (!lrg_input_snapshot_read (self->current[i], buffer))
            return lrg_state_buffer_check (buffer, error);
    }

    /* Objects: same count and identifiers, in order */
    n_objects = lrg_state_buffer_read_uint32 (buffer);
    if (n_objects != self->objects->len)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "State has %u objects, simulation has %u",
                     n_objects, self->objects->len);
        return FALSE;
    }
    for (i = 0; i < n_objects; i++)
    {
        GObject *object = g_ptr_array_index (self->objects, i);
        g_autofree gchar *id = NULL;
        g_autoptr(GBytes) block = NULL;
        g_autoptr(LrgStateBuffer) object_buffer = NULL;

        id = lrg_state_buffer_read_string (buffer);
        block = lrg_state_buffer_read_data (buffer);
        if (!lrg_state_buffer_check (buffer, error))
            return FALSE;
        if (g_strcmp0 (id, object_state_id (object)) != 0)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "State object %u is '%s', simulation has '%s'",
                         i, id != NULL ? id : "(null)", object_state_id (object));
            return FALSE;
        }

        object_buffer = lrg_state_buffer_new_from_bytes (block);
        if (!load_object (object, object_buffer, error))
            return FALSE;
    }

    self->tick = tick;
    return TRUE;
}

gboolean
lrg_simulation_load_bytes (LrgSimulation  *self,
                           GBytes         *bytes,
                           GError        **error)
{
    g_autoptr(LrgStateBuffer) buffer = NULL;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), FALSE);
    g_return_val_if_fail (bytes != NULL, FALSE);

    buffer = lrg_state_buffer_new_from_bytes (bytes);
    return lrg_simulation_load_state (self, buffer, error);
}

guint64
lrg_simulation_checksum (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), 0);

    lrg_state_buffer_reset (self->scratch);
    lrg_simulation_save_state (self, self->scratch);
    return lrg_state_buffer_checksum (self->scratch);
}

/* ==========================================================================
 * Desync detector
 * ========================================================================== */

guint
lrg_simulation_get_checksum_interval (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), 0);

    return self->checksum_interval;
}

void
lrg_simulation_set_checksum_interval (LrgSimulation *self,
                                      guint          interval)
{
    g_return_if_fail (LRG_IS_SIMULATION (self));

    if (self->checksum_interval == interval)
        return;

    /* Slots are indexed by tick / interval: a new interval invalidates them */
    memset (self->history, 0, sizeof (ChecksumSlot) * LRG_SIM_CHECKSUM_HISTORY);
    self->checksum_interval = interval;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHECKSUM_INTERVAL]);
}

gboolean
lrg_simulation_lookup_checksum (LrgSimulation *self,
                                guint64        tick,
                                guint64       *checksum)
{
    const ChecksumSlot *slot;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), FALSE);

    if (self->checksum_interval == 0 || tick % self->checksum_interval != 0)
        return FALSE;

    slot = &self->history[(tick / self->checksum_interval) % LRG_SIM_CHECKSUM_HISTORY];
    if (!slot->valid || slot->tick != tick)
        return FALSE;
    if (checksum != NULL)
        *checksum = slot->checksum;
    return TRUE;
}

gboolean
lrg_simulation_verify_checksum (LrgSimulation *self,
                                guint64        tick,
                                guint64        expected)
{
    guint64 local;

    g_return_val_if_fail (LRG_IS_SIMULATION (self), FALSE);

    /* The current state can always be checked, even between intervals */
    if (tick == self->tick)
        local = lrg_simulation_checksum (self);
    else if (!lrg_simulation_lookup_checksum (self, tick, &local))
        return TRUE;

    if (local == expected)
        return TRUE;

    if (self->first_desync_tick < 0 || (gint64) tick < self->first_desync_tick)
        self->first_desync_tick = (gint64) tick;
    lrg_log_debug ("Desync at tick %" G_GUINT64_FORMAT ": local %016" G_GINT64_MODIFIER
                   "x, expected %016" G_GINT64_MODIFIER "x", tick, local, expected);
    g_signal_emit (self, signals[SIGNAL_DESYNC], 0, tick, local, expected);
    return FALSE;
}

gint64
lrg_simulation_get_first_desync_tick (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), -1);

    return self->first_desync_tick;
}

/* ==========================================================================
 * Recording
 * ========================================================================== */

LrgReplay *
lrg_simulation_start_recording (LrgSimulation *self,
                                const gchar   *initial_state)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);

    g_clear_object (&self->recording);
    self->recording = lrg_replay_new_for_simulation (self, initial_state);
    return self->recording;
}

LrgReplay *
lrg_simulation_get_recording (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);

    return self->recording;
}

LrgReplay *
lrg_simulation_stop_recording (LrgSimulation *self)
{
    g_return_val_if_fail (LRG_IS_SIMULATION (self), NULL);

    return g_steal_pointer (&self->recording);
}
