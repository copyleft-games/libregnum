/* lrg-animator-component3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnimatorComponent3D: one animator step (evaluate()):
 *   1. resolve the graph (load the .ranim and its clips the first time)
 *      and the skeleton (explicit, else the skinned model's);
 *   2. advance the graph by delta x speed (layers, transitions, events);
 *   3. blend the layers into a pose and take the root motion out of it;
 *   4. write the pose into the skeleton, run the IK goals on it in the
 *      entity's world frame;
 *   5. build the skinning palette (bone world x inverse bind) and hand it
 *      to the entity's LrgSkinnedModelComponent;
 *   6. move and turn the entity by the root motion.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-animator-component3d.h"
#include "../lrg-entity3d.h"

struct _LrgAnimatorComponent3D
{
	LrgComponent parent_instance;

	LrgAnimGraph     *graph;
	gchar            *graph_path;
	gboolean          load_failed;
	LrgSkeleton      *skeleton;
	gfloat            speed;
	gboolean          playing;
	gint              apply_root_motion;  /* -1: as the graph says */
	gboolean          ik_enabled;

	LrgIKRaycastFunc  raycast;
	gpointer          raycast_data;
	GDestroyNotify    raycast_destroy;

	GArray           *poses;    /* LrgBonePose */
	GArray           *palette;  /* gfloat */
	gfloat            root_delta[3];
	gfloat            root_yaw;
};

G_DEFINE_FINAL_TYPE (LrgAnimatorComponent3D, lrg_animator_component3d, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_GRAPH,
	PROP_GRAPH_PATH,
	PROP_SKELETON,
	PROP_SPEED,
	PROP_PLAYING,
	PROP_APPLY_ROOT_MOTION,
	PROP_IK_ENABLED,
	N_PROPS
};

enum
{
	SIGNAL_EVENT,
	SIGNAL_STATE_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* column-major r = a * b */
static void
mat4_mul (const gfloat *a,
          const gfloat *b,
          gfloat       *r)
{
	guint c, row, k;

	for (c = 0; c < 4; c++)
		for (row = 0; row < 4; row++)
		{
			gfloat s = 0.0f;

			for (k = 0; k < 4; k++)
				s += a[k * 4 + row] * b[c * 4 + k];
			r[c * 4 + row] = s;
		}
}

static void
on_graph_event (LrgAnimGraph      *graph,
                const gchar       *layer_name,
                const gchar       *state_name,
                LrgAnimationEvent *event,
                gpointer           user_data)
{
	LrgAnimatorComponent3D *self = LRG_ANIMATOR_COMPONENT3D (user_data);

	g_signal_emit (self, signals[SIGNAL_EVENT],
	               event->name != NULL ? g_quark_from_string (event->name) : 0,
	               layer_name, state_name, event);
}

static void
on_graph_state (LrgAnimGraph *graph,
                const gchar  *layer_name,
                const gchar  *state_name,
                gpointer      user_data)
{
	g_signal_emit (LRG_ANIMATOR_COMPONENT3D (user_data), signals[SIGNAL_STATE_CHANGED], 0,
	               layer_name, state_name);
}

static void
attach_graph (LrgAnimatorComponent3D *self,
              LrgAnimGraph           *graph)
{
	if (self->graph != NULL)
		g_signal_handlers_disconnect_by_data (self->graph, self);
	g_set_object (&self->graph, graph);
	if (graph != NULL)
	{
		g_signal_connect_object (graph, "event", G_CALLBACK (on_graph_event), self, 0);
		g_signal_connect_object (graph, "state-changed", G_CALLBACK (on_graph_state), self, 0);
	}
}

/* Loads the graph from its path once. */
static void
ensure_graph (LrgAnimatorComponent3D *self)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgAnimGraph) graph = NULL;

	if (self->graph != NULL || self->graph_path == NULL || self->load_failed)
		return;
	graph = lrg_anim_graph_new_from_file (self->graph_path, &error);
	if (graph != NULL && !lrg_anim_graph_resolve_clips (graph, &error))
		g_clear_object (&graph);
	if (graph == NULL)
	{
		g_warning ("LrgAnimatorComponent3D: cannot load '%s': %s", self->graph_path, error->message);
		self->load_failed = TRUE;
		return;
	}
	attach_graph (self, graph);
}

static LrgSkinnedModelComponent *
find_skinned (LrgAnimatorComponent3D *self)
{
	LrgEntity3D  *entity = lrg_component_get_entity (LRG_COMPONENT (self));
	LrgComponent *c;

	if (entity == NULL)
		return NULL;
	c = lrg_entity3d_get_component (entity, LRG_TYPE_SKINNED_MODEL_COMPONENT);
	return c != NULL ? LRG_SKINNED_MODEL_COMPONENT (c) : NULL;
}

/* The palette for @skeleton: through the skinned mesh's inverse binds
 * when the model is a skinned mesh, else the skeleton's (computed from the
 * bind pose the first time none are set). */
static void
build_palette (LrgAnimatorComponent3D   *self,
               LrgSkeleton              *skeleton,
               LrgSkinnedModelComponent *smc,
               guint                     n)
{
	LrgSkinnedMesh *mesh = smc != NULL ? lrg_skinned_model_component_get_skinned_mesh (smc) : NULL;
	gfloat         *pal;
	guint           b;

	g_array_set_size (self->palette, 16 * n);
	pal = (gfloat *) (gpointer) self->palette->data;

	if (mesh != NULL && lrg_skinned_mesh_get_bone_count (mesh) <= n)
	{
		g_array_set_size (self->palette, 16 * lrg_skinned_mesh_get_bone_count (mesh));
		pal = (gfloat *) (gpointer) self->palette->data;
		if (lrg_skinned_mesh_compute_palette_from_skeleton (mesh, skeleton, pal))
			return;
		g_array_set_size (self->palette, 16 * n);
		pal = (gfloat *) (gpointer) self->palette->data;
	}

	if (n > 0 && lrg_skeleton_get_inverse_bind_matrix (skeleton, 0) == NULL)
		lrg_skeleton_compute_inverse_bind_matrices (skeleton);
	for (b = 0; b < n; b++)
	{
		LrgBone      *bone = lrg_skeleton_get_bone (skeleton, (gint) b);
		const gfloat *inv = lrg_skeleton_get_inverse_bind_matrix (skeleton, (gint) b);
		gfloat        world[16];

		if (bone == NULL)
		{
			memset (pal + 16 * b, 0, 16 * sizeof (gfloat));
			pal[16 * b] = pal[16 * b + 5] = pal[16 * b + 10] = pal[16 * b + 15] = 1.0f;
			continue;
		}
		lrg_bone_pose_to_matrix (lrg_bone_get_world_pose (bone), world);
		if (inv != NULL)
			mat4_mul (world, inv, pal + 16 * b);
		else
			memcpy (pal + 16 * b, world, sizeof world);
	}
}

/* Moves the entity by the model-space root motion. */
static void
apply_root_motion (LrgAnimatorComponent3D *self,
                   LrgEntity3D            *entity,
                   const gfloat           *delta,
                   gfloat                  yaw,
                   gboolean                move)
{
	gfloat rot[4], scale[3], pos[3], d[3], t[3];
	gfloat tx, ty, tz;

	lrg_entity3d_get_rotation (entity, rot);
	lrg_entity3d_get_scale (entity, scale);
	lrg_entity3d_get_position (entity, pos);

	/* model space -> the entity's parent space: scale, then rotate */
	d[0] = delta[0] * scale[0];
	d[1] = delta[1] * scale[1];
	d[2] = delta[2] * scale[2];
	tx = 2.0f * (rot[1] * d[2] - rot[2] * d[1]);
	ty = 2.0f * (rot[2] * d[0] - rot[0] * d[2]);
	tz = 2.0f * (rot[0] * d[1] - rot[1] * d[0]);
	t[0] = d[0] + rot[3] * tx + (rot[1] * tz - rot[2] * ty);
	t[1] = d[1] + rot[3] * ty + (rot[2] * tx - rot[0] * tz);
	t[2] = d[2] + rot[3] * tz + (rot[0] * ty - rot[1] * tx);
	self->root_delta[0] = t[0];
	self->root_delta[1] = t[1];
	self->root_delta[2] = t[2];
	if (!move)
		return;
	if (t[0] != 0.0f || t[1] != 0.0f || t[2] != 0.0f)
		lrg_entity3d_set_position_xyz (entity, pos[0] + t[0], pos[1] + t[1], pos[2] + t[2]);
	if (yaw != 0.0f)
	{
		/* turn about the entity's own up axis: rot * yaw */
		gfloat q[4] = { 0.0f, sinf (0.5f * yaw), 0.0f, cosf (0.5f * yaw) };
		gfloat r[4];

		r[0] = rot[3] * q[0] + rot[0] * q[3] + rot[1] * q[2] - rot[2] * q[1];
		r[1] = rot[3] * q[1] - rot[0] * q[2] + rot[1] * q[3] + rot[2] * q[0];
		r[2] = rot[3] * q[2] + rot[0] * q[1] - rot[1] * q[0] + rot[2] * q[3];
		r[3] = rot[3] * q[3] - rot[0] * q[0] - rot[1] * q[1] - rot[2] * q[2];
		lrg_entity3d_set_rotation (entity, r);
	}
}

/* ==========================================================================
 * Evaluation
 * ========================================================================== */

gboolean
lrg_animator_component3d_evaluate (LrgAnimatorComponent3D *self,
                                   gfloat                  delta_time)
{
	LrgSkinnedModelComponent *smc;
	LrgSkeleton              *skeleton;
	LrgEntity3D              *entity;
	LrgBonePose              *poses;
	gfloat                    delta[3], yaw;
	gfloat                    model[16];
	gboolean                  apply;
	guint                     n, i;

	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), FALSE);

	/* 1. graph and skeleton */
	ensure_graph (self);
	smc = find_skinned (self);
	skeleton = lrg_animator_component3d_get_skeleton (self);
	if (self->graph == NULL || skeleton == NULL)
		return FALSE;
	entity = lrg_component_get_entity (LRG_COMPONENT (self));

	/* 2. time */
	if (self->playing)
		lrg_anim_graph_update (self->graph, delta_time * self->speed);
	else if (!lrg_anim_graph_is_started (self->graph))
		lrg_anim_graph_start (self->graph);

	/* 3. blend */
	n = lrg_skeleton_get_bone_count (skeleton);
	g_array_set_size (self->poses, n);
	poses = (LrgBonePose *) (gpointer) self->poses->data;
	lrg_anim_graph_evaluate (self->graph, skeleton, poses, n, delta, &yaw);
	if (!self->playing)
	{
		delta[0] = delta[1] = delta[2] = 0.0f;
		yaw = 0.0f;
	}

	/* 4. skeleton + IK */
	lrg_anim_pose_apply (skeleton, poses, n);
	if (self->ik_enabled)
	{
		if (entity != NULL)
			lrg_entity3d_get_world_matrix (entity, model);
		for (i = 0; i < lrg_anim_graph_get_ik_goal_count (self->graph); i++)
			lrg_ik_goal_apply (lrg_anim_graph_get_ik_goal (self->graph, i), skeleton,
			                   entity != NULL ? model : NULL, self->raycast, self->raycast_data);
	}

	/* 5. palette */
	build_palette (self, skeleton, smc, n);
	if (smc != NULL)
		lrg_skinned_model_component_set_palette (smc, (const gfloat *) (gpointer) self->palette->data,
		                                         self->palette->len / 16);

	/* 6. root motion */
	apply = self->apply_root_motion >= 0 ? self->apply_root_motion
	                                     : lrg_anim_graph_get_apply_root_motion (self->graph);
	self->root_yaw = yaw;
	if (entity != NULL)
		apply_root_motion (self, entity, delta, yaw, apply);  /* moves only when applying */
	else
	{
		memcpy (self->root_delta, delta, sizeof self->root_delta);
	}
	return TRUE;
}

/* ==========================================================================
 * LrgComponent
 * ========================================================================== */

static void
lrg_animator_component3d_update (LrgComponent *component,
                                 gfloat        delta)
{
	lrg_animator_component3d_evaluate (LRG_ANIMATOR_COMPONENT3D (component), delta);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_animator_component3d_dispose (GObject *object)
{
	LrgAnimatorComponent3D *self = LRG_ANIMATOR_COMPONENT3D (object);

	attach_graph (self, NULL);
	g_clear_object (&self->skeleton);
	if (self->raycast_destroy != NULL)
		self->raycast_destroy (self->raycast_data);
	self->raycast = NULL;
	self->raycast_data = NULL;
	self->raycast_destroy = NULL;

	G_OBJECT_CLASS (lrg_animator_component3d_parent_class)->dispose (object);
}

static void
lrg_animator_component3d_finalize (GObject *object)
{
	LrgAnimatorComponent3D *self = LRG_ANIMATOR_COMPONENT3D (object);

	g_free (self->graph_path);
	g_array_unref (self->poses);
	g_array_unref (self->palette);

	G_OBJECT_CLASS (lrg_animator_component3d_parent_class)->finalize (object);
}

static void
lrg_animator_component3d_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
	LrgAnimatorComponent3D *self = LRG_ANIMATOR_COMPONENT3D (object);

	switch (prop_id)
	{
	case PROP_GRAPH:             g_value_set_object (value, self->graph); break;
	case PROP_GRAPH_PATH:        g_value_set_string (value, self->graph_path); break;
	case PROP_SKELETON:          g_value_set_object (value, self->skeleton); break;
	case PROP_SPEED:             g_value_set_float (value, self->speed); break;
	case PROP_PLAYING:           g_value_set_boolean (value, self->playing); break;
	case PROP_APPLY_ROOT_MOTION: g_value_set_boolean (value, lrg_animator_component3d_get_apply_root_motion (self)); break;
	case PROP_IK_ENABLED:        g_value_set_boolean (value, self->ik_enabled); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animator_component3d_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
	LrgAnimatorComponent3D *self = LRG_ANIMATOR_COMPONENT3D (object);

	switch (prop_id)
	{
	case PROP_GRAPH:
		lrg_animator_component3d_set_graph (self, g_value_get_object (value));
		break;
	case PROP_GRAPH_PATH:
		lrg_animator_component3d_set_graph_path (self, g_value_get_string (value));
		break;
	case PROP_SKELETON:
		lrg_animator_component3d_set_skeleton (self, g_value_get_object (value));
		break;
	case PROP_SPEED:
		lrg_animator_component3d_set_speed (self, g_value_get_float (value));
		break;
	case PROP_PLAYING:
		lrg_animator_component3d_set_playing (self, g_value_get_boolean (value));
		break;
	case PROP_APPLY_ROOT_MOTION:
		lrg_animator_component3d_set_apply_root_motion (self, g_value_get_boolean (value));
		break;
	case PROP_IK_ENABLED:
		lrg_animator_component3d_set_ik_enabled (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animator_component3d_class_init (LrgAnimatorComponent3DClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_animator_component3d_dispose;
	object_class->finalize = lrg_animator_component3d_finalize;
	object_class->get_property = lrg_animator_component3d_get_property;
	object_class->set_property = lrg_animator_component3d_set_property;

	component_class->update = lrg_animator_component3d_update;

	/**
	 * LrgAnimatorComponent3D:graph:
	 *
	 * The running #LrgAnimGraph.
	 */
	properties[PROP_GRAPH] = g_param_spec_object ("graph", NULL, "Animation graph", LRG_TYPE_ANIM_GRAPH, rw);

	/**
	 * LrgAnimatorComponent3D:graph-path:
	 *
	 * The .ranim file loaded on the next update.
	 */
	properties[PROP_GRAPH_PATH] = g_param_spec_string ("graph-path", NULL, ".ranim path", NULL, rw);

	/**
	 * LrgAnimatorComponent3D:skeleton:
	 *
	 * The skeleton posed (default: the skinned model's).
	 */
	properties[PROP_SKELETON] = g_param_spec_object ("skeleton", NULL, "Skeleton", LRG_TYPE_SKELETON, rw);

	/**
	 * LrgAnimatorComponent3D:speed:
	 *
	 * Time scale of the graph.
	 */
	properties[PROP_SPEED] = g_param_spec_float ("speed", NULL, "Playback speed", 0.0f, 100.0f, 1.0f, rw);

	/**
	 * LrgAnimatorComponent3D:playing:
	 *
	 * Whether time advances.
	 */
	properties[PROP_PLAYING] = g_param_spec_boolean ("playing", NULL, "Playing", TRUE, rw);

	/**
	 * LrgAnimatorComponent3D:apply-root-motion:
	 *
	 * Whether the entity follows the root motion (defaults to the graph's
	 * `root_motion.apply`).
	 */
	properties[PROP_APPLY_ROOT_MOTION] =
		g_param_spec_boolean ("apply-root-motion", NULL, "Apply root motion", TRUE, rw);

	/**
	 * LrgAnimatorComponent3D:ik-enabled:
	 *
	 * Whether the IK post-pass runs.
	 */
	properties[PROP_IK_ENABLED] = g_param_spec_boolean ("ik-enabled", NULL, "IK enabled", TRUE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgAnimatorComponent3D::event:
	 * @self: the animator
	 * @layer_name: (nullable): the layer that played it
	 * @state_name: the state whose clip carries it
	 * @event: the clip event
	 *
	 * An animation event (footstep, hit frame, sound cue). Detailed by
	 * the event name: connect to `"event::footstep"`.
	 */
	signals[SIGNAL_EVENT] =
		g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING,
		              LRG_TYPE_ANIMATION_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgAnimatorComponent3D::state-changed:
	 * @self: the animator
	 * @layer_name: (nullable): the layer
	 * @state_name: its new current state
	 *
	 * A layer entered a state.
	 */
	signals[SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_animator_component3d_init (LrgAnimatorComponent3D *self)
{
	self->speed = 1.0f;
	self->playing = TRUE;
	self->apply_root_motion = -1;
	self->ik_enabled = TRUE;
	self->poses = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
	self->palette = g_array_new (FALSE, TRUE, sizeof (gfloat));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnimatorComponent3D *
lrg_animator_component3d_new (void)
{
	return g_object_new (LRG_TYPE_ANIMATOR_COMPONENT3D, NULL);
}

LrgAnimatorComponent3D *
lrg_animator_component3d_new_with_graph (LrgAnimGraph *graph)
{
	return g_object_new (LRG_TYPE_ANIMATOR_COMPONENT3D, "graph", graph, NULL);
}

LrgAnimGraph *
lrg_animator_component3d_get_graph (LrgAnimatorComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), NULL);
	return self->graph;
}

void
lrg_animator_component3d_set_graph (LrgAnimatorComponent3D *self,
                                    LrgAnimGraph           *graph)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	g_return_if_fail (graph == NULL || LRG_IS_ANIM_GRAPH (graph));

	if (self->graph == graph)
		return;
	attach_graph (self, graph);
	if (graph != NULL)
		lrg_anim_graph_start (graph);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRAPH]);
}

const gchar *
lrg_animator_component3d_get_graph_path (LrgAnimatorComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), NULL);
	return self->graph_path;
}

void
lrg_animator_component3d_set_graph_path (LrgAnimatorComponent3D *self,
                                         const gchar            *path)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));

	if (g_strcmp0 (self->graph_path, path) == 0)
		return;
	g_free (self->graph_path);
	self->graph_path = g_strdup (path);
	self->load_failed = FALSE;
	attach_graph (self, NULL);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRAPH_PATH]);
}

LrgSkeleton *
lrg_animator_component3d_get_skeleton (LrgAnimatorComponent3D *self)
{
	LrgSkinnedModelComponent *smc;

	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), NULL);
	if (self->skeleton != NULL)
		return self->skeleton;
	smc = find_skinned (self);
	return smc != NULL ? lrg_skinned_model_component_get_skeleton (smc) : NULL;
}

void
lrg_animator_component3d_set_skeleton (LrgAnimatorComponent3D *self,
                                       LrgSkeleton            *skeleton)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	g_return_if_fail (skeleton == NULL || LRG_IS_SKELETON (skeleton));
	if (g_set_object (&self->skeleton, skeleton))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON]);
}

LrgSkinnedModelComponent *
lrg_animator_component3d_get_skinned_model (LrgAnimatorComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), NULL);
	return find_skinned (self);
}

void
lrg_animator_component3d_set_float (LrgAnimatorComponent3D *self,
                                    const gchar            *name,
                                    gfloat                  value)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	ensure_graph (self);
	if (self->graph != NULL)
		lrg_anim_graph_set_float (self->graph, name, value);
}

gfloat
lrg_animator_component3d_get_float (LrgAnimatorComponent3D *self,
                                    const gchar            *name)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), 0.0f);
	return self->graph != NULL ? lrg_anim_graph_get_float (self->graph, name) : 0.0f;
}

void
lrg_animator_component3d_set_int (LrgAnimatorComponent3D *self,
                                  const gchar            *name,
                                  gint                    value)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	ensure_graph (self);
	if (self->graph != NULL)
		lrg_anim_graph_set_int (self->graph, name, value);
}

gint
lrg_animator_component3d_get_int (LrgAnimatorComponent3D *self,
                                  const gchar            *name)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), 0);
	return self->graph != NULL ? lrg_anim_graph_get_int (self->graph, name) : 0;
}

void
lrg_animator_component3d_set_bool (LrgAnimatorComponent3D *self,
                                   const gchar            *name,
                                   gboolean                value)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	ensure_graph (self);
	if (self->graph != NULL)
		lrg_anim_graph_set_bool (self->graph, name, value);
}

gboolean
lrg_animator_component3d_get_bool (LrgAnimatorComponent3D *self,
                                   const gchar            *name)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), FALSE);
	return self->graph != NULL && lrg_anim_graph_get_bool (self->graph, name);
}

void
lrg_animator_component3d_set_trigger (LrgAnimatorComponent3D *self,
                                      const gchar            *name)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	ensure_graph (self);
	if (self->graph != NULL)
		lrg_anim_graph_set_trigger (self->graph, name);
}

void
lrg_animator_component3d_reset_trigger (LrgAnimatorComponent3D *self,
                                        const gchar            *name)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	if (self->graph != NULL)
		lrg_anim_graph_reset_trigger (self->graph, name);
}

const gchar *
lrg_animator_component3d_get_current_state (LrgAnimatorComponent3D *self,
                                            const gchar            *layer_name)
{
	LrgAnimationLayer *layer;

	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), NULL);
	if (self->graph == NULL)
		return NULL;
	layer = layer_name != NULL ? lrg_anim_graph_get_layer_by_name (self->graph, layer_name)
	                           : lrg_anim_graph_get_layer (self->graph, 0);
	if (layer == NULL || lrg_animation_layer_get_state_machine (layer) == NULL)
		return NULL;
	return lrg_animation_state_machine_get_current_state_name (lrg_animation_layer_get_state_machine (layer));
}

void
lrg_animator_component3d_set_layer_weight (LrgAnimatorComponent3D *self,
                                           const gchar            *layer_name,
                                           gfloat                  weight)
{
	LrgAnimationLayer *layer;

	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	if (self->graph == NULL)
		return;
	layer = lrg_anim_graph_get_layer_by_name (self->graph, layer_name);
	if (layer != NULL)
		lrg_animation_layer_set_weight (layer, weight);
}

gfloat
lrg_animator_component3d_get_speed (LrgAnimatorComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), 1.0f);
	return self->speed;
}

void
lrg_animator_component3d_set_speed (LrgAnimatorComponent3D *self,
                                    gfloat                  speed)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	speed = MAX (speed, 0.0f);
	if (self->speed == speed)
		return;
	self->speed = speed;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED]);
}

gboolean
lrg_animator_component3d_get_playing (LrgAnimatorComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), FALSE);
	return self->playing;
}

void
lrg_animator_component3d_set_playing (LrgAnimatorComponent3D *self,
                                      gboolean                playing)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	playing = !!playing;
	if (self->playing == playing)
		return;
	self->playing = playing;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

gboolean
lrg_animator_component3d_get_apply_root_motion (LrgAnimatorComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), FALSE);
	if (self->apply_root_motion >= 0)
		return self->apply_root_motion;
	return self->graph != NULL ? lrg_anim_graph_get_apply_root_motion (self->graph) : TRUE;
}

void
lrg_animator_component3d_set_apply_root_motion (LrgAnimatorComponent3D *self,
                                                gboolean                apply)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	apply = !!apply;
	if (self->apply_root_motion == apply)
		return;
	self->apply_root_motion = apply;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_APPLY_ROOT_MOTION]);
}

void
lrg_animator_component3d_get_root_motion (LrgAnimatorComponent3D *self,
                                          gfloat                 *out_translation,
                                          gfloat                 *out_yaw)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	g_return_if_fail (out_translation != NULL);
	memcpy (out_translation, self->root_delta, sizeof self->root_delta);
	if (out_yaw != NULL)
		*out_yaw = self->root_yaw;
}

gboolean
lrg_animator_component3d_get_ik_enabled (LrgAnimatorComponent3D *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), FALSE);
	return self->ik_enabled;
}

void
lrg_animator_component3d_set_ik_enabled (LrgAnimatorComponent3D *self,
                                         gboolean                enabled)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));
	enabled = !!enabled;
	if (self->ik_enabled == enabled)
		return;
	self->ik_enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IK_ENABLED]);
}

gboolean
lrg_animator_component3d_set_ik_target (LrgAnimatorComponent3D *self,
                                        const gchar            *goal_name,
                                        gfloat                  x,
                                        gfloat                  y,
                                        gfloat                  z)
{
	LrgIKGoal *goal;

	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), FALSE);
	ensure_graph (self);
	goal = self->graph != NULL ? lrg_anim_graph_get_ik_goal_by_name (self->graph, goal_name) : NULL;
	if (goal == NULL)
		return FALSE;
	lrg_ik_goal_set_target (goal, x, y, z);
	return TRUE;
}

gboolean
lrg_animator_component3d_set_ik_weight (LrgAnimatorComponent3D *self,
                                        const gchar            *goal_name,
                                        gfloat                  weight)
{
	LrgIKGoal *goal;

	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), FALSE);
	ensure_graph (self);
	goal = self->graph != NULL ? lrg_anim_graph_get_ik_goal_by_name (self->graph, goal_name) : NULL;
	if (goal == NULL)
		return FALSE;
	lrg_ik_goal_set_weight (goal, weight);
	return TRUE;
}

void
lrg_animator_component3d_set_raycast_func (LrgAnimatorComponent3D *self,
                                           LrgIKRaycastFunc        func,
                                           gpointer                user_data,
                                           GDestroyNotify          destroy)
{
	g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self));

	if (self->raycast_destroy != NULL)
		self->raycast_destroy (self->raycast_data);
	self->raycast = func;
	self->raycast_data = user_data;
	self->raycast_destroy = destroy;
}

const gfloat *
lrg_animator_component3d_get_palette (LrgAnimatorComponent3D *self,
                                      guint                  *out_n_bones)
{
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT3D (self), NULL);
	if (out_n_bones != NULL)
		*out_n_bones = self->palette->len / 16;
	return self->palette->len > 0 ? (const gfloat *) (gpointer) self->palette->data : NULL;
}
