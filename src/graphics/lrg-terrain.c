/* lrg-terrain.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Chunked heightfield terrain. Chunks keep their source heights,
 * materials and UV variants; meshes are generated lazily on the CPU (one
 * or more per material, 16-bit indices, chunk-local positions) and kept
 * for inspection and re-upload. upload() lends the CPU arrays to raylib
 * and clears them from the raylib Mesh afterwards, so UnloadMesh() only
 * ever releases GPU objects (the same scheme as LrgStaticBatch).
 *
 * Positions are relative to each chunk's corner and drawing translates by
 * (corner - origin) computed in double precision, so a floating origin
 * costs nothing and stays exact far from world zero.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-terrain.h"

/* raylib binds up to MAX_MATERIAL_MAPS (12) maps; keep headroom. */
#define TERRAIN_MATERIAL_MAPS (16)

/* The three meaningful bits of a per-cell UV variant. */
#define TERRAIN_UV_MASK (LRG_TERRAIN_UV_ROTATE_90 | LRG_TERRAIN_UV_FLIP_U | LRG_TERRAIN_UV_FLIP_V)

typedef struct
{
    guint    material;
    GArray  *positions;   /* gfloat x3, chunk-local */
    GArray  *normals;     /* gfloat x3 */
    GArray  *texcoords;   /* gfloat x2 */
    GArray  *indices;     /* guint16 */
    Mesh     mesh;        /* GPU handles only */
    gboolean uploaded;
} TerrainMesh;

typedef struct
{
    gint64     key;        /* hash key, see chunk_key() */
    gint       cx;
    gint       cz;
    gfloat    *heights;    /* (res + 1)² */
    guint     *materials;  /* res² */
    guint8    *variants;   /* res² */
    gfloat     min_y;
    gfloat     max_y;
    GPtrArray *meshes;     /* TerrainMesh*, valid when mesh_valid */
    gboolean   mesh_valid; /* CPU meshes match the source data */
} TerrainChunk;

typedef struct
{
    GrlTexture *owned;
    Texture2D   texture;
} MaterialTexture;

struct _LrgTerrain
{
    GObject     parent_instance;

    guint       resolution;
    gfloat      chunk_size;
    guint       repeat;
    gdouble     origin[3];
    Color       tint;
    GHashTable *chunks;     /* &chunk->key -> TerrainChunk* */
    GHashTable *textures;   /* GUINT_TO_POINTER (material) -> MaterialTexture* */
    GrlShader  *shader;
};

G_DEFINE_TYPE (LrgTerrain, lrg_terrain, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------ */
/* Meshes                                                                   */
/* ------------------------------------------------------------------------ */

/* terrain_mesh_unload_gpu:
 * Releases the mesh's GPU objects. Without a context only the CPU-side
 * VBO id array can be released. */
static void
terrain_mesh_unload_gpu (TerrainMesh *tm)
{
    if (!tm->uploaded)
        return;
    if (IsWindowReady ())
        UnloadMesh (tm->mesh);
    else
        RL_FREE (tm->mesh.vboId);
    memset (&tm->mesh, 0, sizeof tm->mesh);
    tm->uploaded = FALSE;
}

static void
terrain_mesh_free (gpointer data)
{
    TerrainMesh *tm = data;

    terrain_mesh_unload_gpu (tm);
    g_array_unref (tm->positions);
    g_array_unref (tm->normals);
    g_array_unref (tm->texcoords);
    g_array_unref (tm->indices);
    g_free (tm);
}

static TerrainMesh *
terrain_mesh_new (guint material)
{
    TerrainMesh *tm = g_new0 (TerrainMesh, 1);

    tm->material = material;
    tm->positions = g_array_new (FALSE, FALSE, sizeof (gfloat));
    tm->normals = g_array_new (FALSE, FALSE, sizeof (gfloat));
    tm->texcoords = g_array_new (FALSE, FALSE, sizeof (gfloat));
    tm->indices = g_array_new (FALSE, FALSE, sizeof (guint16));
    return tm;
}

/* ------------------------------------------------------------------------ */
/* Chunks                                                                   */
/* ------------------------------------------------------------------------ */

static gint64
chunk_key (gint cx,
           gint cz)
{
    return (gint64)(((guint64)(guint32)cx << 32) | (guint64)(guint32)cz);
}

static void
chunk_free (gpointer data)
{
    TerrainChunk *chunk = data;

    g_clear_pointer (&chunk->meshes, g_ptr_array_unref);
    g_free (chunk->heights);
    g_free (chunk->materials);
    g_free (chunk->variants);
    g_free (chunk);
}

static TerrainChunk *
lookup_chunk (LrgTerrain *self,
              gint        cx,
              gint        cz)
{
    gint64 key = chunk_key (cx, cz);

    return g_hash_table_lookup (self->chunks, &key);
}

/* invalidate_chunk:
 * Drops a chunk's CPU meshes (and with them their GPU meshes) so the next
 * inspection or upload rebuilds them. */
static void
invalidate_chunk (TerrainChunk *chunk)
{
    if (chunk == NULL)
        return;
    chunk->mesh_valid = FALSE;
}

/* invalidate_neighbours:
 * Edge normals sample the four orthogonal neighbours, so they change
 * whenever a chunk appears or disappears next to them. */
static void
invalidate_neighbours (LrgTerrain *self,
                       gint        cx,
                       gint        cz)
{
    invalidate_chunk (lookup_chunk (self, cx - 1, cz));
    invalidate_chunk (lookup_chunk (self, cx + 1, cz));
    invalidate_chunk (lookup_chunk (self, cx, cz - 1));
    invalidate_chunk (lookup_chunk (self, cx, cz + 1));
}

/* sample_grid:
 * Height at grid vertex (i, j) of @chunk, where i or j may be one step
 * outside [0, res]; such samples come from the neighbouring chunk, whose
 * vertex 0 coincides with our vertex res. */
static gboolean
sample_grid (LrgTerrain         *self,
             const TerrainChunk *chunk,
             gint                i,
             gint                j,
             gfloat             *out)
{
    const TerrainChunk *source = chunk;
    gint                res = (gint)self->resolution;
    gint                dx = 0;
    gint                dz = 0;

    if (i < 0)
        dx = -1;
    else if (i > res)
        dx = 1;
    if (j < 0)
        dz = -1;
    else if (j > res)
        dz = 1;

    if (dx != 0 || dz != 0)
    {
        source = lookup_chunk (self, chunk->cx + dx, chunk->cz + dz);
        if (source == NULL)
            return FALSE;
        i -= dx * res;
        j -= dz * res;
    }
    *out = source->heights[j * (res + 1) + i];
    return TRUE;
}

/* grid_normal:
 * Central differences of the heights (one-sided at an unloaded edge);
 * the surface y = h(x, z) has normal (-dh/dx, 1, -dh/dz). */
static void
grid_normal (LrgTerrain         *self,
             const TerrainChunk *chunk,
             gint                i,
             gint                j,
             gfloat              cell,
             gfloat             *n)
{
    gfloat h = chunk->heights[j * ((gint)self->resolution + 1) + i];
    gfloat a;
    gfloat b;
    gboolean has_a;
    gboolean has_b;
    gfloat dhdx = 0.0f;
    gfloat dhdz = 0.0f;
    gfloat len;

    has_a = sample_grid (self, chunk, i - 1, j, &a);
    has_b = sample_grid (self, chunk, i + 1, j, &b);
    if (has_a && has_b)
        dhdx = (b - a) / (2.0f * cell);
    else if (has_b)
        dhdx = (b - h) / cell;
    else if (has_a)
        dhdx = (h - a) / cell;

    has_a = sample_grid (self, chunk, i, j - 1, &a);
    has_b = sample_grid (self, chunk, i, j + 1, &b);
    if (has_a && has_b)
        dhdz = (b - a) / (2.0f * cell);
    else if (has_b)
        dhdz = (b - h) / cell;
    else if (has_a)
        dhdz = (h - a) / cell;

    len = sqrtf (dhdx * dhdx + 1.0f + dhdz * dhdz);
    n[0] = -dhdx / len;
    n[1] = 1.0f / len;
    n[2] = -dhdz / len;
}

/* apply_variant:
 * Reorients a cell-local texture coordinate (s, t in [0, 1]): rotation
 * first, (s, t) -> (t, 1 - s), then the flips. */
static void
apply_variant (guint8  variant,
               gfloat *s,
               gfloat *t)
{
    gfloat tmp;

    if (variant & LRG_TERRAIN_UV_ROTATE_90)
    {
        tmp = *s;
        *s = *t;
        *t = 1.0f - tmp;
    }
    if (variant & LRG_TERRAIN_UV_FLIP_U)
        *s = 1.0f - *s;
    if (variant & LRG_TERRAIN_UV_FLIP_V)
        *t = 1.0f - *t;
}

/* positive_mod:
 * a mod m in [0, m) for negative a too (global cell indices west/north
 * of the world origin). */
static guint
positive_mod (gint64 a,
              guint  m)
{
    gint64 r = a % (gint64)m;

    return (guint)(r < 0 ? r + (gint64)m : r);
}

typedef struct
{
    const guint *materials;
} SortContext;

static gint
compare_cells (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
    const SortContext *ctx = user_data;
    guint ca = *(const guint *)a;
    guint cb = *(const guint *)b;
    guint ma = ctx->materials[ca];
    guint mb = ctx->materials[cb];

    if (ma != mb)
        return ma < mb ? -1 : 1;
    return (ca > cb) - (ca < cb);
}

/* build_chunk_meshes:
 * Generates the chunk's CPU meshes: cells grouped by material (ascending)
 * and, inside a material, in row-major cell order. Each cell owns four
 * vertices (its UVs are its own) and two counter-clockwise triangles. */
static void
build_chunk_meshes (LrgTerrain   *self,
                    TerrainChunk *chunk)
{
    g_autofree gfloat *grid_normals = NULL;
    g_autoptr(GArray)  order = NULL;
    SortContext        ctx;
    TerrainMesh       *current = NULL;
    guint              res = self->resolution;
    guint              stride = res + 1;
    gfloat             cell = self->chunk_size / (gfloat)res;
    guint              n_cells = res * res;
    guint              k;
    guint              i;
    guint              j;

    g_clear_pointer (&chunk->meshes, g_ptr_array_unref);
    chunk->meshes = g_ptr_array_new_with_free_func (terrain_mesh_free);

    /* One normal per grid vertex, shared by the up to four cells that
     * touch it, so lighting is smooth across cells. */
    grid_normals = g_new (gfloat, stride * stride * 3);
    for (j = 0; j <= res; j++)
        for (i = 0; i <= res; i++)
            grid_normal (self, chunk, (gint)i, (gint)j, cell,
                         &grid_normals[(j * stride + i) * 3]);

    order = g_array_sized_new (FALSE, FALSE, sizeof (guint), n_cells);
    for (k = 0; k < n_cells; k++)
        g_array_append_val (order, k);
    ctx.materials = chunk->materials;
    g_array_sort_with_data (order, compare_cells, &ctx);

    for (k = 0; k < n_cells; k++)
    {
        guint   c = g_array_index (order, guint, k);
        guint   ci = c % res;
        guint   cj = c / res;
        guint   material = chunk->materials[c];
        guint8  variant = chunk->variants[c];
        guint   gu = positive_mod ((gint64)chunk->cx * res + ci, self->repeat);
        guint   gv = positive_mod ((gint64)chunk->cz * res + cj, self->repeat);
        guint16 base;
        guint   corner;

        if (current == NULL || current->material != material ||
            current->positions->len / 3 + 4 > LRG_TERRAIN_MAX_MESH_VERTICES)
        {
            current = terrain_mesh_new (material);
            g_ptr_array_add (chunk->meshes, current);
        }
        base = (guint16)(current->positions->len / 3);

        /* corners: 0 = (i, j), 1 = (i+1, j), 2 = (i, j+1), 3 = (i+1, j+1) */
        for (corner = 0; corner < 4; corner++)
        {
            guint  gi = ci + (corner & 1u);
            guint  gj = cj + (corner >> 1);
            gfloat p[3];
            gfloat uv[2];
            gfloat s = (gfloat)(corner & 1u);
            gfloat t = (gfloat)(corner >> 1);

            p[0] = (gfloat)gi * cell;
            p[1] = chunk->heights[gj * stride + gi];
            p[2] = (gfloat)gj * cell;
            g_array_append_vals (current->positions, p, 3);
            g_array_append_vals (current->normals, &grid_normals[(gj * stride + gi) * 3], 3);

            apply_variant (variant, &s, &t);
            uv[0] = ((gfloat)gu + s) / (gfloat)self->repeat;
            uv[1] = ((gfloat)gv + t) / (gfloat)self->repeat;
            g_array_append_vals (current->texcoords, uv, 2);
        }

        /* (0, 2, 1) and (1, 2, 3): both have normal +Y in raylib's
         * counter-clockwise convention. */
        {
            guint16 tri[6];

            tri[0] = base;
            tri[1] = (guint16)(base + 2);
            tri[2] = (guint16)(base + 1);
            tri[3] = (guint16)(base + 1);
            tri[4] = (guint16)(base + 2);
            tri[5] = (guint16)(base + 3);
            g_array_append_vals (current->indices, tri, 6);
        }
    }
    chunk->mesh_valid = TRUE;
}

static TerrainChunk *
ready_chunk (LrgTerrain *self,
             gint        cx,
             gint        cz)
{
    TerrainChunk *chunk = lookup_chunk (self, cx, cz);

    if (chunk != NULL && !chunk->mesh_valid)
        build_chunk_meshes (self, chunk);
    return chunk;
}

static TerrainMesh *
mesh_at (LrgTerrain *self,
         gint        cx,
         gint        cz,
         guint       mesh)
{
    TerrainChunk *chunk = ready_chunk (self, cx, cz);

    if (chunk == NULL || mesh >= chunk->meshes->len)
        return NULL;
    return g_ptr_array_index (chunk->meshes, mesh);
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

static void
material_texture_free (gpointer data)
{
    MaterialTexture *entry = data;

    g_clear_object (&entry->owned);
    g_free (entry);
}

static void
lrg_terrain_finalize (GObject *object)
{
    LrgTerrain *self = LRG_TERRAIN (object);

    g_clear_pointer (&self->chunks, g_hash_table_unref);
    g_clear_pointer (&self->textures, g_hash_table_unref);
    g_clear_object (&self->shader);

    G_OBJECT_CLASS (lrg_terrain_parent_class)->finalize (object);
}

static void
lrg_terrain_class_init (LrgTerrainClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_terrain_finalize;
}

static void
lrg_terrain_init (LrgTerrain *self)
{
    self->resolution = 1;
    self->chunk_size = 1.0f;
    self->repeat = 1;
    self->tint = (Color) { 255, 255, 255, 255 };
    self->chunks = g_hash_table_new_full (g_int64_hash, g_int64_equal, NULL, chunk_free);
    self->textures = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                            material_texture_free);
}

LrgTerrain *
lrg_terrain_new (guint  resolution,
                 gfloat chunk_size)
{
    LrgTerrain *self;

    g_return_val_if_fail (resolution >= 1 && resolution <= LRG_TERRAIN_MAX_RESOLUTION, NULL);
    g_return_val_if_fail (chunk_size > 0.0f && isfinite (chunk_size), NULL);

    self = g_object_new (LRG_TYPE_TERRAIN, NULL);
    self->resolution = resolution;
    self->chunk_size = chunk_size;
    return self;
}

guint
lrg_terrain_get_resolution (LrgTerrain *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN (self), 0);
    return self->resolution;
}

gfloat
lrg_terrain_get_chunk_size (LrgTerrain *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN (self), 0.0f);
    return self->chunk_size;
}

/* ------------------------------------------------------------------------ */
/* Chunk data                                                               */
/* ------------------------------------------------------------------------ */

gboolean
lrg_terrain_set_chunk (LrgTerrain    *self,
                       gint           cx,
                       gint           cz,
                       const gfloat  *heights,
                       guint          n_heights,
                       const guint   *materials,
                       const guint8  *variants,
                       GError       **error)
{
    TerrainChunk *chunk;
    guint         stride;
    guint         n_cells;
    guint         k;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), FALSE);
    g_return_val_if_fail (heights != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    stride = self->resolution + 1;
    n_cells = self->resolution * self->resolution;

    /* Validate everything before touching the terrain. */
    if (n_heights != stride * stride)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Terrain chunk needs %u heights, got %u", stride * stride, n_heights);
        return FALSE;
    }
    for (k = 0; k < n_heights; k++)
    {
        if (!isfinite (heights[k]))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Terrain height %u is not finite", k);
            return FALSE;
        }
    }
    if (variants != NULL)
    {
        for (k = 0; k < n_cells; k++)
        {
            if ((variants[k] & ~TERRAIN_UV_MASK) != 0)
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Terrain cell %u has invalid UV variant 0x%02x", k, variants[k]);
                return FALSE;
            }
        }
    }

    chunk = g_new0 (TerrainChunk, 1);
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->key = chunk_key (cx, cz);
    chunk->heights = g_memdup2 (heights, n_heights * sizeof (gfloat));
    if (materials != NULL)
        chunk->materials = g_memdup2 (materials, n_cells * sizeof (guint));
    else
        chunk->materials = g_new0 (guint, n_cells);
    if (variants != NULL)
        chunk->variants = g_memdup2 (variants, n_cells);
    else
        chunk->variants = g_new0 (guint8, n_cells);

    chunk->min_y = G_MAXFLOAT;
    chunk->max_y = -G_MAXFLOAT;
    for (k = 0; k < n_heights; k++)
    {
        chunk->min_y = MIN (chunk->min_y, heights[k]);
        chunk->max_y = MAX (chunk->max_y, heights[k]);
    }

    /* Replacing frees the old chunk, including its GPU meshes. */
    g_hash_table_replace (self->chunks, &chunk->key, chunk);
    invalidate_neighbours (self, cx, cz);
    return TRUE;
}

gboolean
lrg_terrain_remove_chunk (LrgTerrain *self,
                          gint        cx,
                          gint        cz)
{
    gint64 key;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), FALSE);

    key = chunk_key (cx, cz);
    if (!g_hash_table_remove (self->chunks, &key))
        return FALSE;
    invalidate_neighbours (self, cx, cz);
    return TRUE;
}

gboolean
lrg_terrain_has_chunk (LrgTerrain *self,
                       gint        cx,
                       gint        cz)
{
    g_return_val_if_fail (LRG_IS_TERRAIN (self), FALSE);
    return lookup_chunk (self, cx, cz) != NULL;
}

guint
lrg_terrain_get_chunk_count (LrgTerrain *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN (self), 0);
    return g_hash_table_size (self->chunks);
}

void
lrg_terrain_clear (LrgTerrain *self)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));
    g_hash_table_remove_all (self->chunks);
}

/* ------------------------------------------------------------------------ */
/* Queries                                                                  */
/* ------------------------------------------------------------------------ */

void
lrg_terrain_world_to_chunk (LrgTerrain *self,
                            gdouble     x,
                            gdouble     z,
                            gint       *cx,
                            gint       *cz)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));

    if (cx != NULL)
        *cx = (gint)floor (x / (gdouble)self->chunk_size);
    if (cz != NULL)
        *cz = (gint)floor (z / (gdouble)self->chunk_size);
}

gboolean
lrg_terrain_height_at (LrgTerrain *self,
                       gdouble     x,
                       gdouble     z,
                       gfloat     *height)
{
    TerrainChunk *chunk;
    gdouble       cell;
    gdouble       lx;
    gdouble       lz;
    gint          cx;
    gint          cz;
    gint          i;
    gint          j;
    gint          res;
    gint          stride;
    gfloat        fx;
    gfloat        fz;
    gfloat        h00;
    gfloat        h10;
    gfloat        h01;
    gfloat        h11;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), FALSE);

    if (!isfinite (x) || !isfinite (z))
        return FALSE;

    lrg_terrain_world_to_chunk (self, x, z, &cx, &cz);
    chunk = lookup_chunk (self, cx, cz);
    if (chunk == NULL)
        return FALSE;

    /* Cell coordinates inside the chunk, clamped against rounding. */
    res = (gint)self->resolution;
    stride = res + 1;
    cell = (gdouble)self->chunk_size / (gdouble)res;
    lx = (x - (gdouble)cx * (gdouble)self->chunk_size) / cell;
    lz = (z - (gdouble)cz * (gdouble)self->chunk_size) / cell;
    i = CLAMP ((gint)floor (lx), 0, res - 1);
    j = CLAMP ((gint)floor (lz), 0, res - 1);
    fx = (gfloat)CLAMP (lx - (gdouble)i, 0.0, 1.0);
    fz = (gfloat)CLAMP (lz - (gdouble)j, 0.0, 1.0);

    h00 = chunk->heights[j * stride + i];
    h10 = chunk->heights[j * stride + i + 1];
    h01 = chunk->heights[(j + 1) * stride + i];
    h11 = chunk->heights[(j + 1) * stride + i + 1];

    if (height != NULL)
        *height = (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fz)
                  + (h01 * (1.0f - fx) + h11 * fx) * fz;
    return TRUE;
}

gboolean
lrg_terrain_get_chunk_bounds (LrgTerrain *self,
                              gint        cx,
                              gint        cz,
                              gdouble    *min_x,
                              gdouble    *min_y,
                              gdouble    *min_z,
                              gdouble    *max_x,
                              gdouble    *max_y,
                              gdouble    *max_z)
{
    TerrainChunk *chunk;
    gdouble       size;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), FALSE);

    chunk = lookup_chunk (self, cx, cz);
    if (chunk == NULL)
        return FALSE;

    size = (gdouble)self->chunk_size;
    if (min_x != NULL)
        *min_x = (gdouble)cx * size;
    if (min_y != NULL)
        *min_y = chunk->min_y;
    if (min_z != NULL)
        *min_z = (gdouble)cz * size;
    if (max_x != NULL)
        *max_x = ((gdouble)cx + 1.0) * size;
    if (max_y != NULL)
        *max_y = chunk->max_y;
    if (max_z != NULL)
        *max_z = ((gdouble)cz + 1.0) * size;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                 */
/* ------------------------------------------------------------------------ */

void
lrg_terrain_set_texture_repeat (LrgTerrain *self,
                                guint       cells)
{
    GHashTableIter iter;
    gpointer       value;

    g_return_if_fail (LRG_IS_TERRAIN (self));
    g_return_if_fail (cells >= 1);

    if (self->repeat == cells)
        return;
    self->repeat = cells;

    g_hash_table_iter_init (&iter, self->chunks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        invalidate_chunk (value);
}

guint
lrg_terrain_get_texture_repeat (LrgTerrain *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN (self), 1);
    return self->repeat;
}

void
lrg_terrain_set_origin (LrgTerrain *self,
                        gdouble     x,
                        gdouble     y,
                        gdouble     z)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));

    self->origin[0] = x;
    self->origin[1] = y;
    self->origin[2] = z;
}

void
lrg_terrain_get_origin (LrgTerrain *self,
                        gdouble    *x,
                        gdouble    *y,
                        gdouble    *z)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));

    if (x != NULL)
        *x = self->origin[0];
    if (y != NULL)
        *y = self->origin[1];
    if (z != NULL)
        *z = self->origin[2];
}

/* ------------------------------------------------------------------------ */
/* Mesh inspection                                                          */
/* ------------------------------------------------------------------------ */

guint
lrg_terrain_get_chunk_mesh_count (LrgTerrain *self,
                                  gint        cx,
                                  gint        cz)
{
    TerrainChunk *chunk;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), 0);

    chunk = ready_chunk (self, cx, cz);
    return chunk != NULL ? chunk->meshes->len : 0;
}

guint
lrg_terrain_get_chunk_mesh_material (LrgTerrain *self,
                                     gint        cx,
                                     gint        cz,
                                     guint       mesh)
{
    TerrainMesh *tm;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), 0);

    tm = mesh_at (self, cx, cz, mesh);
    return tm != NULL ? tm->material : 0;
}

/* get_array:
 * Shared body of the CPU array getters. */
static gconstpointer
get_array (GArray *array,
           guint  *n)
{
    if (n != NULL)
        *n = array->len;
    return array->len > 0 ? array->data : NULL;
}

const gfloat *
lrg_terrain_get_chunk_mesh_positions (LrgTerrain *self,
                                      gint        cx,
                                      gint        cz,
                                      guint       mesh,
                                      guint      *n_floats)
{
    TerrainMesh *tm;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), NULL);

    if (n_floats != NULL)
        *n_floats = 0;
    tm = mesh_at (self, cx, cz, mesh);
    return tm != NULL ? get_array (tm->positions, n_floats) : NULL;
}

const gfloat *
lrg_terrain_get_chunk_mesh_normals (LrgTerrain *self,
                                    gint        cx,
                                    gint        cz,
                                    guint       mesh,
                                    guint      *n_floats)
{
    TerrainMesh *tm;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), NULL);

    if (n_floats != NULL)
        *n_floats = 0;
    tm = mesh_at (self, cx, cz, mesh);
    return tm != NULL ? get_array (tm->normals, n_floats) : NULL;
}

const gfloat *
lrg_terrain_get_chunk_mesh_texcoords (LrgTerrain *self,
                                      gint        cx,
                                      gint        cz,
                                      guint       mesh,
                                      guint      *n_floats)
{
    TerrainMesh *tm;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), NULL);

    if (n_floats != NULL)
        *n_floats = 0;
    tm = mesh_at (self, cx, cz, mesh);
    return tm != NULL ? get_array (tm->texcoords, n_floats) : NULL;
}

const guint16 *
lrg_terrain_get_chunk_mesh_indices (LrgTerrain *self,
                                    gint        cx,
                                    gint        cz,
                                    guint       mesh,
                                    guint      *n_indices)
{
    TerrainMesh *tm;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), NULL);

    if (n_indices != NULL)
        *n_indices = 0;
    tm = mesh_at (self, cx, cz, mesh);
    return tm != NULL ? get_array (tm->indices, n_indices) : NULL;
}

/* ------------------------------------------------------------------------ */
/* Materials and shader                                                     */
/* ------------------------------------------------------------------------ */

void
lrg_terrain_set_material_texture (LrgTerrain *self,
                                  guint       material,
                                  GrlTexture *texture)
{
    MaterialTexture *entry;

    g_return_if_fail (LRG_IS_TERRAIN (self));
    g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

    if (texture == NULL)
    {
        g_hash_table_remove (self->textures, GUINT_TO_POINTER (material));
        return;
    }
    entry = g_new0 (MaterialTexture, 1);
    entry->owned = g_object_ref (texture);
    entry->texture = *(Texture2D *)grl_texture_get_handle (texture);
    g_hash_table_replace (self->textures, GUINT_TO_POINTER (material), entry);
}

void
lrg_terrain_set_tint (LrgTerrain     *self,
                      const GrlColor *tint)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));

    if (tint == NULL)
        self->tint = (Color) { 255, 255, 255, 255 };
    else
        self->tint = (Color) { tint->r, tint->g, tint->b, tint->a };
}

void
lrg_terrain_set_shader (LrgTerrain *self,
                        GrlShader  *shader)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));
    g_return_if_fail (shader == NULL || GRL_IS_SHADER (shader));

    g_set_object (&self->shader, shader);
}

/* ------------------------------------------------------------------------ */
/* GPU                                                                      */
/* ------------------------------------------------------------------------ */

static void
upload_mesh (TerrainMesh *tm)
{
    Mesh mesh;

    terrain_mesh_unload_gpu (tm);

    /* raylib reads the borrowed CPU arrays during UploadMesh() only. */
    memset (&mesh, 0, sizeof mesh);
    mesh.vertexCount = (gint)(tm->positions->len / 3);
    mesh.triangleCount = (gint)(tm->indices->len / 3);
    mesh.vertices = (gfloat *)tm->positions->data;
    mesh.normals = (gfloat *)tm->normals->data;
    mesh.texcoords = (gfloat *)tm->texcoords->data;
    mesh.indices = (unsigned short *)tm->indices->data;
    UploadMesh (&mesh, false);

    mesh.vertices = NULL;
    mesh.normals = NULL;
    mesh.texcoords = NULL;
    mesh.indices = NULL;
    tm->mesh = mesh;
    tm->uploaded = TRUE;
}

gboolean
lrg_terrain_upload (LrgTerrain  *self,
                    GError     **error)
{
    GHashTableIter iter;
    gpointer       value;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!IsWindowReady ())
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                             "A graphics context is required to upload terrain");
        return FALSE;
    }

    g_hash_table_iter_init (&iter, self->chunks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        TerrainChunk *chunk = value;
        guint         m;

        if (!chunk->mesh_valid)
            build_chunk_meshes (self, chunk);
        for (m = 0; m < chunk->meshes->len; m++)
        {
            TerrainMesh *tm = g_ptr_array_index (chunk->meshes, m);

            if (!tm->uploaded && tm->indices->len > 0)
                upload_mesh (tm);
        }
    }
    return TRUE;
}

gboolean
lrg_terrain_is_uploaded (LrgTerrain *self)
{
    GHashTableIter iter;
    gpointer       value;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), FALSE);

    g_hash_table_iter_init (&iter, self->chunks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        TerrainChunk *chunk = value;
        guint         m;

        if (!chunk->mesh_valid || chunk->meshes == NULL)
            return FALSE;
        for (m = 0; m < chunk->meshes->len; m++)
        {
            TerrainMesh *tm = g_ptr_array_index (chunk->meshes, m);

            if (!tm->uploaded)
                return FALSE;
        }
    }
    return TRUE;
}

void
lrg_terrain_unload (LrgTerrain *self)
{
    GHashTableIter iter;
    gpointer       value;

    g_return_if_fail (LRG_IS_TERRAIN (self));

    g_hash_table_iter_init (&iter, self->chunks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        TerrainChunk *chunk = value;
        guint         m;

        if (chunk->meshes == NULL)
            continue;
        for (m = 0; m < chunk->meshes->len; m++)
            terrain_mesh_unload_gpu (g_ptr_array_index (chunk->meshes, m));
    }
}

/* draw_chunk:
 * DrawMesh() each uploaded mesh with a stack material (terrain shader or
 * raylib's default, the material texture or raylib's white texture, the
 * tint) and a translation to (corner - origin). The translation is the
 * model matrix, so lighting shaders see origin-relative positions. */
static void
draw_chunk (LrgTerrain   *self,
            TerrainChunk *chunk)
{
    MaterialMap maps[TERRAIN_MATERIAL_MAPS];
    Material    material;
    Matrix      transform;
    guint       m;

    if (chunk->meshes == NULL)
        return;

    memset (&transform, 0, sizeof transform);
    transform.m0 = 1.0f;
    transform.m5 = 1.0f;
    transform.m10 = 1.0f;
    transform.m15 = 1.0f;
    transform.m12 = (gfloat)((gdouble)chunk->cx * (gdouble)self->chunk_size - self->origin[0]);
    transform.m13 = (gfloat)(-self->origin[1]);
    transform.m14 = (gfloat)((gdouble)chunk->cz * (gdouble)self->chunk_size - self->origin[2]);

    memset (&material, 0, sizeof material);
    if (self->shader != NULL)
        material.shader = *(Shader *)grl_shader_get_handle (self->shader);
    else
    {
        material.shader.id = rlGetShaderIdDefault ();
        material.shader.locs = rlGetShaderLocsDefault ();
    }

    for (m = 0; m < chunk->meshes->len; m++)
    {
        TerrainMesh     *tm = g_ptr_array_index (chunk->meshes, m);
        MaterialTexture *entry;
        Mesh             mesh;

        if (!tm->uploaded)
            continue;

        /* DrawMesh() draws indexed only when Mesh.indices is non-NULL;
         * the uploaded element buffer is what is actually drawn. */
        mesh = tm->mesh;
        mesh.indices = (unsigned short *)tm->indices->data;

        memset (maps, 0, sizeof maps);
        entry = g_hash_table_lookup (self->textures, GUINT_TO_POINTER (tm->material));
        if (entry != NULL)
            maps[MATERIAL_MAP_ALBEDO].texture = entry->texture;
        else
        {
            maps[MATERIAL_MAP_ALBEDO].texture.id = rlGetTextureIdDefault ();
            maps[MATERIAL_MAP_ALBEDO].texture.width = 1;
            maps[MATERIAL_MAP_ALBEDO].texture.height = 1;
            maps[MATERIAL_MAP_ALBEDO].texture.mipmaps = 1;
            maps[MATERIAL_MAP_ALBEDO].texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        }
        maps[MATERIAL_MAP_ALBEDO].color = self->tint;
        material.maps = maps;

        DrawMesh (mesh, material, transform);
    }
}

void
lrg_terrain_draw (LrgTerrain *self)
{
    GHashTableIter iter;
    gpointer       value;

    g_return_if_fail (LRG_IS_TERRAIN (self));

    g_hash_table_iter_init (&iter, self->chunks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        draw_chunk (self, value);
}

void
lrg_terrain_draw_chunk (LrgTerrain *self,
                        gint        cx,
                        gint        cz)
{
    TerrainChunk *chunk;

    g_return_if_fail (LRG_IS_TERRAIN (self));

    chunk = lookup_chunk (self, cx, cz);
    if (chunk != NULL)
        draw_chunk (self, chunk);
}
