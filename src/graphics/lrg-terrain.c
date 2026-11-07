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
 *
 * On OpenGL 3.3 the meshes are not uploaded one raylib Mesh each: every
 * chunk owns a fixed-size slot (4 vertices and 6 indices per cell) in
 * shared pages of vertex and index buffers with one VAO per page, and
 * draw() binds the program, the per-pass uniforms and the page once and
 * issues one glDrawElementsBaseVertex() per mesh with only the chunk's
 * matrices changing between draws. The uniform values, vertex data and
 * draw order are exactly those of the DrawMesh() path, so the pixels
 * are too. Elsewhere (or with LRG_TERRAIN_DRAW=meshes) each mesh is a
 * raylib Mesh drawn with DrawMesh(), as before.
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
#include "graphics/lrg-draw-cull-private.h"
#include "graphics/lrg-camera.h"

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
    GArray  *grid;        /* guint32: grid vertex of each vertex (normal refresh) */
    guint    first_vertex; /* first vertex inside the chunk's slot (shared path) */
    guint    first_index;  /* first index inside the chunk's slot (shared path) */
    Mesh     mesh;        /* GPU handles only (raylib mesh path) */
    gboolean uploaded;
    gboolean normals_dirty; /* uploaded, but the normals changed since */
} TerrainMesh;

typedef struct
{
    struct _LrgTerrain *owner; /* gets the slot back when the chunk goes */
    gint       slot;       /* slot in the shared pages, -1 = none */
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
    gboolean   normals_stale; /* a neighbour came or went: edge normals moved */
    gint       layer;      /* render layer override, -1 = the terrain's */
} TerrainChunk;

typedef struct
{
    GrlTexture *owned;
    Texture2D   texture;
} MaterialTexture;

/* One page of chunk slots: positions, texcoords and normals in three
 * vertex buffers at raylib's attribute locations 0, 1 and 2, and the
 * 16-bit indices, bound together in one VAO. */
typedef struct
{
    guint vao;
    guint vbo[3];
    guint ebo;
} TerrainPage;

/* How meshes reach the GPU, fixed at the first upload with a window. */
typedef enum
{
    TERRAIN_PATH_UNDECIDED,
    TERRAIN_PATH_SHARED,  /* chunk slots in shared pages, base-vertex draws */
    TERRAIN_PATH_MESHES   /* one raylib Mesh per terrain mesh, DrawMesh() */
} TerrainPath;

/* One chunk in draw order with its box in draw space (the chunk-local
 * box moved by the translation the chunk is drawn with), so the per-pass
 * culling loop reads one compact array instead of chasing every chunk. */
typedef struct
{
    gfloat        lo[3];
    gfloat        hi[3];
    gboolean      always;  /* no finite height range: never culled */
    TerrainChunk *chunk;
} DrawEntry;

/* A block of up to 4 x 4 neighbouring chunks: the union of their boxes,
 * tested first, and their entries in the draw list. */
typedef struct
{
    gfloat lo[3];
    gfloat hi[3];
    guint  first;  /* first member in draw_members */
    guint  count;
} DrawBlock;

/* Chunks per block side (a power of two) for the per-pass culling. */
#define TERRAIN_BLOCK_SHIFT (2)

/* Bytes of one page (fewer slots for very fine chunks, never under one). */
#define TERRAIN_PAGE_BYTES (8u * 1024u * 1024u)

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
    GHashTable *assets;     /* GUINT_TO_POINTER (material) -> LrgMaterial* (0.9.0) */
    GrlShader  *shader;
    TerrainPath path;
    GArray     *pages;      /* TerrainPage */
    GArray     *free_slots; /* guint: released slots, reused last first */
    guint       slot_count; /* slots handed out from the pages so far */
    guint       slots_per_page;
    GArray     *draw_list;  /* DrawEntry, in the chunk table's order */
    GArray     *draw_blocks;  /* DrawBlock */
    GArray     *draw_members; /* guint: draw_list indices, grouped by block */
    GArray     *draw_visible; /* guint8 per draw_list entry, per pass scratch */
    gboolean    draw_list_valid;
    guint       layer;        /* render layer of chunks without an override */
    GHashTable *chunk_layers; /* gint64 chunk key -> GINT_TO_POINTER (layer + 1);
                               * survives a chunk's replacement */
};

G_DEFINE_TYPE (LrgTerrain, lrg_terrain, G_TYPE_OBJECT)

static void release_pages (LrgTerrain *self);

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
    if (tm->mesh.vboId == NULL)
    {
        /* shared path: the data lives in the chunk's slot, owned by the
         * terrain's pages */
        tm->uploaded = FALSE;
        return;
    }
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
    g_array_unref (tm->grid);
    g_free (tm);
}

/* terrain_mesh_new:
 * A mesh of @cells cells, its arrays sized for them (four vertices and
 * six indices a cell) and filled by the caller. */
static TerrainMesh *
terrain_mesh_new (guint material,
                  guint cells)
{
    TerrainMesh *tm = g_new0 (TerrainMesh, 1);

    tm->material = material;
    tm->positions = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), cells * 12);
    tm->normals = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), cells * 12);
    tm->texcoords = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), cells * 8);
    tm->indices = g_array_sized_new (FALSE, FALSE, sizeof (guint16), cells * 6);
    tm->grid = g_array_sized_new (FALSE, FALSE, sizeof (guint32), cells * 4);
    g_array_set_size (tm->positions, cells * 12);
    g_array_set_size (tm->normals, cells * 12);
    g_array_set_size (tm->texcoords, cells * 8);
    g_array_set_size (tm->indices, cells * 6);
    g_array_set_size (tm->grid, cells * 4);
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

    if (chunk->slot >= 0 && chunk->owner != NULL && chunk->owner->free_slots != NULL)
    {
        guint slot = (guint)chunk->slot;

        g_array_append_val (chunk->owner->free_slots, slot);
    }
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

/* stale_normals:
 * Only a neighbour changed: the chunk's geometry stands, its edge
 * normals need recomputing (refresh_normals()), which is far cheaper
 * than a rebuild. A chunk without valid meshes rebuilds anyway. */
static void
stale_normals (TerrainChunk *chunk)
{
    if (chunk == NULL || !chunk->mesh_valid)
        return;
    chunk->normals_stale = TRUE;
}

/* invalidate_neighbours:
 * Edge normals sample the four orthogonal neighbours, so they change
 * whenever a chunk appears or disappears next to them. */
static void
invalidate_neighbours (LrgTerrain *self,
                       gint        cx,
                       gint        cz)
{
    stale_normals (lookup_chunk (self, cx - 1, cz));
    stale_normals (lookup_chunk (self, cx + 1, cz));
    stale_normals (lookup_chunk (self, cx, cz - 1));
    stale_normals (lookup_chunk (self, cx, cz + 1));
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

/* compare_guint:
 * Ascending order of material ids. */
static gint
compare_guint (gconstpointer a,
               gconstpointer b)
{
    guint ua = *(const guint *)a;
    guint ub = *(const guint *)b;

    return (ua > ub) - (ua < ub);
}

/* compute_grid_normals:
 * One normal per grid vertex, shared by the up to four cells that touch
 * it, so lighting is smooth across cells (and, through the neighbours'
 * heights, across chunks). */
static gfloat *
compute_grid_normals (LrgTerrain         *self,
                      const TerrainChunk *chunk)
{
    guint   res = self->resolution;
    guint   stride = res + 1;
    gfloat  cell = self->chunk_size / (gfloat)res;
    gfloat *normals;
    guint   i;
    guint   j;

    normals = g_new (gfloat, stride * stride * 3);
    for (j = 0; j <= res; j++)
        for (i = 0; i <= res; i++)
            grid_normal (self, chunk, (gint)i, (gint)j, cell, &normals[(j * stride + i) * 3]);
    return normals;
}

/* build_chunk_meshes:
 * Generates the chunk's CPU meshes: cells grouped by material (ascending)
 * and, inside a material, in row-major cell order. Each cell owns four
 * vertices (its UVs are its own) and two counter-clockwise triangles.
 * The cells are bucketed by material in one pass (a material's cells
 * keep their row-major order) and every array is sized up front. */
static void
build_chunk_meshes (LrgTerrain   *self,
                    TerrainChunk *chunk)
{
    g_autofree gfloat *grid_normals = NULL;
    g_autoptr(GArray)  materials = NULL;     /* distinct ids, ascending */
    g_autoptr(GHashTable) slot_of = NULL;    /* material -> slot + 1 */
    g_autofree guint  *counts = NULL;        /* cells of each slot */
    g_autofree guint  *first = NULL;         /* first entry of each slot in order[] */
    g_autofree guint  *order = NULL;         /* cells bucketed by material */
    guint              res = self->resolution;
    guint              stride = res + 1;
    gfloat             cell = self->chunk_size / (gfloat)res;
    guint              n_cells = res * res;
    guint              max_cells = LRG_TERRAIN_MAX_MESH_VERTICES / 4;
    guint              k;
    guint              m;
    guint              vertex_at = 0;
    guint              index_at = 0;

    g_clear_pointer (&chunk->meshes, g_ptr_array_unref);
    chunk->meshes = g_ptr_array_new_with_free_func (terrain_mesh_free);
    grid_normals = compute_grid_normals (self, chunk);

    /* the distinct materials in ascending order, then a stable bucket
     * pass: a counting sort by material, row-major inside one */
    materials = g_array_new (FALSE, FALSE, sizeof (guint));
    slot_of = g_hash_table_new (g_direct_hash, g_direct_equal);
    for (k = 0; k < n_cells; k++)
    {
        if (!g_hash_table_contains (slot_of, GUINT_TO_POINTER (chunk->materials[k])))
        {
            g_hash_table_insert (slot_of, GUINT_TO_POINTER (chunk->materials[k]), GUINT_TO_POINTER (1));
            g_array_append_val (materials, chunk->materials[k]);
        }
    }
    g_array_sort (materials, compare_guint);
    for (m = 0; m < materials->len; m++)
        g_hash_table_insert (slot_of, GUINT_TO_POINTER (g_array_index (materials, guint, m)),
                             GUINT_TO_POINTER (m + 1));

    counts = g_new0 (guint, materials->len);
    first = g_new0 (guint, materials->len);
    order = g_new (guint, n_cells);
    {
        g_autofree guint *slot = g_new (guint, n_cells);
        g_autofree guint *fill = g_new0 (guint, materials->len);

        for (k = 0; k < n_cells; k++)
        {
            slot[k] = GPOINTER_TO_UINT (g_hash_table_lookup (slot_of,
                                                             GUINT_TO_POINTER (chunk->materials[k]))) - 1;
            counts[slot[k]]++;
        }
        for (m = 1; m < materials->len; m++)
            first[m] = first[m - 1] + counts[m - 1];
        for (k = 0; k < n_cells; k++)
            order[first[slot[k]] + fill[slot[k]]++] = k;
    }

    /* one mesh per material, split when it outgrows 16-bit indices */
    for (m = 0; m < materials->len; m++)
    {
        guint material = g_array_index (materials, guint, m);
        guint done;

        for (done = 0; done < counts[m];)
        {
            guint        n = MIN (counts[m] - done, max_cells);
            TerrainMesh *tm = terrain_mesh_new (material, n);
            gfloat      *pos = (gfloat *)(gpointer)tm->positions->data;
            gfloat      *nrm = (gfloat *)(gpointer)tm->normals->data;
            gfloat      *tex = (gfloat *)(gpointer)tm->texcoords->data;
            guint16     *idx = (guint16 *)(gpointer)tm->indices->data;
            guint32     *grid = (guint32 *)(gpointer)tm->grid->data;
            guint        q;

            for (q = 0; q < n; q++)
            {
                guint   c = order[first[m] + done + q];
                guint   ci = c % res;
                guint   cj = c / res;
                guint8  variant = chunk->variants[c];
                guint   gu = positive_mod ((gint64)chunk->cx * res + ci, self->repeat);
                guint   gv = positive_mod ((gint64)chunk->cz * res + cj, self->repeat);
                guint16 base = (guint16)(q * 4);
                guint   corner;

                /* corners: 0 = (i, j), 1 = (i+1, j), 2 = (i, j+1), 3 = (i+1, j+1) */
                for (corner = 0; corner < 4; corner++)
                {
                    guint  gi = ci + (corner & 1u);
                    guint  gj = cj + (corner >> 1);
                    guint  v = q * 4 + corner;
                    guint  g = gj * stride + gi;
                    gfloat st = (gfloat)(corner & 1u);
                    gfloat tt = (gfloat)(corner >> 1);

                    pos[v * 3 + 0] = (gfloat)gi * cell;
                    pos[v * 3 + 1] = chunk->heights[g];
                    pos[v * 3 + 2] = (gfloat)gj * cell;
                    memcpy (&nrm[v * 3], &grid_normals[g * 3], 3 * sizeof (gfloat));
                    grid[v] = g;

                    apply_variant (variant, &st, &tt);
                    tex[v * 2 + 0] = ((gfloat)gu + st) / (gfloat)self->repeat;
                    tex[v * 2 + 1] = ((gfloat)gv + tt) / (gfloat)self->repeat;
                }

                /* (0, 2, 1) and (1, 2, 3): both have normal +Y in raylib's
                 * counter-clockwise convention. */
                idx[q * 6 + 0] = base;
                idx[q * 6 + 1] = (guint16)(base + 2);
                idx[q * 6 + 2] = (guint16)(base + 1);
                idx[q * 6 + 3] = (guint16)(base + 1);
                idx[q * 6 + 4] = (guint16)(base + 2);
                idx[q * 6 + 5] = (guint16)(base + 3);
            }
            /* meshes follow each other inside the chunk's slot */
            tm->first_vertex = vertex_at;
            tm->first_index = index_at;
            vertex_at += n * 4;
            index_at += n * 6;
            g_ptr_array_add (chunk->meshes, tm);
            done += n;
        }
    }
    chunk->mesh_valid = TRUE;
    chunk->normals_stale = FALSE;
}

/* refresh_normals:
 * A neighbour came or went: recompute the grid normals and copy them
 * into every mesh (geometry, order and texture coordinates stand). An
 * uploaded mesh re-sends only its normal buffer at the next upload. */
static void
refresh_normals (LrgTerrain   *self,
                 TerrainChunk *chunk)
{
    g_autofree gfloat *grid_normals = NULL;
    guint              m;

    grid_normals = compute_grid_normals (self, chunk);
    for (m = 0; m < chunk->meshes->len; m++)
    {
        TerrainMesh   *tm = g_ptr_array_index (chunk->meshes, m);
        gfloat        *nrm = (gfloat *)(gpointer)tm->normals->data;
        const guint32 *grid = (const guint32 *)(gpointer)tm->grid->data;
        guint          v;

        for (v = 0; v < tm->grid->len; v++)
            memcpy (&nrm[v * 3], &grid_normals[grid[v] * 3], 3 * sizeof (gfloat));
        if (tm->uploaded)
            tm->normals_dirty = TRUE;
    }
    chunk->normals_stale = FALSE;
}

static TerrainChunk *
ready_chunk (LrgTerrain *self,
             gint        cx,
             gint        cz)
{
    TerrainChunk *chunk = lookup_chunk (self, cx, cz);

    if (chunk != NULL && !chunk->mesh_valid)
        build_chunk_meshes (self, chunk);
    else if (chunk != NULL && chunk->normals_stale)
        refresh_normals (self, chunk);
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
    release_pages (self);
    g_clear_pointer (&self->free_slots, g_array_unref);
    g_clear_pointer (&self->pages, g_array_unref);
    g_clear_pointer (&self->draw_list, g_array_unref);
    g_clear_pointer (&self->draw_blocks, g_array_unref);
    g_clear_pointer (&self->draw_members, g_array_unref);
    g_clear_pointer (&self->draw_visible, g_array_unref);
    g_clear_pointer (&self->textures, g_hash_table_unref);
    g_clear_pointer (&self->assets, g_hash_table_unref);
    g_clear_pointer (&self->chunk_layers, g_hash_table_unref);
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
    self->assets = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
    self->pages = g_array_new (FALSE, FALSE, sizeof (TerrainPage));
    self->free_slots = g_array_new (FALSE, FALSE, sizeof (guint));
    self->draw_list = g_array_new (FALSE, FALSE, sizeof (DrawEntry));
    self->draw_blocks = g_array_new (FALSE, FALSE, sizeof (DrawBlock));
    self->draw_members = g_array_new (FALSE, FALSE, sizeof (guint));
    self->draw_visible = g_array_new (FALSE, FALSE, sizeof (guint8));
    self->chunk_layers = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
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
    chunk->owner = self;
    chunk->slot = -1;
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->key = chunk_key (cx, cz);
    /* a layer set for these coordinates outlives the chunk data */
    chunk->layer = GPOINTER_TO_INT (g_hash_table_lookup (self->chunk_layers, &chunk->key)) - 1;
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
    self->draw_list_valid = FALSE;
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
    self->draw_list_valid = FALSE;
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
    self->draw_list_valid = FALSE;
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

    /* world_to_chunk() leaves its outputs untouched when its argument
     * checks fail, so give them a defined value first. */
    cx = 0;
    cz = 0;
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
    self->draw_list_valid = FALSE;
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
lrg_terrain_set_material (LrgTerrain  *self,
                          guint        material,
                          LrgMaterial *asset)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));
    g_return_if_fail (asset == NULL || LRG_IS_MATERIAL (asset));

    if (asset == NULL)
        g_hash_table_remove (self->assets, GUINT_TO_POINTER (material));
    else
        g_hash_table_replace (self->assets, GUINT_TO_POINTER (material), g_object_ref (asset));
}

LrgMaterial *
lrg_terrain_get_material (LrgTerrain *self,
                          guint       material)
{
    g_return_val_if_fail (LRG_IS_TERRAIN (self), NULL);
    return g_hash_table_lookup (self->assets, GUINT_TO_POINTER (material));
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

/* glDrawElementsBaseVertex() (OpenGL 3.2 core), looked up through raylib's
 * loader as graylib's frame sync does with its sync functions. */
#if defined(_WIN32)
#define TERRAIN_GLAPIENTRY __stdcall
#else
#define TERRAIN_GLAPIENTRY
#endif
typedef void (TERRAIN_GLAPIENTRY *TerrainDrawElementsBaseVertexFn) (guint         mode,
                                                                   gint          count,
                                                                   guint         type,
                                                                   const void   *indices,
                                                                   gint          base_vertex);
#define TERRAIN_GL_TRIANGLES      (0x0004)
#define TERRAIN_GL_UNSIGNED_SHORT (0x1403)

static TerrainDrawElementsBaseVertexFn draw_elements_base_vertex = NULL;

/* choose_path:
 * The shared-page path needs OpenGL 3.3 (VAOs and base-vertex draws);
 * LRG_TERRAIN_DRAW=meshes forces the raylib mesh path (a driver
 * workaround, and the reference the pixel tests compare against). Read
 * once per terrain, at its first upload. */
static void
choose_path (LrgTerrain *self)
{
    const gchar *forced;
    gint         version;

    if (self->path != TERRAIN_PATH_UNDECIDED)
        return;
    self->path = TERRAIN_PATH_MESHES;
    forced = g_getenv ("LRG_TERRAIN_DRAW");
    if (forced != NULL && g_strcmp0 (forced, "meshes") == 0)
        return;
    version = rlGetVersion ();
    if (version != RL_OPENGL_33 && version != RL_OPENGL_43)
        return;
    if (draw_elements_base_vertex == NULL)
        draw_elements_base_vertex =
            (TerrainDrawElementsBaseVertexFn)rlGetProcAddress ("glDrawElementsBaseVertex");
    if (draw_elements_base_vertex != NULL)
        self->path = TERRAIN_PATH_SHARED;
}

/* slot_vertices / slot_indices:
 * A chunk's meshes together hold 4 vertices and 6 indices per cell,
 * whatever its materials, so every slot has the same size. */
static guint
slot_vertices (LrgTerrain *self)
{
    return 4u * self->resolution * self->resolution;
}

static guint
slot_indices (LrgTerrain *self)
{
    return 6u * self->resolution * self->resolution;
}

/* set_attribute_defaults:
 * The constant values UploadMesh() gives the attributes a terrain mesh
 * has no buffer for (generic attribute values are context state, and a
 * shader reading vertexColor sees them); set whenever the shared path
 * uploads, where the mesh path's UploadMesh() set them. */
static void
set_attribute_defaults (void)
{
    static const gfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const gfloat tangent[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const gfloat zero2[2] = { 0.0f, 0.0f };

    rlSetVertexAttributeDefault (RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, white, SHADER_ATTRIB_VEC4, 4);
    rlSetVertexAttributeDefault (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT, tangent, SHADER_ATTRIB_VEC4, 4);
    rlSetVertexAttributeDefault (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2, zero2, SHADER_ATTRIB_VEC2, 2);
}

/* add_page:
 * Allocates one page of slots (no data yet) and its VAO. */
static void
add_page (LrgTerrain *self)
{
    TerrainPage page;
    gsize       vertices = (gsize)slot_vertices (self) * self->slots_per_page;
    gsize       indices = (gsize)slot_indices (self) * self->slots_per_page;

    memset (&page, 0, sizeof page);
    page.vao = rlLoadVertexArray ();
    rlEnableVertexArray (page.vao);
    page.vbo[0] = rlLoadVertexBuffer (NULL, (gint)(vertices * 3 * sizeof (gfloat)), false);
    rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, 3, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION);
    page.vbo[1] = rlLoadVertexBuffer (NULL, (gint)(vertices * 2 * sizeof (gfloat)), false);
    rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD, 2, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD);
    page.vbo[2] = rlLoadVertexBuffer (NULL, (gint)(vertices * 3 * sizeof (gfloat)), false);
    rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, 3, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL);
    page.ebo = rlLoadVertexBufferElement (NULL, (gint)(indices * sizeof (guint16)), false);
    rlDisableVertexArray ();
    rlDisableVertexBuffer ();
    g_array_append_val (self->pages, page);
}

/* release_pages:
 * Frees every page (GPU objects only with a live context) and forgets
 * all slots; the chunks' meshes must already be marked not uploaded. */
static void
release_pages (LrgTerrain *self)
{
    guint i;

    if (self->pages == NULL)
        return;
    if (IsWindowReady ())
    {
        for (i = 0; i < self->pages->len; i++)
        {
            TerrainPage *page = &g_array_index (self->pages, TerrainPage, i);

            rlUnloadVertexArray (page->vao);
            rlUnloadVertexBuffer (page->vbo[0]);
            rlUnloadVertexBuffer (page->vbo[1]);
            rlUnloadVertexBuffer (page->vbo[2]);
            rlUnloadVertexBuffer (page->ebo);
        }
    }
    g_array_set_size (self->pages, 0);
    if (self->free_slots != NULL)
        g_array_set_size (self->free_slots, 0);
    self->slot_count = 0;
}

/* ensure_slot:
 * Gives @chunk a slot: a released one if any, else the next one of the
 * last page, opening a new page when it is full. */
static void
ensure_slot (LrgTerrain   *self,
             TerrainChunk *chunk)
{
    if (chunk->slot >= 0)
        return;
    if (self->free_slots->len > 0)
    {
        chunk->slot = (gint)g_array_index (self->free_slots, guint, self->free_slots->len - 1);
        g_array_set_size (self->free_slots, self->free_slots->len - 1);
        return;
    }
    if (self->slots_per_page == 0)
    {
        gsize slot_bytes = (gsize)slot_vertices (self) * 8 * sizeof (gfloat)
                           + (gsize)slot_indices (self) * sizeof (guint16);

        self->slots_per_page = (guint)CLAMP (TERRAIN_PAGE_BYTES / slot_bytes, 1, 256);
    }
    if (self->slot_count % self->slots_per_page == 0)
        add_page (self);
    chunk->slot = (gint)self->slot_count++;
}

/* write_mesh:
 * Copies @tm into its place inside @chunk's slot. */
static void
write_mesh (LrgTerrain   *self,
            TerrainChunk *chunk,
            TerrainMesh  *tm)
{
    TerrainPage *page;
    guint        local;
    gsize        vertex;
    gsize        index;

    terrain_mesh_unload_gpu (tm);
    ensure_slot (self, chunk);
    page = &g_array_index (self->pages, TerrainPage, (guint)chunk->slot / self->slots_per_page);
    local = (guint)chunk->slot % self->slots_per_page;
    vertex = (gsize)local * slot_vertices (self) + tm->first_vertex;
    index = (gsize)local * slot_indices (self) + tm->first_index;

    /* the element buffer binding belongs to the VAO: bind the page's */
    rlEnableVertexArray (page->vao);
    rlUpdateVertexBuffer (page->vbo[0], tm->positions->data,
                          (gint)(tm->positions->len * sizeof (gfloat)),
                          (gint)(vertex * 3 * sizeof (gfloat)));
    rlUpdateVertexBuffer (page->vbo[1], tm->texcoords->data,
                          (gint)(tm->texcoords->len * sizeof (gfloat)),
                          (gint)(vertex * 2 * sizeof (gfloat)));
    rlUpdateVertexBuffer (page->vbo[2], tm->normals->data,
                          (gint)(tm->normals->len * sizeof (gfloat)),
                          (gint)(vertex * 3 * sizeof (gfloat)));
    rlUpdateVertexBufferElements (page->ebo, tm->indices->data,
                                  (gint)(tm->indices->len * sizeof (guint16)),
                                  (gint)(index * sizeof (guint16)));
    rlDisableVertexArray ();
    rlDisableVertexBuffer ();
    set_attribute_defaults ();
    tm->uploaded = TRUE;
    tm->normals_dirty = FALSE;
}

/* write_normals:
 * Re-sends only @tm's normals into its slot. */
static void
write_normals (LrgTerrain   *self,
               TerrainChunk *chunk,
               TerrainMesh  *tm)
{
    TerrainPage *page = &g_array_index (self->pages, TerrainPage,
                                        (guint)chunk->slot / self->slots_per_page);
    gsize        vertex = (gsize)((guint)chunk->slot % self->slots_per_page) * slot_vertices (self)
                          + tm->first_vertex;

    rlUpdateVertexBuffer (page->vbo[2], tm->normals->data,
                          (gint)(tm->normals->len * sizeof (gfloat)),
                          (gint)(vertex * 3 * sizeof (gfloat)));
    rlDisableVertexBuffer ();
    tm->normals_dirty = FALSE;
}

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

    choose_path (self);
    g_hash_table_iter_init (&iter, self->chunks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        TerrainChunk *chunk = value;
        guint         m;

        if (!chunk->mesh_valid)
            build_chunk_meshes (self, chunk);
        else if (chunk->normals_stale)
            refresh_normals (self, chunk);
        for (m = 0; m < chunk->meshes->len; m++)
        {
            TerrainMesh *tm = g_ptr_array_index (chunk->meshes, m);

            if (self->path == TERRAIN_PATH_SHARED)
            {
                if (!tm->uploaded && tm->indices->len > 0)
                    write_mesh (self, chunk, tm);
                else if (tm->uploaded && tm->normals_dirty)
                    write_normals (self, chunk, tm);
            }
            else if (!tm->uploaded && tm->indices->len > 0)
            {
                upload_mesh (tm);
            }
            else if (tm->uploaded && tm->normals_dirty)
            {
                /* only the normals moved: re-send that buffer */
                UpdateMeshBuffer (tm->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL,
                                  tm->normals->data,
                                  (gint)(tm->normals->len * sizeof (gfloat)), 0);
                tm->normals_dirty = FALSE;
            }
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

        if (!chunk->mesh_valid || chunk->normals_stale || chunk->meshes == NULL)
            return FALSE;
        for (m = 0; m < chunk->meshes->len; m++)
        {
            TerrainMesh *tm = g_ptr_array_index (chunk->meshes, m);

            if (!tm->uploaded || tm->normals_dirty)
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

        chunk->slot = -1;
        if (chunk->meshes == NULL)
            continue;
        for (m = 0; m < chunk->meshes->len; m++)
            terrain_mesh_unload_gpu (g_ptr_array_index (chunk->meshes, m));
    }
    release_pages (self);
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
        LrgMaterial     *asset;
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

        /* a bound LrgMaterial: its uniforms, maps (same units) and state */
        asset = g_hash_table_lookup (self->assets, GUINT_TO_POINTER (tm->material));
        if (asset != NULL)
        {
            Color c;

            lrg_material_apply (asset, material.shader.id);
            lrg_material_fill_raylib_maps (asset, maps);
            c = maps[MATERIAL_MAP_ALBEDO].color;
            maps[MATERIAL_MAP_ALBEDO].color.r = (unsigned char)((c.r * self->tint.r) / 255);
            maps[MATERIAL_MAP_ALBEDO].color.g = (unsigned char)((c.g * self->tint.g) / 255);
            maps[MATERIAL_MAP_ALBEDO].color.b = (unsigned char)((c.b * self->tint.b) / 255);
            maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)((c.a * self->tint.a) / 255);
            lrg_material_apply_state (asset);
            DrawMesh (mesh, material, transform);
            lrg_material_restore_state (asset);
            lrg_material_reset_uniforms (material.shader.id);
            continue;
        }

        DrawMesh (mesh, material, transform);
    }
}

/* ------------------------------------------------------------------------ */
/* Shared-page drawing                                                      */
/* ------------------------------------------------------------------------ */

/* matrix_multiply / matrix_invert / matrix_transpose:
 * raymath's MatrixMultiply(), MatrixInvert() and MatrixTranspose(), the
 * same float operations in the same order, so the uniforms sent below
 * are bit for bit those DrawMesh() sends (raymath.h itself is not
 * gnu89-clean). Fields mK are column-major element K. */
static Matrix
matrix_multiply (Matrix left,
                 Matrix right)
{
    Matrix result;

    result.m0 = left.m0*right.m0 + left.m1*right.m4 + left.m2*right.m8 + left.m3*right.m12;
    result.m1 = left.m0*right.m1 + left.m1*right.m5 + left.m2*right.m9 + left.m3*right.m13;
    result.m2 = left.m0*right.m2 + left.m1*right.m6 + left.m2*right.m10 + left.m3*right.m14;
    result.m3 = left.m0*right.m3 + left.m1*right.m7 + left.m2*right.m11 + left.m3*right.m15;
    result.m4 = left.m4*right.m0 + left.m5*right.m4 + left.m6*right.m8 + left.m7*right.m12;
    result.m5 = left.m4*right.m1 + left.m5*right.m5 + left.m6*right.m9 + left.m7*right.m13;
    result.m6 = left.m4*right.m2 + left.m5*right.m6 + left.m6*right.m10 + left.m7*right.m14;
    result.m7 = left.m4*right.m3 + left.m5*right.m7 + left.m6*right.m11 + left.m7*right.m15;
    result.m8 = left.m8*right.m0 + left.m9*right.m4 + left.m10*right.m8 + left.m11*right.m12;
    result.m9 = left.m8*right.m1 + left.m9*right.m5 + left.m10*right.m9 + left.m11*right.m13;
    result.m10 = left.m8*right.m2 + left.m9*right.m6 + left.m10*right.m10 + left.m11*right.m14;
    result.m11 = left.m8*right.m3 + left.m9*right.m7 + left.m10*right.m11 + left.m11*right.m15;
    result.m12 = left.m12*right.m0 + left.m13*right.m4 + left.m14*right.m8 + left.m15*right.m12;
    result.m13 = left.m12*right.m1 + left.m13*right.m5 + left.m14*right.m9 + left.m15*right.m13;
    result.m14 = left.m12*right.m2 + left.m13*right.m6 + left.m14*right.m10 + left.m15*right.m14;
    result.m15 = left.m12*right.m3 + left.m13*right.m7 + left.m14*right.m11 + left.m15*right.m15;
    return result;
}

static Matrix
matrix_invert (Matrix mat)
{
    Matrix result;
    gfloat a00 = mat.m0, a01 = mat.m1, a02 = mat.m2, a03 = mat.m3;
    gfloat a10 = mat.m4, a11 = mat.m5, a12 = mat.m6, a13 = mat.m7;
    gfloat a20 = mat.m8, a21 = mat.m9, a22 = mat.m10, a23 = mat.m11;
    gfloat a30 = mat.m12, a31 = mat.m13, a32 = mat.m14, a33 = mat.m15;
    gfloat b00 = a00*a11 - a01*a10;
    gfloat b01 = a00*a12 - a02*a10;
    gfloat b02 = a00*a13 - a03*a10;
    gfloat b03 = a01*a12 - a02*a11;
    gfloat b04 = a01*a13 - a03*a11;
    gfloat b05 = a02*a13 - a03*a12;
    gfloat b06 = a20*a31 - a21*a30;
    gfloat b07 = a20*a32 - a22*a30;
    gfloat b08 = a20*a33 - a23*a30;
    gfloat b09 = a21*a32 - a22*a31;
    gfloat b10 = a21*a33 - a23*a31;
    gfloat b11 = a22*a33 - a23*a32;
    gfloat inv_det = 1.0f/(b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06);

    result.m0 = (a11*b11 - a12*b10 + a13*b09)*inv_det;
    result.m1 = (-a01*b11 + a02*b10 - a03*b09)*inv_det;
    result.m2 = (a31*b05 - a32*b04 + a33*b03)*inv_det;
    result.m3 = (-a21*b05 + a22*b04 - a23*b03)*inv_det;
    result.m4 = (-a10*b11 + a12*b08 - a13*b07)*inv_det;
    result.m5 = (a00*b11 - a02*b08 + a03*b07)*inv_det;
    result.m6 = (-a30*b05 + a32*b02 - a33*b01)*inv_det;
    result.m7 = (a20*b05 - a22*b02 + a23*b01)*inv_det;
    result.m8 = (a10*b10 - a11*b08 + a13*b06)*inv_det;
    result.m9 = (-a00*b10 + a01*b08 - a03*b06)*inv_det;
    result.m10 = (a30*b04 - a31*b02 + a33*b00)*inv_det;
    result.m11 = (-a20*b04 + a21*b02 - a23*b00)*inv_det;
    result.m12 = (-a10*b09 + a11*b07 - a12*b06)*inv_det;
    result.m13 = (a00*b09 - a01*b07 + a02*b06)*inv_det;
    result.m14 = (-a30*b03 + a31*b01 - a32*b00)*inv_det;
    result.m15 = (a20*b03 - a21*b01 + a22*b00)*inv_det;
    return result;
}

static Matrix
matrix_transpose (Matrix mat)
{
    Matrix result;

    result.m0 = mat.m0;   result.m1 = mat.m4;   result.m2 = mat.m8;   result.m3 = mat.m12;
    result.m4 = mat.m1;   result.m5 = mat.m5;   result.m6 = mat.m9;   result.m7 = mat.m13;
    result.m8 = mat.m2;   result.m9 = mat.m6;   result.m10 = mat.m10; result.m11 = mat.m14;
    result.m12 = mat.m3;  result.m13 = mat.m7;  result.m14 = mat.m11; result.m15 = mat.m15;
    return result;
}

/* The state one shared-path draw() keeps across its meshes. */
typedef struct
{
    gboolean begun;       /* program bound, per-pass uniforms sent */
    gboolean skip;        /* the shader has no locations: draw nothing */
    Shader   shader;
    Matrix   view;        /* rlgl's modelview (the camera) */
    Matrix   projection;
    Matrix   extra;       /* rlgl's push/pop transform */
    gboolean stereo;
    guint    page;        /* bound page, G_MAXUINT for none */
    guint    texture;     /* texture bound on slot 0 */
    gboolean textured;    /* a texture was bound by this draw */
} SharedDraw;

/* chunk_transform:
 * The model matrix of @chunk: a translation to (corner - origin). */
static Matrix
chunk_transform (LrgTerrain   *self,
                 TerrainChunk *chunk)
{
    Matrix transform;

    memset (&transform, 0, sizeof transform);
    transform.m0 = 1.0f;
    transform.m5 = 1.0f;
    transform.m10 = 1.0f;
    transform.m15 = 1.0f;
    transform.m12 = (gfloat)((gdouble)chunk->cx * (gdouble)self->chunk_size - self->origin[0]);
    transform.m13 = (gfloat)(-self->origin[1]);
    transform.m14 = (gfloat)((gdouble)chunk->cz * (gdouble)self->chunk_size - self->origin[2]);
    return transform;
}

/* terrain_shader:
 * The terrain shader, or raylib's default. */
static Shader
terrain_shader (LrgTerrain *self)
{
    Shader shader;

    if (self->shader != NULL)
        return *(Shader *)grl_shader_get_handle (self->shader);
    memset (&shader, 0, sizeof shader);
    shader.id = rlGetShaderIdDefault ();
    shader.locs = rlGetShaderLocsDefault ();
    return shader;
}

/* shared_begin:
 * What DrawMesh() sends before every mesh but that is the same for all
 * of them: the program, colDiffuse (the tint), colSpecular (unset, so
 * zero), view and projection. Deferred to the first mesh drawn, so a
 * fully culled pass touches no GL state, as before. */
static void
shared_begin (LrgTerrain *self,
              SharedDraw *d)
{
    const gint *locs;

    d->begun = TRUE;
    d->shader = terrain_shader (self);
    rlEnableShader (d->shader.id);
    locs = d->shader.locs;
    if (locs == NULL)
    {
        d->skip = TRUE;
        return;
    }
    if (locs[SHADER_LOC_COLOR_DIFFUSE] != -1)
    {
        gfloat values[4];

        values[0] = (gfloat)self->tint.r/255.0f;
        values[1] = (gfloat)self->tint.g/255.0f;
        values[2] = (gfloat)self->tint.b/255.0f;
        values[3] = (gfloat)self->tint.a/255.0f;
        rlSetUniform (locs[SHADER_LOC_COLOR_DIFFUSE], values, SHADER_UNIFORM_VEC4, 1);
    }
    if (locs[SHADER_LOC_COLOR_SPECULAR] != -1)
    {
        static const gfloat zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        rlSetUniform (locs[SHADER_LOC_COLOR_SPECULAR], zero, SHADER_UNIFORM_VEC4, 1);
    }
    d->view = rlGetMatrixModelview ();
    d->projection = rlGetMatrixProjection ();
    d->extra = rlGetMatrixTransform ();
    if (locs[SHADER_LOC_MATRIX_VIEW] != -1)
        rlSetUniformMatrix (locs[SHADER_LOC_MATRIX_VIEW], d->view);
    if (locs[SHADER_LOC_MATRIX_PROJECTION] != -1)
        rlSetUniformMatrix (locs[SHADER_LOC_MATRIX_PROJECTION], d->projection);
    d->stereo = rlIsStereoRenderEnabled ();
    d->page = G_MAXUINT;
}

/* shared_set_color:
 * colDiffuse = @rgb times the terrain tint (what shared_begin() sets for
 * plain meshes when @rgb is white). */
static void
shared_set_color (LrgTerrain   *self,
                  SharedDraw   *d,
                  const gfloat *rgba)
{
    gfloat values[4];

    if (d->shader.locs[SHADER_LOC_COLOR_DIFFUSE] == -1)
        return;
    values[0] = rgba[0] * (gfloat)self->tint.r / 255.0f;
    values[1] = rgba[1] * (gfloat)self->tint.g / 255.0f;
    values[2] = rgba[2] * (gfloat)self->tint.b / 255.0f;
    values[3] = rgba[3] * (gfloat)self->tint.a / 255.0f;
    rlSetUniform (d->shader.locs[SHADER_LOC_COLOR_DIFFUSE], values, SHADER_UNIFORM_VEC4, 1);
}

/* shared_material_begin:
 * Binds an LrgMaterial for one shared-page mesh: its render state, then
 * its uniforms and maps (unit 0 included) and its colour times the tint.
 * lrg_material_apply_state() flushes the rlgl batch, which may unbind
 * the program and the vertex array: the state goes first, the program is
 * enabled again by lrg_material_apply() and the page is rebound. */
static void
shared_material_begin (LrgTerrain  *self,
                       SharedDraw  *d,
                       LrgMaterial *asset)
{
    gfloat color[4];

    lrg_material_apply_state (asset);
    lrg_material_apply (asset, d->shader.id);
    rlEnableShader (d->shader.id);
    lrg_material_get_base_color (asset, color);
    shared_set_color (self, d, color);
    d->textured = FALSE;
    d->texture = 0;
    d->page = G_MAXUINT;
}

/* shared_material_end:
 * Back to the plain-mesh state: default state and uniforms, the tint as
 * colDiffuse; slot 0 is rebound by the next plain mesh. */
static void
shared_material_end (LrgTerrain  *self,
                     SharedDraw  *d,
                     LrgMaterial *asset)
{
    static const gfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    lrg_material_restore_state (asset);
    lrg_material_reset_uniforms (d->shader.id);
    rlEnableShader (d->shader.id);
    shared_set_color (self, d, white);
    d->page = G_MAXUINT;
}

/* shared_chunk:
 * Draws @chunk's uploaded meshes: the chunk's model, normal and MVP
 * matrices (computed as DrawMesh() does), then per mesh its texture
 * (only when it changes), its page (only when it changes) and one
 * base-vertex draw of its index range. */
static void
shared_chunk (LrgTerrain   *self,
              SharedDraw   *d,
              TerrainChunk *chunk)
{
    const gint *locs;
    Matrix      mvp;
    Matrix      model_view;
    gboolean    matrices = FALSE;
    guint       m;

    if (chunk->meshes == NULL || chunk->slot < 0)
        return;
    memset (&mvp, 0, sizeof mvp);
    memset (&model_view, 0, sizeof model_view);
    for (m = 0; m < chunk->meshes->len; m++)
    {
        TerrainMesh     *tm = g_ptr_array_index (chunk->meshes, m);
        MaterialTexture *entry;
        LrgMaterial     *asset;
        guint            texture;
        guint            page;
        gsize            index;
        gint             base;
        gint             eye;

        if (!tm->uploaded)
            continue;
        if (!d->begun)
            shared_begin (self, d);
        if (d->skip)
            return;
        locs = d->shader.locs;

        /* the chunk's matrices, once for all its meshes */
        if (!matrices)
        {
            Matrix model = matrix_multiply (chunk_transform (self, chunk), d->extra);

            if (locs[SHADER_LOC_MATRIX_MODEL] != -1)
                rlSetUniformMatrix (locs[SHADER_LOC_MATRIX_MODEL], model);
            model_view = matrix_multiply (model, d->view);
            if (locs[SHADER_LOC_MATRIX_NORMAL] != -1)
                rlSetUniformMatrix (locs[SHADER_LOC_MATRIX_NORMAL],
                                    matrix_transpose (matrix_invert (model)));
            if (!d->stereo)
            {
                mvp = matrix_multiply (model_view, d->projection);
                rlSetUniformMatrix (locs[SHADER_LOC_MATRIX_MVP], mvp);
            }
            matrices = TRUE;
        }

        /* a bound LrgMaterial binds its own maps and uniforms (unit 0
         * included: force a rebind for the next plain mesh) */
        asset = g_hash_table_lookup (self->assets, GUINT_TO_POINTER (tm->material));
        if (asset != NULL)
            shared_material_begin (self, d, asset);

        /* the albedo map on slot 0 (raylib's white texture without one) */
        entry = g_hash_table_lookup (self->textures, GUINT_TO_POINTER (tm->material));
        texture = entry != NULL ? entry->texture.id : rlGetTextureIdDefault ();
        if (asset != NULL)
        {
            /* lrg_material_apply() bound the material's albedo */
        }
        else if (texture == 0)
        {
            /* DrawMesh() binds nothing for a zero id: what is bound then
             * is what the previous mesh's unbinding left, nothing */
            if (d->textured && d->texture > 0)
            {
                rlActiveTextureSlot (0);
                rlDisableTexture ();
            }
            d->texture = 0;
        }
        else if (!d->textured || texture != d->texture)
        {
            gint slot = 0;

            rlActiveTextureSlot (0);
            rlEnableTexture (texture);
            rlSetUniform (locs[SHADER_LOC_MAP_DIFFUSE], &slot, SHADER_UNIFORM_INT, 1);
            d->texture = texture;
        }
        d->textured = TRUE;

        page = (guint)chunk->slot / self->slots_per_page;
        if (page != d->page)
        {
            rlEnableVertexArray (g_array_index (self->pages, TerrainPage, page).vao);
            d->page = page;
        }
        base = (gint)(((guint)chunk->slot % self->slots_per_page) * slot_vertices (self)
                      + tm->first_vertex);
        index = (gsize)((guint)chunk->slot % self->slots_per_page) * slot_indices (self)
                + tm->first_index;

        if (!d->stereo)
        {
            draw_elements_base_vertex (TERRAIN_GL_TRIANGLES, (gint)tm->indices->len,
                                       TERRAIN_GL_UNSIGNED_SHORT,
                                       (const void *)(index * sizeof (guint16)), base);
            if (asset != NULL)
                shared_material_end (self, d, asset);
            continue;
        }
        for (eye = 0; eye < 2; eye++)
        {
            rlViewport (eye * rlGetFramebufferWidth () / 2, 0, rlGetFramebufferWidth () / 2,
                        rlGetFramebufferHeight ());
            mvp = matrix_multiply (matrix_multiply (model_view, rlGetMatrixViewOffsetStereo (eye)),
                                   rlGetMatrixProjectionStereo (eye));
            rlSetUniformMatrix (locs[SHADER_LOC_MATRIX_MVP], mvp);
            draw_elements_base_vertex (TERRAIN_GL_TRIANGLES, (gint)tm->indices->len,
                                       TERRAIN_GL_UNSIGNED_SHORT,
                                       (const void *)(index * sizeof (guint16)), base);
        }
        if (asset != NULL)
            shared_material_end (self, d, asset);
    }
}

/* shared_end:
 * Leaves the state DrawMesh() leaves: no texture on slot 0, no VAO or
 * buffers, no program, rlgl's matrices as they were. */
static void
shared_end (SharedDraw *d)
{
    if (!d->begun || d->skip)
        return;
    if (d->textured && d->texture > 0)
    {
        rlActiveTextureSlot (0);
        rlDisableTexture ();
    }
    rlDisableVertexArray ();
    rlDisableVertexBuffer ();
    rlDisableVertexBufferElement ();
    rlDisableShader ();
    rlSetMatrixModelview (d->view);
    rlSetMatrixProjection (d->projection);
}

/* build_blocks:
 * Groups the draw list into blocks of 2^TERRAIN_BLOCK_SHIFT chunks a
 * side by chunk coordinate; a chunk without a finite height range gets
 * no part in its block's box (it is never culled anyway). */
static void
build_blocks (LrgTerrain *self)
{
    g_autoptr(GHashTable) block_of = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    g_autofree guint     *block_index = NULL;
    g_autofree guint     *fill = NULL;
    guint                 i;
    guint                 b;

    g_array_set_size (self->draw_blocks, 0);
    block_index = g_new (guint, MAX (self->draw_list->len, 1));
    for (i = 0; i < self->draw_list->len; i++)
    {
        const DrawEntry *entry = &g_array_index (self->draw_list, DrawEntry, i);
        gint64           key = chunk_key (entry->chunk->cx >> TERRAIN_BLOCK_SHIFT,
                                          entry->chunk->cz >> TERRAIN_BLOCK_SHIFT);
        gpointer         found;
        DrawBlock       *block;
        guint            k;

        if (g_hash_table_lookup_extended (block_of, &key, NULL, &found))
            b = GPOINTER_TO_UINT (found);
        else
        {
            DrawBlock fresh;

            memset (&fresh, 0, sizeof fresh);
            for (k = 0; k < 3; k++)
            {
                fresh.lo[k] = G_MAXFLOAT;
                fresh.hi[k] = -G_MAXFLOAT;
            }
            b = self->draw_blocks->len;
            g_array_append_val (self->draw_blocks, fresh);
            g_hash_table_insert (block_of, g_memdup2 (&key, sizeof key), GUINT_TO_POINTER (b));
        }
        block = &g_array_index (self->draw_blocks, DrawBlock, b);
        block->count++;
        block_index[i] = b;
        if (entry->always)
            continue;
        for (k = 0; k < 3; k++)
        {
            block->lo[k] = MIN (block->lo[k], entry->lo[k]);
            block->hi[k] = MAX (block->hi[k], entry->hi[k]);
        }
    }

    /* members grouped by block, in draw order inside each */
    fill = g_new0 (guint, MAX (self->draw_blocks->len, 1));
    for (b = 1; b < self->draw_blocks->len; b++)
    {
        DrawBlock *prev = &g_array_index (self->draw_blocks, DrawBlock, b - 1);

        g_array_index (self->draw_blocks, DrawBlock, b).first = prev->first + prev->count;
    }
    g_array_set_size (self->draw_members, self->draw_list->len);
    for (i = 0; i < self->draw_list->len; i++)
    {
        DrawBlock *block = &g_array_index (self->draw_blocks, DrawBlock, block_index[i]);

        g_array_index (self->draw_members, guint, block->first + fill[block_index[i]]++) = i;
    }
}

/* ensure_draw_list:
 * The chunks in the table's iteration order (the order draw() has
 * always used) with their draw-space boxes: the chunk-local box (the
 * heights' range over the chunk's square) plus the translation
 * draw_chunk() uses as the model matrix, added in float exactly as
 * lrg_draw_cull_frustum_box_visible_offset() would. Rebuilt after a
 * chunk is added, replaced or removed or the origin moves. */
static void
ensure_draw_list (LrgTerrain *self)
{
    GHashTableIter iter;
    gpointer       value;

    if (self->draw_list_valid)
        return;
    g_array_set_size (self->draw_list, 0);
    g_hash_table_iter_init (&iter, self->chunks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        TerrainChunk *chunk = value;
        DrawEntry     entry;
        gfloat        offset[3];

        offset[0] = (gfloat)((gdouble)chunk->cx * (gdouble)self->chunk_size - self->origin[0]);
        offset[1] = (gfloat)(-self->origin[1]);
        offset[2] = (gfloat)((gdouble)chunk->cz * (gdouble)self->chunk_size - self->origin[2]);
        entry.lo[0] = 0.0f + offset[0];
        entry.lo[1] = chunk->min_y + offset[1];
        entry.lo[2] = 0.0f + offset[2];
        entry.hi[0] = self->chunk_size + offset[0];
        entry.hi[1] = chunk->max_y + offset[1];
        entry.hi[2] = self->chunk_size + offset[2];
        entry.always = chunk->min_y > chunk->max_y;
        entry.chunk = chunk;
        g_array_append_val (self->draw_list, entry);
    }
    build_blocks (self);
    g_array_set_size (self->draw_visible, self->draw_list->len);
    self->draw_list_valid = TRUE;
}

/* cull_draw_list:
 * Marks in draw_visible the entries that can land inside @frustum. A
 * block whose united box is certainly outside rejects all its chunks at
 * once (each of their boxes lies inside it, so each is outside too);
 * the chunks of the other blocks are tested one by one. Culling only
 * ever skips chunks that draw no pixel, so the output is unchanged. */
static void
cull_draw_list (LrgTerrain           *self,
                const LrgDrawFrustum *frustum)
{
    const DrawEntry *entries = (const DrawEntry *)(gconstpointer)self->draw_list->data;
    const guint     *members = (const guint *)(gconstpointer)self->draw_members->data;
    guint8          *visible = (guint8 *)self->draw_visible->data;
    guint            b;

    for (b = 0; b < self->draw_blocks->len; b++)
    {
        const DrawBlock *block = &g_array_index (self->draw_blocks, DrawBlock, b);
        gboolean         seen = lrg_draw_cull_frustum_box_visible (frustum, block->lo, block->hi);
        guint            k;

        for (k = block->first; k < block->first + block->count; k++)
        {
            const DrawEntry *entry = &entries[members[k]];

            visible[members[k]] = entry->always
                || (seen && lrg_draw_cull_frustum_box_visible (frustum, entry->lo, entry->hi));
        }
    }
}

/* chunk_layer_visible:
 * Whether @chunk's render layer (its override, else the terrain's) is
 * in @mask, the active cull mask. */
static inline gboolean
chunk_layer_visible (LrgTerrain         *self,
                     const TerrainChunk *chunk,
                     guint32             mask)
{
    guint layer = chunk->layer >= 0 ? (guint)chunk->layer : self->layer;

    return (mask & LRG_RENDER_LAYERS_BIT (layer)) != 0;
}

void
lrg_terrain_set_layer (LrgTerrain *self,
                       guint       layer)
{
    g_return_if_fail (LRG_IS_TERRAIN (self));
    g_return_if_fail (layer < LRG_RENDER_LAYERS_COUNT);
    self->layer = layer;
}

guint
lrg_terrain_get_layer (LrgTerrain *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN (self), 0);
    return self->layer;
}

void
lrg_terrain_set_chunk_layer (LrgTerrain *self,
                             gint        cx,
                             gint        cz,
                             gint        layer)
{
    TerrainChunk *chunk;
    gint64        key;

    g_return_if_fail (LRG_IS_TERRAIN (self));
    g_return_if_fail (layer >= -1 && layer < LRG_RENDER_LAYERS_COUNT);

    /* Remember the override by coordinates (so set_chunk() replacing
     * the data keeps it) and apply it to the loaded chunk, if any. */
    key = chunk_key (cx, cz);
    if (layer < 0)
        g_hash_table_remove (self->chunk_layers, &key);
    else
        g_hash_table_insert (self->chunk_layers, g_memdup2 (&key, sizeof key),
                             GINT_TO_POINTER (layer + 1));
    chunk = lookup_chunk (self, cx, cz);
    if (chunk != NULL)
        chunk->layer = layer;
}

guint
lrg_terrain_get_chunk_layer (LrgTerrain *self,
                             gint        cx,
                             gint        cz)
{
    gint64   key;
    gpointer value;

    g_return_val_if_fail (LRG_IS_TERRAIN (self), 0);

    key = chunk_key (cx, cz);
    value = g_hash_table_lookup (self->chunk_layers, &key);
    return value != NULL ? (guint)(GPOINTER_TO_INT (value) - 1) : self->layer;
}

void
lrg_terrain_draw (LrgTerrain *self)
{
    LrgDrawFrustum   frustum;
    gboolean         cull;
    SharedDraw       shared;
    const DrawEntry *entries;
    guint32          mask;
    gboolean         filter;
    guint            i;

    g_return_if_fail (LRG_IS_TERRAIN (self));

    /* Render layers: with every layer active (the common case) no
     * chunk is tested; otherwise chunks whose layer is outside the
     * active cull mask are skipped like culled ones. */
    mask = lrg_render_layers_get_active_mask ();
    filter = mask != LRG_RENDER_LAYERS_MASK_ALL;

    /* chunks wholly outside the live rlgl frustum (the camera's, or a
     * shadow pass's light) draw no pixel: skip them */
    cull = lrg_draw_cull_frustum_current (&frustum);
    ensure_draw_list (self);
    entries = (const DrawEntry *)(gconstpointer)self->draw_list->data;
    memset (&shared, 0, sizeof shared);
    if (self->path == TERRAIN_PATH_SHARED)
    {
        if (cull)
            cull_draw_list (self, &frustum);
        for (i = 0; i < self->draw_list->len; i++)
            if ((!cull || g_array_index (self->draw_visible, guint8, i))
                && (!filter || chunk_layer_visible (self, entries[i].chunk, mask)))
                shared_chunk (self, &shared, entries[i].chunk);
        shared_end (&shared);
        return;
    }

    /* the raylib mesh path tests every chunk on its own (it is the
     * reference the block culling above is checked against) */
    for (i = 0; i < self->draw_list->len; i++)
    {
        const DrawEntry *entry = &entries[i];

        if (cull && !entry->always
            && !lrg_draw_cull_frustum_box_visible (&frustum, entry->lo, entry->hi))
            continue;
        if (filter && !chunk_layer_visible (self, entry->chunk, mask))
            continue;
        draw_chunk (self, entry->chunk);
    }
}

void
lrg_terrain_draw_chunk (LrgTerrain *self,
                        gint        cx,
                        gint        cz)
{
    TerrainChunk *chunk;

    g_return_if_fail (LRG_IS_TERRAIN (self));

    chunk = lookup_chunk (self, cx, cz);
    if (chunk == NULL
        || !chunk_layer_visible (self, chunk, lrg_render_layers_get_active_mask ()))
        return;
    if (self->path == TERRAIN_PATH_SHARED)
    {
        SharedDraw shared;

        memset (&shared, 0, sizeof shared);
        shared_chunk (self, &shared, chunk);
        shared_end (&shared);
    }
    else
        draw_chunk (self, chunk);
}
