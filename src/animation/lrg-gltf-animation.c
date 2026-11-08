/* lrg-gltf-animation.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * glTF animation import. The file is parsed and validated by cgltf (the
 * copy compiled into raylib, reached through its public functions; only
 * the declarations are included here), then converted:
 *
 *   skin      -> LrgSkeleton    bone i = joint i, parent = nearest joint
 *                                ancestor, bind = node rest TRS, inverse
 *                                bind matrices from the skin
 *   animation -> LrgAnimationClip one track per animated node, glTF
 *                                channels kept as clip channels (keys,
 *                                interpolation untouched), weights as
 *                                morph weight channels
 *   mesh      -> LrgSkinnedMesh + LrgMorphTargets
 *
 * Root joints often hang under a non-joint node (Blender's "Armature",
 * with a rotation and a 0.01 or 100 scale). glTF skinning works in scene
 * space, so the world transform of those ancestors is folded into the
 * root joint: into its bind pose, and into the keys of its channels.
 * With a uniform ancestor scale that folding is exact per component
 * (translation' = T + R (s t), rotation' = R q, scale' = s v; tangents
 * take the linear part only), so the channels stay independent and keep
 * their interpolation. A non-uniform ancestor scale is approximated by
 * its mean and reported with a warning.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>

#include "external/cgltf.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "../lrg-enums.h"
#include "lrg-gltf-animation.h"
#include "lrg-bone.h"
#include "lrg-bone-pose.h"

#define GLTF_MAX_MESH_VERTICES (65535u)
#define GLTF_MAX_JOINTS        (255u)

struct _LrgGltfAnimation
{
	GObject      parent_instance;

	cgltf_data  *data;
	GBytes      *bytes;        /* in-memory source (cgltf points into it) */

	gchar      **node_names;   /* unique per node */
	gchar      **skin_names;
	gchar      **clip_names;
	LrgBonePose *ancestor;     /* per node: world pose folded into it */
	gboolean    *folded;       /* per node: ancestor is not the identity */
};

G_DEFINE_FINAL_TYPE (LrgGltfAnimation, lrg_gltf_animation, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------ */
/* GObject                                                                  */
/* ------------------------------------------------------------------------ */

static void
lrg_gltf_animation_finalize (GObject *object)
{
	LrgGltfAnimation *self = LRG_GLTF_ANIMATION (object);

	if (self->data != NULL)
		cgltf_free (self->data);
	g_clear_pointer (&self->bytes, g_bytes_unref);
	g_strfreev (self->node_names);
	g_strfreev (self->skin_names);
	g_strfreev (self->clip_names);
	g_free (self->ancestor);
	g_free (self->folded);

	G_OBJECT_CLASS (lrg_gltf_animation_parent_class)->finalize (object);
}

static void
lrg_gltf_animation_class_init (LrgGltfAnimationClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_gltf_animation_finalize;
}

static void
lrg_gltf_animation_init (LrgGltfAnimation *self)
{
}

/* ------------------------------------------------------------------------ */
/* Small math                                                               */
/* ------------------------------------------------------------------------ */

/* q = a * b (Hamilton product, x y z w) */
static void
quat_mul (const gfloat *a,
          const gfloat *b,
          gfloat       *q)
{
	gfloat out[4];

	out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	memcpy (q, out, sizeof out);
}

/* v' = q v q^-1 for a unit quaternion */
static void
quat_rotate_vec (const gfloat *q,
                 const gfloat *v,
                 gfloat       *out)
{
	gfloat tx, ty, tz;
	gfloat r[3];

	/* t = 2 cross(q.xyz, v); v' = v + w t + cross(q.xyz, t) */
	tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	r[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	r[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	r[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
	memcpy (out, r, sizeof r);
}

/*
 * matrix_to_pose:
 * Decomposes an affine column-major matrix into translation, rotation
 * and scale (column lengths; a negative determinant flips X). Returns
 * FALSE when the scale is not uniform (the pose is then approximate).
 */
static gboolean
matrix_to_pose (const gfloat *m,
                LrgBonePose  *pose)
{
	gfloat sx, sy, sz;
	gfloat r[9];
	gfloat trace;
	gfloat q[4];
	gfloat det;

	sx = sqrtf (m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	sy = sqrtf (m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
	sz = sqrtf (m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
	det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) +
	      m[8] * (m[1] * m[6] - m[2] * m[5]);
	if (det < 0.0f)
		sx = -sx;
	lrg_bone_pose_set_identity (pose);
	pose->position_x = m[12];
	pose->position_y = m[13];
	pose->position_z = m[14];
	pose->scale_x = sx;
	pose->scale_y = sy;
	pose->scale_z = sz;
	if (fabsf (sx) < 1e-12f || sy < 1e-12f || sz < 1e-12f)
		return FALSE;

	/* rotation matrix r[col*3+row] */
	r[0] = m[0] / sx; r[1] = m[1] / sx; r[2] = m[2] / sx;
	r[3] = m[4] / sy; r[4] = m[5] / sy; r[5] = m[6] / sy;
	r[6] = m[8] / sz; r[7] = m[9] / sz; r[8] = m[10] / sz;

	/* Shepperd's method: the largest diagonal term picks the branch */
	trace = r[0] + r[4] + r[8];
	if (trace > 0.0f)
	{
		gfloat s = sqrtf (trace + 1.0f) * 2.0f;

		q[3] = 0.25f * s;
		q[0] = (r[5] - r[7]) / s;
		q[1] = (r[6] - r[2]) / s;
		q[2] = (r[1] - r[3]) / s;
	}
	else if (r[0] > r[4] && r[0] > r[8])
	{
		gfloat s = sqrtf (1.0f + r[0] - r[4] - r[8]) * 2.0f;

		q[3] = (r[5] - r[7]) / s;
		q[0] = 0.25f * s;
		q[1] = (r[3] + r[1]) / s;
		q[2] = (r[6] + r[2]) / s;
	}
	else if (r[4] > r[8])
	{
		gfloat s = sqrtf (1.0f + r[4] - r[0] - r[8]) * 2.0f;

		q[3] = (r[6] - r[2]) / s;
		q[0] = (r[3] + r[1]) / s;
		q[1] = 0.25f * s;
		q[2] = (r[7] + r[5]) / s;
	}
	else
	{
		gfloat s = sqrtf (1.0f + r[8] - r[0] - r[4]) * 2.0f;

		q[3] = (r[1] - r[3]) / s;
		q[0] = (r[6] + r[2]) / s;
		q[1] = (r[7] + r[5]) / s;
		q[2] = 0.25f * s;
	}
	lrg_bone_pose_set_rotation (pose, q[0], q[1], q[2], q[3]);
	lrg_bone_pose_normalize_rotation (pose);

	return fabsf (fabsf (sx) - sy) <= 1e-4f * sy && fabsf (sy - sz) <= 1e-4f * sy;
}

/* the node's rest TRS as glTF defines it (matrix decomposed when given) */
static void
node_rest_pose (const cgltf_node *node,
                LrgBonePose      *pose)
{
	if (node->has_matrix)
	{
		matrix_to_pose (node->matrix, pose);
		return;
	}
	lrg_bone_pose_set_identity (pose);
	if (node->has_translation)
		lrg_bone_pose_set_position (pose, node->translation[0], node->translation[1],
		                            node->translation[2]);
	if (node->has_rotation)
		lrg_bone_pose_set_rotation (pose, node->rotation[0], node->rotation[1],
		                            node->rotation[2], node->rotation[3]);
	if (node->has_scale)
		lrg_bone_pose_set_scale (pose, node->scale[0], node->scale[1], node->scale[2]);
}

/* the uniform scale used when folding (mean of the axes) */
static gfloat
pose_uniform_scale (const LrgBonePose *p)
{
	return (p->scale_x + p->scale_y + p->scale_z) / 3.0f;
}

/* fold the ancestor pose into a translation value (or a tangent) */
static void
fold_translation (const LrgBonePose *a,
                  const gfloat      *v,
                  gboolean           tangent,
                  gfloat            *out)
{
	gfloat q[4] = { a->rotation_x, a->rotation_y, a->rotation_z, a->rotation_w };
	gfloat s = pose_uniform_scale (a);
	gfloat sv[3] = { v[0] * s, v[1] * s, v[2] * s };

	quat_rotate_vec (q, sv, out);
	if (!tangent)
	{
		out[0] += a->position_x;
		out[1] += a->position_y;
		out[2] += a->position_z;
	}
}

/* fold the ancestor pose into a rotation value (or a tangent): R * q */
static void
fold_rotation (const LrgBonePose *a,
               const gfloat      *v,
               gfloat            *out)
{
	gfloat q[4] = { a->rotation_x, a->rotation_y, a->rotation_z, a->rotation_w };

	quat_mul (q, v, out);
}

/* fold the ancestor pose into a scale value (or a tangent) */
static void
fold_scale (const LrgBonePose *a,
            const gfloat      *v,
            gfloat            *out)
{
	gfloat s = pose_uniform_scale (a);

	out[0] = v[0] * s;
	out[1] = v[1] * s;
	out[2] = v[2] * s;
}

/* ------------------------------------------------------------------------ */
/* Loading                                                                  */
/* ------------------------------------------------------------------------ */

/* cgltf result -> a GError in LRG_GLTF_ERROR */
static void
set_cgltf_error (GError       **error,
                 cgltf_result   result,
                 const gchar   *what)
{
	gint code;

	switch (result)
	{
	case cgltf_result_data_too_short:
	case cgltf_result_unknown_format:
		code = LRG_GLTF_ERROR_INVALID_CONTAINER;
		break;
	case cgltf_result_invalid_json:
		code = LRG_GLTF_ERROR_INVALID_JSON;
		break;
	case cgltf_result_legacy_gltf:
		code = LRG_GLTF_ERROR_UNSUPPORTED_VERSION;
		break;
	default:
		code = LRG_GLTF_ERROR_INVALID_DATA;
		break;
	}
	g_set_error (error, LRG_GLTF_ERROR, code, "glTF %s failed (cgltf result %d)", what, (gint)result);
}

/*
 * check_buffer_uris:
 * External buffer files must stay under the base directory: no absolute
 * paths, no URL schemes, no ".." components. Without a base directory
 * only embedded data is accepted.
 */
static gboolean
check_buffer_uris (cgltf_data   *data,
                   gboolean      have_base,
                   GError      **error)
{
	cgltf_size i;

	for (i = 0; i < data->buffers_count; i++)
	{
		const gchar      *uri = data->buffers[i].uri;
		g_auto(GStrv)     parts = NULL;
		guint             k;

		if (uri == NULL || g_str_has_prefix (uri, "data:"))
			continue;
		if (!have_base)
		{
			g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
			             "glTF buffer %u names an external file without a base directory", (guint)i);
			return FALSE;
		}
		if (g_path_is_absolute (uri) || strstr (uri, "://") != NULL || uri[0] == '\\')
		{
			g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
			             "glTF buffer URI '%s' is not relative", uri);
			return FALSE;
		}
		parts = g_strsplit_set (uri, "/\\", -1);
		for (k = 0; parts[k] != NULL; k++)
		{
			if (g_strcmp0 (parts[k], "..") == 0)
			{
				g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED,
				             "glTF buffer URI '%s' leaves the file's directory", uri);
				return FALSE;
			}
		}
	}
	return TRUE;
}

/*
 * prepare:
 * Builds the name tables and, per node, the world transform of its
 * non-joint ancestors when it is a root joint of some skin.
 */
static void
prepare (LrgGltfAnimation *self)
{
	cgltf_data *d = self->data;
	GHashTable *seen;
	cgltf_size  i;
	cgltf_size  j;

	/* unique node names */
	self->node_names = g_new0 (gchar *, d->nodes_count + 1);
	seen = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < d->nodes_count; i++)
	{
		const gchar *name = d->nodes[i].name;

		if (name == NULL || name[0] == '\0')
			self->node_names[i] = g_strdup_printf ("node_%u", (guint)i);
		else if (g_hash_table_contains (seen, name))
			self->node_names[i] = g_strdup_printf ("%s_%u", name, (guint)i);
		else
			self->node_names[i] = g_strdup (name);
		g_hash_table_add (seen, self->node_names[i]);
	}
	g_hash_table_destroy (seen);

	self->skin_names = g_new0 (gchar *, d->skins_count + 1);
	for (i = 0; i < d->skins_count; i++)
		self->skin_names[i] = d->skins[i].name != NULL ? g_strdup (d->skins[i].name)
		                                               : g_strdup_printf ("skin_%u", (guint)i);
	self->clip_names = g_new0 (gchar *, d->animations_count + 1);
	for (i = 0; i < d->animations_count; i++)
		self->clip_names[i] = d->animations[i].name != NULL ? g_strdup (d->animations[i].name)
		                                                    : g_strdup_printf ("clip_%u", (guint)i);

	/* root joints: fold the world transform of the parent node */
	self->ancestor = g_new0 (LrgBonePose, MAX (d->nodes_count, 1));
	self->folded = g_new0 (gboolean, MAX (d->nodes_count, 1));
	for (i = 0; i < d->nodes_count; i++)
		lrg_bone_pose_set_identity (&self->ancestor[i]);
	for (i = 0; i < d->skins_count; i++)
	{
		const cgltf_skin *skin = &d->skins[i];

		for (j = 0; j < skin->joints_count; j++)
		{
			const cgltf_node *joint = skin->joints[j];
			const cgltf_node *up;
			gboolean          has_joint_ancestor = FALSE;
			cgltf_size        k;
			gfloat            m[16];
			cgltf_size        n = (cgltf_size)(joint - d->nodes);

			if (joint->parent == NULL || self->folded[n])
				continue;
			for (up = joint->parent; up != NULL && !has_joint_ancestor; up = up->parent)
				for (k = 0; k < skin->joints_count; k++)
					if (skin->joints[k] == up)
						has_joint_ancestor = TRUE;
			if (has_joint_ancestor)
				continue;
			cgltf_node_transform_world (joint->parent, m);
			if (!matrix_to_pose (m, &self->ancestor[n]))
				g_warning ("glTF joint '%s' has a non-uniformly scaled ancestor; its folded transform is approximate",
				           self->node_names[n]);
			self->folded[n] = TRUE;
		}
	}
}

static LrgGltfAnimation *
finish_load (cgltf_data   *data,
             GBytes       *bytes,
             const gchar  *gltf_path,
             GError      **error)
{
	g_autoptr(LrgGltfAnimation) self = NULL;
	cgltf_options               options;
	cgltf_result                result;

	self = g_object_new (LRG_TYPE_GLTF_ANIMATION, NULL);
	self->data = data;
	self->bytes = bytes != NULL ? g_bytes_ref (bytes) : NULL;

	if (!check_buffer_uris (data, gltf_path != NULL, error))
		return NULL;
	memset (&options, 0, sizeof options);
	result = cgltf_load_buffers (&options, data, gltf_path);
	if (result != cgltf_result_success)
	{
		set_cgltf_error (error, result, "buffer loading");
		return NULL;
	}
	result = cgltf_validate (data);
	if (result != cgltf_result_success)
	{
		set_cgltf_error (error, result, "validation");
		return NULL;
	}
	prepare (self);
	return g_steal_pointer (&self);
}

LrgGltfAnimation *
lrg_gltf_animation_new_from_file (const gchar  *path,
                                  GError      **error)
{
	cgltf_options options;
	cgltf_data   *data = NULL;
	cgltf_result  result;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "%s: no such file", path);
		return NULL;
	}
	memset (&options, 0, sizeof options);
	result = cgltf_parse_file (&options, path, &data);
	if (result != cgltf_result_success)
	{
		set_cgltf_error (error, result, "parsing");
		g_prefix_error (error, "%s: ", path);
		return NULL;
	}
	return finish_load (data, NULL, path, error);
}

LrgGltfAnimation *
lrg_gltf_animation_new_from_bytes (GBytes       *bytes,
                                   const gchar  *base_dir,
                                   GError      **error)
{
	cgltf_options     options;
	cgltf_data       *data = NULL;
	cgltf_result      result;
	gconstpointer     raw;
	gsize             size;
	g_autofree gchar *gltf_path = NULL;

	g_return_val_if_fail (bytes != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	raw = g_bytes_get_data (bytes, &size);
	memset (&options, 0, sizeof options);
	result = cgltf_parse (&options, raw, size, &data);
	if (result != cgltf_result_success)
	{
		set_cgltf_error (error, result, "parsing");
		return NULL;
	}
	/* cgltf resolves external files against the directory of a path */
	if (base_dir != NULL)
		gltf_path = g_build_filename (base_dir, "memory.gltf", NULL);
	return finish_load (data, bytes, gltf_path, error);
}

/* ------------------------------------------------------------------------ */
/* Names                                                                    */
/* ------------------------------------------------------------------------ */

guint
lrg_gltf_animation_get_node_count (LrgGltfAnimation *self)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), 0);
	return (guint)self->data->nodes_count;
}

const gchar *
lrg_gltf_animation_get_node_name (LrgGltfAnimation *self,
                                  guint             node)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	if (node >= self->data->nodes_count)
		return NULL;
	return self->node_names[node];
}

guint
lrg_gltf_animation_get_skin_count (LrgGltfAnimation *self)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), 0);
	return (guint)self->data->skins_count;
}

const gchar *
lrg_gltf_animation_get_skin_name (LrgGltfAnimation *self,
                                  guint             skin)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	if (skin >= self->data->skins_count)
		return NULL;
	return self->skin_names[skin];
}

guint
lrg_gltf_animation_get_skin_joint_count (LrgGltfAnimation *self,
                                         guint             skin)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), 0);
	if (skin >= self->data->skins_count)
		return 0;
	return (guint)self->data->skins[skin].joints_count;
}

guint
lrg_gltf_animation_get_clip_count (LrgGltfAnimation *self)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), 0);
	return (guint)self->data->animations_count;
}

const gchar *
lrg_gltf_animation_get_clip_name (LrgGltfAnimation *self,
                                  guint             clip)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	if (clip >= self->data->animations_count)
		return NULL;
	return self->clip_names[clip];
}

gint
lrg_gltf_animation_find_clip (LrgGltfAnimation *self,
                              const gchar      *name)
{
	cgltf_size i;

	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), -1);
	g_return_val_if_fail (name != NULL, -1);

	for (i = 0; i < self->data->animations_count; i++)
		if (g_strcmp0 (self->clip_names[i], name) == 0)
			return (gint)i;
	return -1;
}

guint
lrg_gltf_animation_get_mesh_count (LrgGltfAnimation *self)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), 0);
	return (guint)self->data->meshes_count;
}

guint
lrg_gltf_animation_get_mesh_morph_target_count (LrgGltfAnimation *self,
                                                guint             mesh)
{
	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), 0);
	if (mesh >= self->data->meshes_count || self->data->meshes[mesh].primitives_count == 0)
		return 0;
	return (guint)self->data->meshes[mesh].primitives[0].targets_count;
}

/* ------------------------------------------------------------------------ */
/* Skeleton                                                                 */
/* ------------------------------------------------------------------------ */

/* the node's rest pose with its folded ancestors */
static void
node_bind_pose (LrgGltfAnimation *self,
                cgltf_size        n,
                LrgBonePose      *pose)
{
	LrgBonePose rest;

	node_rest_pose (&self->data->nodes[n], &rest);
	if (self->folded[n])
	{
		g_autoptr(LrgBonePose) world = lrg_bone_pose_compose (&self->ancestor[n], &rest);

		/* compose multiplies scales per axis; folding uses the mean */
		*pose = *world;
		pose->scale_x = rest.scale_x * pose_uniform_scale (&self->ancestor[n]);
		pose->scale_y = rest.scale_y * pose_uniform_scale (&self->ancestor[n]);
		pose->scale_z = rest.scale_z * pose_uniform_scale (&self->ancestor[n]);
	}
	else
		*pose = rest;
}

LrgSkeleton *
lrg_gltf_animation_create_skeleton (LrgGltfAnimation  *self,
                                    guint              skin_index,
                                    GError           **error)
{
	g_autoptr(LrgSkeleton) skeleton = NULL;
	const cgltf_skin      *skin;
	g_autofree gfloat     *ibm = NULL;
	cgltf_size             j;
	cgltf_size             k;

	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (skin_index >= self->data->skins_count)
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
		             "glTF has no skin %u (%u skins)", skin_index, (guint)self->data->skins_count);
		return NULL;
	}
	skin = &self->data->skins[skin_index];

	/* the skin's inverse bind matrices, 16 floats per joint */
	if (skin->inverse_bind_matrices != NULL)
	{
		if (skin->inverse_bind_matrices->count < skin->joints_count)
		{
			g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
			             "glTF skin %u has fewer inverse bind matrices than joints", skin_index);
			return NULL;
		}
		ibm = g_new (gfloat, 16 * skin->joints_count);
		cgltf_accessor_unpack_floats (skin->inverse_bind_matrices, ibm, 16 * skin->joints_count);
	}

	skeleton = lrg_skeleton_new ();
	lrg_skeleton_set_name (skeleton, self->skin_names[skin_index]);
	for (j = 0; j < skin->joints_count; j++)
	{
		const cgltf_node  *joint = skin->joints[j];
		cgltf_size         n = (cgltf_size)(joint - self->data->nodes);
		g_autoptr(LrgBone) bone = lrg_bone_new (self->node_names[n], (gint)j);
		const cgltf_node  *up;
		gint               parent = -1;
		LrgBonePose        bind;

		/* nearest ancestor that is a joint of this skin */
		for (up = joint->parent; up != NULL && parent < 0; up = up->parent)
			for (k = 0; k < skin->joints_count; k++)
				if (skin->joints[k] == up)
				{
					parent = (gint)k;
					break;
				}
		lrg_bone_set_parent_index (bone, parent);
		node_bind_pose (self, n, &bind);
		lrg_bone_set_bind_pose (bone, &bind);
		lrg_bone_set_local_pose (bone, &bind);

		/* length: distance to the first child joint, else a small stub */
		lrg_bone_set_length (bone, 0.1f);
		for (k = 0; k < joint->children_count; k++)
		{
			LrgBonePose child;

			node_rest_pose (joint->children[k], &child);
			if (child.position_x != 0.0f || child.position_y != 0.0f || child.position_z != 0.0f)
			{
				lrg_bone_set_length (bone, sqrtf (child.position_x * child.position_x +
				                                  child.position_y * child.position_y +
				                                  child.position_z * child.position_z));
				break;
			}
		}
		lrg_skeleton_add_bone (skeleton, bone);

		if (ibm != NULL)
			lrg_skeleton_set_inverse_bind_matrix (skeleton, (gint)j, ibm + 16 * j);
		else
		{
			gfloat identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

			lrg_skeleton_set_inverse_bind_matrix (skeleton, (gint)j, identity);
		}
	}
	lrg_skeleton_calculate_world_poses (skeleton);
	return g_steal_pointer (&skeleton);
}

/* ------------------------------------------------------------------------ */
/* Clips                                                                    */
/* ------------------------------------------------------------------------ */

static LrgAnimationInterpolation
interpolation_of (cgltf_interpolation_type type)
{
	switch (type)
	{
	case cgltf_interpolation_type_step:
		return LRG_ANIMATION_INTERPOLATION_STEP;
	case cgltf_interpolation_type_cubic_spline:
		return LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE;
	case cgltf_interpolation_type_linear:
	case cgltf_interpolation_type_max_enum:
	default:
		return LRG_ANIMATION_INTERPOLATION_LINEAR;
	}
}

/* the morph target count a weights channel of @node drives */
static guint
node_target_count (const cgltf_node *node)
{
	if (node->mesh == NULL || node->mesh->primitives_count == 0)
		return 0;
	return (guint)node->mesh->primitives[0].targets_count;
}

/*
 * add_channel:
 * One glTF channel into the clip: TRS onto the node's track (with the
 * ancestor fold for root joints), weights as a weight channel.
 */
static gboolean
add_channel (LrgGltfAnimation              *self,
             LrgAnimationClip              *clip,
             const cgltf_animation_channel *channel,
             guint                          clip_index,
             guint                          channel_index,
             GError                       **error)
{
	const cgltf_animation_sampler *sampler = channel->sampler;
	LrgAnimationInterpolation      interp;
	cgltf_size                     n;
	cgltf_size                     n_keys;
	cgltf_size                     per_key;
	guint                          comps;
	g_autofree gfloat             *times = NULL;
	g_autofree gfloat             *values = NULL;
	gint                           track;
	cgltf_size                     i;

	if (channel->target_node == NULL || sampler == NULL || sampler->input == NULL ||
	    sampler->output == NULL)
		return TRUE;   /* a channel without a target is ignored, as glTF allows */

	n = (cgltf_size)(channel->target_node - self->data->nodes);
	interp = interpolation_of (sampler->interpolation);
	n_keys = sampler->input->count;
	per_key = interp == LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE ? 3 : 1;

	switch (channel->target_path)
	{
	case cgltf_animation_path_type_translation:
	case cgltf_animation_path_type_scale:
		comps = 3;
		break;
	case cgltf_animation_path_type_rotation:
		comps = 4;
		break;
	case cgltf_animation_path_type_weights:
		comps = node_target_count (channel->target_node);
		if (comps == 0 && n_keys > 0)
			comps = (guint)(sampler->output->count / (n_keys * per_key));
		break;
	case cgltf_animation_path_type_invalid:
	case cgltf_animation_path_type_max_enum:
	default:
		return TRUE;   /* unknown paths (extensions) are skipped */
	}

	/* values: one element per key (per_key for cubic); weights count
	 * scalars, so their element count is n_keys * per_key * targets */
	if (n_keys == 0 || comps == 0 ||
	    (channel->target_path == cgltf_animation_path_type_weights
	     ? sampler->output->count != n_keys * per_key * comps
	     : sampler->output->count != n_keys * per_key))
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
		             "glTF animation %u channel %u: %u keys but %u values",
		             clip_index, channel_index, (guint)n_keys, (guint)sampler->output->count);
		return FALSE;
	}
	times = g_new (gfloat, n_keys);
	cgltf_accessor_unpack_floats (sampler->input, times, n_keys);
	values = g_new (gfloat, n_keys * per_key * comps);
	cgltf_accessor_unpack_floats (sampler->output, values, n_keys * per_key * comps);

	if (channel->target_path == cgltf_animation_path_type_weights)
		return lrg_animation_clip_add_weights_channel (clip, self->node_names[n], comps, interp,
		                                               (guint)n_keys, times, values, error) >= 0;

	/* the node's track: created with its rest pose as the base */
	track = lrg_animation_clip_find_track (clip, self->node_names[n]);
	if (track < 0)
	{
		LrgBonePose base;

		track = (gint)lrg_animation_clip_add_track (clip, self->node_names[n]);
		node_bind_pose (self, n, &base);
		lrg_animation_clip_set_track_base_pose (clip, (guint)track, &base);
	}

	/* root joints: fold the ancestors into every key (and tangent) */
	if (self->folded[n])
	{
		const LrgBonePose *a = &self->ancestor[n];

		for (i = 0; i < n_keys * per_key; i++)
		{
			gfloat  *v = values + i * comps;
			gboolean tangent = per_key == 3 && i % 3 != 1;
			gfloat   out[4];

			switch (channel->target_path)
			{
			case cgltf_animation_path_type_translation:
				fold_translation (a, v, tangent, out);
				break;
			case cgltf_animation_path_type_rotation:
				fold_rotation (a, v, out);
				break;
			case cgltf_animation_path_type_scale:
			case cgltf_animation_path_type_weights:
			case cgltf_animation_path_type_invalid:
			case cgltf_animation_path_type_max_enum:
			default:
				fold_scale (a, v, out);
				break;
			}
			memcpy (v, out, comps * sizeof (gfloat));
		}
	}

	return lrg_animation_clip_set_track_channel (clip, (guint)track,
	                                             channel->target_path == cgltf_animation_path_type_translation
	                                             ? LRG_ANIMATION_CHANNEL_TRANSLATION
	                                             : channel->target_path == cgltf_animation_path_type_rotation
	                                             ? LRG_ANIMATION_CHANNEL_ROTATION
	                                             : LRG_ANIMATION_CHANNEL_SCALE,
	                                             interp, (guint)n_keys, times, values, error);
}

LrgAnimationClip *
lrg_gltf_animation_create_clip (LrgGltfAnimation  *self,
                                guint              clip_index,
                                GError           **error)
{
	g_autoptr(LrgAnimationClip) clip = NULL;
	const cgltf_animation      *anim;
	cgltf_size                  c;

	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (clip_index >= self->data->animations_count)
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
		             "glTF has no animation %u (%u animations)", clip_index,
		             (guint)self->data->animations_count);
		return NULL;
	}
	anim = &self->data->animations[clip_index];
	clip = lrg_animation_clip_new (self->clip_names[clip_index]);
	lrg_animation_clip_set_loop_mode (clip, LRG_ANIMATION_LOOP_REPEAT);
	for (c = 0; c < anim->channels_count; c++)
		if (!add_channel (self, clip, &anim->channels[c], clip_index, (guint)c, error))
			return NULL;
	return g_steal_pointer (&clip);
}

GPtrArray *
lrg_gltf_animation_create_clips (LrgGltfAnimation  *self,
                                 GError           **error)
{
	g_autoptr(GPtrArray) clips = NULL;
	cgltf_size           i;

	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	clips = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < self->data->animations_count; i++)
	{
		LrgAnimationClip *clip = lrg_gltf_animation_create_clip (self, (guint)i, error);

		if (clip == NULL)
			return NULL;
		g_ptr_array_add (clips, clip);
	}
	return g_steal_pointer (&clips);
}

/* ------------------------------------------------------------------------ */
/* Meshes                                                                   */
/* ------------------------------------------------------------------------ */

/* growable vertex streams of a mesh being merged */
typedef struct
{
	GArray *positions;   /* gfloat x3 */
	GArray *normals;     /* gfloat x3 */
	GArray *texcoords;   /* gfloat x2 */
	GArray *colors;      /* guint8 x4 */
	GArray *bone_idx;    /* guint8 x4 */
	GArray *bone_w;      /* gfloat x4 */
	GArray *indices;     /* guint32 */
	gboolean any_normals;
	gboolean any_texcoords;
	gboolean any_colors;

	/* morph targets: per target its group, name, default weight and
	 * vertex range; deltas recorded per vertex range */
	GPtrArray *morph_groups;   /* gchar*, per target */
	GPtrArray *morph_names;    /* gchar*, per target */
	GArray    *morph_default;  /* gfloat, per target */
	GPtrArray *morph_pos;      /* GArray<gfloat> sized like positions, per target */
	GPtrArray *morph_nrm;      /* GArray<gfloat> or NULL, per target */
} MeshBuilder;

static void
builder_init (MeshBuilder *b)
{
	memset (b, 0, sizeof *b);
	b->positions = g_array_new (FALSE, TRUE, sizeof (gfloat));
	b->normals = g_array_new (FALSE, TRUE, sizeof (gfloat));
	b->texcoords = g_array_new (FALSE, TRUE, sizeof (gfloat));
	b->colors = g_array_new (FALSE, TRUE, sizeof (guint8));
	b->bone_idx = g_array_new (FALSE, TRUE, sizeof (guint8));
	b->bone_w = g_array_new (FALSE, TRUE, sizeof (gfloat));
	b->indices = g_array_new (FALSE, TRUE, sizeof (guint32));
	b->morph_groups = g_ptr_array_new_with_free_func (g_free);
	b->morph_names = g_ptr_array_new_with_free_func (g_free);
	b->morph_default = g_array_new (FALSE, TRUE, sizeof (gfloat));
	b->morph_pos = g_ptr_array_new_with_free_func ((GDestroyNotify)g_array_unref);
	b->morph_nrm = g_ptr_array_new ();
}

static void
builder_clear (MeshBuilder *b)
{
	guint i;

	g_array_unref (b->positions);
	g_array_unref (b->normals);
	g_array_unref (b->texcoords);
	g_array_unref (b->colors);
	g_array_unref (b->bone_idx);
	g_array_unref (b->bone_w);
	g_array_unref (b->indices);
	g_ptr_array_unref (b->morph_groups);
	g_ptr_array_unref (b->morph_names);
	g_array_unref (b->morph_default);
	g_ptr_array_unref (b->morph_pos);
	for (i = 0; i < b->morph_nrm->len; i++)
		if (g_ptr_array_index (b->morph_nrm, i) != NULL)
			g_array_unref (g_ptr_array_index (b->morph_nrm, i));
	g_ptr_array_unref (b->morph_nrm);
}

static const cgltf_accessor *
find_attribute (const cgltf_attribute *attrs,
                cgltf_size             count,
                cgltf_attribute_type   type,
                cgltf_int              index)
{
	cgltf_size i;

	for (i = 0; i < count; i++)
		if (attrs[i].type == type && attrs[i].index == index)
			return attrs[i].data;
	return NULL;
}

/* unpack an accessor of n elements, comps floats each (zeros when absent) */
static void
unpack_into (const cgltf_accessor *a,
             cgltf_size            n,
             guint                 comps,
             gfloat               *out)
{
	g_autofree gfloat *tmp = NULL;
	cgltf_size         have;
	cgltf_size         i;
	guint              c;

	memset (out, 0, n * comps * sizeof (gfloat));
	if (a == NULL)
		return;
	have = cgltf_num_components (a->type);
	tmp = g_new0 (gfloat, a->count * have);
	cgltf_accessor_unpack_floats (a, tmp, a->count * have);
	for (i = 0; i < n && i < a->count; i++)
		for (c = 0; c < comps && c < have; c++)
			out[i * comps + c] = tmp[i * have + c];
}

/* the default morph weight of target t of @node (node weights, else mesh) */
static gfloat
default_weight (const cgltf_node *node,
                const cgltf_mesh *mesh,
                cgltf_size        t)
{
	if (node != NULL && t < node->weights_count)
		return node->weights[t];
	if (t < mesh->weights_count)
		return mesh->weights[t];
	return 0.0f;
}

/*
 * builder_add_mesh:
 * Appends the triangle primitives of @mesh. With @skinned the joint
 * indices and weights are read; without, every vertex follows bone 0.
 * Morph targets go into a group named @group; every target of the group
 * covers all vertices of the merged mesh (zero outside this mesh).
 */
static gboolean
builder_add_mesh (MeshBuilder       *b,
                  const cgltf_node  *node,
                  const cgltf_mesh  *mesh,
                  const gchar       *group,
                  gboolean           skinned,
                  guint              n_joints,
                  GError           **error)
{
	cgltf_size p;
	cgltf_size t;
	guint      first_target = b->morph_pos->len;
	cgltf_size n_targets = mesh->primitives_count > 0 ? mesh->primitives[0].targets_count : 0;

	/* the group's targets, zero-filled up to the current vertex count */
	for (t = 0; t < n_targets; t++)
	{
		GArray *pos = g_array_new (FALSE, TRUE, sizeof (gfloat));
		gchar  *name;

		g_array_set_size (pos, b->positions->len);
		g_ptr_array_add (b->morph_pos, pos);
		g_ptr_array_add (b->morph_nrm, NULL);
		g_ptr_array_add (b->morph_groups, g_strdup (group));
		if (t < mesh->target_names_count && mesh->target_names[t] != NULL)
			name = g_strdup (mesh->target_names[t]);
		else
			name = g_strdup_printf ("%s.%u", group, (guint)t);
		g_ptr_array_add (b->morph_names, name);
		{
			gfloat w = default_weight (node, mesh, t);

			g_array_append_val (b->morph_default, w);
		}
	}

	for (p = 0; p < mesh->primitives_count; p++)
	{
		const cgltf_primitive *prim = &mesh->primitives[p];
		const cgltf_accessor  *pos_acc;
		const cgltf_accessor  *a;
		cgltf_size             n;
		guint                  base = b->positions->len / 3;
		guint                  old_len;
		cgltf_size             v;
		g_autofree gfloat     *tmp = NULL;

		if (prim->type != cgltf_primitive_type_triangles)
			continue;
		pos_acc = find_attribute (prim->attributes, prim->attributes_count,
		                          cgltf_attribute_type_position, 0);
		if (pos_acc == NULL)
			continue;
		n = pos_acc->count;
		if (base + n > GLTF_MAX_MESH_VERTICES)
		{
			g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
			             "glTF mesh '%s' needs more than %u vertices", group, GLTF_MAX_MESH_VERTICES);
			return FALSE;
		}

		/* positions */
		old_len = b->positions->len;
		g_array_set_size (b->positions, old_len + 3 * n);
		unpack_into (pos_acc, n, 3, &g_array_index (b->positions, gfloat, old_len));

		/* normals (zero rows mark vertices without) */
		a = find_attribute (prim->attributes, prim->attributes_count, cgltf_attribute_type_normal, 0);
		old_len = b->normals->len;
		g_array_set_size (b->normals, old_len + 3 * n);
		unpack_into (a, n, 3, &g_array_index (b->normals, gfloat, old_len));
		b->any_normals |= a != NULL;

		/* texcoords */
		a = find_attribute (prim->attributes, prim->attributes_count, cgltf_attribute_type_texcoord, 0);
		old_len = b->texcoords->len;
		g_array_set_size (b->texcoords, old_len + 2 * n);
		unpack_into (a, n, 2, &g_array_index (b->texcoords, gfloat, old_len));
		b->any_texcoords |= a != NULL;

		/* colours: floats 0..1 -> RGBA8; RGB gets alpha 1 */
		a = find_attribute (prim->attributes, prim->attributes_count, cgltf_attribute_type_color, 0);
		tmp = g_new (gfloat, 4 * n);
		unpack_into (a, n, 4, tmp);
		old_len = b->colors->len;
		g_array_set_size (b->colors, old_len + 4 * n);
		for (v = 0; v < n; v++)
		{
			guint c;

			if (a != NULL && cgltf_num_components (a->type) == 3)
				tmp[4 * v + 3] = 1.0f;
			for (c = 0; c < 4; c++)
				g_array_index (b->colors, guint8, old_len + 4 * v + c) =
					a != NULL ? (guint8)(CLAMP (tmp[4 * v + c], 0.0f, 1.0f) * 255.0f + 0.5f) : 255;
		}
		b->any_colors |= a != NULL;

		/* influences */
		old_len = b->bone_w->len;
		g_array_set_size (b->bone_w, old_len + 4 * n);
		g_array_set_size (b->bone_idx, old_len + 4 * n);
		if (skinned)
		{
			const cgltf_accessor *ja = find_attribute (prim->attributes, prim->attributes_count,
			                                           cgltf_attribute_type_joints, 0);
			const cgltf_accessor *wa = find_attribute (prim->attributes, prim->attributes_count,
			                                           cgltf_attribute_type_weights, 0);

			unpack_into (wa, n, 4, &g_array_index (b->bone_w, gfloat, old_len));
			for (v = 0; v < n; v++)
			{
				cgltf_uint j[4] = { 0, 0, 0, 0 };
				guint      c;

				if (ja != NULL)
					cgltf_accessor_read_uint (ja, v, j, 4);
				for (c = 0; c < 4; c++)
				{
					if (j[c] >= n_joints)
					{
						g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
						             "glTF mesh '%s' vertex %u names joint %u of %u",
						             group, (guint)v, (guint)j[c], n_joints);
						return FALSE;
					}
					g_array_index (b->bone_idx, guint8, old_len + 4 * v + c) = (guint8)j[c];
				}
			}
		}
		else
			for (v = 0; v < n; v++)
				g_array_index (b->bone_w, gfloat, old_len + 4 * v) = 1.0f;

		/* indices (sequential when the primitive has none) */
		if (prim->indices != NULL)
		{
			cgltf_size k;

			for (k = 0; k < prim->indices->count; k++)
			{
				guint32 idx = (guint32)cgltf_accessor_read_index (prim->indices, k) + base;

				g_array_append_val (b->indices, idx);
			}
		}
		else
		{
			for (v = 0; v < n; v++)
			{
				guint32 idx = (guint32)(base + v);

				g_array_append_val (b->indices, idx);
			}
		}

		/* every target grows by this primitive's vertices: the deltas of
		 * this mesh's targets, zeros for the other groups */
		for (t = 0; t < b->morph_pos->len; t++)
		{
			GArray *pos = g_ptr_array_index (b->morph_pos, t);
			GArray *nrm = g_ptr_array_index (b->morph_nrm, t);
			guint   at = pos->len;

			g_array_set_size (pos, at + 3 * n);
			if (nrm != NULL)
				g_array_set_size (nrm, at + 3 * n);
			if (t >= first_target && t - first_target < prim->targets_count)
			{
				const cgltf_morph_target *mt = &prim->targets[t - first_target];
				const cgltf_accessor     *dp = find_attribute (mt->attributes, mt->attributes_count,
				                                               cgltf_attribute_type_position, 0);
				const cgltf_accessor     *dn = find_attribute (mt->attributes, mt->attributes_count,
				                                               cgltf_attribute_type_normal, 0);

				unpack_into (dp, n, 3, &g_array_index (pos, gfloat, at));
				if (dn != NULL)
				{
					if (nrm == NULL)
					{
						/* first normal deltas: zeros for what came before */
						nrm = g_array_new (FALSE, TRUE, sizeof (gfloat));
						g_array_set_size (nrm, at + 3 * n);
						g_ptr_array_index (b->morph_nrm, t) = nrm;
					}
					unpack_into (dn, n, 3, &g_array_index (nrm, gfloat, at));
				}
			}
		}
	}
	return TRUE;
}

/* the builder as an LrgSkinnedMesh with n_bones bones */
static LrgSkinnedMesh *
builder_finish (MeshBuilder  *b,
                guint         n_bones,
                const gchar  *what,
                GError      **error)
{
	g_autoptr(LrgSkinnedMesh) mesh = NULL;
	guint                     n = b->positions->len / 3;
	guint                     t;

	if (n == 0 || b->indices->len == 0)
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
		             "glTF %s has no triangle primitive", what);
		return NULL;
	}
	mesh = lrg_skinned_mesh_new (n_bones);
	if (mesh == NULL)
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
		             "glTF %s has %u joints (1 .. 255 supported)", what, n_bones);
		return NULL;
	}
	if (!lrg_skinned_mesh_set_geometry (mesh, n,
	                                    (const gfloat *)(gpointer)b->positions->data,
	                                    b->any_normals ? (const gfloat *)(gpointer)b->normals->data : NULL,
	                                    b->any_texcoords ? (const gfloat *)(gpointer)b->texcoords->data : NULL,
	                                    b->any_colors ? (const guint8 *)b->colors->data : NULL,
	                                    (const guint8 *)b->bone_idx->data,
	                                    (const gfloat *)(gpointer)b->bone_w->data,
	                                    b->indices->len,
	                                    (const guint32 *)(gpointer)b->indices->data, error))
		return NULL;

	if (b->morph_pos->len > 0)
	{
		g_autoptr(LrgMorphTargets) targets = lrg_morph_targets_new (n);

		for (t = 0; t < b->morph_pos->len; t++)
		{
			GArray *pos = g_ptr_array_index (b->morph_pos, t);
			GArray *nrm = g_ptr_array_index (b->morph_nrm, t);

			lrg_morph_targets_add_target (targets, g_ptr_array_index (b->morph_groups, t),
			                              g_ptr_array_index (b->morph_names, t),
			                              (const gfloat *)(gpointer)pos->data,
			                              nrm != NULL ? (const gfloat *)(gpointer)nrm->data : NULL,
			                              g_array_index (b->morph_default, gfloat, t));
		}
		lrg_skinned_mesh_set_morph_targets (mesh, targets);
	}
	return g_steal_pointer (&mesh);
}

LrgSkinnedMesh *
lrg_gltf_animation_create_skinned_mesh (LrgGltfAnimation  *self,
                                        guint              skin_index,
                                        GError           **error)
{
	g_autoptr(LrgSkeleton)    skeleton = NULL;
	LrgSkinnedMesh           *mesh;
	const cgltf_skin         *skin;
	MeshBuilder               b;
	cgltf_size                i;

	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	skeleton = lrg_gltf_animation_create_skeleton (self, skin_index, error);
	if (skeleton == NULL)
		return NULL;
	skin = &self->data->skins[skin_index];
	if (skin->joints_count == 0 || skin->joints_count > GLTF_MAX_JOINTS)
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_DATA,
		             "glTF skin %u has %u joints (1 .. %u supported)", skin_index,
		             (guint)skin->joints_count, GLTF_MAX_JOINTS);
		return NULL;
	}

	builder_init (&b);
	for (i = 0; i < self->data->nodes_count; i++)
	{
		const cgltf_node *node = &self->data->nodes[i];

		if (node->skin != skin || node->mesh == NULL)
			continue;
		if (!builder_add_mesh (&b, node, node->mesh, self->node_names[i], TRUE,
		                       (guint)skin->joints_count, error))
		{
			builder_clear (&b);
			return NULL;
		}
	}
	mesh = builder_finish (&b, (guint)skin->joints_count, self->skin_names[skin_index], error);
	builder_clear (&b);
	if (mesh == NULL)
		return NULL;

	/* inverse binds from the skin; the palette at the bind pose */
	lrg_skinned_mesh_set_inverse_binds_from_skeleton (mesh, skeleton);
	lrg_skinned_mesh_apply_skeleton (mesh, skeleton);
	return mesh;
}

LrgSkinnedMesh *
lrg_gltf_animation_create_mesh (LrgGltfAnimation  *self,
                                guint              mesh_index,
                                GError           **error)
{
	const cgltf_mesh *gmesh;
	const cgltf_node *user = NULL;
	const gchar      *group;
	LrgSkinnedMesh   *mesh;
	MeshBuilder       b;
	cgltf_size        i;

	g_return_val_if_fail (LRG_IS_GLTF_ANIMATION (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (mesh_index >= self->data->meshes_count)
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE,
		             "glTF has no mesh %u (%u meshes)", mesh_index, (guint)self->data->meshes_count);
		return NULL;
	}
	gmesh = &self->data->meshes[mesh_index];

	/* the morph group: the first node using the mesh, else the mesh name */
	for (i = 0; i < self->data->nodes_count && user == NULL; i++)
		if (self->data->nodes[i].mesh == gmesh)
			user = &self->data->nodes[i];
	if (user != NULL)
		group = self->node_names[user - self->data->nodes];
	else
		group = gmesh->name != NULL ? gmesh->name : "mesh";

	builder_init (&b);
	if (!builder_add_mesh (&b, user, gmesh, group, FALSE, 1, error))
	{
		builder_clear (&b);
		return NULL;
	}
	mesh = builder_finish (&b, 1, group, error);
	builder_clear (&b);
	return mesh;
}

/* ------------------------------------------------------------------------ */
/* Convenience                                                              */
/* ------------------------------------------------------------------------ */

LrgSkeleton *
lrg_gltf_load_skeleton (const gchar  *path,
                        guint         skin,
                        GError      **error)
{
	g_autoptr(LrgGltfAnimation) importer = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	importer = lrg_gltf_animation_new_from_file (path, error);
	if (importer == NULL)
		return NULL;
	return lrg_gltf_animation_create_skeleton (importer, skin, error);
}

GPtrArray *
lrg_gltf_load_clips (const gchar  *path,
                     GError      **error)
{
	g_autoptr(LrgGltfAnimation) importer = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	importer = lrg_gltf_animation_new_from_file (path, error);
	if (importer == NULL)
		return NULL;
	return lrg_gltf_animation_create_clips (importer, error);
}
