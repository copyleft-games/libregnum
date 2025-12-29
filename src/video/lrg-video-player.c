/* lrg-video-player.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-video-player.h"
#include <gio/gio.h>
#include <string.h>

/**
 * SECTION:lrg-video-player
 * @Title: LrgVideoPlayer
 * @Short_description: Video playback controller
 *
 * #LrgVideoPlayer provides video playback with support for seeking,
 * volume control, subtitles, and loop modes.
 *
 * The player decodes video frames into a texture that can be rendered
 * with standard 2D drawing routines.
 *
 * Note: Actual video decoding requires FFmpeg. Without it, the player
 * provides a stub implementation for testing.
 */

struct _LrgVideoPlayer
{
    GObject            parent_instance;

    /* State */
    LrgVideoState      state;
    LrgVideoError      error;
    gchar             *error_message;
    gchar             *path;

    /* Video info */
    guint              width;
    guint              height;
    gfloat             frame_rate;
    gdouble            duration;

    /* Playback */
    gdouble            position;
    gfloat             volume;
    gboolean           muted;
    gboolean           loop;
    gfloat             playback_rate;

    /* Rendering */
    LrgVideoTexture   *texture;
    LrgVideoSubtitles *subtitles;
};

G_DEFINE_TYPE (LrgVideoPlayer, lrg_video_player, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_STATE,
    PROP_POSITION,
    PROP_DURATION,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FRAME_RATE,
    PROP_VOLUME,
    PROP_MUTED,
    PROP_LOOP,
    PROP_PLAYBACK_RATE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STATE_CHANGED,
    SIGNAL_POSITION_CHANGED,
    SIGNAL_FINISHED,
    SIGNAL_ERROR,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_video_player_dispose (GObject *object)
{
    LrgVideoPlayer *self = LRG_VIDEO_PLAYER (object);

    g_clear_object (&self->texture);
    g_clear_object (&self->subtitles);

    G_OBJECT_CLASS (lrg_video_player_parent_class)->dispose (object);
}

static void
lrg_video_player_finalize (GObject *object)
{
    LrgVideoPlayer *self = LRG_VIDEO_PLAYER (object);

    g_free (self->path);
    g_free (self->error_message);

    G_OBJECT_CLASS (lrg_video_player_parent_class)->finalize (object);
}

static void
lrg_video_player_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgVideoPlayer *self = LRG_VIDEO_PLAYER (object);

    switch (prop_id)
    {
        case PROP_STATE:
            g_value_set_enum (value, self->state);
            break;
        case PROP_POSITION:
            g_value_set_double (value, self->position);
            break;
        case PROP_DURATION:
            g_value_set_double (value, self->duration);
            break;
        case PROP_WIDTH:
            g_value_set_uint (value, self->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint (value, self->height);
            break;
        case PROP_FRAME_RATE:
            g_value_set_float (value, self->frame_rate);
            break;
        case PROP_VOLUME:
            g_value_set_float (value, self->volume);
            break;
        case PROP_MUTED:
            g_value_set_boolean (value, self->muted);
            break;
        case PROP_LOOP:
            g_value_set_boolean (value, self->loop);
            break;
        case PROP_PLAYBACK_RATE:
            g_value_set_float (value, self->playback_rate);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_video_player_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgVideoPlayer *self = LRG_VIDEO_PLAYER (object);

    switch (prop_id)
    {
        case PROP_VOLUME:
            lrg_video_player_set_volume (self, g_value_get_float (value));
            break;
        case PROP_MUTED:
            lrg_video_player_set_muted (self, g_value_get_boolean (value));
            break;
        case PROP_LOOP:
            lrg_video_player_set_loop (self, g_value_get_boolean (value));
            break;
        case PROP_PLAYBACK_RATE:
            lrg_video_player_set_playback_rate (self, g_value_get_float (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
set_state (LrgVideoPlayer *self,
           LrgVideoState   new_state)
{
    if (self->state != new_state)
    {
        self->state = new_state;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
        g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, new_state);
    }
}

static void
set_error (LrgVideoPlayer *self,
           LrgVideoError   error,
           const gchar    *message)
{
    self->error = error;
    g_free (self->error_message);
    self->error_message = g_strdup (message);
    set_state (self, LRG_VIDEO_STATE_ERROR);
    g_signal_emit (self, signals[SIGNAL_ERROR], 0, error, message);
}

static void
lrg_video_player_class_init (LrgVideoPlayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_video_player_dispose;
    object_class->finalize = lrg_video_player_finalize;
    object_class->get_property = lrg_video_player_get_property;
    object_class->set_property = lrg_video_player_set_property;

    properties[PROP_STATE] =
        g_param_spec_enum ("state",
                           "State",
                           "Current playback state",
                           LRG_TYPE_VIDEO_STATE,
                           LRG_VIDEO_STATE_STOPPED,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_POSITION] =
        g_param_spec_double ("position",
                             "Position",
                             "Current playback position in seconds",
                             0.0, G_MAXDOUBLE, 0.0,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DURATION] =
        g_param_spec_double ("duration",
                             "Duration",
                             "Video duration in seconds",
                             0.0, G_MAXDOUBLE, 0.0,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_WIDTH] =
        g_param_spec_uint ("width",
                           "Width",
                           "Video width in pixels",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEIGHT] =
        g_param_spec_uint ("height",
                           "Height",
                           "Video height in pixels",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FRAME_RATE] =
        g_param_spec_float ("frame-rate",
                            "Frame Rate",
                            "Video frame rate in FPS",
                            0.0f, 1000.0f, 0.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VOLUME] =
        g_param_spec_float ("volume",
                            "Volume",
                            "Audio volume (0.0 to 1.0)",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MUTED] =
        g_param_spec_boolean ("muted",
                              "Muted",
                              "Whether audio is muted",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOOP] =
        g_param_spec_boolean ("loop",
                              "Loop",
                              "Whether to loop playback",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYBACK_RATE] =
        g_param_spec_float ("playback-rate",
                            "Playback Rate",
                            "Playback rate (1.0 = normal)",
                            0.1f, 4.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgVideoPlayer::state-changed:
     * @player: the player
     * @state: the new state
     *
     * Emitted when the playback state changes.
     */
    signals[SIGNAL_STATE_CHANGED] =
        g_signal_new ("state-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_VIDEO_STATE);

    /**
     * LrgVideoPlayer::position-changed:
     * @player: the player
     * @position: the new position in seconds
     *
     * Emitted when the playback position changes.
     */
    signals[SIGNAL_POSITION_CHANGED] =
        g_signal_new ("position-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_DOUBLE);

    /**
     * LrgVideoPlayer::finished:
     * @player: the player
     *
     * Emitted when playback reaches the end.
     */
    signals[SIGNAL_FINISHED] =
        g_signal_new ("finished",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgVideoPlayer::error:
     * @player: the player
     * @error: the error code
     * @message: error message
     *
     * Emitted when an error occurs.
     */
    signals[SIGNAL_ERROR] =
        g_signal_new ("error",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, LRG_TYPE_VIDEO_ERROR, G_TYPE_STRING);
}

static void
lrg_video_player_init (LrgVideoPlayer *self)
{
    self->state = LRG_VIDEO_STATE_STOPPED;
    self->error = LRG_VIDEO_ERROR_NONE;
    self->error_message = NULL;
    self->path = NULL;
    self->width = 0;
    self->height = 0;
    self->frame_rate = 0.0f;
    self->duration = 0.0;
    self->position = 0.0;
    self->volume = 1.0f;
    self->muted = FALSE;
    self->loop = FALSE;
    self->playback_rate = 1.0f;
    self->texture = NULL;
    self->subtitles = lrg_video_subtitles_new ();
}

LrgVideoPlayer *
lrg_video_player_new (void)
{
    return g_object_new (LRG_TYPE_VIDEO_PLAYER, NULL);
}

gboolean
lrg_video_player_open (LrgVideoPlayer  *player,
                       const gchar     *path,
                       GError         **error)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    /* Close any existing video */
    lrg_video_player_close (player);

    /* Check if file exists */
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        set_error (player, LRG_VIDEO_ERROR_NOT_FOUND, "Video file not found");
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Video file not found: %s", path);
        return FALSE;
    }

    set_state (player, LRG_VIDEO_STATE_LOADING);

    /* Store path */
    player->path = g_strdup (path);

#ifdef LRG_HAS_FFMPEG
    /* TODO: Actual FFmpeg initialization would go here */
    /*
     * 1. Open format context with avformat_open_input
     * 2. Find stream info with avformat_find_stream_info
     * 3. Find video and audio streams
     * 4. Open video codec
     * 5. Open audio codec
     * 6. Create decode thread
     */
#endif

    /*
     * For now, create a stub video with test pattern.
     * This allows testing without FFmpeg.
     */
    player->width = 640;
    player->height = 480;
    player->frame_rate = 30.0f;
    player->duration = 10.0;

    /* Create texture */
    g_clear_object (&player->texture);
    player->texture = lrg_video_texture_new (player->width, player->height);

    /* Clear error state */
    player->error = LRG_VIDEO_ERROR_NONE;
    g_clear_pointer (&player->error_message, g_free);

    set_state (player, LRG_VIDEO_STATE_STOPPED);

    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_WIDTH]);
    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_HEIGHT]);
    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_FRAME_RATE]);
    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_DURATION]);

    return TRUE;
}

void
lrg_video_player_close (LrgVideoPlayer *player)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    /* Stop playback */
    lrg_video_player_stop (player);

    /* Clean up resources */
    g_clear_pointer (&player->path, g_free);
    g_clear_object (&player->texture);

    /* Reset video info */
    player->width = 0;
    player->height = 0;
    player->frame_rate = 0.0f;
    player->duration = 0.0;
    player->position = 0.0;

    /* Clear error state */
    player->error = LRG_VIDEO_ERROR_NONE;
    g_clear_pointer (&player->error_message, g_free);

    set_state (player, LRG_VIDEO_STATE_STOPPED);

    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_WIDTH]);
    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_HEIGHT]);
    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_FRAME_RATE]);
    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_DURATION]);
    g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_POSITION]);
}

void
lrg_video_player_play (LrgVideoPlayer *player)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->state == LRG_VIDEO_STATE_LOADING ||
        player->state == LRG_VIDEO_STATE_ERROR)
    {
        return;
    }

    if (player->path == NULL)
        return;

    /* Resume from finished state at beginning */
    if (player->state == LRG_VIDEO_STATE_FINISHED)
        player->position = 0.0;

    set_state (player, LRG_VIDEO_STATE_PLAYING);
}

void
lrg_video_player_pause (LrgVideoPlayer *player)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->state == LRG_VIDEO_STATE_PLAYING)
    {
        set_state (player, LRG_VIDEO_STATE_PAUSED);
    }
}

void
lrg_video_player_stop (LrgVideoPlayer *player)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->state == LRG_VIDEO_STATE_PLAYING ||
        player->state == LRG_VIDEO_STATE_PAUSED ||
        player->state == LRG_VIDEO_STATE_FINISHED)
    {
        player->position = 0.0;
        g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_POSITION]);
        set_state (player, LRG_VIDEO_STATE_STOPPED);
    }
}

void
lrg_video_player_seek (LrgVideoPlayer *player,
                       gdouble         position)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->path == NULL)
        return;

    /* Clamp position */
    if (position < 0.0)
        position = 0.0;
    if (position > player->duration)
        position = player->duration;

    if (player->position != position)
    {
        player->position = position;
        g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_POSITION]);
        g_signal_emit (player, signals[SIGNAL_POSITION_CHANGED], 0, position);
    }
}

LrgVideoState
lrg_video_player_get_state (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), LRG_VIDEO_STATE_STOPPED);
    return player->state;
}

gdouble
lrg_video_player_get_position (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), 0.0);
    return player->position;
}

gdouble
lrg_video_player_get_duration (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), 0.0);
    return player->duration;
}

guint
lrg_video_player_get_width (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), 0);
    return player->width;
}

guint
lrg_video_player_get_height (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), 0);
    return player->height;
}

gfloat
lrg_video_player_get_frame_rate (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), 0.0f);
    return player->frame_rate;
}

void
lrg_video_player_set_volume (LrgVideoPlayer *player,
                             gfloat          volume)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    volume = CLAMP (volume, 0.0f, 1.0f);

    if (player->volume != volume)
    {
        player->volume = volume;
        g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_VOLUME]);
    }
}

gfloat
lrg_video_player_get_volume (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), 1.0f);
    return player->volume;
}

void
lrg_video_player_set_muted (LrgVideoPlayer *player,
                            gboolean        muted)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->muted != muted)
    {
        player->muted = muted;
        g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_MUTED]);
    }
}

gboolean
lrg_video_player_get_muted (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), FALSE);
    return player->muted;
}

void
lrg_video_player_set_loop (LrgVideoPlayer *player,
                           gboolean        loop)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->loop != loop)
    {
        player->loop = loop;
        g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_LOOP]);
    }
}

gboolean
lrg_video_player_get_loop (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), FALSE);
    return player->loop;
}

void
lrg_video_player_set_playback_rate (LrgVideoPlayer *player,
                                    gfloat          rate)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    rate = CLAMP (rate, 0.1f, 4.0f);

    if (player->playback_rate != rate)
    {
        player->playback_rate = rate;
        g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_PLAYBACK_RATE]);
    }
}

gfloat
lrg_video_player_get_playback_rate (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), 1.0f);
    return player->playback_rate;
}

void
lrg_video_player_update (LrgVideoPlayer *player,
                         gfloat          delta_time)
{
    gdouble new_position;

    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->state != LRG_VIDEO_STATE_PLAYING)
        return;

    /* Advance position */
    new_position = player->position + (gdouble) delta_time * player->playback_rate;

    /* Check for end of video */
    if (new_position >= player->duration)
    {
        if (player->loop)
        {
            /* Loop back to beginning */
            new_position = 0.0;
        }
        else
        {
            /* Video finished */
            new_position = player->duration;
            set_state (player, LRG_VIDEO_STATE_FINISHED);
            g_signal_emit (player, signals[SIGNAL_FINISHED], 0);
        }
    }

    if (player->position != new_position)
    {
        player->position = new_position;
        g_object_notify_by_pspec (G_OBJECT (player), properties[PROP_POSITION]);
        g_signal_emit (player, signals[SIGNAL_POSITION_CHANGED], 0, new_position);
    }

    /* Update subtitles */
    lrg_video_subtitles_update (player->subtitles, player->position);

    /*
     * In a real implementation with FFmpeg, this would:
     * 1. Check if we need a new frame based on PTS
     * 2. Pull decoded frame from queue
     * 3. Update texture with frame data
     * 4. Process audio samples
     */
}

LrgVideoTexture *
lrg_video_player_get_texture (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), NULL);
    return player->texture;
}

LrgVideoSubtitles *
lrg_video_player_get_subtitles (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), NULL);
    return player->subtitles;
}

gboolean
lrg_video_player_load_subtitles (LrgVideoPlayer  *player,
                                 const gchar     *path,
                                 GError         **error)
{
    g_autoptr(LrgVideoSubtitleTrack) track = NULL;
    gboolean result;

    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    track = lrg_video_subtitle_track_new ();

    /* Detect format from extension */
    if (g_str_has_suffix (path, ".srt"))
    {
        result = lrg_video_subtitle_track_load_srt (track, path, error);
    }
    else if (g_str_has_suffix (path, ".vtt"))
    {
        result = lrg_video_subtitle_track_load_vtt (track, path, error);
    }
    else
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                     "Unsupported subtitle format: %s", path);
        return FALSE;
    }

    if (result)
    {
        lrg_video_subtitles_set_track (player->subtitles, track);
    }

    return result;
}

void
lrg_video_player_draw (LrgVideoPlayer *player,
                       gint            x,
                       gint            y,
                       gint            width,
                       gint            height)
{
    g_return_if_fail (LRG_IS_VIDEO_PLAYER (player));

    if (player->texture == NULL)
        return;

    /*
     * In a real implementation, this would:
     * 1. Upload texture data to GPU if needed
     * 2. Draw textured quad at specified position/size
     * 3. Draw subtitles on top
     *
     * The actual rendering depends on the graphics backend (raylib, etc.)
     */

    /* Draw subtitles */
    lrg_video_subtitles_draw (player->subtitles, width, height);

    (void) x;
    (void) y;
}

gboolean
lrg_video_player_is_open (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), FALSE);
    return player->path != NULL;
}

LrgVideoError
lrg_video_player_get_error (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), LRG_VIDEO_ERROR_NONE);
    return player->error;
}

const gchar *
lrg_video_player_get_error_message (LrgVideoPlayer *player)
{
    g_return_val_if_fail (LRG_IS_VIDEO_PLAYER (player), NULL);
    return player->error_message;
}
