/* lrg-photo-dof.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-photo-dof.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-photo-dof
 * @Title: LrgPhotoDof
 * @Short_description: Thin lens depth of field for photos
 *
 * The blur radius of every pixel comes from the thin lens equation
 * (lrg_photo_dof_get_coc()); the shader gathers a golden angle spiral of
 * taps over that radius, weighting each tap by whether its own blur
 * reaches the pixel, so a sharp subject does not smear into the blurred
 * background.  The scene is drawn into an #LrgPhotoDepthTarget so the
 * shader can read the depth buffer.
 *
 * A gather blur has limits: a blurred object in front of a sharp one
 * does not bleed over it (the sharp pixel has no radius of its own).
 * For photos at a sensible aperture that is hardly visible.
 */

/* the number of taps, CPU and shader alike */
#define DOF_TAPS (32)

/* the sensor is 24 mm tall */
#define SENSOR_HEIGHT_M (0.024f)

/* the shader: the depth texture is unit 1, the colour texture unit 0 */
static const gchar dof_fs[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D depthMap;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec4 dofA;\n"   /* near, far, focus, k (blur per relative defocus, px) */
    "uniform vec3 dofB;\n"   /* max blur px, 1 / width, 1 / height */
    "out vec4 finalColor;\n"
    "float lrg_linear(float d)\n"
    "{\n"
    "    float z = d * 2.0 - 1.0;\n"
    "    return 2.0 * dofA.x * dofA.y / (dofA.y + dofA.x - z * (dofA.y - dofA.x));\n"
    "}\n"
    "float lrg_coc(float dist)\n"
    "{\n"
    "    return min(dofA.w * abs(dist - dofA.z) / max(dist, 0.0001), dofB.x);\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec4 center = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    float c0 = lrg_coc(lrg_linear(texture(depthMap, fragTexCoord).r));\n"
    "    if (c0 < 0.5)\n"
    "    {\n"
    "        finalColor = center;\n"
    "        return;\n"
    "    }\n"
    "    vec3 acc = center.rgb;\n"
    "    float wsum = 1.0;\n"
    "    for (int i = 1; i <= " "32" "; i++)\n"
    "    {\n"
    "        float t = float(i) / 32.0;\n"
    "        float r = sqrt(t) * c0;\n"
    "        float a = float(i) * 2.39996323;\n"
    "        vec2 uv = fragTexCoord + vec2(cos(a), sin(a)) * r * dofB.yz;\n"
    "        float cs = lrg_coc(lrg_linear(texture(depthMap, uv).r));\n"
    "        float w = clamp(cs - r + 1.0, 0.0, 1.0);\n"
    "        acc += texture(texture0, uv).rgb * w;\n"
    "        wsum += w;\n"
    "    }\n"
    "    finalColor = vec4(acc / wsum, center.a);\n"
    "}\n";

struct _LrgPhotoDof
{
    GObject    parent_instance;

    gboolean   enabled;
    gfloat     focus_distance;
    gfloat     aperture;
    gfloat     fov;
    gfloat     meters_per_unit;
    gfloat     max_blur;
    gfloat     strength;
    gfloat     near_clip;
    gfloat     far_clip;

    GrlShader *shader;
    gboolean   shader_tried;
    gint       loc_a;
    gint       loc_b;
    gint       loc_depth;
};

enum
{
    PROP_0,
    PROP_ENABLED,
    PROP_FOCUS_DISTANCE,
    PROP_APERTURE,
    PROP_FOV,
    PROP_METERS_PER_UNIT,
    PROP_MAX_BLUR,
    PROP_STRENGTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgPhotoDof, lrg_photo_dof, G_TYPE_OBJECT)

static gfloat
clampf (gfloat value, gfloat lo, gfloat hi)
{
    if (!isfinite (value))
        return lo;
    return value < lo ? lo : (value > hi ? hi : value);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

gboolean
lrg_photo_dof_get_enabled (LrgPhotoDof *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_DOF (self), FALSE);

    return self->enabled;
}

void
lrg_photo_dof_set_enabled (LrgPhotoDof *self,
                           gboolean     enabled)
{
    g_return_if_fail (LRG_IS_PHOTO_DOF (self));

    enabled = !!enabled;
    if (self->enabled == enabled)
        return;
    self->enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

#define FLOAT_ACCESSORS(name, field, lo, hi, prop) \
gfloat \
lrg_photo_dof_get_##name (LrgPhotoDof *self) \
{ \
    g_return_val_if_fail (LRG_IS_PHOTO_DOF (self), 0.0f); \
    return self->field; \
} \
\
void \
lrg_photo_dof_set_##name (LrgPhotoDof *self, \
                          gfloat       value) \
{ \
    g_return_if_fail (LRG_IS_PHOTO_DOF (self)); \
    value = clampf (value, lo, hi); \
    if (self->field == value) \
        return; \
    self->field = value; \
    g_object_notify_by_pspec (G_OBJECT (self), properties[prop]); \
}

FLOAT_ACCESSORS (focus_distance, focus_distance, 0.1f, 100000.0f, PROP_FOCUS_DISTANCE)
FLOAT_ACCESSORS (aperture, aperture, 1.0f, 32.0f, PROP_APERTURE)
FLOAT_ACCESSORS (fov, fov, 5.0f, 150.0f, PROP_FOV)
FLOAT_ACCESSORS (meters_per_unit, meters_per_unit, 0.001f, 1000.0f, PROP_METERS_PER_UNIT)
FLOAT_ACCESSORS (max_blur, max_blur, 1.0f, 64.0f, PROP_MAX_BLUR)
FLOAT_ACCESSORS (strength, strength, 0.1f, 64.0f, PROP_STRENGTH)

void
lrg_photo_dof_set_clip (LrgPhotoDof *self,
                        gfloat       near_clip,
                        gfloat       far_clip)
{
    g_return_if_fail (LRG_IS_PHOTO_DOF (self));

    if (!(near_clip > 0.0f) || !(far_clip > near_clip))
        return;
    self->near_clip = near_clip;
    self->far_clip = far_clip;
}

/* ==========================================================================
 * The lens
 * ========================================================================== */

gfloat
lrg_photo_dof_get_focal_length (LrgPhotoDof *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_DOF (self), 0.0f);

    /* a 24 mm tall sensor sees fov degrees vertically */
    return 12.0f / tanf (self->fov * (gfloat)G_PI / 360.0f);
}

/*
 * The blur per relative defocus |d - focus| / d, in pixels of a picture
 * @image_height tall: the aperture diameter A = f / N, the lens
 * magnification f / (s1 - f), halved to a radius, over the sensor
 * height, times the exaggeration.  Multiply by |d - focus| / d.
 */
static gfloat
coc_scale (LrgPhotoDof *self,
           gint         image_height)
{
    gfloat f;
    gfloat s1;
    gfloat diameter;

    f = lrg_photo_dof_get_focal_length (self) / 1000.0f;
    s1 = MAX (self->focus_distance * self->meters_per_unit, f * 1.01f);
    diameter = (f / self->aperture) * (f / (s1 - f));
    return 0.5f * diameter / SENSOR_HEIGHT_M * (gfloat)image_height * self->strength;
}

/* the blur limit scales with the picture's height */
static gfloat
max_px (LrgPhotoDof *self,
        gint         image_height)
{
    return self->max_blur * (gfloat)image_height / 1080.0f;
}

gfloat
lrg_photo_dof_get_coc (LrgPhotoDof *self,
                       gfloat       distance,
                       gint         image_height)
{
    gfloat coc;

    g_return_val_if_fail (LRG_IS_PHOTO_DOF (self), 0.0f);
    g_return_val_if_fail (image_height > 0, 0.0f);

    if (!(distance > 0.0f))
        distance = 0.0001f;
    coc = coc_scale (self, image_height) * fabsf (distance - self->focus_distance) / distance;
    return MIN (coc, max_px (self, image_height));
}

gboolean
lrg_photo_dof_apply_image (LrgPhotoDof  *self,
                           GrlImage     *image,
                           const gfloat *distances,
                           gsize         n_distances,
                           GError      **error)
{
    g_autofree guint8 *source = NULL;
    g_autofree gfloat *cocs = NULL;
    guint8 *pixels;
    gsize size;
    gint width;
    gint height;
    gint x;
    gint y;

    g_return_val_if_fail (LRG_IS_PHOTO_DOF (self), FALSE);
    g_return_val_if_fail (GRL_IS_IMAGE (image), FALSE);
    g_return_val_if_fail (distances != NULL, FALSE);

    if (!self->enabled)
        return TRUE;

    width = grl_image_get_width (image);
    height = grl_image_get_height (image);
    pixels = grl_image_get_pixels (image, &size);
    if (width <= 0 || height <= 0 || pixels == NULL ||
        size < (gsize)width * (gsize)height * 4 ||
        n_distances < (gsize)width * (gsize)height)
    {
        g_set_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_ARGUMENT,
                     "depth of field: %d x %d image with %" G_GSIZE_FORMAT " distances",
                     width, height, n_distances);
        return FALSE;
    }

    /* the blur of every pixel first, the taps read their neighbours' */
    source = g_memdup2 (pixels, (gsize)width * (gsize)height * 4);
    cocs = g_new (gfloat, (gsize)width * (gsize)height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            cocs[(gsize)y * (gsize)width + (gsize)x] =
                lrg_photo_dof_get_coc (self, distances[(gsize)y * (gsize)width + (gsize)x], height);

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            gsize at = (gsize)y * (gsize)width + (gsize)x;
            gfloat c0 = cocs[at];
            gfloat acc[3];
            gfloat wsum = 1.0f;
            gint i;

            if (c0 < 0.5f)
                continue;
            for (i = 0; i < 3; i++)
                acc[i] = (gfloat)source[at * 4 + (gsize)i];
            for (i = 1; i <= DOF_TAPS; i++)
            {
                gfloat r = sqrtf ((gfloat)i / (gfloat)DOF_TAPS) * c0;
                gfloat a = (gfloat)i * 2.39996323f;
                gint sx = x + (gint)floorf (cosf (a) * r + 0.5f);
                gint sy = y + (gint)floorf (sinf (a) * r + 0.5f);
                gsize sat;
                gfloat w;
                gint ch;

                /* clamped to the edge like the texture */
                sx = CLAMP (sx, 0, width - 1);
                sy = CLAMP (sy, 0, height - 1);
                sat = (gsize)sy * (gsize)width + (gsize)sx;
                w = clampf (cocs[sat] - r + 1.0f, 0.0f, 1.0f);
                for (ch = 0; ch < 3; ch++)
                    acc[ch] += (gfloat)source[sat * 4 + (gsize)ch] * w;
                wsum += w;
            }
            for (i = 0; i < 3; i++)
                pixels[at * 4 + (gsize)i] = (guint8)(acc[i] / wsum + 0.5f);
        }
    }
    return TRUE;
}

/* ==========================================================================
 * The shader
 * ========================================================================== */

static GrlShader *
get_shader (LrgPhotoDof *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;

    self->shader = grl_shader_new_from_memory (NULL, dof_fs, &error);
    if (self->shader == NULL)
    {
        g_warning ("photo dof: %s", error != NULL ? error->message : "no shader");
        return NULL;
    }
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_warning ("photo dof: the shader did not compile (GLSL 330 needed)");
        g_clear_object (&self->shader);
        return NULL;
    }
    self->loc_a = grl_shader_get_location (self->shader, "dofA");
    self->loc_b = grl_shader_get_location (self->shader, "dofB");
    self->loc_depth = grl_shader_get_location (self->shader, "depthMap");
    return self->shader;
}

gboolean
lrg_photo_dof_draw_target (LrgPhotoDof         *self,
                           LrgPhotoDepthTarget *target,
                           const GrlRectangle  *dest)
{
    Shader *handle;
    Texture2D color;
    Rectangle src;
    Rectangle dst;
    Vector2 origin = { 0.0f, 0.0f };
    GrlShader *shader;
    gint width;
    gint height;
    gint unit = 1;
    gfloat a[4];
    gfloat b[3];

    g_return_val_if_fail (LRG_IS_PHOTO_DOF (self), FALSE);
    g_return_val_if_fail (LRG_IS_PHOTO_DEPTH_TARGET (target), FALSE);
    g_return_val_if_fail (dest != NULL, FALSE);

    width = lrg_photo_depth_target_get_width (target);
    height = lrg_photo_depth_target_get_height (target);
    color.id = lrg_photo_depth_target_get_color_id (target);
    color.width = width;
    color.height = height;
    color.mipmaps = 1;
    color.format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    src.x = 0.0f;
    src.y = 0.0f;
    src.width = (gfloat)width;
    src.height = -(gfloat)height;
    dst.x = dest->x;
    dst.y = dest->y;
    dst.width = dest->width;
    dst.height = dest->height;

    shader = self->enabled ? get_shader (self) : NULL;
    if (shader == NULL)
    {
        DrawTexturePro (color, src, dst, origin, 0.0f, WHITE);
        return !self->enabled;
    }

    /* near, far, focus, blur per relative defocus; max blur, texel size */
    a[0] = self->near_clip;
    a[1] = self->far_clip;
    a[2] = self->focus_distance;
    a[3] = coc_scale (self, height);
    b[0] = max_px (self, height);
    b[1] = 1.0f / (gfloat)width;
    b[2] = 1.0f / (gfloat)height;
    handle = grl_shader_get_handle (shader);
    BeginShaderMode (*handle);
    SetShaderValue (*handle, self->loc_a, a, SHADER_UNIFORM_VEC4);
    SetShaderValue (*handle, self->loc_b, b, SHADER_UNIFORM_VEC3);
    SetShaderValue (*handle, self->loc_depth, &unit, SHADER_UNIFORM_INT);
    rlActiveTextureSlot (1);
    rlEnableTexture (lrg_photo_depth_target_get_depth_id (target));
    rlActiveTextureSlot (0);
    DrawTexturePro (color, src, dst, origin, 0.0f, WHITE);
    EndShaderMode ();
    rlActiveTextureSlot (1);
    rlDisableTexture ();
    rlActiveTextureSlot (0);
    return TRUE;
}

void
lrg_photo_dof_release_gpu (LrgPhotoDof *self)
{
    g_return_if_fail (LRG_IS_PHOTO_DOF (self));

    g_clear_object (&self->shader);
    self->shader_tried = FALSE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_photo_dof_finalize (GObject *object)
{
    g_clear_object (&LRG_PHOTO_DOF (object)->shader);

    G_OBJECT_CLASS (lrg_photo_dof_parent_class)->finalize (object);
}

static void
lrg_photo_dof_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgPhotoDof *self = LRG_PHOTO_DOF (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        g_value_set_boolean (value, self->enabled);
        break;
    case PROP_FOCUS_DISTANCE:
        g_value_set_float (value, self->focus_distance);
        break;
    case PROP_APERTURE:
        g_value_set_float (value, self->aperture);
        break;
    case PROP_FOV:
        g_value_set_float (value, self->fov);
        break;
    case PROP_METERS_PER_UNIT:
        g_value_set_float (value, self->meters_per_unit);
        break;
    case PROP_MAX_BLUR:
        g_value_set_float (value, self->max_blur);
        break;
    case PROP_STRENGTH:
        g_value_set_float (value, self->strength);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_dof_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgPhotoDof *self = LRG_PHOTO_DOF (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        lrg_photo_dof_set_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_FOCUS_DISTANCE:
        lrg_photo_dof_set_focus_distance (self, g_value_get_float (value));
        break;
    case PROP_APERTURE:
        lrg_photo_dof_set_aperture (self, g_value_get_float (value));
        break;
    case PROP_FOV:
        lrg_photo_dof_set_fov (self, g_value_get_float (value));
        break;
    case PROP_METERS_PER_UNIT:
        lrg_photo_dof_set_meters_per_unit (self, g_value_get_float (value));
        break;
    case PROP_MAX_BLUR:
        lrg_photo_dof_set_max_blur (self, g_value_get_float (value));
        break;
    case PROP_STRENGTH:
        lrg_photo_dof_set_strength (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_dof_class_init (LrgPhotoDofClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_photo_dof_finalize;
    object_class->get_property = lrg_photo_dof_get_property;
    object_class->set_property = lrg_photo_dof_set_property;

    /**
     * LrgPhotoDof:enabled:
     *
     * Whether the blur is applied.
     *
     * Since: 1.0
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled", "Enabled", "Whether the blur is applied", FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoDof:focus-distance:
     *
     * The sharp distance from the camera in world units.
     *
     * Since: 1.0
     */
    properties[PROP_FOCUS_DISTANCE] =
        g_param_spec_float ("focus-distance", "Focus distance", "The sharp distance",
                            0.1f, 100000.0f, 10.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoDof:aperture:
     *
     * The f-number.
     *
     * Since: 1.0
     */
    properties[PROP_APERTURE] =
        g_param_spec_float ("aperture", "Aperture", "The f-number",
                            1.0f, 32.0f, 4.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoDof:fov:
     *
     * The vertical field of view in degrees the focal length comes from.
     *
     * Since: 1.0
     */
    properties[PROP_FOV] =
        g_param_spec_float ("fov", "Field of view", "The vertical field of view",
                            5.0f, 150.0f, 50.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoDof:meters-per-unit:
     *
     * Metres per world unit.
     *
     * Since: 1.0
     */
    properties[PROP_METERS_PER_UNIT] =
        g_param_spec_float ("meters-per-unit", "Metres per unit", "Metres per world unit",
                            0.001f, 1000.0f, 1.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoDof:max-blur:
     *
     * The largest blur radius, in pixels of a 1080 tall picture.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_BLUR] =
        g_param_spec_float ("max-blur", "Maximum blur", "The largest blur radius",
                            1.0f, 64.0f, 24.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoDof:strength:
     *
     * The multiplier on the physical blur.
     *
     * Since: 1.0
     */
    properties[PROP_STRENGTH] =
        g_param_spec_float ("strength", "Strength", "The multiplier on the physical blur",
                            0.1f, 64.0f, 8.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_photo_dof_init (LrgPhotoDof *self)
{
    self->enabled = FALSE;
    self->focus_distance = 10.0f;
    self->aperture = 4.0f;
    self->fov = 50.0f;
    self->meters_per_unit = 1.0f;
    self->max_blur = 24.0f;
    self->strength = 8.0f;
    self->near_clip = 0.1f;
    self->far_clip = 1000.0f;
}

LrgPhotoDof *
lrg_photo_dof_new (void)
{
    return g_object_new (LRG_TYPE_PHOTO_DOF, NULL);
}
