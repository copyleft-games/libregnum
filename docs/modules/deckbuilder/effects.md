# Effect System

The effect system provides a flexible, extensible way to define and execute card effects. It includes effect data containers, an executor interface, a resolution stack for proper ordering, and an event bus for triggers.

## LrgCardEffect

`LrgCardEffect` is a boxed type that holds effect data. It stores the effect type and parameters needed for execution.

### Creating Effects

```c
/* Create a damage effect */
LrgCardEffect *damage = lrg_card_effect_new ("damage");
lrg_card_effect_set_param_int (damage, "amount", 6);
lrg_card_effect_set_param_int (damage, "target", LRG_CARD_TARGET_SINGLE_ENEMY);

/* Create a block effect */
LrgCardEffect *block = lrg_card_effect_new ("block");
lrg_card_effect_set_param_int (block, "amount", 5);

/* Create a draw effect */
LrgCardEffect *draw = lrg_card_effect_new ("draw");
lrg_card_effect_set_param_int (draw, "count", 2);

/* Create an apply-status effect */
LrgCardEffect *poison = lrg_card_effect_new ("apply-status");
lrg_card_effect_set_param_string (poison, "status_id", "poison");
lrg_card_effect_set_param_int (poison, "stacks", 3);
```

### Effect Parameters

```c
/* Set parameters */
lrg_card_effect_set_param_int (effect, "amount", 10);
lrg_card_effect_set_param_float (effect, "multiplier", 1.5f);
lrg_card_effect_set_param_string (effect, "status", "vulnerable");
lrg_card_effect_set_param_bool (effect, "random", TRUE);

/* Get parameters */
gint amount = lrg_card_effect_get_param_int (effect, "amount");
gfloat mult = lrg_card_effect_get_param_float (effect, "multiplier");
const gchar *status = lrg_card_effect_get_param_string (effect, "status");
gboolean random = lrg_card_effect_get_param_bool (effect, "random");
```

### Effect Flags

```c
typedef enum {
    LRG_EFFECT_FLAG_NONE          = 0,
    LRG_EFFECT_FLAG_UNBLOCKABLE   = 1 << 0,  /* Ignores block */
    LRG_EFFECT_FLAG_PIERCING      = 1 << 1,  /* Partially ignores block */
    LRG_EFFECT_FLAG_TRUE_DAMAGE   = 1 << 2,  /* Ignores all modifiers */
    LRG_EFFECT_FLAG_HP_LOSS       = 1 << 3,  /* Not affected by vulnerable */
    LRG_EFFECT_FLAG_LIFESTEAL     = 1 << 4,  /* Heals for damage dealt */
    LRG_EFFECT_FLAG_AOE           = 1 << 5,  /* Hits all enemies */
    LRG_EFFECT_FLAG_DELAYED       = 1 << 6   /* Executes at turn end */
} LrgEffectFlags;

/* Set flags */
lrg_card_effect_set_flags (effect, LRG_EFFECT_FLAG_UNBLOCKABLE |
                                    LRG_EFFECT_FLAG_LIFESTEAL);
```

## LrgCardEffectExecutor

The executor interface defines how effects are executed. Implement this interface for custom effect types.

### Interface

```c
struct _LrgCardEffectExecutorInterface
{
    GTypeInterface g_iface;

    /* Get the effect type this executor handles */
    const gchar * (*get_effect_type) (LrgCardEffectExecutor *self);

    /* Execute the effect */
    gboolean (*execute) (LrgCardEffectExecutor *self,
                         LrgCardEffect         *effect,
                         LrgCombatContext      *ctx,
                         LrgCombatant          *source,
                         LrgCombatant          *target);

    /* Validate effect parameters */
    gboolean (*validate) (LrgCardEffectExecutor *self,
                          LrgCardEffect         *effect,
                          GError               **error);
};
```

### Built-in Effects

| Effect | Parameters | Description |
|--------|------------|-------------|
| `damage` | amount, target, flags | Deal damage to target |
| `block` | amount | Gain block |
| `draw` | count | Draw cards |
| `energy` | amount | Gain/lose energy |
| `apply-status` | status_id, stacks, duration, target | Apply status effect |
| `add-card` | card_id, destination | Add card to hand/draw/discard |
| `heal` | amount, target | Heal HP |
| `discard` | count, random | Discard cards |
| `exhaust` | count, random | Exhaust cards |
| `scry` | count | Look at top cards of draw pile |
| `copy` | card, destination | Copy a card |
| `transform` | card, target_card_id | Transform card into another |
| `seek` | card_id, destination | Find specific card |
| `upgrade` | card, tier | Upgrade a card |

### Creating Custom Executors

```c
/* Define custom executor */
G_DECLARE_FINAL_TYPE (MyEffectExecutor, my_effect_executor,
                       MY, EFFECT_EXECUTOR, GObject)

static const gchar *
my_effect_executor_get_effect_type (LrgCardEffectExecutor *exec)
{
    return "my-custom-effect";
}

static gboolean
my_effect_executor_execute (LrgCardEffectExecutor *exec,
                            LrgCardEffect         *effect,
                            LrgCombatContext      *ctx,
                            LrgCombatant          *source,
                            LrgCombatant          *target)
{
    gint amount = lrg_card_effect_get_param_int (effect, "amount");
    /* Custom effect logic */
    return TRUE;
}

static void
my_effect_executor_iface_init (LrgCardEffectExecutorInterface *iface)
{
    iface->get_effect_type = my_effect_executor_get_effect_type;
    iface->execute = my_effect_executor_execute;
}
```

## LrgCardEffectRegistry

The registry manages effect executors. It's a singleton that maps effect types to their executors.

```c
/* Get registry */
LrgCardEffectRegistry *registry = lrg_card_effect_registry_get_default ();

/* Register custom executor */
g_autoptr(MyEffectExecutor) exec = my_effect_executor_new ();
lrg_card_effect_registry_register (registry, LRG_CARD_EFFECT_EXECUTOR (exec));

/* Execute an effect */
LrgCardEffect *effect = lrg_card_effect_new ("damage");
lrg_card_effect_set_param_int (effect, "amount", 10);
lrg_card_effect_registry_execute (registry, effect, ctx, source, target);
```

## LrgEffectStack

The effect stack manages effect resolution order. Effects are pushed onto the stack and resolved in order, allowing for interrupts and proper sequencing.

### Stack Operations

```c
/* Get stack from combat context */
LrgEffectStack *stack = lrg_combat_context_get_effect_stack (ctx);

/* Push effect onto stack */
LrgEffectStackEntry *entry = lrg_effect_stack_entry_new (effect, source, target);
lrg_effect_stack_push (stack, entry);

/* Resolve top effect */
gboolean resolved = lrg_effect_stack_resolve_top (stack, ctx);

/* Resolve all effects */
gboolean all_resolved = lrg_effect_stack_resolve_all (stack, ctx);

/* Insert interrupt (resolves before current) */
lrg_effect_stack_insert_interrupt (stack, interrupt_entry);
```

### Effect Priority

```c
typedef enum {
    LRG_EFFECT_PRIORITY_FIRST,   /* Resolves first */
    LRG_EFFECT_PRIORITY_HIGH,
    LRG_EFFECT_PRIORITY_NORMAL,
    LRG_EFFECT_PRIORITY_LOW,
    LRG_EFFECT_PRIORITY_LAST     /* Resolves last */
} LrgEffectPriority;

/* Set priority on stack entry */
lrg_effect_stack_entry_set_priority (entry, LRG_EFFECT_PRIORITY_HIGH);
```

## LrgEventBus

The event bus provides a pub/sub system for card events. Cards, relics, and status effects can subscribe to events and react to them.

### Event Types

```c
typedef enum {
    /* Combat lifecycle */
    LRG_CARD_EVENT_COMBAT_START,
    LRG_CARD_EVENT_COMBAT_END,
    LRG_CARD_EVENT_TURN_START,
    LRG_CARD_EVENT_TURN_END,

    /* Card actions */
    LRG_CARD_EVENT_CARD_DRAWN,
    LRG_CARD_EVENT_CARD_PLAYED,
    LRG_CARD_EVENT_CARD_DISCARDED,
    LRG_CARD_EVENT_CARD_EXHAUSTED,
    LRG_CARD_EVENT_CARD_RETAINED,
    LRG_CARD_EVENT_CARD_TRANSFORMED,
    LRG_CARD_EVENT_CARD_UPGRADED,

    /* Combat events */
    LRG_CARD_EVENT_DAMAGE_DEALT,
    LRG_CARD_EVENT_DAMAGE_TAKEN,
    LRG_CARD_EVENT_BLOCK_GAINED,
    LRG_CARD_EVENT_BLOCK_BROKEN,
    LRG_CARD_EVENT_HEAL,
    LRG_CARD_EVENT_OVERKILL,

    /* Status events */
    LRG_CARD_EVENT_STATUS_APPLIED,
    LRG_CARD_EVENT_STATUS_REMOVED,
    LRG_CARD_EVENT_STATUS_EXPIRED,

    /* Enemy events */
    LRG_CARD_EVENT_ENEMY_DIED,
    LRG_CARD_EVENT_ENEMY_SPAWNED,
    LRG_CARD_EVENT_INTENT_REVEALED,

    /* Resource events */
    LRG_CARD_EVENT_ENERGY_GAINED,
    LRG_CARD_EVENT_ENERGY_SPENT,
    LRG_CARD_EVENT_GOLD_GAINED,
    LRG_CARD_EVENT_GOLD_SPENT,

    /* Deck events */
    LRG_CARD_EVENT_SHUFFLE,
    LRG_CARD_EVENT_DECK_EMPTY,

    /* Special */
    LRG_CARD_EVENT_COMBO_TRIGGERED,
    LRG_CARD_EVENT_SYNERGY_ACTIVATED
} LrgCardEventType;
```

### Publishing Events

```c
/* Get event bus */
LrgEventBus *bus = lrg_event_bus_get_default ();

/* Create event data */
LrgCardEventData *data = lrg_card_event_data_new (LRG_CARD_EVENT_DAMAGE_DEALT);
lrg_card_event_data_set_source (data, player);
lrg_card_event_data_set_target (data, enemy);
lrg_card_event_data_set_amount (data, damage);

/* Publish event */
lrg_event_bus_publish (bus, data);
```

### Subscribing to Events

```c
/* Subscribe using LrgTriggerListener interface */
struct _LrgTriggerListenerInterface
{
    GTypeInterface g_iface;

    /* Which events to listen for */
    LrgCardEventType * (*get_subscribed_events) (LrgTriggerListener *self,
                                                  gsize              *n_events);

    /* Priority for ordering */
    gint (*get_priority) (LrgTriggerListener *self);

    /* Called before event is processed */
    gboolean (*on_event_pre) (LrgTriggerListener *self,
                              LrgCardEventData   *event);

    /* Called after event is processed */
    void (*on_event_post) (LrgTriggerListener *self,
                           LrgCardEventData   *event);
};

/* Register listener */
lrg_event_bus_add_listener (bus, LRG_TRIGGER_LISTENER (my_listener));
```

## Effect Execution Flow

1. Card is played
2. Card's effects are queued on the effect stack
3. Trigger listeners are notified (pre-event)
4. Top effect is resolved
5. Status effects/relics may add interrupt effects
6. Continue until stack is empty
7. Trigger listeners are notified (post-event)

```c
/* Example: Playing a card */
static void
play_card (LrgCombatManager *manager,
           LrgCardInstance  *card,
           LrgCombatant     *target)
{
    LrgCombatContext *ctx = manager->context;
    LrgEffectStack *stack = lrg_combat_context_get_effect_stack (ctx);
    LrgEventBus *bus = lrg_event_bus_get_default ();

    /* Queue card effects */
    LrgCardDef *def = lrg_card_instance_get_definition (card);
    GPtrArray *effects = lrg_card_def_get_effects (def);

    for (guint i = 0; i < effects->len; i++)
    {
        LrgCardEffect *effect = g_ptr_array_index (effects, i);
        LrgEffectStackEntry *entry = lrg_effect_stack_entry_new (
            effect, player, target);
        lrg_effect_stack_push (stack, entry);
    }

    /* Publish card played event */
    LrgCardEventData *event = lrg_card_event_data_new (LRG_CARD_EVENT_CARD_PLAYED);
    lrg_card_event_data_set_card (event, card);
    lrg_event_bus_publish (bus, event);

    /* Resolve all effects */
    lrg_effect_stack_resolve_all (stack, ctx);
}
```

## See Also

- [Cards Documentation](cards.md)
- [Status Effects Documentation](status-effects.md)
- [Combat Documentation](combat.md)
