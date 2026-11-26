/* lrg-glsl.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The platform GLSL dialect.  The header texts live in
 * lrg-glsl-private.h (LRG_GLSL_HEADER_DESKTOP / _ES); this file picks
 * one at run time and splices it in front of a shader body.
 */

#include "config.h"

#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-glsl.h"
#include "graphics/lrg-glsl-private.h"

/* the dialect lrg_glsl_set_dialect() chose; LRG_GLSL_DIALECT_AUTO means
 * the build's own.  An int so g_atomic_int_* can guard it. */
static gint glsl_dialect = LRG_GLSL_DIALECT_AUTO;

LrgGlslDialect
lrg_glsl_get_dialect (void)
{
	LrgGlslDialect dialect = (LrgGlslDialect)g_atomic_int_get (&glsl_dialect);

	if (dialect == LRG_GLSL_DIALECT_AUTO)
		return LRG_GLSL_BUILD_IS_ES ? LRG_GLSL_DIALECT_GLSL_ES_300 : LRG_GLSL_DIALECT_GLSL_330;
	return dialect;
}

void
lrg_glsl_set_dialect (LrgGlslDialect dialect)
{
	g_return_if_fail (dialect == LRG_GLSL_DIALECT_AUTO ||
	                  dialect == LRG_GLSL_DIALECT_GLSL_330 ||
	                  dialect == LRG_GLSL_DIALECT_GLSL_ES_300);

	g_atomic_int_set (&glsl_dialect, (gint)dialect);
}

gboolean
lrg_glsl_is_es (void)
{
	return lrg_glsl_get_dialect () == LRG_GLSL_DIALECT_GLSL_ES_300;
}

const gchar *
lrg_glsl_get_header (void)
{
	return lrg_glsl_is_es () ? LRG_GLSL_HEADER_ES : LRG_GLSL_HEADER_DESKTOP;
}

guint
lrg_glsl_get_header_lines (void)
{
	return lrg_glsl_is_es () ? (guint)LRG_GLSL_HEADER_ES_LINES : (guint)LRG_GLSL_HEADER_DESKTOP_LINES;
}

/*
 * find_version:
 * Where the leading `#version` directive of @body is: skips whitespace,
 * `//` comments and `/ * * /` comments (the only things GLSL allows before
 * it), then accepts `#`, optional blanks, `version` and a blank or the
 * end of the line.  On a match *@out_end is the directive's end (its
 * newline or the string's end, exclusive).
 *
 * Returns: the directive's first character, or NULL when @body does not
 *   start with one (an unterminated block comment counts as no match)
 */
static const gchar *
find_version (const gchar  *body,
              const gchar **out_end)
{
	const gchar *p = body;
	const gchar *q;

	for (;;)
	{
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v')
			p++;
		if (p[0] == '/' && p[1] == '/')
		{
			while (*p != '\0' && *p != '\n')
				p++;
			continue;
		}
		if (p[0] == '/' && p[1] == '*')
		{
			q = strstr (p + 2, "*/");
			if (q == NULL)
				return NULL;
			p = q + 2;
			continue;
		}
		break;
	}

	if (*p != '#')
		return NULL;
	q = p + 1;
	while (*q == ' ' || *q == '\t')
		q++;
	if (strncmp (q, "version", 7) != 0)
		return NULL;
	q += 7;
	if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r' && *q != '\0')
		return NULL;
	while (*q != '\0' && *q != '\n')
		q++;
	*out_end = q;
	return p;
}

gchar *
lrg_glsl_prepare_source (const gchar *body,
                         const gchar *defines)
{
	GString *text;
	const gchar *version;
	const gchar *version_end = NULL;
	const gchar *header;
	gsize header_len;
	gsize len;

	if (body == NULL)
		body = "";
	header = lrg_glsl_get_header ();
	header_len = strlen (header);

	/* 1. the dialect's header and the caller's defines, each ending in a
	 *    newline so the next part starts a line */
	text = g_string_new (header);
	if (defines != NULL && defines[0] != '\0')
	{
		g_string_append (text, defines);
		len = strlen (defines);
		if (defines[len - 1] != '\n')
			g_string_append_c (text, '\n');
	}

	/* 2. a body that already starts with this dialect's header (a source
	 *    prepared before, or a built-in literal of this build) keeps it:
	 *    the defines went after it, the rest follows unchanged */
	if (strncmp (body, header, header_len) == 0)
	{
		g_string_append (text, body + header_len);
		return g_string_free (text, FALSE);
	}

	/* 3. otherwise the body, minus the text of a leading #version
	 *    directive (its newline stays, so the body keeps its line numbers) */
	version = find_version (body, &version_end);
	if (version != NULL)
	{
		g_string_append_len (text, body, (gssize)(version - body));
		g_string_append (text, version_end);
	}
	else
	{
		g_string_append (text, body);
	}

	return g_string_free (text, FALSE);
}

Shader
lrg_glsl_load_shader (const gchar *vs,
                      const gchar *fs)
{
	g_autofree gchar *vs_text = NULL;
	g_autofree gchar *fs_text = NULL;

	/* NULL stays NULL: raylib substitutes its default stage */
	if (vs != NULL)
		vs_text = lrg_glsl_prepare_source (vs, NULL);
	if (fs != NULL)
		fs_text = lrg_glsl_prepare_source (fs, NULL);
	return LoadShaderFromMemory (vs_text, fs_text);
}

GrlShader *
lrg_glsl_shader_new_from_memory (const gchar  *vs_code,
                                 const gchar  *fs_code,
                                 GError      **error)
{
	g_autofree gchar *vs_text = NULL;
	g_autofree gchar *fs_text = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (vs_code != NULL)
		vs_text = lrg_glsl_prepare_source (vs_code, NULL);
	if (fs_code != NULL)
		fs_text = lrg_glsl_prepare_source (fs_code, NULL);
	return grl_shader_new_from_memory (vs_text, fs_text, error);
}
