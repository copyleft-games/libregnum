/* lrg-editor-scene-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSceneRenderer: the editor's viewport drawing, ported out of
 * the immediate-mode editor (lrg-editor-render.c and the overlays of
 * lrg-editor-panel-viewport.c) into a standalone object.
 *
 * lrg_editor_scene_renderer_render() runs inside an LrgViewport render
 * callback, i.e. with the viewport's render texture already bound. The
 * camera matrices come from LrgEditorView (lrg-editor-math.h), the same
 * ones picking uses, and are loaded straight into rlgl. Primitives are
 * unit-sized meshes matching lrg_editor_node_get_local_bounds(), drawn
 * with each node's world matrix and a small lit shader; mesh and sprite
 * assets are loaded on demand and cached by path. After the 3D pass the
 * transform gizmo, orientation axes, box-select rectangle and overlay
 * text are drawn in viewport-local pixels with plain raylib 2D calls, so
 * the gizmo is pixel-identical to its hit testing.
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
#include "../lrg-level.h"
#include "../lrg-node.h"
#include "../lrg-node-private.h"
#include "../lrg-node-visual.h"
#include "../lrg-node-tilemap.h"
#include "../lrg-asset-meta.h"
#include "../../scene/lrg-material3d.h"
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define RENDER_LOG_DOMAIN "Libregnum-Editor-Render"

#define TORUS_MAJOR       (0.375f)
#define TORUS_MINOR       (0.125f)
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

struct _LrgEditorSceneRenderer
{
	GObject            parent_instance;

	LrgEditor         *editor;
	LrgEditorSettings *settings;
	LrgEditorGizmo    *gizmo;
	gchar             *base_dir;
	gboolean           playing;

	/* GPU resources, valid while has_gl. */
	gboolean           has_gl;
	Shader             lit;
	gboolean           has_lit;
	gint               loc_light;
	gint               loc_ambient;
	Material           material;
	Texture2D          white;
	Mesh               cube;
	Mesh               sphere;
	Mesh               plane;
	Mesh               cylinder;
	Mesh               cone;
	Mesh               torus;
	Mesh               disc;

	GHashTable        *models;     /* abs path -> Model*, NULL value = failed */
	GHashTable        *textures;   /* abs path -> Texture2D*, NULL value = failed */
	/* Per-frame lookups by the node's asset string as written (no path
	 * building): values are borrowed from models/textures, NULL = failed.
	 * Valid for base_dir only; cleared when it changes. */
	GHashTable        *model_by_asset;
	GHashTable        *texture_by_asset;

	/* grid-size / grid-extent, refreshed on notify instead of a
	 * g_object_get() by name every frame. */
	gdouble            grid_size;
	guint              grid_extent;
	gboolean           grid_dirty;

	/* Node count for the stats line, recounted when the tree changes. */
	guint              node_count;
	guint              node_count_serial;
	LrgNode           *node_count_root;
	gboolean           node_count_valid;

	guint              drawn;
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
 * Cache value destructors
 * ========================================================================== */

/*
 * Cached models and textures are GPU objects: unloading them needs the GL
 * context. When the window is already gone only the bookkeeping is freed
 * (the driver reclaims the GPU memory with the context).
 */
static void
model_free (gpointer data)
{
	Model *m = (Model *) data;

	if (m == NULL)
		return;
	if (IsWindowReady ())
		UnloadModel (*m);
	g_free (m);
}

static void
texture_free (gpointer data)
{
	Texture2D *t = (Texture2D *) data;

	if (t == NULL)
		return;
	if (IsWindowReady ())
		UnloadTexture (*t);
	g_free (t);
}

/* ==========================================================================
 * Small math helpers (raymath.h is not gnu89-clean)
 * ========================================================================== */

static Matrix
to_matrix (const gfloat *m)
{
	Matrix out;

	/* LrgEditor matrices are column-major like OpenGL; raylib's fields
	 * are named by that same storage index. */
	out.m0 = m[0];   out.m1 = m[1];   out.m2 = m[2];   out.m3 = m[3];
	out.m4 = m[4];   out.m5 = m[5];   out.m6 = m[6];   out.m7 = m[7];
	out.m8 = m[8];   out.m9 = m[9];   out.m10 = m[10]; out.m11 = m[11];
	out.m12 = m[12]; out.m13 = m[13]; out.m14 = m[14]; out.m15 = m[15];
	return out;
}

static void
from_matrix (Matrix  m,
             gfloat *out)
{
	out[0] = m.m0;   out[1] = m.m1;   out[2] = m.m2;   out[3] = m.m3;
	out[4] = m.m4;   out[5] = m.m5;   out[6] = m.m6;   out[7] = m.m7;
	out[8] = m.m8;   out[9] = m.m9;   out[10] = m.m10; out[11] = m.m11;
	out[12] = m.m12; out[13] = m.m13; out[14] = m.m14; out[15] = m.m15;
}

/* raylib convention: mat_mul (a, b) applies a first, then b. */
static Matrix
mat_mul (Matrix a,
         Matrix b)
{
	gfloat x[16], y[16], o[16];

	from_matrix (a, x);
	from_matrix (b, y);
	lrg_editor_math_multiply (y, x, o);
	return to_matrix (o);
}

static Matrix
mat_translate (gfloat x,
               gfloat y,
               gfloat z)
{
	gfloat loc[3] = { x, y, z }, rot[3] = { 0.0f, 0.0f, 0.0f }, scl[3] = { 1.0f, 1.0f, 1.0f };
	gfloat m[16];

	lrg_editor_math_compose (loc, rot, scl, m);
	return to_matrix (m);
}

static Matrix
mat_rotate_x (gfloat angle)
{
	gfloat loc[3] = { 0.0f, 0.0f, 0.0f }, rot[3] = { angle, 0.0f, 0.0f }, scl[3] = { 1.0f, 1.0f, 1.0f };
	gfloat m[16];

	lrg_editor_math_compose (loc, rot, scl, m);
	return to_matrix (m);
}

static Matrix
mat_scale (gfloat x,
           gfloat y,
           gfloat z)
{
	gfloat loc[3] = { 0.0f, 0.0f, 0.0f }, rot[3] = { 0.0f, 0.0f, 0.0f }, scl[3] = { x, y, z };
	gfloat m[16];

	lrg_editor_math_compose (loc, rot, scl, m);
	return to_matrix (m);
}

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

/* ==========================================================================
 * GPU setup and teardown
 * ========================================================================== */

static const gchar *lit_vs =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"in vec3 vertexNormal;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 matNormal;\n"
	"out vec2 fragTexCoord;\n"
	"out vec3 fragNormal;\n"
	"void main ()\n"
	"{\n"
	"    fragTexCoord = vertexTexCoord;\n"
	"    fragNormal = normalize (vec3 (matNormal * vec4 (vertexNormal, 0.0)));\n"
	"    gl_Position = mvp * vec4 (vertexPosition, 1.0);\n"
	"}\n";

static const gchar *lit_fs =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec3 fragNormal;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec3 lightDir;\n"
	"uniform float ambient;\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    vec4 base = texture (texture0, fragTexCoord) * colDiffuse;\n"
	"    float d = abs (dot (normalize (fragNormal), -lightDir));\n"
	"    float l = ambient + (1.0 - ambient) * d;\n"
	"    if (base.a < 0.01) discard;\n"
	"    finalColor = vec4 (base.rgb * l, base.a);\n"
	"}\n";

/* A torus around +Y, sized to the picking bounds. */
static Mesh
gen_torus (gint major_segments,
           gint minor_segments)
{
	Mesh mesh;
	gint i, j, v = 0, t = 0;

	memset (&mesh, 0, sizeof mesh);
	mesh.vertexCount = (major_segments + 1) * (minor_segments + 1);
	mesh.triangleCount = major_segments * minor_segments * 2;
	mesh.vertices = (gfloat *) MemAlloc ((guint) mesh.vertexCount * 3 * sizeof (gfloat));
	mesh.normals = (gfloat *) MemAlloc ((guint) mesh.vertexCount * 3 * sizeof (gfloat));
	mesh.texcoords = (gfloat *) MemAlloc ((guint) mesh.vertexCount * 2 * sizeof (gfloat));
	mesh.indices = (gushort *) MemAlloc ((guint) mesh.triangleCount * 3 * sizeof (gushort));

	/* Vertices: a ring of minor circles around the major circle. */
	for (i = 0; i <= major_segments; i++)
	{
		gfloat u = (gfloat) i / (gfloat) major_segments * 2.0f * (gfloat) G_PI;

		for (j = 0; j <= minor_segments; j++)
		{
			gfloat w = (gfloat) j / (gfloat) minor_segments * 2.0f * (gfloat) G_PI;
			gfloat nx = cosf (w) * cosf (u);
			gfloat ny = sinf (w);
			gfloat nz = cosf (w) * sinf (u);

			mesh.vertices[v * 3 + 0] = (TORUS_MAJOR + TORUS_MINOR * cosf (w)) * cosf (u);
			mesh.vertices[v * 3 + 1] = TORUS_MINOR * ny;
			mesh.vertices[v * 3 + 2] = (TORUS_MAJOR + TORUS_MINOR * cosf (w)) * sinf (u);
			mesh.normals[v * 3 + 0] = nx;
			mesh.normals[v * 3 + 1] = ny;
			mesh.normals[v * 3 + 2] = nz;
			mesh.texcoords[v * 2 + 0] = (gfloat) i / (gfloat) major_segments;
			mesh.texcoords[v * 2 + 1] = (gfloat) j / (gfloat) minor_segments;
			v++;
		}
	}

	/* Two triangles per quad of the grid. */
	for (i = 0; i < major_segments; i++)
		for (j = 0; j < minor_segments; j++)
		{
			gushort a = (gushort) (i * (minor_segments + 1) + j);
			gushort b = (gushort) ((i + 1) * (minor_segments + 1) + j);

			mesh.indices[t++] = a;
			mesh.indices[t++] = (gushort) (a + 1);
			mesh.indices[t++] = b;
			mesh.indices[t++] = b;
			mesh.indices[t++] = (gushort) (a + 1);
			mesh.indices[t++] = (gushort) (b + 1);
		}

	UploadMesh (&mesh, FALSE);
	return mesh;
}

/*
 * Creates the shader, meshes and default material on first use. Returns
 * FALSE without a window (nothing can be created then).
 */
static gboolean
ensure_gl (LrgEditorSceneRenderer *self)
{
	if (self->has_gl)
		return TRUE;
	if (!IsWindowReady ())
		return FALSE;

	self->cube = GenMeshCube (1.0f, 1.0f, 1.0f);
	self->sphere = GenMeshSphere (0.5f, 16, 24);
	self->plane = GenMeshPlane (1.0f, 1.0f, 1, 1);
	self->cylinder = GenMeshCylinder (0.5f, 1.0f, 24);
	self->cone = GenMeshCone (0.5f, 1.0f, 24);
	self->torus = gen_torus (32, 12);
	self->disc = GenMeshPoly (32, 0.5f);

	self->material = LoadMaterialDefault ();
	self->white = self->material.maps[MATERIAL_MAP_DIFFUSE].texture;

	/* The lit shader is optional: without GL 3.3 everything is flat. */
	self->lit = LoadShaderFromMemory (lit_vs, lit_fs);
	self->has_lit = IsShaderValid (self->lit) && self->lit.id != rlGetShaderIdDefault ();
	if (self->has_lit)
	{
		self->loc_light = GetShaderLocation (self->lit, "lightDir");
		self->loc_ambient = GetShaderLocation (self->lit, "ambient");
		self->material.shader = self->lit;
	}

	self->has_gl = TRUE;
	return TRUE;
}

/*
 * Frees the GPU resources. With @gl FALSE (no context any more) only the
 * CPU-side bookkeeping is dropped: GL calls without a context crash.
 */
static void
release_resources (LrgEditorSceneRenderer *self,
                   gboolean                gl)
{
	/* The by-asset maps borrow their values: drop them first. The value
	 * destructors of the path maps check the window themselves. */
	if (self->model_by_asset != NULL)
		g_hash_table_remove_all (self->model_by_asset);
	if (self->texture_by_asset != NULL)
		g_hash_table_remove_all (self->texture_by_asset);
	if (self->models != NULL)
		g_hash_table_remove_all (self->models);
	if (self->textures != NULL)
		g_hash_table_remove_all (self->textures);

	if (!self->has_gl)
		return;

	if (gl)
	{
		UnloadMesh (self->cube);
		UnloadMesh (self->sphere);
		UnloadMesh (self->plane);
		UnloadMesh (self->cylinder);
		UnloadMesh (self->cone);
		UnloadMesh (self->torus);
		UnloadMesh (self->disc);
		if (self->has_lit)
			UnloadShader (self->lit);
	}
	/* The default material's shader and texture belong to rlgl; only its
	 * map array is ours (plain CPU memory). */
	MemFree (self->material.maps);
	memset (&self->material, 0, sizeof self->material);
	self->has_lit = FALSE;
	self->has_gl = FALSE;
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
 * Asset caches
 * ========================================================================== */

static Color
material_color (LrgNodeVisual *visual)
{
	LrgMaterial3D *mat = lrg_node_visual_get_material (visual);
	gfloat         rgba[4] = { 0.75f, 0.75f, 0.78f, 1.0f };

	if (mat != NULL)
		lrg_material3d_get_color (mat, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
	return (Color) { (guchar) (CLAMP (rgba[0], 0.0f, 1.0f) * 255.0f), (guchar) (CLAMP (rgba[1], 0.0f, 1.0f) * 255.0f),
	                 (guchar) (CLAMP (rgba[2], 0.0f, 1.0f) * 255.0f), (guchar) (CLAMP (rgba[3], 0.0f, 1.0f) * 255.0f) };
}

/* An asset string as an absolute path: absolute paths unchanged,
 * relative ones against the base directory (or the working directory). */
static gchar *
resolve_path (LrgEditorSceneRenderer *self,
              const gchar            *asset)
{
	if (g_path_is_absolute (asset))
		return g_strdup (asset);
	if (self->base_dir != NULL)
		return g_build_filename (self->base_dir, asset, NULL);
	return g_canonicalize_filename (asset, NULL);
}

static Model *
load_model (LrgEditorSceneRenderer *self,
            const gchar            *asset)
{
	g_autofree gchar *abs = resolve_path (self, asset);
	Model            *model;
	gint              i;

	if (g_hash_table_contains (self->models, abs))
		return (Model *) g_hash_table_lookup (self->models, abs);

	/* Failures are cached too, and reported once. */
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Model not found: %s", abs);
		g_hash_table_insert (self->models, g_steal_pointer (&abs), NULL);
		return NULL;
	}

	model = g_new0 (Model, 1);
	*model = LoadModel (abs);
	if (model->meshCount == 0)
	{
		UnloadModel (*model);
		g_free (model);
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Could not load model %s", abs);
		g_hash_table_insert (self->models, g_steal_pointer (&abs), NULL);
		return NULL;
	}
	if (self->has_lit)
		for (i = 0; i < model->materialCount; i++)
			model->materials[i].shader = self->lit;
	g_hash_table_insert (self->models, g_steal_pointer (&abs), model);
	return model;
}

/* The model for a node's asset string: one hash lookup per node per
 * frame; the path is resolved (and the model loaded) on first use. Without
 * a base directory relative paths resolve against the working directory,
 * which is not tracked, so they always take the resolving path. */
static Model *
get_model (LrgEditorSceneRenderer *self,
           const gchar            *asset)
{
	gpointer value;
	Model   *model;

	if (self->base_dir == NULL && !g_path_is_absolute (asset))
		return load_model (self, asset);
	if (g_hash_table_lookup_extended (self->model_by_asset, asset, NULL, &value))
		return (Model *) value;
	model = load_model (self, asset);
	g_hash_table_insert (self->model_by_asset, g_strdup (asset), model);
	return model;
}

static Texture2D *
load_texture (LrgEditorSceneRenderer *self,
              const gchar            *asset)
{
	g_autofree gchar *abs = resolve_path (self, asset);
	Texture2D        *tex;

	if (g_hash_table_contains (self->textures, abs))
		return (Texture2D *) g_hash_table_lookup (self->textures, abs);

	tex = NULL;
	if (g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		tex = g_new0 (Texture2D, 1);
		if (g_str_has_suffix (abs, ".pep") || g_str_has_suffix (abs, ".PEP"))
		{
			/* raylib's .pep loader is off; graylib decodes it */
			g_autoptr(GrlImage) image = grl_image_new_from_file (abs);

			if (image != NULL && grl_image_is_valid (image))
				*tex = LoadTextureFromImage (*(Image *) grl_image_get_handle (image));
		}
		else
			*tex = LoadTexture (abs);
		if (tex->id == 0)
			g_clear_pointer (&tex, g_free);
	}
	if (tex == NULL)
		g_log (RENDER_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "Could not load texture %s", abs);
	g_hash_table_insert (self->textures, g_steal_pointer (&abs), tex);
	return tex;
}

/* As get_model(), for sprite and tileset textures. */
static Texture2D *
get_texture (LrgEditorSceneRenderer *self,
             const gchar            *asset)
{
	gpointer   value;
	Texture2D *tex;

	if (self->base_dir == NULL && !g_path_is_absolute (asset))
		return load_texture (self, asset);
	if (g_hash_table_lookup_extended (self->texture_by_asset, asset, NULL, &value))
		return (Texture2D *) value;
	tex = load_texture (self, asset);
	g_hash_table_insert (self->texture_by_asset, g_strdup (asset), tex);
	return tex;
}

/* ==========================================================================
 * 3D drawing helpers
 * ========================================================================== */

static void
draw_mesh (LrgEditorSceneRenderer *self,
           Mesh                    mesh,
           Matrix                  transform,
           Color                   color,
           Texture2D              *texture)
{
	self->material.maps[MATERIAL_MAP_DIFFUSE].color = color;
	self->material.maps[MATERIAL_MAP_DIFFUSE].texture = (texture != NULL) ? *texture : self->white;
	DrawMesh (mesh, self->material, transform);
	self->drawn++;
}

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

/* ==========================================================================
 * Scene walk
 * ========================================================================== */

typedef struct
{
	LrgEditorSceneRenderer *self;
	const LrgEditorView    *view;
	LrgEditorSelection     *selection;
	LrgNode                *primary;
	gboolean                show_icons;
	gboolean                tile_mode;
} SceneWalk;

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
draw_tilemap (SceneWalk     *walk,
              LrgNode       *node,
              LrgNodeVisual *visual,
              const gfloat  *world)
{
	LrgEditorSceneRenderer *self = walk->self;
	LrgNodeTilemap         *grid = lrg_node_visual_get_tilemap (visual);
	const gchar            *asset = lrg_node_visual_get_asset (visual);
	Texture2D              *tex = (asset != NULL) ? get_texture (self, asset) : NULL;
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
		rlSetTexture (tex != NULL ? tex->id : self->white.id);
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
		self->drawn++;
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
		if (walk->tile_mode && walk->primary == node && w * h <= 256u * 256u)
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

/* Draws the geometry of one node (primitive, model, sprite, tilemap,
 * placeholder boxes) with its world matrix. */
static void
draw_node_geometry (SceneWalk    *walk,
                    LrgNode      *node,
                    const gfloat *world)
{
	LrgEditorSceneRenderer *self = walk->self;
	LrgNodeVisual          *visual = lrg_node_get_visual (node);
	Matrix                  m = to_matrix (world);
	Color                   col;

	if (visual == NULL)
		return;

	col = material_color (visual);
	switch (lrg_node_visual_get_kind (visual))
	{
	case LRG_NODE_VISUAL_PRIMITIVE:
		switch (lrg_node_visual_get_primitive (visual))
		{
		case LRG_PRIMITIVE_CUBE:
			draw_mesh (self, self->cube, m, col, NULL);
			break;
		case LRG_PRIMITIVE_UV_SPHERE:
		case LRG_PRIMITIVE_ICO_SPHERE:
			draw_mesh (self, self->sphere, m, col, NULL);
			break;
		case LRG_PRIMITIVE_PLANE:
			draw_mesh (self, self->plane, m, col, NULL);
			break;
		case LRG_PRIMITIVE_CIRCLE:
			draw_mesh (self, self->disc, m, col, NULL);
			break;
		case LRG_PRIMITIVE_CYLINDER:
			/* raylib's cylinder and cone stand on y = 0; centre them. */
			draw_mesh (self, self->cylinder, mat_mul (mat_translate (0.0f, -0.5f, 0.0f), m), col, NULL);
			break;
		case LRG_PRIMITIVE_CONE:
			draw_mesh (self, self->cone, mat_mul (mat_translate (0.0f, -0.5f, 0.0f), m), col, NULL);
			break;
		case LRG_PRIMITIVE_TORUS:
			draw_mesh (self, self->torus, m, col, NULL);
			break;
		case LRG_PRIMITIVE_GRID:
			push_local (world);
			DrawGrid (10, 0.1f);
			rlPopMatrix ();
			break;
		case LRG_PRIMITIVE_RECTANGLE_2D:
			draw_mesh (self, self->plane, mat_mul (mat_rotate_x ((gfloat) G_PI * 0.5f), m), col, NULL);
			break;
		case LRG_PRIMITIVE_CIRCLE_2D:
			draw_mesh (self, self->disc, mat_mul (mat_rotate_x ((gfloat) G_PI * 0.5f), m), col, NULL);
			break;
		default:
			push_local (world);
			draw_unit_wire_box (0.5f, 0.5f, 0.5f, col);
			rlPopMatrix ();
			break;
		}
		break;

	case LRG_NODE_VISUAL_MESH_ASSET:
		{
			const gchar *asset = lrg_node_visual_get_asset (visual);
			Model       *model = (asset != NULL) ? get_model (self, asset) : NULL;
			gint         i;

			if (model == NULL)
			{
				/* Missing asset: a magenta placeholder the size of the bounds. */
				push_local (world);
				draw_unit_wire_box (0.5f, 0.5f, 0.5f, MAGENTA);
				rlPopMatrix ();
				break;
			}
			for (i = 0; i < model->meshCount; i++)
			{
				Material mat = model->materials[model->meshMaterial[i]];

				mat.maps[MATERIAL_MAP_DIFFUSE].color = col;
				DrawMesh (model->meshes[i], mat, mat_mul (model->transform, m));
				self->drawn++;
			}
		}
		break;

	case LRG_NODE_VISUAL_SPRITE:
		{
			const gchar *asset = lrg_node_visual_get_asset (visual);
			Texture2D   *tex = (asset != NULL) ? get_texture (self, asset) : NULL;
			Matrix       quad = mat_rotate_x ((gfloat) G_PI * 0.5f);

			/* Keep the image's aspect ratio inside the unit quad. */
			if (tex != NULL && tex->width > 0 && tex->height > 0)
			{
				gfloat aspect = (gfloat) tex->width / (gfloat) tex->height;

				quad = mat_mul (quad, aspect >= 1.0f ? mat_scale (1.0f, 1.0f / aspect, 1.0f)
				                                     : mat_scale (aspect, 1.0f, 1.0f));
			}
			if (lrg_node_visual_get_material (visual) == NULL)
				col = WHITE;
			draw_mesh (self, self->plane, mat_mul (quad, m), col, tex);
			if (tex == NULL)
			{
				push_local (world);
				draw_unit_wire_box (0.5f, 0.5f, 0.0f, MAGENTA);
				rlPopMatrix ();
			}
		}
		break;

	case LRG_NODE_VISUAL_TILEMAP:
		if (lrg_node_visual_get_tilemap (visual) != NULL)
		{
			draw_tilemap (walk, node, visual, world);
			break;
		}
		/* No grid yet: a framed placeholder. */
		push_local (world);
		draw_unit_wire_box (0.5f, 0.5f, 0.0f, (Color) { 120, 200, 140, 255 });
		DrawLine3D ((Vector3) { -0.5f, 0.0f, 0.0f }, (Vector3) { 0.5f, 0.0f, 0.0f }, (Color) { 120, 200, 140, 120 });
		DrawLine3D ((Vector3) { 0.0f, -0.5f, 0.0f }, (Vector3) { 0.0f, 0.5f, 0.0f }, (Color) { 120, 200, 140, 120 });
		rlPopMatrix ();
		break;

	case LRG_NODE_VISUAL_PREFAB_INSTANCE:
		push_local (world);
		draw_unit_wire_box (0.5f, 0.5f, 0.5f, (Color) { 137, 220, 235, 255 });
		rlPopMatrix ();
		break;

	case LRG_NODE_VISUAL_CAD_PART:
		draw_mesh (self, self->cube, m, (Color) { 250, 179, 135, 255 }, NULL);
		break;

	default:
		break;
	}
}

/* The icon of a light, camera, audio emitter or empty node, at a
 * constant on-screen size and oriented like the node. */
static void
draw_node_icon (SceneWalk    *walk,
                LrgNode      *node,
                const gfloat *world,
                gboolean      selected)
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
	scale = lrg_editor_view_pixel_world_size (walk->view, pos) * 120.0f;

	/* Same location and rotation, uniform scale for the icon size. */
	if (!lrg_editor_math_decompose (world, loc, rot, scl))
		return;
	one[0] = one[1] = one[2] = CLAMP (scale, 0.05f, 50.0f);
	lrg_editor_math_compose (loc, rot, one, m);
	push_local (m);
	draw_icon (kind, color);
	rlPopMatrix ();
}

/* Draws @node and its visible descendants. @parent_world is the world
 * matrix of @node's parent, so each node composes one local matrix onto
 * it instead of walking back to the root (O(n), not O(n * depth)). */
static void
draw_subtree (SceneWalk    *walk,
              LrgNode      *node,
              const gfloat *parent_world)
{
	GPtrArray *children;
	gfloat     local[16];
	gfloat     world[16];
	gboolean   selected;
	guint      i;

	if (!lrg_node_get_visible (node))
		return;

	lrg_editor_node_get_local_matrix (node, local);
	lrg_editor_math_multiply (parent_world, local, world);
	selected = (walk->selection != NULL) && lrg_editor_selection_contains (walk->selection, node);

	draw_node_geometry (walk, node, world);
	if (walk->show_icons)
		draw_node_icon (walk, node, world, selected);

	/* Selection: the picking bounds, primary brighter. */
	if (selected)
	{
		gfloat min[3], max[3];

		lrg_editor_node_get_local_bounds (node, min, max);
		push_local (world);
		DrawCubeWires ((Vector3) { (min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f },
		               (max[0] - min[0]) * 1.02f, (max[1] - min[1]) * 1.02f, (max[2] - min[2]) * 1.02f,
		               node == walk->primary ? (Color) { 250, 179, 135, 255 } : (Color) { 249, 226, 175, 200 });
		rlPopMatrix ();
	}

	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
		draw_subtree (walk, (LrgNode *) g_ptr_array_index (children, i), world);
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

	if (self->settings != NULL)
		g_signal_handlers_disconnect_by_data (self->settings, self);
	g_clear_object (&self->settings);
	g_clear_object (&self->gizmo);
	g_clear_object (&self->editor);

	G_OBJECT_CLASS (lrg_editor_scene_renderer_parent_class)->dispose (object);
}

static void
lrg_editor_scene_renderer_finalize (GObject *object)
{
	LrgEditorSceneRenderer *self = LRG_EDITOR_SCENE_RENDERER (object);

	/* The by-asset maps borrow from the path maps: free them first. */
	g_clear_pointer (&self->model_by_asset, g_hash_table_unref);
	g_clear_pointer (&self->texture_by_asset, g_hash_table_unref);
	g_clear_pointer (&self->models, g_hash_table_unref);
	g_clear_pointer (&self->textures, g_hash_table_unref);
	g_clear_pointer (&self->base_dir, g_free);

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
	self->models = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, model_free);
	self->textures = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, texture_free);
	self->model_by_asset = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->texture_by_asset = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->grid_size = 1.0;
	self->grid_extent = 50;
	self->grid_dirty = TRUE;
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

	if (g_strcmp0 (dir, self->base_dir) == 0)
		return;

	/* Relative asset strings now resolve somewhere else. */
	g_hash_table_remove_all (self->model_by_asset);
	g_hash_table_remove_all (self->texture_by_asset);
	g_free (self->base_dir);
	self->base_dir = g_strdup (dir);
}

const gchar *
lrg_editor_scene_renderer_get_base_dir (LrgEditorSceneRenderer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), NULL);

	return self->base_dir;
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

gboolean
lrg_editor_scene_renderer_render (LrgEditorSceneRenderer *self,
                                  LrgEditorViewState     *view,
                                  gint                    viewport,
                                  LrgEditorViewportNav   *nav,
                                  gboolean                active,
                                  gfloat                  width,
                                  gfloat                  height)
{
	LrgEditorViewCamera  *cam;
	LrgEditorView         v;
	LrgEditorRenderMode   mode;
	LrgEditorViewOverlays overlays;
	LrgLevel             *level;
	SceneWalk             walk;
	gfloat                view_m[16], proj_m[16];
	gboolean              wire;

	g_return_val_if_fail (LRG_IS_EDITOR_SCENE_RENDERER (self), FALSE);
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (view), FALSE);
	g_return_val_if_fail (nav == NULL || LRG_IS_EDITOR_VIEWPORT_NAV (nav), FALSE);

	self->drawn = 0;
	if (!ensure_gl (self))
		return FALSE;

	/* Our own copy of the camera, sized to the target. */
	cam = lrg_editor_view_state_get_camera (view, viewport);
	v = cam->view;
	v.width = MAX (width, 1.0f);
	v.height = MAX (height, 1.0f);
	mode = lrg_editor_view_state_get_render_mode (view, viewport);
	overlays = lrg_editor_view_state_get_overlays (view, viewport);
	wire = (mode == LRG_EDITOR_RENDER_MODE_WIREFRAME);

	lrg_editor_view_get_view_matrix (&v, view_m);
	lrg_editor_view_get_projection_matrix (&v, proj_m);

	ClearBackground (self->playing ? COL_BG_PLAY : COL_BG_EDIT);

	/* --- 3D pass ----------------------------------------------------------
	 * BeginMode3D() with our own matrices (the same ones picking uses). */
	rlDrawRenderBatchActive ();
	rlMatrixMode (RL_PROJECTION);
	rlPushMatrix ();
	rlLoadIdentity ();
	rlMultMatrixf (proj_m);
	rlMatrixMode (RL_MODELVIEW);
	rlLoadIdentity ();
	rlMultMatrixf (view_m);
	rlEnableDepthTest ();

	if (self->has_lit)
	{
		const gfloat light[3] = { -0.35f, -0.85f, -0.4f };
		gfloat       len = sqrtf (light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
		gfloat       dir[3];
		gfloat       ambient = 0.35f;

		dir[0] = light[0] / len;
		dir[1] = light[1] / len;
		dir[2] = light[2] / len;
		SetShaderValue (self->lit, self->loc_light, dir, SHADER_UNIFORM_VEC3);
		SetShaderValue (self->lit, self->loc_ambient, &ambient, SHADER_UNIFORM_FLOAT);
	}

	if ((overlays & LRG_EDITOR_VIEW_OVERLAY_GRID) != 0)
		draw_grid (self, &v, cam->view_2d);

	/* Nodes, icons and selection bounds in one walk of the tree. */
	level = (self->editor != NULL) ? lrg_editor_get_level (self->editor) : NULL;
	if (level != NULL && lrg_level_get_root (level) != NULL)
	{
		LrgNode   *root = lrg_level_get_root (level);
		GPtrArray *top;
		gfloat     root_world[16];
		guint      i;

		if (wire)
			rlEnableWireMode ();
		walk.self = self;
		walk.view = &v;
		walk.selection = lrg_editor_get_selection (self->editor);
		walk.primary = (walk.selection != NULL) ? lrg_editor_selection_get_primary (walk.selection) : NULL;
		walk.show_icons = (overlays & LRG_EDITOR_VIEW_OVERLAY_ICONS) != 0;
		walk.tile_mode = lrg_editor_view_state_get_tile_mode (view);
		lrg_editor_node_get_world_matrix (root, root_world);
		top = lrg_node_get_children (root);
		for (i = 0; top != NULL && i < top->len; i++)
			draw_subtree (&walk, (LrgNode *) g_ptr_array_index (top, i), root_world);
		rlDrawRenderBatchActive ();
		if (wire)
			rlDisableWireMode ();
	}

	/* Back to the target's 2D projection (EndMode3D pops ours). */
	EndMode3D ();

	/* --- 2D pass, viewport-local pixels ----------------------------------- */
	if (active && self->gizmo != NULL && self->editor != NULL && (overlays & LRG_EDITOR_VIEW_OVERLAY_GIZMOS) != 0)
		draw_gizmo (self, &v);
	if (nav != NULL)
	{
		if (!cam->view_2d)
			draw_nav_gizmo (nav);
		draw_box_select (nav);
	}
	if (self->editor != NULL)
		draw_overlay_text (self, cam, mode, (overlays & LRG_EDITOR_VIEW_OVERLAY_STATS) != 0, v.width);
	rlDrawRenderBatchActive ();

	return TRUE;
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

	if (abs_path == NULL)
		return;
	if (!g_hash_table_contains (self->models, abs_path) && !g_hash_table_contains (self->textures, abs_path))
		return;

	/* The by-asset maps borrow from the path maps, so they are cleared
	 * wholesale (they refill on the next lookup). */
	g_hash_table_remove_all (self->model_by_asset);
	g_hash_table_remove_all (self->texture_by_asset);
	g_hash_table_remove (self->models, abs_path);
	g_hash_table_remove (self->textures, abs_path);
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

	if (abs_path == NULL || size == 0 || !ensure_gl (self) || !g_file_test (abs_path, G_FILE_TEST_IS_REGULAR))
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

	/* Light the model with the viewport shader, restoring the model's own
	 * shaders before unloading (UnloadModel frees non-default shaders). */
	saved = g_new (Shader, MAX (model.materialCount, 1));
	for (i = 0; i < model.materialCount; i++)
	{
		saved[i] = model.materials[i].shader;
		if (self->has_lit)
			model.materials[i].shader = self->lit;
	}
	if (self->has_lit)
	{
		const gfloat dir[3] = { -0.40f, -0.80f, -0.45f };
		gfloat       ambient = 0.4f;

		SetShaderValue (self->lit, self->loc_light, dir, SHADER_UNIFORM_VEC3);
		SetShaderValue (self->lit, self->loc_ambient, &ambient, SHADER_UNIFORM_FLOAT);
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
