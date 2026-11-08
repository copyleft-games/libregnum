/* lrg-text-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTextEditor: a multi-line, syntax highlighted text editor for
 * scripts, consoles and chat boxes.
 *
 * Model
 *   The text is an array of lines (GString each, no newline). Positions
 *   are (line, byte offset) pairs and always sit on a UTF-8 character
 *   boundary; the public API speaks in character columns. Every change
 *   goes through buffer_replace(), which also records undo edits.
 *
 * Highlighting
 *   Each line caches the syntax state it started in, the state it ended
 *   in and (once painted) its spans. valid_upto is the first line whose
 *   cached entry state is not known to be right. Painting walks from
 *   there to the last visible line: a line that is clean and starts in
 *   the same state as before is skipped (its cached end state is still
 *   right), anything else is re-tokenized. So an edit costs the edited
 *   line plus the following lines until the state converges, and lines
 *   far below the view are only touched when they scroll into view.
 *   Lines above the view are tokenized without spans (state only).
 *
 * Layout
 *   The editor is a monospace grid: a character is one cell (two for
 *   wide characters, tabs reach the next tab stop). With wrapping on, a
 *   line is split into rows at the last space that fits (or mid-word
 *   when a word is wider than the view); each line caches its row
 *   breaks, invalidated by a serial number when the wrap width or tab
 *   width changes, and a prefix sum maps rows to lines.
 *
 * View
 *   Two spring-smoothed scroll axes with overlay scrollbars (shared
 *   with LrgScrollBox / LrgListView), a caret that glides to its new
 *   cell on springs and blinks on the canvas clock, current-line tint,
 *   selection, find matches, bracket matching and a line-number gutter.
 *   Only visible rows are tokenized and painted, so documents of tens
 *   of thousands of lines paint in constant time.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-text-editor.h"
#include "lrg-scrollbar-private.h"
#include "../text/lrg-font-manager.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Tunables
 * ========================================================================== */

/* Row height as a multiple of the font size */
#define LINE_SPACING (1.45f)

/* Padding between the frame and the text, horizontal / vertical */
#define PAD_X (8.0f)
#define PAD_Y (6.0f)

/* Space either side of the line numbers */
#define GUTTER_PAD (10.0f)

/* Width of the breakpoint column, only when show-gutter-marks is on */
#define MARKER_W (14.0f)

/* Caret thickness */
#define CARET_WIDTH (2.0f)

/* Seconds the caret stays solid after activity, then one blink period */
#define CARET_SOLID (0.6)
#define CARET_PERIOD (1.1)

/* Presses within this many seconds (canvas clock) and pixels count as
 * double / triple clicks */
#define MULTI_CLICK_TIME (0.4)
#define MULTI_CLICK_SLOP (6.0f)

/* Typing runs longer apart than this start a new undo step */
#define COALESCE_TIME (1.5)

/* Undo steps kept */
#define UNDO_LIMIT (500)

/* Bytes scanned looking for a matching bracket */
#define BRACKET_SCAN_LIMIT (200000)

/* Rows the preferred height grows to before it stops following the text */
#define NATURAL_ROWS (12u)
#define NATURAL_WIDTH (360.0f)

/* Wrapping never squeezes a row below this many columns */
#define MIN_WRAP_COLS (8u)

/* ==========================================================================
 * Types
 * ========================================================================== */

typedef struct
{
	guint line;
	guint byte;
} Pos;

typedef struct
{
	GString  *text;
	GArray   *spans;          /* LrgSyntaxSpan, NULL until painted */
	guint     state_in;
	guint     state_out;
	gboolean  dirty;          /* text changed since last tokenized */
	guint     layout_serial;  /* layout below valid when == editor's */
	guint     cols;           /* display columns of the whole line */
	GArray   *breaks;         /* guint byte offsets where rows 2.. start */
} Line;

typedef enum
{
	STEP_OTHER,
	STEP_TYPE,
	STEP_BACKSPACE,
	STEP_DELETE
} StepKind;

typedef struct
{
	Pos    start;
	gchar *removed;
	gchar *inserted;
} Edit;

typedef struct
{
	GArray   *edits;          /* Edit */
	StepKind  kind;
	Pos       cursor_before;
	Pos       anchor_before;
	Pos       cursor_after;
	Pos       anchor_after;
} Step;

typedef enum
{
	DRAG_NONE,
	DRAG_CHAR,
	DRAG_WORD,
	DRAG_LINE
} DragMode;

/* Geometry of the current allocation, all relative to the widget */
typedef struct
{
	GrlFont *font;
	gfloat   size;       /* font size */
	gfloat   cell;       /* column width */
	gfloat   line_h;     /* row height */
	gfloat   border;     /* frame border width */
	gfloat   gutter;     /* gutter width (0 without line numbers) */
	gfloat   vx;         /* text viewport */
	gfloat   vy;
	gfloat   vw;
	gfloat   vh;
} Geo;

struct _LrgTextEditor
{
	LrgContainer   parent_instance;

	/* Text */
	GPtrArray     *lines;          /* Line* */
	Pos            cursor;
	Pos            anchor;
	gfloat         goal_x;         /* remembered x for vertical moves, < 0 = none */
	guint          edit_serial;    /* bumped on every text change */

	/* Highlighting */
	LrgSyntax     *syntax;
	gchar         *language;
	guint          valid_upto;
	guint          tokenize_count;
	GrlColor       token_colors[LRG_SYNTAX_N_TOKENS];
	guint32        token_colors_set;

	/* Layout */
	guint          layout_serial;
	guint          wrap_cols;      /* columns per row when wrapping */
	GArray        *row_starts;     /* guint: first row of each line (wrap) */
	guint          n_rows;
	gboolean       rows_valid;
	guint          max_cols;
	gboolean       max_cols_valid;
	GrlFont       *cell_font;      /* cell width cache key */
	gfloat         cell_size;
	gfloat         cell_width;

	/* Undo */
	GPtrArray     *undo;           /* Step* */
	GPtrArray     *redo;           /* Step* */
	gboolean       break_coalesce;
	gdouble        last_edit_time;
	Step          *open_step;      /* step being recorded, owned by undo */
	gboolean       replaying;

	/* Change notification */
	gboolean       text_changed;
	Pos            notified_cursor;
	guint          natural_rows;

	/* View */
	LrgScrollAxis  ax;             /* ax.bar owned */
	LrgScrollAxis  ay;             /* ay.bar owned */
	LrgScrollFx    fx;
	LrgSpring      caret_x;
	LrgSpring      caret_y;
	gboolean       reveal_pending;
	gboolean       stepped;
	gboolean       in_layout;
	gdouble        clock;
	gdouble        activity_time;

	/* Mouse */
	DragMode       drag;
	Pos            drag_start;     /* word / line anchor range */
	Pos            drag_end;
	guint          clicks;
	gdouble        last_click_time;
	gfloat         last_click_x;
	gfloat         last_click_y;

	/* Find */
	gchar         *needle;
	gboolean       needle_case;
	GString       *needle_fold;    /* case-folded needle (insensitive find) */
	Line          *fold_line;      /* line whose fold is cached below */
	guint          fold_serial;    /* edit_serial the fold was made at */
	GString       *fold_text;      /* case-folded copy of fold_line */
	GArray        *fold_map;       /* guint per fold byte: source char byte */

	/* Bracket match cache */
	guint          bracket_serial;
	Pos            bracket_cursor;
	gboolean       bracket_found;
	Pos            bracket_a;
	Pos            bracket_b;
	GArray        *bracket_spans;  /* scratch spans for off-screen lines */

	/* Properties */
	gboolean       show_line_numbers;
	gboolean       wrap;
	gboolean       read_only;
	guint          tab_width;
	gboolean       insert_spaces;
	gchar         *placeholder;
	gboolean       submit_on_enter;
	guint          max_lines;
	gchar         *font;
	gfloat         font_size;
	gboolean       highlight_line;
	gboolean       auto_indent;

	/* Gutter marks, diagnostics, completion, hover (0-based lines) */
	gboolean       show_gutter_marks;
	GHashTable    *gutter_marks;   /* GUINT line -> flags */
	gint           execution_line; /* -1 = none */
	GPtrArray     *diagnostics;    /* LrgTextDiagnostic* */
	GPtrArray     *completions;    /* LrgTextCompletion* */
	gint           completion_selected;
	gboolean       completion_visible;
	gboolean       completion_hold; /* accept() is inserting; keep the popup */
	gchar         *hover_text;

	GString       *scratch;        /* painting buffer */
};

G_DEFINE_FINAL_TYPE (LrgTextEditor, lrg_text_editor, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_TEXT,
	PROP_LANGUAGE,
	PROP_SYNTAX,
	PROP_SHOW_LINE_NUMBERS,
	PROP_WRAP,
	PROP_READ_ONLY,
	PROP_TAB_WIDTH,
	PROP_INSERT_SPACES,
	PROP_PLACEHOLDER,
	PROP_SUBMIT_ON_ENTER,
	PROP_MAX_LINES,
	PROP_FONT,
	PROP_FONT_SIZE,
	PROP_HIGHLIGHT_CURRENT_LINE,
	PROP_AUTO_INDENT,
	PROP_SHOW_GUTTER_MARKS,
	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	SIGNAL_CURSOR_MOVED,
	SIGNAL_SUBMIT,
	SIGNAL_GUTTER_CLICKED,
	SIGNAL_GOTO_DEFINITION,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* Clipboard used when there is no window (tests, headless tools) */
static gchar *fallback_clipboard = NULL;

/* ==========================================================================
 * Lines and positions
 * ========================================================================== */

static Line *
line_new (const gchar *text,
          gssize       len)
{
	Line *l = g_new0 (Line, 1);

	l->text = g_string_new_len (text != NULL ? text : "", text != NULL ? len : 0);
	l->dirty = TRUE;
	return l;
}

static void
line_free (gpointer data)
{
	Line *l = data;

	g_string_free (l->text, TRUE);
	g_clear_pointer (&l->spans, g_array_unref);
	g_clear_pointer (&l->breaks, g_array_unref);
	g_free (l);
}

static inline Line *
get_line (LrgTextEditor *self,
          guint          i)
{
	return g_ptr_array_index (self->lines, i);
}

static inline guint
n_lines (LrgTextEditor *self)
{
	return self->lines->len;
}

static gint
pos_cmp (Pos a,
         Pos b)
{
	if (a.line != b.line)
		return a.line < b.line ? -1 : 1;
	if (a.byte != b.byte)
		return a.byte < b.byte ? -1 : 1;
	return 0;
}

static inline Pos
pos_make (guint line,
          guint byte)
{
	Pos p;

	p.line = line;
	p.byte = byte;
	return p;
}

static Pos
doc_end (LrgTextEditor *self)
{
	guint last = n_lines (self) - 1;

	return pos_make (last, (guint) get_line (self, last)->text->len);
}

/* clamp_pos: inside the document and on a character boundary */
static Pos
clamp_pos (LrgTextEditor *self,
           Pos            p)
{
	Line        *l;
	const gchar *s;

	p.line = MIN (p.line, n_lines (self) - 1);
	l = get_line (self, p.line);
	p.byte = MIN (p.byte, (guint) l->text->len);
	s = l->text->str;
	while (p.byte > 0 && (((guchar) s[p.byte]) & 0xC0) == 0x80)
		p.byte--;
	return p;
}

/* char_to_byte / byte_to_char: character columns <-> byte offsets */
static guint
char_to_byte (Line  *l,
              guint  column)
{
	const gchar *s = l->text->str;
	const gchar *end = s + l->text->len;
	const gchar *p = s;

	while (column > 0 && p < end)
	{
		p = g_utf8_next_char (p);
		column--;
	}
	return (guint) (MIN (p, end) - s);
}

static guint
byte_to_char (Line  *l,
              guint  byte)
{
	return (guint) g_utf8_pointer_to_offset (l->text->str, l->text->str + MIN (byte, (guint) l->text->len));
}

/* prev_byte / next_byte: neighbouring character boundary in a line */
static guint
prev_byte (Line  *l,
           guint  byte)
{
	const gchar *s = l->text->str;

	if (byte == 0)
		return 0;
	return (guint) (g_utf8_prev_char (s + byte) - s);
}

static guint
next_byte (Line  *l,
           guint  byte)
{
	const gchar *s = l->text->str;

	if (byte >= l->text->len)
		return (guint) l->text->len;
	return (guint) (g_utf8_next_char (s + byte) - s);
}

/*
 * normalize_text:
 *
 * Text from outside (set_text, paste, typing): valid UTF-8, line ends
 * as '\n' only.
 */
static gchar *
normalize_text (const gchar *text)
{
	g_autofree gchar *valid = g_utf8_make_valid (text != NULL ? text : "", -1);
	GString          *out = g_string_sized_new (strlen (valid));
	const gchar      *p;

	for (p = valid; *p != '\0'; p++)
	{
		if (*p == '\r')
		{
			g_string_append_c (out, '\n');
			if (p[1] == '\n')
				p++;
		}
		else
			g_string_append_c (out, *p);
	}
	return g_string_free (out, FALSE);
}

/* pos_after: where @text ends when inserted at @start */
static Pos
pos_after (Pos          start,
           const gchar *text)
{
	const gchar *nl = strrchr (text, '\n');
	const gchar *p;
	guint        breaks = 0;

	if (nl == NULL)
		return pos_make (start.line, start.byte + (guint) strlen (text));
	for (p = text; *p != '\0'; p++)
		if (*p == '\n')
			breaks++;
	return pos_make (start.line + breaks, (guint) strlen (nl + 1));
}

/* range_text: the text between @a and @b (a <= b), lines joined with '\n' */
static gchar *
range_text (LrgTextEditor *self,
            Pos            a,
            Pos            b)
{
	GString *out;
	guint    i;

	if (a.line == b.line)
		return g_strndup (get_line (self, a.line)->text->str + a.byte, b.byte - a.byte);

	out = g_string_new (get_line (self, a.line)->text->str + a.byte);
	for (i = a.line + 1; i < b.line; i++)
	{
		g_string_append_c (out, '\n');
		g_string_append_len (out, get_line (self, i)->text->str, (gssize) get_line (self, i)->text->len);
	}
	g_string_append_c (out, '\n');
	g_string_append_len (out, get_line (self, b.line)->text->str, b.byte);
	return g_string_free (out, FALSE);
}

/* ==========================================================================
 * Fonts and metrics
 * ========================================================================== */

static const gchar * const mono_files[] = {
	"/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Regular.ttf",
	"/usr/share/fonts/jetbrains-mono-fonts/JetBrainsMono-Regular.ttf",
	"/usr/share/fonts/adobe-source-code-pro-fonts/SourceCodePro-Regular.otf",
	"/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
	"/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
	"/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
	NULL
};

/* One search for the "ui-mono" face per process (shared with markdown) */
static gboolean mono_attempted = FALSE;

/*
 * editor_font:
 *
 * The font registered under the editor's font name. For "ui-mono" (the
 * default) the system monospace files are tried once when a window is
 * open, registering the face for everyone (LrgMarkdownLabel uses the
 * same name). Falls back to the theme's UI font.
 */
static GrlFont *
editor_font (LrgTextEditor *self)
{
	LrgFontManager *mgr = lrg_font_manager_get_default ();
	const gchar    *name = self->font != NULL ? self->font : "ui-mono";
	GrlFont        *font = lrg_font_manager_get_font (mgr, name);
	guint           i;

	if (font == NULL && g_strcmp0 (name, "ui-mono") == 0 && !mono_attempted && IsWindowReady ())
	{
		mono_attempted = TRUE;
		for (i = 0; mono_files[i] != NULL && font == NULL; i++)
		{
			Font *handle;

			if (!g_file_test (mono_files[i], G_FILE_TEST_IS_REGULAR))
				continue;
			if (!lrg_font_manager_load_font (mgr, name, mono_files[i], 64, NULL))
				continue;
			font = lrg_font_manager_get_font (mgr, name);
			handle = font != NULL ? grl_font_get_handle (font) : NULL;
			if (handle != NULL && handle->texture.id != 0)
			{
				GenTextureMipmaps (&handle->texture);
				SetTextureFilter (handle->texture, TEXTURE_FILTER_TRILINEAR);
			}
		}
	}
	if (font == NULL)
		font = lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
	return font;
}

static gfloat
font_size (LrgTextEditor *self)
{
	if (self->font_size > 0.0f)
		return self->font_size;
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_FONT_SIZE);
}

/* cell_width: advance of one column, measured once per font and size */
static gfloat
cell_width (LrgTextEditor *self,
            GrlFont       *font,
            gfloat         size)
{
	if (self->cell_width <= 0.0f || self->cell_font != font || self->cell_size != size)
	{
		gfloat w = 0.0f;

		lrg_paint_measure_text (font, "0000000000", size, 0.0f, &w, NULL);
		self->cell_width = w > 0.0f ? w / 10.0f : size * 0.6f;
		self->cell_font = font;
		self->cell_size = size;
	}
	return self->cell_width;
}

/* char_cols: columns a character takes at column @col */
static guint
char_cols (LrgTextEditor *self,
           gunichar       ch,
           guint          col)
{
	if (ch == '\t')
		return self->tab_width - (col % self->tab_width);
	if (g_unichar_iszerowidth (ch))
		return 0;
	if (g_unichar_iswide (ch))
		return 2;
	return 1;
}

/* col_at: display column of byte @byte in @l (tabs expanded) */
static guint
col_at (LrgTextEditor *self,
        Line          *l,
        guint          byte)
{
	const gchar *s = l->text->str;
	const gchar *end = s + MIN (byte, (guint) l->text->len);
	const gchar *p;
	guint        col = 0;

	for (p = s; p < end; p = g_utf8_next_char (p))
		col += char_cols (self, g_utf8_get_char (p), col);
	return col;
}

static gint
digits (guint n)
{
	gint d = 1;

	while (n >= 10)
	{
		n /= 10;
		d++;
	}
	return d;
}

/*
 * geometry:
 *
 * Font, metrics and the text viewport for the current allocation. A
 * change of the wrap width (or font) invalidates every line's layout.
 */
static void
geometry (LrgTextEditor *self,
          Geo           *g)
{
	LrgWidget *w = LRG_WIDGET (self);
	LrgTheme  *theme = lrg_widget_get_theme (w);
	guint      cols;

	g->font = editor_font (self);
	g->size = font_size (self);
	g->cell = cell_width (self, g->font, g->size);
	g->line_h = roundf (g->size * LINE_SPACING);
	g->border = lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH);
	g->gutter = 0.0f;
	if (self->show_line_numbers)
		g->gutter += roundf ((gfloat) MAX (3, digits (n_lines (self))) * g->cell + GUTTER_PAD * 2.0f);
	if (self->show_gutter_marks)
		g->gutter += MARKER_W;
	g->vx = g->border + g->gutter;
	g->vy = g->border;
	g->vw = MAX (0.0f, lrg_widget_get_width (w) - g->border * 2.0f - g->gutter);
	g->vh = MAX (0.0f, lrg_widget_get_height (w) - g->border * 2.0f);

	/* Columns per row when wrapping (the scrollbar overlays the text) */
	cols = (guint) MAX (0.0f, floorf ((g->vw - PAD_X * 2.0f - CARET_WIDTH) / g->cell));
	cols = MAX (cols, MIN_WRAP_COLS);
	if (cols != self->wrap_cols)
	{
		self->wrap_cols = cols;
		if (self->wrap)
		{
			self->layout_serial++;
			self->rows_valid = FALSE;
		}
	}
}

/* ==========================================================================
 * Row layout
 * ========================================================================== */

/*
 * layout_line:
 *
 * Display columns of the line and, when wrapping, the byte offsets
 * where its rows 2.. start. A row breaks after the last space or tab
 * that fits; a word longer than a row breaks where it overflows.
 */
static void
layout_line (LrgTextEditor *self,
             Line          *l)
{
	const gchar *s = l->text->str;
	const gchar *end = s + l->text->len;
	const gchar *p;
	guint        wrap = self->wrap ? self->wrap_cols : 0;
	guint        col = 0;
	guint        row_col = 0;
	guint        row_byte = 0;
	guint        cand_byte = 0;
	guint        cand_col = 0;

	if (l->layout_serial == self->layout_serial)
		return;
	l->layout_serial = self->layout_serial;
	if (l->breaks != NULL)
		g_array_set_size (l->breaks, 0);

	for (p = s; p < end; p = g_utf8_next_char (p))
	{
		gunichar ch = g_utf8_get_char (p);
		guint    w = char_cols (self, ch, col);
		guint    byte = (guint) (p - s);

		if (wrap > 0 && col + w - row_col > wrap && byte > row_byte)
		{
			guint brk_byte = byte;
			guint brk_col = col;

			if (cand_byte > row_byte)
			{
				brk_byte = cand_byte;
				brk_col = cand_col;
			}
			if (l->breaks == NULL)
				l->breaks = g_array_new (FALSE, FALSE, sizeof (guint));
			g_array_append_val (l->breaks, brk_byte);
			row_byte = brk_byte;
			row_col = brk_col;
			cand_byte = row_byte;
		}
		col += w;
		if (ch == ' ' || ch == '\t')
		{
			cand_byte = (guint) (g_utf8_next_char (p) - s);
			cand_col = col;
		}
	}
	l->cols = col;
}

static guint
line_rows (Line *l)
{
	return 1 + (l->breaks != NULL ? l->breaks->len : 0);
}

/* row_bounds: bytes of row @r of @l */
static void
row_bounds (Line  *l,
            guint  r,
            guint *start,
            guint *end)
{
	*start = 0;
	*end = (guint) l->text->len;
	if (l->breaks == NULL)
		return;
	r = MIN (r, l->breaks->len);
	if (r > 0)
		*start = g_array_index (l->breaks, guint, r - 1);
	if (r < l->breaks->len)
		*end = g_array_index (l->breaks, guint, r);
}

/* row_in_line: the row of @l holding @byte (a break offset starts the next row) */
static guint
row_in_line (Line  *l,
             guint  byte)
{
	guint r = 0;

	if (l->breaks == NULL)
		return 0;
	while (r < l->breaks->len && byte >= g_array_index (l->breaks, guint, r))
		r++;
	return r;
}

/*
 * ensure_rows:
 *
 * Lays out dirty lines and rebuilds the line -> first row prefix sum
 * (wrap only; without wrapping a row is a line) and the widest line.
 */
static void
ensure_rows (LrgTextEditor *self)
{
	guint i;
	guint n = n_lines (self);

	if (self->rows_valid && self->max_cols_valid)
		return;

	if (!self->wrap)
	{
		self->n_rows = n;
		if (!self->max_cols_valid)
		{
			self->max_cols = 0;
			for (i = 0; i < n; i++)
			{
				Line *l = get_line (self, i);

				layout_line (self, l);
				self->max_cols = MAX (self->max_cols, l->cols);
			}
		}
	}
	else
	{
		guint row = 0;

		g_array_set_size (self->row_starts, n);
		self->max_cols = 0;
		for (i = 0; i < n; i++)
		{
			Line *l = get_line (self, i);

			layout_line (self, l);
			g_array_index (self->row_starts, guint, i) = row;
			row += line_rows (l);
		}
		self->n_rows = row;
	}
	self->rows_valid = TRUE;
	self->max_cols_valid = TRUE;
}

/* first_row: the first visual row of line @i */
static guint
first_row (LrgTextEditor *self,
           guint          i)
{
	ensure_rows (self);
	return self->wrap ? g_array_index (self->row_starts, guint, i) : i;
}

/* row_of: the visual row of a position */
static guint
row_of (LrgTextEditor *self,
        Pos            p)
{
	Line *l = get_line (self, p.line);

	ensure_rows (self);
	layout_line (self, l);
	return first_row (self, p.line) + row_in_line (l, p.byte);
}

/* line_of_row: the line holding visual row @row and the row inside it */
static guint
line_of_row (LrgTextEditor *self,
             guint          row,
             guint         *sub)
{
	guint lo = 0;
	guint hi;

	ensure_rows (self);
	if (!self->wrap)
	{
		*sub = 0;
		return MIN (row, n_lines (self) - 1);
	}
	row = MIN (row, self->n_rows - 1);

	/* last line whose first row <= row */
	hi = n_lines (self) - 1;
	while (lo < hi)
	{
		guint mid = (lo + hi + 1) / 2;

		if (g_array_index (self->row_starts, guint, mid) <= row)
			lo = mid;
		else
			hi = mid - 1;
	}
	*sub = row - g_array_index (self->row_starts, guint, lo);
	return lo;
}

/* x_in_row: x of byte @byte from the start of its row, in columns */
static guint
cols_in_row (LrgTextEditor *self,
             Line          *l,
             guint          byte)
{
	guint rs;
	guint re;

	row_bounds (l, row_in_line (l, byte), &rs, &re);
	return col_at (self, l, byte) - col_at (self, l, rs);
}

/*
 * byte_at_x:
 * @x: offset from the row start in canvas units
 *
 * The character boundary in row @r of @l nearest to @x.
 */
static guint
byte_at_x (LrgTextEditor *self,
           Line          *l,
           guint          r,
           gfloat         x,
           gfloat         cell)
{
	const gchar *s = l->text->str;
	guint        rs;
	guint        re;
	guint        col;
	guint        base;
	guint        b;

	row_bounds (l, r, &rs, &re);
	base = col_at (self, l, rs);
	col = base;
	b = rs;
	while (b < re)
	{
		guint w = char_cols (self, g_utf8_get_char (s + b), col);

		if ((gfloat) (col - base) * cell + (gfloat) w * cell * 0.5f > x)
			break;
		col += w;
		b = next_byte (l, b);
	}
	/* a wrapped row ends before its break (which starts the next row) */
	if (b == re && re < l->text->len && b > rs)
		b = prev_byte (l, b);
	return b;
}

/* ==========================================================================
 * Highlighting
 * ========================================================================== */

/*
 * ensure_states:
 * @upto: last line that must have a correct entry state
 * @spans_from: lines from here on are tokenized with spans
 *
 * Walks from valid_upto: clean lines entering in their cached state
 * are kept (their end state still holds); other lines are tokenized,
 * with spans when they are at or after @spans_from (the visible ones),
 * else state only. Painted lines that already have spans and did not
 * change are never tokenized again.
 */
static void
ensure_states (LrgTextEditor *self,
               guint          upto,
               guint          spans_from)
{
	guint i;
	guint state;
	guint n = n_lines (self);

	if (self->syntax == NULL)
		return;

	upto = MIN (upto, n - 1);
	i = MIN (self->valid_upto, n);
	state = i == 0 ? 0 : get_line (self, i - 1)->state_out;

	for (; i <= upto; i++)
	{
		Line *l = get_line (self, i);

		if (!l->dirty && l->state_in == state)
		{
			state = l->state_out;
			continue;
		}

		l->state_in = state;
		l->dirty = FALSE;
		if (i >= spans_from)
		{
			if (l->spans == NULL)
				l->spans = g_array_new (FALSE, FALSE, sizeof (LrgSyntaxSpan));
			g_array_set_size (l->spans, 0);
			l->state_out = lrg_syntax_tokenize_line (self->syntax, l->text->str, (gssize) l->text->len,
			                                         state, l->spans);
			self->tokenize_count++;
		}
		else
		{
			g_clear_pointer (&l->spans, g_array_unref);
			l->state_out = lrg_syntax_tokenize_line (self->syntax, l->text->str, (gssize) l->text->len,
			                                         state, NULL);
		}
		state = l->state_out;
	}
	self->valid_upto = MAX (self->valid_upto, upto + 1);
}

/* line_spans: the spans of a line whose state is valid (tokenized on demand) */
static GArray *
line_spans (LrgTextEditor *self,
            guint          i)
{
	Line *l = get_line (self, i);

	if (self->syntax == NULL)
		return NULL;
	if (l->spans == NULL)
	{
		l->spans = g_array_new (FALSE, FALSE, sizeof (LrgSyntaxSpan));
		l->state_out = lrg_syntax_tokenize_line (self->syntax, l->text->str, (gssize) l->text->len,
		                                         l->state_in, l->spans);
		self->tokenize_count++;
	}
	return l->spans;
}

/* reset_highlight: forget every cached state (language changed) */
static void
reset_highlight (LrgTextEditor *self)
{
	guint i;

	for (i = 0; i < n_lines (self); i++)
	{
		Line *l = get_line (self, i);

		l->dirty = TRUE;
		g_clear_pointer (&l->spans, g_array_unref);
	}
	self->valid_upto = 0;
}

/* ==========================================================================
 * Buffer editing
 * ========================================================================== */

/* invalidate_view: text changed between lines @from and on */
static void
invalidate (LrgTextEditor *self,
            guint          from)
{
	self->valid_upto = MIN (self->valid_upto, from);
	self->rows_valid = FALSE;
	self->max_cols_valid = FALSE;
	self->edit_serial++;
	self->text_changed = TRUE;
}

/*
 * buffer_replace:
 * @a: start (a <= b)
 * @b: end
 * @text: normalised replacement ('\n' separated)
 *
 * The one primitive every change goes through: removes [a, b), inserts
 * @text, keeps line caches coherent (the edited line and new lines are
 * dirty, the highlight walk restarts at @a's line) and, unless an undo
 * is being replayed, records the edit in the open undo step.
 *
 * Returns: the position after the inserted text
 */
static Pos
buffer_replace (LrgTextEditor *self,
                Pos            a,
                Pos            b,
                const gchar   *text)
{
	g_autofree gchar *suffix = NULL;
	g_autofree gchar *removed = NULL;
	Line             *la;
	const gchar      *piece;
	const gchar      *nl;
	guint             at;
	Pos               end;

	if (pos_cmp (a, b) > 0)
	{
		Pos t = a;

		a = b;
		b = t;
	}
	if (pos_cmp (a, b) == 0 && (text == NULL || text[0] == '\0'))
		return a;
	if (text == NULL)
		text = "";

	if (self->open_step != NULL && !self->replaying)
		removed = range_text (self, a, b);

	/* 1. Cut: keep the tail of the last line, drop the lines between */
	la = get_line (self, a.line);
	suffix = g_strdup (get_line (self, b.line)->text->str + b.byte);
	g_string_truncate (la->text, a.byte);
	if (b.line > a.line)
		g_ptr_array_remove_range (self->lines, a.line + 1, b.line - a.line);

	/* 2. Insert: first piece onto line a, further pieces as new lines */
	at = a.line;
	piece = text;
	nl = strchr (piece, '\n');
	g_string_append_len (la->text, piece, nl != NULL ? nl - piece : (gssize) strlen (piece));
	while (nl != NULL)
	{
		piece = nl + 1;
		nl = strchr (piece, '\n');
		at++;
		g_ptr_array_insert (self->lines, (gint) at,
		                    line_new (piece, nl != NULL ? nl - piece : (gssize) strlen (piece)));
	}
	end = pos_make (at, (guint) get_line (self, at)->text->len);
	g_string_append (get_line (self, at)->text, suffix);

	la->dirty = TRUE;
	la->layout_serial = 0;
	invalidate (self, a.line);

	/* 3. Undo record, merged with the previous edit of a typing run */
	if (removed != NULL)
	{
		GArray *edits = self->open_step->edits;
		Edit   *last = edits->len > 0 ? &g_array_index (edits, Edit, edits->len - 1) : NULL;
		Edit    e;

		if (last != NULL && removed[0] == '\0' && pos_cmp (pos_after (last->start, last->inserted), a) == 0 &&
		    self->open_step->kind == STEP_TYPE)
		{
			gchar *joined = g_strconcat (last->inserted, text, NULL);

			g_free (last->inserted);
			last->inserted = joined;
		}
		else if (last != NULL && text[0] == '\0' && last->inserted[0] == '\0' &&
		         self->open_step->kind == STEP_BACKSPACE && pos_cmp (b, last->start) == 0)
		{
			gchar *joined = g_strconcat (removed, last->removed, NULL);

			g_free (last->removed);
			last->removed = joined;
			last->start = a;
		}
		else if (last != NULL && text[0] == '\0' && last->inserted[0] == '\0' &&
		         self->open_step->kind == STEP_DELETE && pos_cmp (a, last->start) == 0)
		{
			gchar *joined = g_strconcat (last->removed, removed, NULL);

			g_free (last->removed);
			last->removed = joined;
		}
		else
		{
			e.start = a;
			e.removed = g_steal_pointer (&removed);
			e.inserted = g_strdup (text);
			g_array_append_val (edits, e);
		}
	}
	/* Typing dismisses the popup. completion_accept() holds it across
	 * the replacement it just inserted. */
	if (!self->completion_hold && self->completion_visible)
	{
		self->completion_visible = FALSE;
		self->completion_selected = -1;
		if (self->completions != NULL)
			g_ptr_array_set_size (self->completions, 0);
	}
	return end;
}

/* ==========================================================================
 * Undo steps
 * ========================================================================== */

static void
clear_edit (gpointer data)
{
	Edit *e = data;

	g_free (e->removed);
	g_free (e->inserted);
}

static Step *
step_new (StepKind kind)
{
	Step *s = g_new0 (Step, 1);

	s->edits = g_array_new (FALSE, FALSE, sizeof (Edit));
	g_array_set_clear_func (s->edits, clear_edit);
	s->kind = kind;
	return s;
}

static void
step_free (gpointer data)
{
	Step *s = data;

	g_array_unref (s->edits);
	g_free (s);
}

/*
 * begin_step:
 * @kind: what the action is (typing and deleting runs coalesce)
 * @text: (nullable): for STEP_TYPE, the typed text
 *
 * Opens the undo step a user action records into. A typing run
 * continues the previous TYPE step unless the caret moved in between,
 * COALESCE_TIME passed, or a word ends (a space after a non-space), so
 * undo removes about a word at a time. Backspace and Delete runs
 * coalesce the same way. Anything else opens a fresh step.
 */
static void
begin_step (LrgTextEditor *self,
            StepKind       kind,
            const gchar   *text)
{
	Step *top = self->undo->len > 0 ? g_ptr_array_index (self->undo, self->undo->len - 1) : NULL;

	g_ptr_array_set_size (self->redo, 0);

	if (kind != STEP_OTHER && top != NULL && top->kind == kind && !self->break_coalesce &&
	    self->clock - self->last_edit_time <= COALESCE_TIME && pos_cmp (self->cursor, top->cursor_after) == 0)
	{
		gboolean word_end = FALSE;

		if (kind == STEP_TYPE && text != NULL && top->edits->len > 0)
		{
			const Edit *last = &g_array_index (top->edits, Edit, top->edits->len - 1);
			gsize       n = strlen (last->inserted);

			word_end = (text[0] == ' ' || text[0] == '\t') && n > 0 &&
			           last->inserted[n - 1] != ' ' && last->inserted[n - 1] != '\t';
		}
		if (!word_end)
		{
			self->open_step = top;
			return;
		}
	}

	top = step_new (kind);
	top->cursor_before = self->cursor;
	top->anchor_before = self->anchor;
	g_ptr_array_add (self->undo, top);
	if (self->undo->len > UNDO_LIMIT)
		g_ptr_array_remove_index (self->undo, 0);
	self->open_step = top;
}

/* end_step: closes the open step, dropping it if nothing was recorded */
static void
end_step (LrgTextEditor *self)
{
	Step *s = self->open_step;

	self->open_step = NULL;
	if (s == NULL)
		return;
	if (s->edits->len == 0)
	{
		g_ptr_array_remove (self->undo, s);
		return;
	}
	s->cursor_after = self->cursor;
	s->anchor_after = self->anchor;
	self->break_coalesce = FALSE;
	self->last_edit_time = self->clock;
}

/* ==========================================================================
 * Notification and the view
 * ========================================================================== */

static gboolean
smooth (LrgTextEditor *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

static gboolean
has_selection (LrgTextEditor *self)
{
	return pos_cmp (self->cursor, self->anchor) != 0;
}

static void
sel_range (LrgTextEditor *self,
           Pos           *a,
           Pos           *b)
{
	if (pos_cmp (self->anchor, self->cursor) <= 0)
	{
		*a = self->anchor;
		*b = self->cursor;
	}
	else
	{
		*a = self->cursor;
		*b = self->anchor;
	}
}

/*
 * enforce_max_lines:
 *
 * Drops the oldest lines past max-lines. Caret, anchor and the scroll
 * offset move up with the text so the view does not jump; undo history
 * would point at the wrong lines and is cleared.
 */
static void
enforce_max_lines (LrgTextEditor *self)
{
	guint  drop;
	gfloat line_h;
	guint  dropped_rows = 0;
	guint  i;

	if (self->max_lines == 0 || n_lines (self) <= self->max_lines)
		return;

	drop = n_lines (self) - self->max_lines;
	line_h = lrg_text_editor_get_line_height (self);
	for (i = 0; i < drop; i++)
	{
		layout_line (self, get_line (self, i));
		dropped_rows += line_rows (get_line (self, i));
	}
	g_ptr_array_remove_range (self->lines, 0, drop);

	self->cursor = self->cursor.line >= drop ? pos_make (self->cursor.line - drop, self->cursor.byte) : pos_make (0, 0);
	self->anchor = self->anchor.line >= drop ? pos_make (self->anchor.line - drop, self->anchor.byte) : pos_make (0, 0);

	/* the first line now starts the document: state 0 */
	get_line (self, 0)->dirty = TRUE;
	invalidate (self, 0);

	self->ay.pos.value = MAX (0.0f, self->ay.pos.value - (gfloat) dropped_rows * line_h);
	self->ay.pos.target = MAX (0.0f, self->ay.pos.target - (gfloat) dropped_rows * line_h);
	self->caret_y.value -= (gfloat) dropped_rows * line_h;
	lrg_text_editor_clear_history (self);
}

/*
 * flush:
 *
 * Ends a user or API action: trims to max-lines, emits ::changed and
 * notify::text when the text changed, ::cursor-moved when the caret
 * moved, re-measures when the natural height changed, and asks the
 * view to bring the caret into sight.
 */
static void
flush (LrgTextEditor *self,
       gboolean       reveal)
{
	guint rows;

	if (self->text_changed)
		enforce_max_lines (self);
	if (reveal)
		self->reveal_pending = TRUE;
	self->activity_time = self->clock;

	if (self->text_changed)
	{
		self->text_changed = FALSE;
		rows = CLAMP (n_lines (self), 1u, NATURAL_ROWS);
		if (rows != self->natural_rows)
		{
			self->natural_rows = rows;
			lrg_widget_queue_layout (LRG_WIDGET (self));
		}
		g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
	}
	if (pos_cmp (self->cursor, self->notified_cursor) != 0)
	{
		Line *l = get_line (self, self->cursor.line);

		self->notified_cursor = self->cursor;
		g_signal_emit (self, signals[SIGNAL_CURSOR_MOVED], 0, self->cursor.line,
		               byte_to_char (l, self->cursor.byte));
	}
}

/* move_to: put the caret at @p, extending the selection with @extend */
static void
move_to (LrgTextEditor *self,
         Pos            p,
         gboolean       extend)
{
	self->cursor = clamp_pos (self, p);
	if (!extend)
		self->anchor = self->cursor;
	self->break_coalesce = TRUE;
}

/* update_extent: content and viewport sizes of both axes */
static void
update_extent (LrgTextEditor *self,
               const Geo     *g)
{
	gfloat content_h;
	gfloat content_w;

	ensure_rows (self);
	content_h = (gfloat) self->n_rows * g->line_h + PAD_Y * 2.0f;
	content_w = self->wrap ? g->vw : (gfloat) self->max_cols * g->cell + PAD_X * 2.0f + CARET_WIDTH;
	lrg_scroll_axis_set_extent (&self->ay, content_h, g->vh);
	lrg_scroll_axis_set_extent (&self->ax, content_w, g->vw);
	if (self->wrap)
		lrg_scroll_axis_scroll_to (&self->ax, 0.0f, FALSE);
}

/*
 * sync_view:
 *
 * Aims the caret springs at the caret's cell and, when a caret move
 * asked for it, scrolls the least amount that shows the caret with a
 * little context (two columns sideways, the padding vertically).
 */
static void
sync_view (LrgTextEditor *self,
           const Geo     *g)
{
	Line  *l = get_line (self, self->cursor.line);
	guint  row = row_of (self, self->cursor);
	gfloat cx = PAD_X + (gfloat) cols_in_row (self, l, self->cursor.byte) * g->cell;
	gfloat cy = PAD_Y + (gfloat) row * g->line_h;

	lrg_spring_set_target (&self->caret_x, cx);
	lrg_spring_set_target (&self->caret_y, cy);

	if (!self->reveal_pending || lrg_widget_get_width (LRG_WIDGET (self)) <= 0.0f)
		return;
	self->reveal_pending = FALSE;
	update_extent (self, g);

	{
		gfloat top = self->ay.pos.target;
		gfloat want = top;

		if (cy - PAD_Y < top)
			want = cy - PAD_Y;
		else if (cy + g->line_h + PAD_Y > top + g->vh)
			want = cy + g->line_h + PAD_Y - g->vh;
		if (want != top)
			lrg_scroll_axis_scroll_to (&self->ay, want, smooth (self));
	}
	if (!self->wrap)
	{
		gfloat left = self->ax.pos.target;
		gfloat margin = g->cell * 2.0f;
		gfloat want = left;

		if (cx - margin - PAD_X < left)
			want = cx - margin - PAD_X;
		else if (cx + margin + PAD_X > left + g->vw)
			want = cx + margin + PAD_X - g->vw;
		if (want != left)
			lrg_scroll_axis_scroll_to (&self->ax, want, smooth (self));
	}
}

/* refresh: geometry + extents + caret targets (outside painting) */
static void
refresh (LrgTextEditor *self)
{
	Geo g;

	geometry (self, &g);
	update_extent (self, &g);
	sync_view (self, &g);
}

/* ==========================================================================
 * Words, lines and brackets
 * ========================================================================== */

/* char_class: 0 space, 1 word, 2 other */
static gint
char_class (gunichar ch)
{
	if (g_unichar_isspace (ch))
		return 0;
	if (g_unichar_isalnum (ch) || ch == '_')
		return 1;
	return 2;
}

/* word_left / word_right: Ctrl+arrow targets (crossing line ends) */
static Pos
word_left (LrgTextEditor *self,
           Pos            p)
{
	Line  *l = get_line (self, p.line);
	gint   cls;

	if (p.byte == 0)
		return p.line > 0 ? pos_make (p.line - 1, (guint) get_line (self, p.line - 1)->text->len) : p;

	/* skip spaces, then the run of one class */
	while (p.byte > 0 && char_class (g_utf8_get_char (l->text->str + prev_byte (l, p.byte))) == 0)
		p.byte = prev_byte (l, p.byte);
	if (p.byte == 0)
		return p;
	cls = char_class (g_utf8_get_char (l->text->str + prev_byte (l, p.byte)));
	while (p.byte > 0 && char_class (g_utf8_get_char (l->text->str + prev_byte (l, p.byte))) == cls)
		p.byte = prev_byte (l, p.byte);
	return p;
}

static Pos
word_right (LrgTextEditor *self,
            Pos            p)
{
	Line *l = get_line (self, p.line);
	guint n = (guint) l->text->len;
	gint  cls;

	if (p.byte >= n)
		return p.line + 1 < n_lines (self) ? pos_make (p.line + 1, 0) : p;

	cls = char_class (g_utf8_get_char (l->text->str + p.byte));
	while (p.byte < n && cls != 0 && char_class (g_utf8_get_char (l->text->str + p.byte)) == cls)
		p.byte = next_byte (l, p.byte);
	while (p.byte < n && char_class (g_utf8_get_char (l->text->str + p.byte)) == 0)
		p.byte = next_byte (l, p.byte);
	return p;
}

/* word_bounds: the run of one class around @p (for double clicks) */
static void
word_bounds (LrgTextEditor *self,
             Pos            p,
             Pos           *a,
             Pos           *b)
{
	Line *l = get_line (self, p.line);
	guint n = (guint) l->text->len;
	guint s = p.byte;
	guint e = p.byte;
	gint  cls;

	if (n == 0)
	{
		*a = *b = pos_make (p.line, 0);
		return;
	}
	if (s >= n)
		s = prev_byte (l, n);
	cls = char_class (g_utf8_get_char (l->text->str + s));
	e = s;
	while (s > 0 && char_class (g_utf8_get_char (l->text->str + prev_byte (l, s))) == cls)
		s = prev_byte (l, s);
	while (e < n && char_class (g_utf8_get_char (l->text->str + e)) == cls)
		e = next_byte (l, e);
	*a = pos_make (p.line, s);
	*b = pos_make (p.line, e);
}

/* line_bounds: the whole line, including its newline when there is one */
static void
line_bounds (LrgTextEditor *self,
             guint          line,
             Pos           *a,
             Pos           *b)
{
	*a = pos_make (line, 0);
	*b = line + 1 < n_lines (self) ? pos_make (line + 1, 0)
	                               : pos_make (line, (guint) get_line (self, line)->text->len);
}

/* leading_ws: bytes of leading whitespace of a line */
static guint
leading_ws (Line *l)
{
	guint i = 0;

	while (i < l->text->len && (l->text->str[i] == ' ' || l->text->str[i] == '\t'))
		i++;
	return i;
}

static gchar
bracket_partner (gchar c,
                 gint *dir)
{
	switch (c)
	{
	case '(': *dir = 1; return ')';
	case '[': *dir = 1; return ']';
	case '{': *dir = 1; return '}';
	case ')': *dir = -1; return '(';
	case ']': *dir = -1; return '[';
	case '}': *dir = -1; return '{';
	default: *dir = 0; return '\0';
	}
}

/*
 * scan_spans:
 *
 * The highlight spans bracket matching tests line @i against, or %NULL
 * without a language. The line's entry state is brought up to date
 * first (state only, so the walk stays cheap); a line that already has
 * painted spans reuses them, any other line is tokenized into a scratch
 * array (not cached and not counted by the tokenize diagnostics, so a
 * long scan does not leave spans behind for off-screen lines). The
 * returned array is valid until the next call.
 */
static GArray *
scan_spans (LrgTextEditor *self,
            guint          i)
{
	Line *l;

	if (self->syntax == NULL)
		return NULL;
	ensure_states (self, i, G_MAXUINT);
	l = get_line (self, i);
	if (l->spans != NULL && !l->dirty)
		return l->spans;
	g_array_set_size (self->bracket_spans, 0);
	lrg_syntax_tokenize_line (self->syntax, l->text->str, (gssize) l->text->len,
	                          l->state_in, self->bracket_spans);
	return self->bracket_spans;
}

/*
 * in_literal:
 *
 * Whether byte @b lies in a string, escape or comment span. Spans are
 * sorted and disjoint, so a binary search finds the one covering @b.
 * %NULL spans (no language) never hide anything.
 */
static gboolean
in_literal (GArray *spans,
            guint   b)
{
	guint lo = 0;
	guint hi;

	if (spans == NULL)
		return FALSE;
	hi = spans->len;
	while (lo < hi)
	{
		guint          mid = lo + (hi - lo) / 2;
		LrgSyntaxSpan *s = &g_array_index (spans, LrgSyntaxSpan, mid);

		if (b < s->start)
			hi = mid;
		else if (b >= s->end)
			lo = mid + 1;
		else
			return s->token == LRG_SYNTAX_TOKEN_STRING
			       || s->token == LRG_SYNTAX_TOKEN_ESCAPE
			       || s->token == LRG_SYNTAX_TOKEN_COMMENT;
	}
	return FALSE;
}

/*
 * scan_bracket:
 *
 * From the bracket at @p, walks forwards or backwards counting nesting
 * of the same pair until the partner (at most BRACKET_SCAN_LIMIT bytes).
 *
 * With a language set, brackets inside string, escape and comment
 * tokens are invisible: they neither nest nor match, and a bracket at
 * @p that is itself inside one has no partner at all. Each line's spans
 * are fetched once, when the walk enters it.
 */
static gboolean
scan_bracket (LrgTextEditor *self,
              Pos            p,
              Pos           *out)
{
	gchar   open = get_line (self, p.line)->text->str[p.byte];
	gint    dir;
	gchar   close = bracket_partner (open, &dir);
	gint    depth = 0;
	guint   budget = BRACKET_SCAN_LIMIT;
	guint   line = p.line;
	gint64  b = (gint64) p.byte;
	GArray *spans;

	if (dir == 0)
		return FALSE;

	/* 1. A bracket in a string or comment is text, not a bracket */
	spans = scan_spans (self, line);
	if (in_literal (spans, p.byte))
		return FALSE;

	/* 2. Walk, skipping literal bytes, until depth returns to zero */
	while (budget-- > 0)
	{
		Line *l = get_line (self, line);

		if (b >= 0 && b < (gint64) l->text->len)
		{
			gchar c = l->text->str[b];

			if ((c == open || c == close) && !in_literal (spans, (guint) b))
			{
				if (c == open)
					depth++;
				else if (--depth == 0)
				{
					*out = pos_make (line, (guint) b);
					return TRUE;
				}
			}
			b += dir;
			continue;
		}

		/* 3. Next / previous line (its spans are fetched once here) */
		if (dir > 0)
		{
			if (line + 1 >= n_lines (self))
				return FALSE;
			line++;
			b = 0;
		}
		else
		{
			if (line == 0)
				return FALSE;
			line--;
			b = (gint64) get_line (self, line)->text->len - 1;
		}
		spans = scan_spans (self, line);
	}
	return FALSE;
}

/*
 * update_brackets:
 *
 * The bracket pair at the caret (the character before it first, then
 * the one after), cached until the text or the caret changes.
 */
static void
update_brackets (LrgTextEditor *self)
{
	Line *l = get_line (self, self->cursor.line);
	Pos   other;
	gint  dir;

	if (self->bracket_serial == self->edit_serial && pos_cmp (self->bracket_cursor, self->cursor) == 0)
		return;
	self->bracket_serial = self->edit_serial;
	self->bracket_cursor = self->cursor;
	self->bracket_found = FALSE;

	if (self->cursor.byte > 0)
	{
		Pos before = pos_make (self->cursor.line, self->cursor.byte - 1);

		bracket_partner (l->text->str[before.byte], &dir);
		if (dir != 0 && scan_bracket (self, before, &other))
		{
			self->bracket_a = before;
			self->bracket_b = other;
			self->bracket_found = TRUE;
			return;
		}
	}
	if (self->cursor.byte < l->text->len)
	{
		bracket_partner (l->text->str[self->cursor.byte], &dir);
		if (dir != 0 && scan_bracket (self, self->cursor, &other))
		{
			self->bracket_a = self->cursor;
			self->bracket_b = other;
			self->bracket_found = TRUE;
		}
	}
}

/* ==========================================================================
 * Find
 * ========================================================================== */

/*
 * fold_append:
 * @out: receives the folded text
 * @map: (nullable): receives, per byte appended to @out, the byte of
 *   @s (plus @base) of the character it came from
 * @s: UTF-8 text
 * @len: bytes of @s
 * @base: added to the offsets written to @map
 *
 * Unicode case folding for case-insensitive find, one character at a
 * time so every folded byte can be traced back to its source character:
 * ASCII is lowered directly (the fast path); any other character goes
 * through g_utf8_casefold() and then g_utf8_normalize() with
 * %G_NORMALIZE_DEFAULT, which decomposes it ("ä" -> "a" + U+0308), so
 * precomposed and decomposed spellings fold to the same bytes. Folding
 * may change the byte length ("ß" -> "ss", "İ" -> "i" + U+0307); the map
 * is what lets matches land on the right source bytes. Invalid bytes
 * are copied through unchanged.
 */
static void
fold_append (GString     *out,
             GArray      *map,
             const gchar *s,
             gsize        len,
             guint        base)
{
	gsize i = 0;

	while (i < len)
	{
		guchar   c = (guchar) s[i];
		gsize    before = out->len;
		gsize    clen = 1;
		gunichar ch;

		if (c < 0x80)
			g_string_append_c (out, g_ascii_tolower ((gchar) c));
		else if ((ch = g_utf8_get_char_validated (s + i, (gssize) (len - i))) == (gunichar) -1
		         || ch == (gunichar) -2)
			g_string_append_c (out, (gchar) c);
		else
		{
			g_autofree gchar *folded = NULL;
			g_autofree gchar *norm = NULL;

			clen = (gsize) (g_utf8_next_char (s + i) - (s + i));
			folded = g_utf8_casefold (s + i, (gssize) clen);
			norm = g_utf8_normalize (folded, -1, G_NORMALIZE_DEFAULT);
			g_string_append (out, norm != NULL ? norm : folded);
		}

		/* every byte this character produced points back at it */
		if (map != NULL)
		{
			guint src = base + (guint) i;

			for (; before < out->len; before++)
				g_array_append_val (map, src);
		}
		i += clen;
	}
}

/*
 * fold_line:
 *
 * Caches the folded copy of @l and its byte map. One line is cached,
 * keyed by the line and the edit serial, so painting several rows of a
 * wrapped line or counting many matches in it folds it once.
 */
static void
fold_line (LrgTextEditor *self,
           Line          *l)
{
	if (self->fold_line == l && self->fold_serial == self->edit_serial)
		return;
	g_string_truncate (self->fold_text, 0);
	g_array_set_size (self->fold_map, 0);
	fold_append (self->fold_text, self->fold_map, l->text->str, l->text->len, 0);
	self->fold_line = l;
	self->fold_serial = self->edit_serial;
}

/*
 * match_folded:
 *
 * Case-insensitive search of the folded needle in the folded line. A
 * match must start where a source character's fold starts and end where
 * one ends: "s" does not match half of "ß", and "i" does not match the
 * "i" of a folded "İ". The source range is read back through the map.
 */
static gssize
match_folded (LrgTextEditor *self,
              Line          *l,
              guint          from,
              guint         *end)
{
	const gchar *f;
	const guint *map;
	gsize        fl;
	gsize        n = self->needle_fold->len;
	gsize        k;

	fold_line (self, l);
	f = self->fold_text->str;
	fl = self->fold_text->len;
	map = (const guint *) (gpointer) self->fold_map->data;
	if (n == 0 || fl < n)
		return -1;

	for (k = 0; k + n <= fl; k++)
	{
		/* only at character starts at or after @from */
		if ((k > 0 && map[k] == map[k - 1]) || map[k] < from)
			continue;
		if (f[k] != self->needle_fold->str[0] || memcmp (f + k, self->needle_fold->str, n) != 0)
			continue;
		/* must not end inside one character's fold */
		if (k + n < fl && map[k + n] == map[k + n - 1])
			continue;
		*end = k + n < fl ? map[k + n] : (guint) l->text->len;
		return (gssize) map[k];
	}
	return -1;
}

/*
 * match_in:
 * @end: (out) (optional): byte after the match
 *
 * First match of the needle in @l starting at or after @from, or -1.
 * Case-sensitive find compares bytes; otherwise the line is folded (see
 * fold_append()) and the match may span a different number of bytes
 * than the needle, so callers use @end, never strlen (needle).
 */
static gssize
match_in (LrgTextEditor *self,
          Line          *l,
          guint          from,
          guint         *end)
{
	gsize  n = strlen (self->needle);
	gsize  i;
	guint  e = 0;
	gssize m = -1;

	if (!self->needle_case)
		m = match_folded (self, l, from, &e);
	else if (n > 0 && l->text->len >= n)
	{
		for (i = from; i + n <= l->text->len; i++)
		{
			if (memcmp (l->text->str + i, self->needle, n) == 0)
			{
				m = (gssize) i;
				e = (guint) (i + n);
				break;
			}
		}
	}
	if (m >= 0 && end != NULL)
		*end = e;
	return m;
}

/* match_before: last match in @l starting before @before, or -1 */
static gssize
match_before (LrgTextEditor *self,
              Line          *l,
              guint          before,
              guint         *end)
{
	gssize found = -1;
	guint  e = 0;
	gssize m = match_in (self, l, 0, &e);

	while (m >= 0 && (guint) m < before)
	{
		found = m;
		*end = e;
		m = match_in (self, l, (guint) m + 1, &e);
	}
	return found;
}

static void
select_match (LrgTextEditor *self,
              guint          line,
              guint          byte,
              guint          end)
{
	self->anchor = pos_make (line, byte);
	self->cursor = pos_make (line, end);
	self->break_coalesce = TRUE;
	flush (self, TRUE);
}

/* find_from: first match at or after @p, wrapping around once */
static gboolean
find_from (LrgTextEditor *self,
           Pos            p)
{
	guint n = n_lines (self);
	guint k;

	for (k = 0; k <= n; k++)
	{
		guint  line = (p.line + k) % n;
		guint  end = 0;
		gssize m = match_in (self, get_line (self, line), k == 0 ? p.byte : 0, &end);

		if (m >= 0 && !(k == n && (guint) m >= p.byte))
		{
			select_match (self, line, (guint) m, end);
			return TRUE;
		}
	}
	return FALSE;
}

/* ==========================================================================
 * Editing actions
 * ========================================================================== */

/* user_can_edit: typing / pasting / undo allowed */
static gboolean
user_can_edit (LrgTextEditor *self)
{
	return !self->read_only && lrg_widget_get_enabled (LRG_WIDGET (self));
}

/*
 * replace_selection:
 *
 * One recorded action: replaces the selection (or inserts at the
 * caret) with @text and leaves the caret after it.
 */
static void
replace_selection (LrgTextEditor *self,
                   StepKind       kind,
                   const gchar   *text)
{
	Pos a;
	Pos b;

	if (has_selection (self))
		kind = STEP_OTHER;
	begin_step (self, kind, text);
	sel_range (self, &a, &b);
	self->cursor = self->anchor = buffer_replace (self, a, b, text);
	self->goal_x = -1.0f;
	end_step (self);
	flush (self, TRUE);
}

/* delete_range_action: a recorded deletion (Backspace / Delete runs) */
static void
delete_range_action (LrgTextEditor *self,
                     StepKind       kind,
                     Pos            a,
                     Pos            b)
{
	if (pos_cmp (a, b) == 0)
		return;
	begin_step (self, kind, NULL);
	self->cursor = self->anchor = buffer_replace (self, a, b, "");
	self->goal_x = -1.0f;
	end_step (self);
	flush (self, TRUE);
}

/* indent_unit: one level of indentation */
static gchar *
indent_unit (LrgTextEditor *self)
{
	return self->insert_spaces ? g_strnfill (self->tab_width, ' ') : g_strdup ("\t");
}

/*
 * newline:
 *
 * Enter with auto-indent: the new line copies the current line's
 * leading whitespace, one level more after an opening bracket or ':'.
 * Between a bracket pair ("{|}") the closer moves to its own line at
 * the original indentation.
 */
static void
newline (LrgTextEditor *self)
{
	Line             *la;
	g_autofree gchar *indent = NULL;
	g_autofree gchar *unit = NULL;
	g_autofree gchar *text = NULL;
	Pos               a;
	Pos               b;
	gchar             before = '\0';
	gchar             after = '\0';
	gint              dir;

	if (!self->auto_indent)
	{
		replace_selection (self, STEP_OTHER, "\n");
		return;
	}

	sel_range (self, &a, &b);
	la = get_line (self, a.line);
	indent = g_strndup (la->text->str, MIN (leading_ws (la), a.byte));
	if (a.byte > 0)
	{
		guint k = a.byte;

		while (k > 0 && (la->text->str[k - 1] == ' ' || la->text->str[k - 1] == '\t'))
			k--;
		before = k > 0 ? la->text->str[k - 1] : '\0';
	}
	if (b.byte < get_line (self, b.line)->text->len)
		after = get_line (self, b.line)->text->str[b.byte];

	unit = indent_unit (self);
	bracket_partner (before, &dir);
	if (dir > 0 || before == ':')
	{
		gint d2;
		gchar partner = bracket_partner (before, &d2);

		if (dir > 0 && after == partner)
		{
			Pos caret;

			/* "{|}" -> "{\n\t|\n}" */
			text = g_strconcat ("\n", indent, unit, "\n", indent, NULL);
			begin_step (self, STEP_OTHER, NULL);
			buffer_replace (self, a, b, text);
			caret = pos_make (a.line + 1, (guint) (strlen (indent) + strlen (unit)));
			self->cursor = self->anchor = caret;
			end_step (self);
			flush (self, TRUE);
			return;
		}
		text = g_strconcat ("\n", indent, unit, NULL);
	}
	else
		text = g_strconcat ("\n", indent, NULL);
	replace_selection (self, STEP_OTHER, text);
}

/*
 * indent_lines:
 * @outdent: remove a level instead of adding one
 *
 * Indents / outdents every line touched by the selection (a selection
 * ending at column 0 does not touch its last line). The selection is
 * kept over the same text.
 */
static void
indent_lines (LrgTextEditor *self,
              gboolean       outdent)
{
	g_autofree gchar *unit = indent_unit (self);
	Pos               a;
	Pos               b;
	guint             last;
	guint             i;

	sel_range (self, &a, &b);
	last = (b.line > a.line && b.byte == 0) ? b.line - 1 : b.line;

	begin_step (self, STEP_OTHER, NULL);
	for (i = a.line; i <= last; i++)
	{
		Line *l = get_line (self, i);
		gint  delta;

		if (!outdent)
		{
			if (l->text->len == 0 && a.line != last)
				continue;
			buffer_replace (self, pos_make (i, 0), pos_make (i, 0), unit);
			delta = (gint) strlen (unit);
		}
		else
		{
			guint k = 0;

			/* one tab, or up to tab-width spaces */
			if (l->text->len > 0 && l->text->str[0] == '\t')
				k = 1;
			else
				while (k < self->tab_width && k < l->text->len && l->text->str[k] == ' ')
					k++;
			if (k == 0)
				continue;
			buffer_replace (self, pos_make (i, 0), pos_make (i, k), "");
			delta = -(gint) k;
		}

		/* keep caret and anchor on their characters; a position at
		 * column 0 stays there so the selection covers the new indent */
		if (i == self->cursor.line && self->cursor.byte > 0)
			self->cursor.byte = (guint) MAX (0, (gint) self->cursor.byte + delta);
		if (i == self->anchor.line && self->anchor.byte > 0)
			self->anchor.byte = (guint) MAX (0, (gint) self->anchor.byte + delta);
	}
	self->cursor = clamp_pos (self, self->cursor);
	self->anchor = clamp_pos (self, self->anchor);
	end_step (self);
	flush (self, TRUE);
}

/* tab_key: indent a multi-line selection, else insert a tab / spaces */
static void
tab_key (LrgTextEditor *self)
{
	Pos a;
	Pos b;

	sel_range (self, &a, &b);
	if (a.line != b.line)
	{
		indent_lines (self, FALSE);
		return;
	}
	if (self->insert_spaces)
	{
		Line             *l = get_line (self, a.line);
		guint             col = col_at (self, l, a.byte);
		g_autofree gchar *spaces = g_strnfill (self->tab_width - col % self->tab_width, ' ');

		replace_selection (self, STEP_TYPE, spaces);
	}
	else
		replace_selection (self, STEP_TYPE, "\t");
}

/*
 * backspace:
 *
 * Deletes the selection, else the previous word (Ctrl), else back to
 * the previous tab stop inside space indentation, else one character
 * (joining lines at column 0).
 */
static void
backspace (LrgTextEditor *self,
           gboolean       ctrl)
{
	Line *l = get_line (self, self->cursor.line);
	Pos   p = self->cursor;
	Pos   a;
	Pos   b;

	if (has_selection (self))
	{
		sel_range (self, &a, &b);
		delete_range_action (self, STEP_OTHER, a, b);
		return;
	}
	if (ctrl)
	{
		delete_range_action (self, STEP_BACKSPACE, word_left (self, p), p);
		return;
	}
	if (p.byte == 0)
	{
		if (p.line > 0)
			delete_range_action (self, STEP_BACKSPACE,
			                     pos_make (p.line - 1, (guint) get_line (self, p.line - 1)->text->len), p);
		return;
	}
	if (self->insert_spaces && p.byte <= leading_ws (l))
	{
		guint k;
		gboolean spaces = TRUE;

		for (k = 0; k < p.byte; k++)
			if (l->text->str[k] != ' ')
				spaces = FALSE;
		if (spaces)
		{
			guint n = ((p.byte - 1) % self->tab_width) + 1;

			delete_range_action (self, STEP_BACKSPACE, pos_make (p.line, p.byte - n), p);
			return;
		}
	}
	delete_range_action (self, STEP_BACKSPACE, pos_make (p.line, prev_byte (l, p.byte)), p);
}

static void
delete_forward (LrgTextEditor *self,
                gboolean       ctrl)
{
	Line *l = get_line (self, self->cursor.line);
	Pos   p = self->cursor;
	Pos   a;
	Pos   b;

	if (has_selection (self))
	{
		sel_range (self, &a, &b);
		delete_range_action (self, STEP_OTHER, a, b);
		return;
	}
	if (ctrl)
		delete_range_action (self, STEP_DELETE, p, word_right (self, p));
	else if (p.byte < l->text->len)
		delete_range_action (self, STEP_DELETE, p, pos_make (p.line, next_byte (l, p.byte)));
	else if (p.line + 1 < n_lines (self))
		delete_range_action (self, STEP_DELETE, p, pos_make (p.line + 1, 0));
}

/* ==========================================================================
 * Caret movement
 * ========================================================================== */

/* caret_x_in_row: the caret's x from its row start (goal for Up / Down) */
static gfloat
caret_x_in_row (LrgTextEditor *self,
                gfloat         cell)
{
	if (self->goal_x < 0.0f)
		self->goal_x = (gfloat) cols_in_row (self, get_line (self, self->cursor.line), self->cursor.byte) * cell;
	return self->goal_x;
}

/* move_rows: Up / Down / PageUp / PageDown by @delta visual rows */
static void
move_rows (LrgTextEditor *self,
           gint           delta,
           gboolean       extend)
{
	Geo    g;
	gint64 row;
	guint  sub;
	guint  line;
	gfloat x;

	geometry (self, &g);
	x = caret_x_in_row (self, g.cell);
	row = (gint64) row_of (self, self->cursor) + delta;

	if (row < 0)
	{
		self->cursor = pos_make (0, 0);
		if (!extend)
			self->anchor = self->cursor;
		self->break_coalesce = TRUE;
		return;
	}
	if (row >= (gint64) self->n_rows)
	{
		self->cursor = doc_end (self);
		if (!extend)
			self->anchor = self->cursor;
		self->break_coalesce = TRUE;
		return;
	}
	line = line_of_row (self, (guint) row, &sub);
	self->cursor = pos_make (line, byte_at_x (self, get_line (self, line), sub, x, g.cell));
	if (!extend)
		self->anchor = self->cursor;
	self->break_coalesce = TRUE;
}

/* rows_per_page: whole rows in the viewport, minus one of overlap */
static gint
rows_per_page (LrgTextEditor *self)
{
	Geo g;

	geometry (self, &g);
	return MAX (1, (gint) floorf (g.vh / g.line_h) - 1);
}

/* home / end of the caret's row (smart home: indentation first) */
static Pos
home_pos (LrgTextEditor *self)
{
	Line *l = get_line (self, self->cursor.line);
	guint r;
	guint rs;
	guint re;
	guint ws;

	layout_line (self, l);
	r = row_in_line (l, self->cursor.byte);
	row_bounds (l, r, &rs, &re);
	if (r > 0)
		return pos_make (self->cursor.line, rs);
	ws = leading_ws (l);
	return pos_make (self->cursor.line, self->cursor.byte == ws ? 0 : ws);
}

static Pos
end_pos (LrgTextEditor *self)
{
	Line *l = get_line (self, self->cursor.line);
	guint r;
	guint rs;
	guint re;

	layout_line (self, l);
	r = row_in_line (l, self->cursor.byte);
	row_bounds (l, r, &rs, &re);
	if (re < l->text->len && re > rs)
		re = prev_byte (l, re);
	return pos_make (self->cursor.line, re);
}

/* ==========================================================================
 * Clipboard
 * ========================================================================== */

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
 * lrg_text_editor_style_defaults:
 *
 * BOX      recessed INPUT well, BORDER frame turning ACCENT when focused
 * HEADER   the gutter: a tint between INPUT and PANEL, BORDER separator,
 *          TEXT_MUTED numbers (TEXT for the caret line: SELECTED)
 * ITEM     current-line band: TEXT at 5 %
 * FILL     selection: SELECTION; a muted wash while unfocused
 * MARK     caret: ACCENT, CARET_WIDTH wide
 * INDICATOR  bracket match: ACCENT outline and wash; CHECKED = find
 *          match: WARNING wash
 * LABEL    placeholder: TEXT_MUTED
 */
static void
lrg_text_editor_style_defaults (LrgWidget      *widget,
                                LrgUIPart       part,
                                LrgWidgetState  state,
                                LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlColor  c;

	if (lrg_scroll_style_defaults (theme, part, state, look))
		return;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (state & LRG_WIDGET_STATE_FOCUSED) ? LRG_THEME_COLOR_ACCENT
		                                                             : LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_HEADER:
		lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT),
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 0.6f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_SELECTED) ? LRG_THEME_COLOR_TEXT
		                                                                  : LRG_THEME_COLOR_TEXT_MUTED));
		return;

	case LRG_UI_PART_ITEM:
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT), 0.05f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, 0.0f);
		return;

	case LRG_UI_PART_FILL:
		if (state & LRG_WIDGET_STATE_FOCUSED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
		else
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.22f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, 3.0f);
		return;

	case LRG_UI_PART_MARK:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_size (look, CARET_WIDTH);
		return;

	case LRG_UI_PART_INDICATOR:
		if (state & LRG_WIDGET_STATE_CHECKED)
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING), 0.32f, &c);
			lrg_part_look_set_background (look, &c);
		}
		else
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.18f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.7f, &c);
			lrg_part_look_set_border (look, &c, 1.0f);
		}
		lrg_part_look_set_radius (look, 3.0f);
		return;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_text_editor_parent_class)->style_defaults (widget, part, state, look);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_text_editor_layout_children (LrgContainer *container)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (container);
	Geo            g;
	gfloat         zone;
	gboolean       v;
	gboolean       h;

	if (self->in_layout || self->ay.bar == NULL)
		return;
	self->in_layout = TRUE;

	geometry (self, &g);
	update_extent (self, &g);
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	v = lrg_scroll_axis_get_max (&self->ay) > 0.0f;
	h = !self->wrap && lrg_scroll_axis_get_max (&self->ax) > 0.0f;

	lrg_widget_set_visible (self->ay.bar, v);
	lrg_widget_set_visible (self->ax.bar, h);
	lrg_widget_allocate (self->ay.bar, g.vx + g.vw - zone, g.vy, zone, MAX (0.0f, g.vh - (h ? zone : 0.0f)));
	lrg_widget_allocate (self->ax.bar, g.vx, g.vy + g.vh - zone, MAX (0.0f, g.vw - (v ? zone : 0.0f)), zone);

	sync_view (self, &g);
	self->in_layout = FALSE;
}

static void
lrg_text_editor_measure (LrgWidget *widget,
                         gfloat    *preferred_width,
                         gfloat    *preferred_height)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (widget);
	Geo            g;
	guint          rows = CLAMP (n_lines (self), 1u, NATURAL_ROWS);

	geometry (self, &g);
	if (preferred_width != NULL)
		*preferred_width = NATURAL_WIDTH + g.gutter;
	if (preferred_height != NULL)
		*preferred_height = (gfloat) rows * g.line_h + PAD_Y * 2.0f + g.border * 2.0f;
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

/*
 * lrg_text_editor_step:
 *
 * Advances the clock (blink, multi-click and coalescing timing), the
 * scroll springs, the caret glide springs and the scrollbar fade. The
 * first step snaps everything so a new editor does not animate in. The
 * blink itself is a pure function of the clock and is not reported as
 * motion, so a focused editor does not keep the canvas busy.
 *
 * Returns: %TRUE while scrolling, the caret glides or the bars fade
 */
static gboolean
lrg_text_editor_step (LrgWidget *widget,
                      gfloat     delta_time)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (widget);
	gboolean       moving;
	gboolean       keep;
	LrgMotion      motion = lrg_widget_get_motion (widget);

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (self->ay.bar == NULL)
		return FALSE;

	refresh (self);

	lrg_spring_set_motion (&self->caret_x, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	lrg_spring_set_motion (&self->caret_y, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	if (!self->stepped)
	{
		self->stepped = TRUE;
		lrg_spring_snap (&self->caret_x, self->caret_x.target);
		lrg_spring_snap (&self->caret_y, self->caret_y.target);
		lrg_scroll_axis_scroll_to (&self->ay, self->ay.pos.target, FALSE);
		lrg_scroll_axis_scroll_to (&self->ax, self->ax.pos.target, FALSE);
		return FALSE;
	}

	moving = lrg_scroll_axis_step_snap (&self->ay, delta_time);
	moving = lrg_scroll_axis_step_snap (&self->ax, delta_time) || moving;
	if (moving)
		lrg_scroll_fx_poke (&self->fx);
	moving = lrg_spring_step (&self->caret_x, delta_time) || moving;
	moving = lrg_spring_step (&self->caret_y, delta_time) || moving;

	keep = self->ay.dragging || self->ax.dragging ||
	       (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED) ||
	       (lrg_widget_get_state_flags (self->ax.bar) & LRG_WIDGET_STATE_HOVERED);
	return lrg_scroll_fx_update (&self->fx, keep, delta_time) || moving;
}

/* caret_alpha: solid after activity, then a soft cosine blink */
static gfloat
caret_alpha (LrgTextEditor *self)
{
	gdouble t = self->clock - self->activity_time;

	if (t < CARET_SOLID)
		return 1.0f;
	t = fmod (t - CARET_SOLID, CARET_PERIOD) / CARET_PERIOD;
	return (gfloat) (0.5 + 0.5 * cos (t * G_PI * 2.0));
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

typedef struct
{
	LrgPaintList *list;
	Geo           g;
	gfloat        ox;        /* world x of column 0 (scrolled) */
	gfloat        oy;        /* world y of row 0 (scrolled) */
	GrlColor      colors[LRG_SYNTAX_N_TOKENS];
} Painter;

/* paint_run: one run of same-coloured text without tabs */
static void
paint_run (LrgTextEditor *self,
           Painter       *pt,
           const gchar   *s,
           guint          from,
           guint          to,
           guint          col,
           gfloat         y,
           LrgSyntaxToken token)
{
	guint i;

	if (to <= from)
		return;
	for (i = from; i < to && s[i] == ' '; i++)
		;
	if (i == to)
		return;
	g_string_truncate (self->scratch, 0);
	g_string_append_len (self->scratch, s + from, (gssize) (to - from));
	lrg_paint_list_text (pt->list, pt->g.font, self->scratch->str,
	                     pt->ox + (gfloat) col * pt->g.cell, y, pt->g.size, 0.0f, &pt->colors[token]);
}

/*
 * paint_row_text:
 *
 * Walks the characters of one row, cutting runs where the token kind
 * changes or at tabs, and draws each run at its grid column.
 */
static void
paint_row_text (LrgTextEditor *self,
                Painter       *pt,
                Line          *l,
                GArray        *spans,
                guint          rs,
                guint          re,
                gfloat         y)
{
	const gchar   *s = l->text->str;
	guint          base = col_at (self, l, rs);
	guint          col = base;
	guint          run = rs;
	guint          run_col = 0;
	guint          k = 0;
	guint          b = rs;
	LrgSyntaxToken cur = LRG_SYNTAX_TOKEN_TEXT;
	LrgSyntaxToken tok;

	/* first span that ends after the row start */
	while (spans != NULL && k < spans->len && g_array_index (spans, LrgSyntaxSpan, k).end <= rs)
		k++;

	while (b < re)
	{
		gunichar ch = g_utf8_get_char (s + b);

		while (spans != NULL && k < spans->len && g_array_index (spans, LrgSyntaxSpan, k).end <= b)
			k++;
		tok = (spans != NULL && k < spans->len && g_array_index (spans, LrgSyntaxSpan, k).start <= b)
		      ? g_array_index (spans, LrgSyntaxSpan, k).token : LRG_SYNTAX_TOKEN_TEXT;

		if (tok != cur || ch == '\t')
		{
			paint_run (self, pt, s, run, b, run_col, y, cur);
			run = b;
			run_col = col - base;
			cur = tok;
		}
		col += char_cols (self, ch, col);
		b = next_byte (l, b);
		if (ch == '\t')
		{
			run = b;
			run_col = col - base;
		}
	}
	paint_run (self, pt, s, run, re, run_col, y, cur);
}

/* paint_cells: a look as a box over bytes [a, b) of a row */
static void
paint_cells (LrgTextEditor     *self,
             Painter           *pt,
             const LrgPartLook *look,
             Line              *l,
             guint              rs,
             guint              a,
             guint              b,
             gfloat             extra,
             gfloat             y)
{
	guint  base = col_at (self, l, rs);
	gfloat x0 = (gfloat) (col_at (self, l, a) - base) * pt->g.cell;
	gfloat x1 = (gfloat) (col_at (self, l, b) - base) * pt->g.cell + extra;

	if (x1 <= x0)
		return;
	lrg_widget_paint_box (LRG_WIDGET (self), pt->list, look, pt->ox + x0, y, x1 - x0, pt->g.line_h);
}

/* paint_selection_row: the part of the selection on one row */
static void
paint_selection_row (LrgTextEditor     *self,
                     Painter           *pt,
                     const LrgPartLook *look,
                     guint              line,
                     Line              *l,
                     guint              rs,
                     guint              re,
                     gboolean           last_row,
                     gfloat             y)
{
	Pos   a;
	Pos   b;
	guint from;
	guint to;
	gfloat extra = 0.0f;

	sel_range (self, &a, &b);
	if (line < a.line || line > b.line)
		return;
	from = line == a.line ? MAX (a.byte, rs) : rs;
	to = line == b.line ? MIN (b.byte, re) : re;
	if (line == a.line && a.byte > re)
		return;
	if (line == b.line && b.byte < rs)
		return;
	/* the newline of a selected line end shows as one cell */
	if (line < b.line && last_row)
		extra = pt->g.cell * 0.6f;
	if (to < from)
		return;
	paint_cells (self, pt, look, l, rs, from, to, extra, y);
}

/* paint_gutter: background, separator, marker column and line numbers */
static void
paint_gutter (LrgTextEditor *self,
              Painter       *pt,
              gfloat         wx,
              gfloat         wy,
              guint          row0,
              guint          row1,
              gfloat         top)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  look;
	LrgPartLook  cur;
	gfloat       gx = wx + pt->g.border;
	gfloat       gy = wy + pt->g.border;
	gfloat       mark_w = self->show_gutter_marks ? MARKER_W : 0.0f;
	gchar        num[16];
	guint        row;

	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_HEADER, LRG_WIDGET_STATE_NONE, &look);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_HEADER, LRG_WIDGET_STATE_SELECTED, &cur);
	lrg_paint_list_rect (pt->list, gx, gy, pt->g.gutter, pt->g.vh, 0.0f, &look.background);
	if (look.border_width > 0.0f)
		lrg_paint_list_rect (pt->list, gx + pt->g.gutter - look.border_width, gy, look.border_width,
		                     pt->g.vh, 0.0f, &look.border);

	lrg_paint_list_push_clip (pt->list, gx, gy, pt->g.gutter, pt->g.vh);
	for (row = row0; row <= row1 && row < self->n_rows; row++)
	{
		guint  sub;
		guint  line = line_of_row (self, row, &sub);
		gfloat y = top + (gfloat) row * pt->g.line_h;
		gsize  n;

		if (sub != 0)
			continue;
		if (self->show_gutter_marks)
		{
			gpointer found = NULL;

			if (g_hash_table_lookup_extended (self->gutter_marks, GUINT_TO_POINTER (line), NULL, &found))
			{
				LrgTextGutterMark flags = (LrgTextGutterMark) GPOINTER_TO_UINT (found);
				const GrlColor   *color = NULL;

				if ((flags & LRG_TEXT_GUTTER_MARK_BREAKPOINT_DISABLED) != 0)
					color = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
				else if ((flags & LRG_TEXT_GUTTER_MARK_BREAKPOINT) != 0)
					color = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
				if (color != NULL)
					lrg_paint_list_rect (pt->list, gx + 3.0f, y + (pt->g.line_h - 8.0f) * 0.5f,
					                     8.0f, 8.0f, 4.0f, color);
				if ((flags & LRG_TEXT_GUTTER_MARK_CURRENT) != 0)
					lrg_paint_list_rect (pt->list, gx + 1.0f, y + 2.0f, 3.0f, pt->g.line_h - 4.0f, 1.0f,
					                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
			}
		}
		if (!self->show_line_numbers)
			continue;
		n = (gsize) g_snprintf (num, sizeof num, "%u", line + 1);
		lrg_paint_list_text (pt->list, pt->g.font, num,
		                     gx + mark_w + (pt->g.gutter - mark_w) - GUTTER_PAD - (gfloat) n * pt->g.cell,
		                     y + (pt->g.line_h - pt->g.size) * 0.5f, pt->g.size, 0.0f,
		                     line == self->cursor.line ? &cur.foreground : &look.foreground);
	}
	lrg_paint_list_pop_clip (pt->list);
}

/* paint_squiggle: a one-pixel wave under a span of one row */
static void
paint_squiggle (LrgPaintList   *list,
                gfloat          x0,
                gfloat          x1,
                gfloat          y,
                const GrlColor *color)
{
	gfloat x;

	if (x1 < x0 + 2.0f)
		x1 = x0 + 2.0f;
	for (x = x0; x < x1; x += 4.0f)
	{
		gfloat w = MIN (3.0f, x1 - x);
		gfloat dy = (((gint) (x / 4.0f)) & 1) != 0 ? 2.0f : 0.0f;

		lrg_paint_list_rect (list, x, y + dy, w, 1.5f, 0.0f, color);
	}
}

/* paint_diagnostics_row: squiggles of the diagnostics that touch this row */
static void
paint_diagnostics_row (LrgTextEditor *self,
                       Painter       *pt,
                       guint          line,
                       Line          *l,
                       guint          rs,
                       guint          re,
                       gfloat         y)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	guint     i;

	if (self->diagnostics == NULL)
		return;
	for (i = 0; i < self->diagnostics->len; i++)
	{
		LrgTextDiagnostic *d = g_ptr_array_index (self->diagnostics, i);
		guint              a_byte;
		guint              b_byte;
		guint              from;
		guint              to;
		LrgThemeColor      which;
		const GrlColor    *color;

		if (line < d->line || line > d->end_line)
			continue;
		a_byte = line == d->line ? char_to_byte (l, d->column) : 0;
		if (line == d->end_line)
			b_byte = char_to_byte (l, d->end_column);
		else
			b_byte = (guint) l->text->len;
		if (line == d->line && line == d->end_line && b_byte <= a_byte)
			b_byte = next_byte (l, a_byte);
		from = MAX (a_byte, rs);
		to = MIN (b_byte, re);
		if (to <= from && !(line == d->line && a_byte >= rs && a_byte <= re))
			continue;
		if (to <= from)
		{
			from = MIN (a_byte, re);
			to = MIN (next_byte (l, from), re);
			if (to <= from)
				to = from + 1;
		}
		switch (d->severity)
		{
		case LRG_TEXT_DIAGNOSTIC_WARNING:
			which = LRG_THEME_COLOR_WARNING;
			break;
		case LRG_TEXT_DIAGNOSTIC_INFO:
			which = LRG_THEME_COLOR_ACCENT;
			break;
		case LRG_TEXT_DIAGNOSTIC_HINT:
			which = LRG_THEME_COLOR_TEXT_MUTED;
			break;
		case LRG_TEXT_DIAGNOSTIC_ERROR:
		default:
			which = LRG_THEME_COLOR_DANGER;
			break;
		}
		color = lrg_theme_get_color (theme, which);
		paint_squiggle (pt->list,
		                pt->ox + (gfloat) col_at (self, l, from) * pt->g.cell -
		                    (gfloat) col_at (self, l, rs) * pt->g.cell,
		                pt->ox + (gfloat) col_at (self, l, to) * pt->g.cell -
		                    (gfloat) col_at (self, l, rs) * pt->g.cell,
		                y + pt->g.line_h - 3.0f, color);
	}
}

/* paint_overlays: completion popup and hover, outside the text clip */
static void
paint_overlays (LrgTextEditor *self,
                Painter       *pt,
                gfloat         wx,
                gfloat         wy)
{
	LrgWidget      *widget = LRG_WIDGET (self);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	const GrlColor *bg;
	const GrlColor *fg;
	const GrlColor *muted;
	const GrlColor *sel;
	gfloat          x;
	gfloat          y;
	gfloat          row_h;
	guint           i;
	guint           shown;
	guint           start;

	if (self->hover_text != NULL && self->hover_text[0] != '\0')
	{
		gfloat w = 0.0f;
		gfloat h = 0.0f;

		bg = lrg_theme_get_color (theme, LRG_THEME_COLOR_TOOLTIP);
		fg = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
		lrg_paint_measure_text (pt->g.font, self->hover_text, pt->g.size, 0.0f, &w, &h);
		w = MIN (w + 16.0f, pt->g.vw);
		h = MAX (h, pt->g.size) + 10.0f;
		x = wx + pt->g.vx + self->caret_x.value;
		y = wy + pt->g.vy + self->caret_y.value - h - 4.0f;
		if (y < wy)
			y = wy + pt->g.vy + self->caret_y.value + pt->g.line_h + 2.0f;
		lrg_paint_list_rect (pt->list, x, y, w, h, 4.0f, bg);
		lrg_paint_list_rect (pt->list, x, y, w, 1.0f, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
		lrg_paint_list_text (pt->list, pt->g.font, self->hover_text, x + 8.0f,
		                     y + (h - pt->g.size) * 0.5f, pt->g.size, 0.0f, fg);
	}

	if (!self->completion_visible || self->completions == NULL || self->completions->len == 0)
		return;

	bg = lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP);
	fg = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	sel = lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
	row_h = pt->g.line_h;
	shown = MIN (self->completions->len, 8);
	start = 0;
	if (self->completion_selected >= (gint) shown)
		start = (guint) self->completion_selected - shown + 1;
	x = wx + pt->g.vx + PAD_X;
	y = wy + pt->g.vy + self->caret_y.value + pt->g.line_h;
	if (y + row_h * (gfloat) shown > wy + lrg_widget_get_height (widget))
		y = wy + pt->g.vy + self->caret_y.value - row_h * (gfloat) shown;
	lrg_paint_list_rect (pt->list, x, y, MIN (360.0f, pt->g.vw), row_h * (gfloat) shown, 4.0f, bg);
	lrg_paint_list_rect (pt->list, x, y, MIN (360.0f, pt->g.vw), 1.0f, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	for (i = 0; i < shown && start + i < self->completions->len; i++)
	{
		LrgTextCompletion *item = g_ptr_array_index (self->completions, start + i);
		gfloat             ry = y + (gfloat) i * row_h;
		const gchar       *label = item->label != NULL ? item->label : "";

		if ((gint) (start + i) == self->completion_selected)
			lrg_paint_list_rect (pt->list, x + 2.0f, ry + 1.0f, MIN (356.0f, pt->g.vw - 4.0f),
			                     row_h - 2.0f, 3.0f, sel);
		lrg_paint_list_text (pt->list, pt->g.font, label, x + 8.0f,
		                     ry + (row_h - pt->g.size) * 0.5f, pt->g.size, 0.0f, fg);
		if (item->detail != NULL)
			lrg_paint_list_text (pt->list, pt->g.font, item->detail, x + 180.0f,
			                     ry + (row_h - pt->g.size) * 0.5f, pt->g.size, 0.0f, muted);
	}
}

/* paint_matches_row: find-match washes on one row */
static void
paint_matches_row (LrgTextEditor     *self,
                   Painter           *pt,
                   const LrgPartLook *look,
                   Line              *l,
                   guint              rs,
                   guint              re,
                   gfloat             y)
{
	gsize  n = strlen (self->needle);
	guint  end = 0;
	gssize m;

	/*
	 * A case-sensitive match is exactly n bytes, so one that reaches
	 * into this row starts at most n - 1 bytes before it. Folded
	 * matches can be longer or shorter than the needle: scan the line
	 * from its start (the fold is cached per line, so this is cheap).
	 */
	m = match_in (self, l, self->needle_case && rs > n ? rs - (guint) n + 1 : 0, &end);
	while (m >= 0 && (guint) m < re)
	{
		guint a = MAX ((guint) m, rs);
		guint b = MIN (end, re);

		if (b > a)
			paint_cells (self, pt, look, l, rs, a, b, 0.0f, y);
		m = match_in (self, l, (guint) m + 1, &end);
	}
}

/*
 * lrg_text_editor_paint:
 *
 * 1. Frame (BOX) and, with line numbers, the gutter (HEADER).
 * 2. Visible rows only: make sure their lines have highlight spans
 *    (lines above get state only, once).
 * 3. Clipped to the text viewport, per row: current-line band (ITEM),
 *    find matches (INDICATOR, checked), selection (FILL), bracket pair
 *    (INDICATOR), the text in token colours; then the placeholder
 *    (LABEL) and the caret (MARK) on its glide springs.
 * 4. Overlay scrollbars.
 */
static void
lrg_text_editor_paint (LrgWidget    *widget,
                       LrgPaintList *list)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (widget);
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	gboolean       focused = (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUSED) != 0;
	gfloat         wx = lrg_widget_get_world_x (widget);
	gfloat         wy = lrg_widget_get_world_y (widget);
	LrgPartLook    box;
	LrgPartLook    band;
	LrgPartLook    sel;
	LrgPartLook    match;
	LrgPartLook    bracket;
	Painter        pt;
	gfloat         sy;
	gfloat         sx;
	gfloat         top;
	guint          row0;
	guint          row1;
	guint          line0;
	guint          line1;
	guint          sub;
	guint          row;
	guint          t;

	if (self->ay.bar == NULL)
		return;

	/* 0. Geometry, extents and caret targets for this allocation */
	pt.list = list;
	geometry (self, &pt.g);
	update_extent (self, &pt.g);
	sync_view (self, &pt.g);

	/* 1. Frame */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, wx, wy, lrg_widget_get_width (widget),
	                      lrg_widget_get_height (widget));
	lrg_paint_list_push_opacity (list, box.opacity > 0.0f ? MIN (box.opacity, 1.0f) : 1.0f);

	/* 2. Visible rows and their highlighting */
	sy = lrg_scroll_axis_get_value (&self->ay);
	sx = lrg_scroll_axis_get_value (&self->ax);
	top = wy + pt.g.vy + PAD_Y - sy;
	pt.ox = wx + pt.g.vx + PAD_X - sx;
	pt.oy = top;
	row0 = (guint) MAX (0.0f, floorf ((sy - PAD_Y) / pt.g.line_h));
	row1 = (guint) MAX (0.0f, floorf ((sy - PAD_Y + pt.g.vh) / pt.g.line_h));
	row1 = MIN (row1, self->n_rows - 1);
	row0 = MIN (row0, row1);
	line0 = line_of_row (self, row0, &sub);
	line1 = line_of_row (self, row1, &sub);
	ensure_states (self, line1, line0);

	for (t = 0; t < LRG_SYNTAX_N_TOKENS; t++)
	{
		if (self->token_colors_set & (1u << t))
			pt.colors[t] = self->token_colors[t];
		else if (t == LRG_SYNTAX_TOKEN_TEXT)
			pt.colors[t] = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
		else
			lrg_syntax_get_token_color (self->syntax, theme, (LrgSyntaxToken) t, &pt.colors[t]);
	}

	if (pt.g.gutter > 0.0f)
		paint_gutter (self, &pt, wx, wy, row0, row1, top);

	/* 3. Text viewport */
	lrg_paint_list_push_clip (list, wx + pt.g.vx, wy + pt.g.vy, pt.g.vw, pt.g.vh);

	lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &band);
	lrg_widget_resolve_look (widget, LRG_UI_PART_FILL, &sel);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, LRG_WIDGET_STATE_CHECKED, &match);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, LRG_WIDGET_STATE_NONE, &bracket);
	update_brackets (self);

	/* 3a. Current-line band behind every row of the caret line */
	if (self->highlight_line && !has_selection (self))
	{
		Line  *cl = get_line (self, self->cursor.line);
		guint  fr = first_row (self, self->cursor.line);
		gfloat y = top + (gfloat) fr * pt.g.line_h;

		layout_line (self, cl);
		lrg_widget_paint_box (widget, list, &band, wx + pt.g.vx, y, pt.g.vw,
		                      (gfloat) line_rows (cl) * pt.g.line_h);
	}

	/* 3a2. Stopped-line band (ITEM, selected), independent of the caret */
	if (self->execution_line >= 0 && (guint) self->execution_line < n_lines (self))
	{
		LrgPartLook exec;
		Line       *el = get_line (self, (guint) self->execution_line);
		guint       fr = first_row (self, (guint) self->execution_line);
		gfloat      y = top + (gfloat) fr * pt.g.line_h;

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_SELECTED, &exec);
		layout_line (self, el);
		lrg_widget_paint_box (widget, list, &exec, wx + pt.g.vx, y, pt.g.vw,
		                      (gfloat) line_rows (el) * pt.g.line_h);
	}

	for (row = row0; row <= row1; row++)
	{
		guint   r;
		guint   line = line_of_row (self, row, &r);
		Line   *l = get_line (self, line);
		gfloat  y = top + (gfloat) row * pt.g.line_h;
		guint   rs;
		guint   re;

		layout_line (self, l);
		row_bounds (l, r, &rs, &re);

		if (self->needle != NULL)
			paint_matches_row (self, &pt, &match, l, rs, re, y);
		if (has_selection (self))
			paint_selection_row (self, &pt, &sel, line, l, rs, re, r + 1 == line_rows (l), y);
		if (self->bracket_found && focused)
		{
			if (self->bracket_a.line == line && self->bracket_a.byte >= rs && self->bracket_a.byte < re)
				paint_cells (self, &pt, &bracket, l, rs, self->bracket_a.byte,
				             next_byte (l, self->bracket_a.byte), 0.0f, y);
			if (self->bracket_b.line == line && self->bracket_b.byte >= rs && self->bracket_b.byte < re)
				paint_cells (self, &pt, &bracket, l, rs, self->bracket_b.byte,
				             next_byte (l, self->bracket_b.byte), 0.0f, y);
		}
		paint_row_text (self, &pt, l, line_spans (self, line), rs, re,
		                y + (pt.g.line_h - pt.g.size) * 0.5f);
		paint_diagnostics_row (self, &pt, line, l, rs, re, y);
	}

	/* 3b. Placeholder while empty */
	if (self->placeholder != NULL && n_lines (self) == 1 && get_line (self, 0)->text->len == 0)
	{
		LrgPartLook label;

		lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
		lrg_paint_list_text (list, pt.g.font, self->placeholder, pt.ox + CARET_WIDTH * 2.0f,
		                     top + (pt.g.line_h - pt.g.size) * 0.5f, pt.g.size, 0.0f, &label.foreground);
	}

	/* 3c. Caret on its glide springs, eased blink */
	if (focused && !self->read_only && lrg_widget_get_enabled (widget))
	{
		LrgPartLook caret;
		GrlColor    c;
		gfloat      cw;

		lrg_widget_resolve_look (widget, LRG_UI_PART_MARK, &caret);
		cw = caret.size > 0.0f ? caret.size : CARET_WIDTH;
		lrg_paint_color_with_alpha (&caret.foreground,
		                            ((gfloat) caret.foreground.a / 255.0f) * caret_alpha (self), &c);
		lrg_paint_list_rect (list, wx + pt.g.vx + self->caret_x.value - sx - cw * 0.5f,
		                     wy + pt.g.vy + self->caret_y.value - sy + 1.0f, cw, pt.g.line_h - 2.0f,
		                     cw * 0.5f, &c);
	}

	lrg_paint_list_pop_clip (list);
	lrg_paint_list_pop_opacity (list);

	/* 4. Overlay scrollbars */
	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
	if (!self->wrap)
		lrg_scroll_bar_paint (widget, list, &self->ax, FALSE, self->fx.fade.value);

	/* 5. Completion and hover sit above the scrollbars */
	paint_overlays (self, &pt, wx, wy);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* pos_at_point: the character boundary nearest a world point (clamped) */
static Pos
pos_at_point (LrgTextEditor *self,
              gfloat         x,
              gfloat         y)
{
	Geo    g;
	gfloat rel_y;
	gfloat rel_x;
	gint64 row;
	guint  sub;
	guint  line;

	geometry (self, &g);
	ensure_rows (self);
	rel_y = y - (lrg_widget_get_world_y (LRG_WIDGET (self)) + g.vy + PAD_Y) + lrg_scroll_axis_get_value (&self->ay);
	rel_x = x - (lrg_widget_get_world_x (LRG_WIDGET (self)) + g.vx + PAD_X) + lrg_scroll_axis_get_value (&self->ax);
	row = (gint64) floorf (rel_y / g.line_h);
	if (row < 0)
		return pos_make (0, 0);
	if (row >= (gint64) self->n_rows)
		return doc_end (self);
	line = line_of_row (self, (guint) row, &sub);
	return pos_make (line, byte_at_x (self, get_line (self, line), sub, rel_x, g.cell));
}

/*
 * extend_drag:
 *
 * While dragging, the selection runs from the press to @p in the
 * press's granularity: characters, whole words (double click) or whole
 * lines (triple click).
 */
static void
extend_drag (LrgTextEditor *self,
             Pos            p)
{
	Pos a;
	Pos b;

	switch (self->drag)
	{
	case DRAG_WORD:
		word_bounds (self, p, &a, &b);
		break;
	case DRAG_LINE:
		line_bounds (self, p.line, &a, &b);
		break;
	case DRAG_CHAR:
	case DRAG_NONE:
	default:
		move_to (self, p, TRUE);
		return;
	}
	if (pos_cmp (a, self->drag_start) < 0)
	{
		self->anchor = self->drag_end;
		self->cursor = a;
	}
	else
	{
		self->anchor = self->drag_start;
		self->cursor = pos_cmp (b, self->drag_end) > 0 ? b : self->drag_end;
	}
	self->break_coalesce = TRUE;
}

/* line_at_y: the document line under a world y (clamped) */
static guint
line_at_y (LrgTextEditor *self,
           gfloat         y)
{
	Geo    g;
	gfloat rel_y;
	gint64 row;
	guint  sub;

	geometry (self, &g);
	ensure_rows (self);
	rel_y = y - (lrg_widget_get_world_y (LRG_WIDGET (self)) + g.vy + PAD_Y) +
	        lrg_scroll_axis_get_value (&self->ay);
	row = (gint64) floorf (rel_y / g.line_h);
	if (row < 0)
		return 0;
	if (self->n_rows == 0 || row >= (gint64) self->n_rows)
		return n_lines (self) - 1;
	return line_of_row (self, (guint) row, &sub);
}

/*
 * handle_press:
 *
 * Counts multi-clicks on the canvas clock: one places the caret (Shift
 * extends), two select a word, three a line; the drag that follows
 * extends in the same unit. A click in the marker column emits
 * ::gutter-clicked. Ctrl+click emits ::goto-definition when a handler
 * is connected.
 */
static void
handle_press (LrgTextEditor     *self,
              gfloat             x,
              gfloat             y,
              LrgInputModifiers  mods)
{
	Geo   g;
	gfloat wx;
	Pos   p;

	geometry (self, &g);
	wx = lrg_widget_get_world_x (LRG_WIDGET (self));
	if (self->show_gutter_marks && x >= wx + g.border && x < wx + g.border + MARKER_W)
	{
		g_signal_emit (self, signals[SIGNAL_GUTTER_CLICKED], 0, line_at_y (self, y));
		return;
	}
	p = pos_at_point (self, x, y);
	if ((mods & LRG_INPUT_MODIFIER_CTRL) != 0 &&
	    g_signal_has_handler_pending (G_OBJECT (self), signals[SIGNAL_GOTO_DEFINITION], 0, FALSE))
	{
		g_signal_emit (self, signals[SIGNAL_GOTO_DEFINITION], 0, p.line,
		               byte_to_char (get_line (self, p.line), p.byte));
		return;
	}

	if (self->clicks > 0 && self->clock - self->last_click_time <= MULTI_CLICK_TIME &&
	    fabsf (x - self->last_click_x) <= MULTI_CLICK_SLOP && fabsf (y - self->last_click_y) <= MULTI_CLICK_SLOP)
		self->clicks = self->clicks % 3 + 1;
	else
		self->clicks = 1;
	self->last_click_time = self->clock;
	self->last_click_x = x;
	self->last_click_y = y;

	switch (self->clicks)
	{
	case 2:
		word_bounds (self, p, &self->drag_start, &self->drag_end);
		self->anchor = self->drag_start;
		self->cursor = self->drag_end;
		self->drag = DRAG_WORD;
		break;
	case 3:
		line_bounds (self, p.line, &self->drag_start, &self->drag_end);
		self->anchor = self->drag_start;
		self->cursor = self->drag_end;
		self->drag = DRAG_LINE;
		break;
	default:
		move_to (self, p, (mods & LRG_INPUT_MODIFIER_SHIFT) != 0);
		self->drag = DRAG_CHAR;
		break;
	}
	self->goal_x = -1.0f;
	self->break_coalesce = TRUE;
	flush (self, TRUE);
}

/* submit: emit ::submit */
static void
submit (LrgTextEditor *self)
{
	g_signal_emit (self, signals[SIGNAL_SUBMIT], 0);
}

/*
 * handle_key:
 *
 * Navigation and editing keys. Everything the editor uses returns
 * TRUE so the canvas neither moves focus (arrows), activates (Enter,
 * Space) nor fires shortcuts for plain typing. Printable keys are
 * consumed only without Ctrl, Alt or Super (AltGr, Ctrl+Alt together,
 * still types), so Ctrl / Alt / Super chords the editor does not
 * implement (Ctrl+S, Alt+F ...) reach the application's shortcuts; the
 * chords it implements (Ctrl+A/C/X/V/Z/Y, Ctrl+Insert, Shift+Insert)
 * are consumed. Escape only clears a selection.
 */
static gboolean
handle_key (LrgTextEditor     *self,
            GrlKey             key,
            LrgInputModifiers  mods)
{
	gboolean shift = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;
	gboolean ctrl = (mods & LRG_INPUT_MODIFIER_CTRL) != 0;
	gboolean alt = (mods & LRG_INPUT_MODIFIER_ALT) != 0;
	gboolean edit = user_can_edit (self);
	gboolean keep_goal = FALSE;
	/* Plain (or Shift) keys type; so does AltGr (Ctrl+Alt together).
	 * Any other Ctrl, Alt or Super chord belongs to the application's
	 * shortcuts unless the editor implements it below. */
	gboolean typing = !(mods & LRG_INPUT_MODIFIER_SUPER) && (ctrl == alt);
	Pos      a;
	Pos      b;

	if (self->completion_visible && !ctrl && !alt)
	{
		if (key == GRL_KEY_UP)
		{
			lrg_text_editor_completion_move (self, -1);
			return TRUE;
		}
		if (key == GRL_KEY_DOWN)
		{
			lrg_text_editor_completion_move (self, 1);
			return TRUE;
		}
		if (key == GRL_KEY_ENTER || key == GRL_KEY_KP_ENTER || key == GRL_KEY_TAB)
		{
			lrg_text_editor_completion_accept (self);
			return TRUE;
		}
		if (key == GRL_KEY_ESCAPE)
		{
			lrg_text_editor_hide_completion (self);
			return TRUE;
		}
	}

	sel_range (self, &a, &b);

	switch (key)
	{
	/* ---- navigation ---- */
	case GRL_KEY_LEFT:
		if (has_selection (self) && !shift && !ctrl)
			move_to (self, a, FALSE);
		else if (ctrl)
			move_to (self, word_left (self, self->cursor), shift);
		else if (self->cursor.byte > 0)
			move_to (self, pos_make (self->cursor.line,
			                         prev_byte (get_line (self, self->cursor.line), self->cursor.byte)), shift);
		else if (self->cursor.line > 0)
			move_to (self, pos_make (self->cursor.line - 1,
			                         (guint) get_line (self, self->cursor.line - 1)->text->len), shift);
		else
			move_to (self, self->cursor, shift);
		break;

	case GRL_KEY_RIGHT:
		if (has_selection (self) && !shift && !ctrl)
			move_to (self, b, FALSE);
		else if (ctrl)
			move_to (self, word_right (self, self->cursor), shift);
		else if (self->cursor.byte < get_line (self, self->cursor.line)->text->len)
			move_to (self, pos_make (self->cursor.line,
			                         next_byte (get_line (self, self->cursor.line), self->cursor.byte)), shift);
		else if (self->cursor.line + 1 < n_lines (self))
			move_to (self, pos_make (self->cursor.line + 1, 0), shift);
		else
			move_to (self, self->cursor, shift);
		break;

	case GRL_KEY_UP:
		if (ctrl)
		{
			Geo g;

			geometry (self, &g);
			lrg_scroll_axis_wheel (&self->ay, -g.line_h, smooth (self));
			return TRUE;
		}
		move_rows (self, -1, shift);
		keep_goal = TRUE;
		break;

	case GRL_KEY_DOWN:
		if (ctrl)
		{
			Geo g;

			geometry (self, &g);
			lrg_scroll_axis_wheel (&self->ay, g.line_h, smooth (self));
			return TRUE;
		}
		move_rows (self, 1, shift);
		keep_goal = TRUE;
		break;

	case GRL_KEY_PAGE_UP:
	case GRL_KEY_PAGE_DOWN:
		{
			gint page = rows_per_page (self);
			Geo  g;

			geometry (self, &g);
			if (key == GRL_KEY_PAGE_UP)
				page = -page;
			move_rows (self, page, shift);
			/* the view moves by the same amount, keeping the caret's place */
			lrg_scroll_axis_scroll_to (&self->ay, self->ay.pos.target + (gfloat) page * g.line_h, smooth (self));
			keep_goal = TRUE;
			break;
		}

	case GRL_KEY_HOME:
		move_to (self, ctrl ? pos_make (0, 0) : home_pos (self), shift);
		break;

	case GRL_KEY_END:
		move_to (self, ctrl ? doc_end (self) : end_pos (self), shift);
		break;

	case GRL_KEY_ESCAPE:
		if (!has_selection (self))
			return FALSE;
		move_to (self, self->cursor, FALSE);
		break;

	/* ---- editing ---- */
	case GRL_KEY_BACKSPACE:
		if (edit)
			backspace (self, ctrl);
		return TRUE;

	case GRL_KEY_DELETE:
		if (edit)
			delete_forward (self, ctrl);
		return TRUE;

	case GRL_KEY_ENTER:
	case GRL_KEY_KP_ENTER:
		if (self->submit_on_enter ? !shift : ctrl)
		{
			submit (self);
			return TRUE;
		}
		if (edit)
			newline (self);
		return TRUE;

	case GRL_KEY_TAB:
		if (!edit)
			return FALSE;
		if (shift)
			indent_lines (self, TRUE);
		else
			tab_key (self);
		return TRUE;

	case GRL_KEY_A:
		if (!ctrl || alt)
			return typing;
		lrg_text_editor_select_all (self);
		return TRUE;

	case GRL_KEY_C:
	case GRL_KEY_INSERT:
		if (key == GRL_KEY_C && (!ctrl || alt))
			return typing;
		if (key == GRL_KEY_INSERT && !ctrl)
		{
			if (shift)
				lrg_text_editor_paste_clipboard (self);
			return TRUE;
		}
		lrg_text_editor_copy_clipboard (self);
		return TRUE;

	case GRL_KEY_X:
		if (!ctrl || alt)
			return typing;
		lrg_text_editor_cut_clipboard (self);
		return TRUE;

	case GRL_KEY_V:
		if (!ctrl || alt)
			return typing;
		lrg_text_editor_paste_clipboard (self);
		return TRUE;

	case GRL_KEY_Z:
		if (!ctrl || alt)
			return typing;
		if (edit)
		{
			if (shift)
				lrg_text_editor_redo (self);
			else
				lrg_text_editor_undo (self);
		}
		return TRUE;

	case GRL_KEY_Y:
		if (!ctrl || alt)
			return typing;
		if (edit)
			lrg_text_editor_redo (self);
		return TRUE;

	case GRL_KEY_F9:
		if (self->show_gutter_marks &&
		    g_signal_has_handler_pending (G_OBJECT (self), signals[SIGNAL_GUTTER_CLICKED], 0, FALSE))
		{
			g_signal_emit (self, signals[SIGNAL_GUTTER_CLICKED], 0, self->cursor.line);
			return TRUE;
		}
		return FALSE;

	case GRL_KEY_F12:
		if (g_signal_has_handler_pending (G_OBJECT (self), signals[SIGNAL_GOTO_DEFINITION], 0, FALSE))
		{
			Line *l = get_line (self, self->cursor.line);

			g_signal_emit (self, signals[SIGNAL_GOTO_DEFINITION], 0, self->cursor.line,
			               byte_to_char (l, self->cursor.byte));
			return TRUE;
		}
		return FALSE;

	default:
		/*
		 * Printable keys without Ctrl are typing: swallowed so they do
		 * not trigger shortcuts or activate; the character arrives as
		 * a TEXT_INPUT event. Ctrl combinations go to the application.
		 */
		return typing && key >= 32 && key <= 126;
	}

	if (!keep_goal)
		self->goal_x = -1.0f;
	flush (self, TRUE);
	return TRUE;
}

static gboolean
lrg_text_editor_handle_event (LrgWidget        *widget,
                              const LrgUIEvent *event)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (widget);
	LrgWidget     *target = lrg_ui_event_get_target (event);
	gfloat         x = lrg_ui_event_get_x (event);
	gfloat         y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		lrg_scroll_fx_poke (&self->fx);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		lrg_scroll_fx_poke (&self->fx);
		if (self->ay.dragging)
		{
			lrg_scroll_bar_drag (&self->ay, TRUE, y);
			return TRUE;
		}
		if (self->ax.dragging)
		{
			lrg_scroll_bar_drag (&self->ax, FALSE, x);
			return TRUE;
		}
		if (self->drag != DRAG_NONE)
		{
			extend_drag (self, pos_at_point (self, x, y));
			flush (self, TRUE);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		refresh (self);
		if (target == self->ay.bar)
		{
			lrg_scroll_bar_press (&self->ay, TRUE, y, smooth (self));
			lrg_scroll_fx_poke (&self->fx);
			return TRUE;
		}
		if (target == self->ax.bar)
		{
			lrg_scroll_bar_press (&self->ax, FALSE, x, smooth (self));
			lrg_scroll_fx_poke (&self->fx);
			return TRUE;
		}
		handle_press (self, x, y, lrg_ui_event_get_modifiers (event));
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->ay.dragging = FALSE;
		self->ax.dragging = FALSE;
		self->drag = DRAG_NONE;
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		{
			gboolean handled;
			gfloat   dy = lrg_ui_event_get_scroll_y (event);
			gfloat   dx = lrg_ui_event_get_scroll_x (event);

			refresh (self);
			/* Shift+wheel scrolls sideways */
			if (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_SHIFT)
			{
				dx = dy;
				dy = 0.0f;
			}
			handled = dy != 0.0f && lrg_scroll_axis_wheel (&self->ay, -dy * LRG_SCROLL_WHEEL_STEP, smooth (self));
			if (dx != 0.0f && !self->wrap)
				handled = lrg_scroll_axis_wheel (&self->ax, -dx * LRG_SCROLL_WHEEL_STEP, smooth (self)) || handled;
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_PAN:
		{
			gboolean handled;

			refresh (self);
			handled = lrg_scroll_axis_pan (&self->ay, lrg_ui_event_get_delta_y (event));
			if (!self->wrap)
				handled = lrg_scroll_axis_pan (&self->ax, lrg_ui_event_get_delta_x (event)) || handled;
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_PAN_END:
		lrg_scroll_axis_flick (&self->ay, smooth (self) ? lrg_ui_event_get_velocity_y (event) : 0.0f);
		if (!self->wrap)
			lrg_scroll_axis_flick (&self->ax, smooth (self) ? lrg_ui_event_get_velocity_x (event) : 0.0f);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		refresh (self);
		return handle_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));

	case LRG_UI_EVENT_TEXT_INPUT:
		{
			LrgInputModifiers mods = lrg_ui_event_get_modifiers (event);
			gunichar          ch = lrg_ui_event_get_codepoint (event);
			gchar             buf[8];
			gint              len;

			if (!user_can_edit (self))
				return FALSE;
			/* Ctrl / Super chords are not text (AltGr = Ctrl+Alt still is) */
			if (((mods & LRG_INPUT_MODIFIER_CTRL) && !(mods & LRG_INPUT_MODIFIER_ALT)) ||
			    (mods & LRG_INPUT_MODIFIER_SUPER))
				return FALSE;
			if (ch == 0 || g_unichar_iscntrl (ch) || !g_unichar_validate (ch))
				return FALSE;
			len = g_unichar_to_utf8 (ch, buf);
			buf[len] = '\0';
			replace_selection (self, STEP_TYPE, buf);
			return TRUE;
		}

	case LRG_UI_EVENT_ACTIVATE:
		/* Enter is an editing key; a gamepad A keeps focus */
		return TRUE;

	case LRG_UI_EVENT_FOCUS_IN:
		self->activity_time = self->clock;
		self->break_coalesce = TRUE;
		return TRUE;

	case LRG_UI_EVENT_FOCUS_OUT:
		self->drag = DRAG_NONE;
		self->break_coalesce = TRUE;
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_text_editor_constructed (GObject *object)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (object);

	G_OBJECT_CLASS (lrg_text_editor_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	self->ax.bar = lrg_scroll_bar_new ("hscrollbar");
	lrg_container_add_child (LRG_CONTAINER (self), self->ay.bar);
	lrg_container_add_child (LRG_CONTAINER (self), self->ax.bar);
	self->in_layout = FALSE;
}

static void
lrg_text_editor_dispose (GObject *object)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (object);

	g_clear_object (&self->ay.bar);
	g_clear_object (&self->ax.bar);
	g_clear_object (&self->syntax);

	G_OBJECT_CLASS (lrg_text_editor_parent_class)->dispose (object);
}

static void
lrg_text_editor_finalize (GObject *object)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (object);

	g_ptr_array_unref (self->lines);
	g_ptr_array_unref (self->undo);
	g_ptr_array_unref (self->redo);
	g_array_unref (self->row_starts);
	g_string_free (self->scratch, TRUE);
	g_free (self->language);
	g_free (self->placeholder);
	g_free (self->font);
	g_free (self->needle);
	g_string_free (self->needle_fold, TRUE);
	g_string_free (self->fold_text, TRUE);
	g_array_unref (self->fold_map);
	g_array_unref (self->bracket_spans);
	g_clear_pointer (&self->gutter_marks, g_hash_table_unref);
	g_clear_pointer (&self->diagnostics, g_ptr_array_unref);
	g_clear_pointer (&self->completions, g_ptr_array_unref);
	g_free (self->hover_text);

	G_OBJECT_CLASS (lrg_text_editor_parent_class)->finalize (object);
}

static void
lrg_text_editor_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (object);

	switch (prop_id)
	{
	case PROP_TEXT:
		g_value_take_string (value, lrg_text_editor_get_text (self));
		break;
	case PROP_LANGUAGE:
		g_value_set_string (value, self->language);
		break;
	case PROP_SYNTAX:
		g_value_set_object (value, self->syntax);
		break;
	case PROP_SHOW_LINE_NUMBERS:
		g_value_set_boolean (value, self->show_line_numbers);
		break;
	case PROP_WRAP:
		g_value_set_boolean (value, self->wrap);
		break;
	case PROP_READ_ONLY:
		g_value_set_boolean (value, self->read_only);
		break;
	case PROP_TAB_WIDTH:
		g_value_set_uint (value, self->tab_width);
		break;
	case PROP_INSERT_SPACES:
		g_value_set_boolean (value, self->insert_spaces);
		break;
	case PROP_PLACEHOLDER:
		g_value_set_string (value, self->placeholder);
		break;
	case PROP_SUBMIT_ON_ENTER:
		g_value_set_boolean (value, self->submit_on_enter);
		break;
	case PROP_MAX_LINES:
		g_value_set_uint (value, self->max_lines);
		break;
	case PROP_FONT:
		g_value_set_string (value, lrg_text_editor_get_font (self));
		break;
	case PROP_FONT_SIZE:
		g_value_set_float (value, self->font_size);
		break;
	case PROP_HIGHLIGHT_CURRENT_LINE:
		g_value_set_boolean (value, self->highlight_line);
		break;
	case PROP_AUTO_INDENT:
		g_value_set_boolean (value, self->auto_indent);
		break;
	case PROP_SHOW_GUTTER_MARKS:
		g_value_set_boolean (value, self->show_gutter_marks);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_text_editor_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgTextEditor *self = LRG_TEXT_EDITOR (object);

	switch (prop_id)
	{
	case PROP_TEXT:
		lrg_text_editor_set_text (self, g_value_get_string (value));
		break;
	case PROP_LANGUAGE:
		lrg_text_editor_set_language (self, g_value_get_string (value));
		break;
	case PROP_SYNTAX:
		lrg_text_editor_set_syntax (self, g_value_get_object (value));
		break;
	case PROP_SHOW_LINE_NUMBERS:
		lrg_text_editor_set_show_line_numbers (self, g_value_get_boolean (value));
		break;
	case PROP_WRAP:
		lrg_text_editor_set_wrap (self, g_value_get_boolean (value));
		break;
	case PROP_READ_ONLY:
		lrg_text_editor_set_read_only (self, g_value_get_boolean (value));
		break;
	case PROP_TAB_WIDTH:
		lrg_text_editor_set_tab_width (self, g_value_get_uint (value));
		break;
	case PROP_INSERT_SPACES:
		lrg_text_editor_set_insert_spaces (self, g_value_get_boolean (value));
		break;
	case PROP_PLACEHOLDER:
		lrg_text_editor_set_placeholder (self, g_value_get_string (value));
		break;
	case PROP_SUBMIT_ON_ENTER:
		lrg_text_editor_set_submit_on_enter (self, g_value_get_boolean (value));
		break;
	case PROP_MAX_LINES:
		lrg_text_editor_set_max_lines (self, g_value_get_uint (value));
		break;
	case PROP_FONT:
		lrg_text_editor_set_font (self, g_value_get_string (value));
		break;
	case PROP_FONT_SIZE:
		lrg_text_editor_set_font_size (self, g_value_get_float (value));
		break;
	case PROP_HIGHLIGHT_CURRENT_LINE:
		lrg_text_editor_set_highlight_current_line (self, g_value_get_boolean (value));
		break;
	case PROP_AUTO_INDENT:
		lrg_text_editor_set_auto_indent (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_GUTTER_MARKS:
		lrg_text_editor_set_show_gutter_marks (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_text_editor_class_init (LrgTextEditorClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->constructed = lrg_text_editor_constructed;
	object_class->dispose = lrg_text_editor_dispose;
	object_class->finalize = lrg_text_editor_finalize;
	object_class->get_property = lrg_text_editor_get_property;
	object_class->set_property = lrg_text_editor_set_property;

	widget_class->measure = lrg_text_editor_measure;
	widget_class->paint = lrg_text_editor_paint;
	widget_class->step = lrg_text_editor_step;
	widget_class->style_defaults = lrg_text_editor_style_defaults;
	widget_class->handle_event = lrg_text_editor_handle_event;

	container_class->layout_children = lrg_text_editor_layout_children;

	/* Code and logs are laid out left to right; the scrollbar zones are
	 * placed for that, so the container must not mirror them in RTL */
	lrg_widget_class_set_handles_direction (widget_class, TRUE);
	/* Tab indents (KEY_DOWN reaches us before focus moves); read-only
	 * editors return FALSE for it so focus can leave, as does Ctrl+Tab */
	lrg_widget_class_set_accepts_tab (widget_class, TRUE);

	/**
	 * LrgTextEditor:text:
	 *
	 * The whole text, lines joined with '\n'. Setting it resets the
	 * caret, selection, undo history and scroll position.
	 */
	properties[PROP_TEXT] =
		g_param_spec_string ("text", "Text", "The edited text", "", rw);

	/**
	 * LrgTextEditor:language:
	 *
	 * Name of the highlighting language (lrg_syntax_get_for_name()),
	 * %NULL for plain text. Kept in step with #LrgTextEditor:syntax.
	 */
	properties[PROP_LANGUAGE] =
		g_param_spec_string ("language", "Language", "Highlighting language name", NULL, rw);

	/**
	 * LrgTextEditor:syntax:
	 *
	 * The highlighter, %NULL for plain text.
	 */
	properties[PROP_SYNTAX] =
		g_param_spec_object ("syntax", "Syntax", "Highlighter", LRG_TYPE_SYNTAX, rw);

	/**
	 * LrgTextEditor:show-line-numbers:
	 *
	 * Whether a line-number gutter is shown on the left.
	 */
	properties[PROP_SHOW_LINE_NUMBERS] =
		g_param_spec_boolean ("show-line-numbers", "Show line numbers", "Show the gutter", FALSE, rw);

	/**
	 * LrgTextEditor:wrap:
	 *
	 * Whether long lines wrap at word boundaries; otherwise the view
	 * scrolls horizontally.
	 */
	properties[PROP_WRAP] =
		g_param_spec_boolean ("wrap", "Wrap", "Wrap long lines", FALSE, rw);

	/**
	 * LrgTextEditor:read-only:
	 *
	 * Whether the user may edit. Selection and copying still work and
	 * the program can still change the text.
	 */
	properties[PROP_READ_ONLY] =
		g_param_spec_boolean ("read-only", "Read only", "Block user editing", FALSE, rw);

	/**
	 * LrgTextEditor:tab-width:
	 *
	 * Columns per tab stop.
	 */
	properties[PROP_TAB_WIDTH] =
		g_param_spec_uint ("tab-width", "Tab width", "Columns per tab stop", 1, 16, 4, rw);

	/**
	 * LrgTextEditor:insert-spaces:
	 *
	 * Whether Tab (and indentation) uses spaces instead of a tab.
	 */
	properties[PROP_INSERT_SPACES] =
		g_param_spec_boolean ("insert-spaces", "Insert spaces", "Indent with spaces", FALSE, rw);

	/**
	 * LrgTextEditor:placeholder:
	 *
	 * Hint shown in TEXT_MUTED while the editor is empty.
	 */
	properties[PROP_PLACEHOLDER] =
		g_param_spec_string ("placeholder", "Placeholder", "Hint while empty", NULL, rw);

	/**
	 * LrgTextEditor:submit-on-enter:
	 *
	 * Enter emits #LrgTextEditor::submit and Shift+Enter inserts a
	 * newline (chat boxes, consoles). Otherwise Enter inserts a newline
	 * and Ctrl+Enter submits.
	 */
	properties[PROP_SUBMIT_ON_ENTER] =
		g_param_spec_boolean ("submit-on-enter", "Submit on Enter", "Enter submits", FALSE, rw);

	/**
	 * LrgTextEditor:max-lines:
	 *
	 * Keep at most this many lines, dropping the oldest (consoles,
	 * logs); 0 for no limit.
	 */
	properties[PROP_MAX_LINES] =
		g_param_spec_uint ("max-lines", "Max lines", "Line limit, 0 for none", 0, G_MAXUINT, 0, rw);

	/**
	 * LrgTextEditor:font:
	 *
	 * Font name in the default #LrgFontManager ("ui-mono" by default).
	 */
	properties[PROP_FONT] =
		g_param_spec_string ("font", "Font", "Font name", "ui-mono", rw);

	/**
	 * LrgTextEditor:font-size:
	 *
	 * Font size; 0 uses the theme's FONT_SIZE.
	 */
	properties[PROP_FONT_SIZE] =
		g_param_spec_float ("font-size", "Font size", "Font size, 0 for the theme's", 0.0f, 512.0f, 0.0f, rw);

	/**
	 * LrgTextEditor:highlight-current-line:
	 *
	 * Whether the caret line gets a faint band (ITEM part).
	 */
	properties[PROP_HIGHLIGHT_CURRENT_LINE] =
		g_param_spec_boolean ("highlight-current-line", "Highlight current line",
		                      "Tint the caret line", TRUE, rw);

	/**
	 * LrgTextEditor:auto-indent:
	 *
	 * Whether Enter copies the indentation of the current line.
	 */
	properties[PROP_AUTO_INDENT] =
		g_param_spec_boolean ("auto-indent", "Auto indent", "Keep indentation on Enter", TRUE, rw);

	/**
	 * LrgTextEditor:show-gutter-marks:
	 *
	 * Whether the gutter reserves a column for breakpoint markers.
	 * Off by default, so editors that only show line numbers keep
	 * their layout.
	 */
	properties[PROP_SHOW_GUTTER_MARKS] =
		g_param_spec_boolean ("show-gutter-marks", "Show gutter marks",
		                      "Reserve a column for breakpoint markers", FALSE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTextEditor::changed:
	 * @self: the editor
	 *
	 * The text changed (typing, editing, undo, or the API).
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgTextEditor::cursor-moved:
	 * @self: the editor
	 * @line: the caret line
	 * @column: the caret column in characters
	 *
	 * The caret moved.
	 */
	signals[SIGNAL_CURSOR_MOVED] =
		g_signal_new ("cursor-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	/**
	 * LrgTextEditor::submit:
	 * @self: the editor
	 *
	 * Enter was pressed with #LrgTextEditor:submit-on-enter (or
	 * Ctrl+Enter without it). Typical handlers read the text, then
	 * clear it with lrg_text_editor_set_text().
	 */
	signals[SIGNAL_SUBMIT] =
		g_signal_new ("submit", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgTextEditor::gutter-clicked:
	 * @self: the editor
	 * @line: the 0-based line under the click (or the caret line for F9)
	 *
	 * The marker column was clicked, or F9 was pressed while a handler
	 * is connected and #LrgTextEditor:show-gutter-marks is set. The
	 * caret does not move.
	 */
	signals[SIGNAL_GUTTER_CLICKED] =
		g_signal_new ("gutter-clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgTextEditor::goto-definition:
	 * @self: the editor
	 * @line: the 0-based line
	 * @column: the 0-based character column
	 *
	 * F12 or Ctrl+click, only when a handler is connected. The editor
	 * does not look the symbol up; the handler does.
	 */
	signals[SIGNAL_GOTO_DEFINITION] =
		g_signal_new ("goto-definition", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

static void
lrg_text_editor_init (LrgTextEditor *self)
{
	self->lines = g_ptr_array_new_with_free_func (line_free);
	g_ptr_array_add (self->lines, line_new ("", 0));
	self->undo = g_ptr_array_new_with_free_func (step_free);
	self->redo = g_ptr_array_new_with_free_func (step_free);
	self->row_starts = g_array_new (FALSE, TRUE, sizeof (guint));
	self->scratch = g_string_new (NULL);
	self->goal_x = -1.0f;
	self->layout_serial = 1;
	self->natural_rows = 1;
	self->bracket_serial = G_MAXUINT;
	self->bracket_spans = g_array_new (FALSE, FALSE, sizeof (LrgSyntaxSpan));
	self->needle_fold = g_string_new (NULL);
	self->fold_text = g_string_new (NULL);
	self->fold_map = g_array_new (FALSE, FALSE, sizeof (guint));
	self->fold_serial = G_MAXUINT;

	self->tab_width = 4;
	self->highlight_line = TRUE;
	self->auto_indent = TRUE;
	self->execution_line = -1;
	self->completion_selected = -1;
	self->gutter_marks = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->diagnostics = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_text_diagnostic_free);
	self->completions = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_text_completion_free);

	lrg_scroll_axis_init (&self->ax);
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_fx_init (&self->fx);
	lrg_spring_init (&self->caret_x, PAD_X, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->caret_y, PAD_Y, LRG_MOTION_SNAPPY);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API: text
 * ========================================================================== */

LrgTextEditor *
lrg_text_editor_new (void)
{
	return g_object_new (LRG_TYPE_TEXT_EDITOR, NULL);
}

gchar *
lrg_text_editor_get_text (LrgTextEditor *self)
{
	GString *out;
	guint    i;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);

	out = g_string_new (NULL);
	for (i = 0; i < n_lines (self); i++)
	{
		if (i > 0)
			g_string_append_c (out, '\n');
		g_string_append_len (out, get_line (self, i)->text->str, (gssize) get_line (self, i)->text->len);
	}
	return g_string_free (out, FALSE);
}

void
lrg_text_editor_set_text (LrgTextEditor *self,
                          const gchar   *text)
{
	g_autofree gchar *norm = NULL;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	norm = normalize_text (text);
	g_ptr_array_set_size (self->lines, 0);
	g_ptr_array_add (self->lines, line_new ("", 0));
	buffer_replace (self, pos_make (0, 0), pos_make (0, 0), norm);
	self->cursor = self->anchor = pos_make (0, 0);
	self->goal_x = -1.0f;
	self->valid_upto = 0;
	self->text_changed = TRUE;
	lrg_text_editor_clear_history (self);
	lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);
	lrg_scroll_axis_scroll_to (&self->ax, 0.0f, FALSE);
	lrg_spring_snap (&self->caret_x, PAD_X);
	lrg_spring_snap (&self->caret_y, PAD_Y);
	flush (self, FALSE);
}

guint
lrg_text_editor_get_line_count (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 0);
	return n_lines (self);
}

const gchar *
lrg_text_editor_get_line (LrgTextEditor *self,
                          guint          line)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);
	if (line >= n_lines (self))
		return NULL;
	return get_line (self, line)->text->str;
}

void
lrg_text_editor_insert_text (LrgTextEditor *self,
                             const gchar   *text)
{
	g_autofree gchar *norm = NULL;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	g_return_if_fail (text != NULL);

	norm = normalize_text (text);
	self->break_coalesce = TRUE;
	replace_selection (self, STEP_OTHER, norm);
}

void
lrg_text_editor_append (LrgTextEditor *self,
                        const gchar   *text)
{
	g_autofree gchar *norm = NULL;
	gboolean          follow;
	Pos               end;
	Step             *saved;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	g_return_if_fail (text != NULL);

	norm = normalize_text (text);
	end = doc_end (self);
	follow = pos_cmp (self->cursor, end) == 0 && !has_selection (self);

	saved = self->open_step;
	self->open_step = NULL;
	end = buffer_replace (self, end, end, norm);
	self->open_step = saved;

	if (follow)
		self->cursor = self->anchor = end;
	flush (self, follow);
}

gboolean
lrg_text_editor_delete_selection (LrgTextEditor *self)
{
	Pos a;
	Pos b;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	if (!has_selection (self))
		return FALSE;
	sel_range (self, &a, &b);
	delete_range_action (self, STEP_OTHER, a, b);
	return TRUE;
}

/* ==========================================================================
 * Public API: caret and selection
 * ========================================================================== */

void
lrg_text_editor_get_cursor (LrgTextEditor *self,
                            guint         *line,
                            guint         *column)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (line != NULL)
		*line = self->cursor.line;
	if (column != NULL)
		*column = byte_to_char (get_line (self, self->cursor.line), self->cursor.byte);
}

/* pos_for: line / character column -> clamped position */
static Pos
pos_for (LrgTextEditor *self,
         guint          line,
         guint          column)
{
	line = MIN (line, n_lines (self) - 1);
	return pos_make (line, char_to_byte (get_line (self, line), column));
}

void
lrg_text_editor_set_cursor (LrgTextEditor *self,
                            guint          line,
                            guint          column)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	move_to (self, pos_for (self, line, column), FALSE);
	self->goal_x = -1.0f;
	flush (self, TRUE);
}

void
lrg_text_editor_select_range (LrgTextEditor *self,
                              guint          anchor_line,
                              guint          anchor_column,
                              guint          cursor_line,
                              guint          cursor_column)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	self->anchor = pos_for (self, anchor_line, anchor_column);
	self->cursor = pos_for (self, cursor_line, cursor_column);
	self->goal_x = -1.0f;
	self->break_coalesce = TRUE;
	flush (self, TRUE);
}

void
lrg_text_editor_select_all (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	self->anchor = pos_make (0, 0);
	self->cursor = doc_end (self);
	self->goal_x = -1.0f;
	self->break_coalesce = TRUE;
	flush (self, TRUE);
}

gboolean
lrg_text_editor_get_selection_bounds (LrgTextEditor *self,
                                      guint         *start_line,
                                      guint         *start_column,
                                      guint         *end_line,
                                      guint         *end_column)
{
	Pos a;
	Pos b;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	sel_range (self, &a, &b);
	if (start_line != NULL)
		*start_line = a.line;
	if (start_column != NULL)
		*start_column = byte_to_char (get_line (self, a.line), a.byte);
	if (end_line != NULL)
		*end_line = b.line;
	if (end_column != NULL)
		*end_column = byte_to_char (get_line (self, b.line), b.byte);
	return has_selection (self);
}

gchar *
lrg_text_editor_get_selected_text (LrgTextEditor *self)
{
	Pos a;
	Pos b;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);

	if (!has_selection (self))
		return NULL;
	sel_range (self, &a, &b);
	return range_text (self, a, b);
}

/* ==========================================================================
 * Public API: undo and clipboard
 * ========================================================================== */

/*
 * replay:
 * @forward: redo (apply) instead of undo (revert)
 *
 * Applies or reverts a step's edits without recording them.
 */
static void
replay (LrgTextEditor *self,
        Step          *s,
        gboolean       forward)
{
	guint i;

	self->replaying = TRUE;
	if (forward)
	{
		for (i = 0; i < s->edits->len; i++)
		{
			Edit *e = &g_array_index (s->edits, Edit, i);

			buffer_replace (self, e->start, pos_after (e->start, e->removed), e->inserted);
		}
		self->cursor = clamp_pos (self, s->cursor_after);
		self->anchor = clamp_pos (self, s->anchor_after);
	}
	else
	{
		for (i = s->edits->len; i > 0; i--)
		{
			Edit *e = &g_array_index (s->edits, Edit, i - 1);

			buffer_replace (self, e->start, pos_after (e->start, e->inserted), e->removed);
		}
		self->cursor = clamp_pos (self, s->cursor_before);
		self->anchor = clamp_pos (self, s->anchor_before);
	}
	self->replaying = FALSE;
	self->break_coalesce = TRUE;
	self->goal_x = -1.0f;
	flush (self, TRUE);
}

gboolean
lrg_text_editor_undo (LrgTextEditor *self)
{
	Step *s;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	if (self->undo->len == 0)
		return FALSE;
	s = g_ptr_array_steal_index (self->undo, self->undo->len - 1);
	replay (self, s, FALSE);
	g_ptr_array_add (self->redo, s);
	return TRUE;
}

gboolean
lrg_text_editor_redo (LrgTextEditor *self)
{
	Step *s;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	if (self->redo->len == 0)
		return FALSE;
	s = g_ptr_array_steal_index (self->redo, self->redo->len - 1);
	replay (self, s, TRUE);
	g_ptr_array_add (self->undo, s);
	return TRUE;
}

gboolean
lrg_text_editor_can_undo (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->undo->len > 0;
}

gboolean
lrg_text_editor_can_redo (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->redo->len > 0;
}

void
lrg_text_editor_clear_history (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	g_ptr_array_set_size (self->undo, 0);
	g_ptr_array_set_size (self->redo, 0);
	self->open_step = NULL;
	self->break_coalesce = TRUE;
}

void
lrg_text_editor_copy_clipboard (LrgTextEditor *self)
{
	g_autofree gchar *text = NULL;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	text = lrg_text_editor_get_selected_text (self);
	if (text != NULL)
		clipboard_set (text);
}

void
lrg_text_editor_cut_clipboard (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	lrg_text_editor_copy_clipboard (self);
	if (user_can_edit (self))
		lrg_text_editor_delete_selection (self);
}

void
lrg_text_editor_paste_clipboard (LrgTextEditor *self)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *norm = NULL;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (!user_can_edit (self))
		return;
	text = clipboard_get ();
	if (text == NULL || text[0] == '\0')
		return;
	norm = normalize_text (text);
	self->break_coalesce = TRUE;
	replace_selection (self, STEP_OTHER, norm);
}

/* ==========================================================================
 * Public API: indentation and find
 * ========================================================================== */

void
lrg_text_editor_indent (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	indent_lines (self, FALSE);
}

void
lrg_text_editor_outdent (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	indent_lines (self, TRUE);
}

gboolean
lrg_text_editor_find (LrgTextEditor *self,
                      const gchar   *needle,
                      gboolean       case_sensitive)
{
	Pos a;
	Pos b;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	g_clear_pointer (&self->needle, g_free);
	if (needle == NULL || needle[0] == '\0' || strchr (needle, '\n') != NULL)
		return FALSE;
	self->needle = g_strdup (needle);
	self->needle_case = case_sensitive;
	g_string_truncate (self->needle_fold, 0);
	if (!case_sensitive)
		fold_append (self->needle_fold, NULL, needle, strlen (needle), 0);
	sel_range (self, &a, &b);
	return find_from (self, a);
}

gboolean
lrg_text_editor_find_next (LrgTextEditor *self)
{
	Pos a;
	Pos b;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	if (self->needle == NULL)
		return FALSE;
	sel_range (self, &a, &b);
	/* past the current match (or the caret) */
	return find_from (self, has_selection (self) ? pos_make (a.line, a.byte + 1) : b);
}

gboolean
lrg_text_editor_find_previous (LrgTextEditor *self)
{
	Pos   a;
	Pos   b;
	guint n;
	guint k;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	if (self->needle == NULL)
		return FALSE;
	sel_range (self, &a, &b);
	n = n_lines (self);
	for (k = 0; k <= n; k++)
	{
		guint  line = (a.line + n - (k % n)) % n;
		Line  *l = get_line (self, line);
		guint  end = 0;
		gssize m = match_before (self, l, k == 0 ? a.byte : (guint) l->text->len + 1, &end);

		if (m >= 0)
		{
			select_match (self, line, (guint) m, end);
			return TRUE;
		}
	}
	return FALSE;
}

guint
lrg_text_editor_get_match_count (LrgTextEditor *self)
{
	guint count = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 0);

	if (self->needle == NULL)
		return 0;
	for (i = 0; i < n_lines (self); i++)
	{
		Line  *l = get_line (self, i);
		guint  end = 0;
		gssize m = match_in (self, l, 0, &end);

		/* non-overlapping: the next match starts after this one ends */
		while (m >= 0)
		{
			count++;
			m = match_in (self, l, end, &end);
		}
	}
	return count;
}

gboolean
lrg_text_editor_get_bracket_match (LrgTextEditor *self,
                                   guint         *line,
                                   guint         *column,
                                   guint         *match_line,
                                   guint         *match_column)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	/* the same cached pair painting uses */
	update_brackets (self);
	if (!self->bracket_found)
		return FALSE;
	if (line != NULL)
		*line = self->bracket_a.line;
	if (column != NULL)
		*column = byte_to_char (get_line (self, self->bracket_a.line), self->bracket_a.byte);
	if (match_line != NULL)
		*match_line = self->bracket_b.line;
	if (match_column != NULL)
		*match_column = byte_to_char (get_line (self, self->bracket_b.line), self->bracket_b.byte);
	return TRUE;
}

/* ==========================================================================
 * Public API: view
 * ========================================================================== */

void
lrg_text_editor_scroll_to_line (LrgTextEditor *self,
                                guint          line,
                                gboolean       animate)
{
	Geo g;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	geometry (self, &g);
	update_extent (self, &g);
	line = MIN (line, n_lines (self) - 1);
	lrg_scroll_axis_scroll_to (&self->ay, (gfloat) first_row (self, line) * g.line_h, animate && smooth (self));
}

void
lrg_text_editor_get_scroll (LrgTextEditor *self,
                            gfloat        *x,
                            gfloat        *y)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	refresh (self);
	if (x != NULL)
		*x = self->ax.pos.target;
	if (y != NULL)
		*y = self->ay.pos.target;
}

gfloat
lrg_text_editor_get_line_height (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 0.0f);
	return roundf (font_size (self) * LINE_SPACING);
}

guint
lrg_text_editor_get_row_count (LrgTextEditor *self)
{
	Geo g;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 0);

	geometry (self, &g);
	ensure_rows (self);
	return self->n_rows;
}

guint
lrg_text_editor_get_tokenize_count (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 0);
	return self->tokenize_count;
}

void
lrg_text_editor_set_token_color (LrgTextEditor  *self,
                                 LrgSyntaxToken  token,
                                 const GrlColor *color)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	g_return_if_fail ((guint) token < LRG_SYNTAX_N_TOKENS);

	if (color != NULL)
	{
		self->token_colors[token] = *color;
		self->token_colors_set |= 1u << token;
	}
	else
		self->token_colors_set &= ~(1u << token);
}

/* ==========================================================================
 * Public API: properties
 * ========================================================================== */

const gchar *
lrg_text_editor_get_language (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);
	return self->language;
}

void
lrg_text_editor_set_language (LrgTextEditor *self,
                              const gchar   *language)
{
	LrgSyntax *syn = NULL;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (language != NULL && language[0] != '\0')
	{
		syn = lrg_syntax_get_for_name (language);
		if (syn == NULL)
			lrg_debug (LRG_LOG_DOMAIN_UI, "text editor: unknown language '%s', plain text", language);
	}
	lrg_text_editor_set_syntax (self, syn);
}

LrgSyntax *
lrg_text_editor_get_syntax (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);
	return self->syntax;
}

void
lrg_text_editor_set_syntax (LrgTextEditor *self,
                            LrgSyntax     *syntax)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	g_return_if_fail (syntax == NULL || LRG_IS_SYNTAX (syntax));

	if (!g_set_object (&self->syntax, syntax))
		return;
	g_free (self->language);
	self->language = syntax != NULL ? g_strdup (lrg_syntax_get_name (syntax)) : NULL;
	reset_highlight (self);
	/* which brackets sit in strings / comments depends on the language */
	self->bracket_serial = G_MAXUINT;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYNTAX]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
}

gboolean
lrg_text_editor_get_show_line_numbers (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->show_line_numbers;
}

void
lrg_text_editor_set_show_line_numbers (LrgTextEditor *self,
                                       gboolean       show)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	show = !!show;
	if (self->show_line_numbers == show)
		return;
	self->show_line_numbers = show;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LINE_NUMBERS]);
}

gboolean
lrg_text_editor_get_wrap (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->wrap;
}

void
lrg_text_editor_set_wrap (LrgTextEditor *self,
                          gboolean       wrap)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	wrap = !!wrap;
	if (self->wrap == wrap)
		return;
	self->wrap = wrap;
	self->layout_serial++;
	self->rows_valid = FALSE;
	self->max_cols_valid = FALSE;
	self->reveal_pending = TRUE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WRAP]);
}

gboolean
lrg_text_editor_get_read_only (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->read_only;
}

void
lrg_text_editor_set_read_only (LrgTextEditor *self,
                               gboolean       read_only)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	read_only = !!read_only;
	if (self->read_only == read_only)
		return;
	self->read_only = read_only;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_READ_ONLY]);
}

guint
lrg_text_editor_get_tab_width (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 4);
	return self->tab_width;
}

void
lrg_text_editor_set_tab_width (LrgTextEditor *self,
                               guint          tab_width)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	tab_width = CLAMP (tab_width, 1u, 16u);
	if (self->tab_width == tab_width)
		return;
	self->tab_width = tab_width;
	self->layout_serial++;
	self->rows_valid = FALSE;
	self->max_cols_valid = FALSE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAB_WIDTH]);
}

gboolean
lrg_text_editor_get_insert_spaces (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->insert_spaces;
}

void
lrg_text_editor_set_insert_spaces (LrgTextEditor *self,
                                   gboolean       insert_spaces)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	insert_spaces = !!insert_spaces;
	if (self->insert_spaces == insert_spaces)
		return;
	self->insert_spaces = insert_spaces;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INSERT_SPACES]);
}

const gchar *
lrg_text_editor_get_placeholder (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);
	return self->placeholder;
}

void
lrg_text_editor_set_placeholder (LrgTextEditor *self,
                                 const gchar   *placeholder)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (g_strcmp0 (self->placeholder, placeholder) == 0)
		return;
	g_free (self->placeholder);
	self->placeholder = g_strdup (placeholder);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER]);
}

gboolean
lrg_text_editor_get_submit_on_enter (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->submit_on_enter;
}

void
lrg_text_editor_set_submit_on_enter (LrgTextEditor *self,
                                     gboolean       submit_on_enter)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	submit_on_enter = !!submit_on_enter;
	if (self->submit_on_enter == submit_on_enter)
		return;
	self->submit_on_enter = submit_on_enter;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBMIT_ON_ENTER]);
}

guint
lrg_text_editor_get_max_lines (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 0);
	return self->max_lines;
}

void
lrg_text_editor_set_max_lines (LrgTextEditor *self,
                               guint          max_lines)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (self->max_lines == max_lines)
		return;
	self->max_lines = max_lines;
	if (max_lines > 0 && n_lines (self) > max_lines)
	{
		enforce_max_lines (self);
		flush (self, FALSE);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_LINES]);
}

const gchar *
lrg_text_editor_get_font (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);
	return self->font != NULL ? self->font : "ui-mono";
}

void
lrg_text_editor_set_font (LrgTextEditor *self,
                          const gchar   *font)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (g_strcmp0 (self->font, font) == 0)
		return;
	g_free (self->font);
	self->font = g_strdup (font);
	self->cell_width = 0.0f;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT]);
}

gfloat
lrg_text_editor_get_font_size (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), 0.0f);
	return self->font_size;
}

void
lrg_text_editor_set_font_size (LrgTextEditor *self,
                               gfloat         size)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	size = MAX (0.0f, size);
	if (self->font_size == size)
		return;
	self->font_size = size;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
}

gboolean
lrg_text_editor_get_highlight_current_line (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->highlight_line;
}

void
lrg_text_editor_set_highlight_current_line (LrgTextEditor *self,
                                            gboolean       highlight)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	highlight = !!highlight;
	if (self->highlight_line == highlight)
		return;
	self->highlight_line = highlight;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIGHLIGHT_CURRENT_LINE]);
}

gboolean
lrg_text_editor_get_auto_indent (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->auto_indent;
}

void
lrg_text_editor_set_auto_indent (LrgTextEditor *self,
                                 gboolean       auto_indent)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	auto_indent = !!auto_indent;
	if (self->auto_indent == auto_indent)
		return;
	self->auto_indent = auto_indent;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_INDENT]);
}

/* ==========================================================================
 * Gutter marks, diagnostics, completion, hover
 * ========================================================================== */

GType
lrg_text_gutter_mark_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_TEXT_GUTTER_MARK_NONE, "LRG_TEXT_GUTTER_MARK_NONE", "none" },
			{ LRG_TEXT_GUTTER_MARK_BREAKPOINT, "LRG_TEXT_GUTTER_MARK_BREAKPOINT", "breakpoint" },
			{ LRG_TEXT_GUTTER_MARK_BREAKPOINT_DISABLED, "LRG_TEXT_GUTTER_MARK_BREAKPOINT_DISABLED", "breakpoint-disabled" },
			{ LRG_TEXT_GUTTER_MARK_CURRENT, "LRG_TEXT_GUTTER_MARK_CURRENT", "current" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgTextGutterMark"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_text_diagnostic_severity_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TEXT_DIAGNOSTIC_ERROR, "LRG_TEXT_DIAGNOSTIC_ERROR", "error" },
			{ LRG_TEXT_DIAGNOSTIC_WARNING, "LRG_TEXT_DIAGNOSTIC_WARNING", "warning" },
			{ LRG_TEXT_DIAGNOSTIC_INFO, "LRG_TEXT_DIAGNOSTIC_INFO", "info" },
			{ LRG_TEXT_DIAGNOSTIC_HINT, "LRG_TEXT_DIAGNOSTIC_HINT", "hint" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTextDiagnosticSeverity"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

LrgTextDiagnostic *
lrg_text_diagnostic_copy (const LrgTextDiagnostic *diagnostic)
{
	LrgTextDiagnostic *copy;

	g_return_val_if_fail (diagnostic != NULL, NULL);
	copy = g_new0 (LrgTextDiagnostic, 1);
	*copy = *diagnostic;
	copy->message = g_strdup (diagnostic->message);
	return copy;
}

void
lrg_text_diagnostic_free (LrgTextDiagnostic *diagnostic)
{
	if (diagnostic == NULL)
		return;
	g_free (diagnostic->message);
	g_free (diagnostic);
}

G_DEFINE_BOXED_TYPE (LrgTextDiagnostic, lrg_text_diagnostic, lrg_text_diagnostic_copy, lrg_text_diagnostic_free)

LrgTextCompletion *
lrg_text_completion_copy (const LrgTextCompletion *item)
{
	LrgTextCompletion *copy;

	g_return_val_if_fail (item != NULL, NULL);
	copy = g_new0 (LrgTextCompletion, 1);
	copy->label = g_strdup (item->label);
	copy->detail = g_strdup (item->detail);
	copy->insert_text = g_strdup (item->insert_text);
	return copy;
}

void
lrg_text_completion_free (LrgTextCompletion *item)
{
	if (item == NULL)
		return;
	g_free (item->label);
	g_free (item->detail);
	g_free (item->insert_text);
	g_free (item);
}

G_DEFINE_BOXED_TYPE (LrgTextCompletion, lrg_text_completion, lrg_text_completion_copy, lrg_text_completion_free)

gboolean
lrg_text_editor_get_show_gutter_marks (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->show_gutter_marks;
}

void
lrg_text_editor_set_show_gutter_marks (LrgTextEditor *self,
                                       gboolean       show)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	show = !!show;
	if (self->show_gutter_marks == show)
		return;
	self->show_gutter_marks = show;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_GUTTER_MARKS]);
}

void
lrg_text_editor_set_gutter_mark (LrgTextEditor     *self,
                                 guint              line,
                                 LrgTextGutterMark  mark)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (mark == LRG_TEXT_GUTTER_MARK_NONE)
		g_hash_table_remove (self->gutter_marks, GUINT_TO_POINTER (line));
	else
		g_hash_table_insert (self->gutter_marks, GUINT_TO_POINTER (line), GUINT_TO_POINTER (mark));
}

LrgTextGutterMark
lrg_text_editor_get_gutter_mark (LrgTextEditor *self,
                                 guint          line)
{
	gpointer found = NULL;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), LRG_TEXT_GUTTER_MARK_NONE);
	if (!g_hash_table_lookup_extended (self->gutter_marks, GUINT_TO_POINTER (line), NULL, &found))
		return LRG_TEXT_GUTTER_MARK_NONE;
	return (LrgTextGutterMark) GPOINTER_TO_UINT (found);
}

void
lrg_text_editor_clear_gutter_marks (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	g_hash_table_remove_all (self->gutter_marks);
}

void
lrg_text_editor_set_execution_line (LrgTextEditor *self,
                                    gint           line)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (line < 0)
		line = -1;
	self->execution_line = line;
}

gint
lrg_text_editor_get_execution_line (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), -1);
	return self->execution_line;
}

void
lrg_text_editor_set_diagnostics (LrgTextEditor *self,
                                 GPtrArray     *diagnostics)
{
	guint i;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	g_ptr_array_set_size (self->diagnostics, 0);
	if (diagnostics == NULL)
		return;
	for (i = 0; i < diagnostics->len; i++)
	{
		LrgTextDiagnostic *d = g_ptr_array_index (diagnostics, i);

		if (d != NULL)
			g_ptr_array_add (self->diagnostics, lrg_text_diagnostic_copy (d));
	}
}

GPtrArray *
lrg_text_editor_get_diagnostics (LrgTextEditor *self)
{
	GPtrArray *out;
	guint      i;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);

	out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_text_diagnostic_free);
	for (i = 0; i < self->diagnostics->len; i++)
		g_ptr_array_add (out, lrg_text_diagnostic_copy (g_ptr_array_index (self->diagnostics, i)));
	return out;
}

void
lrg_text_editor_show_completion (LrgTextEditor *self,
                                 GPtrArray     *items)
{
	guint i;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	g_ptr_array_set_size (self->completions, 0);
	self->completion_selected = 0;
	self->completion_visible = FALSE;
	if (items == NULL || items->len == 0)
		return;
	for (i = 0; i < items->len; i++)
	{
		LrgTextCompletion *item = g_ptr_array_index (items, i);

		if (item != NULL)
			g_ptr_array_add (self->completions, lrg_text_completion_copy (item));
	}
	if (self->completions->len == 0)
		return;
	self->completion_visible = TRUE;
	self->completion_selected = 0;
}

void
lrg_text_editor_hide_completion (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	self->completion_visible = FALSE;
	self->completion_selected = -1;
	if (self->completions != NULL)
		g_ptr_array_set_size (self->completions, 0);
}

gboolean
lrg_text_editor_get_completion_visible (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);
	return self->completion_visible;
}

gint
lrg_text_editor_get_completion_selected (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), -1);
	if (!self->completion_visible)
		return -1;
	return self->completion_selected;
}

void
lrg_text_editor_completion_move (LrgTextEditor *self,
                                 gint           delta)
{
	gint n;
	gint next;

	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (!self->completion_visible || self->completions == NULL || self->completions->len == 0)
		return;
	n = (gint) self->completions->len;
	next = self->completion_selected + delta;
	if (next < 0)
		next = 0;
	if (next >= n)
		next = n - 1;
	self->completion_selected = next;
}

gboolean
lrg_text_editor_completion_accept (LrgTextEditor *self)
{
	LrgTextCompletion *item;
	Line              *line;
	const gchar       *text;
	const gchar       *caret;
	const gchar       *start;
	const gchar       *insert;
	guint              a;
	guint              b;

	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), FALSE);

	if (!self->completion_visible || self->completions == NULL || self->completions->len == 0)
		return FALSE;
	if (self->completion_selected < 0 || (guint) self->completion_selected >= self->completions->len)
		self->completion_selected = 0;
	item = g_ptr_array_index (self->completions, (guint) self->completion_selected);
	insert = item->insert_text != NULL ? item->insert_text : item->label;
	if (insert == NULL)
		insert = "";

	line = get_line (self, self->cursor.line);
	text = line->text->str;
	caret = text + MIN (self->cursor.byte, (guint) line->text->len);
	start = caret;
	while (start > text)
	{
		const gchar *prev = g_utf8_prev_char (start);
		gunichar     ch = g_utf8_get_char (prev);

		if (!g_unichar_isalnum (ch) && ch != '_')
			break;
		start = prev;
	}
	a = byte_to_char (line, (guint) (start - text));
	b = byte_to_char (line, (guint) (caret - text));
	self->completion_hold = TRUE;
	lrg_text_editor_select_range (self, self->cursor.line, a, self->cursor.line, b);
	lrg_text_editor_insert_text (self, insert);
	self->completion_hold = FALSE;
	lrg_text_editor_hide_completion (self);
	return TRUE;
}

void
lrg_text_editor_show_hover (LrgTextEditor *self,
                            const gchar   *text)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));

	if (text == NULL || text[0] == '\0')
	{
		lrg_text_editor_hide_hover (self);
		return;
	}
	g_free (self->hover_text);
	self->hover_text = g_strdup (text);
}

void
lrg_text_editor_hide_hover (LrgTextEditor *self)
{
	g_return_if_fail (LRG_IS_TEXT_EDITOR (self));
	g_clear_pointer (&self->hover_text, g_free);
}

const gchar *
lrg_text_editor_get_hover_text (LrgTextEditor *self)
{
	g_return_val_if_fail (LRG_IS_TEXT_EDITOR (self), NULL);
	return self->hover_text;
}
