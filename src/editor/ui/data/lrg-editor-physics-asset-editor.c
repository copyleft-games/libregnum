/* lrg-editor-physics-asset-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPhysicsAssetEditor: the `.rphysasset` data editor (see the
 * header), plus the project's physics world settings and the
 * `physics.layers.*` IPC methods (the Physics page of Project Settings in
 * lrg-editor-dialogs.c shares the settings helpers).
 *
 * Layout:
 *
 *   +---------------------------------+---------------------------------+
 *   | (one scrolling column)          | Model [chars/hero.glb v] [Sim]  |
 *   | PARTS  14 · 70 kg               | +-----------------------------+ |
 *   | [bone v] [Add Part] [Remove]    | |                             | |
 *   |  pelvis     box      12.0 kg    | |   skinned model, part       | |
 *   |  spine      capsule   9.4 kg    | |   shapes, joint arcs and    | |
 *   |  ...                            | |   cones of the selection    | |
 *   | SELECTED PART: property grid    | |                             | |
 *   | GENERATE [70] [0.2] [Gen] [Stab]| |   [Mesh][Shapes][Joints]    | |
 *   | EXCLUSIONS  a - b  [Remove]     | +-----------------------------+ |
 *   | [a v] [b v] [Exclude]           |                                 |
 *   +---------------------------------+---------------------------------+
 *
 * The document is the truth. After every change its text is parsed with
 * the engine's strict loader into an LrgPhysicsAsset (a load error is an
 * issue and empties the views). Edits never touch that parsed asset: they
 * parse a fresh copy, change it through the LrgPhysicsAsset API (which
 * validates), and write it back:
 *  - a part edit rewrites `/parts/<i>` in the writer's canonical form (the
 *    defaults left out), merged into one undo step per grid gesture
 *    (lrg_editor_document_replace_gesture());
 *  - a structural edit (add / remove part, generate, stabilize,
 *    exclusions) rewrites the whole document in one labelled group.
 * The preview model is editor state in the asset's `.meta` sidecar.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-physics-asset-editor.h"
#include "lrg-editor-anim-graph-editor.h"
#include "lrg-editor-material-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../lrg-project.h"
#include "../../../core/lrg-asset-meta.h"
#include "../../../animation/lrg-bone.h"
#include "../../../animation/lrg-gltf-animation.h"
#include "../../../physics/lrg-dynamics.h"
#include "../../../physics/lrg-physics-shape.h"
#include "../../../physics/lrg-physics-layers.h"
#include "../../../debug/lrg-debug-draw.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-viewport.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-spin-box.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <yaml-glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define INSTALLED_KEY     "lrg-editor-physasset-installed"
#define META_MODEL_KEY    "preview_model"
#define VIEW_FOV          (36.0f)
#define SIM_FRAME         (1.0f / 60.0f)
#define DEFAULT_MASS      (70.0f)
#define DEFAULT_RATIO     (0.2f)
#define NONE_LABEL        "(none)"

/* ==========================================================================
 * Small math (quaternions are xyzw)
 * ========================================================================== */

/* o = a * b: apply b, then a */
static void
quat_mul (const gfloat *a,
          const gfloat *b,
          gfloat       *o)
{
	gfloat r[4];

	r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	memcpy (o, r, sizeof r);
}

/* o = q v q^-1 */
static void
quat_rotate (const gfloat *q,
             const gfloat *v,
             gfloat       *o)
{
	gfloat tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	gfloat ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	gfloat tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	gfloat r[3];

	r[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	r[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	r[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
	memcpy (o, r, sizeof r);
}

/*
 * Euler angles (radians) <-> quaternion. The rotation applies X first,
 * then Y, then Z (q = qz * qy * qx), the order the grid shows.
 */
static void
quat_from_euler (gfloat  x,
                 gfloat  y,
                 gfloat  z,
                 gfloat *q)
{
	gfloat cx = cosf (0.5f * x), sx = sinf (0.5f * x);
	gfloat cy = cosf (0.5f * y), sy = sinf (0.5f * y);
	gfloat cz = cosf (0.5f * z), sz = sinf (0.5f * z);

	q[0] = sx * cy * cz - cx * sy * sz;
	q[1] = cx * sy * cz + sx * cy * sz;
	q[2] = cx * cy * sz - sx * sy * cz;
	q[3] = cx * cy * cz + sx * sy * sz;
}

static void
quat_to_euler (const gfloat *q,
               gfloat       *e)
{
	gfloat x = q[0], y = q[1], z = q[2], w = q[3];
	gfloat s = 2.0f * (w * y - z * x);

	e[0] = atan2f (2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y));
	e[1] = asinf (CLAMP (s, -1.0f, 1.0f));
	e[2] = atan2f (2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z));
}

/* A rigid transform: position + rotation. */
typedef struct
{
	gfloat p[3];
	gfloat q[4];
} Xf;

/* o = a * (p, q): @p and @q given in a's frame */
static void
xf_apply (const Xf     *a,
          const gfloat *p,
          const gfloat *q,
          Xf           *o)
{
	gfloat r[3];
	Xf     t;

	quat_rotate (a->q, p, r);
	t.p[0] = a->p[0] + r[0];
	t.p[1] = a->p[1] + r[1];
	t.p[2] = a->p[2] + r[2];
	quat_mul (a->q, q, t.q);
	*o = t;
}

/* A point of frame @f at local @v. */
static GrlVector3
xf_point (const Xf *f,
          gfloat    x,
          gfloat    y,
          gfloat    z)
{
	gfloat     v[3] = { x, y, z };
	gfloat     r[3];
	GrlVector3 out;

	quat_rotate (f->q, v, r);
	out.x = f->p[0] + r[0];
	out.y = f->p[1] + r[1];
	out.z = f->p[2] + r[2];
	return out;
}

/* Column-major matrix of a transform (for oriented boxes). */
static void
xf_matrix (const Xf *f,
           gfloat   *m)
{
	gfloat x = f->q[0], y = f->q[1], z = f->q[2], w = f->q[3];

	m[0] = 1 - 2 * (y * y + z * z);
	m[1] = 2 * (x * y + z * w);
	m[2] = 2 * (x * z - y * w);
	m[3] = 0;
	m[4] = 2 * (x * y - z * w);
	m[5] = 1 - 2 * (x * x + z * z);
	m[6] = 2 * (y * z + x * w);
	m[7] = 0;
	m[8] = 2 * (x * z + y * w);
	m[9] = 2 * (y * z - x * w);
	m[10] = 1 - 2 * (x * x + y * y);
	m[11] = 0;
	m[12] = f->p[0];
	m[13] = f->p[1];
	m[14] = f->p[2];
	m[15] = 1;
}

/* ==========================================================================
 * Shape and joint names (the format's nicks)
 * ========================================================================== */

static const gchar *
shape_nick (LrgPhysicsAssetShape shape)
{
	switch (shape)
	{
	case LRG_PHYSICS_ASSET_SHAPE_BOX:
		return "box";
	case LRG_PHYSICS_ASSET_SHAPE_SPHERE:
		return "sphere";
	case LRG_PHYSICS_ASSET_SHAPE_CAPSULE:
	default:
		return "capsule";
	}
}

static gboolean
shape_from_nick (const gchar          *nick,
                 LrgPhysicsAssetShape *out)
{
	if (g_strcmp0 (nick, "capsule") == 0)
		*out = LRG_PHYSICS_ASSET_SHAPE_CAPSULE;
	else if (g_strcmp0 (nick, "box") == 0)
		*out = LRG_PHYSICS_ASSET_SHAPE_BOX;
	else if (g_strcmp0 (nick, "sphere") == 0)
		*out = LRG_PHYSICS_ASSET_SHAPE_SPHERE;
	else
		return FALSE;
	return TRUE;
}

static const gchar *
joint_nick (LrgJointKind kind)
{
	switch (kind)
	{
	case LRG_JOINT_FIXED:
		return "fixed";
	case LRG_JOINT_POINT:
		return "point";
	case LRG_JOINT_HINGE:
		return "hinge";
	case LRG_JOINT_CONE:
		return "cone";
	case LRG_JOINT_SWING_TWIST:
	default:
		return "swing-twist";
	}
}

static gboolean
joint_from_nick (const gchar  *nick,
                 LrgJointKind *out)
{
	if (g_strcmp0 (nick, "fixed") == 0)
		*out = LRG_JOINT_FIXED;
	else if (g_strcmp0 (nick, "point") == 0)
		*out = LRG_JOINT_POINT;
	else if (g_strcmp0 (nick, "hinge") == 0)
		*out = LRG_JOINT_HINGE;
	else if (g_strcmp0 (nick, "cone") == 0)
		*out = LRG_JOINT_CONE;
	else if (g_strcmp0 (nick, "swing-twist") == 0 || g_strcmp0 (nick, "swing_twist") == 0)
		*out = LRG_JOINT_SWING_TWIST;
	else
		return FALSE;
	return TRUE;
}

static gboolean
joint_has_twist (LrgJointKind kind)
{
	return kind == LRG_JOINT_HINGE || kind == LRG_JOINT_SWING_TWIST;
}

static gboolean
joint_has_swing (LrgJointKind kind)
{
	return kind == LRG_JOINT_CONE || kind == LRG_JOINT_SWING_TWIST;
}

/* ==========================================================================
 * The preview view (an internal LrgContainer: viewport, caption, chips)
 * ========================================================================== */

#define EDITOR_TYPE_PHYS_VIEW (editor_phys_view_get_type ())
G_DECLARE_FINAL_TYPE (EditorPhysView, editor_phys_view, EDITOR, PHYS_VIEW, LrgContainer)

struct _EditorPhysView
{
	LrgContainer                 parent_instance;

	LrgEditorPhysicsAssetEditor *editor;     /* weak */
	LrgViewport                 *viewport;
	LrgWidget                   *caption;
	LrgWidget                   *pill;
	LrgWidget                   *bar;
	LrgWidget                   *mesh_chip;
	LrgWidget                   *shapes_chip;
	LrgWidget                   *joints_chip;

	gfloat                       yaw;
	gfloat                       pitch;
	gfloat                       zoom;
	gboolean                     dragging;
	gfloat                       last_x;
	gfloat                       last_y;
};

G_DEFINE_FINAL_TYPE (EditorPhysView, editor_phys_view, LRG_TYPE_CONTAINER)

/* ==========================================================================
 * LrgEditorPhysicsAssetEditor
 * ========================================================================== */

struct _LrgEditorPhysicsAssetEditor
{
	LrgEditorDataEditor  parent_instance;

	/* the model of the document */
	LrgPhysicsAsset     *asset;          /* parsed from the document */
	gchar               *load_error;
	gint                 selected;

	/* the preview model */
	gchar               *model;          /* absolute, canonical */
	LrgSkeleton         *skeleton;
	LrgSkinnedMesh      *mesh;
	gboolean             uploaded;
	gboolean             upload_failed;
	gfloat               bmin[3];
	gfloat               bmax[3];

	/* the simulation */
	LrgDynamicsWorld    *world;
	LrgRagdoll          *ragdoll;
	gfloat               sim_time;
	LrgDebugDraw        *draw;

	/* view toggles */
	gboolean             show_mesh;
	gboolean             show_shapes;
	gboolean             show_joints;

	/* widgets */
	EditorPhysView      *view;
	LrgComboBox         *model_combo;
	LrgButton           *sim_button;
	LrgWidget           *parts;
	LrgLabel            *summary;
	LrgComboBox         *add_combo;
	LrgSpinBox          *gen_mass;
	LrgSpinBox          *gen_ratio;
	LrgWidget           *exclusions;
	LrgComboBox         *excl_a;
	LrgComboBox         *excl_b;
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	GPtrArray           *model_list;

	/* bookkeeping */
	guint                gesture;        /* the grid's gesture, 0 outside one */
	gboolean             syncing;
	gboolean             parts_stale;
	gboolean             combos_stale;
	gboolean             fields_stale;
	gchar               *fields_sig;
	gchar               *parts_sig;
};

G_DEFINE_FINAL_TYPE (LrgEditorPhysicsAssetEditor, lrg_editor_physics_asset_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void stop_simulation (LrgEditorPhysicsAssetEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorPhysicsAssetEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorPhysicsAssetEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* @path made absolute against the project (or the cwd). */
static gchar *
abs_path (LrgEditorApp *app,
          const gchar  *path)
{
	g_autofree gchar *p = NULL;

	if (g_path_is_absolute (path))
		return g_canonicalize_filename (path, NULL);
	p = app != NULL ? editor_app_project_path (app, path) : g_strdup (path);
	return g_canonicalize_filename (p, NULL);
}

/* ==========================================================================
 * The asset
 * ========================================================================== */

/* A fresh asset parsed from the document text (what edits change). */
static LrgPhysicsAsset *
parse_doc (LrgEditorPhysicsAssetEditor  *self,
           GError                      **error)
{
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);

	return lrg_physics_asset_new_from_data (text, -1, error);
}

static void
load_asset (LrgEditorPhysicsAssetEditor *self)
{
	g_autoptr(GError) error = NULL;

	g_clear_object (&self->asset);
	g_clear_pointer (&self->load_error, g_free);
	self->asset = parse_doc (self, &error);
	if (self->asset == NULL)
		self->load_error = g_strdup (error->message);
	if (self->asset == NULL || self->selected >= (gint) lrg_physics_asset_get_part_count (self->asset))
		self->selected = self->asset != NULL && lrg_physics_asset_get_part_count (self->asset) > 0
		                 ? (gint) lrg_physics_asset_get_part_count (self->asset) - 1 : -1;
}

/* The part whose bone is the nearest ancestor of @index's bone, or -1. */
static gint
parent_part (LrgEditorPhysicsAssetEditor *self,
             LrgPhysicsAsset             *asset,
             guint                        index)
{
	const LrgPhysicsAssetPart *part = lrg_physics_asset_get_part (asset, index);
	LrgBone                   *bone;
	gint                       p, depth = 0;

	if (part == NULL || self->skeleton == NULL)
		return -1;
	bone = lrg_skeleton_get_bone_by_name (self->skeleton, part->bone);
	if (bone == NULL)
		return -1;
	p = lrg_bone_get_parent_index (bone);
	while (p >= 0 && depth++ < 256)
	{
		LrgBone *pb = lrg_skeleton_get_bone (self->skeleton, p);
		gint     found = lrg_physics_asset_find_part (asset, lrg_bone_get_name (pb));

		if (found >= 0)
			return found;
		p = lrg_bone_get_parent_index (pb);
	}
	return -1;
}

/* Whether the part has a joint (a part without an ancestor part has none). */
static gboolean
part_has_joint (LrgEditorPhysicsAssetEditor *self,
                guint                        index)
{
	/* without a skeleton every part might have a parent: show the joint */
	return self->skeleton == NULL || parent_part (self, self->asset, index) >= 0;
}

static gfloat
total_mass (LrgPhysicsAsset *asset)
{
	gfloat sum = 0.0f;
	guint  i;

	for (i = 0; asset != NULL && i < lrg_physics_asset_get_part_count (asset); i++)
		sum += lrg_physics_asset_get_part (asset, i)->mass;
	return sum;
}

/* The canonical YAML of one part (the writer's form, defaults left out). */
static gchar *
part_yaml (const LrgPhysicsAssetPart  *part,
           GError                    **error)
{
	g_autoptr(LrgPhysicsAsset)   tmp = lrg_physics_asset_new ();
	g_autoptr(LrgEditorDocument) tdoc = NULL;
	g_autofree gchar            *text = NULL;

	if (lrg_physics_asset_add_part (tmp, part, error) < 0)
		return NULL;
	text = lrg_physics_asset_save_to_data (tmp, NULL, error);
	if (text == NULL)
		return NULL;
	tdoc = lrg_editor_document_new_from_data (text, -1, LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET, error);
	if (tdoc == NULL)
		return NULL;
	return lrg_editor_document_get_yaml (tdoc, "/parts/0");
}

/*
 * commit_asset:
 * Writes @asset as the whole document in one undo group labelled @label.
 * Nothing is written when the text would not change.
 */
static gboolean
commit_asset (LrgEditorPhysicsAssetEditor  *self,
              LrgPhysicsAsset              *asset,
              const gchar                  *label,
              GError                      **error)
{
	g_autofree gchar *text = lrg_physics_asset_save_to_data (asset, NULL, error);
	g_autofree gchar *now = NULL;
	gboolean          ok;

	if (text == NULL)
		return FALSE;
	now = lrg_editor_document_to_data (doc_of (self), NULL);
	if (g_strcmp0 (now, text) == 0)
		return TRUE;
	lrg_editor_document_begin_group (doc_of (self), label);
	ok = lrg_editor_document_replace_gesture (doc_of (self), "", text, 0, error);
	lrg_editor_document_end_group (doc_of (self));
	return ok;
}

/*
 * commit_part:
 * Validates @part as part @index of the current asset, then rewrites
 * `/parts/<index>`. Inside a grid gesture the rewrites merge into one undo
 * step; otherwise each is a group of its own.
 */
static gboolean
commit_part (LrgEditorPhysicsAssetEditor  *self,
             guint                         index,
             const LrgPhysicsAssetPart    *part,
             guint                         gesture,
             GError                      **error)
{
	g_autoptr(LrgPhysicsAsset) copy = parse_doc (self, error);
	g_autofree gchar          *yaml = NULL;
	g_autofree gchar          *path = NULL;
	g_autofree gchar          *now = NULL;
	gboolean                   ok;

	if (copy == NULL)
		return FALSE;
	if (index >= lrg_physics_asset_get_part_count (copy))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No part %u", index);
		return FALSE;
	}
	/* the asset's own validation: sizes, mass, limits, unique bone */
	if (!lrg_physics_asset_set_part (copy, index, part, error))
		return FALSE;
	yaml = part_yaml (part, error);
	if (yaml == NULL)
		return FALSE;
	path = g_strdup_printf ("/parts/%u", index);
	now = lrg_editor_document_get_yaml (doc_of (self), path);
	if (g_strcmp0 (now, yaml) == 0)
		return TRUE;
	if (gesture != 0)
		return lrg_editor_document_replace_gesture (doc_of (self), path, yaml, gesture, error);
	{
		g_autofree gchar *label = g_strdup_printf ("Edit Part %s", part->bone);

		lrg_editor_document_begin_group (doc_of (self), label);
		ok = lrg_editor_document_replace_gesture (doc_of (self), path, yaml, 0, error);
		lrg_editor_document_end_group (doc_of (self));
	}
	return ok;
}

/* ==========================================================================
 * The preview model
 * ========================================================================== */

static void
clear_model (LrgEditorPhysicsAssetEditor *self)
{
	stop_simulation (self);
	if (self->mesh != NULL && self->uploaded && IsWindowReady ())
		lrg_skinned_mesh_unload (self->mesh);
	g_clear_object (&self->mesh);
	g_clear_object (&self->skeleton);
	g_clear_pointer (&self->model, g_free);
	self->uploaded = FALSE;
	self->upload_failed = FALSE;
}

/* The bounds the camera frames: bind positions, else the bones. */
static void
compute_bounds (LrgEditorPhysicsAssetEditor *self)
{
	const gfloat *pos = NULL;
	guint         n = 0, i, k;

	for (k = 0; k < 3; k++)
	{
		self->bmin[k] = G_MAXFLOAT;
		self->bmax[k] = -G_MAXFLOAT;
	}
	if (self->mesh != NULL)
	{
		pos = lrg_skinned_mesh_get_bind_positions (self->mesh);
		n = lrg_skinned_mesh_get_vertex_count (self->mesh);
	}
	for (i = 0; pos != NULL && i < n; i++)
		for (k = 0; k < 3; k++)
		{
			self->bmin[k] = MIN (self->bmin[k], pos[3 * i + k]);
			self->bmax[k] = MAX (self->bmax[k], pos[3 * i + k]);
		}
	for (i = 0; self->skeleton != NULL && i < lrg_skeleton_get_bone_count (self->skeleton); i++)
	{
		const LrgBonePose *w = lrg_bone_get_world_pose (lrg_skeleton_get_bone (self->skeleton, (gint) i));
		gfloat             p[3] = { w->position_x, w->position_y, w->position_z };

		for (k = 0; k < 3; k++)
		{
			self->bmin[k] = MIN (self->bmin[k], p[k]);
			self->bmax[k] = MAX (self->bmax[k], p[k]);
		}
	}
	if (self->bmin[0] > self->bmax[0])
	{
		for (k = 0; k < 3; k++)
		{
			self->bmin[k] = -1.0f;
			self->bmax[k] = 1.0f;
		}
		self->bmin[1] = 0.0f;
		self->bmax[1] = 2.0f;
	}
}

/* Puts the skeleton (and the mesh) back in the bind pose. */
static void
pose_bind (LrgEditorPhysicsAssetEditor *self)
{
	if (self->skeleton == NULL)
		return;
	lrg_skeleton_reset_to_bind (self->skeleton);
	lrg_skeleton_calculate_world_poses (self->skeleton);
	if (self->mesh != NULL)
		lrg_skinned_mesh_apply_skeleton (self->mesh, self->skeleton);
}

/* Loads @abs (a skinned glTF) as the preview model; NULL clears. */
static gboolean
load_model (LrgEditorPhysicsAssetEditor  *self,
            const gchar                  *abs,
            GError                      **error)
{
	g_autoptr(LrgGltfAnimation) gltf = NULL;
	g_autoptr(LrgSkeleton)      skeleton = NULL;
	g_autoptr(LrgSkinnedMesh)   mesh = NULL;

	if (abs != NULL && g_strcmp0 (abs, self->model) == 0 && self->skeleton != NULL)
		return TRUE;
	if (abs == NULL)
	{
		clear_model (self);
		compute_bounds (self);
		return TRUE;
	}
	gltf = lrg_gltf_animation_new_from_file (abs, error);
	if (gltf == NULL)
		return FALSE;
	if (lrg_gltf_animation_get_skin_count (gltf) == 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s has no skeleton (no skin)", abs);
		return FALSE;
	}
	skeleton = lrg_gltf_animation_create_skeleton (gltf, 0, error);
	if (skeleton == NULL)
		return FALSE;
	mesh = lrg_gltf_animation_create_skinned_mesh (gltf, 0, NULL);
	clear_model (self);
	self->model = g_strdup (abs);
	self->skeleton = g_steal_pointer (&skeleton);
	self->mesh = g_steal_pointer (&mesh);
	if (self->mesh != NULL)
		lrg_skinned_mesh_set_tint (self->mesh, 150, 156, 172, 255);
	pose_bind (self);
	compute_bounds (self);
	return TRUE;
}

/* The `.meta` sidecar's preview model, absolute, or NULL. */
static gchar *
meta_model (LrgEditorPhysicsAssetEditor *self)
{
	const gchar            *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autofree gchar       *rel = NULL;
	g_autofree gchar       *dir = NULL;

	if (path == NULL)
		return NULL;
	meta = lrg_asset_meta_load_for_asset (path, NULL);
	if (meta == NULL)
		return NULL;
	rel = lrg_asset_meta_get_setting_string (meta, META_MODEL_KEY);
	if (rel == NULL || *rel == '\0')
		return NULL;
	if (g_path_is_absolute (rel))
		return g_canonicalize_filename (rel, NULL);
	dir = g_path_get_dirname (path);
	return g_canonicalize_filename (rel, dir);
}

/* Writes (or removes) the sidecar's preview model. */
static gboolean
write_meta_model (LrgEditorPhysicsAssetEditor  *self,
                  const gchar                  *abs,
                  GError                      **error)
{
	const gchar            *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autofree gchar       *meta_path = NULL;

	if (path == NULL)
		return TRUE;      /* untitled: kept in memory only */
	meta = lrg_asset_meta_load_for_asset (path, NULL);
	if (meta == NULL)
		meta = lrg_asset_meta_new_for_asset (path);
	if (abs == NULL)
		lrg_asset_meta_remove_setting (meta, META_MODEL_KEY);
	else
	{
		g_autofree gchar *dir = g_path_get_dirname (path);
		g_autofree gchar *rel = editor_mat_relative_to (dir, abs);

		lrg_asset_meta_set_setting_string (meta, META_MODEL_KEY, rel);
	}
	meta_path = lrg_asset_meta_path_for_asset (path);
	return lrg_asset_meta_save_to_file (meta, meta_path, error);
}

/* Whether @skeleton has every bone the asset names. */
static gboolean
skeleton_fits (LrgSkeleton     *skeleton,
               LrgPhysicsAsset *asset)
{
	guint i;

	if (asset == NULL || lrg_physics_asset_get_part_count (asset) == 0)
		return FALSE;
	for (i = 0; i < lrg_physics_asset_get_part_count (asset); i++)
		if (lrg_skeleton_get_bone_by_name (skeleton, lrg_physics_asset_get_part (asset, i)->bone) == NULL)
			return FALSE;
	return TRUE;
}

/*
 * sync_model:
 * The sidecar's model; without one, the first project model (of at most
 * 32 looked at) whose skeleton has every bone the asset names.
 */
static void
sync_model (LrgEditorPhysicsAssetEditor *self)
{
	g_autofree gchar     *abs = meta_model (self);
	g_autoptr(GPtrArray)  models = NULL;
	guint                 i;

	if (abs != NULL)
	{
		g_autoptr(GError) error = NULL;

		if (!load_model (self, abs, &error))
			g_debug ("physics asset preview: %s", error->message);
		return;
	}
	if (self->skeleton != NULL || self->asset == NULL || app_of (self) == NULL)
		return;
	models = editor_anim_list_models (app_of (self));
	for (i = 0; i < models->len && i < 32; i++)
	{
		g_autofree gchar      *p = abs_path (app_of (self), g_ptr_array_index (models, i));
		g_autoptr(LrgSkeleton) skel = lrg_gltf_load_skeleton (p, 0, NULL);

		if (skel != NULL && skeleton_fits (skel, self->asset))
		{
			load_model (self, p, NULL);
			return;
		}
	}
}

/* ==========================================================================
 * Simulation
 * ========================================================================== */

static void
stop_simulation (LrgEditorPhysicsAssetEditor *self)
{
	if (self->ragdoll != NULL)
		lrg_ragdoll_remove_from_world (self->ragdoll);
	g_clear_object (&self->ragdoll);
	g_clear_object (&self->world);
	self->sim_time = 0.0f;
	pose_bind (self);
	if (self->sim_button != NULL)
		lrg_button_set_text (self->sim_button, "Simulate");
}

static gboolean
start_simulation (LrgEditorPhysicsAssetEditor  *self,
                  GError                      **error)
{
	EditorPhysicsSettings       settings;
	g_autoptr(LrgDynamicsWorld) world = NULL;
	g_autoptr(LrgRagdoll)       ragdoll = NULL;
	g_autoptr(LrgPhysicsShape)  plane = NULL;
	gfloat                      n[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                      o[3] = { 0.0f, 0.0f, 0.0f };
	guint                       ground;

	stop_simulation (self);
	if (self->asset == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s",
		             self->load_error != NULL ? self->load_error : "The asset does not load");
		return FALSE;
	}
	if (self->skeleton == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Pick a preview model first");
		return FALSE;
	}
	if (lrg_physics_asset_get_part_count (self->asset) == 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "The asset has no parts");
		return FALSE;
	}
	if (!lrg_physics_asset_validate (self->asset, self->skeleton, error))
		return FALSE;

	/* the project's world settings */
	editor_physics_settings_load (app_of (self), &settings);
	world = lrg_dynamics_world_new ();
	if (!lrg_dynamics_world_set_gravity (world, settings.gravity, error) ||
	    !lrg_dynamics_world_set_step_limits (world, settings.fixed_dt, settings.max_substeps, error) ||
	    !lrg_dynamics_world_set_sleep (world, settings.sleep_threshold, settings.sleep_substeps, error) ||
	    !lrg_dynamics_world_set_material (world, 0.0f, 0.8f, error))
		return FALSE;
	lrg_dynamics_world_set_iterations (world, settings.iterations);

	/* a ground plane under the model's feet */
	o[1] = self->bmin[1];
	plane = lrg_physics_shape_new_plane (n, 0.0f);
	if (!lrg_dynamics_world_add_body (world, plane, LRG_DYNAMICS_BODY_STATIC, 0.0f, o, NULL, &ground, error))
		return FALSE;

	pose_bind (self);
	ragdoll = lrg_ragdoll_new (self->asset, self->skeleton, error);
	if (ragdoll == NULL || !lrg_ragdoll_add_to_world (ragdoll, world, error))
		return FALSE;
	lrg_ragdoll_set_mode (ragdoll, LRG_RAGDOLL_PASSIVE);
	self->world = g_steal_pointer (&world);
	self->ragdoll = g_steal_pointer (&ragdoll);
	self->sim_time = 0.0f;
	if (self->sim_button != NULL)
		lrg_button_set_text (self->sim_button, "Stop");
	return TRUE;
}

/* One frame of the simulation: step, write the bones, skin the mesh. */
static void
sim_step (LrgEditorPhysicsAssetEditor *self,
          gfloat                       dt)
{
	g_autoptr(GError) error = NULL;

	if (self->world == NULL || self->ragdoll == NULL || dt <= 0.0f)
		return;
	if (!lrg_dynamics_world_step (self->world, MIN (dt, 0.1f), NULL, &error))
	{
		g_warning ("physics asset preview: %s", error->message);
		stop_simulation (self);
		return;
	}
	self->sim_time += dt;
	lrg_ragdoll_write_pose (self->ragdoll);
	if (self->mesh != NULL)
		lrg_skinned_mesh_apply_skeleton (self->mesh, self->skeleton);
}

/* ==========================================================================
 * Drawing the parts and joints
 * ========================================================================== */

/* The world frame of a bone (scale ignored). */
static gboolean
bone_frame (LrgEditorPhysicsAssetEditor *self,
            const gchar                 *bone,
            Xf                          *out)
{
	LrgBone           *b = self->skeleton != NULL ? lrg_skeleton_get_bone_by_name (self->skeleton, bone) : NULL;
	const LrgBonePose *w;

	if (b == NULL)
		return FALSE;
	w = lrg_bone_get_world_pose (b);
	out->p[0] = w->position_x;
	out->p[1] = w->position_y;
	out->p[2] = w->position_z;
	out->q[0] = w->rotation_x;
	out->q[1] = w->rotation_y;
	out->q[2] = w->rotation_z;
	out->q[3] = w->rotation_w;
	return TRUE;
}

static void
draw_part (LrgEditorPhysicsAssetEditor *self,
           const LrgPhysicsAssetPart   *part,
           gboolean                     selected)
{
	GrlColor color = selected ? (GrlColor) { 255, 176, 64, 255 } : (GrlColor) { 96, 214, 232, 220 };
	Xf       bone, f;

	if (!bone_frame (self, part->bone, &bone))
		return;
	xf_apply (&bone, part->offset, part->rotation, &f);
	switch (part->shape)
	{
	case LRG_PHYSICS_ASSET_SHAPE_SPHERE:
		{
			GrlVector3 c = xf_point (&f, 0, 0, 0);

			lrg_debug_draw_sphere (self->draw, &c, part->radius, &color, 0.0f);
		}
		break;
	case LRG_PHYSICS_ASSET_SHAPE_BOX:
		{
			gfloat     m[16];
			GrlVector3 he = { part->half_extents[0], part->half_extents[1], part->half_extents[2] };

			xf_matrix (&f, m);
			lrg_debug_draw_oriented_box (self->draw, m, &he, &color, 0.0f);
		}
		break;
	case LRG_PHYSICS_ASSET_SHAPE_CAPSULE:
	default:
		{
			GrlVector3 a = xf_point (&f, 0, part->half_height, 0);
			GrlVector3 b = xf_point (&f, 0, -part->half_height, 0);

			lrg_debug_draw_capsule (self->draw, &a, &b, part->radius, &color, 0.0f);
		}
		break;
	}
}

/* The joint frame of the selected part with its limits: an arc for the
 * twist range about X, an elliptic cone for the swing half angles. */
static void
draw_joint (LrgEditorPhysicsAssetEditor *self,
            const LrgPhysicsAssetPart   *part,
            gfloat                       size)
{
	static const GrlColor red = { 240, 84, 84, 255 };
	static const GrlColor green = { 110, 220, 110, 255 };
	static const GrlColor blue = { 90, 150, 255, 255 };
	static const GrlColor twist_c = { 255, 214, 90, 255 };
	static const GrlColor swing_c = { 200, 120, 255, 255 };
	GrlVector3            pts[49];
	GrlVector3            c;
	Xf                    bone, f;
	guint                 i, n;

	if (!bone_frame (self, part->bone, &bone))
		return;
	xf_apply (&bone, part->joint_position, part->joint_rotation, &f);
	c = xf_point (&f, 0, 0, 0);
	{
		GrlVector3 x = xf_point (&f, size * 1.2f, 0, 0);
		GrlVector3 y = xf_point (&f, 0, size * 0.5f, 0);
		GrlVector3 z = xf_point (&f, 0, 0, size * 0.5f);

		lrg_debug_draw_arrow (self->draw, &c, &x, size * 0.15f, &red, 0.0f);
		lrg_debug_draw_line (self->draw, &c, &y, &green, 0.0f);
		lrg_debug_draw_line (self->draw, &c, &z, &blue, 0.0f);
	}
	if (part->joint == LRG_JOINT_FIXED || part->joint == LRG_JOINT_POINT)
		return;

	/* twist: an arc in the YZ plane from min to max, with spokes */
	if (joint_has_twist (part->joint))
	{
		gfloat r = size * 0.8f;

		n = 32;
		for (i = 0; i <= n; i++)
		{
			gfloat a = part->twist_min + (part->twist_max - part->twist_min) * (gfloat) i / (gfloat) n;

			pts[i] = xf_point (&f, 0, r * cosf (a), r * sinf (a));
		}
		lrg_debug_draw_polyline (self->draw, pts, n + 1, FALSE, &twist_c, 0.0f);
		lrg_debug_draw_line (self->draw, &c, &pts[0], &twist_c, 0.0f);
		lrg_debug_draw_line (self->draw, &c, &pts[n], &twist_c, 0.0f);
	}

	/* swing: the cone of directions X may take; the half angle at angle
	 * t around X follows the ellipse of swing_y (toward Z) and swing_z
	 * (toward Y) */
	if (joint_has_swing (part->joint))
	{
		gfloat sy = MAX (part->swing_y, 1e-3f);
		gfloat sz = MAX (part->swing_z, 1e-3f);
		gfloat len = size * 1.1f;

		n = 48;
		for (i = 0; i < n; i++)
		{
			gfloat t = 2.0f * (gfloat) G_PI * (gfloat) i / (gfloat) n;
			gfloat ct = cosf (t), st = sinf (t);
			gfloat half = 1.0f / sqrtf ((ct / sy) * (ct / sy) + (st / sz) * (st / sz));

			pts[i] = xf_point (&f, len * cosf (half), len * sinf (half) * st, len * sinf (half) * ct);
		}
		lrg_debug_draw_polyline (self->draw, pts, n, TRUE, &swing_c, 0.0f);
		for (i = 0; i < n; i += n / 4)
			lrg_debug_draw_line (self->draw, &c, &pts[i], &swing_c, 0.0f);
	}
}

/* Records this frame's overlay into self->draw. */
static void
record_overlay (LrgEditorPhysicsAssetEditor *self,
                gfloat                       radius)
{
	guint i, n;

	lrg_debug_draw_set_depth_test (self->draw, FALSE);
	if (self->world != NULL && self->show_shapes)
		lrg_dynamics_world_debug_draw (self->world, self->draw,
		                               LRG_DYNAMICS_DEBUG_SHAPES | LRG_DYNAMICS_DEBUG_SLEEPING |
		                               LRG_DYNAMICS_DEBUG_CONTACTS);
	if (self->asset == NULL || self->skeleton == NULL)
		return;
	n = lrg_physics_asset_get_part_count (self->asset);
	if (self->world == NULL && self->show_shapes)
		for (i = 0; i < n; i++)
			draw_part (self, lrg_physics_asset_get_part (self->asset, i), (gint) i == self->selected);
	if (self->show_joints && self->selected >= 0 && (guint) self->selected < n &&
	    part_has_joint (self, (guint) self->selected))
		draw_joint (self, lrg_physics_asset_get_part (self->asset, (guint) self->selected), radius * 0.22f);
}

/* ==========================================================================
 * EditorPhysView
 * ========================================================================== */

static void
view_render (LrgViewport      *viewport,
             GrlRenderTexture *target,
             gfloat            width,
             gfloat            height,
             gpointer          user_data)
{
	EditorPhysView              *view = EDITOR_PHYS_VIEW (user_data);
	LrgEditorPhysicsAssetEditor *self = view->editor;
	Camera3D                     cam;
	gfloat                       c[3], radius, dist, yaw, pitch;
	guint                        k;

	ClearBackground ((Color) { 26, 28, 38, 255 });
	if (self == NULL)
		return;
	for (k = 0; k < 3; k++)
		c[k] = 0.5f * (self->bmin[k] + self->bmax[k]);
	radius = 0.5f * sqrtf ((self->bmax[0] - self->bmin[0]) * (self->bmax[0] - self->bmin[0]) +
	                       (self->bmax[1] - self->bmin[1]) * (self->bmax[1] - self->bmin[1]) +
	                       (self->bmax[2] - self->bmin[2]) * (self->bmax[2] - self->bmin[2]));
	radius = MAX (radius, 0.05f);
	dist = radius / tanf (0.5f * VIEW_FOV * (gfloat) G_PI / 180.0f) * 1.15f * view->zoom;
	if (width < height && width > 0.0f)
		dist *= height / width;
	yaw = view->yaw;
	pitch = view->pitch;
	memset (&cam, 0, sizeof cam);
	cam.target = (Vector3) { c[0], c[1], c[2] };
	cam.position = (Vector3) { c[0] + dist * cosf (pitch) * sinf (yaw), c[1] + dist * sinf (pitch),
	                           c[2] + dist * cosf (pitch) * cosf (yaw) };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = VIEW_FOV;
	cam.projection = CAMERA_PERSPECTIVE;

	BeginMode3D (cam);
	rlPushMatrix ();
	rlTranslatef (c[0], self->bmin[1], c[2]);
	DrawGrid (16, MAX (radius / 4.0f, 0.05f));
	rlPopMatrix ();

	/* the mesh (uploaded on first use: needs the context) */
	if (self->mesh != NULL && !self->uploaded && !self->upload_failed)
	{
		g_autoptr(GError) error = NULL;

		self->uploaded = lrg_skinned_mesh_upload (self->mesh, &error);
		if (!self->uploaded)
		{
			self->upload_failed = TRUE;
			g_warning ("physics asset preview: cannot upload %s: %s", self->model, error->message);
		}
	}
	if (self->show_mesh && self->mesh != NULL && self->uploaded)
		lrg_skinned_mesh_draw (self->mesh, NULL);
	rlDrawRenderBatchActive ();

	/* the parts, the joint gizmos and the bodies on top */
	record_overlay (self, radius);
	lrg_debug_draw_draw_3d (self->draw);
	EndMode3D ();
	lrg_debug_draw_clear (self->draw);
}

/* The caption: model, part count, simulation time over a soft scrim. */
static void
view_caption_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	EditorPhysView              *view = EDITOR_PHYS_VIEW (user_data);
	LrgEditorPhysicsAssetEditor *self = view->editor;
	LrgTheme                    *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                       fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont                     *font = lrg_theme_get_ui_font (theme);
	const GrlColor               scrim = { 0, 0, 0, 110 };
	const GrlColor               ink = { 236, 238, 246, 255 };
	g_autofree gchar            *base = NULL;
	g_autofree gchar            *text = NULL;
	gfloat                       tw = 0.0f;

	if (self == NULL)
		return;
	if (self->model == NULL)
		text = g_strdup ("No preview model: pick a skinned .glb");
	else
	{
		base = g_path_get_basename (self->model);
		if (self->world != NULL)
			text = g_strdup_printf ("%s  \xc2\xb7  simulating %.1f s", base, self->sim_time);
		else
			text = g_strdup_printf ("%s  \xc2\xb7  %u bones", base, lrg_skeleton_get_bone_count (self->skeleton));
	}
	lrg_paint_measure_text (font, text, fs, 0.0f, &tw, NULL);
	tw = MIN (tw, width - 20.0f);
	lrg_paint_list_rect (list, x, y, tw + 20.0f, fs + 12.0f, (fs + 12.0f) * 0.5f, &scrim);
	lrg_paint_list_push_clip (list, x, y, tw + 20.0f, fs + 12.0f);
	lrg_paint_list_text (list, font, text, x + 10.0f, y + 6.0f, fs, 0.0f, &ink);
	lrg_paint_list_pop_clip (list);
}

/* A glass pill behind the chip bar. */
static void
view_pill_paint (LrgCustomWidget *widget,
                 LrgPaintList    *list,
                 gfloat           x,
                 gfloat           y,
                 gfloat           width,
                 gfloat           height,
                 gpointer         user_data)
{
	LrgTheme      *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	const GrlColor shadow = { 0, 0, 0, 90 };
	GrlColor       pill;

	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 0.82f, &pill);
	lrg_paint_list_shadow (list, x, y, width, height, height * 0.5f, 12.0f, 0.0f, 0.0f, 3.0f, &shadow);
	lrg_paint_list_rect (list, x, y, width, height, height * 0.5f, &pill);
	lrg_paint_list_border (list, x, y, width, height, height * 0.5f, 1.0f,
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
}

static void
view_sync_chips (EditorPhysView *view)
{
	LrgEditorPhysicsAssetEditor *self = view->editor;

	if (self == NULL)
		return;
	editor_chip_set_checked (view->mesh_chip, self->show_mesh);
	editor_chip_set_checked (view->shapes_chip, self->show_shapes);
	editor_chip_set_checked (view->joints_chip, self->show_joints);
}

static void
on_chip (LrgWidget *chip,
         gpointer   user_data)
{
	EditorPhysView              *view = EDITOR_PHYS_VIEW (user_data);
	LrgEditorPhysicsAssetEditor *self = view->editor;

	if (self == NULL)
		return;
	if (chip == view->mesh_chip)
		self->show_mesh = !self->show_mesh;
	else if (chip == view->shapes_chip)
		self->show_shapes = !self->show_shapes;
	else
		self->show_joints = !self->show_joints;
	view_sync_chips (view);
}

static void
editor_phys_view_layout_children (LrgContainer *container)
{
	EditorPhysView *view = EDITOR_PHYS_VIEW (container);
	gfloat          w = lrg_widget_get_width (LRG_WIDGET (view));
	gfloat          h = lrg_widget_get_height (LRG_WIDGET (view));
	gfloat          bw = 0.0f, bh = 0.0f;
	gfloat          inset = 10.0f;
	gboolean        compact;

	if (view->viewport == NULL || view->caption == NULL || view->bar == NULL)
		return;
	lrg_widget_place_in_slot (LRG_WIDGET (view->viewport), 0.0f, 0.0f, w, h);
	lrg_widget_place_in_slot (view->caption, inset, inset, MAX (w - 2.0f * inset, 0.0f), 28.0f);
	editor_chip_set_compact (view->mesh_chip, FALSE);
	editor_chip_set_compact (view->shapes_chip, FALSE);
	editor_chip_set_compact (view->joints_chip, FALSE);
	lrg_widget_get_preferred_size (view->bar, &bw, &bh);
	compact = bw > w - 2.0f * inset - 10.0f;
	editor_chip_set_compact (view->mesh_chip, compact);
	editor_chip_set_compact (view->shapes_chip, compact);
	editor_chip_set_compact (view->joints_chip, compact);
	lrg_widget_get_preferred_size (view->bar, &bw, &bh);
	bw = MIN (bw, MAX (w - 2.0f * inset, 0.0f));
	lrg_widget_place_in_slot (view->pill, (w - bw) * 0.5f - 5.0f, MAX (h - bh - inset, 0.0f) - 5.0f, bw + 10.0f,
	                          bh + 10.0f);
	lrg_widget_place_in_slot (view->bar, (w - bw) * 0.5f, MAX (h - bh - inset, 0.0f), bw, bh);
}

static void
editor_phys_view_measure (LrgWidget *widget,
                          gfloat    *width,
                          gfloat    *height)
{
	*width = 280.0f;
	*height = 260.0f;
}

/* Drag to orbit, wheel to zoom. */
static gboolean
editor_phys_view_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
	EditorPhysView *view = EDITOR_PHYS_VIEW (widget);
	gfloat          x = lrg_ui_event_get_x (event);
	gfloat          y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		view->dragging = TRUE;
		view->last_x = x;
		view->last_y = y;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (!view->dragging)
			return FALSE;
		view->yaw -= (x - view->last_x) * 0.012f;
		view->pitch = CLAMP (view->pitch + (y - view->last_y) * 0.010f, -1.35f, 1.35f);
		view->last_x = x;
		view->last_y = y;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (!view->dragging)
			return FALSE;
		view->dragging = FALSE;
		return TRUE;
	case LRG_UI_EVENT_SCROLL:
		view->zoom = CLAMP (view->zoom * (lrg_ui_event_get_scroll_y (event) > 0.0f ? 0.9f : 1.1f), 0.25f, 4.0f);
		return TRUE;
	default:
		return FALSE;
	}
}

static void
editor_phys_view_dispose (GObject *object)
{
	EditorPhysView *view = EDITOR_PHYS_VIEW (object);

	if (view->caption != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (view->caption), NULL, NULL, NULL);
	if (view->pill != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (view->pill), NULL, NULL, NULL);
	if (view->viewport != NULL)
		lrg_viewport_set_render_func (view->viewport, NULL, NULL, NULL);
	g_clear_weak_pointer (&view->editor);
	G_OBJECT_CLASS (editor_phys_view_parent_class)->dispose (object);
}

static void
editor_phys_view_class_init (EditorPhysViewClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = editor_phys_view_dispose;
	LRG_WIDGET_CLASS (klass)->measure = editor_phys_view_measure;
	LRG_WIDGET_CLASS (klass)->handle_event = editor_phys_view_handle_event;
	LRG_CONTAINER_CLASS (klass)->layout_children = editor_phys_view_layout_children;
}

static void
editor_phys_view_init (EditorPhysView *view)
{
	view->yaw = 0.6f;
	view->pitch = 0.25f;
	view->zoom = 1.0f;
}

static EditorPhysView *
editor_phys_view_new (LrgEditorPhysicsAssetEditor *editor)
{
	static const LrgCustomWidgetFuncs caption_funcs = { NULL, view_caption_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs pill_funcs = { NULL, view_pill_paint, NULL, NULL, { NULL } };
	EditorPhysView *view = g_object_new (EDITOR_TYPE_PHYS_VIEW, NULL);

	g_set_weak_pointer (&view->editor, editor);
	lrg_widget_set_name (LRG_WIDGET (view), "physasset-preview");

	view->viewport = lrg_viewport_new ();
	lrg_widget_set_name (LRG_WIDGET (view->viewport), "physasset-preview-viewport");
	lrg_viewport_set_auto_clear (view->viewport, FALSE);
	lrg_viewport_set_continuous (view->viewport, TRUE);
	lrg_viewport_set_render_func (view->viewport, view_render, view, NULL);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (view->viewport), TRUE);
	lrg_container_add_child (LRG_CONTAINER (view), LRG_WIDGET (view->viewport));
	g_object_unref (view->viewport);

	view->caption = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (view->caption), &caption_funcs, view, NULL);
	lrg_widget_set_pointer_transparent (view->caption, TRUE);
	lrg_container_add_child (LRG_CONTAINER (view), view->caption);
	g_object_unref (view->caption);

	view->pill = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (view->pill), &pill_funcs, view, NULL);
	lrg_widget_set_pointer_transparent (view->pill, TRUE);
	lrg_container_add_child (LRG_CONTAINER (view), view->pill);
	g_object_unref (view->pill);

	view->bar = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (view->bar), 2.0f);
	view->mesh_chip = editor_chip_new ("physasset-show-mesh", LRG_ICON_USER, "Mesh", on_chip, view);
	editor_chip_set_tint (view->mesh_chip, LRG_THEME_COLOR_ACCENT);
	lrg_container_add_child (LRG_CONTAINER (view->bar), view->mesh_chip);
	g_object_unref (view->mesh_chip);
	view->shapes_chip = editor_chip_new ("physasset-show-shapes", LRG_ICON_EYE, "Shapes", on_chip, view);
	editor_chip_set_tint (view->shapes_chip, LRG_THEME_COLOR_LINK);
	lrg_container_add_child (LRG_CONTAINER (view->bar), view->shapes_chip);
	g_object_unref (view->shapes_chip);
	view->joints_chip = editor_chip_new ("physasset-show-joints", LRG_ICON_REFRESH, "Joints", on_chip, view);
	editor_chip_set_tint (view->joints_chip, LRG_THEME_COLOR_WARNING);
	lrg_container_add_child (LRG_CONTAINER (view->bar), view->joints_chip);
	g_object_unref (view->joints_chip);
	lrg_container_add_child (LRG_CONTAINER (view), view->bar);
	g_object_unref (view->bar);
	return view;
}

/* ==========================================================================
 * Parts list, add-part picker, exclusions
 * ========================================================================== */

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static LrgWidget *
heading (const gchar *text)
{
	LrgLabel *l = lrg_label_new (text);

	lrg_widget_add_class (LRG_WIDGET (l), "dim-label");
	return LRG_WIDGET (l);
}

static void
on_part_clicked (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);

	lrg_editor_physics_asset_editor_select_part (self,
	                                             GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index")));
}

static void
on_exclusion_remove (LrgButton *button,
                     gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	g_autoptr(GError)            error = NULL;

	if (!lrg_editor_physics_asset_editor_set_exclusion (self, g_object_get_data (G_OBJECT (button), "a"),
	                                                     g_object_get_data (G_OBJECT (button), "b"), FALSE, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* A signature of what the lists show, so they rebuild only on change. */
static gchar *
parts_signature (LrgEditorPhysicsAssetEditor *self)
{
	GString *s = g_string_new (self->load_error);
	guint    i;

	g_string_append_printf (s, "|%s|%d|", self->model, self->selected);
	for (i = 0; self->asset != NULL && i < lrg_physics_asset_get_part_count (self->asset); i++)
	{
		const LrgPhysicsAssetPart *p = lrg_physics_asset_get_part (self->asset, i);

		g_string_append_printf (s, "%s:%d:%g,", p->bone, (gint) p->shape, p->mass);
	}
	for (i = 0; self->asset != NULL && i < lrg_physics_asset_get_exclusion_count (self->asset); i++)
	{
		const gchar *a = NULL, *b = NULL;

		lrg_physics_asset_get_exclusion (self->asset, i, &a, &b);
		g_string_append_printf (s, "x%s-%s,", a, b);
	}
	return g_string_free (s, FALSE);
}

static void
rebuild_parts (LrgEditorPhysicsAssetEditor *self)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	guint     i, n;

	self->parts_stale = FALSE;
	g_free (self->parts_sig);
	self->parts_sig = parts_signature (self);
	self->syncing = TRUE;
	lrg_container_remove_all (LRG_CONTAINER (self->parts));
	lrg_container_remove_all (LRG_CONTAINER (self->exclusions));
	if (self->asset == NULL)
	{
		LrgLabel *hint = lrg_label_new (self->load_error != NULL ? self->load_error : "The asset does not load");

		lrg_label_set_color (hint, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		add_to (self->parts, LRG_WIDGET (hint));
		lrg_label_set_text (self->summary, "PARTS");
		self->syncing = FALSE;
		return;
	}
	n = lrg_physics_asset_get_part_count (self->asset);
	{
		g_autofree gchar *sum = g_strdup_printf ("PARTS  \xc2\xb7  %u  \xc2\xb7  %.1f kg", n, total_mass (self->asset));

		lrg_label_set_text (self->summary, sum);
	}
	if (n == 0)
	{
		LrgWidget *hint = heading (self->skeleton != NULL ? "No parts yet: Generate from the skeleton, or add one"
		                                                  : "No parts yet: pick a model, then Generate");

		add_to (self->parts, hint);
	}
	for (i = 0; i < n; i++)
	{
		const LrgPhysicsAssetPart *p = lrg_physics_asset_get_part (self->asset, i);
		LrgWidget                 *row = LRG_WIDGET (lrg_hbox_new ());
		LrgButton                 *pick = lrg_button_new (p->bone);
		g_autofree gchar          *pname = g_strdup_printf ("physasset-part-%s", p->bone);
		g_autofree gchar          *mass = g_strdup_printf ("%.2f kg", p->mass);
		LrgLabel                  *shape = lrg_label_new (shape_nick (p->shape));
		LrgLabel                  *ml = lrg_label_new (mass);
		gboolean                   missing = self->skeleton != NULL &&
		                                     lrg_skeleton_get_bone_by_name (self->skeleton, p->bone) == NULL;

		lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
		lrg_widget_set_name (LRG_WIDGET (pick), pname);
		lrg_widget_add_class (LRG_WIDGET (pick), (gint) i == self->selected ? "primary" : "flat");
		lrg_widget_set_fill_weight (LRG_WIDGET (pick), 1.0f);
		g_object_set_data (G_OBJECT (pick), "index", GINT_TO_POINTER ((gint) i));
		g_signal_connect (pick, "clicked", G_CALLBACK (on_part_clicked), self);
		add_to (row, LRG_WIDGET (pick));
		lrg_label_set_color (shape, lrg_theme_get_color (theme, missing ? LRG_THEME_COLOR_WARNING
		                                                                 : LRG_THEME_COLOR_TEXT_MUTED));
		if (missing)
			lrg_label_set_text (shape, "no bone!");
		lrg_widget_set_min_size (LRG_WIDGET (shape), 70.0f, 0.0f);
		lrg_widget_set_valign (LRG_WIDGET (shape), LRG_ALIGN_CENTER);
		add_to (row, LRG_WIDGET (shape));
		lrg_widget_set_min_size (LRG_WIDGET (ml), 70.0f, 0.0f);
		lrg_widget_set_valign (LRG_WIDGET (ml), LRG_ALIGN_CENTER);
		add_to (row, LRG_WIDGET (ml));
		add_to (self->parts, row);
	}

	/* exclusions */
	for (i = 0; i < lrg_physics_asset_get_exclusion_count (self->asset); i++)
	{
		const gchar      *a = NULL, *b = NULL;
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		g_autofree gchar *text = NULL;
		g_autofree gchar *bname = NULL;
		LrgLabel         *l;
		LrgButton        *rm = lrg_button_new ("Remove");

		lrg_physics_asset_get_exclusion (self->asset, i, &a, &b);
		text = g_strdup_printf ("%s  \xe2\x9f\xb7  %s", a, b);
		bname = g_strdup_printf ("physasset-exclusion-remove-%u", i);
		l = lrg_label_new (text);
		lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
		lrg_widget_set_fill_weight (LRG_WIDGET (l), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (l), LRG_ALIGN_CENTER);
		add_to (row, LRG_WIDGET (l));
		lrg_widget_set_name (LRG_WIDGET (rm), bname);
		lrg_widget_add_class (LRG_WIDGET (rm), "flat");
		g_object_set_data_full (G_OBJECT (rm), "a", g_strdup (a), g_free);
		g_object_set_data_full (G_OBJECT (rm), "b", g_strdup (b), g_free);
		g_signal_connect (rm, "clicked", G_CALLBACK (on_exclusion_remove), self);
		add_to (row, LRG_WIDGET (rm));
		add_to (self->exclusions, row);
	}
	if (lrg_physics_asset_get_exclusion_count (self->asset) == 0)
		add_to (self->exclusions, heading (lrg_physics_asset_get_exclude_adjacent (self->asset)
		                                   ? "None (jointed parts never collide)" : "None"));
	self->syncing = FALSE;
	self->combos_stale = TRUE;
}

/* The model picker, the free-bone picker and the exclusion pickers. */
static void
rebuild_combos (LrgEditorPhysicsAssetEditor *self)
{
	LrgEditorApp *app = app_of (self);
	guint         i;
	gint          sel = 0;

	self->combos_stale = FALSE;
	self->syncing = TRUE;
	g_clear_pointer (&self->model_list, g_ptr_array_unref);
	self->model_list = editor_anim_list_models (app);
	lrg_combo_box_remove_all (self->model_combo);
	lrg_combo_box_append (self->model_combo, "Preview model\xe2\x80\xa6");
	for (i = 0; i < self->model_list->len; i++)
	{
		const gchar      *rel = g_ptr_array_index (self->model_list, i);
		g_autofree gchar *abs = abs_path (app, rel);

		lrg_combo_box_append (self->model_combo, rel);
		if (g_strcmp0 (abs, self->model) == 0)
			sel = (gint) i + 1;
	}
	lrg_combo_box_set_selected (self->model_combo, sel);

	/* bones without a part */
	lrg_combo_box_remove_all (self->add_combo);
	for (i = 0; self->skeleton != NULL && i < lrg_skeleton_get_bone_count (self->skeleton); i++)
	{
		const gchar *name = lrg_bone_get_name (lrg_skeleton_get_bone (self->skeleton, (gint) i));

		if (self->asset == NULL || lrg_physics_asset_find_part (self->asset, name) < 0)
			lrg_combo_box_append (self->add_combo, name);
	}
	if (lrg_combo_box_get_n_items (self->add_combo) == 0)
		lrg_combo_box_append (self->add_combo, self->skeleton != NULL ? "(every bone has a part)" : NONE_LABEL);
	lrg_combo_box_set_selected (self->add_combo, 0);

	/* exclusion pickers: bones with parts */
	lrg_combo_box_remove_all (self->excl_a);
	lrg_combo_box_remove_all (self->excl_b);
	for (i = 0; self->asset != NULL && i < lrg_physics_asset_get_part_count (self->asset); i++)
	{
		lrg_combo_box_append (self->excl_a, lrg_physics_asset_get_part (self->asset, i)->bone);
		lrg_combo_box_append (self->excl_b, lrg_physics_asset_get_part (self->asset, i)->bone);
	}
	lrg_combo_box_set_selected (self->excl_a, 0);
	lrg_combo_box_set_selected (self->excl_b, MIN (1, lrg_combo_box_get_n_items (self->excl_b) - 1));
	self->syncing = FALSE;
}

/* ==========================================================================
 * The property grid: the selected part, its joint, the asset
 * ========================================================================== */

/* The grid's rows depend on the selection, the shape and the joint kind. */
static gchar *
fields_signature (LrgEditorPhysicsAssetEditor *self)
{
	const LrgPhysicsAssetPart *p = NULL;

	if (self->asset != NULL && self->selected >= 0)
		p = lrg_physics_asset_get_part (self->asset, (guint) self->selected);
	if (p == NULL)
		return g_strdup_printf ("none|%d", self->asset != NULL);
	return g_strdup_printf ("%d|%s|%d|%d|%d", self->selected, p->bone, (gint) p->shape, (gint) p->joint,
	                        part_has_joint (self, (guint) self->selected));
}

static void
add_part_field (LrgEditorPhysicsAssetEditor *self,
                const gchar                 *name,
                const gchar                 *key,
                GType                        type,
                const gchar                 *label,
                const gchar                 *group,
                const gchar                 *hint)
{
	g_autofree gchar *path = g_strdup_printf ("/parts/%d/%s", self->selected, key);

	lrg_editor_data_source_add_field (self->source, name, path, type, label, group, hint, NULL);
}

static void
rebuild_fields (LrgEditorPhysicsAssetEditor *self)
{
	const LrgPhysicsAssetPart *p = NULL;

	self->fields_stale = FALSE;
	g_free (self->fields_sig);
	self->fields_sig = fields_signature (self);
	lrg_editor_data_source_clear (self->source);
	if (self->asset != NULL && self->selected >= 0)
		p = lrg_physics_asset_get_part (self->asset, (guint) self->selected);
	if (p != NULL)
	{
		g_autofree gchar *pg = g_strdup_printf ("Part \xc2\xb7 %s", p->bone);
		gint              parent = parent_part (self, self->asset, (guint) self->selected);
		g_autofree gchar *jg = g_strdup_printf ("Joint to %s",
		                                        parent >= 0 ? lrg_physics_asset_get_part (self->asset,
		                                                                                  (guint) parent)->bone
		                                                    : "the parent part");

		add_part_field (self, "bone", "bone", G_TYPE_STRING, "Bone", pg, NULL);
		add_part_field (self, "shape", "shape", G_TYPE_STRING, "Shape", pg, "enum-strings:capsule,box,sphere");
		if (p->shape != LRG_PHYSICS_ASSET_SHAPE_BOX)
			add_part_field (self, "radius", "radius", G_TYPE_DOUBLE, "Radius", pg, "range:0.005:5:0.005");
		if (p->shape == LRG_PHYSICS_ASSET_SHAPE_CAPSULE)
			add_part_field (self, "half-height", "half-height", G_TYPE_DOUBLE, "Half height", pg,
			                "range:0:5:0.005");
		if (p->shape == LRG_PHYSICS_ASSET_SHAPE_BOX)
		{
			add_part_field (self, "extent-x", "half-extents", G_TYPE_DOUBLE, "Half extent X", pg,
			                "range:0.005:5:0.005");
			add_part_field (self, "extent-y", "half-extents", G_TYPE_DOUBLE, "Half extent Y", pg,
			                "range:0.005:5:0.005");
			add_part_field (self, "extent-z", "half-extents", G_TYPE_DOUBLE, "Half extent Z", pg,
			                "range:0.005:5:0.005");
		}
		add_part_field (self, "offset-x", "offset", G_TYPE_DOUBLE, "Offset X", pg, "range:-5:5:0.005");
		add_part_field (self, "offset-y", "offset", G_TYPE_DOUBLE, "Offset Y", pg, "range:-5:5:0.005");
		add_part_field (self, "offset-z", "offset", G_TYPE_DOUBLE, "Offset Z", pg, "range:-5:5:0.005");
		add_part_field (self, "rotation-x", "rotation", G_TYPE_DOUBLE, "Rotation X", pg, "angle");
		add_part_field (self, "rotation-y", "rotation", G_TYPE_DOUBLE, "Rotation Y", pg, "angle");
		add_part_field (self, "rotation-z", "rotation", G_TYPE_DOUBLE, "Rotation Z", pg, "angle");
		add_part_field (self, "mass", "mass", G_TYPE_DOUBLE, "Mass (kg)", pg, "range:0.01:1000:0.1");
		if (part_has_joint (self, (guint) self->selected))
		{
			add_part_field (self, "joint", "joint/kind", G_TYPE_STRING, "Kind", jg,
			                "enum-strings:swing-twist,cone,hinge,point,fixed");
			add_part_field (self, "joint-x", "joint/position", G_TYPE_DOUBLE, "Pivot X", jg, "range:-5:5:0.005");
			add_part_field (self, "joint-y", "joint/position", G_TYPE_DOUBLE, "Pivot Y", jg, "range:-5:5:0.005");
			add_part_field (self, "joint-z", "joint/position", G_TYPE_DOUBLE, "Pivot Z", jg, "range:-5:5:0.005");
			add_part_field (self, "joint-rotation-x", "joint/rotation", G_TYPE_DOUBLE, "Frame X", jg, "angle");
			add_part_field (self, "joint-rotation-y", "joint/rotation", G_TYPE_DOUBLE, "Frame Y", jg, "angle");
			add_part_field (self, "joint-rotation-z", "joint/rotation", G_TYPE_DOUBLE, "Frame Z", jg, "angle");
			if (joint_has_twist (p->joint))
			{
				add_part_field (self, "twist-min", "joint/twist", G_TYPE_DOUBLE, "Twist min", jg, "angle");
				add_part_field (self, "twist-max", "joint/twist", G_TYPE_DOUBLE, "Twist max", jg, "angle");
			}
			if (joint_has_swing (p->joint))
			{
				add_part_field (self, "swing-y", "joint/swing", G_TYPE_DOUBLE, "Swing Y", jg, "angle");
				add_part_field (self, "swing-z", "joint/swing", G_TYPE_DOUBLE, "Swing Z", jg, "angle");
			}
			add_part_field (self, "friction", "joint/friction", G_TYPE_DOUBLE, "Friction (N m)", jg,
			                "range:0:10000:0.1");
			add_part_field (self, "motor-max-torque", "joint/motor-max-torque", G_TYPE_DOUBLE, "Motor torque",
			                jg, "range:0:100000:1");
			add_part_field (self, "motor-frequency", "joint/motor-frequency", G_TYPE_DOUBLE, "Motor Hz", jg,
			                "range:0.1:200:0.5");
			add_part_field (self, "motor-damping", "joint/motor-damping", G_TYPE_DOUBLE, "Motor damping", jg,
			                "range:0:1000:0.1");
		}
	}
	if (self->asset != NULL)
	{
		lrg_editor_data_source_add_field (self->source, "name", "/name", G_TYPE_STRING, "Name", "Asset", NULL, "''");
		lrg_editor_data_source_add_field (self->source, "exclude-adjacent", "/exclude-adjacent", G_TYPE_BOOLEAN,
		                                  "Jointed parts never collide", "Asset", NULL, "true");
	}
	lrg_property_grid_rebuild (self->grid);
}

/* Reads a field from the parsed part (defaults included, angles in radians). */
static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	const LrgPhysicsAssetPart   *p = NULL;
	gfloat                       e[3];

	if (self->asset == NULL)
		return FALSE;
	if (g_strcmp0 (name, "name") == 0)
	{
		g_value_set_string (value, lrg_physics_asset_get_name (self->asset) != NULL
		                           ? lrg_physics_asset_get_name (self->asset) : "");
		return TRUE;
	}
	if (g_strcmp0 (name, "exclude-adjacent") == 0)
	{
		g_value_set_boolean (value, lrg_physics_asset_get_exclude_adjacent (self->asset));
		return TRUE;
	}
	if (self->selected >= 0)
		p = lrg_physics_asset_get_part (self->asset, (guint) self->selected);
	if (p == NULL)
		return FALSE;

#define DBL(n, v) if (g_strcmp0 (name, n) == 0) { g_value_set_double (value, (gdouble) (v)); return TRUE; }
	if (g_strcmp0 (name, "bone") == 0)
	{
		g_value_set_string (value, p->bone);
		return TRUE;
	}
	if (g_strcmp0 (name, "shape") == 0)
	{
		g_value_set_string (value, shape_nick (p->shape));
		return TRUE;
	}
	if (g_strcmp0 (name, "joint") == 0)
	{
		g_value_set_string (value, joint_nick (p->joint));
		return TRUE;
	}
	DBL ("radius", p->radius);
	DBL ("half-height", p->half_height);
	DBL ("extent-x", p->half_extents[0]);
	DBL ("extent-y", p->half_extents[1]);
	DBL ("extent-z", p->half_extents[2]);
	DBL ("offset-x", p->offset[0]);
	DBL ("offset-y", p->offset[1]);
	DBL ("offset-z", p->offset[2]);
	DBL ("mass", p->mass);
	DBL ("joint-x", p->joint_position[0]);
	DBL ("joint-y", p->joint_position[1]);
	DBL ("joint-z", p->joint_position[2]);
	DBL ("twist-min", p->twist_min);
	DBL ("twist-max", p->twist_max);
	DBL ("swing-y", p->swing_y);
	DBL ("swing-z", p->swing_z);
	DBL ("friction", p->friction);
	DBL ("motor-max-torque", p->motor_max_torque);
	DBL ("motor-frequency", p->motor_frequency);
	DBL ("motor-damping", p->motor_damping);
	if (g_str_has_prefix (name, "rotation-"))
	{
		quat_to_euler (p->rotation, e);
		g_value_set_double (value, (gdouble) e[name[9] - 'x']);
		return TRUE;
	}
	if (g_str_has_prefix (name, "joint-rotation-"))
	{
		quat_to_euler (p->joint_rotation, e);
		g_value_set_double (value, (gdouble) e[name[15] - 'x']);
		return TRUE;
	}
#undef DBL
	return FALSE;
}

/*
 * apply_value:
 * Sets field @name of @p to @value. A shape change carries the size over
 * (a capsule becomes the box around it, and so on).
 */
static gboolean
apply_value (LrgPhysicsAssetPart  *p,
             const gchar          *name,
             const GValue         *value,
             GError              **error)
{
	gfloat d = G_VALUE_HOLDS_DOUBLE (value) ? (gfloat) g_value_get_double (value) : 0.0f;
	gfloat e[3];

	if (g_strcmp0 (name, "bone") == 0)
	{
		const gchar *s = g_value_get_string (value);

		if (s == NULL || *s == '\0')
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A part needs a bone");
			return FALSE;
		}
		g_free (p->bone);
		p->bone = g_strdup (s);
	}
	else if (g_strcmp0 (name, "shape") == 0)
	{
		LrgPhysicsAssetShape shape;
		gfloat               r = p->radius, hh = p->half_height;

		if (!shape_from_nick (g_value_get_string (value), &shape))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown shape '%s'",
			             g_value_get_string (value));
			return FALSE;
		}
		if (shape == p->shape)
			return TRUE;
		if (p->shape == LRG_PHYSICS_ASSET_SHAPE_BOX)
		{
			r = MAX (MIN (p->half_extents[0], p->half_extents[2]), 0.005f);
			hh = MAX (p->half_extents[1] - r, 0.0f);
		}
		if (shape == LRG_PHYSICS_ASSET_SHAPE_BOX)
		{
			p->half_extents[0] = r;
			p->half_extents[1] = (p->shape == LRG_PHYSICS_ASSET_SHAPE_CAPSULE) ? hh + r : r;
			p->half_extents[2] = r;
		}
		else
		{
			p->radius = (shape == LRG_PHYSICS_ASSET_SHAPE_SPHERE && p->shape == LRG_PHYSICS_ASSET_SHAPE_CAPSULE)
			            ? r + hh * 0.5f : r;
			p->half_height = hh;
		}
		p->shape = shape;
	}
	else if (g_strcmp0 (name, "joint") == 0)
	{
		if (!joint_from_nick (g_value_get_string (value), &p->joint))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown joint kind '%s'",
			             g_value_get_string (value));
			return FALSE;
		}
	}
	else if (g_strcmp0 (name, "radius") == 0)
		p->radius = d;
	else if (g_strcmp0 (name, "half-height") == 0)
		p->half_height = d;
	else if (g_str_has_prefix (name, "extent-"))
		p->half_extents[name[7] - 'x'] = d;
	else if (g_str_has_prefix (name, "offset-"))
		p->offset[name[7] - 'x'] = d;
	else if (g_str_has_prefix (name, "rotation-"))
	{
		quat_to_euler (p->rotation, e);
		e[name[9] - 'x'] = d;
		quat_from_euler (e[0], e[1], e[2], p->rotation);
	}
	else if (g_str_has_prefix (name, "joint-rotation-"))
	{
		quat_to_euler (p->joint_rotation, e);
		e[name[15] - 'x'] = d;
		quat_from_euler (e[0], e[1], e[2], p->joint_rotation);
	}
	else if (g_strcmp0 (name, "joint-x") == 0 || g_strcmp0 (name, "joint-y") == 0 || g_strcmp0 (name, "joint-z") == 0)
		p->joint_position[name[6] - 'x'] = d;
	else if (g_strcmp0 (name, "mass") == 0)
		p->mass = d;
	else if (g_strcmp0 (name, "twist-min") == 0)
		p->twist_min = d;
	else if (g_strcmp0 (name, "twist-max") == 0)
		p->twist_max = d;
	else if (g_strcmp0 (name, "swing-y") == 0)
		p->swing_y = d;
	else if (g_strcmp0 (name, "swing-z") == 0)
		p->swing_z = d;
	else if (g_strcmp0 (name, "friction") == 0)
		p->friction = d;
	else if (g_strcmp0 (name, "motor-max-torque") == 0)
		p->motor_max_torque = d;
	else if (g_strcmp0 (name, "motor-frequency") == 0)
		p->motor_frequency = d;
	else if (g_strcmp0 (name, "motor-damping") == 0)
		p->motor_damping = d;
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown part field '%s'", name);
		return FALSE;
	}
	return TRUE;
}

/* Every part field goes through the asset (validated, canonical). */
static gboolean
source_write (LrgEditorDataSource *source,
              const gchar         *name,
              GValue              *value,
              gpointer             user_data)
{
	LrgEditorPhysicsAssetEditor   *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	g_autoptr(LrgPhysicsAssetPart) part = NULL;
	g_autoptr(GError)              error = NULL;

	/* the asset's own scalars are written at their paths */
	if (g_strcmp0 (name, "name") == 0 || g_strcmp0 (name, "exclude-adjacent") == 0)
		return TRUE;
	if (self->asset == NULL || self->selected < 0)
		return FALSE;
	part = lrg_physics_asset_part_copy (lrg_physics_asset_get_part (self->asset, (guint) self->selected));
	if (!apply_value (part, name, value, &error) ||
	    !commit_part (self, (guint) self->selected, part, self->gesture, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	return FALSE;
}

static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture,
                    gpointer         user_data)
{
	LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data)->gesture = gesture;
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture,
                  gboolean         cancelled,
                  gpointer         user_data)
{
	LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data)->gesture = 0;
}

/* ==========================================================================
 * Toolbar and buttons
 * ========================================================================== */

static void
on_model_combo (LrgComboBox *combo,
                gpointer     user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	gint                         i = lrg_combo_box_get_selected (combo);
	g_autoptr(GError)            error = NULL;

	if (self->syncing || i <= 0 || self->model_list == NULL || (guint) i > self->model_list->len)
		return;
	if (!lrg_editor_physics_asset_editor_set_model (self, g_ptr_array_index (self->model_list, i - 1), &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_simulate (LrgButton *button,
             gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	g_autoptr(GError)            error = NULL;

	if (!lrg_editor_physics_asset_editor_set_simulating (self, self->world == NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_reset (LrgButton *button,
          gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	gboolean                     was = self->world != NULL;

	stop_simulation (self);
	if (was)
		start_simulation (self, NULL);
}

static void
on_add_part (LrgButton *button,
             gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	const gchar                 *bone = lrg_combo_box_get_selected_text (self->add_combo);
	g_autoptr(GError)            error = NULL;

	if (bone == NULL || self->skeleton == NULL || lrg_skeleton_get_bone_by_name (self->skeleton, bone) == NULL)
		return;
	if (lrg_editor_physics_asset_editor_add_part (self, bone, &error) < 0)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_remove_part (LrgButton *button,
                gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	g_autoptr(GError)            error = NULL;

	if (self->selected < 0)
		return;
	if (!lrg_editor_physics_asset_editor_remove_part (self, (guint) self->selected, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_stabilize (LrgButton *button,
              gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	g_autoptr(GError)            error = NULL;

	if (!lrg_editor_physics_asset_editor_stabilize (self, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_generate (LrgButton *button,
             gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	g_autoptr(GError)            error = NULL;

	if (!lrg_editor_physics_asset_editor_generate (self, (gfloat) lrg_spin_box_get_value (self->gen_mass),
	                                               (gfloat) lrg_spin_box_get_value (self->gen_ratio), &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else if (app_of (self) != NULL)
		editor_app_notify (app_of (self), EDITOR_NOTIFY_INFO, "Generated %u parts",
		                   self->asset != NULL ? lrg_physics_asset_get_part_count (self->asset) : 0u);
}

static void
on_exclude (LrgButton *button,
            gpointer   user_data)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (user_data);
	const gchar                 *a = lrg_combo_box_get_selected_text (self->excl_a);
	const gchar                 *b = lrg_combo_box_get_selected_text (self->excl_b);
	g_autoptr(GError)            error = NULL;

	if (a == NULL || b == NULL)
		return;
	if (!lrg_editor_physics_asset_editor_set_exclusion (self, a, b, TRUE, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

static void
physasset_refresh (LrgEditorDataEditor *editor,
                   const gchar         *path)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (editor);
	g_autofree gchar            *sig = NULL;

	if (self->parts == NULL)
		return;
	/* the ragdoll holds the old asset: an edit ends the simulation */
	if (self->world != NULL)
		stop_simulation (self);
	load_asset (self);
	sync_model (self);
	sig = parts_signature (self);
	if (g_strcmp0 (sig, self->parts_sig) != 0)
		self->parts_stale = TRUE;
	g_clear_pointer (&sig, g_free);
	sig = fields_signature (self);
	if (g_strcmp0 (sig, self->fields_sig) != 0)
		self->fields_stale = TRUE;
	else
	{
		lrg_editor_data_source_document_changed (self->source, "");
		lrg_property_grid_refresh (self->grid);
	}
}

static void
physasset_validate (LrgEditorDataEditor *editor,
                    GPtrArray           *issues)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (editor);
	guint                        i, n;

	if (self->load_error != NULL)
	{
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 1, 1, self->load_error));
		return;
	}
	if (self->asset == NULL)
		return;
	n = lrg_physics_asset_get_part_count (self->asset);
	if (n == 0)
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_INFO, "/parts", 0, 0,
		                                                        "The asset has no parts"));
	if (self->skeleton == NULL)
		return;
	for (i = 0; i < n; i++)
	{
		const LrgPhysicsAssetPart *p = lrg_physics_asset_get_part (self->asset, i);
		g_autofree gchar          *path = g_strdup_printf ("/parts/%u/bone", i);
		guint                      line = 0, col = 0;
		gint                       parent;

		lrg_editor_document_get_line (doc_of (self), path, &line, &col);
		if (lrg_skeleton_get_bone_by_name (self->skeleton, p->bone) == NULL)
		{
			g_autofree gchar *msg = g_strdup_printf ("bone '%s' is not in the preview model's skeleton", p->bone);

			g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, path, line, col, msg));
			continue;
		}
		/* mass ratios across a joint are the main cause of joint stretch */
		parent = parent_part (self, self->asset, i);
		if (parent >= 0)
		{
			gfloat pm = lrg_physics_asset_get_part ((self->asset), (guint) parent)->mass;
			gfloat ratio = p->mass / MAX (pm, 1e-6f);

			if (ratio > 4.0f || ratio < 0.25f)
			{
				g_autofree gchar *msg = g_strdup_printf ("'%s' weighs %.1f times its parent '%s': Stabilize masses",
				                                         p->bone, ratio,
				                                         lrg_physics_asset_get_part (self->asset,
				                                                                     (guint) parent)->bone);

				g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col,
				                                                        msg));
			}
		}
	}
}

static gchar *
physasset_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (editor);

	return g_strdup_printf ("parts=%u selected=%d simulating=%s",
	                        self->asset != NULL ? lrg_physics_asset_get_part_count (self->asset) : 0u, self->selected,
	                        self->world != NULL ? "true" : "false");
}

static gboolean
lrg_editor_physics_asset_editor_step (LrgWidget *widget,
                                      gfloat     delta_time)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (widget);
	gboolean                     busy;

	/* simulate before the viewport (a child) renders in its own step */
	if (self->world != NULL)
		sim_step (self, delta_time);
	busy = LRG_WIDGET_CLASS (lrg_editor_physics_asset_editor_parent_class)->step (widget, delta_time);
	if (self->parts_stale)
		rebuild_parts (self);
	if (self->combos_stale)
		rebuild_combos (self);
	if (self->fields_stale)
		rebuild_fields (self);
	if (self->view != NULL)
		view_sync_chips (self->view);
	return busy || self->world != NULL;
}

static LrgButton *
tool_button (const gchar *label,
             const gchar *name,
             GCallback    func,
             gpointer     data)
{
	LrgButton *b = lrg_button_new (label);

	lrg_widget_set_name (LRG_WIDGET (b), name);
	g_signal_connect (b, "clicked", func, data);
	return b;
}

static void
lrg_editor_physics_asset_editor_constructed (GObject *object)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (object);
	LrgEditorDataEditor         *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp                *app;
	LrgWidget                   *top, *scroll, *row, *right, *bar, *gscroll, *lower, *column, *grid_box;
	LrgSplitter                 *outer;

	G_OBJECT_CLASS (lrg_editor_physics_asset_editor_parent_class)->constructed (object);
	app = app_of (self);
	self->draw = lrg_debug_draw_new ();

	/* --- left top: parts, generate, exclusions --- */
	top = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (top), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (top), 8.0f);
	self->summary = LRG_LABEL (heading ("PARTS"));
	lrg_widget_set_name (LRG_WIDGET (self->summary), "physasset-summary");
	lrg_container_add_child (LRG_CONTAINER (top), LRG_WIDGET (self->summary));
	g_object_unref (self->summary);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	self->add_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->add_combo), "physasset-add-bone");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->add_combo), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->add_combo));
	g_object_unref (self->add_combo);
	add_to (row, LRG_WIDGET (tool_button ("Add Part", "physasset-add-part", G_CALLBACK (on_add_part), self)));
	add_to (row, LRG_WIDGET (tool_button ("Remove", "physasset-remove-part", G_CALLBACK (on_remove_part), self)));
	add_to (top, row);
	self->parts = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->parts, "physasset-parts");
	lrg_container_set_spacing (LRG_CONTAINER (self->parts), 2.0f);
	scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (scroll, "physasset-parts-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (scroll), self->parts);
	g_object_unref (self->parts);
	lrg_widget_set_fill_weight (scroll, 1.0f);
	add_to (top, scroll);


	/* --- left bottom (scrolls): generate, exclusions, the property grid --- */
	lower = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (lower), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (lower), 8.0f);
	add_to (lower, heading ("GENERATE FROM SKELETON  (total kg, radius / bone length)"));
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	self->gen_mass = lrg_spin_box_new_with_range (1.0, 1000.0, 1.0);
	lrg_spin_box_set_value (self->gen_mass, DEFAULT_MASS);
	lrg_widget_set_name (LRG_WIDGET (self->gen_mass), "physasset-generate-mass");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->gen_mass), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->gen_mass));
	g_object_unref (self->gen_mass);
	self->gen_ratio = lrg_spin_box_new_with_range (0.05, 1.0, 0.05);
	lrg_spin_box_set_digits (self->gen_ratio, 2);
	lrg_spin_box_set_value (self->gen_ratio, DEFAULT_RATIO);
	lrg_widget_set_name (LRG_WIDGET (self->gen_ratio), "physasset-generate-ratio");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->gen_ratio), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->gen_ratio));
	g_object_unref (self->gen_ratio);
	add_to (row, LRG_WIDGET (tool_button ("Generate", "physasset-generate", G_CALLBACK (on_generate), self)));
	add_to (row, LRG_WIDGET (tool_button ("Stabilize Masses", "physasset-stabilize", G_CALLBACK (on_stabilize),
	                                      self)));
	add_to (lower, row);

	add_to (lower, heading ("COLLISION EXCLUSIONS"));
	self->exclusions = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->exclusions, "physasset-exclusions");
	lrg_container_set_spacing (LRG_CONTAINER (self->exclusions), 2.0f);
	lrg_container_add_child (LRG_CONTAINER (lower), self->exclusions);
	g_object_unref (self->exclusions);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	self->excl_a = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->excl_a), "physasset-exclude-a");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->excl_a), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->excl_a));
	g_object_unref (self->excl_a);
	self->excl_b = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->excl_b), "physasset-exclude-b");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->excl_b), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (self->excl_b));
	g_object_unref (self->excl_b);
	add_to (row, LRG_WIDGET (tool_button ("Exclude", "physasset-exclude", G_CALLBACK (on_exclude), self)));
	add_to (lower, row);

	/* --- left bottom: the property grid --- */
	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "physasset-grid");
	lrg_property_grid_set_label_width (self->grid, 130.0f);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	g_signal_connect_object (self->grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), self, 0);
	g_signal_connect_object (self->grid, "edit-end", G_CALLBACK (on_grid_edit_end), self, 0);
	/* one scrolling column: parts, the selected part, generate,
	 * exclusions (the parts list scrolls on its own past 240 px) */
	column = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (column, "physasset-column");
	lrg_container_set_spacing (LRG_CONTAINER (column), 4.0f);
	add_to (column, top);
	grid_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (grid_box), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (grid_box), 8.0f);
	add_to (grid_box, heading ("SELECTED PART"));
	lrg_container_add_child (LRG_CONTAINER (grid_box), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	add_to (column, grid_box);
	add_to (column, lower);
	gscroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (gscroll, "physasset-grid-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (gscroll), column);
	g_object_unref (column);

	/* --- right: model picker, simulate, preview --- */
	right = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (right), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (right), 6.0f);
	bar = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (bar), 6.0f);
	self->model_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->model_combo), "physasset-model");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->model_combo), 1.0f);
	g_signal_connect_object (self->model_combo, "changed", G_CALLBACK (on_model_combo), self, 0);
	lrg_container_add_child (LRG_CONTAINER (bar), LRG_WIDGET (self->model_combo));
	g_object_unref (self->model_combo);
	self->sim_button = tool_button ("Simulate", "physasset-simulate", G_CALLBACK (on_simulate), self);
	lrg_widget_add_class (LRG_WIDGET (self->sim_button), "primary");
	lrg_container_add_child (LRG_CONTAINER (bar), LRG_WIDGET (self->sim_button));
	g_object_unref (self->sim_button);
	add_to (bar, LRG_WIDGET (tool_button ("Reset", "physasset-reset", G_CALLBACK (on_reset), self)));
	add_to (right, bar);
	self->view = editor_phys_view_new (self);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->view), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (self->view));
	g_object_unref (self->view);

	outer = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (outer), "physasset-split");
	lrg_splitter_set_start_child (outer, gscroll);
	g_object_unref (gscroll);
	lrg_splitter_set_end_child (outer, right);
	g_object_unref (right);
	lrg_splitter_set_default_fraction (outer, 0.42f);
	lrg_splitter_set_fraction (outer, 0.42f, FALSE);
	lrg_splitter_set_min_sizes (outer, 360.0f, 300.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (outer));
	g_object_unref (outer);

	load_asset (self);
	sync_model (self);
	compute_bounds (self);
	if (self->asset != NULL && lrg_physics_asset_get_part_count (self->asset) > 0)
		self->selected = 0;
	rebuild_parts (self);
	rebuild_combos (self);
	rebuild_fields (self);
	view_sync_chips (self->view);
	if (app != NULL)
		lrg_editor_physics_asset_editor_install (app);
}

static void
lrg_editor_physics_asset_editor_dispose (GObject *object)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (object);

	if (self->ragdoll != NULL)
		lrg_ragdoll_remove_from_world (self->ragdoll);
	g_clear_object (&self->ragdoll);
	g_clear_object (&self->world);
	if (self->mesh != NULL && self->uploaded && IsWindowReady ())
		lrg_skinned_mesh_unload (self->mesh);
	self->uploaded = FALSE;
	g_clear_object (&self->mesh);
	g_clear_object (&self->skeleton);
	G_OBJECT_CLASS (lrg_editor_physics_asset_editor_parent_class)->dispose (object);
}

static void
lrg_editor_physics_asset_editor_finalize (GObject *object)
{
	LrgEditorPhysicsAssetEditor *self = LRG_EDITOR_PHYSICS_ASSET_EDITOR (object);

	g_clear_object (&self->asset);
	g_clear_object (&self->source);
	g_clear_object (&self->draw);
	g_clear_pointer (&self->model_list, g_ptr_array_unref);
	g_free (self->model);
	g_free (self->load_error);
	g_free (self->fields_sig);
	g_free (self->parts_sig);
	G_OBJECT_CLASS (lrg_editor_physics_asset_editor_parent_class)->finalize (object);
}

static void
lrg_editor_physics_asset_editor_class_init (LrgEditorPhysicsAssetEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_physics_asset_editor_constructed;
	object_class->dispose = lrg_editor_physics_asset_editor_dispose;
	object_class->finalize = lrg_editor_physics_asset_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_physics_asset_editor_step;
	data_class->refresh = physasset_refresh;
	data_class->validate = physasset_validate;
	data_class->get_state = physasset_get_state;
}

static void
lrg_editor_physics_asset_editor_init (LrgEditorPhysicsAssetEditor *self)
{
	self->selected = -1;
	self->show_mesh = TRUE;
	self->show_shapes = TRUE;
	self->show_joints = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorPhysicsAssetEditor *
lrg_editor_physics_asset_editor_new (LrgEditorApp      *app,
                                     LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_PHYSICS_ASSET_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorPhysicsAssetEditor *
lrg_editor_physics_asset_editor_open (LrgEditorApp  *app,
                                      const gchar   *path,
                                      GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a physics asset", path);
		return NULL;
	}
	return LRG_EDITOR_PHYSICS_ASSET_EDITOR (e);
}

gboolean
lrg_editor_physics_asset_editor_create (LrgEditorApp  *app,
                                        const gchar   *path,
                                        const gchar   *model,
                                        gfloat         total_mass,
                                        gfloat         radius_ratio,
                                        guint         *n_parts,
                                        GError       **error)
{
	g_autofree gchar           *abs = NULL;
	g_autofree gchar           *dir = NULL;
	g_autofree gchar           *mabs = NULL;
	g_autofree gchar           *text = NULL;
	g_autoptr(LrgPhysicsAsset)  asset = NULL;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (n_parts != NULL)
		*n_parts = 0;
	abs = abs_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s", dir);
		return FALSE;
	}
	if (model != NULL)
	{
		g_autoptr(LrgSkeleton) skeleton = NULL;

		if (total_mass <= 0.0f || radius_ratio < 0.05f || radius_ratio > 1.0f)
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                     "total_mass must be positive and radius_ratio between 0.05 and 1");
			return FALSE;
		}
		mabs = abs_path (app, model);
		skeleton = lrg_gltf_load_skeleton (mabs, 0, error);
		if (skeleton == NULL)
			return FALSE;
		asset = lrg_physics_asset_new_from_skeleton (skeleton, total_mass, radius_ratio);
	}
	else
		asset = lrg_physics_asset_new ();
	{
		g_autofree gchar *base = g_path_get_basename (abs);
		gchar            *dot = strrchr (base, '.');

		if (dot != NULL)
			*dot = '\0';
		lrg_physics_asset_set_name (asset, base);
	}
	text = lrg_physics_asset_save_to_data (asset, NULL, error);
	if (text == NULL || !g_file_set_contents (abs, text, -1, error))
		return FALSE;
	if (mabs != NULL)
	{
		g_autoptr(LrgAssetMeta) meta = lrg_asset_meta_new_for_asset (abs);
		g_autofree gchar       *rel = editor_mat_relative_to (dir, mabs);
		g_autofree gchar       *meta_path = lrg_asset_meta_path_for_asset (abs);

		lrg_asset_meta_set_setting_string (meta, META_MODEL_KEY, rel);
		if (!lrg_asset_meta_save_to_file (meta, meta_path, error))
			return FALSE;
	}
	if (n_parts != NULL)
		*n_parts = lrg_physics_asset_get_part_count (asset);
	return TRUE;
}

LrgPhysicsAsset *
lrg_editor_physics_asset_editor_get_asset (LrgEditorPhysicsAssetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), NULL);
	return self->asset;
}

const gchar *
lrg_editor_physics_asset_editor_get_load_error (LrgEditorPhysicsAssetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), NULL);
	return self->load_error;
}

gboolean
lrg_editor_physics_asset_editor_set_model (LrgEditorPhysicsAssetEditor  *self,
                                           const gchar                  *model,
                                           GError                      **error)
{
	g_autofree gchar *abs = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);

	if (model != NULL)
	{
		abs = abs_path (app_of (self), model);
		if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No model at %s", abs);
			return FALSE;
		}
	}
	if (!load_model (self, abs, error))
		return FALSE;
	if (!write_meta_model (self, abs, error))
		return FALSE;
	self->parts_stale = TRUE;
	self->combos_stale = TRUE;
	self->fields_stale = TRUE;
	return TRUE;
}

const gchar *
lrg_editor_physics_asset_editor_get_model (LrgEditorPhysicsAssetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), NULL);
	return self->model;
}

LrgSkeleton *
lrg_editor_physics_asset_editor_get_skeleton (LrgEditorPhysicsAssetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), NULL);
	return self->skeleton;
}

gboolean
lrg_editor_physics_asset_editor_select_part (LrgEditorPhysicsAssetEditor *self,
                                             gint                         index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);

	if (index < -1 || (index >= 0 && (self->asset == NULL ||
	                                  (guint) index >= lrg_physics_asset_get_part_count (self->asset))))
		return FALSE;
	if (index == self->selected)
		return TRUE;
	self->selected = index;
	self->parts_stale = TRUE;
	self->fields_stale = TRUE;
	return TRUE;
}

gint
lrg_editor_physics_asset_editor_get_selected_part (LrgEditorPhysicsAssetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), -1);
	return self->selected;
}

/* Fails with a message when the editor has no preview skeleton. */
static gboolean
need_skeleton (LrgEditorPhysicsAssetEditor  *self,
               GError                      **error)
{
	if (self->skeleton != NULL)
		return TRUE;
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	                     "Pick a preview model with a skeleton first");
	return FALSE;
}

gint
lrg_editor_physics_asset_editor_add_part (LrgEditorPhysicsAssetEditor  *self,
                                          const gchar                  *bone,
                                          GError                      **error)
{
	g_autoptr(LrgPhysicsAsset)     copy = NULL;
	g_autoptr(LrgPhysicsAssetPart) part = NULL;
	g_autofree gchar              *label = NULL;
	gint                           index;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), -1);
	g_return_val_if_fail (bone != NULL, -1);

	copy = parse_doc (self, error);
	if (copy == NULL)
		return -1;
	if (self->skeleton != NULL && lrg_skeleton_get_bone_by_name (self->skeleton, bone) == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "The skeleton has no bone '%s'", bone);
		return -1;
	}
	/* with a skeleton the generator's part for that bone is the template:
	 * it runs along the bone with a joint along its axis */
	if (self->skeleton != NULL)
	{
		g_autoptr(LrgPhysicsAsset) gen = lrg_physics_asset_new_from_skeleton (self->skeleton, 10.0f, DEFAULT_RATIO);
		gint                       gi = lrg_physics_asset_find_part (gen, bone);

		if (gi >= 0)
		{
			part = lrg_physics_asset_part_copy (lrg_physics_asset_get_part (gen, (guint) gi));
			part->mass = lrg_physics_asset_get_part_count (copy) > 0
			             ? total_mass (copy) / (gfloat) lrg_physics_asset_get_part_count (copy) : 1.0f;
		}
	}
	if (part == NULL)
		part = lrg_physics_asset_part_new (bone);
	index = lrg_physics_asset_add_part (copy, part, error);
	if (index < 0)
		return -1;
	label = g_strdup_printf ("Add Part %s", bone);
	if (!commit_asset (self, copy, label, error))
		return -1;
	lrg_editor_physics_asset_editor_select_part (self, index);
	return index;
}

gboolean
lrg_editor_physics_asset_editor_remove_part (LrgEditorPhysicsAssetEditor  *self,
                                             guint                         index,
                                             GError                      **error)
{
	g_autoptr(LrgPhysicsAsset) copy = NULL;
	g_autofree gchar          *label = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);

	copy = parse_doc (self, error);
	if (copy == NULL)
		return FALSE;
	if (index >= lrg_physics_asset_get_part_count (copy))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No part %u", index);
		return FALSE;
	}
	label = g_strdup_printf ("Remove Part %s", lrg_physics_asset_get_part (copy, index)->bone);
	lrg_physics_asset_remove_part (copy, index);
	if (self->selected >= (gint) lrg_physics_asset_get_part_count (copy))
		self->selected = (gint) lrg_physics_asset_get_part_count (copy) - 1;
	self->fields_stale = TRUE;
	return commit_asset (self, copy, label, error);
}

gboolean
lrg_editor_physics_asset_editor_set_part (LrgEditorPhysicsAssetEditor  *self,
                                          guint                         index,
                                          const LrgPhysicsAssetPart    *part,
                                          GError                      **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);
	g_return_val_if_fail (part != NULL, FALSE);
	return commit_part (self, index, part, 0, error);
}

gboolean
lrg_editor_physics_asset_editor_generate (LrgEditorPhysicsAssetEditor  *self,
                                          gfloat                        total_mass_kg,
                                          gfloat                        radius_ratio,
                                          GError                      **error)
{
	g_autoptr(LrgPhysicsAsset) gen = NULL;
	g_autoptr(LrgPhysicsAsset) copy = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);

	if (!need_skeleton (self, error))
		return FALSE;
	if (total_mass_kg <= 0.0f || radius_ratio < 0.05f || radius_ratio > 1.0f)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                     "total_mass must be positive and radius_ratio between 0.05 and 1");
		return FALSE;
	}
	gen = lrg_physics_asset_new_from_skeleton (self->skeleton, total_mass_kg, radius_ratio);
	/* keep what is not generated: the name (exclusions name old bones) */
	copy = parse_doc (self, NULL);
	if (copy != NULL)
	{
		lrg_physics_asset_set_name (gen, lrg_physics_asset_get_name (copy));
		lrg_physics_asset_set_exclude_adjacent (gen, lrg_physics_asset_get_exclude_adjacent (copy));
	}
	self->selected = lrg_physics_asset_get_part_count (gen) > 0 ? 0 : -1;
	self->fields_stale = TRUE;
	return commit_asset (self, gen, "Generate from Skeleton", error);
}

gboolean
lrg_editor_physics_asset_editor_stabilize (LrgEditorPhysicsAssetEditor  *self,
                                           GError                      **error)
{
	g_autoptr(LrgPhysicsAsset) copy = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);

	if (!need_skeleton (self, error))
		return FALSE;
	copy = parse_doc (self, error);
	if (copy == NULL)
		return FALSE;
	lrg_physics_asset_stabilize (copy, self->skeleton);
	return commit_asset (self, copy, "Stabilize Masses", error);
}

gboolean
lrg_editor_physics_asset_editor_set_exclusion (LrgEditorPhysicsAssetEditor  *self,
                                               const gchar                  *bone_a,
                                               const gchar                  *bone_b,
                                               gboolean                      excluded,
                                               GError                      **error)
{
	g_autoptr(LrgPhysicsAsset) copy = NULL;
	guint                      i;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);
	g_return_val_if_fail (bone_a != NULL && bone_b != NULL, FALSE);

	copy = parse_doc (self, error);
	if (copy == NULL)
		return FALSE;
	if (lrg_physics_asset_find_part (copy, bone_a) < 0 || lrg_physics_asset_find_part (copy, bone_b) < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Both '%s' and '%s' need a part", bone_a, bone_b);
		return FALSE;
	}
	if (g_strcmp0 (bone_a, bone_b) == 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "An exclusion pairs two different parts");
		return FALSE;
	}
	if (excluded)
		lrg_physics_asset_add_exclusion (copy, bone_a, bone_b);
	else
		for (i = lrg_physics_asset_get_exclusion_count (copy); i > 0; i--)
		{
			const gchar *a = NULL, *b = NULL;

			lrg_physics_asset_get_exclusion (copy, i - 1, &a, &b);
			if ((g_strcmp0 (a, bone_a) == 0 && g_strcmp0 (b, bone_b) == 0) ||
			    (g_strcmp0 (a, bone_b) == 0 && g_strcmp0 (b, bone_a) == 0))
				lrg_physics_asset_remove_exclusion (copy, i - 1);
		}
	return commit_asset (self, copy, excluded ? "Add Exclusion" : "Remove Exclusion", error);
}

gboolean
lrg_editor_physics_asset_editor_set_simulating (LrgEditorPhysicsAssetEditor  *self,
                                                gboolean                      simulating,
                                                GError                      **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);

	if (!simulating)
	{
		stop_simulation (self);
		return TRUE;
	}
	return start_simulation (self, error);
}

gboolean
lrg_editor_physics_asset_editor_get_simulating (LrgEditorPhysicsAssetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), FALSE);
	return self->world != NULL;
}

void
lrg_editor_physics_asset_editor_advance (LrgEditorPhysicsAssetEditor *self,
                                         gfloat                       seconds)
{
	g_return_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self));

	while (self->world != NULL && seconds > 1e-6f)
	{
		gfloat dt = MIN (seconds, SIM_FRAME);

		sim_step (self, dt);
		seconds -= dt;
	}
}

LrgRagdoll *
lrg_editor_physics_asset_editor_get_ragdoll (LrgEditorPhysicsAssetEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), NULL);
	return self->ragdoll;
}

GrlImage *
lrg_editor_physics_asset_editor_render_preview (LrgEditorPhysicsAssetEditor *self,
                                                guint                        size)
{
	RenderTexture2D rt;
	Image           img;
	GrlImage       *out;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_ASSET_EDITOR (self), NULL);

	if (size == 0 || !IsWindowReady () || self->view == NULL)
		return NULL;
	rt = LoadRenderTexture ((gint) size, (gint) size);
	if (rt.id == 0)
		return NULL;
	BeginTextureMode (rt);
	view_render (self->view->viewport, NULL, (gfloat) size, (gfloat) size, self->view);
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	UnloadRenderTexture (rt);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	ImageFlipVertical (&img);
	out = grl_image_new_from_pixels (img.width, img.height, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, img.data);
	UnloadImage (img);
	return out;
}

/* ==========================================================================
 * Project physics settings (project.ryaml `physics:` block)
 * ========================================================================== */

void
editor_physics_settings_defaults (EditorPhysicsSettings *settings)
{
	/* the values LrgDynamicsWorld starts with, gravity Y up */
	settings->gravity[0] = 0.0f;
	settings->gravity[1] = -9.81f;
	settings->gravity[2] = 0.0f;
	settings->fixed_dt = 1.0f / 60.0f;
	settings->max_substeps = 4;
	settings->iterations = 10;
	settings->sleep_threshold = 9.0e-4f;
	settings->sleep_substeps = 30;
}

gboolean
editor_physics_settings_validate (const EditorPhysicsSettings  *s,
                                  GError                      **error)
{
	if (s->fixed_dt <= 0.0f || s->fixed_dt > 0.1f)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "fixed_dt must be in (0, 0.1] s");
		return FALSE;
	}
	if (s->max_substeps < 1 || s->max_substeps > LRG_DYNAMICS_MAX_SUBSTEPS)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "max_substeps must be 1 to %u",
		             (guint) LRG_DYNAMICS_MAX_SUBSTEPS);
		return FALSE;
	}
	if (s->iterations < 1 || s->iterations > 32)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "iterations must be 1 to 32");
		return FALSE;
	}
	if (s->sleep_threshold < 0.0f || s->sleep_substeps < 1)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                     "sleep_threshold must be >= 0 and sleep_substeps >= 1");
		return FALSE;
	}
	if (!isfinite (s->gravity[0]) || !isfinite (s->gravity[1]) || !isfinite (s->gravity[2]))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "gravity must be finite");
		return FALSE;
	}
	return TRUE;
}

static gchar *
manifest_path (LrgEditorApp *app)
{
	if (app == NULL || app->project == NULL)
		return NULL;
	return g_build_filename (lrg_project_get_root (app->project), LRG_PROJECT_MANIFEST, NULL);
}

/* The root `physics:` mapping of a parsed manifest (or inside project:). */
static YamlMapping *
find_physics (YamlNode *root)
{
	YamlNode *block = root != NULL ? lrg_physics_layers_find_block (root) : NULL;

	return (block != NULL && yaml_node_get_node_type (block) == YAML_NODE_MAPPING) ? yaml_node_get_mapping (block)
	                                                                               : NULL;
}

static gdouble
number_member (YamlMapping *map,
               const gchar *key,
               gdouble      fallback)
{
	YamlNode    *n = map != NULL ? yaml_mapping_get_member (map, key) : NULL;
	const gchar *s;
	gchar       *end = NULL;
	gdouble      v;

	if (n == NULL || yaml_node_get_node_type (n) != YAML_NODE_SCALAR)
		return fallback;
	s = yaml_node_get_scalar (n);
	v = g_ascii_strtod (s, &end);
	return (end != s && end != NULL && *end == '\0') ? v : fallback;
}

gboolean
editor_physics_settings_load (LrgEditorApp          *app,
                              EditorPhysicsSettings *s)
{
	g_autofree gchar     *path = manifest_path (app);
	g_autoptr(YamlParser) parser = NULL;
	YamlMapping          *map;
	YamlNode             *g;
	EditorPhysicsSettings d;

	editor_physics_settings_defaults (s);
	if (path == NULL)
		return FALSE;
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, NULL))
		return FALSE;
	map = find_physics (yaml_parser_get_root (parser));
	if (map == NULL)
		return TRUE;
	d = *s;
	g = yaml_mapping_get_member (map, "gravity");
	if (g != NULL && yaml_node_get_node_type (g) == YAML_NODE_SEQUENCE &&
	    yaml_sequence_get_length (yaml_node_get_sequence (g)) == 3)
	{
		guint k;

		for (k = 0; k < 3; k++)
		{
			YamlNode    *e = yaml_sequence_get_element (yaml_node_get_sequence (g), k);
			const gchar *t = (e != NULL && yaml_node_get_node_type (e) == YAML_NODE_SCALAR)
			                 ? yaml_node_get_scalar (e) : NULL;

			if (t != NULL)
				s->gravity[k] = (gfloat) g_ascii_strtod (t, NULL);
		}
	}
	s->fixed_dt = (gfloat) number_member (map, "fixed_dt", d.fixed_dt);
	s->max_substeps = (guint) number_member (map, "max_substeps", d.max_substeps);
	s->iterations = (guint) number_member (map, "iterations", d.iterations);
	s->sleep_threshold = (gfloat) number_member (map, "sleep_threshold", d.sleep_threshold);
	s->sleep_substeps = (guint) number_member (map, "sleep_substeps", d.sleep_substeps);
	/* a broken value falls back to the default rather than break worlds */
	if (!editor_physics_settings_validate (s, NULL))
		*s = d;
	return TRUE;
}

/*
 * editor_physics_project_save:
 * lrg_project_save() (the layer table goes through LrgProject) followed
 * by the world settings written into the same `physics:` block. LrgProject
 * keeps the block it read at open time, so the settings are re-applied
 * after every save: with @settings NULL the file's current ones.
 */
gboolean
editor_physics_project_save (LrgEditorApp                 *app,
                             const EditorPhysicsSettings  *settings,
                             GError                      **error)
{
	g_autofree gchar         *path = manifest_path (app);
	EditorPhysicsSettings     s;
	g_autoptr(YamlParser)     parser = NULL;
	g_autoptr(YamlNode)       root = NULL;
	g_autoptr(YamlDocument)   ydoc = NULL;
	g_autoptr(YamlGenerator)  gen = NULL;
	g_autoptr(YamlSequence)   gravity = NULL;
	g_autofree gchar         *data = NULL;
	YamlMapping              *map;
	guint                     k;

	if (path == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No project is open");
		return FALSE;
	}
	if (settings != NULL)
	{
		if (!editor_physics_settings_validate (settings, error))
			return FALSE;
		s = *settings;
	}
	else
		editor_physics_settings_load (app, &s);
	if (!lrg_project_save (app->project, error))
		return FALSE;

	/* patch the block LrgProject just wrote */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, error))
		return FALSE;
	root = yaml_node_copy (yaml_parser_get_root (parser));
	map = find_physics (root);
	if (map == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "project.ryaml has no physics: block");
		return FALSE;
	}
	gravity = yaml_sequence_new ();
	for (k = 0; k < 3; k++)
		yaml_sequence_add_double_element (gravity, (gdouble) s.gravity[k]);
	yaml_mapping_set_sequence_member (map, "gravity", gravity);
	yaml_mapping_set_double_member (map, "fixed_dt", (gdouble) s.fixed_dt);
	yaml_mapping_set_int_member (map, "max_substeps", (gint64) s.max_substeps);
	yaml_mapping_set_int_member (map, "iterations", (gint64) s.iterations);
	yaml_mapping_set_double_member (map, "sleep_threshold", (gdouble) s.sleep_threshold);
	yaml_mapping_set_int_member (map, "sleep_substeps", (gint64) s.sleep_substeps);
	ydoc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, ydoc);
	data = yaml_generator_to_data (gen, NULL, error);
	if (data == NULL)
		return FALSE;
	return g_file_set_contents (path, data, -1, error);
}

/* ==========================================================================
 * IPC helpers
 * ========================================================================== */

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name,
            gboolean    *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_boolean (n);
	return TRUE;
}

/* A number (JSON int or double, or a numeric string). */
static gboolean
node_number (JsonNode *n,
             gdouble  *out)
{
	GType t;

	if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	t = json_node_get_value_type (n);
	if (t == G_TYPE_INT64 || t == G_TYPE_INT)
		*out = (gdouble) json_node_get_int (n);
	else if (t == G_TYPE_DOUBLE || t == G_TYPE_FLOAT)
		*out = json_node_get_double (n);
	else if (t == G_TYPE_STRING)
	{
		const gchar *s = json_node_get_string (n);
		gchar       *end = NULL;

		*out = g_ascii_strtod (s, &end);
		return end != s && *end == '\0';
	}
	else
		return FALSE;
	return TRUE;
}

static gboolean
param_number (JsonObject  *params,
              const gchar *name,
              gdouble     *out)
{
	return params != NULL && json_object_has_member (params, name) &&
	       node_number (json_object_get_member (params, name), out);
}

/* An array of @n numbers. */
static gboolean
node_vector (JsonNode *n,
             gfloat   *out,
             guint     count)
{
	JsonArray *a;
	guint      k;

	if (n == NULL || !JSON_NODE_HOLDS_ARRAY (n))
		return FALSE;
	a = json_node_get_array (n);
	if (json_array_get_length (a) != count)
		return FALSE;
	for (k = 0; k < count; k++)
	{
		gdouble v;

		if (!node_number (json_array_get_element (a, k), &v))
			return FALSE;
		out[k] = (gfloat) v;
	}
	return TRUE;
}

static const gchar *
require_string (JsonObject   *params,
                const gchar  *name,
                GError      **error)
{
	const gchar *s = param_string (params, name);

	if (s == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
	return s;
}

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

/* A float as the double with the same shortest text (0.1f -> 0.1, not
 * 0.10000000149011612), so JSON replies read like the file. */
static gdouble
clean (gfloat v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_formatd (buf, sizeof buf, "%.7g", (gdouble) v);
	return g_ascii_strtod (buf, NULL);
}

static JsonArray *
float_array (const gfloat *v,
             guint         n)
{
	JsonArray *a = json_array_new ();
	guint      k;

	for (k = 0; k < n; k++)
		json_array_add_double_element (a, clean (v[k]));
	return a;
}

/* ==========================================================================
 * IPC: physasset.*
 * ========================================================================== */

static LrgEditorPhysicsAssetEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar                 *path = require_string (params, "path", error);
	LrgEditorPhysicsAssetEditor *self;
	g_autoptr(GError)            err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_physics_asset_editor_open (app, path, &err);
	if (self == NULL)
		fail (error, err);
	return self;
}

/* "part": an index or a bone name. */
static gint
ipc_part (LrgEditorPhysicsAssetEditor  *self,
          JsonObject                   *params,
          GError                      **error)
{
	const gchar *bone = param_string (params, "part");
	gdouble      v;
	gint         index = -1;

	if (self->asset == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s",
		             self->load_error != NULL ? self->load_error : "The asset does not load");
		return -1;
	}
	if (bone != NULL)
	{
		gchar *end = NULL;
		gint64 i = g_ascii_strtoll (bone, &end, 10);

		index = (end != bone && *end == '\0') ? (gint) i : lrg_physics_asset_find_part (self->asset, bone);
	}
	else if (param_number (params, "part", &v))
		index = (gint) v;
	else
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing 'part' (an index or a bone name)");
		return -1;
	}
	if (index < 0 || (guint) index >= lrg_physics_asset_get_part_count (self->asset))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such part");
		return -1;
	}
	return index;
}

static JsonObject *
part_json (LrgEditorPhysicsAssetEditor *self,
           guint                        index)
{
	const LrgPhysicsAssetPart *p = lrg_physics_asset_get_part (self->asset, index);
	JsonObject                *o = json_object_new ();
	JsonObject                *j;
	gint                       parent = parent_part (self, self->asset, index);
	gfloat                     tw[2] = { p->twist_min, p->twist_max };
	gfloat                     sw[2] = { p->swing_y, p->swing_z };

	json_object_set_int_member (o, "index", index);
	json_object_set_string_member (o, "bone", p->bone);
	json_object_set_string_member (o, "shape", shape_nick (p->shape));
	if (p->shape != LRG_PHYSICS_ASSET_SHAPE_BOX)
		json_object_set_double_member (o, "radius", clean (p->radius));
	if (p->shape == LRG_PHYSICS_ASSET_SHAPE_CAPSULE)
		json_object_set_double_member (o, "half_height", clean (p->half_height));
	if (p->shape == LRG_PHYSICS_ASSET_SHAPE_BOX)
		json_object_set_array_member (o, "half_extents", float_array (p->half_extents, 3));
	json_object_set_array_member (o, "offset", float_array (p->offset, 3));
	json_object_set_array_member (o, "rotation", float_array (p->rotation, 4));
	json_object_set_double_member (o, "mass", clean (p->mass));
	if (self->skeleton != NULL)
	{
		if (parent >= 0)
			json_object_set_string_member (o, "parent", lrg_physics_asset_get_part (self->asset, (guint) parent)->bone);
		else
			json_object_set_null_member (o, "parent");
	}
	j = json_object_new ();
	json_object_set_string_member (j, "kind", joint_nick (p->joint));
	json_object_set_array_member (j, "position", float_array (p->joint_position, 3));
	json_object_set_array_member (j, "rotation", float_array (p->joint_rotation, 4));
	json_object_set_array_member (j, "twist", float_array (tw, 2));
	json_object_set_array_member (j, "swing", float_array (sw, 2));
	json_object_set_double_member (j, "friction", clean (p->friction));
	json_object_set_double_member (j, "motor_max_torque", clean (p->motor_max_torque));
	json_object_set_double_member (j, "motor_frequency", clean (p->motor_frequency));
	json_object_set_double_member (j, "motor_damping", clean (p->motor_damping));
	json_object_set_object_member (o, "joint", j);
	return o;
}

static JsonObject *
asset_json (LrgEditorPhysicsAssetEditor *self)
{
	JsonObject *o = json_object_new ();
	JsonArray  *parts = json_array_new ();
	JsonArray  *excl = json_array_new ();
	JsonArray  *bones = json_array_new ();
	JsonArray  *free_bones = json_array_new ();
	guint       i;

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	if (self->load_error != NULL)
		json_object_set_string_member (o, "load_error", self->load_error);
	if (self->model != NULL)
		json_object_set_string_member (o, "model", self->model);
	else
		json_object_set_null_member (o, "model");
	if (self->asset != NULL)
	{
		const gchar *name = lrg_physics_asset_get_name (self->asset);

		if (name != NULL)
			json_object_set_string_member (o, "name", name);
		else
			json_object_set_null_member (o, "name");
		json_object_set_boolean_member (o, "exclude_adjacent", lrg_physics_asset_get_exclude_adjacent (self->asset));
		json_object_set_double_member (o, "total_mass", clean (total_mass (self->asset)));
		for (i = 0; i < lrg_physics_asset_get_part_count (self->asset); i++)
			json_array_add_object_element (parts, part_json (self, i));
		for (i = 0; i < lrg_physics_asset_get_exclusion_count (self->asset); i++)
		{
			const gchar *a = NULL, *b = NULL;
			JsonArray   *pair = json_array_new ();

			lrg_physics_asset_get_exclusion (self->asset, i, &a, &b);
			json_array_add_string_element (pair, a);
			json_array_add_string_element (pair, b);
			json_array_add_array_element (excl, pair);
		}
	}
	for (i = 0; self->skeleton != NULL && i < lrg_skeleton_get_bone_count (self->skeleton); i++)
	{
		const gchar *bn = lrg_bone_get_name (lrg_skeleton_get_bone (self->skeleton, (gint) i));

		json_array_add_string_element (bones, bn);
		if (self->asset == NULL || lrg_physics_asset_find_part (self->asset, bn) < 0)
			json_array_add_string_element (free_bones, bn);
	}
	json_object_set_array_member (o, "parts", parts);
	json_object_set_array_member (o, "exclusions", excl);
	json_object_set_array_member (o, "bones", bones);
	json_object_set_array_member (o, "free_bones", free_bones);
	json_object_set_int_member (o, "selected", self->selected);
	json_object_set_boolean_member (o, "simulating", self->world != NULL);
	return o;
}

/* Applies a JSON "values" object to @p (keys as in physasset.get). */
static gboolean
apply_json_values (LrgPhysicsAssetPart  *p,
                   JsonObject           *values,
                   GError              **error)
{
	GList *members = json_object_get_members (values);
	GList *l;
	gboolean ok = TRUE;

	/* the shape first: a shape change resizes before explicit sizes */
	if (json_object_has_member (values, "shape"))
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, json_object_get_string_member (values, "shape"));
		ok = apply_value (p, "shape", &v, error);
		g_value_unset (&v);
	}
	for (l = members; ok && l != NULL; l = l->next)
	{
		g_autofree gchar *key = g_strdelimit (g_strdup (l->data), "_", '-');
		JsonNode         *n = json_object_get_member (values, l->data);
		gdouble           d;
		gfloat            v[4];

		if (g_strcmp0 (key, "shape") == 0)
			continue;
		if (g_strcmp0 (key, "bone") == 0 || g_strcmp0 (key, "joint") == 0 || g_strcmp0 (key, "kind") == 0)
		{
			GValue gv = G_VALUE_INIT;

			if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "'%s' must be a string", key);
				ok = FALSE;
				break;
			}
			g_value_init (&gv, G_TYPE_STRING);
			g_value_set_string (&gv, json_node_get_string (n));
			ok = apply_value (p, g_strcmp0 (key, "bone") == 0 ? "bone" : "joint", &gv, error);
			g_value_unset (&gv);
		}
		else if (g_strcmp0 (key, "half-extents") == 0 && node_vector (n, v, 3))
			memcpy (p->half_extents, v, sizeof (gfloat) * 3);
		else if (g_strcmp0 (key, "offset") == 0 && node_vector (n, v, 3))
			memcpy (p->offset, v, sizeof (gfloat) * 3);
		else if (g_strcmp0 (key, "rotation") == 0 && node_vector (n, v, 4))
			memcpy (p->rotation, v, sizeof (gfloat) * 4);
		else if (g_strcmp0 (key, "rotation-degrees") == 0 && node_vector (n, v, 3))
			quat_from_euler (v[0] * (gfloat) G_PI / 180.0f, v[1] * (gfloat) G_PI / 180.0f,
			                 v[2] * (gfloat) G_PI / 180.0f, p->rotation);
		else if (g_strcmp0 (key, "joint-position") == 0 && node_vector (n, v, 3))
			memcpy (p->joint_position, v, sizeof (gfloat) * 3);
		else if (g_strcmp0 (key, "joint-rotation") == 0 && node_vector (n, v, 4))
			memcpy (p->joint_rotation, v, sizeof (gfloat) * 4);
		else if (g_strcmp0 (key, "joint-rotation-degrees") == 0 && node_vector (n, v, 3))
			quat_from_euler (v[0] * (gfloat) G_PI / 180.0f, v[1] * (gfloat) G_PI / 180.0f,
			                 v[2] * (gfloat) G_PI / 180.0f, p->joint_rotation);
		else if (g_strcmp0 (key, "twist") == 0 && node_vector (n, v, 2))
		{
			p->twist_min = v[0];
			p->twist_max = v[1];
		}
		else if (g_strcmp0 (key, "twist-degrees") == 0 && node_vector (n, v, 2))
		{
			p->twist_min = v[0] * (gfloat) G_PI / 180.0f;
			p->twist_max = v[1] * (gfloat) G_PI / 180.0f;
		}
		else if (g_strcmp0 (key, "swing") == 0 && node_vector (n, v, 2))
		{
			p->swing_y = v[0];
			p->swing_z = v[1];
		}
		else if (g_strcmp0 (key, "swing-degrees") == 0 && node_vector (n, v, 2))
		{
			p->swing_y = v[0] * (gfloat) G_PI / 180.0f;
			p->swing_z = v[1] * (gfloat) G_PI / 180.0f;
		}
		else if (node_number (n, &d) &&
		         (g_strcmp0 (key, "radius") == 0 || g_strcmp0 (key, "half-height") == 0 ||
		          g_strcmp0 (key, "mass") == 0 || g_strcmp0 (key, "friction") == 0 ||
		          g_strcmp0 (key, "motor-max-torque") == 0 || g_strcmp0 (key, "motor-frequency") == 0 ||
		          g_strcmp0 (key, "motor-damping") == 0))
		{
			GValue gv = G_VALUE_INIT;

			g_value_init (&gv, G_TYPE_DOUBLE);
			g_value_set_double (&gv, d);
			ok = apply_value (p, key, &gv, error);
			g_value_unset (&gv);
		}
		else
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown or malformed value '%s'",
			             (const gchar *) l->data);
			ok = FALSE;
		}
	}
	g_list_free (members);
	return ok;
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject                  *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = require_string (params, "path", error);
	g_autoptr(GError)  err = NULL;
	gdouble            mass = DEFAULT_MASS, ratio = DEFAULT_RATIO;
	guint              n = 0;
	JsonObject        *o;

	if (path == NULL)
		return NULL;
	param_number (params, "total_mass", &mass);
	param_number (params, "radius_ratio", &ratio);
	if (!lrg_editor_physics_asset_editor_create (app, path, param_string (params, "model"), (gfloat) mass,
	                                             (gfloat) ratio, &n, &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	{
		g_autofree gchar *abs = abs_path (app, path);

		json_object_set_string_member (o, "path", abs);
	}
	json_object_set_int_member (o, "parts", n);
	return object_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? object_node (asset_json (self)) : NULL;
}

static JsonNode *
m_set_model (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)            err = NULL;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_physics_asset_editor_set_model (self, param_string (params, "model"), &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (asset_json (self));
}

static JsonNode *
m_add_part (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *bone;
	g_autoptr(GError)            err = NULL;
	gint                         index;
	JsonObject                  *o;

	if (self == NULL || (bone = require_string (params, "bone", error)) == NULL)
		return NULL;
	index = lrg_editor_physics_asset_editor_add_part (self, bone, &err);
	if (index < 0)
	{
		fail (error, err);
		return NULL;
	}
	if (json_object_has_member (params, "values") && JSON_NODE_HOLDS_OBJECT (json_object_get_member (params, "values")))
	{
		g_autoptr(LrgPhysicsAssetPart) part = lrg_physics_asset_part_copy (lrg_physics_asset_get_part (self->asset,
		                                                                                              (guint) index));

		if (!apply_json_values (part, json_object_get_object_member (params, "values"), &err) ||
		    !commit_part (self, (guint) index, part, 0, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	o = asset_json (self);
	json_object_set_int_member (o, "index", index);
	return object_node (o);
}

static JsonNode *
m_remove_part (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)            err = NULL;
	gint                         index;

	if (self == NULL || (index = ipc_part (self, params, error)) < 0)
		return NULL;
	if (!lrg_editor_physics_asset_editor_remove_part (self, (guint) index, &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (asset_json (self));
}

static JsonNode *
m_set_part (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorPhysicsAssetEditor   *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(LrgPhysicsAssetPart) part = NULL;
	g_autoptr(GError)              err = NULL;
	gint                           index;

	if (self == NULL || (index = ipc_part (self, params, error)) < 0)
		return NULL;
	if (!json_object_has_member (params, "values") || !JSON_NODE_HOLDS_OBJECT (json_object_get_member (params, "values")))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing object parameter 'values'");
		return NULL;
	}
	part = lrg_physics_asset_part_copy (lrg_physics_asset_get_part (self->asset, (guint) index));
	if (!apply_json_values (part, json_object_get_object_member (params, "values"), &err) ||
	    !lrg_editor_physics_asset_editor_set_part (self, (guint) index, part, &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (part_json (self, (guint) index));
}

static JsonNode *
m_generate (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)            err = NULL;
	gdouble                      mass = DEFAULT_MASS, ratio = DEFAULT_RATIO;

	if (self == NULL)
		return NULL;
	param_number (params, "total_mass", &mass);
	param_number (params, "radius_ratio", &ratio);
	if (!lrg_editor_physics_asset_editor_generate (self, (gfloat) mass, (gfloat) ratio, &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (asset_json (self));
}

static JsonNode *
m_stabilize (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)            err = NULL;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_physics_asset_editor_stabilize (self, &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (asset_json (self));
}

static JsonNode *
m_exclude (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *a, *b;
	gboolean                     remove = FALSE;
	g_autoptr(GError)            err = NULL;

	if (self == NULL || (a = require_string (params, "a", error)) == NULL ||
	    (b = require_string (params, "b", error)) == NULL)
		return NULL;
	param_bool (params, "remove", &remove);
	if (!lrg_editor_physics_asset_editor_set_exclusion (self, a, b, !remove, &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (asset_json (self));
}

static JsonNode *
m_set (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(LrgPhysicsAsset)   copy = NULL;
	g_autoptr(GError)            err = NULL;
	gboolean                     adj;

	if (self == NULL)
		return NULL;
	copy = parse_doc (self, &err);
	if (copy == NULL)
	{
		fail (error, err);
		return NULL;
	}
	if (json_object_has_member (params, "name"))
		lrg_physics_asset_set_name (copy, param_string (params, "name"));
	if (param_bool (params, "exclude_adjacent", &adj))
		lrg_physics_asset_set_exclude_adjacent (copy, adj);
	if (!commit_asset (self, copy, "Edit Asset", &err))
	{
		fail (error, err);
		return NULL;
	}
	return object_node (asset_json (self));
}

static JsonNode *
m_select (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	gint                         index;
	JsonObject                  *o;

	if (self == NULL || (index = ipc_part (self, params, error)) < 0)
		return NULL;
	lrg_editor_physics_asset_editor_select_part (self, index);
	o = json_object_new ();
	json_object_set_int_member (o, "selected", index);
	json_object_set_string_member (o, "bone", lrg_physics_asset_get_part (self->asset, (guint) index)->bone);
	return object_node (o);
}

static JsonNode *
m_simulate (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)            err = NULL;
	gboolean                     run = TRUE, reset = FALSE;
	gdouble                      advance = 0.0;
	JsonObject                  *o;
	JsonArray                   *parts;
	guint                        i;

	if (self == NULL)
		return NULL;
	param_bool (params, "reset", &reset);
	if (!param_bool (params, "run", &run))
		run = reset ? self->world != NULL : TRUE;
	if (reset)
		stop_simulation (self);
	if (run && self->world == NULL && !lrg_editor_physics_asset_editor_set_simulating (self, TRUE, &err))
	{
		fail (error, err);
		return NULL;
	}
	if (!run)
		stop_simulation (self);
	if (param_number (params, "advance", &advance) && advance > 0.0)
		lrg_editor_physics_asset_editor_advance (self, (gfloat) MIN (advance, 60.0));
	o = json_object_new ();
	json_object_set_boolean_member (o, "simulating", self->world != NULL);
	json_object_set_double_member (o, "time", clean (self->sim_time));
	parts = json_array_new ();
	for (i = 0; self->asset != NULL && self->skeleton != NULL && i < lrg_physics_asset_get_part_count (self->asset);
	     i++)
	{
		const gchar *bone = lrg_physics_asset_get_part (self->asset, i)->bone;
		JsonObject  *po = json_object_new ();
		Xf           f;

		json_object_set_string_member (po, "bone", bone);
		if (bone_frame (self, bone, &f))
			json_object_set_array_member (po, "position", float_array (f.p, 3));
		json_array_add_object_element (parts, po);
	}
	json_object_set_array_member (o, "parts", parts);
	return object_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorPhysicsAssetEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)            err = NULL;
	JsonObject                  *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

/* ==========================================================================
 * IPC: physics.layers.*
 * ========================================================================== */

static LrgPhysicsLayers *
ipc_layers (LrgEditorApp  *app,
            GError       **error)
{
	if (app->project == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No project is open");
		return NULL;
	}
	return lrg_project_get_physics_layers (app->project);
}

/* A layer parameter: a name or an index (number or decimal string). */
static gint
param_layer (LrgPhysicsLayers  *layers,
             JsonObject        *params,
             const gchar       *key,
             GError           **error)
{
	const gchar       *s = param_string (params, key);
	gdouble            v;
	g_autoptr(GError)  err = NULL;
	gint               i;

	if (s != NULL)
	{
		i = lrg_physics_layers_resolve (layers, s, &err);
		if (i < 0)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "%s", err->message);
		return i;
	}
	if (param_number (params, key, &v) && v >= 0.0 && v < (gdouble) LRG_PHYSICS_LAYERS_MAX)
		return (gint) v;
	g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
	             "'%s' must be a layer name or an index 0 to 31", key);
	return -1;
}

static JsonObject *
settings_json (const EditorPhysicsSettings *s)
{
	JsonObject *o = json_object_new ();

	json_object_set_array_member (o, "gravity", float_array (s->gravity, 3));
	json_object_set_double_member (o, "fixed_dt", clean (s->fixed_dt));
	json_object_set_int_member (o, "max_substeps", s->max_substeps);
	json_object_set_int_member (o, "iterations", s->iterations);
	json_object_set_double_member (o, "sleep_threshold", clean (s->sleep_threshold));
	json_object_set_int_member (o, "sleep_substeps", s->sleep_substeps);
	return o;
}

static JsonNode *
layers_json (LrgEditorApp *app)
{
	LrgPhysicsLayers     *layers = lrg_project_get_physics_layers (app->project);
	JsonObject           *o = json_object_new ();
	JsonArray            *names = json_array_new ();
	JsonArray            *rows = json_array_new ();
	JsonArray            *pairs = json_array_new ();
	EditorPhysicsSettings s;
	guint                 a, b;

	for (a = 0; a < LRG_PHYSICS_LAYERS_MAX; a++)
	{
		const gchar *n = lrg_physics_layers_get_name (layers, a);

		if (n != NULL)
			json_array_add_string_element (names, n);
		else
			json_array_add_null_element (names);
		json_array_add_int_element (rows, (gint64) lrg_physics_layers_get_collide_mask (layers, a));
		for (b = a; b < LRG_PHYSICS_LAYERS_MAX; b++)
			if (!lrg_physics_layers_get_collides (layers, a, b))
			{
				JsonArray   *pair = json_array_new ();
				const gchar *na = lrg_physics_layers_get_name (layers, a);
				const gchar *nb = lrg_physics_layers_get_name (layers, b);

				if (na != NULL)
					json_array_add_string_element (pair, na);
				else
					json_array_add_int_element (pair, a);
				if (nb != NULL)
					json_array_add_string_element (pair, nb);
				else
					json_array_add_int_element (pair, b);
				json_array_add_array_element (pairs, pair);
			}
	}
	json_object_set_array_member (o, "layers", names);
	json_object_set_int_member (o, "count", lrg_physics_layers_get_count (layers));
	json_object_set_array_member (o, "matrix", rows);
	json_object_set_array_member (o, "no_collide", pairs);
	editor_physics_settings_load (app, &s);
	json_object_set_object_member (o, "settings", settings_json (&s));
	return object_node (o);
}

/* Saves the project after a layer edit; NULL settings keep the file's. */
static gboolean
layers_save (LrgEditorApp                 *app,
             const EditorPhysicsSettings  *settings,
             GError                      **error)
{
	g_autoptr(GError) err = NULL;

	if (!editor_physics_project_save (app, settings, &err))
	{
		fail (error, err);
		return FALSE;
	}
	return TRUE;
}

static JsonNode *
m_layers_get (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);

	if (ipc_layers (app, error) == NULL)
		return NULL;
	return layers_json (app);
}

static JsonNode *
m_layers_set_name (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	LrgPhysicsLayers  *layers = ipc_layers (app, error);
	g_autoptr(GError)  err = NULL;
	gdouble            v;
	const gchar       *name;

	if (layers == NULL)
		return NULL;
	if (!param_number (params, "index", &v) || v < 0.0 || v >= (gdouble) LRG_PHYSICS_LAYERS_MAX)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'index' must be 0 to 31");
		return NULL;
	}
	name = param_string (params, "name");
	if (name == NULL && json_object_has_member (params, "name") &&
	    !JSON_NODE_HOLDS_NULL (json_object_get_member (params, "name")))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'name' must be a string or null");
		return NULL;
	}
	if (!lrg_physics_layers_set_name (layers, (guint) v, name, &err))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, err->message);
		return NULL;
	}
	if (!layers_save (app, NULL, error))
		return NULL;
	return layers_json (app);
}

static JsonNode *
m_layers_set_collides (LrgEditorServer  *server,
                       JsonObject       *params,
                       gpointer          user_data,
                       GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	LrgPhysicsLayers *layers = ipc_layers (app, error);
	gboolean          collides;
	gint              a, b;

	if (layers == NULL || (a = param_layer (layers, params, "a", error)) < 0 ||
	    (b = param_layer (layers, params, "b", error)) < 0)
		return NULL;
	if (!param_bool (params, "collides", &collides))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing boolean parameter 'collides'");
		return NULL;
	}
	lrg_physics_layers_set_collides (layers, (guint) a, (guint) b, collides);
	if (!layers_save (app, NULL, error))
		return NULL;
	return layers_json (app);
}

static JsonNode *
m_layers_settings (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp          *app = LRG_EDITOR_APP (user_data);
	EditorPhysicsSettings  s;
	gdouble                v;
	gboolean               changed = FALSE;

	if (ipc_layers (app, error) == NULL)
		return NULL;
	editor_physics_settings_load (app, &s);
	if (json_object_has_member (params, "gravity"))
	{
		if (!node_vector (json_object_get_member (params, "gravity"), s.gravity, 3))
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "'gravity' must be [x, y, z]");
			return NULL;
		}
		changed = TRUE;
	}
#define NUM(key, field, type) \
	if (param_number (params, key, &v)) { s.field = (type) v; changed = TRUE; }
	NUM ("fixed_dt", fixed_dt, gfloat);
	NUM ("max_substeps", max_substeps, guint);
	NUM ("iterations", iterations, guint);
	NUM ("sleep_threshold", sleep_threshold, gfloat);
	NUM ("sleep_substeps", sleep_substeps, guint);
#undef NUM
	if (param_bool (params, "reset", &changed) && changed)
		editor_physics_settings_defaults (&s);
	if (changed && !layers_save (app, &s, error))
		return NULL;
	editor_physics_settings_load (app, &s);
	return object_node (settings_json (&s));
}

static JsonNode *
m_layers_reset (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	LrgPhysicsLayers *layers = ipc_layers (app, error);
	gboolean          empty = FALSE;

	if (layers == NULL)
		return NULL;
	param_bool (params, "empty", &empty);
	if (empty)
		lrg_physics_layers_reset (layers);
	else
		lrg_physics_layers_reset_default (layers);
	if (!layers_save (app, NULL, error))
		return NULL;
	return layers_json (app);
}

static JsonNode *
m_layers_open (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o;

	if (ipc_layers (app, error) == NULL)
		return NULL;
	editor_dialog_project_settings_page (app, "physics");
	o = json_object_new ();
	json_object_set_string_member (o, "dialog", "project-settings");
	json_object_set_string_member (o, "page", "physics");
	return object_node (o);
}

void
lrg_editor_physics_asset_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "physasset.open", m_open },
		{ "physasset.create", m_create },
		{ "physasset.get", m_get },
		{ "physasset.set_model", m_set_model },
		{ "physasset.add_part", m_add_part },
		{ "physasset.remove_part", m_remove_part },
		{ "physasset.set_part", m_set_part },
		{ "physasset.generate", m_generate },
		{ "physasset.stabilize", m_stabilize },
		{ "physasset.exclude", m_exclude },
		{ "physasset.set", m_set },
		{ "physasset.select", m_select },
		{ "physasset.simulate", m_simulate },
		{ "physasset.save", m_save },
		{ "physics.layers.get", m_layers_get },
		{ "physics.layers.set_name", m_layers_set_name },
		{ "physics.layers.set_collides", m_layers_set_collides },
		{ "physics.layers.settings", m_layers_settings },
		{ "physics.layers.reset", m_layers_reset },
		{ "physics.layers.open", m_layers_open }
	};
	LrgEditorServer *server;
	guint            i;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	if (g_object_get_data (G_OBJECT (app), INSTALLED_KEY) != NULL)
		return;
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALLED_KEY, GINT_TO_POINTER (1));
	lrg_editor_data_editor_install (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
