/* test-ui-text-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSyntax (built-in languages, multi-line state, YAML languages,
 * colours) and LrgTextEditor (editing, UTF-8, selection, undo
 * coalescing, indentation, submit mode, max-lines, find, wrapping,
 * scrolling, virtualised painting, read-only).
 *
 * Headless: the editor sits on a canvas, is driven through
 * lrg_canvas_process_input() and stepped at 60 Hz. GPU cases render
 * under a hidden window and skip without a display.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Syntax helpers
 * ========================================================================== */

/* tokenize: spans of one line from @state; *@out_state gets the end state */
static GArray *
tokenize (LrgSyntax   *syn,
          const gchar *line,
          guint        state,
          guint       *out_state)
{
	GArray *spans = g_array_new (FALSE, FALSE, sizeof (LrgSyntaxSpan));
	guint   end = lrg_syntax_tokenize_line (syn, line, -1, state, spans);

	if (out_state != NULL)
		*out_state = end;
	return spans;
}

/* token_at: the kind covering byte @byte (TEXT in the gaps) */
static LrgSyntaxToken
token_at (GArray *spans,
          guint   byte)
{
	guint i;

	for (i = 0; i < spans->len; i++)
	{
		LrgSyntaxSpan *s = &g_array_index (spans, LrgSyntaxSpan, i);

		if (byte >= s->start && byte < s->end)
			return s->token;
	}
	return LRG_SYNTAX_TOKEN_TEXT;
}

/* end_state: the state a line ends in (state-only pass) */
static guint
end_state (LrgSyntax   *syn,
           const gchar *line,
           guint        state)
{
	return lrg_syntax_tokenize_line (syn, line, -1, state, NULL);
}

/* kind_of: the token at the first occurrence of @needle in @line */
static LrgSyntaxToken
kind_of (LrgSyntax   *syn,
         const gchar *line,
         guint        state,
         const gchar *needle)
{
	g_autoptr(GArray) spans = tokenize (syn, line, state, NULL);
	const gchar      *at = strstr (line, needle);

	g_assert_nonnull (at);
	return token_at (spans, (guint) (at - line));
}

/* ==========================================================================
 * Syntax tests
 * ========================================================================== */

static void
test_syntax_registry (void)
{
	g_auto(GStrv) names = lrg_syntax_list_languages ();
	static const gchar * const expected[] = {
		"bash", "c", "glsl", "ini", "json", "lua", "markdown", "python", "yaml", NULL
	};
	guint i;

	for (i = 0; expected[i] != NULL; i++)
	{
		g_assert_true (g_strv_contains ((const gchar * const *) names, expected[i]));
		g_assert_nonnull (lrg_syntax_get_for_name (expected[i]));
	}
	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_name ("PY")), ==, "python");
	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_name ("sh")), ==, "bash");
	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_name ("yml")), ==, "yaml");
	g_assert_null (lrg_syntax_get_for_name ("cobol"));

	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_filename ("/src/lrg-widget.c")), ==, "c");
	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_filename ("main.lua")), ==, "lua");
	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_filename ("glow.frag")), ==, "glsl");
	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_filename ("README.md")), ==, "markdown");
	g_assert_cmpstr (lrg_syntax_get_name (lrg_syntax_get_for_filename ("level.rlevel")), ==, "yaml");
	g_assert_null (lrg_syntax_get_for_filename ("photo.png"));
	g_assert_cmpint (lrg_syntax_get_base (lrg_syntax_get_for_name ("ini")), ==, LRG_SYNTAX_BASE_INI);
	g_assert_cmpint (lrg_syntax_lookup_word (lrg_syntax_get_for_name ("c"), "gboolean"), ==,
	                 LRG_SYNTAX_TOKEN_TYPE);
}

static void
test_syntax_c (void)
{
	LrgSyntax         *c = lrg_syntax_get_for_name ("c");
	g_autoptr(GArray)  spans = NULL;
	const gchar       *str = "s = \"a\\nb\";";

	g_assert_cmpint (kind_of (c, "#include <stdio.h>", 0, "#include"), ==, LRG_SYNTAX_TOKEN_PREPROCESSOR);
	g_assert_cmpint (kind_of (c, "#include <stdio.h>", 0, "<stdio"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (c, "#define MAX_ROWS (10)", 0, "MAX_ROWS"), ==, LRG_SYNTAX_TOKEN_CONSTANT);

	g_assert_cmpint (kind_of (c, "static gint run (void) { return 0x1F; }", 0, "static"), ==,
	                 LRG_SYNTAX_TOKEN_KEYWORD);
	g_assert_cmpint (kind_of (c, "static gint run (void) { return 0x1F; }", 0, "gint"), ==,
	                 LRG_SYNTAX_TOKEN_TYPE);
	g_assert_cmpint (kind_of (c, "static gint run (void) { return 0x1F; }", 0, "run"), ==,
	                 LRG_SYNTAX_TOKEN_FUNCTION);
	g_assert_cmpint (kind_of (c, "static gint run (void) { return 0x1F; }", 0, "0x1F"), ==,
	                 LRG_SYNTAX_TOKEN_NUMBER);
	g_assert_cmpint (kind_of (c, "static gint run (void) { return 0x1F; }", 0, "{"), ==,
	                 LRG_SYNTAX_TOKEN_PUNCTUATION);
	g_assert_cmpint (kind_of (c, "x = a + 1.5e3f; // done", 0, "+"), ==, LRG_SYNTAX_TOKEN_OPERATOR);
	g_assert_cmpint (kind_of (c, "x = a + 1.5e3f; // done", 0, "1.5e3f"), ==, LRG_SYNTAX_TOKEN_NUMBER);
	g_assert_cmpint (kind_of (c, "x = a + 1.5e3f; // done", 0, "// done"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (c, "p = NULL;", 0, "NULL"), ==, LRG_SYNTAX_TOKEN_CONSTANT);
	g_assert_cmpint (kind_of (c, "a2b = 3;", 0, "a2b"), ==, LRG_SYNTAX_TOKEN_TEXT);

	/* escapes split the string */
	spans = tokenize (c, str, 0, NULL);
	g_assert_cmpint (token_at (spans, 4), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (token_at (spans, 6), ==, LRG_SYNTAX_TOKEN_ESCAPE);
	g_assert_cmpint (token_at (spans, 7), ==, LRG_SYNTAX_TOKEN_ESCAPE);
	g_assert_cmpint (token_at (spans, 8), ==, LRG_SYNTAX_TOKEN_STRING);

	/* spans are ordered and disjoint */
	{
		guint i;

		for (i = 1; i < spans->len; i++)
			g_assert_cmpuint (g_array_index (spans, LrgSyntaxSpan, i - 1).end, <=,
			                  g_array_index (spans, LrgSyntaxSpan, i).start);
	}
}

/*
 * Multi-line constructs carry state to the next line and the state
 * returns to 0 once they close, so a highlighter can stop re-tokenizing
 * as soon as a line starts in the state it started in before.
 */
static void
test_syntax_multiline (void)
{
	LrgSyntax *c = lrg_syntax_get_for_name ("c");
	LrgSyntax *py = lrg_syntax_get_for_name ("python");
	LrgSyntax *lua = lrg_syntax_get_for_name ("lua");
	LrgSyntax *sh = lrg_syntax_get_for_name ("bash");
	LrgSyntax *md = lrg_syntax_get_for_name ("markdown");
	guint      s1;
	guint      s2;
	guint      s3;

	/* C block comment */
	g_assert_cmpint (kind_of (c, "int a; /* open", 0, "int"), ==, LRG_SYNTAX_TOKEN_TYPE);
	s1 = end_state (c, "int a; /* open", 0);
	g_assert_cmpuint (s1, !=, 0);
	g_assert_cmpint (kind_of (c, "still int here", s1, "int"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	s2 = end_state (c, "still int here", s1);
	g_assert_cmpuint (s2, ==, s1);
	g_assert_cmpint (kind_of (c, "end */ int b;", s2, "end"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (c, "end */ int b;", s2, "int"), ==, LRG_SYNTAX_TOKEN_TYPE);
	s3 = end_state (c, "end */ int b;", s2);
	g_assert_cmpuint (s3, ==, 0);

	/* C string continued with a backslash */
	s1 = end_state (c, "s = \"long \\", 0);
	g_assert_cmpuint (s1, !=, 0);
	g_assert_cmpint (kind_of (c, "tail\"; int x;", s1, "tail"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (c, "tail\"; int x;", s1, "int"), ==, LRG_SYNTAX_TOKEN_TYPE);

	/* Python triple-quoted string (prefixed too) */
	s1 = end_state (py, "doc = r\"\"\"first", 0);
	g_assert_cmpuint (s1, !=, 0);
	g_assert_cmpint (kind_of (py, "def not code", s1, "def"), ==, LRG_SYNTAX_TOKEN_STRING);
	s2 = end_state (py, "end\"\"\" + x", s1);
	g_assert_cmpuint (s2, ==, 0);

	/* Lua long comment wins over the line comment, long strings */
	s1 = end_state (lua, "--[[ block", 0);
	g_assert_cmpuint (s1, !=, 0);
	g_assert_cmpint (kind_of (lua, "local x ]] local y", s1, "x"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (lua, "local x ]] local y", s1, "local y"), ==, LRG_SYNTAX_TOKEN_KEYWORD);
	s1 = end_state (lua, "s = [[raw", 0);
	g_assert_cmpuint (s1, !=, 0);
	g_assert_cmpint (kind_of (lua, "then]]", s1, "then"), ==, LRG_SYNTAX_TOKEN_STRING);

	/* Shell quotes span lines */
	s1 = end_state (sh, "echo \"multi", 0);
	g_assert_cmpuint (s1, !=, 0);
	g_assert_cmpint (kind_of (sh, "if done\" fi", s1, "if"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (sh, "if done\" fi", s1, "fi"), ==, LRG_SYNTAX_TOKEN_KEYWORD);

	/* Markdown fenced code */
	s1 = end_state (md, "```c", 0);
	g_assert_cmpuint (s1, !=, 0);
	g_assert_cmpint (kind_of (md, "# not a heading", s1, "#"), ==, LRG_SYNTAX_TOKEN_STRING);
	s2 = end_state (md, "```", s1);
	g_assert_cmpuint (s2, ==, 0);
}

static void
test_syntax_languages (void)
{
	LrgSyntax *py = lrg_syntax_get_for_name ("python");
	LrgSyntax *sh = lrg_syntax_get_for_name ("bash");
	LrgSyntax *yaml = lrg_syntax_get_for_name ("yaml");
	LrgSyntax *json = lrg_syntax_get_for_name ("json");
	LrgSyntax *md = lrg_syntax_get_for_name ("markdown");
	LrgSyntax *ini = lrg_syntax_get_for_name ("ini");
	LrgSyntax *glsl = lrg_syntax_get_for_name ("glsl");
	guint      st;

	/* Python */
	g_assert_cmpint (kind_of (py, "@app.route", 0, "@app"), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpint (kind_of (py, "def run(self):", 0, "def"), ==, LRG_SYNTAX_TOKEN_KEYWORD);
	g_assert_cmpint (kind_of (py, "def run(self):", 0, "self"), ==, LRG_SYNTAX_TOKEN_VARIABLE);
	g_assert_cmpint (kind_of (py, "x = None  # nothing", 0, "None"), ==, LRG_SYNTAX_TOKEN_CONSTANT);
	g_assert_cmpint (kind_of (py, "x = None  # nothing", 0, "#"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (py, "print(f'{x}')", 0, "f'"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (py, "print(f'{x}')", 0, "print"), ==, LRG_SYNTAX_TOKEN_FUNCTION);

	/* Bash: variables, builtins, '#' needs a space, raw single quotes */
	g_assert_cmpint (kind_of (sh, "echo \"$HOME\" ${#arr[@]}", 0, "echo"), ==, LRG_SYNTAX_TOKEN_FUNCTION);
	g_assert_cmpint (kind_of (sh, "cd $HOME # go", 0, "$HOME"), ==, LRG_SYNTAX_TOKEN_VARIABLE);
	g_assert_cmpint (kind_of (sh, "cd $HOME # go", 0, "# go"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (sh, "n=${#arr}", 0, "#"), ==, LRG_SYNTAX_TOKEN_VARIABLE);
	g_assert_cmpint (kind_of (sh, "echo 'a\\b'", 0, "\\"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (sh, "echo \"a\\b\"", 0, "\\"), ==, LRG_SYNTAX_TOKEN_ESCAPE);

	/* YAML */
	g_assert_cmpint (kind_of (yaml, "name: hero # main", 0, "name"), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpint (kind_of (yaml, "name: hero # main", 0, "hero"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (yaml, "name: hero # main", 0, "# main"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (yaml, "- speed: 4.5", 0, "-"), ==, LRG_SYNTAX_TOKEN_PUNCTUATION);
	g_assert_cmpint (kind_of (yaml, "- speed: 4.5", 0, "4.5"), ==, LRG_SYNTAX_TOKEN_NUMBER);
	g_assert_cmpint (kind_of (yaml, "on: Yes", 0, "Yes"), ==, LRG_SYNTAX_TOKEN_CONSTANT);
	g_assert_cmpint (kind_of (yaml, "base: &b {x: 1}", 0, "&b"), ==, LRG_SYNTAX_TOKEN_VARIABLE);
	g_assert_cmpint (kind_of (yaml, "base: &b {x: 1}", 0, "x"), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpint (kind_of (yaml, "v: !!str 3", 0, "!!str"), ==, LRG_SYNTAX_TOKEN_TYPE);
	g_assert_cmpint (kind_of (yaml, "\"quoted key\": 1", 0, "quoted"), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpint (kind_of (yaml, "\tkey: 1", 0, "\t"), ==, LRG_SYNTAX_TOKEN_ERROR);
	st = end_state (yaml, "script: |", 0);
	g_assert_cmpuint (st, !=, 0);
	g_assert_cmpint (kind_of (yaml, "  key: inside", st, "key"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (yaml, "next: out", st, "next"), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpuint (lrg_syntax_tokenize_line (yaml, "next: out", -1, st, NULL), ==, 0);

	/* JSON: keys, strict words */
	g_assert_cmpint (kind_of (json, "{\"id\": 3, \"ok\": true}", 0, "\"id\""), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpint (kind_of (json, "{\"id\": 3, \"ok\": true}", 0, "true"), ==, LRG_SYNTAX_TOKEN_CONSTANT);
	g_assert_cmpint (kind_of (json, "[\"v\", nope]", 0, "\"v\""), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (json, "[\"v\", nope]", 0, "nope"), ==, LRG_SYNTAX_TOKEN_ERROR);

	/* Markdown inline */
	g_assert_cmpint (kind_of (md, "## Title", 0, "Title"), ==, LRG_SYNTAX_TOKEN_HEADING);
	g_assert_cmpint (kind_of (md, "a **bold** and `code`", 0, "bold"), ==, LRG_SYNTAX_TOKEN_EMPHASIS);
	g_assert_cmpint (kind_of (md, "a **bold** and `code`", 0, "code"), ==, LRG_SYNTAX_TOKEN_STRING);
	g_assert_cmpint (kind_of (md, "see [docs](http://x)", 0, "docs"), ==, LRG_SYNTAX_TOKEN_LINK);
	g_assert_cmpint (kind_of (md, "- item", 0, "-"), ==, LRG_SYNTAX_TOKEN_PUNCTUATION);
	g_assert_cmpint (kind_of (md, "snake_case_word", 0, "case"), ==, LRG_SYNTAX_TOKEN_TEXT);

	/* INI */
	g_assert_cmpint (kind_of (ini, "[window]", 0, "window"), ==, LRG_SYNTAX_TOKEN_HEADING);
	g_assert_cmpint (kind_of (ini, "width = 1280 ; px", 0, "width"), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpint (kind_of (ini, "width = 1280 ; px", 0, "1280"), ==, LRG_SYNTAX_TOKEN_NUMBER);
	g_assert_cmpint (kind_of (ini, "width = 1280 ; px", 0, "; px"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (ini, "vsync = TRUE", 0, "TRUE"), ==, LRG_SYNTAX_TOKEN_CONSTANT);
	g_assert_cmpint (kind_of (ini, "# comment", 0, "comment"), ==, LRG_SYNTAX_TOKEN_COMMENT);

	/* GLSL */
	g_assert_cmpint (kind_of (glsl, "vec3 c = texture(tex, uv).rgb;", 0, "vec3"), ==, LRG_SYNTAX_TOKEN_TYPE);
	g_assert_cmpint (kind_of (glsl, "vec3 c = texture(tex, uv).rgb;", 0, "texture"), ==,
	                 LRG_SYNTAX_TOKEN_FUNCTION);
	g_assert_cmpint (kind_of (glsl, "gl_Position = p;", 0, "gl_Position"), ==, LRG_SYNTAX_TOKEN_VARIABLE);

	/* UTF-8 identifiers stay whole and invalid bytes do not crash */
	g_assert_cmpint (kind_of (py, "größe = 1", 0, "größe"), ==, LRG_SYNTAX_TOKEN_TEXT);
	g_assert_cmpuint (lrg_syntax_tokenize_line (py, "\xff\xfe = '\xc3", -1, 0, NULL), ==, 0);
}

static const gchar *toml_yaml =
	"name: toml-lite\n"
	"base: ini\n"
	"extensions: [\"*.tomlite\"]\n"
	"aliases: [tl]\n"
	"line-comment: \"#\"\n"
	"comment-needs-space: true\n"
	"strings: \"\\\"'\"\n"
	"multiline-strings:\n"
	"  - '\"\"\"'\n"
	"case-sensitive: false\n"
	"constants: [true, false, inf, nan]\n"
	"number-pattern: \"[+-]?[0-9][0-9_]*(\\\\.[0-9]+)?\"\n"
	"colors:\n"
	"  heading: \"#ff00ff\"\n";

static void
test_syntax_yaml_load (void)
{
	g_autoptr(GError)    error = NULL;
	g_autoptr(LrgSyntax) syn = lrg_syntax_new_from_yaml (toml_yaml, -1, &error);
	GrlColor             c;
	g_autoptr(LrgTheme)  theme = lrg_theme_new_preset ("midnight");
	guint                st;

	g_assert_no_error (error);
	g_assert_nonnull (syn);
	g_assert_cmpstr (lrg_syntax_get_name (syn), ==, "toml-lite");
	g_assert_cmpint (lrg_syntax_get_base (syn), ==, LRG_SYNTAX_BASE_INI);
	g_assert_true (lrg_syntax_matches_filename (syn, "a/b/c.tomlite"));

	g_assert_cmpint (kind_of (syn, "[server]", 0, "server"), ==, LRG_SYNTAX_TOKEN_HEADING);
	g_assert_cmpint (kind_of (syn, "port = -8_080 # dev", 0, "port"), ==, LRG_SYNTAX_TOKEN_ATTRIBUTE);
	g_assert_cmpint (kind_of (syn, "port = -8_080 # dev", 0, "-8_080"), ==, LRG_SYNTAX_TOKEN_NUMBER);
	g_assert_cmpint (kind_of (syn, "port = -8_080 # dev", 0, "# dev"), ==, LRG_SYNTAX_TOKEN_COMMENT);
	g_assert_cmpint (kind_of (syn, "on = TRUE", 0, "TRUE"), ==, LRG_SYNTAX_TOKEN_CONSTANT);
	st = end_state (syn, "text = \"\"\"multi", 0);
	g_assert_cmpuint (st, !=, 0);
	g_assert_cmpint (kind_of (syn, "key = 1", st, "key"), ==, LRG_SYNTAX_TOKEN_STRING);

	/* colour override from the definition */
	lrg_syntax_get_token_color (syn, theme, LRG_SYNTAX_TOKEN_HEADING, &c);
	g_assert_cmpuint (c.r, ==, 255);
	g_assert_cmpuint (c.g, ==, 0);
	g_assert_cmpuint (c.b, ==, 255);

	/* registered: found by name, alias and file name; later wins */
	lrg_syntax_register (syn);
	g_assert_true (lrg_syntax_get_for_name ("Toml-Lite") == syn);
	g_assert_true (lrg_syntax_get_for_name ("tl") == syn);
	g_assert_true (lrg_syntax_get_for_filename ("x.tomlite") == syn);
}

static void
test_syntax_yaml_bad (void)
{
	static const gchar * const bad[] = {
		"name: [unclosed",                                 /* not YAML */
		"- just\n- a list\n",                              /* not a mapping */
		"keywords: [if]\n",                                /* no name */
		"name: x\nnumber-pattern: \"([0-9\"\n",            /* bad regex */
		"name: x\nkeywords: {a: b}\n",                     /* list expected */
		"name: x\nkeywords: [[nested]]\n",                 /* strings expected */
		"name: x\nbase: cobol\n",                          /* unknown base */
		"name: x\nblock-comment: [\"/*\"]\n",              /* pair expected */
		"name: x\ncolors: {sparkle: \"#fff\"}\n",          /* unknown token */
		"name: x\ncolors: {keyword: \"not a colour\"}\n",  /* bad colour */
		"name: x\nescape: \"ab\"\n",                       /* one character */
		NULL
	};
	guint i;

	for (i = 0; bad[i] != NULL; i++)
	{
		g_autoptr(GError)    error = NULL;
		g_autoptr(LrgSyntax) syn = lrg_syntax_new_from_yaml (bad[i], -1, &error);

		g_assert_null (syn);
		g_assert_nonnull (error);
	}

	{
		g_autoptr(GError)    error = NULL;
		g_autoptr(LrgSyntax) syn = lrg_syntax_new_from_file ("/nonexistent/lang.yaml", &error);

		g_assert_null (syn);
		g_assert_nonnull (error);
	}
}

/* Every derived token colour reads on the editor background in every preset */
static void
test_syntax_colors (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	LrgSyntax           *c = lrg_syntax_get_for_name ("c");
	guint                i;
	guint                t;

	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (presets[i]);
		const GrlColor     *bg = lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT);
		GrlColor            kw;
		GrlColor            str;

		for (t = 0; t < LRG_SYNTAX_N_TOKENS; t++)
		{
			GrlColor col;

			lrg_syntax_get_token_color (NULL, theme, (LrgSyntaxToken) t, &col);
			g_assert_cmpfloat (lrg_paint_color_contrast (&col, bg), >=, 3.0f);
		}
		/* keywords and strings are told apart */
		lrg_syntax_get_token_color (c, theme, LRG_SYNTAX_TOKEN_KEYWORD, &kw);
		lrg_syntax_get_token_color (c, theme, LRG_SYNTAX_TOKEN_STRING, &str);
		g_assert_false (kw.r == str.r && kw.g == str.g && kw.b == str.b);
	}
}

/* ==========================================================================
 * Canvas and input helpers
 * ========================================================================== */

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	lrg_canvas_set_paint_background (canvas, FALSE);
	return canvas;
}

/* editor_on: a new editor placed on @canvas (the canvas owns it) */
static LrgTextEditor *
editor_on (LrgCanvas *canvas,
           gfloat     w,
           gfloat     h)
{
	LrgTextEditor *ed = lrg_text_editor_new ();

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (ed));
	g_object_unref (ed);
	lrg_widget_allocate (LRG_WIDGET (ed), 10.0f, 10.0f, w, h);
	lrg_canvas_record (canvas);
	return ed;
}

static void
frame (LrgCanvasInput *input,
       gfloat          x,
       gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = DT;
}

static void
press_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
drag_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_at (LrgCanvas *canvas,
            gfloat     x,
            gfloat     y)
{
	LrgCanvasInput input;

	frame (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	press_at (canvas, x, y);
	release_at (canvas, x, y);
}

/* key_mods: one frame with a key press and modifiers */
static void
key_mods (LrgCanvas         *canvas,
          GrlKey             k,
          LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame (&input, 0.0f, 0.0f);
	input.modifiers = mods;
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
key (LrgCanvas *canvas,
     GrlKey     k)
{
	key_mods (canvas, k, LRG_INPUT_MODIFIER_NONE);
}

/* type: typed characters (one frame per character, like real typing) */
static void
type (LrgCanvas   *canvas,
      const gchar *utf8)
{
	const gchar *p;

	for (p = utf8; *p != '\0'; p = g_utf8_next_char (p))
	{
		LrgCanvasInput input;

		frame (&input, 0.0f, 0.0f);
		lrg_canvas_input_add_char (&input, g_utf8_get_char (p));
		lrg_canvas_process_input (canvas, &input);
	}
}

static void
settle (LrgCanvas *canvas,
        guint      frames)
{
	guint i;

	for (i = 0; i < frames; i++)
		lrg_canvas_update (canvas, DT);
}

/* focus: click the editor's top-left text area so it has focus */
static void
focus (LrgCanvas     *canvas,
       LrgTextEditor *ed)
{
	click_at (canvas, lrg_widget_get_world_x (LRG_WIDGET (ed)) + 4.0f,
	          lrg_widget_get_world_y (LRG_WIDGET (ed)) + 4.0f);
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (ed)) & LRG_WIDGET_STATE_FOCUSED);
	settle (canvas, 30);
}

static gchar *
text_of (LrgTextEditor *ed)
{
	return lrg_text_editor_get_text (ed);
}

#define ASSERT_TEXT(ed, expect) \
	G_STMT_START { g_autofree gchar *t_ = text_of (ed); g_assert_cmpstr (t_, ==, (expect)); } G_STMT_END

static void
assert_cursor (LrgTextEditor *ed,
               guint          line,
               guint          column)
{
	guint l;
	guint c;

	lrg_text_editor_get_cursor (ed, &l, &c);
	g_assert_cmpuint (l, ==, line);
	g_assert_cmpuint (c, ==, column);
}

static void
count_cb (gpointer  instance,
          gpointer  user_data)
{
	(*(guint *) user_data)++;
}

static void
moved_cb (LrgTextEditor *ed,
          guint          line,
          guint          column,
          gpointer       user_data)
{
	(*(guint *) user_data)++;
}

/* ==========================================================================
 * Editor tests
 * ========================================================================== */

static void
test_editor_basics (void)
{
	g_autoptr(LrgTextEditor) ed = lrg_text_editor_new ();
	guint                    changed = 0;

	g_signal_connect (ed, "changed", G_CALLBACK (count_cb), &changed);

	g_assert_cmpuint (lrg_text_editor_get_line_count (ed), ==, 1);
	ASSERT_TEXT (ed, "");

	lrg_text_editor_set_text (ed, "one\r\ntwo\rthree\nfour");
	g_assert_cmpuint (changed, ==, 1);
	g_assert_cmpuint (lrg_text_editor_get_line_count (ed), ==, 4);
	ASSERT_TEXT (ed, "one\ntwo\nthree\nfour");
	g_assert_cmpstr (lrg_text_editor_get_line (ed, 2), ==, "three");
	g_assert_null (lrg_text_editor_get_line (ed, 4));
	assert_cursor (ed, 0, 0);

	/* invalid UTF-8 is repaired */
	lrg_text_editor_set_text (ed, "ok\xff!");
	g_assert_true (g_utf8_validate (lrg_text_editor_get_line (ed, 0), -1, NULL));

	/* properties round-trip and notify */
	g_object_set (ed, "text", "x", "language", "py", "tab-width", 8, "insert-spaces", TRUE,
	              "show-line-numbers", TRUE, "wrap", TRUE, "placeholder", "Type...",
	              "max-lines", 50, "submit-on-enter", TRUE, "font-size", 18.0f, NULL);
	g_assert_cmpstr (lrg_text_editor_get_language (ed), ==, "python");
	g_assert_true (lrg_text_editor_get_syntax (ed) == lrg_syntax_get_for_name ("python"));
	g_assert_cmpuint (lrg_text_editor_get_tab_width (ed), ==, 8);
	g_assert_true (lrg_text_editor_get_insert_spaces (ed));
	g_assert_true (lrg_text_editor_get_show_line_numbers (ed));
	g_assert_true (lrg_text_editor_get_wrap (ed));
	g_assert_cmpstr (lrg_text_editor_get_placeholder (ed), ==, "Type...");
	g_assert_cmpuint (lrg_text_editor_get_max_lines (ed), ==, 50);
	g_assert_true (lrg_text_editor_get_submit_on_enter (ed));
	g_assert_cmpfloat (lrg_text_editor_get_font_size (ed), ==, 18.0f);
	g_assert_cmpstr (lrg_text_editor_get_font (ed), ==, "ui-mono");
	g_assert_true (lrg_text_editor_get_auto_indent (ed));
	g_assert_true (lrg_text_editor_get_highlight_current_line (ed));
	lrg_text_editor_set_language (ed, "no-such-language");
	g_assert_null (lrg_text_editor_get_syntax (ed));
	g_assert_null (lrg_text_editor_get_language (ed));

	/* API insert and append */
	lrg_text_editor_set_text (ed, "ab");
	lrg_text_editor_set_cursor (ed, 0, 1);
	lrg_text_editor_insert_text (ed, "X\nY");
	ASSERT_TEXT (ed, "aX\nYb");
	assert_cursor (ed, 1, 1);
	g_assert_true (lrg_text_editor_can_undo (ed));
	lrg_text_editor_append (ed, "\ntail");
	ASSERT_TEXT (ed, "aX\nYb\ntail");
	assert_cursor (ed, 1, 1);
}

static void
test_editor_typing (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 200.0f);
	guint                changed = 0;
	guint                moved = 0;

	g_signal_connect (ed, "changed", G_CALLBACK (count_cb), &changed);
	g_signal_connect (ed, "cursor-moved", G_CALLBACK (moved_cb), &moved);
	focus (canvas, ed);

	type (canvas, "if (x) {");
	ASSERT_TEXT (ed, "if (x) {");
	g_assert_cmpuint (changed, ==, 8);
	g_assert_cmpuint (moved, >=, 8);

	/* Enter after '{' indents one level (tab by default) */
	key (canvas, GRL_KEY_ENTER);
	type (canvas, "go();");
	ASSERT_TEXT (ed, "if (x) {\n\tgo();");
	key (canvas, GRL_KEY_ENTER);
	ASSERT_TEXT (ed, "if (x) {\n\tgo();\n\t");

	/* Backspace, Delete, joins */
	key (canvas, GRL_KEY_BACKSPACE);
	key (canvas, GRL_KEY_BACKSPACE);
	ASSERT_TEXT (ed, "if (x) {\n\tgo();");
	key_mods (canvas, GRL_KEY_HOME, LRG_INPUT_MODIFIER_CTRL);
	assert_cursor (ed, 0, 0);
	key (canvas, GRL_KEY_DELETE);
	ASSERT_TEXT (ed, "f (x) {\n\tgo();");
	key (canvas, GRL_KEY_END);
	key (canvas, GRL_KEY_DELETE);
	ASSERT_TEXT (ed, "f (x) {\tgo();");

	/* Ctrl+Backspace deletes a word */
	key_mods (canvas, GRL_KEY_END, LRG_INPUT_MODIFIER_CTRL);
	key_mods (canvas, GRL_KEY_BACKSPACE, LRG_INPUT_MODIFIER_CTRL);
	ASSERT_TEXT (ed, "f (x) {\tgo");

	/* Enter between brackets puts the closer on its own line */
	lrg_text_editor_set_text (ed, "{}");
	lrg_text_editor_set_cursor (ed, 0, 1);
	key (canvas, GRL_KEY_ENTER);
	ASSERT_TEXT (ed, "{\n\t\n}");
	assert_cursor (ed, 1, 1);

	/* Ctrl+letters are not text */
	{
		LrgCanvasInput input;

		frame (&input, 0.0f, 0.0f);
		input.modifiers = LRG_INPUT_MODIFIER_CTRL;
		lrg_canvas_input_add_char (&input, 's');
		lrg_canvas_process_input (canvas, &input);
		ASSERT_TEXT (ed, "{\n\t\n}");
	}
}

static void
test_editor_utf8 (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 200.0f);
	guint                i;

	focus (canvas, ed);
	lrg_text_editor_set_text (ed, "aé€😀b");
	for (i = 1; i <= 5; i++)
	{
		key (canvas, GRL_KEY_RIGHT);
		assert_cursor (ed, 0, i);
	}
	key (canvas, GRL_KEY_RIGHT);
	assert_cursor (ed, 0, 5);

	/* Backspace removes the whole 4-byte emoji */
	key (canvas, GRL_KEY_LEFT);
	key (canvas, GRL_KEY_BACKSPACE);
	ASSERT_TEXT (ed, "aé€b");
	assert_cursor (ed, 0, 3);

	/* Shift+Left selects whole characters */
	key_mods (canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_SHIFT);
	key_mods (canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_SHIFT);
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "é€");
	}

	/* Typing multibyte text */
	type (canvas, "ñ日本");
	ASSERT_TEXT (ed, "añ日本b");
	assert_cursor (ed, 0, 4);

	/* Up / Down keep the column across lines of different scripts */
	lrg_text_editor_set_text (ed, "日本語テキスト\nabcdefghij");
	lrg_text_editor_set_cursor (ed, 1, 6);
	key (canvas, GRL_KEY_UP);
	assert_cursor (ed, 0, 3);
	key (canvas, GRL_KEY_DOWN);
	assert_cursor (ed, 1, 6);
}

static void
test_editor_selection (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 200.0f);
	gfloat               lh = lrg_text_editor_get_line_height (ed);
	gfloat               ex = lrg_widget_get_world_x (LRG_WIDGET (ed));
	gfloat               ey = lrg_widget_get_world_y (LRG_WIDGET (ed));
	guint                sl;
	guint                sc;
	guint                el;
	guint                ec;

	focus (canvas, ed);
	lrg_text_editor_set_text (ed, "hello world\nsecond line\nthird");

	/* keyboard */
	key_mods (canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL);
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "hello ");
	}
	key_mods (canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_true (lrg_text_editor_get_selection_bounds (ed, &sl, &sc, &el, &ec));
	g_assert_cmpuint (sl, ==, 0);
	g_assert_cmpuint (sc, ==, 0);
	g_assert_cmpuint (el, ==, 1);
	key (canvas, GRL_KEY_ESCAPE);
	g_assert_false (lrg_text_editor_get_selection_bounds (ed, NULL, NULL, NULL, NULL));

	key_mods (canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "hello world\nsecond line\nthird");
	}

	/* Typing replaces the selection */
	type (canvas, "Z");
	ASSERT_TEXT (ed, "Z");
	lrg_text_editor_set_text (ed, "hello world\nsecond line\nthird");
	settle (canvas, 30);

	/* double click selects a word, triple click the line */
	click_at (canvas, ex + 24.0f, ey + 6.0f + lh * 0.5f);
	click_at (canvas, ex + 24.0f, ey + 6.0f + lh * 0.5f);
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "hello");
	}
	click_at (canvas, ex + 24.0f, ey + 6.0f + lh * 0.5f);
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "hello world\n");
	}
	settle (canvas, 60);

	/* drag from the start of line 0 into line 2 */
	press_at (canvas, ex + 2.0f, ey + 6.0f + lh * 0.5f);
	drag_to (canvas, ex + 60.0f, ey + 6.0f + lh * 1.5f);
	drag_to (canvas, ex + 380.0f, ey + 6.0f + lh * 2.5f);
	release_at (canvas, ex + 380.0f, ey + 6.0f + lh * 2.5f);
	g_assert_true (lrg_text_editor_get_selection_bounds (ed, &sl, &sc, &el, &ec));
	g_assert_cmpuint (sl, ==, 0);
	g_assert_cmpuint (sc, ==, 0);
	g_assert_cmpuint (el, ==, 2);
	g_assert_cmpuint (ec, ==, 5);

	/* Shift+click extends */
	settle (canvas, 60);
	lrg_text_editor_set_cursor (ed, 0, 0);
	{
		LrgCanvasInput input;

		frame (&input, ex + 380.0f, ey + 6.0f + lh * 1.5f);
		input.modifiers = LRG_INPUT_MODIFIER_SHIFT;
		input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
		input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
		lrg_canvas_process_input (canvas, &input);
		release_at (canvas, ex + 380.0f, ey + 6.0f + lh * 1.5f);
	}
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "hello world\nsecond line");
	}

	/* API */
	lrg_text_editor_select_range (ed, 1, 7, 1, 0);
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "second ");
	}
	assert_cursor (ed, 1, 0);
	g_assert_true (lrg_text_editor_delete_selection (ed));
	ASSERT_TEXT (ed, "hello world\nline\nthird");
	g_assert_false (lrg_text_editor_delete_selection (ed));
}

static void
test_editor_undo (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 200.0f);

	focus (canvas, ed);

	/* a typing run is one step; a new word starts a new step */
	type (canvas, "hello");
	g_assert_true (lrg_text_editor_can_undo (ed));
	key_mods (canvas, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL);
	ASSERT_TEXT (ed, "");
	g_assert_false (lrg_text_editor_can_undo (ed));
	g_assert_true (lrg_text_editor_can_redo (ed));
	key_mods (canvas, GRL_KEY_Y, LRG_INPUT_MODIFIER_CTRL);
	ASSERT_TEXT (ed, "hello");
	assert_cursor (ed, 0, 5);

	type (canvas, " world");
	ASSERT_TEXT (ed, "hello world");
	g_assert_true (lrg_text_editor_undo (ed));
	ASSERT_TEXT (ed, "hello");
	g_assert_true (lrg_text_editor_undo (ed));
	ASSERT_TEXT (ed, "");
	key_mods (canvas, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT);
	key_mods (canvas, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_TEXT (ed, "hello world");
	g_assert_false (lrg_text_editor_redo (ed));

	/* moving the caret breaks the run */
	lrg_text_editor_set_text (ed, "");
	type (canvas, "ab");
	key (canvas, GRL_KEY_LEFT);
	key (canvas, GRL_KEY_RIGHT);
	type (canvas, "cd");
	lrg_text_editor_undo (ed);
	ASSERT_TEXT (ed, "ab");

	/* a pause longer than the coalescing window breaks it too */
	lrg_text_editor_set_text (ed, "");
	type (canvas, "ab");
	settle (canvas, 120);
	type (canvas, "cd");
	lrg_text_editor_undo (ed);
	ASSERT_TEXT (ed, "ab");

	/* backspace runs coalesce and restore the caret */
	lrg_text_editor_set_text (ed, "abcdef");
	lrg_text_editor_set_cursor (ed, 0, 6);
	key (canvas, GRL_KEY_BACKSPACE);
	key (canvas, GRL_KEY_BACKSPACE);
	key (canvas, GRL_KEY_BACKSPACE);
	ASSERT_TEXT (ed, "abc");
	lrg_text_editor_undo (ed);
	ASSERT_TEXT (ed, "abcdef");
	assert_cursor (ed, 0, 6);
	g_assert_false (lrg_text_editor_can_undo (ed));

	/* multi-line paste is one step; redo is dropped by a new edit */
	lrg_text_editor_insert_text (ed, "\nx\ny");
	ASSERT_TEXT (ed, "abcdef\nx\ny");
	lrg_text_editor_undo (ed);
	ASSERT_TEXT (ed, "abcdef");
	type (canvas, "!");
	g_assert_false (lrg_text_editor_can_redo (ed));
}

static void
test_editor_indent (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 200.0f);

	focus (canvas, ed);

	/* Tab inserts a tab; with insert-spaces it reaches the next stop */
	key (canvas, GRL_KEY_TAB);
	ASSERT_TEXT (ed, "\t");
	lrg_text_editor_set_text (ed, "ab");
	lrg_text_editor_set_insert_spaces (ed, TRUE);
	lrg_text_editor_set_cursor (ed, 0, 2);
	key (canvas, GRL_KEY_TAB);
	ASSERT_TEXT (ed, "ab  ");
	key (canvas, GRL_KEY_TAB);
	ASSERT_TEXT (ed, "ab      ");

	/* smart backspace in space indentation goes back a whole stop */
	lrg_text_editor_set_text (ed, "        x");
	lrg_text_editor_set_cursor (ed, 0, 8);
	key (canvas, GRL_KEY_BACKSPACE);
	ASSERT_TEXT (ed, "    x");

	/* a multi-line selection indents / outdents every touched line */
	lrg_text_editor_set_text (ed, "a\nb\nc\nd");
	lrg_text_editor_select_range (ed, 0, 0, 2, 1);
	key (canvas, GRL_KEY_TAB);
	ASSERT_TEXT (ed, "    a\n    b\n    c\nd");
	{
		g_autofree gchar *sel = lrg_text_editor_get_selected_text (ed);

		g_assert_cmpstr (sel, ==, "    a\n    b\n    c");
	}
	key_mods (canvas, GRL_KEY_TAB, LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_TEXT (ed, "a\nb\nc\nd");
	lrg_text_editor_undo (ed);
	ASSERT_TEXT (ed, "    a\n    b\n    c\nd");
	lrg_text_editor_undo (ed);
	ASSERT_TEXT (ed, "a\nb\nc\nd");

	/* outdent of a single line, tabs too */
	lrg_text_editor_set_text (ed, "\t\tdeep");
	lrg_text_editor_set_cursor (ed, 0, 3);
	lrg_text_editor_outdent (ed);
	ASSERT_TEXT (ed, "\tdeep");
	assert_cursor (ed, 0, 2);
	lrg_text_editor_indent (ed);
	ASSERT_TEXT (ed, "    \tdeep");

	/* auto-indent keeps the indentation, ':' adds a level */
	lrg_text_editor_set_text (ed, "    def f():");
	lrg_text_editor_set_cursor (ed, 0, 12);
	key (canvas, GRL_KEY_ENTER);
	ASSERT_TEXT (ed, "    def f():\n        ");
	lrg_text_editor_set_auto_indent (ed, FALSE);
	key (canvas, GRL_KEY_ENTER);
	ASSERT_TEXT (ed, "    def f():\n        \n");
}

static void
on_submit (LrgTextEditor *ed,
           gpointer       user_data)
{
	GPtrArray *log = user_data;

	g_ptr_array_add (log, lrg_text_editor_get_text (ed));
	lrg_text_editor_set_text (ed, "");
}

static void
test_editor_submit (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 80.0f);
	g_autoptr(GPtrArray) log = g_ptr_array_new_with_free_func (g_free);

	g_signal_connect (ed, "submit", G_CALLBACK (on_submit), log);
	lrg_text_editor_set_submit_on_enter (ed, TRUE);
	lrg_text_editor_set_placeholder (ed, "Say something");
	focus (canvas, ed);

	type (canvas, "hi");
	key_mods (canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_SHIFT);
	type (canvas, "there");
	ASSERT_TEXT (ed, "hi\nthere");
	key (canvas, GRL_KEY_ENTER);
	g_assert_cmpuint (log->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (log, 0), ==, "hi\nthere");
	ASSERT_TEXT (ed, "");

	/* without the mode Enter is a newline and Ctrl+Enter submits */
	lrg_text_editor_set_submit_on_enter (ed, FALSE);
	type (canvas, "a");
	key (canvas, GRL_KEY_ENTER);
	ASSERT_TEXT (ed, "a\n");
	key_mods (canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (log->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (log, 1), ==, "a\n");
}

static void
test_editor_max_lines (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 120.0f);
	guint                i;
	gfloat               y0;
	gfloat               y1;

	lrg_text_editor_set_read_only (ed, TRUE);
	lrg_text_editor_set_max_lines (ed, 20);

	/* a console: appended lines follow the end, the oldest drop out */
	lrg_text_editor_set_text (ed, "line 0");
	lrg_text_editor_set_cursor (ed, 0, 6);
	for (i = 1; i < 100; i++)
	{
		g_autofree gchar *s = g_strdup_printf ("\nline %u", i);

		lrg_text_editor_append (ed, s);
		lrg_canvas_update (canvas, DT);
	}
	g_assert_cmpuint (lrg_text_editor_get_line_count (ed), ==, 20);
	g_assert_cmpstr (lrg_text_editor_get_line (ed, 0), ==, "line 80");
	g_assert_cmpstr (lrg_text_editor_get_line (ed, 19), ==, "line 99");
	assert_cursor (ed, 19, 7);
	g_assert_false (lrg_text_editor_can_undo (ed));
	settle (canvas, 60);
	lrg_text_editor_get_scroll (ed, NULL, &y0);
	g_assert_cmpfloat (y0, >, 0.0f);

	/* with the caret elsewhere, the view stays put */
	lrg_text_editor_set_cursor (ed, 0, 0);
	settle (canvas, 60);
	lrg_text_editor_get_scroll (ed, NULL, &y0);
	lrg_text_editor_append (ed, "\nmore");
	settle (canvas, 60);
	lrg_text_editor_get_scroll (ed, NULL, &y1);
	g_assert_cmpfloat (fabsf (y1 - y0), <=, lrg_text_editor_get_line_height (ed) + 0.5f);
	g_assert_cmpstr (lrg_text_editor_get_line (ed, 19), ==, "more");

	/* lowering the limit trims right away */
	lrg_text_editor_set_max_lines (ed, 5);
	g_assert_cmpuint (lrg_text_editor_get_line_count (ed), ==, 5);
	g_assert_cmpstr (lrg_text_editor_get_line (ed, 4), ==, "more");
}

static void
test_editor_find (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 200.0f);
	guint                sl;
	guint                sc;
	guint                el;
	guint                ec;
	LrgPaintList        *list;
	guint                plain;

	lrg_text_editor_set_text (ed, "foo bar\nbar FOO foo\nnone\nfoo");
	list = lrg_canvas_record (canvas);
	plain = lrg_paint_list_get_n_instances (list);

	g_assert_true (lrg_text_editor_find (ed, "foo", TRUE));
	g_assert_cmpuint (lrg_text_editor_get_match_count (ed), ==, 3);
	g_assert_true (lrg_text_editor_get_selection_bounds (ed, &sl, &sc, &el, &ec));
	g_assert_cmpuint (sl, ==, 0);
	g_assert_cmpuint (sc, ==, 0);
	g_assert_cmpuint (ec, ==, 3);

	/* every match gets a highlight */
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >=, plain + 3);

	g_assert_true (lrg_text_editor_find_next (ed));
	lrg_text_editor_get_selection_bounds (ed, &sl, &sc, NULL, NULL);
	g_assert_cmpuint (sl, ==, 1);
	g_assert_cmpuint (sc, ==, 8);
	g_assert_true (lrg_text_editor_find_next (ed));
	lrg_text_editor_get_selection_bounds (ed, &sl, &sc, NULL, NULL);
	g_assert_cmpuint (sl, ==, 3);
	g_assert_true (lrg_text_editor_find_next (ed));
	lrg_text_editor_get_selection_bounds (ed, &sl, &sc, NULL, NULL);
	g_assert_cmpuint (sl, ==, 0);
	g_assert_true (lrg_text_editor_find_previous (ed));
	lrg_text_editor_get_selection_bounds (ed, &sl, &sc, NULL, NULL);
	g_assert_cmpuint (sl, ==, 3);
	g_assert_true (lrg_text_editor_find_previous (ed));
	lrg_text_editor_get_selection_bounds (ed, &sl, &sc, NULL, NULL);
	g_assert_cmpuint (sl, ==, 1);
	g_assert_cmpuint (sc, ==, 8);

	/* case-insensitive finds FOO too */
	g_assert_true (lrg_text_editor_find (ed, "foo", FALSE));
	g_assert_cmpuint (lrg_text_editor_get_match_count (ed), ==, 4);

	g_assert_false (lrg_text_editor_find (ed, "zzz", TRUE));
	g_assert_cmpuint (lrg_text_editor_get_match_count (ed), ==, 0);
	g_assert_false (lrg_text_editor_find (ed, NULL, TRUE));
	g_assert_false (lrg_text_editor_find_next (ed));
}

static void
test_editor_wrap (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 160.0f, 300.0f);
	gfloat               x;
	guint                line;
	guint                col;

	lrg_text_editor_set_text (ed, "the quick brown fox jumps over the lazy dog again and again\nshort");

	/* no wrap: one row per line, horizontal scrolling */
	g_assert_cmpuint (lrg_text_editor_get_row_count (ed), ==, 2);
	focus (canvas, ed);
	lrg_text_editor_set_cursor (ed, 0, 0);
	key (canvas, GRL_KEY_END);
	settle (canvas, 60);
	lrg_text_editor_get_scroll (ed, &x, NULL);
	g_assert_cmpfloat (x, >, 0.0f);

	/* wrap: the long line takes several rows, no horizontal scroll */
	lrg_text_editor_set_wrap (ed, TRUE);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_text_editor_get_row_count (ed), >, 3);
	settle (canvas, 60);
	lrg_text_editor_get_scroll (ed, &x, NULL);
	g_assert_cmpfloat (x, ==, 0.0f);

	/* rows break after spaces: Down moves within the same line */
	lrg_text_editor_set_cursor (ed, 0, 0);
	key (canvas, GRL_KEY_DOWN);
	lrg_text_editor_get_cursor (ed, &line, &col);
	g_assert_cmpuint (line, ==, 0);
	g_assert_cmpuint (col, >, 4);
	{
		/* the row starts at a word start */
		const gchar *t = lrg_text_editor_get_line (ed, 0);
		const gchar *p = g_utf8_offset_to_pointer (t, col);

		g_assert_true (p == t || p[-1] == ' ' || p[0] == ' ');
	}

	/* End stays on the row; Home returns to its start */
	key (canvas, GRL_KEY_END);
	lrg_text_editor_get_cursor (ed, &line, &col);
	g_assert_cmpuint (line, ==, 0);
	key (canvas, GRL_KEY_HOME);
	lrg_text_editor_get_cursor (ed, &line, &col);
	g_assert_cmpuint (col, >, 0);

	/* narrower: more rows; editing re-wraps */
	lrg_widget_allocate (LRG_WIDGET (ed), 10.0f, 10.0f, 120.0f, 300.0f);
	{
		guint before = lrg_text_editor_get_row_count (ed);

		lrg_text_editor_set_text (ed, "short\nshort");
		g_assert_cmpuint (lrg_text_editor_get_row_count (ed), ==, 2);
		g_assert_cmpuint (before, >, 2);
	}

	/* a single word longer than the row breaks mid-word */
	lrg_text_editor_set_text (ed, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
	g_assert_cmpuint (lrg_text_editor_get_row_count (ed), >, 1);
}

static void
test_editor_scroll (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 300.0f, 200.0f);
	GString             *s = g_string_new (NULL);
	gfloat               lh = lrg_text_editor_get_line_height (ed);
	gfloat               y;
	gfloat               y2;
	guint                i;
	guint                line;

	for (i = 0; i < 200; i++)
		g_string_append_printf (s, "%sline %u", i > 0 ? "\n" : "", i);
	lrg_text_editor_set_text (ed, s->str);
	g_string_free (s, TRUE);
	focus (canvas, ed);

	/* Ctrl+End scrolls the caret into view at the bottom */
	key_mods (canvas, GRL_KEY_END, LRG_INPUT_MODIFIER_CTRL);
	settle (canvas, 90);
	lrg_text_editor_get_scroll (ed, NULL, &y);
	g_assert_cmpfloat (y, >=, 199.0f * lh + lh - 200.0f);
	g_assert_cmpfloat (y, <=, 199.0f * lh + 12.0f);

	/* Page Up moves the caret and the view together */
	key (canvas, GRL_KEY_PAGE_UP);
	settle (canvas, 90);
	lrg_text_editor_get_cursor (ed, &line, NULL);
	g_assert_cmpuint (line, <, 199);
	lrg_text_editor_get_scroll (ed, NULL, &y2);
	g_assert_cmpfloat (y2, <, y);
	g_assert_cmpfloat ((gfloat) line * lh + 6.0f, >=, y2);
	g_assert_cmpfloat ((gfloat) (line + 1) * lh + 6.0f, <=, y2 + 200.0f);

	/* the wheel scrolls without moving the caret; typing brings it back */
	{
		LrgCanvasInput input;

		frame (&input, 100.0f, 100.0f);
		input.wheel_y = 30.0f;
		lrg_canvas_process_input (canvas, &input);
	}
	settle (canvas, 90);
	lrg_text_editor_get_scroll (ed, NULL, &y);
	g_assert_cmpfloat (y, <, y2 - 200.0f);
	{
		guint l2;

		lrg_text_editor_get_cursor (ed, &l2, NULL);
		g_assert_cmpuint (l2, ==, line);
	}
	type (canvas, "x");
	settle (canvas, 90);
	lrg_text_editor_get_scroll (ed, NULL, &y);
	g_assert_cmpfloat ((gfloat) line * lh + 6.0f, >=, y);
	g_assert_cmpfloat ((gfloat) (line + 1) * lh + 6.0f, <=, y + 200.0f);

	/* Ctrl+Home back to the top */
	key_mods (canvas, GRL_KEY_HOME, LRG_INPUT_MODIFIER_CTRL);
	settle (canvas, 90);
	lrg_text_editor_get_scroll (ed, NULL, &y);
	g_assert_cmpfloat (y, ==, 0.0f);

	/* scroll_to_line */
	lrg_text_editor_scroll_to_line (ed, 100, FALSE);
	lrg_text_editor_get_scroll (ed, NULL, &y);
	g_assert_cmpfloat (y, ==, 100.0f * lh);

	/* stepping reports motion while gliding, then settles */
	lrg_text_editor_scroll_to_line (ed, 10, TRUE);
	g_assert_true (lrg_canvas_update (canvas, DT));
	settle (canvas, 120);
	g_assert_false (lrg_canvas_update (canvas, DT));
}

/* Only the visible rows are tokenized and painted, even for 10k lines */
static void
test_editor_virtual (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 500.0f, 400.0f);
	GString             *s = g_string_new (NULL);
	gfloat               lh;
	guint                visible;
	guint                count;
	guint                n_small;
	guint                n_big;
	guint                i;

	lrg_text_editor_set_language (ed, "c");
	lrg_text_editor_set_show_line_numbers (ed, TRUE);
	lrg_text_editor_set_text (ed, "int x = 1; /* one */");
	n_small = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));

	for (i = 0; i < 10000; i++)
		g_string_append_printf (s, "%sint value_%u = %u; /* note */", i > 0 ? "\n" : "", i, i);
	lrg_text_editor_set_text (ed, s->str);
	g_string_free (s, TRUE);
	lh = lrg_text_editor_get_line_height (ed);
	visible = (guint) ceilf (400.0f / lh) + 1;

	count = lrg_text_editor_get_tokenize_count (ed);
	n_big = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpuint (lrg_text_editor_get_tokenize_count (ed) - count, <=, visible);
	g_assert_cmpuint (n_big, <=, n_small + visible * 64);

	/* repainting the same view tokenizes nothing */
	count = lrg_text_editor_get_tokenize_count (ed);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_text_editor_get_tokenize_count (ed), ==, count);

	/* jumping to the middle: lines above get state only */
	lrg_text_editor_scroll_to_line (ed, 5000, FALSE);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_text_editor_get_tokenize_count (ed) - count, <=, visible);

	/* an edit that keeps the state re-tokenizes just that line */
	lrg_text_editor_set_cursor (ed, 5002, 0);
	settle (canvas, 2);
	lrg_canvas_record (canvas);
	count = lrg_text_editor_get_tokenize_count (ed);
	lrg_text_editor_insert_text (ed, "x");
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_text_editor_get_tokenize_count (ed) - count, ==, 1);

	/* opening a block comment re-tokenizes the following line too; that
	 * line's own closing comment brings the state back, so the walk
	 * converges there and stops */
	lrg_text_editor_set_cursor (ed, 5002, 1000);
	count = lrg_text_editor_get_tokenize_count (ed);
	lrg_text_editor_insert_text (ed, " /* open");
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_text_editor_get_tokenize_count (ed) - count, ==, 2);
	count = lrg_text_editor_get_tokenize_count (ed);
	lrg_text_editor_undo (ed);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_text_editor_get_tokenize_count (ed) - count, <=, visible);

	/* scrolling with the wheel paints the new rows only */
	settle (canvas, 30);
	g_assert_cmpuint (lrg_text_editor_get_row_count (ed), ==, 10000);
}

static void
test_editor_read_only (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTextEditor       *ed = editor_on (canvas, 400.0f, 200.0f);
	g_autofree gchar    *copied = NULL;

	lrg_text_editor_set_text (ed, "locked text");
	lrg_text_editor_set_read_only (ed, TRUE);
	focus (canvas, ed);

	type (canvas, "abc");
	key (canvas, GRL_KEY_BACKSPACE);
	key (canvas, GRL_KEY_DELETE);
	key (canvas, GRL_KEY_ENTER);
	key (canvas, GRL_KEY_TAB);
	ASSERT_TEXT (ed, "locked text");

	/* selection and copy work; cut and paste do not edit */
	key_mods (canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	key_mods (canvas, GRL_KEY_C, LRG_INPUT_MODIFIER_CTRL);
	key_mods (canvas, GRL_KEY_X, LRG_INPUT_MODIFIER_CTRL);
	key_mods (canvas, GRL_KEY_V, LRG_INPUT_MODIFIER_CTRL);
	key_mods (canvas, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL);
	ASSERT_TEXT (ed, "locked text");

	/* the copied text pastes into a writable editor */
	lrg_text_editor_set_read_only (ed, FALSE);
	lrg_text_editor_set_cursor (ed, 0, 11);
	key_mods (canvas, GRL_KEY_V, LRG_INPUT_MODIFIER_CTRL);
	ASSERT_TEXT (ed, "locked textlocked text");

	/* the program can still change a read-only editor */
	lrg_text_editor_set_read_only (ed, TRUE);
	lrg_text_editor_append (ed, "!");
	ASSERT_TEXT (ed, "locked textlocked text!");
}

/* Keys: plain typing and navigation are consumed, Ctrl shortcuts are not */
static void
test_editor_keys_consumed (void)
{
	g_autoptr(LrgTextEditor) ed = lrg_text_editor_new ();
	static const GrlKey      used[] = {
		GRL_KEY_A, GRL_KEY_S, GRL_KEY_SPACE, GRL_KEY_LEFT, GRL_KEY_RIGHT, GRL_KEY_UP, GRL_KEY_DOWN,
		GRL_KEY_HOME, GRL_KEY_END, GRL_KEY_PAGE_UP, GRL_KEY_PAGE_DOWN, GRL_KEY_ENTER,
		GRL_KEY_BACKSPACE, GRL_KEY_DELETE, GRL_KEY_TAB
	};
	guint i;

	lrg_widget_set_size (LRG_WIDGET (ed), 300.0f, 100.0f);
	lrg_text_editor_set_text (ed, "abc\ndef");
	for (i = 0; i < G_N_ELEMENTS (used); i++)
	{
		g_autoptr(LrgUIEvent) ev = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN, used[i]);

		g_assert_true (lrg_widget_handle_event (LRG_WIDGET (ed), ev));
	}
	{
		g_autoptr(LrgUIEvent) ev = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN, GRL_KEY_S);

		lrg_ui_event_set_modifiers (ev, LRG_INPUT_MODIFIER_CTRL);
		g_assert_false (lrg_widget_handle_event (LRG_WIDGET (ed), ev));
	}
	{
		g_autoptr(LrgUIEvent) ev = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN, GRL_KEY_F5);

		g_assert_false (lrg_widget_handle_event (LRG_WIDGET (ed), ev));
	}
	{
		g_autoptr(LrgUIEvent) ev = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN, GRL_KEY_ESCAPE);

		/* nothing selected: Escape is left to the canvas */
		g_assert_false (lrg_widget_handle_event (LRG_WIDGET (ed), ev));
	}
}

/* Brackets, placeholder, gutter and caret paint in every preset */
static void
test_editor_presets (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	g_autoptr(LrgTheme)  theme = lrg_theme_new_preset ("midnight");
	LrgTextEditor       *ed;
	LrgTextEditor       *empty;
	guint                i;
	guint                plain;
	guint                n;

	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	ed = editor_on (canvas, 300.0f, 200.0f);
	empty = lrg_text_editor_new ();
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (empty));
	g_object_unref (empty);
	lrg_widget_allocate (LRG_WIDGET (empty), 400.0f, 10.0f, 200.0f, 60.0f);

	lrg_text_editor_set_language (ed, "c");
	lrg_text_editor_set_show_line_numbers (ed, TRUE);
	lrg_text_editor_set_text (ed, "int main (void)\n{\n\treturn (1 + 2);\n}");
	focus (canvas, ed);
	lrg_text_editor_set_cursor (ed, 2, 3);
	settle (canvas, 30);
	plain = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));

	/* next to '(' the bracket pair is outlined */
	lrg_text_editor_set_cursor (ed, 2, 8);
	settle (canvas, 30);
	n = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpuint (n, >=, plain + 2);

	/* placeholder shows only while empty */
	plain = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	lrg_text_editor_set_placeholder (empty, "Message");
	n = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpuint (n, >=, plain);

	lrg_text_editor_select_range (ed, 0, 2, 2, 3);
	for (i = 0; presets[i] != NULL; i++)
	{
		lrg_theme_apply_preset (theme, presets[i]);
		settle (canvas, 10);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (canvas)), >, 0);
	}

	/* the caret blink does not keep the canvas busy */
	settle (canvas, 120);
	g_assert_false (lrg_canvas_update (canvas, DT));
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (320)
#define GPU_H (200)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-ui-text-editor-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static Image
render_canvas (LrgCanvas *canvas)
{
	RenderTexture2D target = LoadRenderTexture (GPU_W, GPU_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	lrg_canvas_render (canvas);
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

static gboolean
pixel_near (Image          *image,
            gint            x,
            gint            y,
            const GrlColor *c,
            gint            tol)
{
	Color p = GetImageColor (*image, x, y);

	return ABS ((gint) p.r - (gint) c->r) <= tol &&
	       ABS ((gint) p.g - (gint) c->g) <= tol &&
	       ABS ((gint) p.b - (gint) c->b) <= tol;
}

/* count_unlike: pixels in a rectangle that differ from @bg */
static guint
count_unlike (Image          *image,
              gint            x0,
              gint            y0,
              gint            x1,
              gint            y1,
              const GrlColor *bg)
{
	guint n = 0;
	gint  x;
	gint  y;

	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++)
			if (!pixel_near (image, x, y, bg, 24))
				n++;
	return n;
}

/*
 * GPU: in every preset the gutter shows its tint, highlighted code
 * puts glyph pixels on the INPUT well, and a 10k-line document records
 * no more text than a 30-line one.
 */
static void
test_gpu_editor (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas) canvas = NULL;
	g_autoptr(LrgTheme)  theme = NULL;
	LrgTextEditor       *ed;
	GString             *s;
	guint                i;
	guint                small;
	guint                big;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	ed = lrg_text_editor_new ();
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (ed));
	g_object_unref (ed);
	lrg_widget_allocate (LRG_WIDGET (ed), 0.0f, 0.0f, GPU_W, GPU_H);
	lrg_text_editor_set_language (ed, "c");
	lrg_text_editor_set_show_line_numbers (ed, TRUE);
	lrg_text_editor_set_highlight_current_line (ed, FALSE);
	lrg_text_editor_set_text (ed, "#include <glib.h>\nstatic gint count = 42; /* answer */\nreturn \"text\";");
	settle (canvas, 5);

	for (i = 0; presets[i] != NULL; i++)
	{
		const GrlColor *input;
		GrlColor        gutter;
		Image           img;

		lrg_theme_apply_preset (theme, presets[i]);
		settle (canvas, 5);
		input = lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT);
		lrg_paint_color_mix (input, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 0.6f, &gutter);
		img = render_canvas (canvas);

		/* gutter tint under the numbers' column, below the text */
		g_assert_true (pixel_near (&img, 6, GPU_H - 20, &gutter, 4));
		/* empty text area is the INPUT well */
		g_assert_true (pixel_near (&img, GPU_W - 30, GPU_H - 20, input, 4));
		/* the first rows have glyph pixels */
		g_assert_cmpuint (count_unlike (&img, 50, 4, GPU_W - 20, 60, input), >, 50);
		UnloadImage (img);
	}

	/* virtualised: 10k lines record about as much as 30 */
	s = g_string_new (NULL);
	for (i = 0; i < 30; i++)
		g_string_append_printf (s, "%sint value_%u = %u; /* note */", i > 0 ? "\n" : "", i, i);
	lrg_text_editor_set_text (ed, s->str);
	small = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	for (; i < 10000; i++)
		g_string_append_printf (s, "\nint value_%u = %u; /* note */", i, i);
	lrg_text_editor_set_text (ed, s->str);
	g_string_free (s, TRUE);
	big = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpuint (small, >, 100);
	g_assert_cmpuint (big, <=, small + small / 4);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/syntax/registry", test_syntax_registry);
	g_test_add_func ("/ui/syntax/c", test_syntax_c);
	g_test_add_func ("/ui/syntax/multiline", test_syntax_multiline);
	g_test_add_func ("/ui/syntax/languages", test_syntax_languages);
	g_test_add_func ("/ui/syntax/yaml-load", test_syntax_yaml_load);
	g_test_add_func ("/ui/syntax/yaml-bad", test_syntax_yaml_bad);
	g_test_add_func ("/ui/syntax/colors", test_syntax_colors);
	g_test_add_func ("/ui/text-editor/basics", test_editor_basics);
	g_test_add_func ("/ui/text-editor/typing", test_editor_typing);
	g_test_add_func ("/ui/text-editor/utf8", test_editor_utf8);
	g_test_add_func ("/ui/text-editor/selection", test_editor_selection);
	g_test_add_func ("/ui/text-editor/undo", test_editor_undo);
	g_test_add_func ("/ui/text-editor/indent", test_editor_indent);
	g_test_add_func ("/ui/text-editor/submit", test_editor_submit);
	g_test_add_func ("/ui/text-editor/max-lines", test_editor_max_lines);
	g_test_add_func ("/ui/text-editor/find", test_editor_find);
	g_test_add_func ("/ui/text-editor/wrap", test_editor_wrap);
	g_test_add_func ("/ui/text-editor/scroll", test_editor_scroll);
	g_test_add_func ("/ui/text-editor/virtual", test_editor_virtual);
	g_test_add_func ("/ui/text-editor/read-only", test_editor_read_only);
	g_test_add_func ("/ui/text-editor/keys-consumed", test_editor_keys_consumed);
	g_test_add_func ("/ui/text-editor/presets", test_editor_presets);
	g_test_add_func ("/ui/gpu/text-editor", test_gpu_editor);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
