# Deckbuilder Module

The Deckbuilder module provides a complete roguelike deckbuilding framework for games, supporting both **combat deckbuilders** (Slay the Spire, Monster Train) and **scoring deckbuilders** (Balatro). It includes cards, decks, effects, keywords, status effects, relics, combat, run/map progression, scoring, and meta-progression systems.

## Overview

The module is organized into several interconnected systems:

1. **Card System** - Card definitions, instances, piles, and hand management
2. **Deck System** - Deck templates, runtime state, and construction
3. **Effect System** - Card effects with resolution stack and event bus
4. **Keyword System** - Built-in and custom card keywords with synergies
5. **Status Effect System** - Buffs and debuffs with lifecycle hooks
6. **Relic/Potion System** - Passive modifiers and consumable items
7. **Combat System** - Turn-based combat with enemies and intents
8. **Run/Map System** - Procedural map generation and progression
9. **Scoring System** - Poker hand evaluation and joker modifiers (Balatro-style)
10. **Meta-Progression** - Characters, unlocks, ascension, and profiles

## Key Features

- **Dual Game Support**: Both combat and scoring deckbuilder paradigms
- **Full Combat System**: Enemies, intents, damage/block, turn phases
- **Scoring System**: Poker hands, chips/mult, jokers, card enhancements
- **Effect Resolution Stack**: Proper ordering of card effects and interrupts
- **Event Bus**: Trigger system for complex card interactions
- **Status Effects**: Buffs/debuffs with stacking, duration, and lifecycle hooks
- **Relics and Potions**: Passive and active item systems
- **Procedural Maps**: Weighted encounter pools, shops, events, rest sites
- **Meta-Progression**: Characters, unlocks, ascension levels (A1-A20)
- **Save/Load Support**: Full run state persistence via LrgSaveable
- **Mod Extensibility**: All major types are derivable with provider interfaces

## Module Structure

### Core Card Types

- **[LrgCardDef](cards.md#lrgcarddef)** - Card definition (derivable)
- **[LrgCardInstance](cards.md#lrgcardinstance)** - Runtime card instance
- **[LrgCardPile](cards.md#lrgcardpile)** - Draw, discard, exhaust piles
- **[LrgHand](cards.md#lrghand)** - Hand management
- **[LrgZone](cards.md#lrgzone)** - Zone abstraction

### Deck Management

- **[LrgDeckDef](deck-building.md#lrgdeckdef)** - Deck template (derivable)
- **[LrgDeckInstance](deck-building.md#lrgdeckinstance)** - Runtime deck state
- **[LrgDeckBuilder](deck-building.md#lrgdeckbuilder)** - Deck construction/validation

### Effect System

- **[LrgCardEffect](effects.md#lrgcardeffect)** - Effect data container
- **[LrgCardEffectExecutor](effects.md#lrgcardeffectexecutor)** - Effect executor interface
- **[LrgCardEffectRegistry](effects.md#lrgcardeffectregistry)** - Effect registry
- **[LrgEffectStack](effects.md#lrgeffectstack)** - Effect resolution stack
- **[LrgEventBus](effects.md#lrgeventbus)** - Event/trigger system

### Keywords and Synergies

- **[LrgCardKeyword](keywords.md#lrgcardkeyword)** - Built-in keyword flags
- **[LrgCardKeywordDef](keywords.md#lrgcardkeyworddef)** - Custom keywords (derivable)
- **[LrgSynergy](keywords.md#lrgsynergy)** - Synergy definitions (derivable)

### Status Effects

- **[LrgStatusEffectDef](status-effects.md#lrgstatuseffectdef)** - Status definition (derivable)
- **[LrgStatusEffectInstance](status-effects.md#lrgstatuseffectinstance)** - Status runtime
- **[LrgStatusEffectRegistry](status-effects.md#lrgstatuseffectregistry)** - Status registry

### Relics and Potions

- **[LrgRelicDef](relics.md#lrgrelicdef)** - Relic definition (derivable)
- **[LrgRelicInstance](relics.md#lrgrelicinstance)** - Relic runtime
- **[LrgPotionDef](relics.md#lrgpotiondef)** - Potion definition (derivable)
- **[LrgPotionInstance](relics.md#lrgpotioninstance)** - Potion runtime

### Combat System

- **[LrgCombatant](combat.md#lrgcombatant)** - Combatant interface
- **[LrgCombatRules](combat.md#lrgcombatrules)** - Combat rules interface
- **[LrgPlayerCombatant](combat.md#lrgplayercombatant)** - Player implementation
- **[LrgEnemyDef](combat.md#lrgenemydef)** - Enemy definition (derivable)
- **[LrgEnemyInstance](combat.md#lrgenemyinstance)** - Enemy runtime
- **[LrgCombatContext](combat.md#lrgcombatcontext)** - Combat state
- **[LrgCombatManager](combat.md#lrgcombatmanager)** - Combat flow controller

### Run/Map System

- **[LrgRun](run-structure.md#lrgrun)** - Run state container
- **[LrgRunConfig](run-structure.md#lrgrunconfig)** - Run configuration (derivable)
- **[LrgMapNode](run-structure.md#lrgmapnode)** - Map node (derivable)
- **[LrgMapGenerator](run-structure.md#lrgmapgenerator)** - Procedural map generation
- **[LrgCardPool](run-structure.md#lrgcardpool)** - Weighted card selection
- **[LrgShop](run-structure.md#lrgshop)** - Shop system
- **[LrgEventDef](run-structure.md#lrgeventdef)** - Random events (derivable)

### Scoring System (Balatro-style)

- **[LrgScoringHand](scoring.md#lrgscoringhand)** - Hand evaluation (derivable)
- **[LrgScoringContext](scoring.md#lrgscoringcontext)** - Scoring state
- **[LrgScoringRules](scoring.md#lrgscoringrules)** - Scoring rules interface
- **[LrgJokerDef](scoring.md#lrgjokerdef)** - Joker definition (derivable)
- **[LrgJokerInstance](scoring.md#lrgjokerinstance)** - Joker runtime
- **[LrgScoringManager](scoring.md#lrgscoringmanager)** - Scoring flow controller

### Meta-Progression

- **[LrgCharacterDef](meta-progression.md#lrgcharacterdef)** - Character definition (derivable)
- **[LrgPlayerProfile](meta-progression.md#lrgplayerprofile)** - Persistent save data
- **[LrgUnlockDef](meta-progression.md#lrgunlockdef)** - Unlock definition (derivable)
- **[LrgAscension](meta-progression.md#lrgascension)** - Challenge modifiers
- **[LrgDeckbuilderManager](meta-progression.md#lrgdeckbuildermanager)** - Singleton coordinator

## Quick Start

### Combat Deckbuilder (Slay the Spire style)

```c
/* Get the deckbuilder manager */
LrgDeckbuilderManager *manager = lrg_deckbuilder_manager_get_default ();

/* Register a character */
g_autoptr(LrgCharacterDef) ironclad = lrg_character_def_new ("ironclad");
lrg_character_def_set_name (ironclad, "The Ironclad");
lrg_character_def_set_starting_health (ironclad, 80);
lrg_character_def_set_starting_gold (ironclad, 99);
lrg_deckbuilder_manager_register_character (manager, ironclad);

/* Start a run */
LrgRun *run = lrg_deckbuilder_manager_start_run (manager, "ironclad", 0, NULL);

/* Get current map node and enter combat */
LrgMapNode *node = lrg_run_get_current_node (run);
if (lrg_map_node_get_node_type (node) == LRG_MAP_NODE_COMBAT)
{
    LrgCombatManager *combat = lrg_combat_manager_get_default ();
    lrg_combat_manager_start_combat (combat, run);

    /* Game loop */
    while (lrg_combat_manager_get_phase (combat) != LRG_COMBAT_PHASE_FINISHED)
    {
        /* Draw cards, play cards, end turn */
        LrgHand *hand = lrg_combat_manager_get_hand (combat);
        LrgCardInstance *card = lrg_hand_get_card (hand, 0);

        if (lrg_combat_manager_can_play_card (combat, card))
        {
            lrg_combat_manager_play_card (combat, card, target);
        }

        lrg_combat_manager_end_turn (combat);
    }
}
```

### Scoring Deckbuilder (Balatro style)

```c
/* Get the scoring manager */
LrgScoringManager *scoring = lrg_scoring_manager_get_default ();

/* Start a scoring round */
lrg_scoring_manager_start_round (scoring, run);

/* Select cards from hand to play */
LrgHand *hand = lrg_scoring_manager_get_hand (scoring);
GPtrArray *selected = g_ptr_array_new ();
g_ptr_array_add (selected, lrg_hand_get_card (hand, 0));
g_ptr_array_add (selected, lrg_hand_get_card (hand, 2));
g_ptr_array_add (selected, lrg_hand_get_card (hand, 4));

/* Play the hand */
LrgScoringContext *ctx = lrg_scoring_manager_play_hand (scoring, selected);

/* Get results */
LrgHandType hand_type = lrg_scoring_context_get_hand_type (ctx);
gdouble chips = lrg_scoring_context_get_final_chips (ctx);
gdouble mult = lrg_scoring_context_get_final_mult (ctx);
gint64 score = lrg_scoring_context_get_final_score (ctx);

g_print ("Scored %s for %ld points!\n",
         lrg_hand_type_get_name (hand_type), score);

g_ptr_array_unref (selected);
```

## Card Types

Cards are categorized by type:

| Type | Description |
|------|-------------|
| `LRG_CARD_TYPE_ATTACK` | Offensive cards that deal damage |
| `LRG_CARD_TYPE_SKILL` | Utility cards (block, draw, etc.) |
| `LRG_CARD_TYPE_POWER` | Persistent effects for the combat |
| `LRG_CARD_TYPE_STATUS` | Negative cards added by enemies |
| `LRG_CARD_TYPE_CURSE` | Permanent negative cards |

## Card Rarities

| Rarity | Description |
|--------|-------------|
| `LRG_CARD_RARITY_STARTER` | Starting deck cards |
| `LRG_CARD_RARITY_COMMON` | Frequently offered cards |
| `LRG_CARD_RARITY_UNCOMMON` | Less common cards |
| `LRG_CARD_RARITY_RARE` | Powerful rare cards |
| `LRG_CARD_RARITY_SPECIAL` | Event/shop exclusive cards |

## Built-in Keywords

| Keyword | Effect |
|---------|--------|
| `INNATE` | Always drawn on first turn |
| `ETHEREAL` | Exhausts if not played |
| `RETAIN` | Keeps in hand between turns |
| `EXHAUST` | Removed from deck when played |
| `UNPLAYABLE` | Cannot be played normally |
| `X_COST` | Uses all available energy |
| `AUTOPLAY` | Plays automatically when drawn |

## Built-in Status Effects

| Status | Type | Effect |
|--------|------|--------|
| Strength | Buff | +X damage dealt |
| Dexterity | Buff | +X block gained |
| Vulnerable | Debuff | Take 50% more damage |
| Weak | Debuff | Deal 25% less damage |
| Frail | Debuff | Gain 25% less block |
| Poison | Debuff | Take X damage at turn end, -1 |
| Thorns | Buff | Deal X damage when hit |
| Intangible | Buff | Reduce all damage to 1 |
| Artifact | Buff | Block next N debuffs |

## Scoring Hand Types

| Hand Type | Base Chips | Base Mult |
|-----------|------------|-----------|
| High Card | 5 | 1 |
| Pair | 10 | 2 |
| Two Pair | 20 | 2 |
| Three of a Kind | 30 | 3 |
| Straight | 30 | 4 |
| Flush | 35 | 4 |
| Full House | 40 | 4 |
| Four of a Kind | 60 | 7 |
| Straight Flush | 100 | 8 |
| Royal Flush | 100 | 8 |
| Five of a Kind | 120 | 12 |

## Ascension Modifiers (A1-A20)

Each ascension level adds cumulative difficulty:

| Level | Modifier |
|-------|----------|
| A1 | Elites have harder patterns |
| A2 | Start with Ascender's Bane curse |
| A3 | Less healing from rest sites |
| A4 | Reduced starting gold |
| A5 | Heal less between acts |
| A6 | Start with less max HP |
| A7 | Elites have more HP |
| A10 | Bosses have harder patterns |
| A15 | Bosses have more HP |
| A17 | Less healing from all sources |
| A18 | Start with even less max HP |
| A20 | Enemies do more damage |

## Integration with Other Modules

The Deckbuilder module integrates with:

- **Economy Module**: Energy as `LrgResource` via `LrgResourcePool`
- **Save Module**: Full run state persistence via `LrgSaveable`
- **Mod Module**: Content provision via `LrgCardProvider`
- **Data Loader**: Load definitions from YAML
- **UI Module**: Display cards, hand, combat state

## See Also

- [Cards Documentation](cards.md)
- [Effects Documentation](effects.md)
- [Keywords Documentation](keywords.md)
- [Status Effects Documentation](status-effects.md)
- [Relics Documentation](relics.md)
- [Combat Documentation](combat.md)
- [Run Structure Documentation](run-structure.md)
- [Scoring Documentation](scoring.md)
- [Meta-Progression Documentation](meta-progression.md)
- [Deck Building Documentation](deck-building.md)
- [Modding Documentation](modding.md)
