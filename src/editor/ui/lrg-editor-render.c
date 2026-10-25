/* lrg-editor-render.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Viewport rendering (needs a window). Once per frame, before the UI is
 * drawn, editor_render_viewport() renders the level into an offscreen
 * target the size of the viewport panel. The panel queued a draw callback
 * (editor_render_blit) where that image goes, so the GUI composites it
 * under its own overlays; the gizmo is drawn by the callback in screen
 * space, which makes it pixel-identical to the gizmo's hit testing.
 *
 * The camera matrices come from LrgEditorView (lrg-editor-math.h), the
 * same ones picking uses, and are loaded straight into rlgl. Primitives
 * are unit-sized meshes matching lrg_editor_node_get_local_bounds(), drawn
 * with each node's world matrix and a small lit shader; mesh and sprite
 * assets are loaded on demand and cached by path.
 */

#include "lrg-editor-app-private.h"
#include "../../gui/lrg-gui-raylib-private.h"
#include <math.h>
#include <string.h>

#define TORUS_MAJOR   (0.375f)
#define TORUS_MINOR   (0.125f)
#define ICON_SIZE     (0.25f)

typedef struct
{
	RenderTexture2D target;
	gboolean        has_target;
	gint            tw;
	gint            th;
	Shader          lit;
	gboolean        has_lit;
	gint            loc_light;
	gint            loc_ambient;
	Material        material;
	Texture2D       white;
	Mesh            cube;
	Mesh            sphere;
	Mesh            plane;
	Mesh            cylinder;
	Mesh            cone;
	Mesh            torus;
	Mesh            disc;
	GHashTable     *models;     /* abs path -> Model*, NULL value = failed */
	GHashTable     *textures;   /* abs path -> Texture2D*, NULL value = failed */
	/* Per-frame lookups by the node's asset string as written (no path
	 * building): values are borrowed from models/textures, NULL = failed.
	 * Valid for asset_base only; cleared when the base directory that
	 * relative paths resolve against changes. */
	GHashTable     *model_by_asset;
	GHashTable     *texture_by_asset;
	gchar          *asset_base;
	/* grid-size / grid-extent, refreshed on notify instead of a
	 * g_object_get() by name every frame. */
	gdouble         grid_size;
	guint           grid_extent;
	gboolean        grid_dirty;
	guint           drawn;
} EditorRender;

static void
model_free (gpointer data)
{
	Model *m = data;

	if (m == NULL)
		return;
	UnloadModel (*m);
	g_free (m);
}

static void
texture_free (gpointer data)
{
	Texture2D *t = data;

	if (t == NULL)
		return;
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

/* ==========================================================================
 * Setup
 * ========================================================================== */

static void
grid_settings_changed (GObject    *settings,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
	EditorRender *r = user_data;

	r->grid_dirty = TRUE;
}

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
	mesh.vertices = MemAlloc ((guint) mesh.vertexCount * 3 * sizeof (gfloat));
	mesh.normals = MemAlloc ((guint) mesh.vertexCount * 3 * sizeof (gfloat));
	mesh.texcoords = MemAlloc ((guint) mesh.vertexCount * 2 * sizeof (gfloat));
	mesh.indices = MemAlloc ((guint) mesh.triangleCount * 3 * sizeof (gushort));

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

void
editor_render_init (LrgEditorApp *self)
{
	EditorRender *r;

	if (self->render != NULL)
		return;

	r = g_new0 (EditorRender, 1);
	self->render = r;

	r->cube = GenMeshCube (1.0f, 1.0f, 1.0f);
	r->sphere = GenMeshSphere (0.5f, 16, 24);
	r->plane = GenMeshPlane (1.0f, 1.0f, 1, 1);
	r->cylinder = GenMeshCylinder (0.5f, 1.0f, 24);
	r->cone = GenMeshCone (0.5f, 1.0f, 24);
	r->torus = gen_torus (32, 12);
	r->disc = GenMeshPoly (32, 0.5f);

	r->material = LoadMaterialDefault ();
	r->white = r->material.maps[MATERIAL_MAP_DIFFUSE].texture;

	/* The lit shader is optional: without GL 3.3 everything is flat. */
	r->lit = LoadShaderFromMemory (lit_vs, lit_fs);
	r->has_lit = IsShaderValid (r->lit) && r->lit.id != rlGetShaderIdDefault ();
	if (r->has_lit)
	{
		r->loc_light = GetShaderLocation (r->lit, "lightDir");
		r->loc_ambient = GetShaderLocation (r->lit, "ambient");
		r->material.shader = r->lit;
	}

	r->models = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, model_free);
	r->textures = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, texture_free);
	r->model_by_asset = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	r->texture_by_asset = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	r->grid_dirty = TRUE;
	g_signal_connect (self->settings, "notify::grid-size", G_CALLBACK (grid_settings_changed), r);
	g_signal_connect (self->settings, "notify::grid-extent", G_CALLBACK (grid_settings_changed), r);
}

void
editor_render_shutdown (LrgEditorApp *self)
{
	EditorRender *r = self->render;

	if (r == NULL)
		return;

	g_signal_handlers_disconnect_by_data (self->settings, r);
	/* The by-asset maps borrow their values: drop them first. */
	g_hash_table_unref (r->model_by_asset);
	g_hash_table_unref (r->texture_by_asset);
	g_free (r->asset_base);
	g_hash_table_unref (r->models);
	g_hash_table_unref (r->textures);
	UnloadMesh (r->cube);
	UnloadMesh (r->sphere);
	UnloadMesh (r->plane);
	UnloadMesh (r->cylinder);
	UnloadMesh (r->cone);
	UnloadMesh (r->torus);
	UnloadMesh (r->disc);
	if (r->has_lit)
		UnloadShader (r->lit);
	/* The default material's shader and texture belong to rlgl. */
	RL_FREE (r->material.maps);
	if (r->has_target)
		UnloadRenderTexture (r->target);
	g_free (r);
	self->render = NULL;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */


static Color
gui_color (LrgGuiColor c)
{
	return (Color) { (guchar) (c >> 24), (guchar) (c >> 16), (guchar) (c >> 8), (guchar) c };
}

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

/* Clears the by-asset maps when relative asset paths would now resolve
 * somewhere else (project or level directory changed). Once per frame. */
static void
sync_asset_base (LrgEditorApp *self)
{
	EditorRender *r = self->render;
	const gchar  *base = editor_app_base_dir (self);

	if (g_strcmp0 (base, r->asset_base) == 0)
		return;
	g_hash_table_remove_all (r->model_by_asset);
	g_hash_table_remove_all (r->texture_by_asset);
	g_free (r->asset_base);
	r->asset_base = g_strdup (base);
}

static Model *
load_model (LrgEditorApp *self,
            const gchar  *asset)
{
	EditorRender     *r = self->render;
	g_autofree gchar *abs = editor_app_project_path (self, asset);
	Model            *model;
	gint              i;

	if (g_hash_table_contains (r->models, abs))
		return g_hash_table_lookup (r->models, abs);

	/* Failures are cached too, and reported once. */
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "render", abs);
		g_hash_table_insert (r->models, g_steal_pointer (&abs), NULL);
		return NULL;
	}

	model = g_new0 (Model, 1);
	*model = LoadModel (abs);
	if (model->meshCount == 0)
	{
		g_free (model);
		lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "render", abs);
		g_hash_table_insert (r->models, g_steal_pointer (&abs), NULL);
		return NULL;
	}
	if (r->has_lit)
		for (i = 0; i < model->materialCount; i++)
			model->materials[i].shader = r->lit;
	g_hash_table_insert (r->models, g_steal_pointer (&abs), model);
	return model;
}

/* The model for a node's asset string: one hash lookup per node per
 * frame; the path is resolved (and the model loaded) on first use. A
 * project-less editor resolves against the working directory, which is
 * not tracked, so it always takes the resolving path. */
static Model *
get_model (LrgEditorApp *self,
           const gchar  *asset)
{
	EditorRender *r = self->render;
	gpointer      value;
	Model        *model;

	if (r->asset_base == NULL && !g_path_is_absolute (asset))
		return load_model (self, asset);
	if (g_hash_table_lookup_extended (r->model_by_asset, asset, NULL, &value))
		return value;
	model = load_model (self, asset);
	g_hash_table_insert (r->model_by_asset, g_strdup (asset), model);
	return model;
}

static Texture2D *
load_texture (LrgEditorApp *self,
              const gchar  *asset)
{
	EditorRender     *r = self->render;
	g_autofree gchar *abs = editor_app_project_path (self, asset);
	Texture2D        *tex;

	if (g_hash_table_contains (r->textures, abs))
		return g_hash_table_lookup (r->textures, abs);

	tex = NULL;
	if (g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		tex = g_new0 (Texture2D, 1);
		*tex = LoadTexture (abs);
		if (tex->id == 0)
			g_clear_pointer (&tex, g_free);
	}
	if (tex == NULL)
		lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "render", abs);
	g_hash_table_insert (r->textures, g_steal_pointer (&abs), tex);
	return tex;
}

/* As get_model(), for sprite textures. */
static Texture2D *
get_texture (LrgEditorApp *self,
             const gchar  *asset)
{
	EditorRender *r = self->render;
	gpointer      value;
	Texture2D    *tex;

	if (r->asset_base == NULL && !g_path_is_absolute (asset))
		return load_texture (self, asset);
	if (g_hash_table_lookup_extended (r->texture_by_asset, asset, NULL, &value))
		return value;
	tex = load_texture (self, asset);
	g_hash_table_insert (r->texture_by_asset, g_strdup (asset), tex);
	return tex;
}

static void
draw_mesh (EditorRender *r,
           Mesh          mesh,
           Matrix        transform,
           Color         color,
           Texture2D    *texture)
{
	r->material.maps[MATERIAL_MAP_DIFFUSE].color = color;
	r->material.maps[MATERIAL_MAP_DIFFUSE].texture = (texture != NULL) ? *texture : r->white;
	DrawMesh (mesh, r->material, transform);
	r->drawn++;
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
 * Scene
 * ========================================================================== */

static void
draw_node_geometry (LrgEditorApp *self,
                    LrgNode      *node,
                    const gfloat *world)
{
	EditorRender  *r = self->render;
	LrgNodeVisual *visual = lrg_node_get_visual (node);
	Matrix         m = to_matrix (world);
	Color          col;

	if (visual == NULL)
		return;

	col = material_color (visual);
	switch (lrg_node_visual_get_kind (visual))
	{
	case LRG_NODE_VISUAL_PRIMITIVE:
		switch (lrg_node_visual_get_primitive (visual))
		{
		case LRG_PRIMITIVE_CUBE:
			draw_mesh (r, r->cube, m, col, NULL);
			break;
		case LRG_PRIMITIVE_UV_SPHERE:
		case LRG_PRIMITIVE_ICO_SPHERE:
			draw_mesh (r, r->sphere, m, col, NULL);
			break;
		case LRG_PRIMITIVE_PLANE:
			draw_mesh (r, r->plane, m, col, NULL);
			break;
		case LRG_PRIMITIVE_CIRCLE:
			draw_mesh (r, r->disc, m, col, NULL);
			break;
		case LRG_PRIMITIVE_CYLINDER:
			/* raylib's cylinder and cone stand on y = 0; centre them. */
			draw_mesh (r, r->cylinder, mat_mul (mat_translate (0.0f, -0.5f, 0.0f), m), col, NULL);
			break;
		case LRG_PRIMITIVE_CONE:
			draw_mesh (r, r->cone, mat_mul (mat_translate (0.0f, -0.5f, 0.0f), m), col, NULL);
			break;
		case LRG_PRIMITIVE_TORUS:
			draw_mesh (r, r->torus, m, col, NULL);
			break;
		case LRG_PRIMITIVE_GRID:
			push_local (world);
			DrawGrid (10, 0.1f);
			rlPopMatrix ();
			break;
		case LRG_PRIMITIVE_RECTANGLE_2D:
			draw_mesh (r, r->plane, mat_mul (mat_rotate_x ((gfloat) G_PI * 0.5f), m), col, NULL);
			break;
		case LRG_PRIMITIVE_CIRCLE_2D:
			draw_mesh (r, r->disc, mat_mul (mat_rotate_x ((gfloat) G_PI * 0.5f), m), col, NULL);
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
				r->drawn++;
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
			draw_mesh (r, r->plane, mat_mul (quad, m), col, tex);
			if (tex == NULL)
			{
				push_local (world);
				draw_unit_wire_box (0.5f, 0.5f, 0.0f, MAGENTA);
				rlPopMatrix ();
			}
		}
		break;

	case LRG_NODE_VISUAL_TILEMAP:
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
		draw_mesh (r, r->cube, m, (Color) { 250, 179, 135, 255 }, NULL);
		break;

	default:
		break;
	}
}

static void
draw_node_icon (LrgEditorApp *self,
                LrgNode      *node,
                const gfloat *world,
                gboolean      selected)
{
	LrgNodeVisual    *visual = lrg_node_get_visual (node);
	LrgNodeVisualKind kind = (visual != NULL) ? lrg_node_visual_get_kind (visual) : LRG_NODE_VISUAL_NONE;
	gfloat            pos[3];
	gfloat            scale;
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

	/* Icons keep a constant on-screen size, oriented like the node. The
	 * world position is the translation column of @world (what
	 * lrg_editor_node_get_world_position() would recompute). */
	pos[0] = world[12];
	pos[1] = world[13];
	pos[2] = world[14];
	scale = lrg_editor_view_pixel_world_size (&self->view, pos) * 120.0f;
	{
		gfloat loc[3], rot[3], scl[3], m[16];
		gfloat one[3] = { 1.0f, 1.0f, 1.0f };

		if (!lrg_editor_math_decompose (world, loc, rot, scl))
			return;
		one[0] = one[1] = one[2] = CLAMP (scale, 0.05f, 50.0f);
		lrg_editor_math_compose (loc, rot, one, m);
		push_local (m);
		draw_icon (kind, color);
		rlPopMatrix ();
	}
}

typedef struct
{
	LrgEditorApp       *self;
	LrgEditorSelection *selection;
	LrgNode            *primary;
} SceneWalk;

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
	selected = lrg_editor_selection_contains (walk->selection, node);

	draw_node_geometry (walk->self, node, world);
	if (walk->self->show_icons)
		draw_node_icon (walk->self, node, world, selected);

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
		draw_subtree (walk, g_ptr_array_index (children, i), world);
}

/* An infinite-looking grid whose spacing follows the zoom level. */
static void
draw_grid (LrgEditorApp *self)
{
	gfloat  dist, spacing;
	gint    slices;
	gfloat  ext;
	gfloat  cx, cy;
	EditorRender *r = self->render;
	gdouble       size;
	guint         extent;

	/* Cells are grid-size times a power of ten that follows the zoom;
	 * grid-extent is the half width in cells. Read from the settings
	 * only after they changed (see grid_settings_changed()). */
	if (r->grid_dirty)
	{
		r->grid_size = 1.0;
		r->grid_extent = 50;
		g_object_get (self->settings, "grid-size", &r->grid_size, "grid-extent", &r->grid_extent, NULL);
		r->grid_dirty = FALSE;
	}
	size = MAX (r->grid_size, 0.0001);
	extent = r->grid_extent;
	slices = (gint) CLAMP (extent * 2, 2, 400);

	if (self->view.orthographic)
		dist = self->view.ortho_height;
	else
	{
		gfloat d[3] = { self->view.position[0] - self->view.target[0], self->view.position[1] - self->view.target[1],
		                self->view.position[2] - self->view.target[2] };

		dist = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	}
	spacing = (gfloat) size * powf (10.0f, floorf (log10f (MAX (dist / ((gfloat) size * 4.0f), 0.0001f))));
	spacing = MAX (spacing, (gfloat) size);
	ext = spacing * (gfloat) slices * 0.5f;

	rlPushMatrix ();
	if (self->view_2d)
	{
		/* XY plane, centred under the camera. */
		cx = floorf (self->view.target[0] / spacing) * spacing;
		cy = floorf (self->view.target[1] / spacing) * spacing;
		rlTranslatef (cx, cy, 0.0f);
		rlRotatef (90.0f, 1.0f, 0.0f, 0.0f);
	}
	else
	{
		cx = floorf (self->view.target[0] / spacing) * spacing;
		cy = floorf (self->view.target[2] / spacing) * spacing;
		/* A hair above y = 0 so it does not z-fight ground planes. */
		rlTranslatef (cx, 0.002f, cy);
	}
	DrawGrid (slices, spacing);
	rlPopMatrix ();

	/* World axes through the origin. */
	DrawLine3D ((Vector3) { -ext + cx, 0.0f, 0.0f }, (Vector3) { ext + cx, 0.0f, 0.0f }, (Color) { 243, 139, 168, 200 });
	if (self->view_2d)
		DrawLine3D ((Vector3) { 0.0f, -ext + cy, 0.0f }, (Vector3) { 0.0f, ext + cy, 0.0f }, (Color) { 166, 227, 161, 200 });
	else
		DrawLine3D ((Vector3) { 0.0f, 0.0f, -ext + cy }, (Vector3) { 0.0f, 0.0f, ext + cy }, (Color) { 137, 180, 250, 200 });
}

void
editor_render_viewport (LrgEditorApp *self)
{
	EditorRender *r = self->render;
	SceneWalk     walk;
	GPtrArray    *top;
	LrgNode      *root;
	gfloat        root_world[16];
	gfloat        view[16], proj[16];
	gint          w, h;
	guint         i;

	if (r == NULL || !self->viewport_visible)
		return;

	sync_asset_base (self);

	w = MAX (1, (gint) self->viewport_rect.w);
	h = MAX (1, (gint) self->viewport_rect.h);
	if (!r->has_target || r->tw != w || r->th != h)
	{
		if (r->has_target)
			UnloadRenderTexture (r->target);
		r->target = LoadRenderTexture (w, h);
		r->has_target = IsRenderTextureValid (r->target);
		r->tw = w;
		r->th = h;
		if (!r->has_target)
			return;
	}

	self->view.width = (gfloat) w;
	self->view.height = (gfloat) h;
	lrg_editor_view_get_view_matrix (&self->view, view);
	lrg_editor_view_get_projection_matrix (&self->view, proj);

	BeginTextureMode (r->target);
	ClearBackground (self->playing ? (Color) { 24, 28, 38, 255 } : (Color) { 30, 30, 46, 255 });

	/* BeginMode3D() with our own matrices (the same ones picking uses). */
	rlDrawRenderBatchActive ();
	rlMatrixMode (RL_PROJECTION);
	rlPushMatrix ();
	rlLoadIdentity ();
	rlMultMatrixf (proj);
	rlMatrixMode (RL_MODELVIEW);
	rlLoadIdentity ();
	rlMultMatrixf (view);
	rlEnableDepthTest ();

	if (r->has_lit)
	{
		const gfloat light[3] = { -0.35f, -0.85f, -0.4f };
		gfloat       len = sqrtf (light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
		gfloat       dir[3] = { light[0] / len, light[1] / len, light[2] / len };
		gfloat       ambient = 0.35f;

		SetShaderValue (r->lit, r->loc_light, dir, SHADER_UNIFORM_VEC3);
		SetShaderValue (r->lit, r->loc_ambient, &ambient, SHADER_UNIFORM_FLOAT);
	}

	if (self->show_grid)
		draw_grid (self);

	if (self->wireframe)
		rlEnableWireMode ();
	walk.self = self;
	walk.selection = lrg_editor_get_selection (self->editor);
	walk.primary = lrg_editor_selection_get_primary (walk.selection);
	r->drawn = 0;
	root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	lrg_editor_node_get_world_matrix (root, root_world);
	top = lrg_node_get_children (root);
	for (i = 0; top != NULL && i < top->len; i++)
		draw_subtree (&walk, g_ptr_array_index (top, i), root_world);
	rlDrawRenderBatchActive ();
	if (self->wireframe)
		rlDisableWireMode ();

	EndMode3D ();
	EndTextureMode ();
}

/* ==========================================================================
 * Compositing and the gizmo
 * ========================================================================== */

static Color
handle_color (LrgEditorApp         *self,
              LrgEditorGizmoHandle  handle,
              LrgGuiStyleColor      base)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);

	if (lrg_editor_gizmo_get_active (self->gizmo) == handle ||
	    (!lrg_editor_gizmo_is_dragging (self->gizmo) && lrg_editor_gizmo_get_hovered (self->gizmo) == handle))
		return (Color) { 249, 226, 175, 255 };
	return gui_color (style->colors[base]);
}

static gboolean
project (LrgEditorApp     *self,
         const LrgGuiRect *rect,
         const gfloat     *p,
         Vector2          *out)
{
	gfloat x, y;

	if (!lrg_editor_view_project (&self->view, p, &x, &y, NULL))
		return FALSE;
	out->x = rect->x + x;
	out->y = rect->y + y;
	return TRUE;
}

static void
draw_gizmo (LrgEditorApp     *self,
            const LrgGuiRect *rect)
{
	LrgEditorTool tool = lrg_editor_get_tool (self->editor);
	gfloat        origin[3], axes[3][3], size;
	Vector2       o;
	gint          i, k;

	if (!lrg_editor_gizmo_is_visible (self->gizmo))
		return;

	lrg_editor_gizmo_get_origin (self->gizmo, origin);
	for (i = 0; i < 3; i++)
		lrg_editor_gizmo_get_axis (self->gizmo, (guint) i, axes[i]);
	size = lrg_editor_gizmo_get_size (self->gizmo, &self->view);
	if (!project (self, rect, origin, &o))
		return;

	if (tool == LRG_EDITOR_TOOL_ROTATE)
	{
		/* One ring per axis plus the screen-aligned outer ring. */
		for (i = 0; i < 3; i++)
		{
			const gfloat *u = axes[(i + 1) % 3];
			const gfloat *v = axes[(i + 2) % 3];
			Color         c = handle_color (self, LRG_EDITOR_GIZMO_HANDLE_X + i, LRG_GUI_COL_AXIS_X + i);
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
				if (project (self, rect, p, &q))
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
		                  handle_color (self, LRG_EDITOR_GIZMO_HANDLE_CENTER, LRG_GUI_COL_TEXT));
		return;
	}

	/* Plane squares (translate). */
	if (tool == LRG_EDITOR_TOOL_TRANSLATE)
	{
		static const gint pairs[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
		static const LrgGuiStyleColor tints[3] = { LRG_GUI_COL_AXIS_Z, LRG_GUI_COL_AXIS_X, LRG_GUI_COL_AXIS_Y };
		const gfloat  ca[4] = { 0.2f, 0.45f, 0.45f, 0.2f };
		const gfloat  cb[4] = { 0.2f, 0.2f, 0.45f, 0.45f };

		for (i = 0; i < 3; i++)
		{
			Vector2  q[4];
			gboolean ok = TRUE;
			Color    c = handle_color (self, LRG_EDITOR_GIZMO_HANDLE_XY + i, tints[i]);
			gint     j;

			for (j = 0; j < 4 && ok; j++)
			{
				gfloat p[3];

				for (k = 0; k < 3; k++)
					p[k] = origin[k] + (axes[pairs[i][0]][k] * ca[j] + axes[pairs[i][1]][k] * cb[j]) * size;
				ok = project (self, rect, p, &q[j]);
			}
			if (!ok)
				continue;
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
		Color   c = handle_color (self, LRG_EDITOR_GIZMO_HANDLE_X + i, LRG_GUI_COL_AXIS_X + i);

		for (k = 0; k < 3; k++)
			tip[k] = origin[k] + axes[i][k] * size;
		if (!project (self, rect, tip, &t))
			continue;
		if (hypotf (t.x - o.x, t.y - o.y) < 4.0f)
			continue;   /* pointing at the camera */

		DrawLineEx (o, t, 3.0f, c);
		if (tool == LRG_EDITOR_TOOL_SCALE)
			DrawRectangleV ((Vector2) { t.x - 5.0f, t.y - 5.0f }, (Vector2) { 10.0f, 10.0f }, c);
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
		Color c = handle_color (self, LRG_EDITOR_GIZMO_HANDLE_CENTER, LRG_GUI_COL_TEXT);

		if (tool == LRG_EDITOR_TOOL_SCALE)
			DrawRectangleV ((Vector2) { o.x - 6.0f, o.y - 6.0f }, (Vector2) { 12.0f, 12.0f }, c);
		else
			DrawCircleLinesV (o, 7.0f, c);
	}
}

void
editor_render_blit (const LrgGuiRect *rect,
                    gpointer          user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);
	EditorRender *r = self->render;

	if (r == NULL || !r->has_target)
		return;

	/* Render textures are stored upside down. */
	DrawTextureRec (r->target.texture,
	                (Rectangle) { 0.0f, 0.0f, (gfloat) r->tw, -(gfloat) r->th },
	                (Vector2) { rect->x, rect->y }, WHITE);

	BeginScissorMode ((gint) rect->x, (gint) rect->y, (gint) rect->w, (gint) rect->h);
	draw_gizmo (self, rect);
	EndScissorMode ();
}

gboolean
editor_render_screenshot (LrgEditorApp  *self,
                          const gchar   *path,
                          GError       **error)
{
	EditorRender *r = self->render;
	Image         image;
	gboolean      ok;

	if (r == NULL || !r->has_target)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "No viewport image (the editor is headless or the viewport is hidden)");
		return FALSE;
	}

	image = LoadImageFromTexture (r->target.texture);
	ImageFlipVertical (&image);
	ok = ExportImage (image, path);
	UnloadImage (image);
	if (!ok)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not write %s", path);
		return FALSE;
	}
	return TRUE;
}
