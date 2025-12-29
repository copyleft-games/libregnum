---
title: Economy System
---

# Economy/Resource System

Libregnum's economy module provides resource management, production chains, and market simulation for tycoon, economy, and idle games.

> **[Home](index.md)** > Economy

## Overview

The economy system consists of 8 core classes:

| Class | Type | Description |
|-------|------|-------------|
| `LrgResource` | Derivable | Resource definition (currency, material, energy) |
| `LrgResourcePool` | Final | Container storing resource quantities |
| `LrgProductionRecipe` | Final | Input->output crafting/production recipe |
| `LrgProducer` | Component | Component that produces resources over time |
| `LrgConsumer` | Component | Component that consumes resources continuously |
| `LrgMarket` | Final | Supply/demand price simulation |
| `LrgEconomyManager` | Singleton | Central registry for resources and recipes |
| `LrgOfflineCalculator` | Final | Calculate offline progress for idle games |

## Quick Start

```c
#include <libregnum.h>

/* Create resources */
LrgResource *gold = lrg_resource_new ("gold");
lrg_resource_set_name (gold, "Gold");
lrg_resource_set_category (gold, LRG_RESOURCE_CATEGORY_CURRENCY);

LrgResource *wood = lrg_resource_new ("wood");
lrg_resource_set_name (wood, "Wood");
lrg_resource_set_category (wood, LRG_RESOURCE_CATEGORY_MATERIAL);

/* Create a resource pool */
LrgResourcePool *pool = lrg_resource_pool_new ();
lrg_resource_pool_add (pool, gold, 100.0);
lrg_resource_pool_add (pool, wood, 50.0);

/* Check if can afford something */
if (lrg_resource_pool_has (pool, gold, 25.0))
{
    lrg_resource_pool_remove (pool, gold, 25.0);
    /* Purchase complete */
}

/* Register with economy manager */
LrgEconomyManager *manager = lrg_economy_manager_get_default ();
lrg_economy_manager_register_resource (manager, gold);
lrg_economy_manager_register_resource (manager, wood);

/* Clean up */
g_object_unref (pool);
g_object_unref (gold);
g_object_unref (wood);
```

## Core Concepts

### Resources

Resources are the fundamental unit of the economy. Each resource has:

- **ID**: Unique identifier for lookups
- **Name**: Human-readable display name
- **Category**: Currency, material, food, energy, population, or custom
- **Min/Max Value**: Bounds for validation
- **Decimal Places**: Precision for formatting

```c
LrgResource *mana = lrg_resource_new ("mana");
lrg_resource_set_name (mana, "Mana Points");
lrg_resource_set_category (mana, LRG_RESOURCE_CATEGORY_ENERGY);
lrg_resource_set_min_value (mana, 0.0);
lrg_resource_set_max_value (mana, 100.0);
lrg_resource_set_decimal_places (mana, 0);

/* Format for display */
g_autofree gchar *display = lrg_resource_format_value (mana, 75.0);
/* Returns "75" */
```

**Categories**:

```c
typedef enum {
    LRG_RESOURCE_CATEGORY_CURRENCY,   /* Gold, coins, gems */
    LRG_RESOURCE_CATEGORY_MATERIAL,   /* Wood, stone, ore */
    LRG_RESOURCE_CATEGORY_FOOD,       /* Wheat, meat, bread */
    LRG_RESOURCE_CATEGORY_ENERGY,     /* Power, mana, fuel */
    LRG_RESOURCE_CATEGORY_POPULATION, /* Workers, citizens */
    LRG_RESOURCE_CATEGORY_CUSTOM      /* Game-specific */
} LrgResourceCategory;
```

### Resource Pools

A ResourcePool is a container that stores quantities of multiple resources:

```c
LrgResourcePool *inventory = lrg_resource_pool_new ();

/* Add resources */
lrg_resource_pool_add (inventory, gold, 500.0);
lrg_resource_pool_add (inventory, wood, 200.0);

/* Query amounts */
gdouble gold_amount = lrg_resource_pool_get (inventory, gold);

/* Check affordability */
gboolean can_buy = lrg_resource_pool_has (inventory, gold, 100.0);

/* Remove resources (fails if insufficient) */
if (lrg_resource_pool_remove (inventory, gold, 100.0))
{
    /* Success */
}

/* Transfer between pools */
lrg_resource_pool_transfer (source_pool, dest_pool, gold, 50.0);

/* Merge entire pool into another */
lrg_resource_pool_merge (dest_pool, source_pool);

/* Clear all resources */
lrg_resource_pool_clear (inventory);
```

**Signals**:

- `resource-changed`: Emitted when any resource amount changes
- `resource-depleted`: Emitted when a resource reaches zero

```c
static void
on_resource_changed (LrgResourcePool *pool,
                     LrgResource     *resource,
                     gdouble          old_amount,
                     gdouble          new_amount,
                     gpointer         user_data)
{
    g_print ("%s changed from %.2f to %.2f\n",
             lrg_resource_get_name (resource),
             old_amount, new_amount);
}

g_signal_connect (pool, "resource-changed",
                  G_CALLBACK (on_resource_changed), NULL);
```

**Multipliers**:

Pools support per-resource multipliers for bonuses:

```c
/* Double gold income */
lrg_resource_pool_set_multiplier (pool, gold, 2.0);

lrg_resource_pool_add (pool, gold, 100.0);
/* Actually adds 200 gold */
```

### Production Recipes

Recipes define transformations from inputs to outputs:

```c
/* Create a smelting recipe: 2 iron ore + 1 coal -> 1 iron bar */
LrgProductionRecipe *smelt_iron = lrg_production_recipe_new ("smelt_iron");
lrg_production_recipe_set_name (smelt_iron, "Smelt Iron");
lrg_production_recipe_set_production_time (smelt_iron, 5.0);  /* 5 seconds */

/* Add inputs */
lrg_production_recipe_add_input (smelt_iron, iron_ore, 2.0);
lrg_production_recipe_add_input (smelt_iron, coal, 1.0);

/* Add output (with 100% chance) */
lrg_production_recipe_add_output (smelt_iron, iron_bar, 1.0, 1.0);

/* Check if production is possible */
if (lrg_production_recipe_can_produce (smelt_iron, input_pool))
{
    /* Execute production */
    lrg_production_recipe_produce (smelt_iron, input_pool, output_pool);
}
```

**Output Chances**:

Outputs can have probabilistic drops:

```c
/* 100% chance for 1 iron bar */
lrg_production_recipe_add_output (recipe, iron_bar, 1.0, 1.0);

/* 25% chance for rare gem */
lrg_production_recipe_add_output (recipe, rare_gem, 1.0, 0.25);

/* Use guaranteed production for testing */
lrg_production_recipe_produce_guaranteed (recipe, input, output);
```

### Producers (Component)

LrgProducer is a component that handles production over time:

```c
/* Create producer component */
LrgProducer *sawmill = lrg_producer_new ();
lrg_producer_set_recipe (sawmill, wood_plank_recipe);
lrg_producer_set_resource_pool (sawmill, factory_pool);
lrg_producer_set_auto_restart (sawmill, TRUE);
lrg_producer_set_rate_multiplier (sawmill, 1.5);  /* 50% faster */

/* Start production */
lrg_producer_start_production (sawmill);

/* In game loop */
lrg_producer_update (sawmill, delta_time);

/* Query state */
gboolean is_producing = lrg_producer_get_is_producing (sawmill);
gdouble progress = lrg_producer_get_progress (sawmill);  /* 0.0 to 1.0 */
```

**Virtual Methods** (for subclassing):

```c
struct _LrgProducerClass
{
    LrgComponentClass parent_class;

    void (*on_production_started)  (LrgProducer *self);
    void (*on_production_complete) (LrgProducer *self);
    gboolean (*can_produce)        (LrgProducer *self);
};
```

**Signals**:

- `production-started`: Emitted when production begins
- `production-complete`: Emitted when production finishes

### Consumers (Component)

LrgConsumer handles continuous resource consumption:

```c
/* Create consumer component */
LrgConsumer *population = lrg_consumer_new ();
lrg_consumer_set_resource_pool (population, city_pool);

/* Require 10 food per second */
lrg_consumer_add_requirement (population, food, 10.0);

/* Require 5 water per second */
lrg_consumer_add_requirement (population, water, 5.0);

/* In game loop */
lrg_consumer_update (population, delta_time);

/* Check satisfaction */
gdouble satisfaction = lrg_consumer_get_satisfaction (population);  /* 0.0 to 1.0 */
gboolean is_starving = lrg_consumer_get_is_starved (population);
```

**Virtual Methods**:

```c
struct _LrgConsumerClass
{
    LrgComponentClass parent_class;

    void (*on_starved)   (LrgConsumer *self, LrgResource *resource);
    void (*on_satisfied) (LrgConsumer *self);
};
```

**Signals**:

- `starved`: Emitted when a requirement cannot be met
- `satisfied`: Emitted when all requirements are met again

### Market Simulation

The market simulates supply/demand price fluctuations:

```c
LrgMarket *market = lrg_market_new ();

/* Register tradeable resources */
lrg_market_register_resource (market, wheat,
                              10.0,   /* base price */
                              5.0,    /* min price */
                              50.0);  /* max price */

/* Set market properties */
lrg_market_set_volatility (market, 0.2);      /* Price fluctuation rate */
lrg_market_set_buy_markup (market, 1.1);      /* 10% markup when buying */
lrg_market_set_sell_markdown (market, 0.9);   /* 10% markdown when selling */

/* Get prices */
gdouble current_price = lrg_market_get_price (market, wheat);
gdouble buy_price = lrg_market_get_buy_price (market, wheat);
gdouble sell_price = lrg_market_get_sell_price (market, wheat);

/* Execute transactions */
if (lrg_market_buy (market, wheat, 100.0, gold, player_pool))
{
    /* Bought 100 wheat, paid in gold */
}

if (lrg_market_sell (market, wheat, 50.0, gold, player_pool))
{
    /* Sold 50 wheat, received gold */
}

/* Update market (call each frame) */
lrg_market_update (market, delta_time);

/* Supply/demand affects prices */
lrg_market_add_supply (market, wheat, 1000.0);  /* Price drops */
lrg_market_add_demand (market, wheat, 500.0);   /* Price rises */
```

### Economy Manager (Singleton)

The EconomyManager provides central registration and lookup:

```c
LrgEconomyManager *manager = lrg_economy_manager_get_default ();

/* Register resources */
lrg_economy_manager_register_resource (manager, gold);
lrg_economy_manager_register_resource (manager, wood);
lrg_economy_manager_register_resource (manager, iron);

/* Lookup by ID */
LrgResource *res = lrg_economy_manager_get_resource (manager, "gold");

/* Get resources by category */
GList *materials = lrg_economy_manager_get_resources_by_category (
    manager, LRG_RESOURCE_CATEGORY_MATERIAL);
g_list_free (materials);

/* Register recipes */
lrg_economy_manager_register_recipe (manager, smelt_iron);

/* Find recipes that produce a resource */
GList *iron_recipes = lrg_economy_manager_get_recipes_for_output (manager, iron);

/* Get/set the global market */
LrgMarket *market = lrg_economy_manager_get_market (manager);

/* Update all systems */
lrg_economy_manager_update (manager, delta_time);

/* Clear all registrations */
lrg_economy_manager_clear (manager);
```

### Offline Calculator

For idle games, calculate progress while the game was closed:

```c
LrgOfflineCalculator *calc = lrg_offline_calculator_new ();

/* Configure settings */
lrg_offline_calculator_set_efficiency (calc, 0.5);     /* 50% production */
lrg_offline_calculator_set_max_hours (calc, 8.0);      /* Cap at 8 hours */
lrg_offline_calculator_set_min_seconds (calc, 60.0);   /* Ignore < 1 minute */

/* Register producers to track */
lrg_offline_calculator_add_producer (calc, sawmill);
lrg_offline_calculator_add_producer (calc, mine);

/* On game close: save timestamp */
lrg_offline_calculator_take_snapshot (calc);
gint64 timestamp = lrg_offline_calculator_get_snapshot_time (calc);
/* Save timestamp to save file */

/* On game open: restore and calculate */
lrg_offline_calculator_set_snapshot_time (calc, saved_timestamp);

/* Calculate gains and apply to pool */
gdouble offline_seconds = lrg_offline_calculator_apply (calc, player_pool);
g_print ("You were away for %.1f hours\n", offline_seconds / 3600.0);

/* Or calculate without applying */
LrgResourcePool *gains = lrg_resource_pool_new ();
offline_seconds = lrg_offline_calculator_calculate (calc, gains);
/* Show gains to player, then merge */
lrg_resource_pool_merge (player_pool, gains);
```

## Integration Patterns

### Attaching to Entities

Producers and Consumers are components for the ECS:

```c
/* Create entity */
LrgEntity *factory = lrg_entity_new ("factory");

/* Add producer component */
LrgProducer *producer = lrg_producer_new ();
lrg_producer_set_recipe (producer, factory_recipe);
lrg_producer_set_resource_pool (producer, factory_storage);
lrg_entity_add_component (factory, LRG_COMPONENT (producer));

/* Components update with entity */
lrg_entity_update (factory, delta_time);
```

### YAML Data Loading

Resources and recipes can be defined in YAML:

```yaml
# resources/materials.yaml
---
type: resource
id: iron_ore
name: "Iron Ore"
category: material
min_value: 0
max_value: 10000
---
type: resource
id: coal
name: "Coal"
category: energy
---
type: resource
id: iron_bar
name: "Iron Bar"
category: material
```

```yaml
# recipes/smelting.yaml
---
type: production_recipe
id: smelt_iron
name: "Smelt Iron"
production_time: 5.0
inputs:
  - resource: iron_ore
    amount: 2
  - resource: coal
    amount: 1
outputs:
  - resource: iron_bar
    amount: 1
    chance: 1.0
```

### Save/Load Support

Resource pools serialize naturally:

```c
/* Save pool state */
void
save_pool_to_yaml (LrgResourcePool *pool, const gchar *path)
{
    /* Pool implements LrgSaveable interface */
    YamlBuilder *builder = yaml_builder_new ();
    lrg_saveable_save (LRG_SAVEABLE (pool), builder);
    /* ... write to file ... */
}

/* Load pool state */
LrgResourcePool *
load_pool_from_yaml (const gchar *path, LrgEconomyManager *manager)
{
    LrgDataLoader *loader = lrg_data_loader_new ();
    lrg_data_loader_set_registry (loader, registry);

    GObject *obj = lrg_data_loader_load_file (loader, path, &error);
    return LRG_RESOURCE_POOL (obj);
}
```

## Best Practices

### 1. Resource Naming

Use clear, consistent IDs:

```c
/* Good */
lrg_resource_new ("gold_coin");
lrg_resource_new ("iron_ore");
lrg_resource_new ("wood_plank");

/* Bad */
lrg_resource_new ("Gold");    /* Uppercase */
lrg_resource_new ("res_001"); /* Non-descriptive */
```

### 2. Pool Organization

Use separate pools for different purposes:

```c
LrgResourcePool *player_inventory;   /* What player carries */
LrgResourcePool *bank_storage;       /* Stored in bank */
LrgResourcePool *factory_input;      /* Factory input buffer */
LrgResourcePool *factory_output;     /* Factory output buffer */
```

### 3. Recipe Validation

Check requirements before starting production:

```c
if (!lrg_production_recipe_can_produce (recipe, pool))
{
    /* Show error to player */
    return;
}
lrg_producer_start_production (producer);
```

### 4. Market Balance

Tune volatility and price bounds carefully:

```c
/* Low volatility for stable economy */
lrg_market_set_volatility (market, 0.05);

/* Tight price bounds to prevent exploitation */
lrg_market_register_resource (market, wheat,
                              10.0,  /* base */
                              8.0,   /* min: 80% of base */
                              15.0); /* max: 150% of base */
```

### 5. Offline Fairness

Balance offline progress:

```c
/* Typical idle game settings */
lrg_offline_calculator_set_efficiency (calc, 0.25);   /* 25% of active */
lrg_offline_calculator_set_max_hours (calc, 12.0);    /* Cap at 12 hours */
lrg_offline_calculator_set_min_seconds (calc, 300.0); /* Ignore < 5 min */
```

## See Also

- [Architecture Overview](architecture.md) - System design principles
- [ECS Module](ecs.md) - Entity-Component-System for game objects
- [Save System](save.md) - Serialization support
- [Building System](building.md) - Buildings that use producers/consumers
