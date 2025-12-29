/* lrg-trigger-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manager for 2D triggers implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRIGGER2D
#include "../lrg-log.h"

#include "lrg-trigger-manager.h"
#include "lrg-trigger2d-private.h"

/**
 * EntityState:
 *
 * Internal state for tracking an entity.
 */
typedef struct
{
    gpointer     entity;
    gfloat       x;
    gfloat       y;
    guint32      collision_layer;
    GHashTable  *inside_triggers;  /* trigger -> TRUE if inside */
} EntityState;

static EntityState *
entity_state_new (gpointer entity,
                  guint32  collision_layer)
{
    EntityState *state;

    state = g_new0 (EntityState, 1);
    state->entity          = entity;
    state->x               = 0.0f;
    state->y               = 0.0f;
    state->collision_layer = collision_layer;
    state->inside_triggers = g_hash_table_new (g_direct_hash, g_direct_equal);

    return state;
}

static void
entity_state_free (EntityState *state)
{
    if (state != NULL)
    {
        g_hash_table_unref (state->inside_triggers);
        g_free (state);
    }
}

/**
 * LrgTriggerManager:
 *
 * Manager for 2D triggers.
 *
 * #LrgTriggerManager handles trigger registration, entity tracking,
 * and event dispatching. It provides enter/stay/exit event detection
 * for entities moving through trigger zones.
 *
 * Since: 1.0
 */
struct _LrgTriggerManager
{
    GObject parent_instance;

    /* Registered triggers */
    GPtrArray  *triggers;
    GHashTable *triggers_by_id;

    /* Tracked entities */
    GHashTable *entities;  /* entity pointer -> EntityState */

    /* Debug mode */
    gboolean debug_enabled;
};

G_DEFINE_FINAL_TYPE (LrgTriggerManager, lrg_trigger_manager, G_TYPE_OBJECT)

enum
{
    SIGNAL_TRIGGER_ENTERED,
    SIGNAL_TRIGGER_STAYED,
    SIGNAL_TRIGGER_EXITED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
    PROP_0,
    PROP_TRIGGER_COUNT,
    PROP_DEBUG_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Internal helpers */

static void
emit_trigger_event (LrgTriggerManager     *self,
                    LrgTrigger2D          *trigger,
                    EntityState           *entity_state,
                    LrgTrigger2DEventType  event_type)
{
    g_autoptr(LrgTriggerEvent) event = NULL;
    guint signal_id;

    /* Create event */
    event = lrg_trigger_event_new (event_type,
                                   entity_state->entity,
                                   entity_state->x,
                                   entity_state->y);

    /* Emit on trigger */
    g_signal_emit_by_name (trigger, "triggered", event_type, entity_state->entity);

    /* Mark fired for one-shot triggers */
    if (event_type == LRG_TRIGGER2D_EVENT_ENTER)
        lrg_trigger2d_mark_fired (trigger);

    /* Emit on manager */
    switch (event_type)
    {
    case LRG_TRIGGER2D_EVENT_ENTER:
        signal_id = signals[SIGNAL_TRIGGER_ENTERED];
        break;
    case LRG_TRIGGER2D_EVENT_STAY:
        signal_id = signals[SIGNAL_TRIGGER_STAYED];
        break;
    case LRG_TRIGGER2D_EVENT_EXIT:
        signal_id = signals[SIGNAL_TRIGGER_EXITED];
        break;
    default:
        return;
    }

    g_signal_emit (self, signal_id, 0, trigger, event);

    if (self->debug_enabled)
    {
        const gchar *type_str = "UNKNOWN";
        switch (event_type)
        {
        case LRG_TRIGGER2D_EVENT_ENTER: type_str = "ENTER"; break;
        case LRG_TRIGGER2D_EVENT_STAY:  type_str = "STAY";  break;
        case LRG_TRIGGER2D_EVENT_EXIT:  type_str = "EXIT";  break;
        }
        lrg_debug (LRG_LOG_DOMAIN_TRIGGER2D,
                   "Trigger event: %s on '%s' at (%.1f, %.1f)",
                   type_str,
                   lrg_trigger2d_get_id (trigger) ?: "(unnamed)",
                   entity_state->x, entity_state->y);
    }
}

static void
process_entity_for_trigger (LrgTriggerManager *self,
                            LrgTrigger2D      *trigger,
                            EntityState       *entity_state)
{
    gboolean was_inside;
    gboolean is_inside;
    gboolean can_fire;

    /* Check if trigger can interact with this entity's layer */
    if (!lrg_trigger2d_can_collide_with (trigger, entity_state->collision_layer))
        return;

    /* Check if trigger is ready to fire */
    can_fire = lrg_trigger2d_is_enabled (trigger) &&
               !lrg_trigger2d_is_on_cooldown (trigger);

    if (lrg_trigger2d_is_one_shot (trigger) && lrg_trigger2d_has_fired (trigger))
        can_fire = FALSE;

    /* Check current state */
    was_inside = g_hash_table_contains (entity_state->inside_triggers, trigger);
    is_inside = lrg_trigger2d_test_point (trigger, entity_state->x, entity_state->y);

    /* Handle state transitions */
    if (!was_inside && is_inside)
    {
        /* Entity just entered */
        g_hash_table_insert (entity_state->inside_triggers, trigger, GINT_TO_POINTER (TRUE));
        if (can_fire)
            emit_trigger_event (self, trigger, entity_state, LRG_TRIGGER2D_EVENT_ENTER);
    }
    else if (was_inside && is_inside)
    {
        /* Entity is still inside */
        if (can_fire)
            emit_trigger_event (self, trigger, entity_state, LRG_TRIGGER2D_EVENT_STAY);
    }
    else if (was_inside && !is_inside)
    {
        /* Entity just exited */
        g_hash_table_remove (entity_state->inside_triggers, trigger);
        /* Always emit exit, even if can't fire new enters */
        emit_trigger_event (self, trigger, entity_state, LRG_TRIGGER2D_EVENT_EXIT);
    }
}

/* GObject implementation */

static void
lrg_trigger_manager_finalize (GObject *object)
{
    LrgTriggerManager *self = LRG_TRIGGER_MANAGER (object);

    g_clear_pointer (&self->triggers, g_ptr_array_unref);
    g_clear_pointer (&self->triggers_by_id, g_hash_table_unref);
    g_clear_pointer (&self->entities, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_trigger_manager_parent_class)->finalize (object);
}

static void
lrg_trigger_manager_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgTriggerManager *self = LRG_TRIGGER_MANAGER (object);

    switch (prop_id)
    {
    case PROP_TRIGGER_COUNT:
        g_value_set_uint (value, self->triggers->len);
        break;
    case PROP_DEBUG_ENABLED:
        g_value_set_boolean (value, self->debug_enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger_manager_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgTriggerManager *self = LRG_TRIGGER_MANAGER (object);

    switch (prop_id)
    {
    case PROP_DEBUG_ENABLED:
        lrg_trigger_manager_set_debug_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger_manager_class_init (LrgTriggerManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize     = lrg_trigger_manager_finalize;
    object_class->get_property = lrg_trigger_manager_get_property;
    object_class->set_property = lrg_trigger_manager_set_property;

    /**
     * LrgTriggerManager:trigger-count:
     *
     * The number of registered triggers.
     *
     * Since: 1.0
     */
    properties[PROP_TRIGGER_COUNT] =
        g_param_spec_uint ("trigger-count",
                           "Trigger Count",
                           "Number of registered triggers",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTriggerManager:debug-enabled:
     *
     * Whether debug logging is enabled.
     *
     * Since: 1.0
     */
    properties[PROP_DEBUG_ENABLED] =
        g_param_spec_boolean ("debug-enabled",
                              "Debug Enabled",
                              "Whether debug logging is enabled",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTriggerManager::trigger-entered:
     * @self: The #LrgTriggerManager
     * @trigger: The trigger that was entered
     * @event: The trigger event
     *
     * Emitted when an entity enters a trigger.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TRIGGER_ENTERED] =
        g_signal_new ("trigger-entered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_TRIGGER2D,
                      LRG_TYPE_TRIGGER_EVENT);

    /**
     * LrgTriggerManager::trigger-stayed:
     * @self: The #LrgTriggerManager
     * @trigger: The trigger
     * @event: The trigger event
     *
     * Emitted when an entity stays inside a trigger.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TRIGGER_STAYED] =
        g_signal_new ("trigger-stayed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_TRIGGER2D,
                      LRG_TYPE_TRIGGER_EVENT);

    /**
     * LrgTriggerManager::trigger-exited:
     * @self: The #LrgTriggerManager
     * @trigger: The trigger that was exited
     * @event: The trigger event
     *
     * Emitted when an entity exits a trigger.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TRIGGER_EXITED] =
        g_signal_new ("trigger-exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_TRIGGER2D,
                      LRG_TYPE_TRIGGER_EVENT);
}

static void
lrg_trigger_manager_init (LrgTriggerManager *self)
{
    self->triggers = g_ptr_array_new_with_free_func (g_object_unref);
    self->triggers_by_id = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, NULL);
    self->entities = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                            NULL, (GDestroyNotify)entity_state_free);
    self->debug_enabled = FALSE;
}

/* Public API */

/**
 * lrg_trigger_manager_new:
 *
 * Creates a new trigger manager.
 *
 * Returns: (transfer full): A new #LrgTriggerManager
 *
 * Since: 1.0
 */
LrgTriggerManager *
lrg_trigger_manager_new (void)
{
    return g_object_new (LRG_TYPE_TRIGGER_MANAGER, NULL);
}

/**
 * lrg_trigger_manager_add_trigger:
 * @self: A #LrgTriggerManager
 * @trigger: The trigger to add
 *
 * Adds a trigger to the manager.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_add_trigger (LrgTriggerManager *self,
                                 LrgTrigger2D      *trigger)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));
    g_return_if_fail (LRG_IS_TRIGGER2D (trigger));

    g_ptr_array_add (self->triggers, g_object_ref (trigger));

    id = lrg_trigger2d_get_id (trigger);
    if (id != NULL)
        g_hash_table_insert (self->triggers_by_id, g_strdup (id), trigger);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGER_COUNT]);

    if (self->debug_enabled)
    {
        lrg_debug (LRG_LOG_DOMAIN_TRIGGER2D,
                   "Added trigger '%s'",
                   id ?: "(unnamed)");
    }
}

/**
 * lrg_trigger_manager_remove_trigger:
 * @self: A #LrgTriggerManager
 * @trigger: The trigger to remove
 *
 * Removes a trigger from the manager.
 *
 * Returns: %TRUE if the trigger was found and removed
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_manager_remove_trigger (LrgTriggerManager *self,
                                    LrgTrigger2D      *trigger)
{
    const gchar  *id;
    GHashTableIter iter;
    EntityState  *entity_state;

    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_TRIGGER2D (trigger), FALSE);

    if (!g_ptr_array_remove (self->triggers, trigger))
        return FALSE;

    /* Remove from ID lookup */
    id = lrg_trigger2d_get_id (trigger);
    if (id != NULL)
        g_hash_table_remove (self->triggers_by_id, id);

    /* Remove from all entity tracking */
    g_hash_table_iter_init (&iter, self->entities);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&entity_state))
    {
        g_hash_table_remove (entity_state->inside_triggers, trigger);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGER_COUNT]);

    return TRUE;
}

/**
 * lrg_trigger_manager_remove_trigger_by_id:
 * @self: A #LrgTriggerManager
 * @id: The trigger ID to remove
 *
 * Removes a trigger by its ID.
 *
 * Returns: %TRUE if a trigger was found and removed
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_manager_remove_trigger_by_id (LrgTriggerManager *self,
                                          const gchar       *id)
{
    LrgTrigger2D *trigger;

    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    trigger = g_hash_table_lookup (self->triggers_by_id, id);
    if (trigger == NULL)
        return FALSE;

    return lrg_trigger_manager_remove_trigger (self, trigger);
}

/**
 * lrg_trigger_manager_get_trigger:
 * @self: A #LrgTriggerManager
 * @id: The trigger ID to find
 *
 * Gets a trigger by its ID.
 *
 * Returns: (transfer none) (nullable): The trigger, or %NULL if not found
 *
 * Since: 1.0
 */
LrgTrigger2D *
lrg_trigger_manager_get_trigger (LrgTriggerManager *self,
                                 const gchar       *id)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->triggers_by_id, id);
}

/**
 * lrg_trigger_manager_get_triggers:
 * @self: A #LrgTriggerManager
 *
 * Gets all registered triggers.
 *
 * Returns: (transfer none) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
GPtrArray *
lrg_trigger_manager_get_triggers (LrgTriggerManager *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), NULL);
    return self->triggers;
}

/**
 * lrg_trigger_manager_get_trigger_count:
 * @self: A #LrgTriggerManager
 *
 * Gets the number of registered triggers.
 *
 * Returns: The trigger count
 *
 * Since: 1.0
 */
guint
lrg_trigger_manager_get_trigger_count (LrgTriggerManager *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), 0);
    return self->triggers->len;
}

/**
 * lrg_trigger_manager_clear:
 * @self: A #LrgTriggerManager
 *
 * Removes all triggers from the manager.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_clear (LrgTriggerManager *self)
{
    GHashTableIter iter;
    EntityState   *entity_state;

    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));

    g_ptr_array_set_size (self->triggers, 0);
    g_hash_table_remove_all (self->triggers_by_id);

    /* Clear entity tracking */
    g_hash_table_iter_init (&iter, self->entities);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&entity_state))
    {
        g_hash_table_remove_all (entity_state->inside_triggers);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGER_COUNT]);
}

/**
 * lrg_trigger_manager_register_entity:
 * @self: A #LrgTriggerManager
 * @entity: The entity to track
 * @collision_layer: The collision layer of the entity
 *
 * Registers an entity for trigger detection.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_register_entity (LrgTriggerManager *self,
                                     gpointer           entity,
                                     guint32            collision_layer)
{
    EntityState *state;

    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));
    g_return_if_fail (entity != NULL);

    if (g_hash_table_contains (self->entities, entity))
        return;

    state = entity_state_new (entity, collision_layer);
    g_hash_table_insert (self->entities, entity, state);

    if (self->debug_enabled)
    {
        lrg_debug (LRG_LOG_DOMAIN_TRIGGER2D,
                   "Registered entity %p with layer 0x%08X",
                   entity, collision_layer);
    }
}

/**
 * lrg_trigger_manager_unregister_entity:
 * @self: A #LrgTriggerManager
 * @entity: The entity to unregister
 *
 * Unregisters an entity from trigger detection.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_unregister_entity (LrgTriggerManager *self,
                                       gpointer           entity)
{
    EntityState    *state;
    GHashTableIter  iter;
    LrgTrigger2D   *trigger;

    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));
    g_return_if_fail (entity != NULL);

    state = g_hash_table_lookup (self->entities, entity);
    if (state == NULL)
        return;

    /* Emit exit events for any triggers we're inside */
    g_hash_table_iter_init (&iter, state->inside_triggers);
    while (g_hash_table_iter_next (&iter, (gpointer *)&trigger, NULL))
    {
        emit_trigger_event (self, trigger, state, LRG_TRIGGER2D_EVENT_EXIT);
    }

    g_hash_table_remove (self->entities, entity);

    if (self->debug_enabled)
    {
        lrg_debug (LRG_LOG_DOMAIN_TRIGGER2D,
                   "Unregistered entity %p", entity);
    }
}

/**
 * lrg_trigger_manager_set_entity_position:
 * @self: A #LrgTriggerManager
 * @entity: The entity
 * @x: New X position
 * @y: New Y position
 *
 * Updates an entity's position for trigger detection.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_set_entity_position (LrgTriggerManager *self,
                                         gpointer           entity,
                                         gfloat             x,
                                         gfloat             y)
{
    EntityState *state;

    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));
    g_return_if_fail (entity != NULL);

    state = g_hash_table_lookup (self->entities, entity);
    if (state == NULL)
        return;

    state->x = x;
    state->y = y;
}

/**
 * lrg_trigger_manager_set_entity_layer:
 * @self: A #LrgTriggerManager
 * @entity: The entity
 * @collision_layer: New collision layer
 *
 * Updates an entity's collision layer.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_set_entity_layer (LrgTriggerManager *self,
                                      gpointer           entity,
                                      guint32            collision_layer)
{
    EntityState *state;

    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));
    g_return_if_fail (entity != NULL);

    state = g_hash_table_lookup (self->entities, entity);
    if (state == NULL)
        return;

    state->collision_layer = collision_layer;
}

/**
 * lrg_trigger_manager_update:
 * @self: A #LrgTriggerManager
 * @delta_time: Time elapsed since last update
 *
 * Updates all triggers and processes entity positions.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_update (LrgTriggerManager *self,
                            gfloat             delta_time)
{
    GHashTableIter iter;
    EntityState   *entity_state;
    guint          i;

    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));

    /* Update cooldowns for all triggers */
    for (i = 0; i < self->triggers->len; i++)
    {
        LrgTrigger2D *trigger = g_ptr_array_index (self->triggers, i);
        lrg_trigger2d_update_cooldown (trigger, delta_time);
    }

    /* Process each entity against all triggers */
    g_hash_table_iter_init (&iter, self->entities);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&entity_state))
    {
        for (i = 0; i < self->triggers->len; i++)
        {
            LrgTrigger2D *trigger = g_ptr_array_index (self->triggers, i);
            process_entity_for_trigger (self, trigger, entity_state);
        }
    }
}

/**
 * lrg_trigger_manager_check_point:
 * @self: A #LrgTriggerManager
 * @x: X coordinate to check
 * @y: Y coordinate to check
 * @collision_layer: Collision layer to check against
 *
 * Checks which triggers contain the given point.
 *
 * Returns: (transfer full) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
GPtrArray *
lrg_trigger_manager_check_point (LrgTriggerManager *self,
                                 gfloat             x,
                                 gfloat             y,
                                 guint32            collision_layer)
{
    GPtrArray *result;
    guint      i;

    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), NULL);

    result = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < self->triggers->len; i++)
    {
        LrgTrigger2D *trigger = g_ptr_array_index (self->triggers, i);

        if (!lrg_trigger2d_is_enabled (trigger))
            continue;

        if (!lrg_trigger2d_can_collide_with (trigger, collision_layer))
            continue;

        if (lrg_trigger2d_test_point (trigger, x, y))
            g_ptr_array_add (result, g_object_ref (trigger));
    }

    return result;
}

/**
 * lrg_trigger_manager_check_bounds:
 * @self: A #LrgTriggerManager
 * @x: X coordinate of bounds
 * @y: Y coordinate of bounds
 * @width: Width of bounds
 * @height: Height of bounds
 * @collision_layer: Collision layer to check against
 *
 * Gets triggers that might overlap with the given bounds.
 *
 * Returns: (transfer full) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
GPtrArray *
lrg_trigger_manager_check_bounds (LrgTriggerManager *self,
                                  gfloat             x,
                                  gfloat             y,
                                  gfloat             width,
                                  gfloat             height,
                                  guint32            collision_layer)
{
    GPtrArray *result;
    guint      i;

    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), NULL);

    result = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < self->triggers->len; i++)
    {
        LrgTrigger2D *trigger = g_ptr_array_index (self->triggers, i);
        gfloat bx, by, bw, bh;

        if (!lrg_trigger2d_is_enabled (trigger))
            continue;

        if (!lrg_trigger2d_can_collide_with (trigger, collision_layer))
            continue;

        /* Get trigger bounds */
        lrg_trigger2d_get_bounds (trigger, &bx, &by, &bw, &bh);

        /* AABB intersection test */
        if (x < bx + bw && x + width > bx &&
            y < by + bh && y + height > by)
        {
            g_ptr_array_add (result, g_object_ref (trigger));
        }
    }

    return result;
}

/**
 * lrg_trigger_manager_get_entities_in_trigger:
 * @self: A #LrgTriggerManager
 * @trigger: The trigger to check
 *
 * Gets all entities currently inside the trigger.
 *
 * Returns: (transfer full) (element-type gpointer): Array of entity pointers
 *
 * Since: 1.0
 */
GPtrArray *
lrg_trigger_manager_get_entities_in_trigger (LrgTriggerManager *self,
                                             LrgTrigger2D      *trigger)
{
    GPtrArray      *result;
    GHashTableIter  iter;
    EntityState    *entity_state;

    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), NULL);
    g_return_val_if_fail (LRG_IS_TRIGGER2D (trigger), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->entities);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&entity_state))
    {
        if (g_hash_table_contains (entity_state->inside_triggers, trigger))
            g_ptr_array_add (result, entity_state->entity);
    }

    return result;
}

/**
 * lrg_trigger_manager_get_triggers_containing_entity:
 * @self: A #LrgTriggerManager
 * @entity: The entity to check
 *
 * Gets all triggers that currently contain the entity.
 *
 * Returns: (transfer full) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
GPtrArray *
lrg_trigger_manager_get_triggers_containing_entity (LrgTriggerManager *self,
                                                    gpointer           entity)
{
    GPtrArray      *result;
    EntityState    *state;
    GHashTableIter  iter;
    LrgTrigger2D   *trigger;

    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), NULL);
    g_return_val_if_fail (entity != NULL, NULL);

    result = g_ptr_array_new_with_free_func (g_object_unref);

    state = g_hash_table_lookup (self->entities, entity);
    if (state == NULL)
        return result;

    g_hash_table_iter_init (&iter, state->inside_triggers);
    while (g_hash_table_iter_next (&iter, (gpointer *)&trigger, NULL))
    {
        g_ptr_array_add (result, g_object_ref (trigger));
    }

    return result;
}

/**
 * lrg_trigger_manager_set_debug_enabled:
 * @self: A #LrgTriggerManager
 * @enabled: Whether debug mode is enabled
 *
 * Enables or disables debug mode.
 *
 * Since: 1.0
 */
void
lrg_trigger_manager_set_debug_enabled (LrgTriggerManager *self,
                                       gboolean           enabled)
{
    g_return_if_fail (LRG_IS_TRIGGER_MANAGER (self));

    if (self->debug_enabled != enabled)
    {
        self->debug_enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEBUG_ENABLED]);
    }
}

/**
 * lrg_trigger_manager_is_debug_enabled:
 * @self: A #LrgTriggerManager
 *
 * Checks if debug mode is enabled.
 *
 * Returns: %TRUE if debug mode is enabled
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_manager_is_debug_enabled (LrgTriggerManager *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_MANAGER (self), FALSE);
    return self->debug_enabled;
}
