/* lrg-palettize.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-palettize.h"
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

/**
 * SECTION:lrg-palettize
 * @Title: LrgPalettize
 * @Short_description: Palette quantization with an ordered dither
 *
 * #LrgPalettize draws a picture with the colours of a fixed palette of
 * up to 256 entries, the look of an 8-bit (VGA) game: each pixel takes
 * the nearest palette colour.  An optional ordered dither (an 8 x 8
 * Bayer matrix) moves each pixel's colour by its threshold first, so
 * gradients between palette colours become patterns instead of bands.
 *
 * The nearest colour is precomputed into a 32 x 32 x 32 lookup table
 * (rebuilt whenever the palette changes); the GLSL 330 fragment shader
 * rounds each pixel to the table's levels and samples it, so the cost is
 * the same for any palette size.  lrg_palettize_apply_rgb() is the same
 * arithmetic on the CPU, lrg_palettize_find_nearest() the exact search
 * the table is built from.
 */

#define LUT_LEVELS   (LRG_PALETTIZE_LUT_SIZE)
#define LUT_CELLS    (LUT_LEVELS * LUT_LEVELS * LUT_LEVELS)
#define LUT_WIDTH    (LUT_LEVELS * LUT_LEVELS)
#define BAYER_CELLS  (LRG_PALETTIZE_BAYER_SIZE * LRG_PALETTIZE_BAYER_SIZE)

/* the classic 8 x 8 Bayer matrix, [y][x] */
static const guint8 bayer8[BAYER_CELLS] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21
};

/* the shader mirrors lrg_palettize_apply_rgb(); the Bayer table is the
 * one above (gl_FragCoord counts rows from the bottom, so the pattern is
 * mirrored vertically against the CPU's rows: the same thresholds) */
static const gchar palettize_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform sampler2D lut;\n"
    "uniform float dither;\n"
    "uniform float ditherStrength;\n"
    "uniform float ditherScale;\n"
    "uniform float intensity;\n"
    "out vec4 finalColor;\n"
    "const float bayer[64] = float[64](\n"
    "     0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,\n"
    "    48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,\n"
    "    12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,\n"
    "    60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,\n"
    "     3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,\n"
    "    51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,\n"
    "    15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,\n"
    "    63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0);\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    vec3 c = texel.rgb;\n"
    "    if (dither > 0.5) {\n"
    "        ivec2 p = ivec2(floor(gl_FragCoord.xy / ditherScale)) & 7;\n"
    "        float t = (bayer[p.y * 8 + p.x] + 0.5) / 64.0 - 0.5;\n"
    "        c += vec3(t * ditherStrength);\n"
    "    }\n"
    "    vec3 q = floor(clamp(c, 0.0, 1.0) * 31.0 + 0.5);\n"
    "    vec2 uv = vec2((q.r + 32.0 * q.b + 0.5) / 1024.0, (q.g + 0.5) / 32.0);\n"
    "    vec3 pal = texture(lut, uv).rgb;\n"
    "    finalColor = vec4(mix(texel.rgb, pal, intensity), texel.a);\n"
    "}\n";

struct _LrgPalettize
{
    LrgPostEffect  parent_instance;

    guint8         palette[LRG_PALETTIZE_MAX_COLORS * 3];
    guint          size;          /* 0: no palette */
    guint8        *lut;           /* LUT_CELLS indices, index r + 32 b + 1024 g */

    gboolean       dither;
    gfloat         dither_strength;
    gfloat         dither_scale;

    /* GPU state, made on first draw (needs a GL context) */
    GrlShader     *shader;
    gboolean       shader_tried;
    Texture2D      lut_texture;
    gboolean       lut_dirty;
    gint           loc_lut;
    gint           loc_dither;
    gint           loc_dither_strength;
    gint           loc_dither_scale;
    gint           loc_intensity;
};

G_DEFINE_TYPE (LrgPalettize, lrg_palettize, LRG_TYPE_POST_EFFECT)

enum
{
    PROP_0,
    PROP_DITHER,
    PROP_DITHER_STRENGTH,
    PROP_DITHER_SCALE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* a table level (0 .. 31) as an 8-bit channel */
static guint8
level_to_byte (guint level)
{
    return (guint8)((level * 255 + (LUT_LEVELS - 1) / 2) / (LUT_LEVELS - 1));
}

/* the brute-force search the table is built from */
static guint
nearest (LrgPalettize *self,
         gint          r,
         gint          g,
         gint          b)
{
    guint best;
    gint best_distance;
    guint i;

    best = 0;
    best_distance = G_MAXINT;
    for (i = 0; i < self->size; i++)
    {
        gint dr = r - (gint)self->palette[i * 3];
        gint dg = g - (gint)self->palette[i * 3 + 1];
        gint db = b - (gint)self->palette[i * 3 + 2];
        gint distance = dr * dr + dg * dg + db * db;

        /* strictly smaller: the lowest index wins a tie */
        if (distance < best_distance)
        {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static void
rebuild_lut (LrgPalettize *self)
{
    guint r;
    guint g;
    guint b;

    if (self->lut == NULL)
        self->lut = g_new0 (guint8, LUT_CELLS);
    for (g = 0; g < LUT_LEVELS; g++)
        for (b = 0; b < LUT_LEVELS; b++)
            for (r = 0; r < LUT_LEVELS; r++)
                self->lut[r + LUT_LEVELS * b + LUT_WIDTH * g] =
                    (guint8)nearest (self, level_to_byte (r), level_to_byte (g), level_to_byte (b));
    self->lut_dirty = TRUE;
}

static void
release_gpu (LrgPalettize *self)
{
    /* textures belong to the context; without a window there is none */
    if (self->lut_texture.id != 0 && IsWindowReady ())
        UnloadTexture (self->lut_texture);
    memset (&self->lut_texture, 0, sizeof (self->lut_texture));
    self->lut_dirty = TRUE;
    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

/* the shader and the table texture, current; NULL when unavailable */
static GrlShader *
ensure_gpu (LrgPalettize *self)
{
    g_autoptr(GError) error = NULL;
    g_autofree guint8 *pixels = NULL;
    Shader *handle;
    Image image;
    guint i;

    if (!IsWindowReady ())
        return NULL;
    if (self->shader == NULL && !self->shader_tried)
    {
        self->shader_tried = TRUE;
        self->shader = grl_shader_new_from_memory (NULL, palettize_fs, &error);
        if (self->shader == NULL)
        {
            g_warning ("palettize: %s", error != NULL ? error->message : "no shader");
            return NULL;
        }
        handle = grl_shader_get_handle (self->shader);
        if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
        {
            g_warning ("palettize: the shader did not compile (GLSL 330 needed)");
            g_clear_object (&self->shader);
            return NULL;
        }
        self->loc_lut = grl_shader_get_location (self->shader, "lut");
        self->loc_dither = grl_shader_get_location (self->shader, "dither");
        self->loc_dither_strength = grl_shader_get_location (self->shader, "ditherStrength");
        self->loc_dither_scale = grl_shader_get_location (self->shader, "ditherScale");
        self->loc_intensity = grl_shader_get_location (self->shader, "intensity");
    }
    if (self->shader == NULL)
        return NULL;

    if (self->lut_dirty || self->lut_texture.id == 0)
    {
        /* the table as colours: column r + 32 b, row g */
        pixels = g_new (guint8, LUT_CELLS * 3);
        for (i = 0; i < LUT_CELLS; i++)
            memcpy (pixels + i * 3, self->palette + self->lut[i] * 3, 3);
        if (self->lut_texture.id != 0)
            UnloadTexture (self->lut_texture);
        image.data = pixels;
        image.width = LUT_WIDTH;
        image.height = LUT_LEVELS;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
        self->lut_texture = LoadTextureFromImage (image);
        if (self->lut_texture.id == 0)
        {
            g_warning ("palettize: the lookup table texture could not be made");
            return NULL;
        }
        SetTextureFilter (self->lut_texture, TEXTURE_FILTER_POINT);
        SetTextureWrap (self->lut_texture, TEXTURE_WRAP_CLAMP);
        self->lut_dirty = FALSE;
    }
    return self->shader;
}

/* binds the shader with its uniforms; the caller draws, then ends it */
static void
begin_shader (LrgPalettize *self)
{
    Shader *handle;

    grl_shader_begin (self->shader);
    handle = grl_shader_get_handle (self->shader);
    /* after the begin: raylib binds extra samplers at the batch's draw */
    SetShaderValueTexture (*handle, self->loc_lut, self->lut_texture);
    grl_shader_set_value_float (self->shader, self->loc_dither, self->dither ? 1.0f : 0.0f);
    grl_shader_set_value_float (self->shader, self->loc_dither_strength, self->dither_strength);
    grl_shader_set_value_float (self->shader, self->loc_dither_scale, self->dither_scale);
    grl_shader_set_value_float (self->shader, self->loc_intensity,
                                lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)));
}

/* --- the effect --- */

static gboolean
lrg_palettize_real_initialize (LrgPostEffect *effect,
                               guint          width,
                               guint          height,
                               GError       **error)
{
    (void)effect;
    (void)width;
    (void)height;
    (void)error;
    return TRUE;
}

static void
lrg_palettize_real_shutdown (LrgPostEffect *effect)
{
    release_gpu (LRG_PALETTIZE (effect));
}

static void
lrg_palettize_real_apply (LrgPostEffect *effect,
                          guint          source_texture_id,
                          guint          target_texture_id,
                          guint          width,
                          guint          height,
                          gfloat         delta_time)
{
    LrgPalettize *self = LRG_PALETTIZE (effect);
    Texture2D source;
    Rectangle src;
    Rectangle dst;
    Vector2 origin = { 0.0f, 0.0f };
    GrlShader *shader;

    (void)target_texture_id;
    (void)delta_time;

    if (source_texture_id == 0 || width == 0 || height == 0)
        return;

    source.id = source_texture_id;
    source.width = (int)width;
    source.height = (int)height;
    source.mipmaps = 1;
    source.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    src.x = 0.0f;
    src.y = 0.0f;
    src.width = (float)width;
    src.height = -(float)height;
    dst.x = 0.0f;
    dst.y = 0.0f;
    dst.width = (float)width;
    dst.height = (float)height;

    shader = lrg_palettize_is_identity (self) ? NULL : ensure_gpu (self);
    if (shader != NULL)
        begin_shader (self);
    DrawTexturePro (source, src, dst, origin, 0.0f, WHITE);
    if (shader != NULL)
        grl_shader_end (shader);
}

static const gchar *
lrg_palettize_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "palettize";
}

static void
lrg_palettize_real_resize (LrgPostEffect *effect,
                           guint          width,
                           guint          height)
{
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_palettize_finalize (GObject *object)
{
    LrgPalettize *self = LRG_PALETTIZE (object);

    release_gpu (self);
    g_clear_pointer (&self->lut, g_free);

    G_OBJECT_CLASS (lrg_palettize_parent_class)->finalize (object);
}

static void
lrg_palettize_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgPalettize *self = LRG_PALETTIZE (object);

    switch (prop_id)
    {
    case PROP_DITHER:
        g_value_set_boolean (value, self->dither);
        break;
    case PROP_DITHER_STRENGTH:
        g_value_set_float (value, self->dither_strength);
        break;
    case PROP_DITHER_SCALE:
        g_value_set_float (value, self->dither_scale);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_palettize_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgPalettize *self = LRG_PALETTIZE (object);

    switch (prop_id)
    {
    case PROP_DITHER:
        lrg_palettize_set_dither (self, g_value_get_boolean (value));
        break;
    case PROP_DITHER_STRENGTH:
        lrg_palettize_set_dither_strength (self, g_value_get_float (value));
        break;
    case PROP_DITHER_SCALE:
        lrg_palettize_set_dither_scale (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_palettize_class_init (LrgPalettizeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->finalize = lrg_palettize_finalize;
    object_class->get_property = lrg_palettize_get_property;
    object_class->set_property = lrg_palettize_set_property;

    effect_class->initialize = lrg_palettize_real_initialize;
    effect_class->shutdown = lrg_palettize_real_shutdown;
    effect_class->apply = lrg_palettize_real_apply;
    effect_class->resize = lrg_palettize_real_resize;
    effect_class->get_name = lrg_palettize_real_get_name;

    /**
     * LrgPalettize:dither:
     *
     * Whether the ordered dither runs before the palette lookup.
     */
    properties[PROP_DITHER] =
        g_param_spec_boolean ("dither", "Dither", "Ordered (Bayer) dither before the lookup",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPalettize:dither-strength:
     *
     * The dither's spread, 0 to 1 of the channel range.
     */
    properties[PROP_DITHER_STRENGTH] =
        g_param_spec_float ("dither-strength", "Dither Strength", "The dither's spread",
                            0.0f, 1.0f, 0.1f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgPalettize:dither-scale:
     *
     * Target pixels per dither cell.
     */
    properties[PROP_DITHER_SCALE] =
        g_param_spec_float ("dither-scale", "Dither Scale", "Target pixels per dither cell",
                            1.0f, 64.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_palettize_init (LrgPalettize *self)
{
    self->dither_strength = 0.1f;
    self->dither_scale = 1.0f;
    self->lut_dirty = TRUE;
    self->loc_lut = -1;
    self->loc_dither = -1;
    self->loc_dither_strength = -1;
    self->loc_dither_scale = -1;
    self->loc_intensity = -1;
}

/* --- public API --- */

LrgPalettize *
lrg_palettize_new (void)
{
    return g_object_new (LRG_TYPE_PALETTIZE, NULL);
}

gboolean
lrg_palettize_set_palette (LrgPalettize  *self,
                           GBytes        *rgb,
                           GError       **error)
{
    gconstpointer data;
    gsize length;

    g_return_val_if_fail (LRG_IS_PALETTIZE (self), FALSE);
    g_return_val_if_fail (rgb != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    data = g_bytes_get_data (rgb, &length);
    if (length == 0 || length % 3 != 0 || length > LRG_PALETTIZE_MAX_COLORS * 3)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "a palette is 1 to %d RGB triples, not %" G_GSIZE_FORMAT " bytes",
                     LRG_PALETTIZE_MAX_COLORS, length);
        return FALSE;
    }
    memcpy (self->palette, data, length);
    self->size = (guint)(length / 3);
    rebuild_lut (self);
    return TRUE;
}

void
lrg_palettize_clear_palette (LrgPalettize *self)
{
    g_return_if_fail (LRG_IS_PALETTIZE (self));

    self->size = 0;
    g_clear_pointer (&self->lut, g_free);
    self->lut_dirty = TRUE;
}

guint
lrg_palettize_get_palette_size (LrgPalettize *self)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), 0);

    return self->size;
}

gboolean
lrg_palettize_get_color (LrgPalettize *self,
                         guint         index,
                         guint8       *r,
                         guint8       *g,
                         guint8       *b)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), FALSE);

    if (index >= self->size)
        return FALSE;
    if (r != NULL)
        *r = self->palette[index * 3];
    if (g != NULL)
        *g = self->palette[index * 3 + 1];
    if (b != NULL)
        *b = self->palette[index * 3 + 2];
    return TRUE;
}

guint
lrg_palettize_find_nearest (LrgPalettize *self,
                            guint8        r,
                            guint8        g,
                            guint8        b)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), 0);

    return nearest (self, r, g, b);
}

guint
lrg_palettize_get_lut_index (LrgPalettize *self,
                             guint         r,
                             guint         g,
                             guint         b)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), 0);

    if (self->lut == NULL || r >= LUT_LEVELS || g >= LUT_LEVELS || b >= LUT_LEVELS)
        return 0;
    return self->lut[r + LUT_LEVELS * b + LUT_WIDTH * g];
}

guint
lrg_palettize_get_bayer (guint x,
                         guint y)
{
    return bayer8[(y % LRG_PALETTIZE_BAYER_SIZE) * LRG_PALETTIZE_BAYER_SIZE +
                  (x % LRG_PALETTIZE_BAYER_SIZE)];
}

gboolean
lrg_palettize_get_dither (LrgPalettize *self)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), FALSE);

    return self->dither;
}

void
lrg_palettize_set_dither (LrgPalettize *self,
                          gboolean      dither)
{
    g_return_if_fail (LRG_IS_PALETTIZE (self));

    dither = !!dither;
    if (self->dither == dither)
        return;
    self->dither = dither;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DITHER]);
}

gfloat
lrg_palettize_get_dither_strength (LrgPalettize *self)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), 0.0f);

    return self->dither_strength;
}

void
lrg_palettize_set_dither_strength (LrgPalettize *self,
                                   gfloat        strength)
{
    g_return_if_fail (LRG_IS_PALETTIZE (self));

    strength = isfinite (strength) ? CLAMP (strength, 0.0f, 1.0f) : 0.0f;
    if (self->dither_strength == strength)
        return;
    self->dither_strength = strength;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DITHER_STRENGTH]);
}

gfloat
lrg_palettize_get_dither_scale (LrgPalettize *self)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), 1.0f);

    return self->dither_scale;
}

void
lrg_palettize_set_dither_scale (LrgPalettize *self,
                                gfloat        scale)
{
    g_return_if_fail (LRG_IS_PALETTIZE (self));

    scale = isfinite (scale) ? CLAMP (scale, 1.0f, 64.0f) : 1.0f;
    if (self->dither_scale == scale)
        return;
    self->dither_scale = scale;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DITHER_SCALE]);
}

gboolean
lrg_palettize_is_identity (LrgPalettize *self)
{
    g_return_val_if_fail (LRG_IS_PALETTIZE (self), TRUE);

    return self->size == 0 || lrg_post_effect_get_intensity (LRG_POST_EFFECT (self)) <= 0.0f;
}

void
lrg_palettize_apply_rgb (LrgPalettize *self,
                         guint         x,
                         guint         y,
                         const gfloat *in,
                         gfloat       *out)
{
    gfloat original[3];
    gfloat offset;
    gfloat intensity;
    guint q[3];
    guint index;
    guint k;

    g_return_if_fail (LRG_IS_PALETTIZE (self));
    g_return_if_fail (in != NULL);
    g_return_if_fail (out != NULL);

    for (k = 0; k < 3; k++)
        original[k] = in[k];
    if (self->size == 0 || self->lut == NULL)
    {
        for (k = 0; k < 3; k++)
            out[k] = original[k];
        return;
    }

    /* the dither's offset: the threshold's centre around 0 */
    offset = 0.0f;
    if (self->dither)
        offset = (((gfloat)lrg_palettize_get_bayer (x, y) + 0.5f) / (gfloat)BAYER_CELLS - 0.5f) *
                 self->dither_strength;
    for (k = 0; k < 3; k++)
    {
        gfloat c = CLAMP (original[k] + offset, 0.0f, 1.0f);

        q[k] = (guint)floorf (c * (gfloat)(LUT_LEVELS - 1) + 0.5f);
    }
    index = self->lut[q[0] + LUT_LEVELS * q[2] + LUT_WIDTH * q[1]];
    intensity = lrg_post_effect_get_intensity (LRG_POST_EFFECT (self));
    for (k = 0; k < 3; k++)
    {
        gfloat p = (gfloat)self->palette[index * 3 + k] / 255.0f;

        out[k] = original[k] + (p - original[k]) * intensity;
    }
}

const gchar *
lrg_palettize_get_fragment_shader (void)
{
    return palettize_fs;
}

gboolean
lrg_palettize_draw_texture (LrgPalettize       *self,
                            GrlTexture         *texture,
                            const GrlRectangle *source,
                            const GrlRectangle *dest)
{
    g_autoptr(GrlColor) white = NULL;
    GrlShader *shader;
    GrlVector2 origin;
    gboolean identity;

    g_return_val_if_fail (LRG_IS_PALETTIZE (self), FALSE);
    g_return_val_if_fail (GRL_IS_TEXTURE (texture), FALSE);
    g_return_val_if_fail (source != NULL, FALSE);
    g_return_val_if_fail (dest != NULL, FALSE);

    white = grl_color_new (255, 255, 255, 255);
    origin.x = 0.0f;
    origin.y = 0.0f;
    identity = lrg_palettize_is_identity (self);
    shader = identity ? NULL : ensure_gpu (self);
    if (shader != NULL)
        begin_shader (self);
    grl_draw_texture_pro (texture, source, dest, &origin, 0.0f, white);
    if (shader != NULL)
        grl_shader_end (shader);
    return identity || shader != NULL;
}
