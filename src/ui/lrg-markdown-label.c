/* lrg-markdown-label.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMarkdownLabel: Renders a Markdown subset (headings, emphasis, code, lists, quotes, links).
 *
 * The label is built on #LrgRichText: every Markdown block becomes one
 * LrgRichText whose BBCode markup is generated from the Markdown.
 *
 *  1. parse_blocks(): the source is split into lines and grouped into
 *     blocks (paragraphs, headings, list items, quotes, fenced code,
 *     rules). Paragraph, list and quote lines that follow without a
 *     blank line continue the block (soft line breaks).
 *  2. parse_inline(): each block's text is translated into LrgRichText
 *     markup: "**x**" becomes "[b]x[/b]", "*x*" "[i]x[/i]", a link
 *     "[u]text[/u]". LrgRichText tags written in the Markdown ([wave],
 *     [shake], [rainbow], [typewriter=N], [pulse], [fade], [color=..],
 *     [size=..], [b], [i], [u], [s]) pass straight through, so game text
 *     can animate. Inline code and link targets have no BBCode form;
 *     their extents are kept as ranges of plain-text characters, which
 *     line up with the rich text's spans. Literal "[" characters are
 *     stored as U+E000 in the markup (LrgRichText has no escape) and
 *     turned back into "[" whenever text is measured or drawn.
 *  3. The markup is parsed by lrg_rich_text_set_markup(); its spans
 *     carry the LrgTextStyle (bold, italic, underline, strikethrough),
 *     size multiplier, colour and effect of every run.
 *  4. relayout(): the rich text spans are broken into words and flowed
 *     into lines no wider than the widget, producing positioned
 *     fragments and decorations (bullets, quote bars, code panels,
 *     rules). LrgRichText's own max-width wrap is not used: it cannot
 *     report where a link ended up. It runs again whenever the width,
 *     the font size or the available font faces change.
 *
 * paint() records the fragments into the paint list with
 * lrg_paint_list_text() (glyphs batch, clip and follow appear
 * transforms); nothing is drawn immediately. Fragments with a text
 * effect are recorded glyph by glyph with the offset and colour the
 * effect gives each character.
 *
 * Effects are animated from step(): lrg_rich_text_update() advances
 * each block's rich text (its completion state answers
 * lrg_markdown_label_get_effects_complete()), including its span
 * effects (lrg_rich_text_get_span_effect()), which paint() applies per
 * glyph with span-local character indices.
 *
 * Faces: bold, italic, bold-italic and monospace text use the fonts
 * named "ui-bold", "ui-italic", "ui-bold-italic" and "ui-mono" in the
 * default LrgFontManager. When one is missing the label tries once to
 * load it from common system font files (only with a window open);
 * when none can be found it falls back to the UI font with synthetic
 * bold (drawn twice 0.6 px apart) and synthetic italic (a slight accent
 * tint with a faint underline). Faces loaded through the font manager
 * cover ASCII; fragments with other characters use the UI font with the
 * synthetic fallbacks.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-markdown-label.h"
#include "../text/lrg-font-manager.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Fallback width when measured before any width was assigned */
#define MD_NATURAL_MAX (480.0f)

/* Private style bit: text inside a block quote */
#define STYLE_QUOTE (1u << 8)

/* Private style bit: heading text */
#define STYLE_HEADING (1u << 9)

/* Private style bit: a line of a fenced code block */
#define STYLE_BLOCK_CODE (1u << 10)

/* Private style bits from LrgTextStyle with no Markdown syntax */
#define STYLE_UNDERLINE (1u << 11)
#define STYLE_STRIKE    (1u << 12)

/* Stand-in for a literal '[' inside rich text markup (U+E000) */
#define LITERAL_BRACKET     "\xee\x80\x80"
#define LITERAL_BRACKET_CP  (0xE000)

/* Rich text default colour meaning "the theme's colour for this part";
 * any other span colour was set with [color=..] */
#define THEME_R (1)
#define THEME_G (2)
#define THEME_B (3)

/* Size the font manager faces are loaded at (as the theme UI font) */
#define FACE_BASE_SIZE (64)

typedef enum
{
	FACE_BOLD,
	FACE_ITALIC,
	FACE_BOLD_ITALIC,
	FACE_MONO,
	N_FACES
} Face;

typedef struct
{
	gchar *text;
	guint  style;
	gint   link;          /* index into links, -1 = none */
} Span;

typedef struct
{
	guint    start;       /* plain-text characters */
	guint    end;
	gint     link;        /* link index, or -1 for inline code */
} Range;

typedef struct
{
	LrgMarkdownBlockKind  kind;
	gint                  level;
	gint                  number;
	GString              *source;   /* raw block text, markup included */
	GArray               *spans;    /* Span: the Markdown-level spans */
	GArray               *ranges;   /* Range: code and link extents */
	gchar                *markup;   /* the generated LrgRichText markup */
	LrgRichText          *rich;
	GPtrArray            *effects;  /* LrgTextEffect or NULL, per rich span */
	gchar                *plain;
} Block;

typedef enum
{
	ITEM_TEXT,
	ITEM_BULLET,
	ITEM_NUMBER,
	ITEM_QUOTE_BAR,
	ITEM_CODE_PANEL,
	ITEM_RULE
} ItemKind;

typedef struct
{
	ItemKind  kind;
	gfloat    x;
	gfloat    y;
	gfloat    width;
	gfloat    height;     /* line height for text */
	gfloat    size;       /* font size for text */
	gchar    *text;
	guint     style;
	gint      link;
	guint     block;
	gint      span;       /* rich text span, -1 for none */
	guint     char0;      /* index of the first character in the span */
	GrlColor  color;      /* explicit [color=..] colour */
	gboolean  has_color;
} Item;

struct _LrgMarkdownLabel
{
	LrgWidget  parent_instance;

	gchar     *markdown;
	gfloat     font_size;      /* 0 = theme */
	GrlColor   text_color;
	gboolean   text_color_set;

	GArray    *blocks;         /* Block */
	GPtrArray *links;          /* gchar * */

	/* Layout for laid_width / laid_size / laid_faces */
	GArray    *items;          /* Item */
	gfloat     laid_width;
	gfloat     laid_size;
	guint      laid_faces;
	gfloat     content_height;
	gfloat     content_width;
	guint      n_lines;

	gint       hover_link;
	gint       press_link;
	gint       focus_link;
	LrgSpring  link_hover;     /* underline of the hovered link */
	gboolean   stepped;
};

G_DEFINE_FINAL_TYPE (LrgMarkdownLabel, lrg_markdown_label, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_MARKDOWN,
	PROP_FONT_SIZE,
	PROP_TEXT_COLOR,
	N_PROPS
};

enum
{
	SIGNAL_LINK_ACTIVATED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

GType
lrg_markdown_block_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_MARKDOWN_BLOCK_PARAGRAPH, "LRG_MARKDOWN_BLOCK_PARAGRAPH", "paragraph" },
			{ LRG_MARKDOWN_BLOCK_HEADING, "LRG_MARKDOWN_BLOCK_HEADING", "heading" },
			{ LRG_MARKDOWN_BLOCK_BULLET_ITEM, "LRG_MARKDOWN_BLOCK_BULLET_ITEM", "bullet-item" },
			{ LRG_MARKDOWN_BLOCK_NUMBERED_ITEM, "LRG_MARKDOWN_BLOCK_NUMBERED_ITEM", "numbered-item" },
			{ LRG_MARKDOWN_BLOCK_QUOTE, "LRG_MARKDOWN_BLOCK_QUOTE", "quote" },
			{ LRG_MARKDOWN_BLOCK_CODE_BLOCK, "LRG_MARKDOWN_BLOCK_CODE_BLOCK", "code-block" },
			{ LRG_MARKDOWN_BLOCK_RULE, "LRG_MARKDOWN_BLOCK_RULE", "rule" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgMarkdownBlockKind"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

GType
lrg_markdown_span_style_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_MARKDOWN_SPAN_NONE, "LRG_MARKDOWN_SPAN_NONE", "none" },
			{ LRG_MARKDOWN_SPAN_BOLD, "LRG_MARKDOWN_SPAN_BOLD", "bold" },
			{ LRG_MARKDOWN_SPAN_ITALIC, "LRG_MARKDOWN_SPAN_ITALIC", "italic" },
			{ LRG_MARKDOWN_SPAN_CODE, "LRG_MARKDOWN_SPAN_CODE", "code" },
			{ LRG_MARKDOWN_SPAN_LINK, "LRG_MARKDOWN_SPAN_LINK", "link" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgMarkdownSpanStyle"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Element cleanup
 * ========================================================================== */

static void
span_clear (gpointer data)
{
	Span *s = data;

	g_clear_pointer (&s->text, g_free);
}

static void
block_clear (gpointer data)
{
	Block *b = data;

	if (b->source != NULL)
		g_string_free (b->source, TRUE);
	b->source = NULL;
	g_clear_pointer (&b->spans, g_array_unref);
	g_clear_pointer (&b->ranges, g_array_unref);
	g_clear_pointer (&b->markup, g_free);
	g_clear_object (&b->rich);
	g_clear_pointer (&b->effects, g_ptr_array_unref);
	g_clear_pointer (&b->plain, g_free);
}

/* unref_effect: free function for effect arrays (NULL entries allowed) */
static void
unref_effect (gpointer data)
{
	if (data != NULL)
		g_object_unref (data);
}

static void
item_clear (gpointer data)
{
	Item *it = data;

	g_clear_pointer (&it->text, g_free);
}

/* unescape: @text with the literal-bracket stand-ins turned back into '[' */
static gchar *
unescape (const gchar *text,
          gssize       len)
{
	GString     *out = g_string_new (NULL);
	const gchar *p = text;
	const gchar *end;

	if (text == NULL)
		return g_string_free (out, FALSE);
	end = len < 0 ? text + strlen (text) : text + len;
	while (p < end)
	{
		gunichar c = g_utf8_get_char (p);

		if (c == LITERAL_BRACKET_CP)
			g_string_append_c (out, '[');
		else
			g_string_append_unichar (out, c);
		p = g_utf8_next_char (p);
	}
	return g_string_free (out, FALSE);
}

/* ==========================================================================
 * Inline parsing: Markdown to LrgRichText markup
 * ========================================================================== */

typedef struct
{
	GArray    *spans;      /* Span */
	GPtrArray *links;      /* gchar * */
	GArray    *ranges;     /* Range */
	GString   *markup;     /* LrgRichText markup being built */
	guint      plain;      /* plain characters emitted so far */
} Inline;

/*
 * emit:
 *
 * Text that is shown: a Markdown span, its markup (with '[' replaced
 * by the stand-in so LrgRichText does not read it as a tag) and the
 * plain-character count.
 */
static void
emit (Inline      *ctx,
      const gchar *text,
      gssize       len,
      guint        style,
      gint         link)
{
	Span s;
	gsize n;
	gsize i;

	if (text == NULL)
		return;
	n = len < 0 ? strlen (text) : (gsize) len;
	if (n == 0)
		return;

	s.text = g_strndup (text, n);
	s.style = style;
	s.link = link;
	g_array_append_val (ctx->spans, s);

	for (i = 0; i < n; i++)
	{
		if (text[i] == '[')
			g_string_append (ctx->markup, LITERAL_BRACKET);
		else
			g_string_append_c (ctx->markup, text[i]);
	}
	ctx->plain += (guint) g_utf8_strlen (text, (gssize) n);
}

/* flush: emit the pending plain text */
static void
flush (Inline  *ctx,
       GString *buf,
       guint    style,
       gint     link)
{
	if (buf->len > 0)
		emit (ctx, buf->str, (gssize) buf->len, style, link);
	g_string_truncate (buf, 0);
}

/* add_range: record a code or link extent in plain characters */
static void
add_range (Inline *ctx,
           guint   start,
           gint    link)
{
	Range r;

	if (ctx->plain <= start)
		return;
	r.start = start;
	r.end = ctx->plain;
	r.link = link;
	g_array_append_val (ctx->ranges, r);
}

static gboolean
is_word_char (gchar c)
{
	return g_ascii_isalnum (c);
}

/*
 * rich_tag_length:
 *
 * If @p starts an LrgRichText tag ("[wave]", "[/wave]", "[color=red]",
 * "[typewriter=40]", ...), the tag's length, else 0. Only the tag names
 * LrgRichText understands count, so other brackets stay text.
 */
static gsize
rich_tag_length (const gchar *p)
{
	static const gchar * const names[] = {
		"b", "i", "u", "s", "color", "size", "shake", "wave", "rainbow",
		"typewriter", "pulse", "fade", NULL
	};
	const gchar *q = p + 1;
	const gchar *name;
	const gchar *end;
	gsize        n;
	guint        i;

	if (*q == '/')
		q++;
	name = q;
	while (g_ascii_isalpha (*q))
		q++;
	n = (gsize) (q - name);
	if (n == 0 || (*q != ']' && *q != '=' && *q != ' '))
		return 0;
	for (i = 0; names[i] != NULL; i++)
		if (strlen (names[i]) == n && strncmp (names[i], name, n) == 0)
			break;
	if (names[i] == NULL)
		return 0;

	/* The closing bracket, with no other bracket or line break first */
	for (end = q; *end != '\0' && *end != ']'; end++)
		if (*end == '[' || *end == '\n')
			return 0;
	return *end == ']' ? (gsize) (end - p) + 1 : 0;
}

/* toggle_tags: markup for emphasis turning on or off */
static void
toggle_tags (Inline *ctx,
             guint   before,
             guint   after)
{
	gboolean bold_on = (after & LRG_MARKDOWN_SPAN_BOLD) && !(before & LRG_MARKDOWN_SPAN_BOLD);
	gboolean bold_off = !(after & LRG_MARKDOWN_SPAN_BOLD) && (before & LRG_MARKDOWN_SPAN_BOLD);
	gboolean it_on = (after & LRG_MARKDOWN_SPAN_ITALIC) && !(before & LRG_MARKDOWN_SPAN_ITALIC);
	gboolean it_off = !(after & LRG_MARKDOWN_SPAN_ITALIC) && (before & LRG_MARKDOWN_SPAN_ITALIC);

	/* Close inner before outer (LrgRichText pops its state stack) */
	if (it_off)
		g_string_append (ctx->markup, "[/i]");
	if (bold_off)
		g_string_append (ctx->markup, "[/b]");
	if (bold_on)
		g_string_append (ctx->markup, "[b]");
	if (it_on)
		g_string_append (ctx->markup, "[i]");
}

/*
 * parse_inline:
 *
 * Translates @text into spans and rich text markup. Emphasis markers
 * toggle BOLD ("**", "__") and ITALIC ("*", "_") and become [b] / [i];
 * a marker with whitespace on both sides, and "_" inside a word
 * (snake_case), is literal. "`code`" is taken verbatim and recorded as
 * a code range. "[text](url)" registers @url in the link list, parses
 * the text with the LINK flag inside [u] .. [/u] and records a link
 * range. LrgRichText tags pass through untouched. "\" escapes the next
 * punctuation character (including "[").
 */
static void
parse_inline (const gchar *text,
              guint        base_style,
              gint         link,
              Inline      *ctx)
{
	g_autoptr(GString) buf = g_string_new (NULL);
	guint              style = base_style;
	gsize              n = strlen (text);
	gsize              i = 0;

	while (i < n)
	{
		gchar c = text[i];

		/* Escapes */
		if (c == '\\' && i + 1 < n && g_ascii_ispunct (text[i + 1]))
		{
			g_string_append_c (buf, text[i + 1]);
			i += 2;
			continue;
		}

		/* Inline code */
		if (c == '`')
		{
			const gchar *close = strchr (text + i + 1, '`');

			if (close != NULL)
			{
				guint start;

				flush (ctx, buf, style, link);
				start = ctx->plain;
				emit (ctx, text + i + 1, close - (text + i + 1), style | LRG_MARKDOWN_SPAN_CODE, link);
				add_range (ctx, start, -1);
				i = (gsize) (close - text) + 1;
				continue;
			}
		}

		/* Emphasis */
		if (c == '*' || c == '_')
		{
			gsize    run = 0;
			gchar    prev = i > 0 ? text[i - 1] : ' ';
			gchar    next;
			gboolean literal;

			while (i + run < n && text[i + run] == c)
				run++;
			next = i + run < n ? text[i + run] : ' ';

			/* Opening needs text after it, closing text before it */
			literal = g_ascii_isspace (prev) && g_ascii_isspace (next);
			if (c == '_' && is_word_char (prev) && is_word_char (next))
				literal = TRUE;
			if (!literal)
			{
				guint before = style;

				flush (ctx, buf, style, link);
				if (run >= 2)
				{
					style ^= LRG_MARKDOWN_SPAN_BOLD;
					if (run >= 3)
						style ^= LRG_MARKDOWN_SPAN_ITALIC;
				}
				else
					style ^= LRG_MARKDOWN_SPAN_ITALIC;
				toggle_tags (ctx, before, style);
				i += MIN (run, (gsize) 3);
				/* Extra markers beyond three stay literal */
				while (run > 3)
				{
					g_string_append_c (buf, c);
					run--;
				}
				continue;
			}
			g_string_append_len (buf, text + i, (gssize) run);
			i += run;
			continue;
		}

		if (c == '[')
		{
			gsize tag;

			/* Links: [text](url) */
			if (link < 0)
			{
				const gchar *close = strchr (text + i + 1, ']');

				if (close != NULL && close[1] == '(')
				{
					const gchar *end = strchr (close + 2, ')');

					if (end != NULL)
					{
						g_autofree gchar *label = g_strndup (text + i + 1, (gsize) (close - (text + i + 1)));
						gint              id = (gint) ctx->links->len;
						guint             start;

						flush (ctx, buf, style, link);
						g_ptr_array_add (ctx->links,
						                 g_strstrip (g_strndup (close + 2, (gsize) (end - (close + 2)))));
						start = ctx->plain;
						g_string_append (ctx->markup, "[u]");
						parse_inline (label, style | LRG_MARKDOWN_SPAN_LINK, id, ctx);
						g_string_append (ctx->markup, "[/u]");
						add_range (ctx, start, id);
						i = (gsize) (end - text) + 1;
						continue;
					}
				}
			}

			/* LrgRichText tags pass through (effects, colours, sizes) */
			tag = rich_tag_length (text + i);
			if (tag > 0)
			{
				flush (ctx, buf, style, link);
				g_string_append_len (ctx->markup, text + i, (gssize) tag);
				i += tag;
				continue;
			}
		}

		g_string_append_c (buf, c);
		i++;
	}
	flush (ctx, buf, style, link);

	/* Unclosed emphasis: close it so the next block starts clean */
	toggle_tags (ctx, style, base_style);
}

/* ==========================================================================
 * Block parsing
 * ========================================================================== */

/* leading_spaces: indentation width (tab = 4) and the first non-space */
static gint
leading_spaces (const gchar  *line,
                const gchar **rest)
{
	gint n = 0;

	while (*line == ' ' || *line == '\t')
	{
		n += *line == '\t' ? 4 : 1;
		line++;
	}
	*rest = line;
	return n;
}

/* is_rule: three or more of the same '-', '*' or '_', spaces allowed */
static gboolean
is_rule (const gchar *s)
{
	gchar ch = 0;
	gint  count = 0;

	for (; *s != '\0'; s++)
	{
		if (*s == ' ' || *s == '\t')
			continue;
		if (*s != '-' && *s != '*' && *s != '_')
			return FALSE;
		if (ch != 0 && *s != ch)
			return FALSE;
		ch = *s;
		count++;
	}
	return count >= 3;
}

/* new_block: append an empty block of @kind */
static Block *
new_block (GArray               *blocks,
           LrgMarkdownBlockKind  kind,
           gint                  level)
{
	Block b;

	memset (&b, 0, sizeof (Block));
	b.kind = kind;
	b.level = level;
	b.source = g_string_new (NULL);
	g_array_append_val (blocks, b);
	return &g_array_index (blocks, Block, blocks->len - 1);
}

/* append_text: continue a block's text (soft line break = space) */
static void
append_text (Block       *b,
             const gchar *text)
{
	g_autofree gchar *trimmed = g_strstrip (g_strdup (text));

	if (trimmed[0] == '\0')
		return;
	if (b->source->len > 0)
		g_string_append_c (b->source, ' ');
	g_string_append (b->source, trimmed);
}

/*
 * parse_blocks:
 *
 * Line-based block parser for the supported subset; see the file
 * comment. Lazy continuation: a line that does not start a block of
 * its own continues the open paragraph, list item or quote.
 */
static void
parse_blocks (LrgMarkdownLabel *self)
{
	g_auto(GStrv) lines = NULL;
	Block        *open = NULL;
	guint         i;

	g_array_set_size (self->blocks, 0);
	g_ptr_array_set_size (self->links, 0);
	if (self->markdown == NULL)
		return;

	lines = g_strsplit (self->markdown, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		gchar       *line = lines[i];
		const gchar *rest;
		gint         indent;
		gsize        len = strlen (line);

		/* Strip a trailing CR (Windows line ends) */
		if (len > 0 && line[len - 1] == '\r')
			line[len - 1] = '\0';
		indent = leading_spaces (line, &rest);

		/* Blank line: closes the open block */
		if (rest[0] == '\0')
		{
			open = NULL;
			continue;
		}

		/* Fenced code: everything up to the closing fence, verbatim */
		if (g_str_has_prefix (rest, "```") || g_str_has_prefix (rest, "~~~"))
		{
			gchar    fence = rest[0];
			Block   *b = new_block (self->blocks, LRG_MARKDOWN_BLOCK_CODE_BLOCK, 0);
			gboolean first = TRUE;

			for (i++; lines[i] != NULL; i++)
			{
				const gchar *r;
				gsize        l = strlen (lines[i]);

				if (l > 0 && lines[i][l - 1] == '\r')
					lines[i][l - 1] = '\0';
				leading_spaces (lines[i], &r);
				if (r[0] == fence && r[1] == fence && r[2] == fence)
					break;
				if (!first)
					g_string_append_c (b->source, '\n');
				g_string_append (b->source, lines[i]);
				first = FALSE;
			}
			open = NULL;
			if (lines[i] == NULL)
				break;
			continue;
		}

		/* Headings */
		if (rest[0] == '#')
		{
			gint level = 0;

			while (rest[level] == '#')
				level++;
			if (level <= 6 && (rest[level] == ' ' || rest[level] == '\0'))
			{
				Block            *b = new_block (self->blocks, LRG_MARKDOWN_BLOCK_HEADING, level);
				g_autofree gchar *t = g_strdup (rest + level);
				gchar            *end;

				/* Optional closing #'s */
				g_strstrip (t);
				end = t + strlen (t);
				while (end > t && end[-1] == '#')
					end--;
				*end = '\0';
				append_text (b, t);
				open = NULL;
				continue;
			}
		}

		/* Rules (before bullets: "- - -" and "***" look like items) */
		if (is_rule (rest))
		{
			new_block (self->blocks, LRG_MARKDOWN_BLOCK_RULE, 0);
			open = NULL;
			continue;
		}

		/* Quotes; consecutive quote lines join */
		if (rest[0] == '>')
		{
			const gchar *t = rest + 1;

			if (*t == ' ')
				t++;
			if (open == NULL || open->kind != LRG_MARKDOWN_BLOCK_QUOTE)
				open = new_block (self->blocks, LRG_MARKDOWN_BLOCK_QUOTE, 0);
			append_text (open, t);
			continue;
		}

		/* Bullet items */
		if ((rest[0] == '-' || rest[0] == '*' || rest[0] == '+') && (rest[1] == ' ' || rest[1] == '\t'))
		{
			open = new_block (self->blocks, LRG_MARKDOWN_BLOCK_BULLET_ITEM, indent / 2);
			append_text (open, rest + 2);
			continue;
		}

		/* Numbered items */
		if (g_ascii_isdigit (rest[0]))
		{
			const gchar *p = rest;

			while (g_ascii_isdigit (*p))
				p++;
			if ((*p == '.' || *p == ')') && (p[1] == ' ' || p[1] == '\t') && p - rest <= 9)
			{
				open = new_block (self->blocks, LRG_MARKDOWN_BLOCK_NUMBERED_ITEM, indent / 2);
				open->number = (gint) g_ascii_strtoll (rest, NULL, 10);
				append_text (open, p + 2);
				continue;
			}
		}

		/* Continuation or a new paragraph */
		if (open == NULL)
			open = new_block (self->blocks, LRG_MARKDOWN_BLOCK_PARAGRAPH, 0);
		append_text (open, rest);
	}

	/* Each block: rich text markup, then the LrgRichText itself */
	for (i = 0; i < self->blocks->len; i++)
	{
		Block  *b = &g_array_index (self->blocks, Block, i);
		Inline  ctx;
		guint   k;

		b->spans = g_array_new (FALSE, TRUE, sizeof (Span));
		g_array_set_clear_func (b->spans, span_clear);
		b->ranges = g_array_new (FALSE, TRUE, sizeof (Range));
		ctx.spans = b->spans;
		ctx.links = self->links;
		ctx.ranges = b->ranges;
		ctx.markup = g_string_new (NULL);
		ctx.plain = 0;

		if (b->kind == LRG_MARKDOWN_BLOCK_CODE_BLOCK)
		{
			/* Verbatim, one code range over everything */
			emit (&ctx, b->source->str, (gssize) b->source->len, LRG_MARKDOWN_SPAN_CODE, -1);
			add_range (&ctx, 0, -1);
		}
		else if (b->kind == LRG_MARKDOWN_BLOCK_HEADING)
		{
			/* Headings are bold text */
			g_string_append (ctx.markup, "[b]");
			parse_inline (b->source->str, LRG_MARKDOWN_SPAN_NONE, -1, &ctx);
			g_string_append (ctx.markup, "[/b]");
		}
		else if (b->kind != LRG_MARKDOWN_BLOCK_RULE)
			parse_inline (b->source->str, LRG_MARKDOWN_SPAN_NONE, -1, &ctx);

		b->markup = g_string_free (ctx.markup, FALSE);

		/* The sentinel default colour marks spans without [color=..] */
		b->rich = lrg_rich_text_new ();
		lrg_rich_text_set_default_color (b->rich, THEME_R, THEME_G, THEME_B, 0);
		lrg_rich_text_set_markup (b->rich, b->markup);
		b->plain = unescape (lrg_rich_text_get_plain_text (b->rich), -1);

		/*
		 * The rich text's own span effects (lrg_rich_text_get_span_effect),
		 * one entry per span: lrg_rich_text_update() advances them and
		 * paint() applies them per glyph.
		 */
		b->effects = g_ptr_array_new_with_free_func (unref_effect);
		for (k = 0; k < lrg_rich_text_get_span_count (b->rich); k++)
		{
			LrgTextEffect *effect = lrg_rich_text_get_span_effect (b->rich, k);

			if (effect != NULL)
				g_object_ref (effect);
			g_ptr_array_add (b->effects, effect);
		}
	}
}

/* ==========================================================================
 * Font faces
 * ========================================================================== */

static const gchar * const face_names[N_FACES] = {
	"ui-bold", "ui-italic", "ui-bold-italic", "ui-mono"
};

static const gchar * const face_bold_files[] = {
	"/usr/share/fonts/inter/Inter-Bold.ttf",
	"/usr/share/fonts/rsms-inter-fonts/Inter-Bold.ttf",
	"/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Bold.otf",
	"/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
	"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
	"/usr/share/fonts/open-sans/OpenSans-Bold.ttf",
	"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
	"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
	NULL
};

static const gchar * const face_italic_files[] = {
	"/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Italic.ttf",
	"/usr/share/fonts/inter/Inter-Italic.ttf",
	"/usr/share/fonts/google-noto/NotoSans-Italic.ttf",
	"/usr/share/fonts/truetype/noto/NotoSans-Italic.ttf",
	"/usr/share/fonts/open-sans/OpenSans-Italic.ttf",
	"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Italic.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf",
	"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Oblique.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
	"/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf",
	NULL
};

static const gchar * const face_bold_italic_files[] = {
	"/usr/share/fonts/inter/Inter-BoldItalic.ttf",
	"/usr/share/fonts/google-noto/NotoSans-BoldItalic.ttf",
	"/usr/share/fonts/truetype/noto/NotoSans-BoldItalic.ttf",
	"/usr/share/fonts/open-sans/OpenSans-BoldItalic.ttf",
	"/usr/share/fonts/liberation-sans-fonts/LiberationSans-BoldItalic.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf",
	"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-BoldOblique.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf",
	"/usr/share/fonts/TTF/DejaVuSans-BoldOblique.ttf",
	NULL
};

static const gchar * const face_mono_files[] = {
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

/* One search per face and process (fonts are shared through the manager) */
static gboolean face_attempted[N_FACES];

/*
 * get_face:
 *
 * The font registered in the default LrgFontManager as @face's name.
 * If there is none, the first time a window is open the system font
 * files are tried and the first that loads is registered under that
 * name (mipmapped like the theme UI font, so it stays clean when drawn
 * small). Applications can register their own faces under these names
 * before the label first paints.
 *
 * Returns: (nullable): the face
 */
static GrlFont *
get_face (Face face)
{
	static const gchar * const *files[N_FACES] = {
		face_bold_files, face_italic_files, face_bold_italic_files, face_mono_files
	};
	LrgFontManager *mgr = lrg_font_manager_get_default ();
	GrlFont        *font = lrg_font_manager_get_font (mgr, face_names[face]);
	guint           i;

	if (font != NULL || face_attempted[face] || !IsWindowReady ())
		return font;
	face_attempted[face] = TRUE;

	for (i = 0; files[face][i] != NULL && font == NULL; i++)
	{
		Font *handle;

		if (!g_file_test (files[face][i], G_FILE_TEST_IS_REGULAR))
			continue;
		if (!lrg_font_manager_load_font (mgr, face_names[face], files[face][i], FACE_BASE_SIZE, NULL))
			continue;
		font = lrg_font_manager_get_font (mgr, face_names[face]);
		handle = font != NULL ? grl_font_get_handle (font) : NULL;
		if (handle != NULL && handle->texture.id != 0)
		{
			GenTextureMipmaps (&handle->texture);
			SetTextureFilter (handle->texture, TEXTURE_FILTER_TRILINEAR);
		}
	}
	if (font == NULL)
		lrg_debug (LRG_LOG_DOMAIN_UI, "no %s face found; markdown falls back to synthetic styles",
		           face_names[face]);
	return font;
}

/* faces_key: bit mask of the faces available now (0 without a window) */
static guint
faces_key (void)
{
	guint key = 0;
	guint f;

	if (!IsWindowReady ())
		return 0;
	for (f = 0; f < N_FACES; f++)
		if (get_face ((Face) f) != NULL)
			key |= 1u << f;
	return key | (1u << N_FACES);
}

/* is_ascii: faces loaded through the font manager only cover ASCII */
static gboolean
is_ascii (const gchar *text)
{
	for (; *text != '\0'; text++)
		if ((guchar) *text >= 0x80)
			return FALSE;
	return TRUE;
}

/*
 * pick_font:
 *
 * The font for text with @style: a span's custom font when the font
 * manager has it, else the matching face (bold, italic, bold-italic or
 * mono), else the UI font. *@fake_bold / *@fake_italic say which styles
 * the chosen font does not provide, to be synthesised when drawing.
 */
static GrlFont *
pick_font (LrgMarkdownLabel *self,
           const gchar      *text,
           guint             style,
           const gchar      *custom,
           gboolean         *fake_bold,
           gboolean         *fake_italic)
{
	GrlFont  *ui = lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
	gboolean  bold = (style & LRG_MARKDOWN_SPAN_BOLD) != 0;
	gboolean  italic = (style & LRG_MARKDOWN_SPAN_ITALIC) != 0;
	GrlFont  *f;

	*fake_bold = bold;
	*fake_italic = italic;

	if (custom != NULL)
	{
		f = lrg_font_manager_get_font (lrg_font_manager_get_default (), custom);
		if (f != NULL)
			return f;
	}
	if (!IsWindowReady () || (text != NULL && !is_ascii (text)))
		return ui;

	if (style & LRG_MARKDOWN_SPAN_CODE)
	{
		f = get_face (FACE_MONO);
		return f != NULL ? f : ui;
	}
	if (bold && italic && (f = get_face (FACE_BOLD_ITALIC)) != NULL)
	{
		*fake_bold = FALSE;
		*fake_italic = FALSE;
		return f;
	}
	if (bold && (f = get_face (FACE_BOLD)) != NULL)
	{
		*fake_bold = FALSE;
		return f;
	}
	if (italic && (f = get_face (FACE_ITALIC)) != NULL)
	{
		*fake_italic = FALSE;
		return f;
	}
	return ui;
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static gfloat
body_size (LrgMarkdownLabel *self)
{
	if (self->font_size > 0.0f)
		return self->font_size;
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_FONT_SIZE);
}

/* heading_size: font size for heading @level relative to the body */
static gfloat
heading_size (LrgMarkdownLabel *self,
              gint              level)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat    body = body_size (self);
	gfloat    scale = body / MAX (1.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE));

	switch (level)
	{
	case 1:
		return roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_TITLE) * scale);
	case 2:
		return roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_LARGE) * scale);
	default:
		return roundf (body * 1.12f);
	}
}

static gfloat
line_height (gfloat size)
{
	return roundf (size * 1.4f);
}

/* measure_width: width of @text drawn with @style's font */
static gfloat
measure_width (LrgMarkdownLabel *self,
               const gchar      *text,
               gfloat            size,
               guint             style,
               const gchar      *custom)
{
	gboolean fb;
	gboolean fi;
	gfloat   w = 0.0f;

	if (text == NULL || text[0] == '\0')
		return 0.0f;
	lrg_paint_measure_text (pick_font (self, text, style, custom, &fb, &fi), text, size, 1.0f, &w, NULL);
	return w;
}

/* code_size: inline / block code is a touch smaller than the text */
static gfloat
code_size (gfloat size)
{
	return roundf (size * 0.92f);
}

/* chip_pad: horizontal padding inside an inline code chip */
static gfloat
chip_pad (gfloat size)
{
	return roundf (MAX (2.0f, size * 0.25f));
}

static Item *
add_item (LrgMarkdownLabel *self,
          ItemKind          kind,
          gfloat            x,
          gfloat            y,
          gfloat            w,
          gfloat            h)
{
	Item it;

	memset (&it, 0, sizeof (Item));
	it.kind = kind;
	it.x = x;
	it.y = y;
	it.width = w;
	it.height = h;
	it.link = -1;
	it.span = -1;
	g_array_append_val (self->items, it);
	return &g_array_index (self->items, Item, self->items->len - 1);
}

/* range_at: whether plain character @pos is code and / or in a link */
static void
range_at (Block    *b,
          guint     pos,
          gboolean *code,
          gint     *link)
{
	guint i;

	*code = FALSE;
	*link = -1;
	for (i = 0; i < b->ranges->len; i++)
	{
		Range *r = &g_array_index (b->ranges, Range, i);

		if (pos < r->start || pos >= r->end)
			continue;
		if (r->link >= 0)
			*link = r->link;
		else
			*code = TRUE;
	}
}

/* text_style_bits: LrgTextStyle to the label's style bits */
static guint
text_style_bits (LrgTextStyle ts)
{
	guint bits = 0;

	if (ts & LRG_TEXT_STYLE_BOLD)
		bits |= LRG_MARKDOWN_SPAN_BOLD;
	if (ts & LRG_TEXT_STYLE_ITALIC)
		bits |= LRG_MARKDOWN_SPAN_ITALIC;
	if (ts & LRG_TEXT_STYLE_UNDERLINE)
		bits |= STYLE_UNDERLINE;
	if (ts & LRG_TEXT_STYLE_STRIKETHROUGH)
		bits |= STYLE_STRIKE;
	return bits;
}

/* finish_line: give every fragment of the line the line's height */
static void
finish_line (LrgMarkdownLabel *self,
             gint              first,
             gfloat            height)
{
	guint i;

	for (i = (guint) first; i < self->items->len; i++)
		g_array_index (self->items, Item, i).height = height;
}

/*
 * flow:
 *
 * Lays out block @index's rich text spans as words from @x0 to @x1
 * starting at *@y, wrapping at word boundaries. Each word carries its
 * rich span's style, size multiplier, colour and effect, and the code
 * / link range it falls in. Neighbouring words of the same span, style
 * and link on a line merge into one fragment (one text run, one code
 * chip, one continuous underline), except in effect spans, where every
 * word keeps the index of its first character for the effect. A line
 * is as tall as its largest text. Words wider than the line overflow
 * it. Advances *@y past the last line and returns the number of lines.
 */
static guint
flow (LrgMarkdownLabel *self,
      guint             index,
      guint             extra_style,
      gfloat            base,
      gfloat            x0,
      gfloat            x1,
      gfloat           *y)
{
	Block    *b = &g_array_index (self->blocks, Block, index);
	gfloat    space = MAX (measure_width (self, " ", base, 0, NULL), base * 0.28f);
	gfloat    cx = x0;
	gfloat    line_h = line_height (base);
	guint     lines = 1;
	gint      line_first = (gint) self->items->len;
	gboolean  pending_space = FALSE;
	guint     pos = 0;
	guint     k;

	for (k = 0; k < lrg_rich_text_get_span_count (b->rich); k++)
	{
		const LrgTextSpan *span = lrg_rich_text_get_span (b->rich, k);
		const gchar       *p = lrg_text_span_get_text (span);
		const gchar       *custom = lrg_text_span_get_custom_font (span);
		guint              span_bits = text_style_bits (lrg_text_span_get_style (span)) | extra_style;
		gfloat             mult = lrg_text_span_get_font_size (span);
		gboolean           has_effect = g_ptr_array_index (b->effects, k) != NULL;
		GrlColor           color;
		gboolean           has_color;
		guint              ci = 0;

		lrg_text_span_get_color (span, &color.r, &color.g, &color.b, &color.a);
		has_color = !(color.r == THEME_R && color.g == THEME_G && color.b == THEME_B);
		color.a = 255;

		while (*p != '\0')
		{
			gunichar          c = g_utf8_get_char (p);
			const gchar      *start;
			guint             word_ci;
			gboolean          code;
			gint              link;
			guint             style;
			gfloat            size;
			gfloat            ww;
			gfloat            gap;
			gfloat            pad;
			Item             *prev = NULL;
			g_autofree gchar *word = NULL;

			/* Whitespace between words */
			if (c == ' ' || c == '\t' || c == '\n')
			{
				pending_space = TRUE;
				p = g_utf8_next_char (p);
				ci++;
				pos++;
				continue;
			}

			/* A word: up to whitespace or a code / link boundary */
			start = p;
			word_ci = ci;
			range_at (b, pos, &code, &link);
			while (*p != '\0')
			{
				gboolean code2;
				gint     link2;

				c = g_utf8_get_char (p);
				if (c == ' ' || c == '\t' || c == '\n')
					break;
				range_at (b, pos, &code2, &link2);
				if (code2 != code || link2 != link)
					break;
				p = g_utf8_next_char (p);
				ci++;
				pos++;
			}
			word = unescape (start, p - start);
			style = span_bits | (code ? LRG_MARKDOWN_SPAN_CODE : 0) | (link >= 0 ? LRG_MARKDOWN_SPAN_LINK : 0);
			size = roundf (base * mult * (code ? 0.92f : 1.0f));
			ww = measure_width (self, word, size, style, custom);
			pad = code ? chip_pad (base) : 0.0f;
			gap = (pending_space && cx > x0) ? space : 0.0f;

			/* Wrap */
			if (cx > x0 && cx + gap + ww + pad * 2.0f > x1)
			{
				finish_line (self, line_first, line_h);
				*y += line_h;
				line_h = line_height (base);
				cx = x0;
				gap = 0.0f;
				lines++;
				line_first = (gint) self->items->len;
			}
			line_h = MAX (line_h, line_height (size));

			/* Merge with the previous fragment of the same span */
			if ((gint) self->items->len > line_first && !has_effect)
			{
				prev = &g_array_index (self->items, Item, self->items->len - 1);
				if (prev->kind != ITEM_TEXT || prev->style != style || prev->link != link ||
				    prev->block != index || prev->span != (gint) k || prev->size != size)
					prev = NULL;
			}
			if (prev != NULL)
			{
				gchar *joined = g_strconcat (prev->text, gap > 0.0f ? " " : "", word, NULL);

				g_free (prev->text);
				prev->text = joined;
				prev->width = measure_width (self, joined, size, style, custom);
				cx = prev->x + prev->width + pad;
			}
			else
			{
				Item *it;

				cx += gap + pad;
				it = add_item (self, ITEM_TEXT, cx, *y, ww, line_height (base));
				it->text = g_steal_pointer (&word);
				it->style = style;
				it->link = link;
				it->size = size;
				it->block = index;
				it->span = (gint) k;
				it->char0 = word_ci;
				it->color = color;
				it->has_color = has_color;
				cx += ww + pad;
			}
			pending_space = FALSE;
			self->content_width = MAX (self->content_width, cx);
		}
	}
	finish_line (self, line_first, line_h);
	*y += line_h;
	return lines;
}

/*
 * relayout:
 *
 * Positions every block for @width (relative to the widget's top-left)
 * and records the content height and line count. Each block's rich
 * text is told its font size and wrap width too, so its own measure
 * agrees with the label.
 */
static void
relayout (LrgMarkdownLabel *self,
          gfloat            width)
{
	gfloat size = body_size (self);
	gfloat y = 0.0f;
	gfloat indent = roundf (size * 1.5f);
	guint  i;

	g_array_set_size (self->items, 0);
	self->laid_width = width;
	self->laid_size = size;
	self->laid_faces = faces_key ();
	self->n_lines = 0;
	self->content_width = 0.0f;

	for (i = 0; i < self->blocks->len; i++)
	{
		Block                *b = &g_array_index (self->blocks, Block, i);
		LrgMarkdownBlockKind  prev_kind = i > 0 ? g_array_index (self->blocks, Block, i - 1).kind
		                                        : LRG_MARKDOWN_BLOCK_PARAGRAPH;
		gboolean              list = b->kind == LRG_MARKDOWN_BLOCK_BULLET_ITEM ||
		                             b->kind == LRG_MARKDOWN_BLOCK_NUMBERED_ITEM;
		gboolean              prev_list = prev_kind == LRG_MARKDOWN_BLOCK_BULLET_ITEM ||
		                                  prev_kind == LRG_MARKDOWN_BLOCK_NUMBERED_ITEM;

		/* Gap above: tight between list items, roomy before headings */
		if (i > 0)
		{
			if (list && prev_list)
				y += roundf (size * 0.2f);
			else if (b->kind == LRG_MARKDOWN_BLOCK_HEADING)
				y += roundf (size * 1.0f);
			else
				y += roundf (size * 0.6f);
		}

		lrg_rich_text_set_font_size (b->rich, b->kind == LRG_MARKDOWN_BLOCK_HEADING ? heading_size (self, b->level)
		                                                                             : size);

		switch (b->kind)
		{
		case LRG_MARKDOWN_BLOCK_HEADING:
			lrg_rich_text_set_max_width (b->rich, width);
			self->n_lines += flow (self, i, STYLE_HEADING, heading_size (self, b->level), 0.0f, width, &y);
			/* Level 1 and 2 headings get a hairline under them */
			if (b->level <= 2)
			{
				y += roundf (size * 0.2f);
				add_item (self, ITEM_RULE, 0.0f, y, width, 1.0f);
				y += 1.0f;
			}
			break;

		case LRG_MARKDOWN_BLOCK_BULLET_ITEM:
		case LRG_MARKDOWN_BLOCK_NUMBERED_ITEM:
			{
				gfloat left = indent * (gfloat) (b->level + 1);
				gfloat top = y;
				Item  *marker;

				if (b->kind == LRG_MARKDOWN_BLOCK_BULLET_ITEM)
				{
					/* Disc for depth 0, ring below; centred on line 1 */
					marker = add_item (self, ITEM_BULLET, left - indent * 0.55f, top + line_height (size) * 0.5f,
					                   roundf (MAX (2.0f, size * 0.16f)), 0.0f);
					marker->style = (guint) b->level;
				}
				else
				{
					g_autofree gchar *num = g_strdup_printf ("%d.", b->number);
					gfloat            nw = measure_width (self, num, size, 0, NULL);

					marker = add_item (self, ITEM_NUMBER, left - roundf (size * 0.4f) - nw, top, nw,
					                   line_height (size));
					marker->text = g_steal_pointer (&num);
					marker->size = size;
				}
				lrg_rich_text_set_max_width (b->rich, MAX (0.0f, width - left));
				self->n_lines += flow (self, i, 0, size, left, width, &y);
			}
			break;

		case LRG_MARKDOWN_BLOCK_QUOTE:
			{
				gfloat top = y;
				gfloat bar = roundf (MAX (3.0f, size * 0.2f));

				lrg_rich_text_set_max_width (b->rich, MAX (0.0f, width - indent * 0.8f));
				self->n_lines += flow (self, i, STYLE_QUOTE, size, indent * 0.8f, width, &y);
				add_item (self, ITEM_QUOTE_BAR, 0.0f, top, bar, y - top);
			}
			break;

		case LRG_MARKDOWN_BLOCK_CODE_BLOCK:
			{
				gfloat         cs = code_size (size);
				gfloat         clh = line_height (cs);
				gfloat         pad = roundf (size * 0.6f);
				g_auto(GStrv)  code_lines = g_strsplit (b->plain, "\n", -1);
				gfloat         top = y;
				guint          panel = self->items->len;
				guint          k;

				/* Verbatim lines (the rich text's single code span),
				 * not wrapped; the panel clips them */
				lrg_rich_text_set_font_size (b->rich, cs);
				add_item (self, ITEM_CODE_PANEL, 0.0f, top, width, 0.0f);
				y += pad;
				for (k = 0; code_lines[k] != NULL; k++)
				{
					Item  *it;
					guint  st = LRG_MARKDOWN_SPAN_CODE | STYLE_BLOCK_CODE;
					gfloat lw = measure_width (self, code_lines[k], cs, st, NULL);

					it = add_item (self, ITEM_TEXT, pad, y, lw, clh);
					it->text = g_strdup (code_lines[k]);
					it->style = st;
					it->size = cs;
					it->block = i;
					self->content_width = MAX (self->content_width, pad * 2.0f + lw);
					y += clh;
					self->n_lines++;
				}
				y += pad;
				g_array_index (self->items, Item, panel).height = y - top;
			}
			break;

		case LRG_MARKDOWN_BLOCK_RULE:
			y += roundf (size * 0.4f);
			add_item (self, ITEM_RULE, 0.0f, y, width, 1.0f);
			y += 1.0f + roundf (size * 0.4f);
			break;

		case LRG_MARKDOWN_BLOCK_PARAGRAPH:
		default:
			lrg_rich_text_set_max_width (b->rich, width);
			self->n_lines += flow (self, i, 0, size, 0.0f, width, &y);
			break;
		}
	}

	self->content_height = y;
}

/* layout_width: the width to wrap at: the widget's, or the natural one */
static gfloat
layout_width (LrgMarkdownLabel *self)
{
	gfloat w = lrg_widget_get_width (LRG_WIDGET (self));

	if (w > 0.0f)
		return w;

	/* Not sized yet: the longest unwrapped line, capped */
	relayout (self, G_MAXFLOAT / 4.0f);
	return MIN (MAX (self->content_width, 1.0f), MD_NATURAL_MAX);
}

/* ensure_layout: re-run the layout if the width or font size changed */
static void
ensure_layout (LrgMarkdownLabel *self)
{
	gfloat w = layout_width (self);

	/* Faces turning up (a window opened) change every measurement */
	if (w != self->laid_width || body_size (self) != self->laid_size || faces_key () != self->laid_faces)
		relayout (self, w);
}

/* invalidate: the content changed */
static void
invalidate (LrgMarkdownLabel *self)
{
	self->laid_width = -1.0f;
	self->hover_link = -1;
	self->press_link = -1;
	if (self->focus_link >= (gint) self->links->len)
		self->focus_link = -1;
	lrg_widget_set_focusable (LRG_WIDGET (self), self->links->len > 0);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* link_at: the link under canvas point, -1 for none */
static gint
link_at (LrgMarkdownLabel *self,
         gfloat            px,
         gfloat            py)
{
	gfloat rx = px - lrg_widget_get_world_x (LRG_WIDGET (self));
	gfloat ry = py - lrg_widget_get_world_y (LRG_WIDGET (self));
	guint  i;

	ensure_layout (self);
	for (i = 0; i < self->items->len; i++)
	{
		Item *it = &g_array_index (self->items, Item, i);

		if (it->kind == ITEM_TEXT && it->link >= 0 &&
		    rx >= it->x && rx < it->x + it->width && ry >= it->y && ry < it->y + it->height)
			return it->link;
	}
	return -1;
}

/* ==========================================================================
 * LrgWidget vfuncs
 * ========================================================================== */

static void
lrg_markdown_label_style_defaults (LrgWidget      *widget,
                                   LrgUIPart       part,
                                   LrgWidgetState  state,
                                   LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_LABEL:
	case LRG_UI_PART_HEADER:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		break;

	case LRG_UI_PART_ITEM:
		{
			GrlColor c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);

			/* Links: LINK colour, lighter under the pointer; SELECTED
			 * (keyboard cursor) adds a FOCUS ring */
			if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
				lrg_paint_color_shade (&c, (state & LRG_WIDGET_STATE_PRESSED) ? -0.1f : 0.15f, &c);
			lrg_part_look_set_foreground (look, &c);
			lrg_part_look_set_radius (look, 3.0f);
			if (state & LRG_WIDGET_STATE_SELECTED)
				lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS),
				                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH));
		}
		break;

	case LRG_UI_PART_MARK:
		/* Inline code chip */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 1.0f);
		lrg_part_look_set_radius (look, MAX (2.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) * 0.5f));
		break;

	case LRG_UI_PART_BOX:
		/* Fenced code panel */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		break;

	case LRG_UI_PART_INDICATOR:
		/* Quote bar (background) and quoted text (foreground) */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		break;

	case LRG_UI_PART_TRACK:
		/* Horizontal rules */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
		break;

	case LRG_UI_PART_ARROW:
		/* List bullets and numbers */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	case LRG_UI_PART_FOCUS:
		{
			GrlColor none = *lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS);

			/* The focused link carries the ring instead */
			none.a = 0;
			lrg_part_look_set_border (look, &none, 0.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_markdown_label_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_markdown_label_style_overrides (LrgWidget      *widget,
                                    LrgUIPart       part,
                                    LrgWidgetState  state,
                                    LrgPartLook    *look)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (widget);

	if (self->text_color_set && (part == LRG_UI_PART_LABEL || part == LRG_UI_PART_HEADER))
		lrg_part_look_set_foreground (look, &self->text_color);
}

static void
lrg_markdown_label_measure (LrgWidget *widget,
                            gfloat    *width,
                            gfloat    *height)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (widget);
	gfloat            w = layout_width (self);

	/* Wrap at the current width; the height follows */
	relayout (self, w);
	*width = w;
	*height = self->content_height;
}

static void
lrg_markdown_label_arrange (LrgWidget *widget)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (widget);
	gfloat            before = self->content_height;

	ensure_layout (self);

	/* A new width that changes the wrapped height asks the parent to
	 * measure again; same width next time, so this settles */
	if (self->content_height != before && lrg_widget_get_height (widget) != self->content_height)
		lrg_widget_queue_layout (widget);
}

/*
 * lrg_markdown_label_step:
 *
 * Moves the link underline spring, and animates text effects: each
 * block's LrgRichText is advanced with lrg_rich_text_update() and the
 * its span effects, which paint() applies per glyph, advance with it.
 * Busy while any effect is still running (looping effects such as wave
 * never finish).
 */
static gboolean
lrg_markdown_label_step (LrgWidget *widget,
                         gfloat     delta_time)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (widget);
	LrgMotion         motion = lrg_widget_get_motion (widget);
	gboolean          busy = FALSE;
	guint             i;

	for (i = 0; i < self->blocks->len; i++)
	{
		Block *b = &g_array_index (self->blocks, Block, i);
		guint  k;

		lrg_rich_text_update (b->rich, delta_time);
		for (k = 0; k < b->effects->len; k++)
		{
			LrgTextEffect *effect = g_ptr_array_index (b->effects, k);

			if (effect != NULL)
				busy |= !lrg_text_effect_is_complete (effect);
		}
	}

	lrg_spring_set_motion (&self->link_hover, motion);
	lrg_spring_set_target (&self->link_hover, self->hover_link >= 0 ? 1.0f : 0.0f);
	if (!self->stepped || motion == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->link_hover, self->link_hover.target);
		self->stepped = TRUE;
		return busy;
	}
	return lrg_spring_step (&self->link_hover, delta_time) || busy;
}

/* item_effect: the effect animating @it, or NULL */
static LrgTextEffect *
item_effect (LrgMarkdownLabel *self,
             Item             *it)
{
	Block *b;

	if (it->span < 0 || it->block >= self->blocks->len)
		return NULL;
	b = &g_array_index (self->blocks, Block, it->block);
	if ((guint) it->span >= b->effects->len)
		return NULL;
	return g_ptr_array_index (b->effects, it->span);
}

/* item_custom_font: the rich span's custom font name, or NULL */
static const gchar *
item_custom_font (LrgMarkdownLabel *self,
                  Item             *it)
{
	Block *b;

	if (it->span < 0 || it->block >= self->blocks->len)
		return NULL;
	b = &g_array_index (self->blocks, Block, it->block);
	if ((guint) it->span >= lrg_rich_text_get_span_count (b->rich))
		return NULL;
	return lrg_text_span_get_custom_font (lrg_rich_text_get_span (b->rich, (guint) it->span));
}

/*
 * paint_effect_glyphs:
 *
 * Records @it one glyph at a time with the offset and colour its
 * effect gives each character (wave, shake, rainbow, typewriter, fade,
 * pulse). Characters the effect hides (alpha 0) still advance.
 */
static void
paint_effect_glyphs (LrgPaintList   *list,
                     GrlFont        *font,
                     Item           *it,
                     LrgTextEffect  *effect,
                     gfloat          x,
                     gfloat          ty,
                     const GrlColor *fg,
                     gboolean        fake_bold)
{
	const gchar *p;
	guint        n = 0;

	for (p = it->text; *p != '\0'; p = g_utf8_next_char (p), n++)
	{
		gchar    glyph[8];
		gint     len = g_unichar_to_utf8 (g_utf8_get_char (p), glyph);
		gfloat   dx = 0.0f;
		gfloat   dy = 0.0f;
		gfloat   w = 0.0f;
		GrlColor c = *fg;

		glyph[len] = '\0';
		lrg_text_effect_apply (effect, it->char0 + n, &dx, &dy, &c.r, &c.g, &c.b, &c.a);
		if (c.a > 0)
		{
			/* Keep the fragment's own opacity under the effect's */
			c.a = (guint8) (((guint) c.a * (guint) fg->a) / 255u);
			lrg_paint_list_text (list, font, glyph, x + dx, ty + dy, it->size, 1.0f, &c);
			if (fake_bold)
				lrg_paint_list_text (list, font, glyph, x + dx + 0.6f, ty + dy, it->size, 1.0f, &c);
		}
		lrg_paint_measure_text (font, glyph, it->size, 1.0f, &w, NULL);
		x += w + 1.0f;
	}
}

/* paint_text_item: one text fragment with its inline decorations */
static void
paint_text_item (LrgMarkdownLabel *self,
                 LrgPaintList     *list,
                 Item             *it,
                 gfloat            ox,
                 gfloat            oy)
{
	LrgWidget      *widget = LRG_WIDGET (self);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	LrgTextEffect  *effect = item_effect (self, it);
	LrgPartLook     look;
	GrlFont        *font;
	GrlColor        fg;
	gboolean        fake_bold;
	gboolean        fake_italic;
	gfloat          x = ox + it->x;
	gfloat          th = it->size;
	gfloat          ty;
	gboolean        code = (it->style & LRG_MARKDOWN_SPAN_CODE) != 0;
	gboolean        block_code = (it->style & STYLE_BLOCK_CODE) != 0;

	font = pick_font (self, it->text, it->style, item_custom_font (self, it), &fake_bold, &fake_italic);
	lrg_paint_measure_text (font, it->text, it->size, 1.0f, NULL, &th);
	ty = roundf (oy + it->y + (it->height - th) * 0.5f);

	/* Colour: link > code > quote > heading > body */
	if (it->link >= 0)
	{
		LrgWidgetState st = LRG_WIDGET_STATE_NONE;

		if (it->link == self->hover_link)
			st |= LRG_WIDGET_STATE_HOVERED;
		if (it->link == self->press_link && it->link == self->hover_link)
			st |= LRG_WIDGET_STATE_PRESSED;
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, st, &look);
		fg = look.foreground;
	}
	else if (block_code)
	{
		lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
		fg = look.foreground;
	}
	else if (it->style & STYLE_QUOTE)
	{
		lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &look);
		fg = look.foreground;
	}
	else
	{
		lrg_widget_resolve_look (widget, (it->style & STYLE_HEADING) ? LRG_UI_PART_HEADER : LRG_UI_PART_LABEL,
		                         &look);
		fg = look.foreground;
	}

	/* No italic face: emphasis is a slight accent tint plus a faint
	 * underline (links only underline on hover, so they stay distinct) */
	if (fake_italic && it->link < 0)
		lrg_paint_color_mix (&fg, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.18f, &fg);

	/* Inline code chip behind the text */
	if (code && !block_code)
	{
		LrgPartLook chip;
		gfloat      pad = chip_pad (self->laid_size);
		gfloat      ch = roundf (it->size * 1.3f);

		lrg_widget_resolve_look (widget, LRG_UI_PART_MARK, &chip);
		lrg_paint_list_rect_ex (list, x - pad, roundf (oy + it->y + (it->height - ch) * 0.5f), it->width + pad * 2.0f,
		                        ch, &chip.radii, &chip.background, NULL, chip.border_width, &chip.border);
		if (it->link < 0)
			fg = chip.foreground;
	}

	/* A [color=..] from the rich text beats the role colour (not links) */
	if (it->has_color && it->link < 0)
	{
		guint8 alpha = fg.a;

		fg = it->color;
		fg.a = alpha;
	}

	if (effect != NULL)
		paint_effect_glyphs (list, font, it, effect, roundf (x), ty, &fg, fake_bold);
	else
	{
		lrg_paint_list_text (list, font, it->text, roundf (x), ty, it->size, 1.0f, &fg);
		/* Synthetic bold: a second pass 0.6 px to the right */
		if (fake_bold)
			lrg_paint_list_text (list, font, it->text, roundf (x) + 0.6f, ty, it->size, 1.0f, &fg);
	}

	if (fake_italic && it->link < 0 && !code)
	{
		GrlColor faint;

		lrg_paint_color_with_alpha (&fg, ((gfloat) fg.a / 255.0f) * 0.4f, &faint);
		lrg_paint_list_rect (list, x, ty + th + 0.5f, it->width, 1.0f, 0.0f, &faint);
	}

	/* [u] / [s] from the rich text (links underline only on hover) */
	if ((it->style & STYLE_UNDERLINE) && it->link < 0)
		lrg_paint_list_rect (list, x, ty + th + 0.5f, it->width, MAX (1.0f, roundf (it->size * 0.07f)), 0.0f, &fg);
	if (it->style & STYLE_STRIKE)
		lrg_paint_list_rect (list, x, roundf (ty + th * 0.55f), it->width, MAX (1.0f, roundf (it->size * 0.07f)),
		                     0.0f, &fg);

	/* Link underline: grows in from the left on hover */
	if (it->link >= 0 && it->link == self->hover_link)
	{
		gfloat t = self->stepped ? self->link_hover.value : 1.0f;

		if (t > 0.01f)
			lrg_paint_list_rect (list, x, ty + th + 0.5f, it->width * CLAMP (t, 0.0f, 1.0f),
			                     MAX (1.0f, roundf (it->size * 0.07f)), 0.0f, &fg);
	}

	/* Keyboard cursor on a link */
	if (it->link >= 0 && it->link == self->focus_link &&
	    (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUS_VISIBLE))
	{
		LrgPartLook ring;

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_SELECTED, &ring);
		lrg_paint_list_rect_ex (list, x - 3.0f, oy + it->y, it->width + 6.0f, it->height, &ring.radii, NULL, NULL,
		                        ring.border_width, &ring.border);
	}
}

static void
lrg_markdown_label_paint (LrgWidget    *widget,
                          LrgPaintList *list)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (widget);
	GrlFont          *font = lrg_theme_get_ui_font (lrg_widget_get_theme (widget));
	LrgPartLook       label;
	gfloat            ox = lrg_widget_get_world_x (widget);
	gfloat            oy = lrg_widget_get_world_y (widget);
	gfloat            clip_bottom = 0.0f;
	gboolean          clipped = FALSE;
	guint             i;

	ensure_layout (self);
	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
	if (label.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, label.opacity);

	for (i = 0; i < self->items->len; i++)
	{
		Item *it = &g_array_index (self->items, Item, i);

		/* Code block lines are clipped to their panel */
		if (clipped && it->y >= clip_bottom)
		{
			lrg_paint_list_pop_clip (list);
			clipped = FALSE;
		}

		switch (it->kind)
		{
		case ITEM_TEXT:
			paint_text_item (self, list, it, ox, oy);
			break;

		case ITEM_BULLET:
			{
				LrgPartLook m;
				gfloat      r = it->width;

				lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &m);
				if (it->style == 0)
					lrg_paint_list_circle (list, ox + it->x, oy + it->y, r, &m.foreground);
				else
					lrg_paint_list_border (list, ox + it->x - r, oy + it->y - r, r * 2.0f, r * 2.0f, r,
					                       MAX (1.0f, r * 0.45f), &m.foreground);
			}
			break;

		case ITEM_NUMBER:
			{
				LrgPartLook m;
				gfloat      th = it->size;

				lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &m);
				lrg_paint_measure_text (font, it->text, it->size, 1.0f, NULL, &th);
				lrg_paint_list_text (list, font, it->text, roundf (ox + it->x),
				                     roundf (oy + it->y + (it->height - th) * 0.5f), it->size, 1.0f, &m.foreground);
			}
			break;

		case ITEM_QUOTE_BAR:
			{
				LrgPartLook q;

				lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &q);
				lrg_paint_list_rect_ex (list, ox + it->x, oy + it->y, it->width, it->height, &q.radii,
				                        &q.background, NULL, 0.0f, NULL);
			}
			break;

		case ITEM_CODE_PANEL:
			{
				LrgPartLook panel;

				lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &panel);
				lrg_widget_paint_box (widget, list, &panel, ox + it->x, oy + it->y, it->width, it->height);
				if (clipped)
					lrg_paint_list_pop_clip (list);
				lrg_paint_list_push_clip (list, ox + it->x, oy + it->y, it->width, it->height);
				clipped = TRUE;
				clip_bottom = it->y + it->height;
			}
			break;

		case ITEM_RULE:
			{
				LrgPartLook r;

				lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &r);
				lrg_paint_list_rect (list, ox + it->x, oy + it->y, it->width, MAX (1.0f, it->height), 0.0f,
				                     &r.background);
			}
			break;

		default:
			break;
		}
	}
	if (clipped)
		lrg_paint_list_pop_clip (list);

	if (label.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

/* activate_link: emit link-activated for link @i */
static void
activate_link (LrgMarkdownLabel *self,
               gint              i)
{
	if (i < 0 || i >= (gint) self->links->len)
		return;
	g_signal_emit (self, signals[SIGNAL_LINK_ACTIVATED], 0, (const gchar *) g_ptr_array_index (self->links, i));
}

static gboolean
lrg_markdown_label_handle_event (LrgWidget        *widget,
                                 const LrgUIEvent *event)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (widget);
	gfloat            ex = lrg_ui_event_get_x (event);
	gfloat            ey = lrg_ui_event_get_y (event);
	gint              n = (gint) self->links->len;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_link = lrg_widget_contains_point (widget, ex, ey) ? link_at (self, ex, ey) : -1;
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_link = -1;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->press_link = link_at (self, ex, ey);
		self->hover_link = self->press_link;
		if (self->press_link >= 0)
			self->focus_link = self->press_link;
		return self->press_link >= 0;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		{
			gint pressed = self->press_link;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			self->press_link = -1;
			if (pressed >= 0 && link_at (self, ex, ey) == pressed)
			{
				activate_link (self, pressed);
				return TRUE;
			}
			return pressed >= 0;
		}

	case LRG_UI_EVENT_FOCUS_IN:
		if (self->focus_link < 0 && n > 0)
			self->focus_link = 0;
		return FALSE;

	case LRG_UI_EVENT_KEY_DOWN:
		if (n == 0)
			return FALSE;
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_LEFT:
			if (self->focus_link <= 0)
				return FALSE;
			self->focus_link--;
			return TRUE;
		case GRL_KEY_RIGHT:
			if (self->focus_link >= n - 1)
				return FALSE;
			self->focus_link++;
			return TRUE;
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_ACTIVATE:
		if (n == 0)
			return FALSE;
		activate_link (self, self->focus_link < 0 ? 0 : self->focus_link);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_markdown_label_finalize (GObject *object)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (object);

	g_clear_pointer (&self->markdown, g_free);
	g_clear_pointer (&self->blocks, g_array_unref);
	g_clear_pointer (&self->links, g_ptr_array_unref);
	g_clear_pointer (&self->items, g_array_unref);

	G_OBJECT_CLASS (lrg_markdown_label_parent_class)->finalize (object);
}

static void
lrg_markdown_label_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (object);

	switch (prop_id)
	{
	case PROP_MARKDOWN:
		g_value_set_string (value, self->markdown);
		break;
	case PROP_FONT_SIZE:
		g_value_set_float (value, self->font_size);
		break;
	case PROP_TEXT_COLOR:
		g_value_set_boxed (value, self->text_color_set ? &self->text_color : NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_markdown_label_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgMarkdownLabel *self = LRG_MARKDOWN_LABEL (object);

	switch (prop_id)
	{
	case PROP_MARKDOWN:
		lrg_markdown_label_set_markdown (self, g_value_get_string (value));
		break;
	case PROP_FONT_SIZE:
		lrg_markdown_label_set_font_size (self, g_value_get_float (value));
		break;
	case PROP_TEXT_COLOR:
		lrg_markdown_label_set_text_color (self, g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_markdown_label_class_init (LrgMarkdownLabelClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->finalize = lrg_markdown_label_finalize;
	object_class->get_property = lrg_markdown_label_get_property;
	object_class->set_property = lrg_markdown_label_set_property;

	widget_class->style_defaults = lrg_markdown_label_style_defaults;
	widget_class->style_overrides = lrg_markdown_label_style_overrides;
	widget_class->measure = lrg_markdown_label_measure;
	widget_class->arrange = lrg_markdown_label_arrange;
	widget_class->paint = lrg_markdown_label_paint;
	widget_class->step = lrg_markdown_label_step;
	widget_class->handle_event = lrg_markdown_label_handle_event;

	/**
	 * LrgMarkdownLabel:markdown:
	 *
	 * The Markdown source.
	 */
	properties[PROP_MARKDOWN] =
		g_param_spec_string ("markdown", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMarkdownLabel:font-size:
	 *
	 * Body font size; 0 follows the theme.
	 */
	properties[PROP_FONT_SIZE] =
		g_param_spec_float ("font-size", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMarkdownLabel:text-color:
	 *
	 * Explicit body and heading colour, or %NULL for the theme's.
	 */
	properties[PROP_TEXT_COLOR] =
		g_param_spec_boxed ("text-color", NULL, NULL, GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgMarkdownLabel::link-activated:
	 * @self: the label
	 * @url: the link target
	 *
	 * Emitted when a link is clicked, or activated with Enter / Space /
	 * gamepad A while it has the keyboard cursor.
	 */
	signals[SIGNAL_LINK_ACTIVATED] =
		g_signal_new ("link-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_markdown_label_init (LrgMarkdownLabel *self)
{
	self->blocks = g_array_new (FALSE, TRUE, sizeof (Block));
	g_array_set_clear_func (self->blocks, block_clear);
	self->links = g_ptr_array_new_with_free_func (g_free);
	self->items = g_array_new (FALSE, TRUE, sizeof (Item));
	g_array_set_clear_func (self->items, item_clear);
	self->laid_width = -1.0f;
	self->hover_link = -1;
	self->press_link = -1;
	self->focus_link = -1;
	lrg_spring_init (&self->link_hover, 0.0f, LRG_MOTION_SMOOTH);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgMarkdownLabel *
lrg_markdown_label_new (const gchar *markdown)
{
	return g_object_new (LRG_TYPE_MARKDOWN_LABEL, "markdown", markdown, NULL);
}

const gchar *
lrg_markdown_label_get_markdown (LrgMarkdownLabel *self)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), NULL);

	return self->markdown;
}

void
lrg_markdown_label_set_markdown (LrgMarkdownLabel *self,
                                 const gchar      *markdown)
{
	g_return_if_fail (LRG_IS_MARKDOWN_LABEL (self));

	if (g_strcmp0 (markdown, self->markdown) == 0)
		return;
	g_free (self->markdown);
	self->markdown = g_strdup (markdown);
	parse_blocks (self);
	invalidate (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKDOWN]);
}

gfloat
lrg_markdown_label_get_font_size (LrgMarkdownLabel *self)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), 0.0f);

	return self->font_size;
}

void
lrg_markdown_label_set_font_size (LrgMarkdownLabel *self,
                                  gfloat            size)
{
	g_return_if_fail (LRG_IS_MARKDOWN_LABEL (self));

	size = MAX (size, 0.0f);
	if (size == self->font_size)
		return;
	self->font_size = size;
	invalidate (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
}

const GrlColor *
lrg_markdown_label_get_text_color (LrgMarkdownLabel *self)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), NULL);

	return self->text_color_set ? &self->text_color : NULL;
}

void
lrg_markdown_label_set_text_color (LrgMarkdownLabel *self,
                                   const GrlColor   *color)
{
	g_return_if_fail (LRG_IS_MARKDOWN_LABEL (self));

	if (color == NULL && !self->text_color_set)
		return;
	if (color != NULL && self->text_color_set && memcmp (color, &self->text_color, sizeof (GrlColor)) == 0)
		return;
	self->text_color_set = color != NULL;
	if (color != NULL)
		self->text_color = *color;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

guint
lrg_markdown_label_get_n_blocks (LrgMarkdownLabel *self)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), 0);

	return self->blocks->len;
}

LrgMarkdownBlockKind
lrg_markdown_label_get_block_kind (LrgMarkdownLabel *self,
                                   guint             index)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), LRG_MARKDOWN_BLOCK_PARAGRAPH);

	if (index >= self->blocks->len)
		return LRG_MARKDOWN_BLOCK_PARAGRAPH;
	return g_array_index (self->blocks, Block, index).kind;
}

gint
lrg_markdown_label_get_block_level (LrgMarkdownLabel *self,
                                    guint             index)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), 0);

	if (index >= self->blocks->len)
		return 0;
	return g_array_index (self->blocks, Block, index).level;
}

const gchar *
lrg_markdown_label_get_block_text (LrgMarkdownLabel *self,
                                   guint             index)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), NULL);

	if (index >= self->blocks->len)
		return NULL;
	return g_array_index (self->blocks, Block, index).plain;
}

guint
lrg_markdown_label_get_block_n_spans (LrgMarkdownLabel *self,
                                      guint             index)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), 0);

	if (index >= self->blocks->len)
		return 0;
	return g_array_index (self->blocks, Block, index).spans->len;
}

const gchar *
lrg_markdown_label_get_block_span (LrgMarkdownLabel     *self,
                                   guint                 index,
                                   guint                 span,
                                   LrgMarkdownSpanStyle *style)
{
	Block *b;
	Span  *s;

	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), NULL);

	if (index >= self->blocks->len)
		return NULL;
	b = &g_array_index (self->blocks, Block, index);
	if (span >= b->spans->len)
		return NULL;
	s = &g_array_index (b->spans, Span, span);
	if (style != NULL)
		*style = (LrgMarkdownSpanStyle) (s->style & 0xff);
	return s->text;
}

guint
lrg_markdown_label_get_n_links (LrgMarkdownLabel *self)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), 0);

	return self->links->len;
}

const gchar *
lrg_markdown_label_get_link_url (LrgMarkdownLabel *self,
                                 guint             index)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), NULL);

	if (index >= self->links->len)
		return NULL;
	return g_ptr_array_index (self->links, index);
}

gboolean
lrg_markdown_label_get_link_bounds (LrgMarkdownLabel *self,
                                    guint             index,
                                    GrlRectangle     *bounds)
{
	guint i;

	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), FALSE);
	g_return_val_if_fail (bounds != NULL, FALSE);

	ensure_layout (self);
	for (i = 0; i < self->items->len; i++)
	{
		Item *it = &g_array_index (self->items, Item, i);

		if (it->kind != ITEM_TEXT || it->link != (gint) index)
			continue;
		bounds->x = lrg_widget_get_world_x (LRG_WIDGET (self)) + it->x;
		bounds->y = lrg_widget_get_world_y (LRG_WIDGET (self)) + it->y;
		bounds->width = it->width;
		bounds->height = it->height;
		return TRUE;
	}
	return FALSE;
}

guint
lrg_markdown_label_get_n_lines (LrgMarkdownLabel *self)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), 0);

	ensure_layout (self);
	return self->n_lines;
}

LrgRichText *
lrg_markdown_label_get_block_rich_text (LrgMarkdownLabel *self,
                                        guint             index)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), NULL);

	if (index >= self->blocks->len)
		return NULL;
	return g_array_index (self->blocks, Block, index).rich;
}

const gchar *
lrg_markdown_label_get_block_markup (LrgMarkdownLabel *self,
                                     guint             index)
{
	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), NULL);

	if (index >= self->blocks->len)
		return NULL;
	return g_array_index (self->blocks, Block, index).markup;
}

void
lrg_markdown_label_reset_effects (LrgMarkdownLabel *self)
{
	guint i;

	g_return_if_fail (LRG_IS_MARKDOWN_LABEL (self));

	for (i = 0; i < self->blocks->len; i++)
	{
		Block *b = &g_array_index (self->blocks, Block, i);

		lrg_rich_text_reset_effects (b->rich);
	}
}

gboolean
lrg_markdown_label_get_effects_complete (LrgMarkdownLabel *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_MARKDOWN_LABEL (self), TRUE);

	for (i = 0; i < self->blocks->len; i++)
		if (!lrg_rich_text_effects_complete (g_array_index (self->blocks, Block, i).rich))
			return FALSE;
	return TRUE;
}
