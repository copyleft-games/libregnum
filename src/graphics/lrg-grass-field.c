/* lrg-grass-field.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Instanced grass. Placement hashes (seed, cell, slot) so it needs no
 * state and no random stream: any cell can be placed again, in any
 * order, on any thread, with the same result. The draw is an
 * instanced draw of a 12-vertex card mesh; the vertex shader bends the
 * card in the wind, shades it from base to tip, lights it with the bound
 * light set (LRG_LIGHT_SAMPLE_GLSL, as the billboard batch does) and
 * computes the distance fade, which the fragment shader turns into a
 * dithered discard (no sorting needed).
 *
 * The instance matrices live in a vertex buffer of their own, sent only
 * when the blades change (raylib's DrawMeshInstanced() makes, fills and
 * deletes one on every call). The blades are cut into groups of
 * GRASS_GROUP consecutive instances with the box their cards can reach
 * (wind included); a draw skips the groups wholly outside the frustum or
 * wholly beyond the fade's end and draws the rest in their original
 * order, consecutive groups as one call, so what is drawn is drawn
 * exactly as one DrawMeshInstanced() of every blade drew it.
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
#include "graphics/lrg-draw-cull-private.h"

/* the material maps LoadMaterialDefault() allocates and the draw walks:
 * raylib's MAX_MATERIAL_MAPS (rmodels.c, not exported) */
#define GRASS_MATERIAL_MAPS (12u)

/* instances per culling group (consecutive in the caller's order) */
#define GRASS_GROUP (256u)

/* one culling group: instances first .. first + count - 1 and the box
 * their cards cover at rest (before the wind) */
typedef struct
{
    guint  first;
    guint  count;
    gfloat min[3];
    gfloat max[3];
} GrassGroup;

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
    GArray      *groups;        /* GrassGroup */
    gboolean     instances_dirty; /* the instance buffer is behind */
    guint        instance_vbo;  /* 0: none yet */
    guint        instance_capacity; /* instances the buffer holds room for */

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
    g_clear_pointer (&self->groups, g_array_unref);

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
    self->groups = g_array_new (FALSE, FALSE, sizeof (GrassGroup));
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

/* build_groups:
 * Cuts the instances into GRASS_GROUP runs and boxes each: the card
 * spans x, z in [-0.5, 0.5] and y in [0, 1] before its matrix, so a
 * blade covers its matrix applied to the box's centre plus |M| times
 * the half extents. */
static void
build_groups (LrgGrassField *self)
{
    guint first;

    g_array_set_size (self->groups, 0);
    for (first = 0; first < self->instances->len; first += GRASS_GROUP)
    {
        GrassGroup group;
        guint      i;
        guint      k;

        group.first = first;
        group.count = MIN (self->instances->len - first, GRASS_GROUP);
        for (k = 0; k < 3; k++)
        {
            group.min[k] = G_MAXFLOAT;
            group.max[k] = -G_MAXFLOAT;
        }
        for (i = 0; i < group.count; i++)
        {
            const Matrix *m = &g_array_index (self->instances, Matrix, first + i);
            gfloat        c[3];
            gfloat        e[3];

            /* centre (0, 0.5, 0) and half extents (0.5, 0.5, 0.5) */
            c[0] = m->m4 * 0.5f + m->m12;
            c[1] = m->m5 * 0.5f + m->m13;
            c[2] = m->m6 * 0.5f + m->m14;
            e[0] = 0.5f * (fabsf (m->m0) + fabsf (m->m4) + fabsf (m->m8));
            e[1] = 0.5f * (fabsf (m->m1) + fabsf (m->m5) + fabsf (m->m9));
            e[2] = 0.5f * (fabsf (m->m2) + fabsf (m->m6) + fabsf (m->m10));
            for (k = 0; k < 3; k++)
            {
                group.min[k] = MIN (group.min[k], c[k] - e[k]);
                group.max[k] = MAX (group.max[k], c[k] + e[k]);
            }
        }
        g_array_append_val (self->groups, group);
    }
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
    build_groups (self);
    self->instances_dirty = TRUE;
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

/* upload_instances:
 * Sends the instance matrices (column-major, as raylib's
 * MatrixToFloatV() lays them out; a Matrix declares its fields in
 * another order, so they are copied one by one) into the instance
 * buffer, growing it when the blades outgrew it. */
static void
upload_instances (LrgGrassField *self)
{
    g_autofree gfloat *data = NULL;
    guint              n = self->instances->len;
    guint              i;

    data = g_new (gfloat, (gsize)n * 16);
    for (i = 0; i < n; i++)
    {
        const Matrix *m = &g_array_index (self->instances, Matrix, i);
        gfloat       *v = data + (gsize)i * 16;

        v[0] = m->m0;   v[1] = m->m1;   v[2] = m->m2;   v[3] = m->m3;
        v[4] = m->m4;   v[5] = m->m5;   v[6] = m->m6;   v[7] = m->m7;
        v[8] = m->m8;   v[9] = m->m9;   v[10] = m->m10; v[11] = m->m11;
        v[12] = m->m12; v[13] = m->m13; v[14] = m->m14; v[15] = m->m15;
    }
    if (self->instance_vbo != 0 && self->instance_capacity < n)
    {
        rlUnloadVertexBuffer (self->instance_vbo);
        self->instance_vbo = 0;
    }
    if (self->instance_vbo == 0)
    {
        self->instance_vbo = rlLoadVertexBuffer (data, (gint)((gsize)n * 16 * sizeof (gfloat)),
                                                 FALSE);
        self->instance_capacity = n;
    }
    else
    {
        rlUpdateVertexBuffer (self->instance_vbo, data,
                              (gint)((gsize)n * 16 * sizeof (gfloat)), 0);
    }
    self->instances_dirty = FALSE;
}

/* group_visible:
 * Whether any blade of @group can show: inside @frustum (when not
 * %NULL) and, when @eye is not %NULL, nearer it than the fade's end
 * (from there on 1 - smoothstep (start, end, d) is 0 and every fragment
 * is discarded). @pad_x and @pad_z are the most the wind moves a vertex
 * along x and z. */
static gboolean
group_visible (LrgGrassField        *self,
               const GrassGroup     *group,
               const LrgDrawFrustum *frustum,
               const gfloat         *eye,
               gfloat                pad_x,
               gfloat                pad_z)
{
    gfloat lo[3];
    gfloat hi[3];
    gfloat d2;
    gfloat reach;
    guint  k;

    lo[0] = group->min[0] - pad_x;
    lo[1] = group->min[1];
    lo[2] = group->min[2] - pad_z;
    hi[0] = group->max[0] + pad_x;
    hi[1] = group->max[1];
    hi[2] = group->max[2] + pad_z;
    if (frustum != NULL && !lrg_draw_cull_frustum_box_visible (frustum, lo, hi))
        return FALSE;
    if (eye == NULL)
        return TRUE;

    d2 = 0.0f;
    for (k = 0; k < 3; k++)
    {
        gfloat e = eye[k] < lo[k] ? lo[k] - eye[k] : eye[k] > hi[k] ? eye[k] - hi[k] : 0.0f;

        d2 += e * e;
    }
    reach = self->fade_end * 1.001f + 0.01f;
    return d2 <= reach * reach;
}

/* draw_run:
 * One instanced draw of instances @first .. @first + @count - 1: the
 * instance attribute is pointed at the run's first matrix. */
static void
draw_run (LrgGrassField *self,
          guint          first,
          guint          count)
{
    gint  loc = self->material.shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM];
    guint k;

    for (k = 0; k < 4; k++)
    {
        rlEnableVertexAttribute ((guint)loc + k);
        rlSetVertexAttribute ((guint)loc + k, 4, RL_FLOAT, FALSE, (gint)(16 * sizeof (gfloat)),
                              (gint)(((gsize)first * 16 + k * 4) * sizeof (gfloat)));
        rlSetVertexAttributeDivisor ((guint)loc + k, 1);
    }
    rlDrawVertexArrayElementsInstanced (0, self->mesh.triangleCount * 3, NULL, (gint)count);
}

/* draw_instanced:
 * DrawMeshInstanced() with the persistent instance buffer: the same
 * uniforms, textures and matrices (mvp = P x MV x T), one instanced
 * draw per run of visible groups. Needs the mesh's vertex array. */
static void
draw_instanced (LrgGrassField *self,
                const gfloat  *eye)
{
    LrgDrawFrustum frustum;
    const LrgDrawFrustum *use_frustum;
    const gfloat  *use_eye;
    Shader         shader = self->material.shader;
    Matrix         view;
    Matrix         projection;
    Matrix         mvp;
    gfloat         pad_x;
    gfloat         pad_z;
    guint          run_first = 0;
    guint          run_count = 0;
    guint          i;

    if (self->instances_dirty || self->instance_vbo == 0)
        upload_instances (self);

    /* the uniforms DrawMeshInstanced() sends */
    rlEnableShader (shader.id);
    if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] != -1)
    {
        Color  c = self->material.maps[MATERIAL_MAP_DIFFUSE].color;
        gfloat values[4];

        values[0] = (gfloat)c.r / 255.0f;
        values[1] = (gfloat)c.g / 255.0f;
        values[2] = (gfloat)c.b / 255.0f;
        values[3] = (gfloat)c.a / 255.0f;
        rlSetUniform (shader.locs[SHADER_LOC_COLOR_DIFFUSE], values, SHADER_UNIFORM_VEC4, 1);
    }
    if (shader.locs[SHADER_LOC_COLOR_SPECULAR] != -1)
    {
        Color  c = self->material.maps[MATERIAL_MAP_SPECULAR].color;
        gfloat values[4];

        values[0] = (gfloat)c.r / 255.0f;
        values[1] = (gfloat)c.g / 255.0f;
        values[2] = (gfloat)c.b / 255.0f;
        values[3] = (gfloat)c.a / 255.0f;
        rlSetUniform (shader.locs[SHADER_LOC_COLOR_SPECULAR], values, SHADER_UNIFORM_VEC4, 1);
    }
    view = rlGetMatrixModelview ();
    projection = rlGetMatrixProjection ();
    if (shader.locs[SHADER_LOC_MATRIX_VIEW] != -1)
        rlSetUniformMatrix (shader.locs[SHADER_LOC_MATRIX_VIEW], view);
    if (shader.locs[SHADER_LOC_MATRIX_PROJECTION] != -1)
        rlSetUniformMatrix (shader.locs[SHADER_LOC_MATRIX_PROJECTION], projection);
    if (shader.locs[SHADER_LOC_MATRIX_NORMAL] != -1)
    {
        /* the model matrix is the identity: so is its normal matrix */
        Matrix identity;

        memset (&identity, 0, sizeof identity);
        identity.m0 = 1.0f;
        identity.m5 = 1.0f;
        identity.m10 = 1.0f;
        identity.m15 = 1.0f;
        rlSetUniformMatrix (shader.locs[SHADER_LOC_MATRIX_NORMAL], identity);
    }
    for (i = 0; i < GRASS_MATERIAL_MAPS; i++)
    {
        gint unit = (gint)i;

        if (self->material.maps[i].texture.id == 0)
            continue;
        rlActiveTextureSlot (unit);
        if (i == MATERIAL_MAP_IRRADIANCE || i == MATERIAL_MAP_PREFILTER
            || i == MATERIAL_MAP_CUBEMAP)
            rlEnableTextureCubemap (self->material.maps[i].texture.id);
        else
            rlEnableTexture (self->material.maps[i].texture.id);
        rlSetUniform (shader.locs[SHADER_LOC_MAP_DIFFUSE + i], &unit, SHADER_UNIFORM_INT, 1);
    }
    /* raylib's MatrixMultiply (MatrixMultiply (T, V), P) */
    mvp = lrg_draw_cull_mul (projection, lrg_draw_cull_mul (view, rlGetMatrixTransform ()));
    rlSetUniformMatrix (shader.locs[SHADER_LOC_MATRIX_MVP], mvp);

    /* what may be skipped: groups outside that same mvp's frustum, and
     * groups past the fade (only with a finite eye and wind) */
    use_frustum = lrg_draw_cull_frustum_from_matrix (&frustum, mvp) ? &frustum : NULL;
    use_eye = isfinite (eye[0]) && isfinite (eye[1]) && isfinite (eye[2]) ? eye : NULL;
    pad_x = self->wind_strength * 1.001f;
    pad_z = self->wind_strength * 0.6f * 1.001f;
    if (!isfinite (pad_x))
    {
        use_frustum = NULL;
        use_eye = NULL;
    }

    rlEnableVertexArray (self->mesh.vaoId);
    rlEnableVertexBuffer (self->instance_vbo);
    for (i = 0; i < self->groups->len; i++)
    {
        const GrassGroup *group = &g_array_index (self->groups, GrassGroup, i);

        if ((use_frustum != NULL || use_eye != NULL)
            && !group_visible (self, group, use_frustum, use_eye, pad_x, pad_z))
            continue;
        if (run_count > 0 && run_first + run_count == group->first)
        {
            run_count += group->count;
            continue;
        }
        if (run_count > 0)
            draw_run (self, run_first, run_count);
        run_first = group->first;
        run_count = group->count;
    }
    if (run_count > 0)
        draw_run (self, run_first, run_count);

    for (i = 0; i < GRASS_MATERIAL_MAPS; i++)
    {
        if (self->material.maps[i].texture.id == 0)
            continue;
        rlActiveTextureSlot ((gint)i);
        if (i == MATERIAL_MAP_IRRADIANCE || i == MATERIAL_MAP_PREFILTER
            || i == MATERIAL_MAP_CUBEMAP)
            rlDisableTextureCubemap ();
        else
            rlDisableTexture ();
    }
    rlDisableVertexArray ();
    rlDisableVertexBuffer ();
    rlDisableVertexBufferElement ();
    rlDisableShader ();
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
    if (self->mesh.vaoId == 0 || rlIsStereoRenderEnabled ()
        || self->material.shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM] < 0)
    {
        /* no vertex array (or two eyes): raylib's own path, every blade */
        DrawMeshInstanced (self->mesh, self->material, (const Matrix *)self->instances->data,
                           (gint)self->instances->len);
    }
    else
    {
        draw_instanced (self, eye);
    }
    rlEnableBackfaceCulling ();
}

void
lrg_grass_field_release_gpu (LrgGrassField *self)
{
    g_return_if_fail (LRG_IS_GRASS_FIELD (self));

    if (self->instance_vbo != 0)
        rlUnloadVertexBuffer (self->instance_vbo);
    self->instance_vbo = 0;
    self->instance_capacity = 0;
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
