# Deck Building

This document covers deck definitions, deck instances, and the deck builder utility for constructing and validating decks.

## LrgDeckDef

Deck definitions are templates that specify deck constraints and starting configuration.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique deck identifier |
| `name` | `gchar*` | Display name |
| `min-size` | `gint` | Minimum deck size |
| `max-size` | `gint` | Maximum deck size |
| `starting-hand-size` | `gint` | Cards drawn at turn start |
| `max-hand-size` | `gint` | Maximum hand size |
| `starting-energy` | `gint` | Energy per turn |

### Virtual Methods

```c
struct _LrgDeckDefClass
{
    GObjectClass parent_class;

    /* Validate a deck against this definition */
    gboolean (*validate) (LrgDeckDef    *self,
                          LrgDeckInstance *deck,
                          GError        **error);

    /* Get allowed card types for this deck */
    LrgCardType (*get_allowed_types) (LrgDeckDef *self);

    /* Check if a specific card can be added */
    gboolean (*can_add_card) (LrgDeckDef  *self,
                              LrgCardDef  *card,
                              LrgDeckInstance *deck);

    gpointer _reserved[8];
};
```

### Creating Deck Definitions

```c
/* Create deck template */
g_autoptr(LrgDeckDef) ironclad_deck = lrg_deck_def_new ("ironclad-starter");
lrg_deck_def_set_name (ironclad_deck, "Ironclad Starter Deck");
lrg_deck_def_set_min_size (ironclad_deck, 10);
lrg_deck_def_set_max_size (ironclad_deck, 40);
lrg_deck_def_set_starting_hand_size (ironclad_deck, 5);
lrg_deck_def_set_max_hand_size (ironclad_deck, 10);
lrg_deck_def_set_starting_energy (ironclad_deck, 3);

/* Define starting cards */
lrg_deck_def_add_card (ironclad_deck, "strike", 5);
lrg_deck_def_add_card (ironclad_deck, "defend", 4);
lrg_deck_def_add_card (ironclad_deck, "bash", 1);
```

### YAML Definition

```yaml
type: deck-def
id: "ironclad-starter"
name: "Ironclad Starter Deck"
min-size: 10
max-size: 40
starting-hand-size: 5
max-hand-size: 10
starting-energy: 3
cards:
  strike: 5
  defend: 4
  bash: 1
```

## LrgDeckInstance

Deck instances represent the runtime state of a deck during a run. They implement `LrgSaveable`.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `definition` | `LrgDeckDef*` | The deck template |
| `cards` | `GPtrArray*` | All cards in deck |
| `draw-pile` | `LrgCardPile*` | Current draw pile |
| `discard-pile` | `LrgCardPile*` | Current discard pile |
| `exhaust-pile` | `LrgCardPile*` | Exhausted cards |

### Deck Operations

```c
/* Create deck from definition */
LrgDeckInstance *deck = lrg_deck_instance_new (ironclad_deck);

/* Get all cards */
GPtrArray *all_cards = lrg_deck_instance_get_all_cards (deck);
guint deck_size = lrg_deck_instance_get_size (deck);

/* Add card */
lrg_deck_instance_add_card (deck, card_instance);

/* Remove card */
lrg_deck_instance_remove_card (deck, card_instance);

/* Find cards */
GPtrArray *strikes = lrg_deck_instance_find_cards_by_id (deck, "strike");
GPtrArray *attacks = lrg_deck_instance_find_cards_by_type (deck, LRG_CARD_TYPE_ATTACK);

/* Get piles */
LrgCardPile *draw = lrg_deck_instance_get_draw_pile (deck);
LrgCardPile *discard = lrg_deck_instance_get_discard_pile (deck);
LrgCardPile *exhaust = lrg_deck_instance_get_exhaust_pile (deck);
```

### Combat Setup

```c
/* Initialize deck for combat */
void
setup_combat_deck (LrgDeckInstance *deck)
{
    LrgCardPile *draw = lrg_deck_instance_get_draw_pile (deck);
    LrgCardPile *discard = lrg_deck_instance_get_discard_pile (deck);
    LrgCardPile *exhaust = lrg_deck_instance_get_exhaust_pile (deck);

    /* Clear piles */
    lrg_card_pile_clear (draw);
    lrg_card_pile_clear (discard);
    lrg_card_pile_clear (exhaust);

    /* Add all cards to draw pile */
    GPtrArray *cards = lrg_deck_instance_get_all_cards (deck);
    for (guint i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);

        /* Reset card state */
        lrg_card_instance_reset (card);

        /* Add to draw pile */
        lrg_card_pile_add (draw, card, LRG_PILE_POSITION_TOP);
    }

    /* Shuffle draw pile */
    GRand *rng = lrg_run_get_rng (current_run);
    lrg_card_pile_shuffle (draw, rng);
}
```

### Drawing and Shuffling

```c
/* Draw cards */
void
draw_cards (LrgDeckInstance *deck, LrgHand *hand, gint count)
{
    LrgCardPile *draw = lrg_deck_instance_get_draw_pile (deck);
    LrgCardPile *discard = lrg_deck_instance_get_discard_pile (deck);

    for (gint i = 0; i < count; i++)
    {
        /* Check if draw pile is empty */
        if (lrg_card_pile_get_count (draw) == 0)
        {
            /* Shuffle discard into draw */
            shuffle_discard_into_draw (deck);

            if (lrg_card_pile_get_count (draw) == 0)
            {
                /* No cards to draw */
                break;
            }
        }

        /* Check hand size */
        if (lrg_hand_is_full (hand))
        {
            break;
        }

        /* Draw card */
        LrgCardInstance *card = lrg_card_pile_draw (draw);
        lrg_hand_add_card (hand, card);
    }
}

/* Shuffle discard into draw */
void
shuffle_discard_into_draw (LrgDeckInstance *deck)
{
    LrgCardPile *draw = lrg_deck_instance_get_draw_pile (deck);
    LrgCardPile *discard = lrg_deck_instance_get_discard_pile (deck);

    /* Move all cards from discard to draw */
    while (lrg_card_pile_get_count (discard) > 0)
    {
        LrgCardInstance *card = lrg_card_pile_draw (discard);
        lrg_card_pile_add (draw, card, LRG_PILE_POSITION_TOP);
    }

    /* Shuffle */
    GRand *rng = lrg_run_get_rng (current_run);
    lrg_card_pile_shuffle (draw, rng);

    /* Emit shuffle event */
    LrgEventBus *bus = lrg_event_bus_get_default ();
    LrgCardEventData *event = lrg_card_event_data_new (LRG_CARD_EVENT_SHUFFLE);
    lrg_event_bus_publish (bus, event);
}
```

### Signals

| Signal | Description |
|--------|-------------|
| `card-added` | Card was added to deck |
| `card-removed` | Card was removed from deck |
| `deck-modified` | Deck structure changed |

## LrgDeckBuilder

The deck builder utility helps construct and validate decks.

### Validation

```c
/* Get builder */
LrgDeckBuilder *builder = lrg_deck_builder_get_default ();

/* Validate deck */
g_autoptr(GError) error = NULL;
gboolean valid = lrg_deck_builder_validate (builder, deck, &error);

if (!valid)
{
    g_warning ("Invalid deck: %s", error->message);
}
```

### Validation Rules

```c
/* Check deck size */
gboolean
check_deck_size (LrgDeckInstance *deck)
{
    LrgDeckDef *def = lrg_deck_instance_get_definition (deck);
    guint size = lrg_deck_instance_get_size (deck);

    gint min = lrg_deck_def_get_min_size (def);
    gint max = lrg_deck_def_get_max_size (def);

    return size >= min && size <= max;
}

/* Check card copies */
gboolean
check_card_copies (LrgDeckInstance *deck)
{
    GHashTable *counts = g_hash_table_new (g_str_hash, g_str_equal);
    GPtrArray *cards = lrg_deck_instance_get_all_cards (deck);

    for (guint i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        LrgCardDef *def = lrg_card_instance_get_definition (card);
        const gchar *id = lrg_card_def_get_id (def);

        gint count = GPOINTER_TO_INT (g_hash_table_lookup (counts, id));
        count++;
        g_hash_table_insert (counts, (gpointer)id, GINT_TO_POINTER (count));

        gint max_copies = lrg_card_def_get_max_copies (def);
        if (max_copies > 0 && count > max_copies)
        {
            g_hash_table_destroy (counts);
            return FALSE;
        }
    }

    g_hash_table_destroy (counts);
    return TRUE;
}
```

### Building Decks

```c
/* Start building a deck */
LrgDeckInstance *deck = lrg_deck_builder_create (builder, deck_def);

/* Add cards */
lrg_deck_builder_add_card (builder, deck, strike_def, 5);
lrg_deck_builder_add_card (builder, deck, defend_def, 4);
lrg_deck_builder_add_card (builder, deck, bash_def, 1);

/* Finalize and validate */
if (lrg_deck_builder_finalize (builder, deck, &error))
{
    /* Deck is ready */
}
```

### Card Addition During Run

```c
/* Add card reward to deck */
void
add_card_to_deck (LrgRun *run, LrgCardDef *card_def)
{
    LrgDeckInstance *deck = lrg_run_get_deck (run);

    /* Create instance */
    LrgCardInstance *card = lrg_card_instance_new (card_def);

    /* Add to deck */
    lrg_deck_instance_add_card (deck, card);

    /* Update profile stats */
    LrgPlayerProfile *profile = lrg_player_profile_get_default ();
    lrg_player_profile_increment_stat (profile, "cards_added", 1);
}

/* Remove card from deck */
void
remove_card_from_deck (LrgRun *run, LrgCardInstance *card)
{
    LrgDeckInstance *deck = lrg_run_get_deck (run);

    /* Remove from deck */
    lrg_deck_instance_remove_card (deck, card);

    /* Update profile stats */
    LrgPlayerProfile *profile = lrg_player_profile_get_default ();
    lrg_player_profile_increment_stat (profile, "cards_removed", 1);
}

/* Upgrade card */
void
upgrade_card (LrgCardInstance *card)
{
    if (lrg_card_instance_can_upgrade (card))
    {
        lrg_card_instance_upgrade (card);

        /* Update profile stats */
        LrgPlayerProfile *profile = lrg_player_profile_get_default ();
        lrg_player_profile_increment_stat (profile, "cards_upgraded", 1);
    }
}
```

### Deck Viewing

```c
/* Get deck summary */
void
print_deck_summary (LrgDeckInstance *deck)
{
    GPtrArray *cards = lrg_deck_instance_get_all_cards (deck);

    /* Count by type */
    gint attacks = 0, skills = 0, powers = 0, statuses = 0, curses = 0;

    for (guint i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        LrgCardDef *def = lrg_card_instance_get_definition (card);

        switch (lrg_card_def_get_card_type (def))
        {
            case LRG_CARD_TYPE_ATTACK: attacks++; break;
            case LRG_CARD_TYPE_SKILL: skills++; break;
            case LRG_CARD_TYPE_POWER: powers++; break;
            case LRG_CARD_TYPE_STATUS: statuses++; break;
            case LRG_CARD_TYPE_CURSE: curses++; break;
        }
    }

    g_print ("Deck: %u cards\n", cards->len);
    g_print ("  Attacks: %d\n", attacks);
    g_print ("  Skills: %d\n", skills);
    g_print ("  Powers: %d\n", powers);
    g_print ("  Statuses: %d\n", statuses);
    g_print ("  Curses: %d\n", curses);
}
```

## See Also

- [Cards Documentation](cards.md)
- [Run Structure Documentation](run-structure.md)
- [Combat Documentation](combat.md)
