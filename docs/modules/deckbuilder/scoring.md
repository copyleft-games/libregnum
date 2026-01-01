# Scoring System

The scoring system provides Balatro-style poker hand evaluation with chips, multipliers, and joker modifiers. It supports custom hand types, card enhancements, and complex scoring chains.

## Overview

Scoring deckbuilders work differently from combat deckbuilders:

1. Player is dealt a hand of playing cards
2. Player selects cards to form a poker hand
3. Hand is evaluated for chips and multiplier
4. Jokers modify the score
5. Final score = chips x mult
6. Score must meet or exceed target to win round

## LrgScoringHand

Scoring hand definitions define how hands are evaluated.

### Hand Types

```c
typedef enum {
    LRG_HAND_TYPE_NONE,
    LRG_HAND_TYPE_HIGH_CARD,
    LRG_HAND_TYPE_PAIR,
    LRG_HAND_TYPE_TWO_PAIR,
    LRG_HAND_TYPE_THREE_OF_A_KIND,
    LRG_HAND_TYPE_STRAIGHT,
    LRG_HAND_TYPE_FLUSH,
    LRG_HAND_TYPE_FULL_HOUSE,
    LRG_HAND_TYPE_FOUR_OF_A_KIND,
    LRG_HAND_TYPE_STRAIGHT_FLUSH,
    LRG_HAND_TYPE_ROYAL_FLUSH,
    LRG_HAND_TYPE_FIVE_OF_A_KIND,
    LRG_HAND_TYPE_CUSTOM = 100
} LrgHandType;
```

### Base Values

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

### Virtual Methods

```c
struct _LrgScoringHandClass
{
    GObjectClass parent_class;

    /* Evaluate cards and return hand type */
    LrgHandType (*evaluate) (LrgScoringHand *self,
                             GPtrArray      *cards);

    /* Get scoring cards from the hand */
    GPtrArray * (*get_scoring_cards) (LrgScoringHand *self,
                                       GPtrArray      *cards);

    /* Get base chips for this hand type */
    gint (*get_base_chips) (LrgScoringHand *self);

    /* Get base mult for this hand type */
    gdouble (*get_base_mult) (LrgScoringHand *self);

    gpointer _reserved[8];
};
```

## Card Suits and Ranks

### Suits

```c
typedef enum {
    LRG_CARD_SUIT_NONE,
    LRG_CARD_SUIT_HEARTS,
    LRG_CARD_SUIT_DIAMONDS,
    LRG_CARD_SUIT_CLUBS,
    LRG_CARD_SUIT_SPADES,
    LRG_CARD_SUIT_WILD,        /* Counts as any suit */
    LRG_CARD_SUIT_CUSTOM = 100
} LrgCardSuit;
```

### Ranks

```c
typedef enum {
    LRG_CARD_RANK_NONE,
    LRG_CARD_RANK_ACE = 1,
    LRG_CARD_RANK_TWO = 2,
    LRG_CARD_RANK_THREE = 3,
    LRG_CARD_RANK_FOUR = 4,
    LRG_CARD_RANK_FIVE = 5,
    LRG_CARD_RANK_SIX = 6,
    LRG_CARD_RANK_SEVEN = 7,
    LRG_CARD_RANK_EIGHT = 8,
    LRG_CARD_RANK_NINE = 9,
    LRG_CARD_RANK_TEN = 10,
    LRG_CARD_RANK_JACK = 11,
    LRG_CARD_RANK_QUEEN = 12,
    LRG_CARD_RANK_KING = 13,
    LRG_CARD_RANK_WILD = 14     /* Counts as any rank */
} LrgCardRank;
```

### Chip Values by Rank

| Rank | Chips |
|------|-------|
| 2-10 | Face value |
| Jack | 10 |
| Queen | 10 |
| King | 10 |
| Ace | 11 |

## Card Enhancements

### Enhancement Types

```c
typedef enum {
    LRG_CARD_ENHANCEMENT_NONE,
    LRG_CARD_ENHANCEMENT_BONUS,   /* +30 chips */
    LRG_CARD_ENHANCEMENT_MULT,    /* +4 mult */
    LRG_CARD_ENHANCEMENT_WILD,    /* Counts as any suit */
    LRG_CARD_ENHANCEMENT_GLASS,   /* x2 mult, may break */
    LRG_CARD_ENHANCEMENT_STEEL,   /* x1.5 mult while in hand */
    LRG_CARD_ENHANCEMENT_STONE,   /* +50 chips, no rank/suit */
    LRG_CARD_ENHANCEMENT_GOLD,    /* +$3 when held at round end */
    LRG_CARD_ENHANCEMENT_LUCKY    /* 1/5 chance +20 mult, 1/15 chance +$20 */
} LrgCardEnhancement;
```

### Card Seals

```c
typedef enum {
    LRG_CARD_SEAL_NONE,
    LRG_CARD_SEAL_GOLD,    /* +$3 when played */
    LRG_CARD_SEAL_RED,     /* Retrigger card */
    LRG_CARD_SEAL_BLUE,    /* Create Planet card when held */
    LRG_CARD_SEAL_PURPLE   /* Create Tarot card when discarded */
} LrgCardSeal;
```

### Card Editions

```c
typedef enum {
    LRG_CARD_EDITION_BASE,
    LRG_CARD_EDITION_FOIL,         /* +50 chips */
    LRG_CARD_EDITION_HOLOGRAPHIC,  /* +10 mult */
    LRG_CARD_EDITION_POLYCHROME    /* x1.5 mult */
} LrgCardEdition;
```

## LrgScoringContext

The scoring context holds the state of a scoring evaluation.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `played-cards` | `GPtrArray*` | Cards played this hand |
| `held-cards` | `GPtrArray*` | Cards held in hand |
| `hand-type` | `LrgHandType` | Evaluated hand type |
| `scoring-cards` | `GPtrArray*` | Cards that contribute to score |
| `chips` | `gdouble` | Current chip total |
| `mult` | `gdouble` | Current multiplier |
| `final-score` | `gint64` | Final calculated score |

### Scoring Flow

```c
/* Create context */
LrgScoringContext *ctx = lrg_scoring_context_new ();
lrg_scoring_context_set_played_cards (ctx, selected);
lrg_scoring_context_set_held_cards (ctx, hand);

/* Evaluate hand */
LrgScoringRules *rules = lrg_scoring_rules_get_default ();
LrgHandType hand_type = lrg_scoring_rules_evaluate_hand (rules, selected);
lrg_scoring_context_set_hand_type (ctx, hand_type);

/* Get base chips/mult */
gdouble chips = lrg_scoring_rules_get_base_chips (rules, hand_type);
gdouble mult = lrg_scoring_rules_get_base_mult (rules, hand_type);

/* Add chips from scoring cards */
GPtrArray *scoring = lrg_scoring_rules_get_scoring_cards (rules, selected);
for (guint i = 0; i < scoring->len; i++)
{
    LrgCardInstance *card = g_ptr_array_index (scoring, i);
    chips += lrg_card_instance_get_chip_value (card);
}

/* Apply joker modifiers */
GPtrArray *jokers = lrg_run_get_jokers (run);
for (guint i = 0; i < jokers->len; i++)
{
    LrgJokerInstance *joker = g_ptr_array_index (jokers, i);
    chips = lrg_joker_instance_modify_chips (joker, ctx, chips);
    mult = lrg_joker_instance_modify_mult (joker, ctx, mult);
}

/* Calculate final score */
gint64 score = (gint64)(chips * mult);
lrg_scoring_context_set_final_score (ctx, score);
```

## LrgJokerDef

Jokers are special cards that modify scoring.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique joker identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Effect description |
| `rarity` | `LrgJokerRarity` | Joker rarity |
| `base-sell-value` | `gint` | Base sell price |

### Joker Rarities

```c
typedef enum {
    LRG_JOKER_RARITY_COMMON,
    LRG_JOKER_RARITY_UNCOMMON,
    LRG_JOKER_RARITY_RARE,
    LRG_JOKER_RARITY_LEGENDARY
} LrgJokerRarity;
```

### Joker Editions

```c
typedef enum {
    LRG_JOKER_EDITION_BASE,
    LRG_JOKER_EDITION_FOIL,         /* +50 chips */
    LRG_JOKER_EDITION_HOLOGRAPHIC,  /* +10 mult */
    LRG_JOKER_EDITION_POLYCHROME,   /* x1.5 mult */
    LRG_JOKER_EDITION_NEGATIVE      /* +1 joker slot */
} LrgJokerEdition;
```

### Virtual Methods

```c
struct _LrgJokerDefClass
{
    GObjectClass parent_class;

    /* Called at start of scoring */
    void (*on_score_start)  (LrgJokerDef      *self,
                             LrgScoringContext *ctx);

    /* Modify chip total */
    gdouble (*modify_chips) (LrgJokerDef      *self,
                             LrgScoringContext *ctx,
                             gdouble           chips);

    /* Modify mult total */
    gdouble (*modify_mult)  (LrgJokerDef      *self,
                             LrgScoringContext *ctx,
                             gdouble           mult);

    /* Called at end of scoring */
    void (*on_score_end)    (LrgJokerDef      *self,
                             LrgScoringContext *ctx);

    /* Per-card chip bonus */
    gdouble (*get_card_chips) (LrgJokerDef      *self,
                               LrgCardInstance  *card,
                               LrgScoringContext *ctx);

    /* Per-card mult bonus */
    gdouble (*get_card_mult) (LrgJokerDef      *self,
                              LrgCardInstance  *card,
                              LrgScoringContext *ctx);

    /* Should this card retrigger? */
    gboolean (*retrigger_card) (LrgJokerDef      *self,
                                LrgCardInstance  *card,
                                LrgScoringContext *ctx);

    /* Called when card is played */
    void (*on_played)       (LrgJokerDef      *self,
                             LrgScoringContext *ctx,
                             LrgCardInstance  *card);

    /* Called for held cards */
    void (*on_held)         (LrgJokerDef      *self,
                             LrgScoringContext *ctx,
                             LrgCardInstance  *card);

    /* Called when card is discarded */
    void (*on_discarded)    (LrgJokerDef      *self,
                             LrgScoringContext *ctx,
                             LrgCardInstance  *card);

    gpointer _reserved[8];
};
```

## Built-in Jokers

### Mult Joker (+4 Mult)

```c
static gdouble
mult_joker_modify_mult (LrgJokerDef      *self,
                        LrgScoringContext *ctx,
                        gdouble           mult)
{
    return mult + 4.0;
}
```

### Chips Joker (+50 Chips)

```c
static gdouble
chips_joker_modify_chips (LrgJokerDef      *self,
                          LrgScoringContext *ctx,
                          gdouble           chips)
{
    return chips + 50.0;
}
```

### Retrigger Joker (Retrigger Aces)

```c
static gboolean
retrigger_joker_retrigger_card (LrgJokerDef      *self,
                                 LrgCardInstance  *card,
                                 LrgScoringContext *ctx)
{
    return lrg_card_instance_get_rank (card) == LRG_CARD_RANK_ACE;
}
```

### Greedy Joker (+3 Mult per Diamond)

```c
static gdouble
greedy_joker_get_card_mult (LrgJokerDef      *self,
                            LrgCardInstance  *card,
                            LrgScoringContext *ctx)
{
    if (lrg_card_instance_get_suit (card) == LRG_CARD_SUIT_DIAMONDS)
        return 3.0;
    return 0.0;
}
```

## LrgScoringManager

The scoring manager controls the scoring game flow.

### Operations

```c
/* Get manager */
LrgScoringManager *scoring = lrg_scoring_manager_get_default ();

/* Start round */
lrg_scoring_manager_start_round (scoring, run);

/* Get current state */
LrgHand *hand = lrg_scoring_manager_get_hand (scoring);
gint plays_remaining = lrg_scoring_manager_get_plays_remaining (scoring);
gint discards_remaining = lrg_scoring_manager_get_discards_remaining (scoring);
gint64 target_score = lrg_scoring_manager_get_target_score (scoring);
gint64 current_score = lrg_scoring_manager_get_current_score (scoring);

/* Select cards */
lrg_scoring_manager_select_card (scoring, card);
lrg_scoring_manager_deselect_card (scoring, card);

/* Play selected hand */
LrgScoringContext *ctx = lrg_scoring_manager_play_hand (scoring);

/* Discard selected cards */
lrg_scoring_manager_discard_selected (scoring);

/* Check round end */
if (lrg_scoring_manager_is_round_complete (scoring))
{
    gboolean won = lrg_scoring_manager_get_current_score (scoring) >=
                   lrg_scoring_manager_get_target_score (scoring);
}
```

### Signals

| Signal | Description |
|--------|-------------|
| `round-started` | Round has begun |
| `round-ended` | Round has ended |
| `hand-played` | Hand was played |
| `cards-discarded` | Cards were discarded |
| `score-changed` | Score was updated |

## Custom Hand Types

```c
/* Define custom "Flush House" hand (full house + flush) */
G_DECLARE_FINAL_TYPE (FlushHouse, flush_house, MY, FLUSH_HOUSE, LrgScoringHand)

static LrgHandType
flush_house_evaluate (LrgScoringHand *hand, GPtrArray *cards)
{
    /* Check for full house AND flush */
    if (is_full_house (cards) && is_flush (cards))
    {
        return LRG_HAND_TYPE_CUSTOM;
    }
    return LRG_HAND_TYPE_NONE;
}

static gint
flush_house_get_base_chips (LrgScoringHand *hand)
{
    return 140;  /* Higher than regular full house */
}

static gdouble
flush_house_get_base_mult (LrgScoringHand *hand)
{
    return 14.0;
}
```

## See Also

- [Cards Documentation](cards.md)
- [Run Structure Documentation](run-structure.md)
- [Meta-Progression Documentation](meta-progression.md)
