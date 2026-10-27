/* lrg-billboard-batch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Batched camera-facing quads. Billboards are stored as plain records.
 *
 * Static billboards live on the GPU: once a batch of at least
 * GPU_MIN_QUADS billboards has been drawn GPU_SETTLE_DRAWS times without
 * a billboard being added (at its first draw when GPU_EAGER_QUADS were
 * added since the last upload), its visible billboards are written once into
 * vertex buffers sorted by material (pages of up to PAGE_QUADS quads,
 * 16-bit indices). Every frame the vertex shader turns each quad
 * towards the eye and lights it at its centre (LRG_LIGHT_SAMPLE_GLSL),
 * so a draw costs one call per material instead of rebuilding every
 * quad on the CPU.
 *
 * Billboards that change afterwards (a new material, position, size,
 * tint, visibility...) are "demoted": their quad in the buffer is
 * zeroed (no area, no fragments) and they join the loose list, which
 * is drawn the original way: quads rebuilt every draw from the camera
 * position and emitted through rlgl's immediate batch grouped by
 * material, lit on the CPU (lit_tint). Billboards added after the
 * upload are loose too until enough of them justify a rebuild; a batch
 * whose quads mostly get demoted (particles, actors that move every
 * frame) drops its buffers and stays immediate until it is cleared.
 *
 * Corner math is shared with the headless get_corners() and mirrored in
 * the vertex shader, so tests, picking and both paths agree on what is
 * drawn.
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
#include "graphics/lrg-light-set-private.h"
#include "graphics/lrg-draw-cull-private.h"
#include "lighting/lrg-shadow-renderer.h"
#include "lighting/lrg-shadow-renderer-private.h"
#include "weather/lrg-ground-overlay-private.h"
#include "core/lrg-prewarm.h"

/* Below this horizontal distance the camera is "above" the billboard and
 * the axis-locked facing is undefined; a fixed basis is used instead. */
#define BILLBOARD_MIN_FACING (1e-6f)

/* GPU path: quads per vertex page (16-bit indices: 4 vertices each) */
#define PAGE_QUADS (16384u)

/* GPU path: most quads of one culling cluster (a run of one material
 * in upload order, tested against the frustum as one box) */
#define CLUSTER_QUADS (256u)

/* GPU path: smallest batch worth its own buffers */
#define GPU_MIN_QUADS (64u)

/* GPU path: draws without an added billboard before the upload */
#define GPU_SETTLE_DRAWS (2u)

/* GPU path: a batch this large uploads at its first draw (a streamed
 * landscape tile: drawing it the immediate way first costs a frame) */
#define GPU_EAGER_QUADS (1024u)

/* one vertex of the GPU path: position (the billboard's pivot point),
 * texture coordinate, tint, and (corner right offset, corner up offset,
 * light sample height above the pivot, emissive) */
typedef struct
{
    gfloat position[3];
    gfloat uv[2];
    guint8 color[4];
    gfloat corner[4];
} GpuVertex;

/* one vertex page of the GPU path */
typedef struct
{
    guint vao;
    guint vbo;
    guint quads;
} GpuPage;

/* a run of quads of one material in upload order */
typedef struct
{
    guint material;
    guint first;          /* quad index over all pages */
    guint count;
    guint cluster_first;  /* its clusters in GpuCluster order */
    guint cluster_count;
} GpuRange;

/* up to CLUSTER_QUADS consecutive quads of one range and the box they
 * can reach in the batch's space: the pivots grown by the farthest
 * corner (in any facing), plus the wind's bend at draw time */
typedef struct
{
    guint  first;         /* quad index over all pages */
    guint  count;
    gfloat min[3];
    gfloat max[3];
    gfloat sway_reach;    /* most sway weight x corner height above a pivot */
} GpuCluster;

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
    gfloat   sway;        /* 0 .. 1: how much the wind bends it */
    gint     gpu_slot;    /* quad index in the GPU pages, -1: loose */
    gboolean dynamic;     /* changed after an upload: always loose */
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
    LrgLightSetFogLocations fog_locs;
    LrgLightSet      *light_set;    /* own lights, NULL: the bound set */
    gboolean          lit;

    /* wind sway: direction (x, z), lean and gust per metre of height,
     * gust frequency (radians per second) and the clock */
    gfloat            wind[4];
    gfloat            wind_frequency;
    gfloat            wind_time;

    /* GPU path (see the file comment) */
    GArray           *pages;        /* GpuPage */
    GArray           *ranges;       /* GpuRange, by material */
    GArray           *clusters;     /* GpuCluster, in quad order */
    GArray           *loose;        /* guint billboard indices drawn immediately */
    GPtrArray        *order;        /* Billboard *: draw/shadow sort list, reused
                                     * (a fresh one per pass mmaps past ~16k) */
    guint             ebo;          /* shared quad index pattern */
    guint             gpu_quads;    /* quads uploaded */
    guint             demoted;      /* uploaded quads zeroed since */
    guint             added;        /* billboards added since the upload */
    guint             settle;       /* draws since the last add */
    gboolean          gpu_off;      /* volatile batch: immediate until cleared */
    Shader            gpu_shader;
    gboolean          gpu_tried;
    gboolean          gpu_loaded;
    gint              gpu_attr[4];  /* position, texcoord, color, corner */
    gint              loc_mvp;
    gint              loc_model;
    gint              loc_eye;
    gint              loc_spherical;
    gint              loc_cam_right;
    gint              loc_cam_up;
    gint              loc_lit;
    gint              loc_cutoff;
    gint              loc_wind;
    gint              loc_wind_time;
    LrgLightSetSampleLocations light_locs;
    LrgLightSetFogLocations gpu_fog_locs;

    /* shadows (U14.1): receiving rides the GPU program, casting has a
     * program of its own (the same quads, written as the caster shader) */
    gboolean          receive;
    gint              loc_recv_on;
    gint              loc_recv_sun;
    LrgShadowLocations recv_shadow_locs;
    LrgGroundOverlayLocations recv_overlay_locs;
    /* the loose quads' receiving (gaps G8): the cutout program's own */
    gboolean          gpu_has_bounds;   /* the settled quads' box, the batch's space */
    gfloat            gpu_min[3];
    gfloat            gpu_max[3];
    gint              loose_loc_recv_on;
    gint              loose_loc_recv_sun;
    LrgShadowLocations loose_shadow_locs;
    LrgGroundOverlayLocations loose_overlay_locs;
    Shader            cast_shader;
    gboolean          cast_tried;
    gboolean          cast_loaded;
    gint              cast_loc_mvp;
    gint              cast_loc_model;
    gint              cast_loc_eye;
    gint              cast_loc_wind;
    gint              cast_loc_wind_time;
    gint              cast_loc_mode;
    gint              cast_loc_light;
    gint              cast_loc_spherical;
    gint              cast_loc_lit;
};

static void lrg_billboard_batch_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgBillboardBatch, lrg_billboard_batch, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                lrg_billboard_batch_prewarmable_init))

/* raylib's default vertex shader inputs, plus an alpha test and the
 * light set's fog. rlVertex3f() applies the rlgl transform on the CPU,
 * so vertexPosition is already in the light set's draw space. */
/* The loose quads' normal is the facing times 1 + the share of the quad's
 * light that comes from the sun (gaps G8): the receiving fragment stage
 * reads both back. */
static const gchar cutout_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "out vec3 fragPosition;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragFacing;\n"
    "out float fragSunShare;\n"
    "void main() {\n"
    "    float len = length(vertexNormal);\n"
    "    fragFacing = len > 1e-6 ? vertexNormal / len : vec3(0.0, 0.0, 1.0);\n"
    "    fragSunShare = clamp(len - 1.0, 0.0, 1.0);\n"
    "    fragPosition = vertexPosition;\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const gchar cutout_fs[] =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragFacing;\n"
    "in float fragSunShare;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float alphaCutoff;\n"
    "uniform int recvOn;\n"
    "uniform vec3 recvSun;\n"
    LRG_FOG_GLSL
    LRG_SHADOW_GLSL
    LRG_GROUND_OVERLAY_GLSL
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    if (texel.a < alphaCutoff) discard;\n"
    "    if (recvOn != 0 && fragSunShare > 0.0) {\n"
    "        float sh = shadowCascades > 0 ? lrg_sun_shadow(fragPosition - recvSun * 0.6, fragFacing) : 1.0;\n"
    "        float snow;\n"
    "        float cloud = 0.0;\n"
    "        if (ovOn != 0) lrg_overlay_eval(fragPosition, vec3(0.0), snow, cloud);\n"
    "        texel.rgb *= (1.0 - fragSunShare * (1.0 - sh)) * (1.0 - cloud);\n"
    "    }\n"
    "    finalColor = vec4(mix(texel.rgb, fogColor.rgb, lrg_fog_amount(fragPosition)), texel.a);\n"
    "}\n";

/* The GPU path: the quad's corner from the pivot point and the facing
 * basis (compute_basis() / build_corners() in GLSL), the tint lit at
 * the quad's centre (lit_tint() in GLSL); matModel is the rlgl
 * transform the immediate path applies on the CPU. */
static const gchar gpu_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "in vec4 bbCorner;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform vec3 eyeLocal;\n"
    "uniform int spherical;\n"
    "uniform vec3 camRight;\n"
    "uniform vec3 camUp;\n"
    "uniform int lit;\n"
    "uniform vec4 wind;\n"
    "uniform vec2 windTime;\n"
    "uniform int recvOn;\n"
    "uniform vec3 recvSun;\n"
    LRG_LIGHT_SAMPLE_GLSL
    LRG_SHADOW_GLSL
    LRG_GROUND_OVERLAY_GLSL
    "out vec3 fragPosition;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec4 fragSnow;\n"
    "void main() {\n"
    "    vec3 right;\n"
    "    vec3 up;\n"
    "    if (spherical == 1) {\n"
    "        right = camRight;\n"
    "        up = camUp;\n"
    "    } else {\n"
    "        vec2 d = eyeLocal.xz - vertexPosition.xz;\n"
    "        float len = length(d);\n"
    "        right = len < 1e-6 ? vec3(1.0, 0.0, 0.0) : vec3(d.y / len, 0.0, -d.x / len);\n"
    "        up = vec3(0.0, 1.0, 0.0);\n"
    "    }\n"
    "    vec3 local = vertexPosition + right * bbCorner.x + up * bbCorner.y;\n"
    "    float emissive = mod(bbCorner.w, 2.0);\n"
    "    float sway = floor(bbCorner.w * 0.5 + 0.25) / 255.0;\n"
    "    if (sway > 0.0 && bbCorner.y > 0.0) {\n"
    "        float phase = dot(vertexPosition.xz, vec2(0.37, 0.21));\n"
    "        float bend = (wind.z + wind.w * sin(windTime.x * windTime.y + phase)) * sway * bbCorner.y;\n"
    "        local.xz += wind.xy * bend;\n"
    "    }\n"
    "    vec4 c = vertexColor;\n"
    "    vec3 world = (matModel * vec4(local, 1.0)).xyz;\n"
    "    vec4 snowOut = vec4(0.0);\n"
    "    if (lit == 1 && emissive < 0.5) {\n"
    "        vec3 centre = (matModel * vec4(vertexPosition + vec3(0.0, bbCorner.z, 0.0), 1.0)).xyz;\n"
    "        vec3 sampled = lrg_light_sample(centre);\n"
    "        c.rgb = clamp(c.rgb * sampled, 0.0, 1.0);\n"
    "        if (recvOn != 0) {\n"
    "            vec3 w = vec3(0.299, 0.587, 0.114);\n"
    "            float share = clamp(dot(sunColor, w) / max(dot(sampled, w), 1e-4), 0.0, 1.0);\n"
    "            vec3 facing = normalize((matModel * vec4(cross(right, up), 0.0)).xyz);\n"
    "            float sh = shadowCascades > 0 && share > 0.0 ? lrg_sun_shadow_f(world - recvSun * 0.6, facing, 0) : 1.0;\n"
    "            float snow = 0.0;\n"
    "            float cloud = 0.0;\n"
    "            if (ovOn != 0) lrg_overlay_eval(world, vec3(0.0, 1.0, 0.0), snow, cloud);\n"
    "            c.rgb *= (1.0 - share * (1.0 - sh)) * (1.0 - cloud * share);\n"
    "            if (sway > 0.0)\n"
    "                snowOut = vec4(clamp(ovSnowColor.rgb * sampled, 0.0, 1.0) * (1.0 - share * (1.0 - sh)),\n"
    "                               bbCorner.y > 0.0 ? snow * 0.6 : 0.0);\n"
    "        }\n"
    "    }\n"
    "    fragSnow = snowOut;\n"
    "    fragColor = c;\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragPosition = world;\n"
    "    gl_Position = mvp * vec4(local, 1.0);\n"
    "}\n";

/* Receiving (U14.1, gaps G8): the sun's shadow map and the cloud shadows
 * darken the quad by the share of its light that comes from the sun; the
 * quad is pulled toward the sun first so it does not shadow itself.  The
 * lookups run per corner in the vertex stage (a settled batch is tens of
 * thousands of quads with much overdraw, and the filtered cascade lookup
 * per fragment cost milliseconds on ultra), so a quad's shade is the
 * blend of its corners'.  Swaying quads (plants) take snow on their upper
 * corners, lit as the quad is. */
static const gchar gpu_fs[] =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec4 fragSnow;\n"
    "uniform sampler2D texture0;\n"
    "uniform float alphaCutoff;\n"
    LRG_FOG_GLSL
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * fragColor;\n"
    "    if (texel.a < alphaCutoff) discard;\n"
    "    texel.rgb = mix(texel.rgb, fragSnow.rgb, fragSnow.a);\n"
    "    finalColor = vec4(mix(texel.rgb, fogColor.rgb, lrg_fog_amount(fragPosition)), texel.a);\n"
    "}\n";

/* The caster program: gpu_vs with the facing pinned to axis-Y around the
 * light (eyeLocal is the light's side), written as the shadow renderer's
 * caster shader does: the window depth (mode 0) or the distance to the
 * light over its radius (mode 1). */
static const gchar cast_fs[] =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform int casterMode;\n"
    "uniform vec4 casterLight;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    if (texture(texture0, fragTexCoord).a < 0.5) discard;\n"
    "    float d = casterMode == 1 ? length(fragPosition - casterLight.xyz) / casterLight.w\n"
    "                              : gl_FragCoord.z;\n"
    "    finalColor = vec4(d, 0.0, 0.0, 1.0);\n"
    "}\n";

static void gpu_release (LrgBillboardBatch *self);

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
    if (self->gpu_loaded && IsWindowReady ())
        UnloadShader (self->gpu_shader);
    if (self->cast_loaded && IsWindowReady ())
        UnloadShader (self->cast_shader);
    gpu_release (self);
    g_clear_pointer (&self->pages, g_array_unref);
    g_clear_pointer (&self->ranges, g_array_unref);
    g_clear_pointer (&self->clusters, g_array_unref);
    g_clear_pointer (&self->loose, g_array_unref);
    g_clear_pointer (&self->order, g_ptr_array_unref);

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
    memset (&self->fog_locs, 0xff, sizeof self->fog_locs);   /* all -1 */
    self->lit = TRUE;
    self->pages = g_array_new (FALSE, TRUE, sizeof (GpuPage));
    self->ranges = g_array_new (FALSE, TRUE, sizeof (GpuRange));
    self->clusters = g_array_new (FALSE, TRUE, sizeof (GpuCluster));
    self->loose = g_array_new (FALSE, FALSE, sizeof (guint));
    self->order = g_ptr_array_new ();

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
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

/* touch:
 * A billboard is about to change. One already on the GPU is demoted
 * (and never uploaded again: it is one that changes):
 * its quad is zeroed in the vertex buffer and it is drawn loose from
 * now on. A batch whose uploaded quads are mostly demoted gives its
 * buffers up and stays immediate (until cleared). */
static void
touch (LrgBillboardBatch *self,
       Billboard         *b)
{
    guint     index;
    guint     page;
    guint     quad;
    GpuVertex zero[4];

    if (b->gpu_slot < 0)
        return;
    b->dynamic = TRUE;

    index = (guint)(b - (Billboard *)(gpointer)self->billboards->data);
    page = (guint)b->gpu_slot / PAGE_QUADS;
    quad = (guint)b->gpu_slot % PAGE_QUADS;
    b->gpu_slot = -1;
    g_array_append_val (self->loose, index);
    self->demoted++;

    if (self->demoted * 2 > self->gpu_quads)
    {
        self->gpu_off = TRUE;
        gpu_release (self);
        return;
    }
    if (page < self->pages->len && IsWindowReady ())
    {
        memset (zero, 0, sizeof zero);
        rlUpdateVertexBuffer (g_array_index (self->pages, GpuPage, page).vbo,
                              zero, (gint)sizeof zero, (gint)(quad * sizeof zero));
    }
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
    b.gpu_slot = -1;
    g_array_append_val (self->billboards, b);

    /* loose until the next upload */
    if (self->gpu_quads > 0)
    {
        guint index = self->billboards->len - 1;

        g_array_append_val (self->loose, index);
    }
    self->added++;
    self->settle = 0;

    return self->billboards->len - 1;
}

void
lrg_billboard_batch_clear (LrgBillboardBatch *self)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    g_array_set_size (self->billboards, 0);
    gpu_release (self);
    self->gpu_off = FALSE;
    self->added = 0;
    self->settle = 0;
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
    touch (self, b);
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
    touch (self, b);
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
    touch (self, b);
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
    touch (self, b);

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
    if (b->material == material)
        return;
    touch (self, b);
    /* a billboard that changes material animates: keep it loose */
    b->dynamic = TRUE;
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
    touch (self, b);

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
lrg_billboard_batch_set_sway (LrgBillboardBatch *self,
                              guint              index,
                              gfloat             weight)
{
    Billboard *b;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    b = billboard_at (self, index);
    g_return_if_fail (b != NULL);
    if (!isfinite (weight))
        return;
    weight = CLAMP (weight, 0.0f, 1.0f);
    if (weight == b->sway)
        return;
    touch (self, b);
    b->sway = weight;
}

gfloat
lrg_billboard_batch_get_sway (LrgBillboardBatch *self,
                              guint              index)
{
    Billboard *b;

    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), 0.0f);

    b = billboard_at (self, index);
    g_return_val_if_fail (b != NULL, 0.0f);
    return b->sway;
}

void
lrg_billboard_batch_set_wind (LrgBillboardBatch *self,
                              gfloat             direction_x,
                              gfloat             direction_z,
                              gfloat             lean,
                              gfloat             gust,
                              gfloat             frequency)
{
    gfloat len;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    if (!isfinite (direction_x) || !isfinite (direction_z) || !isfinite (lean) ||
        !isfinite (gust) || !isfinite (frequency))
        return;
    len = sqrtf (direction_x * direction_x + direction_z * direction_z);
    if (len < 1e-6f)
    {
        direction_x = 0.0f;
        direction_z = 0.0f;
    }
    else
    {
        direction_x /= len;
        direction_z /= len;
    }
    self->wind[0] = direction_x;
    self->wind[1] = direction_z;
    self->wind[2] = lean;
    self->wind[3] = gust;
    self->wind_frequency = frequency;
}

void
lrg_billboard_batch_set_wind_time (LrgBillboardBatch *self,
                                   gdouble            seconds)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));

    /* folded to an hour so a float keeps sub-frame precision (the sway
     * skips a little once an hour, unnoticeable in a gusting wind) */
    if (isfinite (seconds))
        self->wind_time = (gfloat)fmod (seconds, 3600.0);
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
    if (!b->visible == !visible)
        return;
    touch (self, b);
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
build_corners (LrgBillboardBatch *self,
               const Billboard   *b,
               const gfloat      *right,
               const gfloat      *up,
               gfloat             out[4][3])
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

    /* wind: the corners above the pivot bend downwind by their height
     * (the vertex shader does the same; the weight is quantized to
     * 1/255 there, so it is here too) */
    if (b->sway > 0.0f)
    {
        gfloat weight = floorf (b->sway * 255.0f + 0.5f) / 255.0f;
        gfloat above = b->height * (1.0f - b->pivot_y);
        gfloat phase = b->position[0] * 0.37f + b->position[2] * 0.21f;
        gfloat bend;

        if (above > 0.0f)
        {
            bend = (self->wind[2] + self->wind[3] *
                    sinf (self->wind_time * self->wind_frequency + phase)) * weight * above;
            out[2][0] += self->wind[0] * bend;
            out[2][2] += self->wind[1] * bend;
            out[3][0] += self->wind[0] * bend;
            out[3][2] += self->wind[1] * bend;
        }
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
    build_corners (self, b, right, up, out);

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
lrg_billboard_batch_set_receive_shadows (LrgBillboardBatch *self,
                                         gboolean           receive)
{
    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    self->receive = receive;
}

gboolean
lrg_billboard_batch_get_receive_shadows (LrgBillboardBatch *self)
{
    g_return_val_if_fail (LRG_IS_BILLBOARD_BATCH (self), FALSE);
    return self->receive;
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
    if (!b->emissive == !emissive)
        return;
    touch (self, b);
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

/* lit_tint_light:
 * lit_tint() that also hands back the sampled light (3 floats); FALSE
 * (and @out_light untouched) for an unlit or emissive quad. */
static gboolean
lit_tint_light (const Billboard *b,
                LrgLightSet     *lights,
                const Matrix    *transform,
                guint8          *out,
                gfloat          *out_light)
{
    GrlVector3 centre;
    gfloat     local[3];
    gfloat     light[3];
    guint      k;

    memcpy (out, b->tint, 4);
    if (lights == NULL || b->emissive)
        return FALSE;

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
    if (out_light != NULL)
        memcpy (out_light, light, sizeof light);
    return TRUE;
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
    (void)lit_tint_light (b, lights, transform, out, NULL);
}

/* lit_tint_share:
 * lit_tint() that also returns the share of the light at the quad's centre
 * that comes from the sun (@sun_rgb, NULL when it is off or not received):
 * the luminance of the sun's colour over the sampled light's, as the GPU
 * path computes it.  0 for an unlit or emissive quad. */
static gfloat
lit_tint_share (const Billboard *b,
                LrgLightSet     *lights,
                const Matrix    *transform,
                const gfloat    *sun_rgb,
                guint8          *out)
{
    gfloat light[3];
    gfloat sampled;

    if (!lit_tint_light (b, lights, transform, out, light) || sun_rgb == NULL)
        return 0.0f;
    sampled = 0.299f * light[0] + 0.587f * light[1] + 0.114f * light[2];
    return CLAMP ((0.299f * sun_rgb[0] + 0.587f * sun_rgb[1] + 0.114f * sun_rgb[2])
                  / MAX (sampled, 1e-4f), 0.0f, 1.0f);
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
    lrg_light_set_fog_locations (self->shader, &self->fog_locs);
    self->loose_loc_recv_on = GetShaderLocation (self->shader, "recvOn");
    self->loose_loc_recv_sun = GetShaderLocation (self->shader, "recvSun");
    lrg_shadow_locations (self->shader, &self->loose_shadow_locs);
    lrg_ground_overlay_locations (self->shader, &self->loose_overlay_locs);
}

static gint
compare_material (gconstpointer a,
                  gconstpointer b)
{
    guint ma = (*(Billboard * const *)a)->material;
    guint mb = (*(Billboard * const *)b)->material;

    return (ma > mb) - (ma < mb);
}

/* ------------------------------------------------------------------------ */
/* GPU path                                                                 */
/* ------------------------------------------------------------------------ */

/* gpu_release:
 * Drops the vertex pages and the index buffer (GL objects only while
 * the context exists; without one they died with it). Every billboard
 * is loose afterwards. */
static void
gpu_release (LrgBillboardBatch *self)
{
    gboolean ready = IsWindowReady ();
    guint    i;

    self->gpu_has_bounds = FALSE;

    for (i = 0; self->pages != NULL && i < self->pages->len; i++)
    {
        GpuPage *page = &g_array_index (self->pages, GpuPage, i);

        if (ready)
        {
            rlUnloadVertexArray (page->vao);
            rlUnloadVertexBuffer (page->vbo);
        }
    }
    if (self->ebo != 0 && ready)
        rlUnloadVertexBuffer (self->ebo);
    self->ebo = 0;
    if (self->pages != NULL)
        g_array_set_size (self->pages, 0);
    if (self->ranges != NULL)
        g_array_set_size (self->ranges, 0);
    if (self->clusters != NULL)
        g_array_set_size (self->clusters, 0);
    if (self->loose != NULL)
        g_array_set_size (self->loose, 0);
    for (i = 0; self->billboards != NULL && i < self->billboards->len; i++)
        g_array_index (self->billboards, Billboard, i).gpu_slot = -1;
    self->gpu_quads = 0;
    self->demoted = 0;
}

/* gpu_ensure_shader:
 * Compiles the GPU path's shader once; FALSE (and the immediate path
 * for good) when GLSL 330 is unavailable. */
static gboolean
gpu_ensure_shader (LrgBillboardBatch *self)
{
    gint unit = 0;

    if (self->gpu_tried)
        return self->gpu_loaded;
    self->gpu_tried = TRUE;

    self->gpu_shader = LoadShaderFromMemory (gpu_vs, gpu_fs);
    if (self->gpu_shader.id == 0 || self->gpu_shader.id == rlGetShaderIdDefault ())
        return FALSE;

    self->gpu_attr[0] = rlGetLocationAttrib (self->gpu_shader.id, "vertexPosition");
    self->gpu_attr[1] = rlGetLocationAttrib (self->gpu_shader.id, "vertexTexCoord");
    self->gpu_attr[2] = rlGetLocationAttrib (self->gpu_shader.id, "vertexColor");
    self->gpu_attr[3] = rlGetLocationAttrib (self->gpu_shader.id, "bbCorner");
    if (self->gpu_attr[0] < 0 || self->gpu_attr[1] < 0 || self->gpu_attr[2] < 0
        || self->gpu_attr[3] < 0)
    {
        UnloadShader (self->gpu_shader);
        return FALSE;
    }
    self->loc_mvp = GetShaderLocation (self->gpu_shader, "mvp");
    self->loc_model = GetShaderLocation (self->gpu_shader, "matModel");
    self->loc_eye = GetShaderLocation (self->gpu_shader, "eyeLocal");
    self->loc_spherical = GetShaderLocation (self->gpu_shader, "spherical");
    self->loc_cam_right = GetShaderLocation (self->gpu_shader, "camRight");
    self->loc_cam_up = GetShaderLocation (self->gpu_shader, "camUp");
    self->loc_lit = GetShaderLocation (self->gpu_shader, "lit");
    self->loc_cutoff = GetShaderLocation (self->gpu_shader, "alphaCutoff");
    self->loc_wind = GetShaderLocation (self->gpu_shader, "wind");
    self->loc_wind_time = GetShaderLocation (self->gpu_shader, "windTime");
    self->loc_recv_on = GetShaderLocation (self->gpu_shader, "recvOn");
    self->loc_recv_sun = GetShaderLocation (self->gpu_shader, "recvSun");
    lrg_shadow_locations (self->gpu_shader, &self->recv_shadow_locs);
    lrg_ground_overlay_locations (self->gpu_shader, &self->recv_overlay_locs);
    lrg_light_set_sample_locations (self->gpu_shader, &self->light_locs);
    lrg_light_set_fog_locations (self->gpu_shader, &self->gpu_fog_locs);
    SetShaderValue (self->gpu_shader, GetShaderLocation (self->gpu_shader, "texture0"),
                    &unit, SHADER_UNIFORM_INT);
    self->gpu_loaded = TRUE;
    return TRUE;
}

/* compare_index_material:
 * Orders billboard indices by material (g_array_sort() is stable, so
 * a material keeps insertion order, as the immediate path draws). */
static gint
compare_index_material (gconstpointer a,
                        gconstpointer b,
                        gpointer      user_data)
{
    GArray *billboards = user_data;
    guint   ma = g_array_index (billboards, Billboard, *(const guint *)a).material;
    guint   mb = g_array_index (billboards, Billboard, *(const guint *)b).material;

    return (ma > mb) - (ma < mb);
}

/* fill_quad:
 * The four vertices of @b in build_corners() order (bottom-left,
 * bottom-right, top-right, top-left) with the matching texture
 * corners, as the immediate path emits them. */
static void
fill_quad (const Billboard *b,
           GpuVertex       *v)
{
    gfloat left = -b->width * b->pivot_x;
    gfloat right = b->width * (1.0f - b->pivot_x);
    gfloat bottom = -b->height * b->pivot_y;
    gfloat top = b->height * (1.0f - b->pivot_y);
    gfloat cx[4];
    gfloat cy[4];
    gfloat u[4];
    gfloat w[4];
    guint  k;

    cx[0] = left;  cy[0] = bottom; u[0] = b->uv[0]; w[0] = b->uv[3];
    cx[1] = right; cy[1] = bottom; u[1] = b->uv[2]; w[1] = b->uv[3];
    cx[2] = right; cy[2] = top;    u[2] = b->uv[2]; w[2] = b->uv[1];
    cx[3] = left;  cy[3] = top;    u[3] = b->uv[0]; w[3] = b->uv[1];

    for (k = 0; k < 4; k++)
    {
        memcpy (v[k].position, b->position, sizeof v[k].position);
        v[k].uv[0] = u[k];
        v[k].uv[1] = w[k];
        memcpy (v[k].color, b->tint, 4);
        v[k].corner[0] = cx[k];
        v[k].corner[1] = cy[k];
        v[k].corner[2] = (0.5f - b->pivot_y) * b->height;
        /* w: emissive (0/1) plus twice the sway weight in 1/255 steps */
        v[k].corner[3] = (b->emissive ? 1.0f : 0.0f) +
                         2.0f * floorf (CLAMP (b->sway, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
}

/* cluster_add_quad:
 * Grows @cluster by the reach of @b: the pivot plus the farthest corner
 * offset in any direction (sqrt (cx^2 + cy^2): the facing basis is
 * orthonormal in both modes), and the most a corner above the pivot can
 * be bent by the wind (sway weight x height). */
static void
cluster_add_quad (GpuCluster      *cluster,
                  const Billboard *b)
{
    gfloat left = -b->width * b->pivot_x;
    gfloat right = b->width * (1.0f - b->pivot_x);
    gfloat bottom = -b->height * b->pivot_y;
    gfloat top = b->height * (1.0f - b->pivot_y);
    gfloat reach;
    gfloat above;
    guint  k;

    reach = sqrtf (MAX (left * left, right * right) + MAX (bottom * bottom, top * top));
    for (k = 0; k < 3; k++)
    {
        cluster->min[k] = MIN (cluster->min[k], b->position[k] - reach);
        cluster->max[k] = MAX (cluster->max[k], b->position[k] + reach);
    }
    /* the shader's sway weight is rounded to 1/255 steps */
    above = MAX (MAX (top, bottom), 0.0f);
    cluster->sway_reach = MAX (cluster->sway_reach,
                               (CLAMP (b->sway, 0.0f, 1.0f) + 0.01f) * above);
}

/* gpu_build_clusters:
 * Cuts every material run into clusters of at most CLUSTER_QUADS quads
 * and records their boxes; @order maps quad indices to billboards. */
static void
gpu_build_clusters (LrgBillboardBatch *self,
                    GArray            *order)
{
    guint r;

    g_array_set_size (self->clusters, 0);
    for (r = 0; r < self->ranges->len; r++)
    {
        GpuRange *range = &g_array_index (self->ranges, GpuRange, r);
        guint     q;

        range->cluster_first = self->clusters->len;
        for (q = 0; q < range->count; q += CLUSTER_QUADS)
        {
            GpuCluster cluster;
            guint      n = MIN (range->count - q, CLUSTER_QUADS);
            guint      i;
            guint      k;

            cluster.first = range->first + q;
            cluster.count = n;
            cluster.sway_reach = 0.0f;
            for (k = 0; k < 3; k++)
            {
                cluster.min[k] = G_MAXFLOAT;
                cluster.max[k] = -G_MAXFLOAT;
            }
            for (i = 0; i < n; i++)
            {
                guint index = g_array_index (order, guint, cluster.first + i);

                cluster_add_quad (&cluster, &g_array_index (self->billboards, Billboard, index));
            }
            g_array_append_val (self->clusters, cluster);
        }
        range->cluster_count = self->clusters->len - range->cluster_first;
    }
}

/* gpu_build:
 * Uploads every visible, unchanging billboard into vertex pages sorted
 * by material; the rest become the loose list. */
static void
gpu_build (LrgBillboardBatch *self)
{
    g_autoptr(GArray) order = NULL;
    g_autofree GpuVertex *vertices = NULL;
    g_autofree guint16 *indices = NULL;
    guint i;
    guint first;

    gpu_release (self);

    order = g_array_sized_new (FALSE, FALSE, sizeof (guint), self->billboards->len);
    for (i = 0; i < self->billboards->len; i++)
    {
        Billboard *b = &g_array_index (self->billboards, Billboard, i);

        if (b->visible && !b->dynamic)
            g_array_append_val (order, i);
        else
            g_array_append_val (self->loose, i);
    }
    self->added = 0;
    if (order->len < GPU_MIN_QUADS)
    {
        /* not worth buffers: everything stays loose */
        g_array_set_size (self->loose, 0);
        for (i = 0; i < self->billboards->len; i++)
            g_array_append_val (self->loose, i);
        return;
    }
    g_array_sort_with_data (order, compare_index_material, self->billboards);

    /* the index pattern of one full page, shared by every page */
    indices = g_new (guint16, (gsize)PAGE_QUADS * 6);
    for (i = 0; i < PAGE_QUADS; i++)
    {
        indices[i * 6 + 0] = (guint16)(i * 4 + 0);
        indices[i * 6 + 1] = (guint16)(i * 4 + 1);
        indices[i * 6 + 2] = (guint16)(i * 4 + 2);
        indices[i * 6 + 3] = (guint16)(i * 4 + 0);
        indices[i * 6 + 4] = (guint16)(i * 4 + 2);
        indices[i * 6 + 5] = (guint16)(i * 4 + 3);
    }

    vertices = g_new (GpuVertex, (gsize)MIN (order->len, PAGE_QUADS) * 4);
    for (first = 0; first < order->len; first += PAGE_QUADS)
    {
        GpuPage page;
        guint   n = MIN (order->len - first, PAGE_QUADS);
        gint    stride = (gint)sizeof (GpuVertex);

        for (i = 0; i < n; i++)
        {
            guint      index = g_array_index (order, guint, first + i);
            Billboard *b = &g_array_index (self->billboards, Billboard, index);

            fill_quad (b, vertices + (gsize)i * 4);
            b->gpu_slot = (gint)(first + i);
            /* the box the casting passes cull against (a quad reaches its
             * width or height around its pivot) */
            {
                gfloat reach = MAX (b->width, b->height);
                guint  k;

                for (k = 0; k < 3; k++)
                {
                    gfloat lo = b->position[k] - reach;
                    gfloat hi = b->position[k] + reach;

                    if (!self->gpu_has_bounds || lo < self->gpu_min[k])
                        self->gpu_min[k] = lo;
                    if (!self->gpu_has_bounds || hi > self->gpu_max[k])
                        self->gpu_max[k] = hi;
                }
                self->gpu_has_bounds = TRUE;
            }
        }

        page.quads = n;
        page.vao = rlLoadVertexArray ();
        rlEnableVertexArray (page.vao);
        page.vbo = rlLoadVertexBuffer (vertices, (gint)(n * 4 * sizeof (GpuVertex)), FALSE);
        rlSetVertexAttribute ((guint)self->gpu_attr[0], 3, RL_FLOAT, FALSE, stride,
                              (gint)G_STRUCT_OFFSET (GpuVertex, position));
        rlEnableVertexAttribute ((guint)self->gpu_attr[0]);
        rlSetVertexAttribute ((guint)self->gpu_attr[1], 2, RL_FLOAT, FALSE, stride,
                              (gint)G_STRUCT_OFFSET (GpuVertex, uv));
        rlEnableVertexAttribute ((guint)self->gpu_attr[1]);
        rlSetVertexAttribute ((guint)self->gpu_attr[2], 4, RL_UNSIGNED_BYTE, TRUE, stride,
                              (gint)G_STRUCT_OFFSET (GpuVertex, color));
        rlEnableVertexAttribute ((guint)self->gpu_attr[2]);
        rlSetVertexAttribute ((guint)self->gpu_attr[3], 4, RL_FLOAT, FALSE, stride,
                              (gint)G_STRUCT_OFFSET (GpuVertex, corner));
        rlEnableVertexAttribute ((guint)self->gpu_attr[3]);
        if (self->ebo == 0)
            self->ebo = rlLoadVertexBufferElement (indices,
                                                   (gint)((gsize)PAGE_QUADS * 6 * sizeof (guint16)),
                                                   FALSE);
        else
            rlEnableVertexBufferElement (self->ebo);
        rlDisableVertexArray ();
        g_array_append_val (self->pages, page);
    }

    /* the material runs */
    for (i = 0; i < order->len; i++)
    {
        guint material = g_array_index (self->billboards, Billboard,
                                        g_array_index (order, guint, i)).material;
        GpuRange *last = self->ranges->len > 0
                         ? &g_array_index (self->ranges, GpuRange, self->ranges->len - 1)
                         : NULL;

        if (last != NULL && last->material == material)
        {
            last->count++;
        }
        else
        {
            GpuRange range;

            range.material = material;
            range.first = i;
            range.count = 1;
            range.cluster_first = 0;
            range.cluster_count = 0;
            g_array_append_val (self->ranges, range);
        }
    }
    gpu_build_clusters (self, order);
    self->gpu_quads = order->len;
}

/* gpu_maybe_build:
 * Uploads once a large enough batch has settled (a very large one at
 * once), and again when the billboards added since outnumber an eighth
 * of the upload. */
static void
gpu_maybe_build (LrgBillboardBatch *self)
{
    if (self->gpu_off)
        return;
    if (self->settle < GPU_SETTLE_DRAWS &&
        !(self->settle >= 1 && self->added >= GPU_EAGER_QUADS))
        return;
    if (self->gpu_quads == 0)
    {
        if (self->added == 0 || self->billboards->len < GPU_MIN_QUADS)
            return;
    }
    else if (self->added <= MAX (GPU_MIN_QUADS, self->gpu_quads / 8))
    {
        return;
    }
    if (!gpu_ensure_shader (self))
    {
        self->gpu_off = TRUE;
        return;
    }
    gpu_build (self);
}

/* mat_to_array / mat_from_array:
 * A raylib Matrix by element number (m0 .. m15); the struct stores its
 * fields in another order (m0, m4, m8, m12, m1, ...). */
static void
mat_to_array (const Matrix *m,
              gfloat       *v)
{
    v[0] = m->m0;   v[1] = m->m1;   v[2] = m->m2;   v[3] = m->m3;
    v[4] = m->m4;   v[5] = m->m5;   v[6] = m->m6;   v[7] = m->m7;
    v[8] = m->m8;   v[9] = m->m9;   v[10] = m->m10; v[11] = m->m11;
    v[12] = m->m12; v[13] = m->m13; v[14] = m->m14; v[15] = m->m15;
}

static Matrix
mat_from_array (const gfloat *v)
{
    Matrix m;

    m.m0 = v[0];   m.m1 = v[1];   m.m2 = v[2];   m.m3 = v[3];
    m.m4 = v[4];   m.m5 = v[5];   m.m6 = v[6];   m.m7 = v[7];
    m.m8 = v[8];   m.m9 = v[9];   m.m10 = v[10]; m.m11 = v[11];
    m.m12 = v[12]; m.m13 = v[13]; m.m14 = v[14]; m.m15 = v[15];
    return m;
}

/* mat_mul:
 * raylib's MatrixMultiply (@a applied first, then @b):
 * result.m[4i + j] = sum over k of a.m[4i + k] * b.m[4k + j]. */
static Matrix
mat_mul (const Matrix *a,
         const Matrix *b)
{
    gfloat x[16];
    gfloat y[16];
    gfloat o[16];
    guint  i;
    guint  j;

    mat_to_array (a, x);
    mat_to_array (b, y);
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            o[i * 4 + j] = x[i * 4 + 0] * y[0 * 4 + j] + x[i * 4 + 1] * y[1 * 4 + j]
                           + x[i * 4 + 2] * y[2 * 4 + j] + x[i * 4 + 3] * y[3 * 4 + j];
    return mat_from_array (o);
}

/* gpu_draw_span:
 * Draws quads @first .. @first + @count - 1 of the pages, split at page
 * edges; *@page_bound is the vertex array bound so far. */
static void
gpu_draw_span (LrgBillboardBatch *self,
               guint              first,
               guint              count,
               guint             *page_bound)
{
    guint end = first + count;
    guint q;

    for (q = first; q < end;)
    {
        guint page = q / PAGE_QUADS;
        guint in_page = q % PAGE_QUADS;
        guint n = MIN (end - q, PAGE_QUADS - in_page);

        if (page != *page_bound)
        {
            rlEnableVertexArray (g_array_index (self->pages, GpuPage, page).vao);
            *page_bound = page;
        }
        rlDrawVertexArrayElements ((gint)(in_page * 6), (gint)(n * 6), NULL);
        q += n;
    }
}

/* gpu_draw_ranges:
 * Every material run with its texture, under the program the caller
 * bound. Clusters wholly outside the live rlgl frustum (the camera's, or
 * a shadow pass's light: the program's mvp is that same P x MV x T) are
 * skipped; the visible ones of a run are drawn in their upload order,
 * neighbours merged into one call, so what is drawn is drawn exactly as
 * before. */
static void
gpu_draw_ranges (LrgBillboardBatch *self)
{
    LrgDrawFrustum frustum;
    gboolean       cull;
    gfloat         bend;
    guint          page_bound = G_MAXUINT;
    guint          i;

    /* the most a corner can move per metre of sway reach */
    bend = sqrtf (self->wind[0] * self->wind[0] + self->wind[1] * self->wind[1])
           * (fabsf (self->wind[2]) + fabsf (self->wind[3]));
    cull = isfinite (bend) && lrg_draw_cull_frustum_current (&frustum);

    rlActiveTextureSlot (0);
    for (i = 0; i < self->ranges->len; i++)
    {
        GpuRange        *range = &g_array_index (self->ranges, GpuRange, i);
        MaterialTexture *entry;
        guint            span_first = 0;
        guint            span_count = 0;
        guint            c;

        entry = g_hash_table_lookup (self->textures, GUINT_TO_POINTER (range->material));
        if (entry == NULL)
            continue;
        rlEnableTexture (entry->texture.id);

        for (c = 0; c < range->cluster_count; c++)
        {
            GpuCluster *cl = &g_array_index (self->clusters, GpuCluster,
                                             range->cluster_first + c);

            if (cull)
            {
                gfloat pad = bend * cl->sway_reach;
                gfloat lo[3];
                gfloat hi[3];

                lo[0] = cl->min[0] - pad;
                lo[1] = cl->min[1];
                lo[2] = cl->min[2] - pad;
                hi[0] = cl->max[0] + pad;
                hi[1] = cl->max[1];
                hi[2] = cl->max[2] + pad;
                if (!lrg_draw_cull_frustum_box_visible (&frustum, lo, hi))
                    continue;
            }
            if (span_count > 0 && span_first + span_count == cl->first)
            {
                span_count += cl->count;
                continue;
            }
            if (span_count > 0)
                gpu_draw_span (self, span_first, span_count, &page_bound);
            span_first = cl->first;
            span_count = cl->count;
        }
        if (span_count > 0)
            gpu_draw_span (self, span_first, span_count, &page_bound);
    }
    rlDisableVertexArray ();
    rlDisableTexture ();
}

/* gpu_draw:
 * One indexed draw per material run (split at page edges) under the
 * GPU path's shader, with the rlgl transform, the eye in the batch's
 * space and the lights and fog of @lights (unlit without). */
static void
gpu_draw (LrgBillboardBatch *self,
          const gfloat      *camera,
          const gfloat      *cam_right,
          const gfloat      *cam_up,
          LrgLightSet       *lights)
{
    Matrix   transform;
    Matrix   modelview;
    Matrix   projection;
    Matrix   mvp;
    gint     spherical;
    gint     lit;

    /* the immediate batch so far belongs before these draws */
    rlDrawRenderBatchActive ();

    transform = rlGetMatrixTransform ();
    modelview = rlGetMatrixModelview ();
    projection = rlGetMatrixProjection ();
    mvp = mat_mul (&transform, &modelview);
    mvp = mat_mul (&mvp, &projection);

    rlEnableShader (self->gpu_shader.id);
    rlSetUniformMatrix (self->loc_mvp, mvp);
    rlSetUniformMatrix (self->loc_model, transform);
    SetShaderValue (self->gpu_shader, self->loc_eye, camera, SHADER_UNIFORM_VEC3);
    spherical = self->mode == LRG_BILLBOARD_MODE_SPHERICAL;
    SetShaderValue (self->gpu_shader, self->loc_spherical, &spherical, SHADER_UNIFORM_INT);
    SetShaderValue (self->gpu_shader, self->loc_cam_right, cam_right, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->gpu_shader, self->loc_cam_up, cam_up, SHADER_UNIFORM_VEC3);
    SetShaderValue (self->gpu_shader, self->loc_cutoff, &self->cutoff, SHADER_UNIFORM_FLOAT);
    {
        gfloat wind_time[2];

        wind_time[0] = self->wind_time;
        wind_time[1] = self->wind_frequency;
        SetShaderValue (self->gpu_shader, self->loc_wind, self->wind, SHADER_UNIFORM_VEC4);
        SetShaderValue (self->gpu_shader, self->loc_wind_time, wind_time, SHADER_UNIFORM_VEC2);
    }
    lit = lrg_light_set_upload_sample (lights, self->gpu_shader, &self->light_locs);
    SetShaderValue (self->gpu_shader, self->loc_lit, &lit, SHADER_UNIFORM_INT);
    {
        /* shadows and cloud shadows, only when asked for and the lights are there */
        gint recv = 0;

        if (lit && self->receive)
            recv = lrg_light_set_upload_receive (lights, self->gpu_shader,
                                                 &self->recv_shadow_locs,
                                                 &self->recv_overlay_locs,
                                                 self->loc_recv_sun) ? 1 : 0;
        SetShaderValue (self->gpu_shader, self->loc_recv_on, &recv, SHADER_UNIFORM_INT);
    }
    lrg_light_set_upload_fog (lights, self->gpu_shader, &self->gpu_fog_locs);

    gpu_draw_ranges (self);
    rlDisableShader ();
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

void
lrg_billboard_batch_draw (LrgBillboardBatch *self,
                          const GrlVector3  *camera_position)
{
    GPtrArray *order;
    Matrix    view;
    gfloat    camera[3];
    gfloat    cam_right[3];
    gfloat    cam_up[3];
    gboolean  use_shader;
    gboolean  receive;
    gboolean  sun_on;
    gfloat    sun_rgb[3];
    LrgLightSet *lights;
    Matrix    transform;
    guint     current;
    gboolean  open;
    guint     n_candidates;
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

    lights = active_light_set (self);

    /* the settled part on the GPU */
    if (self->settle < G_MAXUINT)
        self->settle++;
    gpu_maybe_build (self);
    if (self->gpu_quads > 0)
        gpu_draw (self, camera, cam_right, cam_up, lights);

    /* The loose part (everything without an upload): visible, textured
     * billboards grouped by material, one bind each. */
    n_candidates = self->gpu_quads > 0 ? self->loose->len : self->billboards->len;
    order = self->order;
    g_ptr_array_set_size (order, 0);
    for (i = 0; i < n_candidates; i++)
    {
        guint      index = self->gpu_quads > 0 ? g_array_index (self->loose, guint, i) : i;
        Billboard *b = &g_array_index (self->billboards, Billboard, index);

        if (b->visible && g_hash_table_contains (self->textures, GUINT_TO_POINTER (b->material)))
            g_ptr_array_add (order, b);
    }
    if (order->len == 0)
        return;
    g_ptr_array_sort (order, compare_material);

    /* rlVertex3f() applies the push/pop transform on the CPU; light the
     * centres in that same space (the space the static-geometry shader
     * lights, see lrg_light_set_sample()). */
    transform = rlGetMatrixTransform ();

    /* The shader is needed for the cutout or for the light set's fog;
     * without either the default shader blends plainly. */
    use_shader = FALSE;
    receive = lights != NULL && self->receive;
    if (self->cutoff > 0.0f || receive
        || (lights != NULL && lrg_light_set_get_fog (lights, NULL, NULL, NULL, NULL, NULL)))
    {
        ensure_shader (self);
        use_shader = self->shader_loaded;
    }
    if (use_shader)
    {
        gint recv = 0;

        BeginShaderMode (self->shader);
        SetShaderValue (self->shader, self->cutoff_loc, &self->cutoff, SHADER_UNIFORM_FLOAT);
        lrg_light_set_upload_fog (lights, self->shader, &self->fog_locs);
        /* gaps G8: the loose quads (actors, loot, a batch that never
         * settles) receive per fragment: they are few */
        if (receive)
            recv = lrg_light_set_upload_receive (lights, self->shader, &self->loose_shadow_locs,
                                                 &self->loose_overlay_locs,
                                                 self->loose_loc_recv_sun) ? 1 : 0;
        SetShaderValue (self->shader, self->loose_loc_recv_on, &recv, SHADER_UNIFORM_INT);
    }
    sun_on = receive && lrg_light_set_get_directional (lights, NULL, sun_rgb);

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
        build_corners (self, b, right, up, q);

        {
            guint8 rgba[4];
            gfloat share = 0.0f;
            gfloat scale;

            share = lit_tint_share (b, lights, &transform, sun_on ? sun_rgb : NULL, rgba);
            rlColor4ub (rgba[0], rgba[1], rgba[2], rgba[3]);
            /* facing normal = right x up (points at the camera), its
             * length 1 + the sun's share of the light (the receiving
             * shader reads it back) */
            scale = 1.0f + share;
            rlNormal3f ((right[1] * up[2] - right[2] * up[1]) * scale,
                        (right[2] * up[0] - right[0] * up[2]) * scale,
                        (right[0] * up[1] - right[1] * up[0]) * scale);
        }
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

/* ------------------------------------------------------------------------ */
/* Casting shadows                                                          */
/* ------------------------------------------------------------------------ */

/* cast_ensure_shader:
 * Compiles the caster program once (the GPU path's vertex stage, the
 * caster's fragment stage); FALSE when it did not compile. */
static gboolean
cast_ensure_shader (LrgBillboardBatch *self)
{
    gint unit = 0;

    if (self->cast_tried)
        return self->cast_loaded;
    self->cast_tried = TRUE;

    self->cast_shader = LoadShaderFromMemory (gpu_vs, cast_fs);
    if (self->cast_shader.id == 0 || self->cast_shader.id == rlGetShaderIdDefault ())
        return FALSE;
    self->cast_loc_mvp = GetShaderLocation (self->cast_shader, "mvp");
    self->cast_loc_model = GetShaderLocation (self->cast_shader, "matModel");
    self->cast_loc_eye = GetShaderLocation (self->cast_shader, "eyeLocal");
    self->cast_loc_wind = GetShaderLocation (self->cast_shader, "wind");
    self->cast_loc_wind_time = GetShaderLocation (self->cast_shader, "windTime");
    self->cast_loc_mode = GetShaderLocation (self->cast_shader, "casterMode");
    self->cast_loc_light = GetShaderLocation (self->cast_shader, "casterLight");
    self->cast_loc_spherical = GetShaderLocation (self->cast_shader, "spherical");
    self->cast_loc_lit = GetShaderLocation (self->cast_shader, "lit");
    SetShaderValue (self->cast_shader, GetShaderLocation (self->cast_shader, "texture0"),
                    &unit, SHADER_UNIFORM_INT);
    self->cast_loaded = TRUE;
    return TRUE;
}

void
lrg_billboard_batch_draw_shadow (LrgBillboardBatch *self,
                                 LrgShadowRenderer *renderer,
                                 const GrlVector3  *camera_position)
{
    GPtrArray *order;
    Matrix   transform;
    gint     mode;
    gfloat   light[4];
    gfloat   direction[3];
    gfloat   eye[3];
    gfloat   offset[3];
    gboolean far_from_light = FALSE;
    guint    i;

    g_return_if_fail (LRG_IS_BILLBOARD_BATCH (self));
    g_return_if_fail (LRG_IS_SHADOW_RENDERER (renderer));
    g_return_if_fail (camera_position != NULL);

    if (self->billboards->len == 0 || !IsWindowReady ()
        || !lrg_shadow_renderer_get_pass_state (renderer, &mode, light, direction))
        return;

    /* the side the quads face: a cube face looks at its light; a cascade
     * stands far off along the way the light came from */
    transform = rlGetMatrixTransform ();
    offset[0] = transform.m12;
    offset[1] = transform.m13;
    offset[2] = transform.m14;
    if (mode == 1)
    {
        eye[0] = light[0] - offset[0];
        eye[1] = light[1] - offset[1];
        eye[2] = light[2] - offset[2];
    }
    else
    {
        eye[0] = camera_position->x - direction[0] * 1.0e5f;
        eye[1] = camera_position->y - direction[1] * 1.0e5f;
        eye[2] = camera_position->z - direction[2] * 1.0e5f;
    }

    /* the settled quads, as they are drawn lit; a cube face's pass skips
     * a batch whose quads all lie beyond its light's reach (a town's
     * torches reach one or two of the pixels' batches) */
    if (self->settle < G_MAXUINT)
        self->settle++;
    gpu_maybe_build (self);
    if (self->gpu_quads > 0 && mode == 1 && self->gpu_has_bounds)
    {
        gfloat d2 = 0.0f;
        guint  k;

        for (k = 0; k < 3; k++)
        {
            gfloat v = eye[k];
            gfloat e = v < self->gpu_min[k] ? self->gpu_min[k] - v
                     : v > self->gpu_max[k] ? v - self->gpu_max[k] : 0.0f;

            d2 += e * e;
        }
        if (d2 > light[3] * light[3])
            far_from_light = TRUE;
    }
    if (self->gpu_quads > 0 && !far_from_light && cast_ensure_shader (self))
    {
        Matrix   modelview = rlGetMatrixModelview ();
        Matrix   projection = rlGetMatrixProjection ();
        Matrix   mvp;
        gfloat   wind_time[2];

        rlDrawRenderBatchActive ();
        mvp = mat_mul (&transform, &modelview);
        mvp = mat_mul (&mvp, &projection);
        rlEnableShader (self->cast_shader.id);
        rlSetUniformMatrix (self->cast_loc_mvp, mvp);
        rlSetUniformMatrix (self->cast_loc_model, transform);
        SetShaderValue (self->cast_shader, self->cast_loc_eye, eye, SHADER_UNIFORM_VEC3);
        wind_time[0] = self->wind_time;
        wind_time[1] = self->wind_frequency;
        SetShaderValue (self->cast_shader, self->cast_loc_wind, self->wind, SHADER_UNIFORM_VEC4);
        SetShaderValue (self->cast_shader, self->cast_loc_wind_time, wind_time,
                        SHADER_UNIFORM_VEC2);
        {
            gint spherical = 0;
            gint off = 0;

            SetShaderValue (self->cast_shader, self->cast_loc_spherical, &spherical,
                            SHADER_UNIFORM_INT);
            SetShaderValue (self->cast_shader, self->cast_loc_lit, &off, SHADER_UNIFORM_INT);
        }
        SetShaderValue (self->cast_shader, self->cast_loc_mode, &mode, SHADER_UNIFORM_INT);
        SetShaderValue (self->cast_shader, self->cast_loc_light, light, SHADER_UNIFORM_VEC4);

        gpu_draw_ranges (self);
        rlDisableShader ();
    }

    /* the loose quads (the ones that move): rebuilt facing the light
     * side, drawn under the renderer's own caster shader */
    {
        GrlShader *caster = lrg_shadow_renderer_get_caster_shader (renderer);
        guint      n_candidates = self->gpu_quads > 0 ? self->loose->len : self->billboards->len;
        guint      current = 0;
        gboolean   open = FALSE;

        if (caster == NULL)
            return;
        order = self->order;
        g_ptr_array_set_size (order, 0);
        for (i = 0; i < n_candidates; i++)
        {
            guint      index = self->gpu_quads > 0 ? g_array_index (self->loose, guint, i) : i;
            Billboard *b = &g_array_index (self->billboards, Billboard, index);

            if (b->visible && g_hash_table_contains (self->textures,
                                                     GUINT_TO_POINTER (b->material)))
                g_ptr_array_add (order, b);
        }
        if (order->len == 0)
            return;
        g_ptr_array_sort (order, compare_material);

        BeginShaderMode (*(Shader *)grl_shader_get_handle (caster));
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
            /* axis-Y around the light side, whatever the batch's mode */
            compute_basis (self, b, eye, NULL, NULL, right, up);
            build_corners (self, b, right, up, q);
            rlColor4ub (255, 255, 255, 255);
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
        rlDrawRenderBatchActive ();
        EndShaderMode ();
    }
}

/* ------------------------------------------------------------------------ */
/* Prewarm                                                                  */
/* ------------------------------------------------------------------------ */

/* lrg_billboard_batch_prewarm:
 * What the first draws build: the cutout, GPU-path and caster shaders,
 * and the GPU pages of a batch big enough for them, uploaded as if the
 * batch had settled (it has, when a loading screen runs this). Later
 * additions follow the usual rules. */
static gboolean
lrg_billboard_batch_prewarm (LrgPrewarmable *prewarmable)
{
    LrgBillboardBatch *self = LRG_BILLBOARD_BATCH (prewarmable);

    ensure_shader (self);
    if (!gpu_ensure_shader (self))
        return TRUE;
    cast_ensure_shader (self);
    if (self->settle < GPU_SETTLE_DRAWS)
        self->settle = GPU_SETTLE_DRAWS;
    gpu_maybe_build (self);
    return TRUE;
}

static void
lrg_billboard_batch_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_billboard_batch_prewarm;
}
