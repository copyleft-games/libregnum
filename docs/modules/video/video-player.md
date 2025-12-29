# Video Player

`LrgVideoPlayer` provides video playback with audio synchronization and subtitle support.

## Type

```c
#define LRG_TYPE_VIDEO_PLAYER (lrg_video_player_get_type ())
G_DECLARE_FINAL_TYPE (LrgVideoPlayer, lrg_video_player, LRG, VIDEO_PLAYER, GObject)
```

## Creation

```c
LrgVideoPlayer *lrg_video_player_new (void);
```

## Opening and Closing

```c
gboolean lrg_video_player_open (LrgVideoPlayer  *player,
                                 const gchar     *path,
                                 GError         **error);

void lrg_video_player_close (LrgVideoPlayer *player);

gboolean lrg_video_player_is_open (LrgVideoPlayer *player);
```

## Playback Control

```c
void lrg_video_player_play (LrgVideoPlayer *player);
void lrg_video_player_pause (LrgVideoPlayer *player);
void lrg_video_player_stop (LrgVideoPlayer *player);
void lrg_video_player_seek (LrgVideoPlayer *player, gdouble position);
```

## State

```c
typedef enum {
    LRG_VIDEO_STATE_STOPPED,   /* Not playing, position at 0 */
    LRG_VIDEO_STATE_LOADING,   /* File is being opened */
    LRG_VIDEO_STATE_PLAYING,   /* Actively playing */
    LRG_VIDEO_STATE_PAUSED,    /* Paused at current position */
    LRG_VIDEO_STATE_FINISHED,  /* Reached end of video */
    LRG_VIDEO_STATE_ERROR      /* Error occurred */
} LrgVideoState;

LrgVideoState lrg_video_player_get_state (LrgVideoPlayer *player);
```

## Position and Duration

```c
gdouble lrg_video_player_get_position (LrgVideoPlayer *player);
gdouble lrg_video_player_get_duration (LrgVideoPlayer *player);
```

## Video Properties

```c
guint lrg_video_player_get_width (LrgVideoPlayer *player);
guint lrg_video_player_get_height (LrgVideoPlayer *player);
gfloat lrg_video_player_get_frame_rate (LrgVideoPlayer *player);
```

## Audio

```c
void lrg_video_player_set_volume (LrgVideoPlayer *player, gfloat volume);
gfloat lrg_video_player_get_volume (LrgVideoPlayer *player);

void lrg_video_player_set_muted (LrgVideoPlayer *player, gboolean muted);
gboolean lrg_video_player_get_muted (LrgVideoPlayer *player);
```

## Playback Options

```c
void lrg_video_player_set_loop (LrgVideoPlayer *player, gboolean loop);
gboolean lrg_video_player_get_loop (LrgVideoPlayer *player);

void lrg_video_player_set_playback_rate (LrgVideoPlayer *player, gfloat rate);
gfloat lrg_video_player_get_playback_rate (LrgVideoPlayer *player);
```

## Update and Draw

```c
void lrg_video_player_update (LrgVideoPlayer *player, gfloat delta_time);

void lrg_video_player_draw (LrgVideoPlayer *player,
                             gint            x,
                             gint            y,
                             gint            width,
                             gint            height);
```

## Texture Access

```c
LrgVideoTexture *lrg_video_player_get_texture (LrgVideoPlayer *player);
```

## Subtitles

```c
gboolean lrg_video_player_load_subtitles (LrgVideoPlayer  *player,
                                           const gchar     *path,
                                           GError         **error);

LrgVideoSubtitles *lrg_video_player_get_subtitles (LrgVideoPlayer *player);
```

## Error Handling

```c
typedef enum {
    LRG_VIDEO_ERROR_NONE,       /* No error */
    LRG_VIDEO_ERROR_FAILED,     /* Generic failure */
    LRG_VIDEO_ERROR_NOT_FOUND,  /* Video file not found */
    LRG_VIDEO_ERROR_FORMAT,     /* Unsupported format */
    LRG_VIDEO_ERROR_CODEC,      /* Codec not available */
    LRG_VIDEO_ERROR_DECODE,     /* Decode error */
    LRG_VIDEO_ERROR_SEEK,       /* Seek operation failed */
    LRG_VIDEO_ERROR_AUDIO       /* Audio stream error */
} LrgVideoError;

LrgVideoError lrg_video_player_get_error (LrgVideoPlayer *player);
const gchar *lrg_video_player_get_error_message (LrgVideoPlayer *player);
```

---

## Example: Basic Playback

```c
g_autoptr(LrgVideoPlayer) player = lrg_video_player_new ();
g_autoptr(GError) error = NULL;

if (!lrg_video_player_open (player, "cutscene.mp4", &error))
{
    g_warning ("Failed to open: %s", error->message);
    return;
}

lrg_video_player_play (player);

/* Game loop */
while (running)
{
    lrg_video_player_update (player, delta_time);

    if (lrg_video_player_get_state (player) == LRG_VIDEO_STATE_FINISHED)
    {
        running = FALSE;
    }

    lrg_video_player_draw (player, 0, 0, screen_width, screen_height);
}

lrg_video_player_close (player);
```

---

## Example: Looping Background Video

```c
g_autoptr(LrgVideoPlayer) bg_video = lrg_video_player_new ();
lrg_video_player_open (bg_video, "menu_background.mp4", NULL);
lrg_video_player_set_loop (bg_video, TRUE);
lrg_video_player_set_volume (bg_video, 0.0f);  /* Mute background video */
lrg_video_player_play (bg_video);
```

---

## Example: Video with Subtitles

```c
g_autoptr(LrgVideoPlayer) player = lrg_video_player_new ();
lrg_video_player_open (player, "dialog.mp4", NULL);

/* Load subtitles */
lrg_video_player_load_subtitles (player, "dialog.srt", NULL);

/* Configure subtitle appearance */
LrgVideoSubtitles *subs = lrg_video_player_get_subtitles (player);
lrg_video_subtitles_set_font_size (subs, 24.0f);
lrg_video_subtitles_set_position (subs, LRG_SUBTITLE_POSITION_BOTTOM);
lrg_video_subtitles_set_visible (subs, TRUE);

lrg_video_player_play (player);
```

---

## Example: Seek and Progress

```c
/* Display progress bar */
gdouble position = lrg_video_player_get_position (player);
gdouble duration = lrg_video_player_get_duration (player);
gfloat progress = (gfloat)(position / duration);

draw_progress_bar (progress);

/* Seek on click */
if (clicked_on_progress_bar)
{
    gdouble seek_pos = click_x_ratio * duration;
    lrg_video_player_seek (player, seek_pos);
}
```

---

## Example: Playback Rate

```c
/* Slow motion */
lrg_video_player_set_playback_rate (player, 0.5f);

/* Normal speed */
lrg_video_player_set_playback_rate (player, 1.0f);

/* Fast forward */
lrg_video_player_set_playback_rate (player, 2.0f);
```

---

## Video Texture

Access the raw texture for custom rendering:

```c
LrgVideoTexture *texture = lrg_video_player_get_texture (player);

if (lrg_video_texture_is_valid (texture))
{
    guint width = lrg_video_texture_get_width (texture);
    guint height = lrg_video_texture_get_height (texture);

    gsize size;
    const guint8 *data = lrg_video_texture_get_data (texture, &size);

    /* Custom rendering with RGBA data */
}
```

---

## State Machine

Handle state transitions:

```c
void
on_update (gfloat delta_time)
{
    lrg_video_player_update (player, delta_time);

    LrgVideoState state = lrg_video_player_get_state (player);

    switch (state)
    {
    case LRG_VIDEO_STATE_PLAYING:
        /* Normal playback */
        break;

    case LRG_VIDEO_STATE_FINISHED:
        /* Video ended - proceed to next scene */
        on_video_complete ();
        break;

    case LRG_VIDEO_STATE_ERROR:
        /* Handle error */
        g_warning ("Video error: %s",
                   lrg_video_player_get_error_message (player));
        skip_video ();
        break;

    default:
        break;
    }
}
```

---

## Performance Tips

1. **Pre-load videos** - Open videos before they're needed
2. **Match resolution** - Use video resolution close to display size
3. **Limit concurrent players** - Each player uses significant memory
4. **Close when done** - Call `close()` to free resources immediately
