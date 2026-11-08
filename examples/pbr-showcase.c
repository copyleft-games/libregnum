/* pbr-showcase.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 rendering features in one level, loaded the way a game loads
 * any level (lrg_level_load_world3d()) and drawn with LrgWorld3DRenderer:
 *
 * - an environment (equirect HDR sky, image-based lighting, fog,
 *   volumetric fog) and a post stack (.rpost: bloom, ACES tonemap,
 *   vignette), both authored in the level's `environment:` block and put
 *   on the world by lrg_level_instantiate_3d();
 * - a grid of spheres with .rmat materials: a metal row and a dielectric
 *   row, roughness rising left to right (material instances of two
 *   parents);
 * - a sun, a spot and a point light, all with shadows;
 * - a decal on the floor, a reflection probe around the stage;
 * - a chrome knot drawn through an LrgLodComponent (its .meta asks for
 *   three levels of detail);
 * - SSAO and keyboard toggles for every feature, with a legend.
 *
 * The level is examples/data/pbr-showcase/pbr-showcase.rlevel.  On its
 * first run the example writes the binary assets the level references
 * next to it (the HDR sky, the decal texture, the knot model and its
 * .meta) when they are missing.
 *
 * Controls: 1..8 toggle features, V cycles the view mode, R recaptures
 * the reflection probe, arrows or right mouse drag orbit, mouse wheel
 * or +/- zoom, Space toggles the automatic orbit, Esc quits.
 *
 * Run with --help for options.
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <rlgl.h>
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>

#define DEFAULT_LEVEL   "data/pbr-showcase/pbr-showcase.rlevel"
#define WINDOW_W        (1280)
#define WINDOW_H        (720)
#define SKY_W           (512)
#define SKY_H           (256)

static const gchar *license_text =
	"pbr-showcase - libregnum physically based rendering showcase\n"
	"Copyright (C) 2026  Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* ==========================================================================
 * Generated assets
 *
 * The level references a few binary files; rather than shipping them,
 * the example writes them next to the level the first time it runs.
 * ========================================================================== */

/*
 * write_sky:
 *
 * An equirectangular HDR sky: a blue zenith fading to a hazy horizon,
 * a brown ground, and a bright sun in the direction the level's Sun
 * light comes from, so the image-based lighting and the reflections
 * agree with the direct light.  Values are linear radiance; the sun disk
 * is far brighter than 1 so it blooms in reflections.
 */
static gboolean
write_sky (const gchar  *path,
           GError      **error)
{
	g_autoptr(LrgHdrImage) sky = lrg_hdr_image_new (SKY_W, SKY_H);
	gfloat                 sun[3] = { 0.48f, 0.72f, 0.36f };
	gfloat                 len;
	guint                  x;
	guint                  y;

	len = sqrtf (sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
	sun[0] /= len;
	sun[1] /= len;
	sun[2] /= len;

	for (y = 0; y < SKY_H; y++)
	{
		/* the engine's mapping: v = acos (y) / pi, u = 0.5 looks down -Z,
		 * u grows toward +X (lrg_hdr_image_direction_to_uv()) */
		gfloat theta = ((gfloat) y + 0.5f) / (gfloat) SKY_H * (gfloat) G_PI;
		gfloat dy = cosf (theta);

		for (x = 0; x < SKY_W; x++)
		{
			gfloat phi = (((gfloat) x + 0.5f) / (gfloat) SKY_W - 0.5f) * 2.0f * (gfloat) G_PI;
			gfloat d[3];
			gfloat rgb[3];
			gfloat cos_sun;
			gfloat t;

			d[0] = sinf (phi) * sinf (theta);
			d[1] = dy;
			d[2] = -cosf (phi) * sinf (theta);

			if (dy >= 0.0f)
			{
				/* sky: horizon haze to a deep zenith */
				t = powf (dy, 0.45f);
				rgb[0] = 0.85f * (1.0f - t) + 0.16f * t;
				rgb[1] = 0.88f * (1.0f - t) + 0.32f * t;
				rgb[2] = 0.95f * (1.0f - t) + 0.75f * t;
			}
			else
			{
				/* ground: warm brown, darker straight down */
				t = powf (-dy, 0.6f);
				rgb[0] = 0.32f * (1.0f - t) + 0.12f * t;
				rgb[1] = 0.27f * (1.0f - t) + 0.10f * t;
				rgb[2] = 0.22f * (1.0f - t) + 0.08f * t;
			}

			/* the sun: a tight hot disk and a wide soft glow */
			cos_sun = d[0] * sun[0] + d[1] * sun[1] + d[2] * sun[2];
			if (cos_sun > 0.9995f)
			{
				rgb[0] += 90.0f;
				rgb[1] += 80.0f;
				rgb[2] += 64.0f;
			}
			else if (cos_sun > 0.0f)
			{
				gfloat glow = powf (cos_sun, 64.0f) * 2.0f;

				rgb[0] += glow * 1.0f;
				rgb[1] += glow * 0.85f;
				rgb[2] += glow * 0.6f;
			}
			lrg_hdr_image_set_pixel (sky, x, y, rgb);
		}
	}
	return lrg_hdr_image_save_to_file (sky, path, error);
}

/*
 * write_decal:
 *
 * The decal picture: a ringed emblem with the engine's name on a
 * transparent background (alpha masks the projection).  Needs the window
 * (the default font).
 */
static gboolean
write_decal (const gchar *path)
{
	Image    img = GenImageColor (256, 256, BLANK);
	gboolean ok;
	gint     r;

	/* soft-edged rings, painted outside in */
	for (r = 120; r > 0; r -= 2)
	{
		Color c;

		if (r > 108)
			c = (Color) { 250, 180, 40, (unsigned char) (255 - (r - 108) * 18) };
		else if (r > 92)
			c = (Color) { 30, 26, 24, 230 };
		else if (r > 84)
			c = (Color) { 250, 180, 40, 255 };
		else
			c = (Color) { 24, 60, 110, 235 };
		ImageDrawCircle (&img, 128, 128, r, c);
	}
	ImageDrawText (&img, "libregnum", 128 - MeasureText ("libregnum", 30) / 2, 104, 30, RAYWHITE);
	ImageDrawText (&img, "0.9", 128 - MeasureText ("0.9", 30) / 2, 138, 30, (Color) { 250, 180, 40, 255 });
	ok = ExportImage (img, path);
	UnloadImage (img);
	return ok;
}

/*
 * write_knot:
 *
 * A dense trefoil knot (about 5000 triangles) as OBJ, plus the .meta
 * sidecar that asks for an LOD chain of three levels.  That sidecar is
 * what makes lrg_level_instantiate_3d() give the node an
 * LrgLodComponent.
 */
static gboolean
write_knot (const gchar  *path,
            GError      **error)
{
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autofree gchar       *meta_path = g_strconcat (path, ".meta", NULL);
	Mesh                    mesh;
	gboolean                ok;

	if (!g_file_test (path, G_FILE_TEST_EXISTS))
	{
		mesh = GenMeshKnot (1.0f, 0.42f, 16, 160);
		ok = ExportMesh (mesh, path);
		UnloadMesh (mesh);
		if (!ok)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "cannot write %s", path);
			return FALSE;
		}
	}
	if (g_file_test (meta_path, G_FILE_TEST_EXISTS))
		return TRUE;
	meta = lrg_asset_meta_new_for_asset (path);
	lrg_asset_meta_set_model_lod_levels (meta, 3);
	lrg_asset_meta_set_model_lod_ratios (meta, "1.0, 0.3, 0.08");
	lrg_asset_meta_set_model_lod_screen_sizes (meta, "0.12, 0.04, 0.0");
	return lrg_asset_meta_save_to_file (meta, meta_path, error);
}

/* Writes every missing generated asset under @dir. */
static void
ensure_assets (const gchar *dir)
{
	g_autofree gchar  *skies = g_build_filename (dir, "skies", NULL);
	g_autofree gchar  *textures = g_build_filename (dir, "textures", NULL);
	g_autofree gchar  *models = g_build_filename (dir, "models", NULL);
	g_autofree gchar  *sky = g_build_filename (skies, "showcase-sky.hdr", NULL);
	g_autofree gchar  *decal = g_build_filename (textures, "showcase-decal.png", NULL);
	g_autofree gchar  *knot = g_build_filename (models, "knot.obj", NULL);
	g_autoptr(GError)  error = NULL;

	g_mkdir_with_parents (skies, 0755);
	g_mkdir_with_parents (textures, 0755);
	g_mkdir_with_parents (models, 0755);

	if (!g_file_test (sky, G_FILE_TEST_EXISTS) && !write_sky (sky, &error))
	{
		g_printerr ("pbr-showcase: cannot write the sky: %s\n", error->message);
		g_clear_error (&error);
	}
	if (!g_file_test (decal, G_FILE_TEST_EXISTS) && !write_decal (decal))
		g_printerr ("pbr-showcase: cannot write %s\n", decal);
	if (!write_knot (knot, &error))
		g_printerr ("pbr-showcase: cannot write the knot: %s\n", error->message);
}

/* ==========================================================================
 * The orbit camera
 *
 * Drives the level's camera entity around a target point: yaw, pitch and
 * distance, applied as the entity's position and Euler rotation (the
 * camera component follows its entity).
 * ========================================================================== */

typedef struct
{
	LrgEntity3D *entity;     /* the level's camera entity (borrowed) */
	gfloat       target[3];
	gfloat       yaw;        /* radians, 0 = looking down -Z */
	gfloat       pitch;      /* radians, positive = above the target */
	gfloat       distance;
	gboolean     auto_orbit;
} Orbit;

static void
orbit_apply (Orbit *o)
{
	gfloat pos[3];
	gfloat euler[3];

	if (o->entity == NULL)
		return;
	pos[0] = o->target[0] + o->distance * sinf (o->yaw) * cosf (o->pitch);
	pos[1] = o->target[1] + o->distance * sinf (o->pitch);
	pos[2] = o->target[2] + o->distance * cosf (o->yaw) * cosf (o->pitch);
	euler[0] = -o->pitch;
	euler[1] = o->yaw;
	euler[2] = 0.0f;
	lrg_entity3d_set_position (o->entity, pos);
	lrg_entity3d_set_rotation_euler (o->entity, euler);
}

static void
orbit_update (Orbit  *o,
              gfloat  delta)
{
	Vector2 md;

	if (o->auto_orbit)
		o->yaw += 0.12f * delta;
	if (IsKeyDown (KEY_LEFT))
		o->yaw -= 1.2f * delta;
	if (IsKeyDown (KEY_RIGHT))
		o->yaw += 1.2f * delta;
	if (IsKeyDown (KEY_UP))
		o->pitch += 0.8f * delta;
	if (IsKeyDown (KEY_DOWN))
		o->pitch -= 0.8f * delta;
	if (IsMouseButtonDown (MOUSE_BUTTON_RIGHT))
	{
		md = GetMouseDelta ();
		o->yaw -= md.x * 0.005f;
		o->pitch += md.y * 0.005f;
	}
	o->distance -= GetMouseWheelMove () * 0.8f;
	if (IsKeyDown (KEY_EQUAL) || IsKeyDown (KEY_KP_ADD))
		o->distance -= 6.0f * delta;
	if (IsKeyDown (KEY_MINUS) || IsKeyDown (KEY_KP_SUBTRACT))
		o->distance += 6.0f * delta;
	o->pitch = CLAMP (o->pitch, 0.02f, 1.3f);
	o->distance = CLAMP (o->distance, 3.0f, 40.0f);
	orbit_apply (o);
}

/* ==========================================================================
 * Feature toggles and the legend
 * ========================================================================== */

typedef struct
{
	LrgWorld3DRenderer *renderer;
	LrgWorld3D         *world;
	gboolean            probes_on;
	gint                lod_force;    /* -1 automatic, else the level */
	guint               view;         /* index into view_modes */
} Showcase;

static const struct
{
	LrgWorld3DRenderMode  mode;
	const gchar          *name;
} view_modes[] = {
	{ LRG_WORLD3D_RENDER_MODE_LIT,              "lit" },
	{ LRG_WORLD3D_RENDER_MODE_LIGHTING_ONLY,    "lighting only" },
	{ LRG_WORLD3D_RENDER_MODE_REFLECTIONS,      "reflections" },
	{ LRG_WORLD3D_RENDER_MODE_AO,               "ambient occlusion" },
	{ LRG_WORLD3D_RENDER_MODE_LIGHT_COMPLEXITY, "light complexity" },
	{ LRG_WORLD3D_RENDER_MODE_NORMALS,          "normals" },
	{ LRG_WORLD3D_RENDER_MODE_WIREFRAME,        "wireframe" },
};

/* Flips one renderer flag. */
static void
toggle_flag (LrgWorld3DRenderer    *renderer,
             LrgWorld3DRenderFlags  flag)
{
	lrg_world3d_renderer_set_flags (renderer, lrg_world3d_renderer_get_flags (renderer) ^ flag);
}

/* Enables or disables every reflection probe component (a disabled
 * component takes its probe out of the environment). */
static void
set_probes (Showcase *s,
            gboolean  on)
{
	g_autoptr(GPtrArray) comps = lrg_world3d_get_components_of_type (s->world,
	                                                                LRG_TYPE_REFLECTION_PROBE_COMPONENT);
	guint                i;

	s->probes_on = on;
	for (i = 0; comps != NULL && i < comps->len; i++)
	{
		lrg_component_set_enabled (g_ptr_array_index (comps, i), on);
		lrg_reflection_probe_component_sync (g_ptr_array_index (comps, i));
	}
}

/* Forces (or frees) the level of every LOD component. */
static void
set_lod (Showcase *s,
         gint      level)
{
	g_autoptr(GPtrArray) comps = lrg_world3d_get_components_of_type (s->world, LRG_TYPE_LOD_COMPONENT);
	guint                i;

	s->lod_force = level;
	for (i = 0; comps != NULL && i < comps->len; i++)
		lrg_lod_component_set_force_level (g_ptr_array_index (comps, i), level);
}

/* The first LOD component, for the legend. */
static LrgLodComponent *
first_lod (LrgWorld3D *world)
{
	g_autoptr(GPtrArray) comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_LOD_COMPONENT);

	return (comps != NULL && comps->len > 0) ? g_ptr_array_index (comps, 0) : NULL;
}

/*
 * press:
 *
 * The action of one feature key ('1' .. '8', 'v', 'r'), shared by the
 * keyboard and --keys.
 */
static void
press (Showcase *s,
       gchar     key)
{
	LrgWorld3DRenderer *r = s->renderer;

	switch (g_ascii_tolower (key))
	{
	case '1':
		lrg_world3d_renderer_set_ibl_enabled (r, !lrg_world3d_renderer_get_ibl_enabled (r));
		break;
	case '2':
		toggle_flag (r, LRG_WORLD3D_RENDER_FLAG_SHADOWS);
		break;
	case '3':
		lrg_world3d_renderer_set_ssao_enabled (r, !lrg_world3d_renderer_get_ssao_enabled (r));
		break;
	case '4':
		lrg_world3d_renderer_set_decals_enabled (r, !lrg_world3d_renderer_get_decals_enabled (r));
		break;
	case '5':
		toggle_flag (r, LRG_WORLD3D_RENDER_FLAG_POST);
		break;
	case '6':
		lrg_world3d_renderer_set_volumetric_enabled (r, !lrg_world3d_renderer_get_volumetric_enabled (r));
		break;
	case '7':
		set_probes (s, !s->probes_on);
		break;
	case '8':
		set_lod (s, s->lod_force >= 2 ? -1 : s->lod_force + 1);
		break;
	case 'v':
		s->view = (s->view + 1) % G_N_ELEMENTS (view_modes);
		lrg_world3d_renderer_set_render_mode (r, view_modes[s->view].mode);
		break;
	case 'r':
		{
			g_autoptr(GPtrArray) comps = lrg_world3d_get_components_of_type (s->world,
			                                                                LRG_TYPE_REFLECTION_PROBE_COMPONENT);
			guint i;

			for (i = 0; comps != NULL && i < comps->len; i++)
				lrg_reflection_probe_component_request_capture (g_ptr_array_index (comps, i));
		}
		break;
	default:
		break;
	}
}

static void
handle_keys (Showcase *s,
             Orbit    *orbit)
{
	static const struct { gint key; gchar c; } keys[] = {
		{ KEY_ONE, '1' }, { KEY_TWO, '2' }, { KEY_THREE, '3' }, { KEY_FOUR, '4' },
		{ KEY_FIVE, '5' }, { KEY_SIX, '6' }, { KEY_SEVEN, '7' }, { KEY_EIGHT, '8' },
		{ KEY_V, 'v' }, { KEY_R, 'r' },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (keys); i++)
		if (IsKeyPressed (keys[i].key))
			press (s, keys[i].c);
	if (IsKeyPressed (KEY_SPACE))
		orbit->auto_orbit = !orbit->auto_orbit;
}

/* One legend row: the key, the feature and its state. */
static void
legend_row (gint         x,
            gint         y,
            const gchar *key,
            const gchar *label,
            const gchar *state,
            gboolean     on)
{
	DrawText (key, x, y, 18, (Color) { 250, 190, 70, 255 });
	DrawText (label, x + 34, y, 18, RAYWHITE);
	DrawText (state, x + 200, y, 18, on ? (Color) { 120, 230, 140, 255 } : (Color) { 230, 110, 100, 255 });
}

static void
draw_legend (Showcase *s)
{
	LrgWorld3DRenderer *r = s->renderer;
	guint               flags = lrg_world3d_renderer_get_flags (r);
	LrgLodComponent    *lod = first_lod (s->world);
	g_autofree gchar   *lod_state = NULL;
	gint                x = 16;
	gint                y = 16;
	gint                dy = 24;

	if (lod != NULL)
		lod_state = g_strdup_printf ("%s %d", s->lod_force < 0 ? "auto" : "forced",
		                             lrg_lod_component_get_current_level (lod));
	else
		lod_state = g_strdup ("none");

	DrawRectangle (x - 8, y - 8, 300, dy * 12 + 12, (Color) { 12, 14, 20, 175 });
	DrawText ("libregnum 0.9 PBR showcase", x, y, 20, RAYWHITE);
	y += dy + 6;
	legend_row (x, y, "1", "image-based light", lrg_world3d_renderer_get_ibl_enabled (r) ? "on" : "off",
	            lrg_world3d_renderer_get_ibl_enabled (r));
	y += dy;
	legend_row (x, y, "2", "shadows", (flags & LRG_WORLD3D_RENDER_FLAG_SHADOWS) ? "on" : "off",
	            (flags & LRG_WORLD3D_RENDER_FLAG_SHADOWS) != 0);
	y += dy;
	legend_row (x, y, "3", "SSAO", lrg_world3d_renderer_get_ssao_enabled (r) ? "on" : "off",
	            lrg_world3d_renderer_get_ssao_enabled (r));
	y += dy;
	legend_row (x, y, "4", "decals", lrg_world3d_renderer_get_decals_enabled (r) ? "on" : "off",
	            lrg_world3d_renderer_get_decals_enabled (r));
	y += dy;
	legend_row (x, y, "5", "post (.rpost)", (flags & LRG_WORLD3D_RENDER_FLAG_POST) ? "on" : "off",
	            (flags & LRG_WORLD3D_RENDER_FLAG_POST) != 0);
	y += dy;
	legend_row (x, y, "6", "volumetric fog", lrg_world3d_renderer_get_volumetric_enabled (r) ? "on" : "off",
	            lrg_world3d_renderer_get_volumetric_enabled (r));
	y += dy;
	legend_row (x, y, "7", "reflection probe", s->probes_on ? "on" : "off", s->probes_on);
	y += dy;
	legend_row (x, y, "8", "knot LOD", lod_state, TRUE);
	y += dy;
	legend_row (x, y, "V", "view", view_modes[s->view].name, TRUE);
	y += dy;
	DrawText ("R recapture probe   Space orbit", x, y, 16, LIGHTGRAY);
	y += dy - 4;
	DrawText ("arrows / RMB orbit   wheel zoom", x, y, 16, LIGHTGRAY);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

static LrgEntity3D *
find_camera_entity (LrgWorld3D *world)
{
	g_autoptr(GPtrArray) cams = lrg_world3d_find_with_component (world, LRG_TYPE_CAMERA_COMPONENT);

	return (cams != NULL && cams->len > 0) ? g_ptr_array_index (cams, 0) : NULL;
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext)     ctx = NULL;
	g_autoptr(GError)             error = NULL;
	g_autoptr(GrlWindow)          window = NULL;
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgWorld3DRenderer) renderer = NULL;
	g_autoptr(LrgLevel)           level = NULL;
	g_autofree gchar             *opt_level = NULL;
	g_autofree gchar             *opt_screenshot = NULL;
	g_autofree gchar             *level_dir = NULL;
	gint                          opt_frames = 60;
	gboolean                      opt_license = FALSE;
	gboolean                      opt_no_legend = FALSE;
	g_autofree gchar             *opt_keys = NULL;
	LrgEngine                    *engine;
	Showcase                      show;
	Orbit                         orbit = { NULL, { 0.0f, 1.2f, 0.0f }, 0.0f, 0.24f, 12.5f, FALSE };
	gint                          frame = 0;

	GOptionEntry entries[] = {
		{ "level", 'l', 0, G_OPTION_ARG_FILENAME, &opt_level,
		  "The .rlevel file to show (default: " DEFAULT_LEVEL ")", "PATH" },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames,
		  "Frames rendered before --screenshot (default 60)", "N" },
		{ "no-legend", 0, 0, G_OPTION_ARG_NONE, &opt_no_legend, "Hide the key legend", NULL },
		{ "keys", 'k', 0, G_OPTION_ARG_STRING, &opt_keys,
		  "Press these feature keys at start (e.g. \"56\" turns post and volumetric fog off)", "KEYS" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- the 0.9.0 rendering features in one level");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Keys: 1 IBL, 2 shadows, 3 SSAO, 4 decals, 5 post stack, 6 volumetric fog,\n"
		"7 reflection probe, 8 knot LOD level (auto, 0, 1, 2), V view mode,\n"
		"R recapture the probe, arrows / right mouse drag orbit, wheel or +/- zoom,\n"
		"Space automatic orbit, Esc quit.\n\n"
		"Examples (from the examples/ directory):\n"
		"  pbr-showcase\n"
		"  pbr-showcase --screenshot showcase.png --frames 90\n"
		"  pbr-showcase --keys 1vv --screenshot no-ibl-reflections.png\n"
		"  pbr-showcase --level ~/game/levels/look-test.rlevel\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("pbr-showcase: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (opt_frames < 1)
		opt_frames = 1;
	if (opt_level == NULL)
		opt_level = g_strdup (DEFAULT_LEVEL);
	level_dir = g_path_get_dirname (opt_level);

	/* 1. The engine: its asset manager loads the .rmat materials and the
	 *    LOD chain; the level's directory is a search path for them. */
	engine = lrg_engine_get_default ();
	if (!lrg_engine_startup (engine, &error))
	{
		g_printerr ("pbr-showcase: engine startup failed: %s\n", error->message);
		return 1;
	}
	lrg_asset_manager_add_search_path (lrg_engine_get_asset_manager (engine), level_dir);

	/* 2. The window, then the generated assets (the decal and the knot
	 *    use raylib's CPU image / mesh helpers, which want a context). */
	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	window = grl_window_new (WINDOW_W, WINDOW_H, "libregnum - pbr-showcase");
	grl_window_set_target_fps (window, 60);
	ensure_assets (level_dir);

	/* 3. The level: entities, and the look (environment + post stack)
	 *    on the world. */
	world = lrg_world3d_new ();
	level = lrg_level_load_world3d (opt_level, world, &error);
	if (level == NULL)
	{
		g_printerr ("pbr-showcase: cannot load %s: %s\n", opt_level, error->message);
		return 1;
	}
	g_print ("pbr-showcase: %u entities, %u lights, environment %s, post stack %s\n",
	         lrg_world3d_get_entity_count (world), lrg_world3d_get_n_lights (world),
	         lrg_world3d_get_environment (world) != NULL ? "yes" : "no",
	         lrg_world3d_get_post_stack (world) != NULL ? "yes" : "no");

	/* 4. The renderer: post-processing and SSAO on (the level's post
	 *    stack is built into it), the environment baked and the probe
	 *    captured once, before the first frame. */
	renderer = lrg_world3d_renderer_new ();
	lrg_world3d_renderer_set_base_dir (renderer, level_dir);
	lrg_world3d_renderer_set_flags (renderer, lrg_world3d_renderer_get_flags (renderer)
	                                          | LRG_WORLD3D_RENDER_FLAG_POST);
	lrg_world3d_renderer_set_ssao_enabled (renderer, TRUE);

	orbit.entity = find_camera_entity (world);
	orbit_apply (&orbit);
	lrg_world3d_step (world, 1.0f / 60.0f);   /* camera, lights, probe boxes in place */

	if (!lrg_world3d_renderer_bake_environment (renderer, &error))
	{
		g_printerr ("pbr-showcase: environment bake failed: %s\n", error->message);
		g_clear_error (&error);
	}
	g_print ("pbr-showcase: captured %u reflection probe(s)\n",
	         lrg_world3d_renderer_capture_probes (renderer, world));

	show.renderer = renderer;
	show.world = world;
	show.probes_on = TRUE;
	show.lod_force = -1;
	show.view = 0;
	if (opt_keys != NULL)
	{
		const gchar *k;

		for (k = opt_keys; *k != '\0'; k++)
			press (&show, *k);
	}

	while (!grl_window_should_close (window))
	{
		/* a fixed step when capturing keeps screenshots reproducible */
		gfloat delta = (opt_screenshot != NULL) ? 1.0f / 60.0f : GetFrameTime ();

		handle_keys (&show, &orbit);
		orbit_update (&orbit, delta);
		lrg_world3d_step (world, delta);

		/* probes whose capture is pending (R, or a moved probe) */
		if (lrg_reflection_probe_component_count_pending (world) > 0)
			lrg_world3d_renderer_capture_probes (renderer, world);

		grl_window_begin_drawing (window);
		lrg_world3d_renderer_render (renderer, world, NULL, NULL, 0, 0);
		if (!opt_no_legend)
			draw_legend (&show);

		if (opt_screenshot != NULL && frame + 1 >= opt_frames)
		{
			Image shot;

			/* flush the queued 2D text before reading the pixels back */
			rlDrawRenderBatchActive ();
			shot = LoadImageFromScreen ();
			if (!ExportImage (shot, opt_screenshot))
				g_printerr ("pbr-showcase: could not write %s\n", opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	/* GPU resources go before the window closes */
	lrg_world3d_renderer_release (renderer);
	g_clear_object (&renderer);
	g_clear_object (&world);
	lrg_engine_shutdown (engine);
	return 0;
}
