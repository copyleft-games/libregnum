/* game-chair-simulator.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A humorous chair-testing simulation demonstrating:
 * - LrgGameStateManager / LrgGameState: Multiple game states
 * - Particle effects: Dust, sparkles, smoke, RGB
 * - Rich text effects: Shake, wave, rainbow comfort descriptions
 * - Screen shake: When sitting hard or chair breaks
 * - Tweening/easing: Smooth bounce via lrg_easing_apply()
 * - Achievement tracking: LrgAchievementManager with progress
 * - Color grading: Per-chair mood tinting
 * - Settings persistence: LrgSettings save/load
 *
 * Controls:
 *   LEFT/RIGHT   - Browse chairs
 *   ENTER/SPACE  - Sit down / Select
 *   1-5          - Rate chair (in rating screen)
 *   ESC          - Back / Quit
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>
#include <string.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define WINDOW_WIDTH        (1024)
#define WINDOW_HEIGHT       (768)
#define CHAIR_COUNT         (8)
#define MAX_PARTICLES       (300)

/* Particle types */
#define PARTICLE_DUST       (0)
#define PARTICLE_SPARKLE    (1)
#define PARTICLE_SMOKE      (2)
#define PARTICLE_RGB        (3)
#define PARTICLE_COZY       (4)

/* Rich text effect types */
#define TEXT_EFFECT_NONE    (0)
#define TEXT_EFFECT_SHAKE   (1)
#define TEXT_EFFECT_WAVE    (2)
#define TEXT_EFFECT_RAINBOW (3)

/* Color grade types */
#define GRADE_NONE          (0)
#define GRADE_WARM          (1)
#define GRADE_COOL          (2)
#define GRADE_GOLDEN        (3)
#define GRADE_GREEN         (4)

/* Timing */
#define SIT_BOUNCE_DURATION (0.6f)
#define SIT_HOLD_DURATION   (4.0f)
#define BREAK_DELAY         (0.7f)

/* Menu items */
#define MENU_START          (0)
#define MENU_QUIT           (1)
#define MENU_ITEM_COUNT     (2)

/* Chair drawing constants */
#define CHAIR_CENTER_X      (WINDOW_WIDTH / 2)
#define CHAIR_CENTER_Y      (420)

/* =============================================================================
 * CHAIR DATA
 * ========================================================================== */

typedef struct
{
    const gchar *name;
    const gchar *description;
    gint         comfort;           /* 1-10 scale */
    gint         color_grade;       /* GRADE_* constant */
    gint         particle_type;     /* PARTICLE_* constant */
    gint         text_effect;       /* TEXT_EFFECT_* constant */
    LrgEasingType easing;           /* Easing for sit animation */
    gfloat       bounce_amp;        /* Bounce amplitude in pixels */
    gfloat       shake_intensity;   /* Screen shake on sit (0 = none) */
    gboolean     breaks;            /* TRUE if chair collapses */
    const gchar *comfort_phrase;    /* Displayed during sitting */
    guint8       seat_r, seat_g, seat_b;
    guint8       back_r, back_g, back_b;
    guint8       leg_r, leg_g, leg_b;
} ChairData;

static const ChairData chairs[CHAIR_COUNT] = {
    { /* 0: Wooden Kitchen Chair */
        "Wooden Kitchen Chair",
        "Basic. Reliable. Uncomfortable.",
        3, GRADE_NONE, PARTICLE_DUST, TEXT_EFFECT_NONE,
        LRG_EASING_EASE_OUT_BOUNCE, 15.0f, 0.0f, FALSE,
        "It's... a chair.",
        139, 90, 43,
        120, 70, 30,
        100, 60, 25
    },
    { /* 1: Office Swivel Chair */
        "Office Swivel Chair",
        "Synergy. Productivity. Lumbar support.",
        5, GRADE_COOL, PARTICLE_DUST, TEXT_EFFECT_NONE,
        LRG_EASING_EASE_OUT_BOUNCE, 25.0f, 0.0f, FALSE,
        "Adequate. Corporate approved.",
        50, 50, 60,
        40, 40, 50,
        80, 80, 90
    },
    { /* 2: Gaming Chair */
        "Gaming Chair",
        "RGB makes you sit faster.",
        7, GRADE_NONE, PARTICLE_RGB, TEXT_EFFECT_RAINBOW,
        LRG_EASING_EASE_OUT_BOUNCE, 30.0f, 0.1f, FALSE,
        "EPIC GAMER COMFORT ENGAGED",
        30, 30, 30,
        200, 30, 30,
        40, 40, 40
    },
    { /* 3: Bean Bag */
        "Bean Bag",
        "You will never stand up again.",
        8, GRADE_WARM, PARTICLE_COZY, TEXT_EFFECT_WAVE,
        LRG_EASING_EASE_OUT_ELASTIC, 50.0f, 0.0f, FALSE,
        "So... squishy... can't... move...",
        180, 120, 60,
        170, 110, 50,
        160, 100, 40
    },
    { /* 4: La-Z-Boy Recliner */
        "La-Z-Boy Recliner",
        "For people who have given up standing.",
        9, GRADE_WARM, PARTICLE_SPARKLE, TEXT_EFFECT_WAVE,
        LRG_EASING_EASE_OUT_BOUNCE, 35.0f, 0.0f, FALSE,
        "This must be what heaven feels like...",
        120, 50, 30,
        110, 45, 25,
        70, 30, 15
    },
    { /* 5: Antique Throne */
        "Antique Throne",
        "Impressive. Ornate. Spine-crushing.",
        4, GRADE_GOLDEN, PARTICLE_SPARKLE, TEXT_EFFECT_SHAKE,
        LRG_EASING_EASE_OUT_QUAD, 12.0f, 0.15f, FALSE,
        "Your spine does NOT approve",
        180, 140, 40,
        160, 120, 30,
        100, 70, 20
    },
    { /* 6: Hammock Chair */
        "Hammock Chair",
        "Gentle swaying. Existential peace.",
        9, GRADE_GREEN, PARTICLE_COZY, TEXT_EFFECT_WAVE,
        LRG_EASING_EASE_OUT_BOUNCE, 40.0f, 0.0f, FALSE,
        "The world melts away... sway... sway...",
        60, 120, 60,
        50, 110, 50,
        139, 90, 43
    },
    { /* 7: Broken Folding Chair */
        "Broken Folding Chair",
        "Held together with hope and duct tape.",
        1, GRADE_NONE, PARTICLE_SMOKE, TEXT_EFFECT_SHAKE,
        LRG_EASING_EASE_OUT_BOUNCE, 15.0f, 1.5f, TRUE,
        "OH NO",
        150, 150, 150,
        130, 130, 130,
        100, 100, 100
    },
};

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_bg = NULL;
static GrlColor *color_text = NULL;
static GrlColor *color_dim = NULL;
static GrlColor *color_accent = NULL;
static GrlColor *color_selected = NULL;
static GrlColor *color_panel = NULL;
static GrlColor *color_star_on = NULL;
static GrlColor *color_star_off = NULL;
static GrlColor *color_comfort_fill = NULL;
static GrlColor *color_comfort_bg = NULL;
static GrlColor *color_person = NULL;
static GrlColor *color_person_head = NULL;

static void
init_colors (void)
{
    color_bg = grl_color_new (25, 28, 35, 255);
    color_text = grl_color_new (230, 235, 245, 255);
    color_dim = grl_color_new (130, 135, 150, 255);
    color_accent = grl_color_new (100, 180, 255, 255);
    color_selected = grl_color_new (255, 200, 80, 255);
    color_panel = grl_color_new (40, 45, 55, 230);
    color_star_on = grl_color_new (255, 215, 0, 255);
    color_star_off = grl_color_new (80, 80, 90, 255);
    color_comfort_fill = grl_color_new (100, 220, 120, 255);
    color_comfort_bg = grl_color_new (50, 55, 65, 255);
    color_person = grl_color_new (80, 130, 200, 255);
    color_person_head = grl_color_new (255, 200, 170, 255);
}

static void
cleanup_colors (void)
{
    g_clear_pointer (&color_bg, grl_color_free);
    g_clear_pointer (&color_text, grl_color_free);
    g_clear_pointer (&color_dim, grl_color_free);
    g_clear_pointer (&color_accent, grl_color_free);
    g_clear_pointer (&color_selected, grl_color_free);
    g_clear_pointer (&color_panel, grl_color_free);
    g_clear_pointer (&color_star_on, grl_color_free);
    g_clear_pointer (&color_star_off, grl_color_free);
    g_clear_pointer (&color_comfort_fill, grl_color_free);
    g_clear_pointer (&color_comfort_bg, grl_color_free);
    g_clear_pointer (&color_person, grl_color_free);
    g_clear_pointer (&color_person_head, grl_color_free);
}

/* =============================================================================
 * PARTICLE SYSTEM
 * ========================================================================== */

typedef struct
{
    gfloat   x, y;
    gfloat   vx, vy;
    gfloat   life;
    gfloat   max_life;
    gfloat   size;
    guint8   r, g, b, a;
    gboolean active;
} Particle;

typedef struct
{
    Particle particles[MAX_PARTICLES];
    gint     active_count;
} ParticlePool;

static ParticlePool *g_particles = NULL;

static void
particle_pool_init (void)
{
    g_particles = g_new0 (ParticlePool, 1);
}

static void
particle_pool_cleanup (void)
{
    g_free (g_particles);
    g_particles = NULL;
}

static void
particle_pool_clear (void)
{
    gint i;

    for (i = 0; i < MAX_PARTICLES; i++)
    {
        g_particles->particles[i].active = FALSE;
    }
    g_particles->active_count = 0;
}

static void
emit_particle (gfloat x, gfloat y, gint type)
{
    gint i;
    Particle *p;
    gfloat angle, speed;

    for (i = 0; i < MAX_PARTICLES; i++)
    {
        p = &g_particles->particles[i];
        if (!p->active)
        {
            p->active = TRUE;
            p->x = x;
            p->y = y;
            g_particles->active_count++;

            switch (type)
            {
            case PARTICLE_DUST:
                p->vx = (g_random_double () - 0.5) * 80.0f;
                p->vy = -40.0f - g_random_double () * 40.0f;
                p->life = p->max_life = 0.8f + g_random_double () * 0.5f;
                p->size = 3.0f + g_random_double () * 3.0f;
                p->r = 160;
                p->g = 140;
                p->b = 100;
                p->a = 200;
                break;

            case PARTICLE_SPARKLE:
                angle = g_random_double () * 2.0f * G_PI;
                speed = 30.0f + g_random_double () * 60.0f;
                p->vx = cosf (angle) * speed;
                p->vy = sinf (angle) * speed;
                p->life = p->max_life = 1.5f + g_random_double () * 1.0f;
                p->size = 2.0f + g_random_double () * 2.0f;
                p->r = 255;
                p->g = 215 + g_random_int_range (0, 40);
                p->b = 50 + g_random_int_range (0, 100);
                p->a = 255;
                break;

            case PARTICLE_SMOKE:
                p->vx = (g_random_double () - 0.5) * 60.0f;
                p->vy = -50.0f - g_random_double () * 30.0f;
                p->life = p->max_life = 2.0f + g_random_double () * 1.5f;
                p->size = 6.0f + g_random_double () * 6.0f;
                p->r = 80;
                p->g = 80;
                p->b = 90;
                p->a = 180;
                break;

            case PARTICLE_RGB:
                angle = g_random_double () * 2.0f * G_PI;
                speed = 50.0f + g_random_double () * 80.0f;
                p->vx = cosf (angle) * speed;
                p->vy = sinf (angle) * speed;
                p->life = p->max_life = 1.0f + g_random_double () * 0.5f;
                p->size = 3.0f + g_random_double () * 2.0f;
                /* Random bright color */
                switch (g_random_int_range (0, 3))
                {
                case 0: p->r = 255; p->g = 50;  p->b = 50;  break;
                case 1: p->r = 50;  p->g = 255; p->b = 50;  break;
                case 2: p->r = 50;  p->g = 50;  p->b = 255; break;
                }
                p->a = 255;
                break;

            case PARTICLE_COZY:
                p->vx = (g_random_double () - 0.5) * 30.0f;
                p->vy = -15.0f - g_random_double () * 20.0f;
                p->life = p->max_life = 2.0f + g_random_double () * 1.0f;
                p->size = 2.0f + g_random_double () * 2.0f;
                p->r = 255;
                p->g = 180 + g_random_int_range (0, 50);
                p->b = 80 + g_random_int_range (0, 40);
                p->a = 180;
                break;
            }
            break;
        }
    }
}

static void
emit_burst (gfloat x, gfloat y, gint type, gint count)
{
    gint i;

    for (i = 0; i < count; i++)
    {
        emit_particle (x + (g_random_double () - 0.5) * 40.0f,
                       y + (g_random_double () - 0.5) * 20.0f,
                       type);
    }
}

static void
particle_pool_update (gfloat delta)
{
    gint i;
    Particle *p;
    gfloat life_ratio;

    for (i = 0; i < MAX_PARTICLES; i++)
    {
        p = &g_particles->particles[i];
        if (!p->active)
            continue;

        p->life -= delta;
        if (p->life <= 0.0f)
        {
            p->active = FALSE;
            g_particles->active_count--;
            continue;
        }

        /* Smoke expands */
        if (p->size > 5.0f && p->r < 100 && p->g < 100)
        {
            p->size += 4.0f * delta;
        }

        /* Gravity for dust and smoke */
        p->vy += 30.0f * delta;

        p->x += p->vx * delta;
        p->y += p->vy * delta;

        /* Fade out */
        life_ratio = p->life / p->max_life;
        p->a = (guint8)(life_ratio * 200.0f);
    }
}

static void
particle_pool_draw (void)
{
    gint i;
    Particle *p;
    GrlColor *color;

    for (i = 0; i < MAX_PARTICLES; i++)
    {
        p = &g_particles->particles[i];
        if (!p->active)
            continue;

        color = grl_color_new (p->r, p->g, p->b, p->a);
        grl_draw_circle ((gint)p->x, (gint)p->y, (gint)p->size, color);
        grl_color_free (color);
    }
}

/* =============================================================================
 * SCREEN SHAKE / POST-FX
 * ========================================================================== */

typedef struct
{
    gfloat shake_intensity;
    gfloat shake_timer;
    gint   color_grade;
} PostFXState;

static PostFXState *g_postfx = NULL;

static void
postfx_init (void)
{
    g_postfx = g_new0 (PostFXState, 1);
}

static void
postfx_cleanup (void)
{
    g_free (g_postfx);
    g_postfx = NULL;
}

static void
postfx_trigger_shake (gfloat intensity)
{
    g_postfx->shake_intensity = intensity;
    g_postfx->shake_timer = 0.4f;
}

static void
postfx_update (gfloat delta)
{
    if (g_postfx->shake_timer > 0.0f)
    {
        g_postfx->shake_timer -= delta;
        if (g_postfx->shake_timer <= 0.0f)
        {
            g_postfx->shake_intensity = 0.0f;
        }
    }
}

static void
postfx_get_offset (gint *out_x, gint *out_y)
{
    if (g_postfx->shake_intensity > 0.0f)
    {
        *out_x = (gint)((g_random_double () - 0.5) * g_postfx->shake_intensity * 20.0f);
        *out_y = (gint)((g_random_double () - 0.5) * g_postfx->shake_intensity * 20.0f);
    }
    else
    {
        *out_x = 0;
        *out_y = 0;
    }
}

static void
postfx_draw_color_overlay (void)
{
    GrlColor *overlay;

    switch (g_postfx->color_grade)
    {
    case GRADE_WARM:
        overlay = grl_color_new (255, 230, 200, 25);
        break;
    case GRADE_COOL:
        overlay = grl_color_new (180, 200, 255, 25);
        break;
    case GRADE_GOLDEN:
        overlay = grl_color_new (255, 215, 100, 20);
        break;
    case GRADE_GREEN:
        overlay = grl_color_new (180, 255, 180, 20);
        break;
    default:
        return;
    }

    grl_draw_rectangle (0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, overlay);
    grl_color_free (overlay);
}

/* =============================================================================
 * RICH TEXT EFFECTS
 * ========================================================================== */

typedef struct
{
    const gchar *text;
    gint         effect;
    gfloat       timer;
} RichTextRenderer;

static RichTextRenderer *g_richtext = NULL;

static void
richtext_init (void)
{
    g_richtext = g_new0 (RichTextRenderer, 1);
    g_richtext->text = "";
    g_richtext->effect = TEXT_EFFECT_NONE;
}

static void
richtext_cleanup (void)
{
    g_free (g_richtext);
    g_richtext = NULL;
}

static void
richtext_set (const gchar *text, gint effect)
{
    g_richtext->text = text;
    g_richtext->effect = effect;
    g_richtext->timer = 0.0f;
}

static void
richtext_update (gfloat delta)
{
    g_richtext->timer += delta;
}

static void
richtext_draw (gint x, gint y, gint font_size)
{
    const gchar *text;
    gint len, i;
    gint char_x, char_y;
    guint8 r, g, b;
    gfloat hue, f;
    gint h_i;
    guint8 q, t;
    GrlColor *color;
    gchar char_str[2];

    text = g_richtext->text;
    len = (gint)strlen (text);
    char_x = x;

    for (i = 0; i < len; i++)
    {
        char_y = y;
        r = 255;
        g = 255;
        b = 255;

        switch (g_richtext->effect)
        {
        case TEXT_EFFECT_SHAKE:
            char_x += (gint)((g_random_double () - 0.5) * 4.0);
            char_y += (gint)((g_random_double () - 0.5) * 4.0);
            break;

        case TEXT_EFFECT_WAVE:
            char_y += (gint)(sinf (g_richtext->timer * 5.0f + i * 0.3f) * 8.0f);
            break;

        case TEXT_EFFECT_RAINBOW:
            hue = fmodf (g_richtext->timer + i * 0.1f, 1.0f);
            h_i = (gint)(hue * 6.0f);
            f = hue * 6.0f - h_i;
            q = (guint8)(255 * (1.0f - f));
            t = (guint8)(255 * f);
            switch (h_i % 6)
            {
            case 0: r = 255; g = t;   b = 0;   break;
            case 1: r = q;   g = 255; b = 0;   break;
            case 2: r = 0;   g = 255; b = t;   break;
            case 3: r = 0;   g = q;   b = 255; break;
            case 4: r = t;   g = 0;   b = 255; break;
            case 5: r = 255; g = 0;   b = q;   break;
            }
            break;
        }

        color = grl_color_new (r, g, b, 255);
        char_str[0] = text[i];
        char_str[1] = '\0';
        grl_draw_text (char_str, char_x, char_y, font_size, color);
        char_x += grl_measure_text (char_str, font_size);
        grl_color_free (color);
    }
}

/* =============================================================================
 * SIT ANIMATION (easing-based)
 * ========================================================================== */

typedef struct
{
    gboolean      active;
    gfloat        elapsed;
    gfloat        duration;
    LrgEasingType easing;
    gfloat        amplitude;
    gfloat        sway_timer;     /* For hammock ongoing sway */
    gboolean      sway_active;
} SitAnimation;

static SitAnimation *g_sit_anim = NULL;

static void
sit_anim_init (void)
{
    g_sit_anim = g_new0 (SitAnimation, 1);
}

static void
sit_anim_cleanup (void)
{
    g_free (g_sit_anim);
    g_sit_anim = NULL;
}

static void
sit_anim_start (LrgEasingType easing, gfloat amplitude, gboolean sway)
{
    g_sit_anim->active = TRUE;
    g_sit_anim->elapsed = 0.0f;
    g_sit_anim->duration = SIT_BOUNCE_DURATION;
    g_sit_anim->easing = easing;
    g_sit_anim->amplitude = amplitude;
    g_sit_anim->sway_timer = 0.0f;
    g_sit_anim->sway_active = sway;
}

static void
sit_anim_update (gfloat delta)
{
    if (!g_sit_anim->active)
        return;

    g_sit_anim->elapsed += delta;
    g_sit_anim->sway_timer += delta;
}

static gfloat
sit_anim_get_offset (void)
{
    gfloat t;
    gfloat bounce_offset;
    gfloat sway_offset;

    if (!g_sit_anim->active)
        return 0.0f;

    /* Bounce phase */
    if (g_sit_anim->elapsed < g_sit_anim->duration)
    {
        t = g_sit_anim->elapsed / g_sit_anim->duration;
        /* Invert: start high, settle to 0 */
        bounce_offset = (1.0f - lrg_easing_apply (g_sit_anim->easing, t))
                        * g_sit_anim->amplitude;
        return -bounce_offset;
    }

    /* Ongoing sway for hammock */
    if (g_sit_anim->sway_active)
    {
        sway_offset = sinf (g_sit_anim->sway_timer * 1.5f) * 6.0f;
        return sway_offset;
    }

    return 0.0f;
}

/* =============================================================================
 * ACHIEVEMENT TRACKING
 * ========================================================================== */

static LrgAchievementManager *g_achieve_mgr = NULL;

/* Achievement toast notification */
typedef struct
{
    gchar  *text;
    gfloat  timer;
    gfloat  duration;
} AchievementToast;

static AchievementToast g_toast = { NULL, 0.0f, 3.0f };

static void
toast_show (const gchar *text)
{
    g_free (g_toast.text);
    g_toast.text = g_strdup (text);
    g_toast.timer = g_toast.duration;
}

static void
toast_update (gfloat delta)
{
    if (g_toast.timer > 0.0f)
    {
        g_toast.timer -= delta;
    }
}

static void
toast_draw (void)
{
    gint text_width;
    gint panel_x, panel_y, panel_w, panel_h;
    gfloat slide_t;
    GrlColor *toast_bg;
    GrlColor *toast_border;

    if (g_toast.timer <= 0.0f || g_toast.text == NULL)
        return;

    /* Slide in from top */
    slide_t = fminf (1.0f, (g_toast.duration - g_toast.timer) / 0.3f);
    if (g_toast.timer < 0.5f)
    {
        slide_t = g_toast.timer / 0.5f;
    }

    text_width = grl_measure_text (g_toast.text, 18);
    panel_w = text_width + 40;
    panel_h = 40;
    panel_x = (WINDOW_WIDTH - panel_w) / 2;
    panel_y = (gint)(lrg_easing_interpolate (LRG_EASING_EASE_OUT_BACK,
                                              -panel_h, 15.0f, slide_t));

    toast_bg = grl_color_new (50, 40, 70, 230);
    toast_border = grl_color_new (255, 215, 0, 200);

    grl_draw_rectangle (panel_x, panel_y, panel_w, panel_h, toast_bg);
    grl_draw_rectangle (panel_x, panel_y + panel_h - 3, panel_w, 3, toast_border);
    grl_draw_text (g_toast.text, panel_x + 20, panel_y + 10, 18, color_star_on);

    grl_color_free (toast_bg);
    grl_color_free (toast_border);
}

static void
on_achievement_unlocked (LrgAchievement *achievement,
                         gpointer        user_data)
{
    g_autofree gchar *msg = NULL;

    (void)user_data;

    msg = g_strdup_printf ("Achievement: %s",
                           lrg_achievement_get_name (achievement));
    toast_show (msg);
}

static void
register_achievement (const gchar *id,
                      const gchar *name,
                      const gchar *description,
                      gint64       target)
{
    LrgAchievement *a;

    a = lrg_achievement_new_with_progress (id, name, description, target);
    g_signal_connect (a, "unlocked",
                      G_CALLBACK (on_achievement_unlocked), NULL);
    /* Manager takes ownership (transfer full) */
    lrg_achievement_manager_register (g_achieve_mgr, a);
}

static void
achievements_init (void)
{
    g_achieve_mgr = lrg_achievement_manager_get_default ();

    register_achievement ("first-sit",
                          "First Impressions",
                          "Sat in your first chair", 1);
    register_achievement ("sat-in-five",
                          "Seasoned Sitter",
                          "Tested 5 different chairs", 5);
    register_achievement ("perfect-chair",
                          "The Golden Seat",
                          "Gave a chair a perfect 5-star rating", 1);
    register_achievement ("broke-chair",
                          "Structural Failure",
                          "Experienced catastrophic chair collapse", 1);
    register_achievement ("completionist",
                          "Professional Tester",
                          "Sat in all 8 chairs", 8);
}

/* =============================================================================
 * GLOBAL GAME DATA
 * ========================================================================== */

typedef struct
{
    gboolean chairs_tested[CHAIR_COUNT];
    gint     chair_ratings[CHAIR_COUNT];  /* 0 = unrated, 1-5 */
    gint     total_sits;
    gint     current_chair;
    gboolean chair_broken;                /* Set during sitting if collapsed */
} GameData;

static GameData *g_game_data = NULL;
static LrgGameStateManager *g_state_manager = NULL;
static LrgSettings *g_settings = NULL;
static GrlWindow *g_window = NULL;

static void
game_data_init (void)
{
    g_game_data = g_new0 (GameData, 1);
}

static void
game_data_cleanup (void)
{
    g_free (g_game_data);
    g_game_data = NULL;
}

static gint
game_data_count_tested (void)
{
    gint count, i;

    count = 0;
    for (i = 0; i < CHAIR_COUNT; i++)
    {
        if (g_game_data->chairs_tested[i])
            count++;
    }
    return count;
}

/* =============================================================================
 * CHAIR DRAWING HELPER
 * ========================================================================== */

static void
draw_chair (const ChairData *chair,
            gint             cx,
            gint             cy,
            gfloat           scale,
            gfloat           y_offset,
            gboolean         broken)
{
    gint seat_w, seat_h, back_w, back_h;
    gint leg_h;
    GrlColor *seat_color, *back_color, *leg_color;
    gint seat_x, seat_y, back_x, back_y;
    gint is_bean_bag;

    seat_w = (gint)(80 * scale);
    seat_h = (gint)(15 * scale);
    back_w = (gint)(12 * scale);
    back_h = (gint)(60 * scale);
    leg_h = (gint)(40 * scale);

    seat_color = grl_color_new (chair->seat_r, chair->seat_g, chair->seat_b, 255);
    back_color = grl_color_new (chair->back_r, chair->back_g, chair->back_b, 255);
    leg_color = grl_color_new (chair->leg_r, chair->leg_g, chair->leg_b, 255);

    /* Check if bean bag (index 3) for special rendering */
    is_bean_bag = (chair == &chairs[3]);

    if (is_bean_bag)
    {
        /* Bean bag: large rounded shape */
        grl_draw_circle (cx, cy + (gint)y_offset, (gint)(40 * scale), seat_color);
        grl_draw_circle (cx, cy + (gint)y_offset - (gint)(15 * scale),
                         (gint)(30 * scale), back_color);
    }
    else if (broken)
    {
        /* Broken: scattered pieces */
        grl_draw_rectangle (cx - seat_w / 2 - 20,
                            cy + (gint)y_offset + 10,
                            seat_w / 2, seat_h, seat_color);
        grl_draw_rectangle (cx + 10,
                            cy + (gint)y_offset + 20,
                            seat_w / 2, seat_h + 3, seat_color);
        grl_draw_rectangle (cx - 30,
                            cy + (gint)y_offset - 10,
                            back_w, back_h / 2, back_color);
        grl_draw_rectangle (cx + 25,
                            cy + (gint)y_offset + 30,
                            (gint)(6 * scale), leg_h / 2, leg_color);
    }
    else
    {
        /* Normal chair: seat, back, legs */
        seat_x = cx - seat_w / 2;
        seat_y = cy + (gint)y_offset;

        /* Legs (4 lines as thin rectangles) */
        grl_draw_rectangle (seat_x + 5,
                            seat_y + seat_h,
                            (gint)(5 * scale), leg_h, leg_color);
        grl_draw_rectangle (seat_x + seat_w - (gint)(10 * scale),
                            seat_y + seat_h,
                            (gint)(5 * scale), leg_h, leg_color);

        /* Seat */
        grl_draw_rectangle (seat_x, seat_y, seat_w, seat_h, seat_color);

        /* Back */
        back_x = seat_x - back_w + 5;
        back_y = seat_y - back_h + 5;
        grl_draw_rectangle (back_x, back_y, back_w, back_h, back_color);

        /* Extra back support piece */
        grl_draw_rectangle (back_x, seat_y - (gint)(5 * scale),
                            seat_w / 2, (gint)(5 * scale), back_color);
    }

    grl_color_free (seat_color);
    grl_color_free (back_color);
    grl_color_free (leg_color);
}

/* Draw a simple stick figure person sitting */
static void
draw_person_sitting (gint cx, gint cy, gfloat y_offset)
{
    gint body_x, body_y;

    body_x = cx;
    body_y = cy + (gint)y_offset;

    /* Head */
    grl_draw_circle (body_x + 5, body_y - 35, 12, color_person_head);

    /* Body (torso) */
    grl_draw_rectangle (body_x - 8, body_y - 22, 20, 25, color_person);

    /* Legs (sitting position: horizontal) */
    grl_draw_rectangle (body_x + 5, body_y + 3, 30, 10, color_person);
}

/* =============================================================================
 * COMFORT METER DRAWING
 * ========================================================================== */

static void
draw_comfort_meter (gfloat fill, gint comfort_max)
{
    gint bar_x, bar_y, bar_w, bar_h;
    gint fill_w;
    g_autofree gchar *label = NULL;

    bar_x = WINDOW_WIDTH / 2 - 150;
    bar_y = WINDOW_HEIGHT - 80;
    bar_w = 300;
    bar_h = 20;

    grl_draw_rectangle (bar_x, bar_y, bar_w, bar_h, color_comfort_bg);
    fill_w = (gint)(bar_w * fill);
    if (fill_w > 0)
    {
        grl_draw_rectangle (bar_x, bar_y, fill_w, bar_h, color_comfort_fill);
    }

    label = g_strdup_printf ("Comfort: %d/10", comfort_max);
    grl_draw_text (label, bar_x, bar_y - 22, 16, color_text);
}

/* =============================================================================
 * STAR RATING DRAWING
 * ========================================================================== */

static void
draw_stars (gint x, gint y, gint rating, gint size, gfloat pulse_timer)
{
    gint i;
    gint spacing;
    GrlColor *color;
    gint star_size;

    spacing = size + 10;

    for (i = 0; i < 5; i++)
    {
        if (i < rating)
        {
            /* Pulsing filled star */
            star_size = size + (gint)(sinf (pulse_timer * 3.0f + i * 0.5f) * 2.0f);
            color = color_star_on;
        }
        else
        {
            star_size = size;
            color = color_star_off;
        }

        /* Draw star as a circle (simplified) */
        grl_draw_circle (x + i * spacing + size / 2, y + size / 2,
                         star_size / 2, color);
    }
}

/* =============================================================================
 * GAME STATE: MAIN MENU
 * ========================================================================== */

#define CHAIR_TYPE_MAIN_MENU_STATE (chair_main_menu_state_get_type ())
G_DECLARE_FINAL_TYPE (ChairMainMenuState, chair_main_menu_state,
                      CHAIR, MAIN_MENU_STATE, LrgGameState)

struct _ChairMainMenuState
{
    LrgGameState parent_instance;
    gint         selected;
    gdouble      title_bob;
    gfloat       chair_spin;
    gboolean     quit_requested;
};

G_DEFINE_TYPE (ChairMainMenuState, chair_main_menu_state, LRG_TYPE_GAME_STATE)

/* Forward declaration for ChairSelectState type */
static GType chair_select_state_get_type (void);
#define CHAIR_TYPE_SELECT_STATE (chair_select_state_get_type ())

static const gchar *menu_labels[MENU_ITEM_COUNT] = {
    "Start Testing",
    "Quit"
};

static void
chair_main_menu_state_enter (LrgGameState *state)
{
    ChairMainMenuState *self = CHAIR_MAIN_MENU_STATE (state);

    self->selected = MENU_START;
    self->title_bob = 0.0;
    self->chair_spin = 0.0f;
    self->quit_requested = FALSE;
}

static void
chair_main_menu_state_update (LrgGameState *state,
                               gdouble       delta)
{
    ChairMainMenuState *self = CHAIR_MAIN_MENU_STATE (state);
    LrgGameState *new_state;

    self->title_bob += delta * 2.0;
    self->chair_spin += (gfloat)delta * 60.0f;

    /* Navigate */
    if (grl_input_is_key_pressed (GRL_KEY_UP) || grl_input_is_key_pressed (GRL_KEY_W))
        self->selected = (self->selected - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
    if (grl_input_is_key_pressed (GRL_KEY_DOWN) || grl_input_is_key_pressed (GRL_KEY_S))
        self->selected = (self->selected + 1) % MENU_ITEM_COUNT;

    /* Select */
    if (grl_input_is_key_pressed (GRL_KEY_ENTER) || grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        switch (self->selected)
        {
        case MENU_START:
            new_state = g_object_new (CHAIR_TYPE_SELECT_STATE, NULL);
            lrg_game_state_manager_push (g_state_manager, new_state);
            break;
        case MENU_QUIT:
            self->quit_requested = TRUE;
            break;
        }
    }
}

static void
chair_main_menu_state_draw (LrgGameState *state)
{
    ChairMainMenuState *self = CHAIR_MAIN_MENU_STATE (state);
    gint title_width, sub_width;
    gint title_y;
    gint i, item_y, item_width, item_x;
    gint tested;
    GrlColor *color;
    g_autofree gchar *stats_str = NULL;
    g_autofree gchar *sub2 = NULL;

    /* Title */
    title_width = grl_measure_text ("CHAIR SIMULATOR", 48);
    title_y = 120 + (gint)(sin (self->title_bob) * 5.0);
    grl_draw_text ("CHAIR SIMULATOR", (WINDOW_WIDTH - title_width) / 2,
                   title_y, 48, color_accent);

    /* Subtitle */
    sub_width = grl_measure_text ("Professional Chair Testing Services, LLC", 18);
    grl_draw_text ("Professional Chair Testing Services, LLC",
                   (WINDOW_WIDTH - sub_width) / 2, 180, 18, color_dim);

    /* Decorative chair (simple spinning indicator via bob offset) */
    {
        gfloat decor_offset;

        decor_offset = sinf (self->chair_spin * 0.02f) * 5.0f;
        draw_chair (&chairs[4], WINDOW_WIDTH / 2, 280, 0.8f, decor_offset, FALSE);
    }

    /* Menu items */
    for (i = 0; i < MENU_ITEM_COUNT; i++)
    {
        item_y = 400 + i * 50;
        item_width = grl_measure_text (menu_labels[i], 28);
        item_x = (WINDOW_WIDTH - item_width) / 2;

        color = (i == self->selected) ? color_selected : color_text;
        grl_draw_text (menu_labels[i], item_x, item_y, 28, color);

        if (i == self->selected)
        {
            grl_draw_text (">", item_x - 30, item_y, 28, color_selected);
            grl_draw_text ("<", item_x + item_width + 10, item_y, 28, color_selected);
        }
    }

    /* Stats */
    tested = game_data_count_tested ();
    stats_str = g_strdup_printf ("Chairs Tested: %d/%d | Total Sits: %d",
                                 tested, CHAIR_COUNT, g_game_data->total_sits);
    sub2 = g_strdup_printf ("%s", stats_str);
    {
        gint sw;

        sw = grl_measure_text (sub2, 14);
        grl_draw_text (sub2, (WINDOW_WIDTH - sw) / 2, 550, 14, color_dim);
    }

    /* Instructions */
    grl_draw_text ("UP/DOWN to navigate, ENTER to select",
                   250, WINDOW_HEIGHT - 40, 16, color_dim);
}

static void
chair_main_menu_state_class_init (ChairMainMenuStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = chair_main_menu_state_enter;
    state_class->update = chair_main_menu_state_update;
    state_class->draw = chair_main_menu_state_draw;
}

static void
chair_main_menu_state_init (ChairMainMenuState *self)
{
    self->selected = 0;
    self->title_bob = 0.0;
    self->chair_spin = 0.0f;
    self->quit_requested = FALSE;
}

/* =============================================================================
 * GAME STATE: CHAIR SELECT
 * ========================================================================== */

G_DECLARE_FINAL_TYPE (ChairSelectState, chair_select_state,
                      CHAIR, SELECT_STATE, LrgGameState)

struct _ChairSelectState
{
    LrgGameState parent_instance;
    gfloat       preview_bob;
};

G_DEFINE_TYPE (ChairSelectState, chair_select_state, LRG_TYPE_GAME_STATE)

/* Forward declaration for sitting state */
static GType chair_sitting_state_get_type (void);
#define CHAIR_TYPE_SITTING_STATE (chair_sitting_state_get_type ())

static void
chair_select_state_enter (LrgGameState *state)
{
    ChairSelectState *self = CHAIR_SELECT_STATE (state);

    self->preview_bob = 0.0f;
    particle_pool_clear ();
    g_postfx->color_grade = GRADE_NONE;
    g_postfx->shake_intensity = 0.0f;
}

static void
chair_select_state_update (LrgGameState *state,
                            gdouble       delta)
{
    ChairSelectState *self = CHAIR_SELECT_STATE (state);
    LrgGameState *new_state;

    self->preview_bob += (gfloat)delta * 1.5f;

    /* Browse chairs */
    if (grl_input_is_key_pressed (GRL_KEY_LEFT))
    {
        g_game_data->current_chair = (g_game_data->current_chair - 1 + CHAIR_COUNT)
                                     % CHAIR_COUNT;
    }
    if (grl_input_is_key_pressed (GRL_KEY_RIGHT))
    {
        g_game_data->current_chair = (g_game_data->current_chair + 1) % CHAIR_COUNT;
    }

    /* Sit in chair */
    if (grl_input_is_key_pressed (GRL_KEY_ENTER) || grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        new_state = g_object_new (CHAIR_TYPE_SITTING_STATE, NULL);
        lrg_game_state_manager_push (g_state_manager, new_state);
    }

    /* Back to menu */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        lrg_game_state_manager_pop (g_state_manager);
    }
}

static void
chair_select_state_draw (LrgGameState *state)
{
    ChairSelectState *self = CHAIR_SELECT_STATE (state);
    const ChairData *chair;
    gint name_w, desc_w;
    gfloat bob_offset;
    gint i, thumb_x, thumb_y;
    GrlColor *thumb_color;
    GrlColor *check_color;
    g_autofree gchar *comfort_str = NULL;
    g_autofree gchar *nav_str = NULL;
    gint rating;
    gint nav_w;

    chair = &chairs[g_game_data->current_chair];
    bob_offset = sinf (self->preview_bob) * 4.0f;

    /* Chair name */
    name_w = grl_measure_text (chair->name, 32);
    grl_draw_text (chair->name, (WINDOW_WIDTH - name_w) / 2, 60, 32, color_accent);

    /* Description */
    desc_w = grl_measure_text (chair->description, 18);
    grl_draw_text (chair->description, (WINDOW_WIDTH - desc_w) / 2, 100, 18, color_dim);

    /* Draw chair preview */
    draw_chair (chair, CHAIR_CENTER_X, CHAIR_CENTER_Y - 40, 1.5f, bob_offset, FALSE);

    /* Comfort rating */
    comfort_str = g_strdup_printf ("Comfort: %d/10", chair->comfort);
    {
        gint cw;

        cw = grl_measure_text (comfort_str, 20);
        grl_draw_text (comfort_str, (WINDOW_WIDTH - cw) / 2, 530, 20, color_text);
    }

    /* Show existing rating if any */
    rating = g_game_data->chair_ratings[g_game_data->current_chair];
    if (rating > 0)
    {
        draw_stars ((WINDOW_WIDTH - 5 * 30) / 2, 560, rating, 20, self->preview_bob);
    }
    else if (g_game_data->chairs_tested[g_game_data->current_chair])
    {
        gint tw;

        tw = grl_measure_text ("Tested - Not Rated", 16);
        grl_draw_text ("Tested - Not Rated", (WINDOW_WIDTH - tw) / 2, 565, 16, color_dim);
    }

    /* Navigation arrows */
    grl_draw_text ("<", 80, CHAIR_CENTER_Y - 50, 48, color_dim);
    grl_draw_text (">", WINDOW_WIDTH - 110, CHAIR_CENTER_Y - 50, 48, color_dim);

    /* Thumbnail bar at bottom */
    thumb_y = WINDOW_HEIGHT - 70;
    thumb_x = (WINDOW_WIDTH - CHAIR_COUNT * 50) / 2;

    for (i = 0; i < CHAIR_COUNT; i++)
    {
        if (i == g_game_data->current_chair)
        {
            thumb_color = color_accent;
        }
        else if (g_game_data->chairs_tested[i])
        {
            thumb_color = color_dim;
        }
        else
        {
            thumb_color = color_panel;
        }

        grl_draw_rectangle (thumb_x + i * 50, thumb_y, 40, 30, thumb_color);

        /* Checkmark for tested */
        if (g_game_data->chairs_tested[i])
        {
            check_color = grl_color_new (100, 255, 100, 255);
            grl_draw_text ("v", thumb_x + i * 50 + 14, thumb_y + 6, 16, check_color);
            grl_color_free (check_color);
        }
    }

    /* Navigation hint */
    nav_str = g_strdup_printf ("< %d/%d >", g_game_data->current_chair + 1, CHAIR_COUNT);
    nav_w = grl_measure_text (nav_str, 16);
    grl_draw_text (nav_str, (WINDOW_WIDTH - nav_w) / 2, WINDOW_HEIGHT - 35, 16, color_dim);

    /* Instructions */
    grl_draw_text ("LEFT/RIGHT: Browse | ENTER: Sit | ESC: Back",
                   230, WINDOW_HEIGHT - 15, 14, color_dim);
}

static void
chair_select_state_class_init (ChairSelectStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = chair_select_state_enter;
    state_class->update = chair_select_state_update;
    state_class->draw = chair_select_state_draw;
}

static void
chair_select_state_init (ChairSelectState *self)
{
    self->preview_bob = 0.0f;
}

/* =============================================================================
 * GAME STATE: SITTING
 * ========================================================================== */

G_DECLARE_FINAL_TYPE (ChairSittingState, chair_sitting_state,
                      CHAIR, SITTING_STATE, LrgGameState)

struct _ChairSittingState
{
    LrgGameState parent_instance;
    gfloat       elapsed;
    gfloat       comfort_fill;
    gfloat       particle_timer;
    gboolean     broke;
    gfloat       break_timer;
    gboolean     sitting_complete;
};

G_DEFINE_TYPE (ChairSittingState, chair_sitting_state, LRG_TYPE_GAME_STATE)

/* Forward declaration for rating state */
static GType chair_rating_state_get_type (void);
#define CHAIR_TYPE_RATING_STATE (chair_rating_state_get_type ())

static void
chair_sitting_state_enter (LrgGameState *state)
{
    ChairSittingState *self = CHAIR_SITTING_STATE (state);
    const ChairData *chair;
    gboolean sway;

    chair = &chairs[g_game_data->current_chair];

    /* Reset state */
    self->elapsed = 0.0f;
    self->comfort_fill = 0.0f;
    self->particle_timer = 0.0f;
    self->broke = FALSE;
    self->break_timer = 0.0f;
    self->sitting_complete = FALSE;

    /* Start sit animation */
    sway = (g_game_data->current_chair == 6);
    sit_anim_start (chair->easing, chair->bounce_amp, sway);

    /* Initial particle burst */
    emit_burst ((gfloat)CHAIR_CENTER_X, (gfloat)CHAIR_CENTER_Y,
                chair->particle_type, 20);

    /* Screen shake */
    if (chair->shake_intensity > 0.0f && !chair->breaks)
    {
        postfx_trigger_shake (chair->shake_intensity);
    }

    /* Color grade */
    g_postfx->color_grade = chair->color_grade;

    /* Rich text */
    richtext_set (chair->comfort_phrase, chair->text_effect);

    /* Achievement: track the sit */
    g_game_data->chairs_tested[g_game_data->current_chair] = TRUE;
    g_game_data->total_sits++;
    g_game_data->chair_broken = FALSE;

    lrg_achievement_manager_increment_progress (g_achieve_mgr, "first-sit", 1);
    lrg_achievement_manager_set_progress (g_achieve_mgr, "sat-in-five",
                                          (gint64)game_data_count_tested ());
    lrg_achievement_manager_set_progress (g_achieve_mgr, "completionist",
                                          (gint64)game_data_count_tested ());
}

static void
chair_sitting_state_update (LrgGameState *state,
                             gdouble       delta)
{
    ChairSittingState *self = CHAIR_SITTING_STATE (state);
    const ChairData *chair;
    gfloat fdt;
    gfloat emit_interval;
    LrgGameState *new_state;

    fdt = (gfloat)delta;
    chair = &chairs[g_game_data->current_chair];

    self->elapsed += fdt;

    /* Update subsystems */
    sit_anim_update (fdt);
    particle_pool_update (fdt);
    postfx_update (fdt);
    richtext_update (fdt);

    /* Fill comfort meter */
    if (!self->broke)
    {
        self->comfort_fill += fdt / (SIT_HOLD_DURATION * 0.8f);
        if (self->comfort_fill > 1.0f)
            self->comfort_fill = 1.0f;
    }

    /* Continuous particle emission */
    self->particle_timer += fdt;
    emit_interval = 0.15f;
    if (self->broke)
        emit_interval = 0.05f;

    while (self->particle_timer >= emit_interval)
    {
        self->particle_timer -= emit_interval;
        emit_particle ((gfloat)CHAIR_CENTER_X + (g_random_double () - 0.5) * 60.0f,
                       (gfloat)CHAIR_CENTER_Y + (g_random_double () - 0.5) * 20.0f,
                       self->broke ? PARTICLE_SMOKE : chair->particle_type);
    }

    /* Broken chair sequence */
    if (chair->breaks && !self->broke && self->elapsed >= BREAK_DELAY)
    {
        self->broke = TRUE;
        self->break_timer = 0.0f;
        g_game_data->chair_broken = TRUE;

        /* Massive effects */
        postfx_trigger_shake (1.5f);
        emit_burst ((gfloat)CHAIR_CENTER_X, (gfloat)CHAIR_CENTER_Y, PARTICLE_SMOKE, 50);
        richtext_set ("OH NO", TEXT_EFFECT_SHAKE);

        /* Achievement */
        lrg_achievement_manager_increment_progress (g_achieve_mgr, "broke-chair", 1);
    }

    if (self->broke)
    {
        self->break_timer += fdt;
    }

    /* Auto-advance to rating */
    if (!self->sitting_complete)
    {
        if (self->broke && self->break_timer >= 2.0f)
        {
            self->sitting_complete = TRUE;
        }
        else if (!chair->breaks && self->elapsed >= SIT_HOLD_DURATION)
        {
            self->sitting_complete = TRUE;
        }
    }

    if (self->sitting_complete)
    {
        new_state = g_object_new (CHAIR_TYPE_RATING_STATE, NULL);
        lrg_game_state_manager_replace (g_state_manager, new_state);
    }
}

static void
chair_sitting_state_draw (LrgGameState *state)
{
    ChairSittingState *self = CHAIR_SITTING_STATE (state);
    const ChairData *chair;
    gint shake_x, shake_y;
    gfloat y_offset;
    gint name_w;
    gint fail_w;
    g_autofree gchar *sit_status = NULL;

    chair = &chairs[g_game_data->current_chair];

    /* Screen shake offset */
    postfx_get_offset (&shake_x, &shake_y);
    y_offset = sit_anim_get_offset ();

    /* Chair name at top */
    name_w = grl_measure_text (chair->name, 24);
    grl_draw_text (chair->name,
                   (WINDOW_WIDTH - name_w) / 2 + shake_x,
                   40 + shake_y, 24, color_accent);

    /* Draw the chair */
    draw_chair (chair,
                CHAIR_CENTER_X + shake_x,
                CHAIR_CENTER_Y + shake_y,
                1.5f, y_offset, self->broke);

    /* Draw person sitting (only if not broken) */
    if (!self->broke)
    {
        draw_person_sitting (CHAIR_CENTER_X + shake_x,
                             CHAIR_CENTER_Y + shake_y - 15,
                             y_offset);
    }
    else
    {
        /* Person on the ground */
        draw_person_sitting (CHAIR_CENTER_X + shake_x + 20,
                             CHAIR_CENTER_Y + shake_y + 30,
                             0.0f);
    }

    /* Particles */
    particle_pool_draw ();

    /* Rich text comfort phrase */
    {
        gint text_total_w;

        text_total_w = grl_measure_text (chair->comfort_phrase, 22);
        richtext_draw ((WINDOW_WIDTH - text_total_w) / 2 + shake_x,
                       200 + shake_y, 22);
    }

    /* Comfort meter */
    draw_comfort_meter (self->comfort_fill, chair->comfort);

    /* Structural failure text */
    if (self->broke)
    {
        fail_w = grl_measure_text ("STRUCTURAL FAILURE", 36);
        grl_draw_text ("STRUCTURAL FAILURE",
                       (WINDOW_WIDTH - fail_w) / 2 + shake_x,
                       300 + shake_y, 36, color_selected);
    }

    /* Status text */
    if (!self->broke)
    {
        sit_status = g_strdup_printf ("Sitting... %.0f%%",
                                      self->comfort_fill * 100.0f);
    }
    else
    {
        sit_status = g_strdup ("Chair has collapsed. Recalculating comfort...");
    }
    grl_draw_text (sit_status, 20, WINDOW_HEIGHT - 30, 14, color_dim);

    /* Color grade overlay */
    postfx_draw_color_overlay ();
}

static void
chair_sitting_state_class_init (ChairSittingStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = chair_sitting_state_enter;
    state_class->update = chair_sitting_state_update;
    state_class->draw = chair_sitting_state_draw;
}

static void
chair_sitting_state_init (ChairSittingState *self)
{
    self->elapsed = 0.0f;
    self->comfort_fill = 0.0f;
    self->particle_timer = 0.0f;
    self->broke = FALSE;
    self->break_timer = 0.0f;
    self->sitting_complete = FALSE;
}

/* =============================================================================
 * GAME STATE: RATING
 * ========================================================================== */

G_DECLARE_FINAL_TYPE (ChairRatingState, chair_rating_state,
                      CHAIR, RATING_STATE, LrgGameState)

struct _ChairRatingState
{
    LrgGameState parent_instance;
    gint         selected_rating;
    gfloat       star_pulse;
    gboolean     submitted;
    gfloat       exit_timer;
};

G_DEFINE_TYPE (ChairRatingState, chair_rating_state, LRG_TYPE_GAME_STATE)

static void
chair_rating_state_enter (LrgGameState *state)
{
    ChairRatingState *self = CHAIR_RATING_STATE (state);

    self->selected_rating = 0;
    self->star_pulse = 0.0f;
    self->submitted = FALSE;
    self->exit_timer = 0.0f;

    /* Clear effects from sitting */
    particle_pool_clear ();
    g_postfx->color_grade = GRADE_NONE;
    g_postfx->shake_intensity = 0.0f;
}

static void
chair_rating_state_update (LrgGameState *state,
                            gdouble       delta)
{
    ChairRatingState *self = CHAIR_RATING_STATE (state);

    self->star_pulse += (gfloat)delta;

    if (self->submitted)
    {
        self->exit_timer -= (gfloat)delta;
        if (self->exit_timer <= 0.0f)
        {
            lrg_game_state_manager_pop (g_state_manager);
        }
        return;
    }

    /* Number keys 1-5 */
    if (grl_input_is_key_pressed (GRL_KEY_ONE))   self->selected_rating = 1;
    if (grl_input_is_key_pressed (GRL_KEY_TWO))   self->selected_rating = 2;
    if (grl_input_is_key_pressed (GRL_KEY_THREE)) self->selected_rating = 3;
    if (grl_input_is_key_pressed (GRL_KEY_FOUR))  self->selected_rating = 4;
    if (grl_input_is_key_pressed (GRL_KEY_FIVE))  self->selected_rating = 5;

    /* LEFT/RIGHT to adjust */
    if (grl_input_is_key_pressed (GRL_KEY_LEFT) && self->selected_rating > 1)
        self->selected_rating--;
    if (grl_input_is_key_pressed (GRL_KEY_RIGHT) && self->selected_rating < 5)
        self->selected_rating++;

    /* Submit */
    if ((grl_input_is_key_pressed (GRL_KEY_ENTER) ||
         grl_input_is_key_pressed (GRL_KEY_SPACE)) &&
        self->selected_rating > 0)
    {
        g_game_data->chair_ratings[g_game_data->current_chair] = self->selected_rating;
        self->submitted = TRUE;
        self->exit_timer = 1.5f;

        /* Achievement: perfect rating */
        if (self->selected_rating == 5)
        {
            lrg_achievement_manager_increment_progress (g_achieve_mgr,
                                                        "perfect-chair", 1);
        }
    }

    /* Skip rating */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        lrg_game_state_manager_pop (g_state_manager);
    }
}

static void
chair_rating_state_draw (LrgGameState *state)
{
    ChairRatingState *self = CHAIR_RATING_STATE (state);
    const ChairData *chair;
    gint title_w, name_w;
    gint stars_x, stars_y;
    gint tested;
    gfloat avg;
    gint rated_count, sum, i;
    g_autofree gchar *comfort_str = NULL;
    g_autofree gchar *broke_str = NULL;
    g_autofree gchar *thanks = NULL;
    g_autofree gchar *stats = NULL;
    g_autofree gchar *avg_str = NULL;

    chair = &chairs[g_game_data->current_chair];

    /* Title */
    title_w = grl_measure_text ("Rate This Chair", 36);
    grl_draw_text ("Rate This Chair",
                   (WINDOW_WIDTH - title_w) / 2, 80, 36, color_accent);

    /* Chair name */
    name_w = grl_measure_text (chair->name, 24);
    grl_draw_text (chair->name, (WINDOW_WIDTH - name_w) / 2, 140, 24, color_text);

    /* Small chair preview */
    draw_chair (chair, WINDOW_WIDTH / 2, 240, 0.8f, 0.0f,
                g_game_data->chair_broken);

    /* Stars */
    stars_x = (WINDOW_WIDTH - 5 * 40) / 2;
    stars_y = 340;

    if (self->selected_rating > 0)
    {
        draw_stars (stars_x, stars_y, self->selected_rating, 30, self->star_pulse);
    }
    else
    {
        draw_stars (stars_x, stars_y, 0, 30, 0.0f);
        {
            gint hint_w;

            hint_w = grl_measure_text ("Press 1-5 to rate", 16);
            grl_draw_text ("Press 1-5 to rate",
                           (WINDOW_WIDTH - hint_w) / 2, stars_y + 40, 16, color_dim);
        }
    }

    /* Chair info */
    comfort_str = g_strdup_printf ("Comfort Level: %d/10", chair->comfort);
    grl_draw_text (comfort_str, WINDOW_WIDTH / 2 - 80, 400, 16, color_text);

    if (g_game_data->chair_broken)
    {
        broke_str = g_strdup ("Status: COLLAPSED");
        grl_draw_text (broke_str, WINDOW_WIDTH / 2 - 80, 425, 16, color_selected);
    }

    /* Submitted message */
    if (self->submitted)
    {
        thanks = g_strdup ("Thank you for your professional assessment.");
        {
            gint tw;

            tw = grl_measure_text (thanks, 18);
            grl_draw_text (thanks, (WINDOW_WIDTH - tw) / 2, 480, 18, color_star_on);
        }
    }

    /* Stats at bottom */
    tested = game_data_count_tested ();
    rated_count = 0;
    sum = 0;
    for (i = 0; i < CHAIR_COUNT; i++)
    {
        if (g_game_data->chair_ratings[i] > 0)
        {
            rated_count++;
            sum += g_game_data->chair_ratings[i];
        }
    }
    avg = (rated_count > 0) ? (gfloat)sum / (gfloat)rated_count : 0.0f;

    stats = g_strdup_printf ("Chairs Tested: %d/%d", tested, CHAIR_COUNT);
    grl_draw_text (stats, 20, WINDOW_HEIGHT - 60, 14, color_dim);

    if (rated_count > 0)
    {
        avg_str = g_strdup_printf ("Avg Rating: %.1f stars (%d rated)",
                                   avg, rated_count);
        grl_draw_text (avg_str, 20, WINDOW_HEIGHT - 40, 14, color_dim);
    }

    /* Instructions */
    if (!self->submitted)
    {
        grl_draw_text ("1-5: Rate | ENTER: Submit | ESC: Skip",
                       270, WINDOW_HEIGHT - 20, 14, color_dim);
    }
}

static void
chair_rating_state_class_init (ChairRatingStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = chair_rating_state_enter;
    state_class->update = chair_rating_state_update;
    state_class->draw = chair_rating_state_draw;
}

static void
chair_rating_state_init (ChairRatingState *self)
{
    self->selected_rating = 0;
    self->star_pulse = 0.0f;
    self->submitted = FALSE;
    self->exit_timer = 0.0f;
}

/* =============================================================================
 * MAIN FUNCTION
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr (GError) error = NULL;
    LrgGameState *menu_state;
    LrgGameState *current;
    gfloat delta;
    g_autoptr (GrlColor) bg = NULL;

    (void)argc;
    (void)argv;

    /* Settings */
    g_settings = lrg_settings_new ();
    if (!lrg_settings_load_default_path (g_settings, "chair-simulator", &error))
    {
        g_message ("Using default settings: %s",
                   error ? error->message : "first run");
        g_clear_error (&error);
    }

    /* Window */
    g_window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                                "Chair Simulator - Professional Chair Testing");
    grl_window_set_target_fps (g_window, 60);

    /* Initialize subsystems */
    init_colors ();
    particle_pool_init ();
    postfx_init ();
    richtext_init ();
    sit_anim_init ();
    game_data_init ();
    achievements_init ();

    bg = grl_color_new (25, 28, 35, 255);

    /* State manager - push takes ownership (transfer full) */
    g_state_manager = lrg_game_state_manager_new ();
    menu_state = g_object_new (CHAIR_TYPE_MAIN_MENU_STATE, NULL);
    lrg_game_state_manager_push (g_state_manager, menu_state);

    /* Main loop */
    while (!grl_window_should_close (g_window))
    {
        delta = grl_window_get_frame_time (g_window);

        /* Check quit from main menu */
        current = lrg_game_state_manager_get_current (g_state_manager);
        if (current != NULL && CHAIR_IS_MAIN_MENU_STATE (current))
        {
            ChairMainMenuState *menu = CHAIR_MAIN_MENU_STATE (current);
            if (menu->quit_requested)
                break;
        }

        /* Global updates */
        toast_update (delta);

        /* State update + draw */
        lrg_game_state_manager_update (g_state_manager, (gdouble)delta);

        grl_window_begin_drawing (g_window);
        grl_draw_clear_background (bg);
        lrg_game_state_manager_draw (g_state_manager);
        toast_draw ();
        grl_draw_fps (WINDOW_WIDTH - 80, 10);
        grl_window_end_drawing (g_window);
    }

    /* Cleanup */
    lrg_game_state_manager_clear (g_state_manager);
    g_clear_object (&g_state_manager);

    if (!lrg_settings_save_default_path (g_settings, "chair-simulator", &error))
    {
        g_warning ("Failed to save settings: %s", error->message);
    }
    g_clear_object (&g_settings);

    g_free (g_toast.text);
    game_data_cleanup ();
    sit_anim_cleanup ();
    richtext_cleanup ();
    postfx_cleanup ();
    particle_pool_cleanup ();
    cleanup_colors ();
    g_clear_object (&g_window);

    return 0;
}
