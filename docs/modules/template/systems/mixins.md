# Template Mixins

Mixins are interfaces that provide optional functionality to game templates. A template can implement multiple mixins to combine features from different game genres.

## Available Mixins

| Mixin | Description |
|-------|-------------|
| `LrgIdleMixin` | Idle/incremental game mechanics |
| `LrgDeckMixin` | Deckbuilder/card game mechanics |

---

## LrgIdleMixin

The `LrgIdleMixin` interface provides idle/incremental game functionality including offline progress calculation, prestige systems, and automatic saving.

### Interface Methods

```c
struct _LrgIdleMixinInterface
{
    GTypeInterface parent_iface;

    /* Calculate offline progress since last session */
    void (*calculate_offline_progress) (LrgIdleMixin *self,
                                        gdouble       seconds_elapsed);

    /* Handle prestige/reset mechanics */
    void (*on_prestige)                (LrgIdleMixin *self,
                                        guint         prestige_level);

    /* Auto-save trigger */
    void (*on_auto_save)               (LrgIdleMixin *self);

    /* Get current save data for persistence */
    GVariant * (*get_save_data)        (LrgIdleMixin *self);

    /* Restore from save data */
    void (*restore_save_data)          (LrgIdleMixin *self,
                                        GVariant     *data);
};
```

### Implementing LrgIdleMixin

```c
#define MY_TYPE_IDLE_GAME (my_idle_game_get_type ())
G_DECLARE_FINAL_TYPE (MyIdleGame, my_idle_game, MY, IDLE_GAME, LrgIdleTemplate)

static void
my_idle_game_calculate_offline (LrgIdleMixin *mixin, gdouble seconds)
{
    MyIdleGame *self = MY_IDLE_GAME (mixin);

    /* Calculate resources gained while offline */
    LrgBigNumber *rate = self->production_rate;
    LrgBigNumber *gained = lrg_big_number_multiply_float (rate, seconds);

    /* Apply offline efficiency (e.g., 50% of normal rate) */
    LrgBigNumber *adjusted = lrg_big_number_multiply_float (gained, 0.5);

    lrg_big_number_add (self->resources, adjusted);

    lrg_big_number_free (gained);
    lrg_big_number_free (adjusted);
}

static void
my_idle_game_on_prestige (LrgIdleMixin *mixin, guint level)
{
    MyIdleGame *self = MY_IDLE_GAME (mixin);

    /* Reset resources but keep prestige multiplier */
    lrg_big_number_set_double (self->resources, 0.0);

    /* Calculate prestige bonus */
    self->prestige_multiplier = 1.0 + (level * 0.1);  /* +10% per prestige */

    /* Unlock prestige upgrades */
    unlock_prestige_tier (self, level);
}

static void
my_idle_game_idle_mixin_init (LrgIdleMixinInterface *iface)
{
    iface->calculate_offline_progress = my_idle_game_calculate_offline;
    iface->on_prestige = my_idle_game_on_prestige;
    iface->on_auto_save = my_idle_game_auto_save;
    iface->get_save_data = my_idle_game_get_save_data;
    iface->restore_save_data = my_idle_game_restore_save_data;
}

G_DEFINE_TYPE_WITH_CODE (MyIdleGame, my_idle_game, LRG_TYPE_IDLE_TEMPLATE,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN, my_idle_game_idle_mixin_init))
```

### Using LrgIdleMixin

```c
/* Check if object supports idle mechanics */
if (LRG_IS_IDLE_MIXIN (game))
{
    LrgIdleMixin *idle = LRG_IDLE_MIXIN (game);

    /* Calculate offline progress on game start */
    gdouble seconds = get_seconds_since_last_session ();
    lrg_idle_mixin_calculate_offline_progress (idle, seconds);

    /* Show offline earnings popup */
    show_offline_earnings (game);
}

/* Trigger prestige */
lrg_idle_mixin_on_prestige (idle, new_prestige_level);

/* Auto-save */
lrg_idle_mixin_on_auto_save (idle);
```

### Save/Restore Pattern

```c
static GVariant *
my_idle_game_get_save_data (LrgIdleMixin *mixin)
{
    MyIdleGame *self = MY_IDLE_GAME (mixin);

    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    /* Save big numbers as strings */
    g_autofree gchar *resources_str = lrg_big_number_to_string (self->resources);
    g_variant_builder_add (&builder, "{sv}", "resources",
                           g_variant_new_string (resources_str));

    g_variant_builder_add (&builder, "{sv}", "prestige_level",
                           g_variant_new_uint32 (self->prestige_level));

    g_variant_builder_add (&builder, "{sv}", "timestamp",
                           g_variant_new_int64 (g_get_real_time () / G_USEC_PER_SEC));

    return g_variant_builder_end (&builder);
}

static void
my_idle_game_restore_save_data (LrgIdleMixin *mixin, GVariant *data)
{
    MyIdleGame *self = MY_IDLE_GAME (mixin);

    GVariant *value;

    value = g_variant_lookup_value (data, "resources", G_VARIANT_TYPE_STRING);
    if (value != NULL)
    {
        lrg_big_number_set_string (self->resources, g_variant_get_string (value, NULL));
        g_variant_unref (value);
    }

    value = g_variant_lookup_value (data, "prestige_level", G_VARIANT_TYPE_UINT32);
    if (value != NULL)
    {
        self->prestige_level = g_variant_get_uint32 (value);
        g_variant_unref (value);
    }
}
```

---

## LrgDeckMixin

The `LrgDeckMixin` interface provides deckbuilding mechanics including deck management, draw/discard piles, hand management, and turn-based hooks.

### Interface Methods

```c
struct _LrgDeckMixinInterface
{
    GTypeInterface parent_iface;

    /* Draw cards from draw pile to hand */
    void (*draw_cards)         (LrgDeckMixin *self,
                                guint         count);

    /* Discard cards from hand */
    void (*discard_hand)       (LrgDeckMixin *self);

    /* Shuffle discard pile into draw pile */
    void (*shuffle_discard)    (LrgDeckMixin *self);

    /* Called at start of turn */
    void (*on_turn_start)      (LrgDeckMixin *self);

    /* Called at end of turn */
    void (*on_turn_end)        (LrgDeckMixin *self);

    /* Called when a card is played */
    void (*on_card_played)     (LrgDeckMixin *self,
                                gpointer      card);

    /* Get all cards in specific pile */
    GPtrArray * (*get_draw_pile)    (LrgDeckMixin *self);
    GPtrArray * (*get_discard_pile) (LrgDeckMixin *self);
    GPtrArray * (*get_hand)         (LrgDeckMixin *self);
    GPtrArray * (*get_exhaust_pile) (LrgDeckMixin *self);
};
```

### Implementing LrgDeckMixin

```c
#define MY_TYPE_CARD_GAME (my_card_game_get_type ())
G_DECLARE_FINAL_TYPE (MyCardGame, my_card_game, MY, CARD_GAME, LrgDeckbuilderTemplate)

struct _MyCardGame
{
    LrgDeckbuilderTemplate parent_instance;
    GPtrArray *draw_pile;
    GPtrArray *discard_pile;
    GPtrArray *hand;
    GPtrArray *exhaust_pile;
    guint hand_size;
};

static void
my_card_game_draw_cards (LrgDeckMixin *mixin, guint count)
{
    MyCardGame *self = MY_CARD_GAME (mixin);

    for (guint i = 0; i < count; i++)
    {
        /* Shuffle if draw pile empty */
        if (self->draw_pile->len == 0)
        {
            lrg_deck_mixin_shuffle_discard (mixin);
            if (self->draw_pile->len == 0)
                break;  /* No more cards */
        }

        /* Draw from top of pile */
        gpointer card = g_ptr_array_steal_index (self->draw_pile,
                                                  self->draw_pile->len - 1);
        g_ptr_array_add (self->hand, card);
    }
}

static void
my_card_game_discard_hand (LrgDeckMixin *mixin)
{
    MyCardGame *self = MY_CARD_GAME (mixin);

    while (self->hand->len > 0)
    {
        gpointer card = g_ptr_array_steal_index (self->hand, 0);
        g_ptr_array_add (self->discard_pile, card);
    }
}

static void
my_card_game_shuffle_discard (LrgDeckMixin *mixin)
{
    MyCardGame *self = MY_CARD_GAME (mixin);

    /* Move all discard to draw pile */
    while (self->discard_pile->len > 0)
    {
        gpointer card = g_ptr_array_steal_index (self->discard_pile, 0);
        g_ptr_array_add (self->draw_pile, card);
    }

    /* Fisher-Yates shuffle */
    for (guint i = self->draw_pile->len - 1; i > 0; i--)
    {
        guint j = g_random_int_range (0, i + 1);
        gpointer tmp = g_ptr_array_index (self->draw_pile, i);
        self->draw_pile->pdata[i] = self->draw_pile->pdata[j];
        self->draw_pile->pdata[j] = tmp;
    }
}

static void
my_card_game_on_turn_start (LrgDeckMixin *mixin)
{
    MyCardGame *self = MY_CARD_GAME (mixin);

    /* Draw starting hand */
    lrg_deck_mixin_draw_cards (mixin, self->hand_size);

    /* Reset energy/mana */
    self->energy = self->max_energy;
}

static void
my_card_game_on_turn_end (LrgDeckMixin *mixin)
{
    MyCardGame *self = MY_CARD_GAME (mixin);

    /* Discard remaining hand */
    lrg_deck_mixin_discard_hand (mixin);

    /* End of turn effects */
    apply_end_of_turn_effects (self);
}

static void
my_card_game_deck_mixin_init (LrgDeckMixinInterface *iface)
{
    iface->draw_cards = my_card_game_draw_cards;
    iface->discard_hand = my_card_game_discard_hand;
    iface->shuffle_discard = my_card_game_shuffle_discard;
    iface->on_turn_start = my_card_game_on_turn_start;
    iface->on_turn_end = my_card_game_on_turn_end;
    iface->on_card_played = my_card_game_on_card_played;
    iface->get_draw_pile = my_card_game_get_draw_pile;
    iface->get_discard_pile = my_card_game_get_discard_pile;
    iface->get_hand = my_card_game_get_hand;
    iface->get_exhaust_pile = my_card_game_get_exhaust_pile;
}

G_DEFINE_TYPE_WITH_CODE (MyCardGame, my_card_game, LRG_TYPE_DECKBUILDER_TEMPLATE,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_DECK_MIXIN, my_card_game_deck_mixin_init))
```

### Using LrgDeckMixin

```c
/* Check if object supports deck mechanics */
if (LRG_IS_DECK_MIXIN (game))
{
    LrgDeckMixin *deck = LRG_DECK_MIXIN (game);

    /* Start a new turn */
    lrg_deck_mixin_on_turn_start (deck);

    /* Get current hand */
    GPtrArray *hand = lrg_deck_mixin_get_hand (deck);
    for (guint i = 0; i < hand->len; i++)
    {
        Card *card = g_ptr_array_index (hand, i);
        render_card (card, i);
    }

    /* Play a card */
    lrg_deck_mixin_on_card_played (deck, selected_card);

    /* Draw additional cards */
    lrg_deck_mixin_draw_cards (deck, 2);

    /* End turn */
    lrg_deck_mixin_on_turn_end (deck);
}
```

### Card Playing Example

```c
static void
my_card_game_on_card_played (LrgDeckMixin *mixin, gpointer card_ptr)
{
    MyCardGame *self = MY_CARD_GAME (mixin);
    Card *card = CARD (card_ptr);

    /* Check if can afford */
    if (card->cost > self->energy)
        return;

    /* Spend energy */
    self->energy -= card->cost;

    /* Execute card effect */
    card_execute_effect (card, self);

    /* Move card from hand to discard (or exhaust) */
    if (card->exhausts)
    {
        g_ptr_array_add (self->exhaust_pile, card);
    }
    else
    {
        g_ptr_array_add (self->discard_pile, card);
    }

    /* Remove from hand */
    g_ptr_array_remove (self->hand, card);

    /* Emit signal for animations */
    g_signal_emit_by_name (self, "card-played", card);
}
```

---

## Combining Mixins

A single template can implement multiple mixins:

```c
/* An idle game with card mechanics */
G_DEFINE_TYPE_WITH_CODE (MyHybridGame, my_hybrid_game, LRG_TYPE_GAME_TEMPLATE,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN, my_hybrid_idle_mixin_init)
    G_IMPLEMENT_INTERFACE (LRG_TYPE_DECK_MIXIN, my_hybrid_deck_mixin_init))

static void
my_hybrid_game_update (LrgGameTemplate *template, gdouble delta)
{
    MyHybridGame *self = MY_HYBRID_GAME (template);

    /* Idle mechanics - resources accumulate over time */
    update_idle_production (self, delta);

    /* Card mechanics - handle card battles */
    if (self->in_combat)
        update_card_combat (self, delta);
}
```

---

## Checking for Mixin Support

```c
static void
generic_game_handler (GObject *game_object)
{
    /* Handle idle features if supported */
    if (LRG_IS_IDLE_MIXIN (game_object))
    {
        LrgIdleMixin *idle = LRG_IDLE_MIXIN (game_object);
        setup_idle_ui (idle);
    }

    /* Handle deck features if supported */
    if (LRG_IS_DECK_MIXIN (game_object))
    {
        LrgDeckMixin *deck = LRG_DECK_MIXIN (game_object);
        setup_card_ui (deck);
    }
}
```

## Related Documentation

- [LrgIdleTemplate](../templates/idle-template.md) - Idle game template
- [LrgDeckbuilderTemplate](../templates/deckbuilder-template.md) - Deckbuilder templates
- [GObject Interfaces](https://docs.gtk.org/gobject/concepts.html#interfaces) - GObject interface documentation
