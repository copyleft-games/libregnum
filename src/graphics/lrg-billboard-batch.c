/* lrg-billboard-batch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Batched camera-facing quads. Billboards are stored as plain records;
 * quads are rebuilt every draw from the camera position (they turn as
 * the camera moves) and emitted through rlgl's immediate batch grouped
 * by material, so a town of several thousand flats costs one texture
 * bind per distinct texture. Corner math is shared with the headless
 * get_corners() so tests and picking see exactly what is drawn.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-billboard-batch.h"

/* Below this horizontal distance the camera is "above" the billboard and
 * the axis-locked facing is undefined; a fixed basis is used instead. */
#define BILLBOARD_MIN_FACING (1e-6f)

typedef struct
{
    guint    material;
    gfloat   position[3];
    gfloat   width;
    gfloat   height;
    gfloat   pivot_x;
    gfloat   pivot_y;
    gfloat   uv[4];       /* u0, v0 (top), u1, v1 (bottom) */
    guint8   tint[4];
    gboolean visible;
    gboolean emissive;
} Billboard;

typedef struct
{
    GrlTexture *owned;
    Texture2D   texture;
} MaterialTexture;

struct _LrgBillboardBatch
{
    GObject           parent_instance;

    LrgBillboardMode  mode;
    GArray           *billboards;   /* Billboard */
    GHashTable       *textures;     /* GUINT_TO_POINTER (material) -> MaterialTexture* */
    gfloat            cutoff;
    Shader            shader;       /* cutout shader, loaded on first draw */
    gboolean          shader_tried;
    gboolean          shader_loaded;
    gint              cutoff_loc;
    LrgLightSet      *light_set;    /* own lights, NULL: the bound set */
    gboolean          lit;
};

G_DEFINE_TYPE (LrgBillboardBatch, lrg_billboard_batch, G_TYPE_OBJECT)

/* raylib's default vertex shader inputs, plus an alpha test. */
static const gchar cutout_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const gchar cutout_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float alphaCutoff;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    if (texel.a < alphaCutoff) discard;\n"
    "    finalColor = texel;\n"
    "}\n";

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
lrg_billboard_batch_finalize (GObject *object)
{
    LrgBillboardBatch *self = LRG_BILLBOARD_BATCH (object);

    /* GPU objects can only be released while the context exists. */
    if (self->shader_loaded && IsWindowReady ())
        UnloadShader (self->shader);

    g_clear_object (&self->light_set);
    g_clear_pointer (&self->billboards, g_array_unref);
    g_clear_pointer (&self->textures, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_billboard_batch_parent_class)->finalize (object);
}

static void
lrg_billboard_batch_class_init (LrgBillboardBatchClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_billboard_batch_finalize;
}

static void
lrg_billboard_batch_init (LrgBillboardBatch *self)
{
    self->mode = LRG_BILLBOARD_MODE_AXIS_Y;
    self->billboards = g_array_new (FALSE, TRUE, sizeof (Billboard));
    self->textures = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                            NULL, material_texture_free);
    self->cutoff = LRG_BILLBOARD_BATCH_DEFAULT_CUTOFF;
    self->cutoff_loc = -1;
    self->lit = TRUE;
}

LrgBillboardBatch *
lrg_billboard_batch_new (void)
{
    return g_object_new (LRG_TYPE_BILLBOARD_BATCH, NULL);
}

void
lrg_billboard_batch_set_mode (LrgBillboardBatch *self,
                              LrgBillboardMode   mode)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    self->mode = mode;
}

LrgBillboardMode
lrg_billboard_batch_get_mode (LrgBillboardBatch *self)
{
    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), LRG_BILLBOARD_MODE_AXIS_Y);
    return self->mode;
}

/* ------------------------------------------------------------------------ */
/* Billboards                                                               */
/* ------------------------------------------------------------------------ */

static Billboard *
billboard_at (LrgBillboardBatch *self,
              guint              index)
{
    if (index >= self->billboards->len)
        return NULL;
    return &g_array_index (self->billboards, Billboard, index);
}

guint
lrg_billboard_batch_add (LrgBillboardBatch *self,
                         guint              material,
                         const GrlVector3  *position,
                         gfloat             width,
                         gfloat             height)
{
    Billboard b;

    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), 0);
    g_return_val_if_fail (position != NULL, 0);
    g_return_val_if_fail (width > 0.0f && height > 0.0f, 0);

    memset (&b, 0, sizeof b);
    b.material = material;
    b.position[0] = position->x;
    b.position[1] = position->y;
    b.position[2] = position->z;
    b.width = width;
    b.height = height;
    b.pivot_x = 0.5f;
    b.pivot_y = 0.0f;
    b.uv[0] = 0.0f;
    b.uv[1] = 0.0f;
    b.uv[2] = 1.0f;
    b.uv[3] = 1.0f;
    b.tint[0] = b.tint[1] = b.tint[2] = b.tint[3] = 255;
    b.visible = TRUE;
    g_array_append_val (self->billboards, b);

    return self->billboards->len - 1;
}

void
lrg_billboard_batch_clear (LrgBillboardBatch *self)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    g_array_set_size (self->billboards, 0);
}

guint
lrg_billboard_batch_get_count (LrgBillboardBatch *self)
{
    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), 0);
    return self->billboards->len;
}

void
lrg_billboard_batch_set_position (LrgBillboardBatch *self,
                                  guint              index,
                                  const GrlVector3  *position)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    g_return_if_fail (position != NULL);

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);
    b->position[0] = position->x;
    b->position[1] = position->y;
    b->position[2] = position->z;
}

void
lrg_billboard_batch_set_size (LrgBillboardBatch *self,
                              guint              index,
                              gfloat             width,
                              gfloat             height)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    g_return_if_fail (width > 0.0f && height > 0.0f);

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);
    b->width = width;
    b->height = height;
}

void
lrg_billboard_batch_set_pivot (LrgBillboardBatch *self,
                               guint              index,
                               gfloat             pivot_x,
                               gfloat             pivot_y)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);
    b->pivot_x = pivot_x;
    b->pivot_y = pivot_y;
}

void
lrg_billboard_batch_set_uv (LrgBillboardBatch  *self,
                            guint               index,
                            const GrlRectangle *uv)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);

    if (uv == NULL)
    {
        b->uv[0] = 0.0f;
        b->uv[1] = 0.0f;
        b->uv[2] = 1.0f;
        b->uv[3] = 1.0f;
        return;
    }
    b->uv[0] = uv->x;
    b->uv[1] = uv->y;
    b->uv[2] = uv->x + uv->width;
    b->uv[3] = uv->y + uv->height;
}

void
lrg_billboard_batch_set_material (LrgBillboardBatch *self,
                                  guint              index,
                                  guint              material)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);
    b->material = material;
}

void
lrg_billboard_batch_set_tint (LrgBillboardBatch *self,
                              guint              index,
                              const GrlColor    *tint)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);

    if (tint == NULL)
    {
        b->tint[0] = b->tint[1] = b->tint[2] = b->tint[3] = 255;
        return;
    }
    b->tint[0] = tint->r;
    b->tint[1] = tint->g;
    b->tint[2] = tint->b;
    b->tint[3] = tint->a;
}

void
lrg_billboard_batch_set_visible (LrgBillboardBatch *self,
                                 guint              index,
                                 gboolean           visible)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);
    b->visible = visible;
}

void
lrg_billboard_batch_set_material_texture (LrgBillboardBatch *self,
                                          guint              material,
                                          GrlTexture        *texture)
{
    MaterialTexture *entry;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
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
lrg_billboard_batch_set_alpha_cutoff (LrgBillboardBatch *self,
                                      gfloat             cutoff)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    self->cutoff = cutoff;
}

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

/* compute_basis:
 * Axis-locked: right is perpendicular to the horizontal direction from the
 * billboard to the camera, so the quad faces the eye and stays upright.
 * The camera's right for a view direction -f is cross (-f, Y) = (f.z, 0,
 * -f.x). Spherical: the camera's own right/up. */
static void
compute_basis (LrgBillboardBatch *self,
               const Billboard   *b,
               const gfloat      *camera,
               const gfloat      *cam_right,
               const gfloat      *cam_up,
               gfloat            *right,
               gfloat            *up)
{
    if (self->mode == LRG_BILLBOARD_MODE_SPHERICAL && cam_right != NULL && cam_up != NULL)
    {
        memcpy (right, cam_right, 3 * sizeof (gfloat));
        memcpy (up, cam_up, 3 * sizeof (gfloat));
        return;
    }

    {
        gfloat dx  = camera[0] - b->position[0];
        gfloat dz  = camera[2] - b->position[2];
        gfloat len = sqrtf (dx * dx + dz * dz);

        if (len < BILLBOARD_MIN_FACING)
        {
            right[0] = 1.0f;
            right[1] = 0.0f;
            right[2] = 0.0f;
        }
        else
        {
            right[0] = dz / len;
            right[1] = 0.0f;
            right[2] = -dx / len;
        }
        up[0] = 0.0f;
        up[1] = 1.0f;
        up[2] = 0.0f;
    }
}

/* build_corners:
 * Bottom-left, bottom-right, top-right, top-left, counter-clockwise as
 * seen from the camera (front-facing for raylib's default culling). */
static void
build_corners (const Billboard *b,
               const gfloat    *right,
               const gfloat    *up,
               gfloat           out[4][3])
{
    guint axis;

    for (axis = 0; axis < 3; axis++)
    {
        gfloat bl = b->position[axis]
                    - right[axis] * b->width * b->pivot_x
                    - up[axis] * b->height * b->pivot_y;

        out[0][axis] = bl;
        out[1][axis] = bl + right[axis] * b->width;
        out[2][axis] = bl + right[axis] * b->width + up[axis] * b->height;
        out[3][axis] = bl + up[axis] * b->height;
    }
}

gboolean
lrg_billboard_batch_get_corners (LrgBillboardBatch *self,
                                 guint              index,
                                 const GrlVector3  *camera_position,
                                 const GrlVector3  *camera_right,
                                 const GrlVector3  *camera_up,
                                 GrlVector3        *corners)
{
    Billboard *b;
    gfloat     camera[3];
    gfloat     cam_right[3];
    gfloat     cam_up[3];
    gfloat     right[3];
    gfloat     up[3];
    gfloat     out[4][3];
    guint      i;

    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), FALSE);
    g_return_val_if_fail (camera_position != NULL, FALSE);
    g_return_val_if_fail (corners != NULL, FALSE);

    b = billboard_at (self, index);
    if (b == NULL)
        return FALSE;

    camera[0] = camera_position->x;
    camera[1] = camera_position->y;
    camera[2] = camera_position->z;
    if (camera_right != NULL && camera_up != NULL)
    {
        cam_right[0] = camera_right->x;
        cam_right[1] = camera_right->y;
        cam_right[2] = camera_right->z;
        cam_up[0] = camera_up->x;
        cam_up[1] = camera_up->y;
        cam_up[2] = camera_up->z;
    }

    compute_basis (self, b, camera,
                   camera_right != NULL ? cam_right : NULL,
                   camera_up != NULL ? cam_up : NULL,
                   right, up);
    build_corners (b, right, up, out);

    for (i = 0; i < 4; i++)
    {
        corners[i].x = out[i][0];
        corners[i].y = out[i][1];
        corners[i].z = out[i][2];
    }
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Lighting                                                                 */
/* ------------------------------------------------------------------------ */

void
lrg_billboard_batch_set_light_set (LrgBillboardBatch *self,
                                   LrgLightSet       *light_set)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    g_return_if_fail (light_set == NULL || LRG_IS_LIGHT_SET (light_set));

    g_set_object (&self->light_set, light_set);
}

LrgLightSet *
lrg_billboard_batch_get_light_set (LrgBillboardBatch *self)
{
    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), NULL);
    return self->light_set;
}

void
lrg_billboard_batch_set_lit (LrgBillboardBatch *self,
                             gboolean           lit)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    self->lit = lit;
}

gboolean
lrg_billboard_batch_get_lit (LrgBillboardBatch *self)
{
    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), FALSE);
    return self->lit;
}

void
lrg_billboard_batch_set_emissive (LrgBillboardBatch *self,
                                  guint              index,
                                  gboolean           emissive)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);
    b->emissive = emissive;
}

gboolean
lrg_billboard_batch_get_emissive (LrgBillboardBatch *self,
                                  guint              index)
{
    Billboard *b;

    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), FALSE);

    b = billboard_at (self, index);
    return b != NULL && b->emissive;
}

/* active_light_set:
 * The lights a draw uses now: the batch's own, else the bound set;
 * NULL for an unlit batch. */
static LrgLightSet *
active_light_set (LrgBillboardBatch *self)
{
    if (!self->lit)
        return NULL;
    return self->light_set != NULL ? self->light_set : lrg_light_set_get_bound ();
}

/* lit_tint:
 * The billboard's tint times the light at its centre (pivot moved to
 * the middle of the quad along world up, then through @transform, the
 * rlgl matrix vertices go through), clamped per channel. */
static void
lit_tint (const Billboard *b,
          LrgLightSet     *lights,
          const Matrix    *transform,
          guint8          *out)
{
    GrlVector3 centre;
    gfloat     local[3];
    gfloat     light[3];
    guint      k;

    memcpy (out, b->tint, 4);
    if (lights == NULL || b->emissive)
        return;

    local[0] = b->position[0];
    local[1] = b->position[1] + (0.5f - b->pivot_y) * b->height;
    local[2] = b->position[2];
    centre.x = transform->m0 * local[0] + transform->m4 * local[1]
               + transform->m8 * local[2] + transform->m12;
    centre.y = transform->m1 * local[0] + transform->m5 * local[1]
               + transform->m9 * local[2] + transform->m13;
    centre.z = transform->m2 * local[0] + transform->m6 * local[1]
               + transform->m10 * local[2] + transform->m14;

    lrg_light_set_sample (lights, &centre, NULL, light);
    for (k = 0; k < 3; k++)
    {
        gfloat v = (gfloat)b->tint[k] * light[k];

        out[k] = (guint8)CLAMP (v + 0.5f, 0.0f, 255.0f);
    }
}

gboolean
lrg_billboard_batch_get_lit_tint (LrgBillboardBatch *self,
                                  guint              index,
                                  GrlColor          *out_tint)
{
    Billboard *b;
    Matrix     identity = { 1.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, 1.0f };
    guint8     rgba[4];

    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), FALSE);
    g_return_val_if_fail (out_tint != NULL, FALSE);

    b = billboard_at (self, index);
    if (b == NULL)
        return FALSE;

    lit_tint (b, active_light_set (self), &identity, rgba);
    out_tint->r = rgba[0];
    out_tint->g = rgba[1];
    out_tint->b = rgba[2];
    out_tint->a = rgba[3];
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

/* ensure_shader:
 * Loads the cutout shader once. On failure (e.g. a GLES2 context) the
 * batch falls back to raylib's default shader with plain blending. */
static void
ensure_shader (LrgBillboardBatch *self)
{
    if (self->shader_tried)
        return;
    self->shader_tried = TRUE;

    self->shader = LoadShaderFromMemory (cutout_vs, cutout_fs);
    if (self->shader.id == 0 || self->shader.id == rlGetShaderIdDefault ())
        return;

    self->shader_loaded = TRUE;
    self->cutoff_loc = GetShaderLocation (self->shader, "alphaCutoff");
}

static gint
compare_material (gconstpointer a,
                  gconstpointer b)
{
    guint ma = (*(Billboard * const *)a)->material;
    guint mb = (*(Billboard * const *)b)->material;

    return (ma > mb) - (ma < mb);
}

void
lrg_billboard_batch_draw (LrgBillboardBatch *self,
                          const GrlVector3  *camera_position)
{
    g_autoptr(GPtrArray) order = NULL;
    Matrix    view;
    gfloat    camera[3];
    gfloat    cam_right[3];
    gfloat    cam_up[3];
    gboolean  use_shader;
    LrgLightSet *lights;
    Matrix    transform;
    guint     current;
    gboolean  open;
    guint     i;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    g_return_if_fail (camera_position != NULL);

    if (self->billboards->len == 0)
        return;

    camera[0] = camera_position->x;
    camera[1] = camera_position->y;
    camera[2] = camera_position->z;

    /* The view matrix rows are the camera basis in world space. */
    view = rlGetMatrixModelview ();
    cam_right[0] = view.m0;
    cam_right[1] = view.m4;
    cam_right[2] = view.m8;
    cam_up[0] = view.m1;
    cam_up[1] = view.m5;
    cam_up[2] = view.m9;

    /* Group visible, textured billboards by material: one bind each. */
    order = g_ptr_array_sized_new (self->billboards->len);
    for (i = 0; i < self->billboards->len; i++)
    {
        Billboard *b = &g_array_index (self->billboards, Billboard, i);

        if (b->visible && g_hash_table_contains (self->textures, GUINT_TO_POINTER (b->material)))
            g_ptr_array_add (order, b);
    }
    if (order->len == 0)
        return;
    g_ptr_array_sort (order, compare_material);

    /* rlVertex3f() applies the push/pop transform on the CPU; light the
     * centres in that same space (the space the static-geometry shader
     * lights, see lrg_light_set_sample()). */
    lights = active_light_set (self);
    transform = rlGetMatrixTransform ();

    use_shader = FALSE;
    if (self->cutoff > 0.0f)
    {
        ensure_shader (self);
        use_shader = self->shader_loaded;
    }
    if (use_shader)
    {
        BeginShaderMode (self->shader);
        SetShaderValue (self->shader, self->cutoff_loc, &self->cutoff, SHADER_UNIFORM_FLOAT);
    }

    open = FALSE;
    current = 0;
    for (i = 0; i < order->len; i++)
    {
        Billboard *b = g_ptr_array_index (order, i);
        gfloat     right[3];
        gfloat     up[3];
        gfloat     q[4][3];

        if (!open || b->material != current)
        {
            MaterialTexture *entry;

            if (open)
                rlEnd ();
            entry = g_hash_table_lookup (self->textures, GUINT_TO_POINTER (b->material));
            rlSetTexture (entry->texture.id);
            rlBegin (RL_QUADS);
            open = TRUE;
            current = b->material;
        }

        compute_basis (self, b, camera, cam_right, cam_up, right, up);
        build_corners (b, right, up, q);

        {
            guint8 rgba[4];

            lit_tint (b, lights, &transform, rgba);
            rlColor4ub (rgba[0], rgba[1], rgba[2], rgba[3]);
        }
        /* facing normal = right x up (points at the camera) */
        rlNormal3f (right[1] * up[2] - right[2] * up[1],
                    right[2] * up[0] - right[0] * up[2],
                    right[0] * up[1] - right[1] * up[0]);
        rlTexCoord2f (b->uv[0], b->uv[3]);
        rlVertex3f (q[0][0], q[0][1], q[0][2]);
        rlTexCoord2f (b->uv[2], b->uv[3]);
        rlVertex3f (q[1][0], q[1][1], q[1][2]);
        rlTexCoord2f (b->uv[2], b->uv[1]);
        rlVertex3f (q[2][0], q[2][1], q[2][2]);
        rlTexCoord2f (b->uv[0], b->uv[1]);
        rlVertex3f (q[3][0], q[3][1], q[3][2]);
    }
    if (open)
        rlEnd ();
    rlSetTexture (0);

    if (use_shader)
    {
        /* Flush while the cutout shader is still bound. */
        rlDrawRenderBatchActive ();
        EndShaderMode ();
    }
}
