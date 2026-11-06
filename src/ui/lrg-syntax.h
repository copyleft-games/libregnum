/* lrg-syntax.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSyntax: an incremental, line-at-a-time syntax highlighter.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-theme.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Token kinds
 * ========================================================================== */

/**
 * LrgSyntaxToken:
 * @LRG_SYNTAX_TOKEN_TEXT: plain text (gaps between spans)
 * @LRG_SYNTAX_TOKEN_KEYWORD: a reserved word (if, return, local)
 * @LRG_SYNTAX_TOKEN_TYPE: a type name (int, gboolean, vec3)
 * @LRG_SYNTAX_TOKEN_FUNCTION: a function name or built-in function
 * @LRG_SYNTAX_TOKEN_STRING: a string literal
 * @LRG_SYNTAX_TOKEN_NUMBER: a numeric literal
 * @LRG_SYNTAX_TOKEN_COMMENT: a comment
 * @LRG_SYNTAX_TOKEN_PREPROCESSOR: a preprocessor directive, fence or
 *   document marker
 * @LRG_SYNTAX_TOKEN_OPERATOR: an operator (+ - = &&)
 * @LRG_SYNTAX_TOKEN_PUNCTUATION: brackets, commas, semicolons
 * @LRG_SYNTAX_TOKEN_CONSTANT: a named constant (NULL, true, ALL_CAPS)
 * @LRG_SYNTAX_TOKEN_ATTRIBUTE: a key, decorator or attribute
 * @LRG_SYNTAX_TOKEN_ESCAPE: an escape sequence inside a string
 * @LRG_SYNTAX_TOKEN_VARIABLE: a sigil variable ($HOME), anchor or self
 * @LRG_SYNTAX_TOKEN_HEADING: a heading or section name
 * @LRG_SYNTAX_TOKEN_EMPHASIS: emphasised or strong text
 * @LRG_SYNTAX_TOKEN_LINK: a link or URL
 * @LRG_SYNTAX_TOKEN_ERROR: invalid input
 *
 * What a span of highlighted text is. Each kind has a colour derived
 * from the theme (see lrg_syntax_get_token_color()).
 */
typedef enum
{
	LRG_SYNTAX_TOKEN_TEXT,
	LRG_SYNTAX_TOKEN_KEYWORD,
	LRG_SYNTAX_TOKEN_TYPE,
	LRG_SYNTAX_TOKEN_FUNCTION,
	LRG_SYNTAX_TOKEN_STRING,
	LRG_SYNTAX_TOKEN_NUMBER,
	LRG_SYNTAX_TOKEN_COMMENT,
	LRG_SYNTAX_TOKEN_PREPROCESSOR,
	LRG_SYNTAX_TOKEN_OPERATOR,
	LRG_SYNTAX_TOKEN_PUNCTUATION,
	LRG_SYNTAX_TOKEN_CONSTANT,
	LRG_SYNTAX_TOKEN_ATTRIBUTE,
	LRG_SYNTAX_TOKEN_ESCAPE,
	LRG_SYNTAX_TOKEN_VARIABLE,
	LRG_SYNTAX_TOKEN_HEADING,
	LRG_SYNTAX_TOKEN_EMPHASIS,
	LRG_SYNTAX_TOKEN_LINK,
	LRG_SYNTAX_TOKEN_ERROR
} LrgSyntaxToken;

/**
 * LRG_SYNTAX_N_TOKENS:
 *
 * Number of #LrgSyntaxToken kinds.
 */
#define LRG_SYNTAX_N_TOKENS (LRG_SYNTAX_TOKEN_ERROR + 1)

#define LRG_TYPE_SYNTAX_TOKEN (lrg_syntax_token_get_type ())

/**
 * lrg_syntax_token_get_type:
 *
 * Returns: the #GType of #LrgSyntaxToken
 */
LRG_AVAILABLE_IN_ALL
GType lrg_syntax_token_get_type (void) G_GNUC_CONST;

/**
 * LrgSyntaxBase:
 * @LRG_SYNTAX_BASE_CODE: a programming language: comments, strings,
 *   numbers and word lists
 * @LRG_SYNTAX_BASE_MARKDOWN: Markdown: headings, emphasis, code, links
 * @LRG_SYNTAX_BASE_YAML: YAML: keys, scalars, anchors, block scalars
 * @LRG_SYNTAX_BASE_INI: INI / TOML-like: sections, keys and values
 *
 * The lexer a language is built on. Word lists, comments and strings
 * configure every base; the non-code bases add their own structure.
 */
typedef enum
{
	LRG_SYNTAX_BASE_CODE,
	LRG_SYNTAX_BASE_MARKDOWN,
	LRG_SYNTAX_BASE_YAML,
	LRG_SYNTAX_BASE_INI
} LrgSyntaxBase;

#define LRG_TYPE_SYNTAX_BASE (lrg_syntax_base_get_type ())

/**
 * lrg_syntax_base_get_type:
 *
 * Returns: the #GType of #LrgSyntaxBase
 */
LRG_AVAILABLE_IN_ALL
GType lrg_syntax_base_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * Errors
 * ========================================================================== */

/**
 * LRG_SYNTAX_ERROR:
 *
 * Error domain of language definitions.
 */
#define LRG_SYNTAX_ERROR (lrg_syntax_error_quark ())

/**
 * LrgSyntaxError:
 * @LRG_SYNTAX_ERROR_INVALID: the definition is malformed (wrong types,
 *   missing name, bad pattern)
 *
 * Errors of lrg_syntax_new_from_yaml() and lrg_syntax_new_from_file().
 */
typedef enum
{
	LRG_SYNTAX_ERROR_INVALID
} LrgSyntaxError;

/**
 * lrg_syntax_error_quark:
 *
 * Returns: the #GQuark of #LRG_SYNTAX_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_syntax_error_quark (void);

/* ==========================================================================
 * Spans
 * ========================================================================== */

#define LRG_TYPE_SYNTAX_SPAN (lrg_syntax_span_get_type ())

/**
 * LrgSyntaxSpan:
 * @start: first byte of the span in the line
 * @end: byte after the span
 * @token: what the bytes are
 *
 * One highlighted range of a line. Spans are emitted in order and never
 * overlap; bytes not covered by any span are %LRG_SYNTAX_TOKEN_TEXT.
 */
typedef struct
{
	guint          start;
	guint          end;
	LrgSyntaxToken token;
} LrgSyntaxSpan;

/**
 * lrg_syntax_span_get_type:
 *
 * Returns: the #GType of #LrgSyntaxSpan
 */
LRG_AVAILABLE_IN_ALL
GType lrg_syntax_span_get_type (void) G_GNUC_CONST;

/**
 * lrg_syntax_span_copy:
 * @self: a span
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgSyntaxSpan * lrg_syntax_span_copy (const LrgSyntaxSpan *self);

/**
 * lrg_syntax_span_free:
 * @self: (nullable): a span from lrg_syntax_span_copy()
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_span_free (LrgSyntaxSpan *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSyntaxSpan, lrg_syntax_span_free)

/* ==========================================================================
 * LrgSyntax
 * ========================================================================== */

#define LRG_TYPE_SYNTAX (lrg_syntax_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSyntax, lrg_syntax, LRG, SYNTAX, GObject)

/**
 * lrg_syntax_new:
 * @name: language name (e.g. "toml")
 * @base: the lexer the language is built on
 *
 * Creates an empty language to configure in code with the setters
 * below, then optionally lrg_syntax_register().
 *
 * Returns: (transfer full): a new #LrgSyntax
 */
LRG_AVAILABLE_IN_ALL
LrgSyntax * lrg_syntax_new (const gchar   *name,
                            LrgSyntaxBase  base);

/**
 * lrg_syntax_new_from_yaml:
 * @data: YAML text of a language definition
 * @length: length of @data, or -1 if nul-terminated
 * @error: return location for a #GError
 *
 * Builds a language from a YAML definition (see docs/modules/ui/widgets/
 * syntax.org for the schema). The language is not registered.
 *
 * Returns: (transfer full) (nullable): the language, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSyntax * lrg_syntax_new_from_yaml (const gchar  *data,
                                      gssize        length,
                                      GError      **error);

/**
 * lrg_syntax_new_from_file:
 * @path: a YAML language definition file
 * @error: return location for a #GError
 *
 * lrg_syntax_new_from_yaml() on the contents of @path.
 *
 * Returns: (transfer full) (nullable): the language, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSyntax * lrg_syntax_new_from_file (const gchar  *path,
                                      GError      **error);

/**
 * lrg_syntax_register:
 * @syntax: a language
 *
 * Makes @syntax findable by lrg_syntax_get_for_name() (its name and
 * aliases) and lrg_syntax_get_for_filename() (its file patterns). A
 * language registered later wins over an earlier one, built-ins
 * included, so applications can replace a built-in language.
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_register (LrgSyntax *syntax);

/**
 * lrg_syntax_get_for_name:
 * @name: a language name or alias, case-insensitive ("c", "py", "sh")
 *
 * Looks a language up among the built-ins (c, glsl, lua, python, bash,
 * yaml, json, markdown, ini) and registered languages.
 *
 * Returns: (transfer none) (nullable): the language, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSyntax * lrg_syntax_get_for_name (const gchar *name);

/**
 * lrg_syntax_get_for_filename:
 * @filename: a file name or path
 *
 * Picks a language by the file's base name matched against each
 * language's patterns ("*.c", "Makefile").
 *
 * Returns: (transfer none) (nullable): the language, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSyntax * lrg_syntax_get_for_filename (const gchar *filename);

/**
 * lrg_syntax_list_languages:
 *
 * Returns: (transfer full) (array zero-terminated=1): the names of all
 *   known languages, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_syntax_list_languages (void);

/* --------------------------------------------------------------------------
 * Tokenizing
 * -------------------------------------------------------------------------- */

/**
 * lrg_syntax_tokenize_line:
 * @self: a language
 * @line: one line of UTF-8 text, without its newline
 * @length: length of @line in bytes, or -1 if nul-terminated
 * @state: the state the previous line ended in (0 for the first line)
 * @spans: (nullable) (element-type LrgSyntaxSpan): array the spans are
 *   appended to, or %NULL to compute only the end state
 *
 * Tokenizes a single line. Constructs that span lines (block comments,
 * multi-line strings, fenced code, YAML block scalars) are carried in
 * the returned state, so an editor re-tokenizes a changed line and the
 * following lines only until the state they start with is the same as
 * before (the highlighting has converged).
 *
 * Returns: the state at the end of the line, for the next line
 */
LRG_AVAILABLE_IN_ALL
guint lrg_syntax_tokenize_line (LrgSyntax   *self,
                                const gchar *line,
                                gssize       length,
                                guint        state,
                                GArray      *spans);

/* --------------------------------------------------------------------------
 * Colours
 * -------------------------------------------------------------------------- */

/**
 * lrg_syntax_get_token_color:
 * @self: (nullable): a language whose overrides apply, or %NULL
 * @theme: the theme the colours derive from
 * @token: a token kind
 * @out: (out caller-allocates): the colour
 *
 * The colour @token is drawn in: an override set with
 * lrg_syntax_set_token_color(), else a colour derived from the theme's
 * roles (keywords ACCENT, strings SUCCESS, numbers WARNING, comments
 * TEXT_MUTED, functions LINK, ...) and lifted towards TEXT until it
 * reads against the INPUT background, so every preset stays legible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_get_token_color (LrgSyntax      *self,
                                 LrgTheme       *theme,
                                 LrgSyntaxToken  token,
                                 GrlColor       *out);

/**
 * lrg_syntax_set_token_color:
 * @self: a language
 * @token: a token kind
 * @color: (nullable): the colour, or %NULL for the theme default
 *
 * Overrides the colour of @token for every editor using @self. Built-in
 * languages are shared: override a per-editor colour with
 * lrg_text_editor_set_token_color() instead.
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_token_color (LrgSyntax      *self,
                                 LrgSyntaxToken  token,
                                 const GrlColor *color);

/* --------------------------------------------------------------------------
 * Definition
 * -------------------------------------------------------------------------- */

/**
 * lrg_syntax_get_name:
 * @self: a language
 *
 * Returns: (transfer none): the language name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_syntax_get_name (LrgSyntax *self);

/**
 * lrg_syntax_get_base:
 * @self: a language
 *
 * Returns: the lexer the language is built on
 */
LRG_AVAILABLE_IN_ALL
LrgSyntaxBase lrg_syntax_get_base (LrgSyntax *self);

/**
 * lrg_syntax_add_words:
 * @self: a language
 * @token: the kind the words are highlighted as (keyword, type,
 *   constant, function, variable, ...)
 * @words: (array zero-terminated=1): identifiers
 *
 * Adds identifiers to the language's word table. A word added again
 * takes the later kind.
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_add_words (LrgSyntax           *self,
                           LrgSyntaxToken       token,
                           const gchar * const *words);

/**
 * lrg_syntax_lookup_word:
 * @self: a language
 * @word: an identifier
 *
 * Returns: the kind of @word, %LRG_SYNTAX_TOKEN_TEXT if it is not in
 *   the word table
 */
LRG_AVAILABLE_IN_ALL
LrgSyntaxToken lrg_syntax_lookup_word (LrgSyntax   *self,
                                       const gchar *word);

/**
 * lrg_syntax_add_line_comment:
 * @self: a language
 * @marker: text that starts a comment running to the end of the line
 *
 * Several markers are allowed (INI uses ";" and "#").
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_add_line_comment (LrgSyntax   *self,
                                  const gchar *marker);

/**
 * lrg_syntax_set_block_comment:
 * @self: a language
 * @start: (nullable): text that opens a block comment, %NULL for none
 * @end: (nullable): text that closes it
 * @nested: whether block comments nest
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_block_comment (LrgSyntax   *self,
                                   const gchar *start,
                                   const gchar *end,
                                   gboolean     nested);

/**
 * lrg_syntax_set_string_quotes:
 * @self: a language
 * @quotes: (nullable): the characters that delimit strings ("\"'")
 * @escape: escape character inside strings, 0 for none
 * @span_lines: whether a string left open continues on the next line
 *   (shell quotes); otherwise only an escaped line end continues it
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_string_quotes (LrgSyntax   *self,
                                   const gchar *quotes,
                                   gunichar     escape,
                                   gboolean     span_lines);

/**
 * lrg_syntax_set_raw_quotes:
 * @self: a language
 * @quotes: (nullable): quote characters whose strings have no escapes
 *   (the shell's "'"), %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_raw_quotes (LrgSyntax   *self,
                                const gchar *quotes);

/**
 * lrg_syntax_add_multiline_string:
 * @self: a language
 * @start: text that opens the string ("\"\"\"", "[[")
 * @end: text that closes it
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_add_multiline_string (LrgSyntax   *self,
                                      const gchar *start,
                                      const gchar *end);

/**
 * lrg_syntax_set_preprocessor:
 * @self: a language
 * @prefix: (nullable): text that starts a directive at the beginning
 *   of a line ("#"), %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_preprocessor (LrgSyntax   *self,
                                  const gchar *prefix);

/**
 * lrg_syntax_set_prefixes:
 * @self: a language
 * @variable: (nullable): characters that start a variable ("$")
 * @attribute: (nullable): characters that start a decorator or
 *   attribute ("@")
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_prefixes (LrgSyntax   *self,
                              const gchar *variable,
                              const gchar *attribute);

/**
 * lrg_syntax_set_word_chars:
 * @self: a language
 * @chars: (nullable): extra characters allowed inside identifiers
 *   besides letters, digits and '_' ("-" for CSS-like names)
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_word_chars (LrgSyntax   *self,
                                const gchar *chars);

/**
 * lrg_syntax_set_number_pattern:
 * @self: a language
 * @pattern: (nullable): a #GRegex pattern matched where a number may
 *   start, %NULL for the built-in number scanner
 * @error: return location for a #GError
 *
 * Returns: %TRUE if the pattern compiled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_syntax_set_number_pattern (LrgSyntax    *self,
                                        const gchar  *pattern,
                                        GError      **error);

/**
 * lrg_syntax_set_options:
 * @self: a language
 * @case_sensitive: whether the word table is case-sensitive
 * @functions: whether an identifier followed by '(' is a function
 * @caps_constants: whether ALL_CAPS identifiers are constants
 * @keys: whether a string or word followed by ':' is a key (JSON)
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_options (LrgSyntax *self,
                             gboolean   case_sensitive,
                             gboolean   functions,
                             gboolean   caps_constants,
                             gboolean   keys);

/**
 * lrg_syntax_set_comment_needs_space:
 * @self: a language
 * @needs_space: a line comment only starts at the line start or after
 *   whitespace (shell: "$#" is not a comment)
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_comment_needs_space (LrgSyntax *self,
                                         gboolean   needs_space);

/**
 * lrg_syntax_set_strict:
 * @self: a language
 * @strict: identifiers missing from the word table are errors (JSON
 *   allows only true, false and null)
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_set_strict (LrgSyntax *self,
                            gboolean   strict);

/**
 * lrg_syntax_add_pattern:
 * @self: a language
 * @pattern: a file name glob ("*.c", "Makefile")
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_add_pattern (LrgSyntax   *self,
                             const gchar *pattern);

/**
 * lrg_syntax_add_alias:
 * @self: a language
 * @alias: another name lrg_syntax_get_for_name() accepts ("py")
 */
LRG_AVAILABLE_IN_ALL
void lrg_syntax_add_alias (LrgSyntax   *self,
                           const gchar *alias);

/**
 * lrg_syntax_matches_filename:
 * @self: a language
 * @filename: a file name or path
 *
 * Returns: %TRUE if the base name of @filename matches one of the
 *   language's patterns
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_syntax_matches_filename (LrgSyntax   *self,
                                      const gchar *filename);

G_END_DECLS
