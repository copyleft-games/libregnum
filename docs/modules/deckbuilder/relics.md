# Relics and Potions

Relics provide passive bonuses throughout a run. Potions are consumable items with powerful one-time effects.

## LrgRelicDef

Relic definitions are derivable GObjects with lifecycle hooks for various game events.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique relic identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Relic description |
| `flavor-text` | `gchar*` | Lore text |
| `icon` | `gchar*` | Icon path |
| `rarity` | `LrgRelicRarity` | Relic rarity |
| `character` | `gchar*` | Character restriction (NULL for any) |

### Relic Rarities

```c
typedef enum {
    LRG_RELIC_RARITY_STARTER,   /* Starting relics */
    LRG_RELIC_RARITY_COMMON,    /* Frequently found */
    LRG_RELIC_RARITY_UNCOMMON,  /* Less common */
    LRG_RELIC_RARITY_RARE,      /* Powerful rare relics */
    LRG_RELIC_RARITY_BOSS,      /* Dropped by bosses */
    LRG_RELIC_RARITY_EVENT,     /* Event exclusive */
    LRG_RELIC_RARITY_SHOP       /* Shop exclusive */
} LrgRelicRarity;
```

### Virtual Methods

```c
struct _LrgRelicDefClass
{
    GObjectClass parent_class;

    /* Lifecycle hooks */
    void (*on_acquired)     (LrgRelicDef      *self,
                             LrgCombatContext *ctx);

    void (*on_combat_start) (LrgRelicDef      *self,
                             LrgCombatContext *ctx);

    void (*on_combat_end)   (LrgRelicDef      *self,
                             LrgCombatContext *ctx);

    void (*on_turn_start)   (LrgRelicDef      *self,
                             LrgCombatContext *ctx);

    void (*on_turn_end)     (LrgRelicDef      *self,
                             LrgCombatContext *ctx);

    /* Event hooks */
    void (*on_card_played)  (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             LrgCardInstance  *card);

    void (*on_damage_dealt) (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             gint              damage,
                             LrgCombatant     *target);

    void (*on_damage_taken) (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             gint              damage);

    void (*on_gold_gained)  (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             gint              amount);

    void (*on_card_drawn)   (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             LrgCardInstance  *card);

    /* Modifier hooks */
    gint (*modify_damage)   (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             gint              damage,
                             gboolean          outgoing);

    gint (*modify_block)    (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             gint              block);

    gint (*modify_energy)   (LrgRelicDef      *self,
                             LrgCombatContext *ctx,
                             gint              energy);

    gpointer _reserved[8];
};
```

## Built-in Relics

### Burning Blood

```c
/* Heal 6 HP at end of combat */
static void
burning_blood_on_combat_end (LrgRelicDef      *self,
                             LrgCombatContext *ctx)
{
    LrgCombatant *player = lrg_combat_context_get_player (ctx);
    lrg_combatant_heal (player, 6);
}
```

### Ring of the Snake

```c
/* Draw 2 extra cards at start of each combat */
static void
ring_of_snake_on_combat_start (LrgRelicDef      *self,
                                LrgCombatContext *ctx)
{
    lrg_combat_context_draw_cards (ctx, 2);
}
```

### Vajra

```c
/* Start each combat with 1 Strength */
static void
vajra_on_combat_start (LrgRelicDef      *self,
                       LrgCombatContext *ctx)
{
    LrgCombatant *player = lrg_combat_context_get_player (ctx);
    lrg_combatant_apply_status (player, "strength", 1);
}
```

## LrgRelicInstance

Relic instances track runtime state for relics with counters.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `definition` | `LrgRelicDef*` | The relic definition |
| `counter` | `gint` | Counter value (for triggered relics) |
| `enabled` | `gboolean` | Whether relic is active |

### Operations

```c
/* Create instance */
LrgRelicInstance *relic = lrg_relic_instance_new (burning_blood_def);

/* Counter operations */
lrg_relic_instance_set_counter (relic, 3);
lrg_relic_instance_increment_counter (relic, 1);
gint count = lrg_relic_instance_get_counter (relic);

/* Enable/disable */
lrg_relic_instance_set_enabled (relic, FALSE);
```

## LrgRelicRegistry

```c
/* Get registry */
LrgRelicRegistry *registry = lrg_relic_registry_get_default ();

/* Register relic */
lrg_relic_registry_register (registry, relic_def);

/* Look up relic */
LrgRelicDef *def = lrg_relic_registry_get (registry, "burning-blood");

/* Get relics by rarity */
GList *common = lrg_relic_registry_get_by_rarity (registry, LRG_RELIC_RARITY_COMMON);
```

## LrgPotionDef

Potion definitions are derivable GObjects for consumable items.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique potion identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Potion description |
| `icon` | `gchar*` | Icon path |
| `rarity` | `LrgPotionRarity` | Potion rarity |
| `requires-target` | `gboolean` | Needs a target to use |

### Virtual Methods

```c
struct _LrgPotionDefClass
{
    GObjectClass parent_class;

    /* Check if potion can be used */
    gboolean (*can_use)    (LrgPotionDef     *self,
                            LrgCombatContext *ctx);

    /* Use the potion */
    void     (*on_use)     (LrgPotionDef     *self,
                            LrgCombatContext *ctx,
                            LrgCombatant     *target);

    /* Generate tooltip */
    gchar *  (*get_tooltip)(LrgPotionDef     *self,
                            LrgCombatContext *ctx);

    gpointer _reserved[8];
};
```

## Built-in Potions

### Health Potion

```c
static void
health_potion_on_use (LrgPotionDef     *self,
                      LrgCombatContext *ctx,
                      LrgCombatant     *target)
{
    LrgCombatant *player = lrg_combat_context_get_player (ctx);
    lrg_combatant_heal (player, 30);
}
```

### Fire Potion

```c
static void
fire_potion_on_use (LrgPotionDef     *self,
                    LrgCombatContext *ctx,
                    LrgCombatant     *target)
{
    /* Deal 20 damage to target */
    lrg_combat_context_deal_damage (ctx, target, 20);
}
```

### Block Potion

```c
static void
block_potion_on_use (LrgPotionDef     *self,
                     LrgCombatContext *ctx,
                     LrgCombatant     *target)
{
    LrgCombatant *player = lrg_combat_context_get_player (ctx);
    lrg_combatant_add_block (player, 12);
}
```

## Potion Management

```c
/* Get potion slots from run */
GPtrArray *potions = lrg_run_get_potions (run);
guint max_potions = lrg_run_get_max_potions (run);

/* Add potion */
if (lrg_run_can_add_potion (run))
{
    lrg_run_add_potion (run, fire_potion_instance);
}

/* Use potion */
LrgPotionInstance *potion = g_ptr_array_index (potions, 0);
if (lrg_potion_instance_can_use (potion, ctx))
{
    lrg_potion_instance_use (potion, ctx, target);
    lrg_run_remove_potion (run, potion);
}

/* Discard potion (to make room) */
lrg_run_discard_potion (run, potion);
```

## Creating Custom Relics

```c
/* Define a "Mana Crystal" relic that gives +1 energy */
G_DECLARE_FINAL_TYPE (ManaCrystal, mana_crystal, MY, MANA_CRYSTAL, LrgRelicDef)

static gint
mana_crystal_modify_energy (LrgRelicDef      *self,
                            LrgCombatContext *ctx,
                            gint              energy)
{
    return energy + 1;
}

static void
mana_crystal_class_init (ManaCrystalClass *klass)
{
    LrgRelicDefClass *relic_class = LRG_RELIC_DEF_CLASS (klass);
    relic_class->modify_energy = mana_crystal_modify_energy;
}
```

## YAML Definitions

### Relic YAML

```yaml
type: relic-def
id: "burning-blood"
name: "Burning Blood"
description: "At the end of combat, heal 6 HP."
rarity: starter
icon: "relics/burning-blood.png"
triggers:
  - event: combat-end
    effect: heal
    params:
      amount: 6
```

### Potion YAML

```yaml
type: potion-def
id: "fire-potion"
name: "Fire Potion"
description: "Deal 20 damage to a target enemy."
rarity: common
requires-target: true
icon: "potions/fire.png"
effects:
  - type: damage
    params:
      amount: 20
```

## See Also

- [Combat Documentation](combat.md)
- [Run Structure Documentation](run-structure.md)
- [Status Effects Documentation](status-effects.md)
