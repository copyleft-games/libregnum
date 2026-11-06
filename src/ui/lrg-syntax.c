/* lrg-syntax.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSyntax: an incremental, line-at-a-time syntax highlighter.
 *
 * A language is a small table-driven description (word lists, comment
 * markers, string quotes, a few flags) on top of one of four lexers:
 * code, Markdown, YAML and INI. Every lexer works on one line at a time
 * and carries what continues into the next line (an open block comment,
 * a multi-line string, a fenced code block, a YAML block scalar) in a
 * single guint state:
 *
 *   bits 16..23  kind of construct (ST_*)
 *   bits  0..15  its argument (comment depth, string index, quote, ...)
 *
 * State 0 means "nothing open". An editor that knows the state each
 * line started with re-tokenizes a changed line, then the following
 * lines only while the state entering them differs from last time; as
 * soon as a line starts in the same state as before, everything below
 * it is known to be unchanged.
 *
 * Built-in languages are created on first use and kept in a registry
 * together with languages the application registers (YAML or code);
 * later registrations win, so a built-in can be replaced.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <string.h>
#include <yaml-glib.h>

#include "lrg-syntax.h"
#include "lrg-style.h"
#include "../paint/lrg-paint-types.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Enum / boxed types
 * ========================================================================== */

GType
lrg_syntax_token_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SYNTAX_TOKEN_TEXT, "LRG_SYNTAX_TOKEN_TEXT", "text" },
			{ LRG_SYNTAX_TOKEN_KEYWORD, "LRG_SYNTAX_TOKEN_KEYWORD", "keyword" },
			{ LRG_SYNTAX_TOKEN_TYPE, "LRG_SYNTAX_TOKEN_TYPE", "type" },
			{ LRG_SYNTAX_TOKEN_FUNCTION, "LRG_SYNTAX_TOKEN_FUNCTION", "function" },
			{ LRG_SYNTAX_TOKEN_STRING, "LRG_SYNTAX_TOKEN_STRING", "string" },
			{ LRG_SYNTAX_TOKEN_NUMBER, "LRG_SYNTAX_TOKEN_NUMBER", "number" },
			{ LRG_SYNTAX_TOKEN_COMMENT, "LRG_SYNTAX_TOKEN_COMMENT", "comment" },
			{ LRG_SYNTAX_TOKEN_PREPROCESSOR, "LRG_SYNTAX_TOKEN_PREPROCESSOR", "preprocessor" },
			{ LRG_SYNTAX_TOKEN_OPERATOR, "LRG_SYNTAX_TOKEN_OPERATOR", "operator" },
			{ LRG_SYNTAX_TOKEN_PUNCTUATION, "LRG_SYNTAX_TOKEN_PUNCTUATION", "punctuation" },
			{ LRG_SYNTAX_TOKEN_CONSTANT, "LRG_SYNTAX_TOKEN_CONSTANT", "constant" },
			{ LRG_SYNTAX_TOKEN_ATTRIBUTE, "LRG_SYNTAX_TOKEN_ATTRIBUTE", "attribute" },
			{ LRG_SYNTAX_TOKEN_ESCAPE, "LRG_SYNTAX_TOKEN_ESCAPE", "escape" },
			{ LRG_SYNTAX_TOKEN_VARIABLE, "LRG_SYNTAX_TOKEN_VARIABLE", "variable" },
			{ LRG_SYNTAX_TOKEN_HEADING, "LRG_SYNTAX_TOKEN_HEADING", "heading" },
			{ LRG_SYNTAX_TOKEN_EMPHASIS, "LRG_SYNTAX_TOKEN_EMPHASIS", "emphasis" },
			{ LRG_SYNTAX_TOKEN_LINK, "LRG_SYNTAX_TOKEN_LINK", "link" },
			{ LRG_SYNTAX_TOKEN_ERROR, "LRG_SYNTAX_TOKEN_ERROR", "error" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSyntaxToken"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_syntax_base_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SYNTAX_BASE_CODE, "LRG_SYNTAX_BASE_CODE", "code" },
			{ LRG_SYNTAX_BASE_MARKDOWN, "LRG_SYNTAX_BASE_MARKDOWN", "markdown" },
			{ LRG_SYNTAX_BASE_YAML, "LRG_SYNTAX_BASE_YAML", "yaml" },
			{ LRG_SYNTAX_BASE_INI, "LRG_SYNTAX_BASE_INI", "ini" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSyntaxBase"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

G_DEFINE_QUARK (lrg-syntax-error-quark, lrg_syntax_error)

LrgSyntaxSpan *
lrg_syntax_span_copy (const LrgSyntaxSpan *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return (LrgSyntaxSpan *) g_memdup2 (self, sizeof (LrgSyntaxSpan));
}

void
lrg_syntax_span_free (LrgSyntaxSpan *self)
{
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgSyntaxSpan, lrg_syntax_span, lrg_syntax_span_copy, lrg_syntax_span_free)

/* ==========================================================================
 * State encoding
 * ========================================================================== */

#define ST_NONE       (0u)
#define ST_BLOCK      (1u)  /* inside a block comment; arg = depth */
#define ST_MLSTRING   (2u)  /* inside a multi-line string; arg = index */
#define ST_STRING     (3u)  /* a quoted string continues; arg = quote byte */
#define ST_FENCE      (4u)  /* Markdown fenced code; arg = fence char << 8 | count */
#define ST_YAML_BLOCK (5u)  /* YAML block scalar; arg = indent of its parent line */

#define ST_MAKE(kind, arg) ((((guint) (kind)) << 16) | (((guint) (arg)) & 0xffffu))
#define ST_KIND(state)     (((state) >> 16) & 0xffu)
#define ST_ARG(state)      ((state) & 0xffffu)

/* Longest word looked up in the word table (longer words are never keywords) */
#define MAX_WORD (64)

/* Characters lexed as operators and as punctuation in code */
#define OPERATOR_CHARS    "+-*/%=<>!&|^~?:"
#define PUNCTUATION_CHARS "()[]{},;."

/* ==========================================================================
 * The language
 * ========================================================================== */

typedef struct
{
	gchar *start;
	gchar *end;
} Pair;

struct _LrgSyntax
{
	GObject        parent_instance;

	gchar         *name;
	LrgSyntaxBase  base;

	GHashTable    *words;          /* gchar* (folded when case-insensitive) -> token */
	GPtrArray     *line_comments;  /* gchar* */
	gchar         *block_start;
	gchar         *block_end;
	gboolean       nested;

	gchar         *quotes;
	gchar         *raw_quotes;
	gunichar       escape;
	gboolean       strings_span;
	GArray        *ml_strings;     /* Pair */

	gchar         *preprocessor;
	gchar         *variable_prefix;
	gchar         *attribute_prefix;
	gchar         *word_chars;
	GRegex        *number_regex;

	gboolean       case_sensitive;
	gboolean       functions;
	gboolean       caps_constants;
	gboolean       keys;
	gboolean       strict;
	gboolean       comment_needs_space;

	GPtrArray     *patterns;       /* gchar* */
	GPtrArray     *aliases;        /* gchar* */

	GrlColor       colors[LRG_SYNTAX_N_TOKENS];
	guint32        colors_set;     /* bit per token */
};

G_DEFINE_FINAL_TYPE (LrgSyntax, lrg_syntax, G_TYPE_OBJECT)

static void
clear_pair (gpointer data)
{
	Pair *p = data;

	g_free (p->start);
	g_free (p->end);
}

static void
lrg_syntax_finalize (GObject *object)
{
	LrgSyntax *self = LRG_SYNTAX (object);

	g_free (self->name);
	g_hash_table_unref (self->words);
	g_ptr_array_unref (self->line_comments);
	g_free (self->block_start);
	g_free (self->block_end);
	g_free (self->quotes);
	g_free (self->raw_quotes);
	g_array_unref (self->ml_strings);
	g_free (self->preprocessor);
	g_free (self->variable_prefix);
	g_free (self->attribute_prefix);
	g_free (self->word_chars);
	g_clear_pointer (&self->number_regex, g_regex_unref);
	g_ptr_array_unref (self->patterns);
	g_ptr_array_unref (self->aliases);

	G_OBJECT_CLASS (lrg_syntax_parent_class)->finalize (object);
}

static void
lrg_syntax_class_init (LrgSyntaxClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_syntax_finalize;
}

static void
lrg_syntax_init (LrgSyntax *self)
{
	self->words = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->line_comments = g_ptr_array_new_with_free_func (g_free);
	self->ml_strings = g_array_new (FALSE, TRUE, sizeof (Pair));
	g_array_set_clear_func (self->ml_strings, clear_pair);
	self->patterns = g_ptr_array_new_with_free_func (g_free);
	self->aliases = g_ptr_array_new_with_free_func (g_free);
	self->case_sensitive = TRUE;
	self->functions = TRUE;
}

LrgSyntax *
lrg_syntax_new (const gchar   *name,
                LrgSyntaxBase  base)
{
	LrgSyntax *self;

	g_return_val_if_fail (name != NULL, NULL);

	self = g_object_new (LRG_TYPE_SYNTAX, NULL);
	self->name = g_strdup (name);
	self->base = base;

	/* Every base understands quoted strings with backslash escapes */
	self->quotes = g_strdup ("\"'");
	self->escape = '\\';
	return self;
}

/* ==========================================================================
 * Definition setters
 * ========================================================================== */

const gchar *
lrg_syntax_get_name (LrgSyntax *self)
{
	g_return_val_if_fail (LRG_IS_SYNTAX (self), NULL);
	return self->name;
}

LrgSyntaxBase
lrg_syntax_get_base (LrgSyntax *self)
{
	g_return_val_if_fail (LRG_IS_SYNTAX (self), LRG_SYNTAX_BASE_CODE);
	return self->base;
}

/* fold_word: the table key of @word (lower-cased when case-insensitive) */
static gchar *
fold_word (LrgSyntax   *self,
           const gchar *word,
           gssize       len)
{
	return self->case_sensitive ? g_strndup (word, len < 0 ? strlen (word) : (gsize) len)
	                            : g_ascii_strdown (word, len);
}

void
lrg_syntax_add_words (LrgSyntax           *self,
                      LrgSyntaxToken       token,
                      const gchar * const *words)
{
	guint i;

	g_return_if_fail (LRG_IS_SYNTAX (self));
	g_return_if_fail ((guint) token < LRG_SYNTAX_N_TOKENS);

	if (words == NULL)
		return;
	for (i = 0; words[i] != NULL; i++)
		g_hash_table_replace (self->words, fold_word (self, words[i], -1), GINT_TO_POINTER (token));
}

LrgSyntaxToken
lrg_syntax_lookup_word (LrgSyntax   *self,
                        const gchar *word)
{
	g_autofree gchar *key = NULL;

	g_return_val_if_fail (LRG_IS_SYNTAX (self), LRG_SYNTAX_TOKEN_TEXT);
	g_return_val_if_fail (word != NULL, LRG_SYNTAX_TOKEN_TEXT);

	key = fold_word (self, word, -1);
	return (LrgSyntaxToken) GPOINTER_TO_INT (g_hash_table_lookup (self->words, key));
}

void
lrg_syntax_add_line_comment (LrgSyntax   *self,
                             const gchar *marker)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	if (marker != NULL && marker[0] != '\0')
		g_ptr_array_add (self->line_comments, g_strdup (marker));
}

void
lrg_syntax_set_block_comment (LrgSyntax   *self,
                              const gchar *start,
                              const gchar *end,
                              gboolean     nested)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));

	g_clear_pointer (&self->block_start, g_free);
	g_clear_pointer (&self->block_end, g_free);
	if (start != NULL && start[0] != '\0' && end != NULL && end[0] != '\0')
	{
		self->block_start = g_strdup (start);
		self->block_end = g_strdup (end);
	}
	self->nested = nested;
}

void
lrg_syntax_set_string_quotes (LrgSyntax   *self,
                              const gchar *quotes,
                              gunichar     escape,
                              gboolean     span_lines)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));

	g_free (self->quotes);
	self->quotes = g_strdup (quotes != NULL ? quotes : "");
	self->escape = escape < 0x80 ? escape : 0;
	self->strings_span = span_lines;
}

void
lrg_syntax_set_raw_quotes (LrgSyntax   *self,
                           const gchar *quotes)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	g_free (self->raw_quotes);
	self->raw_quotes = g_strdup (quotes);
}

void
lrg_syntax_add_multiline_string (LrgSyntax   *self,
                                 const gchar *start,
                                 const gchar *end)
{
	Pair p;

	g_return_if_fail (LRG_IS_SYNTAX (self));
	g_return_if_fail (start != NULL && start[0] != '\0');
	g_return_if_fail (end != NULL && end[0] != '\0');

	/* The index is stored in the state's 16-bit argument */
	if (self->ml_strings->len >= 0xffff)
		return;
	p.start = g_strdup (start);
	p.end = g_strdup (end);
	g_array_append_val (self->ml_strings, p);
}

void
lrg_syntax_set_preprocessor (LrgSyntax   *self,
                             const gchar *prefix)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	g_free (self->preprocessor);
	self->preprocessor = (prefix != NULL && prefix[0] != '\0') ? g_strdup (prefix) : NULL;
}

void
lrg_syntax_set_prefixes (LrgSyntax   *self,
                         const gchar *variable,
                         const gchar *attribute)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	g_free (self->variable_prefix);
	g_free (self->attribute_prefix);
	self->variable_prefix = (variable != NULL && variable[0] != '\0') ? g_strdup (variable) : NULL;
	self->attribute_prefix = (attribute != NULL && attribute[0] != '\0') ? g_strdup (attribute) : NULL;
}

void
lrg_syntax_set_word_chars (LrgSyntax   *self,
                           const gchar *chars)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	g_free (self->word_chars);
	self->word_chars = (chars != NULL && chars[0] != '\0') ? g_strdup (chars) : NULL;
}

gboolean
lrg_syntax_set_number_pattern (LrgSyntax    *self,
                               const gchar  *pattern,
                               GError      **error)
{
	GRegex *regex = NULL;

	g_return_val_if_fail (LRG_IS_SYNTAX (self), FALSE);

	if (pattern != NULL && pattern[0] != '\0')
	{
		regex = g_regex_new (pattern, G_REGEX_OPTIMIZE, G_REGEX_MATCH_ANCHORED, error);
		if (regex == NULL)
			return FALSE;
	}
	g_clear_pointer (&self->number_regex, g_regex_unref);
	self->number_regex = regex;
	return TRUE;
}

void
lrg_syntax_set_options (LrgSyntax *self,
                        gboolean   case_sensitive,
                        gboolean   functions,
                        gboolean   caps_constants,
                        gboolean   keys)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));

	/* Re-key the word table when the case rule changes */
	if (self->case_sensitive != case_sensitive)
	{
		GHashTable     *old = self->words;
		GHashTableIter  iter;
		gpointer        k;
		gpointer        v;

		self->case_sensitive = case_sensitive;
		self->words = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		g_hash_table_iter_init (&iter, old);
		while (g_hash_table_iter_next (&iter, &k, &v))
			g_hash_table_replace (self->words, fold_word (self, k, -1), v);
		g_hash_table_unref (old);
	}
	self->functions = functions;
	self->caps_constants = caps_constants;
	self->keys = keys;
}

void
lrg_syntax_set_strict (LrgSyntax *self,
                       gboolean   strict)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	self->strict = strict;
}

void
lrg_syntax_set_comment_needs_space (LrgSyntax *self,
                                    gboolean   needs_space)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	self->comment_needs_space = needs_space;
}

void
lrg_syntax_add_pattern (LrgSyntax   *self,
                        const gchar *pattern)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	if (pattern != NULL && pattern[0] != '\0')
		g_ptr_array_add (self->patterns, g_strdup (pattern));
}

void
lrg_syntax_add_alias (LrgSyntax   *self,
                      const gchar *alias)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	if (alias != NULL && alias[0] != '\0')
		g_ptr_array_add (self->aliases, g_ascii_strdown (alias, -1));
}

gboolean
lrg_syntax_matches_filename (LrgSyntax   *self,
                             const gchar *filename)
{
	g_autofree gchar *base = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_SYNTAX (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	base = g_path_get_basename (filename);
	for (i = 0; i < self->patterns->len; i++)
		if (g_pattern_match_simple (g_ptr_array_index (self->patterns, i), base))
			return TRUE;
	return FALSE;
}

/* ==========================================================================
 * Lexing helpers
 * ========================================================================== */

typedef struct
{
	LrgSyntax   *syn;
	const gchar *text;
	gsize        len;
	GArray      *spans;  /* nullable: state-only pass */
} Lex;

/*
 * emit:
 *
 * Appends a span. TEXT spans are not stored (they are the gaps), empty
 * spans are dropped, and a span that continues the previous one with
 * the same kind extends it, so a run of operators is a single span.
 */
static void
emit (Lex            *lx,
      gsize           start,
      gsize           end,
      LrgSyntaxToken  token)
{
	LrgSyntaxSpan span;

	if (lx->spans == NULL || end <= start || token == LRG_SYNTAX_TOKEN_TEXT)
		return;
	if (lx->spans->len > 0)
	{
		LrgSyntaxSpan *last = &g_array_index (lx->spans, LrgSyntaxSpan, lx->spans->len - 1);

		if (last->token == token && last->end == (guint) start)
		{
			last->end = (guint) end;
			return;
		}
	}
	span.start = (guint) start;
	span.end = (guint) end;
	span.token = token;
	g_array_append_val (lx->spans, span);
}

/* retag: give every span from index @from on the kind @token */
static void
retag (Lex            *lx,
       guint           from,
       LrgSyntaxToken  token)
{
	guint i;

	if (lx->spans == NULL)
		return;
	for (i = from; i < lx->spans->len; i++)
		g_array_index (lx->spans, LrgSyntaxSpan, i).token = token;

	/* collapse into one span */
	if (lx->spans->len > from + 1)
	{
		g_array_index (lx->spans, LrgSyntaxSpan, from).end =
			g_array_index (lx->spans, LrgSyntaxSpan, lx->spans->len - 1).end;
		g_array_set_size (lx->spans, from + 1);
	}
}

static guint
n_spans (Lex *lx)
{
	return lx->spans != NULL ? lx->spans->len : 0;
}

/* starts: does @word occur at @at? */
static gboolean
starts (Lex         *lx,
        gsize        at,
        const gchar *word)
{
	gsize n;

	if (word == NULL)
		return FALSE;
	n = strlen (word);
	return n > 0 && at + n <= lx->len && memcmp (lx->text + at, word, n) == 0;
}

/* find: first occurrence of @word at or after @at, or -1 */
static gssize
find (Lex         *lx,
      gsize        at,
      const gchar *word)
{
	gsize n = strlen (word);
	gsize i;

	for (i = at; i + n <= lx->len; i++)
		if (memcmp (lx->text + i, word, n) == 0)
			return (gssize) i;
	return -1;
}

static gboolean
in_set (const gchar *set,
        gchar        c)
{
	return set != NULL && c != '\0' && strchr (set, c) != NULL;
}

static gboolean
is_space (gchar c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* is_word: identifier character (letters, digits, '_', UTF-8, extras) */
static gboolean
is_word (LrgSyntax *syn,
         gchar      c)
{
	return g_ascii_isalnum (c) || c == '_' || (guchar) c >= 0x80 || in_set (syn->word_chars, c);
}

/* next_char: byte after the UTF-8 character at @i */
static gsize
next_char (Lex   *lx,
           gsize  i)
{
	const gchar *p = g_utf8_find_next_char (lx->text + i, lx->text + lx->len);

	return p != NULL ? (gsize) (p - lx->text) : lx->len;
}

/* skip_spaces: first non-space byte at or after @i */
static gsize
skip_spaces (Lex   *lx,
             gsize  i)
{
	while (i < lx->len && is_space (lx->text[i]))
		i++;
	return i;
}

/*
 * lex_string_body:
 * @i: first byte after the opening quote (or of the line, when resuming)
 * @seg: where the STRING span being built starts
 * @esc: the escape character, '\0' for none
 * @closed: (out): the closing quote was found
 * @continued: (out): the line ended in an escape (the string continues)
 *
 * Scans a quoted string to its closing @quote, emitting STRING runs
 * and ESCAPE spans for escape sequences (not in raw quotes).
 *
 * Returns: the byte after the string
 */
static gsize
lex_string_body (Lex      *lx,
                 gsize     i,
                 gsize     seg,
                 gchar     quote,
                 gchar     esc,
                 gboolean *closed,
                 gboolean *continued)
{
	*closed = FALSE;
	*continued = FALSE;

	while (i < lx->len)
	{
		gchar c = lx->text[i];

		if (esc != '\0' && c == esc)
		{
			gsize end;

			emit (lx, seg, i, LRG_SYNTAX_TOKEN_STRING);
			if (i + 1 >= lx->len)
			{
				/* escaped line end: the string goes on */
				emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_ESCAPE);
				*continued = TRUE;
				return lx->len;
			}
			end = next_char (lx, i + 1);
			emit (lx, i, end, LRG_SYNTAX_TOKEN_ESCAPE);
			i = end;
			seg = i;
			continue;
		}
		if (c == quote)
		{
			i++;
			emit (lx, seg, i, LRG_SYNTAX_TOKEN_STRING);
			*closed = TRUE;
			return i;
		}
		i++;
	}
	emit (lx, seg, lx->len, LRG_SYNTAX_TOKEN_STRING);
	return lx->len;
}

/* string_escape: the escape character inside @quote strings, or '\0' */
static gchar
string_escape (Lex   *lx,
               gchar  quote)
{
	if (in_set (lx->syn->raw_quotes, quote))
		return '\0';
	return (gchar) lx->syn->escape;
}

/*
 * lex_block_comment:
 * @from: where the COMMENT span starts (the opener, or 0 when resuming)
 * @i: where scanning continues
 * @depth: (inout): open comments; 0 when the comment closed
 *
 * Returns: the byte after the comment, or the line end
 */
static gsize
lex_block_comment (Lex   *lx,
                   gsize  from,
                   gsize  i,
                   guint *depth)
{
	LrgSyntax *syn = lx->syn;
	gsize      elen = strlen (syn->block_end);
	gsize      slen = strlen (syn->block_start);

	while (i < lx->len && *depth > 0)
	{
		if (starts (lx, i, syn->block_end))
		{
			i += elen;
			(*depth)--;
		}
		else if (syn->nested && *depth < 0xffff && starts (lx, i, syn->block_start))
		{
			i += slen;
			(*depth)++;
		}
		else
			i++;
	}
	emit (lx, from, i, LRG_SYNTAX_TOKEN_COMMENT);
	return i;
}

/*
 * scan_number:
 *
 * Length of a number at @i: the language's pattern when it has one,
 * else hex (0x...), decimals with fraction and exponent, '_'
 * separators and suffixes (1.5f, 10ULL). 0 if there is no number.
 */
static gsize
scan_number (Lex   *lx,
             gsize  i)
{
	const gchar *t = lx->text;
	gsize        end = i;

	if (i >= lx->len)
		return 0;
	if (lx->syn->number_regex != NULL)
	{
		g_autoptr(GMatchInfo) info = NULL;
		gint                  s;
		gint                  e;

		if (g_regex_match_full (lx->syn->number_regex, t, (gssize) lx->len, (gint) i,
		                        G_REGEX_MATCH_ANCHORED, &info, NULL) &&
		    g_match_info_fetch_pos (info, 0, &s, &e) && s == (gint) i && e > s)
			return (gsize) (e - s);
		return 0;
	}

	if (!(g_ascii_isdigit (t[i]) || (t[i] == '.' && i + 1 < lx->len && g_ascii_isdigit (t[i + 1]))))
		return 0;

	if (t[i] == '0' && i + 1 < lx->len && (t[i + 1] == 'x' || t[i + 1] == 'X'))
	{
		end = i + 2;
		while (end < lx->len && (g_ascii_isxdigit (t[end]) || t[end] == '_'))
			end++;
	}
	else
	{
		while (end < lx->len && (g_ascii_isdigit (t[end]) || t[end] == '_'))
			end++;
		if (end < lx->len && t[end] == '.' && !(end + 1 < lx->len && t[end + 1] == '.'))
		{
			end++;
			while (end < lx->len && (g_ascii_isdigit (t[end]) || t[end] == '_'))
				end++;
		}
		if (end < lx->len && (t[end] == 'e' || t[end] == 'E'))
		{
			gsize e = end + 1;

			if (e < lx->len && (t[e] == '+' || t[e] == '-'))
				e++;
			if (e < lx->len && g_ascii_isdigit (t[e]))
			{
				end = e;
				while (end < lx->len && g_ascii_isdigit (t[end]))
					end++;
			}
		}
	}
	/* suffixes: u, l, f, ULL, i32 ... */
	while (end < lx->len && g_ascii_isalnum (t[end]))
		end++;
	return end - i;
}

/* word_kind: the table entry for text[i, end), TEXT if absent */
static LrgSyntaxToken
word_kind (Lex   *lx,
           gsize  i,
           gsize  end)
{
	gchar buf[MAX_WORD + 1];
	gsize n = end - i;
	gsize k;

	if (n == 0 || n > MAX_WORD)
		return LRG_SYNTAX_TOKEN_TEXT;
	memcpy (buf, lx->text + i, n);
	buf[n] = '\0';
	if (!lx->syn->case_sensitive)
		for (k = 0; k < n; k++)
			buf[k] = g_ascii_tolower (buf[k]);
	return (LrgSyntaxToken) GPOINTER_TO_INT (g_hash_table_lookup (lx->syn->words, buf));
}

/* is_caps: an ALL_CAPS identifier of two or more characters */
static gboolean
is_caps (Lex   *lx,
         gsize  i,
         gsize  end)
{
	gboolean letter = FALSE;
	gsize    k;

	if (end - i < 2)
		return FALSE;
	for (k = i; k < end; k++)
	{
		gchar c = lx->text[k];

		if (g_ascii_isupper (c))
			letter = TRUE;
		else if (!(g_ascii_isdigit (c) || c == '_'))
			return FALSE;
	}
	return letter;
}

/* followed_by: is the next non-space character after @i @c? */
static gboolean
followed_by (Lex   *lx,
             gsize  i,
             gchar  c)
{
	i = skip_spaces (lx, i);
	return i < lx->len && lx->text[i] == c;
}

/* key_colon: ':' that ends a key (not "::") after @i */
static gboolean
key_colon (Lex   *lx,
           gsize  i)
{
	i = skip_spaces (lx, i);
	return i < lx->len && lx->text[i] == ':' && !(i + 1 < lx->len && lx->text[i + 1] == ':');
}

/* line_comment_at: a line comment marker starting at @i */
static gboolean
line_comment_at (Lex   *lx,
                 gsize  i)
{
	guint k;

	if (lx->syn->comment_needs_space && i > 0 && !is_space (lx->text[i - 1]))
		return FALSE;
	for (k = 0; k < lx->syn->line_comments->len; k++)
		if (starts (lx, i, g_ptr_array_index (lx->syn->line_comments, k)))
			return TRUE;
	return FALSE;
}

/* ==========================================================================
 * Code lexer
 * ========================================================================== */

/*
 * lex_quoted:
 * @i: position of the opening quote
 * @end_state: (out): state if the string stays open, else ST_NONE
 *
 * A complete quoted string from its opening quote. For key languages
 * (JSON) a string followed by ':' becomes an ATTRIBUTE.
 *
 * Returns: the byte after the string (the line end when it stays open)
 */
static gsize
lex_quoted (Lex   *lx,
            gsize  i,
            guint *end_state)
{
	gchar    quote = lx->text[i];
	guint    first = n_spans (lx);
	gboolean closed;
	gboolean continued;
	gsize    end;

	*end_state = ST_NONE;
	end = lex_string_body (lx, i + 1, i, quote, string_escape (lx, quote), &closed, &continued);
	if (!closed)
	{
		if (continued || lx->syn->strings_span)
			*end_state = ST_MAKE (ST_STRING, (guchar) quote);
		return end;
	}
	if (lx->syn->keys && key_colon (lx, end))
		retag (lx, first, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	return end;
}

/*
 * lex_variable:
 *
 * A sigil variable at @i: "${...}" to the closing brace, "$name",
 * or one special character ("$1", "$@", "$?").
 *
 * Returns: the byte after it, or @i when it is not a variable
 */
static gsize
lex_variable (Lex   *lx,
              gsize  i)
{
	gsize end = i + 1;

	if (end >= lx->len)
		return i;
	if (lx->text[end] == '{')
	{
		while (end < lx->len && lx->text[end] != '}')
			end++;
		end = MIN (end + 1, lx->len);
	}
	else if (is_word (lx->syn, lx->text[end]))
	{
		while (end < lx->len && is_word (lx->syn, lx->text[end]))
			end++;
	}
	else if (in_set ("@#?$!*-", lx->text[end]))
		end++;
	else
		return i;
	emit (lx, i, end, LRG_SYNTAX_TOKEN_VARIABLE);
	return end;
}

/*
 * lex_code:
 * @i: where to start (0, or after an INI key)
 * @state: state entering the line
 *
 * The code lexer: resumes an open construct, then scans tokens in this
 * order: block comment, line comment, preprocessor directive (first
 * token only), multi-line string, quoted string, attribute / variable
 * sigils, number, identifier (word table, string prefixes, ALL_CAPS
 * constants, function calls, keys), operators and punctuation.
 *
 * Returns: the state at the end of the line
 */
static guint
lex_code (Lex   *lx,
          gsize  i,
          guint  state)
{
	LrgSyntax   *syn = lx->syn;
	const gchar *t = lx->text;
	gboolean     first = TRUE;
	guint        kind = ST_KIND (state);

	/* 1. Resume what the previous line left open */
	if (kind == ST_BLOCK && syn->block_start != NULL)
	{
		guint depth = MAX (1u, ST_ARG (state));

		i = lex_block_comment (lx, i, i, &depth);
		if (depth > 0)
			return ST_MAKE (ST_BLOCK, depth);
		first = FALSE;
	}
	else if (kind == ST_MLSTRING && ST_ARG (state) < syn->ml_strings->len)
	{
		Pair  *p = &g_array_index (syn->ml_strings, Pair, ST_ARG (state));
		gssize e = find (lx, i, p->end);

		if (e < 0)
		{
			emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_STRING);
			return state;
		}
		emit (lx, i, (gsize) e + strlen (p->end), LRG_SYNTAX_TOKEN_STRING);
		i = (gsize) e + strlen (p->end);
		first = FALSE;
	}
	else if (kind == ST_STRING)
	{
		gboolean closed;
		gboolean continued;

		i = lex_string_body (lx, i, i, (gchar) ST_ARG (state), string_escape (lx, (gchar) ST_ARG (state)),
		                     &closed, &continued);
		if (!closed)
			return (continued || syn->strings_span) ? state : ST_NONE;
		first = FALSE;
	}

	/* 2. Tokens */
	while (i < lx->len)
	{
		gchar c = t[i];
		gsize end;
		guint k;

		if (is_space (c))
		{
			i++;
			continue;
		}

		/* Block comments first: "--[[" must win over the "--" line comment */
		if (syn->block_start != NULL && starts (lx, i, syn->block_start))
		{
			guint depth = 1;

			i = lex_block_comment (lx, i, i + strlen (syn->block_start), &depth);
			if (depth > 0)
				return ST_MAKE (ST_BLOCK, depth);
			first = FALSE;
			continue;
		}
		if (line_comment_at (lx, i))
		{
			emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_COMMENT);
			return ST_NONE;
		}

		/* #include <file> / #define NAME: the directive word, then
		 * a <header> as a string; the rest lexes normally */
		if (first && syn->preprocessor != NULL && starts (lx, i, syn->preprocessor))
		{
			end = skip_spaces (lx, i + strlen (syn->preprocessor));
			while (end < lx->len && is_word (syn, t[end]))
				end++;
			emit (lx, i, end, LRG_SYNTAX_TOKEN_PREPROCESSOR);
			i = skip_spaces (lx, end);
			if (i < lx->len && t[i] == '<')
			{
				end = i;
				while (end < lx->len && t[end] != '>')
					end++;
				if (end < lx->len)
				{
					emit (lx, i, end + 1, LRG_SYNTAX_TOKEN_STRING);
					i = end + 1;
				}
			}
			first = FALSE;
			continue;
		}
		first = FALSE;

		/* Multi-line strings ("""...""", [[...]]) */
		for (k = 0; k < syn->ml_strings->len; k++)
			if (starts (lx, i, g_array_index (syn->ml_strings, Pair, k).start))
				break;
		if (k < syn->ml_strings->len)
		{
			Pair  *p = &g_array_index (syn->ml_strings, Pair, k);
			gssize e = find (lx, i + strlen (p->start), p->end);

			if (e < 0)
			{
				emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_STRING);
				return ST_MAKE (ST_MLSTRING, k);
			}
			end = (gsize) e + strlen (p->end);
			emit (lx, i, end, LRG_SYNTAX_TOKEN_STRING);
			i = end;
			continue;
		}

		/* Quoted strings */
		if (in_set (syn->quotes, c))
		{
			guint open;

			i = lex_quoted (lx, i, &open);
			if (open != ST_NONE)
				return open;
			continue;
		}

		/* @decorator / @attribute.path */
		if (in_set (syn->attribute_prefix, c) && i + 1 < lx->len && is_word (syn, t[i + 1]) &&
		    !g_ascii_isdigit (t[i + 1]))
		{
			end = i + 1;
			while (end < lx->len && (is_word (syn, t[end]) || t[end] == '.'))
				end++;
			emit (lx, i, end, LRG_SYNTAX_TOKEN_ATTRIBUTE);
			i = end;
			continue;
		}

		/* $variable */
		if (in_set (syn->variable_prefix, c))
		{
			end = lex_variable (lx, i);
			if (end > i)
			{
				i = end;
				continue;
			}
		}

		/* Numbers (not inside an identifier) */
		if ((i == 0 || !is_word (syn, t[i - 1])) && (syn->number_regex != NULL ? !g_ascii_isalpha (c) : TRUE))
		{
			gsize n = scan_number (lx, i);

			if (n > 0)
			{
				emit (lx, i, i + n, LRG_SYNTAX_TOKEN_NUMBER);
				i += n;
				continue;
			}
		}

		/* Identifiers */
		if (is_word (syn, c))
		{
			LrgSyntaxToken tok;

			end = i;
			while (end < lx->len && is_word (syn, t[end]))
				end++;

			/* String prefixes: r"raw", b'bytes', f"{x}" */
			if (end - i <= 2 && end < lx->len && in_set (syn->quotes, t[end]))
			{
				gsize k2;
				gboolean prefix = TRUE;

				for (k2 = i; k2 < end; k2++)
					if (!in_set ("rRbBfFuUL8", t[k2]))
						prefix = FALSE;
				if (prefix && word_kind (lx, i, end) == LRG_SYNTAX_TOKEN_TEXT)
				{
					guint open;

					emit (lx, i, end, LRG_SYNTAX_TOKEN_STRING);
					/* a prefixed triple quote is a multi-line string */
					for (k = 0; k < syn->ml_strings->len; k++)
						if (starts (lx, end, g_array_index (syn->ml_strings, Pair, k).start))
							break;
					if (k < syn->ml_strings->len)
					{
						i = end;
						continue;
					}
					i = lex_quoted (lx, end, &open);
					if (open != ST_NONE)
						return open;
					continue;
				}
			}

			tok = word_kind (lx, i, end);
			if (tok == LRG_SYNTAX_TOKEN_TEXT)
			{
				if (syn->keys && key_colon (lx, end))
					tok = LRG_SYNTAX_TOKEN_ATTRIBUTE;
				else if (syn->strict)
					tok = LRG_SYNTAX_TOKEN_ERROR;
				else if (syn->caps_constants && is_caps (lx, i, end))
					tok = LRG_SYNTAX_TOKEN_CONSTANT;
				else if (syn->functions && followed_by (lx, end, '('))
					tok = LRG_SYNTAX_TOKEN_FUNCTION;
			}
			emit (lx, i, end, tok);
			i = end;
			continue;
		}

		if (in_set (OPERATOR_CHARS, c))
		{
			emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_OPERATOR);
			i++;
			continue;
		}
		if (in_set (PUNCTUATION_CHARS, c))
		{
			emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
			i++;
			continue;
		}
		end = next_char (lx, i);
		if (syn->strict)
			emit (lx, i, end, LRG_SYNTAX_TOKEN_ERROR);
		i = end;
	}
	return ST_NONE;
}

/* ==========================================================================
 * Markdown lexer
 * ========================================================================== */

/* run_length: how many @c in a row start at @i */
static gsize
run_length (Lex   *lx,
            gsize  i,
            gchar  c)
{
	gsize n = 0;

	while (i + n < lx->len && lx->text[i + n] == c)
		n++;
	return n;
}

/* find_run: next run of exactly @n @c at or after @i, or -1 */
static gssize
find_run (Lex   *lx,
          gsize  i,
          gchar  c,
          gsize  n)
{
	while (i < lx->len)
	{
		if (lx->text[i] == c)
		{
			gsize r = run_length (lx, i, c);

			if (r == n)
				return (gssize) i;
			i += r;
			continue;
		}
		i++;
	}
	return -1;
}

/*
 * lex_markdown_inline:
 *
 * Code spans (STRING), emphasis and strong runs (EMPHASIS, markers
 * included), links and images "[text](url)" and autolinks (LINK) and
 * backslash escapes (ESCAPE).
 */
static void
lex_markdown_inline (Lex   *lx,
                     gsize  i)
{
	const gchar *t = lx->text;

	while (i < lx->len)
	{
		gchar  c = t[i];
		gssize e;

		if (c == '\\' && i + 1 < lx->len && g_ascii_ispunct (t[i + 1]))
		{
			emit (lx, i, i + 2, LRG_SYNTAX_TOKEN_ESCAPE);
			i += 2;
			continue;
		}
		if (c == '`')
		{
			gsize n = run_length (lx, i, '`');

			e = find_run (lx, i + n, '`', n);
			if (e >= 0)
			{
				emit (lx, i, (gsize) e + n, LRG_SYNTAX_TOKEN_STRING);
				i = (gsize) e + n;
			}
			else
				i += n;
			continue;
		}
		if ((c == '*' || c == '_') && !(c == '_' && i > 0 && g_ascii_isalnum (t[i - 1])))
		{
			gsize n = MIN (run_length (lx, i, c), (gsize) 3);

			if (i + n < lx->len && !is_space (t[i + n]))
			{
				e = find_run (lx, i + n, c, n);
				if (e >= 0)
				{
					emit (lx, i, (gsize) e + n, LRG_SYNTAX_TOKEN_EMPHASIS);
					i = (gsize) e + n;
					continue;
				}
			}
			i += run_length (lx, i, c);
			continue;
		}
		if (c == '[' || (c == '!' && i + 1 < lx->len && t[i + 1] == '['))
		{
			gsize  open = c == '!' ? i + 1 : i;
			gssize close = find (lx, open, "](");

			if (close >= 0)
			{
				gssize paren = find (lx, (gsize) close + 2, ")");

				if (paren >= 0)
				{
					emit (lx, i, (gsize) paren + 1, LRG_SYNTAX_TOKEN_LINK);
					i = (gsize) paren + 1;
					continue;
				}
			}
			i = open + 1;
			continue;
		}
		if (c == '<' && (starts (lx, i + 1, "http://") || starts (lx, i + 1, "https://")))
		{
			e = find (lx, i, ">");
			if (e >= 0)
			{
				emit (lx, i, (gsize) e + 1, LRG_SYNTAX_TOKEN_LINK);
				i = (gsize) e + 1;
				continue;
			}
		}
		if ((c == 'h') && (starts (lx, i, "http://") || starts (lx, i, "https://")) &&
		    (i == 0 || is_space (t[i - 1])))
		{
			gsize end = i;

			while (end < lx->len && !is_space (t[end]))
				end++;
			emit (lx, i, end, LRG_SYNTAX_TOKEN_LINK);
			i = end;
			continue;
		}
		i = next_char (lx, i);
	}
}

/* is_rule: a thematic break ("---", "* * *") or setext underline */
static gboolean
is_rule (Lex   *lx,
         gsize  i,
         gchar *which)
{
	gchar c = lx->text[i];
	gsize n = 0;

	if (c != '-' && c != '*' && c != '_' && c != '=')
		return FALSE;
	for (; i < lx->len; i++)
	{
		if (lx->text[i] == c)
			n++;
		else if (!is_space (lx->text[i]))
			return FALSE;
	}
	*which = c;
	return n >= 3;
}

/*
 * lex_markdown:
 *
 * Block structure first: fenced code (carried in the state, its lines
 * are STRING), ATX headings, rules, block quotes and list markers;
 * then the inline lexer on the rest of the line.
 */
static guint
lex_markdown (Lex   *lx,
              guint  state)
{
	const gchar *t = lx->text;
	gsize        i = skip_spaces (lx, 0);
	gchar        rule;

	/* Inside fenced code: until a fence as long as the opener */
	if (ST_KIND (state) == ST_FENCE)
	{
		gchar fch = (gchar) (ST_ARG (state) >> 8);
		gsize fcount = ST_ARG (state) & 0xff;

		if (i < lx->len && t[i] == fch && run_length (lx, i, fch) >= fcount &&
		    skip_spaces (lx, i + run_length (lx, i, fch)) == lx->len)
		{
			emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_PREPROCESSOR);
			return ST_NONE;
		}
		emit (lx, 0, lx->len, LRG_SYNTAX_TOKEN_STRING);
		return state;
	}

	if (i >= lx->len)
		return ST_NONE;

	/* Fence opener: ``` or ~~~ and an info string (the language) */
	if ((t[i] == '`' || t[i] == '~') && run_length (lx, i, t[i]) >= 3 && i < 4)
	{
		gchar fch = t[i];
		gsize n = run_length (lx, i, fch);

		emit (lx, i, i + n, LRG_SYNTAX_TOKEN_PREPROCESSOR);
		emit (lx, skip_spaces (lx, i + n), lx->len, LRG_SYNTAX_TOKEN_TYPE);
		return ST_MAKE (ST_FENCE, ((guint) (guchar) fch << 8) | (guint) MIN (n, (gsize) 0xff));
	}

	/* # Heading */
	if (t[i] == '#')
	{
		gsize n = run_length (lx, i, '#');

		if (n <= 6 && (i + n == lx->len || is_space (t[i + n])))
		{
			emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_HEADING);
			return ST_NONE;
		}
	}

	/* Rules and setext underlines */
	if (is_rule (lx, i, &rule))
	{
		emit (lx, i, lx->len, rule == '=' ? LRG_SYNTAX_TOKEN_HEADING : LRG_SYNTAX_TOKEN_PUNCTUATION);
		return ST_NONE;
	}

	/* > quote markers */
	while (i < lx->len && t[i] == '>')
	{
		emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
		i = skip_spaces (lx, i + 1);
	}

	/* List markers: "- ", "* ", "+ ", "1. ", "2) " */
	if (i < lx->len && (t[i] == '-' || t[i] == '*' || t[i] == '+') &&
	    i + 1 < lx->len && is_space (t[i + 1]))
	{
		emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
		i += 2;
	}
	else if (i < lx->len && g_ascii_isdigit (t[i]))
	{
		gsize end = i;

		while (end < lx->len && g_ascii_isdigit (t[end]))
			end++;
		if (end < lx->len && (t[end] == '.' || t[end] == ')') &&
		    (end + 1 == lx->len || is_space (t[end + 1])))
		{
			emit (lx, i, end + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
			i = end + 1;
		}
	}

	lex_markdown_inline (lx, i);
	return ST_NONE;
}

/* ==========================================================================
 * YAML lexer
 * ========================================================================== */

/* yaml_plain_end: end of a plain scalar starting at @i */
static gsize
yaml_plain_end (Lex      *lx,
                gsize     i,
                gboolean  flow)
{
	const gchar *t = lx->text;

	while (i < lx->len)
	{
		gchar c = t[i];

		if (c == ':' && (i + 1 == lx->len || is_space (t[i + 1]) || (flow && in_set (",]}", t[i + 1]))))
			break;
		if (c == '#' && i > 0 && is_space (t[i - 1]))
			break;
		if (flow && in_set (",[]{}", c))
			break;
		i++;
	}
	/* trailing spaces are not part of the scalar */
	while (i > 0 && is_space (t[i - 1]))
		i--;
	return i;
}

/* yaml_scalar_kind: constants, numbers, else a plain string */
static LrgSyntaxToken
yaml_scalar_kind (Lex   *lx,
                  gsize  i,
                  gsize  end)
{
	LrgSyntaxToken tok = word_kind (lx, i, end);

	if (tok != LRG_SYNTAX_TOKEN_TEXT)
		return tok;
	if (end - i == 1 && lx->text[i] == '~')
		return LRG_SYNTAX_TOKEN_CONSTANT;
	if (scan_number (lx, (lx->text[i] == '-' || lx->text[i] == '+') ? i + 1 : i) ==
	    end - ((lx->text[i] == '-' || lx->text[i] == '+') ? i + 1 : i))
		return LRG_SYNTAX_TOKEN_NUMBER;
	return LRG_SYNTAX_TOKEN_STRING;
}

/*
 * lex_yaml:
 *
 * Indentation (tabs are errors), document markers and directives,
 * sequence dashes, keys (plain or quoted scalars before ": "), values
 * (quoted strings, anchors and aliases, tags, constants, numbers,
 * plain strings), flow collections and comments. A "|" or ">" value
 * starts a block scalar: following lines indented deeper than this
 * line are STRING, carried in the state.
 */
static guint
lex_yaml (Lex   *lx,
          guint  state)
{
	const gchar *t = lx->text;
	gsize        indent = 0;
	gsize        i;
	gint         flow = 0;

	while (indent < lx->len && (t[indent] == ' ' || t[indent] == '\t'))
		indent++;

	/* Block scalar continuation: deeper lines (and blank ones) */
	if (ST_KIND (state) == ST_YAML_BLOCK)
	{
		if (indent == lx->len)
			return state;
		if (indent > ST_ARG (state))
		{
			emit (lx, indent, lx->len, LRG_SYNTAX_TOKEN_STRING);
			return state;
		}
	}

	/* Tabs are not allowed in YAML indentation */
	for (i = 0; i < indent; i++)
		if (t[i] == '\t')
			emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_ERROR);

	i = indent;
	if (i >= lx->len)
		return ST_NONE;

	if (indent == 0 && (starts (lx, 0, "---") || starts (lx, 0, "...")) &&
	    (lx->len == 3 || is_space (t[3])))
	{
		emit (lx, 0, 3, LRG_SYNTAX_TOKEN_PREPROCESSOR);
		i = skip_spaces (lx, 3);
	}
	else if (indent == 0 && t[0] == '%')
	{
		emit (lx, 0, lx->len, LRG_SYNTAX_TOKEN_PREPROCESSOR);
		return ST_NONE;
	}

	/* "- " sequence entries, possibly nested on one line */
	while (i < lx->len && t[i] == '-' && (i + 1 == lx->len || is_space (t[i + 1])))
	{
		emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
		i = skip_spaces (lx, i + 1);
	}

	while (i < lx->len)
	{
		gchar c = t[i];
		gsize end;

		if (is_space (c))
		{
			i++;
			continue;
		}
		if (c == '#' && (i == 0 || is_space (t[i - 1])))
		{
			emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_COMMENT);
			break;
		}
		if (in_set ("[{", c))
		{
			flow++;
			emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
			i++;
			continue;
		}
		if (in_set ("]},", c))
		{
			if (c != ',')
				flow = MAX (0, flow - 1);
			emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
			i++;
			continue;
		}
		if (c == ':' && (i + 1 == lx->len || is_space (t[i + 1])))
		{
			emit (lx, i, i + 1, LRG_SYNTAX_TOKEN_PUNCTUATION);
			i++;
			continue;
		}
		/* Block scalar indicator: | |- >+ |2 */
		if ((c == '|' || c == '>') && flow == 0)
		{
			end = i + 1;
			while (end < lx->len && in_set ("+-0123456789", t[end]))
				end++;
			if (skip_spaces (lx, end) == lx->len || t[skip_spaces (lx, end)] == '#')
			{
				emit (lx, i, end, LRG_SYNTAX_TOKEN_OPERATOR);
				end = skip_spaces (lx, end);
				if (end < lx->len)
					emit (lx, end, lx->len, LRG_SYNTAX_TOKEN_COMMENT);
				return ST_MAKE (ST_YAML_BLOCK, MIN (indent, (gsize) 0xffff));
			}
		}
		/* &anchor, *alias */
		if ((c == '&' || c == '*') && i + 1 < lx->len && !is_space (t[i + 1]))
		{
			end = i + 1;
			while (end < lx->len && !is_space (t[end]) && !in_set (",[]{}", t[end]))
				end++;
			emit (lx, i, end, LRG_SYNTAX_TOKEN_VARIABLE);
			i = end;
			continue;
		}
		/* !tag, !!str */
		if (c == '!')
		{
			end = i;
			while (end < lx->len && !is_space (t[end]))
				end++;
			emit (lx, i, end, LRG_SYNTAX_TOKEN_TYPE);
			i = end;
			continue;
		}
		/* Quoted scalars; a key when followed by ':' */
		if (c == '"' || c == '\'')
		{
			guint    first = n_spans (lx);
			gboolean closed;
			gboolean continued;

			/* single quotes escape by doubling, never with '\' */
			end = lex_string_body (lx, i + 1, i, c, c == '\'' ? '\0' : '\\', &closed, &continued);
			if (closed && key_colon (lx, end))
				retag (lx, first, LRG_SYNTAX_TOKEN_ATTRIBUTE);
			i = end;
			continue;
		}
		/* Plain scalar: key or value */
		end = yaml_plain_end (lx, i, flow > 0);
		if (end <= i)
		{
			i = next_char (lx, i);
			continue;
		}
		if (end < lx->len && key_colon (lx, end))
			emit (lx, i, end, LRG_SYNTAX_TOKEN_ATTRIBUTE);
		else
			emit (lx, i, end, yaml_scalar_kind (lx, i, end));
		i = end;
	}
	return ST_NONE;
}

/* ==========================================================================
 * INI lexer
 * ========================================================================== */

/*
 * lex_ini:
 *
 * Comment lines, [section] / [[table]] headers (HEADING) and
 * "key = value" lines: the key is an ATTRIBUTE, the separator an
 * OPERATOR and the value is lexed as code (strings, numbers, word
 * table constants, inline comments after whitespace). A multi-line
 * value string (TOML """) continues through the code lexer.
 */
static guint
lex_ini (Lex   *lx,
         guint  state)
{
	const gchar *t = lx->text;
	gsize        i;
	gsize        end;
	guint        k;

	if (state != ST_NONE)
		return lex_code (lx, 0, state);

	i = skip_spaces (lx, 0);
	if (i >= lx->len)
		return ST_NONE;

	for (k = 0; k < lx->syn->line_comments->len; k++)
		if (starts (lx, i, g_ptr_array_index (lx->syn->line_comments, k)))
		{
			emit (lx, i, lx->len, LRG_SYNTAX_TOKEN_COMMENT);
			return ST_NONE;
		}

	if (t[i] == '[')
	{
		end = i;
		while (end < lx->len && t[end] != ']')
			end++;
		while (end < lx->len && t[end] == ']')
			end++;
		emit (lx, i, end, LRG_SYNTAX_TOKEN_HEADING);
		return lex_code (lx, end, ST_NONE);
	}

	/* key = value (also "key: value") */
	end = i;
	while (end < lx->len && t[end] != '=' && t[end] != ':')
		end++;
	if (end < lx->len)
	{
		gsize key_end = end;

		while (key_end > i && is_space (t[key_end - 1]))
			key_end--;
		emit (lx, i, key_end, LRG_SYNTAX_TOKEN_ATTRIBUTE);
		emit (lx, end, end + 1, LRG_SYNTAX_TOKEN_OPERATOR);
		return lex_code (lx, end + 1, ST_NONE);
	}
	return lex_code (lx, i, ST_NONE);
}

/* ==========================================================================
 * Tokenizing
 * ========================================================================== */

guint
lrg_syntax_tokenize_line (LrgSyntax   *self,
                          const gchar *line,
                          gssize       length,
                          guint        state,
                          GArray      *spans)
{
	Lex lx;

	g_return_val_if_fail (LRG_IS_SYNTAX (self), 0);
	g_return_val_if_fail (line != NULL || length == 0, 0);
	g_return_val_if_fail (spans == NULL ||
	                      g_array_get_element_size (spans) == sizeof (LrgSyntaxSpan), 0);

	lx.syn = self;
	lx.text = line != NULL ? line : "";
	lx.len = length < 0 ? strlen (lx.text) : (gsize) length;
	lx.spans = spans;

	switch (self->base)
	{
	case LRG_SYNTAX_BASE_MARKDOWN:
		return lex_markdown (&lx, state);
	case LRG_SYNTAX_BASE_YAML:
		return lex_yaml (&lx, state);
	case LRG_SYNTAX_BASE_INI:
		return lex_ini (&lx, state);
	case LRG_SYNTAX_BASE_CODE:
	default:
		return lex_code (&lx, 0, state);
	}
}

/* ==========================================================================
 * Colours
 * ========================================================================== */

/* Contrast the derived colours must reach against the INPUT background */
#define MIN_CONTRAST (3.2f)

/*
 * derive_color:
 *
 * The theme-role recipe of each token. Presets only guarantee a few
 * hue families (accent, link, success, warning, danger), so the less
 * common kinds are mixes of two roles: types are teal (success + link),
 * constants orange (warning + danger), preprocessor lines rose
 * (danger + accent) and so on. The result is then pulled towards TEXT
 * until it reaches MIN_CONTRAST on the editor background, which keeps
 * dark accents legible on dark presets and pale ones on light presets.
 */
static void
derive_color (LrgTheme       *theme,
              LrgSyntaxToken  token,
              GrlColor       *out)
{
	const GrlColor *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor *link = lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK);
	const GrlColor *ok = lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS);
	const GrlColor *warn = lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING);
	const GrlColor *bad = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
	const GrlColor *bg = lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT);
	guint           pass;

	switch (token)
	{
	case LRG_SYNTAX_TOKEN_KEYWORD:
		lrg_paint_color_mix (accent, text, 0.12f, out);
		break;
	case LRG_SYNTAX_TOKEN_TYPE:
		lrg_paint_color_mix (ok, link, 0.5f, out);
		break;
	case LRG_SYNTAX_TOKEN_FUNCTION:
		lrg_paint_color_mix (link, text, 0.25f, out);
		break;
	case LRG_SYNTAX_TOKEN_STRING:
		*out = *ok;
		break;
	case LRG_SYNTAX_TOKEN_NUMBER:
		*out = *warn;
		break;
	case LRG_SYNTAX_TOKEN_COMMENT:
		*out = *muted;
		break;
	case LRG_SYNTAX_TOKEN_PREPROCESSOR:
		lrg_paint_color_mix (bad, accent, 0.35f, out);
		break;
	case LRG_SYNTAX_TOKEN_OPERATOR:
		lrg_paint_color_mix (text, accent, 0.4f, out);
		break;
	case LRG_SYNTAX_TOKEN_PUNCTUATION:
		lrg_paint_color_mix (text, muted, 0.5f, out);
		break;
	case LRG_SYNTAX_TOKEN_CONSTANT:
		lrg_paint_color_mix (warn, bad, 0.4f, out);
		break;
	case LRG_SYNTAX_TOKEN_ATTRIBUTE:
		lrg_paint_color_mix (link, bad, 0.3f, out);
		break;
	case LRG_SYNTAX_TOKEN_ESCAPE:
		lrg_paint_color_mix (warn, ok, 0.3f, out);
		break;
	case LRG_SYNTAX_TOKEN_VARIABLE:
		lrg_paint_color_mix (bad, warn, 0.5f, out);
		break;
	case LRG_SYNTAX_TOKEN_HEADING:
		*out = *accent;
		break;
	case LRG_SYNTAX_TOKEN_EMPHASIS:
		lrg_paint_color_mix (warn, text, 0.3f, out);
		break;
	case LRG_SYNTAX_TOKEN_LINK:
		*out = *link;
		break;
	case LRG_SYNTAX_TOKEN_ERROR:
		*out = *bad;
		break;
	case LRG_SYNTAX_TOKEN_TEXT:
	default:
		*out = *text;
		return;
	}

	out->a = 255;
	for (pass = 0; pass < 6 && lrg_paint_color_contrast (out, bg) < MIN_CONTRAST; pass++)
		lrg_paint_color_mix (out, text, 0.35f, out);
}

void
lrg_syntax_get_token_color (LrgSyntax      *self,
                            LrgTheme       *theme,
                            LrgSyntaxToken  token,
                            GrlColor       *out)
{
	g_return_if_fail (self == NULL || LRG_IS_SYNTAX (self));
	g_return_if_fail (LRG_IS_THEME (theme));
	g_return_if_fail (out != NULL);

	if ((guint) token >= LRG_SYNTAX_N_TOKENS)
		token = LRG_SYNTAX_TOKEN_TEXT;
	if (self != NULL && (self->colors_set & (1u << token)))
	{
		*out = self->colors[token];
		return;
	}
	derive_color (theme, token, out);
}

void
lrg_syntax_set_token_color (LrgSyntax      *self,
                            LrgSyntaxToken  token,
                            const GrlColor *color)
{
	g_return_if_fail (LRG_IS_SYNTAX (self));
	g_return_if_fail ((guint) token < LRG_SYNTAX_N_TOKENS);

	if (color != NULL)
	{
		self->colors[token] = *color;
		self->colors_set |= 1u << token;
	}
	else
		self->colors_set &= ~(1u << token);
}

/* ==========================================================================
 * YAML definitions
 * ========================================================================== */

static gboolean
invalid (GError      **error,
         const gchar  *what)
{
	g_set_error (error, LRG_SYNTAX_ERROR, LRG_SYNTAX_ERROR_INVALID, "language definition: %s", what);
	return FALSE;
}

/* yaml_string: a scalar member as a string, or NULL; error when not a scalar */
static gboolean
yaml_string (YamlMapping  *map,
             const gchar  *key,
             const gchar **out,
             GError      **error)
{
	YamlNode *node = yaml_mapping_get_member (map, key);

	*out = NULL;
	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return TRUE;
	if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
	{
		g_set_error (error, LRG_SYNTAX_ERROR, LRG_SYNTAX_ERROR_INVALID,
		             "language definition: '%s' must be a string", key);
		return FALSE;
	}
	*out = yaml_node_get_string (node);
	return TRUE;
}

/* yaml_bool: a boolean member, @fallback when absent */
static gboolean
yaml_bool (YamlMapping *map,
           const gchar *key,
           gboolean     fallback)
{
	YamlNode    *node = yaml_mapping_get_member (map, key);
	const gchar *s;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return fallback;
	s = yaml_node_get_string (node);
	if (s == NULL)
		return fallback;
	return g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0 ||
	       g_ascii_strcasecmp (s, "on") == 0 || g_strcmp0 (s, "1") == 0;
}

/*
 * yaml_strings:
 *
 * A member that is a string or a sequence of strings, as a NULL
 * terminated vector (empty when absent).
 */
static gboolean
yaml_strings (YamlMapping  *map,
              const gchar  *key,
              GStrv        *out,
              GError      **error)
{
	YamlNode          *node = yaml_mapping_get_member (map, key);
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();

	*out = NULL;
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
		g_strv_builder_add (b, yaml_node_get_string (node));
	else if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);
		guint         i;

		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlNode *el = yaml_sequence_get_element (seq, i);

			if (yaml_node_get_node_type (el) != YAML_NODE_SCALAR)
			{
				g_set_error (error, LRG_SYNTAX_ERROR, LRG_SYNTAX_ERROR_INVALID,
				             "language definition: '%s' must list strings", key);
				return FALSE;
			}
			g_strv_builder_add (b, yaml_node_get_string (el));
		}
	}
	else if (node != NULL && yaml_node_get_node_type (node) != YAML_NODE_NULL)
	{
		g_set_error (error, LRG_SYNTAX_ERROR, LRG_SYNTAX_ERROR_INVALID,
		             "language definition: '%s' must be a string or a list", key);
		return FALSE;
	}
	*out = g_strv_builder_end (b);
	return TRUE;
}

/* Word-list keys and the kinds they map to */
static const struct
{
	const gchar    *key;
	LrgSyntaxToken  token;
} word_lists[] = {
	{ "keywords", LRG_SYNTAX_TOKEN_KEYWORD },
	{ "types", LRG_SYNTAX_TOKEN_TYPE },
	{ "constants", LRG_SYNTAX_TOKEN_CONSTANT },
	{ "functions", LRG_SYNTAX_TOKEN_FUNCTION },
	{ "builtins", LRG_SYNTAX_TOKEN_FUNCTION },
	{ "variables", LRG_SYNTAX_TOKEN_VARIABLE },
	{ "attributes", LRG_SYNTAX_TOKEN_ATTRIBUTE },
	{ "operators", LRG_SYNTAX_TOKEN_OPERATOR }
};

/*
 * apply_yaml:
 *
 * Fills @self from the definition mapping (see syntax.org): base,
 * options, comments, strings, prefixes, numbers, word lists, patterns,
 * aliases and colour overrides. Stops at the first malformed member.
 */
static gboolean
apply_yaml (LrgSyntax    *self,
            YamlMapping  *map,
            GError      **error)
{
	const gchar *s;
	const gchar *s2;
	g_auto(GStrv) list = NULL;
	YamlNode    *node;
	guint        i;

	/* Options first: they decide how the word table is keyed */
	lrg_syntax_set_options (self,
	                        yaml_bool (map, "case-sensitive", TRUE),
	                        yaml_bool (map, "function-calls", TRUE),
	                        yaml_bool (map, "caps-constants", FALSE),
	                        yaml_bool (map, "keys", FALSE));
	lrg_syntax_set_strict (self, yaml_bool (map, "strict", FALSE));
	lrg_syntax_set_comment_needs_space (self, yaml_bool (map, "comment-needs-space", FALSE));

	/* Comments */
	if (!yaml_strings (map, "line-comment", &list, error))
		return FALSE;
	for (i = 0; list[i] != NULL; i++)
		lrg_syntax_add_line_comment (self, list[i]);
	g_clear_pointer (&list, g_strfreev);

	if (!yaml_strings (map, "block-comment", &list, error))
		return FALSE;
	if (g_strv_length (list) == 1 || g_strv_length (list) > 2)
		return invalid (error, "'block-comment' must be [start, end]");
	if (list[0] != NULL)
		lrg_syntax_set_block_comment (self, list[0], list[1], yaml_bool (map, "nested-comments", FALSE));
	g_clear_pointer (&list, g_strfreev);

	/* Strings */
	if (!yaml_string (map, "strings", &s, error) || !yaml_string (map, "escape", &s2, error))
		return FALSE;
	if (s2 != NULL && strlen (s2) > 1)
		return invalid (error, "'escape' must be one character");
	if (s != NULL || s2 != NULL || yaml_mapping_has_member (map, "strings-span-lines"))
		lrg_syntax_set_string_quotes (self, s != NULL ? s : self->quotes,
		                              s2 != NULL ? (gunichar) (guchar) s2[0] : self->escape,
		                              yaml_bool (map, "strings-span-lines", FALSE));
	if (!yaml_string (map, "raw-strings", &s, error))
		return FALSE;
	lrg_syntax_set_raw_quotes (self, s);

	node = yaml_mapping_get_member (map, "multiline-strings");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);

		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlNode     *el = yaml_sequence_get_element (seq, i);
			YamlSequence *pair;

			if (yaml_node_get_node_type (el) == YAML_NODE_SCALAR)
			{
				/* same delimiter both ways: """ */
				s = yaml_node_get_string (el);
				if (s == NULL || s[0] == '\0')
					return invalid (error, "empty 'multiline-strings' delimiter");
				lrg_syntax_add_multiline_string (self, s, s);
				continue;
			}
			if (yaml_node_get_node_type (el) != YAML_NODE_SEQUENCE ||
			    yaml_sequence_get_length ((pair = yaml_node_get_sequence (el))) != 2)
				return invalid (error, "'multiline-strings' entries must be a string or [start, end]");
			s = yaml_sequence_get_string_element (pair, 0);
			s2 = yaml_sequence_get_string_element (pair, 1);
			if (s == NULL || s[0] == '\0' || s2 == NULL || s2[0] == '\0')
				return invalid (error, "empty 'multiline-strings' delimiter");
			lrg_syntax_add_multiline_string (self, s, s2);
		}
	}
	else if (node != NULL && yaml_node_get_node_type (node) != YAML_NODE_NULL)
		return invalid (error, "'multiline-strings' must be a list");

	/* Prefixes, identifiers, numbers */
	if (!yaml_string (map, "preprocessor", &s, error))
		return FALSE;
	lrg_syntax_set_preprocessor (self, s);
	if (!yaml_string (map, "variable-prefix", &s, error) || !yaml_string (map, "attribute-prefix", &s2, error))
		return FALSE;
	lrg_syntax_set_prefixes (self, s, s2);
	if (!yaml_string (map, "word-chars", &s, error))
		return FALSE;
	lrg_syntax_set_word_chars (self, s);
	if (!yaml_string (map, "number-pattern", &s, error))
		return FALSE;
	if (s != NULL && !lrg_syntax_set_number_pattern (self, s, error))
		return FALSE;

	/* Word lists */
	for (i = 0; i < G_N_ELEMENTS (word_lists); i++)
	{
		if (!yaml_strings (map, word_lists[i].key, &list, error))
			return FALSE;
		lrg_syntax_add_words (self, word_lists[i].token, (const gchar * const *) list);
		g_clear_pointer (&list, g_strfreev);
	}

	/* Lookup */
	if (!yaml_strings (map, "extensions", &list, error))
		return FALSE;
	for (i = 0; list[i] != NULL; i++)
		lrg_syntax_add_pattern (self, list[i]);
	g_clear_pointer (&list, g_strfreev);
	if (!yaml_strings (map, "aliases", &list, error))
		return FALSE;
	for (i = 0; list[i] != NULL; i++)
		lrg_syntax_add_alias (self, list[i]);
	g_clear_pointer (&list, g_strfreev);

	/* Colours: { keyword: "#ff79c6", ... } */
	node = yaml_mapping_get_member (map, "colors");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		YamlMapping *colors = yaml_node_get_mapping (node);
		GEnumClass  *klass = g_type_class_ref (LRG_TYPE_SYNTAX_TOKEN);

		for (i = 0; i < yaml_mapping_get_size (colors); i++)
		{
			const gchar *key = yaml_mapping_get_key (colors, i);
			YamlNode    *value = yaml_mapping_get_value (colors, i);
			GEnumValue  *ev = g_enum_get_value_by_nick (klass, key);
			GrlColor     c;

			if (ev == NULL || yaml_node_get_node_type (value) != YAML_NODE_SCALAR ||
			    !lrg_color_parse (yaml_node_get_string (value), &c))
			{
				g_type_class_unref (klass);
				g_set_error (error, LRG_SYNTAX_ERROR, LRG_SYNTAX_ERROR_INVALID,
				             "language definition: bad colour entry '%s'", key);
				return FALSE;
			}
			lrg_syntax_set_token_color (self, (LrgSyntaxToken) ev->value, &c);
		}
		g_type_class_unref (klass);
	}
	else if (node != NULL && yaml_node_get_node_type (node) != YAML_NODE_NULL)
		return invalid (error, "'colors' must be a mapping");

	return TRUE;
}

LrgSyntax *
lrg_syntax_new_from_yaml (const gchar  *data,
                          gssize        length,
                          GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(LrgSyntax)  self = NULL;
	YamlNode             *root;
	YamlMapping          *map;
	const gchar          *name;
	const gchar          *base_name;
	LrgSyntaxBase         base = LRG_SYNTAX_BASE_CODE;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, error))
		return NULL;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		invalid (error, "the document must be a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (root);

	if (!yaml_string (map, "name", &name, error))
		return NULL;
	if (name == NULL || name[0] == '\0')
	{
		invalid (error, "'name' is required");
		return NULL;
	}

	if (!yaml_string (map, "base", &base_name, error))
		return NULL;
	if (base_name != NULL)
	{
		GEnumClass *klass = g_type_class_ref (LRG_TYPE_SYNTAX_BASE);
		GEnumValue *ev = g_enum_get_value_by_nick (klass, base_name);

		if (ev != NULL)
			base = (LrgSyntaxBase) ev->value;
		g_type_class_unref (klass);
		if (ev == NULL)
		{
			g_set_error (error, LRG_SYNTAX_ERROR, LRG_SYNTAX_ERROR_INVALID,
			             "language definition: unknown base '%s'", base_name);
			return NULL;
		}
	}

	self = lrg_syntax_new (name, base);
	if (!apply_yaml (self, map, error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgSyntax *
lrg_syntax_new_from_file (const gchar  *path,
                          GError      **error)
{
	g_autofree gchar *contents = NULL;
	gsize             len = 0;

	g_return_val_if_fail (path != NULL, NULL);

	if (!g_file_get_contents (path, &contents, &len, error))
		return NULL;
	return lrg_syntax_new_from_yaml (contents, (gssize) len, error);
}

/* ==========================================================================
 * Built-in languages
 * ========================================================================== */

static const gchar * const c_keywords[] = {
	"auto", "break", "case", "const", "continue", "default", "do", "else", "enum", "extern",
	"for", "goto", "if", "inline", "register", "restrict", "return", "sizeof", "static",
	"struct", "switch", "typedef", "union", "volatile", "while", "_Alignas", "_Alignof",
	"_Atomic", "_Generic", "_Noreturn", "_Static_assert", "_Thread_local", "__attribute__",
	"typeof", "__typeof__", "asm", "__asm__", NULL
};

static const gchar * const c_types[] = {
	"void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool",
	"_Bool", "_Complex", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "off_t",
	"int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
	"FILE", "va_list", "wchar_t",
	/* GLib */
	"gboolean", "gchar", "guchar", "gint", "guint", "gshort", "gushort", "glong", "gulong",
	"gint8", "guint8", "gint16", "guint16", "gint32", "guint32", "gint64", "guint64",
	"gfloat", "gdouble", "gsize", "gssize", "goffset", "gintptr", "guintptr", "gpointer",
	"gconstpointer", "gunichar", "gunichar2", "GType", "GQuark", "GObject", "GObjectClass",
	"GError", "GList", "GSList", "GHashTable", "GArray", "GPtrArray", "GByteArray", "GString",
	"GBytes", "GVariant", "GValue", "GParamSpec", "GMainLoop", "GMainContext", "GSource",
	"GCancellable", "GTask", "GFile", "GInputStream", "GOutputStream", "GRegex", "GMatchInfo",
	"GDateTime", "GTimer", "GMutex", "GRecMutex", "GCond", "GThread", "GStrv", "GStrvBuilder",
	"GListModel", "GListStore", "GAsyncResult", "GSettings", "GKeyFile", "GQueue", "GTree",
	"GSequence", "GEnumClass", "GFlagsClass", "GTypeInterface", "GClosure", "GCallback",
	"GDestroyNotify", "GFunc", "GCompareFunc", "GHashFunc", "GEqualFunc", "GSourceFunc", NULL
};

static const gchar * const c_constants[] = {
	"NULL", "TRUE", "FALSE", "true", "false", "nullptr", NULL
};

static const gchar * const glsl_keywords[] = {
	"if", "else", "for", "while", "do", "return", "break", "continue", "discard", "switch",
	"case", "default", "uniform", "in", "out", "inout", "attribute", "varying", "const",
	"layout", "precision", "highp", "mediump", "lowp", "struct", "flat", "smooth",
	"noperspective", "centroid", "invariant", "buffer", "shared", "coherent", "readonly",
	"writeonly", NULL
};

static const gchar * const glsl_types[] = {
	"void", "bool", "int", "uint", "float", "double", "vec2", "vec3", "vec4", "ivec2",
	"ivec3", "ivec4", "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4", "dvec2",
	"dvec3", "dvec4", "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4", "mat3x2",
	"mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4", "sampler1D", "sampler2D", "sampler3D",
	"samplerCube", "sampler2DArray", "sampler2DShadow", "isampler2D", "usampler2D",
	"image2D", NULL
};

static const gchar * const glsl_functions[] = {
	"texture", "texture2D", "textureLod", "texelFetch", "textureSize", "mix", "clamp",
	"smoothstep", "step", "dot", "cross", "normalize", "length", "distance", "reflect",
	"refract", "sin", "cos", "tan", "asin", "acos", "atan", "pow", "exp", "exp2", "log",
	"log2", "sqrt", "inversesqrt", "abs", "sign", "floor", "ceil", "fract", "mod", "min",
	"max", "dFdx", "dFdy", "fwidth", "transpose", "inverse", "radians", "degrees", NULL
};

static const gchar * const glsl_variables[] = {
	"gl_Position", "gl_FragCoord", "gl_FragColor", "gl_FragDepth", "gl_PointSize",
	"gl_VertexID", "gl_InstanceID", "gl_FrontFacing", "gl_PointCoord", NULL
};

static const gchar * const lua_keywords[] = {
	"and", "break", "do", "else", "elseif", "end", "for", "function", "goto", "if", "in",
	"local", "not", "or", "repeat", "return", "then", "until", "while", NULL
};

static const gchar * const lua_constants[] = { "nil", "true", "false", NULL };

static const gchar * const lua_functions[] = {
	"print", "pairs", "ipairs", "next", "type", "tostring", "tonumber", "require", "pcall",
	"xpcall", "error", "assert", "select", "setmetatable", "getmetatable", "rawget", "rawset",
	"rawequal", "rawlen", "unpack", "load", "dofile", "collectgarbage", NULL
};

static const gchar * const lua_variables[] = {
	"self", "_G", "_ENV", "string", "table", "math", "os", "io", "coroutine", "utf8", "debug",
	NULL
};

static const gchar * const py_keywords[] = {
	"and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
	"elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
	"lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with",
	"yield", "match", "case", NULL
};

static const gchar * const py_constants[] = {
	"True", "False", "None", "NotImplemented", "Ellipsis", "__name__", "__file__", NULL
};

static const gchar * const py_types[] = {
	"int", "float", "str", "bytes", "bool", "list", "dict", "set", "frozenset", "tuple",
	"object", "complex", "bytearray", "type", "Exception", "ValueError", "TypeError",
	"KeyError", "IndexError", "RuntimeError", "OSError", NULL
};

static const gchar * const py_functions[] = {
	"print", "len", "range", "enumerate", "zip", "map", "filter", "sorted", "reversed",
	"open", "isinstance", "issubclass", "getattr", "setattr", "hasattr", "super", "min",
	"max", "sum", "abs", "any", "all", "repr", "iter", "next", "input", "round", "format",
	NULL
};

static const gchar * const py_variables[] = { "self", "cls", NULL };

static const gchar * const sh_keywords[] = {
	"if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac",
	"in", "function", "select", "return", "break", "continue", "local", "export", "declare",
	"readonly", "typeset", "time", NULL
};

static const gchar * const sh_functions[] = {
	"echo", "printf", "cd", "pwd", "read", "set", "unset", "shift", "exit", "test", "source",
	"trap", "eval", "exec", "shopt", "alias", "unalias", "type", "command", "builtin",
	"getopts", "wait", "kill", "jobs", "mapfile", "readarray", "let", NULL
};

static const gchar * const sh_constants[] = { "true", "false", NULL };

static const gchar * const yaml_constants[] = {
	"true", "false", "yes", "no", "on", "off", "null", ".inf", ".nan", NULL
};

static const gchar * const json_constants[] = { "true", "false", "null", NULL };

static const gchar * const ini_constants[] = {
	"true", "false", "yes", "no", "on", "off", "none", "null", NULL
};

/* add_patterns / add_aliases: NULL-terminated varargs helpers */
static void
add_patterns (LrgSyntax *syn,
              ...)
{
	va_list      ap;
	const gchar *p;

	va_start (ap, syn);
	while ((p = va_arg (ap, const gchar *)) != NULL)
		lrg_syntax_add_pattern (syn, p);
	va_end (ap);
}

static void
add_aliases (LrgSyntax *syn,
             ...)
{
	va_list      ap;
	const gchar *p;

	va_start (ap, syn);
	while ((p = va_arg (ap, const gchar *)) != NULL)
		lrg_syntax_add_alias (syn, p);
	va_end (ap);
}

/* c_like: comments, strings and preprocessor shared by C and GLSL */
static LrgSyntax *
c_like (const gchar *name)
{
	LrgSyntax *syn = lrg_syntax_new (name, LRG_SYNTAX_BASE_CODE);

	lrg_syntax_add_line_comment (syn, "//");
	lrg_syntax_set_block_comment (syn, "/*", "*/", FALSE);
	lrg_syntax_set_preprocessor (syn, "#");
	lrg_syntax_set_options (syn, TRUE, TRUE, TRUE, FALSE);
	return syn;
}

static LrgSyntax *
build_c (void)
{
	LrgSyntax *syn = c_like ("c");

	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_KEYWORD, c_keywords);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_TYPE, c_types);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, c_constants);
	add_patterns (syn, "*.c", "*.h", "*.inc", NULL);
	add_aliases (syn, "h", "gnu89", "c99", NULL);
	return syn;
}

static LrgSyntax *
build_glsl (void)
{
	LrgSyntax *syn = c_like ("glsl");

	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_KEYWORD, glsl_keywords);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_TYPE, glsl_types);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_FUNCTION, glsl_functions);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_VARIABLE, glsl_variables);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, c_constants);
	add_patterns (syn, "*.glsl", "*.vert", "*.frag", "*.vs", "*.fs", "*.geom", "*.comp", NULL);
	add_aliases (syn, "shader", "vert", "frag", NULL);
	return syn;
}

static LrgSyntax *
build_lua (void)
{
	LrgSyntax *syn = lrg_syntax_new ("lua", LRG_SYNTAX_BASE_CODE);

	lrg_syntax_add_line_comment (syn, "--");
	lrg_syntax_set_block_comment (syn, "--[[", "]]", FALSE);
	lrg_syntax_add_multiline_string (syn, "[[", "]]");
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_KEYWORD, lua_keywords);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, lua_constants);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_FUNCTION, lua_functions);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_VARIABLE, lua_variables);
	add_patterns (syn, "*.lua", NULL);
	return syn;
}

static LrgSyntax *
build_python (void)
{
	LrgSyntax *syn = lrg_syntax_new ("python", LRG_SYNTAX_BASE_CODE);

	lrg_syntax_add_line_comment (syn, "#");
	lrg_syntax_add_multiline_string (syn, "\"\"\"", "\"\"\"");
	lrg_syntax_add_multiline_string (syn, "'''", "'''");
	lrg_syntax_set_prefixes (syn, NULL, "@");
	lrg_syntax_set_options (syn, TRUE, TRUE, TRUE, FALSE);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_KEYWORD, py_keywords);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, py_constants);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_TYPE, py_types);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_FUNCTION, py_functions);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_VARIABLE, py_variables);
	add_patterns (syn, "*.py", "*.pyw", "*.pyi", NULL);
	add_aliases (syn, "py", "python3", NULL);
	return syn;
}

static LrgSyntax *
build_bash (void)
{
	LrgSyntax *syn = lrg_syntax_new ("bash", LRG_SYNTAX_BASE_CODE);

	lrg_syntax_add_line_comment (syn, "#");
	lrg_syntax_set_comment_needs_space (syn, TRUE);
	lrg_syntax_set_string_quotes (syn, "\"'`", '\\', TRUE);
	lrg_syntax_set_raw_quotes (syn, "'");
	lrg_syntax_set_prefixes (syn, "$", NULL);
	lrg_syntax_set_options (syn, TRUE, FALSE, FALSE, FALSE);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_KEYWORD, sh_keywords);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_FUNCTION, sh_functions);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, sh_constants);
	add_patterns (syn, "*.sh", "*.bash", ".bashrc", ".bash_profile", ".profile", "*.zsh", NULL);
	add_aliases (syn, "sh", "shell", "zsh", NULL);
	return syn;
}

static LrgSyntax *
build_yaml (void)
{
	LrgSyntax *syn = lrg_syntax_new ("yaml", LRG_SYNTAX_BASE_YAML);

	lrg_syntax_set_options (syn, FALSE, FALSE, FALSE, FALSE);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, yaml_constants);
	add_patterns (syn, "*.yaml", "*.yml", "*.ryaml", "*.rlevel", "*.rprefab", NULL);
	add_aliases (syn, "yml", NULL);
	return syn;
}

static LrgSyntax *
build_json (void)
{
	LrgSyntax *syn = lrg_syntax_new ("json", LRG_SYNTAX_BASE_CODE);

	lrg_syntax_set_string_quotes (syn, "\"", '\\', FALSE);
	lrg_syntax_set_options (syn, TRUE, FALSE, FALSE, TRUE);
	lrg_syntax_set_strict (syn, TRUE);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, json_constants);
	add_patterns (syn, "*.json", "*.geojson", NULL);
	return syn;
}

static LrgSyntax *
build_markdown (void)
{
	LrgSyntax *syn = lrg_syntax_new ("markdown", LRG_SYNTAX_BASE_MARKDOWN);

	add_patterns (syn, "*.md", "*.markdown", "README", NULL);
	add_aliases (syn, "md", NULL);
	return syn;
}

static LrgSyntax *
build_ini (void)
{
	LrgSyntax *syn = lrg_syntax_new ("ini", LRG_SYNTAX_BASE_INI);

	lrg_syntax_add_line_comment (syn, ";");
	lrg_syntax_add_line_comment (syn, "#");
	lrg_syntax_set_comment_needs_space (syn, TRUE);
	lrg_syntax_add_multiline_string (syn, "\"\"\"", "\"\"\"");
	lrg_syntax_set_options (syn, FALSE, FALSE, FALSE, FALSE);
	lrg_syntax_add_words (syn, LRG_SYNTAX_TOKEN_CONSTANT, ini_constants);
	add_patterns (syn, "*.ini", "*.cfg", "*.conf", "*.desktop", "*.toml", "*.service", NULL);
	add_aliases (syn, "toml", "conf", "desktop", NULL);
	return syn;
}

/* ==========================================================================
 * Registry
 * ========================================================================== */

/* Registered languages in registration order; later entries win */
static GPtrArray *registry = NULL;
G_LOCK_DEFINE_STATIC (registry);

/* ensure_registry: the built-ins, created once (lock held) */
static void
ensure_registry (void)
{
	if (registry != NULL)
		return;
	registry = g_ptr_array_new_with_free_func (g_object_unref);
	g_ptr_array_add (registry, build_c ());
	g_ptr_array_add (registry, build_glsl ());
	g_ptr_array_add (registry, build_lua ());
	g_ptr_array_add (registry, build_python ());
	g_ptr_array_add (registry, build_bash ());
	g_ptr_array_add (registry, build_yaml ());
	g_ptr_array_add (registry, build_json ());
	g_ptr_array_add (registry, build_markdown ());
	g_ptr_array_add (registry, build_ini ());
}

void
lrg_syntax_register (LrgSyntax *syntax)
{
	g_return_if_fail (LRG_IS_SYNTAX (syntax));

	G_LOCK (registry);
	ensure_registry ();
	g_ptr_array_remove (registry, syntax);
	g_ptr_array_add (registry, g_object_ref (syntax));
	G_UNLOCK (registry);
}

/* has_name: @name (lower case) is the language's name or an alias */
static gboolean
has_name (LrgSyntax   *syn,
          const gchar *name)
{
	guint i;

	if (g_ascii_strcasecmp (syn->name, name) == 0)
		return TRUE;
	for (i = 0; i < syn->aliases->len; i++)
		if (g_strcmp0 (g_ptr_array_index (syn->aliases, i), name) == 0)
			return TRUE;
	return FALSE;
}

LrgSyntax *
lrg_syntax_get_for_name (const gchar *name)
{
	g_autofree gchar *lower = NULL;
	LrgSyntax        *found = NULL;
	guint             i;

	g_return_val_if_fail (name != NULL, NULL);

	lower = g_ascii_strdown (name, -1);
	G_LOCK (registry);
	ensure_registry ();
	for (i = registry->len; i > 0 && found == NULL; i--)
		if (has_name (g_ptr_array_index (registry, i - 1), lower))
			found = g_ptr_array_index (registry, i - 1);
	G_UNLOCK (registry);
	return found;
}

LrgSyntax *
lrg_syntax_get_for_filename (const gchar *filename)
{
	LrgSyntax *found = NULL;
	guint      i;

	g_return_val_if_fail (filename != NULL, NULL);

	G_LOCK (registry);
	ensure_registry ();
	for (i = registry->len; i > 0 && found == NULL; i--)
		if (lrg_syntax_matches_filename (g_ptr_array_index (registry, i - 1), filename))
			found = g_ptr_array_index (registry, i - 1);
	G_UNLOCK (registry);
	return found;
}

static gint
compare_names (gconstpointer a,
               gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

gchar **
lrg_syntax_list_languages (void)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new ();
	g_autoptr(GHashTable) seen = g_hash_table_new (g_str_hash, g_str_equal);
	guint                 i;

	G_LOCK (registry);
	ensure_registry ();
	for (i = 0; i < registry->len; i++)
	{
		LrgSyntax *syn = g_ptr_array_index (registry, i);

		if (g_hash_table_add (seen, syn->name))
			g_ptr_array_add (names, g_strdup (syn->name));
	}
	G_UNLOCK (registry);

	g_ptr_array_sort (names, compare_names);
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}
