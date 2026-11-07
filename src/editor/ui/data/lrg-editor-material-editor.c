/* lrg-editor-material-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMaterialEditor: the `.rmat` data editor (see the header).
 *
 * Layout: a horizontal splitter with the live preview on the left and a
 * property grid on the right:
 *
 *   +--------------------------+-------------------------------+
 *   |  (preview, sky + mesh)   | Material   name, parent, ...  |
 *   |                          | Surface    base colour, PBR   |
 *   |                          | Emission   colour, strength   |
 *   | lit · instance of base   | Textures   albedo .. height   |
 *   | [Sphere][Cube][Plane][R] | Render     blend, cull, ...   |
 *   +--------------------------+ Uniforms   (custom shading)   |
 *
 * The document is the truth. The grid reads and writes it through a
 * private LrgPropertySource (MatSource) whose fields are YAML paths; a
 * missing key reads as the value the material inherits (parent chain,
 * else the default) and is reported as DEFAULT, a key an instance sets
 * as OVERRIDDEN; "Reset" removes the key, which is the revert. Colours
 * and vectors are YAML sequences, so a grid gesture (a drag, a colour
 * pick) is wrapped in one document group: one undo step.
 *
 * The preview material is rebuilt from the document text after every
 * change (parsing is cheap; textures and parents come from the preview's
 * LrgAssetManager cache, so nothing is re-read from disk). When the
 * parent is open in another material editor, its live (unsaved)
 * material becomes the parent, so edits of a base show in its variants.
 *
 * The preview renderer (EditorMatPreview, one per app) owns everything
 * on the GPU: the three meshes, an LrgLightSet (warm key sun, cool rim
 * light, soft ambient) with a baked gradient LrgEnvironment for image
 * based lighting, and a cache of compiled LrgShaderAssets for materials
 * with custom shading. The shader graph editor and the assets panel's
 * material thumbnails draw through it too. GPU objects are created
 * lazily with a window and are never unloaded without one (the GL
 * context may already be gone when the app is finalized).
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-material-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-viewport.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-types.h"
#include "../../../graphics/lrg-light-set.h"
#include "../../../lighting/lrg-environment.h"
#include "../../../core/lrg-asset-manager.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#define PREVIEW_KEY        "lrg-editor-mat-preview"
#define INSTALLED_KEY      "lrg-editor-material-installed"
#define PREVIEW_MAPS       (12)          /* raylib MAX_MATERIAL_MAPS */
#define PREVIEW_ENV_SIZE   (32)          /* baked cubemap edge: cheap, smooth */
#define PREVIEW_FOV        (32.0f)
#define PREVIEW_SPIN_SPEED (0.55f)       /* radians per second */
#define DEG2RADF           ((gfloat) G_PI / 180.0f)

/* ==========================================================================
 * Enum
 * ========================================================================== */

GType
lrg_editor_material_preview_shape_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_MATERIAL_PREVIEW_SPHERE, "LRG_EDITOR_MATERIAL_PREVIEW_SPHERE", "sphere" },
			{ LRG_EDITOR_MATERIAL_PREVIEW_CUBE, "LRG_EDITOR_MATERIAL_PREVIEW_CUBE", "cube" },
			{ LRG_EDITOR_MATERIAL_PREVIEW_PLANE, "LRG_EDITOR_MATERIAL_PREVIEW_PLANE", "plane" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorMaterialPreviewShape"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

static const gchar *
shape_nick (guint shape)
{
	switch (shape)
	{
	case LRG_EDITOR_MATERIAL_PREVIEW_CUBE:
		return "cube";
	case LRG_EDITOR_MATERIAL_PREVIEW_PLANE:
		return "plane";
	default:
		return "sphere";
	}
}

static gint
shape_from_nick (const gchar *nick)
{
	if (g_strcmp0 (nick, "sphere") == 0)
		return LRG_EDITOR_MATERIAL_PREVIEW_SPHERE;
	if (g_strcmp0 (nick, "cube") == 0)
		return LRG_EDITOR_MATERIAL_PREVIEW_CUBE;
	if (g_strcmp0 (nick, "plane") == 0)
		return LRG_EDITOR_MATERIAL_PREVIEW_PLANE;
	return -1;
}

/* ==========================================================================
 * Paths
 * ========================================================================== */

/* @path relative to the directory @from_dir ("../x/y.png" when outside);
 * both absolute. */
gchar *
editor_mat_relative_to (const gchar *from_dir,
                        const gchar *path)
{
	g_autofree gchar *ca = g_canonicalize_filename (from_dir, NULL);
	g_autofree gchar *cb = g_canonicalize_filename (path, NULL);
	g_auto(GStrv)     pa = g_strsplit (ca, G_DIR_SEPARATOR_S, -1);
	g_auto(GStrv)     pb = g_strsplit (cb, G_DIR_SEPARATOR_S, -1);
	GString          *out = g_string_new (NULL);
	guint             i = 0, j;

	while (pa[i] != NULL && pb[i] != NULL && g_strcmp0 (pa[i], pb[i]) == 0)
		i++;
	for (j = i; pa[j] != NULL; j++)
		if (*pa[j] != '\0')
			g_string_append (out, ".." G_DIR_SEPARATOR_S);
	for (j = i; pb[j] != NULL; j++)
	{
		g_string_append (out, pb[j]);
		if (pb[j + 1] != NULL)
			g_string_append_c (out, G_DIR_SEPARATOR);
	}
	return g_string_free (out, FALSE);
}

/* @path made absolute against the app's project (or the cwd). */
static gchar *
app_abs_path (LrgEditorApp *app,
              const gchar  *path)
{
	if (path == NULL)
		return NULL;
	if (g_path_is_absolute (path))
		return g_canonicalize_filename (path, NULL);
	if (app != NULL)
	{
		g_autofree gchar *p = editor_app_project_path (app, path);

		return g_canonicalize_filename (p, NULL);
	}
	return g_canonicalize_filename (path, NULL);
}

/* ==========================================================================
 * EditorMatPreview: the shared GPU preview renderer (one per app)
 * ========================================================================== */

struct _EditorMatPreview
{
	LrgEditorApp    *app;           /* weak (the app owns us) */
	LrgAssetManager *manager;
	gchar           *root;          /* project root the manager follows */
	GHashTable      *shaders;       /* abs path -> LrgShaderAsset* */

	/* GPU, valid while @window is the current window */
	gboolean         gpu;
	gpointer         window;
	Mesh             meshes[3];
	LrgLightSet     *lights;
	LrgEnvironment  *env;
	gboolean         env_failed;
};

/* GL objects that outlived their context are parked here and never
 * freed: unloading them without a context would crash. */
static GPtrArray *graveyard;

static void
preview_drop_gpu (EditorMatPreview *self,
                  gboolean          unload)
{
	GHashTableIter iter;
	gpointer       value;
	guint          i;

	if (!self->gpu)
		return;
	if (unload)
	{
		for (i = 0; i < G_N_ELEMENTS (self->meshes); i++)
			UnloadMesh (self->meshes[i]);
		g_hash_table_iter_init (&iter, self->shaders);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			lrg_shader_asset_release (LRG_SHADER_ASSET (value));
		if (self->env != NULL)
			lrg_environment_release_gpu (self->env);
		g_clear_object (&self->env);
		g_clear_object (&self->lights);
	}
	else
	{
		/* the context is gone: forget the ids, park the objects */
		if (graveyard == NULL)
			graveyard = g_ptr_array_new ();
		if (self->lights != NULL)
			g_ptr_array_add (graveyard, g_steal_pointer (&self->lights));
		if (self->env != NULL)
			g_ptr_array_add (graveyard, g_steal_pointer (&self->env));
		g_hash_table_iter_init (&iter, self->shaders);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			g_ptr_array_add (graveyard, g_object_ref (value));
		g_hash_table_remove_all (self->shaders);
	}
	memset (self->meshes, 0, sizeof self->meshes);
	self->gpu = FALSE;
	self->window = NULL;
	self->env_failed = FALSE;
}

static void
preview_free (gpointer data)
{
	EditorMatPreview *self = data;

	preview_drop_gpu (self, IsWindowReady () && GetWindowHandle () == self->window);
	if (!IsWindowReady () && g_hash_table_size (self->shaders) > 0)
	{
		/* compiled programs whose context is gone: park them */
		GHashTableIter iter;
		gpointer       value;

		if (graveyard == NULL)
			graveyard = g_ptr_array_new ();
		g_hash_table_iter_init (&iter, self->shaders);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			if (lrg_shader_asset_is_compiled (LRG_SHADER_ASSET (value)))
				g_ptr_array_add (graveyard, g_object_ref (value));
	}
	g_hash_table_unref (self->shaders);
	g_clear_object (&self->manager);
	g_free (self->root);
	g_free (self);
}

EditorMatPreview *
editor_mat_preview_get (LrgEditorApp *app)
{
	EditorMatPreview *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_get_data (G_OBJECT (app), PREVIEW_KEY);
	if (self == NULL)
	{
		self = g_new0 (EditorMatPreview, 1);
		self->app = app;
		self->shaders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		g_object_set_data_full (G_OBJECT (app), PREVIEW_KEY, self, preview_free);
	}
	return self;
}

/* The manager, following the app's project directory: guid references
 * and project-relative paths resolve, textures load once with their
 * .meta import settings. */
LrgAssetManager *
editor_mat_preview_get_manager (EditorMatPreview *self)
{
	const gchar *root = lrg_editor_app_get_project_dir (self->app);

	if (self->manager != NULL && g_strcmp0 (root, self->root) == 0)
		return self->manager;
	g_clear_object (&self->manager);
	g_free (self->root);
	self->root = g_strdup (root);
	self->manager = lrg_asset_manager_new ();
	if (root != NULL)
		lrg_asset_manager_set_project_root (self->manager, root);
	return self->manager;
}

/* A file changed (saved by an editor, changed on disk): forget cached
 * copies so the next parse sees it; shader assets reload. */
void
editor_mat_preview_file_changed (LrgEditorApp *app,
                                 const gchar  *abs_path)
{
	EditorMatPreview *self = g_object_get_data (G_OBJECT (app), PREVIEW_KEY);
	GHashTableIter    iter;
	gpointer          key, value;

	if (self == NULL || abs_path == NULL)
		return;
	if (self->manager != NULL)
		lrg_asset_manager_unload (self->manager, abs_path);

	/* a .rshader, or a stage file one of them reads */
	g_hash_table_iter_init (&iter, self->shaders);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		LrgShaderAsset *asset = LRG_SHADER_ASSET (value);
		const gchar    *vs = lrg_shader_asset_get_source_path (asset, LRG_SHADER_STAGE_VERTEX);
		const gchar    *fs = lrg_shader_asset_get_source_path (asset, LRG_SHADER_STAGE_FRAGMENT);
		g_autofree gchar *dir = g_path_get_dirname (key);
		g_autofree gchar *avs = vs != NULL ? g_canonicalize_filename (vs, dir) : NULL;
		g_autofree gchar *afs = fs != NULL ? g_canonicalize_filename (fs, dir) : NULL;

		if (g_strcmp0 (key, abs_path) == 0 || g_strcmp0 (avs, abs_path) == 0 || g_strcmp0 (afs, abs_path) == 0)
		{
			g_autoptr(GError) error = NULL;

			/* keep-last-good: a failing reload keeps the old program */
			if (!lrg_shader_asset_reload (asset, &error))
				g_debug ("material preview: %s did not reload: %s", (const gchar *) key, error->message);
		}
	}
}

/* A user shader asset by absolute path, loaded once (headless) and
 * compiled on first draw. */
LrgShaderAsset *
editor_mat_preview_get_shader (EditorMatPreview  *self,
                               const gchar       *abs_path,
                               GError           **error)
{
	LrgShaderAsset *asset;

	g_return_val_if_fail (abs_path != NULL, NULL);

	asset = g_hash_table_lookup (self->shaders, abs_path);
	if (asset != NULL)
		return asset;
	asset = lrg_shader_asset_new_from_file (abs_path, error);
	if (asset == NULL)
		return NULL;
	g_hash_table_insert (self->shaders, g_strdup (abs_path), asset);
	return asset;
}

/* Creates the GPU objects with the current window (once per window). */
static gboolean
preview_ensure_gpu (EditorMatPreview *self)
{
	const GrlColor   ambient = { 46, 50, 64, 255 };
	const GrlColor   sun = { 255, 240, 220, 255 };
	const GrlColor   rim = { 120, 170, 255, 255 };
	const GrlVector3 sun_dir = { -0.55f, -0.65f, -0.52f };
	const gfloat     zenith[3] = { 0.16f, 0.24f, 0.42f };
	const gfloat     horizon[3] = { 0.62f, 0.58f, 0.56f };
	const gfloat     ground[3] = { 0.10f, 0.09f, 0.10f };
	const gfloat     amb_rgb[3] = { 0.30f, 0.32f, 0.38f };

	if (!IsWindowReady ())
		return FALSE;
	if (self->gpu && self->window == GetWindowHandle ())
		return TRUE;
	preview_drop_gpu (self, FALSE);

	/* meshes: tangents for normal maps */
	self->meshes[LRG_EDITOR_MATERIAL_PREVIEW_SPHERE] = GenMeshSphere (0.95f, 48, 64);
	self->meshes[LRG_EDITOR_MATERIAL_PREVIEW_CUBE] = GenMeshCube (1.35f, 1.35f, 1.35f);
	self->meshes[LRG_EDITOR_MATERIAL_PREVIEW_PLANE] = GenMeshPlane (2.2f, 2.2f, 1, 1);
	GenMeshTangents (&self->meshes[LRG_EDITOR_MATERIAL_PREVIEW_SPHERE]);
	GenMeshTangents (&self->meshes[LRG_EDITOR_MATERIAL_PREVIEW_CUBE]);
	GenMeshTangents (&self->meshes[LRG_EDITOR_MATERIAL_PREVIEW_PLANE]);

	/* a three-point-ish studio: warm key sun, cool rim, soft fill */
	self->lights = lrg_light_set_new ();
	lrg_light_set_set_ambient (self->lights, &ambient);
	lrg_light_set_set_directional (self->lights, &sun_dir, &sun);
	lrg_light_set_add_point_light (self->lights, -2.2, 1.6, -2.6, &rim, 7.0f);

	/* image based lighting from a small baked gradient sky */
	self->env = lrg_environment_new ();
	lrg_environment_set_sky_source (self->env, LRG_ENVIRONMENT_SKY_GRADIENT);
	lrg_environment_set_gradient (self->env, zenith, horizon, ground);
	lrg_environment_set_resolution (self->env, PREVIEW_ENV_SIZE);
	lrg_environment_set_ambient (self->env, amb_rgb, 1.0f);
	lrg_light_set_set_environment (self->lights, self->env);

	self->gpu = TRUE;
	self->window = GetWindowHandle ();
	return TRUE;
}

/* Work that must run outside BeginTextureMode(): the environment bake. */
void
editor_mat_preview_prepare (EditorMatPreview *self)
{
	g_autoptr(GError) error = NULL;

	if (!preview_ensure_gpu (self) || self->env_failed)
		return;
	if (!lrg_environment_needs_bake (self->env))
		return;
	if (!lrg_environment_bake (self->env, &error))
	{
		/* flat ambient keeps working without IBL */
		g_debug ("material preview: environment bake failed: %s", error->message);
		self->env_failed = TRUE;
	}
}

/* A rotation about Y by @a and a tilt about X by @t, column-major raylib
 * matrix (no raymath: gnu89). */
static Matrix
preview_transform (gfloat a,
                   gfloat t)
{
	gfloat ca = cosf (a), sa = sinf (a);
	gfloat ct = cosf (t), st = sinf (t);
	Matrix m;

	/* R = Rx(t) * Ry(a) */
	memset (&m, 0, sizeof m);
	m.m0 = ca;        m.m4 = 0.0f;  m.m8 = sa;        m.m12 = 0.0f;
	m.m1 = st * sa;   m.m5 = ct;    m.m9 = -st * ca;  m.m13 = 0.0f;
	m.m2 = -ct * sa;  m.m6 = st;    m.m10 = ct * ca;  m.m14 = 0.0f;
	m.m3 = 0.0f;      m.m7 = 0.0f;  m.m11 = 0.0f;     m.m15 = 1.0f;
	return m;
}

/* The shader asset a custom material draws with, or NULL. */
static LrgShaderAsset *
material_shader (EditorMatPreview *self,
                 LrgMaterial      *material)
{
	const gchar      *ref;
	g_autofree gchar *abs = NULL;

	if (material == NULL || lrg_material_get_shading_model (material) != LRG_SHADING_MODEL_CUSTOM)
		return NULL;
	ref = lrg_material_get_shader (material);
	if (ref == NULL)
		return NULL;
	abs = lrg_material_resolve_reference (material, ref);
	if (abs == NULL)
		return NULL;
	return editor_mat_preview_get_shader (self, abs, NULL);
}

/* Compiles @shader once per program version; a failure is not retried
 * every frame (a reload resets the mark). */
static gboolean
shader_ready (LrgShaderAsset *shader)
{
	if (lrg_shader_asset_is_compiled (shader))
		return TRUE;
	if (g_object_get_data (G_OBJECT (shader), "lrg-preview-failed") != NULL)
		return FALSE;
	if (!lrg_shader_asset_compile (shader, NULL))
	{
		g_object_set_data (G_OBJECT (shader), "lrg-preview-failed", GINT_TO_POINTER (1));
		return FALSE;
	}
	return TRUE;
}

/*
 * editor_mat_preview_draw:
 *
 * Draws the preview inside the caller's BeginTextureMode(): the sky, then
 * the mesh with @material through @shader (else the material's own
 * custom shader, else the light set's lit program). Returns FALSE
 * without a window.
 */
gboolean
editor_mat_preview_draw (EditorMatPreview *self,
                         LrgMaterial      *material,
                         LrgShaderAsset   *shader,
                         guint             shape,
                         gfloat            yaw,
                         gfloat            pitch,
                         gfloat            spin,
                         gint              width,
                         gint              height)
{
	MaterialMap  maps[PREVIEW_MAPS];
	Material     mat;
	Camera3D     cam;
	GrlVector3   eye;
	GrlShader   *program = NULL;
	Shader      *handle;
	gfloat       dist;
	const gfloat tilt = 0.0f;
	guint        id;

	if (!preview_ensure_gpu (self) || width <= 0 || height <= 0)
		return FALSE;
	shape = MIN (shape, 2u);

	/* --- camera on an orbit around the origin --- */
	/* framed with a margin at any aspect: the narrower side decides */
	dist = (shape == LRG_EDITOR_MATERIAL_PREVIEW_SPHERE) ? 4.6f : 5.4f;
	if (width < height)
		dist *= (gfloat) height / (gfloat) width;
	memset (&cam, 0, sizeof cam);
	cam.position.x = dist * cosf (pitch) * sinf (yaw);
	cam.position.y = dist * sinf (pitch);
	cam.position.z = dist * cosf (pitch) * cosf (yaw);
	cam.target = (Vector3) { 0.0f, 0.0f, 0.0f };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = PREVIEW_FOV;
	cam.projection = CAMERA_PERSPECTIVE;
	eye.x = cam.position.x;
	eye.y = cam.position.y;
	eye.z = cam.position.z;

	ClearBackground ((Color) { 24, 26, 36, 255 });
	BeginMode3D (cam);

	/* --- the sky behind everything (once baked) --- */
	if (lrg_environment_is_baked (self->env))
		lrg_environment_draw_sky (self->env, &eye);

	/* --- the program --- */
	lrg_light_set_apply (self->lights, eye.x, eye.y, eye.z);
	if (shader == NULL)
		shader = material_shader (self, material);
	if (shader != NULL && shader_ready (shader))
	{
		if (lrg_shader_asset_get_lit (shader))
			lrg_shader_asset_bind_lights (shader, self->lights);
		lrg_shader_asset_set_time (shader, (gfloat) GetTime ());
		lrg_shader_asset_apply (shader, NULL);
		program = lrg_shader_asset_get_shader (shader);
	}
	if (program == NULL)
		program = lrg_light_set_get_shader (self->lights);

	memset (maps, 0, sizeof maps);
	memset (&mat, 0, sizeof mat);
	if (program != NULL)
		mat.shader = *(Shader *) grl_shader_get_handle (program);
	else
	{
		mat.shader.id = rlGetShaderIdDefault ();
		mat.shader.locs = rlGetShaderLocsDefault ();
	}
	handle = &mat.shader;
	id = handle->id;
	maps[MATERIAL_MAP_ALBEDO].texture.id = rlGetTextureIdDefault ();
	maps[MATERIAL_MAP_ALBEDO].texture.width = 1;
	maps[MATERIAL_MAP_ALBEDO].texture.height = 1;
	maps[MATERIAL_MAP_ALBEDO].texture.mipmaps = 1;
	maps[MATERIAL_MAP_ALBEDO].texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	maps[MATERIAL_MAP_ALBEDO].color = (Color) { 200, 200, 205, 255 };
	mat.maps = maps;

	/* --- the mesh --- */
	if (material != NULL)
	{
		if (program != NULL)
			lrg_material_apply (material, id);
		lrg_material_fill_raylib_maps (material, maps);
		lrg_material_apply_state (material);
	}
	else if (program != NULL)
		lrg_material_reset_uniforms (id);
	DrawMesh (self->meshes[shape], mat, preview_transform (spin, tilt));
	if (material != NULL)
	{
		lrg_material_restore_state (material);
		if (program != NULL)
			lrg_material_reset_uniforms (id);
	}
	EndMode3D ();
	return TRUE;
}

GrlImage *
editor_mat_preview_render_image (EditorMatPreview *self,
                                 LrgMaterial      *material,
                                 LrgShaderAsset   *shader,
                                 guint             shape,
                                 gfloat            yaw,
                                 gfloat            pitch,
                                 gfloat            spin,
                                 guint             size)
{
	RenderTexture2D rt;
	Image           img;
	GrlImage       *out;

	if (size == 0 || !IsWindowReady ())
		return NULL;
	editor_mat_preview_prepare (self);
	rt = LoadRenderTexture ((gint) size, (gint) size);
	if (rt.id == 0)
		return NULL;
	BeginTextureMode (rt);
	editor_mat_preview_draw (self, material, shader, shape, yaw, pitch, spin, (gint) size, (gint) size);
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
 * EditorMatPreviewView: the preview widget
 *
 * A container with an LrgViewport (pointer transparent, rendering through
 * the shared preview every frame), a caption overlay and a chip bar
 * (shape chips, Rotate). Dragging orbits the camera, the wheel does
 * nothing (the framing is fixed: materials read best at one size).
 * ========================================================================== */

#define EDITOR_TYPE_MAT_PREVIEW_VIEW (editor_mat_preview_view_get_type ())
G_DECLARE_FINAL_TYPE (EditorMatPreviewView, editor_mat_preview_view, EDITOR, MAT_PREVIEW_VIEW, LrgContainer)

struct _EditorMatPreviewView
{
	LrgContainer            parent_instance;

	LrgEditorApp           *app;          /* weak */
	EditorMatPreviewSource  source;
	gpointer                source_data;
	LrgViewport            *viewport;
	LrgWidget              *caption;
	LrgWidget              *pill;
	LrgWidget              *bar;
	LrgWidget              *chips[3];
	LrgWidget              *rotate_chip;
	guint                   shape;
	gboolean                rotate;
	gfloat                  yaw;
	gfloat                  pitch;
	gfloat                  spin;
	gboolean                dragging;
	gfloat                  last_x;
	gfloat                  last_y;
};

G_DEFINE_FINAL_TYPE (EditorMatPreviewView, editor_mat_preview_view, LRG_TYPE_CONTAINER)

static void
view_render (LrgViewport      *viewport,
             GrlRenderTexture *target,
             gfloat            width,
             gfloat            height,
             gpointer          user_data)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (user_data);
	LrgMaterial          *material = NULL;
	LrgShaderAsset       *shader = NULL;

	if (self->app == NULL)
		return;
	if (self->source != NULL)
		self->source (self->source_data, &material, &shader);
	editor_mat_preview_draw (editor_mat_preview_get (self->app), material, shader, self->shape, self->yaw,
	                         self->pitch, self->spin, (gint) width, (gint) height);
}

/* The caption: what is drawn, over a soft scrim at the top. */
static void
caption_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (user_data);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat                fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont              *font = lrg_theme_get_ui_font (theme);
	LrgMaterial          *material = NULL;
	LrgShaderAsset       *shader = NULL;
	g_autofree gchar     *text = NULL;
	const gchar          *model = "no material";
	const GrlColor        scrim = { 0, 0, 0, 90 };
	const GrlColor        ink = { 236, 238, 246, 255 };
	gfloat                tw = 0.0f;

	if (self->source != NULL)
		self->source (self->source_data, &material, &shader);
	if (shader != NULL)
		model = lrg_shader_asset_get_lit (shader) ? "shader graph · lit" : "shader graph · unlit";
	else if (material != NULL)
	{
		switch (lrg_material_get_shading_model (material))
		{
		case LRG_SHADING_MODEL_UNLIT:
			model = "unlit";
			break;
		case LRG_SHADING_MODEL_CUSTOM:
			model = "custom shader";
			break;
		default:
			model = "lit · PBR";
			break;
		}
	}
	text = (material != NULL && lrg_material_get_parent (material) != NULL)
	       ? g_strdup_printf ("%s  ·  %s  ·  instance", shape_nick (self->shape), model)
	       : g_strdup_printf ("%s  ·  %s", shape_nick (self->shape), model);
	lrg_paint_measure_text (font, text, fs, 0.0f, &tw, NULL);
	lrg_paint_list_rect (list, x, y, tw + 20.0f, fs + 12.0f, (fs + 12.0f) * 0.5f, &scrim);
	lrg_paint_list_text (list, font, text, x + 10.0f, y + 6.0f, fs, 0.0f, &ink);
}

static void
update_chips (EditorMatPreviewView *self)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (self->chips); i++)
		editor_chip_set_checked (self->chips[i], i == self->shape);
	editor_chip_set_checked (self->rotate_chip, self->rotate);
}

static void
on_shape_chip (LrgWidget *chip,
               gpointer   user_data)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (user_data);
	guint                 i;

	for (i = 0; i < G_N_ELEMENTS (self->chips); i++)
		if (self->chips[i] == chip)
			editor_mat_preview_view_set_shape (LRG_WIDGET (self), i);
}

static void
on_rotate_chip (LrgWidget *chip,
                gpointer   user_data)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (user_data);

	editor_mat_preview_view_set_rotate (LRG_WIDGET (self), !self->rotate);
}

static void
editor_mat_preview_view_layout_children (LrgContainer *container)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (container);
	gfloat                w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                bw = 0.0f, bh = 0.0f;
	gfloat                inset = 10.0f;

	/* children are added one by one at construction */
	if (self->viewport == NULL || self->caption == NULL || self->pill == NULL || self->bar == NULL)
		return;
	lrg_widget_place_in_slot (LRG_WIDGET (self->viewport), 0.0f, 0.0f, w, h);
	lrg_widget_place_in_slot (self->caption, inset, inset, MAX (w - 2.0f * inset, 0.0f), 28.0f);
	/* icon-only chips when the labels do not fit */
	{
		gboolean compact = FALSE;
		guint    i;

		for (i = 0; i < G_N_ELEMENTS (self->chips); i++)
			editor_chip_set_compact (self->chips[i], FALSE);
		editor_chip_set_compact (self->rotate_chip, FALSE);
		lrg_widget_get_preferred_size (self->bar, &bw, &bh);
		compact = bw > w - 2.0f * inset - 10.0f;
		for (i = 0; i < G_N_ELEMENTS (self->chips); i++)
			editor_chip_set_compact (self->chips[i], compact);
		editor_chip_set_compact (self->rotate_chip, compact);
	}
	lrg_widget_get_preferred_size (self->bar, &bw, &bh);
	bw = MIN (bw, MAX (w - 2.0f * inset, 0.0f));
	lrg_widget_place_in_slot (self->pill, (w - bw) * 0.5f - 5.0f, MAX (h - bh - inset, 0.0f) - 5.0f, bw + 10.0f,
	                          bh + 10.0f);
	lrg_widget_place_in_slot (self->bar, (w - bw) * 0.5f, MAX (h - bh - inset, 0.0f), bw, bh);
}

static void
editor_mat_preview_view_measure (LrgWidget *widget,
                                 gfloat    *width,
                                 gfloat    *height)
{
	*width = 240.0f;
	*height = 240.0f;
}

/* A glass pill behind the chip bar so it reads over any sky (a child
 * between the viewport and the bar: children paint in order). */
static void
pill_paint (LrgCustomWidget *widget,
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

static gboolean
editor_mat_preview_view_step (LrgWidget *widget,
                              gfloat     delta_time)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (widget);
	gboolean              busy = FALSE;

	if (LRG_WIDGET_CLASS (editor_mat_preview_view_parent_class)->step != NULL)
		busy = LRG_WIDGET_CLASS (editor_mat_preview_view_parent_class)->step (widget, delta_time);
	if (self->rotate && !self->dragging)
	{
		self->spin = fmodf (self->spin + delta_time * PREVIEW_SPIN_SPEED, 2.0f * (gfloat) G_PI);
		busy = TRUE;
	}
	/* the bake must happen before the viewport enters texture mode in
	 * its own step (children step after their parent) */
	if (self->app != NULL && lrg_widget_get_visible (widget))
		editor_mat_preview_prepare (editor_mat_preview_get (self->app));
	return busy;
}

/* Drag to orbit (yaw around, pitch up and down). */
static gboolean
editor_mat_preview_view_handle_event (LrgWidget        *widget,
                                      const LrgUIEvent *event)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (widget);
	LrgUIEventType        type = lrg_ui_event_get_event_type (event);
	gfloat                x = lrg_ui_event_get_x (event);
	gfloat                y = lrg_ui_event_get_y (event);

	switch (type)
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
	default:
		return FALSE;
	}
}

static void
editor_mat_preview_view_dispose (GObject *object)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (object);

	if (self->caption != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->caption), NULL, NULL, NULL);
	if (self->pill != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->pill), NULL, NULL, NULL);
	if (self->viewport != NULL)
		lrg_viewport_set_render_func (self->viewport, NULL, NULL, NULL);
	self->source = NULL;
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (editor_mat_preview_view_parent_class)->dispose (object);
}

static void
editor_mat_preview_view_class_init (EditorMatPreviewViewClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = editor_mat_preview_view_dispose;
	widget_class->measure = editor_mat_preview_view_measure;
	widget_class->step = editor_mat_preview_view_step;
	widget_class->handle_event = editor_mat_preview_view_handle_event;
	container_class->layout_children = editor_mat_preview_view_layout_children;
}

static void
editor_mat_preview_view_init (EditorMatPreviewView *self)
{
	self->yaw = 0.62f;
	self->pitch = 0.32f;
	self->rotate = TRUE;
}

LrgWidget *
editor_mat_preview_view_new (LrgEditorApp           *app,
                             const gchar            *name,
                             EditorMatPreviewSource  source,
                             gpointer                user_data)
{
	static const LrgCustomWidgetFuncs caption_funcs = { NULL, caption_paint, NULL, NULL, { NULL } };
	static const LrgCustomWidgetFuncs pill_funcs = { NULL, pill_paint, NULL, NULL, { NULL } };
	static const struct { const gchar *suffix; LrgIcon icon; const gchar *label; } shapes[] = {
		{ "sphere", LRG_ICON_STAR_HALF, "Sphere" },
		{ "cube", LRG_ICON_FILE, "Cube" },
		{ "plane", LRG_ICON_MINUS, "Plane" }
	};
	EditorMatPreviewView *self = g_object_new (EDITOR_TYPE_MAT_PREVIEW_VIEW, NULL);
	guint                 i;

	g_set_weak_pointer (&self->app, app);
	self->source = source;
	self->source_data = user_data;
	lrg_widget_set_name (LRG_WIDGET (self), name);

	/* the 3D content */
	self->viewport = lrg_viewport_new ();
	{
		g_autofree gchar *vname = g_strconcat (name, "-viewport", NULL);

		lrg_widget_set_name (LRG_WIDGET (self->viewport), vname);
	}
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
	for (i = 0; i < G_N_ELEMENTS (shapes); i++)
	{
		g_autofree gchar *cname = g_strconcat (name, "-", shapes[i].suffix, NULL);

		self->chips[i] = editor_chip_new (cname, shapes[i].icon, shapes[i].label, on_shape_chip, self);
		lrg_container_add_child (LRG_CONTAINER (self->bar), self->chips[i]);
		g_object_unref (self->chips[i]);
	}
	{
		g_autofree gchar *rname = g_strconcat (name, "-rotate", NULL);

		self->rotate_chip = editor_chip_new (rname, LRG_ICON_REFRESH, "Rotate", on_rotate_chip, self);
		editor_chip_set_tint (self->rotate_chip, LRG_THEME_COLOR_SUCCESS);
		lrg_container_add_child (LRG_CONTAINER (self->bar), self->rotate_chip);
		g_object_unref (self->rotate_chip);
	}
	lrg_container_add_child (LRG_CONTAINER (self), self->bar);
	g_object_unref (self->bar);
	update_chips (self);
	return LRG_WIDGET (self);
}

guint
editor_mat_preview_view_get_shape (LrgWidget *view)
{
	return EDITOR_MAT_PREVIEW_VIEW (view)->shape;
}

void
editor_mat_preview_view_set_shape (LrgWidget *view,
                                   guint      shape)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (view);

	self->shape = MIN (shape, 2u);
	/* a plane reads best from above */
	if (self->shape == LRG_EDITOR_MATERIAL_PREVIEW_PLANE && self->pitch < 0.6f)
		self->pitch = 0.75f;
	update_chips (self);
}

gboolean
editor_mat_preview_view_get_rotate (LrgWidget *view)
{
	return EDITOR_MAT_PREVIEW_VIEW (view)->rotate;
}

void
editor_mat_preview_view_set_rotate (LrgWidget *view,
                                    gboolean   rotate)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (view);

	self->rotate = !!rotate;
	update_chips (self);
}

GrlImage *
editor_mat_preview_view_render (LrgWidget *view,
                                guint      size)
{
	EditorMatPreviewView *self = EDITOR_MAT_PREVIEW_VIEW (view);
	LrgMaterial          *material = NULL;
	LrgShaderAsset       *shader = NULL;

	if (self->app == NULL)
		return NULL;
	if (self->source != NULL)
		self->source (self->source_data, &material, &shader);
	return editor_mat_preview_render_image (editor_mat_preview_get (self->app), material, shader, self->shape,
	                                        self->yaw, self->pitch, self->spin, size);
}

/* ==========================================================================
 * Fields: the .rmat keys the grid shows
 * ========================================================================== */

typedef enum
{
	F_STRING,
	F_REF,
	F_ENUM,
	F_FLOAT,
	F_BOOL,
	F_INT,
	F_COLOR3,
	F_COLOR4,
	F_VEC2,
	F_VEC3,
	F_VEC4
} FieldKind;

typedef struct
{
	gchar     *name;     /* property name (pspec-valid) */
	gchar     *key;      /* dotted .rmat key */
	gchar     *path;     /* YAML path */
	gchar     *label;
	gchar     *group;
	gchar     *hint;     /* asset:texture, enum-strings:..., range:... */
	FieldKind  kind;
	gdouble    min;
	gdouble    max;
	gfloat     def[4];   /* default when nothing resolves */
	gchar     *def_text; /* string default */
	gboolean   uniform;  /* a uniforms.<name> entry */
	gchar     *utype;    /* uniform type nick written with the value */
} Field;

static void
field_free (gpointer data)
{
	Field *f = data;

	g_free (f->name);
	g_free (f->key);
	g_free (f->path);
	g_free (f->label);
	g_free (f->group);
	g_free (f->hint);
	g_free (f->def_text);
	g_free (f->utype);
	g_free (f);
}

/* "render_state.blend" -> "/render_state/blend" */
static gchar *
key_to_path (const gchar *key)
{
	g_auto(GStrv) parts = g_strsplit (key, ".", -1);
	gchar        *path = g_strdup ("");
	guint         i;

	for (i = 0; parts[i] != NULL; i++)
	{
		gchar *next = lrg_editor_document_path_join (path, parts[i]);

		g_free (path);
		path = next;
	}
	return path;
}

/* Short keys the engine's override list uses (blend, cull, ...) mapped to
 * their place in the file. */
static const gchar *
canonical_key (const gchar *key)
{
	static const gchar * const render_keys[] = { "blend", "cull", "depth_test", "depth_write", "double_sided" };
	static gchar *mapped[G_N_ELEMENTS (render_keys)];
	guint         i;

	for (i = 0; i < G_N_ELEMENTS (render_keys); i++)
		if (g_strcmp0 (key, render_keys[i]) == 0)
		{
			if (mapped[i] == NULL)
				mapped[i] = g_strconcat ("render_state.", render_keys[i], NULL);
			return mapped[i];
		}
	return key;
}

static Field *
field_new (const gchar *name,
           const gchar *key,
           const gchar *label,
           const gchar *group,
           FieldKind    kind)
{
	Field *f = g_new0 (Field, 1);

	f->name = g_strdup (name);
	f->key = g_strdup (key);
	f->path = key_to_path (key);
	f->label = g_strdup (label);
	f->group = g_strdup (group);
	f->kind = kind;
	f->min = 0.0;
	f->max = 1.0;
	return f;
}

static Field *
field_float (const gchar *name,
             const gchar *key,
             const gchar *label,
             const gchar *group,
             gdouble      min,
             gdouble      max,
             gdouble      step,
             gfloat       def)
{
	Field *f = field_new (name, key, label, group, F_FLOAT);

	f->min = min;
	f->max = max;
	f->def[0] = def;
	f->hint = g_strdup_printf ("range:%g,%g,%g", min, max, step);
	return f;
}

static Field *
field_enum (const gchar *name,
            const gchar *key,
            const gchar *label,
            const gchar *group,
            const gchar *choices,
            const gchar *def)
{
	Field *f = field_new (name, key, label, group, F_ENUM);

	f->hint = g_strconcat ("enum-strings:", choices, NULL);
	f->def_text = g_strdup (def);
	return f;
}

static Field *
field_ref (const gchar *name,
           const gchar *key,
           const gchar *label,
           const gchar *group,
           const gchar *kind)
{
	Field *f = field_new (name, key, label, group, F_REF);

	f->hint = g_strconcat ("asset:", kind, NULL);
	return f;
}

/* ==========================================================================
 * MatSource: the grid's view of the document
 * ========================================================================== */

#define EDITOR_TYPE_MAT_SOURCE (editor_mat_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorMatSource, editor_mat_source, EDITOR, MAT_SOURCE, GObject)

struct _EditorMatSource
{
	GObject                  parent_instance;
	LrgEditorMaterialEditor *editor;    /* weak */
	GPtrArray               *fields;    /* Field* */
	gboolean                 in_gesture;
	gboolean                 wrote;
	guint                    grid_gesture;
};

static void editor_mat_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorMatSource, editor_mat_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, editor_mat_source_iface_init))

/* ==========================================================================
 * The editor instance
 * ========================================================================== */

struct _LrgEditorMaterialEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgWidget           *preview;     /* EditorMatPreviewView */
	LrgPropertyGrid     *grid;
	EditorMatSource     *source;
	LrgMaterial         *material;    /* the parsed document, nullable */
	gchar               *parse_error;
	gchar               *grid_sig;
	gboolean             grid_stale;
	LrgWidget           *revert_chip;
};

G_DEFINE_FINAL_TYPE (LrgEditorMaterialEditor, lrg_editor_material_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_material (LrgEditorMaterialEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorMaterialEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorMaterialEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* The folder relative references resolve against. */
static gchar *
doc_dir (LrgEditorMaterialEditor *self)
{
	LrgEditorDocument *doc = doc_of (self);

	if (lrg_editor_document_get_base_dir (doc) != NULL)
		return g_strdup (lrg_editor_document_get_base_dir (doc));
	if (lrg_editor_document_get_path (doc) != NULL)
		return g_path_get_dirname (lrg_editor_document_get_path (doc));
	return g_get_current_dir ();
}

static gboolean
doc_has_parent (LrgEditorMaterialEditor *self)
{
	const gchar *p = lrg_editor_document_get_scalar (doc_of (self), "/parent");

	return p != NULL && *p != '\0';
}

/* --- reading numbers and sequences ------------------------------------------ */

static gboolean
doc_number (LrgEditorDocument *doc,
            const gchar       *path,
            gdouble           *out)
{
	const gchar *s = lrg_editor_document_get_scalar (doc, path);
	gchar       *end = NULL;
	gdouble      v;

	if (s == NULL)
		return FALSE;
	v = g_ascii_strtod (s, &end);
	if (end == s)
		return FALSE;
	*out = v;
	return TRUE;
}

/* Up to 4 numbers of a sequence (or one scalar) at @path. */
static guint
doc_numbers (LrgEditorDocument *doc,
             const gchar       *path,
             gfloat            *out)
{
	gint n, i;
	guint got = 0;

	if (lrg_editor_document_get_kind (doc, path) == LRG_EDITOR_DOCUMENT_NODE_SCALAR)
	{
		gdouble v;

		if (!doc_number (doc, path, &v))
			return 0;
		out[0] = (gfloat) v;
		return 1;
	}
	n = lrg_editor_document_get_length (doc, path);
	if (lrg_editor_document_get_kind (doc, path) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		return 0;
	for (i = 0; i < n && i < 4; i++)
	{
		g_autofree gchar *item = g_strdup_printf ("%s/%d", path, i);
		gdouble           v = 0.0;

		doc_number (doc, item, &v);
		out[got++] = (gfloat) v;
	}
	return got;
}

/* The value node of a uniform entry: the mapping's `value`, else the
 * entry itself (shorthand). */
static gchar *
uniform_value_path (LrgEditorDocument *doc,
                    const gchar       *path)
{
	if (lrg_editor_document_get_kind (doc, path) == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		return g_strconcat (path, "/value", NULL);
	return g_strdup (path);
}

static guint8
to_byte (gfloat v)
{
	return (guint8) CLAMP ((gint) lroundf (v * 255.0f), 0, 255);
}

static void
set_color_value (GValue       *value,
                 const gfloat *c,
                 guint         n)
{
	GrlColor col;

	col.r = to_byte (c[0]);
	col.g = to_byte (n > 1 ? c[1] : c[0]);
	col.b = to_byte (n > 2 ? c[2] : c[0]);
	col.a = n > 3 ? to_byte (c[3]) : 255;
	g_value_set_boxed (value, &col);
}

static void
set_vector_value (GValue       *value,
                  FieldKind     kind,
                  const gfloat *v)
{
	if (kind == F_VEC2)
	{
		GrlVector2 x = { v[0], v[1] };

		g_value_set_boxed (value, &x);
	}
	else if (kind == F_VEC3)
	{
		GrlVector3 x = { v[0], v[1], v[2] };

		g_value_set_boxed (value, &x);
	}
	else
	{
		GrlVector4 x;

		x.x = v[0];
		x.y = v[1];
		x.z = v[2];
		x.w = v[3];
		g_value_set_boxed (value, &x);
	}
}

static GType
field_type (const Field *f)
{
	switch (f->kind)
	{
	case F_FLOAT:
		return G_TYPE_DOUBLE;
	case F_BOOL:
		return G_TYPE_BOOLEAN;
	case F_INT:
		return G_TYPE_INT64;
	case F_COLOR3:
	case F_COLOR4:
		return GRL_TYPE_COLOR;
	case F_VEC2:
		return GRL_TYPE_VECTOR2;
	case F_VEC3:
		return GRL_TYPE_VECTOR3;
	case F_VEC4:
		return GRL_TYPE_VECTOR4;
	default:
		return G_TYPE_STRING;
	}
}

/* The value a missing key shows: what the material resolves (parent
 * chain), else the field default. Fills 4 floats or a string. */
static void
resolved_value (LrgEditorMaterialEditor *self,
                const Field             *f,
                gfloat                  *v,
                const gchar            **text)
{
	LrgMaterial *m = self->material;

	memcpy (v, f->def, sizeof f->def);
	*text = f->def_text;
	if (m == NULL)
		return;

	if (f->uniform)
	{
		g_autofree gchar *name = g_strdup (f->key + strlen ("uniforms."));
		LrgMaterialUniformType type;

		if (lrg_material_get_uniform (m, name, &type, v) && type == LRG_MATERIAL_UNIFORM_TEXTURE)
			*text = lrg_material_get_uniform_texture_ref (m, name);
		return;
	}
	if (g_strcmp0 (f->key, "base_color") == 0)
		lrg_material_get_base_color (m, v);
	else if (g_strcmp0 (f->key, "emission_color") == 0)
		lrg_material_get_emission_color (m, v);
	else if (g_strcmp0 (f->key, "metallic") == 0)
		v[0] = lrg_material_get_metallic (m);
	else if (g_strcmp0 (f->key, "roughness") == 0)
		v[0] = lrg_material_get_roughness (m);
	else if (g_strcmp0 (f->key, "occlusion_strength") == 0)
		v[0] = lrg_material_get_occlusion_strength (m);
	else if (g_strcmp0 (f->key, "normal_scale") == 0)
		v[0] = lrg_material_get_normal_scale (m);
	else if (g_strcmp0 (f->key, "emission_strength") == 0)
		v[0] = lrg_material_get_emission_strength (m);
	else if (g_strcmp0 (f->key, "alpha_cutoff") == 0)
		v[0] = lrg_material_get_alpha_cutoff (m);
	else if (g_strcmp0 (f->key, "height_scale") == 0)
		v[0] = lrg_material_get_height_scale (m);
	else if (g_strcmp0 (f->key, "render_state.depth_test") == 0)
		v[0] = lrg_material_get_depth_test (m) ? 1.0f : 0.0f;
	else if (g_strcmp0 (f->key, "render_state.depth_write") == 0)
		v[0] = lrg_material_get_depth_write (m) ? 1.0f : 0.0f;
	else if (g_strcmp0 (f->key, "render_state.double_sided") == 0)
		v[0] = lrg_material_get_double_sided (m) ? 1.0f : 0.0f;
	else if (g_strcmp0 (f->key, "shading_model") == 0)
	{
		static const gchar * const nicks[] = { "lit", "unlit", "custom" };

		*text = nicks[MIN ((guint) lrg_material_get_shading_model (m), 2u)];
	}
	else if (g_strcmp0 (f->key, "render_state.blend") == 0)
	{
		static const gchar * const nicks[] = { "opaque", "cutout", "alpha", "additive", "multiply" };

		*text = nicks[MIN ((guint) lrg_material_get_blend_mode (m), 4u)];
	}
	else if (g_strcmp0 (f->key, "render_state.cull") == 0)
	{
		static const gchar * const nicks[] = { "back", "front", "none" };

		*text = nicks[MIN ((guint) lrg_material_get_cull_mode (m), 2u)];
	}
	else if (g_strcmp0 (f->key, "render_queue") == 0)
	{
		static const gchar * const nicks[] = { "auto", "opaque", "cutout", "transparent" };

		*text = nicks[MIN ((guint) lrg_material_get_render_queue (m), 3u)];
	}
	else if (g_strcmp0 (f->key, "shader") == 0)
		*text = lrg_material_get_shader (m);
	else if (g_str_has_prefix (f->key, "textures."))
	{
		static const gchar * const slots[] = { "albedo", "normal", "orm", "emission", "height", "metallic_gloss" };
		guint i;

		for (i = 0; i < G_N_ELEMENTS (slots); i++)
			if (g_strcmp0 (f->key + strlen ("textures."), slots[i]) == 0)
				*text = lrg_material_get_texture_ref (m, (LrgMaterialSlot) i);
	}
}

/* Reads a field from the document, else the resolved value. */
static void
field_read (LrgEditorMaterialEditor *self,
            const Field             *f,
            GValue                  *value)
{
	LrgEditorDocument *doc = doc_of (self);
	g_autofree gchar  *vpath = f->uniform ? uniform_value_path (doc, f->path) : g_strdup (f->path);
	gboolean           present = lrg_editor_document_has_path (doc, vpath);
	gfloat             v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const gchar       *text = NULL;
	guint              n = 0;

	g_value_init (value, field_type (f));
	if (present)
	{
		n = doc_numbers (doc, vpath, v);
		text = lrg_editor_document_get_scalar (doc, vpath);
	}
	if (!present || ((f->kind == F_FLOAT || f->kind >= F_COLOR3 || f->kind == F_INT) && n == 0))
	{
		resolved_value (self, f, v, &text);
		n = 4;
	}

	switch (f->kind)
	{
	case F_FLOAT:
		g_value_set_double (value, (gdouble) v[0]);
		break;
	case F_INT:
		g_value_set_int64 (value, (gint64) lroundf (v[0]));
		break;
	case F_BOOL:
		if (present && text != NULL)
			g_value_set_boolean (value, g_ascii_strcasecmp (text, "true") == 0 || g_strcmp0 (text, "1") == 0 ||
			                            g_ascii_strcasecmp (text, "yes") == 0 ||
			                            g_ascii_strcasecmp (text, "on") == 0);
		else
		{
			if (!present)
				resolved_value (self, f, v, &text);
			g_value_set_boolean (value, v[0] != 0.0f);
		}
		break;
	case F_COLOR3:
		set_color_value (value, v, MIN (n, 3u));
		break;
	case F_COLOR4:
		if (n < 4)
			v[3] = 1.0f;
		set_color_value (value, v, 4);
		break;
	case F_VEC2:
	case F_VEC3:
	case F_VEC4:
		set_vector_value (value, f->kind, v);
		break;
	default:
		g_value_set_string (value, text != NULL ? text : "");
		break;
	}
}

/* --- writing ---------------------------------------------------------------- */

/* Makes sure every mapping above @path exists ("/render_state" for
 * "/render_state/blend"). */
static gboolean
ensure_parents (LrgEditorDocument  *doc,
                const gchar        *path,
                GError            **error)
{
	g_auto(GStrv) parts = lrg_editor_document_path_split (path, error);
	g_autofree gchar *cur = g_strdup ("");
	guint         i;

	if (parts == NULL)
		return FALSE;
	for (i = 0; parts[i] != NULL && parts[i + 1] != NULL; i++)
	{
		g_autofree gchar *next = lrg_editor_document_path_join (cur, parts[i]);

		if (lrg_editor_document_get_kind (doc, next) != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		{
			if (lrg_editor_document_has_path (doc, next))
			{
				if (!lrg_editor_document_replace (doc, next, "{}", error))
					return FALSE;
			}
			else if (!lrg_editor_document_add_key (doc, cur, parts[i], "{}", -1, error))
				return FALSE;
		}
		g_free (cur);
		cur = g_steal_pointer (&next);
	}
	return TRUE;
}

/* Writes YAML text at @path, creating the key (and its parents). */
static gboolean
write_yaml (LrgEditorDocument  *doc,
            const gchar        *path,
            const gchar        *yaml,
            GError            **error)
{
	g_autofree gchar *parent = NULL;
	const gchar      *slash;

	if (lrg_editor_document_has_path (doc, path))
		return lrg_editor_document_replace (doc, path, yaml, error);
	if (!ensure_parents (doc, path, error))
		return FALSE;
	slash = strrchr (path, '/');
	parent = g_strndup (path, (gsize) (slash - path));
	{
		g_auto(GStrv) parts = lrg_editor_document_path_split (path, error);
		guint         n = parts != NULL ? g_strv_length (parts) : 0;

		if (n == 0)
			return FALSE;
		return lrg_editor_document_add_key (doc, parent, parts[n - 1], yaml, -1, error);
	}
}

static gchar *
format_number (gdouble v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	/* three decimals are plenty for material values, and stable */
	v = round (v * 1000.0) / 1000.0;
	if (v == floor (v) && fabs (v) < 1e9)
		return g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) v);
	return g_strdup (g_ascii_formatd (buf, sizeof buf, "%.3f", v));
}

static gchar *
format_numbers (const gfloat *v,
                guint         n)
{
	GString *s = g_string_new ("[");
	guint    i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *x = format_number ((gdouble) v[i]);

		if (i > 0)
			g_string_append (s, ", ");
		g_string_append (s, x);
	}
	g_string_append_c (s, ']');
	return g_string_free (s, FALSE);
}

/* YAML text of @value for field @f (NULL when it has nothing to write). */
static gchar *
field_yaml (const Field  *f,
            const GValue *value)
{
	gfloat v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	gchar *core = NULL;

	switch (f->kind)
	{
	case F_FLOAT:
		core = format_number (CLAMP (g_value_get_double (value), f->min, f->max));
		break;
	case F_INT:
		core = g_strdup_printf ("%" G_GINT64_FORMAT, g_value_get_int64 (value));
		break;
	case F_BOOL:
		/* bool uniforms are ints in the material table */
		if (f->uniform)
			core = g_strdup (g_value_get_boolean (value) ? "1" : "0");
		else
			core = g_strdup (g_value_get_boolean (value) ? "true" : "false");
		break;
	case F_COLOR3:
	case F_COLOR4:
		{
			const GrlColor *c = g_value_get_boxed (value);

			if (c == NULL)
				return NULL;
			v[0] = (gfloat) c->r / 255.0f;
			v[1] = (gfloat) c->g / 255.0f;
			v[2] = (gfloat) c->b / 255.0f;
			v[3] = (gfloat) c->a / 255.0f;
			core = format_numbers (v, f->kind == F_COLOR3 ? 3 : 4);
		}
		break;
	case F_VEC2:
		{
			const GrlVector2 *x = g_value_get_boxed (value);

			if (x == NULL)
				return NULL;
			v[0] = x->x;
			v[1] = x->y;
			core = format_numbers (v, 2);
		}
		break;
	case F_VEC3:
		{
			const GrlVector3 *x = g_value_get_boxed (value);

			if (x == NULL)
				return NULL;
			v[0] = x->x;
			v[1] = x->y;
			v[2] = x->z;
			core = format_numbers (v, 3);
		}
		break;
	case F_VEC4:
		{
			const GrlVector4 *x = g_value_get_boxed (value);

			if (x == NULL)
				return NULL;
			v[0] = x->x;
			v[1] = x->y;
			v[2] = x->z;
			v[3] = x->w;
			core = format_numbers (v, 4);
		}
		break;
	default:
		{
			const gchar *s = g_value_get_string (value);
			g_autofree gchar *esc = NULL;

			if (s == NULL || *s == '\0')
				return g_strdup ("null");
			esc = g_strescape (s, NULL);
			core = g_strdup_printf ("\"%s\"", esc);
		}
		break;
	}

	/* typed uniforms keep their type next to the value */
	if (f->uniform && f->utype != NULL)
	{
		gchar *wrapped = g_strdup_printf ("{ type: %s, value: %s }", f->utype, core);

		g_free (core);
		return wrapped;
	}
	return core;
}

/* --- LrgPropertySource ------------------------------------------------------- */

static Field *
source_field (EditorMatSource *self,
              const gchar     *name)
{
	guint i;

	for (i = 0; i < self->fields->len; i++)
	{
		Field *f = g_ptr_array_index (self->fields, i);

		if (g_strcmp0 (f->name, name) == 0)
			return f;
	}
	return NULL;
}

static GPtrArray *
source_list (LrgPropertySource *source)
{
	EditorMatSource *self = EDITOR_MAT_SOURCE (source);
	GPtrArray       *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint            i;

	for (i = 0; i < self->fields->len; i++)
	{
		Field           *f = g_ptr_array_index (self->fields, i);
		GParamSpec      *pspec;
		LrgPropertyInfo *info;

		switch (f->kind)
		{
		case F_FLOAT:
			pspec = g_param_spec_double (f->name, f->label, NULL, f->min, f->max,
			                             CLAMP ((gdouble) f->def[0], f->min, f->max), G_PARAM_READWRITE);
			break;
		case F_INT:
			pspec = g_param_spec_int64 (f->name, f->label, NULL, (gint64) f->min, (gint64) f->max,
			                            CLAMP ((gint64) f->def[0], (gint64) f->min, (gint64) f->max),
			                            G_PARAM_READWRITE);
			break;
		case F_BOOL:
			pspec = g_param_spec_boolean (f->name, f->label, NULL, f->def[0] != 0.0f, G_PARAM_READWRITE);
			break;
		case F_COLOR3:
		case F_COLOR4:
		case F_VEC2:
		case F_VEC3:
		case F_VEC4:
			pspec = g_param_spec_boxed (f->name, f->label, NULL, field_type (f), G_PARAM_READWRITE);
			break;
		default:
			pspec = g_param_spec_string (f->name, f->label, NULL, f->def_text, G_PARAM_READWRITE);
			break;
		}
		g_param_spec_ref_sink (pspec);
		info = lrg_property_info_new (pspec);
		g_param_spec_unref (pspec);
		lrg_property_info_set_label (info, f->label);
		lrg_property_info_set_group (info, f->group);
		if (f->hint != NULL)
			lrg_property_info_set_hint (info, f->hint);
		else if (f->kind == F_COLOR3 || f->kind == F_COLOR4)
			lrg_property_info_set_hint (info, "color");
		else if (f->kind == F_VEC2)
			lrg_property_info_set_hint (info, "vector2");
		else if (f->kind == F_VEC3)
			lrg_property_info_set_hint (info, "vector3");
		else if (f->kind == F_VEC4)
			lrg_property_info_set_hint (info, "vector4");
		g_ptr_array_add (out, info);
	}
	return out;
}

static gboolean
source_get (LrgPropertySource *source,
            const gchar       *name,
            GValue            *value)
{
	EditorMatSource *self = EDITOR_MAT_SOURCE (source);
	Field           *f = source_field (self, name);

	if (f == NULL || self->editor == NULL)
		return FALSE;
	field_read (self->editor, f, value);
	return TRUE;
}

/* A reference dropped from the assets panel arrives project-relative;
 * it is stored relative to the material's folder (guid: stays). */
static gchar *
rewrite_reference (LrgEditorMaterialEditor *self,
                   const gchar             *ref)
{
	LrgEditorApp     *app = app_of (self);
	g_autofree gchar *dir = doc_dir (self);
	g_autofree gchar *here = NULL;
	g_autofree gchar *abs = NULL;

	if (ref == NULL || *ref == '\0' || g_str_has_prefix (ref, "guid:"))
		return g_strdup (ref);
	here = g_canonicalize_filename (ref, dir);
	if (!g_path_is_absolute (ref) && g_file_test (here, G_FILE_TEST_EXISTS))
		return g_strdup (ref);
	abs = app_abs_path (app, ref);
	if (abs != NULL && g_file_test (abs, G_FILE_TEST_EXISTS))
		return editor_mat_relative_to (dir, abs);
	return g_strdup (ref);
}

static gboolean
source_set (LrgPropertySource *source,
            const gchar       *name,
            const GValue      *value)
{
	EditorMatSource         *self = EDITOR_MAT_SOURCE (source);
	Field                   *f = source_field (self, name);
	LrgEditorMaterialEditor *ed = self->editor;
	g_auto(GValue)           v = G_VALUE_INIT;
	g_autofree gchar        *yaml = NULL;
	g_autoptr(GError)        error = NULL;

	if (f == NULL || ed == NULL)
		return FALSE;
	g_value_init (&v, field_type (f));
	if (!g_value_transform (value, &v))
		return FALSE;
	if (f->kind == F_REF && g_value_get_string (&v) != NULL)
	{
		g_autofree gchar *rel = rewrite_reference (ed, g_value_get_string (&v));

		g_value_set_string (&v, rel);
	}
	yaml = field_yaml (f, &v);
	if (yaml == NULL)
		return FALSE;
	if (!write_yaml (doc_of (ed), f->path, yaml, &error))
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (ed), error);
		return FALSE;
	}
	self->wrote = TRUE;
	return TRUE;
}

static LrgPropertyState
source_state (LrgPropertySource *source,
              const gchar       *name)
{
	EditorMatSource *self = EDITOR_MAT_SOURCE (source);
	Field           *f = source_field (self, name);

	if (f == NULL || self->editor == NULL)
		return LRG_PROPERTY_STATE_NONE;
	if (!lrg_editor_document_has_path (doc_of (self->editor), f->path))
		return LRG_PROPERTY_STATE_DEFAULT;
	if (g_strcmp0 (f->key, "parent") != 0 && g_strcmp0 (f->key, "name") != 0 && doc_has_parent (self->editor))
		return LRG_PROPERTY_STATE_OVERRIDDEN;
	return LRG_PROPERTY_STATE_NONE;
}

static gboolean
source_reset (LrgPropertySource *source,
              const gchar       *name)
{
	EditorMatSource *self = EDITOR_MAT_SOURCE (source);
	Field           *f = source_field (self, name);

	if (f == NULL || self->editor == NULL)
		return FALSE;
	return lrg_editor_material_editor_revert_key (self->editor, f->key);
}

static void
editor_mat_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = source_list;
	iface->get_value = source_get;
	iface->set_value = source_set;
	iface->get_state = source_state;
	iface->reset = source_reset;
}

static void
editor_mat_source_finalize (GObject *object)
{
	EditorMatSource *self = EDITOR_MAT_SOURCE (object);

	g_ptr_array_unref (self->fields);
	g_clear_weak_pointer (&self->editor);
	G_OBJECT_CLASS (editor_mat_source_parent_class)->finalize (object);
}

static void
editor_mat_source_class_init (EditorMatSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_mat_source_finalize;
}

static void
editor_mat_source_init (EditorMatSource *self)
{
	self->fields = g_ptr_array_new_with_free_func (field_free);
}

/* A grid gesture (a drag, a colour pick, typing) is one document group,
 * so it is one undo step even though colours are whole sequences. */
static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gpointer         user_data)
{
	EditorMatSource *self = EDITOR_MAT_SOURCE (user_data);
	Field           *f = source_field (self, name);
	g_autofree gchar *label = NULL;

	if (self->editor == NULL || f == NULL || self->in_gesture)
		return;
	label = g_strdup_printf ("Edit %s", f->label);
	lrg_editor_document_begin_group (doc_of (self->editor), label);
	self->in_gesture = TRUE;
	self->wrote = FALSE;
	self->grid_gesture = gesture_id;
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture_id,
                  gboolean         commit,
                  gpointer         user_data)
{
	EditorMatSource *self = EDITOR_MAT_SOURCE (user_data);

	if (self->editor == NULL || !self->in_gesture || gesture_id != self->grid_gesture)
		return;
	lrg_editor_document_end_group (doc_of (self->editor));
	self->in_gesture = FALSE;
	if (!commit && self->wrote)
		lrg_editor_document_undo (doc_of (self->editor));
	self->wrote = FALSE;
}

/* ==========================================================================
 * The grid
 * ========================================================================== */

/* The fields of every material. */
static void
add_static_fields (GPtrArray *fields)
{
	Field *f;

	g_ptr_array_add (fields, field_new ("name", "name", "Name", "Material", F_STRING));
	g_ptr_array_add (fields, field_ref ("parent", "parent", "Parent", "Material", "material"));
	g_ptr_array_add (fields, field_enum ("shading-model", "shading_model", "Shading", "Material", "lit,unlit,custom",
	                                     "lit"));
	g_ptr_array_add (fields, field_ref ("shader", "shader", "Shader", "Material", "shader"));

	f = field_new ("base-color", "base_color", "Base colour", "Surface", F_COLOR4);
	f->def[0] = f->def[1] = f->def[2] = f->def[3] = 1.0f;
	g_ptr_array_add (fields, f);
	g_ptr_array_add (fields, field_float ("metallic", "metallic", "Metallic", "Surface", 0, 1, 0.01, 0.0f));
	g_ptr_array_add (fields, field_float ("roughness", "roughness", "Roughness", "Surface", 0, 1, 0.01, 0.5f));
	g_ptr_array_add (fields, field_float ("occlusion-strength", "occlusion_strength", "Occlusion", "Surface", 0, 1,
	                                      0.01, 1.0f));
	g_ptr_array_add (fields, field_float ("normal-scale", "normal_scale", "Normal scale", "Surface", 0, 8, 0.05,
	                                      1.0f));
	g_ptr_array_add (fields, field_float ("height-scale", "height_scale", "Height scale", "Surface", 0, 1, 0.005,
	                                      0.05f));

	g_ptr_array_add (fields, field_new ("emission-color", "emission_color", "Colour", "Emission", F_COLOR3));
	g_ptr_array_add (fields, field_float ("emission-strength", "emission_strength", "Strength", "Emission", 0, 1000,
	                                      0.1, 0.0f));

	g_ptr_array_add (fields, field_ref ("tex-albedo", "textures.albedo", "Albedo", "Textures", "texture"));
	g_ptr_array_add (fields, field_ref ("tex-normal", "textures.normal", "Normal", "Textures", "texture"));
	g_ptr_array_add (fields, field_ref ("tex-orm", "textures.orm", "ORM", "Textures", "texture"));
	g_ptr_array_add (fields, field_ref ("tex-emission", "textures.emission", "Emission", "Textures", "texture"));
	g_ptr_array_add (fields, field_ref ("tex-height", "textures.height", "Height", "Textures", "texture"));
	g_ptr_array_add (fields, field_ref ("tex-metallic-gloss", "textures.metallic_gloss", "Metallic / gloss",
	                                    "Textures", "texture"));

	g_ptr_array_add (fields, field_enum ("blend", "render_state.blend", "Blend", "Render state",
	                                     "opaque,cutout,alpha,additive,multiply", "opaque"));
	g_ptr_array_add (fields, field_float ("alpha-cutoff", "alpha_cutoff", "Alpha cutoff", "Render state", 0, 1,
	                                      0.01, 0.5f));
	g_ptr_array_add (fields, field_enum ("cull", "render_state.cull", "Cull", "Render state", "back,front,none",
	                                     "back"));
	f = field_new ("depth-test", "render_state.depth_test", "Depth test", "Render state", F_BOOL);
	f->def[0] = 1.0f;
	g_ptr_array_add (fields, f);
	f = field_new ("depth-write", "render_state.depth_write", "Depth write", "Render state", F_BOOL);
	f->def[0] = 1.0f;
	g_ptr_array_add (fields, f);
	g_ptr_array_add (fields, field_new ("double-sided", "render_state.double_sided", "Double sided", "Render state",
	                                    F_BOOL));
	g_ptr_array_add (fields, field_enum ("render-queue", "render_queue", "Queue", "Render state",
	                                     "auto,opaque,cutout,transparent", "auto"));
}

/* A property name for a uniform: "u-" + the name with '_' as '-'. */
static gchar *
uniform_prop_name (const gchar *uniform)
{
	gchar *name = g_strconcat ("u-", uniform, NULL);
	gchar *p;

	for (p = name; *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '-')
			*p = '-';
	return name;
}

/* The custom shader the document names (for the uniform inspector). */
static LrgShaderAsset *
doc_shader (LrgEditorMaterialEditor *self)
{
	LrgEditorApp     *app = app_of (self);
	const gchar      *ref;
	g_autofree gchar *abs = NULL;

	if (app == NULL || self->material == NULL ||
	    lrg_material_get_shading_model (self->material) != LRG_SHADING_MODEL_CUSTOM)
		return NULL;
	ref = lrg_material_get_shader (self->material);
	if (ref == NULL)
		return NULL;
	abs = lrg_material_resolve_reference (self->material, ref);
	return abs != NULL ? editor_mat_preview_get_shader (editor_mat_preview_get (app), abs, NULL) : NULL;
}

/* Fields of the custom uniforms: the shader's descriptions first (typed,
 * ranged, hinted), then entries of the file the shader does not
 * describe (typed by their YAML). */
static void
add_uniform_fields (LrgEditorMaterialEditor *self,
                    GPtrArray               *fields,
                    GString                 *sig)
{
	LrgEditorDocument *doc = doc_of (self);
	LrgShaderAsset    *shader = doc_shader (self);
	g_autoptr(GHashTable) seen = g_hash_table_new (g_str_hash, g_str_equal);
	g_auto(GStrv)      keys = NULL;
	guint              i, n = shader != NULL ? lrg_shader_asset_get_n_uniforms (shader) : 0;

	for (i = 0; i < n; i++)
	{
		const LrgShaderUniform *u = lrg_shader_asset_get_uniform_at (shader, i);
		const gchar            *uname = lrg_shader_uniform_get_name (u);
		const gchar            *hint = lrg_shader_uniform_get_hint (u);
		LrgShaderUniformType    type = lrg_shader_uniform_get_uniform_type (u);
		g_autofree gchar       *pname = uniform_prop_name (uname);
		g_autofree gchar       *key = g_strconcat ("uniforms.", uname, NULL);
		Field                  *f = NULL;
		gfloat                  def[4];
		gfloat                  lo = 0.0f, hi = 1.0f;

		if (g_strcmp0 (hint, "hidden") == 0)
			continue;
		lrg_shader_uniform_get_default (u, def);
		switch (type)
		{
		case LRG_SHADER_UNIFORM_FLOAT:
			if (!lrg_shader_uniform_get_range (u, &lo, &hi))
			{
				lo = MIN (0.0f, def[0]);
				hi = MAX (1.0f, def[0] * 4.0f);
			}
			f = field_float (pname, key, uname, "Uniforms", lo, hi, (hi - lo) / 100.0, def[0]);
			f->utype = g_strdup ("float");
			break;
		case LRG_SHADER_UNIFORM_INT:
			f = field_new (pname, key, uname, "Uniforms", F_INT);
			f->min = lrg_shader_uniform_get_range (u, &lo, &hi) ? lo : -1000000.0;
			f->max = lrg_shader_uniform_get_range (u, &lo, &hi) ? hi : 1000000.0;
			f->utype = g_strdup ("int");
			break;
		case LRG_SHADER_UNIFORM_BOOL:
			f = field_new (pname, key, uname, "Uniforms", F_BOOL);
			f->utype = g_strdup ("int");
			break;
		case LRG_SHADER_UNIFORM_COLOR:
			f = field_new (pname, key, uname, "Uniforms", F_COLOR4);
			f->utype = g_strdup ("color");
			break;
		case LRG_SHADER_UNIFORM_VEC2:
			f = field_new (pname, key, uname, "Uniforms", F_VEC2);
			f->utype = g_strdup ("vec2");
			break;
		case LRG_SHADER_UNIFORM_VEC3:
			f = field_new (pname, key, uname, "Uniforms", g_strcmp0 (hint, "color") == 0 ? F_COLOR3 : F_VEC3);
			f->utype = g_strdup ("vec3");
			break;
		case LRG_SHADER_UNIFORM_VEC4:
			f = field_new (pname, key, uname, "Uniforms", g_strcmp0 (hint, "color") == 0 ? F_COLOR4 : F_VEC4);
			f->utype = g_strdup (g_strcmp0 (hint, "color") == 0 ? "color" : "vec4");
			break;
		case LRG_SHADER_UNIFORM_SAMPLER2D:
			f = field_ref (pname, key, uname, "Uniforms", "texture");
			f->def_text = g_strdup (lrg_shader_uniform_get_default_texture (u));
			f->utype = g_strdup ("texture");
			break;
		default:
			continue;
		}
		memcpy (f->def, def, sizeof def);
		f->uniform = TRUE;
		g_ptr_array_add (fields, f);
		g_hash_table_add (seen, (gpointer) uname);
		g_string_append_printf (sig, "|%s:%d", uname, (gint) type);
	}

	/* entries the shader does not describe */
	if (lrg_editor_document_get_kind (doc, "/uniforms") == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		keys = lrg_editor_document_get_keys (doc, "/uniforms");
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *path = NULL;
		g_autofree gchar *vpath = NULL;
		g_autofree gchar *pname = NULL;
		g_autofree gchar *key = NULL;
		gfloat            v[4];
		guint             nv;
		Field            *f;

		if (g_hash_table_contains (seen, keys[i]))
			continue;
		path = lrg_editor_document_path_join ("/uniforms", keys[i]);
		vpath = uniform_value_path (doc, path);
		pname = uniform_prop_name (keys[i]);
		key = g_strconcat ("uniforms.", keys[i], NULL);
		nv = doc_numbers (doc, vpath, v);
		switch (nv)
		{
		case 0:
			f = field_ref (pname, key, keys[i], "Uniforms", "texture");
			f->utype = g_strdup ("texture");
			break;
		case 1:
			f = field_float (pname, key, keys[i], "Uniforms", MIN (0.0f, v[0]), MAX (1.0f, v[0] * 4.0f), 0.01,
			                 v[0]);
			break;
		case 2:
			f = field_new (pname, key, keys[i], "Uniforms", F_VEC2);
			break;
		case 3:
			f = field_new (pname, key, keys[i], "Uniforms", F_VEC3);
			break;
		default:
			f = field_new (pname, key, keys[i], "Uniforms", F_COLOR4);
			f->utype = g_strdup ("color");
			break;
		}
		f->uniform = TRUE;
		g_ptr_array_add (fields, f);
		g_string_append_printf (sig, "|%s:%u", keys[i], nv);
	}
}

/* What the grid's rows depend on: the shading model, the shader and
 * the uniform keys. */
static gchar *
grid_signature (LrgEditorMaterialEditor *self,
                GPtrArray               *fields)
{
	GString *sig = g_string_new (NULL);

	g_string_append_printf (sig, "%d|%s", self->material != NULL ? (gint) lrg_material_get_shading_model (self->material)
	                                                                : -1,
	                        self->material != NULL && lrg_material_get_shader (self->material) != NULL
	                        ? lrg_material_get_shader (self->material) : "");
	add_uniform_fields (self, fields, sig);
	return g_string_free (sig, FALSE);
}

static void
rebuild_grid (LrgEditorMaterialEditor *self)
{
	g_ptr_array_set_size (self->source->fields, 0);
	add_static_fields (self->source->fields);
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self, self->source->fields);
	self->grid_stale = FALSE;
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
}

/* Lists project files for the reference rows' pickers. */
static void
collect_files (const gchar  *dir,
               const gchar  *root,
               const gchar **exts,
               GStrvBuilder *out,
               guint         depth,
               guint        *count)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL && *count < 400)
	{
		g_autofree gchar *child = g_build_filename (dir, name, NULL);
		g_autofree gchar *lower = g_ascii_strdown (name, -1);
		guint             i;

		if (name[0] == '.')
			continue;
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
		{
			if (depth < 6)
				collect_files (child, root, exts, out, depth + 1, count);
			continue;
		}
		for (i = 0; exts[i] != NULL; i++)
			if (g_str_has_suffix (lower, exts[i]))
			{
				g_strv_builder_take (out, editor_mat_relative_to (root, child));
				(*count)++;
				break;
			}
	}
}

static gchar **
reference_items (LrgPropertyGrid *grid,
                 const gchar     *name,
                 const gchar     *hint,
                 gpointer         user_data)
{
	static const gchar *tex[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".qoi", NULL };
	static const gchar *mat[] = { ".rmat", NULL };
	static const gchar *shd[] = { ".rshader", NULL };
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (user_data);
	LrgEditorApp            *app = app_of (self);
	g_autofree gchar        *dir = doc_dir (self);
	const gchar             *root = (app != NULL && lrg_editor_app_get_project_dir (app) != NULL)
	                                ? lrg_editor_app_get_project_dir (app) : dir;
	GStrvBuilder            *b = g_strv_builder_new ();
	const gchar            **exts = tex;
	guint                    count = 0;
	gchar                  **out;
	guint                    i;

	if (g_strcmp0 (hint, "asset:material") == 0)
		exts = mat;
	else if (g_strcmp0 (hint, "asset:shader") == 0)
		exts = shd;
	collect_files (root, root, exts, b, 0, &count);
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);

	/* the rows store paths relative to this material's folder */
	for (i = 0; out[i] != NULL; i++)
	{
		g_autofree gchar *abs = g_build_filename (root, out[i], NULL);

		g_free (out[i]);
		out[i] = editor_mat_relative_to (dir, abs);
	}
	return out;
}

/* ==========================================================================
 * The preview material
 * ========================================================================== */

/* The open material editor of @abs, if any. */
static LrgEditorMaterialEditor *
find_open (LrgEditorApp *app,
           const gchar  *abs)
{
	LrgEditorDataEditor *e;

	if (app == NULL || abs == NULL)
		return NULL;
	e = lrg_editor_data_editor_find (app, abs);
	return (e != NULL && LRG_IS_EDITOR_MATERIAL_EDITOR (e)) ? LRG_EDITOR_MATERIAL_EDITOR (e) : NULL;
}

/* Parses the document into the preview material. */
static void
rebuild_material (LrgEditorMaterialEditor *self)
{
	LrgEditorApp            *app = app_of (self);
	g_autofree gchar        *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autofree gchar        *dir = doc_dir (self);
	LrgAssetManager         *manager = app != NULL ? editor_mat_preview_get_manager (editor_mat_preview_get (app))
	                                                : NULL;
	g_autoptr(GError)        error = NULL;
	LrgMaterial             *m;

	m = lrg_material_new_from_data (text, -1, dir, manager, &error);
	g_clear_pointer (&self->parse_error, g_free);
	if (m == NULL)
	{
		/* keep showing the last good material */
		self->parse_error = g_strdup (error->message);
		return;
	}

	/* a parent open in another editor shows its unsaved state */
	if (lrg_material_get_parent (m) != NULL && lrg_material_get_parent_ref (m) != NULL)
	{
		g_autofree gchar        *pabs = lrg_material_resolve_reference (m, lrg_material_get_parent_ref (m));
		LrgEditorMaterialEditor *other = find_open (app, pabs);

		if (other != NULL && other != self && other->material != NULL &&
		    !lrg_editor_material_editor_is_overridden (other, "parent") && other->material != m)
			lrg_material_set_parent (m, other->material);
	}
	g_clear_object (&self->material);
	self->material = m;
}

/* Other open editors whose parent is @self refresh their preview. */
static void
refresh_children (LrgEditorMaterialEditor *self)
{
	LrgEditorApp     *app = app_of (self);
	const gchar      *mine = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autoptr(GList)  list = NULL;
	GList            *l;

	if (app == NULL || mine == NULL)
		return;
	list = lrg_editor_data_editor_list (app);
	for (l = list; l != NULL; l = l->next)
	{
		LrgEditorMaterialEditor *other;
		g_autofree gchar        *pabs = NULL;

		if (!LRG_IS_EDITOR_MATERIAL_EDITOR (l->data) || l->data == (gpointer) self)
			continue;
		other = LRG_EDITOR_MATERIAL_EDITOR (l->data);
		if (other->material == NULL || lrg_material_get_parent_ref (other->material) == NULL)
			continue;
		pabs = lrg_material_resolve_reference (other->material, lrg_material_get_parent_ref (other->material));
		if (g_strcmp0 (pabs, mine) == 0)
		{
			rebuild_material (other);
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (other->source), NULL);
			lrg_property_grid_refresh (other->grid);
		}
	}
}

static void
preview_source (gpointer         user_data,
                LrgMaterial    **material,
                LrgShaderAsset **shader)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (user_data);

	*material = self->material;
	*shader = NULL;
}

static void
sync_revert_chip (LrgEditorMaterialEditor *self)
{
	g_auto(GStrv) keys = lrg_editor_material_editor_list_overrides (self);
	guint         n = g_strv_length (keys);
	g_autofree gchar *badge = n > 0 ? g_strdup_printf ("%u", n) : NULL;

	lrg_widget_set_visible (self->revert_chip, doc_has_parent (self));
	editor_chip_set_badge (self->revert_chip, badge);
}

/* ==========================================================================
 * Tools
 * ========================================================================== */

static void
on_revert_all (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (user_data);
	guint                    n = lrg_editor_material_editor_revert_all (self);

	if (app_of (self) != NULL)
		editor_app_set_status (app_of (self), "Reverted %u override(s)", n);
}

static void
on_variant (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (user_data);
	LrgEditorApp            *app = app_of (self);
	const gchar             *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *variant = NULL;

	if (app == NULL || path == NULL)
		return;
	variant = lrg_editor_material_editor_create_variant (app, path, NULL, &error);
	if (variant == NULL || lrg_editor_material_editor_open (app, variant, &error) == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* Assigns this material to the selected nodes (one undo step). */
static void
on_assign (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (user_data);
	LrgEditorApp            *app = app_of (self);
	const gchar             *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_autoptr(GPtrArray)     nodes = NULL;
	guint                    i, done = 0;

	if (app == NULL || path == NULL)
		return;
	nodes = lrg_editor_get_selected_roots (app->editor);
	for (i = 0; nodes != NULL && i < nodes->len; i++)
		if (lrg_editor_material_editor_assign_to_node (app, lrg_node_get_guid (g_ptr_array_index (nodes, i)), path,
		                                               NULL))
			done++;
	if (done == 0)
		editor_app_notify (app, EDITOR_NOTIFY_WARNING, "Select a primitive or mesh node to assign the material to");
	else
		editor_app_set_status (app, "Assigned %s to %u node(s)",
		                       lrg_editor_document_get_display_name (doc_of (self)), done);
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

static void
material_refresh (LrgEditorDataEditor *editor,
                  const gchar         *path)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (editor);
	g_autoptr(GPtrArray)     probe = g_ptr_array_new_with_free_func (field_free);
	g_autofree gchar        *sig = NULL;

	rebuild_material (self);
	refresh_children (self);
	sync_revert_chip (self);
	sig = grid_signature (self, probe);
	if (g_strcmp0 (sig, self->grid_sig) != 0)
	{
		/* rebuilt next step: never under the grid's own callbacks */
		self->grid_stale = TRUE;
		return;
	}
	lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self->source), NULL);
	lrg_property_grid_refresh (self->grid);
}

static void
material_reveal (LrgEditorDataEditor *editor,
                 const gchar         *path,
                 guint                line)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (editor);
	g_auto(GStrv)            parts = lrg_editor_document_path_split (path, NULL);
	const gchar             *group = NULL;

	if (parts == NULL || parts[0] == NULL)
		return;
	if (g_strcmp0 (parts[0], "textures") == 0)
		group = "Textures";
	else if (g_strcmp0 (parts[0], "render_state") == 0 || g_strcmp0 (parts[0], "render_queue") == 0)
		group = "Render state";
	else if (g_strcmp0 (parts[0], "uniforms") == 0)
		group = "Uniforms";
	else if (g_str_has_prefix (parts[0], "emission"))
		group = "Emission";
	if (group != NULL)
		lrg_property_grid_set_group_expanded (self->grid, group, TRUE);
}

/* Parse errors, unresolved references and a custom shader that does not
 * load, at the line of their key. */
static void
issue_at (LrgEditorMaterialEditor  *self,
          GPtrArray                *issues,
          LrgEditorProblemSeverity  severity,
          const gchar              *path,
          const gchar              *message)
{
	guint line = 0, col = 0;

	lrg_editor_document_get_line (doc_of (self), path, &line, &col);
	g_ptr_array_add (issues, lrg_editor_document_issue_new (severity, path, line, col, message));
}

static void
material_validate (LrgEditorDataEditor *editor,
                   GPtrArray           *issues)
{
	static const gchar * const slots[] = { "albedo", "normal", "orm", "emission", "height", "metallic_gloss" };
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (editor);
	LrgEditorDocument       *doc = doc_of (self);
	guint                    i;

	rebuild_material (self);
	if (self->parse_error != NULL)
	{
		issue_at (self, issues, LRG_EDITOR_PROBLEM_ERROR, "", self->parse_error);
		return;
	}
	if (self->material == NULL)
		return;

	/* textures that do not resolve (guid: needs the project index) */
	for (i = 0; i < G_N_ELEMENTS (slots); i++)
	{
		g_autofree gchar *path = g_strconcat ("/textures/", slots[i], NULL);
		const gchar      *ref = lrg_editor_document_get_scalar (doc, path);
		g_autofree gchar *abs = NULL;
		g_autofree gchar *msg = NULL;

		if (ref == NULL || *ref == '\0')
			continue;
		abs = lrg_material_resolve_reference (self->material, ref);
		if (abs != NULL)
			continue;
		msg = g_strdup_printf ("texture '%s' (%s) not found", ref, slots[i]);
		issue_at (self, issues, LRG_EDITOR_PROBLEM_WARNING, path, msg);
	}

	/* the custom shader */
	if (lrg_material_get_shading_model (self->material) == LRG_SHADING_MODEL_CUSTOM)
	{
		const gchar      *ref = lrg_material_get_shader (self->material);
		g_autofree gchar *abs = ref != NULL ? lrg_material_resolve_reference (self->material, ref) : NULL;
		g_autoptr(GError) error = NULL;

		if (ref == NULL)
			issue_at (self, issues, LRG_EDITOR_PROBLEM_WARNING, "/shading_model",
			          "custom shading without a shader: the lit shader is used");
		else if (abs == NULL)
		{
			g_autofree gchar *msg = g_strdup_printf ("shader '%s' not found", ref);

			issue_at (self, issues, LRG_EDITOR_PROBLEM_ERROR, "/shader", msg);
		}
		else if (app_of (self) != NULL &&
		         editor_mat_preview_get_shader (editor_mat_preview_get (app_of (self)), abs, &error) == NULL)
			issue_at (self, issues, LRG_EDITOR_PROBLEM_ERROR, "/shader", error->message);
	}
}

static gchar *
material_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (editor);
	g_auto(GStrv)            keys = lrg_editor_material_editor_list_overrides (self);

	return g_strdup_printf ("shape=%s rotate=%d model=%s overrides=%u",
	                        shape_nick (editor_mat_preview_view_get_shape (self->preview)),
	                        editor_mat_preview_view_get_rotate (self->preview) ? 1 : 0,
	                        self->material == NULL ? "-"
	                        : lrg_material_get_shading_model (self->material) == LRG_SHADING_MODEL_UNLIT ? "unlit"
	                        : lrg_material_get_shading_model (self->material) == LRG_SHADING_MODEL_CUSTOM ? "custom"
	                        : "lit", g_strv_length (keys));
}

/* Saving drops the cached parse of this file so instances (and
 * thumbnails) see the new text. */
static gboolean
material_save (LrgEditorDataEditor  *editor,
               GError              **error)
{
	LrgEditorDataEditorClass *parent = LRG_EDITOR_DATA_EDITOR_CLASS (lrg_editor_material_editor_parent_class);
	LrgEditorApp             *app = lrg_editor_data_editor_get_app (editor);

	if (!parent->save (editor, error))
		return FALSE;
	if (app != NULL)
		editor_mat_preview_file_changed (app, lrg_editor_data_editor_get_path (editor));
	return TRUE;
}

static gboolean
lrg_editor_material_editor_step (LrgWidget *widget,
                                 gfloat     delta_time)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (widget);
	gboolean                 busy = LRG_WIDGET_CLASS (lrg_editor_material_editor_parent_class)->step (widget,
	                                                                                                    delta_time);

	if (self->grid_stale)
		rebuild_grid (self);
	return busy;
}

static void
lrg_editor_material_editor_constructed (GObject *object)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (object);
	LrgEditorDataEditor     *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp            *app;
	LrgSplitter             *split;
	LrgWidget               *chip;

	G_OBJECT_CLASS (lrg_editor_material_editor_parent_class)->constructed (object);
	app = app_of (self);

	rebuild_material (self);

	/* preview */
	self->preview = editor_mat_preview_view_new (app, "material-preview", preview_source, self);

	/* grid over the document */
	self->source = g_object_new (EDITOR_TYPE_MAT_SOURCE, NULL);
	g_set_weak_pointer (&self->source->editor, self);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "material-grid");
	lrg_property_grid_set_label_width (self->grid, 118.0f);
	lrg_property_grid_set_reference_func (self->grid, reference_items, self, NULL);
	g_signal_connect_object (self->grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), self->source, 0);
	g_signal_connect_object (self->grid, "edit-end", G_CALLBACK (on_grid_edit_end), self->source, 0);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (split), "material-split");
	lrg_splitter_set_start_child (split, self->preview);
	g_object_unref (self->preview);
	lrg_splitter_set_end_child (split, LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	lrg_splitter_set_default_fraction (split, 0.48f);
	lrg_splitter_set_fraction (split, 0.48f, FALSE);
	lrg_splitter_set_min_sizes (split, 200.0f, 260.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));
	g_object_unref (split);

	/* toolbar */
	chip = editor_chip_new ("material-assign", LRG_ICON_ARROW_RIGHT, "Assign", on_assign, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_SUCCESS);
	lrg_editor_data_editor_add_tool (base, chip);
	g_object_unref (chip);
	chip = editor_chip_new ("material-variant", LRG_ICON_PLUS, "Variant", on_variant, self);
	lrg_editor_data_editor_add_tool (base, chip);
	g_object_unref (chip);
	self->revert_chip = editor_chip_new ("material-revert-all", LRG_ICON_REFRESH, "Revert All", on_revert_all, self);
	editor_chip_set_tint (self->revert_chip, LRG_THEME_COLOR_WARNING);
	lrg_editor_data_editor_add_tool (base, self->revert_chip);
	g_object_unref (self->revert_chip);

	rebuild_grid (self);
	sync_revert_chip (self);
}

static void
lrg_editor_material_editor_dispose (GObject *object)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (object);

	if (self->source != NULL)
		g_clear_weak_pointer (&self->source->editor);
	G_OBJECT_CLASS (lrg_editor_material_editor_parent_class)->dispose (object);
}

static void
lrg_editor_material_editor_finalize (GObject *object)
{
	LrgEditorMaterialEditor *self = LRG_EDITOR_MATERIAL_EDITOR (object);

	g_clear_object (&self->source);
	g_clear_object (&self->material);
	g_free (self->parse_error);
	g_free (self->grid_sig);
	G_OBJECT_CLASS (lrg_editor_material_editor_parent_class)->finalize (object);
}

static void
lrg_editor_material_editor_class_init (LrgEditorMaterialEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_material_editor_constructed;
	object_class->dispose = lrg_editor_material_editor_dispose;
	object_class->finalize = lrg_editor_material_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_material_editor_step;
	data_class->refresh = material_refresh;
	data_class->reveal = material_reveal;
	data_class->validate = material_validate;
	data_class->get_state = material_get_state;
	data_class->save = material_save;
}

static void
lrg_editor_material_editor_init (LrgEditorMaterialEditor *self)
{
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorMaterialEditor *
lrg_editor_material_editor_new (LrgEditorApp      *app,
                                LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_MATERIAL_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorMaterialEditor *
lrg_editor_material_editor_open (LrgEditorApp  *app,
                                 const gchar   *path,
                                 GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_MATERIAL_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a material", path);
		return NULL;
	}
	return LRG_EDITOR_MATERIAL_EDITOR (e);
}

LrgMaterial *
lrg_editor_material_editor_get_material (LrgEditorMaterialEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), NULL);
	return self->material;
}

gboolean
lrg_editor_material_editor_set_key (LrgEditorMaterialEditor  *self,
                                    const gchar              *key,
                                    const gchar              *yaml,
                                    GError                  **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *label = NULL;
	LrgEditorDocument *doc;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL && *key != '\0', FALSE);
	g_return_val_if_fail (yaml != NULL, FALSE);

	doc = doc_of (self);
	path = key_to_path (canonical_key (key));
	label = g_strdup_printf ("Set %s", key);
	lrg_editor_document_begin_group (doc, label);
	ok = write_yaml (doc, path, yaml, error);
	lrg_editor_document_end_group (doc);
	return ok;
}

gboolean
lrg_editor_material_editor_revert_key (LrgEditorMaterialEditor *self,
                                       const gchar             *key)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GError) error = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	path = key_to_path (canonical_key (key));
	if (!lrg_editor_document_has_path (doc_of (self), path))
		return FALSE;
	if (!lrg_editor_document_remove (doc_of (self), path, &error))
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	return TRUE;
}

gboolean
lrg_editor_material_editor_is_overridden (LrgEditorMaterialEditor *self,
                                          const gchar             *key)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	if (!doc_has_parent (self) || g_strcmp0 (key, "parent") == 0 || g_strcmp0 (key, "name") == 0)
		return FALSE;
	path = key_to_path (canonical_key (key));
	return lrg_editor_document_has_path (doc_of (self), path);
}

/* Every leaf key the document sets under @path (dotted). */
static void
collect_keys (LrgEditorDocument *doc,
              const gchar       *path,
              const gchar       *prefix,
              GStrvBuilder      *out)
{
	g_auto(GStrv) keys = lrg_editor_document_get_keys (doc, path);
	guint         i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *child = lrg_editor_document_path_join (path, keys[i]);
		g_autofree gchar *dotted = prefix != NULL ? g_strconcat (prefix, ".", keys[i], NULL) : g_strdup (keys[i]);

		if (prefix == NULL && (g_strcmp0 (keys[i], "version") == 0 || g_strcmp0 (keys[i], "name") == 0 ||
		                       g_strcmp0 (keys[i], "parent") == 0))
			continue;
		if (prefix == NULL && (g_strcmp0 (keys[i], "textures") == 0 || g_strcmp0 (keys[i], "render_state") == 0 ||
		                       g_strcmp0 (keys[i], "uniforms") == 0) &&
		    lrg_editor_document_get_kind (doc, child) == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		{
			collect_keys (doc, child, keys[i], out);
			continue;
		}
		g_strv_builder_add (out, dotted);
	}
}

GStrv
lrg_editor_material_editor_list_overrides (LrgEditorMaterialEditor *self)
{
	GStrvBuilder *b;
	GStrv         out;

	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), NULL);

	b = g_strv_builder_new ();
	if (doc_has_parent (self))
		collect_keys (doc_of (self), "", NULL, b);
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

guint
lrg_editor_material_editor_revert_all (LrgEditorMaterialEditor *self)
{
	g_auto(GStrv) keys = NULL;
	guint         i, n = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), 0);

	keys = lrg_editor_material_editor_list_overrides (self);
	if (keys[0] == NULL)
		return 0;
	lrg_editor_document_begin_group (doc_of (self), "Revert All Overrides");
	for (i = 0; keys[i] != NULL; i++)
		if (lrg_editor_material_editor_revert_key (self, keys[i]))
			n++;
	/* empty sections left behind go too */
	if (lrg_editor_document_get_length (doc_of (self), "/textures") == 0)
		lrg_editor_document_remove (doc_of (self), "/textures", NULL);
	if (lrg_editor_document_get_length (doc_of (self), "/render_state") == 0)
		lrg_editor_document_remove (doc_of (self), "/render_state", NULL);
	if (lrg_editor_document_get_length (doc_of (self), "/uniforms") == 0)
		lrg_editor_document_remove (doc_of (self), "/uniforms", NULL);
	lrg_editor_document_end_group (doc_of (self));
	return n;
}

LrgEditorMaterialPreviewShape
lrg_editor_material_editor_get_preview_shape (LrgEditorMaterialEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), LRG_EDITOR_MATERIAL_PREVIEW_SPHERE);
	return (LrgEditorMaterialPreviewShape) editor_mat_preview_view_get_shape (self->preview);
}

void
lrg_editor_material_editor_set_preview_shape (LrgEditorMaterialEditor       *self,
                                              LrgEditorMaterialPreviewShape  shape)
{
	g_return_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self));
	editor_mat_preview_view_set_shape (self->preview, (guint) shape);
}

gboolean
lrg_editor_material_editor_get_preview_rotate (LrgEditorMaterialEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), FALSE);
	return editor_mat_preview_view_get_rotate (self->preview);
}

void
lrg_editor_material_editor_set_preview_rotate (LrgEditorMaterialEditor *self,
                                               gboolean                 rotate)
{
	g_return_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self));
	editor_mat_preview_view_set_rotate (self->preview, rotate);
}

GrlImage *
lrg_editor_material_editor_render_preview (LrgEditorMaterialEditor *self,
                                           guint                    size)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MATERIAL_EDITOR (self), NULL);
	if (self->material == NULL)
		return NULL;
	return editor_mat_preview_view_render (self->preview, size);
}

/* --- files ------------------------------------------------------------------ */

gboolean
lrg_editor_material_editor_create (LrgEditorApp  *app,
                                   const gchar   *path,
                                   const gchar   *parent,
                                   const gchar   *shading_model,
                                   GError       **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *stem = NULL;
	g_autofree gchar *text = NULL;
	const gchar      *dot;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	abs = app_abs_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	if (shading_model != NULL && g_strcmp0 (shading_model, "lit") != 0 && g_strcmp0 (shading_model, "unlit") != 0 &&
	    g_strcmp0 (shading_model, "custom") != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown shading model '%s' (lit, unlit or custom)", shading_model);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	base = g_path_get_basename (abs);
	dot = strrchr (base, '.');
	stem = dot != NULL && dot != base ? g_strndup (base, (gsize) (dot - base)) : g_strdup (base);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s", dir);
		return FALSE;
	}

	if (parent != NULL && *parent != '\0')
		text = g_strdup_printf ("version: 1\nname: %s\nparent: \"%s\"\n", stem, parent);
	else
		text = g_strdup_printf ("version: 1\nname: %s\nshading_model: %s\nbase_color: [0.8, 0.8, 0.8, 1]\n"
		                        "metallic: 0\nroughness: 0.5\n",
		                        stem, shading_model != NULL ? shading_model : "lit");
	return g_file_set_contents (abs, text, -1, error);
}

/* The starter fragment stages (the .rshader preamble declares the
 * inputs, the uniforms and, when lit, the light set library). */
static const gchar *SHADER_LIT_FRAG =
	"/* A lit user shader: stripes of the tint, lit by the scene's lights. */\n"
	"void main()\n"
	"{\n"
	"    vec3  n = normalize(fragNormal);\n"
	"    vec3  v = normalize(lrg_view_position() - fragPosition);\n"
	"    float band = 0.5 + 0.5 * sin(fragTexCoord.y * stripes * 6.2831853 + lrgTime);\n"
	"    vec3  albedo = mix(tint.rgb, tint.rgb * 0.3, smoothstep(0.45, 0.55, band));\n"
	"    vec3  lit = lrg_light_pbr(fragPosition, n, v, vec4(lrg_to_linear(albedo), tint.a), 0.0, roughness, 1.0);\n"
	"    finalColor = vec4(lrg_to_display(lit), tint.a);\n"
	"}\n";

static const gchar *SHADER_UNLIT_FRAG =
	"/* An unlit user shader: animated stripes of the tint. */\n"
	"void main()\n"
	"{\n"
	"    float band = 0.5 + 0.5 * sin(fragTexCoord.y * stripes * 6.2831853 + lrgTime);\n"
	"    vec3  color = mix(tint.rgb, tint.rgb * 0.3, smoothstep(0.45, 0.55, band));\n"
	"    finalColor = vec4(color * (1.0 - 0.5 * roughness), tint.a);\n"
	"}\n";

gboolean
lrg_editor_material_editor_create_shader (LrgEditorApp  *app,
                                          const gchar   *path,
                                          gboolean       lit,
                                          GError       **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *stem = NULL;
	g_autofree gchar *frag_name = NULL;
	g_autofree gchar *frag = NULL;
	g_autofree gchar *doc = NULL;
	const gchar      *dot;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	abs = app_abs_path (app, path);
	dir = g_path_get_dirname (abs);
	base = g_path_get_basename (abs);
	dot = strrchr (base, '.');
	stem = dot != NULL && dot != base ? g_strndup (base, (gsize) (dot - base)) : g_strdup (base);
	frag_name = g_strconcat (stem, ".frag", NULL);
	frag = g_build_filename (dir, frag_name, NULL);
	if (g_file_test (abs, G_FILE_TEST_EXISTS) || g_file_test (frag, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists",
		             g_file_test (abs, G_FILE_TEST_EXISTS) ? abs : frag);
		return FALSE;
	}
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s", dir);
		return FALSE;
	}
	doc = g_strdup_printf ("version: 1\n"
	                       "name: %s\n"
	                       "lit: %s\n"
	                       "fragment: %s\n"
	                       "uniforms:\n"
	                       "  tint:\n"
	                       "    type: color\n"
	                       "    default: [0.9, 0.55, 0.2, 1]\n"
	                       "    hint: color\n"
	                       "  stripes:\n"
	                       "    type: float\n"
	                       "    default: 6\n"
	                       "    range: [1, 32]\n"
	                       "    hint: slider\n"
	                       "  roughness:\n"
	                       "    type: float\n"
	                       "    default: 0.45\n"
	                       "    range: [0, 1]\n"
	                       "    hint: slider\n",
	                       stem, lit ? "true" : "false", frag_name);
	if (!g_file_set_contents (frag, lit ? SHADER_LIT_FRAG : SHADER_UNLIT_FRAG, -1, error))
		return FALSE;
	return g_file_set_contents (abs, doc, -1, error);
}

gchar *
lrg_editor_material_editor_suggest_variant_path (const gchar *path)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *stem = NULL;
	const gchar      *dot;
	guint             i;

	g_return_val_if_fail (path != NULL, NULL);

	dir = g_path_get_dirname (path);
	base = g_path_get_basename (path);
	dot = strrchr (base, '.');
	stem = dot != NULL && dot != base ? g_strndup (base, (gsize) (dot - base)) : g_strdup (base);
	for (i = 1; ; i++)
	{
		g_autofree gchar *name = i == 1 ? g_strdup_printf ("%s_variant.rmat", stem)
		                                : g_strdup_printf ("%s_variant%u.rmat", stem, i);
		gchar            *candidate = g_build_filename (dir, name, NULL);

		if (!g_file_test (candidate, G_FILE_TEST_EXISTS) || i > 9999)
			return candidate;
		g_free (candidate);
	}
}

gchar *
lrg_editor_material_editor_create_variant (LrgEditorApp  *app,
                                           const gchar   *path,
                                           const gchar   *dest,
                                           GError       **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *target = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *rel = NULL;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	abs = app_abs_path (app, path);
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s does not exist", abs);
		return NULL;
	}
	target = dest != NULL ? app_abs_path (app, dest) : lrg_editor_material_editor_suggest_variant_path (abs);
	dir = g_path_get_dirname (target);
	rel = editor_mat_relative_to (dir, abs);
	if (!lrg_editor_material_editor_create (app, target, rel, NULL, error))
		return NULL;
	return g_steal_pointer (&target);
}

/* --- assignment ------------------------------------------------------------- */

gboolean
lrg_editor_material_editor_assign_to_node (LrgEditorApp  *app,
                                           const gchar   *node_ref,
                                           const gchar   *path,
                                           GError       **error)
{
	LrgNode          *node;
	LrgNodeVisual    *visual;
	LrgNodeVisualKind kind;
	g_autofree gchar *ref = NULL;
	g_auto(GValue)    v = G_VALUE_INIT;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (node_ref != NULL, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	node = lrg_editor_find_node (app->editor, node_ref);
	if (node == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No node '%s'", node_ref);
		return FALSE;
	}
	visual = lrg_node_get_visual (node);
	kind = visual != NULL ? lrg_node_visual_get_kind (visual) : LRG_NODE_VISUAL_NONE;
	if (kind != LRG_NODE_VISUAL_PRIMITIVE && kind != LRG_NODE_VISUAL_MESH_ASSET)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "Node '%s' has no primitive or mesh to take a material", lrg_node_get_name (node));
		return FALSE;
	}

	/* the reference: guid when the asset database knows the file */
	if (g_str_has_prefix (path, "guid:"))
		ref = g_strdup (path);
	else
	{
		g_autofree gchar *abs = app_abs_path (app, path);
		LrgAssetEntry    *entry = app->assets != NULL ? lrg_asset_database_lookup_path (app->assets, abs) : NULL;

		if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s does not exist", abs);
			return FALSE;
		}
		if (entry != NULL && lrg_asset_entry_get_guid (entry) != NULL)
			ref = g_strconcat ("guid:", lrg_asset_entry_get_guid (entry), NULL);
		else
			ref = editor_app_relative_path (app, abs);
	}

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, ref);
	if (!lrg_editor_set_object_property (app->editor, G_OBJECT (visual), "material-ref", &v, FALSE))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not set the material of '%s'",
		             lrg_node_get_name (node));
		return FALSE;
	}
	return TRUE;
}

/* --- thumbnails ------------------------------------------------------------- */

GrlImage *
lrg_editor_material_editor_render_thumbnail (LrgEditorApp *app,
                                             const gchar  *path,
                                             guint         size)
{
	EditorMatPreview        *preview;
	g_autoptr(LrgMaterial)   material = NULL;
	LrgEditorMaterialEditor *open;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if (!IsWindowReady ())
		return NULL;
	preview = editor_mat_preview_get (app);
	open = find_open (app, path);
	if (open != NULL && open->material != NULL)
		material = g_object_ref (open->material);
	else
		material = lrg_material_new_from_file (path, editor_mat_preview_get_manager (preview), NULL);
	if (material == NULL)
		return NULL;
	return editor_mat_preview_render_image (preview, material, NULL, LRG_EDITOR_MATERIAL_PREVIEW_SPHERE, 0.62f,
	                                        0.32f, 0.0f, size);
}

/* ==========================================================================
 * IPC: material.*
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

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

/* The open editor of params.path, opening it when needed. */
static LrgEditorMaterialEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar             *path = require_string (params, "path", error);
	LrgEditorMaterialEditor *self;
	g_autoptr(GError)        err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_material_editor_open (app, path, &err);
	if (self == NULL)
		fail (error, err);
	return self;
}

static JsonArray *
float_array (const gfloat *v,
             guint         n)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; i < n; i++)
		json_array_add_double_element (a, round ((gdouble) v[i] * 10000.0) / 10000.0);
	return a;
}

/* Every effective value of @m as JSON. */
static JsonObject *
material_to_json (LrgMaterial *m,
                  const gchar *path,
                  GStrv        overrides)
{
	static const gchar * const models[] = { "lit", "unlit", "custom" };
	static const gchar * const blends[] = { "opaque", "cutout", "alpha", "additive", "multiply" };
	static const gchar * const culls[] = { "back", "front", "none" };
	static const gchar * const queues[] = { "auto", "opaque", "cutout", "transparent" };
	static const gchar * const slots[] = { "albedo", "normal", "orm", "emission", "height", "metallic_gloss" };
	JsonObject   *o = json_object_new ();
	JsonObject   *tex = json_object_new ();
	JsonObject   *uni = json_object_new ();
	JsonArray    *ov = json_array_new ();
	g_auto(GStrv) names = lrg_material_list_uniforms (m);
	gfloat        c[4];
	guint         i;

	json_object_set_string_member (o, "path", path);
	json_object_set_string_member (o, "name", lrg_material_get_name (m));
	json_object_set_string_member (o, "parent", lrg_material_get_parent_ref (m));
	json_object_set_string_member (o, "shading_model", models[MIN ((guint) lrg_material_get_shading_model (m), 2u)]);
	json_object_set_string_member (o, "shader", lrg_material_get_shader (m));
	lrg_material_get_base_color (m, c);
	json_object_set_array_member (o, "base_color", float_array (c, 4));
	json_object_set_double_member (o, "metallic", lrg_material_get_metallic (m));
	json_object_set_double_member (o, "roughness", lrg_material_get_roughness (m));
	json_object_set_double_member (o, "occlusion_strength", lrg_material_get_occlusion_strength (m));
	json_object_set_double_member (o, "normal_scale", lrg_material_get_normal_scale (m));
	lrg_material_get_emission_color (m, c);
	json_object_set_array_member (o, "emission_color", float_array (c, 3));
	json_object_set_double_member (o, "emission_strength", lrg_material_get_emission_strength (m));
	json_object_set_double_member (o, "alpha_cutoff", lrg_material_get_alpha_cutoff (m));
	json_object_set_double_member (o, "height_scale", lrg_material_get_height_scale (m));
	json_object_set_string_member (o, "blend_mode", blends[MIN ((guint) lrg_material_get_blend_mode (m), 4u)]);
	json_object_set_string_member (o, "cull_mode", culls[MIN ((guint) lrg_material_get_cull_mode (m), 2u)]);
	json_object_set_boolean_member (o, "depth_test", lrg_material_get_depth_test (m));
	json_object_set_boolean_member (o, "depth_write", lrg_material_get_depth_write (m));
	json_object_set_boolean_member (o, "double_sided", lrg_material_get_double_sided (m));
	json_object_set_string_member (o, "render_queue", queues[MIN ((guint) lrg_material_get_render_queue (m), 3u)]);
	for (i = 0; i < G_N_ELEMENTS (slots); i++)
		json_object_set_string_member (tex, slots[i], lrg_material_get_texture_ref (m, (LrgMaterialSlot) i));
	json_object_set_object_member (o, "textures", tex);
	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		LrgMaterialUniformType type;
		gfloat                 v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		if (!lrg_material_get_uniform (m, names[i], &type, v))
			continue;
		switch (type)
		{
		case LRG_MATERIAL_UNIFORM_TEXTURE:
			json_object_set_string_member (uni, names[i], lrg_material_get_uniform_texture_ref (m, names[i]));
			break;
		case LRG_MATERIAL_UNIFORM_FLOAT:
		case LRG_MATERIAL_UNIFORM_INT:
			json_object_set_double_member (uni, names[i], v[0]);
			break;
		case LRG_MATERIAL_UNIFORM_VEC2:
			json_object_set_array_member (uni, names[i], float_array (v, 2));
			break;
		case LRG_MATERIAL_UNIFORM_VEC3:
			json_object_set_array_member (uni, names[i], float_array (v, 3));
			break;
		default:
			json_object_set_array_member (uni, names[i], float_array (v, 4));
			break;
		}
	}
	json_object_set_object_member (o, "uniforms", uni);
	for (i = 0; overrides != NULL && overrides[i] != NULL; i++)
		json_array_add_string_element (ov, overrides[i]);
	json_object_set_array_member (o, "overrides", ov);
	return o;
}

static JsonNode *
m_material_open (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorMaterialEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_material_create (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = require_string (params, "path", error);
	g_autofree gchar  *abs = NULL;
	g_autoptr(GError)  err = NULL;
	JsonObject        *o;

	if (path == NULL)
		return NULL;
	if (!lrg_editor_material_editor_create (app, path, param_string (params, "parent"),
	                                        param_string (params, "shading_model"), &err))
	{
		fail (error, err);
		return NULL;
	}
	abs = app_abs_path (app, path);
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	return object_node (o);
}

static JsonNode *
m_material_create_variant (LrgEditorServer  *server,
                           JsonObject       *params,
                           gpointer          user_data,
                           GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = require_string (params, "path", error);
	g_autofree gchar  *variant = NULL;
	g_autoptr(GError)  err = NULL;
	JsonObject        *o;

	if (path == NULL)
		return NULL;
	variant = lrg_editor_material_editor_create_variant (app, path, param_string (params, "dest"), &err);
	if (variant == NULL)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", variant);
	return object_node (o);
}

static JsonNode *
m_material_get (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (user_data);
	const gchar             *path = require_string (params, "path", error);
	g_autofree gchar        *abs = NULL;
	LrgEditorMaterialEditor *open;
	g_autoptr(LrgMaterial)   m = NULL;
	g_auto(GStrv)            overrides = NULL;
	g_autoptr(GError)        err = NULL;

	if (path == NULL)
		return NULL;
	abs = app_abs_path (app, path);
	open = find_open (app, abs);
	if (open != NULL && open->material != NULL)
	{
		m = g_object_ref (open->material);
		overrides = lrg_editor_material_editor_list_overrides (open);
	}
	else
	{
		m = lrg_material_new_from_file (abs, editor_mat_preview_get_manager (editor_mat_preview_get (app)), &err);
		if (m == NULL)
		{
			fail (error, err);
			return NULL;
		}
		overrides = lrg_material_get_parent (m) != NULL ? lrg_material_list_overrides (m) : g_new0 (gchar *, 1);
	}
	return object_node (material_to_json (m, abs, overrides));
}

/* YAML text of a JSON value, numbers with up to 6 significant digits. */
static gchar *
json_to_yaml (JsonNode *node)
{
	if (JSON_NODE_HOLDS_VALUE (node) && (json_node_get_value_type (node) == G_TYPE_DOUBLE ||
	                                     json_node_get_value_type (node) == G_TYPE_INT64))
	{
		gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

		return g_strdup (g_ascii_formatd (buf, sizeof buf, "%.6g", json_node_get_double (node)));
	}
	if (JSON_NODE_HOLDS_ARRAY (node))
	{
		JsonArray *a = json_node_get_array (node);
		GString   *s = g_string_new ("[");
		guint      i;

		for (i = 0; i < json_array_get_length (a); i++)
		{
			g_autofree gchar *item = json_to_yaml (json_array_get_element (a, i));

			if (i > 0)
				g_string_append (s, ", ");
			g_string_append (s, item);
		}
		g_string_append_c (s, ']');
		return g_string_free (s, FALSE);
	}
	return json_to_string (node, FALSE);
}

static JsonNode *
m_material_set (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorMaterialEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar             *key;
	JsonNode                *value;
	g_autofree gchar        *yaml = NULL;
	g_autoptr(GError)        err = NULL;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	key = require_string (params, "key", error);
	if (key == NULL)
		return NULL;
	if (!json_object_has_member (params, "value"))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing parameter 'value'");
		return NULL;
	}
	/* JSON is YAML; numbers are written short (0.3, not 0.2999...) */
	value = json_object_get_member (params, "value");
	yaml = json_to_yaml (value);
	if (!lrg_editor_material_editor_set_key (self, key, yaml, &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "key", key);
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_material_revert (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorMaterialEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar             *key;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	key = require_string (params, "key", error);
	if (key == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "key", key);
	json_object_set_boolean_member (o, "reverted", lrg_editor_material_editor_revert_key (self, key));
	return object_node (o);
}

static JsonNode *
m_material_save (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorMaterialEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)        err = NULL;
	JsonObject              *o;

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

static JsonNode *
m_material_preview (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorMaterialEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar             *shape;
	JsonObject              *o;

	if (self == NULL)
		return NULL;
	shape = param_string (params, "shape");
	if (shape != NULL)
	{
		gint s = shape_from_nick (shape);

		if (s < 0)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown shape '%s' (sphere, cube or plane)", shape);
			return NULL;
		}
		lrg_editor_material_editor_set_preview_shape (self, (LrgEditorMaterialPreviewShape) s);
	}
	if (params != NULL && json_object_has_member (params, "rotate"))
		lrg_editor_material_editor_set_preview_rotate (self, json_object_get_boolean_member (params, "rotate"));
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "shape", shape_nick (lrg_editor_material_editor_get_preview_shape (self)));
	json_object_set_boolean_member (o, "rotate", lrg_editor_material_editor_get_preview_rotate (self));
	return object_node (o);
}

static JsonNode *
m_material_assign (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *node = require_string (params, "node", error);
	const gchar       *path;
	g_autoptr(GError)  err = NULL;
	LrgNode           *n;
	JsonObject        *o;

	if (node == NULL)
		return NULL;
	path = require_string (params, "path", error);
	if (path == NULL)
		return NULL;
	if (!lrg_editor_material_editor_assign_to_node (app, node, path, &err))
	{
		fail (error, err);
		return NULL;
	}
	n = lrg_editor_find_node (app->editor, node);
	o = json_object_new ();
	json_object_set_string_member (o, "node", lrg_node_get_guid (n));
	json_object_set_string_member (o, "material", lrg_node_visual_get_material_ref (lrg_node_get_visual (n)));
	return object_node (o);
}

static JsonNode *
m_material_list (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	g_autoptr(GList)  list = lrg_editor_data_editor_list (app);
	JsonArray        *a = json_array_new ();
	JsonObject       *o = json_object_new ();
	GList            *l;

	for (l = list; l != NULL; l = l->next)
	{
		JsonObject *e;

		if (!LRG_IS_EDITOR_MATERIAL_EDITOR (l->data))
			continue;
		e = json_object_new ();
		json_object_set_string_member (e, "path", lrg_editor_data_editor_get_path (l->data));
		json_object_set_boolean_member (e, "dirty", lrg_editor_data_editor_get_dirty (l->data));
		json_array_add_object_element (a, e);
	}
	json_object_set_array_member (o, "editors", a);
	return object_node (o);
}

void
lrg_editor_material_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "material.open", m_material_open },
		{ "material.create", m_material_create },
		{ "material.create_variant", m_material_create_variant },
		{ "material.get", m_material_get },
		{ "material.set", m_material_set },
		{ "material.revert", m_material_revert },
		{ "material.save", m_material_save },
		{ "material.preview", m_material_preview },
		{ "material.assign", m_material_assign },
		{ "material.list", m_material_list }
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
