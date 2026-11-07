/* lrg-foliage.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFoliage: scatter layers over a terrain, their density maps, the
 * `foliage:` block of a .rterrain, and instanced drawing.  See
 * lrg-foliage.h and docs/modules/graphics/foliage.org.
 *
 * Placement.  The terrain's cells are grouped into square patches of
 * `patch_cells` cells.  A layer keeps its instances per patch; marking a
 * region dirty (painting density, sculpting, painting surface layers)
 * re-scatters only the patches it overlaps.  In a cell, a layer has
 * `slots = ceil (density * cell area)` candidate slots (at most
 * LRG_FOLIAGE_MAX_PER_CELL).  Slot k of cell (i, j) has its own hash of
 * (foliage seed, layer seed, layer index, i, j, k): its first unit
 * decides acceptance (unit < expected / slots, where expected = density
 * x cell area x painted density x the surface-layer factor at the
 * slot's spot), the next ones its position inside the cell, yaw and
 * scale.  Then the slope and height rules reject candidates.  The result
 * depends on nothing else, so the same inputs always give the same
 * instances in the same order.
 *
 * Drawing.  Each layer is one transparent-category custom render item
 * (drawn after the opaque world; the draw writes depth itself and alpha
 * tests, so it behaves like cutout geometry, but stays out of the depth
 * prepass and shadow passes).  The draw culls patches against the view
 * frustum and the fade end, picks the LOD mesh per patch by distance and
 * issues one instanced draw per run of patches sharing a mesh, with a
 * small shader: instance transform, a gentle sway, vertex light from the
 * light set (lrg_light_sample) times a soft sun term, alpha test, a
 * dithered distance fade and fog.  Grass layers hand their blades to an
 * LrgGrassField instead.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-foliage.h"
#include "lrg-terrain-asset.h"
#include "lrg-terrain-asset-private.h"
#include "lrg-tree-mesh.h"
#include "lrg-grass-field.h"
#include "lrg-light-set.h"
#include "lrg-light-set-private.h"
#include "lrg-draw-cull-private.h"
#include "lrg-hash-noise-private.h"
#include "../core/lrg-data-loader.h"
#include "../world3d/lrg-world3d-renderer.h"

/* ==========================================================================
 * Enums and boxed types
 * ========================================================================== */

GType
lrg_foliage_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FOLIAGE_KIND_MESH, "LRG_FOLIAGE_KIND_MESH", "mesh" },
			{ LRG_FOLIAGE_KIND_BILLBOARD, "LRG_FOLIAGE_KIND_BILLBOARD", "billboard" },
			{ LRG_FOLIAGE_KIND_GRASS, "LRG_FOLIAGE_KIND_GRASS", "grass" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgFoliageKind"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

G_DEFINE_BOXED_TYPE (LrgFoliageInstance, lrg_foliage_instance, lrg_foliage_instance_copy, lrg_foliage_instance_free)

LrgFoliageInstance *
lrg_foliage_instance_copy (const LrgFoliageInstance *instance)
{
	g_return_val_if_fail (instance != NULL, NULL);
	return g_memdup2 (instance, sizeof *instance);
}

void
lrg_foliage_instance_free (LrgFoliageInstance *instance)
{
	g_free (instance);
}

void
lrg_foliage_instance_get_matrix (const LrgFoliageInstance *instance,
                                 gfloat                   *out)
{
	gfloat up[3], len, c, s, ax[3], axl, ca, sa, t, r[9], y[9];
	guint  i, j, k;

	g_return_if_fail (instance != NULL);
	g_return_if_fail (out != NULL);

	/* the yaw about +Y (columns of the rotation) */
	c = cosf (instance->yaw);
	s = sinf (instance->yaw);
	y[0] = c;    y[1] = 0.0f; y[2] = -s;
	y[3] = 0.0f; y[4] = 1.0f; y[5] = 0.0f;
	y[6] = s;    y[7] = 0.0f; y[8] = c;

	/* the tilt taking +Y onto up (Rodrigues about +Y x up) */
	len = sqrtf (instance->up[0] * instance->up[0] + instance->up[1] * instance->up[1] +
	             instance->up[2] * instance->up[2]);
	up[0] = len > 1e-6f ? instance->up[0] / len : 0.0f;
	up[1] = len > 1e-6f ? instance->up[1] / len : 1.0f;
	up[2] = len > 1e-6f ? instance->up[2] / len : 0.0f;
	ax[0] = up[2];
	ax[1] = 0.0f;
	ax[2] = -up[0];
	axl = sqrtf (ax[0] * ax[0] + ax[2] * ax[2]);
	if (axl < 1e-6f)
	{
		memcpy (r, y, sizeof r);
	}
	else
	{
		gfloat tilt[9];

		ax[0] /= axl;
		ax[2] /= axl;
		ca = CLAMP (up[1], -1.0f, 1.0f);
		sa = axl;
		t = 1.0f - ca;
		/* column-major 3x3 */
		tilt[0] = ca + ax[0] * ax[0] * t;
		tilt[1] = ax[2] * sa;
		tilt[2] = ax[0] * ax[2] * t;
		tilt[3] = -ax[2] * sa;
		tilt[4] = ca;
		tilt[5] = ax[0] * sa;
		tilt[6] = ax[0] * ax[2] * t;
		tilt[7] = -ax[0] * sa;
		tilt[8] = ca + ax[2] * ax[2] * t;
		/* r = tilt * yaw */
		for (j = 0; j < 3; j++)
			for (i = 0; i < 3; i++)
			{
				gfloat sum = 0.0f;

				for (k = 0; k < 3; k++)
					sum += tilt[k * 3 + i] * y[j * 3 + k];
				r[j * 3 + i] = sum;
			}
	}

	for (j = 0; j < 3; j++)
	{
		for (i = 0; i < 3; i++)
			out[j * 4 + i] = r[j * 3 + i] * instance->scale;
		out[j * 4 + 3] = 0.0f;
	}
	out[12] = instance->position[0];
	out[13] = instance->position[1];
	out[14] = instance->position[2];
	out[15] = 1.0f;
}

/* ==========================================================================
 * GPU-side helpers
 * ========================================================================== */

/* A patch's instances and their local bounds. */
typedef struct
{
	GArray   *instances;   /* LrgFoliageInstance */
	gboolean  dirty;
	gfloat    min[3];
	gfloat    max[3];
} Patch;

/* One drawable mesh of a layer (built here, or borrowed from a model). */
typedef struct
{
	Mesh     mesh;
	gboolean owned;        /* built by us: unload */
	guint    texture;      /* GL texture id, 0 = white */
	Color    color;
} LayerMesh;

static const gchar foliage_vs[] =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"in vec3 vertexNormal;\n"
	"in vec4 vertexColor;\n"
	"in mat4 instanceTransform;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 model;\n"
	"uniform vec3 viewPos;\n"
	"uniform vec2 fade;\n"
	"uniform vec3 sunDir;\n"
	"uniform float time;\n"
	"uniform float sway;\n"
	"uniform int lit;\n"
	LRG_LIGHT_SAMPLE_GLSL
	"out vec2 fragTexCoord;\n"
	"out vec4 fragColor;\n"
	"out vec3 fragLight;\n"
	"out float fragFade;\n"
	"out vec3 fragPosition;\n"
	"void main() {\n"
	"    vec4 lp = instanceTransform * vec4(vertexPosition, 1.0);\n"
	"    vec2 root = instanceTransform[3].xz;\n"
	"    float h = max(vertexPosition.y, 0.0);\n"
	"    float ph = dot(root, vec2(0.131, 0.173));\n"
	"    lp.x += sin(time * 1.3 + ph) * sway * h * h;\n"
	"    lp.z += cos(time * 1.1 + ph * 1.37) * sway * h * h * 0.6;\n"
	"    vec4 wp = model * lp;\n"
	"    vec3 n = normalize(mat3(model) * mat3(instanceTransform) * vertexNormal);\n"
	"    float lam = 0.55 + 0.45 * max(dot(n, -sunDir), 0.0);\n"
	"    fragLight = (lit == 1 ? lrg_light_sample(wp.xyz) : vec3(1.0)) * lam;\n"
	"    fragColor = vertexColor;\n"
	"    fragTexCoord = vertexTexCoord;\n"
	"    fragFade = 1.0 - smoothstep(fade.x, fade.y, distance(wp.xyz, viewPos));\n"
	"    fragPosition = wp.xyz;\n"
	"    gl_Position = mvp * lp;\n"
	"}\n";

static const gchar foliage_fs[] =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"in vec3 fragLight;\n"
	"in float fragFade;\n"
	"in vec3 fragPosition;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec4 colDiffuse;\n"
	LRG_FOG_GLSL
	"out vec4 finalColor;\n"
	"void main() {\n"
	"    vec4 t = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
	"    if (t.a < 0.5) discard;\n"
	"    float dither = fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)) * 52.9829189);\n"
	"    if (fragFade <= dither) discard;\n"
	"    vec3 c = t.rgb * fragLight;\n"
	"    finalColor = vec4(mix(c, fogColor.rgb, lrg_fog_amount(fragPosition)), 1.0);\n"
	"}\n";

/* ==========================================================================
 * LrgFoliageLayer
 * ========================================================================== */

struct _LrgFoliageLayer
{
	GObject         parent_instance;

	LrgFoliage     *owner;            /* weak: the foliage holding the layer */

	gchar          *name;
	LrgFoliageKind  kind;
	gchar          *mesh;
	gchar          *lod_mesh;
	gfloat          lod_distance;
	gchar          *texture;
	gfloat          color[4];
	gfloat          density;
	gfloat          material_density[LRG_TERRAIN_ASSET_MAX_LAYERS];
	gfloat          scale[2];
	gfloat          slope[2];
	gfloat          height[2];
	gboolean        has_height;
	gfloat          align;
	gfloat          size[2];
	gfloat          fade[2];
	guint32         seed;
	gchar          *density_file;
	YamlMapping    *extra;

	/* runtime */
	guint8         *density_map;      /* cells_x * cells_z */
	Patch          *patches;          /* n_patches */
	guint           n_patches;

	/* GPU */
	gboolean        gpu_dirty;        /* instances changed since upload */
	guint           vbo;
	guint           vbo_capacity;
	GArray         *run_first;        /* guint per patch: first instance */
	LayerMesh       meshes[2];        /* LOD 0, LOD 1 */
	guint           n_meshes[2];
	gboolean        meshes_tried;
	LrgGrassField  *grass;
	gfloat          grass_world[16];
	gboolean        grass_dirty;
};

G_DEFINE_FINAL_TYPE (LrgFoliageLayer, lrg_foliage_layer, G_TYPE_OBJECT)

static void layer_changed (LrgFoliageLayer *self);
static void layer_release_gpu (LrgFoliageLayer *self);

static void
lrg_foliage_layer_finalize (GObject *object)
{
	LrgFoliageLayer *self = LRG_FOLIAGE_LAYER (object);
	guint            p;

	layer_release_gpu (self);
	g_free (self->name);
	g_free (self->mesh);
	g_free (self->lod_mesh);
	g_free (self->texture);
	g_free (self->density_file);
	g_free (self->density_map);
	for (p = 0; p < self->n_patches; p++)
		g_array_unref (self->patches[p].instances);
	g_free (self->patches);
	g_clear_pointer (&self->run_first, g_array_unref);
	g_clear_object (&self->grass);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);
	G_OBJECT_CLASS (lrg_foliage_layer_parent_class)->finalize (object);
}

static void
lrg_foliage_layer_class_init (LrgFoliageLayerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_foliage_layer_finalize;
}

static void
lrg_foliage_layer_init (LrgFoliageLayer *self)
{
	guint l;

	self->color[0] = self->color[1] = self->color[2] = self->color[3] = 1.0f;
	self->density = 0.05f;
	for (l = 0; l < LRG_TERRAIN_ASSET_MAX_LAYERS; l++)
		self->material_density[l] = 1.0f;
	self->scale[0] = 0.8f;
	self->scale[1] = 1.2f;
	self->slope[0] = 0.0f;
	self->slope[1] = 35.0f;
	self->height[0] = -1.0e9f;
	self->height[1] = 1.0e9f;
	self->size[0] = 1.0f;
	self->size[1] = 1.0f;
	self->fade[0] = 80.0f;
	self->fade[1] = 100.0f;
	self->lod_distance = 40.0f;
	self->run_first = g_array_new (FALSE, FALSE, sizeof (guint));
}

LrgFoliageLayer *
lrg_foliage_layer_new (const gchar    *name,
                       LrgFoliageKind  kind)
{
	LrgFoliageLayer *self = (LrgFoliageLayer *) g_object_new (LRG_TYPE_FOLIAGE_LAYER, NULL);

	self->name = g_strdup (name);
	self->kind = kind;
	if (kind == LRG_FOLIAGE_KIND_GRASS)
	{
		self->fade[0] = 25.0f;
		self->fade[1] = 35.0f;
		self->size[0] = 0.4f;
		self->size[1] = 0.5f;
		self->density = 4.0f;
		self->color[0] = 0.45f;
		self->color[1] = 0.72f;
		self->color[2] = 0.28f;
	}
	return self;
}

const gchar *
lrg_foliage_layer_get_name (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), NULL);
	return self->name;
}

LrgFoliageKind
lrg_foliage_layer_get_kind (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), LRG_FOLIAGE_KIND_MESH);
	return self->kind;
}

const gchar *
lrg_foliage_layer_get_mesh (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), NULL);
	return self->mesh;
}

void
lrg_foliage_layer_set_mesh (LrgFoliageLayer *self,
                            const gchar     *mesh)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	g_free (self->mesh);
	self->mesh = g_strdup (mesh);
	layer_release_gpu (self);
}

const gchar *
lrg_foliage_layer_get_lod_mesh (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), NULL);
	return self->lod_mesh;
}

void
lrg_foliage_layer_set_lod_mesh (LrgFoliageLayer *self,
                                const gchar     *mesh,
                                gfloat           distance)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	g_free (self->lod_mesh);
	self->lod_mesh = g_strdup (mesh);
	if (distance > 0.0f && isfinite (distance))
		self->lod_distance = distance;
	layer_release_gpu (self);
}

gfloat
lrg_foliage_layer_get_lod_distance (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), 0.0f);
	return self->lod_distance;
}

const gchar *
lrg_foliage_layer_get_texture (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), NULL);
	return self->texture;
}

void
lrg_foliage_layer_set_texture (LrgFoliageLayer *self,
                               const gchar     *texture)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	g_free (self->texture);
	self->texture = g_strdup (texture);
	layer_release_gpu (self);
}

void
lrg_foliage_layer_get_color (LrgFoliageLayer *self,
                             gfloat          *out)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->color, sizeof self->color);
}

void
lrg_foliage_layer_set_color (LrgFoliageLayer *self,
                             gfloat           r,
                             gfloat           g,
                             gfloat           b,
                             gfloat           a)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	self->color[0] = CLAMP (r, 0.0f, 1.0f);
	self->color[1] = CLAMP (g, 0.0f, 1.0f);
	self->color[2] = CLAMP (b, 0.0f, 1.0f);
	self->color[3] = CLAMP (a, 0.0f, 1.0f);
	self->grass_dirty = TRUE;
}

gfloat
lrg_foliage_layer_get_density (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), 0.0f);
	return self->density;
}

void
lrg_foliage_layer_set_density (LrgFoliageLayer *self,
                               gfloat           density)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	self->density = (density >= 0.0f && isfinite (density)) ? density : 0.0f;
	layer_changed (self);
}

gfloat
lrg_foliage_layer_get_material_density (LrgFoliageLayer *self,
                                        guint            terrain_layer)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), 0.0f);
	if (terrain_layer >= LRG_TERRAIN_ASSET_MAX_LAYERS)
		return 0.0f;
	return self->material_density[terrain_layer];
}

void
lrg_foliage_layer_set_material_density (LrgFoliageLayer *self,
                                        guint            terrain_layer,
                                        gfloat           factor)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (terrain_layer >= LRG_TERRAIN_ASSET_MAX_LAYERS || !isfinite (factor))
		return;
	self->material_density[terrain_layer] = CLAMP (factor, 0.0f, 1.0f);
	layer_changed (self);
}

void
lrg_foliage_layer_get_scale_range (LrgFoliageLayer *self,
                                   gfloat          *min,
                                   gfloat          *max)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (min != NULL)
		*min = self->scale[0];
	if (max != NULL)
		*max = self->scale[1];
}

void
lrg_foliage_layer_set_scale_range (LrgFoliageLayer *self,
                                   gfloat           min,
                                   gfloat           max)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (!(min > 0.0f) || !(max >= min) || !isfinite (max))
		return;
	self->scale[0] = min;
	self->scale[1] = max;
	layer_changed (self);
}

void
lrg_foliage_layer_get_slope_range (LrgFoliageLayer *self,
                                   gfloat          *min,
                                   gfloat          *max)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (min != NULL)
		*min = self->slope[0];
	if (max != NULL)
		*max = self->slope[1];
}

void
lrg_foliage_layer_set_slope_range (LrgFoliageLayer *self,
                                   gfloat           min,
                                   gfloat           max)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	min = CLAMP (min, 0.0f, 90.0f);
	max = CLAMP (max, min, 90.0f);
	self->slope[0] = min;
	self->slope[1] = max;
	layer_changed (self);
}

void
lrg_foliage_layer_get_height_range (LrgFoliageLayer *self,
                                    gfloat          *min,
                                    gfloat          *max)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (min != NULL)
		*min = self->height[0];
	if (max != NULL)
		*max = self->height[1];
}

void
lrg_foliage_layer_set_height_range (LrgFoliageLayer *self,
                                    gfloat           min,
                                    gfloat           max)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (!isfinite (min) || !isfinite (max) || max < min)
		return;
	self->height[0] = min;
	self->height[1] = max;
	self->has_height = TRUE;
	layer_changed (self);
}

gfloat
lrg_foliage_layer_get_align (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), 0.0f);
	return self->align;
}

void
lrg_foliage_layer_set_align (LrgFoliageLayer *self,
                             gfloat           align)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	self->align = isfinite (align) ? CLAMP (align, 0.0f, 1.0f) : 0.0f;
	layer_changed (self);
}

void
lrg_foliage_layer_get_size (LrgFoliageLayer *self,
                            gfloat          *width,
                            gfloat          *height)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (width != NULL)
		*width = self->size[0];
	if (height != NULL)
		*height = self->size[1];
}

void
lrg_foliage_layer_set_size (LrgFoliageLayer *self,
                            gfloat           width,
                            gfloat           height)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (!(width > 0.0f) || !(height > 0.0f) || !isfinite (width) || !isfinite (height))
		return;
	self->size[0] = width;
	self->size[1] = height;
	layer_release_gpu (self);
	self->grass_dirty = TRUE;
}

void
lrg_foliage_layer_get_fade (LrgFoliageLayer *self,
                            gfloat          *start,
                            gfloat          *end)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (start != NULL)
		*start = self->fade[0];
	if (end != NULL)
		*end = self->fade[1];
}

void
lrg_foliage_layer_set_fade (LrgFoliageLayer *self,
                            gfloat           start,
                            gfloat           end)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	if (!(start >= 0.0f) || !(end > start) || !isfinite (end))
		return;
	self->fade[0] = start;
	self->fade[1] = end;
	self->grass_dirty = TRUE;
}

guint32
lrg_foliage_layer_get_seed (LrgFoliageLayer *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (self), 0);
	return self->seed;
}

void
lrg_foliage_layer_set_seed (LrgFoliageLayer *self,
                            guint32          seed)
{
	g_return_if_fail (LRG_IS_FOLIAGE_LAYER (self));
	self->seed = seed;
	layer_changed (self);
	layer_release_gpu (self);   /* built-in trees are generated from it */
}

/* ==========================================================================
 * LrgFoliage
 * ========================================================================== */

struct _LrgFoliage
{
	GObject      parent_instance;

	guint32      seed;
	guint        patch_cells;
	guint        cells_x;
	guint        cells_z;
	gfloat       cell_size;
	guint        patches_x;
	guint        patches_z;
	GPtrArray   *layers;     /* LrgFoliageLayer */
	gchar       *base_dir;   /* where relative mesh / texture references resolve */
	YamlMapping *extra;

	/* GPU (shared by every layer of this set) */
	gboolean     gpu_tried;
	gboolean     gpu_ok;
	Shader       shader;
	gint         loc_mvp;
	gint         loc_model;
	gint         loc_view;
	gint         loc_fade;
	gint         loc_sun;
	gint         loc_time;
	gint         loc_sway;
	gint         loc_color;
	gint         loc_texture;
	gint         loc_instance;
	gint         loc_lit;
	LrgLightSetSampleLocations light_locs;
	LrgLightSetFogLocations    fog_locs;
};

G_DEFINE_FINAL_TYPE (LrgFoliage, lrg_foliage, G_TYPE_OBJECT)

static void
lrg_foliage_finalize (GObject *object)
{
	LrgFoliage *self = LRG_FOLIAGE (object);
	guint       i;

	lrg_foliage_release_gpu (self);
	for (i = 0; i < self->layers->len; i++)
		((LrgFoliageLayer *) g_ptr_array_index (self->layers, i))->owner = NULL;
	g_ptr_array_unref (self->layers);
	g_free (self->base_dir);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);
	G_OBJECT_CLASS (lrg_foliage_parent_class)->finalize (object);
}

static void
lrg_foliage_class_init (LrgFoliageClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_foliage_finalize;
}

static void
lrg_foliage_init (LrgFoliage *self)
{
	self->patch_cells = 16;
	self->cell_size = 1.0f;
	self->layers = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgFoliage *
lrg_foliage_new (void)
{
	return (LrgFoliage *) g_object_new (LRG_TYPE_FOLIAGE, NULL);
}

/* (Re)builds a layer's patch array for the foliage's grid; every patch
 * dirty and empty. */
static void
layer_alloc_patches (LrgFoliage      *self,
                     LrgFoliageLayer *layer)
{
	guint p;

	for (p = 0; p < layer->n_patches; p++)
		g_array_unref (layer->patches[p].instances);
	g_free (layer->patches);
	layer->n_patches = self->patches_x * self->patches_z;
	layer->patches = g_new0 (Patch, MAX (layer->n_patches, 1u));
	for (p = 0; p < layer->n_patches; p++)
	{
		layer->patches[p].instances = g_array_new (FALSE, FALSE, sizeof (LrgFoliageInstance));
		layer->patches[p].dirty = TRUE;
	}
	layer->gpu_dirty = TRUE;
	layer->grass_dirty = TRUE;
}

/* Every patch of a layer dirty (its rules changed). */
static void
layer_changed (LrgFoliageLayer *self)
{
	guint p;

	for (p = 0; p < self->n_patches; p++)
		self->patches[p].dirty = TRUE;
}

static void
update_patch_grid (LrgFoliage *self)
{
	guint i;

	self->patches_x = (self->cells_x + self->patch_cells - 1) / self->patch_cells;
	self->patches_z = (self->cells_z + self->patch_cells - 1) / self->patch_cells;
	for (i = 0; i < self->layers->len; i++)
		layer_alloc_patches (self, g_ptr_array_index (self->layers, i));
}

guint32
lrg_foliage_get_seed (LrgFoliage *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE (self), 0);
	return self->seed;
}

void
lrg_foliage_set_seed (LrgFoliage *self,
                      guint32     seed)
{
	guint i;

	g_return_if_fail (LRG_IS_FOLIAGE (self));
	self->seed = seed;
	for (i = 0; i < self->layers->len; i++)
		layer_changed (g_ptr_array_index (self->layers, i));
}

guint
lrg_foliage_get_patch_cells (LrgFoliage *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE (self), 16);
	return self->patch_cells;
}

void
lrg_foliage_set_patch_cells (LrgFoliage *self,
                             guint       cells)
{
	g_return_if_fail (LRG_IS_FOLIAGE (self));
	cells = CLAMP (cells, 1u, 256u);
	if (cells == self->patch_cells)
		return;
	self->patch_cells = cells;
	update_patch_grid (self);
}

void
lrg_foliage_set_grid (LrgFoliage *self,
                      guint       cells_x,
                      guint       cells_z,
                      gfloat      cell_size)
{
	guint i, x, z;

	g_return_if_fail (LRG_IS_FOLIAGE (self));
	if (!(cell_size > 0.0f) || !isfinite (cell_size))
		cell_size = 1.0f;

	/* density maps keep their overlap; new cells are full */
	for (i = 0; i < self->layers->len; i++)
	{
		LrgFoliageLayer *layer = g_ptr_array_index (self->layers, i);
		guint8          *map = g_malloc ((gsize) MAX (cells_x * cells_z, 1u));

		memset (map, 255, (gsize) MAX (cells_x * cells_z, 1u));
		if (layer->density_map != NULL)
			for (z = 0; z < MIN (cells_z, self->cells_z); z++)
				for (x = 0; x < MIN (cells_x, self->cells_x); x++)
					map[(gsize) z * cells_x + x] = layer->density_map[(gsize) z * self->cells_x + x];
		g_free (layer->density_map);
		layer->density_map = map;
	}
	self->cells_x = cells_x;
	self->cells_z = cells_z;
	self->cell_size = cell_size;
	update_patch_grid (self);
}

void
_lrg_foliage_set_grid (LrgFoliage *self,
                       guint       cells_x,
                       guint       cells_z,
                       gfloat      cell_size)
{
	lrg_foliage_set_grid (self, cells_x, cells_z, cell_size);
}

void
lrg_foliage_get_grid (LrgFoliage *self,
                      guint      *cells_x,
                      guint      *cells_z,
                      gfloat     *cell_size)
{
	g_return_if_fail (LRG_IS_FOLIAGE (self));
	if (cells_x != NULL)
		*cells_x = self->cells_x;
	if (cells_z != NULL)
		*cells_z = self->cells_z;
	if (cell_size != NULL)
		*cell_size = self->cell_size;
}

guint
lrg_foliage_get_n_layers (LrgFoliage *self)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE (self), 0);
	return self->layers->len;
}

LrgFoliageLayer *
lrg_foliage_get_layer (LrgFoliage *self,
                       guint       index)
{
	g_return_val_if_fail (LRG_IS_FOLIAGE (self), NULL);
	if (index >= self->layers->len)
		return NULL;
	return g_ptr_array_index (self->layers, index);
}

gint
lrg_foliage_add_layer (LrgFoliage      *self,
                       LrgFoliageLayer *layer)
{
	gsize cells;

	g_return_val_if_fail (LRG_IS_FOLIAGE (self), -1);
	g_return_val_if_fail (LRG_IS_FOLIAGE_LAYER (layer), -1);
	if (self->layers->len >= LRG_FOLIAGE_MAX_LAYERS || layer->owner != NULL)
		return -1;
	layer->owner = self;
	cells = (gsize) MAX (self->cells_x * self->cells_z, 1u);
	g_free (layer->density_map);
	layer->density_map = g_malloc (cells);
	memset (layer->density_map, 255, cells);
	layer_alloc_patches (self, layer);
	g_ptr_array_add (self->layers, g_object_ref (layer));
	return (gint) self->layers->len - 1;
}

gboolean
lrg_foliage_remove_layer (LrgFoliage *self,
                          guint       index)
{
	LrgFoliageLayer *layer;

	g_return_val_if_fail (LRG_IS_FOLIAGE (self), FALSE);
	if (index >= self->layers->len)
		return FALSE;
	layer = g_ptr_array_index (self->layers, index);
	layer_release_gpu (layer);
	layer->owner = NULL;
	g_ptr_array_remove_index (self->layers, index);
	return TRUE;
}

guint8 *
_lrg_foliage_density (LrgFoliage *self,
                      guint       layer)
{
	if (layer >= self->layers->len)
		return NULL;
	return ((LrgFoliageLayer *) g_ptr_array_index (self->layers, layer))->density_map;
}

gfloat
lrg_foliage_get_density (LrgFoliage *self,
                         guint       layer,
                         guint       x,
                         guint       z)
{
	guint8 *map;

	g_return_val_if_fail (LRG_IS_FOLIAGE (self), 0.0f);
	map = _lrg_foliage_density (self, layer);
	if (map == NULL || x >= self->cells_x || z >= self->cells_z)
		return 0.0f;
	return (gfloat) map[(gsize) z * self->cells_x + x] / 255.0f;
}

void
lrg_foliage_set_density (LrgFoliage *self,
                         guint       layer,
                         guint       x,
                         guint       z,
                         gfloat      density)
{
	guint8 *map;

	g_return_if_fail (LRG_IS_FOLIAGE (self));
	map = _lrg_foliage_density (self, layer);
	if (map == NULL || x >= self->cells_x || z >= self->cells_z || !isfinite (density))
		return;
	map[(gsize) z * self->cells_x + x] = (guint8) lrintf (CLAMP (density, 0.0f, 1.0f) * 255.0f);
	lrg_foliage_mark_dirty (self, x, z, 1, 1);
}

void
lrg_foliage_fill_density (LrgFoliage *self,
                          guint       layer,
                          gfloat      density)
{
	guint8 *map;

	g_return_if_fail (LRG_IS_FOLIAGE (self));
	map = _lrg_foliage_density (self, layer);
	if (map == NULL || !isfinite (density))
		return;
	memset (map, (gint) lrintf (CLAMP (density, 0.0f, 1.0f) * 255.0f), (gsize) MAX (self->cells_x * self->cells_z, 1u));
	layer_changed (g_ptr_array_index (self->layers, layer));
}

void
lrg_foliage_mark_dirty (LrgFoliage *self,
                        guint       x,
                        guint       z,
                        guint       width,
                        guint       height)
{
	guint px0, pz0, px1, pz1, px, pz, i;

	g_return_if_fail (LRG_IS_FOLIAGE (self));
	if (width == 0 || height == 0 || self->patches_x == 0 || self->patches_z == 0)
		return;
	px0 = MIN (x / self->patch_cells, self->patches_x - 1);
	pz0 = MIN (z / self->patch_cells, self->patches_z - 1);
	px1 = MIN ((x + width - 1) / self->patch_cells, self->patches_x - 1);
	pz1 = MIN ((z + height - 1) / self->patch_cells, self->patches_z - 1);
	for (i = 0; i < self->layers->len; i++)
	{
		LrgFoliageLayer *layer = g_ptr_array_index (self->layers, i);

		for (pz = pz0; pz <= pz1; pz++)
			for (px = px0; px <= px1; px++)
				layer->patches[pz * self->patches_x + px].dirty = TRUE;
	}
}

guint
lrg_foliage_get_dirty_patches (LrgFoliage *self)
{
	guint i, p, n = 0;

	g_return_val_if_fail (LRG_IS_FOLIAGE (self), 0);
	for (i = 0; i < self->layers->len; i++)
	{
		LrgFoliageLayer *layer = g_ptr_array_index (self->layers, i);

		for (p = 0; p < layer->n_patches; p++)
			if (layer->patches[p].dirty)
				n++;
	}
	return n;
}

guint
lrg_foliage_get_instance_count (LrgFoliage *self,
                                guint       layer)
{
	LrgFoliageLayer *l;
	guint            p, n = 0;

	g_return_val_if_fail (LRG_IS_FOLIAGE (self), 0);
	l = lrg_foliage_get_layer (self, layer);
	if (l == NULL)
		return 0;
	for (p = 0; p < l->n_patches; p++)
		n += l->patches[p].instances->len;
	return n;
}

gboolean
lrg_foliage_get_instance (LrgFoliage         *self,
                          guint               layer,
                          guint               index,
                          LrgFoliageInstance *out)
{
	LrgFoliageLayer *l;
	guint            p;

	g_return_val_if_fail (LRG_IS_FOLIAGE (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	l = lrg_foliage_get_layer (self, layer);
	if (l == NULL)
		return FALSE;
	for (p = 0; p < l->n_patches; p++)
	{
		GArray *a = l->patches[p].instances;

		if (index < a->len)
		{
			*out = g_array_index (a, LrgFoliageInstance, index);
			return TRUE;
		}
		index -= a->len;
	}
	return FALSE;
}

/* Grows a patch's bounds by an instance (its foot, plus a generous
 * envelope of its scaled size). */
static void
patch_add_bounds (Patch                    *patch,
                  LrgFoliageLayer          *layer,
                  const LrgFoliageInstance *inst)
{
	gfloat r = MAX (layer->size[0], layer->size[1]) * inst->scale;
	guint  k;

	if (patch->instances->len == 1)
	{
		for (k = 0; k < 3; k++)
		{
			patch->min[k] = inst->position[k];
			patch->max[k] = inst->position[k];
		}
	}
	patch->min[0] = MIN (patch->min[0], inst->position[0] - r);
	patch->min[1] = MIN (patch->min[1], inst->position[1] - r * 0.1f);
	patch->min[2] = MIN (patch->min[2], inst->position[2] - r);
	patch->max[0] = MAX (patch->max[0], inst->position[0] + r);
	patch->max[1] = MAX (patch->max[1], inst->position[1] + r * 1.2f);
	patch->max[2] = MAX (patch->max[2], inst->position[2] + r);
}

gboolean
lrg_foliage_add_instance (LrgFoliage               *self,
                          guint                     layer,
                          const LrgFoliageInstance *instance)
{
	LrgFoliageLayer *l;
	Patch           *patch;
	gint             cx, cz;
	guint            px, pz;

	g_return_val_if_fail (LRG_IS_FOLIAGE (self), FALSE);
	g_return_val_if_fail (instance != NULL, FALSE);
	l = lrg_foliage_get_layer (self, layer);
	if (l == NULL || l->n_patches == 0)
		return FALSE;
	cx = (gint) floorf (instance->position[0] / self->cell_size);
	cz = (gint) floorf (instance->position[2] / self->cell_size);
	px = MIN ((guint) MAX (cx, 0) / self->patch_cells, self->patches_x - 1);
	pz = MIN ((guint) MAX (cz, 0) / self->patch_cells, self->patches_z - 1);
	patch = &l->patches[pz * self->patches_x + px];
	g_array_append_val (patch->instances, *instance);
	patch->dirty = FALSE;
	patch_add_bounds (patch, l, instance);
	l->gpu_dirty = TRUE;
	l->grass_dirty = TRUE;
	return TRUE;
}

void
lrg_foliage_clear_instances (LrgFoliage *self,
                             guint       layer)
{
	LrgFoliageLayer *l;
	guint            p;

	g_return_if_fail (LRG_IS_FOLIAGE (self));
	l = lrg_foliage_get_layer (self, layer);
	if (l == NULL)
		return;
	for (p = 0; p < l->n_patches; p++)
		g_array_set_size (l->patches[p].instances, 0);
	l->gpu_dirty = TRUE;
	l->grass_dirty = TRUE;
}

/* ==========================================================================
 * Scattering
 * ========================================================================== */

/* Places one patch of one layer on @terrain. */
static void
scatter_patch (LrgFoliage      *self,
               LrgFoliageLayer *layer,
               guint            layer_index,
               LrgTerrainAsset *terrain,
               guint            px,
               guint            pz)
{
	Patch   *patch = &layer->patches[pz * self->patches_x + px];
	gfloat   area = self->cell_size * self->cell_size;
	gfloat   cos_min = cosf (layer->slope[1] * (gfloat) G_PI / 180.0f);
	gfloat   cos_max = cosf (layer->slope[0] * (gfloat) G_PI / 180.0f);
	guint32  seed = lrg_hash_mix (self->seed ^ lrg_hash_mix (layer->seed + 0x632be5abu * (layer_index + 1)));
	guint    slots, ci, cj, k, l;
	guint    ci0 = px * self->patch_cells, cj0 = pz * self->patch_cells;
	guint    ci1 = MIN (ci0 + self->patch_cells, self->cells_x), cj1 = MIN (cj0 + self->patch_cells, self->cells_z);
	gdouble  want = (gdouble) layer->density * (gdouble) area;

	g_array_set_size (patch->instances, 0);
	patch->dirty = FALSE;
	slots = (guint) MIN (ceil (want), (gdouble) LRG_FOLIAGE_MAX_PER_CELL);
	if (slots == 0)
		return;

	for (cj = cj0; cj < cj1; cj++)
		for (ci = ci0; ci < ci1; ci++)
		{
			gfloat painted = (gfloat) layer->density_map[(gsize) cj * self->cells_x + ci] / 255.0f;

			if (painted <= 0.0f)
				continue;
			for (k = 0; k < slots; k++)
			{
				guint32            h = lrg_hash_2d (seed ^ lrg_hash_mix (k * 0x9e3779b9u + 1u), ci, cj);
				guint32            state = h;
				gfloat             accept = lrg_hash_unit (h);
				gfloat             x, z, y, w[LRG_TERRAIN_ASSET_MAX_LAYERS], n[3], mat = 0.0f, expected;
				LrgFoliageInstance inst;

				x = ((gfloat) ci + lrg_hash_next_unit (&state)) * self->cell_size;
				z = ((gfloat) cj + lrg_hash_next_unit (&state)) * self->cell_size;
				inst.yaw = lrg_hash_next_unit (&state) * 2.0f * (gfloat) G_PI;
				inst.scale = layer->scale[0] + (layer->scale[1] - layer->scale[0]) * lrg_hash_next_unit (&state);

				/* the surface-layer factor at the spot */
				lrg_terrain_asset_sample_weights (terrain, x, z, w);
				for (l = 0; l < LRG_TERRAIN_ASSET_MAX_LAYERS; l++)
					mat += w[l] * layer->material_density[l];
				expected = (gfloat) (want / (gdouble) slots) * painted * MIN (mat, 1.0f);
				if (!(accept < expected))
					continue;

				/* ground, slope and height rules */
				if (!lrg_terrain_asset_sample_height (terrain, x, z, &y))
					continue;
				if (y < layer->height[0] || y > layer->height[1])
					continue;
				lrg_terrain_asset_sample_normal (terrain, x, z, n);
				if (n[1] < cos_min - 1e-6f || n[1] > cos_max + 1e-6f)
					continue;

				inst.position[0] = x;
				inst.position[1] = y;
				inst.position[2] = z;
				inst.up[0] = n[0] * layer->align;
				inst.up[1] = 1.0f + (n[1] - 1.0f) * layer->align;
				inst.up[2] = n[2] * layer->align;
				g_array_append_val (patch->instances, inst);
				patch_add_bounds (patch, layer, &inst);
			}
		}
}

void
_lrg_foliage_scatter (LrgFoliage      *self,
                      LrgTerrainAsset *terrain)
{
	guint i, px, pz;

	for (i = 0; i < self->layers->len; i++)
	{
		LrgFoliageLayer *layer = g_ptr_array_index (self->layers, i);
		gboolean         changed = FALSE;

		for (pz = 0; pz < self->patches_z; pz++)
			for (px = 0; px < self->patches_x; px++)
				if (layer->patches[pz * self->patches_x + px].dirty)
				{
					scatter_patch (self, layer, i, terrain, px, pz);
					changed = TRUE;
				}
		if (changed)
		{
			layer->gpu_dirty = TRUE;
			layer->grass_dirty = TRUE;
		}
	}
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

/* "a/b" relative to the foliage's directory, or as is. */
static gchar *
resolve_ref (LrgFoliage  *self,
             const gchar *ref)
{
	if (ref == NULL)
		return NULL;
	if (g_str_has_prefix (ref, "guid:") || g_str_has_prefix (ref, "builtin:") || g_path_is_absolute (ref) ||
	    self->base_dir == NULL)
		return g_strdup (ref);
	return g_build_filename (self->base_dir, ref, NULL);
}

static gboolean
read_pair (TerrainReader *rd,
           YamlNode      *node,
           const gchar   *path,
           gfloat        *out)
{
	return _lrg_terrain_rd_numbers (rd, node, path, 2, 2, out, NULL);
}

static gboolean
read_layer (LrgFoliage    *self,
            TerrainReader *rd,
            YamlNode      *node,
            const gchar   *base,
            const gchar   *base_dir)
{
	g_autoptr(LrgFoliageLayer) layer = lrg_foliage_layer_new (NULL, LRG_FOLIAGE_KIND_MESH);
	YamlMapping               *map;
	guint                      i;
	gint                       index;

	if (!_lrg_terrain_rd_mapping (rd, node, base, &map))
		return FALSE;

	/* the kind first: it picks the defaults the other keys override */
	if (yaml_mapping_has_member (map, "kind"))
	{
		gint              kind;
		g_autofree gchar *p = g_strdup_printf ("%s/kind", base);

		if (!_lrg_terrain_rd_enum (rd, yaml_mapping_get_member (map, "kind"), p, LRG_TYPE_FOLIAGE_KIND, &kind))
			return FALSE;
		g_clear_object (&layer);
		layer = lrg_foliage_layer_new (NULL, (LrgFoliageKind) kind);
	}

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar      *key = yaml_mapping_get_key (map, i);
		YamlNode         *member = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", base, key);
		const gchar      *s;
		gfloat            v2[2];

		if (g_strcmp0 (key, "kind") == 0)
			continue;
		if (g_strcmp0 (key, "name") == 0 || g_strcmp0 (key, "mesh") == 0 || g_strcmp0 (key, "lod_mesh") == 0 ||
		    g_strcmp0 (key, "texture") == 0 || g_strcmp0 (key, "density_file") == 0)
		{
			gchar **slot;

			if (!_lrg_terrain_rd_string (rd, member, p, &s))
				return FALSE;
			if (g_strcmp0 (key, "name") == 0)
				slot = &layer->name;
			else if (g_strcmp0 (key, "mesh") == 0)
				slot = &layer->mesh;
			else if (g_strcmp0 (key, "lod_mesh") == 0)
				slot = &layer->lod_mesh;
			else if (g_strcmp0 (key, "texture") == 0)
				slot = &layer->texture;
			else
				slot = &layer->density_file;
			g_free (*slot);
			*slot = g_strdup (s);
		}
		else if (g_strcmp0 (key, "lod_distance") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, 0.001, 1.0e7, &layer->lod_distance))
				return FALSE;
		}
		else if (g_strcmp0 (key, "color") == 0)
		{
			gfloat c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			guint  n, k;

			if (!_lrg_terrain_rd_numbers (rd, member, p, 3, 4, c, &n))
				return FALSE;
			for (k = 0; k < 4; k++)
				if (c[k] < 0.0f || c[k] > 1.0f)
					return _lrg_terrain_rd_fail (rd, p, "colour components must lie in 0 .. 1");
			memcpy (layer->color, c, sizeof c);
		}
		else if (g_strcmp0 (key, "density") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, 0.0, 10000.0, &layer->density))
				return FALSE;
		}
		else if (g_strcmp0 (key, "material_density") == 0)
		{
			gfloat f[LRG_TERRAIN_ASSET_MAX_LAYERS];
			guint  n, k;

			if (!_lrg_terrain_rd_numbers (rd, member, p, 1, LRG_TERRAIN_ASSET_MAX_LAYERS, f, &n))
				return FALSE;
			for (k = 0; k < n; k++)
			{
				if (f[k] < 0.0f || f[k] > 1.0f)
					return _lrg_terrain_rd_fail (rd, p, "factors must lie in 0 .. 1");
				layer->material_density[k] = f[k];
			}
		}
		else if (g_strcmp0 (key, "scale") == 0)
		{
			if (!read_pair (rd, member, p, v2))
				return FALSE;
			if (!(v2[0] > 0.0f) || v2[1] < v2[0])
				return _lrg_terrain_rd_fail (rd, p, "expected 0 < min <= max");
			memcpy (layer->scale, v2, sizeof v2);
		}
		else if (g_strcmp0 (key, "slope") == 0)
		{
			if (!read_pair (rd, member, p, v2))
				return FALSE;
			if (v2[0] < 0.0f || v2[1] > 90.0f || v2[1] < v2[0])
				return _lrg_terrain_rd_fail (rd, p, "expected 0 <= min <= max <= 90 degrees");
			memcpy (layer->slope, v2, sizeof v2);
		}
		else if (g_strcmp0 (key, "height") == 0)
		{
			if (!read_pair (rd, member, p, v2))
				return FALSE;
			if (v2[1] < v2[0])
				return _lrg_terrain_rd_fail (rd, p, "expected min <= max");
			memcpy (layer->height, v2, sizeof v2);
			layer->has_height = TRUE;
		}
		else if (g_strcmp0 (key, "align") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, 0.0, 1.0, &layer->align))
				return FALSE;
		}
		else if (g_strcmp0 (key, "size") == 0)
		{
			if (!read_pair (rd, member, p, v2))
				return FALSE;
			if (!(v2[0] > 0.0f) || !(v2[1] > 0.0f))
				return _lrg_terrain_rd_fail (rd, p, "expected two sizes above 0");
			memcpy (layer->size, v2, sizeof v2);
		}
		else if (g_strcmp0 (key, "fade") == 0)
		{
			if (!read_pair (rd, member, p, v2))
				return FALSE;
			if (v2[0] < 0.0f || !(v2[1] > v2[0]))
				return _lrg_terrain_rd_fail (rd, p, "expected 0 <= start < end");
			memcpy (layer->fade, v2, sizeof v2);
		}
		else if (g_strcmp0 (key, "seed") == 0)
		{
			guint seed;

			if (!_lrg_terrain_rd_uint (rd, member, p, 0, G_MAXUINT32, &seed))
				return FALSE;
			layer->seed = seed;
		}
		else
			_lrg_terrain_keep_extra (&layer->extra, key, member);
	}

	index = lrg_foliage_add_layer (self, layer);
	if (index < 0)
		return _lrg_terrain_rd_fail (rd, base, "too many foliage layers (at most %u)",
		                             (guint) LRG_FOLIAGE_MAX_LAYERS);

	/* the density sidecar: one gray byte per terrain cell */
	if (layer->density_file != NULL && base_dir != NULL)
	{
		TerrainImage      img;
		g_autoptr(GError) local = NULL;
		g_autofree gchar *file = g_path_is_absolute (layer->density_file)
		                         ? g_strdup (layer->density_file)
		                         : g_build_filename (base_dir, layer->density_file, NULL);
		g_autofree gchar *p = g_strdup_printf ("%s/density_file", base);
		gsize             c, cells = (gsize) self->cells_x * self->cells_z;

		if (!_lrg_terrain_png_load (file, &img, &local))
		{
			rd->code = local->code;
			return _lrg_terrain_rd_fail (rd, p, "%s: %s", layer->density_file, local->message);
		}
		if (img.width != self->cells_x || img.height != self->cells_z)
		{
			guint got_w = img.width, got_h = img.height;

			_lrg_terrain_image_clear (&img);
			rd->code = LRG_TERRAIN_ASSET_ERROR_INVALID;
			return _lrg_terrain_rd_fail (rd, p, "%s: expected a %ux%u image, got %ux%u", layer->density_file,
			                             self->cells_x, self->cells_z, got_w, got_h);
		}
		for (c = 0; c < cells; c++)
			layer->density_map[c] = (guint8) ((_lrg_terrain_image_get16 (&img, (guint) (c % self->cells_x),
			                                                             (guint) (c / self->cells_x), 0) +
			                                   128u) / 257u);
		_lrg_terrain_image_clear (&img);
	}
	return TRUE;
}

gboolean
_lrg_foliage_read (LrgFoliage    *self,
                   TerrainReader *rd,
                   YamlNode      *node,
                   const gchar   *path,
                   const gchar   *base_dir)
{
	YamlMapping *map;
	guint        i, j;

	if (!_lrg_terrain_rd_mapping (rd, node, path, &map))
		return FALSE;
	g_free (self->base_dir);
	self->base_dir = g_strdup (base_dir);
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar      *key = yaml_mapping_get_key (map, i);
		YamlNode         *member = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "seed") == 0)
		{
			guint seed;

			if (!_lrg_terrain_rd_uint (rd, member, p, 0, G_MAXUINT32, &seed))
				return FALSE;
			self->seed = seed;
		}
		else if (g_strcmp0 (key, "patch_cells") == 0)
		{
			guint cells;

			if (!_lrg_terrain_rd_uint (rd, member, p, 1, 256, &cells))
				return FALSE;
			lrg_foliage_set_patch_cells (self, cells);
		}
		else if (g_strcmp0 (key, "layers") == 0)
		{
			YamlSequence *seq;

			if (!_lrg_terrain_rd_sequence (rd, member, p, &seq))
				return FALSE;
			for (j = 0; j < yaml_sequence_get_length (seq); j++)
			{
				g_autofree gchar *lp = g_strdup_printf ("%s[%u]", p, j);

				if (!read_layer (self, rd, yaml_sequence_get_element (seq, j), lp, base_dir))
					return FALSE;
			}
		}
		else
			_lrg_terrain_keep_extra (&self->extra, key, member);
	}
	return TRUE;
}

gboolean
_lrg_foliage_is_empty (LrgFoliage *self)
{
	return self->layers->len == 0 && self->extra == NULL;
}

/* Whether a density map is full everywhere (nothing to write). */
static gboolean
density_full (LrgFoliage      *self,
              LrgFoliageLayer *layer)
{
	gsize c, cells = (gsize) self->cells_x * self->cells_z;

	for (c = 0; c < cells; c++)
		if (layer->density_map[c] != 255)
			return FALSE;
	return TRUE;
}

static void
set_pair (YamlMapping  *map,
          const gchar  *key,
          const gfloat *v)
{
	_lrg_terrain_set_member (map, key, _lrg_terrain_float_list (v, 2));
}

YamlNode *
_lrg_foliage_write (LrgFoliage  *self,
                    const gchar *dir,
                    const gchar *stem,
                    gboolean     renew,
                    GError     **error)
{
	g_autoptr(YamlNode) root = yaml_node_new_mapping (NULL);
	YamlMapping        *map = yaml_node_get_mapping (root);
	YamlNode           *list;
	YamlSequence       *seq;
	guint               i, l, n;

	_lrg_terrain_set_member (map, "seed", lrg_data_yaml_int (self->seed));
	if (self->patch_cells != 16)
		_lrg_terrain_set_member (map, "patch_cells", lrg_data_yaml_int (self->patch_cells));

	list = yaml_node_new_sequence (NULL);
	seq = yaml_node_get_sequence (list);
	for (i = 0; i < self->layers->len; i++)
	{
		LrgFoliageLayer *layer = g_ptr_array_index (self->layers, i);
		YamlNode        *ln = yaml_node_new_mapping (NULL);
		YamlMapping     *lm = yaml_node_get_mapping (ln);

		if (layer->name != NULL)
			_lrg_terrain_set_member (lm, "name", lrg_data_yaml_string (layer->name));
		_lrg_terrain_set_member (lm, "kind",
		                         lrg_data_yaml_string (_lrg_terrain_enum_nick (LRG_TYPE_FOLIAGE_KIND,
		                                                                       (gint) layer->kind)));
		if (layer->mesh != NULL)
			_lrg_terrain_set_member (lm, "mesh", lrg_data_yaml_string (layer->mesh));
		if (layer->lod_mesh != NULL)
		{
			_lrg_terrain_set_member (lm, "lod_mesh", lrg_data_yaml_string (layer->lod_mesh));
			_lrg_terrain_set_member (lm, "lod_distance", lrg_data_yaml_float (layer->lod_distance));
		}
		if (layer->texture != NULL)
			_lrg_terrain_set_member (lm, "texture", lrg_data_yaml_string (layer->texture));
		_lrg_terrain_set_member (lm, "color", _lrg_terrain_float_list (layer->color, 4));
		_lrg_terrain_set_member (lm, "density", lrg_data_yaml_float (layer->density));

		/* factors up to the last one that is not 1 */
		n = 0;
		for (l = 0; l < LRG_TERRAIN_ASSET_MAX_LAYERS; l++)
			if (layer->material_density[l] != 1.0f)
				n = l + 1;
		if (n > 0)
			_lrg_terrain_set_member (lm, "material_density", _lrg_terrain_float_list (layer->material_density, n));
		set_pair (lm, "scale", layer->scale);
		set_pair (lm, "slope", layer->slope);
		if (layer->has_height)
			set_pair (lm, "height", layer->height);
		_lrg_terrain_set_member (lm, "align", lrg_data_yaml_float (layer->align));
		set_pair (lm, "size", layer->size);
		set_pair (lm, "fade", layer->fade);
		_lrg_terrain_set_member (lm, "seed", lrg_data_yaml_int (layer->seed));

		/* the density sidecar, only when painted */
		if (!density_full (self, layer))
		{
			if (renew || layer->density_file == NULL)
			{
				g_free (layer->density_file);
				layer->density_file = g_strdup_printf ("%s.foliage%u.png", stem, i);
			}
			_lrg_terrain_set_member (lm, "density_file", lrg_data_yaml_string (layer->density_file));
			if (dir != NULL)
			{
				g_autofree gchar *file = g_path_is_absolute (layer->density_file)
				                         ? g_strdup (layer->density_file)
				                         : g_build_filename (dir, layer->density_file, NULL);

				if (!_lrg_terrain_png_save (file, self->cells_x, self->cells_z, 1, 8, layer->density_map, error))
				{
					yaml_node_unref (ln);
					yaml_node_unref (list);
					return NULL;
				}
			}
		}
		_lrg_terrain_write_extra (lm, layer->extra);
		yaml_sequence_add_element (seq, ln);
		yaml_node_unref (ln);
	}
	_lrg_terrain_set_member (map, "layers", list);
	_lrg_terrain_write_extra (map, self->extra);
	if (dir != NULL)
	{
		g_free (self->base_dir);
		self->base_dir = g_strdup (dir);
	}
	return g_steal_pointer (&root);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

static void
layer_mesh_release (LayerMesh *lm)
{
	if (lm->owned && lm->mesh.vertexCount > 0)
		UnloadMesh (lm->mesh);
	memset (lm, 0, sizeof *lm);
}

static void
layer_release_gpu (LrgFoliageLayer *self)
{
	guint lod, m;

	if (self->vbo != 0 && IsWindowReady ())
		rlUnloadVertexBuffer (self->vbo);
	self->vbo = 0;
	self->vbo_capacity = 0;
	for (lod = 0; lod < 2; lod++)
	{
		for (m = 0; m < self->n_meshes[lod]; m++)
			if (IsWindowReady ())
				layer_mesh_release (&self->meshes[lod]);
		self->n_meshes[lod] = 0;
		memset (&self->meshes[lod], 0, sizeof self->meshes[lod]);
	}
	self->meshes_tried = FALSE;
	self->gpu_dirty = TRUE;
	if (self->grass != NULL && IsWindowReady ())
		lrg_grass_field_release_gpu (self->grass);
}

void
lrg_foliage_release_gpu (LrgFoliage *self)
{
	guint i;

	g_return_if_fail (LRG_IS_FOLIAGE (self));
	for (i = 0; i < self->layers->len; i++)
		layer_release_gpu (g_ptr_array_index (self->layers, i));
	if (self->gpu_ok && IsWindowReady ())
		UnloadShader (self->shader);
	memset (&self->shader, 0, sizeof self->shader);
	self->gpu_ok = FALSE;
	self->gpu_tried = FALSE;
}

static gboolean
ensure_shader (LrgFoliage *self)
{
	if (self->gpu_tried)
		return self->gpu_ok;
	if (!IsWindowReady ())
		return FALSE;
	self->gpu_tried = TRUE;
	self->shader = LoadShaderFromMemory (foliage_vs, foliage_fs);
	if (self->shader.id == 0 || self->shader.id == rlGetShaderIdDefault ())
	{
		g_warning ("LrgFoliage: the foliage shader did not compile (GLSL 330 needed)");
		return FALSE;
	}
	self->loc_mvp = GetShaderLocation (self->shader, "mvp");
	self->loc_model = GetShaderLocation (self->shader, "model");
	self->loc_view = GetShaderLocation (self->shader, "viewPos");
	self->loc_fade = GetShaderLocation (self->shader, "fade");
	self->loc_sun = GetShaderLocation (self->shader, "sunDir");
	self->loc_time = GetShaderLocation (self->shader, "time");
	self->loc_sway = GetShaderLocation (self->shader, "sway");
	self->loc_color = GetShaderLocation (self->shader, "colDiffuse");
	self->loc_texture = GetShaderLocation (self->shader, "texture0");
	self->loc_instance = GetShaderLocationAttrib (self->shader, "instanceTransform");
	self->loc_lit = GetShaderLocation (self->shader, "lit");
	lrg_light_set_sample_locations (self->shader, &self->light_locs);
	lrg_light_set_fog_locations (self->shader, &self->fog_locs);
	self->gpu_ok = self->loc_instance >= 0;
	return self->gpu_ok;
}

/* A built-in tree from the layer's seed and height, as an uploaded mesh. */
static gboolean
build_tree_mesh (LrgFoliageLayer *layer,
                 const gchar     *shape_nick,
                 LayerMesh       *out)
{
	g_autoptr(GEnumClass)  klass = g_type_class_ref (LRG_TYPE_TREE_SHAPE);
	g_autoptr(LrgTreeMesh) tree = NULL;
	GEnumValue            *ev = g_enum_get_value_by_nick (klass, shape_nick);
	const gfloat          *pos, *nrm;
	const guint8          *col;
	const guint32         *idx;
	guint                  nv, ni, i;
	gfloat                 tint[4];

	if (ev == NULL)
		return FALSE;
	tree = lrg_tree_mesh_new ((LrgTreeShape) ev->value, layer->seed, layer->size[1]);
	pos = lrg_tree_mesh_get_positions (tree, &nv);
	nrm = lrg_tree_mesh_get_normals (tree, NULL);
	col = lrg_tree_mesh_get_colors (tree, NULL);
	idx = lrg_tree_mesh_get_indices (tree, &ni);
	nv /= 3;
	if (nv == 0 || nv > 65535)
		return FALSE;
	memset (out, 0, sizeof *out);
	out->mesh.vertexCount = (gint) nv;
	out->mesh.triangleCount = (gint) (ni / 3);
	out->mesh.vertices = MemAlloc (sizeof (gfloat) * 3 * nv);
	out->mesh.normals = MemAlloc (sizeof (gfloat) * 3 * nv);
	out->mesh.texcoords = MemAlloc (sizeof (gfloat) * 2 * nv);
	out->mesh.colors = MemAlloc (4 * nv);
	out->mesh.indices = MemAlloc (sizeof (gushort) * ni);
	memcpy (out->mesh.vertices, pos, sizeof (gfloat) * 3 * nv);
	memcpy (out->mesh.normals, nrm, sizeof (gfloat) * 3 * nv);
	memcpy (out->mesh.colors, col, 4 * (gsize) nv);
	for (i = 0; i < ni; i++)
		out->mesh.indices[i] = (gushort) idx[i];
	UploadMesh (&out->mesh, FALSE);
	out->owned = TRUE;
	lrg_foliage_layer_get_color (layer, tint);
	out->color = (Color) { (guint8) (tint[0] * 255.0f), (guint8) (tint[1] * 255.0f), (guint8) (tint[2] * 255.0f),
	                       255 };
	return TRUE;
}

/* Two crossed width x height cards standing on the origin. */
static void
build_card_mesh (LrgFoliageLayer *layer,
                 LayerMesh       *out)
{
	gfloat hw = layer->size[0] * 0.5f, h = layer->size[1];
	guint  q, v;

	memset (out, 0, sizeof *out);
	out->mesh.vertexCount = 8;
	out->mesh.triangleCount = 4;
	out->mesh.vertices = MemAlloc (sizeof (gfloat) * 3 * 8);
	out->mesh.normals = MemAlloc (sizeof (gfloat) * 3 * 8);
	out->mesh.texcoords = MemAlloc (sizeof (gfloat) * 2 * 8);
	out->mesh.colors = MemAlloc (4 * 8);
	out->mesh.indices = MemAlloc (sizeof (gushort) * 12);
	for (q = 0; q < 2; q++)
		for (v = 0; v < 4; v++)
		{
			guint  i = q * 4 + v;
			gfloat s = (v == 0 || v == 3) ? -hw : hw;
			gfloat y = (v < 2) ? 0.0f : h;

			out->mesh.vertices[i * 3 + 0] = q == 0 ? s : 0.0f;
			out->mesh.vertices[i * 3 + 1] = y;
			out->mesh.vertices[i * 3 + 2] = q == 0 ? 0.0f : s;
			out->mesh.normals[i * 3 + 0] = 0.0f;
			out->mesh.normals[i * 3 + 1] = 1.0f;
			out->mesh.normals[i * 3 + 2] = 0.0f;
			out->mesh.texcoords[i * 2 + 0] = (v == 0 || v == 3) ? 0.0f : 1.0f;
			out->mesh.texcoords[i * 2 + 1] = (v < 2) ? 1.0f : 0.0f;
			out->mesh.colors[i * 4 + 0] = 255;
			out->mesh.colors[i * 4 + 1] = 255;
			out->mesh.colors[i * 4 + 2] = 255;
			out->mesh.colors[i * 4 + 3] = 255;
		}
	for (q = 0; q < 2; q++)
	{
		gushort b = (gushort) (q * 4);

		out->mesh.indices[q * 6 + 0] = b;
		out->mesh.indices[q * 6 + 1] = (gushort) (b + 1);
		out->mesh.indices[q * 6 + 2] = (gushort) (b + 2);
		out->mesh.indices[q * 6 + 3] = b;
		out->mesh.indices[q * 6 + 4] = (gushort) (b + 2);
		out->mesh.indices[q * 6 + 5] = (gushort) (b + 3);
	}
	UploadMesh (&out->mesh, FALSE);
	out->owned = TRUE;
	out->color = WHITE;
}

/* The mesh of one LOD: built-in tree, card, or the model's first mesh
 * (with its diffuse texture and colour) through the renderer's cache. */
static void
ensure_lod_mesh (LrgFoliage         *self,
                 LrgFoliageLayer    *layer,
                 LrgWorld3DRenderer *renderer,
                 guint               lod)
{
	const gchar      *ref = lod == 0 ? layer->mesh : layer->lod_mesh;
	g_autofree gchar *path = NULL;
	GrlModel         *model;
	Model            *m;

	layer->n_meshes[lod] = 0;
	if (layer->kind == LRG_FOLIAGE_KIND_BILLBOARD && lod == 0)
	{
		build_card_mesh (layer, &layer->meshes[0]);
		if (layer->texture != NULL && renderer != NULL)
		{
			g_autofree gchar *tex_path = resolve_ref (self, layer->texture);
			GrlTexture       *tex = lrg_world3d_renderer_get_texture (renderer, tex_path);

			if (tex != NULL)
				layer->meshes[0].texture = ((Texture2D *) grl_texture_get_handle (tex))->id;
		}
		layer->n_meshes[0] = 1;
		return;
	}
	if (ref == NULL)
		return;
	if (g_str_has_prefix (ref, "builtin:"))
	{
		if (build_tree_mesh (layer, ref + strlen ("builtin:"), &layer->meshes[lod]))
			layer->n_meshes[lod] = 1;
		return;
	}
	if (renderer == NULL)
		return;
	path = resolve_ref (self, ref);
	model = lrg_world3d_renderer_get_model (renderer, path);
	if (model == NULL)
		return;
	m = (Model *) grl_model_get_handle (model);
	if (m == NULL || m->meshCount <= 0)
		return;

	/* the model's first mesh, borrowed (the renderer's cache owns it) */
	memset (&layer->meshes[lod], 0, sizeof layer->meshes[lod]);
	layer->meshes[lod].mesh = m->meshes[0];
	layer->meshes[lod].owned = FALSE;
	layer->meshes[lod].color = WHITE;
	if (m->materials != NULL && m->meshMaterial != NULL && m->meshMaterial[0] < m->materialCount)
	{
		MaterialMap *map = &m->materials[m->meshMaterial[0]].maps[MATERIAL_MAP_DIFFUSE];

		layer->meshes[lod].texture = map->texture.id;
		layer->meshes[lod].color = map->color;
	}
	layer->n_meshes[lod] = 1;
}

/* Rebuilds the layer's instance buffer: patch after patch, each patch's
 * first instance recorded for the draw runs. */
static void
upload_instances (LrgFoliageLayer *layer)
{
	g_autofree gfloat *data = NULL;
	guint              total = 0, p, i, at = 0;

	g_array_set_size (layer->run_first, layer->n_patches);
	for (p = 0; p < layer->n_patches; p++)
		total += layer->patches[p].instances->len;
	layer->gpu_dirty = FALSE;
	if (total == 0)
		return;
	data = g_new (gfloat, (gsize) total * 16);
	for (p = 0; p < layer->n_patches; p++)
	{
		GArray *a = layer->patches[p].instances;

		g_array_index (layer->run_first, guint, p) = at;
		for (i = 0; i < a->len; i++)
			lrg_foliage_instance_get_matrix (&g_array_index (a, LrgFoliageInstance, i), data + (gsize) at++ * 16);
	}
	if (layer->vbo != 0 && layer->vbo_capacity < total)
	{
		rlUnloadVertexBuffer (layer->vbo);
		layer->vbo = 0;
	}
	if (layer->vbo == 0)
	{
		layer->vbo = rlLoadVertexBuffer (data, (gint) ((gsize) total * 16 * sizeof (gfloat)), FALSE);
		layer->vbo_capacity = total;
	}
	else
		rlUpdateVertexBuffer (layer->vbo, data, (gint) ((gsize) total * 16 * sizeof (gfloat)), 0);
}

/* One instanced draw of instances first .. first + count - 1 of @lm. */
static void
draw_run (LrgFoliage *self,
          LayerMesh  *lm,
          guint       vbo,
          guint       first,
          guint       count)
{
	guint loc = (guint) self->loc_instance;
	guint k;

	if (lm->mesh.vaoId == 0 || count == 0)
		return;
	rlEnableVertexArray (lm->mesh.vaoId);

	/* attributes the mesh lacks take constant defaults */
	if (lm->mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR] == 0)
	{
		gfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		rlSetVertexAttributeDefault (RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, white, SHADER_ATTRIB_VEC4, 4);
	}
	if (lm->mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL] == 0)
	{
		gfloat up[3] = { 0.0f, 1.0f, 0.0f };

		rlSetVertexAttributeDefault (RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, up, SHADER_ATTRIB_VEC3, 3);
	}
	rlEnableVertexBuffer (vbo);
	for (k = 0; k < 4; k++)
	{
		rlEnableVertexAttribute (loc + k);
		rlSetVertexAttribute (loc + k, 4, RL_FLOAT, FALSE, (gint) (16 * sizeof (gfloat)),
		                      (gint) (((gsize) first * 16 + k * 4) * sizeof (gfloat)));
		rlSetVertexAttributeDivisor (loc + k, 1);
	}
	if (lm->mesh.indices != NULL || lm->mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_INDICES] != 0)
		rlDrawVertexArrayElementsInstanced (0, lm->mesh.triangleCount * 3, NULL, (gint) count);
	else
		rlDrawVertexArrayInstanced (0, lm->mesh.vertexCount, (gint) count);
	for (k = 0; k < 4; k++)
	{
		rlSetVertexAttributeDivisor (loc + k, 0);
		rlDisableVertexAttribute (loc + k);
	}
	rlDisableVertexBuffer ();
	rlDisableVertexArray ();
}

static Matrix
to_matrix (const gfloat *f)
{
	Matrix m;

	m.m0 = f[0];   m.m1 = f[1];   m.m2 = f[2];   m.m3 = f[3];
	m.m4 = f[4];   m.m5 = f[5];   m.m6 = f[6];   m.m7 = f[7];
	m.m8 = f[8];   m.m9 = f[9];   m.m10 = f[10]; m.m11 = f[11];
	m.m12 = f[12]; m.m13 = f[13]; m.m14 = f[14]; m.m15 = f[15];
	return m;
}

/* Transforms a point by a column-major matrix. */
static void
xform_point (const gfloat *m,
             const gfloat *p,
             gfloat       *out)
{
	guint i;

	for (i = 0; i < 3; i++)
		out[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i];
}

/* What a layer item's draw callback needs (the list owns it). */
typedef struct
{
	LrgFoliage *foliage;   /* referenced */
	guint       layer;
	gfloat      world[16];
	gfloat      eye[3];
	gfloat      time;
} LayerDraw;

static void
layer_draw_free (gpointer data)
{
	LayerDraw *ld = (LayerDraw *) data;

	g_object_unref (ld->foliage);
	g_free (ld);
}

/* Grass layers: the blades in world space, drawn by an LrgGrassField. */
static void
draw_grass (LrgFoliage         *self,
            LrgFoliageLayer    *layer,
            LayerDraw          *ld,
            LrgWorld3DRenderer *renderer)
{
	g_autoptr(LrgLightSet) previous = NULL;
	LrgLightSet           *lights = renderer != NULL ? lrg_world3d_renderer_get_light_set (renderer) : NULL;

	if (layer->grass == NULL)
	{
		layer->grass = lrg_grass_field_new (layer->seed);
		layer->grass_dirty = TRUE;
	}
	if (layer->grass_dirty || memcmp (layer->grass_world, ld->world, sizeof ld->world) != 0)
	{
		g_autoptr(GArray) blades = g_array_new (FALSE, FALSE, sizeof (LrgGrassBlade));
		GrlColor          base, tip;
		guint             p, i;

		for (p = 0; p < layer->n_patches; p++)
		{
			GArray *a = layer->patches[p].instances;

			for (i = 0; i < a->len; i++)
			{
				const LrgFoliageInstance *inst = &g_array_index (a, LrgFoliageInstance, i);
				LrgGrassBlade             b;
				guint32                   h = lrg_hash_mix ((guint32) (p * 7919u + i * 104729u) ^ layer->seed);

				xform_point (ld->world, inst->position, b.position);
				b.yaw = inst->yaw;
				b.height = layer->size[1] * inst->scale;
				b.width = layer->size[0] * inst->scale;
				b.shade = 0.85f + 0.3f * lrg_hash_unit (h);
				g_array_append_val (blades, b);
			}
		}
		lrg_grass_field_set_blades (layer->grass, (const LrgGrassBlade *) blades->data, blades->len);
		base.r = (guint8) (layer->color[0] * 0.45f * 255.0f);
		base.g = (guint8) (layer->color[1] * 0.45f * 255.0f);
		base.b = (guint8) (layer->color[2] * 0.45f * 255.0f);
		base.a = 255;
		tip.r = (guint8) (layer->color[0] * 255.0f);
		tip.g = (guint8) (layer->color[1] * 255.0f);
		tip.b = (guint8) (layer->color[2] * 255.0f);
		tip.a = 255;
		lrg_grass_field_set_colors (layer->grass, &base, &tip);
		lrg_grass_field_set_fade (layer->grass, layer->fade[0], layer->fade[1]);
		memcpy (layer->grass_world, ld->world, sizeof ld->world);
		layer->grass_dirty = FALSE;
	}

	/* lit by the renderer's light set while drawing */
	if (lrg_light_set_get_bound () != NULL)
		previous = g_object_ref (lrg_light_set_get_bound ());
	if (lights != NULL)
		lrg_light_set_bind (lights);
	rlEnableDepthMask ();
	lrg_grass_field_draw (layer->grass, ld->eye, ld->time);
	rlDrawRenderBatchActive ();
	rlDisableDepthMask ();
	if (previous != NULL)
		lrg_light_set_bind (previous);
	else
		lrg_light_set_unbind ();
}

static void
draw_layer (const LrgRenderItem *item,
            LrgRenderCategory    category,
            gpointer             pass_data)
{
	LayerDraw          *ld = (LayerDraw *) item->user_data;
	LrgFoliage         *self = ld->foliage;
	LrgFoliageLayer    *layer = lrg_foliage_get_layer (self, ld->layer);
	LrgWorld3DRenderer *renderer = LRG_IS_WORLD3D_RENDERER (pass_data) ? LRG_WORLD3D_RENDERER (pass_data) : NULL;
	LrgLightSet        *lights;
	LrgDrawFrustum      frustum;
	gboolean            use_frustum;
	Matrix              world, mvp;
	gfloat              fade[2], sun[3] = { 0.5f, -0.75f, -0.45f }, color[4], sway, scale;
	guint               p, lod, run_first = 0, run_count = 0, run_lod = 0;
	gint                unit = 0, lit;

	if (layer == NULL || category == LRG_RENDER_CATEGORY_SHADOW_CASTER)
		return;
	if (layer->kind == LRG_FOLIAGE_KIND_GRASS)
	{
		draw_grass (self, layer, ld, renderer);
		return;
	}
	if (!ensure_shader (self))
		return;
	if (!layer->meshes_tried)
	{
		ensure_lod_mesh (self, layer, renderer, 0);
		ensure_lod_mesh (self, layer, renderer, 1);
		layer->meshes_tried = TRUE;
	}
	if (layer->n_meshes[0] == 0)
		return;
	if (layer->gpu_dirty || (layer->vbo == 0 && layer->run_first->len == 0))
		upload_instances (layer);
	if (layer->vbo == 0)
		return;

	rlDrawRenderBatchActive ();
	world = to_matrix (ld->world);
	mvp = lrg_draw_cull_mul (rlGetMatrixProjection (), lrg_draw_cull_mul (rlGetMatrixModelview (), world));
	use_frustum = !rlIsStereoRenderEnabled () && lrg_draw_cull_frustum_from_matrix (&frustum, mvp);

	/* the uniforms */
	rlEnableShader (self->shader.id);
	rlSetUniformMatrix (self->loc_mvp, mvp);
	rlSetUniformMatrix (self->loc_model, world);
	SetShaderValue (self->shader, self->loc_view, ld->eye, SHADER_UNIFORM_VEC3);
	fade[0] = layer->fade[0];
	fade[1] = layer->fade[1];
	SetShaderValue (self->shader, self->loc_fade, fade, SHADER_UNIFORM_VEC2);
	if (renderer != NULL)
		lrg_world3d_renderer_get_sun_direction (renderer, sun);
	SetShaderValue (self->shader, self->loc_sun, sun, SHADER_UNIFORM_VEC3);
	SetShaderValue (self->shader, self->loc_time, &ld->time, SHADER_UNIFORM_FLOAT);
	sway = layer->kind == LRG_FOLIAGE_KIND_BILLBOARD ? 0.04f : 0.004f;
	SetShaderValue (self->shader, self->loc_sway, &sway, SHADER_UNIFORM_FLOAT);
	lights = renderer != NULL ? lrg_world3d_renderer_get_light_set (renderer) : NULL;
	lit = lrg_light_set_upload_sample (lights, self->shader, &self->light_locs) ? 1 : 0;
	SetShaderValue (self->shader, self->loc_lit, &lit, SHADER_UNIFORM_INT);
	lrg_light_set_upload_fog (lights, self->shader, &self->fog_locs);
	SetShaderValue (self->shader, self->loc_texture, &unit, SHADER_UNIFORM_INT);

	rlEnableDepthMask ();
	rlEnableDepthTest ();
	rlDisableBackfaceCulling ();

	/* world scale (for the patch reach) */
	scale = MAX (sqrtf (ld->world[0] * ld->world[0] + ld->world[1] * ld->world[1] + ld->world[2] * ld->world[2]),
	             sqrtf (ld->world[8] * ld->world[8] + ld->world[9] * ld->world[9] + ld->world[10] * ld->world[10]));

	/* runs of consecutive visible patches sharing an LOD */
	for (p = 0; p <= layer->n_patches; p++)
	{
		gboolean visible = FALSE;

		lod = 0;
		if (p < layer->n_patches && layer->patches[p].instances->len > 0)
		{
			Patch  *patch = &layer->patches[p];
			gfloat  c[3], wc[3], r, d;

			c[0] = 0.5f * (patch->min[0] + patch->max[0]);
			c[1] = 0.5f * (patch->min[1] + patch->max[1]);
			c[2] = 0.5f * (patch->min[2] + patch->max[2]);
			r = 0.5f * sqrtf ((patch->max[0] - patch->min[0]) * (patch->max[0] - patch->min[0]) +
			                  (patch->max[1] - patch->min[1]) * (patch->max[1] - patch->min[1]) +
			                  (patch->max[2] - patch->min[2]) * (patch->max[2] - patch->min[2])) * scale;
			xform_point (ld->world, c, wc);
			d = sqrtf ((wc[0] - ld->eye[0]) * (wc[0] - ld->eye[0]) + (wc[1] - ld->eye[1]) * (wc[1] - ld->eye[1]) +
			           (wc[2] - ld->eye[2]) * (wc[2] - ld->eye[2]));
			visible = (d - r) < layer->fade[1] &&
			          (!use_frustum || lrg_draw_cull_frustum_box_visible (&frustum, patch->min, patch->max));
			if (visible && layer->n_meshes[1] > 0 && d - r > layer->lod_distance)
				lod = 1;
		}
		if (visible && run_count > 0 && lod == run_lod &&
		    run_first + run_count == g_array_index (layer->run_first, guint, p))
		{
			run_count += layer->patches[p].instances->len;
			continue;
		}
		if (run_count > 0)
		{
			LayerMesh *lm = &layer->meshes[run_lod];

			lrg_foliage_layer_get_color (layer, color);
			color[0] *= (gfloat) lm->color.r / 255.0f;
			color[1] *= (gfloat) lm->color.g / 255.0f;
			color[2] *= (gfloat) lm->color.b / 255.0f;
			SetShaderValue (self->shader, self->loc_color, color, SHADER_UNIFORM_VEC4);
			rlActiveTextureSlot (0);
			rlEnableTexture (lm->texture != 0 ? lm->texture : rlGetTextureIdDefault ());
			draw_run (self, lm, layer->vbo, run_first, run_count);
			run_count = 0;
		}
		if (visible)
		{
			run_first = g_array_index (layer->run_first, guint, p);
			run_count = layer->patches[p].instances->len;
			run_lod = lod;
		}
	}

	rlActiveTextureSlot (0);
	rlDisableTexture ();
	rlDisableShader ();
	rlEnableBackfaceCulling ();
	rlDisableDepthMask ();
}

void
_lrg_foliage_collect_render (LrgFoliage      *self,
                             LrgTerrainAsset *terrain,
                             LrgRenderList   *list,
                             LrgEntity3D     *entity,
                             LrgComponent    *component,
                             gint             layer_override,
                             gfloat           time)
{
	gfloat eye[3];
	guint  i, p;

	if (self->layers->len == 0)
		return;
	if (terrain != NULL && lrg_foliage_get_dirty_patches (self) > 0)
	{
		if (self->base_dir == NULL && lrg_terrain_asset_get_base_dir (terrain) != NULL)
			self->base_dir = g_strdup (lrg_terrain_asset_get_base_dir (terrain));
		_lrg_foliage_scatter (self, terrain);
	}
	lrg_render_list_get_eye (list, eye);

	for (i = 0; i < self->layers->len; i++)
	{
		LrgFoliageLayer  *layer = g_ptr_array_index (self->layers, i);
		LrgRenderItem     item;
		LrgBoundingBox3D  local;
		LayerDraw        *ld;
		gboolean          any = FALSE;

		for (p = 0; p < layer->n_patches; p++)
		{
			Patch *patch = &layer->patches[p];

			if (patch->instances->len == 0)
				continue;
			if (!any)
			{
				local.min.x = patch->min[0];
				local.min.y = patch->min[1];
				local.min.z = patch->min[2];
				local.max.x = patch->max[0];
				local.max.y = patch->max[1];
				local.max.z = patch->max[2];
				any = TRUE;
				continue;
			}
			local.min.x = MIN (local.min.x, patch->min[0]);
			local.min.y = MIN (local.min.y, patch->min[1]);
			local.min.z = MIN (local.min.z, patch->min[2]);
			local.max.x = MAX (local.max.x, patch->max[0]);
			local.max.y = MAX (local.max.y, patch->max[1]);
			local.max.z = MAX (local.max.z, patch->max[2]);
		}
		if (!any)
			continue;

		lrg_render_item_init_for_entity (&item, entity, component);
		item.kind = LRG_RENDER_ITEM_CUSTOM;
		item.flags = (LrgRenderItemFlags) (LRG_RENDER_ITEM_FLAG_TRANSPARENT | LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED);
		if (layer_override >= 0)
			item.layer = (guint) layer_override;
		item.sort_key = ((guint64) LRG_RENDER_ITEM_CUSTOM << 56) | (guint64) (0x100u + i);
		lrg_render_item_set_local_bounds (&item, &local);
		if (!lrg_render_list_accepts (list, &item))
			continue;

		ld = g_new0 (LayerDraw, 1);
		ld->foliage = g_object_ref (self);
		ld->layer = i;
		memcpy (ld->world, item.world_matrix, sizeof ld->world);
		memcpy (ld->eye, eye, sizeof eye);
		ld->time = time;
		item.draw_func = draw_layer;
		item.user_data = ld;
		item.user_data_destroy = layer_draw_free;
		lrg_render_list_add (list, &item);
	}
}
