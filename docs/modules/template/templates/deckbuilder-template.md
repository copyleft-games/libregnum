# LrgDeckbuilderTemplate

`LrgDeckbuilderTemplate` is a game template specialized for deckbuilder games. It provides deck management, turn structure, card play mechanics, and energy systems.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgDeckbuilderTemplate (derivable)
    ├── LrgDeckbuilderCombatTemplate (Slay the Spire-style)
    └── LrgDeckbuilderPokerTemplate (Balatro-style)
```

## Features

- Complete deck management (draw pile, discard pile, exhaust pile, hand)
- Turn structure with start/end turn hooks
- Energy system with cost evaluation
- Card play with target selection
- Card add/remove from deck
- Integration with `LrgDeckMixin` for composition

## Quick Start

```c
#define MY_TYPE_DECKBUILDER (my_deckbuilder_get_type ())
G_DECLARE_FINAL_TYPE (MyDeckbuilder, my_deckbuilder, MY, DECKBUILDER, LrgDeckbuilderTemplate)

struct _MyDeckbuilder
{
    LrgDeckbuilderTemplate parent_instance;
};

G_DEFINE_TYPE (MyDeckbuilder, my_deckbuilder, LRG_TYPE_DECKBUILDER_TEMPLATE)

static void
my_deckbuilder_configure (LrgGameTemplate *template)
{
    LrgDeckbuilderTemplate *deck = LRG_DECKBUILDER_TEMPLATE (template);

    g_object_set (template,
                  "title", "Card Combat",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);

    /* Configure deck settings */
    lrg_deckbuilder_template_set_max_energy (deck, 3);
    lrg_deckbuilder_template_set_base_hand_size (deck, 5);
}

static LrgDeckDef *
my_deckbuilder_create_deck_def (LrgDeckbuilderTemplate *template)
{
    LrgDeckDef *def = lrg_deck_def_new ("Starter Deck");

    /* Add starting cards */
    for (gint i = 0; i < 5; i++)
        lrg_deck_def_add_card (def, strike_card_def);
    for (gint i = 0; i < 5; i++)
        lrg_deck_def_add_card (def, defend_card_def);

    return def;
}

static gboolean
my_deckbuilder_on_card_played (LrgDeckbuilderTemplate *template,
                                 LrgCardInstance        *card,
                                 gpointer                target)
{
    MyDeckbuilder *self = MY_DECKBUILDER (template);

    /* Execute card effect based on card type */
    const gchar *card_id = lrg_card_instance_get_id (card);

    if (g_str_equal (card_id, "strike"))
    {
        deal_damage_to_enemy (target, 6);
    }
    else if (g_str_equal (card_id, "defend"))
    {
        add_block_to_player (5);
    }

    return TRUE;  /* Card was successfully played */
}

static void
my_deckbuilder_class_init (MyDeckbuilderClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgDeckbuilderTemplateClass *deck_class = LRG_DECKBUILDER_TEMPLATE_CLASS (klass);

    template_class->configure = my_deckbuilder_configure;
    deck_class->create_deck_def = my_deckbuilder_create_deck_def;
    deck_class->on_card_played = my_deckbuilder_on_card_played;
}
```

## Virtual Methods

```c
/* Creates the default deck definition for new games */
LrgDeckDef * (*create_deck_def) (LrgDeckbuilderTemplate *self);

/* Creates a deck instance from a definition */
LrgDeckInstance * (*create_deck_instance) (LrgDeckbuilderTemplate *self,
                                            LrgDeckDef             *def);

/* Called when a card is played - implement card effects here */
gboolean (*on_card_played) (LrgDeckbuilderTemplate *self,
                             LrgCardInstance        *card,
                             gpointer                target);

/* Calculate the effective cost to play a card */
gint (*evaluate_card_cost) (LrgDeckbuilderTemplate *self,
                            LrgCardInstance        *card);

/* Check if a card can be played */
gboolean (*can_play_card) (LrgDeckbuilderTemplate *self,
                           LrgCardInstance        *card,
                           gpointer                target);

/* Called at the start of each turn */
void (*start_turn) (LrgDeckbuilderTemplate *self, guint turn_number);

/* Called at the end of each turn */
void (*end_turn) (LrgDeckbuilderTemplate *self, guint turn_number);

/* Get energy at turn start */
gint (*get_starting_energy) (LrgDeckbuilderTemplate *self);

/* Get number of cards to draw at turn start */
guint (*get_cards_to_draw) (LrgDeckbuilderTemplate *self);
```

## Deck Management

### Access Deck

```c
LrgDeckInstance *deck = lrg_deckbuilder_template_get_deck_instance (template);

/* Set a different deck */
lrg_deckbuilder_template_set_deck_instance (template, new_deck);
```

### Draw Cards

```c
/* Draw cards from draw pile to hand */
guint drawn = lrg_deckbuilder_template_draw_cards (template, 5);

/* Note: If draw pile is empty, discard pile is shuffled into draw pile */
```

### Add/Remove Cards

```c
/* Add a new card to the master deck */
lrg_deckbuilder_template_add_card_to_deck (template, power_card_def);

/* Remove a specific card instance */
gboolean removed = lrg_deckbuilder_template_remove_card_from_deck (template, card);
```

## Turn Management

### Turn Flow

```c
/* Start a new turn (draws cards, resets energy) */
lrg_deckbuilder_template_start_turn (template);

/* End current turn (discards hand, triggers end-turn effects) */
lrg_deckbuilder_template_end_turn (template);

/* Check whose turn it is */
gboolean player_turn = lrg_deckbuilder_template_is_player_turn (template);

/* Get current turn number */
guint turn = lrg_deckbuilder_template_get_current_turn (template);
```

### Custom Turn Logic

```c
static void
my_deckbuilder_start_turn (LrgDeckbuilderTemplate *template, guint turn_number)
{
    MyDeckbuilder *self = MY_DECKBUILDER (template);

    /* Call parent to draw cards and reset energy */
    LRG_DECKBUILDER_TEMPLATE_CLASS (my_deckbuilder_parent_class)->start_turn (template, turn_number);

    /* Custom start-turn logic */
    trigger_start_of_turn_powers ();
    show_enemy_intent ();
}

static void
my_deckbuilder_end_turn (LrgDeckbuilderTemplate *template, guint turn_number)
{
    MyDeckbuilder *self = MY_DECKBUILDER (template);

    /* Call parent to discard hand */
    LRG_DECKBUILDER_TEMPLATE_CLASS (my_deckbuilder_parent_class)->end_turn (template, turn_number);

    /* Custom end-turn logic */
    trigger_end_of_turn_powers ();
    execute_enemy_attacks ();
}
```

## Playing Cards

### Basic Card Play

```c
/* Play a card from hand */
gboolean success = lrg_deckbuilder_template_play_card (template, card, target);

/* Play card at hand index */
gboolean success = lrg_deckbuilder_template_play_card_at (template, 0, target);
```

### Check Playability

```c
/* Check if card can be played */
gboolean can_play = lrg_deckbuilder_template_can_play_card (template, card);

/* Get effective card cost (after modifiers) */
gint cost = lrg_deckbuilder_template_get_card_cost (template, card);
```

### Cost Modification

```c
static gint
my_deckbuilder_evaluate_card_cost (LrgDeckbuilderTemplate *template,
                                    LrgCardInstance        *card)
{
    MyDeckbuilder *self = MY_DECKBUILDER (template);
    gint base_cost = lrg_card_instance_get_cost (card);

    /* Apply cost modifiers */
    if (self->has_cost_reduction_buff)
        base_cost -= 1;

    if (lrg_card_instance_has_tag (card, "skill") && self->skill_discount_active)
        base_cost -= 1;

    return MAX (0, base_cost);
}
```

## Energy System

```c
/* Get/set current energy */
gint energy = lrg_deckbuilder_template_get_current_energy (template);
lrg_deckbuilder_template_set_current_energy (template, 3);

/* Get/set max energy */
gint max = lrg_deckbuilder_template_get_max_energy (template);
lrg_deckbuilder_template_set_max_energy (template, 4);

/* Spend energy */
gboolean spent = lrg_deckbuilder_template_spend_energy (template, 2);

/* Gain energy */
lrg_deckbuilder_template_gain_energy (template, 1);

/* Reset to starting energy */
lrg_deckbuilder_template_reset_energy (template);
```

## Hand Size

```c
/* Get/set base hand size (cards drawn per turn) */
guint hand_size = lrg_deckbuilder_template_get_base_hand_size (template);
lrg_deckbuilder_template_set_base_hand_size (template, 6);
```

## Combat Variant (Slay the Spire-style)

`LrgDeckbuilderCombatTemplate` extends with combat mechanics:

```c
/* Player health */
lrg_deckbuilder_combat_template_set_player_health (template, 80);
lrg_deckbuilder_combat_template_set_player_max_health (template, 100);

/* Block system */
lrg_deckbuilder_combat_template_add_block (template, 10);
gint block = lrg_deckbuilder_combat_template_get_block (template);

/* Damage calculation */
lrg_deckbuilder_combat_template_deal_damage_to_enemy (template, enemy, 15);
lrg_deckbuilder_combat_template_deal_damage_to_player (template, 8);

/* Enemy management */
lrg_deckbuilder_combat_template_add_enemy (template, goblin);
lrg_deckbuilder_combat_template_remove_enemy (template, goblin);
GPtrArray *enemies = lrg_deckbuilder_combat_template_get_enemies (template);
```

## Poker Variant (Balatro-style)

`LrgDeckbuilderPokerTemplate` extends with poker mechanics:

```c
/* Hand evaluation */
LrgPokerHandType hand = lrg_deckbuilder_poker_template_evaluate_hand (template);
/* Returns: PAIR, TWO_PAIR, THREE_OF_A_KIND, STRAIGHT, FLUSH, etc. */

/* Scoring */
gint64 score = lrg_deckbuilder_poker_template_calculate_score (template);

/* Joker modifiers */
lrg_deckbuilder_poker_template_add_joker (template, joker);
GPtrArray *jokers = lrg_deckbuilder_poker_template_get_jokers (template);

/* Chip/mult system */
gint chips = lrg_deckbuilder_poker_template_get_chips (template);
gint mult = lrg_deckbuilder_poker_template_get_mult (template);
lrg_deckbuilder_poker_template_add_chips (template, 50);
lrg_deckbuilder_poker_template_add_mult (template, 4);
```

## LrgDeckMixin Interface

For composing deck mechanics into other templates:

```c
static LrgDeckInstance *
my_state_get_deck (LrgDeckMixin *mixin)
{
    MyState *self = MY_STATE (mixin);
    return self->current_deck;
}

static void
my_state_deck_mixin_init (LrgDeckMixinInterface *iface)
{
    iface->get_deck = my_state_get_deck;
    iface->on_card_drawn = my_state_on_card_drawn;
    iface->on_card_discarded = my_state_on_card_discarded;
}

G_DEFINE_TYPE_WITH_CODE (MyState, my_state, LRG_TYPE_GAME_STATE,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_DECK_MIXIN, my_state_deck_mixin_init))
```

## Related Documentation

- [LrgGameTemplate](game-template.md) - Base template features
- [Deckbuilder Example](../examples/deckbuilder.md) - Complete example
