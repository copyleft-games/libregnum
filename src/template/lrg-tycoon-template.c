/* lrg-tycoon-template.c - Tycoon/management game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include "lrg-tycoon-template.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <raylib.h>
#include <math.h>

/* Default values */
#define LRG_TYCOON_DEFAULT_DAY_LENGTH      (60.0f)    /* 1 minute real = 1 day */
#define LRG_TYCOON_DEFAULT_TICK_INTERVAL   (1.0f)     /* Economy tick every second */
#define LRG_TYCOON_DEFAULT_GRID_SIZE       (32.0f)
#define LRG_TYCOON_DEFAULT_PAN_SPEED       (400.0f)
#define LRG_TYCOON_DEFAULT_ZOOM_SPEED      (0.1f)
#define LRG_TYCOON_DEFAULT_MIN_ZOOM        (0.25f)
#define LRG_TYCOON_DEFAULT_MAX_ZOOM        (4.0f)
#define LRG_TYCOON_DEFAULT_EDGE_PAN_MARGIN (32)
#define LRG_TYCOON_DEFAULT_STARTING_MONEY  (10000)

typedef struct
{
    /* Time control */
    LrgTimeSpeed time_speed;
    LrgTimeSpeed prev_speed;      /* For toggle pause */
    gfloat day_length;            /* Seconds per day at 1x */
    gfloat day_timer;             /* Current day progress */
    guint current_day;

    /* Economy tick */
    gfloat tick_interval;
    gfloat tick_timer;

    /* Overlay */
    LrgTycoonOverlay overlay;

    /* Build mode */
    gboolean build_mode;
    gboolean show_grid;
    gfloat grid_size;

    /* Camera controls */
    gfloat pan_speed;
    gfloat zoom_speed;
    gfloat min_zoom;
    gfloat max_zoom;
    gint edge_pan_margin;
    gfloat current_zoom;

    /* Resources */
    gint64 money;

    /* Camera position for pan */
    gfloat camera_x;
    gfloat camera_y;

    /* Camera bounds (clamped after panning) */
    gboolean camera_bounds_enabled;
    gfloat camera_min_x;
    gfloat camera_min_y;
    gfloat camera_max_x;
    gfloat camera_max_y;
} LrgTycoonTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTycoonTemplate, lrg_tycoon_template,
                            LRG_TYPE_GAME_2D_TEMPLATE)

/* Property IDs */
enum
{
    PROP_0,
    PROP_TIME_SPEED,
    PROP_DAY,
    PROP_DAY_LENGTH,
    PROP_TICK_INTERVAL,
    PROP_OVERLAY,
    PROP_BUILD_MODE,
    PROP_SHOW_GRID,
    PROP_GRID_SIZE,
    PROP_PAN_SPEED,
    PROP_ZOOM_SPEED,
    PROP_MIN_ZOOM,
    PROP_MAX_ZOOM,
    PROP_EDGE_PAN_MARGIN,
    PROP_MONEY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signal IDs */
enum
{
    SIGNAL_TIME_SPEED_CHANGED,
    SIGNAL_OVERLAY_CHANGED,
    SIGNAL_ECONOMY_TICK,
    SIGNAL_DAY_CHANGED,
    SIGNAL_BUILD_MODE_ENTER,
    SIGNAL_BUILD_MODE_EXIT,
    SIGNAL_MONEY_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static gfloat
get_speed_multiplier (LrgTimeSpeed speed)
{
    switch (speed)
    {
        case LRG_TIME_PAUSED:  return 0.0f;
        case LRG_TIME_NORMAL:  return 1.0f;
        case LRG_TIME_FAST:    return 2.0f;
        case LRG_TIME_FASTER:  return 3.0f;
        case LRG_TIME_FASTEST: return 4.0f;
        default:               return 1.0f;
    }
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_tycoon_template_real_on_time_speed_changed (LrgTycoonTemplate *self,
                                                LrgTimeSpeed       old_speed,
                                                LrgTimeSpeed       new_speed)
{
    /* Default: no-op */
    (void)self;
    (void)old_speed;
    (void)new_speed;
}

static void
lrg_tycoon_template_real_on_overlay_changed (LrgTycoonTemplate *self,
                                             LrgTycoonOverlay   old_overlay,
                                             LrgTycoonOverlay   new_overlay)
{
    /* Default: no-op */
    (void)self;
    (void)old_overlay;
    (void)new_overlay;
}

static void
lrg_tycoon_template_real_on_economy_tick (LrgTycoonTemplate *self)
{
    /* Default: emit signal only */
    g_signal_emit (self, signals[SIGNAL_ECONOMY_TICK], 0);
}

static void
lrg_tycoon_template_real_on_day_changed (LrgTycoonTemplate *self,
                                         guint              day)
{
    /* Default: emit signal only */
    g_signal_emit (self, signals[SIGNAL_DAY_CHANGED], 0, day);
}

static void
lrg_tycoon_template_real_on_build_mode_enter (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    priv = lrg_tycoon_template_get_instance_private (self);

    priv->show_grid = TRUE;
    g_signal_emit (self, signals[SIGNAL_BUILD_MODE_ENTER], 0);
}

static void
lrg_tycoon_template_real_on_build_mode_exit (LrgTycoonTemplate *self)
{
    g_signal_emit (self, signals[SIGNAL_BUILD_MODE_EXIT], 0);
}

static void
lrg_tycoon_template_real_update_economy (LrgTycoonTemplate *self,
                                         gdouble            delta)
{
    LrgTycoonTemplateClass *klass;
    LrgTycoonTemplatePrivate *priv;

    priv = lrg_tycoon_template_get_instance_private (self);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);

    if (priv->time_speed == LRG_TIME_PAUSED)
        return;

    /* Update economy tick timer */
    priv->tick_timer += (gfloat)delta;
    while (priv->tick_timer >= priv->tick_interval)
    {
        priv->tick_timer -= priv->tick_interval;

        if (klass->on_economy_tick != NULL)
            klass->on_economy_tick (self);
    }

    /* Update day timer */
    priv->day_timer += (gfloat)delta;
    while (priv->day_timer >= priv->day_length)
    {
        priv->day_timer -= priv->day_length;
        priv->current_day++;

        if (klass->on_day_changed != NULL)
            klass->on_day_changed (self, priv->current_day);
    }
}

static void
lrg_tycoon_template_real_draw_overlay (LrgTycoonTemplate *self)
{
    /* Default: no overlay rendering */
    (void)self;
}

static void
lrg_tycoon_template_real_draw_grid (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;
    LrgCamera2D *camera;
    gfloat cam_x;
    gfloat cam_y;
    gfloat zoom;
    gint virt_w;
    gint virt_h;
    gfloat view_w;
    gfloat view_h;
    gint start_x;
    gint end_x;
    gint start_y;
    gint end_y;
    gint i;
    g_autoptr(GrlColor) grid_color = NULL;

    priv = lrg_tycoon_template_get_instance_private (self);

    camera = lrg_game_2d_template_get_camera (LRG_GAME_2D_TEMPLATE (self));
    if (camera == NULL)
        return;

    /* Get camera position and zoom */
    {
        GrlVector2 *target = lrg_camera2d_get_target (camera);
        cam_x = grl_vector2_get_x (target);
        cam_y = grl_vector2_get_y (target);
    }
    zoom = lrg_camera2d_get_zoom (camera);

    /* Get virtual resolution */
    virt_w = lrg_game_2d_template_get_virtual_width (LRG_GAME_2D_TEMPLATE (self));
    virt_h = lrg_game_2d_template_get_virtual_height (LRG_GAME_2D_TEMPLATE (self));

    /* Calculate visible area */
    view_w = (gfloat)virt_w / zoom;
    view_h = (gfloat)virt_h / zoom;

    start_x = (gint)floorf ((cam_x - view_w / 2.0f) / priv->grid_size) - 1;
    end_x = (gint)ceilf ((cam_x + view_w / 2.0f) / priv->grid_size) + 1;
    start_y = (gint)floorf ((cam_y - view_h / 2.0f) / priv->grid_size) - 1;
    end_y = (gint)ceilf ((cam_y + view_h / 2.0f) / priv->grid_size) + 1;

    grid_color = grl_color_new (100, 100, 100, 100);

    /* Draw vertical lines */
    for (i = start_x; i <= end_x; i++)
    {
        gfloat x;
        gint y1;
        gint y2;

        x = (gfloat)i * priv->grid_size;
        y1 = (gint)((gfloat)start_y * priv->grid_size);
        y2 = (gint)((gfloat)end_y * priv->grid_size);

        grl_draw_line ((gint)x, y1, (gint)x, y2, grid_color);
    }

    /* Draw horizontal lines */
    for (i = start_y; i <= end_y; i++)
    {
        gfloat y;
        gint x1;
        gint x2;

        y = (gfloat)i * priv->grid_size;
        x1 = (gint)((gfloat)start_x * priv->grid_size);
        x2 = (gint)((gfloat)end_x * priv->grid_size);

        grl_draw_line (x1, (gint)y, x2, (gint)y, grid_color);
    }
}

static void
lrg_tycoon_template_real_draw_resources_hud (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;
    g_autofree gchar *money_text = NULL;
    g_autofree gchar *day_text = NULL;
    g_autofree gchar *speed_text = NULL;
    g_autoptr(GrlColor) text_color = NULL;

    priv = lrg_tycoon_template_get_instance_private (self);

    text_color = grl_color_new (255, 255, 255, 255);

    /* Money display */
    money_text = g_strdup_printf ("$%ld", (long)priv->money);
    grl_draw_text (money_text, 10, 10, 20, text_color);

    /* Day display */
    day_text = g_strdup_printf ("Day %u", priv->current_day);
    grl_draw_text (day_text, 10, 35, 20, text_color);

    /* Speed indicator */
    switch (priv->time_speed)
    {
        case LRG_TIME_PAUSED:  speed_text = g_strdup ("||"); break;
        case LRG_TIME_NORMAL:  speed_text = g_strdup (">"); break;
        case LRG_TIME_FAST:    speed_text = g_strdup (">>"); break;
        case LRG_TIME_FASTER:  speed_text = g_strdup (">>>"); break;
        case LRG_TIME_FASTEST: speed_text = g_strdup (">>>>"); break;
        default:               speed_text = g_strdup (">"); break;
    }
    grl_draw_text (speed_text, 10, 60, 20, text_color);
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

/*
 * update_camera: Override the 2D template's camera-follow logic with a no-op.
 *
 * The tycoon template manages its own camera panning (WASD, arrows, edge pan)
 * in pre_update and applies camera_x/camera_y directly to the camera target.
 * The parent's update_camera would fight this by lerping toward its own
 * camera_target (default 0,0), effectively undoing our positioning.
 */
static void
lrg_tycoon_template_update_camera (LrgGame2DTemplate *template,
                                    gdouble            delta)
{
    (void) template;
    (void) delta;
    /* Intentionally empty — camera is managed in pre_update */
}

static void
lrg_tycoon_template_pre_update (LrgGameTemplate *template,
                                gdouble          delta)
{
    LrgTycoonTemplate *self;
    LrgTycoonTemplateClass *klass;
    LrgTycoonTemplatePrivate *priv;
    LrgCamera2D *camera;
    gfloat scaled_delta;
    gfloat pan_x;
    gfloat pan_y;
    gint mouse_x;
    gint mouse_y;
    gint screen_w;
    gint screen_h;
    gfloat wheel;

    self = LRG_TYCOON_TEMPLATE (template);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);
    priv = lrg_tycoon_template_get_instance_private (self);

    /* Handle time control hotkeys */
    if (IsKeyPressed (KEY_SPACE))
        lrg_tycoon_template_toggle_pause (self);

    if (IsKeyPressed (KEY_ONE))
        lrg_tycoon_template_set_time_speed (self, LRG_TIME_NORMAL);
    else if (IsKeyPressed (KEY_TWO))
        lrg_tycoon_template_set_time_speed (self, LRG_TIME_FAST);
    else if (IsKeyPressed (KEY_THREE))
        lrg_tycoon_template_set_time_speed (self, LRG_TIME_FASTER);
    else if (IsKeyPressed (KEY_FOUR))
        lrg_tycoon_template_set_time_speed (self, LRG_TIME_FASTEST);

    /* Toggle grid with G */
    if (IsKeyPressed (KEY_G))
        priv->show_grid = !priv->show_grid;

    /* Toggle build mode with B */
    if (IsKeyPressed (KEY_B))
    {
        if (priv->build_mode)
            lrg_tycoon_template_exit_build_mode (self);
        else
            lrg_tycoon_template_enter_build_mode (self);
    }

    /* Cycle overlays with Tab */
    if (IsKeyPressed (KEY_TAB))
    {
        LrgTycoonOverlay new_overlay;

        new_overlay = (priv->overlay + 1) % (LRG_TYCOON_OVERLAY_CUSTOM + 1);
        lrg_tycoon_template_set_overlay (self, new_overlay);
    }

    /* Camera panning with keyboard */
    pan_x = 0.0f;
    pan_y = 0.0f;

    if (IsKeyDown (KEY_RIGHT) || IsKeyDown (KEY_D))
        pan_x += 1.0f;
    if (IsKeyDown (KEY_LEFT) || IsKeyDown (KEY_A))
        pan_x -= 1.0f;
    if (IsKeyDown (KEY_DOWN) || IsKeyDown (KEY_S))
        pan_y += 1.0f;
    if (IsKeyDown (KEY_UP) || IsKeyDown (KEY_W))
        pan_y -= 1.0f;

    /* Edge panning */
    if (priv->edge_pan_margin > 0)
    {
        mouse_x = GetMouseX ();
        mouse_y = GetMouseY ();
        screen_w = GetScreenWidth ();
        screen_h = GetScreenHeight ();

        if (mouse_x < priv->edge_pan_margin)
            pan_x -= 1.0f;
        else if (mouse_x > screen_w - priv->edge_pan_margin)
            pan_x += 1.0f;

        if (mouse_y < priv->edge_pan_margin)
            pan_y -= 1.0f;
        else if (mouse_y > screen_h - priv->edge_pan_margin)
            pan_y += 1.0f;
    }

    /* Apply pan */
    if (pan_x != 0.0f || pan_y != 0.0f)
    {
        gfloat len;

        len = sqrtf (pan_x * pan_x + pan_y * pan_y);
        if (len > 0.0f)
        {
            pan_x /= len;
            pan_y /= len;
        }

        priv->camera_x += pan_x * priv->pan_speed * (gfloat)delta / priv->current_zoom;
        priv->camera_y += pan_y * priv->pan_speed * (gfloat)delta / priv->current_zoom;
    }

    /* Clamp camera to bounds if enabled */
    if (priv->camera_bounds_enabled)
    {
        priv->camera_x = CLAMP (priv->camera_x, priv->camera_min_x, priv->camera_max_x);
        priv->camera_y = CLAMP (priv->camera_y, priv->camera_min_y, priv->camera_max_y);
    }

    /* Mouse wheel zoom */
    wheel = GetMouseWheelMove ();
    if (wheel != 0.0f)
    {
        priv->current_zoom += wheel * priv->zoom_speed;
        priv->current_zoom = CLAMP (priv->current_zoom, priv->min_zoom, priv->max_zoom);
    }

    /* Apply camera position and zoom */
    camera = lrg_game_2d_template_get_camera (LRG_GAME_2D_TEMPLATE (self));
    if (camera != NULL)
    {
        lrg_camera2d_set_target_xy (camera, priv->camera_x, priv->camera_y);
        lrg_camera2d_set_zoom (camera, priv->current_zoom);
    }

    /* Update economy (with time scaling) */
    scaled_delta = delta * get_speed_multiplier (priv->time_speed);
    if (klass->update_economy != NULL)
        klass->update_economy (self, scaled_delta);

    /* Chain up */
    LRG_GAME_TEMPLATE_CLASS (lrg_tycoon_template_parent_class)->pre_update (template, delta);
}

static void
lrg_tycoon_template_draw_world (LrgGame2DTemplate *template)
{
    LrgTycoonTemplate *self;
    LrgTycoonTemplateClass *klass;
    LrgTycoonTemplatePrivate *priv;

    self = LRG_TYCOON_TEMPLATE (template);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);
    priv = lrg_tycoon_template_get_instance_private (self);

    /* Draw grid if enabled */
    if (priv->show_grid && klass->draw_grid != NULL)
        klass->draw_grid (self);

    /* Draw overlay */
    if (priv->overlay != LRG_TYCOON_OVERLAY_NONE && klass->draw_overlay != NULL)
        klass->draw_overlay (self);

    /* Chain up */
    LRG_GAME_2D_TEMPLATE_CLASS (lrg_tycoon_template_parent_class)->draw_world (template);
}

static void
lrg_tycoon_template_draw_ui (LrgGame2DTemplate *template)
{
    LrgTycoonTemplate *self;
    LrgTycoonTemplateClass *klass;

    self = LRG_TYCOON_TEMPLATE (template);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);

    /* Draw resources HUD */
    if (klass->draw_resources_hud != NULL)
        klass->draw_resources_hud (self);

    /* Chain up */
    LRG_GAME_2D_TEMPLATE_CLASS (lrg_tycoon_template_parent_class)->draw_ui (template);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_tycoon_template_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgTycoonTemplate *self = LRG_TYCOON_TEMPLATE (object);
    LrgTycoonTemplatePrivate *priv = lrg_tycoon_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_TIME_SPEED:
            lrg_tycoon_template_set_time_speed (self, g_value_get_enum (value));
            break;
        case PROP_DAY:
            lrg_tycoon_template_set_day (self, g_value_get_uint (value));
            break;
        case PROP_DAY_LENGTH:
            lrg_tycoon_template_set_day_length (self, g_value_get_float (value));
            break;
        case PROP_TICK_INTERVAL:
            lrg_tycoon_template_set_tick_interval (self, g_value_get_float (value));
            break;
        case PROP_OVERLAY:
            lrg_tycoon_template_set_overlay (self, g_value_get_enum (value));
            break;
        case PROP_BUILD_MODE:
            if (g_value_get_boolean (value))
                lrg_tycoon_template_enter_build_mode (self);
            else
                lrg_tycoon_template_exit_build_mode (self);
            break;
        case PROP_SHOW_GRID:
            lrg_tycoon_template_set_show_grid (self, g_value_get_boolean (value));
            break;
        case PROP_GRID_SIZE:
            lrg_tycoon_template_set_grid_size (self, g_value_get_float (value));
            break;
        case PROP_PAN_SPEED:
            lrg_tycoon_template_set_pan_speed (self, g_value_get_float (value));
            break;
        case PROP_ZOOM_SPEED:
            lrg_tycoon_template_set_zoom_speed (self, g_value_get_float (value));
            break;
        case PROP_MIN_ZOOM:
            priv->min_zoom = g_value_get_float (value);
            break;
        case PROP_MAX_ZOOM:
            priv->max_zoom = g_value_get_float (value);
            break;
        case PROP_EDGE_PAN_MARGIN:
            lrg_tycoon_template_set_edge_pan_margin (self, g_value_get_int (value));
            break;
        case PROP_MONEY:
            lrg_tycoon_template_set_money (self, g_value_get_int64 (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tycoon_template_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgTycoonTemplate *self = LRG_TYCOON_TEMPLATE (object);
    LrgTycoonTemplatePrivate *priv = lrg_tycoon_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_TIME_SPEED:
            g_value_set_enum (value, priv->time_speed);
            break;
        case PROP_DAY:
            g_value_set_uint (value, priv->current_day);
            break;
        case PROP_DAY_LENGTH:
            g_value_set_float (value, priv->day_length);
            break;
        case PROP_TICK_INTERVAL:
            g_value_set_float (value, priv->tick_interval);
            break;
        case PROP_OVERLAY:
            g_value_set_enum (value, priv->overlay);
            break;
        case PROP_BUILD_MODE:
            g_value_set_boolean (value, priv->build_mode);
            break;
        case PROP_SHOW_GRID:
            g_value_set_boolean (value, priv->show_grid);
            break;
        case PROP_GRID_SIZE:
            g_value_set_float (value, priv->grid_size);
            break;
        case PROP_PAN_SPEED:
            g_value_set_float (value, priv->pan_speed);
            break;
        case PROP_ZOOM_SPEED:
            g_value_set_float (value, priv->zoom_speed);
            break;
        case PROP_MIN_ZOOM:
            g_value_set_float (value, priv->min_zoom);
            break;
        case PROP_MAX_ZOOM:
            g_value_set_float (value, priv->max_zoom);
            break;
        case PROP_EDGE_PAN_MARGIN:
            g_value_set_int (value, priv->edge_pan_margin);
            break;
        case PROP_MONEY:
            g_value_set_int64 (value, priv->money);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tycoon_template_class_init (LrgTycoonTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame2DTemplateClass *template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    object_class->set_property = lrg_tycoon_template_set_property;
    object_class->get_property = lrg_tycoon_template_get_property;

    /* Override parent virtuals */
    template_class->pre_update = lrg_tycoon_template_pre_update;
    template_2d_class->update_camera = lrg_tycoon_template_update_camera;
    template_2d_class->draw_world = lrg_tycoon_template_draw_world;
    template_2d_class->draw_ui = lrg_tycoon_template_draw_ui;

    /* Set up class virtuals */
    klass->on_time_speed_changed = lrg_tycoon_template_real_on_time_speed_changed;
    klass->on_overlay_changed = lrg_tycoon_template_real_on_overlay_changed;
    klass->on_economy_tick = lrg_tycoon_template_real_on_economy_tick;
    klass->on_day_changed = lrg_tycoon_template_real_on_day_changed;
    klass->on_build_mode_enter = lrg_tycoon_template_real_on_build_mode_enter;
    klass->on_build_mode_exit = lrg_tycoon_template_real_on_build_mode_exit;
    klass->update_economy = lrg_tycoon_template_real_update_economy;
    klass->draw_overlay = lrg_tycoon_template_real_draw_overlay;
    klass->draw_grid = lrg_tycoon_template_real_draw_grid;
    klass->draw_resources_hud = lrg_tycoon_template_real_draw_resources_hud;

    /* Properties */
    properties[PROP_TIME_SPEED] =
        g_param_spec_int ("time-speed", NULL, NULL,
                          LRG_TIME_PAUSED, LRG_TIME_FASTEST,
                          LRG_TIME_NORMAL,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DAY] =
        g_param_spec_uint ("day", NULL, NULL,
                           1, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DAY_LENGTH] =
        g_param_spec_float ("day-length", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_TYCOON_DEFAULT_DAY_LENGTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TICK_INTERVAL] =
        g_param_spec_float ("tick-interval", NULL, NULL,
                            0.1f, G_MAXFLOAT, LRG_TYCOON_DEFAULT_TICK_INTERVAL,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_OVERLAY] =
        g_param_spec_int ("overlay", NULL, NULL,
                          LRG_TYCOON_OVERLAY_NONE, LRG_TYCOON_OVERLAY_CUSTOM,
                          LRG_TYCOON_OVERLAY_NONE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BUILD_MODE] =
        g_param_spec_boolean ("build-mode", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_GRID] =
        g_param_spec_boolean ("show-grid", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRID_SIZE] =
        g_param_spec_float ("grid-size", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_TYCOON_DEFAULT_GRID_SIZE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PAN_SPEED] =
        g_param_spec_float ("pan-speed", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_TYCOON_DEFAULT_PAN_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ZOOM_SPEED] =
        g_param_spec_float ("zoom-speed", NULL, NULL,
                            0.01f, 1.0f, LRG_TYCOON_DEFAULT_ZOOM_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MIN_ZOOM] =
        g_param_spec_float ("min-zoom", NULL, NULL,
                            0.1f, 10.0f, LRG_TYCOON_DEFAULT_MIN_ZOOM,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_ZOOM] =
        g_param_spec_float ("max-zoom", NULL, NULL,
                            0.1f, 10.0f, LRG_TYCOON_DEFAULT_MAX_ZOOM,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_EDGE_PAN_MARGIN] =
        g_param_spec_int ("edge-pan-margin", NULL, NULL,
                          0, 200, LRG_TYCOON_DEFAULT_EDGE_PAN_MARGIN,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MONEY] =
        g_param_spec_int64 ("money", NULL, NULL,
                            G_MININT64, G_MAXINT64, LRG_TYCOON_DEFAULT_STARTING_MONEY,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */
    signals[SIGNAL_TIME_SPEED_CHANGED] =
        g_signal_new ("time-speed-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    signals[SIGNAL_OVERLAY_CHANGED] =
        g_signal_new ("overlay-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    signals[SIGNAL_ECONOMY_TICK] =
        g_signal_new ("economy-tick",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_DAY_CHANGED] =
        g_signal_new ("day-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    signals[SIGNAL_BUILD_MODE_ENTER] =
        g_signal_new ("build-mode-enter",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_BUILD_MODE_EXIT] =
        g_signal_new ("build-mode-exit",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_MONEY_CHANGED] =
        g_signal_new ("money-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT64, G_TYPE_INT64);
}

static void
lrg_tycoon_template_init (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    priv = lrg_tycoon_template_get_instance_private (self);

    priv->time_speed = LRG_TIME_NORMAL;
    priv->prev_speed = LRG_TIME_NORMAL;
    priv->day_length = LRG_TYCOON_DEFAULT_DAY_LENGTH;
    priv->day_timer = 0.0f;
    priv->current_day = 1;

    priv->tick_interval = LRG_TYCOON_DEFAULT_TICK_INTERVAL;
    priv->tick_timer = 0.0f;

    priv->overlay = LRG_TYCOON_OVERLAY_NONE;

    priv->build_mode = FALSE;
    priv->show_grid = FALSE;
    priv->grid_size = LRG_TYCOON_DEFAULT_GRID_SIZE;

    priv->pan_speed = LRG_TYCOON_DEFAULT_PAN_SPEED;
    priv->zoom_speed = LRG_TYCOON_DEFAULT_ZOOM_SPEED;
    priv->min_zoom = LRG_TYCOON_DEFAULT_MIN_ZOOM;
    priv->max_zoom = LRG_TYCOON_DEFAULT_MAX_ZOOM;
    priv->edge_pan_margin = LRG_TYCOON_DEFAULT_EDGE_PAN_MARGIN;
    priv->current_zoom = 1.0f;

    priv->money = LRG_TYCOON_DEFAULT_STARTING_MONEY;

    priv->camera_x = 0.0f;
    priv->camera_y = 0.0f;

    priv->camera_bounds_enabled = FALSE;
    priv->camera_min_x = 0.0f;
    priv->camera_min_y = 0.0f;
    priv->camera_max_x = 0.0f;
    priv->camera_max_y = 0.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTycoonTemplate *
lrg_tycoon_template_new (void)
{
    return g_object_new (LRG_TYPE_TYCOON_TEMPLATE, NULL);
}

LrgTimeSpeed
lrg_tycoon_template_get_time_speed (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), LRG_TIME_NORMAL);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->time_speed;
}

void
lrg_tycoon_template_set_time_speed (LrgTycoonTemplate *self,
                                    LrgTimeSpeed       speed)
{
    LrgTycoonTemplateClass *klass;
    LrgTycoonTemplatePrivate *priv;
    LrgTimeSpeed old_speed;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);

    if (priv->time_speed == speed)
        return;

    old_speed = priv->time_speed;

    /* Save previous non-paused speed for toggle */
    if (old_speed != LRG_TIME_PAUSED)
        priv->prev_speed = old_speed;

    priv->time_speed = speed;

    if (klass->on_time_speed_changed != NULL)
        klass->on_time_speed_changed (self, old_speed, speed);

    g_signal_emit (self, signals[SIGNAL_TIME_SPEED_CHANGED], 0, old_speed, speed);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_SPEED]);
}

void
lrg_tycoon_template_toggle_pause (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);

    if (priv->time_speed == LRG_TIME_PAUSED)
        lrg_tycoon_template_set_time_speed (self, priv->prev_speed);
    else
        lrg_tycoon_template_set_time_speed (self, LRG_TIME_PAUSED);
}

gboolean
lrg_tycoon_template_is_paused (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), FALSE);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->time_speed == LRG_TIME_PAUSED;
}

gfloat
lrg_tycoon_template_get_time_multiplier (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 1.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return get_speed_multiplier (priv->time_speed);
}

guint
lrg_tycoon_template_get_day (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 1);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->current_day;
}

void
lrg_tycoon_template_set_day (LrgTycoonTemplate *self,
                             guint              day)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (day > 0);

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->current_day = day;
    priv->day_timer = 0.0f;
}

gfloat
lrg_tycoon_template_get_day_progress (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 0.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->day_timer / priv->day_length;
}

gfloat
lrg_tycoon_template_get_day_length (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 60.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->day_length;
}

void
lrg_tycoon_template_set_day_length (LrgTycoonTemplate *self,
                                    gfloat             seconds)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (seconds >= 1.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->day_length = seconds;
}

gfloat
lrg_tycoon_template_get_tick_interval (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 1.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->tick_interval;
}

void
lrg_tycoon_template_set_tick_interval (LrgTycoonTemplate *self,
                                       gfloat             seconds)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (seconds >= 0.1f);

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->tick_interval = seconds;
}

LrgTycoonOverlay
lrg_tycoon_template_get_overlay (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), LRG_TYCOON_OVERLAY_NONE);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->overlay;
}

void
lrg_tycoon_template_set_overlay (LrgTycoonTemplate *self,
                                 LrgTycoonOverlay   overlay)
{
    LrgTycoonTemplateClass *klass;
    LrgTycoonTemplatePrivate *priv;
    LrgTycoonOverlay old_overlay;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);

    if (priv->overlay == overlay)
        return;

    old_overlay = priv->overlay;
    priv->overlay = overlay;

    if (klass->on_overlay_changed != NULL)
        klass->on_overlay_changed (self, old_overlay, overlay);

    g_signal_emit (self, signals[SIGNAL_OVERLAY_CHANGED], 0, old_overlay, overlay);
}

gboolean
lrg_tycoon_template_is_build_mode (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), FALSE);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->build_mode;
}

void
lrg_tycoon_template_enter_build_mode (LrgTycoonTemplate *self)
{
    LrgTycoonTemplateClass *klass;
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);

    if (priv->build_mode)
        return;

    priv->build_mode = TRUE;

    if (klass->on_build_mode_enter != NULL)
        klass->on_build_mode_enter (self);
}

void
lrg_tycoon_template_exit_build_mode (LrgTycoonTemplate *self)
{
    LrgTycoonTemplateClass *klass;
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    klass = LRG_TYCOON_TEMPLATE_GET_CLASS (self);

    if (!priv->build_mode)
        return;

    priv->build_mode = FALSE;

    if (klass->on_build_mode_exit != NULL)
        klass->on_build_mode_exit (self);
}

gboolean
lrg_tycoon_template_get_show_grid (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), FALSE);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->show_grid;
}

void
lrg_tycoon_template_set_show_grid (LrgTycoonTemplate *self,
                                   gboolean           show)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->show_grid = show;
}

gfloat
lrg_tycoon_template_get_grid_size (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 32.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->grid_size;
}

void
lrg_tycoon_template_set_grid_size (LrgTycoonTemplate *self,
                                   gfloat             size)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (size >= 1.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->grid_size = size;
}

void
lrg_tycoon_template_snap_to_grid (LrgTycoonTemplate *self,
                                  gfloat             x,
                                  gfloat             y,
                                  gfloat            *grid_x,
                                  gfloat            *grid_y)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (grid_x != NULL);
    g_return_if_fail (grid_y != NULL);

    priv = lrg_tycoon_template_get_instance_private (self);

    *grid_x = floorf (x / priv->grid_size) * priv->grid_size + priv->grid_size / 2.0f;
    *grid_y = floorf (y / priv->grid_size) * priv->grid_size + priv->grid_size / 2.0f;
}

void
lrg_tycoon_template_world_to_grid (LrgTycoonTemplate *self,
                                   gfloat             world_x,
                                   gfloat             world_y,
                                   gint              *grid_x,
                                   gint              *grid_y)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (grid_x != NULL);
    g_return_if_fail (grid_y != NULL);

    priv = lrg_tycoon_template_get_instance_private (self);

    *grid_x = (gint)floorf (world_x / priv->grid_size);
    *grid_y = (gint)floorf (world_y / priv->grid_size);
}

void
lrg_tycoon_template_grid_to_world (LrgTycoonTemplate *self,
                                   gint               grid_x,
                                   gint               grid_y,
                                   gfloat            *world_x,
                                   gfloat            *world_y)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (world_x != NULL);
    g_return_if_fail (world_y != NULL);

    priv = lrg_tycoon_template_get_instance_private (self);

    *world_x = (gfloat)grid_x * priv->grid_size + priv->grid_size / 2.0f;
    *world_y = (gfloat)grid_y * priv->grid_size + priv->grid_size / 2.0f;
}

gfloat
lrg_tycoon_template_get_pan_speed (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 400.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->pan_speed;
}

void
lrg_tycoon_template_set_pan_speed (LrgTycoonTemplate *self,
                                   gfloat             speed)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->pan_speed = speed;
}

gfloat
lrg_tycoon_template_get_zoom_speed (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 0.1f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->zoom_speed;
}

void
lrg_tycoon_template_set_zoom_speed (LrgTycoonTemplate *self,
                                    gfloat             speed)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->zoom_speed = CLAMP (speed, 0.01f, 1.0f);
}

gfloat
lrg_tycoon_template_get_min_zoom (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 0.25f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->min_zoom;
}

gfloat
lrg_tycoon_template_get_max_zoom (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 4.0f);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->max_zoom;
}

void
lrg_tycoon_template_set_zoom_limits (LrgTycoonTemplate *self,
                                     gfloat             min_zoom,
                                     gfloat             max_zoom)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));
    g_return_if_fail (min_zoom > 0.0f);
    g_return_if_fail (max_zoom > min_zoom);

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->min_zoom = min_zoom;
    priv->max_zoom = max_zoom;

    /* Clamp current zoom to new limits */
    priv->current_zoom = CLAMP (priv->current_zoom, min_zoom, max_zoom);
}

gint
lrg_tycoon_template_get_edge_pan_margin (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 32);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->edge_pan_margin;
}

void
lrg_tycoon_template_set_edge_pan_margin (LrgTycoonTemplate *self,
                                         gint               margin)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->edge_pan_margin = MAX (margin, 0);
}

void
lrg_tycoon_template_set_camera_position (LrgTycoonTemplate *self,
                                          gfloat             x,
                                          gfloat             y)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->camera_x = x;
    priv->camera_y = y;
}

/*
 * lrg_tycoon_template_set_camera_bounds:
 *
 * Sets the camera panning bounds. When enabled, the camera position
 * is clamped to the given rectangle after every pan operation,
 * preventing the camera from leaving the world area.
 */
void
lrg_tycoon_template_set_camera_bounds (LrgTycoonTemplate *self,
                                        gfloat             min_x,
                                        gfloat             min_y,
                                        gfloat             max_x,
                                        gfloat             max_y)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    priv->camera_bounds_enabled = TRUE;
    priv->camera_min_x = min_x;
    priv->camera_min_y = min_y;
    priv->camera_max_x = max_x;
    priv->camera_max_y = max_y;
}

gint64
lrg_tycoon_template_get_money (LrgTycoonTemplate *self)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 0);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->money;
}

void
lrg_tycoon_template_set_money (LrgTycoonTemplate *self,
                               gint64             money)
{
    LrgTycoonTemplatePrivate *priv;
    gint64 old_money;

    g_return_if_fail (LRG_IS_TYCOON_TEMPLATE (self));

    priv = lrg_tycoon_template_get_instance_private (self);
    old_money = priv->money;
    priv->money = money;

    if (old_money != money)
        g_signal_emit (self, signals[SIGNAL_MONEY_CHANGED], 0, old_money, money);
}

gint64
lrg_tycoon_template_add_money (LrgTycoonTemplate *self,
                               gint64             amount)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), 0);

    priv = lrg_tycoon_template_get_instance_private (self);
    lrg_tycoon_template_set_money (self, priv->money + amount);

    return priv->money;
}

gboolean
lrg_tycoon_template_can_afford (LrgTycoonTemplate *self,
                                gint64             cost)
{
    LrgTycoonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TYCOON_TEMPLATE (self), FALSE);

    priv = lrg_tycoon_template_get_instance_private (self);
    return priv->money >= cost;
}
