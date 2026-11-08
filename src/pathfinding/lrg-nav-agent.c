/* lrg-nav-agent.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Navigation agents and the crowd that steps them.
 *
 * Third-party code: the ORCA routines in the section "ORCA (port of
 * RVO2)" below (the obstacle and agent half-plane construction of
 * Agent::computeNewVelocity() and linearProgram1/2/3()) are a C port of
 * the RVO2 Library 2.0, Agent.cpp:
 *
 *     Copyright 2008 University of North Carolina at Chapel Hill
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 *     implied.  See the License for the specific language governing
 *     permissions and limitations under the License.
 *
 *     https://gamma.cs.unc.edu/RVO2/
 *
 * Changes from the original: ported from C++ to C (gnu89, single
 * precision as the original); obstacles are the navigation mesh's
 * boundary segments, each a two-vertex obstacle (both vertices convex)
 * chosen from the side the agent is on, gathered from the mesh's
 * spatial buckets instead of RVO2's obstacle k-d tree; the reciprocal
 * share of an agent pair is 0.5 shifted by the difference of their
 * avoidance priorities (0.5 in RVO2), and 1 against agents that do not
 * avoid; neighbours are sorted by distance then index for determinism.
 * The rest of this file (paths, steering, links, the crowd) is
 * libregnum's own (AGPL-3.0-or-later).
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-nav-agent.h"
#include "lrg-nav-poly-mesh-private.h"
#include "lrg-nav-agent-private.h"

#define RVO_EPSILON      (0.00001f)
#define MAX_PATH_NODES   (8192u)
#define LINK_REACH       (0.35f)    /* how close to a link's start counts as there */
#define BLOCK_CHECK_TIME (0.5f)     /* seconds between "is my corner still visible" checks */
#define SKIP_CHECK_TIME  (0.1f)     /* seconds between "can I see the corner after it" checks */
#define PUSH_ITERATIONS  (4u)

/* a half-plane of permitted velocities: left of point + t * direction */
typedef struct
{
	gfloat point[2];
	gfloat direction[2];
} OrcaLine;

/* a vertex of a two-vertex obstacle (one boundary segment) */
typedef struct
{
	gfloat point[2];
	gfloat unit_dir[2];
	guint  next;
	guint  prev;
} ObstVertex;

/* signals queued during a step, flushed after it */
enum
{
	PENDING_ARRIVED     = 1 << 0,
	PENDING_PATH_FAILED = 1 << 1,
	PENDING_LINK_ENTER  = 1 << 2,
	PENDING_LINK_EXIT   = 1 << 3
};

struct _LrgNavAgent
{
	GObject parent_instance;

	LrgNavPolyMesh *mesh;       /* owned ref or NULL */
	LrgNavCrowd    *crowd;      /* weak: the crowd clears it */
	guint           mesh_serial;
	guint           region;     /* the mesh region it stands in, 0 unknown */

	/* kinematic state */
	gfloat position[3];
	gfloat velocity[3];
	gfloat desired[3];          /* preferred velocity of the last step */
	gfloat new_velocity[2];     /* ORCA result (crowd scratch) */

	/* settings */
	gfloat   radius;
	gfloat   height;
	gfloat   max_speed;
	gfloat   max_accel;
	gfloat   priority;
	gboolean avoidance;
	gfloat   neighbour_dist;
	guint    max_neighbours;
	gfloat   time_horizon;
	gfloat   time_horizon_obst;
	gfloat   stopping_distance;
	gfloat   slowing_distance;
	gfloat   separation_weight;
	gfloat   repath_interval;

	/* destination and path */
	LrgNavAgentState state;
	gboolean has_destination;
	gfloat   destination[3];
	GArray  *path;              /* gfloat, 3 per point */
	GArray  *link_starts;       /* guint: point indices where links begin */
	guint    corner;
	gboolean repath;
	gfloat   repath_timer;
	gfloat   block_timer;
	gfloat   skip_timer;

	/* link traversal */
	gfloat   link_from[3];
	gfloat   link_to[3];
	gfloat   link_t;
	gfloat   link_length;

	LrgNavPolyFilterFunc filter;
	gpointer             filter_data;
	GDestroyNotify       filter_destroy;

	guint    pending;

	/* debug: the last step's ORCA lines and neighbours */
	GArray  *debug_lines;       /* OrcaLine */
	GArray  *debug_neighbours;  /* guint agent indices */
	GArray  *debug_walls;       /* guint boundary segment indices */
};

struct _LrgNavCrowd
{
	GObject parent_instance;

	LrgNavPolyMesh *mesh;
	GPtrArray      *agents;     /* LrgNavAgent* (owned refs) */
	LrgNavAgent    *debug_agent; /* weak: ORCA / neighbours only for it */
	guint64         steps;
	gdouble         last_seconds;

	/* scratch */
	GArray         *grid_start; /* guint per cell + 1: where its agents start */
	GArray         *grid_items; /* guint agent indices, cell by cell */
	gint32          grid_w;
	gint32          grid_d;
	gfloat          grid_min[2];
	gfloat          grid_cell;
	gfloat          max_radius;
	GArray         *candidates; /* guint */
	GArray         *walls;      /* guint32 */
	GArray         *lines;      /* OrcaLine */
	GArray         *proj;       /* OrcaLine */
	GArray         *verts;      /* ObstVertex */
};

G_DEFINE_FINAL_TYPE (LrgNavAgent, lrg_nav_agent, G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE (LrgNavCrowd, lrg_nav_crowd, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_RADIUS,
	PROP_HEIGHT,
	PROP_MAX_SPEED,
	PROP_MAX_ACCELERATION,
	PROP_AVOIDANCE_PRIORITY,
	PROP_AVOIDANCE_ENABLED,
	PROP_NEIGHBOUR_DISTANCE,
	PROP_MAX_NEIGHBOURS,
	PROP_TIME_HORIZON,
	PROP_OBSTACLE_TIME_HORIZON,
	PROP_STOPPING_DISTANCE,
	PROP_SLOWING_DISTANCE,
	PROP_SEPARATION_WEIGHT,
	PROP_REPATH_INTERVAL,
	PROP_STATE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_ARRIVED,
	SIGNAL_PATH_FAILED,
	SIGNAL_LINK_ENTERED,
	SIGNAL_LINK_EXITED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
	CROWD_PROP_0,
	CROWD_PROP_MESH,
	CROWD_N_PROPS
};

static GParamSpec *crowd_properties[CROWD_N_PROPS];

enum
{
	CROWD_SIGNAL_STEPPED,
	CROWD_N_SIGNALS
};

static guint crowd_signals[CROWD_N_SIGNALS];

GType
lrg_nav_agent_state_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_NAV_AGENT_STATE_IDLE, "LRG_NAV_AGENT_STATE_IDLE", "idle" },
			{ LRG_NAV_AGENT_STATE_MOVING, "LRG_NAV_AGENT_STATE_MOVING", "moving" },
			{ LRG_NAV_AGENT_STATE_ON_LINK, "LRG_NAV_AGENT_STATE_ON_LINK", "on-link" },
			{ LRG_NAV_AGENT_STATE_ARRIVED, "LRG_NAV_AGENT_STATE_ARRIVED", "arrived" },
			{ LRG_NAV_AGENT_STATE_FAILED, "LRG_NAV_AGENT_STATE_FAILED", "failed" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgNavAgentState"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Small vector helpers (X/Z plane as 2D x/y)
 * ========================================================================== */

static gfloat
det2 (const gfloat *a,
      const gfloat *b)
{
	return a[0] * b[1] - a[1] * b[0];
}

static gfloat
dot2 (const gfloat *a,
      const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1];
}

static gfloat
abs_sq2 (const gfloat *a)
{
	return a[0] * a[0] + a[1] * a[1];
}

static void
normalize2 (gfloat *a)
{
	gfloat len = sqrtf (abs_sq2 (a));

	if (len > 0.0f)
	{
		a[0] /= len;
		a[1] /= len;
	}
}

static gfloat
dist2_xz (const gfloat *a,
          const gfloat *b)
{
	gfloat dx = a[0] - b[0];
	gfloat dz = a[2] - b[2];

	return sqrtf (dx * dx + dz * dz);
}

/* ==========================================================================
 * LrgNavAgent: GObject
 * ========================================================================== */

static void
set_state (LrgNavAgent      *self,
           LrgNavAgentState  state)
{
	if (self->state == state)
		return;
	self->state = state;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

static void
lrg_nav_agent_finalize (GObject *object)
{
	LrgNavAgent *self = LRG_NAV_AGENT (object);

	if (self->filter_destroy != NULL)
		self->filter_destroy (self->filter_data);
	g_clear_object (&self->mesh);
	g_array_unref (self->path);
	g_array_unref (self->link_starts);
	g_array_unref (self->debug_lines);
	g_array_unref (self->debug_neighbours);
	g_array_unref (self->debug_walls);
	G_OBJECT_CLASS (lrg_nav_agent_parent_class)->finalize (object);
}

static gfloat *
agent_float_field (LrgNavAgent *self,
                   guint        prop_id)
{
	switch (prop_id)
	{
	case PROP_RADIUS:                return &self->radius;
	case PROP_HEIGHT:                return &self->height;
	case PROP_MAX_SPEED:             return &self->max_speed;
	case PROP_MAX_ACCELERATION:      return &self->max_accel;
	case PROP_AVOIDANCE_PRIORITY:    return &self->priority;
	case PROP_NEIGHBOUR_DISTANCE:    return &self->neighbour_dist;
	case PROP_TIME_HORIZON:          return &self->time_horizon;
	case PROP_OBSTACLE_TIME_HORIZON: return &self->time_horizon_obst;
	case PROP_STOPPING_DISTANCE:     return &self->stopping_distance;
	case PROP_SLOWING_DISTANCE:      return &self->slowing_distance;
	case PROP_SEPARATION_WEIGHT:     return &self->separation_weight;
	case PROP_REPATH_INTERVAL:       return &self->repath_interval;
	default:                         return NULL;
	}
}

static void
lrg_nav_agent_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgNavAgent *self = LRG_NAV_AGENT (object);
	gfloat      *f = agent_float_field (self, prop_id);

	if (f != NULL)
	{
		g_value_set_float (value, *f);
		return;
	}
	switch (prop_id)
	{
	case PROP_AVOIDANCE_ENABLED: g_value_set_boolean (value, self->avoidance); break;
	case PROP_MAX_NEIGHBOURS:    g_value_set_uint (value, self->max_neighbours); break;
	case PROP_STATE:             g_value_set_enum (value, (gint) self->state); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_nav_agent_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgNavAgent *self = LRG_NAV_AGENT (object);
	gfloat      *f = agent_float_field (self, prop_id);

	if (f != NULL)
	{
		if (*f != g_value_get_float (value))
		{
			*f = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		return;
	}
	switch (prop_id)
	{
	case PROP_AVOIDANCE_ENABLED:
		if (self->avoidance != g_value_get_boolean (value))
		{
			self->avoidance = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_MAX_NEIGHBOURS:
		if (self->max_neighbours != g_value_get_uint (value))
		{
			self->max_neighbours = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static GParamSpec *
agent_float_pspec (const gchar *name,
                   const gchar *blurb,
                   gfloat       min,
                   gfloat       max,
                   gfloat       def)
{
	return g_param_spec_float (name, NULL, blurb, min, max, def,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_nav_agent_class_init (LrgNavAgentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_nav_agent_finalize;
	object_class->get_property = lrg_nav_agent_get_property;
	object_class->set_property = lrg_nav_agent_set_property;

	/**
	 * LrgNavAgent:radius:
	 *
	 * The agent's radius for avoidance, in metres.  Bake the mesh with
	 * the same (or the largest) radius: the mesh is eroded by the bake
	 * radius, walls only keep the centre on it.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_RADIUS] = agent_float_pspec ("radius", "Radius", 0.01f, 100.0f, 0.3f);

	/**
	 * LrgNavAgent:height:
	 *
	 * The agent's height; agents whose feet are further apart vertically
	 * than the larger height ignore each other (other floors).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_HEIGHT] = agent_float_pspec ("height", "Height", 0.01f, 100.0f, 1.8f);

	/**
	 * LrgNavAgent:max-speed:
	 *
	 * Top speed in metres per second.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MAX_SPEED] = agent_float_pspec ("max-speed", "Max speed", 0.0f, 1000.0f, 3.5f);

	/**
	 * LrgNavAgent:max-acceleration:
	 *
	 * How fast the velocity may change, in metres per second squared.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MAX_ACCELERATION] = agent_float_pspec ("max-acceleration", "Max acceleration",
	                                                       0.01f, 10000.0f, 8.0f);

	/**
	 * LrgNavAgent:avoidance-priority:
	 *
	 * 0 .. 1.  Two agents share avoiding each other by 0.5 each when
	 * their priorities are equal; the share shifts by half the
	 * difference, so a priority-1 agent never yields to a priority-0 one.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_AVOIDANCE_PRIORITY] = agent_float_pspec ("avoidance-priority", "Avoidance priority",
	                                                         0.0f, 1.0f, 0.5f);

	/**
	 * LrgNavAgent:avoidance-enabled:
	 *
	 * Whether the agent runs ORCA in a crowd.  Others avoid it either
	 * way.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_AVOIDANCE_ENABLED] =
		g_param_spec_boolean ("avoidance-enabled", NULL, "Avoid others", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNavAgent:neighbour-distance:
	 *
	 * How far (centre to centre, metres) other agents are considered;
	 * at least #LrgNavAgent:max-speed x #LrgNavAgent:time-horizon is
	 * advisable.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_NEIGHBOUR_DISTANCE] = agent_float_pspec ("neighbour-distance", "Neighbour distance",
	                                                         0.0f, 1000.0f, 8.0f);

	/**
	 * LrgNavAgent:max-neighbours:
	 *
	 * How many of the nearest agents ORCA considers.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_MAX_NEIGHBOURS] =
		g_param_spec_uint ("max-neighbours", NULL, "Max neighbours", 0, 256, 10,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNavAgent:time-horizon:
	 *
	 * Seconds ahead ORCA keeps the agent's velocity collision-free
	 * against other agents: larger reacts earlier and more cautiously.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_TIME_HORIZON] = agent_float_pspec ("time-horizon", "Time horizon", 0.01f, 100.0f, 2.0f);

	/**
	 * LrgNavAgent:obstacle-time-horizon:
	 *
	 * Seconds ahead ORCA keeps the velocity clear of the mesh's walls.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_OBSTACLE_TIME_HORIZON] = agent_float_pspec ("obstacle-time-horizon", "Obstacle time horizon",
	                                                            0.01f, 100.0f, 1.0f);

	/**
	 * LrgNavAgent:stopping-distance:
	 *
	 * The agent has arrived when the destination is this close (X/Z).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_STOPPING_DISTANCE] = agent_float_pspec ("stopping-distance", "Stopping distance",
	                                                        0.0f, 1000.0f, 0.1f);

	/**
	 * LrgNavAgent:slowing-distance:
	 *
	 * Arrive steering: within this distance of the destination the
	 * desired speed falls linearly to zero.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_SLOWING_DISTANCE] = agent_float_pspec ("slowing-distance", "Slowing distance",
	                                                       0.0f, 1000.0f, 1.0f);

	/**
	 * LrgNavAgent:separation-weight:
	 *
	 * Separation steering added to the preferred velocity: a push away
	 * from neighbours closer than twice their combined radii, scaled by
	 * this weight (0 leaves spacing to ORCA alone).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_SEPARATION_WEIGHT] = agent_float_pspec ("separation-weight", "Separation weight",
	                                                        0.0f, 100.0f, 0.0f);

	/**
	 * LrgNavAgent:repath-interval:
	 *
	 * Seconds between automatic replans of a moving agent (0: only when
	 * the mesh changes, lrg_nav_agent_request_repath() is called or the
	 * way to the next corner is blocked).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_REPATH_INTERVAL] = agent_float_pspec ("repath-interval", "Repath interval",
	                                                      0.0f, 3600.0f, 0.0f);

	/**
	 * LrgNavAgent:state:
	 *
	 * What the agent is doing (#LrgNavAgentState).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_STATE] =
		g_param_spec_enum ("state", NULL, "State", LRG_TYPE_NAV_AGENT_STATE, LRG_NAV_AGENT_STATE_IDLE,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgNavAgent::arrived:
	 * @agent: the agent
	 *
	 * The agent came within #LrgNavAgent:stopping-distance of its
	 * destination.  In a crowd it is emitted after the step.
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_ARRIVED] = g_signal_new ("arrived", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                                        0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgNavAgent::path-failed:
	 * @agent: the agent
	 *
	 * No path to the destination was found (the agent is
	 * %LRG_NAV_AGENT_STATE_FAILED).
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_PATH_FAILED] = g_signal_new ("path-failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                                            0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgNavAgent::link-entered:
	 * @agent: the agent
	 *
	 * The agent started crossing an off-mesh link (play a climb or jump
	 * animation).
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_LINK_ENTERED] = g_signal_new ("link-entered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                                             0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgNavAgent::link-exited:
	 * @agent: the agent
	 *
	 * The agent reached the far end of the link it was crossing.
	 *
	 * Since: 0.9.0
	 */
	signals[SIGNAL_LINK_EXITED] = g_signal_new ("link-exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                                            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_nav_agent_init (LrgNavAgent *self)
{
	self->radius = 0.3f;
	self->height = 1.8f;
	self->max_speed = 3.5f;
	self->max_accel = 8.0f;
	self->priority = 0.5f;
	self->avoidance = TRUE;
	self->neighbour_dist = 8.0f;
	self->max_neighbours = 10;
	self->time_horizon = 2.0f;
	self->time_horizon_obst = 1.0f;
	self->stopping_distance = 0.1f;
	self->slowing_distance = 1.0f;
	self->state = LRG_NAV_AGENT_STATE_IDLE;
	self->path = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->link_starts = g_array_new (FALSE, FALSE, sizeof (guint));
	self->debug_lines = g_array_new (FALSE, FALSE, sizeof (OrcaLine));
	self->debug_neighbours = g_array_new (FALSE, FALSE, sizeof (guint));
	self->debug_walls = g_array_new (FALSE, FALSE, sizeof (guint32));
}

LrgNavAgent *
lrg_nav_agent_new (void)
{
	return g_object_new (LRG_TYPE_NAV_AGENT, NULL);
}

/* ==========================================================================
 * LrgNavAgent: path planning and following
 * ========================================================================== */

static void
query_extents (LrgNavAgent *self,
               gfloat      *ext)
{
	ext[0] = 1.0f + self->radius;
	ext[1] = MAX (self->height, 1.0f);
	ext[2] = 1.0f + self->radius;
}

static gboolean
mesh_ready (LrgNavAgent *self)
{
	return self->mesh != NULL && lrg_nav_poly_mesh_get_is_baked (self->mesh);
}

static gboolean
is_link_start (LrgNavAgent *self,
               guint        point)
{
	guint i;

	for (i = 0; i < self->link_starts->len; i++)
		if (g_array_index (self->link_starts, guint, i) == point)
			return TRUE;
	return FALSE;
}

static guint
n_points (LrgNavAgent *self)
{
	return self->path->len / 3;
}

static const gfloat *
point_at (LrgNavAgent *self,
          guint        i)
{
	return &g_array_index (self->path, gfloat, i * 3);
}

/*
 * plan:
 * Finds a path from the agent to its destination.  Success: MOVING,
 * steering to point 1.  Failure: FAILED and path-failed queued.
 */
static gboolean
plan (LrgNavAgent *self)
{
	g_autofree gfloat *pts = NULL;
	guint              n_floats = 0;
	gfloat             ext[3];

	self->repath = FALSE;
	self->repath_timer = 0.0f;
	self->block_timer = 0.0f;
	g_array_set_size (self->path, 0);
	g_array_set_size (self->link_starts, 0);
	self->corner = 0;
	if (!self->has_destination)
		return FALSE;
	/* remember the mesh's state even when it cannot answer yet: plan again
	 * only when it changes, not every step */
	if (self->mesh != NULL)
		self->mesh_serial = lrg_nav_poly_mesh_get_serial (self->mesh);
	if (mesh_ready (self))
	{
		query_extents (self, ext);
		pts = _lrg_nav_poly_mesh_find_path_links (self->mesh, self->position, self->destination, ext,
		                                          MAX_PATH_NODES, self->filter, self->filter_data,
		                                          &n_floats, self->link_starts, NULL);
	}
	if (pts == NULL || n_floats < 6)
	{
		g_array_set_size (self->link_starts, 0);
		set_state (self, LRG_NAV_AGENT_STATE_FAILED);
		self->pending |= PENDING_PATH_FAILED;
		return FALSE;
	}
	g_array_append_vals (self->path, pts, n_floats);
	/* the destination as the mesh knows it */
	memcpy (self->destination, point_at (self, n_points (self) - 1), sizeof (gfloat) * 3);
	self->corner = 1;
	set_state (self, LRG_NAV_AGENT_STATE_MOVING);
	return TRUE;
}

/* the speed the agent wants at distance @d from its destination */
static gfloat
arrive_speed (LrgNavAgent *self,
              gfloat       d)
{
	if (self->slowing_distance <= 0.0f || d >= self->slowing_distance)
		return self->max_speed;
	return self->max_speed * MAX (d / self->slowing_distance, 0.15f);
}

static void
start_link (LrgNavAgent *self)
{
	const gfloat *a = point_at (self, self->corner);
	const gfloat *b = point_at (self, self->corner + 1);

	memcpy (self->link_from, self->position, sizeof (self->link_from));
	memcpy (self->link_to, b, sizeof (self->link_to));
	(void) a;
	self->link_t = 0.0f;
	self->link_length = MAX (sqrtf ((b[0] - self->position[0]) * (b[0] - self->position[0]) +
	                                (b[1] - self->position[1]) * (b[1] - self->position[1]) +
	                                (b[2] - self->position[2]) * (b[2] - self->position[2])), 1e-3f);
	set_state (self, LRG_NAV_AGENT_STATE_ON_LINK);
	self->pending |= PENDING_LINK_ENTER;
}

/*
 * prepare:
 * Replans when needed, advances the corner, starts links and arrives;
 * leaves the preferred velocity in self->desired.
 */
static void
prepare (LrgNavAgent *self,
         gfloat       delta)
{
	guint  n;
	gfloat reach;

	self->desired[0] = self->desired[1] = self->desired[2] = 0.0f;
	if (!self->has_destination || self->state == LRG_NAV_AGENT_STATE_ON_LINK)
		return;

	/* --- replanning triggers --- */
	if (self->mesh != NULL && lrg_nav_poly_mesh_get_serial (self->mesh) != self->mesh_serial)
		self->repath = TRUE;
	if (self->state == LRG_NAV_AGENT_STATE_MOVING && self->repath_interval > 0.0f)
	{
		self->repath_timer += delta;
		if (self->repath_timer >= self->repath_interval)
			self->repath = TRUE;
	}
	if (self->repath)
	{
		if (self->state == LRG_NAV_AGENT_STATE_ARRIVED)
			self->repath = FALSE;
		else
			plan (self);
	}
	if (self->state != LRG_NAV_AGENT_STATE_MOVING)
		return;

	/* --- advance past the corners already reached --- */
	n = n_points (self);
	reach = MAX (self->radius * 0.5f, 0.05f);
	while (self->corner < n - 1)
	{
		const gfloat *c = point_at (self, self->corner);

		if (is_link_start (self, self->corner))
		{
			if (dist2_xz (self->position, c) <= LINK_REACH)
			{
				start_link (self);
				return;
			}
			break;
		}
		if (dist2_xz (self->position, c) > reach)
			break;
		self->corner++;
		self->block_timer = 0.0f;
	}

	/* --- skip a corner when the one after it is in plain sight: an agent
	 * pushed off its line by others never has to reach the exact corner
	 * point (crowds would pile up there) --- */
	self->skip_timer += delta;
	if (self->skip_timer >= SKIP_CHECK_TIME && mesh_ready (self) && self->corner + 1 < n &&
	    !is_link_start (self, self->corner))
	{
		gfloat ext[3];
		gfloat f = 0.0f;

		self->skip_timer = 0.0f;
		query_extents (self, ext);
		if (!lrg_nav_poly_mesh_raycast (self->mesh, self->position, point_at (self, self->corner + 1), ext,
		                                self->filter, self->filter_data, &f, NULL) &&
		    fabsf (point_at (self, self->corner + 1)[1] - self->position[1]) <= self->height)
		{
			self->corner++;
			self->block_timer = 0.0f;
		}
	}

	/* --- arrived? --- */
	if (self->corner >= n - 1 &&
	    dist2_xz (self->position, self->destination) <= MAX (self->stopping_distance, 1e-3f))
	{
		set_state (self, LRG_NAV_AGENT_STATE_ARRIVED);
		self->pending |= PENDING_ARRIVED;
		return;
	}

	/* --- blocked corner: replan (checked every BLOCK_CHECK_TIME) --- */
	self->block_timer += delta;
	if (self->block_timer >= BLOCK_CHECK_TIME && mesh_ready (self))
	{
		gfloat ext[3];

		self->block_timer = 0.0f;
		query_extents (self, ext);
		{
			gfloat f = 1.0f;

			/* replan only when the walk stops well short: corners sit on edges */
			if (lrg_nav_poly_mesh_raycast (self->mesh, self->position, point_at (self, self->corner), ext,
			                               self->filter, self->filter_data, &f, NULL) &&
			    f < 0.9f && !plan (self))
				return;
		}
	}

	/* --- steer: towards the corner, arriving at the last one --- */
	n = n_points (self);
	if (self->corner >= n)
		return;
	{
		const gfloat *c = point_at (self, self->corner);
		gfloat        dir[2];
		gfloat        d;
		gfloat        speed;

		dir[0] = c[0] - self->position[0];
		dir[1] = c[2] - self->position[2];
		d = sqrtf (abs_sq2 (dir));
		if (d < 1e-6f)
			return;
		speed = self->max_speed;
		if (self->corner >= n - 1)
			speed = arrive_speed (self, d);
		else if (is_link_start (self, self->corner))
			speed = arrive_speed (self, d + LINK_REACH);
		/* never overshoot the target in one step */
		if (delta > 0.0f)
			speed = MIN (speed, d / delta);
		self->desired[0] = dir[0] / d * speed;
		self->desired[2] = dir[1] / d * speed;
	}
}

/* moves along an off-mesh link; returns TRUE when it was crossed */
static void
advance_link (LrgNavAgent *self,
              gfloat       delta)
{
	guint k;

	self->link_t += (self->max_speed * delta) / self->link_length;
	if (self->link_t >= 1.0f)
	{
		memcpy (self->position, self->link_to, sizeof (self->link_to));
		self->corner += 1;
		set_state (self, LRG_NAV_AGENT_STATE_MOVING);
		self->pending |= PENDING_LINK_EXIT;
		self->velocity[0] = self->velocity[1] = self->velocity[2] = 0.0f;
		/* the link may end at the destination */
		if (self->corner >= n_points (self) - 1 &&
		    dist2_xz (self->position, self->destination) <= MAX (self->stopping_distance, 1e-3f))
		{
			set_state (self, LRG_NAV_AGENT_STATE_ARRIVED);
			self->pending |= PENDING_ARRIVED;
		}
		return;
	}
	for (k = 0; k < 3; k++)
	{
		gfloat p = self->link_from[k] + (self->link_to[k] - self->link_from[k]) * self->link_t;

		self->velocity[k] = delta > 0.0f ? (p - self->position[k]) / delta : 0.0f;
		self->position[k] = p;
	}
	self->velocity[1] = 0.0f;
}

/* snaps @pos onto the mesh near it; FALSE (and @pos untouched) when off */
static gboolean
project (LrgNavAgent *self,
         gfloat      *pos)
{
	gfloat ext[3];
	gfloat out[3];
	guint  poly = 0;

	if (!mesh_ready (self))
		return TRUE;

	ext[0] = MAX (0.5f, self->radius * 2.0f);
	ext[1] = MAX (self->height * 0.5f, 0.5f);
	ext[2] = ext[0];
	/* stay in the region it walks: never hop onto an island nearby */
	if (self->region != 0 && self->mesh_serial == lrg_nav_poly_mesh_get_serial (self->mesh))
	{
		if (!lrg_nav_poly_mesh_find_nearest_in_region (self->mesh, pos, ext, self->region, out, &poly))
			return FALSE;
	}
	else if (!lrg_nav_poly_mesh_find_nearest (self->mesh, pos, ext, out, &poly))
		return FALSE;
	self->region = lrg_nav_poly_mesh_get_polygon_region (self->mesh, poly);
	memcpy (pos, out, sizeof (out));
	return TRUE;
}

/* applies the acceleration limit to reach @target (2D) and integrates */
static void
integrate (LrgNavAgent  *self,
           const gfloat *target,
           gfloat        delta)
{
	gfloat dv[2];
	gfloat max_dv = self->max_accel * delta;
	gfloat len;
	gfloat next[3];

	dv[0] = target[0] - self->velocity[0];
	dv[1] = target[1] - self->velocity[2];
	len = sqrtf (abs_sq2 (dv));
	if (len > max_dv && len > 0.0f)
	{
		dv[0] *= max_dv / len;
		dv[1] *= max_dv / len;
	}
	self->velocity[0] += dv[0];
	self->velocity[2] += dv[1];
	self->velocity[1] = 0.0f;
	/* a stopped agent with nowhere to go stays put */
	if (abs_sq2 (dv) < 1e-12f && self->velocity[0] == 0.0f && self->velocity[2] == 0.0f)
		return;
	next[0] = self->position[0] + self->velocity[0] * delta;
	next[1] = self->position[1];
	next[2] = self->position[2] + self->velocity[2] * delta;
	if (project (self, next))
		memcpy (self->position, next, sizeof (next));
	else
		self->velocity[0] = self->velocity[2] = 0.0f;
}

static void
flush_signals (LrgNavAgent *self)
{
	guint pending = self->pending;

	self->pending = 0;
	g_object_ref (self);
	if (pending & PENDING_PATH_FAILED)
		g_signal_emit (self, signals[SIGNAL_PATH_FAILED], 0);
	if (pending & PENDING_LINK_ENTER)
		g_signal_emit (self, signals[SIGNAL_LINK_ENTERED], 0);
	if (pending & PENDING_LINK_EXIT)
		g_signal_emit (self, signals[SIGNAL_LINK_EXITED], 0);
	if (pending & PENDING_ARRIVED)
		g_signal_emit (self, signals[SIGNAL_ARRIVED], 0);
	g_object_unref (self);
}

/* ==========================================================================
 * LrgNavAgent: public API
 * ========================================================================== */

LrgNavPolyMesh *
lrg_nav_agent_get_mesh (LrgNavAgent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), NULL);
	return self->mesh;
}

void
lrg_nav_agent_set_mesh (LrgNavAgent    *self,
                        LrgNavPolyMesh *mesh)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));
	g_return_if_fail (mesh == NULL || LRG_IS_NAV_POLY_MESH (mesh));

	if (!g_set_object (&self->mesh, mesh))
		return;
	self->mesh_serial = 0;
	self->region = 0;
	if (self->has_destination && self->state != LRG_NAV_AGENT_STATE_ARRIVED)
		self->repath = TRUE;
}

void
lrg_nav_agent_get_position (LrgNavAgent *self,
                            gfloat      *out)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->position, sizeof (self->position));
}

gboolean
lrg_nav_agent_warp (LrgNavAgent  *self,
                    const gfloat *position)
{
	gfloat   p[3];
	gboolean on;

	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), FALSE);
	g_return_val_if_fail (position != NULL, FALSE);

	memcpy (p, position, sizeof (p));
	on = TRUE;
	if (mesh_ready (self))
	{
		gfloat ext[3];
		gfloat out[3];
		guint  poly = 0;

		query_extents (self, ext);
		on = lrg_nav_poly_mesh_find_nearest (self->mesh, p, ext, out, &poly);
		if (on)
		{
			memcpy (p, out, sizeof (out));
			self->region = lrg_nav_poly_mesh_get_polygon_region (self->mesh, poly);
		}
	}
	memcpy (self->position, p, sizeof (p));
	self->velocity[0] = self->velocity[1] = self->velocity[2] = 0.0f;
	if (self->state == LRG_NAV_AGENT_STATE_ON_LINK)
		set_state (self, LRG_NAV_AGENT_STATE_MOVING);
	if (self->has_destination && self->state != LRG_NAV_AGENT_STATE_ARRIVED)
		self->repath = TRUE;
	return on;
}

void
_lrg_nav_agent_follow_position (LrgNavAgent  *self,
                                const gfloat *position)
{
	gfloat p[3];

	g_return_if_fail (LRG_IS_NAV_AGENT (self));

	memcpy (p, position, sizeof (p));
	if (self->state != LRG_NAV_AGENT_STATE_ON_LINK && project (self, p))
		memcpy (self->position, p, sizeof (p));
	else if (self->state != LRG_NAV_AGENT_STATE_ON_LINK)
		memcpy (self->position, position, sizeof (p));
}

void
lrg_nav_agent_get_velocity (LrgNavAgent *self,
                            gfloat      *out)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->velocity, sizeof (self->velocity));
}

void
lrg_nav_agent_get_desired_velocity (LrgNavAgent *self,
                                    gfloat      *out)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->desired, sizeof (self->desired));
}

gboolean
lrg_nav_agent_set_destination (LrgNavAgent  *self,
                               const gfloat *destination)
{
	gboolean ok;

	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), FALSE);
	g_return_val_if_fail (destination != NULL, FALSE);

	memcpy (self->destination, destination, sizeof (self->destination));
	self->has_destination = TRUE;
	if (self->state == LRG_NAV_AGENT_STATE_ON_LINK)
	{
		/* finish the link first, then plan from its end */
		self->repath = TRUE;
		return TRUE;
	}
	ok = plan (self);
	flush_signals (self);
	return ok;
}

gboolean
lrg_nav_agent_get_destination (LrgNavAgent *self,
                               gfloat      *out)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), FALSE);

	if (out != NULL && self->has_destination)
		memcpy (out, self->destination, sizeof (self->destination));
	return self->has_destination;
}

void
lrg_nav_agent_stop (LrgNavAgent *self)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));

	self->has_destination = FALSE;
	self->repath = FALSE;
	g_array_set_size (self->path, 0);
	g_array_set_size (self->link_starts, 0);
	self->corner = 0;
	set_state (self, LRG_NAV_AGENT_STATE_IDLE);
}

void
lrg_nav_agent_request_repath (LrgNavAgent *self)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));

	if (self->has_destination)
		self->repath = TRUE;
}

LrgNavAgentState
lrg_nav_agent_get_state (LrgNavAgent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), LRG_NAV_AGENT_STATE_IDLE);
	return self->state;
}

const gfloat *
lrg_nav_agent_get_path (LrgNavAgent *self,
                        guint       *n_floats)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), NULL);
	g_return_val_if_fail (n_floats != NULL, NULL);

	*n_floats = self->path->len;
	return self->path->len > 0 ? (const gfloat *) (gpointer) self->path->data : NULL;
}

guint
lrg_nav_agent_get_corner_index (LrgNavAgent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), 0);
	return self->corner;
}

gfloat
lrg_nav_agent_get_remaining_distance (LrgNavAgent *self)
{
	gfloat d;
	guint  i;

	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), 0.0f);

	if (self->path->len == 0 || self->corner >= n_points (self) ||
	    (self->state != LRG_NAV_AGENT_STATE_MOVING && self->state != LRG_NAV_AGENT_STATE_ON_LINK))
		return 0.0f;
	d = dist2_xz (self->position, point_at (self, self->corner));
	for (i = self->corner; i + 1 < n_points (self); i++)
	{
		const gfloat *a = point_at (self, i);
		const gfloat *b = point_at (self, i + 1);

		d += sqrtf ((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) +
		            (b[2] - a[2]) * (b[2] - a[2]));
	}
	return d;
}

void
lrg_nav_agent_set_filter (LrgNavAgent          *self,
                          LrgNavPolyFilterFunc  filter,
                          gpointer              user_data,
                          GDestroyNotify        destroy)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));

	if (self->filter_destroy != NULL)
		self->filter_destroy (self->filter_data);
	self->filter = filter;
	self->filter_data = user_data;
	self->filter_destroy = destroy;
	if (self->has_destination)
		self->repath = TRUE;
}

#define AGENT_FLOAT_ACCESSORS(getter, setter, field, prop, min_value)          \
gfloat                                                                         \
getter (LrgNavAgent *self)                                                     \
{                                                                              \
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), 0.0f);                      \
	return self->field;                                                        \
}                                                                              \
                                                                               \
void                                                                           \
setter (LrgNavAgent *self,                                                     \
        gfloat       value)                                                    \
{                                                                              \
	g_return_if_fail (LRG_IS_NAV_AGENT (self));                                \
	g_object_set (self, prop, MAX (value, min_value), NULL);                   \
}

AGENT_FLOAT_ACCESSORS (lrg_nav_agent_get_radius, lrg_nav_agent_set_radius, radius, "radius", 0.01f)
AGENT_FLOAT_ACCESSORS (lrg_nav_agent_get_height, lrg_nav_agent_set_height, height, "height", 0.01f)
AGENT_FLOAT_ACCESSORS (lrg_nav_agent_get_max_speed, lrg_nav_agent_set_max_speed, max_speed, "max-speed", 0.0f)
AGENT_FLOAT_ACCESSORS (lrg_nav_agent_get_max_acceleration, lrg_nav_agent_set_max_acceleration, max_accel,
                       "max-acceleration", 0.01f)

gfloat
lrg_nav_agent_get_avoidance_priority (LrgNavAgent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), 0.0f);
	return self->priority;
}

void
lrg_nav_agent_set_avoidance_priority (LrgNavAgent *self,
                                      gfloat       priority)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));
	g_object_set (self, "avoidance-priority", CLAMP (priority, 0.0f, 1.0f), NULL);
}

gboolean
lrg_nav_agent_get_avoidance_enabled (LrgNavAgent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), FALSE);
	return self->avoidance;
}

void
lrg_nav_agent_set_avoidance_enabled (LrgNavAgent *self,
                                     gboolean     enabled)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));
	g_object_set (self, "avoidance-enabled", enabled ? TRUE : FALSE, NULL);
}

LrgNavCrowd *
lrg_nav_agent_get_crowd (LrgNavAgent *self)
{
	g_return_val_if_fail (LRG_IS_NAV_AGENT (self), NULL);
	return self->crowd;
}

void
lrg_nav_agent_update (LrgNavAgent *self,
                      gfloat       delta)
{
	g_return_if_fail (LRG_IS_NAV_AGENT (self));

	if (delta <= 0.0f)
		return;
	g_object_ref (self);
	prepare (self, delta);
	if (self->state == LRG_NAV_AGENT_STATE_ON_LINK)
		advance_link (self, delta);
	else
	{
		gfloat target[2];

		target[0] = self->desired[0];
		target[1] = self->desired[2];
		integrate (self, target, delta);
	}
	flush_signals (self);
	g_object_unref (self);
}

/* ==========================================================================
 * ORCA (port of RVO2, Apache-2.0: see the file header)
 * ========================================================================== */

/* RVO2 linearProgram1: the best point on line @line_no within the speed circle */
static gboolean
linear_program1 (const OrcaLine *lines,
                 guint           line_no,
                 gfloat          radius,
                 const gfloat   *opt_velocity,
                 gboolean        direction_opt,
                 gfloat         *result)
{
	const OrcaLine *ln = &lines[line_no];
	gfloat          dot_product = dot2 (ln->point, ln->direction);
	gfloat          discriminant = dot_product * dot_product + radius * radius - abs_sq2 (ln->point);
	gfloat          sqrt_discriminant;
	gfloat          t_left;
	gfloat          t_right;
	guint           i;

	if (discriminant < 0.0f)
		return FALSE;   /* the max speed circle fully invalidates the line */

	sqrt_discriminant = sqrtf (discriminant);
	t_left = -dot_product - sqrt_discriminant;
	t_right = -dot_product + sqrt_discriminant;

	for (i = 0; i < line_no; i++)
	{
		gfloat denominator = det2 (ln->direction, lines[i].direction);
		gfloat diff[2];
		gfloat numerator;
		gfloat t;

		diff[0] = ln->point[0] - lines[i].point[0];
		diff[1] = ln->point[1] - lines[i].point[1];
		numerator = det2 (lines[i].direction, diff);

		if (fabsf (denominator) <= RVO_EPSILON)
		{
			/* lines @line_no and @i are (almost) parallel */
			if (numerator < 0.0f)
				return FALSE;
			continue;
		}

		t = numerator / denominator;
		if (denominator >= 0.0f)
			t_right = MIN (t_right, t);   /* line i bounds line_no on the right */
		else
			t_left = MAX (t_left, t);     /* line i bounds line_no on the left */

		if (t_left > t_right)
			return FALSE;
	}

	if (direction_opt)
	{
		/* optimise direction */
		if (dot2 (opt_velocity, ln->direction) > 0.0f)
		{
			result[0] = ln->point[0] + t_right * ln->direction[0];
			result[1] = ln->point[1] + t_right * ln->direction[1];
		}
		else
		{
			result[0] = ln->point[0] + t_left * ln->direction[0];
			result[1] = ln->point[1] + t_left * ln->direction[1];
		}
	}
	else
	{
		/* optimise closest point */
		gfloat diff[2];
		gfloat t;

		diff[0] = opt_velocity[0] - ln->point[0];
		diff[1] = opt_velocity[1] - ln->point[1];
		t = dot2 (ln->direction, diff);
		if (t < t_left)
			t = t_left;
		else if (t > t_right)
			t = t_right;
		result[0] = ln->point[0] + t * ln->direction[0];
		result[1] = ln->point[1] + t * ln->direction[1];
	}
	return TRUE;
}

/* RVO2 linearProgram2: returns the number of lines satisfied before a failure */
static guint
linear_program2 (const OrcaLine *lines,
                 guint           n_lines,
                 gfloat          radius,
                 const gfloat   *opt_velocity,
                 gboolean        direction_opt,
                 gfloat         *result)
{
	guint i;

	if (direction_opt)
	{
		/* optimise direction: the optimisation velocity is of unit length */
		result[0] = opt_velocity[0] * radius;
		result[1] = opt_velocity[1] * radius;
	}
	else if (abs_sq2 (opt_velocity) > radius * radius)
	{
		/* optimise closest point, outside the circle */
		gfloat n[2];

		n[0] = opt_velocity[0];
		n[1] = opt_velocity[1];
		normalize2 (n);
		result[0] = n[0] * radius;
		result[1] = n[1] * radius;
	}
	else
	{
		result[0] = opt_velocity[0];
		result[1] = opt_velocity[1];
	}

	for (i = 0; i < n_lines; i++)
	{
		gfloat diff[2];

		diff[0] = lines[i].point[0] - result[0];
		diff[1] = lines[i].point[1] - result[1];
		if (det2 (lines[i].direction, diff) > 0.0f)
		{
			/* the result does not satisfy constraint i: compute a new one */
			gfloat temp[2];

			temp[0] = result[0];
			temp[1] = result[1];
			if (!linear_program1 (lines, i, radius, opt_velocity, direction_opt, result))
			{
				result[0] = temp[0];
				result[1] = temp[1];
				return i;
			}
		}
	}
	return n_lines;
}

/* RVO2 linearProgram3: the least-penetrating velocity when 2 failed */
static void
linear_program3 (const OrcaLine *lines,
                 guint           n_lines,
                 guint           n_obst_lines,
                 guint           begin_line,
                 gfloat          radius,
                 GArray         *proj,
                 gfloat         *result)
{
	gfloat distance = 0.0f;
	guint  i;

	for (i = begin_line; i < n_lines; i++)
	{
		gfloat diff[2];

		diff[0] = lines[i].point[0] - result[0];
		diff[1] = lines[i].point[1] - result[1];
		if (det2 (lines[i].direction, diff) > distance)
		{
			/* the result does not satisfy the constraint of line i */
			gfloat opt[2];
			gfloat temp[2];
			guint  j;

			g_array_set_size (proj, 0);
			g_array_append_vals (proj, lines, n_obst_lines);
			for (j = n_obst_lines; j < i; j++)
			{
				OrcaLine line;
				gfloat   determinant = det2 (lines[i].direction, lines[j].direction);

				if (fabsf (determinant) <= RVO_EPSILON)
				{
					/* lines i and j are parallel */
					if (dot2 (lines[i].direction, lines[j].direction) > 0.0f)
						continue;   /* same direction */
					line.point[0] = 0.5f * (lines[i].point[0] + lines[j].point[0]);
					line.point[1] = 0.5f * (lines[i].point[1] + lines[j].point[1]);
				}
				else
				{
					gfloat d2[2];
					gfloat s;

					d2[0] = lines[i].point[0] - lines[j].point[0];
					d2[1] = lines[i].point[1] - lines[j].point[1];
					s = det2 (lines[j].direction, d2) / determinant;
					line.point[0] = lines[i].point[0] + s * lines[i].direction[0];
					line.point[1] = lines[i].point[1] + s * lines[i].direction[1];
				}
				line.direction[0] = lines[j].direction[0] - lines[i].direction[0];
				line.direction[1] = lines[j].direction[1] - lines[i].direction[1];
				normalize2 (line.direction);
				g_array_append_val (proj, line);
			}

			temp[0] = result[0];
			temp[1] = result[1];
			opt[0] = -lines[i].direction[1];
			opt[1] = lines[i].direction[0];
			if (linear_program2 ((const OrcaLine *) (gpointer) proj->data, proj->len, radius, opt, TRUE, result)
			    < proj->len)
			{
				/* in principle impossible (the result is feasible by
				 * definition); a float error keeps the current result */
				result[0] = temp[0];
				result[1] = temp[1];
			}
			diff[0] = lines[i].point[0] - result[0];
			diff[1] = lines[i].point[1] - result[1];
			distance = det2 (lines[i].direction, diff);
		}
	}
}

/*
 * orca_obstacle_lines:
 * RVO2's obstacle half-planes for the two-vertex obstacles in @verts
 * whose first vertices (the edge the agent sees) are listed in @firsts,
 * nearest first.
 */
static void
orca_obstacle_lines (const gfloat     *position,
                     const gfloat     *velocity,
                     gfloat            radius,
                     gfloat            time_horizon_obst,
                     const ObstVertex *verts,
                     const guint      *firsts,
                     guint             n_firsts,
                     GArray           *lines)
{
	gfloat inv_time_horizon_obst = 1.0f / time_horizon_obst;
	gfloat radius_sq = radius * radius;
	guint  i;

	for (i = 0; i < n_firsts; i++)
	{
		const ObstVertex *obstacle1 = &verts[firsts[i]];
		const ObstVertex *obstacle2 = &verts[obstacle1->next];
		gfloat            rel1[2];
		gfloat            rel2[2];
		gboolean          already_covered = FALSE;
		gfloat            dist_sq1;
		gfloat            dist_sq2;
		gfloat            obstacle_vector[2];
		gfloat            s;
		gfloat            dist_sq_line;
		gfloat            tmp[2];
		gfloat            left_leg[2];
		gfloat            right_leg[2];
		gboolean          left_foreign = FALSE;
		gboolean          right_foreign = FALSE;
		const ObstVertex *left_neighbour;
		gfloat            left_cutoff[2];
		gfloat            right_cutoff[2];
		gfloat            cutoff_vec[2];
		gfloat            t;
		gfloat            t_left;
		gfloat            t_right;
		gfloat            dist_sq_cutoff;
		gfloat            dist_sq_left;
		gfloat            dist_sq_right;
		OrcaLine          line;
		guint             j;

		rel1[0] = obstacle1->point[0] - position[0];
		rel1[1] = obstacle1->point[1] - position[1];
		rel2[0] = obstacle2->point[0] - position[0];
		rel2[1] = obstacle2->point[1] - position[1];

		/* already taken care of by earlier obstacle lines? */
		for (j = 0; j < lines->len; j++)
		{
			const OrcaLine *l = &g_array_index (lines, OrcaLine, j);
			gfloat          a[2];
			gfloat          b[2];

			a[0] = inv_time_horizon_obst * rel1[0] - l->point[0];
			a[1] = inv_time_horizon_obst * rel1[1] - l->point[1];
			b[0] = inv_time_horizon_obst * rel2[0] - l->point[0];
			b[1] = inv_time_horizon_obst * rel2[1] - l->point[1];
			if (det2 (a, l->direction) - inv_time_horizon_obst * radius >= -RVO_EPSILON &&
			    det2 (b, l->direction) - inv_time_horizon_obst * radius >= -RVO_EPSILON)
			{
				already_covered = TRUE;
				break;
			}
		}
		if (already_covered)
			continue;

		/* not covered yet: collisions? */
		dist_sq1 = abs_sq2 (rel1);
		dist_sq2 = abs_sq2 (rel2);
		obstacle_vector[0] = obstacle2->point[0] - obstacle1->point[0];
		obstacle_vector[1] = obstacle2->point[1] - obstacle1->point[1];
		s = (-rel1[0] * obstacle_vector[0] - rel1[1] * obstacle_vector[1]) / abs_sq2 (obstacle_vector);
		tmp[0] = -rel1[0] - s * obstacle_vector[0];
		tmp[1] = -rel1[1] - s * obstacle_vector[1];
		dist_sq_line = abs_sq2 (tmp);

		if (s < 0.0f && dist_sq1 <= radius_sq)
		{
			/* collision with the left vertex (convex) */
			line.point[0] = line.point[1] = 0.0f;
			line.direction[0] = -rel1[1];
			line.direction[1] = rel1[0];
			normalize2 (line.direction);
			g_array_append_val (lines, line);
			continue;
		}
		else if (s > 1.0f && dist_sq2 <= radius_sq)
		{
			/* collision with the right vertex, unless the neighbouring edge handles it */
			if (det2 (rel2, obstacle2->unit_dir) >= 0.0f)
			{
				line.point[0] = line.point[1] = 0.0f;
				line.direction[0] = -rel2[1];
				line.direction[1] = rel2[0];
				normalize2 (line.direction);
				g_array_append_val (lines, line);
			}
			continue;
		}
		else if (s >= 0.0f && s < 1.0f && dist_sq_line <= radius_sq)
		{
			/* collision with the segment */
			line.point[0] = line.point[1] = 0.0f;
			line.direction[0] = -obstacle1->unit_dir[0];
			line.direction[1] = -obstacle1->unit_dir[1];
			g_array_append_val (lines, line);
			continue;
		}

		/* no collision: the legs (both from one vertex when seen obliquely) */
		if (s < 0.0f && dist_sq_line <= radius_sq)
		{
			/* seen obliquely: the left vertex defines the velocity obstacle */
			gfloat leg1 = sqrtf (dist_sq1 - radius_sq);

			obstacle2 = obstacle1;
			left_leg[0] = (rel1[0] * leg1 - rel1[1] * radius) / dist_sq1;
			left_leg[1] = (rel1[0] * radius + rel1[1] * leg1) / dist_sq1;
			right_leg[0] = (rel1[0] * leg1 + rel1[1] * radius) / dist_sq1;
			right_leg[1] = (-rel1[0] * radius + rel1[1] * leg1) / dist_sq1;
		}
		else if (s > 1.0f && dist_sq_line <= radius_sq)
		{
			/* seen obliquely: the right vertex defines the velocity obstacle */
			gfloat leg2 = sqrtf (dist_sq2 - radius_sq);

			obstacle1 = obstacle2;
			left_leg[0] = (rel2[0] * leg2 - rel2[1] * radius) / dist_sq2;
			left_leg[1] = (rel2[0] * radius + rel2[1] * leg2) / dist_sq2;
			right_leg[0] = (rel2[0] * leg2 + rel2[1] * radius) / dist_sq2;
			right_leg[1] = (-rel2[0] * radius + rel2[1] * leg2) / dist_sq2;
		}
		else
		{
			/* the usual case (both vertices convex) */
			gfloat leg1 = sqrtf (dist_sq1 - radius_sq);
			gfloat leg2 = sqrtf (dist_sq2 - radius_sq);

			left_leg[0] = (rel1[0] * leg1 - rel1[1] * radius) / dist_sq1;
			left_leg[1] = (rel1[0] * radius + rel1[1] * leg1) / dist_sq1;
			right_leg[0] = (rel2[0] * leg2 + rel2[1] * radius) / dist_sq2;
			right_leg[1] = (-rel2[0] * radius + rel2[1] * leg2) / dist_sq2;
		}

		/* legs never point into the neighbouring edge at a convex vertex:
		 * take that edge's cut-off line instead (and add nothing when the
		 * velocity projects on such a "foreign" leg) */
		left_neighbour = &verts[obstacle1->prev];
		tmp[0] = -left_neighbour->unit_dir[0];
		tmp[1] = -left_neighbour->unit_dir[1];
		if (det2 (left_leg, tmp) >= 0.0f)
		{
			left_leg[0] = tmp[0];
			left_leg[1] = tmp[1];
			left_foreign = TRUE;
		}
		if (det2 (right_leg, obstacle2->unit_dir) <= 0.0f)
		{
			right_leg[0] = obstacle2->unit_dir[0];
			right_leg[1] = obstacle2->unit_dir[1];
			right_foreign = TRUE;
		}

		/* the cut-off centres */
		left_cutoff[0] = inv_time_horizon_obst * (obstacle1->point[0] - position[0]);
		left_cutoff[1] = inv_time_horizon_obst * (obstacle1->point[1] - position[1]);
		right_cutoff[0] = inv_time_horizon_obst * (obstacle2->point[0] - position[0]);
		right_cutoff[1] = inv_time_horizon_obst * (obstacle2->point[1] - position[1]);
		cutoff_vec[0] = right_cutoff[0] - left_cutoff[0];
		cutoff_vec[1] = right_cutoff[1] - left_cutoff[1];

		/* project the current velocity on the velocity obstacle */
		tmp[0] = velocity[0] - left_cutoff[0];
		tmp[1] = velocity[1] - left_cutoff[1];
		t = obstacle1 == obstacle2 ? 0.5f : dot2 (tmp, cutoff_vec) / abs_sq2 (cutoff_vec);
		t_left = dot2 (tmp, left_leg);
		{
			gfloat r[2];

			r[0] = velocity[0] - right_cutoff[0];
			r[1] = velocity[1] - right_cutoff[1];
			t_right = dot2 (r, right_leg);
		}

		if ((t < 0.0f && t_left < 0.0f) || (obstacle1 == obstacle2 && t_left < 0.0f && t_right < 0.0f))
		{
			/* project on the left cut-off circle */
			gfloat unit_w[2];

			unit_w[0] = velocity[0] - left_cutoff[0];
			unit_w[1] = velocity[1] - left_cutoff[1];
			normalize2 (unit_w);
			line.direction[0] = unit_w[1];
			line.direction[1] = -unit_w[0];
			line.point[0] = left_cutoff[0] + radius * inv_time_horizon_obst * unit_w[0];
			line.point[1] = left_cutoff[1] + radius * inv_time_horizon_obst * unit_w[1];
			g_array_append_val (lines, line);
			continue;
		}
		else if (t > 1.0f && t_right < 0.0f)
		{
			/* project on the right cut-off circle */
			gfloat unit_w[2];

			unit_w[0] = velocity[0] - right_cutoff[0];
			unit_w[1] = velocity[1] - right_cutoff[1];
			normalize2 (unit_w);
			line.direction[0] = unit_w[1];
			line.direction[1] = -unit_w[0];
			line.point[0] = right_cutoff[0] + radius * inv_time_horizon_obst * unit_w[0];
			line.point[1] = right_cutoff[1] + radius * inv_time_horizon_obst * unit_w[1];
			g_array_append_val (lines, line);
			continue;
		}

		/* project on the left leg, the right leg or the cut-off line, whichever is closest */
		if (t < 0.0f || t > 1.0f || obstacle1 == obstacle2)
			dist_sq_cutoff = G_MAXFLOAT;
		else
		{
			tmp[0] = velocity[0] - (left_cutoff[0] + t * cutoff_vec[0]);
			tmp[1] = velocity[1] - (left_cutoff[1] + t * cutoff_vec[1]);
			dist_sq_cutoff = abs_sq2 (tmp);
		}
		if (t_left < 0.0f)
			dist_sq_left = G_MAXFLOAT;
		else
		{
			tmp[0] = velocity[0] - (left_cutoff[0] + t_left * left_leg[0]);
			tmp[1] = velocity[1] - (left_cutoff[1] + t_left * left_leg[1]);
			dist_sq_left = abs_sq2 (tmp);
		}
		if (t_right < 0.0f)
			dist_sq_right = G_MAXFLOAT;
		else
		{
			tmp[0] = velocity[0] - (right_cutoff[0] + t_right * right_leg[0]);
			tmp[1] = velocity[1] - (right_cutoff[1] + t_right * right_leg[1]);
			dist_sq_right = abs_sq2 (tmp);
		}

		if (dist_sq_cutoff <= dist_sq_left && dist_sq_cutoff <= dist_sq_right)
		{
			/* project on the cut-off line */
			line.direction[0] = -obstacle1->unit_dir[0];
			line.direction[1] = -obstacle1->unit_dir[1];
			line.point[0] = left_cutoff[0] + radius * inv_time_horizon_obst * -line.direction[1];
			line.point[1] = left_cutoff[1] + radius * inv_time_horizon_obst * line.direction[0];
			g_array_append_val (lines, line);
		}
		else if (dist_sq_left <= dist_sq_right)
		{
			/* project on the left leg */
			if (left_foreign)
				continue;
			line.direction[0] = left_leg[0];
			line.direction[1] = left_leg[1];
			line.point[0] = left_cutoff[0] + radius * inv_time_horizon_obst * -line.direction[1];
			line.point[1] = left_cutoff[1] + radius * inv_time_horizon_obst * line.direction[0];
			g_array_append_val (lines, line);
		}
		else
		{
			/* project on the right leg */
			if (right_foreign)
				continue;
			line.direction[0] = -right_leg[0];
			line.direction[1] = -right_leg[1];
			line.point[0] = right_cutoff[0] + radius * inv_time_horizon_obst * -line.direction[1];
			line.point[1] = right_cutoff[1] + radius * inv_time_horizon_obst * line.direction[0];
			g_array_append_val (lines, line);
		}
	}
}

/*
 * orca_agent_line:
 * RVO2's half-plane for one neighbour; @share is this agent's part of
 * the avoidance (0.5 in RVO2).
 */
static void
orca_agent_line (const gfloat *position,
                 const gfloat *velocity,
                 gfloat        radius,
                 const gfloat *other_position,
                 const gfloat *other_velocity,
                 gfloat        other_radius,
                 gfloat        time_horizon,
                 gfloat        time_step,
                 gfloat        share,
                 GArray       *lines)
{
	gfloat   inv_time_horizon = 1.0f / time_horizon;
	gfloat   rel_pos[2];
	gfloat   rel_vel[2];
	gfloat   dist_sq;
	gfloat   combined_radius = radius + other_radius;
	gfloat   combined_radius_sq = combined_radius * combined_radius;
	gfloat   u[2];
	OrcaLine line;

	rel_pos[0] = other_position[0] - position[0];
	rel_pos[1] = other_position[1] - position[1];
	rel_vel[0] = velocity[0] - other_velocity[0];
	rel_vel[1] = velocity[1] - other_velocity[1];
	dist_sq = abs_sq2 (rel_pos);

	if (dist_sq > combined_radius_sq)
	{
		/* no collision: w from the cut-off centre to the relative velocity */
		gfloat w[2];
		gfloat w_length_sq;
		gfloat dot_product1;

		w[0] = rel_vel[0] - inv_time_horizon * rel_pos[0];
		w[1] = rel_vel[1] - inv_time_horizon * rel_pos[1];
		w_length_sq = abs_sq2 (w);
		dot_product1 = dot2 (w, rel_pos);

		if (dot_product1 < 0.0f && dot_product1 * dot_product1 > combined_radius_sq * w_length_sq)
		{
			/* project on the cut-off circle */
			gfloat w_length = sqrtf (w_length_sq);
			gfloat unit_w[2];

			unit_w[0] = w[0] / w_length;
			unit_w[1] = w[1] / w_length;
			line.direction[0] = unit_w[1];
			line.direction[1] = -unit_w[0];
			u[0] = (combined_radius * inv_time_horizon - w_length) * unit_w[0];
			u[1] = (combined_radius * inv_time_horizon - w_length) * unit_w[1];
		}
		else
		{
			/* project on the legs */
			gfloat leg = sqrtf (dist_sq - combined_radius_sq);
			gfloat dot_product2;

			if (det2 (rel_pos, w) > 0.0f)
			{
				/* left leg */
				line.direction[0] = (rel_pos[0] * leg - rel_pos[1] * combined_radius) / dist_sq;
				line.direction[1] = (rel_pos[0] * combined_radius + rel_pos[1] * leg) / dist_sq;
			}
			else
			{
				/* right leg */
				line.direction[0] = -(rel_pos[0] * leg + rel_pos[1] * combined_radius) / dist_sq;
				line.direction[1] = -(-rel_pos[0] * combined_radius + rel_pos[1] * leg) / dist_sq;
			}
			dot_product2 = dot2 (rel_vel, line.direction);
			u[0] = dot_product2 * line.direction[0] - rel_vel[0];
			u[1] = dot_product2 * line.direction[1] - rel_vel[1];
		}
	}
	else
	{
		/* collision: project on the cut-off circle of one time step */
		gfloat inv_time_step = 1.0f / time_step;
		gfloat w[2];
		gfloat w_length;
		gfloat unit_w[2];

		w[0] = rel_vel[0] - inv_time_step * rel_pos[0];
		w[1] = rel_vel[1] - inv_time_step * rel_pos[1];
		w_length = sqrtf (abs_sq2 (w));
		if (w_length < 1e-9f)
		{
			/* coincident and at rest relative to each other: push along +x */
			unit_w[0] = 1.0f;
			unit_w[1] = 0.0f;
		}
		else
		{
			unit_w[0] = w[0] / w_length;
			unit_w[1] = w[1] / w_length;
		}
		line.direction[0] = unit_w[1];
		line.direction[1] = -unit_w[0];
		u[0] = (combined_radius * inv_time_step - w_length) * unit_w[0];
		u[1] = (combined_radius * inv_time_step - w_length) * unit_w[1];
	}

	line.point[0] = velocity[0] + share * u[0];
	line.point[1] = velocity[1] + share * u[1];
	g_array_append_val (lines, line);
}

/* ==========================================================================
 * LrgNavCrowd
 * ========================================================================== */

static void
lrg_nav_crowd_finalize (GObject *object)
{
	LrgNavCrowd *self = LRG_NAV_CROWD (object);
	guint        i;

	for (i = 0; i < self->agents->len; i++)
		((LrgNavAgent *) g_ptr_array_index (self->agents, i))->crowd = NULL;
	g_ptr_array_unref (self->agents);
	g_clear_object (&self->mesh);
	g_array_unref (self->grid_start);
	g_array_unref (self->grid_items);
	g_array_unref (self->candidates);
	g_array_unref (self->walls);
	g_array_unref (self->lines);
	g_array_unref (self->proj);
	g_array_unref (self->verts);
	G_OBJECT_CLASS (lrg_nav_crowd_parent_class)->finalize (object);
}

static void
lrg_nav_crowd_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgNavCrowd *self = LRG_NAV_CROWD (object);

	switch (prop_id)
	{
	case CROWD_PROP_MESH:
		g_value_set_object (value, self->mesh);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_nav_crowd_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgNavCrowd *self = LRG_NAV_CROWD (object);

	switch (prop_id)
	{
	case CROWD_PROP_MESH:
		lrg_nav_crowd_set_mesh (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_nav_crowd_class_init (LrgNavCrowdClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_nav_crowd_finalize;
	object_class->get_property = lrg_nav_crowd_get_property;
	object_class->set_property = lrg_nav_crowd_set_property;

	/**
	 * LrgNavCrowd:mesh:
	 *
	 * The #LrgNavPolyMesh the crowd's agents walk.
	 *
	 * Since: 0.9.0
	 */
	crowd_properties[CROWD_PROP_MESH] =
		g_param_spec_object ("mesh", NULL, "Navigation mesh", LRG_TYPE_NAV_POLY_MESH,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	g_object_class_install_properties (object_class, CROWD_N_PROPS, crowd_properties);

	/**
	 * LrgNavCrowd::stepped:
	 * @crowd: the crowd
	 * @delta: the step's length in seconds
	 *
	 * Emitted at the end of every lrg_nav_crowd_step(), after the
	 * agents' own signals: copy agent positions to entities here.
	 *
	 * Since: 0.9.0
	 */
	crowd_signals[CROWD_SIGNAL_STEPPED] =
		g_signal_new ("stepped", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_FLOAT);
}

static void
lrg_nav_crowd_init (LrgNavCrowd *self)
{
	self->agents = g_ptr_array_new_with_free_func (g_object_unref);
	self->grid_start = g_array_new (FALSE, TRUE, sizeof (guint));
	self->grid_items = g_array_new (FALSE, FALSE, sizeof (guint));
	self->candidates = g_array_new (FALSE, FALSE, sizeof (guint));
	self->walls = g_array_new (FALSE, FALSE, sizeof (guint32));
	self->lines = g_array_new (FALSE, FALSE, sizeof (OrcaLine));
	self->proj = g_array_new (FALSE, FALSE, sizeof (OrcaLine));
	self->verts = g_array_new (FALSE, FALSE, sizeof (ObstVertex));
}

LrgNavCrowd *
lrg_nav_crowd_new (LrgNavPolyMesh *mesh)
{
	g_return_val_if_fail (mesh == NULL || LRG_IS_NAV_POLY_MESH (mesh), NULL);
	return g_object_new (LRG_TYPE_NAV_CROWD, "mesh", mesh, NULL);
}

LrgNavPolyMesh *
lrg_nav_crowd_get_mesh (LrgNavCrowd *self)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), NULL);
	return self->mesh;
}

void
lrg_nav_crowd_set_mesh (LrgNavCrowd    *self,
                        LrgNavPolyMesh *mesh)
{
	guint i;

	g_return_if_fail (LRG_IS_NAV_CROWD (self));
	g_return_if_fail (mesh == NULL || LRG_IS_NAV_POLY_MESH (mesh));

	if (!g_set_object (&self->mesh, mesh))
		return;
	for (i = 0; i < self->agents->len; i++)
		lrg_nav_agent_set_mesh (g_ptr_array_index (self->agents, i), mesh);
	g_object_notify_by_pspec (G_OBJECT (self), crowd_properties[CROWD_PROP_MESH]);
}

gboolean
lrg_nav_crowd_add_agent (LrgNavCrowd *self,
                         LrgNavAgent *agent)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), FALSE);
	g_return_val_if_fail (LRG_IS_NAV_AGENT (agent), FALSE);

	if (agent->crowd != NULL)
		return FALSE;
	agent->crowd = self;
	g_ptr_array_add (self->agents, g_object_ref (agent));
	lrg_nav_agent_set_mesh (agent, self->mesh);
	return TRUE;
}

gboolean
lrg_nav_crowd_remove_agent (LrgNavCrowd *self,
                            LrgNavAgent *agent)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), FALSE);
	g_return_val_if_fail (LRG_IS_NAV_AGENT (agent), FALSE);

	if (agent->crowd != self)
		return FALSE;
	agent->crowd = NULL;
	if (self->debug_agent == agent)
		self->debug_agent = NULL;
	return g_ptr_array_remove (self->agents, agent);
}

guint
lrg_nav_crowd_get_n_agents (LrgNavCrowd *self)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), 0);
	return self->agents->len;
}

LrgNavAgent *
lrg_nav_crowd_get_agent (LrgNavCrowd *self,
                         guint        index)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), NULL);

	if (index >= self->agents->len)
		return NULL;
	return g_ptr_array_index (self->agents, index);
}

void
lrg_nav_crowd_clear (LrgNavCrowd *self)
{
	guint i;

	g_return_if_fail (LRG_IS_NAV_CROWD (self));

	for (i = 0; i < self->agents->len; i++)
		((LrgNavAgent *) g_ptr_array_index (self->agents, i))->crowd = NULL;
	g_ptr_array_set_size (self->agents, 0);
	self->debug_agent = NULL;
}

void
lrg_nav_crowd_set_debug_agent (LrgNavCrowd *self,
                               LrgNavAgent *agent)
{
	g_return_if_fail (LRG_IS_NAV_CROWD (self));
	g_return_if_fail (agent == NULL || LRG_IS_NAV_AGENT (agent));

	self->debug_agent = (agent != NULL && agent->crowd == self) ? agent : NULL;
}

LrgNavAgent *
lrg_nav_crowd_get_debug_agent (LrgNavCrowd *self)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), NULL);
	return self->debug_agent;
}

guint64
lrg_nav_crowd_get_step_count (LrgNavCrowd *self)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), 0);
	return self->steps;
}

gdouble
lrg_nav_crowd_get_last_step_seconds (LrgNavCrowd *self)
{
	g_return_val_if_fail (LRG_IS_NAV_CROWD (self), 0.0);
	return self->last_seconds;
}

/* --- the neighbour grid --- */

#define GRID_MAX_CELLS (1u << 20)

/* the dense grid cell of an X/Z position, clamped into the grid */
static void
grid_cell_of (LrgNavCrowd *self,
              gfloat       x,
              gfloat       z,
              gint32      *cx,
              gint32      *cz)
{
	*cx = CLAMP ((gint32) floorf ((x - self->grid_min[0]) / self->grid_cell), 0, self->grid_w - 1);
	*cz = CLAMP ((gint32) floorf ((z - self->grid_min[1]) / self->grid_cell), 0, self->grid_d - 1);
}

/*
 * build_grid:
 * A dense uniform grid over the agents' extent (cells of four times the
 * largest radius, at least 2 m), filled by counting sort: agents keep
 * their index order inside a cell, so neighbour lists are deterministic.
 */
static void
build_grid (LrgNavCrowd *self)
{
	gfloat  mn[2] = { G_MAXFLOAT, G_MAXFLOAT };
	gfloat  mx[2] = { -G_MAXFLOAT, -G_MAXFLOAT };
	guint   n = self->agents->len;
	guint   n_cells;
	guint  *start;
	guint  *fill;
	guint   i;

	self->max_radius = 0.0f;
	for (i = 0; i < n; i++)
	{
		LrgNavAgent *a = g_ptr_array_index (self->agents, i);

		self->max_radius = MAX (self->max_radius, a->radius);
		mn[0] = MIN (mn[0], a->position[0]);
		mn[1] = MIN (mn[1], a->position[2]);
		mx[0] = MAX (mx[0], a->position[0]);
		mx[1] = MAX (mx[1], a->position[2]);
	}
	if (n == 0)
	{
		mn[0] = mn[1] = mx[0] = mx[1] = 0.0f;
	}
	self->grid_cell = MAX (2.0f, self->max_radius * 4.0f);
	/* never more than GRID_MAX_CELLS: grow the cells for huge extents */
	while (((mx[0] - mn[0]) / self->grid_cell + 1.0f) * ((mx[1] - mn[1]) / self->grid_cell + 1.0f) >
	       (gfloat) GRID_MAX_CELLS)
		self->grid_cell *= 2.0f;
	self->grid_min[0] = mn[0];
	self->grid_min[1] = mn[1];
	self->grid_w = (gint32) floorf ((mx[0] - mn[0]) / self->grid_cell) + 1;
	self->grid_d = (gint32) floorf ((mx[1] - mn[1]) / self->grid_cell) + 1;
	n_cells = (guint) self->grid_w * (guint) self->grid_d;

	/* counting sort by cell */
	g_array_set_size (self->grid_start, 0);
	g_array_set_size (self->grid_start, n_cells + 1);
	g_array_set_size (self->grid_items, n);
	start = (guint *) (gpointer) self->grid_start->data;
	for (i = 0; i < n; i++)
	{
		LrgNavAgent *a = g_ptr_array_index (self->agents, i);
		gint32       cx;
		gint32       cz;

		grid_cell_of (self, a->position[0], a->position[2], &cx, &cz);
		start[(guint) cz * (guint) self->grid_w + (guint) cx + 1]++;
	}
	for (i = 1; i <= n_cells; i++)
		start[i] += start[i - 1];
	fill = g_new (guint, n_cells + 1);
	memcpy (fill, start, sizeof (guint) * (n_cells + 1));
	for (i = 0; i < n; i++)
	{
		LrgNavAgent *a = g_ptr_array_index (self->agents, i);
		gint32       cx;
		gint32       cz;

		grid_cell_of (self, a->position[0], a->position[2], &cx, &cz);
		g_array_index (self->grid_items, guint, fill[(guint) cz * (guint) self->grid_w + (guint) cx]++) = i;
	}
	g_free (fill);
}

typedef struct
{
	guint  index;
	gfloat dist_sq;
} Neighbour;

static gint
compare_neighbours (gconstpointer a,
                    gconstpointer b)
{
	const Neighbour *x = a;
	const Neighbour *y = b;

	if (x->dist_sq != y->dist_sq)
		return x->dist_sq < y->dist_sq ? -1 : 1;
	return x->index < y->index ? -1 : (x->index > y->index ? 1 : 0);
}

/* inserts @n into the sorted @out, keeping at most @max_count (0: all) */
static void
insert_neighbour (GArray          *out,
                  const Neighbour *n,
                  guint            max_count)
{
	guint at = out->len;

	while (at > 0 && compare_neighbours (n, &g_array_index (out, Neighbour, at - 1)) < 0)
		at--;
	if (max_count > 0 && at >= max_count)
		return;
	g_array_insert_val (out, at, *n);
	if (max_count > 0 && out->len > max_count)
		g_array_set_size (out, max_count);
}

/*
 * find_neighbours:
 * The agents within @range of agent @self_index (and within the taller
 * height vertically), nearest first, ties by index; with @max_count only
 * the nearest @max_count (the range shrinks to the farthest kept one, as
 * RVO2's neighbour search does).
 */
static void
find_neighbours (LrgNavCrowd *self,
                 guint        self_index,
                 gfloat       range,
                 guint        max_count,
                 GArray      *out)
{
	LrgNavAgent *a = g_ptr_array_index (self->agents, self_index);
	gint32       cx0;
	gint32       cx1;
	gint32       cz0;
	gint32       cz1;
	gint32       cx;
	gint32       cz;
	gfloat       range_sq = range * range;
	const guint *start = (const guint *) (gpointer) self->grid_start->data;

	g_array_set_size (out, 0);
	grid_cell_of (self, a->position[0] - range, a->position[2] - range, &cx0, &cz0);
	grid_cell_of (self, a->position[0] + range, a->position[2] + range, &cx1, &cz1);
	for (cz = cz0; cz <= cz1; cz++)
	{
		for (cx = cx0; cx <= cx1; cx++)
		{
			guint cell = (guint) cz * (guint) self->grid_w + (guint) cx;
			guint j;

			for (j = start[cell]; j < start[cell + 1]; j++)
			{
				guint        other = g_array_index (self->grid_items, guint, j);
				LrgNavAgent *b = g_ptr_array_index (self->agents, other);
				Neighbour    n;
				gfloat       dx;
				gfloat       dz;

				if (other == self_index)
					continue;
				if (fabsf (b->position[1] - a->position[1]) > MAX (a->height, b->height))
					continue;
				dx = b->position[0] - a->position[0];
				dz = b->position[2] - a->position[2];
				n.dist_sq = dx * dx + dz * dz;
				if (n.dist_sq >= range_sq)
					continue;
				n.index = other;
				insert_neighbour (out, &n, max_count);
				if (max_count > 0 && out->len == max_count)
					range_sq = MIN (range_sq, g_array_index (out, Neighbour, max_count - 1).dist_sq + 1e-6f);
			}
		}
	}
}

/* this agent's share of avoiding @b */
static gfloat
avoid_share (LrgNavAgent *a,
             LrgNavAgent *b)
{
	if (!b->avoidance || b->state == LRG_NAV_AGENT_STATE_ON_LINK)
		return 1.0f;
	return CLAMP (0.5f + (b->priority - a->priority) * 0.5f, 0.0f, 1.0f);
}

/* adds separation steering to a's desired velocity */
static void
separation (LrgNavCrowd *self,
            LrgNavAgent *a,
            GArray      *neighbours)
{
	gfloat push[2] = { 0.0f, 0.0f };
	guint  i;

	if (a->separation_weight <= 0.0f || a->state != LRG_NAV_AGENT_STATE_MOVING)
		return;
	for (i = 0; i < neighbours->len; i++)
	{
		const Neighbour *n = &g_array_index (neighbours, Neighbour, i);
		LrgNavAgent     *b = g_ptr_array_index (self->agents, n->index);
		gfloat           reach = (a->radius + b->radius) * 2.0f;
		gfloat           d = sqrtf (n->dist_sq);
		gfloat           w;

		if (d >= reach || d < 1e-6f)
			continue;
		w = (1.0f - d / reach);
		push[0] += (a->position[0] - b->position[0]) / d * w;
		push[1] += (a->position[2] - b->position[2]) / d * w;
	}
	a->desired[0] += push[0] * a->separation_weight * a->max_speed;
	a->desired[2] += push[1] * a->separation_weight * a->max_speed;
	{
		gfloat v[2];
		gfloat len;

		v[0] = a->desired[0];
		v[1] = a->desired[2];
		len = sqrtf (abs_sq2 (v));
		if (len > a->max_speed && len > 0.0f)
		{
			a->desired[0] *= a->max_speed / len;
			a->desired[2] *= a->max_speed / len;
		}
	}
}

/* ORCA for agent @index: fills a->new_velocity */
static void
solve_agent (LrgNavCrowd *self,
             guint        index,
             GArray      *neighbours,
             gfloat       delta)
{
	LrgNavAgent *a = g_ptr_array_index (self->agents, index);
	gfloat       pos[2];
	gfloat       vel[2];
	gfloat       pref[2];
	gfloat       result[2];
	guint        n_obst_lines;
	guint        fail;
	guint        i;
	guint        used;

	pos[0] = a->position[0];
	pos[1] = a->position[2];
	vel[0] = a->velocity[0];
	vel[1] = a->velocity[2];
	pref[0] = a->desired[0];
	pref[1] = a->desired[2];
	g_array_set_size (self->lines, 0);
	g_array_set_size (a->debug_neighbours, 0);
	g_array_set_size (a->debug_walls, 0);

	/* --- walls: boundary segments in range, as two-vertex obstacles --- */
	g_array_set_size (self->verts, 0);
	if (self->mesh != NULL && lrg_nav_poly_mesh_get_is_baked (self->mesh))
	{
		gfloat             range = a->time_horizon_obst * a->max_speed + a->radius;
		g_autoptr(GArray)  firsts = g_array_new (FALSE, FALSE, sizeof (Neighbour));
		g_autofree guint  *order = NULL;

		_lrg_nav_poly_mesh_query_boundary (self->mesh, a->position, range, a->height, self->walls);
		for (i = 0; i < self->walls->len; i++)
		{
			guint32       w = g_array_index (self->walls, guint32, i);
			const gfloat *s = _lrg_nav_poly_mesh_get_boundary_segment (self->mesh, w);
			ObstVertex    va;
			ObstVertex    vb;
			gfloat        ab[2];
			gfloat        left_of;
			guint         base = self->verts->len;
			Neighbour     n;
			gfloat        rel[2];
			gfloat        t;
			gfloat        len_sq;

			ab[0] = s[3] - s[0];
			ab[1] = s[5] - s[2];
			len_sq = abs_sq2 (ab);
			if (len_sq < 1e-10f)
				continue;
			va.point[0] = s[0];
			va.point[1] = s[2];
			vb.point[0] = s[3];
			vb.point[1] = s[5];
			va.unit_dir[0] = ab[0];
			va.unit_dir[1] = ab[1];
			normalize2 (va.unit_dir);
			vb.unit_dir[0] = -va.unit_dir[0];
			vb.unit_dir[1] = -va.unit_dir[1];
			va.next = va.prev = base + 1;
			vb.next = vb.prev = base;
			g_array_append_val (self->verts, va);
			g_array_append_val (self->verts, vb);

			/* the edge whose right side the agent is on (RVO2's leftOf < 0) */
			{
				gfloat am[2];

				am[0] = va.point[0] - pos[0];
				am[1] = va.point[1] - pos[1];
				left_of = det2 (am, ab);
			}
			if (left_of == 0.0f)
				continue;
			n.index = left_of < 0.0f ? base : base + 1;
			rel[0] = pos[0] - va.point[0];
			rel[1] = pos[1] - va.point[1];
			t = CLAMP (dot2 (rel, ab) / len_sq, 0.0f, 1.0f);
			rel[0] = pos[0] - (va.point[0] + t * ab[0]);
			rel[1] = pos[1] - (va.point[1] + t * ab[1]);
			n.dist_sq = abs_sq2 (rel);
			g_array_append_val (firsts, n);
			g_array_append_val (a->debug_walls, w);
		}
		g_array_sort (firsts, compare_neighbours);
		order = g_new (guint, MAX (firsts->len, 1u));
		for (i = 0; i < firsts->len; i++)
			order[i] = g_array_index (firsts, Neighbour, i).index;
		orca_obstacle_lines (pos, vel, MAX (a->radius * 0.25f, 0.02f), a->time_horizon_obst,
		                     (const ObstVertex *) (gpointer) self->verts->data, order, firsts->len,
		                     self->lines);
	}
	n_obst_lines = self->lines->len;

	/* --- agents: the nearest max-neighbours --- */
	used = MIN (neighbours->len, a->max_neighbours);
	for (i = 0; i < used; i++)
	{
		const Neighbour *n = &g_array_index (neighbours, Neighbour, i);
		LrgNavAgent     *b = g_ptr_array_index (self->agents, n->index);
		gfloat           bpos[2];
		gfloat           bvel[2];

		bpos[0] = b->position[0];
		bpos[1] = b->position[2];
		bvel[0] = b->velocity[0];
		bvel[1] = b->velocity[2];
		orca_agent_line (pos, vel, a->radius, bpos, bvel, b->radius, a->time_horizon, delta,
		                 avoid_share (a, b), self->lines);
		g_array_append_val (a->debug_neighbours, n->index);
	}

	/* --- solve --- */
	fail = linear_program2 ((const OrcaLine *) (gpointer) self->lines->data, self->lines->len, a->max_speed,
	                        pref, FALSE, result);
	if (fail < self->lines->len)
		linear_program3 ((const OrcaLine *) (gpointer) self->lines->data, self->lines->len, n_obst_lines, fail,
		                 a->max_speed, self->proj, result);
	a->new_velocity[0] = result[0];
	a->new_velocity[1] = result[1];
	g_array_set_size (a->debug_lines, 0);
	g_array_append_vals (a->debug_lines, self->lines->data, self->lines->len);
}

/* pushes overlapping agents apart (what acceleration limits let through) */
static void
resolve_overlaps (LrgNavCrowd *self,
                  GArray      *neighbours)
{
	guint it;
	guint i;
	guint j;

	for (it = 0; it < PUSH_ITERATIONS; it++)
	{
		gboolean moved = FALSE;

		build_grid (self);
		for (i = 0; i < self->agents->len; i++)
		{
			LrgNavAgent *a = g_ptr_array_index (self->agents, i);

			if (a->state == LRG_NAV_AGENT_STATE_ON_LINK)
				continue;
			find_neighbours (self, i, a->radius + self->max_radius, 0, neighbours);
			for (j = 0; j < neighbours->len; j++)
			{
				const Neighbour *n = &g_array_index (neighbours, Neighbour, j);
				LrgNavAgent     *b = g_ptr_array_index (self->agents, n->index);
				gfloat           min_d = a->radius + b->radius;
				gfloat           d;
				gfloat           dir[2];
				gfloat           pen;
				gfloat           wa;
				gfloat           wb;
				gfloat           na[3];
				gfloat           nb[3];

				if (n->index < i || b->state == LRG_NAV_AGENT_STATE_ON_LINK)
					continue;
				if (n->dist_sq >= min_d * min_d)
					continue;
				d = sqrtf (n->dist_sq);
				if (d > 1e-6f)
				{
					dir[0] = (b->position[0] - a->position[0]) / d;
					dir[1] = (b->position[2] - a->position[2]) / d;
				}
				else
				{
					dir[0] = 1.0f;
					dir[1] = 0.0f;
				}
				pen = min_d - d + 1e-4f;
				wa = avoid_share (a, b);
				wb = avoid_share (b, a);
				if (wa + wb <= 0.0f)
					wa = wb = 0.5f;
				else
				{
					gfloat sum = wa + wb;

					wa /= sum;
					wb /= sum;
				}
				memcpy (na, a->position, sizeof (na));
				memcpy (nb, b->position, sizeof (nb));
				na[0] -= dir[0] * pen * wa;
				na[2] -= dir[1] * pen * wa;
				nb[0] += dir[0] * pen * wb;
				nb[2] += dir[1] * pen * wb;
				if (project (a, na))
					memcpy (a->position, na, sizeof (na));
				if (project (b, nb))
					memcpy (b->position, nb, sizeof (nb));
				moved = TRUE;
			}
		}
		if (!moved)
			break;
	}
}

void
lrg_nav_crowd_step (LrgNavCrowd *self,
                    gfloat       delta)
{
	g_autoptr(GArray) neighbours = NULL;
	gint64            started;
	guint             i;

	g_return_if_fail (LRG_IS_NAV_CROWD (self));

	if (delta <= 0.0f)
		return;
	started = g_get_monotonic_time ();
	g_object_ref (self);
	g_ptr_array_ref (self->agents);
	neighbours = g_array_new (FALSE, FALSE, sizeof (Neighbour));

	/* --- 1. plan and steer --- */
	for (i = 0; i < self->agents->len; i++)
		prepare (g_ptr_array_index (self->agents, i), delta);

	/* --- 2. separation and ORCA, all from the state before the step --- */
	build_grid (self);
	for (i = 0; i < self->agents->len; i++)
	{
		LrgNavAgent *a = g_ptr_array_index (self->agents, i);

		find_neighbours (self, i, a->neighbour_dist, MAX (a->max_neighbours, 1u), neighbours);
		separation (self, a, neighbours);
		if (a->avoidance && a->state != LRG_NAV_AGENT_STATE_ON_LINK)
			solve_agent (self, i, neighbours, delta);
		else
		{
			a->new_velocity[0] = a->desired[0];
			a->new_velocity[1] = a->desired[2];
			g_array_set_size (a->debug_lines, 0);
			g_array_set_size (a->debug_neighbours, 0);
			g_array_set_size (a->debug_walls, 0);
		}
	}

	/* --- 3. integrate --- */
	for (i = 0; i < self->agents->len; i++)
	{
		LrgNavAgent *a = g_ptr_array_index (self->agents, i);

		if (a->state == LRG_NAV_AGENT_STATE_ON_LINK)
			advance_link (a, delta);
		else
			integrate (a, a->new_velocity, delta);
	}

	/* --- 4. push apart what is left overlapping --- */
	resolve_overlaps (self, neighbours);

	/* --- 5. signals --- */
	self->steps++;
	self->last_seconds = (gdouble) (g_get_monotonic_time () - started) / (gdouble) G_USEC_PER_SEC;
	for (i = 0; i < self->agents->len; i++)
		flush_signals (g_ptr_array_index (self->agents, i));
	g_signal_emit (self, crowd_signals[CROWD_SIGNAL_STEPPED], 0, delta);
	g_ptr_array_unref (self->agents);
	g_object_unref (self);
}

/* ==========================================================================
 * Debug draw
 * ========================================================================== */

static GrlColor
agent_rgba (guint8 r,
            guint8 g,
            guint8 b,
            guint8 a)
{
	GrlColor c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

static GrlVector3
agent_v3 (gfloat x,
          gfloat y,
          gfloat z)
{
	GrlVector3 v;

	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

static GrlColor
state_color (LrgNavAgentState state)
{
	switch (state)
	{
	case LRG_NAV_AGENT_STATE_MOVING:  return agent_rgba (70, 140, 255, 255);
	case LRG_NAV_AGENT_STATE_ON_LINK: return agent_rgba (40, 230, 220, 255);
	case LRG_NAV_AGENT_STATE_ARRIVED: return agent_rgba (80, 220, 100, 255);
	case LRG_NAV_AGENT_STATE_FAILED:  return agent_rgba (240, 60, 60, 255);
	case LRG_NAV_AGENT_STATE_IDLE:
	default:                          return agent_rgba (170, 170, 170, 255);
	}
}

void
lrg_nav_crowd_debug_draw (LrgNavCrowd      *self,
                          LrgDebugDraw     *draw,
                          LrgNavDebugFlags  flags)
{
	guint i;

	g_return_if_fail (LRG_IS_NAV_CROWD (self));
	g_return_if_fail (LRG_IS_DEBUG_DRAW (draw));

	if (self->mesh != NULL)
		lrg_nav_poly_mesh_debug_draw (self->mesh, draw, flags);
	lrg_debug_draw_push (draw);
	lrg_debug_draw_set_category (draw, LRG_DEBUG_CATEGORY_NAVIGATION);
	for (i = 0; i < self->agents->len; i++)
	{
		LrgNavAgent *a = g_ptr_array_index (self->agents, i);
		gfloat       lift = 0.05f;
		GrlVector3   feet = agent_v3 (a->position[0], a->position[1] + lift, a->position[2]);
		GrlVector3   head = agent_v3 (a->position[0], a->position[1] + a->height, a->position[2]);
		GrlColor     col = state_color (a->state);
		GrlVector3   up = agent_v3 (0.0f, 1.0f, 0.0f);
		guint        k;

		if (flags & LRG_NAV_DEBUG_AGENTS)
		{
			lrg_debug_draw_cylinder (draw, &feet, &head, a->radius, &col, 0.0f);
			lrg_debug_draw_set_thickness (draw, 2.0f);
			lrg_debug_draw_circle (draw, &feet, &up, a->radius, &col, 0.0f);
			lrg_debug_draw_set_thickness (draw, 1.0f);
		}
		if ((flags & LRG_NAV_DEBUG_PATHS) && a->path->len >= 6 && a->corner < n_points (a))
		{
			GrlColor   pc = agent_rgba (255, 230, 80, 220);
			GrlVector3 p = feet;
			const gfloat *c;
			GrlVector3 q;

			for (k = a->corner; k < n_points (a); k++)
			{
				c = point_at (a, k);
				q = agent_v3 (c[0], c[1] + lift, c[2]);
				lrg_debug_draw_line (draw, &p, &q, &pc, 0.0f);
				p = q;
			}
			c = point_at (a, a->corner);
			q = agent_v3 (c[0], c[1] + lift, c[2]);
			lrg_debug_draw_point (draw, &q, 0.15f, &pc, 0.0f);
		}
		if (flags & LRG_NAV_DEBUG_VELOCITIES)
		{
			GrlColor   wc = agent_rgba (255, 255, 255, 200);
			GrlColor   vc = agent_rgba (60, 255, 90, 255);
			GrlVector3 base = agent_v3 (a->position[0], a->position[1] + 0.3f, a->position[2]);
			GrlVector3 tip;

			tip = agent_v3 (base.x + a->desired[0] * 0.5f, base.y, base.z + a->desired[2] * 0.5f);
			lrg_debug_draw_line (draw, &base, &tip, &wc, 0.0f);
			tip = agent_v3 (base.x + a->velocity[0] * 0.5f, base.y, base.z + a->velocity[2] * 0.5f);
			lrg_debug_draw_arrow (draw, &base, &tip, 0.15f, &vc, 0.0f);
		}
		if ((flags & LRG_NAV_DEBUG_ORCA) && (self->debug_agent == NULL || self->debug_agent == a))
		{
			/* velocity space drawn around the agent at a 0.5 s scale */
			GrlColor oc = agent_rgba (255, 80, 200, 200);
			gfloat   y = a->position[1] + 0.35f;

			for (k = 0; k < a->debug_lines->len; k++)
			{
				const OrcaLine *l = &g_array_index (a->debug_lines, OrcaLine, k);
				gfloat          px = a->position[0] + l->point[0] * 0.5f;
				gfloat          pz = a->position[2] + l->point[1] * 0.5f;
				GrlVector3      s = agent_v3 (px - l->direction[0] * 1.5f, y, pz - l->direction[1] * 1.5f);
				GrlVector3      e = agent_v3 (px + l->direction[0] * 1.5f, y, pz + l->direction[1] * 1.5f);
				GrlVector3      m = agent_v3 (px, y, pz);
				GrlVector3      t = agent_v3 (px - l->direction[1] * 0.2f, y, pz + l->direction[0] * 0.2f);

				lrg_debug_draw_line (draw, &s, &e, &oc, 0.0f);
				lrg_debug_draw_line (draw, &m, &t, &oc, 0.0f);
			}
		}
		if ((flags & LRG_NAV_DEBUG_NEIGHBOURS) && (self->debug_agent == NULL || self->debug_agent == a))
		{
			GrlColor nc = agent_rgba (200, 200, 255, 120);
			GrlColor wc = agent_rgba (255, 120, 40, 200);

			for (k = 0; k < a->debug_neighbours->len; k++)
			{
				guint        other = g_array_index (a->debug_neighbours, guint, k);
				LrgNavAgent *b;
				GrlVector3   q;

				if (other >= self->agents->len)
					continue;
				b = g_ptr_array_index (self->agents, other);
				q = agent_v3 (b->position[0], b->position[1] + lift, b->position[2]);
				lrg_debug_draw_line (draw, &feet, &q, &nc, 0.0f);
			}
			for (k = 0; self->mesh != NULL && k < a->debug_walls->len; k++)
			{
				const gfloat *s = _lrg_nav_poly_mesh_get_boundary_segment (self->mesh,
				                                                            g_array_index (a->debug_walls, guint32, k));
				GrlVector3    mid = agent_v3 ((s[0] + s[3]) * 0.5f, (s[1] + s[4]) * 0.5f + lift, (s[2] + s[5]) * 0.5f);

				lrg_debug_draw_line (draw, &feet, &mid, &wc, 0.0f);
			}
		}
	}
	lrg_debug_draw_pop (draw);
}
