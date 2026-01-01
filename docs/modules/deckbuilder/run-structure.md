# Run/Map System

The run system manages roguelike progression through procedurally generated maps with various node types including combat, shops, events, and rest sites.

## LrgRun

The run is the central state container for a roguelike playthrough. It implements `LrgSaveable` for persistence.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `character-id` | `gchar*` | Character being played |
| `seed` | `guint64` | Run seed for RNG |
| `current-act` | `gint` | Current act (1-4) |
| `current-floor` | `gint` | Current floor in act |
| `gold` | `gint` | Current gold |
| `max-health` | `gint` | Maximum HP |
| `current-health` | `gint` | Current HP |
| `ascension-level` | `gint` | Ascension difficulty |
| `state` | `LrgRunState` | Current run state |

### Run States

```c
typedef enum {
    LRG_RUN_STATE_MAP,      /* Viewing map, choosing path */
    LRG_RUN_STATE_COMBAT,   /* In combat */
    LRG_RUN_STATE_REWARDS,  /* Post-combat rewards */
    LRG_RUN_STATE_SHOP,     /* In shop */
    LRG_RUN_STATE_EVENT,    /* Random event */
    LRG_RUN_STATE_REST,     /* Rest site */
    LRG_RUN_STATE_VICTORY,  /* Run won */
    LRG_RUN_STATE_DEFEAT    /* Run lost */
} LrgRunState;
```

### Run Operations

```c
/* Create a new run */
LrgRun *run = lrg_run_new ("ironclad", seed);

/* Set ascension level */
lrg_run_set_ascension_level (run, 5);

/* Get deck */
LrgDeckInstance *deck = lrg_run_get_deck (run);

/* Get relics */
GPtrArray *relics = lrg_run_get_relics (run);

/* Get potions */
GPtrArray *potions = lrg_run_get_potions (run);

/* Gold management */
lrg_run_add_gold (run, 50);
lrg_run_spend_gold (run, 100);

/* Health management */
lrg_run_heal (run, 10);
lrg_run_take_damage (run, 5);
lrg_run_increase_max_health (run, 5);
```

## LrgMapNode

Map nodes represent locations on the procedural map.

### Node Types

```c
typedef enum {
    LRG_MAP_NODE_COMBAT,    /* Normal enemy encounter */
    LRG_MAP_NODE_ELITE,     /* Elite enemy encounter */
    LRG_MAP_NODE_BOSS,      /* Boss fight */
    LRG_MAP_NODE_EVENT,     /* Random event */
    LRG_MAP_NODE_SHOP,      /* Shop */
    LRG_MAP_NODE_REST,      /* Rest/campfire site */
    LRG_MAP_NODE_TREASURE,  /* Treasure chest */
    LRG_MAP_NODE_MYSTERY    /* Unknown node */
} LrgMapNodeType;
```

### Node Properties

| Property | Type | Description |
|----------|------|-------------|
| `type` | `LrgMapNodeType` | Node type |
| `x` | `gint` | Column position |
| `y` | `gint` | Row/floor position |
| `visited` | `gboolean` | Has been visited |
| `connections` | `GPtrArray*` | Connected nodes |

### Virtual Methods

```c
struct _LrgMapNodeClass
{
    GObjectClass parent_class;

    /* Called when player enters node */
    void (*on_enter) (LrgMapNode *self,
                      LrgRun     *run);

    /* Called when node is completed */
    void (*on_complete) (LrgMapNode *self,
                         LrgRun     *run);

    /* Get node icon for map display */
    const gchar * (*get_icon) (LrgMapNode *self);

    gpointer _reserved[8];
};
```

## Built-in Node Types

### Combat Node

```c
static void
combat_node_on_enter (LrgMapNode *node, LrgRun *run)
{
    /* Get encounter from pool */
    LrgEncounterPool *pool = lrg_run_get_encounter_pool (run);
    GPtrArray *enemies = lrg_encounter_pool_draw (pool, run->current_floor);

    /* Start combat */
    LrgCombatManager *combat = lrg_combat_manager_get_default ();
    lrg_combat_manager_start_combat (combat, run, enemies);
}
```

### Elite Node

```c
static void
elite_node_on_enter (LrgMapNode *node, LrgRun *run)
{
    /* Get elite encounter */
    LrgEncounterPool *pool = lrg_run_get_elite_pool (run);
    GPtrArray *enemies = lrg_encounter_pool_draw_elite (pool, run->current_floor);

    /* Start combat with elite rewards flag */
    LrgCombatManager *combat = lrg_combat_manager_get_default ();
    lrg_combat_manager_start_combat (combat, run, enemies);
    lrg_combat_manager_set_elite_fight (combat, TRUE);
}
```

### Rest Site Node

```c
static void
rest_node_on_enter (LrgMapNode *node, LrgRun *run)
{
    /* Present rest options */
    lrg_run_set_state (run, LRG_RUN_STATE_REST);

    /* Options:
     * - Rest: Heal 30% max HP
     * - Smith: Upgrade a card
     * - Dig: Get relic (with Shovel)
     * - Recall: Get old card (with Dream Catcher)
     */
}

/* Player chooses to rest */
void
rest_site_rest (LrgRun *run)
{
    gint heal_amount = lrg_run_get_max_health (run) * 0.3f;
    lrg_run_heal (run, heal_amount);
}

/* Player chooses to upgrade */
void
rest_site_smith (LrgRun *run, LrgCardInstance *card)
{
    lrg_card_instance_upgrade (card);
}
```

## LrgMapGenerator

The map generator creates procedural maps. It's derivable for custom generation.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `width` | `gint` | Map width (columns) |
| `height` | `gint` | Map height (rows) |
| `combat-weight` | `gfloat` | Weight for combat nodes |
| `event-weight` | `gfloat` | Weight for event nodes |
| `shop-floors` | `GPtrArray*` | Floors that have shops |
| `rest-floors` | `GPtrArray*` | Floors that have rest sites |

### Generation

```c
/* Get generator */
LrgMapGenerator *generator = lrg_map_generator_get_default ();

/* Generate map for act */
GPtrArray *nodes = lrg_map_generator_generate (generator, run, act);

/* Get starting nodes */
GPtrArray *starts = lrg_map_generator_get_starting_nodes (generator);

/* Get nodes at floor */
GPtrArray *floor_nodes = lrg_map_generator_get_nodes_at_floor (generator, floor);

/* Check if nodes are connected */
gboolean connected = lrg_map_node_is_connected_to (node_a, node_b);
```

### Custom Generator

```c
/* Override generation for custom maps */
G_DECLARE_FINAL_TYPE (MyMapGenerator, my_map_generator,
                       MY, MAP_GENERATOR, LrgMapGenerator)

static GPtrArray *
my_map_generator_generate (LrgMapGenerator *gen,
                            LrgRun          *run,
                            gint             act)
{
    /* Custom map generation logic */
    GPtrArray *nodes = g_ptr_array_new_with_free_func (g_object_unref);

    /* Add nodes with connections */
    /* ... */

    return nodes;
}
```

## LrgEncounterPool

Encounter pools manage weighted enemy selection.

```c
/* Create pool */
LrgEncounterPool *pool = lrg_encounter_pool_new ();

/* Add encounters */
lrg_encounter_pool_add (pool, "slime_small", 1.0f, 1, 5);   /* Floors 1-5 */
lrg_encounter_pool_add (pool, "slime_medium", 0.8f, 3, 10); /* Floors 3-10 */
lrg_encounter_pool_add (pool, "gremlin_gang", 0.5f, 5, 15); /* Floors 5-15 */

/* Draw encounter for floor */
GPtrArray *enemies = lrg_encounter_pool_draw (pool, current_floor);
```

## LrgCardPool

Card pools manage weighted card selection for rewards.

```c
/* Get card pool from character */
LrgCardPool *pool = lrg_character_def_get_card_pool (character);

/* Draw random card */
LrgCardDef *card = lrg_card_pool_draw (pool, rng);

/* Draw by rarity */
LrgCardDef *rare = lrg_card_pool_draw_rarity (pool, LRG_CARD_RARITY_RARE, rng);

/* Modify weights */
lrg_card_pool_set_modifier (pool, "strike", 0.5f);  /* Half as likely */
lrg_card_pool_remove_card (pool, "ascenders_bane"); /* Never offer */
```

## LrgShop

The shop provides purchasable items.

### Shop Items

```c
typedef enum {
    LRG_SHOP_ITEM_CARD,           /* Card for sale */
    LRG_SHOP_ITEM_RELIC,          /* Relic for sale */
    LRG_SHOP_ITEM_POTION,         /* Potion for sale */
    LRG_SHOP_ITEM_CARD_REMOVAL,   /* Remove a card */
    LRG_SHOP_ITEM_CARD_UPGRADE,   /* Upgrade a card (Slay the Spire mod) */
    LRG_SHOP_ITEM_CARD_TRANSFORM, /* Transform a card */
    LRG_SHOP_ITEM_SPECIAL         /* Special shop item */
} LrgShopItemType;
```

### Shop Operations

```c
/* Generate shop */
LrgShop *shop = lrg_shop_generate (run, rng);

/* Get items */
GPtrArray *cards = lrg_shop_get_cards (shop);
GPtrArray *relics = lrg_shop_get_relics (shop);
GPtrArray *potions = lrg_shop_get_potions (shop);

/* Check prices */
gint price = lrg_shop_item_get_price (item);
gboolean can_afford = lrg_shop_can_afford (shop, item, lrg_run_get_gold (run));

/* Purchase item */
if (lrg_shop_can_afford (shop, item, gold))
{
    lrg_shop_purchase (shop, item, run);
}

/* Card removal service */
gint removal_cost = lrg_shop_get_removal_cost (shop);
lrg_shop_remove_card (shop, run, card);
```

## LrgEventDef

Random events are derivable story encounters.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique event identifier |
| `name` | `gchar*` | Event title |
| `description` | `gchar*` | Event text |
| `image` | `gchar*` | Event image path |
| `options` | `GPtrArray*` | Available choices |

### Event Options

```c
/* Add options to event */
LrgEventOption *opt1 = lrg_event_option_new ("[Heal 20 HP] Rest for a while.");
lrg_event_option_add_effect (opt1, "heal", 20);
lrg_event_def_add_option (event, opt1);

LrgEventOption *opt2 = lrg_event_option_new ("[Lose 10 HP] Take the mysterious fruit.");
lrg_event_option_add_effect (opt2, "damage", 10);
lrg_event_option_add_effect (opt2, "add_relic", "mango");
lrg_event_def_add_option (event, opt2);

LrgEventOption *opt3 = lrg_event_option_new ("[Leave]");
lrg_event_def_add_option (event, opt3);
```

### Custom Events

```c
G_DECLARE_FINAL_TYPE (MyEvent, my_event, MY, EVENT, LrgEventDef)

static void
my_event_on_option_selected (LrgEventDef *event,
                              gint         option_index,
                              LrgRun      *run)
{
    switch (option_index)
    {
        case 0:
            lrg_run_heal (run, 20);
            break;
        case 1:
            lrg_run_take_damage (run, 10);
            lrg_run_add_relic (run, mango_relic);
            break;
        case 2:
            /* Leave, no effect */
            break;
    }
}
```

## LrgRewardScreen

Displays post-combat rewards.

```c
/* Generate rewards */
LrgRewardScreen *rewards = lrg_reward_screen_new (run, is_elite, is_boss);

/* Get available rewards */
gint gold = lrg_reward_screen_get_gold (rewards);
GPtrArray *cards = lrg_reward_screen_get_card_choices (rewards);
LrgPotionInstance *potion = lrg_reward_screen_get_potion (rewards);
LrgRelicInstance *relic = lrg_reward_screen_get_relic (rewards);

/* Player claims rewards */
lrg_reward_screen_claim_gold (rewards, run);
lrg_reward_screen_claim_card (rewards, run, chosen_card);
lrg_reward_screen_claim_potion (rewards, run);
lrg_reward_screen_claim_relic (rewards, run);

/* Skip card choice */
lrg_reward_screen_skip_card (rewards);
```

## See Also

- [Combat Documentation](combat.md)
- [Cards Documentation](cards.md)
- [Meta-Progression Documentation](meta-progression.md)
