/* test-level-look.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Levels carry their look into the runtime:
 *
 * - LrgReflectionProbeComponent: properties, registration with the
 *   world's environment (enter / exit / disable / environment replaced),
 *   box = entity position + extents x world scale, pending captures;
 * - lrg_level_instantiate_3d(): the environment block becomes a copy on
 *   the world (sky images loaded from a base directory, shared when the
 *   level already holds them), the `post_stack:` reference becomes the
 *   world's LrgPostStack, decal and reflection-probe nodes become their
 *   components, a mesh asset with `lod-levels` > 1 in its .meta becomes
 *   an LrgLodComponent;
 * - the registry catalog lists every 0.9.0 3D component;
 * - GPU (under a display): capturing the world's probes clears pending.
 *
 * Only runtime API: the suite builds against a BUILD_EDITOR=0 library.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-level-look-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* A temporary directory removed recursively at the end of a test. */
static gchar *
make_temp_dir (void)
{
	g_autoptr(GError) error = NULL;
	gchar            *dir = g_dir_make_tmp ("lrg-level-look-XXXXXX", &error);

	g_assert_no_error (error);
	return dir;
}

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	if (d == NULL)
		return;
	while ((name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *path = g_build_filename (dir, name, NULL);

		if (g_file_test (path, G_FILE_TEST_IS_DIR))
			remove_tree (path);
		else
			g_unlink (path);
	}
	g_rmdir (dir);
}

static void
write_file (const gchar *dir,
            const gchar *name,
            const gchar *text)
{
	g_autofree gchar  *path = g_build_filename (dir, name, NULL);
	g_autofree gchar  *parent = g_path_get_dirname (path);
	g_autoptr(GError)  error = NULL;

	g_mkdir_with_parents (parent, 0755);
	g_file_set_contents (path, text, -1, &error);
	g_assert_no_error (error);
}

/* The first component of @type on the entity named @name. */
static gpointer
find_component (LrgWorld3D  *world,
                const gchar *name,
                GType        type)
{
	LrgEntity3D *entity = lrg_world3d_find_by_name (world, name);

	g_assert_nonnull (entity);
	return lrg_entity3d_get_component (entity, type);
}

/* ==========================================================================
 * Reflection probe component
 * ========================================================================== */

static void
test_probe_component_properties (void)
{
	g_autoptr(LrgReflectionProbeComponent) comp = lrg_reflection_probe_component_new ();
	LrgReflectionProbe *probe = lrg_reflection_probe_component_get_probe (comp);
	gfloat              e[3];
	gfloat              blend;
	guint               res;

	g_assert_true (LRG_IS_REFLECTION_PROBE (probe));
	lrg_reflection_probe_component_get_extents (comp, e);
	g_assert_cmpfloat (e[0], ==, 5.0f);
	g_assert_true (lrg_reflection_probe_component_get_auto_recapture (comp));
	g_assert_false (lrg_reflection_probe_component_get_needs_capture (comp));

	/* the .rlevel param names map by name onto these properties */
	g_object_set (comp, "extents-x", 2.0f, "extents-y", 3.0f, "extents-z", 4.0f,
	              "blend-distance", 0.5f, "intensity", 2.0f, "box-projection", FALSE,
	              "resolution", 64u, NULL);
	lrg_reflection_probe_component_get_extents (comp, e);
	g_assert_cmpfloat (e[0], ==, 2.0f);
	g_assert_cmpfloat (e[2], ==, 4.0f);
	g_assert_cmpfloat (lrg_reflection_probe_get_intensity (probe), ==, 2.0f);
	g_assert_false (lrg_reflection_probe_get_box_projection (probe));
	g_assert_cmpuint (lrg_reflection_probe_get_resolution (probe), ==, 64);
	g_object_get (comp, "blend-distance", &blend, "resolution", &res, NULL);
	g_assert_cmpfloat (blend, ==, 0.5f);
	g_assert_cmpuint (res, ==, 64);

	/* a resolution change and an explicit request are pending captures */
	g_assert_true (lrg_reflection_probe_component_get_needs_capture (comp));
}

static void
test_probe_component_registration (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(LrgEnvironment) env2 = lrg_environment_new ();
	g_autoptr(LrgEntity3D)    entity = lrg_entity3d_new_with_name ("Probe");
	g_autoptr(LrgReflectionProbeComponent) comp = lrg_reflection_probe_component_new ();
	LrgReflectionProbe       *probe = lrg_reflection_probe_component_get_probe (comp);
	gfloat                    pos[3] = { 1.0f, 2.0f, 3.0f };
	gfloat                    scl[3] = { 2.0f, 1.0f, 0.5f };
	gfloat                    v[3];

	lrg_world3d_set_environment (world, env);
	lrg_entity3d_set_position (entity, pos);
	lrg_entity3d_set_scale (entity, scl);
	lrg_reflection_probe_component_set_extents (comp, 1.0f, 2.0f, 4.0f);
	lrg_entity3d_add_component (entity, LRG_COMPONENT (comp));

	/* entering the world registers with its environment and asks for a
	 * first capture; the box is the scaled extents at the position */
	lrg_entity3d_add_child (lrg_world3d_get_root (world), entity);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 1);
	g_assert_true (lrg_environment_get_probe (env, 0) == probe);
	g_assert_true (lrg_reflection_probe_component_get_needs_capture (comp));
	g_assert_cmpuint (lrg_reflection_probe_component_count_pending (world), ==, 1);
	lrg_reflection_probe_get_position (probe, v);
	g_assert_cmpfloat_with_epsilon (v[0], 1.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (v[2], 3.0f, 1e-5f);
	lrg_reflection_probe_get_extents (probe, v);
	g_assert_cmpfloat_with_epsilon (v[0], 2.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (v[1], 2.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (v[2], 2.0f, 1e-5f);

	/* a disabled component leaves the environment, enabling brings it back */
	lrg_component_set_enabled (LRG_COMPONENT (comp), FALSE);
	lrg_reflection_probe_component_sync (comp);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 0);
	g_assert_cmpuint (lrg_reflection_probe_component_count_pending (world), ==, 0);
	lrg_component_set_enabled (LRG_COMPONENT (comp), TRUE);
	lrg_reflection_probe_component_sync (comp);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 1);

	/* a new world environment: the next sync (late update) moves the probe */
	lrg_world3d_set_environment (world, env2);
	lrg_world3d_step (world, 0.016f);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 0);
	g_assert_cmpuint (lrg_environment_get_n_probes (env2), ==, 1);

	/* no environment: the probe is unregistered */
	lrg_world3d_set_environment (world, NULL);
	lrg_reflection_probe_component_sync (comp);
	g_assert_cmpuint (lrg_environment_get_n_probes (env2), ==, 0);
	lrg_world3d_set_environment (world, env);
	lrg_reflection_probe_component_sync (comp);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 1);

	/* leaving the world unregisters */
	lrg_entity3d_remove_child (lrg_world3d_get_root (world), entity);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 0);
}

static void
test_probe_component_auto_recapture (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(LrgEntity3D)    entity = lrg_entity3d_new_with_name ("Probe");
	g_autoptr(LrgReflectionProbeComponent) comp = lrg_reflection_probe_component_new ();
	gfloat                    pos[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                    p[3];

	lrg_world3d_set_environment (world, env);
	lrg_entity3d_add_component (entity, LRG_COMPONENT (comp));
	lrg_entity3d_add_child (lrg_world3d_get_root (world), entity);
	g_assert_true (lrg_reflection_probe_component_get_needs_capture (comp));

	/* with auto-recapture off a move updates the box, requests nothing
	 * beyond the pending first capture */
	lrg_reflection_probe_component_set_auto_recapture (comp, FALSE);
	lrg_entity3d_set_position (entity, pos);
	lrg_world3d_step (world, 0.016f);
	lrg_reflection_probe_get_position (lrg_reflection_probe_component_get_probe (comp), p);
	g_assert_cmpfloat_with_epsilon (p[1], 1.0f, 1e-5f);
	g_assert_true (lrg_reflection_probe_component_get_needs_capture (comp));
	g_object_set (comp, "auto-recapture", TRUE, NULL);
	g_assert_true (lrg_reflection_probe_component_get_auto_recapture (comp));
}

/* ==========================================================================
 * Instantiation of the level look
 * ========================================================================== */

static const gchar *look_level =
	"type: level\n"
	"name: look\n"
	"default_view: 3d\n"
	"environment:\n"
	"  sky:\n"
	"    source: equirect\n"
	"    path: skies/sky.hdr\n"
	"    exposure: 0.5\n"
	"  ambient:\n"
	"    color: [0.1, 0.2, 0.3]\n"
	"  post_stack: post/look.rpost\n"
	"nodes:\n"
	"  - name: Decal\n"
	"    transform:\n"
	"      location: [0.0, 0.0, 0.0]\n"
	"    visual:\n"
	"      kind: decal\n"
	"      asset: textures/splat.png\n"
	"      material:\n"
	"        color: [1.0, 0.5, 0.25, 0.75]\n"
	"      params:\n"
	"        size_x: 2.0\n"
	"        size_y: 0.5\n"
	"        size_z: 3.0\n"
	"        fade_angle: 45.0\n"
	"        opacity: 0.6\n"
	"        normal: textures/missing_normal.png\n"
	"  - name: Probe\n"
	"    transform:\n"
	"      location: [1.0, 2.0, 3.0]\n"
	"    visual:\n"
	"      kind: reflection-probe\n"
	"      params:\n"
	"        extents_x: 4.0\n"
	"        extents_y: 2.0\n"
	"        extents_z: 6.0\n"
	"        blend_distance: 0.25\n"
	"        intensity: 1.5\n"
	"        box_projection: false\n"
	"        resolution: 64\n"
	"  - name: Statue\n"
	"    visual:\n"
	"      kind: mesh-asset\n"
	"      asset: models/statue.glb\n"
	"  - name: Crate\n"
	"    visual:\n"
	"      kind: mesh-asset\n"
	"      asset: models/crate.glb\n";

static const gchar *look_post =
	"version: 1\n"
	"name: Look\n"
	"effects:\n"
	"  - type: bloom\n"
	"  - type: tonemap\n"
	"    params:\n"
	"      operator: aces\n"
	"  - type: vignette\n";

/* Writes the level's asset tree into @dir. */
static void
write_look_assets (const gchar *dir)
{
	g_autoptr(LrgHdrImage)  sky = lrg_hdr_image_new (8, 4);
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autofree gchar       *sky_path = g_build_filename (dir, "skies", "sky.hdr", NULL);
	g_autofree gchar       *statue = g_build_filename (dir, "models", "statue.glb", NULL);
	g_autofree gchar       *meta_path = NULL;
	g_autoptr(GError)       error = NULL;
	gfloat                  rgb[3] = { 0.5f, 0.6f, 0.9f };
	guint                   x;
	guint                   y;

	for (y = 0; y < 4; y++)
		for (x = 0; x < 8; x++)
			lrg_hdr_image_set_pixel (sky, x, y, rgb);
	g_mkdir_with_parents (g_path_get_dirname (sky_path), 0755);
	g_assert_true (lrg_hdr_image_save_to_file (sky, sky_path, &error));
	g_assert_no_error (error);

	write_file (dir, "post/look.rpost", look_post);
	write_file (dir, "textures/splat.png", "not really a png");
	/* the LOD decision reads only the sidecar: the model is never loaded */
	write_file (dir, "models/statue.glb", "glb");
	write_file (dir, "models/crate.glb", "glb");
	meta = lrg_asset_meta_new_for_asset (statue);
	lrg_asset_meta_set_model_lod_levels (meta, 3);
	meta_path = g_strconcat (statue, ".meta", NULL);
	g_assert_true (lrg_asset_meta_save_to_file (meta, meta_path, &error));
	g_assert_no_error (error);
}

static LrgLevel *
load_look_level (void)
{
	g_autoptr(LrgLevelSerializer) serializer = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	LrgLevel                     *level;

	level = lrg_level_serializer_load_from_data (serializer, look_level, -1, &error);
	g_assert_no_error (error);
	g_assert_nonnull (level);
	return level;
}

static void
test_instantiate_look (void)
{
	g_autofree gchar         *dir = make_temp_dir ();
	g_autoptr(LrgLevel)       level = NULL;
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(GError)         error = NULL;
	LrgEnvironment           *env;
	LrgPostStack             *stack;
	LrgDecalComponent        *decal;
	LrgReflectionProbeComponent *probe;
	LrgLodComponent          *lod;
	gfloat                    v[3];
	gfloat                    f;
	g_autofree gchar         *normal = NULL;

	write_look_assets (dir);
	level = load_look_level ();

	/* LOD components load through the engine's asset manager */
	g_assert_true (lrg_engine_startup (lrg_engine_get_default (), &error));
	g_assert_no_error (error);
	g_assert_true (lrg_level_instantiate_3d_with_base_dir (level, world, NULL, dir, &error));
	g_assert_no_error (error);

	/* the environment: a copy (the level's stays untouched), sky loaded */
	env = lrg_world3d_get_environment (world);
	g_assert_nonnull (env);
	g_assert_true (env != lrg_level_get_environment (level));
	g_assert_cmpint (lrg_environment_get_sky_source (env), ==, LRG_ENVIRONMENT_SKY_EQUIRECT);
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_exposure (env), 0.5f, 1e-5f);
	lrg_environment_get_ambient (env, v, &f);
	g_assert_cmpfloat_with_epsilon (v[2], 0.3f, 1e-5f);
	g_assert_nonnull (lrg_environment_get_equirect (env));
	g_assert_cmpuint (lrg_hdr_image_get_width (lrg_environment_get_equirect (env)), ==, 8);
	g_assert_null (lrg_environment_get_equirect (lrg_level_get_environment (level)));

	/* the post stack */
	stack = lrg_world3d_get_post_stack (world);
	g_assert_nonnull (stack);
	g_assert_cmpuint (lrg_post_stack_get_effect_count (stack), ==, 3);
	g_assert_cmpstr (lrg_post_stack_get_effect_type (stack, 0), ==, "bloom");
	g_assert_cmpstr (lrg_post_stack_get_effect_type (stack, 1), ==, "tonemap");

	/* the decal */
	decal = find_component (world, "Decal", LRG_TYPE_DECAL_COMPONENT);
	g_assert_nonnull (decal);
	lrg_decal_component_get_size (decal, v);
	g_assert_cmpfloat (v[0], ==, 2.0f);
	g_assert_cmpfloat (v[1], ==, 0.5f);
	g_assert_cmpfloat (v[2], ==, 3.0f);
	g_assert_true (g_path_is_absolute (lrg_decal_component_get_albedo_path (decal)));
	g_assert_true (g_str_has_suffix (lrg_decal_component_get_albedo_path (decal), "textures/splat.png"));
	g_object_get (decal, "normal-path", &normal, "opacity", &f, NULL);
	g_assert_cmpstr (normal, ==, "textures/missing_normal.png");   /* unresolved: kept */
	g_assert_cmpfloat_with_epsilon (f, 0.6f, 1e-5f);
	g_object_get (decal, "color-g", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 0.5f, 1e-5f);
	g_object_get (decal, "fade-angle", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 45.0f, 1e-4f);

	/* the reflection probe: configured and on the world's environment */
	probe = find_component (world, "Probe", LRG_TYPE_REFLECTION_PROBE_COMPONENT);
	g_assert_nonnull (probe);
	lrg_reflection_probe_component_get_extents (probe, v);
	g_assert_cmpfloat (v[0], ==, 4.0f);
	g_assert_cmpfloat (v[2], ==, 6.0f);
	g_assert_cmpfloat (lrg_reflection_probe_get_intensity (lrg_reflection_probe_component_get_probe (probe)), ==, 1.5f);
	g_assert_false (lrg_reflection_probe_get_box_projection (lrg_reflection_probe_component_get_probe (probe)));
	g_assert_cmpuint (lrg_reflection_probe_get_resolution (lrg_reflection_probe_component_get_probe (probe)), ==, 64);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 1);
	g_assert_cmpuint (lrg_environment_get_n_probes (lrg_level_get_environment (level)), ==, 0);
	lrg_reflection_probe_get_position (lrg_reflection_probe_component_get_probe (probe), v);
	g_assert_cmpfloat_with_epsilon (v[1], 2.0f, 1e-5f);

	/* LOD: only the mesh asset whose sidecar asks for it */
	lod = find_component (world, "Statue", LRG_TYPE_LOD_COMPONENT);
	g_assert_nonnull (lod);
	g_assert_null (find_component (world, "Statue", LRG_TYPE_MESH_RENDERER_COMPONENT));
	g_assert_true (g_path_is_absolute (lrg_lod_component_get_mesh_asset (lod)));
	g_assert_nonnull (lrg_lod_component_get_asset_manager (lod));
	g_assert_null (find_component (world, "Crate", LRG_TYPE_LOD_COMPONENT));
	g_assert_nonnull (find_component (world, "Crate", LRG_TYPE_MESH_RENDERER_COMPONENT));

	g_clear_object (&world);
	lrg_engine_shutdown (lrg_engine_get_default ());
	remove_tree (dir);
}

/* Sky images the level already loaded are shared, not read again. */
static void
test_instantiate_shares_loaded_sky (void)
{
	g_autoptr(LrgLevel)    level = load_look_level ();
	g_autoptr(LrgWorld3D)  world = lrg_world3d_new ();
	g_autoptr(LrgHdrImage) sky = lrg_hdr_image_new (4, 2);
	g_autoptr(GError)      error = NULL;

	/* no base dir, nothing on disk: only the shared image can work */
	lrg_environment_set_equirect (lrg_level_get_environment (level), sky);
	g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*post stack*");
	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_test_assert_expected_messages ();
	g_assert_no_error (error);
	g_assert_true (lrg_environment_get_equirect (lrg_world3d_get_environment (world)) == sky);
	g_assert_null (lrg_world3d_get_post_stack (world));
}

/* A level without an environment block leaves the world's look alone. */
static void
test_instantiate_no_environment (void)
{
	g_autoptr(LrgLevel)       level = lrg_level_new ("empty");
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(GError)         error = NULL;

	lrg_world3d_set_environment (world, env);
	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_true (lrg_world3d_get_environment (world) == env);
}

/* lrg_level_load_world3d() resolves against the level file's directory. */
static void
test_load_world3d_base_dir (void)
{
	g_autofree gchar     *dir = make_temp_dir ();
	g_autofree gchar     *path = NULL;
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	g_autoptr(LrgLevel)   level = NULL;
	g_autoptr(GError)     error = NULL;

	write_look_assets (dir);
	write_file (dir, "look.rlevel", look_level);
	path = g_build_filename (dir, "look.rlevel", NULL);
	level = lrg_level_load_world3d (path, world, &error);
	g_assert_no_error (error);
	g_assert_nonnull (level);
	g_assert_nonnull (lrg_environment_get_equirect (lrg_world3d_get_environment (world)));
	g_assert_nonnull (lrg_world3d_get_post_stack (world));

	g_clear_object (&world);
	remove_tree (dir);
}

/* The visual kinds map to the new component types. */
static void
test_visual_component_types (void)
{
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (LRG_NODE_VISUAL_REFLECTION_PROBE);
	g_autoptr(LrgComponent)  comp = NULL;
	GValue                   v = G_VALUE_INIT;

	g_assert_cmpuint (lrg_level_visual_get_component_type (LRG_NODE_VISUAL_DECAL), ==,
	                  LRG_TYPE_DECAL_COMPONENT);
	g_assert_cmpuint (lrg_level_visual_get_component_type (LRG_NODE_VISUAL_REFLECTION_PROBE), ==,
	                  LRG_TYPE_REFLECTION_PROBE_COMPONENT);

	lrg_node_visual_set_param_double (visual, "intensity", 3.0);
	comp = lrg_level_visual_create_component (visual);
	g_assert_true (LRG_IS_REFLECTION_PROBE_COMPONENT (comp));

	/* play-in-editor's "apply to level" stores the value back by name */
	g_value_init (&v, G_TYPE_FLOAT);
	g_value_set_float (&v, 0.75f);
	g_assert_true (lrg_level_visual_store_property (visual, "blend-distance", &v));
	g_value_unset (&v);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (visual, "blend_distance", 0.0),
	                                0.75, 1e-6);
}

/* ==========================================================================
 * Registry catalog
 * ========================================================================== */

static void
test_registry_3d_components (void)
{
	g_autoptr(LrgRegistry) registry = lrg_registry_new ();
	const struct { const gchar *name; GType type; } rows[] = {
		{ "mesh-renderer",    LRG_TYPE_MESH_RENDERER_COMPONENT },
		{ "skinned-model",    LRG_TYPE_SKINNED_MODEL_COMPONENT },
		{ "light",            LRG_TYPE_LIGHT_COMPONENT },
		{ "camera",           LRG_TYPE_CAMERA_COMPONENT },
		{ "audio-source",     LRG_TYPE_AUDIO_SOURCE_COMPONENT },
		{ "audio-listener",   LRG_TYPE_AUDIO_LISTENER_COMPONENT },
		{ "billboard",        LRG_TYPE_BILLBOARD_COMPONENT },
		{ "tilemap",          LRG_TYPE_TILEMAP_COMPONENT },
		{ "spawn",            LRG_TYPE_SPAWN_COMPONENT },
		{ "trigger",          LRG_TYPE_TRIGGER_COMPONENT },
		{ "decal",            LRG_TYPE_DECAL_COMPONENT },
		{ "lod",              LRG_TYPE_LOD_COMPONENT },
		{ "reflection-probe", LRG_TYPE_REFLECTION_PROBE_COMPONENT },
	};
	guint i;

	lrg_registry_register_builtin (registry);
	for (i = 0; i < G_N_ELEMENTS (rows); i++)
	{
		g_autoptr(GObject) obj = NULL;

		g_assert_cmpuint (lrg_registry_lookup (registry, rows[i].name), ==, rows[i].type);
		obj = lrg_registry_create (registry, rows[i].name, NULL);
		g_assert_nonnull (obj);
		g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (obj, rows[i].type));
	}
}

/* ==========================================================================
 * GPU: captures clear pending requests
 * ========================================================================== */

static void
draw_nothing (LrgReflectionProbe *probe,
              guint               face,
              const gfloat       *view,
              const gfloat       *projection,
              gpointer            user_data)
{
	guint *faces = user_data;

	(void) probe;
	(void) face;
	(void) view;
	(void) projection;
	(*faces)++;
}

static void
test_probe_capture_world (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(LrgEntity3D)    entity = lrg_entity3d_new_with_name ("Probe");
	g_autoptr(LrgReflectionProbeComponent) comp = lrg_reflection_probe_component_new ();
	LrgReflectionProbe       *probe = lrg_reflection_probe_component_get_probe (comp);
	g_autoptr(GError)         error = NULL;
	guint                     faces = 0;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}

	lrg_world3d_set_environment (world, env);
	g_object_set (comp, "resolution", 16u, NULL);
	lrg_entity3d_add_component (entity, LRG_COMPONENT (comp));
	lrg_entity3d_add_child (lrg_world3d_get_root (world), entity);

	g_assert_cmpint (lrg_reflection_probe_component_capture_world (world, FALSE, draw_nothing,
	                                                               &faces, &error), ==, 1);
	g_assert_no_error (error);
	g_assert_cmpuint (faces, ==, 6);
	g_assert_true (lrg_reflection_probe_is_ready (probe));
	g_assert_false (lrg_reflection_probe_component_get_needs_capture (comp));
	g_assert_cmpint (lrg_reflection_probe_component_capture_world (world, FALSE, draw_nothing,
	                                                               &faces, &error), ==, 0);

	/* a request, satisfied by a capture made directly on the probe */
	lrg_reflection_probe_component_request_capture (comp);
	g_assert_true (lrg_reflection_probe_component_get_needs_capture (comp));
	g_assert_true (lrg_reflection_probe_capture (probe, draw_nothing, &faces, &error));
	g_assert_false (lrg_reflection_probe_component_get_needs_capture (comp));

	/* forced: captured again although nothing is pending */
	g_assert_cmpint (lrg_reflection_probe_component_capture_world (world, TRUE, draw_nothing,
	                                                               &faces, &error), ==, 1);
	lrg_reflection_probe_release_gpu (probe);
}

int
main (int   argc,
      char *argv[])
{
	int ret;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/level-look/probe-component/properties", test_probe_component_properties);
	g_test_add_func ("/level-look/probe-component/registration", test_probe_component_registration);
	g_test_add_func ("/level-look/probe-component/auto-recapture", test_probe_component_auto_recapture);
	g_test_add_func ("/level-look/instantiate/look", test_instantiate_look);
	g_test_add_func ("/level-look/instantiate/shares-loaded-sky", test_instantiate_shares_loaded_sky);
	g_test_add_func ("/level-look/instantiate/no-environment", test_instantiate_no_environment);
	g_test_add_func ("/level-look/instantiate/load-world3d", test_load_world3d_base_dir);
	g_test_add_func ("/level-look/instantiate/visual-types", test_visual_component_types);
	g_test_add_func ("/level-look/registry/3d-components", test_registry_3d_components);
	g_test_add_func ("/level-look/gpu/capture-world", test_probe_capture_world);

	ret = g_test_run ();
	g_clear_object (&test_window);
	return ret;
}
