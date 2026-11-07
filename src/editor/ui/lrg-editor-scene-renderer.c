/* lrg-editor-scene-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSceneRenderer: the editor's viewport drawing.
 *
 * Every pixel of geometry goes through the engine's shared renderer,
 * LrgWorld3DRenderer (src/world3d/lrg-world3d-renderer.c), so the editor
 * and a game draw the same scene the same way:
 *
 *   authoring   the level's nodes become a transient LrgRenderList each
 *               render (primitives, models, sprites as textured quads,
 *               LIGHT nodes as LrgLight3DDesc lights); the runtime renderer
 *               syncs the lights, runs the shadow passes, draws the sky and
 *               the opaque / cutout / transparent passes and the post
 *               chain
 *   playing 3D  the play session's LrgWorld3D is drawn instead, through
 *               lrg_world3d_renderer_render_view()
 *
 * What only an editor shows stays here and is drawn from the runtime
 * renderer's hooks inside its 3D pass: the grid ("draw-background"), and
 * tilemap grids, missing-asset and prefab placeholders, node icons and
 * selection bounds ("draw-overlay").  After the 3D pass the transform
 * gizmo, orientation axes, box-select rectangle and overlay text are drawn
 * in viewport-local pixels with plain raylib 2D calls, so the gizmo is
 * pixel-identical to its hit testing.
 *
 * Colours are fixed Catppuccin Mocha values: the ones the old GUI style
 * produced for axes, text and accents.
 */

#include "config.h"

#include "lrg-editor-scene-renderer.h"
#include "../lrg-editor.h"
#include "../lrg-editor-selection.h"
#include "../lrg-editor-math.h"
#include "../lrg-editor-gizmo.h"
#include "../lrg-editor-settings.h"
#include "../lrg-editor-play-session.h"
#include "../lrg-level.h"
#include "../lrg-node.h"
#include "../lrg-node-private.h"
#include "../lrg-node-visual.h"
#include "../lrg-node-tilemap.h"
#include "../lrg-asset-meta.h"
#include "../../scene/lrg-material3d.h"
#include "../../world3d/lrg-world3d.h"
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define ICON_SIZE         (0.25f)

#define OVERLAY_FONT_SIZE (15)
#define OVERLAY_PAD       (6.0f)
#define NAV_LABEL_SIZE    (12)

#define FONT_SAMPLE_TEXT  "Aa Bb 123"
#define FONT_DEFAULT_SIZE (32u)
#define FONT_DEFAULT_CPS  "32-126"

/* Catppuccin Mocha, as the old LrgGuiStyle resolved them. */
#define COL_RED           ((Color) { 0xf3, 0x8b, 0xa8, 0xff })
#define COL_GREEN         ((Color) { 0xa6, 0xe3, 0xa1, 0xff })
#define COL_BLUE          ((Color) { 0x89, 0xb4, 0xfa, 0xff })
#define COL_YELLOW        ((Color) { 0xf9, 0xe2, 0xaf, 0xff })
#define COL_TEXT          ((Color) { 0xcd, 0xd6, 0xf4, 0xff })
#define COL_OVERLAY0      ((Color) { 0x6c, 0x70, 0x86, 0xff })
#define COL_LAVENDER      ((Color) { 0xb4, 0xbe, 0xfe, 0xff })
#define COL_BASE          ((Color) { 0x1e, 0x1e, 0x2e, 0xff })
#define COL_CRUST         ((Color) { 0x11, 0x11, 0x1b, 0xff })
#define COL_BG_EDIT       ((Color) { 30, 30, 46, 255 })
#define COL_BG_PLAY       ((Color) { 24, 28, 38, 255 })

#define N_POST_SLOTS (LRG_EDITOR_SCENE_RENDERER_PREVIEW_SLOT + 1)

/*
 * What the overlay hook draws for one visible node after the runtime
 * renderer drew the geometry: its world matrix and whether it is
 * selected.  Filled by the same tree walk that builds the render list,
 * so the tree is walked once per render.
 */
typedef struct
{
	LrgNode *node;
	gfloat   world[16];
	gboolean selected;
} OverlayEntry;

struct _LrgEditorSceneRenderer
{
	GObject               parent_instance;

	LrgEditor            *editor;
	LrgEditorSettings    *settings;
	LrgEditorGizmo       *gizmo;
	LrgEditorPlaySession *play;
	gboolean              playing;

	/* The shared runtime renderer: lights, shadows, sky, post, caches,
	 * primitive and model drawing */
	LrgWorld3DRenderer   *runtime;
	LrgRenderList        *list;
	GArray               *overlay;     /* OverlayEntry, rebuilt every render */
	gulong                bg_handler;
	gulong                overlay_handler;

	/* State of the render in progress, read by the hooks */
	const LrgEditorView  *cur_view;
	gboolean              cur_view_2d;
	gboolean              cur_grid;
	gboolean              cur_icons;
	gboolean              cur_bounds;
	gboolean              cur_tile_mode;
	gboolean              cur_nodes;   /* FALSE while drawing the play world */
	LrgNode              *cur_primary;
	guint                 tile_draws;

	/* Thumbnails light models with their own copy of the fallback lit
	 * shader (the runtime's shaders are private to it) */
	Shader                thumb_lit;
	gboolean              has_thumb_lit;
	gint                  thumb_loc_light;
	gint                  thumb_loc_ambient;

	/* grid-size / grid-extent, refreshed on notify instead of a
	 * g_object_get() by name every frame. */
	gdouble               grid_size;
	guint                 grid_extent;
	gboolean              grid_dirty;

	/* Per-viewport preview post-processing (plus the camera preview). */
	LrgPostProcessor     *post[N_POST_SLOTS];

	gboolean              draw_overlays;
	guint                 generation;

	/* Node count for the stats line, recounted when the tree changes. */
	guint                 node_count;
	guint                 node_count_serial;
	LrgNode              *node_count_root;
	gboolean              node_count_valid;

	guint                 drawn;
};

enum
{
	PROP_0,
	PROP_EDITOR,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgEditorSceneRenderer, lrg_editor_scene_renderer, G_TYPE_OBJECT)

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static Vector2
v2 (gfloat x,
    gfloat y)
{
	Vector2 v;

	v.x = x;
	v.y = y;
	return v;
}

static Color
with_alpha (Color   c,
            guchar  a)
{
	c.a = a;
	return c;
}

static void
normalize3 (gfloat *v)
{
	gfloat len = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (len < 1e-6f)
		return;
	v[0] /= len;
	v[1] /= len;
	v[2] /= len;
}

/* The fallback lit shader for thumbnails, compiled on first use. */
static gboolean
ensure_thumb_shader (LrgEditorSceneRenderer *self)
{
	if (self->has_thumb_lit)
		return TRUE;
	if (!IsWindowReady ())
		return FALSE;
	self->thumb_lit = LoadShaderFromMemory (lrg_world3d_renderer_get_shader_source ("scene.vs"),
	                                        lrg_world3d_renderer_get_shader_source ("lit.fs"));
	if (!IsShaderValid (self->thumb_lit) || self->thumb_lit.id == rlGetShaderIdDefault ())
	{
		memset (&self->thumb_lit, 0, sizeof self->thumb_lit);
		return FALSE;
	}
	self->thumb_loc_light = GetShaderLocation (self->thumb_lit, "lightDir");
	self->thumb_loc_ambient = GetShaderLocation (self->thumb_lit, "ambient");
	self->has_thumb_lit = TRUE;
	return TRUE;
}

/*
 * Frees the GPU resources: the runtime renderer's, the thumbnail shader,
 * and the post processors (replaced by fresh ones: LrgColorGrade owns a
 * shader of the old context).  With @gl FALSE only bookkeeping goes.
 */
static void
release_resources (LrgEditorSceneRenderer *self,
                   gboolean                gl)
{
	guint i;

	if (self->runtime != NULL)
		lrg_world3d_renderer_release (self->runtime);
	if (self->has_thumb_lit && gl)
		UnloadShader (self->thumb_lit);
	memset (&self->thumb_lit, 0, sizeof self->thumb_lit);
	self->has_thumb_lit = FALSE;
	for (i = 0; i < N_POST_SLOTS; i++)
	{
		g_clear_object (&self->post[i]);
		self->post[i] = lrg_world3d_renderer_new_preview_post_processor ();
	}
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

static void
grid_settings_changed (GObject    *settings,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (user_data);

	(void) settings;
	(void) pspec;
	self->grid_dirty = TRUE;
	self->generation++;
}

/* Reads grid-size / grid-extent after a change (or defaults without
 * settings). */
static void
sync_grid_settings (LrgEditorSceneRenderer *self)
{
	if (!self->grid_dirty)
		return;
	self->grid_size = 1.0;
	self->grid_extent = 50;
	if (self->settings != NULL)
		g_object_get (self->settings, "grid-size", &self->grid_size, "grid-extent", &self->grid_extent, NULL);
	self->grid_dirty = FALSE;
}

/* ==========================================================================
 * Nodes -> render items and lights
 * ========================================================================== */

/* A node's material colour (a light grey without a material). */
static void
material_rgba (LrgNodeVisual *visual,
               gfloat        *rgba)
{
	LrgMaterial3D *mat = lrg_node_visual_get_material (visual);

	rgba[0] = 0.75f;
	rgba[1] = 0.75f;
	rgba[2] = 0.78f;
	rgba[3] = 1.0f;
	if (mat != NULL)
		lrg_material3d_get_color (mat, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
}

/* A string visual param, or NULL. */
static const gchar *
param_string (LrgNodeVisual *visual,
              const gchar   *name)
{
	const GValue *v = lrg_node_visual_get_param (visual, name);

	return (v != NULL && G_VALUE_HOLDS_STRING (v)) ? g_value_get_string (v) : NULL;
}

/*
 * The light of a LIGHT node: "light_type" directional (shining down the
 * node's local -Z, like a camera looks), spot ("spot", or a "spot_angle"
 * above 0) or point; "intensity", "range" and "spot_angle" (degrees)
 * params; the material colour (white without one) as colour.
 */
static void
node_light_desc (LrgNodeVisual  *visual,
                 const gfloat   *world,
                 LrgLight3DDesc *out)
{
	LrgMaterial3D *mat = lrg_node_visual_get_material (visual);
	const gchar   *type = param_string (visual, "light_type");
	const gfloat   fwd[3] = { 0.0f, 0.0f, -1.0f };
	gfloat         rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	gfloat         spot;

	lrg_light3d_desc_init (out);
	if (mat != NULL)
		lrg_material3d_get_color (mat, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
	out->color[0] = rgba[0];
	out->color[1] = rgba[1];
	out->color[2] = rgba[2];
	out->intensity = (gfloat) lrg_node_visual_get_param_double (visual, "intensity", 1.0);
	out->range = (gfloat) lrg_node_visual_get_param_double (visual, "range", 10.0);
	spot = (gfloat) lrg_node_visual_get_param_double (visual, "spot_angle", 0.0);
	out->position[0] = world[12];
	out->position[1] = world[13];
	out->position[2] = world[14];
	lrg_editor_math_transform_direction (world, fwd, out->direction);
	normalize3 (out->direction);

	if (g_strcmp0 (type, "directional") == 0)
		out->kind = LRG_LIGHT3D_DIRECTIONAL;
	else if (g_strcmp0 (type, "spot") == 0 || spot > 0.0f)
	{
		out->kind = LRG_LIGHT3D_SPOT;
		if (spot > 0.0f)
		{
			out->outer_angle = spot * 0.5f * (gfloat) G_PI / 180.0f;
			out->inner_angle = out->outer_angle * 0.8f;
		}
	}
	else
		out->kind = LRG_LIGHT3D_POINT;
}

/* The tree walk that turns nodes into items, lights and overlay entries. */
typedef struct
{
	LrgEditorSceneRenderer *self;
	LrgRenderList          *list;     /* NULL: lights only */
	GArray                 *lights;   /* LrgLight3DDesc, or NULL */
	LrgEditorSelection     *selection;
	gboolean                scene_lighting;
} NodeWalk;

/*
 * The render item of one node's geometry: primitives, models (from the
 * runtime renderer's cache), sprites (a textured quad keeping the image's
 * aspect ratio) and CAD parts.  Missing assets, tilemaps and prefab
 * instances have no item: the overlay hook draws them.
 */
static void
add_node_item (NodeWalk      *walk,
               LrgNode       *node,
               LrgNodeVisual *visual,
               const gfloat  *world)
{
	LrgEditorSceneRenderer *self = walk->self;
	LrgRenderItem           item;
	gfloat                  rgba[4];

	lrg_render_item_init (&item);
	memcpy (item.world_matrix, world, sizeof item.world_matrix);
	memset (item.params, 0, sizeof item.params);
	material_rgba (visual, rgba);
	memcpy (item.color, rgba, sizeof item.color);
	item.flags = LRG_RENDER_ITEM_FLAG_CAST_SHADOW | LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;

	switch (lrg_node_visual_get_kind (visual))
	{
	case LRG_NODE_VISUAL_PRIMITIVE:
		item.kind = LRG_RENDER_ITEM_PRIMITIVE;
		item.primitive = lrg_node_visual_get_primitive (visual);
		/* Lines and flat 2D shapes cast no shadows */
		if (item.primitive == LRG_PRIMITIVE_GRID || item.primitive == LRG_PRIMITIVE_RECTANGLE_2D ||
		    item.primitive == LRG_PRIMITIVE_CIRCLE_2D)
			item.flags &= ~LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
		break;

	case LRG_NODE_VISUAL_MESH_ASSET:
		{
			const gchar *asset = lrg_node_visual_get_asset (visual);
			GrlModel    *model = (asset != NULL) ? lrg_world3d_renderer_get_model (self->runtime, asset) : NULL;

			if (model == NULL)
				return;   /* the overlay draws a magenta placeholder */
			item.kind = LRG_RENDER_ITEM_MODEL;
			item.model = model;
		}
		break;

	case LRG_NODE_VISUAL_SPRITE:
		{
			const gchar *asset = lrg_node_visual_get_asset (visual);
			GrlTexture  *tex = (asset != NULL) ? lrg_world3d_renderer_get_texture (self->runtime, asset) : NULL;
			gfloat       loc[3] = { 0.0f, 0.0f, 0.0f };
			gfloat       rot[3] = { (gfloat) G_PI * 0.5f, 0.0f, 0.0f };
			gfloat       scl[3] = { 1.0f, 1.0f, 1.0f };
			gfloat       quad[16], aspect_m[16], tmp[16];

			/* A unit plane stood up into the XY plane, keeping the
			 * image's aspect ratio inside the unit quad */
			lrg_editor_math_compose (loc, rot, scl, quad);
			if (tex != NULL && grl_texture_get_width (tex) > 0 && grl_texture_get_height (tex) > 0)
			{
				gfloat aspect = (gfloat) grl_texture_get_width (tex) / (gfloat) grl_texture_get_height (tex);
				gfloat zero[3] = { 0.0f, 0.0f, 0.0f };

				if (aspect >= 1.0f)
					scl[1] = 1.0f / aspect;
				else
					scl[0] = aspect;
				lrg_editor_math_compose (zero, zero, scl, aspect_m);
				lrg_editor_math_multiply (aspect_m, quad, tmp);
				memcpy (quad, tmp, sizeof quad);
			}
			lrg_editor_math_multiply (world, quad, item.world_matrix);
			item.kind = LRG_RENDER_ITEM_PRIMITIVE;
			item.primitive = LRG_PRIMITIVE_PLANE;
			item.texture = tex;
			item.flags &= ~LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
			if (lrg_node_visual_get_material (visual) == NULL)
				item.color[0] = item.color[1] = item.color[2] = item.color[3] = 1.0f;
		}
		break;

	case LRG_NODE_VISUAL_CAD_PART:
		item.kind = LRG_RENDER_ITEM_PRIMITIVE;
		item.primitive = LRG_PRIMITIVE_CUBE;
		item.color[0] = 250.0f / 255.0f;
		item.color[1] = 179.0f / 255.0f;
		item.color[2] = 135.0f / 255.0f;
		item.color[3] = 1.0f;
		break;

	default:
		return;
	}

	/* Translucent materials blend after the opaque pass */
	if (item.color[3] < 1.0f)
		item.flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
	lrg_render_list_add (walk->list, &item);
}

/* Walks @node's visible subtree: items, lights and overlay entries.
 * @parent_world is the parent's world matrix, so each node composes one
 * local matrix onto it instead of walking back to the root. */
static void
walk_subtree (NodeWalk     *walk,
              LrgNode      *node,
              const gfloat *parent_world)
{
	LrgNodeVisual *visual;
	GPtrArray     *children;
	gfloat         local[16];
	gfloat         world[16];
	guint          i;

	if (!lrg_node_get_visible (node))
		return;

	lrg_editor_node_get_local_matrix (node, local);
	lrg_editor_math_multiply (parent_world, local, world);
	visual = lrg_node_get_visual (node);

	if (walk->list != NULL)
	{
		OverlayEntry entry;

		if (visual != NULL)
			add_node_item (walk, node, visual, world);
		entry.node = node;
		memcpy (entry.world, world, sizeof entry.world);
		entry.selected = (walk->selection != NULL) && lrg_editor_selection_contains (walk->selection, node);
		g_array_append_val (walk->self->overlay, entry);
	}

	if (walk->scene_lighting && visual != NULL && lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_LIGHT)
	{
		LrgLight3DDesc light;

		node_light_desc (visual, world, &light);
		if (walk->list != NULL)
			lrg_render_list_add_light (walk->list, &light);
		if (walk->lights != NULL)
			g_array_append_val (walk->lights, light);
	}

	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
		walk_subtree (walk, (LrgNode *) g_ptr_array_index (children, i), world);
}

/* Walks the level's top-level nodes (the root itself draws nothing). */
static void
walk_level (NodeWalk *walk,
            LrgLevel *level)
{
	LrgNode   *root = lrg_level_get_root (level);
	GPtrArray *top = lrg_node_get_children (root);
	gfloat     root_world[16];
	guint      i;

	lrg_editor_node_get_world_matrix (root, root_world);
	for (i = 0; top != NULL && i < top->len; i++)
		walk_subtree (walk, (LrgNode *) g_ptr_array_index (top, i), root_world);
}

/* ==========================================================================
 * 3D overlays (drawn from the runtime renderer's hooks)
 * ========================================================================== */

/* Lines drawn in a node's local space (rlgl immediate mode). */
static void
push_local (const gfloat *world)
{
	rlPushMatrix ();
	rlMultMatrixf (world);
}

static void
draw_unit_wire_box (gfloat hx,
                    gfloat hy,
                    gfloat hz,
                    Color  color)
{
	DrawCubeWires ((Vector3) { 0.0f, 0.0f, 0.0f }, hx * 2.0f, hy * 2.0f, hz * 2.0f, color);
}

/* Gizmo-like icons for nodes without geometry. */
static void
draw_icon (LrgNodeVisualKind kind,
           Color             color)
{
	const gfloat s = ICON_SIZE;

	switch (kind)
	{
	case LRG_NODE_VISUAL_LIGHT:
		{
			gint i;

			/* A bulb with rays. */
			DrawSphereWires ((Vector3) { 0.0f, 0.0f, 0.0f }, s * 0.5f, 6, 8, color);
			for (i = 0; i < 8; i++)
			{
				gfloat a = (gfloat) i * (gfloat) G_PI / 4.0f;

				DrawLine3D ((Vector3) { cosf (a) * s * 0.7f, sinf (a) * s * 0.7f, 0.0f },
				            (Vector3) { cosf (a) * s * 1.1f, sinf (a) * s * 1.1f, 0.0f }, color);
			}
			/* The light shines down its local -Z, like a camera. */
			DrawLine3D ((Vector3) { 0.0f, 0.0f, 0.0f }, (Vector3) { 0.0f, 0.0f, -s * 3.0f }, color);
		}
		break;
	case LRG_NODE_VISUAL_CAMERA:
		{
			Vector3 o = { 0.0f, 0.0f, 0.0f };
			Vector3 c[4] = {
				{ -s, -s * 0.6f, -s * 2.0f }, { s, -s * 0.6f, -s * 2.0f },
				{ s, s * 0.6f, -s * 2.0f }, { -s, s * 0.6f, -s * 2.0f }
			};
			gint    i;

			/* A body and a frustum looking down -Z. */
			DrawCubeWires ((Vector3) { 0.0f, 0.0f, s * 0.5f }, s, s, s, color);
			for (i = 0; i < 4; i++)
			{
				DrawLine3D (o, c[i], color);
				DrawLine3D (c[i], c[(i + 1) % 4], color);
			}
			/* The "up" tick. */
			DrawTriangle3D ((Vector3) { -s * 0.3f, s * 0.7f, -s * 2.0f }, (Vector3) { s * 0.3f, s * 0.7f, -s * 2.0f },
			                (Vector3) { 0.0f, s * 1.0f, -s * 2.0f }, color);
		}
		break;
	case LRG_NODE_VISUAL_AUDIO_EMITTER:
		/* A speaker with two sound waves. */
		DrawSphereWires ((Vector3) { 0.0f, 0.0f, 0.0f }, s * 0.4f, 4, 8, color);
		DrawCircle3D ((Vector3) { 0.0f, 0.0f, 0.0f }, s * 0.8f, (Vector3) { 0.0f, 1.0f, 0.0f }, 90.0f, color);
		DrawCircle3D ((Vector3) { 0.0f, 0.0f, 0.0f }, s * 1.2f, (Vector3) { 0.0f, 1.0f, 0.0f }, 90.0f, color);
		break;
	default:
		/* Empty node: a small axis cross. */
		DrawLine3D ((Vector3) { -s, 0.0f, 0.0f }, (Vector3) { s, 0.0f, 0.0f }, color);
		DrawLine3D ((Vector3) { 0.0f, -s, 0.0f }, (Vector3) { 0.0f, s, 0.0f }, color);
		DrawLine3D ((Vector3) { 0.0f, 0.0f, -s }, (Vector3) { 0.0f, 0.0f, s }, color);
		break;
	}
}

/* Colour of an untextured tile id (matches the tile palette swatches). */
static Color
tile_swatch (guint id)
{
	static const guint32 palette[] = {
		0xf38ba8ffu, 0xfab387ffu, 0xf9e2afffu, 0xa6e3a1ffu, 0x94e2d5ffu, 0x89dcebffu,
		0x74c7ecffu, 0x89b4faffu, 0xb4befeffu, 0xcba6f7ffu, 0xf5c2e7ffu, 0xeba0acffu
	};
	guint32 c = palette[(id - 1) % G_N_ELEMENTS (palette)];

	return (Color) { (guchar) (c >> 24), (guchar) (c >> 16), (guchar) (c >> 8), (guchar) c };
}

/*
 * Draws a tilemap node's grid in its local XY plane: one textured quad
 * per non-empty cell (tileset slices), or flat swatches without a
 * tileset. Layers stack bottom first with a tiny z offset. The bounds
 * always show; cell lines show while the node is the tile paint target
 * (tile mode on and the node is the primary selection).
 */
static void
draw_tilemap (LrgEditorSceneRenderer *self,
              LrgNode                *node,
              LrgNodeVisual          *visual,
              const gfloat           *world)
{
	LrgNodeTilemap         *grid = lrg_node_visual_get_tilemap (visual);
	const gchar            *asset = lrg_node_visual_get_asset (visual);
	GrlTexture             *gtex = (asset != NULL) ? lrg_world3d_renderer_get_texture (self->runtime, asset) : NULL;
	Texture2D              *tex = (gtex != NULL) ? (Texture2D *) grl_texture_get_handle (gtex) : NULL;
	gfloat                  ts = (gfloat) lrg_node_visual_get_param_double (visual, "tile_size", 1.0);
	guint                   w = lrg_node_tilemap_get_width (grid);
	guint                   h = lrg_node_tilemap_get_height (grid);
	guint                   tw = lrg_node_tilemap_get_tile_width (grid);
	guint                   th = lrg_node_tilemap_get_tile_height (grid);
	guint                   cols = 0, count = 0;
	guint                   layer, x, y;

	if (!(ts > 0.0f))
		ts = 1.0f;
	/* The tileset is usable when it holds at least one tile. */
	if (tex != NULL && tw > 0 && th > 0 && tex->width >= (gint) tw && tex->height >= (gint) th)
	{
		cols = (guint) tex->width / tw;
		count = cols * ((guint) tex->height / th);
	}
	else
		tex = NULL;

	push_local (world);
	rlDisableBackfaceCulling ();
	for (layer = 0; layer < lrg_node_tilemap_get_n_layers (grid); layer++)
	{
		gfloat       z = 0.002f * (gfloat) layer;
		gsize        n = 0;
		const guint *cells = lrg_node_tilemap_get_layer_tiles (grid, layer, &n);

		if (!lrg_node_tilemap_get_layer_visible (grid, layer) || cells == NULL)
			continue;
		rlSetTexture (tex != NULL ? tex->id : rlGetTextureIdDefault ());
		for (y = 0; y < h; y++)
		{
			rlCheckRenderBatchLimit ((gint) (4 * w));
			rlBegin (RL_QUADS);
			for (x = 0; x < w; x++)
			{
				guint  id = cells[(gsize) y * w + x];
				gfloat x0 = (gfloat) x * ts, x1 = (gfloat) (x + 1) * ts;
				gfloat y0 = -(gfloat) y * ts, y1 = -(gfloat) (y + 1) * ts;
				gfloat u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
				Color  c = WHITE;

				if (id == 0)
					continue;
				if (tex != NULL)
				{
					guint idx = (id - 1) % MAX (count, 1u);

					u0 = (gfloat) ((idx % cols) * tw) / (gfloat) tex->width;
					v0 = (gfloat) ((idx / cols) * th) / (gfloat) tex->height;
					u1 = u0 + (gfloat) tw / (gfloat) tex->width;
					v1 = v0 + (gfloat) th / (gfloat) tex->height;
				}
				else
					c = tile_swatch (id);
				rlColor4ub (c.r, c.g, c.b, c.a);
				rlNormal3f (0.0f, 0.0f, 1.0f);
				rlTexCoord2f (u0, v0);
				rlVertex3f (x0, y0, z);
				rlTexCoord2f (u0, v1);
				rlVertex3f (x0, y1, z);
				rlTexCoord2f (u1, v1);
				rlVertex3f (x1, y1, z);
				rlTexCoord2f (u1, v0);
				rlVertex3f (x1, y0, z);
			}
			rlEnd ();
		}
		rlSetTexture (0);
		self->tile_draws++;
	}
	rlEnableBackfaceCulling ();

	/* Bounds always; cell lines while painting this node. */
	{
		Color  edge = { 120, 200, 140, 200 };
		gfloat gw = (gfloat) w * ts, gh = -(gfloat) h * ts;

		DrawLine3D ((Vector3) { 0.0f, 0.0f, 0.0f }, (Vector3) { gw, 0.0f, 0.0f }, edge);
		DrawLine3D ((Vector3) { gw, 0.0f, 0.0f }, (Vector3) { gw, gh, 0.0f }, edge);
		DrawLine3D ((Vector3) { gw, gh, 0.0f }, (Vector3) { 0.0f, gh, 0.0f }, edge);
		DrawLine3D ((Vector3) { 0.0f, gh, 0.0f }, (Vector3) { 0.0f, 0.0f, 0.0f }, edge);
		if (self->cur_tile_mode && self->cur_primary == node && w * h <= 256u * 256u)
		{
			Color line = { 120, 200, 140, 70 };

			for (x = 1; x < w; x++)
				DrawLine3D ((Vector3) { (gfloat) x * ts, 0.0f, 0.001f }, (Vector3) { (gfloat) x * ts, gh, 0.001f }, line);
			for (y = 1; y < h; y++)
				DrawLine3D ((Vector3) { 0.0f, -(gfloat) y * ts, 0.001f }, (Vector3) { gw, -(gfloat) y * ts, 0.001f },
				            line);
		}
	}
	rlPopMatrix ();
}

/* The icon of a light, camera, audio emitter or empty node, at a
 * constant on-screen size and oriented like the node. */
static void
draw_node_icon (LrgEditorSceneRenderer *self,
                LrgNode                *node,
                const gfloat           *world,
                gboolean                selected)
{
	LrgNodeVisual    *visual = lrg_node_get_visual (node);
	LrgNodeVisualKind kind = (visual != NULL) ? lrg_node_visual_get_kind (visual) : LRG_NODE_VISUAL_NONE;
	gfloat            pos[3];
	gfloat            scale;
	gfloat            loc[3], rot[3], scl[3], m[16];
	gfloat            one[3];
	Color             color;

	if (kind != LRG_NODE_VISUAL_NONE && kind != LRG_NODE_VISUAL_LIGHT && kind != LRG_NODE_VISUAL_CAMERA &&
	    kind != LRG_NODE_VISUAL_AUDIO_EMITTER)
		return;

	switch (kind)
	{
	case LRG_NODE_VISUAL_LIGHT:         color = (Color) { 249, 226, 175, 255 }; break;
	case LRG_NODE_VISUAL_CAMERA:        color = (Color) { 180, 190, 254, 255 }; break;
	case LRG_NODE_VISUAL_AUDIO_EMITTER: color = (Color) { 166, 227, 161, 255 }; break;
	default:                            color = (Color) { 166, 173, 200, 255 }; break;
	}
	if (selected)
		color = (Color) { 250, 179, 135, 255 };

	/* The world position is the translation column of @world (what
	 * lrg_editor_node_get_world_position() would recompute). */
	pos[0] = world[12];
	pos[1] = world[13];
	pos[2] = world[14];
	scale = lrg_editor_view_pixel_world_size (self->cur_view, pos) * 120.0f;

	/* Same location and rotation, uniform scale for the icon size. */
	if (!lrg_editor_math_decompose (world, loc, rot, scl))
		return;
	one[0] = one[1] = one[2] = CLAMP (scale, 0.05f, 50.0f);
	lrg_editor_math_compose (loc, rot, one, m);
	push_local (m);
	draw_icon (kind, color);
	rlPopMatrix ();
}

/* An infinite-looking grid whose spacing follows the zoom level: the XZ
 * ground plane in 3D, the XY plane in 2D. */
static void
draw_grid (LrgEditorSceneRenderer *self,
           const LrgEditorView    *view,
           gboolean                view_2d)
{
	gfloat  dist, spacing;
	gint    slices;
	gfloat  ext;
	gfloat  cx, cy;
	gdouble size;
	guint   extent;

	/* Cells are grid-size times a power of ten that follows the zoom;
	 * grid-extent is the half width in cells. */
	sync_grid_settings (self);
	size = MAX (self->grid_size, 0.0001);
	extent = self->grid_extent;
	slices = (gint) CLAMP (extent * 2, 2, 400);

	if (view->orthographic)
		dist = view->ortho_height;
	else
	{
		gfloat d[3] = { view->position[0] - view->target[0], view->position[1] - view->target[1],
		                view->position[2] - view->target[2] };

		dist = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	}
	spacing = (gfloat) size * powf (10.0f, floorf (log10f (MAX (dist / ((gfloat) size * 4.0f), 0.0001f))));
	spacing = MAX (spacing, (gfloat) size);
	ext = spacing * (gfloat) slices * 0.5f;

	rlPushMatrix ();
	if (view_2d)
	{
		/* XY plane, centred under the camera. */
		cx = floorf (view->target[0] / spacing) * spacing;
		cy = floorf (view->target[1] / spacing) * spacing;
		rlTranslatef (cx, cy, 0.0f);
		rlRotatef (90.0f, 1.0f, 0.0f, 0.0f);
	}
	else
	{
		cx = floorf (view->target[0] / spacing) * spacing;
		cy = floorf (view->target[2] / spacing) * spacing;
		/* A hair above y = 0 so it does not z-fight ground planes. */
		rlTranslatef (cx, 0.002f, cy);
	}
	DrawGrid (slices, spacing);
	rlPopMatrix ();

	/* World axes through the origin. */
	DrawLine3D ((Vector3) { -ext + cx, 0.0f, 0.0f }, (Vector3) { ext + cx, 0.0f, 0.0f }, (Color) { 243, 139, 168, 200 });
	if (view_2d)
		DrawLine3D ((Vector3) { 0.0f, -ext + cy, 0.0f }, (Vector3) { 0.0f, ext + cy, 0.0f }, (Color) { 166, 227, 161, 200 });
	else
		DrawLine3D ((Vector3) { 0.0f, 0.0f, -ext + cy }, (Vector3) { 0.0f, 0.0f, ext + cy }, (Color) { 137, 180, 250, 200 });
}

/* ==========================================================================
 * 2D overlays: transform gizmo
 * ========================================================================== */

/* The base colour of axis @i (0..2 = X Y Z). */
static Color
axis_color (gint i)
{
	switch (i)
	{
	case 0:  return COL_RED;
	case 1:  return COL_GREEN;
	default: return COL_BLUE;
	}
}

/* Active or hovered handles light up yellow. */
static Color
handle_color (LrgEditorGizmo       *gizmo,
              LrgEditorGizmoHandle  handle,
              Color                 base)
{
	if (lrg_editor_gizmo_get_active (gizmo) == handle ||
	    (!lrg_editor_gizmo_is_dragging (gizmo) && lrg_editor_gizmo_get_hovered (gizmo) == handle))
		return COL_YELLOW;
	return base;
}

static gboolean
project (const LrgEditorView *view,
         const gfloat        *p,
         Vector2             *out)
{
	gfloat x, y;

	if (!lrg_editor_view_project (view, p, &x, &y, NULL))
		return FALSE;
	out->x = x;
	out->y = y;
	return TRUE;
}

/*
 * Draws the transform gizmo of the current tool in viewport-local
 * pixels: rings (rotate), arms with arrow heads and plane squares
 * (translate) or arms with boxes (scale), plus the centre handle.
 */
static void
draw_gizmo (LrgEditorSceneRenderer *self,
            const LrgEditorView    *view)
{
	LrgEditorGizmo *gizmo = self->gizmo;
	LrgEditorTool   tool = lrg_editor_get_tool (self->editor);
	gfloat          origin[3], axes[3][3], size;
	Vector2         o;
	gint            i, k;

	if (!lrg_editor_gizmo_is_visible (gizmo))
		return;

	lrg_editor_gizmo_get_origin (gizmo, origin);
	for (i = 0; i < 3; i++)
		lrg_editor_gizmo_get_axis (gizmo, (guint) i, axes[i]);
	size = lrg_editor_gizmo_get_size (gizmo, view);
	if (!project (view, origin, &o))
		return;

	if (tool == LRG_EDITOR_TOOL_ROTATE)
	{
		/* One ring per axis plus the screen-aligned outer ring. */
		for (i = 0; i < 3; i++)
		{
			const gfloat *u = axes[(i + 1) % 3];
			const gfloat *v = axes[(i + 2) % 3];
			Color         c = handle_color (gizmo, (LrgEditorGizmoHandle) (LRG_EDITOR_GIZMO_HANDLE_X + i), axis_color (i));
			Vector2       prev = o;
			gboolean      have_prev = FALSE;
			gint          s;

			for (s = 0; s <= 64; s++)
			{
				gfloat  a = (gfloat) s / 64.0f * 2.0f * (gfloat) G_PI;
				gfloat  p[3];
				Vector2 q;

				for (k = 0; k < 3; k++)
					p[k] = origin[k] + (u[k] * cosf (a) + v[k] * sinf (a)) * size;
				if (project (view, p, &q))
				{
					if (have_prev)
						DrawLineEx (prev, q, 2.5f, c);
					prev = q;
					have_prev = TRUE;
				}
				else
					have_prev = FALSE;
			}
		}
		DrawCircleLinesV (o, LRG_EDITOR_GIZMO_SIZE_PIXELS * 1.2f,
		                  handle_color (gizmo, LRG_EDITOR_GIZMO_HANDLE_CENTER, COL_TEXT));
		return;
	}

	/* Plane squares (translate): XY tinted like Z, YZ like X, XZ like Y. */
	if (tool == LRG_EDITOR_TOOL_TRANSLATE)
	{
		static const gint pairs[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
		static const gint tints[3] = { 2, 0, 1 };
		const gfloat      ca[4] = { 0.2f, 0.45f, 0.45f, 0.2f };
		const gfloat      cb[4] = { 0.2f, 0.2f, 0.45f, 0.45f };

		for (i = 0; i < 3; i++)
		{
			Vector2  q[4];
			gboolean ok = TRUE;
			Color    c = handle_color (gizmo, (LrgEditorGizmoHandle) (LRG_EDITOR_GIZMO_HANDLE_XY + i),
			                           axis_color (tints[i]));
			gint     j;

			for (j = 0; j < 4 && ok; j++)
			{
				gfloat p[3];

				for (k = 0; k < 3; k++)
					p[k] = origin[k] + (axes[pairs[i][0]][k] * ca[j] + axes[pairs[i][1]][k] * cb[j]) * size;
				ok = project (view, p, &q[j]);
			}
			if (!ok)
				continue;
			/* Both windings so the square shows from either side. */
			c.a = 90;
			DrawTriangle (q[0], q[2], q[1], c);
			DrawTriangle (q[0], q[1], q[2], c);
			DrawTriangle (q[0], q[3], q[2], c);
			DrawTriangle (q[0], q[2], q[3], c);
			c.a = 255;
			for (j = 0; j < 4; j++)
				DrawLineEx (q[j], q[(j + 1) % 4], 1.0f, c);
		}
	}

	/* Arms with arrow heads (translate) or boxes (scale). */
	for (i = 0; i < 3; i++)
	{
		gfloat  tip[3];
		Vector2 t;
		Color   c = handle_color (gizmo, (LrgEditorGizmoHandle) (LRG_EDITOR_GIZMO_HANDLE_X + i), axis_color (i));

		for (k = 0; k < 3; k++)
			tip[k] = origin[k] + axes[i][k] * size;
		if (!project (view, tip, &t))
			continue;
		if (hypotf (t.x - o.x, t.y - o.y) < 4.0f)
			continue;   /* pointing at the camera */

		DrawLineEx (o, t, 3.0f, c);
		if (tool == LRG_EDITOR_TOOL_SCALE)
			DrawRectangleV (v2 (t.x - 5.0f, t.y - 5.0f), v2 (10.0f, 10.0f), c);
		else
		{
			gfloat  len = hypotf (t.x - o.x, t.y - o.y);
			Vector2 d = v2 ((t.x - o.x) / len, (t.y - o.y) / len);
			Vector2 a = v2 (t.x + d.x * 14.0f, t.y + d.y * 14.0f);
			Vector2 b = v2 (t.x - d.y * 6.0f, t.y + d.x * 6.0f);
			Vector2 e = v2 (t.x + d.y * 6.0f, t.y - d.x * 6.0f);

			DrawTriangle (a, b, e, c);
			DrawTriangle (a, e, b, c);
		}
	}

	/* Centre handle. */
	{
		Color c = handle_color (gizmo, LRG_EDITOR_GIZMO_HANDLE_CENTER, COL_TEXT);

		if (tool == LRG_EDITOR_TOOL_SCALE)
			DrawRectangleV (v2 (o.x - 6.0f, o.y - 6.0f), v2 (12.0f, 12.0f), c);
		else
			DrawCircleLinesV (o, 7.0f, c);
	}
}

/* ==========================================================================
 * 2D overlays: orientation axes, box select, text
 * ========================================================================== */

/*
 * The orientation gizmo, bottom left: a faint disc (brighter while hot or
 * dragged), +X +Y +Z as labelled discs on lines, -X -Y -Z as smaller
 * translucent discs, far ends drawn first. Geometry comes from @nav so it
 * matches its hit testing.
 */
static void
draw_nav_gizmo (LrgEditorViewportNav *nav)
{
	static const gchar *names[3] = { "X", "Y", "Z" };
	gfloat              cx, cy, depth[6];
	gint                order[6], i, j, hot;
	gboolean            lit;

	if (!lrg_editor_viewport_nav_get_gizmo_center (nav, &cx, &cy))
		return;
	hot = lrg_editor_viewport_nav_get_axis_hot (nav);
	lit = lrg_editor_viewport_nav_get_mode (nav) == LRG_EDITOR_NAV_MODE_GIZMO || hot != -1;

	/* A faint disc shows the drag area. */
	DrawCircleV (v2 (cx, cy), LRG_EDITOR_NAV_GIZMO_RADIUS + LRG_EDITOR_NAV_GIZMO_END_HIT,
	             with_alpha (COL_BASE, lit ? 0xb0 : 0x60));

	/* Far ends first so near ones draw on top (selection sort of six). */
	for (i = 0; i < 6; i++)
	{
		gfloat dx, dy;

		order[i] = i;
		lrg_editor_viewport_nav_get_gizmo_end (nav, i, &dx, &dy, &depth[i]);
	}
	for (i = 0; i < 6; i++)
		for (j = i + 1; j < 6; j++)
			if (depth[order[j]] < depth[order[i]])
			{
				gint t = order[i];

				order[i] = order[j];
				order[j] = t;
			}

	for (i = 0; i < 6; i++)
	{
		gint   e = order[i];
		gfloat dx, dy;
		Color  col = axis_color (e % 3);

		lrg_editor_viewport_nav_get_gizmo_end (nav, e, &dx, &dy, NULL);
		if (e < 3)
		{
			gint tw = MeasureText (names[e], NAV_LABEL_SIZE);

			DrawLineEx (v2 (cx, cy), v2 (cx + dx, cy + dy), 2.0f, col);
			DrawCircleV (v2 (cx + dx, cy + dy), hot == e ? 9.0f : 7.5f, col);
			DrawText (names[e], (gint) (cx + dx - (gfloat) tw * 0.5f), (gint) (cy + dy - (gfloat) NAV_LABEL_SIZE * 0.5f),
			          NAV_LABEL_SIZE, COL_CRUST);
		}
		else
			DrawCircleV (v2 (cx + dx, cy + dy), hot == e ? 7.5f : 6.0f, with_alpha (col, 0x90));
	}
}

/* The box-select rectangle while one is being dragged. */
static void
draw_box_select (LrgEditorViewportNav *nav)
{
	gfloat    x, y, w, h;
	Rectangle r;

	if (!lrg_editor_viewport_nav_get_box_rect (nav, &x, &y, &w, &h))
		return;
	r.x = x;
	r.y = y;
	r.width = w;
	r.height = h;
	DrawRectangleRec (r, with_alpha (COL_LAVENDER, 0x30));
	DrawRectangleLinesEx (r, 1.0f, COL_LAVENDER);
}

/* Number of nodes in the level, recounted only when the tree changed. */
static guint
count_nodes (LrgEditorSceneRenderer *self,
             LrgLevel               *level)
{
	LrgNode *root = lrg_level_get_root (level);
	guint    serial = lrg_node_get_tree_serial ();

	if (!self->node_count_valid || self->node_count_serial != serial || self->node_count_root != root)
	{
		self->node_count = lrg_level_count_nodes (level);
		self->node_count_serial = serial;
		self->node_count_root = root;
		self->node_count_valid = TRUE;
	}
	return self->node_count;
}

/* "Perspective", "Orthographic", "Top (ortho)", "2D", ... plus the
 * render mode when it is not the lit default. */
static gchar *
view_label (const LrgEditorViewCamera *cam,
            LrgEditorRenderMode        mode)
{
	const gchar *base;
	const gchar *suffix;
	const gchar *axis = NULL;

	switch (cam->axis_view)
	{
	case LRG_EDITOR_AXIS_VIEW_FRONT:  axis = "Front";  break;
	case LRG_EDITOR_AXIS_VIEW_BACK:   axis = "Back";   break;
	case LRG_EDITOR_AXIS_VIEW_RIGHT:  axis = "Right";  break;
	case LRG_EDITOR_AXIS_VIEW_LEFT:   axis = "Left";   break;
	case LRG_EDITOR_AXIS_VIEW_TOP:    axis = "Top";    break;
	case LRG_EDITOR_AXIS_VIEW_BOTTOM: axis = "Bottom"; break;
	default:                          axis = NULL;     break;
	}

	switch (mode)
	{
	case LRG_EDITOR_RENDER_MODE_UNLIT:         suffix = " | Unlit";    break;
	case LRG_EDITOR_RENDER_MODE_WIREFRAME:     suffix = " | Wire";     break;
	case LRG_EDITOR_RENDER_MODE_NORMALS:       suffix = " | Normals";  break;
	case LRG_EDITOR_RENDER_MODE_LIGHTING_ONLY: suffix = " | Lighting"; break;
	default:                                   suffix = "";            break;
	}

	if (cam->view_2d)
		base = "2D";
	else if (axis != NULL)
		return g_strdup_printf ("%s%s%s", axis, cam->view.orthographic ? " (ortho)" : "", suffix);
	else
		base = cam->view.orthographic ? "Orthographic" : "Perspective";
	return g_strdup_printf ("%s%s", base, suffix);
}

/* The view label top left and, with STATS, the counts top right. */
static void
draw_overlay_text (LrgEditorSceneRenderer    *self,
                   const LrgEditorViewCamera *cam,
                   LrgEditorRenderMode        mode,
                   gboolean                   stats,
                   gfloat                     width)
{
	g_autofree gchar *label = view_label (cam, mode);
	gint              lw = MeasureText (label, OVERLAY_FONT_SIZE);

	/* The label sits on a translucent chip like the old view button. */
	DrawRectangleRec ((Rectangle) { OVERLAY_PAD, OVERLAY_PAD, (gfloat) lw + OVERLAY_PAD * 2.0f,
	                                (gfloat) OVERLAY_FONT_SIZE + OVERLAY_PAD },
	                  with_alpha (COL_BASE, 0xc0));
	DrawText (label, (gint) (OVERLAY_PAD * 2.0f), (gint) (OVERLAY_PAD * 1.5f), OVERLAY_FONT_SIZE, COL_TEXT);

	if (stats)
	{
		LrgLevel          *level = lrg_editor_get_level (self->editor);
		LrgEditorSelection *sel = lrg_editor_get_selection (self->editor);
		g_autofree gchar   *text = NULL;
		gint                tw;

		text = g_strdup_printf ("%u nodes  %u selected  %d fps", level != NULL ? count_nodes (self, level) : 0u,
		                        sel != NULL ? lrg_editor_selection_get_count (sel) : 0u, GetFPS ());
		tw = MeasureText (text, OVERLAY_FONT_SIZE);
		DrawText (text, (gint) (width - (gfloat) tw - OVERLAY_PAD * 2.0f), (gint) (OVERLAY_PAD * 1.5f),
		          OVERLAY_FONT_SIZE, COL_OVERLAY0);
	}
}

/*
 * The editor-only 3D drawing of one node, after the runtime renderer drew
 * every node's geometry: tilemap grids, placeholders for missing assets
 * and prefab instances, the node icon and the selection bounds.
 */
static void
draw_node_overlay (LrgEditorSceneRenderer *self,
                   const OverlayEntry     *entry)
{
	LrgNode       *node = entry->node;
	LrgNodeVisual *visual = lrg_node_get_visual (node);

	if (visual != NULL)
	{
		const gchar *asset = lrg_node_visual_get_asset (visual);

		switch (lrg_node_visual_get_kind (visual))
		{
		case LRG_NODE_VISUAL_MESH_ASSET:
			/* Missing asset: a magenta placeholder the size of the bounds */
			if (asset == NULL || lrg_world3d_renderer_get_model (self->runtime, asset) == NULL)
			{
				push_local (entry->world);
				draw_unit_wire_box (0.5f, 0.5f, 0.5f, MAGENTA);
				rlPopMatrix ();
			}
			break;

		case LRG_NODE_VISUAL_SPRITE:
			if (asset == NULL || lrg_world3d_renderer_get_texture (self->runtime, asset) == NULL)
			{
				push_local (entry->world);
				draw_unit_wire_box (0.5f, 0.5f, 0.0f, MAGENTA);
				rlPopMatrix ();
			}
			break;

		case LRG_NODE_VISUAL_TILEMAP:
			if (lrg_node_visual_get_tilemap (visual) != NULL)
			{
				draw_tilemap (self, node, visual, entry->world);
				break;
			}
			/* No grid yet: a framed placeholder. */
			push_local (entry->world);
			draw_unit_wire_box (0.5f, 0.5f, 0.0f, (Color) { 120, 200, 140, 255 });
			DrawLine3D ((Vector3) { -0.5f, 0.0f, 0.0f }, (Vector3) { 0.5f, 0.0f, 0.0f }, (Color) { 120, 200, 140, 120 });
			DrawLine3D ((Vector3) { 0.0f, -0.5f, 0.0f }, (Vector3) { 0.0f, 0.5f, 0.0f }, (Color) { 120, 200, 140, 120 });
			rlPopMatrix ();
			break;

		case LRG_NODE_VISUAL_PREFAB_INSTANCE:
			push_local (entry->world);
			draw_unit_wire_box (0.5f, 0.5f, 0.5f, (Color) { 137, 220, 235, 255 });
			rlPopMatrix ();
			break;

		default:
			break;
		}
	}

	if (self->cur_icons)
		draw_node_icon (self, node, entry->world, entry->selected);

	/* Selection: the picking bounds, primary brighter; overlay BOUNDS
	 * shows them faintly for every node. */
	if (entry->selected || self->cur_bounds)
	{
		gfloat min[3], max[3];

		lrg_editor_node_get_local_bounds (node, min, max);
		push_local (entry->world);
		DrawCubeWires ((Vector3) { (min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f },
		               (max[0] - min[0]) * 1.02f, (max[1] - min[1]) * 1.02f, (max[2] - min[2]) * 1.02f,
		               node == self->cur_primary ? (Color) { 250, 179, 135, 255 }
		               : entry->selected ? (Color) { 249, 226, 175, 200 } : (Color) { 166, 173, 200, 90 });
		rlPopMatrix ();
	}
}

/* "draw-background": the grid, behind the scene's geometry. */
static void
on_draw_background (LrgWorld3DRenderer   *runtime,
                    const LrgWorld3DView *view,
                    gpointer              user_data)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (user_data);

	(void) runtime;
	(void) view;
	if (self->cur_view != NULL && self->cur_grid)
		draw_grid (self, self->cur_view, self->cur_view_2d);
}

/* "draw-overlay": the per-node editor drawing (not while the play world
 * is drawn: its entities are not the authoring nodes). */
static void
on_draw_overlay (LrgWorld3DRenderer   *runtime,
                 const LrgWorld3DView *view,
                 gpointer              user_data)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (user_data);
	guint                   i;

	(void) runtime;
	(void) view;
	if (self->cur_view == NULL || !self->cur_nodes)
		return;
	for (i = 0; i < self->overlay->len; i++)
		draw_node_overlay (self, &g_array_index (self->overlay, OverlayEntry, i));
}

/* ==========================================================================
 * Thumbnails
 * ========================================================================== */

/* Reads a render texture back into a top-down RGBA8 GrlImage. */
static GrlImage *
read_back (RenderTexture2D rt)
{
	Image     img;
	GrlImage *out;

	img = LoadImageFromTexture (rt.texture);
	ImageFlipVertical (&img);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	out = grl_image_new_from_pixels (img.width, img.height, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, img.data);
	UnloadImage (img);
	return out;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_scene_renderer_dispose (GObject *object)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (object);

	/* GPU objects only with a live context; otherwise drop bookkeeping. */
	release_resources (self, IsWindowReady ());

	if (self->runtime != NULL)
	{
		g_clear_signal_handler (&self->bg_handler, self->runtime);
		g_clear_signal_handler (&self->overlay_handler, self->runtime);
	}
	g_clear_object (&self->runtime);
	g_clear_pointer (&self->list, lrg_render_list_unref);
	if (self->settings != NULL)
		g_signal_handlers_disconnect_by_data (self->settings, self);
	g_clear_object (&self->settings);
	g_clear_object (&self->gizmo);
	g_clear_object (&self->play);
	g_clear_object (&self->editor);

	G_OBJECT_CLASS (lrg_editor_scene_renderer_parent_class)->dispose (object);
}

static void
lrg_editor_scene_renderer_finalize (GObject *object)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (object);
	guint                   i;

	for (i = 0; i < N_POST_SLOTS; i++)
		g_clear_object (&self->post[i]);
	g_clear_pointer (&self->overlay, g_array_unref);

	G_OBJECT_CLASS (lrg_editor_scene_renderer_parent_class)->finalize (object);
}

static void
lrg_editor_scene_renderer_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		g_value_set_object (value, self->editor);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_scene_renderer_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		/* Construct-only. */
		g_set_object (&self->editor, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_scene_renderer_class_init (LrgEditorSceneRendererClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_editor_scene_renderer_dispose;
	object_class->finalize = lrg_editor_scene_renderer_finalize;
	object_class->get_property = lrg_editor_scene_renderer_get_property;
	object_class->set_property = lrg_editor_scene_renderer_set_property;

	/**
	 * LrgEditorSceneRenderer:editor:
	 *
	 * The #LrgEditor whose level, selection and tool are drawn.
	 */
	properties[PROP_EDITOR] =
		g_param_spec_object ("editor", NULL, NULL, LRG_TYPE_EDITOR,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_editor_scene_renderer_init (LrgEditorSceneRenderer *self)
{
	guint i;

	self->grid_size = 1.0;
	self->grid_extent = 50;
	self->grid_dirty = TRUE;
	self->draw_overlays = TRUE;

	/* The shared renderer; its hooks draw the editor-only 3D parts */
	self->runtime = lrg_world3d_renderer_new ();
	self->list = lrg_render_list_new ();
	self->overlay = g_array_new (FALSE, FALSE, sizeof (OverlayEntry));
	self->bg_handler = g_signal_connect (self->runtime, "draw-background", G_CALLBACK (on_draw_background), self);
	self->overlay_handler = g_signal_connect (self->runtime, "draw-overlay", G_CALLBACK (on_draw_overlay), self);
	for (i = 0; i < N_POST_SLOTS; i++)
		self->post[i] = lrg_world3d_renderer_new_preview_post_processor ();
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorSceneRenderer *
lrg_editor_scene_renderer_new (LrgEditor *editor)
{
	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);

	return (LrgEditorSceneRenderer *) g_object_new (LRG_TYPE_EDITOR_SCENE_RENDERER, "editor", editor, NULL);
}

LrgEditor *
lrg_editor_scene_renderer_get_editor (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return self->editor;
}

void
lrg_editor_scene_renderer_set_settings (LrgEditorSceneRenderer *self,
                                        LrgEditorSettings      *settings)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));
	g_return_if_fail (settings == NULL || G_IS_OBJECT (settings));

	if (self->settings == settings)
		return;

	/* Swap the notify handlers along with the reference. */
	if (self->settings != NULL)
		g_signal_handlers_disconnect_by_data (self->settings, self);
	g_set_object (&self->settings, settings);
	if (self->settings != NULL)
	{
		g_signal_connect (self->settings, "notify::grid-size", G_CALLBACK (grid_settings_changed), self);
		g_signal_connect (self->settings, "notify::grid-extent", G_CALLBACK (grid_settings_changed), self);
	}
	self->grid_dirty = TRUE;
}

void
lrg_editor_scene_renderer_set_base_dir (LrgEditorSceneRenderer *self,
                                        const gchar            *dir)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));

	/* The runtime renderer forgets its per-string lookups and bumps its
	 * generation (part of ours) */
	lrg_world3d_renderer_set_base_dir (self->runtime, dir);
}

const gchar *
lrg_editor_scene_renderer_get_base_dir (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return lrg_world3d_renderer_get_base_dir (self->runtime);
}

void
lrg_editor_scene_renderer_set_gizmo (LrgEditorSceneRenderer *self,
                                     LrgEditorGizmo         *gizmo)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));
	g_return_if_fail (gizmo == NULL || LRG_IS_EDITOR_GIZMO (gizmo));

	g_set_object (&self->gizmo, gizmo);
}

void
lrg_editor_scene_renderer_set_playing (LrgEditorSceneRenderer *self,
                                       gboolean                playing)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));

	self->playing = playing ? TRUE : FALSE;
}

gboolean
lrg_editor_scene_renderer_get_playing (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), FALSE);

	return self->playing;
}

void
lrg_editor_scene_renderer_set_play_session (LrgEditorSceneRenderer *self,
                                            LrgEditorPlaySession   *session)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));
	g_return_if_fail (session == NULL || LRG_IS_EDITOR_PLAY_SESSION (session));

	g_set_object (&self->play, session);
}

LrgEditorPlaySession *
lrg_editor_scene_renderer_get_play_session (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return self->play;
}

LrgWorld3DRenderer *
lrg_editor_scene_renderer_get_runtime_renderer (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return self->runtime;
}

guint
lrg_editor_scene_renderer_sync_lights (LrgEditorSceneRenderer *self,
                                       gboolean                scene_lighting)
{
	g_autoptr(GArray) lights = NULL;
	LrgLevel         *level;

	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), 0);

	/* The same light descriptions a render puts into its list */
	lights = g_array_new (FALSE, FALSE, sizeof (LrgLight3DDesc));
	level = (self->editor != NULL) ? lrg_editor_get_level (self->editor) : NULL;
	if (scene_lighting && level != NULL && lrg_level_get_root (level) != NULL)
	{
		NodeWalk walk;

		memset (&walk, 0, sizeof walk);
		walk.self = self;
		walk.lights = lights;
		walk.scene_lighting = TRUE;
		walk_level (&walk, level);
	}
	return lrg_world3d_renderer_sync_lights (self->runtime, (const LrgLight3DDesc *) (gpointer) lights->data,
	                                         lights->len);
}

gboolean
lrg_editor_scene_renderer_has_scene_sun (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), FALSE);

	return lrg_world3d_renderer_has_scene_sun (self->runtime);
}

/*
 * The whole render: the authoring nodes (or the play world) through the
 * runtime renderer, then the legacy 2D overlays.
 */
static gboolean
render_core (LrgEditorSceneRenderer    *self,
             const LrgEditorView       *in_view,
             const LrgEditorViewCamera *cam,
             gboolean                   view_2d,
             guint                      slot,
             LrgEditorRenderMode        mode,
             LrgEditorViewOverlays      overlays,
             gboolean                   tile_mode,
             LrgEditorViewportNav      *nav,
             gboolean                   active,
             gfloat                     width,
             gfloat                     height)
{
	LrgEditorView          v;
	LrgWorld3DView         rv;
	LrgLevel              *level;
	LrgWorld3D            *world = NULL;
	LrgWorld3DRenderFlags  flags = LRG_WORLD3D_RENDER_FLAG_CLEAR;
	LrgEditorSelection    *selection;
	GrlColor               bg;
	gboolean               perspective;

	self->drawn = 0;
	if (!IsWindowReady ())
		return FALSE;

	v = *in_view;
	v.width = MAX (width, 1.0f);
	v.height = MAX (height, 1.0f);
	perspective = !v.orthographic && !view_2d;
	level = (self->editor != NULL) ? lrg_editor_get_level (self->editor) : NULL;
	if (level != NULL && lrg_level_get_root (level) == NULL)
		level = NULL;
	if (self->play != NULL)
		world = lrg_editor_play_session_get_world3d (self->play);

	/* --- The editor camera as the runtime renderer's view ---------------- */
	lrg_world3d_view_init (&rv, v.width, v.height);
	memcpy (rv.position, v.position, sizeof rv.position);
	memcpy (rv.target, v.target, sizeof rv.target);
	memcpy (rv.up, v.up, sizeof rv.up);
	rv.fovy = v.fovy;
	rv.ortho_height = v.ortho_height;
	rv.orthographic = v.orthographic;
	rv.near_plane = v.near_plane;
	rv.far_plane = v.far_plane;
	rv.cull_mask = LRG_RENDER_LAYERS_MASK_ALL;

	/* --- Viewport overlays -> render settings ---------------------------- */
	if ((overlays & LRG_EDITOR_VIEW_OVERLAY_SHADOWS) && perspective)
		flags |= LRG_WORLD3D_RENDER_FLAG_SHADOWS;
	if ((overlays & LRG_EDITOR_VIEW_OVERLAY_SKY) && perspective)
		flags |= LRG_WORLD3D_RENDER_FLAG_SKY;
	if ((overlays & LRG_EDITOR_VIEW_OVERLAY_POST) && slot < N_POST_SLOTS)
	{
		flags |= LRG_WORLD3D_RENDER_FLAG_POST;
		lrg_world3d_renderer_set_post_processor (self->runtime, self->post[slot]);
	}
	else
		lrg_world3d_renderer_set_post_processor (self->runtime, NULL);
	lrg_world3d_renderer_set_flags (self->runtime, flags);
	lrg_world3d_renderer_set_render_mode (self->runtime, (LrgWorld3DRenderMode) mode);
	bg.r = self->playing ? COL_BG_PLAY.r : COL_BG_EDIT.r;
	bg.g = self->playing ? COL_BG_PLAY.g : COL_BG_EDIT.g;
	bg.b = self->playing ? COL_BG_PLAY.b : COL_BG_EDIT.b;
	bg.a = 255;
	lrg_world3d_renderer_set_background (self->runtime, &bg);

	/* --- What the hooks draw ------------------------------------------- */
	selection = (self->editor != NULL) ? lrg_editor_get_selection (self->editor) : NULL;
	self->cur_view = &v;
	self->cur_view_2d = view_2d;
	self->cur_grid = (overlays & LRG_EDITOR_VIEW_OVERLAY_GRID) != 0;
	self->cur_icons = (overlays & LRG_EDITOR_VIEW_OVERLAY_ICONS) != 0;
	self->cur_bounds = (overlays & LRG_EDITOR_VIEW_OVERLAY_BOUNDS) != 0;
	self->cur_tile_mode = tile_mode;
	self->cur_primary = (selection != NULL) ? lrg_editor_selection_get_primary (selection) : NULL;
	self->tile_draws = 0;
	g_array_set_size (self->overlay, 0);

	if (world != NULL)
	{
		/* Playing a 3D level: the runtime world, exactly as the game
		 * draws it (its own lights) */
		self->cur_nodes = FALSE;
		lrg_world3d_renderer_render_view (self->runtime, world, &rv);
	}
	else
	{
		/* Authoring: the nodes as a transient render list */
		lrg_render_list_clear (self->list);
		lrg_render_list_set_view (self->list, v.position, LRG_RENDER_LAYERS_MASK_ALL, NULL);
		if (level != NULL)
		{
			NodeWalk walk;

			memset (&walk, 0, sizeof walk);
			walk.self = self;
			walk.list = self->list;
			walk.selection = selection;
			walk.scene_lighting = (overlays & LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING) != 0;
			walk_level (&walk, level);
		}
		lrg_render_list_sort (self->list);
		self->cur_nodes = TRUE;
		lrg_world3d_renderer_render_list (self->runtime, self->list, &rv);
	}
	self->drawn = lrg_world3d_renderer_get_drawn (self->runtime) + self->tile_draws;

	/* --- Legacy 2D pass, viewport-local pixels ---------------------------- */
	if (self->draw_overlays && self->editor != NULL)
	{
		if (active && self->gizmo != NULL && (overlays & LRG_EDITOR_VIEW_OVERLAY_GIZMOS) != 0)
			draw_gizmo (self, &v);
		if (nav != NULL)
		{
			if (!view_2d)
				draw_nav_gizmo (nav);
			draw_box_select (nav);
		}
		if (cam != NULL)
			draw_overlay_text (self, cam, mode, (overlays & LRG_EDITOR_VIEW_OVERLAY_STATS) != 0, v.width);
	}
	rlDrawRenderBatchActive ();

	/* The view lives on this stack frame: the hooks must not see it */
	self->cur_view = NULL;
	g_array_set_size (self->overlay, 0);
	return TRUE;
}

gboolean
lrg_editor_scene_renderer_render (LrgEditorSceneRenderer *self,
                                  LrgEditorViewState     *view,
                                  gint                    viewport,
                                  LrgEditorViewportNav   *nav,
                                  gboolean                active,
                                  gfloat                  width,
                                  gfloat                  height)
{
	LrgEditorViewCamera *cam;
	guint                slot;

	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (view), FALSE);
	g_return_val_if_fail (nav == NULL || LRG_IS_EDITOR_VIEWPORT_NAV (nav), FALSE);

	cam = lrg_editor_view_state_get_camera (view, viewport);
	slot = (viewport >= 0) ? (guint) viewport : lrg_editor_view_state_get_active_viewport (view);
	return render_core (self, &cam->view, cam, cam->view_2d, slot,
	                    lrg_editor_view_state_get_render_mode (view, viewport),
	                    lrg_editor_view_state_get_overlays (view, viewport),
	                    lrg_editor_view_state_get_tile_mode (view), nav, active, width, height);
}

gboolean
lrg_editor_scene_renderer_render_view (LrgEditorSceneRenderer *self,
                                       const LrgEditorView    *view,
                                       gboolean                view_2d,
                                       guint                   slot,
                                       LrgEditorRenderMode     mode,
                                       LrgEditorViewOverlays   overlays,
                                       gfloat                  width,
                                       gfloat                  height)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);

	return render_core (self, view, NULL, view_2d, slot, mode, overlays, FALSE, NULL, FALSE, width, height);
}

void
lrg_editor_scene_renderer_set_draw_overlays (LrgEditorSceneRenderer *self,
                                             gboolean                draw_overlays)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));

	self->draw_overlays = draw_overlays ? TRUE : FALSE;
}

gboolean
lrg_editor_scene_renderer_get_draw_overlays (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), FALSE);

	return self->draw_overlays;
}

LrgLightSet *
lrg_editor_scene_renderer_get_light_set (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return lrg_world3d_renderer_get_light_set (self->runtime);
}

LrgShadowRenderer *
lrg_editor_scene_renderer_get_shadow_renderer (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return lrg_world3d_renderer_get_shadow_renderer (self->runtime);
}

LrgSkyDome *
lrg_editor_scene_renderer_get_sky_dome (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return lrg_world3d_renderer_get_sky_dome (self->runtime);
}

LrgPostProcessor *
lrg_editor_scene_renderer_get_post_processor (LrgEditorSceneRenderer *self,
                                              guint                   slot)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	if (slot >= N_POST_SLOTS)
		return NULL;
	return self->post[slot];
}

guint
lrg_editor_scene_renderer_get_generation (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), 0);

	/* Ours (grid settings) plus the runtime's (assets, base directory) */
	return self->generation + lrg_world3d_renderer_get_generation (self->runtime);
}

gboolean
lrg_editor_scene_renderer_is_asset_missing (LrgEditorSceneRenderer *self,
                                            LrgNode                *node)
{
	LrgNodeVisual     *visual;
	LrgNodeVisualKind  kind;
	const gchar       *asset;

	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	visual = lrg_node_get_visual (node);
	if (visual == NULL)
		return FALSE;
	kind = lrg_node_visual_get_kind (visual);
	if (kind != LRG_NODE_VISUAL_MESH_ASSET && kind != LRG_NODE_VISUAL_SPRITE && kind != LRG_NODE_VISUAL_TILEMAP)
		return FALSE;
	asset = lrg_node_visual_get_asset (visual);
	if (asset == NULL || *asset == '\0')
		return kind != LRG_NODE_VISUAL_TILEMAP;   /* a tilemap may use plain swatches */
	return lrg_world3d_renderer_is_asset_missing (self->runtime, asset);
}

void
lrg_editor_scene_renderer_camera_node_view (LrgNode       *node,
                                            gfloat         width,
                                            gfloat         height,
                                            LrgEditorView *out)
{
	LrgNodeVisual *visual;
	gfloat         world[16], fwd[3], up[3];
	const gfloat   minus_z[3] = { 0.0f, 0.0f, -1.0f };
	const gfloat   plus_y[3] = { 0.0f, 1.0f, 0.0f };

	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (out != NULL);

	lrg_editor_view_init_default (out, MAX (width, 1.0f), MAX (height, 1.0f));
	lrg_editor_node_get_world_matrix (node, world);
	lrg_editor_math_transform_direction (world, minus_z, fwd);
	lrg_editor_math_transform_direction (world, plus_y, up);
	normalize3 (fwd);
	normalize3 (up);

	out->position[0] = world[12];
	out->position[1] = world[13];
	out->position[2] = world[14];
	out->target[0] = world[12] + fwd[0];
	out->target[1] = world[13] + fwd[1];
	out->target[2] = world[14] + fwd[2];
	memcpy (out->up, up, sizeof up);
	out->orthographic = FALSE;

	visual = lrg_node_get_visual (node);
	if (visual != NULL)
	{
		out->fovy = (gfloat) CLAMP (lrg_node_visual_get_param_double (visual, "fov", 60.0), 1.0, 179.0);
		out->near_plane = (gfloat) MAX (lrg_node_visual_get_param_double (visual, "near", 0.1), 0.001);
		out->far_plane = (gfloat) MAX (lrg_node_visual_get_param_double (visual, "far", 1000.0),
		                               out->near_plane + 0.01);
	}
}

const gchar *
lrg_editor_scene_renderer_get_shader_source (const gchar *name)
{
	g_return_val_if_fail (name != NULL, NULL);

	/* The shaders are the runtime renderer's (one copy in the library) */
	return lrg_world3d_renderer_get_shader_source (name);
}

guint
lrg_editor_scene_renderer_get_drawn (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), 0);

	return self->drawn;
}

void
lrg_editor_scene_renderer_forget_asset (LrgEditorSceneRenderer *self,
                                        const gchar            *abs_path)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));

	/* The runtime renderer bumps its generation when it had a copy */
	if (abs_path != NULL)
		lrg_world3d_renderer_forget_asset (self->runtime, abs_path);
}

GrlImage *
lrg_editor_scene_renderer_render_model_thumbnail (LrgEditorSceneRenderer *self,
                                                  const gchar            *abs_path,
                                                  guint                   size)
{
	Model           model;
	BoundingBox     bb;
	RenderTexture2D rt;
	Camera3D        cam;
	GrlImage       *out;
	Shader         *saved;
	Vector3         center;
	gfloat          radius, dist;
	gint            i;

	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	if (abs_path == NULL || size == 0 || !IsWindowReady () || !g_file_test (abs_path, G_FILE_TEST_IS_REGULAR))
		return NULL;

	model = LoadModel (abs_path);
	if (model.meshCount == 0)
	{
		UnloadModel (model);
		return NULL;
	}

	/* Frame the bounding sphere with a 35 degree lens from a 3/4 angle. */
	bb = GetModelBoundingBox (model);
	center = (Vector3) { (bb.min.x + bb.max.x) * 0.5f, (bb.min.y + bb.max.y) * 0.5f, (bb.min.z + bb.max.z) * 0.5f };
	radius = 0.5f * sqrtf ((bb.max.x - bb.min.x) * (bb.max.x - bb.min.x) + (bb.max.y - bb.min.y) * (bb.max.y - bb.min.y) +
	                       (bb.max.z - bb.min.z) * (bb.max.z - bb.min.z));
	radius = MAX (radius, 0.01f);
	dist = radius / tanf (35.0f * 0.5f * (gfloat) G_PI / 180.0f) * 1.05f;

	memset (&cam, 0, sizeof cam);
	cam.target = center;
	cam.position = (Vector3) { center.x + dist * 0.62f, center.y + dist * 0.48f, center.z + dist * 0.62f };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = 35.0f;
	cam.projection = CAMERA_PERSPECTIVE;

	/* Light the model with the fallback lit shader, restoring the model's
	 * own shaders before unloading (UnloadModel frees non-default
	 * shaders). */
	ensure_thumb_shader (self);
	saved = g_new (Shader, MAX (model.materialCount, 1));
	for (i = 0; i < model.materialCount; i++)
	{
		saved[i] = model.materials[i].shader;
		if (self->has_thumb_lit)
			model.materials[i].shader = self->thumb_lit;
	}
	if (self->has_thumb_lit)
	{
		const gfloat dir[3] = { -0.40f, -0.80f, -0.45f };
		gfloat       ambient = 0.4f;

		SetShaderValue (self->thumb_lit, self->thumb_loc_light, dir, SHADER_UNIFORM_VEC3);
		SetShaderValue (self->thumb_lit, self->thumb_loc_ambient, &ambient, SHADER_UNIFORM_FLOAT);
	}

	out = NULL;
	rt = LoadRenderTexture ((gint) size, (gint) size);
	if (IsRenderTextureValid (rt))
	{
		BeginTextureMode (rt);
		ClearBackground (COL_BG_EDIT);
		BeginMode3D (cam);
		DrawModel (model, (Vector3) { 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
		EndMode3D ();
		EndTextureMode ();
		out = read_back (rt);
		UnloadRenderTexture (rt);
	}

	for (i = 0; i < model.materialCount; i++)
		model.materials[i].shader = saved[i];
	g_free (saved);
	UnloadModel (model);
	return out;
}

GrlImage *
lrg_editor_scene_renderer_render_font_thumbnail (LrgEditorSceneRenderer *self,
                                                 const gchar            *abs_path,
                                                 guint                   size)
{
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autoptr(GArray)       cps = NULL;
	g_autofree gchar       *ranges = NULL;
	g_autofree gint        *codepoints = NULL;
	gint                    n_codepoints = 0;
	guint                   font_size = FONT_DEFAULT_SIZE;
	Font                    font;
	Vector2                 m;
	RenderTexture2D         rt;
	GrlImage               *out;
	gfloat                  scale, draw_size, spacing;
	guint                   i;

	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	if (abs_path == NULL || size == 0 || !IsWindowReady () || !g_file_test (abs_path, G_FILE_TEST_IS_REGULAR))
		return NULL;

	/* Import settings from the sidecar; defaults when there is none (or
	 * it does not parse). */
	meta = lrg_asset_meta_load_for_asset (abs_path, NULL);
	if (meta != NULL)
	{
		font_size = lrg_asset_meta_get_font_size (meta);
		ranges = lrg_asset_meta_get_font_codepoints (meta);
	}
	if (font_size == 0)
		font_size = FONT_DEFAULT_SIZE;
	if (ranges == NULL)
		ranges = g_strdup (FONT_DEFAULT_CPS);
	cps = lrg_asset_meta_parse_codepoints (ranges, NULL);
	if (cps != NULL && cps->len > 0)
	{
		codepoints = g_new (gint, cps->len);
		for (i = 0; i < cps->len; i++)
			codepoints[i] = (gint) g_array_index (cps, guint32, i);
		n_codepoints = (gint) cps->len;
	}

	/* A failed load returns an empty font (or, for some formats, the
	 * default font): neither is a sample of this file. */
	font = LoadFontEx (abs_path, (gint) font_size, codepoints, n_codepoints);
	if (font.texture.id == 0 || font.glyphCount == 0 || font.texture.id == GetFontDefault ().texture.id)
	{
		UnloadFont (font);
		return NULL;
	}
	SetTextureFilter (font.texture, TEXTURE_FILTER_BILINEAR);

	/* Fit the sample into 86% of the width and half the height. */
	spacing = (gfloat) font_size / 16.0f;
	m = MeasureTextEx (font, FONT_SAMPLE_TEXT, (gfloat) font_size, spacing);
	scale = MIN ((gfloat) size * 0.86f / MAX (m.x, 1.0f), (gfloat) size * 0.5f / MAX (m.y, 1.0f));
	draw_size = (gfloat) font_size * scale;
	spacing *= scale;
	m = MeasureTextEx (font, FONT_SAMPLE_TEXT, draw_size, spacing);

	out = NULL;
	rt = LoadRenderTexture ((gint) size, (gint) size);
	if (IsRenderTextureValid (rt))
	{
		BeginTextureMode (rt);
		ClearBackground (COL_BG_EDIT);
		DrawTextEx (font, FONT_SAMPLE_TEXT, v2 (((gfloat) size - m.x) * 0.5f, ((gfloat) size - m.y) * 0.5f), draw_size,
		            spacing, COL_TEXT);
		EndTextureMode ();
		out = read_back (rt);
		UnloadRenderTexture (rt);
	}
	UnloadFont (font);
	return out;
}

void
lrg_editor_scene_renderer_release (LrgEditorSceneRenderer *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self));

	release_resources (self, IsWindowReady ());
}
