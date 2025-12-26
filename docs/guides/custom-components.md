# How to Create Custom ECS Components

This guide explains how to create custom components for the Entity Component System (ECS) in Libregnum. Components are derivable classes that attach behavior and data to game entities.

## Component Basics

A component:
- Derives from `LrgComponent`
- Stores game-specific data
- Implements lifecycle hooks (attach/detach/update)
- Can interact with other components via the entity

## Creating a Simple Component

### Step 1: Define the Component Class

Create a header file for your component:

```c
/* my-damage-component.h */
#pragma once

#include <libregnum.h>

G_BEGIN_DECLS

#define MY_TYPE_DAMAGE_COMPONENT (my_damage_component_get_type())

G_DECLARE_FINAL_TYPE(MyDamageComponent,
                     my_damage_component,
                     MY,
                     DAMAGE_COMPONENT,
                     LrgComponent)

/* Construction */
MyDamageComponent *
my_damage_component_new(guint base_damage);

/* Properties */
guint
my_damage_component_get_base_damage(MyDamageComponent *self);

void
my_damage_component_set_base_damage(MyDamageComponent *self,
                                    guint              base_damage);

/* Custom methods */
guint
my_damage_component_calculate_damage(MyDamageComponent *self);

G_END_DECLS
```

### Step 2: Implement the Component

Create the implementation file:

```c
/* my-damage-component.c */
#include "my-damage-component.h"

struct _MyDamageComponent
{
    LrgComponent parent_instance;
    guint        base_damage;
};

typedef struct
{
    /* Private data if needed */
} MyDamageComponentPrivate;

G_DEFINE_TYPE(MyDamageComponent,
              my_damage_component,
              LRG_TYPE_COMPONENT)

/* Lifecycle hooks */

static void
my_damage_component_attach(LrgComponent *component,
                           LrgEntity    *entity)
{
    MyDamageComponent *self = MY_DAMAGE_COMPONENT(component);

    /* Called when component is added to entity */
    g_debug("Damage component attached to entity");

    /* Call parent implementation */
    LRG_COMPONENT_CLASS(my_damage_component_parent_class)->attach(component,
                                                                  entity);
}

static void
my_damage_component_detach(LrgComponent *component,
                           LrgEntity    *entity)
{
    MyDamageComponent *self = MY_DAMAGE_COMPONENT(component);

    /* Called when component is removed from entity */
    g_debug("Damage component detached from entity");

    /* Call parent implementation */
    LRG_COMPONENT_CLASS(my_damage_component_parent_class)->detach(component,
                                                                 entity);
}

static void
my_damage_component_update(LrgComponent *component,
                           gfloat        delta_time)
{
    MyDamageComponent *self = MY_DAMAGE_COMPONENT(component);

    /* Called each frame - implement behavior here */
    /* delta_time is frame delta in seconds */

    /* Call parent implementation first */
    LRG_COMPONENT_CLASS(my_damage_component_parent_class)->update(component,
                                                                 delta_time);
}

/* Class initialization */

static void
my_damage_component_class_init(MyDamageComponentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    LrgComponentClass *component_class = LRG_COMPONENT_CLASS(klass);

    /* Register virtual functions */
    component_class->attach = my_damage_component_attach;
    component_class->detach = my_damage_component_detach;
    component_class->update = my_damage_component_update;
}

/* Instance initialization */

static void
my_damage_component_init(MyDamageComponent *self)
{
    self->base_damage = 10;
}

/* Public API */

MyDamageComponent *
my_damage_component_new(guint base_damage)
{
    MyDamageComponent *self = g_object_new(MY_TYPE_DAMAGE_COMPONENT, NULL);
    self->base_damage = base_damage;
    return self;
}

guint
my_damage_component_get_base_damage(MyDamageComponent *self)
{
    g_return_val_if_fail(MY_IS_DAMAGE_COMPONENT(self), 0);
    return self->base_damage;
}

void
my_damage_component_set_base_damage(MyDamageComponent *self,
                                    guint              base_damage)
{
    g_return_if_fail(MY_IS_DAMAGE_COMPONENT(self));
    self->base_damage = base_damage;
    g_object_notify(G_OBJECT(self), "base-damage");
}

guint
my_damage_component_calculate_damage(MyDamageComponent *self)
{
    g_return_val_if_fail(MY_IS_DAMAGE_COMPONENT(self), 0);

    guint damage = self->base_damage;

    /* Apply bonuses, random variation, etc. */
    guint variance = g_random_int_range(0, self->base_damage / 10 + 1);
    damage += variance;

    return damage;
}
```

## Component with State Machine

For components with multiple states:

```c
/* attack-component.h */
#pragma once

#include <libregnum.h>

G_BEGIN_DECLS

#define MY_TYPE_ATTACK_COMPONENT (my_attack_component_get_type())

typedef enum
{
    MY_ATTACK_STATE_IDLE,
    MY_ATTACK_STATE_CHARGING,
    MY_ATTACK_STATE_ATTACKING,
    MY_ATTACK_STATE_COOLDOWN
} MyAttackState;

G_DECLARE_FINAL_TYPE(MyAttackComponent,
                     my_attack_component,
                     MY,
                     ATTACK_COMPONENT,
                     LrgComponent)

MyAttackComponent *
my_attack_component_new(void);

MyAttackState
my_attack_component_get_state(MyAttackComponent *self);

G_END_DECLS
```

```c
/* attack-component.c */
#include "attack-component.h"

struct _MyAttackComponent
{
    LrgComponent  parent_instance;
    MyAttackState state;
    gfloat        state_timer;
    gfloat        attack_cooldown;
};

G_DEFINE_TYPE(MyAttackComponent,
              my_attack_component,
              LRG_TYPE_COMPONENT)

/* State transition logic */

static void
my_attack_component_transition_state(MyAttackComponent *self,
                                     MyAttackState     new_state)
{
    g_debug("Attack state transition: %d → %d", self->state, new_state);

    self->state = new_state;
    self->state_timer = 0.0f;

    switch (self->state) {
    case MY_ATTACK_STATE_IDLE:
        /* Reset attack */
        break;

    case MY_ATTACK_STATE_CHARGING:
        /* Start charge animation */
        break;

    case MY_ATTACK_STATE_ATTACKING:
        /* Execute attack */
        break;

    case MY_ATTACK_STATE_COOLDOWN:
        /* Start cooldown timer */
        self->state_timer = self->attack_cooldown;
        break;
    }
}

static void
my_attack_component_update(LrgComponent *component,
                           gfloat        delta_time)
{
    MyAttackComponent *self = MY_ATTACK_COMPONENT(component);

    /* Update state timers */
    self->state_timer += delta_time;

    switch (self->state) {
    case MY_ATTACK_STATE_IDLE:
        /* Can start new attack */
        break;

    case MY_ATTACK_STATE_CHARGING:
        if (self->state_timer > 0.5f) {
            /* Charge complete */
            my_attack_component_transition_state(self, MY_ATTACK_STATE_ATTACKING);
        }
        break;

    case MY_ATTACK_STATE_ATTACKING:
        if (self->state_timer > 0.2f) {
            /* Attack finished */
            my_attack_component_transition_state(self, MY_ATTACK_STATE_COOLDOWN);
        }
        break;

    case MY_ATTACK_STATE_COOLDOWN:
        if (self->state_timer >= self->attack_cooldown) {
            /* Cooldown finished */
            my_attack_component_transition_state(self, MY_ATTACK_STATE_IDLE);
        }
        break;
    }

    LRG_COMPONENT_CLASS(my_attack_component_parent_class)->update(component,
                                                                 delta_time);
}

static void
my_attack_component_class_init(MyAttackComponentClass *klass)
{
    LrgComponentClass *component_class = LRG_COMPONENT_CLASS(klass);
    component_class->update = my_attack_component_update;
}

static void
my_attack_component_init(MyAttackComponent *self)
{
    self->state = MY_ATTACK_STATE_IDLE;
    self->state_timer = 0.0f;
    self->attack_cooldown = 1.0f;
}

MyAttackComponent *
my_attack_component_new(void)
{
    return g_object_new(MY_TYPE_ATTACK_COMPONENT, NULL);
}

MyAttackState
my_attack_component_get_state(MyAttackComponent *self)
{
    g_return_val_if_fail(MY_IS_ATTACK_COMPONENT(self), MY_ATTACK_STATE_IDLE);
    return self->state;
}
```

## Component Interaction

Components can interact via the entity:

```c
/* health-component.h */
#pragma once
#include <libregnum.h>

#define MY_TYPE_HEALTH_COMPONENT (my_health_component_get_type())

G_DECLARE_FINAL_TYPE(MyHealthComponent,
                     my_health_component,
                     MY,
                     HEALTH_COMPONENT,
                     LrgComponent)

MyHealthComponent *
my_health_component_new(guint max_health);

guint
my_health_component_get_current_health(MyHealthComponent *self);

guint
my_health_component_get_max_health(MyHealthComponent *self);

void
my_health_component_take_damage(MyHealthComponent *self,
                                guint              damage);

gboolean
my_health_component_is_alive(MyHealthComponent *self);

G_END_DECLS
```

```c
/* health-component.c */
#include "health-component.h"

struct _MyHealthComponent
{
    LrgComponent parent_instance;
    guint        current_health;
    guint        max_health;
};

G_DEFINE_TYPE(MyHealthComponent,
              my_health_component,
              LRG_TYPE_COMPONENT)

static void
my_health_component_class_init(MyHealthComponentClass *klass)
{
    /* No virtual function overrides needed */
}

static void
my_health_component_init(MyHealthComponent *self)
{
    self->current_health = 100;
    self->max_health = 100;
}

MyHealthComponent *
my_health_component_new(guint max_health)
{
    MyHealthComponent *self = g_object_new(MY_TYPE_HEALTH_COMPONENT, NULL);
    self->max_health = max_health;
    self->current_health = max_health;
    return self;
}

guint
my_health_component_get_current_health(MyHealthComponent *self)
{
    g_return_val_if_fail(MY_IS_HEALTH_COMPONENT(self), 0);
    return self->current_health;
}

guint
my_health_component_get_max_health(MyHealthComponent *self)
{
    g_return_val_if_fail(MY_IS_HEALTH_COMPONENT(self), 0);
    return self->max_health;
}

void
my_health_component_take_damage(MyHealthComponent *self,
                                guint              damage)
{
    g_return_if_fail(MY_IS_HEALTH_COMPONENT(self));

    if (self->current_health > damage) {
        self->current_health -= damage;
    } else {
        self->current_health = 0;
    }

    g_signal_emit_by_name(self, "health-changed", self->current_health);
}

gboolean
my_health_component_is_alive(MyHealthComponent *self)
{
    g_return_val_if_fail(MY_IS_HEALTH_COMPONENT(self), FALSE);
    return self->current_health > 0;
}
```

## Usage in Game

```c
/* Create entity */
g_autoptr(LrgEntity) player = lrg_entity_new();

/* Attach components */
MyHealthComponent *health = my_health_component_new(100);
lrg_entity_attach_component(player, LRG_COMPONENT(health));
g_object_unref(health);

MyDamageComponent *damage = my_damage_component_new(15);
lrg_entity_attach_component(player, LRG_COMPONENT(damage));
g_object_unref(damage);

/* Get component from entity */
MyHealthComponent *player_health = (MyHealthComponent *)
    lrg_entity_get_component(player, MY_TYPE_HEALTH_COMPONENT);

if (player_health != NULL) {
    /* Take damage */
    guint dmg = my_damage_component_calculate_damage(damage);
    my_health_component_take_damage(player_health, dmg);

    /* Check status */
    if (!my_health_component_is_alive(player_health)) {
        g_debug("Player defeated");
    }
}
```

## Component Communication

Components can emit signals for inter-component communication:

```c
/* Register a signal on component creation */

static void
my_health_component_class_init(MyHealthComponentClass *klass)
{
    g_signal_new("health-changed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__UINT,
                 G_TYPE_NONE,
                 1, G_TYPE_UINT);
}

/* Listen to signals from other components */

static void
on_health_changed(MyHealthComponent *health,
                  guint              current_hp,
                  gpointer           user_data)
{
    g_message("Health changed to: %u", current_hp);
}

/* In entity setup */
MyHealthComponent *health = my_health_component_new(100);
g_signal_connect(health, "health-changed",
                G_CALLBACK(on_health_changed), NULL);
```

## Best Practices

1. **Keep components focused**: A component should have a single responsibility
2. **Use properties for data**: Expose configurable data as properties
3. **Document lifecycle hooks**: Clearly explain when attach/detach/update are called
4. **Handle NULL entities**: Components may be used without being attached
5. **Use g_autoptr**: Let GObject handle cleanup
6. **Emit signals for events**: Allow other systems to react to component changes
7. **Validate inputs**: Use g_return_if_fail() and g_return_val_if_fail()
8. **Write tests**: Create unit tests for component behavior

## See Also

- [ECS Module Documentation](../modules/ecs/index.md)
- [Implementing Saveable for Persistence](implementing-saveable.md)
