/* game-deckbuilder-poker.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Scoring Deckbuilder Demo - Balatro Style
 *
 * This example demonstrates the scoring deckbuilder module with full
 * mouse navigation support. Click cards to select them (up to 5),
 * click Play Hand to score, or click Discard to replace selected cards.
 */

#define LIBREGNUM_INSIDE
#include <libregnum.h>
#include <graylib.h>

/* Window dimensions (1440p) */
#define WINDOW_WIDTH  2560
#define WINDOW_HEIGHT 1440

/* Layout constants (2.5x scale for 1440p) */
#define CARD_WIDTH      175
#define CARD_HEIGHT     250
#define CARD_SPACING    25
#define CARD_Y          1300
#define JOKER_WIDTH     200
#define JOKER_HEIGHT    250
#define JOKER_SPACING   38
#define JOKER_Y         200
#define BUTTON_WIDTH    325
#define BUTTON_HEIGHT   112
#define BUTTON_Y        1000
#define MAX_SELECTION   5
#define MAX_HAND_SIZE   8

/* Game state */
typedef enum {
    GAME_STATE_PLAYING,
    GAME_STATE_SCORING,
    GAME_STATE_ROUND_WIN,
    GAME_STATE_ROUND_LOSE,
    GAME_STATE_GAME_OVER
} PokerGameState;

/* GObject type definition */
#define DEMO_TYPE_POKER_GAME (demo_poker_game_get_type ())
G_DECLARE_FINAL_TYPE (DemoPokerGame, demo_poker_game, DEMO, POKER_GAME, GObject)

struct _DemoPokerGame
{
    GObject parent_instance;

    /* Core scoring state */
    LrgScoringManager *scoring_manager;
    LrgHand           *hand;

    /* Deck management */
    GPtrArray         *all_card_defs;   /* 52 card definitions */
    GPtrArray         *draw_pile;       /* LrgCardInstance refs */
    GPtrArray         *discard_pile;    /* LrgCardInstance refs */

    /* Jokers */
    GPtrArray         *joker_defs;
    GPtrArray         *jokers;          /* LrgJokerInstance */

    /* Round state */
    gint               round;
    gint64             current_score;
    gint64             target_score;
    gint               hands_remaining;
    gint               discards_remaining;
    PokerGameState     game_state;

    /* Last hand scoring info */
    LrgHandType        last_hand_type;
    gint64             last_chips;
    gint64             last_mult;
    gint64             last_score;

    /* UI state */
    gint               hovered_card;
    gboolean           hovered_play_button;
    gboolean           hovered_discard_button;

    /* Animation */
    gfloat             score_anim_timer;
    gchar             *message;
    gfloat             message_timer;

    /* UI Labels - reusable for text rendering */
    LrgLabel           *label_round;
    LrgLabel           *label_score;
    LrgLabel           *label_target;
    LrgLabel           *label_hands;
    LrgLabel           *label_discards;
    LrgLabel           *label_breakdown;
    LrgLabel           *label_last_hand;
    LrgLabel           *label_preview;
    LrgLabel           *label_play_button;
    LrgLabel           *label_discard_button;
    LrgLabel           *label_state;
    LrgLabel           *label_state_info;
    LrgLabel           *label_message;
    LrgLabel           *label_instructions1;
    LrgLabel           *label_instructions2;

    /* Pool of reusable labels for cards/jokers */
    GPtrArray          *label_pool;
    guint               label_pool_index;
};

G_DEFINE_TYPE (DemoPokerGame, demo_poker_game, G_TYPE_OBJECT)

/* Forward declarations */
static void demo_poker_game_create_deck (DemoPokerGame *self);
static void demo_poker_game_create_jokers (DemoPokerGame *self);
static void demo_poker_game_start_round (DemoPokerGame *self);
static void demo_poker_game_shuffle_deck (DemoPokerGame *self);
static void demo_poker_game_deal_hand (DemoPokerGame *self);
static void demo_poker_game_play_hand (DemoPokerGame *self);
static void demo_poker_game_discard_selected (DemoPokerGame *self);
static void demo_poker_game_draw (DemoPokerGame *self);
static void demo_poker_game_handle_input (DemoPokerGame *self);
static void demo_poker_game_update (DemoPokerGame *self, gfloat delta);
static void demo_poker_game_set_message (DemoPokerGame *self, const gchar *msg);
static const gchar * demo_poker_game_get_hand_name (LrgHandType type);

/*
 * draw_label:
 *
 * Helper to configure and draw a label in one call.
 */
static void
draw_label (LrgLabel       *label,
            const gchar    *text,
            gfloat          x,
            gfloat          y,
            gfloat          font_size,
            const GrlColor *color)
{
    lrg_label_set_text (label, text);
    lrg_widget_set_position (LRG_WIDGET (label), x, y);
    lrg_label_set_font_size (label, font_size);
    lrg_label_set_color (label, color);
    lrg_widget_draw (LRG_WIDGET (label));
}

/*
 * get_pool_label:
 *
 * Get a label from the reusable pool.
 */
static LrgLabel *
get_pool_label (DemoPokerGame *self)
{
    LrgLabel *label;

    if (self->label_pool_index >= self->label_pool->len)
        return g_ptr_array_index (self->label_pool, self->label_pool->len - 1);

    label = g_ptr_array_index (self->label_pool, self->label_pool_index);
    self->label_pool_index++;
    return label;
}

/*
 * reset_label_pool:
 *
 * Reset the pool index at the start of each draw frame.
 */
static void
reset_label_pool (DemoPokerGame *self)
{
    self->label_pool_index = 0;
}

/*
 * demo_poker_game_dispose:
 *
 * Clean up references when the game object is disposed.
 */
static void
demo_poker_game_dispose (GObject *object)
{
    DemoPokerGame *self = DEMO_POKER_GAME (object);

    g_clear_object (&self->scoring_manager);
    g_clear_object (&self->hand);
    g_clear_pointer (&self->all_card_defs, g_ptr_array_unref);
    g_clear_pointer (&self->draw_pile, g_ptr_array_unref);
    g_clear_pointer (&self->discard_pile, g_ptr_array_unref);
    g_clear_pointer (&self->joker_defs, g_ptr_array_unref);
    g_clear_pointer (&self->jokers, g_ptr_array_unref);
    g_clear_pointer (&self->message, g_free);

    /* Clean up UI labels */
    g_clear_object (&self->label_round);
    g_clear_object (&self->label_score);
    g_clear_object (&self->label_target);
    g_clear_object (&self->label_hands);
    g_clear_object (&self->label_discards);
    g_clear_object (&self->label_breakdown);
    g_clear_object (&self->label_last_hand);
    g_clear_object (&self->label_preview);
    g_clear_object (&self->label_play_button);
    g_clear_object (&self->label_discard_button);
    g_clear_object (&self->label_state);
    g_clear_object (&self->label_state_info);
    g_clear_object (&self->label_message);
    g_clear_object (&self->label_instructions1);
    g_clear_object (&self->label_instructions2);
    g_clear_pointer (&self->label_pool, g_ptr_array_unref);

    G_OBJECT_CLASS (demo_poker_game_parent_class)->dispose (object);
}

/*
 * demo_poker_game_class_init:
 *
 * Initialize the class structure.
 */
static void
demo_poker_game_class_init (DemoPokerGameClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = demo_poker_game_dispose;
}

/*
 * demo_poker_game_init:
 *
 * Initialize a new game instance.
 */
static void
demo_poker_game_init (DemoPokerGame *self)
{
    guint i;

    self->scoring_manager = lrg_scoring_manager_new ();
    self->hand = lrg_hand_new_with_size (MAX_HAND_SIZE);
    self->all_card_defs = g_ptr_array_new_with_free_func (g_object_unref);
    self->draw_pile = g_ptr_array_new_with_free_func (g_object_unref);
    self->discard_pile = g_ptr_array_new_with_free_func (g_object_unref);
    self->joker_defs = g_ptr_array_new_with_free_func (g_object_unref);
    self->jokers = g_ptr_array_new_with_free_func (g_object_unref);

    self->round = 1;
    self->game_state = GAME_STATE_PLAYING;
    self->hovered_card = -1;
    self->score_anim_timer = 0.0f;
    self->message = NULL;
    self->message_timer = 0.0f;

    /* Create UI labels for fixed text elements */
    self->label_round = lrg_label_new (NULL);
    self->label_score = lrg_label_new (NULL);
    self->label_target = lrg_label_new (NULL);
    self->label_hands = lrg_label_new (NULL);
    self->label_discards = lrg_label_new (NULL);
    self->label_breakdown = lrg_label_new (NULL);
    self->label_last_hand = lrg_label_new (NULL);
    self->label_preview = lrg_label_new (NULL);
    self->label_play_button = lrg_label_new (NULL);
    self->label_discard_button = lrg_label_new (NULL);
    self->label_state = lrg_label_new (NULL);
    self->label_state_info = lrg_label_new (NULL);
    self->label_message = lrg_label_new (NULL);
    self->label_instructions1 = lrg_label_new (NULL);
    self->label_instructions2 = lrg_label_new (NULL);

    /* Create pool of reusable labels for cards/jokers (50 should be plenty) */
    self->label_pool = g_ptr_array_new_with_free_func (g_object_unref);
    for (i = 0; i < 50; i++)
    {
        g_ptr_array_add (self->label_pool, lrg_label_new (NULL));
    }
    self->label_pool_index = 0;
}

/*
 * demo_poker_game_new:
 *
 * Create a new poker demo game.
 */
static DemoPokerGame *
demo_poker_game_new (void)
{
    DemoPokerGame *self = g_object_new (DEMO_TYPE_POKER_GAME, NULL);

    demo_poker_game_create_deck (self);
    demo_poker_game_create_jokers (self);
    demo_poker_game_start_round (self);

    return self;
}

/*
 * demo_poker_game_create_deck:
 *
 * Create a standard 52-card deck with proper suits, ranks, and chip values.
 */
static void
demo_poker_game_create_deck (DemoPokerGame *self)
{
    const gchar *suit_names[] = { "Hearts", "Diamonds", "Clubs", "Spades" };
    LrgCardSuit suits[] = {
        LRG_CARD_SUIT_HEARTS,
        LRG_CARD_SUIT_DIAMONDS,
        LRG_CARD_SUIT_CLUBS,
        LRG_CARD_SUIT_SPADES
    };
    const gchar *rank_names[] = {
        "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    gint chip_values[] = {
        11, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 10, 10
    };
    guint s;
    guint r;

    for (s = 0; s < 4; s++)
    {
        for (r = 0; r < 13; r++)
        {
            LrgCardDef *card;
            g_autofree gchar *id = NULL;
            g_autofree gchar *name = NULL;

            id = g_strdup_printf ("%s_%s", rank_names[r], suit_names[s]);
            name = g_strdup_printf ("%s of %s", rank_names[r], suit_names[s]);

            card = lrg_card_def_new (id);
            lrg_card_def_set_name (card, name);
            /* Playing cards don't need combat type - use skill as placeholder */
            lrg_card_def_set_card_type (card, LRG_CARD_TYPE_SKILL);
            lrg_card_def_set_suit (card, suits[s]);
            lrg_card_def_set_rank (card, (LrgCardRank)(r + 1));
            lrg_card_def_set_chip_value (card, chip_values[r]);

            g_ptr_array_add (self->all_card_defs, card);
        }
    }
}

/*
 * demo_poker_game_create_jokers:
 *
 * Create joker definitions and add starting jokers.
 */
static void
demo_poker_game_create_jokers (DemoPokerGame *self)
{
    LrgJokerDef *joker_def;
    LrgJokerInstance *joker;

    /* Greedy Joker - +4 Mult always */
    joker_def = lrg_joker_def_new ("greedy", "Greedy Joker");
    lrg_joker_def_set_description (joker_def, "+4 Mult");
    lrg_joker_def_set_rarity (joker_def, LRG_JOKER_RARITY_COMMON);
    lrg_joker_def_set_plus_mult (joker_def, 4);
    g_ptr_array_add (self->joker_defs, joker_def);

    joker = lrg_joker_instance_new (joker_def);
    g_ptr_array_add (self->jokers, joker);
    lrg_scoring_manager_add_joker (self->scoring_manager, joker);

    /* Lusty Joker - +30 Chips always */
    joker_def = lrg_joker_def_new ("lusty", "Lusty Joker");
    lrg_joker_def_set_description (joker_def, "+30 Chips");
    lrg_joker_def_set_rarity (joker_def, LRG_JOKER_RARITY_COMMON);
    lrg_joker_def_set_plus_chips (joker_def, 30);
    g_ptr_array_add (self->joker_defs, joker_def);

    joker = lrg_joker_instance_new (joker_def);
    g_ptr_array_add (self->jokers, joker);
    lrg_scoring_manager_add_joker (self->scoring_manager, joker);
}

/*
 * demo_poker_game_start_round:
 *
 * Start a new round with appropriate target score.
 */
static void
demo_poker_game_start_round (DemoPokerGame *self)
{
    /* Target score increases each round */
    self->target_score = 300 * self->round + (self->round - 1) * 100;
    self->current_score = 0;
    self->hands_remaining = 4;
    self->discards_remaining = 3;
    self->game_state = GAME_STATE_PLAYING;

    /* Reset last hand info */
    self->last_hand_type = LRG_HAND_TYPE_NONE;
    self->last_chips = 0;
    self->last_mult = 0;
    self->last_score = 0;

    lrg_scoring_manager_start_round (self->scoring_manager,
                                     self->target_score,
                                     self->hands_remaining,
                                     self->discards_remaining);

    /* Clear hand and rebuild deck */
    lrg_hand_clear (self->hand);
    g_ptr_array_set_size (self->draw_pile, 0);
    g_ptr_array_set_size (self->discard_pile, 0);

    demo_poker_game_shuffle_deck (self);
    demo_poker_game_deal_hand (self);

    {
        g_autofree gchar *msg = g_strdup_printf ("Round %d - Score %lld to win!",
            self->round, (long long)self->target_score);
        demo_poker_game_set_message (self, msg);
    }
}

/*
 * demo_poker_game_shuffle_deck:
 *
 * Create card instances and shuffle into the draw pile.
 */
static void
demo_poker_game_shuffle_deck (DemoPokerGame *self)
{
    guint i;
    guint n;
    GRand *rng;

    /* Create instances of all 52 cards */
    for (i = 0; i < self->all_card_defs->len; i++)
    {
        LrgCardDef *def = g_ptr_array_index (self->all_card_defs, i);
        LrgCardInstance *card = lrg_card_instance_new (def);
        g_ptr_array_add (self->draw_pile, card);
    }

    /* Fisher-Yates shuffle */
    rng = g_rand_new ();
    n = self->draw_pile->len;
    for (i = n - 1; i > 0; i--)
    {
        guint j = g_rand_int_range (rng, 0, (gint32)(i + 1));
        gpointer tmp = self->draw_pile->pdata[i];
        self->draw_pile->pdata[i] = self->draw_pile->pdata[j];
        self->draw_pile->pdata[j] = tmp;
    }
    g_rand_free (rng);
}

/*
 * demo_poker_game_deal_hand:
 *
 * Deal cards to fill hand up to MAX_HAND_SIZE.
 */
static void
demo_poker_game_deal_hand (DemoPokerGame *self)
{
    while (lrg_hand_get_count (self->hand) < MAX_HAND_SIZE && self->draw_pile->len > 0)
    {
        /* If draw pile is empty, shuffle discard into draw */
        if (self->draw_pile->len == 0 && self->discard_pile->len > 0)
        {
            guint i;
            for (i = 0; i < self->discard_pile->len; i++)
            {
                LrgCardInstance *card = g_object_ref (g_ptr_array_index (self->discard_pile, i));
                g_ptr_array_add (self->draw_pile, card);
            }
            g_ptr_array_set_size (self->discard_pile, 0);

            /* Shuffle */
            {
                GRand *rng = g_rand_new ();
                guint n = self->draw_pile->len;
                for (i = n - 1; i > 0; i--)
                {
                    guint j = g_rand_int_range (rng, 0, (gint32)(i + 1));
                    gpointer tmp = self->draw_pile->pdata[i];
                    self->draw_pile->pdata[i] = self->draw_pile->pdata[j];
                    self->draw_pile->pdata[j] = tmp;
                }
                g_rand_free (rng);
            }
        }

        if (self->draw_pile->len > 0)
        {
            /* Take card from top of draw pile (end of array) */
            LrgCardInstance *card = g_ptr_array_index (self->draw_pile, self->draw_pile->len - 1);
            g_object_ref (card);
            g_ptr_array_remove_index (self->draw_pile, self->draw_pile->len - 1);
            lrg_hand_add (self->hand, card);
        }
    }
}

/*
 * demo_poker_game_set_message:
 *
 * Set a temporary message to display.
 */
static void
demo_poker_game_set_message (DemoPokerGame *self,
                             const gchar   *msg)
{
    g_clear_pointer (&self->message, g_free);
    self->message = g_strdup (msg);
    self->message_timer = 3.0f;
}

/*
 * demo_poker_game_get_hand_name:
 *
 * Get the display name for a hand type.
 */
static const gchar *
demo_poker_game_get_hand_name (LrgHandType type)
{
    switch (type)
    {
        case LRG_HAND_TYPE_HIGH_CARD:       return "High Card";
        case LRG_HAND_TYPE_PAIR:            return "Pair";
        case LRG_HAND_TYPE_TWO_PAIR:        return "Two Pair";
        case LRG_HAND_TYPE_THREE_OF_A_KIND: return "Three of a Kind";
        case LRG_HAND_TYPE_STRAIGHT:        return "Straight";
        case LRG_HAND_TYPE_FLUSH:           return "Flush";
        case LRG_HAND_TYPE_FULL_HOUSE:      return "Full House";
        case LRG_HAND_TYPE_FOUR_OF_A_KIND:  return "Four of a Kind";
        case LRG_HAND_TYPE_STRAIGHT_FLUSH:  return "Straight Flush";
        case LRG_HAND_TYPE_ROYAL_FLUSH:     return "Royal Flush";
        case LRG_HAND_TYPE_FIVE_OF_A_KIND:  return "Five of a Kind";
        default:                            return "Unknown";
    }
}

/*
 * demo_poker_game_play_hand:
 *
 * Play the selected cards and score them.
 */
static void
demo_poker_game_play_hand (DemoPokerGame *self)
{
    GPtrArray *selected;
    LrgScoringContext *ctx;
    gint64 score;
    guint i;

    selected = lrg_hand_get_selected (self->hand);

    if (selected->len == 0)
    {
        demo_poker_game_set_message (self, "Select cards to play!");
        return;
    }

    if (self->hands_remaining <= 0)
    {
        demo_poker_game_set_message (self, "No hands remaining!");
        return;
    }

    /* Score the hand */
    score = lrg_scoring_manager_play_hand (self->scoring_manager, selected);
    ctx = lrg_scoring_manager_get_last_context (self->scoring_manager);

    /* Store scoring info for display */
    self->last_hand_type = lrg_scoring_context_get_hand_type (ctx);
    self->last_chips = lrg_scoring_context_get_total_chips (ctx);
    self->last_mult = lrg_scoring_context_get_total_mult (ctx);
    self->last_score = score;

    self->current_score += score;
    self->hands_remaining--;
    self->score_anim_timer = 1.5f;

    /* Move played cards to discard */
    for (i = 0; i < selected->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (selected, i);
        g_object_ref (card);
        lrg_hand_remove (self->hand, card);
        g_ptr_array_add (self->discard_pile, card);
    }

    /* Clear selection */
    lrg_hand_clear_selection (self->hand);

    /* Check win condition */
    if (self->current_score >= self->target_score)
    {
        self->game_state = GAME_STATE_ROUND_WIN;
        demo_poker_game_set_message (self, "Round Complete!");
        return;
    }

    /* Check lose condition */
    if (self->hands_remaining <= 0)
    {
        self->game_state = GAME_STATE_ROUND_LOSE;
        demo_poker_game_set_message (self, "Out of hands - Round Lost!");
        return;
    }

    /* Deal more cards */
    demo_poker_game_deal_hand (self);
}

/*
 * demo_poker_game_discard_selected:
 *
 * Discard selected cards and draw replacements.
 */
static void
demo_poker_game_discard_selected (DemoPokerGame *self)
{
    GPtrArray *selected;
    guint i;

    selected = lrg_hand_get_selected (self->hand);

    if (selected->len == 0)
    {
        demo_poker_game_set_message (self, "Select cards to discard!");
        return;
    }

    if (self->discards_remaining <= 0)
    {
        demo_poker_game_set_message (self, "No discards remaining!");
        return;
    }

    lrg_scoring_manager_discard (self->scoring_manager, selected);
    self->discards_remaining--;

    /* Move discarded cards to discard pile */
    for (i = 0; i < selected->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (selected, i);
        g_object_ref (card);
        lrg_hand_remove (self->hand, card);
        g_ptr_array_add (self->discard_pile, card);
    }

    /* Clear selection */
    lrg_hand_clear_selection (self->hand);

    /* Deal replacements */
    demo_poker_game_deal_hand (self);

    demo_poker_game_set_message (self, "Cards discarded!");
}

/*
 * point_in_rect:
 *
 * Check if a point is inside a rectangle.
 */
static gboolean
point_in_rect (gint px, gint py,
               gint rx, gint ry, gint rw, gint rh)
{
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

/*
 * demo_poker_game_get_card_x:
 *
 * Calculate the X position for a card in hand.
 */
static gint
demo_poker_game_get_card_x (DemoPokerGame *self, gint index)
{
    guint card_count = lrg_hand_get_count (self->hand);
    gint total_width;
    gint start_x;

    if (card_count == 0)
        return 0;

    total_width = (gint)card_count * CARD_WIDTH + ((gint)card_count - 1) * CARD_SPACING;
    start_x = (WINDOW_WIDTH - total_width) / 2;

    return start_x + index * (CARD_WIDTH + CARD_SPACING);
}

/*
 * demo_poker_game_handle_input:
 *
 * Process mouse input for card selection.
 */
static void
demo_poker_game_handle_input (DemoPokerGame *self)
{
    gint mx;
    gint my;
    gboolean clicked;
    guint card_count;
    guint i;
    GPtrArray *selected;

    mx = grl_input_get_mouse_x ();
    my = grl_input_get_mouse_y ();
    clicked = grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT);

    /* Reset hover states */
    self->hovered_card = -1;
    self->hovered_play_button = FALSE;
    self->hovered_discard_button = FALSE;

    /* Handle round end states */
    if (self->game_state == GAME_STATE_ROUND_WIN)
    {
        /* Check for click to continue */
        if (clicked)
        {
            self->round++;
            demo_poker_game_start_round (self);
        }
        return;
    }
    else if (self->game_state == GAME_STATE_ROUND_LOSE)
    {
        /* Check for click to restart */
        if (clicked)
        {
            self->round = 1;
            demo_poker_game_start_round (self);
        }
        return;
    }

    /* Check Play Hand button */
    {
        gint play_x = WINDOW_WIDTH / 2 - BUTTON_WIDTH - 20;
        if (point_in_rect (mx, my, play_x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT))
        {
            self->hovered_play_button = TRUE;
            if (clicked)
            {
                demo_poker_game_play_hand (self);
                return;
            }
        }
    }

    /* Check Discard button */
    {
        gint discard_x = WINDOW_WIDTH / 2 + 20;
        if (point_in_rect (mx, my, discard_x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT))
        {
            self->hovered_discard_button = TRUE;
            if (clicked)
            {
                demo_poker_game_discard_selected (self);
                return;
            }
        }
    }

    /* Check card hovers and clicks */
    card_count = lrg_hand_get_count (self->hand);
    for (i = 0; i < card_count; i++)
    {
        gint card_x = demo_poker_game_get_card_x (self, (gint)i);
        gint card_y = CARD_Y;
        LrgCardInstance *card = lrg_hand_get_card_at (self->hand, i);

        /* Raise selected cards */
        if (lrg_hand_is_selected (self->hand, card))
            card_y -= 75;
        else if ((gint)i == self->hovered_card)
            card_y -= 12;

        if (point_in_rect (mx, my, card_x, card_y, CARD_WIDTH, CARD_HEIGHT))
        {
            self->hovered_card = (gint)i;

            if (clicked)
            {
                selected = lrg_hand_get_selected (self->hand);

                if (lrg_hand_is_selected (self->hand, card))
                {
                    /* Deselect */
                    lrg_hand_deselect (self->hand, card);
                }
                else if (selected->len < MAX_SELECTION)
                {
                    /* Select */
                    lrg_hand_select (self->hand, card);
                }
                else
                {
                    demo_poker_game_set_message (self, "Max 5 cards selected!");
                }
            }
            break;
        }
    }
}

/*
 * demo_poker_game_update:
 *
 * Update game state each frame.
 */
static void
demo_poker_game_update (DemoPokerGame *self, gfloat delta)
{
    /* Update message timer */
    if (self->message_timer > 0.0f)
    {
        self->message_timer -= delta;
        if (self->message_timer <= 0.0f)
        {
            g_clear_pointer (&self->message, g_free);
            self->message_timer = 0.0f;
        }
    }

    /* Update score animation */
    if (self->score_anim_timer > 0.0f)
    {
        self->score_anim_timer -= delta;
    }
}

/*
 * demo_poker_game_get_suit_char:
 *
 * Get the suit character for display.
 */
static gchar
demo_poker_game_get_suit_char (LrgCardSuit suit)
{
    switch (suit)
    {
        case LRG_CARD_SUIT_HEARTS:   return 'H';
        case LRG_CARD_SUIT_DIAMONDS: return 'D';
        case LRG_CARD_SUIT_CLUBS:    return 'C';
        case LRG_CARD_SUIT_SPADES:   return 'S';
        default:                     return '?';
    }
}

/*
 * demo_poker_game_get_rank_str:
 *
 * Get the rank string for display.
 */
static const gchar *
demo_poker_game_get_rank_str (LrgCardRank rank)
{
    static const gchar *ranks[] = {
        "?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    if (rank >= 1 && rank <= 13)
        return ranks[rank];
    return "?";
}

/*
 * demo_poker_game_draw_card:
 *
 * Draw a playing card at the specified position.
 */
static void
demo_poker_game_draw_card (DemoPokerGame   *self,
                           LrgCardInstance *card,
                           gint             x,
                           gint             y,
                           gboolean         is_hovered,
                           gboolean         is_selected)
{
    LrgCardDef *def;
    LrgCardSuit suit;
    LrgCardRank rank;
    gint chip_value;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) suit_color = NULL;
    g_autoptr(GrlColor) border_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;
    g_autofree gchar *suit_str = NULL;
    g_autofree gchar *chip_str = NULL;
    const gchar *rank_str;

    def = lrg_card_instance_get_def (card);
    suit = lrg_card_def_get_suit (def);
    rank = lrg_card_def_get_rank (def);
    chip_value = lrg_card_def_get_chip_value (def);
    rank_str = demo_poker_game_get_rank_str (rank);

    /* Adjust for selection/hover */
    if (is_selected)
        y -= 30;
    else if (is_hovered)
        y -= 5;

    /* Card background */
    bg_color = grl_color_new (240, 235, 220, 255);
    border_color = is_selected ? grl_color_new (255, 200, 0, 255)
                               : grl_color_new (60, 60, 60, 255);

    grl_draw_rectangle (x, y, CARD_WIDTH, CARD_HEIGHT, bg_color);
    grl_draw_rectangle_lines (x, y, CARD_WIDTH, CARD_HEIGHT, border_color);

    /* Suit color */
    if (suit == LRG_CARD_SUIT_HEARTS || suit == LRG_CARD_SUIT_DIAMONDS)
    {
        suit_color = grl_color_new (200, 40, 40, 255);
    }
    else
    {
        suit_color = grl_color_new (30, 30, 30, 255);
    }

    text_color = grl_color_new (40, 40, 40, 255);

    /* Draw rank in corner */
    draw_label (get_pool_label (self), rank_str, x + 12, y + 12, 45, suit_color);

    /* Draw suit character */
    suit_str = g_strdup_printf ("%c", demo_poker_game_get_suit_char (suit));
    draw_label (get_pool_label (self), suit_str, x + 12, y + 62, 35, suit_color);

    /* Draw chip value at bottom */
    chip_str = g_strdup_printf ("+%d", chip_value);
    draw_label (get_pool_label (self), chip_str, x + CARD_WIDTH / 2 - 25, y + CARD_HEIGHT - 50, 30, text_color);
}

/*
 * demo_poker_game_draw_joker:
 *
 * Draw a joker card.
 */
static void
demo_poker_game_draw_joker (DemoPokerGame    *self,
                            LrgJokerInstance *joker,
                            gint              x,
                            gint              y)
{
    LrgJokerDef *def;
    const gchar *name;
    const gchar *desc;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) border_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;

    def = lrg_joker_instance_get_def (joker);
    name = lrg_joker_def_get_name (def);
    desc = lrg_joker_def_get_description (def, joker);

    bg_color = grl_color_new (100, 80, 140, 255);
    border_color = grl_color_new (200, 180, 255, 255);
    text_color = grl_color_new (255, 255, 255, 255);

    grl_draw_rectangle (x, y, JOKER_WIDTH, JOKER_HEIGHT, bg_color);
    grl_draw_rectangle_lines (x, y, JOKER_WIDTH, JOKER_HEIGHT, border_color);

    draw_label (get_pool_label (self), name, x + 12, y + 25, 30, text_color);
    draw_label (get_pool_label (self), desc, x + 12, y + 100, 35, text_color);
}

/*
 * demo_poker_game_draw:
 *
 * Render the entire game screen.
 */
static void
demo_poker_game_draw (DemoPokerGame *self)
{
    guint i;
    guint card_count;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;
    g_autoptr(GrlColor) score_color = NULL;
    g_autoptr(GrlColor) target_color = NULL;
    g_autoptr(GrlColor) info_color = NULL;
    g_autoptr(GrlColor) button_color = NULL;
    g_autoptr(GrlColor) button_hover = NULL;
    g_autoptr(GrlColor) button_disabled = NULL;
    g_autoptr(GrlColor) hand_type_color = NULL;
    g_autoptr(GrlColor) msg_color = NULL;
    g_autofree gchar *round_str = NULL;
    g_autofree gchar *score_str = NULL;
    g_autofree gchar *target_str = NULL;
    g_autofree gchar *hands_str = NULL;
    g_autofree gchar *discards_str = NULL;

    bg_color = grl_color_new (25, 40, 25, 255);
    text_color = grl_color_new (255, 255, 255, 255);
    score_color = grl_color_new (255, 220, 100, 255);
    target_color = grl_color_new (150, 255, 150, 255);
    info_color = grl_color_new (180, 180, 180, 255);
    button_color = grl_color_new (60, 100, 60, 255);
    button_hover = grl_color_new (80, 140, 80, 255);
    button_disabled = grl_color_new (80, 80, 80, 255);
    hand_type_color = grl_color_new (255, 200, 100, 255);
    msg_color = grl_color_new (255, 255, 150, 255);

    grl_draw_clear_background (bg_color);

    /* Reset label pool at the start of each frame */
    reset_label_pool (self);

    /* Draw header info */
    round_str = g_strdup_printf ("Round %d", self->round);
    draw_label (self->label_round, round_str, 50, 37, 60, text_color);

    score_str = g_strdup_printf ("Score: %lld", (long long)self->current_score);
    draw_label (self->label_score, score_str, 50, 112, 50, score_color);

    target_str = g_strdup_printf ("Target: %lld", (long long)self->target_score);
    draw_label (self->label_target, target_str, 500, 112, 50, target_color);

    hands_str = g_strdup_printf ("Hands: %d", self->hands_remaining);
    draw_label (self->label_hands, hands_str, WINDOW_WIDTH - 375, 37, 45, info_color);

    discards_str = g_strdup_printf ("Discards: %d", self->discards_remaining);
    draw_label (self->label_discards, discards_str, WINDOW_WIDTH - 375, 100, 45, info_color);

    /* Draw jokers */
    {
        guint joker_count = self->jokers->len;
        gint total_width = (gint)joker_count * JOKER_WIDTH + ((gint)joker_count - 1) * JOKER_SPACING;
        gint start_x = (WINDOW_WIDTH - total_width) / 2;

        for (i = 0; i < joker_count; i++)
        {
            LrgJokerInstance *joker = g_ptr_array_index (self->jokers, i);
            gint jx = start_x + (gint)i * (JOKER_WIDTH + JOKER_SPACING);
            demo_poker_game_draw_joker (self, joker, jx, JOKER_Y);
        }
    }

    /* Draw last hand scoring info */
    if (self->last_hand_type != LRG_HAND_TYPE_NONE && self->score_anim_timer > 0.0f)
    {
        const gchar *hand_name = demo_poker_game_get_hand_name (self->last_hand_type);
        g_autofree gchar *breakdown = g_strdup_printf ("%s: %lld x %lld = %lld",
            hand_name,
            (long long)self->last_chips,
            (long long)self->last_mult,
            (long long)self->last_score);

        draw_label (self->label_breakdown, breakdown, WINDOW_WIDTH / 2 - 375, 625, 55, hand_type_color);
    }
    else if (self->last_hand_type != LRG_HAND_TYPE_NONE)
    {
        const gchar *hand_name = demo_poker_game_get_hand_name (self->last_hand_type);
        g_autofree gchar *last_str = g_strdup_printf ("Last: %s (+%lld)",
            hand_name, (long long)self->last_score);
        draw_label (self->label_last_hand, last_str, WINDOW_WIDTH / 2 - 250, 625, 45, info_color);
    }

    /* Draw preview if cards selected */
    {
        GPtrArray *selected = lrg_hand_get_selected (self->hand);
        if (selected->len > 0)
        {
            LrgHandType preview_type = lrg_scoring_manager_evaluate_hand (
                self->scoring_manager, selected);
            const gchar *preview_name = demo_poker_game_get_hand_name (preview_type);

            g_autofree gchar *preview_str = g_strdup_printf ("Preview: %s", preview_name);
            draw_label (self->label_preview, preview_str, WINDOW_WIDTH / 2 - 200, 725, 40, info_color);
        }
    }

    /* Draw buttons */
    if (self->game_state == GAME_STATE_PLAYING)
    {
        gint play_x = WINDOW_WIDTH / 2 - BUTTON_WIDTH - 50;
        gint discard_x = WINDOW_WIDTH / 2 + 50;

        /* Play Hand button */
        {
            GrlColor *btn_color = self->hands_remaining > 0
                ? (self->hovered_play_button ? button_hover : button_color)
                : button_disabled;
            grl_draw_rectangle (play_x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, btn_color);
            draw_label (self->label_play_button, "Play Hand", play_x + 50, BUTTON_Y + 30, 45, text_color);
        }

        /* Discard button */
        {
            GrlColor *btn_color = self->discards_remaining > 0
                ? (self->hovered_discard_button ? button_hover : button_color)
                : button_disabled;
            grl_draw_rectangle (discard_x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, btn_color);
            draw_label (self->label_discard_button, "Discard", discard_x + 75, BUTTON_Y + 30, 45, text_color);
        }
    }

    /* Draw cards in hand */
    card_count = lrg_hand_get_count (self->hand);
    for (i = 0; i < card_count; i++)
    {
        LrgCardInstance *card = lrg_hand_get_card_at (self->hand, i);
        gint cx = demo_poker_game_get_card_x (self, (gint)i);
        gboolean hovered = ((gint)i == self->hovered_card);
        gboolean selected = lrg_hand_is_selected (self->hand, card);
        demo_poker_game_draw_card (self, card, cx, CARD_Y, hovered, selected);
    }

    /* Draw round end overlays */
    if (self->game_state == GAME_STATE_ROUND_WIN)
    {
        g_autoptr(GrlColor) overlay = grl_color_new (0, 100, 0, 200);
        g_autoptr(GrlColor) win_text = grl_color_new (255, 255, 100, 255);

        grl_draw_rectangle (WINDOW_WIDTH / 2 - 375, 800, 750, 250, overlay);
        draw_label (self->label_state, "ROUND COMPLETE!", WINDOW_WIDTH / 2 - 250, 862, 60, win_text);
        draw_label (self->label_state_info, "Click to continue", WINDOW_WIDTH / 2 - 200, 950, 40, text_color);
    }
    else if (self->game_state == GAME_STATE_ROUND_LOSE)
    {
        g_autoptr(GrlColor) overlay = grl_color_new (100, 0, 0, 200);
        g_autoptr(GrlColor) lose_text = grl_color_new (255, 100, 100, 255);

        grl_draw_rectangle (WINDOW_WIDTH / 2 - 375, 800, 750, 250, overlay);
        draw_label (self->label_state, "ROUND FAILED!", WINDOW_WIDTH / 2 - 212, 862, 60, lose_text);
        draw_label (self->label_state_info, "Click to restart", WINDOW_WIDTH / 2 - 175, 950, 40, text_color);
    }

    /* Draw message */
    if (self->message != NULL && self->message_timer > 0.0f)
    {
        draw_label (self->label_message, self->message, 50, WINDOW_HEIGHT - 75, 40, msg_color);
    }

    /* Draw instructions */
    {
        g_autoptr(GrlColor) instr_color = grl_color_new (150, 150, 150, 255);
        draw_label (self->label_instructions1,
                    "Click cards to select (max 5), then Play Hand or Discard",
                    50, 1150, 30, instr_color);
        draw_label (self->label_instructions2,
                    "Score chips x mult to reach target before running out of hands",
                    50, 1190, 30, instr_color);
    }
}

/*
 * main:
 *
 * Entry point for the scoring deckbuilder demo.
 */
int
main (int    argc,
      char **argv)
{
    g_autoptr(GrlWindow) window = NULL;
    g_autoptr(DemoPokerGame) game = NULL;

    /* Create window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "Scoring Deckbuilder Demo");
    grl_window_set_target_fps (window, 60);

    /* Create game */
    game = demo_poker_game_new ();

    /* Main loop */
    while (!grl_window_should_close (window))
    {
        gfloat delta = grl_window_get_frame_time (window);

        /* Handle input */
        demo_poker_game_handle_input (game);

        /* Update game state */
        demo_poker_game_update (game, delta);

        /* Render */
        grl_window_begin_drawing (window);
        demo_poker_game_draw (game);
        grl_window_end_drawing (window);
    }

    return 0;
}
