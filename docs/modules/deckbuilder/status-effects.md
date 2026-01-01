# Status Effects

Status effects are buffs and debuffs applied to combatants. They have stacking behavior, duration, and lifecycle hooks for complex interactions.

## LrgStatusEffectDef

Status effect definitions are derivable GObjects that define the blueprint for status types.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique status identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Status description |
| `icon` | `gchar*` | Icon path |
| `is-buff` | `gboolean` | TRUE for buffs, FALSE for debuffs |
| `is-permanent` | `gboolean` | Does not decay over time |
| `stack-mode` | `LrgStatusStackMode` | How stacks combine |
| `max-stacks` | `gint` | Maximum stack count (-1 for unlimited) |

### Stack Modes

```c
typedef enum {
    LRG_STATUS_STACK_INTENSITY,  /* Stacks increase effect power */
    LRG_STATUS_STACK_DURATION,   /* Stacks increase duration */
    LRG_STATUS_STACK_REPLACE,    /* New application replaces old */
    LRG_STATUS_STACK_NONE        /* Cannot stack */
} LrgStatusStackMode;
```

### Virtual Methods

```c
struct _LrgStatusEffectDefClass
{
    GObjectClass parent_class;

    /* Called when status is applied */
    void (*on_applied)        (LrgStatusEffectDef *self,
                               LrgCombatant       *target,
                               gint                stacks);

    /* Called when status is removed */
    void (*on_removed)        (LrgStatusEffectDef *self,
                               LrgCombatant       *target);

    /* Called at turn start */
    void (*on_turn_start)     (LrgStatusEffectDef *self,
                               LrgCombatant       *target,
                               gint                stacks);

    /* Called at turn end */
    void (*on_turn_end)       (LrgStatusEffectDef *self,
                               LrgCombatant       *target,
                               gint                stacks);

    /* Modify outgoing damage */
    gint (*on_damage_dealt)   (LrgStatusEffectDef *self,
                               LrgCombatant       *source,
                               LrgCombatant       *target,
                               gint                damage,
                               gint                stacks);

    /* Modify incoming damage */
    gint (*on_damage_received)(LrgStatusEffectDef *self,
                               LrgCombatant       *target,
                               LrgCombatant       *source,
                               gint                damage,
                               gint                stacks);

    /* Modify block gained */
    gint (*on_block_gained)   (LrgStatusEffectDef *self,
                               LrgCombatant       *target,
                               gint                block,
                               gint                stacks);

    gpointer _reserved[8];
};
```

## Built-in Status Effects

### Strength

```c
/* Increases damage dealt */
static gint
strength_on_damage_dealt (LrgStatusEffectDef *self,
                          LrgCombatant       *source,
                          LrgCombatant       *target,
                          gint                damage,
                          gint                stacks)
{
    return damage + stacks;  /* +1 damage per stack */
}
```

### Dexterity

```c
/* Increases block gained */
static gint
dexterity_on_block_gained (LrgStatusEffectDef *self,
                           LrgCombatant       *target,
                           gint                block,
                           gint                stacks)
{
    return block + stacks;  /* +1 block per stack */
}
```

### Vulnerable

```c
/* Take 50% more damage */
static gint
vulnerable_on_damage_received (LrgStatusEffectDef *self,
                               LrgCombatant       *target,
                               LrgCombatant       *source,
                               gint                damage,
                               gint                stacks)
{
    return (gint)(damage * 1.5f);
}

static void
vulnerable_on_turn_end (LrgStatusEffectDef *self,
                        LrgCombatant       *target,
                        gint                stacks)
{
    /* Reduce stacks by 1 */
    lrg_combatant_remove_status (target, "vulnerable", 1);
}
```

### Weak

```c
/* Deal 25% less damage */
static gint
weak_on_damage_dealt (LrgStatusEffectDef *self,
                      LrgCombatant       *source,
                      LrgCombatant       *target,
                      gint                damage,
                      gint                stacks)
{
    return (gint)(damage * 0.75f);
}
```

### Frail

```c
/* Gain 25% less block */
static gint
frail_on_block_gained (LrgStatusEffectDef *self,
                       LrgCombatant       *target,
                       gint                block,
                       gint                stacks)
{
    return (gint)(block * 0.75f);
}
```

### Poison

```c
/* Take X damage at turn end, reduce by 1 */
static void
poison_on_turn_end (LrgStatusEffectDef *self,
                    LrgCombatant       *target,
                    gint                stacks)
{
    /* Deal poison damage (HP loss, not affected by block) */
    lrg_combatant_lose_hp (target, stacks);

    /* Reduce stacks by 1 */
    lrg_combatant_remove_status (target, "poison", 1);
}
```

### Thorns

```c
/* Deal X damage when attacked */
static gint
thorns_on_damage_received (LrgStatusEffectDef *self,
                           LrgCombatant       *target,
                           LrgCombatant       *source,
                           gint                damage,
                           gint                stacks)
{
    if (damage > 0 && source != NULL)
    {
        /* Reflect damage back to attacker */
        lrg_combatant_take_damage (source, stacks, LRG_EFFECT_FLAG_HP_LOSS);
    }
    return damage;
}
```

### Intangible

```c
/* Reduce all damage to 1 */
static gint
intangible_on_damage_received (LrgStatusEffectDef *self,
                               LrgCombatant       *target,
                               LrgCombatant       *source,
                               gint                damage,
                               gint                stacks)
{
    if (damage > 1)
        return 1;
    return damage;
}

static void
intangible_on_turn_end (LrgStatusEffectDef *self,
                        LrgCombatant       *target,
                        gint                stacks)
{
    /* Reduce by 1 at turn end */
    lrg_combatant_remove_status (target, "intangible", 1);
}
```

### Artifact

```c
/* Block next N debuff applications */
static void
artifact_on_status_applied (LrgStatusEffectDef *self,
                            LrgCombatant       *target,
                            const gchar        *status_id,
                            gint                stacks)
{
    LrgStatusEffectDef *incoming = lrg_status_effect_registry_get (
        lrg_status_effect_registry_get_default (), status_id);

    if (!lrg_status_effect_def_is_buff (incoming))
    {
        /* Block the debuff */
        lrg_combatant_remove_status (target, status_id, stacks);
        /* Consume one artifact stack */
        lrg_combatant_remove_status (target, "artifact", 1);
    }
}
```

## LrgStatusEffectInstance

Status instances track the current state of an applied status.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `definition` | `LrgStatusEffectDef*` | The status definition |
| `stacks` | `gint` | Current stack count |
| `duration` | `gint` | Remaining turns (-1 for permanent) |

### Operations

```c
/* Create instance */
LrgStatusEffectInstance *instance = lrg_status_effect_instance_new (def, 3);

/* Modify stacks */
lrg_status_effect_instance_add_stacks (instance, 2);
lrg_status_effect_instance_remove_stacks (instance, 1);

/* Check if expired */
if (lrg_status_effect_instance_get_stacks (instance) <= 0)
{
    /* Remove status */
}
```

## LrgStatusEffectRegistry

The registry manages status effect definitions:

```c
/* Get registry */
LrgStatusEffectRegistry *registry = lrg_status_effect_registry_get_default ();

/* Register status */
lrg_status_effect_registry_register (registry, status_def);

/* Look up status */
LrgStatusEffectDef *def = lrg_status_effect_registry_get (registry, "poison");

/* Get all statuses */
GList *all_statuses = lrg_status_effect_registry_get_all (registry);
```

## Creating Custom Status Effects

```c
/* Define a "Burning" status */
G_DECLARE_FINAL_TYPE (BurningStatus, burning_status, MY, BURNING_STATUS,
                       LrgStatusEffectDef)

static void
burning_on_turn_start (LrgStatusEffectDef *self,
                       LrgCombatant       *target,
                       gint                stacks)
{
    /* Deal fire damage at turn start */
    lrg_combatant_take_damage (target, stacks * 2, LRG_EFFECT_FLAG_HP_LOSS);
}

static void
burning_on_turn_end (LrgStatusEffectDef *self,
                     LrgCombatant       *target,
                     gint                stacks)
{
    /* Reduce stacks by 1 */
    lrg_combatant_remove_status (target, "burning", 1);
}

static void
burning_status_class_init (BurningStatusClass *klass)
{
    LrgStatusEffectDefClass *status_class = LRG_STATUS_EFFECT_DEF_CLASS (klass);
    status_class->on_turn_start = burning_on_turn_start;
    status_class->on_turn_end = burning_on_turn_end;
}

/* Create and register */
g_autoptr(BurningStatus) burning = burning_status_new ();
lrg_status_effect_def_set_id (LRG_STATUS_EFFECT_DEF (burning), "burning");
lrg_status_effect_def_set_name (LRG_STATUS_EFFECT_DEF (burning), "Burning");
lrg_status_effect_def_set_is_buff (LRG_STATUS_EFFECT_DEF (burning), FALSE);
lrg_status_effect_def_set_stack_mode (LRG_STATUS_EFFECT_DEF (burning),
                                       LRG_STATUS_STACK_INTENSITY);
lrg_status_effect_registry_register (registry, LRG_STATUS_EFFECT_DEF (burning));
```

## Status Application

```c
/* Apply status to combatant */
lrg_combatant_apply_status (target, "poison", 5);

/* Remove status */
lrg_combatant_remove_status (target, "poison", 2);  /* Remove 2 stacks */
lrg_combatant_clear_status (target, "poison");       /* Remove all */

/* Check status */
gint stacks = lrg_combatant_get_status_stacks (target, "poison");
gboolean has_poison = lrg_combatant_has_status (target, "poison");

/* Get all statuses */
GPtrArray *buffs = lrg_combatant_get_buffs (target);
GPtrArray *debuffs = lrg_combatant_get_debuffs (target);
```

## See Also

- [Combat Documentation](combat.md)
- [Effects Documentation](effects.md)
- [Relics Documentation](relics.md)
