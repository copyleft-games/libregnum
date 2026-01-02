/* game-deckbuilder-combat.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Combat Deckbuilder Demo - Slay the Spire Style
 *
 * This example demonstrates the combat deckbuilder module with full
 * mouse navigation support. Click cards to select them, click enemies
 * to target attacks, and use the End Turn button to pass to enemies.
 */

#define LIBREGNUM_INSIDE
#include <libregnum.h>
#include <graylib.h>

/* Window dimensions (1440p) */
#define WINDOW_WIDTH  2560
#define WINDOW_HEIGHT 1440

/* Layout constants (2.5x scale for 1440p) */
#define CARD_WIDTH      250
#define CARD_HEIGHT     350
#define CARD_SPACING    38
#define CARD_Y          1100
#define ENEMY_WIDTH     250
#define ENEMY_HEIGHT    300
#define ENEMY_SPACING   75
#define ENEMY_Y         250
#define BUTTON_WIDTH    300
#define BUTTON_HEIGHT   100
#define BUTTON_Y        875

/* UI state */
typedef enum {
    UI_STATE_PLAYING,
    UI_STATE_SELECT_TARGET,
    UI_STATE_ENEMY_TURN,
    UI_STATE_VICTORY,
    UI_STATE_DEFEAT
} CombatUIState;

/* GObject type definition */
#define DEMO_TYPE_COMBAT_GAME (demo_combat_game_get_type ())
G_DECLARE_FINAL_TYPE (DemoCombatGame, demo_combat_game, DEMO, COMBAT_GAME, GObject)

struct _DemoCombatGame
{
    GObject parent_instance;

    /* Core combat state */
    LrgCombatManager   *combat_manager;
    LrgCombatContext   *combat_context;
    LrgPlayerCombatant *player;
    LrgHand            *hand;

    /* Card definitions */
    GPtrArray          *card_defs;

    /* Enemy definitions */
    GPtrArray          *enemy_defs;

    /* UI state */
    CombatUIState       ui_state;
    gint                hovered_card;
    gint                hovered_enemy;
    LrgCardInstance    *selected_card;
    gboolean            hovered_button;

    /* Message display */
    gchar              *message;
    gfloat              message_timer;

    /* Enemy turn animation */
    gint                current_enemy_action;
    gfloat              enemy_action_timer;

    /* UI Labels - reusable for text rendering */
    LrgLabel           *label_energy;
    LrgLabel           *label_hp;
    LrgLabel           *label_block;
    LrgLabel           *label_message;
    LrgLabel           *label_instructions1;
    LrgLabel           *label_instructions2;
    LrgLabel           *label_state;
    LrgLabel           *label_button;

    /* Pool of reusable labels for cards/enemies */
    GPtrArray          *label_pool;
    guint               label_pool_index;
};

G_DEFINE_TYPE (DemoCombatGame, demo_combat_game, G_TYPE_OBJECT)

/* Forward declarations */
static void demo_combat_game_create_card_defs (DemoCombatGame *self);
static void demo_combat_game_create_enemy_defs (DemoCombatGame *self);
static void demo_combat_game_init_combat (DemoCombatGame *self);
static void demo_combat_game_draw (DemoCombatGame *self);
static void demo_combat_game_handle_input (DemoCombatGame *self);
static void demo_combat_game_update (DemoCombatGame *self, gfloat delta);
static void demo_combat_game_set_message (DemoCombatGame *self, const gchar *msg);
static void demo_combat_game_start_enemy_turn (DemoCombatGame *self);
static void demo_combat_game_check_combat_end (DemoCombatGame *self);

/*
 * draw_label:
 *
 * Update a label's properties and draw it.
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
 * Get a label from the pool for temporary text rendering.
 * Pool resets at the start of each frame.
 */
static LrgLabel *
get_pool_label (DemoCombatGame *self)
{
    LrgLabel *label;

    if (self->label_pool_index >= self->label_pool->len)
    {
        /* Pool exhausted, return the last one (shouldn't happen) */
        return g_ptr_array_index (self->label_pool, self->label_pool->len - 1);
    }

    label = g_ptr_array_index (self->label_pool, self->label_pool_index);
    self->label_pool_index++;

    return label;
}

/*
 * reset_label_pool:
 *
 * Reset the label pool index for a new frame.
 */
static void
reset_label_pool (DemoCombatGame *self)
{
    self->label_pool_index = 0;
}

/*
 * demo_combat_game_dispose:
 *
 * Clean up references when the game object is disposed.
 */
static void
demo_combat_game_dispose (GObject *object)
{
    DemoCombatGame *self = DEMO_COMBAT_GAME (object);

    g_clear_object (&self->combat_manager);
    g_clear_object (&self->combat_context);
    g_clear_object (&self->player);
    g_clear_pointer (&self->card_defs, g_ptr_array_unref);
    g_clear_pointer (&self->enemy_defs, g_ptr_array_unref);
    g_clear_pointer (&self->message, g_free);

    /* Clean up labels */
    g_clear_object (&self->label_energy);
    g_clear_object (&self->label_hp);
    g_clear_object (&self->label_block);
    g_clear_object (&self->label_message);
    g_clear_object (&self->label_instructions1);
    g_clear_object (&self->label_instructions2);
    g_clear_object (&self->label_state);
    g_clear_object (&self->label_button);
    g_clear_pointer (&self->label_pool, g_ptr_array_unref);

    G_OBJECT_CLASS (demo_combat_game_parent_class)->dispose (object);
}

/*
 * demo_combat_game_class_init:
 *
 * Initialize the class structure.
 */
static void
demo_combat_game_class_init (DemoCombatGameClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = demo_combat_game_dispose;
}

/*
 * demo_combat_game_init:
 *
 * Initialize a new game instance.
 */
static void
demo_combat_game_init (DemoCombatGame *self)
{
    guint i;

    self->combat_manager = lrg_combat_manager_new ();
    self->card_defs = g_ptr_array_new_with_free_func (g_object_unref);
    self->enemy_defs = g_ptr_array_new_with_free_func (g_object_unref);
    self->ui_state = UI_STATE_PLAYING;
    self->hovered_card = -1;
    self->hovered_enemy = -1;
    self->selected_card = NULL;
    self->message = NULL;
    self->message_timer = 0.0f;
    self->current_enemy_action = 0;
    self->enemy_action_timer = 0.0f;

    /* Create UI labels */
    self->label_energy = lrg_label_new (NULL);
    self->label_hp = lrg_label_new (NULL);
    self->label_block = lrg_label_new (NULL);
    self->label_message = lrg_label_new (NULL);
    self->label_instructions1 = lrg_label_new (NULL);
    self->label_instructions2 = lrg_label_new (NULL);
    self->label_state = lrg_label_new (NULL);
    self->label_button = lrg_label_new (NULL);

    /* Create pool of reusable labels for cards and enemies */
    self->label_pool = g_ptr_array_new_with_free_func (g_object_unref);
    for (i = 0; i < 50; i++)
    {
        g_ptr_array_add (self->label_pool, lrg_label_new (NULL));
    }
    self->label_pool_index = 0;
}

/*
 * demo_combat_game_new:
 *
 * Create a new combat demo game and initialize combat.
 */
static DemoCombatGame *
demo_combat_game_new (void)
{
    DemoCombatGame *self = g_object_new (DEMO_TYPE_COMBAT_GAME, NULL);

    demo_combat_game_create_card_defs (self);
    demo_combat_game_create_enemy_defs (self);
    demo_combat_game_init_combat (self);

    return self;
}

/*
 * demo_combat_game_create_card_defs:
 *
 * Create the card definitions for the demo:
 * - Strike: 1 energy, deal 6 damage
 * - Defend: 1 energy, gain 5 block
 * - Bash: 2 energy, deal 8 damage, apply 2 Vulnerable
 */
static void
demo_combat_game_create_card_defs (DemoCombatGame *self)
{
    LrgCardDef *card;
    LrgCardEffect *effect;

    /* Strike - basic attack */
    card = lrg_card_def_new ("strike");
    lrg_card_def_set_name (card, "Strike");
    lrg_card_def_set_description (card, "Deal 6 damage.");
    lrg_card_def_set_card_type (card, LRG_CARD_TYPE_ATTACK);
    lrg_card_def_set_rarity (card, LRG_CARD_RARITY_STARTER);
    lrg_card_def_set_base_cost (card, 1);
    lrg_card_def_set_target_type (card, LRG_CARD_TARGET_SINGLE_ENEMY);

    effect = lrg_card_effect_new ("damage");
    lrg_card_effect_set_param_int (effect, "amount", 6);
    lrg_card_def_add_effect (card, effect);

    g_ptr_array_add (self->card_defs, card);

    /* Defend - basic block */
    card = lrg_card_def_new ("defend");
    lrg_card_def_set_name (card, "Defend");
    lrg_card_def_set_description (card, "Gain 5 Block.");
    lrg_card_def_set_card_type (card, LRG_CARD_TYPE_SKILL);
    lrg_card_def_set_rarity (card, LRG_CARD_RARITY_STARTER);
    lrg_card_def_set_base_cost (card, 1);
    lrg_card_def_set_target_type (card, LRG_CARD_TARGET_SELF);

    effect = lrg_card_effect_new ("block");
    lrg_card_effect_set_param_int (effect, "amount", 5);
    lrg_card_def_add_effect (card, effect);

    g_ptr_array_add (self->card_defs, card);

    /* Bash - attack with vulnerable */
    card = lrg_card_def_new ("bash");
    lrg_card_def_set_name (card, "Bash");
    lrg_card_def_set_description (card, "Deal 8 damage. Apply 2 Vulnerable.");
    lrg_card_def_set_card_type (card, LRG_CARD_TYPE_ATTACK);
    lrg_card_def_set_rarity (card, LRG_CARD_RARITY_STARTER);
    lrg_card_def_set_base_cost (card, 2);
    lrg_card_def_set_target_type (card, LRG_CARD_TARGET_SINGLE_ENEMY);

    effect = lrg_card_effect_new ("damage");
    lrg_card_effect_set_param_int (effect, "amount", 8);
    lrg_card_def_add_effect (card, effect);

    effect = lrg_card_effect_new ("apply_status");
    lrg_card_effect_set_param_string (effect, "status", "vulnerable");
    lrg_card_effect_set_param_int (effect, "stacks", 2);
    lrg_card_def_add_effect (card, effect);

    g_ptr_array_add (self->card_defs, card);
}

/*
 * demo_combat_game_create_enemy_defs:
 *
 * Create enemy definitions:
 * - Slime: 20 HP, attacks for 8 or defends for 5
 * - Imp: 15 HP, alternates attack/defend
 * - Goblin: 25 HP, always attacks for 12
 */
static void
demo_combat_game_create_enemy_defs (DemoCombatGame *self)
{
    LrgEnemyDef *enemy;
    LrgEnemyIntent *intent;

    /* Slime - basic enemy with weighted random intents */
    enemy = lrg_enemy_def_new ("slime", "Slime");
    lrg_enemy_def_set_description (enemy, "A gelatinous blob.");
    lrg_enemy_def_set_enemy_type (enemy, LRG_ENEMY_TYPE_NORMAL);
    lrg_enemy_def_set_base_health (enemy, 20);
    lrg_enemy_def_set_health_variance (enemy, 4);

    intent = lrg_enemy_intent_new_attack (8, 1);
    lrg_enemy_def_add_intent_pattern (enemy, intent, 75);

    intent = lrg_enemy_intent_new_defend (5);
    lrg_enemy_def_add_intent_pattern (enemy, intent, 25);

    g_ptr_array_add (self->enemy_defs, enemy);

    /* Imp - alternating pattern */
    enemy = lrg_enemy_def_new ("imp", "Imp");
    lrg_enemy_def_set_description (enemy, "A mischievous fire demon.");
    lrg_enemy_def_set_enemy_type (enemy, LRG_ENEMY_TYPE_NORMAL);
    lrg_enemy_def_set_base_health (enemy, 15);
    lrg_enemy_def_set_health_variance (enemy, 3);

    intent = lrg_enemy_intent_new_attack (6, 1);
    lrg_enemy_def_add_intent_pattern (enemy, intent, 50);

    intent = lrg_enemy_intent_new_defend (4);
    lrg_enemy_def_add_intent_pattern (enemy, intent, 50);

    g_ptr_array_add (self->enemy_defs, enemy);

    /* Goblin - aggressive attacker */
    enemy = lrg_enemy_def_new ("goblin", "Goblin");
    lrg_enemy_def_set_description (enemy, "A vicious green creature.");
    lrg_enemy_def_set_enemy_type (enemy, LRG_ENEMY_TYPE_NORMAL);
    lrg_enemy_def_set_base_health (enemy, 25);
    lrg_enemy_def_set_health_variance (enemy, 5);

    intent = lrg_enemy_intent_new_attack (12, 1);
    lrg_enemy_def_add_intent_pattern (enemy, intent, 100);

    g_ptr_array_add (self->enemy_defs, enemy);
}

/*
 * demo_combat_game_init_combat:
 *
 * Initialize a new combat encounter with player and enemies.
 */
static void
demo_combat_game_init_combat (DemoCombatGame *self)
{
    guint i;
    LrgCardDef *strike;
    LrgCardDef *defend;
    LrgCardDef *bash;
    LrgCardPile *draw_pile;
    LrgCardInstance *card;

    /* Create player */
    self->player = lrg_player_combatant_new ("player", "Hero", 80);

    /* Create combat context */
    self->combat_context = lrg_combat_context_new (self->player, NULL);

    /* Add enemies */
    for (i = 0; i < self->enemy_defs->len; i++)
    {
        LrgEnemyDef *def = g_ptr_array_index (self->enemy_defs, i);
        LrgEnemyInstance *enemy = lrg_enemy_instance_new (def);
        lrg_combat_context_add_enemy (self->combat_context, enemy);
        /* Let enemies decide their first intent */
        lrg_enemy_instance_decide_intent (enemy, self->combat_context);
    }

    /* Get card definitions */
    strike = g_ptr_array_index (self->card_defs, 0);
    defend = g_ptr_array_index (self->card_defs, 1);
    bash = g_ptr_array_index (self->card_defs, 2);

    /* Add starter deck to draw pile */
    draw_pile = lrg_combat_context_get_draw_pile (self->combat_context);

    /* 5 Strikes */
    for (i = 0; i < 5; i++)
    {
        card = lrg_card_instance_new (strike);
        lrg_card_pile_add (draw_pile, card, LRG_PILE_POSITION_TOP);
    }

    /* 4 Defends */
    for (i = 0; i < 4; i++)
    {
        card = lrg_card_instance_new (defend);
        lrg_card_pile_add (draw_pile, card, LRG_PILE_POSITION_TOP);
    }

    /* 1 Bash */
    card = lrg_card_instance_new (bash);
    lrg_card_pile_add (draw_pile, card, LRG_PILE_POSITION_TOP);

    /* Shuffle the draw pile */
    lrg_card_pile_shuffle (draw_pile, NULL);

    /* Get hand reference */
    self->hand = lrg_combat_context_get_hand (self->combat_context);

    /* Start combat - this also starts the first player turn and draws cards */
    lrg_combat_manager_start_combat (self->combat_manager, self->combat_context);
    self->ui_state = UI_STATE_PLAYING;
    demo_combat_game_set_message (self, "Combat started! Select a card to play.");
}

/*
 * demo_combat_game_set_message:
 *
 * Set a temporary message to display to the player.
 */
static void
demo_combat_game_set_message (DemoCombatGame *self,
                              const gchar    *msg)
{
    g_clear_pointer (&self->message, g_free);
    self->message = g_strdup (msg);
    self->message_timer = 3.0f;
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
 * demo_combat_game_get_card_x:
 *
 * Calculate the X position for a card in hand.
 */
static gint
demo_combat_game_get_card_x (DemoCombatGame *self, gint index)
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
 * demo_combat_game_get_enemy_x:
 *
 * Calculate the X position for an enemy.
 */
static gint
demo_combat_game_get_enemy_x (DemoCombatGame *self, gint index)
{
    GPtrArray *enemies = lrg_combat_context_get_enemies (self->combat_context);
    guint enemy_count = enemies->len;
    gint total_width;
    gint start_x;

    if (enemy_count == 0)
        return 0;

    total_width = (gint)enemy_count * ENEMY_WIDTH + ((gint)enemy_count - 1) * ENEMY_SPACING;
    start_x = (WINDOW_WIDTH - total_width) / 2;

    return start_x + index * (ENEMY_WIDTH + ENEMY_SPACING);
}

/*
 * demo_combat_game_handle_input:
 *
 * Process mouse input for card selection and targeting.
 */
static void
demo_combat_game_handle_input (DemoCombatGame *self)
{
    gint mx;
    gint my;
    gboolean clicked;
    guint i;
    guint card_count;
    GPtrArray *enemies;

    mx = grl_input_get_mouse_x ();
    my = grl_input_get_mouse_y ();
    clicked = grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT);

    /* Reset hover states */
    self->hovered_card = -1;
    self->hovered_enemy = -1;
    self->hovered_button = FALSE;

    /* Don't process input during enemy turn or end states */
    if (self->ui_state == UI_STATE_ENEMY_TURN ||
        self->ui_state == UI_STATE_VICTORY ||
        self->ui_state == UI_STATE_DEFEAT)
    {
        return;
    }

    /* Check End Turn button */
    if (point_in_rect (mx, my,
                       (WINDOW_WIDTH - BUTTON_WIDTH) / 2, BUTTON_Y,
                       BUTTON_WIDTH, BUTTON_HEIGHT))
    {
        self->hovered_button = TRUE;
        if (clicked && self->ui_state == UI_STATE_PLAYING)
        {
            demo_combat_game_start_enemy_turn (self);
            return;
        }
    }

    /* Check card hovers and clicks */
    if (self->hand == NULL)
        return;
    card_count = lrg_hand_get_count (self->hand);
    for (i = 0; i < card_count; i++)
    {
        gint card_x = demo_combat_game_get_card_x (self, (gint)i);
        gint card_y = CARD_Y;
        LrgCardInstance *card = lrg_hand_get_card_at (self->hand, i);

        if (card == NULL)
            continue;

        /* Raise selected/hovered cards */
        if (lrg_hand_is_selected (self->hand, card))
            card_y -= 75;
        else if ((gint)i == self->hovered_card)
            card_y -= 25;

        if (point_in_rect (mx, my, card_x, card_y, CARD_WIDTH, CARD_HEIGHT))
        {
            self->hovered_card = (gint)i;

            if (clicked && self->ui_state == UI_STATE_PLAYING)
            {
                LrgCardDef *def = lrg_card_instance_get_def (card);
                gint cost = lrg_card_def_get_base_cost (def);
                gint energy = lrg_combat_context_get_energy (self->combat_context);

                /* Check if we can afford the card */
                if (cost > energy)
                {
                    demo_combat_game_set_message (self, "Not enough energy!");
                    return;
                }

                /* Select the card */
                lrg_hand_clear_selection (self->hand);
                lrg_hand_select (self->hand, card);
                self->selected_card = card;

                /* Check if card needs a target */
                if (lrg_card_def_get_target_type (def) == LRG_CARD_TARGET_SINGLE_ENEMY)
                {
                    self->ui_state = UI_STATE_SELECT_TARGET;
                    demo_combat_game_set_message (self, "Click an enemy to target.");
                }
                else
                {
                    /* Self-target card, play immediately */
                    g_autoptr(GError) error = NULL;
                    if (lrg_combat_manager_play_card (self->combat_manager, card, NULL, &error))
                    {
                        g_autofree gchar *msg = g_strdup_printf ("Played %s!", lrg_card_def_get_name (def));
                        demo_combat_game_set_message (self, msg);
                    }
                    else
                    {
                        demo_combat_game_set_message (self, error ? error->message : "Failed to play card!");
                    }
                    lrg_hand_clear_selection (self->hand);
                    self->selected_card = NULL;
                    demo_combat_game_check_combat_end (self);
                }
                return;
            }
            break;
        }
    }

    /* Check enemy hovers and clicks */
    enemies = lrg_combat_context_get_enemies (self->combat_context);
    for (i = 0; i < enemies->len; i++)
    {
        gint enemy_x = demo_combat_game_get_enemy_x (self, (gint)i);
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);

        /* Skip dead enemies */
        if (!lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
            continue;

        if (point_in_rect (mx, my, enemy_x, ENEMY_Y, ENEMY_WIDTH, ENEMY_HEIGHT))
        {
            self->hovered_enemy = (gint)i;

            if (clicked && self->ui_state == UI_STATE_SELECT_TARGET && self->selected_card != NULL)
            {
                /* Play the card on this enemy */
                g_autoptr(GError) error = NULL;

                if (lrg_combat_manager_play_card (self->combat_manager,
                                                   self->selected_card,
                                                   LRG_COMBATANT (enemy),
                                                   &error))
                {
                    g_autofree gchar *msg = g_strdup_printf ("Dealt damage to %s!",
                        lrg_enemy_def_get_name (lrg_enemy_instance_get_def (enemy)));
                    demo_combat_game_set_message (self, msg);
                }
                else
                {
                    demo_combat_game_set_message (self, error ? error->message : "Failed!");
                }

                lrg_hand_clear_selection (self->hand);
                self->selected_card = NULL;
                self->ui_state = UI_STATE_PLAYING;
                demo_combat_game_check_combat_end (self);
                return;
            }
            break;
        }
    }

    /* Right-click to cancel selection */
    if (grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_RIGHT))
    {
        if (self->ui_state == UI_STATE_SELECT_TARGET)
        {
            lrg_hand_clear_selection (self->hand);
            self->selected_card = NULL;
            self->ui_state = UI_STATE_PLAYING;
            demo_combat_game_set_message (self, "Selection cancelled.");
        }
    }
}

/*
 * demo_combat_game_start_enemy_turn:
 *
 * Begin the enemy turn phase.
 */
static void
demo_combat_game_start_enemy_turn (DemoCombatGame *self)
{
    lrg_combat_manager_end_player_turn (self->combat_manager);

    self->ui_state = UI_STATE_ENEMY_TURN;
    self->current_enemy_action = 0;
    self->enemy_action_timer = 0.5f; /* Initial delay */

    demo_combat_game_set_message (self, "Enemy turn...");
}

/*
 * demo_combat_game_update:
 *
 * Update game state each frame.
 */
static void
demo_combat_game_update (DemoCombatGame *self, gfloat delta)
{
    GPtrArray *enemies;
    LrgEnemyInstance *enemy;

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

    /* Process enemy turn */
    if (self->ui_state == UI_STATE_ENEMY_TURN)
    {
        self->enemy_action_timer -= delta;

        if (self->enemy_action_timer <= 0.0f)
        {
            enemies = lrg_combat_context_get_enemies (self->combat_context);

            /* Find next living enemy */
            while (self->current_enemy_action < (gint)enemies->len)
            {
                enemy = g_ptr_array_index (enemies, self->current_enemy_action);

                if (lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
                {
                    /* Execute this enemy's intent */
                    lrg_enemy_instance_execute_intent (enemy, self->combat_context);

                    /* Decide next intent */
                    lrg_enemy_instance_decide_intent (enemy, self->combat_context);

                    /* Check if player died */
                    if (!lrg_combatant_is_alive (LRG_COMBATANT (self->player)))
                    {
                        self->ui_state = UI_STATE_DEFEAT;
                        demo_combat_game_set_message (self, "DEFEAT - You have been slain!");
                        return;
                    }

                    self->current_enemy_action++;
                    self->enemy_action_timer = 0.7f; /* Delay between enemy actions */
                    return;
                }

                self->current_enemy_action++;
            }

            /* All enemies have acted, start player turn */
            lrg_combat_manager_start_player_turn (self->combat_manager);
            lrg_combat_manager_draw_cards (self->combat_manager, 5);
            self->ui_state = UI_STATE_PLAYING;
            demo_combat_game_set_message (self, "Your turn!");
        }
    }
}

/*
 * demo_combat_game_check_combat_end:
 *
 * Check if combat has ended (victory or defeat).
 */
static void
demo_combat_game_check_combat_end (DemoCombatGame *self)
{
    /* Check for victory */
    if (lrg_combat_manager_check_victory (self->combat_manager))
    {
        self->ui_state = UI_STATE_VICTORY;
        demo_combat_game_set_message (self, "VICTORY! All enemies defeated!");
        return;
    }

    /* Check for defeat */
    if (lrg_combat_manager_check_defeat (self->combat_manager))
    {
        self->ui_state = UI_STATE_DEFEAT;
        demo_combat_game_set_message (self, "DEFEAT - You have been slain!");
        return;
    }
}

/*
 * demo_combat_game_draw_card:
 *
 * Draw a single card at the specified position.
 */
static void
demo_combat_game_draw_card (DemoCombatGame  *self,
                            LrgCardInstance *card,
                            gint             x,
                            gint             y,
                            gboolean         is_hovered,
                            gboolean         is_selected)
{
    LrgCardDef *def;
    const gchar *name;
    gint cost;
    LrgCardType card_type;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) border_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;
    g_autoptr(GrlColor) cost_color = NULL;
    g_autofree gchar *cost_str = NULL;

    def = lrg_card_instance_get_def (card);
    name = lrg_card_def_get_name (def);
    cost = lrg_card_def_get_base_cost (def);
    card_type = lrg_card_def_get_card_type (def);

    /* Adjust position for hover/selection */
    if (is_selected)
        y -= 30;
    else if (is_hovered)
        y -= 10;

    /* Set colors based on card type */
    if (card_type == LRG_CARD_TYPE_ATTACK)
    {
        bg_color = grl_color_new (180, 80, 80, 255);
    }
    else if (card_type == LRG_CARD_TYPE_SKILL)
    {
        bg_color = grl_color_new (80, 120, 180, 255);
    }
    else
    {
        bg_color = grl_color_new (120, 120, 120, 255);
    }

    border_color = is_selected ? grl_color_new (255, 255, 0, 255)
                               : grl_color_new (40, 40, 40, 255);
    text_color = grl_color_new (255, 255, 255, 255);
    cost_color = grl_color_new (200, 200, 255, 255);

    /* Draw card background */
    grl_draw_rectangle (x, y, CARD_WIDTH, CARD_HEIGHT, bg_color);

    /* Draw border */
    grl_draw_rectangle_lines (x, y, CARD_WIDTH, CARD_HEIGHT, border_color);

    /* Draw card name */
    draw_label (get_pool_label (self), name, (gfloat)(x + 12), (gfloat)(y + 25), 35.0f, text_color);

    /* Draw energy cost */
    cost_str = g_strdup_printf ("%d", cost);
    draw_label (get_pool_label (self), cost_str, (gfloat)(x + CARD_WIDTH - 50), (gfloat)(y + 12), 45.0f, cost_color);

    /* Draw card type indicator */
    if (card_type == LRG_CARD_TYPE_ATTACK)
        draw_label (get_pool_label (self), "ATK", (gfloat)(x + 12), (gfloat)(y + CARD_HEIGHT - 62), 30.0f, text_color);
    else if (card_type == LRG_CARD_TYPE_SKILL)
        draw_label (get_pool_label (self), "SKL", (gfloat)(x + 12), (gfloat)(y + CARD_HEIGHT - 62), 30.0f, text_color);
}

/*
 * demo_combat_game_draw_enemy:
 *
 * Draw a single enemy at the specified position.
 */
static void
demo_combat_game_draw_enemy (DemoCombatGame   *self,
                             LrgEnemyInstance *enemy,
                             gint              x,
                             gint              y,
                             gboolean          is_hovered)
{
    LrgEnemyDef *def;
    const gchar *name;
    gint current_hp;
    gint max_hp;
    gint block;
    const LrgEnemyIntent *intent;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) border_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;
    g_autoptr(GrlColor) hp_color = NULL;
    g_autoptr(GrlColor) block_color = NULL;
    g_autoptr(GrlColor) dead_color = NULL;
    g_autofree gchar *hp_str = NULL;
    g_autofree gchar *intent_str = NULL;
    g_autofree gchar *block_str = NULL;

    def = lrg_enemy_instance_get_def (enemy);
    name = lrg_enemy_def_get_name (def);
    current_hp = lrg_combatant_get_current_health (LRG_COMBATANT (enemy));
    max_hp = lrg_combatant_get_max_health (LRG_COMBATANT (enemy));
    block = lrg_combatant_get_block (LRG_COMBATANT (enemy));
    intent = lrg_enemy_instance_get_intent (enemy);

    /* Check if dead */
    if (!lrg_combatant_is_alive (LRG_COMBATANT (enemy)))
    {
        dead_color = grl_color_new (60, 60, 60, 200);
        grl_draw_rectangle (x, y, ENEMY_WIDTH, ENEMY_HEIGHT, dead_color);
        text_color = grl_color_new (150, 150, 150, 255);
        draw_label (get_pool_label (self), "DEAD", (gfloat)(x + 62), (gfloat)(y + 125), 40.0f, text_color);
        return;
    }

    /* Set colors */
    bg_color = is_hovered ? grl_color_new (100, 60, 60, 255)
                          : grl_color_new (80, 50, 50, 255);
    border_color = is_hovered && self->ui_state == UI_STATE_SELECT_TARGET
                   ? grl_color_new (255, 255, 0, 255)
                   : grl_color_new (40, 40, 40, 255);
    text_color = grl_color_new (255, 255, 255, 255);
    hp_color = grl_color_new (200, 80, 80, 255);
    block_color = grl_color_new (100, 150, 200, 255);

    /* Draw enemy background */
    grl_draw_rectangle (x, y, ENEMY_WIDTH, ENEMY_HEIGHT, bg_color);
    grl_draw_rectangle_lines (x, y, ENEMY_WIDTH, ENEMY_HEIGHT, border_color);

    /* Draw name */
    draw_label (get_pool_label (self), name, (gfloat)(x + 12), (gfloat)(y + 12), 35.0f, text_color);

    /* Draw HP */
    hp_str = g_strdup_printf ("HP: %d/%d", current_hp, max_hp);
    draw_label (get_pool_label (self), hp_str, (gfloat)(x + 12), (gfloat)(y + 75), 30.0f, hp_color);

    /* Draw Block if any */
    if (block > 0)
    {
        block_str = g_strdup_printf ("Block: %d", block);
        draw_label (get_pool_label (self), block_str, (gfloat)(x + 12), (gfloat)(y + 125), 30.0f, block_color);
    }

    /* Draw intent */
    if (intent != NULL)
    {
        LrgIntentType intent_type = lrg_enemy_intent_get_intent_type (intent);

        if (intent_type == LRG_INTENT_ATTACK)
        {
            gint damage = lrg_enemy_intent_get_damage (intent);
            intent_str = g_strdup_printf ("ATK: %d", damage);
        }
        else if (intent_type == LRG_INTENT_DEFEND)
        {
            gint block_amt = lrg_enemy_intent_get_block (intent);
            intent_str = g_strdup_printf ("DEF: %d", block_amt);
        }
        else if (intent_type == LRG_INTENT_BUFF)
        {
            intent_str = g_strdup ("BUFF");
        }
        else
        {
            intent_str = g_strdup ("???");
        }

        draw_label (get_pool_label (self), intent_str, (gfloat)(x + 12), (gfloat)(y + ENEMY_HEIGHT - 62), 35.0f, text_color);
    }
}

/*
 * demo_combat_game_draw:
 *
 * Render the entire game screen.
 */
static void
demo_combat_game_draw (DemoCombatGame *self)
{
    gint energy;
    gint current_hp;
    gint max_hp;
    gint block;
    guint card_count;
    guint i;
    GPtrArray *enemies;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) text_color = NULL;
    g_autoptr(GrlColor) energy_color = NULL;
    g_autoptr(GrlColor) hp_color = NULL;
    g_autoptr(GrlColor) block_color = NULL;
    g_autoptr(GrlColor) button_color = NULL;
    g_autoptr(GrlColor) button_hover = NULL;
    g_autoptr(GrlColor) msg_color = NULL;
    g_autofree gchar *energy_str = NULL;
    g_autofree gchar *hp_str = NULL;
    g_autofree gchar *block_str = NULL;

    /* Reset label pool for this frame */
    reset_label_pool (self);

    bg_color = grl_color_new (30, 30, 40, 255);
    text_color = grl_color_new (255, 255, 255, 255);
    energy_color = grl_color_new (255, 200, 100, 255);
    hp_color = grl_color_new (200, 80, 80, 255);
    block_color = grl_color_new (100, 150, 200, 255);
    button_color = grl_color_new (60, 100, 60, 255);
    button_hover = grl_color_new (80, 130, 80, 255);
    msg_color = grl_color_new (255, 255, 150, 255);

    grl_draw_clear_background (bg_color);

    /* Draw player stats */
    energy = lrg_combat_context_get_energy (self->combat_context);
    current_hp = lrg_combatant_get_current_health (LRG_COMBATANT (self->player));
    max_hp = lrg_combatant_get_max_health (LRG_COMBATANT (self->player));
    block = lrg_combatant_get_block (LRG_COMBATANT (self->player));

    energy_str = g_strdup_printf ("Energy: %d/3", energy);
    draw_label (self->label_energy, energy_str, 50.0f, 37.0f, 50.0f, energy_color);

    hp_str = g_strdup_printf ("HP: %d/%d", current_hp, max_hp);
    draw_label (self->label_hp, hp_str, 50.0f, 100.0f, 50.0f, hp_color);

    if (block > 0)
    {
        block_str = g_strdup_printf ("Block: %d", block);
        draw_label (self->label_block, block_str, 450.0f, 100.0f, 50.0f, block_color);
    }

    /* Draw enemies */
    enemies = lrg_combat_context_get_enemies (self->combat_context);
    for (i = 0; i < enemies->len; i++)
    {
        LrgEnemyInstance *enemy = g_ptr_array_index (enemies, i);
        gint ex = demo_combat_game_get_enemy_x (self, (gint)i);
        gboolean hovered = ((gint)i == self->hovered_enemy);
        demo_combat_game_draw_enemy (self, enemy, ex, ENEMY_Y, hovered);
    }

    /* Draw End Turn button */
    if (self->ui_state == UI_STATE_PLAYING || self->ui_state == UI_STATE_SELECT_TARGET)
    {
        gint button_x = (WINDOW_WIDTH - BUTTON_WIDTH) / 2;

        grl_draw_rectangle (button_x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT,
                            self->hovered_button ? button_hover : button_color);
        draw_label (self->label_button, "End Turn", (gfloat)(button_x + 50), (gfloat)(BUTTON_Y + 25), 45.0f, text_color);
    }

    /* Draw cards in hand */
    if (self->hand != NULL)
    {
        card_count = lrg_hand_get_count (self->hand);
        for (i = 0; i < card_count; i++)
        {
            LrgCardInstance *card = lrg_hand_get_card_at (self->hand, i);
            if (card != NULL)
            {
                gint cx = demo_combat_game_get_card_x (self, (gint)i);
                gboolean hovered = ((gint)i == self->hovered_card);
                gboolean selected = lrg_hand_is_selected (self->hand, card);
                demo_combat_game_draw_card (self, card, cx, CARD_Y, hovered, selected);
            }
        }
    }

    /* Draw state indicator */
    if (self->ui_state == UI_STATE_ENEMY_TURN)
    {
        draw_label (self->label_state, "ENEMY TURN", (gfloat)(WINDOW_WIDTH / 2 - 150), 750.0f, 60.0f, text_color);
    }
    else if (self->ui_state == UI_STATE_SELECT_TARGET)
    {
        draw_label (self->label_state, "SELECT TARGET", (gfloat)(WINDOW_WIDTH / 2 - 175), 750.0f, 50.0f, energy_color);
    }
    else if (self->ui_state == UI_STATE_VICTORY)
    {
        g_autoptr(GrlColor) victory = grl_color_new (100, 255, 100, 255);
        draw_label (self->label_state, "VICTORY!", (gfloat)(WINDOW_WIDTH / 2 - 150), 700.0f, 80.0f, victory);
    }
    else if (self->ui_state == UI_STATE_DEFEAT)
    {
        g_autoptr(GrlColor) defeat = grl_color_new (255, 80, 80, 255);
        draw_label (self->label_state, "DEFEAT!", (gfloat)(WINDOW_WIDTH / 2 - 125), 700.0f, 80.0f, defeat);
    }

    /* Draw message */
    if (self->message != NULL && self->message_timer > 0.0f)
    {
        draw_label (self->label_message, self->message, 50.0f, 650.0f, 40.0f, msg_color);
    }

    /* Draw instructions */
    {
        g_autoptr(GrlColor) instr_color = grl_color_new (150, 150, 150, 255);
        draw_label (self->label_instructions1, "Click cards to select, click enemies to attack", 50.0f, 700.0f, 30.0f, instr_color);
        draw_label (self->label_instructions2, "Right-click to cancel, click End Turn when done", 50.0f, 740.0f, 30.0f, instr_color);
    }
}

/*
 * main:
 *
 * Entry point for the combat deckbuilder demo.
 */
int
main (int    argc,
      char **argv)
{
    g_autoptr(GrlWindow) window = NULL;
    g_autoptr(DemoCombatGame) game = NULL;
    g_autoptr(GrlColor) title_color = NULL;

    /* Create window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "Combat Deckbuilder Demo");
    grl_window_set_target_fps (window, 60);

    /* Create game */
    game = demo_combat_game_new ();

    /* Main loop */
    while (!grl_window_should_close (window))
    {
        gfloat delta = grl_window_get_frame_time (window);

        /* Handle input */
        demo_combat_game_handle_input (game);

        /* Update game state */
        demo_combat_game_update (game, delta);

        /* Render */
        grl_window_begin_drawing (window);
        demo_combat_game_draw (game);
        grl_window_end_drawing (window);
    }

    return 0;
}
