/* game-omnomagon.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A complete 3D Omnomagon game demonstrating libregnum's data-driven
 * architecture with YAML configuration, GObject type system, and
 * graylib 3D rendering.
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>
#include <string.h>

/* =============================================================================
 * ENUMERATIONS
 * ========================================================================== */

typedef enum
{
	GHOST_STATE_CHASE,
	GHOST_STATE_SCATTER,
	GHOST_STATE_FRIGHTENED,
	GHOST_STATE_DEAD
} GhostState;

typedef enum
{
	GAME_STATE_PLAYING,
	GAME_STATE_WIN,
	GAME_STATE_LOSE,
	GAME_STATE_PAUSED
} GameState;

typedef enum
{
	CAMERA_MODE_ISOMETRIC,
	CAMERA_MODE_THIRDPERSON,
	CAMERA_MODE_FIRSTPERSON,
	CAMERA_MODE_COUNT
} CameraMode;

/* =============================================================================
 * FORWARD DECLARATIONS
 * ========================================================================== */

typedef struct _PacPellet PacPellet;
typedef struct _PacPlayer PacPlayer;
typedef struct _PacGhost PacGhost;
typedef struct _PacMaze PacMaze;
typedef struct _PacGame PacGame;

/* Forward declare type macros */
#define PAC_TYPE_PELLET (pac_pellet_get_type())
#define PAC_TYPE_PLAYER (pac_player_get_type())
#define PAC_TYPE_GHOST (pac_ghost_get_type())
#define PAC_TYPE_MAZE (pac_maze_get_type())
#define PAC_TYPE_GAME (pac_game_get_type())

G_DECLARE_FINAL_TYPE (PacPellet, pac_pellet, PAC, PELLET, GObject)
G_DECLARE_FINAL_TYPE (PacPlayer, pac_player, PAC, PLAYER, GObject)
G_DECLARE_FINAL_TYPE (PacGhost, pac_ghost, PAC, GHOST, GObject)
G_DECLARE_FINAL_TYPE (PacMaze, pac_maze, PAC, MAZE, GObject)
G_DECLARE_FINAL_TYPE (PacGame, pac_game, PAC, GAME, GObject)

/* =============================================================================
 * PAC_PELLET TYPE
 * ========================================================================== */

struct _PacPellet
{
	GObject parent_instance;

	GrlVector3 *position;
	gboolean    is_power_pellet;
	gboolean    collected;
	gint        points;
};

enum
{
	PROP_PELLET_0,
	PROP_PELLET_POSITION,
	PROP_PELLET_IS_POWER_PELLET,
	PROP_PELLET_COLLECTED,
	PROP_PELLET_POINTS,
	N_PELLET_PROPS
};

static GParamSpec *pellet_properties[N_PELLET_PROPS];

G_DEFINE_TYPE (PacPellet, pac_pellet, G_TYPE_OBJECT)

static void
pac_pellet_finalize (GObject *object)
{
	PacPellet *self = PAC_PELLET (object);

	g_clear_pointer (&self->position, grl_vector3_free);

	G_OBJECT_CLASS (pac_pellet_parent_class)->finalize (object);
}

static void
pac_pellet_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	PacPellet *self = PAC_PELLET (object);

	switch (prop_id)
	{
	case PROP_PELLET_POSITION:
		g_value_set_boxed (value, self->position);
		break;
	case PROP_PELLET_IS_POWER_PELLET:
		g_value_set_boolean (value, self->is_power_pellet);
		break;
	case PROP_PELLET_COLLECTED:
		g_value_set_boolean (value, self->collected);
		break;
	case PROP_PELLET_POINTS:
		g_value_set_int (value, self->points);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_pellet_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	PacPellet *self = PAC_PELLET (object);

	switch (prop_id)
	{
	case PROP_PELLET_POSITION:
		g_clear_pointer (&self->position, grl_vector3_free);
		self->position = g_value_dup_boxed (value);
		break;
	case PROP_PELLET_IS_POWER_PELLET:
		self->is_power_pellet = g_value_get_boolean (value);
		break;
	case PROP_PELLET_COLLECTED:
		self->collected = g_value_get_boolean (value);
		break;
	case PROP_PELLET_POINTS:
		self->points = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_pellet_class_init (PacPelletClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pac_pellet_finalize;
	object_class->get_property = pac_pellet_get_property;
	object_class->set_property = pac_pellet_set_property;

	pellet_properties[PROP_PELLET_POSITION] =
		g_param_spec_boxed ("position",
		                    "Position",
		                    "3D position in the maze",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	pellet_properties[PROP_PELLET_IS_POWER_PELLET] =
		g_param_spec_boolean ("is-power-pellet",
		                      "Is Power Pellet",
		                      "Whether this is a power pellet",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	pellet_properties[PROP_PELLET_COLLECTED] =
		g_param_spec_boolean ("collected",
		                      "Collected",
		                      "Whether this pellet has been collected",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	pellet_properties[PROP_PELLET_POINTS] =
		g_param_spec_int ("points",
		                  "Points",
		                  "Point value of this pellet",
		                  0, G_MAXINT, 10,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PELLET_PROPS, pellet_properties);
}

static void
pac_pellet_init (PacPellet *self)
{
	self->position = grl_vector3_new (0.0f, 0.0f, 0.0f);
	self->is_power_pellet = FALSE;
	self->collected = FALSE;
	self->points = 10;
}

static PacPellet *
pac_pellet_new (GrlVector3 *position,
                gboolean    is_power_pellet)
{
	gint points;

	points = is_power_pellet ? 50 : 10;

	return g_object_new (PAC_TYPE_PELLET,
	                     "position", position,
	                     "is-power-pellet", is_power_pellet,
	                     "points", points,
	                     NULL);
}

/* =============================================================================
 * PAC_PLAYER TYPE
 * ========================================================================== */

struct _PacPlayer
{
	GObject parent_instance;

	GrlVector3 *position;
	GrlVector3 *direction;
	gfloat      speed;
	gint        score;
	gint        lives;
	gboolean    power_mode;
	gfloat      power_time;
};

enum
{
	PROP_PLAYER_0,
	PROP_PLAYER_POSITION,
	PROP_PLAYER_DIRECTION,
	PROP_PLAYER_SPEED,
	PROP_PLAYER_SCORE,
	PROP_PLAYER_LIVES,
	PROP_PLAYER_POWER_MODE,
	PROP_PLAYER_POWER_TIME,
	N_PLAYER_PROPS
};

static GParamSpec *player_properties[N_PLAYER_PROPS];

G_DEFINE_TYPE (PacPlayer, pac_player, G_TYPE_OBJECT)

static void
pac_player_finalize (GObject *object)
{
	PacPlayer *self = PAC_PLAYER (object);

	g_clear_pointer (&self->position, grl_vector3_free);
	g_clear_pointer (&self->direction, grl_vector3_free);

	G_OBJECT_CLASS (pac_player_parent_class)->finalize (object);
}

static void
pac_player_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	PacPlayer *self = PAC_PLAYER (object);

	switch (prop_id)
	{
	case PROP_PLAYER_POSITION:
		g_value_set_boxed (value, self->position);
		break;
	case PROP_PLAYER_DIRECTION:
		g_value_set_boxed (value, self->direction);
		break;
	case PROP_PLAYER_SPEED:
		g_value_set_float (value, self->speed);
		break;
	case PROP_PLAYER_SCORE:
		g_value_set_int (value, self->score);
		break;
	case PROP_PLAYER_LIVES:
		g_value_set_int (value, self->lives);
		break;
	case PROP_PLAYER_POWER_MODE:
		g_value_set_boolean (value, self->power_mode);
		break;
	case PROP_PLAYER_POWER_TIME:
		g_value_set_float (value, self->power_time);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_player_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	PacPlayer *self = PAC_PLAYER (object);

	switch (prop_id)
	{
	case PROP_PLAYER_POSITION:
		g_clear_pointer (&self->position, grl_vector3_free);
		self->position = g_value_dup_boxed (value);
		break;
	case PROP_PLAYER_DIRECTION:
		g_clear_pointer (&self->direction, grl_vector3_free);
		self->direction = g_value_dup_boxed (value);
		break;
	case PROP_PLAYER_SPEED:
		self->speed = g_value_get_float (value);
		break;
	case PROP_PLAYER_SCORE:
		self->score = g_value_get_int (value);
		break;
	case PROP_PLAYER_LIVES:
		self->lives = g_value_get_int (value);
		break;
	case PROP_PLAYER_POWER_MODE:
		self->power_mode = g_value_get_boolean (value);
		break;
	case PROP_PLAYER_POWER_TIME:
		self->power_time = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_player_class_init (PacPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pac_player_finalize;
	object_class->get_property = pac_player_get_property;
	object_class->set_property = pac_player_set_property;

	player_properties[PROP_PLAYER_POSITION] =
		g_param_spec_boxed ("position", "Position", "Player position",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	player_properties[PROP_PLAYER_DIRECTION] =
		g_param_spec_boxed ("direction", "Direction", "Movement direction",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	player_properties[PROP_PLAYER_SPEED] =
		g_param_spec_float ("speed", "Speed", "Movement speed",
		                    0.0f, G_MAXFLOAT, 3.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	player_properties[PROP_PLAYER_SCORE] =
		g_param_spec_int ("score", "Score", "Player score",
		                  0, G_MAXINT, 0,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	player_properties[PROP_PLAYER_LIVES] =
		g_param_spec_int ("lives", "Lives", "Remaining lives",
		                  0, G_MAXINT, 3,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	player_properties[PROP_PLAYER_POWER_MODE] =
		g_param_spec_boolean ("power-mode", "Power Mode", "Power mode active",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	player_properties[PROP_PLAYER_POWER_TIME] =
		g_param_spec_float ("power-time", "Power Time", "Remaining power time",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PLAYER_PROPS, player_properties);
}

static void
pac_player_init (PacPlayer *self)
{
	self->position = grl_vector3_new (0.0f, 0.5f, 0.0f);
	self->direction = grl_vector3_new (0.0f, 0.0f, 0.0f);
	self->speed = 3.0f;
	self->score = 0;
	self->lives = 3;
	self->power_mode = FALSE;
	self->power_time = 0.0f;
}

static PacPlayer *
pac_player_new (GrlVector3 *spawn_position)
{
	return g_object_new (PAC_TYPE_PLAYER,
	                     "position", spawn_position,
	                     NULL);
}

/* =============================================================================
 * PAC_GHOST TYPE
 * ========================================================================== */

struct _PacGhost
{
	GObject parent_instance;

	GrlVector3 *position;
	GrlVector3 *direction;
	gfloat      speed;
	GrlColor   *color;
	GhostState  state;
	GrlVector3 *spawn_point;
};

enum
{
	PROP_GHOST_0,
	PROP_GHOST_POSITION,
	PROP_GHOST_DIRECTION,
	PROP_GHOST_SPEED,
	PROP_GHOST_COLOR,
	PROP_GHOST_STATE,
	PROP_GHOST_SPAWN_POINT,
	N_GHOST_PROPS
};

static GParamSpec *ghost_properties[N_GHOST_PROPS];

G_DEFINE_TYPE (PacGhost, pac_ghost, G_TYPE_OBJECT)

static void
pac_ghost_finalize (GObject *object)
{
	PacGhost *self = PAC_GHOST (object);

	g_clear_pointer (&self->position, grl_vector3_free);
	g_clear_pointer (&self->direction, grl_vector3_free);
	g_clear_pointer (&self->color, grl_color_free);
	g_clear_pointer (&self->spawn_point, grl_vector3_free);

	G_OBJECT_CLASS (pac_ghost_parent_class)->finalize (object);
}

static void
pac_ghost_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	PacGhost *self = PAC_GHOST (object);

	switch (prop_id)
	{
	case PROP_GHOST_POSITION:
		g_value_set_boxed (value, self->position);
		break;
	case PROP_GHOST_DIRECTION:
		g_value_set_boxed (value, self->direction);
		break;
	case PROP_GHOST_SPEED:
		g_value_set_float (value, self->speed);
		break;
	case PROP_GHOST_COLOR:
		g_value_set_boxed (value, self->color);
		break;
	case PROP_GHOST_STATE:
		g_value_set_int (value, self->state);
		break;
	case PROP_GHOST_SPAWN_POINT:
		g_value_set_boxed (value, self->spawn_point);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_ghost_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	PacGhost *self = PAC_GHOST (object);

	switch (prop_id)
	{
	case PROP_GHOST_POSITION:
		g_clear_pointer (&self->position, grl_vector3_free);
		self->position = g_value_dup_boxed (value);
		break;
	case PROP_GHOST_DIRECTION:
		g_clear_pointer (&self->direction, grl_vector3_free);
		self->direction = g_value_dup_boxed (value);
		break;
	case PROP_GHOST_SPEED:
		self->speed = g_value_get_float (value);
		break;
	case PROP_GHOST_COLOR:
		g_clear_pointer (&self->color, grl_color_free);
		self->color = g_value_dup_boxed (value);
		break;
	case PROP_GHOST_STATE:
		self->state = g_value_get_int (value);
		break;
	case PROP_GHOST_SPAWN_POINT:
		g_clear_pointer (&self->spawn_point, grl_vector3_free);
		self->spawn_point = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_ghost_class_init (PacGhostClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pac_ghost_finalize;
	object_class->get_property = pac_ghost_get_property;
	object_class->set_property = pac_ghost_set_property;

	ghost_properties[PROP_GHOST_POSITION] =
		g_param_spec_boxed ("position", "Position", "Ghost position",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	ghost_properties[PROP_GHOST_DIRECTION] =
		g_param_spec_boxed ("direction", "Direction", "Movement direction",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	ghost_properties[PROP_GHOST_SPEED] =
		g_param_spec_float ("speed", "Speed", "Movement speed",
		                    0.0f, G_MAXFLOAT, 2.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	ghost_properties[PROP_GHOST_COLOR] =
		g_param_spec_boxed ("color", "Color", "Ghost color",
		                    GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	ghost_properties[PROP_GHOST_STATE] =
		g_param_spec_int ("state", "State", "Ghost AI state",
		                  GHOST_STATE_CHASE, GHOST_STATE_DEAD, GHOST_STATE_CHASE,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	ghost_properties[PROP_GHOST_SPAWN_POINT] =
		g_param_spec_boxed ("spawn-point", "Spawn Point", "Respawn location",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_GHOST_PROPS, ghost_properties);
}

static void
pac_ghost_init (PacGhost *self)
{
	self->position = grl_vector3_new (0.0f, 0.5f, 0.0f);
	self->direction = grl_vector3_new (0.0f, 0.0f, 0.0f);
	self->speed = 2.5f;
	self->color = grl_color_new (255, 0, 0, 255);
	self->state = GHOST_STATE_CHASE;
	self->spawn_point = grl_vector3_new (0.0f, 0.5f, 0.0f);
}

static PacGhost *
pac_ghost_new (GrlVector3 *spawn_position,
               GrlColor   *color)
{
	return g_object_new (PAC_TYPE_GHOST,
	                     "position", spawn_position,
	                     "spawn-point", spawn_position,
	                     "color", color,
	                     NULL);
}

/* =============================================================================
 * PAC_MAZE TYPE
 * ========================================================================== */

struct _PacMaze
{
	GObject parent_instance;

	gint         width;
	gint         height;
	gfloat       tile_size;
	GArray      *walls;
	GPtrArray   *pellets;
	GArray      *ghost_spawns;
	GrlVector3  *player_spawn;
	gchar       *layout;
};

enum
{
	PROP_MAZE_0,
	PROP_MAZE_WIDTH,
	PROP_MAZE_HEIGHT,
	PROP_MAZE_TILE_SIZE,
	PROP_MAZE_PLAYER_SPAWN,
	PROP_MAZE_LAYOUT,
	N_MAZE_PROPS
};

static GParamSpec *maze_properties[N_MAZE_PROPS];

G_DEFINE_TYPE (PacMaze, pac_maze, G_TYPE_OBJECT)

/* Forward declaration */
static void pac_maze_parse_layout (PacMaze *self, const gchar *layout);

static void
pac_maze_finalize (GObject *object)
{
	PacMaze *self = PAC_MAZE (object);

	if (self->walls != NULL)
		g_array_unref (self->walls);

	if (self->pellets != NULL)
		g_ptr_array_unref (self->pellets);

	if (self->ghost_spawns != NULL)
		g_array_unref (self->ghost_spawns);

	g_clear_pointer (&self->player_spawn, grl_vector3_free);
	g_free (self->layout);

	G_OBJECT_CLASS (pac_maze_parent_class)->finalize (object);
}

static void
pac_maze_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	PacMaze *self = PAC_MAZE (object);

	switch (prop_id)
	{
	case PROP_MAZE_WIDTH:
		g_value_set_int (value, self->width);
		break;
	case PROP_MAZE_HEIGHT:
		g_value_set_int (value, self->height);
		break;
	case PROP_MAZE_TILE_SIZE:
		g_value_set_float (value, self->tile_size);
		break;
	case PROP_MAZE_PLAYER_SPAWN:
		g_value_set_boxed (value, self->player_spawn);
		break;
	case PROP_MAZE_LAYOUT:
		g_value_set_string (value, self->layout);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_maze_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	PacMaze *self = PAC_MAZE (object);

	switch (prop_id)
	{
	case PROP_MAZE_WIDTH:
		self->width = g_value_get_int (value);
		break;
	case PROP_MAZE_HEIGHT:
		self->height = g_value_get_int (value);
		break;
	case PROP_MAZE_TILE_SIZE:
		self->tile_size = g_value_get_float (value);
		break;
	case PROP_MAZE_PLAYER_SPAWN:
		g_clear_pointer (&self->player_spawn, grl_vector3_free);
		self->player_spawn = g_value_dup_boxed (value);
		break;
	case PROP_MAZE_LAYOUT:
		g_free (self->layout);
		self->layout = g_value_dup_string (value);

		/* Automatically parse the layout when it's set */
		if (self->layout != NULL)
			pac_maze_parse_layout (self, self->layout);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_maze_class_init (PacMazeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pac_maze_finalize;
	object_class->get_property = pac_maze_get_property;
	object_class->set_property = pac_maze_set_property;

	maze_properties[PROP_MAZE_WIDTH] =
		g_param_spec_int ("width", "Width", "Maze width in tiles",
		                  0, G_MAXINT, 19,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	maze_properties[PROP_MAZE_HEIGHT] =
		g_param_spec_int ("height", "Height", "Maze height in tiles",
		                  0, G_MAXINT, 21,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	maze_properties[PROP_MAZE_TILE_SIZE] =
		g_param_spec_float ("tile-size", "Tile Size", "Size of each tile",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	maze_properties[PROP_MAZE_PLAYER_SPAWN] =
		g_param_spec_boxed ("player-spawn", "Player Spawn", "Player start position",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	maze_properties[PROP_MAZE_LAYOUT] =
		g_param_spec_string ("layout", "Layout", "Maze layout as ASCII art",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_MAZE_PROPS, maze_properties);
}

static void
pac_maze_init (PacMaze *self)
{
	self->width = 19;
	self->height = 21;
	self->tile_size = 1.0f;
	self->walls = g_array_new (FALSE, FALSE, sizeof (GrlVector3));
	self->pellets = g_ptr_array_new_with_free_func (g_object_unref);
	self->ghost_spawns = g_array_new (FALSE, FALSE, sizeof (GrlVector3));
	self->player_spawn = grl_vector3_new (9.5f, 0.5f, 15.5f);
	self->layout = NULL;
}

static void
pac_maze_parse_layout (PacMaze     *self,
                       const gchar *layout)
{
	gchar **lines;
	gint    row;
	gint    col;
	gint    num_lines;

	/* Split layout into lines */
	lines = g_strsplit (layout, "\n", -1);
	num_lines = g_strv_length (lines);

	/* Parse each line */
	for (row = 0; row < num_lines && row < self->height; row++)
	{
		gint line_len;

		line_len = strlen (lines[row]);

		for (col = 0; col < line_len && col < self->width; col++)
		{
			gchar      c;
			gfloat     x;
			gfloat     z;
			GrlVector3 pos;

			c = lines[row][col];
			x = col * self->tile_size + self->tile_size * 0.5f;
			z = row * self->tile_size + self->tile_size * 0.5f;

			if (c == '#')
			{
				/* Wall */
				pos.x = x;
				pos.y = 0.5f;
				pos.z = z;
				g_array_append_val (self->walls, pos);
			}
			else if (c == '.' || c == 'O')
			{
				/* Pellet or power pellet */
				GrlVector3 *pellet_pos;
				PacPellet  *pellet;
				gboolean    is_power;

				pellet_pos = grl_vector3_new (x, 0.5f, z);
				is_power = (c == 'O');
				pellet = pac_pellet_new (pellet_pos, is_power);
				g_ptr_array_add (self->pellets, pellet);

				grl_vector3_free (pellet_pos);
			}
			else if (c == 'G')
			{
				/* Ghost spawn point */
				pos.x = x;
				pos.y = 0.5f;
				pos.z = z;
				g_array_append_val (self->ghost_spawns, pos);
			}
		}
	}

	g_strfreev (lines);
}

static void
pac_maze_render (PacMaze *self)
{
	g_autoptr(GrlColor) wall_color = NULL;
	g_autoptr(GrlColor) pellet_color = NULL;
	g_autoptr(GrlColor) power_color = NULL;
	guint               i;

	wall_color = grl_color_new (50, 50, 200, 255);
	pellet_color = grl_color_new (255, 255, 255, 255);
	power_color = grl_color_new (255, 255, 0, 255);

	/* Draw walls using LrgCube3D */
	for (i = 0; i < self->walls->len; i++)
	{
		g_autoptr(LrgCube3D) cube = NULL;
		GrlVector3          *wall;

		wall = &g_array_index (self->walls, GrlVector3, i);
		cube = lrg_cube3d_new_full (wall->x, wall->y, wall->z,
		                            1.0f, 0.25f, 1.0f,
		                            wall_color);
		lrg_drawable_draw (LRG_DRAWABLE (cube), 0.0f);
	}

	/* Draw pellets using LrgSphere3D */
	for (i = 0; i < self->pellets->len; i++)
	{
		PacPellet *pellet;

		pellet = g_ptr_array_index (self->pellets, i);

		if (!pellet->collected)
		{
			g_autoptr(LrgSphere3D) sphere = NULL;
			gfloat                 radius;
			GrlColor              *color;

			radius = pellet->is_power_pellet ? 0.3f : 0.15f;
			color = pellet->is_power_pellet ? power_color : pellet_color;

			sphere = lrg_sphere3d_new_full (pellet->position->x,
			                                pellet->position->y,
			                                pellet->position->z,
			                                radius,
			                                color);
			lrg_drawable_draw (LRG_DRAWABLE (sphere), 0.0f);
		}
	}

	/* Grid floor removed - maze structure is visible without it */
}

/* =============================================================================
 * PAC_GAME TYPE
 * ========================================================================== */

struct _PacGame
{
	GObject parent_instance;

	PacMaze    *maze;
	PacPlayer  *player;
	GPtrArray  *ghosts;
	GameState   state;
	gint        total_pellets;
	gint        collected_pellets;
};

enum
{
	PROP_GAME_0,
	PROP_GAME_MAZE,
	PROP_GAME_PLAYER,
	PROP_GAME_STATE,
	N_GAME_PROPS
};

static GParamSpec *game_properties[N_GAME_PROPS];

G_DEFINE_TYPE (PacGame, pac_game, G_TYPE_OBJECT)

static void
pac_game_finalize (GObject *object)
{
	PacGame *self = PAC_GAME (object);

	g_clear_object (&self->maze);
	g_clear_object (&self->player);

	if (self->ghosts != NULL)
		g_ptr_array_unref (self->ghosts);

	G_OBJECT_CLASS (pac_game_parent_class)->finalize (object);
}

static void
pac_game_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	PacGame *self = PAC_GAME (object);

	switch (prop_id)
	{
	case PROP_GAME_MAZE:
		g_value_set_object (value, self->maze);
		break;
	case PROP_GAME_PLAYER:
		g_value_set_object (value, self->player);
		break;
	case PROP_GAME_STATE:
		g_value_set_int (value, self->state);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_game_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	PacGame *self = PAC_GAME (object);

	switch (prop_id)
	{
	case PROP_GAME_MAZE:
		g_clear_object (&self->maze);
		self->maze = g_value_dup_object (value);
		break;
	case PROP_GAME_PLAYER:
		g_clear_object (&self->player);
		self->player = g_value_dup_object (value);
		break;
	case PROP_GAME_STATE:
		self->state = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pac_game_class_init (PacGameClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pac_game_finalize;
	object_class->get_property = pac_game_get_property;
	object_class->set_property = pac_game_set_property;

	game_properties[PROP_GAME_MAZE] =
		g_param_spec_object ("maze", "Maze", "The game maze",
		                     PAC_TYPE_MAZE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	game_properties[PROP_GAME_PLAYER] =
		g_param_spec_object ("player", "Player", "The player",
		                     PAC_TYPE_PLAYER,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	game_properties[PROP_GAME_STATE] =
		g_param_spec_int ("state", "State", "Game state",
		                  GAME_STATE_PLAYING, GAME_STATE_PAUSED, GAME_STATE_PLAYING,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_GAME_PROPS, game_properties);
}

static void
pac_game_init (PacGame *self)
{
	self->maze = NULL;
	self->player = NULL;
	self->ghosts = g_ptr_array_new_with_free_func (g_object_unref);
	self->state = GAME_STATE_PLAYING;
	self->total_pellets = 0;
	self->collected_pellets = 0;
}

static PacGame *
pac_game_new (void)
{
	return g_object_new (PAC_TYPE_GAME, NULL);
}

/* =============================================================================
 * HELPER FUNCTIONS
 * ========================================================================== */

static gboolean
check_wall_collision (PacMaze    *maze,
                      GrlVector3 *position)
{
	gint   grid_x;
	gint   grid_z;
	guint  i;

	grid_x = (gint)(position->x / maze->tile_size);
	grid_z = (gint)(position->z / maze->tile_size);

	/* Check if this grid position contains a wall */
	for (i = 0; i < maze->walls->len; i++)
	{
		GrlVector3 *wall;
		gint        wall_x;
		gint        wall_z;

		wall = &g_array_index (maze->walls, GrlVector3, i);
		wall_x = (gint)(wall->x / maze->tile_size);
		wall_z = (gint)(wall->z / maze->tile_size);

		if (wall_x == grid_x && wall_z == grid_z)
			return TRUE;
	}

	return FALSE;
}

static gboolean
check_entity_collision (GrlVector3 *pos1,
                        GrlVector3 *pos2,
                        gfloat      radius)
{
	gfloat dx;
	gfloat dz;
	gfloat dist_sq;

	dx = pos1->x - pos2->x;
	dz = pos1->z - pos2->z;
	dist_sq = dx * dx + dz * dz;

	return dist_sq < (radius * radius);
}

static void
update_camera (CameraMode              mode,
               LrgCameraIsometric     *cam_iso,
               LrgCameraThirdPerson   *cam_tp,
               LrgCameraFirstPerson   *cam_fp,
               PacPlayer              *player,
               LrgInputManager        *input_manager,
               gfloat                  delta_time)
{
	gfloat mouse_dx = 0.0f;
	gfloat mouse_dy = 0.0f;

	lrg_input_manager_get_mouse_delta (input_manager, &mouse_dx, &mouse_dy);

	switch (mode)
	{
	case CAMERA_MODE_ISOMETRIC:
		lrg_camera_isometric_focus_on (cam_iso,
		                               player->position->x,
		                               player->position->y,
		                               player->position->z);
		break;

	case CAMERA_MODE_THIRDPERSON:
		lrg_camera_thirdperson_orbit (cam_tp, mouse_dx, mouse_dy);
		lrg_camera_thirdperson_follow (cam_tp,
		                               player->position->x,
		                               player->position->y,
		                               player->position->z,
		                               delta_time);
		break;

	case CAMERA_MODE_FIRSTPERSON:
		lrg_camera_firstperson_rotate (cam_fp, mouse_dx, mouse_dy);
		lrg_camera_firstperson_set_body_position (cam_fp,
		                                          player->position->x,
		                                          player->position->y,
		                                          player->position->z);
		break;

	default:
		break;
	}
}

static void
render_ui (PacGame *game)
{
	g_autofree gchar     *score_text = NULL;
	g_autofree gchar     *lives_text = NULL;
	g_autoptr(GrlColor)   white = NULL;
	g_autoptr(LrgText2D)  score_label = NULL;
	g_autoptr(LrgText2D)  lives_label = NULL;

	white = grl_color_new (255, 255, 255, 255);

	score_text = g_strdup_printf ("Score: %d", game->player->score);
	lives_text = g_strdup_printf ("Lives: %d", game->player->lives);

	/* Draw UI text using LrgText2D */
	score_label = lrg_text2d_new_full (10.0f, 10.0f, score_text, 20.0f, white);
	lrg_drawable_draw (LRG_DRAWABLE (score_label), 0.0f);

	lives_label = lrg_text2d_new_full (10.0f, 35.0f, lives_text, 20.0f, white);
	lrg_drawable_draw (LRG_DRAWABLE (lives_label), 0.0f);

	if (game->state == GAME_STATE_WIN)
	{
		g_autoptr(LrgText2D) win_label = NULL;

		win_label = lrg_text2d_new_full (200.0f, 300.0f,
		                                 "YOU WIN! Press R to restart",
		                                 40.0f, white);
		lrg_drawable_draw (LRG_DRAWABLE (win_label), 0.0f);
	}
	else if (game->state == GAME_STATE_LOSE)
	{
		g_autoptr(LrgText2D) lose_label = NULL;

		lose_label = lrg_text2d_new_full (180.0f, 300.0f,
		                                  "GAME OVER! Press R to restart",
		                                  40.0f, white);
		lrg_drawable_draw (LRG_DRAWABLE (lose_label), 0.0f);
	}

	if (game->player->power_mode)
	{
		g_autofree gchar     *power_text = NULL;
		g_autoptr(LrgText2D)  power_label = NULL;

		power_text = g_strdup_printf ("POWER MODE: %.1f", game->player->power_time);
		power_label = lrg_text2d_new_full (10.0f, 60.0f, power_text, 20.0f, white);
		lrg_drawable_draw (LRG_DRAWABLE (power_label), 0.0f);
	}
}

/* =============================================================================
 * GAME UPDATE METHODS
 * ========================================================================== */

static void
pac_player_update (PacPlayer       *self,
                   PacMaze         *maze,
                   LrgInputManager *input_manager,
                   gfloat           delta)
{
	g_autoptr(GrlVector3) input_dir = NULL;
	g_autoptr(GrlVector3) new_pos = NULL;
	gfloat                move_x;
	gfloat                move_z;
	gfloat                len;

	input_dir = grl_vector3_new (0.0f, 0.0f, 0.0f);

	/* Get input direction via LrgInputManager */
	if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_W))
		input_dir->z -= 1.0f;
	if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_S))
		input_dir->z += 1.0f;
	if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_A))
		input_dir->x -= 1.0f;
	if (lrg_input_manager_is_key_down (input_manager, GRL_KEY_D))
		input_dir->x += 1.0f;

	/* Normalize and apply speed */
	move_x = input_dir->x;
	move_z = input_dir->z;
	len = sqrtf (move_x * move_x + move_z * move_z);

	if (len > 0.0f)
	{
		move_x /= len;
		move_z /= len;

		new_pos = grl_vector3_new (
			self->position->x + move_x * self->speed * delta,
			self->position->y,
			self->position->z + move_z * self->speed * delta);

		/* Check wall collision */
		if (!check_wall_collision (maze, new_pos))
		{
			g_clear_pointer (&self->position, grl_vector3_free);
			self->position = g_steal_pointer (&new_pos);

			g_clear_pointer (&self->direction, grl_vector3_free);
			self->direction = grl_vector3_new (move_x, 0.0f, move_z);
		}
	}

	/* Update power mode timer */
	if (self->power_mode && self->power_time > 0.0f)
	{
		self->power_time -= delta;
		if (self->power_time <= 0.0f)
			self->power_mode = FALSE;
	}
}

static void
pac_player_render (PacPlayer *self)
{
	g_autoptr(GrlColor)   color = NULL;
	g_autoptr(LrgSphere3D) sphere = NULL;

	color = grl_color_new (255, 255, 0, 255);

	/* Draw player as sphere using LrgSphere3D */
	sphere = lrg_sphere3d_new_full (self->position->x,
	                                self->position->y,
	                                self->position->z,
	                                0.4f,
	                                color);
	lrg_drawable_draw (LRG_DRAWABLE (sphere), 0.0f);

	/* Draw direction indicator */
	if (sqrtf (self->direction->x * self->direction->x +
	           self->direction->z * self->direction->z) > 0.01f)
	{
		g_autoptr(GrlColor)  line_color = NULL;
		g_autoptr(LrgLine3D) line = NULL;

		line_color = grl_color_new (255, 0, 0, 255);
		line = lrg_line3d_new_full (self->position->x,
		                            self->position->y,
		                            self->position->z,
		                            self->position->x + self->direction->x * 0.6f,
		                            self->position->y,
		                            self->position->z + self->direction->z * 0.6f,
		                            line_color);
		lrg_drawable_draw (LRG_DRAWABLE (line), 0.0f);
	}
}

static void
pac_ghost_update (PacGhost  *self,
                  PacPlayer *player,
                  PacMaze   *maze,
                  gfloat     delta)
{
	g_autoptr(GrlVector3) target_pos = NULL;
	g_autoptr(GrlVector3) dir_to_target = NULL;
	g_autoptr(GrlVector3) new_pos = NULL;
	gfloat                dx;
	gfloat                dz;
	gfloat                len;

	/* Determine target based on state */
	switch (self->state)
	{
	case GHOST_STATE_CHASE:
		/* Chase player */
		target_pos = grl_vector3_copy (player->position);
		break;

	case GHOST_STATE_FRIGHTENED:
		/* Run away from player */
		target_pos = grl_vector3_new (
			self->position->x - (player->position->x - self->position->x),
			self->position->y,
			self->position->z - (player->position->z - self->position->z));
		break;

	case GHOST_STATE_DEAD:
		/* Return to spawn */
		target_pos = grl_vector3_copy (self->spawn_point);

		/* Check if reached spawn */
		if (check_entity_collision (self->position, target_pos, 0.5f))
		{
			self->state = GHOST_STATE_CHASE;
		}
		break;

	default:
		target_pos = grl_vector3_copy (self->position);
		break;
	}

	/* Calculate direction to target */
	dx = target_pos->x - self->position->x;
	dz = target_pos->z - self->position->z;
	len = sqrtf (dx * dx + dz * dz);

	if (len > 0.1f)
	{
		dx /= len;
		dz /= len;

		new_pos = grl_vector3_new (
			self->position->x + dx * self->speed * delta,
			self->position->y,
			self->position->z + dz * self->speed * delta);

		/* Check wall collision */
		if (!check_wall_collision (maze, new_pos))
		{
			g_clear_pointer (&self->position, grl_vector3_free);
			self->position = g_steal_pointer (&new_pos);

			g_clear_pointer (&self->direction, grl_vector3_free);
			self->direction = grl_vector3_new (dx, 0.0f, dz);
		}
	}
}

static void
pac_ghost_render (PacGhost *self)
{
	g_autoptr(GrlColor)    render_color = NULL;
	g_autoptr(LrgSphere3D) sphere = NULL;

	/* If frightened, make blue */
	if (self->state == GHOST_STATE_FRIGHTENED)
	{
		render_color = grl_color_new (100, 100, 255, 255);
	}
	else if (self->state == GHOST_STATE_DEAD)
	{
		render_color = grl_color_new (128, 128, 128, 255);
	}
	else
	{
		render_color = grl_color_copy (self->color);
	}

	/* Draw ghost as sphere using LrgSphere3D */
	sphere = lrg_sphere3d_new_full (self->position->x,
	                                self->position->y,
	                                self->position->z,
	                                0.4f,
	                                render_color);
	lrg_drawable_draw (LRG_DRAWABLE (sphere), 0.0f);
}

static void
pac_game_check_collisions (PacGame *self)
{
	guint i;

	/* Check pellet collection */
	for (i = 0; i < self->maze->pellets->len; i++)
	{
		PacPellet *pellet;

		pellet = g_ptr_array_index (self->maze->pellets, i);

		if (!pellet->collected &&
		    check_entity_collision (self->player->position,
		                            pellet->position, 0.5f))
		{
			pellet->collected = TRUE;
			self->player->score += pellet->points;
			self->collected_pellets++;

			/* Power pellet activates power mode */
			if (pellet->is_power_pellet)
			{
				guint j;

				self->player->power_mode = TRUE;
				self->player->power_time = 10.0f;

				/* Frighten all ghosts */
				for (j = 0; j < self->ghosts->len; j++)
				{
					PacGhost *ghost;

					ghost = g_ptr_array_index (self->ghosts, j);
					if (ghost->state != GHOST_STATE_DEAD)
						ghost->state = GHOST_STATE_FRIGHTENED;
				}
			}
		}
	}

	/* Check ghost collisions */
	for (i = 0; i < self->ghosts->len; i++)
	{
		PacGhost *ghost;

		ghost = g_ptr_array_index (self->ghosts, i);

		if (check_entity_collision (self->player->position,
		                            ghost->position, 0.8f))
		{
			if (self->player->power_mode &&
			    ghost->state == GHOST_STATE_FRIGHTENED)
			{
				/* Eat ghost */
				self->player->score += 200;
				ghost->state = GHOST_STATE_DEAD;
			}
			else if (ghost->state != GHOST_STATE_DEAD)
			{
				/* Player dies */
				self->player->lives--;

				if (self->player->lives <= 0)
				{
					self->state = GAME_STATE_LOSE;
				}
				else
				{
					/* Reset positions */
					g_clear_pointer (&self->player->position, grl_vector3_free);
					self->player->position = grl_vector3_copy (self->maze->player_spawn);
				}
			}
		}
	}

	/* Check win condition */
	if (self->collected_pellets >= self->total_pellets)
	{
		self->state = GAME_STATE_WIN;
	}
}

static void
pac_game_update (PacGame         *self,
                 LrgInputManager *input_manager,
                 gfloat           delta)
{
	guint i;

	/* Update player */
	pac_player_update (self->player, self->maze, input_manager, delta);

	/* Update ghosts */
	for (i = 0; i < self->ghosts->len; i++)
	{
		PacGhost *ghost;

		ghost = g_ptr_array_index (self->ghosts, i);
		pac_ghost_update (ghost, self->player, self->maze, delta);
	}

	/* Check collisions */
	pac_game_check_collisions (self);
}

static void
pac_game_render (PacGame *self)
{
	guint i;

	/* Render maze */
	pac_maze_render (self->maze);

	/* Render player */
	pac_player_render (self->player);

	/* Render ghosts */
	for (i = 0; i < self->ghosts->len; i++)
	{
		PacGhost *ghost;

		ghost = g_ptr_array_index (self->ghosts, i);
		pac_ghost_render (ghost);
	}
}

static void
pac_game_reset (PacGame *self)
{
	guint i;

	/* Reset player */
	self->player->score = 0;
	self->player->lives = 3;
	self->player->power_mode = FALSE;
	self->player->power_time = 0.0f;
	g_clear_pointer (&self->player->position, grl_vector3_free);
	self->player->position = grl_vector3_copy (self->maze->player_spawn);

	/* Reset pellets */
	for (i = 0; i < self->maze->pellets->len; i++)
	{
		PacPellet *pellet;

		pellet = g_ptr_array_index (self->maze->pellets, i);
		pellet->collected = FALSE;
	}

	/* Reset ghosts */
	for (i = 0; i < self->ghosts->len; i++)
	{
		PacGhost   *ghost;
		GrlVector3 *spawn;

		ghost = g_ptr_array_index (self->ghosts, i);
		ghost->state = GHOST_STATE_CHASE;

		spawn = &g_array_index (self->maze->ghost_spawns, GrlVector3, i);
		g_clear_pointer (&ghost->position, grl_vector3_free);
		ghost->position = grl_vector3_copy (spawn);
	}

	self->collected_pellets = 0;
	self->state = GAME_STATE_PLAYING;
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError)        error = NULL;
	LrgEngine               *engine;
	LrgRegistry             *registry;
	LrgDataLoader           *loader;
	LrgRenderer             *renderer;
	LrgInputManager         *input_manager;
	g_autoptr(PacGame)       game = NULL;
	g_autoptr(PacMaze)       maze = NULL;
	g_autoptr(PacPlayer)     player = NULL;
	g_autoptr(LrgGrlWindow)       window = NULL;
	g_autoptr(LrgCameraIsometric) camera_iso = NULL;
	g_autoptr(LrgCameraThirdPerson) camera_tp = NULL;
	g_autoptr(LrgCameraFirstPerson) camera_fp = NULL;
	CameraMode                    camera_mode = CAMERA_MODE_ISOMETRIC;
	g_autoptr(GrlColor)           bg_color = NULL;
	guint                         i;

	/* Create window first (before engine startup for graphics) */
	window = lrg_grl_window_new (800, 600, "3D Omnomagon - Libregnum Example");
	lrg_window_set_target_fps (LRG_WINDOW (window), 60);

	/* Initialize engine with window */
	engine = lrg_engine_get_default ();
	lrg_engine_set_window (engine, LRG_WINDOW (window));

	if (!lrg_engine_startup (engine, &error))
	{
		g_error ("Failed to start engine: %s", error->message);
		return 1;
	}

	/* Get renderer (created automatically when window was set) */
	renderer = lrg_engine_get_renderer (engine);

	/* Get input manager */
	input_manager = lrg_input_manager_get_default ();

	/* Register custom types */
	registry = lrg_engine_get_registry (engine);
	lrg_registry_register (registry, "pac-pellet", PAC_TYPE_PELLET);
	lrg_registry_register (registry, "pac-player", PAC_TYPE_PLAYER);
	lrg_registry_register (registry, "pac-ghost", PAC_TYPE_GHOST);
	lrg_registry_register (registry, "pac-maze", PAC_TYPE_MAZE);
	lrg_registry_register (registry, "pac-game", PAC_TYPE_GAME);

	/* Load maze from YAML */
	loader = lrg_engine_get_data_loader (engine);
	g_object_set (loader, "registry", registry, NULL);

	maze = PAC_MAZE (lrg_data_loader_load_file (loader, "data/omnomagon-maze.yaml", &error));
	if (maze == NULL)
	{
		g_error ("Failed to load maze: %s", error->message);
		return 1;
	}

	/* Layout is automatically parsed when loaded from YAML via set_property */

	/* Create player */
	player = pac_player_new (maze->player_spawn);

	/* Create ghosts */
	{
		g_autoptr(GrlColor) red = grl_color_new (255, 0, 0, 255);
		g_autoptr(GrlColor) pink = grl_color_new (255, 184, 255, 255);
		g_autoptr(GrlColor) cyan = grl_color_new (0, 255, 255, 255);
		g_autoptr(GrlColor) orange = grl_color_new (255, 184, 82, 255);
		GrlColor           *ghost_colors[4];
		guint               num_ghosts;

		ghost_colors[0] = red;
		ghost_colors[1] = pink;
		ghost_colors[2] = cyan;
		ghost_colors[3] = orange;

		num_ghosts = MIN (maze->ghost_spawns->len, 4);

		game = pac_game_new ();
		g_object_set (game,
		              "maze", maze,
		              "player", player,
		              NULL);

		for (i = 0; i < num_ghosts; i++)
		{
			GrlVector3       *spawn;
			g_autoptr(PacGhost) ghost = NULL;

			spawn = &g_array_index (maze->ghost_spawns, GrlVector3, i);
			ghost = pac_ghost_new (spawn, ghost_colors[i]);
			g_ptr_array_add (game->ghosts, g_steal_pointer (&ghost));
		}
	}

	/* Count total pellets */
	game->total_pellets = maze->pellets->len;

	/* Create all three cameras */
	camera_iso = lrg_camera_isometric_new ();
	lrg_camera_isometric_set_tile_width (camera_iso, 1.0f);
	lrg_camera_isometric_set_tile_height (camera_iso, 0.5f);
	lrg_camera_isometric_set_zoom (camera_iso, 0.05f);

	camera_tp = lrg_camera_thirdperson_new ();
	lrg_camera_thirdperson_set_distance (camera_tp, 8.0f);
	lrg_camera_thirdperson_set_pitch (camera_tp, 35.0f);
	lrg_camera_thirdperson_set_height_offset (camera_tp, 0.5f);

	camera_fp = lrg_camera_firstperson_new ();
	lrg_camera_firstperson_set_eye_height (camera_fp, 0.5f);

	/* Set initial camera (isometric) on renderer */
	lrg_renderer_set_camera (renderer, LRG_CAMERA (camera_iso));
	bg_color = grl_color_new (0, 0, 0, 255);

	/* Main game loop */
	while (!lrg_window_should_close (LRG_WINDOW (window)))
	{
		gfloat delta;

		delta = lrg_window_get_frame_time (LRG_WINDOW (window));

		/* Poll input */
		lrg_input_manager_poll (input_manager);

		/* Update */
		if (game->state == GAME_STATE_PLAYING)
		{
			pac_game_update (game, input_manager, delta);
		}

		/* Reset on R key */
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_R))
		{
			pac_game_reset (game);
		}

		/* Cycle camera mode with C key */
		if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_C))
		{
			camera_mode = (camera_mode + 1) % CAMERA_MODE_COUNT;

			/* Update renderer's active camera */
			switch (camera_mode)
			{
			case CAMERA_MODE_ISOMETRIC:
				lrg_renderer_set_camera (renderer, LRG_CAMERA (camera_iso));
				break;
			case CAMERA_MODE_THIRDPERSON:
				lrg_renderer_set_camera (renderer, LRG_CAMERA (camera_tp));
				/* Snap to player on switch */
				lrg_camera_thirdperson_snap_to_target (camera_tp,
				                                       game->player->position->x,
				                                       game->player->position->y,
				                                       game->player->position->z);
				break;
			case CAMERA_MODE_FIRSTPERSON:
				lrg_renderer_set_camera (renderer, LRG_CAMERA (camera_fp));
				break;
			default:
				break;
			}
		}

		/* Render using new graphics system */
		lrg_renderer_begin_frame (renderer);
		lrg_renderer_clear (renderer, bg_color);

		/* Update camera position */
		update_camera (camera_mode, camera_iso, camera_tp, camera_fp,
		               game->player, input_manager, delta);

		/* Render world layer (with camera transform) */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);
		pac_game_render (game);
		lrg_renderer_end_layer (renderer);

		/* Render UI layer (no camera transform) */
		lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
		render_ui (game);
		lrg_renderer_end_layer (renderer);

		lrg_renderer_end_frame (renderer);
	}

	/* Cleanup */
	lrg_engine_shutdown (engine);

	return 0;
}
