/* lrg-static-batch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Static mesh batching. Meshes are transformed on the CPU and appended to
 * per-(material, layer) chunks limited to 16-bit indices. Chunks keep
 * their CPU arrays; upload() hands raylib borrowed pointers, uploads, and
 * immediately clears them from the raylib Mesh so UnloadMesh() only ever
 * releases GPU objects.
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
#include "graphics/lrg-static-batch.h"

/* Transforms with a smaller |determinant| are treated as singular. */
#define BATCH_SINGULAR_EPSILON (1e-12)
/* raylib binds up to MAX_MATERIAL_MAPS (12) maps; keep headroom. */
#define BATCH_MATERIAL_MAPS    (16)

typedef struct
{
    guint    material;
    guint    layer;
    GArray  *positions;   /* gfloat x3 */
    GArray  *normals;     /* gfloat x3 */
    GArray  *texcoords;   /* gfloat x2 */
    GArray  *colors;      /* guint8 x4 */
    GArray  *indices;     /* guint16 */
    gfloat   min[3];
    gfloat   max[3];
    Mesh     mesh;        /* GPU handles only */
    gboolean uploaded;
    gboolean dirty;
} Chunk;

typedef struct
{
    GrlTexture *owned;    /* nullable: set_material_texture() reference */
    Texture2D   texture;
} MaterialTexture;

struct _LrgStaticBatch
{
    GObject     parent_instance;

    GPtrArray  *chunks;          /* Chunk* */
    guint       max_vertices;
    GHashTable *textures;        /* GUINT_TO_POINTER (material) -> MaterialTexture* */
    Color       tints[LRG_STATIC_BATCH_MAX_LAYERS];
    GrlShader  *shader;
};

G_DEFINE_TYPE (LrgStaticBatch, lrg_static_batch, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------ */
/* Chunks                                                                   */
/* ------------------------------------------------------------------------ */

/* chunk_unload_gpu:
 * Releases the chunk's GPU objects. Without a context only the CPU-side
 * VBO id array can be released. */
static void
chunk_unload_gpu (Chunk *chunk)
{
    if (!chunk->uploaded)
        return;
    if (IsWindowReady ())
        UnloadMesh (chunk->mesh);
    else
        RL_FREE (chunk->mesh.vboId);
    memset (&chunk->mesh, 0, sizeof chunk->mesh);
    chunk->uploaded = FALSE;
    chunk->dirty = TRUE;
}

static void
chunk_free (gpointer data)
{
    Chunk *chunk = data;

    chunk_unload_gpu (chunk);
    g_array_unref (chunk->positions);
    g_array_unref (chunk->normals);
    g_array_unref (chunk->texcoords);
    g_array_unref (chunk->colors);
    g_array_unref (chunk->indices);
    g_free (chunk);
}

static Chunk *
chunk_new (LrgStaticBatch *self,
           guint           material,
           guint           layer)
{
    Chunk *chunk = g_new0 (Chunk, 1);

    chunk->material = material;
    chunk->layer = layer;
    chunk->positions = g_array_new (FALSE, FALSE, sizeof (gfloat));
    chunk->normals = g_array_new (FALSE, FALSE, sizeof (gfloat));
    chunk->texcoords = g_array_new (FALSE, FALSE, sizeof (gfloat));
    chunk->colors = g_array_new (FALSE, FALSE, sizeof (guint8));
    chunk->indices = g_array_new (FALSE, FALSE, sizeof (guint16));
    chunk->min[0] = chunk->min[1] = chunk->min[2] = G_MAXFLOAT;
    chunk->max[0] = chunk->max[1] = chunk->max[2] = -G_MAXFLOAT;
    chunk->dirty = TRUE;
    g_ptr_array_add (self->chunks, chunk);
    return chunk;
}

static guint
chunk_vertex_count (const Chunk *chunk)
{
    return chunk->positions->len / 3;
}

/* open_chunk:
 * The most recent chunk for (material, layer), or NULL. */
static Chunk *
open_chunk (LrgStaticBatch *self,
            guint           material,
            guint           layer)
{
    guint i;

    for (i = self->chunks->len; i > 0; i--)
    {
        Chunk *chunk = g_ptr_array_index (self->chunks, i - 1);

        if (chunk->material == material && chunk->layer == layer)
            return chunk;
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Transform                                                                */
/* ------------------------------------------------------------------------ */

/* Prepared transform: the 4x4 in raylib field form plus the normal matrix
 * (cofactor of the upper 3x3, sign-corrected, i.e. proportional to the
 * inverse transpose) and whether it mirrors. */
typedef struct
{
    Matrix   m;
    gdouble  normal[3][3];
    gboolean mirror;
} BatchTransform;

static Matrix
matrix_identity (void)
{
    Matrix m;

    memset (&m, 0, sizeof m);
    m.m0 = m.m5 = m.m10 = m.m15 = 1.0f;
    return m;
}

static Matrix
matrix_from_grl (const GrlMatrix *matrix)
{
    Matrix m;

    if (matrix == NULL)
        return matrix_identity ();
    m.m0 = matrix->m0;   m.m4 = matrix->m4;   m.m8 = matrix->m8;   m.m12 = matrix->m12;
    m.m1 = matrix->m1;   m.m5 = matrix->m5;   m.m9 = matrix->m9;   m.m13 = matrix->m13;
    m.m2 = matrix->m2;   m.m6 = matrix->m6;   m.m10 = matrix->m10; m.m14 = matrix->m14;
    m.m3 = matrix->m3;   m.m7 = matrix->m7;   m.m11 = matrix->m11; m.m15 = matrix->m15;
    return m;
}

/* matrix_multiply:
 * raymath MatrixMultiply(left, right): @left applies first. Fields mK
 * are column-major element K. */
static Matrix
matrix_multiply (Matrix left,
                 Matrix right)
{
    const gfloat *l = &left.m0;
    const gfloat *r = &right.m0;
    gfloat        la[16], ra[16], out[16];
    Matrix        result;
    gint          c, row, k;
    static const gint field_of[16] = {
        /* storage position of mK inside Matrix (row-major field order) */
        0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15
    };
    gfloat       *res = &result.m0;

    for (k = 0; k < 16; k++)
    {
        la[k] = l[field_of[k]];
        ra[k] = r[field_of[k]];
    }
    for (c = 0; c < 4; c++)
        for (row = 0; row < 4; row++)
        {
            gfloat sum = 0.0f;

            for (k = 0; k < 4; k++)
                sum += la[c * 4 + k] * ra[k * 4 + row];
            out[c * 4 + row] = sum;
        }
    for (k = 0; k < 16; k++)
        res[field_of[k]] = out[k];
    return result;
}

/* prepare_transform:
 * Validates @m and derives the normal matrix. Fails for non-finite or
 * singular matrices. */
static gboolean
prepare_transform (Matrix          m,
                   BatchTransform *out,
                   GError        **error)
{
    gdouble a = m.m0, b = m.m4, c = m.m8;
    gdouble d = m.m1, e = m.m5, f = m.m9;
    gdouble g = m.m2, h = m.m6, i = m.m10;
    gdouble det;
    gdouble sign;
    const gfloat *fields = &m.m0;
    gint    k;

    for (k = 0; k < 16; k++)
    {
        if (!isfinite (fields[k]))
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Batch transform is not finite");
            return FALSE;
        }
    }
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!isfinite (det) || fabs (det) < BATCH_SINGULAR_EPSILON)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Batch transform is singular");
        return FALSE;
    }

    /* Cofactor matrix = det * inverse transpose. Multiplying by sign(det)
     * keeps normals pointing outward under mirroring. */
    sign = det < 0 ? -1.0 : 1.0;
    out->normal[0][0] = sign * (e * i - f * h);
    out->normal[0][1] = sign * -(d * i - f * g);
    out->normal[0][2] = sign * (d * h - e * g);
    out->normal[1][0] = sign * -(b * i - c * h);
    out->normal[1][1] = sign * (a * i - c * g);
    out->normal[1][2] = sign * -(a * h - b * g);
    out->normal[2][0] = sign * (b * f - c * e);
    out->normal[2][1] = sign * -(a * f - c * d);
    out->normal[2][2] = sign * (a * e - b * d);
    out->m = m;
    out->mirror = det < 0;
    return TRUE;
}

static void
transform_position (const BatchTransform *t,
                    const gfloat         *in,
                    gfloat               *out)
{
    const Matrix *m = &t->m;

    out[0] = m->m0 * in[0] + m->m4 * in[1] + m->m8 * in[2] + m->m12;
    out[1] = m->m1 * in[0] + m->m5 * in[1] + m->m9 * in[2] + m->m13;
    out[2] = m->m2 * in[0] + m->m6 * in[1] + m->m10 * in[2] + m->m14;
}

/* transform_normal:
 * Normal matrix times @in, normalized; degenerate normals become +Y. The
 * cofactor rows correspond to output components. */
static void
transform_normal (const BatchTransform *t,
                  const gfloat         *in,
                  gfloat               *out)
{
    gdouble n[3];
    gdouble length;
    gint    r;

    for (r = 0; r < 3; r++)
        n[r] = t->normal[r][0] * in[0] + t->normal[r][1] * in[1] + t->normal[r][2] * in[2];
    length = sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!isfinite (length) || length < 1e-20)
    {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
        return;
    }
    out[0] = (gfloat)(n[0] / length);
    out[1] = (gfloat)(n[1] / length);
    out[2] = (gfloat)(n[2] / length);
}

/* ------------------------------------------------------------------------ */
/* Merge                                                                    */
/* ------------------------------------------------------------------------ */

/*
 * Source:
 *
 * One mesh being merged, already transformed into world space.
 */
typedef struct
{
    const gfloat  *positions;   /* world, 3 per vertex */
    const gfloat  *normals;     /* world, 3 per vertex */
    const gfloat  *texcoords;   /* nullable */
    const guint8  *colors;      /* nullable */
} Source;

/* append_vertex:
 * Copies source vertex @v into @chunk and grows its bounds. */
static void
append_vertex (Chunk        *chunk,
               const Source *source,
               guint         v)
{
    static const gfloat zero_uv[2] = { 0.0f, 0.0f };
    static const guint8 white[4] = { 255, 255, 255, 255 };
    const gfloat       *p = source->positions + v * 3;
    gint                k;

    g_array_append_vals (chunk->positions, p, 3);
    g_array_append_vals (chunk->normals, source->normals + v * 3, 3);
    g_array_append_vals (chunk->texcoords, source->texcoords != NULL ? source->texcoords + v * 2 : zero_uv, 2);
    g_array_append_vals (chunk->colors, source->colors != NULL ? source->colors + v * 4 : white, 4);
    for (k = 0; k < 3; k++)
    {
        chunk->min[k] = MIN (chunk->min[k], p[k]);
        chunk->max[k] = MAX (chunk->max[k], p[k]);
    }
    chunk->dirty = TRUE;
}

static void
append_triangle (Chunk    *chunk,
                 guint16   i0,
                 guint16   i1,
                 guint16   i2,
                 gboolean  mirror)
{
    guint16 tri[3];

    tri[0] = i0;
    tri[1] = mirror ? i2 : i1;
    tri[2] = mirror ? i1 : i2;
    g_array_append_vals (chunk->indices, tri, 3);
}

static guint32
source_index (const guint32 *indices,
              guint          position)
{
    return indices != NULL ? indices[position] : position;
}

/* merge:
 * Bulk-appends the whole mesh when it fits in one chunk; otherwise walks
 * triangles, remapping vertices per chunk (duplicating the ones shared
 * across a split) and opening a new chunk whenever the next triangle's
 * new vertices would overflow the limit. */
static void
merge (LrgStaticBatch *self,
       const Source   *source,
       guint           n_vertices,
       const guint32  *indices,
       guint           n_indices,
       gboolean        mirror,
       guint           material,
       guint           layer)
{
    Chunk   *chunk = open_chunk (self, material, layer);
    guint    t;
    guint    n_triangles = n_indices / 3;

    if (n_vertices <= self->max_vertices)
    {
        guint base;
        guint v;

        if (chunk == NULL || chunk_vertex_count (chunk) + n_vertices > self->max_vertices)
            chunk = chunk_new (self, material, layer);
        base = chunk_vertex_count (chunk);
        for (v = 0; v < n_vertices; v++)
            append_vertex (chunk, source, v);
        for (t = 0; t < n_triangles; t++)
            append_triangle (chunk,
                             (guint16)(base + source_index (indices, t * 3)),
                             (guint16)(base + source_index (indices, t * 3 + 1)),
                             (guint16)(base + source_index (indices, t * 3 + 2)),
                             mirror);
        return;
    }

    {
        g_autofree guint32 *remap = g_new (guint32, n_vertices);
        g_autofree guint32 *stamp = g_new0 (guint32, n_vertices);
        guint32             generation = 1;

        if (chunk == NULL)
            chunk = chunk_new (self, material, layer);

        for (t = 0; t < n_triangles; t++)
        {
            guint32 v[3];
            guint   needed = 0;
            gint    k;

            for (k = 0; k < 3; k++)
            {
                gint j;
                gboolean repeat = FALSE;

                v[k] = source_index (indices, t * 3 + (guint)k);
                for (j = 0; j < k; j++)
                    repeat = repeat || v[j] == v[k];
                if (stamp[v[k]] != generation && !repeat)
                    needed++;
            }
            if (chunk_vertex_count (chunk) + needed > self->max_vertices)
            {
                chunk = chunk_new (self, material, layer);
                generation++;
            }
            for (k = 0; k < 3; k++)
            {
                if (stamp[v[k]] != generation)
                {
                    stamp[v[k]] = generation;
                    remap[v[k]] = chunk_vertex_count (chunk);
                    append_vertex (chunk, source, v[k]);
                }
            }
            append_triangle (chunk, (guint16)remap[v[0]], (guint16)remap[v[1]],
                             (guint16)remap[v[2]], mirror);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* GObject                                                                  */
/* ------------------------------------------------------------------------ */

static void
material_texture_free (gpointer data)
{
    MaterialTexture *entry = data;

    g_clear_object (&entry->owned);
    g_free (entry);
}

static void
lrg_static_batch_finalize (GObject *object)
{
    LrgStaticBatch *self = LRG_STATIC_BATCH (object);

    g_clear_pointer (&self->chunks, g_ptr_array_unref);
    g_clear_pointer (&self->textures, g_hash_table_unref);
    g_clear_object (&self->shader);

    G_OBJECT_CLASS (lrg_static_batch_parent_class)->finalize (object);
}

static void
lrg_static_batch_class_init (LrgStaticBatchClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_static_batch_finalize;
}

static void
lrg_static_batch_init (LrgStaticBatch *self)
{
    guint i;

    self->chunks = g_ptr_array_new_with_free_func (chunk_free);
    self->textures = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                            material_texture_free);
    self->max_vertices = LRG_STATIC_BATCH_MAX_VERTICES;
    for (i = 0; i < LRG_STATIC_BATCH_MAX_LAYERS; i++)
        self->tints[i] = (Color) { 255, 255, 255, 255 };
}

LrgStaticBatch *
lrg_static_batch_new (void)
{
    return g_object_new (LRG_TYPE_STATIC_BATCH, NULL);
}

void
lrg_static_batch_set_max_chunk_vertices (LrgStaticBatch *self,
                                         guint           max_vertices)
{
    g_return_if_fail (LRG_IS_STATIC_BATCH (self));

    self->max_vertices = CLAMP (max_vertices, 3u, (guint)LRG_STATIC_BATCH_MAX_VERTICES);
}

guint
lrg_static_batch_get_max_chunk_vertices (LrgStaticBatch *self)
{
    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    return self->max_vertices;
}

/* ------------------------------------------------------------------------ */
/* Adding meshes                                                            */
/* ------------------------------------------------------------------------ */

/* add_mesh_matrix:
 * Shared validation and merge for every add_* entry point. */
static gboolean
add_mesh_matrix (LrgStaticBatch  *self,
                 const gfloat    *positions,
                 const gfloat    *normals,
                 const gfloat    *texcoords,
                 const guint8    *colors,
                 guint            n_vertices,
                 const guint32   *indices,
                 guint            n_indices,
                 Matrix           matrix,
                 guint            material,
                 guint            layer,
                 GError         **error)
{
    BatchTransform      transform;
    g_autofree gfloat  *world_positions = NULL;
    g_autofree gfloat  *world_normals = NULL;
    Source              source;
    guint               v;
    guint               k;

    if (layer >= LRG_STATIC_BATCH_MAX_LAYERS)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Batch layer %u is out of range (0..31)", layer);
        return FALSE;
    }
    if (n_vertices == 0)
        return TRUE;
    if (positions == NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Batch mesh has no positions");
        return FALSE;
    }
    if (indices == NULL)
        n_indices = n_vertices;
    if (n_indices % 3 != 0)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Batch mesh index count %u is not a multiple of 3", n_indices);
        return FALSE;
    }
    for (k = 0; indices != NULL && k < n_indices; k++)
    {
        if (indices[k] >= n_vertices)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Batch mesh index %u (%u) is out of range", k, indices[k]);
            return FALSE;
        }
    }
    for (k = 0; k < n_vertices * 3; k++)
    {
        if (!isfinite (positions[k]))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Batch mesh vertex %u is not finite", k / 3);
            return FALSE;
        }
    }
    if (!prepare_transform (matrix, &transform, error))
        return FALSE;

    /* Transform once, merge afterwards (split meshes revisit vertices). */
    world_positions = g_new (gfloat, (gsize)n_vertices * 3);
    world_normals = g_new (gfloat, (gsize)n_vertices * 3);
    for (v = 0; v < n_vertices; v++)
    {
        static const gfloat up[3] = { 0.0f, 1.0f, 0.0f };

        transform_position (&transform, positions + v * 3, world_positions + v * 3);
        transform_normal (&transform, normals != NULL ? normals + v * 3 : up, world_normals + v * 3);
    }

    source.positions = world_positions;
    source.normals = world_normals;
    source.texcoords = texcoords;
    source.colors = colors;
    merge (self, &source, n_vertices, indices, n_indices, transform.mirror, material, layer);
    return TRUE;
}

gboolean
lrg_static_batch_add_mesh (LrgStaticBatch  *self,
                           const gfloat    *positions,
                           const gfloat    *normals,
                           const gfloat    *texcoords,
                           const guint8    *colors,
                           guint            n_vertices,
                           const guint32   *indices,
                           guint            n_indices,
                           const GrlMatrix *transform,
                           guint            material,
                           guint            layer,
                           GError         **error)
{
    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return add_mesh_matrix (self, positions, normals, texcoords, colors, n_vertices,
                            indices, n_indices, matrix_from_grl (transform),
                            material, layer, error);
}

/* add_raylib_mesh:
 * Widens a raylib Mesh's 16-bit indices and forwards its CPU arrays. */
static gboolean
add_raylib_mesh (LrgStaticBatch  *self,
                 const Mesh      *mesh,
                 Matrix           matrix,
                 guint            material,
                 guint            layer,
                 GError         **error)
{
    g_autofree guint32 *indices = NULL;
    guint               n_indices = 0;
    guint               k;

    if (mesh->vertexCount > 0 && mesh->vertices == NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Mesh has no CPU-side vertex data");
        return FALSE;
    }
    if (mesh->indices != NULL)
    {
        n_indices = (guint)mesh->triangleCount * 3;
        indices = g_new (guint32, MAX (1u, n_indices));
        for (k = 0; k < n_indices; k++)
            indices[k] = mesh->indices[k];
    }
    return add_mesh_matrix (self, mesh->vertices, mesh->normals, mesh->texcoords,
                            mesh->colors, (guint)MAX (0, mesh->vertexCount),
                            indices, n_indices, matrix, material, layer, error);
}

gboolean
lrg_static_batch_add_grl_mesh (LrgStaticBatch  *self,
                               GrlMesh         *mesh,
                               const GrlMatrix *transform,
                               guint            material,
                               guint            layer,
                               GError         **error)
{
    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), FALSE);
    g_return_val_if_fail (GRL_IS_MESH (mesh), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return add_raylib_mesh (self, grl_mesh_get_handle (mesh), matrix_from_grl (transform),
                            material, layer, error);
}

gboolean
lrg_static_batch_add_model (LrgStaticBatch  *self,
                            GrlModel        *model,
                            const GrlMatrix *transform,
                            guint            layer,
                            GError         **error)
{
    Model  *raw;
    Matrix  matrix;
    gint    i;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), FALSE);
    g_return_val_if_fail (GRL_IS_MODEL (model), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!grl_model_is_valid (model))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Model is not valid");
        return FALSE;
    }
    raw = grl_model_get_handle (model);
    matrix = matrix_multiply (raw->transform, matrix_from_grl (transform));

    for (i = 0; i < raw->meshCount; i++)
    {
        Texture2D texture = { 0, 0, 0, 0, 0 };
        guint     material;

        if (raw->materials != NULL && raw->meshMaterial != NULL &&
            raw->materials[raw->meshMaterial[i]].maps != NULL)
            texture = raw->materials[raw->meshMaterial[i]].maps[MATERIAL_MAP_ALBEDO].texture;
        material = texture.id;

        /* Remember the borrowed texture unless the caller set one. */
        if (material != 0 && !g_hash_table_contains (self->textures, GUINT_TO_POINTER (material)))
        {
            MaterialTexture *entry = g_new0 (MaterialTexture, 1);

            entry->texture = texture;
            g_hash_table_insert (self->textures, GUINT_TO_POINTER (material), entry);
        }
        if (!add_raylib_mesh (self, &raw->meshes[i], matrix, material, layer, error))
            return FALSE;
    }
    return TRUE;
}

void
lrg_static_batch_clear (LrgStaticBatch *self)
{
    g_return_if_fail (LRG_IS_STATIC_BATCH (self));

    g_ptr_array_set_size (self->chunks, 0);
}

/* ------------------------------------------------------------------------ */
/* Inspection                                                               */
/* ------------------------------------------------------------------------ */

static Chunk *
chunk_at (LrgStaticBatch *self,
          guint           chunk)
{
    return chunk < self->chunks->len ? g_ptr_array_index (self->chunks, chunk) : NULL;
}

guint
lrg_static_batch_get_chunk_count (LrgStaticBatch *self)
{
    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    return self->chunks->len;
}

guint
lrg_static_batch_get_vertex_count (LrgStaticBatch *self)
{
    guint total = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    for (i = 0; i < self->chunks->len; i++)
        total += chunk_vertex_count (g_ptr_array_index (self->chunks, i));
    return total;
}

guint
lrg_static_batch_get_triangle_count (LrgStaticBatch *self)
{
    guint total = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    for (i = 0; i < self->chunks->len; i++)
        total += ((Chunk *)g_ptr_array_index (self->chunks, i))->indices->len / 3;
    return total;
}

guint
lrg_static_batch_get_chunk_material (LrgStaticBatch *self,
                                     guint           chunk)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    entry = chunk_at (self, chunk);
    return entry != NULL ? entry->material : 0;
}

guint
lrg_static_batch_get_chunk_layer (LrgStaticBatch *self,
                                  guint           chunk)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    entry = chunk_at (self, chunk);
    return entry != NULL ? entry->layer : 0;
}

guint
lrg_static_batch_get_chunk_vertex_count (LrgStaticBatch *self,
                                         guint           chunk)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    entry = chunk_at (self, chunk);
    return entry != NULL ? chunk_vertex_count (entry) : 0;
}

guint
lrg_static_batch_get_chunk_index_count (LrgStaticBatch *self,
                                        guint           chunk)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), 0);

    entry = chunk_at (self, chunk);
    return entry != NULL ? entry->indices->len : 0;
}

gboolean
lrg_static_batch_get_chunk_bounds (LrgStaticBatch *self,
                                   guint           chunk,
                                   gfloat         *min_x,
                                   gfloat         *min_y,
                                   gfloat         *min_z,
                                   gfloat         *max_x,
                                   gfloat         *max_y,
                                   gfloat         *max_z)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), FALSE);

    entry = chunk_at (self, chunk);
    if (entry == NULL)
        return FALSE;
    if (min_x != NULL) *min_x = entry->min[0];
    if (min_y != NULL) *min_y = entry->min[1];
    if (min_z != NULL) *min_z = entry->min[2];
    if (max_x != NULL) *max_x = entry->max[0];
    if (max_y != NULL) *max_y = entry->max[1];
    if (max_z != NULL) *max_z = entry->max[2];
    return TRUE;
}

const gfloat *
lrg_static_batch_get_chunk_positions (LrgStaticBatch *self,
                                      guint           chunk,
                                      guint          *n_floats)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), NULL);
    g_return_val_if_fail (n_floats != NULL, NULL);

    entry = chunk_at (self, chunk);
    *n_floats = entry != NULL ? entry->positions->len : 0;
    return entry != NULL ? (const gfloat *)entry->positions->data : NULL;
}

const gfloat *
lrg_static_batch_get_chunk_normals (LrgStaticBatch *self,
                                    guint           chunk,
                                    guint          *n_floats)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), NULL);
    g_return_val_if_fail (n_floats != NULL, NULL);

    entry = chunk_at (self, chunk);
    *n_floats = entry != NULL ? entry->normals->len : 0;
    return entry != NULL ? (const gfloat *)entry->normals->data : NULL;
}

const guint8 *
lrg_static_batch_get_chunk_colors (LrgStaticBatch *self,
                                   guint           chunk,
                                   guint          *n_bytes)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), NULL);
    g_return_val_if_fail (n_bytes != NULL, NULL);

    entry = chunk_at (self, chunk);
    *n_bytes = entry != NULL ? entry->colors->len : 0;
    return entry != NULL ? (const guint8 *)entry->colors->data : NULL;
}

const guint16 *
lrg_static_batch_get_chunk_indices (LrgStaticBatch *self,
                                    guint           chunk,
                                    guint          *n_indices)
{
    Chunk *entry;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), NULL);
    g_return_val_if_fail (n_indices != NULL, NULL);

    entry = chunk_at (self, chunk);
    *n_indices = entry != NULL ? entry->indices->len : 0;
    return entry != NULL ? (const guint16 *)entry->indices->data : NULL;
}

/* ------------------------------------------------------------------------ */
/* Appearance                                                               */
/* ------------------------------------------------------------------------ */

void
lrg_static_batch_set_material_texture (LrgStaticBatch *self,
                                       guint           material,
                                       GrlTexture     *texture)
{
    MaterialTexture *entry;

    g_return_if_fail (LRG_IS_STATIC_BATCH (self));
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
lrg_static_batch_set_layer_tint (LrgStaticBatch *self,
                                 guint           layer,
                                 const GrlColor *tint)
{
    g_return_if_fail (LRG_IS_STATIC_BATCH (self));
    g_return_if_fail (layer < LRG_STATIC_BATCH_MAX_LAYERS);

    if (tint == NULL)
        self->tints[layer] = (Color) { 255, 255, 255, 255 };
    else
        self->tints[layer] = (Color) { tint->r, tint->g, tint->b, tint->a };
}

void
lrg_static_batch_set_shader (LrgStaticBatch *self,
                             GrlShader      *shader)
{
    g_return_if_fail (LRG_IS_STATIC_BATCH (self));
    g_return_if_fail (shader == NULL || GRL_IS_SHADER (shader));

    g_set_object (&self->shader, shader);
}

/* ------------------------------------------------------------------------ */
/* GPU                                                                      */
/* ------------------------------------------------------------------------ */

gboolean
lrg_static_batch_upload (LrgStaticBatch  *self,
                         GError         **error)
{
    guint i;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!IsWindowReady ())
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                             "A graphics context is required to upload a static batch");
        return FALSE;
    }

    for (i = 0; i < self->chunks->len; i++)
    {
        Chunk *chunk = g_ptr_array_index (self->chunks, i);
        Mesh   mesh;

        if (chunk->uploaded && !chunk->dirty)
            continue;
        chunk_unload_gpu (chunk);

        /* raylib reads the borrowed CPU arrays during UploadMesh() only. */
        memset (&mesh, 0, sizeof mesh);
        mesh.vertexCount = (gint)chunk_vertex_count (chunk);
        mesh.triangleCount = (gint)(chunk->indices->len / 3);
        mesh.vertices = (gfloat *)chunk->positions->data;
        mesh.normals = (gfloat *)chunk->normals->data;
        mesh.texcoords = (gfloat *)chunk->texcoords->data;
        mesh.colors = (unsigned char *)chunk->colors->data;
        mesh.indices = (unsigned short *)chunk->indices->data;
        UploadMesh (&mesh, false);

        mesh.vertices = NULL;
        mesh.normals = NULL;
        mesh.texcoords = NULL;
        mesh.colors = NULL;
        mesh.indices = NULL;
        chunk->mesh = mesh;
        chunk->uploaded = TRUE;
        chunk->dirty = FALSE;
    }
    return TRUE;
}

gboolean
lrg_static_batch_is_uploaded (LrgStaticBatch *self)
{
    guint i;

    g_return_val_if_fail (LRG_IS_STATIC_BATCH (self), FALSE);

    for (i = 0; i < self->chunks->len; i++)
    {
        Chunk *chunk = g_ptr_array_index (self->chunks, i);

        if (!chunk->uploaded || chunk->dirty)
            return FALSE;
    }
    return TRUE;
}

void
lrg_static_batch_unload (LrgStaticBatch *self)
{
    guint i;

    g_return_if_fail (LRG_IS_STATIC_BATCH (self));

    for (i = 0; i < self->chunks->len; i++)
        chunk_unload_gpu (g_ptr_array_index (self->chunks, i));
}

/* draw_one:
 * DrawMesh() with a stack material: batch shader (or raylib's default),
 * the material texture (or raylib's white texture) and the layer tint. */
static void
draw_one (LrgStaticBatch *self,
          Chunk          *chunk)
{
    MaterialMap      maps[BATCH_MATERIAL_MAPS];
    Material         material;
    MaterialTexture *entry;

    if (!chunk->uploaded || chunk->indices->len == 0)
        return;

    memset (maps, 0, sizeof maps);
    memset (&material, 0, sizeof material);
    if (self->shader != NULL)
        material.shader = *(Shader *)grl_shader_get_handle (self->shader);
    else
    {
        material.shader.id = rlGetShaderIdDefault ();
        material.shader.locs = rlGetShaderLocsDefault ();
    }
    entry = g_hash_table_lookup (self->textures, GUINT_TO_POINTER (chunk->material));
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
    maps[MATERIAL_MAP_ALBEDO].color = self->tints[chunk->layer];
    material.maps = maps;

    DrawMesh (chunk->mesh, material, matrix_identity ());
}

void
lrg_static_batch_draw (LrgStaticBatch *self,
                       guint32         layer_mask)
{
    guint i;

    g_return_if_fail (LRG_IS_STATIC_BATCH (self));

    for (i = 0; i < self->chunks->len; i++)
    {
        Chunk *chunk = g_ptr_array_index (self->chunks, i);

        if ((layer_mask >> chunk->layer) & 1u)
            draw_one (self, chunk);
    }
}

void
lrg_static_batch_draw_chunk (LrgStaticBatch *self,
                             guint           chunk)
{
    Chunk *entry;

    g_return_if_fail (LRG_IS_STATIC_BATCH (self));

    entry = chunk_at (self, chunk);
    if (entry != NULL)
        draw_one (self, entry);
}
