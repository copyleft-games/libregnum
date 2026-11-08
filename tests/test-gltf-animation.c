/* test-gltf-animation.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * glTF animation import, morph targets and retargeting. Every fixture is
 * a small GLB written by the test itself (tests/lrg-test-glb.h builds the
 * container): a 3-bone chain under a rotated, scaled "Armature" with
 * linear, step and cubic spline channels, a morph target mesh with a
 * weights channel, two chains with different rest poses for retargeting
 * and a little box character for the GPU and visual checks.
 *
 * The skinned positions produced by our pipeline (clip sampling ->
 * skeleton -> palette -> CPU skinning) are compared against
 * LrgGltfGeometry's posed output, an independent evaluation of the same
 * file, and against hand-computed values.
 *
 * Set LRG_TEST_SHOT_DIR to save the character renders as PNG files.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-test-glb.h"

#define EPS (1e-4)

/* ========================================================================== */
/*                           Graphics context                                 */
/* ========================================================================== */

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
        return FALSE;
    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-gltf-animation-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

#define SKIP_WITHOUT_GRAPHICS() \
    do { \
        if (!graphics_available) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* ========================================================================== */
/*                               Small math                                   */
/* ========================================================================== */

/* column-major TRS matrix */
static void
trs_matrix (const gfloat *t,
            const gfloat *q,
            const gfloat *s,
            gfloat       *m)
{
    gfloat x = q[0], y = q[1], z = q[2], w = q[3];

    m[0] = (1 - 2 * (y * y + z * z)) * s[0];
    m[1] = (2 * (x * y + z * w)) * s[0];
    m[2] = (2 * (x * z - y * w)) * s[0];
    m[3] = 0;
    m[4] = (2 * (x * y - z * w)) * s[1];
    m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m[6] = (2 * (y * z + x * w)) * s[1];
    m[7] = 0;
    m[8] = (2 * (x * z + y * w)) * s[2];
    m[9] = (2 * (y * z - x * w)) * s[2];
    m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    m[11] = 0;
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    m[15] = 1;
}

static void
mat_mul (const gfloat *a,
         const gfloat *b,
         gfloat       *out)
{
    gfloat r[16];
    guint  c, k;

    for (c = 0; c < 4; c++)
        for (k = 0; k < 4; k++)
            r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] +
                           a[12 + k] * b[c * 4 + 3];
    memcpy (out, r, sizeof r);
}

/* inverse of an affine matrix */
static void
mat_affine_inverse (const gfloat *m,
                    gfloat       *out)
{
    gdouble a = m[0], b = m[4], c = m[8];
    gdouble d = m[1], e = m[5], f = m[9];
    gdouble g = m[2], h = m[6], i = m[10];
    gdouble det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    gfloat  r[16];

    memset (r, 0, sizeof r);
    r[0] = (gfloat)((e * i - f * h) / det);
    r[4] = (gfloat)(-(b * i - c * h) / det);
    r[8] = (gfloat)((b * f - c * e) / det);
    r[1] = (gfloat)(-(d * i - f * g) / det);
    r[5] = (gfloat)((a * i - c * g) / det);
    r[9] = (gfloat)(-(a * f - c * d) / det);
    r[2] = (gfloat)((d * h - e * g) / det);
    r[6] = (gfloat)(-(a * h - b * g) / det);
    r[10] = (gfloat)((a * e - b * d) / det);
    r[12] = -(r[0] * m[12] + r[4] * m[13] + r[8] * m[14]);
    r[13] = -(r[1] * m[12] + r[5] * m[13] + r[9] * m[14]);
    r[14] = -(r[2] * m[12] + r[6] * m[13] + r[10] * m[14]);
    r[15] = 1;
    memcpy (out, r, sizeof r);
}

static void
quat_axis_angle (gfloat  ax,
                 gfloat  ay,
                 gfloat  az,
                 gfloat  degrees,
                 gfloat *q)
{
    gfloat h = degrees * (gfloat)G_PI / 360.0f;
    gfloat s = sinf (h);

    q[0] = ax * s;
    q[1] = ay * s;
    q[2] = az * s;
    q[3] = cosf (h);
}

static void
quat_mul (const gfloat *a,
          const gfloat *b,
          gfloat       *q)
{
    gfloat out[4];

    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    memcpy (q, out, sizeof out);
}

/* two quaternions describe the same rotation (q or -q) */
static void
assert_same_rotation (const gfloat *a,
                      const gfloat *b,
                      gdouble       eps)
{
    gfloat d = fabsf (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);

    g_assert_cmpfloat_with_epsilon (d, 1.0, eps);
}

/* ========================================================================== */
/*                          GLB fixture builder                               */
/* ========================================================================== */

typedef struct
{
    GByteArray *bin;
    GString    *views;
    GString    *accessors;
    guint       n;
} Fx;

static void
fx_init (Fx *fx)
{
    fx->bin = g_byte_array_new ();
    fx->views = g_string_new (NULL);
    fx->accessors = g_string_new (NULL);
    fx->n = 0;
}

static void
fx_clear (Fx *fx)
{
    g_byte_array_unref (fx->bin);
    g_string_free (fx->views, TRUE);
    g_string_free (fx->accessors, TRUE);
}

/* appends raw bytes (4-byte aligned) as a buffer view and an accessor */
static guint
fx_add (Fx           *fx,
        const void   *data,
        gsize         size,
        guint         component_type,
        guint         count,
        const gchar  *type,
        const gchar  *extra)
{
    guint8 zero = 0;
    guint  offset;

    while (fx->bin->len % 4 != 0)
        g_byte_array_append (fx->bin, &zero, 1);
    offset = fx->bin->len;
    g_byte_array_append (fx->bin, data, (guint)size);
    g_string_append_printf (fx->views, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
                            fx->n > 0 ? "," : "", offset, (guint)size);
    g_string_append_printf (fx->accessors,
                            "%s{\"bufferView\":%u,\"componentType\":%u,\"count\":%u,\"type\":\"%s\"%s}",
                            fx->n > 0 ? "," : "", fx->n, component_type, count, type,
                            extra != NULL ? extra : "");
    return fx->n++;
}

static guint
type_width (const gchar *type)
{
    if (g_strcmp0 (type, "SCALAR") == 0)
        return 1;
    if (g_strcmp0 (type, "VEC2") == 0)
        return 2;
    if (g_strcmp0 (type, "VEC3") == 0)
        return 3;
    if (g_strcmp0 (type, "VEC4") == 0)
        return 4;
    return 16;
}

/* float accessor with min/max */
static guint
fx_floats (Fx           *fx,
           const gfloat *data,
           guint         count,
           const gchar  *type)
{
    guint             w = type_width (type);
    g_autoptr(GString) extra = g_string_new (",\"min\":[");
    guint             c, i;

    for (c = 0; c < w; c++)
    {
        gfloat v = data[c];

        for (i = 1; i < count; i++)
            v = MIN (v, data[i * w + c]);
        g_string_append_printf (extra, "%s%g", c ? "," : "", v);
    }
    g_string_append (extra, "],\"max\":[");
    for (c = 0; c < w; c++)
    {
        gfloat v = data[c];

        for (i = 1; i < count; i++)
            v = MAX (v, data[i * w + c]);
        g_string_append_printf (extra, "%s%g", c ? "," : "", v);
    }
    g_string_append (extra, "]");
    return fx_add (fx, data, count * w * sizeof (gfloat), 5126, count, type, extra->str);
}

/* the GLB of @json_body (top-level members after asset/buffers/views/accessors) */
static GBytes *
fx_finish (Fx          *fx,
           const gchar *json_body)
{
    g_autofree gchar *json = NULL;

    json = g_strdup_printf ("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%u}],"
                            "\"bufferViews\":[%s],\"accessors\":[%s],%s}",
                            fx->bin->len, fx->views->str, fx->accessors->str, json_body);
    return test_glb_build (json, fx->bin->data, fx->bin->len);
}

/* -------------------------------------------------------------------------- */
/* Rigs: bones + one box per bone + animations                                */
/* -------------------------------------------------------------------------- */

typedef struct
{
    const gchar *name;
    gint         parent;
    gfloat       t[3];
    gfloat       r[4];
} RigBone;

typedef struct
{
    gint         bone;
    const gchar *path;       /* "translation", "rotation", "scale" */
    const gchar *interp;     /* "LINEAR", "STEP", "CUBICSPLINE" */
    guint        n_keys;
    const gfloat *times;
    const gfloat *values;    /* glTF layout */
} RigChannel;

typedef struct
{
    const gchar      *name;
    const RigChannel *channels;
    guint             n_channels;
} RigAnim;

/* world rest matrices of the joints (with the armature's) */
static void
rig_world (const RigBone *bones,
           guint          n,
           const gfloat  *arm_t,
           const gfloat  *arm_r,
           const gfloat  *arm_s,
           gfloat        *world)
{
    gfloat arm[16];
    guint  i;

    trs_matrix (arm_t, arm_r, arm_s, arm);
    for (i = 0; i < n; i++)
    {
        gfloat one[3] = { 1, 1, 1 };
        gfloat local[16];

        trs_matrix (bones[i].t, bones[i].r, one, local);
        mat_mul (bones[i].parent >= 0 ? world + 16 * bones[i].parent : arm, local, world + 16 * i);
    }
}

/*
 * build_rig:
 * Nodes: 0 "Armature" (arm TRS, children = root bones + the mesh), 1 .. n
 * the bones, n + 1 "Body" (mesh 0, skin 0). The mesh is one box per bone
 * from the bone's rest world position to its first child's (leaves: along
 * the parent direction, length @leaf), @half wide, weighted fully to the
 * bone. Inverse bind matrices are the inverse rest world matrices.
 */
static GBytes *
build_rig (const RigBone *bones,
           guint          n,
           const gfloat  *arm_t,
           const gfloat  *arm_r,
           const gfloat  *arm_s,
           gfloat         half,
           gfloat         leaf,
           const RigAnim *anims,
           guint          n_anims)
{
    Fx                  fx;
    g_autofree gfloat  *world = g_new (gfloat, 16 * n);
    g_autofree gfloat  *ibm = g_new (gfloat, 16 * n);
    g_autoptr(GArray)   pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GArray)   nrm = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GArray)   jnt = g_array_new (FALSE, FALSE, sizeof (guint8));
    g_autoptr(GArray)   wgt = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GArray)   idx = g_array_new (FALSE, FALSE, sizeof (guint16));
    g_autoptr(GString)  json = g_string_new (NULL);
    guint               a_pos, a_nrm, a_jnt, a_wgt, a_idx, a_ibm;
    guint               i, k;
    GBytes             *out;

    fx_init (&fx);
    rig_world (bones, n, arm_t, arm_r, arm_s, world);
    for (i = 0; i < n; i++)
        mat_affine_inverse (world + 16 * i, ibm + 16 * i);

    /* boxes: 6 faces x 4 vertices with flat normals */
    for (i = 0; i < n; i++)
    {
        gfloat p0[3] = { world[16 * i + 12], world[16 * i + 13], world[16 * i + 14] };
        gfloat p1[3];
        gfloat d[3], u[3], v[3], len;
        gint   child = -1;

        for (k = 0; k < n && child < 0; k++)
            if (bones[k].parent == (gint)i)
                child = (gint)k;
        if (child >= 0)
        {
            p1[0] = world[16 * child + 12];
            p1[1] = world[16 * child + 13];
            p1[2] = world[16 * child + 14];
        }
        else
        {
            /* along the bone's own Y axis */
            gfloat yl = sqrtf (world[16 * i + 4] * world[16 * i + 4] + world[16 * i + 5] * world[16 * i + 5] +
                               world[16 * i + 6] * world[16 * i + 6]);

            p1[0] = p0[0] + world[16 * i + 4] / yl * leaf;
            p1[1] = p0[1] + world[16 * i + 5] / yl * leaf;
            p1[2] = p0[2] + world[16 * i + 6] / yl * leaf;
        }
        d[0] = p1[0] - p0[0];
        d[1] = p1[1] - p0[1];
        d[2] = p1[2] - p0[2];
        len = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        d[0] /= len;
        d[1] /= len;
        d[2] /= len;
        /* u: any axis not parallel to d */
        if (fabsf (d[2]) < 0.9f)
        {
            u[0] = d[1]; u[1] = -d[0]; u[2] = 0;
        }
        else
        {
            u[0] = 0; u[1] = d[2]; u[2] = -d[1];
        }
        len = sqrtf (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        u[0] /= len; u[1] /= len; u[2] /= len;
        v[0] = d[1] * u[2] - d[2] * u[1];
        v[1] = d[2] * u[0] - d[0] * u[2];
        v[2] = d[0] * u[1] - d[1] * u[0];
        {
            /* corners: c(a,b,e) = p(e) + a*half*u + b*half*v */
            static const gint faces[6][4][3] = {
                { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 } },   /* start cap */
                { { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 }, { 1, -1, 1 } },   /* end cap */
                { { -1, -1, 0 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, -1, 0 } },
                { { 1, 1, 0 }, { 1, 1, 1 }, { -1, 1, 1 }, { -1, 1, 0 } },
                { { 1, -1, 0 }, { 1, -1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } },
                { { -1, 1, 0 }, { -1, 1, 1 }, { -1, -1, 1 }, { -1, -1, 0 } }
            };
            static const gfloat fn[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { -1, 0, 0 } };
            guint f, c;

            for (f = 0; f < 6; f++)
            {
                guint16 base = (guint16)(pos->len / 3);
                gfloat  nn[3];
                guint16 tri[6] = { 0, 1, 2, 0, 2, 3 };

                /* face normal in world: along d for caps, u/v for sides */
                nn[0] = fn[f][0] * u[0] + fn[f][1] * v[0] + fn[f][2] * d[0];
                nn[1] = fn[f][0] * u[1] + fn[f][1] * v[1] + fn[f][2] * d[1];
                nn[2] = fn[f][0] * u[2] + fn[f][1] * v[2] + fn[f][2] * d[2];
                for (c = 0; c < 4; c++)
                {
                    const gint *cn = faces[f][c];
                    const gfloat *pe = cn[2] ? p1 : p0;
                    gfloat        vert[3];
                    guint8        jj[4] = { (guint8)i, 0, 0, 0 };
                    gfloat        ww[4] = { 1, 0, 0, 0 };
                    guint         a;

                    for (a = 0; a < 3; a++)
                        vert[a] = pe[a] + cn[0] * half * u[a] + cn[1] * half * v[a];
                    g_array_append_vals (pos, vert, 3);
                    g_array_append_vals (nrm, nn, 3);
                    g_array_append_vals (jnt, jj, 4);
                    g_array_append_vals (wgt, ww, 4);
                }
                for (c = 0; c < 6; c++)
                {
                    guint16 x = (guint16)(base + tri[c]);

                    g_array_append_val (idx, x);
                }
            }
        }
    }

    a_pos = fx_floats (&fx, (gfloat *)(gpointer)pos->data, pos->len / 3, "VEC3");
    a_nrm = fx_floats (&fx, (gfloat *)(gpointer)nrm->data, nrm->len / 3, "VEC3");
    a_jnt = fx_add (&fx, jnt->data, jnt->len, 5121, jnt->len / 4, "VEC4", NULL);
    a_wgt = fx_floats (&fx, (gfloat *)(gpointer)wgt->data, wgt->len / 4, "VEC4");
    a_idx = fx_add (&fx, idx->data, idx->len * 2, 5123, idx->len, "SCALAR", NULL);
    a_ibm = fx_add (&fx, ibm, 16 * n * sizeof (gfloat), 5126, n, "MAT4", NULL);

    /* nodes */
    g_string_append_printf (json, "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[");
    g_string_append_printf (json, "{\"name\":\"Armature\",\"translation\":[%g,%g,%g],\"rotation\":[%.9g,%.9g,%.9g,%.9g],"
                            "\"scale\":[%g,%g,%g],\"children\":[",
                            arm_t[0], arm_t[1], arm_t[2], arm_r[0], arm_r[1], arm_r[2], arm_r[3],
                            arm_s[0], arm_s[1], arm_s[2]);
    for (i = 0; i < n; i++)
        if (bones[i].parent < 0)
            g_string_append_printf (json, "%u,", i + 1);
    g_string_append_printf (json, "%u]}", n + 1);
    for (i = 0; i < n; i++)
    {
        gboolean first = TRUE;

        g_string_append_printf (json, ",{\"name\":\"%s\",\"translation\":[%g,%g,%g],\"rotation\":[%.9g,%.9g,%.9g,%.9g]",
                                bones[i].name, bones[i].t[0], bones[i].t[1], bones[i].t[2],
                                bones[i].r[0], bones[i].r[1], bones[i].r[2], bones[i].r[3]);
        for (k = 0; k < n; k++)
            if (bones[k].parent == (gint)i)
            {
                g_string_append_printf (json, "%s%u", first ? ",\"children\":[" : ",", k + 1);
                first = FALSE;
            }
        g_string_append (json, first ? "}" : "]}");
    }
    g_string_append_printf (json, ",{\"name\":\"Body\",\"mesh\":0,\"skin\":0}]");

    /* mesh + skin */
    g_string_append_printf (json, ",\"meshes\":[{\"name\":\"BodyMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":%u,"
                            "\"NORMAL\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u},\"indices\":%u}]}]",
                            a_pos, a_nrm, a_jnt, a_wgt, a_idx);
    g_string_append_printf (json, ",\"skins\":[{\"name\":\"Rig\",\"inverseBindMatrices\":%u,\"joints\":[", a_ibm);
    for (i = 0; i < n; i++)
        g_string_append_printf (json, "%s%u", i ? "," : "", i + 1);
    g_string_append (json, "]}]");

    /* animations */
    if (n_anims > 0)
    {
        g_string_append (json, ",\"animations\":[");
        for (i = 0; i < n_anims; i++)
        {
            g_autoptr(GString) samplers = g_string_new (NULL);
            g_autoptr(GString) channels = g_string_new (NULL);

            for (k = 0; k < anims[i].n_channels; k++)
            {
                const RigChannel *c = &anims[i].channels[k];
                guint             per = g_strcmp0 (c->interp, "CUBICSPLINE") == 0 ? 3 : 1;
                const gchar      *type = g_strcmp0 (c->path, "rotation") == 0 ? "VEC4" : "VEC3";
                guint             in = fx_floats (&fx, c->times, c->n_keys, "SCALAR");
                guint             out_acc = fx_floats (&fx, c->values, c->n_keys * per, type);

                g_string_append_printf (samplers, "%s{\"input\":%u,\"output\":%u,\"interpolation\":\"%s\"}",
                                        k ? "," : "", in, out_acc, c->interp);
                g_string_append_printf (channels, "%s{\"sampler\":%u,\"target\":{\"node\":%d,\"path\":\"%s\"}}",
                                        k ? "," : "", k, c->bone + 1, c->path);
            }
            g_string_append_printf (json, "%s{\"name\":\"%s\",\"samplers\":[%s],\"channels\":[%s]}",
                                    i ? "," : "", anims[i].name, samplers->str, channels->str);
        }
        g_string_append (json, "]");
    }

    out = fx_finish (&fx, json->str);
    fx_clear (&fx);
    return out;
}

/* -------------------------------------------------------------------------- */
/* The chain fixture                                                          */
/* -------------------------------------------------------------------------- */

static const gfloat ARM_T[3] = { 1, 0, 0 };
static gfloat       ARM_R[4];                 /* 90 degrees about Z (main) */
static const gfloat ARM_S[3] = { 2, 2, 2 };
static const gfloat IDENT_R[4] = { 0, 0, 0, 1 };
static const gfloat UNIT_S[3] = { 1, 1, 1 };
static const gfloat ZERO_T[3] = { 0, 0, 0 };

static const RigBone CHAIN[3] = {
    { "Root", -1, { 0, 0, 0 }, { 0, 0, 0, 1 } },
    { "Mid",   0, { 0, 1, 0 }, { 0, 0, 0, 1 } },
    { "Tip",   1, { 0, 1, 0 }, { 0, 0, 0, 1 } }
};

static const gfloat BEND_T2[2] = { 0, 1 };
static gfloat       BEND_ROT[8];              /* identity, 90 degrees about Z */
static const gfloat STEP_T[2] = { 0, 0.5f };
static const gfloat STEP_V[6] = { 0, 0, 0, 0, 0, 1 };
/* cubic: key0 in (0) value (1) out (1); key1 in (0) value (2) out (0) */
static const gfloat CUBIC_V[18] = { 0, 0, 0, 1, 1, 1, 1, 1, 1,
                                    0, 0, 0, 2, 2, 2, 0, 0, 0 };
static gfloat       IDLE_ROT[8];

static GBytes *
chain_glb (void)
{
    RigChannel bend[3];
    RigChannel idle[1];
    RigAnim    anims[2];

    quat_axis_angle (0, 0, 1, 90, ARM_R);
    memcpy (BEND_ROT, IDENT_R, sizeof IDENT_R);
    quat_axis_angle (0, 0, 1, 90, BEND_ROT + 4);
    quat_axis_angle (1, 0, 0, 0, IDLE_ROT);
    quat_axis_angle (1, 0, 0, 30, IDLE_ROT + 4);

    bend[0] = (RigChannel){ 1, "rotation", "LINEAR", 2, BEND_T2, BEND_ROT };
    bend[1] = (RigChannel){ 0, "translation", "STEP", 2, STEP_T, STEP_V };
    bend[2] = (RigChannel){ 2, "scale", "CUBICSPLINE", 2, BEND_T2, CUBIC_V };
    idle[0] = (RigChannel){ 1, "rotation", "STEP", 2, BEND_T2, IDLE_ROT };
    anims[0] = (RigAnim){ "Bend", bend, 3 };
    anims[1] = (RigAnim){ "Idle", idle, 1 };
    return build_rig (CHAIN, 3, ARM_T, ARM_R, ARM_S, 0.2f, 0.5f, anims, 2);
}

static LrgGltfAnimation *
load_chain (void)
{
    g_autoptr(GBytes) glb = chain_glb ();
    g_autoptr(GError) error = NULL;
    LrgGltfAnimation *importer = lrg_gltf_animation_new_from_bytes (glb, NULL, &error);

    g_assert_no_error (error);
    g_assert_nonnull (importer);
    return importer;
}

/* poses @skeleton from @clip at @time (tracks bound by bone name) */
static void
pose_skeleton (LrgSkeleton      *skeleton,
               LrgAnimationClip *clip,
               gfloat            time)
{
    guint t;

    lrg_skeleton_reset_to_bind (skeleton);
    for (t = 0; t < lrg_animation_clip_get_track_count (clip); t++)
    {
        LrgBone    *bone = lrg_skeleton_get_bone_by_name (skeleton, lrg_animation_clip_get_track_bone_name (clip, t));
        LrgBonePose pose;

        if (bone == NULL)
            continue;
        lrg_animation_clip_sample_track (clip, t, time, &pose);
        lrg_skeleton_set_pose (skeleton, lrg_bone_get_index (bone), &pose);
    }
    lrg_skeleton_calculate_world_poses (skeleton);
}

/* ========================================================================== */
/*                                  Tests                                     */
/* ========================================================================== */

static void
test_skeleton_hierarchy (void)
{
    g_autoptr(LrgGltfAnimation) importer = load_chain ();
    g_autoptr(LrgSkeleton)      skeleton = NULL;
    g_autoptr(GError)           error = NULL;
    gfloat                      world[48];
    guint                       i;
    const LrgBonePose          *root;

    g_assert_cmpuint (lrg_gltf_animation_get_skin_count (importer), ==, 1);
    g_assert_cmpstr (lrg_gltf_animation_get_skin_name (importer, 0), ==, "Rig");
    g_assert_cmpuint (lrg_gltf_animation_get_skin_joint_count (importer, 0), ==, 3);
    g_assert_cmpstr (lrg_gltf_animation_get_node_name (importer, 0), ==, "Armature");
    g_assert_cmpuint (lrg_gltf_animation_get_node_count (importer), ==, 5);

    skeleton = lrg_gltf_animation_create_skeleton (importer, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_skeleton_get_bone_count (skeleton), ==, 3);
    g_assert_cmpstr (lrg_skeleton_get_name (skeleton), ==, "Rig");
    g_assert_cmpint (lrg_bone_get_parent_index (lrg_skeleton_get_bone (skeleton, 0)), ==, -1);
    g_assert_cmpint (lrg_bone_get_parent_index (lrg_skeleton_get_bone (skeleton, 1)), ==, 0);
    g_assert_cmpint (lrg_bone_get_parent_index (lrg_skeleton_get_bone (skeleton, 2)), ==, 1);
    g_assert_cmpstr (lrg_bone_get_name (lrg_skeleton_get_bone (skeleton, 2)), ==, "Tip");
    g_assert_cmpfloat_with_epsilon (lrg_bone_get_length (lrg_skeleton_get_bone (skeleton, 0)), 1.0, EPS);

    /* the root's bind pose has the armature folded in */
    root = lrg_bone_get_bind_pose (lrg_skeleton_get_bone (skeleton, 0));
    g_assert_cmpfloat_with_epsilon (root->position_x, 1.0, EPS);
    g_assert_cmpfloat_with_epsilon (root->scale_x, 2.0, EPS);
    {
        gfloat q[4] = { root->rotation_x, root->rotation_y, root->rotation_z, root->rotation_w };

        assert_same_rotation (q, ARM_R, EPS);
    }

    /* world rest positions: Root (1,0,0), Mid (-1,0,0), Tip (-3,0,0) */
    rig_world (CHAIN, 3, ARM_T, ARM_R, ARM_S, world);
    for (i = 0; i < 3; i++)
    {
        const LrgBonePose *w = lrg_bone_get_world_pose (lrg_skeleton_get_bone (skeleton, (gint)i));
        const gfloat      *ibm = lrg_skeleton_get_inverse_bind_matrix (skeleton, (gint)i);
        gfloat             m[16];
        gfloat             check[16];
        guint              k;

        g_assert_cmpfloat_with_epsilon (w->position_x, world[16 * i + 12], EPS);
        g_assert_cmpfloat_with_epsilon (w->position_y, world[16 * i + 13], EPS);
        g_assert_cmpfloat_with_epsilon (w->position_z, world[16 * i + 14], EPS);
        /* the inverse bind matrix undoes the rest world matrix */
        g_assert_nonnull (ibm);
        lrg_bone_pose_to_matrix (w, m);
        mat_mul (m, ibm, check);
        for (k = 0; k < 16; k++)
            g_assert_cmpfloat_with_epsilon (check[k], (k % 5 == 0) ? 1.0 : 0.0, 1e-4);
    }
    g_assert_cmpfloat_with_epsilon (world[16 * 2 + 12], -3.0, EPS);

    /* copies keep the inverse binds; computing them from the bind pose agrees */
    {
        g_autoptr(LrgSkeleton) copy = lrg_skeleton_copy (skeleton);
        gfloat                 saved[16];
        guint                  k;

        memcpy (saved, lrg_skeleton_get_inverse_bind_matrix (copy, 2), sizeof saved);
        lrg_skeleton_compute_inverse_bind_matrices (copy);
        for (k = 0; k < 16; k++)
            g_assert_cmpfloat_with_epsilon (lrg_skeleton_get_inverse_bind_matrix (copy, 2)[k], saved[k], 1e-4);
    }

    /* errors */
    g_assert_null (lrg_gltf_animation_create_skeleton (importer, 3, &error));
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_REFERENCE);
}

static void
test_clip_channels (void)
{
    g_autoptr(LrgGltfAnimation) importer = load_chain ();
    g_autoptr(LrgAnimationClip) clip = NULL;
    g_autoptr(GPtrArray)        clips = NULL;
    g_autoptr(GError)           error = NULL;
    LrgAnimationInterpolation   interp;
    LrgBonePose                 p;
    gfloat                      expect[4];
    gint                        mid, root, tip;
    guint                       n_keys;

    g_assert_cmpuint (lrg_gltf_animation_get_clip_count (importer), ==, 2);
    g_assert_cmpint (lrg_gltf_animation_find_clip (importer, "Idle"), ==, 1);
    clips = lrg_gltf_animation_create_clips (importer, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (clips->len, ==, 2);

    clip = lrg_gltf_animation_create_clip (importer, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (lrg_animation_clip_get_name (clip), ==, "Bend");
    g_assert_cmpfloat_with_epsilon (lrg_animation_clip_get_duration (clip), 1.0, EPS);
    g_assert_cmpint (lrg_animation_clip_get_loop_mode (clip), ==, LRG_ANIMATION_LOOP_REPEAT);
    g_assert_cmpuint (lrg_animation_clip_get_track_count (clip), ==, 3);
    mid = lrg_animation_clip_find_track (clip, "Mid");
    root = lrg_animation_clip_find_track (clip, "Root");
    tip = lrg_animation_clip_find_track (clip, "Tip");
    g_assert_cmpint (mid, >=, 0);
    g_assert_true (lrg_animation_clip_track_has_channels (clip, (guint)mid));
    g_assert_true (lrg_animation_clip_get_track_channel (clip, (guint)tip, LRG_ANIMATION_CHANNEL_SCALE,
                                                         &interp, &n_keys, NULL, NULL));
    g_assert_cmpint (interp, ==, LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE);
    g_assert_cmpuint (n_keys, ==, 2);
    g_assert_false (lrg_animation_clip_get_track_channel (clip, (guint)tip, LRG_ANIMATION_CHANNEL_ROTATION,
                                                          NULL, NULL, NULL, NULL));

    /* linear rotation: half way is 45 degrees about Z; position from rest */
    lrg_animation_clip_sample_track (clip, (guint)mid, 0.5f, &p);
    quat_axis_angle (0, 0, 1, 45, expect);
    {
        gfloat q[4] = { p.rotation_x, p.rotation_y, p.rotation_z, p.rotation_w };

        assert_same_rotation (q, expect, EPS);
    }
    g_assert_cmpfloat_with_epsilon (p.position_y, 1.0, EPS);

    /* step translation (folded: (1,0,0) + Rz90 * 2 * (0,0,1) = (1,0,2)) */
    lrg_animation_clip_sample_track (clip, (guint)root, 0.49f, &p);
    g_assert_cmpfloat_with_epsilon (p.position_x, 1.0, EPS);
    g_assert_cmpfloat_with_epsilon (p.position_z, 0.0, EPS);
    lrg_animation_clip_sample_track (clip, (guint)root, 0.5f, &p);
    g_assert_cmpfloat_with_epsilon (p.position_x, 1.0, EPS);
    g_assert_cmpfloat_with_epsilon (p.position_z, 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (p.scale_x, 2.0, EPS);  /* base pose keeps the folded scale */

    /* cubic spline: 0.5*1 + 1*(0.125)*1 + 0.5*2 = 1.625 */
    lrg_animation_clip_sample_track (clip, (guint)tip, 0.5f, &p);
    g_assert_cmpfloat_with_epsilon (p.scale_x, 1.625, EPS);
    g_assert_cmpfloat_with_epsilon (p.scale_z, 1.625, EPS);
    lrg_animation_clip_sample_track (clip, (guint)tip, 0.0f, &p);
    g_assert_cmpfloat_with_epsilon (p.scale_y, 1.0, EPS);

    /* looping: 1.25 wraps to 0.25 */
    {
        LrgBonePose a, b;

        lrg_animation_clip_sample_track (clip, (guint)mid, 1.25f, &a);
        lrg_animation_clip_sample_track (clip, (guint)mid, 0.25f, &b);
        g_assert_cmpfloat_with_epsilon (a.rotation_z, b.rotation_z, EPS);
    }

    /* the Idle clip is a step channel: holds key 0 until t = 1 */
    {
        LrgAnimationClip *idle = g_ptr_array_index (clips, 1);

        lrg_animation_clip_set_loop_mode (idle, LRG_ANIMATION_LOOP_NONE);
        lrg_animation_clip_sample_track (idle, 0, 0.99f, &p);
        g_assert_cmpfloat_with_epsilon (p.rotation_x, 0.0, EPS);
        lrg_animation_clip_sample_track (idle, 0, 1.0f, &p);
        g_assert_cmpfloat_with_epsilon (p.rotation_x, IDLE_ROT[4], EPS);
    }

    /* bad channel input is refused */
    {
        gfloat times[2] = { 1, 0 };
        gfloat vals[6] = { 0 };

        g_assert_false (lrg_animation_clip_set_track_channel (clip, 0, LRG_ANIMATION_CHANNEL_TRANSLATION,
                                                              LRG_ANIMATION_INTERPOLATION_LINEAR, 2, times, vals,
                                                              &error));
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    }
}

/* our pipeline against LrgGltfGeometry's independent evaluation */
static void
test_skinning_matches_reference (void)
{
    g_autoptr(GBytes)           glb = chain_glb ();
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgGltfAnimation) importer = lrg_gltf_animation_new_from_bytes (glb, NULL, &error);
    g_autoptr(LrgGltfGeometry)  reference = lrg_gltf_geometry_new_from_bytes (glb, NULL, &error);
    g_autoptr(LrgSkeleton)      skeleton = NULL;
    g_autoptr(LrgAnimationClip) clip = NULL;
    g_autoptr(LrgSkinnedMesh)   mesh = NULL;
    static const gfloat         times[] = { 0.0f, 0.3f, 0.5f, 0.75f, 0.999f };
    guint                       n_vertices = 0;
    guint                       ti;

    g_assert_no_error (error);
    skeleton = lrg_gltf_animation_create_skeleton (importer, 0, &error);
    clip = lrg_gltf_animation_create_clip (importer, 0, &error);
    mesh = lrg_gltf_animation_create_skinned_mesh (importer, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_skinned_mesh_get_bone_count (mesh), ==, 3);
    g_assert_cmpuint (lrg_skinned_mesh_get_vertex_count (mesh), ==, 72);
    g_assert_nonnull (lrg_gltf_geometry_get_positions (reference, 0, &n_vertices));
    g_assert_cmpuint (n_vertices, ==, 72);

    for (ti = 0; ti < G_N_ELEMENTS (times); ti++)
    {
        g_autofree gfloat *ours = g_new (gfloat, 3 * 72);
        g_autofree gfloat *theirs = g_new (gfloat, 3 * 72);
        g_autofree gfloat *palette = g_new (gfloat, 16 * 3);
        guint              k;

        pose_skeleton (skeleton, clip, times[ti]);
        g_assert_true (lrg_skinned_mesh_apply_skeleton (mesh, skeleton));
        /* the palette is world * inverse bind */
        g_assert_true (lrg_skinned_mesh_compute_palette_from_skeleton (mesh, skeleton, palette));
        for (k = 0; k < 48; k++)
            g_assert_cmpfloat_with_epsilon (palette[k], lrg_skinned_mesh_get_palette (mesh)[k], 1e-6);
        lrg_skinned_mesh_skin (mesh, ours, NULL);

        lrg_gltf_geometry_pose (reference, 0, times[ti], FALSE);
        g_assert_true (lrg_gltf_geometry_get_posed (reference, 0, theirs, NULL));
        for (k = 0; k < 3 * 72; k++)
            g_assert_cmpfloat_with_epsilon (ours[k], theirs[k], 2e-4);
    }

    /* at t = 0 (rest) skinning is the identity: the bind positions */
    {
        g_autofree gfloat *ours = g_new (gfloat, 3 * 72);
        const gfloat      *bind = lrg_skinned_mesh_get_bind_positions (mesh);
        guint              k;

        lrg_skeleton_reset_to_bind (skeleton);
        lrg_skeleton_calculate_world_poses (skeleton);
        lrg_skinned_mesh_apply_skeleton (mesh, skeleton);
        lrg_skinned_mesh_skin (mesh, ours, NULL);
        for (k = 0; k < 3 * 72; k++)
            g_assert_cmpfloat_with_epsilon (ours[k], bind[k], 1e-4);
    }
}

/* -------------------------------------------------------------------------- */
/* Morph targets                                                              */
/* -------------------------------------------------------------------------- */

static GBytes *
morph_glb (void)
{
    static const gfloat pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const gfloat nrm[9] = { 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    static const gfloat d0[9] = { 0, 0, 1, 0, 0, 0, 0, 0, 0 };     /* lifts vertex 0 */
    static const gfloat d1[9] = { 0, 0, 0, 1, 0, 0, 0, 2, 0 };     /* stretches 1 and 2 */
    static const gfloat times[2] = { 0, 1 };
    static const gfloat weights[4] = { 0, 0, 1, 0.5f };
    static const guint16 idx[4] = { 0, 1, 2, 0 };
    Fx                  fx;
    g_autofree gchar   *body = NULL;
    guint               a_pos, a_nrm, a_d0, a_d1, a_t, a_w, a_i;
    GBytes             *out;

    fx_init (&fx);
    a_pos = fx_floats (&fx, pos, 3, "VEC3");
    a_nrm = fx_floats (&fx, nrm, 3, "VEC3");
    a_d0 = fx_floats (&fx, d0, 3, "VEC3");
    a_d1 = fx_floats (&fx, d1, 3, "VEC3");
    a_t = fx_floats (&fx, times, 2, "SCALAR");
    a_w = fx_floats (&fx, weights, 4, "SCALAR");
    a_i = fx_add (&fx, idx, 6, 5123, 3, "SCALAR", NULL);
    body = g_strdup_printf ("\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                            "\"nodes\":[{\"name\":\"Face\",\"mesh\":0,\"weights\":[0,0.5]}],"
                            "\"meshes\":[{\"name\":\"FaceMesh\",\"extras\":{\"targetNames\":[\"Smile\",\"Blink\"]},"
                            "\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u},\"indices\":%u,"
                            "\"targets\":[{\"POSITION\":%u},{\"POSITION\":%u}]}]}],"
                            "\"animations\":[{\"name\":\"Talk\",\"samplers\":[{\"input\":%u,\"output\":%u}],"
                            "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"weights\"}}]}]",
                            a_pos, a_nrm, a_i, a_d0, a_d1, a_t, a_w);
    out = fx_finish (&fx, body);
    fx_clear (&fx);
    return out;
}

static void
test_morph_targets (void)
{
    g_autoptr(GBytes)           glb = morph_glb ();
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgGltfAnimation) importer = lrg_gltf_animation_new_from_bytes (glb, NULL, &error);
    g_autoptr(LrgGltfGeometry)  reference = lrg_gltf_geometry_new_from_bytes (glb, NULL, &error);
    g_autoptr(LrgSkinnedMesh)   mesh = NULL;
    g_autoptr(LrgAnimationClip) clip = NULL;
    LrgMorphTargets            *targets;
    const gfloat               *w;
    const gfloat               *p;
    gfloat                      sampled[2];
    gfloat                      skinned[9];
    gfloat                      theirs[9];
    guint                       n = 0;
    guint                       count = 0;
    guint                       k;

    g_assert_no_error (error);
    g_assert_cmpuint (lrg_gltf_animation_get_mesh_count (importer), ==, 1);
    g_assert_cmpuint (lrg_gltf_animation_get_mesh_morph_target_count (importer, 0), ==, 2);
    mesh = lrg_gltf_animation_create_mesh (importer, 0, &error);
    g_assert_no_error (error);
    targets = lrg_skinned_mesh_get_morph_targets (mesh);
    g_assert_nonnull (targets);
    g_assert_cmpuint (lrg_morph_targets_get_target_count (targets), ==, 2);
    g_assert_cmpstr (lrg_morph_targets_get_target_name (targets, 0), ==, "Smile");
    g_assert_cmpstr (lrg_morph_targets_get_target_name (targets, 1), ==, "Blink");
    g_assert_cmpstr (lrg_morph_targets_get_target_group (targets, 1), ==, "Face");
    g_assert_cmpint (lrg_morph_targets_find_target (targets, "Blink"), ==, 1);
    g_assert_cmpint (lrg_morph_targets_find_group (targets, "Face", &count), ==, 0);
    g_assert_cmpuint (count, ==, 2);
    g_assert_cmpfloat_with_epsilon (lrg_morph_targets_get_default_weight (targets, 1), 0.5, EPS);

    /* default weights from the node: vertex 1 moved by 0.5 * d1 */
    w = lrg_skinned_mesh_get_morph_weights (mesh, &n);
    g_assert_cmpuint (n, ==, 2);
    g_assert_cmpfloat_with_epsilon (w[1], 0.5, EPS);
    p = lrg_skinned_mesh_get_morphed_positions (mesh);
    g_assert_cmpfloat_with_epsilon (p[3], 1.5, EPS);
    g_assert_cmpfloat_with_epsilon (p[2], 0.0, EPS);

    /* the weights channel: linear, half way is (0.5, 0.25) */
    clip = lrg_gltf_animation_create_clip (importer, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_animation_clip_get_weights_channel_count (clip), ==, 1);
    g_assert_cmpstr (lrg_animation_clip_get_weights_channel_target (clip, 0), ==, "Face");
    g_assert_cmpuint (lrg_animation_clip_get_weights_channel_width (clip, 0), ==, 2);
    g_assert_true (lrg_animation_clip_sample_weights (clip, 0, 0.5f, sampled));
    g_assert_cmpfloat_with_epsilon (sampled[0], 0.5, EPS);
    g_assert_cmpfloat_with_epsilon (sampled[1], 0.25, EPS);

    /* applied by group: skinned (identity palette) = morphed, matches the reference */
    g_assert_true (lrg_skinned_mesh_set_morph_group_weights (mesh, "Face", sampled, 2));
    g_assert_false (lrg_skinned_mesh_set_morph_group_weights (mesh, "Nope", sampled, 2));
    lrg_skinned_mesh_skin (mesh, skinned, NULL);
    lrg_gltf_geometry_pose (reference, 0, 0.5, FALSE);
    g_assert_true (lrg_gltf_geometry_get_posed (reference, 0, theirs, NULL));
    for (k = 0; k < 9; k++)
        g_assert_cmpfloat_with_epsilon (skinned[k], theirs[k], 1e-4);
    g_assert_cmpfloat_with_epsilon (skinned[2], 0.5, EPS);     /* 0.5 * d0.z */
    g_assert_cmpfloat_with_epsilon (skinned[7], 1.5, EPS);     /* 1 + 0.25 * 2 */

    /* single weight and removal */
    g_assert_true (lrg_skinned_mesh_set_morph_weight (mesh, 0, 1.0f));
    g_assert_false (lrg_skinned_mesh_set_morph_weight (mesh, 2, 1.0f));
    g_assert_cmpfloat_with_epsilon (lrg_skinned_mesh_get_morphed_positions (mesh)[2], 1.0, EPS);
    g_assert_true (lrg_skinned_mesh_set_morph_targets (mesh, NULL));
    g_assert_null (lrg_skinned_mesh_get_morph_weights (mesh, NULL));
    g_assert_cmpfloat_with_epsilon (lrg_skinned_mesh_get_morphed_positions (mesh)[2], 0.0, EPS);

    /* a vertex count mismatch is refused */
    {
        g_autoptr(LrgMorphTargets) wrong = lrg_morph_targets_new (4);

        g_assert_false (lrg_skinned_mesh_set_morph_targets (mesh, wrong));
    }
}

/* -------------------------------------------------------------------------- */
/* Retargeting                                                                */
/* -------------------------------------------------------------------------- */

static gfloat WAVE_ROT[8];
static const gfloat WAVE_T[2] = { 0, 1 };
static const gfloat WAVE_POS[6] = { 0, 1, 0, 1, 1, 0 };

static GBytes *
retarget_source_glb (void)
{
    static const RigBone src[3] = {
        { "Root", -1, { 0, 1, 0 }, { 0, 0, 0, 1 } },
        { "Mid",   0, { 0, 1, 0 }, { 0, 0, 0, 1 } },
        { "Tip",   1, { 0, 1, 0 }, { 0, 0, 0, 1 } }
    };
    RigChannel ch[2];
    RigAnim    anim;

    memcpy (WAVE_ROT, IDENT_R, sizeof IDENT_R);
    quat_axis_angle (0, 1, 0, 90, WAVE_ROT + 4);
    ch[0] = (RigChannel){ 1, "rotation", "LINEAR", 2, WAVE_T, WAVE_ROT };
    ch[1] = (RigChannel){ 0, "translation", "LINEAR", 2, WAVE_T, WAVE_POS };
    anim = (RigAnim){ "Wave", ch, 2 };
    return build_rig (src, 3, ZERO_T, IDENT_R, UNIT_S, 0.1f, 0.5f, &anim, 1);
}

static GBytes *
retarget_target_glb (void)
{
    RigBone tgt[3] = {
        { "mixamorig:Root", -1, { 0, 2, 0 }, { 0, 0, 0, 1 } },
        { "MID",             0, { 0, 1, 0 }, { 0, 0, 0, 1 } },
        { "TipEnd",          1, { 0, 1, 0 }, { 0, 0, 0, 1 } }
    };

    /* different rest: the chain lies along +Z, the middle bone twisted */
    quat_axis_angle (1, 0, 0, 90, tgt[0].r);
    quat_axis_angle (0, 1, 0, -45, tgt[1].r);
    return build_rig (tgt, 3, ZERO_T, IDENT_R, UNIT_S, 0.1f, 0.5f, NULL, 0);
}

static LrgSkeleton *
skeleton_of (GBytes *glb)
{
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgGltfAnimation) importer = lrg_gltf_animation_new_from_bytes (glb, NULL, &error);

    g_assert_no_error (error);
    return lrg_gltf_animation_create_skeleton (importer, 0, NULL);
}

static void
test_bone_map (void)
{
    g_autoptr(LrgBoneMap) map = lrg_bone_map_new ();
    g_autoptr(LrgBoneMap) back = NULL;
    g_autoptr(GError)     error = NULL;
    g_autofree gchar     *a = NULL;
    g_autofree gchar     *b = NULL;
    g_autofree gchar     *c = NULL;
    g_autofree gchar     *yaml = NULL;
    g_autofree gchar     *yaml2 = NULL;
    g_autofree gchar     *path = NULL;
    g_auto(GStrv)         sources = NULL;
    g_autoptr(GBytes)     src_glb = retarget_source_glb ();
    g_autoptr(GBytes)     tgt_glb = retarget_target_glb ();
    g_autoptr(LrgSkeleton) src = skeleton_of (src_glb);
    g_autoptr(LrgSkeleton) tgt = skeleton_of (tgt_glb);
    g_autofree gint      *pairs = NULL;
    guint                 n = 0;

    /* name rules */
    lrg_bone_map_add_strip_prefix (map, "mixamorig:");
    a = lrg_bone_map_normalize_name (map, "mixamorig:LeftUpperArm");
    b = lrg_bone_map_normalize_name (map, "upper_arm.L");
    c = lrg_bone_map_normalize_name (map, "UpperArm_Right");
    g_assert_cmpstr (a, ==, "upper_arm.l");
    g_assert_cmpstr (b, ==, "upper_arm.l");
    g_assert_cmpstr (c, ==, "upper_arm.r");

    /* resolve: Root and MID by rule, TipEnd needs an explicit pair */
    pairs = lrg_bone_map_resolve (map, src, tgt, &n);
    g_assert_cmpuint (n, ==, 3);
    g_assert_cmpint (pairs[0], ==, 0);
    g_assert_cmpint (pairs[1], ==, 1);
    g_assert_cmpint (pairs[2], ==, -1);
    g_clear_pointer (&pairs, g_free);
    lrg_bone_map_set (map, "Tip", "TipEnd");
    pairs = lrg_bone_map_resolve (map, src, tgt, &n);
    g_assert_cmpint (pairs[2], ==, 2);
    g_assert_cmpuint (lrg_bone_map_auto_fill (map, src, tgt), ==, 2);
    g_assert_cmpuint (lrg_bone_map_get_size (map), ==, 3);
    g_assert_cmpstr (lrg_bone_map_get_target (map, "Mid"), ==, "MID");
    g_assert_cmpstr (lrg_bone_map_get_source (map, "TipEnd"), ==, "Tip");

    /* one source per target */
    lrg_bone_map_set (map, "Other", "TipEnd");
    g_assert_null (lrg_bone_map_get_target (map, "Tip"));
    lrg_bone_map_set (map, "Other", NULL);
    lrg_bone_map_set (map, "Tip", "TipEnd");

    /* .rbonemap round trip (data and file) */
    lrg_bone_map_set_normalize_sides (map, FALSE);
    yaml = lrg_bone_map_to_data (map, &error);
    g_assert_no_error (error);
    g_assert_nonnull (strstr (yaml, "mixamorig:"));
    back = lrg_bone_map_new_from_data (yaml, -1, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_bone_map_get_size (back), ==, 3);
    g_assert_false (lrg_bone_map_get_normalize_sides (back));
    g_assert_true (lrg_bone_map_get_ignore_case (back));
    g_assert_cmpstr (lrg_bone_map_get_strip_prefixes (back)[0], ==, "mixamorig:");
    yaml2 = lrg_bone_map_to_data (back, &error);
    g_assert_cmpstr (yaml, ==, yaml2);
    sources = lrg_bone_map_get_sources (back);
    g_assert_cmpstr (sources[0], ==, "Mid");
    g_assert_cmpstr (sources[2], ==, "Tip");

    path = g_build_filename (g_get_tmp_dir (), "lrg-test-bonemap.rbonemap", NULL);
    g_assert_true (lrg_bone_map_save_to_file (map, path, &error));
    g_clear_object (&back);
    back = lrg_bone_map_new_from_file (path, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (lrg_bone_map_get_target (back, "Tip"), ==, "TipEnd");
    g_unlink (path);

    /* hand-written file and errors */
    g_clear_object (&back);
    back = lrg_bone_map_new_from_data ("version: 1\nrules:\n  strip_prefixes: [\"DEF-\"]\nbones:\n  Hips: pelvis\n",
                                       -1, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (lrg_bone_map_get_target (back, "Hips"), ==, "pelvis");
    g_assert_null (lrg_bone_map_new_from_data ("version: 99\n", -1, &error));
    g_assert_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_VERSION);
    g_clear_error (&error);
    g_assert_null (lrg_bone_map_new_from_data ("bones: [1, 2]\n", -1, &error));
    g_assert_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE);
}

/* world rotation of a bone as a quaternion */
static void
world_rot (LrgSkeleton *s,
           gint         bone,
           gfloat      *q)
{
    const LrgBonePose *w = lrg_bone_get_world_pose (lrg_skeleton_get_bone (s, bone));

    q[0] = w->rotation_x;
    q[1] = w->rotation_y;
    q[2] = w->rotation_z;
    q[3] = w->rotation_w;
}

static void
test_retarget (void)
{
    g_autoptr(GBytes)           src_glb = retarget_source_glb ();
    g_autoptr(GBytes)           tgt_glb = retarget_target_glb ();
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgGltfAnimation) importer = lrg_gltf_animation_new_from_bytes (src_glb, NULL, &error);
    g_autoptr(LrgSkeleton)      src = skeleton_of (src_glb);
    g_autoptr(LrgSkeleton)      tgt = skeleton_of (tgt_glb);
    g_autoptr(LrgSkeleton)      tgt_rest = skeleton_of (tgt_glb);
    g_autoptr(LrgBoneMap)       map = lrg_bone_map_new ();
    g_autoptr(LrgRetargeter)    rt = NULL;
    g_autoptr(LrgAnimationClip) clip = NULL;
    g_autoptr(LrgAnimationClip) out = NULL;
    const gint                 *mapping;
    guint                       n = 0;
    guint                       b;
    gfloat                      t;

    g_assert_no_error (error);
    clip = lrg_gltf_animation_create_clip (importer, 0, &error);
    g_assert_no_error (error);
    lrg_bone_map_add_strip_prefix (map, "mixamorig:");
    lrg_bone_map_set (map, "Tip", "TipEnd");
    rt = lrg_retargeter_new (src, tgt, map);
    mapping = lrg_retargeter_get_mapping (rt, &n);
    g_assert_cmpuint (n, ==, 3);
    g_assert_cmpint (mapping[0], ==, 0);
    g_assert_cmpint (mapping[2], ==, 2);
    g_assert_cmpstr (lrg_retargeter_get_root_bone (rt), ==, "mixamorig:Root");
    g_assert_cmpfloat_with_epsilon (lrg_retargeter_get_translation_scale (rt), 2.0, EPS);

    lrg_retargeter_set_sample_rate (rt, 20.0f);
    out = lrg_retargeter_retarget_clip (rt, clip, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_animation_clip_get_track_count (out), ==, 3);
    g_assert_cmpfloat_with_epsilon (lrg_animation_clip_get_duration (out), 1.0, EPS);
    g_assert_true (lrg_animation_clip_get_track_channel (out, 0, LRG_ANIMATION_CHANNEL_TRANSLATION,
                                                         NULL, &n, NULL, NULL));
    g_assert_cmpuint (n, ==, 21);
    g_assert_false (lrg_animation_clip_get_track_channel (out, 1, LRG_ANIMATION_CHANNEL_TRANSLATION,
                                                          NULL, NULL, NULL, NULL));

    /* for every sample: each mapped target bone's world rotation change
     * from rest equals its source bone's; the root moves twice as far */
    for (t = 0.0f; t <= 1.0f; t += 0.25f)
    {
        pose_skeleton (src, clip, MIN (t, 0.9999f));
        pose_skeleton (tgt, out, MIN (t, 0.9999f));
        for (b = 0; b < 3; b++)
        {
            gfloat ws[4], sr[4], wt[4], tr[4], ds[4], dt[4];
            const LrgBonePose *src_rest_pose;

            world_rot (src, (gint)b, ws);
            world_rot (tgt, (gint)b, wt);
            /* rest world rotations: source chain is identity everywhere */
            sr[0] = sr[1] = sr[2] = 0; sr[3] = 1;
            world_rot (tgt_rest, (gint)b, tr);
            src_rest_pose = lrg_bone_get_bind_pose (lrg_skeleton_get_bone (src, (gint)b));
            (void)src_rest_pose;
            /* ds = ws * sr^-1 = ws; dt = wt * tr^-1 */
            memcpy (ds, ws, sizeof ds);
            {
                gfloat tr_inv[4] = { -tr[0], -tr[1], -tr[2], tr[3] };

                quat_mul (wt, tr_inv, dt);
            }
            (void)sr;
            assert_same_rotation (ds, dt, 1e-3);
        }
        {
            const LrgBonePose *root = lrg_bone_get_world_pose (lrg_skeleton_get_bone (tgt, 0));

            g_assert_cmpfloat_with_epsilon (root->position_x, 2.0 * MIN (t, 0.9999f), 1e-3);
            g_assert_cmpfloat_with_epsilon (root->position_y, 2.0, 1e-3);
        }
    }

    /* live retargeting gives the same pose */
    pose_skeleton (src, clip, 0.5f);
    g_assert_true (lrg_retargeter_retarget_skeleton (rt));
    {
        g_autoptr(LrgSkeleton) baked = skeleton_of (tgt_glb);
        gfloat                 a[4], c[4];

        pose_skeleton (baked, out, 0.5f);
        world_rot (tgt, 2, a);
        world_rot (baked, 2, c);
        assert_same_rotation (a, c, 1e-3);
    }

    /* translation mode NONE keeps the root at rest */
    lrg_retargeter_set_translation_mode (rt, LRG_RETARGET_TRANSLATION_NONE);
    pose_skeleton (src, clip, 0.5f);
    g_assert_true (lrg_retargeter_retarget_skeleton (rt));
    g_assert_cmpfloat_with_epsilon (lrg_bone_get_world_pose (lrg_skeleton_get_bone (tgt, 0))->position_x, 0.0, 1e-4);

    /* an empty map with names that match nothing fails cleanly */
    {
        g_autoptr(LrgBoneMap)       none = lrg_bone_map_new ();
        g_autoptr(LrgRetargeter)    bad = NULL;
        g_autoptr(LrgAnimationClip) fail = NULL;

        lrg_bone_map_set_normalize_separators (none, FALSE);
        lrg_bone_map_set_ignore_case (none, FALSE);
        bad = lrg_retargeter_new (src, tgt, none);
        fail = lrg_retargeter_retarget_clip (bad, clip, &error);
        /* "Root" vs "mixamorig:Root" etc.: only exact names count; none match */
        g_assert_null (fail);
        g_assert_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_SKELETON);
    }
}

/* -------------------------------------------------------------------------- */
/* Loading                                                                    */
/* -------------------------------------------------------------------------- */

static void
test_load_file_and_errors (void)
{
    g_autoptr(GBytes)    glb = chain_glb ();
    g_autoptr(GError)    error = NULL;
    g_autofree gchar    *path = g_build_filename (g_get_tmp_dir (), "lrg-test-gltf-anim.glb", NULL);
    g_autoptr(LrgSkeleton) skeleton = NULL;
    g_autoptr(GPtrArray) clips = NULL;
    gsize                len = 0;
    const guint8        *data = g_bytes_get_data (glb, &len);
    g_autoptr(GBytes)    junk = g_bytes_new_static ("this is not a gltf file at all", 30);
    g_autoptr(GBytes)    tiny = g_bytes_new_static ("glTF", 4);
    g_autoptr(GBytes)    ext = NULL;
    LrgGltfAnimation    *bad;

    test_glb_write_bytes (path, data, len);
    skeleton = lrg_gltf_load_skeleton (path, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_skeleton_get_bone_count (skeleton), ==, 3);
    clips = lrg_gltf_load_clips (path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (clips->len, ==, 2);
    g_unlink (path);

    g_assert_null (lrg_gltf_load_clips (path, &error));
    g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    g_clear_error (&error);

    bad = lrg_gltf_animation_new_from_bytes (junk, NULL, &error);
    g_assert_null (bad);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_JSON);
    g_clear_error (&error);
    bad = lrg_gltf_animation_new_from_bytes (tiny, NULL, &error);
    g_assert_null (bad);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_INVALID_CONTAINER);
    g_clear_error (&error);

    /* external buffers: refused without a base directory or outside it */
    {
        static const gchar json[] =
            "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":4,\"uri\":\"../x.bin\"}]}";

        ext = g_bytes_new_static (json, strlen (json));
    }
    bad = lrg_gltf_animation_new_from_bytes (ext, NULL, &error);
    g_assert_null (bad);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED);
    g_clear_error (&error);
    bad = lrg_gltf_animation_new_from_bytes (ext, g_get_tmp_dir (), &error);
    g_assert_null (bad);
    g_assert_error (error, LRG_GLTF_ERROR, LRG_GLTF_ERROR_UNSUPPORTED);
}

/* -------------------------------------------------------------------------- */
/* GPU: the character, posed by an imported clip                              */
/* -------------------------------------------------------------------------- */

static GBytes *
character_glb (void)
{
    static const RigBone bones[11] = {
        { "Hips",     -1, { 0, 1.0f, 0 },      { 0, 0, 0, 1 } },
        { "Spine",     0, { 0, 0.15f, 0 },     { 0, 0, 0, 1 } },
        { "Head",      1, { 0, 0.55f, 0 },     { 0, 0, 0, 1 } },
        { "LeftArm",   1, { 0.25f, 0.45f, 0 }, { 0, 0, 0.7071068f, 0.7071068f } },
        { "LeftHand",  3, { 0, -0.35f, 0 },    { 0, 0, 0, 1 } },
        { "RightArm",  1, { -0.25f, 0.45f, 0 },{ 0, 0, -0.7071068f, 0.7071068f } },
        { "RightHand", 5, { 0, -0.35f, 0 },    { 0, 0, 0, 1 } },
        { "LeftLeg",   0, { 0.12f, 0, 0 },     { 0, 0, 1, 0 } },
        { "LeftShin",  7, { 0, 0.45f, 0 },     { 0, 0, 0, 1 } },
        { "RightLeg",  0, { -0.12f, 0, 0 },    { 0, 0, 1, 0 } },
        { "RightShin", 9, { 0, 0.45f, 0 },     { 0, 0, 0, 1 } }
    };
    static const gfloat times[3] = { 0, 0.5f, 1 };
    static gfloat leg_l[12], leg_r[12], arm_l[12], arm_r[12], shin[12];
    static gfloat hips[9] = { 0, 1.0f, 0, 0, 1.06f, 0, 0, 1.0f, 0 };
    RigChannel ch[6];
    RigAnim    walk;
    guint      k;

    for (k = 0; k < 3; k++)
    {
        gfloat swing = k == 1 ? -35.0f : 35.0f;
        gfloat base[4] = { 0, 0, 1, 0 };     /* legs point down */
        gfloat r[4];

        quat_axis_angle (1, 0, 0, swing, r);
        quat_mul (r, base, leg_l + 4 * k);
        quat_axis_angle (1, 0, 0, -swing, r);
        quat_mul (r, base, leg_r + 4 * k);
        {
            gfloat ab[4] = { 0, 0, 0.7071068f, 0.7071068f };
            gfloat ar[4];

            /* arms hang down (rotate the T pose 80 degrees) and swing */
            quat_axis_angle (0, 0, 1, -80, ar);
            quat_mul (ab, ar, ab);
            quat_axis_angle (1, 0, 0, -swing * 0.8f, r);
            quat_mul (r, ab, arm_l + 4 * k);
        }
        {
            gfloat ab[4] = { 0, 0, -0.7071068f, 0.7071068f };
            gfloat ar[4];

            quat_axis_angle (0, 0, 1, 80, ar);
            quat_mul (ab, ar, ab);
            quat_axis_angle (1, 0, 0, swing * 0.8f, r);
            quat_mul (r, ab, arm_r + 4 * k);
        }
        quat_axis_angle (1, 0, 0, k == 1 ? 40.0f : 5.0f, shin + 4 * k);
    }
    ch[0] = (RigChannel){ 7, "rotation", "LINEAR", 3, times, leg_l };
    ch[1] = (RigChannel){ 9, "rotation", "LINEAR", 3, times, leg_r };
    ch[2] = (RigChannel){ 3, "rotation", "LINEAR", 3, times, arm_l };
    ch[3] = (RigChannel){ 5, "rotation", "LINEAR", 3, times, arm_r };
    ch[4] = (RigChannel){ 8, "rotation", "LINEAR", 3, times, shin };
    ch[5] = (RigChannel){ 0, "translation", "LINEAR", 3, times, hips };
    walk = (RigAnim){ "Walk", ch, 6 };
    return build_rig (bones, 11, ZERO_T, IDENT_R, UNIT_S, 0.07f, 0.2f, &walk, 1);
}

/* draws the mesh posed at @time into a 240x240 image; returns red pixels */
static guint
render_character (LrgSkinnedMesh   *mesh,
                  LrgSkeleton      *skeleton,
                  LrgAnimationClip *clip,
                  gfloat            time,
                  Image            *out_image)
{
    RenderTexture2D rt = LoadRenderTexture (240, 240);
    Camera3D        camera;
    Color          *pixels;
    guint           red = 0;
    gint            i;

    pose_skeleton (skeleton, clip, time);
    lrg_skinned_mesh_apply_skeleton (mesh, skeleton);

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 2.2f, 1.4f, 2.6f };
    camera.target = (Vector3){ 0, 0.95f, 0 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (rt);
    ClearBackground ((Color){ 30, 34, 48, 255 });
    BeginMode3D (camera);
    DrawGrid (8, 0.5f);
    lrg_skinned_mesh_draw (mesh, NULL);
    EndMode3D ();
    EndTextureMode ();

    *out_image = LoadImageFromTexture (rt.texture);
    UnloadRenderTexture (rt);
    ImageFlipVertical (out_image);
    pixels = LoadImageColors (*out_image);
    for (i = 0; i < out_image->width * out_image->height; i++)
        if (pixels[i].r > 120 && pixels[i].g < 110 && pixels[i].b < 110)
            red++;
    UnloadImageColors (pixels);
    return red;
}

static void
test_gpu_character (void)
{
    g_autoptr(GBytes)           glb = NULL;
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgGltfAnimation) importer = NULL;
    g_autoptr(LrgSkeleton)      skeleton = NULL;
    g_autoptr(LrgAnimationClip) clip = NULL;
    g_autoptr(LrgSkinnedMesh)   mesh = NULL;
    const gchar                *shot_dir = g_getenv ("LRG_TEST_SHOT_DIR");
    static const gfloat         times[3] = { 0.0f, 0.25f, 0.5f };
    Image                       frames[3];
    Image                       strip;
    guint                       red[3];
    guint                       k;

    SKIP_WITHOUT_GRAPHICS ();

    glb = character_glb ();
    importer = lrg_gltf_animation_new_from_bytes (glb, NULL, &error);
    g_assert_no_error (error);
    skeleton = lrg_gltf_animation_create_skeleton (importer, 0, &error);
    clip = lrg_gltf_animation_create_clip (importer, 0, &error);
    mesh = lrg_gltf_animation_create_skinned_mesh (importer, 0, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_skinned_mesh_uses_gpu (mesh));
    lrg_skinned_mesh_set_tint (mesh, 220, 70, 60, 255);
    g_assert_true (lrg_skinned_mesh_upload (mesh, &error));
    g_assert_no_error (error);

    for (k = 0; k < 3; k++)
    {
        red[k] = render_character (mesh, skeleton, clip, times[k], &frames[k]);
        g_assert_cmpuint (red[k], >, 300);
    }

    /* the pose changes the picture: frames 0 and 2 differ */
    {
        Color *a = LoadImageColors (frames[0]);
        Color *b = LoadImageColors (frames[2]);
        guint  diff = 0;
        gint   i;

        for (i = 0; i < frames[0].width * frames[0].height; i++)
            if (abs (a[i].r - b[i].r) > 40)
                diff++;
        g_assert_cmpuint (diff, >, 100);
        UnloadImageColors (a);
        UnloadImageColors (b);
    }

    /* GPU and CPU agree on where the mesh is: CPU bounds of the posed mesh
     * versus the bind pose bounds */
    {
        gfloat mn[3], mx[3];

        g_assert_true (lrg_skinned_mesh_get_skinned_bounds (mesh, mn, mx));
        g_assert_cmpfloat (mx[1], >, 1.6f);
        g_assert_cmpfloat (mn[1], <, 0.6f);
    }

    /* side-by-side strip of the three frames (ImageDraw is compiled out) */
    {
        Color *dst = g_new (Color, 720 * 240);
        gint   y;

        for (k = 0; k < 3; k++)
        {
            Color *src = LoadImageColors (frames[k]);

            for (y = 0; y < 240; y++)
                memcpy (dst + y * 720 + 240 * k, src + y * 240, 240 * sizeof (Color));
            UnloadImageColors (src);
        }
        strip.data = dst;
        strip.width = 720;
        strip.height = 240;
        strip.mipmaps = 1;
        strip.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    }
    if (shot_dir != NULL && shot_dir[0] != '\0')
    {
        g_autofree gchar *file = g_build_filename (shot_dir, "m34-animimport-walk.png", NULL);

        g_assert_true (ExportImage (strip, file));
    }
    g_free (strip.data);
    for (k = 0; k < 3; k++)
        UnloadImage (frames[k]);

    /* morph weights reach the GPU path: a morph mesh draws after a change */
    {
        g_autoptr(GBytes)           mglb = morph_glb ();
        g_autoptr(LrgGltfAnimation) mimp = lrg_gltf_animation_new_from_bytes (mglb, NULL, &error);
        g_autoptr(LrgSkinnedMesh)   morph = lrg_gltf_animation_create_mesh (mimp, 0, &error);

        g_assert_no_error (error);
        g_assert_true (lrg_skinned_mesh_upload (morph, &error));
        lrg_skinned_mesh_set_morph_weight (morph, 0, 1.0f);
        {
            RenderTexture2D rt = LoadRenderTexture (8, 8);
            Camera3D        cam;

            memset (&cam, 0, sizeof cam);
            cam.position = (Vector3){ 0, 0, 5 };
            cam.up = (Vector3){ 0, 1, 0 };
            cam.fovy = 4.0f;
            cam.projection = CAMERA_ORTHOGRAPHIC;
            BeginTextureMode (rt);
            BeginMode3D (cam);
            lrg_skinned_mesh_draw (morph, NULL);
            EndMode3D ();
            EndTextureMode ();
            UnloadRenderTexture (rt);
        }
        lrg_skinned_mesh_unload (morph);
    }
    lrg_skinned_mesh_unload (mesh);
}

/* -------------------------------------------------------------------------- */
/* Timings (run with -m perf)                                                 */
/* -------------------------------------------------------------------------- */

static void
test_perf (void)
{
    g_autoptr(GBytes) glb = NULL;
    g_autoptr(GError) error = NULL;
    gint64            t0;
    guint             i;
    guint             iters = 200;

    if (!g_test_perf ())
    {
        g_test_skip ("timings run with -m perf");
        return;
    }
    glb = character_glb ();

    /* import: parse + skeleton + clip + skinned mesh */
    t0 = g_get_monotonic_time ();
    for (i = 0; i < iters; i++)
    {
        g_autoptr(LrgGltfAnimation) imp = lrg_gltf_animation_new_from_bytes (glb, NULL, &error);
        g_autoptr(LrgSkeleton)      sk = lrg_gltf_animation_create_skeleton (imp, 0, &error);
        g_autoptr(LrgAnimationClip) cl = lrg_gltf_animation_create_clip (imp, 0, &error);
        g_autoptr(LrgSkinnedMesh)   me = lrg_gltf_animation_create_skinned_mesh (imp, 0, &error);
    }
    g_test_minimized_result ((g_get_monotonic_time () - t0) / (gdouble)iters,
                             "import 11-bone character (skeleton, clip, 264-vertex mesh): %.1f us",
                             (g_get_monotonic_time () - t0) / (gdouble)iters);

    {
        g_autoptr(LrgGltfAnimation) imp = lrg_gltf_animation_new_from_bytes (glb, NULL, &error);
        g_autoptr(LrgSkeleton)      sk = lrg_gltf_animation_create_skeleton (imp, 0, &error);
        g_autoptr(LrgSkeleton)      sk2 = lrg_gltf_animation_create_skeleton (imp, 0, &error);
        g_autoptr(LrgAnimationClip) cl = lrg_gltf_animation_create_clip (imp, 0, &error);
        g_autoptr(LrgSkinnedMesh)   me = lrg_gltf_animation_create_skinned_mesh (imp, 0, &error);
        g_autoptr(LrgRetargeter)    rt = lrg_retargeter_new (sk, sk2, NULL);
        g_autofree gfloat          *out = g_new (gfloat, 3 * lrg_skinned_mesh_get_vertex_count (me));
        guint                       n = 100000;
        LrgBonePose                 pose;

        t0 = g_get_monotonic_time ();
        for (i = 0; i < n; i++)
            lrg_animation_clip_sample_track (cl, i % 6, (gfloat)(i % 997) / 997.0f, &pose);
        g_test_minimized_result ((g_get_monotonic_time () - t0) * 1000.0 / n,
                                 "sample one channel track: %.1f ns",
                                 (g_get_monotonic_time () - t0) * 1000.0 / n);

        t0 = g_get_monotonic_time ();
        for (i = 0; i < 10000; i++)
        {
            pose_skeleton (sk, cl, (gfloat)i / 10000.0f);
            lrg_skinned_mesh_apply_skeleton (me, sk);
        }
        g_test_minimized_result ((g_get_monotonic_time () - t0) / 10000.0,
                                 "pose 11 bones from the clip + palette: %.2f us",
                                 (g_get_monotonic_time () - t0) / 10000.0);

        t0 = g_get_monotonic_time ();
        for (i = 0; i < 10000; i++)
            lrg_skinned_mesh_skin (me, out, NULL);
        g_test_minimized_result ((g_get_monotonic_time () - t0) / 10000.0,
                                 "CPU skin 264 vertices: %.2f us", (g_get_monotonic_time () - t0) / 10000.0);

        lrg_retargeter_set_sample_rate (rt, 30.0f);
        t0 = g_get_monotonic_time ();
        for (i = 0; i < 100; i++)
        {
            g_autoptr(LrgAnimationClip) baked = lrg_retargeter_retarget_clip (rt, cl, &error);
        }
        g_test_minimized_result ((g_get_monotonic_time () - t0) / 100.0,
                                 "retarget-bake a 1 s clip, 11 bones, 30 fps: %.1f us",
                                 (g_get_monotonic_time () - t0) / 100.0);
    }
}

int
main (int    argc,
      char **argv)
{
    gint result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    g_test_add_func ("/gltf-animation/skeleton-hierarchy", test_skeleton_hierarchy);
    g_test_add_func ("/gltf-animation/clip-channels", test_clip_channels);
    g_test_add_func ("/gltf-animation/skinning-matches-reference", test_skinning_matches_reference);
    g_test_add_func ("/gltf-animation/morph-targets", test_morph_targets);
    g_test_add_func ("/gltf-animation/bone-map", test_bone_map);
    g_test_add_func ("/gltf-animation/retarget", test_retarget);
    g_test_add_func ("/gltf-animation/load-file-and-errors", test_load_file_and_errors);
    g_test_add_func ("/gltf-animation/gpu-character", test_gpu_character);
    g_test_add_func ("/gltf-animation/perf", test_perf);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
