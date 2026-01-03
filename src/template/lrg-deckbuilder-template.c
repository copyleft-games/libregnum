/* lrg-deckbuilder-template.c
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of LrgDeckbuilderTemplate - a derivable game template
 * for deckbuilder games. Implements LrgDeckMixin for deck/hand/discard
 * management with turn structure and energy system.
 */

#include "config.h"
#include "lrg-deckbuilder-template.h"
#include "lrg-deck-mixin.h"
#include "../deckbuilder/lrg-deck-instance.h"
#include "../deckbuilder/lrg-deck-def.h"
#include "../deckbuilder/lrg-card-instance.h"
#include "../deckbuilder/lrg-card-def.h"
#include "../deckbuilder/lrg-card-pile.h"
#include "../deckbuilder/lrg-hand.h"
#include "../lrg-log.h"

/* Default values */
#define DEFAULT_MAX_ENERGY      3
#define DEFAULT_BASE_HAND_SIZE  5
#define DEFAULT_STARTING_ENERGY 3
#define DEFAULT_CARDS_TO_DRAW   5

typedef struct _LrgDeckbuilderTemplatePrivate
{
    LrgDeckInstance *deck_instance;
    gint             current_energy;
    gint             max_energy;
    guint            current_turn;
    guint            base_hand_size;
    gboolean         is_player_turn;
} LrgDeckbuilderTemplatePrivate;

/* Property IDs */
enum
{
    PROP_0,
    PROP_CURRENT_ENERGY,
    PROP_MAX_ENERGY,
    PROP_CURRENT_TURN,
    PROP_BASE_HAND_SIZE,
    PROP_IS_PLAYER_TURN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signals */
enum
{
    SIGNAL_TURN_STARTED,
    SIGNAL_TURN_ENDED,
    SIGNAL_CARD_PLAYED,
    SIGNAL_ENERGY_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Forward declarations */
static void lrg_deckbuilder_template_deck_mixin_init (LrgDeckMixinInterface *iface);
static void lrg_deckbuilder_template_pre_startup     (LrgGameTemplate       *template);
static void lrg_deckbuilder_template_shutdown        (LrgGameTemplate       *template);

G_DEFINE_TYPE_WITH_CODE (LrgDeckbuilderTemplate, lrg_deckbuilder_template,
                         LRG_TYPE_GAME_TEMPLATE,
                         G_ADD_PRIVATE (LrgDeckbuilderTemplate)
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DECK_MIXIN,
                                                lrg_deckbuilder_template_deck_mixin_init))

/* ==========================================================================
 * LrgDeckMixin Interface Implementation
 * ========================================================================== */

static LrgDeckInstance *
lrg_deckbuilder_template_mixin_get_deck_instance (LrgDeckMixin *mixin)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (mixin);
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->deck_instance;
}

static guint
lrg_deckbuilder_template_mixin_get_hand_size (LrgDeckMixin *mixin)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (mixin);
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->base_hand_size;
}

static void
lrg_deckbuilder_template_mixin_on_card_drawn (LrgDeckMixin    *mixin,
                                               LrgCardInstance *card)
{
    /* Default: nothing special on draw */
    (void)mixin;
    (void)card;
}

static void
lrg_deckbuilder_template_mixin_on_card_played (LrgDeckMixin    *mixin,
                                                LrgCardInstance *card,
                                                gpointer         target)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (mixin);

    g_signal_emit (self, signals[SIGNAL_CARD_PLAYED], 0, card, target);
}

static void
lrg_deckbuilder_template_mixin_on_card_discarded (LrgDeckMixin    *mixin,
                                                   LrgCardInstance *card)
{
    /* Default: nothing special on discard */
    (void)mixin;
    (void)card;
}

static void
lrg_deckbuilder_template_mixin_on_deck_shuffled (LrgDeckMixin *mixin)
{
    /* Default: nothing special on shuffle */
    (void)mixin;
}

static void
lrg_deckbuilder_template_mixin_on_turn_started (LrgDeckMixin *mixin,
                                                 guint         turn_number)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (mixin);

    g_signal_emit (self, signals[SIGNAL_TURN_STARTED], 0, turn_number);
}

static void
lrg_deckbuilder_template_mixin_on_turn_ended (LrgDeckMixin *mixin,
                                               guint         turn_number)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (mixin);

    g_signal_emit (self, signals[SIGNAL_TURN_ENDED], 0, turn_number);
}

static void
lrg_deckbuilder_template_deck_mixin_init (LrgDeckMixinInterface *iface)
{
    iface->get_deck_instance = lrg_deckbuilder_template_mixin_get_deck_instance;
    iface->get_hand_size = lrg_deckbuilder_template_mixin_get_hand_size;
    iface->on_card_drawn = lrg_deckbuilder_template_mixin_on_card_drawn;
    iface->on_card_played = lrg_deckbuilder_template_mixin_on_card_played;
    iface->on_card_discarded = lrg_deckbuilder_template_mixin_on_card_discarded;
    iface->on_deck_shuffled = lrg_deckbuilder_template_mixin_on_deck_shuffled;
    iface->on_turn_started = lrg_deckbuilder_template_mixin_on_turn_started;
    iface->on_turn_ended = lrg_deckbuilder_template_mixin_on_turn_ended;
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static LrgDeckDef *
lrg_deckbuilder_template_real_create_deck_def (LrgDeckbuilderTemplate *self)
{
    /*
     * Default implementation returns NULL - subclasses must override
     * to provide their own deck definition.
     */
    (void)self;
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
               "LrgDeckbuilderTemplate::create_deck_def not overridden");
    return NULL;
}

static LrgDeckInstance *
lrg_deckbuilder_template_real_create_deck_instance (LrgDeckbuilderTemplate *self,
                                                     LrgDeckDef             *def)
{
    (void)self;

    if (def == NULL)
        return NULL;

    return lrg_deck_instance_new (def);
}

static gboolean
lrg_deckbuilder_template_real_on_card_played (LrgDeckbuilderTemplate *self,
                                               LrgCardInstance        *card,
                                               gpointer                target)
{
    /* Default: card effects should be handled by subclasses */
    (void)self;
    (void)card;
    (void)target;
    return TRUE;
}

static gint
lrg_deckbuilder_template_real_evaluate_card_cost (LrgDeckbuilderTemplate *self,
                                                   LrgCardInstance        *card)
{
    /*
     * Default: return the effective cost from the card instance,
     * which includes base cost + modifiers.
     */
    (void)self;

    if (card == NULL)
        return 0;

    return lrg_card_instance_get_effective_cost (card, NULL);
}

static gboolean
lrg_deckbuilder_template_real_can_play_card (LrgDeckbuilderTemplate *self,
                                              LrgCardInstance        *card,
                                              gpointer                target)
{
    LrgDeckbuilderTemplatePrivate *priv;
    gint cost;

    (void)target;

    if (card == NULL)
        return FALSE;

    priv = lrg_deckbuilder_template_get_instance_private (self);

    /* Check if it's the player's turn */
    if (!priv->is_player_turn)
        return FALSE;

    /* Check for unplayable keyword */
    if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_UNPLAYABLE))
        return FALSE;

    /* Check energy cost */
    cost = lrg_deckbuilder_template_get_card_cost (self, card);
    if (cost > priv->current_energy)
        return FALSE;

    return TRUE;
}

static void
lrg_deckbuilder_template_real_start_turn (LrgDeckbuilderTemplate *self,
                                           guint                   turn_number)
{
    LrgDeckbuilderTemplateClass *klass;
    guint cards_to_draw;

    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    /* Reset energy */
    lrg_deckbuilder_template_reset_energy (self);

    /* Draw cards */
    cards_to_draw = DEFAULT_CARDS_TO_DRAW;
    if (klass->get_cards_to_draw != NULL)
        cards_to_draw = klass->get_cards_to_draw (self);

    lrg_deck_mixin_draw_cards (LRG_DECK_MIXIN (self), cards_to_draw);

    /* Notify via mixin hook */
    lrg_deck_mixin_on_turn_started (LRG_DECK_MIXIN (self), turn_number);
}

static void
lrg_deckbuilder_template_real_end_turn (LrgDeckbuilderTemplate *self,
                                         guint                   turn_number)
{
    /* Discard remaining hand */
    lrg_deck_mixin_discard_hand (LRG_DECK_MIXIN (self));

    /* Notify via mixin hook */
    lrg_deck_mixin_on_turn_ended (LRG_DECK_MIXIN (self), turn_number);
}

static gint
lrg_deckbuilder_template_real_get_starting_energy (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->max_energy;
}

static guint
lrg_deckbuilder_template_real_get_cards_to_draw (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->base_hand_size;
}

/* ==========================================================================
 * LrgGameTemplate Virtual Method Overrides
 * ========================================================================== */

static void
lrg_deckbuilder_template_pre_startup (LrgGameTemplate *template)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (template);
    LrgDeckbuilderTemplatePrivate *priv;
    LrgDeckbuilderTemplateClass *klass;
    LrgDeckDef *deck_def = NULL;

    priv = lrg_deckbuilder_template_get_instance_private (self);
    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    /* Create deck definition via virtual method */
    if (klass->create_deck_def != NULL)
        deck_def = klass->create_deck_def (self);

    /* Create deck instance */
    if (deck_def != NULL)
    {
        if (klass->create_deck_instance != NULL)
            priv->deck_instance = klass->create_deck_instance (self, deck_def);
        else
            priv->deck_instance = lrg_deck_instance_new (deck_def);

        g_object_unref (deck_def);
    }

    /* Chain up */
    if (LRG_GAME_TEMPLATE_CLASS (lrg_deckbuilder_template_parent_class)->pre_startup != NULL)
    {
        LRG_GAME_TEMPLATE_CLASS (lrg_deckbuilder_template_parent_class)->pre_startup (template);
    }

    lrg_info (LRG_LOG_DOMAIN_TEMPLATE, "Deckbuilder template pre-startup complete");
}

static void
lrg_deckbuilder_template_shutdown (LrgGameTemplate *template)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (template);
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);

    /* Clean up deck instance */
    g_clear_object (&priv->deck_instance);

    /* Chain up */
    if (LRG_GAME_TEMPLATE_CLASS (lrg_deckbuilder_template_parent_class)->shutdown != NULL)
    {
        LRG_GAME_TEMPLATE_CLASS (lrg_deckbuilder_template_parent_class)->shutdown (template);
    }

    lrg_info (LRG_LOG_DOMAIN_TEMPLATE, "Deckbuilder template shutdown complete");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_deckbuilder_template_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (object);
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CURRENT_ENERGY:
        priv->current_energy = g_value_get_int (value);
        break;

    case PROP_MAX_ENERGY:
        priv->max_energy = g_value_get_int (value);
        break;

    case PROP_BASE_HAND_SIZE:
        priv->base_hand_size = g_value_get_uint (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_deckbuilder_template_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (object);
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CURRENT_ENERGY:
        g_value_set_int (value, priv->current_energy);
        break;

    case PROP_MAX_ENERGY:
        g_value_set_int (value, priv->max_energy);
        break;

    case PROP_CURRENT_TURN:
        g_value_set_uint (value, priv->current_turn);
        break;

    case PROP_BASE_HAND_SIZE:
        g_value_set_uint (value, priv->base_hand_size);
        break;

    case PROP_IS_PLAYER_TURN:
        g_value_set_boolean (value, priv->is_player_turn);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_deckbuilder_template_dispose (GObject *object)
{
    LrgDeckbuilderTemplate *self = LRG_DECKBUILDER_TEMPLATE (object);
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);

    g_clear_object (&priv->deck_instance);

    G_OBJECT_CLASS (lrg_deckbuilder_template_parent_class)->dispose (object);
}

static void
lrg_deckbuilder_template_init (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    priv = lrg_deckbuilder_template_get_instance_private (self);

    priv->deck_instance = NULL;
    priv->current_energy = DEFAULT_STARTING_ENERGY;
    priv->max_energy = DEFAULT_MAX_ENERGY;
    priv->current_turn = 0;
    priv->base_hand_size = DEFAULT_BASE_HAND_SIZE;
    priv->is_player_turn = FALSE;
}

static void
lrg_deckbuilder_template_class_init (LrgDeckbuilderTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    /* GObject methods */
    object_class->set_property = lrg_deckbuilder_template_set_property;
    object_class->get_property = lrg_deckbuilder_template_get_property;
    object_class->dispose = lrg_deckbuilder_template_dispose;

    /* LrgGameTemplate overrides */
    template_class->pre_startup = lrg_deckbuilder_template_pre_startup;
    template_class->shutdown = lrg_deckbuilder_template_shutdown;

    /* Virtual methods with default implementations */
    klass->create_deck_def = lrg_deckbuilder_template_real_create_deck_def;
    klass->create_deck_instance = lrg_deckbuilder_template_real_create_deck_instance;
    klass->on_card_played = lrg_deckbuilder_template_real_on_card_played;
    klass->evaluate_card_cost = lrg_deckbuilder_template_real_evaluate_card_cost;
    klass->can_play_card = lrg_deckbuilder_template_real_can_play_card;
    klass->start_turn = lrg_deckbuilder_template_real_start_turn;
    klass->end_turn = lrg_deckbuilder_template_real_end_turn;
    klass->get_starting_energy = lrg_deckbuilder_template_real_get_starting_energy;
    klass->get_cards_to_draw = lrg_deckbuilder_template_real_get_cards_to_draw;

    /* Properties */
    properties[PROP_CURRENT_ENERGY] =
        g_param_spec_int ("current-energy",
                          "Current Energy",
                          "The current amount of energy available",
                          0, G_MAXINT, DEFAULT_STARTING_ENERGY,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_ENERGY] =
        g_param_spec_int ("max-energy",
                          "Max Energy",
                          "The maximum energy at turn start",
                          1, G_MAXINT, DEFAULT_MAX_ENERGY,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    properties[PROP_CURRENT_TURN] =
        g_param_spec_uint ("current-turn",
                           "Current Turn",
                           "The current turn number (1-indexed)",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BASE_HAND_SIZE] =
        g_param_spec_uint ("base-hand-size",
                           "Base Hand Size",
                           "The number of cards to draw at turn start",
                           1, 20, DEFAULT_BASE_HAND_SIZE,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    properties[PROP_IS_PLAYER_TURN] =
        g_param_spec_boolean ("is-player-turn",
                              "Is Player Turn",
                              "Whether it is currently the player's turn",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */
    signals[SIGNAL_TURN_STARTED] =
        g_signal_new ("turn-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    signals[SIGNAL_TURN_ENDED] =
        g_signal_new ("turn-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    signals[SIGNAL_CARD_PLAYED] =
        g_signal_new ("card-played",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_POINTER);

    signals[SIGNAL_ENERGY_CHANGED] =
        g_signal_new ("energy-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_new:
 *
 * Creates a new deckbuilder template with default settings.
 *
 * Returns: (transfer full): a new #LrgDeckbuilderTemplate
 */
LrgDeckbuilderTemplate *
lrg_deckbuilder_template_new (void)
{
    return g_object_new (LRG_TYPE_DECKBUILDER_TEMPLATE, NULL);
}

/* ==========================================================================
 * Public API - Deck Access
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_get_deck_instance:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the current deck instance.
 *
 * Returns: (transfer none): the #LrgDeckInstance
 */
LrgDeckInstance *
lrg_deckbuilder_template_get_deck_instance (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), NULL);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->deck_instance;
}

/**
 * lrg_deckbuilder_template_set_deck_instance:
 * @self: an #LrgDeckbuilderTemplate
 * @deck: (transfer none): the deck to use
 *
 * Sets the current deck instance.
 */
void
lrg_deckbuilder_template_set_deck_instance (LrgDeckbuilderTemplate *self,
                                            LrgDeckInstance        *deck)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));

    priv = lrg_deckbuilder_template_get_instance_private (self);

    if (priv->deck_instance != deck)
    {
        g_clear_object (&priv->deck_instance);
        if (deck != NULL)
            priv->deck_instance = g_object_ref (deck);
    }
}

/* ==========================================================================
 * Public API - Properties
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_get_current_energy:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the current energy available.
 *
 * Returns: current energy
 */
gint
lrg_deckbuilder_template_get_current_energy (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), 0);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->current_energy;
}

/**
 * lrg_deckbuilder_template_set_current_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @energy: energy value
 *
 * Sets the current energy.
 */
void
lrg_deckbuilder_template_set_current_energy (LrgDeckbuilderTemplate *self,
                                             gint                    energy)
{
    LrgDeckbuilderTemplatePrivate *priv;
    gint old_energy;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));

    priv = lrg_deckbuilder_template_get_instance_private (self);
    old_energy = priv->current_energy;

    if (old_energy != energy)
    {
        priv->current_energy = energy;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_ENERGY]);
        g_signal_emit (self, signals[SIGNAL_ENERGY_CHANGED], 0, old_energy, energy);
    }
}

/**
 * lrg_deckbuilder_template_get_max_energy:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the maximum energy.
 *
 * Returns: maximum energy
 */
gint
lrg_deckbuilder_template_get_max_energy (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), 0);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->max_energy;
}

/**
 * lrg_deckbuilder_template_set_max_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @energy: max energy value
 *
 * Sets the maximum energy.
 */
void
lrg_deckbuilder_template_set_max_energy (LrgDeckbuilderTemplate *self,
                                         gint                    energy)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));
    g_return_if_fail (energy >= 1);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    priv->max_energy = energy;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_ENERGY]);
}

/**
 * lrg_deckbuilder_template_get_current_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the current turn number.
 *
 * Returns: current turn (1-indexed)
 */
guint
lrg_deckbuilder_template_get_current_turn (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), 0);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->current_turn;
}

/**
 * lrg_deckbuilder_template_get_base_hand_size:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the base hand size (cards drawn per turn).
 *
 * Returns: base hand size
 */
guint
lrg_deckbuilder_template_get_base_hand_size (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), 0);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->base_hand_size;
}

/**
 * lrg_deckbuilder_template_set_base_hand_size:
 * @self: an #LrgDeckbuilderTemplate
 * @size: new base hand size
 *
 * Sets the base hand size.
 */
void
lrg_deckbuilder_template_set_base_hand_size (LrgDeckbuilderTemplate *self,
                                             guint                   size)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));
    g_return_if_fail (size >= 1);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    priv->base_hand_size = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BASE_HAND_SIZE]);
}

/* ==========================================================================
 * Public API - Turn Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_start_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Starts a new turn. Increments turn counter, draws cards, resets energy.
 */
void
lrg_deckbuilder_template_start_turn (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;
    LrgDeckbuilderTemplateClass *klass;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));

    priv = lrg_deckbuilder_template_get_instance_private (self);
    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    /* Increment turn */
    priv->current_turn++;
    priv->is_player_turn = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_TURN]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PLAYER_TURN]);

    /* Call virtual method */
    if (klass->start_turn != NULL)
        klass->start_turn (self, priv->current_turn);
}

/**
 * lrg_deckbuilder_template_end_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Ends the current turn. Discards hand, triggers end-turn effects.
 */
void
lrg_deckbuilder_template_end_turn (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;
    LrgDeckbuilderTemplateClass *klass;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));

    priv = lrg_deckbuilder_template_get_instance_private (self);
    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    priv->is_player_turn = FALSE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PLAYER_TURN]);

    /* Call virtual method */
    if (klass->end_turn != NULL)
        klass->end_turn (self, priv->current_turn);
}

/**
 * lrg_deckbuilder_template_is_player_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Checks if it's the player's turn.
 *
 * Returns: %TRUE if player can act
 */
gboolean
lrg_deckbuilder_template_is_player_turn (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), FALSE);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    return priv->is_player_turn;
}

/* ==========================================================================
 * Public API - Card Operations
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_play_card:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): the card to play
 * @target: (nullable): optional target
 *
 * Plays a card from hand. Checks cost, executes effect, moves to discard.
 *
 * Returns: %TRUE if the card was played
 */
gboolean
lrg_deckbuilder_template_play_card (LrgDeckbuilderTemplate *self,
                                    LrgCardInstance        *card,
                                    gpointer                target)
{
    LrgDeckbuilderTemplatePrivate *priv;
    LrgDeckbuilderTemplateClass *klass;
    LrgHand *hand;
    LrgCardPile *discard;
    gint cost;
    gboolean can_play;
    gboolean card_success;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    priv = lrg_deckbuilder_template_get_instance_private (self);
    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    if (priv->deck_instance == NULL)
        return FALSE;

    /* Check if card can be played */
    if (klass->can_play_card != NULL)
        can_play = klass->can_play_card (self, card, target);
    else
        can_play = lrg_deckbuilder_template_can_play_card (self, card);

    if (!can_play)
        return FALSE;

    /* Get cost and spend energy */
    cost = lrg_deckbuilder_template_get_card_cost (self, card);
    if (!lrg_deckbuilder_template_spend_energy (self, cost))
        return FALSE;

    /* Execute card effect via virtual method */
    card_success = TRUE;
    if (klass->on_card_played != NULL)
        card_success = klass->on_card_played (self, card, target);

    if (!card_success)
    {
        /* Refund energy if card failed */
        lrg_deckbuilder_template_gain_energy (self, cost);
        return FALSE;
    }

    /* Increment play count */
    lrg_card_instance_increment_play_count (card);

    /* Call mixin hook */
    lrg_deck_mixin_on_card_played (LRG_DECK_MIXIN (self), card, target);

    /* Move card from hand to discard (unless it exhausts) */
    hand = lrg_deck_instance_get_hand (priv->deck_instance);
    discard = lrg_deck_instance_get_discard_pile (priv->deck_instance);

    if (lrg_card_instance_has_keyword (card, LRG_CARD_KEYWORD_EXHAUST))
    {
        /* Move to exhaust pile */
        LrgCardPile *exhaust = lrg_deck_instance_get_exhaust_pile (priv->deck_instance);
        LrgCardInstance *removed = lrg_hand_remove (hand, card);
        if (removed != NULL)
        {
            lrg_card_pile_add_top (exhaust, removed);
            lrg_deck_mixin_on_card_exhausted (LRG_DECK_MIXIN (self), removed);
        }
    }
    else
    {
        /* Move to discard pile */
        lrg_hand_discard (hand, card, discard);
        lrg_deck_mixin_on_card_discarded (LRG_DECK_MIXIN (self), card);
    }

    return TRUE;
}

/**
 * lrg_deckbuilder_template_play_card_at:
 * @self: an #LrgDeckbuilderTemplate
 * @hand_index: index in hand
 * @target: (nullable): optional target
 *
 * Plays the card at a specific hand index.
 *
 * Returns: %TRUE if the card was played
 */
gboolean
lrg_deckbuilder_template_play_card_at (LrgDeckbuilderTemplate *self,
                                       guint                   hand_index,
                                       gpointer                target)
{
    LrgDeckbuilderTemplatePrivate *priv;
    LrgHand *hand;
    LrgCardInstance *card;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), FALSE);

    priv = lrg_deckbuilder_template_get_instance_private (self);

    if (priv->deck_instance == NULL)
        return FALSE;

    hand = lrg_deck_instance_get_hand (priv->deck_instance);
    card = lrg_hand_get_card_at (hand, hand_index);

    if (card == NULL)
        return FALSE;

    return lrg_deckbuilder_template_play_card (self, card, target);
}

/**
 * lrg_deckbuilder_template_can_play_card:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): the card to check
 *
 * Checks if a card can be played (enough energy, valid target, etc.).
 *
 * Returns: %TRUE if the card can be played
 */
gboolean
lrg_deckbuilder_template_can_play_card (LrgDeckbuilderTemplate *self,
                                        LrgCardInstance        *card)
{
    LrgDeckbuilderTemplateClass *klass;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    if (klass->can_play_card != NULL)
        return klass->can_play_card (self, card, NULL);

    return lrg_deckbuilder_template_real_can_play_card (self, card, NULL);
}

/**
 * lrg_deckbuilder_template_get_card_cost:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): the card to evaluate
 *
 * Gets the effective cost to play a card.
 *
 * Returns: energy cost
 */
gint
lrg_deckbuilder_template_get_card_cost (LrgDeckbuilderTemplate *self,
                                        LrgCardInstance        *card)
{
    LrgDeckbuilderTemplateClass *klass;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), 0);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), 0);

    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    if (klass->evaluate_card_cost != NULL)
        return klass->evaluate_card_cost (self, card);

    return lrg_card_instance_get_effective_cost (card, NULL);
}

/**
 * lrg_deckbuilder_template_draw_cards:
 * @self: an #LrgDeckbuilderTemplate
 * @count: number of cards to draw
 *
 * Draws cards from the deck to hand.
 *
 * Returns: actual number of cards drawn
 */
guint
lrg_deckbuilder_template_draw_cards (LrgDeckbuilderTemplate *self,
                                     guint                   count)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), 0);

    return lrg_deck_mixin_draw_cards (LRG_DECK_MIXIN (self), count);
}

/**
 * lrg_deckbuilder_template_add_card_to_deck:
 * @self: an #LrgDeckbuilderTemplate
 * @card_def: (transfer none): card definition to add
 *
 * Adds a new card to the deck (master deck).
 */
void
lrg_deckbuilder_template_add_card_to_deck (LrgDeckbuilderTemplate *self,
                                           LrgCardDef             *card_def)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));
    g_return_if_fail (LRG_IS_CARD_DEF (card_def));

    priv = lrg_deckbuilder_template_get_instance_private (self);

    if (priv->deck_instance != NULL)
        lrg_deck_instance_add_card (priv->deck_instance, card_def);
}

/**
 * lrg_deckbuilder_template_remove_card_from_deck:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): card instance to remove
 *
 * Removes a card from the deck permanently.
 *
 * Returns: %TRUE if the card was removed
 */
gboolean
lrg_deckbuilder_template_remove_card_from_deck (LrgDeckbuilderTemplate *self,
                                                LrgCardInstance        *card)
{
    LrgDeckbuilderTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), FALSE);
    g_return_val_if_fail (LRG_IS_CARD_INSTANCE (card), FALSE);

    priv = lrg_deckbuilder_template_get_instance_private (self);

    if (priv->deck_instance == NULL)
        return FALSE;

    return lrg_deck_instance_remove_card (priv->deck_instance, card);
}

/* ==========================================================================
 * Public API - Energy Operations
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_spend_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @amount: energy to spend
 *
 * Spends energy.
 *
 * Returns: %TRUE if enough energy was available
 */
gboolean
lrg_deckbuilder_template_spend_energy (LrgDeckbuilderTemplate *self,
                                       gint                    amount)
{
    LrgDeckbuilderTemplatePrivate *priv;
    gint new_energy;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self), FALSE);
    g_return_val_if_fail (amount >= 0, FALSE);

    priv = lrg_deckbuilder_template_get_instance_private (self);

    if (priv->current_energy < amount)
        return FALSE;

    new_energy = priv->current_energy - amount;
    lrg_deckbuilder_template_set_current_energy (self, new_energy);

    return TRUE;
}

/**
 * lrg_deckbuilder_template_gain_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @amount: energy to gain
 *
 * Gains energy.
 */
void
lrg_deckbuilder_template_gain_energy (LrgDeckbuilderTemplate *self,
                                      gint                    amount)
{
    LrgDeckbuilderTemplatePrivate *priv;
    gint new_energy;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));
    g_return_if_fail (amount >= 0);

    priv = lrg_deckbuilder_template_get_instance_private (self);

    new_energy = priv->current_energy + amount;
    lrg_deckbuilder_template_set_current_energy (self, new_energy);
}

/**
 * lrg_deckbuilder_template_reset_energy:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Resets energy to max (or starting) value.
 */
void
lrg_deckbuilder_template_reset_energy (LrgDeckbuilderTemplate *self)
{
    LrgDeckbuilderTemplateClass *klass;
    gint starting_energy;

    g_return_if_fail (LRG_IS_DECKBUILDER_TEMPLATE (self));

    klass = LRG_DECKBUILDER_TEMPLATE_GET_CLASS (self);

    if (klass->get_starting_energy != NULL)
        starting_energy = klass->get_starting_energy (self);
    else
        starting_energy = lrg_deckbuilder_template_get_max_energy (self);

    lrg_deckbuilder_template_set_current_energy (self, starting_energy);
}
