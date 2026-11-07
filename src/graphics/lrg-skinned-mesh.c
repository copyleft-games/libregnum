/* lrg-skinned-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Skinned meshes with a bone matrix palette. The CPU keeps the bind
 * geometry, four renormalised influences per vertex, the inverse bind
 * matrices and the current palette (all column-major). upload() hands
 * raylib borrowed arrays for the usual attributes (then clears them so
 * UnloadMesh() frees GPU objects only) and adds the bone index and
 * weight buffers to the mesh's vertex array at raylib's bone attribute
 * locations itself, because raylib's own GPU skinning path is compiled
 * out (SUPPORT_GPU_SKINNING 0). draw() sets the palette uniform on the
 * shader and calls DrawMesh(). Meshes with more bones than the palette
 * holds, or a context without vertex arrays, are skinned on the CPU into
 * a dynamic position and normal buffer instead.
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
#include "graphics/lrg-skinned-mesh.h"
#include "animation/lrg-skeleton.h"
#include "animation/lrg-bone.h"
#include "animation/lrg-bone-pose.h"
#include "core/lrg-prewarm.h"

/* raylib binds up to MAX_MATERIAL_MAPS (12) maps; keep headroom. */
#define SKIN_MATERIAL_MAPS (16)
#define SKIN_MAX_VERTICES  (65535u)

struct _LrgSkinnedMesh
{
    GObject     parent_instance;

    guint       n_bones;
    gfloat     *inverse_bind;   /* 16 per bone */
    gfloat     *palette;        /* 16 per bone */
    gfloat     *normal_palette; /* inverse-transpose linear transforms */
    gboolean    palette_dirty;  /* CPU mode: buffers need re-skinning */

    guint       n_vertices;
    gfloat     *positions;      /* 3 per vertex */
    gfloat     *normals;        /* 3 per vertex, nullable */
    gfloat     *texcoords;      /* 2 per vertex, nullable */
    guint8     *colors;         /* 4 per vertex, nullable */
    guint8     *bone_indices;   /* 4 per vertex */
    gfloat     *bone_weights;   /* 4 per vertex */
    guint       n_indices;
    guint16    *indices;

    GrlTexture *texture;
    LrgMaterial *material;      /* nullable, wins over texture/tint/cutoff */
    GrlShader  *shader;
    Color       tint;
    gfloat      alpha_cutoff;

    gboolean    uploaded;
    gboolean    gpu;            /* skinned by the shader (else CPU) */
    gboolean    holds_shader;   /* holds a reference on the built-in shader */
    Mesh        mesh;
    guint       bone_index_vbo;
    guint       bone_weight_vbo;
    gfloat     *scratch_positions;
    gfloat     *scratch_normals;

    /* uniform locations of the program drawn last, looked up once per
     * program (loc_shader_id 0: not looked up yet) */
    guint       loc_shader_id;
    gint        loc_use_skinning;
    gint        loc_alpha_cutoff;
    gint        loc_bone_matrices;

    /* morph targets: blended on the CPU before skinning */
    LrgMorphTargets *morph;          /* nullable */
    gfloat          *morph_weights;  /* one per target */
    gfloat          *morphed_positions;
    gfloat          *morphed_normals;
    gboolean         morph_dirty;    /* morphed_* need recomputing */
    gboolean         morph_gpu_dirty;/* GPU vertex buffers need the morphed data */
};

static void lrg_skinned_mesh_prewarmable_init (LrgPrewarmableInterface *iface);
static void morph_clear (LrgSkinnedMesh *self);
static void morph_touch (LrgSkinnedMesh *self);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgSkinnedMesh, lrg_skinned_mesh, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                      lrg_skinned_mesh_prewarmable_init))

/* ------------------------------------------------------------------------ */
/* The built-in shader                                                      */
/* ------------------------------------------------------------------------ */

static const gchar skin_vertex_source[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "in vec4 vertexBoneIndices;\n"
    "in vec4 vertexBoneWeights;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 boneMatrices[64];\n"
    "uniform int useSkinning;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragNormal;\n"
    "vec3 affineNormal(mat4 transform, vec3 normal)\n"
    "{\n"
    "    mat3 linear = mat3(transform);\n"
    "    mat3 cofactors = mat3(cross(linear[1], linear[2]), cross(linear[2], linear[0]), cross(linear[0], linear[1]));\n"
    "    float determinant = dot(linear[0], cofactors[0]);\n"
    "    return abs(determinant) > 1e-20 ? (cofactors * normal) / determinant : vec3(0.0);\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    mat4 skin = mat4(1.0);\n"
    "    if (useSkinning != 0)\n"
    "        skin = vertexBoneWeights.x * boneMatrices[int(vertexBoneIndices.x)]\n"
    "             + vertexBoneWeights.y * boneMatrices[int(vertexBoneIndices.y)]\n"
    "             + vertexBoneWeights.z * boneMatrices[int(vertexBoneIndices.z)]\n"
    "             + vertexBoneWeights.w * boneMatrices[int(vertexBoneIndices.w)];\n"
    "    vec4 position = skin * vec4(vertexPosition, 1.0);\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    vec3 normal = vertexNormal;\n"
    "    if (useSkinning != 0)\n"
    "        normal = vertexBoneWeights.x * affineNormal(boneMatrices[int(vertexBoneIndices.x)], vertexNormal)\n"
    "               + vertexBoneWeights.y * affineNormal(boneMatrices[int(vertexBoneIndices.y)], vertexNormal)\n"
    "               + vertexBoneWeights.z * affineNormal(boneMatrices[int(vertexBoneIndices.z)], vertexNormal)\n"
    "               + vertexBoneWeights.w * affineNormal(boneMatrices[int(vertexBoneIndices.w)], vertexNormal);\n"
    "    fragNormal = affineNormal(matModel, normal);\n"
    "    gl_Position = mvp * position;\n"
    "}\n";

static const gchar skin_fragment_source[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragNormal;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float alphaCutoff;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    float light = 1.0;\n"
    "    if (texel.a < alphaCutoff)\n"
    "        discard;\n"
    "    if (length(fragNormal) > 0.0001)\n"
    "        light = 0.6 + 0.4 * max(dot(normalize(fragNormal), normalize(vec3(0.3, 1.0, 0.4))), 0.0);\n"
    "    finalColor = vec4(texel.rgb * light, texel.a);\n"
    "}\n";

/* One built-in program per process, loaded by the first upload and
 * released when the last mesh holding it unloads. */
static Shader builtin_shader;
static guint  builtin_users = 0;

static gboolean
builtin_shader_acquire (void)
{
    if (builtin_users == 0)
    {
        builtin_shader = LoadShaderFromMemory (skin_vertex_source, skin_fragment_source);
        if (builtin_shader.id == 0 || builtin_shader.id == rlGetShaderIdDefault ())
        {
            memset (&builtin_shader, 0, sizeof builtin_shader);
            return FALSE;
        }
    }
    builtin_users++;
    return TRUE;
}

static void
builtin_shader_release (void)
{
    if (builtin_users == 0)
        return;
    builtin_users--;
    if (builtin_users == 0 && builtin_shader.id != 0)
    {
        if (IsWindowReady ())
            UnloadShader (builtin_shader);
        memset (&builtin_shader, 0, sizeof builtin_shader);
    }
}

const gchar *
lrg_skinned_mesh_get_vertex_shader_source (void)
{
    return skin_vertex_source;
}

/* ------------------------------------------------------------------------ */
/* Matrix helpers (column-major, m[column * 4 + row])                       */
/* ------------------------------------------------------------------------ */

static void
mat4_identity (gfloat *m)
{
    memset (m, 0, 16 * sizeof (gfloat));
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

/* out = a * b; out may not alias a or b */
static void
mat4_multiply (const gfloat *a,
               const gfloat *b,
               gfloat       *out)
{
    guint c;
    guint r;

    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
            out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                             a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
}

/* raylib's Matrix names its fields by column-major index (m12 is the X
 * translation) although they are laid out row by row in memory. */
static Matrix
to_raylib_matrix (const gfloat *m)
{
    Matrix out;

    out.m0 = m[0];   out.m1 = m[1];   out.m2 = m[2];   out.m3 = m[3];
    out.m4 = m[4];   out.m5 = m[5];   out.m6 = m[6];   out.m7 = m[7];
    out.m8 = m[8];   out.m9 = m[9];   out.m10 = m[10]; out.m11 = m[11];
    out.m12 = m[12]; out.m13 = m[13]; out.m14 = m[14]; out.m15 = m[15];
    return out;
}

/* ------------------------------------------------------------------------ */
/* GObject                                                                  */
/* ------------------------------------------------------------------------ */

static void
free_geometry (LrgSkinnedMesh *self)
{
    g_clear_pointer (&self->positions, g_free);
    g_clear_pointer (&self->normals, g_free);
    g_clear_pointer (&self->texcoords, g_free);
    g_clear_pointer (&self->colors, g_free);
    g_clear_pointer (&self->bone_indices, g_free);
    g_clear_pointer (&self->bone_weights, g_free);
    g_clear_pointer (&self->indices, g_free);
    g_clear_pointer (&self->scratch_positions, g_free);
    g_clear_pointer (&self->scratch_normals, g_free);
    self->n_vertices = 0;
    self->n_indices = 0;
}

static void
lrg_skinned_mesh_finalize (GObject *object)
{
    LrgSkinnedMesh *self = LRG_SKINNED_MESH (object);

    lrg_skinned_mesh_unload (self);
    free_geometry (self);
    g_free (self->inverse_bind);
    g_free (self->palette);
    g_free (self->normal_palette);
    g_clear_object (&self->texture);
    g_clear_object (&self->material);
    g_clear_object (&self->shader);
    g_clear_object (&self->morph);
    g_free (self->morph_weights);
    g_free (self->morphed_positions);
    g_free (self->morphed_normals);

    G_OBJECT_CLASS (lrg_skinned_mesh_parent_class)->finalize (object);
}

static void
lrg_skinned_mesh_class_init (LrgSkinnedMeshClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_skinned_mesh_finalize;
}

static void
lrg_skinned_mesh_init (LrgSkinnedMesh *self)
{
    self->tint.r = 255;
    self->tint.g = 255;
    self->tint.b = 255;
    self->tint.a = 255;
    self->alpha_cutoff = 0.0f;
    memset (&self->mesh, 0, sizeof self->mesh);

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgSkinnedMesh *
lrg_skinned_mesh_new (guint n_bones)
{
    LrgSkinnedMesh *self;
    guint           i;

    if (n_bones == 0 || n_bones > 255)
        return NULL;

    self = g_object_new (LRG_TYPE_SKINNED_MESH, NULL);
    self->n_bones = n_bones;
    self->inverse_bind = g_new (gfloat, 16 * n_bones);
    self->palette = g_new (gfloat, 16 * n_bones);
    self->normal_palette = g_new (gfloat, 16 * n_bones);
    for (i = 0; i < n_bones; i++)
    {
        mat4_identity (self->inverse_bind + 16 * i);
        mat4_identity (self->palette + 16 * i);
        mat4_identity (self->normal_palette + 16 * i);
    }
    self->palette_dirty = TRUE;
    return self;
}

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

gboolean
lrg_skinned_mesh_set_geometry (LrgSkinnedMesh  *self,
                               guint            n_vertices,
                               const gfloat    *positions,
                               const gfloat    *normals,
                               const gfloat    *texcoords,
                               const guint8    *colors,
                               const guint8    *bone_indices,
                               const gfloat    *bone_weights,
                               guint            n_indices,
                               const guint32   *indices,
                               GError         **error)
{
    g_autofree gfloat  *weights = NULL;
    g_autofree guint16 *small = NULL;
    guint               v;
    guint               k;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* validate everything before touching the mesh */
    if (n_vertices == 0 || n_vertices > SKIN_MAX_VERTICES || n_indices == 0 ||
        n_indices % 3 != 0 || positions == NULL || bone_indices == NULL ||
        bone_weights == NULL || indices == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Skinned mesh needs 1 .. %u vertices, a triangle list and four influences per vertex",
                     SKIN_MAX_VERTICES);
        return FALSE;
    }
    for (k = 0; k < n_indices; k++)
    {
        if (indices[k] >= n_vertices)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "Skinned mesh index %u names vertex %u of %u", k, indices[k], n_vertices);
            return FALSE;
        }
    }

    /* renormalise the influences; a vertex without weight follows bone 0 */
    weights = g_new (gfloat, 4 * n_vertices);
    for (v = 0; v < n_vertices; v++)
    {
        gfloat sum = 0.0f;

        for (k = 0; k < LRG_SKINNED_MESH_INFLUENCES; k++)
        {
            gfloat w = bone_weights[v * 4 + k];

            if (!isfinite (w) || w < 0.0f)
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Skinned mesh vertex %u has a bad weight", v);
                return FALSE;
            }
            if (w > 0.0f && bone_indices[v * 4 + k] >= self->n_bones)
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Skinned mesh vertex %u names bone %u of %u",
                             v, (guint)bone_indices[v * 4 + k], self->n_bones);
                return FALSE;
            }
            sum += w;
        }
        for (k = 0; k < LRG_SKINNED_MESH_INFLUENCES; k++)
        {
            if (sum > 0.0f)
                weights[v * 4 + k] = bone_weights[v * 4 + k] / sum;
            else
                weights[v * 4 + k] = k == 0 ? 1.0f : 0.0f;
        }
    }
    small = g_new (guint16, n_indices);
    for (k = 0; k < n_indices; k++)
        small[k] = (guint16)indices[k];

    lrg_skinned_mesh_unload (self);
    /* morph targets cover a vertex count; a new count invalidates them */
    if (self->morph != NULL && n_vertices != self->n_vertices)
        morph_clear (self);
    free_geometry (self);
    self->n_vertices = n_vertices;
    self->positions = g_memdup2 (positions, 3 * n_vertices * sizeof (gfloat));
    self->normals = normals != NULL ? g_memdup2 (normals, 3 * n_vertices * sizeof (gfloat)) : NULL;
    self->texcoords = texcoords != NULL ? g_memdup2 (texcoords, 2 * n_vertices * sizeof (gfloat)) : NULL;
    self->colors = colors != NULL ? g_memdup2 (colors, 4 * n_vertices) : NULL;
    self->bone_indices = g_new (guint8, 4 * n_vertices);
    /* an unweighted slot may carry any index: clamp it so the shader
     * never reads past the palette */
    for (v = 0; v < 4 * n_vertices; v++)
        self->bone_indices[v] = weights[v] > 0.0f ? bone_indices[v] : 0;
    self->bone_weights = g_steal_pointer (&weights);
    self->n_indices = n_indices;
    self->indices = g_steal_pointer (&small);
    self->palette_dirty = TRUE;
    if (self->morph != NULL)
    {
        g_clear_pointer (&self->morphed_positions, g_free);
        g_clear_pointer (&self->morphed_normals, g_free);
        morph_touch (self);
    }
    return TRUE;
}

guint
lrg_skinned_mesh_get_bone_count (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), 0);
    return self->n_bones;
}

guint
lrg_skinned_mesh_get_vertex_count (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), 0);
    return self->n_vertices;
}

guint
lrg_skinned_mesh_get_triangle_count (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), 0);
    return self->n_indices / 3;
}

const gfloat *
lrg_skinned_mesh_get_bind_positions (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);
    return self->positions;
}

const gfloat *
lrg_skinned_mesh_get_weights (LrgSkinnedMesh  *self,
                              const guint8   **out_bone_indices)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);
    if (out_bone_indices != NULL)
        *out_bone_indices = self->bone_indices;
    return self->bone_weights;
}

/* ------------------------------------------------------------------------ */
/* Palette                                                                  */
/* ------------------------------------------------------------------------ */

gboolean
lrg_skinned_mesh_set_inverse_bind (LrgSkinnedMesh *self,
                                   guint           bone,
                                   const gfloat   *matrix)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (matrix != NULL, FALSE);

    if (bone >= self->n_bones)
        return FALSE;
    memcpy (self->inverse_bind + 16 * bone, matrix, 16 * sizeof (gfloat));
    return TRUE;
}

const gfloat *
lrg_skinned_mesh_get_inverse_bind (LrgSkinnedMesh *self,
                                   guint           bone)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);

    if (bone >= self->n_bones)
        return NULL;
    return self->inverse_bind + 16 * bone;
}

void
lrg_skinned_mesh_compute_palette (LrgSkinnedMesh *self,
                                  const gfloat   *bone_world,
                                  gfloat         *out_palette)
{
    guint b;

    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    g_return_if_fail (bone_world != NULL);
    g_return_if_fail (out_palette != NULL);

    for (b = 0; b < self->n_bones; b++)
        mat4_multiply (bone_world + 16 * b, self->inverse_bind + 16 * b, out_palette + 16 * b);
}

gboolean
lrg_skinned_mesh_compute_palette_from_skeleton (LrgSkinnedMesh *self,
                                                LrgSkeleton    *skeleton,
                                                gfloat         *out_palette)
{
    g_autofree gfloat *world = NULL;
    guint              b;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (LRG_IS_SKELETON (skeleton), FALSE);
    g_return_val_if_fail (out_palette != NULL, FALSE);

    if (lrg_skeleton_get_bone_count (skeleton) < self->n_bones)
        return FALSE;
    world = g_new (gfloat, 16 * self->n_bones);
    for (b = 0; b < self->n_bones; b++)
    {
        LrgBone *bone = lrg_skeleton_get_bone (skeleton, (gint)b);

        if (bone == NULL)
            return FALSE;
        lrg_bone_pose_to_matrix (lrg_bone_get_world_pose (bone), world + 16 * b);
    }
    lrg_skinned_mesh_compute_palette (self, world, out_palette);
    return TRUE;
}

/* Positions use the affine palette; normals use its inverse transpose. Cache
 * the latter per bone so CPU skinning does not invert once per vertex. A
 * collapsed transform has no defined normal and contributes zero, matching
 * the guarded GPU calculation rather than generating NaNs. */
static void
normal_matrix (const gfloat *matrix, gfloat *normal)
{
    gdouble determinant;
    guint column, row;

    memset (normal, 0, 16 * sizeof (gfloat));
    normal[0] = matrix[5] * matrix[10] - matrix[6] * matrix[9];
    normal[1] = matrix[6] * matrix[8] - matrix[4] * matrix[10];
    normal[2] = matrix[4] * matrix[9] - matrix[5] * matrix[8];
    normal[4] = matrix[9] * matrix[2] - matrix[10] * matrix[1];
    normal[5] = matrix[10] * matrix[0] - matrix[8] * matrix[2];
    normal[6] = matrix[8] * matrix[1] - matrix[9] * matrix[0];
    normal[8] = matrix[1] * matrix[6] - matrix[2] * matrix[5];
    normal[9] = matrix[2] * matrix[4] - matrix[0] * matrix[6];
    normal[10] = matrix[0] * matrix[5] - matrix[1] * matrix[4];
    determinant = (gdouble)matrix[0] * normal[0] + (gdouble)matrix[1] * normal[1] + (gdouble)matrix[2] * normal[2];
    if (fabs (determinant) > 1e-20)
        for (column = 0; column < 3; column++)
            for (row = 0; row < 3; row++)
                normal[column * 4 + row] = (gfloat)(normal[column * 4 + row] / determinant);
    else
        memset (normal, 0, 16 * sizeof (gfloat));
    normal[15] = 1;
}

void
lrg_skinned_mesh_set_palette (LrgSkinnedMesh *self,
                              const gfloat   *palette)
{
    guint bone;
    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    g_return_if_fail (palette != NULL);

    memcpy (self->palette, palette, 16 * self->n_bones * sizeof (gfloat));
    for (bone = 0; bone < self->n_bones; bone++)
        normal_matrix (palette + 16 * bone, self->normal_palette + 16 * bone);
    self->palette_dirty = TRUE;
}

const gfloat *
lrg_skinned_mesh_get_palette (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);
    return self->palette;
}

/* ------------------------------------------------------------------------ */
/* Morph targets on the mesh                                                */
/* ------------------------------------------------------------------------ */

/* drop the morph targets and their caches */
static void
morph_clear (LrgSkinnedMesh *self)
{
    g_clear_object (&self->morph);
    g_clear_pointer (&self->morph_weights, g_free);
    g_clear_pointer (&self->morphed_positions, g_free);
    g_clear_pointer (&self->morphed_normals, g_free);
    self->morph_dirty = FALSE;
    self->morph_gpu_dirty = FALSE;
}

/* the weights changed: the blend, the CPU skin and the GPU buffers are stale */
static void
morph_touch (LrgSkinnedMesh *self)
{
    self->morph_dirty = TRUE;
    self->morph_gpu_dirty = TRUE;
    self->palette_dirty = TRUE;
}

/*
 * base_positions / base_normals:
 * What skinning starts from: the bind geometry, blended with the morph
 * weights when the mesh has targets (recomputed lazily after a weight
 * change).
 */
static void
morph_ensure (LrgSkinnedMesh *self)
{
    if (self->morph == NULL || !self->morph_dirty || self->n_vertices == 0)
        return;
    if (self->morphed_positions == NULL)
        self->morphed_positions = g_new (gfloat, 3 * self->n_vertices);
    if (self->normals != NULL && self->morphed_normals == NULL)
        self->morphed_normals = g_new (gfloat, 3 * self->n_vertices);
    lrg_morph_targets_apply (self->morph, self->morph_weights,
                             lrg_morph_targets_get_target_count (self->morph),
                             self->positions, self->normals,
                             self->morphed_positions, self->morphed_normals);
    self->morph_dirty = FALSE;
}

static const gfloat *
base_positions (LrgSkinnedMesh *self)
{
    if (self->morph == NULL)
        return self->positions;
    morph_ensure (self);
    return self->morphed_positions;
}

static const gfloat *
base_normals (LrgSkinnedMesh *self)
{
    if (self->morph == NULL || self->normals == NULL)
        return self->normals;
    morph_ensure (self);
    return self->morphed_normals;
}

gboolean
lrg_skinned_mesh_set_morph_targets (LrgSkinnedMesh  *self,
                                    LrgMorphTargets *targets)
{
    guint n;
    guint i;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (targets == NULL || LRG_IS_MORPH_TARGETS (targets), FALSE);

    if (targets != NULL && lrg_morph_targets_get_vertex_count (targets) != self->n_vertices)
        return FALSE;

    /* referenced before the old set is dropped: @targets may be it */
    if (targets != NULL)
        g_object_ref (targets);
    morph_clear (self);
    if (targets == NULL)
    {
        /* back to the bind geometry on the GPU too */
        self->morph_gpu_dirty = TRUE;
        self->palette_dirty = TRUE;
        return TRUE;
    }
    self->morph = targets;   /* the reference taken above */
    n = lrg_morph_targets_get_target_count (targets);
    self->morph_weights = g_new0 (gfloat, MAX (n, 1));
    for (i = 0; i < n; i++)
        self->morph_weights[i] = lrg_morph_targets_get_default_weight (targets, i);
    morph_touch (self);
    /* an uploaded mesh needs dynamic vertex buffers now */
    if (self->uploaded)
        lrg_skinned_mesh_unload (self);
    return TRUE;
}

LrgMorphTargets *
lrg_skinned_mesh_get_morph_targets (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);
    return self->morph;
}

void
lrg_skinned_mesh_set_morph_weights (LrgSkinnedMesh *self,
                                    const gfloat   *weights,
                                    guint           n_weights)
{
    guint n;

    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    g_return_if_fail (weights != NULL || n_weights == 0);

    if (self->morph == NULL)
        return;
    n = MIN (n_weights, lrg_morph_targets_get_target_count (self->morph));
    if (n == 0 || memcmp (self->morph_weights, weights, n * sizeof (gfloat)) == 0)
        return;
    memcpy (self->morph_weights, weights, n * sizeof (gfloat));
    morph_touch (self);
}

gboolean
lrg_skinned_mesh_set_morph_weight (LrgSkinnedMesh *self,
                                   guint           target,
                                   gfloat          weight)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);

    if (self->morph == NULL || target >= lrg_morph_targets_get_target_count (self->morph))
        return FALSE;
    if (self->morph_weights[target] != weight)
    {
        self->morph_weights[target] = weight;
        morph_touch (self);
    }
    return TRUE;
}

gboolean
lrg_skinned_mesh_set_morph_group_weights (LrgSkinnedMesh *self,
                                          const gchar    *group,
                                          const gfloat   *weights,
                                          guint           n_weights)
{
    gint  first;
    guint count = 0;
    guint n;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (group != NULL, FALSE);
    g_return_val_if_fail (weights != NULL || n_weights == 0, FALSE);

    if (self->morph == NULL)
        return FALSE;
    first = lrg_morph_targets_find_group (self->morph, group, &count);
    if (first < 0)
        return FALSE;
    n = MIN (n_weights, count);
    if (n > 0 && memcmp (self->morph_weights + first, weights, n * sizeof (gfloat)) != 0)
    {
        memcpy (self->morph_weights + first, weights, n * sizeof (gfloat));
        morph_touch (self);
    }
    return TRUE;
}

const gfloat *
lrg_skinned_mesh_get_morph_weights (LrgSkinnedMesh *self,
                                    guint          *n_weights)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);

    if (n_weights != NULL)
        *n_weights = self->morph != NULL ? lrg_morph_targets_get_target_count (self->morph) : 0;
    return self->morph != NULL ? self->morph_weights : NULL;
}

const gfloat *
lrg_skinned_mesh_get_morphed_positions (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);
    return base_positions (self);
}

/* ------------------------------------------------------------------------ */
/* Skeleton helpers                                                         */
/* ------------------------------------------------------------------------ */

guint
lrg_skinned_mesh_set_inverse_binds_from_skeleton (LrgSkinnedMesh *self,
                                                  LrgSkeleton    *skeleton)
{
    guint b;
    guint copied = 0;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), 0);
    g_return_val_if_fail (LRG_IS_SKELETON (skeleton), 0);

    for (b = 0; b < self->n_bones; b++)
    {
        const gfloat *m = lrg_skeleton_get_inverse_bind_matrix (skeleton, (gint)b);

        if (m == NULL)
            continue;
        memcpy (self->inverse_bind + 16 * b, m, 16 * sizeof (gfloat));
        copied++;
    }
    return copied;
}

gboolean
lrg_skinned_mesh_apply_skeleton (LrgSkinnedMesh *self,
                                 LrgSkeleton    *skeleton)
{
    g_autofree gfloat *palette = NULL;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (LRG_IS_SKELETON (skeleton), FALSE);

    palette = g_new (gfloat, 16 * self->n_bones);
    if (!lrg_skinned_mesh_compute_palette_from_skeleton (self, skeleton, palette))
        return FALSE;
    lrg_skinned_mesh_set_palette (self, palette);
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* CPU skinning                                                             */
/* ------------------------------------------------------------------------ */

void
lrg_skinned_mesh_skin (LrgSkinnedMesh *self,
                       gfloat         *out_positions,
                       gfloat         *out_normals)
{
    const gfloat *positions;
    const gfloat *normals;
    guint         v;

    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    g_return_if_fail (out_positions != NULL || self->n_vertices == 0);

    /* skin the morph-blended geometry when the mesh has targets */
    positions = base_positions (self);
    normals = base_normals (self);
    for (v = 0; v < self->n_vertices; v++)
    {
        const gfloat *p = positions + 3 * v;
        gfloat        acc[3] = { 0.0f, 0.0f, 0.0f };
        gfloat        nacc[3] = { 0.0f, 0.0f, 0.0f };
        guint         k;

        for (k = 0; k < LRG_SKINNED_MESH_INFLUENCES; k++)
        {
            gfloat        w = self->bone_weights[v * 4 + k];
            const gfloat *m;

            if (w <= 0.0f)
                continue;
            m = self->palette + 16 * self->bone_indices[v * 4 + k];
            acc[0] += w * (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]);
            acc[1] += w * (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]);
            acc[2] += w * (m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
            if (normals != NULL)
            {
                const gfloat *n = normals + 3 * v;
                const gfloat *normal = self->normal_palette + 16 * self->bone_indices[v * 4 + k];

                nacc[0] += w * (normal[0] * n[0] + normal[4] * n[1] + normal[8] * n[2]);
                nacc[1] += w * (normal[1] * n[0] + normal[5] * n[1] + normal[9] * n[2]);
                nacc[2] += w * (normal[2] * n[0] + normal[6] * n[1] + normal[10] * n[2]);
            }
        }
        memcpy (out_positions + 3 * v, acc, sizeof acc);
        if (out_normals != NULL)
        {
            gfloat len = sqrtf (nacc[0] * nacc[0] + nacc[1] * nacc[1] + nacc[2] * nacc[2]);

            if (len > 1e-8f)
            {
                nacc[0] /= len;
                nacc[1] /= len;
                nacc[2] /= len;
            }
            memcpy (out_normals + 3 * v, nacc, sizeof nacc);
        }
    }
}

gboolean
lrg_skinned_mesh_get_skinned_bounds (LrgSkinnedMesh *self,
                                     gfloat         *out_min,
                                     gfloat         *out_max)
{
    g_autofree gfloat *skinned = NULL;
    guint              v;
    guint              a;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);

    if (self->n_vertices == 0)
        return FALSE;
    skinned = g_new (gfloat, 3 * self->n_vertices);
    lrg_skinned_mesh_skin (self, skinned, NULL);
    for (a = 0; a < 3; a++)
    {
        out_min[a] = skinned[a];
        out_max[a] = skinned[a];
    }
    for (v = 1; v < self->n_vertices; v++)
        for (a = 0; a < 3; a++)
        {
            out_min[a] = MIN (out_min[a], skinned[3 * v + a]);
            out_max[a] = MAX (out_max[a], skinned[3 * v + a]);
        }
    return TRUE;
}

gboolean
lrg_skinned_mesh_uses_gpu (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    if (self->uploaded)
        return self->gpu;
    return self->n_bones <= LRG_SKINNED_MESH_MAX_BONES;
}

/* ------------------------------------------------------------------------ */
/* Material                                                                 */
/* ------------------------------------------------------------------------ */

void
lrg_skinned_mesh_set_texture (LrgSkinnedMesh *self,
                              GrlTexture     *texture)
{
    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));
    g_set_object (&self->texture, texture);
}

void
lrg_skinned_mesh_set_tint (LrgSkinnedMesh *self,
                           guint8          r,
                           guint8          g,
                           guint8          b,
                           guint8          a)
{
    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    self->tint.r = r;
    self->tint.g = g;
    self->tint.b = b;
    self->tint.a = a;
}

void
lrg_skinned_mesh_set_alpha_cutoff (LrgSkinnedMesh *self,
                                   gfloat          cutoff)
{
    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    self->alpha_cutoff = CLAMP (cutoff, 0.0f, 1.0f);
}

void
lrg_skinned_mesh_set_material (LrgSkinnedMesh *self,
                               LrgMaterial    *material)
{
    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    g_return_if_fail (material == NULL || LRG_IS_MATERIAL (material));
    g_set_object (&self->material, material);
}

LrgMaterial *
lrg_skinned_mesh_get_material (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), NULL);
    return self->material;
}

void
lrg_skinned_mesh_set_shader (LrgSkinnedMesh *self,
                             GrlShader      *shader)
{
    g_return_if_fail (LRG_IS_SKINNED_MESH (self));
    g_return_if_fail (shader == NULL || GRL_IS_SHADER (shader));
    g_set_object (&self->shader, shader);
    /* a released program's id may come back for another one */
    self->loc_shader_id = 0;
}

/* ------------------------------------------------------------------------ */
/* GPU                                                                      */
/* ------------------------------------------------------------------------ */

gboolean
lrg_skinned_mesh_upload (LrgSkinnedMesh  *self,
                         GError         **error)
{
    Mesh mesh;

    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!IsWindowReady ())
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                             "A graphics context is required to upload a skinned mesh");
        return FALSE;
    }
    if (self->n_vertices == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "The skinned mesh has no geometry");
        return FALSE;
    }
    if (self->uploaded)
        return TRUE;

    if (!self->holds_shader)
    {
        if (!builtin_shader_acquire ())
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "The skinning shader did not compile");
            return FALSE;
        }
        self->holds_shader = TRUE;
    }

    self->gpu = self->n_bones <= LRG_SKINNED_MESH_MAX_BONES;
    self->scratch_positions = g_new (gfloat, 3 * self->n_vertices);
    self->scratch_normals = self->normals != NULL ? g_new (gfloat, 3 * self->n_vertices) : NULL;
    if (!self->gpu)
        lrg_skinned_mesh_skin (self, self->scratch_positions, self->scratch_normals);

    /* raylib reads the borrowed CPU arrays during UploadMesh() only;
     * the CPU path keeps its position and normal buffers dynamic */
    memset (&mesh, 0, sizeof mesh);
    mesh.vertexCount = (gint)self->n_vertices;
    mesh.triangleCount = (gint)(self->n_indices / 3);
    /* with morph targets the GPU skins the blended positions, which
     * change with the weights: the buffers are dynamic then */
    mesh.vertices = self->gpu ? (gfloat *)base_positions (self) : self->scratch_positions;
    mesh.normals = self->gpu ? (gfloat *)base_normals (self) : self->scratch_normals;
    mesh.texcoords = self->texcoords;
    mesh.colors = self->colors;
    mesh.indices = self->indices;
    UploadMesh (&mesh, !self->gpu || self->morph != NULL);
    mesh.vertices = NULL;
    mesh.normals = NULL;
    mesh.texcoords = NULL;
    mesh.colors = NULL;
    mesh.indices = NULL;

    /* the bone attributes go into the mesh's vertex array at raylib's
     * bone locations, where rlLoadShaderProgram() binds the shader's
     * "vertexBoneIndices" and "vertexBoneWeights" */
    if (self->gpu && mesh.vaoId != 0 && rlEnableVertexArray (mesh.vaoId))
    {
        self->bone_index_vbo = rlLoadVertexBuffer (self->bone_indices,
                                                   (gint)(4 * self->n_vertices), false);
        rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES, 4,
                              RL_UNSIGNED_BYTE, false, 0, 0);
        rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES);
        self->bone_weight_vbo = rlLoadVertexBuffer (self->bone_weights,
                                                    (gint)(4 * self->n_vertices * sizeof (gfloat)),
                                                    false);
        rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS, 4,
                              RL_FLOAT, false, 0, 0);
        rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS);
        rlDisableVertexArray ();
    }
    else if (self->gpu)
    {
        /* no vertex arrays: DrawMesh() would not bind our attributes;
         * skin on the CPU instead */
        UnloadMesh (mesh);
        self->gpu = FALSE;
        lrg_skinned_mesh_skin (self, self->scratch_positions, self->scratch_normals);
        memset (&mesh, 0, sizeof mesh);
        mesh.vertexCount = (gint)self->n_vertices;
        mesh.triangleCount = (gint)(self->n_indices / 3);
        mesh.vertices = self->scratch_positions;
        mesh.normals = self->scratch_normals;
        mesh.texcoords = self->texcoords;
        mesh.colors = self->colors;
        mesh.indices = self->indices;
        UploadMesh (&mesh, true);
        mesh.vertices = NULL;
        mesh.normals = NULL;
        mesh.texcoords = NULL;
        mesh.colors = NULL;
        mesh.indices = NULL;
    }

    self->mesh = mesh;
    self->uploaded = TRUE;
    self->palette_dirty = FALSE;
    self->morph_gpu_dirty = FALSE;
    return TRUE;
}

gboolean
lrg_skinned_mesh_is_uploaded (LrgSkinnedMesh *self)
{
    g_return_val_if_fail (LRG_IS_SKINNED_MESH (self), FALSE);
    return self->uploaded;
}

void
lrg_skinned_mesh_unload (LrgSkinnedMesh *self)
{
    g_return_if_fail (LRG_IS_SKINNED_MESH (self));

    if (self->uploaded && IsWindowReady ())
    {
        if (self->bone_index_vbo != 0)
            rlUnloadVertexBuffer (self->bone_index_vbo);
        if (self->bone_weight_vbo != 0)
            rlUnloadVertexBuffer (self->bone_weight_vbo);
        UnloadMesh (self->mesh);
    }
    self->bone_index_vbo = 0;
    self->bone_weight_vbo = 0;
    memset (&self->mesh, 0, sizeof self->mesh);
    self->uploaded = FALSE;
    g_clear_pointer (&self->scratch_positions, g_free);
    g_clear_pointer (&self->scratch_normals, g_free);
    if (self->holds_shader)
    {
        builtin_shader_release ();
        self->holds_shader = FALSE;
    }
    self->loc_shader_id = 0;
}

void
lrg_skinned_mesh_draw (LrgSkinnedMesh  *self,
                       const GrlMatrix *transform)
{
    MaterialMap maps[SKIN_MATERIAL_MAPS];
    Material    material;
    Matrix      model;
    Mesh        mesh;
    gint        loc;
    gint        use_skinning;

    g_return_if_fail (LRG_IS_SKINNED_MESH (self));

    if (!self->uploaded && !lrg_skinned_mesh_upload (self, NULL))
        return;

    /* the CPU path re-skins into the dynamic buffers when posed */
    if (!self->gpu && self->palette_dirty)
    {
        lrg_skinned_mesh_skin (self, self->scratch_positions, self->scratch_normals);
        UpdateMeshBuffer (self->mesh, 0, self->scratch_positions,
                          (gint)(3 * self->n_vertices * sizeof (gfloat)), 0);
        if (self->scratch_normals != NULL)
            UpdateMeshBuffer (self->mesh, 2, self->scratch_normals,
                              (gint)(3 * self->n_vertices * sizeof (gfloat)), 0);
        self->palette_dirty = FALSE;
    }
    /* the GPU path re-uploads the morph blend when the weights moved */
    if (self->gpu && self->morph_gpu_dirty)
    {
        const gfloat *bp = base_positions (self);
        const gfloat *bn = base_normals (self);

        UpdateMeshBuffer (self->mesh, 0, bp, (gint)(3 * self->n_vertices * sizeof (gfloat)), 0);
        if (bn != NULL)
            UpdateMeshBuffer (self->mesh, 2, bn, (gint)(3 * self->n_vertices * sizeof (gfloat)), 0);
        self->morph_gpu_dirty = FALSE;
    }

    memset (maps, 0, sizeof maps);
    memset (&material, 0, sizeof material);
    material.shader = self->shader != NULL ? *(Shader *)grl_shader_get_handle (self->shader)
                                           : builtin_shader;
    if (self->texture != NULL)
        maps[MATERIAL_MAP_ALBEDO].texture = *(Texture2D *)grl_texture_get_handle (self->texture);
    else
    {
        maps[MATERIAL_MAP_ALBEDO].texture.id = rlGetTextureIdDefault ();
        maps[MATERIAL_MAP_ALBEDO].texture.width = 1;
        maps[MATERIAL_MAP_ALBEDO].texture.height = 1;
        maps[MATERIAL_MAP_ALBEDO].texture.mipmaps = 1;
        maps[MATERIAL_MAP_ALBEDO].texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    maps[MATERIAL_MAP_ALBEDO].color = self->tint;
    if (self->material != NULL)
    {
        /* the material's maps on the same units lrg_material_apply()
         * binds, its base colour times the tint */
        Color c;

        lrg_material_fill_raylib_maps (self->material, maps);
        c = maps[MATERIAL_MAP_ALBEDO].color;
        maps[MATERIAL_MAP_ALBEDO].color.r = (unsigned char)((c.r * self->tint.r) / 255);
        maps[MATERIAL_MAP_ALBEDO].color.g = (unsigned char)((c.g * self->tint.g) / 255);
        maps[MATERIAL_MAP_ALBEDO].color.b = (unsigned char)((c.b * self->tint.b) / 255);
        maps[MATERIAL_MAP_ALBEDO].color.a = (unsigned char)((c.a * self->tint.a) / 255);
        lrg_material_apply (self->material, material.shader.id);
    }
    material.maps = maps;

    /* the uniforms DrawMesh() does not set: the palette, the skinning
     * switch and the alpha cutoff (set on the active program) */
    rlEnableShader (material.shader.id);
    if (self->loc_shader_id != material.shader.id)
    {
        /* looked up by name once per program, not every draw */
        self->loc_shader_id = material.shader.id;
        self->loc_use_skinning = rlGetLocationUniform (material.shader.id, "useSkinning");
        self->loc_alpha_cutoff = rlGetLocationUniform (material.shader.id, "alphaCutoff");
        self->loc_bone_matrices = rlGetLocationUniform (material.shader.id, "boneMatrices");
    }
    use_skinning = self->gpu ? 1 : 0;
    loc = self->loc_use_skinning;
    if (loc >= 0)
        rlSetUniform (loc, &use_skinning, RL_SHADER_UNIFORM_INT, 1);
    loc = self->loc_alpha_cutoff;
    if (loc >= 0 && self->material == NULL)
        rlSetUniform (loc, &self->alpha_cutoff, RL_SHADER_UNIFORM_FLOAT, 1);
    if (self->gpu)
    {
        Matrix palette[LRG_SKINNED_MESH_MAX_BONES];
        guint  b;

        for (b = 0; b < self->n_bones; b++)
            palette[b] = to_raylib_matrix (self->palette + 16 * b);
        loc = self->loc_bone_matrices;
        if (loc >= 0)
            rlSetUniformMatrices (loc, palette, (gint)self->n_bones);
    }

    /* DrawMesh() draws indexed only when Mesh.indices is non-NULL; the
     * pointer is only tested on OpenGL 3.3 (see LrgStaticBatch) */
    mesh = self->mesh;
    mesh.indices = self->indices;
    if (transform != NULL)
        model = *(const Matrix *)transform;
    else
    {
        gfloat identity[16];

        mat4_identity (identity);
        model = to_raylib_matrix (identity);
    }
    if (self->material != NULL)
    {
        lrg_material_apply_state (self->material);
        DrawMesh (mesh, material, model);
        lrg_material_restore_state (self->material);
        lrg_material_reset_uniforms (material.shader.id);
    }
    else
        DrawMesh (mesh, material, model);
}

/* ------------------------------------------------------------------------ */
/* Prewarm                                                                  */
/* ------------------------------------------------------------------------ */

/* lrg_skinned_mesh_prewarm: uploads the mesh (and loads the built-in
 * skinning shader with the first one) now instead of at its first draw */
static gboolean
lrg_skinned_mesh_prewarm (LrgPrewarmable *prewarmable)
{
    LrgSkinnedMesh *self = LRG_SKINNED_MESH (prewarmable);

    if (!self->uploaded && self->n_vertices > 0)
        lrg_skinned_mesh_upload (self, NULL);
    return TRUE;
}

static void
lrg_skinned_mesh_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_skinned_mesh_prewarm;
}

/* ------------------------------------------------------------------------ */
/* LrgMorphTargets                                                          */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gchar  *group;
    gchar  *name;
    gfloat *positions;   /* 3 per vertex */
    gfloat *normals;     /* 3 per vertex, nullable */
    gfloat  default_weight;
} MorphTarget;

struct _LrgMorphTargets
{
    GObject    parent_instance;

    guint      n_vertices;
    GPtrArray *targets;  /* MorphTarget* */
};

G_DEFINE_FINAL_TYPE (LrgMorphTargets, lrg_morph_targets, G_TYPE_OBJECT)

static void
morph_target_free (gpointer data)
{
    MorphTarget *t = data;

    g_free (t->group);
    g_free (t->name);
    g_free (t->positions);
    g_free (t->normals);
    g_free (t);
}

static void
lrg_morph_targets_finalize (GObject *object)
{
    LrgMorphTargets *self = LRG_MORPH_TARGETS (object);

    g_clear_pointer (&self->targets, g_ptr_array_unref);
    G_OBJECT_CLASS (lrg_morph_targets_parent_class)->finalize (object);
}

static void
lrg_morph_targets_class_init (LrgMorphTargetsClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_morph_targets_finalize;
}

static void
lrg_morph_targets_init (LrgMorphTargets *self)
{
    self->targets = g_ptr_array_new_with_free_func (morph_target_free);
}

LrgMorphTargets *
lrg_morph_targets_new (guint n_vertices)
{
    LrgMorphTargets *self;

    if (n_vertices == 0)
        return NULL;
    self = g_object_new (LRG_TYPE_MORPH_TARGETS, NULL);
    self->n_vertices = n_vertices;
    return self;
}

guint
lrg_morph_targets_add_target (LrgMorphTargets *self,
                              const gchar     *group,
                              const gchar     *name,
                              const gfloat    *position_deltas,
                              const gfloat    *normal_deltas,
                              gfloat           default_weight)
{
    MorphTarget *t;
    guint        in_group = 0;
    guint        i;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), 0);
    g_return_val_if_fail (position_deltas != NULL, 0);

    if (group == NULL)
        group = "";
    for (i = 0; i < self->targets->len; i++)
        if (g_strcmp0 (((MorphTarget *)g_ptr_array_index (self->targets, i))->group, group) == 0)
            in_group++;

    t = g_new0 (MorphTarget, 1);
    t->group = g_strdup (group);
    t->name = name != NULL ? g_strdup (name) : g_strdup_printf ("%s.%u", group, in_group);
    t->positions = g_memdup2 (position_deltas, 3 * (gsize)self->n_vertices * sizeof (gfloat));
    t->normals = normal_deltas != NULL
                 ? g_memdup2 (normal_deltas, 3 * (gsize)self->n_vertices * sizeof (gfloat)) : NULL;
    t->default_weight = default_weight;
    g_ptr_array_add (self->targets, t);
    return self->targets->len - 1;
}

guint
lrg_morph_targets_get_vertex_count (LrgMorphTargets *self)
{
    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), 0);
    return self->n_vertices;
}

guint
lrg_morph_targets_get_target_count (LrgMorphTargets *self)
{
    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), 0);
    return self->targets->len;
}

static MorphTarget *
morph_target_at (LrgMorphTargets *self,
                 guint            target)
{
    if (target >= self->targets->len)
        return NULL;
    return g_ptr_array_index (self->targets, target);
}

const gchar *
lrg_morph_targets_get_target_name (LrgMorphTargets *self,
                                   guint            target)
{
    MorphTarget *t;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), NULL);
    t = morph_target_at (self, target);
    return t != NULL ? t->name : NULL;
}

const gchar *
lrg_morph_targets_get_target_group (LrgMorphTargets *self,
                                    guint            target)
{
    MorphTarget *t;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), NULL);
    t = morph_target_at (self, target);
    return t != NULL ? t->group : NULL;
}

gint
lrg_morph_targets_find_target (LrgMorphTargets *self,
                               const gchar     *name)
{
    guint i;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), -1);
    g_return_val_if_fail (name != NULL, -1);

    for (i = 0; i < self->targets->len; i++)
        if (g_strcmp0 (((MorphTarget *)g_ptr_array_index (self->targets, i))->name, name) == 0)
            return (gint)i;
    return -1;
}

gint
lrg_morph_targets_find_group (LrgMorphTargets *self,
                              const gchar     *group,
                              guint           *out_count)
{
    guint i;
    guint j;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), -1);
    g_return_val_if_fail (group != NULL, -1);

    if (out_count != NULL)
        *out_count = 0;
    for (i = 0; i < self->targets->len; i++)
    {
        if (g_strcmp0 (((MorphTarget *)g_ptr_array_index (self->targets, i))->group, group) != 0)
            continue;
        /* the contiguous run starting here */
        for (j = i; j < self->targets->len &&
             g_strcmp0 (((MorphTarget *)g_ptr_array_index (self->targets, j))->group, group) == 0; j++)
            ;
        if (out_count != NULL)
            *out_count = j - i;
        return (gint)i;
    }
    return -1;
}

gfloat
lrg_morph_targets_get_default_weight (LrgMorphTargets *self,
                                      guint            target)
{
    MorphTarget *t;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), 0.0f);
    t = morph_target_at (self, target);
    return t != NULL ? t->default_weight : 0.0f;
}

const gfloat *
lrg_morph_targets_get_position_deltas (LrgMorphTargets *self,
                                       guint            target)
{
    MorphTarget *t;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), NULL);
    t = morph_target_at (self, target);
    return t != NULL ? t->positions : NULL;
}

const gfloat *
lrg_morph_targets_get_normal_deltas (LrgMorphTargets *self,
                                     guint            target)
{
    MorphTarget *t;

    g_return_val_if_fail (LRG_IS_MORPH_TARGETS (self), NULL);
    t = morph_target_at (self, target);
    return t != NULL ? t->normals : NULL;
}

void
lrg_morph_targets_apply (LrgMorphTargets *self,
                         const gfloat    *weights,
                         guint            n_weights,
                         const gfloat    *base_positions_in,
                         const gfloat    *base_normals_in,
                         gfloat          *out_positions,
                         gfloat          *out_normals)
{
    gsize n3;
    guint i;
    gsize k;

    g_return_if_fail (LRG_IS_MORPH_TARGETS (self));
    g_return_if_fail (base_positions_in != NULL && out_positions != NULL);
    g_return_if_fail (weights != NULL || n_weights == 0);

    n3 = 3 * (gsize)self->n_vertices;
    memmove (out_positions, base_positions_in, n3 * sizeof (gfloat));
    if (base_normals_in != NULL && out_normals != NULL)
        memmove (out_normals, base_normals_in, n3 * sizeof (gfloat));

    /* accumulate every target with a non-zero weight */
    for (i = 0; i < self->targets->len && i < n_weights; i++)
    {
        MorphTarget *t = g_ptr_array_index (self->targets, i);
        gfloat       w = weights[i];

        if (w == 0.0f || !isfinite (w))
            continue;
        for (k = 0; k < n3; k++)
            out_positions[k] += w * t->positions[k];
        if (t->normals != NULL && base_normals_in != NULL && out_normals != NULL)
            for (k = 0; k < n3; k++)
                out_normals[k] += w * t->normals[k];
    }

    /* blended normals are no longer unit length */
    if (base_normals_in != NULL && out_normals != NULL)
    {
        for (k = 0; k < n3; k += 3)
        {
            gfloat len = sqrtf (out_normals[k] * out_normals[k] + out_normals[k + 1] * out_normals[k + 1] +
                                out_normals[k + 2] * out_normals[k + 2]);

            if (len > 1e-8f)
            {
                out_normals[k] /= len;
                out_normals[k + 1] /= len;
                out_normals[k + 2] /= len;
            }
        }
    }
}
