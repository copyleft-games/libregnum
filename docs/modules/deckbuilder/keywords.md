# Keywords

Keywords are special properties that modify card behavior. The system includes built-in keywords (as flags) and support for custom keywords via derivable types.

## LrgCardKeyword

Built-in keywords are represented as flags that can be combined:

```c
typedef enum {
    LRG_CARD_KEYWORD_NONE       = 0,
    LRG_CARD_KEYWORD_INNATE     = 1 << 0,
    LRG_CARD_KEYWORD_ETHEREAL   = 1 << 1,
    LRG_CARD_KEYWORD_RETAIN     = 1 << 2,
    LRG_CARD_KEYWORD_EXHAUST    = 1 << 3,
    LRG_CARD_KEYWORD_UNPLAYABLE = 1 << 4,
    LRG_CARD_KEYWORD_X_COST     = 1 << 5,
    LRG_CARD_KEYWORD_AUTOPLAY   = 1 << 6
} LrgCardKeyword;
```

### Keyword Descriptions

| Keyword | Effect |
|---------|--------|
| **Innate** | Card is always drawn in opening hand |
| **Ethereal** | If not played by end of turn, exhausts instead of discarding |
| **Retain** | Card stays in hand between turns (not discarded at turn end) |
| **Exhaust** | When played, card is removed from deck for the combat |
| **Unplayable** | Cannot be played normally (may have other uses) |
| **X Cost** | Cost equals all available energy; effects scale with energy spent |
| **Autoplay** | Automatically plays when drawn (no energy cost) |

### Using Keywords

```c
/* Set keywords on card definition */
lrg_card_def_set_keywords (card, LRG_CARD_KEYWORD_EXHAUST |
                                  LRG_CARD_KEYWORD_ETHEREAL);

/* Check for keyword */
if (lrg_card_def_has_keyword (card, LRG_CARD_KEYWORD_INNATE))
{
    /* Prioritize for opening hand */
}

/* Add keyword */
lrg_card_def_add_keyword (card, LRG_CARD_KEYWORD_RETAIN);

/* Remove keyword */
lrg_card_def_remove_keyword (card, LRG_CARD_KEYWORD_ETHEREAL);
```

### Keyword Processing

The combat manager automatically handles keyword effects:

```c
/* At turn start - handle Innate */
static void
draw_opening_hand (LrgCombatManager *manager)
{
    LrgCardPile *draw = manager->draw_pile;

    /* Find and draw innate cards first */
    for (guint i = 0; i < lrg_card_pile_get_count (draw); i++)
    {
        LrgCardInstance *card = lrg_card_pile_peek (draw, i);
        if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_INNATE))
        {
            lrg_card_pile_draw_card (draw, card->uuid);
            lrg_hand_add_card (manager->hand, card);
        }
    }

    /* Draw remaining cards */
    draw_cards (manager, manager->cards_per_turn);
}

/* At turn end - handle Ethereal and Retain */
static void
end_turn (LrgCombatManager *manager)
{
    LrgHand *hand = manager->hand;

    for (gint i = lrg_hand_get_count (hand) - 1; i >= 0; i--)
    {
        LrgCardInstance *card = lrg_hand_get_card (hand, i);

        if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_RETAIN))
        {
            /* Keep in hand */
            continue;
        }

        lrg_hand_remove_card (hand, i);

        if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_ETHEREAL))
        {
            /* Exhaust instead of discard */
            lrg_card_pile_add (manager->exhaust_pile, card, LRG_PILE_POSITION_TOP);
        }
        else
        {
            /* Normal discard */
            lrg_card_pile_add (manager->discard_pile, card, LRG_PILE_POSITION_TOP);
        }
    }
}
```

## LrgCardKeywordDef

For custom keywords with complex behavior, use the derivable `LrgCardKeywordDef`:

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique keyword identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Tooltip description |
| `icon` | `gchar*` | Icon path |

### Virtual Methods

```c
struct _LrgCardKeywordDefClass
{
    GObjectClass parent_class;

    /* Called when card with keyword is drawn */
    void (*on_card_drawn)    (LrgCardKeywordDef *self,
                              LrgCardInstance   *card,
                              LrgCombatContext  *ctx);

    /* Called when card with keyword is played */
    void (*on_card_played)   (LrgCardKeywordDef *self,
                              LrgCardInstance   *card,
                              LrgCombatContext  *ctx);

    /* Called when card with keyword is discarded */
    void (*on_card_discarded)(LrgCardKeywordDef *self,
                              LrgCardInstance   *card,
                              LrgCombatContext  *ctx);

    /* Called at turn start for cards with keyword in hand */
    void (*on_turn_start)    (LrgCardKeywordDef *self,
                              LrgCardInstance   *card,
                              LrgCombatContext  *ctx);

    /* Called at turn end for cards with keyword in hand */
    void (*on_turn_end)      (LrgCardKeywordDef *self,
                              LrgCardInstance   *card,
                              LrgCombatContext  *ctx);

    gpointer _reserved[8];
};
```

### Creating Custom Keywords

```c
/* Define a "Rage" keyword that gains strength when discarded */
G_DECLARE_FINAL_TYPE (RageKeyword, rage_keyword, MY, RAGE_KEYWORD, LrgCardKeywordDef)

static void
rage_keyword_on_card_discarded (LrgCardKeywordDef *def,
                                 LrgCardInstance   *card,
                                 LrgCombatContext  *ctx)
{
    LrgCombatant *player = lrg_combat_context_get_player (ctx);
    lrg_combatant_apply_status (player, "strength", 1);
}

static void
rage_keyword_class_init (RageKeywordClass *klass)
{
    LrgCardKeywordDefClass *keyword_class = LRG_CARD_KEYWORD_DEF_CLASS (klass);
    keyword_class->on_card_discarded = rage_keyword_on_card_discarded;
}

/* Register with keyword registry */
LrgCardKeywordRegistry *registry = lrg_card_keyword_registry_get_default ();
g_autoptr(RageKeyword) rage = rage_keyword_new ();
lrg_card_keyword_def_set_id (LRG_CARD_KEYWORD_DEF (rage), "rage");
lrg_card_keyword_def_set_name (LRG_CARD_KEYWORD_DEF (rage), "Rage");
lrg_card_keyword_def_set_description (LRG_CARD_KEYWORD_DEF (rage),
    "When discarded, gain 1 Strength.");
lrg_card_keyword_registry_register (registry, LRG_CARD_KEYWORD_DEF (rage));
```

## LrgCardKeywordRegistry

The registry manages custom keywords:

```c
/* Get registry */
LrgCardKeywordRegistry *registry = lrg_card_keyword_registry_get_default ();

/* Register keyword */
lrg_card_keyword_registry_register (registry, keyword_def);

/* Look up keyword */
LrgCardKeywordDef *def = lrg_card_keyword_registry_get (registry, "rage");

/* Get all keywords */
GList *all_keywords = lrg_card_keyword_registry_get_all (registry);
```

## LrgSynergy

Synergies define bonuses when certain card combinations are played.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique synergy identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Synergy description |
| `required-tags` | `GPtrArray*` | Tags that cards must have |
| `required-count` | `gint` | Minimum cards needed |

### Virtual Methods

```c
struct _LrgSynergyDefClass
{
    GObjectClass parent_class;

    /* Check if a card matches this synergy */
    gboolean (*card_matches)   (LrgSynergyDef    *self,
                                LrgCardDef       *card);

    /* Calculate bonus based on matching cards */
    gint     (*calculate_bonus)(LrgSynergyDef    *self,
                                LrgCombatContext *ctx);

    /* Get description with current bonus */
    gchar *  (*get_description)(LrgSynergyDef    *self,
                                LrgCombatContext *ctx);

    gpointer _reserved[8];
};
```

### Creating Synergies

```c
/* Create a fire synergy */
g_autoptr(LrgSynergyDef) fire_synergy = lrg_synergy_def_new ("fire-mastery");
lrg_synergy_def_set_name (fire_synergy, "Fire Mastery");
lrg_synergy_def_set_description (fire_synergy,
    "Fire cards deal +2 damage for each other fire card played this turn.");
lrg_synergy_def_add_required_tag (fire_synergy, "fire");
lrg_synergy_def_set_required_count (fire_synergy, 2);

/* Check synergy during combat */
gint fire_cards_played = lrg_combat_context_get_tag_count (ctx, "fire");
if (fire_cards_played >= 2)
{
    gint bonus = lrg_synergy_def_calculate_bonus (fire_synergy, ctx);
    damage += bonus;
}
```

### Synergy Tracking

```c
/* Track tags played this turn */
static void
on_card_played (LrgCombatContext *ctx, LrgCardInstance *card)
{
    LrgCardDef *def = lrg_card_instance_get_definition (card);
    GPtrArray *tags = lrg_card_def_get_tags (def);

    for (guint i = 0; i < tags->len; i++)
    {
        const gchar *tag = g_ptr_array_index (tags, i);
        lrg_combat_context_increment_tag_count (ctx, tag);
    }

    /* Check for synergy triggers */
    check_synergies (ctx);
}

/* Reset at turn start */
static void
on_turn_start (LrgCombatContext *ctx)
{
    lrg_combat_context_reset_tag_counts (ctx);
}
```

## See Also

- [Cards Documentation](cards.md)
- [Effects Documentation](effects.md)
- [Combat Documentation](combat.md)
