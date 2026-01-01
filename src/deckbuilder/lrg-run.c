/* lrg-run.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-run.h"
#include "lrg-relic-def.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgRun:
 *
 * Represents a complete deckbuilder run from start to finish.
 *
 * A run tracks:
 * - The player's character and stats
 * - The master deck (cards collected during the run)
 * - Relics collected
 * - Potions held
 * - Gold accumulated
 * - Current position on the map
 * - Run statistics (time, kills, floors cleared)
 *
 * The run progresses through multiple acts, each with its own map.
 * Victory is achieved by defeating the final boss.
 *
 * Since: 1.0
 */
struct _LrgRun
{
    GObject parent_instance;

    gchar              *character_id;
    guint64             seed;
    LrgRunState         state;

    /* Player state */
    LrgPlayerCombatant *player;
    LrgDeckInstance    *deck;
    gint                gold;

    /* Inventory */
    GPtrArray          *relics;   /* LrgRelicInstance */
    GPtrArray          *potions;  /* LrgPotionInstance */
    gint                max_potions;

    /* Map state */
    gint                current_act;
    gint                current_floor;
    LrgRunMap          *map;
    LrgMapNode         *current_node;  /* weak ref */

    /* Statistics */
    gint                total_floors_cleared;
    gint                enemies_killed;
    gdouble             elapsed_time;
};

G_DEFINE_FINAL_TYPE (LrgRun, lrg_run, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CHARACTER_ID,
    PROP_SEED,
    PROP_STATE,
    PROP_GOLD,
    PROP_CURRENT_ACT,
    PROP_MAX_POTIONS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signals */
enum
{
    SIGNAL_STATE_CHANGED,
    SIGNAL_GOLD_CHANGED,
    SIGNAL_RELIC_ADDED,
    SIGNAL_POTION_ADDED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_run_finalize (GObject *object)
{
    LrgRun *self = LRG_RUN (object);

    g_free (self->character_id);
    g_clear_object (&self->player);
    g_clear_object (&self->deck);
    g_clear_object (&self->map);
    g_ptr_array_unref (self->relics);
    g_ptr_array_unref (self->potions);

    G_OBJECT_CLASS (lrg_run_parent_class)->finalize (object);
}

static void
lrg_run_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
    LrgRun *self = LRG_RUN (object);

    switch (prop_id)
    {
    case PROP_CHARACTER_ID:
        g_value_set_string (value, self->character_id);
        break;
    case PROP_SEED:
        g_value_set_uint64 (value, self->seed);
        break;
    case PROP_STATE:
        g_value_set_enum (value, self->state);
        break;
    case PROP_GOLD:
        g_value_set_int (value, self->gold);
        break;
    case PROP_CURRENT_ACT:
        g_value_set_int (value, self->current_act);
        break;
    case PROP_MAX_POTIONS:
        g_value_set_int (value, self->max_potions);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_run_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
    LrgRun *self = LRG_RUN (object);

    switch (prop_id)
    {
    case PROP_CHARACTER_ID:
        g_free (self->character_id);
        self->character_id = g_value_dup_string (value);
        break;
    case PROP_SEED:
        self->seed = g_value_get_uint64 (value);
        break;
    case PROP_STATE:
        lrg_run_set_state (self, g_value_get_enum (value));
        break;
    case PROP_GOLD:
        lrg_run_set_gold (self, g_value_get_int (value));
        break;
    case PROP_CURRENT_ACT:
        self->current_act = g_value_get_int (value);
        break;
    case PROP_MAX_POTIONS:
        self->max_potions = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_run_class_init (LrgRunClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_run_finalize;
    object_class->get_property = lrg_run_get_property;
    object_class->set_property = lrg_run_set_property;

    /**
     * LrgRun:character-id:
     *
     * The character class identifier.
     *
     * Since: 1.0
     */
    properties[PROP_CHARACTER_ID] =
        g_param_spec_string ("character-id",
                             "Character ID",
                             "Character class identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRun:seed:
     *
     * Random seed for the run.
     *
     * Since: 1.0
     */
    properties[PROP_SEED] =
        g_param_spec_uint64 ("seed",
                             "Seed",
                             "Random seed",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRun:state:
     *
     * Current run state.
     *
     * Since: 1.0
     */
    properties[PROP_STATE] =
        g_param_spec_enum ("state",
                           "State",
                           "Current run state",
                           LRG_TYPE_RUN_STATE,
                           LRG_RUN_STATE_NOT_STARTED,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRun:gold:
     *
     * Player's current gold.
     *
     * Since: 1.0
     */
    properties[PROP_GOLD] =
        g_param_spec_int ("gold",
                          "Gold",
                          "Current gold",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRun:current-act:
     *
     * Current act number (1-based).
     *
     * Since: 1.0
     */
    properties[PROP_CURRENT_ACT] =
        g_param_spec_int ("current-act",
                          "Current Act",
                          "Current act number",
                          1, G_MAXINT, 1,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRun:max-potions:
     *
     * Maximum potion slots.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_POTIONS] =
        g_param_spec_int ("max-potions",
                          "Max Potions",
                          "Maximum potion slots",
                          0, G_MAXINT, 3,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgRun::state-changed:
     * @self: the run
     * @old_state: previous state
     * @new_state: new state
     *
     * Emitted when the run state changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_STATE_CHANGED] =
        g_signal_new ("state-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_RUN_STATE,
                      LRG_TYPE_RUN_STATE);

    /**
     * LrgRun::gold-changed:
     * @self: the run
     * @old_gold: previous gold
     * @new_gold: new gold
     *
     * Emitted when gold changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_GOLD_CHANGED] =
        g_signal_new ("gold-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_INT, G_TYPE_INT);

    /**
     * LrgRun::relic-added:
     * @self: the run
     * @relic: the relic that was added
     *
     * Emitted when a relic is added.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RELIC_ADDED] =
        g_signal_new ("relic-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_RELIC_INSTANCE);

    /**
     * LrgRun::potion-added:
     * @self: the run
     * @potion: the potion that was added
     *
     * Emitted when a potion is added.
     *
     * Since: 1.0
     */
    signals[SIGNAL_POTION_ADDED] =
        g_signal_new ("potion-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_POTION_INSTANCE);
}

static void
lrg_run_init (LrgRun *self)
{
    self->state = LRG_RUN_STATE_NOT_STARTED;
    self->gold = 0;
    self->current_act = 1;
    self->current_floor = 0;
    self->max_potions = 3;

    self->relics = g_ptr_array_new_with_free_func (g_object_unref);
    self->potions = g_ptr_array_new_with_free_func (g_object_unref);

    self->total_floors_cleared = 0;
    self->enemies_killed = 0;
    self->elapsed_time = 0.0;
}

LrgRun *
lrg_run_new (const gchar *character_id,
             guint64      seed)
{
    g_return_val_if_fail (character_id != NULL, NULL);

    return g_object_new (LRG_TYPE_RUN,
                         "character-id", character_id,
                         "seed", seed,
                         NULL);
}

guint64
lrg_run_get_seed (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0);

    return self->seed;
}

const gchar *
lrg_run_get_character_id (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), NULL);

    return self->character_id;
}

LrgRunState
lrg_run_get_state (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), LRG_RUN_STATE_NOT_STARTED);

    return self->state;
}

void
lrg_run_set_state (LrgRun     *self,
                   LrgRunState state)
{
    LrgRunState old_state;

    g_return_if_fail (LRG_IS_RUN (self));

    if (self->state != state)
    {
        old_state = self->state;
        self->state = state;

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
        g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, old_state, state);

        lrg_debug (LRG_LOG_DOMAIN,
                   "Run state changed: %d -> %d", old_state, state);
    }
}

LrgPlayerCombatant *
lrg_run_get_player (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), NULL);

    return self->player;
}

void
lrg_run_set_player (LrgRun             *self,
                    LrgPlayerCombatant *player)
{
    g_return_if_fail (LRG_IS_RUN (self));

    if (g_set_object (&self->player, player))
    {
        lrg_debug (LRG_LOG_DOMAIN, "Player set for run");
    }
}

LrgDeckInstance *
lrg_run_get_deck (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), NULL);

    return self->deck;
}

void
lrg_run_set_deck (LrgRun          *self,
                  LrgDeckInstance *deck)
{
    g_return_if_fail (LRG_IS_RUN (self));

    if (g_set_object (&self->deck, deck))
    {
        lrg_debug (LRG_LOG_DOMAIN, "Deck set for run");
    }
}

gint
lrg_run_get_current_act (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0);

    return self->current_act;
}

gint
lrg_run_get_current_floor (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0);

    return self->current_floor;
}

LrgRunMap *
lrg_run_get_map (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), NULL);

    return self->map;
}

void
lrg_run_set_map (LrgRun    *self,
                 LrgRunMap *map)
{
    g_return_if_fail (LRG_IS_RUN (self));

    if (g_set_object (&self->map, map))
    {
        self->current_floor = 0;
        self->current_node = NULL;
        lrg_debug (LRG_LOG_DOMAIN, "Map set for run");
    }
}

LrgMapNode *
lrg_run_get_current_node (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), NULL);

    return self->current_node;
}

void
lrg_run_set_current_node (LrgRun     *self,
                          LrgMapNode *node)
{
    g_return_if_fail (LRG_IS_RUN (self));

    if (self->current_node != node)
    {
        self->current_node = node;

        if (node != NULL)
        {
            self->current_floor = lrg_map_node_get_row (node);
            lrg_map_node_set_visited (node, TRUE);
        }

        lrg_debug (LRG_LOG_DOMAIN,
                   "Current node set to %s (floor %d)",
                   node ? lrg_map_node_get_id (node) : "(none)",
                   self->current_floor);
    }
}

void
lrg_run_advance_act (LrgRun *self)
{
    g_return_if_fail (LRG_IS_RUN (self));

    self->current_act++;
    self->current_floor = 0;
    self->current_node = NULL;
    g_clear_object (&self->map);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_ACT]);

    lrg_debug (LRG_LOG_DOMAIN, "Advanced to act %d", self->current_act);
}

GPtrArray *
lrg_run_get_relics (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), NULL);

    return self->relics;
}

void
lrg_run_add_relic (LrgRun           *self,
                   LrgRelicInstance *relic)
{
    g_return_if_fail (LRG_IS_RUN (self));
    g_return_if_fail (LRG_IS_RELIC_INSTANCE (relic));

    g_ptr_array_add (self->relics, relic);  /* Takes ownership */

    g_signal_emit (self, signals[SIGNAL_RELIC_ADDED], 0, relic);

    lrg_debug (LRG_LOG_DOMAIN,
               "Added relic: %s (total: %u)",
               lrg_relic_instance_get_id (relic),
               self->relics->len);
}

gboolean
lrg_run_has_relic (LrgRun      *self,
                   const gchar *relic_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_RUN (self), FALSE);
    g_return_val_if_fail (relic_id != NULL, FALSE);

    for (i = 0; i < self->relics->len; i++)
    {
        LrgRelicInstance *relic = g_ptr_array_index (self->relics, i);
        LrgRelicDef *def = lrg_relic_instance_get_def (relic);

        if (def != NULL && g_strcmp0 (lrg_relic_def_get_id (def), relic_id) == 0)
            return TRUE;
    }

    return FALSE;
}

LrgRelicInstance *
lrg_run_get_relic (LrgRun      *self,
                   const gchar *relic_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_RUN (self), NULL);
    g_return_val_if_fail (relic_id != NULL, NULL);

    for (i = 0; i < self->relics->len; i++)
    {
        LrgRelicInstance *relic = g_ptr_array_index (self->relics, i);
        LrgRelicDef *def = lrg_relic_instance_get_def (relic);

        if (def != NULL && g_strcmp0 (lrg_relic_def_get_id (def), relic_id) == 0)
            return relic;
    }

    return NULL;
}

GPtrArray *
lrg_run_get_potions (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), NULL);

    return self->potions;
}

gint
lrg_run_get_max_potions (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0);

    return self->max_potions;
}

void
lrg_run_set_max_potions (LrgRun *self,
                         gint    max)
{
    g_return_if_fail (LRG_IS_RUN (self));
    g_return_if_fail (max >= 0);

    if (self->max_potions != max)
    {
        self->max_potions = max;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_POTIONS]);
    }
}

gboolean
lrg_run_add_potion (LrgRun            *self,
                    LrgPotionInstance *potion)
{
    g_return_val_if_fail (LRG_IS_RUN (self), FALSE);
    g_return_val_if_fail (LRG_IS_POTION_INSTANCE (potion), FALSE);

    if ((gint)self->potions->len >= self->max_potions)
    {
        lrg_debug (LRG_LOG_DOMAIN, "Cannot add potion: inventory full");
        g_object_unref (potion);  /* We took ownership */
        return FALSE;
    }

    g_ptr_array_add (self->potions, potion);  /* Takes ownership */

    g_signal_emit (self, signals[SIGNAL_POTION_ADDED], 0, potion);

    lrg_debug (LRG_LOG_DOMAIN,
               "Added potion (total: %u/%d)",
               self->potions->len, self->max_potions);

    return TRUE;
}

gboolean
lrg_run_remove_potion (LrgRun *self,
                       guint   index)
{
    g_return_val_if_fail (LRG_IS_RUN (self), FALSE);

    if (index >= self->potions->len)
        return FALSE;

    g_ptr_array_remove_index (self->potions, index);

    lrg_debug (LRG_LOG_DOMAIN,
               "Removed potion at index %u (remaining: %u)",
               index, self->potions->len);

    return TRUE;
}

gint
lrg_run_get_gold (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0);

    return self->gold;
}

void
lrg_run_set_gold (LrgRun *self,
                  gint    gold)
{
    gint old_gold;

    g_return_if_fail (LRG_IS_RUN (self));

    if (gold < 0)
        gold = 0;

    if (self->gold != gold)
    {
        old_gold = self->gold;
        self->gold = gold;

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GOLD]);
        g_signal_emit (self, signals[SIGNAL_GOLD_CHANGED], 0, old_gold, gold);
    }
}

void
lrg_run_add_gold (LrgRun *self,
                  gint    amount)
{
    g_return_if_fail (LRG_IS_RUN (self));

    if (amount != 0)
    {
        lrg_run_set_gold (self, self->gold + amount);
    }
}

gboolean
lrg_run_spend_gold (LrgRun *self,
                    gint    amount)
{
    g_return_val_if_fail (LRG_IS_RUN (self), FALSE);
    g_return_val_if_fail (amount >= 0, FALSE);

    if (self->gold < amount)
    {
        lrg_debug (LRG_LOG_DOMAIN,
                   "Cannot spend %d gold (only have %d)",
                   amount, self->gold);
        return FALSE;
    }

    lrg_run_set_gold (self, self->gold - amount);
    return TRUE;
}

gint
lrg_run_get_total_floors_cleared (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0);

    return self->total_floors_cleared;
}

gint
lrg_run_get_enemies_killed (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0);

    return self->enemies_killed;
}

void
lrg_run_add_enemy_killed (LrgRun *self)
{
    g_return_if_fail (LRG_IS_RUN (self));

    self->enemies_killed++;
}

gdouble
lrg_run_get_elapsed_time (LrgRun *self)
{
    g_return_val_if_fail (LRG_IS_RUN (self), 0.0);

    return self->elapsed_time;
}

void
lrg_run_add_elapsed_time (LrgRun  *self,
                          gdouble  seconds)
{
    g_return_if_fail (LRG_IS_RUN (self));

    if (seconds > 0)
    {
        self->elapsed_time += seconds;
    }
}
