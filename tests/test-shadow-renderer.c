/* test-shadow-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgShadowRenderer's headless side: the matrix helpers,
 * the cascade splits, the fitted cascades (every corner of a slice lands
 * inside its light camera, the camera is snapped to whole texels), the
 * cube faces (their projection matches the face lookup of the light
 * shader) and the pass plan. The GPU passes are covered by
 * test-light-set's /light-set/shader-shadows.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

static const GrlColor white = { 255, 255, 255, 255 };

static void
test_shadow_matrices (void)
{
    gfloat eye[3] = { 1, 2, 3 };
    gfloat target[3] = { 1, 2, -7 };
    gfloat up[3] = { 0, 1, 0 };
    gfloat view[16];
    gfloat proj[16];
    gfloat vp[16];
    gfloat ident[16];
    gfloat out[3];
    gfloat p[3];

    lrg_shadow_look_at (eye, target, up, view);
    /* the eye goes to the origin, the target down -Z */
    lrg_shadow_transform_point (view, eye, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.0f, 1e-5);
    g_assert_cmpfloat_with_epsilon (out[2], 0.0f, 1e-5);
    lrg_shadow_transform_point (view, target, out);
    g_assert_cmpfloat_with_epsilon (out[2], -10.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (out[0], 0.0f, 1e-5);

    /* perspective: the near plane to -1, the far plane to +1 */
    lrg_shadow_perspective (90.0f, 1.0f, 1.0f, 100.0f, proj);
    p[0] = 0; p[1] = 0; p[2] = -1;
    lrg_shadow_transform_point (proj, p, out);
    g_assert_cmpfloat_with_epsilon (out[2], -1.0f, 1e-5);
    p[2] = -100;
    lrg_shadow_transform_point (proj, p, out);
    g_assert_cmpfloat_with_epsilon (out[2], 1.0f, 1e-4);
    /* 90 degrees: x = -z at the edge */
    p[0] = 5; p[2] = -5;
    lrg_shadow_transform_point (proj, p, out);
    g_assert_cmpfloat_with_epsilon (out[0], 1.0f, 1e-5);

    /* ortho maps its box to the unit cube */
    lrg_shadow_ortho (-2, 2, -1, 1, 0, 10, proj);
    p[0] = 2; p[1] = -1; p[2] = -10;
    lrg_shadow_transform_point (proj, p, out);
    g_assert_cmpfloat_with_epsilon (out[0], 1.0f, 1e-6);
    g_assert_cmpfloat_with_epsilon (out[1], -1.0f, 1e-6);
    g_assert_cmpfloat_with_epsilon (out[2], 1.0f, 1e-6);

    /* multiply: the product transforms like the two in a row */
    lrg_shadow_perspective (60.0f, 1.5f, 0.5f, 50.0f, proj);
    lrg_shadow_multiply (proj, view, vp);
    p[0] = 3; p[1] = 1; p[2] = -4;
    {
        gfloat a[3];

        lrg_shadow_transform_point (view, p, a);
        lrg_shadow_transform_point (proj, a, a);
        lrg_shadow_transform_point (vp, p, out);
        g_assert_cmpfloat_with_epsilon (out[0], a[0], 1e-4);
        g_assert_cmpfloat_with_epsilon (out[1], a[1], 1e-4);
        g_assert_cmpfloat_with_epsilon (out[2], a[2], 1e-4);
    }
    memset (ident, 0, sizeof ident);
    ident[0] = ident[5] = ident[10] = ident[15] = 1.0f;
    lrg_shadow_multiply (ident, vp, ident);
    g_assert_cmpfloat_with_epsilon (ident[0], vp[0], 1e-6);
    g_assert_cmpfloat_with_epsilon (ident[14], vp[14], 1e-6);
}

static void
test_shadow_splits (void)
{
    gfloat splits[4];
    guint  i;

    g_assert_true (lrg_shadow_split_distances (1.0f, 100.0f, 4, 0.0f, splits));
    for (i = 0; i < 4; i++)
        g_assert_cmpfloat_with_epsilon (splits[i], 1.0f + 99.0f * (gfloat)(i + 1) / 4.0f, 1e-4);
    g_assert_true (lrg_shadow_split_distances (1.0f, 100.0f, 4, 1.0f, splits));
    g_assert_cmpfloat_with_epsilon (splits[0], powf (100.0f, 0.25f), 1e-4);
    g_assert_cmpfloat_with_epsilon (splits[1], 10.0f, 1e-4);
    g_assert_cmpfloat (splits[3], ==, 100.0f);
    g_assert_true (lrg_shadow_split_distances (0.1f, 80.0f, 3, 0.6f, splits));
    g_assert_cmpfloat (splits[0], <, splits[1]);
    g_assert_cmpfloat (splits[1], <, splits[2]);
    g_assert_cmpfloat (splits[2], ==, 80.0f);

    /* bad arguments leave the output alone */
    splits[0] = -5.0f;
    g_assert_false (lrg_shadow_split_distances (0.0f, 10.0f, 2, 0.5f, splits));
    g_assert_false (lrg_shadow_split_distances (5.0f, 1.0f, 2, 0.5f, splits));
    g_assert_false (lrg_shadow_split_distances (1.0f, 10.0f, 0, 0.5f, splits));
    g_assert_false (lrg_shadow_split_distances (1.0f, 10.0f, 5, 0.5f, splits));
    g_assert_cmpfloat (splits[0], ==, -5.0f);
}

/* every corner of every slice projects inside its cascade */
static void
test_shadow_cascades (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    GrlVector3 sun = { 0.4f, -0.8f, 0.3f };
    gfloat camera[3] = { 120.0f, 15.0f, -40.0f };
    gfloat forward[3] = { 0.6f, -0.2f, -0.77f };
    gfloat up[3] = { 0, 1, 0 };
    gfloat right[3];
    gfloat cup[3];
    gfloat len;
    gfloat fovy = 65.0f;
    gfloat aspect = 16.0f / 9.0f;
    guint  res;
    guint  cascades;
    gfloat dist;
    guint  i;
    guint  c;

    lrg_shadow_renderer_set_sun (shadows, 1024, 3, 90.0f, 0.6f);
    lrg_shadow_renderer_get_sun (shadows, &res, &cascades, &dist);
    g_assert_cmpuint (res, ==, 1024);
    g_assert_cmpuint (cascades, ==, 3);
    g_assert_cmpfloat (dist, ==, 90.0f);

    /* no directional light: no cascade */
    g_assert_cmpuint (lrg_shadow_renderer_update (shadows, lights, camera, forward, up, fovy,
                                                  aspect, 0.1f), ==, 0);
    g_assert_cmpuint (lrg_shadow_renderer_get_cascade_count (shadows), ==, 0);
    g_assert_false (lrg_shadow_renderer_get_cascade (shadows, 0, NULL, NULL, NULL));

    lrg_light_set_set_directional (lights, &sun, &white);
    /* a floating origin: everything is planned in draw space */
    lrg_light_set_set_origin (lights, 100.0, 0.0, 0.0);
    g_assert_cmpuint (lrg_shadow_renderer_update (shadows, lights, camera, forward, up, fovy,
                                                  aspect, 0.1f), ==, 3);
    g_assert_cmpuint (lrg_shadow_renderer_get_cascade_count (shadows), ==, 3);
    g_assert_cmpuint (lrg_shadow_renderer_get_pass (shadows, 2, &i, NULL, NULL), ==,
                      LRG_SHADOW_PASS_CASCADE);
    g_assert_cmpuint (i, ==, 2);
    g_assert_cmpuint (lrg_shadow_renderer_get_pass (shadows, 3, NULL, NULL, NULL), ==,
                      LRG_SHADOW_PASS_NONE);

    len = sqrtf (forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    for (i = 0; i < 3; i++)
        forward[i] /= len;
    right[0] = forward[1] * up[2] - forward[2] * up[1];
    right[1] = forward[2] * up[0] - forward[0] * up[2];
    right[2] = forward[0] * up[1] - forward[1] * up[0];
    len = sqrtf (right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    for (i = 0; i < 3; i++)
        right[i] /= len;
    cup[0] = right[1] * forward[2] - right[2] * forward[1];
    cup[1] = right[2] * forward[0] - right[0] * forward[2];
    cup[2] = right[0] * forward[1] - right[1] * forward[0];

    for (c = 0; c < 3; c++)
    {
        gfloat near;
        gfloat far;
        gfloat m[16];
        gfloat origin[3] = { 0, 0, 0 };
        gfloat o[3];
        guint  k;

        g_assert_true (lrg_shadow_renderer_get_cascade (shadows, c, &near, &far, m));
        if (c == 0)
            g_assert_cmpfloat (near, ==, 0.1f);
        if (c == 2)
            g_assert_cmpfloat (far, ==, 90.0f);
        for (k = 0; k < 8; k++)
        {
            gfloat d = (k & 4) ? far : near;
            gfloat h = d * tanf (fovy * (gfloat)G_PI / 360.0f);
            gfloat w = h * aspect;
            gfloat p[3];
            gfloat q[3];
            guint  j;

            for (j = 0; j < 3; j++)
                p[j] = camera[j] - (j == 0 ? 100.0f : 0.0f) + forward[j] * d
                       + right[j] * w * ((k & 1) ? 1.0f : -1.0f)
                       + cup[j] * h * ((k & 2) ? 1.0f : -1.0f);
            lrg_shadow_transform_point (m, p, q);
            g_assert_cmpfloat (fabsf (q[0]), <=, 1.0001f);
            g_assert_cmpfloat (fabsf (q[1]), <=, 1.0001f);
            g_assert_cmpfloat (q[2], >=, -1.0001f);
            g_assert_cmpfloat (q[2], <=, 1.0001f);
        }
        /* the draw-space origin sits on a texel corner (no crawling) */
        lrg_shadow_transform_point (m, origin, o);
        g_assert_cmpfloat_with_epsilon (o[0] * 512.0f, roundf (o[0] * 512.0f), 2e-2);
        g_assert_cmpfloat_with_epsilon (o[1] * 512.0f, roundf (o[1] * 512.0f), 2e-2);
    }

    /* turning the sun's shadow off */
    lrg_shadow_renderer_set_sun (shadows, 0, 1, 50.0f, 0.5f);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, fovy, aspect, 0.1f);
    g_assert_cmpuint (lrg_shadow_renderer_get_cascade_count (shadows), ==, 0);
    /* no context: no pass binds, nothing is ready */
    lrg_shadow_renderer_set_sun (shadows, 256, 1, 50.0f, 0.5f);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, fovy, aspect, 0.1f);
    g_assert_false (lrg_shadow_renderer_is_ready (shadows));
    g_assert_false (lrg_shadow_renderer_begin_pass (shadows, 99));
}

/* the face lookup of LRG_SHADOW_GLSL, in C */
static void
glsl_face_lookup (const gfloat *d,
                  guint        *out_face,
                  gfloat       *out_ndc)
{
    gfloat a[3] = { fabsf (d[0]), fabsf (d[1]), fabsf (d[2]) };
    gfloat F[3] = { 0, 0, 0 };
    gfloat U[3] = { 0, 1, 0 };
    gfloat vz[3];
    gfloat vx[3];
    gfloat vy[3];
    gfloat len;
    gfloat fd;
    guint  k;

    if (a[0] >= a[1] && a[0] >= a[2])
    {
        *out_face = d[0] > 0 ? 0 : 1;
        F[0] = d[0] > 0 ? 1.0f : -1.0f;
    }
    else if (a[1] >= a[2])
    {
        *out_face = d[1] > 0 ? 2 : 3;
        F[1] = d[1] > 0 ? 1.0f : -1.0f;
        U[1] = 0;
        U[2] = 1;
    }
    else
    {
        *out_face = d[2] > 0 ? 4 : 5;
        F[2] = d[2] > 0 ? 1.0f : -1.0f;
    }
    for (k = 0; k < 3; k++)
        vz[k] = -F[k];
    vx[0] = U[1] * vz[2] - U[2] * vz[1];
    vx[1] = U[2] * vz[0] - U[0] * vz[2];
    vx[2] = U[0] * vz[1] - U[1] * vz[0];
    len = sqrtf (vx[0] * vx[0] + vx[1] * vx[1] + vx[2] * vx[2]);
    for (k = 0; k < 3; k++)
        vx[k] /= len;
    vy[0] = vz[1] * vx[2] - vz[2] * vx[1];
    vy[1] = vz[2] * vx[0] - vz[0] * vx[2];
    vy[2] = vz[0] * vx[1] - vz[1] * vx[0];
    fd = d[0] * F[0] + d[1] * F[1] + d[2] * F[2];
    out_ndc[0] = (d[0] * vx[0] + d[1] * vx[1] + d[2] * vx[2]) / fd;
    out_ndc[1] = (d[0] * vy[0] + d[1] * vy[1] + d[2] * vy[2]) / fd;
}

static void
test_shadow_cube_faces (void)
{
    static const gfloat dirs[][3] = {
        { 3.0f, 1.0f, -0.5f }, { -2.0f, 0.4f, 1.1f }, { 0.3f, 4.0f, 1.0f },
        { -0.5f, -3.0f, 0.9f }, { 1.0f, -0.2f, 5.0f }, { -0.8f, 0.6f, -2.0f },
        { 1.0f, 1.0f, 0.999f }
    };
    gfloat light[3] = { 5.0f, 2.0f, -3.0f };
    gfloat view[16];
    gfloat proj[16];
    gfloat vp[16];
    guint  i;

    g_assert_false (lrg_shadow_cube_face_matrices (6, light, 10.0f, view, proj));
    for (i = 0; i < G_N_ELEMENTS (dirs); i++)
    {
        gfloat p[3];
        gfloat q[3];
        gfloat ndc[2];
        guint  face;
        guint  k;

        glsl_face_lookup (dirs[i], &face, ndc);
        g_assert_true (lrg_shadow_cube_face_matrices (face, light, 10.0f, view, proj));
        lrg_shadow_multiply (proj, view, vp);
        for (k = 0; k < 3; k++)
            p[k] = light[k] + dirs[i][k];
        lrg_shadow_transform_point (vp, p, q);
        /* the pass draws that point where the shader looks it up */
        g_assert_cmpfloat_with_epsilon (q[0], ndc[0], 1e-4);
        g_assert_cmpfloat_with_epsilon (q[1], ndc[1], 1e-4);
        g_assert_cmpfloat (fabsf (q[0]), <=, 1.0001f);
        g_assert_cmpfloat (fabsf (q[1]), <=, 1.0001f);
        g_assert_cmpfloat (q[2], >, -1.0f);
        g_assert_cmpfloat (q[2], <, 1.0f);
    }
}

static void
test_shadow_point_plan (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    GrlVector3 sun = { 0, -1, 0 };
    gfloat camera[3] = { 0, 1.6f, 0 };
    gfloat forward[3] = { 0, 0, -1 };
    gfloat up[3] = { 0, 1, 0 };
    gfloat vp[16];
    guint  torch;
    guint  spell;
    guint  candle;
    guint  res;
    guint  max;
    guint  index;
    guint  face;
    guint  i;

    torch = lrg_light_set_add_point_light (lights, 0, 1.6, 0, &white, 8);
    spell = lrg_light_set_add_point_light (lights, 0, 1.6, -5, &white, 6);
    candle = lrg_light_set_add_point_light (lights, 3, 2, 3, &white, 4);
    for (i = 0; i < 10; i++)
        lrg_light_set_add_point_light (lights, 10.0 + (gdouble)i, 2, 0, &white, 3);
    lrg_light_set_set_point_light_shadow (lights, torch, 3);
    lrg_light_set_set_point_light_shadow (lights, spell, 2);
    lrg_light_set_set_point_light_shadow (lights, candle, 1);

    lrg_shadow_renderer_get_points (shadows, &res, &max);
    g_assert_cmpuint (max, ==, 0);
    g_assert_cmpuint (lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60, 1,
                                                  0.1f), ==, 0);

    lrg_shadow_renderer_set_points (shadows, 256, 2, 0.0f);
    lrg_shadow_renderer_get_points (shadows, &res, &max);
    g_assert_cmpuint (res, ==, 256);
    g_assert_cmpuint (max, ==, 2);
    lrg_light_set_set_directional (lights, &sun, &white);
    g_assert_cmpuint (lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60, 1,
                                                  0.1f), ==, 1 + 12);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, torch), ==, 0);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, spell), ==, 1);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, candle), ==, LRG_SHADOW_NO_SLOT);
    g_assert_cmpuint (lrg_shadow_renderer_get_point_light (shadows, 2), ==, G_MAXUINT);

    /* pass 0 is the cascade, then six faces per light in slot order */
    g_assert_cmpuint (lrg_shadow_renderer_get_pass (shadows, 0, NULL, NULL, NULL), ==,
                      LRG_SHADOW_PASS_CASCADE);
    g_assert_cmpuint (lrg_shadow_renderer_get_pass (shadows, 1 + 6 + 4, &index, &face, vp), ==,
                      LRG_SHADOW_PASS_CUBE_FACE);
    g_assert_cmpuint (index, ==, 1);
    g_assert_cmpuint (face, ==, 4);
    {
        /* +Z face of the spell light: a point 2 ahead of it in +Z is centred */
        gfloat p[3] = { 0, 1.6f, -3 };
        gfloat q[3];

        lrg_shadow_transform_point (vp, p, q);
        g_assert_cmpfloat_with_epsilon (q[0], 0.0f, 1e-4);
        g_assert_cmpfloat_with_epsilon (q[1], 0.0f, 1e-4);
    }
    g_assert_cmpuint (lrg_shadow_renderer_get_pass (shadows, 13, NULL, NULL, NULL), ==,
                      LRG_SHADOW_PASS_NONE);

    /* a removed light frees its slot on the next update */
    lrg_light_set_remove_point_light (lights, torch);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60, 1, 0.1f);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, spell), ==, 0);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, candle), ==, 1);

    /* settings */
    lrg_shadow_renderer_set_strength (shadows, 2.0f);
    g_assert_cmpfloat (lrg_shadow_renderer_get_strength (shadows), ==, 1.0f);
    lrg_shadow_renderer_set_strength (shadows, 0.25f);
    g_assert_cmpfloat (lrg_shadow_renderer_get_strength (shadows), ==, 0.25f);
    lrg_shadow_renderer_set_bias (shadows, -1.0f, NAN);
    lrg_shadow_renderer_set_filter (shadows, 9);
    {
        GEnumClass *klass = g_type_class_ref (LRG_TYPE_SHADOW_PASS_KIND);

        g_assert_cmpstr (g_enum_get_value (klass, LRG_SHADOW_PASS_CUBE_FACE)->value_nick, ==,
                         "cube-face");
        g_type_class_unref (klass);
    }
}

/*
 * gaps G8: the pass cache's refresh is staggered.  A still view with a sun
 * (3 cascades) and a torch (6 cube faces): after the first round every pass
 * is rendered once; with refresh 4 they all expire together, but at most
 * ceil (9 / 4) = 3 render per frame, the oldest first, and none waits more
 * than twice the refresh.  A moved camera makes the cascades render at once.
 */
static void
test_stagger (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    GrlVector3 sun = { 0.4f, -0.8f, 0.3f };
    GrlColor   white = { 255, 255, 255, 255 };
    gfloat camera[3] = { 0.0f, 2.0f, 0.0f };
    gfloat forward[3] = { 0.0f, 0.0f, -1.0f };
    gfloat up[3] = { 0, 1, 0 };
    guint  passes;
    guint  frame;
    guint  last_render[16];
    guint  max_per_frame = 0;
    guint  max_gap = 0;
    guint  rendered_total = 0;
    guint  i;

    lrg_shadow_renderer_set_sun (shadows, 512, 3, 60.0f, 0.6f);
    lrg_shadow_renderer_set_points (shadows, 256, 1, 30.0f);
    lrg_shadow_renderer_set_refresh (shadows, 4);
    lrg_light_set_set_directional (lights, &sun, &white);
    lrg_light_set_add_point_light (lights, 1.0, 1.5, -2.0, &white, 12.0f);
    for (i = 0; i < 16; i++)
        last_render[i] = 0;

    for (frame = 1; frame <= 60; frame++)
    {
        guint per_frame = 0;

        lrg_light_set_select (lights, camera[0], camera[1], camera[2]);
        passes = lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60.0f, 1.0f, 0.1f);
        g_assert_cmpuint (passes, >=, 3);
        for (i = 0; i < passes && i < 16; i++)
        {
            if (lrg_shadow_renderer_pass_is_current (shadows, i))
                continue;
            lrg_shadow_renderer_mark_rendered (shadows, i);
            per_frame++;
            rendered_total++;
            if (last_render[i] != 0)
                max_gap = MAX (max_gap, frame - last_render[i]);
            last_render[i] = frame;
        }
        if (frame > 1)
            max_per_frame = MAX (max_per_frame, per_frame);
        /* the first frame renders everything; later ones are rationed */
        if (frame == 1)
            g_assert_cmpuint (per_frame, ==, passes);
    }
    /* after the first round: no frame renders more than the ration */
    g_assert_cmpuint (max_per_frame, <=, (passes + 3) / 4);
    /* nobody waits more than twice the refresh (8 frames) plus the ration's turn */
    g_assert_cmpuint (max_gap, <=, 8);
    /* and they do render again (the cache is not frozen) */
    g_assert_cmpuint (rendered_total, >, passes * 3);

    /* a moved camera: the cascades' matrices changed, so they render at once */
    {
        guint rendered_now = 0;

        camera[0] += 20.0f;
        lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60.0f, 1.0f, 0.1f);
        for (i = 0; i < 3; i++)
            if (!lrg_shadow_renderer_pass_is_current (shadows, i))
                rendered_now++;
        g_assert_cmpuint (rendered_now, ==, 3);
    }
}

/*
 * The pass cache with LRG_SHADOW_REFRESH_NEVER and box invalidation: two
 * torches far apart under a sun keep every tile for any number of still
 * updates; a box moved next to the first torch forgets only the tiles
 * whose light camera sees it (some of that torch's faces, the cascades
 * covering it), never the far torch's; a box with a NaN forgets all.
 */
static void
test_invalidate_box (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    GrlVector3 sun = { 0.3f, -1.0f, 0.2f };
    GrlColor   white = { 255, 255, 255, 255 };
    gfloat camera[3] = { 0.0f, 2.0f, 0.0f };
    gfloat forward[3] = { 0.0f, 0.0f, -1.0f };
    gfloat up[3] = { 0, 1, 0 };
    gfloat near_lo[3] = { -10.5f, 0.0f, -10.5f };
    gfloat near_hi[3] = { -9.5f, 1.0f, -9.5f };
    gfloat bad[3] = { 0.0f, NAN, 0.0f };
    guint  torch_a;
    guint  torch_b;
    gint   slot_a;
    gint   slot_b;
    guint  passes;
    guint  cascades;
    guint  dropped;
    guint  frame;
    guint  face;
    guint  i;
    guint  a_dropped = 0;

    lrg_shadow_renderer_set_sun (shadows, 512, 2, 40.0f, 0.5f);
    lrg_shadow_renderer_set_points (shadows, 128, 2, 0.0f);
    lrg_shadow_renderer_set_refresh (shadows, LRG_SHADOW_REFRESH_NEVER);
    g_assert_cmpuint (lrg_shadow_renderer_get_refresh (shadows), ==, LRG_SHADOW_REFRESH_NEVER);
    lrg_light_set_set_directional (lights, &sun, &white);
    torch_a = lrg_light_set_add_point_light (lights, -10.0, 2.0, -10.0, &white, 6.0f);
    torch_b = lrg_light_set_add_point_light (lights, 30.0, 2.0, -30.0, &white, 6.0f);
    lrg_light_set_set_point_light_shadow (lights, torch_a, 1);
    lrg_light_set_set_point_light_shadow (lights, torch_b, 1);

    lrg_light_set_select (lights, camera[0], camera[1], camera[2]);
    passes = lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60.0f, 1.0f, 0.1f);
    cascades = lrg_shadow_renderer_get_cascade_count (shadows);
    g_assert_cmpuint (cascades, ==, 2);
    g_assert_cmpuint (passes, ==, cascades + 12);
    slot_a = lrg_shadow_renderer_get_light_slot (shadows, torch_a);
    slot_b = lrg_shadow_renderer_get_light_slot (shadows, torch_b);
    g_assert_cmpint (slot_a, >=, 0);
    g_assert_cmpint (slot_b, >=, 0);
    for (i = 0; i < passes; i++)
        lrg_shadow_renderer_mark_rendered (shadows, i);

    /* a still view: nothing renders again, however long */
    for (frame = 0; frame < 500; frame++)
    {
        lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60.0f, 1.0f, 0.1f);
        for (i = 0; i < passes; i++)
            g_assert_true (lrg_shadow_renderer_pass_is_current (shadows, i));
    }
    g_assert_true (lrg_shadow_renderer_is_ready (shadows));

    /* a crate moved next to torch A */
    dropped = lrg_shadow_renderer_invalidate_box (shadows, near_lo, near_hi);
    g_assert_cmpuint (dropped, >, 0);
    for (face = 0; face < 6; face++)
    {
        guint pass_a = cascades + (guint)slot_a * 6 + face;
        guint pass_b = cascades + (guint)slot_b * 6 + face;

        if (!lrg_shadow_renderer_pass_is_current (shadows, pass_a))
            a_dropped++;
        g_assert_true (lrg_shadow_renderer_pass_is_current (shadows, pass_b));
    }
    /* the crate lies beside and below the torch: some faces, not all */
    g_assert_cmpuint (a_dropped, >=, 1);
    g_assert_cmpuint (a_dropped, <, 6);
    /* the next update renders exactly the forgotten passes */
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60.0f, 1.0f, 0.1f);
    {
        guint stale = 0;

        for (i = 0; i < passes; i++)
            if (!lrg_shadow_renderer_pass_is_current (shadows, i))
                stale++;
        g_assert_cmpuint (stale, ==, dropped);
    }
    for (i = 0; i < passes; i++)
        lrg_shadow_renderer_mark_rendered (shadows, i);

    /* a box nobody sees (far below the world, beyond every light) */
    {
        gfloat lo[3] = { 500.0f, -400.0f, 500.0f };
        gfloat hi[3] = { 501.0f, -399.0f, 501.0f };

        g_assert_cmpuint (lrg_shadow_renderer_invalidate_box (shadows, lo, hi), ==, 0);
    }

    /* an unplaceable box: every tile */
    g_assert_cmpuint (lrg_shadow_renderer_invalidate_box (shadows, bad, near_hi), ==, passes);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60.0f, 1.0f, 0.1f);
    for (i = 0; i < passes; i++)
        g_assert_false (lrg_shadow_renderer_pass_is_current (shadows, i));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/shadow/matrices", test_shadow_matrices);
    g_test_add_func ("/shadow/splits", test_shadow_splits);
    g_test_add_func ("/shadow/cascades", test_shadow_cascades);
    g_test_add_func ("/shadow/invalidate-box", test_invalidate_box);
    g_test_add_func ("/shadow/stagger", test_stagger);
    g_test_add_func ("/shadow/cube-faces", test_shadow_cube_faces);
    g_test_add_func ("/shadow/point-plan", test_shadow_point_plan);

    return g_test_run ();
}
