/* lrg-reel-player.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelPlayer - interactive preview window for a #LrgReel composition.
 */

#include "config.h"
#include "lrg-reel-player.h"
#include "lrg-reel.h"
#include "lrg-reel-renderer.h"
#include <graylib.h>
#include <math.h>

/*
 * Instance structure (FINAL — not exported to the header).
 */
struct _LrgReelPlayer
{
    GObject parent_instance;

    LrgReel         *reel;
    LrgReelRenderer *renderer;

    gint     cur_frame;
    gboolean playing;
    gboolean looping;
    gdouble  accumulator; /* fractional frames accumulated since last advance */
};

G_DEFINE_FINAL_TYPE (LrgReelPlayer, lrg_reel_player, G_TYPE_OBJECT)

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_player_dispose (GObject *object)
{
    LrgReelPlayer *self = LRG_REEL_PLAYER (object);

    g_clear_object (&self->renderer);
    g_clear_object (&self->reel);

    G_OBJECT_CLASS (lrg_reel_player_parent_class)->dispose (object);
}

static void
lrg_reel_player_class_init (LrgReelPlayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_reel_player_dispose;
}

static void
lrg_reel_player_init (LrgReelPlayer *self)
{
    self->reel        = NULL;
    self->renderer    = NULL;
    self->cur_frame   = 0;
    self->playing     = FALSE;
    self->looping     = FALSE;
    self->accumulator = 0.0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_player_new:
 * @reel: (transfer none): the #LrgReel to preview.
 *
 * Creates a new player bound to @reel.  No window is opened at this point;
 * call lrg_reel_player_run() to open the window and start playback.
 *
 * The player holds its own reference to @reel.
 *
 * Returns: (transfer full): a new #LrgReelPlayer
 *
 * Since: 1.0
 */
LrgReelPlayer *
lrg_reel_player_new (LrgReel *reel)
{
    LrgReelPlayer *self;

    g_return_val_if_fail (LRG_IS_REEL (reel), NULL);

    self           = g_object_new (LRG_TYPE_REEL_PLAYER, NULL);
    self->reel     = g_object_ref (reel);
    self->renderer = lrg_reel_renderer_new (reel);

    return self;
}

/**
 * lrg_reel_player_run:
 * @self: an #LrgReelPlayer
 *
 * Opens a window sized to the reel's resolution, starts playback from the
 * current frame, and blocks until the window is closed.
 *
 * Space toggles play/pause; Left/Right arrow keys step one frame backward or
 * forward (with key-repeat).  The reel is letterbox-fitted into the window
 * with black bars if the window size does not match the reel's aspect ratio.
 *
 * Since: 1.0
 */
void
lrg_reel_player_run (LrgReelPlayer *self)
{
    GrlWindow    *win;
    GrlTexture   *tex;
    GrlImage     *img;
    gint          reel_w;
    gint          reel_h;
    gdouble       fps;
    gint          total;
    const gchar  *reel_id;
    const gchar  *title;
    gint          target_fps;
    GrlColor      black;
    GrlColor      white;

    g_return_if_fail (LRG_IS_REEL_PLAYER (self));

    reel_w  = lrg_reel_get_width  (self->reel);
    reel_h  = lrg_reel_get_height (self->reel);
    fps     = lrg_reel_get_fps    (self->reel);
    total   = lrg_reel_get_duration_in_frames (self->reel);
    reel_id = lrg_reel_get_id (self->reel);
    title   = (reel_id != NULL && reel_id[0] != '\0') ? reel_id : "Reel";

    /* Round fps to nearest integer for the window target. */
    target_fps = (gint)(fps + 0.5);

    win = grl_window_new (reel_w, reel_h, title);
    grl_window_set_target_fps (win, target_fps);

    /* Upload the first frame to a GPU texture. */
    img = lrg_reel_renderer_get_canvas_image (self->renderer, self->cur_frame);
    tex = grl_texture_new_from_image (img);

    /* Start playing from the current position. */
    self->playing     = TRUE;
    self->accumulator = 0.0;

    /* Prepare stack-allocated GBoxed color values. */
    black.r = 0;   black.g = 0;   black.b = 0;   black.a = 255;
    white.r = 255; white.g = 255; white.b = 255; white.a = 255;

    while (!grl_window_should_close (win))
    {
        gint    ww;
        gint    wh;
        gdouble scale;
        gdouble dw;
        gdouble dh;
        gdouble dx;
        gdouble dy;

        /* ---------------------------------------------------------------
         * Input handling
         * --------------------------------------------------------------- */
        if (grl_input_is_key_pressed (GRL_KEY_SPACE))
            lrg_reel_player_toggle (self);

        if (grl_input_is_key_pressed_repeat (GRL_KEY_RIGHT))
            lrg_reel_player_seek (self, self->cur_frame + 1);

        if (grl_input_is_key_pressed_repeat (GRL_KEY_LEFT))
            lrg_reel_player_seek (self, self->cur_frame - 1);

        /* ---------------------------------------------------------------
         * Frame advance
         * --------------------------------------------------------------- */
        if (self->playing)
        {
            gdouble dt;

            dt = (gdouble) grl_window_get_frame_time (win);
            self->accumulator += dt * fps;

            while (self->accumulator >= 1.0)
            {
                self->cur_frame++;
                self->accumulator -= 1.0;
            }

            if (self->cur_frame >= total)
            {
                if (self->looping)
                    self->cur_frame = self->cur_frame % total;
                else
                {
                    self->cur_frame = total - 1;
                    self->playing   = FALSE;
                }
            }
        }

        /* ---------------------------------------------------------------
         * Render frame: composite on CPU, upload to GPU, blit to window.
         * --------------------------------------------------------------- */
        img = lrg_reel_renderer_get_canvas_image (self->renderer,
                                                   self->cur_frame);
        grl_texture_update (tex, img);

        grl_window_begin_drawing (win);
        grl_window_clear_background (win, &black);

        /* Letterbox-fit the reel into the current window dimensions. */
        ww    = grl_window_get_width  (win);
        wh    = grl_window_get_height (win);
        scale = (gdouble) ww / reel_w;
        if ((gdouble) wh / reel_h < scale)
            scale = (gdouble) wh / reel_h;

        dw = reel_w * scale;
        dh = reel_h * scale;
        dx = ((gdouble) ww - dw) * 0.5;
        dy = ((gdouble) wh - dh) * 0.5;

        {
            GrlRectangle src;
            GrlRectangle dst;
            GrlVector2   origin;

            src.x      = 0.0f;
            src.y      = 0.0f;
            src.width  = (gfloat) reel_w;
            src.height = (gfloat) reel_h;

            dst.x      = (gfloat) dx;
            dst.y      = (gfloat) dy;
            dst.width  = (gfloat) dw;
            dst.height = (gfloat) dh;

            origin.x = 0.0f;
            origin.y = 0.0f;

            grl_draw_texture_pro (tex, &src, &dst, &origin, 0.0f, &white);
        }

        grl_window_end_drawing (win);
    }

    /* Clean up GPU resources acquired in this scope. */
    g_clear_object (&tex);
    g_clear_object (&win);
}

/**
 * lrg_reel_player_play:
 * @self: an #LrgReelPlayer
 *
 * Starts or resumes playback.
 *
 * Since: 1.0
 */
void
lrg_reel_player_play (LrgReelPlayer *self)
{
    g_return_if_fail (LRG_IS_REEL_PLAYER (self));
    self->playing = TRUE;
}

/**
 * lrg_reel_player_pause:
 * @self: an #LrgReelPlayer
 *
 * Pauses playback.  The current frame is retained.
 *
 * Since: 1.0
 */
void
lrg_reel_player_pause (LrgReelPlayer *self)
{
    g_return_if_fail (LRG_IS_REEL_PLAYER (self));
    self->playing = FALSE;
}

/**
 * lrg_reel_player_toggle:
 * @self: an #LrgReelPlayer
 *
 * Toggles between playing and paused states.
 *
 * Since: 1.0
 */
void
lrg_reel_player_toggle (LrgReelPlayer *self)
{
    g_return_if_fail (LRG_IS_REEL_PLAYER (self));
    self->playing = !self->playing;
}

/**
 * lrg_reel_player_seek:
 * @self: an #LrgReelPlayer
 * @frame: the target frame number.
 *
 * Seeks to @frame, clamped to the range [0, duration - 1].
 * The frame accumulator is reset to zero on seek.
 *
 * Since: 1.0
 */
void
lrg_reel_player_seek (LrgReelPlayer *self,
                      gint           frame)
{
    gint total;

    g_return_if_fail (LRG_IS_REEL_PLAYER (self));

    total = lrg_reel_get_duration_in_frames (self->reel);

    if (frame < 0)
        frame = 0;
    if (frame >= total)
        frame = total - 1;

    self->cur_frame   = frame;
    self->accumulator = 0.0;
}

/**
 * lrg_reel_player_set_loop:
 * @self: an #LrgReelPlayer
 * @loop: %TRUE to loop, %FALSE to stop at the last frame.
 *
 * Sets whether the player loops back to frame 0 when the last frame is
 * reached.
 *
 * Since: 1.0
 */
void
lrg_reel_player_set_loop (LrgReelPlayer *self,
                           gboolean       loop)
{
    g_return_if_fail (LRG_IS_REEL_PLAYER (self));
    self->looping = loop;
}

/**
 * lrg_reel_player_get_loop:
 * @self: an #LrgReelPlayer
 *
 * Returns: %TRUE if looping is enabled.
 *
 * Since: 1.0
 */
gboolean
lrg_reel_player_get_loop (LrgReelPlayer *self)
{
    g_return_val_if_fail (LRG_IS_REEL_PLAYER (self), FALSE);
    return self->looping;
}

/**
 * lrg_reel_player_get_current_frame:
 * @self: an #LrgReelPlayer
 *
 * Returns: the current playback frame (0-based).
 *
 * Since: 1.0
 */
gint
lrg_reel_player_get_current_frame (LrgReelPlayer *self)
{
    g_return_val_if_fail (LRG_IS_REEL_PLAYER (self), 0);
    return self->cur_frame;
}

/**
 * lrg_reel_player_get_playing:
 * @self: an #LrgReelPlayer
 *
 * Returns: %TRUE if the player is currently playing (not paused or stopped).
 *
 * Since: 1.0
 */
gboolean
lrg_reel_player_get_playing (LrgReelPlayer *self)
{
    g_return_val_if_fail (LRG_IS_REEL_PLAYER (self), FALSE);
    return self->playing;
}
