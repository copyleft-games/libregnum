/* lrg-grass-field.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Instanced grass. Placement hashes (seed, cell, slot) so it needs no
 * state and no random stream: any cell can be placed again, in any
 * order, on any thread, with the same result. The draw is one
 * DrawMeshInstanced() of a 12-vertex card mesh; the vertex shader
 * bends the card in the wind, shades it from base to tip, lights it
 * with the bound light set (LRG_LIGHT_SAMPLE_GLSL, as the billboard
 * batch does) and computes the distance fade, which the fragment
 * shader turns into a dithered discard (no sorting needed).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-grass-field.h"
#include "graphics/lrg-light-set-private.h"
#include "graphics/lrg-hash-noise-private.h"

G_DEFINE_BOXED_TYPE (LrgGrassBlade, lrg_grass_blade, lrg_grass_blade_copy, lrg_grass_blade_free)

static const gchar grass_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "uniform float time;\n"
    "uniform vec2 wind;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec2 fade;\n"
    "uniform vec3 baseColor;\n"
    "uniform vec3 tipColor;\n"
    "uniform int lit;\n"
    LRG_LIGHT_SAMPLE_GLSL
    "out vec3 grassColor;\n"
    "out float grassFade;\n"
    "out vec3 fragPosition;\n"
    "void main() {\n"
    "    vec4 wp = instanceTransform * vec4(vertexPosition, 1.0);\n"
    "    vec2 root = instanceTransform[3].xz;\n"
    "    float h = vertexTexCoord.y;\n"
    "    float phase = dot(root, vec2(0.131, 0.173));\n"
    "    float bend = wind.x * h * h;\n"
    "    wp.x += sin(time * wind.y + phase) * bend;\n"
    "    wp.z += cos(time * wind.y * 0.83 + phase * 1.37) * bend * 0.6;\n"
    "    float shade = 0.85 + 0.3 * fract(sin(dot(root, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "    vec3 c = mix(baseColor, tipColor, h) * shade;\n"
    "    if (lit == 1) c *= lrg_light_sample(wp.xyz);\n"
    "    grassColor = c;\n"
    "    float d = distance(wp.xyz, viewPos);\n"
    "    grassFade = 1.0 - smoothstep(fade.x, fade.y, d);\n"
    "    fragPosition = wp.xyz;\n"
    "    gl_Position = mvp * wp;\n"
    "}\n";

static const gchar grass_fs[] =
    "#version 330\n"
    "in vec3 grassColor;\n"
    "in float grassFade;\n"
    "in vec3 fragPosition;\n"
    LRG_FOG_GLSL
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float dither = fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)) * 52.9829189);\n"
    "    if (grassFade <= dither) discard;\n"
    "    finalColor = vec4(mix(grassColor, fogColor.rgb, lrg_fog_amount(fragPosition)), 1.0);\n"
    "}\n";

struct _LrgGrassField
{
    GObject      parent_instance;

    guint32      seed;
    gfloat       cell_size;
    guint        blades_per_cell;
    gfloat       min_height;
    gfloat       max_height;
    gfloat       min_width;
    gfloat       max_width;
    gfloat       fade_start;
    gfloat       fade_end;
    gfloat       wind_strength;
    gfloat       wind_frequency;
    gfloat       base[3];
    gfloat       tip[3];

    GArray      *instances;     /* Matrix */

    /* GPU, made on the first draw */
    gboolean     gpu_tried;
    gboolean     gpu_loaded;
    Shader       shader;
    Mesh         mesh;
    Material     material;
    gint         loc_time;
    gint         loc_wind;
    gint         loc_view;
    gint         loc_fade;
    gint         loc_base;
    gint         loc_tip;
    gint         loc_lit;
    LrgLightSetSampleLocations light_locs;
    LrgLightSetFogLocations    fog_locs;
};

G_DEFINE_FINAL_TYPE (LrgGrassField, lrg_grass_field, G_TYPE_OBJECT)

LrgGrassBlade *
lrg_grass_blade_copy (const LrgGrassBlade *blade)
{
    g_return_val_if_fail (blade != NULL, NULL);
    return g_memdup2 (blade, sizeof (LrgGrassBlade));
}

void
lrg_grass_blade_free (LrgGrassBlade *blade)
{
    g_free (blade);
}

static void
lrg_grass_field_finalize (GObject *object)
{
    LrgGrassField *self = LRG_GRASS_FIELD (object);

    /* GPU objects only while a context lives (never at exit) */
    if (IsWindowReady ())
        lrg_grass_field_release_gpu (self);
    g_clear_pointer (&self->instances, g_array_unref);

    G_OBJECT_CLASS (lrg_grass_field_parent_class)->finalize (object);
}

static void
lrg_grass_field_class_init (LrgGrassFieldClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_grass_field_finalize;
}

static void
lrg_grass_field_init (LrgGrassField *self)
{
    self->cell_size = 2.0f;
    self->blades_per_cell = 24;
    self->min_height = 0.35f;
    self->max_height = 0.7f;
    self->min_width = 0.3f;
    self->max_width = 0.5f;
    self->fade_start = 30.0f;
    self->fade_end = 45.0f;
    self->wind_strength = 0.08f;
    self->wind_frequency = 1.6f;
    self->base[0] = 0.18f; self->base[1] = 0.32f; self->base[2] = 0.12f;
    self->tip[0] = 0.45f; self->tip[1] = 0.62f; self->tip[2] = 0.25f;
    self->instances = g_array_new (FALSE, FALSE, sizeof (Matrix));
}

LrgGrassField *
lrg_grass_field_new (guint32 seed)
{
    LrgGrassField *self;

    self = g_object_new (LRG_TYPE_GRASS_FIELD, NULL);
    self->seed = seed;
    return self;
}

void
lrg_grass_field_set_cell_size (LrgGrassField *self,
                               gfloat         size)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    if (isfinite (size) && size > 0.0f)
        self->cell_size = size;
}

gfloat
lrg_grass_field_get_cell_size (LrgGrassField *self)
{
    g_return_val_if_fail (LRG_IS_GRASS_FIELD (self), 0.0f);
    return self->cell_size;
}

void
lrg_grass_field_set_blades_per_cell (LrgGrassField *self,
                                     guint          blades)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    self->blades_per_cell = MIN (blades, (guint)LRG_GRASS_FIELD_MAX_BLADES_PER_CELL);
}

guint
lrg_grass_field_get_blades_per_cell (LrgGrassField *self)
{
    g_return_val_if_fail (LRG_IS_GRASS_FIELD (self), 0);
    return self->blades_per_cell;
}

void
lrg_grass_field_set_blade_size (LrgGrassField *self,
                                gfloat         min_height,
                                gfloat         max_height,
                                gfloat         min_width,
                                gfloat         max_width)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    if (!(min_height > 0.0f) || !(max_height >= min_height) ||
        !(min_width > 0.0f) || !(max_width >= min_width))
        return;
    self->min_height = min_height;
    self->max_height = max_height;
    self->min_width = min_width;
    self->max_width = max_width;
}

void
lrg_grass_field_set_fade (LrgGrassField *self,
                          gfloat         start,
                          gfloat         end)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    if (!(start >= 0.0f) || !(end > start))
        return;
    self->fade_start = start;
    self->fade_end = end;
}

gfloat
lrg_grass_field_get_fade_end (LrgGrassField *self)
{
    g_return_val_if_fail (LRG_IS_GRASS_FIELD (self), 0.0f);
    return self->fade_end;
}

void
lrg_grass_field_set_wind (LrgGrassField *self,
                          gfloat         strength,
                          gfloat         frequency)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    self->wind_strength = isfinite (strength) ? MAX (strength, 0.0f) : 0.0f;
    self->wind_frequency = isfinite (frequency) ? frequency : 0.0f;
}

void
lrg_grass_field_set_colors (LrgGrassField  *self,
                            const GrlColor *base,
                            const GrlColor *tip)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    g_return_if_fail (base != NULL && tip != NULL);

    self->base[0] = (gfloat)base->r / 255.0f;
    self->base[1] = (gfloat)base->g / 255.0f;
    self->base[2] = (gfloat)base->b / 255.0f;
    self->tip[0] = (gfloat)tip->r / 255.0f;
    self->tip[1] = (gfloat)tip->g / 255.0f;
    self->tip[2] = (gfloat)tip->b / 255.0f;
}

guint
lrg_grass_field_place_cell (LrgGrassField      *self,
                            gint64              cell_x,
                            gint64              cell_z,
                            gfloat              density,
                            gdouble             draw_offset_x,
                            gdouble             draw_offset_z,
                            LrgGrassHeightFunc  height_func,
                            gpointer            user_data,
                            GArray             *out_blades)
{
    guint32 cell_hash;
    guint placed;
    guint slot;

    g_return_val_if_fail (LRG_IS_GRASS_FIELD (self), 0);
    g_return_val_if_fail (out_blades != NULL, 0);
    g_return_val_if_fail (g_array_get_element_size (out_blades) == sizeof (LrgGrassBlade), 0);

    if (!(density > 0.0f))
        return 0;
    cell_hash = lrg_hash_2d (self->seed, cell_x, cell_z);
    placed = 0;
    for (slot = 0; slot < self->blades_per_cell; slot++)
    {
        LrgGrassBlade blade;
        guint32 state;
        gdouble px;
        gdouble pz;
        gfloat y;

        /* the slot's own stream: its first draw decides presence, so
         * raising the density never moves an existing blade */
        state = lrg_hash_mix (cell_hash ^ (0x27d4eb2dU * (slot + 1)));
        if (lrg_hash_next_unit (&state) >= density)
            continue;
        px = ((gdouble)cell_x + (gdouble)lrg_hash_next_unit (&state)) * self->cell_size;
        pz = ((gdouble)cell_z + (gdouble)lrg_hash_next_unit (&state)) * self->cell_size;
        blade.position[0] = (gfloat)(px - draw_offset_x);
        blade.position[2] = (gfloat)(pz - draw_offset_z);
        y = 0.0f;
        if (height_func != NULL && !height_func (blade.position[0], blade.position[2], &y, user_data))
            continue;
        blade.position[1] = y;
        blade.yaw = lrg_hash_next_unit (&state) * (gfloat)G_PI;
        blade.height = self->min_height + (self->max_height - self->min_height) * lrg_hash_next_unit (&state);
        blade.width = self->min_width + (self->max_width - self->min_width) * lrg_hash_next_unit (&state);
        blade.shade = 0.85f + 0.3f * lrg_hash_next_unit (&state);
        g_array_append_val (out_blades, blade);
        placed++;
    }
    return placed;
}

void
lrg_grass_field_set_blades (LrgGrassField       *self,
                            const LrgGrassBlade *blades,
                            guint                n_blades)
{
    guint i;

    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    g_return_if_fail (blades != NULL || n_blades == 0);

    g_array_set_size (self->instances, n_blades);
    for (i = 0; i < n_blades; i++)
    {
        const LrgGrassBlade *b = &blades[i];
        Matrix *m = &g_array_index (self->instances, Matrix, i);
        gfloat c;
        gfloat s;

        /* translate * rotateY(yaw) * scale(width, height, width) */
        c = cosf (b->yaw);
        s = sinf (b->yaw);
        memset (m, 0, sizeof (*m));
        m->m0 = c * b->width;
        m->m2 = -s * b->width;
        m->m5 = b->height;
        m->m8 = s * b->width;
        m->m10 = c * b->width;
        m->m12 = b->position[0];
        m->m13 = b->position[1];
        m->m14 = b->position[2];
        m->m15 = 1.0f;
    }
}

guint
lrg_grass_field_get_instance_count (LrgGrassField *self)
{
    g_return_val_if_fail (LRG_IS_GRASS_FIELD (self), 0);
    return self->instances->len;
}

/* the unit card: two quads at right angles, two segments high, tapering */
static void
card_positions (gfloat *out)
{
    guint q;
    guint v;

    v = 0;
    for (q = 0; q < 2; q++)
    {
        guint row;

        for (row = 0; row < 3; row++)
        {
            gfloat h;
            gfloat half;
            guint side;

            h = (gfloat)row / 2.0f;
            half = 0.5f * (1.0f - 0.7f * h);
            for (side = 0; side < 2; side++)
            {
                gfloat x;

                x = side == 0 ? -half : half;
                out[v * 3 + 0] = q == 0 ? x : 0.0f;
                out[v * 3 + 1] = h;
                out[v * 3 + 2] = q == 0 ? 0.0f : x;
                v++;
            }
        }
    }
}

void
lrg_grass_field_get_card (LrgGrassField *self,
                          gfloat        *out_positions)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    g_return_if_fail (out_positions != NULL);
    card_positions (out_positions);
}

static gboolean
ensure_gpu (LrgGrassField *self)
{
    guint q;
    guint t;
    guint v;

    if (self->gpu_tried)
        return self->gpu_loaded;
    if (!IsWindowReady ())
        return FALSE;
    self->gpu_tried = TRUE;

    self->shader = LoadShaderFromMemory (grass_vs, grass_fs);
    if (self->shader.id == 0 || self->shader.id == rlGetShaderIdDefault ())
    {
        g_warning ("grass field: the shader did not compile (GLSL 330 needed)");
        return FALSE;
    }
    self->shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM] =
        GetShaderLocationAttrib (self->shader, "instanceTransform");
    self->shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation (self->shader, "mvp");
    self->loc_time = GetShaderLocation (self->shader, "time");
    self->loc_wind = GetShaderLocation (self->shader, "wind");
    self->loc_view = GetShaderLocation (self->shader, "viewPos");
    self->loc_fade = GetShaderLocation (self->shader, "fade");
    self->loc_base = GetShaderLocation (self->shader, "baseColor");
    self->loc_tip = GetShaderLocation (self->shader, "tipColor");
    self->loc_lit = GetShaderLocation (self->shader, "lit");
    lrg_light_set_sample_locations (self->shader, &self->light_locs);
    lrg_light_set_fog_locations (self->shader, &self->fog_locs);

    /* the card mesh: positions, height in texcoord.y, two-sided */
    memset (&self->mesh, 0, sizeof (self->mesh));
    self->mesh.vertexCount = LRG_GRASS_FIELD_CARD_VERTICES;
    self->mesh.triangleCount = LRG_GRASS_FIELD_CARD_TRIANGLES;
    self->mesh.vertices = MemAlloc (sizeof (gfloat) * 3 * LRG_GRASS_FIELD_CARD_VERTICES);
    self->mesh.texcoords = MemAlloc (sizeof (gfloat) * 2 * LRG_GRASS_FIELD_CARD_VERTICES);
    self->mesh.normals = MemAlloc (sizeof (gfloat) * 3 * LRG_GRASS_FIELD_CARD_VERTICES);
    self->mesh.indices = MemAlloc (sizeof (gushort) * 3 * LRG_GRASS_FIELD_CARD_TRIANGLES);
    card_positions (self->mesh.vertices);
    for (v = 0; v < LRG_GRASS_FIELD_CARD_VERTICES; v++)
    {
        self->mesh.texcoords[v * 2 + 0] = (gfloat)(v % 2);
        self->mesh.texcoords[v * 2 + 1] = self->mesh.vertices[v * 3 + 1];
        self->mesh.normals[v * 3 + 0] = 0.0f;
        self->mesh.normals[v * 3 + 1] = 1.0f;
        self->mesh.normals[v * 3 + 2] = 0.0f;
    }
    t = 0;
    for (q = 0; q < 2; q++)
    {
        guint seg;

        for (seg = 0; seg < 2; seg++)
        {
            gushort a = (gushort)(q * 6 + seg * 2);

            self->mesh.indices[t++] = a;
            self->mesh.indices[t++] = (gushort)(a + 1);
            self->mesh.indices[t++] = (gushort)(a + 3);
            self->mesh.indices[t++] = a;
            self->mesh.indices[t++] = (gushort)(a + 3);
            self->mesh.indices[t++] = (gushort)(a + 2);
        }
    }
    UploadMesh (&self->mesh, FALSE);
    self->material = LoadMaterialDefault ();
    self->material.shader = self->shader;
    self->gpu_loaded = TRUE;
    return TRUE;
}

void
lrg_grass_field_draw (LrgGrassField *self,
                      const gfloat  *eye,
                      gfloat         time)
{
    LrgLightSet *lights;
    gfloat wind[2];
    gfloat fade[2];
    gint lit;

    g_return_if_fail (LRG_IS_GRASS_FIELD (self));
    g_return_if_fail (eye != NULL);

    if (self->instances->len == 0 || !ensure_gpu (self))
        return;

    rlDrawRenderBatchActive ();
    lights = lrg_light_set_get_bound ();
    wind[0] = self->wind_strength;
    wind[1] = self->wind_frequency;
    fade[0] = self->fade_start;
    fade[1] = self->fade_end;
    rlEnableShader (self->shader.id);
    SetShaderValue (self->shader, self->loc_time, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue (self->shader, self->loc_wind, wind, SHADER_UNIFORM_VEC2);
    SetShaderValue (self->shader, self->loc_view, eye, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_fade, fade, SHADER_UNIFORM_VEC2);
    SetShaderValue (self->shader, self->loc_base, self->base, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->shader, self->loc_tip, self->tip, SHADER_UNIFORM_VEC3);
    lit = lrg_light_set_upload_sample (lights, self->shader, &self->light_locs) ? 1 : 0;
    SetShaderValue (self->shader, self->loc_lit, &lit, SHADER_UNIFORM_INT);
    lrg_light_set_upload_fog (lights, self->shader, &self->fog_locs);

    rlDisableBackfaceCulling ();
    DrawMeshInstanced (self->mesh, self->material, (const Matrix *)self->instances->data,
                       (gint)self->instances->len);
    rlEnableBackfaceCulling ();
}

void
lrg_grass_field_release_gpu (LrgGrassField *self)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));

    if (self->gpu_loaded)
    {
        UnloadMesh (self->mesh);
        memset (&self->mesh, 0, sizeof (self->mesh));
        MemFree (self->material.maps);
        self->material.maps = NULL;
        UnloadShader (self->shader);
    }
    else if (self->gpu_tried && self->shader.id != 0 && self->shader.id != rlGetShaderIdDefault ())
    {
        UnloadShader (self->shader);
    }
    memset (&self->shader, 0, sizeof (self->shader));
    self->gpu_loaded = FALSE;
    self->gpu_tried = FALSE;
}
