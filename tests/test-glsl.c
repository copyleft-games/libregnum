/* test-glsl.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless unit tests for the GLSL dialect helper (lrg-glsl.h): the
 * header of each dialect (forced with lrg_glsl_set_dialect(), so the ES
 * text is checked on a desktop build), the build default, where defines
 * go, replacing an existing #version line (after whitespace and
 * comments, kept in place so line numbers hold), what is not replaced,
 * NULL and empty inputs, and the dialect enum.  No GL context needed.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

#define ES_HEADER \
	"#version 300 es\n" \
	"precision highp float;\n" \
	"precision highp int;\n" \
	"precision highp sampler2D;\n" \
	"precision highp samplerCube;\n" \
	"precision highp sampler3D;\n" \
	"precision highp sampler2DShadow;\n" \
	"precision highp sampler2DArray;\n"

#define DESKTOP_HEADER "#version 330\n"

/* every test starts and ends on the build's own dialect */
static void
reset_dialect (void)
{
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_AUTO);
}

/* how many '\n' @text holds */
static guint
count_newlines (const gchar *text)
{
	guint n = 0;

	for (; *text != '\0'; text++)
		if (*text == '\n')
			n++;
	return n;
}

/* lrg_glsl_prepare_source (@body, @defines) must be @header + @rest */
static void
assert_prepared (const gchar *body,
                 const gchar *defines,
                 const gchar *rest)
{
	g_autofree gchar *got = lrg_glsl_prepare_source (body, defines);
	g_autofree gchar *want = g_strconcat (lrg_glsl_get_header (), rest, NULL);

	g_assert_cmpstr (got, ==, want);
}

static void
test_glsl_build_default (void)
{
	LrgGlslDialect dialect;

	reset_dialect ();
	dialect = lrg_glsl_get_dialect ();
	g_assert_cmpint (dialect, !=, LRG_GLSL_DIALECT_AUTO);

#if defined(LRG_PLATFORM_ANDROID)
	g_assert_cmpint (dialect, ==, LRG_GLSL_DIALECT_GLSL_ES_300);
	g_assert_true (lrg_glsl_is_es ());
	g_assert_cmpstr (lrg_glsl_get_header (), ==, ES_HEADER);
#else
	g_assert_cmpint (dialect, ==, LRG_GLSL_DIALECT_GLSL_330);
	g_assert_false (lrg_glsl_is_es ());
	g_assert_cmpstr (lrg_glsl_get_header (), ==, DESKTOP_HEADER);
#endif
}

static void
test_glsl_header_es (void)
{
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_ES_300);

	g_assert_cmpint (lrg_glsl_get_dialect (), ==, LRG_GLSL_DIALECT_GLSL_ES_300);
	g_assert_true (lrg_glsl_is_es ());
	g_assert_cmpstr (lrg_glsl_get_header (), ==, ES_HEADER);
	g_assert_cmpuint (lrg_glsl_get_header_lines (), ==, 8);
	g_assert_cmpuint (count_newlines (lrg_glsl_get_header ()), ==, lrg_glsl_get_header_lines ());

	reset_dialect ();
}

static void
test_glsl_header_desktop (void)
{
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_330);

	g_assert_cmpint (lrg_glsl_get_dialect (), ==, LRG_GLSL_DIALECT_GLSL_330);
	g_assert_false (lrg_glsl_is_es ());
	g_assert_cmpstr (lrg_glsl_get_header (), ==, DESKTOP_HEADER);
	g_assert_cmpuint (lrg_glsl_get_header_lines (), ==, 1);

	reset_dialect ();
}

static void
test_glsl_auto_restores (void)
{
	LrgGlslDialect build;

	reset_dialect ();
	build = lrg_glsl_get_dialect ();

	lrg_glsl_set_dialect (build == LRG_GLSL_DIALECT_GLSL_330 ? LRG_GLSL_DIALECT_GLSL_ES_300
	                                                         : LRG_GLSL_DIALECT_GLSL_330);
	g_assert_cmpint (lrg_glsl_get_dialect (), !=, build);
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_AUTO);
	g_assert_cmpint (lrg_glsl_get_dialect (), ==, build);
}

static void
test_glsl_null_and_empty (void)
{
	reset_dialect ();

	assert_prepared (NULL, NULL, "");
	assert_prepared ("", NULL, "");
	assert_prepared ("", "", "");
	assert_prepared (NULL, "#define A 1\n", "#define A 1\n");
}

static void
test_glsl_defines_order (void)
{
	g_autofree gchar *got = NULL;
	const gchar *header;
	const gchar *def;
	const gchar *body;

	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_ES_300);

	/* header, then defines, then body */
	assert_prepared ("void main() {}\n", "#define A 1\n#define B 2\n",
	                 "#define A 1\n#define B 2\nvoid main() {}\n");

	/* a define block without a final newline gets one */
	assert_prepared ("void main() {}\n", "#define A 1", "#define A 1\nvoid main() {}\n");

	/* the defines come after the precision lines, not between them */
	got = lrg_glsl_prepare_source ("void main() {}\n", "#define LIT 1\n");
	header = strstr (got, "precision highp sampler2DArray;\n");
	def = strstr (got, "#define LIT 1\n");
	body = strstr (got, "void main");
	g_assert_nonnull (header);
	g_assert_nonnull (def);
	g_assert_nonnull (body);
	g_assert_true (header < def);
	g_assert_true (def < body);

	reset_dialect ();
}

static void
test_glsl_replace_version (void)
{
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_ES_300);

	/* the legacy line is blanked, its newline kept */
	assert_prepared ("#version 330\nvoid main() {}\n", NULL, "\nvoid main() {}\n");
	/* with a profile, and as the whole body */
	assert_prepared ("#version 330 core\nvoid main() {}\n", NULL, "\nvoid main() {}\n");
	assert_prepared ("#version 330", NULL, "");
	/* an ES body on a desktop dialect is replaced the same way */
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_330);
	assert_prepared ("#version 300 es\nprecision mediump float;\nvoid main() {}\n", NULL,
	                 "\nprecision mediump float;\nvoid main() {}\n");
	/* blanks between # and version */
	assert_prepared ("#  version 330\nX\n", NULL, "\nX\n");
	assert_prepared ("#\tversion\t330\r\nX\n", NULL, "\nX\n");

	reset_dialect ();
}

static void
test_glsl_replace_after_whitespace_and_comments (void)
{
	reset_dialect ();

	/* leading whitespace stays, the directive goes */
	assert_prepared ("\n  \n\t#version 330\nX\n", NULL, "\n  \n\t\nX\n");
	/* line and block comments before it stay */
	assert_prepared ("// legacy shader\n/* a\n   block */ #version 330\nX\n", NULL,
	                 "// legacy shader\n/* a\n   block */ \nX\n");
	/* a directive followed by a comment on its line: the whole line goes */
	assert_prepared ("#version 330 // desktop\nX\n", NULL, "\nX\n");
}

static void
test_glsl_line_numbers_kept (void)
{
	const gchar *body = "// c\n#version 330\nline3\nline4\n";
	const gchar *defines = "#define A 1\n#define B 1\n";
	g_autofree gchar *got = NULL;
	g_auto(GStrv) lines = NULL;
	guint offset;

	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_ES_300);

	got = lrg_glsl_prepare_source (body, defines);
	lines = g_strsplit (got, "\n", -1);
	offset = lrg_glsl_get_header_lines () + 2;

	/* body line n (1-based) is result line offset + n */
	g_assert_cmpstr (lines[offset + 1 - 1], ==, "// c");
	g_assert_cmpstr (lines[offset + 2 - 1], ==, "");
	g_assert_cmpstr (lines[offset + 3 - 1], ==, "line3");
	g_assert_cmpstr (lines[offset + 4 - 1], ==, "line4");

	reset_dialect ();
}

static void
test_glsl_not_replaced (void)
{
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_ES_300);

	/* a #version after code is not the leading directive */
	assert_prepared ("float f;\n#version 330\n", NULL, "float f;\n#version 330\n");
	/* a different directive or a longer identifier */
	assert_prepared ("#versionx 330\n", NULL, "#versionx 330\n");
	assert_prepared ("#define version 330\n", NULL, "#define version 330\n");
	/* inside an unterminated comment */
	assert_prepared ("/* #version 330\n", NULL, "/* #version 330\n");
	/* inside a line comment */
	assert_prepared ("// #version 330\nX\n", NULL, "// #version 330\nX\n");
	/* only the first directive is replaced */
	assert_prepared ("#version 330\n#version 330\n", NULL, "\n#version 330\n");

	reset_dialect ();
}

static void
test_glsl_already_prepared (void)
{
	g_autofree gchar *once = NULL;
	g_autofree gchar *twice = NULL;
	g_autofree gchar *with_defines = NULL;
	g_autofree gchar *want = NULL;

	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_ES_300);

	/* preparing twice changes nothing */
	once = lrg_glsl_prepare_source ("#version 330\nvoid main() {}\n", NULL);
	twice = lrg_glsl_prepare_source (once, NULL);
	g_assert_cmpstr (once, ==, twice);

	/* defines go after the header the body already has */
	with_defines = lrg_glsl_prepare_source (ES_HEADER "void main() {}\n", "#define A 1");
	g_assert_cmpstr (with_defines, ==, ES_HEADER "#define A 1\nvoid main() {}\n");

	/* the other dialect's header is not this one: only #version is replaced */
	lrg_glsl_set_dialect (LRG_GLSL_DIALECT_GLSL_330);
	want = g_strconcat (DESKTOP_HEADER, "\n", ES_HEADER + strlen ("#version 300 es\n"),
	                    "void main() {}\n", NULL);
	g_free (with_defines);
	with_defines = lrg_glsl_prepare_source (ES_HEADER "void main() {}\n", NULL);
	g_assert_cmpstr (with_defines, ==, want);

	reset_dialect ();
}

static void
test_glsl_dialect_enum (void)
{
	GEnumClass *klass = g_type_class_ref (LRG_TYPE_GLSL_DIALECT);

	g_assert_cmpint (g_enum_get_value_by_nick (klass, "auto")->value, ==, LRG_GLSL_DIALECT_AUTO);
	g_assert_cmpint (g_enum_get_value_by_nick (klass, "glsl-330")->value, ==,
	                 LRG_GLSL_DIALECT_GLSL_330);
	g_assert_cmpint (g_enum_get_value_by_nick (klass, "glsl-es-300")->value, ==,
	                 LRG_GLSL_DIALECT_GLSL_ES_300);
	g_type_class_unref (klass);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/glsl/build-default", test_glsl_build_default);
	g_test_add_func ("/glsl/header-es", test_glsl_header_es);
	g_test_add_func ("/glsl/header-desktop", test_glsl_header_desktop);
	g_test_add_func ("/glsl/auto-restores", test_glsl_auto_restores);
	g_test_add_func ("/glsl/null-and-empty", test_glsl_null_and_empty);
	g_test_add_func ("/glsl/defines-order", test_glsl_defines_order);
	g_test_add_func ("/glsl/replace-version", test_glsl_replace_version);
	g_test_add_func ("/glsl/replace-after-whitespace-and-comments",
	                 test_glsl_replace_after_whitespace_and_comments);
	g_test_add_func ("/glsl/line-numbers-kept", test_glsl_line_numbers_kept);
	g_test_add_func ("/glsl/not-replaced", test_glsl_not_replaced);
	g_test_add_func ("/glsl/already-prepared", test_glsl_already_prepared);
	g_test_add_func ("/glsl/dialect-enum", test_glsl_dialect_enum);

	return g_test_run ();
}
