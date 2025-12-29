/* lrg-traffic-agent.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-traffic-agent.h"

/* Default values */
#define DEFAULT_MAX_SPEED           30.0f
#define DEFAULT_DETECTION_RANGE     20.0f
#define ARRIVAL_THRESHOLD           5.0f
#define STEERING_LOOK_AHEAD         10.0f

/* Behavior multipliers */
#define CALM_SPEED_MULT             0.7f
#define CALM_FOLLOW_DIST            15.0f
#define NORMAL_SPEED_MULT           1.0f
#define NORMAL_FOLLOW_DIST          10.0f
#define AGGRESSIVE_SPEED_MULT       1.2f
#define AGGRESSIVE_FOLLOW_DIST      5.0f

typedef struct _Obstacle
{
    gfloat x;
    gfloat y;
    gfloat z;
    gfloat radius;
} Obstacle;

typedef struct _LrgTrafficAgentPrivate
{
    /* Controlled vehicle */
    LrgVehicle *vehicle;

    /* Road network */
    LrgRoadNetwork *network;

    /* Navigation */
    GList *route;
    gchar *current_road_id;
    gfloat current_t;
    gchar *dest_road_id;
    gfloat dest_t;
    gboolean has_destination;

    /* Behavior */
    LrgTrafficBehavior behavior;
    gfloat max_speed;
    gfloat detection_range;

    /* State */
    LrgTrafficState state;
    gboolean is_active;

    /* Obstacles */
    GArray *obstacles;

    /* Internal state */
    gfloat target_speed;
    gfloat steering_input;
} LrgTrafficAgentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTrafficAgent, lrg_traffic_agent, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_VEHICLE,
    PROP_ROAD_NETWORK,
    PROP_BEHAVIOR,
    PROP_MAX_SPEED,
    PROP_STATE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_DESTINATION_REACHED,
    SIGNAL_OBSTACLE_DETECTED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Forward declarations */
static void lrg_traffic_agent_real_update_ai (LrgTrafficAgent *self, gfloat delta);
static void lrg_traffic_agent_real_on_obstacle (LrgTrafficAgent *self, gfloat distance);
static void lrg_traffic_agent_real_on_destination (LrgTrafficAgent *self);

static void
lrg_traffic_agent_dispose (GObject *object)
{
    LrgTrafficAgent *self;
    LrgTrafficAgentPrivate *priv;

    self = LRG_TRAFFIC_AGENT (object);
    priv = lrg_traffic_agent_get_instance_private (self);

    g_clear_object (&priv->vehicle);
    g_clear_object (&priv->network);

    G_OBJECT_CLASS (lrg_traffic_agent_parent_class)->dispose (object);
}

static void
lrg_traffic_agent_finalize (GObject *object)
{
    LrgTrafficAgent *self;
    LrgTrafficAgentPrivate *priv;

    self = LRG_TRAFFIC_AGENT (object);
    priv = lrg_traffic_agent_get_instance_private (self);

    g_list_free (priv->route);
    g_free (priv->current_road_id);
    g_free (priv->dest_road_id);
    g_array_unref (priv->obstacles);

    G_OBJECT_CLASS (lrg_traffic_agent_parent_class)->finalize (object);
}

static void
lrg_traffic_agent_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgTrafficAgent *self;
    LrgTrafficAgentPrivate *priv;

    self = LRG_TRAFFIC_AGENT (object);
    priv = lrg_traffic_agent_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        g_value_set_object (value, priv->vehicle);
        break;

    case PROP_ROAD_NETWORK:
        g_value_set_object (value, priv->network);
        break;

    case PROP_BEHAVIOR:
        g_value_set_int (value, priv->behavior);
        break;

    case PROP_MAX_SPEED:
        g_value_set_float (value, priv->max_speed);
        break;

    case PROP_STATE:
        g_value_set_int (value, priv->state);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_traffic_agent_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgTrafficAgent *self;

    self = LRG_TRAFFIC_AGENT (object);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        lrg_traffic_agent_set_vehicle (self, g_value_get_object (value));
        break;

    case PROP_ROAD_NETWORK:
        lrg_traffic_agent_set_road_network (self, g_value_get_object (value));
        break;

    case PROP_BEHAVIOR:
        lrg_traffic_agent_set_behavior (self, g_value_get_int (value));
        break;

    case PROP_MAX_SPEED:
        lrg_traffic_agent_set_max_speed (self, g_value_get_float (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_traffic_agent_class_init (LrgTrafficAgentClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_traffic_agent_dispose;
    object_class->finalize = lrg_traffic_agent_finalize;
    object_class->get_property = lrg_traffic_agent_get_property;
    object_class->set_property = lrg_traffic_agent_set_property;

    klass->update_ai = lrg_traffic_agent_real_update_ai;
    klass->on_obstacle_detected = lrg_traffic_agent_real_on_obstacle;
    klass->on_destination_reached = lrg_traffic_agent_real_on_destination;

    properties[PROP_VEHICLE] =
        g_param_spec_object ("vehicle",
                             "Vehicle",
                             "Controlled vehicle",
                             LRG_TYPE_VEHICLE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_ROAD_NETWORK] =
        g_param_spec_object ("road-network",
                             "Road Network",
                             "Road network for navigation",
                             LRG_TYPE_ROAD_NETWORK,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_BEHAVIOR] =
        g_param_spec_int ("behavior",
                          "Behavior",
                          "Driving behavior",
                          LRG_TRAFFIC_BEHAVIOR_CALM, LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE,
                          LRG_TRAFFIC_BEHAVIOR_NORMAL,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_SPEED] =
        g_param_spec_float ("max-speed",
                            "Max Speed",
                            "Maximum driving speed",
                            0.0f, 1000.0f, DEFAULT_MAX_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_STATE] =
        g_param_spec_int ("state",
                          "State",
                          "Current state",
                          LRG_TRAFFIC_STATE_IDLE, LRG_TRAFFIC_STATE_ARRIVED,
                          LRG_TRAFFIC_STATE_IDLE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_DESTINATION_REACHED] =
        g_signal_new ("destination-reached",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgTrafficAgentClass, on_destination_reached),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_OBSTACLE_DETECTED] =
        g_signal_new ("obstacle-detected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgTrafficAgentClass, on_obstacle_detected),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_FLOAT);
}

static void
lrg_traffic_agent_init (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    priv = lrg_traffic_agent_get_instance_private (self);

    priv->vehicle = NULL;
    priv->network = NULL;

    priv->route = NULL;
    priv->current_road_id = NULL;
    priv->current_t = 0.0f;
    priv->dest_road_id = NULL;
    priv->dest_t = 0.0f;
    priv->has_destination = FALSE;

    priv->behavior = LRG_TRAFFIC_BEHAVIOR_NORMAL;
    priv->max_speed = DEFAULT_MAX_SPEED;
    priv->detection_range = DEFAULT_DETECTION_RANGE;

    priv->state = LRG_TRAFFIC_STATE_IDLE;
    priv->is_active = FALSE;

    priv->obstacles = g_array_new (FALSE, TRUE, sizeof (Obstacle));

    priv->target_speed = 0.0f;
    priv->steering_input = 0.0f;
}

/*
 * get_behavior_speed_multiplier:
 *
 * Returns speed multiplier for behavior.
 */
static gfloat
get_behavior_speed_multiplier (LrgTrafficBehavior behavior)
{
    switch (behavior)
    {
    case LRG_TRAFFIC_BEHAVIOR_CALM:
        return CALM_SPEED_MULT;
    case LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE:
        return AGGRESSIVE_SPEED_MULT;
    default:
        return NORMAL_SPEED_MULT;
    }
}

/*
 * check_obstacles:
 *
 * Checks for obstacles ahead and returns nearest distance.
 */
static gfloat
check_obstacles (LrgTrafficAgent *self,
                 gfloat           veh_x,
                 gfloat           veh_y,
                 gfloat           veh_z,
                 gfloat           forward_x,
                 gfloat           forward_z)
{
    LrgTrafficAgentPrivate *priv;
    gfloat nearest_dist;
    guint i;

    priv = lrg_traffic_agent_get_instance_private (self);
    nearest_dist = G_MAXFLOAT;

    for (i = 0; i < priv->obstacles->len; i++)
    {
        Obstacle *obs = &g_array_index (priv->obstacles, Obstacle, i);
        gfloat dx, dy, dz;
        gfloat dist;
        gfloat dot;

        dx = obs->x - veh_x;
        dy = obs->y - veh_y;
        dz = obs->z - veh_z;

        dist = sqrtf (dx * dx + dy * dy + dz * dz) - obs->radius;

        /* Check if ahead of vehicle */
        dot = dx * forward_x + dz * forward_z;
        if (dot > 0.0f && dist < nearest_dist && dist < priv->detection_range)
        {
            nearest_dist = dist;
        }
    }

    return nearest_dist;
}

/*
 * lrg_traffic_agent_real_update_ai:
 *
 * Default AI update implementation.
 */
static void
lrg_traffic_agent_real_update_ai (LrgTrafficAgent *self,
                                  gfloat           delta)
{
    LrgTrafficAgentPrivate *priv;
    gfloat veh_x, veh_y, veh_z;
    gfloat forward_x, forward_y, forward_z;
    gfloat target_x, target_y, target_z;
    gfloat to_target_x, to_target_z;
    gfloat cross;
    gfloat speed_mult;
    gfloat road_speed_limit;
    gfloat obstacle_dist;
    LrgRoad *current_road;

    priv = lrg_traffic_agent_get_instance_private (self);

    if (priv->vehicle == NULL || priv->network == NULL)
        return;

    if (!priv->is_active)
        return;

    /* Get vehicle state */
    lrg_vehicle_get_position (priv->vehicle, &veh_x, &veh_y, &veh_z);
    lrg_vehicle_get_forward_vector (priv->vehicle, &forward_x, &forward_y, &forward_z);

    /* Update current road position */
    {
        const gchar *nearest_road;
        gfloat t, dist;

        if (lrg_road_network_get_nearest_road (priv->network, veh_x, veh_y, veh_z,
                                               &nearest_road, &t, &dist))
        {
            g_free (priv->current_road_id);
            priv->current_road_id = g_strdup (nearest_road);
            priv->current_t = t;
        }
    }

    /* Check if arrived at destination */
    if (priv->has_destination && priv->dest_road_id != NULL)
    {
        if (g_strcmp0 (priv->current_road_id, priv->dest_road_id) == 0)
        {
            gfloat dist_on_road = fabsf (priv->current_t - priv->dest_t);
            LrgRoad *dest_road = lrg_road_network_get_road (priv->network, priv->dest_road_id);

            if (dest_road != NULL)
            {
                gfloat dist_in_units = dist_on_road * lrg_road_get_length (dest_road);

                if (dist_in_units < ARRIVAL_THRESHOLD)
                {
                    priv->state = LRG_TRAFFIC_STATE_ARRIVED;
                    priv->has_destination = FALSE;
                    g_signal_emit (self, signals[SIGNAL_DESTINATION_REACHED], 0);
                    return;
                }
            }
        }
    }

    /* Calculate target point on road ahead */
    current_road = lrg_road_network_get_road (priv->network, priv->current_road_id);
    if (current_road == NULL)
    {
        priv->state = LRG_TRAFFIC_STATE_STOPPED;
        lrg_vehicle_set_throttle (priv->vehicle, 0.0f);
        lrg_vehicle_set_brake (priv->vehicle, 1.0f);
        return;
    }

    {
        gfloat road_length = lrg_road_get_length (current_road);
        gfloat look_ahead_t;

        if (road_length > 0.0f)
            look_ahead_t = priv->current_t + (STEERING_LOOK_AHEAD / road_length);
        else
            look_ahead_t = priv->current_t + 0.1f;

        look_ahead_t = CLAMP (look_ahead_t, 0.0f, 1.0f);

        lrg_road_interpolate (current_road, look_ahead_t, &target_x, &target_y, &target_z);
    }

    /* Calculate steering */
    to_target_x = target_x - veh_x;
    to_target_z = target_z - veh_z;

    /* Cross product for turn direction */
    cross = forward_x * to_target_z - forward_z * to_target_x;

    /* Normalize cross product for steering input */
    priv->steering_input = CLAMP (cross * 2.0f, -1.0f, 1.0f);

    /* Calculate speed */
    speed_mult = get_behavior_speed_multiplier (priv->behavior);
    road_speed_limit = lrg_road_get_speed_limit_at (current_road, priv->current_t);
    priv->target_speed = fminf (priv->max_speed, road_speed_limit) * speed_mult;

    /* Check for obstacles */
    obstacle_dist = check_obstacles (self, veh_x, veh_y, veh_z, forward_x, forward_z);

    if (obstacle_dist < priv->detection_range)
    {
        priv->state = LRG_TRAFFIC_STATE_AVOIDING;
        g_signal_emit (self, signals[SIGNAL_OBSTACLE_DETECTED], 0, obstacle_dist);

        /* Slow down based on distance */
        if (obstacle_dist < 5.0f)
        {
            priv->target_speed = 0.0f;
            priv->state = LRG_TRAFFIC_STATE_STOPPED;
        }
        else
        {
            priv->target_speed *= (obstacle_dist / priv->detection_range);
        }
    }
    else
    {
        priv->state = LRG_TRAFFIC_STATE_DRIVING;
    }

    /* Apply controls to vehicle */
    {
        gfloat current_speed = lrg_vehicle_get_speed (priv->vehicle);

        lrg_vehicle_set_steering (priv->vehicle, priv->steering_input);

        if (current_speed < priv->target_speed * 0.9f)
        {
            lrg_vehicle_set_throttle (priv->vehicle, 1.0f);
            lrg_vehicle_set_brake (priv->vehicle, 0.0f);
        }
        else if (current_speed > priv->target_speed * 1.1f)
        {
            lrg_vehicle_set_throttle (priv->vehicle, 0.0f);
            lrg_vehicle_set_brake (priv->vehicle, 0.5f);
        }
        else
        {
            lrg_vehicle_set_throttle (priv->vehicle, 0.5f);
            lrg_vehicle_set_brake (priv->vehicle, 0.0f);
        }
    }
}

static void
lrg_traffic_agent_real_on_obstacle (LrgTrafficAgent *self,
                                    gfloat           distance)
{
    (void)self;
    (void)distance;
    /* Default: nothing special */
}

static void
lrg_traffic_agent_real_on_destination (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    priv = lrg_traffic_agent_get_instance_private (self);

    /* Default: set new random destination */
    lrg_traffic_agent_set_random_destination (self);
    priv->state = LRG_TRAFFIC_STATE_DRIVING;
}

/*
 * Public API
 */

LrgTrafficAgent *
lrg_traffic_agent_new (void)
{
    return g_object_new (LRG_TYPE_TRAFFIC_AGENT, NULL);
}

void
lrg_traffic_agent_set_vehicle (LrgTrafficAgent *self,
                               LrgVehicle      *vehicle)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));
    g_return_if_fail (vehicle == NULL || LRG_IS_VEHICLE (vehicle));

    priv = lrg_traffic_agent_get_instance_private (self);

    if (priv->vehicle == vehicle)
        return;

    g_clear_object (&priv->vehicle);
    if (vehicle != NULL)
        priv->vehicle = g_object_ref (vehicle);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VEHICLE]);
}

LrgVehicle *
lrg_traffic_agent_get_vehicle (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), NULL);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->vehicle;
}

void
lrg_traffic_agent_set_road_network (LrgTrafficAgent *self,
                                    LrgRoadNetwork  *network)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));
    g_return_if_fail (network == NULL || LRG_IS_ROAD_NETWORK (network));

    priv = lrg_traffic_agent_get_instance_private (self);

    if (priv->network == network)
        return;

    g_clear_object (&priv->network);
    if (network != NULL)
        priv->network = g_object_ref (network);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROAD_NETWORK]);
}

LrgRoadNetwork *
lrg_traffic_agent_get_road_network (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), NULL);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->network;
}

gboolean
lrg_traffic_agent_set_destination (LrgTrafficAgent *self,
                                   const gchar     *road_id,
                                   gfloat           t)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), FALSE);
    g_return_val_if_fail (road_id != NULL, FALSE);

    priv = lrg_traffic_agent_get_instance_private (self);

    if (priv->network == NULL)
        return FALSE;

    /* Verify road exists */
    if (lrg_road_network_get_road (priv->network, road_id) == NULL)
        return FALSE;

    g_free (priv->dest_road_id);
    priv->dest_road_id = g_strdup (road_id);
    priv->dest_t = CLAMP (t, 0.0f, 1.0f);
    priv->has_destination = TRUE;

    /* Find route */
    g_list_free (priv->route);
    priv->route = NULL;

    if (priv->current_road_id != NULL)
    {
        lrg_road_network_find_route (priv->network,
                                     priv->current_road_id, priv->current_t,
                                     priv->dest_road_id, priv->dest_t,
                                     &priv->route);
    }

    return TRUE;
}

gboolean
lrg_traffic_agent_set_random_destination (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;
    const gchar *road_id;
    gfloat t;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), FALSE);

    priv = lrg_traffic_agent_get_instance_private (self);

    if (priv->network == NULL)
        return FALSE;

    if (lrg_road_network_get_random_spawn_point (priv->network,
                                                  NULL, NULL, NULL, NULL,
                                                  &road_id, &t))
    {
        return lrg_traffic_agent_set_destination (self, road_id, t);
    }

    return FALSE;
}

void
lrg_traffic_agent_clear_destination (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));

    priv = lrg_traffic_agent_get_instance_private (self);

    g_clear_pointer (&priv->dest_road_id, g_free);
    priv->dest_t = 0.0f;
    priv->has_destination = FALSE;

    g_list_free (priv->route);
    priv->route = NULL;
}

gboolean
lrg_traffic_agent_has_destination (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), FALSE);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->has_destination;
}

void
lrg_traffic_agent_set_behavior (LrgTrafficAgent   *self,
                                LrgTrafficBehavior behavior)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));

    priv = lrg_traffic_agent_get_instance_private (self);

    priv->behavior = behavior;
}

LrgTrafficBehavior
lrg_traffic_agent_get_behavior (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), LRG_TRAFFIC_BEHAVIOR_NORMAL);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->behavior;
}

void
lrg_traffic_agent_set_max_speed (LrgTrafficAgent *self,
                                 gfloat           speed)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));
    g_return_if_fail (speed >= 0.0f);

    priv = lrg_traffic_agent_get_instance_private (self);

    priv->max_speed = speed;
}

gfloat
lrg_traffic_agent_get_max_speed (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), DEFAULT_MAX_SPEED);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->max_speed;
}

LrgTrafficState
lrg_traffic_agent_get_state (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), LRG_TRAFFIC_STATE_IDLE);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->state;
}

const gchar *
lrg_traffic_agent_get_current_road (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), NULL);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->current_road_id;
}

gfloat
lrg_traffic_agent_get_current_position (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), 0.0f);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->current_t;
}

void
lrg_traffic_agent_set_obstacle_detection_range (LrgTrafficAgent *self,
                                                gfloat           range)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));
    g_return_if_fail (range >= 0.0f);

    priv = lrg_traffic_agent_get_instance_private (self);

    priv->detection_range = range;
}

void
lrg_traffic_agent_add_obstacle (LrgTrafficAgent *self,
                                gfloat           x,
                                gfloat           y,
                                gfloat           z,
                                gfloat           radius)
{
    LrgTrafficAgentPrivate *priv;
    Obstacle obs;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));

    priv = lrg_traffic_agent_get_instance_private (self);

    obs.x = x;
    obs.y = y;
    obs.z = z;
    obs.radius = radius;

    g_array_append_val (priv->obstacles, obs);
}

void
lrg_traffic_agent_clear_obstacles (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));

    priv = lrg_traffic_agent_get_instance_private (self);

    g_array_set_size (priv->obstacles, 0);
}

void
lrg_traffic_agent_update (LrgTrafficAgent *self,
                          gfloat           delta)
{
    LrgTrafficAgentClass *klass;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));
    g_return_if_fail (delta > 0.0f);

    klass = LRG_TRAFFIC_AGENT_GET_CLASS (self);

    if (klass->update_ai != NULL)
        klass->update_ai (self, delta);
}

void
lrg_traffic_agent_start (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));

    priv = lrg_traffic_agent_get_instance_private (self);

    priv->is_active = TRUE;
    priv->state = LRG_TRAFFIC_STATE_DRIVING;
}

void
lrg_traffic_agent_stop (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_if_fail (LRG_IS_TRAFFIC_AGENT (self));

    priv = lrg_traffic_agent_get_instance_private (self);

    priv->is_active = FALSE;
    priv->state = LRG_TRAFFIC_STATE_IDLE;

    if (priv->vehicle != NULL)
    {
        lrg_vehicle_set_throttle (priv->vehicle, 0.0f);
        lrg_vehicle_set_brake (priv->vehicle, 1.0f);
        lrg_vehicle_set_steering (priv->vehicle, 0.0f);
    }
}

gboolean
lrg_traffic_agent_is_active (LrgTrafficAgent *self)
{
    LrgTrafficAgentPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRAFFIC_AGENT (self), FALSE);

    priv = lrg_traffic_agent_get_instance_private (self);

    return priv->is_active;
}
