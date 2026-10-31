/* test-prewarm.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgPrewarm, LrgPrewarmable and the loading state's
 * prewarm phase. The queue bookkeeping (weak entries, tracking, the
 * window check) is headless; running entries, warming the engine's
 * renderers, the hidden draw and the loading state need a GL context and
 * open a hidden window on first use (skipped without a display). The
 * headless tests run first, before any window exists.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

static GrlWindow *test_window = NULL;
static gboolean   window_tried = FALSE;

/* ensure_window: the hidden test window, opened on first use; FALSE
 * without a display */
static gboolean
ensure_window (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if (window_tried)
        return test_window != NULL;
    window_tried = TRUE;
    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetTraceLogLevel (LOG_WARNING);
    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (64, 64, "lrg-prewarm-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

#define SKIP_IF_NO_WINDOW() \
    do { \
        if (!ensure_window ()) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* --- a counting func entry ------------------------------------------- */

typedef struct
{
    GString *log;       /* entry names in the order they ran */
    gchar    tag;
    guint    calls;
    guint    steps;     /* calls before it reports finished */
    gboolean *freed;
} Step;

static gboolean
step_run (gpointer user_data)
{
    Step *step = user_data;

    step->calls++;
    g_string_append_c (step->log, step->tag);
    return step->calls >= step->steps;
}

static void
step_free (gpointer user_data)
{
    Step *step = user_data;

    if (step->freed != NULL)
        *step->freed = TRUE;
    g_free (step);
}

static void
add_step (LrgPrewarm *prewarm,
          GString    *log,
          gchar       tag,
          guint       steps,
          gboolean   *freed)
{
    Step *step = g_new0 (Step, 1);

    step->log = log;
    step->tag = tag;
    step->steps = steps;
    step->freed = freed;
    lrg_prewarm_add_func (prewarm, "step", step_run, step, step_free);
}

/* --- headless -------------------------------------------------------- */

/* without a window nothing runs and the queue is kept for later */
static void
test_prewarm_no_window (void)
{
    g_autoptr(LrgPrewarm) prewarm = lrg_prewarm_new ();
    g_autoptr(GString)    log = g_string_new (NULL);
    gboolean              freed = FALSE;

    g_assert_false (IsWindowReady ());
    add_step (prewarm, log, 'a', 1, &freed);
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 1);

    g_assert_false (lrg_prewarm_run (prewarm, 1000000));
    lrg_prewarm_run_all (prewarm);
    g_assert_cmpstr (log->str, ==, "");
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 1);
    g_assert_cmpuint (lrg_prewarm_get_completed (prewarm), ==, 0);

    /* clearing drops the entry and frees its data */
    lrg_prewarm_clear (prewarm);
    g_assert_true (freed);
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 0);

    /* an engine routes to the same default queue and does nothing too */
    g_assert_true (lrg_engine_get_prewarm (lrg_engine_get_default ()) ==
                   lrg_prewarm_get_default ());
    g_assert_false (lrg_engine_prewarm (lrg_engine_get_default (), 1000));
}

/* the queue holds objects weakly; a prewarm without a window is a no-op */
static void
test_prewarm_weak_object (void)
{
    g_autoptr(LrgPrewarm) prewarm = lrg_prewarm_new ();
    LrgLightSet          *lights;

    lights = lrg_light_set_new ();
    g_assert_true (LRG_IS_PREWARMABLE (lights));
    g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (lights)));
    g_assert_null (lrg_light_set_get_shader (lights));

    lrg_prewarm_add_object (prewarm, LRG_PREWARMABLE (lights));
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 1);
    g_object_unref (lights);
    /* the entry outlives the object harmlessly */
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 1);
    lrg_prewarm_clear (prewarm);
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 0);
}

static gpointer
make_on_worker (gpointer data)
{
    return lrg_water_surface_new ();
}

/* renderers made on the default queue's thread join it; made on another
 * thread, or with auto-track off, they do not */
static void
test_prewarm_track (void)
{
    LrgPrewarm                *prewarm = lrg_prewarm_get_default ();
    g_autoptr(LrgWaterSurface) water = NULL;
    g_autoptr(LrgWaterSurface) other = NULL;
    g_autoptr(LrgColorGrade)   grade = NULL;
    GThread                   *thread;
    guint                      before;

    lrg_prewarm_clear (prewarm);
    g_assert_true (lrg_prewarm_get_auto_track (prewarm));

    water = lrg_water_surface_new ();
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 1);
    grade = lrg_color_grade_new ();
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 2);

    before = lrg_prewarm_get_pending (prewarm);
    thread = g_thread_new ("prewarm-worker", make_on_worker, NULL);
    other = g_thread_join (thread);
    g_assert_nonnull (other);
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, before);

    lrg_prewarm_set_auto_track (prewarm, FALSE);
    {
        g_autoptr(LrgLightSet) lights = lrg_light_set_new ();

        g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, before);
    }
    lrg_prewarm_set_auto_track (prewarm, TRUE);
    lrg_prewarm_clear (prewarm);
}

/* a queue nobody runs only keeps entries for live objects (it sweeps the
 * freed ones out as it grows) */
static void
test_prewarm_sweep (void)
{
    LrgPrewarm *prewarm = lrg_prewarm_get_default ();
    guint       i;

    lrg_prewarm_clear (prewarm);
    for (i = 0; i < 2000; i++)
    {
        LrgSphere3D *sphere = lrg_sphere3d_new ();

        g_object_unref (sphere);
    }
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), <, 600);
    lrg_prewarm_clear (prewarm);
}

/* --- with a window --------------------------------------------------- */

/* order, budget, re-queued steps, destroy notifies and counters */
static void
test_prewarm_run (void)
{
    g_autoptr(LrgPrewarm) prewarm = NULL;
    g_autoptr(GString)    log = g_string_new (NULL);
    gboolean              freed_a = FALSE;
    gboolean              freed_b = FALSE;
    gboolean              freed_c = FALSE;

    SKIP_IF_NO_WINDOW ();

    prewarm = lrg_prewarm_new ();
    add_step (prewarm, log, 'a', 1, &freed_a);
    add_step (prewarm, log, 'b', 3, &freed_b);    /* three steps */
    add_step (prewarm, log, 'c', 1, &freed_c);

    /* a budget of 0 runs exactly one entry */
    g_assert_true (lrg_prewarm_run (prewarm, 0));
    g_assert_cmpstr (log->str, ==, "a");
    g_assert_true (freed_a);
    g_assert_true (lrg_prewarm_run (prewarm, 0));
    g_assert_cmpstr (log->str, ==, "ab");
    g_assert_false (freed_b);
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 2);

    /* an unfinished entry goes to the back: c runs before b again */
    g_assert_true (lrg_prewarm_run (prewarm, 0));
    g_assert_cmpstr (log->str, ==, "abc");
    lrg_prewarm_run_all (prewarm);
    g_assert_cmpstr (log->str, ==, "abcbb");
    g_assert_true (freed_b);
    g_assert_true (freed_c);
    g_assert_cmpuint (lrg_prewarm_get_pending (prewarm), ==, 0);
    g_assert_cmpuint (lrg_prewarm_get_completed (prewarm), ==, 3);
    g_assert_cmpint (lrg_prewarm_get_elapsed_us (prewarm), >=, 0);

    /* an empty queue is done */
    g_assert_false (lrg_prewarm_run (prewarm, 1000));
}

static const LrgGrassBlade *
grass_blades (GArray *blades)
{
    return (const LrgGrassBlade *) (gconstpointer) blades->data;
}

/* every engine renderer warms, twice in a row, and its lazily made GPU
 * objects are the ones its draws use afterwards */
static void
test_prewarm_renderers (void)
{
    g_autoptr(LrgLightSet)          lights = NULL;
    g_autoptr(LrgShadowRenderer)    shadows = NULL;
    g_autoptr(LrgColorGrade)        grade = NULL;
    g_autoptr(LrgColorblindFilter)  filter = NULL;
    g_autoptr(LrgSkinnedMesh)       skinned = NULL;
    g_autoptr(GPtrArray)            others = NULL;
    g_autoptr(GArray)               blades = NULL;
    g_autoptr(GError)               error = NULL;
    LrgWaterSurface                *water;
    LrgGrassField                  *grass;
    LrgBillboardBatch              *billboards;
    GrlShader                      *shader;
    guint                           pass;
    guint                           i;
    static const gfloat  positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    static const guint8  bone_indices[12] = { 0 };
    static const gfloat  bone_weights[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    static const guint32 indices[3] = { 0, 1, 2 };

    SKIP_IF_NO_WINDOW ();

    lights = lrg_light_set_new ();
    shadows = lrg_shadow_renderer_new ();
    grade = lrg_color_grade_new ();
    filter = lrg_colorblind_filter_new ();
    skinned = lrg_skinned_mesh_new (1);
    g_assert_true (lrg_skinned_mesh_set_geometry (skinned, 3, positions, NULL, NULL, NULL,
                                                  bone_indices, bone_weights, 3, indices,
                                                  &error));
    g_assert_no_error (error);

    others = g_ptr_array_new_with_free_func (g_object_unref);
    water = lrg_water_surface_new ();
    lrg_water_surface_add_grid (water, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 4, 4, NULL, NULL, NULL);
    g_ptr_array_add (others, water);
    grass = lrg_grass_field_new (3);
    blades = g_array_new (FALSE, FALSE, sizeof (LrgGrassBlade));
    lrg_grass_field_place_cell (grass, 0, 0, 0.0f, 0.0, 0.0, NULL, NULL, blades);
    lrg_grass_field_set_blades (grass, grass_blades (blades), blades->len);
    g_ptr_array_add (others, grass);
    billboards = lrg_billboard_batch_new ();
    for (i = 0; i < 300; i++)
    {
        g_autoptr(GrlVector3) p = grl_vector3_new ((gfloat) i, 0.0f, 0.0f);

        lrg_billboard_batch_add (billboards, 0, p, 1.0f, 1.0f);
    }
    g_ptr_array_add (others, billboards);
    g_ptr_array_add (others, lrg_cloud_layer_new (3));
    g_ptr_array_add (others, lrg_ground_overlay_new ());
    g_ptr_array_add (others, lrg_horizon_ring_new ());
    g_ptr_array_add (others, lrg_heat_haze_new ());
    g_ptr_array_add (others, lrg_palettize_new ());
    g_ptr_array_add (others, lrg_sharpen_new ());
    g_ptr_array_add (others, lrg_photo_dof_new ());
    g_ptr_array_add (others, lrg_photo_look_new ());
    g_ptr_array_add (others, lrg_sphere3d_new ());
    g_ptr_array_add (others, lrg_cone3d_new ());
    g_ptr_array_add (others, lrg_cylinder3d_new ());
    g_ptr_array_add (others, lrg_torus3d_new ());
    g_ptr_array_add (others, lrg_icosphere3d_new ());

    for (pass = 0; pass < 2; pass++)
    {
        g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (lights)));
        g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (shadows)));
        g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (grade)));
        g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (filter)));
        g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (skinned)));
        for (i = 0; i < others->len; i++)
        {
            GObject *object = g_ptr_array_index (others, i);

            g_assert_true (LRG_IS_PREWARMABLE (object));
            g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (object)));
        }
    }

    /* warm: the getters hand out what the prewarm built */
    shader = lrg_light_set_get_shader (lights);
    g_assert_nonnull (shader);
    g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (lights)));
    g_assert_true (lrg_light_set_get_shader (lights) == shader);
    shader = lrg_shadow_renderer_get_caster_shader (shadows);
    g_assert_nonnull (shader);
    g_assert_true (lrg_prewarmable_prewarm (LRG_PREWARMABLE (shadows)));
    g_assert_true (lrg_shadow_renderer_get_caster_shader (shadows) == shader);
    shader = lrg_color_grade_get_shader (grade);
    g_assert_nonnull (shader);
    g_assert_true (lrg_color_grade_get_shader (grade) == shader);
    g_assert_nonnull (lrg_colorblind_filter_get_shader (filter));
    g_assert_true (lrg_skinned_mesh_is_uploaded (skinned));

    lrg_water_surface_release_gpu (water);
    lrg_grass_field_release_gpu (grass);
    lrg_shadow_renderer_release_gpu (shadows);
}

/* a hidden draw reaches no pixel, and drawing works again after it */
static void
test_prewarm_hidden_draw (void)
{
    RenderTexture2D target;
    Image           image;
    Color          *pixels;
    Color           red = { 255, 0, 0, 255 };
    Color           green = { 0, 255, 0, 255 };
    Color           blue = { 0, 0, 255, 255 };
    Color           white = { 255, 255, 255, 255 };

    SKIP_IF_NO_WINDOW ();

    target = LoadRenderTexture (8, 8);
    g_assert_cmpuint (target.id, !=, 0);

    BeginTextureMode (target);
    ClearBackground (red);
    lrg_prewarm_begin_hidden_draw ();
    DrawRectangle (0, 0, 8, 8, green);
    ClearBackground (blue);
    lrg_prewarm_end_hidden_draw ();
    DrawRectangle (0, 0, 4, 8, white);
    EndTextureMode ();

    image = LoadImageFromTexture (target.texture);
    pixels = LoadImageColors (image);
    /* the left half drawn after the hidden part, the right still red */
    g_assert_cmpuint (pixels[0].r, ==, 255);
    g_assert_cmpuint (pixels[0].g, ==, 255);
    g_assert_cmpuint (pixels[7].r, ==, 255);
    g_assert_cmpuint (pixels[7].g, ==, 0);
    g_assert_cmpuint (pixels[7].b, ==, 0);
    UnloadImageColors (pixels);
    UnloadImage (image);
    UnloadRenderTexture (target);
}

static void
count_signal (LrgTemplateLoadingState *state,
              gpointer                 user_data)
{
    guint *count = user_data;

    (*count)++;
}

static gboolean
load_task (gpointer   user_data,
           GError   **error)
{
    return TRUE;
}

/* the loading state runs the queue after its tasks, emits prewarm-draw
 * once, and only then completes */
static void
test_prewarm_loading_state (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;
    g_autoptr(GString)                 log = g_string_new (NULL);
    LrgPrewarm                        *prewarm = lrg_prewarm_get_default ();
    guint                              draws = 0;
    guint                              complete = 0;
    guint                              frame;

    SKIP_IF_NO_WINDOW ();

    lrg_prewarm_clear (prewarm);
    state = lrg_template_loading_state_new ();
    g_assert_true (lrg_template_loading_state_get_prewarm (state));
    g_assert_cmpint (lrg_template_loading_state_get_prewarm_budget (state), >, 0);
    lrg_template_loading_state_set_prewarm_budget (state, 0);    /* one entry a frame */
    lrg_template_loading_state_set_minimum_display_time (state, 0.0);
    lrg_template_loading_state_add_task (state, "load", load_task, NULL, NULL);
    add_step (prewarm, log, 'a', 1, NULL);
    add_step (prewarm, log, 'b', 1, NULL);
    g_signal_connect (state, "prewarm-draw", G_CALLBACK (count_signal), &draws);
    g_signal_connect (state, "complete", G_CALLBACK (count_signal), &complete);

    lrg_game_state_enter (LRG_GAME_STATE (state));
    for (frame = 0; frame < 10 && complete == 0; frame++)
    {
        lrg_game_state_update (LRG_GAME_STATE (state), 1.0 / 60.0);
        /* the task runs on frame 0, a then b on frames 1 and 2 */
        if (frame == 0)
            g_assert_cmpstr (log->str, ==, "");
        if (frame == 1)
            g_assert_cmpstr (log->str, ==, "a");
        if (frame < 3)
            g_assert_false (lrg_template_loading_state_is_complete (state));
        BeginDrawing ();
        lrg_game_state_draw (LRG_GAME_STATE (state));
        EndDrawing ();
    }
    g_assert_cmpstr (log->str, ==, "ab");
    g_assert_cmpuint (draws, ==, 1);
    g_assert_cmpuint (complete, ==, 1);
    g_assert_true (lrg_template_loading_state_is_complete (state));
    lrg_game_state_exit (LRG_GAME_STATE (state));

    /* turned off, the queue is left alone */
    add_step (prewarm, log, 'c', 1, NULL);
    lrg_template_loading_state_set_prewarm (state, FALSE);
    complete = 0;
    lrg_game_state_enter (LRG_GAME_STATE (state));
    lrg_game_state_update (LRG_GAME_STATE (state), 1.0 / 60.0);
    g_assert_cmpuint (complete, ==, 1);
    g_assert_cmpstr (log->str, ==, "ab");
    lrg_game_state_exit (LRG_GAME_STATE (state));
    lrg_prewarm_clear (prewarm);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    /* headless first: no window exists yet */
    g_test_add_func ("/prewarm/no-window", test_prewarm_no_window);
    g_test_add_func ("/prewarm/weak-object", test_prewarm_weak_object);
    g_test_add_func ("/prewarm/track", test_prewarm_track);
    g_test_add_func ("/prewarm/sweep", test_prewarm_sweep);
    g_test_add_func ("/prewarm/run", test_prewarm_run);
    g_test_add_func ("/prewarm/renderers", test_prewarm_renderers);
    g_test_add_func ("/prewarm/hidden-draw", test_prewarm_hidden_draw);
    g_test_add_func ("/prewarm/loading-state", test_prewarm_loading_state);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
