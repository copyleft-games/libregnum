# Event Bus System

The Event Bus provides a priority-based publish/subscribe system for decoupled event handling. It uses generic interfaces allowing any GObject to participate as an event or listener.

## Overview

The event system consists of three parts:

- **LrgEvent** - Interface for objects that can be dispatched
- **LrgEventListener** - Interface for objects that receive events
- **LrgEventBus** - Central dispatcher that routes events to listeners

## Architecture

Events flow through the bus to registered listeners in priority order:

```
Event Source                    Event Bus                      Listeners
     │                              │                              │
     │  emit(event) ───────────────►│                              │
     │                              │  sort by priority            │
     │                              │                              │
     │                              │  on_event() ────────────────►│ Priority 100
     │                              │                              │
     │                              │  on_event() ────────────────►│ Priority 50
     │                              │                              │
     │                              │  on_event() ────────────────►│ Priority 0
     │                              │                              │
     │◄─────────────────────────────│  result                      │
```

## The LrgEvent Interface

Any GObject can be an event by implementing `LrgEvent`:

### Interface Methods

| Method | Description |
|--------|-------------|
| `get_type_mask()` | Returns bitmask identifying event type(s) |
| `is_cancelled()` | Check if event was cancelled by a listener |
| `cancel()` | Mark event as cancelled (stops propagation) |

### Implementing LrgEvent

```c
#include <libregnum.h>

/* Event type bits - define your own event taxonomy */
#define MY_EVENT_DAMAGE    (1ULL << 0)
#define MY_EVENT_HEAL      (1ULL << 1)
#define MY_EVENT_DEATH     (1ULL << 2)

/* Your event object */
G_DECLARE_FINAL_TYPE (MyDamageEvent, my_damage_event, MY, DAMAGE_EVENT, GObject)

struct _MyDamageEvent
{
    GObject   parent_instance;
    gint      amount;
    gboolean  cancelled;
};

/* Implement interface methods */
static guint64
my_damage_event_get_type_mask (LrgEvent *event)
{
    return MY_EVENT_DAMAGE;
}

static gboolean
my_damage_event_is_cancelled (LrgEvent *event)
{
    return MY_DAMAGE_EVENT (event)->cancelled;
}

static void
my_damage_event_cancel (LrgEvent *event)
{
    MY_DAMAGE_EVENT (event)->cancelled = TRUE;
}

/* Interface init function */
static void
my_damage_event_event_init (LrgEventInterface *iface)
{
    iface->get_type_mask = my_damage_event_get_type_mask;
    iface->is_cancelled = my_damage_event_is_cancelled;
    iface->cancel = my_damage_event_cancel;
}

/* Type definition with interface */
G_DEFINE_TYPE_WITH_CODE (MyDamageEvent, my_damage_event, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_EVENT, my_damage_event_event_init))
```

## The LrgEventListener Interface

Objects that respond to events implement `LrgEventListener`:

### Interface Methods

| Method | Description |
|--------|-------------|
| `get_id()` | Unique string identifier for this listener |
| `get_priority()` | Higher priority = called first (default: 0) |
| `get_event_mask()` | Bitmask of event types this listener handles |
| `on_event()` | Called when matching event is dispatched |

### Implementing LrgEventListener

```c
#include <libregnum.h>

G_DECLARE_FINAL_TYPE (ArmorListener, armor_listener, GAME, ARMOR_LISTENER, GObject)

struct _ArmorListener
{
    GObject  parent_instance;
    gint     armor_value;
};

static const gchar *
armor_listener_get_id (LrgEventListener *listener)
{
    return "armor-damage-reducer";
}

static gint
armor_listener_get_priority (LrgEventListener *listener)
{
    /* High priority - reduce damage before other listeners see it */
    return 100;
}

static guint64
armor_listener_get_event_mask (LrgEventListener *listener)
{
    return MY_EVENT_DAMAGE;  /* Only listen for damage events */
}

static gboolean
armor_listener_on_event (LrgEventListener *listener,
                         LrgEvent         *event,
                         gpointer          context)
{
    ArmorListener *self = GAME_ARMOR_LISTENER (listener);
    MyDamageEvent *damage = MY_DAMAGE_EVENT (event);

    /* Reduce damage by armor value */
    damage->amount -= self->armor_value;
    if (damage->amount < 0)
        damage->amount = 0;

    /* Don't cancel - let other listeners process reduced damage */
    return TRUE;  /* Event was handled */
}

static void
armor_listener_listener_init (LrgEventListenerInterface *iface)
{
    iface->get_id = armor_listener_get_id;
    iface->get_priority = armor_listener_get_priority;
    iface->get_event_mask = armor_listener_get_event_mask;
    iface->on_event = armor_listener_on_event;
}

G_DEFINE_TYPE_WITH_CODE (ArmorListener, armor_listener, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_EVENT_LISTENER, armor_listener_listener_init))
```

## The LrgEventBus

The event bus manages listener registration and event dispatch.

### Creating an Event Bus

```c
LrgEventBus *bus = lrg_event_bus_new ();
```

### Registering Listeners

```c
/* Create listener */
ArmorListener *armor = g_object_new (GAME_TYPE_ARMOR_LISTENER, NULL);
armor->armor_value = 10;

/* Register with bus */
lrg_event_bus_register (bus, LRG_EVENT_LISTENER (armor));

/* Unregister when done */
lrg_event_bus_unregister (bus, LRG_EVENT_LISTENER (armor));
```

### Emitting Events

```c
/* Create event */
g_autoptr(MyDamageEvent) event = g_object_new (MY_TYPE_DAMAGE_EVENT, NULL);
event->amount = 25;

/* Emit to all matching listeners */
gboolean handled = lrg_event_bus_emit (bus, LRG_EVENT (event), context);

/* Check final damage after all listeners processed */
g_print ("Final damage: %d\n", event->amount);

/* Check if any listener cancelled the event */
if (lrg_event_is_cancelled (LRG_EVENT (event)))
{
    g_print ("Event was cancelled\n");
}
```

### Emit with Copy

For events that shouldn't be modified:

```c
/* Emit a copy - original event remains unchanged */
g_autoptr(LrgEvent) result = lrg_event_bus_emit_copy (bus, LRG_EVENT (event), context);
```

## Priority System

Listeners are called in priority order (highest first):

| Priority | Typical Use |
|----------|-------------|
| 100+ | Pre-processing (damage reduction, shields) |
| 50 | Normal processing |
| 0 | Default priority |
| -50 | Post-processing (logging, UI updates) |
| -100 | Final handlers |

```c
static gint
shield_get_priority (LrgEventListener *listener)
{
    return 150;  /* Called before armor */
}

static gint
armor_get_priority (LrgEventListener *listener)
{
    return 100;  /* Called after shield */
}

static gint
damage_log_get_priority (LrgEventListener *listener)
{
    return -100;  /* Called last - logs final damage */
}
```

## Event Masks

Use bitmasks to filter which events a listener receives:

```c
/* Define event type bits */
#define EVENT_COMBAT    (1ULL << 0)
#define EVENT_DAMAGE    (1ULL << 1)
#define EVENT_HEAL      (1ULL << 2)
#define EVENT_MOVEMENT  (1ULL << 3)

/* Event can have multiple type bits */
static guint64
melee_damage_get_type_mask (LrgEvent *event)
{
    return EVENT_COMBAT | EVENT_DAMAGE;  /* Both combat and damage */
}

/* Listener can filter by multiple types */
static guint64
combat_log_get_event_mask (LrgEventListener *listener)
{
    return EVENT_COMBAT;  /* Receives all combat-related events */
}
```

## Cancelling Events

Listeners can cancel events to stop propagation:

```c
static gboolean
invincibility_on_event (LrgEventListener *listener,
                        LrgEvent         *event,
                        gpointer          context)
{
    Player *player = context;

    if (player->is_invincible)
    {
        lrg_event_cancel (event);  /* Stop event propagation */
        return TRUE;
    }

    return FALSE;  /* Let other listeners handle it */
}
```

## Signals

The event bus emits signals:

| Signal | When Emitted |
|--------|--------------|
| `listener-registered` | After a listener is added |
| `listener-unregistered` | After a listener is removed |
| `event-dispatched` | After an event completes dispatch |

```c
static void
on_event_dispatched (LrgEventBus *bus,
                     LrgEvent    *event,
                     gpointer     user_data)
{
    g_print ("Event dispatched: cancelled=%s\n",
             lrg_event_is_cancelled (event) ? "yes" : "no");
}

g_signal_connect (bus, "event-dispatched",
                  G_CALLBACK (on_event_dispatched), NULL);
```

## Deckbuilder Integration

The deckbuilder module uses this event system with `LrgCardEvent` and `LrgTriggerListener`:

```c
/* LrgCardEvent implements LrgEvent */
g_autoptr(LrgCardEvent) event = lrg_card_event_new (LRG_CARD_EVENT_TURN_START);
lrg_event_bus_emit (bus, LRG_EVENT (event), combat_context);

/* LrgTriggerListener extends LrgEventListener */
LrgTriggerListener *trigger = /* card trigger */;
lrg_event_bus_register (bus, LRG_EVENT_LISTENER (trigger));
```

## API Reference

### LrgEvent Interface

| Function | Description |
|----------|-------------|
| `lrg_event_get_type_mask(event)` | Get event type bitmask |
| `lrg_event_is_cancelled(event)` | Check if cancelled |
| `lrg_event_cancel(event)` | Cancel the event |

### LrgEventListener Interface

| Function | Description |
|----------|-------------|
| `lrg_event_listener_get_id(listener)` | Get unique ID |
| `lrg_event_listener_get_priority(listener)` | Get priority (higher = first) |
| `lrg_event_listener_get_event_mask(listener)` | Get event type filter |
| `lrg_event_listener_on_event(listener, event, context)` | Handle event |

### LrgEventBus

| Function | Description |
|----------|-------------|
| `lrg_event_bus_new()` | Create new event bus |
| `lrg_event_bus_register(bus, listener)` | Add listener |
| `lrg_event_bus_unregister(bus, listener)` | Remove listener |
| `lrg_event_bus_emit(bus, event, context)` | Dispatch event |
| `lrg_event_bus_emit_copy(bus, event, context)` | Dispatch event copy |
| `lrg_event_bus_get_listener_count(bus)` | Number of listeners |
| `lrg_event_bus_clear(bus)` | Remove all listeners |

## Complete Example

```c
#include <libregnum.h>

/* Event type definitions */
#define EVENT_DAMAGE (1ULL << 0)

/* Simple damage event */
typedef struct
{
    GObject   parent;
    gint      amount;
    gboolean  cancelled;
} DamageEvent;

/* Armor listener that reduces damage */
typedef struct
{
    GObject  parent;
    gint     armor;
} ArmorListener;

int
main (void)
{
    g_autoptr(LrgEventBus) bus = lrg_event_bus_new ();

    /* Create and register armor listener */
    ArmorListener *armor = g_object_new (ARMOR_TYPE_LISTENER, NULL);
    armor->armor = 5;
    lrg_event_bus_register (bus, LRG_EVENT_LISTENER (armor));

    /* Create damage event */
    g_autoptr(DamageEvent) event = g_object_new (DAMAGE_TYPE_EVENT, NULL);
    event->amount = 20;

    g_print ("Damage before: %d\n", event->amount);

    /* Emit - armor listener will reduce damage */
    lrg_event_bus_emit (bus, LRG_EVENT (event), NULL);

    g_print ("Damage after: %d\n", event->amount);  /* 15 */

    /* Cleanup */
    lrg_event_bus_unregister (bus, LRG_EVENT_LISTENER (armor));
    g_object_unref (armor);

    return 0;
}
```

## Related

- [Core Module Overview](index.md) - Core module documentation
- [Deckbuilder Module](../deckbuilder/index.md) - Card game implementation using events
