# Modding Guide

The deckbuilder module is designed for maximum extensibility. All major types are derivable, and the `LrgCardProvider` interface enables mods to provide content.

## LrgCardProvider Interface

Mods implement `LrgCardProvider` to provide deckbuilder content:

```c
struct _LrgCardProviderInterface
{
    GTypeInterface g_iface;

    GList * (*get_card_defs)          (LrgCardProvider *self);
    GList * (*get_deck_defs)          (LrgCardProvider *self);
    GList * (*get_relic_defs)         (LrgCardProvider *self);
    GList * (*get_potion_defs)        (LrgCardProvider *self);
    GList * (*get_enemy_defs)         (LrgCardProvider *self);
    GList * (*get_event_defs)         (LrgCardProvider *self);
    GList * (*get_joker_defs)         (LrgCardProvider *self);
    GList * (*get_effect_executors)   (LrgCardProvider *self);
    GList * (*get_status_effect_defs) (LrgCardProvider *self);
    GList * (*get_keyword_defs)       (LrgCardProvider *self);
    GList * (*get_character_defs)     (LrgCardProvider *self);
};
```

### Implementing a Mod

```c
/* Define mod class */
G_DECLARE_FINAL_TYPE (MyMod, my_mod, MY, MOD, GObject)

/* Implement interfaces */
G_DEFINE_TYPE_WITH_CODE (MyMod, my_mod, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_MODABLE, my_mod_modable_init)
    G_IMPLEMENT_INTERFACE (LRG_TYPE_CARD_PROVIDER, my_mod_provider_init))

/* Provider implementation */
static GList *
my_mod_get_card_defs (LrgCardProvider *provider)
{
    GList *cards = NULL;

    /* Add custom cards */
    cards = g_list_append (cards, create_my_custom_card ());
    cards = g_list_append (cards, create_another_card ());

    return cards;
}

static GList *
my_mod_get_relic_defs (LrgCardProvider *provider)
{
    GList *relics = NULL;

    relics = g_list_append (relics, create_my_custom_relic ());

    return relics;
}

static void
my_mod_provider_init (LrgCardProviderInterface *iface)
{
    iface->get_card_defs = my_mod_get_card_defs;
    iface->get_relic_defs = my_mod_get_relic_defs;
    /* Set other methods as needed */
}
```

### Mod Lifecycle

```c
/* Modable interface */
static void
my_mod_init_impl (LrgModable *modable, LrgEngine *engine)
{
    MyMod *self = MY_MOD (modable);

    /* Register content with manager */
    LrgDeckbuilderManager *manager = lrg_deckbuilder_manager_get_default ();

    /* Cards are registered via provider interface */
    /* But we can also register manually */
    lrg_deckbuilder_manager_register_card (manager, self->special_card);
}

static void
my_mod_shutdown_impl (LrgModable *modable, LrgEngine *engine)
{
    /* Cleanup */
}

static void
my_mod_modable_init (LrgModableInterface *iface)
{
    iface->mod_init = my_mod_init_impl;
    iface->mod_shutdown = my_mod_shutdown_impl;
}
```

## Custom Cards

### Basic Custom Card

```c
/* Create card via API */
LrgCardDef *
create_my_custom_card (void)
{
    g_autoptr(LrgCardDef) card = lrg_card_def_new ("my_slash");
    lrg_card_def_set_name (card, "My Slash");
    lrg_card_def_set_description (card, "Deal {damage} damage.");
    lrg_card_def_set_card_type (card, LRG_CARD_TYPE_ATTACK);
    lrg_card_def_set_rarity (card, LRG_CARD_RARITY_COMMON);
    lrg_card_def_set_base_cost (card, 1);
    lrg_card_def_set_target_type (card, LRG_CARD_TARGET_SINGLE_ENEMY);

    /* Add damage effect */
    LrgCardEffect *effect = lrg_card_effect_new ("damage");
    lrg_card_effect_set_param_int (effect, "amount", 8);
    lrg_card_def_add_effect (card, effect);

    return g_steal_pointer (&card);
}
```

### Card with Custom Behavior

```c
/* Subclass for complex cards */
G_DECLARE_FINAL_TYPE (RampageCard, rampage_card, MY, RAMPAGE_CARD, LrgCardDef)

struct _RampageCard
{
    LrgCardDef parent_instance;
    gint times_played;
};

static gboolean
rampage_on_play (LrgCardDef      *def,
                 LrgCombatContext *ctx,
                 LrgCombatant     *target)
{
    RampageCard *self = MY_RAMPAGE_CARD (def);

    /* Base damage + 5 for each time played */
    gint damage = 8 + (self->times_played * 5);
    self->times_played++;

    lrg_combat_context_deal_damage (ctx, target, damage);
    return TRUE;
}

static gchar *
rampage_get_tooltip (LrgCardDef      *def,
                     LrgCombatContext *ctx)
{
    RampageCard *self = MY_RAMPAGE_CARD (def);
    gint damage = 8 + (self->times_played * 5);

    return g_strdup_printf ("Deal %d damage. Damage increases by 5 each time played this combat.", damage);
}
```

## Custom Relics

### Basic Custom Relic

```c
LrgRelicDef *
create_my_custom_relic (void)
{
    g_autoptr(LrgRelicDef) relic = lrg_relic_def_new ("my_ring");
    lrg_relic_def_set_name (relic, "Ring of Power");
    lrg_relic_def_set_description (relic, "At the start of each combat, gain 2 Strength.");
    lrg_relic_def_set_rarity (relic, LRG_RELIC_RARITY_UNCOMMON);

    return g_steal_pointer (&relic);
}
```

### Relic with Custom Behavior

```c
G_DECLARE_FINAL_TYPE (PowerRing, power_ring, MY, POWER_RING, LrgRelicDef)

static void
power_ring_on_combat_start (LrgRelicDef      *def,
                             LrgCombatContext *ctx)
{
    LrgCombatant *player = lrg_combat_context_get_player (ctx);
    lrg_combatant_apply_status (player, "strength", 2);
}

static void
power_ring_class_init (PowerRingClass *klass)
{
    LrgRelicDefClass *relic_class = LRG_RELIC_DEF_CLASS (klass);
    relic_class->on_combat_start = power_ring_on_combat_start;
}
```

## Custom Status Effects

```c
G_DECLARE_FINAL_TYPE (BurningStatus, burning_status, MY, BURNING_STATUS, LrgStatusEffectDef)

static void
burning_on_turn_start (LrgStatusEffectDef *def,
                       LrgCombatant       *target,
                       gint                stacks)
{
    /* Deal 2 damage per stack at turn start */
    lrg_combatant_lose_hp (target, stacks * 2);
}

static void
burning_on_turn_end (LrgStatusEffectDef *def,
                     LrgCombatant       *target,
                     gint                stacks)
{
    /* Reduce by 1 each turn */
    lrg_combatant_remove_status (target, "burning", 1);
}
```

## Custom Characters

```c
G_DECLARE_FINAL_TYPE (MyCharacter, my_character, MY, CHARACTER, LrgCharacterDef)

static GPtrArray *
my_character_get_starting_deck (LrgCharacterDef *def)
{
    GPtrArray *deck = g_ptr_array_new ();

    /* Custom starting cards */
    for (gint i = 0; i < 4; i++)
        g_ptr_array_add (deck, g_strdup ("quick_strike"));
    for (gint i = 0; i < 4; i++)
        g_ptr_array_add (deck, g_strdup ("dodge"));
    g_ptr_array_add (deck, g_strdup ("backstab"));
    g_ptr_array_add (deck, g_strdup ("neutralize"));

    return deck;
}

static GPtrArray *
my_character_get_starting_relics (LrgCharacterDef *def)
{
    GPtrArray *relics = g_ptr_array_new ();
    g_ptr_array_add (relics, g_strdup ("my_starting_relic"));
    return relics;
}
```

## Custom Enemies

```c
G_DECLARE_FINAL_TYPE (MyEnemy, my_enemy, MY, ENEMY, LrgEnemyDef)

static LrgEnemyIntent *
my_enemy_choose_intent (LrgEnemyDef      *def,
                         LrgEnemyInstance *instance,
                         LrgCombatContext *ctx)
{
    gint turn = lrg_combat_context_get_turn (ctx);
    LrgEnemyIntent *intent;

    if (turn % 3 == 0)
    {
        /* Every 3rd turn, buff self */
        intent = lrg_enemy_intent_new (LRG_ENEMY_INTENT_BUFF);
        lrg_enemy_intent_set_status (intent, "strength");
        lrg_enemy_intent_set_stacks (intent, 2);
    }
    else
    {
        /* Otherwise attack */
        intent = lrg_enemy_intent_new (LRG_ENEMY_INTENT_ATTACK);
        lrg_enemy_intent_set_damage (intent, 10);
    }

    return intent;
}
```

## Custom Effects

### Effect Executor

```c
G_DECLARE_FINAL_TYPE (LifestealExecutor, lifesteal_executor, MY, LIFESTEAL_EXECUTOR, GObject)

static const gchar *
lifesteal_get_effect_type (LrgCardEffectExecutor *exec)
{
    return "lifesteal";
}

static gboolean
lifesteal_execute (LrgCardEffectExecutor *exec,
                   LrgCardEffect         *effect,
                   LrgCombatContext      *ctx,
                   LrgCombatant          *source,
                   LrgCombatant          *target)
{
    gint amount = lrg_card_effect_get_param_int (effect, "amount");

    /* Deal damage */
    gint dealt = lrg_combat_context_deal_damage (ctx, target, amount);

    /* Heal for damage dealt */
    lrg_combatant_heal (source, dealt);

    return TRUE;
}

static void
lifesteal_executor_iface_init (LrgCardEffectExecutorInterface *iface)
{
    iface->get_effect_type = lifesteal_get_effect_type;
    iface->execute = lifesteal_execute;
}

/* Register in mod init */
LrgCardEffectRegistry *registry = lrg_card_effect_registry_get_default ();
lrg_card_effect_registry_register (registry, executor);
```

## Custom Jokers

```c
G_DECLARE_FINAL_TYPE (MyJoker, my_joker, MY, JOKER, LrgJokerDef)

static gdouble
my_joker_modify_mult (LrgJokerDef      *def,
                      LrgScoringContext *ctx,
                      gdouble           mult)
{
    /* +4 mult if hand contains a pair */
    LrgHandType hand_type = lrg_scoring_context_get_hand_type (ctx);

    if (hand_type == LRG_HAND_TYPE_PAIR ||
        hand_type == LRG_HAND_TYPE_TWO_PAIR ||
        hand_type == LRG_HAND_TYPE_FULL_HOUSE)
    {
        return mult + 4.0;
    }

    return mult;
}
```

## YAML Content

Mods can also provide content via YAML files:

### mod-manifest.yaml

```yaml
type: mod-manifest
id: "my-awesome-mod"
name: "My Awesome Mod"
version: "1.0.0"
author: "Your Name"
description: "Adds new cards, relics, and a character."
content:
  cards:
    - "data/cards/my_slash.yaml"
    - "data/cards/my_defend.yaml"
  relics:
    - "data/relics/power_ring.yaml"
  characters:
    - "data/characters/my_character.yaml"
  enemies:
    - "data/enemies/my_enemy.yaml"
```

### Card YAML

```yaml
type: card-def
id: "my_slash"
name: "My Slash"
description: "Deal {damage} damage."
card-type: attack
rarity: common
base-cost: 1
target-type: single-enemy
upgradeable: true
upgraded-version-id: "my_slash+"
effects:
  - type: damage
    params:
      amount: 8
tags:
  - my-mod
```

## Best Practices

1. **Unique IDs**: Prefix all IDs with your mod name to avoid conflicts
2. **GObject Patterns**: Follow libregnum's GObject patterns for consistency
3. **Virtual Methods**: Override virtual methods rather than duplicating logic
4. **Event Bus**: Use the event bus for complex interactions
5. **Save Compatibility**: Ensure custom types can save/load properly
6. **Testing**: Write tests for custom content

## Debugging Mods

```c
/* Enable debug logging */
g_setenv ("G_MESSAGES_DEBUG", "Libregnum-Deckbuilder", TRUE);

/* Log custom events */
lrg_debug (LRG_LOG_DOMAIN_DECKBUILDER, "My card played: %s", card_id);
```

## See Also

- [Cards Documentation](cards.md)
- [Effects Documentation](effects.md)
- [Relics Documentation](relics.md)
- [Meta-Progression Documentation](meta-progression.md)
- [Mod System Documentation](../mod/index.md)
