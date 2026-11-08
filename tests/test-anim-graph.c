/* test-anim-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The .ranim animation graph and its runtime: format round trip and
 * strict errors, state machine transitions (parameters, triggers, exit
 * time, any-state, interruption), blend tree weights (1D, 2D cartesian,
 * 2D directional), layer masks and additive layers, root motion, IK
 * solvers, the animator component in an LrgWorld3D (palette and events)
 * and a GPU check of a skinned stick figure driven by the component.
 *
 * Everything is built procedurally (skeletons and clips with linear
 * channels). Set LRG_ANIMGRAPH_SHOTS to a directory to also save the GPU
 * check's frames as PNGs.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

/* ==========================================================================
 * Procedural skeletons and clips
 * ========================================================================== */

/* Adds a bone at local (x, y, z) with a rotation about Z (radians). */
static void
add_bone (LrgSkeleton *sk,
          const gchar *name,
          gint         index,
          gint         parent,
          gfloat       x,
          gfloat       y,
          gfloat       z,
          gfloat       rot_z,
          gfloat       length)
{
	g_autoptr(LrgBone) b = lrg_bone_new (name, index);
	LrgBonePose        p;

	lrg_bone_pose_set_identity (&p);
	lrg_bone_pose_set_position (&p, x, y, z);
	lrg_bone_pose_set_rotation (&p, 0.0f, 0.0f, sinf (0.5f * rot_z), cosf (0.5f * rot_z));
	lrg_bone_set_parent_index (b, parent);
	lrg_bone_set_bind_pose (b, &p);
	lrg_bone_set_local_pose (b, &p);
	lrg_bone_set_length (b, length);
	lrg_skeleton_add_bone (sk, b);
}

/* A 16-bone stick figure: bones point along their local +Y; arms and legs
 * hang down (rotated half a turn about Z). */
static LrgSkeleton *
make_figure (void)
{
	LrgSkeleton *sk = lrg_skeleton_new ();

	add_bone (sk, "root", 0, -1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	add_bone (sk, "hips", 1, 0, 0.0f, 1.0f, 0.0f, 0.0f, 0.6f);
	add_bone (sk, "spine", 2, 1, 0.0f, 0.6f, 0.0f, 0.0f, 0.6f);
	add_bone (sk, "head", 3, 2, 0.0f, 0.6f, 0.0f, 0.0f, 0.3f);
	add_bone (sk, "l_upper_arm", 4, 2, -0.35f, 0.5f, 0.0f, (gfloat) G_PI, 0.5f);
	add_bone (sk, "l_lower_arm", 5, 4, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f);
	add_bone (sk, "l_hand", 6, 5, 0.0f, 0.5f, 0.0f, 0.0f, 0.12f);
	add_bone (sk, "r_upper_arm", 7, 2, 0.35f, 0.5f, 0.0f, (gfloat) G_PI, 0.5f);
	add_bone (sk, "r_lower_arm", 8, 7, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f);
	add_bone (sk, "r_hand", 9, 8, 0.0f, 0.5f, 0.0f, 0.0f, 0.12f);
	add_bone (sk, "l_thigh", 10, 1, -0.15f, 0.0f, 0.0f, (gfloat) G_PI, 0.5f);
	add_bone (sk, "l_shin", 11, 10, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f);
	add_bone (sk, "l_foot", 12, 11, 0.0f, 0.5f, 0.0f, 0.0f, 0.12f);
	add_bone (sk, "r_thigh", 13, 1, 0.15f, 0.0f, 0.0f, (gfloat) G_PI, 0.5f);
	add_bone (sk, "r_shin", 14, 13, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f);
	add_bone (sk, "r_foot", 15, 14, 0.0f, 0.5f, 0.0f, 0.0f, 0.12f);
	lrg_skeleton_calculate_world_poses (sk);
	return sk;
}

static const LrgBonePose *
bind_of (LrgSkeleton *sk,
         const gchar *bone)
{
	return lrg_bone_get_bind_pose (lrg_skeleton_get_bone_by_name (sk, bone));
}

static LrgAnimationClip *
make_clip (const gchar *name,
           gfloat       duration,
           gboolean     loop)
{
	LrgAnimationClip *clip = lrg_animation_clip_new (name);

	lrg_animation_clip_set_duration (clip, duration);
	lrg_animation_clip_set_loop_mode (clip, loop ? LRG_ANIMATION_LOOP_REPEAT : LRG_ANIMATION_LOOP_NONE);
	return clip;
}

/* A linear rotation channel about (ax, ay, az): bind rotation times the
 * angle at each key. */
static void
rot_channel (LrgAnimationClip *clip,
             LrgSkeleton      *sk,
             const gchar      *bone,
             gfloat            ax,
             gfloat            ay,
             gfloat            az,
             guint             n,
             const gfloat     *times,
             const gfloat     *angles)
{
	const LrgBonePose *bind = bind_of (sk, bone);
	g_autofree gfloat *values = g_new (gfloat, 4 * n);
	g_autoptr(GError)  error = NULL;
	guint              track = lrg_animation_clip_add_track (clip, bone);
	guint              i;

	for (i = 0; i < n; i++)
	{
		gfloat s = sinf (0.5f * angles[i]), c = cosf (0.5f * angles[i]);
		gfloat q[4] = { ax * s, ay * s, az * s, c };
		gfloat b[4] = { bind->rotation_x, bind->rotation_y, bind->rotation_z, bind->rotation_w };

		/* b * q: rotate in the bone's own frame */
		values[4 * i] = b[3] * q[0] + b[0] * q[3] + b[1] * q[2] - b[2] * q[1];
		values[4 * i + 1] = b[3] * q[1] - b[0] * q[2] + b[1] * q[3] + b[2] * q[0];
		values[4 * i + 2] = b[3] * q[2] + b[0] * q[1] - b[1] * q[0] + b[2] * q[3];
		values[4 * i + 3] = b[3] * q[3] - b[0] * q[0] - b[1] * q[1] - b[2] * q[2];
	}
	lrg_animation_clip_set_track_base_pose (clip, track, bind);
	g_assert_true (lrg_animation_clip_set_track_channel (clip, track, LRG_ANIMATION_CHANNEL_ROTATION,
	                                                     LRG_ANIMATION_INTERPOLATION_LINEAR, n, times,
	                                                     values, &error));
	g_assert_no_error (error);
}

/* A linear translation channel (absolute local positions). */
static void
trans_channel (LrgAnimationClip *clip,
               LrgSkeleton      *sk,
               const gchar      *bone,
               guint             n,
               const gfloat     *times,
               const gfloat     *xyz)
{
	g_autoptr(GError) error = NULL;
	guint             track = lrg_animation_clip_add_track (clip, bone);

	lrg_animation_clip_set_track_base_pose (clip, track, bind_of (sk, bone));
	g_assert_true (lrg_animation_clip_set_track_channel (clip, track, LRG_ANIMATION_CHANNEL_TRANSLATION,
	                                                     LRG_ANIMATION_INTERPOLATION_LINEAR, n, times, xyz,
	                                                     &error));
	g_assert_no_error (error);
}

/* "idle": a still pose (one key per limb). */
static LrgAnimationClip *
clip_idle (LrgSkeleton *sk)
{
	LrgAnimationClip *c = make_clip ("idle", 1.0f, TRUE);
	gfloat            t[2] = { 0.0f, 1.0f }, a[2] = { 0.0f, 0.0f };

	rot_channel (c, sk, "l_thigh", 1, 0, 0, 2, t, a);
	return c;
}

/* "walk": legs and arms swing +-35 degrees about X over one second. */
static LrgAnimationClip *
clip_walk (LrgSkeleton *sk,
           gfloat       amplitude)
{
	LrgAnimationClip *c = make_clip ("walk", 1.0f, TRUE);
	gfloat            t[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
	gfloat            a[5] = { 0.0f, amplitude, 0.0f, -amplitude, 0.0f };
	gfloat            b[5] = { 0.0f, -amplitude, 0.0f, amplitude, 0.0f };

	rot_channel (c, sk, "l_thigh", 1, 0, 0, 5, t, a);
	rot_channel (c, sk, "r_thigh", 1, 0, 0, 5, t, b);
	rot_channel (c, sk, "l_upper_arm", 1, 0, 0, 5, t, b);
	rot_channel (c, sk, "r_upper_arm", 1, 0, 0, 5, t, a);
	return c;
}

/* "wave": the right arm raised to the side, forearm waving. */
static LrgAnimationClip *
clip_wave (LrgSkeleton *sk)
{
	LrgAnimationClip *c = make_clip ("wave", 1.0f, TRUE);
	gfloat            t[3] = { 0.0f, 0.5f, 1.0f };
	gfloat            up[3] = { 2.4f, 2.4f, 2.4f };
	gfloat            w[3] = { -0.6f, 0.6f, -0.6f };

	rot_channel (c, sk, "r_upper_arm", 0, 0, 1, 3, t, up);
	rot_channel (c, sk, "r_lower_arm", 0, 0, 1, 3, t, w);
	return c;
}

/* "move": the root slides +2 X per one-second loop (root motion). */
static LrgAnimationClip *
clip_move (LrgSkeleton *sk)
{
	LrgAnimationClip *c = make_clip ("move", 1.0f, TRUE);
	gfloat            t[2] = { 0.0f, 1.0f };
	gfloat            p[6] = { 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f };

	trans_channel (c, sk, "root", 2, t, p);
	return c;
}

static gfloat
dist3 (const gfloat *a,
       const gfloat *b)
{
	return sqrtf ((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

static void
world_pos (LrgSkeleton *sk,
           const gchar *bone,
           gfloat      *out)
{
	const LrgBonePose *w = lrg_bone_get_world_pose (lrg_skeleton_get_bone_by_name (sk, bone));

	out[0] = w->position_x;
	out[1] = w->position_y;
	out[2] = w->position_z;
}

/* ==========================================================================
 * Format
 * ========================================================================== */

static const gchar *GRAPH_TEXT =
	"version: 1\n"
	"name: hero\n"
	"clips:\n"
	"  idle: {source: hero.glb, animation: Idle}\n"
	"  walk: {source: hero.glb, animation: Walk}\n"
	"  wave: {}\n"
	"  move: {}\n"
	"parameters:\n"
	"  - {name: speed, type: float}\n"
	"  - {name: dir_x, type: float}\n"
	"  - {name: dir_y, type: float}\n"
	"  - {name: grounded, type: bool, default: true}\n"
	"  - {name: jump, type: trigger}\n"
	"  - {name: weapon, type: int, default: 2}\n"
	"root_motion: {bone: root, mode: xz, rotation: false, apply: true}\n"
	"layers:\n"
	"  - name: base\n"
	"    default: idle\n"
	"    states:\n"
	"      - {name: idle, clip: idle, editor_pos: [10, 20]}\n"
	"      - name: locomotion\n"
	"        blend_tree:\n"
	"          type: 1d\n"
	"          parameter: speed\n"
	"          children:\n"
	"            - {clip: idle, threshold: 0}\n"
	"            - {clip: walk, threshold: 1, speed: 1.5, note: keep me}\n"
	"      - name: strafe\n"
	"        blend_tree:\n"
	"          type: 2d_directional\n"
	"          parameter: dir_x\n"
	"          parameter_y: dir_y\n"
	"          sync: false\n"
	"          children:\n"
	"            - {clip: idle, position: [0, 0]}\n"
	"            - {tree: {type: direct, children: [{clip: walk, parameter: speed}]}, position: [0, 1]}\n"
	"      - name: air\n"
	"        machine:\n"
	"          default: rise\n"
	"          states:\n"
	"            - {name: rise, clip: move, speed: 0.5, speed_parameter: speed, mirror: true}\n"
	"            - {name: fall, clip: move}\n"
	"          transitions:\n"
	"            - {from: rise, to: fall, duration: 0.1, exit_time: 0.9}\n"
	"    any_state:\n"
	"      - to: air\n"
	"        duration: 0.05\n"
	"        can_transition_to_self: true\n"
	"        conditions: [{parameter: jump}]\n"
	"    transitions:\n"
	"      - from: idle\n"
	"        to: locomotion\n"
	"        duration: 0.2\n"
	"        interruption: destination_then_source\n"
	"        priority: 3\n"
	"        conditions:\n"
	"          - {parameter: speed, op: '>', value: 0.1}\n"
	"          - {parameter: grounded}\n"
	"          - {parameter: weapon, op: '!=', value: 5}\n"
	"      - {from: locomotion, to: idle, duration: 0.3, exit_time: 0.5, offset: 0.25, conditions: [{parameter: grounded, op: '==', value: false}]}\n"
	"  - name: upper\n"
	"    weight: 0.5\n"
	"    blend: additive\n"
	"    mask: [spine]\n"
	"    mask_recursive: false\n"
	"    enabled: false\n"
	"    states: [{name: wave, clip: wave}]\n"
	"ik:\n"
	"  - {name: reach, type: two_bone, bones: [r_upper_arm, r_lower_arm], effector: r_hand, pole: [0, 0, 1], weight: 0.75}\n"
	"  - {name: tail, type: fabrik, bones: [spine, head], iterations: 20, tolerance: 0.01, axis: [0, 0, 1]}\n"
	"  - {name: l_foot, type: foot, bones: [l_thigh, l_shin], effector: l_foot, ray_up: 0.4, ray_down: 0.6, align: true}\n"
	"editor_layout: {zoom: 2}\n";

static void
test_format_round_trip (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgAnimGraph) g = NULL;
	g_autoptr(LrgAnimGraph) g2 = NULL;
	g_autofree gchar       *t1 = NULL;
	g_autofree gchar       *t2 = NULL;
	g_autofree gchar       *path = NULL;
	LrgAnimationStateMachine *m;
	LrgAnimationState      *loco;

	g = lrg_anim_graph_new_from_data (GRAPH_TEXT, -1, &error);
	g_assert_no_error (error);
	g_assert_nonnull (g);

	/* what loaded */
	g_assert_cmpstr (lrg_anim_graph_get_name (g), ==, "hero");
	g_assert_cmpuint (lrg_anim_graph_get_parameter_count (g), ==, 6);
	g_assert_cmpint (lrg_anim_graph_get_parameter_type (g, "jump"), ==, LRG_ANIM_PARAMETER_TRIGGER);
	g_assert_cmpint (lrg_anim_graph_get_int (g, "weapon"), ==, 2);
	g_assert_true (lrg_anim_graph_get_bool (g, "grounded"));
	g_assert_cmpuint (lrg_anim_graph_get_layer_count (g), ==, 2);
	g_assert_cmpuint (lrg_anim_graph_get_ik_goal_count (g), ==, 3);
	g_assert_cmpint (lrg_anim_graph_get_root_motion_mode (g), ==, LRG_ROOT_MOTION_XZ);
	m = lrg_anim_graph_get_machine (g);
	g_assert_cmpstr (lrg_animation_state_machine_get_default_state (m), ==, "idle");
	loco = lrg_animation_state_machine_get_state (m, "locomotion");
	g_assert_nonnull (lrg_animation_state_get_blend_tree (loco));
	g_assert_cmpuint (lrg_blend_tree_get_child_count (lrg_animation_state_get_blend_tree (loco)), ==, 2);
	g_assert_nonnull (lrg_animation_state_get_sub_machine (lrg_animation_state_machine_get_state (m, "air")));
	g_assert_cmpint (lrg_animation_layer_get_blend_mode (lrg_anim_graph_get_layer (g, 1)), ==,
	                 LRG_LAYER_BLEND_ADDITIVE);

	/* canonical text: write, read, write again: identical bytes */
	t1 = lrg_anim_graph_to_data (g, &error);
	g_assert_no_error (error);
	g2 = lrg_anim_graph_new_from_data (t1, -1, &error);
	g_assert_no_error (error);
	t2 = lrg_anim_graph_to_data (g2, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (t1, ==, t2);
	if (g_test_verbose ())
		g_printerr ("%s", t1);

	/* unknown keys survive (top level, state, blend child) */
	g_assert_nonnull (strstr (t1, "editor_layout"));
	g_assert_nonnull (strstr (t1, "editor_pos"));
	g_assert_nonnull (strstr (t1, "keep me"));

	/* and through a file */
	path = g_build_filename (g_get_tmp_dir (), "lrg-test-anim-graph.ranim", NULL);
	g_assert_true (lrg_anim_graph_save_to_file (g2, path, &error));
	g_assert_no_error (error);
	{
		g_autoptr(LrgAnimGraph) g3 = lrg_anim_graph_new_from_file (path, &error);
		g_autofree gchar       *t3 = NULL;

		g_assert_no_error (error);
		t3 = lrg_anim_graph_to_data (g3, NULL);
		g_assert_cmpstr (t3, ==, t1);
		g_assert_nonnull (lrg_anim_graph_get_base_dir (g3));
	}
	g_unlink (path);

	/* edits through the runtime objects are saved */
	lrg_animation_state_set_speed (loco, 2.0f);
	{
		g_autofree gchar *t4 = lrg_anim_graph_to_data (g, NULL);

		g_assert_cmpstr (t4, !=, t1);
		g_assert_nonnull (strstr (t4, "speed: 2"));
	}
}

typedef struct
{
	const gchar *text;
	gint         code;
	const gchar *fragment;
} BadCase;

static void
test_format_strict_errors (void)
{
	static const BadCase cases[] = {
		{ "- just a list\n", LRG_ANIM_GRAPH_ERROR_PARSE, NULL },
		{ "version: 99\n", LRG_ANIM_GRAPH_ERROR_VERSION, "version" },
		{ "version: 1\nparameters: [{name: a, type: double}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "parameters[0].type" },
		{ "version: 1\nparameters: [{name: a, type: float}, {name: a, type: int}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "duplicate" },
		{ "version: 1\nparameters: [{name: a, type: float, default: fast}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "not a number" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s, clip: nope}]}]\n", LRG_ANIM_GRAPH_ERROR_REFERENCE, "layers[0].states[0].clip" },
		{ "version: 1\nlayers: [{name: b, default: x, states: [{name: s}]}]\n", LRG_ANIM_GRAPH_ERROR_REFERENCE, "layers[0].default" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s}, {name: s}]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "duplicate state" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s}], transitions: [{from: s, to: t}]}]\n", LRG_ANIM_GRAPH_ERROR_REFERENCE, "transitions[0].to" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s}], transitions: [{from: s, to: s, conditions: [{parameter: sped}]}]}]\n", LRG_ANIM_GRAPH_ERROR_REFERENCE, "sped" },
		{ "version: 1\nparameters: [{name: j, type: trigger}]\nlayers: [{name: b, states: [{name: s}], transitions: [{from: s, to: s, conditions: [{parameter: j, op: '>'}]}]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "conditions[0]" },
		{ "version: 1\nparameters: [{name: f, type: float}]\nlayers: [{name: b, states: [{name: s}], transitions: [{from: s, to: s, conditions: [{parameter: f}]}]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "op and value" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s, blend_tree: {type: 1d}}]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "needs a parameter" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s, blend_tree: {type: 3d, parameter: x}}]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "blend_tree.type" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s}], transitions: [{from: s, to: s, interruption: maybe}]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "interruption" },
		{ "version: 1\nik: [{name: a, type: two_bone, bones: [x]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "exactly two" },
		{ "version: 1\nlayers: [{name: b, weight: 2, states: []}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "weight" },
		{ "version: 1\nlayers: [{name: b, states: [{name: s, mirror: maybe}]}]\n", LRG_ANIM_GRAPH_ERROR_INVALID, "mirror" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError)       error = NULL;
		g_autoptr(LrgAnimGraph) g = lrg_anim_graph_new_from_data (cases[i].text, -1, &error);

		if (g != NULL || !g_error_matches (error, LRG_ANIM_GRAPH_ERROR, cases[i].code))
			g_error ("case %u: expected error %d, got %s", i, cases[i].code,
			         error != NULL ? error->message : "success");
		if (cases[i].fragment != NULL && strstr (error->message, cases[i].fragment) == NULL)
			g_error ("case %u: '%s' does not mention '%s'", i, error->message, cases[i].fragment);
	}

	/* a failed load leaves the graph as it was */
	{
		g_autoptr(LrgAnimGraph) g = lrg_anim_graph_new_from_data (GRAPH_TEXT, -1, NULL);
		g_autoptr(GError)       error = NULL;

		g_assert_false (lrg_anim_graph_load_from_data (g, "version: 77\n", -1, &error));
		g_assert_cmpuint (lrg_anim_graph_get_layer_count (g), ==, 2);
	}
}

/* ==========================================================================
 * State machine
 * ========================================================================== */

static LrgAnimGraph *
graph_with_clips (const gchar *text,
                  LrgSkeleton *sk)
{
	g_autoptr(GError)            error = NULL;
	LrgAnimGraph                *g = lrg_anim_graph_new_from_data (text, -1, &error);
	g_autoptr(LrgAnimationClip)  idle = clip_idle (sk);
	g_autoptr(LrgAnimationClip)  walk = clip_walk (sk, 0.6f);
	g_autoptr(LrgAnimationClip)  wave = clip_wave (sk);
	g_autoptr(LrgAnimationClip)  move = clip_move (sk);

	g_assert_no_error (error);
	lrg_anim_graph_set_clip (g, "idle", idle);
	lrg_anim_graph_set_clip (g, "walk", walk);
	lrg_anim_graph_set_clip (g, "wave", wave);
	lrg_anim_graph_set_clip (g, "move", move);
	return g;
}

static void
test_machine_transitions (void)
{
	g_autoptr(LrgSkeleton)   sk = make_figure ();
	g_autoptr(LrgAnimGraph)  g = graph_with_clips (GRAPH_TEXT, sk);
	g_auto(GStrv)            missing = NULL;
	LrgAnimationStateMachine *m = lrg_anim_graph_get_machine (g);

	missing = lrg_anim_graph_get_missing_clips (g);
	g_assert_cmpuint (g_strv_length (missing), ==, 0);

	lrg_anim_graph_start (g);
	g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (m), ==, "idle");

	/* conditions: speed > 0.1 AND grounded AND weapon != 5 */
	lrg_anim_graph_update (g, 0.1f);
	g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (m), ==, "idle");
	lrg_anim_graph_set_float (g, "speed", 0.5f);
	lrg_anim_graph_set_int (g, "weapon", 5);
	lrg_anim_graph_update (g, 0.1f);
	g_assert_false (lrg_animation_state_machine_is_transitioning (m));
	lrg_anim_graph_set_int (g, "weapon", 1);
	lrg_anim_graph_update (g, 0.1f);
	g_assert_true (lrg_animation_state_machine_is_transitioning (m));
	g_assert_cmpstr (lrg_animation_state_get_name (lrg_animation_state_machine_get_next_state (m)), ==, "locomotion");
	/* duration 0.2: finished after two more 0.1 steps */
	lrg_anim_graph_update (g, 0.1f);
	lrg_anim_graph_update (g, 0.11f);
	g_assert_false (lrg_animation_state_machine_is_transitioning (m));
	g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (m), ==, "locomotion");

	/* trigger via any-state, consumed by the transition */
	lrg_anim_graph_set_trigger (g, "jump");
	g_assert_true (lrg_anim_graph_get_bool (g, "jump"));
	lrg_anim_graph_update (g, 0.01f);
	g_assert_false (lrg_anim_graph_get_bool (g, "jump"));
	lrg_anim_graph_update (g, 0.1f);
	g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (m), ==, "air");

	/* the sub-machine runs and shares the parameters (speed_parameter) */
	{
		LrgAnimationState        *air = lrg_animation_state_machine_get_state (m, "air");
		LrgAnimationStateMachine *sub = lrg_animation_state_get_sub_machine (air);

		g_assert_true (lrg_animation_state_machine_is_running (sub));
		g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (sub), ==, "rise");
		g_assert_cmpfloat_with_epsilon (lrg_animation_state_machine_get_float (sub, "speed"), 0.5f, 1e-6f);
		/* rise: 1 s clip at speed 0.5 x speed param 0.5 = 0.25 -> exit 0.9 after 3.6 s */
		lrg_anim_graph_update (g, 3.0f);
		g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (sub), ==, "rise");
		lrg_anim_graph_update (g, 0.7f);
		lrg_anim_graph_update (g, 0.2f);
		g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (sub), ==, "fall");
	}
}

/* Exit time: A (1 s clip) -> B at exit 0.5 without conditions; once per
 * loop crossing; interruption during A -> B by B's own transition. */
static void
test_machine_exit_time_interrupt (void)
{
	static const gchar *text =
		"version: 1\n"
		"clips: {walk: {}, idle: {}}\n"
		"parameters: [{name: go, type: bool}]\n"
		"layers:\n"
		"  - name: base\n"
		"    states: [{name: a, clip: walk}, {name: b, clip: idle}, {name: c, clip: idle}]\n"
		"    transitions:\n"
		"      - {from: a, to: b, duration: 1, exit_time: 0.5, interruption: destination}\n"
		"      - {from: b, to: c, duration: 0.5, conditions: [{parameter: go}]}\n";
	g_autoptr(LrgSkeleton)    sk = make_figure ();
	g_autoptr(LrgAnimGraph)   g = graph_with_clips (text, sk);
	LrgAnimationStateMachine *m = lrg_anim_graph_get_machine (g);
	LrgBonePose              *poses = g_new (LrgBonePose, 16);

	lrg_anim_graph_start (g);
	lrg_anim_graph_update (g, 0.3f);
	g_assert_false (lrg_animation_state_machine_is_transitioning (m));
	lrg_anim_graph_update (g, 0.3f);      /* crosses 0.5 */
	g_assert_true (lrg_animation_state_machine_is_transitioning (m));
	g_assert_cmpfloat_with_epsilon (lrg_animation_state_machine_get_transition_progress (m), 0.0f, 1e-6f);
	lrg_anim_graph_update (g, 0.4f);
	g_assert_cmpfloat_with_epsilon (lrg_animation_state_machine_get_transition_progress (m), 0.4f, 1e-5f);
	lrg_anim_graph_evaluate (g, sk, poses, 16, NULL, NULL);   /* remembered for the interrupt */

	/* b -> c may interrupt (destination) */
	lrg_anim_graph_set_bool (g, "go", TRUE);
	lrg_anim_graph_update (g, 0.1f);
	g_assert_true (lrg_animation_state_machine_is_transitioning (m));
	g_assert_cmpstr (lrg_animation_state_get_name (lrg_animation_state_machine_get_next_state (m)), ==, "c");
	g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (m), ==, "b");
	lrg_anim_graph_update (g, 0.6f);
	g_assert_cmpstr (lrg_animation_state_machine_get_current_state_name (m), ==, "c");
	g_free (poses);
}

/* ==========================================================================
 * Blend trees
 * ========================================================================== */

static void
test_blend_1d (void)
{
	g_autoptr(LrgBlendTree)     t = lrg_blend_tree_new (LRG_BLEND_TYPE_1D);
	g_autoptr(LrgSkeleton)      sk = make_figure ();
	g_autoptr(LrgAnimationClip) a = clip_idle (sk);
	g_autoptr(LrgAnimationClip) b = clip_walk (sk, 0.6f);

	lrg_blend_tree_add_child (t, a, 0.0f);
	lrg_blend_tree_add_child (t, b, 2.0f);
	lrg_blend_tree_add_child (t, a, 4.0f);

	lrg_blend_tree_set_parameter (t, 0.5f);
	lrg_blend_tree_compute_weights (t);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (t, 0), 0.75f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (t, 1), 0.25f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (t, 2), 0.0f, 1e-5f);
	lrg_blend_tree_set_parameter (t, 3.0f);
	lrg_blend_tree_compute_weights (t);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (t, 1), 0.5f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (t, 2), 0.5f, 1e-5f);
	lrg_blend_tree_set_parameter (t, 9.0f);   /* clamped to the last */
	lrg_blend_tree_compute_weights (t);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (t, 2), 1.0f, 1e-5f);

	/* sampled pose: at weight 0.5 of a still and a swinging clip the
	 * thigh swings half as far */
	{
		LrgBonePose pose[16], full[16];
		gfloat      angle_full;

		lrg_blend_tree_set_parameter (t, 1.0f);
		lrg_blend_tree_update (t, 0.25f);   /* synced: walk at its peak */
		lrg_blend_tree_sample_pose (t, sk, pose, 16);
		lrg_anim_pose_sample_clip (b, sk, 0.25f, full, 16);
		/* compare the swing quaternion's distance from the bind rotation */
		{
			const LrgBonePose *bind = bind_of (sk, "l_thigh");
			gfloat d_half = fabsf (pose[10].rotation_y - bind->rotation_y) + fabsf (pose[10].rotation_w - bind->rotation_w);
			gfloat d_full = fabsf (full[10].rotation_y - bind->rotation_y) + fabsf (full[10].rotation_w - bind->rotation_w);

			angle_full = d_full;
			g_assert_cmpfloat (d_half, >, 0.0f);
			g_assert_cmpfloat (d_half, <, angle_full);
		}
	}
}

static void
test_blend_2d (void)
{
	g_autoptr(LrgBlendTree) cart = lrg_blend_tree_new (LRG_BLEND_TYPE_2D_FREEFORM);
	g_autoptr(LrgBlendTree) dir = lrg_blend_tree_new (LRG_BLEND_TYPE_2D_SIMPLE);
	static const gfloat     pts[5][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	guint                   i;
	gfloat                  sum;

	for (i = 0; i < 5; i++)
	{
		lrg_blend_tree_add_child_2d (cart, NULL, pts[i][0], pts[i][1]);
		lrg_blend_tree_add_child_2d (dir, NULL, pts[i][0], pts[i][1]);
	}

	/* exact at every child's position */
	for (i = 0; i < 5; i++)
	{
		lrg_blend_tree_set_parameter_2d (cart, pts[i][0], pts[i][1]);
		lrg_blend_tree_compute_weights (cart);
		g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (cart, i), 1.0f, 1e-4f);
		lrg_blend_tree_set_parameter_2d (dir, pts[i][0], pts[i][1]);
		lrg_blend_tree_compute_weights (dir);
		g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (dir, i), 1.0f, 1e-4f);
	}

	/* cartesian: half way between centre and forward */
	lrg_blend_tree_set_parameter_2d (cart, 0.0f, 0.5f);
	lrg_blend_tree_compute_weights (cart);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (cart, 0), 0.5f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (cart, 1), 0.5f, 1e-4f);

	/* directional: half speed forward = idle and forward evenly */
	lrg_blend_tree_set_parameter_2d (dir, 0.0f, 0.5f);
	lrg_blend_tree_compute_weights (dir);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (dir, 0), 0.5f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (dir, 1), 0.5f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (dir, 3), 0.0f, 1e-4f);

	/* directional: full speed diagonal = forward and right evenly, no idle */
	lrg_blend_tree_set_parameter_2d (dir, (gfloat) G_SQRT2 / 2.0f, (gfloat) G_SQRT2 / 2.0f);
	lrg_blend_tree_compute_weights (dir);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (dir, 1), 0.5f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (dir, 2), 0.5f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (lrg_blend_tree_get_child_weight (dir, 0), 0.0f, 1e-3f);

	/* the weights always sum to 1 */
	for (sum = 0.0f, i = 0; i < 5; i++)
		sum += lrg_blend_tree_get_child_weight (cart, i);
	g_assert_cmpfloat_with_epsilon (sum, 1.0f, 1e-5f);
}

/* ==========================================================================
 * Layers
 * ========================================================================== */

static void
test_layers (void)
{
	g_autoptr(LrgSkeleton)       sk = make_figure ();
	g_autoptr(LrgAnimationClip)  walk = clip_walk (sk, 0.6f);
	g_autoptr(LrgAnimationClip)  wave = clip_wave (sk);
	g_autoptr(LrgAnimationClip)  move = clip_move (sk);
	g_autoptr(LrgAnimationLayer) upper = lrg_animation_layer_new ("upper");
	g_autoptr(LrgAnimationLayer) add = lrg_animation_layer_new ("add");
	g_autoptr(LrgAnimationState) wst = lrg_animation_state_new ("wave");
	g_autoptr(LrgAnimationState) mst = lrg_animation_state_new ("move");
	LrgBonePose                  base[16], out[16], wpose[16];
	gfloat                       w[16];

	lrg_anim_pose_sample_clip (walk, sk, 0.25f, base, 16);
	memcpy (out, base, sizeof base);

	/* override + recursive mask on the spine: arms and head follow the
	 * layer, legs keep the base */
	lrg_animation_state_set_clip (wst, wave);
	lrg_animation_layer_set_state (upper, wst);
	lrg_animation_layer_add_mask (upper, "spine");
	lrg_animation_layer_get_bone_weights (upper, sk, w, 16);
	g_assert_cmpfloat (w[2], ==, 1.0f);
	g_assert_cmpfloat (w[7], ==, 1.0f);   /* r_upper_arm under spine */
	g_assert_cmpfloat (w[10], ==, 0.0f);  /* l_thigh */
	lrg_animation_layer_blend_pose (upper, sk, out, 16);
	lrg_anim_pose_sample_clip (wave, sk, 0.0f, wpose, 16);
	g_assert_cmpfloat_with_epsilon (out[7].rotation_z, wpose[7].rotation_z, 1e-5f);
	g_assert_cmpfloat_with_epsilon (out[10].rotation_x, base[10].rotation_x, 1e-6f);
	g_assert_cmpfloat_with_epsilon (out[10].rotation_w, base[10].rotation_w, 1e-6f);

	/* not recursive: only the spine */
	lrg_animation_layer_set_mask_recursive (upper, FALSE);
	lrg_animation_layer_get_bone_weights (upper, sk, w, 16);
	g_assert_cmpfloat (w[2], ==, 1.0f);
	g_assert_cmpfloat (w[7], ==, 0.0f);

	/* additive at half weight: the root slides by half the clip's offset
	 * from the bind pose */
	lrg_animation_state_set_clip (mst, move);
	lrg_animation_state_set_time (mst, 0.5f);   /* root x = 1 */
	lrg_animation_layer_set_state (add, mst);
	lrg_animation_layer_set_blend_mode (add, LRG_LAYER_BLEND_ADDITIVE);
	lrg_animation_layer_set_weight (add, 0.5f);
	memcpy (out, base, sizeof base);
	lrg_animation_layer_blend_pose (add, sk, out, 16);
	g_assert_cmpfloat_with_epsilon (out[0].position_x, base[0].position_x + 0.5f, 1e-4f);
	/* bones the additive clip does not animate are unchanged */
	g_assert_cmpfloat_with_epsilon (out[10].rotation_w, base[10].rotation_w, 1e-5f);
}

/* ==========================================================================
 * Root motion
 * ========================================================================== */

static void
test_root_motion (void)
{
	static const gchar *text =
		"version: 1\n"
		"clips: {move: {}}\n"
		"root_motion: {bone: root, mode: xz, rotation: false, apply: true}\n"
		"layers: [{name: base, states: [{name: m, clip: move}]}]\n";
	g_autoptr(LrgSkeleton)   sk = make_figure ();
	g_autoptr(LrgAnimGraph)  g = graph_with_clips (text, sk);
	LrgBonePose              poses[16];
	gfloat                   d[3], yaw;
	gfloat                   t[3];

	/* a clip on its own: 2 units per loop, loops counted */
	lrg_anim_pose_clip_root_motion (lrg_anim_graph_get_clip (g, "move"), "root", 0.25f, 0.75f, t, NULL);
	g_assert_cmpfloat_with_epsilon (t[0], 1.0f, 1e-3f);
	lrg_anim_pose_clip_root_motion (lrg_anim_graph_get_clip (g, "move"), "root", 0.5f, 2.5f, t, NULL);
	g_assert_cmpfloat_with_epsilon (t[0], 4.0f, 1e-3f);

	lrg_anim_graph_update (g, 0.25f);
	lrg_anim_graph_evaluate (g, sk, poses, 16, d, &yaw);
	g_assert_cmpfloat_with_epsilon (d[0], 0.5f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (d[2], 0.0f, 1e-5f);
	/* the pose keeps the root at its bind X */
	g_assert_cmpfloat_with_epsilon (poses[0].position_x, 0.0f, 1e-5f);

	/* across the loop seam: 0.25 -> 1.25 is one full stride */
	lrg_anim_graph_update (g, 1.0f);
	lrg_anim_graph_evaluate (g, sk, poses, 16, d, &yaw);
	g_assert_cmpfloat_with_epsilon (d[0], 2.0f, 1e-3f);
}

/* ==========================================================================
 * IK
 * ========================================================================== */

static void
test_ik_two_bone (void)
{
	g_autoptr(LrgSkeleton) sk = make_figure ();
	g_autoptr(LrgIKGoal)   goal = lrg_ik_goal_new ("reach", LRG_IK_GOAL_TWO_BONE);
	const gchar           *bones[] = { "r_upper_arm", "r_lower_arm", NULL };
	gfloat                 target[3] = { 0.9f, 1.9f, 0.4f };
	gfloat                 hand[3], shoulder[3], elbow[3];
	gfloat                 before[3];

	world_pos (sk, "r_hand", before);
	lrg_ik_goal_set_bones (goal, bones);
	lrg_ik_goal_set_effector (goal, "r_hand");
	lrg_ik_goal_set_pole (goal, 0.6f, 1.6f, -1.0f);
	lrg_ik_goal_set_target (goal, target[0], target[1], target[2]);
	g_assert_true (lrg_ik_goal_apply (goal, sk, NULL, NULL, NULL));

	/* exact, with the bone lengths kept */
	world_pos (sk, "r_hand", hand);
	world_pos (sk, "r_upper_arm", shoulder);
	world_pos (sk, "r_lower_arm", elbow);
	g_assert_cmpfloat (dist3 (hand, target), <, 1e-3f);
	g_assert_cmpfloat_with_epsilon (dist3 (shoulder, elbow), 0.5f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (dist3 (elbow, hand), 0.5f, 1e-4f);
	/* the elbow bends toward the pole (behind, -Z) */
	g_assert_cmpfloat (elbow[2], <, (shoulder[2] + target[2]) * 0.5f);

	/* out of reach: stretched toward it */
	lrg_ik_goal_set_target (goal, 5.0f, 1.6f, 0.0f);
	g_assert_true (lrg_ik_goal_apply (goal, sk, NULL, NULL, NULL));
	world_pos (sk, "r_hand", hand);
	g_assert_cmpfloat_with_epsilon (dist3 (shoulder, hand), 1.0f, 2e-3f);
	g_assert_cmpfloat (hand[0], >, shoulder[0] + 0.99f);

	/* weight 0 leaves the animated pose */
	lrg_skeleton_reset_to_bind (sk);
	lrg_skeleton_calculate_world_poses (sk);
	lrg_ik_goal_set_weight (goal, 0.0f);
	g_assert_false (lrg_ik_goal_apply (goal, sk, NULL, NULL, NULL));
	world_pos (sk, "r_hand", hand);
	g_assert_cmpfloat (dist3 (hand, before), <, 1e-5f);

	/* a model matrix moves the world target into model space */
	{
		gfloat m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 0, 0, 1 };

		lrg_ik_goal_set_weight (goal, 1.0f);
		lrg_ik_goal_set_target (goal, 10.0f + 0.9f, 1.9f, 0.4f);
		g_assert_true (lrg_ik_goal_apply (goal, sk, m, NULL, NULL));
		world_pos (sk, "r_hand", hand);
		g_assert_cmpfloat (dist3 (hand, target), <, 1e-3f);
	}
}

static void
test_ik_iterative (void)
{
	LrgSkeleton             *sk = lrg_skeleton_new ();
	g_autoptr(LrgIKChain)    chain = NULL;
	g_autoptr(LrgIKSolverFABRIK) fabrik = lrg_ik_solver_fabrik_new ();
	g_autoptr(LrgIKSolverCCD)    ccd = lrg_ik_solver_ccd_new ();
	g_autoptr(LrgIKSolverLookAt) look = lrg_ik_solver_look_at_new ();
	gfloat                   target[3] = { 1.5f, 2.0f, 0.5f };
	gfloat                   tip[3];
	gint                     i;

	/* a 4-bone tentacle up +Y, segments of 0.75 */
	for (i = 0; i < 5; i++)
		add_bone (sk, (const gchar *[]) { "t0", "t1", "t2", "t3", "tip" }[i], i, i - 1,
		          0.0f, i == 0 ? 0.0f : 0.75f, 0.0f, 0.0f, 0.75f);
	lrg_skeleton_calculate_world_poses (sk);

	chain = lrg_ik_chain_new (sk);
	lrg_ik_chain_add_bone (chain, "t0");
	lrg_ik_chain_add_bone (chain, "t1");
	lrg_ik_chain_add_bone (chain, "t2");
	lrg_ik_chain_add_bone (chain, "t3");
	lrg_ik_chain_set_effector_bone (chain, "tip");
	g_assert_cmpfloat_with_epsilon (lrg_ik_chain_get_total_length (chain), 3.0f, 1e-4f);
	lrg_ik_chain_set_target_position (chain, target[0], target[1], target[2]);

	g_assert_true (lrg_ik_solver_solve (LRG_IK_SOLVER (fabrik), chain, 30, 1e-3f));
	lrg_ik_chain_get_end_effector_position (chain, &tip[0], &tip[1], &tip[2]);
	g_assert_cmpfloat (dist3 (tip, target), <, 1e-2f);
	g_assert_cmpfloat_with_epsilon (lrg_ik_chain_get_total_length (chain), 3.0f, 1e-3f);

	/* CCD from the bind pose */
	lrg_skeleton_reset_to_bind (sk);
	lrg_skeleton_calculate_world_poses (sk);
	lrg_ik_chain_set_target_position (chain, -1.0f, 1.5f, 1.0f);
	g_assert_true (lrg_ik_solver_solve (LRG_IK_SOLVER (ccd), chain, 50, 1e-3f));
	lrg_ik_chain_get_end_effector_position (chain, &tip[0], &tip[1], &tip[2]);
	target[0] = -1.0f;
	target[1] = 1.5f;
	target[2] = 1.0f;
	g_assert_cmpfloat (dist3 (tip, target), <, 1e-2f);

	/* look-at: one bone, its +Y axis toward the target */
	{
		g_autoptr(LrgIKChain) one = lrg_ik_chain_new (sk);
		gfloat                root[3], dir[3], want[3];
		gfloat                l;

		lrg_skeleton_reset_to_bind (sk);
		lrg_skeleton_calculate_world_poses (sk);
		lrg_ik_chain_add_bone (one, "t3");
		lrg_ik_chain_set_target_position (one, 3.0f, 2.25f, 0.0f);
		g_assert_true (lrg_ik_solver_solve (LRG_IK_SOLVER (look), one, 1, 0.0f));
		world_pos (sk, "t3", root);
		lrg_ik_chain_get_end_effector_position (one, &tip[0], &tip[1], &tip[2]);
		dir[0] = tip[0] - root[0];
		dir[1] = tip[1] - root[1];
		dir[2] = tip[2] - root[2];
		want[0] = 3.0f - root[0];
		want[1] = 2.25f - root[1];
		want[2] = 0.0f - root[2];
		l = sqrtf (dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]) *
		    sqrtf (want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);
		g_assert_cmpfloat ((dir[0] * want[0] + dir[1] * want[1] + dir[2] * want[2]) / l, >, 0.9999f);
	}
	g_object_unref (sk);
}

/* A flat floor at y = 0.3 for foot placement. */
static gboolean
floor_ray (const gfloat *origin,
           const gfloat *direction,
           gfloat        max_distance,
           gfloat       *out_point,
           gfloat       *out_normal,
           gpointer      user_data)
{
	gfloat h = *(gfloat *) user_data;
	gfloat t;

	if (direction[1] >= 0.0f)
		return FALSE;
	t = (h - origin[1]) / direction[1];
	if (t < 0.0f || t > max_distance)
		return FALSE;
	out_point[0] = origin[0] + direction[0] * t;
	out_point[1] = h;
	out_point[2] = origin[2] + direction[2] * t;
	out_normal[0] = 0.0f;
	out_normal[1] = 1.0f;
	out_normal[2] = 0.0f;
	return TRUE;
}

static void
test_ik_foot (void)
{
	g_autoptr(LrgSkeleton) sk = make_figure ();
	g_autoptr(LrgIKGoal)   foot = lrg_ik_goal_new ("l_foot", LRG_IK_GOAL_FOOT);
	const gchar           *bones[] = { "l_thigh", "l_shin", NULL };
	gfloat                 floor_h = 0.3f, none = -50.0f;
	gfloat                 before[3], after[3];

	lrg_ik_goal_set_bones (foot, bones);
	lrg_ik_goal_set_effector (foot, "l_foot");
	lrg_ik_goal_set_pole (foot, -0.15f, 0.5f, 1.0f);
	lrg_ik_goal_set_foot (foot, 0.5f, 0.5f, TRUE);
	world_pos (sk, "l_foot", before);

	/* no ground in range: nothing changes */
	g_assert_false (lrg_ik_goal_apply (foot, sk, NULL, floor_ray, &none));
	/* ground 0.3 up: the foot rises by 0.3 */
	g_assert_true (lrg_ik_goal_apply (foot, sk, NULL, floor_ray, &floor_h));
	world_pos (sk, "l_foot", after);
	g_assert_cmpfloat_with_epsilon (after[1], before[1] + 0.3f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (after[0], before[0], 1e-3f);
}

/* ==========================================================================
 * Animator component
 * ========================================================================== */

/* A rigid stick-figure mesh: one box per bone along its +Y, weighted 100 %
 * to that bone, in bind (model) space. */
static LrgSkinnedMesh *
make_figure_mesh (LrgSkeleton *sk)
{
	guint                n = lrg_skeleton_get_bone_count (sk);
	g_autoptr(GArray)    pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(GArray)    nrm = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(GArray)    bi = g_array_new (FALSE, FALSE, sizeof (guint8));
	g_autoptr(GArray)    bw = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(GArray)    idx = g_array_new (FALSE, FALSE, sizeof (guint32));
	g_autoptr(GError)    error = NULL;
	LrgSkinnedMesh      *mesh = lrg_skinned_mesh_new (n);
	static const gint    faces[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 },
	                                     { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
	guint                b;

	lrg_skeleton_reset_to_bind (sk);
	lrg_skeleton_calculate_world_poses (sk);
	for (b = 1; b < n; b++)
	{
		LrgBone *bone = lrg_skeleton_get_bone (sk, (gint) b);
		gfloat   len = lrg_bone_get_length (bone);
		gfloat   r = (b == 3) ? 0.15f : (len < 0.2f ? 0.08f : 0.06f);
		gfloat   m[16];
		gfloat   corner[8][3];
		guint    c, f, k;

		if (len < 0.2f)
			len = 0.16f;
		lrg_bone_pose_to_matrix (lrg_bone_get_world_pose (bone), m);
		for (c = 0; c < 8; c++)
		{
			gfloat lx = (c & 4) ? r : -r, ly = (c & 2) ? len : 0.0f, lz = (c & 1) ? r : -r;

			if (b == 3)
				ly = (c & 2) ? 2.0f * r : 0.0f;
			corner[c][0] = m[0] * lx + m[4] * ly + m[8] * lz + m[12];
			corner[c][1] = m[1] * lx + m[5] * ly + m[9] * lz + m[13];
			corner[c][2] = m[2] * lx + m[6] * ly + m[10] * lz + m[14];
		}
		for (f = 0; f < 6; f++)
		{
			guint32 base = pos->len / 3;
			gfloat  e1[3], e2[3], nn[3], l;

			for (k = 0; k < 3; k++)
			{
				e1[k] = corner[faces[f][1]][k] - corner[faces[f][0]][k];
				e2[k] = corner[faces[f][3]][k] - corner[faces[f][0]][k];
			}
			nn[0] = e1[1] * e2[2] - e1[2] * e2[1];
			nn[1] = e1[2] * e2[0] - e1[0] * e2[2];
			nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
			l = sqrtf (nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
			for (c = 0; c < 4; c++)
			{
				guint8 ids[4] = { (guint8) b, 0, 0, 0 };
				gfloat ws[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

				g_array_append_vals (pos, corner[faces[f][c]], 3);
				for (k = 0; k < 3; k++)
				{
					gfloat v = l > 0.0f ? nn[k] / l : 0.0f;

					g_array_append_val (nrm, v);
				}
				g_array_append_vals (bi, ids, 4);
				g_array_append_vals (bw, ws, 4);
			}
			{
				guint32 tri[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };

				g_array_append_vals (idx, tri, 6);
			}
		}
	}
	g_assert_true (lrg_skinned_mesh_set_geometry (mesh, pos->len / 3, (const gfloat *) (gpointer) pos->data,
	                                              (const gfloat *) (gpointer) nrm->data, NULL, NULL,
	                                              (const guint8 *) bi->data, (const gfloat *) (gpointer) bw->data,
	                                              idx->len, (const guint32 *) (gpointer) idx->data, &error));
	g_assert_no_error (error);
	lrg_skeleton_compute_inverse_bind_matrices (sk);
	lrg_skinned_mesh_set_inverse_binds_from_skeleton (mesh, sk);
	return mesh;
}

static const gchar *CHARACTER_TEXT =
	"version: 1\n"
	"clips: {idle: {}, walk: {}, wave: {}, move: {}}\n"
	"parameters: [{name: speed, type: float}, {name: wave, type: bool}]\n"
	"root_motion: {bone: root, mode: xz, rotation: false, apply: true}\n"
	"layers:\n"
	"  - name: base\n"
	"    states:\n"
	"      - name: loco\n"
	"        blend_tree: {type: 1d, parameter: speed, children: [{clip: idle, threshold: 0}, {clip: walk, threshold: 1}]}\n"
	"      - {name: travel, clip: move}\n"
	"  - name: upper\n"
	"    mask: [spine]\n"
	"    states: [{name: rest, clip: idle}, {name: wave, clip: wave}]\n"
	"    transitions:\n"
	"      - {from: rest, to: wave, duration: 0.5, conditions: [{parameter: wave}]}\n"
	"ik:\n"
	"  - {name: reach, type: two_bone, bones: [l_upper_arm, l_lower_arm], effector: l_hand, pole: [-1, 1.5, -1], enabled: false}\n";

static void
on_footstep (LrgAnimatorComponent3D *anim,
             const gchar            *layer,
             const gchar            *state,
             LrgAnimationEvent      *event,
             gpointer                user_data)
{
	g_assert_cmpstr (event->name, ==, "footstep");
	(*(guint *) user_data)++;
}

/* Builds the character entity: skinned model (skeleton + mesh) and the
 * animator. */
static LrgEntity3D *
add_character (LrgWorld3D              *world,
               LrgAnimatorComponent3D **out_anim,
               LrgSkinnedModelComponent **out_smc)
{
	g_autoptr(LrgSkeleton)    sk = make_figure ();
	g_autoptr(LrgSkinnedMesh) mesh = make_figure_mesh (sk);
	g_autoptr(LrgEntity3D)    e = lrg_entity3d_new_with_name ("hero");
	g_autoptr(LrgAnimGraph)   g = graph_with_clips (CHARACTER_TEXT, sk);
	LrgSkinnedModelComponent *smc = lrg_skinned_model_component_new ();
	LrgAnimatorComponent3D   *anim = lrg_animator_component3d_new_with_graph (g);

	/* a footstep at mid-cycle of the walk */
	{
		LrgAnimationEvent *ev = lrg_animation_event_new (0.5f, "footstep");

		lrg_animation_clip_add_event (lrg_anim_graph_get_clip (g, "walk"), ev);
		lrg_animation_event_free (ev);
	}
	lrg_skinned_model_component_set_skeleton (smc, sk);
	lrg_skinned_model_component_set_skinned_mesh (smc, mesh);
	g_assert_true (lrg_entity3d_add_component (e, LRG_COMPONENT (smc)));
	g_assert_true (lrg_entity3d_add_component (e, LRG_COMPONENT (anim)));
	g_object_unref (smc);
	g_object_unref (anim);
	lrg_world3d_add_entity (world, e);
	*out_anim = anim;
	*out_smc = smc;
	return e;
}

static void
test_animator_component (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	LrgAnimatorComponent3D   *anim;
	LrgSkinnedModelComponent *smc;
	LrgEntity3D              *e = add_character (world, &anim, &smc);
	guint                     steps = 0, serial, nb;
	const gfloat             *pal;
	gfloat                    pos[3];
	gint                      i;

	g_signal_connect (anim, "event::footstep", G_CALLBACK (on_footstep), &steps);
	g_assert_true (lrg_animator_component3d_get_skinned_model (anim) == smc);

	/* idle: a palette arrives, identity-like (bind pose) */
	lrg_world3d_update (world, 0.016f);
	serial = lrg_skinned_model_component_get_palette_serial (smc);
	g_assert_cmpuint (serial, >, 0);
	pal = lrg_skinned_model_component_get_palette (smc, &nb);
	g_assert_nonnull (pal);
	g_assert_cmpuint (nb, ==, 16);
	g_assert_cmpfloat_with_epsilon (pal[16 * 10 + 5], 1.0f, 1e-4f);   /* l_thigh unrotated */

	/* walking: the thigh's palette entry rotates; footsteps fire */
	lrg_animator_component3d_set_float (anim, "speed", 1.0f);
	for (i = 0; i < 60; i++)
		lrg_world3d_update (world, 1.0f / 30.0f);   /* two seconds: two footsteps */
	g_assert_cmpuint (lrg_skinned_model_component_get_palette_serial (smc), >, serial);
	g_assert_cmpuint (steps, ==, 2);
	g_assert_cmpstr (lrg_animator_component3d_get_current_state (anim, NULL), ==, "loco");

	/* upper layer transition by parameter */
	lrg_animator_component3d_set_bool (anim, "wave", TRUE);
	for (i = 0; i < 30; i++)
		lrg_world3d_update (world, 1.0f / 30.0f);
	g_assert_cmpstr (lrg_animator_component3d_get_current_state (anim, "upper"), ==, "wave");

	/* root motion moves the entity: 2 units per second along X */
	lrg_entity3d_get_position (e, pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 0.0f, 1e-5f);
	lrg_animation_state_machine_force_state (lrg_anim_graph_get_machine (lrg_animator_component3d_get_graph (anim)),
	                                         "travel");
	for (i = 0; i < 15; i++)
		lrg_world3d_update (world, 1.0f / 30.0f);
	lrg_entity3d_get_position (e, pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 1.0f, 2e-3f);

	/* IK through the component: hand to a world target */
	{
		gfloat    target[3] = { -0.7f, 2.0f, 0.4f };
		gfloat    hand[3], ep[3];
		LrgIKGoal *goal = lrg_anim_graph_get_ik_goal_by_name (lrg_animator_component3d_get_graph (anim), "reach");

		lrg_ik_goal_set_enabled (goal, TRUE);
		lrg_animator_component3d_set_apply_root_motion (anim, FALSE);
		lrg_entity3d_get_position (e, ep);
		g_assert_true (lrg_animator_component3d_set_ik_target (anim, "reach", ep[0] + target[0], target[1], target[2]));
		lrg_world3d_update (world, 1.0f / 30.0f);
		world_pos (lrg_animator_component3d_get_skeleton (anim), "l_hand", hand);
		g_assert_cmpfloat (dist3 (hand, target), <, 2e-3f);
		g_assert_false (lrg_animator_component3d_set_ik_target (anim, "nope", 0, 0, 0));
	}
}


/* Timing for the docs (run with -m perf): a 16-bone character with a
 * blend tree base layer, a masked upper layer and one two-bone IK goal. */
static void
test_perf (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	LrgAnimatorComponent3D   *anim;
	LrgSkinnedModelComponent *smc;
	gint64                    t0, t1;
	gint                      i, n = 20000;

	if (!g_test_perf ())
	{
		g_test_skip ("timing only with -m perf");
		return;
	}
	add_character (world, &anim, &smc);
	lrg_animator_component3d_set_float (anim, "speed", 0.5f);
	lrg_animator_component3d_set_bool (anim, "wave", TRUE);
	lrg_animator_component3d_set_apply_root_motion (anim, FALSE);
	lrg_animator_component3d_evaluate (anim, 0.6f);
	t0 = g_get_monotonic_time ();
	for (i = 0; i < n; i++)
		lrg_animator_component3d_evaluate (anim, 1.0f / 60.0f);
	t1 = g_get_monotonic_time ();
	g_test_minimized_result ((gdouble) (t1 - t0) / n, "animator step: %.2f us (16 bones, 2 layers, no IK)",
	                         (gdouble) (t1 - t0) / n);
	lrg_ik_goal_set_enabled (lrg_anim_graph_get_ik_goal_by_name (lrg_animator_component3d_get_graph (anim), "reach"), TRUE);
	lrg_animator_component3d_set_ik_target (anim, "reach", -0.7f, 2.0f, 0.4f);
	t0 = g_get_monotonic_time ();
	for (i = 0; i < n; i++)
		lrg_animator_component3d_evaluate (anim, 1.0f / 60.0f);
	t1 = g_get_monotonic_time ();
	g_test_minimized_result ((gdouble) (t1 - t0) / n, "animator step with two-bone IK: %.2f us",
	                         (gdouble) (t1 - t0) / n);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

/* Renders the world from the front into a w x h image. */
static Image
render (LrgWorld3D         *world,
        LrgWorld3DRenderer *renderer,
        gint                w,
        gint                h)
{
	LrgWorld3DView  view;
	RenderTexture2D rt = LoadRenderTexture (w, h);
	Image           img;

	lrg_world3d_view_init (&view, (gfloat) w, (gfloat) h);
	view.position[0] = 0.0f;
	view.position[1] = 1.3f;
	view.position[2] = 4.2f;
	view.target[0] = 0.0f;
	view.target[1] = 1.1f;
	view.target[2] = 0.0f;
	BeginTextureMode (rt);
	ClearBackground ((Color) { 30, 30, 46, 255 });
	g_assert_true (lrg_world3d_renderer_render_view (renderer, world, &view));
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	ImageFlipVertical (&img);
	UnloadRenderTexture (rt);
	return img;
}

static void
save_shot (Image       *img,
           const gchar *name)
{
	const gchar      *dir = g_getenv ("LRG_ANIMGRAPH_SHOTS");
	g_autofree gchar *path = NULL;

	if (dir == NULL)
		return;
	path = g_build_filename (dir, name, NULL);
	ExportImage (*img, path);
}

static guint
count_diff (Image *a,
            Image *b)
{
	guint x, y, n = 0;

	for (y = 0; y < (guint) a->height; y++)
		for (x = 0; x < (guint) a->width; x++)
		{
			Color ca = GetImageColor (*a, (gint) x, (gint) y);
			Color cb = GetImageColor (*b, (gint) x, (gint) y);

			if (abs (ca.r - cb.r) + abs (ca.g - cb.g) + abs (ca.b - cb.b) > 30)
				n++;
		}
	return n;
}

static void
test_gpu_skinned_character (void)
{
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgWorld3DRenderer) renderer = NULL;
	LrgAnimatorComponent3D       *anim;
	LrgSkinnedModelComponent     *smc;
	LrgEntity3D                  *target;
	LrgMeshRendererComponent     *mr;
	Image                         bind_img, blend_img, ik_img;
	Color                         body;
	gint                          i;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}

	world = lrg_world3d_new ();
	add_character (world, &anim, &smc);
	lrg_animator_component3d_set_apply_root_motion (anim, FALSE);
	g_object_set (smc, "color-r", 0.98f, "color-g", 0.70f, "color-b", 0.53f, NULL);
	renderer = lrg_world3d_renderer_new ();

	/* bind pose */
	lrg_world3d_update (world, 0.0f);
	bind_img = render (world, renderer, 360, 360);
	body = GetImageColor (bind_img, 180, 150);
	g_assert_false (body.r == 30 && body.g == 30 && body.b == 46);   /* the figure is drawn */
	save_shot (&bind_img, "m34-animgraph-bind.png");

	/* mid-blend: walk at half weight, upper body half way into the wave */
	lrg_animator_component3d_set_float (anim, "speed", 0.5f);
	lrg_animator_component3d_set_bool (anim, "wave", TRUE);
	for (i = 0; i < 15; i++)
		lrg_world3d_update (world, 1.0f / 60.0f);
	blend_img = render (world, renderer, 360, 360);
	save_shot (&blend_img, "m34-animgraph-blend.png");
	g_assert_cmpuint (count_diff (&bind_img, &blend_img), >, 200);

	/* IK: the left hand reaches a red marker */
	target = lrg_entity3d_new_with_name ("target");
	lrg_entity3d_set_position_xyz (target, -0.75f, 1.95f, 0.2f);
	mr = lrg_mesh_renderer_component_new ();
	lrg_mesh_renderer_component_set_size (mr, 0.24f, 0.24f, 0.05f);
	lrg_mesh_renderer_component_set_color (mr, 0.95f, 0.2f, 0.25f, 1.0f);
	g_assert_true (lrg_entity3d_add_component (target, LRG_COMPONENT (mr)));
	g_object_unref (mr);
	lrg_world3d_add_entity (world, target);
	g_object_unref (target);
	lrg_ik_goal_set_enabled (lrg_anim_graph_get_ik_goal_by_name (lrg_animator_component3d_get_graph (anim), "reach"),
	                         TRUE);
	lrg_animator_component3d_set_ik_target (anim, "reach", -0.75f, 1.95f, 0.35f);
	lrg_animator_component3d_set_playing (anim, FALSE);
	lrg_world3d_update (world, 1.0f / 60.0f);
	ik_img = render (world, renderer, 360, 360);
	save_shot (&ik_img, "m34-animgraph-ik.png");
	g_assert_cmpuint (count_diff (&blend_img, &ik_img), >, 100);

	UnloadImage (bind_img);
	UnloadImage (blend_img);
	UnloadImage (ik_img);
	lrg_world3d_renderer_release (renderer);
}

/* ==========================================================================
 * main
 * ========================================================================== */

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-anim-graph-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

int
main (int   argc,
      char *argv[])
{
	int result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/anim-graph/format/round-trip", test_format_round_trip);
	g_test_add_func ("/anim-graph/format/strict-errors", test_format_strict_errors);
	g_test_add_func ("/anim-graph/machine/transitions", test_machine_transitions);
	g_test_add_func ("/anim-graph/machine/exit-time-interrupt", test_machine_exit_time_interrupt);
	g_test_add_func ("/anim-graph/blend-tree/1d", test_blend_1d);
	g_test_add_func ("/anim-graph/blend-tree/2d", test_blend_2d);
	g_test_add_func ("/anim-graph/layers/mask-additive", test_layers);
	g_test_add_func ("/anim-graph/root-motion", test_root_motion);
	g_test_add_func ("/anim-graph/ik/two-bone", test_ik_two_bone);
	g_test_add_func ("/anim-graph/ik/iterative", test_ik_iterative);
	g_test_add_func ("/anim-graph/ik/foot", test_ik_foot);
	g_test_add_func ("/anim-graph/animator/world", test_animator_component);
	g_test_add_func ("/anim-graph/gpu/skinned-character", test_gpu_skinned_character);
	g_test_add_func ("/anim-graph/perf", test_perf);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
