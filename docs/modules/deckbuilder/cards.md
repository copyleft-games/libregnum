# Cards

The card system provides the foundation for deckbuilding games. It includes card definitions (blueprints), card instances (runtime), piles (draw/discard/exhaust), hand management, and zone tracking.

## LrgCardDef

Card definitions are derivable GObjects that define the blueprint for card types. They specify base properties shared by all cards of that type.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique card identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Card description with placeholders |
| `card-type` | `LrgCardType` | Attack, Skill, Power, Status, Curse |
| `rarity` | `LrgCardRarity` | Starter, Common, Uncommon, Rare, Special |
| `base-cost` | `gint` | Base energy cost |
| `target-type` | `LrgCardTargetType` | Targeting mode |
| `keywords` | `LrgCardKeyword` | Keyword flags |
| `upgradeable` | `gboolean` | Can be upgraded |
| `max-copies` | `gint` | Maximum copies in deck |

### Virtual Methods

```c
struct _LrgCardDefClass
{
    GObjectClass parent_class;

    /* Called when card is played */
    gboolean (* on_play)       (LrgCardDef      *self,
                                LrgCombatContext *ctx,
                                LrgCombatant     *target);

    /* Called when card is discarded */
    gboolean (* on_discard)    (LrgCardDef      *self,
                                LrgCombatContext *ctx);

    /* Called when card is exhausted */
    gboolean (* on_exhaust)    (LrgCardDef      *self,
                                LrgCombatContext *ctx);

    /* Called when card is drawn */
    gboolean (* on_draw)       (LrgCardDef      *self,
                                LrgCombatContext *ctx);

    /* Check if card can be played */
    gboolean (* can_play)      (LrgCardDef      *self,
                                LrgCombatContext *ctx);

    /* Calculate current cost (with modifiers) */
    gint     (* calculate_cost)(LrgCardDef      *self,
                                LrgCombatContext *ctx);

    /* Generate tooltip text */
    gchar *  (* get_tooltip)   (LrgCardDef      *self,
                                LrgCombatContext *ctx);

    gpointer _reserved[8];
};
```

### Creating Cards

```c
/* Create a basic attack card */
g_autoptr(LrgCardDef) strike = lrg_card_def_new ("strike");
lrg_card_def_set_name (strike, "Strike");
lrg_card_def_set_description (strike, "Deal {damage} damage.");
lrg_card_def_set_card_type (strike, LRG_CARD_TYPE_ATTACK);
lrg_card_def_set_rarity (strike, LRG_CARD_RARITY_STARTER);
lrg_card_def_set_base_cost (strike, 1);
lrg_card_def_set_target_type (strike, LRG_CARD_TARGET_SINGLE_ENEMY);

/* Add effects */
LrgCardEffect *effect = lrg_card_effect_new ("damage");
lrg_card_effect_set_param_int (effect, "amount", 6);
lrg_card_def_add_effect (strike, effect);
```

### Card Tags

Cards support arbitrary tags for synergy matching:

```c
lrg_card_def_add_tag (strike, "basic");
lrg_card_def_add_tag (strike, "attack");
lrg_card_def_add_tag (fireball, "fire");
lrg_card_def_add_tag (fireball, "magic");

/* Check tags */
if (lrg_card_def_has_tag (card, "fire"))
{
    /* Apply fire synergy bonus */
}
```

### Custom Properties

```c
/* Set custom properties */
lrg_card_def_set_property_int (card, "damage", 6);
lrg_card_def_set_property_int (card, "block", 5);
lrg_card_def_set_property_string (card, "element", "fire");

/* Get properties */
gint damage = lrg_card_def_get_property_int (card, "damage");
```

## LrgCardInstance

Card instances represent actual cards in play. They wrap a card definition with instance-specific state like upgrade tier and temporary modifiers.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `definition` | `LrgCardDef*` | The card definition |
| `upgrade-tier` | `LrgCardUpgradeTier` | Current upgrade level |
| `cost-modifier` | `gint` | Temporary cost adjustment |
| `uuid` | `gchar*` | Unique instance identifier |

### Creating Instances

```c
/* Create card instance from definition */
g_autoptr(LrgCardInstance) card = lrg_card_instance_new (strike_def);

/* Upgrade the card */
lrg_card_instance_upgrade (card);

/* Get effective cost */
gint cost = lrg_card_instance_get_effective_cost (card, combat_ctx);

/* Apply temporary cost modifier */
lrg_card_instance_add_cost_modifier (card, -1);  /* Reduce by 1 */
```

### Upgrade Tiers

| Tier | Suffix | Description |
|------|--------|-------------|
| `BASE` | (none) | Unupgraded card |
| `PLUS` | + | First upgrade |
| `PLUS_PLUS` | ++ | Second upgrade (if supported) |
| `ULTIMATE` | * | Maximum upgrade |

```c
/* Check if upgradeable */
if (lrg_card_instance_can_upgrade (card))
{
    lrg_card_instance_upgrade (card);
}

/* Get upgrade tier */
LrgCardUpgradeTier tier = lrg_card_instance_get_upgrade_tier (card);
```

## LrgCardPile

Card piles are ordered collections of cards. They're used for draw pile, discard pile, exhaust pile, etc.

### Operations

```c
/* Create pile */
g_autoptr(LrgCardPile) draw_pile = lrg_card_pile_new ();

/* Add cards */
lrg_card_pile_add (draw_pile, card, LRG_PILE_POSITION_TOP);
lrg_card_pile_add (draw_pile, card2, LRG_PILE_POSITION_BOTTOM);
lrg_card_pile_add (draw_pile, card3, LRG_PILE_POSITION_RANDOM);

/* Draw from top */
LrgCardInstance *drawn = lrg_card_pile_draw (draw_pile);

/* Draw specific card */
LrgCardInstance *specific = lrg_card_pile_draw_card (draw_pile, card_uuid);

/* Shuffle */
lrg_card_pile_shuffle (draw_pile, rng);

/* Get size */
guint count = lrg_card_pile_get_count (draw_pile);

/* Iterate */
for (guint i = 0; i < lrg_card_pile_get_count (pile); i++)
{
    LrgCardInstance *card = lrg_card_pile_peek (pile, i);
    /* ... */
}
```

### Pile Positions

| Position | Description |
|----------|-------------|
| `TOP` | Add to top of pile |
| `BOTTOM` | Add to bottom of pile |
| `RANDOM` | Insert at random position |

## LrgHand

Hand manages the cards currently held by the player.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `max-size` | `gint` | Maximum hand size |
| `cards` | `GPtrArray*` | Cards in hand |

### Operations

```c
/* Create hand */
g_autoptr(LrgHand) hand = lrg_hand_new (10);  /* Max 10 cards */

/* Add card */
gboolean added = lrg_hand_add_card (hand, card);

/* Get card */
LrgCardInstance *card = lrg_hand_get_card (hand, index);

/* Remove card */
LrgCardInstance *removed = lrg_hand_remove_card (hand, index);

/* Find card by UUID */
LrgCardInstance *found = lrg_hand_find_card (hand, uuid);

/* Check size */
guint count = lrg_hand_get_count (hand);
gboolean full = lrg_hand_is_full (hand);

/* Discard all */
GPtrArray *discarded = lrg_hand_discard_all (hand);
```

### Signals

| Signal | Description |
|--------|-------------|
| `card-added` | Card was added to hand |
| `card-removed` | Card was removed from hand |
| `hand-full` | Hand reached maximum size |

## LrgZone

Zones track which pile/area a card is currently in.

### Zone Types

| Zone | Description |
|------|-------------|
| `DRAW` | Draw pile |
| `HAND` | Player's hand |
| `DISCARD` | Discard pile |
| `EXHAUST` | Exhausted cards |
| `PLAYED` | Currently being played |
| `LIMBO` | Temporary holding area |

### Zone Tracking

```c
/* Get card's current zone */
LrgCardZone zone = lrg_card_instance_get_zone (card);

/* Move card between zones */
lrg_card_instance_set_zone (card, LRG_ZONE_DISCARD);

/* Zone manager tracks all cards */
LrgZone *zone_mgr = lrg_combat_context_get_zone (ctx);
GPtrArray *exhausted = lrg_zone_get_cards_in_zone (zone_mgr, LRG_ZONE_EXHAUST);
```

## YAML Definition

Cards can be loaded from YAML:

```yaml
type: card-def
id: "strike"
name: "Strike"
description: "Deal {damage} damage."
card-type: attack
rarity: starter
base-cost: 1
target-type: single-enemy
upgradeable: true
upgraded-version-id: "strike+"
icon: "cards/strike.png"
effects:
  - type: damage
    target: single-enemy
    params:
      amount: 6
keywords: []
tags:
  - basic
  - starter
```

## Custom Card Subclass

Create custom card types with behavior:

```c
/* Define custom card */
G_DECLARE_FINAL_TYPE (MyCustomCard, my_custom_card, MY, CUSTOM_CARD, LrgCardDef)

struct _MyCustomCard
{
    LrgCardDef parent_instance;
    gint bonus_damage;
};

static gboolean
my_custom_card_on_play (LrgCardDef      *def,
                        LrgCombatContext *ctx,
                        LrgCombatant     *target)
{
    MyCustomCard *self = MY_CUSTOM_CARD (def);
    gint damage = lrg_card_def_get_property_int (def, "damage");
    damage += self->bonus_damage;

    lrg_combat_context_deal_damage (ctx, target, damage);
    return TRUE;
}

static void
my_custom_card_class_init (MyCustomCardClass *klass)
{
    LrgCardDefClass *card_class = LRG_CARD_DEF_CLASS (klass);
    card_class->on_play = my_custom_card_on_play;
}
```

## See Also

- [Effects Documentation](effects.md)
- [Keywords Documentation](keywords.md)
- [Deck Building Documentation](deck-building.md)
