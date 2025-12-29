# Subtitles

The subtitle system consists of `LrgSubtitleCue`, `LrgVideoSubtitleTrack`, and `LrgVideoSubtitles` for loading, storing, and rendering subtitles.

## Subtitle Cue

`LrgSubtitleCue` is a GBoxed type representing a single subtitle entry.

```c
#define LRG_TYPE_SUBTITLE_CUE (lrg_subtitle_cue_get_type ())
```

### Creation

```c
LrgSubtitleCue *lrg_subtitle_cue_new (gdouble      start_time,
                                       gdouble      end_time,
                                       const gchar *text);

LrgSubtitleCue *lrg_subtitle_cue_copy (const LrgSubtitleCue *cue);
void lrg_subtitle_cue_free (LrgSubtitleCue *cue);
```

### Properties

```c
gdouble lrg_subtitle_cue_get_start_time (const LrgSubtitleCue *cue);
gdouble lrg_subtitle_cue_get_end_time (const LrgSubtitleCue *cue);
const gchar *lrg_subtitle_cue_get_text (const LrgSubtitleCue *cue);
```

### Time Check

```c
gboolean lrg_subtitle_cue_contains_time (const LrgSubtitleCue *cue,
                                          gdouble               time);
```

---

## Subtitle Track

`LrgVideoSubtitleTrack` manages a collection of cues.

```c
#define LRG_TYPE_VIDEO_SUBTITLE_TRACK (lrg_video_subtitle_track_get_type ())
G_DECLARE_FINAL_TYPE (LrgVideoSubtitleTrack, lrg_video_subtitle_track,
                      LRG, VIDEO_SUBTITLE_TRACK, GObject)
```

### Creation

```c
LrgVideoSubtitleTrack *lrg_video_subtitle_track_new (void);
```

### Loading Files

```c
gboolean lrg_video_subtitle_track_load_srt (LrgVideoSubtitleTrack  *track,
                                             const gchar            *path,
                                             GError                **error);

gboolean lrg_video_subtitle_track_load_vtt (LrgVideoSubtitleTrack  *track,
                                             const gchar            *path,
                                             GError                **error);

gboolean lrg_video_subtitle_track_load_data (LrgVideoSubtitleTrack  *track,
                                              const gchar            *data,
                                              const gchar            *format,
                                              GError                **error);
```

### Cue Management

```c
void lrg_video_subtitle_track_add_cue (LrgVideoSubtitleTrack *track,
                                        LrgSubtitleCue        *cue);

void lrg_video_subtitle_track_clear (LrgVideoSubtitleTrack *track);

guint lrg_video_subtitle_track_get_cue_count (LrgVideoSubtitleTrack *track);

const LrgSubtitleCue *lrg_video_subtitle_track_get_cue (LrgVideoSubtitleTrack *track,
                                                         guint                  index);
```

### Time-Based Queries

```c
gchar *lrg_video_subtitle_track_get_text_at (LrgVideoSubtitleTrack *track,
                                              gdouble                time);

GPtrArray *lrg_video_subtitle_track_get_cues_at (LrgVideoSubtitleTrack *track,
                                                  gdouble                time);

gdouble lrg_video_subtitle_track_get_duration (LrgVideoSubtitleTrack *track);
```

### Language

```c
void lrg_video_subtitle_track_set_language (LrgVideoSubtitleTrack *track,
                                             const gchar           *language);

const gchar *lrg_video_subtitle_track_get_language (LrgVideoSubtitleTrack *track);
```

---

## Subtitle Renderer

`LrgVideoSubtitles` renders subtitle text on screen.

```c
#define LRG_TYPE_VIDEO_SUBTITLES (lrg_video_subtitles_get_type ())
G_DECLARE_FINAL_TYPE (LrgVideoSubtitles, lrg_video_subtitles, LRG, VIDEO_SUBTITLES, GObject)
```

### Creation

```c
LrgVideoSubtitles *lrg_video_subtitles_new (void);
```

### Track

```c
void lrg_video_subtitles_set_track (LrgVideoSubtitles     *subtitles,
                                     LrgVideoSubtitleTrack *track);

LrgVideoSubtitleTrack *lrg_video_subtitles_get_track (LrgVideoSubtitles *subtitles);
```

### Visibility

```c
void lrg_video_subtitles_set_visible (LrgVideoSubtitles *subtitles, gboolean visible);
gboolean lrg_video_subtitles_get_visible (LrgVideoSubtitles *subtitles);
```

### Position

```c
typedef enum {
    LRG_SUBTITLE_POSITION_BOTTOM,  /* Subtitles at bottom of video */
    LRG_SUBTITLE_POSITION_TOP      /* Subtitles at top of video */
} LrgSubtitlePosition;

void lrg_video_subtitles_set_position (LrgVideoSubtitles   *subtitles,
                                        LrgSubtitlePosition  position);

LrgSubtitlePosition lrg_video_subtitles_get_position (LrgVideoSubtitles *subtitles);
```

### Appearance

```c
void lrg_video_subtitles_set_font_size (LrgVideoSubtitles *subtitles, gfloat size);
gfloat lrg_video_subtitles_get_font_size (LrgVideoSubtitles *subtitles);

void lrg_video_subtitles_set_color (LrgVideoSubtitles *subtitles,
                                     guint8 r, guint8 g, guint8 b, guint8 a);

void lrg_video_subtitles_get_color (LrgVideoSubtitles *subtitles,
                                     guint8 *r, guint8 *g, guint8 *b, guint8 *a);

void lrg_video_subtitles_set_background (LrgVideoSubtitles *subtitles, gboolean enabled);
gboolean lrg_video_subtitles_get_background (LrgVideoSubtitles *subtitles);

void lrg_video_subtitles_set_margin (LrgVideoSubtitles *subtitles, gfloat margin);
gfloat lrg_video_subtitles_get_margin (LrgVideoSubtitles *subtitles);
```

### Update and Draw

```c
void lrg_video_subtitles_update (LrgVideoSubtitles *subtitles, gdouble time);

const gchar *lrg_video_subtitles_get_current_text (LrgVideoSubtitles *subtitles);

void lrg_video_subtitles_draw (LrgVideoSubtitles *subtitles,
                                gint               screen_width,
                                gint               screen_height);
```

---

## Example: Loading SRT File

```c
g_autoptr(LrgVideoSubtitleTrack) track = lrg_video_subtitle_track_new ();
g_autoptr(GError) error = NULL;

if (!lrg_video_subtitle_track_load_srt (track, "dialog.srt", &error))
{
    g_warning ("Failed to load subtitles: %s", error->message);
}
else
{
    g_print ("Loaded %u cues\n",
             lrg_video_subtitle_track_get_cue_count (track));
}
```

---

## Example: Programmatic Cues

```c
g_autoptr(LrgVideoSubtitleTrack) track = lrg_video_subtitle_track_new ();

/* Add cues manually */
lrg_video_subtitle_track_add_cue (track,
    lrg_subtitle_cue_new (0.0, 3.0, "Welcome to the game!"));

lrg_video_subtitle_track_add_cue (track,
    lrg_subtitle_cue_new (3.5, 6.0, "Press any key to continue..."));

lrg_video_subtitle_track_add_cue (track,
    lrg_subtitle_cue_new (6.5, 10.0, "Good luck, adventurer!"));
```

---

## Example: Multiple Languages

```c
typedef struct {
    gchar *code;
    LrgVideoSubtitleTrack *track;
} SubtitleLanguage;

GPtrArray *languages;  /* Array of SubtitleLanguage */
LrgVideoSubtitleTrack *current_track;

void
load_subtitles (void)
{
    languages = g_ptr_array_new ();

    /* Load English */
    LrgVideoSubtitleTrack *en = lrg_video_subtitle_track_new ();
    lrg_video_subtitle_track_load_srt (en, "subs/video_en.srt", NULL);
    lrg_video_subtitle_track_set_language (en, "en");

    /* Load Spanish */
    LrgVideoSubtitleTrack *es = lrg_video_subtitle_track_new ();
    lrg_video_subtitle_track_load_srt (es, "subs/video_es.srt", NULL);
    lrg_video_subtitle_track_set_language (es, "es");

    /* Set default */
    current_track = en;
}

void
switch_language (const gchar *code)
{
    for (guint i = 0; i < languages->len; i++)
    {
        SubtitleLanguage *lang = g_ptr_array_index (languages, i);
        if (g_strcmp0 (lang->code, code) == 0)
        {
            current_track = lang->track;
            lrg_video_subtitles_set_track (subtitles, current_track);
            break;
        }
    }
}
```

---

## Example: Styled Subtitles

```c
LrgVideoSubtitles *subs = lrg_video_subtitles_new ();

/* Yellow text with black background */
lrg_video_subtitles_set_color (subs, 255, 255, 0, 255);
lrg_video_subtitles_set_background (subs, TRUE);
lrg_video_subtitles_set_font_size (subs, 28.0f);
lrg_video_subtitles_set_position (subs, LRG_SUBTITLE_POSITION_BOTTOM);
lrg_video_subtitles_set_margin (subs, 40.0f);
lrg_video_subtitles_set_visible (subs, TRUE);
```

---

## SRT Format Reference

```
1
00:00:01,000 --> 00:00:04,000
First subtitle line.
Can have multiple lines.

2
00:00:05,500 --> 00:00:08,000
Second subtitle.
```

- **Cue number**: Sequential integer (ignored but required)
- **Timing**: `HH:MM:SS,mmm --> HH:MM:SS,mmm`
- **Text**: One or more lines of text
- **Separator**: Empty line between cues

---

## WebVTT Format Reference

```
WEBVTT

00:00:01.000 --> 00:00:04.000
First subtitle line.
Can have multiple lines.

00:00:05.500 --> 00:00:08.000
Second subtitle.
```

- **Header**: Must start with `WEBVTT`
- **Timing**: `HH:MM:SS.mmm --> HH:MM:SS.mmm` (dot instead of comma)
- **Cue IDs**: Optional (can omit numbers)
- **Styling**: VTT supports styling tags (parsed but currently ignored)

---

## Overlapping Cues

Multiple cues can be active at the same time:

```c
gdouble current_time = 5.0;

/* Get all active cues */
g_autoptr(GPtrArray) cues = lrg_video_subtitle_track_get_cues_at (track, current_time);

for (guint i = 0; i < cues->len; i++)
{
    const LrgSubtitleCue *cue = g_ptr_array_index (cues, i);
    g_print ("Active: %s\n", lrg_subtitle_cue_get_text (cue));
}
```
