/* lrg-particle-billboard-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Camera-facing particle quads.  build() is the whole geometry step and
 * runs headless: it reads the system's active particles through
 * lrg_particle_system_foreach_active(), turns each into a quad facing the
 * eye, picks its flipbook cell by age and sorts the quads back to front.
 * draw() submits exactly those vertices through rlgl's immediate batch
 * with the system's blend mode, so tests see what the GPU gets.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "particles/lrg-particle-billboard-renderer.h"

/* Below this length a direction is treated as degenerate. */
#define RENDERER_EPSILON (1e-6f)

/* Side of the generated soft dot texture in pixels. */
#define RENDERER_DOT_SIZE (32)

/* One collected particle: what build() needs, plus its sort key. */
typedef struct
{
    gfloat position[3];
    gfloat color[4];
    gfloat size;
    gfloat rotation;
    gfloat age;
    gfloat max_life;
    gfloat distance_sq;
} Sprite;

struct _LrgParticleBillboardRenderer
{
    GObject     parent_instance;

    GrlTexture *texture;      /* owned, NULL: the dot */
    guint       columns;
    guint       rows;
    gfloat      fps;

    Texture2D   dot;          /* generated on the first textureless draw */
    gboolean    dot_loaded;

    GArray     *scratch;      /* gfloat vertices reused by draw() */
    GArray     *sprites;      /* Sprite: build()'s sort list, reused */
};

G_DEFINE_TYPE (LrgParticleBillboardRenderer, lrg_particle_billboard_renderer, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

static void
lrg_particle_billboard_renderer_finalize (GObject *object)
{
    LrgParticleBillboardRenderer *self = LRG_PARTICLE_BILLBOARD_RENDERER (object);

    lrg_particle_billboard_renderer_release_gpu (self);
    g_clear_object (&self->texture);
    g_clear_pointer (&self->scratch, g_array_unref);
    g_clear_pointer (&self->sprites, g_array_unref);

    G_OBJECT_CLASS (lrg_particle_billboard_renderer_parent_class)->finalize (object);
}

static void
lrg_particle_billboard_renderer_class_init (LrgParticleBillboardRendererClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_particle_billboard_renderer_finalize;
}

static void
lrg_particle_billboard_renderer_init (LrgParticleBillboardRenderer *self)
{
    self->columns = 1;
    self->rows = 1;
    self->fps = 0.0f;
    self->scratch = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->sprites = g_array_new (FALSE, FALSE, sizeof (Sprite));
}

LrgParticleBillboardRenderer *
lrg_particle_billboard_renderer_new (void)
{
    return g_object_new (LRG_TYPE_PARTICLE_BILLBOARD_RENDERER, NULL);
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                 */
/* ------------------------------------------------------------------------ */

void
lrg_particle_billboard_renderer_set_texture (LrgParticleBillboardRenderer *self,
                                             GrlTexture                   *texture)
{
    g_return_if_fail (LRG_IS_PARTICLE_BILLBOARD_RENDERER (self));
    g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

    g_set_object (&self->texture, texture);
}

GrlTexture *
lrg_particle_billboard_renderer_get_texture (LrgParticleBillboardRenderer *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_BILLBOARD_RENDERER (self), NULL);

    return self->texture;
}

void
lrg_particle_billboard_renderer_set_frames (LrgParticleBillboardRenderer *self,
                                            guint                         columns,
                                            guint                         rows,
                                            gfloat                        fps)
{
    g_return_if_fail (LRG_IS_PARTICLE_BILLBOARD_RENDERER (self));
    g_return_if_fail (columns >= 1 && rows >= 1);

    self->columns = columns;
    self->rows = rows;
    self->fps = fps;
}

void
lrg_particle_billboard_renderer_get_frames (LrgParticleBillboardRenderer *self,
                                            guint                        *columns,
                                            guint                        *rows,
                                            gfloat                       *fps)
{
    g_return_if_fail (LRG_IS_PARTICLE_BILLBOARD_RENDERER (self));

    if (columns != NULL)
        *columns = self->columns;
    if (rows != NULL)
        *rows = self->rows;
    if (fps != NULL)
        *fps = self->fps;
}

/* ------------------------------------------------------------------------ */
/* Geometry                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct
{
    GArray       *sprites;
    const gfloat *eye;
} CollectData;

/* collect_particle:
 * foreach_active() callback copying a particle and its squared distance
 * to the eye (the back-to-front sort key). */
static void
collect_particle (const LrgParticle *particle,
                  gpointer           user_data)
{
    CollectData *data = user_data;
    Sprite       sprite;
    gfloat       dx;
    gfloat       dy;
    gfloat       dz;

    sprite.position[0] = particle->position_x;
    sprite.position[1] = particle->position_y;
    sprite.position[2] = particle->position_z;
    sprite.color[0] = particle->color_r;
    sprite.color[1] = particle->color_g;
    sprite.color[2] = particle->color_b;
    sprite.color[3] = particle->color_a;
    sprite.size = particle->size;
    sprite.rotation = particle->rotation;
    sprite.age = particle->age;
    sprite.max_life = particle->max_life;

    dx = sprite.position[0] - data->eye[0];
    dy = sprite.position[1] - data->eye[1];
    dz = sprite.position[2] - data->eye[2];
    sprite.distance_sq = dx * dx + dy * dy + dz * dz;

    g_array_append_val (data->sprites, sprite);
}

/* compare_far_first:
 * Farthest sprite first: alpha blending composites back to front. */
static gint
compare_far_first (gconstpointer a,
                   gconstpointer b)
{
    gfloat da = ((const Sprite *)a)->distance_sq;
    gfloat db = ((const Sprite *)b)->distance_sq;

    return (da < db) - (da > db);
}

static gfloat
vec3_normalize (gfloat *v)
{
    gfloat len = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (len > RENDERER_EPSILON)
    {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
    return len;
}

static void
vec3_cross (const gfloat *a,
            const gfloat *b,
            gfloat       *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* sprite_basis:
 * The quad's right and up axes: facing = sprite -> eye, right = up x
 * facing, quad up = facing x right, both turned by the rotation about the
 * facing axis.  A sprite on the eye faces +Z; an up parallel to the
 * facing direction falls back to world Z (then X) to stay defined. */
static void
sprite_basis (const Sprite *sprite,
              const gfloat *eye,
              const gfloat *up,
              gfloat       *right,
              gfloat       *quad_up)
{
    gfloat facing[3];
    gfloat r[3];
    gfloat u[3];
    gfloat c;
    gfloat s;
    gfloat angle;
    guint  i;

    facing[0] = eye[0] - sprite->position[0];
    facing[1] = eye[1] - sprite->position[1];
    facing[2] = eye[2] - sprite->position[2];
    if (vec3_normalize (facing) <= RENDERER_EPSILON)
    {
        facing[0] = 0.0f;
        facing[1] = 0.0f;
        facing[2] = 1.0f;
    }

    vec3_cross (up, facing, r);
    if (vec3_normalize (r) <= RENDERER_EPSILON)
    {
        static const gfloat alt_z[3] = { 0.0f, 0.0f, 1.0f };
        static const gfloat alt_x[3] = { 1.0f, 0.0f, 0.0f };

        vec3_cross (alt_z, facing, r);
        if (vec3_normalize (r) <= RENDERER_EPSILON)
        {
            vec3_cross (alt_x, facing, r);
            vec3_normalize (r);
        }
    }
    vec3_cross (facing, r, u);
    vec3_normalize (u);

    angle = sprite->rotation * (gfloat)(G_PI / 180.0);
    c = cosf (angle);
    s = sinf (angle);
    for (i = 0; i < 3; i++)
    {
        right[i] = c * r[i] + s * u[i];
        quad_up[i] = -s * r[i] + c * u[i];
    }
}

/* sprite_uv:
 * The flipbook cell of the sprite's age: u0, v0 (top), u1, v1 (bottom). */
static void
sprite_uv (LrgParticleBillboardRenderer *self,
           const Sprite                 *sprite,
           gfloat                       *uv)
{
    guint frames = self->columns * self->rows;
    guint frame = 0;
    guint col;
    guint row;

    if (frames > 1)
    {
        if (self->fps > 0.0f)
        {
            frame = (guint)floorf (MAX (sprite->age, 0.0f) * self->fps) % frames;
        }
        else if (sprite->max_life > 0.0f)
        {
            gfloat t = CLAMP (sprite->age / sprite->max_life, 0.0f, 1.0f);

            frame = MIN ((guint)floorf (t * (gfloat)frames), frames - 1);
        }
    }

    col = frame % self->columns;
    row = frame / self->columns;
    uv[0] = (gfloat)col / (gfloat)self->columns;
    uv[1] = (gfloat)row / (gfloat)self->rows;
    uv[2] = (gfloat)(col + 1) / (gfloat)self->columns;
    uv[3] = (gfloat)(row + 1) / (gfloat)self->rows;
}

static void
write_vertex (gfloat       *out,
              const gfloat *position,
              gfloat        u,
              gfloat        v,
              const gfloat *color)
{
    out[0] = position[0];
    out[1] = position[1];
    out[2] = position[2];
    out[3] = u;
    out[4] = v;
    out[5] = color[0];
    out[6] = color[1];
    out[7] = color[2];
    out[8] = color[3];
}

guint
lrg_particle_billboard_renderer_build (LrgParticleBillboardRenderer *self,
                                       LrgParticleSystem            *system,
                                       const gfloat                 *eye,
                                       const gfloat                 *up,
                                       gfloat                       *vertices,
                                       guint                         max_quads)
{
    GArray     *sprites;
    CollectData data;
    guint       first;
    guint       count;
    guint       i;

    g_return_val_if_fail (LRG_IS_PARTICLE_BILLBOARD_RENDERER (self), 0);
    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (system), 0);
    g_return_val_if_fail (eye != NULL && up != NULL, 0);

    /* Collect and sort back to front, into a list kept between frames:
     * 52 bytes a particle is past glibc's 128 KiB mmap threshold at ~2500
     * particles, and a fresh array would mmap and fault it in every frame. */
    sprites = self->sprites;
    g_array_set_size (sprites, 0);
    data.sprites = sprites;
    data.eye = eye;
    lrg_particle_system_foreach_active (system, collect_particle, &data);
    if (sprites->len == 0 || max_quads == 0)
        return 0;
    g_array_sort (sprites, compare_far_first);

    /* Keep the nearest max_quads: they are the tail of the sorted list. */
    count = MIN (sprites->len, max_quads);
    first = sprites->len - count;
    if (vertices == NULL)
        return count;

    for (i = 0; i < count; i++)
    {
        const Sprite *sprite = &g_array_index (sprites, Sprite, first + i);
        gfloat       *quad = vertices + (gsize)i * LRG_PARTICLE_BILLBOARD_FLOATS_PER_QUAD;
        gfloat        right[3];
        gfloat        quad_up[3];
        gfloat        uv[4];
        gfloat        corner[3];
        gfloat        half;
        guint         k;

        sprite_basis (sprite, eye, up, right, quad_up);
        sprite_uv (self, sprite, uv);
        half = sprite->size * 0.5f;

        /* bottom-left, bottom-right, top-right, top-left */
        for (k = 0; k < 3; k++)
            corner[k] = sprite->position[k] - right[k] * half - quad_up[k] * half;
        write_vertex (quad, corner, uv[0], uv[3], sprite->color);
        for (k = 0; k < 3; k++)
            corner[k] = sprite->position[k] + right[k] * half - quad_up[k] * half;
        write_vertex (quad + LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX, corner, uv[2], uv[3], sprite->color);
        for (k = 0; k < 3; k++)
            corner[k] = sprite->position[k] + right[k] * half + quad_up[k] * half;
        write_vertex (quad + 2 * LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX, corner, uv[2], uv[1], sprite->color);
        for (k = 0; k < 3; k++)
            corner[k] = sprite->position[k] - right[k] * half + quad_up[k] * half;
        write_vertex (quad + 3 * LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX, corner, uv[0], uv[1], sprite->color);
    }

    return count;
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

/* ensure_dot:
 * A soft white dot (radial alpha falloff) for textureless renderers. */
static void
ensure_dot (LrgParticleBillboardRenderer *self)
{
    Image image;

    if (self->dot_loaded)
        return;

    image = GenImageGradientRadial (RENDERER_DOT_SIZE, RENDERER_DOT_SIZE, 0.0f,
                                    (Color){ 255, 255, 255, 255 },
                                    (Color){ 255, 255, 255, 0 });
    self->dot = LoadTextureFromImage (image);
    UnloadImage (image);
    self->dot_loaded = self->dot.id != 0;
}

static gint
raylib_blend_mode (LrgParticleBlendMode mode)
{
    switch (mode)
    {
    case LRG_PARTICLE_BLEND_ADDITIVE:
        return BLEND_ADDITIVE;
    case LRG_PARTICLE_BLEND_MULTIPLY:
        return BLEND_MULTIPLIED;
    case LRG_PARTICLE_BLEND_ALPHA:
    default:
        return BLEND_ALPHA;
    }
}

static guint8
to_byte (gfloat value)
{
    return (guint8)(CLAMP (value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void
lrg_particle_billboard_renderer_draw (LrgParticleBillboardRenderer *self,
                                      LrgParticleSystem            *system,
                                      GrlCamera3D                  *camera)
{
    g_autoptr(GrlVector3) position = NULL;
    g_autoptr(GrlVector3) up_vector = NULL;
    gfloat    eye[3];
    gfloat    up[3];
    guint     active;
    guint     quads;
    guint     texture_id;
    guint     i;

    g_return_if_fail (LRG_IS_PARTICLE_BILLBOARD_RENDERER (self));
    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (system));
    g_return_if_fail (GRL_IS_CAMERA3D (camera));

    active = lrg_particle_system_get_active_count (system);
    if (active == 0)
        return;

    position = grl_camera3d_get_position (camera);
    up_vector = grl_camera3d_get_up (camera);
    eye[0] = position->x;
    eye[1] = position->y;
    eye[2] = position->z;
    up[0] = up_vector->x;
    up[1] = up_vector->y;
    up[2] = up_vector->z;

    g_array_set_size (self->scratch, (guint)active * LRG_PARTICLE_BILLBOARD_FLOATS_PER_QUAD);
    quads = lrg_particle_billboard_renderer_build (self, system, eye, up,
                                                   (gfloat *)(void *)self->scratch->data, active);
    if (quads == 0)
        return;

    if (self->texture != NULL)
    {
        texture_id = ((Texture2D *)grl_texture_get_handle (self->texture))->id;
    }
    else
    {
        ensure_dot (self);
        texture_id = self->dot_loaded ? self->dot.id : rlGetTextureIdDefault ();
    }

    /* Transparent quads test against the scene's depth but do not write
     * it, so overlapping particles all blend.  BeginBlendMode() flushes
     * the batch, so the mode applies to these quads only. */
    rlDrawRenderBatchActive ();
    rlDisableDepthMask ();
    BeginBlendMode (raylib_blend_mode (lrg_particle_system_get_blend_mode (system)));

    rlSetTexture (texture_id);
    rlBegin (RL_QUADS);
    for (i = 0; i < quads; i++)
    {
        const gfloat *quad = (const gfloat *)(const void *)self->scratch->data
                             + (gsize)i * LRG_PARTICLE_BILLBOARD_FLOATS_PER_QUAD;
        guint k;

        for (k = 0; k < 4; k++)
        {
            const gfloat *v = quad + k * LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX;

            rlColor4ub (to_byte (v[5]), to_byte (v[6]), to_byte (v[7]), to_byte (v[8]));
            rlTexCoord2f (v[3], v[4]);
            rlVertex3f (v[0], v[1], v[2]);
        }
    }
    rlEnd ();
    rlSetTexture (0);

    EndBlendMode ();
    rlDrawRenderBatchActive ();
    rlEnableDepthMask ();
}

void
lrg_particle_billboard_renderer_release_gpu (LrgParticleBillboardRenderer *self)
{
    g_return_if_fail (LRG_IS_PARTICLE_BILLBOARD_RENDERER (self));

    /* GPU objects can only be released while the context exists. */
    if (self->dot_loaded && IsWindowReady ())
        UnloadTexture (self->dot);
    self->dot_loaded = FALSE;
    memset (&self->dot, 0, sizeof (self->dot));
}
