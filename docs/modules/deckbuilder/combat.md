# Combat System

The combat system provides turn-based combat with damage, block, status effects, and enemy AI. It uses interfaces for flexibility and supports both traditional combat and alternative rules.

## LrgCombatant

The combatant interface defines the contract for anything that can participate in combat.

### Interface Methods

```c
struct _LrgCombatantInterface
{
    GTypeInterface g_iface;

    /* Health management */
    gint     (*get_current_health) (LrgCombatant *self);
    gint     (*get_max_health)     (LrgCombatant *self);
    void     (*take_damage)        (LrgCombatant *self,
                                    gint          amount,
                                    LrgEffectFlags flags);
    void     (*heal)               (LrgCombatant *self,
                                    gint          amount);
    void     (*lose_hp)            (LrgCombatant *self,
                                    gint          amount);

    /* Block management */
    gint     (*get_block)          (LrgCombatant *self);
    void     (*add_block)          (LrgCombatant *self,
                                    gint          amount);
    void     (*clear_block)        (LrgCombatant *self);

    /* Status effects */
    void     (*apply_status)       (LrgCombatant *self,
                                    const gchar  *status_id,
                                    gint          stacks);
    void     (*remove_status)      (LrgCombatant *self,
                                    const gchar  *status_id,
                                    gint          stacks);
    gint     (*get_status_stacks)  (LrgCombatant *self,
                                    const gchar  *status_id);
    GPtrArray * (*get_all_statuses)(LrgCombatant *self);
};
```

### Using Combatants

```c
/* Deal damage with block absorption */
void
deal_damage (LrgCombatant *target, gint amount, LrgEffectFlags flags)
{
    gint block = lrg_combatant_get_block (target);

    if (!(flags & LRG_EFFECT_FLAG_UNBLOCKABLE))
    {
        if (block >= amount)
        {
            /* Block absorbs all damage */
            lrg_combatant_add_block (target, -amount);
            return;
        }
        else
        {
            /* Partial block */
            amount -= block;
            lrg_combatant_clear_block (target);
        }
    }

    lrg_combatant_take_damage (target, amount, flags);
}
```

## LrgPlayerCombatant

The player implementation of the combatant interface.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `current-health` | `gint` | Current HP |
| `max-health` | `gint` | Maximum HP |
| `block` | `gint` | Current block |
| `energy` | `gint` | Current energy |
| `max-energy` | `gint` | Energy per turn |

### Player Setup

```c
/* Create player from character */
LrgCharacterDef *character = lrg_deckbuilder_manager_get_character (manager, "ironclad");
LrgPlayerCombatant *player = lrg_player_combatant_new (character);

/* Set health from run state */
lrg_player_combatant_set_max_health (player, lrg_run_get_max_health (run));
lrg_player_combatant_set_current_health (player, lrg_run_get_current_health (run));
```

## LrgEnemyDef

Enemy definitions define the blueprint for enemy types.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique enemy identifier |
| `name` | `gchar*` | Display name |
| `min-health` | `gint` | Minimum HP |
| `max-health` | `gint` | Maximum HP |
| `is-elite` | `gboolean` | Is an elite enemy |
| `is-boss` | `gboolean` | Is a boss |

### Virtual Methods

```c
struct _LrgEnemyDefClass
{
    GObjectClass parent_class;

    /* Choose next intent based on AI pattern */
    LrgEnemyIntent * (*choose_intent) (LrgEnemyDef      *self,
                                        LrgEnemyInstance *instance,
                                        LrgCombatContext *ctx);

    /* Execute the current intent */
    void (*execute_turn) (LrgEnemyDef      *self,
                          LrgEnemyInstance *instance,
                          LrgCombatContext *ctx);

    /* Called when enemy spawns */
    void (*on_spawn)     (LrgEnemyDef      *self,
                          LrgEnemyInstance *instance,
                          LrgCombatContext *ctx);

    /* Called when enemy dies */
    void (*on_death)     (LrgEnemyDef      *self,
                          LrgEnemyInstance *instance,
                          LrgCombatContext *ctx);

    gpointer _reserved[8];
};
```

## LrgEnemyIntent

Intents represent what an enemy will do on their turn.

### Intent Types

```c
typedef enum {
    LRG_ENEMY_INTENT_UNKNOWN,   /* Hidden intent */
    LRG_ENEMY_INTENT_ATTACK,    /* Will deal damage */
    LRG_ENEMY_INTENT_DEFEND,    /* Will gain block */
    LRG_ENEMY_INTENT_BUFF,      /* Will buff self */
    LRG_ENEMY_INTENT_DEBUFF,    /* Will debuff player */
    LRG_ENEMY_INTENT_SPECIAL    /* Special action */
} LrgEnemyIntentType;
```

### Intent Properties

| Property | Type | Description |
|----------|------|-------------|
| `type` | `LrgEnemyIntentType` | Intent category |
| `damage` | `gint` | Damage to deal (if attack) |
| `hits` | `gint` | Number of hits |
| `block` | `gint` | Block to gain (if defend) |
| `status` | `gchar*` | Status to apply |
| `stacks` | `gint` | Status stacks |

```c
/* Create attack intent */
LrgEnemyIntent *intent = lrg_enemy_intent_new (LRG_ENEMY_INTENT_ATTACK);
lrg_enemy_intent_set_damage (intent, 12);
lrg_enemy_intent_set_hits (intent, 2);  /* 12x2 */
```

## LrgCombatContext

Combat context holds the state of an ongoing combat.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `player` | `LrgPlayerCombatant*` | The player |
| `enemies` | `GPtrArray*` | Active enemies |
| `turn` | `gint` | Current turn number |
| `phase` | `LrgCombatPhase` | Current phase |
| `draw-pile` | `LrgCardPile*` | Draw pile |
| `hand` | `LrgHand*` | Player's hand |
| `discard-pile` | `LrgCardPile*` | Discard pile |
| `exhaust-pile` | `LrgCardPile*` | Exhausted cards |
| `effect-stack` | `LrgEffectStack*` | Effect resolution stack |

### Combat Phases

```c
typedef enum {
    LRG_COMBAT_PHASE_SETUP,        /* Initializing combat */
    LRG_COMBAT_PHASE_PLAYER_START, /* Start of player turn */
    LRG_COMBAT_PHASE_PLAYER_PLAY,  /* Player can play cards */
    LRG_COMBAT_PHASE_PLAYER_END,   /* End of player turn */
    LRG_COMBAT_PHASE_ENEMY_TURN,   /* Enemies execute intents */
    LRG_COMBAT_PHASE_FINISHED      /* Combat ended */
} LrgCombatPhase;
```

## LrgCombatRules

Combat rules interface allows customizing combat mechanics.

```c
struct _LrgCombatRulesInterface
{
    GTypeInterface g_iface;

    /* Damage calculation */
    gint (*calculate_damage) (LrgCombatRules   *self,
                              gint              base_damage,
                              LrgCombatant     *attacker,
                              LrgCombatant     *defender);

    /* Block calculation */
    gint (*calculate_block)  (LrgCombatRules   *self,
                              gint              base_block,
                              LrgCombatant     *blocker);

    /* Turn resources */
    gint (*get_cards_per_turn)  (LrgCombatRules *self,
                                 LrgCombatant   *combatant);
    gint (*get_energy_per_turn) (LrgCombatRules *self,
                                 LrgCombatant   *combatant);
    gint (*get_block_decay)     (LrgCombatRules *self,
                                 LrgCombatant   *combatant);

    /* Win/lose conditions */
    gboolean (*check_victory) (LrgCombatRules   *self,
                               LrgCombatContext *ctx);
    gboolean (*check_defeat)  (LrgCombatRules   *self,
                               LrgCombatContext *ctx);
};
```

## LrgCombatManager

The combat manager controls combat flow. It's a derivable type for customization.

### Starting Combat

```c
/* Get manager */
LrgCombatManager *combat = lrg_combat_manager_get_default ();

/* Start combat with enemies */
GPtrArray *enemies = g_ptr_array_new ();
g_ptr_array_add (enemies, slime_instance);
g_ptr_array_add (enemies, gremlin_instance);

lrg_combat_manager_start_combat (combat, run, enemies);
```

### Combat Loop

```c
/* Main combat loop */
while (lrg_combat_manager_get_phase (combat) != LRG_COMBAT_PHASE_FINISHED)
{
    LrgCombatPhase phase = lrg_combat_manager_get_phase (combat);

    switch (phase)
    {
        case LRG_COMBAT_PHASE_PLAYER_PLAY:
            /* Player input phase */
            if (player_wants_to_play_card)
            {
                LrgCardInstance *card = selected_card;
                LrgCombatant *target = selected_target;

                if (lrg_combat_manager_can_play_card (combat, card))
                {
                    lrg_combat_manager_play_card (combat, card, target);
                }
            }
            else if (player_wants_to_end_turn)
            {
                lrg_combat_manager_end_turn (combat);
            }
            break;

        case LRG_COMBAT_PHASE_ENEMY_TURN:
            /* Enemies act automatically */
            lrg_combat_manager_execute_enemy_turn (combat);
            break;

        default:
            lrg_combat_manager_advance_phase (combat);
            break;
    }
}

/* Get result */
LrgCombatResult result = lrg_combat_manager_get_result (combat);
if (result == LRG_COMBAT_RESULT_VICTORY)
{
    /* Show rewards */
}
```

### Playing Cards

```c
/* Check if card can be played */
gboolean can_play = lrg_combat_manager_can_play_card (combat, card);

/* Get energy cost */
gint cost = lrg_card_instance_get_effective_cost (card, ctx);

/* Play the card */
lrg_combat_manager_play_card (combat, card, target);
```

### Signals

| Signal | Description |
|--------|-------------|
| `combat-started` | Combat has begun |
| `combat-ended` | Combat has ended |
| `turn-started` | Turn has started |
| `turn-ended` | Turn has ended |
| `card-played` | Card was played |
| `card-drawn` | Card was drawn |
| `card-discarded` | Card was discarded |
| `damage-dealt` | Damage was dealt |
| `block-gained` | Block was gained |
| `enemy-died` | Enemy was defeated |
| `phase-changed` | Combat phase changed |

## Combat Flow

1. **Setup Phase**: Initialize combat, set up deck, reveal enemy intents
2. **Player Start**: Draw cards, reset energy, trigger start-of-turn effects
3. **Player Play**: Player can play cards and use potions
4. **Player End**: Discard hand (respecting Retain), trigger end-of-turn effects
5. **Enemy Turn**: Enemies execute intents, choose next intents
6. **Loop**: Return to Player Start or end if victory/defeat

```c
/* Combat manager handles this internally */
static void
advance_to_next_phase (LrgCombatManager *self)
{
    switch (self->phase)
    {
        case LRG_COMBAT_PHASE_SETUP:
            setup_combat (self);
            self->phase = LRG_COMBAT_PHASE_PLAYER_START;
            break;

        case LRG_COMBAT_PHASE_PLAYER_START:
            start_player_turn (self);
            self->phase = LRG_COMBAT_PHASE_PLAYER_PLAY;
            break;

        case LRG_COMBAT_PHASE_PLAYER_PLAY:
            /* Wait for end_turn() call */
            break;

        case LRG_COMBAT_PHASE_PLAYER_END:
            end_player_turn (self);
            self->phase = LRG_COMBAT_PHASE_ENEMY_TURN;
            break;

        case LRG_COMBAT_PHASE_ENEMY_TURN:
            execute_enemy_turns (self);
            if (check_combat_end (self))
                self->phase = LRG_COMBAT_PHASE_FINISHED;
            else
                self->phase = LRG_COMBAT_PHASE_PLAYER_START;
            break;
    }
}
```

## See Also

- [Cards Documentation](cards.md)
- [Effects Documentation](effects.md)
- [Status Effects Documentation](status-effects.md)
- [Run Structure Documentation](run-structure.md)
