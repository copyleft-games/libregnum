/* lrg-block-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBlockEditor: a Scratch-style block programming editor.
 *
 * Structure of this file:
 *
 *  - LrgBlockKind: parsing label templates into tokens (words and
 *    typed slots) and the slot table;
 *  - LrgBlock: values, nested inputs, mouths and the next block, plus
 *    the per-block layout cache and display springs;
 *  - the editor's model operations: low-level link/unlink, adopting
 *    and orphaning trees (block-added/-removed are queued and flushed
 *    when an edit ends), the connection rules;
 *  - undo: whole-structure snapshots that keep block identity (each
 *    snapshot references every block and records its links, values
 *    and position; restoring rewires the very same objects);
 *  - layout (workspace units at zoom 1), view mapping, painting with
 *    the SDF paint list, springs stepped by the canvas clock;
 *  - interaction (palette drags, carrying stacks with a live insertion
 *    preview, panning, zooming, slot editing, dropdown menus, keys);
 *  - YAML load/save of the palette and the program.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <yaml-glib.h>

#include "lrg-block-editor.h"
#include "lrg-canvas.h"
#include "lrg-menu.h"
#include "lrg-theme.h"
#include "lrg-style.h"
#include "../paint/lrg-paint-list.h"
#include "../paint/lrg-paint-icons.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Geometry constants (workspace units at zoom 1)
 * ========================================================================== */

#define MAX_MOUTHS      (2)
#define TOKEN_GAP       (6.0f)    /* between words and slots */
#define PAD             (10.0f)   /* left/right inside a statement */
#define ARM_W           (16.0f)   /* left arm of C and E blocks */
#define BOTTOM_H        (22.0f)   /* bottom arm of C and E blocks */
#define MOUTH_MIN       (24.0f)   /* an empty mouth */
#define HAT_CAP         (18.0f)   /* dome above a hat's first row */
#define HAT_CAP_W       (100.0f)
#define NOTCH_X         (14.0f)   /* notch / tab: left offset */
#define NOTCH_W         (22.0f)   /* width at the block edge */
#define NOTCH_D         (6.0f)    /* depth */
#define RADIUS          (5.0f)
#define MIN_STMT_W      (64.0f)
#define BOOL_EMPTY_W    (36.0f)
#define SNAP_DIST       (40.0f)   /* snapping reach */
#define BUMP_X          (28.0f)   /* where pushed-out blocks go */
#define DRAG_THRESHOLD  (4.0f)    /* screen units before a press drags */
#define DOUBLE_CLICK    (0.4)     /* seconds (canvas clock) */
#define WHEEL_STEP      (1.12f)   /* zoom per wheel notch with Ctrl */
#define WHEEL_PAN       (48.0f)   /* screen units per wheel notch */
#define STRIP_W         (64.0f)   /* category strip */
#define PAL_PAD         (12.0f)
#define PAL_GAP         (12.0f)
#define PAL_HEADER_H    (30.0f)
#define CLEAN_MARGIN    (24.0f)
#define CLEAN_GAP       (32.0f)
#define PASTE_OFFSET    (24.0f)
#define TEXT_MIN_PX     (4.0f)    /* smaller text is not drawn */

/* ==========================================================================
 * Enumerations and error domain
 * ========================================================================== */

GType
lrg_block_shape_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BLOCK_SHAPE_HAT, "LRG_BLOCK_SHAPE_HAT", "hat" },
			{ LRG_BLOCK_SHAPE_STATEMENT, "LRG_BLOCK_SHAPE_STATEMENT", "statement" },
			{ LRG_BLOCK_SHAPE_C, "LRG_BLOCK_SHAPE_C", "c" },
			{ LRG_BLOCK_SHAPE_E, "LRG_BLOCK_SHAPE_E", "e" },
			{ LRG_BLOCK_SHAPE_CAP, "LRG_BLOCK_SHAPE_CAP", "cap" },
			{ LRG_BLOCK_SHAPE_REPORTER, "LRG_BLOCK_SHAPE_REPORTER", "reporter" },
			{ LRG_BLOCK_SHAPE_BOOLEAN, "LRG_BLOCK_SHAPE_BOOLEAN", "boolean" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgBlockShape"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_block_slot_type_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BLOCK_SLOT_NUMBER, "LRG_BLOCK_SLOT_NUMBER", "number" },
			{ LRG_BLOCK_SLOT_TEXT, "LRG_BLOCK_SLOT_TEXT", "text" },
			{ LRG_BLOCK_SLOT_BOOLEAN, "LRG_BLOCK_SLOT_BOOLEAN", "boolean" },
			{ LRG_BLOCK_SLOT_DROPDOWN, "LRG_BLOCK_SLOT_DROPDOWN", "dropdown" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgBlockSlotType"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_block_place_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BLOCK_PLACE_NONE, "LRG_BLOCK_PLACE_NONE", "none" },
			{ LRG_BLOCK_PLACE_NEXT, "LRG_BLOCK_PLACE_NEXT", "next" },
			{ LRG_BLOCK_PLACE_MOUTH, "LRG_BLOCK_PLACE_MOUTH", "mouth" },
			{ LRG_BLOCK_PLACE_INPUT, "LRG_BLOCK_PLACE_INPUT", "input" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgBlockPlace"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_block_yaml_parts_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_BLOCK_YAML_PALETTE, "LRG_BLOCK_YAML_PALETTE", "palette" },
			{ LRG_BLOCK_YAML_PROGRAM, "LRG_BLOCK_YAML_PROGRAM", "program" },
			{ LRG_BLOCK_YAML_ALL, "LRG_BLOCK_YAML_ALL", "all" },
			{ 0, NULL, NULL }
		};
		GType t = g_flags_register_static (g_intern_static_string ("LrgBlockYamlParts"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_block_editor_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BLOCK_EDITOR_ERROR_LABEL, "LRG_BLOCK_EDITOR_ERROR_LABEL", "label" },
			{ LRG_BLOCK_EDITOR_ERROR_PARSE, "LRG_BLOCK_EDITOR_ERROR_PARSE", "parse" },
			{ LRG_BLOCK_EDITOR_ERROR_UNKNOWN_KIND, "LRG_BLOCK_EDITOR_ERROR_UNKNOWN_KIND", "unknown-kind" },
			{ LRG_BLOCK_EDITOR_ERROR_INVALID, "LRG_BLOCK_EDITOR_ERROR_INVALID", "invalid" },
			{ LRG_BLOCK_EDITOR_ERROR_IO, "LRG_BLOCK_EDITOR_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgBlockEditorError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

G_DEFINE_QUARK (lrg-block-editor-error-quark, lrg_block_editor_error)

/* shape predicates: the connection rules in one place */
static inline gboolean
shape_is_value (LrgBlockShape s)
{
	return s == LRG_BLOCK_SHAPE_REPORTER || s == LRG_BLOCK_SHAPE_BOOLEAN;
}

/* has_top: has a notch on top, so it can hang under another block */
static inline gboolean
shape_has_top (LrgBlockShape s)
{
	return s == LRG_BLOCK_SHAPE_STATEMENT || s == LRG_BLOCK_SHAPE_C ||
	       s == LRG_BLOCK_SHAPE_E || s == LRG_BLOCK_SHAPE_CAP;
}

/* has_bottom: has a tab below, so a block can hang under it */
static inline gboolean
shape_has_bottom (LrgBlockShape s)
{
	return s == LRG_BLOCK_SHAPE_HAT || s == LRG_BLOCK_SHAPE_STATEMENT ||
	       s == LRG_BLOCK_SHAPE_C || s == LRG_BLOCK_SHAPE_E;
}

static inline guint
shape_mouths (LrgBlockShape s)
{
	return s == LRG_BLOCK_SHAPE_C ? 1u : (s == LRG_BLOCK_SHAPE_E ? 2u : 0u);
}

/* slot_accepts: may a block of @shape sit in a slot of @type? */
static inline gboolean
slot_accepts (LrgBlockSlotType type,
              LrgBlockShape    shape)
{
	switch (type)
	{
	case LRG_BLOCK_SLOT_NUMBER:
	case LRG_BLOCK_SLOT_TEXT:
		return shape_is_value (shape);
	case LRG_BLOCK_SLOT_BOOLEAN:
		return shape == LRG_BLOCK_SHAPE_BOOLEAN;
	case LRG_BLOCK_SLOT_DROPDOWN:
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * LrgBlockKind
 * ========================================================================== */

/* One piece of a label: a run of words, or a slot */
typedef struct
{
	guint   row;     /* 0, or 1 for the "else" row of an E block */
	gint    slot;    /* slot index, -1 for words */
	gchar  *text;    /* the words (NULL for slots) */
	gfloat  width;   /* measured width of the words (cache) */
} Token;

typedef struct
{
	gchar             *name;
	LrgBlockSlotType   type;
	gchar             *def;      /* default value */
	gchar            **choices;  /* dropdown only */
	gboolean           custom;   /* choices replaced after parsing */
} Slot;

struct _LrgBlockKind
{
	GObject          parent_instance;

	gchar           *id;
	LrgBlockShape    shape;
	gchar           *category;
	gchar           *label;
	gboolean         has_color;
	GrlColor         color;

	GArray          *tokens;     /* Token */
	GPtrArray       *slots;      /* Slot * */
	guint            n_rows;
	GError          *parse_error;

	/* measured word widths are valid for this font and size */
	gfloat           cache_fs;
	GrlFont         *cache_font;

	LrgBlockEditor  *editor;     /* not owned: the editor it is in */
};

G_DEFINE_FINAL_TYPE (LrgBlockKind, lrg_block_kind, G_TYPE_OBJECT)

enum
{
	KIND_PROP_0,
	KIND_PROP_ID,
	KIND_PROP_SHAPE,
	KIND_PROP_CATEGORY,
	KIND_PROP_LABEL,
	KIND_N_PROPS
};

static GParamSpec *kind_props[KIND_N_PROPS];

static void editor_kind_changed (LrgBlockEditor *self);

static void
token_clear (gpointer data)
{
	Token *t = data;

	g_free (t->text);
}

static void
slot_free (gpointer data)
{
	Slot *s = data;

	g_free (s->name);
	g_free (s->def);
	g_strfreev (s->choices);
	g_free (s);
}

/* add_words: appends a word token for @buf (trimmed), if not empty */
static void
add_words (GArray  *tokens,
           GString *buf,
           guint    row)
{
	Token t;

	g_strstrip (buf->str);
	buf->len = strlen (buf->str);
	if (buf->len > 0)
	{
		t.row = row;
		t.slot = -1;
		t.text = g_strdup (buf->str);
		t.width = 0.0f;
		g_array_append_val (tokens, t);
	}
	g_string_truncate (buf, 0);
}

static gboolean
label_fail (GError      **error,
            const gchar  *label,
            const gchar  *why)
{
	g_set_error (error, LRG_BLOCK_EDITOR_ERROR, LRG_BLOCK_EDITOR_ERROR_LABEL,
	             "block label \"%s\": %s", label, why);
	return FALSE;
}

/* valid_slot_name: letters, digits, '-' and '_' */
static gboolean
valid_slot_name (const gchar *name)
{
	const gchar *c;

	if (name == NULL || *name == '\0')
		return FALSE;
	for (c = name; *c != '\0'; c++)
		if (!g_ascii_isalnum (*c) && *c != '-' && *c != '_')
			return FALSE;
	return TRUE;
}

/*
 * parse_slot:
 *
 * One `{name:type=default}` body (without the braces, escapes already
 * resolved). Dropdown defaults are the '|' separated choices.
 */
static gboolean
parse_slot (const gchar  *label,
            const gchar  *body,
            GPtrArray    *slots,
            GError      **error)
{
	g_autofree gchar *name = NULL;
	g_autofree gchar *type = NULL;
	const gchar      *colon = strchr (body, ':');
	const gchar      *eq;
	const gchar      *def = NULL;
	Slot             *slot;
	guint             i;

	if (colon == NULL)
		return label_fail (error, label, "a slot needs a type ({name:type})");
	name = g_strndup (body, (gsize) (colon - body));
	g_strstrip (name);
	if (!valid_slot_name (name))
		return label_fail (error, label, "bad slot name");
	for (i = 0; i < slots->len; i++)
		if (g_strcmp0 (((Slot *) g_ptr_array_index (slots, i))->name, name) == 0)
			return label_fail (error, label, "duplicate slot name");

	eq = strchr (colon + 1, '=');
	type = eq != NULL ? g_strndup (colon + 1, (gsize) (eq - colon - 1)) : g_strdup (colon + 1);
	g_strstrip (type);
	if (eq != NULL)
		def = eq + 1;

	slot = g_new0 (Slot, 1);
	slot->name = g_steal_pointer (&name);
	if (g_strcmp0 (type, "number") == 0)
	{
		slot->type = LRG_BLOCK_SLOT_NUMBER;
		slot->def = g_strdup (def != NULL ? def : "0");
	}
	else if (g_strcmp0 (type, "text") == 0)
	{
		slot->type = LRG_BLOCK_SLOT_TEXT;
		slot->def = g_strdup (def != NULL ? def : "");
	}
	else if (g_strcmp0 (type, "boolean") == 0)
	{
		slot->type = LRG_BLOCK_SLOT_BOOLEAN;
		slot->def = g_strdup (def != NULL ? def : "");
	}
	else if (g_strcmp0 (type, "dropdown") == 0)
	{
		slot->type = LRG_BLOCK_SLOT_DROPDOWN;
		if (def == NULL || *def == '\0')
		{
			slot_free (slot);
			return label_fail (error, label, "a dropdown slot needs choices ({name:dropdown=a|b})");
		}
		slot->choices = g_strsplit (def, "|", -1);
		for (i = 0; slot->choices[i] != NULL; i++)
		{
			if (*slot->choices[i] == '\0')
			{
				slot_free (slot);
				return label_fail (error, label, "empty dropdown choice");
			}
		}
		slot->def = g_strdup (slot->choices[0]);
	}
	else
	{
		slot_free (slot);
		return label_fail (error, label, "unknown slot type (number, text, boolean or dropdown)");
	}
	g_ptr_array_add (slots, slot);
	return TRUE;
}

/*
 * parse_label:
 *
 * Cuts a label template into tokens: runs of words and slots. A
 * backslash escapes the next character; '{' opens a slot body that
 * ends at the next unescaped '}'; outside braces, '|' starts the
 * second row of an E block (and is an error elsewhere).
 */
static gboolean
parse_label (const gchar    *label,
             LrgBlockShape   shape,
             GArray         *tokens,
             GPtrArray      *slots,
             GError        **error)
{
	g_autoptr(GString) words = g_string_new (NULL);
	const gchar       *c = label;
	guint              row = 0;

	while (*c != '\0')
	{
		if (*c == '\\')
		{
			/* --- escaped character --- */
			if (c[1] == '\0')
				return label_fail (error, label, "dangling backslash");
			g_string_append_c (words, c[1]);
			c += 2;
		}
		else if (*c == '{')
		{
			/* --- a slot: read its body up to the closing brace --- */
			g_autoptr(GString) body = g_string_new (NULL);
			Token              t;

			c++;
			while (*c != '\0' && *c != '}')
			{
				if (*c == '\\' && c[1] != '\0')
					c++;
				else if (*c == '{')
					return label_fail (error, label, "nested '{'");
				g_string_append_c (body, *c);
				c++;
			}
			if (*c != '}')
				return label_fail (error, label, "unterminated '{'");
			c++;
			add_words (tokens, words, row);
			if (!parse_slot (label, body->str, slots, error))
				return FALSE;
			t.row = row;
			t.slot = (gint) slots->len - 1;
			t.text = NULL;
			t.width = 0.0f;
			g_array_append_val (tokens, t);
		}
		else if (*c == '}')
			return label_fail (error, label, "unbalanced '}'");
		else if (*c == '|')
		{
			/* --- second row of an E block --- */
			if (shape != LRG_BLOCK_SHAPE_E || row > 0)
				return label_fail (error, label, "'|' only separates the two rows of an E block");
			add_words (tokens, words, row);
			row = 1;
			c++;
		}
		else
		{
			g_string_append_c (words, *c);
			c++;
		}
	}
	add_words (tokens, words, row);
	return TRUE;
}

static void
lrg_block_kind_constructed (GObject *object)
{
	LrgBlockKind *self = LRG_BLOCK_KIND (object);

	G_OBJECT_CLASS (lrg_block_kind_parent_class)->constructed (object);

	if (self->id == NULL)
		self->id = g_strdup ("");
	if (self->label == NULL)
		self->label = g_strdup ("");
	self->n_rows = self->shape == LRG_BLOCK_SHAPE_E ? 2 : 1;

	/* A malformed template is kept as plain words so the kind still
	 * shows; lrg_block_kind_new() reports the error instead */
	if (!parse_label (self->label, self->shape, self->tokens, self->slots, &self->parse_error))
	{
		Token t;

		g_array_set_size (self->tokens, 0);
		g_ptr_array_set_size (self->slots, 0);
		t.row = 0;
		t.slot = -1;
		t.text = g_strdup (self->label);
		t.width = 0.0f;
		g_array_append_val (self->tokens, t);
	}
}

static void
lrg_block_kind_finalize (GObject *object)
{
	LrgBlockKind *self = LRG_BLOCK_KIND (object);

	g_free (self->id);
	g_free (self->category);
	g_free (self->label);
	g_clear_pointer (&self->tokens, g_array_unref);
	g_clear_pointer (&self->slots, g_ptr_array_unref);
	g_clear_error (&self->parse_error);
	G_OBJECT_CLASS (lrg_block_kind_parent_class)->finalize (object);
}

static void
lrg_block_kind_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgBlockKind *self = LRG_BLOCK_KIND (object);

	switch (prop_id)
	{
	case KIND_PROP_ID:
		g_value_set_string (value, self->id);
		break;
	case KIND_PROP_SHAPE:
		g_value_set_enum (value, self->shape);
		break;
	case KIND_PROP_CATEGORY:
		g_value_set_string (value, self->category);
		break;
	case KIND_PROP_LABEL:
		g_value_set_string (value, self->label);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_block_kind_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgBlockKind *self = LRG_BLOCK_KIND (object);

	switch (prop_id)
	{
	case KIND_PROP_ID:
		g_free (self->id);
		self->id = g_value_dup_string (value);
		break;
	case KIND_PROP_SHAPE:
		self->shape = (LrgBlockShape) g_value_get_enum (value);
		break;
	case KIND_PROP_CATEGORY:
		lrg_block_kind_set_category (self, g_value_get_string (value));
		break;
	case KIND_PROP_LABEL:
		g_free (self->label);
		self->label = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_block_kind_class_init (LrgBlockKindClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = lrg_block_kind_constructed;
	object_class->finalize = lrg_block_kind_finalize;
	object_class->get_property = lrg_block_kind_get_property;
	object_class->set_property = lrg_block_kind_set_property;

	/**
	 * LrgBlockKind:id:
	 *
	 * Unique id of the kind (what interpreters switch on).
	 */
	kind_props[KIND_PROP_ID] =
		g_param_spec_string ("id", "Id", "Unique id", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgBlockKind:shape:
	 *
	 * The block shape.
	 */
	kind_props[KIND_PROP_SHAPE] =
		g_param_spec_enum ("shape", "Shape", "Block shape", LRG_TYPE_BLOCK_SHAPE,
		                   LRG_BLOCK_SHAPE_STATEMENT,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgBlockKind:category:
	 *
	 * Palette category; blocks take its colour unless the kind has
	 * its own.
	 */
	kind_props[KIND_PROP_CATEGORY] =
		g_param_spec_string ("category", "Category", "Palette category", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockKind:label:
	 *
	 * The label template with its slots, see lrg_block_kind_new().
	 */
	kind_props[KIND_PROP_LABEL] =
		g_param_spec_string ("label", "Label", "Label template", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, KIND_N_PROPS, kind_props);
}

static void
lrg_block_kind_init (LrgBlockKind *self)
{
	self->tokens = g_array_new (FALSE, TRUE, sizeof (Token));
	g_array_set_clear_func (self->tokens, token_clear);
	self->slots = g_ptr_array_new_with_free_func (slot_free);
	self->shape = LRG_BLOCK_SHAPE_STATEMENT;
	self->cache_fs = -1.0f;
}

LrgBlockKind *
lrg_block_kind_new (const gchar    *id,
                    LrgBlockShape   shape,
                    const gchar    *category,
                    const gchar    *label,
                    GError        **error)
{
	LrgBlockKind *kind;

	g_return_val_if_fail (id != NULL && *id != '\0', NULL);
	g_return_val_if_fail (label != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	kind = g_object_new (LRG_TYPE_BLOCK_KIND, "id", id, "shape", shape, "category", category,
	                     "label", label, NULL);
	if (kind->parse_error != NULL)
	{
		g_propagate_error (error, g_steal_pointer (&kind->parse_error));
		g_object_unref (kind);
		return NULL;
	}
	return kind;
}

const gchar *
lrg_block_kind_get_id (LrgBlockKind *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), NULL);
	return self->id;
}

LrgBlockShape
lrg_block_kind_get_shape (LrgBlockKind *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), LRG_BLOCK_SHAPE_STATEMENT);
	return self->shape;
}

const gchar *
lrg_block_kind_get_label (LrgBlockKind *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), NULL);
	return self->label;
}

const gchar *
lrg_block_kind_get_category (LrgBlockKind *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), NULL);
	return self->category;
}

void
lrg_block_kind_set_category (LrgBlockKind *self,
                             const gchar  *category)
{
	g_return_if_fail (LRG_IS_BLOCK_KIND (self));

	if (g_strcmp0 (self->category, category) == 0)
		return;
	g_free (self->category);
	self->category = g_strdup (category);
	if (self->editor != NULL)
		editor_kind_changed (self->editor);
	g_object_notify_by_pspec (G_OBJECT (self), kind_props[KIND_PROP_CATEGORY]);
}

gboolean
lrg_block_kind_get_color (LrgBlockKind *self,
                          GrlColor     *color)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), FALSE);

	if (self->has_color && color != NULL)
		*color = self->color;
	return self->has_color;
}

void
lrg_block_kind_set_color (LrgBlockKind   *self,
                          const GrlColor *color)
{
	g_return_if_fail (LRG_IS_BLOCK_KIND (self));

	self->has_color = color != NULL;
	if (color != NULL)
		self->color = *color;
	if (self->editor != NULL)
		editor_kind_changed (self->editor);
}

guint
lrg_block_kind_get_n_slots (LrgBlockKind *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), 0);
	return self->slots->len;
}

guint
lrg_block_kind_get_n_mouths (LrgBlockKind *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), 0);
	return shape_mouths (self->shape);
}

gint
lrg_block_kind_find_slot (LrgBlockKind *self,
                          const gchar  *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), -1);

	for (i = 0; i < self->slots->len; i++)
		if (g_strcmp0 (((Slot *) g_ptr_array_index (self->slots, i))->name, name) == 0)
			return (gint) i;
	return -1;
}

static inline Slot *
kind_slot (LrgBlockKind *self,
           guint         slot)
{
	return g_ptr_array_index (self->slots, slot);
}

const gchar *
lrg_block_kind_get_slot_name (LrgBlockKind *self,
                              guint         slot)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), NULL);
	g_return_val_if_fail (slot < self->slots->len, NULL);
	return kind_slot (self, slot)->name;
}

LrgBlockSlotType
lrg_block_kind_get_slot_type (LrgBlockKind *self,
                              guint         slot)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), LRG_BLOCK_SLOT_TEXT);
	g_return_val_if_fail (slot < self->slots->len, LRG_BLOCK_SLOT_TEXT);
	return kind_slot (self, slot)->type;
}

const gchar *
lrg_block_kind_get_slot_default (LrgBlockKind *self,
                                 guint         slot)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), NULL);
	g_return_val_if_fail (slot < self->slots->len, NULL);
	return kind_slot (self, slot)->def;
}

const gchar * const *
lrg_block_kind_get_slot_choices (LrgBlockKind *self,
                                 guint         slot)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (self), NULL);
	g_return_val_if_fail (slot < self->slots->len, NULL);
	return (const gchar * const *) kind_slot (self, slot)->choices;
}

void
lrg_block_kind_set_slot_choices (LrgBlockKind        *self,
                                 guint                slot,
                                 const gchar * const *choices)
{
	Slot *s;

	g_return_if_fail (LRG_IS_BLOCK_KIND (self));
	g_return_if_fail (slot < self->slots->len);
	g_return_if_fail (choices != NULL && choices[0] != NULL);

	s = kind_slot (self, slot);
	g_return_if_fail (s->type == LRG_BLOCK_SLOT_DROPDOWN);
	g_strfreev (s->choices);
	s->choices = g_strdupv ((gchar **) choices);
	g_free (s->def);
	s->def = g_strdup (choices[0]);
	s->custom = TRUE;
	if (self->editor != NULL)
		editor_kind_changed (self->editor);
}

/* ==========================================================================
 * LrgBlock
 * ========================================================================== */

typedef struct
{
	gfloat x;
	gfloat y;
	gfloat w;
	gfloat h;
} Rect;

struct _LrgBlock
{
	GObject          parent_instance;

	LrgBlockKind    *kind;
	LrgBlockEditor  *editor;      /* not owned */

	/* --- the model --- */
	gchar          **values;      /* one per slot, NULL-terminated */
	LrgBlock       **inputs;      /* one per slot, owned */
	LrgBlock        *mouths[MAX_MOUTHS]; /* owned */
	LrgBlock        *next;        /* owned */
	LrgBlock        *parent;      /* not owned */
	LrgBlockPlace    place;
	guint            index;
	gfloat           x;           /* script position (top blocks) */
	gfloat           y;
	gboolean         running;
	gboolean         selected;

	/* --- layout, relative to the block's top-left --- */
	gfloat           w;
	gfloat           h;
	gfloat           row_y[MAX_MOUTHS];
	gfloat           row_h[MAX_MOUTHS];
	gfloat           row_off[MAX_MOUTHS]; /* hat dome above the content */
	gfloat           mouth_y[MAX_MOUTHS];
	gfloat           mouth_h[MAX_MOUTHS];
	Rect            *slot;        /* one per slot */
	gfloat          *tok_x;       /* one per token */

	/* --- where it is laid out and where it is shown --- */
	gfloat           lx;
	gfloat           ly;
	LrgSpring        sx;
	LrgSpring        sy;
	LrgSpring        glow;
	gboolean         placed;      /* springs initialised */
	gboolean         moving;      /* springs not settled */
};

G_DEFINE_FINAL_TYPE (LrgBlock, lrg_block, G_TYPE_OBJECT)

enum
{
	BLOCK_PROP_0,
	BLOCK_PROP_KIND,
	BLOCK_PROP_RUNNING,
	BLOCK_N_PROPS
};

static GParamSpec *block_props[BLOCK_N_PROPS];

static void editor_set_value  (LrgBlockEditor *self,
                               LrgBlock       *block,
                               guint           slot,
                               const gchar    *value);
static void editor_glow_changed (LrgBlockEditor *self,
                                 LrgBlock       *block);

static void
lrg_block_constructed (GObject *object)
{
	LrgBlock *self = LRG_BLOCK (object);
	guint     n;
	guint     i;

	G_OBJECT_CLASS (lrg_block_parent_class)->constructed (object);
	g_return_if_fail (self->kind != NULL);

	/* --- per-slot storage, defaults from the kind --- */
	n = self->kind->slots->len;
	self->values = g_new0 (gchar *, n + 1);
	for (i = 0; i < n; i++)
		self->values[i] = g_strdup (kind_slot (self->kind, i)->def);
	self->inputs = g_new0 (LrgBlock *, MAX (n, 1));
	self->slot = g_new0 (Rect, MAX (n, 1));
	self->tok_x = g_new0 (gfloat, MAX (self->kind->tokens->len, 1));
}

/* clear_links: drops the references to the blocks hanging under @self */
static void
block_clear_links (LrgBlock *self)
{
	guint i;

	for (i = 0; self->kind != NULL && i < self->kind->slots->len; i++)
		g_clear_object (&self->inputs[i]);
	for (i = 0; i < MAX_MOUTHS; i++)
		g_clear_object (&self->mouths[i]);
	g_clear_object (&self->next);
}

static void
lrg_block_dispose (GObject *object)
{
	LrgBlock *self = LRG_BLOCK (object);
	guint     i;

	/* The children outlive this block only if someone else holds them:
	 * they are no longer attached to anything */
	for (i = 0; self->kind != NULL && self->inputs != NULL && i < self->kind->slots->len; i++)
		if (self->inputs[i] != NULL && self->inputs[i]->parent == self)
			self->inputs[i]->parent = NULL;
	for (i = 0; i < MAX_MOUTHS; i++)
		if (self->mouths[i] != NULL && self->mouths[i]->parent == self)
			self->mouths[i]->parent = NULL;
	if (self->next != NULL && self->next->parent == self)
		self->next->parent = NULL;
	if (self->inputs != NULL)
		block_clear_links (self);
	G_OBJECT_CLASS (lrg_block_parent_class)->dispose (object);
}

static void
lrg_block_finalize (GObject *object)
{
	LrgBlock *self = LRG_BLOCK (object);

	g_strfreev (self->values);
	g_free (self->inputs);
	g_free (self->slot);
	g_free (self->tok_x);
	g_clear_object (&self->kind);
	G_OBJECT_CLASS (lrg_block_parent_class)->finalize (object);
}

static void
lrg_block_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgBlock *self = LRG_BLOCK (object);

	switch (prop_id)
	{
	case BLOCK_PROP_KIND:
		g_value_set_object (value, self->kind);
		break;
	case BLOCK_PROP_RUNNING:
		g_value_set_boolean (value, self->running);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_block_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgBlock *self = LRG_BLOCK (object);

	switch (prop_id)
	{
	case BLOCK_PROP_KIND:
		self->kind = g_value_dup_object (value);
		break;
	case BLOCK_PROP_RUNNING:
		lrg_block_set_running (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_block_class_init (LrgBlockClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = lrg_block_constructed;
	object_class->dispose = lrg_block_dispose;
	object_class->finalize = lrg_block_finalize;
	object_class->get_property = lrg_block_get_property;
	object_class->set_property = lrg_block_set_property;

	/**
	 * LrgBlock:kind:
	 *
	 * The block's kind.
	 */
	block_props[BLOCK_PROP_KIND] =
		g_param_spec_object ("kind", "Kind", "The block kind", LRG_TYPE_BLOCK_KIND,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgBlock:running:
	 *
	 * The block glows as executing, see lrg_block_set_running().
	 */
	block_props[BLOCK_PROP_RUNNING] =
		g_param_spec_boolean ("running", "Running", "Highlighted as executing", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, BLOCK_N_PROPS, block_props);
}

static void
lrg_block_init (LrgBlock *self)
{
	lrg_spring_init (&self->sx, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->sy, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->glow, 0.0f, LRG_MOTION_SMOOTH);
}

LrgBlock *
lrg_block_new (LrgBlockKind *kind)
{
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (kind), NULL);
	return g_object_new (LRG_TYPE_BLOCK, "kind", kind, NULL);
}

LrgBlockKind *
lrg_block_get_kind (LrgBlock *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	return self->kind;
}

const gchar *
lrg_block_get_kind_id (LrgBlock *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	return self->kind->id;
}

LrgBlockEditor *
lrg_block_get_editor (LrgBlock *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	return self->editor;
}

LrgBlock *
lrg_block_get_parent (LrgBlock *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	return self->parent;
}

LrgBlockPlace
lrg_block_get_place (LrgBlock *self,
                     guint    *index)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), LRG_BLOCK_PLACE_NONE);

	if (index != NULL)
		*index = self->index;
	return self->place;
}

LrgBlock *
lrg_block_get_top (LrgBlock *self)
{
	LrgBlock *b;

	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	for (b = self; b->parent != NULL; b = b->parent)
		;
	return b;
}

LrgBlock *
lrg_block_get_next (LrgBlock *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	return self->next;
}

LrgBlock *
lrg_block_get_mouth (LrgBlock *self,
                     guint     mouth)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	return mouth < MAX_MOUTHS ? self->mouths[mouth] : NULL;
}

LrgBlock *
lrg_block_get_input (LrgBlock *self,
                     guint     slot)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	g_return_val_if_fail (slot < self->kind->slots->len, NULL);
	return self->inputs[slot];
}

LrgBlock *
lrg_block_get_input_by_name (LrgBlock    *self,
                             const gchar *name)
{
	gint slot;

	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	slot = lrg_block_kind_find_slot (self->kind, name);
	return slot >= 0 ? self->inputs[slot] : NULL;
}

const gchar *
lrg_block_get_value (LrgBlock *self,
                     guint     slot)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	g_return_val_if_fail (slot < self->kind->slots->len, NULL);
	return self->values[slot];
}

const gchar *
lrg_block_get_value_by_name (LrgBlock    *self,
                             const gchar *name)
{
	gint slot;

	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	slot = lrg_block_kind_find_slot (self->kind, name);
	return slot >= 0 ? self->values[slot] : NULL;
}

/* parse_number: the whole text is a finite number */
static gboolean
parse_number (const gchar *text,
              gdouble     *out)
{
	gchar  *end = NULL;
	gdouble v;

	if (text == NULL)
		return FALSE;
	while (g_ascii_isspace (*text))
		text++;
	if (*text == '\0')
		return FALSE;
	v = g_ascii_strtod (text, &end);
	while (end != NULL && g_ascii_isspace (*end))
		end++;
	if (end == NULL || end == text || *end != '\0' || !isfinite (v))
		return FALSE;
	if (out != NULL)
		*out = v;
	return TRUE;
}

gdouble
lrg_block_get_number (LrgBlock    *self,
                      const gchar *name)
{
	gdouble v = 0.0;

	g_return_val_if_fail (LRG_IS_BLOCK (self), 0.0);
	if (!parse_number (lrg_block_get_value_by_name (self, name), &v))
		return 0.0;
	return v;
}

GVariant *
lrg_block_get_value_variant (LrgBlock *self,
                             guint     slot)
{
	gdouble v = 0.0;

	g_return_val_if_fail (LRG_IS_BLOCK (self), NULL);
	g_return_val_if_fail (slot < self->kind->slots->len, NULL);

	switch (kind_slot (self->kind, slot)->type)
	{
	case LRG_BLOCK_SLOT_NUMBER:
		if (!parse_number (self->values[slot], &v))
			v = 0.0;
		return g_variant_ref_sink (g_variant_new_double (v));
	case LRG_BLOCK_SLOT_BOOLEAN:
		return g_variant_ref_sink (g_variant_new_boolean (g_ascii_strcasecmp (self->values[slot],
		                                                                      "true") == 0));
	case LRG_BLOCK_SLOT_TEXT:
	case LRG_BLOCK_SLOT_DROPDOWN:
	default:
		return g_variant_ref_sink (g_variant_new_string (self->values[slot]));
	}
}

void
lrg_block_set_value (LrgBlock    *self,
                     guint        slot,
                     const gchar *value)
{
	g_return_if_fail (LRG_IS_BLOCK (self));
	g_return_if_fail (slot < self->kind->slots->len);
	g_return_if_fail (value != NULL);

	if (g_strcmp0 (self->values[slot], value) == 0)
		return;
	if (self->editor != NULL)
	{
		editor_set_value (self->editor, self, slot, value);
		return;
	}
	g_free (self->values[slot]);
	self->values[slot] = g_strdup (value);
}

gboolean
lrg_block_set_value_by_name (LrgBlock    *self,
                             const gchar *name,
                             const gchar *value)
{
	gint slot;

	g_return_val_if_fail (LRG_IS_BLOCK (self), FALSE);
	slot = lrg_block_kind_find_slot (self->kind, name);
	if (slot < 0)
		return FALSE;
	lrg_block_set_value (self, (guint) slot, value);
	return TRUE;
}

void
lrg_block_get_position (LrgBlock *self,
                        gfloat   *x,
                        gfloat   *y)
{
	g_return_if_fail (LRG_IS_BLOCK (self));

	if (x != NULL)
		*x = self->x;
	if (y != NULL)
		*y = self->y;
}

gboolean
lrg_block_get_running (LrgBlock *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), FALSE);
	return self->running;
}

void
lrg_block_set_running (LrgBlock *self,
                       gboolean  running)
{
	g_return_if_fail (LRG_IS_BLOCK (self));

	running = !!running;
	if (self->running == running)
		return;
	self->running = running;
	lrg_spring_set_target (&self->glow, running ? 1.0f : 0.0f);
	if (self->editor != NULL)
		editor_glow_changed (self->editor, self);
	else
		lrg_spring_snap (&self->glow, self->glow.target);
	g_object_notify_by_pspec (G_OBJECT (self), block_props[BLOCK_PROP_RUNNING]);
}

gboolean
lrg_block_get_selected (LrgBlock *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK (self), FALSE);
	return self->selected;
}

/* ==========================================================================
 * Tree helpers
 * ========================================================================== */

/*
 * collect_tree:
 *
 * Appends @b and everything hanging under it (inputs, mouths and, with
 * @follow, the blocks below) to @out, in pre-order: a block, its
 * inputs, its mouths, then the next block. The chain is walked in a
 * loop so long scripts do not recurse deeply.
 */
static void
collect_tree (LrgBlock  *b,
              gboolean   follow,
              GPtrArray *out)
{
	while (b != NULL)
	{
		guint i;

		g_ptr_array_add (out, b);
		for (i = 0; i < b->kind->slots->len; i++)
			if (b->inputs[i] != NULL)
				collect_tree (b->inputs[i], TRUE, out);
		for (i = 0; i < MAX_MOUTHS; i++)
			if (b->mouths[i] != NULL)
				collect_tree (b->mouths[i], TRUE, out);
		b = follow ? b->next : NULL;
	}
}

static LrgBlock *
last_of_chain (LrgBlock *b)
{
	while (b->next != NULL)
		b = b->next;
	return b;
}

/* inside: @inner is @outer or hangs (directly or not) under it */
static gboolean
block_inside (LrgBlock *inner,
              LrgBlock *outer)
{
	LrgBlock *b;

	for (b = inner; b != NULL; b = b->parent)
		if (b == outer)
			return TRUE;
	return FALSE;
}

/* wraps: a lone C/E block wraps the blocks it is inserted above */
static gboolean
block_wraps (LrgBlock *b)
{
	return shape_mouths (b->kind->shape) > 0 && b->next == NULL && b->mouths[0] == NULL;
}

/*
 * copy_tree:
 *
 * A free deep copy of @src (values, inputs, mouths and, with @follow,
 * the blocks below). The chain is copied in a loop.
 */
static LrgBlock *
copy_tree (LrgBlock *src,
           gboolean  follow)
{
	LrgBlock *first = NULL;
	LrgBlock *prev = NULL;

	while (src != NULL)
	{
		LrgBlock *b = lrg_block_new (src->kind);
		guint     i;

		for (i = 0; i < src->kind->slots->len; i++)
		{
			g_free (b->values[i]);
			b->values[i] = g_strdup (src->values[i]);
			if (src->inputs[i] != NULL)
			{
				b->inputs[i] = copy_tree (src->inputs[i], FALSE);
				b->inputs[i]->parent = b;
				b->inputs[i]->place = LRG_BLOCK_PLACE_INPUT;
				b->inputs[i]->index = i;
			}
		}
		for (i = 0; i < MAX_MOUTHS; i++)
		{
			if (src->mouths[i] != NULL)
			{
				b->mouths[i] = copy_tree (src->mouths[i], TRUE);
				b->mouths[i]->parent = b;
				b->mouths[i]->place = LRG_BLOCK_PLACE_MOUTH;
				b->mouths[i]->index = i;
			}
		}
		if (prev == NULL)
			first = b;
		else
		{
			prev->next = b;   /* the reference from lrg_block_new() */
			b->parent = prev;
			b->place = LRG_BLOCK_PLACE_NEXT;
			b->index = 0;
		}
		prev = b;
		src = follow ? src->next : NULL;
	}
	return first;
}

/* ==========================================================================
 * LrgBlockEditor: instance structure
 * ========================================================================== */

typedef struct
{
	gchar    *name;
	gboolean  has_color;
	GrlColor  color;
	gfloat    pal_y;      /* header top in the palette, -1 when empty */
} Category;

typedef enum
{
	DRAG_NONE,
	DRAG_PRESS,           /* button down on a program block, not moved yet */
	DRAG_PRESS_PALETTE,   /* button down on a palette block */
	DRAG_CARRY,           /* carrying a stack */
	DRAG_PAN
} Drag;

typedef struct
{
	LrgBlock *block;      /* owned */
	gboolean  added;
} Pending;

typedef struct _Snapshot Snapshot;

struct _LrgBlockEditor
{
	LrgWidget            parent_instance;

	/* --- palette --- */
	GPtrArray           *kinds;          /* LrgBlockKind, owned, insertion order */
	GHashTable          *kind_by_id;
	GPtrArray           *categories;     /* Category * */
	GPtrArray           *palette;        /* LrgBlock, one per kind, palette order */
	gboolean             palette_dirty;
	gfloat               pal_content_h;
	LrgSpring            pal_scroll;

	/* --- program --- */
	GPtrArray           *scripts;        /* LrgBlock top blocks, owned */
	GHashTable          *blocks;         /* every program block (a set) */
	GArray              *pending;        /* Pending: added/removed to emit */
	gboolean             pending_selection;
	guint                edit_depth;

	/* --- undo --- */
	GPtrArray           *undo;           /* Snapshot *, oldest first */
	GPtrArray           *redo;
	guint                undo_limit;
	gboolean             had_undo;
	gboolean             had_redo;

	/* --- view and settings --- */
	gfloat               zoom;
	gfloat               scroll_x;
	gfloat               scroll_y;
	gfloat               min_zoom;
	gfloat               max_zoom;
	gfloat               grid_size;
	gboolean             show_grid;
	gboolean             show_palette;
	gfloat               palette_width;

	/* --- layout cache --- */
	gboolean             layout_dirty;
	gfloat               m_fs;
	GrlFont             *m_font;
	gfloat               slot_h;
	gfloat               row_h;
	GPtrArray           *drawn;          /* program blocks, pre-order, not owned */
	gboolean             anim;

	/* --- pointer interaction --- */
	Drag                 drag;
	gint                 drag_button;
	gfloat               press_x;
	gfloat               press_y;
	gfloat               pointer_x;
	gfloat               pointer_y;
	gboolean             pointer_in;
	LrgBlock            *press_block;
	gint                 press_slot;
	LrgBlockKind        *press_kind;
	LrgBlock            *carried;        /* owned while carrying */
	gfloat               grab_x;         /* pointer - carried top-left (workspace) */
	gfloat               grab_y;
	gfloat               carried_h;
	Snapshot            *drag_snapshot;
	LrgBlock            *target;
	LrgBlockPlace        target_place;
	guint                target_index;
	LrgBlock            *target_top;
	gboolean             has_ghost;
	gfloat               ghost_x;
	gfloat               ghost_y;
	LrgBlock            *last_click;
	gdouble              last_click_time;
	gfloat               last_click_x;
	gfloat               last_click_y;
	gboolean             space_armed;
	gboolean             touch_panning;

	/* --- selection --- */
	LrgBlock            *selected;

	/* --- slot editing --- */
	LrgBlock            *edit_block;
	guint                edit_slot;
	GString             *edit_text;
	gsize                edit_cursor;    /* byte offset */
	gboolean             edit_all;       /* the whole text is selected */
	Snapshot            *edit_snapshot;

	/* --- dropdown menu --- */
	LrgMenu             *menu;
	LrgBlock            *menu_block;
	guint                menu_slot;
	GSimpleActionGroup  *actions;

	/* --- clipboard (a free copy) --- */
	LrgBlock            *clip;

	gdouble              clock;
	gboolean             stepped;
};

G_DEFINE_FINAL_TYPE (LrgBlockEditor, lrg_block_editor, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_ZOOM,
	PROP_SCROLL_X,
	PROP_SCROLL_Y,
	PROP_MIN_ZOOM,
	PROP_MAX_ZOOM,
	PROP_GRID_SIZE,
	PROP_SHOW_GRID,
	PROP_SHOW_PALETTE,
	PROP_PALETTE_WIDTH,
	PROP_UNDO_LIMIT,
	PROP_CAN_UNDO,
	PROP_CAN_REDO,
	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	SIGNAL_BLOCK_ADDED,
	SIGNAL_BLOCK_REMOVED,
	SIGNAL_BLOCK_ACTIVATED,
	SIGNAL_SELECTION_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void ensure_layout          (LrgBlockEditor *self);
static void finish_interaction     (LrgBlockEditor *self);
static void stop_slot_edit         (LrgBlockEditor *self);

/* smooth: springs animate (after the first frame, unless motion is off) */
static gboolean
smooth (LrgBlockEditor *self)
{
	return self->stepped && lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

/* editor_kind_changed: a kind's category or colour changed */
static void
editor_kind_changed (LrgBlockEditor *self)
{
	self->palette_dirty = TRUE;
	self->layout_dirty = TRUE;
}

static void
editor_glow_changed (LrgBlockEditor *self,
                     LrgBlock       *block)
{
	if (!smooth (self))
		lrg_spring_snap (&block->glow, block->glow.target);
	else
		self->anim = TRUE;
}

/* ==========================================================================
 * Membership: adopting and orphaning blocks
 *
 * block-added / block-removed are queued while an edit runs and
 * emitted when it ends, so handlers always see a consistent program.
 * ========================================================================== */

static void
queue_pending (LrgBlockEditor *self,
               LrgBlock       *block,
               gboolean        added)
{
	Pending p;

	p.block = g_object_ref (block);
	p.added = added;
	g_array_append_val (self->pending, p);
}

static void
flush_pending (LrgBlockEditor *self)
{
	while (self->pending->len > 0 || self->pending_selection)
	{
		g_autoptr(GArray) batch = self->pending;
		gboolean          sel = self->pending_selection;
		guint             i;

		self->pending = g_array_new (FALSE, FALSE, sizeof (Pending));
		self->pending_selection = FALSE;
		for (i = 0; i < batch->len; i++)
		{
			Pending *p = &g_array_index (batch, Pending, i);

			g_signal_emit (self, signals[p->added ? SIGNAL_BLOCK_ADDED : SIGNAL_BLOCK_REMOVED], 0,
			               p->block);
			g_object_unref (p->block);
		}
		if (sel)
			g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
	}
}

static void
adopt_block (LrgBlockEditor *self,
             LrgBlock       *b)
{
	if (b->editor == self)
		return;
	b->editor = self;
	b->placed = FALSE;
	g_hash_table_add (self->blocks, b);
	queue_pending (self, b, TRUE);
}

/* adopt_tree: a free tree joins the program */
static void
adopt_tree (LrgBlockEditor *self,
            LrgBlock       *top)
{
	g_autoptr(GPtrArray) all = g_ptr_array_new ();
	guint                i;

	collect_tree (top, TRUE, all);
	for (i = 0; i < all->len; i++)
		adopt_block (self, g_ptr_array_index (all, i));
}

/*
 * forget_block:
 *
 * One block leaves the program: everything in the editor that points
 * at it (selection, slot editing, menus, gestures) lets go.
 */
static void
forget_block (LrgBlockEditor *self,
              LrgBlock       *b)
{
	if (!g_hash_table_remove (self->blocks, b))
		return;
	b->editor = NULL;
	if (self->selected == b)
	{
		b->selected = FALSE;
		self->selected = NULL;
		self->pending_selection = TRUE;
	}
	if (self->edit_block == b)
		stop_slot_edit (self);
	if (self->menu_block == b)
		self->menu_block = NULL;
	if (self->last_click == b)
		self->last_click = NULL;
	if (self->press_block == b)
	{
		self->press_block = NULL;
		if (self->drag == DRAG_PRESS)
			self->drag = DRAG_NONE;
	}
	if (self->target == b)
		self->target = NULL;
	if (self->target_top == b)
		self->target_top = NULL;
	b->running = FALSE;
	lrg_spring_snap (&b->glow, 0.0f);
	queue_pending (self, b, FALSE);
}

static void
orphan_tree (LrgBlockEditor *self,
             LrgBlock       *top)
{
	g_autoptr(GPtrArray) all = g_ptr_array_new ();
	guint                i;

	collect_tree (top, TRUE, all);
	for (i = 0; i < all->len; i++)
		forget_block (self, g_ptr_array_index (all, i));
}

static gboolean
in_program (LrgBlockEditor *self,
            LrgBlock       *b)
{
	return b != NULL && b->editor == self && g_hash_table_contains (self->blocks, b);
}

/* ==========================================================================
 * Low-level links (no undo, no signals)
 * ========================================================================== */

/*
 * unlink_block:
 *
 * Takes @b (with what hangs under it) out of where it is and returns
 * the reference that held it there (the caller owns it). A block that
 * leaves a parent keeps its laid-out position as its script position.
 */
static LrgBlock *
unlink_block (LrgBlockEditor *self,
              LrgBlock       *b)
{
	LrgBlock *p = b->parent;
	guint     i;

	if (p == NULL)
	{
		if (g_ptr_array_find (self->scripts, b, &i))
			return g_ptr_array_steal_index (self->scripts, i);
		return g_object_ref (b);
	}
	switch (b->place)
	{
	case LRG_BLOCK_PLACE_NEXT:
		p->next = NULL;
		break;
	case LRG_BLOCK_PLACE_MOUTH:
		p->mouths[b->index] = NULL;
		break;
	case LRG_BLOCK_PLACE_INPUT:
		p->inputs[b->index] = NULL;
		break;
	case LRG_BLOCK_PLACE_NONE:
	default:
		break;
	}
	b->parent = NULL;
	b->place = LRG_BLOCK_PLACE_NONE;
	b->index = 0;
	b->x = b->lx;
	b->y = b->ly;
	return b;
}

/* link_at: hangs the free block @b (reference taken) at a place of @t */
static void
link_at (LrgBlock      *t,
         LrgBlockPlace  place,
         guint          index,
         LrgBlock      *b)
{
	switch (place)
	{
	case LRG_BLOCK_PLACE_NEXT:
		t->next = b;
		index = 0;
		break;
	case LRG_BLOCK_PLACE_MOUTH:
		t->mouths[index] = b;
		break;
	case LRG_BLOCK_PLACE_INPUT:
		t->inputs[index] = b;
		break;
	case LRG_BLOCK_PLACE_NONE:
	default:
		g_return_if_reached ();
	}
	b->parent = t;
	b->place = place;
	b->index = index;
}

/* link_script: the free block @b (reference taken) becomes a script */
static void
link_script (LrgBlockEditor *self,
             LrgBlock       *b,
             gfloat          x,
             gfloat          y)
{
	b->parent = NULL;
	b->place = LRG_BLOCK_PLACE_NONE;
	b->index = 0;
	b->x = x;
	b->y = y;
	g_ptr_array_add (self->scripts, b);
}

/* attach_ok: the connection rules (see lrg_block_editor_can_attach()) */
static gboolean
attach_ok (LrgBlock      *b,
           LrgBlock      *t,
           LrgBlockPlace  place,
           guint          index)
{
	LrgBlockShape bs = b->kind->shape;
	LrgBlockShape ts = t->kind->shape;
	LrgBlock     *occupant;

	if (block_inside (t, b))
		return FALSE;

	switch (place)
	{
	case LRG_BLOCK_PLACE_INPUT:
		return index < t->kind->slots->len && slot_accepts (kind_slot (t->kind, index)->type, bs);
	case LRG_BLOCK_PLACE_NEXT:
		if (!shape_has_top (bs) || !shape_has_bottom (ts))
			return FALSE;
		occupant = t->next;
		break;
	case LRG_BLOCK_PLACE_MOUTH:
		if (!shape_has_top (bs) || index >= shape_mouths (ts))
			return FALSE;
		occupant = t->mouths[index];
		break;
	case LRG_BLOCK_PLACE_NONE:
	default:
		return FALSE;
	}

	/* What is already there must be able to hang under the stack */
	return occupant == NULL || block_wraps (b) || shape_has_bottom (last_of_chain (b)->kind->shape);
}

/*
 * place_owned:
 *
 * Hangs the free stack @b (reference taken) at a place of @t, which
 * attach_ok() accepted. The blocks that were there hang under the
 * stack (or inside a lone C/E block); a value pushed out of a slot
 * becomes a script beside @t.
 */
static void
place_owned (LrgBlockEditor *self,
             LrgBlock       *b,
             LrgBlock       *t,
             LrgBlockPlace   place,
             guint           index)
{
	LrgBlock *rest = NULL;

	if (place == LRG_BLOCK_PLACE_INPUT)
	{
		if (t->inputs[index] != NULL)
		{
			LrgBlock *old = t->inputs[index];

			t->inputs[index] = NULL;
			link_script (self, old, t->lx + t->w + BUMP_X, t->ly);
		}
		link_at (t, place, index, b);
		return;
	}

	/* --- statements: lift what is at the place, insert, rehang --- */
	if (place == LRG_BLOCK_PLACE_NEXT)
	{
		rest = t->next;
		t->next = NULL;
	}
	else
	{
		rest = t->mouths[index];
		t->mouths[index] = NULL;
	}
	if (rest != NULL)
		rest->parent = NULL;

	link_at (t, place, index, b);
	if (rest == NULL)
		return;

	if (block_wraps (b))
		link_at (b, LRG_BLOCK_PLACE_MOUTH, 0, rest);
	else
	{
		LrgBlock *last = last_of_chain (b);

		if (shape_has_bottom (last->kind->shape))
			link_at (last, LRG_BLOCK_PLACE_NEXT, 0, rest);
		else
			link_script (self, rest, t->lx + t->w + BUMP_X, t->ly + t->h);
	}
}

/* ==========================================================================
 * Undo snapshots
 *
 * A snapshot references every program block and records its links,
 * values and position. Restoring rewires the same objects, so blocks
 * keep their identity across undo and redo.
 * ========================================================================== */

typedef struct
{
	LrgBlock       *block;      /* owned */
	gchar         **values;
	LrgBlock      **inputs;     /* not owned (every block has its own state) */
	LrgBlock       *mouths[MAX_MOUTHS];
	LrgBlock       *next;
	LrgBlock       *parent;
	LrgBlockPlace   place;
	guint           index;
	gfloat          x;
	gfloat          y;
} BlockState;

struct _Snapshot
{
	GArray    *states;          /* BlockState */
	GPtrArray *scripts;         /* LrgBlock, owned */
};

static void
block_state_clear (gpointer data)
{
	BlockState *st = data;

	g_strfreev (st->values);
	g_free (st->inputs);
	g_clear_object (&st->block);
}

static void
snapshot_free (Snapshot *snap)
{
	if (snap == NULL)
		return;
	g_array_unref (snap->states);
	g_ptr_array_unref (snap->scripts);
	g_free (snap);
}

static Snapshot *
snapshot_take (LrgBlockEditor *self)
{
	g_autoptr(GPtrArray) all = g_ptr_array_new ();
	Snapshot            *snap = g_new0 (Snapshot, 1);
	guint                i;

	snap->states = g_array_new (FALSE, TRUE, sizeof (BlockState));
	g_array_set_clear_func (snap->states, block_state_clear);
	snap->scripts = g_ptr_array_new_with_free_func (g_object_unref);

	for (i = 0; i < self->scripts->len; i++)
	{
		LrgBlock *top = g_ptr_array_index (self->scripts, i);

		g_ptr_array_add (snap->scripts, g_object_ref (top));
		collect_tree (top, TRUE, all);
	}
	for (i = 0; i < all->len; i++)
	{
		LrgBlock  *b = g_ptr_array_index (all, i);
		guint      n = b->kind->slots->len;
		BlockState st;
		guint      k;

		memset (&st, 0, sizeof st);
		st.block = g_object_ref (b);
		st.values = g_strdupv (b->values);
		st.inputs = g_new0 (LrgBlock *, MAX (n, 1));
		for (k = 0; k < n; k++)
			st.inputs[k] = b->inputs[k];
		for (k = 0; k < MAX_MOUTHS; k++)
			st.mouths[k] = b->mouths[k];
		st.next = b->next;
		st.parent = b->parent;
		st.place = b->place;
		st.index = b->index;
		st.x = b->x;
		st.y = b->y;
		g_array_append_val (snap->states, st);
	}
	return snap;
}

/*
 * snapshot_restore:
 *
 * Phase A drops every link of every block involved (all of them stay
 * alive through @old and the snapshot), phase B rewires them from the
 * snapshot, phase C updates the membership (queued signals).
 */
static void
snapshot_restore (LrgBlockEditor *self,
                  Snapshot       *snap)
{
	g_autoptr(GPtrArray)  old = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GHashTable) in_snap = g_hash_table_new (NULL, NULL);
	GHashTableIter        iter;
	gpointer              key;
	guint                 i;
	guint                 k;

	g_hash_table_iter_init (&iter, self->blocks);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (old, g_object_ref (key));

	/* --- phase A --- */
	for (i = 0; i < old->len; i++)
		block_clear_links (g_ptr_array_index (old, i));
	for (i = 0; i < snap->states->len; i++)
	{
		BlockState *st = &g_array_index (snap->states, BlockState, i);

		block_clear_links (st->block);
		g_hash_table_add (in_snap, st->block);
	}
	g_ptr_array_set_size (self->scripts, 0);

	/* --- phase B --- */
	for (i = 0; i < snap->states->len; i++)
	{
		BlockState *st = &g_array_index (snap->states, BlockState, i);
		LrgBlock   *b = st->block;

		g_strfreev (b->values);
		b->values = g_strdupv (st->values);
		for (k = 0; k < b->kind->slots->len; k++)
			b->inputs[k] = st->inputs[k] != NULL ? g_object_ref (st->inputs[k]) : NULL;
		for (k = 0; k < MAX_MOUTHS; k++)
			b->mouths[k] = st->mouths[k] != NULL ? g_object_ref (st->mouths[k]) : NULL;
		b->next = st->next != NULL ? g_object_ref (st->next) : NULL;
		b->parent = st->parent;
		b->place = st->place;
		b->index = st->index;
		b->x = st->x;
		b->y = st->y;
	}
	for (i = 0; i < snap->scripts->len; i++)
		g_ptr_array_add (self->scripts, g_object_ref (g_ptr_array_index (snap->scripts, i)));

	/* --- phase C --- */
	for (i = 0; i < old->len; i++)
	{
		LrgBlock *b = g_ptr_array_index (old, i);

		if (!g_hash_table_contains (in_snap, b))
		{
			b->parent = NULL;
			b->place = LRG_BLOCK_PLACE_NONE;
			forget_block (self, b);
		}
	}
	for (i = 0; i < snap->states->len; i++)
		adopt_block (self, g_array_index (snap->states, BlockState, i).block);
	self->layout_dirty = TRUE;
}

/* update_history_state: notifies can-undo / can-redo and the actions */
static void
update_history_state (LrgBlockEditor *self)
{
	gboolean can_undo = self->undo->len > 0;
	gboolean can_redo = self->redo->len > 0;
	GAction *a;

	if (self->actions != NULL)
	{
		a = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "undo");
		if (a != NULL)
			g_simple_action_set_enabled (G_SIMPLE_ACTION (a), can_undo);
		a = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "redo");
		if (a != NULL)
			g_simple_action_set_enabled (G_SIMPLE_ACTION (a), can_redo);
	}
	if (can_undo != self->had_undo)
	{
		self->had_undo = can_undo;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_UNDO]);
	}
	if (can_redo != self->had_redo)
	{
		self->had_redo = can_redo;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_REDO]);
	}
}

/* push_undo: @snap (taken before an edit) becomes the newest undo step */
static void
push_undo (LrgBlockEditor *self,
           Snapshot       *snap)
{
	g_ptr_array_add (self->undo, snap);
	g_ptr_array_set_size (self->redo, 0);
	while (self->undo->len > self->undo_limit)
		g_ptr_array_remove_index (self->undo, 0);
}

/* commit: an edit is complete: relayout, emit what it did, then changed */
static void
commit (LrgBlockEditor *self)
{
	self->layout_dirty = TRUE;
	flush_pending (self);
	update_history_state (self);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/*
 * edit_begin / edit_end:
 *
 * Bracket every public edit. The outermost begin finishes any pointer
 * gesture or slot edit in progress and records an undo step; the
 * outermost end commits. Nested edits form a single undo step.
 */
static void
edit_begin (LrgBlockEditor *self)
{
	if (self->edit_depth == 0)
	{
		finish_interaction (self);
		push_undo (self, snapshot_take (self));
	}
	self->edit_depth++;
}

static void
edit_end (LrgBlockEditor *self)
{
	g_return_if_fail (self->edit_depth > 0);
	if (--self->edit_depth == 0)
		commit (self);
}

static void
editor_set_value (LrgBlockEditor *self,
                  LrgBlock       *block,
                  guint           slot,
                  const gchar    *value)
{
	edit_begin (self);
	g_free (block->values[slot]);
	block->values[slot] = g_strdup (value);
	edit_end (self);
}

/* ==========================================================================
 * Layout (workspace units at zoom 1)
 * ========================================================================== */

static Category *
find_category (LrgBlockEditor *self,
               const gchar    *name)
{
	guint i;

	if (name == NULL)
		name = "";
	for (i = 0; i < self->categories->len; i++)
	{
		Category *c = g_ptr_array_index (self->categories, i);

		if (g_strcmp0 (c->name, name) == 0)
			return c;
	}
	return NULL;
}

static Category *
ensure_category (LrgBlockEditor *self,
                 const gchar    *name)
{
	Category *c = find_category (self, name);

	if (c == NULL)
	{
		c = g_new0 (Category, 1);
		c->name = g_strdup (name != NULL ? name : "");
		c->pal_y = -1.0f;
		g_ptr_array_add (self->categories, c);
		self->palette_dirty = TRUE;
	}
	return c;
}

static void
category_free (gpointer data)
{
	Category *c = data;

	g_free (c->name);
	g_free (c);
}

static void
ensure_metrics (LrgBlockEditor *self)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat    fs = theme != NULL ? lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) : 14.0f;
	GrlFont  *font = theme != NULL ? lrg_theme_get_ui_font (theme) : NULL;

	if (fs <= 0.0f)
		fs = 14.0f;
	if (fs == self->m_fs && font == self->m_font)
		return;
	self->m_fs = fs;
	self->m_font = font;
	self->slot_h = roundf (fs + 10.0f);
	self->row_h = MAX (40.0f, self->slot_h + 16.0f);
	self->layout_dirty = TRUE;
	self->palette_dirty = TRUE;
}

static gfloat
text_width (LrgBlockEditor *self,
            const gchar    *text)
{
	gfloat w = 0.0f;

	if (text == NULL || *text == '\0')
		return 0.0f;
	lrg_paint_measure_text (self->m_font, text, self->m_fs, 0.0f, &w, NULL);
	return w;
}

/* kind_measure: caches the widths of a kind's words for the font */
static void
kind_measure (LrgBlockEditor *self,
              LrgBlockKind   *kind)
{
	guint i;

	if (kind->cache_fs == self->m_fs && kind->cache_font == self->m_font)
		return;
	for (i = 0; i < kind->tokens->len; i++)
	{
		Token *t = &g_array_index (kind->tokens, Token, i);

		if (t->slot < 0)
			t->width = text_width (self, t->text);
	}
	kind->cache_fs = self->m_fs;
	kind->cache_font = self->m_font;
}

static const gchar *
shown_value (LrgBlockEditor *self,
             LrgBlock       *b,
             guint           slot)
{
	if (b == self->edit_block && slot == self->edit_slot)
		return self->edit_text->str;
	return b->values[slot];
}

static void   measure_block (LrgBlockEditor *self,
                             LrgBlock       *b);

/* gap_after: the carried stack would go right below @b */
static inline gboolean
gap_after (LrgBlockEditor *self,
           LrgBlock       *b)
{
	return self->drag == DRAG_CARRY && self->target == b && self->target_place == LRG_BLOCK_PLACE_NEXT;
}

/* measure_chain: total height of a stack (with the insertion gap) */
static gfloat
measure_chain (LrgBlockEditor *self,
               LrgBlock       *first)
{
	gfloat    h = 0.0f;
	LrgBlock *c;

	for (c = first; c != NULL; c = c->next)
	{
		measure_block (self, c);
		h += c->h;
		if (gap_after (self, c))
			h += self->carried_h;
	}
	return h;
}

/* slot_extent: size of a slot: its block, else the empty field */
static void
slot_extent (LrgBlockEditor *self,
             LrgBlock       *b,
             guint           slot,
             gfloat         *w,
             gfloat         *h)
{
	LrgBlock *input = b->inputs[slot];
	gfloat    tw;

	if (input != NULL)
	{
		measure_block (self, input);
		*w = input->w;
		*h = input->h;
		return;
	}
	*h = self->slot_h;
	tw = text_width (self, shown_value (self, b, slot));
	switch (kind_slot (b->kind, slot)->type)
	{
	case LRG_BLOCK_SLOT_BOOLEAN:
		*w = BOOL_EMPTY_W;
		break;
	case LRG_BLOCK_SLOT_DROPDOWN:
		*w = tw + 16.0f + self->slot_h * 0.6f;
		break;
	case LRG_BLOCK_SLOT_NUMBER:
	case LRG_BLOCK_SLOT_TEXT:
	default:
		*w = MAX (self->slot_h + 6.0f, tw + 16.0f);
		break;
	}
	if (b == self->edit_block && slot == self->edit_slot)
		*w += 4.0f;
}

/*
 * measure_block:
 *
 * Sizes one block from its label: per row, words and slots side by
 * side (slots centred vertically), the row as tall as its tallest
 * slot allows; C and E blocks add a mouth after each row (as tall as
 * the stack inside, with the insertion gap) and a bottom arm. Values
 * are measured first, so nested reporters size their slots.
 */
static void
measure_block (LrgBlockEditor *self,
               LrgBlock       *b)
{
	LrgBlockKind *k = b->kind;
	gboolean      value = shape_is_value (k->shape);
	gboolean      boolean = k->shape == LRG_BLOCK_SHAPE_BOOLEAN;
	guint         mouths = shape_mouths (k->shape);
	gfloat        edge = value ? (boolean ? self->slot_h * 0.5f + 4.0f : PAD) : PAD;
	gfloat        width = 0.0f;
	gfloat        y = 0.0f;
	guint         r;
	guint         i;

	kind_measure (self, k);
	for (r = 0; r < k->n_rows; r++)
	{
		gfloat   x = edge;
		gfloat   inner = 0.0f;
		gfloat   cap = (k->shape == LRG_BLOCK_SHAPE_HAT && r == 0) ? HAT_CAP : 0.0f;
		gfloat   row;
		gboolean first = TRUE;

		/* --- words and slots left to right --- */
		for (i = 0; i < k->tokens->len; i++)
		{
			Token *t = &g_array_index (k->tokens, Token, i);

			if (t->row != r)
				continue;
			if (!first)
				x += TOKEN_GAP;
			first = FALSE;
			b->tok_x[i] = x;
			if (t->slot < 0)
				x += t->width;
			else
			{
				Rect *s = &b->slot[t->slot];

				slot_extent (self, b, (guint) t->slot, &s->w, &s->h);
				s->x = x;
				inner = MAX (inner, s->h);
				x += s->w;
			}
		}
		x += edge;
		width = MAX (width, x);

		/* --- the row's height; slots centred in it --- */
		row = value ? MAX (self->slot_h + 4.0f, inner + 6.0f) : MAX (self->row_h, inner + 14.0f);
		b->row_y[r] = y;
		b->row_off[r] = cap;
		b->row_h[r] = row + cap;
		for (i = 0; i < k->tokens->len; i++)
		{
			Token *t = &g_array_index (k->tokens, Token, i);

			if (t->row == r && t->slot >= 0)
				b->slot[t->slot].y = y + cap + (row - b->slot[t->slot].h) * 0.5f;
		}
		y += row + cap;

		/* --- the mouth under the row --- */
		if (r < mouths)
		{
			gfloat mh = measure_chain (self, b->mouths[r]);

			if (self->drag == DRAG_CARRY && self->target == b &&
			    self->target_place == LRG_BLOCK_PLACE_MOUTH && self->target_index == r)
				mh += self->carried_h;
			b->mouth_y[r] = y;
			b->mouth_h[r] = MAX (MOUTH_MIN, mh);
			y += b->mouth_h[r];
		}
	}
	if (mouths > 0)
	{
		y += BOTTOM_H;
		width = MAX (width, ARM_W + 80.0f);
	}
	if (!value)
		width = MAX (width, MIN_STMT_W);
	if (k->shape == LRG_BLOCK_SHAPE_HAT)
		width = MAX (width, HAT_CAP_W + 12.0f);
	b->w = ceilf (width);
	b->h = y;
}

static void place_chain (LrgBlockEditor *self,
                         LrgBlock       *first,
                         gfloat          x,
                         gfloat          y,
                         gboolean        drawn);

/* place_tree: positions a block, its inputs and its mouths */
static void
place_tree (LrgBlockEditor *self,
            LrgBlock       *b,
            gfloat          x,
            gfloat          y,
            gboolean        drawn)
{
	guint i;

	b->lx = x;
	b->ly = y;
	if (drawn)
		g_ptr_array_add (self->drawn, b);
	for (i = 0; i < b->kind->slots->len; i++)
		if (b->inputs[i] != NULL)
			place_tree (self, b->inputs[i], x + b->slot[i].x, y + b->slot[i].y, drawn);
	for (i = 0; i < shape_mouths (b->kind->shape); i++)
	{
		gfloat my = y + b->mouth_y[i];

		if (self->drag == DRAG_CARRY && self->target == b &&
		    self->target_place == LRG_BLOCK_PLACE_MOUTH && self->target_index == i)
		{
			self->has_ghost = TRUE;
			self->ghost_x = x + ARM_W;
			self->ghost_y = my;
			my += self->carried_h;
		}
		place_chain (self, b->mouths[i], x + ARM_W, my, drawn);
	}
}

static void
place_chain (LrgBlockEditor *self,
             LrgBlock       *first,
             gfloat          x,
             gfloat          y,
             gboolean        drawn)
{
	LrgBlock *c;

	for (c = first; c != NULL; c = c->next)
	{
		place_tree (self, c, x, y, drawn);
		y += c->h;
		if (gap_after (self, c))
		{
			self->has_ghost = TRUE;
			self->ghost_x = x;
			self->ghost_y = y;
			y += self->carried_h;
		}
	}
}

/* rebuild_palette: one palette block per kind, grouped by category */
static void
rebuild_palette (LrgBlockEditor *self)
{
	guint c;
	guint i;

	g_ptr_array_set_size (self->palette, 0);
	for (c = 0; c < self->categories->len; c++)
	{
		Category *cat = g_ptr_array_index (self->categories, c);

		for (i = 0; i < self->kinds->len; i++)
		{
			LrgBlockKind *k = g_ptr_array_index (self->kinds, i);

			if (g_strcmp0 (k->category != NULL ? k->category : "", cat->name) == 0)
				g_ptr_array_add (self->palette, lrg_block_new (k));
		}
	}
	self->palette_dirty = FALSE;
}

/* update_springs: shown positions follow the layout */
static void
update_springs (LrgBlockEditor *self,
                LrgBlock       *b,
                gboolean        stick)
{
	if (!b->placed || stick || !smooth (self))
	{
		lrg_spring_snap (&b->sx, b->lx);
		lrg_spring_snap (&b->sy, b->ly);
		b->placed = TRUE;
		b->moving = FALSE;
		return;
	}
	if (b->sx.target != b->lx || b->sy.target != b->ly)
	{
		lrg_spring_set_target (&b->sx, b->lx);
		lrg_spring_set_target (&b->sy, b->ly);
		b->moving = TRUE;
		self->anim = TRUE;
	}
}

/*
 * layout:
 *
 * Sizes and places every script (the carried stack first, as its
 * height opens the insertion gap), the ghost of the insertion point,
 * and the palette column; then retargets the display springs.
 */
static void
layout (LrgBlockEditor *self)
{
	gfloat y;
	guint  c;
	guint  i;

	ensure_metrics (self);
	if (self->palette_dirty)
		rebuild_palette (self);

	g_ptr_array_set_size (self->drawn, 0);
	self->has_ghost = FALSE;

	/* --- the carried stack --- */
	if (self->carried != NULL)
	{
		/* (never a target itself, so it has no gap of its own) */
		self->carried_h = measure_chain (self, self->carried);
		place_chain (self, self->carried, self->carried->x, self->carried->y, FALSE);
	}

	/* --- scripts --- */
	for (i = 0; i < self->scripts->len; i++)
		measure_chain (self, g_ptr_array_index (self->scripts, i));
	for (i = 0; i < self->scripts->len; i++)
	{
		LrgBlock *top = g_ptr_array_index (self->scripts, i);

		place_chain (self, top, top->x, top->y, TRUE);
	}
	if (self->drag == DRAG_CARRY && self->target_top != NULL)
	{
		self->has_ghost = TRUE;
		self->ghost_x = self->target_top->x;
		self->ghost_y = self->target_top->y - self->carried_h;
	}

	/* --- palette: category header, then its blocks --- */
	y = PAL_PAD;
	for (c = 0; c < self->categories->len; c++)
	{
		Category *cat = g_ptr_array_index (self->categories, c);
		gboolean  any = FALSE;

		cat->pal_y = -1.0f;
		for (i = 0; i < self->palette->len; i++)
		{
			LrgBlock *b = g_ptr_array_index (self->palette, i);

			if (g_strcmp0 (b->kind->category != NULL ? b->kind->category : "", cat->name) != 0)
				continue;
			if (!any)
			{
				cat->pal_y = y;
				y += PAL_HEADER_H;
				any = TRUE;
			}
			measure_block (self, b);
			place_tree (self, b, PAL_PAD, y, FALSE);
			y += b->h + PAL_GAP;
		}
		if (any)
			y += 6.0f;
	}
	self->pal_content_h = y + PAL_PAD;

	/* --- display springs --- */
	for (i = 0; i < self->drawn->len; i++)
		update_springs (self, g_ptr_array_index (self->drawn, i), FALSE);
	if (self->carried != NULL)
	{
		g_autoptr(GPtrArray) all = g_ptr_array_new ();

		collect_tree (self->carried, TRUE, all);
		for (i = 0; i < all->len; i++)
			update_springs (self, g_ptr_array_index (all, i), TRUE);
	}
	for (i = 0; i < self->palette->len; i++)
	{
		g_autoptr(GPtrArray) all = g_ptr_array_new ();
		guint                k;

		collect_tree (g_ptr_array_index (self->palette, i), TRUE, all);
		for (k = 0; k < all->len; k++)
			update_springs (self, g_ptr_array_index (all, k), TRUE);
	}
	self->layout_dirty = FALSE;
}

static void
ensure_layout (LrgBlockEditor *self)
{
	ensure_metrics (self);
	if (self->layout_dirty || self->palette_dirty)
		layout (self);
}

/* ==========================================================================
 * View mapping and areas
 * ========================================================================== */

/* areas: the category strip, the palette list and the workspace */
static void
areas (LrgBlockEditor *self,
       Rect           *strip,
       Rect           *pal,
       Rect           *ws)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     wx = lrg_widget_get_world_x (w);
	gfloat     wy = lrg_widget_get_world_y (w);
	gfloat     ww = lrg_widget_get_width (w);
	gfloat     wh = lrg_widget_get_height (w);
	gfloat     sw = self->show_palette ? MIN (STRIP_W, ww) : 0.0f;
	gfloat     pw = self->show_palette ? MIN (self->palette_width, MAX (0.0f, ww - sw)) : 0.0f;
	gboolean   rtl = lrg_widget_is_rtl (w);
	Rect       s;
	Rect       p;
	Rect       k;

	s.y = p.y = k.y = wy;
	s.h = p.h = k.h = wh;
	s.w = sw;
	p.w = pw;
	k.w = MAX (0.0f, ww - sw - pw);
	if (!rtl)
	{
		s.x = wx;
		p.x = wx + sw;
		k.x = wx + sw + pw;
	}
	else
	{
		s.x = wx + ww - sw;
		p.x = wx + ww - sw - pw;
		k.x = wx;
	}
	if (strip != NULL)
		*strip = s;
	if (pal != NULL)
		*pal = p;
	if (ws != NULL)
		*ws = k;
}

static inline gboolean
rect_has (const Rect *r,
          gfloat      x,
          gfloat      y)
{
	return r->w > 0.0f && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

void
lrg_block_editor_workspace_to_screen (LrgBlockEditor *self,
                                      gfloat          wx,
                                      gfloat          wy,
                                      gfloat         *sx,
                                      gfloat         *sy)
{
	Rect ws;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	areas (self, NULL, NULL, &ws);
	if (sx != NULL)
		*sx = ws.x + (wx - self->scroll_x) * self->zoom;
	if (sy != NULL)
		*sy = ws.y + (wy - self->scroll_y) * self->zoom;
}

void
lrg_block_editor_screen_to_workspace (LrgBlockEditor *self,
                                      gfloat          sx,
                                      gfloat          sy,
                                      gfloat         *wx,
                                      gfloat         *wy)
{
	Rect ws;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	areas (self, NULL, NULL, &ws);
	if (wx != NULL)
		*wx = self->scroll_x + (sx - ws.x) / self->zoom;
	if (wy != NULL)
		*wy = self->scroll_y + (sy - ws.y) / self->zoom;
}

void
lrg_block_editor_get_workspace_bounds (LrgBlockEditor *self,
                                       gfloat         *x,
                                       gfloat         *y,
                                       gfloat         *width,
                                       gfloat         *height)
{
	Rect ws;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	areas (self, NULL, NULL, &ws);
	if (x != NULL)
		*x = ws.x;
	if (y != NULL)
		*y = ws.y;
	if (width != NULL)
		*width = ws.w;
	if (height != NULL)
		*height = ws.h;
}

void
lrg_block_editor_get_palette_bounds (LrgBlockEditor *self,
                                     gfloat         *x,
                                     gfloat         *y,
                                     gfloat         *width,
                                     gfloat         *height)
{
	Rect s;
	Rect p;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	areas (self, &s, &p, NULL);
	if (x != NULL)
		*x = MIN (s.x, p.x);
	if (y != NULL)
		*y = s.y;
	if (width != NULL)
		*width = s.w + p.w;
	if (height != NULL)
		*height = s.h;
}

/* in_palette_area: the point is over the palette or its strip */
static gboolean
in_palette_area (LrgBlockEditor *self,
                 gfloat          x,
                 gfloat          y)
{
	Rect s;
	Rect p;

	areas (self, &s, &p, NULL);
	return rect_has (&s, x, y) || rect_has (&p, x, y);
}

static void
notify_view (LrgBlockEditor *self,
             gfloat          oz,
             gfloat          ox,
             gfloat          oy)
{
	if (oz != self->zoom)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
	if (ox != self->scroll_x)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_X]);
	if (oy != self->scroll_y)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_Y]);
}

gfloat
lrg_block_editor_get_zoom (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 1.0f);
	return self->zoom;
}

void
lrg_block_editor_zoom_at (LrgBlockEditor *self,
                          gfloat          x,
                          gfloat          y,
                          gfloat          zoom)
{
	gfloat wx;
	gfloat wy;
	gfloat oz;
	gfloat ox;
	gfloat oy;
	Rect   ws;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	if (!isfinite (zoom))
		return;
	oz = self->zoom;
	ox = self->scroll_x;
	oy = self->scroll_y;

	/* The workspace point under (x, y) stays under it */
	lrg_block_editor_screen_to_workspace (self, x, y, &wx, &wy);
	areas (self, NULL, NULL, &ws);
	self->zoom = CLAMP (zoom, self->min_zoom, self->max_zoom);
	self->scroll_x = wx - (x - ws.x) / self->zoom;
	self->scroll_y = wy - (y - ws.y) / self->zoom;
	notify_view (self, oz, ox, oy);
}

void
lrg_block_editor_set_zoom (LrgBlockEditor *self,
                           gfloat          zoom)
{
	Rect ws;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	areas (self, NULL, NULL, &ws);
	lrg_block_editor_zoom_at (self, ws.x, ws.y, zoom);
}

void
lrg_block_editor_get_scroll (LrgBlockEditor *self,
                             gfloat         *x,
                             gfloat         *y)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	if (x != NULL)
		*x = self->scroll_x;
	if (y != NULL)
		*y = self->scroll_y;
}

void
lrg_block_editor_set_scroll (LrgBlockEditor *self,
                             gfloat          x,
                             gfloat          y)
{
	gfloat ox;
	gfloat oy;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	if (!isfinite (x) || !isfinite (y))
		return;
	ox = self->scroll_x;
	oy = self->scroll_y;
	self->scroll_x = x;
	self->scroll_y = y;
	notify_view (self, self->zoom, ox, oy);
}

void
lrg_block_editor_pan_by (LrgBlockEditor *self,
                         gfloat          dx,
                         gfloat          dy)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	lrg_block_editor_set_scroll (self, self->scroll_x - dx / self->zoom, self->scroll_y - dy / self->zoom);
}

void
lrg_block_editor_set_zoom_range (LrgBlockEditor *self,
                                 gfloat          min_zoom,
                                 gfloat          max_zoom)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	g_return_if_fail (min_zoom > 0.0f && max_zoom >= min_zoom);

	g_object_freeze_notify (G_OBJECT (self));
	if (self->min_zoom != min_zoom)
	{
		self->min_zoom = min_zoom;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_ZOOM]);
	}
	if (self->max_zoom != max_zoom)
	{
		self->max_zoom = max_zoom;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_ZOOM]);
	}
	if (self->zoom < min_zoom || self->zoom > max_zoom)
		lrg_block_editor_set_zoom (self, self->zoom);
	g_object_thaw_notify (G_OBJECT (self));
}

gfloat
lrg_block_editor_get_min_zoom (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 0.3f);
	return self->min_zoom;
}

gfloat
lrg_block_editor_get_max_zoom (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 3.0f);
	return self->max_zoom;
}

/* ==========================================================================
 * Geometry queries and hit testing
 * ========================================================================== */

gboolean
lrg_block_editor_get_block_bounds (LrgBlockEditor *self,
                                   LrgBlock       *block,
                                   gfloat         *x,
                                   gfloat         *y,
                                   gfloat         *width,
                                   gfloat         *height)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_BLOCK (block), FALSE);

	if (!in_program (self, block))
		return FALSE;
	ensure_layout (self);
	lrg_block_editor_workspace_to_screen (self, block->sx.value, block->sy.value, x, y);
	if (width != NULL)
		*width = block->w * self->zoom;
	if (height != NULL)
		*height = block->h * self->zoom;
	return TRUE;
}

gboolean
lrg_block_editor_get_slot_bounds (LrgBlockEditor *self,
                                  LrgBlock       *block,
                                  guint           slot,
                                  gfloat         *x,
                                  gfloat         *y,
                                  gfloat         *width,
                                  gfloat         *height)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_BLOCK (block), FALSE);
	g_return_val_if_fail (slot < block->kind->slots->len, FALSE);

	if (!in_program (self, block))
		return FALSE;
	ensure_layout (self);
	lrg_block_editor_workspace_to_screen (self, block->sx.value + block->slot[slot].x,
	                                      block->sy.value + block->slot[slot].y, x, y);
	if (width != NULL)
		*width = block->slot[slot].w * self->zoom;
	if (height != NULL)
		*height = block->slot[slot].h * self->zoom;
	return TRUE;
}

/* palette_origin: screen position of palette coordinate (0, 0) */
static void
palette_origin (LrgBlockEditor *self,
                gfloat         *ox,
                gfloat         *oy)
{
	Rect p;

	areas (self, NULL, &p, NULL);
	*ox = p.x;
	*oy = p.y - roundf (self->pal_scroll.value);
}

static LrgBlock *
palette_block_for (LrgBlockEditor *self,
                   LrgBlockKind   *kind)
{
	guint i;

	for (i = 0; i < self->palette->len; i++)
	{
		LrgBlock *b = g_ptr_array_index (self->palette, i);

		if (b->kind == kind)
			return b;
	}
	return NULL;
}

gboolean
lrg_block_editor_get_palette_block_bounds (LrgBlockEditor *self,
                                           const gchar    *kind_id,
                                           gfloat         *x,
                                           gfloat         *y,
                                           gfloat         *width,
                                           gfloat         *height)
{
	LrgBlockKind *kind;
	LrgBlock     *b;
	gfloat        ox;
	gfloat        oy;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (kind_id != NULL, FALSE);

	kind = g_hash_table_lookup (self->kind_by_id, kind_id);
	if (kind == NULL || !self->show_palette)
		return FALSE;
	ensure_layout (self);
	b = palette_block_for (self, kind);
	if (b == NULL)
		return FALSE;
	palette_origin (self, &ox, &oy);
	if (x != NULL)
		*x = ox + b->lx;
	if (y != NULL)
		*y = oy + b->ly;
	if (width != NULL)
		*width = b->w;
	if (height != NULL)
		*height = b->h;
	return TRUE;
}

/* on_block: (px, py) is on @b itself (not in a mouth), @b at (bx, by) */
static gboolean
on_block (LrgBlock *b,
          gfloat    bx,
          gfloat    by,
          gfloat    px,
          gfloat    py)
{
	guint i;

	if (px < bx || py < by || px > bx + b->w || py > by + b->h)
		return FALSE;
	for (i = 0; i < shape_mouths (b->kind->shape); i++)
		if (px > bx + ARM_W && py >= by + b->mouth_y[i] && py < by + b->mouth_y[i] + b->mouth_h[i])
			return FALSE;
	return TRUE;
}

/* slot_under: the slot of @b (at bx, by) under the point, or -1 */
static gint
slot_under (LrgBlock *b,
            gfloat    bx,
            gfloat    by,
            gfloat    px,
            gfloat    py)
{
	guint i;

	for (i = 0; i < b->kind->slots->len; i++)
	{
		Rect *s = &b->slot[i];

		if (b->inputs[i] == NULL && px >= bx + s->x && px <= bx + s->x + s->w &&
		    py >= by + s->y && py <= by + s->y + s->h)
			return (gint) i;
	}
	return -1;
}

LrgBlock *
lrg_block_editor_block_at (LrgBlockEditor *self,
                           gfloat          x,
                           gfloat          y,
                           gint           *slot)
{
	Rect   ws;
	gfloat px;
	gfloat py;
	guint  i;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);

	if (slot != NULL)
		*slot = -1;
	ensure_layout (self);
	areas (self, NULL, NULL, &ws);
	if (!rect_has (&ws, x, y))
		return NULL;
	lrg_block_editor_screen_to_workspace (self, x, y, &px, &py);

	/* Reverse pre-order: nested and later blocks first */
	for (i = self->drawn->len; i > 0; i--)
	{
		LrgBlock *b = g_ptr_array_index (self->drawn, i - 1);

		if (!on_block (b, b->sx.value, b->sy.value, px, py))
			continue;
		if (slot != NULL)
			*slot = slot_under (b, b->sx.value, b->sy.value, px, py);
		return b;
	}
	return NULL;
}

/* palette_block_at: the palette block under a screen point */
static LrgBlock *
palette_block_at (LrgBlockEditor *self,
                  gfloat          x,
                  gfloat          y)
{
	Rect   p;
	gfloat ox;
	gfloat oy;
	guint  i;

	ensure_layout (self);
	areas (self, NULL, &p, NULL);
	if (!rect_has (&p, x, y))
		return NULL;
	palette_origin (self, &ox, &oy);
	for (i = 0; i < self->palette->len; i++)
	{
		LrgBlock *b = g_ptr_array_index (self->palette, i);

		if (on_block (b, b->lx, b->ly, x - ox, y - oy))
			return b;
	}
	return NULL;
}

/* ==========================================================================
 * Palette and categories
 * ========================================================================== */

LrgBlockEditor *
lrg_block_editor_new (void)
{
	return g_object_new (LRG_TYPE_BLOCK_EDITOR, NULL);
}

gboolean
lrg_block_editor_add_kind (LrgBlockEditor *self,
                           LrgBlockKind   *kind)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_BLOCK_KIND (kind), FALSE);
	g_return_val_if_fail (kind->editor == NULL || kind->editor == self, FALSE);

	if (g_hash_table_contains (self->kind_by_id, kind->id))
		return FALSE;
	ensure_category (self, kind->category);
	g_ptr_array_add (self->kinds, g_object_ref (kind));
	g_hash_table_insert (self->kind_by_id, kind->id, kind);
	kind->editor = self;
	editor_kind_changed (self);
	return TRUE;
}

LrgBlockKind *
lrg_block_editor_define_kind (LrgBlockEditor  *self,
                              const gchar     *id,
                              LrgBlockShape    shape,
                              const gchar     *category,
                              const gchar     *label,
                              GError         **error)
{
	g_autoptr(LrgBlockKind) kind = NULL;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (g_hash_table_contains (self->kind_by_id, id))
	{
		g_set_error (error, LRG_BLOCK_EDITOR_ERROR, LRG_BLOCK_EDITOR_ERROR_INVALID,
		             "a block kind \"%s\" exists already", id);
		return NULL;
	}
	kind = lrg_block_kind_new (id, shape, category, label, error);
	if (kind == NULL)
		return NULL;
	lrg_block_editor_add_kind (self, kind);
	return kind;   /* the editor holds it */
}

/* kind_in_use: some program block (or the carried stack) is of @kind */
static gboolean
kind_in_use (LrgBlockEditor *self,
             LrgBlockKind   *kind)
{
	GHashTableIter iter;
	gpointer       key;

	g_hash_table_iter_init (&iter, self->blocks);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		if (((LrgBlock *) key)->kind == kind)
			return TRUE;
	return FALSE;
}

gboolean
lrg_block_editor_remove_kind (LrgBlockEditor *self,
                              const gchar    *id)
{
	LrgBlockKind *kind;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	kind = g_hash_table_lookup (self->kind_by_id, id);
	if (kind == NULL || kind_in_use (self, kind))
		return FALSE;
	finish_interaction (self);
	if (self->press_kind == kind)
		self->press_kind = NULL;
	g_hash_table_remove (self->kind_by_id, id);
	kind->editor = NULL;
	g_ptr_array_remove (self->kinds, kind);
	editor_kind_changed (self);
	return TRUE;
}

LrgBlockKind *
lrg_block_editor_get_kind (LrgBlockEditor *self,
                           const gchar    *id)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	return g_hash_table_lookup (self->kind_by_id, id);
}

guint
lrg_block_editor_get_n_kinds (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 0);
	return self->kinds->len;
}

GList *
lrg_block_editor_get_kinds (LrgBlockEditor *self)
{
	GList *list = NULL;
	guint  i;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);

	ensure_layout (self);
	for (i = self->palette->len; i > 0; i--)
		list = g_list_prepend (list, ((LrgBlock *) g_ptr_array_index (self->palette, i - 1))->kind);
	return list;
}

/* drop_program: removes every script silently (no undo step) */
static void
drop_program (LrgBlockEditor *self)
{
	while (self->scripts->len > 0)
	{
		LrgBlock *top = g_ptr_array_steal_index (self->scripts, self->scripts->len - 1);

		orphan_tree (self, top);
		g_object_unref (top);
	}
}

/* drop_palette: removes every kind and category (the program must be empty) */
static void
drop_palette (LrgBlockEditor *self)
{
	guint i;

	for (i = 0; i < self->kinds->len; i++)
		((LrgBlockKind *) g_ptr_array_index (self->kinds, i))->editor = NULL;
	g_hash_table_remove_all (self->kind_by_id);
	g_ptr_array_set_size (self->kinds, 0);
	g_ptr_array_set_size (self->categories, 0);
	g_ptr_array_set_size (self->palette, 0);
	g_clear_object (&self->clip);
	self->press_kind = NULL;
	editor_kind_changed (self);
}

void
lrg_block_editor_clear_palette (LrgBlockEditor *self)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	finish_interaction (self);
	drop_program (self);
	drop_palette (self);
	lrg_block_editor_clear_history (self);
	commit (self);
}

void
lrg_block_editor_add_category (LrgBlockEditor *self,
                               const gchar    *name,
                               const GrlColor *color)
{
	Category *c;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	g_return_if_fail (name != NULL);

	c = ensure_category (self, name);
	c->has_color = color != NULL;
	if (color != NULL)
		c->color = *color;
	editor_kind_changed (self);
}

gchar **
lrg_block_editor_get_categories (LrgBlockEditor *self)
{
	GStrvBuilder *builder;
	gchar       **out;
	guint         i;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);

	builder = g_strv_builder_new ();
	for (i = 0; i < self->categories->len; i++)
		g_strv_builder_add (builder, ((Category *) g_ptr_array_index (self->categories, i))->name);
	out = g_strv_builder_end (builder);
	g_strv_builder_unref (builder);
	return out;
}

/* Familiar Scratch colours, then a palette for other names */
static const struct
{
	const gchar *name;
	guint32      rgb;
} known_categories[] = {
	{ "motion", 0x4c97ff },
	{ "looks", 0x9966ff },
	{ "sound", 0xcf63cf },
	{ "events", 0xffbf00 },
	{ "control", 0xffab19 },
	{ "sensing", 0x5cb1d6 },
	{ "operators", 0x59c059 },
	{ "variables", 0xff8c1a },
	{ "lists", 0xff661a },
	{ "custom", 0xff6680 },
	{ "pen", 0x0fbd8c }
};

void
lrg_block_editor_get_category_color (LrgBlockEditor *self,
                                     const gchar    *name,
                                     GrlColor       *color)
{
	Category *c;
	LrgTheme *theme;
	guint32   rgb;
	guint     i;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	g_return_if_fail (color != NULL);

	c = find_category (self, name);
	if (c != NULL && c->has_color)
	{
		*color = c->color;
		return;
	}

	/* --- default: the familiar colour, else one picked by name --- */
	rgb = known_categories[0].rgb;
	if (name == NULL || *name == '\0')
		rgb = 0x8a8f98;
	else
	{
		gboolean found = FALSE;

		for (i = 0; i < G_N_ELEMENTS (known_categories); i++)
		{
			if (g_ascii_strcasecmp (known_categories[i].name, name) == 0)
			{
				rgb = known_categories[i].rgb;
				found = TRUE;
				break;
			}
		}
		if (!found)
			rgb = known_categories[g_str_hash (name) % G_N_ELEMENTS (known_categories)].rgb;
	}
	color->r = (guint8) ((rgb >> 16) & 0xff);
	color->g = (guint8) ((rgb >> 8) & 0xff);
	color->b = (guint8) (rgb & 0xff);
	color->a = 255;

	/* Tone defaults down a little on dark themes so they do not glare */
	theme = lrg_widget_get_theme (LRG_WIDGET (self));
	if (theme != NULL && lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW)) < 0.3f)
		lrg_paint_color_shade (color, -0.1f, color);
}

void
lrg_block_editor_set_category_color (LrgBlockEditor *self,
                                     const gchar    *name,
                                     const GrlColor *color)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	lrg_block_editor_add_category (self, name, color);
}

/* block_color: the kind's own colour, else its category's */
static void
block_color (LrgBlockEditor *self,
             LrgBlock       *b,
             GrlColor       *out)
{
	if (b->kind->has_color)
		*out = b->kind->color;
	else
		lrg_block_editor_get_category_color (self, b->kind->category, out);
}

gboolean
lrg_block_editor_scroll_palette_to (LrgBlockEditor *self,
                                    const gchar    *category)
{
	Category *c;
	gfloat    max_scroll;
	Rect      p;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);

	ensure_layout (self);
	c = find_category (self, category);
	if (c == NULL || c->pal_y < 0.0f)
		return FALSE;
	areas (self, NULL, &p, NULL);
	max_scroll = MAX (0.0f, self->pal_content_h - p.h);
	lrg_spring_set_target (&self->pal_scroll, CLAMP (c->pal_y - PAL_PAD * 0.5f, 0.0f, max_scroll));
	if (!smooth (self))
		lrg_spring_snap (&self->pal_scroll, self->pal_scroll.target);
	else
		self->anim = TRUE;
	return TRUE;
}

/* ==========================================================================
 * Program
 * ========================================================================== */

LrgBlock *
lrg_block_editor_create_block (LrgBlockEditor *self,
                               const gchar    *kind_id,
                               gfloat          x,
                               gfloat          y)
{
	LrgBlockKind *kind;
	LrgBlock     *b;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	g_return_val_if_fail (kind_id != NULL, NULL);

	kind = g_hash_table_lookup (self->kind_by_id, kind_id);
	if (kind == NULL)
		return NULL;
	b = lrg_block_new (kind);
	lrg_block_editor_add_script (self, b, x, y);
	g_object_unref (b);   /* the script list holds it */
	return b;
}

/* tree_kinds_ok: every block of a free tree is of a kind in the palette */
static gboolean
tree_kinds_ok (LrgBlockEditor *self,
               LrgBlock       *top)
{
	g_autoptr(GPtrArray) all = g_ptr_array_new ();
	guint                i;

	collect_tree (top, TRUE, all);
	for (i = 0; i < all->len; i++)
	{
		LrgBlock *b = g_ptr_array_index (all, i);

		if (g_hash_table_lookup (self->kind_by_id, b->kind->id) != b->kind || b->editor != NULL)
			return FALSE;
	}
	return TRUE;
}

void
lrg_block_editor_add_script (LrgBlockEditor *self,
                             LrgBlock       *block,
                             gfloat          x,
                             gfloat          y)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	g_return_if_fail (LRG_IS_BLOCK (block));
	g_return_if_fail (block->parent == NULL);
	g_return_if_fail (tree_kinds_ok (self, block));

	edit_begin (self);
	link_script (self, g_object_ref (block), x, y);
	adopt_tree (self, block);
	edit_end (self);
}

gboolean
lrg_block_editor_can_attach (LrgBlockEditor *self,
                             LrgBlock       *block,
                             LrgBlock       *target,
                             LrgBlockPlace   place,
                             guint           index)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_BLOCK (block), FALSE);
	g_return_val_if_fail (LRG_IS_BLOCK (target), FALSE);

	if (!in_program (self, block) || !in_program (self, target))
		return FALSE;
	return attach_ok (block, target, place, index);
}

gboolean
lrg_block_editor_attach (LrgBlockEditor *self,
                         LrgBlock       *block,
                         LrgBlock       *target,
                         LrgBlockPlace   place,
                         guint           index)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);

	if (!lrg_block_editor_can_attach (self, block, target, place, index))
		return FALSE;
	ensure_layout (self);
	edit_begin (self);
	place_owned (self, unlink_block (self, block), target, place, index);
	edit_end (self);
	return TRUE;
}

void
lrg_block_editor_detach (LrgBlockEditor *self,
                         LrgBlock       *block,
                         gfloat          x,
                         gfloat          y)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	g_return_if_fail (LRG_IS_BLOCK (block));
	g_return_if_fail (in_program (self, block));

	edit_begin (self);
	if (block->parent == NULL)
	{
		block->x = x;
		block->y = y;
	}
	else
		link_script (self, unlink_block (self, block), x, y);
	edit_end (self);
}

/*
 * remove_block_internal:
 *
 * Takes @b out and orphans it with its inputs and mouths (and the
 * blocks below with @with_following). Otherwise the blocks below move
 * up into its place, or become a script where it stood.
 */
static void
remove_block_internal (LrgBlockEditor *self,
                       LrgBlock       *b,
                       gboolean        with_following)
{
	LrgBlock      *parent = b->parent;
	LrgBlockPlace  place = b->place;
	guint          index = b->index;
	gfloat         x = b->parent == NULL ? b->x : b->lx;
	gfloat         y = b->parent == NULL ? b->y : b->ly;
	LrgBlock      *rest = NULL;
	LrgBlock      *ref;

	if (!with_following && b->next != NULL)
		rest = unlink_block (self, b->next);
	ref = unlink_block (self, b);
	orphan_tree (self, ref);
	g_object_unref (ref);

	if (rest != NULL)
	{
		if (parent != NULL && attach_ok (rest, parent, place, index))
			link_at (parent, place, index, rest);
		else
			link_script (self, rest, x, y);
	}
}

void
lrg_block_editor_remove_block (LrgBlockEditor *self,
                               LrgBlock       *block,
                               gboolean        with_following)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	g_return_if_fail (LRG_IS_BLOCK (block));
	g_return_if_fail (in_program (self, block));

	ensure_layout (self);
	edit_begin (self);
	remove_block_internal (self, block, with_following);
	edit_end (self);
}

void
lrg_block_editor_clear_program (LrgBlockEditor *self)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	if (self->scripts->len == 0)
		return;
	edit_begin (self);
	drop_program (self);
	edit_end (self);
}

guint
lrg_block_editor_get_n_scripts (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 0);
	return self->scripts->len;
}

LrgBlock *
lrg_block_editor_get_nth_script (LrgBlockEditor *self,
                                 guint           index)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	return index < self->scripts->len ? g_ptr_array_index (self->scripts, index) : NULL;
}

GList *
lrg_block_editor_get_scripts (LrgBlockEditor *self)
{
	GList *list = NULL;
	guint  i;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	for (i = self->scripts->len; i > 0; i--)
		list = g_list_prepend (list, g_ptr_array_index (self->scripts, i - 1));
	return list;
}

guint
lrg_block_editor_get_n_blocks (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 0);
	return g_hash_table_size (self->blocks);
}

void
lrg_block_editor_clear_running (LrgBlockEditor *self)
{
	g_autoptr(GPtrArray) all = g_ptr_array_new ();
	GHashTableIter       iter;
	gpointer             key;
	guint                i;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	g_hash_table_iter_init (&iter, self->blocks);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (all, key);
	for (i = 0; i < all->len; i++)
		lrg_block_set_running (g_ptr_array_index (all, i), FALSE);
}

static gint
compare_script_y (gconstpointer a,
                  gconstpointer b)
{
	const LrgBlock *x = *(LrgBlock * const *) a;
	const LrgBlock *y = *(LrgBlock * const *) b;

	if (x->y != y->y)
		return x->y < y->y ? -1 : 1;
	if (x->x != y->x)
		return x->x < y->x ? -1 : 1;
	return 0;
}

void
lrg_block_editor_clean_up (LrgBlockEditor *self)
{
	g_autoptr(GPtrArray) order = NULL;
	gfloat               y = CLEAN_MARGIN;
	guint                i;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	if (self->scripts->len == 0)
		return;
	ensure_layout (self);
	edit_begin (self);
	order = g_ptr_array_sized_new (self->scripts->len);
	for (i = 0; i < self->scripts->len; i++)
		g_ptr_array_add (order, g_ptr_array_index (self->scripts, i));
	g_ptr_array_sort (order, compare_script_y);
	for (i = 0; i < order->len; i++)
	{
		LrgBlock *top = g_ptr_array_index (order, i);

		top->x = CLEAN_MARGIN;
		top->y = y;
		y += measure_chain (self, top) + CLEAN_GAP;
	}
	edit_end (self);
}

/* ==========================================================================
 * Selection and clipboard
 * ========================================================================== */

LrgBlock *
lrg_block_editor_get_selected (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	return self->selected;
}

void
lrg_block_editor_select_block (LrgBlockEditor *self,
                               LrgBlock       *block)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	g_return_if_fail (block == NULL || in_program (self, block));

	if (self->selected == block)
		return;
	if (self->selected != NULL)
		self->selected->selected = FALSE;
	self->selected = block;
	if (block != NULL)
		block->selected = TRUE;
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

gboolean
lrg_block_editor_delete_selection (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);

	if (self->selected == NULL)
		return FALSE;
	lrg_block_editor_remove_block (self, self->selected, FALSE);
	return TRUE;
}

/* script_point: where a block's copy goes: beside where it is shown */
static void
shown_position (LrgBlock *b,
                gfloat   *x,
                gfloat   *y)
{
	if (b->placed)
	{
		*x = b->lx;
		*y = b->ly;
	}
	else
	{
		*x = b->x;
		*y = b->y;
	}
}

LrgBlock *
lrg_block_editor_duplicate_selection (LrgBlockEditor *self)
{
	LrgBlock *copy;
	gfloat    x;
	gfloat    y;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);

	if (self->selected == NULL)
		return NULL;
	ensure_layout (self);
	shown_position (self->selected, &x, &y);
	copy = copy_tree (self->selected, TRUE);
	edit_begin (self);
	link_script (self, copy, x + PASTE_OFFSET, y + PASTE_OFFSET);
	adopt_tree (self, copy);
	edit_end (self);
	lrg_block_editor_select_block (self, copy);
	return copy;
}

gboolean
lrg_block_editor_copy_selection (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);

	if (self->selected == NULL)
		return FALSE;
	g_clear_object (&self->clip);
	self->clip = copy_tree (self->selected, TRUE);
	return TRUE;
}

LrgBlock *
lrg_block_editor_paste (LrgBlockEditor *self)
{
	LrgBlock *copy;
	gfloat    x;
	gfloat    y;
	Rect      ws;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);

	if (self->clip == NULL || !tree_kinds_ok (self, self->clip))
		return NULL;

	/* At the pointer when it is over the workspace, else near the
	 * top-left corner of the view */
	areas (self, NULL, NULL, &ws);
	if (self->pointer_in && rect_has (&ws, self->pointer_x, self->pointer_y))
		lrg_block_editor_screen_to_workspace (self, self->pointer_x, self->pointer_y, &x, &y);
	else
	{
		x = self->scroll_x + CLEAN_MARGIN * 2.0f / self->zoom;
		y = self->scroll_y + CLEAN_MARGIN * 2.0f / self->zoom;
	}
	copy = copy_tree (self->clip, TRUE);
	edit_begin (self);
	link_script (self, copy, x, y);
	adopt_tree (self, copy);
	edit_end (self);
	lrg_block_editor_select_block (self, copy);
	return copy;
}

/* ==========================================================================
 * Undo API
 * ========================================================================== */

gboolean
lrg_block_editor_undo (LrgBlockEditor *self)
{
	Snapshot *snap;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);

	finish_interaction (self);
	if (self->undo->len == 0)
		return FALSE;
	snap = g_ptr_array_steal_index (self->undo, self->undo->len - 1);
	g_ptr_array_add (self->redo, snapshot_take (self));
	snapshot_restore (self, snap);
	snapshot_free (snap);
	commit (self);
	return TRUE;
}

gboolean
lrg_block_editor_redo (LrgBlockEditor *self)
{
	Snapshot *snap;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);

	finish_interaction (self);
	if (self->redo->len == 0)
		return FALSE;
	snap = g_ptr_array_steal_index (self->redo, self->redo->len - 1);
	g_ptr_array_add (self->undo, snapshot_take (self));
	snapshot_restore (self, snap);
	snapshot_free (snap);
	commit (self);
	return TRUE;
}

gboolean
lrg_block_editor_can_undo (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	return self->undo->len > 0;
}

gboolean
lrg_block_editor_can_redo (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	return self->redo->len > 0;
}

void
lrg_block_editor_clear_history (LrgBlockEditor *self)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	g_ptr_array_set_size (self->undo, 0);
	g_ptr_array_set_size (self->redo, 0);
	update_history_state (self);
}

guint
lrg_block_editor_get_undo_limit (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 0);
	return self->undo_limit;
}

void
lrg_block_editor_set_undo_limit (LrgBlockEditor *self,
                                 guint           limit)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	limit = MAX (1u, limit);
	if (self->undo_limit == limit)
		return;
	self->undo_limit = limit;
	while (self->undo->len > limit)
		g_ptr_array_remove_index (self->undo, 0);
	update_history_state (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNDO_LIMIT]);
}

/* ==========================================================================
 * Slot editing and dropdowns
 * ========================================================================== */

/* stop_slot_edit: leaves typing mode, dropping the edit */
static void
stop_slot_edit (LrgBlockEditor *self)
{
	if (self->edit_block == NULL)
		return;
	self->edit_block = NULL;
	g_string_truncate (self->edit_text, 0);
	g_clear_pointer (&self->edit_snapshot, snapshot_free);
	self->layout_dirty = TRUE;
}

/* open_dropdown: the slot's choices in an LrgMenu below it */
static gboolean
open_dropdown (LrgBlockEditor *self,
               LrgBlock       *block,
               guint           slot)
{
	g_autoptr(GMenu) model = g_menu_new ();
	const gchar * const *choices = (const gchar * const *) kind_slot (block->kind, slot)->choices;
	LrgWidget           *root = lrg_widget_get_root (LRG_WIDGET (self));
	gfloat               x;
	gfloat               y;
	gfloat               w;
	gfloat               h;
	guint                i;

	if (choices == NULL || !LRG_IS_CANVAS (root))
		return FALSE;
	for (i = 0; choices[i] != NULL; i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new (choices[i], NULL);

		g_menu_item_set_action_and_target_value (item, "blocks.choose", g_variant_new_string (choices[i]));
		g_menu_append_item (model, item);
	}
	if (self->menu == NULL)
		self->menu = lrg_menu_new ();
	if (lrg_menu_get_open (self->menu))
		lrg_menu_close (self->menu);
	lrg_menu_set_model (self->menu, G_MENU_MODEL (model));
	self->menu_block = block;
	self->menu_slot = slot;

	lrg_block_editor_get_slot_bounds (self, block, slot, &x, &y, &w, &h);
	lrg_menu_popup_at_rect (self->menu, LRG_WIDGET (self), x - lrg_widget_get_world_x (root),
	                        y - lrg_widget_get_world_y (root), w, h, LRG_POPUP_PLACEMENT_BELOW);
	return lrg_menu_get_open (self->menu);
}

gboolean
lrg_block_editor_begin_slot_edit (LrgBlockEditor *self,
                                  LrgBlock       *block,
                                  guint           slot)
{
	LrgBlockSlotType type;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_BLOCK (block), FALSE);
	g_return_val_if_fail (slot < block->kind->slots->len, FALSE);

	if (!in_program (self, block) || block->inputs[slot] != NULL)
		return FALSE;
	type = kind_slot (block->kind, slot)->type;
	if (type == LRG_BLOCK_SLOT_BOOLEAN)
		return FALSE;
	if (self->edit_block != NULL)
		lrg_block_editor_commit_slot_edit (self);
	ensure_layout (self);
	if (type == LRG_BLOCK_SLOT_DROPDOWN)
		return open_dropdown (self, block, slot);

	/* --- typing: the text starts selected, so typing replaces it --- */
	self->edit_block = block;
	self->edit_slot = slot;
	g_string_assign (self->edit_text, block->values[slot]);
	self->edit_cursor = self->edit_text->len;
	self->edit_all = TRUE;
	g_clear_pointer (&self->edit_snapshot, snapshot_free);
	self->edit_snapshot = snapshot_take (self);
	self->layout_dirty = TRUE;
	return TRUE;
}

void
lrg_block_editor_commit_slot_edit (LrgBlockEditor *self)
{
	g_autofree gchar *text = NULL;
	LrgBlock         *b;
	Snapshot         *snap;
	guint             slot;
	gdouble           v;

	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	if (self->edit_block == NULL)
		return;
	b = self->edit_block;
	slot = self->edit_slot;
	text = g_strdup (self->edit_text->str);
	snap = g_steal_pointer (&self->edit_snapshot);
	stop_slot_edit (self);

	/* A number slot keeps only numbers (empty means 0) */
	if (kind_slot (b->kind, slot)->type == LRG_BLOCK_SLOT_NUMBER)
	{
		g_strstrip (text);
		if (*text == '\0')
		{
			g_free (text);
			text = g_strdup ("0");
		}
		else if (!parse_number (text, &v))
		{
			g_free (text);
			text = g_strdup (b->values[slot]);
		}
	}
	if (g_strcmp0 (text, b->values[slot]) == 0)
	{
		snapshot_free (snap);
		return;
	}
	g_free (b->values[slot]);
	b->values[slot] = g_steal_pointer (&text);
	push_undo (self, snap);
	commit (self);
}

void
lrg_block_editor_cancel_slot_edit (LrgBlockEditor *self)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));
	stop_slot_edit (self);
}

gboolean
lrg_block_editor_get_edit_slot (LrgBlockEditor  *self,
                                LrgBlock       **block,
                                guint           *slot)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);

	if (block != NULL)
		*block = self->edit_block;
	if (slot != NULL)
		*slot = self->edit_block != NULL ? self->edit_slot : 0;
	return self->edit_block != NULL;
}

const gchar *
lrg_block_editor_get_edit_text (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	return self->edit_block != NULL ? self->edit_text->str : NULL;
}

/* edit_insert: types one character at the cursor (replacing a selection) */
static void
edit_insert (LrgBlockEditor *self,
             gunichar        c)
{
	gchar buf[8];
	gint  n;

	if (kind_slot (self->edit_block->kind, self->edit_slot)->type == LRG_BLOCK_SLOT_NUMBER &&
	    !(g_unichar_isdigit (c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E'))
		return;
	if (c < 0x20 || c == 0x7f)
		return;
	if (self->edit_all)
	{
		g_string_truncate (self->edit_text, 0);
		self->edit_cursor = 0;
		self->edit_all = FALSE;
	}
	n = g_unichar_to_utf8 (c, buf);
	g_string_insert_len (self->edit_text, (gssize) self->edit_cursor, buf, n);
	self->edit_cursor += (gsize) n;
	self->layout_dirty = TRUE;
}

/* edit_key: cursor movement and deletion while typing */
static gboolean
edit_key (LrgBlockEditor    *self,
          GrlKey             key,
          LrgInputModifiers  mods)
{
	const gchar *s = self->edit_text->str;
	gsize        len = self->edit_text->len;
	gsize        cur = self->edit_cursor;

	switch (key)
	{
	case GRL_KEY_ENTER:
	case GRL_KEY_KP_ENTER:
		lrg_block_editor_commit_slot_edit (self);
		return TRUE;
	case GRL_KEY_ESCAPE:
		lrg_block_editor_cancel_slot_edit (self);
		return TRUE;
	case GRL_KEY_BACKSPACE:
		if (self->edit_all)
		{
			g_string_truncate (self->edit_text, 0);
			self->edit_cursor = 0;
		}
		else if (cur > 0)
		{
			const gchar *prev = g_utf8_find_prev_char (s, s + cur);
			gsize        at = (gsize) (prev - s);

			g_string_erase (self->edit_text, (gssize) at, (gssize) (cur - at));
			self->edit_cursor = at;
		}
		break;
	case GRL_KEY_DELETE:
		if (self->edit_all)
		{
			g_string_truncate (self->edit_text, 0);
			self->edit_cursor = 0;
		}
		else if (cur < len)
		{
			const gchar *next = g_utf8_next_char (s + cur);

			g_string_erase (self->edit_text, (gssize) cur, (gssize) (next - (s + cur)));
		}
		break;
	case GRL_KEY_LEFT:
		if (self->edit_all)
			self->edit_cursor = 0;
		else if (cur > 0)
			self->edit_cursor = (gsize) (g_utf8_find_prev_char (s, s + cur) - s);
		break;
	case GRL_KEY_RIGHT:
		if (self->edit_all)
			self->edit_cursor = len;
		else if (cur < len)
			self->edit_cursor = (gsize) (g_utf8_next_char (s + cur) - s);
		break;
	case GRL_KEY_HOME:
		self->edit_cursor = 0;
		break;
	case GRL_KEY_END:
		self->edit_cursor = len;
		break;
	case GRL_KEY_A:
		if (!(mods & LRG_INPUT_MODIFIER_CTRL))
			return TRUE;   /* typed as text */
		self->edit_all = TRUE;
		self->edit_cursor = len;
		self->layout_dirty = TRUE;
		return TRUE;
	default:
		/* Printable keys arrive as TEXT_INPUT: swallow them here so
		 * Space and shortcuts do not fire while typing */
		return !(mods & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_ALT));
	}
	self->edit_all = FALSE;
	self->layout_dirty = TRUE;
	return TRUE;
}

/* next_editable_slot: the first typable / dropdown slot after @from in @b */
static gint
next_editable_slot (LrgBlock *b,
                    gint      from)
{
	guint i;

	for (i = (guint) (from + 1); i < b->kind->slots->len; i++)
		if (b->inputs[i] == NULL && kind_slot (b->kind, i)->type != LRG_BLOCK_SLOT_BOOLEAN)
			return (gint) i;
	return -1;
}

/* ==========================================================================
 * Actions ("blocks." group on the editor)
 * ========================================================================== */

static void
action_activated (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (user_data);
	const gchar    *name = g_action_get_name (G_ACTION (action));

	if (g_strcmp0 (name, "undo") == 0)
		lrg_block_editor_undo (self);
	else if (g_strcmp0 (name, "redo") == 0)
		lrg_block_editor_redo (self);
	else if (g_strcmp0 (name, "delete") == 0)
		lrg_block_editor_delete_selection (self);
	else if (g_strcmp0 (name, "duplicate") == 0)
		lrg_block_editor_duplicate_selection (self);
	else if (g_strcmp0 (name, "copy") == 0)
		lrg_block_editor_copy_selection (self);
	else if (g_strcmp0 (name, "cut") == 0)
	{
		if (lrg_block_editor_copy_selection (self))
			lrg_block_editor_remove_block (self, self->selected, TRUE);
	}
	else if (g_strcmp0 (name, "paste") == 0)
		lrg_block_editor_paste (self);
	else if (g_strcmp0 (name, "clean-up") == 0)
		lrg_block_editor_clean_up (self);
	else if (g_strcmp0 (name, "choose") == 0 && parameter != NULL)
	{
		/* A dropdown menu entry: the value for the slot it was opened on */
		if (self->menu_block != NULL && in_program (self, self->menu_block))
			lrg_block_set_value (self->menu_block, self->menu_slot, g_variant_get_string (parameter, NULL));
		self->menu_block = NULL;
	}
}

static void
install_actions (LrgBlockEditor *self)
{
	static const gchar * const plain[] = {
		"undo", "redo", "delete", "duplicate", "copy", "cut", "paste", "clean-up"
	};
	g_autoptr(GSimpleAction) choose = NULL;
	guint                    i;

	self->actions = g_simple_action_group_new ();
	for (i = 0; i < G_N_ELEMENTS (plain); i++)
	{
		g_autoptr(GSimpleAction) a = g_simple_action_new (plain[i], NULL);

		g_signal_connect_object (a, "activate", G_CALLBACK (action_activated), self, 0);
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (a));
	}
	choose = g_simple_action_new ("choose", G_VARIANT_TYPE_STRING);
	g_signal_connect_object (choose, "activate", G_CALLBACK (action_activated), self, 0);
	g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (choose));
	lrg_widget_insert_action_group (LRG_WIDGET (self), "blocks", G_ACTION_GROUP (self->actions));
	update_history_state (self);
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_block_editor_style_defaults:
 *
 * Parts: BOX (workspace background), HEADER (palette background),
 * TAB (category strip; SELECTED: the current category's highlight),
 * TRACK (grid dots: foreground), ITEM (blocks: radius, border width,
 * shadow), LABEL (number and text fields: background, foreground,
 * border), MARK (selection outline: border), FILL (running glow:
 * glow colour and size), INDICATOR (insertion ghost).
 */
static void
lrg_block_editor_style_defaults (LrgWidget      *widget,
                                 LrgUIPart       part,
                                 LrgWidgetState  state,
                                 LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    bw = MAX (1.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
	GrlColor  c;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW),
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 0.35f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		break;

	case LRG_UI_PART_HEADER:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		break;

	case LRG_UI_PART_TAB:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		if (state & LRG_WIDGET_STATE_SELECTED)
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.16f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		}
		lrg_part_look_set_radius (look, 8.0f);
		break;

	case LRG_UI_PART_TRACK:
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.7f, &c);
		lrg_part_look_set_foreground (look, &c);
		break;

	case LRG_UI_PART_ITEM:
		lrg_part_look_set_radius (look, RADIUS);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 1.0f);
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 0.6f),
		                          MAX (1.5f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y)));
		break;

	case LRG_UI_PART_LABEL:
		lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT),
		                     &(GrlColor){ 255, 255, 255, 255 }, 0.85f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_foreground (look, &(GrlColor){ 40, 40, 48, 255 });
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW), 0.35f, &c);
		lrg_part_look_set_border (look, &c, 1.0f);
		break;

	case LRG_UI_PART_MARK:
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 2.0f);
		break;

	case LRG_UI_PART_FILL:
		lrg_part_look_set_glow (look, &(GrlColor){ 255, 214, 64, 230 }, 14.0f);
		break;

	case LRG_UI_PART_INDICATOR:
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.35f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, RADIUS);
		break;

	default:
		LRG_WIDGET_CLASS (lrg_block_editor_parent_class)->style_defaults (widget, part, state, look);
		return;
	}

	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

/* Looks resolved once per paint */
typedef struct
{
	LrgPartLook  box;
	LrgPartLook  palette;
	LrgPartLook  strip;
	LrgPartLook  strip_sel;
	LrgPartLook  grid;
	LrgPartLook  item;
	LrgPartLook  field;
	LrgPartLook  mark;
	LrgPartLook  glow;
	LrgPartLook  ghost;
	GrlFont     *font;
	gfloat       fs;
	gfloat       fss;
	GrlColor     accent;
	GrlColor     selection;
	GrlColor     danger;
	GrlColor     text;
	Rect         clip;          /* visible area for culling */
} Looks;

/* Maps layout units to the screen: screen = o + v * s */
typedef struct
{
	gfloat ox;
	gfloat oy;
	gfloat s;
} Map;

static void
resolve_looks (LrgBlockEditor *self,
               Looks          *k)
{
	LrgWidget     *w = LRG_WIDGET (self);
	LrgTheme      *theme = lrg_widget_get_theme (w);
	LrgWidgetState st = lrg_widget_get_state_flags (w) & LRG_WIDGET_STATE_DISABLED;

	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_BOX, st, &k->box);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_HEADER, st, &k->palette);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_TAB, st, &k->strip);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_TAB, st | LRG_WIDGET_STATE_SELECTED, &k->strip_sel);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_TRACK, st, &k->grid);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ITEM, st, &k->item);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_LABEL, st, &k->field);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_MARK, st, &k->mark);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_FILL, st, &k->glow);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_INDICATOR, st, &k->ghost);
	k->font = lrg_theme_get_ui_font (theme);
	k->fs = self->m_fs;
	k->fss = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	k->accent = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	k->selection = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
	k->danger = *lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
	k->text = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
}

/* text_on: readable text colour on a block colour */
static GrlColor
text_on (const GrlColor *base)
{
	static const GrlColor white = { 255, 255, 255, 255 };
	static const GrlColor dark = { 28, 28, 34, 255 };

	return lrg_paint_color_contrast (&white, base) >= 2.2f ? white : dark;
}

/* trapezoid: top edge x0..x1 at y0, bottom edge x2..x3 at y1 */
static void
trapezoid (LrgPaintList   *list,
           gfloat          x0,
           gfloat          x1,
           gfloat          y0,
           gfloat          x2,
           gfloat          x3,
           gfloat          y1,
           const GrlColor *color)
{
	lrg_paint_list_triangle (list, x0, y0, x1, y0, x3, y1, color);
	lrg_paint_list_triangle (list, x0, y0, x3, y1, x2, y1, color);
}

/* tab: the connector below a block (or a row) whose left edge is at @x */
static void
tab (LrgPaintList   *list,
     gfloat          x,
     gfloat          y,
     gfloat          s,
     gfloat          grow,
     const GrlColor *color)
{
	gfloat nx = x + NOTCH_X * s;
	gfloat nw = NOTCH_W * s;
	gfloat nd = NOTCH_D * s;

	trapezoid (list, nx - grow, nx + nw + grow, y - 1.0f, nx + nd - grow, nx + nw - nd + grow, y + nd + grow,
	           color);
}

/* hexagon: a boolean outline, grown by @grow */
static void
hexagon (LrgPaintList   *list,
         gfloat          x,
         gfloat          y,
         gfloat          w,
         gfloat          h,
         gfloat          grow,
         const GrlColor *color)
{
	gfloat tip = MIN (h * 0.5f, w * 0.3f);

	x -= grow;
	y -= grow;
	w += grow * 2.0f;
	h += grow * 2.0f;
	lrg_paint_list_rect (list, x + tip - 0.5f, y, w - tip * 2.0f + 1.0f, h, 0.0f, color);
	lrg_paint_list_triangle (list, x, y + h * 0.5f, x + tip, y, x + tip, y + h, color);
	lrg_paint_list_triangle (list, x + w, y + h * 0.5f, x + w - tip, y + h, x + w - tip, y, color);
}

/* One rounded piece of a block silhouette */
typedef struct
{
	gfloat         x;
	gfloat         y;
	gfloat         w;
	gfloat         h;
	LrgCornerRadii r;
	gboolean       gradient;
} Piece;

/*
 * paint_shape:
 *
 * A block silhouette from paint primitives: rounded rectangles for
 * rows, arms and the bottom of C/E blocks, trapezoid tabs below (and
 * into mouths), and the notch on top cut in @notch_bg. Drawn twice:
 * grown by the border width in the darker border colour, then filled
 * with a subtle top-to-bottom gradient, so the outline wraps the union
 * of the pieces without inner seams.
 */
static void
paint_shape (LrgPaintList   *list,
             const Looks    *k,
             LrgBlock       *b,
             gfloat          X,
             gfloat          Y,
             gfloat          s,
             const GrlColor *base,
             const GrlColor *notch_bg)
{
	LrgBlockShape     shape = b->kind->shape;
	gfloat            W = b->w * s;
	gfloat            H = b->h * s;
	gfloat            r = MAX (1.0f, k->item.radii.top_left) * s;
	gfloat            bw = MAX (1.0f, k->item.border_width * MIN (1.0f, s));
	Piece             pieces[2 * MAX_MOUTHS + 2];
	guint             n = 0;
	guint             i;
	GrlColor          edge;
	LrgPaintGradient  grad;

	lrg_paint_color_shade (base, -0.3f, &edge);
	memset (&grad, 0, sizeof grad);
	grad.kind = LRG_PAINT_GRADIENT_LINEAR;
	grad.angle = 90.0f;
	lrg_paint_color_shade (base, 0.08f, &grad.start);
	lrg_paint_color_shade (base, -0.05f, &grad.end);

	/* --- values: one primitive each --- */
	if (shape == LRG_BLOCK_SHAPE_REPORTER)
	{
		LrgCornerRadii pill;

		lrg_corner_radii_init_uniform (&pill, H * 0.5f);
		lrg_paint_list_rect_ex (list, X, Y, W, H, &pill, NULL, &grad, bw, &edge);
		return;
	}
	if (shape == LRG_BLOCK_SHAPE_BOOLEAN)
	{
		hexagon (list, X, Y, W, H, bw, &edge);
		hexagon (list, X, Y, W, H, 0.0f, base);
		return;
	}

	/* --- the pieces of statement-like blocks --- */
	if (shape_mouths (shape) > 0)
	{
		guint m;

		for (m = 0; m < shape_mouths (shape); m++)
		{
			Piece *row = &pieces[n++];
			Piece *arm = &pieces[n++];

			row->x = X;
			row->y = Y + b->row_y[m] * s;
			row->w = W;
			row->h = b->row_h[m] * s;
			row->r.top_left = m == 0 ? r : 0.0f;
			row->r.top_right = r;
			row->r.bottom_right = r;
			row->r.bottom_left = 0.0f;
			row->gradient = TRUE;
			arm->x = X;
			arm->y = Y + b->mouth_y[m] * s - 1.0f;
			arm->w = ARM_W * s;
			arm->h = b->mouth_h[m] * s + 2.0f;
			lrg_corner_radii_init_uniform (&arm->r, 0.0f);
			arm->gradient = FALSE;
		}
		pieces[n].x = X;
		pieces[n].y = Y + H - BOTTOM_H * s;
		pieces[n].w = W;
		pieces[n].h = BOTTOM_H * s;
		pieces[n].r.top_left = 0.0f;
		pieces[n].r.top_right = r;
		pieces[n].r.bottom_right = r;
		pieces[n].r.bottom_left = r;
		pieces[n].gradient = FALSE;
		n++;
	}
	else if (shape == LRG_BLOCK_SHAPE_HAT)
	{
		gfloat cap = HAT_CAP * s;

		pieces[n].x = X;
		pieces[n].y = Y;
		pieces[n].w = MIN (W, HAT_CAP_W * s);
		pieces[n].h = cap * 2.5f;
		lrg_corner_radii_init_uniform (&pieces[n].r, cap * 1.25f);
		pieces[n].gradient = TRUE;
		n++;
		pieces[n].x = X;
		pieces[n].y = Y + cap;
		pieces[n].w = W;
		pieces[n].h = H - cap;
		pieces[n].r.top_left = 0.0f;
		pieces[n].r.top_right = r;
		pieces[n].r.bottom_right = r;
		pieces[n].r.bottom_left = r;
		pieces[n].gradient = FALSE;
		n++;
	}
	else
	{
		pieces[n].x = X;
		pieces[n].y = Y;
		pieces[n].w = W;
		pieces[n].h = H;
		lrg_corner_radii_init_uniform (&pieces[n].r, r);
		pieces[n].gradient = TRUE;
		n++;
	}

	/* --- pass 1: the outline --- */
	for (i = 0; i < n; i++)
	{
		LrgCornerRadii g = pieces[i].r;

		g.top_left += g.top_left > 0.0f ? bw : 0.0f;
		g.top_right += g.top_right > 0.0f ? bw : 0.0f;
		g.bottom_right += g.bottom_right > 0.0f ? bw : 0.0f;
		g.bottom_left += g.bottom_left > 0.0f ? bw : 0.0f;
		lrg_paint_list_rect_ex (list, pieces[i].x - bw, pieces[i].y - bw, pieces[i].w + bw * 2.0f,
		                        pieces[i].h + bw * 2.0f, &g, &edge, NULL, 0.0f, NULL);
	}
	if (shape_has_bottom (shape))
		tab (list, X, Y + H, s, bw, &edge);
	for (i = 0; i < shape_mouths (shape); i++)
		tab (list, X + ARM_W * s, Y + (b->row_y[i] + b->row_h[i]) * s, s, bw, &edge);

	/* --- pass 2: the fill --- */
	for (i = 0; i < n; i++)
		lrg_paint_list_rect_ex (list, pieces[i].x, pieces[i].y, pieces[i].w, pieces[i].h, &pieces[i].r,
		                        base, pieces[i].gradient ? &grad : NULL, 0.0f, NULL);
	if (shape_has_bottom (shape))
		tab (list, X, Y + H, s, 0.0f, &grad.end);
	for (i = 0; i < shape_mouths (shape); i++)
		tab (list, X + ARM_W * s, Y + (b->row_y[i] + b->row_h[i]) * s, s, 0.0f, base);

	/* --- pass 3: the notch on top, cut in what is behind --- */
	if (notch_bg != NULL && shape_has_top (shape))
	{
		gfloat nx = X + NOTCH_X * s;
		gfloat nw = NOTCH_W * s;
		gfloat nd = NOTCH_D * s;

		trapezoid (list, nx - bw, nx + nw + bw, Y - bw - 0.5f, nx + nd - bw, nx + nw - nd + bw, Y + nd + bw,
		           &edge);
		trapezoid (list, nx, nx + nw, Y - bw - 1.0f, nx + nd, nx + nw - nd, Y + nd, notch_bg);
	}
}

/*
 * paint_content:
 *
 * The words and the empty slots of every row: number fields are pills,
 * text fields rounded boxes, boolean holes darker hexagons, dropdowns
 * darker boxes with a chevron. The slot being typed into shows its
 * text (selected or with a caret); the slot a dragged value would drop
 * into is outlined.
 */
static void
paint_content (LrgBlockEditor *self,
               LrgPaintList   *list,
               const Looks    *k,
               LrgBlock       *b,
               gfloat          X,
               gfloat          Y,
               gfloat          s,
               const GrlColor *base)
{
	LrgBlockKind *kind = b->kind;
	GrlColor      ink = text_on (base);
	gfloat        fs = k->fs * s;
	gboolean      text = fs >= TEXT_MIN_PX;
	guint         i;

	for (i = 0; i < kind->tokens->len; i++)
	{
		Token *t = &g_array_index (kind->tokens, Token, i);
		gfloat top = Y + (b->row_y[t->row] + b->row_off[t->row]) * s;
		gfloat rh = (b->row_h[t->row] - b->row_off[t->row]) * s;

		if (t->slot < 0)
		{
			if (text)
				lrg_paint_list_text (list, k->font, t->text, roundf (X + b->tok_x[i] * s),
				                     roundf (top + (rh - fs) * 0.5f), fs, 0.0f, &ink);
			continue;
		}
		if (b->inputs[t->slot] != NULL)
			continue;

		{
			Rect            *r = &b->slot[t->slot];
			LrgBlockSlotType type = kind_slot (kind, (guint) t->slot)->type;
			gfloat           sx = X + r->x * s;
			gfloat           sy = Y + r->y * s;
			gfloat           sw = r->w * s;
			gfloat           sh = r->h * s;
			gboolean         editing = b == self->edit_block && (guint) t->slot == self->edit_slot;
			const gchar     *value = shown_value (self, b, (guint) t->slot);
			GrlColor         dark;
			gfloat           tw = 0.0f;

			switch (type)
			{
			case LRG_BLOCK_SLOT_BOOLEAN:
				lrg_paint_color_shade (base, -0.28f, &dark);
				hexagon (list, sx, sy, sw, sh, 0.0f, &dark);
				break;

			case LRG_BLOCK_SLOT_DROPDOWN:
				{
					gfloat icon = self->slot_h * 0.55f * s;
					GrlColor rim;

					lrg_paint_color_shade (base, -0.18f, &dark);
					lrg_paint_color_shade (base, -0.32f, &rim);
					lrg_paint_list_rect_ex (list, sx, sy, sw, sh,
					                        &(LrgCornerRadii){ 4.0f * s, 4.0f * s, 4.0f * s, 4.0f * s },
					                        &dark, NULL, 1.0f, &rim);
					if (text)
						lrg_paint_list_text (list, k->font, value, roundf (sx + 8.0f * s),
						                     roundf (sy + (sh - fs) * 0.5f), fs, 0.0f, &ink);
					lrg_paint_list_icon (list, LRG_ICON_CHEVRON_DOWN, sx + sw - icon - 4.0f * s,
					                     sy + (sh - icon) * 0.5f, icon, 0.0f, &ink);
				}
				break;

			case LRG_BLOCK_SLOT_NUMBER:
			case LRG_BLOCK_SLOT_TEXT:
			default:
				{
					LrgCornerRadii rr;

					lrg_corner_radii_init_uniform (&rr, type == LRG_BLOCK_SLOT_NUMBER ? sh * 0.5f : 4.0f * s);
					lrg_paint_list_rect_ex (list, sx, sy, sw, sh, &rr, &k->field.background, NULL,
					                        editing ? MAX (1.5f, 2.0f * s) : k->field.border_width,
					                        editing ? &k->accent : &k->field.border);
					if (!text)
						break;
					lrg_paint_measure_text (k->font, value, fs, 0.0f, &tw, NULL);
					if (editing && self->edit_all && *value != '\0')
						lrg_paint_list_rect (list, sx + (sw - tw) * 0.5f - 1.0f, sy + 3.0f * s, tw + 2.0f,
						                     sh - 6.0f * s, 2.0f, &k->selection);
					lrg_paint_list_text (list, k->font, value, roundf (sx + (sw - tw) * 0.5f),
					                     roundf (sy + (sh - fs) * 0.5f), fs, 0.0f, &k->field.foreground);
					if (editing && !self->edit_all)
					{
						g_autofree gchar *before = g_strndup (value, self->edit_cursor);
						gfloat            cw = 0.0f;

						lrg_paint_measure_text (k->font, before, fs, 0.0f, &cw, NULL);
						lrg_paint_list_line (list, sx + (sw - tw) * 0.5f + cw + 0.5f, sy + 4.0f * s,
						                     sx + (sw - tw) * 0.5f + cw + 0.5f, sy + sh - 4.0f * s,
						                     MAX (1.0f, 1.5f * s), &k->accent);
					}
				}
				break;
			}

			/* --- the slot a dragged value would drop into --- */
			if (self->drag == DRAG_CARRY && self->target == b && self->target_place == LRG_BLOCK_PLACE_INPUT &&
			    self->target_index == (guint) t->slot)
				lrg_paint_list_border (list, sx - 2.0f, sy - 2.0f, sw + 4.0f, sh + 4.0f, sh * 0.5f + 2.0f, 2.5f,
				                       &(GrlColor){ 255, 255, 255, 235 });
		}
	}
}

static void paint_chain (LrgBlockEditor *self,
                         LrgPaintList   *list,
                         const Looks    *k,
                         LrgBlock       *first,
                         const Map      *map,
                         const GrlColor *notch_bg);

static gboolean
visible (const Looks *k,
         gfloat       x,
         gfloat       y,
         gfloat       w,
         gfloat       h)
{
	return x + w + 8.0f >= k->clip.x && y + h + 12.0f >= k->clip.y &&
	       x - 8.0f <= k->clip.x + k->clip.w && y - 8.0f <= k->clip.y + k->clip.h;
}

/*
 * paint_one:
 *
 * A block with its mouths and inputs. Mouth stacks go first so the
 * block's tabs overlap their first blocks, inputs last so they sit on
 * the block.
 */
static void
paint_one (LrgBlockEditor *self,
           LrgPaintList   *list,
           const Looks    *k,
           LrgBlock       *b,
           const Map      *map,
           const GrlColor *notch_bg)
{
	gfloat   X = map->ox + b->sx.value * map->s;
	gfloat   Y = map->oy + b->sy.value * map->s;
	GrlColor base;
	guint    i;

	if (!visible (k, X, Y, b->w * map->s, b->h * map->s))
		return;
	for (i = 0; i < shape_mouths (b->kind->shape); i++)
		paint_chain (self, list, k, b->mouths[i], map, NULL);

	block_color (self, b, &base);
	paint_shape (list, k, b, X, Y, map->s, &base, notch_bg);
	paint_content (self, list, k, b, X, Y, map->s, &base);

	if (b->selected && b->editor == self)
	{
		gfloat g = 2.0f + k->mark.border_width * 0.5f;
		gfloat rad = shape_is_value (b->kind->shape) ? b->h * map->s * 0.5f + g : RADIUS * map->s + g;

		lrg_paint_list_border (list, X - g, Y - g, b->w * map->s + g * 2.0f, b->h * map->s + g * 2.0f, rad,
		                       k->mark.border_width, &k->mark.border);
	}

	for (i = 0; i < b->kind->slots->len; i++)
		if (b->inputs[i] != NULL)
			paint_one (self, list, k, b->inputs[i], map, NULL);
}

/* paint_chain: a stack from its last block up, so each tab overlaps the block below */
static void
paint_chain (LrgBlockEditor *self,
             LrgPaintList   *list,
             const Looks    *k,
             LrgBlock       *first,
             const Map      *map,
             const GrlColor *notch_bg)
{
	g_autoptr(GPtrArray) chain = NULL;
	LrgBlock            *c;
	guint                i;

	if (first == NULL)
		return;
	chain = g_ptr_array_new ();
	for (c = first; c != NULL; c = c->next)
		g_ptr_array_add (chain, c);
	for (i = chain->len; i > 0; i--)
		paint_one (self, list, k, g_ptr_array_index (chain, i - 1), map, i == 1 ? notch_bg : NULL);
}

/*
 * body_rects:
 *
 * The rectangles a block covers on screen: its bounds, or for C and E
 * blocks the rows, the arm and the bottom (not the mouths, which show
 * the stacks inside), so shadows and glows hug the silhouette.
 */
static guint
body_rects (LrgBlock *b,
            gfloat    X,
            gfloat    Y,
            gfloat    s,
            Rect     *out)
{
	guint mouths = shape_mouths (b->kind->shape);
	guint n = 0;
	guint i;

	if (mouths == 0)
	{
		out[0].x = X;
		out[0].y = Y;
		out[0].w = b->w * s;
		out[0].h = b->h * s;
		return 1;
	}
	for (i = 0; i < mouths; i++)
	{
		out[n].x = X;
		out[n].y = Y + b->row_y[i] * s;
		out[n].w = b->w * s;
		out[n].h = b->row_h[i] * s;
		n++;
	}
	out[n].x = X;
	out[n].y = Y + b->mouth_y[0] * s;
	out[n].w = ARM_W * s;
	out[n].h = (b->h - BOTTOM_H - b->mouth_y[0]) * s;
	n++;
	out[n].x = X;
	out[n].y = Y + (b->h - BOTTOM_H) * s;
	out[n].w = b->w * s;
	out[n].h = BOTTOM_H * s;
	n++;
	return n;
}

/*
 * paint_script:
 *
 * Shadows of every block first (so no shadow falls on the script
 * itself), then the running glow, then the blocks. A @lifted script
 * (being carried) casts a deeper shadow.
 */
static void
paint_script (LrgBlockEditor *self,
              LrgPaintList   *list,
              const Looks    *k,
              LrgBlock       *top,
              const Map      *map,
              const GrlColor *notch_bg,
              gboolean        lifted)
{
	g_autoptr(GPtrArray) all = g_ptr_array_new ();
	gfloat               lift = lifted ? 2.2f : 1.0f;
	gfloat               script_glow = CLAMP (top->glow.value, 0.0f, 1.0f);
	guint                i;

	collect_tree (top, TRUE, all);

	/* --- shadows --- */
	if (k->item.shadow.a > 0)
	{
		GrlColor sh = k->item.shadow;

		sh.a = (guint8) MIN (255.0f, (gfloat) sh.a * (lifted ? 1.4f : 0.8f));
		for (i = 0; i < all->len; i++)
		{
			LrgBlock *b = g_ptr_array_index (all, i);

			Rect      r[MAX_MOUTHS + 2];
			guint     n;
			guint     j;

			if (b->place == LRG_BLOCK_PLACE_INPUT)
				continue;
			n = body_rects (b, map->ox + b->sx.value * map->s, map->oy + b->sy.value * map->s, map->s, r);
			for (j = 0; j < n; j++)
				lrg_paint_list_shadow (list, r[j].x, r[j].y, r[j].w, r[j].h, RADIUS * map->s,
				                       k->item.shadow_blur * lift * map->s, 0.0f, 0.0f,
				                       k->item.shadow_offset_y * lift * lift * map->s, &sh);
		}
	}

	/* --- running glow: the whole script from its top, else per block --- */
	for (i = 0; i < all->len; i++)
	{
		LrgBlock *b = g_ptr_array_index (all, i);
		gfloat    g = MAX (script_glow, CLAMP (b->glow.value, 0.0f, 1.0f));
		GrlColor  c = k->glow.glow;
		gfloat    pad = 3.0f * map->s;
		Rect      r[MAX_MOUTHS + 2];
		guint     n;
		guint     j;

		if (g <= 0.01f || b->place == LRG_BLOCK_PLACE_INPUT)
			continue;
		c.a = (guint8) ((gfloat) c.a * g);
		n = body_rects (b, map->ox + b->sx.value * map->s, map->oy + b->sy.value * map->s, map->s, r);
		for (j = 0; j < n; j++)
			lrg_paint_list_shadow (list, r[j].x - pad, r[j].y - pad, r[j].w + pad * 2.0f, r[j].h + pad * 2.0f,
			                       (RADIUS + 3.0f) * map->s, MAX (2.0f, k->glow.glow_size * map->s * 0.6f),
			                       0.0f, 0.0f, 0.0f, &c);
	}

	/* --- blocks --- */
	paint_chain (self, list, k, top, map, notch_bg);
}

/* paint_grid: a dot every grid-size units, skipped when too dense */
static void
paint_grid (LrgBlockEditor *self,
            LrgPaintList   *list,
            const Looks    *k,
            const Rect     *ws)
{
	gfloat step = self->grid_size;
	gfloat s;
	gfloat gx0;
	gfloat gy0;
	gfloat gx;
	gfloat gy;
	guint  count = 0;

	if (!self->show_grid || k->grid.foreground.a == 0)
		return;
	while (step * self->zoom < 12.0f)
		step *= 2.0f;
	s = step * self->zoom;
	gx0 = floorf (self->scroll_x / step) * step;
	gy0 = floorf (self->scroll_y / step) * step;
	for (gx = gx0; ws->x + (gx - self->scroll_x) * self->zoom <= ws->x + ws->w; gx += step)
		for (gy = gy0; ws->y + (gy - self->scroll_y) * self->zoom <= ws->y + ws->h; gy += step)
		{
			if (++count > 6000)
				return;
			lrg_paint_list_circle (list, ws->x + (gx - self->scroll_x) * self->zoom,
			                       ws->y + (gy - self->scroll_y) * self->zoom, MAX (0.8f, MIN (1.4f, s / 20.0f)),
			                       &k->grid.foreground);
		}
}

/* active_category: the category at the top of the palette's view */
static gint
active_category (LrgBlockEditor *self)
{
	gint  best = -1;
	guint i;

	for (i = 0; i < self->categories->len; i++)
	{
		Category *c = g_ptr_array_index (self->categories, i);

		if (c->pal_y < 0.0f)
			continue;
		if (best < 0 || c->pal_y <= self->pal_scroll.target + PAL_PAD)
			best = (gint) i;
	}
	return best;
}

#define STRIP_ITEM (52.0f)

/* strip_category_at: the category of the strip entry at a screen y */
static Category *
strip_category_at (LrgBlockEditor *self,
                   gfloat          y)
{
	Rect  strip;
	gint  slot;
	gint  n = 0;
	guint i;

	areas (self, &strip, NULL, NULL);
	slot = (gint) floorf ((y - strip.y - 6.0f) / STRIP_ITEM);
	for (i = 0; i < self->categories->len; i++)
	{
		Category *c = g_ptr_array_index (self->categories, i);

		if (c->pal_y < 0.0f)
			continue;
		if (n++ == slot)
			return c;
	}
	return NULL;
}

static void
paint_palette (LrgBlockEditor *self,
               LrgPaintList   *list,
               Looks          *k)
{
	LrgWidget *w = LRG_WIDGET (self);
	Rect       strip;
	Rect       pal;
	Map        map;
	gint       active = active_category (self);
	gint       n = 0;
	guint      i;

	areas (self, &strip, &pal, NULL);
	if (pal.w <= 0.0f && strip.w <= 0.0f)
		return;

	/* --- the block list --- */
	lrg_widget_paint_box (w, list, &k->palette, pal.x, pal.y, pal.w, pal.h);
	lrg_paint_list_push_clip (list, pal.x, pal.y, pal.w, pal.h);
	palette_origin (self, &map.ox, &map.oy);
	map.s = 1.0f;
	k->clip = pal;
	for (i = 0; i < self->categories->len; i++)
	{
		Category *c = g_ptr_array_index (self->categories, i);
		GrlColor  dot;

		if (c->pal_y < 0.0f)
			continue;
		lrg_block_editor_get_category_color (self, c->name, &dot);
		lrg_paint_list_circle (list, map.ox + PAL_PAD + 5.0f, map.oy + c->pal_y + PAL_HEADER_H * 0.5f, 5.0f, &dot);
		if (*c->name != '\0')
			lrg_paint_list_text (list, k->font, c->name, roundf (map.ox + PAL_PAD + 16.0f),
			                     roundf (map.oy + c->pal_y + (PAL_HEADER_H - k->fss) * 0.5f), k->fss, 0.0f,
			                     &k->palette.foreground);
	}
	for (i = 0; i < self->palette->len; i++)
		paint_one (self, list, k, g_ptr_array_index (self->palette, i), &map, &k->palette.background);

	/* --- dropping a stack here deletes it: show it --- */
	if (self->drag == DRAG_CARRY && in_palette_area (self, self->pointer_x, self->pointer_y))
	{
		GrlColor tint;
		gfloat   icon = 44.0f;

		lrg_paint_color_with_alpha (&k->danger, 0.16f, &tint);
		lrg_paint_list_rect (list, pal.x, pal.y, pal.w, pal.h, 0.0f, &tint);
		lrg_paint_list_icon (list, LRG_ICON_TRASH, pal.x + (pal.w - icon) * 0.5f, pal.y + (pal.h - icon) * 0.5f,
		                     icon, 0.0f, &k->danger);
	}
	lrg_paint_list_pop_clip (list);

	/* --- the category strip --- */
	lrg_widget_paint_box (w, list, &k->strip, strip.x, strip.y, strip.w, strip.h);
	lrg_paint_list_push_clip (list, strip.x, strip.y, strip.w, strip.h);
	for (i = 0; i < self->categories->len; i++)
	{
		Category    *c = g_ptr_array_index (self->categories, i);
		gfloat       top = strip.y + 6.0f + (gfloat) n * STRIP_ITEM;
		gfloat       cx = strip.x + strip.w * 0.5f;
		gfloat       tw = 0.0f;
		gfloat       ts = k->fss * 0.85f;
		GrlColor     dot;
		GrlColor     ring;
		const gchar *name = *c->name != '\0' ? c->name : "...";

		if (c->pal_y < 0.0f)
			continue;
		n++;
		if ((gint) i == active)
			lrg_widget_paint_box (w, list, &k->strip_sel, strip.x + 4.0f, top, strip.w - 8.0f, STRIP_ITEM - 4.0f);
		lrg_block_editor_get_category_color (self, c->name, &dot);
		lrg_paint_color_shade (&dot, -0.3f, &ring);
		lrg_paint_list_circle (list, cx, top + 15.0f, 11.0f, &ring);
		lrg_paint_list_circle (list, cx, top + 15.0f, 10.0f, &dot);
		lrg_paint_measure_text (k->font, name, ts, 0.0f, &tw, NULL);
		lrg_paint_list_text (list, k->font, name, roundf (cx - MIN (tw, strip.w - 6.0f) * 0.5f),
		                     roundf (top + 29.0f), ts, 0.0f,
		                     (gint) i == active ? &k->strip_sel.foreground : &k->strip.foreground);
	}
	lrg_paint_list_pop_clip (list);
}

static void
lrg_block_editor_paint (LrgWidget    *widget,
                        LrgPaintList *list)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (widget);
	Looks           k;
	Rect            ws;
	Map             map;
	guint           i;

	ensure_layout (self);
	resolve_looks (self, &k);
	areas (self, NULL, NULL, &ws);

	/* --- workspace: background, grid, ghost, scripts --- */
	lrg_widget_paint_box (widget, list, &k.box, ws.x, ws.y, ws.w, ws.h);
	lrg_paint_list_push_clip (list, ws.x, ws.y, ws.w, ws.h);
	paint_grid (self, list, &k, &ws);
	map.ox = ws.x - self->scroll_x * self->zoom;
	map.oy = ws.y - self->scroll_y * self->zoom;
	map.s = self->zoom;
	k.clip = ws;
	if (self->drag == DRAG_CARRY && self->has_ghost && self->carried != NULL)
	{
		gfloat gx = map.ox + self->ghost_x * map.s;
		gfloat gy = map.oy + self->ghost_y * map.s;

		lrg_paint_list_rect (list, gx, gy, self->carried->w * map.s, self->carried_h * map.s,
		                     k.ghost.radii.top_left * map.s, &k.ghost.background);
		tab (list, gx, gy + self->carried_h * map.s, map.s, 0.0f, &k.ghost.background);
	}
	for (i = 0; i < self->scripts->len; i++)
		paint_script (self, list, &k, g_ptr_array_index (self->scripts, i), &map, &k.box.background, FALSE);
	lrg_paint_list_pop_clip (list);

	/* --- palette and category strip --- */
	if (self->show_palette)
		paint_palette (self, list, &k);

	/* --- the carried stack, on top of everything --- */
	if (self->carried != NULL)
	{
		gboolean trash = in_palette_area (self, self->pointer_x, self->pointer_y);

		k.clip.x = lrg_widget_get_world_x (widget);
		k.clip.y = lrg_widget_get_world_y (widget);
		k.clip.w = lrg_widget_get_width (widget);
		k.clip.h = lrg_widget_get_height (widget);
		lrg_paint_list_push_clip (list, k.clip.x, k.clip.y, k.clip.w, k.clip.h);
		if (trash)
			lrg_paint_list_push_opacity (list, 0.55f);
		paint_script (self, list, &k, self->carried, &map, &k.box.background, TRUE);
		if (trash)
			lrg_paint_list_pop_opacity (list);
		lrg_paint_list_pop_clip (list);
	}
}

static void
lrg_block_editor_measure (LrgWidget *widget,
                          gfloat    *preferred_width,
                          gfloat    *preferred_height)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (widget);

	/* A workspace has no natural size: a comfortable area */
	*preferred_width = 520.0f + (self->show_palette ? self->palette_width + STRIP_W : 0.0f);
	*preferred_height = 360.0f;
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

static gboolean
step_block (LrgBlock  *b,
            LrgMotion  motion,
            gfloat     dt)
{
	gboolean moving = FALSE;

	if (b->moving)
	{
		LrgMotion m = motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SNAPPY;
		gboolean  a = FALSE;

		lrg_spring_set_motion (&b->sx, m);
		lrg_spring_set_motion (&b->sy, m);
		a |= lrg_spring_step (&b->sx, dt);
		a |= lrg_spring_step (&b->sy, dt);
		if (!a)
		{
			lrg_spring_snap (&b->sx, b->sx.target);
			lrg_spring_snap (&b->sy, b->sy.target);
			b->moving = FALSE;
		}
		moving |= a;
	}
	if (b->glow.value != b->glow.target || b->glow.velocity != 0.0f)
	{
		lrg_spring_set_motion (&b->glow, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SMOOTH);
		if (lrg_spring_step (&b->glow, dt))
			moving = TRUE;
		else
			lrg_spring_snap (&b->glow, b->glow.target);
	}
	return moving;
}

/*
 * lrg_block_editor_step:
 *
 * Blocks glide to their laid-out places (snapping, bumping, gaps
 * opening under a dragged stack), glows fade, the palette scrolls.
 * The first frame snaps everything into place.
 */
static gboolean
lrg_block_editor_step (LrgWidget *widget,
                       gfloat     delta_time)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (widget);
	LrgMotion       motion = lrg_widget_get_motion (widget);
	GHashTableIter  iter;
	gpointer        key;
	gboolean        moving = FALSE;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	ensure_layout (self);

	if (!self->stepped)
	{
		self->stepped = TRUE;
		g_hash_table_iter_init (&iter, self->blocks);
		while (g_hash_table_iter_next (&iter, &key, NULL))
		{
			LrgBlock *b = key;

			lrg_spring_snap (&b->sx, b->sx.target);
			lrg_spring_snap (&b->sy, b->sy.target);
			lrg_spring_snap (&b->glow, b->glow.target);
			b->moving = FALSE;
		}
		lrg_spring_snap (&self->pal_scroll, self->pal_scroll.target);
		self->anim = FALSE;
		return FALSE;
	}
	if (!self->anim)
		return FALSE;

	g_hash_table_iter_init (&iter, self->blocks);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		moving |= step_block (key, motion, delta_time);

	if (self->pal_scroll.value != self->pal_scroll.target || self->pal_scroll.velocity != 0.0f)
	{
		lrg_spring_set_motion (&self->pal_scroll, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SMOOTH);
		if (lrg_spring_step (&self->pal_scroll, delta_time))
			moving = TRUE;
		else
			lrg_spring_snap (&self->pal_scroll, self->pal_scroll.target);
	}
	self->anim = moving;
	return moving;
}

/* ==========================================================================
 * Interaction
 * ========================================================================== */

/*
 * find_target:
 *
 * Where the carried stack would go if dropped now, measured on a
 * layout without the insertion gap (so the gap cannot move the target
 * it opened for): the nearest valid connection within reach of the
 * stack's top-left corner (or, for values, of its left middle against
 * an empty slot's left middle). Statement stacks ending with a tab can
 * also go above a script.
 */
static void
find_target (LrgBlockEditor *self)
{
	LrgBlock     *d = self->carried;
	LrgBlockShape ds;
	gfloat        best = SNAP_DIST * SNAP_DIST;
	gfloat        cx;
	gfloat        cy;
	guint         i;
	guint         s;

	self->target = NULL;
	self->target_top = NULL;
	layout (self);
	self->layout_dirty = TRUE;
	if (d == NULL || in_palette_area (self, self->pointer_x, self->pointer_y))
		return;
	ds = d->kind->shape;
	cx = d->x;
	cy = d->y;

	for (i = 0; i < self->drawn->len; i++)
	{
		LrgBlock *t = g_ptr_array_index (self->drawn, i);

		if (shape_is_value (ds))
		{
			/* --- an empty slot that takes the value --- */
			for (s = 0; s < t->kind->slots->len; s++)
			{
				Rect  *r = &t->slot[s];
				gfloat ex;
				gfloat ey;

				if (t->inputs[s] != NULL || !slot_accepts (kind_slot (t->kind, s)->type, ds))
					continue;
				ex = cx - (t->lx + r->x);
				ey = (cy + d->h * 0.5f) - (t->ly + r->y + r->h * 0.5f);
				if (ex * ex + ey * ey < best)
				{
					best = ex * ex + ey * ey;
					self->target = t;
					self->target_place = LRG_BLOCK_PLACE_INPUT;
					self->target_index = s;
				}
			}
			continue;
		}

		/* --- under a block --- */
		if (attach_ok (d, t, LRG_BLOCK_PLACE_NEXT, 0))
		{
			gfloat ex = cx - t->lx;
			gfloat ey = cy - (t->ly + t->h);

			if (ex * ex + ey * ey < best)
			{
				best = ex * ex + ey * ey;
				self->target = t;
				self->target_place = LRG_BLOCK_PLACE_NEXT;
				self->target_index = 0;
			}
		}

		/* --- into a mouth --- */
		for (s = 0; s < shape_mouths (t->kind->shape); s++)
		{
			gfloat ex;
			gfloat ey;

			if (!attach_ok (d, t, LRG_BLOCK_PLACE_MOUTH, s))
				continue;
			ex = cx - (t->lx + ARM_W);
			ey = cy - (t->ly + t->mouth_y[s]);
			if (ex * ex + ey * ey < best)
			{
				best = ex * ex + ey * ey;
				self->target = t;
				self->target_place = LRG_BLOCK_PLACE_MOUTH;
				self->target_index = s;
			}
		}
	}

	/* --- above a script (its top takes a stack over it) --- */
	if (!shape_is_value (ds) && shape_has_bottom (last_of_chain (d)->kind->shape))
	{
		for (i = 0; i < self->scripts->len; i++)
		{
			LrgBlock *t = g_ptr_array_index (self->scripts, i);
			gfloat    ex;
			gfloat    ey;

			if (!shape_has_top (t->kind->shape))
				continue;
			ex = cx - t->x;
			ey = cy + self->carried_h - t->y;
			if (ex * ex + ey * ey < best)
			{
				best = ex * ex + ey * ey;
				self->target = NULL;
				self->target_top = t;
			}
		}
	}
}

/* select_quiet: selects without checks (used while gestures run) */
static void
select_quiet (LrgBlockEditor *self,
              LrgBlock       *block)
{
	if (self->selected == block)
		return;
	if (self->selected != NULL)
		self->selected->selected = FALSE;
	self->selected = block;
	if (block != NULL)
		block->selected = TRUE;
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

/* update_carry: the carried stack follows the pointer */
static void
update_carry (LrgBlockEditor *self)
{
	gfloat px;
	gfloat py;

	lrg_block_editor_screen_to_workspace (self, self->pointer_x, self->pointer_y, &px, &py);
	self->carried->x = px - self->grab_x;
	self->carried->y = py - self->grab_y;
	find_target (self);
}

/*
 * begin_carry:
 *
 * @b (a program block) leaves its place with the blocks below it and
 * follows the pointer; @snap is the program before the gesture (the
 * undo step, or what Escape restores); (@bx, @by) is where @b is shown
 * in workspace units, so it does not jump under the pointer.
 */
static void
begin_carry (LrgBlockEditor *self,
             LrgBlock       *b,
             Snapshot       *snap,
             gfloat          bx,
             gfloat          by)
{
	gfloat px;
	gfloat py;

	g_clear_pointer (&self->drag_snapshot, snapshot_free);
	self->drag_snapshot = snap;
	self->carried = unlink_block (self, b);
	/* (grabbed where the button went down, so the block keeps its
	 * offset from the pointer) */
	lrg_block_editor_screen_to_workspace (self, self->press_x, self->press_y, &px, &py);
	self->grab_x = px - bx;
	self->grab_y = py - by;
	self->drag = DRAG_CARRY;
	self->last_click = NULL;
	select_quiet (self, b);
	update_carry (self);
}

/*
 * end_carry:
 *
 * Drops the carried stack: over the palette it is deleted; at the
 * target it connects; above a script it becomes that script's new
 * top; elsewhere it is a script of its own. One undo step. With
 * @cancel the program before the gesture is restored.
 */
static void
end_carry (LrgBlockEditor *self,
           gboolean        cancel)
{
	g_autoptr(LrgBlock) d = g_steal_pointer (&self->carried);
	Snapshot           *snap = g_steal_pointer (&self->drag_snapshot);
	LrgBlock           *target = self->target;
	LrgBlock           *top = self->target_top;
	LrgBlockPlace       place = self->target_place;
	guint               index = self->target_index;
	gboolean            trash = in_palette_area (self, self->pointer_x, self->pointer_y);
	gfloat              height = self->carried_h;
	guint               i;

	self->drag = DRAG_NONE;
	self->target = NULL;
	self->target_top = NULL;
	self->layout_dirty = TRUE;
	if (d == NULL)
	{
		snapshot_free (snap);
		return;
	}
	if (cancel)
	{
		if (snap != NULL)
			snapshot_restore (self, snap);
		snapshot_free (snap);
		flush_pending (self);
		return;
	}

	if (trash)
		orphan_tree (self, d);
	else if (target != NULL && in_program (self, target) && attach_ok (d, target, place, index))
		place_owned (self, g_object_ref (d), target, place, index);
	else if (top != NULL && in_program (self, top) && top->parent == NULL &&
	         g_ptr_array_find (self->scripts, top, &i))
	{
		LrgBlock *tref = g_ptr_array_steal_index (self->scripts, i);

		link_at (last_of_chain (d), LRG_BLOCK_PLACE_NEXT, 0, tref);
		link_script (self, g_object_ref (d), top->x, top->y - height);
	}
	else
		link_script (self, g_object_ref (d), d->x, d->y);

	if (snap != NULL)
		push_undo (self, snap);
	commit (self);
}

/* finish_interaction: ends gestures and typing, keeping their result */
static void
finish_interaction (LrgBlockEditor *self)
{
	if (self->drag == DRAG_CARRY)
		end_carry (self, FALSE);
	self->drag = DRAG_NONE;
	self->press_block = NULL;
	self->press_kind = NULL;
	if (self->edit_block != NULL)
		lrg_block_editor_commit_slot_edit (self);
}

/* carry_from_palette: a new block of the pressed kind leaves the palette */
static void
carry_from_palette (LrgBlockEditor *self)
{
	LrgBlock *pb = self->press_kind != NULL ? palette_block_for (self, self->press_kind) : NULL;
	Snapshot *snap;
	LrgBlock *b;
	gfloat    ox;
	gfloat    oy;
	gfloat    bx;
	gfloat    by;

	if (pb == NULL)
	{
		self->drag = DRAG_NONE;
		return;
	}
	snap = snapshot_take (self);
	b = lrg_block_new (self->press_kind);
	adopt_tree (self, b);
	palette_origin (self, &ox, &oy);
	lrg_block_editor_screen_to_workspace (self, ox + pb->lx, oy + pb->ly, &bx, &by);
	begin_carry (self, b, snap, bx, by);
	g_object_unref (b);   /* the carried reference keeps it */
}

static gboolean
handle_press (LrgBlockEditor   *self,
              const LrgUIEvent *event)
{
	gfloat    x = lrg_ui_event_get_x (event);
	gfloat    y = lrg_ui_event_get_y (event);
	gint      button = lrg_ui_event_get_button (event);
	LrgBlock *b;
	LrgBlock *top;
	gint      slot = -1;
	Rect      strip;
	Rect      pal;

	if (self->drag != DRAG_NONE)
		return TRUE;
	self->press_x = x;
	self->press_y = y;
	self->pointer_x = x;
	self->pointer_y = y;
	self->pointer_in = TRUE;
	self->drag_button = button;

	/* --- a click anywhere but the slot being typed in keeps the text --- */
	if (self->edit_block != NULL)
	{
		b = lrg_block_editor_block_at (self, x, y, &slot);
		if (b == self->edit_block && slot == (gint) self->edit_slot && button == GRL_MOUSE_BUTTON_LEFT)
			return TRUE;
		lrg_block_editor_commit_slot_edit (self);
	}

	/* --- panning: middle, right (unless a context menu is set), Space + left --- */
	if (button == GRL_MOUSE_BUTTON_MIDDLE || (button == GRL_MOUSE_BUTTON_LEFT && self->space_armed))
	{
		self->drag = DRAG_PAN;
		return TRUE;
	}
	if (button == GRL_MOUSE_BUTTON_RIGHT)
	{
		if (lrg_widget_get_context_menu (LRG_WIDGET (self)) != NULL)
			return FALSE;
		self->drag = DRAG_PAN;
		return TRUE;
	}
	if (button != GRL_MOUSE_BUTTON_LEFT)
		return FALSE;

	/* --- category strip and palette --- */
	areas (self, &strip, &pal, NULL);
	if (rect_has (&strip, x, y))
	{
		Category *c = strip_category_at (self, y);

		if (c != NULL)
			lrg_block_editor_scroll_palette_to (self, c->name);
		return TRUE;
	}
	if (rect_has (&pal, x, y))
	{
		b = palette_block_at (self, x, y);
		if (b != NULL)
		{
			self->press_kind = b->kind;
			self->drag = DRAG_PRESS_PALETTE;
		}
		return TRUE;
	}

	/* --- workspace: a block, else empty space (which pans) --- */
	b = lrg_block_editor_block_at (self, x, y, &slot);
	if (b == NULL)
	{
		select_quiet (self, NULL);
		self->drag = DRAG_PAN;
		return TRUE;
	}
	top = lrg_block_get_top (b);
	if (self->last_click != NULL && lrg_block_get_top (self->last_click) == top &&
	    self->clock - self->last_click_time <= DOUBLE_CLICK &&
	    fabsf (x - self->last_click_x) + fabsf (y - self->last_click_y) <= DRAG_THRESHOLD * 2.0f)
	{
		/* Double click: run the script */
		self->last_click = NULL;
		select_quiet (self, b);
		g_signal_emit (self, signals[SIGNAL_BLOCK_ACTIVATED], 0, top);
		return TRUE;
	}
	self->last_click = b;
	self->last_click_time = self->clock;
	self->last_click_x = x;
	self->last_click_y = y;
	select_quiet (self, b);
	self->press_block = b;
	self->press_slot = slot;
	self->drag = DRAG_PRESS;
	return TRUE;
}

static gboolean
handle_motion (LrgBlockEditor   *self,
               const LrgUIEvent *event)
{
	gfloat x = lrg_ui_event_get_x (event);
	gfloat y = lrg_ui_event_get_y (event);
	gfloat dx = x - self->pointer_x;
	gfloat dy = y - self->pointer_y;
	gboolean far = fabsf (x - self->press_x) + fabsf (y - self->press_y) >= DRAG_THRESHOLD;

	self->pointer_x = x;
	self->pointer_y = y;
	self->pointer_in = TRUE;

	switch (self->drag)
	{
	case DRAG_PAN:
		lrg_block_editor_pan_by (self, dx, dy);
		return TRUE;
	case DRAG_PRESS:
		if (far && self->press_block != NULL)
		{
			LrgBlock *b = self->press_block;

			ensure_layout (self);
			self->press_block = NULL;
			begin_carry (self, b, snapshot_take (self), b->sx.value, b->sy.value);
		}
		return TRUE;
	case DRAG_PRESS_PALETTE:
		if (far)
			carry_from_palette (self);
		return TRUE;
	case DRAG_CARRY:
		update_carry (self);
		return TRUE;
	case DRAG_NONE:
	default:
		return FALSE;
	}
}

static gboolean
handle_release (LrgBlockEditor   *self,
                const LrgUIEvent *event)
{
	gint      button = lrg_ui_event_get_button (event);
	LrgBlock *b = self->press_block;
	gint      slot = self->press_slot;
	Drag      drag = self->drag;

	if (drag == DRAG_NONE || button != self->drag_button)
		return FALSE;
	self->pointer_x = lrg_ui_event_get_x (event);
	self->pointer_y = lrg_ui_event_get_y (event);
	self->drag = DRAG_NONE;
	self->press_block = NULL;
	self->press_kind = NULL;

	if (drag == DRAG_CARRY)
	{
		self->drag = DRAG_CARRY;
		update_carry (self);
		end_carry (self, FALSE);
	}
	else if (drag == DRAG_PRESS && b != NULL && slot >= 0)
		lrg_block_editor_begin_slot_edit (self, b, (guint) slot);
	return TRUE;
}

/* ensure_visible: scrolls the workspace so @b is in view */
static void
ensure_visible (LrgBlockEditor *self,
                LrgBlock       *b)
{
	Rect   ws;
	gfloat x;
	gfloat y;
	gfloat w = b->w * self->zoom;
	gfloat h = MIN (b->h * self->zoom, 200.0f);
	gfloat dx = 0.0f;
	gfloat dy = 0.0f;

	ensure_layout (self);
	areas (self, NULL, NULL, &ws);
	lrg_block_editor_workspace_to_screen (self, b->lx, b->ly, &x, &y);
	if (x < ws.x + 8.0f)
		dx = ws.x + 8.0f - x;
	else if (x + w > ws.x + ws.w - 8.0f)
		dx = MAX (ws.x + 8.0f - x, ws.x + ws.w - 8.0f - x - w);
	if (y < ws.y + 8.0f)
		dy = ws.y + 8.0f - y;
	else if (y + h > ws.y + ws.h - 8.0f)
		dy = MAX (ws.y + 8.0f - y, ws.y + ws.h - 8.0f - y - h);
	if (dx != 0.0f || dy != 0.0f)
		lrg_block_editor_pan_by (self, dx, dy);
}

/*
 * navigate:
 *
 * Keyboard selection: Up/Down walk the selected script in reading
 * order (block, its inputs, its mouths, the block below); Left/Right
 * jump to the previous/next script.
 */
static gboolean
navigate (LrgBlockEditor *self,
          GrlKey          key)
{
	LrgBlock *top;
	LrgBlock *pick = NULL;
	guint     i;

	if (self->scripts->len == 0)
		return TRUE;
	if (self->selected == NULL)
		pick = g_ptr_array_index (self->scripts, 0);
	else if (key == GRL_KEY_UP || key == GRL_KEY_DOWN)
	{
		g_autoptr(GPtrArray) all = g_ptr_array_new ();

		top = lrg_block_get_top (self->selected);
		collect_tree (top, TRUE, all);
		if (g_ptr_array_find (all, self->selected, &i))
		{
			if (key == GRL_KEY_UP && i > 0)
				pick = g_ptr_array_index (all, i - 1);
			else if (key == GRL_KEY_DOWN && i + 1 < all->len)
				pick = g_ptr_array_index (all, i + 1);
		}
	}
	else
	{
		top = lrg_block_get_top (self->selected);
		if (g_ptr_array_find (self->scripts, top, &i))
		{
			if (key == GRL_KEY_LEFT && i > 0)
				pick = g_ptr_array_index (self->scripts, i - 1);
			else if (key == GRL_KEY_RIGHT && i + 1 < self->scripts->len)
				pick = g_ptr_array_index (self->scripts, i + 1);
		}
	}
	if (pick != NULL)
	{
		select_quiet (self, pick);
		ensure_visible (self, pick);
	}
	return TRUE;
}

static gboolean
handle_key (LrgBlockEditor    *self,
            GrlKey             key,
            LrgInputModifiers  mods)
{
	gboolean ctrl = (mods & LRG_INPUT_MODIFIER_CTRL) != 0;
	gboolean shift = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;

	/* --- typing into a slot --- */
	if (self->edit_block != NULL)
	{
		if (key == GRL_KEY_TAB)
		{
			LrgBlock *b = self->edit_block;
			gint      next = next_editable_slot (b, (gint) self->edit_slot);

			lrg_block_editor_commit_slot_edit (self);
			if (next >= 0)
				lrg_block_editor_begin_slot_edit (self, b, (guint) next);
			return TRUE;
		}
		return edit_key (self, key, mods);
	}

	if (key == GRL_KEY_TAB)
	{
		gint s;

		if (self->selected == NULL || ctrl)
			return FALSE;
		s = next_editable_slot (self->selected, -1);
		return s >= 0 && lrg_block_editor_begin_slot_edit (self, self->selected, (guint) s);
	}
	if (mods & LRG_INPUT_MODIFIER_ALT)
		return FALSE;

	/* --- Ctrl shortcuts --- */
	if (ctrl)
	{
		switch (key)
		{
		case GRL_KEY_Z:
			if (shift)
				lrg_block_editor_redo (self);
			else
				lrg_block_editor_undo (self);
			return TRUE;
		case GRL_KEY_Y:
			lrg_block_editor_redo (self);
			return TRUE;
		case GRL_KEY_D:
			lrg_block_editor_duplicate_selection (self);
			return TRUE;
		case GRL_KEY_C:
			lrg_block_editor_copy_selection (self);
			return TRUE;
		case GRL_KEY_X:
			if (lrg_block_editor_copy_selection (self))
				lrg_block_editor_remove_block (self, self->selected, TRUE);
			return TRUE;
		case GRL_KEY_V:
			lrg_block_editor_paste (self);
			return TRUE;
		default:
			return FALSE;
		}
	}
	if (shift)
		return FALSE;

	/* --- plain keys --- */
	switch (key)
	{
	case GRL_KEY_DELETE:
	case GRL_KEY_BACKSPACE:
		return lrg_block_editor_delete_selection (self);
	case GRL_KEY_UP:
	case GRL_KEY_DOWN:
	case GRL_KEY_LEFT:
	case GRL_KEY_RIGHT:
		return navigate (self, key);
	case GRL_KEY_ENTER:
	case GRL_KEY_KP_ENTER:
		if (self->selected == NULL)
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_BLOCK_ACTIVATED], 0, lrg_block_get_top (self->selected));
		return TRUE;
	case GRL_KEY_SPACE:
		/* Arms Space + drag panning */
		self->space_armed = TRUE;
		return TRUE;
	case GRL_KEY_ESCAPE:
		if (self->drag == DRAG_CARRY)
		{
			end_carry (self, TRUE);
			return TRUE;
		}
		if (self->selected != NULL)
		{
			select_quiet (self, NULL);
			return TRUE;
		}
		return FALSE;
	default:
		return FALSE;
	}
}

static gboolean
lrg_block_editor_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (widget);
	LrgWidget      *target = lrg_ui_event_get_target (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return handle_press (self, event);

	case LRG_UI_EVENT_MOUSE_MOVE:
		return handle_motion (self, event);

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		return handle_release (self, event);

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat x = lrg_ui_event_get_x (event);
			gfloat y = lrg_ui_event_get_y (event);
			gfloat sy = lrg_ui_event_get_scroll_y (event);
			gfloat sx = lrg_ui_event_get_scroll_x (event);
			LrgInputModifiers mods = lrg_ui_event_get_modifiers (event);

			if (in_palette_area (self, x, y))
			{
				/* The palette scrolls on its own */
				Rect   pal;
				gfloat max_scroll;

				ensure_layout (self);
				areas (self, NULL, &pal, NULL);
				max_scroll = MAX (0.0f, self->pal_content_h - pal.h);
				lrg_spring_set_target (&self->pal_scroll,
				                       CLAMP (self->pal_scroll.target - sy * WHEEL_PAN, 0.0f, max_scroll));
				if (!smooth (self))
					lrg_spring_snap (&self->pal_scroll, self->pal_scroll.target);
				else
					self->anim = TRUE;
				return TRUE;
			}
			if (mods & LRG_INPUT_MODIFIER_CTRL)
				lrg_block_editor_zoom_at (self, x, y, self->zoom * powf (WHEEL_STEP, sy));
			else if (mods & LRG_INPUT_MODIFIER_SHIFT)
				lrg_block_editor_pan_by (self, sy * WHEEL_PAN, 0.0f);
			else
				lrg_block_editor_pan_by (self, sx * WHEEL_PAN, sy * WHEEL_PAN);
			return TRUE;
		}

	case LRG_UI_EVENT_PAN:
		{
			gfloat x = lrg_ui_event_get_x (event);
			gfloat y = lrg_ui_event_get_y (event);
			gfloat dx = lrg_ui_event_get_delta_x (event);
			gfloat dy = lrg_ui_event_get_delta_y (event);

			/* Claim drags that start on empty workspace (or the palette,
			 * which then scrolls); a drag on a block falls through to the
			 * mouse replay and carries it */
			if (!self->touch_panning)
			{
				if (self->drag != DRAG_NONE ||
				    lrg_block_editor_block_at (self, x - dx, y - dy, NULL) != NULL ||
				    palette_block_at (self, x - dx, y - dy) != NULL)
					return FALSE;
				self->touch_panning = TRUE;
			}
			if (in_palette_area (self, x - dx, y - dy))
			{
				Rect pal;

				areas (self, NULL, &pal, NULL);
				lrg_spring_snap (&self->pal_scroll, CLAMP (self->pal_scroll.target - dy, 0.0f,
				                                           MAX (0.0f, self->pal_content_h - pal.h)));
			}
			else
				lrg_block_editor_pan_by (self, dx, dy);
			return TRUE;
		}

	case LRG_UI_EVENT_PAN_END:
		if (!self->touch_panning)
			return FALSE;
		self->touch_panning = FALSE;
		return TRUE;

	case LRG_UI_EVENT_ZOOM:
		lrg_block_editor_zoom_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event),
		                          self->zoom * lrg_ui_event_get_scale (event));
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		if (target != widget)
			return FALSE;
		return handle_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));

	case LRG_UI_EVENT_KEY_UP:
		if (target != widget || lrg_ui_event_get_key (event) != GRL_KEY_SPACE)
			return FALSE;
		self->space_armed = FALSE;
		if (self->drag == DRAG_PAN && self->drag_button == GRL_MOUSE_BUTTON_LEFT)
			self->drag = DRAG_NONE;
		return TRUE;

	case LRG_UI_EVENT_TEXT_INPUT:
		if (self->edit_block == NULL)
			return FALSE;
		edit_insert (self, lrg_ui_event_get_codepoint (event));
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		if (target != widget || self->selected == NULL)
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_BLOCK_ACTIVATED], 0, lrg_block_get_top (self->selected));
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		if (self->drag == DRAG_CARRY)
		{
			end_carry (self, TRUE);
			return TRUE;
		}
		if (self->edit_block != NULL)
		{
			lrg_block_editor_cancel_slot_edit (self);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (target == widget)
			self->pointer_in = FALSE;
		return FALSE;

	case LRG_UI_EVENT_FOCUS_OUT:
		self->space_armed = FALSE;
		if (self->edit_block != NULL)
			lrg_block_editor_commit_slot_edit (self);
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * YAML: writing
 * ========================================================================== */

/* fmt_num: a float as the shortest text that reads back exactly */
static gchar *
fmt_num (gfloat v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_formatd (buf, sizeof buf, "%.9g", (gdouble) v);
	return g_strdup (buf);
}

static void
set_num (YamlMapping *m,
         const gchar *key,
         gfloat       v)
{
	g_autofree gchar *text = fmt_num (v);

	yaml_mapping_set_string_member (m, key, text);
}

/*
 * set_text:
 *
 * A string member. Texts a YAML reader would take for null ("", "~",
 * "null") are double-quoted so they come back as the same string.
 */
static void
set_text (YamlMapping *m,
          const gchar *key,
          const gchar *text)
{
	if (*text == '\0' || g_strcmp0 (text, "~") == 0 || g_ascii_strcasecmp (text, "null") == 0)
	{
		g_autoptr(YamlNode) node = yaml_node_new_scalar (text, YAML_SCALAR_STYLE_DOUBLE_QUOTED);

		yaml_mapping_set_member (m, key, node);
	}
	else
		yaml_mapping_set_string_member (m, key, text);
}

static gchar *
color_to_hex (const GrlColor *c)
{
	return g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
}

/* hex_to_color: "#rrggbb" or "#rrggbbaa" */
static gboolean
hex_to_color (const gchar *text,
              GrlColor    *out)
{
	guint64 v;
	gchar  *end = NULL;
	gsize   len;

	if (text == NULL || text[0] != '#')
		return FALSE;
	len = strlen (text + 1);
	if (len != 6 && len != 8)
		return FALSE;
	v = g_ascii_strtoull (text + 1, &end, 16);
	if (end == NULL || *end != '\0')
		return FALSE;
	if (len == 6)
		v = (v << 8) | 0xff;
	out->r = (guint8) ((v >> 24) & 0xff);
	out->g = (guint8) ((v >> 16) & 0xff);
	out->b = (guint8) ((v >> 8) & 0xff);
	out->a = (guint8) (v & 0xff);
	return TRUE;
}

static YamlSequence *chain_to_yaml (LrgBlock *first);

/* block_to_yaml: kind, every slot value, nested inputs, mouths */
static YamlMapping *
block_to_yaml (LrgBlock *b)
{
	YamlMapping *m = yaml_mapping_new ();
	guint        n = b->kind->slots->len;
	guint        i;

	yaml_mapping_set_string_member (m, "kind", b->kind->id);
	if (n > 0)
	{
		YamlMapping *values = yaml_mapping_new ();
		YamlMapping *inputs = NULL;

		for (i = 0; i < n; i++)
		{
			set_text (values, kind_slot (b->kind, i)->name, b->values[i]);
			if (b->inputs[i] != NULL)
			{
				YamlMapping *im = block_to_yaml (b->inputs[i]);

				if (inputs == NULL)
					inputs = yaml_mapping_new ();
				yaml_mapping_set_mapping_member (inputs, kind_slot (b->kind, i)->name, im);
				yaml_mapping_unref (im);
			}
		}
		yaml_mapping_set_mapping_member (m, "values", values);
		yaml_mapping_unref (values);
		if (inputs != NULL)
		{
			yaml_mapping_set_mapping_member (m, "inputs", inputs);
			yaml_mapping_unref (inputs);
		}
	}
	if (shape_mouths (b->kind->shape) > 0)
	{
		YamlSequence *mouths = yaml_sequence_new ();

		for (i = 0; i < shape_mouths (b->kind->shape); i++)
		{
			YamlSequence *chain = chain_to_yaml (b->mouths[i]);

			yaml_sequence_add_sequence_element (mouths, chain);
			yaml_sequence_unref (chain);
		}
		yaml_mapping_set_sequence_member (m, "mouths", mouths);
		yaml_sequence_unref (mouths);
	}
	return m;
}

static YamlSequence *
chain_to_yaml (LrgBlock *first)
{
	YamlSequence *seq = yaml_sequence_new ();
	LrgBlock     *c;

	for (c = first; c != NULL; c = c->next)
	{
		YamlMapping *m = block_to_yaml (c);

		yaml_sequence_add_mapping_element (seq, m);
		yaml_mapping_unref (m);
	}
	return seq;
}

static YamlMapping *
kind_to_yaml (LrgBlockKind *k)
{
	YamlMapping *m = yaml_mapping_new ();
	GEnumClass  *shapes = g_type_class_ref (LRG_TYPE_BLOCK_SHAPE);
	GEnumValue  *v = g_enum_get_value (shapes, (gint) k->shape);
	YamlMapping *choices = NULL;
	guint        i;

	yaml_mapping_set_string_member (m, "id", k->id);
	yaml_mapping_set_string_member (m, "shape", v != NULL ? v->value_nick : "statement");
	if (k->category != NULL)
		set_text (m, "category", k->category);
	set_text (m, "label", k->label);
	if (k->has_color)
	{
		g_autofree gchar *hex = color_to_hex (&k->color);

		yaml_mapping_set_string_member (m, "color", hex);
	}
	for (i = 0; i < k->slots->len; i++)
	{
		Slot         *s = kind_slot (k, i);
		YamlSequence *seq;
		guint         c;

		if (!s->custom)
			continue;
		if (choices == NULL)
			choices = yaml_mapping_new ();
		seq = yaml_sequence_new ();
		for (c = 0; s->choices[c] != NULL; c++)
			yaml_sequence_add_string_element (seq, s->choices[c]);
		yaml_mapping_set_sequence_member (choices, s->name, seq);
		yaml_sequence_unref (seq);
	}
	if (choices != NULL)
	{
		yaml_mapping_set_mapping_member (m, "choices", choices);
		yaml_mapping_unref (choices);
	}
	g_type_class_unref (shapes);
	return m;
}

gchar *
lrg_block_editor_save_yaml (LrgBlockEditor     *self,
                            LrgBlockYamlParts   parts,
                            gsize              *length,
                            GError            **error)
{
	g_autoptr(YamlGenerator) generator = NULL;
	g_autoptr(YamlNode)      root_node = NULL;
	YamlMapping             *root;
	gsize                    len = 0;
	gchar                   *data;
	guint                    i;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	root = yaml_mapping_new ();
	yaml_mapping_set_int_member (root, "version", 1);

	/* --- palette: categories, then kinds --- */
	if (parts & LRG_BLOCK_YAML_PALETTE)
	{
		YamlSequence *cats = yaml_sequence_new ();
		YamlSequence *kinds = yaml_sequence_new ();

		for (i = 0; i < self->categories->len; i++)
		{
			Category    *c = g_ptr_array_index (self->categories, i);
			YamlMapping *m = yaml_mapping_new ();

			set_text (m, "name", c->name);
			if (c->has_color)
			{
				g_autofree gchar *hex = color_to_hex (&c->color);

				yaml_mapping_set_string_member (m, "color", hex);
			}
			yaml_sequence_add_mapping_element (cats, m);
			yaml_mapping_unref (m);
		}
		for (i = 0; i < self->kinds->len; i++)
		{
			YamlMapping *m = kind_to_yaml (g_ptr_array_index (self->kinds, i));

			yaml_sequence_add_mapping_element (kinds, m);
			yaml_mapping_unref (m);
		}
		yaml_mapping_set_sequence_member (root, "categories", cats);
		yaml_mapping_set_sequence_member (root, "kinds", kinds);
		yaml_sequence_unref (cats);
		yaml_sequence_unref (kinds);
	}

	/* --- program: the view, then the scripts --- */
	if (parts & LRG_BLOCK_YAML_PROGRAM)
	{
		YamlMapping  *view = yaml_mapping_new ();
		YamlSequence *scripts = yaml_sequence_new ();

		set_num (view, "x", self->scroll_x);
		set_num (view, "y", self->scroll_y);
		set_num (view, "zoom", self->zoom);
		for (i = 0; i < self->scripts->len; i++)
		{
			LrgBlock     *top = g_ptr_array_index (self->scripts, i);
			YamlMapping  *m = yaml_mapping_new ();
			YamlSequence *chain = chain_to_yaml (top);

			set_num (m, "x", top->x);
			set_num (m, "y", top->y);
			yaml_mapping_set_sequence_member (m, "blocks", chain);
			yaml_sequence_unref (chain);
			yaml_sequence_add_mapping_element (scripts, m);
			yaml_mapping_unref (m);
		}
		yaml_mapping_set_mapping_member (root, "view", view);
		yaml_mapping_set_sequence_member (root, "scripts", scripts);
		yaml_mapping_unref (view);
		yaml_sequence_unref (scripts);
	}

	root_node = yaml_node_new_mapping (root);
	yaml_mapping_unref (root);
	generator = yaml_generator_new ();
	yaml_generator_set_root (generator, root_node);
	data = yaml_generator_to_data (generator, &len, error);
	if (data != NULL && length != NULL)
		*length = len;
	return data;
}

gboolean
lrg_block_editor_save_yaml_to_file (LrgBlockEditor     *self,
                                    LrgBlockYamlParts   parts,
                                    const gchar        *path,
                                    GError            **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(GError) local = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	data = lrg_block_editor_save_yaml (self, parts, &len, error);
	if (data == NULL)
		return FALSE;
	if (!g_file_set_contents (path, data, (gssize) len, &local))
	{
		g_set_error (error, LRG_BLOCK_EDITOR_ERROR, LRG_BLOCK_EDITOR_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * YAML: reading
 *
 * Documents are turned into free blocks and kinds first, validated as
 * they are built; the editor only changes once everything parsed.
 * ========================================================================== */

static gboolean yaml_fail (GError      **error,
                           gint          code,
                           const gchar  *format,
                           ...) G_GNUC_PRINTF (3, 4);

static gboolean
yaml_fail (GError      **error,
           gint          code,
           const gchar  *format,
           ...)
{
	g_autofree gchar *msg = NULL;
	va_list           args;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error (error, LRG_BLOCK_EDITOR_ERROR, code, "block editor YAML: %s", msg);
	return FALSE;
}

/* node_text: a scalar's text ("" for null); FALSE for collections */
static gboolean
node_text (YamlNode     *node,
           const gchar **out)
{
	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
	{
		*out = "";
		return TRUE;
	}
	if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	*out = yaml_node_get_scalar (node);
	if (*out == NULL)
		*out = "";
	return TRUE;
}

/* member_text: a scalar member's text, %NULL when absent or not a scalar */
static const gchar *
member_text (YamlMapping *m,
             const gchar *key)
{
	const gchar *text = NULL;

	if (!yaml_mapping_has_member (m, key))
		return NULL;
	if (!node_text (yaml_mapping_get_member (m, key), &text))
		return NULL;
	return text;
}

static gboolean
member_number (YamlMapping *m,
               const gchar *key,
               gdouble      fallback,
               gdouble     *out)
{
	const gchar *text;

	if (!yaml_mapping_has_member (m, key))
	{
		*out = fallback;
		return TRUE;
	}
	text = member_text (m, key);
	return parse_number (text, out);
}

/* member_kind: a member that must be a mapping / sequence if present */
static YamlNode *
member_of_type (YamlMapping  *m,
                const gchar  *key,
                YamlNodeType  type)
{
	YamlNode *node;

	if (!yaml_mapping_has_member (m, key))
		return NULL;
	node = yaml_mapping_get_member (m, key);
	if (node == NULL || yaml_node_get_node_type (node) != type)
		return NULL;
	return node;
}

static LrgBlock *build_chain (GHashTable  *kinds,
                              YamlNode    *node,
                              gboolean     nested,
                              GError     **error);

/* build_block: one block mapping into a free block */
static LrgBlock *
build_block (GHashTable  *kinds,
             YamlNode    *node,
             GError     **error)
{
	g_autoptr(LrgBlock) b = NULL;
	YamlMapping        *m;
	YamlNode           *sub;
	LrgBlockKind       *kind;
	const gchar        *id;
	guint               i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "a block must be a mapping");
		return NULL;
	}
	m = yaml_node_get_mapping (node);
	id = member_text (m, "kind");
	if (id == NULL || *id == '\0')
	{
		yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "a block needs a kind");
		return NULL;
	}
	kind = g_hash_table_lookup (kinds, id);
	if (kind == NULL)
	{
		yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_UNKNOWN_KIND, "unknown block kind \"%s\"", id);
		return NULL;
	}
	b = lrg_block_new (kind);

	/* --- slot values --- */
	if (yaml_mapping_has_member (m, "values"))
	{
		YamlMapping *values;

		sub = member_of_type (m, "values", YAML_NODE_MAPPING);
		if (sub == NULL)
		{
			yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "values of \"%s\" must be a mapping", id);
			return NULL;
		}
		values = yaml_node_get_mapping (sub);
		for (i = 0; i < yaml_mapping_get_size (values); i++)
		{
			const gchar *name = yaml_mapping_get_key (values, i);
			gint         slot = lrg_block_kind_find_slot (kind, name);
			const gchar *text;

			if (slot < 0)
			{
				yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "\"%s\" has no slot \"%s\"", id, name);
				return NULL;
			}
			if (!node_text (yaml_mapping_get_member (values, name), &text))
			{
				yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "value \"%s\" of \"%s\" is not a scalar", name, id);
				return NULL;
			}
			g_free (b->values[slot]);
			b->values[slot] = g_strdup (text);
		}
	}

	/* --- nested reporters and booleans --- */
	if (yaml_mapping_has_member (m, "inputs"))
	{
		YamlMapping *inputs;

		sub = member_of_type (m, "inputs", YAML_NODE_MAPPING);
		if (sub == NULL)
		{
			yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "inputs of \"%s\" must be a mapping", id);
			return NULL;
		}
		inputs = yaml_node_get_mapping (sub);
		for (i = 0; i < yaml_mapping_get_size (inputs); i++)
		{
			const gchar *name = yaml_mapping_get_key (inputs, i);
			gint         slot = lrg_block_kind_find_slot (kind, name);
			LrgBlock    *child;

			if (slot < 0)
			{
				yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "\"%s\" has no slot \"%s\"", id, name);
				return NULL;
			}
			child = build_block (kinds, yaml_mapping_get_member (inputs, name), error);
			if (child == NULL)
				return NULL;
			if (!slot_accepts (kind_slot (kind, (guint) slot)->type, child->kind->shape))
			{
				yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "\"%s\" does not fit slot \"%s\" of \"%s\"",
				           child->kind->id, name, id);
				g_object_unref (child);
				return NULL;
			}
			link_at (b, LRG_BLOCK_PLACE_INPUT, (guint) slot, child);
		}
	}

	/* --- stacks in the mouths --- */
	if (yaml_mapping_has_member (m, "mouths"))
	{
		YamlSequence *mouths;

		sub = member_of_type (m, "mouths", YAML_NODE_SEQUENCE);
		if (sub == NULL)
		{
			yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "mouths of \"%s\" must be a sequence", id);
			return NULL;
		}
		mouths = yaml_node_get_sequence (sub);
		if (yaml_sequence_get_length (mouths) > shape_mouths (kind->shape))
		{
			yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "\"%s\" has %u mouth(s)", id,
			           shape_mouths (kind->shape));
			return NULL;
		}
		for (i = 0; i < yaml_sequence_get_length (mouths); i++)
		{
			g_autoptr(GError) local = NULL;
			LrgBlock         *chain = build_chain (kinds, yaml_sequence_get_element (mouths, i), TRUE, &local);

			if (local != NULL)
			{
				g_propagate_error (error, g_steal_pointer (&local));
				return NULL;
			}
			if (chain != NULL)
				link_at (b, LRG_BLOCK_PLACE_MOUTH, i, chain);
		}
	}
	return g_steal_pointer (&b);
}

/*
 * build_chain:
 *
 * A sequence of blocks hung one under the other. In a mouth (@nested)
 * every block needs a notch on top; in a script only the blocks after
 * the first do. Values only stand alone, as a script of one block.
 * Returns %NULL for an empty sequence (and on errors, with @error set).
 */
static LrgBlock *
build_chain (GHashTable  *kinds,
             YamlNode    *node,
             gboolean     nested,
             GError     **error)
{
	YamlSequence *seq;
	LrgBlock     *first = NULL;
	LrgBlock     *prev = NULL;
	guint         n;
	guint         i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "a stack must be a sequence of blocks");
		return NULL;
	}
	seq = yaml_node_get_sequence (node);
	n = yaml_sequence_get_length (seq);
	for (i = 0; i < n; i++)
	{
		LrgBlock     *b = build_block (kinds, yaml_sequence_get_element (seq, i), error);
		LrgBlockShape s;

		if (b == NULL)
			goto fail;
		s = b->kind->shape;
		if ((shape_is_value (s) && (nested || n > 1)) ||
		    ((nested || i > 0) && !shape_has_top (s)) ||
		    (prev != NULL && !shape_has_bottom (prev->kind->shape)))
		{
			yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "\"%s\" cannot be at position %u of a stack",
			           b->kind->id, i);
			g_object_unref (b);
			goto fail;
		}
		if (prev == NULL)
			first = b;
		else
			link_at (prev, LRG_BLOCK_PLACE_NEXT, 0, b);
		prev = b;
	}
	return first;

fail:
	g_clear_object (&first);
	return NULL;
}

typedef struct
{
	LrgBlock *top;
	gfloat    x;
	gfloat    y;
} ScriptSpec;

static void
script_spec_clear (gpointer data)
{
	ScriptSpec *s = data;

	g_clear_object (&s->top);
}

/* parse_scripts: the `scripts:` sequence into free trees */
static gboolean
parse_scripts (GHashTable  *kinds,
               YamlNode    *node,
               GArray      *out,
               GError     **error)
{
	YamlSequence *seq;
	guint         i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "scripts must be a sequence");
	seq = yaml_node_get_sequence (node);
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		YamlNode   *item = yaml_sequence_get_element (seq, i);
		YamlMapping *m;
		ScriptSpec  spec;
		gdouble     x;
		gdouble     y;

		if (item == NULL || yaml_node_get_node_type (item) != YAML_NODE_MAPPING)
			return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "script %u must be a mapping", i);
		m = yaml_node_get_mapping (item);
		if (!member_number (m, "x", 0.0, &x) || !member_number (m, "y", 0.0, &y))
			return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "bad position of script %u", i);
		spec.top = build_chain (kinds, member_of_type (m, "blocks", YAML_NODE_SEQUENCE), FALSE, error);
		if (spec.top == NULL)
		{
			if (error != NULL && *error == NULL)
				yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "script %u has no blocks", i);
			return FALSE;
		}
		spec.x = (gfloat) x;
		spec.y = (gfloat) y;
		g_array_append_val (out, spec);
	}
	return TRUE;
}

/* parse_palette: `categories:` and `kinds:` into new objects */
static gboolean
parse_palette (YamlMapping  *root,
               GPtrArray    *cats,
               GPtrArray    *kinds,
               GHashTable   *by_id,
               GError      **error)
{
	GEnumClass *shapes;
	YamlNode   *node;
	gboolean    ok = TRUE;
	guint       i;

	/* --- categories --- */
	if (yaml_mapping_has_member (root, "categories"))
	{
		YamlSequence *seq;

		node = member_of_type (root, "categories", YAML_NODE_SEQUENCE);
		if (node == NULL)
			return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "categories must be a sequence");
		seq = yaml_node_get_sequence (node);
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlNode    *item = yaml_sequence_get_element (seq, i);
			YamlMapping *m;
			Category    *c;
			const gchar *name;
			const gchar *hex;

			if (item == NULL || yaml_node_get_node_type (item) != YAML_NODE_MAPPING)
				return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "category %u must be a mapping", i);
			m = yaml_node_get_mapping (item);
			name = member_text (m, "name");
			if (name == NULL)
				return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "category %u needs a name", i);
			c = g_new0 (Category, 1);
			c->name = g_strdup (name);
			c->pal_y = -1.0f;
			g_ptr_array_add (cats, c);
			hex = member_text (m, "color");
			if (hex != NULL)
			{
				if (!hex_to_color (hex, &c->color))
					return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "bad colour of category \"%s\"", name);
				c->has_color = TRUE;
			}
		}
	}

	/* --- kinds --- */
	if (!yaml_mapping_has_member (root, "kinds"))
		return TRUE;
	node = member_of_type (root, "kinds", YAML_NODE_SEQUENCE);
	if (node == NULL)
		return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "kinds must be a sequence");
	shapes = g_type_class_ref (LRG_TYPE_BLOCK_SHAPE);
	for (i = 0; ok && i < yaml_sequence_get_length (yaml_node_get_sequence (node)); i++)
	{
		YamlNode     *item = yaml_sequence_get_element (yaml_node_get_sequence (node), i);
		YamlMapping  *m;
		LrgBlockKind *kind;
		GEnumValue   *shape;
		const gchar  *id;
		const gchar  *label;
		const gchar  *hex;
		YamlNode     *choices;

		if (item == NULL || yaml_node_get_node_type (item) != YAML_NODE_MAPPING)
		{
			ok = yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "kind %u must be a mapping", i);
			break;
		}
		m = yaml_node_get_mapping (item);
		id = member_text (m, "id");
		label = member_text (m, "label");
		shape = g_enum_get_value_by_nick (shapes, member_text (m, "shape") != NULL ? member_text (m, "shape")
		                                                                          : "statement");
		if (id == NULL || *id == '\0' || label == NULL || shape == NULL)
		{
			ok = yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "kind %u needs an id, a label and a known shape", i);
			break;
		}
		if (g_hash_table_contains (by_id, id))
		{
			ok = yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "duplicate kind \"%s\"", id);
			break;
		}
		kind = lrg_block_kind_new (id, (LrgBlockShape) shape->value, member_text (m, "category"), label, error);
		if (kind == NULL)
		{
			ok = FALSE;
			break;
		}
		g_ptr_array_add (kinds, kind);
		g_hash_table_insert (by_id, kind->id, kind);

		hex = member_text (m, "color");
		if (hex != NULL)
		{
			if (!hex_to_color (hex, &kind->color))
			{
				ok = yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "bad colour of kind \"%s\"", id);
				break;
			}
			kind->has_color = TRUE;
		}

		/* --- dropdown choices replaced after the label --- */
		choices = member_of_type (m, "choices", YAML_NODE_MAPPING);
		if (choices != NULL)
		{
			YamlMapping *cm = yaml_node_get_mapping (choices);
			guint        c;

			for (c = 0; ok && c < yaml_mapping_get_size (cm); c++)
			{
				const gchar         *name = yaml_mapping_get_key (cm, c);
				gint                 slot = lrg_block_kind_find_slot (kind, name);
				YamlNode            *list = yaml_mapping_get_member (cm, name);
				g_autoptr(GPtrArray) strv = g_ptr_array_new ();
				guint                e;

				if (slot < 0 || kind_slot (kind, (guint) slot)->type != LRG_BLOCK_SLOT_DROPDOWN ||
				    list == NULL || yaml_node_get_node_type (list) != YAML_NODE_SEQUENCE ||
				    yaml_sequence_get_length (yaml_node_get_sequence (list)) == 0)
				{
					ok = yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID, "bad choices for \"%s\" of \"%s\"",
					                name, id);
					break;
				}
				for (e = 0; e < yaml_sequence_get_length (yaml_node_get_sequence (list)); e++)
				{
					const gchar *text;

					if (!node_text (yaml_sequence_get_element (yaml_node_get_sequence (list), e), &text))
						text = "";
					g_ptr_array_add (strv, (gpointer) text);
				}
				g_ptr_array_add (strv, NULL);
				lrg_block_kind_set_slot_choices (kind, (guint) slot, (const gchar * const *) strv->pdata);
			}
		}
	}
	g_type_class_unref (shapes);
	return ok;
}

gboolean
lrg_block_editor_load_yaml (LrgBlockEditor  *self,
                            const gchar     *data,
                            gssize           length,
                            GError         **error)
{
	g_autoptr(YamlParser)  parser = NULL;
	g_autoptr(GError)      local = NULL;
	g_autoptr(GPtrArray)   cats = g_ptr_array_new_with_free_func (category_free);
	g_autoptr(GPtrArray)   kinds = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GHashTable)  by_id = g_hash_table_new (g_str_hash, g_str_equal);
	g_autoptr(GArray)      scripts = g_array_new (FALSE, TRUE, sizeof (ScriptSpec));
	YamlNode              *root_node;
	YamlMapping           *root;
	GHashTable            *table;
	gboolean               has_palette;
	gboolean               has_scripts = FALSE;
	gboolean               has_view = FALSE;
	gdouble                version;
	gdouble                vx = 0.0;
	gdouble                vy = 0.0;
	gdouble                vz = 1.0;
	guint                  i;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_array_set_clear_func (scripts, script_spec_clear);

	/* --- the document --- */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
		return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "%s", local->message);
	root_node = yaml_parser_get_root (parser);
	if (root_node == NULL || yaml_node_get_node_type (root_node) != YAML_NODE_MAPPING)
		return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "the document is not a mapping");
	root = yaml_node_get_mapping (root_node);
	if (!member_number (root, "version", 1.0, &version) || version != 1.0)
		return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "unsupported version");

	/* --- palette --- */
	has_palette = yaml_mapping_has_member (root, "kinds") || yaml_mapping_has_member (root, "categories");
	if (has_palette && !parse_palette (root, cats, kinds, by_id, error))
		return FALSE;
	table = has_palette ? by_id : self->kind_by_id;

	/* --- program: from the document, else the current one rebuilt on
	 * the new palette (it must still fit) --- */
	if (yaml_mapping_has_member (root, "scripts"))
	{
		has_scripts = TRUE;
		if (!parse_scripts (table, yaml_mapping_get_member (root, "scripts"), scripts, error))
			return FALSE;
	}
	else if (has_palette && self->scripts->len > 0)
	{
		g_autofree gchar     *program = NULL;
		g_autoptr(YamlParser) again = yaml_parser_new ();
		YamlMapping          *proot;

		finish_interaction (self);
		program = lrg_block_editor_save_yaml (self, LRG_BLOCK_YAML_PROGRAM, NULL, error);
		if (program == NULL || !yaml_parser_load_from_data (again, program, -1, error))
			return FALSE;
		proot = yaml_node_get_mapping (yaml_parser_get_root (again));
		if (!parse_scripts (table, yaml_mapping_get_member (proot, "scripts"), scripts, &local))
			return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_INVALID,
			                  "the program does not fit the new palette: %s", local->message);
		has_scripts = TRUE;
	}

	/* --- view --- */
	if (yaml_mapping_has_member (root, "view"))
	{
		YamlNode    *node = member_of_type (root, "view", YAML_NODE_MAPPING);
		YamlMapping *v;

		if (node == NULL)
			return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "view must be a mapping");
		v = yaml_node_get_mapping (node);
		if (!member_number (v, "x", 0.0, &vx) || !member_number (v, "y", 0.0, &vy) ||
		    !member_number (v, "zoom", 1.0, &vz) || vz <= 0.0)
			return yaml_fail (error, LRG_BLOCK_EDITOR_ERROR_PARSE, "bad view");
		has_view = TRUE;
	}

	/* --- everything parsed: apply --- */
	finish_interaction (self);
	if (has_palette || has_scripts)
		drop_program (self);
	if (has_palette)
	{
		drop_palette (self);
		for (i = 0; i < cats->len; i++)
		{
			Category *c = g_ptr_array_index (cats, i);

			lrg_block_editor_add_category (self, c->name, c->has_color ? &c->color : NULL);
		}
		for (i = 0; i < kinds->len; i++)
			lrg_block_editor_add_kind (self, g_ptr_array_index (kinds, i));
	}
	for (i = 0; i < scripts->len; i++)
	{
		ScriptSpec *spec = &g_array_index (scripts, ScriptSpec, i);

		link_script (self, g_steal_pointer (&spec->top), spec->x, spec->y);
		adopt_tree (self, g_ptr_array_index (self->scripts, self->scripts->len - 1));
	}
	if (has_view)
	{
		gfloat oz = self->zoom;
		gfloat ox = self->scroll_x;
		gfloat oy = self->scroll_y;

		self->zoom = CLAMP ((gfloat) vz, self->min_zoom, self->max_zoom);
		self->scroll_x = (gfloat) vx;
		self->scroll_y = (gfloat) vy;
		notify_view (self, oz, ox, oy);
	}
	lrg_block_editor_clear_history (self);
	commit (self);
	return TRUE;
}

gboolean
lrg_block_editor_load_yaml_from_file (LrgBlockEditor  *self,
                                      const gchar     *path,
                                      GError         **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(GError) local = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents (path, &data, &len, &local))
	{
		g_set_error (error, LRG_BLOCK_EDITOR_ERROR, LRG_BLOCK_EDITOR_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return lrg_block_editor_load_yaml (self, data, (gssize) len, error);
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

gfloat
lrg_block_editor_get_grid_size (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 24.0f);
	return self->grid_size;
}

void
lrg_block_editor_set_grid_size (LrgBlockEditor *self,
                                gfloat          size)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	size = MAX (2.0f, size);
	if (self->grid_size == size)
		return;
	self->grid_size = size;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_SIZE]);
}

gboolean
lrg_block_editor_get_show_grid (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), TRUE);
	return self->show_grid;
}

void
lrg_block_editor_set_show_grid (LrgBlockEditor *self,
                                gboolean        show)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	show = !!show;
	if (self->show_grid == show)
		return;
	self->show_grid = show;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_GRID]);
}

gboolean
lrg_block_editor_get_show_palette (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), TRUE);
	return self->show_palette;
}

void
lrg_block_editor_set_show_palette (LrgBlockEditor *self,
                                   gboolean        show)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	show = !!show;
	if (self->show_palette == show)
		return;
	self->show_palette = show;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_PALETTE]);
}

gfloat
lrg_block_editor_get_palette_width (LrgBlockEditor *self)
{
	g_return_val_if_fail (LRG_IS_BLOCK_EDITOR (self), 220.0f);
	return self->palette_width;
}

void
lrg_block_editor_set_palette_width (LrgBlockEditor *self,
                                    gfloat          width)
{
	g_return_if_fail (LRG_IS_BLOCK_EDITOR (self));

	width = MAX (40.0f, width);
	if (self->palette_width == width)
		return;
	self->palette_width = width;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PALETTE_WIDTH]);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_block_editor_dispose (GObject *object)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (object);
	GHashTableIter  iter;
	gpointer        key;
	guint           i;

	/* Silent teardown: no signals while being destroyed */
	if (self->menu != NULL && lrg_menu_get_open (self->menu))
		lrg_menu_close (self->menu);
	g_clear_object (&self->menu);
	if (self->blocks != NULL)
	{
		g_hash_table_iter_init (&iter, self->blocks);
		while (g_hash_table_iter_next (&iter, &key, NULL))
		{
			((LrgBlock *) key)->editor = NULL;
			((LrgBlock *) key)->selected = FALSE;
		}
		g_hash_table_remove_all (self->blocks);
	}
	for (i = 0; self->kinds != NULL && i < self->kinds->len; i++)
		((LrgBlockKind *) g_ptr_array_index (self->kinds, i))->editor = NULL;
	self->selected = NULL;
	self->edit_block = NULL;
	self->menu_block = NULL;
	self->press_block = NULL;
	self->press_kind = NULL;
	self->last_click = NULL;
	self->target = NULL;
	self->target_top = NULL;
	self->drag = DRAG_NONE;
	g_clear_object (&self->carried);
	g_clear_pointer (&self->drag_snapshot, snapshot_free);
	g_clear_pointer (&self->edit_snapshot, snapshot_free);
	if (self->undo != NULL)
		g_ptr_array_set_size (self->undo, 0);
	if (self->redo != NULL)
		g_ptr_array_set_size (self->redo, 0);
	if (self->scripts != NULL)
		g_ptr_array_set_size (self->scripts, 0);
	if (self->palette != NULL)
		g_ptr_array_set_size (self->palette, 0);
	if (self->pending != NULL)
	{
		for (i = 0; i < self->pending->len; i++)
			g_object_unref (g_array_index (self->pending, Pending, i).block);
		g_array_set_size (self->pending, 0);
	}
	g_clear_object (&self->clip);
	g_clear_object (&self->actions);

	G_OBJECT_CLASS (lrg_block_editor_parent_class)->dispose (object);
}

static void
lrg_block_editor_finalize (GObject *object)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (object);

	g_clear_pointer (&self->kinds, g_ptr_array_unref);
	g_clear_pointer (&self->kind_by_id, g_hash_table_unref);
	g_clear_pointer (&self->categories, g_ptr_array_unref);
	g_clear_pointer (&self->palette, g_ptr_array_unref);
	g_clear_pointer (&self->scripts, g_ptr_array_unref);
	g_clear_pointer (&self->blocks, g_hash_table_unref);
	g_clear_pointer (&self->pending, g_array_unref);
	g_clear_pointer (&self->undo, g_ptr_array_unref);
	g_clear_pointer (&self->redo, g_ptr_array_unref);
	g_clear_pointer (&self->drawn, g_ptr_array_unref);
	if (self->edit_text != NULL)
		g_string_free (self->edit_text, TRUE);
	G_OBJECT_CLASS (lrg_block_editor_parent_class)->finalize (object);
}

static void
lrg_block_editor_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (object);

	switch (prop_id)
	{
	case PROP_ZOOM:
		g_value_set_float (value, self->zoom);
		break;
	case PROP_SCROLL_X:
		g_value_set_float (value, self->scroll_x);
		break;
	case PROP_SCROLL_Y:
		g_value_set_float (value, self->scroll_y);
		break;
	case PROP_MIN_ZOOM:
		g_value_set_float (value, self->min_zoom);
		break;
	case PROP_MAX_ZOOM:
		g_value_set_float (value, self->max_zoom);
		break;
	case PROP_GRID_SIZE:
		g_value_set_float (value, self->grid_size);
		break;
	case PROP_SHOW_GRID:
		g_value_set_boolean (value, self->show_grid);
		break;
	case PROP_SHOW_PALETTE:
		g_value_set_boolean (value, self->show_palette);
		break;
	case PROP_PALETTE_WIDTH:
		g_value_set_float (value, self->palette_width);
		break;
	case PROP_UNDO_LIMIT:
		g_value_set_uint (value, self->undo_limit);
		break;
	case PROP_CAN_UNDO:
		g_value_set_boolean (value, self->undo->len > 0);
		break;
	case PROP_CAN_REDO:
		g_value_set_boolean (value, self->redo->len > 0);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_block_editor_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgBlockEditor *self = LRG_BLOCK_EDITOR (object);

	switch (prop_id)
	{
	case PROP_ZOOM:
		lrg_block_editor_set_zoom (self, g_value_get_float (value));
		break;
	case PROP_SCROLL_X:
		lrg_block_editor_set_scroll (self, g_value_get_float (value), self->scroll_y);
		break;
	case PROP_SCROLL_Y:
		lrg_block_editor_set_scroll (self, self->scroll_x, g_value_get_float (value));
		break;
	case PROP_MIN_ZOOM:
		lrg_block_editor_set_zoom_range (self, g_value_get_float (value),
		                                 MAX (self->max_zoom, g_value_get_float (value)));
		break;
	case PROP_MAX_ZOOM:
		lrg_block_editor_set_zoom_range (self, MIN (self->min_zoom, g_value_get_float (value)),
		                                 g_value_get_float (value));
		break;
	case PROP_GRID_SIZE:
		lrg_block_editor_set_grid_size (self, g_value_get_float (value));
		break;
	case PROP_SHOW_GRID:
		lrg_block_editor_set_show_grid (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_PALETTE:
		lrg_block_editor_set_show_palette (self, g_value_get_boolean (value));
		break;
	case PROP_PALETTE_WIDTH:
		lrg_block_editor_set_palette_width (self, g_value_get_float (value));
		break;
	case PROP_UNDO_LIMIT:
		lrg_block_editor_set_undo_limit (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_block_editor_class_init (LrgBlockEditorClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_block_editor_dispose;
	object_class->finalize = lrg_block_editor_finalize;
	object_class->get_property = lrg_block_editor_get_property;
	object_class->set_property = lrg_block_editor_set_property;

	widget_class->measure = lrg_block_editor_measure;
	widget_class->paint = lrg_block_editor_paint;
	widget_class->step = lrg_block_editor_step;
	widget_class->style_defaults = lrg_block_editor_style_defaults;
	widget_class->handle_event = lrg_block_editor_handle_event;

	/* The editor mirrors only its palette side; workspace coordinates
	 * are absolute. Tab moves between a block's slots. */
	lrg_widget_class_set_handles_direction (widget_class, TRUE);
	lrg_widget_class_set_accepts_tab (widget_class, TRUE);

	/**
	 * LrgBlockEditor:zoom:
	 *
	 * Screen units per workspace unit. Setting it keeps the
	 * workspace's top-left corner in place.
	 */
	properties[PROP_ZOOM] =
		g_param_spec_float ("zoom", "Zoom", "Screen units per workspace unit", 0.01f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:scroll-x:
	 *
	 * Workspace x shown at the workspace's left edge.
	 */
	properties[PROP_SCROLL_X] =
		g_param_spec_float ("scroll-x", "Scroll X", "Workspace x at the left edge", -G_MAXFLOAT, G_MAXFLOAT,
		                    0.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:scroll-y:
	 *
	 * Workspace y shown at the workspace's top edge.
	 */
	properties[PROP_SCROLL_Y] =
		g_param_spec_float ("scroll-y", "Scroll Y", "Workspace y at the top edge", -G_MAXFLOAT, G_MAXFLOAT,
		                    0.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:min-zoom:
	 *
	 * Smallest zoom (default 0.3).
	 */
	properties[PROP_MIN_ZOOM] =
		g_param_spec_float ("min-zoom", "Min zoom", "Smallest zoom", 0.01f, 100.0f, 0.3f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:max-zoom:
	 *
	 * Largest zoom (default 3).
	 */
	properties[PROP_MAX_ZOOM] =
		g_param_spec_float ("max-zoom", "Max zoom", "Largest zoom", 0.01f, 100.0f, 3.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:grid-size:
	 *
	 * Spacing of the workspace dots in workspace units.
	 */
	properties[PROP_GRID_SIZE] =
		g_param_spec_float ("grid-size", "Grid size", "Dot spacing", 2.0f, G_MAXFLOAT, 24.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:show-grid:
	 *
	 * Draw the workspace dot grid.
	 */
	properties[PROP_SHOW_GRID] =
		g_param_spec_boolean ("show-grid", "Show grid", "Draw the dot grid", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:show-palette:
	 *
	 * Show the palette and its category strip (on the left, on the
	 * right in right-to-left layouts).
	 */
	properties[PROP_SHOW_PALETTE] =
		g_param_spec_boolean ("show-palette", "Show palette", "Show the block palette", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:palette-width:
	 *
	 * Width of the palette's block list (the category strip adds 64).
	 */
	properties[PROP_PALETTE_WIDTH] =
		g_param_spec_float ("palette-width", "Palette width", "Width of the block list", 40.0f, G_MAXFLOAT,
		                    220.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:undo-limit:
	 *
	 * The most undo steps kept.
	 */
	properties[PROP_UNDO_LIMIT] =
		g_param_spec_uint ("undo-limit", "Undo limit", "Most undo steps kept", 1, G_MAXUINT, 200,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:can-undo:
	 *
	 * Whether lrg_block_editor_undo() would do something.
	 */
	properties[PROP_CAN_UNDO] =
		g_param_spec_boolean ("can-undo", "Can undo", "There is an edit to undo", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlockEditor:can-redo:
	 *
	 * Whether lrg_block_editor_redo() would do something.
	 */
	properties[PROP_CAN_REDO] =
		g_param_spec_boolean ("can-redo", "Can redo", "There is an edit to redo", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgBlockEditor::changed:
	 * @self: the editor
	 *
	 * The program changed: after every edit (by the user or the API),
	 * undo, redo and loads. Emitted once per edit, after the
	 * #LrgBlockEditor::block-added / #LrgBlockEditor::block-removed
	 * emissions of that edit.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgBlockEditor::block-added:
	 * @self: the editor
	 * @block: a block that joined the program
	 *
	 * Emitted for every block of an added tree (a palette drag, a paste,
	 * a load, an undo that brings blocks back).
	 */
	signals[SIGNAL_BLOCK_ADDED] =
		g_signal_new ("block-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_BLOCK);

	/**
	 * LrgBlockEditor::block-removed:
	 * @self: the editor
	 * @block: a block that left the program
	 *
	 * Emitted for every block of a removed tree. The block is still
	 * valid during the emission and may come back with undo.
	 */
	signals[SIGNAL_BLOCK_REMOVED] =
		g_signal_new ("block-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_BLOCK);

	/**
	 * LrgBlockEditor::block-activated:
	 * @self: the editor
	 * @top: the top block of the script to run
	 *
	 * Double click on a script (usually on its hat), or Enter with a
	 * block of it selected: the application should run the script.
	 */
	signals[SIGNAL_BLOCK_ACTIVATED] =
		g_signal_new ("block-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_BLOCK);

	/**
	 * LrgBlockEditor::selection-changed:
	 * @self: the editor
	 *
	 * The selected block changed.
	 */
	signals[SIGNAL_SELECTION_CHANGED] =
		g_signal_new ("selection-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_block_editor_init (LrgBlockEditor *self)
{
	self->kinds = g_ptr_array_new_with_free_func (g_object_unref);
	self->kind_by_id = g_hash_table_new (g_str_hash, g_str_equal);
	self->categories = g_ptr_array_new_with_free_func (category_free);
	self->palette = g_ptr_array_new_with_free_func (g_object_unref);
	self->scripts = g_ptr_array_new_with_free_func (g_object_unref);
	self->blocks = g_hash_table_new (NULL, NULL);
	self->pending = g_array_new (FALSE, FALSE, sizeof (Pending));
	self->undo = g_ptr_array_new_with_free_func ((GDestroyNotify) snapshot_free);
	self->redo = g_ptr_array_new_with_free_func ((GDestroyNotify) snapshot_free);
	self->drawn = g_ptr_array_new ();
	self->edit_text = g_string_new (NULL);
	self->undo_limit = 200;
	self->zoom = 1.0f;
	self->min_zoom = 0.3f;
	self->max_zoom = 3.0f;
	self->grid_size = 24.0f;
	self->show_grid = TRUE;
	self->show_palette = TRUE;
	self->palette_width = 220.0f;
	self->press_slot = -1;
	self->m_fs = -1.0f;
	self->layout_dirty = TRUE;
	lrg_spring_init (&self->pal_scroll, 0.0f, LRG_MOTION_SMOOTH);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	install_actions (self);
}
