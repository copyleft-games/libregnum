/* game-tuktuk-derby.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * TukTuk Derby - A top-down demolition derby game demonstrating libregnum's
 * physics, AI, and game state systems. Crash your tuktuk into AI opponents
 * and score points through damage, ring-outs, and combos.
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>
#include <string.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define ARENA_RADIUS          180.0f
#define ARENA_RINGOUT_RADIUS  190.0f
#define TUKTUK_WIDTH          4.0f
#define TUKTUK_LENGTH         6.0f
#define TUKTUK_COLLISION_RADIUS 3.0f

#define DEFAULT_MAX_SPEED     35.0f
#define DEFAULT_ACCELERATION  60.0f
#define DEFAULT_TURN_RATE     5.0f
#define DEFAULT_MASS          1.0f
#define DEFAULT_HEALTH        400.0f
#define DEFAULT_RAM_DAMAGE    10.0f

/* Boost system */
#define BOOST_MAX_CHARGE      100.0f
#define BOOST_RECHARGE_RATE   25.0f   /* Per second */
#define BOOST_COST            50.0f   /* Per boost */
#define BOOST_MULTIPLIER      4.0f    /* Speed multiplier during boost */
#define BOOST_DURATION        1.0f    /* Seconds */

/* Collision bounce */
#define COLLISION_BOUNCE      2.5f    /* Velocity multiplier on hit */
#define SPEED_DAMAGE_SCALE    0.5f    /* Extra damage per unit speed */

/* Directional damage zones (angle from facing direction) */
#define FRONT_ARMOR_MULT      0.25f   /* Front is armored - 75% damage reduction */
#define SIDE_DAMAGE_MULT      2.0f    /* Sides are vulnerable - 2x damage */
#define REAR_DAMAGE_MULT      1.0f    /* Rear is normal damage */
#define FRONT_ANGLE           0.7f    /* ~40 degrees from front = front zone */
#define SIDE_ANGLE            2.4f    /* ~140 degrees from front = side zone, beyond = rear */

#define COMBO_WINDOW          2.0f
#define POINTS_PER_DAMAGE     1.0f
#define KNOCKOUT_BONUS        100

/* Particle system */
#define MAX_PARTICLES         200
#define PARTICLE_LIFETIME     0.8f
#define PARTICLE_SPEED        40.0f
#define PARTICLE_GRAVITY      20.0f
#define RINGOUT_BONUS         150
#define SURVIVAL_POINTS_PER_SEC 2

#define POWERUP_RESPAWN_TIME  10.0f
#define POWERUP_RADIUS        1.0f

#define AI_REACTION_TIME      0.1f
#define AI_TARGETING_RANGE    50.0f

#define COUNTDOWN_DURATION    3.0f
#define RESULTS_DISPLAY_TIME  5.0f

/* =============================================================================
 * ENUMERATIONS
 * ========================================================================== */

typedef enum
{
	DERBY_GAME_MODE_QUICK_MATCH,
	DERBY_GAME_MODE_TOURNAMENT,
	DERBY_GAME_MODE_SURVIVAL
} DerbyGameMode;

typedef enum
{
	DERBY_STATE_MENU,
	DERBY_STATE_COUNTDOWN,
	DERBY_STATE_PLAYING,
	DERBY_STATE_PAUSED,
	DERBY_STATE_RESULTS
} DerbyState;

typedef enum
{
	DERBY_HAZARD_NONE = 0,
	DERBY_HAZARD_OIL_SLICK,
	DERBY_HAZARD_SPIKE_STRIP,
	DERBY_HAZARD_RAMP,
	DERBY_HAZARD_FIRE_PIT,
	DERBY_HAZARD_BARRIER
} DerbyHazardType;

typedef enum
{
	DERBY_POWERUP_NONE = 0,
	/* Offensive */
	DERBY_POWERUP_SPEED_BOOST,
	DERBY_POWERUP_RAM_DAMAGE,
	DERBY_POWERUP_NITRO,
	/* Defensive */
	DERBY_POWERUP_SHIELD,
	DERBY_POWERUP_REPAIR,
	DERBY_POWERUP_ARMOR
} DerbyPowerUpType;

/* =============================================================================
 * FORWARD DECLARATIONS
 * ========================================================================== */

typedef struct _DerbyTukTuk DerbyTukTuk;
typedef struct _DerbyHazard DerbyHazard;
typedef struct _DerbyPowerUp DerbyPowerUp;
typedef struct _DerbyArena DerbyArena;
typedef struct _DerbyGame DerbyGame;

/* Particle for explosion effects */
typedef struct
{
	gfloat x, y;
	gfloat vx, vy;
	gfloat life;
	gfloat max_life;
	gfloat size;
	guint8 r, g, b;
	gboolean active;
} DerbyParticle;

#define DERBY_TYPE_TUKTUK (derby_tuktuk_get_type())
#define DERBY_TYPE_HAZARD (derby_hazard_get_type())
#define DERBY_TYPE_POWERUP (derby_powerup_get_type())
#define DERBY_TYPE_ARENA (derby_arena_get_type())
#define DERBY_TYPE_GAME (derby_game_get_type())

G_DECLARE_FINAL_TYPE (DerbyTukTuk, derby_tuktuk, DERBY, TUKTUK, GObject)
G_DECLARE_FINAL_TYPE (DerbyHazard, derby_hazard, DERBY, HAZARD, GObject)
G_DECLARE_FINAL_TYPE (DerbyPowerUp, derby_powerup, DERBY, POWERUP, GObject)
G_DECLARE_FINAL_TYPE (DerbyArena, derby_arena, DERBY, ARENA, GObject)
G_DECLARE_FINAL_TYPE (DerbyGame, derby_game, DERBY, GAME, GObject)

/* =============================================================================
 * DERBY_TUKTUK TYPE
 * ========================================================================== */

struct _DerbyTukTuk
{
	GObject parent_instance;

	/* Position and movement */
	gfloat x, y;
	gfloat rotation;
	gfloat velocity_x, velocity_y;
	gfloat angular_velocity;

	/* Vehicle properties */
	gfloat max_speed;
	gfloat acceleration;
	gfloat turn_rate;
	gfloat mass;

	/* Damage and health */
	gfloat health;
	gfloat max_health;
	gfloat armor;
	gboolean is_destroyed;
	gboolean needs_explosion;

	/* Combat */
	gfloat ram_damage;
	gfloat damage_multiplier;

	/* Scoring */
	gint damage_dealt;
	gint knockouts;
	gint score;

	/* Power-up state */
	DerbyPowerUpType active_powerup;
	gfloat powerup_time;

	/* Boost system */
	gfloat boost_charge;
	gfloat boost_timer;
	gboolean is_boosting;

	/* Combo tracking */
	gfloat combo_timer;
	gint combo_count;

	/* Visual */
	GrlColor *color;
	gint player_index;  /* -1 for AI, 0 for player */

	/* AI state */
	gfloat ai_think_timer;
	gint ai_target_index;
	gfloat ai_accel_input;
	gfloat ai_steer_input;
};

enum
{
	PROP_TUKTUK_0,
	PROP_TUKTUK_X,
	PROP_TUKTUK_Y,
	PROP_TUKTUK_ROTATION,
	PROP_TUKTUK_HEALTH,
	PROP_TUKTUK_MAX_HEALTH,
	PROP_TUKTUK_SCORE,
	PROP_TUKTUK_COLOR,
	PROP_TUKTUK_PLAYER_INDEX,
	N_TUKTUK_PROPS
};

static GParamSpec *tuktuk_properties[N_TUKTUK_PROPS];

G_DEFINE_TYPE (DerbyTukTuk, derby_tuktuk, G_TYPE_OBJECT)

static void
derby_tuktuk_finalize (GObject *object)
{
	DerbyTukTuk *self = DERBY_TUKTUK (object);

	g_clear_pointer (&self->color, grl_color_free);

	G_OBJECT_CLASS (derby_tuktuk_parent_class)->finalize (object);
}

static void
derby_tuktuk_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	DerbyTukTuk *self = DERBY_TUKTUK (object);

	switch (prop_id)
	{
	case PROP_TUKTUK_X:
		g_value_set_float (value, self->x);
		break;
	case PROP_TUKTUK_Y:
		g_value_set_float (value, self->y);
		break;
	case PROP_TUKTUK_ROTATION:
		g_value_set_float (value, self->rotation);
		break;
	case PROP_TUKTUK_HEALTH:
		g_value_set_float (value, self->health);
		break;
	case PROP_TUKTUK_MAX_HEALTH:
		g_value_set_float (value, self->max_health);
		break;
	case PROP_TUKTUK_SCORE:
		g_value_set_int (value, self->score);
		break;
	case PROP_TUKTUK_COLOR:
		g_value_set_boxed (value, self->color);
		break;
	case PROP_TUKTUK_PLAYER_INDEX:
		g_value_set_int (value, self->player_index);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_tuktuk_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	DerbyTukTuk *self = DERBY_TUKTUK (object);

	switch (prop_id)
	{
	case PROP_TUKTUK_X:
		self->x = g_value_get_float (value);
		break;
	case PROP_TUKTUK_Y:
		self->y = g_value_get_float (value);
		break;
	case PROP_TUKTUK_ROTATION:
		self->rotation = g_value_get_float (value);
		break;
	case PROP_TUKTUK_HEALTH:
		self->health = g_value_get_float (value);
		break;
	case PROP_TUKTUK_MAX_HEALTH:
		self->max_health = g_value_get_float (value);
		break;
	case PROP_TUKTUK_SCORE:
		self->score = g_value_get_int (value);
		break;
	case PROP_TUKTUK_COLOR:
		g_clear_pointer (&self->color, grl_color_free);
		self->color = g_value_dup_boxed (value);
		break;
	case PROP_TUKTUK_PLAYER_INDEX:
		self->player_index = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_tuktuk_class_init (DerbyTukTukClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = derby_tuktuk_finalize;
	object_class->get_property = derby_tuktuk_get_property;
	object_class->set_property = derby_tuktuk_set_property;

	tuktuk_properties[PROP_TUKTUK_X] =
		g_param_spec_float ("x", "X", "X position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	tuktuk_properties[PROP_TUKTUK_Y] =
		g_param_spec_float ("y", "Y", "Y position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	tuktuk_properties[PROP_TUKTUK_ROTATION] =
		g_param_spec_float ("rotation", "Rotation", "Rotation angle in radians",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	tuktuk_properties[PROP_TUKTUK_HEALTH] =
		g_param_spec_float ("health", "Health", "Current health",
		                    0.0f, G_MAXFLOAT, DEFAULT_HEALTH,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	tuktuk_properties[PROP_TUKTUK_MAX_HEALTH] =
		g_param_spec_float ("max-health", "Max Health", "Maximum health",
		                    0.0f, G_MAXFLOAT, DEFAULT_HEALTH,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	tuktuk_properties[PROP_TUKTUK_SCORE] =
		g_param_spec_int ("score", "Score", "Current score",
		                  0, G_MAXINT, 0,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	tuktuk_properties[PROP_TUKTUK_COLOR] =
		g_param_spec_boxed ("color", "Color", "TukTuk color",
		                    GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	tuktuk_properties[PROP_TUKTUK_PLAYER_INDEX] =
		g_param_spec_int ("player-index", "Player Index", "Player index (-1 for AI)",
		                  -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_TUKTUK_PROPS, tuktuk_properties);
}

static void
derby_tuktuk_init (DerbyTukTuk *self)
{
	self->x = 0.0f;
	self->y = 0.0f;
	self->rotation = 0.0f;
	self->velocity_x = 0.0f;
	self->velocity_y = 0.0f;
	self->angular_velocity = 0.0f;

	self->max_speed = DEFAULT_MAX_SPEED;
	self->acceleration = DEFAULT_ACCELERATION;
	self->turn_rate = DEFAULT_TURN_RATE;
	self->mass = DEFAULT_MASS;

	self->health = DEFAULT_HEALTH;
	self->max_health = DEFAULT_HEALTH;
	self->armor = 0.0f;
	self->is_destroyed = FALSE;
	self->needs_explosion = FALSE;

	self->ram_damage = DEFAULT_RAM_DAMAGE;
	self->damage_multiplier = 1.0f;

	self->damage_dealt = 0;
	self->knockouts = 0;
	self->score = 0;

	self->active_powerup = DERBY_POWERUP_NONE;
	self->powerup_time = 0.0f;

	self->boost_charge = BOOST_MAX_CHARGE;
	self->boost_timer = 0.0f;
	self->is_boosting = FALSE;

	self->combo_timer = 0.0f;
	self->combo_count = 0;

	self->color = grl_color_new (255, 100, 100, 255);
	self->player_index = -1;

	self->ai_think_timer = 0.0f;
	self->ai_target_index = -1;
	self->ai_accel_input = 1.0f;  /* AI starts accelerating */
	self->ai_steer_input = 0.0f;
}

static DerbyTukTuk *
derby_tuktuk_new (gfloat    x,
                  gfloat    y,
                  gfloat    rotation,
                  GrlColor *color,
                  gint      player_index)
{
	return g_object_new (DERBY_TYPE_TUKTUK,
	                     "x", x,
	                     "y", y,
	                     "rotation", rotation,
	                     "color", color,
	                     "player-index", player_index,
	                     NULL);
}

/* =============================================================================
 * DERBY_HAZARD TYPE
 * ========================================================================== */

struct _DerbyHazard
{
	GObject parent_instance;

	DerbyHazardType type;
	gfloat x, y;
	gfloat width, height;
	gfloat rotation;
	gfloat damage_per_second;
	gfloat effect_strength;
	gfloat health;  /* For destructible barriers */
	gboolean is_active;
};

enum
{
	PROP_HAZARD_0,
	PROP_HAZARD_TYPE,
	PROP_HAZARD_X,
	PROP_HAZARD_Y,
	PROP_HAZARD_WIDTH,
	PROP_HAZARD_HEIGHT,
	N_HAZARD_PROPS
};

static GParamSpec *hazard_properties[N_HAZARD_PROPS];

G_DEFINE_TYPE (DerbyHazard, derby_hazard, G_TYPE_OBJECT)

static void
derby_hazard_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	DerbyHazard *self = DERBY_HAZARD (object);

	switch (prop_id)
	{
	case PROP_HAZARD_TYPE:
		g_value_set_int (value, self->type);
		break;
	case PROP_HAZARD_X:
		g_value_set_float (value, self->x);
		break;
	case PROP_HAZARD_Y:
		g_value_set_float (value, self->y);
		break;
	case PROP_HAZARD_WIDTH:
		g_value_set_float (value, self->width);
		break;
	case PROP_HAZARD_HEIGHT:
		g_value_set_float (value, self->height);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_hazard_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	DerbyHazard *self = DERBY_HAZARD (object);

	switch (prop_id)
	{
	case PROP_HAZARD_TYPE:
		self->type = g_value_get_int (value);
		break;
	case PROP_HAZARD_X:
		self->x = g_value_get_float (value);
		break;
	case PROP_HAZARD_Y:
		self->y = g_value_get_float (value);
		break;
	case PROP_HAZARD_WIDTH:
		self->width = g_value_get_float (value);
		break;
	case PROP_HAZARD_HEIGHT:
		self->height = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_hazard_class_init (DerbyHazardClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = derby_hazard_get_property;
	object_class->set_property = derby_hazard_set_property;

	hazard_properties[PROP_HAZARD_TYPE] =
		g_param_spec_int ("type", "Type", "Hazard type",
		                  DERBY_HAZARD_NONE, DERBY_HAZARD_BARRIER, DERBY_HAZARD_NONE,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	hazard_properties[PROP_HAZARD_X] =
		g_param_spec_float ("x", "X", "X position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	hazard_properties[PROP_HAZARD_Y] =
		g_param_spec_float ("y", "Y", "Y position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	hazard_properties[PROP_HAZARD_WIDTH] =
		g_param_spec_float ("width", "Width", "Width",
		                    0.0f, G_MAXFLOAT, 5.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	hazard_properties[PROP_HAZARD_HEIGHT] =
		g_param_spec_float ("height", "Height", "Height",
		                    0.0f, G_MAXFLOAT, 5.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_HAZARD_PROPS, hazard_properties);
}

static void
derby_hazard_init (DerbyHazard *self)
{
	self->type = DERBY_HAZARD_NONE;
	self->x = 0.0f;
	self->y = 0.0f;
	self->width = 5.0f;
	self->height = 5.0f;
	self->rotation = 0.0f;
	self->damage_per_second = 0.0f;
	self->effect_strength = 1.0f;
	self->health = 200.0f;
	self->is_active = TRUE;
}

static DerbyHazard *
derby_hazard_new (DerbyHazardType type,
                  gfloat          x,
                  gfloat          y,
                  gfloat          width,
                  gfloat          height)
{
	DerbyHazard *hazard;

	hazard = g_object_new (DERBY_TYPE_HAZARD,
	                       "type", type,
	                       "x", x,
	                       "y", y,
	                       "width", width,
	                       "height", height,
	                       NULL);

	/* Set type-specific properties */
	switch (type)
	{
	case DERBY_HAZARD_OIL_SLICK:
		hazard->effect_strength = 0.3f;
		break;
	case DERBY_HAZARD_SPIKE_STRIP:
		hazard->damage_per_second = 10.0f;
		break;
	case DERBY_HAZARD_RAMP:
		hazard->effect_strength = 500.0f;
		break;
	case DERBY_HAZARD_FIRE_PIT:
		hazard->damage_per_second = 15.0f;
		break;
	case DERBY_HAZARD_BARRIER:
		hazard->health = 200.0f;
		break;
	default:
		break;
	}

	return hazard;
}

/* =============================================================================
 * DERBY_POWERUP TYPE
 * ========================================================================== */

struct _DerbyPowerUp
{
	GObject parent_instance;

	DerbyPowerUpType type;
	gfloat x, y;
	gfloat duration;
	gfloat respawn_timer;
	gboolean is_active;
};

enum
{
	PROP_POWERUP_0,
	PROP_POWERUP_TYPE,
	PROP_POWERUP_X,
	PROP_POWERUP_Y,
	N_POWERUP_PROPS
};

static GParamSpec *powerup_properties[N_POWERUP_PROPS];

G_DEFINE_TYPE (DerbyPowerUp, derby_powerup, G_TYPE_OBJECT)

static void
derby_powerup_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	DerbyPowerUp *self = DERBY_POWERUP (object);

	switch (prop_id)
	{
	case PROP_POWERUP_TYPE:
		g_value_set_int (value, self->type);
		break;
	case PROP_POWERUP_X:
		g_value_set_float (value, self->x);
		break;
	case PROP_POWERUP_Y:
		g_value_set_float (value, self->y);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_powerup_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	DerbyPowerUp *self = DERBY_POWERUP (object);

	switch (prop_id)
	{
	case PROP_POWERUP_TYPE:
		self->type = g_value_get_int (value);
		break;
	case PROP_POWERUP_X:
		self->x = g_value_get_float (value);
		break;
	case PROP_POWERUP_Y:
		self->y = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_powerup_class_init (DerbyPowerUpClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = derby_powerup_get_property;
	object_class->set_property = derby_powerup_set_property;

	powerup_properties[PROP_POWERUP_TYPE] =
		g_param_spec_int ("type", "Type", "Power-up type",
		                  DERBY_POWERUP_NONE, DERBY_POWERUP_ARMOR, DERBY_POWERUP_NONE,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	powerup_properties[PROP_POWERUP_X] =
		g_param_spec_float ("x", "X", "X position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	powerup_properties[PROP_POWERUP_Y] =
		g_param_spec_float ("y", "Y", "Y position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_POWERUP_PROPS, powerup_properties);
}

static void
derby_powerup_init (DerbyPowerUp *self)
{
	self->type = DERBY_POWERUP_NONE;
	self->x = 0.0f;
	self->y = 0.0f;
	self->duration = 5.0f;
	self->respawn_timer = 0.0f;
	self->is_active = FALSE;
}

static DerbyPowerUp *
derby_powerup_new (gfloat x,
                   gfloat y)
{
	DerbyPowerUp *powerup;

	powerup = g_object_new (DERBY_TYPE_POWERUP,
	                        "x", x,
	                        "y", y,
	                        NULL);
	powerup->is_active = TRUE;
	/* Random type */
	powerup->type = (g_random_int_range (1, 7));

	return powerup;
}

/* =============================================================================
 * DERBY_ARENA TYPE
 * ========================================================================== */

struct _DerbyArena
{
	GObject parent_instance;

	gfloat radius;
	gfloat ringout_radius;
	GPtrArray *hazards;
	GPtrArray *powerups;
	GArray *spawn_points;
};

enum
{
	PROP_ARENA_0,
	PROP_ARENA_RADIUS,
	N_ARENA_PROPS
};

static GParamSpec *arena_properties[N_ARENA_PROPS];

G_DEFINE_TYPE (DerbyArena, derby_arena, G_TYPE_OBJECT)

static void
derby_arena_finalize (GObject *object)
{
	DerbyArena *self = DERBY_ARENA (object);

	g_clear_pointer (&self->hazards, g_ptr_array_unref);
	g_clear_pointer (&self->powerups, g_ptr_array_unref);
	g_clear_pointer (&self->spawn_points, g_array_unref);

	G_OBJECT_CLASS (derby_arena_parent_class)->finalize (object);
}

static void
derby_arena_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	DerbyArena *self = DERBY_ARENA (object);

	switch (prop_id)
	{
	case PROP_ARENA_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_arena_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	DerbyArena *self = DERBY_ARENA (object);

	switch (prop_id)
	{
	case PROP_ARENA_RADIUS:
		self->radius = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_arena_class_init (DerbyArenaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = derby_arena_finalize;
	object_class->get_property = derby_arena_get_property;
	object_class->set_property = derby_arena_set_property;

	arena_properties[PROP_ARENA_RADIUS] =
		g_param_spec_float ("radius", "Radius", "Arena radius",
		                    0.0f, G_MAXFLOAT, ARENA_RADIUS,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_ARENA_PROPS, arena_properties);
}

static void
derby_arena_init (DerbyArena *self)
{
	self->radius = ARENA_RADIUS;
	self->ringout_radius = ARENA_RINGOUT_RADIUS;
	self->hazards = g_ptr_array_new_with_free_func (g_object_unref);
	self->powerups = g_ptr_array_new_with_free_func (g_object_unref);
	self->spawn_points = g_array_new (FALSE, FALSE, sizeof (GrlVector2));
}

typedef struct
{
	gfloat x;
	gfloat y;
} SpawnPoint;

static void
derby_arena_setup_default (DerbyArena *self)
{
	/* Spawn points spread across the large arena */
	SpawnPoint spawns[] = {
		/* Inner ring */
		{  0.0f,  60.0f },
		{  0.0f, -60.0f },
		{  60.0f,  0.0f },
		{ -60.0f,  0.0f },
		{  42.0f,  42.0f },
		{ -42.0f,  42.0f },
		{  42.0f, -42.0f },
		{ -42.0f, -42.0f },
		/* Middle ring */
		{  0.0f,  100.0f },
		{  0.0f, -100.0f },
		{  100.0f,  0.0f },
		{ -100.0f,  0.0f },
		{  70.0f,  70.0f },
		{ -70.0f,  70.0f },
		{  70.0f, -70.0f },
		{ -70.0f, -70.0f },
		/* Outer ring */
		{  0.0f,  140.0f },
		{  0.0f, -140.0f },
		{  140.0f,  0.0f },
		{ -140.0f,  0.0f },
		{  100.0f,  100.0f },
		{ -100.0f,  100.0f },
		{  100.0f, -100.0f },
		{ -100.0f, -100.0f }
	};
	guint i;
	guint num_spawns;

	num_spawns = G_N_ELEMENTS (spawns);

	/* Add spawn points */
	for (i = 0; i < num_spawns; i++)
	{
		GrlVector2 spawn;
		spawn.x = spawns[i].x;
		spawn.y = spawns[i].y;
		g_array_append_val (self->spawn_points, spawn);
	}

	/* Add hazards - lots of chaos across the big arena! */
	/* Oil slicks - slippery zones (inner) */
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, 50.0f, 50.0f, 18.0f, 18.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, -50.0f, -50.0f, 18.0f, 18.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, 50.0f, -50.0f, 18.0f, 18.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, -50.0f, 50.0f, 18.0f, 18.0f));
	/* Oil slicks - outer ring */
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, 120.0f, 0.0f, 15.0f, 15.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, -120.0f, 0.0f, 15.0f, 15.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, 0.0f, 120.0f, 15.0f, 15.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_OIL_SLICK, 0.0f, -120.0f, 15.0f, 15.0f));

	/* Spike strips - damage zones */
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_SPIKE_STRIP, -80.0f, 80.0f, 20.0f, 5.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_SPIKE_STRIP, 80.0f, -80.0f, 20.0f, 5.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_SPIKE_STRIP, 80.0f, 80.0f, 20.0f, 5.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_SPIKE_STRIP, -80.0f, -80.0f, 20.0f, 5.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_SPIKE_STRIP, 0.0f, 100.0f, 25.0f, 5.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_SPIKE_STRIP, 0.0f, -100.0f, 25.0f, 5.0f));

	/* Ramps - speed boosts */
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_RAMP, 0.0f, 0.0f, 15.0f, 25.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_RAMP, 90.0f, 90.0f, 12.0f, 18.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_RAMP, -90.0f, -90.0f, 12.0f, 18.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_RAMP, 90.0f, -90.0f, 12.0f, 18.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_RAMP, -90.0f, 90.0f, 12.0f, 18.0f));

	/* Fire pits - danger zones (scattered) */
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_FIRE_PIT, 100.0f, 0.0f, 15.0f, 15.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_FIRE_PIT, -100.0f, 0.0f, 15.0f, 15.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_FIRE_PIT, 0.0f, 100.0f, 15.0f, 15.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_FIRE_PIT, 0.0f, -100.0f, 15.0f, 15.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_FIRE_PIT, 130.0f, 50.0f, 12.0f, 12.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_FIRE_PIT, -130.0f, -50.0f, 12.0f, 12.0f));

	/* Barriers - solid obstacles (inner cross) */
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, -70.0f, 0.0f, 8.0f, 35.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, 70.0f, 0.0f, 8.0f, 35.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, 0.0f, 70.0f, 35.0f, 8.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, 0.0f, -70.0f, 35.0f, 8.0f));
	/* Barriers - outer posts */
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, 130.0f, 130.0f, 10.0f, 10.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, -130.0f, 130.0f, 10.0f, 10.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, 130.0f, -130.0f, 10.0f, 10.0f));
	g_ptr_array_add (self->hazards,
	                 derby_hazard_new (DERBY_HAZARD_BARRIER, -130.0f, -130.0f, 10.0f, 10.0f));

	/* Add power-up spawn locations - spread across larger arena */
	g_ptr_array_add (self->powerups, derby_powerup_new (30.0f, 30.0f));
	g_ptr_array_add (self->powerups, derby_powerup_new (-30.0f, 30.0f));
	g_ptr_array_add (self->powerups, derby_powerup_new (30.0f, -30.0f));
	g_ptr_array_add (self->powerups, derby_powerup_new (-30.0f, -30.0f));
	g_ptr_array_add (self->powerups, derby_powerup_new (0.0f, 60.0f));
	g_ptr_array_add (self->powerups, derby_powerup_new (0.0f, -60.0f));
	g_ptr_array_add (self->powerups, derby_powerup_new (60.0f, 0.0f));
	g_ptr_array_add (self->powerups, derby_powerup_new (-60.0f, 0.0f));
}

static DerbyArena *
derby_arena_new (void)
{
	DerbyArena *arena;

	arena = g_object_new (DERBY_TYPE_ARENA, NULL);
	derby_arena_setup_default (arena);

	return arena;
}

/* =============================================================================
 * DERBY_GAME TYPE
 * ========================================================================== */

struct _DerbyGame
{
	GObject parent_instance;

	/* Core game objects */
	DerbyArena *arena;
	DerbyTukTuk *player;
	GPtrArray *tuktuks;  /* All tuktuks including player */

	/* Particle system */
	DerbyParticle particles[MAX_PARTICLES];

	/* Game state */
	DerbyState state;
	DerbyGameMode mode;
	gfloat countdown_timer;
	gfloat match_time;
	gfloat results_timer;

	/* Match settings */
	gint opponent_count;
	gint current_round;
	gint total_rounds;

	/* Survival mode */
	gfloat wave_timer;
	gint wave_number;

	/* Menu selection */
	gint menu_selection;
	gint menu_opponent_count;
};

enum
{
	PROP_GAME_0,
	PROP_GAME_STATE,
	PROP_GAME_MODE,
	N_GAME_PROPS
};

static GParamSpec *game_properties[N_GAME_PROPS];

G_DEFINE_TYPE (DerbyGame, derby_game, G_TYPE_OBJECT)

static void
derby_game_finalize (GObject *object)
{
	DerbyGame *self = DERBY_GAME (object);

	g_clear_object (&self->arena);
	g_clear_pointer (&self->tuktuks, g_ptr_array_unref);

	G_OBJECT_CLASS (derby_game_parent_class)->finalize (object);
}

static void
derby_game_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	DerbyGame *self = DERBY_GAME (object);

	switch (prop_id)
	{
	case PROP_GAME_STATE:
		g_value_set_int (value, self->state);
		break;
	case PROP_GAME_MODE:
		g_value_set_int (value, self->mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_game_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	DerbyGame *self = DERBY_GAME (object);

	switch (prop_id)
	{
	case PROP_GAME_STATE:
		self->state = g_value_get_int (value);
		break;
	case PROP_GAME_MODE:
		self->mode = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
derby_game_class_init (DerbyGameClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = derby_game_finalize;
	object_class->get_property = derby_game_get_property;
	object_class->set_property = derby_game_set_property;

	game_properties[PROP_GAME_STATE] =
		g_param_spec_int ("state", "State", "Game state",
		                  DERBY_STATE_MENU, DERBY_STATE_RESULTS, DERBY_STATE_MENU,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	game_properties[PROP_GAME_MODE] =
		g_param_spec_int ("mode", "Mode", "Game mode",
		                  DERBY_GAME_MODE_QUICK_MATCH, DERBY_GAME_MODE_SURVIVAL,
		                  DERBY_GAME_MODE_QUICK_MATCH,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_GAME_PROPS, game_properties);
}

static void
derby_game_init (DerbyGame *self)
{
	guint i;

	self->arena = NULL;
	self->player = NULL;
	self->tuktuks = g_ptr_array_new_with_free_func (g_object_unref);

	/* Initialize particles */
	for (i = 0; i < MAX_PARTICLES; i++)
		self->particles[i].active = FALSE;

	self->state = DERBY_STATE_MENU;
	self->mode = DERBY_GAME_MODE_QUICK_MATCH;
	self->countdown_timer = 0.0f;
	self->match_time = 0.0f;
	self->results_timer = 0.0f;

	self->opponent_count = 3;
	self->current_round = 1;
	self->total_rounds = 1;

	self->wave_timer = 0.0f;
	self->wave_number = 0;

	self->menu_selection = 0;
	self->menu_opponent_count = 3;
}

/* =============================================================================
 * PARTICLE SYSTEM
 * ========================================================================== */

static void
spawn_particles (DerbyGame *game,
                 gfloat     x,
                 gfloat     y,
                 gint       count,
                 guint8     r,
                 guint8     g,
                 guint8     b)
{
	guint i;
	gint spawned;

	spawned = 0;
	for (i = 0; i < MAX_PARTICLES && spawned < count; i++)
	{
		if (!game->particles[i].active)
		{
			DerbyParticle *p = &game->particles[i];
			gfloat angle = g_random_double () * 2.0 * G_PI;
			gfloat speed = PARTICLE_SPEED * (0.5f + g_random_double () * 0.5f);

			p->x = x;
			p->y = y;
			p->vx = cosf (angle) * speed;
			p->vy = sinf (angle) * speed;
			p->life = PARTICLE_LIFETIME * (0.7f + g_random_double () * 0.3f);
			p->max_life = p->life;
			p->size = 2.0f + g_random_double () * 3.0f;
			p->r = r;
			p->g = g;
			p->b = b;
			p->active = TRUE;
			spawned++;
		}
	}
}

static void
spawn_explosion (DerbyGame *game,
                 gfloat     x,
                 gfloat     y,
                 gboolean   big)
{
	/* Orange/yellow core */
	spawn_particles (game, x, y, big ? 20 : 8, 255, 200, 50);
	/* Red outer */
	spawn_particles (game, x, y, big ? 15 : 5, 255, 100, 50);
	/* White sparks */
	spawn_particles (game, x, y, big ? 10 : 3, 255, 255, 255);
}

static void
spawn_impact_sparks (DerbyGame *game,
                     gfloat     x,
                     gfloat     y,
                     gfloat     intensity)
{
	gint count = (gint)(intensity * 0.5f);
	if (count < 3) count = 3;
	if (count > 15) count = 15;

	/* Yellow/orange sparks */
	spawn_particles (game, x, y, count, 255, 220, 100);
}

static void
update_particles (DerbyGame *game,
                  gfloat     delta)
{
	guint i;

	for (i = 0; i < MAX_PARTICLES; i++)
	{
		DerbyParticle *p = &game->particles[i];
		if (!p->active)
			continue;

		/* Update position */
		p->x += p->vx * delta;
		p->y += p->vy * delta;

		/* Apply gravity */
		p->vy += PARTICLE_GRAVITY * delta;

		/* Friction */
		p->vx *= 0.98f;
		p->vy *= 0.98f;

		/* Update lifetime */
		p->life -= delta;
		if (p->life <= 0.0f)
			p->active = FALSE;
	}
}

/* =============================================================================
 * VEHICLE COLORS
 * ========================================================================== */

static GrlColor *
get_tuktuk_color (gint index)
{
	/* Predefined colors for tuktuks */
	switch (index)
	{
	case 0:  return grl_color_new (255, 220, 50, 255);   /* Yellow - Player */
	case 1:  return grl_color_new (255, 50, 50, 255);    /* Red */
	case 2:  return grl_color_new (50, 100, 255, 255);   /* Blue */
	case 3:  return grl_color_new (50, 200, 50, 255);    /* Green */
	case 4:  return grl_color_new (180, 50, 255, 255);   /* Purple */
	case 5:  return grl_color_new (255, 150, 50, 255);   /* Orange */
	case 6:  return grl_color_new (50, 220, 220, 255);   /* Cyan */
	case 7:  return grl_color_new (255, 100, 180, 255);  /* Pink */
	case 8:  return grl_color_new (240, 240, 240, 255);  /* White */
	case 9:  return grl_color_new (180, 255, 50, 255);   /* Lime */
	default: return grl_color_new (150, 150, 150, 255);  /* Gray */
	}
}

/* =============================================================================
 * GAME LOGIC FUNCTIONS
 * ========================================================================== */

static gfloat
get_combo_multiplier (gint combo_count)
{
	if (combo_count >= 10)
		return 5.0f;
	else if (combo_count >= 5)
		return 3.0f;
	else if (combo_count >= 3)
		return 2.0f;
	else if (combo_count >= 2)
		return 1.5f;
	return 1.0f;
}

static void
derby_tuktuk_take_damage (DerbyTukTuk *self,
                          gfloat       damage,
                          DerbyTukTuk *attacker)
{
	gfloat actual_damage;
	gfloat multiplier;
	gint   points;

	if (self->is_destroyed)
		return;

	/* Apply shield */
	if (self->active_powerup == DERBY_POWERUP_SHIELD)
		return;

	/* Apply armor reduction */
	actual_damage = damage * self->damage_multiplier;
	if (self->active_powerup == DERBY_POWERUP_ARMOR)
		actual_damage *= 0.5f;

	self->health -= actual_damage;

	/* Award points to attacker */
	if (attacker != NULL && attacker != self)
	{
		/* Update combo */
		if (attacker->combo_timer > 0.0f)
			attacker->combo_count++;
		else
			attacker->combo_count = 1;
		attacker->combo_timer = COMBO_WINDOW;

		/* Calculate points */
		multiplier = get_combo_multiplier (attacker->combo_count);
		points = (gint)(actual_damage * POINTS_PER_DAMAGE * multiplier);
		attacker->score += points;
		attacker->damage_dealt += (gint)actual_damage;
	}

	/* Check for destruction */
	if (self->health <= 0.0f)
	{
		self->health = 0.0f;
		self->is_destroyed = TRUE;
		self->needs_explosion = TRUE;

		if (attacker != NULL && attacker != self)
		{
			multiplier = get_combo_multiplier (attacker->combo_count);
			attacker->score += (gint)(KNOCKOUT_BONUS * multiplier);
			attacker->knockouts++;
		}
	}
}

static void
derby_tuktuk_apply_powerup (DerbyTukTuk      *self,
                            DerbyPowerUpType  type)
{
	/* Remove existing power-up effect */
	if (self->active_powerup == DERBY_POWERUP_SPEED_BOOST)
		self->max_speed = DEFAULT_MAX_SPEED;
	if (self->active_powerup == DERBY_POWERUP_RAM_DAMAGE)
		self->ram_damage = DEFAULT_RAM_DAMAGE;

	/* Apply new power-up */
	switch (type)
	{
	case DERBY_POWERUP_SPEED_BOOST:
		self->max_speed = DEFAULT_MAX_SPEED * 1.5f;
		self->powerup_time = 5.0f;
		break;
	case DERBY_POWERUP_RAM_DAMAGE:
		self->ram_damage = DEFAULT_RAM_DAMAGE * 2.0f;
		self->powerup_time = 5.0f;
		break;
	case DERBY_POWERUP_NITRO:
		/* Instant burst */
		self->velocity_x += cosf (self->rotation) * 20.0f;
		self->velocity_y += sinf (self->rotation) * 20.0f;
		self->powerup_time = 0.0f;
		type = DERBY_POWERUP_NONE;
		break;
	case DERBY_POWERUP_SHIELD:
		self->powerup_time = 4.0f;
		break;
	case DERBY_POWERUP_REPAIR:
		self->health = fminf (self->health + self->max_health * 0.5f, self->max_health);
		self->powerup_time = 0.0f;
		type = DERBY_POWERUP_NONE;
		break;
	case DERBY_POWERUP_ARMOR:
		self->powerup_time = 6.0f;
		break;
	default:
		break;
	}

	self->active_powerup = type;
}

static void
derby_tuktuk_activate_boost (DerbyTukTuk *self)
{
	gfloat forward_x, forward_y;
	gfloat speed;

	if (self->is_destroyed || self->is_boosting)
		return;

	if (self->boost_charge < BOOST_COST)
		return;

	/* Consume boost charge */
	self->boost_charge -= BOOST_COST;
	self->is_boosting = TRUE;
	self->boost_timer = BOOST_DURATION;

	/* Apply instant velocity boost in facing direction */
	forward_x = cosf (self->rotation);
	forward_y = sinf (self->rotation);

	speed = sqrtf (self->velocity_x * self->velocity_x +
	               self->velocity_y * self->velocity_y);

	/* Boost to high speed instantly */
	self->velocity_x = forward_x * fmaxf (speed, self->max_speed) * BOOST_MULTIPLIER;
	self->velocity_y = forward_y * fmaxf (speed, self->max_speed) * BOOST_MULTIPLIER;
}

static void
derby_tuktuk_update (DerbyTukTuk     *self,
                     gfloat           accel_input,
                     gfloat           steer_input,
                     gfloat           delta)
{
	gfloat forward_x, forward_y;
	gfloat speed;
	gfloat friction;
	gfloat turn_factor;

	if (self->is_destroyed)
		return;

	/* Calculate current speed first */
	speed = sqrtf (self->velocity_x * self->velocity_x +
	               self->velocity_y * self->velocity_y);

	/* Steering - only works when moving (no tank controls) */
	/* Turn rate scales with speed, max at half max_speed */
	turn_factor = fminf (speed / (self->max_speed * 0.5f), 1.0f);
	if (fabsf (steer_input) > 0.1f && speed > 0.5f)
	{
		self->rotation += steer_input * self->turn_rate * turn_factor * delta;

		/* Also rotate velocity vector to follow steering (car-like behavior) */
		if (speed > 0.1f)
		{
			gfloat vel_angle = atan2f (self->velocity_y, self->velocity_x);
			gfloat turn_amount = steer_input * self->turn_rate * turn_factor * delta * 0.5f;

			/* Gradually align velocity with facing direction */
			self->velocity_x = cosf (vel_angle + turn_amount) * speed;
			self->velocity_y = sinf (vel_angle + turn_amount) * speed;
		}
	}

	/* Forward vector */
	forward_x = cosf (self->rotation);
	forward_y = sinf (self->rotation);

	/* Acceleration */
	if (fabsf (accel_input) > 0.1f)
	{
		self->velocity_x += forward_x * accel_input * self->acceleration * delta;
		self->velocity_y += forward_y * accel_input * self->acceleration * delta;
	}

	/* Recalculate speed after acceleration */
	speed = sqrtf (self->velocity_x * self->velocity_x +
	               self->velocity_y * self->velocity_y);

	/* Speed limit */
	if (speed > self->max_speed)
	{
		self->velocity_x = (self->velocity_x / speed) * self->max_speed;
		self->velocity_y = (self->velocity_y / speed) * self->max_speed;
	}

	/* Friction */
	friction = 0.98f;
	self->velocity_x *= friction;
	self->velocity_y *= friction;

	/* Boost timer update */
	if (self->is_boosting)
	{
		self->boost_timer -= delta;
		if (self->boost_timer <= 0.0f)
			self->is_boosting = FALSE;
	}

	/* Recharge boost when not boosting */
	if (!self->is_boosting && self->boost_charge < BOOST_MAX_CHARGE)
	{
		self->boost_charge += BOOST_RECHARGE_RATE * delta;
		if (self->boost_charge > BOOST_MAX_CHARGE)
			self->boost_charge = BOOST_MAX_CHARGE;
	}

	/* Update position */
	self->x += self->velocity_x * delta;
	self->y += self->velocity_y * delta;

	/* Update combo timer */
	if (self->combo_timer > 0.0f)
	{
		self->combo_timer -= delta;
		if (self->combo_timer <= 0.0f)
			self->combo_count = 0;
	}

	/* Update power-up timer */
	if (self->powerup_time > 0.0f)
	{
		self->powerup_time -= delta;
		if (self->powerup_time <= 0.0f)
		{
			/* Remove power-up effect */
			if (self->active_powerup == DERBY_POWERUP_SPEED_BOOST)
				self->max_speed = DEFAULT_MAX_SPEED;
			if (self->active_powerup == DERBY_POWERUP_RAM_DAMAGE)
				self->ram_damage = DEFAULT_RAM_DAMAGE;
			self->active_powerup = DERBY_POWERUP_NONE;
		}
	}
}

static void
derby_tuktuk_ai_update (DerbyTukTuk *self,
                        DerbyGame   *game,
                        gfloat       delta)
{
	DerbyTukTuk *target;
	gfloat dx, dy;
	gfloat dist;
	gfloat angle_to_target;
	gfloat angle_diff;
	guint i;

	if (self->is_destroyed || self->player_index >= 0)
		return;

	/* AI thinking delay - only recalculate decisions periodically */
	self->ai_think_timer -= delta;
	if (self->ai_think_timer <= 0.0f)
	{
		self->ai_think_timer = AI_REACTION_TIME;

		/* Find target (nearest non-destroyed tuktuk) */
		target = NULL;
		dist = G_MAXFLOAT;

		for (i = 0; i < game->tuktuks->len; i++)
		{
			DerbyTukTuk *other;
			gfloat other_dist;

			other = g_ptr_array_index (game->tuktuks, i);
			if (other == self || other->is_destroyed)
				continue;

			dx = other->x - self->x;
			dy = other->y - self->y;
			other_dist = sqrtf (dx * dx + dy * dy);

			/* Slight preference for low-health targets, no player preference */
			/* AI should target each other, not just the player */
			if (other->health < other->max_health * 0.4f)
				other_dist *= 0.8f;

			if (other_dist < dist)
			{
				dist = other_dist;
				target = other;
			}
		}

		if (target != NULL)
		{
			gfloat target_facing;
			gfloat side_offset;
			gfloat aim_x, aim_y;

			/* Calculate target's side position for flanking attack */
			/* Aim perpendicular to target's facing direction */
			target_facing = target->rotation;

			/* Determine which side is closer to approach */
			dx = target->x - self->x;
			dy = target->y - self->y;
			angle_to_target = atan2f (dy, dx);

			/* Check if we should go for left or right side */
			side_offset = angle_to_target - target_facing;
			while (side_offset > G_PI) side_offset -= 2.0f * G_PI;
			while (side_offset < -G_PI) side_offset += 2.0f * G_PI;

			/* Aim for the side - offset 90 degrees from target's facing */
			if (side_offset > 0)
			{
				/* Approach from target's right side */
				aim_x = target->x + cosf (target_facing + G_PI / 2.0f) * 8.0f;
				aim_y = target->y + sinf (target_facing + G_PI / 2.0f) * 8.0f;
			}
			else
			{
				/* Approach from target's left side */
				aim_x = target->x + cosf (target_facing - G_PI / 2.0f) * 8.0f;
				aim_y = target->y + sinf (target_facing - G_PI / 2.0f) * 8.0f;
			}

			/* When very close, aim directly at target for the hit */
			if (dist < 15.0f)
			{
				aim_x = target->x;
				aim_y = target->y;
			}

			/* Calculate steering toward aim point */
			dx = aim_x - self->x;
			dy = aim_y - self->y;
			angle_to_target = atan2f (dy, dx);
			angle_diff = angle_to_target - self->rotation;

			/* Normalize angle difference */
			while (angle_diff > G_PI)
				angle_diff -= 2.0f * G_PI;
			while (angle_diff < -G_PI)
				angle_diff += 2.0f * G_PI;

			/* Set input based on angle - smoother steering */
			self->ai_steer_input = fmaxf (-1.0f, fminf (1.0f, angle_diff * 2.0f));

			/* Always accelerate */
			self->ai_accel_input = 1.0f;

			/* Low health: try to flee */
			if (self->health < self->max_health * 0.25f)
			{
				self->ai_steer_input = -self->ai_steer_input;
			}

			/* AI boost when aimed at target and close */
			if (fabsf (angle_diff) < 0.3f && dist < 30.0f &&
			    self->boost_charge >= BOOST_COST)
			{
				derby_tuktuk_activate_boost (self);
			}
		}
	}

	/* ALWAYS update movement with stored AI inputs */
	derby_tuktuk_update (self, self->ai_accel_input, self->ai_steer_input, delta);
}

/*
 * get_hit_zone_multiplier:
 * @tuktuk: the tuktuk being hit
 * @hit_angle: angle of incoming hit (direction FROM attacker TO target)
 *
 * Returns damage multiplier based on which part of the tuktuk was hit.
 * Front (armored) = low damage, Side = high damage, Rear = normal
 */
static gfloat
get_hit_zone_multiplier (DerbyTukTuk *tuktuk,
                         gfloat       hit_angle)
{
	gfloat facing_angle;
	gfloat relative_angle;

	/* Get tuktuk's facing direction */
	facing_angle = tuktuk->rotation;

	/* Calculate angle of hit relative to facing direction */
	/* hit_angle points FROM attacker TO this tuktuk */
	/* So we want angle between hit direction and our facing */
	relative_angle = hit_angle - facing_angle;

	/* Normalize to -PI to PI */
	while (relative_angle > G_PI)
		relative_angle -= 2.0f * G_PI;
	while (relative_angle < -G_PI)
		relative_angle += 2.0f * G_PI;

	relative_angle = fabsf (relative_angle);

	/* Front hit: attacker is in front of us (hitting our front) */
	/* This means hit comes from direction we're facing */
	if (relative_angle < FRONT_ANGLE)
		return FRONT_ARMOR_MULT;  /* Armored front */

	/* Rear hit: attacker is behind us */
	if (relative_angle > SIDE_ANGLE)
		return REAR_DAMAGE_MULT;  /* Normal rear damage */

	/* Side hit: most vulnerable */
	return SIDE_DAMAGE_MULT;
}

static gboolean
check_circle_collision (gfloat x1, gfloat y1, gfloat r1,
                        gfloat x2, gfloat y2, gfloat r2)
{
	gfloat dx, dy;
	gfloat dist_sq;
	gfloat radii;

	dx = x2 - x1;
	dy = y2 - y1;
	dist_sq = dx * dx + dy * dy;
	radii = r1 + r2;

	return dist_sq < (radii * radii);
}

static void
resolve_tuktuk_collision (DerbyTukTuk *a,
                          DerbyTukTuk *b,
                          DerbyGame   *game)
{
	gfloat dx, dy;
	gfloat dist;
	gfloat nx, ny;
	gfloat rel_vel_x, rel_vel_y;
	gfloat rel_vel_normal;
	gfloat impulse;
	gfloat overlap;
	gfloat damage;
	gfloat speed_a, speed_b;
	gfloat impact_x, impact_y;

	dx = b->x - a->x;
	dy = b->y - a->y;
	dist = sqrtf (dx * dx + dy * dy);

	if (dist < 0.001f)
		dist = 0.001f;

	/* Normal vector */
	nx = dx / dist;
	ny = dy / dist;

	/* Relative velocity */
	rel_vel_x = a->velocity_x - b->velocity_x;
	rel_vel_y = a->velocity_y - b->velocity_y;
	rel_vel_normal = rel_vel_x * nx + rel_vel_y * ny;

	/* Only resolve if moving toward each other */
	if (rel_vel_normal < 0.0f)
		return;

	/* Calculate speeds before collision for damage */
	speed_a = sqrtf (a->velocity_x * a->velocity_x + a->velocity_y * a->velocity_y);
	speed_b = sqrtf (b->velocity_x * b->velocity_x + b->velocity_y * b->velocity_y);

	/* Impact point (midpoint between the two) */
	impact_x = (a->x + b->x) * 0.5f;
	impact_y = (a->y + b->y) * 0.5f;

	/* Spawn impact sparks based on collision intensity */
	spawn_impact_sparks (game, impact_x, impact_y, rel_vel_normal);

	/* Bouncy impulse magnitude - extra bounce! */
	impulse = (2.0f * rel_vel_normal * COLLISION_BOUNCE) / (a->mass + b->mass);

	/* Apply bouncy impulse */
	a->velocity_x -= impulse * b->mass * nx;
	a->velocity_y -= impulse * b->mass * ny;
	b->velocity_x += impulse * a->mass * nx;
	b->velocity_y += impulse * a->mass * ny;

	/* Separate overlapping tuktuks */
	overlap = (TUKTUK_COLLISION_RADIUS * 2.0f) - dist;
	if (overlap > 0.0f)
	{
		a->x -= nx * overlap * 0.5f;
		a->y -= ny * overlap * 0.5f;
		b->x += nx * overlap * 0.5f;
		b->y += ny * overlap * 0.5f;
	}

	/* Calculate damage: base + speed bonus */
	/* Speed-based damage: faster you're going, more damage you deal */
	damage = fabsf (rel_vel_normal) * 1.5f;

	/* Apply damage with attacker credit based on who was faster */
	if (fabsf (rel_vel_normal) > 1.0f)
	{
		gfloat damage_a, damage_b;
		gfloat hit_angle_to_a, hit_angle_to_b;
		gfloat zone_mult_a, zone_mult_b;

		/* Calculate hit angles - direction of impact for each tuktuk */
		/* For A: hit comes from direction of B */
		hit_angle_to_a = atan2f (a->y - b->y, a->x - b->x);
		/* For B: hit comes from direction of A */
		hit_angle_to_b = atan2f (b->y - a->y, b->x - a->x);

		/* Get damage multipliers based on which part was hit */
		zone_mult_a = get_hit_zone_multiplier (a, hit_angle_to_a);
		zone_mult_b = get_hit_zone_multiplier (b, hit_angle_to_b);

		/* Damage scales with attacker's speed */
		damage_a = damage + (speed_a * SPEED_DAMAGE_SCALE);
		damage_b = damage + (speed_b * SPEED_DAMAGE_SCALE);

		/* Boost bonus damage */
		if (a->is_boosting)
			damage_a *= 2.0f;
		if (b->is_boosting)
			damage_b *= 2.0f;

		/* Apply hit zone multipliers - where you HIT matters */
		damage_a *= zone_mult_b;  /* A's damage affected by where A hit B */
		damage_b *= zone_mult_a;  /* B's damage affected by where B hit A */

		if (speed_a > speed_b)
		{
			derby_tuktuk_take_damage (b, damage_a * a->ram_damage / DEFAULT_RAM_DAMAGE, a);
			derby_tuktuk_take_damage (a, damage * 0.2f * zone_mult_a, NULL);
		}
		else
		{
			derby_tuktuk_take_damage (a, damage_b * b->ram_damage / DEFAULT_RAM_DAMAGE, b);
			derby_tuktuk_take_damage (b, damage * 0.2f * zone_mult_b, NULL);
		}
	}
}

static gboolean
check_rect_collision (gfloat cx, gfloat cy, gfloat radius,
                      gfloat rx, gfloat ry, gfloat rw, gfloat rh)
{
	gfloat closest_x, closest_y;
	gfloat dx, dy;
	gfloat dist_sq;

	/* Find closest point on rectangle to circle center */
	closest_x = fmaxf (rx - rw * 0.5f, fminf (cx, rx + rw * 0.5f));
	closest_y = fmaxf (ry - rh * 0.5f, fminf (cy, ry + rh * 0.5f));

	dx = cx - closest_x;
	dy = cy - closest_y;
	dist_sq = dx * dx + dy * dy;

	return dist_sq < (radius * radius);
}

static void
derby_game_check_collisions (DerbyGame *game,
                             gfloat     delta)
{
	guint i, j;

	/* TukTuk vs TukTuk collisions */
	for (i = 0; i < game->tuktuks->len; i++)
	{
		DerbyTukTuk *a;

		a = g_ptr_array_index (game->tuktuks, i);
		if (a->is_destroyed)
			continue;

		for (j = i + 1; j < game->tuktuks->len; j++)
		{
			DerbyTukTuk *b;

			b = g_ptr_array_index (game->tuktuks, j);
			if (b->is_destroyed)
				continue;

			if (check_circle_collision (a->x, a->y, TUKTUK_COLLISION_RADIUS,
			                            b->x, b->y, TUKTUK_COLLISION_RADIUS))
			{
				resolve_tuktuk_collision (a, b, game);
			}
		}
	}

	/* TukTuk vs Hazard collisions */
	for (i = 0; i < game->tuktuks->len; i++)
	{
		DerbyTukTuk *tuktuk;

		tuktuk = g_ptr_array_index (game->tuktuks, i);
		if (tuktuk->is_destroyed)
			continue;

		for (j = 0; j < game->arena->hazards->len; j++)
		{
			DerbyHazard *hazard;

			hazard = g_ptr_array_index (game->arena->hazards, j);
			if (!hazard->is_active)
				continue;

			if (check_rect_collision (tuktuk->x, tuktuk->y, TUKTUK_COLLISION_RADIUS,
			                          hazard->x, hazard->y, hazard->width, hazard->height))
			{
				switch (hazard->type)
				{
				case DERBY_HAZARD_OIL_SLICK:
					/* Reduce turning */
					tuktuk->velocity_x *= 0.99f;
					tuktuk->velocity_y *= 0.99f;
					break;
				case DERBY_HAZARD_SPIKE_STRIP:
				case DERBY_HAZARD_FIRE_PIT:
					derby_tuktuk_take_damage (tuktuk, hazard->damage_per_second * delta, NULL);
					break;
				case DERBY_HAZARD_RAMP:
					/* Boost forward */
					tuktuk->velocity_x += cosf (tuktuk->rotation) * 0.5f;
					tuktuk->velocity_y += sinf (tuktuk->rotation) * 0.5f;
					break;
				case DERBY_HAZARD_BARRIER:
					/* Push back */
					{
						gfloat dx, dy, dist;

						dx = tuktuk->x - hazard->x;
						dy = tuktuk->y - hazard->y;
						dist = sqrtf (dx * dx + dy * dy);
						if (dist > 0.001f)
						{
							tuktuk->velocity_x = (dx / dist) * 5.0f;
							tuktuk->velocity_y = (dy / dist) * 5.0f;
						}
					}
					break;
				default:
					break;
				}
			}
		}

		/* Check ring-out */
		{
			gfloat dist_from_center;

			dist_from_center = sqrtf (tuktuk->x * tuktuk->x + tuktuk->y * tuktuk->y);
			if (dist_from_center > game->arena->ringout_radius)
			{
				/* Find who pushed them */
				DerbyTukTuk *pusher = NULL;
				gfloat min_dist = G_MAXFLOAT;

				for (j = 0; j < game->tuktuks->len; j++)
				{
					DerbyTukTuk *other;
					gfloat dx, dy, d;

					other = g_ptr_array_index (game->tuktuks, j);
					if (other == tuktuk || other->is_destroyed)
						continue;

					dx = other->x - tuktuk->x;
					dy = other->y - tuktuk->y;
					d = sqrtf (dx * dx + dy * dy);
					if (d < min_dist && d < 10.0f)
					{
						min_dist = d;
						pusher = other;
					}
				}

				tuktuk->is_destroyed = TRUE;
				tuktuk->health = 0.0f;

				if (pusher != NULL)
				{
					pusher->score += RINGOUT_BONUS;
					pusher->knockouts++;
				}
			}
		}
	}

	/* TukTuk vs PowerUp collisions */
	for (i = 0; i < game->tuktuks->len; i++)
	{
		DerbyTukTuk *tuktuk;

		tuktuk = g_ptr_array_index (game->tuktuks, i);
		if (tuktuk->is_destroyed)
			continue;

		for (j = 0; j < game->arena->powerups->len; j++)
		{
			DerbyPowerUp *powerup;

			powerup = g_ptr_array_index (game->arena->powerups, j);
			if (!powerup->is_active)
				continue;

			if (check_circle_collision (tuktuk->x, tuktuk->y, TUKTUK_COLLISION_RADIUS,
			                            powerup->x, powerup->y, POWERUP_RADIUS))
			{
				derby_tuktuk_apply_powerup (tuktuk, powerup->type);
				powerup->is_active = FALSE;
				powerup->respawn_timer = POWERUP_RESPAWN_TIME;
			}
		}
	}
}

static void
derby_game_update_powerups (DerbyGame *game,
                            gfloat     delta)
{
	guint i;

	for (i = 0; i < game->arena->powerups->len; i++)
	{
		DerbyPowerUp *powerup;

		powerup = g_ptr_array_index (game->arena->powerups, i);

		if (!powerup->is_active)
		{
			powerup->respawn_timer -= delta;
			if (powerup->respawn_timer <= 0.0f)
			{
				powerup->is_active = TRUE;
				powerup->type = g_random_int_range (1, 7);
			}
		}
	}
}

static gint
derby_game_count_alive (DerbyGame *game)
{
	gint alive;
	guint i;

	alive = 0;
	for (i = 0; i < game->tuktuks->len; i++)
	{
		DerbyTukTuk *tuktuk;

		tuktuk = g_ptr_array_index (game->tuktuks, i);
		if (!tuktuk->is_destroyed)
			alive++;
	}

	return alive;
}

static void
derby_game_start_match (DerbyGame     *game,
                        DerbyGameMode  mode,
                        gint           opponent_count)
{
	guint i;
	g_autoptr(GrlColor) player_color = NULL;

	/* Clear existing tuktuks */
	g_ptr_array_set_size (game->tuktuks, 0);

	/* Create arena if needed */
	if (game->arena == NULL)
		game->arena = derby_arena_new ();

	/* Reset powerups */
	for (i = 0; i < game->arena->powerups->len; i++)
	{
		DerbyPowerUp *powerup;

		powerup = g_ptr_array_index (game->arena->powerups, i);
		powerup->is_active = TRUE;
		powerup->type = g_random_int_range (1, 7);
	}

	/* Create player tuktuk - spawn at first spawn point */
	player_color = get_tuktuk_color (0);
	game->player = derby_tuktuk_new (0.0f, 80.0f, -G_PI / 2.0f, player_color, 0);
	g_ptr_array_add (game->tuktuks, g_object_ref (game->player));

	/* Create AI tuktuks */
	for (i = 0; i < (guint)opponent_count; i++)
	{
		DerbyTukTuk *ai;
		GrlVector2 *spawn;
		g_autoptr(GrlColor) ai_color = NULL;
		gfloat rotation;

		spawn = &g_array_index (game->arena->spawn_points, GrlVector2, (i + 1) % game->arena->spawn_points->len);
		ai_color = get_tuktuk_color (i + 1);

		/* Face toward center */
		rotation = atan2f (-spawn->y, -spawn->x);

		ai = derby_tuktuk_new (spawn->x, spawn->y, rotation, ai_color, -1);
		g_ptr_array_add (game->tuktuks, ai);
	}

	game->mode = mode;
	game->opponent_count = opponent_count;
	game->state = DERBY_STATE_COUNTDOWN;
	game->countdown_timer = COUNTDOWN_DURATION;
	game->match_time = 0.0f;

	if (mode == DERBY_GAME_MODE_TOURNAMENT)
	{
		game->total_rounds = 5;
		game->current_round = 1;
	}
	else
	{
		game->total_rounds = 1;
		game->current_round = 1;
	}

	if (mode == DERBY_GAME_MODE_SURVIVAL)
	{
		game->wave_timer = 30.0f;
		game->wave_number = 1;
	}
}

static void
derby_game_update (DerbyGame       *game,
                   LrgInputManager *input_manager,
                   gfloat           delta)
{
	guint i;
	gfloat accel_input;
	gfloat steer_input;

	switch (game->state)
	{
	case DERBY_STATE_MENU:
		/* Menu input handling */
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_UP))
		{
			game->menu_selection--;
			if (game->menu_selection < 0)
				game->menu_selection = 3;
		}
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_DOWN))
		{
			game->menu_selection++;
			if (game->menu_selection > 3)
				game->menu_selection = 0;
		}
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_LEFT))
		{
			if (game->menu_selection == 3)
			{
				game->menu_opponent_count--;
				if (game->menu_opponent_count < 1)
					game->menu_opponent_count = 10;
			}
		}
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_RIGHT))
		{
			if (game->menu_selection == 3)
			{
				game->menu_opponent_count++;
				if (game->menu_opponent_count > 10)
					game->menu_opponent_count = 1;
			}
		}
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ENTER) ||
		    lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_SPACE))
		{
			switch (game->menu_selection)
			{
			case 0:
				derby_game_start_match (game, DERBY_GAME_MODE_QUICK_MATCH,
				                        game->menu_opponent_count);
				break;
			case 1:
				derby_game_start_match (game, DERBY_GAME_MODE_TOURNAMENT,
				                        game->menu_opponent_count);
				break;
			case 2:
				derby_game_start_match (game, DERBY_GAME_MODE_SURVIVAL,
				                        game->menu_opponent_count);
				break;
			default:
				break;
			}
		}
		break;

	case DERBY_STATE_COUNTDOWN:
		game->countdown_timer -= delta;
		if (game->countdown_timer <= 0.0f)
			game->state = DERBY_STATE_PLAYING;
		break;

	case DERBY_STATE_PLAYING:
		/* Player input */
		accel_input = 0.0f;
		steer_input = 0.0f;

		if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_W) ||
		    lrg_input_manager_is_key_down (input_manager, GRL_KEY_UP))
			accel_input += 1.0f;
		if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_S) ||
		    lrg_input_manager_is_key_down (input_manager, GRL_KEY_DOWN))
			accel_input -= 0.5f;
		if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_A) ||
		    lrg_input_manager_is_key_down (input_manager, GRL_KEY_LEFT))
			steer_input -= 1.0f;  /* Negative = counter-clockwise = left (screen Y is down) */
		if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_D) ||
		    lrg_input_manager_is_key_down (input_manager, GRL_KEY_RIGHT))
			steer_input += 1.0f;  /* Positive = clockwise = right (screen Y is down) */

		/* Boost on spacebar */
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_SPACE))
			derby_tuktuk_activate_boost (game->player);

		/* Update player */
		if (!game->player->is_destroyed)
			derby_tuktuk_update (game->player, accel_input, steer_input, delta);

		/* Update AI tuktuks */
		for (i = 0; i < game->tuktuks->len; i++)
		{
			DerbyTukTuk *tuktuk;

			tuktuk = g_ptr_array_index (game->tuktuks, i);
			if (tuktuk->player_index < 0)
				derby_tuktuk_ai_update (tuktuk, game, delta);
		}

		/* Update collisions */
		derby_game_check_collisions (game, delta);

		/* Update powerups */
		derby_game_update_powerups (game, delta);

		/* Update particles */
		update_particles (game, delta);

		/* Check for tuktuks that need explosions */
		for (i = 0; i < game->tuktuks->len; i++)
		{
			DerbyTukTuk *tuktuk;

			tuktuk = g_ptr_array_index (game->tuktuks, i);
			if (tuktuk->needs_explosion)
			{
				spawn_explosion (game, tuktuk->x, tuktuk->y, TRUE);
				tuktuk->needs_explosion = FALSE;
			}
		}

		/* Update match time */
		game->match_time += delta;

		/* Check win/lose conditions */
		if (game->player->is_destroyed)
		{
			game->state = DERBY_STATE_RESULTS;
			game->results_timer = RESULTS_DISPLAY_TIME;
		}
		else if (derby_game_count_alive (game) <= 1)
		{
			/* Player won! */
			game->player->score += (gint)(game->match_time * SURVIVAL_POINTS_PER_SEC);
			game->state = DERBY_STATE_RESULTS;
			game->results_timer = RESULTS_DISPLAY_TIME;
		}

		/* Survival mode: spawn waves */
		if (game->mode == DERBY_GAME_MODE_SURVIVAL)
		{
			game->wave_timer -= delta;
			if (game->wave_timer <= 0.0f)
			{
				gint spawn_count;
				gint alive;

				game->wave_number++;
				spawn_count = game->wave_number;
				alive = derby_game_count_alive (game);

				/* Limit max opponents */
				if (alive + spawn_count > 10)
					spawn_count = 10 - alive;

				for (i = 0; i < (guint)spawn_count; i++)
				{
					DerbyTukTuk *ai;
					gint spawn_idx;
					GrlVector2 *spawn;
					g_autoptr(GrlColor) ai_color = NULL;
					gfloat rotation;

					spawn_idx = g_random_int_range (0, game->arena->spawn_points->len);
					spawn = &g_array_index (game->arena->spawn_points, GrlVector2, spawn_idx);
					ai_color = get_tuktuk_color (g_random_int_range (1, 10));

					rotation = atan2f (-spawn->y, -spawn->x);

					ai = derby_tuktuk_new (spawn->x, spawn->y, rotation, ai_color, -1);
					g_ptr_array_add (game->tuktuks, ai);
				}

				game->wave_timer = 30.0f;
			}
		}

		/* Pause */
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ESCAPE))
			game->state = DERBY_STATE_PAUSED;
		break;

	case DERBY_STATE_PAUSED:
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ESCAPE) ||
		    lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_SPACE))
			game->state = DERBY_STATE_PLAYING;
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_Q))
			game->state = DERBY_STATE_MENU;
		break;

	case DERBY_STATE_RESULTS:
		game->results_timer -= delta;
		if (game->results_timer <= 0.0f ||
		    lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_SPACE) ||
		    lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ENTER))
		{
			if (game->mode == DERBY_GAME_MODE_TOURNAMENT &&
			    !game->player->is_destroyed &&
			    game->current_round < game->total_rounds)
			{
				/* Next round */
				game->current_round++;
				derby_game_start_match (game, game->mode, game->opponent_count);
			}
			else
			{
				game->state = DERBY_STATE_MENU;
			}
		}
		break;
	}
}

/* =============================================================================
 * RENDERING FUNCTIONS
 * ========================================================================== */

static void
render_tuktuk (DerbyTukTuk *tuktuk)
{
	GrlRectangle body_rect;
	GrlVector2 origin;
	GrlVector2 center;
	GrlColor front_color;
	gfloat cos_r, sin_r;
	gfloat front_x, front_y;
	gfloat rotation_deg;

	if (tuktuk->is_destroyed)
		return;

	cos_r = cosf (tuktuk->rotation);
	sin_r = sinf (tuktuk->rotation);
	rotation_deg = tuktuk->rotation * 180.0f / G_PI;

	/* Main body - use grl_draw_rectangle_pro for rotation */
	body_rect.x = tuktuk->x;
	body_rect.y = tuktuk->y;
	body_rect.width = TUKTUK_LENGTH;
	body_rect.height = TUKTUK_WIDTH;
	origin.x = TUKTUK_LENGTH * 0.5f;
	origin.y = TUKTUK_WIDTH * 0.5f;
	grl_draw_rectangle_pro (&body_rect, &origin, rotation_deg, tuktuk->color);

	/* Front indicator (small circle) */
	front_color.r = (guint8)(tuktuk->color->r * 0.6f);
	front_color.g = (guint8)(tuktuk->color->g * 0.6f);
	front_color.b = (guint8)(tuktuk->color->b * 0.6f);
	front_color.a = 255;

	front_x = tuktuk->x + cos_r * TUKTUK_LENGTH * 0.3f;
	front_y = tuktuk->y + sin_r * TUKTUK_LENGTH * 0.3f;

	center.x = front_x;
	center.y = front_y;
	grl_draw_circle_v (&center, 0.8f, &front_color);

	/* Health bar */
	if (tuktuk->health < tuktuk->max_health)
	{
		GrlColor bg_color = { 50, 50, 50, 200 };
		GrlColor bar_color;
		gfloat health_pct = tuktuk->health / tuktuk->max_health;

		bar_color.r = (guint8)(50 + 205 * (1.0f - health_pct));
		bar_color.g = (guint8)(50 + 205 * health_pct);
		bar_color.b = 50;
		bar_color.a = 255;

		grl_draw_rectangle ((gint)(tuktuk->x - 2.0f), (gint)(tuktuk->y - TUKTUK_WIDTH * 0.5f - 1.5f), 4, 1, &bg_color);
		grl_draw_rectangle ((gint)(tuktuk->x - 2.0f), (gint)(tuktuk->y - TUKTUK_WIDTH * 0.5f - 1.5f), (gint)(4.0f * health_pct), 1, &bar_color);
	}

	/* Shield effect - only visible when shield powerup is active */
	if (tuktuk->active_powerup == DERBY_POWERUP_SHIELD)
	{
		GrlColor shield_outer = { 0, 200, 255, 60 };
		GrlColor shield_inner = { 100, 220, 255, 120 };
		GrlColor shield_ring = { 200, 255, 255, 200 };
		center.x = tuktuk->x;
		center.y = tuktuk->y;
		grl_draw_circle_v (&center, TUKTUK_COLLISION_RADIUS * 2.0f, &shield_outer);
		grl_draw_circle_v (&center, TUKTUK_COLLISION_RADIUS * 1.5f, &shield_inner);
		grl_draw_circle_lines ((gint)tuktuk->x, (gint)tuktuk->y,
		                       TUKTUK_COLLISION_RADIUS * 2.0f, &shield_ring);
	}
}

static void
render_hazard (DerbyHazard *hazard)
{
	GrlColor color;
	GrlRectangle rect;
	GrlVector2 origin;

	if (!hazard->is_active)
		return;

	switch (hazard->type)
	{
	case DERBY_HAZARD_OIL_SLICK:
		color = (GrlColor){ 30, 30, 30, 180 };
		break;
	case DERBY_HAZARD_SPIKE_STRIP:
		color = (GrlColor){ 100, 100, 100, 255 };
		break;
	case DERBY_HAZARD_RAMP:
		color = (GrlColor){ 139, 90, 43, 255 };
		break;
	case DERBY_HAZARD_FIRE_PIT:
		color = (GrlColor){ 255, 100, 0, 200 };
		break;
	case DERBY_HAZARD_BARRIER:
		color = (GrlColor){ 80, 80, 80, 255 };
		break;
	default:
		color = (GrlColor){ 128, 128, 128, 255 };
		break;
	}

	rect.x = hazard->x;
	rect.y = hazard->y;
	rect.width = hazard->width;
	rect.height = hazard->height;
	origin.x = hazard->width * 0.5f;
	origin.y = hazard->height * 0.5f;
	grl_draw_rectangle_pro (&rect, &origin, hazard->rotation * 180.0f / G_PI, &color);
}

static void
render_powerup (DerbyPowerUp *powerup)
{
	GrlColor color;
	GrlVector2 center;

	if (!powerup->is_active)
		return;

	switch (powerup->type)
	{
	case DERBY_POWERUP_SPEED_BOOST:
		color = (GrlColor){ 0, 255, 255, 255 };
		break;
	case DERBY_POWERUP_RAM_DAMAGE:
		color = (GrlColor){ 255, 0, 0, 255 };
		break;
	case DERBY_POWERUP_NITRO:
		color = (GrlColor){ 255, 165, 0, 255 };
		break;
	case DERBY_POWERUP_SHIELD:
		color = (GrlColor){ 100, 150, 255, 255 };
		break;
	case DERBY_POWERUP_REPAIR:
		color = (GrlColor){ 0, 255, 0, 255 };
		break;
	case DERBY_POWERUP_ARMOR:
		color = (GrlColor){ 128, 128, 128, 255 };
		break;
	default:
		color = (GrlColor){ 255, 255, 255, 255 };
		break;
	}

	center.x = powerup->x;
	center.y = powerup->y;
	grl_draw_circle_v (&center, POWERUP_RADIUS * 2.0f, &color);
}

static void
render_particles (DerbyGame *game)
{
	guint i;

	for (i = 0; i < MAX_PARTICLES; i++)
	{
		DerbyParticle *p = &game->particles[i];
		if (!p->active)
			continue;

		{
			GrlVector2 pos = { p->x, p->y };
			gfloat alpha = (p->life / p->max_life) * 255.0f;
			gfloat size = p->size * (0.5f + 0.5f * (p->life / p->max_life));
			GrlColor color = { p->r, p->g, p->b, (guint8)alpha };

			grl_draw_circle_v (&pos, size, &color);
		}
	}
}

static void
render_arena (DerbyArena *arena)
{
	GrlVector2 center = { 0.0f, 0.0f };
	GrlColor boundary_color = { 200, 50, 50, 255 };
	GrlColor floor_color = { 60, 60, 70, 255 };
	GrlColor line_color = { 100, 100, 120, 255 };
	guint i;

	/* Boundary ring (danger zone) - draw first so floor covers center */
	grl_draw_circle_v (&center, arena->ringout_radius, &boundary_color);

	/* Arena floor - draw on top of boundary */
	grl_draw_circle_v (&center, arena->radius, &floor_color);

	/* Draw arena boundary line */
	grl_draw_circle_lines (0, 0, arena->radius, &line_color);

	/* Hazards */
	for (i = 0; i < arena->hazards->len; i++)
	{
		DerbyHazard *hazard;

		hazard = g_ptr_array_index (arena->hazards, i);
		render_hazard (hazard);
	}

	/* Power-ups */
	for (i = 0; i < arena->powerups->len; i++)
	{
		DerbyPowerUp *powerup;

		powerup = g_ptr_array_index (arena->powerups, i);
		render_powerup (powerup);
	}
}

static void
render_hud (DerbyGame *game)
{
	GrlColor white = { 255, 255, 255, 255 };
	g_autofree gchar *score_text = NULL;
	g_autofree gchar *health_text = NULL;
	g_autofree gchar *alive_text = NULL;

	score_text = g_strdup_printf ("Score: %d", game->player->score);
	health_text = g_strdup_printf ("Health: %.0f", game->player->health);
	alive_text = g_strdup_printf ("Alive: %d", derby_game_count_alive (game));

	grl_draw_text (score_text, 10, 10, 20, &white);
	grl_draw_text (health_text, 10, 35, 20, &white);
	grl_draw_text (alive_text, 10, 60, 20, &white);

	/* Combo display */
	if (game->player->combo_count > 1)
	{
		GrlColor combo_color = { 255, 200, 50, 255 };
		g_autofree gchar *combo_text = NULL;

		combo_text = g_strdup_printf ("COMBO x%d (%.1fx)",
		                              game->player->combo_count,
		                              get_combo_multiplier (game->player->combo_count));
		grl_draw_text (combo_text, 10, 85, 20, &combo_color);
	}

	/* Power-up indicator */
	if (game->player->active_powerup != DERBY_POWERUP_NONE)
	{
		GrlColor powerup_color = { 100, 255, 100, 255 };
		g_autofree gchar *powerup_text = NULL;
		const gchar *powerup_name;

		switch (game->player->active_powerup)
		{
		case DERBY_POWERUP_SPEED_BOOST:
			powerup_name = "SPEED";
			break;
		case DERBY_POWERUP_RAM_DAMAGE:
			powerup_name = "DAMAGE";
			break;
		case DERBY_POWERUP_SHIELD:
			powerup_name = "SHIELD";
			break;
		case DERBY_POWERUP_ARMOR:
			powerup_name = "ARMOR";
			break;
		default:
			powerup_name = "POWER";
			break;
		}

		powerup_text = g_strdup_printf ("%s: %.1fs", powerup_name, game->player->powerup_time);
		grl_draw_text (powerup_text, 10, 110, 20, &powerup_color);
	}

	/* Mode-specific info */
	if (game->mode == DERBY_GAME_MODE_TOURNAMENT)
	{
		g_autofree gchar *round_text = NULL;

		round_text = g_strdup_printf ("Round %d/%d", game->current_round, game->total_rounds);
		grl_draw_text (round_text, 650, 10, 20, &white);
	}
	else if (game->mode == DERBY_GAME_MODE_SURVIVAL)
	{
		g_autofree gchar *wave_text = NULL;

		wave_text = g_strdup_printf ("Wave %d", game->wave_number);
		grl_draw_text (wave_text, 650, 10, 20, &white);
	}

	/* Boost meter at bottom of screen */
	{
		GrlColor boost_bg = { 50, 50, 50, 200 };
		GrlColor boost_fill = { 50, 200, 255, 255 };
		GrlColor boost_ready = { 100, 255, 100, 255 };
		GrlRectangle bar_bg = { 300, 570, 200, 20 };
		GrlRectangle bar_fill;
		gfloat fill_pct;

		fill_pct = game->player->boost_charge / BOOST_MAX_CHARGE;
		bar_fill.x = 300;
		bar_fill.y = 570;
		bar_fill.width = 200.0f * fill_pct;
		bar_fill.height = 20;

		grl_draw_rectangle_rec (&bar_bg, &boost_bg);
		grl_draw_rectangle_rec (&bar_fill,
		                        game->player->boost_charge >= BOOST_COST ? &boost_ready : &boost_fill);
		grl_draw_text ("BOOST [SPACE]", 310, 573, 16, &white);

		if (game->player->is_boosting)
		{
			GrlColor boosting = { 255, 255, 100, 255 };
			grl_draw_text ("BOOSTING!", 520, 573, 16, &boosting);
		}
	}
}

static void
render_menu (DerbyGame *game)
{
	GrlColor yellow = { 255, 220, 50, 255 };
	GrlColor white = { 255, 255, 255, 255 };
	GrlColor gray = { 150, 150, 150, 255 };
	g_autofree gchar *opp_text = NULL;

	grl_draw_text ("TUKTUK DERBY", 250, 80, 50, &yellow);

	grl_draw_text ("Quick Match", 300, 200, 30, game->menu_selection == 0 ? &yellow : &white);
	grl_draw_text ("Tournament", 300, 250, 30, game->menu_selection == 1 ? &yellow : &white);
	grl_draw_text ("Survival", 300, 300, 30, game->menu_selection == 2 ? &yellow : &white);

	opp_text = g_strdup_printf ("< Opponents: %d >", game->menu_opponent_count);
	grl_draw_text (opp_text, 280, 370, 25, game->menu_selection == 3 ? &yellow : &white);

	grl_draw_text ("Arrow keys: Navigate | Enter/Space: Select", 200, 500, 18, &gray);
}

static void
render_countdown (DerbyGame *game)
{
	GrlColor yellow = { 255, 220, 50, 255 };
	g_autofree gchar *countdown_text = NULL;

	if (game->countdown_timer > 0.0f)
	{
		countdown_text = g_strdup_printf ("%d", (gint)ceilf (game->countdown_timer));
	}
	else
	{
		countdown_text = g_strdup ("GO!");
	}

	grl_draw_text (countdown_text, 370, 280, 80, &yellow);
}

static void
render_paused (void)
{
	GrlColor white = { 255, 255, 255, 255 };
	GrlColor gray = { 150, 150, 150, 255 };

	grl_draw_text ("PAUSED", 320, 250, 50, &white);
	grl_draw_text ("Press ESC or SPACE to resume", 280, 330, 20, &gray);
	grl_draw_text ("Press Q to quit to menu", 300, 360, 20, &gray);
}

static void
render_results (DerbyGame *game)
{
	GrlColor white = { 255, 255, 255, 255 };
	GrlColor yellow = { 255, 220, 50, 255 };
	GrlColor red = { 255, 50, 50, 255 };
	GrlColor *result_color;
	g_autofree gchar *score_text = NULL;
	g_autofree gchar *knockouts_text = NULL;
	g_autofree gchar *damage_text = NULL;
	const gchar *result_text;

	if (game->player->is_destroyed)
	{
		result_text = "DESTROYED!";
		result_color = &red;
	}
	else
	{
		result_text = "VICTORY!";
		result_color = &yellow;
	}

	grl_draw_text (result_text, 300, 150, 60, result_color);

	score_text = g_strdup_printf ("Final Score: %d", game->player->score);
	grl_draw_text (score_text, 280, 250, 30, &white);

	knockouts_text = g_strdup_printf ("Knockouts: %d", game->player->knockouts);
	grl_draw_text (knockouts_text, 290, 300, 25, &white);

	damage_text = g_strdup_printf ("Damage Dealt: %d", game->player->damage_dealt);
	grl_draw_text (damage_text, 280, 340, 25, &white);

	grl_draw_text ("Press SPACE to continue...", 280, 450, 20, &white);
}

static void
derby_game_render (DerbyGame   *game,
                   LrgRenderer *renderer)
{
	GrlColor bg_color = { 20, 20, 30, 255 };
	guint i;

	lrg_renderer_begin_frame (renderer);
	grl_draw_clear_background (&bg_color);

	switch (game->state)
	{
	case DERBY_STATE_MENU:
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
		render_menu (game);
		lrg_renderer_end_layer (renderer);
		break;

	case DERBY_STATE_COUNTDOWN:
	case DERBY_STATE_PLAYING:
	case DERBY_STATE_PAUSED:
		/* World layer */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);

		render_arena (game->arena);

		for (i = 0; i < game->tuktuks->len; i++)
		{
			DerbyTukTuk *tuktuk;

			tuktuk = g_ptr_array_index (game->tuktuks, i);
			render_tuktuk (tuktuk);
		}

		/* Render particles on top */
		render_particles (game);

		lrg_renderer_end_layer (renderer);

		/* UI layer */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);

		render_hud (game);

		if (game->state == DERBY_STATE_COUNTDOWN)
			render_countdown (game);
		else if (game->state == DERBY_STATE_PAUSED)
			render_paused ();

		lrg_renderer_end_layer (renderer);
		break;

	case DERBY_STATE_RESULTS:
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
		render_results (game);
		lrg_renderer_end_layer (renderer);
		break;
	}

	lrg_renderer_end_frame (renderer);
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError) error = NULL;
	LrgEngine *engine;
	LrgRenderer *renderer;
	LrgInputManager *input_manager;
	g_autoptr(LrgGrlWindow) window = NULL;
	g_autoptr(LrgCameraTopDown) camera = NULL;
	g_autoptr(DerbyGame) game = NULL;

	/* Create window */
	window = lrg_grl_window_new (800, 600, "TukTuk Derby - Libregnum Example");
	lrg_window_set_target_fps (LRG_WINDOW (window), 60);

	/* Initialize engine */
	engine = lrg_engine_get_default ();
	lrg_engine_set_window (engine, LRG_WINDOW (window));

	if (!lrg_engine_startup (engine, &error))
	{
		g_error ("Failed to start engine: %s", error->message);
		return 1;
	}

	/* Get renderer and input manager */
	renderer = lrg_engine_get_renderer (engine);
	input_manager = lrg_input_manager_get_default ();

	/* Create top-down camera - centered on player */
	camera = lrg_camera_topdown_new ();
	lrg_camera2d_set_offset_xy (LRG_CAMERA2D (camera), 400.0f, 300.0f);  /* Screen center */
	lrg_camera2d_set_zoom (LRG_CAMERA2D (camera), 4.0f);
	lrg_camera_topdown_set_follow_speed (camera, 8.0f);
	lrg_camera_topdown_set_deadzone_radius (camera, 0.0f);
	/* Initialize camera to player spawn position (0, 80) - snap immediately */
	lrg_camera_topdown_follow (camera, 0.0f, 80.0f, 100.0f);
	lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

	/* Create game */
	game = g_object_new (DERBY_TYPE_GAME, NULL);

	/* Main game loop */
	while (!lrg_window_should_close (LRG_WINDOW (window)))
	{
		gfloat delta;

		delta = lrg_window_get_frame_time (LRG_WINDOW (window));

		/* Poll input */
		lrg_input_manager_poll (input_manager);

		/* Update game */
		derby_game_update (game, input_manager, delta);

		/* Update camera to follow player during gameplay */
		if (game->state == DERBY_STATE_PLAYING ||
		    game->state == DERBY_STATE_COUNTDOWN ||
		    game->state == DERBY_STATE_PAUSED)
		{
			if (game->player != NULL && !game->player->is_destroyed)
			{
				/* Use topdown follow for smooth camera movement */
				lrg_camera_topdown_follow (camera,
				                           game->player->x,
				                           game->player->y,
				                           delta);
			}
		}

		/* Render game */
		derby_game_render (game, renderer);
	}

	/* Cleanup */
	lrg_engine_shutdown (engine);

	return 0;
}
