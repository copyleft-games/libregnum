/* lrg-cad-app.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standalone parametric-CAD viewer for libregnum (CAD=1 builds).
 *
 * Usage:
 *   lrg-cad-app part.cad
 *
 * Loads a .cad / .ccad part through the LrgCadManager bake path and shows
 * it with an orbit camera.  The part file is watched: editing it in any
 * editor live-reloads the geometry.  Parameters can be retuned in place
 * (no text editing) -- TAB selects a parameter, +/- adjust it (clamped to
 * its range) and the part re-bakes.
 *
 * Controls:
 *   Right-mouse drag  -- orbit          Scroll -- zoom
 *   TAB               -- next parameter +/-    -- adjust selected parameter
 *   R                 -- force reload   Escape -- quit
 *
 * Build:  make CAD=1 CAD_GLIB_DIR=/path/to/cad-glib examples
 */

#include <libregnum.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef LRG_ENABLE_CAD

typedef struct
{
  gchar       *path;
  LrgCadManager *mgr;
  GHashTable  *overrides;     /* name -> gdouble*  (retuned params) */
  GPtrArray   *params;        /* CadParam* (metadata, from the document) */
  gint         selected;      /* index into params, or -1 */
  time_t       mtime;         /* last-seen file mtime */
  gdouble      orbit_yaw;
  gdouble      orbit_pitch;
  gdouble      orbit_dist;
} CadApp;

static time_t
file_mtime (const gchar *path)
{
  struct stat st;
  return (stat (path, &st) == 0) ? st.st_mtime : 0;
}

/* (Re)load param metadata from the document for the on-screen list. */
static void
cad_app_refresh_params (CadApp *app)
{
  g_autoptr (GError) error = NULL;
  CadDocument *doc = lrg_cad_manager_load (app->mgr, app->path, &error);

  g_clear_pointer (&app->params, g_ptr_array_unref);
  app->params = NULL;
  if (doc != NULL)
    {
      GPtrArray *p = cad_document_get_params (doc);
      if (p != NULL)
        app->params = g_ptr_array_ref (p);
    }
  if (app->selected >= (gint) (app->params ? app->params->len : 0))
    app->selected = app->params && app->params->len ? 0 : -1;
}

/* Bake the current part + overrides; returns the model array (transfer none). */
static GPtrArray *
cad_app_bake (CadApp *app, gdouble *out_cx, gdouble *out_cy, gdouble *out_cz,
              gdouble *out_extent)
{
  g_autoptr (GError) error = NULL;
  LrgCadBakeResult *bake =
    lrg_cad_manager_bake (app->mgr, app->path, app->overrides, 0.0, NULL,
                          &error);
  if (bake == NULL)
    {
      if (error != NULL)
        fprintf (stderr, "lrg-cad-app: bake failed: %s\n", error->message);
      return NULL;
    }
  {
    CadSolid *solid = lrg_cad_bake_result_get_solid (bake);
    gdouble mn[3], mx[3];
    cad_solid_get_bbox (solid, &mn[0], &mn[1], &mn[2], &mx[0], &mx[1], &mx[2]);
    if (out_cx) *out_cx = (mn[0] + mx[0]) * 0.5;
    if (out_cy) *out_cy = (mn[1] + mx[1]) * 0.5;
    if (out_cz) *out_cz = (mn[2] + mx[2]) * 0.5;
    if (out_extent)
      *out_extent = fmax (mx[0] - mn[0], fmax (mx[1] - mn[1], mx[2] - mn[2]));
  }
  return lrg_cad_bake_result_get_models (bake);
}

/* Adjust the selected parameter by DELTA (a fraction of its range). */
static void
cad_app_adjust (CadApp *app, gdouble dir)
{
  CadParam *p;
  gdouble cur, step, *val;

  if (app->params == NULL || app->selected < 0
      || app->selected >= (gint) app->params->len)
    return;
  p = g_ptr_array_index (app->params, app->selected);
  val = g_hash_table_lookup (app->overrides, p->name);
  cur = val ? *val : p->default_value;
  step = (p->maximum > p->minimum && p->maximum < G_MAXDOUBLE
          && p->minimum > -G_MAXDOUBLE)
         ? (p->maximum - p->minimum) / 20.0 : 1.0;
  cur += dir * step;
  if (p->minimum > -G_MAXDOUBLE) cur = fmax (cur, p->minimum);
  if (p->maximum < G_MAXDOUBLE)  cur = fmin (cur, p->maximum);
  if (val == NULL)
    {
      val = g_new (gdouble, 1);
      g_hash_table_insert (app->overrides, g_strdup (p->name), val);
    }
  *val = cur;
  lrg_cad_manager_invalidate (app->mgr, app->path);
  cad_app_refresh_params (app);
}

int
main (int argc, char *argv[])
{
  CadApp app = { 0 };
  GrlWindow *window;
  GrlCamera3D *camera;
  const gchar *display = g_getenv ("DISPLAY");
  const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

  if (argc < 2)
    {
      fprintf (stderr, "usage: %s part.cad\n", argv[0]);
      return 2;
    }
  if (!((display && display[0]) || (wayland && wayland[0])))
    {
      fprintf (stderr, "lrg-cad-app: no graphical display; nothing to do.\n");
      return 0;
    }

  app.path = g_strdup (argv[1]);
  app.mgr = lrg_cad_manager_get_default ();
  app.overrides = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, g_free);
  app.selected = -1;
  app.orbit_yaw = 0.6;
  app.orbit_pitch = 0.5;
  app.orbit_dist = 0.0;
  app.mtime = file_mtime (app.path);

  window = grl_window_new (1000, 720, "lrg-cad-app");
  camera = grl_camera3d_new ();
  cad_app_refresh_params (&app);

  while (!grl_window_should_close (window))
    {
      gdouble cx = 0, cy = 0, cz = 0, extent = 1.0;
      GPtrArray *models;
      time_t now_mtime = file_mtime (app.path);

      /* Live reload on file change. */
      if (now_mtime != 0 && now_mtime != app.mtime)
        {
          app.mtime = now_mtime;
          lrg_cad_manager_invalidate (app.mgr, app.path);
          cad_app_refresh_params (&app);
        }

      /* Input. */
      if (grl_input_is_key_pressed (GRL_KEY_R))
        {
          lrg_cad_manager_invalidate (app.mgr, app.path);
          cad_app_refresh_params (&app);
        }
      if (grl_input_is_key_pressed (GRL_KEY_TAB)
          && app.params && app.params->len)
        app.selected = (app.selected + 1) % (gint) app.params->len;
      if (grl_input_is_key_pressed (GRL_KEY_EQUAL)
          || grl_input_is_key_pressed (GRL_KEY_KP_ADD))
        cad_app_adjust (&app, +1.0);
      if (grl_input_is_key_pressed (GRL_KEY_MINUS)
          || grl_input_is_key_pressed (GRL_KEY_KP_SUBTRACT))
        cad_app_adjust (&app, -1.0);
      if (grl_input_is_mouse_button_down (GRL_MOUSE_BUTTON_RIGHT))
        {
          g_autoptr (GrlVector2) d = grl_input_get_mouse_delta ();
          if (d)
            {
              app.orbit_yaw -= d->x * 0.005;
              app.orbit_pitch += d->y * 0.005;
              app.orbit_pitch = fmax (-1.5, fmin (1.5, app.orbit_pitch));
            }
        }

      models = cad_app_bake (&app, &cx, &cy, &cz, &extent);

      if (app.orbit_dist <= 0.0)
        app.orbit_dist = extent * 2.5 + 5.0;
      app.orbit_dist -= grl_input_get_mouse_wheel_move () * extent * 0.3;
      app.orbit_dist = fmax (1.0, app.orbit_dist);

      grl_camera3d_set_target_xyz (camera, (gfloat) cx, (gfloat) cy,
                                   (gfloat) cz);
      grl_camera3d_set_position_xyz
        (camera,
         (gfloat) (cx + app.orbit_dist * cos (app.orbit_pitch)
                   * sin (app.orbit_yaw)),
         (gfloat) (cy + app.orbit_dist * sin (app.orbit_pitch)),
         (gfloat) (cz + app.orbit_dist * cos (app.orbit_pitch)
                   * cos (app.orbit_yaw)));

      grl_window_begin_drawing (window);
      {
        g_autoptr (GrlColor) bg = grl_color_new (15, 15, 22, 255);
        g_autoptr (GrlColor) tint = grl_color_new (200, 210, 235, 255);
        g_autoptr (GrlVector3) origin = grl_vector3_new (0, 0, 0);
        grl_window_clear_background (window, bg);
        grl_camera3d_begin (camera);
        grl_draw_grid (20, 1.0f);
        if (models != NULL)
          {
            guint i;
            for (i = 0; i < models->len; i++)
              grl_model_draw (g_ptr_array_index (models, i), origin, 1.0f,
                              tint);
          }
        grl_camera3d_end (camera);

        /* Parameter HUD. */
        if (app.params != NULL)
          {
            guint i;
            for (i = 0; i < app.params->len; i++)
              {
                CadParam *p = g_ptr_array_index (app.params, i);
                gdouble *ov = g_hash_table_lookup (app.overrides, p->name);
                g_autofree gchar *line =
                  g_strdup_printf ("%s%s = %g",
                                   ((gint) i == app.selected) ? "> " : "  ",
                                   p->name, ov ? *ov : p->default_value);
                g_autoptr (GrlColor) fg =
                  ((gint) i == app.selected)
                    ? grl_color_new (255, 215, 95, 255)
                    : grl_color_new (210, 210, 210, 255);
                grl_draw_text (line, 12, 12 + (gint) i * 20, 18, fg);
              }
          }
      }
      grl_window_end_drawing (window);
    }

  g_clear_pointer (&app.params, g_ptr_array_unref);
  g_hash_table_unref (app.overrides);
  g_free (app.path);
  g_object_unref (camera);
  g_object_unref (window);
  return 0;
}

#else /* !LRG_ENABLE_CAD */

#include <stdio.h>
int
main (void)
{
  fprintf (stderr, "lrg-cad-app: built without CAD support (need CAD=1).\n");
  return 0;
}

#endif /* LRG_ENABLE_CAD */
