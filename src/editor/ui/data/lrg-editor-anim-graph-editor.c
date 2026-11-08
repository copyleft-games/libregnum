/* lrg-editor-anim-graph-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAnimGraphEditor: the `.ranim` data editor (see the header).
 *
 * Layout:
 *
 *   +-------------+-----------------------------------+----------------+
 *   | PARAMETERS  | [base][upper][+]  base > air      |  preview       |
 *   |  speed ==o  |                                   |  (skinned      |
 *   |  jump [Fire]|   LrgNodeGraph: one card per      |   model)       |
 *   |  + F I B T  |   state, wires = transitions,     +----------------+
 *   | CLIPS       |   "Any State" card, active state  | inspector:     |
 *   |  idle  walk |   highlighted while previewing    | state / tree / |
 *   |  [+ model]  |                                   | transition +   |
 *   |             |                                   | conditions /   |
 *   |             |                                   | layer          |
 *   +-------------+-----------------------------------+----------------+
 *
 * The document is the truth. Every gesture writes it directly through the
 * LrgEditorDocument path API (so undo, redo, save and external changes
 * come for free), card positions included (`editor_pos: [x, y]` on each
 * state; the engine keeps unknown keys). Canvas gestures that make several
 * writes (a Delete removing a state and its wires) are wrapped in one
 * document group opened on the first write and closed on idle.
 *
 * After every document change the text is parsed into a fresh LrgAnimGraph
 * (the "preview graph"): its clips are resolved through a per-editor cache
 * of glTF files, the live parameter values the user set are pushed into
 * it and it is started. The preview view (EditorAnimView, shared with the
 * bone map editor, the clip preview of the assets panel and the inspector)
 * asks for a pose every frame: the graph is updated by the frame time when
 * playing and evaluated into the preview model's skeleton.
 *
 * The canvas is rebuilt from the document whenever the change did not come
 * from the canvas itself. Highlights (the active state, the state being
 * blended in, the wire of the running transition) are refreshed every
 * frame from the preview graph's machines.
 *
 * Private types in this file:
 *   EditorAnimView    the skinned model preview widget
 *   AgBlendSpace      the blend-space view (1D strip or 2D plane)
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-anim-graph-editor.h"
#include "lrg-editor-material-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../animation/lrg-gltf-animation.h"
#include "../../../animation/lrg-animation-state-machine.h"
#include "../../../animation/lrg-animation-state.h"
#include "../../../animation/lrg-animation-transition.h"
#include "../../../animation/lrg-animation-layer.h"
#include "../../../animation/lrg-blend-tree.h"
#include "../../../animation/lrg-bone.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-viewport.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-slider.h"
#include "../../../ui/lrg-checkbox.h"
#include "../../../ui/lrg-spin-box.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-breadcrumb.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define INSTALLED_KEY     "lrg-editor-anim-installed"
#define ANY_ID            LRG_EDITOR_ANIM_GRAPH_ANY_STATE
#define VIEW_FOV          (36.0f)
#define VIEW_SPIN_SPEED   (0.45f)        /* radians per second */
#define GRID_COLS         (4)
#define GRID_DX           (230.0f)
#define GRID_DY           (150.0f)
#define CARD_W            (170.0f)

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

/* "a/b" + "c" -> "a/b/c" (the document's escaped path join). */
static gchar *
pj (const gchar *parent,
    const gchar *token)
{
	return lrg_editor_document_path_join (parent, token);
}

/* "a/b" + index -> "a/b/<i>". */
static gchar *
pji (const gchar *parent,
     guint        index)
{
	g_autofree gchar *tok = g_strdup_printf ("%u", index);

	return lrg_editor_document_path_join (parent, tok);
}

/* A YAML scalar for a string: quoted when it is not a plain identifier. */
static gchar *
yaml_str (const gchar *s)
{
	const gchar *p;
	gboolean     plain = s != NULL && *s != '\0' && (g_ascii_isalpha (*s) || *s == '_');

	for (p = s; plain && *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-' && *p != '.')
			plain = FALSE;
	/* words YAML reads as booleans or null stay strings */
	if (plain && (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "false") == 0 ||
	              g_ascii_strcasecmp (s, "yes") == 0 || g_ascii_strcasecmp (s, "no") == 0 ||
	              g_ascii_strcasecmp (s, "on") == 0 || g_ascii_strcasecmp (s, "off") == 0 ||
	              g_ascii_strcasecmp (s, "null") == 0))
		plain = FALSE;
	if (plain)
		return g_strdup (s);
	{
		GString *out = g_string_new ("\"");

		for (p = s != NULL ? s : ""; *p != '\0'; p++)
		{
			if (*p == '"' || *p == '\\')
				g_string_append_c (out, '\\');
			g_string_append_c (out, *p);
		}
		g_string_append_c (out, '"');
		return g_string_free (out, FALSE);
	}
}

/* A float written the shortest way that reads back ("0.25", "3"). */
static gchar *
num_str (gdouble v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	if (fabs (v - rint (v)) < 1e-9 && fabs (v) < 1e9)
		return g_strdup_printf ("%d", (gint) rint (v));
	g_ascii_formatd (buf, sizeof buf, "%.6g", v);
	return g_strdup (buf);
}

/* ==========================================================================
 * EditorAnimView: the skinned model preview
 *
 * A container with an LrgViewport (pointer transparent, rendering every
 * frame), a caption and a chip bar (Play, Bones, Turntable). Dragging
 * orbits the camera, the wheel zooms. The model is a glTF file: skin 0
 * becomes the skeleton and the skinned mesh (a file without a skin shows
 * its first mesh, unposed), the animations its clips. Every step the
 * owner's pose function (or the view's own clip playback) poses the
 * skeleton, world poses and the palette follow.
 * ========================================================================== */

#define EDITOR_TYPE_ANIM_VIEW (editor_anim_view_get_type ())
G_DECLARE_FINAL_TYPE (EditorAnimView, editor_anim_view, EDITOR, ANIM_VIEW, LrgContainer)

struct _EditorAnimView
{
	LrgContainer        parent_instance;

	LrgEditorApp       *app;          /* weak */
	LrgViewport        *viewport;
	LrgWidget          *caption;
	LrgWidget          *pill;
	LrgWidget          *bar;
	LrgWidget          *play_chip;
	LrgWidget          *bones_chip;
	LrgWidget          *turn_chip;

	gchar              *model;        /* absolute path */
	LrgSkeleton        *skeleton;
	LrgSkinnedMesh     *mesh;
	GPtrArray          *clips;        /* LrgAnimationClip */
	LrgBonePose        *poses;
	guint               n_poses;
	gboolean            uploaded;
	gboolean            upload_failed;
	gfloat              bmin[3];
	gfloat              bmax[3];

	EditorAnimPoseFunc  pose_func;
	gpointer            pose_data;
	gint                clip;
	gfloat              time;
	gboolean            playing;
	gboolean            show_bones;
	gboolean            turntable;
	gchar              *caption_text;
	GArray             *targets;      /* gfloat x3 */
	guint8              tint[3];

	gfloat              yaw;
	gfloat              pitch;
	gfloat              spin;
	gfloat              zoom;
	gboolean            dragging;
	gfloat              last_x;
	gfloat              last_y;
};

G_DEFINE_FINAL_TYPE (EditorAnimView, editor_anim_view, LRG_TYPE_CONTAINER)

/* Drops the model (and its GPU buffers when a context exists). */
static void
view_clear_model (EditorAnimView *self)
{
	if (self->mesh != NULL && self->uploaded && IsWindowReady ())
		lrg_skinned_mesh_unload (self->mesh);
	g_clear_object (&self->mesh);
	g_clear_object (&self->skeleton);
	g_clear_pointer (&self->clips, g_ptr_array_unref);
	g_clear_pointer (&self->poses, g_free);
	g_clear_pointer (&self->model, g_free);
	self->n_poses = 0;
	self->uploaded = FALSE;
	self->upload_failed = FALSE;
}

/* The bounds the camera frames: bind positions, else the bones. */
static void
view_compute_bounds (EditorAnimView *self)
{
	const gfloat *pos = NULL;
	guint         n = 0, i, k;

	self->bmin[0] = self->bmin[1] = self->bmin[2] = G_MAXFLOAT;
	self->bmax[0] = self->bmax[1] = self->bmax[2] = -G_MAXFLOAT;
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
	if (n == 0 && self->skeleton != NULL)
	{
		n = lrg_skeleton_get_bone_count (self->skeleton);
		for (i = 0; i < n; i++)
		{
			const LrgBonePose *w = lrg_bone_get_world_pose (lrg_skeleton_get_bone (self->skeleton, (gint) i));
			gfloat             p[3] = { w->position_x, w->position_y, w->position_z };

			for (k = 0; k < 3; k++)
			{
				self->bmin[k] = MIN (self->bmin[k], p[k]);
				self->bmax[k] = MAX (self->bmax[k], p[k]);
			}
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

/* Samples the view's own clip into the skeleton at self->time. */
static void
view_pose_own_clip (EditorAnimView *self)
{
	LrgAnimationClip *clip;
	guint             ch;

	if (self->clips == NULL || self->clip < 0 || (guint) self->clip >= self->clips->len)
	{
		lrg_skeleton_reset_to_bind (self->skeleton);
		return;
	}
	clip = g_ptr_array_index (self->clips, self->clip);
	lrg_anim_pose_sample_clip (clip, self->skeleton, self->time, self->poses, self->n_poses);
	lrg_anim_pose_apply (self->skeleton, self->poses, self->n_poses);
	/* morph weight channels drive the mesh's blend shapes */
	for (ch = 0; self->mesh != NULL && ch < lrg_animation_clip_get_weights_channel_count (clip); ch++)
	{
		guint  width = lrg_animation_clip_get_weights_channel_width (clip, ch);
		gfloat w[64];

		if (width == 0 || width > G_N_ELEMENTS (w))
			continue;
		if (lrg_animation_clip_sample_weights (clip, ch, self->time, w))
			lrg_skinned_mesh_set_morph_group_weights (self->mesh,
			                                          lrg_animation_clip_get_weights_channel_target (clip, ch),
			                                          w, width);
	}
}

void
editor_anim_view_pose_now (LrgWidget *view,
                           gfloat     dt)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	if (self->skeleton == NULL)
		return;
	if (self->pose_func != NULL)
		self->pose_func (self->pose_data, self->skeleton, self->mesh, dt);
	else
	{
		/* own playback: loop the clip */
		if (self->clips != NULL && self->clip >= 0 && (guint) self->clip < self->clips->len)
		{
			gfloat len = lrg_animation_clip_get_duration (g_ptr_array_index (self->clips, self->clip));

			self->time += dt;
			if (len > 0.0f && self->time > len)
				self->time = fmodf (self->time, len);
		}
		view_pose_own_clip (self);
	}
	lrg_skeleton_calculate_world_poses (self->skeleton);
	if (self->mesh != NULL)
		lrg_skinned_mesh_apply_skeleton (self->mesh, self->skeleton);
}

/* World position of bone @i. */
static Vector3
bone_pos (LrgSkeleton *skeleton,
          gint         i)
{
	const LrgBonePose *w = lrg_bone_get_world_pose (lrg_skeleton_get_bone (skeleton, i));

	return (Vector3) { w->position_x, w->position_y, w->position_z };
}

/* Draws the model with the orbit camera; inside the caller's texture mode. */
static void
view_draw (EditorAnimView *self,
           gint            width,
           gint            height)
{
	Camera3D cam;
	gfloat   c[3], radius, dist, pitch, yaw;
	gfloat   ground;
	guint    k;

	for (k = 0; k < 3; k++)
		c[k] = 0.5f * (self->bmin[k] + self->bmax[k]);
	radius = 0.5f * sqrtf ((self->bmax[0] - self->bmin[0]) * (self->bmax[0] - self->bmin[0]) +
	                       (self->bmax[1] - self->bmin[1]) * (self->bmax[1] - self->bmin[1]) +
	                       (self->bmax[2] - self->bmin[2]) * (self->bmax[2] - self->bmin[2]));
	radius = MAX (radius, 0.05f);
	dist = radius / tanf (0.5f * VIEW_FOV * (gfloat) G_PI / 180.0f) * 1.45f * self->zoom;
	if (width < height && width > 0)
		dist *= (gfloat) height / (gfloat) width;
	pitch = self->pitch;
	yaw = self->yaw + self->spin;
	memset (&cam, 0, sizeof cam);
	cam.target = (Vector3) { c[0], c[1], c[2] };
	cam.position = (Vector3) { c[0] + dist * cosf (pitch) * sinf (yaw), c[1] + dist * sinf (pitch),
	                           c[2] + dist * cosf (pitch) * cosf (yaw) };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = VIEW_FOV;
	cam.projection = CAMERA_PERSPECTIVE;

	ClearBackground ((Color) { 26, 28, 38, 255 });
	BeginMode3D (cam);

	/* a ground grid under the model, sized to it */
	ground = self->bmin[1];
	rlPushMatrix ();
	rlTranslatef (c[0], ground, c[2]);
	DrawGrid (12, MAX (radius / 4.0f, 0.05f));
	rlPopMatrix ();

	/* the mesh (uploaded on first use: needs the context) */
	if (self->mesh != NULL && !self->uploaded && !self->upload_failed)
	{
		g_autoptr(GError) error = NULL;

		self->uploaded = lrg_skinned_mesh_upload (self->mesh, &error);
		if (!self->uploaded)
		{
			self->upload_failed = TRUE;
			g_warning ("animation preview: cannot upload %s: %s", self->model, error->message);
		}
	}
	if (self->mesh != NULL && self->uploaded)
		lrg_skinned_mesh_draw (self->mesh, NULL);

	/* bones and IK targets on top of the mesh */
	rlDrawRenderBatchActive ();
	rlDisableDepthTest ();
	if (self->show_bones && self->skeleton != NULL)
	{
		guint n = lrg_skeleton_get_bone_count (self->skeleton);
		guint i;

		for (i = 0; i < n; i++)
		{
			LrgBone *bone = lrg_skeleton_get_bone (self->skeleton, (gint) i);
			gint     parent = lrg_bone_get_parent_index (bone);
			Vector3  p = bone_pos (self->skeleton, (gint) i);

			if (parent >= 0 && (guint) parent < n)
				DrawLine3D (bone_pos (self->skeleton, parent), p, (Color) { 255, 196, 64, 255 });
			DrawSphere (p, radius * 0.018f, (Color) { 255, 230, 140, 255 });
		}
	}
	for (k = 0; self->targets != NULL && k + 2 < self->targets->len; k += 3)
	{
		Vector3 t = { g_array_index (self->targets, gfloat, k), g_array_index (self->targets, gfloat, k + 1),
		              g_array_index (self->targets, gfloat, k + 2) };

		DrawSphereWires (t, radius * 0.05f, 6, 8, (Color) { 90, 220, 255, 255 });
	}
	rlDrawRenderBatchActive ();
	rlEnableDepthTest ();
	EndMode3D ();
}

static void
view_render (LrgViewport      *viewport,
             GrlRenderTexture *target,
             gfloat            width,
             gfloat            height,
             gpointer          user_data)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (user_data);

	if (self->skeleton == NULL && self->mesh == NULL)
	{
		ClearBackground ((Color) { 26, 28, 38, 255 });
		return;
	}
	view_draw (self, (gint) width, (gint) height);
}

/* The caption: model, clip, time over a soft scrim; a hint without a model. */
static void
view_caption_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	EditorAnimView   *self = EDITOR_ANIM_VIEW (user_data);
	LrgTheme         *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat            fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont          *font = lrg_theme_get_ui_font (theme);
	const GrlColor    scrim = { 0, 0, 0, 96 };
	const GrlColor    ink = { 236, 238, 246, 255 };
	g_autofree gchar *base = NULL;
	g_autofree gchar *text = NULL;
	gfloat            tw = 0.0f;

	if (self->model == NULL)
		text = g_strdup ("No model: pick a .glb");
	else
	{
		base = g_path_get_basename (self->model);
		text = (self->caption_text != NULL) ? g_strdup_printf ("%s  ·  %s", base, self->caption_text)
		                                    : g_strdup (base);
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
view_sync_chips (EditorAnimView *self)
{
	editor_chip_set_checked (self->play_chip, self->playing);
	editor_chip_set_icon (self->play_chip, self->playing ? LRG_ICON_PAUSE : LRG_ICON_PLAY);
	editor_chip_set_label (self->play_chip, self->playing ? "Pause" : "Play");
	editor_chip_set_checked (self->bones_chip, self->show_bones);
	editor_chip_set_checked (self->turn_chip, self->turntable);
}

static void
on_view_play (LrgWidget *chip,
              gpointer   user_data)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (user_data);

	editor_anim_view_set_playing (LRG_WIDGET (self), !self->playing);
	g_signal_emit_by_name (self, "playing-toggled");
}

static void
on_view_bones (LrgWidget *chip,
               gpointer   user_data)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (user_data);

	editor_anim_view_set_show_bones (LRG_WIDGET (self), !self->show_bones);
}

static void
on_view_turn (LrgWidget *chip,
              gpointer   user_data)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (user_data);

	self->turntable = !self->turntable;
	view_sync_chips (self);
}

static void
editor_anim_view_layout_children (LrgContainer *container)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (container);
	gfloat          w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat          h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat          bw = 0.0f, bh = 0.0f;
	gfloat          inset = 10.0f;
	gboolean        compact;

	if (self->viewport == NULL || self->caption == NULL || self->pill == NULL || self->bar == NULL)
		return;
	lrg_widget_place_in_slot (LRG_WIDGET (self->viewport), 0.0f, 0.0f, w, h);
	lrg_widget_place_in_slot (self->caption, inset, inset, MAX (w - 2.0f * inset, 0.0f), 28.0f);
	/* icon-only chips when the labels do not fit */
	editor_chip_set_compact (self->play_chip, FALSE);
	editor_chip_set_compact (self->bones_chip, FALSE);
	editor_chip_set_compact (self->turn_chip, FALSE);
	lrg_widget_get_preferred_size (self->bar, &bw, &bh);
	compact = bw > w - 2.0f * inset - 10.0f;
	editor_chip_set_compact (self->play_chip, compact);
	editor_chip_set_compact (self->bones_chip, compact);
	editor_chip_set_compact (self->turn_chip, compact);
	lrg_widget_get_preferred_size (self->bar, &bw, &bh);
	bw = MIN (bw, MAX (w - 2.0f * inset, 0.0f));
	lrg_widget_place_in_slot (self->pill, (w - bw) * 0.5f - 5.0f, MAX (h - bh - inset, 0.0f) - 5.0f, bw + 10.0f,
	                          bh + 10.0f);
	lrg_widget_place_in_slot (self->bar, (w - bw) * 0.5f, MAX (h - bh - inset, 0.0f), bw, bh);
}

static void
editor_anim_view_measure (LrgWidget *widget,
                          gfloat    *width,
                          gfloat    *height)
{
	*width = 240.0f;
	*height = 220.0f;
}

static gboolean
editor_anim_view_step (LrgWidget *widget,
                       gfloat     delta_time)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (widget);
	gboolean        busy = FALSE;

	if (LRG_WIDGET_CLASS (editor_anim_view_parent_class)->step != NULL)
		busy = LRG_WIDGET_CLASS (editor_anim_view_parent_class)->step (widget, delta_time);
	if (self->turntable && !self->dragging)
	{
		self->spin = fmodf (self->spin + delta_time * VIEW_SPIN_SPEED, 2.0f * (gfloat) G_PI);
		busy = TRUE;
	}
	/* pose before the viewport (a child) renders in its own step */
	if (self->skeleton != NULL)
	{
		editor_anim_view_pose_now (widget, self->playing ? delta_time : 0.0f);
		busy = busy || self->playing;
	}
	return busy;
}

/* Drag to orbit, wheel to zoom. */
static gboolean
editor_anim_view_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (widget);
	gfloat          x = lrg_ui_event_get_x (event);
	gfloat          y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		self->dragging = TRUE;
		self->last_x = x;
		self->last_y = y;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (!self->dragging)
			return FALSE;
		self->yaw -= (x - self->last_x) * 0.012f;
		self->pitch = CLAMP (self->pitch + (y - self->last_y) * 0.010f, -1.35f, 1.35f);
		self->last_x = x;
		self->last_y = y;
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (!self->dragging)
			return FALSE;
		self->dragging = FALSE;
		return TRUE;
	case LRG_UI_EVENT_SCROLL:
		self->zoom = CLAMP (self->zoom * (lrg_ui_event_get_scroll_y (event) > 0.0f ? 0.9f : 1.1f), 0.25f, 4.0f);
		return TRUE;
	default:
		return FALSE;
	}
}

static void
editor_anim_view_dispose (GObject *object)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (object);

	if (self->caption != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->caption), NULL, NULL, NULL);
	if (self->pill != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->pill), NULL, NULL, NULL);
	if (self->viewport != NULL)
		lrg_viewport_set_render_func (self->viewport, NULL, NULL, NULL);
	self->pose_func = NULL;
	view_clear_model (self);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (editor_anim_view_parent_class)->dispose (object);
}

static void
editor_anim_view_finalize (GObject *object)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (object);

	g_free (self->caption_text);
	g_clear_pointer (&self->targets, g_array_unref);
	G_OBJECT_CLASS (editor_anim_view_parent_class)->finalize (object);
}

static void
editor_anim_view_class_init (EditorAnimViewClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = editor_anim_view_dispose;
	object_class->finalize = editor_anim_view_finalize;
	widget_class->measure = editor_anim_view_measure;
	widget_class->step = editor_anim_view_step;
	widget_class->handle_event = editor_anim_view_handle_event;
	container_class->layout_children = editor_anim_view_layout_children;
	/* owners hear about the Play chip through this signal */
	g_signal_new ("playing-toggled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	              G_TYPE_NONE, 0);
}

static void
editor_anim_view_init (EditorAnimView *self)
{
	self->yaw = 0.55f;
	self->pitch = 0.22f;
	self->zoom = 1.0f;
	self->clip = -1;
	self->playing = TRUE;
	self->tint[0] = 196;
	self->tint[1] = 202;
	self->tint[2] = 214;
}

LrgWidget *
editor_anim_view_new (LrgEditorApp *app,
                      const gchar  *name)
{
	static const LrgCustomWidgetFuncs caption_funcs = { NULL, view_caption_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs pill_funcs = { NULL, view_pill_paint, NULL, NULL, { NULL } };
	EditorAnimView   *self = g_object_new (EDITOR_TYPE_ANIM_VIEW, NULL);
	g_autofree gchar *vname = g_strconcat (name, "-viewport", NULL);
	g_autofree gchar *pname = g_strconcat (name, "-play", NULL);
	g_autofree gchar *bname = g_strconcat (name, "-bones", NULL);
	g_autofree gchar *tname = g_strconcat (name, "-turntable", NULL);

	g_set_weak_pointer (&self->app, app);
	lrg_widget_set_name (LRG_WIDGET (self), name);

	/* the 3D content */
	self->viewport = lrg_viewport_new ();
	lrg_widget_set_name (LRG_WIDGET (self->viewport), vname);
	lrg_viewport_set_auto_clear (self->viewport, FALSE);
	lrg_viewport_set_continuous (self->viewport, TRUE);
	lrg_viewport_set_render_func (self->viewport, view_render, self, NULL);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self->viewport), TRUE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->viewport));
	g_object_unref (self->viewport);

	/* caption overlay */
	self->caption = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->caption), &caption_funcs, self, NULL);
	lrg_widget_set_pointer_transparent (self->caption, TRUE);
	lrg_container_add_child (LRG_CONTAINER (self), self->caption);
	g_object_unref (self->caption);

	/* chip bar on a pill */
	self->pill = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->pill), &pill_funcs, self, NULL);
	lrg_widget_set_pointer_transparent (self->pill, TRUE);
	lrg_container_add_child (LRG_CONTAINER (self), self->pill);
	g_object_unref (self->pill);
	self->bar = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (self->bar), 2.0f);
	self->play_chip = editor_chip_new (pname, LRG_ICON_PAUSE, "Pause", on_view_play, self);
	editor_chip_set_tint (self->play_chip, LRG_THEME_COLOR_SUCCESS);
	lrg_container_add_child (LRG_CONTAINER (self->bar), self->play_chip);
	g_object_unref (self->play_chip);
	self->bones_chip = editor_chip_new (bname, LRG_ICON_USER, "Bones", on_view_bones, self);
	editor_chip_set_tint (self->bones_chip, LRG_THEME_COLOR_WARNING);
	lrg_container_add_child (LRG_CONTAINER (self->bar), self->bones_chip);
	g_object_unref (self->bones_chip);
	self->turn_chip = editor_chip_new (tname, LRG_ICON_REFRESH, "Turntable", on_view_turn, self);
	editor_chip_set_tint (self->turn_chip, LRG_THEME_COLOR_ACCENT);
	lrg_container_add_child (LRG_CONTAINER (self->bar), self->turn_chip);
	g_object_unref (self->turn_chip);
	lrg_container_add_child (LRG_CONTAINER (self), self->bar);
	g_object_unref (self->bar);
	view_sync_chips (self);
	return LRG_WIDGET (self);
}

gboolean
editor_anim_view_set_model (LrgWidget    *view,
                            const gchar  *abs_path,
                            GError      **error)
{
	EditorAnimView             *self = EDITOR_ANIM_VIEW (view);
	g_autoptr(LrgGltfAnimation) gltf = NULL;
	g_autoptr(LrgSkeleton)      skeleton = NULL;
	g_autoptr(LrgSkinnedMesh)   mesh = NULL;
	g_autoptr(GPtrArray)        clips = NULL;

	if (abs_path == NULL)
	{
		view_clear_model (self);
		return TRUE;
	}
	if (g_strcmp0 (abs_path, self->model) == 0)
		return TRUE;
	gltf = lrg_gltf_animation_new_from_file (abs_path, error);
	if (gltf == NULL)
		return FALSE;
	if (lrg_gltf_animation_get_skin_count (gltf) > 0)
	{
		skeleton = lrg_gltf_animation_create_skeleton (gltf, 0, error);
		if (skeleton == NULL)
			return FALSE;
		mesh = lrg_gltf_animation_create_skinned_mesh (gltf, 0, NULL);
	}
	else if (lrg_gltf_animation_get_mesh_count (gltf) > 0)
		mesh = lrg_gltf_animation_create_mesh (gltf, 0, NULL);
	if (skeleton == NULL && mesh == NULL)
	{
		g_set_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE, "%s has no skin and no mesh",
		             abs_path);
		return FALSE;
	}
	clips = lrg_gltf_animation_create_clips (gltf, NULL);

	view_clear_model (self);
	self->model = g_strdup (abs_path);
	self->skeleton = g_steal_pointer (&skeleton);
	self->mesh = g_steal_pointer (&mesh);
	self->clips = clips != NULL ? g_steal_pointer (&clips) : g_ptr_array_new_with_free_func (g_object_unref);
	if (self->mesh != NULL)
		lrg_skinned_mesh_set_tint (self->mesh, self->tint[0], self->tint[1], self->tint[2], 255);
	if (self->skeleton != NULL)
	{
		self->n_poses = lrg_skeleton_get_bone_count (self->skeleton);
		self->poses = g_new0 (LrgBonePose, MAX (self->n_poses, 1u));
		lrg_skeleton_calculate_world_poses (self->skeleton);
	}
	self->clip = (self->clips->len > 0 && self->pose_func == NULL) ? 0 : -1;
	self->time = 0.0f;
	view_compute_bounds (self);
	if (self->skeleton != NULL)
		editor_anim_view_pose_now (view, 0.0f);
	return TRUE;
}

const gchar *
editor_anim_view_get_model (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->model;
}

LrgSkeleton *
editor_anim_view_get_skeleton (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->skeleton;
}

LrgSkinnedMesh *
editor_anim_view_get_mesh (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->mesh;
}

GPtrArray *
editor_anim_view_get_clips (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->clips;
}

void
editor_anim_view_set_pose_func (LrgWidget          *view,
                                EditorAnimPoseFunc  func,
                                gpointer            user_data)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	self->pose_func = func;
	self->pose_data = user_data;
}

void
editor_anim_view_set_clip (LrgWidget *view,
                           gint       index)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	self->clip = (self->clips != NULL && index >= 0 && (guint) index < self->clips->len) ? index : -1;
	self->time = 0.0f;
	if (self->skeleton != NULL)
		editor_anim_view_pose_now (view, 0.0f);
}

gint
editor_anim_view_get_clip (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->clip;
}

void
editor_anim_view_set_time (LrgWidget *view,
                           gfloat     time)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	self->time = MAX (time, 0.0f);
	if (self->skeleton != NULL)
		editor_anim_view_pose_now (view, 0.0f);
}

gfloat
editor_anim_view_get_time (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->time;
}

void
editor_anim_view_set_playing (LrgWidget *view,
                              gboolean   playing)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	self->playing = playing;
	view_sync_chips (self);
}

gboolean
editor_anim_view_get_playing (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->playing;
}

void
editor_anim_view_set_show_bones (LrgWidget *view,
                                 gboolean   show)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	self->show_bones = show;
	view_sync_chips (self);
}

gboolean
editor_anim_view_get_show_bones (LrgWidget *view)
{
	return EDITOR_ANIM_VIEW (view)->show_bones;
}

void
editor_anim_view_set_caption (LrgWidget   *view,
                              const gchar *text)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	if (g_strcmp0 (text, self->caption_text) == 0)
		return;
	g_free (self->caption_text);
	self->caption_text = g_strdup (text);
}

void
editor_anim_view_set_morph (LrgWidget *view,
                            guint      target,
                            gfloat     weight)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	if (self->mesh != NULL)
		lrg_skinned_mesh_set_morph_weight (self->mesh, target, weight);
}

void
editor_anim_view_set_targets (LrgWidget    *view,
                              const gfloat *xyz,
                              guint         n)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	if (self->targets == NULL)
		self->targets = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_array_set_size (self->targets, 0);
	if (xyz != NULL && n > 0)
		g_array_append_vals (self->targets, xyz, 3 * n);
}

void
editor_anim_view_set_tint (LrgWidget *view,
                           guint8     r,
                           guint8     g,
                           guint8     b)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);

	self->tint[0] = r;
	self->tint[1] = g;
	self->tint[2] = b;
	if (self->mesh != NULL)
		lrg_skinned_mesh_set_tint (self->mesh, r, g, b, 255);
}

GrlImage *
editor_anim_view_render (LrgWidget *view,
                         guint      size)
{
	EditorAnimView *self = EDITOR_ANIM_VIEW (view);
	RenderTexture2D rt;
	Image           img;
	GrlImage       *out;

	if (size == 0 || !IsWindowReady () || (self->skeleton == NULL && self->mesh == NULL))
		return NULL;
	rt = LoadRenderTexture ((gint) size, (gint) size);
	if (rt.id == 0)
		return NULL;
	BeginTextureMode (rt);
	view_draw (self, (gint) size, (gint) size);
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	UnloadRenderTexture (rt);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	ImageFlipVertical (&img);
	out = grl_image_new_from_pixels (img.width, img.height, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, img.data);
	UnloadImage (img);
	return out;
}

/* Walks @dir for glTF files (skipping hidden folders and build output). */
static void
collect_models (const gchar *root,
                const gchar *dir,
                GPtrArray   *out,
                guint        depth)
{
	g_autoptr(GDir) d = NULL;
	const gchar    *name;

	if (depth > 6 || out->len >= 500)
		return;
	d = g_dir_open (dir, 0, NULL);
	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *child = NULL;

		if (name[0] == '.' || g_strcmp0 (name, "build") == 0)
			continue;
		child = g_build_filename (dir, name, NULL);
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
			collect_models (root, child, out, depth + 1);
		else if (g_str_has_suffix (name, ".glb") || g_str_has_suffix (name, ".gltf"))
			g_ptr_array_add (out, g_strdup (child + strlen (root) + 1));
	}
}

static gint
cmp_strings (gconstpointer a,
             gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

GPtrArray *
editor_anim_list_models (LrgEditorApp *app)
{
	GPtrArray   *out = g_ptr_array_new_with_free_func (g_free);
	const gchar *root = app != NULL ? lrg_editor_app_get_project_dir (app) : NULL;

	if (root == NULL)
		return out;
	collect_models (root, root, out, 0);
	g_ptr_array_sort (out, cmp_strings);
	return out;
}

/* ==========================================================================
 * LrgEditorAnimGraphEditor
 * ========================================================================== */

struct _LrgEditorAnimGraphEditor
{
	LrgEditorDataEditor  parent_instance;

	/* the model */
	LrgAnimGraph        *graph;          /* parsed from the document */
	gchar               *load_error;
	GHashTable          *clip_cache;     /* abs source -> GPtrArray of clips */
	GHashTable          *live;           /* parameter -> GVariant (live values) */
	gchar               *machine;        /* YAML path of the machine shown */

	/* selection: a state, or a transition (sel_from/sel_to) */
	gchar               *sel_state;
	gchar               *sel_from;
	gchar               *sel_to;

	/* widgets */
	LrgNodeGraph        *canvas;
	LrgBreadcrumb       *crumbs;
	LrgWidget           *layer_bar;
	LrgWidget           *view;
	LrgWidget           *inspector_title;
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	LrgWidget           *cond_box;
	LrgWidget           *blend_space;
	LrgWidget           *param_rows;
	LrgWidget           *clip_rows;
	LrgComboBox         *model_combo;
	GPtrArray           *model_list;     /* project-relative glTF paths */
	LrgSlider           *scrub;

	/* preview */
	gchar               *preview_model;  /* absolute */
	gboolean             playing;
	gfloat               ptime;
	LrgBonePose         *poses;
	guint                n_poses;

	/* bookkeeping */
	gboolean             syncing;        /* rebuilding widgets: ignore their signals */
	gboolean             writing;        /* a canvas gesture writes the document */
	gboolean             group_open;     /* a lazily closed canvas group is open */
	guint                group_idle;
	gboolean             canvas_stale;
	gboolean             grid_stale;
	gboolean             rows_stale;
	gchar               *grid_sig;
	gchar               *rows_sig;
	gchar               *cond_sig;
	gchar               *hl_sig;         /* highlight signature */
	guint                framed;
	guint                gesture;
};

G_DEFINE_FINAL_TYPE (LrgEditorAnimGraphEditor, lrg_editor_anim_graph_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_canvas (LrgEditorAnimGraphEditor *self);
static void rebuild_grid (LrgEditorAnimGraphEditor *self);
static void rebuild_rows (LrgEditorAnimGraphEditor *self);
static void rebuild_conditions (LrgEditorAnimGraphEditor *self);
static void rebuild_chrome (LrgEditorAnimGraphEditor *self);
static void load_graph (LrgEditorAnimGraphEditor *self);
static void sync_preview_model (LrgEditorAnimGraphEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorAnimGraphEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorAnimGraphEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* The directory clip sources and the preview model resolve against. */
static gchar *
base_dir_of (LrgEditorAnimGraphEditor *self)
{
	const gchar  *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	LrgEditorApp *app = app_of (self);

	if (path != NULL)
		return g_path_get_dirname (path);
	if (app != NULL && lrg_editor_app_get_project_dir (app) != NULL)
		return g_strdup (lrg_editor_app_get_project_dir (app));
	return g_get_current_dir ();
}

/* A path relative to the project (or absolute) made absolute. */
static gchar *
project_abs (LrgEditorAnimGraphEditor *self,
             const gchar              *path)
{
	LrgEditorApp     *app = app_of (self);
	g_autofree gchar *dir = NULL;

	/* canonical, so paths compare equal however they were written */
	if (g_path_is_absolute (path))
		return g_canonicalize_filename (path, NULL);
	if (app != NULL && lrg_editor_app_get_project_dir (app) != NULL)
		return g_canonicalize_filename (path, lrg_editor_app_get_project_dir (app));
	dir = base_dir_of (self);
	return g_canonicalize_filename (path, dir);
}

/* --- canvas edits: one lazily closed document group per gesture --- */

static gboolean
close_group_idle (gpointer user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	self->group_idle = 0;
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
	return G_SOURCE_REMOVE;
}

/* Closes a canvas group still open (before an API edit or an undo). */
static void
flush_group (LrgEditorAnimGraphEditor *self)
{
	g_clear_handle_id (&self->group_idle, g_source_remove);
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
}

/* Opens (or continues) the group of the current canvas gesture. */
static void
canvas_group (LrgEditorAnimGraphEditor *self,
              const gchar              *label)
{
	if (!self->group_open)
	{
		lrg_editor_document_begin_group (doc_of (self), label);
		self->group_open = TRUE;
	}
	if (self->group_idle == 0)
		self->group_idle = g_idle_add (close_group_idle, self);
}

/* One undo step of an API / panel edit. */
static void
edit_begin (LrgEditorAnimGraphEditor *self,
            const gchar              *label)
{
	flush_group (self);
	lrg_editor_document_begin_group (doc_of (self), label);
}

static void
edit_end (LrgEditorAnimGraphEditor *self)
{
	lrg_editor_document_end_group (doc_of (self));
}

/* ==========================================================================
 * Reading the document
 * ========================================================================== */

/* The layer index of a machine path ("/layers/2/..." -> 2). */
static guint
machine_layer (const gchar *machine)
{
	g_auto(GStrv) parts = lrg_editor_document_path_split (machine, NULL);

	if (parts == NULL || parts[0] == NULL || parts[1] == NULL)
		return 0;
	return (guint) g_ascii_strtoull (parts[1], NULL, 10);
}

static gint
n_items (LrgEditorAnimGraphEditor *self,
         const gchar              *path)
{
	gint n = lrg_editor_document_get_length (doc_of (self), path);

	return lrg_editor_document_get_kind (doc_of (self), path) == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE ? MAX (n, 0) : 0;
}

static guint
n_layers (LrgEditorAnimGraphEditor *self)
{
	return (guint) n_items (self, "/layers");
}

/* A scalar under @parent ("name", "clip", ...) or NULL. */
static const gchar *
scalar_at (LrgEditorAnimGraphEditor *self,
           const gchar              *parent,
           const gchar              *key)
{
	g_autofree gchar *p = pj (parent, key);

	return lrg_editor_document_get_scalar (doc_of (self), p);
}

static gboolean
has_key (LrgEditorAnimGraphEditor *self,
         const gchar              *parent,
         const gchar              *key)
{
	g_autofree gchar *p = pj (parent, key);

	return lrg_editor_document_has_path (doc_of (self), p);
}

/* The index of state @name in machine @machine, or -1. */
static gint
state_index_in (LrgEditorAnimGraphEditor *self,
                const gchar              *machine,
                const gchar              *name)
{
	g_autofree gchar *states = pj (machine, "states");
	gint              n = n_items (self, states);
	gint              i;

	for (i = 0; name != NULL && i < n; i++)
	{
		g_autofree gchar *sp = pji (states, (guint) i);

		if (g_strcmp0 (scalar_at (self, sp, "name"), name) == 0)
			return i;
	}
	return -1;
}

static gchar *
state_path_in (LrgEditorAnimGraphEditor *self,
               const gchar              *machine,
               const gchar              *name)
{
	gint              i = state_index_in (self, machine, name);
	g_autofree gchar *states = pj (machine, "states");

	return i >= 0 ? pji (states, (guint) i) : NULL;
}

/* "clip", "blend_tree", "machine" or "empty". */
static const gchar *
state_kind (LrgEditorAnimGraphEditor *self,
            const gchar              *state_path)
{
	if (has_key (self, state_path, "blend_tree"))
		return "blend_tree";
	if (has_key (self, state_path, "machine"))
		return "machine";
	if (has_key (self, state_path, "clip"))
		return "clip";
	return "empty";
}

/* The default state of a machine (explicit, else the first state). */
static const gchar *
default_state (LrgEditorAnimGraphEditor *self,
               const gchar              *machine)
{
	const gchar      *d = scalar_at (self, machine, "default");
	g_autofree gchar *states = NULL;
	g_autofree gchar *p0 = NULL;

	if (d != NULL)
		return d;
	states = pj (machine, "states");
	p0 = pji (states, 0);
	return scalar_at (self, p0, "name");
}

/* The YAML path of the transition @from -> @to (any-state for "*"). */
static gchar *
transition_path_in (LrgEditorAnimGraphEditor *self,
                    const gchar              *machine,
                    const gchar              *from,
                    const gchar              *to)
{
	gboolean          any = g_strcmp0 (from, ANY_ID) == 0;
	g_autofree gchar *list = pj (machine, any ? "any_state" : "transitions");
	gint              n = n_items (self, list);
	gint              i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *tp = pji (list, (guint) i);

		if ((any || g_strcmp0 (scalar_at (self, tp, "from"), from) == 0) &&
		    g_strcmp0 (scalar_at (self, tp, "to"), to) == 0)
			return g_steal_pointer (&tp);
	}
	return NULL;
}

/* The names of declared parameters of a type ("float|int" for numeric). */
static GPtrArray *
parameter_names (LrgEditorAnimGraphEditor *self,
                 gboolean                  numeric_only)
{
	GPtrArray *out = g_ptr_array_new_with_free_func (g_free);
	gint       n = n_items (self, "/parameters");
	gint       i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *pp = pji ("/parameters", (guint) i);
		const gchar      *name = scalar_at (self, pp, "name");
		const gchar      *type = scalar_at (self, pp, "type");

		if (name == NULL)
			continue;
		if (numeric_only && g_strcmp0 (type, "float") != 0 && g_strcmp0 (type, "int") != 0)
			continue;
		g_ptr_array_add (out, g_strdup (name));
	}
	return out;
}

static const gchar *
parameter_type (LrgEditorAnimGraphEditor *self,
                const gchar              *name)
{
	gint n = n_items (self, "/parameters");
	gint i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *pp = pji ("/parameters", (guint) i);

		if (g_strcmp0 (scalar_at (self, pp, "name"), name) == 0)
			return scalar_at (self, pp, "type");
	}
	return NULL;
}

/* The clip keys under `clips:`. */
static GStrv
clip_keys (LrgEditorAnimGraphEditor *self)
{
	GStrv keys = lrg_editor_document_get_keys (doc_of (self), "/clips");

	return keys != NULL ? keys : g_new0 (gchar *, 1);
}

/* "a,b,c" for enum-strings hints, with an optional leading empty entry. */
static gchar *
enum_hint (GPtrArray *names,
           gboolean   with_empty)
{
	GString *s = g_string_new ("enum-strings:");
	guint    i;

	if (with_empty)
		g_string_append (s, "(none)");
	for (i = 0; i < names->len; i++)
		g_string_append_printf (s, "%s%s", (i > 0 || with_empty) ? "," : "", (const gchar *) g_ptr_array_index (names, i));
	return g_string_free (s, FALSE);
}

/* A free name "<base>", "<base>2", ... among @taken (NULL-terminated test fn). */
static gchar *
free_state_name (LrgEditorAnimGraphEditor *self,
                 const gchar              *base)
{
	guint n;

	for (n = 1; n < 10000; n++)
	{
		g_autofree gchar *name = n == 1 ? g_strdup (base) : g_strdup_printf ("%s%u", base, n);

		if (state_index_in (self, self->machine, name) < 0)
			return g_steal_pointer (&name);
	}
	return g_strdup (base);
}

/* ==========================================================================
 * The preview graph
 * ========================================================================== */

/* Clip resolver: glTF files loaded once per editor, animations by name. */
static LrgAnimationClip *
resolve_clip (LrgAnimGraph  *graph,
              const gchar   *clip_name,
              const gchar   *source,
              const gchar   *animation,
              gpointer       user_data,
              GError       **error)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autofree gchar         *abs = NULL;
	GPtrArray                *clips;
	const gchar              *want = animation != NULL ? animation : clip_name;
	guint                     i;

	if (source == NULL)
	{
		g_set_error (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_CLIP, "clip '%s' has no source", clip_name);
		return NULL;
	}
	abs = g_path_is_absolute (source) ? g_strdup (source)
	                                  : g_build_filename (lrg_anim_graph_get_base_dir (graph), source, NULL);
	clips = g_hash_table_lookup (self->clip_cache, abs);
	if (clips == NULL)
	{
		clips = lrg_gltf_load_clips (abs, error);
		if (clips == NULL)
			return NULL;
		g_hash_table_insert (self->clip_cache, g_strdup (abs), clips);
	}
	for (i = 0; i < clips->len; i++)
	{
		LrgAnimationClip *c = g_ptr_array_index (clips, i);

		if (g_strcmp0 (lrg_animation_clip_get_name (c), want) == 0)
			return g_object_ref (c);
	}
	g_set_error (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_CLIP, "%s has no animation '%s'", source, want);
	return NULL;
}

/* Pushes a live value into the graph; FALSE when the kinds do not fit. */
static gboolean
apply_live (LrgAnimGraph *graph,
            const gchar  *name,
            GVariant     *value)
{
	guint i, n = lrg_anim_graph_get_parameter_count (graph);

	for (i = 0; i < n; i++)
	{
		if (g_strcmp0 (lrg_anim_graph_get_parameter_name (graph, i), name) != 0)
			continue;
		switch (lrg_anim_graph_get_parameter_type (graph, name))
		{
		case LRG_ANIM_PARAMETER_FLOAT:
			if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
				lrg_anim_graph_set_float (graph, name, (gfloat) g_variant_get_double (value));
			else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
				lrg_anim_graph_set_float (graph, name, (gfloat) g_variant_get_int32 (value));
			else
				return FALSE;
			return TRUE;
		case LRG_ANIM_PARAMETER_INT:
			if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
				lrg_anim_graph_set_int (graph, name, g_variant_get_int32 (value));
			else if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
				lrg_anim_graph_set_int (graph, name, (gint) rint (g_variant_get_double (value)));
			else
				return FALSE;
			return TRUE;
		case LRG_ANIM_PARAMETER_BOOL:
			if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
				return FALSE;
			lrg_anim_graph_set_bool (graph, name, g_variant_get_boolean (value));
			return TRUE;
		case LRG_ANIM_PARAMETER_TRIGGER:
			if (!g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
				return FALSE;
			if (g_variant_get_boolean (value))
				lrg_anim_graph_set_trigger (graph, name);
			else
				lrg_anim_graph_reset_trigger (graph, name);
			return TRUE;
		default:
			return FALSE;
		}
	}
	return FALSE;
}

/* Parses the document into a fresh, started preview graph. */
static void
load_graph (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar        *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autofree gchar        *dir = base_dir_of (self);
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgAnimGraph)  graph = lrg_anim_graph_new_from_data (text, -1, &error);
	GHashTableIter           it;
	gpointer                 key, value;

	g_clear_pointer (&self->load_error, g_free);
	g_clear_object (&self->graph);
	if (graph == NULL)
	{
		self->load_error = g_strdup (error->message);
		return;
	}
	lrg_anim_graph_set_base_dir (graph, dir);
	lrg_anim_graph_set_clip_resolver (graph, resolve_clip, self, NULL);
	/* missing clips are listed by lrg_anim_graph_get_missing_clips() */
	lrg_anim_graph_resolve_clips (graph, NULL);
	lrg_anim_graph_start (graph);
	/* triggers are one-shot: they are not carried over */
	g_hash_table_iter_init (&it, self->live);
	while (g_hash_table_iter_next (&it, &key, &value))
		if (lrg_anim_graph_get_parameter_type (graph, key) != LRG_ANIM_PARAMETER_TRIGGER)
			apply_live (graph, key, value);
	self->graph = g_steal_pointer (&graph);
	self->ptime = 0.0f;
}

/* The runtime machine of a machine path in the preview graph. */
static LrgAnimationStateMachine *
runtime_machine (LrgEditorAnimGraphEditor *self,
                 const gchar              *machine)
{
	g_auto(GStrv)             parts = NULL;
	LrgAnimationLayer        *layer;
	LrgAnimationStateMachine *m;
	g_autofree gchar         *walk = NULL;
	guint                     i;

	if (self->graph == NULL || machine == NULL)
		return NULL;
	parts = lrg_editor_document_path_split (machine, NULL);
	if (parts == NULL || g_strv_length (parts) < 2)
		return NULL;
	layer = lrg_anim_graph_get_layer (self->graph, (guint) g_ascii_strtoull (parts[1], NULL, 10));
	if (layer == NULL)
		return NULL;
	m = lrg_animation_layer_get_state_machine (layer);
	walk = g_strdup_printf ("/layers/%s", parts[1]);
	/* nested: states/<i>/machine, repeated */
	for (i = 2; m != NULL && parts[i] != NULL && parts[i + 1] != NULL && parts[i + 2] != NULL; i += 3)
	{
		g_autofree gchar   *states = pj (walk, "states");
		g_autofree gchar   *sp = pj (states, parts[i + 1]);
		const gchar        *name = scalar_at (self, sp, "name");
		LrgAnimationState  *st = name != NULL ? lrg_animation_state_machine_get_state (m, name) : NULL;

		m = st != NULL ? lrg_animation_state_get_sub_machine (st) : NULL;
		g_free (walk);
		walk = pj (sp, "machine");
	}
	return m;
}

/* The view's pose function: the graph advanced and blended. */
static void
pose_from_graph (gpointer        user_data,
                 LrgSkeleton    *skeleton,
                 LrgSkinnedMesh *mesh,
                 gfloat          dt)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	guint                     n = lrg_skeleton_get_bone_count (skeleton);
	gfloat                    delta[3];
	gfloat                    yaw;

	if (self->graph == NULL)
	{
		lrg_skeleton_reset_to_bind (skeleton);
		return;
	}
	if (n > self->n_poses)
	{
		g_free (self->poses);
		self->poses = g_new0 (LrgBonePose, n);
		self->n_poses = n;
	}
	if (dt > 0.0f)
	{
		lrg_anim_graph_update (self->graph, dt);
		self->ptime += dt;
	}
	lrg_anim_graph_evaluate (self->graph, skeleton, self->poses, n, delta, &yaw);
	lrg_anim_pose_apply (skeleton, self->poses, n);
}

/* ==========================================================================
 * The canvas
 * ========================================================================== */

/* A card body: one or two lines of muted detail ("clip · walk"). */
typedef struct
{
	gchar         *text;
	LrgThemeColor  role;
} CardBody;

static void
card_body_free (gpointer data)
{
	CardBody *b = data;

	g_free (b->text);
	g_free (b);
}

static void
card_body_paint (LrgCustomWidget *widget,
                 LrgPaintList    *list,
                 gfloat           x,
                 gfloat           y,
                 gfloat           width,
                 gfloat           height,
                 gpointer         user_data)
{
	CardBody       *b = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat          fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor *ink = lrg_theme_get_color (theme, b->role);

	/* the card lays the body out at its zoomed size */
	fs = MIN (fs, (height - 4.0f) / 1.2f);
	if (fs < 4.0f)
		return;
	lrg_paint_list_push_clip (list, x, y, width, height);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), b->text, x + 6.0f, y + 2.0f, fs, 0.0f, ink);
	lrg_paint_list_pop_clip (list);
}

static LrgWidget *
card_body_new (const gchar   *text,
               LrgThemeColor  role)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, card_body_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	CardBody        *b = g_new0 (CardBody, 1);

	b->text = g_strdup (text);
	b->role = role;
	lrg_custom_widget_set_funcs (w, &funcs, b, card_body_free);
	lrg_custom_widget_set_preferred_width (w, CARD_W - 20.0f);
	lrg_custom_widget_set_preferred_height (w, 20.0f);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (w), TRUE);
	return LRG_WIDGET (w);
}

/* The detail line of a state card. */
static gchar *
state_detail (LrgEditorAnimGraphEditor *self,
              const gchar              *sp,
              LrgThemeColor            *role)
{
	const gchar *kind = state_kind (self, sp);

	*role = LRG_THEME_COLOR_TEXT_MUTED;
	if (g_strcmp0 (kind, "clip") == 0)
	{
		const gchar      *clip = scalar_at (self, sp, "clip");

		if (self->graph != NULL && clip != NULL && lrg_anim_graph_get_clip (self->graph, clip) == NULL)
		{
			*role = LRG_THEME_COLOR_DANGER;
			return g_strdup_printf ("clip · %s (missing)", clip);
		}
		return g_strdup_printf ("clip · %s", clip != NULL ? clip : "?");
	}
	if (g_strcmp0 (kind, "blend_tree") == 0)
	{
		g_autofree gchar *bt = pj (sp, "blend_tree");
		const gchar      *type = scalar_at (self, bt, "type");
		g_autofree gchar *ch = pj (bt, "children");

		*role = LRG_THEME_COLOR_LINK;
		return g_strdup_printf ("blend %s · %d clips", type != NULL ? type : "1d", n_items (self, ch));
	}
	if (g_strcmp0 (kind, "machine") == 0)
	{
		g_autofree gchar *mp = pj (sp, "machine");
		g_autofree gchar *st = pj (mp, "states");

		*role = LRG_THEME_COLOR_ACCENT;
		return g_strdup_printf ("machine · %d states", n_items (self, st));
	}
	return g_strdup ("empty (bind pose)");
}

/* Reads a 2-number position under @parent/@key; FALSE when missing. */
static gboolean
read_pos (LrgEditorAnimGraphEditor *self,
          const gchar              *parent,
          const gchar              *key,
          gfloat                   *x,
          gfloat                   *y)
{
	g_autofree gchar *p = pj (parent, key);
	g_autofree gchar *p0 = pji (p, 0);
	g_autofree gchar *p1 = pji (p, 1);
	const gchar      *sx = lrg_editor_document_get_scalar (doc_of (self), p0);
	const gchar      *sy = lrg_editor_document_get_scalar (doc_of (self), p1);

	if (sx == NULL || sy == NULL)
		return FALSE;
	*x = (gfloat) g_ascii_strtod (sx, NULL);
	*y = (gfloat) g_ascii_strtod (sy, NULL);
	return TRUE;
}

static void
add_state_ports (LrgGraphNode *card,
                 gboolean      input)
{
	LrgGraphPort *port;

	if (input)
	{
		port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_INPUT, "in", "state");
		lrg_graph_port_set_multi_link (port, TRUE);
	}
	port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_OUTPUT, "out", "state");
	lrg_graph_port_set_multi_link (port, TRUE);
}

/* Rebuilds every card and wire of the shown machine from the document. */
static void
rebuild_canvas (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar *states = pj (self->machine, "states");
	g_autofree gchar *trans = pj (self->machine, "transitions");
	g_autofree gchar *any = pj (self->machine, "any_state");
	const gchar      *def = default_state (self, self->machine);
	gint              n = n_items (self, states);
	gint              i;
	gfloat            ax = -GRID_DX, ay = 0.0f;

	self->syncing = TRUE;
	self->canvas_stale = FALSE;
	g_clear_pointer (&self->hl_sig, g_free);
	lrg_node_graph_clear (self->canvas);

	for (i = 0; i < n; i++)
	{
		g_autofree gchar        *sp = pji (states, (guint) i);
		const gchar             *name = scalar_at (self, sp, "name");
		const gchar             *kind = state_kind (self, sp);
		g_autoptr(LrgGraphNode)  card = NULL;
		g_autofree gchar        *detail = NULL;
		LrgThemeColor            role;
		gfloat                   x, y;

		if (name == NULL)
			continue;
		card = lrg_graph_node_new (name, name);
		lrg_graph_node_set_category (card, g_strcmp0 (kind, "blend_tree") == 0 ? "blend"
		                                   : g_strcmp0 (kind, "machine") == 0 ? "machine"
		                                   : g_strcmp0 (kind, "clip") == 0 ? "clip" : "state");
		lrg_graph_node_set_icon (card, g_strcmp0 (name, def) == 0 ? "star"
		                               : g_strcmp0 (kind, "machine") == 0 ? "menu"
		                               : g_strcmp0 (kind, "blend_tree") == 0 ? "gear" : "play");
		lrg_graph_node_set_min_width (card, CARD_W);
		add_state_ports (card, TRUE);
		detail = state_detail (self, sp, &role);
		{
			g_autoptr(LrgWidget) body = card_body_new (detail, role);

			lrg_graph_node_set_body (card, body);
		}
		if (!read_pos (self, sp, "editor_pos", &x, &y))
		{
			x = (gfloat) (i % GRID_COLS) * GRID_DX;
			y = (gfloat) (i / GRID_COLS) * GRID_DY;
		}
		lrg_graph_node_set_position (card, x, y);
		lrg_node_graph_append_node (self->canvas, card);
	}

	/* the any-state card */
	{
		g_autoptr(LrgGraphNode) card = lrg_graph_node_new (ANY_ID, "Any State");

		lrg_graph_node_set_category (card, "any");
		lrg_graph_node_set_icon (card, "refresh");
		lrg_graph_node_set_min_width (card, 120.0f);
		add_state_ports (card, FALSE);
		read_pos (self, self->machine, "editor_any_pos", &ax, &ay);
		lrg_graph_node_set_position (card, ax, ay);
		lrg_node_graph_append_node (self->canvas, card);
	}

	/* wires */
	n = n_items (self, trans);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *tp = pji (trans, (guint) i);
		const gchar      *from = scalar_at (self, tp, "from");
		const gchar      *to = scalar_at (self, tp, "to");

		if (from != NULL && to != NULL && g_strcmp0 (from, to) != 0)
			lrg_node_graph_connect_by_name (self->canvas, from, "out", to, "in", NULL);
	}
	n = n_items (self, any);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *tp = pji (any, (guint) i);
		const gchar      *to = scalar_at (self, tp, "to");

		if (to != NULL)
			lrg_node_graph_connect_by_name (self->canvas, ANY_ID, "out", to, "in", NULL);
	}

	/* the selection survives */
	if (self->sel_state != NULL)
	{
		LrgGraphNode *card = lrg_node_graph_get_node (self->canvas, self->sel_state);

		if (card != NULL)
			lrg_node_graph_select_node (self->canvas, card, FALSE);
		else
			g_clear_pointer (&self->sel_state, g_free);
	}
	else if (self->sel_from != NULL)
	{
		g_autoptr(GList) links = lrg_node_graph_get_links (self->canvas);
		GList           *l;
		gboolean         found = FALSE;

		for (l = links; l != NULL; l = l->next)
		{
			LrgGraphLink *link = l->data;

			if (g_strcmp0 (lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link))),
			               self->sel_from) == 0 &&
			    g_strcmp0 (lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link))),
			               self->sel_to) == 0)
			{
				lrg_node_graph_select_link (self->canvas, link);
				found = TRUE;
				break;
			}
		}
		if (!found)
		{
			g_clear_pointer (&self->sel_from, g_free);
			g_clear_pointer (&self->sel_to, g_free);
		}
	}
	self->syncing = FALSE;
}

/* Makes sure @parent/@key is a sequence (adds `key: []`). */
static gboolean
ensure_list (LrgEditorAnimGraphEditor  *self,
             const gchar               *parent,
             const gchar               *key,
             GError                   **error)
{
	g_autofree gchar *p = pj (parent, key);

	if (lrg_editor_document_get_kind (doc_of (self), p) == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		return TRUE;
	if (lrg_editor_document_has_path (doc_of (self), p))
		return lrg_editor_document_replace (doc_of (self), p, "[]", error);
	return lrg_editor_document_add_key (doc_of (self), parent, key, "[]", -1, error);
}

/* Writes a 2-number position (editor_pos / editor_any_pos). */
static gboolean
write_pos (LrgEditorAnimGraphEditor *self,
           const gchar              *parent,
           const gchar              *key,
           gfloat                    x,
           gfloat                    y)
{
	g_autofree gchar *p = pj (parent, key);
	g_autofree gchar *sx = num_str (rint (x));
	g_autofree gchar *sy = num_str (rint (y));
	g_autofree gchar *text = g_strdup_printf ("[%s, %s]", sx, sy);

	if (lrg_editor_document_has_path (doc_of (self), p))
		return lrg_editor_document_replace (doc_of (self), p, text, NULL);
	return lrg_editor_document_add_key (doc_of (self), parent, key, text, -1, NULL);
}

/* Removes a state, its transitions and its `default:` mention (no group). */
static gboolean
remove_state_raw (LrgEditorAnimGraphEditor *self,
                  const gchar              *machine,
                  const gchar              *name)
{
	g_autofree gchar *sp = state_path_in (self, machine, name);
	g_autofree gchar *trans = pj (machine, "transitions");
	g_autofree gchar *any = pj (machine, "any_state");
	gint              i;

	if (sp == NULL)
		return FALSE;
	for (i = n_items (self, trans) - 1; i >= 0; i--)
	{
		g_autofree gchar *tp = pji (trans, (guint) i);

		if (g_strcmp0 (scalar_at (self, tp, "from"), name) == 0 || g_strcmp0 (scalar_at (self, tp, "to"), name) == 0)
			lrg_editor_document_remove (doc_of (self), tp, NULL);
	}
	for (i = n_items (self, any) - 1; i >= 0; i--)
	{
		g_autofree gchar *tp = pji (any, (guint) i);

		if (g_strcmp0 (scalar_at (self, tp, "to"), name) == 0)
			lrg_editor_document_remove (doc_of (self), tp, NULL);
	}
	if (g_strcmp0 (scalar_at (self, machine, "default"), name) == 0)
	{
		g_autofree gchar *dp = pj (machine, "default");

		lrg_editor_document_remove (doc_of (self), dp, NULL);
	}
	return lrg_editor_document_remove (doc_of (self), sp, NULL);
}

/* Adds a transition item (no group). */
static gboolean
add_transition_raw (LrgEditorAnimGraphEditor  *self,
                    const gchar               *machine,
                    const gchar               *from,
                    const gchar               *to,
                    gfloat                     duration,
                    GError                   **error)
{
	gboolean          any = g_strcmp0 (from, ANY_ID) == 0;
	g_autofree gchar *yf = yaml_str (from);
	g_autofree gchar *yt = yaml_str (to);
	g_autofree gchar *yd = num_str (duration);
	g_autofree gchar *text = any ? g_strdup_printf ("{to: %s, duration: %s}", yt, yd)
	                             : g_strdup_printf ("{from: %s, to: %s, duration: %s}", yf, yt, yd);
	g_autofree gchar *list = pj (machine, any ? "any_state" : "transitions");

	if (!ensure_list (self, machine, any ? "any_state" : "transitions", error))
		return FALSE;
	return lrg_editor_document_insert_item (doc_of (self), list, -1, text, error);
}

/* --- canvas -> document ------------------------------------------------- */

static gboolean
on_can_connect (LrgNodeGraph *graph,
                LrgGraphPort *output,
                LrgGraphPort *input,
                gpointer      user_data)
{
	/* any state to any state except into the any-state card or itself */
	return lrg_graph_port_get_node (output) != lrg_graph_port_get_node (input) &&
	       g_strcmp0 (lrg_graph_node_get_id (lrg_graph_port_get_node (input)), ANY_ID) != 0;
}

static void
on_link_added (LrgNodeGraph *graph,
               LrgGraphLink *link,
               gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	const gchar              *from = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
	const gchar              *to = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));
	g_autoptr(GError)         error = NULL;

	if (self->syncing)
		return;
	self->writing = TRUE;
	canvas_group (self, "Add Transition");
	if (!add_transition_raw (self, self->machine, from, to, 0.25f, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	self->writing = FALSE;
	g_clear_pointer (&self->sel_state, g_free);
	g_free (self->sel_from);
	g_free (self->sel_to);
	self->sel_from = g_strdup (from);
	self->sel_to = g_strdup (to);
	self->grid_stale = TRUE;
}

static void
on_link_removed (LrgNodeGraph *graph,
                 LrgGraphLink *link,
                 gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	const gchar              *from = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
	const gchar              *to = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));
	g_autofree gchar         *tp = NULL;

	if (self->syncing)
		return;
	tp = transition_path_in (self, self->machine, from, to);
	if (tp == NULL)
		return;
	self->writing = TRUE;
	canvas_group (self, "Delete Transition");
	lrg_editor_document_remove (doc_of (self), tp, NULL);
	self->writing = FALSE;
	if (g_strcmp0 (self->sel_from, from) == 0 && g_strcmp0 (self->sel_to, to) == 0)
	{
		g_clear_pointer (&self->sel_from, g_free);
		g_clear_pointer (&self->sel_to, g_free);
	}
	self->grid_stale = TRUE;
}

static void
on_node_moved (LrgNodeGraph *graph,
               LrgGraphNode *card,
               gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	const gchar              *id = lrg_graph_node_get_id (card);
	g_autofree gchar         *sp = NULL;

	if (self->syncing)
		return;
	self->writing = TRUE;
	canvas_group (self, "Move State");
	if (g_strcmp0 (id, ANY_ID) == 0)
		write_pos (self, self->machine, "editor_any_pos", lrg_graph_node_get_x (card), lrg_graph_node_get_y (card));
	else if ((sp = state_path_in (self, self->machine, id)) != NULL)
		write_pos (self, sp, "editor_pos", lrg_graph_node_get_x (card), lrg_graph_node_get_y (card));
	self->writing = FALSE;
}

static void
on_node_removed (LrgNodeGraph *graph,
                 LrgGraphNode *card,
                 gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	const gchar              *id = lrg_graph_node_get_id (card);

	if (self->syncing)
		return;
	/* the any-state card cannot go: put it back */
	if (g_strcmp0 (id, ANY_ID) == 0)
	{
		self->canvas_stale = TRUE;
		return;
	}
	self->writing = TRUE;
	canvas_group (self, "Delete State");
	remove_state_raw (self, self->machine, id);
	self->writing = FALSE;
	if (g_strcmp0 (self->sel_state, id) == 0)
		g_clear_pointer (&self->sel_state, g_free);
	/* the default star may have moved to the first state */
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
}

/* Ctrl+D: the copy becomes a state named "<name>_copy" with its settings. */
static void
on_node_duplicated (LrgNodeGraph *graph,
                    LrgGraphNode *original,
                    LrgGraphNode *copy,
                    gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	const gchar              *id = lrg_graph_node_get_id (original);
	g_autofree gchar         *sp = NULL;
	g_autofree gchar         *yaml = NULL;
	g_autofree gchar         *base = NULL;
	g_autofree gchar         *name = NULL;
	g_autofree gchar         *states = pj (self->machine, "states");
	g_autofree gchar         *np = NULL;
	gint                      n;

	if (self->syncing || g_strcmp0 (id, ANY_ID) == 0 || (sp = state_path_in (self, self->machine, id)) == NULL)
	{
		self->canvas_stale = TRUE;
		return;
	}
	yaml = lrg_editor_document_get_yaml (doc_of (self), sp);
	base = g_strconcat (id, "_copy", NULL);
	name = free_state_name (self, base);
	self->writing = TRUE;
	canvas_group (self, "Duplicate State");
	n = n_items (self, states);
	if (yaml != NULL && lrg_editor_document_insert_item (doc_of (self), states, -1, yaml, NULL))
	{
		np = pji (states, (guint) n);
		{
			g_autofree gchar *namep = pj (np, "name");

			lrg_editor_document_set_string (doc_of (self), namep, name, 0, NULL);
		}
		write_pos (self, np, "editor_pos", lrg_graph_node_get_x (copy), lrg_graph_node_get_y (copy));
	}
	self->writing = FALSE;
	self->canvas_stale = TRUE;
}

/* A wire dropped on empty canvas: a new state there, connected. */
static void
on_link_dropped (LrgNodeGraph *graph,
                 LrgGraphPort *port,
                 gfloat        x,
                 gfloat        y,
                 gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	const gchar              *from = lrg_graph_node_get_id (lrg_graph_port_get_node (port));
	g_autofree gchar         *name = NULL;

	if (self->syncing || lrg_graph_port_get_direction (port) != LRG_GRAPH_PORT_OUTPUT)
		return;
	name = free_state_name (self, "state");
	if (lrg_editor_anim_graph_editor_add_state (self, name, "empty", NULL, x, y, NULL))
	{
		lrg_editor_anim_graph_editor_add_transition (self, from, name, 0.25f, NULL);
		lrg_editor_anim_graph_editor_select_state (self, name);
	}
}

static void
on_node_activated (LrgNodeGraph *graph,
                   LrgGraphNode *card,
                   gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	lrg_editor_anim_graph_editor_enter_state (self, lrg_graph_node_get_id (card));
}

static void
on_canvas_selection (LrgNodeGraph *graph,
                     gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autoptr(GList)          sel = NULL;
	LrgGraphLink             *link;
	const gchar              *id = NULL;

	if (self->syncing)
		return;
	sel = lrg_node_graph_get_selected_nodes (graph);
	link = lrg_node_graph_get_selected_link (graph);
	if (sel != NULL && sel->next == NULL)
		id = lrg_graph_node_get_id (sel->data);
	if (g_strcmp0 (id, ANY_ID) == 0)
		id = NULL;
	g_clear_pointer (&self->sel_state, g_free);
	g_clear_pointer (&self->sel_from, g_free);
	g_clear_pointer (&self->sel_to, g_free);
	if (id != NULL)
		self->sel_state = g_strdup (id);
	else if (link != NULL)
	{
		self->sel_from = g_strdup (lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link))));
		self->sel_to = g_strdup (lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link))));
	}
	self->grid_stale = TRUE;
}

/* Active state, blending state and running transition, every frame. */
static void
update_highlights (LrgEditorAnimGraphEditor *self)
{
	LrgAnimationStateMachine *m = runtime_machine (self, self->machine);
	const gchar              *cur = NULL;
	const gchar              *nxt = NULL;
	const gchar              *tf = NULL;
	const gchar              *tt = NULL;
	g_autofree gchar         *sig = NULL;
	LrgTheme                 *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	guint                     i, n;

	if (m != NULL)
	{
		LrgAnimationState      *ns = lrg_animation_state_machine_get_next_state (m);
		LrgAnimationTransition *tr = lrg_animation_state_machine_get_active_transition (m);

		cur = lrg_animation_state_machine_get_current_state_name (m);
		nxt = ns != NULL ? lrg_animation_state_get_name (ns) : NULL;
		if (tr != NULL)
		{
			tf = lrg_animation_transition_is_any_state (tr) ? ANY_ID : lrg_animation_transition_get_source (tr);
			tt = lrg_animation_transition_get_target (tr);
		}
	}
	sig = g_strdup_printf ("%s|%s|%s>%s", cur, nxt, tf, tt);
	if (g_strcmp0 (sig, self->hl_sig) == 0)
		return;
	g_free (self->hl_sig);
	self->hl_sig = g_steal_pointer (&sig);

	n = lrg_node_graph_get_n_nodes (self->canvas);
	for (i = 0; i < n; i++)
	{
		LrgGraphNode *card = lrg_node_graph_get_nth_node (self->canvas, i);
		const gchar  *id = lrg_graph_node_get_id (card);

		if (g_strcmp0 (id, cur) == 0)
			lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
		else if (g_strcmp0 (id, nxt) == 0)
			lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
		else
			lrg_graph_node_set_accent (card, NULL);
	}
	{
		g_autoptr(GList) links = lrg_node_graph_get_links (self->canvas);
		GList           *l;

		for (l = links; l != NULL; l = l->next)
		{
			LrgGraphLink *link = l->data;
			const gchar  *a = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
			const gchar  *b = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));

			lrg_graph_link_set_color (link, (tf != NULL && g_strcmp0 (a, tf) == 0 && g_strcmp0 (b, tt) == 0)
			                                ? lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS) : NULL);
		}
	}
}

/* ==========================================================================
 * Chrome: layer chips and breadcrumbs
 * ========================================================================== */

static void
on_layer_chip (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	gint                      index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (chip), "layer")) - 1;

	if (index >= 0)
		lrg_editor_anim_graph_editor_set_layer (self, (guint) index);
	else
	{
		/* the "+" chip: a new layer with a free name */
		guint n;

		for (n = n_layers (self) + 1; n < 1000; n++)
		{
			g_autofree gchar *name = g_strdup_printf ("layer%u", n);

			if (lrg_editor_anim_graph_editor_add_layer (self, name, NULL))
			{
				lrg_editor_anim_graph_editor_set_layer (self, n_layers (self) - 1);
				break;
			}
		}
	}
}

static void
on_crumb (LrgBreadcrumb *crumbs,
          guint          index,
          gpointer       user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	guint                     depth = lrg_breadcrumb_get_n_segments (crumbs);

	/* segment 0 is the layer; every later one a nested machine */
	while (depth > index + 1 && lrg_editor_anim_graph_editor_go_up (self))
		depth--;
}

/* Layer chips and the breadcrumb path (deferred to the next step). */
static void
rebuild_chrome (LrgEditorAnimGraphEditor *self)
{
	guint         n = n_layers (self);
	guint         cur = machine_layer (self->machine);
	g_auto(GStrv) crumbs = lrg_editor_anim_graph_editor_get_breadcrumbs (self);
	guint         i;

	lrg_container_remove_all (LRG_CONTAINER (self->layer_bar));
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *lp = pji ("/layers", i);
		const gchar      *name = scalar_at (self, lp, "name");
		g_autofree gchar *cname = g_strdup_printf ("anim-layer-%u", i);
		LrgWidget        *chip = editor_chip_new (cname, LRG_ICON_MENU, name != NULL ? name : "layer", on_layer_chip,
		                                          self);

		g_object_set_data (G_OBJECT (chip), "layer", GINT_TO_POINTER ((gint) i + 1));
		editor_chip_set_checked (chip, i == cur);
		editor_chip_set_tint (chip, LRG_THEME_COLOR_ACCENT);
		lrg_container_add_child (LRG_CONTAINER (self->layer_bar), chip);
		g_object_unref (chip);
	}
	{
		LrgWidget *chip = editor_chip_new ("anim-layer-add", LRG_ICON_PLUS, "Layer", on_layer_chip, self);

		lrg_container_add_child (LRG_CONTAINER (self->layer_bar), chip);
		g_object_unref (chip);
	}

	self->syncing = TRUE;
	lrg_breadcrumb_remove_all (self->crumbs);
	for (i = 0; crumbs != NULL && crumbs[i] != NULL; i++)
		lrg_breadcrumb_append (self->crumbs, crumbs[i]);
	self->syncing = FALSE;
}

/* ==========================================================================
 * Left column: parameters with live values, clip slots
 * ========================================================================== */

static const gchar *
row_param (gpointer widget)
{
	return g_object_get_data (G_OBJECT (widget), "param");
}

static void
on_param_float (LrgSpinBox *spin,
                gpointer    user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_anim_graph_editor_set_parameter_value (self, row_param (spin),
		                                                  g_variant_new_double (lrg_spin_box_get_value (spin)));
}

static void
on_param_int (LrgSpinBox *spin,
              gpointer    user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_anim_graph_editor_set_parameter_value (self, row_param (spin),
		                                                  g_variant_new_int32 ((gint32) rint (lrg_spin_box_get_value (spin))));
}

static void
on_param_bool (LrgCheckbox *check,
               gboolean     checked,
               gpointer     user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_anim_graph_editor_set_parameter_value (self, row_param (check), g_variant_new_boolean (checked));
}

static void
on_param_fire (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	lrg_editor_anim_graph_editor_set_parameter_value (self, row_param (button), g_variant_new_boolean (TRUE));
}

static void
on_param_remove (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	/* the rows are rebuilt on the next step, not inside this emission */
	lrg_editor_anim_graph_editor_remove_parameter (self, row_param (button));
}

static void
on_param_add (LrgButton *button,
              gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	const gchar              *type = g_object_get_data (G_OBJECT (button), "type");
	guint                     n;

	for (n = 1; n < 1000; n++)
	{
		g_autofree gchar *name = n == 1 ? g_strdup (type) : g_strdup_printf ("%s%u", type, n);

		if (parameter_type (self, name) == NULL)
		{
			lrg_editor_anim_graph_editor_add_parameter (self, name, type, NULL);
			return;
		}
	}
}

static void
on_clips_from_model (LrgButton *button,
                     gpointer   user_data)
{
	lrg_editor_anim_graph_editor_declare_model_clips (LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data), NULL);
}

static LrgWidget *
small_button (const gchar *name,
              const gchar *text,
              GCallback    callback,
              gpointer     user_data)
{
	LrgButton *b = lrg_button_new (text);

	lrg_widget_set_name (LRG_WIDGET (b), name);
	g_signal_connect (b, "clicked", callback, user_data);
	return LRG_WIDGET (b);
}

static LrgWidget *
heading_label (const gchar *text)
{
	LrgLabel *l = lrg_label_new (text);

	lrg_widget_add_class (LRG_WIDGET (l), "dim-label");
	return LRG_WIDGET (l);
}

/* What the rows show; rebuilt only when it changes. */
static gchar *
rows_signature (LrgEditorAnimGraphEditor *self)
{
	GString       *s = g_string_new (NULL);
	g_auto(GStrv)  keys = clip_keys (self);
	gint           n = n_items (self, "/parameters");
	gint           i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *pp = pji ("/parameters", (guint) i);

		g_string_append_printf (s, "%s:%s;", scalar_at (self, pp, "name"), scalar_at (self, pp, "type"));
	}
	g_string_append_c (s, '|');
	for (i = 0; keys[i] != NULL; i++)
	{
		g_autofree gchar *cp = pj ("/clips", keys[i]);
		gboolean          missing = self->graph != NULL && lrg_anim_graph_get_clip (self->graph, keys[i]) == NULL;

		g_string_append_printf (s, "%s=%s#%s%s;", keys[i], scalar_at (self, cp, "source"),
		                        scalar_at (self, cp, "animation"), missing ? "!" : "");
	}
	return g_string_free (s, FALSE);
}

static void
rebuild_rows (LrgEditorAnimGraphEditor *self)
{
	LrgTheme      *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	g_auto(GStrv)  keys = clip_keys (self);
	gint           n = n_items (self, "/parameters");
	gint           i;

	self->rows_stale = FALSE;
	g_free (self->rows_sig);
	self->rows_sig = rows_signature (self);
	self->syncing = TRUE;

	/* --- parameters --- */
	lrg_container_remove_all (LRG_CONTAINER (self->param_rows));
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *pp = pji ("/parameters", (guint) i);
		const gchar      *name = scalar_at (self, pp, "name");
		const gchar      *type = scalar_at (self, pp, "type");
		g_autoptr(GVariant) v = name != NULL ? lrg_editor_anim_graph_editor_get_parameter_value (self, name) : NULL;
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		LrgWidget        *label;
		LrgWidget        *control = NULL;
		LrgWidget        *rm;
		g_autofree gchar *rname = g_strdup_printf ("anim-param-%s", name);
		g_autofree gchar *cname = g_strdup_printf ("anim-param-%s-value", name);
		g_autofree gchar *xname = g_strdup_printf ("anim-param-%s-remove", name);
		g_autofree gchar *text = g_strdup_printf ("%s  %s", name, type != NULL ? type : "?");

		if (name == NULL)
		{
			g_object_unref (row);
			continue;
		}
		lrg_widget_set_name (row, rname);
		lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
		label = LRG_WIDGET (lrg_label_new (text));
		lrg_widget_set_fill_weight (label, 1.0f);
		lrg_container_add_child (LRG_CONTAINER (row), label);
		g_object_unref (label);
		if (g_strcmp0 (type, "float") == 0 || g_strcmp0 (type, "int") == 0)
		{
			gboolean    is_int = g_strcmp0 (type, "int") == 0;
			LrgSpinBox *spin = lrg_spin_box_new_with_range (-1000.0, 1000.0, is_int ? 1.0 : 0.05);

			lrg_spin_box_set_digits (spin, is_int ? 0 : 2);
			if (v != NULL)
				lrg_spin_box_set_value (spin, g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE)
				                              ? g_variant_get_double (v) : (gdouble) g_variant_get_int32 (v));
			g_signal_connect (spin, "value-changed", is_int ? G_CALLBACK (on_param_int) : G_CALLBACK (on_param_float),
			                  self);
			control = LRG_WIDGET (spin);
			lrg_widget_set_min_size (control, 72.0f, 0.0f);
		}
		else if (g_strcmp0 (type, "bool") == 0)
		{
			LrgCheckbox *check = lrg_checkbox_new (NULL);

			lrg_checkbox_set_checked (check, v != NULL && g_variant_get_boolean (v));
			g_signal_connect (check, "toggled", G_CALLBACK (on_param_bool), self);
			control = LRG_WIDGET (check);
		}
		else
			control = small_button (cname, "Fire", G_CALLBACK (on_param_fire), self);
		lrg_widget_set_name (control, cname);
		g_object_set_data_full (G_OBJECT (control), "param", g_strdup (name), g_free);
		lrg_container_add_child (LRG_CONTAINER (row), control);
		g_object_unref (control);
		rm = small_button (xname, "\xc3\x97", G_CALLBACK (on_param_remove), self);
		g_object_set_data_full (G_OBJECT (rm), "param", g_strdup (name), g_free);
		lrg_container_add_child (LRG_CONTAINER (row), rm);
		g_object_unref (rm);
		lrg_container_add_child (LRG_CONTAINER (self->param_rows), row);
		g_object_unref (row);
	}
	if (n == 0)
	{
		LrgWidget *hint = heading_label ("No parameters yet");

		lrg_container_add_child (LRG_CONTAINER (self->param_rows), hint);
		g_object_unref (hint);
	}

	/* --- clip slots --- */
	lrg_container_remove_all (LRG_CONTAINER (self->clip_rows));
	for (i = 0; keys[i] != NULL; i++)
	{
		g_autofree gchar *cp = pj ("/clips", keys[i]);
		const gchar      *source = scalar_at (self, cp, "source");
		const gchar      *anim = scalar_at (self, cp, "animation");
		gboolean          missing = self->graph != NULL && lrg_anim_graph_get_clip (self->graph, keys[i]) == NULL;
		g_autofree gchar *text = source != NULL
		                         ? g_strdup_printf ("%s%s  \xe2\x86\x90 %s \xc2\xb7 %s", missing ? "\xe2\x9a\xa0 " : "",
		                                            keys[i], source, anim != NULL ? anim : keys[i])
		                         : g_strdup_printf ("%s%s  (bound in code)", missing ? "\xe2\x9a\xa0 " : "", keys[i]);
		LrgLabel         *label = lrg_label_new (text);
		g_autofree gchar *lname = g_strdup_printf ("anim-clip-%s", keys[i]);

		lrg_widget_set_name (LRG_WIDGET (label), lname);
		if (missing)
			lrg_label_set_color (label, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
		lrg_container_add_child (LRG_CONTAINER (self->clip_rows), LRG_WIDGET (label));
		g_object_unref (label);
	}
	if (keys[0] == NULL)
	{
		LrgWidget *hint = heading_label ("No clip slots: add them from the model");

		lrg_container_add_child (LRG_CONTAINER (self->clip_rows), hint);
		g_object_unref (hint);
	}
	self->syncing = FALSE;
}

/* Bool / trigger controls follow the graph (a trigger drops when consumed). */
static void
sync_live_controls (LrgEditorAnimGraphEditor *self)
{
	guint i, n;

	if (self->graph == NULL)
		return;
	n = lrg_anim_graph_get_parameter_count (self->graph);
	self->syncing = TRUE;
	for (i = 0; i < n; i++)
	{
		const gchar      *name = lrg_anim_graph_get_parameter_name (self->graph, i);
		g_autofree gchar *cname = g_strdup_printf ("anim-param-%s-value", name);
		LrgWidget        *w = lrg_widget_find (self->param_rows, cname);

		if (w != NULL && LRG_IS_CHECKBOX (w) &&
		    lrg_checkbox_get_checked (LRG_CHECKBOX (w)) != lrg_anim_graph_get_bool (self->graph, name))
			lrg_checkbox_set_checked (LRG_CHECKBOX (w), lrg_anim_graph_get_bool (self->graph, name));
		/* numbers set elsewhere (IPC, the blend space) show up too,
		 * unless the user is dragging this very spin box */
		else if (w != NULL && LRG_IS_SPIN_BOX (w) && !lrg_spin_box_get_scrubbing (LRG_SPIN_BOX (w)))
		{
			gdouble v = lrg_anim_graph_get_parameter_type (self->graph, name) == LRG_ANIM_PARAMETER_INT
			            ? (gdouble) lrg_anim_graph_get_int (self->graph, name)
			            : (gdouble) lrg_anim_graph_get_float (self->graph, name);

			if (fabs (lrg_spin_box_get_value (LRG_SPIN_BOX (w)) - v) > 1e-4)
				lrg_spin_box_set_value (LRG_SPIN_BOX (w), v);
		}
	}
	self->syncing = FALSE;
}

/* ==========================================================================
 * Inspector: the property grid over the selection
 * ========================================================================== */

/* Creates missing mapping parents of @path (root_motion:, blend_tree: ...). */
static void
ensure_parents (LrgEditorAnimGraphEditor *self,
                const gchar              *path)
{
	g_auto(GStrv)     parts = lrg_editor_document_path_split (path, NULL);
	g_autofree gchar *walk = g_strdup ("");
	guint             i;

	for (i = 0; parts != NULL && parts[i] != NULL && parts[i + 1] != NULL; i++)
	{
		g_autofree gchar *next = pj (walk, parts[i]);

		if (!lrg_editor_document_has_path (doc_of (self), next))
		{
			gboolean numeric = g_ascii_isdigit (parts[i + 1][0]);

			if (!lrg_editor_document_add_key (doc_of (self), walk, parts[i], numeric ? "[0, 0]" : "{}", -1, NULL))
				return;
		}
		g_free (walk);
		walk = g_steal_pointer (&next);
	}
}

/* The path of the selected state, or NULL. */
static gchar *
sel_state_path (LrgEditorAnimGraphEditor *self)
{
	return self->sel_state != NULL ? state_path_in (self, self->machine, self->sel_state) : NULL;
}

/* Converts the selected state to another kind (one undo step). */
static void
convert_state (LrgEditorAnimGraphEditor *self,
               const gchar              *sp,
               const gchar              *kind)
{
	static const gchar * const keys[] = { "clip", "blend_tree", "machine" };
	g_auto(GStrv)               clips = clip_keys (self);
	guint                       i;

	if (g_strcmp0 (state_kind (self, sp), kind) == 0)
		return;
	edit_begin (self, "Change State Kind");
	for (i = 0; i < G_N_ELEMENTS (keys); i++)
		if (has_key (self, sp, keys[i]))
		{
			g_autofree gchar *kp = pj (sp, keys[i]);

			lrg_editor_document_remove (doc_of (self), kp, NULL);
		}
	if (g_strcmp0 (kind, "clip") == 0 && clips[0] != NULL)
	{
		g_autofree gchar *v = yaml_str (clips[0]);

		lrg_editor_document_add_key (doc_of (self), sp, "clip", v, 1, NULL);
	}
	else if (g_strcmp0 (kind, "blend_tree") == 0)
	{
		g_autoptr(GPtrArray) nums = parameter_names (self, TRUE);
		g_autofree gchar    *text = NULL;

		if (nums->len == 0)
		{
			lrg_editor_anim_graph_editor_add_parameter (self, "blend", "float", NULL);
			g_ptr_array_add (nums, g_strdup ("blend"));
		}
		text = g_strdup_printf ("{type: 1d, parameter: %s, children: []}", (const gchar *) g_ptr_array_index (nums, 0));
		lrg_editor_document_add_key (doc_of (self), sp, "blend_tree", text, 1, NULL);
	}
	else if (g_strcmp0 (kind, "machine") == 0)
		lrg_editor_document_add_key (doc_of (self), sp, "machine", "{default: entry, states: [{name: entry}]}", 1,
		                             NULL);
	edit_end (self);
}

static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autofree gchar         *sp = sel_state_path (self);

	if (g_strcmp0 (name, "state-kind") == 0 && sp != NULL)
	{
		g_value_set_string (value, state_kind (self, sp));
		return TRUE;
	}
	if (g_strcmp0 (name, "state-default") == 0 && sp != NULL)
	{
		g_value_set_boolean (value, g_strcmp0 (default_state (self, self->machine), self->sel_state) == 0);
		return TRUE;
	}
	if (g_strcmp0 (name, "tr-from") == 0)
	{
		g_value_set_string (value, g_strcmp0 (self->sel_from, ANY_ID) == 0 ? "Any State" : self->sel_from);
		return TRUE;
	}
	if (g_strcmp0 (name, "tr-to") == 0)
	{
		g_value_set_string (value, self->sel_to);
		return TRUE;
	}
	if (g_strcmp0 (name, "tr-has-exit") == 0)
	{
		g_autofree gchar *tp = lrg_editor_anim_graph_editor_get_selected_transition (self);

		g_value_set_boolean (value, tp != NULL && has_key (self, tp, "exit_time"));
		return TRUE;
	}
	if (g_strcmp0 (name, "state-speed-param") == 0 && sp != NULL)
	{
		const gchar *v = scalar_at (self, sp, "speed_parameter");

		g_value_set_string (value, v != NULL ? v : "(none)");
		return TRUE;
	}
	if (g_strcmp0 (name, "layer-mask") == 0)
	{
		g_autofree gchar *lp = pji ("/layers", machine_layer (self->machine));
		g_autofree gchar *mp = pj (lp, "mask");
		gint              n = n_items (self, mp);
		GString          *s = g_string_new (NULL);
		gint              i;

		for (i = 0; i < n; i++)
		{
			g_autofree gchar *ip = pji (mp, (guint) i);

			g_string_append_printf (s, "%s%s", i > 0 ? ", " : "", lrg_editor_document_get_scalar (doc_of (self), ip));
		}
		g_value_take_string (value, g_string_free (s, FALSE));
		return TRUE;
	}
	return FALSE;
}

static gboolean
source_write (LrgEditorDataSource *source,
              const gchar         *name,
              GValue              *value,
              gpointer             user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autofree gchar         *sp = sel_state_path (self);
	const gchar              *path = lrg_editor_data_source_get_field_path (source, name);

	if (g_strcmp0 (name, "state-name") == 0)
	{
		g_autoptr(GError) error = NULL;

		if (self->sel_state != NULL &&
		    !lrg_editor_anim_graph_editor_rename_state (self, self->sel_state, g_value_get_string (value), &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	if (g_strcmp0 (name, "state-kind") == 0)
	{
		if (sp != NULL)
			convert_state (self, sp, g_value_get_string (value));
		self->grid_stale = TRUE;
		self->canvas_stale = TRUE;
		return FALSE;
	}
	if (g_strcmp0 (name, "state-default") == 0)
	{
		if (g_value_get_boolean (value) && self->sel_state != NULL)
			lrg_editor_anim_graph_editor_set_default_state (self, self->sel_state);
		return FALSE;
	}
	if (g_strcmp0 (name, "tr-from") == 0 || g_strcmp0 (name, "tr-to") == 0)
		return FALSE;
	if (g_strcmp0 (name, "tr-has-exit") == 0)
	{
		g_autofree gchar *tp = lrg_editor_anim_graph_editor_get_selected_transition (self);
		g_autofree gchar *ep = tp != NULL ? pj (tp, "exit_time") : NULL;

		if (tp == NULL)
			return FALSE;
		if (g_value_get_boolean (value) && !has_key (self, tp, "exit_time"))
			lrg_editor_document_add_key (doc_of (self), tp, "exit_time", "0.9", -1, NULL);
		else if (!g_value_get_boolean (value) && has_key (self, tp, "exit_time"))
			lrg_editor_document_remove (doc_of (self), ep, NULL);
		self->grid_stale = TRUE;
		return FALSE;
	}
	if (g_strcmp0 (name, "state-speed-param") == 0 && sp != NULL)
	{
		g_autofree gchar *pp = pj (sp, "speed_parameter");

		if (g_strcmp0 (g_value_get_string (value), "(none)") == 0 || g_value_get_string (value) == NULL ||
		    *g_value_get_string (value) == '\0')
		{
			if (lrg_editor_document_has_path (doc_of (self), pp))
				lrg_editor_document_remove (doc_of (self), pp, NULL);
		}
		else
			lrg_editor_document_set_string (doc_of (self), pp, g_value_get_string (value), 0, NULL);
		return FALSE;
	}
	if (g_strcmp0 (name, "layer-mask") == 0)
	{
		g_autofree gchar *lp = pji ("/layers", machine_layer (self->machine));
		g_autofree gchar *mp = pj (lp, "mask");
		g_auto(GStrv)     bones = g_strsplit (g_value_get_string (value) != NULL ? g_value_get_string (value) : "", ",",
		                                      -1);
		GString          *s = g_string_new ("[");
		guint             i, k = 0;

		for (i = 0; bones[i] != NULL; i++)
		{
			g_autofree gchar *b = g_strstrip (g_strdup (bones[i]));
			g_autofree gchar *yb = NULL;

			if (*b == '\0')
				continue;
			yb = yaml_str (b);
			g_string_append_printf (s, "%s%s", k++ > 0 ? ", " : "", yb);
		}
		g_string_append_c (s, ']');
		if (k == 0)
		{
			if (lrg_editor_document_has_path (doc_of (self), mp))
				lrg_editor_document_remove (doc_of (self), mp, NULL);
		}
		else if (lrg_editor_document_has_path (doc_of (self), mp))
			lrg_editor_document_replace (doc_of (self), mp, s->str, NULL);
		else
			lrg_editor_document_add_key (doc_of (self), lp, "mask", s->str, -1, NULL);
		g_string_free (s, TRUE);
		return FALSE;
	}
	/* 2D blend positions live in a two-number list */
	if (g_str_has_prefix (name, "child") && (g_str_has_suffix (name, "-x") || g_str_has_suffix (name, "-y")) &&
	    path != NULL)
	{
		g_autofree gchar *list = g_path_get_dirname (path);

		if (!lrg_editor_document_has_path (doc_of (self), list))
			ensure_parents (self, path);
		return TRUE;
	}
	/* nested mappings (root_motion:, blend_tree:) are created on demand */
	if (path != NULL && !lrg_editor_document_has_path (doc_of (self), path))
		ensure_parents (self, path);
	return TRUE;
}

/* What the grid shows, so a refresh only rebuilds when this changes. */
static gchar *
grid_signature (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar *sp = sel_state_path (self);
	g_autofree gchar *rows = rows_signature (self);
	GString          *s = g_string_new (NULL);

	g_string_append_printf (s, "%s|%s|%s>%s|%s", self->machine, sp, self->sel_from, self->sel_to, rows);
	if (sp != NULL)
	{
		g_autofree gchar *bt = pj (sp, "blend_tree");
		g_autofree gchar *ch = pj (bt, "children");

		g_string_append_printf (s, "|%s|%s|%d", state_kind (self, sp), scalar_at (self, bt, "type"),
		                        n_items (self, ch));
	}
	return g_string_free (s, FALSE);
}

static void
add_field (LrgEditorAnimGraphEditor *self,
           const gchar              *name,
           const gchar              *parent,
           const gchar              *key,
           GType                     type,
           const gchar              *label,
           const gchar              *group,
           const gchar              *hint,
           const gchar              *def)
{
	g_autofree gchar *path = key != NULL ? pj (parent, key) : g_strdup (parent);

	lrg_editor_data_source_add_field (self->source, name, path, type, label, group, hint, def);
}

static void
build_state_fields (LrgEditorAnimGraphEditor *self,
                    const gchar              *sp)
{
	const gchar         *kind = state_kind (self, sp);
	g_autoptr(GPtrArray) clips = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GPtrArray) nums = parameter_names (self, TRUE);
	g_auto(GStrv)        keys = clip_keys (self);
	g_autofree gchar    *clip_hint = NULL;
	g_autofree gchar    *num_hint = enum_hint (nums, FALSE);
	g_autofree gchar    *speed_hint = enum_hint (nums, TRUE);
	guint                i;

	for (i = 0; keys[i] != NULL; i++)
		g_ptr_array_add (clips, g_strdup (keys[i]));
	clip_hint = enum_hint (clips, FALSE);

	add_field (self, "state-name", sp, "name", G_TYPE_STRING, "Name", "State", NULL, NULL);
	add_field (self, "state-kind", sp, NULL, G_TYPE_STRING, "Plays", "State",
	           "enum-strings:clip,blend_tree,machine,empty", "empty");
	add_field (self, "state-default", sp, NULL, G_TYPE_BOOLEAN, "Default state", "State", NULL, "false");
	if (g_strcmp0 (kind, "clip") == 0)
		add_field (self, "state-clip", sp, "clip", G_TYPE_STRING, "Clip", "State", clip_hint, NULL);
	add_field (self, "state-speed", sp, "speed", G_TYPE_DOUBLE, "Speed", "State", "range:0,4,0.05", "1");
	add_field (self, "state-speed-param", sp, "speed_parameter", G_TYPE_STRING, "Speed parameter", "State", speed_hint,
	           "(none)");
	add_field (self, "state-mirror", sp, "mirror", G_TYPE_BOOLEAN, "Mirror", "State", NULL, "false");

	if (g_strcmp0 (kind, "blend_tree") == 0)
	{
		g_autofree gchar *bt = pj (sp, "blend_tree");
		g_autofree gchar *ch = pj (bt, "children");
		const gchar      *type = scalar_at (self, bt, "type");
		gboolean          two_d = type != NULL && g_str_has_prefix (type, "2d");
		gboolean          direct = g_strcmp0 (type, "direct") == 0;
		gint              n = n_items (self, ch);
		gint              k;

		add_field (self, "tree-type", bt, "type", G_TYPE_STRING, "Type", "Blend tree",
		           "enum-strings:1d,2d_directional,2d_cartesian,direct", "1d");
		if (!direct)
			add_field (self, "tree-param", bt, "parameter", G_TYPE_STRING, two_d ? "Parameter X" : "Parameter",
			           "Blend tree", num_hint, NULL);
		if (two_d)
			add_field (self, "tree-param-y", bt, "parameter_y", G_TYPE_STRING, "Parameter Y", "Blend tree", num_hint,
			           NULL);
		add_field (self, "tree-sync", bt, "sync", G_TYPE_BOOLEAN, "Sync phases", "Blend tree", NULL, "true");
		for (k = 0; k < n; k++)
		{
			g_autofree gchar *cp = pji (ch, (guint) k);
			g_autofree gchar *group = g_strdup_printf ("Child %d", k + 1);
			g_autofree gchar *f_clip = g_strdup_printf ("child%d-clip", k);

			add_field (self, f_clip, cp, "clip", G_TYPE_STRING, "Clip", group, clip_hint, NULL);
			if (direct)
			{
				g_autofree gchar *f = g_strdup_printf ("child%d-parameter", k);

				add_field (self, f, cp, "parameter", G_TYPE_STRING, "Weight parameter", group, num_hint, NULL);
			}
			else if (two_d)
			{
				g_autofree gchar *fx = g_strdup_printf ("child%d-x", k);
				g_autofree gchar *fy = g_strdup_printf ("child%d-y", k);
				g_autofree gchar *pos = pj (cp, "position");
				g_autofree gchar *px = pji (pos, 0);
				g_autofree gchar *py = pji (pos, 1);

				lrg_editor_data_source_add_field (self->source, fx, px, G_TYPE_DOUBLE, "Position X", group, NULL, "0");
				lrg_editor_data_source_add_field (self->source, fy, py, G_TYPE_DOUBLE, "Position Y", group, NULL, "0");
			}
			else
			{
				g_autofree gchar *f = g_strdup_printf ("child%d-threshold", k);

				add_field (self, f, cp, "threshold", G_TYPE_DOUBLE, "Threshold", group, NULL, "0");
			}
			{
				g_autofree gchar *f = g_strdup_printf ("child%d-speed", k);

				add_field (self, f, cp, "speed", G_TYPE_DOUBLE, "Speed", group, "range:0,4,0.05", "1");
			}
		}
	}
}

static void
build_transition_fields (LrgEditorAnimGraphEditor *self,
                         const gchar              *tp)
{
	gboolean any = g_strcmp0 (self->sel_from, ANY_ID) == 0;

	add_field (self, "tr-from", tp, "from", G_TYPE_STRING, "From", "Transition", NULL, NULL);
	add_field (self, "tr-to", tp, "to", G_TYPE_STRING, "To", "Transition", NULL, NULL);
	add_field (self, "tr-duration", tp, "duration", G_TYPE_DOUBLE, "Duration (s)", "Transition", "range:0,10,0.01",
	           "0.25");
	add_field (self, "tr-has-exit", tp, NULL, G_TYPE_BOOLEAN, "Has exit time", "Transition", NULL, "false");
	if (has_key (self, tp, "exit_time"))
		add_field (self, "tr-exit", tp, "exit_time", G_TYPE_DOUBLE, "Exit time", "Transition", "range:0,4,0.01",
		           "0.9");
	add_field (self, "tr-offset", tp, "offset", G_TYPE_DOUBLE, "Offset", "Transition", "range:0,1,0.01", "0");
	add_field (self, "tr-priority", tp, "priority", G_TYPE_INT64, "Priority", "Transition", NULL, "0");
	add_field (self, "tr-interruption", tp, "interruption", G_TYPE_STRING, "Interruption", "Transition",
	           "enum-strings:none,source,destination,source_then_destination,destination_then_source", "none");
	if (any)
		add_field (self, "tr-self", tp, "can_transition_to_self", G_TYPE_BOOLEAN, "To itself", "Transition", NULL,
		           "false");
}

static void
build_layer_fields (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar *lp = pji ("/layers", machine_layer (self->machine));

	add_field (self, "layer-name", lp, "name", G_TYPE_STRING, "Name", "Layer", NULL, NULL);
	add_field (self, "layer-weight", lp, "weight", G_TYPE_DOUBLE, "Weight", "Layer", "range:0,1,0.01", "1");
	add_field (self, "layer-blend", lp, "blend", G_TYPE_STRING, "Blend", "Layer", "enum-strings:override,additive",
	           "override");
	add_field (self, "layer-enabled", lp, "enabled", G_TYPE_BOOLEAN, "Enabled", "Layer", NULL, "true");
	add_field (self, "layer-mask", lp, NULL, G_TYPE_STRING, "Mask bones", "Layer", NULL, "");
	add_field (self, "layer-mask-rec", lp, "mask_recursive", G_TYPE_BOOLEAN, "Mask children", "Layer", NULL, "true");
	add_field (self, "graph-name", "", "name", G_TYPE_STRING, "Name", "Graph", NULL, NULL);
	add_field (self, "rm-bone", "/root_motion", "bone", G_TYPE_STRING, "Root bone", "Root motion", NULL, "");
	add_field (self, "rm-mode", "/root_motion", "mode", G_TYPE_STRING, "Mode", "Root motion",
	           "enum-strings:none,xz,xyz", "none");
	add_field (self, "rm-rotation", "/root_motion", "rotation", G_TYPE_BOOLEAN, "Extract turn", "Root motion", NULL,
	           "false");
	add_field (self, "rm-apply", "/root_motion", "apply", G_TYPE_BOOLEAN, "Move the entity", "Root motion", NULL,
	           "true");
}

static void
rebuild_grid (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar *sp = sel_state_path (self);
	g_autofree gchar *tp = lrg_editor_anim_graph_editor_get_selected_transition (self);
	g_autofree gchar *title = NULL;
	gboolean          blend = FALSE;

	self->grid_stale = FALSE;
	lrg_editor_data_source_clear (self->source);
	if (sp != NULL)
	{
		build_state_fields (self, sp);
		blend = g_strcmp0 (state_kind (self, sp), "blend_tree") == 0;
		title = g_strdup_printf ("State \xc2\xb7 %s", self->sel_state);
	}
	else if (tp != NULL)
	{
		build_transition_fields (self, tp);
		title = g_strdup_printf ("Transition \xc2\xb7 %s \xe2\x86\x92 %s",
		                         g_strcmp0 (self->sel_from, ANY_ID) == 0 ? "Any State" : self->sel_from, self->sel_to);
	}
	else
	{
		g_autofree gchar *lp = pji ("/layers", machine_layer (self->machine));
		const gchar      *ln = scalar_at (self, lp, "name");

		build_layer_fields (self);
		title = g_strdup_printf ("Layer \xc2\xb7 %s", ln != NULL ? ln : "?");
	}
	lrg_label_set_text (LRG_LABEL (self->inspector_title), title);
	lrg_widget_set_visible (self->blend_space, blend);
	lrg_widget_set_visible (self->cond_box, tp != NULL);
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
	g_clear_pointer (&self->cond_sig, g_free);
	rebuild_conditions (self);
}

/* ==========================================================================
 * Conditions editor
 * ========================================================================== */

static const gchar * const num_ops[] = { "==", "!=", ">", "<", ">=", "<=" };

/* The condition item the row widget @w edits. */
static gchar *
cond_path_of (LrgEditorAnimGraphEditor *self,
              gpointer                  w)
{
	g_autofree gchar *tp = lrg_editor_anim_graph_editor_get_selected_transition (self);
	g_autofree gchar *cl = tp != NULL ? pj (tp, "conditions") : NULL;

	return cl != NULL ? pji (cl, GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (w), "cond"))) : NULL;
}

/* Writes condition @cp for @param with @op / @value (YAML text). */
static void
write_condition (LrgEditorAnimGraphEditor *self,
                 const gchar              *cp,
                 const gchar              *param,
                 const gchar              *op,
                 const gchar              *value)
{
	g_autofree gchar *yp = yaml_str (param);
	g_autofree gchar *text = (op == NULL) ? g_strdup_printf ("{parameter: %s}", yp)
	                                      : g_strdup_printf ("{parameter: %s, op: '%s', value: %s}", yp, op, value);
	g_autoptr(GError) error = NULL;

	edit_begin (self, "Edit Condition");
	if (!lrg_editor_document_replace (doc_of (self), cp, text, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	edit_end (self);
}

/* The default form of a condition on @param. */
static void
default_condition (LrgEditorAnimGraphEditor *self,
                   const gchar              *param,
                   const gchar             **op,
                   const gchar             **value)
{
	const gchar *type = parameter_type (self, param);

	*op = NULL;
	*value = NULL;
	if (g_strcmp0 (type, "float") == 0)
	{
		*op = ">";
		*value = "0.1";
	}
	else if (g_strcmp0 (type, "int") == 0)
	{
		*op = "==";
		*value = "0";
	}
}

static void
on_cond_param (LrgComboBox *combo,
               gpointer     user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autofree gchar         *cp = NULL;
	const gchar              *param = lrg_combo_box_get_selected_text (combo);
	const gchar              *op, *value;

	if (self->syncing || param == NULL || (cp = cond_path_of (self, combo)) == NULL)
		return;
	default_condition (self, param, &op, &value);
	write_condition (self, cp, param, op, value);
}

static void
on_cond_op (LrgComboBox *combo,
            gpointer     user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autofree gchar         *cp = NULL;
	const gchar              *param;
	const gchar              *text = lrg_combo_box_get_selected_text (combo);
	const gchar              *type;

	if (self->syncing || text == NULL || (cp = cond_path_of (self, combo)) == NULL)
		return;
	param = scalar_at (self, cp, "parameter");
	type = parameter_type (self, param);
	if (g_strcmp0 (type, "bool") == 0)
	{
		/* "is true" is the short form, "is false" an explicit compare */
		if (g_str_has_suffix (text, "true"))
			write_condition (self, cp, param, NULL, NULL);
		else
			write_condition (self, cp, param, "==", "false");
		return;
	}
	if (g_strcmp0 (type, "trigger") == 0)
		return;
	{
		const gchar      *v = scalar_at (self, cp, "value");
		g_autofree gchar *pdup = g_strdup (param);

		write_condition (self, cp, pdup, text, v != NULL ? v : "0");
	}
}

static void
on_cond_value (LrgTextInput *input,
               gpointer      user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autofree gchar         *cp = NULL;
	g_autofree gchar         *vp = NULL;
	const gchar              *text = lrg_text_input_get_text (input);
	gchar                    *end = NULL;
	gdouble                   v;

	if (self->syncing || text == NULL || (cp = cond_path_of (self, input)) == NULL)
		return;
	v = g_ascii_strtod (text, &end);
	if (end == text || *end != '\0')
		return;
	vp = pj (cp, "value");
	/* one undo step per typing session */
	if (self->gesture == 0)
		self->gesture = lrg_editor_data_editor_new_gesture (LRG_EDITOR_DATA_EDITOR (self));
	if (g_strcmp0 (parameter_type (self, scalar_at (self, cp, "parameter")), "int") == 0)
		lrg_editor_document_set_int (doc_of (self), vp, (gint64) rint (v), self->gesture, NULL);
	else
		lrg_editor_document_set_double (doc_of (self), vp, v, self->gesture, NULL);
}

static void
on_cond_remove (LrgButton *button,
                gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autofree gchar         *cp = cond_path_of (self, button);

	if (cp == NULL)
		return;
	edit_begin (self, "Remove Condition");
	lrg_editor_document_remove (doc_of (self), cp, NULL);
	edit_end (self);
}

static void
on_cond_add (LrgButton *button,
             gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	g_autoptr(GPtrArray)      names = parameter_names (self, FALSE);
	const gchar              *op, *value;
	g_autoptr(GError)         error = NULL;

	if (self->sel_to == NULL)
		return;
	if (names->len == 0)
	{
		if (app_of (self) != NULL)
			editor_app_notify (app_of (self), EDITOR_NOTIFY_WARNING, "Declare a parameter first (left column)");
		return;
	}
	default_condition (self, g_ptr_array_index (names, 0), &op, &value);
	if (!lrg_editor_anim_graph_editor_add_condition (self, self->sel_from, self->sel_to, g_ptr_array_index (names, 0),
	                                                 op, value, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* Structure of the conditions (values excluded: typing must not rebuild). */
static gchar *
cond_signature (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar *tp = lrg_editor_anim_graph_editor_get_selected_transition (self);
	g_autofree gchar *cl = tp != NULL ? pj (tp, "conditions") : NULL;
	g_autofree gchar *rows = rows_signature (self);
	GString          *s = g_string_new (tp);
	gint              n = cl != NULL ? n_items (self, cl) : 0;
	gint              i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *cp = pji (cl, (guint) i);

		g_string_append_printf (s, "|%s %s", scalar_at (self, cp, "parameter"), scalar_at (self, cp, "op"));
		if (g_strcmp0 (parameter_type (self, scalar_at (self, cp, "parameter")), "bool") == 0)
			g_string_append (s, scalar_at (self, cp, "value") != NULL ? scalar_at (self, cp, "value") : "");
	}
	g_string_append_printf (s, "|%s", rows);
	return g_string_free (s, FALSE);
}

static void
rebuild_conditions (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar    *tp = lrg_editor_anim_graph_editor_get_selected_transition (self);
	g_autofree gchar    *cl = tp != NULL ? pj (tp, "conditions") : NULL;
	g_autofree gchar    *sig = cond_signature (self);
	g_autoptr(GPtrArray) names = parameter_names (self, FALSE);
	gint                 n = cl != NULL ? n_items (self, cl) : 0;
	gint                 i;

	if (g_strcmp0 (sig, self->cond_sig) == 0)
		return;
	g_free (self->cond_sig);
	self->cond_sig = g_steal_pointer (&sig);
	self->gesture = 0;
	self->syncing = TRUE;
	lrg_container_remove_all (LRG_CONTAINER (self->cond_box));
	{
		LrgWidget *h = heading_label ("Conditions (all must hold)");

		lrg_container_add_child (LRG_CONTAINER (self->cond_box), h);
		g_object_unref (h);
	}
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *cp = pji (cl, (guint) i);
		const gchar      *param = scalar_at (self, cp, "parameter");
		const gchar      *op = scalar_at (self, cp, "op");
		const gchar      *value = scalar_at (self, cp, "value");
		const gchar      *type = parameter_type (self, param);
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		LrgComboBox      *pc = lrg_combo_box_new ();
		LrgComboBox      *oc = lrg_combo_box_new ();
		LrgWidget        *rm;
		g_autofree gchar *rname = g_strdup_printf ("anim-cond-%d", i);
		g_autofree gchar *pname = g_strdup_printf ("anim-cond-%d-param", i);
		g_autofree gchar *oname = g_strdup_printf ("anim-cond-%d-op", i);
		g_autofree gchar *vname = g_strdup_printf ("anim-cond-%d-value", i);
		g_autofree gchar *xname = g_strdup_printf ("anim-cond-%d-remove", i);
		guint             k;

		lrg_widget_set_name (row, rname);
		lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
		lrg_widget_set_name (LRG_WIDGET (pc), pname);
		for (k = 0; k < names->len; k++)
		{
			lrg_combo_box_append (pc, g_ptr_array_index (names, k));
			if (g_strcmp0 (g_ptr_array_index (names, k), param) == 0)
				lrg_combo_box_set_selected (pc, (gint) k);
		}
		g_object_set_data (G_OBJECT (pc), "cond", GUINT_TO_POINTER ((guint) i));
		g_signal_connect (pc, "changed", G_CALLBACK (on_cond_param), self);
		lrg_widget_set_fill_weight (LRG_WIDGET (pc), 1.0f);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (pc));
		g_object_unref (pc);

		lrg_widget_set_name (LRG_WIDGET (oc), oname);
		g_object_set_data (G_OBJECT (oc), "cond", GUINT_TO_POINTER ((guint) i));
		if (g_strcmp0 (type, "bool") == 0)
		{
			lrg_combo_box_append (oc, "is true");
			lrg_combo_box_append (oc, "is false");
			lrg_combo_box_set_selected (oc, (op != NULL && g_strcmp0 (value, "false") == 0) ||
			                                (g_strcmp0 (op, "!=") == 0 && g_strcmp0 (value, "true") == 0) ? 1 : 0);
		}
		else if (g_strcmp0 (type, "trigger") == 0)
		{
			lrg_combo_box_append (oc, "fired");
			lrg_combo_box_set_selected (oc, 0);
		}
		else
		{
			for (k = 0; k < G_N_ELEMENTS (num_ops); k++)
			{
				lrg_combo_box_append (oc, num_ops[k]);
				if (g_strcmp0 (num_ops[k], op) == 0)
					lrg_combo_box_set_selected (oc, (gint) k);
			}
		}
		g_signal_connect (oc, "changed", G_CALLBACK (on_cond_op), self);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (oc));
		g_object_unref (oc);

		if (g_strcmp0 (type, "float") == 0 || g_strcmp0 (type, "int") == 0)
		{
			LrgTextInput *vi = lrg_text_input_new ();

			lrg_widget_set_name (LRG_WIDGET (vi), vname);
			lrg_text_input_set_text (vi, value != NULL ? value : "0");
			g_object_set_data (G_OBJECT (vi), "cond", GUINT_TO_POINTER ((guint) i));
			g_signal_connect (vi, "text-changed", G_CALLBACK (on_cond_value), self);
			lrg_widget_set_min_size (LRG_WIDGET (vi), 64.0f, 0.0f);
			lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (vi));
			g_object_unref (vi);
		}
		rm = small_button (xname, "\xc3\x97", G_CALLBACK (on_cond_remove), self);
		g_object_set_data (G_OBJECT (rm), "cond", GUINT_TO_POINTER ((guint) i));
		lrg_container_add_child (LRG_CONTAINER (row), rm);
		g_object_unref (rm);
		lrg_container_add_child (LRG_CONTAINER (self->cond_box), row);
		g_object_unref (row);
	}
	if (n == 0)
	{
		LrgWidget *h = heading_label (has_key (self, tp != NULL ? tp : "/", "exit_time")
		                              ? "None: leaves at the exit time" : "None: fires at once");

		lrg_container_add_child (LRG_CONTAINER (self->cond_box), h);
		g_object_unref (h);
	}
	{
		LrgWidget *add = small_button ("anim-cond-add", "+ Condition", G_CALLBACK (on_cond_add), self);

		lrg_container_add_child (LRG_CONTAINER (self->cond_box), add);
		g_object_unref (add);
	}
	self->syncing = FALSE;
}

/* ==========================================================================
 * AgBlendSpace: the blend-space view of the selected blend tree state
 *
 * 1D trees are a strip with the children at their thresholds, 2D trees a
 * plane with the children at their positions (axes through the origin).
 * Each child dot grows with its current weight; the parameter value is a
 * ring the user drags to set the live parameters. Direct trees list each
 * child's weight as a bar.
 * ========================================================================== */

typedef struct
{
	LrgEditorAnimGraphEditor *editor;     /* weak via the widget's lifetime */
	gboolean                  dragging;
	gfloat                    lo[2];      /* view range of the last paint */
	gfloat                    hi[2];
	gfloat                    rect[4];    /* plot rect of the last paint */
} AgBlendSpace;

/* The runtime tree of the selected state and its machine (weights fresh). */
static LrgBlendTree *
selected_tree (LrgEditorAnimGraphEditor  *self,
               LrgAnimationStateMachine **machine)
{
	LrgAnimationStateMachine *m = runtime_machine (self, self->machine);
	LrgAnimationState        *st = (m != NULL && self->sel_state != NULL)
	                               ? lrg_animation_state_machine_get_state (m, self->sel_state) : NULL;
	LrgBlendTree             *tree = st != NULL ? lrg_animation_state_get_blend_tree (st) : NULL;

	if (tree != NULL)
	{
		lrg_blend_tree_pull_parameters (tree, m);
		lrg_blend_tree_compute_weights (tree);
	}
	if (machine != NULL)
		*machine = m;
	return tree;
}

static gfloat
param_value (LrgEditorAnimGraphEditor *self,
             const gchar              *name)
{
	if (self->graph == NULL || name == NULL)
		return 0.0f;
	if (lrg_anim_graph_get_parameter_type (self->graph, name) == LRG_ANIM_PARAMETER_INT)
		return (gfloat) lrg_anim_graph_get_int (self->graph, name);
	return lrg_anim_graph_get_float (self->graph, name);
}

static void
blend_space_paint (LrgCustomWidget *widget,
                   LrgPaintList    *list,
                   gfloat           x,
                   gfloat           y,
                   gfloat           width,
                   gfloat           height,
                   gpointer         user_data)
{
	AgBlendSpace             *bs = user_data;
	LrgEditorAnimGraphEditor *self = bs->editor;
	LrgTheme                 *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                  *font = lrg_theme_get_ui_font (theme);
	gfloat                    fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor           *ink = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor           *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor           *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor           *link = lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);
	GrlColor                  grid;
	LrgBlendTree             *tree;
	LrgBlendType              type;
	guint                     n, i;
	gboolean                  two_d;
	gfloat                    px, py, pw, ph;
	gfloat                    vx = 0.0f, vy = 0.0f;

	lrg_paint_list_rect (list, x, y, width, height, 6.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_border (list, x, y, width, height, 6.0f, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	if (self == NULL)
		return;
	tree = selected_tree (self, NULL);
	if (tree == NULL)
	{
		lrg_paint_list_text (list, font, "Blend space: the tree does not load", x + 10.0f, y + 10.0f, fs, 0.0f, muted);
		return;
	}
	type = lrg_blend_tree_get_blend_type (tree);
	n = lrg_blend_tree_get_child_count (tree);
	two_d = type == LRG_BLEND_TYPE_2D_SIMPLE || type == LRG_BLEND_TYPE_2D_FREEFORM;
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.6f, &grid);

	/* --- direct: weight bars --- */
	if (type == LRG_BLEND_TYPE_DIRECT)
	{
		for (i = 0; i < n; i++)
		{
			LrgBlendTreeChild *c = lrg_blend_tree_get_child (tree, i);
			const gchar       *name = lrg_blend_tree_child_get_clip_name (c);
			gfloat             w = lrg_blend_tree_get_child_weight (tree, i);
			gfloat             ry = y + 10.0f + (gfloat) i * (fs + 10.0f);

			lrg_paint_list_text (list, font, name != NULL ? name : "tree", x + 10.0f, ry, fs, 0.0f, ink);
			lrg_paint_list_rect (list, x + 110.0f, ry + 2.0f, (width - 130.0f), fs - 2.0f, 3.0f, &grid);
			lrg_paint_list_rect (list, x + 110.0f, ry + 2.0f, (width - 130.0f) * CLAMP (w, 0.0f, 1.0f), fs - 2.0f,
			                     3.0f, accent);
		}
		return;
	}

	/* --- ranges: the children plus the parameter, padded --- */
	vx = param_value (self, lrg_blend_tree_get_parameter_name (tree));
	vy = two_d ? param_value (self, lrg_blend_tree_get_parameter_y_name (tree)) : 0.0f;
	bs->lo[0] = bs->lo[1] = two_d ? -1.0f : 0.0f;
	bs->hi[0] = bs->hi[1] = 1.0f;
	for (i = 0; i < n; i++)
	{
		LrgBlendTreeChild *c = lrg_blend_tree_get_child (tree, i);
		gfloat             cx = two_d ? c->position_x : c->threshold;
		gfloat             cy = two_d ? c->position_y : 0.0f;

		bs->lo[0] = MIN (bs->lo[0], cx);
		bs->hi[0] = MAX (bs->hi[0], cx);
		bs->lo[1] = MIN (bs->lo[1], cy);
		bs->hi[1] = MAX (bs->hi[1], cy);
	}
	for (i = 0; i < 2; i++)
	{
		gfloat pad = (bs->hi[i] - bs->lo[i]) * 0.12f + 0.05f;

		bs->lo[i] -= pad;
		bs->hi[i] += pad;
	}
	px = x + 12.0f;
	pw = width - 24.0f;
	py = y + 12.0f + fs;
	ph = height - 24.0f - fs;
	if (!two_d)
	{
		py = y + height * 0.5f;
		ph = 0.0f;
	}
	bs->rect[0] = px;
	bs->rect[1] = py;
	bs->rect[2] = pw;
	bs->rect[3] = ph;
#define MAPX(v) (px + ((v) - bs->lo[0]) / (bs->hi[0] - bs->lo[0]) * pw)
#define MAPY(v) (two_d ? (py + ph - ((v) - bs->lo[1]) / (bs->hi[1] - bs->lo[1]) * ph) : py)

	/* --- axes --- */
	{
		g_autofree gchar *label = two_d
		                          ? g_strdup_printf ("%s \xc3\x97 %s", lrg_blend_tree_get_parameter_name (tree),
		                                             lrg_blend_tree_get_parameter_y_name (tree))
		                          : g_strdup (lrg_blend_tree_get_parameter_name (tree));

		lrg_paint_list_text (list, font, label != NULL ? label : "", x + 10.0f, y + 6.0f, fs, 0.0f, muted);
	}
	if (two_d)
	{
		lrg_paint_list_line (list, MAPX (0.0f), py, MAPX (0.0f), py + ph, 1.0f, &grid);
		lrg_paint_list_line (list, px, MAPY (0.0f), px + pw, MAPY (0.0f), 1.0f, &grid);
	}
	else
		lrg_paint_list_line (list, px, py, px + pw, py, 2.0f, &grid);

	/* --- children --- */
	for (i = 0; i < n; i++)
	{
		LrgBlendTreeChild *c = lrg_blend_tree_get_child (tree, i);
		const gchar       *name = lrg_blend_tree_child_get_clip_name (c);
		gfloat             w = lrg_blend_tree_get_child_weight (tree, i);
		gfloat             cx = MAPX (two_d ? c->position_x : c->threshold);
		gfloat             cy = MAPY (two_d ? c->position_y : 0.0f);
		GrlColor           dot;

		lrg_paint_color_with_alpha (link, 0.35f + 0.65f * CLAMP (w, 0.0f, 1.0f), &dot);
		lrg_paint_list_circle (list, cx, cy, 4.0f + 8.0f * CLAMP (w, 0.0f, 1.0f), &dot);
		lrg_paint_list_text (list, font, name != NULL ? name : "tree", cx + 8.0f, cy - fs - 4.0f, fs, 0.0f, ink);
	}

	/* --- the parameter --- */
	{
		gfloat cx = MAPX (CLAMP (vx, bs->lo[0], bs->hi[0]));
		gfloat cy = MAPY (CLAMP (vy, bs->lo[1], bs->hi[1]));

		lrg_paint_list_circle (list, cx, cy, 7.0f, accent);
		lrg_paint_list_circle (list, cx, cy, 3.5f, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	}
#undef MAPX
#undef MAPY
}

/* Sets the live parameters from a point of the plot. */
static void
blend_space_drag (AgBlendSpace *bs,
                  gfloat        ex,
                  gfloat        ey)
{
	LrgEditorAnimGraphEditor *self = bs->editor;
	LrgBlendTree             *tree = self != NULL ? selected_tree (self, NULL) : NULL;
	LrgBlendType              type;
	gfloat                    vx, vy;

	if (tree == NULL || bs->rect[2] <= 0.0f)
		return;
	type = lrg_blend_tree_get_blend_type (tree);
	vx = bs->lo[0] + (ex - bs->rect[0]) / bs->rect[2] * (bs->hi[0] - bs->lo[0]);
	if (lrg_blend_tree_get_parameter_name (tree) != NULL)
		lrg_editor_anim_graph_editor_set_parameter_value (self, lrg_blend_tree_get_parameter_name (tree),
		                                                  g_variant_new_double (vx));
	if ((type == LRG_BLEND_TYPE_2D_SIMPLE || type == LRG_BLEND_TYPE_2D_FREEFORM) && bs->rect[3] > 0.0f &&
	    lrg_blend_tree_get_parameter_y_name (tree) != NULL)
	{
		vy = bs->lo[1] + (bs->rect[1] + bs->rect[3] - ey) / bs->rect[3] * (bs->hi[1] - bs->lo[1]);
		lrg_editor_anim_graph_editor_set_parameter_value (self, lrg_blend_tree_get_parameter_y_name (tree),
		                                                  g_variant_new_double (vy));
	}
}

static gboolean
blend_space_event (LrgCustomWidget  *widget,
                   const LrgUIEvent *event,
                   gpointer          user_data)
{
	AgBlendSpace *bs = user_data;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		bs->dragging = TRUE;
		blend_space_drag (bs, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
		return TRUE;
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (!bs->dragging)
			return FALSE;
		blend_space_drag (bs, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (!bs->dragging)
			return FALSE;
		bs->dragging = FALSE;
		return TRUE;
	default:
		return FALSE;
	}
}

static LrgWidget *
blend_space_new (LrgEditorAnimGraphEditor *self)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, blend_space_paint, NULL, blend_space_event, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	AgBlendSpace    *bs = g_new0 (AgBlendSpace, 1);

	bs->editor = self;
	lrg_widget_set_name (LRG_WIDGET (w), "anim-blend-space");
	lrg_custom_widget_set_funcs (w, &funcs, bs, g_free);
	lrg_custom_widget_set_preferred_width (w, 220.0f);
	lrg_custom_widget_set_preferred_height (w, 190.0f);
	return LRG_WIDGET (w);
}

/* ==========================================================================
 * Preview model and scrub bar
 * ========================================================================== */

/* The model the preview should show: `editor_preview:`, else the first
 * clip slot's source. */
static gchar *
wanted_model (LrgEditorAnimGraphEditor *self)
{
	const gchar      *ref = lrg_editor_document_get_scalar (doc_of (self), "/editor_preview");
	g_autofree gchar *dir = base_dir_of (self);
	g_auto(GStrv)     keys = NULL;
	guint             i;

	if (ref != NULL && *ref != '\0')
		return g_canonicalize_filename (ref, dir);
	keys = clip_keys (self);
	for (i = 0; keys[i] != NULL; i++)
	{
		g_autofree gchar *cp = pj ("/clips", keys[i]);
		const gchar      *source = scalar_at (self, cp, "source");

		if (source != NULL)
			return g_canonicalize_filename (source, dir);
	}
	return NULL;
}

static void
sync_model_combo (LrgEditorAnimGraphEditor *self)
{
	LrgEditorApp *app = app_of (self);
	gint          sel = 0;
	guint         i;

	g_clear_pointer (&self->model_list, g_ptr_array_unref);
	self->model_list = editor_anim_list_models (app);
	self->syncing = TRUE;
	lrg_combo_box_remove_all (self->model_combo);
	lrg_combo_box_append (self->model_combo, "Preview model\xe2\x80\xa6");
	for (i = 0; i < self->model_list->len; i++)
	{
		const gchar      *rel = g_ptr_array_index (self->model_list, i);
		g_autofree gchar *p = app != NULL ? editor_app_project_path (app, rel) : g_strdup (rel);
		g_autofree gchar *abs = g_canonicalize_filename (p, NULL);

		lrg_combo_box_append (self->model_combo, rel);
		if (self->preview_model != NULL && g_strcmp0 (abs, self->preview_model) == 0)
			sel = (gint) i + 1;
	}
	lrg_combo_box_set_selected (self->model_combo, sel);
	self->syncing = FALSE;
}

static void
sync_preview_model (LrgEditorAnimGraphEditor *self)
{
	g_autofree gchar *want = wanted_model (self);
	g_autoptr(GError) error = NULL;

	if (g_strcmp0 (want, self->preview_model) == 0)
		return;
	g_free (self->preview_model);
	self->preview_model = g_steal_pointer (&want);
	if (self->preview_model != NULL && !g_file_test (self->preview_model, G_FILE_TEST_IS_REGULAR))
	{
		editor_anim_view_set_model (self->view, NULL, NULL);
		return;
	}
	if (!editor_anim_view_set_model (self->view, self->preview_model, &error))
		g_debug ("anim graph preview: %s", error->message);
	sync_model_combo (self);
}

static void
on_model_combo (LrgComboBox *combo,
                gpointer     user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);
	gint                      i = lrg_combo_box_get_selected (combo);
	g_autoptr(GError)         error = NULL;

	if (self->syncing || i <= 0 || self->model_list == NULL || (guint) i > self->model_list->len)
		return;
	if (!lrg_editor_anim_graph_editor_set_preview_model (self, g_ptr_array_index (self->model_list, i - 1), &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_scrub (LrgSlider *slider,
          gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	if (!self->syncing)
		lrg_editor_anim_graph_editor_scrub (self, (gfloat) lrg_slider_get_value (slider));
}

static void
on_view_playing (LrgWidget *view,
                 gpointer   user_data)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data);

	self->playing = editor_anim_view_get_playing (view);
}

static void
on_restart_chip (LrgWidget *chip,
                 gpointer   user_data)
{
	lrg_editor_anim_graph_editor_restart (LRG_EDITOR_ANIM_GRAPH_EDITOR (user_data));
}

/* The base layer's current state (for the scrub bar). */
static LrgAnimationState *
base_state (LrgEditorAnimGraphEditor *self)
{
	LrgAnimationStateMachine *m = self->graph != NULL ? lrg_anim_graph_get_machine (self->graph) : NULL;

	return m != NULL ? lrg_animation_state_machine_get_current_state (m) : NULL;
}

/* The caption of the preview: active state per layer, blend progress. */
static void
update_caption (LrgEditorAnimGraphEditor *self)
{
	GString *s = g_string_new (NULL);
	guint    i, n = self->graph != NULL ? lrg_anim_graph_get_layer_count (self->graph) : 0;

	if (self->load_error != NULL)
		g_string_append (s, "graph does not load");
	for (i = 0; i < n; i++)
	{
		LrgAnimationLayer        *layer = lrg_anim_graph_get_layer (self->graph, i);
		LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (layer);
		LrgAnimationState        *ns = m != NULL ? lrg_animation_state_machine_get_next_state (m) : NULL;
		const gchar              *cur = m != NULL ? lrg_animation_state_machine_get_current_state_name (m) : NULL;

		if (i > 0)
			g_string_append (s, "  \xc2\xb7  ");
		if (n > 1)
			g_string_append_printf (s, "%s: ", lrg_animation_layer_get_name (layer));
		g_string_append (s, cur != NULL ? cur : "-");
		if (ns != NULL)
			g_string_append_printf (s, " \xe2\x86\x92 %s %d%%", lrg_animation_state_get_name (ns),
			                        (gint) (100.0f * lrg_animation_state_machine_get_transition_progress (m)));
	}
	editor_anim_view_set_caption (self->view, s->str);
	g_string_free (s, TRUE);
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

/* "layers[0].transitions[2]: msg" -> "/layers/0/transitions/2". */
static gchar *
error_path (const gchar *message)
{
	const gchar *colon = strstr (message, ": ");
	GString     *s = g_string_new ("");
	const gchar *p;

	if (colon == NULL || strchr (message, ' ') < colon)
		return g_string_free (s, FALSE);
	g_string_append_c (s, '/');
	for (p = message; p < colon; p++)
	{
		if (*p == '.' || *p == '[')
			g_string_append_c (s, '/');
		else if (*p != ']')
			g_string_append_c (s, *p);
	}
	return g_string_free (s, FALSE);
}

static void
anim_refresh (LrgEditorDataEditor *editor,
              const gchar         *path)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (editor);
	g_autofree gchar         *sig = NULL;

	if (self->canvas == NULL)
		return;
	load_graph (self);
	/* the machine shown may be gone (undo of an add, a reload) */
	if (!lrg_editor_document_has_path (doc_of (self), self->machine))
	{
		g_free (self->machine);
		self->machine = g_strdup ("/layers/0");
		g_clear_pointer (&self->sel_state, g_free);
		g_clear_pointer (&self->sel_from, g_free);
		g_clear_pointer (&self->sel_to, g_free);
	}
	if (self->sel_state != NULL && state_index_in (self, self->machine, self->sel_state) < 0)
		g_clear_pointer (&self->sel_state, g_free);
	if (self->sel_to != NULL)
	{
		g_autofree gchar *tp = transition_path_in (self, self->machine, self->sel_from, self->sel_to);

		if (tp == NULL)
		{
			g_clear_pointer (&self->sel_from, g_free);
			g_clear_pointer (&self->sel_to, g_free);
		}
	}
	/* the canvas already shows its own gestures */
	if (!self->writing)
		rebuild_canvas (self);
	rebuild_chrome (self);
	sig = rows_signature (self);
	if (g_strcmp0 (sig, self->rows_sig) != 0)
		self->rows_stale = TRUE;
	g_free (sig);
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->grid_sig) != 0)
		self->grid_stale = TRUE;
	else
	{
		lrg_editor_data_source_document_changed (self->source, path);
		lrg_property_grid_refresh (self->grid);
	}
	sync_preview_model (self);
}

static void
anim_validate (LrgEditorDataEditor *editor,
               GPtrArray           *issues)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (editor);
	g_auto(GStrv)             missing = NULL;
	guint                     i;

	if (self->load_error != NULL)
	{
		g_autofree gchar *path = error_path (self->load_error);
		guint             line = 1, col = 1;

		/* the deepest existing ancestor carries the line */
		while (*path != '\0' && !lrg_editor_document_has_path (doc_of (self), path))
		{
			gchar *slash = strrchr (path, '/');

			if (slash == NULL)
				break;
			*slash = '\0';
		}
		lrg_editor_document_get_line (doc_of (self), path, &line, &col);
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, path, line, col,
		                                                        self->load_error));
		return;
	}
	if (self->graph == NULL)
		return;
	missing = lrg_anim_graph_get_missing_clips (self->graph);
	for (i = 0; missing != NULL && missing[i] != NULL; i++)
	{
		g_autofree gchar *path = pj ("/clips", missing[i]);
		g_autofree gchar *msg = g_strdup_printf ("clip '%s' has no animation loaded (check source and animation)",
		                                         missing[i]);
		guint             line = 0, col = 0;

		if (!lrg_editor_document_has_path (doc_of (self), path))
		{
			g_free (path);
			path = g_strdup ("/clips");
		}
		lrg_editor_document_get_line (doc_of (self), path, &line, &col);
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col, msg));
	}
	/* masks, the root motion bone and IK bones against the preview model */
	if (editor_anim_view_get_skeleton (self->view) != NULL)
	{
		g_autoptr(GError) error = NULL;

		if (!lrg_anim_graph_validate_skeleton (self->graph, editor_anim_view_get_skeleton (self->view), &error))
		{
			g_autofree gchar *path = error_path (error->message);
			guint             line = 0, col = 0;

			lrg_editor_document_get_line (doc_of (self), path, &line, &col);
			g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path, line, col,
			                                                        error->message));
		}
	}
}

/* Reveal: walk to the machine holding the path and select its item. */
static void
anim_reveal (LrgEditorDataEditor *editor,
             const gchar         *path,
             guint                line)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (editor);
	g_auto(GStrv)             parts = lrg_editor_document_path_split (path, NULL);
	g_autofree gchar         *machine = NULL;
	guint                     i;

	if (parts == NULL || g_strcmp0 (parts[0], "layers") != 0 || parts[1] == NULL)
		return;
	machine = g_strdup_printf ("/layers/%s", parts[1]);
	/* descend while the path continues into nested machines */
	for (i = 2; parts[i] != NULL && parts[i + 1] != NULL && parts[i + 2] != NULL &&
	            g_strcmp0 (parts[i], "states") == 0 && g_strcmp0 (parts[i + 2], "machine") == 0; i += 3)
	{
		gchar *next = g_strdup_printf ("%s/states/%s/machine", machine, parts[i + 1]);

		g_free (machine);
		machine = next;
	}
	if (!lrg_editor_document_has_path (doc_of (self), machine))
		return;
	g_free (self->machine);
	self->machine = g_steal_pointer (&machine);
	rebuild_canvas (self);
	rebuild_chrome (self);
	if (parts[i] != NULL && parts[i + 1] != NULL && g_strcmp0 (parts[i], "states") == 0)
	{
		g_autofree gchar *states = pj (self->machine, "states");
		g_autofree gchar *sp = pj (states, parts[i + 1]);

		lrg_editor_anim_graph_editor_select_state (self, scalar_at (self, sp, "name"));
	}
	else if (parts[i] != NULL && parts[i + 1] != NULL &&
	         (g_strcmp0 (parts[i], "transitions") == 0 || g_strcmp0 (parts[i], "any_state") == 0))
	{
		g_autofree gchar *list = pj (self->machine, parts[i]);
		g_autofree gchar *tp = pj (list, parts[i + 1]);
		const gchar      *from = g_strcmp0 (parts[i], "any_state") == 0 ? ANY_ID : scalar_at (self, tp, "from");

		lrg_editor_anim_graph_editor_select_transition (self, from, scalar_at (self, tp, "to"));
		return;
	}
	lrg_node_graph_frame_selection (self->canvas, FALSE);
	/* one card framed alone would fill the view: keep its neighbours */
	if (lrg_node_graph_get_zoom (self->canvas) > 1.0f)
		lrg_node_graph_set_zoom (self->canvas, 1.0f);
}

static gchar *
anim_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (editor);
	g_autofree gchar         *states = pj (self->machine, "states");
	g_autofree gchar         *trans = pj (self->machine, "transitions");
	const gchar              *active = lrg_editor_anim_graph_editor_get_active_state (self, NULL);

	return g_strdup_printf ("machine=%s states=%d transitions=%d selected=%s active=%s", self->machine,
	                        n_items (self, states), n_items (self, trans),
	                        self->sel_state != NULL ? self->sel_state : self->sel_to != NULL ? "transition" : "-",
	                        active != NULL ? active : "-");
}

static gboolean
anim_save (LrgEditorDataEditor  *editor,
           GError              **error)
{
	flush_group (LRG_EDITOR_ANIM_GRAPH_EDITOR (editor));
	return LRG_EDITOR_DATA_EDITOR_CLASS (lrg_editor_anim_graph_editor_parent_class)->save (editor, error);
}

static gboolean
anim_undo (LrgEditorDataEditor *editor)
{
	flush_group (LRG_EDITOR_ANIM_GRAPH_EDITOR (editor));
	return lrg_editor_document_undo (lrg_editor_data_editor_get_document (editor));
}

static gboolean
anim_redo (LrgEditorDataEditor *editor)
{
	flush_group (LRG_EDITOR_ANIM_GRAPH_EDITOR (editor));
	return lrg_editor_document_redo (lrg_editor_data_editor_get_document (editor));
}

static gboolean
lrg_editor_anim_graph_editor_step (LrgWidget *widget,
                                   gfloat     delta_time)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (widget);
	gboolean                  busy = LRG_WIDGET_CLASS (lrg_editor_anim_graph_editor_parent_class)->step (widget,
	                                                                                                     delta_time);

	/* deferred rebuilds: never inside a widget's own signal emission */
	if (self->canvas_stale)
		rebuild_canvas (self);
	if (self->rows_stale)
		rebuild_rows (self);
	if (self->grid_stale)
		rebuild_grid (self);
	else
		rebuild_conditions (self);
	if (lrg_breadcrumb_get_n_segments (self->crumbs) == 0)
		rebuild_chrome (self);
	update_highlights (self);
	sync_live_controls (self);
	update_caption (self);

	/* the scrub bar follows the base layer's state while playing */
	if (self->playing)
	{
		LrgAnimationState *st = base_state (self);

		self->syncing = TRUE;
		if (st != NULL)
			lrg_slider_set_value (self->scrub, CLAMP (fmodf (lrg_animation_state_get_normalized_time (st), 1.0f), 0.0f,
			                                          1.0f));
		self->syncing = FALSE;
	}
	/* frame the graph while the dock settles on its size */
	if (self->framed < 3 && lrg_widget_get_width (LRG_WIDGET (self->canvas)) > 200.0f)
	{
		self->framed++;
		lrg_node_graph_frame_all (self->canvas, FALSE);
		/* a big machine in a narrow tab: stay readable, scroll instead */
		if (lrg_node_graph_get_zoom (self->canvas) < 0.6f)
			lrg_node_graph_set_zoom (self->canvas, 0.6f);
	}
	return busy || self->playing;
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
lrg_editor_anim_graph_editor_constructed (GObject *object)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (object);
	LrgEditorDataEditor      *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp             *app;
	LrgWidget                *left, *left_scroll, *adds, *center, *top, *side, *preview_box, *preview_bar;
	LrgWidget                *inspector, *inspector_scroll;
	LrgSplitter              *outer, *inner, *right;
	static const gchar * const types[] = { "float", "int", "bool", "trigger" };
	guint                      i;

	G_OBJECT_CLASS (lrg_editor_anim_graph_editor_parent_class)->constructed (object);
	app = app_of (self);
	self->machine = g_strdup ("/layers/0");
	load_graph (self);

	/* --- left: parameters and clip slots --- */
	left = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (left, "anim-left");
	lrg_container_set_spacing (LRG_CONTAINER (left), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (left), 8.0f);
	add_to (left, heading_label ("PARAMETERS"));
	self->param_rows = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->param_rows, "anim-params");
	lrg_container_set_spacing (LRG_CONTAINER (self->param_rows), 3.0f);
	lrg_container_add_child (LRG_CONTAINER (left), self->param_rows);
	g_object_unref (self->param_rows);
	adds = NULL;
	for (i = 0; i < G_N_ELEMENTS (types); i++)
	{
		g_autofree gchar *name = g_strdup_printf ("anim-add-%s", types[i]);
		g_autofree gchar *text = g_strdup_printf ("+ %c%s", g_ascii_toupper (types[i][0]), types[i] + 1);
		LrgWidget        *b = small_button (name, text, G_CALLBACK (on_param_add), self);

		/* two buttons per row: the column is narrow */
		if (i % 2 == 0)
		{
			adds = LRG_WIDGET (lrg_hbox_new ());
			lrg_container_set_spacing (LRG_CONTAINER (adds), 3.0f);
			lrg_container_add_child (LRG_CONTAINER (left), adds);
			g_object_unref (adds);
		}
		g_object_set_data (G_OBJECT (b), "type", (gpointer) types[i]);
		lrg_widget_set_fill_weight (b, 1.0f);
		add_to (adds, b);
	}
	add_to (left, heading_label ("CLIP SLOTS"));
	self->clip_rows = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->clip_rows, "anim-clips");
	lrg_container_set_spacing (LRG_CONTAINER (self->clip_rows), 3.0f);
	lrg_container_add_child (LRG_CONTAINER (left), self->clip_rows);
	g_object_unref (self->clip_rows);
	add_to (left, small_button ("anim-clips-from-model", "+ Slots from preview model", G_CALLBACK (on_clips_from_model),
	                            self));
	left_scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (left_scroll, "anim-left-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (left_scroll), left);
	g_object_unref (left);

	/* --- center: layer chips + breadcrumbs over the canvas --- */
	center = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (center), 4.0f);
	top = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (top, "anim-topbar");
	lrg_container_set_spacing (LRG_CONTAINER (top), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (top), 4.0f);
	self->layer_bar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->layer_bar, "anim-layers");
	lrg_container_set_spacing (LRG_CONTAINER (self->layer_bar), 2.0f);
	lrg_container_add_child (LRG_CONTAINER (top), self->layer_bar);
	g_object_unref (self->layer_bar);
	self->crumbs = lrg_breadcrumb_new ();
	lrg_widget_set_name (LRG_WIDGET (self->crumbs), "anim-crumbs");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->crumbs), 1.0f);
	g_signal_connect_object (self->crumbs, "activated", G_CALLBACK (on_crumb), self, 0);
	lrg_container_add_child (LRG_CONTAINER (top), LRG_WIDGET (self->crumbs));
	g_object_unref (self->crumbs);
	add_to (center, top);
	self->canvas = lrg_node_graph_new ();
	lrg_widget_set_name (LRG_WIDGET (self->canvas), "anim-canvas");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->canvas), 1.0f);
	lrg_node_graph_set_show_minimap (self->canvas, FALSE);
	lrg_node_graph_set_snap_to_grid (self->canvas, TRUE);
	lrg_node_graph_set_animate_links (self->canvas, TRUE);
	g_signal_connect_object (self->canvas, "can-connect", G_CALLBACK (on_can_connect), self, 0);
	g_signal_connect_object (self->canvas, "link-added", G_CALLBACK (on_link_added), self, 0);
	g_signal_connect_object (self->canvas, "link-removed", G_CALLBACK (on_link_removed), self, 0);
	g_signal_connect_object (self->canvas, "link-dropped", G_CALLBACK (on_link_dropped), self, 0);
	g_signal_connect_object (self->canvas, "node-moved", G_CALLBACK (on_node_moved), self, 0);
	g_signal_connect_object (self->canvas, "node-removed", G_CALLBACK (on_node_removed), self, 0);
	g_signal_connect_object (self->canvas, "node-duplicated", G_CALLBACK (on_node_duplicated), self, 0);
	g_signal_connect_object (self->canvas, "node-activated", G_CALLBACK (on_node_activated), self, 0);
	g_signal_connect_object (self->canvas, "selection-changed", G_CALLBACK (on_canvas_selection), self, 0);
	lrg_container_add_child (LRG_CONTAINER (center), LRG_WIDGET (self->canvas));
	g_object_unref (self->canvas);

	/* --- right: preview over the inspector --- */
	preview_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (preview_box), 4.0f);
	self->view = editor_anim_view_new (app, "anim-preview");
	editor_anim_view_set_pose_func (self->view, pose_from_graph, self);
	lrg_widget_set_fill_weight (self->view, 1.0f);
	g_signal_connect_object (self->view, "playing-toggled", G_CALLBACK (on_view_playing), self, 0);
	lrg_container_add_child (LRG_CONTAINER (preview_box), self->view);
	g_object_unref (self->view);
	preview_bar = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (preview_bar), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (preview_bar), 4.0f);
	self->model_combo = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->model_combo), "anim-model");
	g_signal_connect_object (self->model_combo, "changed", G_CALLBACK (on_model_combo), self, 0);
	add_to (preview_bar, LRG_WIDGET (g_object_ref (self->model_combo)));
	g_object_unref (self->model_combo);
	self->scrub = lrg_slider_new_with_range (0.0, 1.0, 0.0);
	lrg_widget_set_name (LRG_WIDGET (self->scrub), "anim-scrub");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->scrub), 1.0f);
	g_signal_connect_object (self->scrub, "value-changed", G_CALLBACK (on_scrub), self, 0);
	lrg_container_add_child (LRG_CONTAINER (preview_bar), LRG_WIDGET (self->scrub));
	g_object_unref (self->scrub);
	add_to (preview_box, preview_bar);

	inspector = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (inspector, "anim-inspector");
	lrg_container_set_spacing (LRG_CONTAINER (inspector), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (inspector), 8.0f);
	self->inspector_title = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->inspector_title, "anim-inspector-title");
	lrg_container_add_child (LRG_CONTAINER (inspector), self->inspector_title);
	g_object_unref (self->inspector_title);
	self->blend_space = blend_space_new (self);
	lrg_container_add_child (LRG_CONTAINER (inspector), self->blend_space);
	g_object_unref (self->blend_space);
	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "anim-grid");
	lrg_property_grid_set_label_width (self->grid, 112.0f);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	lrg_container_add_child (LRG_CONTAINER (inspector), LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	self->cond_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->cond_box, "anim-conditions");
	lrg_container_set_spacing (LRG_CONTAINER (self->cond_box), 4.0f);
	lrg_container_add_child (LRG_CONTAINER (inspector), self->cond_box);
	g_object_unref (self->cond_box);
	inspector_scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (inspector_scroll, "anim-inspector-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (inspector_scroll), inspector);
	g_object_unref (inspector);

	right = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	lrg_widget_set_name (LRG_WIDGET (right), "anim-side");
	lrg_splitter_set_start_child (right, preview_box);
	g_object_unref (preview_box);
	lrg_splitter_set_end_child (right, inspector_scroll);
	g_object_unref (inspector_scroll);
	lrg_splitter_set_default_fraction (right, 0.45f);
	lrg_splitter_set_fraction (right, 0.45f, FALSE);
	lrg_splitter_set_min_sizes (right, 180.0f, 160.0f);
	side = LRG_WIDGET (right);

	inner = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (inner), "anim-inner");
	lrg_splitter_set_start_child (inner, center);
	g_object_unref (center);
	lrg_splitter_set_end_child (inner, side);
	g_object_unref (side);
	lrg_splitter_set_default_fraction (inner, 0.64f);
	lrg_splitter_set_fraction (inner, 0.64f, FALSE);
	lrg_splitter_set_min_sizes (inner, 280.0f, 260.0f);

	outer = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (outer), "anim-split");
	lrg_splitter_set_start_child (outer, left_scroll);
	g_object_unref (left_scroll);
	lrg_splitter_set_end_child (outer, LRG_WIDGET (inner));
	g_object_unref (inner);
	lrg_splitter_set_default_fraction (outer, 0.18f);
	lrg_splitter_set_fraction (outer, 0.18f, FALSE);
	lrg_splitter_set_min_sizes (outer, 190.0f, 500.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (outer));
	g_object_unref (outer);

	lrg_editor_data_editor_add_tool (base, editor_chip_new ("anim-restart", LRG_ICON_REFRESH, "Restart",
	                                                        on_restart_chip, self));

	self->playing = TRUE;
	sync_model_combo (self);
	sync_preview_model (self);
	rebuild_canvas (self);
	rebuild_chrome (self);
	rebuild_rows (self);
	rebuild_grid (self);
	if (app != NULL)
		lrg_editor_anim_graph_editor_install (app);
}

static void
lrg_editor_anim_graph_editor_dispose (GObject *object)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (object);

	g_clear_handle_id (&self->group_idle, g_source_remove);
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
	if (self->view != NULL)
		editor_anim_view_set_pose_func (self->view, NULL, NULL);
	if (self->blend_space != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->blend_space), NULL, NULL, NULL);
	self->blend_space = NULL;
	G_OBJECT_CLASS (lrg_editor_anim_graph_editor_parent_class)->dispose (object);
}

static void
lrg_editor_anim_graph_editor_finalize (GObject *object)
{
	LrgEditorAnimGraphEditor *self = LRG_EDITOR_ANIM_GRAPH_EDITOR (object);

	g_clear_object (&self->graph);
	g_clear_object (&self->source);
	g_clear_pointer (&self->clip_cache, g_hash_table_unref);
	g_clear_pointer (&self->live, g_hash_table_unref);
	g_clear_pointer (&self->model_list, g_ptr_array_unref);
	g_free (self->load_error);
	g_free (self->machine);
	g_free (self->sel_state);
	g_free (self->sel_from);
	g_free (self->sel_to);
	g_free (self->preview_model);
	g_free (self->poses);
	g_free (self->grid_sig);
	g_free (self->rows_sig);
	g_free (self->cond_sig);
	g_free (self->hl_sig);
	G_OBJECT_CLASS (lrg_editor_anim_graph_editor_parent_class)->finalize (object);
}

static void
lrg_editor_anim_graph_editor_class_init (LrgEditorAnimGraphEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_anim_graph_editor_constructed;
	object_class->dispose = lrg_editor_anim_graph_editor_dispose;
	object_class->finalize = lrg_editor_anim_graph_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_anim_graph_editor_step;
	data_class->refresh = anim_refresh;
	data_class->validate = anim_validate;
	data_class->reveal = anim_reveal;
	data_class->get_state = anim_get_state;
	data_class->save = anim_save;
	data_class->undo = anim_undo;
	data_class->redo = anim_redo;
}

static void
lrg_editor_anim_graph_editor_init (LrgEditorAnimGraphEditor *self)
{
	self->clip_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
	self->live = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
}

/* ==========================================================================
 * Public API: construction
 * ========================================================================== */

LrgEditorAnimGraphEditor *
lrg_editor_anim_graph_editor_new (LrgEditorApp      *app,
                                  LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_ANIM_GRAPH_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorAnimGraphEditor *
lrg_editor_anim_graph_editor_open (LrgEditorApp  *app,
                                   const gchar   *path,
                                   GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not an animation graph", path);
		return NULL;
	}
	return LRG_EDITOR_ANIM_GRAPH_EDITOR (e);
}

/* A clip key from an animation name ("Armature|Walk Cycle" -> "Walk_Cycle"). */
static gchar *
clip_key_for (const gchar *animation)
{
	const gchar *bar = strrchr (animation, '|');
	gchar       *key = g_strdup (bar != NULL && bar[1] != '\0' ? bar + 1 : animation);
	gchar       *p;

	for (p = key; *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-')
			*p = '_';
	if (!g_ascii_isalpha (key[0]) && key[0] != '_')
	{
		gchar *prefixed = g_strconcat ("clip_", key, NULL);

		g_free (key);
		key = prefixed;
	}
	return key;
}

gboolean
lrg_editor_anim_graph_editor_create (LrgEditorApp  *app,
                                     const gchar   *path,
                                     const gchar   *model,
                                     GError       **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *stem = NULL;
	g_autofree gchar *ystem = NULL;
	GString          *text;
	const gchar      *dot;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	abs = (app != NULL && !g_path_is_absolute (path)) ? editor_app_project_path (app, path) : g_strdup (path);
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
	base = g_path_get_basename (abs);
	dot = strrchr (base, '.');
	stem = dot != NULL && dot != base ? g_strndup (base, (gsize) (dot - base)) : g_strdup (base);
	ystem = yaml_str (stem);
	text = g_string_new (NULL);
	g_string_append_printf (text, "version: 1\nname: %s\n", ystem);

	if (model == NULL)
		g_string_append (text, "layers:\n  - name: base\n    default: idle\n    states:\n"
		                       "      - name: idle\n        editor_pos: [0, 0]\n");
	else
	{
		g_autofree gchar            *mabs = (app != NULL && !g_path_is_absolute (model))
		                                    ? editor_app_project_path (app, model) : g_strdup (model);
		g_autoptr(LrgGltfAnimation)  gltf = lrg_gltf_animation_new_from_file (mabs, error);
		g_autofree gchar            *rel = NULL;
		g_autofree gchar            *yrel = NULL;
		g_autoptr(GPtrArray)         keys = g_ptr_array_new_with_free_func (g_free);
		guint                        i, n;

		if (gltf == NULL)
		{
			g_string_free (text, TRUE);
			return FALSE;
		}
		rel = editor_mat_relative_to (dir, mabs);
		yrel = yaml_str (rel);
		g_string_append_printf (text, "editor_preview: %s\n", yrel);
		n = lrg_gltf_animation_get_clip_count (gltf);
		if (n > 0)
			g_string_append (text, "clips:\n");
		for (i = 0; i < n; i++)
		{
			const gchar      *anim = lrg_gltf_animation_get_clip_name (gltf, i);
			g_autofree gchar *key0 = clip_key_for (anim);
			gchar            *key = g_strdup (key0);
			g_autofree gchar *ykey = NULL;
			g_autofree gchar *yanim = yaml_str (anim);
			guint             k, dup = 1;

			for (k = 0; k < keys->len; k++)
				if (g_strcmp0 (g_ptr_array_index (keys, k), key) == 0)
				{
					g_free (key);
					key = g_strdup_printf ("%s%u", key0, ++dup);
					k = (guint) -1;
				}
			g_ptr_array_add (keys, key);
			ykey = yaml_str (key);
			g_string_append_printf (text, "  %s: {source: %s, animation: %s}\n", ykey, yrel, yanim);
		}
		g_string_append (text, "layers:\n  - name: base\n");
		if (keys->len == 0)
			g_string_append (text, "    default: idle\n    states:\n      - name: idle\n        editor_pos: [0, 0]\n");
		else
		{
			g_autofree gchar *def = yaml_str (g_ptr_array_index (keys, 0));

			g_string_append_printf (text, "    default: %s\n    states:\n", def);
			for (i = 0; i < keys->len; i++)
			{
				g_autofree gchar *yk = yaml_str (g_ptr_array_index (keys, i));

				g_string_append_printf (text, "      - name: %s\n        clip: %s\n        editor_pos: [%d, %d]\n", yk,
				                        yk, (gint) ((i % GRID_COLS) * GRID_DX), (gint) ((i / GRID_COLS) * GRID_DY));
			}
		}
	}
	if (!g_file_set_contents (abs, text->str, -1, error))
	{
		g_string_free (text, TRUE);
		return FALSE;
	}
	g_string_free (text, TRUE);
	return TRUE;
}

/* ==========================================================================
 * Public API: model and navigation
 * ========================================================================== */

LrgAnimGraph *
lrg_editor_anim_graph_editor_get_graph (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return self->graph;
}

const gchar *
lrg_editor_anim_graph_editor_get_load_error (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return self->load_error;
}

LrgNodeGraph *
lrg_editor_anim_graph_editor_get_canvas (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return self->canvas;
}

const gchar *
lrg_editor_anim_graph_editor_get_machine_path (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return self->machine;
}

guint
lrg_editor_anim_graph_editor_get_layer (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), 0);
	return machine_layer (self->machine);
}

/* Shows machine @machine (no checks) and refits the canvas. */
static void
show_machine (LrgEditorAnimGraphEditor *self,
              const gchar              *machine)
{
	g_free (self->machine);
	self->machine = g_strdup (machine);
	g_clear_pointer (&self->sel_state, g_free);
	g_clear_pointer (&self->sel_from, g_free);
	g_clear_pointer (&self->sel_to, g_free);
	rebuild_canvas (self);
	rebuild_chrome (self);
	self->grid_stale = TRUE;
	self->framed = 0;
}

gboolean
lrg_editor_anim_graph_editor_set_layer (LrgEditorAnimGraphEditor *self,
                                        guint                     layer)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	if (layer >= n_layers (self))
		return FALSE;
	path = pji ("/layers", layer);
	show_machine (self, path);
	return TRUE;
}

gboolean
lrg_editor_anim_graph_editor_enter_state (LrgEditorAnimGraphEditor *self,
                                          const gchar              *state)
{
	g_autofree gchar *sp = NULL;
	g_autofree gchar *mp = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	sp = state_path_in (self, self->machine, state);
	if (sp == NULL || !has_key (self, sp, "machine"))
		return FALSE;
	mp = pj (sp, "machine");
	show_machine (self, mp);
	return TRUE;
}

gboolean
lrg_editor_anim_graph_editor_go_up (LrgEditorAnimGraphEditor *self)
{
	g_auto(GStrv) parts = NULL;
	GString      *up;
	guint         n, i;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	parts = lrg_editor_document_path_split (self->machine, NULL);
	n = parts != NULL ? g_strv_length (parts) : 0;
	if (n < 5)
		return FALSE;
	/* drop "states/<i>/machine" */
	up = g_string_new ("");
	for (i = 0; i + 3 < n; i++)
	{
		g_autofree gchar *joined = pj (up->str, parts[i]);

		g_string_assign (up, joined);
	}
	{
		g_autofree gchar *machine = g_string_free (up, FALSE);
		g_autofree gchar *child_state = NULL;
		g_autofree gchar *states = pj (machine, "states");
		g_autofree gchar *sp = pj (states, parts[n - 2]);

		child_state = g_strdup (scalar_at (self, sp, "name"));
		show_machine (self, machine);
		/* the state we came from stays selected */
		if (child_state != NULL)
			lrg_editor_anim_graph_editor_select_state (self, child_state);
	}
	return TRUE;
}

GStrv
lrg_editor_anim_graph_editor_get_breadcrumbs (LrgEditorAnimGraphEditor *self)
{
	g_auto(GStrv)     parts = NULL;
	GPtrArray        *out;
	g_autofree gchar *walk = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	out = g_ptr_array_new ();
	parts = lrg_editor_document_path_split (self->machine, NULL);
	if (parts != NULL && parts[0] != NULL && parts[1] != NULL)
	{
		walk = g_strdup_printf ("/layers/%s", parts[1]);
		g_ptr_array_add (out, g_strdup (scalar_at (self, walk, "name") != NULL ? scalar_at (self, walk, "name")
		                                                                      : "layer"));
		for (i = 2; parts[i] != NULL && parts[i + 1] != NULL && parts[i + 2] != NULL; i += 3)
		{
			g_autofree gchar *states = pj (walk, "states");
			g_autofree gchar *sp = pj (states, parts[i + 1]);

			g_ptr_array_add (out, g_strdup (scalar_at (self, sp, "name") != NULL ? scalar_at (self, sp, "name") : "?"));
			g_free (walk);
			walk = pj (sp, "machine");
		}
	}
	g_ptr_array_add (out, NULL);
	return (GStrv) g_ptr_array_free (out, FALSE);
}

/* ==========================================================================
 * Public API: editing
 * ========================================================================== */

static gboolean fail_arg (GError **error, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

/* Sets an invalid-argument error; always FALSE (for `return fail_arg ()`). */
static gboolean
fail_arg (GError      **error,
          const gchar  *format,
          ...)
{
	va_list args;

	va_start (args, format);
	if (error != NULL)
		g_propagate_error (error, g_error_new_valist (G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, format, args));
	va_end (args);
	return FALSE;
}

gboolean
lrg_editor_anim_graph_editor_add_layer (LrgEditorAnimGraphEditor  *self,
                                        const gchar               *name,
                                        GError                   **error)
{
	g_autofree gchar *yn = NULL;
	g_autofree gchar *text = NULL;
	guint             i;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	for (i = 0; i < n_layers (self); i++)
	{
		g_autofree gchar *lp = pji ("/layers", i);

		if (g_strcmp0 (scalar_at (self, lp, "name"), name) == 0)
			return fail_arg (error, "A layer named '%s' exists", name);
	}
	yn = yaml_str (name);
	text = g_strdup_printf ("{name: %s, default: idle, states: [{name: idle, editor_pos: [0, 0]}]}", yn);
	edit_begin (self, "Add Layer");
	ok = ensure_list (self, "", "layers", error) && lrg_editor_document_insert_item (doc_of (self), "/layers", -1,
	                                                                                  text, error);
	edit_end (self);
	return ok;
}

gboolean
lrg_editor_anim_graph_editor_remove_layer (LrgEditorAnimGraphEditor *self,
                                           const gchar              *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	if (n_layers (self) < 2)
		return FALSE;
	for (i = 0; i < n_layers (self); i++)
	{
		g_autofree gchar *lp = pji ("/layers", i);

		if (g_strcmp0 (scalar_at (self, lp, "name"), name) == 0)
		{
			gboolean ok;

			if (machine_layer (self->machine) >= i)
				show_machine (self, "/layers/0");
			edit_begin (self, "Remove Layer");
			ok = lrg_editor_document_remove (doc_of (self), lp, NULL);
			edit_end (self);
			return ok;
		}
	}
	return FALSE;
}

gboolean
lrg_editor_anim_graph_editor_add_state (LrgEditorAnimGraphEditor  *self,
                                        const gchar               *name,
                                        const gchar               *kind,
                                        const gchar               *clip,
                                        gfloat                     x,
                                        gfloat                     y,
                                        GError                   **error)
{
	g_autofree gchar *states = NULL;
	g_autofree gchar *yn = NULL;
	g_autofree gchar *sx = num_str (rint (x));
	g_autofree gchar *sy = num_str (rint (y));
	g_autofree gchar *motion = NULL;
	g_autofree gchar *text = NULL;
	gboolean          first;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	if (*name == '\0' || g_strcmp0 (name, ANY_ID) == 0)
		return fail_arg (error, "'%s' is not a valid state name", name);
	if (state_index_in (self, self->machine, name) >= 0)
		return fail_arg (error, "A state named '%s' exists", name);
	if (kind == NULL)
		kind = clip != NULL ? "clip" : "empty";
	states = pj (self->machine, "states");
	first = n_items (self, states) == 0;
	edit_begin (self, "Add State");
	if (g_strcmp0 (kind, "clip") == 0)
	{
		g_auto(GStrv) keys = clip_keys (self);

		if (clip == NULL)
			clip = keys[0];
		if (clip != NULL)
		{
			g_autofree gchar *cp = pj ("/clips", clip);
			g_autofree gchar *yc = yaml_str (clip);

			if (!lrg_editor_document_has_path (doc_of (self), cp))
			{
				edit_end (self);
				return fail_arg (error, "No clip slot '%s' (declare it under clips:)", clip);
			}
			motion = g_strdup_printf (", clip: %s", yc);
		}
	}
	else if (g_strcmp0 (kind, "blend_tree") == 0)
	{
		g_autoptr(GPtrArray) nums = parameter_names (self, TRUE);

		if (nums->len == 0)
		{
			lrg_editor_anim_graph_editor_add_parameter (self, "blend", "float", NULL);
			g_ptr_array_add (nums, g_strdup ("blend"));
		}
		motion = g_strdup_printf (", blend_tree: {type: 1d, parameter: %s, children: []}",
		                          (const gchar *) g_ptr_array_index (nums, 0));
	}
	else if (g_strcmp0 (kind, "machine") == 0)
		motion = g_strdup (", machine: {default: entry, states: [{name: entry, editor_pos: [0, 0]}]}");
	else if (g_strcmp0 (kind, "empty") != 0)
	{
		edit_end (self);
		return fail_arg (error, "Unknown state kind '%s' (clip, blend_tree, machine, empty)", kind);
	}
	yn = yaml_str (name);
	text = g_strdup_printf ("{name: %s%s, editor_pos: [%s, %s]}", yn, motion != NULL ? motion : "", sx, sy);
	ok = ensure_list (self, self->machine, "states", error) &&
	     lrg_editor_document_insert_item (doc_of (self), states, -1, text, error);
	if (ok && first)
	{
		g_autofree gchar *dp = pj (self->machine, "default");

		lrg_editor_document_set_string (doc_of (self), dp, name, 0, NULL);
	}
	edit_end (self);
	return ok;
}

gboolean
lrg_editor_anim_graph_editor_remove_state (LrgEditorAnimGraphEditor *self,
                                           const gchar              *name)
{
	gboolean ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	if (state_index_in (self, self->machine, name) < 0)
		return FALSE;
	edit_begin (self, "Delete State");
	ok = remove_state_raw (self, self->machine, name);
	edit_end (self);
	return ok;
}

gboolean
lrg_editor_anim_graph_editor_rename_state (LrgEditorAnimGraphEditor  *self,
                                           const gchar               *name,
                                           const gchar               *new_name,
                                           GError                   **error)
{
	g_autofree gchar *sp = NULL;
	g_autofree gchar *np = NULL;
	g_autofree gchar *trans = NULL;
	g_autofree gchar *any = NULL;
	gint              i;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	if (g_strcmp0 (name, new_name) == 0)
		return TRUE;
	sp = state_path_in (self, self->machine, name);
	if (sp == NULL)
		return fail_arg (error, "No state '%s'", name);
	if (new_name == NULL || *new_name == '\0' || g_strcmp0 (new_name, ANY_ID) == 0)
		return fail_arg (error, "'%s' is not a valid state name", new_name != NULL ? new_name : "");
	if (state_index_in (self, self->machine, new_name) >= 0)
		return fail_arg (error, "A state named '%s' exists", new_name);
	trans = pj (self->machine, "transitions");
	any = pj (self->machine, "any_state");
	edit_begin (self, "Rename State");
	np = pj (sp, "name");
	lrg_editor_document_set_string (doc_of (self), np, new_name, 0, NULL);
	for (i = 0; i < n_items (self, trans); i++)
	{
		g_autofree gchar *tp = pji (trans, (guint) i);
		g_autofree gchar *fp = pj (tp, "from");
		g_autofree gchar *xp = pj (tp, "to");

		if (g_strcmp0 (lrg_editor_document_get_scalar (doc_of (self), fp), name) == 0)
			lrg_editor_document_set_string (doc_of (self), fp, new_name, 0, NULL);
		if (g_strcmp0 (lrg_editor_document_get_scalar (doc_of (self), xp), name) == 0)
			lrg_editor_document_set_string (doc_of (self), xp, new_name, 0, NULL);
	}
	for (i = 0; i < n_items (self, any); i++)
	{
		g_autofree gchar *tp = pji (any, (guint) i);
		g_autofree gchar *xp = pj (tp, "to");

		if (g_strcmp0 (lrg_editor_document_get_scalar (doc_of (self), xp), name) == 0)
			lrg_editor_document_set_string (doc_of (self), xp, new_name, 0, NULL);
	}
	if (g_strcmp0 (scalar_at (self, self->machine, "default"), name) == 0)
	{
		g_autofree gchar *dp = pj (self->machine, "default");

		lrg_editor_document_set_string (doc_of (self), dp, new_name, 0, NULL);
	}
	if (g_strcmp0 (self->sel_state, name) == 0)
	{
		g_free (self->sel_state);
		self->sel_state = g_strdup (new_name);
	}
	if (g_strcmp0 (self->sel_from, name) == 0)
	{
		g_free (self->sel_from);
		self->sel_from = g_strdup (new_name);
	}
	if (g_strcmp0 (self->sel_to, name) == 0)
	{
		g_free (self->sel_to);
		self->sel_to = g_strdup (new_name);
	}
	edit_end (self);
	rebuild_canvas (self);
	self->grid_stale = TRUE;
	return TRUE;
}

gboolean
lrg_editor_anim_graph_editor_set_default_state (LrgEditorAnimGraphEditor *self,
                                                const gchar              *name)
{
	g_autofree gchar *dp = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	if (state_index_in (self, self->machine, name) < 0)
		return FALSE;
	dp = pj (self->machine, "default");
	edit_begin (self, "Set Default State");
	ok = lrg_editor_document_set_string (doc_of (self), dp, name, 0, NULL);
	edit_end (self);
	return ok;
}

gboolean
lrg_editor_anim_graph_editor_set_state_position (LrgEditorAnimGraphEditor *self,
                                                 const gchar              *name,
                                                 gfloat                    x,
                                                 gfloat                    y)
{
	g_autofree gchar *sp = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	sp = state_path_in (self, self->machine, name);
	if (sp == NULL)
		return FALSE;
	edit_begin (self, "Move State");
	ok = write_pos (self, sp, "editor_pos", x, y);
	edit_end (self);
	return ok;
}

gchar *
lrg_editor_anim_graph_editor_get_state_path (LrgEditorAnimGraphEditor *self,
                                             const gchar              *name)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return state_path_in (self, self->machine, name);
}

gboolean
lrg_editor_anim_graph_editor_add_transition (LrgEditorAnimGraphEditor  *self,
                                             const gchar               *from,
                                             const gchar               *to,
                                             gfloat                     duration,
                                             GError                   **error)
{
	g_autofree gchar *tp = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (from != NULL && to != NULL, FALSE);
	if (g_strcmp0 (from, ANY_ID) != 0 && state_index_in (self, self->machine, from) < 0)
		return fail_arg (error, "No state '%s'", from);
	if (state_index_in (self, self->machine, to) < 0)
		return fail_arg (error, "No state '%s'", to);
	if (g_strcmp0 (from, to) == 0)
		return fail_arg (error, "A transition from '%s' to itself: use an any-state transition", from);
	tp = transition_path_in (self, self->machine, from, to);
	if (tp != NULL)
		return fail_arg (error, "A transition to '%s' from there exists", to);
	edit_begin (self, "Add Transition");
	ok = add_transition_raw (self, self->machine, from, to, duration, error);
	edit_end (self);
	return ok;
}

gboolean
lrg_editor_anim_graph_editor_remove_transition (LrgEditorAnimGraphEditor *self,
                                                const gchar              *from,
                                                const gchar              *to)
{
	g_autofree gchar *tp = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	tp = transition_path_in (self, self->machine, from, to);
	if (tp == NULL)
		return FALSE;
	edit_begin (self, "Delete Transition");
	ok = lrg_editor_document_remove (doc_of (self), tp, NULL);
	edit_end (self);
	return ok;
}

gchar *
lrg_editor_anim_graph_editor_get_transition_path (LrgEditorAnimGraphEditor *self,
                                                  const gchar              *from,
                                                  const gchar              *to)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return transition_path_in (self, self->machine, from, to);
}

gboolean
lrg_editor_anim_graph_editor_add_condition (LrgEditorAnimGraphEditor  *self,
                                            const gchar               *from,
                                            const gchar               *to,
                                            const gchar               *parameter,
                                            const gchar               *op,
                                            const gchar               *value,
                                            GError                   **error)
{
	g_autofree gchar *tp = NULL;
	g_autofree gchar *cl = NULL;
	g_autofree gchar *yp = NULL;
	g_autofree gchar *text = NULL;
	gboolean          ok;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (parameter != NULL, FALSE);
	tp = transition_path_in (self, self->machine, from, to);
	if (tp == NULL)
		return fail_arg (error, "No transition to '%s'", to != NULL ? to : "");
	if (parameter_type (self, parameter) == NULL)
		return fail_arg (error, "No parameter '%s'", parameter);
	if (op != NULL)
	{
		gboolean known = FALSE;

		for (i = 0; i < G_N_ELEMENTS (num_ops); i++)
			known = known || g_strcmp0 (num_ops[i], op) == 0;
		if (!known)
			return fail_arg (error, "Unknown operator '%s' (==, !=, >, <, >=, <=)", op);
	}
	yp = yaml_str (parameter);
	text = op != NULL ? g_strdup_printf ("{parameter: %s, op: '%s', value: %s}", yp, op, value != NULL ? value : "0")
	                  : g_strdup_printf ("{parameter: %s}", yp);
	cl = pj (tp, "conditions");
	edit_begin (self, "Add Condition");
	ok = ensure_list (self, tp, "conditions", error) &&
	     lrg_editor_document_insert_item (doc_of (self), cl, -1, text, error);
	edit_end (self);
	return ok;
}

gboolean
lrg_editor_anim_graph_editor_add_parameter (LrgEditorAnimGraphEditor  *self,
                                            const gchar               *name,
                                            const gchar               *type,
                                            GError                   **error)
{
	g_autofree gchar *yn = NULL;
	g_autofree gchar *text = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	if (g_strcmp0 (type, "float") != 0 && g_strcmp0 (type, "int") != 0 && g_strcmp0 (type, "bool") != 0 &&
	    g_strcmp0 (type, "trigger") != 0)
		return fail_arg (error, "Unknown parameter type '%s' (float, int, bool, trigger)", type != NULL ? type : "");
	if (*name == '\0' || parameter_type (self, name) != NULL)
		return fail_arg (error, "A parameter named '%s' exists (or the name is empty)", name);
	yn = yaml_str (name);
	text = g_strdup_printf ("{name: %s, type: %s}", yn, type);
	/* nested inside another group (add_state) this joins it */
	lrg_editor_document_begin_group (doc_of (self), "Add Parameter");
	ok = ensure_list (self, "", "parameters", error) &&
	     lrg_editor_document_insert_item (doc_of (self), "/parameters", -1, text, error);
	lrg_editor_document_end_group (doc_of (self));
	return ok;
}

gboolean
lrg_editor_anim_graph_editor_remove_parameter (LrgEditorAnimGraphEditor *self,
                                               const gchar              *name)
{
	gint n = n_items (self, "/parameters");
	gint i;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *pp = pji ("/parameters", (guint) i);

		if (g_strcmp0 (scalar_at (self, pp, "name"), name) == 0)
		{
			gboolean ok;

			g_hash_table_remove (self->live, name);
			edit_begin (self, "Remove Parameter");
			ok = lrg_editor_document_remove (doc_of (self), pp, NULL);
			edit_end (self);
			return ok;
		}
	}
	return FALSE;
}

guint
lrg_editor_anim_graph_editor_declare_model_clips (LrgEditorAnimGraphEditor *self,
                                                  const gchar              *model)
{
	g_autofree gchar            *abs = NULL;
	g_autofree gchar            *dir = NULL;
	g_autofree gchar            *rel = NULL;
	g_autoptr(LrgGltfAnimation)  gltf = NULL;
	g_autoptr(GError)            error = NULL;
	guint                        i, n, added = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), 0);
	abs = model != NULL ? project_abs (self, model) : g_strdup (self->preview_model);
	if (abs == NULL)
	{
		if (app_of (self) != NULL)
			editor_app_notify (app_of (self), EDITOR_NOTIFY_WARNING, "Pick a preview model first");
		return 0;
	}
	gltf = lrg_gltf_animation_new_from_file (abs, &error);
	if (gltf == NULL)
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return 0;
	}
	dir = base_dir_of (self);
	rel = editor_mat_relative_to (dir, abs);
	n = lrg_gltf_animation_get_clip_count (gltf);
	edit_begin (self, "Add Clip Slots");
	if (lrg_editor_document_get_kind (doc_of (self), "/clips") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		if (lrg_editor_document_has_path (doc_of (self), "/clips"))
			lrg_editor_document_replace (doc_of (self), "/clips", "{}", NULL);
		else
			lrg_editor_document_add_key (doc_of (self), "", "clips", "{}", 2, NULL);
	}
	for (i = 0; i < n; i++)
	{
		const gchar      *anim = lrg_gltf_animation_get_clip_name (gltf, i);
		g_auto(GStrv)     keys = clip_keys (self);
		g_autofree gchar *key0 = clip_key_for (anim);
		g_autofree gchar *key = g_strdup (key0);
		g_autofree gchar *yrel = yaml_str (rel);
		g_autofree gchar *yanim = yaml_str (anim);
		g_autofree gchar *text = NULL;
		gboolean          have = FALSE;
		guint             k, dup = 1;

		for (k = 0; keys[k] != NULL; k++)
		{
			g_autofree gchar *cp = pj ("/clips", keys[k]);

			if (g_strcmp0 (scalar_at (self, cp, "animation"), anim) == 0 &&
			    g_strcmp0 (scalar_at (self, cp, "source"), rel) == 0)
				have = TRUE;
		}
		if (have)
			continue;
		while (g_strv_contains ((const gchar * const *) keys, key))
		{
			g_free (key);
			key = g_strdup_printf ("%s%u", key0, ++dup);
		}
		text = g_strdup_printf ("{source: %s, animation: %s}", yrel, yanim);
		if (lrg_editor_document_add_key (doc_of (self), "/clips", key, text, -1, NULL))
			added++;
	}
	edit_end (self);
	return added;
}

/* ==========================================================================
 * Public API: selection
 * ========================================================================== */

void
lrg_editor_anim_graph_editor_select_state (LrgEditorAnimGraphEditor *self,
                                           const gchar              *name)
{
	LrgGraphNode *card;

	g_return_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self));
	g_clear_pointer (&self->sel_from, g_free);
	g_clear_pointer (&self->sel_to, g_free);
	g_free (self->sel_state);
	self->sel_state = (name != NULL && state_index_in (self, self->machine, name) >= 0) ? g_strdup (name) : NULL;
	self->syncing = TRUE;
	lrg_node_graph_unselect_all (self->canvas);
	card = self->sel_state != NULL ? lrg_node_graph_get_node (self->canvas, self->sel_state) : NULL;
	if (card != NULL)
		lrg_node_graph_select_node (self->canvas, card, FALSE);
	self->syncing = FALSE;
	self->grid_stale = TRUE;
}

gboolean
lrg_editor_anim_graph_editor_select_transition (LrgEditorAnimGraphEditor *self,
                                                const gchar              *from,
                                                const gchar              *to)
{
	g_autofree gchar *tp = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	tp = transition_path_in (self, self->machine, from, to);
	if (tp == NULL)
		return FALSE;
	g_clear_pointer (&self->sel_state, g_free);
	g_free (self->sel_from);
	g_free (self->sel_to);
	self->sel_from = g_strdup (from);
	self->sel_to = g_strdup (to);
	/* the canvas follows (rebuild selects the link) */
	rebuild_canvas (self);
	self->grid_stale = TRUE;
	return TRUE;
}

const gchar *
lrg_editor_anim_graph_editor_get_selected_state (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return self->sel_state;
}

gchar *
lrg_editor_anim_graph_editor_get_selected_transition (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	if (self->sel_to == NULL)
		return NULL;
	return transition_path_in (self, self->machine, self->sel_from, self->sel_to);
}

/* ==========================================================================
 * Public API: live preview
 * ========================================================================== */

gboolean
lrg_editor_anim_graph_editor_set_parameter_value (LrgEditorAnimGraphEditor *self,
                                                  const gchar              *name,
                                                  GVariant                 *value)
{
	g_autoptr(GVariant) owned = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL && value != NULL, FALSE);
	owned = g_variant_ref_sink (value);
	if (self->graph == NULL || !apply_live (self->graph, name, owned))
		return FALSE;
	g_hash_table_replace (self->live, g_strdup (name), g_variant_ref (owned));
	return TRUE;
}

GVariant *
lrg_editor_anim_graph_editor_get_parameter_value (LrgEditorAnimGraphEditor *self,
                                                  const gchar              *name)
{
	guint i, n;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	if (self->graph == NULL)
		return NULL;
	n = lrg_anim_graph_get_parameter_count (self->graph);
	for (i = 0; i < n; i++)
	{
		if (g_strcmp0 (lrg_anim_graph_get_parameter_name (self->graph, i), name) != 0)
			continue;
		switch (lrg_anim_graph_get_parameter_type (self->graph, name))
		{
		case LRG_ANIM_PARAMETER_FLOAT:
			return g_variant_ref_sink (g_variant_new_double (lrg_anim_graph_get_float (self->graph, name)));
		case LRG_ANIM_PARAMETER_INT:
			return g_variant_ref_sink (g_variant_new_int32 (lrg_anim_graph_get_int (self->graph, name)));
		default:
			return g_variant_ref_sink (g_variant_new_boolean (lrg_anim_graph_get_bool (self->graph, name)));
		}
	}
	return NULL;
}

gboolean
lrg_editor_anim_graph_editor_set_preview_model (LrgEditorAnimGraphEditor  *self,
                                                const gchar               *model,
                                                GError                   **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *rel = NULL;
	gboolean          ok = TRUE;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	if (model == NULL)
	{
		if (lrg_editor_document_has_path (doc_of (self), "/editor_preview"))
		{
			edit_begin (self, "Preview Model");
			ok = lrg_editor_document_remove (doc_of (self), "/editor_preview", error);
			edit_end (self);
		}
		sync_preview_model (self);
		return ok;
	}
	abs = project_abs (self, model);
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No model at %s", abs);
		return FALSE;
	}
	dir = base_dir_of (self);
	rel = editor_mat_relative_to (dir, abs);
	edit_begin (self, "Preview Model");
	if (lrg_editor_document_has_path (doc_of (self), "/editor_preview"))
		ok = lrg_editor_document_set_string (doc_of (self), "/editor_preview", rel, 0, error);
	else
	{
		g_autofree gchar *yrel = yaml_str (rel);

		ok = lrg_editor_document_add_key (doc_of (self), "", "editor_preview", yrel, -1, error);
	}
	edit_end (self);
	sync_preview_model (self);
	if (ok && g_strcmp0 (editor_anim_view_get_model (self->view), abs) != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s has no skinned mesh to preview", abs);
		return FALSE;
	}
	return ok;
}

const gchar *
lrg_editor_anim_graph_editor_get_preview_model (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return editor_anim_view_get_model (self->view);
}

void
lrg_editor_anim_graph_editor_set_playing (LrgEditorAnimGraphEditor *self,
                                          gboolean                  playing)
{
	g_return_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self));
	self->playing = playing;
	editor_anim_view_set_playing (self->view, playing);
}

gboolean
lrg_editor_anim_graph_editor_get_playing (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	return self->playing;
}

void
lrg_editor_anim_graph_editor_restart (LrgEditorAnimGraphEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self));
	load_graph (self);
	editor_anim_view_pose_now (self->view, 0.0f);
	g_clear_pointer (&self->hl_sig, g_free);
}

void
lrg_editor_anim_graph_editor_advance (LrgEditorAnimGraphEditor *self,
                                      gfloat                    seconds)
{
	g_return_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self));
	if (self->graph == NULL || seconds <= 0.0f)
		return;
	/* in steps of at most 1/30 s, like frames */
	while (seconds > 0.0f)
	{
		gfloat dt = MIN (seconds, 1.0f / 30.0f);

		lrg_anim_graph_update (self->graph, dt);
		self->ptime += dt;
		seconds -= dt;
	}
	editor_anim_view_pose_now (self->view, 0.0f);
}

void
lrg_editor_anim_graph_editor_scrub (LrgEditorAnimGraphEditor *self,
                                    gfloat                    normalized)
{
	LrgAnimationState *st;

	g_return_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self));
	lrg_editor_anim_graph_editor_set_playing (self, FALSE);
	st = base_state (self);
	if (st != NULL)
		lrg_animation_state_set_time (st, CLAMP (normalized, 0.0f, 1.0f) * lrg_animation_state_get_duration (st));
	self->syncing = TRUE;
	lrg_slider_set_value (self->scrub, CLAMP (normalized, 0.0f, 1.0f));
	self->syncing = FALSE;
	editor_anim_view_pose_now (self->view, 0.0f);
}

gfloat
lrg_editor_anim_graph_editor_get_preview_time (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), 0.0f);
	return self->ptime;
}

const gchar *
lrg_editor_anim_graph_editor_get_active_state (LrgEditorAnimGraphEditor *self,
                                               const gchar              *layer)
{
	LrgAnimationLayer        *l;
	LrgAnimationStateMachine *m;

	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	if (self->graph == NULL)
		return NULL;
	if (layer == NULL)
	{
		m = runtime_machine (self, self->machine);
		return m != NULL ? lrg_animation_state_machine_get_current_state_name (m) : NULL;
	}
	l = lrg_anim_graph_get_layer_by_name (self->graph, layer);
	m = l != NULL ? lrg_animation_layer_get_state_machine (l) : NULL;
	return m != NULL ? lrg_animation_state_machine_get_current_state_name (m) : NULL;
}

LrgSkeleton *
lrg_editor_anim_graph_editor_get_preview_skeleton (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return editor_anim_view_get_skeleton (self->view);
}

/* ==========================================================================
 * IPC: anim.*
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

/* A number member (any JSON number); FALSE when absent or not a number. */
static gboolean
param_number (JsonObject  *params,
              const gchar *name,
              gdouble     *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) == G_TYPE_STRING ||
	    json_node_get_value_type (n) == G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_double (n);
	return TRUE;
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

static gboolean
param_is_null (JsonObject  *params,
               const gchar *name)
{
	return params != NULL && json_object_has_member (params, name) &&
	       JSON_NODE_HOLDS_NULL (json_object_get_member (params, name));
}

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (cause != NULL && g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static void
fail_msg (GError      **error,
          gint          code,
          const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, message);
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

/* The editor of params.path (opened when needed), on params.machine. */
static LrgEditorAnimGraphEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar              *path = require_string (params, "path", error);
	const gchar              *machine = param_string (params, "machine");
	LrgEditorAnimGraphEditor *self;
	g_autoptr(GError)         err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_anim_graph_editor_open (app, path, &err);
	if (self == NULL)
	{
		fail (error, err);
		return NULL;
	}
	if (machine != NULL && g_strcmp0 (machine, self->machine) != 0)
	{
		if (!g_str_has_prefix (machine, "/layers/") ||
		    lrg_editor_document_get_kind (doc_of (self), machine) != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		{
			fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such machine path");
			return NULL;
		}
		show_machine (self, machine);
	}
	return self;
}

/* A JSON scalar as YAML text ("0.5", "true", "walk"); NULL for others. */
static gchar *
json_to_yaml (JsonNode *n)
{
	if (n == NULL || JSON_NODE_HOLDS_NULL (n))
		return g_strdup ("null");
	if (JSON_NODE_HOLDS_ARRAY (n))
	{
		JsonArray *a = json_node_get_array (n);
		GString   *s = g_string_new ("[");
		guint      i;

		for (i = 0; i < json_array_get_length (a); i++)
		{
			g_autofree gchar *item = json_to_yaml (json_array_get_element (a, i));

			g_string_append_printf (s, "%s%s", i > 0 ? ", " : "", item != NULL ? item : "null");
		}
		g_string_append_c (s, ']');
		return g_string_free (s, FALSE);
	}
	if (!JSON_NODE_HOLDS_VALUE (n))
		return NULL;
	if (json_node_get_value_type (n) == G_TYPE_BOOLEAN)
		return g_strdup (json_node_get_boolean (n) ? "true" : "false");
	if (json_node_get_value_type (n) == G_TYPE_STRING)
		return yaml_str (json_node_get_string (n));
	return num_str (json_node_get_double (n));
}

/* Sets or removes @parent/@key from a JSON member of @params (absent: kept). */
static gboolean
apply_member (LrgEditorAnimGraphEditor  *self,
              JsonObject                *params,
              const gchar               *member,
              const gchar               *parent,
              const gchar               *key,
              GError                   **error)
{
	g_autofree gchar *p = pj (parent, key);
	g_autofree gchar *text = NULL;

	if (params == NULL || !json_object_has_member (params, member))
		return TRUE;
	if (param_is_null (params, member))
		return !lrg_editor_document_has_path (doc_of (self), p) || lrg_editor_document_remove (doc_of (self), p, error);
	text = json_to_yaml (json_object_get_member (params, member));
	if (text == NULL)
		return fail_arg (error, "Bad value for '%s'", member);
	if (lrg_editor_document_has_path (doc_of (self), p))
		return lrg_editor_document_replace (doc_of (self), p, text, error);
	return lrg_editor_document_add_key (doc_of (self), parent, key, text, -1, error);
}

/* A conditions array as a YAML flow list. */
static gchar *
conditions_yaml (JsonArray  *a,
                 GError    **error)
{
	GString *s = g_string_new ("[");
	guint    i;

	for (i = 0; i < json_array_get_length (a); i++)
	{
		JsonObject       *c = json_array_get_object_element (a, i);
		const gchar      *param = c != NULL ? param_string (c, "parameter") : NULL;
		const gchar      *op = c != NULL ? param_string (c, "op") : NULL;
		g_autofree gchar *yp = NULL;

		if (param == NULL)
		{
			g_string_free (s, TRUE);
			fail_arg (error, "conditions[%s] needs a parameter", "?");
			return NULL;
		}
		yp = yaml_str (param);
		if (op != NULL)
		{
			g_autofree gchar *v = json_object_has_member (c, "value")
			                      ? json_to_yaml (json_object_get_member (c, "value")) : g_strdup ("0");

			g_string_append_printf (s, "%s{parameter: %s, op: '%s', value: %s}", i > 0 ? ", " : "", yp, op, v);
		}
		else
			g_string_append_printf (s, "%s{parameter: %s}", i > 0 ? ", " : "", yp);
	}
	g_string_append_c (s, ']');
	return g_string_free (s, FALSE);
}

static JsonObject *
transition_json (LrgEditorAnimGraphEditor *self,
                 const gchar              *tp,
                 gboolean                  any)
{
	JsonObject       *o = json_object_new ();
	JsonArray        *conds = json_array_new ();
	g_autofree gchar *cl = pj (tp, "conditions");
	const gchar      *v;
	gint              i, n = n_items (self, cl);

	json_object_set_string_member (o, "from", any ? ANY_ID : scalar_at (self, tp, "from"));
	json_object_set_string_member (o, "to", scalar_at (self, tp, "to"));
	v = scalar_at (self, tp, "duration");
	json_object_set_double_member (o, "duration", v != NULL ? g_ascii_strtod (v, NULL) : 0.25);
	if ((v = scalar_at (self, tp, "exit_time")) != NULL)
		json_object_set_double_member (o, "exit_time", g_ascii_strtod (v, NULL));
	v = scalar_at (self, tp, "offset");
	json_object_set_double_member (o, "offset", v != NULL ? g_ascii_strtod (v, NULL) : 0.0);
	v = scalar_at (self, tp, "priority");
	json_object_set_int_member (o, "priority", v != NULL ? g_ascii_strtoll (v, NULL, 10) : 0);
	v = scalar_at (self, tp, "interruption");
	json_object_set_string_member (o, "interruption", v != NULL ? v : "none");
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *cp = pji (cl, (guint) i);
		JsonObject       *c = json_object_new ();
		g_auto(GValue)    gv = G_VALUE_INIT;
		g_autofree gchar *vp = pj (cp, "value");

		json_object_set_string_member (c, "parameter", scalar_at (self, cp, "parameter"));
		if (scalar_at (self, cp, "op") != NULL)
			json_object_set_string_member (c, "op", scalar_at (self, cp, "op"));
		if (lrg_editor_document_get_value (doc_of (self), vp, &gv))
		{
			if (G_VALUE_HOLDS_BOOLEAN (&gv))
				json_object_set_boolean_member (c, "value", g_value_get_boolean (&gv));
			else if (G_VALUE_HOLDS_INT64 (&gv))
				json_object_set_int_member (c, "value", g_value_get_int64 (&gv));
			else if (G_VALUE_HOLDS_DOUBLE (&gv))
				json_object_set_double_member (c, "value", g_value_get_double (&gv));
		}
		json_array_add_object_element (conds, c);
	}
	json_object_set_array_member (o, "conditions", conds);
	return o;
}

static JsonObject *
preview_json (LrgEditorAnimGraphEditor *self)
{
	JsonObject *o = json_object_new ();
	JsonObject *states = json_object_new ();
	guint       i, n = self->graph != NULL ? lrg_anim_graph_get_layer_count (self->graph) : 0;
	JsonNode   *transition = NULL;

	json_object_set_string_member (o, "model", lrg_editor_anim_graph_editor_get_preview_model (self));
	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_double_member (o, "time", self->ptime);
	for (i = 0; i < n; i++)
	{
		LrgAnimationLayer        *layer = lrg_anim_graph_get_layer (self->graph, i);
		LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (layer);
		LrgAnimationTransition   *tr = m != NULL ? lrg_animation_state_machine_get_active_transition (m) : NULL;

		json_object_set_string_member (states, lrg_animation_layer_get_name (layer),
		                               m != NULL ? lrg_animation_state_machine_get_current_state_name (m) : NULL);
		if (i == 0 && tr != NULL)
		{
			JsonObject *t = json_object_new ();

			json_object_set_string_member (t, "from", lrg_animation_transition_is_any_state (tr)
			                                          ? ANY_ID : lrg_animation_transition_get_source (tr));
			json_object_set_string_member (t, "to", lrg_animation_transition_get_target (tr));
			json_object_set_double_member (t, "progress", lrg_animation_state_machine_get_transition_progress (m));
			transition = object_node (t);
		}
	}
	json_object_set_object_member (o, "states", states);
	if (transition != NULL)
		json_object_set_member (o, "transition", transition);
	else
		json_object_set_null_member (o, "transition");
	return o;
}

static JsonNode *
m_anim_open (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject               *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_anim_create (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = require_string (params, "path", error);
	g_autoptr(GError)  err = NULL;
	g_autofree gchar  *abs = NULL;
	JsonObject        *o;

	if (path == NULL)
		return NULL;
	if (!lrg_editor_anim_graph_editor_create (app, path, param_string (params, "model"), &err))
	{
		fail (error, err);
		return NULL;
	}
	abs = g_path_is_absolute (path) ? g_strdup (path) : editor_app_project_path (app, path);
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	return object_node (o);
}

static JsonNode *
m_anim_get (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject               *o;
	JsonArray                *a;
	g_auto(GStrv)             crumbs = NULL;
	g_auto(GStrv)             keys = NULL;
	g_autofree gchar         *states = NULL;
	g_autofree gchar         *trans = NULL;
	g_autofree gchar         *any = NULL;
	const gchar              *def;
	const gchar              *active;
	guint                     i;
	gint                      k;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "name", scalar_at (self, "", "name"));
	json_object_set_string_member (o, "machine", self->machine);
	if (self->load_error != NULL)
		json_object_set_string_member (o, "load_error", self->load_error);
	crumbs = lrg_editor_anim_graph_editor_get_breadcrumbs (self);
	a = json_array_new ();
	for (i = 0; crumbs[i] != NULL; i++)
		json_array_add_string_element (a, crumbs[i]);
	json_object_set_array_member (o, "breadcrumbs", a);

	/* layers */
	a = json_array_new ();
	for (i = 0; i < n_layers (self); i++)
	{
		g_autofree gchar *lp = pji ("/layers", i);
		JsonObject       *l = json_object_new ();
		const gchar      *v;

		json_object_set_string_member (l, "name", scalar_at (self, lp, "name"));
		v = scalar_at (self, lp, "weight");
		json_object_set_double_member (l, "weight", v != NULL ? g_ascii_strtod (v, NULL) : 1.0);
		v = scalar_at (self, lp, "blend");
		json_object_set_string_member (l, "blend", v != NULL ? v : "override");
		v = scalar_at (self, lp, "enabled");
		json_object_set_boolean_member (l, "enabled", g_strcmp0 (v, "false") != 0);
		json_array_add_object_element (a, l);
	}
	json_object_set_array_member (o, "layers", a);

	/* the shown machine */
	states = pj (self->machine, "states");
	def = default_state (self, self->machine);
	active = lrg_editor_anim_graph_editor_get_active_state (self, NULL);
	a = json_array_new ();
	for (k = 0; k < n_items (self, states); k++)
	{
		g_autofree gchar *sp = pji (states, (guint) k);
		JsonObject       *s = json_object_new ();
		const gchar      *name = scalar_at (self, sp, "name");
		gfloat            x = (gfloat) ((k % GRID_COLS) * GRID_DX), y = (gfloat) ((k / GRID_COLS) * GRID_DY);

		read_pos (self, sp, "editor_pos", &x, &y);
		json_object_set_string_member (s, "name", name);
		json_object_set_string_member (s, "kind", state_kind (self, sp));
		if (scalar_at (self, sp, "clip") != NULL)
			json_object_set_string_member (s, "clip", scalar_at (self, sp, "clip"));
		json_object_set_boolean_member (s, "default", g_strcmp0 (name, def) == 0);
		json_object_set_double_member (s, "x", x);
		json_object_set_double_member (s, "y", y);
		json_object_set_boolean_member (s, "active", g_strcmp0 (name, active) == 0);
		json_array_add_object_element (a, s);
	}
	json_object_set_array_member (o, "states", a);
	trans = pj (self->machine, "transitions");
	any = pj (self->machine, "any_state");
	a = json_array_new ();
	for (k = 0; k < n_items (self, any); k++)
	{
		g_autofree gchar *tp = pji (any, (guint) k);

		json_array_add_object_element (a, transition_json (self, tp, TRUE));
	}
	for (k = 0; k < n_items (self, trans); k++)
	{
		g_autofree gchar *tp = pji (trans, (guint) k);

		json_array_add_object_element (a, transition_json (self, tp, FALSE));
	}
	json_object_set_array_member (o, "transitions", a);

	/* parameters with live values */
	a = json_array_new ();
	for (k = 0; k < n_items (self, "/parameters"); k++)
	{
		g_autofree gchar   *pp = pji ("/parameters", (guint) k);
		JsonObject         *p = json_object_new ();
		const gchar        *name = scalar_at (self, pp, "name");
		g_autoptr(GVariant) v = name != NULL ? lrg_editor_anim_graph_editor_get_parameter_value (self, name) : NULL;
		g_autofree gchar   *dp = pj (pp, "default");
		g_auto(GValue)      dv = G_VALUE_INIT;

		json_object_set_string_member (p, "name", name);
		json_object_set_string_member (p, "type", scalar_at (self, pp, "type"));
		if (lrg_editor_document_get_value (doc_of (self), dp, &dv))
		{
			if (G_VALUE_HOLDS_BOOLEAN (&dv))
				json_object_set_boolean_member (p, "default", g_value_get_boolean (&dv));
			else if (G_VALUE_HOLDS_INT64 (&dv))
				json_object_set_int_member (p, "default", g_value_get_int64 (&dv));
			else if (G_VALUE_HOLDS_DOUBLE (&dv))
				json_object_set_double_member (p, "default", g_value_get_double (&dv));
		}
		if (v == NULL)
			json_object_set_null_member (p, "value");
		else if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
			json_object_set_double_member (p, "value", g_variant_get_double (v));
		else if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
			json_object_set_int_member (p, "value", g_variant_get_int32 (v));
		else
			json_object_set_boolean_member (p, "value", g_variant_get_boolean (v));
		json_array_add_object_element (a, p);
	}
	json_object_set_array_member (o, "parameters", a);

	/* clip slots */
	keys = clip_keys (self);
	a = json_array_new ();
	for (i = 0; keys[i] != NULL; i++)
	{
		g_autofree gchar *cp = pj ("/clips", keys[i]);
		JsonObject       *c = json_object_new ();

		json_object_set_string_member (c, "name", keys[i]);
		json_object_set_string_member (c, "source", scalar_at (self, cp, "source"));
		json_object_set_string_member (c, "animation", scalar_at (self, cp, "animation"));
		json_object_set_boolean_member (c, "missing", self->graph == NULL ||
		                                              lrg_anim_graph_get_clip (self->graph, keys[i]) == NULL);
		json_array_add_object_element (a, c);
	}
	json_object_set_array_member (o, "clips", a);
	json_object_set_object_member (o, "preview", preview_json (self));
	return object_node (o);
}

static JsonNode *
m_anim_enter (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *layer = param_string (params, "layer");
	const gchar              *state = param_string (params, "state");
	gboolean                  up = FALSE;
	g_auto(GStrv)             crumbs = NULL;
	JsonObject               *o;
	JsonArray                *a;
	guint                     i;

	if (self == NULL)
		return NULL;
	if (layer != NULL)
	{
		gboolean found = FALSE;

		for (i = 0; i < n_layers (self) && !found; i++)
		{
			g_autofree gchar *lp = pji ("/layers", i);

			if (g_strcmp0 (scalar_at (self, lp, "name"), layer) == 0)
				found = lrg_editor_anim_graph_editor_set_layer (self, i);
		}
		if (!found)
		{
			fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such layer");
			return NULL;
		}
	}
	if (state != NULL && !lrg_editor_anim_graph_editor_enter_state (self, state))
	{
		fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No state with a nested machine by that name");
		return NULL;
	}
	if (param_bool (params, "up", &up) && up && !lrg_editor_anim_graph_editor_go_up (self))
	{
		fail_msg (error, LRG_EDITOR_SERVER_ERROR_FAILED, "Already at the layer's top machine");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "machine", self->machine);
	crumbs = lrg_editor_anim_graph_editor_get_breadcrumbs (self);
	a = json_array_new ();
	for (i = 0; crumbs[i] != NULL; i++)
		json_array_add_string_element (a, crumbs[i]);
	json_object_set_array_member (o, "breadcrumbs", a);
	return object_node (o);
}

static JsonNode *
name_result (const gchar *member,
             const gchar *value)
{
	JsonObject *o = json_object_new ();

	json_object_set_string_member (o, member, value);
	return object_node (o);
}

static JsonNode *
removed_result (gboolean removed)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, "removed", removed);
	return object_node (o);
}

static JsonNode *
m_anim_add_layer (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name;
	g_autoptr(GError)         err = NULL;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	if (!lrg_editor_anim_graph_editor_add_layer (self, name, &err))
	{
		fail (error, err);
		return NULL;
	}
	return name_result ("name", name);
}

static JsonNode *
m_anim_remove_layer (LrgEditorServer  *server,
                     JsonObject       *params,
                     gpointer          user_data,
                     GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	return removed_result (lrg_editor_anim_graph_editor_remove_layer (self, name));
}

static JsonNode *
m_anim_add_state (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name;
	g_autoptr(GError)         err = NULL;
	gdouble                   x = 0.0, y = 0.0;
	g_autofree gchar         *states = NULL;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	states = pj (self->machine, "states");
	if (!param_number (params, "x", &x))
		x = (gdouble) ((n_items (self, states) % GRID_COLS) * GRID_DX);
	if (!param_number (params, "y", &y))
		y = (gdouble) ((n_items (self, states) / GRID_COLS) * GRID_DY);
	if (!lrg_editor_anim_graph_editor_add_state (self, name, param_string (params, "kind"), param_string (params, "clip"),
	                                             (gfloat) x, (gfloat) y, &err))
	{
		fail (error, err);
		return NULL;
	}
	return name_result ("name", name);
}

static JsonNode *
m_anim_remove_state (LrgEditorServer  *server,
                     JsonObject       *params,
                     gpointer          user_data,
                     GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	return removed_result (lrg_editor_anim_graph_editor_remove_state (self, name));
}

static JsonNode *
m_anim_set_state (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name;
	const gchar              *rename;
	g_autofree gchar         *sp = NULL;
	g_autoptr(GError)         err = NULL;
	gboolean                  flag = FALSE;
	gboolean                  ok = TRUE;
	gdouble                   x, y;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	sp = state_path_in (self, self->machine, name);
	if (sp == NULL)
	{
		fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such state in the machine");
		return NULL;
	}
	if (param_string (params, "clip") != NULL)
	{
		g_autofree gchar *cp = pj ("/clips", param_string (params, "clip"));

		if (!lrg_editor_document_has_path (doc_of (self), cp))
		{
			fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "No such clip slot");
			return NULL;
		}
		convert_state (self, sp, "clip");
	}
	edit_begin (self, "Edit State");
	ok = apply_member (self, params, "clip", sp, "clip", &err) &&
	     apply_member (self, params, "speed", sp, "speed", &err) &&
	     apply_member (self, params, "speed_parameter", sp, "speed_parameter", &err) &&
	     apply_member (self, params, "mirror", sp, "mirror", &err);
	if (ok && param_number (params, "x", &x) && param_number (params, "y", &y))
		ok = write_pos (self, sp, "editor_pos", (gfloat) x, (gfloat) y);
	if (ok && param_bool (params, "default", &flag) && flag)
	{
		g_autofree gchar *dp = pj (self->machine, "default");

		ok = lrg_editor_document_set_string (doc_of (self), dp, name, 0, &err);
	}
	edit_end (self);
	if (ok && (rename = param_string (params, "rename")) != NULL)
	{
		ok = lrg_editor_anim_graph_editor_rename_state (self, name, rename, &err);
		name = rename;
	}
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	return name_result ("name", name);
}

/* Applies the optional transition members of @params to @tp. */
static gboolean
apply_transition (LrgEditorAnimGraphEditor  *self,
                  JsonObject                *params,
                  const gchar               *tp,
                  GError                   **error)
{
	gboolean ok;

	ok = apply_member (self, params, "duration", tp, "duration", error) &&
	     apply_member (self, params, "exit_time", tp, "exit_time", error) &&
	     apply_member (self, params, "offset", tp, "offset", error) &&
	     apply_member (self, params, "priority", tp, "priority", error) &&
	     apply_member (self, params, "interruption", tp, "interruption", error) &&
	     apply_member (self, params, "can_transition_to_self", tp, "can_transition_to_self", error);
	if (ok && json_object_has_member (params, "conditions"))
	{
		JsonNode         *n = json_object_get_member (params, "conditions");
		g_autofree gchar *cl = pj (tp, "conditions");
		g_autofree gchar *text = NULL;

		if (!JSON_NODE_HOLDS_ARRAY (n))
			return fail_arg (error, "'%s' must be an array", "conditions");
		text = conditions_yaml (json_node_get_array (n), error);
		if (text == NULL)
			return FALSE;
		ok = lrg_editor_document_has_path (doc_of (self), cl)
		     ? lrg_editor_document_replace (doc_of (self), cl, text, error)
		     : lrg_editor_document_add_key (doc_of (self), tp, "conditions", text, -1, error);
	}
	return ok;
}

static JsonNode *
m_anim_add_transition (LrgEditorServer  *server,
                       JsonObject       *params,
                       gpointer          user_data,
                       GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *from, *to;
	g_autoptr(GError)         err = NULL;
	g_autofree gchar         *tp = NULL;
	gdouble                   duration = 0.25;
	JsonObject               *o;
	gboolean                  ok;

	if (self == NULL || (from = require_string (params, "from", error)) == NULL ||
	    (to = require_string (params, "to", error)) == NULL)
		return NULL;
	param_number (params, "duration", &duration);
	edit_begin (self, "Add Transition");
	ok = lrg_editor_anim_graph_editor_add_transition (self, from, to, (gfloat) duration, &err);
	if (ok)
	{
		tp = transition_path_in (self, self->machine, from, to);
		ok = apply_transition (self, params, tp, &err);
	}
	edit_end (self);
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "from", from);
	json_object_set_string_member (o, "to", to);
	return object_node (o);
}

static JsonNode *
m_anim_set_transition (LrgEditorServer  *server,
                       JsonObject       *params,
                       gpointer          user_data,
                       GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *from, *to;
	g_autoptr(GError)         err = NULL;
	g_autofree gchar         *tp = NULL;
	JsonObject               *o;
	gboolean                  ok;

	if (self == NULL || (from = require_string (params, "from", error)) == NULL ||
	    (to = require_string (params, "to", error)) == NULL)
		return NULL;
	tp = transition_path_in (self, self->machine, from, to);
	if (tp == NULL)
	{
		fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such transition in the machine");
		return NULL;
	}
	edit_begin (self, "Edit Transition");
	ok = apply_transition (self, params, tp, &err);
	edit_end (self);
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "from", from);
	json_object_set_string_member (o, "to", to);
	return object_node (o);
}

static JsonNode *
m_anim_remove_transition (LrgEditorServer  *server,
                          JsonObject       *params,
                          gpointer          user_data,
                          GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *from, *to;

	if (self == NULL || (from = require_string (params, "from", error)) == NULL ||
	    (to = require_string (params, "to", error)) == NULL)
		return NULL;
	return removed_result (lrg_editor_anim_graph_editor_remove_transition (self, from, to));
}

static JsonNode *
m_anim_add_parameter (LrgEditorServer  *server,
                      JsonObject       *params,
                      gpointer          user_data,
                      GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name, *type;
	g_autoptr(GError)         err = NULL;
	gboolean                  ok;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL ||
	    (type = require_string (params, "type", error)) == NULL)
		return NULL;
	edit_begin (self, "Add Parameter");
	ok = lrg_editor_anim_graph_editor_add_parameter (self, name, type, &err);
	if (ok && json_object_has_member (params, "default"))
	{
		gint              n = n_items (self, "/parameters");
		g_autofree gchar *pp = pji ("/parameters", (guint) (n - 1));

		ok = apply_member (self, params, "default", pp, "default", &err);
	}
	edit_end (self);
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	return name_result ("name", name);
}

static JsonNode *
m_anim_remove_parameter (LrgEditorServer  *server,
                         JsonObject       *params,
                         gpointer          user_data,
                         GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	return removed_result (lrg_editor_anim_graph_editor_remove_parameter (self, name));
}

static JsonNode *
m_anim_set_parameter (LrgEditorServer  *server,
                      JsonObject       *params,
                      gpointer          user_data,
                      GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *name;
	JsonNode                 *v;
	GVariant                 *value = NULL;
	g_autoptr(GVariant)       now = NULL;
	JsonObject               *o;

	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	v = json_object_has_member (params, "value") ? json_object_get_member (params, "value") : NULL;
	if (v != NULL && JSON_NODE_HOLDS_VALUE (v) && json_node_get_value_type (v) == G_TYPE_BOOLEAN)
		value = g_variant_new_boolean (json_node_get_boolean (v));
	else if (v != NULL && JSON_NODE_HOLDS_VALUE (v) && json_node_get_value_type (v) != G_TYPE_STRING)
		value = g_variant_new_double (json_node_get_double (v));
	/* set_parameter_value() sinks and drops the floating value */
	if (value == NULL || !lrg_editor_anim_graph_editor_set_parameter_value (self, name, value))
	{
		fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Unknown parameter or a value of the wrong kind");
		return NULL;
	}
	now = lrg_editor_anim_graph_editor_get_parameter_value (self, name);
	o = json_object_new ();
	json_object_set_string_member (o, "name", name);
	if (g_variant_is_of_type (now, G_VARIANT_TYPE_DOUBLE))
		json_object_set_double_member (o, "value", g_variant_get_double (now));
	else if (g_variant_is_of_type (now, G_VARIANT_TYPE_INT32))
		json_object_set_int_member (o, "value", g_variant_get_int32 (now));
	else
		json_object_set_boolean_member (o, "value", g_variant_get_boolean (now));
	return object_node (o);
}

static JsonNode *
m_anim_set_blend_tree (LrgEditorServer  *server,
                       JsonObject       *params,
                       gpointer          user_data,
                       GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *state;
	g_autofree gchar         *sp = NULL;
	g_autofree gchar         *bt = NULL;
	g_autoptr(GError)         err = NULL;
	gboolean                  ok;

	if (self == NULL || (state = require_string (params, "state", error)) == NULL)
		return NULL;
	sp = state_path_in (self, self->machine, state);
	if (sp == NULL)
	{
		fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such state in the machine");
		return NULL;
	}
	edit_begin (self, "Edit Blend Tree");
	convert_state (self, sp, "blend_tree");
	bt = pj (sp, "blend_tree");
	ok = apply_member (self, params, "type", bt, "type", &err) &&
	     apply_member (self, params, "parameter", bt, "parameter", &err) &&
	     apply_member (self, params, "parameter_y", bt, "parameter_y", &err) &&
	     apply_member (self, params, "sync", bt, "sync", &err);
	if (ok && json_object_has_member (params, "children"))
	{
		JsonNode         *n = json_object_get_member (params, "children");
		g_autofree gchar *ch = pj (bt, "children");
		GString          *s = g_string_new ("[");
		static const gchar * const keys[] = { "clip", "threshold", "position", "parameter", "weight", "speed" };
		guint             i, k;

		if (!JSON_NODE_HOLDS_ARRAY (n))
			ok = fail_arg (&err, "'%s' must be an array", "children");
		for (i = 0; ok && i < json_array_get_length (json_node_get_array (n)); i++)
		{
			JsonObject *c = json_array_get_object_element (json_node_get_array (n), i);
			guint       written = 0;

			g_string_append_printf (s, "%s{", i > 0 ? ", " : "");
			for (k = 0; c != NULL && k < G_N_ELEMENTS (keys); k++)
			{
				g_autofree gchar *v = json_object_has_member (c, keys[k])
				                      ? json_to_yaml (json_object_get_member (c, keys[k])) : NULL;

				if (v != NULL)
					g_string_append_printf (s, "%s%s: %s", written++ > 0 ? ", " : "", keys[k], v);
			}
			g_string_append_c (s, '}');
		}
		g_string_append_c (s, ']');
		if (ok)
			ok = lrg_editor_document_has_path (doc_of (self), ch)
			     ? lrg_editor_document_replace (doc_of (self), ch, s->str, &err)
			     : lrg_editor_document_add_key (doc_of (self), bt, "children", s->str, -1, &err);
		g_string_free (s, TRUE);
	}
	edit_end (self);
	if (!ok)
	{
		fail (error, err);
		return NULL;
	}
	return name_result ("state", state);
}

static JsonNode *
m_anim_preview (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar              *model;
	g_autoptr(GError)         err = NULL;
	gboolean                  flag = FALSE;
	gdouble                   v;

	if (self == NULL)
		return NULL;
	if ((model = param_string (params, "model")) != NULL &&
	    !lrg_editor_anim_graph_editor_set_preview_model (self, model, &err))
	{
		fail (error, err);
		return NULL;
	}
	if (param_bool (params, "restart", &flag) && flag)
		lrg_editor_anim_graph_editor_restart (self);
	if (param_bool (params, "play", &flag))
		lrg_editor_anim_graph_editor_set_playing (self, flag);
	if (param_number (params, "advance", &v))
		lrg_editor_anim_graph_editor_advance (self, (gfloat) v);
	if (param_number (params, "scrub", &v))
		lrg_editor_anim_graph_editor_scrub (self, (gfloat) v);
	return object_node (preview_json (self));
}

static JsonNode *
m_anim_validate (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	GPtrArray                *issues;
	JsonObject               *o;
	JsonArray                *a;
	guint                     errors, i;

	if (self == NULL)
		return NULL;
	errors = lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (self));
	issues = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (self));
	a = json_array_new ();
	for (i = 0; issues != NULL && i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);
		JsonObject             *io = json_object_new ();

		json_object_set_string_member (io, "severity", issue->severity == LRG_EDITOR_PROBLEM_ERROR ? "error"
		                                               : issue->severity == LRG_EDITOR_PROBLEM_WARNING ? "warning"
		                                                                                               : "info");
		json_object_set_string_member (io, "path", issue->path);
		json_object_set_int_member (io, "line", issue->line);
		json_object_set_string_member (io, "message", issue->message);
		json_array_add_object_element (a, io);
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "ok", errors == 0);
	json_object_set_array_member (o, "issues", a);
	return object_node (o);
}

static JsonNode *
m_anim_save (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorAnimGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)         err = NULL;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &err))
	{
		fail (error, err);
		return NULL;
	}
	return name_result ("path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
}

void
lrg_editor_anim_graph_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "anim.open", m_anim_open },
		{ "anim.create", m_anim_create },
		{ "anim.get", m_anim_get },
		{ "anim.enter", m_anim_enter },
		{ "anim.add_layer", m_anim_add_layer },
		{ "anim.remove_layer", m_anim_remove_layer },
		{ "anim.add_state", m_anim_add_state },
		{ "anim.remove_state", m_anim_remove_state },
		{ "anim.set_state", m_anim_set_state },
		{ "anim.add_transition", m_anim_add_transition },
		{ "anim.set_transition", m_anim_set_transition },
		{ "anim.remove_transition", m_anim_remove_transition },
		{ "anim.add_parameter", m_anim_add_parameter },
		{ "anim.remove_parameter", m_anim_remove_parameter },
		{ "anim.set_parameter", m_anim_set_parameter },
		{ "anim.set_blend_tree", m_anim_set_blend_tree },
		{ "anim.preview", m_anim_preview },
		{ "anim.validate", m_anim_validate },
		{ "anim.save", m_anim_save }
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

void
lrg_editor_anim_graph_editor_set_show_bones (LrgEditorAnimGraphEditor *self,
                                             gboolean                  show)
{
	g_return_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self));
	editor_anim_view_set_show_bones (self->view, show);
}

gboolean
lrg_editor_anim_graph_editor_get_show_bones (LrgEditorAnimGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), FALSE);
	return editor_anim_view_get_show_bones (self->view);
}

GrlImage *
lrg_editor_anim_graph_editor_render_preview (LrgEditorAnimGraphEditor *self,
                                             guint                     size)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ANIM_GRAPH_EDITOR (self), NULL);
	return editor_anim_view_render (self->view, size);
}
