# Deckbuilder Example

This example demonstrates creating a roguelike deckbuilder game similar to Slay the Spire using `LrgDeckbuilderCombatTemplate`. It includes card drawing, energy management, enemy encounters, and turn-based combat.

## Complete Code

```c
/* deckbuilder.c - Roguelike deckbuilder combat game */

#include <libregnum.h>

/* ==========================================================================
 * Card System
 * ========================================================================== */

typedef enum {
    CARD_TYPE_ATTACK,
    CARD_TYPE_SKILL,
    CARD_TYPE_POWER
} CardType;

typedef struct {
    gchar *name;
    gchar *description;
    CardType type;
    gint cost;
    gint damage;
    gint block;
    gint draw;
    gboolean exhausts;
} Card;

static Card *
card_new (const gchar *name,
          const gchar *description,
          CardType     type,
          gint         cost,
          gint         damage,
          gint         block,
          gint         draw,
          gboolean     exhausts)
{
    Card *card = g_new0 (Card, 1);
    card->name = g_strdup (name);
    card->description = g_strdup (description);
    card->type = type;
    card->cost = cost;
    card->damage = damage;
    card->block = block;
    card->draw = draw;
    card->exhausts = exhausts;
    return card;
}

static void
card_free (Card *card)
{
    g_free (card->name);
    g_free (card->description);
    g_free (card);
}

/* ==========================================================================
 * Enemy System
 * ========================================================================== */

typedef struct {
    gchar *name;
    gint max_health;
    gint current_health;
    gint intent_damage;   /* Damage enemy will deal */
    gint intent_block;    /* Block enemy will gain */
    gint block;           /* Current block */
} Enemy;

static Enemy *
enemy_new (const gchar *name, gint health)
{
    Enemy *enemy = g_new0 (Enemy, 1);
    enemy->name = g_strdup (name);
    enemy->max_health = health;
    enemy->current_health = health;
    return enemy;
}

/* ==========================================================================
 * Type Declaration
 * ========================================================================== */

#define MY_TYPE_DECKBUILDER (my_deckbuilder_get_type ())
G_DECLARE_FINAL_TYPE (MyDeckbuilder, my_deckbuilder, MY, DECKBUILDER,
                      LrgDeckbuilderCombatTemplate)

struct _MyDeckbuilder
{
    LrgDeckbuilderCombatTemplate parent_instance;

    /* Player state */
    gint max_health;
    gint current_health;
    gint block;
    gint energy;
    gint max_energy;

    /* Card piles */
    GPtrArray *draw_pile;
    GPtrArray *discard_pile;
    GPtrArray *hand;
    GPtrArray *exhaust_pile;

    /* Enemy */
    Enemy *current_enemy;

    /* Combat state */
    gboolean player_turn;
    gint selected_card;

    /* Game state */
    gboolean combat_ended;
    gboolean player_won;
};

G_DEFINE_TYPE (MyDeckbuilder, my_deckbuilder, LRG_TYPE_DECKBUILDER_COMBAT_TEMPLATE)

/* ==========================================================================
 * Deck Management
 * ========================================================================== */

static void
shuffle_array (GPtrArray *array)
{
    for (guint i = array->len - 1; i > 0; i--)
    {
        guint j = g_random_int_range (0, i + 1);
        gpointer tmp = array->pdata[i];
        array->pdata[i] = array->pdata[j];
        array->pdata[j] = tmp;
    }
}

static void
init_starter_deck (MyDeckbuilder *self)
{
    /* 5 Strikes */
    for (gint i = 0; i < 5; i++)
    {
        g_ptr_array_add (self->draw_pile,
            card_new ("Strike", "Deal 6 damage", CARD_TYPE_ATTACK, 1, 6, 0, 0, FALSE));
    }

    /* 4 Defends */
    for (gint i = 0; i < 4; i++)
    {
        g_ptr_array_add (self->draw_pile,
            card_new ("Defend", "Gain 5 block", CARD_TYPE_SKILL, 1, 0, 5, 0, FALSE));
    }

    /* 1 Bash */
    g_ptr_array_add (self->draw_pile,
        card_new ("Bash", "Deal 8 damage, apply 2 Vulnerable",
                  CARD_TYPE_ATTACK, 2, 8, 0, 0, FALSE));

    shuffle_array (self->draw_pile);
}

static void
draw_cards (MyDeckbuilder *self, guint count)
{
    for (guint i = 0; i < count && self->hand->len < 10; i++)
    {
        if (self->draw_pile->len == 0)
        {
            /* Shuffle discard into draw */
            while (self->discard_pile->len > 0)
            {
                g_ptr_array_add (self->draw_pile,
                    g_ptr_array_steal_index (self->discard_pile, 0));
            }
            shuffle_array (self->draw_pile);

            if (self->draw_pile->len == 0)
                break;
        }

        Card *card = g_ptr_array_steal_index (self->draw_pile,
                                               self->draw_pile->len - 1);
        g_ptr_array_add (self->hand, card);
    }
}

static void
discard_hand (MyDeckbuilder *self)
{
    while (self->hand->len > 0)
    {
        Card *card = g_ptr_array_steal_index (self->hand, 0);
        g_ptr_array_add (self->discard_pile, card);
    }
}

/* ==========================================================================
 * Combat Logic
 * ========================================================================== */

static void
start_player_turn (MyDeckbuilder *self)
{
    self->player_turn = TRUE;
    self->energy = self->max_energy;
    self->block = 0;  /* Block doesn't carry over */
    draw_cards (self, 5);
}

static void
end_player_turn (MyDeckbuilder *self)
{
    self->player_turn = FALSE;
    discard_hand (self);

    /* Enemy turn */
    self->current_enemy->block = 0;

    /* Enemy action based on intent */
    if (self->current_enemy->intent_damage > 0)
    {
        gint damage = self->current_enemy->intent_damage;

        if (self->block > 0)
        {
            gint blocked = MIN (self->block, damage);
            self->block -= blocked;
            damage -= blocked;
        }

        if (damage > 0)
        {
            self->current_health -= damage;
            if (self->current_health <= 0)
            {
                self->combat_ended = TRUE;
                self->player_won = FALSE;
            }
        }
    }

    if (self->current_enemy->intent_block > 0)
        self->current_enemy->block += self->current_enemy->intent_block;

    /* Set new intent (simple AI: alternate attack/block) */
    if (self->current_enemy->intent_damage > 0)
    {
        self->current_enemy->intent_damage = 0;
        self->current_enemy->intent_block = 6;
    }
    else
    {
        self->current_enemy->intent_damage = 8 + g_random_int_range (0, 5);
        self->current_enemy->intent_block = 0;
    }

    /* Start next player turn */
    if (!self->combat_ended)
        start_player_turn (self);
}

static gboolean
play_card (MyDeckbuilder *self, gint index)
{
    if (index < 0 || index >= (gint)self->hand->len)
        return FALSE;

    Card *card = g_ptr_array_index (self->hand, index);

    /* Check energy */
    if (card->cost > self->energy)
        return FALSE;

    /* Spend energy */
    self->energy -= card->cost;

    /* Apply card effects */
    if (card->damage > 0)
    {
        gint damage = card->damage;

        /* Apply to enemy block first */
        if (self->current_enemy->block > 0)
        {
            gint blocked = MIN (self->current_enemy->block, damage);
            self->current_enemy->block -= blocked;
            damage -= blocked;
        }

        if (damage > 0)
        {
            self->current_enemy->current_health -= damage;
            if (self->current_enemy->current_health <= 0)
            {
                self->combat_ended = TRUE;
                self->player_won = TRUE;
            }
        }
    }

    if (card->block > 0)
        self->block += card->block;

    if (card->draw > 0)
        draw_cards (self, card->draw);

    /* Move card to appropriate pile */
    g_ptr_array_steal_index (self->hand, index);

    if (card->exhausts)
        g_ptr_array_add (self->exhaust_pile, card);
    else
        g_ptr_array_add (self->discard_pile, card);

    return TRUE;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
my_deckbuilder_configure (LrgGameTemplate *template)
{
    MyDeckbuilder *self = MY_DECKBUILDER (template);

    g_object_set (template,
                  "title", "Roguelike Deckbuilder",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    /* Player stats */
    self->max_health = 80;
    self->current_health = 80;
    self->max_energy = 3;
    self->energy = 3;
    self->block = 0;

    /* Initialize card piles */
    self->draw_pile = g_ptr_array_new_with_free_func ((GDestroyNotify)card_free);
    self->discard_pile = g_ptr_array_new ();
    self->hand = g_ptr_array_new ();
    self->exhaust_pile = g_ptr_array_new ();

    /* Create starter deck */
    init_starter_deck (self);

    /* Create enemy */
    self->current_enemy = enemy_new ("Cultist", 50);
    self->current_enemy->intent_damage = 6;

    /* Start combat */
    self->player_turn = TRUE;
    self->selected_card = -1;
    draw_cards (self, 5);
}

static void
my_deckbuilder_update (LrgGameTemplate *template, gdouble delta)
{
    MyDeckbuilder *self = MY_DECKBUILDER (template);

    if (self->combat_ended)
    {
        if (grl_is_key_pressed (GRL_KEY_R))
        {
            /* Reset for new combat */
            self->current_health = self->max_health;
            self->current_enemy->current_health = self->current_enemy->max_health;
            self->combat_ended = FALSE;

            /* Reset deck */
            while (self->hand->len > 0)
                g_ptr_array_add (self->draw_pile, g_ptr_array_steal_index (self->hand, 0));
            while (self->discard_pile->len > 0)
                g_ptr_array_add (self->draw_pile, g_ptr_array_steal_index (self->discard_pile, 0));
            while (self->exhaust_pile->len > 0)
                g_ptr_array_add (self->draw_pile, g_ptr_array_steal_index (self->exhaust_pile, 0));

            shuffle_array (self->draw_pile);
            start_player_turn (self);
        }
        return;
    }

    if (!self->player_turn)
        return;

    /* Card selection with number keys or mouse */
    for (gint i = 0; i < MIN (9, (gint)self->hand->len); i++)
    {
        if (grl_is_key_pressed (GRL_KEY_1 + i))
        {
            play_card (self, i);
            self->selected_card = -1;
        }
    }

    /* End turn with E or Space */
    if (grl_is_key_pressed (GRL_KEY_E) || grl_is_key_pressed (GRL_KEY_SPACE))
        end_player_turn (self);

    /* Escape to quit */
    if (grl_is_key_pressed (GRL_KEY_ESCAPE))
        lrg_game_template_quit (template);
}

static void
my_deckbuilder_draw (LrgGameTemplate *template)
{
    MyDeckbuilder *self = MY_DECKBUILDER (template);

    /* Colors */
    g_autoptr(GrlColor) bg = grl_color_new (20, 25, 30, 255);
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) red = grl_color_new (220, 60, 60, 255);
    g_autoptr(GrlColor) green = grl_color_new (60, 180, 60, 255);
    g_autoptr(GrlColor) blue = grl_color_new (60, 120, 220, 255);
    g_autoptr(GrlColor) gray = grl_color_new (80, 85, 90, 255);
    g_autoptr(GrlColor) gold = grl_color_new (255, 200, 50, 255);

    grl_clear_background (bg);

    /* Draw enemy */
    grl_draw_rectangle (540, 50, 200, 180, gray);
    grl_draw_text (self->current_enemy->name, 560, 60, 20, white);

    g_autofree gchar *enemy_hp = g_strdup_printf ("HP: %d/%d",
        self->current_enemy->current_health, self->current_enemy->max_health);
    grl_draw_text (enemy_hp, 560, 90, 16, red);

    if (self->current_enemy->block > 0)
    {
        g_autofree gchar *enemy_block = g_strdup_printf ("Block: %d",
            self->current_enemy->block);
        grl_draw_text (enemy_block, 560, 115, 16, blue);
    }

    /* Enemy intent */
    if (self->current_enemy->intent_damage > 0)
    {
        g_autofree gchar *intent = g_strdup_printf ("Intent: Attack for %d",
            self->current_enemy->intent_damage);
        grl_draw_text (intent, 560, 190, 16, red);
    }
    else
    {
        g_autofree gchar *intent = g_strdup_printf ("Intent: Block %d",
            self->current_enemy->intent_block);
        grl_draw_text (intent, 560, 190, 16, blue);
    }

    /* Draw player stats */
    g_autofree gchar *player_hp = g_strdup_printf ("HP: %d/%d",
        self->current_health, self->max_health);
    grl_draw_text (player_hp, 50, 300, 24, red);

    if (self->block > 0)
    {
        g_autofree gchar *player_block = g_strdup_printf ("Block: %d", self->block);
        grl_draw_text (player_block, 50, 335, 20, blue);
    }

    /* Draw energy */
    for (gint i = 0; i < self->max_energy; i++)
    {
        GrlColor *color = (i < self->energy) ? gold : gray;
        grl_draw_circle (80 + i * 50, 400, 20, color);
    }

    g_autofree gchar *energy_str = g_strdup_printf ("%d/%d", self->energy, self->max_energy);
    grl_draw_text (energy_str, 60, 430, 16, white);

    /* Draw hand */
    grl_draw_text ("Hand (press 1-9 to play):", 50, 480, 18, white);

    for (guint i = 0; i < self->hand->len; i++)
    {
        Card *card = g_ptr_array_index (self->hand, i);
        gint x = 50 + (i % 5) * 240;
        gint y = 510 + (i / 5) * 100;

        gboolean can_play = card->cost <= self->energy;
        GrlColor *border_color = can_play ? green : gray;

        /* Card background */
        grl_draw_rectangle (x, y, 220, 90, border_color);
        grl_draw_rectangle (x + 2, y + 2, 216, 86, bg);

        /* Card info */
        g_autofree gchar *title = g_strdup_printf ("%d. %s [%d]",
            i + 1, card->name, card->cost);
        grl_draw_text (title, x + 10, y + 10, 16, can_play ? white : gray);
        grl_draw_text (card->description, x + 10, y + 35, 14, gray);

        if (card->damage > 0)
        {
            g_autofree gchar *dmg = g_strdup_printf ("Damage: %d", card->damage);
            grl_draw_text (dmg, x + 10, y + 55, 12, red);
        }
        if (card->block > 0)
        {
            g_autofree gchar *blk = g_strdup_printf ("Block: %d", card->block);
            grl_draw_text (blk, x + 120, y + 55, 12, blue);
        }
    }

    /* Draw pile counts */
    g_autofree gchar *deck_info = g_strdup_printf (
        "Draw: %u | Discard: %u | Exhaust: %u",
        self->draw_pile->len, self->discard_pile->len, self->exhaust_pile->len);
    grl_draw_text (deck_info, 50, 700, 14, gray);

    /* Instructions */
    grl_draw_text ("E/Space: End Turn | ESC: Quit", 900, 700, 14, gray);

    /* Combat end overlay */
    if (self->combat_ended)
    {
        g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 200);
        grl_draw_rectangle (0, 0, 1280, 720, overlay);

        if (self->player_won)
        {
            grl_draw_text ("VICTORY!", 530, 300, 48, green);
            grl_draw_text ("You defeated the enemy!", 470, 380, 24, white);
        }
        else
        {
            grl_draw_text ("DEFEAT", 550, 300, 48, red);
            grl_draw_text ("You have been slain.", 490, 380, 24, white);
        }

        grl_draw_text ("Press R to restart", 530, 450, 20, gray);
    }
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
my_deckbuilder_class_init (MyDeckbuilderClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    template_class->configure = my_deckbuilder_configure;
    template_class->update = my_deckbuilder_update;
    template_class->draw = my_deckbuilder_draw;
}

static void
my_deckbuilder_init (MyDeckbuilder *self)
{
}

/* ==========================================================================
 * Main Entry Point
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(MyDeckbuilder) game = g_object_new (MY_TYPE_DECKBUILDER, NULL);
    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
```

## Key Features Demonstrated

### Card Pile Management

```c
/* Draw from draw pile to hand */
static void
draw_cards (MyDeckbuilder *self, guint count)
{
    for (guint i = 0; i < count; i++)
    {
        if (self->draw_pile->len == 0)
        {
            /* Shuffle discard into draw */
            while (self->discard_pile->len > 0)
            {
                g_ptr_array_add (self->draw_pile,
                    g_ptr_array_steal_index (self->discard_pile, 0));
            }
            shuffle_array (self->draw_pile);
        }

        Card *card = g_ptr_array_steal_index (self->draw_pile,
                                               self->draw_pile->len - 1);
        g_ptr_array_add (self->hand, card);
    }
}
```

### Energy System

```c
static gboolean
play_card (MyDeckbuilder *self, gint index)
{
    Card *card = g_ptr_array_index (self->hand, index);

    /* Check energy cost */
    if (card->cost > self->energy)
        return FALSE;

    self->energy -= card->cost;

    /* Apply effects... */
}
```

### Block and Damage Resolution

```c
/* Damage goes through block first */
if (self->block > 0)
{
    gint blocked = MIN (self->block, damage);
    self->block -= blocked;
    damage -= blocked;
}

if (damage > 0)
    self->current_health -= damage;
```

### Enemy Intent System

```c
/* Enemy telegraphs their next action */
self->current_enemy->intent_damage = 8;  /* Will attack for 8 */

/* Alternate between attack and defense */
if (self->current_enemy->intent_damage > 0)
{
    self->current_enemy->intent_damage = 0;
    self->current_enemy->intent_block = 6;
}
else
{
    self->current_enemy->intent_damage = 8 + g_random_int_range (0, 5);
    self->current_enemy->intent_block = 0;
}
```

## Extending the Example

### Add More Card Types

```c
/* Card that draws more cards */
card_new ("Adrenaline", "Draw 2 cards. Exhaust.",
          CARD_TYPE_SKILL, 0, 0, 0, 2, TRUE);

/* Power card (permanent effect) */
card_new ("Demon Form", "At the start of each turn, gain 2 Strength.",
          CARD_TYPE_POWER, 3, 0, 0, 0, FALSE);
```

### Add Status Effects

```c
typedef struct {
    gint strength;    /* +damage per attack */
    gint dexterity;   /* +block per skill */
    gint vulnerable;  /* Takes 50% more damage */
    gint weak;        /* Deals 25% less damage */
} StatusEffects;
```

## Related Documentation

- [LrgDeckbuilderCombatTemplate](../templates/deckbuilder-template.md) - Combat variant template
- [LrgDeckMixin](../systems/mixins.md) - Deck management interface
- [Object Pooling](../systems/object-pool.md) - Pool cards for performance
