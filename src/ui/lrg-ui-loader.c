/* lrg-ui-loader.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUILoader: Builds widget trees from YAML documents with components and hot reload.
 *
 * The loader works in three stages:
 *
 *  1. PARSE: libyaml's document API turns the text into a tree of #Lv
 *     values (null, scalar, sequence, mapping) that remember their file,
 *     line and column, so every later error can point at the source.
 *
 *  2. EXPAND: the #Lv tree is turned into a tree of #UiNode, one per
 *     widget to build. Components are instantiated, `${...}` expressions
 *     evaluated, `repeat` / `for-each` / `include` / `slot` / `if`
 *     resolved, type names looked up and every value converted to the
 *     #GValue (or look, handler list, ...) it will be applied as. All
 *     validation happens here, so building never fails and a hot reload
 *     that fails leaves the live widgets untouched.
 *
 *  3. BUILD or SYNC: a #UiNode tree is either instantiated, or diffed
 *     against a live widget tree whose widgets remember (in qdata) the
 *     node they were built from. Matching widgets are reused and only
 *     the values whose YAML changed are applied, so state that the YAML
 *     does not mention (typed text, scroll offsets, slider values, ...)
 *     survives.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <errno.h>
#include <yaml.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include "lrg-ui-loader.h"
#include "lrg-widget.h"
#include "lrg-container.h"
#include "lrg-button.h"
#include "lrg-checkbox.h"
#include "lrg-combo-box.h"
#include "lrg-dialog.h"
#include "lrg-grid.h"
#include "lrg-hbox.h"
#include "lrg-image.h"
#include "lrg-label.h"
#include "lrg-list-view.h"
#include "lrg-overlay.h"
#include "lrg-panel.h"
#include "lrg-progress-bar.h"
#include "lrg-scroll-box.h"
#include "lrg-separator.h"
#include "lrg-slider.h"
#include "lrg-spin-box.h"
#include "lrg-tab-view.h"
#include "lrg-text-input.h"
#include "lrg-vbox.h"
#include "lrg-menu.h"
#include "lrg-menu-bar.h"
#include "lrg-anchor-layout.h"
#include "lrg-switcher.h"
#include "lrg-size-box.h"
#include "lrg-viewport.h"
#include "lrg-custom-widget.h"
#include "lrg-text-editor.h"
#include "lrg-syntax.h"
#include "lrg-node-graph.h"
#include "lrg-timeline-view.h"
#include "lrg-dock-area.h"
#include "lrg-block-editor.h"
#include "lrg-file-browser.h"
#include "lrg-header-bar.h"
#include "lrg-pagination.h"
#include "lrg-canvas.h"
#include "lrg-ui-actions.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"

/* Upper bounds that keep a hostile or mistaken document from running away */
#define UI_MAX_DEPTH        (200)
#define UI_MAX_REPEAT       (10000)

/* ==========================================================================
 * Errors
 * ========================================================================== */

G_DEFINE_QUARK (lrg-ui-loader-error-quark, lrg_ui_loader_error)

GType
lrg_ui_loader_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_UI_LOADER_ERROR_PARSE, "LRG_UI_LOADER_ERROR_PARSE", "parse" },
			{ LRG_UI_LOADER_ERROR_IO, "LRG_UI_LOADER_ERROR_IO", "io" },
			{ LRG_UI_LOADER_ERROR_UNKNOWN_TYPE, "LRG_UI_LOADER_ERROR_UNKNOWN_TYPE", "unknown-type" },
			{ LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY, "LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY", "unknown-property" },
			{ LRG_UI_LOADER_ERROR_INVALID_VALUE, "LRG_UI_LOADER_ERROR_INVALID_VALUE", "invalid-value" },
			{ LRG_UI_LOADER_ERROR_EXPRESSION, "LRG_UI_LOADER_ERROR_EXPRESSION", "expression" },
			{ LRG_UI_LOADER_ERROR_HANDLER, "LRG_UI_LOADER_ERROR_HANDLER", "handler" },
			{ LRG_UI_LOADER_ERROR_COMPONENT, "LRG_UI_LOADER_ERROR_COMPONENT", "component" },
			{ LRG_UI_LOADER_ERROR_INCLUDE_CYCLE, "LRG_UI_LOADER_ERROR_INCLUDE_CYCLE", "include-cycle" },
			{ LRG_UI_LOADER_ERROR_STRUCTURE, "LRG_UI_LOADER_ERROR_STRUCTURE", "structure" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = g_enum_register_static (g_intern_static_string ("LrgUILoaderError"), values);
		g_once_init_leave (&type_id, id);
	}

	return (GType)type_id;
}

/* ==========================================================================
 * Lv: a parsed YAML value with its source position
 * ========================================================================== */

typedef enum
{
	LV_NULL,
	LV_SCALAR,
	LV_SEQ,
	LV_MAP
} LvKind;

typedef struct _Lv Lv;

struct _Lv
{
	gint         ref_count;
	LvKind       kind;
	gchar       *str;       /* LV_SCALAR: the text */
	gboolean     quoted;    /* LV_SCALAR: quoted or block scalar, always a string */
	GPtrArray   *keys;      /* LV_MAP: gchar *, in document order */
	GPtrArray   *items;     /* LV_SEQ: elements; LV_MAP: values matching keys */
	const gchar *file;      /* interned file name, or NULL */
	guint        line;      /* 1-based, 0 when unknown */
	guint        column;    /* 1-based */
};

static void lv_unref (Lv *lv);

/*
 * lv_new:
 * Creates a value of @kind that reports the source position of @at
 * (which may be %NULL for synthetic values).
 */
static Lv *
lv_new (LvKind    kind,
        const Lv *at)
{
	Lv *lv;

	lv = g_new0 (Lv, 1);
	lv->ref_count = 1;
	lv->kind = kind;
	if (kind == LV_SEQ || kind == LV_MAP)
		lv->items = g_ptr_array_new_with_free_func ((GDestroyNotify)lv_unref);
	if (kind == LV_MAP)
		lv->keys = g_ptr_array_new_with_free_func (g_free);
	if (at != NULL)
	{
		lv->file = at->file;
		lv->line = at->line;
		lv->column = at->column;
	}
	return lv;
}

static Lv *
lv_new_scalar (const gchar *text,
               gboolean     quoted,
               const Lv    *at)
{
	Lv *lv;

	lv = lv_new (LV_SCALAR, at);
	lv->str = g_strdup (text != NULL ? text : "");
	lv->quoted = quoted;
	return lv;
}

static Lv *
lv_ref (Lv *lv)
{
	g_atomic_int_inc (&lv->ref_count);
	return lv;
}

static void
lv_unref (Lv *lv)
{
	if (lv == NULL)
		return;
	if (!g_atomic_int_dec_and_test (&lv->ref_count))
		return;
	g_free (lv->str);
	if (lv->items != NULL)
		g_ptr_array_unref (lv->items);
	if (lv->keys != NULL)
		g_ptr_array_unref (lv->keys);
	g_free (lv);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Lv, lv_unref)

/*
 * key_equal:
 * Keys compare with '-' and '_' treated as the same character, so
 * `fill_weight` and `fill-weight` name the same thing.
 */
static gboolean
key_equal (const gchar *a,
           const gchar *b)
{
	for (; *a != '\0' && *b != '\0'; a++, b++)
	{
		gchar ca;
		gchar cb;

		ca = (*a == '_') ? '-' : *a;
		cb = (*b == '_') ? '-' : *b;
		if (ca != cb)
			return FALSE;
	}
	return *a == *b;
}

/* Returns the value under @key of a mapping, or NULL */
static Lv *
lv_map_get (const Lv    *map,
            const gchar *key)
{
	guint i;

	if (map == NULL || map->kind != LV_MAP)
		return NULL;
	for (i = 0; i < map->keys->len; i++)
	{
		if (key_equal ((const gchar *)g_ptr_array_index (map->keys, i), key))
			return (Lv *)g_ptr_array_index (map->items, i);
	}
	return NULL;
}

static void
lv_map_add (Lv          *map,
            const gchar *key,
            Lv          *value)
{
	g_ptr_array_add (map->keys, g_strdup (key));
	g_ptr_array_add (map->items, value);
}

/*
 * lv_equal:
 * Deep structural equality, ignoring source positions. Used by the hot
 * reload diff to decide whether a value changed.
 */
static gboolean
lv_equal (const Lv *a,
          const Lv *b)
{
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL || a->kind != b->kind)
		return FALSE;

	switch (a->kind)
	{
	case LV_NULL:
		return TRUE;
	case LV_SCALAR:
		return g_strcmp0 (a->str, b->str) == 0;
	case LV_SEQ:
	case LV_MAP:
		if (a->items->len != b->items->len)
			return FALSE;
		for (i = 0; i < a->items->len; i++)
		{
			if (a->kind == LV_MAP &&
			    g_strcmp0 (g_ptr_array_index (a->keys, i), g_ptr_array_index (b->keys, i)) != 0)
				return FALSE;
			if (!lv_equal (g_ptr_array_index (a->items, i), g_ptr_array_index (b->items, i)))
				return FALSE;
		}
		return TRUE;
	default:
		return FALSE;
	}
}

static const gchar *
lv_kind_name (const Lv *lv)
{
	if (lv == NULL)
		return "nothing";
	switch (lv->kind)
	{
	case LV_NULL:
		return "null";
	case LV_SCALAR:
		return "a scalar";
	case LV_SEQ:
		return "a list";
	case LV_MAP:
		return "a mapping";
	default:
		return "?";
	}
}

/* ==========================================================================
 * Error formatting
 * ========================================================================== */

/*
 * set_error:
 * Formats "<path>: <message> (at <file>:<line>:<column>)". The path is
 * the node path inside the document ("children[2].children[0]"), or
 * "(root)" for the root node. The position comes from @at when known.
 */
static void set_error (GError      **error,
                       gint          code,
                       const gchar  *path,
                       const Lv     *at,
                       const gchar  *format,
                       ...) G_GNUC_PRINTF (5, 6);

static void
set_error (GError      **error,
           gint          code,
           const gchar  *path,
           const Lv     *at,
           const gchar  *format,
           ...)
{
	g_autofree gchar *msg = NULL;
	g_autofree gchar *where = NULL;
	va_list args;

	if (error == NULL)
		return;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);

	if (at != NULL && at->line > 0)
	{
		if (at->file != NULL)
			where = g_strdup_printf (" (at %s:%u:%u)", at->file, at->line, at->column);
		else
			where = g_strdup_printf (" (at line %u, column %u)", at->line, at->column);
	}

	g_set_error (error, LRG_UI_LOADER_ERROR, code, "%s: %s%s",
	             (path != NULL && *path != '\0') ? path : "(root)",
	             msg, where != NULL ? where : "");
}

/* Appends a segment to a node path: "children[2]" + ".tabs[0]" */
static gchar *path_join (const gchar *path,
                         const gchar *format,
                         ...) G_GNUC_PRINTF (2, 3);

static gchar *
path_join (const gchar *path,
           const gchar *format,
           ...)
{
	g_autofree gchar *seg = NULL;
	va_list args;

	va_start (args, format);
	seg = g_strdup_vprintf (format, args);
	va_end (args);

	if (path == NULL || *path == '\0')
		return g_steal_pointer (&seg);
	if (seg[0] == '<' || seg[0] == '{' || seg[0] == '[')
		return g_strconcat (path, seg, NULL);
	return g_strconcat (path, ".", seg, NULL);
}

/* ==========================================================================
 * PARSE: libyaml document -> Lv tree
 * ========================================================================== */

static gboolean
is_null_word (const gchar *s)
{
	return *s == '\0' || g_strcmp0 (s, "~") == 0 || g_strcmp0 (s, "null") == 0 ||
	       g_strcmp0 (s, "Null") == 0 || g_strcmp0 (s, "NULL") == 0;
}

static Lv *
lv_from_yaml (yaml_document_t  *doc,
              yaml_node_t      *yn,
              const gchar      *file,
              guint             depth,
              GError          **error)
{
	Lv tmp;
	Lv *lv;

	memset (&tmp, 0, sizeof (tmp));
	tmp.file = file;
	tmp.line = (guint)yn->start_mark.line + 1;
	tmp.column = (guint)yn->start_mark.column + 1;

	/* Aliases share nodes in libyaml, so a recursive alias would never end */
	if (depth > UI_MAX_DEPTH)
	{
		set_error (error, LRG_UI_LOADER_ERROR_PARSE, "(document)", &tmp,
		           "nesting is too deep (recursive alias?)");
		return NULL;
	}

	switch (yn->type)
	{
	case YAML_SCALAR_NODE:
		{
			g_autofree gchar *text = NULL;
			gboolean quoted;

			text = g_strndup ((const gchar *)yn->data.scalar.value, yn->data.scalar.length);
			quoted = yn->data.scalar.style != YAML_PLAIN_SCALAR_STYLE &&
			         yn->data.scalar.style != YAML_ANY_SCALAR_STYLE;
			if (!quoted && is_null_word (text))
				return lv_new (LV_NULL, &tmp);
			return lv_new_scalar (text, quoted, &tmp);
		}

	case YAML_SEQUENCE_NODE:
		{
			yaml_node_item_t *item;

			lv = lv_new (LV_SEQ, &tmp);
			for (item = yn->data.sequence.items.start; item < yn->data.sequence.items.top; item++)
			{
				yaml_node_t *child;
				Lv *c;

				child = yaml_document_get_node (doc, *item);
				if (child == NULL)
					continue;
				c = lv_from_yaml (doc, child, file, depth + 1, error);
				if (c == NULL)
				{
					lv_unref (lv);
					return NULL;
				}
				g_ptr_array_add (lv->items, c);
			}
			return lv;
		}

	case YAML_MAPPING_NODE:
		{
			yaml_node_pair_t *pair;

			lv = lv_new (LV_MAP, &tmp);
			for (pair = yn->data.mapping.pairs.start; pair < yn->data.mapping.pairs.top; pair++)
			{
				yaml_node_t *kn;
				yaml_node_t *vn;
				g_autofree gchar *key = NULL;
				Lv *v;

				kn = yaml_document_get_node (doc, pair->key);
				vn = yaml_document_get_node (doc, pair->value);
				if (kn == NULL || vn == NULL)
					continue;
				if (kn->type != YAML_SCALAR_NODE)
				{
					Lv kt;

					memset (&kt, 0, sizeof (kt));
					kt.file = file;
					kt.line = (guint)kn->start_mark.line + 1;
					kt.column = (guint)kn->start_mark.column + 1;
					set_error (error, LRG_UI_LOADER_ERROR_PARSE, "(document)", &kt,
					           "mapping keys must be plain text");
					lv_unref (lv);
					return NULL;
				}
				key = g_strndup ((const gchar *)kn->data.scalar.value, kn->data.scalar.length);
				if (lv_map_get (lv, key) != NULL)
				{
					Lv kt;

					memset (&kt, 0, sizeof (kt));
					kt.file = file;
					kt.line = (guint)kn->start_mark.line + 1;
					kt.column = (guint)kn->start_mark.column + 1;
					set_error (error, LRG_UI_LOADER_ERROR_PARSE, "(document)", &kt,
					           "duplicate key '%s'", key);
					lv_unref (lv);
					return NULL;
				}
				v = lv_from_yaml (doc, vn, file, depth + 1, error);
				if (v == NULL)
				{
					lv_unref (lv);
					return NULL;
				}
				lv_map_add (lv, key, v);
			}
			return lv;
		}

	case YAML_NO_NODE:
	default:
		return lv_new (LV_NULL, &tmp);
	}
}

/*
 * parse_yaml:
 * Parses the first document of @data. @file (interned, nullable) is
 * recorded in every value for error messages.
 */
static Lv *
parse_yaml (const gchar  *data,
            gsize         length,
            const gchar  *file,
            GError      **error)
{
	yaml_parser_t parser;
	yaml_document_t doc;
	yaml_node_t *root;
	Lv *lv;

	if (!yaml_parser_initialize (&parser))
	{
		g_set_error (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_PARSE,
		             "(document): cannot initialise the YAML parser");
		return NULL;
	}
	yaml_parser_set_input_string (&parser, (const unsigned char *)data, length);

	if (!yaml_parser_load (&parser, &doc))
	{
		Lv at;

		memset (&at, 0, sizeof (at));
		at.file = file;
		at.line = (guint)parser.problem_mark.line + 1;
		at.column = (guint)parser.problem_mark.column + 1;
		set_error (error, LRG_UI_LOADER_ERROR_PARSE, "(document)", &at,
		           "YAML syntax error: %s%s%s",
		           parser.problem != NULL ? parser.problem : "unknown error",
		           parser.context != NULL ? " " : "",
		           parser.context != NULL ? parser.context : "");
		yaml_parser_delete (&parser);
		return NULL;
	}

	root = yaml_document_get_root_node (&doc);
	if (root == NULL)
	{
		g_set_error (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_STRUCTURE,
		             "(document): the document is empty%s%s",
		             file != NULL ? " in " : "", file != NULL ? file : "");
		yaml_document_delete (&doc);
		yaml_parser_delete (&parser);
		return NULL;
	}

	lv = lv_from_yaml (&doc, root, file, 0, error);
	yaml_document_delete (&doc);
	yaml_parser_delete (&parser);
	return lv;
}

/* ==========================================================================
 * Expression values
 * ========================================================================== */

typedef enum
{
	EV_NULL,
	EV_BOOL,
	EV_NUM,
	EV_STR,
	EV_NODE     /* a list or mapping (an Lv) */
} EvKind;

typedef struct
{
	EvKind   kind;
	gboolean b;
	gdouble  n;
	gchar   *s;
	Lv      *node;
} EVal;

static void
ev_clear (EVal *v)
{
	g_clear_pointer (&v->s, g_free);
	g_clear_pointer (&v->node, lv_unref);
	v->kind = EV_NULL;
	v->b = FALSE;
	v->n = 0.0;
}

static void
ev_set_num (EVal    *v,
            gdouble  n)
{
	ev_clear (v);
	v->kind = EV_NUM;
	v->n = n;
}

static void
ev_set_bool (EVal     *v,
             gboolean  b)
{
	ev_clear (v);
	v->kind = EV_BOOL;
	v->b = b;
}

static void
ev_take_str (EVal  *v,
             gchar *s)
{
	ev_clear (v);
	v->kind = EV_STR;
	v->s = s;
}

static void
ev_copy (EVal       *dst,
         const EVal *src)
{
	ev_clear (dst);
	dst->kind = src->kind;
	dst->b = src->b;
	dst->n = src->n;
	dst->s = g_strdup (src->s);
	dst->node = src->node != NULL ? lv_ref (src->node) : NULL;
}

static void
ev_free (gpointer data)
{
	EVal *v = data;

	ev_clear (v);
	g_free (v);
}

/* Parses a whole string as a number ("12", "-3.5", "1e3", "0x1F") */
static gboolean
parse_number_text (const gchar *s,
                   gdouble     *out)
{
	gchar *end;
	const gchar *p;
	gdouble v;

	if (s == NULL)
		return FALSE;
	while (g_ascii_isspace (*s))
		s++;
	if (*s == '\0')
		return FALSE;

	p = s;
	if (*p == '-' || *p == '+')
		p++;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
	{
		guint64 u;

		errno = 0;
		u = g_ascii_strtoull (p + 2, &end, 16);
		if (errno != 0 || end == p + 2)
			return FALSE;
		v = (gdouble)u;
		if (*s == '-')
			v = -v;
	}
	else
	{
		errno = 0;
		v = g_ascii_strtod (s, &end);
		if (errno != 0 || end == s)
			return FALSE;
	}
	while (g_ascii_isspace (*end))
		end++;
	if (*end != '\0' || !isfinite (v))
		return FALSE;
	*out = v;
	return TRUE;
}

/* Formats a number the way YAML would write it: integers without a dot */
static gchar *
num_to_string (gdouble n)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	if (n == floor (n) && fabs (n) < 1e15)
		return g_strdup_printf ("%" G_GINT64_FORMAT, (gint64)n);
	g_ascii_formatd (buf, sizeof (buf), "%.15g", n);
	return g_strdup (buf);
}

/*
 * ev_from_lv:
 * Types a YAML value for expressions: a plain scalar that reads as a
 * number, true/false or null gets that type; quoted scalars stay text;
 * lists and mappings are carried as nodes.
 */
static void
ev_from_lv (EVal *out,
            Lv   *lv)
{
	gdouble n;

	ev_clear (out);
	if (lv == NULL || lv->kind == LV_NULL)
		return;
	if (lv->kind == LV_SEQ || lv->kind == LV_MAP)
	{
		out->kind = EV_NODE;
		out->node = lv_ref (lv);
		return;
	}
	if (!lv->quoted)
	{
		if (g_strcmp0 (lv->str, "true") == 0 || g_strcmp0 (lv->str, "True") == 0)
		{
			ev_set_bool (out, TRUE);
			return;
		}
		if (g_strcmp0 (lv->str, "false") == 0 || g_strcmp0 (lv->str, "False") == 0)
		{
			ev_set_bool (out, FALSE);
			return;
		}
		if (parse_number_text (lv->str, &n))
		{
			ev_set_num (out, n);
			return;
		}
	}
	ev_take_str (out, g_strdup (lv->str));
}

/* Turns an expression result back into a YAML value */
static Lv *
lv_from_ev (const EVal *v,
            const Lv   *at)
{
	g_autofree gchar *s = NULL;

	switch (v->kind)
	{
	case EV_BOOL:
		return lv_new_scalar (v->b ? "true" : "false", FALSE, at);
	case EV_NUM:
		s = num_to_string (v->n);
		return lv_new_scalar (s, FALSE, at);
	case EV_STR:
		return lv_new_scalar (v->s, TRUE, at);
	case EV_NODE:
		return lv_ref (v->node);
	case EV_NULL:
	default:
		return lv_new (LV_NULL, at);
	}
}

/* Text form of a value; NULL for lists and mappings */
static gchar *
ev_to_string (const EVal *v)
{
	switch (v->kind)
	{
	case EV_NULL:
		return g_strdup ("");
	case EV_BOOL:
		return g_strdup (v->b ? "true" : "false");
	case EV_NUM:
		return num_to_string (v->n);
	case EV_STR:
		return g_strdup (v->s);
	case EV_NODE:
	default:
		return NULL;
	}
}

static gboolean
ev_truthy (const EVal *v)
{
	switch (v->kind)
	{
	case EV_BOOL:
		return v->b;
	case EV_NUM:
		return v->n != 0.0;
	case EV_STR:
		return v->s != NULL && *v->s != '\0' && g_strcmp0 (v->s, "false") != 0;
	case EV_NODE:
		return v->node->items->len > 0;
	case EV_NULL:
	default:
		return FALSE;
	}
}

static gboolean
ev_to_number (const EVal *v,
              gdouble    *out)
{
	switch (v->kind)
	{
	case EV_NUM:
		*out = v->n;
		return TRUE;
	case EV_BOOL:
		*out = v->b ? 1.0 : 0.0;
		return TRUE;
	case EV_STR:
		return parse_number_text (v->s, out);
	default:
		return FALSE;
	}
}

static const gchar *
ev_kind_name (const EVal *v)
{
	switch (v->kind)
	{
	case EV_NULL:
		return "null";
	case EV_BOOL:
		return "a boolean";
	case EV_NUM:
		return "a number";
	case EV_STR:
		return "text";
	case EV_NODE:
		return v->node->kind == LV_SEQ ? "a list" : "a mapping";
	default:
		return "?";
	}
}

/* ==========================================================================
 * Scopes (variables visible to expressions)
 * ========================================================================== */

typedef struct _Scope Scope;

struct _Scope
{
	GHashTable *vars;    /* gchar * -> EVal * */
	Scope      *parent;
};

static Scope *
scope_new (Scope *parent)
{
	Scope *s;

	s = g_new0 (Scope, 1);
	s->vars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, ev_free);
	s->parent = parent;
	return s;
}

static void
scope_free (Scope *s)
{
	if (s == NULL)
		return;
	g_hash_table_unref (s->vars);
	g_free (s);
}

/*
 * scope_set:
 * Defines @name in @s. Names with '-' are also defined with '_' so
 * `label-width` can be written `label_width` inside expressions, where '-'
 * is subtraction.
 */
static void
scope_set (Scope       *s,
           const gchar *name,
           const EVal  *v)
{
	EVal *copy;

	copy = g_new0 (EVal, 1);
	ev_copy (copy, v);
	g_hash_table_insert (s->vars, g_strdup (name), copy);

	if (strchr (name, '-') != NULL)
	{
		g_autofree gchar *alias = NULL;

		alias = g_strdelimit (g_strdup (name), "-", '_');
		copy = g_new0 (EVal, 1);
		ev_copy (copy, v);
		g_hash_table_insert (s->vars, g_steal_pointer (&alias), copy);
	}
}

static const EVal *
scope_lookup (Scope       *s,
              const gchar *name)
{
	for (; s != NULL; s = s->parent)
	{
		const EVal *v;

		v = g_hash_table_lookup (s->vars, name);
		if (v != NULL)
			return v;
	}
	return NULL;
}

/* ==========================================================================
 * Expression evaluator
 *
 * Grammar (lowest to highest precedence):
 *
 *   expr    := or ( '?' expr ':' expr )?
 *   or      := and ( '||' and )*
 *   and     := eq ( '&&' eq )*
 *   eq      := cmp ( ( '==' | '!=' ) cmp )*
 *   cmp     := add ( ( '<' | '<=' | '>' | '>=' ) add )*
 *   add     := mul ( ( '+' | '-' ) mul )*
 *   mul     := unary ( ( '*' | '/' | '%' ) unary )*
 *   unary   := ( '!' | '-' | '+' ) unary | postfix
 *   postfix := primary ( '.' ident | '[' expr ']' )*
 *   primary := number | 'text' | "text" | true | false | null
 *            | ident | ident '(' args ')' | '(' expr ')' | '[' args ']'
 *
 * While `skip` is positive the parser only checks syntax (the untaken
 * side of ?:, && and ||), so `x != null && x.name` never fails.
 * ========================================================================== */

typedef struct
{
	const gchar *p;
	Scope       *scope;
	gint         skip;      /* > 0: parse without evaluating */
	gint         lenient;   /* > 0: unknown names are null (first argument of default()) */
	gchar       *err;       /* first error */
} Expr;

static gboolean ex_ternary (Expr *e, EVal *out);

static gboolean ex_fail (Expr        *e,
                         const gchar *format,
                         ...) G_GNUC_PRINTF (2, 3);

static gboolean
ex_fail (Expr        *e,
         const gchar *format,
         ...)
{
	va_list args;

	if (e->err == NULL)
	{
		va_start (args, format);
		e->err = g_strdup_vprintf (format, args);
		va_end (args);
	}
	return FALSE;
}

static void
ex_ws (Expr *e)
{
	while (g_ascii_isspace (*e->p))
		e->p++;
}

static gboolean
ex_is_ident_start (gchar c)
{
	return g_ascii_isalpha (c) || c == '_';
}

static gboolean
ex_is_ident_char (gchar c)
{
	return g_ascii_isalnum (c) || c == '_';
}

static gchar *
ex_ident (Expr *e)
{
	const gchar *start;

	start = e->p;
	while (ex_is_ident_char (*e->p))
		e->p++;
	return g_strndup (start, (gsize)(e->p - start));
}

/* Numeric operand or an error naming the operator */
static gboolean
ex_num_operand (Expr        *e,
                const EVal  *v,
                const gchar *op,
                gdouble     *out)
{
	if (ev_to_number (v, out))
		return TRUE;
	return ex_fail (e, "operator '%s' needs numbers, got %s", op, ev_kind_name (v));
}

/*
 * ex_compare:
 * Three-way comparison: numerically when both sides read as numbers,
 * otherwise as text. Returns FALSE for lists and mappings.
 */
static gboolean
ex_compare (const EVal *a,
            const EVal *b,
            gint       *out)
{
	gdouble na;
	gdouble nb;
	g_autofree gchar *sa = NULL;
	g_autofree gchar *sb = NULL;

	if (a->kind != EV_STR && b->kind != EV_STR &&
	    ev_to_number (a, &na) && ev_to_number (b, &nb))
	{
		*out = (na < nb) ? -1 : (na > nb) ? 1 : 0;
		return TRUE;
	}
	if ((a->kind == EV_NUM || b->kind == EV_NUM) &&
	    ev_to_number (a, &na) && ev_to_number (b, &nb))
	{
		*out = (na < nb) ? -1 : (na > nb) ? 1 : 0;
		return TRUE;
	}
	sa = ev_to_string (a);
	sb = ev_to_string (b);
	if (sa == NULL || sb == NULL)
		return FALSE;
	*out = g_strcmp0 (sa, sb);
	*out = (*out < 0) ? -1 : (*out > 0) ? 1 : 0;
	return TRUE;
}

static gboolean
ex_equal (const EVal *a,
          const EVal *b)
{
	gint c;

	if (a->kind == EV_NULL || b->kind == EV_NULL)
		return a->kind == b->kind;
	if (a->kind == EV_NODE || b->kind == EV_NODE)
		return a->kind == b->kind && lv_equal (a->node, b->node);
	if (a->kind == EV_BOOL && b->kind == EV_BOOL)
		return a->b == b->b;
	if (!ex_compare (a, b, &c))
		return FALSE;
	return c == 0;
}

/* Length of text (in characters), list or mapping */
static gboolean
ex_len (Expr       *e,
        const EVal *v,
        gdouble    *out)
{
	switch (v->kind)
	{
	case EV_NODE:
		*out = (gdouble)v->node->items->len;
		return TRUE;
	case EV_NULL:
		*out = 0.0;
		return TRUE;
	default:
		{
			g_autofree gchar *s = NULL;

			s = ev_to_string (v);
			*out = (gdouble)g_utf8_strlen (s, -1);
			return TRUE;
		}
	}
}

/*
 * ex_call:
 * Built-in functions. @args holds the evaluated arguments.
 */
static gboolean
ex_call (Expr        *e,
         const gchar *name,
         GArray      *args,
         EVal        *out)
{
	EVal *a;
	guint n;
	gdouble x;
	gdouble y;
	guint i;

	n = args->len;
	a = (EVal *)(gpointer)args->data;

	if (g_strcmp0 (name, "min") == 0 || g_strcmp0 (name, "max") == 0)
	{
		gboolean is_min;

		is_min = name[1] == 'i';
		if (n == 0)
			return ex_fail (e, "%s() needs at least one argument", name);
		if (!ex_num_operand (e, &a[0], name, &x))
			return FALSE;
		for (i = 1; i < n; i++)
		{
			if (!ex_num_operand (e, &a[i], name, &y))
				return FALSE;
			x = is_min ? MIN (x, y) : MAX (x, y);
		}
		ev_set_num (out, x);
		return TRUE;
	}
	if (g_strcmp0 (name, "len") == 0)
	{
		if (n != 1)
			return ex_fail (e, "len() takes one argument");
		if (!ex_len (e, &a[0], &x))
			return FALSE;
		ev_set_num (out, x);
		return TRUE;
	}
	if (g_strcmp0 (name, "default") == 0)
	{
		if (n != 2)
			return ex_fail (e, "default() takes two arguments");
		if (a[0].kind == EV_NULL || (a[0].kind == EV_STR && *a[0].s == '\0'))
			ev_copy (out, &a[1]);
		else
			ev_copy (out, &a[0]);
		return TRUE;
	}
	if (g_strcmp0 (name, "empty") == 0)
	{
		if (n != 1)
			return ex_fail (e, "empty() takes one argument");
		if (!ex_len (e, &a[0], &x))
			return FALSE;
		ev_set_bool (out, x == 0.0);
		return TRUE;
	}
	if (g_strcmp0 (name, "upper") == 0 || g_strcmp0 (name, "lower") == 0 ||
	    g_strcmp0 (name, "str") == 0)
	{
		g_autofree gchar *s = NULL;

		if (n != 1)
			return ex_fail (e, "%s() takes one argument", name);
		s = ev_to_string (&a[0]);
		if (s == NULL)
			return ex_fail (e, "%s() needs text, got %s", name, ev_kind_name (&a[0]));
		if (name[0] == 'u')
			ev_take_str (out, g_utf8_strup (s, -1));
		else if (name[0] == 'l')
			ev_take_str (out, g_utf8_strdown (s, -1));
		else
			ev_take_str (out, g_steal_pointer (&s));
		return TRUE;
	}
	if (g_strcmp0 (name, "int") == 0 || g_strcmp0 (name, "float") == 0 ||
	    g_strcmp0 (name, "round") == 0 || g_strcmp0 (name, "floor") == 0 ||
	    g_strcmp0 (name, "ceil") == 0 || g_strcmp0 (name, "abs") == 0)
	{
		if (n != 1)
			return ex_fail (e, "%s() takes one argument", name);
		if (!ev_to_number (&a[0], &x))
			return ex_fail (e, "%s() needs a number, got %s", name, ev_kind_name (&a[0]));
		if (g_strcmp0 (name, "int") == 0)
			x = trunc (x);
		else if (g_strcmp0 (name, "round") == 0)
			x = round (x);
		else if (g_strcmp0 (name, "floor") == 0)
			x = floor (x);
		else if (g_strcmp0 (name, "ceil") == 0)
			x = ceil (x);
		else if (g_strcmp0 (name, "abs") == 0)
			x = fabs (x);
		ev_set_num (out, x);
		return TRUE;
	}
	return ex_fail (e, "unknown function '%s'", name);
}

static gboolean
ex_primary (Expr *e,
            EVal *out)
{
	ex_ws (e);

	/* ( expr ) */
	if (*e->p == '(')
	{
		e->p++;
		if (!ex_ternary (e, out))
			return FALSE;
		ex_ws (e);
		if (*e->p != ')')
			return ex_fail (e, "expected ')'");
		e->p++;
		return TRUE;
	}

	/* [ a, b, ... ] list literal */
	if (*e->p == '[')
	{
		Lv *list;

		e->p++;
		list = lv_new (LV_SEQ, NULL);
		ex_ws (e);
		while (*e->p != ']')
		{
			EVal item = { 0 };

			if (!ex_ternary (e, &item))
			{
				lv_unref (list);
				return FALSE;
			}
			g_ptr_array_add (list->items, lv_from_ev (&item, NULL));
			ev_clear (&item);
			ex_ws (e);
			if (*e->p == ',')
			{
				e->p++;
				ex_ws (e);
				continue;
			}
			if (*e->p != ']')
			{
				lv_unref (list);
				return ex_fail (e, "expected ',' or ']'");
			}
		}
		e->p++;
		ev_clear (out);
		out->kind = EV_NODE;
		out->node = list;
		return TRUE;
	}

	/* 'text' or "text" */
	if (*e->p == '\'' || *e->p == '"')
	{
		gchar quote;
		GString *s;

		quote = *e->p++;
		s = g_string_new (NULL);
		while (*e->p != quote)
		{
			if (*e->p == '\0')
			{
				g_string_free (s, TRUE);
				return ex_fail (e, "unterminated string");
			}
			if (*e->p == '\\' && e->p[1] != '\0')
			{
				e->p++;
				switch (*e->p)
				{
				case 'n':
					g_string_append_c (s, '\n');
					break;
				case 't':
					g_string_append_c (s, '\t');
					break;
				default:
					g_string_append_c (s, *e->p);
					break;
				}
				e->p++;
				continue;
			}
			g_string_append_c (s, *e->p++);
		}
		e->p++;
		ev_take_str (out, g_string_free (s, FALSE));
		return TRUE;
	}

	/* number */
	if (g_ascii_isdigit (*e->p) || (*e->p == '.' && g_ascii_isdigit (e->p[1])))
	{
		gchar *end;
		gdouble v;

		if (e->p[0] == '0' && (e->p[1] == 'x' || e->p[1] == 'X'))
			v = (gdouble)g_ascii_strtoull (e->p + 2, &end, 16);
		else
			v = g_ascii_strtod (e->p, &end);
		if (end == e->p)
			return ex_fail (e, "bad number");
		e->p = end;
		ev_set_num (out, v);
		return TRUE;
	}

	/* identifier, keyword or function call */
	if (ex_is_ident_start (*e->p))
	{
		g_autofree gchar *name = NULL;
		const EVal *v;

		name = ex_ident (e);
		if (g_strcmp0 (name, "true") == 0 || g_strcmp0 (name, "false") == 0)
		{
			ev_set_bool (out, name[0] == 't');
			return TRUE;
		}
		if (g_strcmp0 (name, "null") == 0)
		{
			ev_clear (out);
			return TRUE;
		}

		ex_ws (e);
		if (*e->p == '(')
		{
			g_autoptr(GArray) args = NULL;
			gboolean ok;
			guint i;

			e->p++;
			args = g_array_new (FALSE, TRUE, sizeof (EVal));
			ex_ws (e);
			ok = TRUE;
			while (*e->p != ')')
			{
				EVal arg = { 0 };
				gboolean lenient;

				/* default(x, y): an undefined x is just null */
				lenient = g_strcmp0 (name, "default") == 0 && args->len == 0;
				if (lenient)
					e->lenient++;
				ok = ex_ternary (e, &arg);
				if (lenient)
					e->lenient--;
				g_array_append_val (args, arg);
				if (!ok)
					break;
				ex_ws (e);
				if (*e->p == ',')
				{
					e->p++;
					ex_ws (e);
					continue;
				}
				if (*e->p != ')')
				{
					ok = ex_fail (e, "expected ',' or ')' in call to %s()", name);
					break;
				}
			}
			if (ok)
			{
				e->p++;
				if (e->skip > 0)
					ev_clear (out);
				else
					ok = ex_call (e, name, args, out);
			}
			for (i = 0; i < args->len; i++)
				ev_clear (&g_array_index (args, EVal, i));
			return ok;
		}

		if (e->skip > 0)
		{
			ev_clear (out);
			return TRUE;
		}
		v = scope_lookup (e->scope, name);
		if (v == NULL)
		{
			if (e->lenient > 0)
			{
				ev_clear (out);
				return TRUE;
			}
			return ex_fail (e, "unknown variable '%s'", name);
		}
		ev_copy (out, v);
		return TRUE;
	}

	if (*e->p == '\0' || *e->p == '}')
		return ex_fail (e, "unexpected end of expression");
	return ex_fail (e, "unexpected character '%c'", *e->p);
}

static gboolean
ex_postfix (Expr *e,
            EVal *out)
{
	if (!ex_primary (e, out))
		return FALSE;

	for (;;)
	{
		ex_ws (e);
		if (*e->p == '.' && ex_is_ident_start (e->p[1]))
		{
			g_autofree gchar *member = NULL;
			Lv *v;

			e->p++;
			member = ex_ident (e);
			if (e->skip > 0)
				continue;
			if (out->kind == EV_NULL && e->lenient > 0)
				continue;
			if (out->kind != EV_NODE || out->node->kind != LV_MAP)
				return ex_fail (e, "'.%s' needs a mapping, got %s", member, ev_kind_name (out));
			v = lv_map_get (out->node, member);
			if (v == NULL && e->lenient == 0)
				return ex_fail (e, "no member '%s'", member);
			{
				g_autoptr(Lv) hold = lv_ref (out->node);

				ev_from_lv (out, v);
			}
			continue;
		}
		if (*e->p == '[')
		{
			EVal idx = { 0 };
			gdouble n;

			e->p++;
			if (!ex_ternary (e, &idx))
				return FALSE;
			ex_ws (e);
			if (*e->p != ']')
			{
				ev_clear (&idx);
				return ex_fail (e, "expected ']'");
			}
			e->p++;
			if (e->skip > 0)
			{
				ev_clear (&idx);
				continue;
			}
			if (out->kind == EV_NODE && out->node->kind == LV_SEQ)
			{
				Lv *v = NULL;
				g_autoptr(Lv) hold = NULL;

				if (!ev_to_number (&idx, &n) || n != floor (n))
				{
					ev_clear (&idx);
					return ex_fail (e, "list index must be an integer");
				}
				if (n < 0)
					n += (gdouble)out->node->items->len;
				if (n >= 0 && n < (gdouble)out->node->items->len)
					v = g_ptr_array_index (out->node->items, (guint)n);
				hold = lv_ref (out->node);
				ev_from_lv (out, v);
			}
			else if (out->kind == EV_NODE && out->node->kind == LV_MAP)
			{
				g_autofree gchar *k = NULL;
				g_autoptr(Lv) hold = NULL;

				k = ev_to_string (&idx);
				hold = lv_ref (out->node);
				ev_from_lv (out, k != NULL ? lv_map_get (out->node, k) : NULL);
			}
			else
			{
				ev_clear (&idx);
				return ex_fail (e, "cannot index %s", ev_kind_name (out));
			}
			ev_clear (&idx);
			continue;
		}
		return TRUE;
	}
}

static gboolean
ex_unary (Expr *e,
          EVal *out)
{
	gdouble n;

	ex_ws (e);
	if (*e->p == '!' && e->p[1] != '=')
	{
		e->p++;
		if (!ex_unary (e, out))
			return FALSE;
		if (e->skip == 0)
			ev_set_bool (out, !ev_truthy (out));
		return TRUE;
	}
	if (*e->p == '-' || *e->p == '+')
	{
		gchar op;

		op = *e->p++;
		if (!ex_unary (e, out))
			return FALSE;
		if (e->skip > 0)
			return TRUE;
		if (!ex_num_operand (e, out, op == '-' ? "-" : "+", &n))
			return FALSE;
		ev_set_num (out, op == '-' ? -n : n);
		return TRUE;
	}
	return ex_postfix (e, out);
}

static gboolean
ex_mul (Expr *e,
        EVal *out)
{
	if (!ex_unary (e, out))
		return FALSE;
	for (;;)
	{
		EVal rhs = { 0 };
		gchar op;
		gdouble a;
		gdouble b;

		ex_ws (e);
		if (*e->p != '*' && *e->p != '/' && *e->p != '%')
			return TRUE;
		op = *e->p++;
		if (!ex_unary (e, &rhs))
			return FALSE;
		if (e->skip > 0)
		{
			ev_clear (&rhs);
			continue;
		}
		if (!ex_num_operand (e, out, op == '*' ? "*" : op == '/' ? "/" : "%", &a) ||
		    !ex_num_operand (e, &rhs, op == '*' ? "*" : op == '/' ? "/" : "%", &b))
		{
			ev_clear (&rhs);
			return FALSE;
		}
		ev_clear (&rhs);
		if (op != '*' && b == 0.0)
			return ex_fail (e, "division by zero");
		if (op == '*')
			ev_set_num (out, a * b);
		else if (op == '/')
			ev_set_num (out, a / b);
		else
			ev_set_num (out, fmod (a, b));
	}
}

static gboolean
ex_add (Expr *e,
        EVal *out)
{
	if (!ex_mul (e, out))
		return FALSE;
	for (;;)
	{
		EVal rhs = { 0 };
		gchar op;
		gdouble a;
		gdouble b;

		ex_ws (e);
		if (*e->p != '+' && *e->p != '-')
			return TRUE;
		op = *e->p++;
		if (!ex_mul (e, &rhs))
			return FALSE;
		if (e->skip > 0)
		{
			ev_clear (&rhs);
			continue;
		}

		/* '+' joins text when either side is text */
		if (op == '+' && (out->kind == EV_STR || rhs.kind == EV_STR))
		{
			g_autofree gchar *sa = NULL;
			g_autofree gchar *sb = NULL;

			sa = ev_to_string (out);
			sb = ev_to_string (&rhs);
			ev_clear (&rhs);
			if (sa == NULL || sb == NULL)
				return ex_fail (e, "cannot join a list or mapping to text");
			ev_take_str (out, g_strconcat (sa, sb, NULL));
			continue;
		}
		if (!ex_num_operand (e, out, op == '+' ? "+" : "-", &a) ||
		    !ex_num_operand (e, &rhs, op == '+' ? "+" : "-", &b))
		{
			ev_clear (&rhs);
			return FALSE;
		}
		ev_clear (&rhs);
		ev_set_num (out, op == '+' ? a + b : a - b);
	}
}

static gboolean
ex_cmp (Expr *e,
        EVal *out)
{
	if (!ex_add (e, out))
		return FALSE;
	for (;;)
	{
		EVal rhs = { 0 };
		gboolean le;
		gboolean ge;
		gboolean lt;
		gint c;

		ex_ws (e);
		le = e->p[0] == '<' && e->p[1] == '=';
		ge = e->p[0] == '>' && e->p[1] == '=';
		lt = e->p[0] == '<' && !le;
		if (!le && !ge && !lt && e->p[0] != '>')
			return TRUE;
		e->p += (le || ge) ? 2 : 1;
		if (!ex_add (e, &rhs))
			return FALSE;
		if (e->skip > 0)
		{
			ev_clear (&rhs);
			continue;
		}
		if (!ex_compare (out, &rhs, &c))
		{
			ev_clear (&rhs);
			return ex_fail (e, "cannot compare %s", ev_kind_name (out));
		}
		ev_clear (&rhs);
		if (le)
			ev_set_bool (out, c <= 0);
		else if (ge)
			ev_set_bool (out, c >= 0);
		else if (lt)
			ev_set_bool (out, c < 0);
		else
			ev_set_bool (out, c > 0);
	}
}

static gboolean
ex_eq (Expr *e,
       EVal *out)
{
	if (!ex_cmp (e, out))
		return FALSE;
	for (;;)
	{
		EVal rhs = { 0 };
		gboolean neq;
		gboolean eq;

		ex_ws (e);
		eq = e->p[0] == '=' && e->p[1] == '=';
		neq = e->p[0] == '!' && e->p[1] == '=';
		if (!eq && !neq)
		{
			if (e->p[0] == '=')
				return ex_fail (e, "'=' is not an operator (use '==')");
			return TRUE;
		}
		e->p += 2;
		if (!ex_cmp (e, &rhs))
			return FALSE;
		if (e->skip > 0)
		{
			ev_clear (&rhs);
			continue;
		}
		eq = ex_equal (out, &rhs);
		ev_clear (&rhs);
		ev_set_bool (out, neq ? !eq : eq);
	}
}

static gboolean
ex_and (Expr *e,
        EVal *out)
{
	if (!ex_eq (e, out))
		return FALSE;
	for (;;)
	{
		EVal rhs = { 0 };
		gboolean lhs;

		ex_ws (e);
		if (e->p[0] != '&' || e->p[1] != '&')
			return TRUE;
		e->p += 2;
		lhs = e->skip == 0 && ev_truthy (out);
		if (!lhs)
			e->skip++;
		if (!ex_eq (e, &rhs))
		{
			if (!lhs)
				e->skip--;
			return FALSE;
		}
		if (!lhs)
			e->skip--;
		if (e->skip == 0)
			ev_set_bool (out, lhs && ev_truthy (&rhs));
		ev_clear (&rhs);
	}
}

static gboolean
ex_or (Expr *e,
       EVal *out)
{
	if (!ex_and (e, out))
		return FALSE;
	for (;;)
	{
		EVal rhs = { 0 };
		gboolean lhs;

		ex_ws (e);
		if (e->p[0] != '|' || e->p[1] != '|')
			return TRUE;
		e->p += 2;
		lhs = e->skip == 0 && ev_truthy (out);
		if (lhs)
			e->skip++;
		if (!ex_and (e, &rhs))
		{
			if (lhs)
				e->skip--;
			return FALSE;
		}
		if (lhs)
			e->skip--;
		if (e->skip == 0)
			ev_set_bool (out, lhs || ev_truthy (&rhs));
		ev_clear (&rhs);
	}
}

static gboolean
ex_ternary (Expr *e,
            EVal *out)
{
	EVal a = { 0 };
	EVal b = { 0 };
	gboolean cond;

	if (!ex_or (e, out))
		return FALSE;
	ex_ws (e);
	if (*e->p != '?')
		return TRUE;
	e->p++;

	/* Evaluate only the taken branch; parse the other one in skip mode */
	cond = e->skip == 0 && ev_truthy (out);
	if (!cond)
		e->skip++;
	if (!ex_ternary (e, &a))
	{
		if (!cond)
			e->skip--;
		return FALSE;
	}
	if (!cond)
		e->skip--;

	ex_ws (e);
	if (*e->p != ':')
	{
		ev_clear (&a);
		return ex_fail (e, "expected ':' in a ? b : c");
	}
	e->p++;

	if (cond)
		e->skip++;
	if (!ex_ternary (e, &b))
	{
		if (cond)
			e->skip--;
		ev_clear (&a);
		return FALSE;
	}
	if (cond)
		e->skip--;

	if (cond)
	{
		ev_copy (out, &a);
	}
	else
	{
		ev_copy (out, &b);
	}
	ev_clear (&a);
	ev_clear (&b);
	return TRUE;
}

/*
 * eval_embedded:
 * Evaluates the expression starting at @text (just after "${") and
 * expects the closing '}'. On success @endp points past the '}'.
 */
static gboolean
eval_embedded (const gchar  *text,
               Scope        *scope,
               EVal         *out,
               const gchar **endp,
               gchar       **errmsg)
{
	Expr e;

	memset (&e, 0, sizeof (e));
	e.p = text;
	e.scope = scope;

	if (!ex_ternary (&e, out))
	{
		*errmsg = g_steal_pointer (&e.err);
		if (*errmsg == NULL)
			*errmsg = g_strdup ("invalid expression");
		ev_clear (out);
		return FALSE;
	}
	ex_ws (&e);
	if (*e.p != '}')
	{
		*errmsg = g_strdup_printf ("expected '}' at '%.12s'", e.p);
		ev_clear (out);
		return FALSE;
	}
	*endp = e.p + 1;
	return TRUE;
}

/* ==========================================================================
 * Interpolation: ${...} inside scalar values
 * ========================================================================== */

/*
 * interp_scalar:
 * Replaces each "${expr}" of a scalar by its value. A scalar that is a
 * single "${expr}" keeps the result's type (a number, a list, ...);
 * otherwise the pieces are joined as text. "$${" writes a literal "${".
 */
static Lv *
interp_scalar (Lv           *in,
               Scope        *scope,
               const gchar  *path,
               GError      **error)
{
	const gchar *s;
	GString *out;

	s = in->str;
	if (strstr (s, "${") == NULL)
		return lv_ref (in);

	/* Whole-value expression: keep the result's type */
	if (s[0] == '$' && s[1] == '{')
	{
		EVal v = { 0 };
		const gchar *end;
		g_autofree gchar *msg = NULL;

		if (!eval_embedded (s + 2, scope, &v, &end, &msg))
		{
			set_error (error, LRG_UI_LOADER_ERROR_EXPRESSION, path, in,
			           "in '%s': %s", s, msg);
			return NULL;
		}
		while (g_ascii_isspace (*end))
			end++;
		if (*end == '\0')
		{
			Lv *r;

			r = lv_from_ev (&v, in);
			ev_clear (&v);
			return r;
		}
		ev_clear (&v);
	}

	/* Text with embedded expressions */
	out = g_string_new (NULL);
	while (*s != '\0')
	{
		if (s[0] == '$' && s[1] == '$' && s[2] == '{')
		{
			g_string_append (out, "${");
			s += 3;
			continue;
		}
		if (s[0] == '$' && s[1] == '{')
		{
			EVal v = { 0 };
			const gchar *end;
			g_autofree gchar *msg = NULL;
			g_autofree gchar *text = NULL;

			if (!eval_embedded (s + 2, scope, &v, &end, &msg))
			{
				set_error (error, LRG_UI_LOADER_ERROR_EXPRESSION, path, in,
				           "in '%s': %s", in->str, msg);
				g_string_free (out, TRUE);
				return NULL;
			}
			text = ev_to_string (&v);
			if (text == NULL)
			{
				set_error (error, LRG_UI_LOADER_ERROR_EXPRESSION, path, in,
				           "in '%s': cannot insert %s into text", in->str, ev_kind_name (&v));
				ev_clear (&v);
				g_string_free (out, TRUE);
				return NULL;
			}
			ev_clear (&v);
			g_string_append (out, text);
			s = end;
			continue;
		}
		g_string_append_c (out, *s++);
	}

	{
		Lv *r;

		r = lv_new_scalar (out->str, in->quoted, in);
		g_string_free (out, TRUE);
		return r;
	}
}

/* Deep interpolation of a value (every scalar inside it) */
static Lv *
lv_resolve (Lv           *in,
            Scope        *scope,
            const gchar  *path,
            GError      **error)
{
	Lv *out;
	guint i;

	if (in == NULL)
		return NULL;
	switch (in->kind)
	{
	case LV_SCALAR:
		return interp_scalar (in, scope, path, error);
	case LV_SEQ:
	case LV_MAP:
		out = lv_new (in->kind, in);
		for (i = 0; i < in->items->len; i++)
		{
			Lv *c;

			c = lv_resolve (g_ptr_array_index (in->items, i), scope, path, error);
			if (c == NULL)
			{
				lv_unref (out);
				return NULL;
			}
			if (in->kind == LV_MAP)
				g_ptr_array_add (out->keys, g_strdup (g_ptr_array_index (in->keys, i)));
			g_ptr_array_add (out->items, c);
		}
		return out;
	case LV_NULL:
	default:
		return lv_ref (in);
	}
}

/* Evaluates a value to an expression value (interpolating first) */
static gboolean
resolve_ev (Lv           *in,
            Scope        *scope,
            const gchar  *path,
            EVal         *out,
            GError      **error)
{
	g_autoptr(Lv) r = NULL;

	r = lv_resolve (in, scope, path, error);
	if (r == NULL)
		return FALSE;
	ev_from_lv (out, r);
	return TRUE;
}

/* Resolves a value that must be a scalar; returns its text */
static gchar *
resolve_string (Lv           *in,
                Scope        *scope,
                const gchar  *path,
                const gchar  *what,
                GError      **error)
{
	g_autoptr(Lv) r = NULL;

	r = lv_resolve (in, scope, path, error);
	if (r == NULL)
		return NULL;
	if (r->kind == LV_NULL)
		return g_strdup ("");
	if (r->kind != LV_SCALAR)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, in,
		           "'%s' must be text, got %s", what, lv_kind_name (r));
		return NULL;
	}
	return g_strdup (r->str);
}

/* ==========================================================================
 * Value conversion: resolved Lv -> typed values
 *
 * Each helper returns FALSE with a message in @msg (no path: the caller
 * adds it) when the value does not fit.
 * ========================================================================== */

static gboolean
conv_number (const Lv  *v,
             gdouble   *out,
             gchar    **msg)
{
	if (v != NULL && v->kind == LV_SCALAR && parse_number_text (v->str, out))
		return TRUE;
	*msg = g_strdup_printf ("expected a number, got %s%s%s%s",
	                        lv_kind_name (v),
	                        (v != NULL && v->kind == LV_SCALAR) ? " '" : "",
	                        (v != NULL && v->kind == LV_SCALAR) ? v->str : "",
	                        (v != NULL && v->kind == LV_SCALAR) ? "'" : "");
	return FALSE;
}

static gboolean
conv_float (const Lv  *v,
            gfloat    *out,
            gchar    **msg)
{
	gdouble d;

	if (!conv_number (v, &d, msg))
		return FALSE;
	*out = (gfloat)d;
	return TRUE;
}

static gboolean
conv_bool (const Lv  *v,
           gboolean  *out,
           gchar    **msg)
{
	static const gchar *yes[] = { "true", "True", "TRUE", "yes", "Yes", "on", "On", "1", NULL };
	static const gchar *no[] = { "false", "False", "FALSE", "no", "No", "off", "Off", "0", NULL };

	if (v != NULL && v->kind == LV_SCALAR)
	{
		if (g_strv_contains (yes, v->str))
		{
			*out = TRUE;
			return TRUE;
		}
		if (g_strv_contains (no, v->str))
		{
			*out = FALSE;
			return TRUE;
		}
	}
	*msg = g_strdup_printf ("expected true or false, got %s%s%s%s", lv_kind_name (v),
	                        (v != NULL && v->kind == LV_SCALAR) ? " '" : "",
	                        (v != NULL && v->kind == LV_SCALAR) ? v->str : "",
	                        (v != NULL && v->kind == LV_SCALAR) ? "'" : "");
	return FALSE;
}

/* Lists the nicks of an enum or flags class, for error messages */
static gchar *
nick_list (GType type)
{
	GString *s;
	guint i;

	s = g_string_new (NULL);
	if (G_TYPE_IS_ENUM (type))
	{
		GEnumClass *ec = g_type_class_ref (type);

		for (i = 0; i < ec->n_values; i++)
			g_string_append_printf (s, "%s%s", i ? ", " : "", ec->values[i].value_nick);
		g_type_class_unref (ec);
	}
	else if (G_TYPE_IS_FLAGS (type))
	{
		GFlagsClass *fc = g_type_class_ref (type);

		for (i = 0; i < fc->n_values; i++)
			g_string_append_printf (s, "%s%s", i ? ", " : "", fc->values[i].value_nick);
		g_type_class_unref (fc);
	}
	return g_string_free (s, FALSE);
}

/*
 * conv_enum:
 * Accepts a nick ("center"), a nick written with '_' or any case
 * ("CENTER", "fly_up"), the full C name ("LRG_ALIGN_CENTER") or the
 * integer value.
 */
static gboolean
conv_enum (GType      type,
           const Lv  *v,
           gint      *out,
           gchar    **msg)
{
	GEnumClass *ec;
	GEnumValue *ev;
	g_autofree gchar *nick = NULL;
	gdouble d;
	guint i;

	if (v == NULL || v->kind != LV_SCALAR)
	{
		*msg = g_strdup_printf ("expected a %s name, got %s", g_type_name (type), lv_kind_name (v));
		return FALSE;
	}

	ec = g_type_class_ref (type);
	nick = g_ascii_strdown (v->str, -1);
	g_strdelimit (nick, "_", '-');
	ev = g_enum_get_value_by_nick (ec, nick);
	if (ev == NULL)
		ev = g_enum_get_value_by_name (ec, v->str);
	if (ev == NULL)
	{
		/* Also accept the bare suffix of the C name without its prefix */
		for (i = 0; i < ec->n_values && ev == NULL; i++)
		{
			if (g_ascii_strcasecmp (ec->values[i].value_nick, v->str) == 0)
				ev = &ec->values[i];
		}
	}
	if (ev == NULL && parse_number_text (v->str, &d) && d == floor (d) &&
	    g_enum_get_value (ec, (gint)d) != NULL)
		ev = g_enum_get_value (ec, (gint)d);

	if (ev == NULL)
	{
		g_autofree gchar *nicks = nick_list (type);

		*msg = g_strdup_printf ("unknown %s value '%s' (expected one of: %s)",
		                        g_type_name (type), v->str, nicks);
		g_type_class_unref (ec);
		return FALSE;
	}
	*out = ev->value;
	g_type_class_unref (ec);
	return TRUE;
}

/*
 * conv_flags:
 * A list of nicks, or one scalar with nicks separated by '|', ',', '+'
 * or spaces; an integer is taken as the raw mask.
 */
static gboolean
conv_flags (GType      type,
            const Lv  *v,
            guint     *out,
            gchar    **msg)
{
	GFlagsClass *fc;
	g_auto(GStrv) words = NULL;
	g_autoptr(GPtrArray) list = NULL;
	gdouble d;
	guint mask;
	guint i;

	if (v != NULL && v->kind == LV_NULL)
	{
		*out = 0;
		return TRUE;
	}
	if (v != NULL && v->kind == LV_SCALAR && parse_number_text (v->str, &d) && d == floor (d) && d >= 0)
	{
		*out = (guint)d;
		return TRUE;
	}

	list = g_ptr_array_new_with_free_func (g_free);
	if (v != NULL && v->kind == LV_SCALAR)
	{
		words = g_strsplit_set (v->str, "|,+ ", -1);
		for (i = 0; words[i] != NULL; i++)
		{
			if (*words[i] != '\0')
				g_ptr_array_add (list, g_strdup (words[i]));
		}
	}
	else if (v != NULL && v->kind == LV_SEQ)
	{
		for (i = 0; i < v->items->len; i++)
		{
			Lv *it = g_ptr_array_index (v->items, i);

			if (it->kind != LV_SCALAR)
			{
				*msg = g_strdup_printf ("flag list entries must be names, got %s", lv_kind_name (it));
				return FALSE;
			}
			g_ptr_array_add (list, g_strdup (it->str));
		}
	}
	else
	{
		*msg = g_strdup_printf ("expected a list of %s names, got %s", g_type_name (type), lv_kind_name (v));
		return FALSE;
	}

	fc = g_type_class_ref (type);
	mask = 0;
	for (i = 0; i < list->len; i++)
	{
		g_autofree gchar *nick = NULL;
		GFlagsValue *fv;

		nick = g_ascii_strdown (g_ptr_array_index (list, i), -1);
		g_strdelimit (nick, "_", '-');
		fv = g_flags_get_value_by_nick (fc, nick);
		if (fv == NULL)
			fv = g_flags_get_value_by_name (fc, g_ptr_array_index (list, i));
		if (fv == NULL)
		{
			g_autofree gchar *nicks = nick_list (type);

			*msg = g_strdup_printf ("unknown %s flag '%s' (expected some of: %s)",
			                        g_type_name (type), (const gchar *)g_ptr_array_index (list, i), nicks);
			g_type_class_unref (fc);
			return FALSE;
		}
		mask |= fv->value;
	}
	g_type_class_unref (fc);
	*out = mask;
	return TRUE;
}

static gint
hex_digit (gchar c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/*
 * conv_color:
 * "#rgb", "#rgba", "#rrggbb", "#rrggbbaa", the names transparent / white /
 * black, a list [r, g, b] or [r, g, b, a] of 0..255 integers, or a mapping
 * {r:, g:, b:, a:}.
 */
static gboolean
conv_color (const Lv  *v,
            GrlColor  *out,
            gchar    **msg)
{
	guint8 c[4] = { 0, 0, 0, 255 };
	guint i;

	if (v != NULL && v->kind == LV_SCALAR)
	{
		const gchar *s = v->str;
		gsize n;

		if (g_ascii_strcasecmp (s, "transparent") == 0)
		{
			c[3] = 0;
			goto done;
		}
		if (g_ascii_strcasecmp (s, "white") == 0)
		{
			c[0] = c[1] = c[2] = 255;
			goto done;
		}
		if (g_ascii_strcasecmp (s, "black") == 0)
			goto done;

		if (*s == '#')
			s++;
		else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
			s += 2;
		n = strlen (s);
		for (i = 0; i < n; i++)
		{
			if (hex_digit (s[i]) < 0)
				break;
		}
		if (i == n && (n == 3 || n == 4))
		{
			for (i = 0; i < n; i++)
				c[i] = (guint8)(hex_digit (s[i]) * 17);
			goto done;
		}
		if (i == n && (n == 6 || n == 8))
		{
			for (i = 0; i < n / 2; i++)
				c[i] = (guint8)(hex_digit (s[i * 2]) * 16 + hex_digit (s[i * 2 + 1]));
			goto done;
		}
		*msg = g_strdup_printf ("invalid colour '%s' (expected #rgb, #rgba, #rrggbb or #rrggbbaa)", v->str);
		return FALSE;
	}

	if (v != NULL && v->kind == LV_SEQ && (v->items->len == 3 || v->items->len == 4))
	{
		for (i = 0; i < v->items->len; i++)
		{
			gdouble d;

			if (!conv_number (g_ptr_array_index (v->items, i), &d, msg))
				return FALSE;
			c[i] = (guint8)CLAMP (d, 0.0, 255.0);
		}
		goto done;
	}

	if (v != NULL && v->kind == LV_MAP)
	{
		static const gchar *names[] = { "r", "g", "b", "a" };

		for (i = 0; i < v->keys->len; i++)
		{
			const gchar *k = g_ptr_array_index (v->keys, i);
			guint j;
			gdouble d;

			for (j = 0; j < 4; j++)
			{
				if (g_strcmp0 (k, names[j]) == 0)
					break;
			}
			if (j == 4)
			{
				*msg = g_strdup_printf ("unknown colour component '%s' (expected r, g, b, a)", k);
				return FALSE;
			}
			if (!conv_number (g_ptr_array_index (v->items, i), &d, msg))
				return FALSE;
			c[j] = (guint8)CLAMP (d, 0.0, 255.0);
		}
		goto done;
	}

	*msg = g_strdup_printf ("expected a colour, got %s", lv_kind_name (v));
	return FALSE;

done:
	out->r = c[0];
	out->g = c[1];
	out->b = c[2];
	out->a = c[3];
	return TRUE;
}

/*
 * conv_four:
 * Four side values: "8" (all), [a] (all), [v, h] (vertical, horizontal),
 * [t, r, b, l] (CSS order), or a mapping with the names in @names
 * (missing ones are 0). Output order is @names' order.
 */
static gboolean
conv_four (const Lv     *v,
           const gchar **names,
           const guint  *css_order,
           gfloat       *out,
           gchar       **msg)
{
	gfloat vals[4];
	guint i;

	memset (vals, 0, sizeof (vals));
	if (v != NULL && v->kind == LV_SCALAR)
	{
		gfloat f;

		if (!conv_float (v, &f, msg))
			return FALSE;
		for (i = 0; i < 4; i++)
			out[i] = f;
		return TRUE;
	}
	if (v != NULL && v->kind == LV_SEQ)
	{
		guint n = v->items->len;

		if (n != 1 && n != 2 && n != 4)
		{
			*msg = g_strdup_printf ("expected 1, 2 or 4 numbers, got %u", n);
			return FALSE;
		}
		for (i = 0; i < n; i++)
		{
			if (!conv_float (g_ptr_array_index (v->items, i), &vals[i], msg))
				return FALSE;
		}
		if (n == 1)
			vals[1] = vals[2] = vals[3] = vals[0];
		else if (n == 2)
		{
			vals[2] = vals[0];
			vals[3] = vals[1];
		}
		/* vals is in CSS order (top, right, bottom, left); map to @names */
		for (i = 0; i < 4; i++)
			out[i] = vals[css_order[i]];
		return TRUE;
	}
	if (v != NULL && v->kind == LV_MAP)
	{
		for (i = 0; i < 4; i++)
			out[i] = 0.0f;
		for (i = 0; i < v->keys->len; i++)
		{
			const gchar *k = g_ptr_array_index (v->keys, i);
			guint j;

			for (j = 0; j < 4; j++)
			{
				if (key_equal (k, names[j]))
					break;
			}
			if (j == 4)
			{
				*msg = g_strdup_printf ("unknown side '%s' (expected %s, %s, %s, %s)",
				                        k, names[0], names[1], names[2], names[3]);
				return FALSE;
			}
			if (!conv_float (g_ptr_array_index (v->items, i), &out[j], msg))
				return FALSE;
		}
		return TRUE;
	}
	*msg = g_strdup_printf ("expected a number, a list or a mapping, got %s", lv_kind_name (v));
	return FALSE;
}

static gboolean
conv_insets (const Lv  *v,
             LrgInsets *out,
             gchar    **msg)
{
	static const gchar *names[] = { "left", "top", "right", "bottom" };
	/* CSS order index (t=0, r=1, b=2, l=3) for left, top, right, bottom */
	static const guint css[] = { 3, 0, 1, 2 };
	gfloat f[4];

	if (!conv_four (v, names, css, f, msg))
		return FALSE;
	out->left = f[0];
	out->top = f[1];
	out->right = f[2];
	out->bottom = f[3];
	return TRUE;
}

static gboolean
conv_radii (const Lv       *v,
            LrgCornerRadii *out,
            gchar         **msg)
{
	static const gchar *names[] = { "top-left", "top-right", "bottom-right", "bottom-left" };
	static const guint css[] = { 0, 1, 2, 3 };
	gfloat f[4];

	if (!conv_four (v, names, css, f, msg))
		return FALSE;
	out->top_left = f[0];
	out->top_right = f[1];
	out->bottom_right = f[2];
	out->bottom_left = f[3];
	return TRUE;
}

static gboolean
conv_vector2 (const Lv   *v,
              GrlVector2 *out,
              gchar     **msg)
{
	if (v != NULL && v->kind == LV_SEQ && v->items->len == 2)
		return conv_float (g_ptr_array_index (v->items, 0), &out->x, msg) &&
		       conv_float (g_ptr_array_index (v->items, 1), &out->y, msg);
	if (v != NULL && v->kind == LV_MAP)
	{
		Lv *x = lv_map_get (v, "x");
		Lv *y = lv_map_get (v, "y");

		out->x = out->y = 0.0f;
		if (x != NULL && !conv_float (x, &out->x, msg))
			return FALSE;
		if (y != NULL && !conv_float (y, &out->y, msg))
			return FALSE;
		return TRUE;
	}
	*msg = g_strdup_printf ("expected [x, y] or {x:, y:}, got %s", lv_kind_name (v));
	return FALSE;
}

/* Integer property: the number must be integral and inside the type */
static gboolean
conv_integer (const Lv  *v,
              gdouble    lo,
              gdouble    hi,
              gdouble   *out,
              gchar    **msg)
{
	gdouble d;

	if (!conv_number (v, &d, msg))
		return FALSE;
	if (d != floor (d))
	{
		*msg = g_strdup_printf ("expected an integer, got '%s'", v->str);
		return FALSE;
	}
	if (d < lo || d > hi)
	{
		*msg = g_strdup_printf ("%s is out of range", v->str);
		return FALSE;
	}
	*out = d;
	return TRUE;
}

/*
 * conv_property:
 * Converts @v into @out, initialised here to the type of @pspec, and
 * checks it against the property's range.
 */
static gboolean
conv_property (GParamSpec *pspec,
               const Lv   *v,
               GValue     *out,
               gchar     **msg)
{
	GType t;
	GType ft;
	gdouble d;

	t = G_PARAM_SPEC_VALUE_TYPE (pspec);
	ft = G_TYPE_FUNDAMENTAL (t);
	g_value_init (out, t);

	switch (ft)
	{
	case G_TYPE_BOOLEAN:
		{
			gboolean b;

			if (!conv_bool (v, &b, msg))
				return FALSE;
			g_value_set_boolean (out, b);
			break;
		}
	case G_TYPE_INT:
		if (!conv_integer (v, G_MININT, G_MAXINT, &d, msg))
			return FALSE;
		g_value_set_int (out, (gint)d);
		break;
	case G_TYPE_UINT:
		if (!conv_integer (v, 0, G_MAXUINT, &d, msg))
			return FALSE;
		g_value_set_uint (out, (guint)d);
		break;
	case G_TYPE_LONG:
		if (!conv_integer (v, G_MINLONG, G_MAXLONG, &d, msg))
			return FALSE;
		g_value_set_long (out, (glong)d);
		break;
	case G_TYPE_ULONG:
		if (!conv_integer (v, 0, G_MAXULONG, &d, msg))
			return FALSE;
		g_value_set_ulong (out, (gulong)d);
		break;
	case G_TYPE_INT64:
		if (!conv_integer (v, -9007199254740992.0, 9007199254740992.0, &d, msg))
			return FALSE;
		g_value_set_int64 (out, (gint64)d);
		break;
	case G_TYPE_UINT64:
		if (!conv_integer (v, 0, 9007199254740992.0, &d, msg))
			return FALSE;
		g_value_set_uint64 (out, (guint64)d);
		break;
	case G_TYPE_CHAR:
		if (!conv_integer (v, G_MININT8, G_MAXINT8, &d, msg))
			return FALSE;
		g_value_set_schar (out, (gint8)d);
		break;
	case G_TYPE_UCHAR:
		if (!conv_integer (v, 0, G_MAXUINT8, &d, msg))
			return FALSE;
		g_value_set_uchar (out, (guchar)d);
		break;
	case G_TYPE_FLOAT:
		if (!conv_number (v, &d, msg))
			return FALSE;
		g_value_set_float (out, (gfloat)d);
		break;
	case G_TYPE_DOUBLE:
		if (!conv_number (v, &d, msg))
			return FALSE;
		g_value_set_double (out, d);
		break;
	case G_TYPE_STRING:
		if (v->kind == LV_NULL)
			g_value_set_string (out, NULL);
		else if (v->kind == LV_SCALAR)
			g_value_set_string (out, v->str);
		else
		{
			*msg = g_strdup_printf ("expected text, got %s", lv_kind_name (v));
			return FALSE;
		}
		break;
	case G_TYPE_ENUM:
		{
			gint e;

			if (!conv_enum (t, v, &e, msg))
				return FALSE;
			g_value_set_enum (out, e);
			break;
		}
	case G_TYPE_FLAGS:
		{
			guint f;

			if (!conv_flags (t, v, &f, msg))
				return FALSE;
			g_value_set_flags (out, f);
			break;
		}
	case G_TYPE_BOXED:
		if (t == GRL_TYPE_COLOR)
		{
			GrlColor c;

			if (!conv_color (v, &c, msg))
				return FALSE;
			g_value_set_boxed (out, &c);
		}
		else if (t == LRG_TYPE_INSETS)
		{
			LrgInsets in;

			if (!conv_insets (v, &in, msg))
				return FALSE;
			g_value_set_boxed (out, &in);
		}
		else if (t == LRG_TYPE_CORNER_RADII)
		{
			LrgCornerRadii r;

			if (!conv_radii (v, &r, msg))
				return FALSE;
			g_value_set_boxed (out, &r);
		}
		else if (t == GRL_TYPE_VECTOR2)
		{
			GrlVector2 p;

			if (!conv_vector2 (v, &p, msg))
				return FALSE;
			g_value_set_boxed (out, &p);
		}
		else if (t == G_TYPE_STRV)
		{
			g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
			g_auto(GStrv) strv = NULL;
			guint i;

			if (v->kind == LV_SCALAR)
				g_strv_builder_add (b, v->str);
			else if (v->kind == LV_SEQ)
			{
				for (i = 0; i < v->items->len; i++)
				{
					Lv *it = g_ptr_array_index (v->items, i);

					if (it->kind != LV_SCALAR)
					{
						*msg = g_strdup_printf ("list entries must be text, got %s", lv_kind_name (it));
						return FALSE;
					}
					g_strv_builder_add (b, it->str);
				}
			}
			else if (v->kind != LV_NULL)
			{
				*msg = g_strdup_printf ("expected a list of text, got %s", lv_kind_name (v));
				return FALSE;
			}
			strv = g_strv_builder_end (b);
			g_value_set_boxed (out, strv);
		}
		else
		{
			*msg = g_strdup_printf ("properties of type %s cannot be set from YAML", g_type_name (t));
			return FALSE;
		}
		break;
	default:
		*msg = g_strdup_printf ("properties of type %s cannot be set from YAML", g_type_name (t));
		return FALSE;
	}

	/* Range check: g_param_value_validate() clamps and reports a change */
	{
		GValue check = G_VALUE_INIT;

		g_value_init (&check, t);
		g_value_copy (out, &check);
		if (g_param_value_validate (pspec, &check))
		{
			g_autofree gchar *shown = NULL;

			shown = (v->kind == LV_SCALAR) ? g_strdup (v->str) : g_strdup ("value");
			*msg = g_strdup_printf ("%s is out of range for '%s'", shown, pspec->name);
			g_value_unset (&check);
			return FALSE;
		}
		g_value_unset (&check);
	}
	return TRUE;
}

/* ==========================================================================
 * Resolved node tree
 * ========================================================================== */

typedef enum
{
	ATTR_PROP,
	ATTR_CLASS,
	ATTR_ON,
	ATTR_LOOK,
	ATTR_APPEAR,
	ATTR_MIN_SIZE,
	ATTR_MAX_SIZE,
	ATTR_ITEMS,
	ATTR_ANCHOR,       /* child of an LrgAnchorLayout: placement in `anchor` */
	ATTR_PAGE_NAME,    /* child of an LrgSwitcher: page name in `text` */
	ATTR_PACK,         /* child of an LrgHeaderBar: `ival` 0 start, 1 end, 2 title */
	ATTR_DOCK_PLACE,   /* LrgDockPanel in an LrgDockArea: `ival` position, `text` relative-to */
	ATTR_DOCK_LAYOUT,  /* LrgDockArea: lrg_dock_area_load_layout() document in `text` */
	ATTR_GRAPH,        /* LrgNodeGraph: lrg_node_graph_load_yaml() document in `text` */
	ATTR_BLOCKS,       /* LrgBlockEditor: lrg_block_editor_load_yaml() document in `text` */
	ATTR_FB_LOCATION,  /* LrgFileBrowser: folder path ("~" expanded) in `text` */
	ATTR_FB_FILTERS,   /* LrgFileBrowser: LrgFileFilter * in `specs`, first is current */
	ATTR_FB_PLACES,    /* LrgFileBrowser: PlaceSpec * in `specs` */
	ATTR_TRACKS,       /* LrgTimelineView: TrackSpec tree in `specs` */
	ATTR_MARKERS       /* LrgTimelineView: MarkerSpec array in `specs` */
} AttrKind;

typedef struct
{
	gchar     *signal;   /* "clicked", "notify::text" */
	GCallback  callback;
	gpointer   user_data;
	gboolean   after;
	gboolean   swapped;
	gchar     *action;   /* "prefix.action" instead of a callback, or NULL */
	GVariant  *target;   /* the action's parameter, nullable */
} HandlerSpec;

/* One entry of a document's top-level `shortcuts:` list */
typedef struct
{
	gchar    *accel;
	gchar    *action;
	GVariant *target;    /* nullable */
} ShortcutSpec;

typedef struct
{
	LrgUIPart      part;
	LrgWidgetState state;
	LrgPartLook    look;
} LookEntry;

typedef struct
{
	AttrKind    kind;
	gchar      *key;       /* canonical key: property name or special key */
	Lv         *value;     /* resolved value, compared by the reload diff */
	GParamSpec *pspec;     /* ATTR_PROP */
	GValue      gvalue;    /* ATTR_PROP */
	gboolean    construct; /* ATTR_PROP: construct-only */
	gboolean    late;      /* ATTR_PROP: applied after children / items */
	GArray     *handlers;  /* ATTR_ON: HandlerSpec */
	GArray     *looks;     /* ATTR_LOOK: LookEntry */
	GStrv       strv;      /* ATTR_CLASS, ATTR_ITEMS */
	gfloat      f[2];      /* ATTR_MIN_SIZE / ATTR_MAX_SIZE; ATTR_APPEAR delay in f[0] */
	gint        appear;    /* ATTR_APPEAR */
	gboolean    child;     /* a per-child setting applied through the parent */
	gint        ival;      /* ATTR_DOCK_PLACE: LrgDockPosition */
	gchar      *text;      /* ATTR_PAGE_NAME, ATTR_DOCK_PLACE, ATTR_DOCK_LAYOUT, ATTR_GRAPH */
	GPtrArray  *specs;     /* ATTR_TRACKS: TrackSpec *; ATTR_MARKERS: MarkerSpec * */
	LrgAnchorData anchor;  /* ATTR_ANCHOR */
} UiAttr;

typedef struct _UiNode UiNode;

typedef struct
{
	gchar  *label;
	UiNode *content;
} UiTab;

struct _UiNode
{
	gint         ref_count;
	GType        type;
	gchar       *path;
	Lv          *src;           /* the node's mapping, for positions */
	gchar       *id;
	GPtrArray   *attrs;         /* UiAttr * */
	GPtrArray   *children;      /* UiNode * */
	const gchar *child_prop;    /* `child:` set through this property, or NULL */
	gboolean     has_tabs;
	GPtrArray   *tabs;          /* UiTab * */
	GPtrArray   *shortcuts;     /* ShortcutSpec *, root node only, nullable */
};

static void
handler_spec_clear (gpointer data)
{
	HandlerSpec *h = data;

	g_free (h->signal);
	g_free (h->action);
	g_clear_pointer (&h->target, g_variant_unref);
}

/* handler_spec_copy: deep copy for merging `on:` entries */
static HandlerSpec
handler_spec_copy (const HandlerSpec *h)
{
	HandlerSpec copy = *h;

	copy.signal = g_strdup (h->signal);
	copy.action = g_strdup (h->action);
	copy.target = h->target != NULL ? g_variant_ref (h->target) : NULL;
	return copy;
}

static void
shortcut_spec_free (gpointer data)
{
	ShortcutSpec *sc = data;

	g_free (sc->accel);
	g_free (sc->action);
	g_clear_pointer (&sc->target, g_variant_unref);
	g_free (sc);
}

static void
ui_attr_free (gpointer data)
{
	UiAttr *a = data;

	g_free (a->key);
	lv_unref (a->value);
	if (G_IS_VALUE (&a->gvalue))
		g_value_unset (&a->gvalue);
	if (a->handlers != NULL)
		g_array_unref (a->handlers);
	if (a->looks != NULL)
		g_array_unref (a->looks);
	g_strfreev (a->strv);
	g_free (a->text);
	if (a->specs != NULL)
		g_ptr_array_unref (a->specs);
	g_free (a);
}

static UiAttr *
ui_attr_new (AttrKind     kind,
             const gchar *key,
             Lv          *value)
{
	UiAttr *a;

	a = g_new0 (UiAttr, 1);
	a->kind = kind;
	a->key = g_strdup (key);
	a->value = value != NULL ? lv_ref (value) : lv_new (LV_NULL, NULL);
	return a;
}

static void ui_node_unref (UiNode *n);

static void
ui_tab_free (gpointer data)
{
	UiTab *t = data;

	g_free (t->label);
	if (t->content != NULL)
		ui_node_unref (t->content);
	g_free (t);
}

static UiNode *
ui_node_new (GType        type,
             const gchar *path,
             Lv          *src)
{
	UiNode *n;

	n = g_new0 (UiNode, 1);
	n->ref_count = 1;
	n->type = type;
	n->path = g_strdup (path);
	n->src = src != NULL ? lv_ref (src) : NULL;
	n->attrs = g_ptr_array_new_with_free_func (ui_attr_free);
	n->children = g_ptr_array_new_with_free_func ((GDestroyNotify)ui_node_unref);
	n->tabs = g_ptr_array_new_with_free_func (ui_tab_free);
	return n;
}

static UiNode *
ui_node_ref (UiNode *n)
{
	g_atomic_int_inc (&n->ref_count);
	return n;
}

static void
ui_node_unref (UiNode *n)
{
	if (n == NULL || !g_atomic_int_dec_and_test (&n->ref_count))
		return;
	g_free (n->path);
	lv_unref (n->src);
	g_free (n->id);
	g_ptr_array_unref (n->attrs);
	g_ptr_array_unref (n->children);
	g_ptr_array_unref (n->tabs);
	g_clear_pointer (&n->shortcuts, g_ptr_array_unref);
	g_free (n);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (UiNode, ui_node_unref)

static UiAttr *
ui_node_find_attr (UiNode      *n,
                   const gchar *key)
{
	guint i;

	if (n == NULL)
		return NULL;
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);

		if (g_strcmp0 (a->key, key) == 0)
			return a;
	}
	return NULL;
}

/* Adds @a, replacing an attribute with the same key (takes ownership) */
static void
ui_node_put_attr (UiNode *n,
                  UiAttr *a)
{
	guint i;

	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *old = g_ptr_array_index (n->attrs, i);

		if (g_strcmp0 (old->key, a->key) == 0)
		{
			g_ptr_array_remove_index (n->attrs, i);
			g_ptr_array_insert (n->attrs, (gint)i, a);
			return;
		}
	}
	g_ptr_array_add (n->attrs, a);
}

/* ==========================================================================
 * The loader object
 * ========================================================================== */

typedef struct
{
	GCallback callback;
	gpointer  user_data;
} NamedHandler;

typedef struct _LiveDoc LiveDoc;

typedef struct
{
	gchar        *path;
	gboolean      exists;
	gint64        mtime;
	gint64        mtime_nsec;
	gint64        size;
	guint64       inode;
	GFileMonitor *monitor;
	gulong        monitor_id;
	LiveDoc      *doc;
} WatchedFile;

struct _LiveDoc
{
	gchar     *path;      /* canonical path of the document */
	GWeakRef   host;
	GWeakRef   root;
	GPtrArray *files;     /* WatchedFile *: the document and its includes */
	gboolean   dirty;     /* flagged by a monitor */
	gboolean   removed;   /* unwatched while polling */
};

struct _LrgUILoader
{
	GObject      parent_instance;

	LrgRegistry *registry;
	GHashTable  *handlers;     /* gchar * -> NamedHandler * */
	GHashTable  *variables;    /* gchar * -> EVal * */
	gboolean     use_symbols;
	gboolean     use_monitors;
	GModule     *self_module;

	GPtrArray   *roots;        /* GWeakRef *: roots built, newest last */
	GHashTable  *ids;          /* gchar * -> GWeakRef * */
	GPtrArray   *docs;         /* LiveDoc * */
	gint         polling;
	GHashTable  *syntax_cache; /* definition text -> LrgSyntax *, reused across reloads */
};

G_DEFINE_FINAL_TYPE (LrgUILoader, lrg_ui_loader, G_TYPE_OBJECT)

enum
{
	SIGNAL_RELOADED,
	SIGNAL_RELOAD_FAILED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static GQuark q_record;

/*
 * WidgetRecord: attached to every widget the loader builds. It holds the
 * node the widget currently reflects (the "old" side of a reload diff)
 * and the ids of the handlers the loader connected.
 */
typedef struct
{
	UiNode *node;
	GArray *handler_ids;   /* gulong */
} WidgetRecord;

static void
widget_record_free (gpointer data)
{
	WidgetRecord *r = data;

	ui_node_unref (r->node);
	g_array_unref (r->handler_ids);
	g_free (r);
}

static WidgetRecord *
widget_get_record (LrgWidget *w)
{
	return g_object_get_qdata (G_OBJECT (w), q_record);
}

/* ==========================================================================
 * Type resolution
 * ========================================================================== */

/* "ScrollBox", "scroll-box", "scroll_box" -> "scrollbox" */
static gchar *
squash_name (const gchar *name)
{
	GString *s;

	s = g_string_new (NULL);
	for (; *name != '\0'; name++)
	{
		if (*name == '-' || *name == '_')
			continue;
		g_string_append_c (s, g_ascii_tolower (*name));
	}
	return g_string_free (s, FALSE);
}

static GType
find_descendant (GType        root,
                 const gchar *squashed)
{
	g_autofree GType *kids = NULL;
	guint n;
	guint i;

	kids = g_type_children (root, &n);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *full = NULL;
		const gchar *tn;
		GType found;

		tn = g_type_name (kids[i]);
		full = squash_name (tn);
		if (g_strcmp0 (full, squashed) == 0)
			return kids[i];
		if (g_str_has_prefix (full, "lrg") && g_strcmp0 (full + 3, squashed) == 0)
			return kids[i];
		found = find_descendant (kids[i], squashed);
		if (found != G_TYPE_INVALID)
			return found;
	}
	return G_TYPE_INVALID;
}

/*
 * pascal_to_snake:
 * "LrgScrollBox" -> "lrg_scroll_box". With @merge_single, one-letter
 * segments are glued to the next one ("LrgVBox" -> "lrg_vbox").
 */
static gchar *
pascal_to_snake (const gchar *name,
                 gboolean     merge_single)
{
	GString *s;
	g_auto(GStrv) parts = NULL;
	gsize i;
	gsize n;

	/* Split before each capital that starts a word */
	s = g_string_new (NULL);
	n = strlen (name);
	for (i = 0; i < n; i++)
	{
		gchar c = name[i];

		if (g_ascii_isupper (c) && i > 0)
		{
			gboolean prev_lower = g_ascii_islower (name[i - 1]) || g_ascii_isdigit (name[i - 1]);
			gboolean next_lower = (i + 1 < n) && g_ascii_islower (name[i + 1]);

			if (prev_lower || next_lower)
				g_string_append_c (s, '_');
		}
		g_string_append_c (s, g_ascii_tolower (c));
	}
	if (!merge_single)
		return g_string_free (s, FALSE);

	/* Glue one-letter segments to the following one */
	parts = g_strsplit (s->str, "_", -1);
	g_string_truncate (s, 0);
	for (i = 0; parts[i] != NULL; i++)
	{
		g_string_append (s, parts[i]);
		if (parts[i + 1] != NULL && strlen (parts[i]) > 1)
			g_string_append_c (s, '_');
	}
	return g_string_free (s, FALSE);
}

typedef GType (*GetTypeFunc) (void);

static GType
lookup_get_type_symbol (LrgUILoader *self,
                        const gchar *snake)
{
	g_autofree gchar *sym = NULL;
	gpointer fn = NULL;
	GType t;

	if (self->self_module == NULL)
		self->self_module = g_module_open (NULL, G_MODULE_BIND_LAZY);
	if (self->self_module == NULL)
		return G_TYPE_INVALID;

	sym = g_strconcat (snake, "_get_type", NULL);
	if (!g_module_symbol (self->self_module, sym, &fn) || fn == NULL)
		return G_TYPE_INVALID;
	t = ((GetTypeFunc)fn) ();
	return t;
}

/*
 * resolve_type:
 * Resolution order: the registry, the exact GType name, a squashed match
 * among LrgWidget's descendants ("scroll-box" -> LrgScrollBox), then a
 * `lrg_<name>_get_type` symbol so types not yet instantiated are found.
 */
static GType
resolve_type (LrgUILoader *self,
              const gchar *name)
{
	g_autofree gchar *squashed = NULL;
	GType t;

	if (self->registry != NULL)
	{
		t = lrg_registry_lookup (self->registry, name);
		if (t != G_TYPE_INVALID)
			return t;
	}

	t = g_type_from_name (name);
	if (t != G_TYPE_INVALID)
		return t;

	squashed = squash_name (name);
	t = find_descendant (LRG_TYPE_WIDGET, squashed);
	if (t != G_TYPE_INVALID)
		return t;

	/* Not registered yet: try the get_type function by naming convention */
	if (g_ascii_islower (name[0]))
	{
		g_autofree gchar *snake = NULL;
		g_autofree gchar *full = NULL;

		snake = g_strdelimit (g_strdup (name), "-", '_');
		full = g_str_has_prefix (snake, "lrg_") ? g_strdup (snake) : g_strconcat ("lrg_", snake, NULL);
		t = lookup_get_type_symbol (self, full);
	}
	else
	{
		g_autofree gchar *pascal = NULL;
		g_autofree gchar *snake = NULL;

		pascal = g_str_has_prefix (name, "Lrg") ? g_strdup (name) : g_strconcat ("Lrg", name, NULL);
		snake = pascal_to_snake (pascal, FALSE);
		t = lookup_get_type_symbol (self, snake);
		if (t == G_TYPE_INVALID)
		{
			g_autofree gchar *merged = pascal_to_snake (pascal, TRUE);

			t = lookup_get_type_symbol (self, merged);
		}
	}
	return t;
}

/* ==========================================================================
 * EXPAND: Lv tree -> UiNode tree
 * ========================================================================== */

typedef struct
{
	gchar       *name;
	Lv          *params;   /* LV_MAP of name -> default, or NULL */
	Lv          *body;
	const gchar *file;
} Component;

static void
component_free (gpointer data)
{
	Component *c = data;

	g_free (c->name);
	lv_unref (c->params);
	lv_unref (c->body);
	g_free (c);
}

typedef struct
{
	GPtrArray  *default_nodes;  /* UiNode *, or NULL when the use site gave none */
	GHashTable *named;          /* name -> GPtrArray of UiNode * */
	gboolean    default_used;
	GHashTable *named_used;     /* set of names */
} SlotFill;

typedef struct
{
	Scope       *scope;
	SlotFill    *slots;   /* inside a component body, else NULL */
	const gchar *file;    /* file the current values come from, or NULL */
} Env;

typedef struct
{
	LrgUILoader *self;
	GHashTable  *components;     /* name -> Component * */
	GPtrArray   *include_stack;  /* gchar * */
	GPtrArray   *comp_stack;     /* const gchar * */
	GPtrArray   *files;          /* gchar *: every file read */
	GHashTable  *file_cache;     /* path -> Lv * */
	Scope       *globals;        /* loader variables + document variables */
	guint        instance;
	guint        depth;
	GHashTable  *menus;          /* name -> Lv *: top-level `menus:` */
	GPtrArray   *syntaxes;       /* LrgSyntax *: `languages:` and `language-file:` */
} Ctx;

static gboolean expand_item (Ctx *c, Lv *item, Env *env, const gchar *path,
                             GPtrArray *out, GError **error);
static gboolean expand_document (Ctx *c, Lv *doc, Env *env, const gchar *path,
                                 gboolean is_main, GPtrArray *out, GError **error);

/*
 * expand_list:
 * Expands a node list (a sequence, or a single mapping taken as a list
 * of one) into @out. Element paths are "<field>[i]".
 */
static gboolean
expand_list (Ctx          *c,
             Lv           *list,
             Env          *env,
             const gchar  *path,
             const gchar  *field,
             GPtrArray    *out,
             GError      **error)
{
	guint i;

	if (list == NULL || list->kind == LV_NULL)
		return TRUE;
	if (list->kind == LV_MAP)
	{
		g_autofree gchar *p = path_join (path, "%s", field);

		return expand_item (c, list, env, p, out, error);
	}
	if (list->kind != LV_SEQ)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, list,
		           "'%s' must be a list of nodes, got %s", field, lv_kind_name (list));
		return FALSE;
	}
	for (i = 0; i < list->items->len; i++)
	{
		g_autofree gchar *p = path_join (path, "%s[%u]", field, i);

		if (!expand_item (c, g_ptr_array_index (list->items, i), env, p, out, error))
			return FALSE;
	}
	return TRUE;
}

/* Expands @list and requires exactly one widget */
static UiNode *
expand_one (Ctx          *c,
            Lv           *list,
            Env          *env,
            const gchar  *path,
            const gchar  *field,
            GError      **error)
{
	g_autoptr(GPtrArray) tmp = g_ptr_array_new_with_free_func ((GDestroyNotify)ui_node_unref);

	if (!expand_list (c, list, env, path, field, tmp, error))
		return NULL;
	if (tmp->len != 1)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, list,
		           "'%s' must give exactly one widget, got %u", field, tmp->len);
		return NULL;
	}
	return ui_node_ref (g_ptr_array_index (tmp, 0));
}

/* --------------------------------------------------------------------------
 * Special keys
 * -------------------------------------------------------------------------- */

/* Parses "hovered", "checked+hovered", "pressed|focused" */
static gboolean
parse_state_key (const gchar    *key,
                 LrgWidgetState *out)
{
	g_auto(GStrv) words = NULL;
	GFlagsClass *fc;
	guint mask;
	guint i;
	gboolean ok;

	words = g_strsplit_set (key, "+|, ", -1);
	fc = g_type_class_ref (LRG_TYPE_WIDGET_STATE);
	mask = 0;
	ok = TRUE;
	for (i = 0; words[i] != NULL && ok; i++)
	{
		g_autofree gchar *nick = NULL;
		GFlagsValue *fv;

		if (*words[i] == '\0')
			continue;
		nick = g_strdelimit (g_ascii_strdown (words[i], -1), "_", '-');
		fv = g_flags_get_value_by_nick (fc, nick);
		if (fv == NULL || fv->value == 0)
			ok = FALSE;
		else
			mask |= fv->value;
	}
	g_type_class_unref (fc);
	if (!ok || mask == 0)
		return FALSE;
	*out = (LrgWidgetState)mask;
	return TRUE;
}

/* A colour, or a mapping {color:, <number field>:} for border/shadow/glow */
static gboolean
look_color_with (const Lv    *v,
                 GrlColor    *color,
                 const gchar *num_key,
                 gfloat      *num,
                 gboolean    *num_set,
                 gchar      **msg)
{
	*num_set = FALSE;
	if (v != NULL && v->kind == LV_MAP && lv_map_get (v, "color") != NULL)
	{
		guint i;

		for (i = 0; i < v->keys->len; i++)
		{
			const gchar *k = g_ptr_array_index (v->keys, i);
			Lv *it = g_ptr_array_index (v->items, i);

			if (key_equal (k, "color"))
			{
				if (!conv_color (it, color, msg))
					return FALSE;
			}
			else if (num_key != NULL && key_equal (k, num_key))
			{
				if (!conv_float (it, num, msg))
					return FALSE;
				*num_set = TRUE;
			}
			else
			{
				*msg = g_strdup_printf ("unknown key '%s' (expected color%s%s)", k,
				                        num_key != NULL ? ", " : "", num_key != NULL ? num_key : "");
				return FALSE;
			}
		}
		return TRUE;
	}
	return conv_color (v, color, msg);
}

static gboolean
parse_gradient (const Lv         *v,
                LrgPaintGradient *g,
                gchar           **msg)
{
	guint i;

	memset (g, 0, sizeof (*g));
	g->kind = LRG_PAINT_GRADIENT_LINEAR;
	g->center_x = 0.5f;
	g->center_y = 0.5f;
	g->radius = 0.5f;
	if (v == NULL || v->kind != LV_MAP)
	{
		*msg = g_strdup_printf ("gradient must be a mapping, got %s", lv_kind_name (v));
		return FALSE;
	}
	for (i = 0; i < v->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (v->keys, i);
		Lv *it = g_ptr_array_index (v->items, i);
		gboolean ok;

		if (key_equal (k, "kind"))
		{
			gint e;

			ok = conv_enum (LRG_TYPE_PAINT_GRADIENT_KIND, it, &e, msg);
			g->kind = (LrgPaintGradientKind)e;
		}
		else if (key_equal (k, "start") || key_equal (k, "from"))
			ok = conv_color (it, &g->start, msg);
		else if (key_equal (k, "end") || key_equal (k, "to"))
			ok = conv_color (it, &g->end, msg);
		else if (key_equal (k, "angle"))
			ok = conv_float (it, &g->angle, msg);
		else if (key_equal (k, "center-x"))
			ok = conv_float (it, &g->center_x, msg);
		else if (key_equal (k, "center-y"))
			ok = conv_float (it, &g->center_y, msg);
		else if (key_equal (k, "center"))
		{
			GrlVector2 p;

			ok = conv_vector2 (it, &p, msg);
			g->center_x = p.x;
			g->center_y = p.y;
		}
		else if (key_equal (k, "radius"))
			ok = conv_float (it, &g->radius, msg);
		else
		{
			*msg = g_strdup_printf ("unknown gradient key '%s' (expected kind, start, end, angle, center, radius)", k);
			ok = FALSE;
		}
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

/*
 * look_number_in:
 * A number between @lo and @hi (inclusive) for look field @what.
 */
static gboolean
look_number_in (const Lv     *v,
                gfloat       *out,
                gfloat        lo,
                gfloat        hi,
                const gchar  *what,
                gchar       **msg)
{
	if (!conv_float (v, out, msg))
		return FALSE;
	if (*out < lo || *out > hi)
	{
		*msg = g_strdup_printf ("%s: %g is outside %g..%g", what, (gdouble)*out, (gdouble)lo, (gdouble)hi);
		return FALSE;
	}
	return TRUE;
}

/*
 * look_glass:
 * A "glass:" mapping (frost, refraction, saturation, tint, rim,
 * dispersion, blur) over the default glass, with the ranges theme YAML
 * enforces.
 */
static gboolean
look_glass (const Lv       *v,
            LrgPaintGlass  *out,
            gchar         **msg)
{
	guint i;

	if (v == NULL || v->kind != LV_MAP)
	{
		*msg = g_strdup_printf ("'glass' must be a mapping, got %s", lv_kind_name (v));
		return FALSE;
	}
	lrg_paint_glass_init_default (out);
	for (i = 0; i < v->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (v->keys, i);
		const Lv    *f = g_ptr_array_index (v->items, i);
		gboolean     ok;

		if (key_equal (k, "frost"))
			ok = look_number_in (f, &out->frost, 0.0f, 1.0f, "glass.frost", msg);
		else if (key_equal (k, "refraction"))
			ok = look_number_in (f, &out->refraction, 0.0f, 256.0f, "glass.refraction", msg);
		else if (key_equal (k, "saturation"))
			ok = look_number_in (f, &out->saturation, 0.0f, 4.0f, "glass.saturation", msg);
		else if (key_equal (k, "rim"))
			ok = look_number_in (f, &out->rim_light, 0.0f, 1.0f, "glass.rim", msg);
		else if (key_equal (k, "dispersion"))
			ok = look_number_in (f, &out->dispersion, 0.0f, 1.0f, "glass.dispersion", msg);
		else if (key_equal (k, "blur"))
			ok = look_number_in (f, &out->blur_radius, 0.0f, 256.0f, "glass.blur", msg);
		else if (key_equal (k, "tint"))
			ok = conv_color (f, &out->tint, msg);
		else
		{
			*msg = g_strdup_printf ("unknown glass field '%s' (expected frost, refraction, saturation, "
			                        "tint, rim, dispersion or blur)", k);
			ok = FALSE;
		}
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

/*
 * look_effect_colors:
 * One colour or a list of one to three; missing ones repeat the last.
 */
static gboolean
look_effect_colors (const Lv     *v,
                    LrgPartLook  *look,
                    gchar       **msg)
{
	GrlColor c[3];
	guint    n = 1;
	guint    i;

	if (v != NULL && v->kind == LV_SEQ)
	{
		n = v->items->len;
		if (n < 1 || n > 3)
		{
			*msg = g_strdup_printf ("'effect-colors' takes one to three colours, got %u", n);
			return FALSE;
		}
		for (i = 0; i < n; i++)
			if (!conv_color (g_ptr_array_index (v->items, i), &c[i], msg))
				return FALSE;
	}
	else if (!conv_color (v, &c[0], msg))
		return FALSE;

	lrg_part_look_set_effect_colors (look, &c[0], n > 1 ? &c[1] : NULL, n > 2 ? &c[2] : NULL);
	return TRUE;
}

/*
 * parse_look_spec:
 * One part's look: field keys fill an #LrgPartLook; keys naming widget
 * states ("hovered", "checked+hovered") nest a look for those states.
 */
static gboolean
parse_look_spec (const Lv       *spec,
                 LrgUIPart       part,
                 LrgWidgetState  state,
                 GArray         *out,
                 gchar         **msg)
{
	LookEntry entry;
	guint nested;
	guint i;

	if (spec == NULL || spec->kind != LV_MAP)
	{
		*msg = g_strdup_printf ("a look must be a mapping of fields, got %s", lv_kind_name (spec));
		return FALSE;
	}

	memset (&entry, 0, sizeof (entry));
	entry.part = part;
	entry.state = state;
	lrg_part_look_init (&entry.look);
	nested = 0;

	for (i = 0; i < spec->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (spec->keys, i);
		Lv *v = g_ptr_array_index (spec->items, i);
		LrgPartLook *l = &entry.look;
		LrgWidgetState st;
		gboolean ok;
		gboolean num_set;
		gfloat num;

		if (key_equal (k, "background"))
		{
			ok = conv_color (v, &l->background, msg);
			l->set |= LRG_PART_LOOK_BACKGROUND;
		}
		else if (key_equal (k, "foreground") || key_equal (k, "color"))
		{
			ok = conv_color (v, &l->foreground, msg);
			l->set |= LRG_PART_LOOK_FOREGROUND;
		}
		else if (key_equal (k, "border"))
		{
			ok = look_color_with (v, &l->border, "width", &num, &num_set, msg);
			l->set |= LRG_PART_LOOK_BORDER;
			if (ok && num_set)
			{
				l->border_width = num;
				l->set |= LRG_PART_LOOK_BORDER_WIDTH;
			}
		}
		else if (key_equal (k, "border-width"))
		{
			ok = conv_float (v, &l->border_width, msg);
			l->set |= LRG_PART_LOOK_BORDER_WIDTH;
		}
		else if (key_equal (k, "radius") || key_equal (k, "radii"))
		{
			ok = conv_radii (v, &l->radii, msg);
			l->set |= LRG_PART_LOOK_RADII;
		}
		else if (key_equal (k, "shadow"))
		{
			ok = look_color_with (v, &l->shadow, "blur", &num, &num_set, msg);
			l->set |= LRG_PART_LOOK_SHADOW;
			if (ok && num_set)
			{
				l->shadow_blur = num;
				l->set |= LRG_PART_LOOK_SHADOW_BLUR;
			}
		}
		else if (key_equal (k, "shadow-blur"))
		{
			ok = conv_float (v, &l->shadow_blur, msg);
			l->set |= LRG_PART_LOOK_SHADOW_BLUR;
		}
		else if (key_equal (k, "shadow-offset-y") || key_equal (k, "shadow-offset"))
		{
			ok = conv_float (v, &l->shadow_offset_y, msg);
			l->set |= LRG_PART_LOOK_SHADOW_OFFSET;
		}
		else if (key_equal (k, "gradient"))
		{
			ok = parse_gradient (v, &l->gradient, msg);
			l->set |= LRG_PART_LOOK_GRADIENT;
		}
		else if (key_equal (k, "opacity"))
		{
			ok = conv_float (v, &l->opacity, msg);
			l->set |= LRG_PART_LOOK_OPACITY;
		}
		else if (key_equal (k, "size"))
		{
			ok = conv_float (v, &l->size, msg);
			l->set |= LRG_PART_LOOK_SIZE;
		}
		else if (key_equal (k, "glow"))
		{
			ok = look_color_with (v, &l->glow, "size", &num, &num_set, msg);
			l->set |= LRG_PART_LOOK_GLOW;
			if (ok && num_set)
			{
				l->glow_size = num;
				l->set |= LRG_PART_LOOK_GLOW_SIZE;
			}
		}
		else if (key_equal (k, "glow-size"))
		{
			ok = conv_float (v, &l->glow_size, msg);
			l->set |= LRG_PART_LOOK_GLOW_SIZE;
		}
		else if (key_equal (k, "material"))
		{
			gint material = LRG_PAINT_MATERIAL_NONE;

			ok = conv_enum (LRG_TYPE_PAINT_MATERIAL, v, &material, msg);
			if (ok)
				lrg_part_look_set_material (l, (LrgPaintMaterial)material);
		}
		else if (key_equal (k, "glass"))
		{
			LrgPaintGlass glass;

			ok = look_glass (v, &glass, msg);
			if (ok)
				lrg_part_look_set_glass (l, &glass);
		}
		else if (key_equal (k, "effect"))
		{
			/* "none" clears an inherited effect; anything else must be a
			 * built-in or already registered effect */
			if (v == NULL || v->kind != LV_SCALAR)
			{
				*msg = g_strdup_printf ("'effect' must be an effect name, got %s", lv_kind_name (v));
				ok = FALSE;
			}
			else if (g_strcmp0 (v->str, "none") == 0)
			{
				lrg_part_look_set_effect (l, NULL);
				ok = TRUE;
			}
			else if (lrg_paint_effect_lookup (v->str) < 0)
			{
				*msg = g_strdup_printf ("unknown effect '%s' (register user effects before loading)", v->str);
				ok = FALSE;
			}
			else
			{
				lrg_part_look_set_effect (l, v->str);
				ok = TRUE;
			}
		}
		else if (key_equal (k, "effect-colors"))
			ok = look_effect_colors (v, l, msg);
		else if (key_equal (k, "effect-intensity"))
		{
			ok = look_number_in (v, &num, 0.0f, 4.0f, "effect-intensity", msg);
			if (ok)
				lrg_part_look_set_effect_intensity (l, num);
		}
		else if (parse_state_key (k, &st))
		{
			ok = parse_look_spec (v, part, (LrgWidgetState)(state | st), out, msg);
			nested++;
		}
		else
		{
			*msg = g_strdup_printf ("unknown look field '%s' (expected background, foreground, border, "
			                        "border-width, radius, shadow, shadow-blur, shadow-offset-y, gradient, "
			                        "opacity, size, glow, glow-size, material, glass, effect, effect-colors, "
			                        "effect-intensity or a state such as hovered)", k);
			ok = FALSE;
		}
		if (!ok)
			return FALSE;
	}

	/* A spec with only nested states adds no look of its own */
	if (entry.look.set != LRG_PART_LOOK_NONE || nested == 0)
		g_array_append_val (out, entry);
	return TRUE;
}

static gboolean
parse_look (const Lv  *v,
            GArray    *out,
            gchar    **msg)
{
	GEnumClass *ec;
	guint i;
	gboolean ok;

	if (v->kind != LV_MAP)
	{
		*msg = g_strdup_printf ("'look' must map part names to looks, got %s", lv_kind_name (v));
		return FALSE;
	}
	ec = g_type_class_ref (LRG_TYPE_UI_PART);
	ok = TRUE;
	for (i = 0; i < v->keys->len && ok; i++)
	{
		g_autofree gchar *nick = NULL;
		GEnumValue *ev;

		nick = g_strdelimit (g_ascii_strdown (g_ptr_array_index (v->keys, i), -1), "_", '-');
		ev = g_enum_get_value_by_nick (ec, nick);
		if (ev == NULL)
		{
			g_autofree gchar *nicks = nick_list (LRG_TYPE_UI_PART);

			*msg = g_strdup_printf ("unknown part '%s' (expected one of: %s)",
			                        (const gchar *)g_ptr_array_index (v->keys, i), nicks);
			ok = FALSE;
			break;
		}
		ok = parse_look_spec (g_ptr_array_index (v->items, i), (LrgUIPart)ev->value,
		                      LRG_WIDGET_STATE_NONE, out, msg);
	}
	g_type_class_unref (ec);
	return ok;
}

/* "a b c" or [a, b, c] -> strv */
static GStrv
parse_word_list (const Lv  *v,
                 gboolean   split_spaces,
                 gchar    **msg)
{
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
	guint i;

	if (v->kind == LV_NULL)
		return g_strv_builder_end (b);
	if (v->kind == LV_SCALAR)
	{
		if (split_spaces)
		{
			g_auto(GStrv) words = g_strsplit_set (v->str, " \t,", -1);

			for (i = 0; words[i] != NULL; i++)
			{
				if (*words[i] != '\0')
					g_strv_builder_add (b, words[i]);
			}
		}
		else
			g_strv_builder_add (b, v->str);
		return g_strv_builder_end (b);
	}
	if (v->kind == LV_SEQ)
	{
		for (i = 0; i < v->items->len; i++)
		{
			Lv *it = g_ptr_array_index (v->items, i);

			if (it->kind != LV_SCALAR)
			{
				*msg = g_strdup_printf ("list entries must be text, got %s", lv_kind_name (it));
				return NULL;
			}
			g_strv_builder_add (b, it->str);
		}
		return g_strv_builder_end (b);
	}
	*msg = g_strdup_printf ("expected text or a list of text, got %s", lv_kind_name (v));
	return NULL;
}

/*
 * is_action_name:
 * "prefix.action" with both parts non-empty. Handler names are C
 * identifiers and never contain a dot, so a dotted value is an action.
 */
static gboolean
is_action_name (const gchar *s)
{
	const gchar *dot = s != NULL ? strchr (s, '.') : NULL;

	return dot != NULL && dot != s && dot[1] != '\0';
}

/*
 * parse_action_target:
 * The parameter of an action reference: @target is a scalar, @type_lv
 * an optional GVariant type string ("s" when missing). Strings are
 * taken as-is; other types are parsed in GVariant text format
 * ("true", "42", "1.5", "('a', 2)").
 */
static gboolean
parse_action_target (const Lv  *target,
                     const Lv  *type_lv,
                     GVariant **out,
                     gchar    **msg)
{
	const gchar *type = "s";
	g_autoptr(GError) local = NULL;
	GVariant *v;

	*out = NULL;
	if (target == NULL)
	{
		if (type_lv != NULL)
		{
			*msg = g_strdup ("'target-type' needs a 'target'");
			return FALSE;
		}
		return TRUE;
	}
	if (target->kind != LV_SCALAR)
	{
		*msg = g_strdup_printf ("'target' must be a scalar, got %s", lv_kind_name (target));
		return FALSE;
	}
	if (type_lv != NULL)
	{
		if (type_lv->kind != LV_SCALAR || !g_variant_type_string_is_valid (type_lv->str))
		{
			*msg = g_strdup ("'target-type' must be a GVariant type string such as s, b, i or d");
			return FALSE;
		}
		type = type_lv->str;
	}

	if (g_strcmp0 (type, "s") == 0)
		v = g_variant_new_string (target->str);
	else
	{
		g_autoptr(GVariantType) vt = g_variant_type_new (type);

		v = g_variant_parse (vt, target->str, NULL, NULL, &local);
		if (v == NULL)
		{
			*msg = g_strdup_printf ("cannot read '%s' as type %s: %s", target->str, type, local->message);
			return FALSE;
		}
	}
	/* g_variant_new_string() is floating, g_variant_parse() is not */
	*out = g_variant_take_ref (v);
	return TRUE;
}

/*
 * parse_handlers:
 * `on:` maps signal names to a handler name, to an action
 * ("prefix.action"), or to {handler:, after: bool, swapped: bool} /
 * {action:, target:, target-type:, after: bool}. Signals are checked
 * against @type and handler names resolved now, so a reload with a typo
 * fails before anything changes. Actions are resolved when the signal
 * fires, from the widget up through its ancestors.
 */
static gboolean
parse_handlers (LrgUILoader  *self,
                GType         type,
                const Lv     *v,
                const gchar  *path,
                GArray       *out,
                GError      **error)
{
	guint i;

	if (v->kind != LV_MAP)
	{
		set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, v,
		           "'on' must map signal names to handler names, got %s", lv_kind_name (v));
		return FALSE;
	}
	for (i = 0; i < v->keys->len; i++)
	{
		const gchar *sig = g_ptr_array_index (v->keys, i);
		Lv *it = g_ptr_array_index (v->items, i);
		HandlerSpec h;
		const gchar *hname = NULL;
		NamedHandler *nh;
		guint signal_id;
		GQuark detail;

		memset (&h, 0, sizeof (h));
		if (!g_signal_parse_name (sig, type, &signal_id, &detail, TRUE))
		{
			set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, it,
			           "%s has no signal '%s'", g_type_name (type), sig);
			return FALSE;
		}

		/* An action instead of a C handler */
		if ((it->kind == LV_SCALAR && is_action_name (it->str)) ||
		    (it->kind == LV_MAP && lv_map_get (it, "action") != NULL))
		{
			Lv *av = it->kind == LV_MAP ? lv_map_get (it, "action") : it;
			Lv *after = it->kind == LV_MAP ? lv_map_get (it, "after") : NULL;
			g_autofree gchar *msg = NULL;

			if (av->kind != LV_SCALAR || !is_action_name (av->str))
			{
				set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, it,
				           "signal '%s': 'action' must be \"prefix.action\"", sig);
				return FALSE;
			}
			if ((it->kind == LV_MAP &&
			     !parse_action_target (lv_map_get (it, "target"), lv_map_get (it, "target-type"),
			                           &h.target, &msg)) ||
			    (after != NULL && !conv_bool (after, &h.after, &msg)))
			{
				set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, it, "signal '%s': %s", sig, msg);
				return FALSE;
			}
			h.action = g_strdup (av->str);
			h.signal = g_strdup (sig);
			g_array_append_val (out, h);
			continue;
		}

		if (it->kind == LV_SCALAR)
			hname = it->str;
		else if (it->kind == LV_MAP)
		{
			Lv *hv = lv_map_get (it, "handler");
			Lv *after = lv_map_get (it, "after");
			Lv *swapped = lv_map_get (it, "swapped");
			g_autofree gchar *msg = NULL;

			if (hv == NULL || hv->kind != LV_SCALAR)
			{
				set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, it,
				           "signal '%s': a mapping needs a 'handler' or an 'action'", sig);
				return FALSE;
			}
			hname = hv->str;
			if ((after != NULL && !conv_bool (after, &h.after, &msg)) ||
			    (swapped != NULL && !conv_bool (swapped, &h.swapped, &msg)))
			{
				set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, it, "signal '%s': %s", sig, msg);
				return FALSE;
			}
		}
		else
		{
			set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, it,
			           "signal '%s': expected a handler name, got %s", sig, lv_kind_name (it));
			return FALSE;
		}

		nh = g_hash_table_lookup (self->handlers, hname);
		if (nh != NULL)
		{
			h.callback = nh->callback;
			h.user_data = nh->user_data;
		}
		else if (self->use_symbols)
		{
			gpointer fn = NULL;

			if (self->self_module == NULL)
				self->self_module = g_module_open (NULL, G_MODULE_BIND_LAZY);
			if (self->self_module != NULL && g_module_symbol (self->self_module, hname, &fn))
				h.callback = (GCallback)fn;
		}
		if (h.callback == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_HANDLER, path, it,
			           "unknown handler '%s' for signal '%s'%s", hname, sig,
			           self->use_symbols ? " (not registered and no such symbol)" :
			                               " (register it with lrg_ui_loader_add_handler())");
			return FALSE;
		}
		h.signal = g_strdup (sig);
		g_array_append_val (out, h);
	}
	return TRUE;
}

/* Properties applied after children and items (they depend on them) */
static gboolean
is_late_property (const gchar *name)
{
	static const gchar *late[] = {
		"value", "selected", "active-tab", "fraction", "scroll-x", "scroll-y",
		"cursor-position", "visible-child-name", "visible-index", "time", NULL
	};

	return g_strv_contains (late, name);
}

/* Builds the Lv that stands for a merged class list / handler map */
static Lv *
lv_from_strv (GStrv     strv,
              const Lv *at)
{
	Lv *seq;
	guint i;

	seq = lv_new (LV_SEQ, at);
	for (i = 0; strv != NULL && strv[i] != NULL; i++)
		g_ptr_array_add (seq->items, lv_new_scalar (strv[i], TRUE, at));
	return seq;
}

static gboolean
type_has_widget_property (GObjectClass *klass,
                          const gchar  *name)
{
	GParamSpec *ps;

	ps = g_object_class_find_property (klass, name);
	return ps != NULL && (ps->flags & G_PARAM_WRITABLE) &&
	       g_type_is_a (LRG_TYPE_WIDGET, G_PARAM_SPEC_VALUE_TYPE (ps));
}

/*
 * put_handlers:
 * Parses an `on:` mapping (or the one-entry mapping an `on-<signal>:`
 * key stands for) into the node's ATTR_ON attribute. With @merge the
 * entries are merged with the existing ones; a signal named again
 * replaces its old entry.
 */
static gboolean
put_handlers (LrgUILoader  *self,
              UiNode       *node,
              Lv           *r,
              const gchar  *path,
              gboolean      merge,
              GError      **error)
{
	UiAttr *a;
	g_autoptr(GArray) hs = g_array_new (FALSE, TRUE, sizeof (HandlerSpec));
	g_autoptr(Lv) value = NULL;
	UiAttr *old;

	g_array_set_clear_func (hs, handler_spec_clear);
	if (!parse_handlers (self, node->type, r, path, hs, error))
		return FALSE;

	/* Overrides add signals; a signal named again replaces the old entry */
	old = merge ? ui_node_find_attr (node, "on") : NULL;
	value = lv_ref (r);
	if (old != NULL)
	{
		g_autoptr(GArray) merged = g_array_new (FALSE, TRUE, sizeof (HandlerSpec));
		guint i;
		guint j;

		g_array_set_clear_func (merged, handler_spec_clear);
		lv_unref (value);
		value = lv_new (LV_MAP, r);
		for (i = 0; i < old->handlers->len; i++)
		{
			HandlerSpec *oh = &g_array_index (old->handlers, HandlerSpec, i);
			HandlerSpec copy;
			gboolean replaced = FALSE;

			for (j = 0; j < hs->len; j++)
			{
				if (g_strcmp0 (g_array_index (hs, HandlerSpec, j).signal, oh->signal) == 0)
					replaced = TRUE;
			}
			if (replaced)
				continue;
			copy = handler_spec_copy (oh);
			g_array_append_val (merged, copy);
			lv_map_add (value, oh->signal, lv_ref (lv_map_get (old->value, oh->signal) != NULL ?
			                                     lv_map_get (old->value, oh->signal) :
			                                     old->value));
		}
		for (j = 0; j < hs->len; j++)
		{
			HandlerSpec copy = handler_spec_copy (&g_array_index (hs, HandlerSpec, j));

			g_array_append_val (merged, copy);
		}
		for (j = 0; j < r->keys->len; j++)
			lv_map_add (value, g_ptr_array_index (r->keys, j), lv_ref (g_ptr_array_index (r->items, j)));
		g_array_unref (hs);
		hs = g_steal_pointer (&merged);
	}
	a = ui_attr_new (ATTR_ON, "on", value);
	a->handlers = g_steal_pointer (&hs);
	ui_node_put_attr (node, a);
	return TRUE;
}

static gboolean check_keys (const Lv *map, const gchar **allowed, const gchar *what,
                            const gchar *path, GError **error);

/* --------------------------------------------------------------------------
 * Writing a value back as YAML
 *
 * Embedded documents (`graph:`, `layout:`, `languages:` entries) are
 * handed to parsers that take YAML text, so the resolved value is
 * written back in flow style (which is also JSON). Plain scalars stay
 * plain when they are simple words or numbers, so they keep their YAML
 * type; everything else is double-quoted.
 * -------------------------------------------------------------------------- */

static gboolean
emit_plain_ok (const gchar *s)
{
	const gchar *p;

	if (s == NULL || *s == '\0')
		return FALSE;
	/* "-" or "-x" would read as a sequence entry or need context */
	if (s[0] == '-' && !g_ascii_isdigit (s[1]) && s[1] != '.')
		return FALSE;
	for (p = s; *p != '\0'; p++)
	{
		if (!g_ascii_isalnum (*p) && *p != '_' && *p != '.' && *p != '+' && *p != '-')
			return FALSE;
	}
	return TRUE;
}

static void
emit_quoted (GString     *out,
             const gchar *s)
{
	const guchar *p;

	g_string_append_c (out, '"');
	for (p = (const guchar *)s; *p != '\0'; p++)
	{
		switch (*p)
		{
		case '"':
			g_string_append (out, "\\\"");
			break;
		case '\\':
			g_string_append (out, "\\\\");
			break;
		case '\n':
			g_string_append (out, "\\n");
			break;
		case '\t':
			g_string_append (out, "\\t");
			break;
		case '\r':
			g_string_append (out, "\\r");
			break;
		default:
			if (*p < 0x20)
				g_string_append_printf (out, "\\x%02x", (guint)*p);
			else
				g_string_append_c (out, (gchar)*p);
			break;
		}
	}
	g_string_append_c (out, '"');
}

static void
lv_emit (GString  *out,
         const Lv *v)
{
	guint i;

	switch (v->kind)
	{
	case LV_SCALAR:
		if (!v->quoted && emit_plain_ok (v->str))
			g_string_append (out, v->str);
		else
			emit_quoted (out, v->str);
		break;
	case LV_SEQ:
		g_string_append_c (out, '[');
		for (i = 0; i < v->items->len; i++)
		{
			if (i > 0)
				g_string_append (out, ", ");
			lv_emit (out, g_ptr_array_index (v->items, i));
		}
		g_string_append_c (out, ']');
		break;
	case LV_MAP:
		g_string_append_c (out, '{');
		for (i = 0; i < v->items->len; i++)
		{
			if (i > 0)
				g_string_append (out, ", ");
			emit_quoted (out, g_ptr_array_index (v->keys, i));
			g_string_append (out, ": ");
			lv_emit (out, g_ptr_array_index (v->items, i));
		}
		g_string_append_c (out, '}');
		break;
	case LV_NULL:
	default:
		g_string_append (out, "null");
		break;
	}
}

/* lv_to_yaml: (transfer full) the YAML text of @v */
static gchar *
lv_to_yaml (const Lv *v)
{
	GString *out = g_string_new (NULL);

	lv_emit (out, v);
	g_string_append_c (out, '\n');
	return g_string_free (out, FALSE);
}

/*
 * resolve_file_path:
 * A file named in a document: relative to the file the value comes
 * from, or to the current directory for documents given as text.
 */
static gchar *
resolve_file_path (Env         *env,
                   const gchar *rel)
{
	g_autofree gchar *base = NULL;

	base = env->file != NULL ? g_path_get_dirname (env->file) : g_get_current_dir ();
	return g_canonicalize_filename (rel, base);
}

/*
 * read_doc_file:
 * Reads a file a document refers to (`graph-file:`, `language-file:`,
 * `languages: [{file:}]`) and adds it to the files a watched document
 * follows, so editing it triggers a reload.
 */
static gchar *
read_doc_file (Ctx          *c,
               const gchar  *full,
               const Lv     *at,
               const gchar  *path,
               GError      **error)
{
	g_autoptr(GError) local = NULL;
	gchar *contents = NULL;

	if (!g_ptr_array_find_with_equal_func (c->files, full, g_str_equal, NULL))
		g_ptr_array_add (c->files, g_strdup (full));
	if (!g_file_get_contents (full, &contents, NULL, &local))
	{
		set_error (error, LRG_UI_LOADER_ERROR_IO, path, at, "cannot read '%s': %s", full, local->message);
		return NULL;
	}
	return contents;
}

/* --------------------------------------------------------------------------
 * Menus
 *
 * A menu is a list of entries:
 *   - {label: _Open, action: app.open, accel: "<Ctrl>o", icon: folder}
 *   - {label: Export, submenu: [...]}           (or submenu: menu-name)
 *   - {section: [...], label: Recent}           (label optional)
 *   - separator                                 (or {separator: true})
 * or the name of a menu declared under the top-level `menus:`. Entries
 * between separators are grouped into sections. The result is a GMenu,
 * built during expansion like every other value.
 * -------------------------------------------------------------------------- */

#define UI_MAX_MENU_DEPTH (32)

static gboolean
is_separator_entry (const Lv *e)
{
	return e->kind == LV_SCALAR &&
	       (g_strcmp0 (e->str, "separator") == 0 || g_strcmp0 (e->str, "-") == 0 ||
	        g_strcmp0 (e->str, "---") == 0);
}

static GMenu *menu_from_lv (Ctx *c, Lv *v, const gchar *path, guint depth, GError **error);

/* Resolves the `menus:` entry @name and builds it */
static GMenu *
menu_from_name (Ctx          *c,
                const Lv     *name,
                const gchar  *path,
                guint         depth,
                GError      **error)
{
	g_autoptr(Lv) r = NULL;
	g_autofree gchar *mp = NULL;
	Lv *def;

	def = c->menus != NULL ? g_hash_table_lookup (c->menus, name->str) : NULL;
	if (def == NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, name,
		           "unknown menu '%s' (declare it under the top-level 'menus:')", name->str);
		return NULL;
	}
	mp = g_strdup_printf ("menus.%s", name->str);
	r = lv_resolve (def, c->globals, mp, error);
	if (r == NULL)
		return NULL;
	return menu_from_lv (c, r, mp, depth + 1, error);
}

/* Builds one mapping entry of a menu into a GMenuItem */
static GMenuItem *
menu_item_from_lv (Ctx          *c,
                   Lv           *e,
                   const gchar  *path,
                   guint         depth,
                   GError      **error)
{
	static const gchar *keys[] = {
		"label", "action", "target", "target-type", "accel", "icon", "submenu", "section",
		"separator", "if", NULL
	};
	g_autoptr(GMenuItem) item = NULL;
	g_autoptr(GVariant) target = NULL;
	g_autofree gchar *msg = NULL;
	Lv *label_lv = lv_map_get (e, "label");
	Lv *action = lv_map_get (e, "action");
	Lv *accel = lv_map_get (e, "accel");
	Lv *icon = lv_map_get (e, "icon");
	Lv *sub = lv_map_get (e, "submenu");
	Lv *sec = lv_map_get (e, "section");
	const gchar *label = NULL;

	if (!check_keys (e, keys, "a menu entry", path, error))
		return NULL;
	if (label_lv != NULL && label_lv->kind != LV_NULL)
	{
		if (label_lv->kind != LV_SCALAR)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, label_lv,
			           "a menu 'label' must be text, got %s", lv_kind_name (label_lv));
			return NULL;
		}
		label = label_lv->str;
	}
	if (sub != NULL && sec != NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, e,
		           "a menu entry cannot be both a 'submenu' and a 'section'");
		return NULL;
	}
	if ((sub != NULL || sec != NULL) && (action != NULL || lv_map_get (e, "target") != NULL))
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, action != NULL ? action : e,
		           "a %s has no 'action' or 'target' (its items do)", sub != NULL ? "submenu" : "section");
		return NULL;
	}
	if (sec == NULL && label == NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, e,
		           "a menu %s needs a 'label'", sub != NULL ? "submenu" : "item");
		return NULL;
	}

	/* Submenus and sections link to a nested menu */
	if (sub != NULL || sec != NULL)
	{
		g_autofree gchar *sp = path_join (path, "%s", sub != NULL ? "submenu" : "section");
		g_autoptr(GMenu) inner = menu_from_lv (c, sub != NULL ? sub : sec, sp, depth + 1, error);

		if (inner == NULL)
			return NULL;
		item = sub != NULL ? g_menu_item_new_submenu (label, G_MENU_MODEL (inner))
		                   : g_menu_item_new_section (label, G_MENU_MODEL (inner));
	}
	else
	{
		item = g_menu_item_new (label, NULL);
		if (action != NULL)
		{
			if (action->kind != LV_SCALAR || !is_action_name (action->str))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, action,
				           "a menu 'action' must be \"prefix.action\"");
				return NULL;
			}
			if (!parse_action_target (lv_map_get (e, "target"), lv_map_get (e, "target-type"), &target, &msg))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, e, "%s", msg);
				return NULL;
			}
			g_menu_item_set_action_and_target_value (item, action->str, target);
		}
		else if (lv_map_get (e, "target") != NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, lv_map_get (e, "target"),
			           "a menu 'target' needs an 'action'");
			return NULL;
		}
	}

	/* Decorations: the accelerator label and a vector icon */
	if (accel != NULL)
	{
		if (accel->kind != LV_SCALAR || !lrg_accel_parse (accel->str, NULL, NULL))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, accel,
			           "invalid accelerator '%s'", accel->kind == LV_SCALAR ? accel->str : "?");
			return NULL;
		}
		g_menu_item_set_attribute (item, "accel", "s", accel->str);
	}
	if (icon != NULL)
	{
		GEnumClass *ec;
		gint value;

		if (!conv_enum (LRG_TYPE_ICON, icon, &value, &msg))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, icon, "menu 'icon': %s", msg);
			return NULL;
		}
		ec = g_type_class_ref (LRG_TYPE_ICON);
		g_menu_item_set_attribute (item, G_MENU_ATTRIBUTE_ICON, "s", g_enum_get_value (ec, value)->value_nick);
		g_type_class_unref (ec);
	}
	return g_steal_pointer (&item);
}

/*
 * menu_from_lv:
 * Builds a GMenu from a resolved menu value: a list of entries or the
 * name of a `menus:` entry. @depth guards against menus that use
 * themselves through names.
 */
static GMenu *
menu_from_lv (Ctx          *c,
              Lv           *v,
              const gchar  *path,
              guint         depth,
              GError      **error)
{
	g_autoptr(GMenu) menu = NULL;
	g_autoptr(GMenu) group = NULL;
	gboolean grouped = FALSE;
	guint i;

	if (depth > UI_MAX_MENU_DEPTH)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, v,
		           "menus nest too deeply (does a menu use itself?)");
		return NULL;
	}
	if (v->kind == LV_SCALAR)
		return menu_from_name (c, v, path, depth, error);
	if (v->kind == LV_NULL)
		return g_menu_new ();
	if (v->kind != LV_SEQ)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, v,
		           "a menu must be a list of entries or the name of a menu, got %s", lv_kind_name (v));
		return NULL;
	}

	/* Separators split the list into sections */
	for (i = 0; i < v->items->len && !grouped; i++)
	{
		Lv *e = g_ptr_array_index (v->items, i);

		grouped = is_separator_entry (e) ||
		          (e->kind == LV_MAP && lv_map_get (e, "separator") != NULL);
	}

	menu = g_menu_new ();
	group = grouped ? g_menu_new () : NULL;
	for (i = 0; i < v->items->len; i++)
	{
		Lv *e = g_ptr_array_index (v->items, i);
		g_autofree gchar *ep = path_join (path, "[%u]", i);
		g_autoptr(GMenuItem) item = NULL;
		gboolean sep;

		if (e->kind != LV_MAP && !is_separator_entry (e))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e,
			           "a menu entry must be a mapping or 'separator', got %s%s%s%s", lv_kind_name (e),
			           e->kind == LV_SCALAR ? " '" : "", e->kind == LV_SCALAR ? e->str : "",
			           e->kind == LV_SCALAR ? "'" : "");
			return NULL;
		}

		/* `if:` drops the entry */
		if (e->kind == LV_MAP && lv_map_get (e, "if") != NULL)
		{
			EVal ev = { 0 };
			gboolean keep;

			ev_from_lv (&ev, lv_map_get (e, "if"));
			keep = ev_truthy (&ev);
			ev_clear (&ev);
			if (!keep)
				continue;
		}

		sep = is_separator_entry (e);
		if (!sep && e->kind == LV_MAP && lv_map_get (e, "separator") != NULL)
		{
			gboolean b = FALSE;
			g_autofree gchar *msg = NULL;

			if (e->keys->len != 1 && !(e->keys->len == 2 && lv_map_get (e, "if") != NULL))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e,
				           "a separator entry takes no other keys");
				return NULL;
			}
			if (!conv_bool (lv_map_get (e, "separator"), &b, &msg))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "'separator': %s", msg);
				return NULL;
			}
			if (!b)
				continue;
			sep = TRUE;
		}
		if (sep)
		{
			if (g_menu_model_get_n_items (G_MENU_MODEL (group)) > 0)
			{
				g_menu_append_section (menu, NULL, G_MENU_MODEL (group));
				g_object_unref (group);
				group = g_menu_new ();
			}
			continue;
		}

		item = menu_item_from_lv (c, e, ep, depth, error);
		if (item == NULL)
			return NULL;
		g_menu_append_item (grouped ? group : menu, item);
	}
	if (grouped && g_menu_model_get_n_items (G_MENU_MODEL (group)) > 0)
		g_menu_append_section (menu, NULL, G_MENU_MODEL (group));
	return g_steal_pointer (&menu);
}

/*
 * menu_describe:
 * A canonical text form of a menu model (attributes and links, in
 * order), stored as the attribute's value so a hot reload re-applies a
 * menu only when its content changed, including through named menus.
 */
static void
menu_describe (GMenuModel *m,
               GString    *out)
{
	gint n;
	gint i;

	n = g_menu_model_get_n_items (m);
	g_string_append_c (out, '[');
	for (i = 0; i < n; i++)
	{
		g_autoptr(GMenuAttributeIter) ai = g_menu_model_iterate_item_attributes (m, i);
		g_autoptr(GMenuLinkIter) li = g_menu_model_iterate_item_links (m, i);
		const gchar *name;
		GVariant *value;
		GMenuModel *link;

		g_string_append_c (out, '(');
		while (g_menu_attribute_iter_get_next (ai, &name, &value))
		{
			g_autofree gchar *txt = g_variant_print (value, TRUE);

			g_string_append_printf (out, "%s=%s;", name, txt);
			g_variant_unref (value);
		}
		while (g_menu_link_iter_get_next (li, &name, &link))
		{
			g_string_append_printf (out, "%s:", name);
			menu_describe (link, out);
			g_object_unref (link);
		}
		g_string_append_c (out, ')');
	}
	g_string_append_c (out, ']');
}

/* register_menus: the top-level `menus:` mapping (name -> menu) */
static gboolean
register_menus (Ctx          *c,
                Lv           *menus,
                GError      **error)
{
	guint i;

	if (menus->kind != LV_MAP)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, "menus", menus,
		           "'menus' must map menu names to menus, got %s", lv_kind_name (menus));
		return FALSE;
	}
	for (i = 0; i < menus->keys->len; i++)
	{
		const gchar *name = g_ptr_array_index (menus->keys, i);

		g_hash_table_replace (c->menus, g_strdup (name), lv_ref (g_ptr_array_index (menus->items, i)));
	}
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Languages (LrgSyntax) for LrgTextEditor
 * -------------------------------------------------------------------------- */

/*
 * syntax_for_text:
 * Builds a language from definition text, reusing the language built
 * from identical text before (so reloading a document does not stack
 * up copies in the global registry).
 */
static LrgSyntax *
syntax_for_text (Ctx          *c,
                 const gchar  *text,
                 const Lv     *at,
                 const gchar  *path,
                 GError      **error)
{
	g_autoptr(GError) local = NULL;
	LrgSyntax *syn;

	syn = g_hash_table_lookup (c->self->syntax_cache, text);
	if (syn != NULL)
		return g_object_ref (syn);
	syn = lrg_syntax_new_from_yaml (text, -1, &local);
	if (syn == NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, at,
		           "invalid language definition: %s", local->message);
		return NULL;
	}
	g_hash_table_insert (c->self->syntax_cache, g_strdup (text), g_object_ref (syn));
	return syn;
}

/* syntax_from_file: a language definition file named in the document */
static LrgSyntax *
syntax_from_file (Ctx          *c,
                  Env          *env,
                  const gchar  *rel,
                  const Lv     *at,
                  const gchar  *path,
                  gchar       **contents_out,
                  GError      **error)
{
	g_autofree gchar *full = resolve_file_path (env, rel);
	g_autofree gchar *contents = NULL;
	LrgSyntax *syn;

	contents = read_doc_file (c, full, at, path, error);
	if (contents == NULL)
		return NULL;
	syn = syntax_for_text (c, contents, at, path, error);
	if (syn != NULL && contents_out != NULL)
		*contents_out = g_steal_pointer (&contents);
	return syn;
}

/*
 * register_languages:
 * The top-level `languages:` list: each entry is a language definition
 * (the LrgSyntax YAML schema), a file name, or {file: name}. The
 * languages are registered once the whole document expanded.
 */
static gboolean
register_languages (Ctx          *c,
                    Lv           *langs,
                    Env          *env,
                    GError      **error)
{
	g_autoptr(Lv) r = NULL;
	guint n;
	guint i;

	r = lv_resolve (langs, env->scope, "languages", error);
	if (r == NULL)
		return FALSE;
	if (r->kind != LV_SEQ && r->kind != LV_MAP && r->kind != LV_SCALAR)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, "languages", langs,
		           "'languages' must be a list of language definitions or file names, got %s",
		           lv_kind_name (r));
		return FALSE;
	}
	n = r->kind == LV_SEQ ? r->items->len : 1;
	for (i = 0; i < n; i++)
	{
		Lv *e = r->kind == LV_SEQ ? g_ptr_array_index (r->items, i) : r;
		g_autofree gchar *ep = g_strdup_printf ("languages[%u]", i);
		LrgSyntax *syn = NULL;
		Lv *file = NULL;

		if (e->kind == LV_SCALAR)
			file = e;
		else if (e->kind == LV_MAP && lv_map_get (e, "file") != NULL)
		{
			file = lv_map_get (e, "file");
			if (e->keys->len != 1 || file->kind != LV_SCALAR)
			{
				set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, ep, e,
				           "a language file entry is {file: name} and nothing else");
				return FALSE;
			}
		}
		if (file != NULL)
			syn = syntax_from_file (c, env, file->str, file, ep, NULL, error);
		else if (e->kind == LV_MAP)
		{
			g_autofree gchar *text = lv_to_yaml (e);

			syn = syntax_for_text (c, text, e, ep, error);
		}
		else
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, ep, e,
			           "a language entry must be a definition mapping or a file name, got %s",
			           lv_kind_name (e));
			return FALSE;
		}
		if (syn == NULL)
			return FALSE;
		g_ptr_array_add (c->syntaxes, syn);
	}
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Per-child settings: `anchor:` (LrgAnchorLayout), `page-name:`
 * (LrgSwitcher), `position:` / `relative-to:` (LrgDockPanel in an
 * LrgDockArea). They are attributes of the child node, checked against
 * the parent in node_check() and applied through the parent.
 * -------------------------------------------------------------------------- */

/* Reads [x, y] or {x:, y:} into two floats */
static gboolean
conv_pair (const Lv     *v,
           const gchar  *kx,
           const gchar  *ky,
           gfloat       *x,
           gfloat       *y,
           gchar       **msg)
{
	if (v->kind == LV_SEQ && v->items->len == 2)
		return conv_float (g_ptr_array_index (v->items, 0), x, msg) &&
		       conv_float (g_ptr_array_index (v->items, 1), y, msg);
	if (v->kind == LV_MAP)
	{
		Lv *a = lv_map_get (v, kx);
		Lv *b = lv_map_get (v, ky);

		return (a == NULL || conv_float (a, x, msg)) && (b == NULL || conv_float (b, y, msg));
	}
	if (v->kind == LV_SCALAR && conv_float (v, x, msg))
	{
		*y = *x;
		return TRUE;
	}
	*msg = g_strdup_printf ("expected [%s, %s], got %s", kx, ky, lv_kind_name (v));
	return FALSE;
}

/*
 * parse_anchor:
 * `anchor: top-right` (an LrgAnchorPreset nick) or a mapping:
 * {preset:, min: [x, y], max: [x, y], offset: [x, y], offsets: [l, t, r, b]
 *  (or {left:, top:, right:, bottom:}), size: [w, h], align: [x, y],
 *  auto-size:, z-order:}. The preset is applied first; `offset` works
 * like lrg_anchor_layout_add() (positive values move inwards from right
 * and bottom anchors); `size` fixes the size of pinned axes (auto-size
 * off).
 */
static gboolean
parse_anchor (const Lv       *v,
              LrgAnchorData  *d,
              gchar         **msg)
{
	static const gchar *keys[] = {
		"preset", "min", "max", "offset", "offsets", "size", "align", "auto-size", "z-order", NULL
	};
	Lv *x;
	gint preset;
	guint i;

	lrg_anchor_data_init (d);
	if (v->kind == LV_SCALAR)
	{
		if (!conv_enum (LRG_TYPE_ANCHOR_PRESET, v, &preset, msg))
			return FALSE;
		lrg_anchor_data_set_preset (d, (LrgAnchorPreset)preset);
		return TRUE;
	}
	if (v->kind != LV_MAP)
	{
		*msg = g_strdup_printf ("expected a preset name or a mapping, got %s", lv_kind_name (v));
		return FALSE;
	}
	for (i = 0; i < v->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (v->keys, i);
		guint j;
		gboolean found = FALSE;

		for (j = 0; keys[j] != NULL && !found; j++)
			found = key_equal (k, keys[j]);
		if (!found)
		{
			*msg = g_strdup_printf ("unknown key '%s' (expected preset, min, max, offset, offsets, size, "
			                        "align, auto-size, z-order)", k);
			return FALSE;
		}
	}

	/* Anchors and pivot: the preset, then explicit anchors */
	if ((x = lv_map_get (v, "preset")) != NULL)
	{
		if (!conv_enum (LRG_TYPE_ANCHOR_PRESET, x, &preset, msg))
			return FALSE;
		lrg_anchor_data_set_preset (d, (LrgAnchorPreset)preset);
	}
	if ((x = lv_map_get (v, "min")) != NULL && !conv_pair (x, "x", "y", &d->min_x, &d->min_y, msg))
		return FALSE;
	if ((x = lv_map_get (v, "max")) != NULL && !conv_pair (x, "x", "y", &d->max_x, &d->max_y, msg))
		return FALSE;
	if ((x = lv_map_get (v, "align")) != NULL && !conv_pair (x, "x", "y", &d->align_x, &d->align_y, msg))
		return FALSE;

	/* Offsets: the lrg_anchor_layout_add() shorthand, or all four */
	if ((x = lv_map_get (v, "offset")) != NULL)
	{
		gfloat ox = 0.0f;
		gfloat oy = 0.0f;

		if (!conv_pair (x, "x", "y", &ox, &oy, msg))
			return FALSE;
		if (d->max_x > d->min_x)
			d->left = d->right = ox;
		else
			d->left = d->min_x >= 1.0f ? -ox : ox;
		if (d->max_y > d->min_y)
			d->top = d->bottom = oy;
		else
			d->top = d->min_y >= 1.0f ? -oy : oy;
	}
	if ((x = lv_map_get (v, "offsets")) != NULL)
	{
		LrgInsets in;

		if (!conv_insets (x, &in, msg))
		{
			g_autofree gchar *m = *msg;

			*msg = g_strdup_printf ("'offsets': %s", m);
			return FALSE;
		}
		d->left = in.left;
		d->top = in.top;
		d->right = in.right;
		d->bottom = in.bottom;
	}
	if ((x = lv_map_get (v, "size")) != NULL)
	{
		if (!conv_pair (x, "width", "height", &d->right, &d->bottom, msg))
			return FALSE;
		d->auto_size = FALSE;
	}
	if ((x = lv_map_get (v, "auto-size")) != NULL && !conv_bool (x, &d->auto_size, msg))
		return FALSE;
	if ((x = lv_map_get (v, "z-order")) != NULL)
	{
		gdouble z;

		if (!conv_integer (x, G_MININT, G_MAXINT, &z, msg))
			return FALSE;
		d->z_order = (gint)z;
	}
	return TRUE;
}

/* Name of the parent type a per-child attribute needs */
static GType
child_attr_parent_type (const UiAttr *a)
{
	switch (a->kind)
	{
	case ATTR_ANCHOR:
		return LRG_TYPE_ANCHOR_LAYOUT;
	case ATTR_PAGE_NAME:
		return LRG_TYPE_SWITCHER;
	case ATTR_PACK:
		return LRG_TYPE_HEADER_BAR;
	case ATTR_DOCK_PLACE:
		return LRG_TYPE_DOCK_AREA;
	default:
		return G_TYPE_INVALID;
	}
}

/*
 * check_child_attrs:
 * Every per-child attribute of @child must fit its parent @parent_type
 * (G_TYPE_INVALID for content set through a property or a tab).
 */
static gboolean
check_child_attrs (GType         parent_type,
                   UiNode       *child,
                   GError      **error)
{
	guint i;

	for (i = 0; i < child->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (child->attrs, i);
		GType need;

		if (!a->child)
			continue;
		need = child_attr_parent_type (a);
		if (parent_type == G_TYPE_INVALID || !g_type_is_a (parent_type, need))
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, child->path, a->value,
			           "%s only valid on a child of %s%s%s",
			           a->kind == ATTR_DOCK_PLACE ? "'position' and 'relative-to' are" :
			           a->kind == ATTR_ANCHOR ? "'anchor' is" :
			           a->kind == ATTR_PACK ? "'pack' is" : "'page-name' is",
			           g_type_name (need),
			           parent_type != G_TYPE_INVALID ? ", not of " : "",
			           parent_type != G_TYPE_INVALID ? g_type_name (parent_type) : "");
			return FALSE;
		}
	}
	return TRUE;
}

/* Applies one per-child attribute through @parent (no-op elsewhere) */
static void
child_attr_apply (LrgContainer *parent,
                  LrgWidget    *w,
                  UiAttr       *a)
{
	if (parent == NULL)
		return;
	switch (a->kind)
	{
	case ATTR_ANCHOR:
		if (LRG_IS_ANCHOR_LAYOUT (parent))
			lrg_anchor_layout_set_child_data (LRG_ANCHOR_LAYOUT (parent), w, &a->anchor);
		break;
	case ATTR_PAGE_NAME:
		if (LRG_IS_SWITCHER (parent))
			lrg_switcher_set_child_name (LRG_SWITCHER (parent), w, a->text);
		break;
	case ATTR_PACK:
		/* `title` makes the child the bar's centred title widget;
		 * start/end move it between the two packing groups */
		if (LRG_IS_HEADER_BAR (parent))
		{
			LrgHeaderBar *bar = LRG_HEADER_BAR (parent);

			if (a->ival == 2)
				lrg_header_bar_set_title_widget (bar, w);
			else
			{
				if (lrg_header_bar_get_title_widget (bar) == w)
				{
					/* Was the title: put it back as an ordinary child.
					 * Unsetting the title removes it from the bar, which
					 * may hold the only reference: keep it alive. */
					g_autoptr(LrgWidget) keep = g_object_ref (w);

					lrg_header_bar_set_title_widget (bar, NULL);
					if (lrg_widget_get_parent (w) == NULL)
						lrg_container_add_child (parent, w);
				}
				lrg_header_bar_set_child_packing (bar, w, a->ival == 1);
			}
		}
		break;
	case ATTR_DOCK_PLACE:
		/* The placement only matters when the panel is docked */
	default:
		break;
	}
}

/* Puts a per-child setting back to its default */
static void
child_attr_reset (LrgContainer *parent,
                  LrgWidget    *w,
                  UiAttr       *a)
{
	LrgAnchorData d;

	if (parent == NULL)
		return;
	switch (a->kind)
	{
	case ATTR_ANCHOR:
		if (LRG_IS_ANCHOR_LAYOUT (parent))
		{
			lrg_anchor_data_init (&d);
			lrg_anchor_layout_set_child_data (LRG_ANCHOR_LAYOUT (parent), w, &d);
		}
		break;
	case ATTR_PAGE_NAME:
		if (LRG_IS_SWITCHER (parent))
			lrg_switcher_set_child_name (LRG_SWITCHER (parent), w, NULL);
		break;
	case ATTR_PACK:
		if (LRG_IS_HEADER_BAR (parent))
		{
			LrgHeaderBar *bar = LRG_HEADER_BAR (parent);

			if (lrg_header_bar_get_title_widget (bar) == w)
			{
				g_autoptr(LrgWidget) keep = g_object_ref (w);

				lrg_header_bar_set_title_widget (bar, NULL);
				if (lrg_widget_get_parent (w) == NULL)
					lrg_container_add_child (parent, w);
			}
			lrg_header_bar_set_child_packing (bar, w, FALSE);
		}
		break;
	default:
		break;
	}
}

/* --------------------------------------------------------------------------
 * LrgTimelineView `tracks:` and `markers:`
 * -------------------------------------------------------------------------- */

typedef struct
{
	gdouble   start;
	gdouble   duration;
	gchar    *label;
	gboolean  has_color;
	GrlColor  color;
} ClipSpec;

typedef struct
{
	gdouble  time;
	gdouble  value;
	gint     interpolation;
	gboolean has_tangents;
	gdouble  in_slope;
	gdouble  out_slope;
	gint     shape;        /* LrgCanvasKeyShape, AUTO unless `shape:` */
} KeySpec;

typedef struct
{
	gchar     *name;
	GPtrArray *props;      /* UiAttr * (ATTR_PROP on LrgTimelineViewTrack) */
	GArray    *clips;      /* ClipSpec */
	GArray    *keys;       /* KeySpec */
	GPtrArray *children;   /* TrackSpec * */
} TrackSpec;

typedef struct
{
	gdouble  time;
	gchar   *label;
} MarkerSpec;

static void
clip_spec_clear (gpointer data)
{
	g_free (((ClipSpec *)data)->label);
}

static void
track_spec_free (gpointer data)
{
	TrackSpec *t = data;

	g_free (t->name);
	g_ptr_array_unref (t->props);
	g_array_unref (t->clips);
	g_array_unref (t->keys);
	g_ptr_array_unref (t->children);
	g_free (t);
}

static void
marker_spec_free (gpointer data)
{
	MarkerSpec *m = data;

	g_free (m->label);
	g_free (m);
}

/* Reads a number member of a mapping, or element @idx of a list */
static gboolean
spec_number (const Lv     *v,
             const gchar  *key,
             guint         idx,
             gboolean      required,
             gdouble      *out,
             gchar       **msg)
{
	Lv *x = NULL;

	if (v->kind == LV_MAP)
		x = lv_map_get (v, key);
	else if (v->kind == LV_SEQ && idx < v->items->len)
		x = g_ptr_array_index (v->items, idx);
	if (x == NULL)
	{
		if (required)
			*msg = g_strdup_printf ("'%s' is missing", key);
		return !required;
	}
	if (!conv_number (x, out, msg))
	{
		g_autofree gchar *m = *msg;

		*msg = g_strdup_printf ("'%s': %s", key, m);
		return FALSE;
	}
	return TRUE;
}

/* Reads a text member of a mapping, or element @idx of a list */
static gboolean
spec_text (const Lv     *v,
           const gchar  *key,
           guint         idx,
           gchar       **out,
           gchar       **msg)
{
	Lv *x = NULL;

	if (v->kind == LV_MAP)
		x = lv_map_get (v, key);
	else if (v->kind == LV_SEQ && idx < v->items->len)
		x = g_ptr_array_index (v->items, idx);
	if (x == NULL || x->kind == LV_NULL)
		return TRUE;
	if (x->kind != LV_SCALAR)
	{
		*msg = g_strdup_printf ("'%s' must be text, got %s", key, lv_kind_name (x));
		return FALSE;
	}
	*out = g_strdup (x->str);
	return TRUE;
}

/* Rejects unknown keys of a mapping entry; lists are positional */
static gboolean
spec_check_keys (const Lv     *v,
                 const gchar **allowed,
                 const gchar  *what,
                 guint         max_len,
                 gchar       **msg)
{
	guint i;
	guint j;

	if (v->kind == LV_SEQ)
	{
		if (v->items->len < 2 || v->items->len > max_len)
		{
			*msg = g_strdup_printf ("a %s list has 2 to %u elements, got %u", what, max_len, v->items->len);
			return FALSE;
		}
		return TRUE;
	}
	if (v->kind != LV_MAP)
	{
		*msg = g_strdup_printf ("a %s must be a mapping or a list, got %s", what, lv_kind_name (v));
		return FALSE;
	}
	for (i = 0; i < v->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (v->keys, i);
		gboolean found = FALSE;

		for (j = 0; allowed[j] != NULL && !found; j++)
			found = key_equal (k, allowed[j]);
		if (!found)
		{
			g_autofree gchar *list = g_strjoinv (", ", (gchar **)allowed);

			*msg = g_strdup_printf ("unknown %s key '%s' (expected %s)", what, k, list);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean parse_tracks (Lv *list, const gchar *path, GPtrArray *out, guint depth, GError **error);

/*
 * parse_track:
 * One entry of `tracks:`: {name:, tracks: [...], clips: [...],
 * keyframes: [...], <any LrgTimelineViewTrack property>}.
 * Clips are {start:, duration:, label:, color:} or [start, duration,
 * label]; keyframes {time:, value:, interpolation:, tangents: [in, out]}
 * or [time, value, interpolation].
 */
static TrackSpec *
parse_track (Lv           *v,
             const gchar  *path,
             guint         depth,
             GError      **error)
{
	static const gchar *clip_keys[] = { "start", "duration", "label", "color", NULL };
	static const gchar *key_keys[] = { "time", "value", "interpolation", "tangents", "shape", NULL };
	g_autofree gchar *msg = NULL;
	TrackSpec *t;
	GObjectClass *klass;
	guint i;

	if (v->kind != LV_MAP)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, v,
		           "a track must be a mapping, got %s", lv_kind_name (v));
		return NULL;
	}
	t = g_new0 (TrackSpec, 1);
	t->props = g_ptr_array_new_with_free_func (ui_attr_free);
	t->clips = g_array_new (FALSE, TRUE, sizeof (ClipSpec));
	g_array_set_clear_func (t->clips, clip_spec_clear);
	t->keys = g_array_new (FALSE, TRUE, sizeof (KeySpec));
	t->children = g_ptr_array_new_with_free_func (track_spec_free);

	klass = g_type_class_ref (LRG_TYPE_TIMELINE_VIEW_TRACK);
	for (i = 0; i < v->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (v->keys, i);
		Lv *x = g_ptr_array_index (v->items, i);
		g_autofree gchar *kp = path_join (path, "%s", k);

		/* --- nested tracks ------------------------------------------- */
		if (key_equal (k, "tracks"))
		{
			if (!parse_tracks (x, path, t->children, depth + 1, error))
				goto fail;
			continue;
		}

		/* --- clips ---------------------------------------------------- */
		if (key_equal (k, "clips") || key_equal (k, "keyframes"))
		{
			gboolean clips = key_equal (k, "clips");
			guint j;

			if (x->kind != LV_SEQ)
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, kp, x,
				           "'%s' must be a list, got %s", k, lv_kind_name (x));
				goto fail;
			}
			for (j = 0; j < x->items->len; j++)
			{
				Lv *e = g_ptr_array_index (x->items, j);
				g_autofree gchar *ep = path_join (kp, "[%u]", j);
				gboolean ok;

				if (clips)
				{
					ClipSpec cs = { 0 };
					Lv *col;

					ok = spec_check_keys (e, clip_keys, "clip", 3, &msg) &&
					     spec_number (e, "start", 0, TRUE, &cs.start, &msg) &&
					     spec_number (e, "duration", 1, TRUE, &cs.duration, &msg) &&
					     spec_text (e, "label", 2, &cs.label, &msg);
					if (ok && cs.duration <= 0.0)
					{
						msg = g_strdup ("'duration' must be positive");
						ok = FALSE;
					}
					col = e->kind == LV_MAP ? lv_map_get (e, "color") : NULL;
					if (ok && col != NULL)
					{
						ok = conv_color (col, &cs.color, &msg);
						cs.has_color = ok;
					}
					if (!ok)
					{
						g_free (cs.label);
						set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "%s", msg);
						goto fail;
					}
					g_array_append_val (t->clips, cs);
				}
				else
				{
					KeySpec ks = { 0 };
					Lv *ip = NULL;
					Lv *tan;

					ks.interpolation = LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR;
					ok = spec_check_keys (e, key_keys, "keyframe", 3, &msg) &&
					     spec_number (e, "time", 0, TRUE, &ks.time, &msg) &&
					     spec_number (e, "value", 1, TRUE, &ks.value, &msg);
					if (ok)
						ip = e->kind == LV_MAP ? lv_map_get (e, "interpolation") :
						     (e->items->len > 2 ? g_ptr_array_index (e->items, 2) : NULL);
					if (ok && ip != NULL)
						ok = conv_enum (LRG_TYPE_TIMELINE_VIEW_INTERPOLATION, ip, &ks.interpolation, &msg);
					tan = e->kind == LV_MAP ? lv_map_get (e, "tangents") : NULL;
					if (ok && tan != NULL)
					{
						gfloat a = 0.0f;
						gfloat b = 0.0f;

						ok = conv_pair (tan, "in", "out", &a, &b, &msg);
						ks.has_tangents = ok;
						ks.in_slope = a;
						ks.out_slope = b;
					}

					/* Per-key shape: beats the timeline's key-shape and the look */
					ks.shape = LRG_CANVAS_KEY_SHAPE_AUTO;
					if (ok && e->kind == LV_MAP && lv_map_get (e, "shape") != NULL)
						ok = conv_enum (LRG_TYPE_CANVAS_KEY_SHAPE, lv_map_get (e, "shape"), &ks.shape, &msg);
					if (!ok)
					{
						set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "%s", msg);
						goto fail;
					}
					g_array_append_val (t->keys, ks);
				}
			}
			continue;
		}

		/* --- the name and the track's properties ---------------------- */
		{
			g_autofree gchar *pname = g_strdelimit (g_strdup (k), "_", '-');
			GParamSpec *pspec = g_object_class_find_property (klass, pname);
			UiAttr *a;

			if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE) ||
			    g_type_is_a (G_PARAM_SPEC_VALUE_TYPE (pspec), G_TYPE_OBJECT))
			{
				set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY, kp, x,
				           "unknown track key '%s' (expected name, tracks, clips, keyframes or a "
				           "writable LrgTimelineViewTrack property)", k);
				goto fail;
			}
			a = ui_attr_new (ATTR_PROP, pspec->name, x);
			a->pspec = pspec;
			if (!conv_property (pspec, x, &a->gvalue, &msg))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, kp, x,
				           "invalid value for track property '%s': %s", k, msg);
				ui_attr_free (a);
				goto fail;
			}
			if (g_strcmp0 (pspec->name, "name") == 0)
			{
				g_free (t->name);
				t->name = g_value_dup_string (&a->gvalue);
			}
			g_ptr_array_add (t->props, a);
		}
	}
	g_type_class_unref (klass);
	return t;

fail:
	g_type_class_unref (klass);
	track_spec_free (t);
	return NULL;
}

static gboolean
parse_tracks (Lv           *list,
              const gchar  *path,
              GPtrArray    *out,
              guint         depth,
              GError      **error)
{
	guint i;

	if (depth > 32)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, list, "tracks nest too deeply");
		return FALSE;
	}
	if (list->kind != LV_SEQ)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, list,
		           "'tracks' must be a list of tracks, got %s", lv_kind_name (list));
		return FALSE;
	}
	for (i = 0; i < list->items->len; i++)
	{
		g_autofree gchar *tp = path_join (path, "tracks[%u]", i);
		TrackSpec *t = parse_track (g_ptr_array_index (list->items, i), tp, depth, error);

		if (t == NULL)
			return FALSE;
		g_ptr_array_add (out, t);
	}
	return TRUE;
}

/* `markers:`: a list of {time:, label:} or [time, label] */
static gboolean
parse_markers (Lv           *list,
               const gchar  *path,
               GPtrArray    *out,
               GError      **error)
{
	static const gchar *keys[] = { "time", "label", NULL };
	guint i;

	if (list->kind != LV_SEQ)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, list,
		           "'markers' must be a list of {time:, label:}, got %s", lv_kind_name (list));
		return FALSE;
	}
	for (i = 0; i < list->items->len; i++)
	{
		Lv *e = g_ptr_array_index (list->items, i);
		g_autofree gchar *ep = path_join (path, "markers[%u]", i);
		g_autofree gchar *msg = NULL;
		MarkerSpec *m = g_new0 (MarkerSpec, 1);

		if (e->kind == LV_SCALAR)
		{
			if (!conv_number (e, &m->time, &msg))
			{
				marker_spec_free (m);
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "%s", msg);
				return FALSE;
			}
		}
		else if (!spec_check_keys (e, keys, "marker", 2, &msg) ||
		         !spec_number (e, "time", 0, TRUE, &m->time, &msg) ||
		         !spec_text (e, "label", 1, &m->label, &msg))
		{
			marker_spec_free (m);
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "%s", msg);
			return FALSE;
		}
		g_ptr_array_add (out, m);
	}
	return TRUE;
}

static GQuark
timeline_tracks_quark (void)
{
	return g_quark_from_static_string ("lrg-ui-loader-tracks");
}

static GQuark
timeline_markers_quark (void)
{
	return g_quark_from_static_string ("lrg-ui-loader-markers");
}

/* TRUE when @track is (still) one of @tv's tracks */
static gboolean
timeline_has_track (LrgTimelineView      *tv,
                    LrgTimelineViewTrack *track)
{
	guint n = lrg_timeline_view_get_n_tracks (tv);
	guint i;

	for (i = 0; i < n; i++)
	{
		if (lrg_timeline_view_get_track (tv, i) == track)
			return TRUE;
	}
	return FALSE;
}

/* Removes the top-level tracks the loader added (with everything on them) */
static void
timeline_clear_tracks (LrgTimelineView *tv)
{
	GPtrArray *made = g_object_steal_qdata (G_OBJECT (tv), timeline_tracks_quark ());
	guint i;

	if (made == NULL)
		return;
	for (i = 0; i < made->len; i++)
	{
		LrgTimelineViewTrack *t = g_ptr_array_index (made, i);

		if (timeline_has_track (tv, t))
			lrg_timeline_view_remove_track (tv, t);
	}
	g_ptr_array_unref (made);
}

static void
timeline_clear_markers (LrgTimelineView *tv)
{
	GArray *ids = g_object_steal_qdata (G_OBJECT (tv), timeline_markers_quark ());
	guint i;

	if (ids == NULL)
		return;
	for (i = 0; i < ids->len; i++)
		lrg_timeline_view_remove_marker (tv, g_array_index (ids, guint, i));
	g_array_unref (ids);
}

/* Creates the tracks of @specs (nested in @parent) with their content */
static void
timeline_add_tracks (LrgTimelineView      *tv,
                     GPtrArray            *specs,
                     LrgTimelineViewTrack *parent,
                     GPtrArray            *made)
{
	guint i;
	guint j;

	for (i = 0; i < specs->len; i++)
	{
		TrackSpec *s = g_ptr_array_index (specs, i);
		g_autoptr(LrgTimelineViewTrack) track = lrg_timeline_view_track_new (s->name);

		for (j = 0; j < s->props->len; j++)
		{
			UiAttr *a = g_ptr_array_index (s->props, j);

			g_object_set_property (G_OBJECT (track), a->pspec->name, &a->gvalue);
		}
		lrg_timeline_view_append_track (tv, track, parent);
		if (made != NULL)
			g_ptr_array_add (made, g_object_ref (track));

		for (j = 0; j < s->clips->len; j++)
		{
			ClipSpec *cs = &g_array_index (s->clips, ClipSpec, j);
			guint id = lrg_timeline_view_add_clip (tv, track, cs->start, cs->duration, cs->label);

			if (cs->has_color)
				lrg_timeline_view_set_clip_color (tv, id, &cs->color);
		}
		for (j = 0; j < s->keys->len; j++)
		{
			KeySpec *ks = &g_array_index (s->keys, KeySpec, j);
			guint id = lrg_timeline_view_add_keyframe (tv, track, ks->time, ks->value,
			                                           (LrgTimelineViewInterpolation)ks->interpolation);

			if (ks->has_tangents)
				lrg_timeline_view_set_keyframe_tangents (tv, id, ks->in_slope, ks->out_slope);
			if (ks->shape != LRG_CANVAS_KEY_SHAPE_AUTO)
				lrg_timeline_view_set_keyframe_shape (tv, id, (LrgCanvasKeyShape) ks->shape);
		}
		timeline_add_tracks (tv, s->children, track, NULL);
	}
}

/* Replaces the loader's tracks (or markers) of @tv by @a's */
static void
timeline_apply (LrgTimelineView *tv,
                UiAttr          *a)
{
	guint i;

	if (a->kind == ATTR_TRACKS)
	{
		GPtrArray *made = g_ptr_array_new_with_free_func (g_object_unref);

		timeline_clear_tracks (tv);
		timeline_add_tracks (tv, a->specs, NULL, made);
		g_object_set_qdata_full (G_OBJECT (tv), timeline_tracks_quark (), made,
		                         (GDestroyNotify)g_ptr_array_unref);
	}
	else
	{
		GArray *ids = g_array_new (FALSE, FALSE, sizeof (guint));

		timeline_clear_markers (tv);
		for (i = 0; i < a->specs->len; i++)
		{
			MarkerSpec *m = g_ptr_array_index (a->specs, i);
			guint id = lrg_timeline_view_add_marker (tv, m->time, m->label);

			g_array_append_val (ids, id);
		}
		g_object_set_qdata_full (G_OBJECT (tv), timeline_markers_quark (), ids,
		                         (GDestroyNotify)g_array_unref);
	}
}

/* --------------------------------------------------------------------------
 * Embedded documents: `graph:` / `graph-file:` (LrgNodeGraph) and
 * `layout:` (LrgDockArea). They are checked during expansion by loading
 * them into a throwaway widget, so a bad document fails before anything
 * is built.
 * -------------------------------------------------------------------------- */

/* Probes a block editor document (palette and/or program) */
static gboolean
check_blocks_text (const gchar  *text,
                   const Lv     *at,
                   const gchar  *path,
                   GError      **error)
{
	g_autoptr(LrgBlockEditor) probe = lrg_block_editor_new ();
	g_autoptr(GError) local = NULL;

	if (g_object_is_floating (probe))
		g_object_ref_sink (probe);
	if (!lrg_block_editor_load_yaml (probe, text, -1, &local))
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, at, "invalid 'blocks': %s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* --------------------------------------------------------------------------
 * LrgFileBrowser `location:`, `filters:` and `places:`
 * -------------------------------------------------------------------------- */

typedef struct
{
	gchar   *label;
	gchar   *path;   /* "~" expanded */
	LrgIcon  icon;
} PlaceSpec;

static void
place_spec_free (gpointer data)
{
	PlaceSpec *p = data;

	g_free (p->label);
	g_free (p->path);
	g_free (p);
}

/* expand_home: "~" and "~/x" become paths under the home folder */
static gchar *
expand_home (const gchar *path)
{
	if (path[0] == '~' && (path[1] == '\0' || path[1] == '/'))
		return g_build_filename (g_get_home_dir (), path + 1, NULL);
	return g_strdup (path);
}

/* text_list: a scalar or a list of scalars as a NULL-terminated vector */
static gboolean
text_list (const Lv     *x,
           const gchar  *key,
           GStrv        *out,
           gchar       **msg)
{
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
	guint i;

	if (x->kind == LV_SCALAR)
		g_strv_builder_add (b, x->str);
	else if (x->kind == LV_SEQ)
	{
		for (i = 0; i < x->items->len; i++)
		{
			const Lv *e = g_ptr_array_index (x->items, i);

			if (e->kind != LV_SCALAR)
			{
				*msg = g_strdup_printf ("'%s' must be text or a list of text", key);
				return FALSE;
			}
			g_strv_builder_add (b, e->str);
		}
	}
	else
	{
		*msg = g_strdup_printf ("'%s' must be text or a list of text, got %s", key, lv_kind_name (x));
		return FALSE;
	}
	*out = g_strv_builder_end (b);
	return TRUE;
}

/* Parses `filters:` into LrgFileFilter objects: each entry is
 * {name, patterns, suffixes, mime-types} (each list may be one text) */
static GPtrArray *
parse_file_filters (const Lv     *v,
                    const gchar  *path,
                    GError      **error)
{
	static const gchar *keys[] = { "name", "patterns", "suffixes", "mime-types", NULL };
	g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func (g_object_unref);
	guint i;

	if (v->kind != LV_SEQ)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, v,
		           "'filters' must be a list of filters, got %s", lv_kind_name (v));
		return NULL;
	}
	for (i = 0; i < v->items->len; i++)
	{
		const Lv *e = g_ptr_array_index (v->items, i);
		g_autofree gchar *ep = path_join (path, "[%u]", i);
		g_autofree gchar *msg = NULL;
		g_autofree gchar *name = NULL;
		g_autoptr(LrgFileFilter) f = NULL;
		guint k;

		if (e->kind != LV_MAP)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e,
			           "a filter must be a mapping {name, patterns, suffixes, mime-types}");
			return NULL;
		}
		if (!spec_check_keys (e, keys, "filter", 0, &msg) || !spec_text (e, "name", 0, &name, &msg))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "%s", msg);
			return NULL;
		}
		if (name == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "a filter needs a 'name'");
			return NULL;
		}
		f = lrg_file_filter_new (name);
		for (k = 1; keys[k] != NULL; k++)
		{
			const Lv *x = lv_map_get (e, keys[k]);
			g_auto(GStrv) list = NULL;
			guint j;

			if (x == NULL)
				continue;
			if (!text_list (x, keys[k], &list, &msg))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, x, "%s", msg);
				return NULL;
			}
			for (j = 0; list[j] != NULL; j++)
			{
				if (k == 1)
					lrg_file_filter_add_pattern (f, list[j]);
				else if (k == 2)
					lrg_file_filter_add_suffix (f, list[j]);
				else
					lrg_file_filter_add_mime_type (f, list[j]);
			}
		}
		g_ptr_array_add (out, g_steal_pointer (&f));
	}
	return g_steal_pointer (&out);
}

/* Parses `places:` entries {label, path, icon} */
static GPtrArray *
parse_file_places (const Lv     *v,
                   const gchar  *path,
                   GError      **error)
{
	static const gchar *keys[] = { "label", "path", "icon", NULL };
	g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func (place_spec_free);
	guint i;

	if (v->kind != LV_SEQ)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, v,
		           "'places' must be a list of places, got %s", lv_kind_name (v));
		return NULL;
	}
	for (i = 0; i < v->items->len; i++)
	{
		const Lv *e = g_ptr_array_index (v->items, i);
		g_autofree gchar *ep = path_join (path, "[%u]", i);
		g_autofree gchar *msg = NULL;
		g_autofree gchar *label = NULL;
		g_autofree gchar *where = NULL;
		gint icon = LRG_ICON_FOLDER;

		if (e->kind != LV_MAP)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e,
			           "a place must be a mapping {label, path, icon}");
			return NULL;
		}
		if (!spec_check_keys (e, keys, "place", 0, &msg) || !spec_text (e, "label", 0, &label, &msg) ||
		    !spec_text (e, "path", 1, &where, &msg) ||
		    (lv_map_get (e, "icon") != NULL && !conv_enum (LRG_TYPE_ICON, lv_map_get (e, "icon"), &icon, &msg)))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "%s", msg);
			return NULL;
		}
		if (label == NULL || where == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, ep, e, "a place needs a 'label' and a 'path'");
			return NULL;
		}
		{
			PlaceSpec *ps = g_new0 (PlaceSpec, 1);

			ps->label = g_steal_pointer (&label);
			ps->path = expand_home (where);
			ps->icon = (LrgIcon) icon;
			g_ptr_array_add (out, ps);
		}
	}
	return g_steal_pointer (&out);
}

/* What the loader added to a browser, so a reload replaces only that and
 * leaves filters and places the application added alone */
#define FB_LOADER_FILTERS "lrg-ui-loader-filters"
#define FB_LOADER_PLACES  "lrg-ui-loader-places"

static void
fb_clear_loader_filters (LrgFileBrowser *fb)
{
	GPtrArray *old = g_object_get_data (G_OBJECT (fb), FB_LOADER_FILTERS);
	guint i;

	if (old == NULL)
		return;
	for (i = 0; i < old->len; i++)
		lrg_file_browser_remove_filter (fb, g_ptr_array_index (old, i));
	g_object_set_data (G_OBJECT (fb), FB_LOADER_FILTERS, NULL);
}

static void
fb_clear_loader_places (LrgFileBrowser *fb)
{
	GPtrArray *old = g_object_get_data (G_OBJECT (fb), FB_LOADER_PLACES);
	guint i;

	if (old == NULL)
		return;
	for (i = 0; i < old->len; i++)
		lrg_file_browser_remove_place (fb, g_ptr_array_index (old, i));
	g_object_set_data (G_OBJECT (fb), FB_LOADER_PLACES, NULL);
}

static void
fb_apply_filters (LrgFileBrowser *fb,
                  GPtrArray      *filters)
{
	GPtrArray *added = g_ptr_array_new_with_free_func (g_object_unref);
	guint i;

	fb_clear_loader_filters (fb);
	for (i = 0; i < filters->len; i++)
	{
		LrgFileFilter *f = g_ptr_array_index (filters, i);

		lrg_file_browser_add_filter (fb, f);
		g_ptr_array_add (added, g_object_ref (f));
	}
	if (filters->len > 0)
		lrg_file_browser_set_filter (fb, g_ptr_array_index (filters, 0));
	g_object_set_data_full (G_OBJECT (fb), FB_LOADER_FILTERS, added, (GDestroyNotify) g_ptr_array_unref);
}

static void
fb_apply_places (LrgFileBrowser *fb,
                 GPtrArray      *places)
{
	GPtrArray *added = g_ptr_array_new_with_free_func (g_object_unref);
	guint i;

	fb_clear_loader_places (fb);
	for (i = 0; i < places->len; i++)
	{
		PlaceSpec *ps = g_ptr_array_index (places, i);
		g_autoptr(GFile) file = g_file_new_for_path (ps->path);

		lrg_file_browser_add_place (fb, ps->label, file, ps->icon);
		g_ptr_array_add (added, g_steal_pointer (&file));
	}
	g_object_set_data_full (G_OBJECT (fb), FB_LOADER_PLACES, added, (GDestroyNotify) g_ptr_array_unref);
}

static gboolean
check_graph_text (const gchar  *text,
                  const Lv     *at,
                  const gchar  *path,
                  GError      **error)
{
	g_autoptr(LrgNodeGraph) probe = lrg_node_graph_new ();
	g_autoptr(GError) local = NULL;

	if (g_object_is_floating (probe))
		g_object_ref_sink (probe);
	if (!lrg_node_graph_load_yaml (probe, text, -1, &local))
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, at, "invalid 'graph': %s", local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
check_layout_text (const gchar  *text,
                   const Lv     *at,
                   const gchar  *path,
                   GError      **error)
{
	g_autoptr(LrgDockArea) probe = lrg_dock_area_new ();
	g_autoptr(GError) local = NULL;

	if (g_object_is_floating (probe))
		g_object_ref_sink (probe);
	if (!lrg_dock_area_load_layout (probe, text, &local))
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, at, "invalid 'layout': %s", local->message);
		return FALSE;
	}
	return TRUE;
}

/*
 * parse_aspect_ratio:
 * "16:9" or "16/9" for an `aspect-ratio` property; gives a number
 * scalar, or NULL when @v is not in that form (it is then converted as
 * an ordinary number).
 */
static Lv *
parse_aspect_ratio (const Lv *v)
{
	g_auto(GStrv) parts = NULL;
	gdouble w;
	gdouble h;
	g_autofree gchar *num = NULL;

	if (v->kind != LV_SCALAR || strpbrk (v->str, ":/") == NULL)
		return NULL;
	parts = g_strsplit_set (v->str, ":/", -1);
	if (g_strv_length (parts) != 2)
		return NULL;
	g_strstrip (parts[0]);
	g_strstrip (parts[1]);
	if (!parse_number_text (parts[0], &w) || !parse_number_text (parts[1], &h) || h <= 0.0)
		return NULL;
	num = num_to_string (w / h);
	return lv_new_scalar (num, FALSE, v);
}

/*
 * node_set_widget_key:
 * The keys added for the wave-3 widgets: menu models (`model:`,
 * `context-menu:`), per-child placement (`anchor:`, `page-name:`,
 * `position:` / `relative-to:`), `language-file:`, `graph:` /
 * `graph-file:`, `layout:`, `tracks:` and `markers:`. Sets *@handled to
 * FALSE when @key is none of them.
 */
static gboolean
node_set_widget_key (Ctx           *c,
                     UiNode        *node,
                     GObjectClass  *klass,
                     const gchar   *key,
                     Lv            *val,
                     Lv            *r,
                     Env           *env,
                     const gchar   *path,
                     gboolean      *handled,
                     GError       **error)
{
	g_autofree gchar *msg = NULL;
	g_autofree gchar *pname = g_strdelimit (g_strdup (key), "_", '-');
	GParamSpec *pspec = g_object_class_find_property (klass, pname);
	UiAttr *a;

	*handled = TRUE;

	/* --- GMenuModel properties: a menu (list) or a `menus:` name ------- */
	if (pspec != NULL && (pspec->flags & G_PARAM_WRITABLE) &&
	    g_type_is_a (G_PARAM_SPEC_VALUE_TYPE (pspec), G_TYPE_MENU_MODEL))
	{
		g_autofree gchar *mp = path_join (path, "%s", pspec->name);
		g_autoptr(GMenu) menu = menu_from_lv (c, r, mp, 0, error);
		g_autoptr(Lv) desc_lv = NULL;
		GString *desc;

		if (menu == NULL)
			return FALSE;
		desc = g_string_new (NULL);
		menu_describe (G_MENU_MODEL (menu), desc);
		desc_lv = lv_new_scalar (desc->str, TRUE, val);
		g_string_free (desc, TRUE);
		a = ui_attr_new (ATTR_PROP, pspec->name, desc_lv);
		a->pspec = pspec;
		g_value_init (&a->gvalue, G_PARAM_SPEC_VALUE_TYPE (pspec));
		g_value_set_object (&a->gvalue, menu);
		a->construct = (pspec->flags & G_PARAM_CONSTRUCT_ONLY) != 0;
		ui_node_put_attr (node, a);
		return TRUE;
	}
	if (pspec == NULL && key_equal (key, "context-menu"))
	{
		set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY, path, val,
		           "'context-menu' needs the LrgWidget:context-menu property, which %s does not have",
		           g_type_name (node->type));
		return FALSE;
	}

	/* --- `aspect-ratio: "16:9"` ---------------------------------------- */
	if (pspec != NULL && g_strcmp0 (pspec->name, "aspect-ratio") == 0 &&
	    (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_FLOAT || G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_DOUBLE))
	{
		g_autoptr(Lv) num = parse_aspect_ratio (r);

		if (num != NULL)
		{
			a = ui_attr_new (ATTR_PROP, pspec->name, r);
			a->pspec = pspec;
			if (!conv_property (pspec, num, &a->gvalue, &msg))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
				           "invalid value for property '%s' of %s: %s", key, g_type_name (node->type), msg);
				ui_attr_free (a);
				return FALSE;
			}
			ui_node_put_attr (node, a);
			return TRUE;
		}
		*handled = FALSE;
		return TRUE;
	}

	/* Everything below is a key that is not a property of the type */
	if (pspec != NULL)
	{
		*handled = FALSE;
		return TRUE;
	}

	/* --- per-child placement ----------------------------------------- */
	if (key_equal (key, "anchor"))
	{
		a = ui_attr_new (ATTR_ANCHOR, "anchor", r);
		a->child = TRUE;
		if (!parse_anchor (r, &a->anchor, &msg))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val, "'anchor': %s", msg);
			ui_attr_free (a);
			return FALSE;
		}
		ui_node_put_attr (node, a);
		return TRUE;
	}
	if (key_equal (key, "page-name"))
	{
		if (r->kind != LV_SCALAR)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'page-name' must be text, got %s", lv_kind_name (r));
			return FALSE;
		}
		a = ui_attr_new (ATTR_PAGE_NAME, "page-name", r);
		a->child = TRUE;
		a->text = g_strdup (r->str);
		ui_node_put_attr (node, a);
		return TRUE;
	}
	if (key_equal (key, "pack"))
	{
		static const gchar *packs[] = { "start", "end", "title", NULL };
		guint p;

		for (p = 0; packs[p] != NULL; p++)
			if (r->kind == LV_SCALAR && g_ascii_strcasecmp (r->str, packs[p]) == 0)
				break;
		if (packs[p] == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'pack' must be start, end or title");
			return FALSE;
		}
		a = ui_attr_new (ATTR_PACK, "pack", r);
		a->child = TRUE;
		a->ival = (gint) p;
		ui_node_put_attr (node, a);
		return TRUE;
	}
	if ((key_equal (key, "position") || key_equal (key, "relative-to")) &&
	    g_type_is_a (node->type, LRG_TYPE_DOCK_PANEL))
	{
		UiAttr *old = ui_node_find_attr (node, "dock-place");
		g_autoptr(Lv) both = lv_new (LV_MAP, val);

		/* One attribute holds both keys; its value is {position, relative-to} */
		a = ui_attr_new (ATTR_DOCK_PLACE, "dock-place", NULL);
		a->child = TRUE;
		a->ival = old != NULL ? old->ival : (gint)LRG_DOCK_POSITION_CENTER;
		a->text = old != NULL ? g_strdup (old->text) : NULL;
		if (old != NULL)
		{
			guint i;

			for (i = 0; i < old->value->keys->len; i++)
			{
				if (!key_equal (g_ptr_array_index (old->value->keys, i), key))
					lv_map_add (both, g_ptr_array_index (old->value->keys, i),
					            lv_ref (g_ptr_array_index (old->value->items, i)));
			}
		}
		lv_map_add (both, key_equal (key, "position") ? "position" : "relative-to", lv_ref (r));
		lv_unref (a->value);
		a->value = lv_ref (both);
		if (key_equal (key, "position"))
		{
			if (!conv_enum (LRG_TYPE_DOCK_POSITION, r, &a->ival, &msg))
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val, "'position': %s", msg);
				ui_attr_free (a);
				return FALSE;
			}
		}
		else
		{
			if (r->kind != LV_SCALAR)
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
				           "'relative-to' must be a panel id, got %s", lv_kind_name (r));
				ui_attr_free (a);
				return FALSE;
			}
			g_free (a->text);
			a->text = g_strdup (r->str);
		}
		ui_node_put_attr (node, a);
		return TRUE;
	}

	/* --- LrgTextEditor `language-file:` -------------------------------- */
	if (key_equal (key, "language-file") && g_type_is_a (node->type, LRG_TYPE_TEXT_EDITOR))
	{
		g_autofree gchar *contents = NULL;
		g_autoptr(LrgSyntax) syn = NULL;
		g_autoptr(Lv) stamp = NULL;
		GParamSpec *sp = g_object_class_find_property (klass, "syntax");

		if (r->kind != LV_SCALAR || *r->str == '\0')
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'language-file' must be a file name");
			return FALSE;
		}
		syn = syntax_from_file (c, env, r->str, val, path, &contents, error);
		if (syn == NULL)
			return FALSE;
		g_ptr_array_add (c->syntaxes, g_object_ref (syn));

		/* The value is the file's text: editing the file re-applies it */
		stamp = lv_new_scalar (contents, TRUE, val);
		a = ui_attr_new (ATTR_PROP, "syntax", stamp);
		a->pspec = sp;
		g_value_init (&a->gvalue, LRG_TYPE_SYNTAX);
		g_value_set_object (&a->gvalue, syn);
		ui_node_put_attr (node, a);
		return TRUE;
	}

	/* --- LrgBlockEditor `blocks:` / `blocks-file:` --------------------- */
	if ((key_equal (key, "blocks") || key_equal (key, "blocks-file")) &&
	    g_type_is_a (node->type, LRG_TYPE_BLOCK_EDITOR))
	{
		g_autofree gchar *text = NULL;
		g_autoptr(Lv) stamp = NULL;

		if (key_equal (key, "blocks-file"))
		{
			g_autofree gchar *full = NULL;

			if (r->kind != LV_SCALAR || *r->str == '\0')
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
				           "'blocks-file' must be a file name");
				return FALSE;
			}
			full = resolve_file_path (env, r->str);
			text = read_doc_file (c, full, val, path, error);
			if (text == NULL)
				return FALSE;
		}
		else if (r->kind == LV_MAP)
			text = lv_to_yaml (r);
		else if (r->kind == LV_SCALAR && r->quoted)
			text = g_strdup (r->str);
		else
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'blocks' must be a block editor document (a mapping), got %s", lv_kind_name (r));
			return FALSE;
		}
		if (!check_blocks_text (text, val, path, error))
			return FALSE;
		stamp = lv_new_scalar (text, TRUE, val);
		a = ui_attr_new (ATTR_BLOCKS, "blocks", stamp);
		a->text = g_steal_pointer (&text);
		a->late = TRUE;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	/* --- LrgNodeGraph `graph:` / `graph-file:` ------------------------- */
	if ((key_equal (key, "graph") || key_equal (key, "graph-file")) &&
	    g_type_is_a (node->type, LRG_TYPE_NODE_GRAPH))
	{
		g_autofree gchar *text = NULL;
		g_autoptr(Lv) stamp = NULL;

		if (key_equal (key, "graph-file"))
		{
			g_autofree gchar *full = NULL;

			if (r->kind != LV_SCALAR || *r->str == '\0')
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
				           "'graph-file' must be a file name");
				return FALSE;
			}
			full = resolve_file_path (env, r->str);
			text = read_doc_file (c, full, val, path, error);
			if (text == NULL)
				return FALSE;
		}
		else if (r->kind == LV_MAP)
			text = lv_to_yaml (r);
		else if (r->kind == LV_SCALAR && r->quoted)
			text = g_strdup (r->str);
		else
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'graph' must be a graph document (a mapping), got %s", lv_kind_name (r));
			return FALSE;
		}
		if (!check_graph_text (text, val, path, error))
			return FALSE;
		stamp = lv_new_scalar (text, TRUE, val);
		a = ui_attr_new (ATTR_GRAPH, "graph", stamp);
		a->text = g_steal_pointer (&text);
		a->late = TRUE;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	/* --- LrgDockArea `layout:` ------------------------------------------ */
	if (key_equal (key, "layout") && g_type_is_a (node->type, LRG_TYPE_DOCK_AREA))
	{
		g_autofree gchar *text = NULL;

		if (r->kind == LV_MAP)
			text = lv_to_yaml (r);
		else if (r->kind == LV_SCALAR)
			text = g_strdup (r->str);
		else
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'layout' must be a saved layout (a mapping or its text), got %s", lv_kind_name (r));
			return FALSE;
		}
		if (!check_layout_text (text, val, path, error))
			return FALSE;
		a = ui_attr_new (ATTR_DOCK_LAYOUT, "layout", r);
		a->text = g_steal_pointer (&text);
		a->late = TRUE;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	/* --- LrgTimelineView `tracks:` / `markers:` --------------------------- */
	if ((key_equal (key, "tracks") || key_equal (key, "markers")) &&
	    g_type_is_a (node->type, LRG_TYPE_TIMELINE_VIEW))
	{
		gboolean tracks = key_equal (key, "tracks");
		GPtrArray *specs;

		specs = g_ptr_array_new_with_free_func (tracks ? track_spec_free : marker_spec_free);
		if (!(tracks ? parse_tracks (r, path, specs, 0, error) : parse_markers (r, path, specs, error)))
		{
			g_ptr_array_unref (specs);
			return FALSE;
		}
		a = ui_attr_new (tracks ? ATTR_TRACKS : ATTR_MARKERS, tracks ? "tracks" : "markers", r);
		a->specs = specs;
		a->late = TRUE;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	*handled = FALSE;
	return TRUE;
}

/*
 * node_set_key:
 * Applies one key of a widget mapping (or a component use-site override)
 * to @node. Values are interpolated in @env; children are expanded in
 * @env. @merge is TRUE for overrides: class lists and `on:` entries are
 * merged with the existing ones instead of replacing them.
 */
static gboolean
node_set_key (Ctx           *c,
              UiNode        *node,
              GObjectClass  *klass,
              const gchar   *key,
              Lv            *val,
              Env           *env,
              const gchar   *path,
              gboolean       merge,
              GError       **error)
{
	LrgUILoader *self = c->self;
	g_autofree gchar *msg = NULL;
	g_autoptr(Lv) r = NULL;
	UiAttr *a;

	/* --- Structure: children, child, tabs ------------------------------ */
	if (key_equal (key, "children"))
	{
		g_ptr_array_set_size (node->children, 0);
		node->child_prop = NULL;
		return expand_list (c, val, env, path, "children", node->children, error);
	}
	if (key_equal (key, "child"))
	{
		UiNode *one;

		one = expand_one (c, val, env, path, "child", error);
		if (one == NULL)
			return FALSE;
		g_ptr_array_set_size (node->children, 0);
		g_ptr_array_add (node->children, one);
		if (type_has_widget_property (klass, "child"))
			node->child_prop = "child";
		else if (type_has_widget_property (klass, "content"))
			node->child_prop = "content";
		else
			node->child_prop = NULL;
		return TRUE;
	}
	if (key_equal (key, "tabs"))
	{
		guint i;

		if (!g_type_is_a (node->type, LRG_TYPE_TAB_VIEW))
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, val,
			           "'tabs' is only valid on LrgTabView, not %s", g_type_name (node->type));
			return FALSE;
		}
		if (val->kind != LV_SEQ)
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, val,
			           "'tabs' must be a list of {label:, content:}, got %s", lv_kind_name (val));
			return FALSE;
		}
		g_ptr_array_set_size (node->tabs, 0);
		node->has_tabs = TRUE;
		for (i = 0; i < val->items->len; i++)
		{
			Lv *t = g_ptr_array_index (val->items, i);
			g_autofree gchar *tp = path_join (path, "tabs[%u]", i);
			Lv *cond;
			UiTab *tab;
			guint k;

			if (t->kind != LV_MAP)
			{
				set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, tp, t,
				           "a tab must be a mapping {label:, content:}, got %s", lv_kind_name (t));
				return FALSE;
			}
			for (k = 0; k < t->keys->len; k++)
			{
				const gchar *tk = g_ptr_array_index (t->keys, k);

				if (!key_equal (tk, "label") && !key_equal (tk, "content") && !key_equal (tk, "if"))
				{
					set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, tp, g_ptr_array_index (t->items, k),
					           "unknown tab key '%s' (expected label, content, if)", tk);
					return FALSE;
				}
			}
			cond = lv_map_get (t, "if");
			if (cond != NULL)
			{
				EVal ev = { 0 };
				gboolean keep;

				if (!resolve_ev (cond, env->scope, tp, &ev, error))
					return FALSE;
				keep = ev_truthy (&ev);
				ev_clear (&ev);
				if (!keep)
					continue;
			}
			if (lv_map_get (t, "content") == NULL)
			{
				set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, tp, t, "a tab needs 'content'");
				return FALSE;
			}
			tab = g_new0 (UiTab, 1);
			tab->label = lv_map_get (t, "label") != NULL ?
			             resolve_string (lv_map_get (t, "label"), env->scope, tp, "label", error) :
			             g_strdup ("");
			if (tab->label == NULL)
			{
				ui_tab_free (tab);
				return FALSE;
			}
			tab->content = expand_one (c, lv_map_get (t, "content"), env, tp, "content", error);
			if (tab->content == NULL)
			{
				ui_tab_free (tab);
				return FALSE;
			}
			g_ptr_array_add (node->tabs, tab);
		}
		return TRUE;
	}

	/* --- Everything else is a value: interpolate it first ------------- */
	r = lv_resolve (val, env->scope, path, error);
	if (r == NULL)
		return FALSE;

	if (key_equal (key, "id"))
	{
		if (r->kind != LV_SCALAR)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'id' must be text, got %s", lv_kind_name (r));
			return FALSE;
		}
		g_free (node->id);
		node->id = g_strdup (r->str);
		return TRUE;
	}

	if (key_equal (key, "class"))
	{
		GStrv words;
		UiAttr *old;

		words = parse_word_list (r, TRUE, &msg);
		if (words == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val, "'class': %s", msg);
			return FALSE;
		}
		old = merge ? ui_node_find_attr (node, "class") : NULL;
		if (old != NULL)
		{
			g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
			guint i;

			g_strv_builder_addv (b, (const gchar **)old->strv);
			for (i = 0; words[i] != NULL; i++)
			{
				if (!g_strv_contains ((const gchar * const *)old->strv, words[i]))
					g_strv_builder_add (b, words[i]);
			}
			g_strfreev (words);
			words = g_strv_builder_end (b);
		}
		{
			g_autoptr(Lv) lv = lv_from_strv (words, val);

			a = ui_attr_new (ATTR_CLASS, "class", lv);
		}
		a->strv = words;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	if (key_equal (key, "items"))
	{
		GStrv items;

		if (!g_type_is_a (node->type, LRG_TYPE_COMBO_BOX))
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, val,
			           "'items' is only valid on LrgComboBox, not %s", g_type_name (node->type));
			return FALSE;
		}
		items = parse_word_list (r, FALSE, &msg);
		if (items == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val, "'items': %s", msg);
			return FALSE;
		}
		a = ui_attr_new (ATTR_ITEMS, "items", r);
		a->strv = items;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	/* --- LrgFileBrowser `location:`, `filters:`, `places:` (before the
	 * property lookup: `location` is also a GFile property) ------------ */
	if (g_type_is_a (node->type, LRG_TYPE_FILE_BROWSER) &&
	    (key_equal (key, "location") || key_equal (key, "filters") || key_equal (key, "places")))
	{
		g_autofree gchar *kp = path_join (path, "%s", key);

		if (key_equal (key, "location"))
		{
			if (r->kind != LV_SCALAR || *r->str == '\0')
			{
				set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, kp, val,
				           "'location' must be a folder path");
				return FALSE;
			}
			a = ui_attr_new (ATTR_FB_LOCATION, "location", r);
			a->text = expand_home (r->str);
		}
		else if (key_equal (key, "filters"))
		{
			GPtrArray *filters = parse_file_filters (r, kp, error);

			if (filters == NULL)
				return FALSE;
			a = ui_attr_new (ATTR_FB_FILTERS, "filters", r);
			a->specs = filters;
		}
		else
		{
			GPtrArray *places = parse_file_places (r, kp, error);

			if (places == NULL)
				return FALSE;
			a = ui_attr_new (ATTR_FB_PLACES, "places", r);
			a->specs = places;
		}
		a->late = TRUE;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	if (key_equal (key, "on"))
		return put_handlers (self, node, r, path, merge, error);

	/* `on-<signal>: action-or-handler`, one signal per key; merged with
	 * `on:` and with each other */
	if (key[0] == 'o' && key[1] == 'n' && (key[2] == '-' || key[2] == '_') && key[3] != '\0' &&
	    g_object_class_find_property (klass, key) == NULL)
	{
		g_autoptr(Lv) one = lv_new (LV_MAP, r);
		g_autofree gchar *sig = g_strdelimit (g_strdup (key + 3), "_", '-');

		lv_map_add (one, sig, lv_ref (r));
		return put_handlers (self, node, one, path, TRUE, error);
	}

	/* `look:` is the per-part look map, except that a scalar on a widget
	 * with a `look` property of its own (LrgNodeGraph, LrgTimelineView:
	 * `look: blueprint`) sets that property; it falls through to the
	 * ordinary property handling below */
	if (key_equal (key, "look") &&
	    !(r->kind == LV_SCALAR && g_object_class_find_property (klass, "look") != NULL))
	{
		g_autoptr(GArray) looks = g_array_new (FALSE, TRUE, sizeof (LookEntry));

		if (!parse_look (r, looks, &msg))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val, "'look': %s", msg);
			return FALSE;
		}
		a = ui_attr_new (ATTR_LOOK, "look", r);
		a->looks = g_steal_pointer (&looks);
		ui_node_put_attr (node, a);
		return TRUE;
	}

	if (key_equal (key, "appear"))
	{
		Lv *kind_lv;
		Lv *delay_lv = NULL;
		gint kind;
		gfloat delay = 0.0f;

		kind_lv = r;
		if (r->kind == LV_MAP)
		{
			guint i;

			kind_lv = lv_map_get (r, "kind");
			delay_lv = lv_map_get (r, "delay");
			for (i = 0; i < r->keys->len; i++)
			{
				const gchar *k = g_ptr_array_index (r->keys, i);

				if (!key_equal (k, "kind") && !key_equal (k, "delay"))
				{
					set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
					           "'appear': unknown key '%s' (expected kind, delay)", k);
					return FALSE;
				}
			}
		}
		if (!conv_enum (LRG_TYPE_APPEAR, kind_lv, &kind, &msg) ||
		    (delay_lv != NULL && !conv_float (delay_lv, &delay, &msg)))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val, "'appear': %s", msg);
			return FALSE;
		}
		a = ui_attr_new (ATTR_APPEAR, "appear", r);
		a->appear = kind;
		a->f[0] = delay;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	if (key_equal (key, "min-size") || key_equal (key, "max-size"))
	{
		gboolean is_min = key[1] == 'i';
		gfloat w = 0.0f;
		gfloat h = 0.0f;
		gboolean ok;

		if (r->kind == LV_SEQ && r->items->len == 2)
			ok = conv_float (g_ptr_array_index (r->items, 0), &w, &msg) &&
			     conv_float (g_ptr_array_index (r->items, 1), &h, &msg);
		else if (r->kind == LV_MAP)
		{
			Lv *wl = lv_map_get (r, "width");
			Lv *hl = lv_map_get (r, "height");

			ok = (wl == NULL || conv_float (wl, &w, &msg)) && (hl == NULL || conv_float (hl, &h, &msg));
		}
		else
		{
			ok = FALSE;
			msg = g_strdup_printf ("expected [width, height], got %s", lv_kind_name (r));
		}
		if (!ok)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "'%s': %s", is_min ? "min-size" : "max-size", msg);
			return FALSE;
		}
		a = ui_attr_new (is_min ? ATTR_MIN_SIZE : ATTR_MAX_SIZE, is_min ? "min-size" : "max-size", r);
		a->f[0] = w;
		a->f[1] = h;
		ui_node_put_attr (node, a);
		return TRUE;
	}

	/* --- Keys of the menu, layout, editor, graph, timeline and dock widgets */
	{
		gboolean handled = FALSE;

		if (!node_set_widget_key (c, node, klass, key, val, r, env, path, &handled, error))
			return FALSE;
		if (handled)
			return TRUE;
	}

	/* --- A GObject property ------------------------------------------- */
	{
		g_autofree gchar *pname = NULL;
		GParamSpec *pspec;

		pname = g_strdelimit (g_strdup (key), "_", '-');
		pspec = g_object_class_find_property (klass, pname);
		if (pspec == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY, path, val,
			           "unknown property '%s' on %s", key, g_type_name (node->type));
			return FALSE;
		}
		if (!(pspec->flags & G_PARAM_WRITABLE))
		{
			set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY, path, val,
			           "property '%s' of %s is read-only", key, g_type_name (node->type));
			return FALSE;
		}
		a = ui_attr_new (ATTR_PROP, pspec->name, r);
		a->pspec = pspec;
		if (!conv_property (pspec, r, &a->gvalue, &msg))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, val,
			           "invalid value for property '%s' of %s: %s", key, g_type_name (node->type), msg);
			ui_attr_free (a);
			return FALSE;
		}
		a->construct = (pspec->flags & G_PARAM_CONSTRUCT_ONLY) != 0;
		a->late = is_late_property (pspec->name);
		ui_node_put_attr (node, a);
		return TRUE;
	}
}

/*
 * check_dock_children:
 * An LrgDockArea holds LrgDockPanel nodes only, and a panel's
 * `relative-to:` must name a panel listed before it.
 */
static gboolean
check_dock_children (UiNode       *node,
                     GError      **error)
{
	guint i;
	guint j;

	for (i = 0; i < node->children->len; i++)
	{
		UiNode *cn = g_ptr_array_index (node->children, i);
		UiAttr *place;
		gboolean found = FALSE;

		if (!g_type_is_a (cn->type, LRG_TYPE_DOCK_PANEL))
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, cn->path, cn->src,
			           "the children of LrgDockArea must be dock-panel nodes, not %s", g_type_name (cn->type));
			return FALSE;
		}
		place = ui_node_find_attr (cn, "dock-place");
		if (place == NULL || place->text == NULL)
			continue;
		for (j = 0; j < i && !found; j++)
			found = g_strcmp0 (((UiNode *)g_ptr_array_index (node->children, j))->id, place->text) == 0;
		if (!found)
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, cn->path, place->value,
			           "'relative-to: %s' names no panel listed before this one", place->text);
			return FALSE;
		}
	}
	return TRUE;
}

/* Checks a finished node: children only where they can go */
static gboolean
node_check (UiNode       *node,
            const gchar  *path,
            GError      **error)
{
	guint i;

	/* Per-child settings must fit the parent they end up in */
	for (i = 0; i < node->children->len; i++)
	{
		if (!check_child_attrs (node->child_prop != NULL ? G_TYPE_INVALID : node->type,
		                        g_ptr_array_index (node->children, i), error))
			return FALSE;
	}
	for (i = 0; i < node->tabs->len; i++)
	{
		if (!check_child_attrs (G_TYPE_INVALID, ((UiTab *)g_ptr_array_index (node->tabs, i))->content, error))
			return FALSE;
	}
	if (g_type_is_a (node->type, LRG_TYPE_DOCK_AREA) && !check_dock_children (node, error))
		return FALSE;

	if (node->children->len == 0)
		return TRUE;
	if (node->child_prop != NULL)
		return TRUE;
	if (!g_type_is_a (node->type, LRG_TYPE_CONTAINER))
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, node->src,
		           "%s cannot have children", g_type_name (node->type));
		return FALSE;
	}
	if (g_type_is_a (node->type, LRG_TYPE_TAB_VIEW))
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, node->src,
		           "give LrgTabView its pages with 'tabs', not 'children'");
		return FALSE;
	}
	if (g_type_is_a (node->type, LRG_TYPE_SCROLL_BOX))
	{
		if (node->children->len != 1)
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, node->src,
			           "LrgScrollBox takes exactly one child, got %u", node->children->len);
			return FALSE;
		}
		node->child_prop = "child";
	}
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Components
 * -------------------------------------------------------------------------- */

static gboolean
is_param (const Component *comp,
          const gchar     *key)
{
	return comp->params != NULL && lv_map_get (comp->params, key) != NULL;
}

static void
slot_fill_clear (SlotFill *f)
{
	g_clear_pointer (&f->default_nodes, g_ptr_array_unref);
	g_clear_pointer (&f->named, g_hash_table_unref);
	g_clear_pointer (&f->named_used, g_hash_table_unref);
}

/*
 * expand_component:
 * Instantiates @comp for the use site @use. Parameters are evaluated in
 * the caller's scope (defaults in the component's own scope), the body
 * is expanded in a fresh scope whose parent is the global scope, the
 * use site's children fill the slots, and the use site's other keys
 * override the body's root.
 */
static UiNode *
expand_component (Ctx          *c,
                  Component    *comp,
                  Lv           *use,
                  Env          *env,
                  const gchar  *path,
                  GError      **error)
{
	g_autoptr(GPtrArray) tmp = NULL;
	g_autofree gchar *body_path = NULL;
	SlotFill fill;
	Scope *scope;
	Env body_env;
	UiNode *root = NULL;
	GObjectClass *klass;
	EVal ev = { 0 };
	guint i;
	gboolean ok = FALSE;

	/* Recursion guard */
	for (i = 0; i < c->comp_stack->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (c->comp_stack, i), comp->name) == 0)
		{
			GString *chain = g_string_new (NULL);
			guint j;

			for (j = 0; j < c->comp_stack->len; j++)
				g_string_append_printf (chain, "%s -> ", (const gchar *)g_ptr_array_index (c->comp_stack, j));
			g_string_append (chain, comp->name);
			set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, use,
			           "component '%s' uses itself (%s)", comp->name, chain->str);
			g_string_free (chain, TRUE);
			return NULL;
		}
	}

	memset (&fill, 0, sizeof (fill));
	scope = scope_new (c->globals);

	/* A number unique to this instance, to build unique ids */
	ev_set_num (&ev, (gdouble)c->instance++);
	scope_set (scope, "instance", &ev);
	ev_clear (&ev);

	/* Use-site keys must be parameters or keys that a widget understands;
	 * the latter are checked when they are applied to the root below. */

	/* Parameters */
	if (comp->params != NULL)
	{
		for (i = 0; i < comp->params->keys->len; i++)
		{
			const gchar *pname = g_ptr_array_index (comp->params->keys, i);
			Lv *given = lv_map_get (use, pname);

			if (given != NULL)
				ok = resolve_ev (given, env->scope, path, &ev, error);
			else
				ok = resolve_ev (g_ptr_array_index (comp->params->items, i), scope, path, &ev, error);
			if (!ok)
				goto out;
			scope_set (scope, pname, &ev);
			ev_clear (&ev);
		}
	}
	ok = FALSE;

	/* Slot fills come from the caller's environment */
	if (lv_map_get (use, "children") != NULL)
	{
		fill.default_nodes = g_ptr_array_new_with_free_func ((GDestroyNotify)ui_node_unref);
		if (!expand_list (c, lv_map_get (use, "children"), env, path, "children", fill.default_nodes, error))
			goto out;
	}
	if (lv_map_get (use, "slots") != NULL)
	{
		Lv *slots = lv_map_get (use, "slots");

		if (slots->kind != LV_MAP)
		{
			set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, slots,
			           "'slots' must map slot names to node lists, got %s", lv_kind_name (slots));
			goto out;
		}
		fill.named = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
		for (i = 0; i < slots->keys->len; i++)
		{
			g_autofree gchar *sp = path_join (path, "slots.%s", (const gchar *)g_ptr_array_index (slots->keys, i));
			GPtrArray *nodes = g_ptr_array_new_with_free_func ((GDestroyNotify)ui_node_unref);

			g_hash_table_insert (fill.named, g_strdup (g_ptr_array_index (slots->keys, i)), nodes);
			if (!expand_list (c, g_ptr_array_index (slots->items, i), env, sp, "", nodes, error))
				goto out;
		}
	}
	fill.named_used = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* Body */
	body_env.scope = scope;
	body_env.slots = &fill;
	body_env.file = comp->file;
	body_path = path_join (path, "<%s>", comp->name);
	tmp = g_ptr_array_new_with_free_func ((GDestroyNotify)ui_node_unref);
	g_ptr_array_add (c->comp_stack, comp->name);
	ok = expand_item (c, comp->body, &body_env, body_path, tmp, error);
	g_ptr_array_remove_index (c->comp_stack, c->comp_stack->len - 1);
	if (!ok)
		goto out;
	ok = FALSE;
	if (tmp->len != 1)
	{
		set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, comp->body,
		           "the body of component '%s' must give exactly one widget, got %u", comp->name, tmp->len);
		goto out;
	}
	root = ui_node_ref (g_ptr_array_index (tmp, 0));

	/* Unused fills: children go to the root when it can hold them */
	if (fill.default_nodes != NULL && fill.default_nodes->len > 0 && !fill.default_used)
	{
		if (!g_type_is_a (root->type, LRG_TYPE_CONTAINER) || g_type_is_a (root->type, LRG_TYPE_TAB_VIEW))
		{
			set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, use,
			           "component '%s' has no slot for children and its root %s cannot hold them",
			           comp->name, g_type_name (root->type));
			goto out;
		}
		for (i = 0; i < fill.default_nodes->len; i++)
			g_ptr_array_add (root->children, ui_node_ref (g_ptr_array_index (fill.default_nodes, i)));
	}
	if (fill.named != NULL)
	{
		GHashTableIter it;
		gpointer key;

		g_hash_table_iter_init (&it, fill.named);
		while (g_hash_table_iter_next (&it, &key, NULL))
		{
			if (!g_hash_table_contains (fill.named_used, key))
			{
				set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, lv_map_get (use, "slots"),
				           "component '%s' has no slot '%s'", comp->name, (const gchar *)key);
				goto out;
			}
		}
	}

	/* Use-site overrides on the root (in the caller's environment) */
	klass = g_type_class_ref (root->type);
	ok = TRUE;
	for (i = 0; i < use->keys->len && ok; i++)
	{
		const gchar *k = g_ptr_array_index (use->keys, i);

		if (key_equal (k, "type") || key_equal (k, "if") || key_equal (k, "children") ||
		    key_equal (k, "slots") || is_param (comp, k))
			continue;
		if (key_equal (k, "child") || key_equal (k, "tabs"))
		{
			set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, g_ptr_array_index (use->items, i),
			           "'%s' cannot be given where component '%s' is used (use slots)", k, comp->name);
			ok = FALSE;
			break;
		}
		ok = node_set_key (c, root, klass, k, g_ptr_array_index (use->items, i), env, path, TRUE, error);
		if (!ok && error != NULL && *error != NULL &&
		    (*error)->code == LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY &&
		    g_object_class_find_property (klass, k) == NULL)
		{
			/* Say what the component accepts, not only what its root does */
			g_autofree gchar *old = g_strdup ((*error)->message);

			g_clear_error (error);
			set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY, path, g_ptr_array_index (use->items, i),
			           "'%s' is neither a parameter of component '%s' nor a property of its root %s",
			           k, comp->name, g_type_name (root->type));
		}
	}
	g_type_class_unref (klass);
	if (ok)
		ok = node_check (root, path, error);

out:
	ev_clear (&ev);
	slot_fill_clear (&fill);
	scope_free (scope);
	if (!ok)
	{
		if (root != NULL)
			ui_node_unref (root);
		return NULL;
	}
	return root;
}

/* --------------------------------------------------------------------------
 * Widget nodes
 * -------------------------------------------------------------------------- */

static UiNode *
expand_widget (Ctx          *c,
               Lv           *map,
               Env          *env,
               const gchar  *path,
               GError      **error)
{
	g_autofree gchar *type_name = NULL;
	Component *comp;
	UiNode *node;
	GObjectClass *klass;
	GType type;
	guint i;

	type_name = resolve_string (lv_map_get (map, "type"), env->scope, path, "type", error);
	if (type_name == NULL)
		return NULL;

	comp = g_hash_table_lookup (c->components, type_name);
	if (comp != NULL)
		return expand_component (c, comp, map, env, path, error);

	type = resolve_type (c->self, type_name);
	if (type == G_TYPE_INVALID)
	{
		set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_TYPE, path, lv_map_get (map, "type"),
		           "unknown widget type or component '%s'", type_name);
		return NULL;
	}
	if (!g_type_is_a (type, LRG_TYPE_WIDGET))
	{
		set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_TYPE, path, lv_map_get (map, "type"),
		           "'%s' (%s) is not a widget type", type_name, g_type_name (type));
		return NULL;
	}
	if (G_TYPE_IS_ABSTRACT (type))
	{
		set_error (error, LRG_UI_LOADER_ERROR_UNKNOWN_TYPE, path, lv_map_get (map, "type"),
		           "'%s' is abstract", g_type_name (type));
		return NULL;
	}

	node = ui_node_new (type, path, map);
	klass = g_type_class_ref (type);
	for (i = 0; i < map->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (map->keys, i);

		if (key_equal (k, "type") || key_equal (k, "if"))
			continue;
		if (key_equal (k, "slots"))
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, g_ptr_array_index (map->items, i),
			           "'slots' is only valid where a component is used");
			goto fail;
		}
		if (!node_set_key (c, node, klass, k, g_ptr_array_index (map->items, i), env, path, FALSE, error))
			goto fail;
	}
	if (!node_check (node, path, error))
		goto fail;
	g_type_class_unref (klass);
	return node;

fail:
	g_type_class_unref (klass);
	ui_node_unref (node);
	return NULL;
}

/* --------------------------------------------------------------------------
 * Structural keys
 * -------------------------------------------------------------------------- */

/* Rejects keys other than @allowed in a structural mapping */
static gboolean
check_keys (const Lv     *map,
            const gchar **allowed,
            const gchar  *what,
            const gchar  *path,
            GError      **error)
{
	guint i;
	guint j;

	for (i = 0; i < map->keys->len; i++)
	{
		const gchar *k = g_ptr_array_index (map->keys, i);
		gboolean found = FALSE;

		for (j = 0; allowed[j] != NULL && !found; j++)
			found = key_equal (k, allowed[j]);
		if (!found)
		{
			g_autofree gchar *list = g_strjoinv (", ", (gchar **)allowed);

			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, g_ptr_array_index (map->items, i),
			           "unknown key '%s' in %s (expected %s)", k, what, list);
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * expand_repeat:
 * `repeat: {count: N, var: i, body: ...}`, or the short form
 * `repeat: N` with `var:` and `body:` beside it.
 */
static gboolean
expand_repeat (Ctx          *c,
               Lv           *item,
               Env          *env,
               const gchar  *path,
               GPtrArray    *out,
               GError      **error)
{
	static const gchar *long_keys[] = { "count", "var", "body", NULL };
	static const gchar *short_keys[] = { "repeat", "var", "body", "if", NULL };
	Lv *spec;
	Lv *count_lv;
	Lv *body;
	g_autofree gchar *var = NULL;
	EVal ev = { 0 };
	gdouble n;
	guint i;

	spec = lv_map_get (item, "repeat");
	if (spec->kind == LV_MAP)
	{
		static const gchar *outer[] = { "repeat", "if", NULL };

		if (!check_keys (item, outer, "a repeat node", path, error) ||
		    !check_keys (spec, long_keys, "repeat", path, error))
			return FALSE;
		count_lv = lv_map_get (spec, "count");
	}
	else
	{
		if (!check_keys (item, short_keys, "a repeat node", path, error))
			return FALSE;
		count_lv = spec;
		spec = item;
	}
	if (count_lv == NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, spec, "repeat needs a 'count'");
		return FALSE;
	}
	body = lv_map_get (spec, "body");
	if (body == NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, spec, "repeat needs a 'body'");
		return FALSE;
	}
	var = lv_map_get (spec, "var") != NULL ?
	      resolve_string (lv_map_get (spec, "var"), env->scope, path, "var", error) : g_strdup ("i");
	if (var == NULL)
		return FALSE;

	if (!resolve_ev (count_lv, env->scope, path, &ev, error))
		return FALSE;
	if (!ev_to_number (&ev, &n) || n != floor (n) || n < 0 || n > UI_MAX_REPEAT)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, path, count_lv,
		           "repeat count must be an integer between 0 and %d, got %s",
		           UI_MAX_REPEAT, ev_kind_name (&ev));
		ev_clear (&ev);
		return FALSE;
	}
	ev_clear (&ev);

	for (i = 0; i < (guint)n; i++)
	{
		g_autofree gchar *p = path_join (path, "repeat[%u]", i);
		Scope *s = scope_new (env->scope);
		Env e2 = *env;
		gboolean ok;

		ev_set_num (&ev, (gdouble)i);
		scope_set (s, var, &ev);
		ev_clear (&ev);
		e2.scope = s;
		ok = expand_list (c, body, &e2, p, "body", out, error);
		scope_free (s);
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

/*
 * expand_for_each:
 * `for-each: {items: [...] | ${list} | "a|b|c", var: item, index: i,
 * body: ...}`, or the short form with the items as the value.
 * A mapping iterates its values, with the key as the index.
 */
static gboolean
expand_for_each (Ctx          *c,
                 Lv           *item,
                 Env          *env,
                 const gchar  *path,
                 GPtrArray    *out,
                 GError      **error)
{
	static const gchar *long_keys[] = { "items", "var", "index", "body", NULL };
	static const gchar *short_keys[] = { "for-each", "var", "index", "body", "if", NULL };
	Lv *spec;
	Lv *items_src;
	Lv *body;
	g_autoptr(Lv) items = NULL;
	g_autofree gchar *var = NULL;
	g_autofree gchar *index = NULL;
	guint i;

	spec = lv_map_get (item, "for-each");
	if (spec->kind == LV_MAP)
	{
		static const gchar *outer[] = { "for-each", "if", NULL };

		if (!check_keys (item, outer, "a for-each node", path, error) ||
		    !check_keys (spec, long_keys, "for-each", path, error))
			return FALSE;
		items_src = lv_map_get (spec, "items");
	}
	else
	{
		if (!check_keys (item, short_keys, "a for-each node", path, error))
			return FALSE;
		items_src = spec;
		spec = item;
	}
	body = lv_map_get (spec, "body");
	if (body == NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, spec, "for-each needs a 'body'");
		return FALSE;
	}
	var = lv_map_get (spec, "var") != NULL ?
	      resolve_string (lv_map_get (spec, "var"), env->scope, path, "var", error) : g_strdup ("item");
	if (var == NULL)
		return FALSE;
	index = lv_map_get (spec, "index") != NULL ?
	        resolve_string (lv_map_get (spec, "index"), env->scope, path, "index", error) : g_strdup ("index");
	if (index == NULL)
		return FALSE;

	if (items_src == NULL)
		return TRUE;
	items = lv_resolve (items_src, env->scope, path, error);
	if (items == NULL)
		return FALSE;

	/* "a|b|c" is a list of three texts */
	if (items->kind == LV_SCALAR)
	{
		g_auto(GStrv) words = g_strsplit (items->str, "|", -1);
		Lv *seq = lv_new (LV_SEQ, items);

		for (i = 0; words[i] != NULL; i++)
		{
			if (*words[i] != '\0' || words[1] != NULL)
				g_ptr_array_add (seq->items, lv_new_scalar (words[i], TRUE, items));
		}
		lv_unref (items);
		items = seq;
	}
	if (items->kind == LV_NULL)
		return TRUE;

	for (i = 0; i < items->items->len; i++)
	{
		g_autofree gchar *p = path_join (path, "for-each[%u]", i);
		Scope *s = scope_new (env->scope);
		Env e2 = *env;
		EVal ev = { 0 };
		gboolean ok;

		ev_from_lv (&ev, g_ptr_array_index (items->items, i));
		scope_set (s, var, &ev);
		if (items->kind == LV_MAP)
			ev_take_str (&ev, g_strdup (g_ptr_array_index (items->keys, i)));
		else
			ev_set_num (&ev, (gdouble)i);
		scope_set (s, index, &ev);
		ev_clear (&ev);
		e2.scope = s;
		ok = expand_list (c, body, &e2, p, "body", out, error);
		scope_free (s);
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

/* Reads and parses a file once per expansion, recording it as a dependency */
static Lv *
load_file_cached (Ctx          *c,
                  const gchar  *path,
                  const Lv     *at,
                  const gchar  *node_path,
                  GError      **error)
{
	g_autofree gchar *contents = NULL;
	g_autoptr(GError) local = NULL;
	gsize len;
	Lv *lv;

	lv = g_hash_table_lookup (c->file_cache, path);
	if (lv != NULL)
		return lv;

	if (!g_ptr_array_find_with_equal_func (c->files, path, g_str_equal, NULL))
		g_ptr_array_add (c->files, g_strdup (path));

	if (!g_file_get_contents (path, &contents, &len, &local))
	{
		set_error (error, LRG_UI_LOADER_ERROR_IO, node_path, at, "cannot read '%s': %s", path, local->message);
		return NULL;
	}
	lv = parse_yaml (contents, len, g_intern_string (path), error);
	if (lv == NULL)
		return NULL;
	g_hash_table_insert (c->file_cache, g_strdup (path), lv);
	return lv;
}

/*
 * expand_include:
 * `include: file.yaml` (relative to the including file, or to the
 * current directory for documents given as text), optionally with
 * `with: {name: value}` variables. The file's components are registered
 * and its root, if it has one, is inserted here.
 */
static gboolean
expand_include (Ctx          *c,
                Lv           *item,
                Env          *env,
                const gchar  *path,
                GPtrArray    *out,
                GError      **error)
{
	static const gchar *keys[] = { "include", "with", "if", NULL };
	g_autofree gchar *rel = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *full = NULL;
	g_autofree gchar *ipath = NULL;
	Lv *doc;
	Lv *with;
	Scope *s = NULL;
	Env e2;
	guint i;
	gboolean ok;

	if (!check_keys (item, keys, "an include node", path, error))
		return FALSE;
	rel = resolve_string (lv_map_get (item, "include"), env->scope, path, "include", error);
	if (rel == NULL)
		return FALSE;
	if (*rel == '\0')
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, item, "'include' needs a file name");
		return FALSE;
	}

	base = env->file != NULL ? g_path_get_dirname (env->file) : g_get_current_dir ();
	full = g_canonicalize_filename (rel, base);

	/* Cycle detection over the chain of files being expanded */
	for (i = 0; i < c->include_stack->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (c->include_stack, i), full) == 0)
		{
			GString *chain = g_string_new (NULL);
			guint j;

			for (j = 0; j < c->include_stack->len; j++)
			{
				g_autofree gchar *bn = g_path_get_basename (g_ptr_array_index (c->include_stack, j));

				g_string_append_printf (chain, "%s -> ", bn);
			}
			{
				g_autofree gchar *bn = g_path_get_basename (full);

				g_string_append (chain, bn);
			}
			set_error (error, LRG_UI_LOADER_ERROR_INCLUDE_CYCLE, path, lv_map_get (item, "include"),
			           "include cycle: %s", chain->str);
			g_string_free (chain, TRUE);
			return FALSE;
		}
	}
	if (c->include_stack->len > 64)
	{
		set_error (error, LRG_UI_LOADER_ERROR_INCLUDE_CYCLE, path, item, "includes nest too deeply");
		return FALSE;
	}

	doc = load_file_cached (c, full, lv_map_get (item, "include"), path, error);
	if (doc == NULL)
		return FALSE;

	/* Optional variables for the included file */
	e2 = *env;
	e2.file = g_intern_string (full);
	with = lv_map_get (item, "with");
	if (with != NULL)
	{
		if (with->kind != LV_MAP)
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, with,
			           "'with' must map names to values, got %s", lv_kind_name (with));
			return FALSE;
		}
		s = scope_new (env->scope);
		for (i = 0; i < with->keys->len; i++)
		{
			EVal ev = { 0 };

			if (!resolve_ev (g_ptr_array_index (with->items, i), env->scope, path, &ev, error))
			{
				scope_free (s);
				return FALSE;
			}
			scope_set (s, g_ptr_array_index (with->keys, i), &ev);
			ev_clear (&ev);
		}
		e2.scope = s;
	}

	{
		g_autofree gchar *bn = g_path_get_basename (full);

		ipath = path_join (path, "{%s}", bn);
	}
	g_ptr_array_add (c->include_stack, g_strdup (full));
	ok = expand_document (c, doc, &e2, ipath, FALSE, out, error);
	g_ptr_array_remove_index (c->include_stack, c->include_stack->len - 1);
	scope_free (s);
	return ok;
}

/*
 * expand_slot:
 * `slot: true` (the default slot) or `slot: name`. Inside a component
 * body it is replaced by what the use site gave; when nothing was given
 * the slot's own `children:` are the fallback.
 */
static gboolean
expand_slot (Ctx          *c,
             Lv           *item,
             Env          *env,
             const gchar  *path,
             GPtrArray    *out,
             GError      **error)
{
	static const gchar *keys[] = { "slot", "children", "if", NULL };
	g_autoptr(Lv) r = NULL;
	GPtrArray *fill = NULL;
	gboolean is_default;
	guint i;

	if (!check_keys (item, keys, "a slot node", path, error))
		return FALSE;
	if (env->slots == NULL)
	{
		set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, item,
		           "'slot' is only valid inside a component body");
		return FALSE;
	}
	r = lv_resolve (lv_map_get (item, "slot"), env->scope, path, error);
	if (r == NULL)
		return FALSE;
	if (r->kind != LV_SCALAR)
	{
		set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, path, item,
		           "'slot' must be true or a slot name, got %s", lv_kind_name (r));
		return FALSE;
	}
	is_default = g_strcmp0 (r->str, "true") == 0 || g_strcmp0 (r->str, "default") == 0;

	if (is_default)
	{
		fill = env->slots->default_nodes;
		env->slots->default_used = TRUE;
	}
	else
	{
		if (env->slots->named != NULL)
			fill = g_hash_table_lookup (env->slots->named, r->str);
		g_hash_table_add (env->slots->named_used, g_strdup (r->str));
	}

	if (fill != NULL)
	{
		for (i = 0; i < fill->len; i++)
			g_ptr_array_add (out, ui_node_ref (g_ptr_array_index (fill, i)));
		return TRUE;
	}

	/* Fallback content */
	return expand_list (c, lv_map_get (item, "children"), env, path, "children", out, error);
}

/*
 * expand_item:
 * One entry of a node list: a widget, a component use, or a structural
 * node. `if:` on any of them skips it when false.
 */
static gboolean
expand_item (Ctx          *c,
             Lv           *item,
             Env          *env,
             const gchar  *path,
             GPtrArray    *out,
             GError      **error)
{
	Lv *cond;
	gboolean ok;

	if (item->kind != LV_MAP)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, item,
		           "expected a widget node (a mapping with 'type'), got %s", lv_kind_name (item));
		return FALSE;
	}
	if (c->depth > UI_MAX_DEPTH)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, item, "the document nests too deeply");
		return FALSE;
	}

	cond = lv_map_get (item, "if");
	if (cond != NULL)
	{
		EVal ev = { 0 };
		gboolean keep;

		if (!resolve_ev (cond, env->scope, path, &ev, error))
			return FALSE;
		keep = ev_truthy (&ev);
		ev_clear (&ev);
		if (!keep)
			return TRUE;
	}

	c->depth++;
	if (lv_map_get (item, "repeat") != NULL)
		ok = expand_repeat (c, item, env, path, out, error);
	else if (lv_map_get (item, "for-each") != NULL)
		ok = expand_for_each (c, item, env, path, out, error);
	else if (lv_map_get (item, "include") != NULL)
		ok = expand_include (c, item, env, path, out, error);
	else if (lv_map_get (item, "slot") != NULL)
		ok = expand_slot (c, item, env, path, out, error);
	else if (lv_map_get (item, "type") != NULL)
	{
		UiNode *n = expand_widget (c, item, env, path, error);

		ok = n != NULL;
		if (ok)
			g_ptr_array_add (out, n);
	}
	else
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, item,
		           "a node needs 'type' (or one of repeat, for-each, include, slot)");
		ok = FALSE;
	}
	c->depth--;
	return ok;
}

/* Registers the components of a `components:` mapping */
static gboolean
register_components (Ctx          *c,
                     Lv           *comps,
                     const gchar  *file,
                     GError      **error)
{
	static const gchar *keys[] = { "params", "body", NULL };
	guint i;

	if (comps->kind != LV_MAP)
	{
		set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, "components", comps,
		           "'components' must map names to {params:, body:}, got %s", lv_kind_name (comps));
		return FALSE;
	}
	for (i = 0; i < comps->keys->len; i++)
	{
		const gchar *name = g_ptr_array_index (comps->keys, i);
		Lv *def = g_ptr_array_index (comps->items, i);
		g_autofree gchar *p = g_strdup_printf ("components.%s", name);
		Component *comp;
		Lv *params;

		if (def->kind != LV_MAP)
		{
			set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, p, def,
			           "a component must be a mapping {params:, body:}, got %s", lv_kind_name (def));
			return FALSE;
		}
		if (!check_keys (def, keys, "a component", p, error))
			return FALSE;
		if (lv_map_get (def, "body") == NULL || lv_map_get (def, "body")->kind != LV_MAP)
		{
			set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, p, def, "a component needs a 'body' node");
			return FALSE;
		}
		params = lv_map_get (def, "params");
		if (params != NULL && params->kind != LV_MAP && params->kind != LV_NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_COMPONENT, p, params,
			           "'params' must map names to default values, got %s", lv_kind_name (params));
			return FALSE;
		}
		comp = g_new0 (Component, 1);
		comp->name = g_strdup (name);
		comp->params = (params != NULL && params->kind == LV_MAP) ? lv_ref (params) : NULL;
		comp->body = lv_ref (lv_map_get (def, "body"));
		comp->file = file;
		g_hash_table_insert (c->components, g_strdup (name), comp);
	}
	return TRUE;
}

/*
 * expand_document:
 * A document is either a node, a list of nodes, or a mapping with
 * `components:`, `variables:` and `root:`. A mapping with `type:` may
 * also carry `components:` and `variables:` beside its widget keys.
 */
static gboolean
expand_document (Ctx          *c,
                 Lv           *doc,
                 Env          *env,
                 const gchar  *path,
                 gboolean      is_main,
                 GPtrArray    *out,
                 GError      **error)
{
	Lv *comps;
	Lv *vars;
	Lv *root;
	Scope *vscope = NULL;
	Scope *saved_globals;
	Env e2;
	gboolean ok = TRUE;
	guint i;

	if (doc->kind == LV_SEQ)
		return expand_list (c, doc, env, path, "", out, error);
	if (doc->kind != LV_MAP)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, doc,
		           "a document must be a mapping or a list, got %s", lv_kind_name (doc));
		return FALSE;
	}

	comps = lv_map_get (doc, "components");
	vars = lv_map_get (doc, "variables");
	root = lv_map_get (doc, "root");

	if (comps != NULL && !register_components (c, comps, env->file, error))
		return FALSE;
	if (lv_map_get (doc, "menus") != NULL && !register_menus (c, lv_map_get (doc, "menus"), error))
		return FALSE;

	/* Document variables, in order (each may use the previous ones) */
	e2 = *env;
	saved_globals = c->globals;
	if (vars != NULL)
	{
		if (vars->kind != LV_MAP)
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, "variables", vars,
			           "'variables' must map names to values, got %s", lv_kind_name (vars));
			return FALSE;
		}
		vscope = scope_new (env->scope);
		for (i = 0; i < vars->keys->len; i++)
		{
			EVal ev = { 0 };
			g_autofree gchar *vp = g_strdup_printf ("variables.%s", (const gchar *)g_ptr_array_index (vars->keys, i));

			if (!resolve_ev (g_ptr_array_index (vars->items, i), vscope, vp, &ev, error))
			{
				scope_free (vscope);
				return FALSE;
			}
			scope_set (vscope, g_ptr_array_index (vars->keys, i), &ev);
			ev_clear (&ev);
		}
		e2.scope = vscope;
		/* Components of the main document see its variables */
		if (is_main)
			c->globals = vscope;
	}

	/* Languages see the document's variables */
	if (lv_map_get (doc, "languages") != NULL && !register_languages (c, lv_map_get (doc, "languages"), &e2, error))
	{
		c->globals = saved_globals;
		scope_free (vscope);
		return FALSE;
	}

	if (lv_map_get (doc, "type") != NULL || lv_map_get (doc, "repeat") != NULL ||
	    lv_map_get (doc, "for-each") != NULL || lv_map_get (doc, "include") != NULL)
	{
		/* The mapping itself is the root node, minus the document keys */
		g_autoptr(Lv) node = lv_new (LV_MAP, doc);

		if (root != NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, root,
			           "'root' cannot be combined with 'type' at the top of a document");
			ok = FALSE;
		}
		for (i = 0; ok && i < doc->keys->len; i++)
		{
			const gchar *k = g_ptr_array_index (doc->keys, i);

			if (key_equal (k, "components") || key_equal (k, "variables") || key_equal (k, "shortcuts") ||
			    key_equal (k, "menus") || key_equal (k, "languages"))
				continue;
			lv_map_add (node, k, lv_ref (g_ptr_array_index (doc->items, i)));
		}
		if (ok)
			ok = expand_item (c, node, &e2, path, out, error);
	}
	else
	{
		for (i = 0; i < doc->keys->len; i++)
		{
			const gchar *k = g_ptr_array_index (doc->keys, i);

			if (!key_equal (k, "components") && !key_equal (k, "variables") && !key_equal (k, "root") &&
			    !key_equal (k, "shortcuts") && !key_equal (k, "menus") && !key_equal (k, "languages"))
			{
				set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, path, g_ptr_array_index (doc->items, i),
				           "unknown top-level key '%s' (expected type, components, variables, shortcuts, "
				           "menus, languages or root)", k);
				ok = FALSE;
				break;
			}
		}
		/* `root:` is the root node itself (or a list that must give one) */
		if (ok && root != NULL && root->kind == LV_MAP)
			ok = expand_item (c, root, &e2, path, out, error);
		else if (ok && root != NULL)
			ok = expand_list (c, root, &e2, path, "root", out, error);
	}

	c->globals = saved_globals;
	scope_free (vscope);
	return ok;
}

/*
 * expand_text:
 * Runs the whole EXPAND stage on a document. @file (canonical, nullable)
 * is the document's own path; every file read is appended to @files.
 */
/*
 * parse_shortcuts:
 * The top-level `shortcuts:` list:
 * `- {accel: "<Ctrl>q", action: app.quit, target: ..., target-type: ...}`.
 * Accelerators and action names are validated here.
 *
 * Returns: (transfer full): ShortcutSpec array, or NULL with @error set
 */
static GPtrArray *
parse_shortcuts (const Lv  *list,
                 GError   **error)
{
	g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func (shortcut_spec_free);
	guint i;

	if (list->kind != LV_SEQ)
	{
		set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, "shortcuts", list,
		           "'shortcuts' must be a list of {accel:, action:}, got %s", lv_kind_name (list));
		return NULL;
	}
	for (i = 0; i < list->items->len; i++)
	{
		const Lv *it = g_ptr_array_index (list->items, i);
		const Lv *accel = lv_map_get (it, "accel");
		const Lv *action = lv_map_get (it, "action");
		g_autofree gchar *msg = NULL;
		GVariant *target = NULL;
		ShortcutSpec *sc;

		if (it->kind != LV_MAP || accel == NULL || action == NULL)
		{
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, "shortcuts", it,
			           "a shortcut needs 'accel' and 'action'");
			return NULL;
		}
		if (accel->kind != LV_SCALAR || !lrg_accel_parse (accel->str, NULL, NULL))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, "shortcuts", accel,
			           "invalid accelerator '%s'", accel->kind == LV_SCALAR ? accel->str : "?");
			return NULL;
		}
		if (action->kind != LV_SCALAR || !is_action_name (action->str))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, "shortcuts", action,
			           "'action' must be \"prefix.action\"");
			return NULL;
		}
		if (!parse_action_target (lv_map_get (it, "target"), lv_map_get (it, "target-type"), &target, &msg))
		{
			set_error (error, LRG_UI_LOADER_ERROR_INVALID_VALUE, "shortcuts", it, "%s", msg);
			return NULL;
		}
		sc = g_new0 (ShortcutSpec, 1);
		sc->accel = g_strdup (accel->str);
		sc->action = g_strdup (action->str);
		sc->target = target;
		g_ptr_array_add (out, sc);
	}
	return g_steal_pointer (&out);
}

static UiNode *
expand_text (LrgUILoader  *self,
             const gchar  *data,
             gsize         length,
             const gchar  *file,
             GPtrArray    *files,
             GError      **error)
{
	g_autoptr(Lv) doc = NULL;
	g_autoptr(GPtrArray) out = NULL;
	GHashTableIter it;
	gpointer k;
	gpointer v;
	Scope *globals;
	Ctx c;
	Env env;
	UiNode *root = NULL;
	gboolean ok;

	doc = parse_yaml (data, length, file != NULL ? g_intern_string (file) : NULL, error);
	if (doc == NULL)
		return NULL;

	/* Global scope: the loader's variables */
	globals = scope_new (NULL);
	g_hash_table_iter_init (&it, self->variables);
	while (g_hash_table_iter_next (&it, &k, &v))
		scope_set (globals, k, v);

	memset (&c, 0, sizeof (c));
	c.self = self;
	c.components = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, component_free);
	c.include_stack = g_ptr_array_new_with_free_func (g_free);
	c.comp_stack = g_ptr_array_new ();
	c.files = files;
	c.file_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)lv_unref);
	c.globals = globals;
	c.menus = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)lv_unref);
	c.syntaxes = g_ptr_array_new_with_free_func (g_object_unref);
	if (file != NULL)
		g_ptr_array_add (c.include_stack, g_strdup (file));

	env.scope = globals;
	env.slots = NULL;
	env.file = file != NULL ? g_intern_string (file) : NULL;

	out = g_ptr_array_new_with_free_func ((GDestroyNotify)ui_node_unref);
	ok = expand_document (&c, doc, &env, "", TRUE, out, error);
	if (ok && out->len != 1)
	{
		if (out->len == 0)
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, "(document)", doc, "the document has no root widget");
		else
			set_error (error, LRG_UI_LOADER_ERROR_STRUCTURE, "(document)", doc,
			           "the document gives %u root widgets; it must give exactly one", out->len);
		ok = FALSE;
	}
	if (ok)
		root = ui_node_ref (g_ptr_array_index (out, 0));

	/* Top-level `shortcuts:` belong to the document's root */
	if (root != NULL && doc->kind == LV_MAP && lv_map_get (doc, "shortcuts") != NULL)
	{
		root->shortcuts = parse_shortcuts (lv_map_get (doc, "shortcuts"), error);
		if (root->shortcuts == NULL)
			g_clear_pointer (&root, ui_node_unref);
	}

	/* Everything checked: make the document's languages findable */
	if (root != NULL)
	{
		guint i;

		for (i = 0; i < c.syntaxes->len; i++)
			lrg_syntax_register (g_ptr_array_index (c.syntaxes, i));
	}

	g_hash_table_unref (c.menus);
	g_ptr_array_unref (c.syntaxes);
	g_hash_table_unref (c.components);
	g_ptr_array_unref (c.include_stack);
	g_ptr_array_unref (c.comp_stack);
	g_hash_table_unref (c.file_cache);
	scope_free (globals);
	return root;
}

static UiNode *
expand_file (LrgUILoader  *self,
             const gchar  *path,
             GPtrArray    *files,
             GError      **error)
{
	g_autofree gchar *full = NULL;
	g_autofree gchar *contents = NULL;
	g_autoptr(GError) local = NULL;
	gsize len;

	full = g_canonicalize_filename (path, NULL);
	g_ptr_array_add (files, g_strdup (full));
	if (!g_file_get_contents (full, &contents, &len, &local))
	{
		g_set_error (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_IO,
		             "(document): cannot read '%s': %s", full, local->message);
		return NULL;
	}
	return expand_text (self, contents, len, full, files, error);
}

/* ==========================================================================
 * BUILD: UiNode -> widgets
 * ========================================================================== */

/*
 * ActionClosure:
 * A signal handler that activates an action on the emitting widget,
 * whatever the signal's signature (the marshaller ignores the
 * arguments). A signal with a boolean return value gets TRUE when the
 * action ran.
 */
typedef struct
{
	GClosure  closure;
	gchar    *action;
	GVariant *target;
} ActionClosure;

static void
action_closure_marshal (GClosure     *closure,
                        GValue       *return_value,
                        guint         n_param_values,
                        const GValue *param_values,
                        gpointer      invocation_hint,
                        gpointer      marshal_data)
{
	ActionClosure *ac = (ActionClosure *)closure;
	GObject *instance;
	gboolean ran = FALSE;

	if (n_param_values < 1 || !G_VALUE_HOLDS_OBJECT (&param_values[0]))
		return;
	instance = g_value_get_object (&param_values[0]);
	if (LRG_IS_WIDGET (instance))
		ran = lrg_widget_activate_action (LRG_WIDGET (instance), ac->action, ac->target);
	if (!ran)
		lrg_debug (LRG_LOG_DOMAIN, "UI loader: action '%s' did not run (not found or disabled)", ac->action);
	if (return_value != NULL && G_VALUE_HOLDS_BOOLEAN (return_value))
		g_value_set_boolean (return_value, ran);
}

static void
action_closure_finalize (gpointer  data,
                         GClosure *closure)
{
	ActionClosure *ac = (ActionClosure *)closure;

	g_free (ac->action);
	g_clear_pointer (&ac->target, g_variant_unref);
}

static gulong
connect_action (LrgWidget         *w,
                const HandlerSpec *h)
{
	GClosure *closure = g_closure_new_simple (sizeof (ActionClosure), NULL);
	ActionClosure *ac = (ActionClosure *)closure;

	ac->action = g_strdup (h->action);
	ac->target = h->target != NULL ? g_variant_ref (h->target) : NULL;
	g_closure_set_marshal (closure, action_closure_marshal);
	g_closure_add_finalize_notifier (closure, NULL, action_closure_finalize);
	return g_signal_connect_closure (w, h->signal, closure, h->after);
}

static void
connect_handlers (LrgWidget    *w,
                  WidgetRecord *rec,
                  UiAttr       *a)
{
	guint i;

	for (i = 0; i < a->handlers->len; i++)
	{
		HandlerSpec *h = &g_array_index (a->handlers, HandlerSpec, i);
		GConnectFlags flags = 0;
		gulong id;

		if (h->action != NULL)
		{
			id = connect_action (w, h);
			if (id != 0)
				g_array_append_val (rec->handler_ids, id);
			continue;
		}
		if (h->after)
			flags |= G_CONNECT_AFTER;
		if (h->swapped)
			flags |= G_CONNECT_SWAPPED;
		id = g_signal_connect_data (w, h->signal, h->callback, h->user_data, NULL, flags);
		if (id != 0)
			g_array_append_val (rec->handler_ids, id);
	}
}

static void
disconnect_handlers (LrgWidget    *w,
                     WidgetRecord *rec)
{
	guint i;

	for (i = 0; i < rec->handler_ids->len; i++)
	{
		gulong id = g_array_index (rec->handler_ids, gulong, i);

		if (g_signal_handler_is_connected (w, id))
			g_signal_handler_disconnect (w, id);
	}
	g_array_set_size (rec->handler_ids, 0);
}

/* Applies one attribute (handlers are connected separately, last) */
static void
apply_attr (LrgWidget *w,
            UiAttr    *a)
{
	guint i;

	switch (a->kind)
	{
	case ATTR_PROP:
		if (!a->construct)
			g_object_set_property (G_OBJECT (w), a->pspec->name, &a->gvalue);
		break;
	case ATTR_CLASS:
		for (i = 0; a->strv[i] != NULL; i++)
			lrg_widget_add_class (w, a->strv[i]);
		break;
	case ATTR_LOOK:
		for (i = 0; i < a->looks->len; i++)
		{
			LookEntry *e = &g_array_index (a->looks, LookEntry, i);

			lrg_widget_set_part_look (w, e->part, e->state, &e->look);
		}
		break;
	case ATTR_APPEAR:
		lrg_widget_set_appear (w, (LrgAppear)a->appear, a->f[0]);
		break;
	case ATTR_MIN_SIZE:
		lrg_widget_set_min_size (w, a->f[0], a->f[1]);
		break;
	case ATTR_MAX_SIZE:
		lrg_widget_set_max_size (w, a->f[0], a->f[1]);
		break;
	case ATTR_ITEMS:
		lrg_combo_box_remove_all (LRG_COMBO_BOX (w));
		for (i = 0; a->strv[i] != NULL; i++)
			lrg_combo_box_append (LRG_COMBO_BOX (w), a->strv[i]);
		break;
	case ATTR_ANCHOR:
	case ATTR_PAGE_NAME:
	case ATTR_PACK:
	case ATTR_DOCK_PLACE:
		/* Through the parent; a widget being built has none yet and
		 * gets them from apply_child_attrs() once it is added */
		child_attr_apply (lrg_widget_get_parent (w), w, a);
		break;
	case ATTR_DOCK_LAYOUT:
		{
			g_autoptr(GError) local = NULL;

			if (!lrg_dock_area_load_layout (LRG_DOCK_AREA (w), a->text, &local))
				lrg_message (LRG_LOG_DOMAIN, "UI loader: dock layout not applied: %s", local->message);
			break;
		}
	case ATTR_GRAPH:
		{
			g_autoptr(GError) local = NULL;

			/* Checked during expansion; only the application's
			 * ::can-connect can still refuse a link here */
			if (!lrg_node_graph_load_yaml (LRG_NODE_GRAPH (w), a->text, -1, &local))
				lrg_message (LRG_LOG_DOMAIN, "UI loader: graph partly loaded: %s", local->message);
			break;
		}
	case ATTR_TRACKS:
	case ATTR_MARKERS:
		timeline_apply (LRG_TIMELINE_VIEW (w), a);
		break;
	case ATTR_BLOCKS:
		{
			g_autoptr(GError) local = NULL;

			/* Checked during expansion */
			if (!lrg_block_editor_load_yaml (LRG_BLOCK_EDITOR (w), a->text, -1, &local))
				lrg_message (LRG_LOG_DOMAIN, "UI loader: blocks not loaded: %s", local->message);
			break;
		}
	case ATTR_FB_LOCATION:
		lrg_file_browser_set_location_path (LRG_FILE_BROWSER (w), a->text);
		break;
	case ATTR_FB_FILTERS:
		fb_apply_filters (LRG_FILE_BROWSER (w), a->specs);
		break;
	case ATTR_FB_PLACES:
		fb_apply_places (LRG_FILE_BROWSER (w), a->specs);
		break;
	case ATTR_ON:
	default:
		break;
	}
}

/* Applies the per-child settings of @w's node through its parent */
static void
apply_child_attrs (LrgWidget *w)
{
	WidgetRecord *rec = widget_get_record (w);
	LrgContainer *parent = lrg_widget_get_parent (w);
	guint i;

	if (rec == NULL || parent == NULL)
		return;
	for (i = 0; i < rec->node->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (rec->node->attrs, i);

		if (a->child)
			child_attr_apply (parent, w, a);
	}
}

/*
 * prop_resettable:
 * Whether a property that disappears from the YAML can be put back to its
 * default in place. Boxed/object properties whose default is NULL cannot
 * (widgets ignore NULL colours), so the widget is rebuilt instead.
 */
static gboolean
prop_resettable (UiAttr *a)
{
	const GValue *def;

	if (a->construct)
		return FALSE;
	def = g_param_spec_get_default_value (a->pspec);
	if (G_VALUE_HOLDS_BOXED (def) && g_value_get_boxed (def) == NULL)
		return G_VALUE_TYPE (def) == LRG_TYPE_INSETS;
	/* Menus and highlighters accept NULL (no menu, plain text) */
	if (g_type_is_a (G_VALUE_TYPE (def), G_TYPE_MENU_MODEL) || g_type_is_a (G_VALUE_TYPE (def), LRG_TYPE_SYNTAX))
		return TRUE;
	if (G_VALUE_HOLDS_OBJECT (def) && g_value_get_object (def) == NULL)
		return FALSE;
	return TRUE;
}

/* Undoes an attribute that is gone from the new document */
static void
reset_attr (LrgWidget    *w,
            WidgetRecord *rec,
            UiAttr       *a)
{
	guint i;

	switch (a->kind)
	{
	case ATTR_PROP:
		{
			const GValue *def = g_param_spec_get_default_value (a->pspec);

			if (G_VALUE_TYPE (def) == LRG_TYPE_INSETS && g_value_get_boxed (def) == NULL)
			{
				LrgInsets zero = { 0.0f, 0.0f, 0.0f, 0.0f };

				lrg_widget_set_margin (w, &zero);
			}
			else if (G_VALUE_HOLDS_STRING (def) && g_value_get_string (def) == NULL)
			{
				g_object_set (w, a->pspec->name, "", NULL);
			}
			else
			{
				g_object_set_property (G_OBJECT (w), a->pspec->name, def);
			}
			break;
		}
	case ATTR_CLASS:
		for (i = 0; a->strv[i] != NULL; i++)
			lrg_widget_remove_class (w, a->strv[i]);
		break;
	case ATTR_ON:
		disconnect_handlers (w, rec);
		break;
	case ATTR_LOOK:
		lrg_widget_clear_part_looks (w);
		break;
	case ATTR_APPEAR:
		lrg_widget_set_appear (w, LRG_APPEAR_NONE, 0.0f);
		break;
	case ATTR_MIN_SIZE:
		lrg_widget_set_min_size (w, 0.0f, 0.0f);
		break;
	case ATTR_MAX_SIZE:
		lrg_widget_set_max_size (w, 0.0f, 0.0f);
		break;
	case ATTR_ITEMS:
		lrg_combo_box_remove_all (LRG_COMBO_BOX (w));
		break;
	case ATTR_ANCHOR:
	case ATTR_PAGE_NAME:
	case ATTR_PACK:
		child_attr_reset (lrg_widget_get_parent (w), w, a);
		break;
	case ATTR_GRAPH:
		lrg_node_graph_clear (LRG_NODE_GRAPH (w));
		break;
	case ATTR_TRACKS:
		timeline_clear_tracks (LRG_TIMELINE_VIEW (w));
		break;
	case ATTR_MARKERS:
		timeline_clear_markers (LRG_TIMELINE_VIEW (w));
		break;
	case ATTR_BLOCKS:
		/* The program goes; kinds stay, like a palette loaded by code */
		lrg_block_editor_clear_program (LRG_BLOCK_EDITOR (w));
		break;
	case ATTR_FB_FILTERS:
		fb_clear_loader_filters (LRG_FILE_BROWSER (w));
		break;
	case ATTR_FB_PLACES:
		fb_clear_loader_places (LRG_FILE_BROWSER (w));
		break;
	case ATTR_FB_LOCATION:
		/* Where the user browsed to stays */
	case ATTR_DOCK_PLACE:
	case ATTR_DOCK_LAYOUT:
		/* The user's arrangement stays */
	default:
		break;
	}
}

static LrgWidget *build_widget (UiNode *n);

/*
 * dock_add_panel:
 * Docks a panel the loader built at its `position:` relative to the
 * panel named by `relative-to:` (the whole area when there is none, or
 * when that panel is gone). The default is a tab in the first stack.
 */
static void
dock_add_panel (LrgDockArea  *area,
                LrgDockPanel *panel)
{
	WidgetRecord *rec = widget_get_record (LRG_WIDGET (panel));
	UiAttr *place = rec != NULL ? ui_node_find_attr (rec->node, "dock-place") : NULL;
	LrgDockPanel *rel = NULL;

	if (place != NULL && place->text != NULL)
		rel = lrg_dock_area_get_panel (area, place->text);
	lrg_dock_area_add_panel (area, panel, rel,
	                         place != NULL ? (LrgDockPosition)place->ival : LRG_DOCK_POSITION_CENTER);
}

/* Adds already-built children / tabs to @w following @n's layout */
static void
attach_child (LrgWidget *w,
              UiNode    *n,
              LrgWidget *child)
{
	if (n->child_prop != NULL)
		g_object_set (w, n->child_prop, child, NULL);
	else
		lrg_container_add_child (LRG_CONTAINER (w), child);
}

/*
 * build_widget:
 * Instantiates a node. Order: construct-only properties, id, normal
 * properties and special keys in document order, combo items, children
 * or tabs, late properties (value, selected, ...), then handlers, so
 * setting up the widget never fires the document's own handlers.
 */
static LrgWidget *
build_widget (UiNode *n)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new ();
	g_autoptr(GArray) values = g_array_new (FALSE, TRUE, sizeof (GValue));
	WidgetRecord *rec;
	LrgWidget *w;
	GObject *obj;
	UiAttr *on = NULL;
	guint i;

	/* Construct-only properties go to g_object_new() */
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);

		if (a->kind == ATTR_PROP && a->construct)
		{
			GValue copy = G_VALUE_INIT;

			g_value_init (&copy, G_VALUE_TYPE (&a->gvalue));
			g_value_copy (&a->gvalue, &copy);
			g_ptr_array_add (names, (gpointer)a->pspec->name);
			g_array_append_val (values, copy);
		}
	}
	obj = g_object_new_with_properties (n->type, names->len, (const gchar **)names->pdata,
	                                    (const GValue *)(gpointer)values->data);
	for (i = 0; i < values->len; i++)
		g_value_unset (&g_array_index (values, GValue, i));
	if (g_object_is_floating (obj))
		g_object_ref_sink (obj);
	w = LRG_WIDGET (obj);

	rec = g_new0 (WidgetRecord, 1);
	rec->node = ui_node_ref (n);
	rec->handler_ids = g_array_new (FALSE, FALSE, sizeof (gulong));
	g_object_set_qdata_full (obj, q_record, rec, widget_record_free);

	if (n->id != NULL)
		lrg_widget_set_name (w, n->id);
	/* A dock panel's id (layout persistence, relative-to) is its YAML id */
	if (n->id != NULL && LRG_IS_DOCK_PANEL (w))
		lrg_dock_panel_set_id (LRG_DOCK_PANEL (w), n->id);

	/* Early values */
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);

		if (a->kind == ATTR_ON)
			on = a;
		else if (a->kind != ATTR_ITEMS && !a->late)
			apply_attr (w, a);
	}
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);

		if (a->kind == ATTR_ITEMS)
			apply_attr (w, a);
	}

	/* Content */
	if (n->has_tabs)
	{
		for (i = 0; i < n->tabs->len; i++)
		{
			UiTab *t = g_ptr_array_index (n->tabs, i);
			g_autoptr(LrgWidget) content = build_widget (t->content);

			lrg_tab_view_add_tab (LRG_TAB_VIEW (w), t->label, content);
		}
	}
	for (i = 0; i < n->children->len; i++)
	{
		g_autoptr(LrgWidget) child = build_widget (g_ptr_array_index (n->children, i));

		if (LRG_IS_DOCK_AREA (w))
			dock_add_panel (LRG_DOCK_AREA (w), LRG_DOCK_PANEL (child));
		else
		{
			attach_child (w, n, child);
			apply_child_attrs (child);
		}
	}

	/* Late values, then handlers */
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);

		if (a->late)
			apply_attr (w, a);
	}
	if (on != NULL)
		connect_handlers (w, rec, on);

	return w;
}

/* ==========================================================================
 * SYNC: diff a new UiNode tree against live widgets
 * ========================================================================== */

static LrgWidget *sync_widget (LrgWidget *w, UiNode *n);

/* TRUE when the node at @n can be applied to @w without rebuilding it */
static gboolean
can_reuse (LrgWidget *w,
           UiNode    *n)
{
	WidgetRecord *rec;
	UiNode *old;
	guint i;

	rec = widget_get_record (w);
	if (rec == NULL)
		return FALSE;
	old = rec->node;
	if (old->type != n->type || G_OBJECT_TYPE (w) != n->type)
		return FALSE;
	if (old->has_tabs != n->has_tabs || g_strcmp0 (old->child_prop, n->child_prop) != 0)
		return FALSE;

	/* Construct-only values must not change; removed values must reset */
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);
		UiAttr *b = ui_node_find_attr (old, a->key);

		if (a->kind == ATTR_PROP && a->construct && (b == NULL || !lv_equal (a->value, b->value)))
			return FALSE;
	}
	for (i = 0; i < old->attrs->len; i++)
	{
		UiAttr *b = g_ptr_array_index (old->attrs, i);

		if (ui_node_find_attr (n, b->key) == NULL && b->kind == ATTR_PROP && !prop_resettable (b))
			return FALSE;
	}
	return TRUE;
}

static const gchar *
widget_id (LrgWidget *w)
{
	WidgetRecord *rec = widget_get_record (w);

	return rec != NULL ? rec->node->id : NULL;
}

/*
 * match_widgets:
 * Pairs each new node with a live widget: first by id (and type), then
 * the k-th id-less node of a type with the k-th id-less live widget of
 * that type. @matched receives the live widget (or NULL) per node.
 */
static void
match_widgets (GPtrArray *nodes,
               GPtrArray *live,
               GPtrArray *matched)
{
	g_autoptr(GHashTable) used = g_hash_table_new (NULL, NULL);
	guint i;
	guint j;

	g_ptr_array_set_size (matched, (gint)nodes->len);
	for (i = 0; i < nodes->len; i++)
	{
		UiNode *n = g_ptr_array_index (nodes, i);

		g_ptr_array_index (matched, i) = NULL;
		if (n->id == NULL)
			continue;
		for (j = 0; j < live->len; j++)
		{
			LrgWidget *lw = g_ptr_array_index (live, j);

			if (!g_hash_table_contains (used, lw) && g_strcmp0 (widget_id (lw), n->id) == 0 &&
			    G_OBJECT_TYPE (lw) == n->type)
			{
				g_ptr_array_index (matched, i) = lw;
				g_hash_table_add (used, lw);
				break;
			}
		}
	}
	for (i = 0; i < nodes->len; i++)
	{
		UiNode *n = g_ptr_array_index (nodes, i);
		guint k = 0;

		if (n->id != NULL)
			continue;
		/* rank of this node among the id-less nodes of its type */
		for (j = 0; j < i; j++)
		{
			UiNode *m = g_ptr_array_index (nodes, j);

			if (m->id == NULL && m->type == n->type)
				k++;
		}
		for (j = 0; j < live->len; j++)
		{
			LrgWidget *lw = g_ptr_array_index (live, j);

			if (widget_id (lw) != NULL || G_OBJECT_TYPE (lw) != n->type)
				continue;
			if (k == 0)
			{
				if (!g_hash_table_contains (used, lw))
				{
					g_ptr_array_index (matched, i) = lw;
					g_hash_table_add (used, lw);
				}
				break;
			}
			k--;
		}
	}
}

/* The children this loader built (other children are left alone) */
static GPtrArray *
owned_children (LrgContainer *c)
{
	GPtrArray *out = g_ptr_array_new ();
	GList *l;

	for (l = lrg_container_get_children (c); l != NULL; l = l->next)
	{
		if (widget_get_record (LRG_WIDGET (l->data)) != NULL)
			g_ptr_array_add (out, l->data);
	}
	return out;
}

/* Diffs a container's `children:` */
static void
sync_children (LrgWidget *w,
               UiNode    *n)
{
	LrgContainer *c = LRG_CONTAINER (w);
	g_autoptr(GPtrArray) live = owned_children (c);
	g_autoptr(GPtrArray) matched = g_ptr_array_new ();
	g_autoptr(GPtrArray) desired = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GPtrArray) now = NULL;
	gboolean in_order;
	guint i;

	match_widgets (n->children, live, matched);

	/* Reuse or build each child */
	for (i = 0; i < n->children->len; i++)
	{
		UiNode *cn = g_ptr_array_index (n->children, i);
		LrgWidget *m = g_ptr_array_index (matched, i);

		if (m != NULL)
			g_ptr_array_add (desired, sync_widget (m, cn));
		else
			g_ptr_array_add (desired, build_widget (cn));
	}

	/* Remove live children that are not wanted any more */
	for (i = 0; i < live->len; i++)
	{
		LrgWidget *lw = g_ptr_array_index (live, i);

		if (!g_ptr_array_find (desired, lw, NULL))
			lrg_container_remove_child (c, lw);
	}

	/* Keep the order: append when the survivors are already in order,
	 * else take the owned children out and put them back in order */
	now = owned_children (c);
	in_order = now->len <= desired->len;
	for (i = 0; in_order && i < now->len; i++)
		in_order = g_ptr_array_index (now, i) == g_ptr_array_index (desired, i);
	if (!in_order)
	{
		for (i = 0; i < now->len; i++)
			lrg_container_remove_child (c, g_ptr_array_index (now, i));
		g_ptr_array_set_size (now, 0);
	}
	for (i = now->len; i < desired->len; i++)
	{
		lrg_container_add_child (c, g_ptr_array_index (desired, i));
		apply_child_attrs (g_ptr_array_index (desired, i));
	}
}

/*
 * sync_dock:
 * Diffs an LrgDockArea's panels. Panels are matched like children;
 * reused panels stay where the user put them, new panels are docked at
 * their `position:`, a rebuilt panel takes its predecessor's place (as a
 * tab beside it) and panels gone from the document are removed.
 */
static void
sync_dock (LrgWidget *w,
           UiNode    *n)
{
	LrgDockArea *area = LRG_DOCK_AREA (w);
	g_autoptr(GList) all = lrg_dock_area_get_panels (area);
	/* Held: a rebuilt panel's predecessor is released by the area */
	g_autoptr(GPtrArray) live = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GPtrArray) matched = g_ptr_array_new ();
	g_autoptr(GPtrArray) keep = g_ptr_array_new_with_free_func (g_object_unref);
	GList *l;
	guint i;

	for (l = all; l != NULL; l = l->next)
	{
		if (widget_get_record (LRG_WIDGET (l->data)) != NULL)
			g_ptr_array_add (live, g_object_ref (l->data));
	}
	match_widgets (n->children, live, matched);

	for (i = 0; i < n->children->len; i++)
	{
		UiNode *cn = g_ptr_array_index (n->children, i);
		LrgWidget *m = g_ptr_array_index (matched, i);
		LrgWidget *next;

		if (m != NULL)
		{
			next = sync_widget (m, cn);
			if (next != m)
			{
				/* Rebuilt: dock the new panel beside the old one, then drop it */
				lrg_dock_area_add_panel (area, LRG_DOCK_PANEL (next), LRG_DOCK_PANEL (m), LRG_DOCK_POSITION_CENTER);
				lrg_dock_area_remove_panel (area, LRG_DOCK_PANEL (m));
				g_ptr_array_index (matched, i) = next;
			}
		}
		else
		{
			next = build_widget (cn);
			dock_add_panel (area, LRG_DOCK_PANEL (next));
		}
		g_ptr_array_add (keep, next);
	}

	/* Panels the document no longer has */
	for (i = 0; i < live->len; i++)
	{
		LrgWidget *lw = g_ptr_array_index (live, i);

		if (!g_ptr_array_find (keep, lw, NULL))
			lrg_dock_area_remove_panel (area, LRG_DOCK_PANEL (lw));
	}
}

/* Diffs a single child set through a property (scroll box, dialog) */
static void
sync_single_child (LrgWidget *w,
                   UiNode    *n)
{
	g_autoptr(GObject) cur = NULL;
	UiNode *cn = g_ptr_array_index (n->children, 0);
	LrgWidget *next;

	g_object_get (w, n->child_prop, &cur, NULL);
	if (cur != NULL && LRG_IS_WIDGET (cur) && widget_get_record (LRG_WIDGET (cur)) != NULL &&
	    G_OBJECT_TYPE (cur) == cn->type &&
	    g_strcmp0 (widget_id (LRG_WIDGET (cur)), cn->id) == 0)
	{
		next = sync_widget (LRG_WIDGET (cur), cn);
		if (next != LRG_WIDGET (cur))
			g_object_set (w, n->child_prop, next, NULL);
		g_object_unref (next);
		return;
	}
	next = build_widget (cn);
	g_object_set (w, n->child_prop, next, NULL);
	g_object_unref (next);
}

/* Diffs a tab view's `tabs:` */
static void
sync_tabs (LrgWidget *w,
           UiNode    *n)
{
	LrgTabView *tv = LRG_TAB_VIEW (w);
	g_autoptr(GPtrArray) live = g_ptr_array_new ();
	g_autoptr(GPtrArray) nodes = g_ptr_array_new ();
	g_autoptr(GPtrArray) matched = g_ptr_array_new ();
	g_autoptr(GPtrArray) desired = g_ptr_array_new_with_free_func (g_object_unref);
	gboolean same;
	guint count;
	guint active;
	guint i;

	count = lrg_tab_view_get_tab_count (tv);
	for (i = 0; i < count; i++)
		g_ptr_array_add (live, lrg_tab_view_get_tab_content (tv, i));
	for (i = 0; i < n->tabs->len; i++)
		g_ptr_array_add (nodes, ((UiTab *)g_ptr_array_index (n->tabs, i))->content);

	match_widgets (nodes, live, matched);
	for (i = 0; i < nodes->len; i++)
	{
		LrgWidget *m = g_ptr_array_index (matched, i);

		if (m != NULL)
			g_ptr_array_add (desired, sync_widget (m, g_ptr_array_index (nodes, i)));
		else
			g_ptr_array_add (desired, build_widget (g_ptr_array_index (nodes, i)));
	}

	/* Same pages in the same order: only labels may change */
	same = desired->len == live->len;
	for (i = 0; same && i < desired->len; i++)
		same = g_ptr_array_index (desired, i) == g_ptr_array_index (live, i);
	if (same)
	{
		for (i = 0; i < n->tabs->len; i++)
		{
			UiTab *t = g_ptr_array_index (n->tabs, i);

			if (g_strcmp0 (lrg_tab_view_get_tab_label (tv, i), t->label) != 0)
				lrg_tab_view_set_tab_label (tv, i, t->label);
		}
		return;
	}

	/* Rebuild the page list, keeping the selected page index */
	active = lrg_tab_view_get_active_tab (tv);
	while (lrg_tab_view_get_tab_count (tv) > 0)
		lrg_tab_view_remove_tab (tv, lrg_tab_view_get_tab_count (tv) - 1);
	for (i = 0; i < desired->len; i++)
	{
		UiTab *t = g_ptr_array_index (n->tabs, i);

		lrg_tab_view_add_tab (tv, t->label, g_ptr_array_index (desired, i));
	}
	if (desired->len > 0)
		lrg_tab_view_set_active_tab (tv, MIN (active, desired->len - 1));
}

/*
 * sync_widget:
 * Brings @w in line with @n. Returns a new reference to @w when it was
 * reused, or a freshly built widget when it had to be rebuilt (type,
 * construct-only value or content kind changed, or a removed value
 * cannot be reset); the caller then swaps it in for @w.
 *
 * Only values whose YAML changed are applied, so live state the document
 * does not mention (typed text, scroll offsets, slider values) stays.
 */
static LrgWidget *
sync_widget (LrgWidget *w,
             UiNode    *n)
{
	WidgetRecord *rec;
	UiNode *old;
	UiAttr *old_on;
	UiAttr *new_on = NULL;
	gboolean items_changed = FALSE;
	guint i;

	if (!can_reuse (w, n))
		return build_widget (n);

	rec = widget_get_record (w);
	old = ui_node_ref (rec->node);

	if (g_strcmp0 (old->id, n->id) != 0)
	{
		lrg_widget_set_name (w, n->id);
		if (LRG_IS_DOCK_PANEL (w))
			lrg_dock_panel_set_id (LRG_DOCK_PANEL (w), n->id);
	}

	/* Removed attributes first */
	for (i = 0; i < old->attrs->len; i++)
	{
		UiAttr *b = g_ptr_array_index (old->attrs, i);

		if (b->kind != ATTR_ON && ui_node_find_attr (n, b->key) == NULL)
			reset_attr (w, rec, b);
	}

	/* Changed early attributes (classes and looks are replaced whole) */
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);
		UiAttr *b = ui_node_find_attr (old, a->key);

		if (a->kind == ATTR_ON)
		{
			new_on = a;
			continue;
		}
		if (a->late || (b != NULL && lv_equal (a->value, b->value)))
			continue;
		if (a->kind == ATTR_ITEMS)
		{
			items_changed = TRUE;
			continue;
		}
		if (b != NULL && (a->kind == ATTR_CLASS || a->kind == ATTR_LOOK))
			reset_attr (w, rec, b);
		apply_attr (w, a);
	}

	/* Combo items: keep the user's selection by text when possible */
	if (items_changed)
	{
		g_autofree gchar *sel = g_strdup (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (w)));
		UiAttr *a = ui_node_find_attr (n, "items");
		gint keep = -1;

		apply_attr (w, a);
		for (i = 0; sel != NULL && a->strv[i] != NULL; i++)
		{
			if (g_strcmp0 (a->strv[i], sel) == 0)
			{
				keep = (gint)i;
				break;
			}
		}
		lrg_combo_box_set_selected (LRG_COMBO_BOX (w), keep);
	}

	/* Content */
	if (n->has_tabs)
		sync_tabs (w, n);
	else if (n->child_prop != NULL && n->children->len == 1)
		sync_single_child (w, n);
	else if (LRG_IS_DOCK_AREA (w))
		sync_dock (w, n);
	else if (LRG_IS_CONTAINER (w))
		sync_children (w, n);

	/* Late attributes */
	for (i = 0; i < n->attrs->len; i++)
	{
		UiAttr *a = g_ptr_array_index (n->attrs, i);
		UiAttr *b = ui_node_find_attr (old, a->key);

		if (a->late && (b == NULL || !lv_equal (a->value, b->value)))
			apply_attr (w, a);
	}

	/* Handlers: reconnect only when the `on:` mapping changed */
	old_on = ui_node_find_attr (old, "on");
	if (!(old_on == NULL && new_on == NULL) &&
	    (old_on == NULL || new_on == NULL || !lv_equal (old_on->value, new_on->value)))
	{
		disconnect_handlers (w, rec);
		if (new_on != NULL)
			connect_handlers (w, rec, new_on);
	}

	ui_node_unref (rec->node);
	rec->node = ui_node_ref (n);
	ui_node_unref (old);
	return g_object_ref (w);
}

/* ==========================================================================
 * Document shortcuts
 * ========================================================================== */

/*
 * RootShortcuts:
 * The `shortcuts:` of the document a root widget was built from, kept
 * on the root (qdata). They are bound on the canvas the root lives in:
 * at once when it is already in one, otherwise when it is added to one.
 * Replacing them (hot reload) or destroying the root unbinds them.
 */
typedef struct
{
	GPtrArray *specs;    /* ShortcutSpec *, shared with the UiNode */
	GWeakRef   canvas;   /* where they are bound, or empty */
} RootShortcuts;

static GQuark
root_shortcuts_quark (void)
{
	return g_quark_from_static_string ("lrg-ui-loader-shortcuts");
}

static void
root_shortcuts_uninstall (RootShortcuts *rs)
{
	g_autoptr(GObject) canvas = g_weak_ref_get (&rs->canvas);
	guint i;

	if (canvas == NULL)
		return;
	for (i = 0; i < rs->specs->len; i++)
		lrg_canvas_remove_shortcut (LRG_CANVAS (canvas),
		                            ((ShortcutSpec *)g_ptr_array_index (rs->specs, i))->accel);
	g_weak_ref_set (&rs->canvas, NULL);
}

static void
root_shortcuts_free (gpointer data)
{
	RootShortcuts *rs = data;

	root_shortcuts_uninstall (rs);
	g_weak_ref_clear (&rs->canvas);
	g_ptr_array_unref (rs->specs);
	g_free (rs);
}

/* root_shortcuts_try_install: bind on the canvas @root is in, if any */
static void
root_shortcuts_try_install (LrgWidget *root)
{
	RootShortcuts *rs = g_object_get_qdata (G_OBJECT (root), root_shortcuts_quark ());
	g_autoptr(GObject) bound = NULL;
	LrgWidget *top;
	guint i;

	if (rs == NULL)
		return;
	bound = g_weak_ref_get (&rs->canvas);
	top = lrg_widget_get_root (root);
	if (!LRG_IS_CANVAS (top) || bound == G_OBJECT (top))
		return;
	if (bound != NULL)
		root_shortcuts_uninstall (rs);

	for (i = 0; i < rs->specs->len; i++)
	{
		ShortcutSpec *sc = g_ptr_array_index (rs->specs, i);

		lrg_canvas_add_shortcut (LRG_CANVAS (top), sc->accel, sc->action, sc->target);
	}
	g_weak_ref_set (&rs->canvas, top);
}

static void
on_root_parent_changed (GObject    *object,
                        GParamSpec *pspec,
                        gpointer    user_data)
{
	root_shortcuts_try_install (LRG_WIDGET (object));
}

/*
 * attach_shortcuts:
 * Replaces the shortcuts carried by @root with those of @node (a
 * document root). The old ones are unbound first.
 */
static void
attach_shortcuts (LrgWidget *root,
                  UiNode    *node)
{
	static GQuark hooked = 0;
	RootShortcuts *rs;

	if (hooked == 0)
		hooked = g_quark_from_static_string ("lrg-ui-loader-shortcuts-hooked");

	if (node->shortcuts == NULL || node->shortcuts->len == 0)
	{
		g_object_set_qdata (G_OBJECT (root), root_shortcuts_quark (), NULL);
		return;
	}

	/* Unbind the previous set before binding the new one */
	g_object_set_qdata (G_OBJECT (root), root_shortcuts_quark (), NULL);
	rs = g_new0 (RootShortcuts, 1);
	rs->specs = g_ptr_array_ref (node->shortcuts);
	g_weak_ref_init (&rs->canvas, NULL);
	g_object_set_qdata_full (G_OBJECT (root), root_shortcuts_quark (), rs, root_shortcuts_free);

	/* Bind now, or when the root reaches a canvas */
	if (g_object_get_qdata (G_OBJECT (root), hooked) == NULL)
	{
		g_signal_connect (root, "notify::parent", G_CALLBACK (on_root_parent_changed), NULL);
		g_object_set_qdata (G_OBJECT (root), hooked, GINT_TO_POINTER (1));
	}
	root_shortcuts_try_install (root);
}

/* ==========================================================================
 * Ids
 * ========================================================================== */

static void
weak_ref_free (gpointer data)
{
	GWeakRef *r = data;

	g_weak_ref_clear (r);
	g_free (r);
}

static GWeakRef *
weak_ref_new (gpointer obj)
{
	GWeakRef *r = g_new0 (GWeakRef, 1);

	g_weak_ref_init (r, obj);
	return r;
}

static void
collect_ids (LrgUILoader *self,
             LrgWidget   *w)
{
	WidgetRecord *rec = widget_get_record (w);
	GList *l;

	if (rec != NULL && rec->node->id != NULL && !g_hash_table_contains (self->ids, rec->node->id))
		g_hash_table_insert (self->ids, g_strdup (rec->node->id), weak_ref_new (w));
	if (LRG_IS_CONTAINER (w))
	{
		for (l = lrg_container_get_children (LRG_CONTAINER (w)); l != NULL; l = l->next)
			collect_ids (self, LRG_WIDGET (l->data));
	}
}

/* Rebuilds the id table from every live root, newest first */
static void
rebuild_ids (LrgUILoader *self)
{
	gint i;

	g_hash_table_remove_all (self->ids);
	for (i = (gint)self->roots->len - 1; i >= 0; i--)
	{
		g_autoptr(GObject) root = g_weak_ref_get (g_ptr_array_index (self->roots, i));

		if (root == NULL)
		{
			g_ptr_array_remove_index (self->roots, (guint)i);
			continue;
		}
		collect_ids (self, LRG_WIDGET (root));
	}
}

static void
remember_root (LrgUILoader *self,
               LrgWidget   *root)
{
	guint i;

	for (i = 0; i < self->roots->len; i++)
	{
		g_autoptr(GObject) r = g_weak_ref_get (g_ptr_array_index (self->roots, i));

		if (r == G_OBJECT (root))
		{
			g_ptr_array_remove_index (self->roots, i);
			break;
		}
	}
	g_ptr_array_add (self->roots, weak_ref_new (root));
}

/* ==========================================================================
 * Watched documents
 * ========================================================================== */

static void
watched_file_free (gpointer data)
{
	WatchedFile *f = data;

	if (f->monitor != NULL)
	{
		g_signal_handler_disconnect (f->monitor, f->monitor_id);
		g_file_monitor_cancel (f->monitor);
		g_object_unref (f->monitor);
	}
	g_free (f->path);
	g_free (f);
}

static void
live_doc_free (gpointer data)
{
	LiveDoc *d = data;

	g_free (d->path);
	g_weak_ref_clear (&d->host);
	g_weak_ref_clear (&d->root);
	g_ptr_array_unref (d->files);
	g_free (d);
}

/* Reads the stamp of a file: existence, mtime (with ns), size, inode */
static void
stat_file (const gchar *path,
           gboolean    *exists,
           gint64      *mtime,
           gint64      *mtime_nsec,
           gint64      *size,
           guint64     *inode)
{
	GStatBuf st;

	*exists = FALSE;
	*mtime = *mtime_nsec = *size = 0;
	*inode = 0;
	if (g_stat (path, &st) != 0)
		return;
	*exists = TRUE;
	*mtime = (gint64)st.st_mtime;
#if defined(__linux__)
	*mtime_nsec = (gint64)st.st_mtim.tv_nsec;
#endif
	*size = (gint64)st.st_size;
	*inode = (guint64)st.st_ino;
}

static gboolean
watched_file_changed (WatchedFile *f)
{
	gboolean exists;
	gint64 mtime;
	gint64 nsec;
	gint64 size;
	guint64 inode;

	stat_file (f->path, &exists, &mtime, &nsec, &size, &inode);
	return exists != f->exists || mtime != f->mtime || nsec != f->mtime_nsec ||
	       size != f->size || inode != f->inode;
}

static void
on_monitor_changed (GFileMonitor      *monitor,
                    GFile             *file,
                    GFile             *other,
                    GFileMonitorEvent  event,
                    gpointer           user_data)
{
	WatchedFile *f = user_data;

	switch (event)
	{
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_DELETED:
	case G_FILE_MONITOR_EVENT_MOVED_IN:
	case G_FILE_MONITOR_EVENT_RENAMED:
	case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
		f->doc->dirty = TRUE;
		break;
	default:
		break;
	}
}

static void
watched_file_monitor (WatchedFile *f,
                      gboolean     on)
{
	if (on && f->monitor == NULL)
	{
		g_autoptr(GFile) file = g_file_new_for_path (f->path);

		f->monitor = g_file_monitor_file (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
		if (f->monitor != NULL)
			f->monitor_id = g_signal_connect (f->monitor, "changed", G_CALLBACK (on_monitor_changed), f);
	}
	else if (!on && f->monitor != NULL)
	{
		g_signal_handler_disconnect (f->monitor, f->monitor_id);
		g_file_monitor_cancel (f->monitor);
		g_clear_object (&f->monitor);
	}
}

/* Replaces the document's file list with @paths, stamped now */
static void
live_doc_set_files (LrgUILoader *self,
                    LiveDoc     *d,
                    GPtrArray   *paths)
{
	guint i;

	g_ptr_array_set_size (d->files, 0);
	for (i = 0; i < paths->len; i++)
	{
		WatchedFile *f = g_new0 (WatchedFile, 1);

		f->path = g_strdup (g_ptr_array_index (paths, i));
		f->doc = d;
		stat_file (f->path, &f->exists, &f->mtime, &f->mtime_nsec, &f->size, &f->inode);
		watched_file_monitor (f, self->use_monitors);
		g_ptr_array_add (d->files, f);
	}
}

/*
 * live_doc_reload:
 * Re-expands the document and diffs it into the live tree. On failure
 * the tree is untouched and ::reload-failed is emitted.
 */
static gboolean
live_doc_reload (LrgUILoader  *self,
                 LiveDoc      *d,
                 GError      **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GError) local = NULL;
	g_autoptr(UiNode) node = NULL;
	g_autoptr(GObject) host = NULL;
	g_autoptr(GObject) old_root = NULL;
	g_autoptr(LrgWidget) new_root = NULL;

	node = expand_file (self, d->path, files, &local);
	if (node == NULL)
	{
		guint i;

		/* Keep watching every file we know of (old and newly found) and
		 * remember their stamps so a broken file is reported once */
		for (i = 0; i < d->files->len; i++)
		{
			WatchedFile *f = g_ptr_array_index (d->files, i);

			if (!g_ptr_array_find_with_equal_func (files, f->path, g_str_equal, NULL))
				g_ptr_array_add (files, g_strdup (f->path));
		}
		live_doc_set_files (self, d, files);
		lrg_message (LRG_LOG_DOMAIN, "UI reload of %s failed: %s", d->path, local->message);
		g_signal_emit (self, signals[SIGNAL_RELOAD_FAILED], 0, d->path, local);
		if (error != NULL)
			*error = g_steal_pointer (&local);
		return FALSE;
	}

	host = g_weak_ref_get (&d->host);
	old_root = g_weak_ref_get (&d->root);
	if (old_root != NULL)
		new_root = sync_widget (LRG_WIDGET (old_root), node);
	else
		new_root = build_widget (node);

	/* The document's shortcuts follow the (possibly new) root; a
	 * replaced root unbinds its old set when it is released */
	if (new_root != LRG_WIDGET (old_root) && old_root != NULL)
		g_object_set_qdata (old_root, root_shortcuts_quark (), NULL);
	attach_shortcuts (new_root, node);

	/* The root was rebuilt (or was gone): swap it in the host */
	if (new_root != LRG_WIDGET (old_root))
	{
		if (old_root != NULL && host != NULL &&
		    lrg_widget_get_parent (LRG_WIDGET (old_root)) == LRG_CONTAINER (host))
			lrg_container_remove_child (LRG_CONTAINER (host), LRG_WIDGET (old_root));
		if (host != NULL)
		{
			lrg_container_add_child (LRG_CONTAINER (host), new_root);
			apply_child_attrs (new_root);
		}
		g_weak_ref_set (&d->root, new_root);
		remember_root (self, new_root);
	}

	live_doc_set_files (self, d, files);
	d->dirty = FALSE;
	rebuild_ids (self);
	g_signal_emit (self, signals[SIGNAL_RELOADED], 0, d->path, new_root);
	return TRUE;
}

static LiveDoc *
find_doc (LrgUILoader *self,
          const gchar *path)
{
	g_autofree gchar *full = g_canonicalize_filename (path, NULL);
	guint i;

	for (i = 0; i < self->docs->len; i++)
	{
		LiveDoc *d = g_ptr_array_index (self->docs, i);

		if (!d->removed && g_strcmp0 (d->path, full) == 0)
			return d;
	}
	return NULL;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
named_handler_free (gpointer data)
{
	g_free (data);
}

static void
lrg_ui_loader_dispose (GObject *object)
{
	LrgUILoader *self = LRG_UI_LOADER (object);

	g_ptr_array_set_size (self->docs, 0);
	g_clear_object (&self->registry);

	G_OBJECT_CLASS (lrg_ui_loader_parent_class)->dispose (object);
}

static void
lrg_ui_loader_finalize (GObject *object)
{
	LrgUILoader *self = LRG_UI_LOADER (object);

	g_hash_table_unref (self->handlers);
	g_hash_table_unref (self->variables);
	g_hash_table_unref (self->ids);
	g_ptr_array_unref (self->roots);
	g_ptr_array_unref (self->docs);
	g_hash_table_unref (self->syntax_cache);
	if (self->self_module != NULL)
		g_module_close (self->self_module);

	G_OBJECT_CLASS (lrg_ui_loader_parent_class)->finalize (object);
}

static void
lrg_ui_loader_class_init (LrgUILoaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_ui_loader_dispose;
	object_class->finalize = lrg_ui_loader_finalize;

	q_record = g_quark_from_static_string ("lrg-ui-loader-record");

	/* Make the built-in widgets resolvable by name before first use */
	g_type_ensure (LRG_TYPE_BUTTON);
	g_type_ensure (LRG_TYPE_CHECKBOX);
	g_type_ensure (LRG_TYPE_COMBO_BOX);
	g_type_ensure (LRG_TYPE_DIALOG);
	g_type_ensure (LRG_TYPE_GRID);
	g_type_ensure (LRG_TYPE_HBOX);
	g_type_ensure (LRG_TYPE_IMAGE);
	g_type_ensure (LRG_TYPE_LABEL);
	g_type_ensure (LRG_TYPE_LIST_VIEW);
	g_type_ensure (LRG_TYPE_OVERLAY);
	g_type_ensure (LRG_TYPE_PANEL);
	g_type_ensure (LRG_TYPE_PROGRESS_BAR);
	g_type_ensure (LRG_TYPE_SCROLL_BOX);
	g_type_ensure (LRG_TYPE_SEPARATOR);
	g_type_ensure (LRG_TYPE_SLIDER);
	g_type_ensure (LRG_TYPE_SPIN_BOX);
	g_type_ensure (LRG_TYPE_TAB_VIEW);
	g_type_ensure (LRG_TYPE_TEXT_INPUT);
	g_type_ensure (LRG_TYPE_VBOX);
	g_type_ensure (LRG_TYPE_MENU);
	g_type_ensure (LRG_TYPE_MENU_BAR);
	g_type_ensure (LRG_TYPE_ANCHOR_LAYOUT);
	g_type_ensure (LRG_TYPE_SWITCHER);
	g_type_ensure (LRG_TYPE_SIZE_BOX);
	g_type_ensure (LRG_TYPE_VIEWPORT);
	g_type_ensure (LRG_TYPE_CUSTOM_WIDGET);
	g_type_ensure (LRG_TYPE_TEXT_EDITOR);
	g_type_ensure (LRG_TYPE_NODE_GRAPH);
	g_type_ensure (LRG_TYPE_TIMELINE_VIEW);
	g_type_ensure (LRG_TYPE_DOCK_AREA);
	g_type_ensure (LRG_TYPE_DOCK_PANEL);
	g_type_ensure (LRG_TYPE_PAGINATION);
	g_type_ensure (LRG_TYPE_FILE_BROWSER);
	g_type_ensure (LRG_TYPE_BLOCK_EDITOR);
	g_type_ensure (LRG_TYPE_HEADER_BAR);

	/**
	 * LrgUILoader::reloaded:
	 * @self: the loader
	 * @path: the canonical path of the document that was reloaded
	 * @root: (type LrgWidget): the document's root widget (the same
	 *   widget as before unless the root's type changed)
	 *
	 * Emitted after a watched document was reloaded and diffed into the
	 * live tree. Look widgets up again here if the document may have
	 * replaced them.
	 */
	signals[SIGNAL_RELOADED] =
		g_signal_new ("reloaded", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, LRG_TYPE_WIDGET);

	/**
	 * LrgUILoader::reload-failed:
	 * @self: the loader
	 * @path: the canonical path of the document
	 * @error: what was wrong, with the node path and position
	 *
	 * Emitted when a watched document changed but could not be loaded.
	 * The live widgets are left exactly as they were.
	 */
	signals[SIGNAL_RELOAD_FAILED] =
		g_signal_new ("reload-failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_ERROR);
}

static void
lrg_ui_loader_init (LrgUILoader *self)
{
	self->handlers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, named_handler_free);
	self->variables = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, ev_free);
	self->ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, weak_ref_free);
	self->roots = g_ptr_array_new_with_free_func (weak_ref_free);
	self->docs = g_ptr_array_new_with_free_func (live_doc_free);
	self->syntax_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgUILoader *
lrg_ui_loader_new (void)
{
	return g_object_new (LRG_TYPE_UI_LOADER, NULL);
}

void
lrg_ui_loader_set_registry (LrgUILoader *self,
                            LrgRegistry *registry)
{
	g_return_if_fail (LRG_IS_UI_LOADER (self));
	g_return_if_fail (registry == NULL || LRG_IS_REGISTRY (registry));

	g_set_object (&self->registry, registry);
}

LrgRegistry *
lrg_ui_loader_get_registry (LrgUILoader *self)
{
	g_return_val_if_fail (LRG_IS_UI_LOADER (self), NULL);

	return self->registry;
}

void
lrg_ui_loader_add_handler (LrgUILoader *self,
                           const gchar *name,
                           GCallback    callback,
                           gpointer     user_data)
{
	NamedHandler *h;

	g_return_if_fail (LRG_IS_UI_LOADER (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (callback != NULL);

	h = g_new0 (NamedHandler, 1);
	h->callback = callback;
	h->user_data = user_data;
	g_hash_table_insert (self->handlers, g_strdup (name), h);
}

void
lrg_ui_loader_set_use_symbols (LrgUILoader *self,
                               gboolean     use_symbols)
{
	g_return_if_fail (LRG_IS_UI_LOADER (self));

	self->use_symbols = use_symbols;
}

gboolean
lrg_ui_loader_get_use_symbols (LrgUILoader *self)
{
	g_return_val_if_fail (LRG_IS_UI_LOADER (self), FALSE);

	return self->use_symbols;
}

void
lrg_ui_loader_set_variable (LrgUILoader  *self,
                            const gchar  *name,
                            const GValue *value)
{
	EVal *v;

	g_return_if_fail (LRG_IS_UI_LOADER (self));
	g_return_if_fail (name != NULL);

	if (value == NULL)
	{
		g_hash_table_remove (self->variables, name);
		return;
	}

	v = g_new0 (EVal, 1);
	switch (G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (value)))
	{
	case G_TYPE_BOOLEAN:
		ev_set_bool (v, g_value_get_boolean (value));
		break;
	case G_TYPE_INT:
		ev_set_num (v, (gdouble)g_value_get_int (value));
		break;
	case G_TYPE_UINT:
		ev_set_num (v, (gdouble)g_value_get_uint (value));
		break;
	case G_TYPE_LONG:
		ev_set_num (v, (gdouble)g_value_get_long (value));
		break;
	case G_TYPE_ULONG:
		ev_set_num (v, (gdouble)g_value_get_ulong (value));
		break;
	case G_TYPE_INT64:
		ev_set_num (v, (gdouble)g_value_get_int64 (value));
		break;
	case G_TYPE_UINT64:
		ev_set_num (v, (gdouble)g_value_get_uint64 (value));
		break;
	case G_TYPE_FLOAT:
		ev_set_num (v, (gdouble)g_value_get_float (value));
		break;
	case G_TYPE_DOUBLE:
		ev_set_num (v, g_value_get_double (value));
		break;
	case G_TYPE_ENUM:
		{
			GEnumClass *ec = g_type_class_ref (G_VALUE_TYPE (value));
			GEnumValue *e = g_enum_get_value (ec, g_value_get_enum (value));

			if (e != NULL)
				ev_take_str (v, g_strdup (e->value_nick));
			else
				ev_set_num (v, (gdouble)g_value_get_enum (value));
			g_type_class_unref (ec);
			break;
		}
	case G_TYPE_STRING:
		if (g_value_get_string (value) != NULL)
			ev_take_str (v, g_strdup (g_value_get_string (value)));
		break;
	default:
		{
			GValue s = G_VALUE_INIT;

			g_value_init (&s, G_TYPE_STRING);
			if (g_value_transform (value, &s))
				ev_take_str (v, g_value_dup_string (&s));
			else
				ev_take_str (v, g_strdup_value_contents (value));
			g_value_unset (&s);
			break;
		}
	}
	g_hash_table_insert (self->variables, g_strdup (name), v);
}

LrgWidget *
lrg_ui_loader_load_from_data (LrgUILoader  *self,
                              const gchar  *data,
                              gssize        length,
                              GError      **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(UiNode) node = NULL;
	LrgWidget *root;

	g_return_val_if_fail (LRG_IS_UI_LOADER (self), NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	node = expand_text (self, data, length < 0 ? strlen (data) : (gsize)length, NULL, files, error);
	if (node == NULL)
		return NULL;
	root = build_widget (node);
	attach_shortcuts (root, node);
	remember_root (self, root);
	rebuild_ids (self);
	return root;
}

LrgWidget *
lrg_ui_loader_load_from_file (LrgUILoader  *self,
                              const gchar  *path,
                              GError      **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(UiNode) node = NULL;
	LrgWidget *root;

	g_return_val_if_fail (LRG_IS_UI_LOADER (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	node = expand_file (self, path, files, error);
	if (node == NULL)
		return NULL;
	root = build_widget (node);
	attach_shortcuts (root, node);
	remember_root (self, root);
	rebuild_ids (self);
	return root;
}

LrgWidget *
lrg_ui_loader_get_widget (LrgUILoader *self,
                          const gchar *id)
{
	GWeakRef *r;
	GObject *obj;

	g_return_val_if_fail (LRG_IS_UI_LOADER (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	r = g_hash_table_lookup (self->ids, id);
	if (r == NULL)
		return NULL;
	obj = g_weak_ref_get (r);
	if (obj == NULL)
		return NULL;
	/* The widget tree owns it; hand out a borrowed pointer */
	g_object_unref (obj);
	return LRG_WIDGET (obj);
}

LrgWidget *
lrg_ui_loader_watch_file (LrgUILoader   *self,
                          const gchar   *path,
                          LrgContainer  *host,
                          GError       **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(UiNode) node = NULL;
	g_autoptr(LrgWidget) root = NULL;
	LiveDoc *d;

	g_return_val_if_fail (LRG_IS_UI_LOADER (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (LRG_IS_CONTAINER (host), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	node = expand_file (self, path, files, error);
	if (node == NULL)
		return NULL;
	root = build_widget (node);
	attach_shortcuts (root, node);
	lrg_container_add_child (host, root);
	/* A root inside an LrgAnchorLayout / LrgSwitcher host takes its
	 * own `anchor:` / `page-name:` */
	apply_child_attrs (root);

	/* Watching the same path again replaces the previous watch */
	d = find_doc (self, path);
	if (d != NULL)
		lrg_ui_loader_unwatch_file (self, path);

	d = g_new0 (LiveDoc, 1);
	d->path = g_canonicalize_filename (path, NULL);
	g_weak_ref_init (&d->host, host);
	g_weak_ref_init (&d->root, root);
	d->files = g_ptr_array_new_with_free_func (watched_file_free);
	live_doc_set_files (self, d, files);
	g_ptr_array_add (self->docs, d);

	remember_root (self, root);
	rebuild_ids (self);
	return root;
}

gboolean
lrg_ui_loader_unwatch_file (LrgUILoader *self,
                            const gchar *path)
{
	LiveDoc *d;

	g_return_val_if_fail (LRG_IS_UI_LOADER (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	d = find_doc (self, path);
	if (d == NULL)
		return FALSE;
	if (self->polling > 0)
	{
		/* Freed after the poll loop finishes */
		d->removed = TRUE;
		g_ptr_array_set_size (d->files, 0);
		return TRUE;
	}
	g_ptr_array_remove (self->docs, d);
	return TRUE;
}

/* Drops documents unwatched during a poll */
static void
prune_docs (LrgUILoader *self)
{
	guint i = 0;

	while (i < self->docs->len)
	{
		LiveDoc *d = g_ptr_array_index (self->docs, i);

		if (d->removed)
			g_ptr_array_remove_index (self->docs, i);
		else
			i++;
	}
}

gboolean
lrg_ui_loader_poll (LrgUILoader *self)
{
	gboolean any = FALSE;
	guint n;
	guint i;
	guint j;

	g_return_val_if_fail (LRG_IS_UI_LOADER (self), FALSE);

	self->polling++;
	n = self->docs->len;
	for (i = 0; i < n && i < self->docs->len; i++)
	{
		LiveDoc *d = g_ptr_array_index (self->docs, i);
		gboolean changed = FALSE;

		if (d->removed)
			continue;
		if (self->use_monitors)
			changed = d->dirty;
		else
		{
			for (j = 0; j < d->files->len && !changed; j++)
				changed = watched_file_changed (g_ptr_array_index (d->files, j));
		}
		if (!changed)
			continue;
		d->dirty = FALSE;
		live_doc_reload (self, d, NULL);
		any = TRUE;
	}
	self->polling--;
	if (self->polling == 0)
		prune_docs (self);
	return any;
}

gboolean
lrg_ui_loader_reload (LrgUILoader  *self,
                      GError      **error)
{
	gboolean ok = TRUE;
	guint n;
	guint i;

	g_return_val_if_fail (LRG_IS_UI_LOADER (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	self->polling++;
	n = self->docs->len;
	for (i = 0; i < n && i < self->docs->len; i++)
	{
		LiveDoc *d = g_ptr_array_index (self->docs, i);

		if (d->removed)
			continue;
		if (!live_doc_reload (self, d, (ok && error != NULL) ? error : NULL))
			ok = FALSE;
	}
	self->polling--;
	if (self->polling == 0)
		prune_docs (self);
	return ok;
}

void
lrg_ui_loader_set_use_monitors (LrgUILoader *self,
                                gboolean     use_monitors)
{
	guint i;
	guint j;

	g_return_if_fail (LRG_IS_UI_LOADER (self));

	use_monitors = !!use_monitors;
	if (self->use_monitors == use_monitors)
		return;
	self->use_monitors = use_monitors;
	for (i = 0; i < self->docs->len; i++)
	{
		LiveDoc *d = g_ptr_array_index (self->docs, i);

		for (j = 0; j < d->files->len; j++)
			watched_file_monitor (g_ptr_array_index (d->files, j), use_monitors);
	}
}

gboolean
lrg_ui_loader_get_use_monitors (LrgUILoader *self)
{
	g_return_val_if_fail (LRG_IS_UI_LOADER (self), FALSE);

	return self->use_monitors;
}
