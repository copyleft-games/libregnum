/* lrg-text-input.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Single-line text entry.
 *
 * The entry records itself through paint(): a BOX part (INPUT
 * background, BORDER border that glides to ACCENT while focused), a
 * LABEL part (text, or the placeholder in TEXT_MUTED), a FILL part
 * (the SELECTION highlight) and a MARK part (the caret, which fades in
 * and out instead of blinking hard). Text scrolls horizontally to keep
 * the caret in view and is clipped to the box.
 *
 * Editing: typed characters arrive as TEXT_INPUT events (composed
 * codepoints); arrows, Home/End (with Shift to select, Ctrl to jump
 * words), Backspace/Delete, Ctrl+A/C/X/V and mouse click / drag /
 * double-click selection are handled here. The legacy colour, padding,
 * radius and font-size properties keep working: once set explicitly
 * they replace the theme defaults, and the colours and radius are
 * applied in style_overrides(), after the theme's style rules, so they
 * beat any matching rule too.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-text-input.h"
#include "lrg-canvas.h"
#include "../lrg-log.h"
#include "../tween/lrg-spring.h"
#include <raylib.h>
#include <math.h>
#include <string.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

/* Glyph spacing used for drawing and measuring */
#define TEXT_SPACING (1.0f)

/* Caret: solid for this long after activity, then a smooth fade cycle */
#define CARET_SOLID_TIME  (0.6)
#define CARET_FADE_PERIOD (1.1)
#define CARET_WIDTH       (2.0f)

/* Two presses closer than this (seconds) count as a double click */
#define DOUBLE_CLICK_TIME (0.4)

/* ... and closer than this (canvas units) */
#define DOUBLE_CLICK_SLOP (4.0f)

/* Opacity of a disabled entry */
#define INPUT_DISABLED_OPACITY (0.45f)

struct _LrgTextInput
{
    LrgWidget  parent_instance;

    gchar     *text;
    gchar     *placeholder;
    guint      max_length;
    gboolean   password_mode;
    gint       cursor_position;
    gint       anchor;          /* other end of the selection (chars) */
    gboolean   focused;

    /* Legacy appearance properties and whether each was set explicitly */
    gfloat     font_size;
    GrlColor   text_color;
    GrlColor   background_color;
    GrlColor   border_color;
    GrlColor   placeholder_color;
    gfloat     corner_radius;
    gfloat     padding;
    gboolean   font_size_set;
    gboolean   text_color_set;
    gboolean   background_set;
    gboolean   border_set;
    gboolean   placeholder_color_set;
    gboolean   radius_set;
    gboolean   padding_set;

    /* Horizontal scroll of the text (canvas units, >= 0) */
    gfloat     scroll_x;

    /* Mouse selection */
    gboolean   dragging;
    gboolean   click_armed;     /* a first click may become a double click */
    gdouble    last_click_time; /* clock at that first click */
    gint       last_click_index;
    gfloat     last_click_x;

    /* Animation: focus cross-fade spring, caret phase. clock is the
     * sum of the step() frame times (seconds), so the caret phase and
     * double-click timing follow the canvas clock. */
    LrgSpring  focus_anim;
    gboolean   stepped;
    gdouble    clock;
    gdouble    activity_time;
};

G_DEFINE_TYPE (LrgTextInput, lrg_text_input, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_TEXT,
    PROP_PLACEHOLDER,
    PROP_MAX_LENGTH,
    PROP_PASSWORD_MODE,
    PROP_CURSOR_POSITION,
    PROP_FOCUSED,
    PROP_FONT_SIZE,
    PROP_TEXT_COLOR,
    PROP_BACKGROUND_COLOR,
    PROP_BORDER_COLOR,
    PROP_PLACEHOLDER_COLOR,
    PROP_CORNER_RADIUS,
    PROP_PADDING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TEXT_CHANGED,
    SIGNAL_SUBMITTED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Legacy default colours, reported by the getters until set */
static const GrlColor DEFAULT_TEXT        = { 255, 255, 255, 255 };
static const GrlColor DEFAULT_BACKGROUND  = { 40, 40, 40, 255 };
static const GrlColor DEFAULT_BORDER      = { 100, 100, 100, 255 };
static const GrlColor DEFAULT_PLACEHOLDER = { 120, 120, 120, 255 };

/* Process-wide clipboard used when no window (and so no system
 * clipboard) is available, e.g. in headless tests */
static gchar *fallback_clipboard = NULL;

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

/* text_length: length of the text in characters */
static gint
text_length (LrgTextInput *self)
{
    return self->text != NULL ? (gint) g_utf8_strlen (self->text, -1) : 0;
}

/* owning_canvas: the canvas that manages our focus, if any */
static LrgCanvas *
owning_canvas (LrgTextInput *self)
{
    LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));

    return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

/* effective metrics: explicit legacy values win over the theme */
static gfloat
eff_font_size (LrgTextInput *self)
{
    if (self->font_size_set)
        return self->font_size;
    return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
                                 LRG_THEME_METRIC_FONT_SIZE);
}

static gfloat
eff_padding (LrgTextInput *self)
{
    if (self->padding_set)
        return self->padding;
    return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
                                 LRG_THEME_METRIC_PADDING_X);
}

static GrlFont *
input_font (LrgTextInput *self)
{
    return lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
}

/*
 * get_display_text:
 *
 * The text as drawn: the real text, or one '*' per character in
 * password mode (so character offsets map one to one).
 */
static gchar *
get_display_text (LrgTextInput *self)
{
    if (self->text == NULL || self->text[0] == '\0')
        return g_strdup ("");

    if (self->password_mode)
        return g_strnfill ((gsize) text_length (self), '*');

    return g_strdup (self->text);
}

/*
 * offset_x:
 * @display: the display text
 * @index: a character offset
 *
 * X of the caret before character @index, relative to the text origin,
 * matching lrg_paint_list_text()'s layout (each glyph advance plus the
 * spacing; the caret sits in the middle of the gap).
 */
static gfloat
offset_x (LrgTextInput *self,
          const gchar  *display,
          gint          index)
{
    g_autofree gchar *prefix = NULL;
    const gchar      *end;
    gfloat            w = 0.0f;

    if (index <= 0 || display[0] == '\0')
        return 0.0f;

    end = g_utf8_offset_to_pointer (display, MIN (index, (gint) g_utf8_strlen (display, -1)));
    prefix = g_strndup (display, (gsize) (end - display));
    lrg_paint_measure_text (input_font (self), prefix, eff_font_size (self), TEXT_SPACING, &w, NULL);
    return w + TEXT_SPACING * 0.5f;
}

/* touch: restart the caret's solid phase (typing, moving) */
static void
touch (LrgTextInput *self)
{
    self->activity_time = self->clock;
}

/*
 * ensure_caret_visible:
 *
 * Adjusts scroll_x so the caret lies inside the inner box and no more
 * text than necessary is scrolled away.
 */
static void
ensure_caret_visible (LrgTextInput *self,
                      const gchar  *display)
{
    gfloat inner_w = lrg_widget_get_width (LRG_WIDGET (self)) - eff_padding (self) * 2.0f;
    gfloat caret = offset_x (self, display, self->cursor_position);
    gfloat full = offset_x (self, display, (gint) g_utf8_strlen (display, -1));

    if (inner_w <= 0.0f)
    {
        self->scroll_x = 0.0f;
        return;
    }

    if (caret - self->scroll_x > inner_w)
        self->scroll_x = caret - inner_w;
    if (caret - self->scroll_x < 0.0f)
        self->scroll_x = caret;

    /* Never leave empty space on the right when the text shrank */
    self->scroll_x = CLAMP (self->scroll_x, 0.0f, MAX (full - inner_w, 0.0f));
}

/*
 * index_at_x:
 * @x: a world X coordinate
 *
 * The character boundary nearest to @x, accounting for scroll.
 */
static gint
index_at_x (LrgTextInput *self,
            gfloat        x)
{
    g_autofree gchar *display = get_display_text (self);
    gint              n = (gint) g_utf8_strlen (display, -1);
    gfloat            origin;
    gfloat            prev;
    gint              i;

    ensure_caret_visible (self, display);
    origin = lrg_widget_get_world_x (LRG_WIDGET (self)) + eff_padding (self) - self->scroll_x;

    /* Walk the boundaries; pick the first one past the midpoint */
    prev = 0.0f;
    for (i = 1; i <= n; i++)
    {
        gfloat cur = offset_x (self, display, i);

        if (x - origin < (prev + cur) * 0.5f)
            return i - 1;
        prev = cur;
    }
    return n;
}

/* is_word_char: letters, digits and underscore form words */
static gboolean
is_word_char (gunichar c)
{
    return g_unichar_isalnum (c) || c == '_';
}

/*
 * word_boundary:
 * @from: a character offset
 * @dir: -1 to search backwards, +1 forwards
 *
 * Ctrl+arrow target: skips spaces, then the word.
 */
static gint
word_boundary (LrgTextInput *self,
               gint          from,
               gint          dir)
{
    gint n = text_length (self);
    gint i = from;

    if (self->text == NULL || self->password_mode)
        return dir < 0 ? 0 : n;

    if (dir < 0)
    {
        while (i > 0 && !is_word_char (g_utf8_get_char (g_utf8_offset_to_pointer (self->text, i - 1))))
            i--;
        while (i > 0 && is_word_char (g_utf8_get_char (g_utf8_offset_to_pointer (self->text, i - 1))))
            i--;
    }
    else
    {
        while (i < n && !is_word_char (g_utf8_get_char (g_utf8_offset_to_pointer (self->text, i))))
            i++;
        while (i < n && is_word_char (g_utf8_get_char (g_utf8_offset_to_pointer (self->text, i))))
            i++;
    }
    return i;
}

/* ==========================================================================
 * Editing core
 * ========================================================================== */

/*
 * move_to:
 * @pos: new cursor position
 * @extend: keep the anchor (Shift held) instead of collapsing
 */
static void
move_to (LrgTextInput *self,
         gint          pos,
         gboolean      extend)
{
    gint n = text_length (self);

    pos = CLAMP (pos, 0, n);
    if (!extend)
        self->anchor = pos;
    if (self->cursor_position != pos)
    {
        self->cursor_position = pos;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
    }
    touch (self);
}

/*
 * replace_range:
 * @start: first character to replace
 * @end: one past the last character to replace
 * @insert: (nullable): UTF-8 to put there (control characters dropped)
 *
 * The single editing primitive: every insertion and deletion goes
 * through here. The insertion is cut to max-length; the cursor lands
 * after it with no selection. Emits notify::text and text-changed
 * when the text actually changed.
 */
static void
replace_range (LrgTextInput *self,
               gint          start,
               gint          end,
               const gchar  *insert)
{
    g_autoptr(GString) clean = g_string_new (NULL);
    GString           *result;
    const gchar       *base = self->text != NULL ? self->text : "";
    const gchar       *p0;
    const gchar       *p1;
    const gchar       *p;
    gint               n = text_length (self);
    gint               room = G_MAXINT;
    gint               added = 0;

    start = CLAMP (start, 0, n);
    end = CLAMP (end, start, n);

    /* 1. Filter the insertion: valid, printable, within max-length */
    if (self->max_length > 0)
        room = (gint) self->max_length - (n - (end - start));
    for (p = insert; p != NULL && *p != '\0' && added < room; p = g_utf8_next_char (p))
    {
        gunichar c = g_utf8_get_char_validated (p, -1);

        if (c == (gunichar) -1 || c == (gunichar) -2)
            break;
        if (c == 0 || g_unichar_iscntrl (c))
            continue;
        g_string_append_unichar (clean, c);
        added++;
    }

    if (start == end && added == 0)
        return;

    /* 2. Splice */
    p0 = g_utf8_offset_to_pointer (base, start);
    p1 = g_utf8_offset_to_pointer (base, end);
    result = g_string_new_len (base, p0 - base);
    g_string_append_len (result, clean->str, (gssize) clean->len);
    g_string_append (result, p1);

    g_free (self->text);
    self->text = g_string_free (result, FALSE);

    /* 3. Cursor after the insertion, nothing selected */
    self->anchor = start + added;
    if (self->cursor_position != start + added)
    {
        self->cursor_position = start + added;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
    }
    touch (self);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    g_signal_emit (self, signals[SIGNAL_TEXT_CHANGED], 0);
}

/* selection_range: ordered selection; TRUE if non-empty */
static gboolean
selection_range (LrgTextInput *self,
                 gint         *start,
                 gint         *end)
{
    gint n = text_length (self);
    gint a = CLAMP (self->anchor, 0, n);
    gint c = CLAMP (self->cursor_position, 0, n);

    *start = MIN (a, c);
    *end = MAX (a, c);
    return *start != *end;
}

/* clipboard_set / clipboard_get: system clipboard with headless fallback */
static void
clipboard_set (const gchar *text)
{
    g_free (fallback_clipboard);
    fallback_clipboard = g_strdup (text);
    if (IsWindowReady ())
        SetClipboardText (text);
}

static gchar *
clipboard_get (void)
{
    if (IsWindowReady ())
    {
        const char *sys = GetClipboardText ();

        if (sys != NULL && sys[0] != '\0')
            return g_strdup (sys);
    }
    return g_strdup (fallback_clipboard);
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * box_defaults:
 *
 * INPUT background, BORDER border (a touch brighter on hover) that
 * becomes ACCENT with a faint accent glow while focused. Explicit
 * background / border colours and radius are applied later, in
 * box_overrides().
 */
static void
box_defaults (LrgTextInput   *self,
              LrgWidgetState  state,
              LrgPartLook    *look)
{
    LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (self));
    gboolean        focused = (state & LRG_WIDGET_STATE_FOCUSED) != 0;
    gboolean        hovered = (state & LRG_WIDGET_STATE_HOVERED) != 0;
    gfloat          bw = lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH);
    const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
    GrlColor        border;
    GrlColor        glow;

    lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));

    if (focused)
        border = *accent;
    else if (hovered)
        lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
                             lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.35f, &border);
    else
        border = *lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
    lrg_part_look_set_border (look, &border, focused ? MAX (bw, 1.5f) : bw);

    lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));

    /* Soft accent halo while focused (transparent otherwise so it fades) */
    lrg_paint_color_with_alpha (accent, focused ? 0.22f : 0.0f, &glow);
    lrg_part_look_set_glow (look, &glow, 6.0f);

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_opacity (look, INPUT_DISABLED_OPACITY);
}

static void
lrg_text_input_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
    LrgTextInput *self = LRG_TEXT_INPUT (widget);
    LrgTheme     *theme = lrg_widget_get_theme (widget);
    gboolean      empty = self->text == NULL || self->text[0] == '\0';

    switch (part)
    {
    case LRG_UI_PART_BOX:
        box_defaults (self, state, look);
        break;

    case LRG_UI_PART_LABEL:
        /* Text colour, or the placeholder colour while empty */
        if (empty)
            lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
        else
            lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
        if (state & LRG_WIDGET_STATE_DISABLED)
            lrg_part_look_set_opacity (look, INPUT_DISABLED_OPACITY);
        break;

    case LRG_UI_PART_FILL:
        /* Selection highlight */
        lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
        lrg_part_look_set_radius (look, 3.0f);
        break;

    case LRG_UI_PART_MARK:
        /* Caret */
        lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
        lrg_part_look_set_size (look, CARET_WIDTH);
        break;

    default:
        LRG_WIDGET_CLASS (lrg_text_input_parent_class)->style_defaults (widget, part, state, look);
        break;
    }
}

/*
 * box_overrides:
 *
 * Explicit BOX values, applied after the theme's style rules so they
 * always win: the background colour, the radius, and the border colour
 * while unfocused (focus always shows the accent border).
 */
static void
box_overrides (LrgTextInput   *self,
               LrgWidgetState  state,
               LrgPartLook    *look)
{
    if (self->background_set)
        lrg_part_look_set_background (look, &self->background_color);
    if (self->border_set && !(state & LRG_WIDGET_STATE_FOCUSED))
        lrg_part_look_set_border (look, &self->border_color, look->border_width);
    if (self->radius_set)
        lrg_part_look_set_radius (look, self->corner_radius);
}

static void
lrg_text_input_style_overrides (LrgWidget      *widget,
                                LrgUIPart       part,
                                LrgWidgetState  state,
                                LrgPartLook    *look)
{
    LrgTextInput   *self = LRG_TEXT_INPUT (widget);
    LrgWidgetClass *parent = LRG_WIDGET_CLASS (lrg_text_input_parent_class);
    gboolean        empty = self->text == NULL || self->text[0] == '\0';

    if (parent->style_overrides != NULL)
        parent->style_overrides (widget, part, state, look);

    switch (part)
    {
    case LRG_UI_PART_BOX:
        box_overrides (self, state, look);
        break;

    case LRG_UI_PART_LABEL:
        /* Text colour, or the placeholder colour while empty */
        if (empty && self->placeholder_color_set)
            lrg_part_look_set_foreground (look, &self->placeholder_color);
        else if (!empty && self->text_color_set)
            lrg_part_look_set_foreground (look, &self->text_color);
        break;

    case LRG_UI_PART_FOCUS:
        /* The ring follows the input's own corner radius */
        if (self->radius_set)
            lrg_part_look_set_radius (look, self->corner_radius + look->border_width + 2.0f);
        break;

    default:
        break;
    }
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/*
 * caret_alpha:
 *
 * Solid right after activity, then a smooth cosine fade in and out.
 */
static gfloat
caret_alpha (LrgTextInput *self)
{
    gdouble t = self->clock - self->activity_time;
    gdouble phase;

    if (t < CARET_SOLID_TIME || lrg_motion_get_reduced ())
        return 1.0f;
    phase = fmod (t - CARET_SOLID_TIME, CARET_FADE_PERIOD) / CARET_FADE_PERIOD;
    return (gfloat) (0.5 + 0.5 * cos (2.0 * G_PI * phase));
}

/* is_focused: keyboard focus through the canvas, or legacy focus */
static gboolean
is_focused (LrgTextInput *self)
{
    return self->focused ||
           (lrg_widget_get_state_flags (LRG_WIDGET (self)) & LRG_WIDGET_STATE_FOCUSED) != 0;
}

/*
 * lrg_text_input_step:
 *
 * Advances the input's clock (caret phase, double-click timing) and
 * the focus cross-fade spring by the canvas frame time. The first step
 * snaps the spring, so an entry that starts focused does not fade in.
 *
 * The caret blink is a pure function of the clock and deliberately
 * does not report itself as animating: a focused entry must not keep
 * the canvas busy forever. Hosts that only redraw while
 * lrg_canvas_update() returns %TRUE simply show a steady caret.
 *
 * Returns: %TRUE while the focus cross-fade is running
 */
static gboolean
lrg_text_input_step (LrgWidget *widget,
                     gfloat     delta_time)
{
    LrgTextInput *self = LRG_TEXT_INPUT (widget);

    if (delta_time > 0.0f)
        self->clock += (gdouble) delta_time;

    lrg_spring_set_motion (&self->focus_anim, lrg_widget_get_motion (widget));
    lrg_spring_set_target (&self->focus_anim, is_focused (self) ? 1.0f : 0.0f);
    if (!self->stepped)
    {
        lrg_spring_snap (&self->focus_anim, self->focus_anim.target);
        self->stepped = TRUE;
        return FALSE;
    }

    return lrg_spring_step (&self->focus_anim, delta_time);
}

/*
 * lrg_text_input_paint:
 *
 * 1. Cross-fade the BOX look between its unfocused and focused
 *    variants by the focus spring (stepped in step()).
 * 2. Clip to the inner box, scroll so the caret shows.
 * 3. Selection highlight, text / placeholder, then the caret.
 */
static void
lrg_text_input_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
    LrgTextInput     *self = LRG_TEXT_INPUT (widget);
    LrgWidgetState    state = lrg_widget_get_state_flags (widget);
    gboolean          focused = is_focused (self);
    gfloat            x = lrg_widget_get_world_x (widget);
    gfloat            y = lrg_widget_get_world_y (widget);
    gfloat            w = lrg_widget_get_width (widget);
    gfloat            h = lrg_widget_get_height (widget);
    gfloat            pad = eff_padding (self);
    gfloat            size = eff_font_size (self);
    GrlFont          *font = input_font (self);
    g_autofree gchar *display = get_display_text (self);
    LrgPartLook       box;
    LrgPartLook       other;
    LrgPartLook       label;
    gfloat            tx;
    gfloat            ty;
    gint              s0;
    gint              s1;

    /* 1. Focus cross-fade. A spring that has not caught up with a focus
     * change yet (no step since) shows the current look unblended. */
    lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
    if (!lrg_spring_is_settled (&self->focus_anim) &&
        self->focus_anim.target == (focused ? 1.0f : 0.0f))
    {
        gfloat f = CLAMP (self->focus_anim.value, 0.0f, 1.0f);
        LrgWidgetState rest = state & ~(LRG_WIDGET_STATE_PRESSED);

        /* box is the look for the current focus; blend from the other */
        lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_BOX,
                                           focused ? (rest & ~LRG_WIDGET_STATE_FOCUSED)
                                                   : (rest | LRG_WIDGET_STATE_FOCUSED),
                                           &other);
        if (focused)
            lrg_part_look_lerp (&other, &box, f, &box);
        else
            lrg_part_look_lerp (&box, &other, f, &box);
    }
    lrg_widget_paint_box (widget, list, &box, x, y, w, h);

    /* 2. Inner clip and scroll */
    lrg_paint_list_push_clip (list, x + box.border_width, y + box.border_width,
                              MAX (w - box.border_width * 2.0f, 0.0f),
                              MAX (h - box.border_width * 2.0f, 0.0f));
    if (focused)
        ensure_caret_visible (self, display);
    else
        self->scroll_x = 0.0f;

    tx = x + pad - self->scroll_x;
    ty = y + (h - size) * 0.5f;

    /* 3a. Selection */
    if (focused && selection_range (self, &s0, &s1))
    {
        LrgPartLook sel;
        gfloat      a = offset_x (self, display, s0);
        gfloat      b = offset_x (self, display, s1);

        lrg_widget_resolve_look (widget, LRG_UI_PART_FILL, &sel);
        lrg_paint_list_rect (list, tx + a, ty - 2.0f, b - a, size + 4.0f,
                             sel.radii.top_left, &sel.background);
    }

    /* 3b. Text or placeholder */
    lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
    {
        const gchar *shown = display[0] != '\0' ? display : self->placeholder;

        if (shown != NULL && shown[0] != '\0')
        {
            GrlColor fg;

            lrg_paint_color_with_alpha (&label.foreground,
                                        ((gfloat) label.foreground.a / 255.0f) * label.opacity, &fg);
            lrg_paint_list_text (list, font, shown, display[0] != '\0' ? tx : x + pad, ty,
                                 size, TEXT_SPACING, &fg);
        }
    }

    /* 3c. Caret, fading in and out */
    if (focused && lrg_widget_get_enabled (widget))
    {
        LrgPartLook caret;
        GrlColor    c;
        gfloat      cx = tx + offset_x (self, display, self->cursor_position);
        gfloat      cw;

        lrg_widget_resolve_look (widget, LRG_UI_PART_MARK, &caret);
        cw = caret.size > 0.0f ? caret.size : CARET_WIDTH;
        lrg_paint_color_with_alpha (&caret.foreground,
                                    ((gfloat) caret.foreground.a / 255.0f) * caret_alpha (self), &c);
        lrg_paint_list_rect (list, cx - cw * 0.5f, ty - 1.0f, cw, size + 2.0f, cw * 0.5f, &c);
    }

    lrg_paint_list_pop_clip (list);
}

/*
 * lrg_text_input_measure:
 *
 * 200 units wide by default; tall enough for the text and padding,
 * never shorter than the theme's control height.
 */
static void
lrg_text_input_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
    LrgTextInput *self = LRG_TEXT_INPUT (widget);
    LrgTheme     *theme = lrg_widget_get_theme (widget);
    gfloat        pad_y = self->padding_set ? self->padding
                                            : lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y);

    if (preferred_width != NULL)
        *preferred_width = 200.0f;
    if (preferred_height != NULL)
        *preferred_height = MAX (eff_font_size (self) + pad_y * 2.0f,
                                 lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT));
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/*
 * handle_key:
 *
 * Editing keys while focused. Every key the entry uses returns TRUE so
 * the canvas does not also navigate (arrows) or activate (Space,
 * Enter). Up / Down are left to focus navigation.
 */
static gboolean
handle_key (LrgTextInput      *self,
            GrlKey             key,
            LrgInputModifiers  mods)
{
    gboolean shift = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;
    gboolean ctrl = (mods & LRG_INPUT_MODIFIER_CTRL) != 0;
    gint     n = text_length (self);
    gint     s0;
    gint     s1;
    gboolean has_sel = selection_range (self, &s0, &s1);

    switch (key)
    {
    case GRL_KEY_LEFT:
        if (has_sel && !shift)
            move_to (self, s0, FALSE);
        else
            move_to (self, ctrl ? word_boundary (self, self->cursor_position, -1)
                                : self->cursor_position - 1, shift);
        return TRUE;

    case GRL_KEY_RIGHT:
        if (has_sel && !shift)
            move_to (self, s1, FALSE);
        else
            move_to (self, ctrl ? word_boundary (self, self->cursor_position, 1)
                                : self->cursor_position + 1, shift);
        return TRUE;

    case GRL_KEY_HOME:
        move_to (self, 0, shift);
        return TRUE;

    case GRL_KEY_END:
        move_to (self, n, shift);
        return TRUE;

    case GRL_KEY_BACKSPACE:
        if (has_sel)
            replace_range (self, s0, s1, NULL);
        else if (self->cursor_position > 0)
            replace_range (self, ctrl ? word_boundary (self, self->cursor_position, -1)
                                      : self->cursor_position - 1,
                           self->cursor_position, NULL);
        return TRUE;

    case GRL_KEY_DELETE:
        if (has_sel)
            replace_range (self, s0, s1, NULL);
        else if (self->cursor_position < n)
            replace_range (self, self->cursor_position,
                           ctrl ? word_boundary (self, self->cursor_position, 1)
                                : self->cursor_position + 1, NULL);
        return TRUE;

    case GRL_KEY_ENTER:
    case GRL_KEY_KP_ENTER:
        g_signal_emit (self, signals[SIGNAL_SUBMITTED], 0);
        return TRUE;

    case GRL_KEY_A:
        if (ctrl)
        {
            lrg_text_input_select_all (self);
            return TRUE;
        }
        break;

    case GRL_KEY_C:
        if (ctrl)
        {
            lrg_text_input_copy_clipboard (self);
            return TRUE;
        }
        break;

    case GRL_KEY_X:
        if (ctrl)
        {
            lrg_text_input_cut_clipboard (self);
            return TRUE;
        }
        break;

    case GRL_KEY_V:
        if (ctrl)
        {
            lrg_text_input_paste_clipboard (self);
            return TRUE;
        }
        break;

    default:
        break;
    }

    /*
     * Printable keys (Space included) are swallowed so they neither
     * trigger shortcuts nor activate; the character itself arrives as
     * a separate TEXT_INPUT event carrying the composed codepoint.
     * With Ctrl, Alt or Super held (and not AltGr = Ctrl+Alt) the key
     * types nothing, so it is left to canvas shortcuts.
     */
    if (((mods & LRG_INPUT_MODIFIER_CTRL) && !(mods & LRG_INPUT_MODIFIER_ALT)) ||
        ((mods & LRG_INPUT_MODIFIER_ALT) && !(mods & LRG_INPUT_MODIFIER_CTRL)) ||
        (mods & LRG_INPUT_MODIFIER_SUPER))
        return FALSE;
    return key >= 32 && key <= 126;
}

/*
 * handle_press:
 *
 * Left press inside: place the caret (Shift extends), double click
 * selects the word under the pointer, and start a drag selection. The
 * canvas captures the pointer, so the drag keeps working outside.
 */
static void
handle_press (LrgTextInput      *self,
              gfloat             x,
              LrgInputModifiers  mods)
{
    gint index = index_at_x (self, x);

    /* Double click: a second press soon after the first (by the canvas
     * clock accumulated in step()), close to it */
    if (self->click_armed &&
        self->clock - self->last_click_time < DOUBLE_CLICK_TIME &&
        ABS (index - self->last_click_index) <= 1 &&
        fabsf (x - self->last_click_x) <= DOUBLE_CLICK_SLOP)
    {
        gint start = word_boundary (self, MIN (index + 1, text_length (self)), -1);
        gint end = word_boundary (self, start, 1);

        lrg_text_input_set_selection (self, start, end);
        self->click_armed = FALSE;
        self->dragging = FALSE;
        return;
    }

    self->click_armed = TRUE;
    self->last_click_time = self->clock;
    self->last_click_index = index;
    self->last_click_x = x;
    move_to (self, index, (mods & LRG_INPUT_MODIFIER_SHIFT) != 0);
    self->dragging = TRUE;
}

static gboolean
lrg_text_input_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
    LrgTextInput   *self = LRG_TEXT_INPUT (widget);
    LrgUIEventType  type = lrg_ui_event_get_event_type (event);
    gfloat          x = lrg_ui_event_get_x (event);
    gfloat          y = lrg_ui_event_get_y (event);
    gboolean        inside;

    switch (type)
    {
    case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
        if (lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
            break;
        inside = lrg_widget_contains_point (widget, x, y);
        if (inside)
        {
            /* Without a canvas, a click focuses directly */
            if (!self->focused && owning_canvas (self) == NULL)
                lrg_text_input_set_focused (self, TRUE);
            handle_press (self, x, lrg_ui_event_get_modifiers (event));
            return TRUE;
        }
        else if (self->focused && owning_canvas (self) == NULL)
        {
            /* Legacy: click outside to unfocus */
            lrg_text_input_set_focused (self, FALSE);
        }
        break;

    case LRG_UI_EVENT_MOUSE_MOVE:
        if (self->dragging)
        {
            move_to (self, index_at_x (self, x), TRUE);
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_MOUSE_BUTTON_UP:
        if (self->dragging && lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_LEFT)
        {
            self->dragging = FALSE;
            return TRUE;
        }
        break;

    case LRG_UI_EVENT_KEY_DOWN:
        if (!self->focused)
            break;
        return handle_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));

    case LRG_UI_EVENT_TEXT_INPUT:
        {
            gunichar          codepoint;
            gchar             buf[8];
            gint              len;
            LrgInputModifiers mods = lrg_ui_event_get_modifiers (event);

            if (!self->focused)
                return FALSE;

            /* Ctrl shortcuts are not text (AltGr = Ctrl+Alt still is) */
            if ((mods & LRG_INPUT_MODIFIER_CTRL) && !(mods & LRG_INPUT_MODIFIER_ALT))
                return FALSE;

            /* Reject control characters; everything printable,
             * including non-ASCII, is inserted as-is */
            codepoint = lrg_ui_event_get_codepoint (event);
            if (codepoint == 0 || g_unichar_iscntrl (codepoint) ||
                !g_unichar_validate (codepoint))
                return FALSE;

            len = g_unichar_to_utf8 (codepoint, buf);
            buf[len] = '\0';
            lrg_text_input_insert_text (self, buf);
            return TRUE;
        }

    case LRG_UI_EVENT_ACTIVATE:
        /* Enter / Space are editing keys; a gamepad A just keeps focus */
        return TRUE;

    case LRG_UI_EVENT_FOCUS_IN:
        if (!self->focused)
        {
            self->focused = TRUE;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED]);
        }
        touch (self);
        return TRUE;

    case LRG_UI_EVENT_FOCUS_OUT:
        self->dragging = FALSE;
        if (self->focused)
        {
            self->focused = FALSE;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED]);
        }
        return TRUE;

    default:
        break;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_text_input_finalize (GObject *object)
{
    LrgTextInput *self = LRG_TEXT_INPUT (object);

    g_free (self->text);
    g_free (self->placeholder);

    G_OBJECT_CLASS (lrg_text_input_parent_class)->finalize (object);
}

static void
lrg_text_input_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgTextInput *self = LRG_TEXT_INPUT (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        g_value_set_string (value, self->text);
        break;
    case PROP_PLACEHOLDER:
        g_value_set_string (value, self->placeholder);
        break;
    case PROP_MAX_LENGTH:
        g_value_set_uint (value, self->max_length);
        break;
    case PROP_PASSWORD_MODE:
        g_value_set_boolean (value, self->password_mode);
        break;
    case PROP_CURSOR_POSITION:
        g_value_set_int (value, self->cursor_position);
        break;
    case PROP_FOCUSED:
        g_value_set_boolean (value, self->focused);
        break;
    case PROP_FONT_SIZE:
        g_value_set_float (value, self->font_size);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &self->text_color);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, &self->background_color);
        break;
    case PROP_BORDER_COLOR:
        g_value_set_boxed (value, &self->border_color);
        break;
    case PROP_PLACEHOLDER_COLOR:
        g_value_set_boxed (value, &self->placeholder_color);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    case PROP_PADDING:
        g_value_set_float (value, self->padding);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_text_input_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgTextInput *self = LRG_TEXT_INPUT (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        lrg_text_input_set_text (self, g_value_get_string (value));
        break;
    case PROP_PLACEHOLDER:
        lrg_text_input_set_placeholder (self, g_value_get_string (value));
        break;
    case PROP_MAX_LENGTH:
        lrg_text_input_set_max_length (self, g_value_get_uint (value));
        break;
    case PROP_PASSWORD_MODE:
        lrg_text_input_set_password_mode (self, g_value_get_boolean (value));
        break;
    case PROP_CURSOR_POSITION:
        lrg_text_input_set_cursor_position (self, g_value_get_int (value));
        break;
    case PROP_FOCUSED:
        lrg_text_input_set_focused (self, g_value_get_boolean (value));
        break;
    case PROP_FONT_SIZE:
        lrg_text_input_set_font_size (self, g_value_get_float (value));
        break;
    case PROP_TEXT_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_text_input_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_BACKGROUND_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_text_input_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_text_input_set_border_color (self, g_value_get_boxed (value));
        break;
    case PROP_PLACEHOLDER_COLOR:
        if (g_value_get_boxed (value) != NULL)
            lrg_text_input_set_placeholder_color (self, g_value_get_boxed (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_text_input_set_corner_radius (self, g_value_get_float (value));
        break;
    case PROP_PADDING:
        lrg_text_input_set_padding (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_text_input_class_init (LrgTextInputClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_text_input_finalize;
    object_class->get_property = lrg_text_input_get_property;
    object_class->set_property = lrg_text_input_set_property;

    widget_class->paint = lrg_text_input_paint;
    widget_class->step = lrg_text_input_step;
    widget_class->style_defaults = lrg_text_input_style_defaults;
    widget_class->style_overrides = lrg_text_input_style_overrides;
    widget_class->measure = lrg_text_input_measure;
    widget_class->handle_event = lrg_text_input_handle_event;

    /**
     * LrgTextInput:text:
     *
     * The current input text.
     */
    properties[PROP_TEXT] =
        g_param_spec_string ("text",
                             "Text",
                             "The current input text",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:placeholder:
     *
     * Placeholder text shown (in TEXT_MUTED) when the input is empty.
     */
    properties[PROP_PLACEHOLDER] =
        g_param_spec_string ("placeholder",
                             "Placeholder",
                             "Placeholder text shown when empty",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:max-length:
     *
     * Maximum number of characters (0 = unlimited). Typing and
     * pasting stop at this length.
     */
    properties[PROP_MAX_LENGTH] =
        g_param_spec_uint ("max-length",
                           "Max Length",
                           "Maximum text length (0 = unlimited)",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:password-mode:
     *
     * Whether to mask the input with asterisks. Masked text is never
     * copied to the clipboard.
     */
    properties[PROP_PASSWORD_MODE] =
        g_param_spec_boolean ("password-mode",
                              "Password Mode",
                              "Whether to mask input",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:cursor-position:
     *
     * The cursor position in characters. Setting it clears the
     * selection.
     */
    properties[PROP_CURSOR_POSITION] =
        g_param_spec_int ("cursor-position",
                          "Cursor Position",
                          "The cursor position in characters",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:focused:
     *
     * Whether the input has keyboard focus. Inside a canvas this
     * follows the canvas focus; setting it moves the canvas focus.
     */
    properties[PROP_FOCUSED] =
        g_param_spec_boolean ("focused",
                              "Focused",
                              "Whether the input has keyboard focus",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:font-size:
     *
     * Text size. The default (20) is only reported: the input uses the
     * theme's FONT_SIZE until a size is set.
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size",
                            "Font Size",
                            "The font size",
                            1.0f, 200.0f, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:text-color:
     *
     * Text colour; the theme's TEXT until set.
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:background-color:
     *
     * Background colour; the theme's INPUT until set.
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "The background color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:border-color:
     *
     * Border colour while unfocused; the theme's BORDER until set.
     * Focus always shows the theme's ACCENT.
     */
    properties[PROP_BORDER_COLOR] =
        g_param_spec_boxed ("border-color",
                            "Border Color",
                            "The border color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:placeholder-color:
     *
     * Placeholder colour; the theme's TEXT_MUTED until set.
     */
    properties[PROP_PLACEHOLDER_COLOR] =
        g_param_spec_boxed ("placeholder-color",
                            "Placeholder Color",
                            "The placeholder text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:corner-radius:
     *
     * Corner radius. The default (4) is only reported: the input uses
     * the theme's CORNER_RADIUS until a radius is set.
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "The corner radius",
                            0.0f, 50.0f, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTextInput:padding:
     *
     * Space between the box edge and the text. The default (8) is only
     * reported: the input uses the theme's PADDING_X / PADDING_Y until
     * a padding is set.
     */
    properties[PROP_PADDING] =
        g_param_spec_float ("padding",
                            "Padding",
                            "Text padding from edges",
                            0.0f, 50.0f, 8.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTextInput::text-changed:
     * @self: the text input that emitted the signal
     *
     * Emitted when the input text changes (typing, deleting, cutting,
     * pasting or lrg_text_input_set_text()).
     */
    signals[SIGNAL_TEXT_CHANGED] =
        g_signal_new ("text-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTextInput::submitted:
     * @self: the text input that emitted the signal
     *
     * Emitted when Enter is pressed while the input is focused.
     */
    signals[SIGNAL_SUBMITTED] =
        g_signal_new ("submitted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_text_input_init (LrgTextInput *self)
{
    lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);

    self->font_size = 20.0f;
    self->text_color = DEFAULT_TEXT;
    self->background_color = DEFAULT_BACKGROUND;
    self->border_color = DEFAULT_BORDER;
    self->placeholder_color = DEFAULT_PLACEHOLDER;
    self->corner_radius = 4.0f;
    self->padding = 8.0f;
    self->last_click_index = -1;

    lrg_spring_init (&self->focus_anim, 0.0f, LRG_MOTION_SMOOTH);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

LrgTextInput *
lrg_text_input_new (void)
{
    return g_object_new (LRG_TYPE_TEXT_INPUT, NULL);
}

LrgTextInput *
lrg_text_input_new_with_placeholder (const gchar *placeholder)
{
    return g_object_new (LRG_TYPE_TEXT_INPUT,
                         "placeholder", placeholder,
                         NULL);
}

/* ==========================================================================
 * Public API - Text
 * ========================================================================== */

const gchar *
lrg_text_input_get_text (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);

    return self->text;
}

void
lrg_text_input_set_text (LrgTextInput *self,
                         const gchar  *text)
{
    gint len;

    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (g_strcmp0 (self->text, text) == 0)
        return;

    g_free (self->text);
    self->text = g_strdup (text);

    /* Keep the cursor and anchor inside the new text */
    len = text_length (self);
    self->anchor = MIN (self->anchor, len);
    if (self->cursor_position > len)
    {
        self->cursor_position = len;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_POSITION]);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    g_signal_emit (self, signals[SIGNAL_TEXT_CHANGED], 0);
}

const gchar *
lrg_text_input_get_placeholder (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);

    return self->placeholder;
}

void
lrg_text_input_set_placeholder (LrgTextInput *self,
                                const gchar  *placeholder)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (g_strcmp0 (self->placeholder, placeholder) == 0)
        return;

    g_free (self->placeholder);
    self->placeholder = g_strdup (placeholder);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER]);
}

/* ==========================================================================
 * Public API - Input Behavior
 * ========================================================================== */

guint
lrg_text_input_get_max_length (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 0);
    return self->max_length;
}

void
lrg_text_input_set_max_length (LrgTextInput *self,
                               guint         max_length)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (self->max_length == max_length)
        return;

    self->max_length = max_length;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_LENGTH]);
}

gboolean
lrg_text_input_get_password_mode (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), FALSE);
    return self->password_mode;
}

void
lrg_text_input_set_password_mode (LrgTextInput *self,
                                  gboolean      password_mode)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    password_mode = !!password_mode;
    if (self->password_mode == password_mode)
        return;

    self->password_mode = password_mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PASSWORD_MODE]);
}

/* ==========================================================================
 * Public API - Cursor and selection
 * ========================================================================== */

gint
lrg_text_input_get_cursor_position (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 0);
    return self->cursor_position;
}

void
lrg_text_input_set_cursor_position (LrgTextInput *self,
                                    gint          position)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    move_to (self, position, FALSE);
}

gboolean
lrg_text_input_get_selection (LrgTextInput *self,
                              gint         *start,
                              gint         *end)
{
    gint     s0;
    gint     s1;
    gboolean has;

    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), FALSE);

    has = selection_range (self, &s0, &s1);
    if (start != NULL)
        *start = s0;
    if (end != NULL)
        *end = s1;
    return has;
}

void
lrg_text_input_set_selection (LrgTextInput *self,
                              gint          anchor,
                              gint          cursor)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    move_to (self, anchor, FALSE);
    move_to (self, cursor, TRUE);
}

void
lrg_text_input_select_all (LrgTextInput *self)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    lrg_text_input_set_selection (self, 0, text_length (self));
}

gchar *
lrg_text_input_get_selected_text (LrgTextInput *self)
{
    gint         s0;
    gint         s1;
    const gchar *p0;
    const gchar *p1;

    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);

    if (!selection_range (self, &s0, &s1))
        return NULL;

    p0 = g_utf8_offset_to_pointer (self->text, s0);
    p1 = g_utf8_offset_to_pointer (self->text, s1);
    return g_strndup (p0, (gsize) (p1 - p0));
}

gboolean
lrg_text_input_delete_selection (LrgTextInput *self)
{
    gint s0;
    gint s1;

    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), FALSE);

    if (!selection_range (self, &s0, &s1))
        return FALSE;
    replace_range (self, s0, s1, NULL);
    return TRUE;
}

void
lrg_text_input_insert_text (LrgTextInput *self,
                            const gchar  *text)
{
    gint s0;
    gint s1;

    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (text != NULL);

    selection_range (self, &s0, &s1);
    replace_range (self, s0, s1, text);
}

void
lrg_text_input_copy_clipboard (LrgTextInput *self)
{
    g_autofree gchar *sel = NULL;

    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (self->password_mode)
        return;
    sel = lrg_text_input_get_selected_text (self);
    if (sel != NULL)
        clipboard_set (sel);
}

void
lrg_text_input_cut_clipboard (LrgTextInput *self)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    if (self->password_mode)
        return;
    lrg_text_input_copy_clipboard (self);
    lrg_text_input_delete_selection (self);
}

void
lrg_text_input_paste_clipboard (LrgTextInput *self)
{
    g_autofree gchar *clip = NULL;

    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    clip = clipboard_get ();
    if (clip != NULL && clip[0] != '\0')
        lrg_text_input_insert_text (self, clip);
}

/* ==========================================================================
 * Public API - Focus
 * ========================================================================== */

gboolean
lrg_text_input_get_focused (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), FALSE);
    return self->focused;
}

void
lrg_text_input_set_focused (LrgTextInput *self,
                            gboolean      focused)
{
    LrgCanvas *canvas;

    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    focused = !!focused;
    if (self->focused == focused)
        return;

    /* Inside a canvas, move the canvas focus; FOCUS_IN / FOCUS_OUT
     * then update our own flag */
    canvas = owning_canvas (self);
    if (canvas != NULL)
    {
        if (focused)
            lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (self));
        else if (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (self))
            lrg_canvas_set_focused_widget (canvas, NULL);
    }

    if (self->focused != focused)
    {
        self->focused = focused;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED]);
    }
    touch (self);
}

/* ==========================================================================
 * Public API - Appearance
 * ========================================================================== */

gfloat
lrg_text_input_get_font_size (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 20.0f);
    return self->font_size;
}

void
lrg_text_input_set_font_size (LrgTextInput *self,
                              gfloat        size)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (size >= 1.0f);

    self->font_size_set = TRUE;
    lrg_widget_queue_layout (LRG_WIDGET (self));
    if (self->font_size == size)
        return;

    self->font_size = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
}

const GrlColor *
lrg_text_input_get_text_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->text_color;
}

void
lrg_text_input_set_text_color (LrgTextInput   *self,
                               const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    self->text_color_set = TRUE;
    if (memcmp (&self->text_color, color, sizeof (GrlColor)) == 0)
        return;

    self->text_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

const GrlColor *
lrg_text_input_get_background_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->background_color;
}

void
lrg_text_input_set_background_color (LrgTextInput   *self,
                                     const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    self->background_set = TRUE;
    if (memcmp (&self->background_color, color, sizeof (GrlColor)) == 0)
        return;

    self->background_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

const GrlColor *
lrg_text_input_get_border_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->border_color;
}

void
lrg_text_input_set_border_color (LrgTextInput   *self,
                                 const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    self->border_set = TRUE;
    if (memcmp (&self->border_color, color, sizeof (GrlColor)) == 0)
        return;

    self->border_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
}

const GrlColor *
lrg_text_input_get_placeholder_color (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), NULL);
    return &self->placeholder_color;
}

void
lrg_text_input_set_placeholder_color (LrgTextInput   *self,
                                      const GrlColor *color)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));
    g_return_if_fail (color != NULL);

    self->placeholder_color_set = TRUE;
    if (memcmp (&self->placeholder_color, color, sizeof (GrlColor)) == 0)
        return;

    self->placeholder_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER_COLOR]);
}

gfloat
lrg_text_input_get_corner_radius (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 4.0f);
    return self->corner_radius;
}

void
lrg_text_input_set_corner_radius (LrgTextInput *self,
                                  gfloat        radius)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    self->radius_set = TRUE;
    if (self->corner_radius == radius)
        return;

    self->corner_radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
}

gfloat
lrg_text_input_get_padding (LrgTextInput *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_INPUT (self), 8.0f);
    return self->padding;
}

void
lrg_text_input_set_padding (LrgTextInput *self,
                            gfloat        padding)
{
    g_return_if_fail (LRG_IS_TEXT_INPUT (self));

    self->padding_set = TRUE;
    lrg_widget_queue_layout (LRG_WIDGET (self));
    if (self->padding == padding)
        return;

    self->padding = padding;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING]);
}
