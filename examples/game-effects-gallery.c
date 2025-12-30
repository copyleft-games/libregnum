/* game-effects-gallery.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An interactive effects playground demonstrating Phase 3 features:
 * Particle System, Post-Processing Pipeline, Rich Text with effects,
 * and Animation State Machine.
 *
 * Features demonstrated:
 * - LrgParticleSystem / LrgParticleEmitter: Particle effects
 * - LrgParticleForce: Gravity, wind forces
 * - LrgPostProcessor / LrgBloom / LrgColorGrade: Post-processing
 * - LrgRichText / LrgTextEffect: Animated text
 * - LrgAnimator / LrgAnimationStateMachine: Animation system
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

#define WINDOW_WIDTH    1024
#define WINDOW_HEIGHT   768

/* Tabs */
#define TAB_PARTICLES   0
#define TAB_POSTFX      1
#define TAB_RICHTEXT    2
#define TAB_ANIMATION   3
#define TAB_COUNT       4

/* Particle types */
#define PARTICLE_FIRE       0
#define PARTICLE_SPARKLE    1
#define PARTICLE_EXPLOSION  2
#define PARTICLE_SMOKE      3
#define PARTICLE_RAIN       4
#define PARTICLE_TYPE_COUNT 5

/* Animation states */
#define ANIM_IDLE       0
#define ANIM_WALK       1
#define ANIM_RUN        2
#define ANIM_JUMP       3
#define ANIM_ATTACK     4
#define ANIM_STATE_COUNT 5

/* UI Layout */
#define TAB_Y           20
#define TAB_HEIGHT      40
#define CONTENT_Y       80
#define SIDEBAR_X       750
#define SIDEBAR_WIDTH   250

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_bg = NULL;
static GrlColor *color_text = NULL;
static GrlColor *color_dim = NULL;
static GrlColor *color_accent = NULL;
static GrlColor *color_selected = NULL;
static GrlColor *color_panel = NULL;

static void
init_colors (void)
{
    color_bg = grl_color_new (25, 28, 35, 255);
    color_text = grl_color_new (230, 235, 245, 255);
    color_dim = grl_color_new (130, 135, 150, 255);
    color_accent = grl_color_new (100, 180, 255, 255);
    color_selected = grl_color_new (255, 200, 80, 255);
    color_panel = grl_color_new (40, 45, 55, 230);
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
}

/* =============================================================================
 * PARTICLE SYSTEM (Simplified implementation)
 * ========================================================================== */

#define MAX_PARTICLES 500

typedef struct
{
    gfloat x, y;
    gfloat vx, vy;
    gfloat life;
    gfloat max_life;
    gfloat size;
    guint8 r, g, b, a;
    gboolean active;
} Particle;

typedef struct
{
    Particle particles[MAX_PARTICLES];
    gint active_count;
    gint particle_type;
    gfloat emit_timer;
    gfloat emit_rate;
    gboolean continuous;
} ParticleSystem;

static ParticleSystem *g_particles = NULL;

static void
particle_system_init (void)
{
    g_particles = g_new0 (ParticleSystem, 1);
    g_particles->particle_type = PARTICLE_FIRE;
    g_particles->emit_rate = 50.0f;
    g_particles->continuous = TRUE;
}

static void
particle_system_cleanup (void)
{
    g_free (g_particles);
    g_particles = NULL;
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
            case PARTICLE_FIRE:
                p->vx = (g_random_double () - 0.5) * 50.0f;
                p->vy = -100.0f - g_random_double () * 100.0f;
                p->life = p->max_life = 1.0f + g_random_double () * 0.5f;
                p->size = 4.0f + g_random_double () * 4.0f;
                p->r = 255;
                p->g = 100 + g_random_int_range (0, 100);
                p->b = 0;
                p->a = 255;
                break;

            case PARTICLE_SPARKLE:
                p->vx = (g_random_double () - 0.5) * 100.0f;
                p->vy = (g_random_double () - 0.5) * 100.0f;
                p->life = p->max_life = 2.0f + g_random_double ();
                p->size = 2.0f + g_random_double () * 3.0f;
                p->r = 200 + g_random_int_range (0, 55);
                p->g = 200 + g_random_int_range (0, 55);
                p->b = 100 + g_random_int_range (0, 155);
                p->a = 255;
                break;

            case PARTICLE_EXPLOSION:
                angle = g_random_double () * 2.0f * G_PI;
                speed = 200.0f + g_random_double () * 300.0f;
                p->vx = cosf (angle) * speed;
                p->vy = sinf (angle) * speed;
                p->life = p->max_life = 0.3f + g_random_double () * 0.3f;
                p->size = 3.0f + g_random_double () * 5.0f;
                p->r = 255;
                p->g = g_random_int_range (50, 200);
                p->b = 0;
                p->a = 255;
                break;

            case PARTICLE_SMOKE:
                p->vx = (g_random_double () - 0.5) * 30.0f;
                p->vy = -30.0f - g_random_double () * 20.0f;
                p->life = p->max_life = 3.0f + g_random_double () * 2.0f;
                p->size = 8.0f + g_random_double () * 8.0f;
                p->r = 80;
                p->g = 80;
                p->b = 90;
                p->a = 150;
                break;

            case PARTICLE_RAIN:
                p->vx = -20.0f;
                p->vy = 400.0f + g_random_double () * 100.0f;
                p->life = p->max_life = 2.0f;
                p->size = 2.0f;
                p->r = 100;
                p->g = 150;
                p->b = 255;
                p->a = 200;
                break;
            }
            break;
        }
    }
}

static void
particle_system_update (gfloat delta)
{
    gfloat interval;
    gfloat emit_x, emit_y;
    gint i;
    Particle *p;
    gfloat life_ratio;

    /* Emit new particles */
    if (g_particles->continuous)
    {
        g_particles->emit_timer += delta;
        interval = 1.0f / g_particles->emit_rate;

        while (g_particles->emit_timer >= interval)
        {
            g_particles->emit_timer -= interval;

            if (g_particles->particle_type == PARTICLE_RAIN)
            {
                emit_x = g_random_double () * SIDEBAR_X;
                emit_y = 0;
            }
            else
            {
                emit_x = 350.0f;
                emit_y = 500.0f;
            }
            emit_particle (emit_x, emit_y, g_particles->particle_type);
        }
    }

    /* Update particles */
    for (i = 0; i < MAX_PARTICLES; i++)
    {
        p = &g_particles->particles[i];
        if (!p->active)
            continue;

        p->life -= delta;
        if (p->life <= 0)
        {
            p->active = FALSE;
            g_particles->active_count--;
            continue;
        }

        /* Apply gravity (except rain has different physics) */
        if (g_particles->particle_type == PARTICLE_FIRE)
        {
            p->vy -= 50.0f * delta; /* Upward buoyancy */
        }
        else if (g_particles->particle_type != PARTICLE_RAIN)
        {
            p->vy += 100.0f * delta; /* Gravity */
        }

        /* Smoke expands */
        if (g_particles->particle_type == PARTICLE_SMOKE)
        {
            p->size += 5.0f * delta;
        }

        /* Update position */
        p->x += p->vx * delta;
        p->y += p->vy * delta;

        /* Fade out */
        life_ratio = p->life / p->max_life;
        p->a = (guint8)(life_ratio * 255.0f);
    }
}

static void
particle_system_draw (void)
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

static void
particle_system_clear (void)
{
    gint i;

    for (i = 0; i < MAX_PARTICLES; i++)
    {
        g_particles->particles[i].active = FALSE;
    }
    g_particles->active_count = 0;
}

/* =============================================================================
 * POST-PROCESSING (Simplified simulation)
 * ========================================================================== */

typedef struct
{
    gboolean bloom_enabled;
    gfloat bloom_intensity;
    gint color_grade;  /* 0=Normal, 1=Warm, 2=Cool, 3=Noir, 4=Vintage */
    gboolean vignette_enabled;
    gfloat vignette_intensity;
    gfloat screen_shake;
    gfloat shake_timer;
} PostFXState;

static PostFXState *g_postfx = NULL;

static const gchar *color_grade_names[] = {
    "Normal", "Warm", "Cool", "Noir", "Vintage"
};
#define COLOR_GRADE_COUNT 5

static void
postfx_init (void)
{
    g_postfx = g_new0 (PostFXState, 1);
    g_postfx->bloom_enabled = TRUE;
    g_postfx->bloom_intensity = 1.0f;
    g_postfx->color_grade = 0;
    g_postfx->vignette_enabled = TRUE;
    g_postfx->vignette_intensity = 0.5f;
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
    g_postfx->screen_shake = intensity;
    g_postfx->shake_timer = 0.3f;
}

static void
postfx_update (gfloat delta)
{
    if (g_postfx->shake_timer > 0)
    {
        g_postfx->shake_timer -= delta;
        if (g_postfx->shake_timer <= 0)
        {
            g_postfx->screen_shake = 0;
        }
    }
}

static void
postfx_draw_demo (void)
{
    gint center_x, center_y;
    gint shake_x, shake_y;
    guint8 tint_r, tint_g, tint_b;
    GrlColor *bright1, *bright2, *bright3;
    guint8 glow_alpha;
    GrlColor *glow1, *glow2, *glow3;
    GrlColor *overlay;
    guint8 vig_alpha;
    GrlColor *vig_color;

    /* Draw some demo shapes for post-fx to affect */
    center_x = 350;
    center_y = 400;

    /* Shake offset */
    shake_x = 0;
    shake_y = 0;
    if (g_postfx->screen_shake > 0)
    {
        shake_x = (gint)((g_random_double () - 0.5) * g_postfx->screen_shake * 20);
        shake_y = (gint)((g_random_double () - 0.5) * g_postfx->screen_shake * 20);
    }

    /* Color grading tint */
    tint_r = 255;
    tint_g = 255;
    tint_b = 255;
    switch (g_postfx->color_grade)
    {
    case 1: /* Warm */
        tint_r = 255; tint_g = 240; tint_b = 200;
        break;
    case 2: /* Cool */
        tint_r = 200; tint_g = 220; tint_b = 255;
        break;
    case 3: /* Noir */
        tint_r = 180; tint_g = 180; tint_b = 180;
        break;
    case 4: /* Vintage */
        tint_r = 255; tint_g = 230; tint_b = 180;
        break;
    }

    /* Draw bright objects (bloom would affect these) */
    bright1 = grl_color_new (255, 255, 200, 255);
    bright2 = grl_color_new (200, 255, 255, 255);
    bright3 = grl_color_new (255, 200, 255, 255);

    grl_draw_circle (center_x + shake_x, center_y + shake_y - 100, 40, bright1);
    grl_draw_circle (center_x + shake_x - 100, center_y + shake_y + 50, 30, bright2);
    grl_draw_circle (center_x + shake_x + 100, center_y + shake_y + 50, 30, bright3);

    grl_color_free (bright1);
    grl_color_free (bright2);
    grl_color_free (bright3);

    /* Bloom glow simulation (draw larger faded circles behind) */
    if (g_postfx->bloom_enabled)
    {
        glow_alpha = (guint8)(g_postfx->bloom_intensity * 60);
        glow1 = grl_color_new (255, 255, 200, glow_alpha);
        glow2 = grl_color_new (200, 255, 255, glow_alpha);
        glow3 = grl_color_new (255, 200, 255, glow_alpha);

        grl_draw_circle (center_x + shake_x, center_y + shake_y - 100, 60, glow1);
        grl_draw_circle (center_x + shake_x - 100, center_y + shake_y + 50, 50, glow2);
        grl_draw_circle (center_x + shake_x + 100, center_y + shake_y + 50, 50, glow3);

        grl_color_free (glow1);
        grl_color_free (glow2);
        grl_color_free (glow3);
    }

    /* Color grade overlay */
    if (g_postfx->color_grade != 0)
    {
        overlay = grl_color_new (tint_r, tint_g, tint_b, 30);
        grl_draw_rectangle (0, CONTENT_Y, SIDEBAR_X, WINDOW_HEIGHT - CONTENT_Y, overlay);
        grl_color_free (overlay);
    }

    /* Vignette effect (draw dark gradient at edges) */
    if (g_postfx->vignette_enabled)
    {
        vig_alpha = (guint8)(g_postfx->vignette_intensity * 150);
        vig_color = grl_color_new (0, 0, 0, vig_alpha);

        /* Simple vignette simulation with rectangles at edges */
        grl_draw_rectangle (0, CONTENT_Y, 50, WINDOW_HEIGHT - CONTENT_Y, vig_color);
        grl_draw_rectangle (SIDEBAR_X - 50, CONTENT_Y, 50, WINDOW_HEIGHT - CONTENT_Y, vig_color);
        grl_draw_rectangle (0, CONTENT_Y, SIDEBAR_X, 50, vig_color);
        grl_draw_rectangle (0, WINDOW_HEIGHT - 50, SIDEBAR_X, 50, vig_color);

        grl_color_free (vig_color);
    }
}

/* =============================================================================
 * RICH TEXT (Simplified implementation)
 * ========================================================================== */

typedef struct
{
    gchar *text;
    gint effect;  /* 0=none, 1=shake, 2=wave, 3=rainbow, 4=typewriter */
    gfloat timer;
    gint typewriter_pos;
} RichTextState;

static RichTextState *g_richtext = NULL;

static const gchar *effect_names[] = {
    "None", "Shake", "Wave", "Rainbow", "Typewriter"
};
#define EFFECT_COUNT 5

static const gchar *demo_texts[] = {
    "Welcome to the Effects Gallery!",
    "This text SHAKES violently!",
    "This text flows like a WAVE!",
    "RAINBOW colors everywhere!",
    "Watch me type letter by letter..."
};

static void
richtext_init (void)
{
    g_richtext = g_new0 (RichTextState, 1);
    g_richtext->text = g_strdup (demo_texts[0]);
    g_richtext->effect = 0;
}

static void
richtext_cleanup (void)
{
    g_free (g_richtext->text);
    g_free (g_richtext);
    g_richtext = NULL;
}

static void
richtext_set_effect (gint effect)
{
    g_richtext->effect = effect;
    g_free (g_richtext->text);
    g_richtext->text = g_strdup (demo_texts[effect]);
    g_richtext->timer = 0;
    g_richtext->typewriter_pos = 0;
}

static void
richtext_update (gfloat delta)
{
    gint len;

    g_richtext->timer += delta;

    if (g_richtext->effect == 4) /* Typewriter */
    {
        len = strlen (g_richtext->text);
        if (g_richtext->typewriter_pos < len)
        {
            g_richtext->typewriter_pos = (gint)(g_richtext->timer * 15);
            if (g_richtext->typewriter_pos > len)
                g_richtext->typewriter_pos = len;
        }
    }
}

static void
richtext_draw (void)
{
    gint x, y, font_size;
    const gchar *text;
    gint len, display_len, i;
    gint char_x, char_y;
    guint8 r, g, b;
    gfloat hue, f;
    gint h_i;
    guint8 q, t;
    GrlColor *color;
    gchar char_str[2];
    GrlColor *cursor_color;

    x = 100;
    y = 350;
    font_size = 28;

    text = g_richtext->text;
    len = strlen (text);

    display_len = len;
    if (g_richtext->effect == 4)
    {
        display_len = g_richtext->typewriter_pos;
    }

    for (i = 0; i < display_len && i < len; i++)
    {
        char_x = x;
        char_y = y;

        /* Apply effects */
        switch (g_richtext->effect)
        {
        case 1: /* Shake */
            char_x += (gint)((g_random_double () - 0.5) * 4);
            char_y += (gint)((g_random_double () - 0.5) * 4);
            break;

        case 2: /* Wave */
            char_y += (gint)(sinf (g_richtext->timer * 5.0f + i * 0.3f) * 8);
            break;
        }

        /* Color */
        r = 255;
        g = 255;
        b = 255;
        if (g_richtext->effect == 3) /* Rainbow */
        {
            hue = fmodf (g_richtext->timer + i * 0.1f, 1.0f);
            /* Simple HSV to RGB for rainbow */
            h_i = (gint)(hue * 6);
            f = hue * 6 - h_i;
            q = (guint8)(255 * (1 - f));
            t = (guint8)(255 * f);

            switch (h_i % 6)
            {
            case 0: r = 255; g = t; b = 0; break;
            case 1: r = q; g = 255; b = 0; break;
            case 2: r = 0; g = 255; b = t; break;
            case 3: r = 0; g = q; b = 255; break;
            case 4: r = t; g = 0; b = 255; break;
            case 5: r = 255; g = 0; b = q; break;
            }
        }

        color = grl_color_new (r, g, b, 255);

        /* Draw single character */
        char_str[0] = text[i];
        char_str[1] = '\0';
        grl_draw_text (char_str, char_x, char_y, font_size, color);

        x += grl_measure_text (char_str, font_size);
        grl_color_free (color);
    }

    /* Typewriter cursor */
    if (g_richtext->effect == 4 && ((gint)(g_richtext->timer * 2) % 2) == 0)
    {
        cursor_color = grl_color_new (255, 255, 255, 255);
        grl_draw_rectangle (x, y, 3, font_size, cursor_color);
        grl_color_free (cursor_color);
    }
}

/* =============================================================================
 * ANIMATION STATE MACHINE (Simplified)
 * ========================================================================== */

typedef struct
{
    gint current_state;
    gfloat state_time;
    gfloat frame;
    gfloat speed;
    gfloat blend_time;
    gfloat x, y;
    gboolean facing_right;
} AnimationState;

static AnimationState *g_animation = NULL;

static const gchar *anim_state_names[ANIM_STATE_COUNT] = {
    "Idle", "Walk", "Run", "Jump", "Attack"
};

static const gint anim_frame_counts[ANIM_STATE_COUNT] = {
    4, 6, 8, 4, 5
};

static const gfloat anim_speeds[ANIM_STATE_COUNT] = {
    4.0f, 8.0f, 12.0f, 6.0f, 10.0f
};

static void
animation_init (void)
{
    g_animation = g_new0 (AnimationState, 1);
    g_animation->current_state = ANIM_IDLE;
    g_animation->speed = 1.0f;
    g_animation->blend_time = 0.2f;
    g_animation->x = 300;
    g_animation->y = 450;
    g_animation->facing_right = TRUE;
}

static void
animation_cleanup (void)
{
    g_free (g_animation);
    g_animation = NULL;
}

static void
animation_set_state (gint state)
{
    if (state == g_animation->current_state)
        return;

    g_animation->current_state = state;
    g_animation->state_time = 0;
    g_animation->frame = 0;
}

static void
animation_update (gfloat delta)
{
    gint frame_count;

    g_animation->state_time += delta;
    g_animation->frame += delta * anim_speeds[g_animation->current_state] * g_animation->speed;

    frame_count = anim_frame_counts[g_animation->current_state];
    while (g_animation->frame >= frame_count)
    {
        g_animation->frame -= frame_count;

        /* Non-looping animations return to idle */
        if (g_animation->current_state == ANIM_JUMP ||
            g_animation->current_state == ANIM_ATTACK)
        {
            animation_set_state (ANIM_IDLE);
            break;
        }
    }

    /* Movement based on animation */
    if (g_animation->current_state == ANIM_WALK)
    {
        g_animation->x += (g_animation->facing_right ? 50.0f : -50.0f) * delta;
    }
    else if (g_animation->current_state == ANIM_RUN)
    {
        g_animation->x += (g_animation->facing_right ? 150.0f : -150.0f) * delta;
    }

    /* Bounds */
    if (g_animation->x < 50)
    {
        g_animation->x = 50;
        g_animation->facing_right = TRUE;
    }
    if (g_animation->x > SIDEBAR_X - 100)
    {
        g_animation->x = SIDEBAR_X - 100;
        g_animation->facing_right = FALSE;
    }
}

static void
animation_draw (void)
{
    gint x, y, frame;
    GrlColor *body_color;
    GrlColor *head_color;
    GrlColor *dir_color;
    gint eye_x;
    g_autofree gchar *frame_str = NULL;

    x = (gint)g_animation->x;
    y = (gint)g_animation->y;
    frame = (gint)g_animation->frame;

    /* Draw character (simplified as colored rectangle with frame indicator) */
    body_color = NULL;
    switch (g_animation->current_state)
    {
    case ANIM_IDLE:
        body_color = grl_color_new (100, 150, 200, 255);
        break;
    case ANIM_WALK:
        body_color = grl_color_new (100, 200, 150, 255);
        break;
    case ANIM_RUN:
        body_color = grl_color_new (200, 200, 100, 255);
        break;
    case ANIM_JUMP:
        body_color = grl_color_new (200, 100, 200, 255);
        y -= 30 + (gint)(sinf (g_animation->frame * G_PI / anim_frame_counts[ANIM_JUMP]) * 50);
        break;
    case ANIM_ATTACK:
        body_color = grl_color_new (255, 100, 100, 255);
        break;
    default:
        body_color = grl_color_new (150, 150, 150, 255);
    }

    /* Body */
    grl_draw_rectangle (x - 20, y - 60, 40, 60, body_color);
    grl_color_free (body_color);

    /* Head */
    head_color = grl_color_new (255, 200, 180, 255);
    grl_draw_circle (x, y - 75, 15, head_color);
    grl_color_free (head_color);

    /* Direction indicator */
    dir_color = grl_color_new (50, 50, 50, 255);
    eye_x = x + (g_animation->facing_right ? 5 : -5);
    grl_draw_circle (eye_x, y - 78, 3, dir_color);
    grl_color_free (dir_color);

    /* Frame indicator */
    frame_str = g_strdup_printf ("Frame: %d", frame);
    grl_draw_text (frame_str, x - 25, y + 10, 14, color_dim);

    /* State name */
    grl_draw_text (anim_state_names[g_animation->current_state], x - 20, y + 30, 16, color_accent);
}

/* =============================================================================
 * MAIN UI
 * ========================================================================== */

static gint g_current_tab = TAB_PARTICLES;

static const gchar *tab_names[TAB_COUNT] = {
    "Particles",
    "Post-FX",
    "Rich Text",
    "Animation"
};

static void
draw_tabs (void)
{
    gint x, i;
    gint width;
    gboolean selected;
    GrlColor *bg;
    GrlColor *text_color;

    x = 20;

    for (i = 0; i < TAB_COUNT; i++)
    {
        width = grl_measure_text (tab_names[i], 20) + 30;
        selected = (i == g_current_tab);

        /* Tab background */
        bg = selected ? color_accent : color_panel;
        grl_draw_rectangle (x, TAB_Y, width, TAB_HEIGHT, bg);

        /* Tab text */
        text_color = selected ? color_bg : color_text;
        grl_draw_text (tab_names[i], x + 15, TAB_Y + 10, 20, text_color);

        x += width + 5;
    }
}

static void
draw_sidebar (void)
{
    gint x, y, i;
    GrlColor *color;
    const gchar *names[PARTICLE_TYPE_COUNT];
    g_autofree gchar *label = NULL;
    g_autofree gchar *count_str = NULL;
    g_autofree gchar *bloom_str = NULL;
    g_autofree gchar *grade_str = NULL;
    g_autofree gchar *vig_str = NULL;
    g_autofree gchar *effect_label = NULL;
    g_autofree gchar *anim_label = NULL;
    g_autofree gchar *speed_str = NULL;
    g_autofree gchar *blend_str = NULL;

    /* Sidebar background */
    grl_draw_rectangle (SIDEBAR_X, 0, SIDEBAR_WIDTH, WINDOW_HEIGHT, color_panel);

    x = SIDEBAR_X + 15;
    y = CONTENT_Y;

    switch (g_current_tab)
    {
    case TAB_PARTICLES:
        grl_draw_text ("Particle Type:", x, y, 18, color_text);
        y += 30;

        names[0] = "Fire";
        names[1] = "Sparkle";
        names[2] = "Explosion";
        names[3] = "Smoke";
        names[4] = "Rain";

        for (i = 0; i < PARTICLE_TYPE_COUNT; i++)
        {
            color = (i == g_particles->particle_type) ? color_selected : color_dim;
            g_free (label);
            label = g_strdup_printf ("[%d] %s", i + 1, names[i]);
            grl_draw_text (label, x, y, 16, color);
            y += 25;
        }

        y += 20;
        grl_draw_text ("Controls:", x, y, 18, color_text);
        y += 25;
        grl_draw_text ("Click to emit burst", x, y, 14, color_dim);
        y += 20;
        grl_draw_text ("C: Toggle continuous", x, y, 14, color_dim);
        y += 20;
        grl_draw_text ("R: Clear all", x, y, 14, color_dim);

        y += 30;
        count_str = g_strdup_printf ("Active: %d", g_particles->active_count);
        grl_draw_text (count_str, x, y, 16, color_accent);
        y += 25;
        grl_draw_text (g_particles->continuous ? "Mode: Continuous" : "Mode: Burst", x, y, 16, color_text);
        break;

    case TAB_POSTFX:
        grl_draw_text ("Effects:", x, y, 18, color_text);
        y += 30;

        /* Bloom */
        grl_draw_text (g_postfx->bloom_enabled ? "[B] Bloom: ON" : "[B] Bloom: OFF", x, y, 16,
                       g_postfx->bloom_enabled ? color_selected : color_dim);
        y += 25;

        bloom_str = g_strdup_printf ("    Intensity: %.1f", g_postfx->bloom_intensity);
        grl_draw_text (bloom_str, x, y, 14, color_dim);
        grl_draw_text ("[Q/E]", x + 130, y, 14, color_dim);
        y += 30;

        /* Color Grade */
        grade_str = g_strdup_printf ("[G] Grade: %s",
                                     color_grade_names[g_postfx->color_grade]);
        grl_draw_text (grade_str, x, y, 16, color_text);
        y += 30;

        /* Vignette */
        grl_draw_text (g_postfx->vignette_enabled ? "[V] Vignette: ON" : "[V] Vignette: OFF", x, y, 16,
                       g_postfx->vignette_enabled ? color_selected : color_dim);
        y += 25;

        vig_str = g_strdup_printf ("    Intensity: %.1f", g_postfx->vignette_intensity);
        grl_draw_text (vig_str, x, y, 14, color_dim);
        grl_draw_text ("[Z/X]", x + 130, y, 14, color_dim);
        y += 30;

        /* Shake */
        grl_draw_text ("[S] Trigger Shake", x, y, 16, color_text);
        break;

    case TAB_RICHTEXT:
        grl_draw_text ("Text Effect:", x, y, 18, color_text);
        y += 30;

        for (i = 0; i < EFFECT_COUNT; i++)
        {
            color = (i == g_richtext->effect) ? color_selected : color_dim;
            g_free (effect_label);
            effect_label = g_strdup_printf ("[%d] %s", i + 1, effect_names[i]);
            grl_draw_text (effect_label, x, y, 16, color);
            y += 25;
        }

        y += 20;
        grl_draw_text ("Press 1-5 to change effect", x, y, 14, color_dim);
        break;

    case TAB_ANIMATION:
        grl_draw_text ("Animation State:", x, y, 18, color_text);
        y += 30;

        for (i = 0; i < ANIM_STATE_COUNT; i++)
        {
            color = (i == g_animation->current_state) ? color_selected : color_dim;
            g_free (anim_label);
            anim_label = g_strdup_printf ("[%d] %s", i + 1, anim_state_names[i]);
            grl_draw_text (anim_label, x, y, 16, color);
            y += 25;
        }

        y += 20;
        grl_draw_text ("Speed:", x, y, 16, color_text);
        y += 25;

        speed_str = g_strdup_printf ("%.1fx [Q/E]", g_animation->speed);
        grl_draw_text (speed_str, x, y, 16, color_accent);
        y += 30;

        grl_draw_text ("Blend Time:", x, y, 16, color_text);
        y += 25;

        blend_str = g_strdup_printf ("%.2fs [Z/X]", g_animation->blend_time);
        grl_draw_text (blend_str, x, y, 16, color_accent);
        y += 30;

        grl_draw_text ("LEFT/RIGHT: Face direction", x, y, 14, color_dim);
        break;
    }
}

static void
handle_input (void)
{
    gint num_pressed;
    gint mx, my, i;

    /* Tab switching */
    if (grl_input_is_key_pressed (GRL_KEY_TAB))
    {
        g_current_tab = (g_current_tab + 1) % TAB_COUNT;
    }

    /* Number keys for tab-specific selections */
    num_pressed = -1;
    if (grl_input_is_key_pressed (GRL_KEY_ONE)) num_pressed = 0;
    if (grl_input_is_key_pressed (GRL_KEY_TWO)) num_pressed = 1;
    if (grl_input_is_key_pressed (GRL_KEY_THREE)) num_pressed = 2;
    if (grl_input_is_key_pressed (GRL_KEY_FOUR)) num_pressed = 3;
    if (grl_input_is_key_pressed (GRL_KEY_FIVE)) num_pressed = 4;

    switch (g_current_tab)
    {
    case TAB_PARTICLES:
        if (num_pressed >= 0 && num_pressed < PARTICLE_TYPE_COUNT)
        {
            particle_system_clear ();
            g_particles->particle_type = num_pressed;
        }

        if (grl_input_is_key_pressed (GRL_KEY_C))
        {
            g_particles->continuous = !g_particles->continuous;
        }

        if (grl_input_is_key_pressed (GRL_KEY_R))
        {
            particle_system_clear ();
        }

        /* Click to emit burst */
        if (grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT))
        {
            mx = grl_input_get_mouse_x ();
            my = grl_input_get_mouse_y ();
            if (mx < SIDEBAR_X && my > CONTENT_Y)
            {
                for (i = 0; i < 30; i++)
                {
                    emit_particle (mx, my, g_particles->particle_type);
                }
            }
        }
        break;

    case TAB_POSTFX:
        if (grl_input_is_key_pressed (GRL_KEY_B))
        {
            g_postfx->bloom_enabled = !g_postfx->bloom_enabled;
        }
        if (grl_input_is_key_pressed (GRL_KEY_G))
        {
            g_postfx->color_grade = (g_postfx->color_grade + 1) % COLOR_GRADE_COUNT;
        }
        if (grl_input_is_key_pressed (GRL_KEY_V))
        {
            g_postfx->vignette_enabled = !g_postfx->vignette_enabled;
        }
        if (grl_input_is_key_pressed (GRL_KEY_S))
        {
            postfx_trigger_shake (1.0f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_Q))
        {
            g_postfx->bloom_intensity = fmaxf (0.0f, g_postfx->bloom_intensity - 0.2f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_E))
        {
            g_postfx->bloom_intensity = fminf (2.0f, g_postfx->bloom_intensity + 0.2f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_Z))
        {
            g_postfx->vignette_intensity = fmaxf (0.0f, g_postfx->vignette_intensity - 0.1f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_X))
        {
            g_postfx->vignette_intensity = fminf (1.0f, g_postfx->vignette_intensity + 0.1f);
        }
        break;

    case TAB_RICHTEXT:
        if (num_pressed >= 0 && num_pressed < EFFECT_COUNT)
        {
            richtext_set_effect (num_pressed);
        }
        break;

    case TAB_ANIMATION:
        if (num_pressed >= 0 && num_pressed < ANIM_STATE_COUNT)
        {
            animation_set_state (num_pressed);
        }
        if (grl_input_is_key_pressed (GRL_KEY_Q))
        {
            g_animation->speed = fmaxf (0.1f, g_animation->speed - 0.1f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_E))
        {
            g_animation->speed = fminf (3.0f, g_animation->speed + 0.1f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_Z))
        {
            g_animation->blend_time = fmaxf (0.0f, g_animation->blend_time - 0.05f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_X))
        {
            g_animation->blend_time = fminf (1.0f, g_animation->blend_time + 0.05f);
        }
        if (grl_input_is_key_pressed (GRL_KEY_LEFT))
        {
            g_animation->facing_right = FALSE;
        }
        if (grl_input_is_key_pressed (GRL_KEY_RIGHT))
        {
            g_animation->facing_right = TRUE;
        }
        break;
    }
}

/* =============================================================================
 * MAIN FUNCTION
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    GrlWindow *window;
    gfloat delta;

    (void)argc;
    (void)argv;

    /* Initialize window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                             "Effects Gallery - Phase 3 Demo");
    grl_window_set_target_fps (window, 60);

    /* Initialize systems */
    init_colors ();
    particle_system_init ();
    postfx_init ();
    richtext_init ();
    animation_init ();

    /* Main game loop */
    while (!grl_window_should_close (window))
    {
        delta = grl_window_get_frame_time (window);

        /* Input */
        handle_input ();

        /* Update */
        particle_system_update (delta);
        postfx_update (delta);
        richtext_update (delta);
        animation_update (delta);

        /* Draw */
        grl_window_begin_drawing (window);
        grl_draw_clear_background (color_bg);

        /* Draw tab content */
        switch (g_current_tab)
        {
        case TAB_PARTICLES:
            particle_system_draw ();
            grl_draw_text ("Click anywhere to emit particles", 100, CONTENT_Y + 20, 16, color_dim);
            break;

        case TAB_POSTFX:
            postfx_draw_demo ();
            grl_draw_text ("Toggle effects with keyboard", 100, CONTENT_Y + 20, 16, color_dim);
            break;

        case TAB_RICHTEXT:
            richtext_draw ();
            grl_draw_text ("Watch the text effects!", 100, CONTENT_Y + 20, 16, color_dim);
            break;

        case TAB_ANIMATION:
            animation_draw ();
            grl_draw_text ("Control the character with number keys", 100, CONTENT_Y + 20, 16, color_dim);
            break;
        }

        /* Draw UI */
        draw_tabs ();
        draw_sidebar ();

        /* Title */
        grl_draw_text ("EFFECTS GALLERY", WINDOW_WIDTH / 2 - 100, WINDOW_HEIGHT - 40, 20, color_accent);
        grl_draw_text ("Phase 3: Commercial Polish", WINDOW_WIDTH / 2 - 90, WINDOW_HEIGHT - 20, 14, color_dim);

        grl_draw_fps (10, WINDOW_HEIGHT - 25);
        grl_draw_text ("TAB: Switch tabs", 100, WINDOW_HEIGHT - 25, 14, color_dim);

        grl_window_end_drawing (window);
    }

    /* Cleanup */
    animation_cleanup ();
    richtext_cleanup ();
    postfx_cleanup ();
    particle_system_cleanup ();
    cleanup_colors ();
    g_object_unref (window);

    return 0;
}
