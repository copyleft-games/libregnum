/* lrg-editor-viewport-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One editor scene viewport on the retained UI toolkit.
 *
 * Children, back to front:
 *
 *   header     LrgHBox: view preset and render mode combo boxes, the
 *              overlays menu button, camera preview and maximise toggles
 *   viewport   LrgViewport in render mode; its render function hands the
 *              target to LrgEditorSceneRenderer for camera @index
 *   overlay    a pointer-transparent LrgCustomWidget painting the 2D
 *              overlays with LrgPaintList (lrg-editor-gizmo-paint.c)
 *   preview    the camera preview, a second LrgViewport (picture in
 *              picture) rendering through the selected camera node
 *
 * Rendering is on demand: signals from the level, selection, view state,
 * play session and renderer mark the panel dirty, and the panel's step
 * (which runs before its children's, so before the viewport's) queues
 * one render. While playing, navigating or dragging it renders every
 * frame. Overlays live in the UI paint pass, so a gizmo hover or a box
 * select does not touch the scene image.
 *
 * Input routing, in the order docs/editor/view-model.org prescribes:
 *
 *   press    make this the active viewport ->
 *            tile painting (paint mode on a tilemap) ->
 *            LrgEditorViewportNav (fly / orbit / pan / zoom / axis gizmo) ->
 *            the transform gizmo (left button) ->
 *            lrg_editor_viewport_nav_press_empty() (arms click / box select)
 *   move     navigation, then the tile stroke or the gizmo drag, else the
 *            gizmo hover highlight
 *   release  ends the tile stroke / gizmo drag, then navigation, which
 *            emits ::click (pick), ::box-select or ::context-menu
 *   keys     Escape cancels a gizmo drag or leaves tile paint mode; the
 *            rest go to the navigator (W A S D Q E while flying), which
 *            returns TRUE only for keys it owns so shortcuts still fire;
 *            key releases arrive through the panel's handle_event (they
 *            bubble from the viewport) and focus loss releases held keys.
 *
 * The navigator is shared by every panel and acts on the view state's
 * active camera; only the panel that took the press ("nav owner") reacts
 * to its gesture signals, so a split layout picks and box-selects once.
 *
 * Animation overlays (0.9.0): with "Bones" on (the overlays menu or
 * lrg_editor_viewport_panel_set_show_bones()) the selected nodes' skeletons
 * are drawn as joint dots and bone lines: the live, animated skeleton of
 * the play world's entity while playing (its LrgAnimatorComponent3D's or
 * LrgSkinnedModelComponent's), else the bind pose of the node's glTF
 * model. With "IK Targets" on (the default) every IK goal of a selected
 * animator draws its last solved target as a ring with its name and a
 * line from the effector bone. Both are painted in the 2D overlay pass.
 *
 * Everything works headless: LrgViewport maps pointer positions with the
 * size its texture is heading for, so tests drive the viewport through
 * lrg_canvas_process_input() without a GL context.
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "lrg-editor-viewport-split.h"
#include "lrg-editor-gizmo-paint.h"
#include "../lrg-asset-database.h"
#include "../../ui/lrg-menu.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-drop-target.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../tween/lrg-spring.h"
#include "../../graphics/lrg-camera.h"
#include "data/lrg-editor-material-editor.h"
#include "data/lrg-editor-post-stack-editor.h"
#include "../../animation/lrg-gltf-animation.h"
#include "../../animation/lrg-anim-graph.h"
#include "../../animation/lrg-bone.h"
#include "../../world3d/lrg-entity3d.h"
#include "../../world3d/components/lrg-animator-component3d.h"
#include "../../world3d/components/lrg-skinned-model-component.h"
#include "../../world3d/lrg-world3d.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include "../../level/lrg-component-desc.h"
#include "lrg-editor-physics-panel.h"
#include <math.h>
#include <string.h>

#define CLICK_SLOP        (4.0f)
#define PREVIEW_FRACTION  (0.30f)
#define PREVIEW_MAX_W     (360.0f)
#define PREVIEW_MIN_W     (120.0f)
#define PREVIEW_MARGIN    (12.0f)
#define PREVIEW_ASPECT    (16.0f / 9.0f)

/* View presets of the header combo box, in item order. */
typedef enum
{
	PRESET_PERSPECTIVE,
	PRESET_ORTHOGRAPHIC,
	PRESET_TOP,
	PRESET_BOTTOM,
	PRESET_FRONT,
	PRESET_BACK,
	PRESET_RIGHT,
	PRESET_LEFT,
	PRESET_2D,
	N_PRESETS
} ViewPreset;

static const gchar * const preset_labels[N_PRESETS] = {
	"Perspective", "Orthographic", "Top", "Bottom", "Front", "Back", "Right", "Left", "2D"
};

/* Render modes of the header combo box, in LrgEditorRenderMode order.
 * The combo lists as many as the enum has (the 0.9.0 debug views
 * included); a mode without a label here shows its enum nick. */
static const gchar * const mode_labels[] = {
	"Lit", "Unlit", "Wireframe", "Normals", "Lighting",
	"Overdraw", "UV", "Light Count", "AO", "Reflections"
};

/* Render layers with an engine name (lrg-camera.h); the rest are
 * "Layer N". */
static const gchar * const layer_names[] = {
	"Default", "UI", "Editor Only", "Ignore Raycast", "Transparent FX", "Water", "Sky"
};

/* Overlay flags of the overlays menu: action suffix (the flag nick) and
 * label. */
static const struct
{
	LrgEditorViewOverlays  flag;
	const gchar           *nick;
	const gchar           *label;
} overlay_items[] = {
	{ LRG_EDITOR_VIEW_OVERLAY_GRID, "grid", "Grid" },
	{ LRG_EDITOR_VIEW_OVERLAY_GIZMOS, "gizmos", "Gizmos" },
	{ LRG_EDITOR_VIEW_OVERLAY_ICONS, "icons", "Icons" },
	{ LRG_EDITOR_VIEW_OVERLAY_BOUNDS, "bounds", "Bounds" },
	{ LRG_EDITOR_VIEW_OVERLAY_STATS, "stats", "Stats" },
	{ LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING, "scene-lighting", "Scene Lighting" },
	{ LRG_EDITOR_VIEW_OVERLAY_SHADOWS, "shadows", "Shadows" },
	{ LRG_EDITOR_VIEW_OVERLAY_SKY, "sky", "Sky" },
	{ LRG_EDITOR_VIEW_OVERLAY_POST, "post", "Post Processing" },
};

/* The header's icon toggles. */
typedef enum
{
	ICON_OVERLAYS,
	ICON_LAYERS,
	ICON_PREVIEW,
	ICON_MAXIMIZE
} IconKind;

struct _LrgEditorViewportPanel
{
	LrgContainer         parent_instance;

	LrgEditorApp        *app;          /* not owned */
	guint                index;
	LrgViewport         *viewport;
	LrgHBox             *header;
	LrgComboBox         *preset_combo;
	LrgComboBox         *mode_combo;
	LrgCustomWidget     *overlay_button;
	LrgCustomWidget     *layers_button;
	LrgMenu             *layers_menu;
	guint32              cull_mask;    /* render layers this viewport shows */
	LrgHBox             *tools;        /* the icon buttons, right aligned */
	LrgCustomWidget     *preview_button;
	LrgCustomWidget     *max_button;
	LrgCustomWidget     *overlay;
	LrgViewport         *preview;
	LrgMenu             *context_menu;
	LrgMenu             *overlay_menu;
	GSimpleActionGroup  *actions;
	LrgDropTarget       *drop;         /* owned by the panel widget */
	LrgLevel            *level;        /* weak: the level whose signals we hold */
	gulong               level_ids[3];

	/* What the panel listens to, held so dispose can disconnect even
	 * after the app went away (the editor may outlive the app) */
	GObject             *sig_editor;
	GObject             *sig_selection;
	GObject             *sig_view;
	GObject             *sig_nav;
	GObject             *sig_play;
	gboolean             syncing;

	LrgInputModifiers    mods;         /* of the last pointer event with modifiers */
	gfloat               pointer_u;
	gfloat               pointer_v;
	gboolean             hovered;
	gboolean             gizmo_drag;   /* the left button drags the transform gizmo */

	/* Volume gizmo (light range / cone, decal and probe boxes) */
	LrgEditorVolumeHandle vol_hot;     /* hovered handle */
	LrgEditorVolumeHandle vol_handle;  /* dragged handle, NONE when idle */
	LrgNode             *vol_node;     /* weak: the node being resized */
	LrgNode             *vol_before;   /* owned: its state before the drag */

	/* Tile painting (paint mode, left button) */
	gboolean             tile_drag;
	gboolean             tile_rect;
	gint                 tile_rect_x0;
	gint                 tile_rect_y0;
	gboolean             tile_erase;

	/* Click cycling through overlapping nodes */
	gfloat               last_pick_x;
	gfloat               last_pick_y;
	gfloat               size_w;
	gfloat               size_h;

	/* Render on demand */
	gboolean             dirty;
	guint                seen_generation;
	gboolean             gpu_enabled;

	/* Camera preview */
	gboolean             preview_enabled;
	LrgNode             *preview_node; /* weak: the selection keeps it */

	/* Missing assets, recomputed after document changes */
	GPtrArray           *missing;      /* LrgNode* (borrowed) */
	gboolean             missing_dirty;

	/* Drops */
	gboolean             drop_hover;
	gfloat               drop_x;
	gfloat               drop_y;
	gchar               *drop_label;

	/* Active-viewport outline */
	LrgSpring            active_spring;

	/* Animation overlays (0.9.0) */
	gboolean             show_bones;
	gboolean             show_ik;
	GHashTable          *rest_skeletons; /* abs model path -> LrgSkeleton (bind pose) */
	guint                anim_bones;     /* painted by the last overlay pass */
	guint                anim_targets;

	/* Physics (0.9.0 M5): the selected node's collider / joint gizmo and
	 * the Physics overlay */
	LrgEditorPhysicsHandle phys_hot;      /* hovered handle */
	guint                phys_hot_index;
	LrgEditorPhysicsHandle phys_handle;   /* dragged handle, NONE when idle */
	guint                phys_index;
	LrgNode             *phys_node;       /* weak: the node being resized */
	LrgNode             *phys_before;     /* owned: its state before the drag */
	LrgDynamicsDebugFlags physics_flags;  /* the Physics overlay */
	LrgDebugDraw        *phys_debug;      /* owned: the play world, recorded per frame */
	gboolean             phys_draw_now;   /* set around this panel's render */
	GObject             *phys_runtime;    /* the runtime renderer we hook */
	gulong               phys_overlay_id;
	guint                phys_painted;
};

G_DEFINE_FINAL_TYPE (LrgEditorViewportPanel, lrg_editor_viewport_panel, LRG_TYPE_CONTAINER)

static void sync_header (LrgEditorViewportPanel *self);
static LrgNode *volume_node (LrgEditorViewportPanel *self);
static void register_methods (LrgEditorApp *app);
static void sync_anim_action (LrgEditorViewportPanel *self, const gchar *name, gboolean value);
static LrgNode *physics_node (LrgEditorViewportPanel *self);
static void physics_hook_runtime (LrgEditorViewportPanel *self);
static guint paint_physics_tree (LrgPaintList *list, const LrgEditorView *view, LrgLevel *level, LrgNode *node,
                                 LrgNode *skip, LrgDynamicsDebugFlags flags, gfloat ox, gfloat oy, gfloat scale);

/* This panel's camera as an LrgEditorView (picking and gizmo math). */
static void
current_view (LrgEditorViewportPanel *self,
              LrgEditorView          *view)
{
	lrg_editor_view_camera_to_view (lrg_editor_view_state_get_camera (self->app->view, (gint) self->index), view);
}

static gboolean
is_active (LrgEditorViewportPanel *self)
{
	return self->app != NULL && lrg_editor_view_state_get_active_viewport (self->app->view) == self->index;
}

#define NAV_OWNER_KEY "lrg-viewport-nav-owner"

/* Whether this panel took the press of the navigator's current gesture
 * (the navigator is shared; its signals are acted on once). */
static gboolean
nav_owned (LrgEditorViewportPanel *self)
{
	return self->app != NULL && g_object_get_data (G_OBJECT (self->app), NAV_OWNER_KEY) == self;
}

static void
mark_dirty (LrgEditorViewportPanel *self)
{
	self->dirty = TRUE;
}

/* The split this panel sits in, or NULL for a lone panel. */
static LrgEditorViewportSplit *
find_split (LrgEditorViewportPanel *self)
{
	LrgWidget *w;

	for (w = LRG_WIDGET (lrg_widget_get_parent (LRG_WIDGET (self))); w != NULL; w = LRG_WIDGET (lrg_widget_get_parent (w)))
		if (LRG_IS_EDITOR_VIEWPORT_SPLIT (w))
			return LRG_EDITOR_VIEWPORT_SPLIT (w);
	return NULL;
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

static void
render_scene (LrgViewport      *viewport,
              GrlRenderTexture *target,
              gfloat            width,
              gfloat            height,
              gpointer          user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	gboolean                active;

	if (self->app == NULL || self->app->renderer == NULL)
		return;
	active = is_active (self);
	lrg_editor_view_state_set_viewport_size (self->app->view, (gint) self->index, width, height);
	lrg_editor_scene_renderer_set_playing (self->app->renderer, lrg_editor_play_session_is_playing (self->app->play));

	/* The play world's physics, drawn from the runtime renderer's
	 * overlay hook inside this render's 3D pass */
	physics_hook_runtime (self);
	self->phys_draw_now = lrg_editor_viewport_panel_record_physics (self) > 0;
	lrg_editor_scene_renderer_render (self->app->renderer, self->app->view, (gint) self->index,
	                                  active ? self->app->nav : NULL, active, width, height);
	self->phys_draw_now = FALSE;
	lrg_debug_draw_end_frame (self->phys_debug);
}

/* The picture in picture: the scene through the selected camera node,
 * lit like this viewport (its lighting, shadow, sky and post overlays). */
static void
render_preview (LrgViewport      *viewport,
                GrlRenderTexture *target,
                gfloat            width,
                gfloat            height,
                gpointer          user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorViewOverlays   overlays;
	LrgEditorView           view;

	if (self->app == NULL || self->app->renderer == NULL || self->preview_node == NULL)
		return;
	overlays = lrg_editor_view_state_get_overlays (self->app->view, (gint) self->index) &
	           (LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING | LRG_EDITOR_VIEW_OVERLAY_SHADOWS |
	            LRG_EDITOR_VIEW_OVERLAY_SKY | LRG_EDITOR_VIEW_OVERLAY_POST);
	lrg_editor_scene_renderer_camera_node_view (self->preview_node, width, height, &view);
	lrg_editor_scene_renderer_render_view (self->app->renderer, &view, FALSE, LRG_EDITOR_SCENE_RENDERER_PREVIEW_SLOT,
	                                       LRG_EDITOR_RENDER_MODE_LIT, overlays, width, height);
}

/* The camera node the preview shows, or NULL. */
static LrgNode *
wanted_preview_node (LrgEditorViewportPanel *self)
{
	LrgNode       *primary;
	LrgNodeVisual *visual;

	if (self->app == NULL || !self->preview_enabled || !is_active (self))
		return NULL;
	primary = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->app->editor));
	if (primary == NULL)
		return NULL;
	visual = lrg_node_get_visual (primary);
	if (visual == NULL || lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_CAMERA)
		return NULL;
	return primary;
}

/* Shows, hides and re-targets the preview. */
static void
update_preview (LrgEditorViewportPanel *self)
{
	LrgNode *node = wanted_preview_node (self);

	if (node == self->preview_node)
		return;
	self->preview_node = node;
	lrg_widget_set_visible (LRG_WIDGET (self->preview), node != NULL);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	if (node != NULL)
		lrg_viewport_queue_render (self->preview);
}

/* Nodes with missing assets (walk after document changes only). */
static void
collect_missing (LrgEditorViewportPanel *self,
                 LrgNode                *node)
{
	GPtrArray *children;
	guint      i;

	if (!lrg_node_get_visible (node))
		return;
	if (lrg_editor_scene_renderer_is_asset_missing (self->app->renderer, node))
		g_ptr_array_add (self->missing, node);
	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
		collect_missing (self, g_ptr_array_index (children, i));
}

static void
refresh_missing (LrgEditorViewportPanel *self)
{
	LrgLevel *level;

	if (!self->missing_dirty || self->app == NULL)
		return;
	self->missing_dirty = FALSE;
	g_ptr_array_set_size (self->missing, 0);
	level = lrg_editor_get_level (self->app->editor);
	if (level != NULL && lrg_level_get_root (level) != NULL)
		collect_missing (self, lrg_level_get_root (level));
}

/* ==========================================================================
 * Overlay painting (the pointer-transparent child above the viewport)
 * ========================================================================== */

/* Where viewport-local pixel (0, 0) is on the canvas, and the scale. */
static gboolean
content_mapping (LrgEditorViewportPanel *self,
                 gfloat                 *ox,
                 gfloat                 *oy,
                 gfloat                 *scale,
                 GrlRectangle           *rect)
{
	LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (self->app->view, (gint) self->index);
	GrlRectangle         r = { 0 };

	if (!lrg_viewport_get_display_rect (self->viewport, &r) || r.width <= 0.0f)
		return FALSE;
	*ox = lrg_widget_get_world_x (LRG_WIDGET (self->viewport)) + r.x;
	*oy = lrg_widget_get_world_y (LRG_WIDGET (self->viewport)) + r.y;
	*scale = r.width / MAX (cam->view.width, 1.0f);
	if (rect != NULL)
	{
		*rect = r;
		rect->x = *ox;
		rect->y = *oy;
	}
	return TRUE;
}

/* ==========================================================================
 * Animation overlays: bones and IK targets
 * ========================================================================== */

/* m (column-major) * p */
static void
xform_point (const gfloat *m,
             const gfloat *p,
             gfloat       *out)
{
	guint r;

	for (r = 0; r < 3; r++)
		out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

/* Projects a world point to canvas coordinates. */
static gboolean
to_canvas (const LrgEditorView *view,
           const gfloat        *world,
           gfloat               ox,
           gfloat               oy,
           gfloat               scale,
           gfloat              *cx,
           gfloat              *cy)
{
	gfloat u, v;

	if (!lrg_editor_view_project (view, world, &u, &v, NULL))
		return FALSE;
	*cx = ox + u * scale;
	*cy = oy + v * scale;
	return TRUE;
}

static void
unref_nullable (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}

/* The bind-pose skeleton of a glTF model, loaded once per path. */
static LrgSkeleton *
rest_skeleton (LrgEditorViewportPanel *self,
               const gchar            *asset)
{
	g_autofree gchar *abs = NULL;
	LrgSkeleton      *skeleton;

	if (asset == NULL || self->app == NULL ||
	    (!g_str_has_suffix (asset, ".glb") && !g_str_has_suffix (asset, ".gltf")))
		return NULL;
	abs = editor_app_project_path (self->app, asset);
	if (self->rest_skeletons == NULL)
		self->rest_skeletons = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, unref_nullable);
	if (g_hash_table_contains (self->rest_skeletons, abs))
		return g_hash_table_lookup (self->rest_skeletons, abs);
	skeleton = g_file_test (abs, G_FILE_TEST_IS_REGULAR) ? lrg_gltf_load_skeleton (abs, 0, NULL) : NULL;
	if (skeleton != NULL)
		lrg_skeleton_calculate_world_poses (skeleton);
	/* a failed load is remembered too (NULL value): no retry per frame */
	g_hash_table_insert (self->rest_skeletons, g_strdup (abs), skeleton);
	return skeleton;
}

/* Joint dots and bone lines of @skeleton placed by @matrix. */
static void
paint_skeleton (LrgEditorViewportPanel *self,
                LrgPaintList           *list,
                LrgTheme               *theme,
                const LrgEditorView    *view,
                LrgSkeleton            *skeleton,
                const gfloat           *matrix,
                gfloat                  ox,
                gfloat                  oy,
                gfloat                  scale)
{
	const GrlColor *line = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	const GrlColor *dot = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	guint           n = lrg_skeleton_get_bone_count (skeleton);
	guint           i;

	for (i = 0; i < n; i++)
	{
		LrgBone           *bone = lrg_skeleton_get_bone (skeleton, (gint) i);
		const LrgBonePose *w = lrg_bone_get_world_pose (bone);
		gint               parent = lrg_bone_get_parent_index (bone);
		gfloat             local[3] = { w->position_x, w->position_y, w->position_z };
		gfloat             world[3];
		gfloat             x, y;

		xform_point (matrix, local, world);
		if (!to_canvas (view, world, ox, oy, scale, &x, &y))
			continue;
		if (parent >= 0 && (guint) parent < n)
		{
			const LrgBonePose *pw = lrg_bone_get_world_pose (lrg_skeleton_get_bone (skeleton, parent));
			gfloat             pl[3] = { pw->position_x, pw->position_y, pw->position_z };
			gfloat             pwld[3];
			gfloat             px, py;

			xform_point (matrix, pl, pwld);
			if (to_canvas (view, pwld, ox, oy, scale, &px, &py))
				lrg_paint_list_line (list, px, py, x, y, 2.0f, line);
		}
		lrg_paint_list_circle (list, x, y, 3.0f, dot);
		self->anim_bones++;
	}
}

/* The IK goals of a running animator: target rings, effector lines. */
static void
paint_ik_targets (LrgEditorViewportPanel *self,
                  LrgPaintList           *list,
                  LrgTheme               *theme,
                  const LrgEditorView    *view,
                  LrgAnimatorComponent3D *animator,
                  const gfloat           *matrix,
                  gfloat                  ox,
                  gfloat                  oy,
                  gfloat                  scale)
{
	LrgAnimGraph   *graph = lrg_animator_component3d_get_graph (animator);
	LrgSkeleton    *skeleton = lrg_animator_component3d_get_skeleton (animator);
	const GrlColor *ring = lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);
	guint           i, n = graph != NULL ? lrg_anim_graph_get_ik_goal_count (graph) : 0;

	for (i = 0; i < n; i++)
	{
		LrgIKGoal *goal = lrg_anim_graph_get_ik_goal (graph, i);
		gfloat     target[3];
		gfloat     x, y;

		if (!lrg_ik_goal_get_enabled (goal) ||
		    (!lrg_ik_goal_get_last_target (goal, target) && !lrg_ik_goal_get_target (goal, target)))
			continue;
		if (!to_canvas (view, target, ox, oy, scale, &x, &y))
			continue;
		/* the effector (or the chain's last bone) reaching for it */
		if (skeleton != NULL)
		{
			g_auto(GStrv)  bones = lrg_ik_goal_get_bones (goal);
			const gchar   *eff = lrg_ik_goal_get_effector (goal);
			LrgBone       *bone = NULL;

			if (eff == NULL && bones != NULL && bones[0] != NULL)
				eff = bones[g_strv_length (bones) - 1];
			bone = eff != NULL ? lrg_skeleton_get_bone_by_name (skeleton, eff) : NULL;
			if (bone != NULL)
			{
				const LrgBonePose *w = lrg_bone_get_world_pose (bone);
				gfloat             local[3] = { w->position_x, w->position_y, w->position_z };
				gfloat             world[3];
				gfloat             ex, ey;

				xform_point (matrix, local, world);
				if (to_canvas (view, world, ox, oy, scale, &ex, &ey))
					lrg_paint_list_line (list, ex, ey, x, y, 1.5f, ring);
			}
		}
		lrg_paint_list_border (list, x - 8.0f, y - 8.0f, 16.0f, 16.0f, 8.0f, 2.0f, ring);
		lrg_paint_list_circle (list, x, y, 2.5f, ring);
		lrg_editor_gizmo_paint_chip (list, theme, lrg_ik_goal_get_name (goal), x + 12.0f, y - 10.0f, FALSE, ring);
		self->anim_targets++;
	}
}

static void
paint_anim_overlay (LrgEditorViewportPanel *self,
                    LrgPaintList           *list,
                    LrgTheme               *theme,
                    const LrgEditorView    *view,
                    gfloat                  ox,
                    gfloat                  oy,
                    gfloat                  scale)
{
	LrgEditorApp       *app = self->app;
	LrgEditorSelection *sel;
	GPtrArray          *nodes;
	gboolean            playing;
	guint               k;

	self->anim_bones = 0;
	self->anim_targets = 0;
	if ((!self->show_bones && !self->show_ik) || app == NULL ||
	    (sel = lrg_editor_get_selection (app->editor)) == NULL)
		return;
	playing = lrg_editor_play_session_is_playing (app->play);
	nodes = lrg_editor_selection_get_nodes (sel);
	for (k = 0; nodes != NULL && k < nodes->len; k++)
	{
		LrgNode       *node = g_ptr_array_index (nodes, k);
		GObject       *live = playing ? lrg_editor_play_session_get_live_for_node (app->play, node) : NULL;
		LrgNodeVisual *visual = lrg_node_get_visual (node);

		if (live != NULL && LRG_IS_ENTITY3D (live))
		{
			LrgEntity3D            *entity = LRG_ENTITY3D (live);
			LrgComponent           *ac = lrg_entity3d_get_component (entity, LRG_TYPE_ANIMATOR_COMPONENT3D);
			LrgComponent           *sc = lrg_entity3d_get_component (entity, LRG_TYPE_SKINNED_MODEL_COMPONENT);
			LrgSkeleton            *skeleton = NULL;
			const gfloat           *m = lrg_entity3d_peek_world_matrix (entity);

			if (ac != NULL)
				skeleton = lrg_animator_component3d_get_skeleton (LRG_ANIMATOR_COMPONENT3D (ac));
			if (skeleton == NULL && sc != NULL)
				skeleton = lrg_skinned_model_component_get_skeleton (LRG_SKINNED_MODEL_COMPONENT (sc));
			if (self->show_bones && skeleton != NULL)
				paint_skeleton (self, list, theme, view, skeleton, m, ox, oy, scale);
			if (self->show_ik && ac != NULL)
				paint_ik_targets (self, list, theme, view, LRG_ANIMATOR_COMPONENT3D (ac), m, ox, oy, scale);
		}
		else if (self->show_bones && visual != NULL &&
		         lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_MESH_ASSET)
		{
			LrgSkeleton *skeleton = rest_skeleton (self, lrg_node_visual_get_asset (visual));
			gfloat       m[16];

			if (skeleton == NULL)
				continue;
			lrg_editor_node_get_world_matrix (node, m);
			paint_skeleton (self, list, theme, view, skeleton, m, ox, oy, scale);
		}
	}
}

static void
overlay_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	LrgEditorViewCamera    *cam;
	LrgEditorViewOverlays   overlays;
	LrgEditorView           view;
	LrgEditorViewportSplit *split;
	GrlRectangle            rect;
	gfloat                  ox, oy, scale;
	gboolean                active = is_active (self);
	guint                   i;

	if (app == NULL || !content_mapping (self, &ox, &oy, &scale, &rect))
		return;
	cam = lrg_editor_view_state_get_camera (app->view, (gint) self->index);
	overlays = lrg_editor_view_state_get_overlays (app->view, (gint) self->index);
	current_view (self, &view);

	lrg_paint_list_push_clip (list, rect.x, rect.y, rect.width, rect.height);

	/* Broken references first, under everything else */
	refresh_missing (self);
	for (i = 0; i < self->missing->len; i++)
		lrg_editor_gizmo_paint_missing_asset (list, theme, &view, g_ptr_array_index (self->missing, i), ox, oy,
		                                      scale);

	/* The hovered tile cell while painting */
	if (active && app->tile_hover_valid && self->hovered && lrg_editor_view_state_get_tile_mode (app->view))
	{
		LrgNode *node = editor_tiles_target (app);

		if (node != NULL)
			lrg_editor_gizmo_paint_tile_hover (list, theme, &view, node, app->tile_hover_x, app->tile_hover_y,
			                                   self->tile_erase || lrg_editor_view_state_get_tile_tool (app->view) ==
			                                                           LRG_EDITOR_TILE_TOOL_ERASER,
			                                   ox, oy, scale);
	}

	/* Volumes of the selected light / decal / probe, and LOD bounds */
	{
		LrgNode *vnode = volume_node (self);

		if (vnode != NULL)
			lrg_editor_gizmo_paint_volume (list, theme, &view, vnode,
			                               self->vol_handle != LRG_EDITOR_VOLUME_HANDLE_NONE ? self->vol_handle
			                                                                                 : self->vol_hot,
			                               self->vol_handle != LRG_EDITOR_VOLUME_HANDLE_NONE, ox, oy, scale);
		if ((overlays & LRG_EDITOR_VIEW_OVERLAY_BOUNDS) && app->renderer != NULL)
		{
			LrgEditorSelection *sel = lrg_editor_get_selection (app->editor);
			LrgNode            *primary = (sel != NULL) ? lrg_editor_selection_get_primary (sel) : NULL;
			LrgNodeVisual      *visual = (primary != NULL) ? lrg_node_get_visual (primary) : NULL;
			LrgLodGroup        *lod = NULL;

			if (visual != NULL && lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_MESH_ASSET &&
			    lrg_node_visual_get_asset (visual) != NULL)
				lod = lrg_editor_scene_renderer_get_lod_group (app->renderer, lrg_node_visual_get_asset (visual));
			if (lod != NULL)
				lrg_editor_gizmo_paint_lod_bounds (list, theme, &view, primary, lod, ox, oy, scale);
		}
	}

	/* Physics: every authored body (the Physics overlay, authoring only)
	 * and the selected node's collider / joint gizmo with its handles */
	self->phys_painted = 0;
	{
		LrgLevel *level = lrg_editor_get_level (app->editor);
		LrgNode  *pnode = physics_node (self);

		if (self->physics_flags != 0 && level != NULL && lrg_level_get_root (level) != NULL &&
		    !lrg_editor_play_session_is_playing (app->play))
			self->phys_painted += paint_physics_tree (list, &view, level, lrg_level_get_root (level), pnode,
			                                          self->physics_flags, ox, oy, scale);
		if (pnode != NULL)
		{
			LrgEditorPhysicsPanel *pp = lrg_editor_physics_panel_find (app);
			LrgNodePhysics        *phys = lrg_node_get_physics (pnode);
			gboolean               dragging = self->phys_handle != LRG_EDITOR_PHYSICS_HANDLE_NONE;

			lrg_editor_gizmo_paint_physics (list, theme, &view, level, pnode,
			                                dragging ? self->phys_handle : self->phys_hot,
			                                dragging ? self->phys_index : self->phys_hot_index,
			                                (pp != NULL && lrg_editor_physics_panel_get_node (pp) == pnode)
			                                ? lrg_editor_physics_panel_get_selected_shape (pp) : -1,
			                                dragging, ox, oy, scale);
			self->phys_painted += (phys != NULL ? lrg_node_physics_get_shapes (phys)->len +
			                                      lrg_node_physics_get_joints (phys)->len : 0u) +
			                      (lrg_node_find_component (pnode, "collider") != NULL && lrg_node_get_is_2d (pnode)
			                       ? 1u : 0u);
		}
	}

	/* Skeletons and IK targets of the selected animated nodes */
	paint_anim_overlay (self, list, theme, &view, ox, oy, scale);

	/* The transform gizmo (every viewport shows it, through its camera) */
	if ((overlays & LRG_EDITOR_VIEW_OVERLAY_GIZMOS) && !lrg_editor_view_state_get_tile_mode (app->view))
		lrg_editor_gizmo_paint_transform (list, app->gizmo, lrg_editor_get_tool (app->editor), &view, ox, oy, scale);

	/* The box-select rectangle of this panel's gesture */
	if (nav_owned (self))
	{
		gfloat bx, by, bw, bh;

		if (lrg_editor_viewport_nav_get_box_rect (app->nav, &bx, &by, &bw, &bh))
			lrg_editor_gizmo_paint_box_select (list, theme, ox + bx * scale, oy + by * scale, bw * scale, bh * scale);
	}

	/* Orientation axes, bottom left (the nav's hot end for the active one) */
	lrg_editor_gizmo_paint_axes (list, theme, cam, active ? lrg_editor_viewport_nav_get_axis_hot (app->nav) : -1,
	                             active && lrg_editor_viewport_nav_get_mode (app->nav) == LRG_EDITOR_NAV_MODE_GIZMO,
	                             ox, oy, scale);

	/* Stats, top right */
	if (overlays & LRG_EDITOR_VIEW_OVERLAY_STATS)
	{
		LrgEditorSelection *sel = lrg_editor_get_selection (app->editor);
		LrgLightSet        *lights = lrg_editor_scene_renderer_get_light_set (app->renderer);
		g_autofree gchar   *text = NULL;

		/* Narrow viewports (split layouts) get the short form */
		if (rect.width >= 520.0f)
			text = g_strdup_printf ("%u nodes   %u selected   %.0f fps\n%u lights%s   %u draws",
			                        editor_app_count_nodes (app),
			                        sel != NULL ? lrg_editor_selection_get_count (sel) : 0u, (gdouble) app->fps,
			                        lrg_light_set_get_point_light_count (lights),
			                        lrg_editor_scene_renderer_has_scene_sun (app->renderer) ? " + sun" : " + editor sun",
			                        lrg_editor_scene_renderer_get_drawn (app->renderer));
		else
			text = g_strdup_printf ("%u nodes  %.0f fps", editor_app_count_nodes (app), (gdouble) app->fps);
		lrg_editor_gizmo_paint_chip (list, theme, text, rect.x + rect.width - 10.0f, rect.y + 10.0f, TRUE, NULL);
	}

	/* An empty level gets a hint */
	if (editor_app_count_nodes (app) == 0 && active)
	{
		const gchar *hint = "Empty level: right-click or use Create to add nodes, or drag assets here";
		gfloat       tw = 0.0f;

		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), hint,
		                        lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL), 0.0f, &tw, NULL);
		lrg_editor_gizmo_paint_chip (list, theme, hint, rect.x + (rect.width - tw) * 0.5f - 8.0f,
		                             rect.y + rect.height * 0.5f, FALSE, NULL);
	}

	/* The drag readout follows the pointer */
	if (self->gizmo_drag)
		lrg_editor_gizmo_paint_drag_readout (list, theme, app->gizmo, lrg_editor_get_tool (app->editor),
		                                     ox + self->pointer_u * scale, oy + self->pointer_v * scale);

	/* A drop hovering over the scene: crosshair and what will happen */
	if (self->drop_hover)
	{
		const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);

		lrg_paint_list_circle (list, self->drop_x, self->drop_y, 5.0f, accent);
		lrg_paint_list_line (list, self->drop_x - 14.0f, self->drop_y, self->drop_x + 14.0f, self->drop_y, 1.5f, accent);
		lrg_paint_list_line (list, self->drop_x, self->drop_y - 14.0f, self->drop_x, self->drop_y + 14.0f, 1.5f, accent);
		if (self->drop_label != NULL)
			lrg_editor_gizmo_paint_chip (list, theme, self->drop_label, self->drop_x + 16.0f, self->drop_y + 12.0f,
			                             FALSE, accent);
	}

	lrg_paint_list_pop_clip (list);

	/* Frames on top: play mode, then the active-viewport outline */
	if (lrg_editor_play_session_is_playing (app->play))
		lrg_editor_gizmo_paint_play_frame (list, theme, rect.x, rect.y, rect.width, rect.height,
		                                   lrg_editor_play_session_is_paused (app->play));
	split = find_split (self);
	if (split != NULL && lrg_editor_viewport_split_get_n_visible (split) > 1)
		lrg_editor_gizmo_paint_active_outline (list, theme, rect.x, rect.y, rect.width, rect.height,
		                                       self->active_spring.value);
}

/* ==========================================================================
 * Tile painting
 * ========================================================================== */

/* Cell under a viewport pixel: the ray meets the node's local XY plane. */
static gboolean
cell_under (LrgEditorViewportPanel *self,
            LrgNode                *node,
            gfloat                  u,
            gfloat                  v,
            gint                   *x,
            gint                   *y)
{
	LrgEditorView view;
	gfloat        world[16], o[3], d[3], n[3], p[3], t, len;
	const gfloat  z[3] = { 0.0f, 0.0f, 1.0f };

	*x = *y = -1;
	if (lrg_node_visual_get_tilemap (lrg_node_get_visual (node)) == NULL)
		return FALSE;

	lrg_editor_node_get_world_matrix (node, world);
	lrg_editor_math_transform_direction (world, z, n);
	len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (len < 1e-6f)
		return FALSE;
	n[0] /= len;
	n[1] /= len;
	n[2] /= len;

	current_view (self, &view);
	lrg_editor_view_get_ray (&view, u, v, o, d);
	if (!lrg_editor_ray_plane (o, d, world + 12, n, &t) || t < 0.0f)
		return FALSE;
	p[0] = o[0] + d[0] * t;
	p[1] = o[1] + d[1] * t;
	p[2] = o[2] + d[2] * t;
	lrg_editor_tile_painter_cell_at (node, p, x, y);
	return TRUE;
}

/* Updates the hovered cell (tilemap.tool "hover"). */
static void
update_tile_hover (LrgEditorViewportPanel *self,
                   LrgNode                *node)
{
	LrgEditorApp   *app = self->app;
	LrgNodeTilemap *grid;
	gboolean        in;

	in = cell_under (self, node, self->pointer_u, self->pointer_v, &app->tile_hover_x, &app->tile_hover_y);
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	app->tile_hover_valid = in && grid != NULL && self->hovered &&
	                        lrg_node_tilemap_in_bounds (grid, app->tile_hover_x, app->tile_hover_y);
}

/* The node tile painting applies to, or NULL when paint mode is off. */
static LrgNode *
tile_target (LrgEditorViewportPanel *self)
{
	if (!lrg_editor_view_state_get_tile_mode (self->app->view))
		return NULL;
	return editor_tiles_target (self->app);
}

static void
tile_finish (LrgEditorViewportPanel *self,
             LrgNode                *node,
             gboolean                commit)
{
	LrgEditorApp *app = self->app;

	if (lrg_editor_tile_painter_is_stroking (app->tile_painter))
	{
		guint n = lrg_editor_tile_painter_end_stroke (app->tile_painter);

		editor_app_set_status (app, "%s %u tile(s)", self->tile_erase ? "Erased" : "Painted", n);
	}
	if (self->tile_rect && commit && node != NULL)
	{
		guint n = lrg_editor_tile_painter_paint_rect (app->tile_painter, node, self->tile_rect_x0,
		                                              self->tile_rect_y0, app->tile_hover_x,
		                                              app->tile_hover_y, self->tile_erase);

		editor_app_set_status (app, "Rectangle: %u tile(s)", n);
	}
	self->tile_rect = FALSE;
	self->tile_drag = FALSE;
	mark_dirty (self);
}

/* Left press in paint mode: start the tool (Ctrl + click picks, Shift
 * erases). Returns TRUE when the tile tool took the press. */
static gboolean
tile_press (LrgEditorViewportPanel *self,
            LrgInputModifiers       mods)
{
	LrgEditorApp      *app = self->app;
	LrgNode           *node = tile_target (self);
	LrgEditorTileTool  tool;

	if (node == NULL || (mods & LRG_INPUT_MODIFIER_ALT))
		return FALSE;

	tool = (mods & LRG_INPUT_MODIFIER_CTRL) ? LRG_EDITOR_TILE_TOOL_PICK
	                                        : lrg_editor_view_state_get_tile_tool (app->view);
	self->tile_erase = tool == LRG_EDITOR_TILE_TOOL_ERASER || (mods & LRG_INPUT_MODIFIER_SHIFT);

	/* A node that was never painted gets its grid first */
	if (lrg_node_visual_get_tilemap (lrg_node_get_visual (node)) == NULL && tool != LRG_EDITOR_TILE_TOOL_PICK)
		lrg_editor_tile_painter_ensure_tilemap (app->tile_painter, node);
	update_tile_hover (self, node);

	switch (tool)
	{
	case LRG_EDITOR_TILE_TOOL_BRUSH:
	case LRG_EDITOR_TILE_TOOL_ERASER:
		if (lrg_editor_tile_painter_begin_stroke (app->tile_painter, node, self->tile_erase))
		{
			update_tile_hover (self, node);
			lrg_editor_tile_painter_stroke_to (app->tile_painter, app->tile_hover_x, app->tile_hover_y);
			self->tile_drag = TRUE;
		}
		break;
	case LRG_EDITOR_TILE_TOOL_RECT:
		self->tile_rect = TRUE;
		self->tile_rect_x0 = app->tile_hover_x;
		self->tile_rect_y0 = app->tile_hover_y;
		self->tile_drag = TRUE;
		break;
	case LRG_EDITOR_TILE_TOOL_FILL:
		editor_app_set_status (app, "Filled %u tile(s)",
		                       lrg_editor_tile_painter_fill (app->tile_painter, node, app->tile_hover_x,
		                                                     app->tile_hover_y, self->tile_erase));
		break;
	case LRG_EDITOR_TILE_TOOL_PICK:
	default:
		{
			guint tile = lrg_editor_tile_painter_pick (app->tile_painter, node, app->tile_hover_x,
			                                           app->tile_hover_y);

			if (tile != 0)
				editor_app_set_status (app, "Picked tile %u", tile);
		}
		break;
	}
	mark_dirty (self);
	return TRUE;
}

/* ==========================================================================
 * Picking and the navigator's gestures
 * ========================================================================== */

/* Click picking with cycling through overlapping nodes. */
static void
on_nav_click (LrgEditorViewportNav *nav,
              gfloat                x,
              gfloat                y,
              LrgInputModifiers     mods,
              gpointer              user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditor              *editor;
	g_autoptr(GPtrArray)    hits = NULL;
	LrgEditorView           view;
	gfloat                  o[3], d[3];
	LrgNode                *current;
	LrgNode                *pick;
	gboolean                same_spot;
	guint                   i;

	if (!nav_owned (self))
		return;
	editor = self->app->editor;
	current = lrg_editor_selection_get_primary (lrg_editor_get_selection (editor));
	current_view (self, &view);
	lrg_editor_view_get_ray (&view, x, y, o, d);
	hits = lrg_editor_pick_all (lrg_editor_get_level (editor), o, d, FALSE);
	if (hits->len == 0)
	{
		if (!(mods & (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL)))
			lrg_editor_select (editor, NULL, FALSE);
		return;
	}

	/* Clicking the same spot again walks to the next node behind */
	same_spot = fabsf (x - self->last_pick_x) < CLICK_SLOP && fabsf (y - self->last_pick_y) < CLICK_SLOP;
	pick = g_ptr_array_index (hits, 0);
	if (same_spot && current != NULL && !(mods & (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL)))
	{
		for (i = 0; i < hits->len; i++)
			if (g_ptr_array_index (hits, i) == current)
			{
				pick = g_ptr_array_index (hits, (i + 1) % hits->len);
				break;
			}
	}
	self->last_pick_x = x;
	self->last_pick_y = y;

	if (mods & LRG_INPUT_MODIFIER_CTRL)
		lrg_editor_toggle_selected (editor, pick);
	else
		lrg_editor_select (editor, pick, (mods & LRG_INPUT_MODIFIER_SHIFT) != 0);
	editor_app_set_status (self->app, "%s%s", lrg_node_get_name (pick) != NULL ? lrg_node_get_name (pick) : "node",
	                       hits->len > 1 ? "  (click again to cycle overlapping nodes)" : "");
}

static void
on_nav_box_select (LrgEditorViewportNav *nav,
                   gfloat                x0,
                   gfloat                y0,
                   gfloat                x1,
                   gfloat                y1,
                   LrgInputModifiers     mods,
                   gpointer              user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	g_autoptr(GPtrArray)    boxed = NULL;
	LrgEditorView           view;

	if (!nav_owned (self))
		return;
	current_view (self, &view);
	boxed = lrg_editor_box_select (lrg_editor_get_level (self->app->editor), &view, x0, y0, x1, y1, FALSE);
	lrg_editor_select_nodes (self->app->editor, boxed, (mods & LRG_INPUT_MODIFIER_SHIFT) != 0);
	editor_app_set_status (self->app, "Box selected %u node(s)", boxed->len);
}

/* The viewport context menu: create, paste, duplicate, delete, group,
 * focus, frame all, select all. */
static GMenuModel *
build_context_menu (void)
{
	static const gchar *items[] = {
		"edit.paste", "-", "edit.duplicate", "edit.delete", "node.group", "-",
		"view.focus", "view.frame-all", "edit.select-all", NULL
	};
	static const gchar *labels[] = {
		"Paste", "-", "Duplicate", "Delete", "Group Selection", "-",
		"Focus Selection", "Frame All", "Select All", NULL
	};
	GMenu                  *menu = g_menu_new ();
	g_autoptr(GMenu)        create = g_menu_new ();
	g_autoptr(GMenu)        section = g_menu_new ();
	const EditorCreateItem *kinds;
	guint                   n, i;

	kinds = editor_create_items (&n);
	for (i = 0; i < n; i++)
	{
		g_autoptr(GMenuItem) item = NULL;

		if (kinds[i].id[0] == '-')
			continue;
		item = g_menu_item_new (kinds[i].label, NULL);
		g_menu_item_set_action_and_target_value (item, "win.create", g_variant_new_string (kinds[i].id));
		g_menu_append_item (create, item);
	}
	g_menu_append_submenu (section, "Create", G_MENU_MODEL (create));
	for (i = 0; items[i] != NULL; i++)
	{
		g_autofree gchar *detailed = NULL;

		if (items[i][0] == '-')
		{
			g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
			g_clear_object (&section);
			section = g_menu_new ();
			continue;
		}
		detailed = g_strconcat ("editor.", items[i], NULL);
		g_menu_append (section, labels[i], detailed);
	}
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	return G_MENU_MODEL (menu);
}

static void
on_nav_context_menu (LrgEditorViewportNav *nav,
                     gfloat                x,
                     gfloat                y,
                     gpointer              user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	gfloat                  wx = 0.0f, wy = 0.0f;

	if (!nav_owned (self))
		return;
	if (self->context_menu == NULL)
	{
		g_autoptr(GMenuModel) model = build_context_menu ();

		self->context_menu = g_object_ref_sink (lrg_menu_new_from_model (model));
	}
	lrg_viewport_texture_to_widget (self->viewport, x, y, &wx, &wy);
	lrg_menu_popup_at_point (self->context_menu, LRG_WIDGET (self),
	                         lrg_widget_get_world_x (LRG_WIDGET (self->viewport)) + wx,
	                         lrg_widget_get_world_y (LRG_WIDGET (self->viewport)) + wy);
}

/* ==========================================================================
 * Pointer and key routing
 * ========================================================================== */

static void
take_focus (LrgEditorViewportPanel *self)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));

	if (LRG_IS_CANVAS (root) && lrg_canvas_get_focused_widget (LRG_CANVAS (root)) != LRG_WIDGET (self->viewport))
		lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (self->viewport));
}

/* Makes this the view state's active viewport (keys and view.* act on it). */
static void
activate (LrgEditorViewportPanel *self)
{
	if (!is_active (self))
		lrg_editor_view_state_set_active_viewport (self->app->view, self->index);
}

/* ==========================================================================
 * Volume gizmo drags
 * ========================================================================== */

/* The node whose volume gizmo shows: the primary selection, unlocked,
 * with the gizmos overlay on and outside tile painting. */
static LrgNode *
volume_node (LrgEditorViewportPanel *self)
{
	LrgEditorApp       *app = self->app;
	LrgEditorSelection *sel;
	LrgNode            *node;

	if (app == NULL || lrg_editor_view_state_get_tile_mode (app->view) ||
	    !(lrg_editor_view_state_get_overlays (app->view, (gint) self->index) & LRG_EDITOR_VIEW_OVERLAY_GIZMOS) ||
	    lrg_editor_play_session_is_playing (app->play))
		return NULL;
	sel = lrg_editor_get_selection (app->editor);
	node = (sel != NULL) ? lrg_editor_selection_get_primary (sel) : NULL;
	if (node == NULL || lrg_node_get_locked (node) || !lrg_editor_volume_has_gizmo (node))
		return NULL;
	return node;
}

static gboolean
volume_press (LrgEditorViewportPanel *self,
              gfloat                  u,
              gfloat                  v)
{
	LrgNode              *node = volume_node (self);
	LrgEditorView         view;
	LrgEditorVolumeHandle h;

	if (node == NULL)
		return FALSE;
	current_view (self, &view);
	h = lrg_editor_volume_hit_test (&view, node, u, v);
	if (h == LRG_EDITOR_VOLUME_HANDLE_NONE)
		return FALSE;
	self->vol_handle = h;
	self->vol_hot = h;
	g_set_weak_pointer (&self->vol_node, node);
	g_clear_object (&self->vol_before);
	self->vol_before = lrg_editor_snapshot_node (node);
	mark_dirty (self);
	return TRUE;
}

/* Writes the value under the pointer into the node, live. */
static void
volume_drag (LrgEditorViewportPanel *self,
             gfloat                  u,
             gfloat                  v)
{
	LrgEditorView  view;
	gdouble        value;
	LrgNodeVisual *visual;
	const gchar   *param = lrg_editor_volume_handle_get_param (self->vol_handle);

	if (self->vol_node == NULL || param == NULL)
		return;
	current_view (self, &view);
	if (!lrg_editor_volume_drag_value (&view, self->vol_node, self->vol_handle, u, v, &value))
		return;
	/* Ctrl snaps to tenths (whole degrees for cones) */
	if (self->mods & LRG_INPUT_MODIFIER_CTRL)
		value = (self->vol_handle == LRG_EDITOR_VOLUME_HANDLE_CONE ||
		         self->vol_handle == LRG_EDITOR_VOLUME_HANDLE_INNER_CONE) ? round (value) : round (value * 10.0) / 10.0;
	visual = lrg_node_get_visual (self->vol_node);
	lrg_node_visual_set_param_double (visual, param, value);
	/* a spot light without an explicit inner cone keeps the 0.8 ratio */
	if (self->vol_handle == LRG_EDITOR_VOLUME_HANDLE_CONE &&
	    lrg_node_visual_get_param_double (visual, "spot_inner_angle", 0.0) > value)
		lrg_node_visual_set_param_double (visual, "spot_inner_angle", value);
	if (lrg_editor_get_level (self->app->editor) != NULL)
		lrg_level_notify_node_changed (lrg_editor_get_level (self->app->editor), self->vol_node);
	mark_dirty (self);
}

/* Ends the drag: one undo step, or the old state back on cancel. */
static void
volume_finish (LrgEditorViewportPanel *self,
               gboolean                commit)
{
	if (self->vol_node != NULL && self->vol_before != NULL)
	{
		if (commit)
		{
			g_autofree gchar *label = g_strdup_printf ("Resize %s",
			                                           lrg_editor_volume_handle_get_param (self->vol_handle));

			lrg_editor_commit_node_state (self->app->editor, self->vol_node, self->vol_before, label, FALSE);
		}
		else
		{
			lrg_node_copy_state (self->vol_node, self->vol_before);
			if (lrg_editor_get_level (self->app->editor) != NULL)
				lrg_level_notify_node_changed (lrg_editor_get_level (self->app->editor), self->vol_node);
			editor_app_set_status (self->app, "Resize cancelled");
		}
	}
	self->vol_handle = LRG_EDITOR_VOLUME_HANDLE_NONE;
	g_clear_weak_pointer (&self->vol_node);
	g_clear_object (&self->vol_before);
	mark_dirty (self);
}

/* ==========================================================================
 * Physics gizmo drags (collider dimensions, joint anchors, 2D colliders)
 * ========================================================================== */

/* The node whose physics gizmo shows: the primary selection, unlocked,
 * with the gizmos overlay on, outside tile painting and play. */
static LrgNode *
physics_node (LrgEditorViewportPanel *self)
{
	LrgEditorApp       *app = self->app;
	LrgEditorSelection *sel;
	LrgNode            *node;

	if (app == NULL || lrg_editor_view_state_get_tile_mode (app->view) ||
	    !(lrg_editor_view_state_get_overlays (app->view, (gint) self->index) & LRG_EDITOR_VIEW_OVERLAY_GIZMOS) ||
	    lrg_editor_play_session_is_playing (app->play))
		return NULL;
	sel = lrg_editor_get_selection (app->editor);
	node = (sel != NULL) ? lrg_editor_selection_get_primary (sel) : NULL;
	if (node == NULL || lrg_node_get_locked (node) || !lrg_editor_physics_has_gizmo (node))
		return NULL;
	return node;
}

static gboolean
physics_press (LrgEditorViewportPanel *self,
               gfloat                  u,
               gfloat                  v)
{
	LrgNode               *node = physics_node (self);
	LrgEditorView          view;
	LrgEditorPhysicsHandle h;
	guint                  index = 0;

	if (node == NULL)
		return FALSE;
	current_view (self, &view);
	h = lrg_editor_physics_hit_test (&view, node, u, v, &index);
	if (h == LRG_EDITOR_PHYSICS_HANDLE_NONE)
		return FALSE;
	self->phys_handle = h;
	self->phys_index = index;
	self->phys_hot = h;
	self->phys_hot_index = index;
	g_set_weak_pointer (&self->phys_node, node);
	g_clear_object (&self->phys_before);
	self->phys_before = lrg_editor_snapshot_node (node);
	/* the Physics panel follows the grabbed shape */
	if (h != LRG_EDITOR_PHYSICS_HANDLE_ANCHOR && h < LRG_EDITOR_PHYSICS_HANDLE_WIDTH_2D &&
	    lrg_editor_physics_panel_find (self->app) != NULL)
		lrg_editor_physics_panel_set_selected_shape (lrg_editor_physics_panel_find (self->app), (gint) index);
	mark_dirty (self);
	return TRUE;
}

/* Writes the value under the pointer into the node, live. The snapshot
 * taken at the press holds deep copies, so the live block can change in
 * place until the release commits one step. */
static void
physics_drag (LrgEditorViewportPanel *self,
              gfloat                  u,
              gfloat                  v)
{
	LrgEditorView        view;
	gfloat               value[3];
	LrgNodePhysics      *phys;
	LrgNodePhysicsShape *shape = NULL;
	LrgNodePhysicsJoint *joint = NULL;
	gint                 k;

	if (self->phys_node == NULL || self->phys_handle == LRG_EDITOR_PHYSICS_HANDLE_NONE)
		return;
	current_view (self, &view);
	if (!lrg_editor_physics_drag_value (&view, self->phys_node, self->phys_handle, self->phys_index, u, v, value))
		return;
	/* Ctrl snaps to centimetres (anchors to decimetres) */
	if (self->mods & LRG_INPUT_MODIFIER_CTRL)
		for (k = 0; k < 3; k++)
			value[k] = (self->phys_handle == LRG_EDITOR_PHYSICS_HANDLE_ANCHOR) ? roundf (value[k] * 10.0f) / 10.0f
			                                                                   : MAX (roundf (value[k] * 100.0f) / 100.0f, 0.01f);
	phys = lrg_node_get_physics (self->phys_node);
	if (phys != NULL && self->phys_index < lrg_node_physics_get_shapes (phys)->len)
		shape = g_ptr_array_index (lrg_node_physics_get_shapes (phys), self->phys_index);
	if (phys != NULL && self->phys_index < lrg_node_physics_get_joints (phys)->len)
		joint = g_ptr_array_index (lrg_node_physics_get_joints (phys), self->phys_index);

	switch (self->phys_handle)
	{
	case LRG_EDITOR_PHYSICS_HANDLE_HALF_X:
	case LRG_EDITOR_PHYSICS_HANDLE_HALF_Y:
	case LRG_EDITOR_PHYSICS_HANDLE_HALF_Z:
		if (shape != NULL)
		{
			gfloat he[3];

			lrg_node_physics_shape_get_half_extents (shape, he);
			he[self->phys_handle - LRG_EDITOR_PHYSICS_HANDLE_HALF_X] = value[0];
			lrg_node_physics_shape_set_half_extents (shape, he[0], he[1], he[2]);
		}
		break;
	case LRG_EDITOR_PHYSICS_HANDLE_RADIUS:
		if (shape != NULL)
			lrg_node_physics_shape_set_radius (shape, value[0]);
		break;
	case LRG_EDITOR_PHYSICS_HANDLE_HALF_HEIGHT:
		if (shape != NULL)
			lrg_node_physics_shape_set_half_height (shape, value[0]);
		break;
	case LRG_EDITOR_PHYSICS_HANDLE_ANCHOR:
		if (joint != NULL)
			lrg_node_physics_joint_set_anchor (joint, value[0], value[1], value[2]);
		break;
	case LRG_EDITOR_PHYSICS_HANDLE_WIDTH_2D:
	case LRG_EDITOR_PHYSICS_HANDLE_HEIGHT_2D:
	case LRG_EDITOR_PHYSICS_HANDLE_RADIUS_2D:
		{
			LrgComponentDesc *desc = lrg_node_find_component (self->phys_node, "collider");

			if (desc != NULL)
				lrg_component_desc_set_double (desc, lrg_editor_physics_handle_get_field (self->phys_handle),
				                               (gdouble) value[0]);
		}
		break;
	case LRG_EDITOR_PHYSICS_HANDLE_NONE:
	default:
		break;
	}
	if (lrg_editor_get_level (self->app->editor) != NULL)
		lrg_level_notify_node_changed (lrg_editor_get_level (self->app->editor), self->phys_node);
	mark_dirty (self);
}

/* Ends the drag: one undo step, or the old state back on cancel. */
static void
physics_finish (LrgEditorViewportPanel *self,
                gboolean                commit)
{
	if (self->phys_node != NULL && self->phys_before != NULL)
	{
		if (commit)
		{
			g_autofree gchar *label = g_strdup_printf ("Resize %s",
			                                           lrg_editor_physics_handle_get_field (self->phys_handle));

			lrg_editor_commit_node_state (self->app->editor, self->phys_node, self->phys_before, label, FALSE);
		}
		else
		{
			lrg_node_copy_state (self->phys_node, self->phys_before);
			if (lrg_editor_get_level (self->app->editor) != NULL)
				lrg_level_notify_node_changed (lrg_editor_get_level (self->app->editor), self->phys_node);
			editor_app_set_status (self->app, "Resize cancelled");
		}
	}
	self->phys_handle = LRG_EDITOR_PHYSICS_HANDLE_NONE;
	g_clear_weak_pointer (&self->phys_node);
	g_clear_object (&self->phys_before);
	mark_dirty (self);
}

/* Walks the level and paints every node's authored physics. */
static guint
paint_physics_tree (LrgPaintList        *list,
                    const LrgEditorView *view,
                    LrgLevel            *level,
                    LrgNode             *node,
                    LrgNode             *skip,
                    LrgDynamicsDebugFlags flags,
                    gfloat               ox,
                    gfloat               oy,
                    gfloat               scale)
{
	GPtrArray *kids = lrg_node_get_children (node);
	guint      n = 0, i;

	if (node != skip && lrg_node_get_visible (node) &&
	    (lrg_node_get_physics (node) != NULL || lrg_node_find_component (node, "collider") != NULL))
		n += lrg_editor_gizmo_paint_physics_shapes (list, view, level, node, flags, 0.55f, ox, oy, scale);
	for (i = 0; kids != NULL && i < kids->len; i++)
		n += paint_physics_tree (list, view, level, g_ptr_array_index (kids, i), skip, flags, ox, oy, scale);
	return n;
}

/* The play world's physics -> the debug draw batch. */
guint
lrg_editor_viewport_panel_record_physics (LrgEditorViewportPanel *self)
{
	LrgWorld3D *world;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), 0);

	lrg_debug_draw_clear (self->phys_debug);
	if (self->app == NULL || self->physics_flags == 0 || !lrg_editor_play_session_is_playing (self->app->play))
		return 0;
	world = lrg_editor_play_session_get_world3d (self->app->play);
	if (world == NULL || lrg_world3d_get_physics (world) == NULL)
		return 0;
	lrg_world3d_debug_draw_physics (world, self->phys_debug, self->physics_flags);
	return lrg_debug_draw_get_count (self->phys_debug);
}

/* The runtime renderer's "draw-overlay" hook: inside the 3D pass of this
 * panel's render, draw the recorded physics. */
static void
on_runtime_draw_overlay (GObject  *runtime,
                         gpointer  view,
                         gpointer  user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	if (self->phys_draw_now)
		lrg_debug_draw_draw_3d (self->phys_debug);
}

/* Hooks the shared runtime renderer once (it lives as long as the app's
 * scene renderer). */
static void
physics_hook_runtime (LrgEditorViewportPanel *self)
{
	LrgWorld3DRenderer *runtime;

	if (self->app == NULL || self->app->renderer == NULL)
		return;
	runtime = lrg_editor_scene_renderer_get_runtime_renderer (self->app->renderer);
	if (runtime == NULL || G_OBJECT (runtime) == self->phys_runtime)
		return;
	if (self->phys_runtime != NULL)
		g_clear_signal_handler (&self->phys_overlay_id, self->phys_runtime);
	g_set_weak_pointer (&self->phys_runtime, G_OBJECT (runtime));
	self->phys_overlay_id = g_signal_connect (runtime, "draw-overlay", G_CALLBACK (on_runtime_draw_overlay), self);
}

void
lrg_editor_viewport_panel_set_physics_overlay (LrgEditorViewportPanel *self,
                                               LrgDynamicsDebugFlags   flags)
{
	static const gchar * const nicks[] = {
		"shapes", "aabbs", "contacts", "joints", "sleeping", "com", "velocity", "broadphase"
	};
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));

	self->physics_flags = flags & LRG_DYNAMICS_DEBUG_ALL;
	for (i = 0; i < G_N_ELEMENTS (nicks); i++)
	{
		g_autofree gchar *name = g_strconcat ("physics-", nicks[i], NULL);

		sync_anim_action (self, name, (self->physics_flags & (1u << i)) != 0);
	}
	mark_dirty (self);
	if (self->overlay != NULL)
		lrg_widget_queue_layout (LRG_WIDGET (self->overlay));
}

LrgDynamicsDebugFlags
lrg_editor_viewport_panel_get_physics_overlay (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), LRG_DYNAMICS_DEBUG_NONE);
	return self->physics_flags;
}

LrgDebugDraw *
lrg_editor_viewport_panel_get_physics_debug_draw (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), NULL);
	return self->phys_debug;
}

guint
lrg_editor_viewport_panel_get_physics_painted (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), 0);
	return self->phys_painted;
}

LrgEditorPhysicsHandle
lrg_editor_viewport_panel_get_physics_handle (LrgEditorViewportPanel *self,
                                              guint                  *index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), LRG_EDITOR_PHYSICS_HANDLE_NONE);

	if (self->phys_handle != LRG_EDITOR_PHYSICS_HANDLE_NONE)
	{
		if (index != NULL)
			*index = self->phys_index;
		return self->phys_handle;
	}
	if (index != NULL)
		*index = self->phys_hot_index;
	return self->phys_hot;
}

static void
on_pointer_press (LrgViewport       *viewport,
                  gint               button,
                  gfloat             u,
                  gfloat             v,
                  LrgInputModifiers  mods,
                  gpointer           user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;
	LrgEditorView           view;

	self->mods = mods;
	self->pointer_u = u;
	self->pointer_v = v;
	self->hovered = TRUE;
	g_object_set_data (G_OBJECT (app), NAV_OWNER_KEY, self);
	activate (self);
	take_focus (self);

	/* Any other button cancels a volume or collider resize */
	if (self->vol_handle != LRG_EDITOR_VOLUME_HANDLE_NONE)
	{
		if (button != (gint) GRL_MOUSE_BUTTON_LEFT)
			volume_finish (self, FALSE);
		return;
	}
	if (self->phys_handle != LRG_EDITOR_PHYSICS_HANDLE_NONE)
	{
		if (button != (gint) GRL_MOUSE_BUTTON_LEFT)
			physics_finish (self, FALSE);
		return;
	}

	/* A right click (or any other button) cancels a gizmo drag */
	if (self->gizmo_drag)
	{
		if (button != (gint) GRL_MOUSE_BUTTON_LEFT)
		{
			lrg_editor_gizmo_cancel_drag (app->gizmo);
			self->gizmo_drag = FALSE;
			mark_dirty (self);
			editor_app_set_status (app, "Transform cancelled");
		}
		return;
	}
	if (self->tile_drag)
		return;

	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && tile_press (self, mods))
		return;
	if (lrg_editor_viewport_nav_pointer_press (app->nav, (GrlMouseButton) button, u, v, mods))
		return;
	if (button != (gint) GRL_MOUSE_BUTTON_LEFT || (mods & LRG_INPUT_MODIFIER_ALT))
		return;

	/* Volume handles sit on the edges of lights, decals and probes and
	 * win over the transform gizmo at the centre */
	if (volume_press (self, u, v))
		return;
	if (physics_press (self, u, v))
		return;
	current_view (self, &view);
	if (lrg_editor_gizmo_begin_drag (app->gizmo, &view, u, v))
		self->gizmo_drag = TRUE;
	else
		lrg_editor_viewport_nav_press_empty (app->nav, u, v);
}

static void
on_pointer_move (LrgViewport *viewport,
                 gfloat       u,
                 gfloat       v,
                 gfloat       dx,
                 gfloat       dy,
                 gpointer     user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;
	LrgEditorView           view;
	LrgNode                *node;
	gboolean                idle;

	self->pointer_u = u;
	self->pointer_v = v;
	self->hovered = TRUE;
	idle = lrg_editor_viewport_nav_get_mode (app->nav) == LRG_EDITOR_NAV_MODE_NONE && !self->gizmo_drag &&
	       !self->tile_drag && self->vol_handle == LRG_EDITOR_VOLUME_HANDLE_NONE &&
	       self->phys_handle == LRG_EDITOR_PHYSICS_HANDLE_NONE;

	/* Hovering with no gesture running makes this the active viewport,
	 * so shortcuts and the keyboard act where the pointer is */
	if (idle && !is_active (self) && app->last_input.buttons_down == 0)
		activate (self);
	if (!is_active (self))
		return;

	if (lrg_editor_viewport_nav_pointer_move (app->nav, u, v, dx, dy, self->mods))
		return;

	node = tile_target (self);
	if (node != NULL)
	{
		update_tile_hover (self, node);
		if (self->tile_drag && lrg_editor_tile_painter_is_stroking (app->tile_painter))
		{
			lrg_editor_tile_painter_stroke_to (app->tile_painter, app->tile_hover_x, app->tile_hover_y);
			mark_dirty (self);
		}
	}
	else
		app->tile_hover_valid = FALSE;

	if (self->vol_handle != LRG_EDITOR_VOLUME_HANDLE_NONE)
	{
		volume_drag (self, u, v);
		return;
	}
	if (self->phys_handle != LRG_EDITOR_PHYSICS_HANDLE_NONE)
	{
		physics_drag (self, u, v);
		return;
	}
	current_view (self, &view);
	if (self->gizmo_drag)
		lrg_editor_gizmo_drag (app->gizmo, &view, u, v, (self->mods & LRG_INPUT_MODIFIER_CTRL) != 0);
	else if (lrg_editor_viewport_nav_get_mode (app->nav) == LRG_EDITOR_NAV_MODE_NONE && !self->tile_drag)
	{
		LrgNode              *vnode = volume_node (self);
		LrgEditorVolumeHandle hot = (vnode != NULL) ? lrg_editor_volume_hit_test (&view, vnode, u, v)
		                                            : LRG_EDITOR_VOLUME_HANDLE_NONE;

		if (hot != self->vol_hot)
		{
			self->vol_hot = hot;
			mark_dirty (self);
		}
		{
			LrgNode               *pnode = physics_node (self);
			guint                  pindex = 0;
			LrgEditorPhysicsHandle phot = (pnode != NULL) ? lrg_editor_physics_hit_test (&view, pnode, u, v, &pindex)
			                                              : LRG_EDITOR_PHYSICS_HANDLE_NONE;

			if (phot != self->phys_hot || pindex != self->phys_hot_index)
			{
				self->phys_hot = phot;
				self->phys_hot_index = pindex;
				mark_dirty (self);
			}
		}
		lrg_editor_gizmo_update_hover (app->gizmo, &view, u, v);
	}
}

static void
on_pointer_release (LrgViewport       *viewport,
                    gint               button,
                    gfloat             u,
                    gfloat             v,
                    LrgInputModifiers  mods,
                    gpointer           user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;

	self->mods = mods;
	self->pointer_u = u;
	self->pointer_v = v;

	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && self->tile_drag)
	{
		LrgNode *node = tile_target (self);

		/* The release carries the last motion: paint up to it first */
		if (node != NULL)
		{
			update_tile_hover (self, node);
			if (lrg_editor_tile_painter_is_stroking (app->tile_painter))
				lrg_editor_tile_painter_stroke_to (app->tile_painter, app->tile_hover_x, app->tile_hover_y);
		}
		tile_finish (self, node, TRUE);
		return;
	}
	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && self->vol_handle != LRG_EDITOR_VOLUME_HANDLE_NONE)
	{
		volume_drag (self, u, v);
		volume_finish (self, TRUE);
		return;
	}
	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && self->phys_handle != LRG_EDITOR_PHYSICS_HANDLE_NONE)
	{
		physics_drag (self, u, v);
		physics_finish (self, TRUE);
		return;
	}
	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && self->gizmo_drag)
	{
		LrgEditorView view;

		current_view (self, &view);
		lrg_editor_gizmo_drag (app->gizmo, &view, u, v, (mods & LRG_INPUT_MODIFIER_CTRL) != 0);
		lrg_editor_gizmo_end_drag (app->gizmo);
		self->gizmo_drag = FALSE;
		mark_dirty (self);
		return;
	}
	lrg_editor_viewport_nav_pointer_release (app->nav, (GrlMouseButton) button, u, v, mods);
}

static void
on_pointer_scroll (LrgViewport *viewport,
                   gfloat       u,
                   gfloat       v,
                   gfloat       scroll_x,
                   gfloat       scroll_y,
                   gpointer     user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	/* The wheel zooms the viewport under the pointer; the scroll signal
	 * has no modifiers, so the app's copy of this frame's input does */
	activate (self);
	lrg_editor_viewport_nav_scroll (self->app->nav, scroll_x, scroll_y, self->app->last_input.modifiers);
}

static gboolean
on_key (LrgViewport       *viewport,
        GrlKey             key,
        LrgInputModifiers  mods,
        gpointer           user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;

	self->mods = mods;
	if (key == GRL_KEY_ESCAPE)
	{
		/* A volume resize, then a gizmo drag own Escape */
		if (self->vol_handle != LRG_EDITOR_VOLUME_HANDLE_NONE)
		{
			volume_finish (self, FALSE);
			return TRUE;
		}
		if (self->phys_handle != LRG_EDITOR_PHYSICS_HANDLE_NONE)
		{
			physics_finish (self, FALSE);
			return TRUE;
		}
		if (self->gizmo_drag)
		{
			lrg_editor_gizmo_cancel_drag (app->gizmo);
			self->gizmo_drag = FALSE;
			mark_dirty (self);
			editor_app_set_status (app, "Transform cancelled");
			return TRUE;
		}
		/* Escape cancels a rectangle, otherwise leaves paint mode */
		if (lrg_editor_view_state_get_tile_mode (app->view))
		{
			gboolean was_rect = self->tile_rect;

			tile_finish (self, NULL, FALSE);
			if (!was_rect)
			{
				lrg_editor_view_state_set_tile_mode (app->view, FALSE);
				app->tile_hover_valid = FALSE;
				editor_app_set_status (app, "Tile painting off");
			}
			return TRUE;
		}
	}
	return lrg_editor_viewport_nav_key_down (app->nav, key, mods);
}

/* Key releases bubble here from the focused viewport; focus loss drops
 * held keys so the camera cannot keep flying. */
static gboolean
lrg_editor_viewport_panel_handle_event (LrgWidget        *widget,
                                        const LrgUIEvent *event)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (widget);

	if (self->app == NULL)
		return FALSE;
	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_KEY_UP:
		return lrg_editor_viewport_nav_key_up (self->app->nav, lrg_ui_event_get_key (event),
		                                       lrg_ui_event_get_modifiers (event));
	case LRG_UI_EVENT_FOCUS_OUT:
		lrg_editor_viewport_nav_focus_lost (self->app->nav);
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hovered = FALSE;
		if (is_active (self))
			self->app->tile_hover_valid = FALSE;
		return FALSE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * Drops
 * ========================================================================== */

/* World point on the ground (y = 0, or z = 0 in 2D) under a pixel. */
static void
ground_point (LrgEditorViewportPanel *self,
              gfloat                  u,
              gfloat                  v,
              gfloat                 *out)
{
	LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (self->app->view, (gint) self->index);
	LrgEditorView        view;
	gfloat               o[3], d[3], t;
	const gfloat         p0[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat         n3d[3] = { 0.0f, 1.0f, 0.0f };
	const gfloat         n2d[3] = { 0.0f, 0.0f, 1.0f };

	current_view (self, &view);
	lrg_editor_view_get_ray (&view, u, v, o, d);
	if (!lrg_editor_ray_plane (o, d, p0, cam->view_2d ? n2d : n3d, &t) || t < 0.0f)
		t = 10.0f;   /* parallel to the ground: a point in front of the camera */
	out[0] = o[0] + d[0] * t;
	out[1] = o[1] + d[1] * t;
	out[2] = o[2] + d[2] * t;
}

LrgNode *
lrg_editor_viewport_panel_drop_asset (LrgEditorViewportPanel *self,
                                      const gchar            *path,
                                      gfloat                  u,
                                      gfloat                  v)
{
	LrgEditorApp      *app;
	LrgAssetType       type;
	g_autofree gchar  *base = NULL;
	gfloat             at[3];
	LrgNode           *node = NULL;
	g_autoptr(GError)  error = NULL;
	gdouble            snap_t = 0.0;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	app = self->app;
	type = lrg_asset_database_classify (path);
	base = g_path_get_basename (path);
	activate (self);
	ground_point (self, u, v, at);
	lrg_editor_get_snap (app->editor, &snap_t, NULL, NULL);
	if (lrg_editor_get_snap_enabled (app->editor))
	{
		at[0] = lrg_editor_snap_value (at[0], (gfloat) snap_t);
		at[1] = lrg_editor_snap_value (at[1], (gfloat) snap_t);
		at[2] = lrg_editor_snap_value (at[2], (gfloat) snap_t);
	}

	/* A material lands on the mesh under the pointer (ED1's undoable
	 * assignment), never as a new node */
	if (g_str_has_suffix (path, ".rmat"))
	{
		LrgEditorView view;
		gfloat        o[3], d[3];
		LrgNode      *hit;

		current_view (self, &view);
		lrg_editor_view_get_ray (&view, u, v, o, d);
		hit = lrg_editor_pick (lrg_editor_get_level (app->editor), o, d, FALSE, NULL);
		if (hit == NULL)
		{
			editor_app_set_status (app, "Drop %s onto a mesh to assign it", base);
			return NULL;
		}
		if (!lrg_editor_material_editor_assign_to_node (app, lrg_node_get_guid (hit), path, &error))
		{
			editor_app_notify (app, EDITOR_NOTIFY_WARNING, "%s: %s", base, error->message);
			return NULL;
		}
		lrg_editor_select (app->editor, hit, FALSE);
		editor_app_set_status (app, "Assigned %s to %s", base, lrg_node_get_name (hit));
		mark_dirty (self);
		return hit;
	}

	/* One undo step for the whole placement */
	lrg_editor_begin_group (app->editor, "Place Asset");
	switch (type)
	{
	case LRG_ASSET_TYPE_PREFAB:
		{
			g_autofree gchar *abs = editor_app_project_path (app, path);

			/* A linked instance (overrides tracked, syncs with the prefab) */
			node = lrg_editor_instantiate_prefab (app->editor, abs, NULL, &error);
		}
		break;
	case LRG_ASSET_TYPE_MODEL:
	case LRG_ASSET_TYPE_TEXTURE:
	case LRG_ASSET_TYPE_CAD:
		{
			LrgNodeVisualKind  kind = (type == LRG_ASSET_TYPE_MODEL) ? LRG_NODE_VISUAL_MESH_ASSET
			                        : (type == LRG_ASSET_TYPE_CAD) ? LRG_NODE_VISUAL_CAD_PART
			                                                        : LRG_NODE_VISUAL_SPRITE;
			g_autoptr(LrgNode) before = NULL;
			g_autofree gchar  *stem = g_strdup (base);
			gchar             *dot = strrchr (stem, '.');

			if (dot != NULL)
				*dot = '\0';
			node = editor_app_create_node (app, stem, kind, 0, NULL);
			if (node != NULL)
			{
				before = lrg_editor_snapshot_node (node);
				lrg_node_visual_set_asset (lrg_node_get_visual (node), path);
				lrg_editor_commit_node_state (app->editor, node, before, "Set Asset", FALSE);
			}
		}
		break;
	case LRG_ASSET_TYPE_SCRIPT:
		{
			/* Scripts attach to the node under the pointer */
			LrgEditorView view;
			gfloat        o[3], d[3];
			LrgNode      *hit;

			current_view (self, &view);
			lrg_editor_view_get_ray (&view, u, v, o, d);
			hit = lrg_editor_pick (lrg_editor_get_level (app->editor), o, d, FALSE, NULL);
			if (hit != NULL)
			{
				g_autoptr(LrgScriptBinding) b = lrg_script_binding_new (editor_script_language_for_path (path), path);

				lrg_editor_add_script (app->editor, hit, b);
				lrg_editor_select (app->editor, hit, FALSE);
				editor_app_set_status (app, "Attached %s to %s", base, lrg_node_get_name (hit));
				node = hit;
			}
			else
				editor_app_set_status (app, "Drop %s onto a node to attach it", base);
		}
		break;
	case LRG_ASSET_TYPE_LEVEL:
		editor_app_request (app, EDITOR_PENDING_OPEN_LEVEL, path);
		break;
	default:
		editor_app_set_status (app, "%s cannot be placed in the scene", base);
		break;
	}

	if (node != NULL && type != LRG_ASSET_TYPE_SCRIPT)
	{
		lrg_editor_transform_node (app->editor, node, at, NULL, NULL, FALSE);
		lrg_editor_select (app->editor, node, FALSE);
		editor_app_set_status (app, "Placed %s at %.2f, %.2f, %.2f", base, (gdouble) at[0], (gdouble) at[1],
		                       (gdouble) at[2]);
	}
	lrg_editor_end_group (app->editor);

	if (error != NULL)
		editor_app_notify (app, EDITOR_NOTIFY_ERROR, "%s: %s", base, error->message);
	mark_dirty (self);
	return node;
}

/* Canvas point -> viewport-local content pixels. */
static void
canvas_to_content (LrgEditorViewportPanel *self,
                   gfloat                  x,
                   gfloat                  y,
                   gfloat                 *u,
                   gfloat                 *v)
{
	lrg_viewport_widget_to_texture (self->viewport, x - lrg_widget_get_world_x (LRG_WIDGET (self->viewport)),
	                                y - lrg_widget_get_world_y (LRG_WIDGET (self->viewport)), u, v);
}

static void
drop_update (LrgEditorViewportPanel *self,
             LrgDragPayload         *payload,
             gfloat                  x,
             gfloat                  y)
{
	self->drop_hover = TRUE;
	self->drop_x = x;
	self->drop_y = y;
	if (self->drop_label == NULL)
	{
		const gchar *type = lrg_drag_payload_get_content_type (payload);

		if (g_strcmp0 (type, "application/x-lrg-asset") == 0 && lrg_drag_payload_get_string (payload) != NULL)
		{
			const gchar      *path = lrg_drag_payload_get_string (payload);
			g_autofree gchar *base = g_path_get_basename (path);
			LrgAssetType      kind = lrg_asset_database_classify (path);

			self->drop_label = g_strdup_printf ("%s %s", g_str_has_suffix (path, ".rmat") ? "Assign" :
			                                    kind == LRG_ASSET_TYPE_SCRIPT ? "Attach" :
			                                    kind == LRG_ASSET_TYPE_PREFAB ? "Instance" :
			                                    kind == LRG_ASSET_TYPE_LEVEL ? "Open" : "Place", base);
		}
		else
			self->drop_label = g_strdup ("Frame node");
	}
	activate (self);
}

static void
on_drop_enter (LrgDropTarget  *target,
               LrgDragPayload *payload,
               gfloat          x,
               gfloat          y,
               gpointer        user_data)
{
	drop_update (LRG_EDITOR_VIEWPORT_PANEL (user_data), payload, x, y);
}

static LrgDragAction
on_drop_motion (LrgDropTarget  *target,
                LrgDragPayload *payload,
                gfloat          x,
                gfloat          y,
                gpointer        user_data)
{
	drop_update (LRG_EDITOR_VIEWPORT_PANEL (user_data), payload, x, y);
	return g_strcmp0 (lrg_drag_payload_get_content_type (payload), "application/x-lrg-node") == 0
	       ? LRG_DRAG_ACTION_LINK : LRG_DRAG_ACTION_COPY;
}

static void
on_drop_leave (LrgDropTarget *target,
               gpointer       user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	self->drop_hover = FALSE;
	g_clear_pointer (&self->drop_label, g_free);
}

static gboolean
on_drop (LrgDropTarget  *target,
         LrgDragPayload *payload,
         gfloat          x,
         gfloat          y,
         gpointer        user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	const gchar            *type = lrg_drag_payload_get_content_type (payload);
	gfloat                  u = 0.0f, v = 0.0f;

	canvas_to_content (self, x, y, &u, &v);
	if (g_strcmp0 (type, "application/x-lrg-asset") == 0)
	{
		g_autofree gchar *path = g_strdup (lrg_drag_payload_get_string (payload));

		if (path == NULL)
			return FALSE;
		lrg_editor_viewport_panel_drop_asset (self, path, u, v);
		return TRUE;
	}

	/* A node dropped from the hierarchy: select and frame it here */
	if (g_strcmp0 (type, "application/x-lrg-node") == 0 && LRG_IS_NODE (lrg_drag_payload_get_object (payload)))
	{
		LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (self->app->view, (gint) self->index);
		LrgNode             *node = LRG_NODE (lrg_drag_payload_get_object (payload));
		g_autoptr(GPtrArray) nodes = g_ptr_array_new ();

		g_ptr_array_add (nodes, node);
		lrg_editor_select (self->app->editor, node, FALSE);
		if (lrg_editor_view_camera_frame_nodes (cam, nodes))
			lrg_editor_view_state_camera_changed (self->app->view, (gint) self->index);
		return TRUE;
	}
	return FALSE;
}

/* ==========================================================================
 * Header: view preset, render mode, overlays, preview and maximise
 * ========================================================================== */

/* The preset that describes a camera. */
static ViewPreset
camera_preset (const LrgEditorViewCamera *cam)
{
	if (cam->view_2d)
		return PRESET_2D;
	switch (cam->axis_view)
	{
	case LRG_EDITOR_AXIS_VIEW_TOP:    return PRESET_TOP;
	case LRG_EDITOR_AXIS_VIEW_BOTTOM: return PRESET_BOTTOM;
	case LRG_EDITOR_AXIS_VIEW_FRONT:  return PRESET_FRONT;
	case LRG_EDITOR_AXIS_VIEW_BACK:   return PRESET_BACK;
	case LRG_EDITOR_AXIS_VIEW_RIGHT:  return PRESET_RIGHT;
	case LRG_EDITOR_AXIS_VIEW_LEFT:   return PRESET_LEFT;
	default:
		return cam->view.orthographic ? PRESET_ORTHOGRAPHIC : PRESET_PERSPECTIVE;
	}
}

/* Applies a view preset to this panel's camera. */
static void
apply_preset (LrgEditorViewportPanel *self,
              ViewPreset              preset)
{
	LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (self->app->view, (gint) self->index);
	gfloat               fov = (gfloat) lrg_editor_view_state_get_fov (self->app->view);
	static const LrgEditorAxisView axes[N_PRESETS] = {
		LRG_EDITOR_AXIS_VIEW_NONE, LRG_EDITOR_AXIS_VIEW_NONE, LRG_EDITOR_AXIS_VIEW_TOP,
		LRG_EDITOR_AXIS_VIEW_BOTTOM, LRG_EDITOR_AXIS_VIEW_FRONT, LRG_EDITOR_AXIS_VIEW_BACK,
		LRG_EDITOR_AXIS_VIEW_RIGHT, LRG_EDITOR_AXIS_VIEW_LEFT, LRG_EDITOR_AXIS_VIEW_NONE
	};

	if (preset == PRESET_2D)
	{
		if (!cam->view_2d)
			lrg_editor_view_camera_set_2d (cam, TRUE, fov);
	}
	else
	{
		if (cam->view_2d)
			lrg_editor_view_camera_set_2d (cam, FALSE, fov);
		if (axes[preset] != LRG_EDITOR_AXIS_VIEW_NONE)
			lrg_editor_view_camera_set_axis_view (cam, axes[preset]);
		else
		{
			/* Free views: leave the axis, then pick the projection */
			cam->axis_view = LRG_EDITOR_AXIS_VIEW_NONE;
			if (cam->view.orthographic != (preset == PRESET_ORTHOGRAPHIC))
				lrg_editor_view_camera_toggle_ortho (cam);
		}
	}
	lrg_editor_view_state_camera_changed (self->app->view, (gint) self->index);
}

static void
on_preset_changed (LrgComboBox *combo,
                   gpointer     user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	gint                    i = lrg_combo_box_get_selected (combo);

	if (self->syncing || i < 0 || i >= N_PRESETS)
		return;
	activate (self);
	apply_preset (self, (ViewPreset) i);
}

/* How many render modes LrgEditorRenderMode has. */
static guint
n_render_modes (void)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EDITOR_RENDER_MODE);

	return klass->n_values;
}

static void
on_mode_changed (LrgComboBox *combo,
                 gpointer     user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	gint                    i = lrg_combo_box_get_selected (combo);

	if (self->syncing || i < 0 || i >= (gint) n_render_modes ())
		return;
	activate (self);
	lrg_editor_view_state_set_render_mode (self->app->view, (gint) self->index, (LrgEditorRenderMode) i);
}

/* vp.overlay-<nick>: a stateful boolean per overlay flag. */
static void
on_overlay_action (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorViewOverlays   flag = (LrgEditorViewOverlays) GPOINTER_TO_UINT (
		g_object_get_data (G_OBJECT (action), "lrg-overlay-flag"));

	lrg_editor_view_state_toggle_overlay (self->app->view, (gint) self->index, flag);
}

/* vp.anim-bones / vp.anim-ik */
static void
on_anim_overlay_action (GSimpleAction *action,
                        GVariant      *parameter,
                        gpointer       user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	if (g_strcmp0 (g_action_get_name (G_ACTION (action)), "anim-bones") == 0)
		lrg_editor_viewport_panel_set_show_bones (self, !self->show_bones);
	else
		lrg_editor_viewport_panel_set_show_ik_targets (self, !self->show_ik);
}

/* vp.physics-<flag>: toggles one Physics overlay flag. */
static void
on_physics_overlay_action (GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	guint                   flag = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (action), "lrg-physics-flag"));

	lrg_editor_viewport_panel_set_physics_overlay (self, self->physics_flags ^ flag);
}

static void
open_overlay_menu (LrgEditorViewportPanel *self)
{
	activate (self);
	if (self->overlay_menu == NULL)
	{
		g_autoptr(GMenu) menu = g_menu_new ();
		g_autoptr(GMenu) view = g_menu_new ();
		g_autoptr(GMenu) render = g_menu_new ();
		guint            i;

		for (i = 0; i < G_N_ELEMENTS (overlay_items); i++)
		{
			g_autofree gchar *detailed = g_strconcat ("vp.overlay-", overlay_items[i].nick, NULL);

			g_menu_append (i < 5 ? view : render, overlay_items[i].label, detailed);
		}
		g_menu_append_section (menu, "Overlays", G_MENU_MODEL (view));
		g_menu_append_section (menu, "Rendering", G_MENU_MODEL (render));
		{
			g_autoptr(GMenu) anim = g_menu_new ();

			g_menu_append (anim, "Bones", "vp.anim-bones");
			g_menu_append (anim, "IK Targets", "vp.anim-ik");
			g_menu_append_section (menu, "Animation", G_MENU_MODEL (anim));
		}
		{
			g_autoptr(GMenu) phys = g_menu_new ();

			g_menu_append (phys, "Collision Shapes", "vp.physics-shapes");
			g_menu_append (phys, "Joints", "vp.physics-joints");
			g_menu_append (phys, "Bounding Boxes", "vp.physics-aabbs");
			g_menu_append (phys, "Contacts (playing)", "vp.physics-contacts");
			g_menu_append (phys, "Sleeping Bodies (playing)", "vp.physics-sleeping");
			g_menu_append (phys, "Centres of Mass", "vp.physics-com");
			g_menu_append (phys, "Velocities (playing)", "vp.physics-velocity");
			g_menu_append (phys, "Broadphase Tree (playing)", "vp.physics-broadphase");
			g_menu_append_section (menu, "Physics", G_MENU_MODEL (phys));
		}
		self->overlay_menu = g_object_ref_sink (lrg_menu_new_from_model (G_MENU_MODEL (menu)));
	}
	lrg_menu_popup_at_widget (self->overlay_menu, LRG_WIDGET (self->overlay_button), LRG_POPUP_PLACEMENT_BELOW);
}

/* ==========================================================================
 * Render layers
 * ========================================================================== */

static gchar *
layer_label (guint layer)
{
	return layer < G_N_ELEMENTS (layer_names) ? g_strdup_printf ("%u  %s", layer, layer_names[layer])
	                                          : g_strdup_printf ("%u  Layer %u", layer, layer);
}

/* vp.layer-<n>: toggles one layer of this viewport's cull mask. */
static void
on_layer_action (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	guint                   layer = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (action), "lrg-layer"));

	lrg_editor_viewport_panel_set_layer_mask (self, self->cull_mask ^ LRG_RENDER_LAYERS_BIT (layer));
}

static void
on_layers_all (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	lrg_editor_viewport_panel_set_layer_mask (LRG_EDITOR_VIEWPORT_PANEL (user_data), LRG_RENDER_LAYERS_MASK_ALL);
}

static void
on_layers_none (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
	lrg_editor_viewport_panel_set_layer_mask (LRG_EDITOR_VIEWPORT_PANEL (user_data), LRG_RENDER_LAYERS_MASK_NONE);
}

/* The layers popup: All / None, the named layers, the rest in a
 * submenu. */
static void
open_layers_menu (LrgEditorViewportPanel *self)
{
	activate (self);
	if (self->layers_menu == NULL)
	{
		g_autoptr(GMenu) menu = g_menu_new ();
		g_autoptr(GMenu) quick = g_menu_new ();
		g_autoptr(GMenu) named = g_menu_new ();
		g_autoptr(GMenu) more = g_menu_new ();
		guint            i;

		g_menu_append (quick, "Show All Layers", "vp.layers-all");
		g_menu_append (quick, "Hide All Layers", "vp.layers-none");
		for (i = 0; i < LRG_RENDER_LAYERS_COUNT; i++)
		{
			g_autofree gchar *label = layer_label (i);
			g_autofree gchar *detailed = g_strdup_printf ("vp.layer-%u", i);

			g_menu_append (i < G_N_ELEMENTS (layer_names) ? named : more, label, detailed);
		}
		g_menu_append_section (menu, NULL, G_MENU_MODEL (quick));
		g_menu_append_section (menu, "Render Layers", G_MENU_MODEL (named));
		g_menu_append_submenu (named, "More Layers", G_MENU_MODEL (more));
		self->layers_menu = g_object_ref_sink (lrg_menu_new_from_model (G_MENU_MODEL (menu)));
	}
	lrg_menu_popup_at_widget (self->layers_menu, LRG_WIDGET (self->layers_button), LRG_POPUP_PLACEMENT_BELOW);
}

/* The two icon toggles of the header, as custom widgets: hover/pressed
 * backgrounds from the theme, an accent fill while checked. */
static gboolean
icon_checked (LrgEditorViewportPanel *self,
              IconKind                kind)
{
	LrgEditorViewportSplit *split;

	if (kind == ICON_OVERLAYS)
		return self->overlay_menu != NULL && lrg_menu_get_open (self->overlay_menu);
	if (kind == ICON_LAYERS)
		return (self->layers_menu != NULL && lrg_menu_get_open (self->layers_menu)) ||
		       self->cull_mask != LRG_RENDER_LAYERS_MASK_ALL;
	if (kind == ICON_PREVIEW)
		return self->preview_enabled;
	split = find_split (self);
	return split != NULL && lrg_editor_viewport_split_get_maximized (split) == (gint) self->index;
}

static void
icon_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat    h = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT);

	size->width = MAX (h, 20.0f);
	size->height = MAX (h, 20.0f);
}

static void
icon_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	IconKind                kind = (IconKind) GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "lrg-icon-kind"));
	LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gboolean                checked = icon_checked (self, kind);
	gfloat                  hover = lrg_widget_get_hover_progress (LRG_WIDGET (widget));
	gfloat                  radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlColor                bg;
	GrlColor                ink;
	gfloat                  cx = x + width * 0.5f, cy = y + height * 0.5f, s = MIN (width, height) * 0.3f;

	/* Background: accent while checked, a control tint fading in on hover */
	if (checked)
		bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	else
	{
		bg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
		bg.a = (guint8) ((gfloat) bg.a * hover);
	}
	if (bg.a > 0)
		lrg_paint_list_rect (list, x + 2.0f, y + 2.0f, width - 4.0f, height - 4.0f, radius, &bg);
	ink = *lrg_theme_get_color (theme, checked ? LRG_THEME_COLOR_TEXT_ON_ACCENT : LRG_THEME_COLOR_TEXT);

	if (kind == ICON_OVERLAYS)
	{
		gfloat box = MIN (width, height) * 0.62f;

		lrg_paint_list_icon (list, LRG_ICON_EYE, cx - box * 0.5f, cy - box * 0.5f, box, 0.0f, &ink);
	}
	else if (kind == ICON_LAYERS)
	{
		/* Three stacked diamonds (a layer stack); the top one is
		 * filled, the lower ones are outlines peeking out below */
		gint i;

		for (i = 2; i >= 0; i--)
		{
			gfloat dy = cy - s * 0.45f + (gfloat) i * s * 0.45f;
			gfloat w = s * 1.15f, h = s * 0.55f;

			if (i == 0)
			{
				lrg_paint_list_triangle (list, cx - w, dy, cx, dy - h, cx + w, dy, &ink);
				lrg_paint_list_triangle (list, cx - w, dy, cx + w, dy, cx, dy + h, &ink);
			}
			else
			{
				lrg_paint_list_line (list, cx - w, dy, cx, dy + h, 1.4f, &ink);
				lrg_paint_list_line (list, cx, dy + h, cx + w, dy, 1.4f, &ink);
			}
		}
	}
	else if (kind == ICON_PREVIEW)
	{
		/* A camera: body plus lens triangle */
		lrg_paint_list_rect (list, cx - s * 1.1f, cy - s * 0.7f, s * 1.5f, s * 1.4f, 2.0f, &ink);
		lrg_paint_list_triangle (list, cx + s * 0.45f, cy, cx + s * 1.15f, cy - s * 0.65f, cx + s * 1.15f,
		                         cy + s * 0.65f, &ink);
	}
	else
	{
		/* Four corner brackets: out (maximise) or in (restore) */
		gfloat a = checked ? s * 0.45f : s;
		gfloat b = checked ? s : s * 0.45f;
		gint   i;

		for (i = 0; i < 4; i++)
		{
			gfloat sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
			gfloat px = cx + sx * (checked ? a : s), py = cy + sy * (checked ? a : s);

			lrg_paint_list_line (list, px, py, px - sx * (b - a) * (checked ? -1.0f : 1.0f), py, 1.6f, &ink);
			lrg_paint_list_line (list, px, py, px, py - sy * (b - a) * (checked ? -1.0f : 1.0f), 1.6f, &ink);
		}
	}
}

static void
toggle_maximized (LrgEditorViewportPanel *self)
{
	LrgEditorViewportSplit *split = find_split (self);

	if (split == NULL)
		return;
	lrg_editor_viewport_split_set_maximized (split,
	                                         lrg_editor_viewport_split_get_maximized (split) == (gint) self->index
	                                         ? -1 : (gint) self->index);
}

static gboolean
icon_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	IconKind                kind = (IconKind) GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "lrg-icon-kind"));

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 ||
		    !lrg_widget_contains_point (LRG_WIDGET (widget), lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)))
			return FALSE;
		activate (self);
		if (kind == ICON_OVERLAYS)
			open_overlay_menu (self);
		else if (kind == ICON_LAYERS)
			open_layers_menu (self);
		else if (kind == ICON_PREVIEW)
			lrg_editor_viewport_panel_set_preview_enabled (self, !self->preview_enabled);
		else
			toggle_maximized (self);
		return TRUE;
	default:
		return FALSE;
	}
}

static LrgCustomWidget *
add_icon_button (LrgEditorViewportPanel *self,
                 IconKind                kind,
                 const gchar            *name)
{
	LrgCustomWidget      *button = lrg_custom_widget_new ();
	LrgCustomWidgetFuncs  funcs;
	g_autofree gchar     *full = g_strdup_printf ("vp%u-%s", self->index, name);

	memset (&funcs, 0, sizeof funcs);
	funcs.measure = icon_measure;
	funcs.paint = icon_paint;
	funcs.event = icon_event;
	g_object_set_data (G_OBJECT (button), "lrg-icon-kind", GINT_TO_POINTER (kind));
	lrg_custom_widget_set_funcs (button, &funcs, self, NULL);
	lrg_widget_set_name (LRG_WIDGET (button), full);
	lrg_widget_set_valign (LRG_WIDGET (button), LRG_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (self->tools), LRG_WIDGET (button));
	g_object_unref (button);
	return button;
}

static LrgComboBox *
add_combo (LrgEditorViewportPanel *self,
           const gchar * const    *labels,
           guint                   n,
           const gchar            *name,
           gfloat                  max_width,
           GCallback               callback)
{
	LrgComboBox      *combo = lrg_combo_box_new ();
	g_autofree gchar *full = g_strdup_printf ("vp%u-%s", self->index, name);
	guint             i;

	for (i = 0; i < n; i++)
		lrg_combo_box_append (combo, labels[i]);
	lrg_widget_set_name (LRG_WIDGET (combo), full);
	lrg_widget_add_class (LRG_WIDGET (combo), "flat");
	lrg_widget_set_max_size (LRG_WIDGET (combo), max_width, 0.0f);
	lrg_widget_set_valign (LRG_WIDGET (combo), LRG_ALIGN_CENTER);
	g_signal_connect (combo, "changed", callback, self);
	lrg_container_add_child (LRG_CONTAINER (self->header), LRG_WIDGET (combo));
	g_object_unref (combo);
	return combo;
}

static void
build_header (LrgEditorViewportPanel *self)
{
	g_autofree gchar    *name = g_strdup_printf ("vp%u-header", self->index);
	g_autofree gchar    *tname = g_strdup_printf ("vp%u-tools", self->index);
	const LrgInsets      ins = { 6.0f, 3.0f, 6.0f, 3.0f };
	guint                i;

	self->header = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (self->header), name);
	lrg_container_set_spacing (LRG_CONTAINER (self->header), 4.0f);
	lrg_widget_set_margin (LRG_WIDGET (self->header), &ins);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->header));
	g_object_unref (self->header);

	self->preset_combo = add_combo (self, preset_labels, N_PRESETS, "view-preset", 138.0f,
	                                G_CALLBACK (on_preset_changed));
	{
		/* every LrgEditorRenderMode value, the debug views included */
		g_autoptr(GEnumClass) modes = g_type_class_ref (LRG_TYPE_EDITOR_RENDER_MODE);
		g_autofree const gchar **labels = g_new0 (const gchar *, modes->n_values + 1);

		for (i = 0; i < modes->n_values; i++)
			labels[i] = (i < G_N_ELEMENTS (mode_labels)) ? mode_labels[i] : modes->values[i].value_nick;
		self->mode_combo = add_combo (self, (const gchar * const *) labels, modes->n_values, "render-mode", 150.0f,
		                              G_CALLBACK (on_mode_changed));
	}

	/* The icon toggles, right aligned in their own box */
	self->tools = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tools), tname);
	lrg_container_set_spacing (LRG_CONTAINER (self->tools), 2.0f);
	lrg_widget_set_margin (LRG_WIDGET (self->tools), &ins);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->tools));
	g_object_unref (self->tools);
	self->overlay_button = add_icon_button (self, ICON_OVERLAYS, "overlays");
	self->layers_button = add_icon_button (self, ICON_LAYERS, "layers");
	self->preview_button = add_icon_button (self, ICON_PREVIEW, "preview-toggle");
	self->max_button = add_icon_button (self, ICON_MAXIMIZE, "maximize");

	/* The per-panel "vp" action group: overlay toggles for the menu */
	self->actions = g_simple_action_group_new ();
	for (i = 0; i < G_N_ELEMENTS (overlay_items); i++)
	{
		g_autofree gchar         *aname = g_strconcat ("overlay-", overlay_items[i].nick, NULL);
		g_autoptr(GSimpleAction)  action = g_simple_action_new_stateful (aname, NULL,
		                                                                  g_variant_new_boolean (FALSE));

		g_object_set_data (G_OBJECT (action), "lrg-overlay-flag", GUINT_TO_POINTER (overlay_items[i].flag));
		g_signal_connect (action, "activate", G_CALLBACK (on_overlay_action), self);
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (action));
	}
	/* Render layers: one stateful toggle per layer, plus All / None */
	for (i = 0; i < LRG_RENDER_LAYERS_COUNT; i++)
	{
		g_autofree gchar         *aname = g_strdup_printf ("layer-%u", i);
		g_autoptr(GSimpleAction)  action = g_simple_action_new_stateful (aname, NULL,
		                                                                  g_variant_new_boolean (TRUE));

		g_object_set_data (G_OBJECT (action), "lrg-layer", GUINT_TO_POINTER (i));
		g_signal_connect (action, "activate", G_CALLBACK (on_layer_action), self);
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (action));
	}
	{
		g_autoptr(GSimpleAction) all = g_simple_action_new ("layers-all", NULL);
		g_autoptr(GSimpleAction) none = g_simple_action_new ("layers-none", NULL);

		g_signal_connect (all, "activate", G_CALLBACK (on_layers_all), self);
		g_signal_connect (none, "activate", G_CALLBACK (on_layers_none), self);
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (all));
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (none));
	}
	/* Animation overlays: per panel, not saved */
	{
		g_autoptr(GSimpleAction) bones = g_simple_action_new_stateful ("anim-bones", NULL, g_variant_new_boolean (FALSE));
		g_autoptr(GSimpleAction) ik = g_simple_action_new_stateful ("anim-ik", NULL, g_variant_new_boolean (TRUE));

		g_signal_connect (bones, "activate", G_CALLBACK (on_anim_overlay_action), self);
		g_signal_connect (ik, "activate", G_CALLBACK (on_anim_overlay_action), self);
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (bones));
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (ik));
	}
	/* Physics overlay flags: one toggle per LrgDynamicsDebugFlags bit */
	{
		static const gchar * const nicks[] = {
			"shapes", "aabbs", "contacts", "joints", "sleeping", "com", "velocity", "broadphase"
		};

		for (i = 0; i < G_N_ELEMENTS (nicks); i++)
		{
			g_autofree gchar         *aname = g_strconcat ("physics-", nicks[i], NULL);
			g_autoptr(GSimpleAction)  action = g_simple_action_new_stateful (aname, NULL,
			                                                                  g_variant_new_boolean (FALSE));

			g_object_set_data (G_OBJECT (action), "lrg-physics-flag", GUINT_TO_POINTER (1u << i));
			g_signal_connect (action, "activate", G_CALLBACK (on_physics_overlay_action), self);
			g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (action));
		}
	}
	lrg_widget_insert_action_group (LRG_WIDGET (self), "vp", G_ACTION_GROUP (self->actions));
}

/* View state -> header widgets and action states. */
static void
sync_header (LrgEditorViewportPanel *self)
{
	LrgEditorViewCamera   *cam;
	LrgEditorViewOverlays  overlays;
	guint                  i;

	if (self->app == NULL)
		return;
	cam = lrg_editor_view_state_get_camera (self->app->view, (gint) self->index);
	overlays = lrg_editor_view_state_get_overlays (self->app->view, (gint) self->index);

	self->syncing = TRUE;
	if (lrg_combo_box_get_selected (self->preset_combo) != (gint) camera_preset (cam))
		lrg_combo_box_set_selected (self->preset_combo, (gint) camera_preset (cam));
	if (lrg_combo_box_get_selected (self->mode_combo) !=
	    (gint) lrg_editor_view_state_get_render_mode (self->app->view, (gint) self->index))
		lrg_combo_box_set_selected (self->mode_combo,
		                            (gint) lrg_editor_view_state_get_render_mode (self->app->view, (gint) self->index));
	for (i = 0; i < G_N_ELEMENTS (overlay_items); i++)
	{
		g_autofree gchar *aname = g_strconcat ("overlay-", overlay_items[i].nick, NULL);
		GAction          *action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), aname);

		g_simple_action_set_state (G_SIMPLE_ACTION (action),
		                           g_variant_new_boolean ((overlays & overlay_items[i].flag) != 0));
	}
	for (i = 0; i < LRG_RENDER_LAYERS_COUNT; i++)
	{
		g_autofree gchar *aname = g_strdup_printf ("layer-%u", i);
		GAction          *action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), aname);

		g_simple_action_set_state (G_SIMPLE_ACTION (action),
		                           g_variant_new_boolean ((self->cull_mask & LRG_RENDER_LAYERS_BIT (i)) != 0));
	}
	self->syncing = FALSE;
}

/* ==========================================================================
 * Model signals
 * ========================================================================== */

static void
on_camera_changed (LrgEditorViewState *view,
                   guint               index,
                   gpointer            user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	if (index != self->index)
		return;
	mark_dirty (self);
	sync_header (self);
}

static void
on_viewport_changed (LrgEditorViewState *view,
                     guint               index,
                     gpointer            user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	if (index != self->index)
		return;
	mark_dirty (self);
	sync_header (self);
	if (self->preview_node != NULL)
		lrg_viewport_queue_render (self->preview);
}

static void
on_view_notify (GObject    *object,
                GParamSpec *pspec,
                gpointer    user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	/* Active viewport, layout, tile mode: the gizmo owner, outline and
	 * the tile grid lines change */
	mark_dirty (self);
	update_preview (self);
}

static void
on_document_changed (gpointer user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	mark_dirty (self);
	self->missing_dirty = TRUE;
	update_preview (self);
	if (self->preview_node != NULL)
		lrg_viewport_queue_render (self->preview);
}

static void
on_level_node (LrgLevel *level,
               LrgNode  *node,
               gpointer  user_data)
{
	on_document_changed (user_data);
}

/* Drops the level's signals by id (cheaper than a scan by data). */
static void
unwatch_level (LrgEditorViewportPanel *self)
{
	guint i;

	if (self->level == NULL)
		return;
	for (i = 0; i < G_N_ELEMENTS (self->level_ids); i++)
		if (self->level_ids[i] != 0)
			g_signal_handler_disconnect (self->level, self->level_ids[i]);
	memset (self->level_ids, 0, sizeof self->level_ids);
	g_object_remove_weak_pointer (G_OBJECT (self->level), (gpointer *) &self->level);
	self->level = NULL;
}

/* Holds the signals of the editor's current level. */
static void
watch_level (LrgEditorViewportPanel *self)
{
	LrgLevel *level = (self->sig_editor != NULL) ? lrg_editor_get_level (LRG_EDITOR (self->sig_editor)) : NULL;

	if (level == self->level)
		return;
	unwatch_level (self);
	self->level = level;
	if (level != NULL)
	{
		g_object_add_weak_pointer (G_OBJECT (level), (gpointer *) &self->level);
		self->level_ids[0] = g_signal_connect (level, "node-added", G_CALLBACK (on_level_node), self);
		self->level_ids[1] = g_signal_connect (level, "node-removed", G_CALLBACK (on_level_node), self);
		self->level_ids[2] = g_signal_connect (level, "node-changed", G_CALLBACK (on_level_node), self);
	}
}

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	watch_level (self);
	on_document_changed (self);
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	self->preview_node = NULL;
	lrg_widget_set_visible (LRG_WIDGET (self->preview), FALSE);
	watch_level (self);
	on_document_changed (self);
}

static void
on_selection_changed (LrgEditorSelection *selection,
                      gpointer            user_data)
{
	on_document_changed (user_data);
}

static void
on_play_changed (LrgEditorPlaySession *play,
                 gpointer              user_data)
{
	mark_dirty (LRG_EDITOR_VIEWPORT_PANEL (user_data));
}

/* ==========================================================================
 * Internal API (the shell calls these on its viewport panel)
 * ========================================================================== */

/* Per frame: ticks the held fly keys (the navigator is shared, so only
 * the shell's panel calls this). Sizes and renders are the panels' own
 * step. */
void
editor_viewport_panel_tick (LrgEditorViewportPanel *self,
                            gfloat                  dt)
{
	if (self->app == NULL)
		return;
	lrg_editor_viewport_nav_tick (self->app->nav, dt);

	/* The split registers its 0.8.1 view.render_mode after the panels
	 * exist; the first frame puts the 0.9.0 one (mode list, debug views)
	 * back on top. */
	if (g_object_get_data (G_OBJECT (self->app), "lrg-vp-ed2-methods") == NULL)
	{
		g_object_set_data (G_OBJECT (self->app), "lrg-vp-ed2-methods", GINT_TO_POINTER (1));
		register_methods (self->app);
	}
}

/* The rectangle of viewport 0 in canvas coordinates (ui.state "viewport"). */
gboolean
editor_viewport_panel_get_rect (LrgEditorViewportPanel *self,
                                gfloat                 *x,
                                gfloat                 *y,
                                gfloat                 *w,
                                gfloat                 *h)
{
	GrlRectangle r = { 0 };
	gboolean     ok;

	ok = lrg_viewport_get_display_rect (self->viewport, &r) && lrg_widget_get_visible (LRG_WIDGET (self)) &&
	     lrg_widget_get_parent (LRG_WIDGET (self)) != NULL;
	*x = lrg_widget_get_world_x (LRG_WIDGET (self->viewport)) + r.x;
	*y = lrg_widget_get_world_y (LRG_WIDGET (self->viewport)) + r.y;
	*w = r.width;
	*h = r.height;
	return ok;
}

/* Whether the pointer is over any viewport of the panel's split. */
gboolean
editor_viewport_panel_is_hovered (LrgEditorViewportPanel *self)
{
	LrgEditorViewportSplit *split = find_split (self);
	guint                   i;

	if (split == NULL)
		return (lrg_widget_get_state_flags (LRG_WIDGET (self->viewport)) & LRG_WIDGET_STATE_HOVERED) != 0;
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewportPanel *p = lrg_editor_viewport_split_get_panel (split, i);

		if (p != NULL && (lrg_widget_get_state_flags (LRG_WIDGET (p->viewport)) & LRG_WIDGET_STATE_HOVERED))
			return TRUE;
	}
	return FALSE;
}

/* Reads the active viewport's last image back and writes it as PNG. */
gboolean
editor_viewport_panel_screenshot (LrgEditorViewportPanel  *self,
                                  const gchar             *path,
                                  GError                 **error)
{
	LrgEditorViewportSplit *split = find_split (self);
	LrgEditorViewportPanel *panel = self;
	GrlRenderTexture       *target;
	g_autoptr(GrlImage)     image = NULL;

	if (split != NULL)
		panel = lrg_editor_viewport_split_get_active_panel (split);
	target = lrg_viewport_get_render_texture (panel->viewport);
	if (target == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "No viewport image (the editor is headless or the viewport is hidden)");
		return FALSE;
	}
	image = grl_texture_to_image (grl_render_texture_get_texture (target));
	if (image == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not read the viewport image back");
		return FALSE;
	}
	grl_image_flip_vertical (image);
	if (!grl_image_export (image, path))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not write %s", path);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * GObject / LrgWidget
 * ========================================================================== */

/* Header strip on top, the viewport below; the overlay covers the
 * viewport; the preview sits bottom right of the viewport. */
static void
lrg_editor_viewport_panel_layout_children (LrgContainer *container)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (container);
	gfloat                  w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                  h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                  hw = 0.0f, hh = 0.0f;
	LrgInsets               m;
	gfloat                  vy;

	/* Children are added one by one during construction */
	if (self->header == NULL || self->tools == NULL || self->viewport == NULL || self->overlay == NULL ||
	    self->preview == NULL)
		return;
	lrg_widget_get_preferred_size (LRG_WIDGET (self->header), &hw, &hh);
	lrg_widget_get_margin (LRG_WIDGET (self->header), &m);
	hh += m.top + m.bottom;
	{
		gfloat    tw = 0.0f, th = 0.0f;
		LrgInsets tm;

		lrg_widget_get_preferred_size (LRG_WIDGET (self->tools), &tw, &th);
		lrg_widget_get_margin (LRG_WIDGET (self->tools), &tm);
		tw += tm.left + tm.right;
		hh = MAX (hh, th + tm.top + tm.bottom);
		lrg_widget_place_in_slot (LRG_WIDGET (self->tools), MAX (w - tw, 0.0f), 0.0f, MIN (tw, w), hh);
		lrg_widget_place_in_slot (LRG_WIDGET (self->header), 0.0f, 0.0f, MAX (w - tw, 0.0f), hh);
	}
	vy = hh;
	lrg_widget_place_in_slot (LRG_WIDGET (self->viewport), 0.0f, vy, w, MAX (h - vy, 1.0f));
	lrg_widget_place_in_slot (LRG_WIDGET (self->overlay), 0.0f, vy, w, MAX (h - vy, 1.0f));

	/* The camera preview: about a third of the width, the camera aspect */
	{
		gfloat pw = CLAMP (w * PREVIEW_FRACTION, PREVIEW_MIN_W, PREVIEW_MAX_W);
		gfloat ph = pw / PREVIEW_ASPECT;

		if (self->preview_node != NULL)
		{
			LrgNodeVisual *visual = lrg_node_get_visual (self->preview_node);
			gdouble        aspect = (visual != NULL) ? lrg_node_visual_get_param_double (visual, "aspect", 0.0) : 0.0;

			if (aspect > 0.1)
				ph = pw / (gfloat) aspect;
		}
		lrg_widget_place_in_slot (LRG_WIDGET (self->preview), MAX (w - pw - PREVIEW_MARGIN, 0.0f),
		                          MAX (h - ph - PREVIEW_MARGIN, vy), pw, ph);
	}
}

/* The header strip's background, then the children. */
static void
lrg_editor_viewport_panel_paint (LrgWidget    *widget,
                                 LrgPaintList *list)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (widget);
	LrgTheme               *theme = lrg_widget_get_theme (widget);
	gfloat                  x = lrg_widget_get_world_x (widget);
	gfloat                  y = lrg_widget_get_world_y (widget);
	gfloat                  hh;

	if (self->viewport == NULL)
	{
		LRG_WIDGET_CLASS (lrg_editor_viewport_panel_parent_class)->paint (widget, list);
		return;
	}
	hh = lrg_widget_get_y (LRG_WIDGET (self->viewport));
	lrg_paint_list_rect (list, x, y, lrg_widget_get_width (widget), hh, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	lrg_paint_list_rect (list, x, y + hh - 1.0f, lrg_widget_get_width (widget), 1.0f, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	LRG_WIDGET_CLASS (lrg_editor_viewport_panel_parent_class)->paint (widget, list);
}

/*
 * Per frame, before the viewport's own step: keep the camera's size in
 * step with the widget (also headless), queue a render when something
 * changed or continuously while playing / navigating / dragging, follow
 * the selection for the preview, and run the active-outline spring.
 */
static gboolean
lrg_editor_viewport_panel_step (LrgWidget *widget,
                                gfloat     dt)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (widget);
	LrgEditorApp           *app = self->app;
	gboolean                continuous, busy;
	gint                    w = 0, h = 0;
	guint                   generation;

	if (app == NULL)
		return FALSE;

	lrg_viewport_get_content_size (self->viewport, &w, &h);
	if (w > 0 && h > 0 && ((gfloat) w != self->size_w || (gfloat) h != self->size_h))
	{
		self->size_w = (gfloat) w;
		self->size_h = (gfloat) h;
		lrg_editor_view_state_set_viewport_size (app->view, (gint) self->index, (gfloat) w, (gfloat) h);
		mark_dirty (self);
	}

	generation = lrg_editor_scene_renderer_get_generation (app->renderer);
	if (generation != self->seen_generation)
	{
		self->seen_generation = generation;
		self->missing_dirty = TRUE;
		mark_dirty (self);
	}

	update_preview (self);

	continuous = lrg_editor_play_session_is_playing (app->play) || self->gizmo_drag || self->tile_drag ||
	             lrg_editor_view_state_get_timeline_playing (app->view) ||
	             (is_active (self) && lrg_editor_viewport_nav_get_mode (app->nav) != LRG_EDITOR_NAV_MODE_NONE &&
	              lrg_editor_viewport_nav_get_mode (app->nav) != LRG_EDITOR_NAV_MODE_BOX);
	if ((self->dirty || continuous) && self->gpu_enabled)
	{
		lrg_viewport_queue_render (self->viewport);
		if (self->preview_node != NULL)
			lrg_viewport_queue_render (self->preview);
		self->dirty = FALSE;
	}

	lrg_spring_set_target (&self->active_spring, is_active (self) ? 1.0f : 0.0f);
	busy = lrg_spring_step (&self->active_spring, dt);
	return busy || continuous;
}

static void
lrg_editor_viewport_panel_dispose (GObject *object)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (object);
	GObject               **sig[5];
	guint                   i;

	sig[0] = &self->sig_editor;
	sig[1] = &self->sig_selection;
	sig[2] = &self->sig_view;
	sig[3] = &self->sig_nav;
	sig[4] = &self->sig_play;
	for (i = 0; i < G_N_ELEMENTS (sig); i++)
		if (*sig[i] != NULL)
		{
			g_signal_handlers_disconnect_by_data (*sig[i], self);
			g_clear_object (sig[i]);
		}
	if (self->app != NULL)
	{
		if (nav_owned (self))
			g_object_set_data (G_OBJECT (self->app), NAV_OWNER_KEY, NULL);
		g_object_remove_weak_pointer (G_OBJECT (self->app), (gpointer *) &self->app);
		self->app = NULL;
	}
	unwatch_level (self);
	if (self->context_menu != NULL)
		lrg_menu_close (self->context_menu);
	g_clear_object (&self->context_menu);
	if (self->overlay_menu != NULL)
		lrg_menu_close (self->overlay_menu);
	g_clear_object (&self->overlay_menu);
	g_clear_weak_pointer (&self->vol_node);
	g_clear_weak_pointer (&self->phys_node);
	if (self->phys_runtime != NULL)
		g_clear_signal_handler (&self->phys_overlay_id, self->phys_runtime);
	g_clear_weak_pointer (&self->phys_runtime);
	g_clear_object (&self->layers_menu);
	g_clear_object (&self->actions);
	self->drop = NULL;   /* owned by the widget */
	self->preview_node = NULL;

	G_OBJECT_CLASS (lrg_editor_viewport_panel_parent_class)->dispose (object);
}

static void
lrg_editor_viewport_panel_finalize (GObject *object)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (object);

	g_clear_pointer (&self->missing, g_ptr_array_unref);
	g_clear_pointer (&self->rest_skeletons, g_hash_table_unref);
	g_clear_pointer (&self->drop_label, g_free);
	g_clear_object (&self->vol_before);
	g_clear_object (&self->phys_before);
	g_clear_object (&self->phys_debug);

	G_OBJECT_CLASS (lrg_editor_viewport_panel_parent_class)->finalize (object);
}

static void
lrg_editor_viewport_panel_class_init (LrgEditorViewportPanelClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_editor_viewport_panel_dispose;
	object_class->finalize = lrg_editor_viewport_panel_finalize;
	widget_class->handle_event = lrg_editor_viewport_panel_handle_event;
	widget_class->paint = lrg_editor_viewport_panel_paint;
	widget_class->step = lrg_editor_viewport_panel_step;
	container_class->layout_children = lrg_editor_viewport_panel_layout_children;
}

static void
lrg_editor_viewport_panel_init (LrgEditorViewportPanel *self)
{
	self->last_pick_x = -1000.0f;
	self->last_pick_y = -1000.0f;
	self->preview_enabled = TRUE;
	self->gpu_enabled = TRUE;
	self->dirty = TRUE;
	self->missing = g_ptr_array_new ();
	self->missing_dirty = TRUE;
	self->cull_mask = LRG_RENDER_LAYERS_MASK_ALL;
	self->show_ik = TRUE;
	self->phys_debug = lrg_debug_draw_new ();
	lrg_spring_init (&self->active_spring, 0.0f, LRG_MOTION_SMOOTH);
}

/* A render-mode LrgViewport that renders on demand. */
static LrgViewport *
new_render_viewport (const gchar          *name,
                     LrgViewportRenderFunc func,
                     gpointer              data)
{
	LrgViewport *viewport = lrg_viewport_new ();

	lrg_widget_set_name (LRG_WIDGET (viewport), name);
	lrg_viewport_set_mode (viewport, LRG_VIEWPORT_MODE_RENDER);
	lrg_viewport_set_continuous (viewport, FALSE);
	lrg_viewport_set_auto_clear (viewport, FALSE);
	lrg_viewport_set_render_func (viewport, func, data, NULL);
	return viewport;
}


/* ==========================================================================
 * IPC: view.layers, view.render_mode (0.9.0 modes), post install
 * ========================================================================== */

static JsonNode *
vp_obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

/* The panel of params.viewport (default: the active viewport). */
static LrgEditorViewportPanel *
ipc_viewport (LrgEditorApp  *app,
              JsonObject    *params,
              GError       **error)
{
	gint64                  index = lrg_editor_view_state_get_active_viewport (app->view);
	LrgEditorViewportPanel *panel = NULL;

	if (params != NULL && json_object_has_member (params, "viewport"))
		index = json_object_get_int_member (params, "viewport");
	if (index >= 0 && index < LRG_EDITOR_VIEW_MAX_VIEWPORTS && app->viewport_split != NULL)
		panel = lrg_editor_viewport_split_get_panel (app->viewport_split, (guint) index);
	if (panel == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "No viewport %" G_GINT64_FORMAT, index);
	return panel;
}

/* {"viewport": 0, "mask": 1} | {"layer": 2, "enabled": false} -> the mask */
static JsonNode *
m_view_layers (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp           *app = LRG_EDITOR_APP (user_data);
	LrgEditorViewportPanel *self = ipc_viewport (app, params, error);
	JsonObject             *o;
	JsonArray              *layers;
	guint32                 mask;
	guint                   i;

	if (self == NULL)
		return NULL;
	mask = self->cull_mask;
	if (params != NULL && json_object_has_member (params, "mask"))
	{
		JsonNode *mn = json_object_get_member (params, "mask");

		/* a number, or "all" / "none" / "0x..." */
		if (JSON_NODE_HOLDS_VALUE (mn) && json_node_get_value_type (mn) == G_TYPE_STRING)
		{
			const gchar *t = json_node_get_string (mn);

			mask = g_strcmp0 (t, "all") == 0 ? LRG_RENDER_LAYERS_MASK_ALL
			     : g_strcmp0 (t, "none") == 0 ? LRG_RENDER_LAYERS_MASK_NONE
			     : (guint32) g_ascii_strtoull (t, NULL, 0);
		}
		else
			mask = (guint32) json_object_get_int_member (params, "mask");
	}
	if (params != NULL && json_object_has_member (params, "layer"))
	{
		JsonNode *ln = json_object_get_member (params, "layer");
		gint64    layer = -1;

		/* a layer index, or its name ("water", "editor-only", "Layer 9") */
		if (JSON_NODE_HOLDS_VALUE (ln) && json_node_get_value_type (ln) == G_TYPE_STRING)
		{
			const gchar *name = json_node_get_string (ln);

			for (i = 0; i < LRG_RENDER_LAYERS_COUNT && layer < 0; i++)
			{
				g_autofree gchar *label = (i < G_N_ELEMENTS (layer_names)) ? g_strdup (layer_names[i])
				                                                           : g_strdup_printf ("Layer %u", i);
				g_autofree gchar *kebab = g_ascii_strdown (label, -1);

				g_strdelimit (kebab, " _", '-');
				if (g_ascii_strcasecmp (name, label) == 0 || g_ascii_strcasecmp (name, kebab) == 0)
					layer = i;
			}
			if (layer < 0)
				layer = g_ascii_strtoll (name, NULL, 10) + (g_ascii_isdigit (*name) ? 0 : LRG_RENDER_LAYERS_COUNT);
		}
		else
			layer = json_object_get_int_member (params, "layer");

		if (layer < 0 || layer >= LRG_RENDER_LAYERS_COUNT)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Layers are 0 .. %d", LRG_RENDER_LAYERS_COUNT - 1);
			return NULL;
		}
		if (json_object_has_member (params, "enabled"))
		{
			if (json_object_get_boolean_member (params, "enabled"))
				mask |= LRG_RENDER_LAYERS_BIT (layer);
			else
				mask &= ~LRG_RENDER_LAYERS_BIT (layer);
		}
		else
			mask ^= LRG_RENDER_LAYERS_BIT (layer);
	}
	if (mask != self->cull_mask)
		lrg_editor_viewport_panel_set_layer_mask (self, mask);

	o = json_object_new ();
	layers = json_array_new ();
	json_object_set_int_member (o, "viewport", self->index);
	json_object_set_int_member (o, "mask", self->cull_mask);
	for (i = 0; i < LRG_RENDER_LAYERS_COUNT; i++)
	{
		JsonObject *l = json_object_new ();
		g_autofree gchar *name = (i < G_N_ELEMENTS (layer_names)) ? g_strdup (layer_names[i])
		                                                          : g_strdup_printf ("Layer %u", i);

		json_object_set_int_member (l, "index", i);
		json_object_set_string_member (l, "name", name);
		json_object_set_boolean_member (l, "enabled", (self->cull_mask & LRG_RENDER_LAYERS_BIT (i)) != 0);
		json_array_add_object_element (layers, l);
	}
	json_object_set_array_member (o, "layers", layers);
	return vp_obj_node (o);
}

/* {"viewport": 1, "mode": "overdraw"} -> {"viewport", "mode", "modes": [...]} */
static JsonNode *
m_view_render_mode (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorApp          *app = LRG_EDITOR_APP (user_data);
	g_autoptr(GEnumClass)  modes = g_type_class_ref (LRG_TYPE_EDITOR_RENDER_MODE);
	LrgEditorViewportPanel *panel = ipc_viewport (app, params, error);
	const gchar           *mode = NULL;
	GEnumValue            *value;
	JsonObject            *o;
	JsonArray             *list;
	guint                  i;

	if (panel == NULL)
		return NULL;
	if (params != NULL && json_object_has_member (params, "mode"))
		mode = json_object_get_string_member (params, "mode");
	if (mode != NULL)
	{
		value = g_enum_get_value_by_nick (modes, mode);
		if (value == NULL)
		{
			g_autoptr(GString) all = g_string_new ("");

			for (i = 0; i < modes->n_values; i++)
				g_string_append_printf (all, "%s%s", i > 0 ? ", " : "", modes->values[i].value_nick);
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown render mode '%s' (%s)", mode, all->str);
			return NULL;
		}
		lrg_editor_view_state_set_render_mode (app->view, (gint) panel->index, (LrgEditorRenderMode) value->value);
	}
	value = g_enum_get_value (modes, lrg_editor_view_state_get_render_mode (app->view, (gint) panel->index));
	o = json_object_new ();
	list = json_array_new ();
	json_object_set_int_member (o, "viewport", panel->index);
	json_object_set_string_member (o, "mode", value != NULL ? value->value_nick : "lit");
	for (i = 0; i < modes->n_values; i++)
		json_array_add_string_element (list, modes->values[i].value_nick);
	json_object_set_array_member (o, "modes", list);
	return vp_obj_node (o);
}

/* physics.overlay {viewport?, flags?, enable?, disable?} -> {flags} */
static const gchar * const physics_flag_nicks[] = {
	"shapes", "aabbs", "contacts", "joints", "sleeping", "com", "velocity", "broadphase"
};

static gboolean
physics_flags_from_json (JsonArray              *array,
                         LrgDynamicsDebugFlags  *out,
                         GError                **error)
{
	guint i, k;

	*out = 0;
	for (i = 0; array != NULL && i < json_array_get_length (array); i++)
	{
		const gchar *nick = json_array_get_string_element (array, i);
		gboolean     found = FALSE;

		if (g_strcmp0 (nick, "all") == 0)
		{
			*out = LRG_DYNAMICS_DEBUG_ALL;
			continue;
		}
		if (g_strcmp0 (nick, "none") == 0)
			continue;
		for (k = 0; k < G_N_ELEMENTS (physics_flag_nicks); k++)
			if (g_strcmp0 (nick, physics_flag_nicks[k]) == 0 ||
			    (k == 3 && g_strcmp0 (nick, "constraints") == 0) ||
			    (k == 5 && g_strcmp0 (nick, "center-of-mass") == 0))
			{
				*out |= (1u << k);
				found = TRUE;
			}
		if (!found)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown physics overlay flag '%s' (shapes, aabbs, contacts, joints, sleeping, com, "
			             "velocity, broadphase, all, none)", nick != NULL ? nick : "(not a string)");
			return FALSE;
		}
	}
	return TRUE;
}

static JsonNode *
m_physics_overlay (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp           *app = LRG_EDITOR_APP (user_data);
	LrgEditorViewportPanel *panels[LRG_EDITOR_VIEW_MAX_VIEWPORTS];
	LrgEditorViewportPanel *first = NULL;
	guint                   n = 0, i;
	JsonObject             *o;
	JsonArray              *a;
	JsonNode               *result;

	/* The panels addressed: one viewport, or all of them */
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewportPanel *p = (app->viewport_split != NULL)
		                            ? lrg_editor_viewport_split_get_panel (app->viewport_split, i)
		                            : (i == 0 ? app->viewport_panel : NULL);

		if (p == NULL)
			continue;
		if (params != NULL && json_object_has_member (params, "viewport") &&
		    json_object_get_int_member (params, "viewport") != (gint64) i)
			continue;
		panels[n++] = p;
	}
	if (n == 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such viewport");
		return NULL;
	}
	first = panels[0];

	/* flags replaces; enable / disable change bits */
	if (params != NULL && (json_object_has_member (params, "flags") || json_object_has_member (params, "enable") ||
	                       json_object_has_member (params, "disable")))
	{
		LrgDynamicsDebugFlags set = 0, on = 0, off = 0;
		gboolean              replace = json_object_has_member (params, "flags");

		if ((replace && !physics_flags_from_json (json_object_get_array_member (params, "flags"), &set, error)) ||
		    (json_object_has_member (params, "enable") &&
		     !physics_flags_from_json (json_object_get_array_member (params, "enable"), &on, error)) ||
		    (json_object_has_member (params, "disable") &&
		     !physics_flags_from_json (json_object_get_array_member (params, "disable"), &off, error)))
			return NULL;
		for (i = 0; i < n; i++)
		{
			LrgDynamicsDebugFlags f = replace ? set : lrg_editor_viewport_panel_get_physics_overlay (panels[i]);

			lrg_editor_viewport_panel_set_physics_overlay (panels[i], (f | on) & ~off);
		}
	}

	o = json_object_new ();
	a = json_array_new ();
	for (i = 0; i < G_N_ELEMENTS (physics_flag_nicks); i++)
		if (lrg_editor_viewport_panel_get_physics_overlay (first) & (1u << i))
			json_array_add_string_element (a, physics_flag_nicks[i]);
	json_object_set_array_member (o, "flags", a);
	json_object_set_int_member (o, "recorded", lrg_editor_viewport_panel_record_physics (first));
	result = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (result, o);
	return result;
}

static void
register_methods (LrgEditorApp *app)
{
	if (app->server == NULL)
		return;
	lrg_editor_server_register_method (app->server, "view.layers", m_view_layers, app, NULL);
	lrg_editor_server_register_method (app->server, "view.render_mode", m_view_render_mode, app, NULL);
	lrg_editor_server_register_method (app->server, "physics.overlay", m_physics_overlay, app, NULL);
}

LrgEditorViewportPanel *
lrg_editor_viewport_panel_new_for_index (LrgEditorApp *app,
                                         guint         index)
{
	LrgEditorViewportPanel *self;
	LrgCustomWidgetFuncs    funcs;
	g_autofree gchar       *name = NULL;
	g_autofree gchar       *pname = NULL;
	static const gchar     *types[] = { "application/x-lrg-asset", "application/x-lrg-node", NULL };

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (index < LRG_EDITOR_VIEW_MAX_VIEWPORTS, NULL);

	self = g_object_new (LRG_TYPE_EDITOR_VIEWPORT_PANEL, NULL);
	self->app = app;
	self->index = index;
	g_object_add_weak_pointer (G_OBJECT (app), (gpointer *) &self->app);

	/* Viewport 0 keeps the 0.8.0 widget names (scripts and tests find
	 * "viewport-panel" / "viewport"); the others are numbered */
	name = index == 0 ? g_strdup ("viewport-panel") : g_strdup_printf ("viewport-panel-%u", index);
	lrg_widget_set_name (LRG_WIDGET (self), name);
	build_header (self);

	g_clear_pointer (&name, g_free);
	name = index == 0 ? g_strdup ("viewport") : g_strdup_printf ("viewport-%u", index);
	self->viewport = new_render_viewport (name, render_scene, self);
	g_signal_connect (self->viewport, "pointer-press", G_CALLBACK (on_pointer_press), self);
	g_signal_connect (self->viewport, "pointer-move", G_CALLBACK (on_pointer_move), self);
	g_signal_connect (self->viewport, "pointer-release", G_CALLBACK (on_pointer_release), self);
	g_signal_connect (self->viewport, "pointer-scroll", G_CALLBACK (on_pointer_scroll), self);
	g_signal_connect (self->viewport, "key", G_CALLBACK (on_key), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->viewport));
	g_object_unref (self->viewport);

	/* Overlays: painted above the scene, never hit */
	self->overlay = lrg_custom_widget_new ();
	memset (&funcs, 0, sizeof funcs);
	funcs.paint = overlay_paint;
	lrg_custom_widget_set_funcs (self->overlay, &funcs, self, NULL);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self->overlay), TRUE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->overlay));
	g_object_unref (self->overlay);

	/* The camera preview, hidden until a camera is selected */
	pname = g_strdup_printf ("vp%u-camera-preview", index);
	self->preview = new_render_viewport (pname, render_preview, self);
	lrg_widget_set_visible (LRG_WIDGET (self->preview), FALSE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->preview));
	g_object_unref (self->preview);

	/* Drops of assets and nodes, highlighted while hovering */
	self->drop = lrg_drop_target_new (types, LRG_DRAG_ACTION_ALL);
	lrg_drop_target_set_paint_highlight (self->drop, TRUE);
	g_signal_connect (self->drop, "enter", G_CALLBACK (on_drop_enter), self);
	g_signal_connect (self->drop, "motion", G_CALLBACK (on_drop_motion), self);
	g_signal_connect (self->drop, "leave", G_CALLBACK (on_drop_leave), self);
	g_signal_connect (self->drop, "drop", G_CALLBACK (on_drop), self);
	lrg_widget_add_drop_target (LRG_WIDGET (self), self->drop);

	self->sig_editor = g_object_ref (G_OBJECT (app->editor));
	self->sig_selection = g_object_ref (G_OBJECT (lrg_editor_get_selection (app->editor)));
	self->sig_view = g_object_ref (G_OBJECT (app->view));
	self->sig_nav = g_object_ref (G_OBJECT (app->nav));
	self->sig_play = g_object_ref (G_OBJECT (app->play));

	/* The navigator's gestures (acted on by the panel that took the press) */
	g_signal_connect (app->nav, "click", G_CALLBACK (on_nav_click), self);
	g_signal_connect (app->nav, "box-select", G_CALLBACK (on_nav_box_select), self);
	g_signal_connect (app->nav, "context-menu", G_CALLBACK (on_nav_context_menu), self);

	/* Everything the scene image depends on */
	g_signal_connect (app->view, "camera-changed", G_CALLBACK (on_camera_changed), self);
	g_signal_connect (app->view, "viewport-changed", G_CALLBACK (on_viewport_changed), self);
	g_signal_connect (app->view, "notify::active-viewport", G_CALLBACK (on_view_notify), self);
	g_signal_connect (app->view, "notify::layout", G_CALLBACK (on_view_notify), self);
	g_signal_connect (app->view, "notify::tile-mode", G_CALLBACK (on_view_notify), self);
	g_signal_connect (app->view, "notify::timeline-time", G_CALLBACK (on_view_notify), self);
	g_signal_connect (app->editor, "changed", G_CALLBACK (on_editor_changed), self);
	g_signal_connect (app->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	g_signal_connect (lrg_editor_get_selection (app->editor), "changed", G_CALLBACK (on_selection_changed), self);
	g_signal_connect (app->play, "started", G_CALLBACK (on_play_changed), self);
	g_signal_connect (app->play, "stopped", G_CALLBACK (on_play_changed), self);
	g_signal_connect (app->play, "paused", G_CALLBACK (on_play_changed), self);
	watch_level (self);

	/* The scene renderer: overlays are painted here, not into the image */
	if (app->renderer != NULL)
		lrg_editor_scene_renderer_set_draw_overlays (app->renderer, FALSE);
	lrg_editor_viewport_split_register_methods (app);
	register_methods (app);
	lrg_editor_post_stack_editor_install (app);
	if (app->renderer != NULL)
		lrg_editor_scene_renderer_set_cull_mask (app->renderer, index, self->cull_mask);
	sync_header (self);
	return self;
}

LrgEditorViewportPanel *
lrg_editor_viewport_panel_new (LrgEditorApp *app)
{
	return lrg_editor_viewport_panel_new_for_index (app, 0);
}

guint
lrg_editor_viewport_panel_get_index (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), 0);
	return self->index;
}

LrgViewport *
lrg_editor_viewport_panel_get_viewport (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), NULL);
	return self->viewport;
}

LrgWidget *
lrg_editor_viewport_panel_get_header (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), NULL);
	return LRG_WIDGET (self->header);
}

LrgViewport *
lrg_editor_viewport_panel_get_preview (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), NULL);
	return self->preview;
}

LrgNode *
lrg_editor_viewport_panel_get_preview_node (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), NULL);
	return self->preview_node;
}

void
lrg_editor_viewport_panel_set_preview_enabled (LrgEditorViewportPanel *self,
                                               gboolean                enabled)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));

	self->preview_enabled = enabled ? TRUE : FALSE;
	update_preview (self);
}

gboolean
lrg_editor_viewport_panel_get_preview_enabled (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), FALSE);
	return self->preview_enabled;
}

gboolean
lrg_editor_viewport_panel_get_render_pending (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), FALSE);
	return self->dirty;
}

void
lrg_editor_viewport_panel_set_gpu_enabled (LrgEditorViewportPanel *self,
                                           gboolean                enabled)
{
	LrgViewportMode mode = enabled ? LRG_VIEWPORT_MODE_RENDER : LRG_VIEWPORT_MODE_TEXTURE;

	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));

	/* Leaving render mode frees a viewport's render texture */
	self->gpu_enabled = enabled ? TRUE : FALSE;
	lrg_viewport_set_mode (self->viewport, mode);
	lrg_viewport_set_mode (self->preview, mode);
	if (enabled)
		mark_dirty (self);
}

/* ==========================================================================
 * Render layers, render queue (0.9.0)
 * ========================================================================== */

void
lrg_editor_viewport_panel_set_layer_mask (LrgEditorViewportPanel *self,
                                          guint32                 mask)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));

	self->cull_mask = mask;
	if (self->app != NULL && self->app->renderer != NULL)
		lrg_editor_scene_renderer_set_cull_mask (self->app->renderer, self->index, mask);
	sync_header (self);
	mark_dirty (self);
}

guint32
lrg_editor_viewport_panel_get_layer_mask (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), LRG_RENDER_LAYERS_MASK_ALL);
	return self->cull_mask;
}

void
lrg_editor_viewport_panel_queue_render (LrgEditorViewportPanel *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));
	mark_dirty (self);
}

/* Mirrors a flag into its stateful vp.* action (the menu's check mark). */
static void
sync_anim_action (LrgEditorViewportPanel *self,
                  const gchar            *name,
                  gboolean                value)
{
	GAction *action = self->actions != NULL ? g_action_map_lookup_action (G_ACTION_MAP (self->actions), name) : NULL;

	if (action != NULL)
		g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (value));
}

void
lrg_editor_viewport_panel_set_show_bones (LrgEditorViewportPanel *self,
                                          gboolean                show)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));
	self->show_bones = show;
	sync_anim_action (self, "anim-bones", show);
	if (self->overlay != NULL)
		lrg_widget_queue_layout (LRG_WIDGET (self->overlay));
}

gboolean
lrg_editor_viewport_panel_get_show_bones (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), FALSE);
	return self->show_bones;
}

void
lrg_editor_viewport_panel_set_show_ik_targets (LrgEditorViewportPanel *self,
                                               gboolean                show)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));
	self->show_ik = show;
	sync_anim_action (self, "anim-ik", show);
	if (self->overlay != NULL)
		lrg_widget_queue_layout (LRG_WIDGET (self->overlay));
}

gboolean
lrg_editor_viewport_panel_get_show_ik_targets (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), FALSE);
	return self->show_ik;
}

void
lrg_editor_viewport_panel_get_anim_overlay_counts (LrgEditorViewportPanel *self,
                                                   guint                  *n_bones,
                                                   guint                  *n_targets)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self));
	if (n_bones != NULL)
		*n_bones = self->anim_bones;
	if (n_targets != NULL)
		*n_targets = self->anim_targets;
}
