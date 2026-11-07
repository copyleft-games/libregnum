/* lrg-shader-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgShaderAsset: user shaders (`.rshader`).
 *
 * A shader asset is a description (name, lit flag, vertex / fragment
 * sources given inline or by path, described uniforms, render state
 * defaults) plus, once compiled, a GL program.  The description lives in
 * a ShaderDesc so that hot reload can parse a complete new one, compile
 * it, and only then swap it in: keep-last-good falls out of never
 * touching the live description until everything has succeeded.
 *
 * Stage text handed to the driver is "preamble + user source".  The
 * preamble declares the engine contract (vertex attributes, matrices,
 * varyings, `finalColor`, `lrgTime`), the lit library for lit fragment
 * stages, and the described uniforms; any of those the user source or
 * the library already declares is left out, so ported raylib shaders
 * and the real light-set library both work.  The preamble's line count
 * maps the driver's line numbers back to the user's.
 *
 * Diagnostics: the stages are first compiled and linked with GL entry
 * points resolved through rlGetProcAddress() so that the info logs can
 * be read (raylib only prints them); on success the GrlShader is built
 * by raylib from the same text (a driver shader cache makes the second
 * compile cheap), which keeps raylib's attribute bindings and location
 * table.
 */

#include "config.h"

#include <string.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <yaml-glib.h>

#include "lrg-shader-asset.h"
#include "lrg-light-set.h"
#include "../core/lrg-data-loader.h"
#include "../lrg-log.h"

/* ========================================================================== */
/* Enums and error domain                                                     */
/* ========================================================================== */

G_DEFINE_QUARK (lrg-shader-asset-error-quark, lrg_shader_asset_error)

GType
lrg_shader_asset_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SHADER_ASSET_ERROR_PARSE, "LRG_SHADER_ASSET_ERROR_PARSE", "parse" },
			{ LRG_SHADER_ASSET_ERROR_IO, "LRG_SHADER_ASSET_ERROR_IO", "io" },
			{ LRG_SHADER_ASSET_ERROR_NO_CONTEXT, "LRG_SHADER_ASSET_ERROR_NO_CONTEXT", "no-context" },
			{ LRG_SHADER_ASSET_ERROR_COMPILE, "LRG_SHADER_ASSET_ERROR_COMPILE", "compile" },
			{ LRG_SHADER_ASSET_ERROR_LINK, "LRG_SHADER_ASSET_ERROR_LINK", "link" },
			{ LRG_SHADER_ASSET_ERROR_UNIFORM, "LRG_SHADER_ASSET_ERROR_UNIFORM", "uniform" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgShaderAssetError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_shader_stage_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SHADER_STAGE_VERTEX, "LRG_SHADER_STAGE_VERTEX", "vertex" },
			{ LRG_SHADER_STAGE_FRAGMENT, "LRG_SHADER_STAGE_FRAGMENT", "fragment" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgShaderStage"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_shader_uniform_type_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SHADER_UNIFORM_FLOAT, "LRG_SHADER_UNIFORM_FLOAT", "float" },
			{ LRG_SHADER_UNIFORM_VEC2, "LRG_SHADER_UNIFORM_VEC2", "vec2" },
			{ LRG_SHADER_UNIFORM_VEC3, "LRG_SHADER_UNIFORM_VEC3", "vec3" },
			{ LRG_SHADER_UNIFORM_VEC4, "LRG_SHADER_UNIFORM_VEC4", "vec4" },
			{ LRG_SHADER_UNIFORM_COLOR, "LRG_SHADER_UNIFORM_COLOR", "color" },
			{ LRG_SHADER_UNIFORM_INT, "LRG_SHADER_UNIFORM_INT", "int" },
			{ LRG_SHADER_UNIFORM_BOOL, "LRG_SHADER_UNIFORM_BOOL", "bool" },
			{ LRG_SHADER_UNIFORM_SAMPLER2D, "LRG_SHADER_UNIFORM_SAMPLER2D", "sampler2D" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgShaderUniformType"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

guint
lrg_shader_uniform_type_get_components (LrgShaderUniformType type)
{
	switch (type)
	{
	case LRG_SHADER_UNIFORM_VEC2:
		return 2;
	case LRG_SHADER_UNIFORM_VEC3:
		return 3;
	case LRG_SHADER_UNIFORM_VEC4:
	case LRG_SHADER_UNIFORM_COLOR:
		return 4;
	case LRG_SHADER_UNIFORM_FLOAT:
	case LRG_SHADER_UNIFORM_INT:
	case LRG_SHADER_UNIFORM_BOOL:
	case LRG_SHADER_UNIFORM_SAMPLER2D:
	default:
		return 1;
	}
}

const gchar *
lrg_shader_uniform_type_get_glsl (LrgShaderUniformType type)
{
	switch (type)
	{
	case LRG_SHADER_UNIFORM_VEC2:
		return "vec2";
	case LRG_SHADER_UNIFORM_VEC3:
		return "vec3";
	case LRG_SHADER_UNIFORM_VEC4:
	case LRG_SHADER_UNIFORM_COLOR:
		return "vec4";
	case LRG_SHADER_UNIFORM_INT:
		return "int";
	case LRG_SHADER_UNIFORM_BOOL:
		return "bool";
	case LRG_SHADER_UNIFORM_SAMPLER2D:
		return "sampler2D";
	case LRG_SHADER_UNIFORM_FLOAT:
	default:
		return "float";
	}
}

/* the nick of @type, the `type:` value in documents */
static const gchar *
uniform_type_nick (LrgShaderUniformType type)
{
	GEnumClass *klass = g_type_class_ref (LRG_TYPE_SHADER_UNIFORM_TYPE);
	GEnumValue *value = g_enum_get_value (klass, (gint) type);
	const gchar *nick = value != NULL ? value->value_nick : "float";

	g_type_class_unref (klass);
	return nick;
}

/* parses a `type:` nick; FALSE when unknown */
static gboolean
uniform_type_from_nick (const gchar          *nick,
                        LrgShaderUniformType *out_type)
{
	GEnumClass *klass;
	GEnumValue *value;

	if (nick == NULL)
		return FALSE;
	klass = g_type_class_ref (LRG_TYPE_SHADER_UNIFORM_TYPE);
	value = g_enum_get_value_by_nick (klass, nick);
	if (value != NULL)
		*out_type = (LrgShaderUniformType) value->value;
	g_type_class_unref (klass);
	return value != NULL;
}

/* ========================================================================== */
/* LrgShaderUniform                                                           */
/* ========================================================================== */

struct _LrgShaderUniform
{
	gchar                *name;
	LrgShaderUniformType  type;
	gfloat                value[4];
	gboolean              has_range;
	gfloat                range_min;
	gfloat                range_max;
	gchar                *hint;
	gchar                *texture;
};

G_DEFINE_BOXED_TYPE (LrgShaderUniform, lrg_shader_uniform,
                     lrg_shader_uniform_copy, lrg_shader_uniform_free)

/* the default value a fresh uniform of @type gets */
static void
uniform_type_default (LrgShaderUniformType  type,
                      gfloat               *out)
{
	gfloat fill = type == LRG_SHADER_UNIFORM_COLOR ? 1.0f : 0.0f;

	out[0] = fill;
	out[1] = fill;
	out[2] = fill;
	out[3] = fill;
}

LrgShaderUniform *
lrg_shader_uniform_new (const gchar          *name,
                        LrgShaderUniformType  type)
{
	LrgShaderUniform *self;

	g_return_val_if_fail (name != NULL, NULL);

	self = g_new0 (LrgShaderUniform, 1);
	self->name = g_strdup (name);
	self->type = type;
	uniform_type_default (type, self->value);
	return self;
}

LrgShaderUniform *
lrg_shader_uniform_copy (const LrgShaderUniform *self)
{
	LrgShaderUniform *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = g_new0 (LrgShaderUniform, 1);
	*copy = *self;
	copy->name = g_strdup (self->name);
	copy->hint = g_strdup (self->hint);
	copy->texture = g_strdup (self->texture);
	return copy;
}

void
lrg_shader_uniform_free (LrgShaderUniform *self)
{
	if (self == NULL)
		return;
	g_free (self->name);
	g_free (self->hint);
	g_free (self->texture);
	g_free (self);
}

const gchar *
lrg_shader_uniform_get_name (const LrgShaderUniform *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->name;
}

LrgShaderUniformType
lrg_shader_uniform_get_uniform_type (const LrgShaderUniform *self)
{
	g_return_val_if_fail (self != NULL, LRG_SHADER_UNIFORM_FLOAT);
	return self->type;
}

void
lrg_shader_uniform_set_default (LrgShaderUniform *self,
                                const gfloat     *values,
                                guint             n_values)
{
	guint i;

	g_return_if_fail (self != NULL);
	g_return_if_fail (values != NULL || n_values == 0);

	if (self->type == LRG_SHADER_UNIFORM_SAMPLER2D)
		return;
	for (i = 0; i < n_values && i < 4; i++)
		self->value[i] = values[i];
}

void
lrg_shader_uniform_get_default (const LrgShaderUniform *self,
                                gfloat                 *out_values)
{
	guint n;
	guint i;

	g_return_if_fail (self != NULL);
	g_return_if_fail (out_values != NULL);

	n = lrg_shader_uniform_type_get_components (self->type);
	for (i = 0; i < 4; i++)
		out_values[i] = (i < n && self->type != LRG_SHADER_UNIFORM_SAMPLER2D) ? self->value[i] : 0.0f;
}

void
lrg_shader_uniform_set_range (LrgShaderUniform *self,
                              gfloat            min,
                              gfloat            max)
{
	g_return_if_fail (self != NULL);
	self->has_range = TRUE;
	self->range_min = min;
	self->range_max = max;
}

void
lrg_shader_uniform_clear_range (LrgShaderUniform *self)
{
	g_return_if_fail (self != NULL);
	self->has_range = FALSE;
	self->range_min = 0.0f;
	self->range_max = 0.0f;
}

gboolean
lrg_shader_uniform_get_range (const LrgShaderUniform *self,
                              gfloat                 *out_min,
                              gfloat                 *out_max)
{
	g_return_val_if_fail (self != NULL, FALSE);

	if (!self->has_range)
		return FALSE;
	if (out_min != NULL)
		*out_min = self->range_min;
	if (out_max != NULL)
		*out_max = self->range_max;
	return TRUE;
}

void
lrg_shader_uniform_set_hint (LrgShaderUniform *self,
                             const gchar      *hint)
{
	g_return_if_fail (self != NULL);
	g_free (self->hint);
	self->hint = (hint != NULL && hint[0] != '\0') ? g_strdup (hint) : NULL;
}

const gchar *
lrg_shader_uniform_get_hint (const LrgShaderUniform *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->hint;
}

void
lrg_shader_uniform_set_default_texture (LrgShaderUniform *self,
                                        const gchar      *path)
{
	g_return_if_fail (self != NULL);
	g_free (self->texture);
	self->texture = (path != NULL && path[0] != '\0') ? g_strdup (path) : NULL;
}

const gchar *
lrg_shader_uniform_get_default_texture (const LrgShaderUniform *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->texture;
}

/* ========================================================================== */
/* LrgShaderDiagnostic and info-log parsing                                   */
/* ========================================================================== */

struct _LrgShaderDiagnostic
{
	LrgShaderStage  stage;
	gchar          *source;
	guint           line;
	guint           column;
	gboolean        is_error;
	gboolean        in_preamble;
	gchar          *message;
};

G_DEFINE_BOXED_TYPE (LrgShaderDiagnostic, lrg_shader_diagnostic,
                     lrg_shader_diagnostic_copy, lrg_shader_diagnostic_free)

LrgShaderDiagnostic *
lrg_shader_diagnostic_copy (const LrgShaderDiagnostic *self)
{
	LrgShaderDiagnostic *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = g_new0 (LrgShaderDiagnostic, 1);
	*copy = *self;
	copy->source = g_strdup (self->source);
	copy->message = g_strdup (self->message);
	return copy;
}

void
lrg_shader_diagnostic_free (LrgShaderDiagnostic *self)
{
	if (self == NULL)
		return;
	g_free (self->source);
	g_free (self->message);
	g_free (self);
}

LrgShaderStage
lrg_shader_diagnostic_get_stage (const LrgShaderDiagnostic *self)
{
	g_return_val_if_fail (self != NULL, LRG_SHADER_STAGE_FRAGMENT);
	return self->stage;
}

const gchar *
lrg_shader_diagnostic_get_source (const LrgShaderDiagnostic *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->source;
}

guint
lrg_shader_diagnostic_get_line (const LrgShaderDiagnostic *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->line;
}

guint
lrg_shader_diagnostic_get_column (const LrgShaderDiagnostic *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->column;
}

gboolean
lrg_shader_diagnostic_is_error (const LrgShaderDiagnostic *self)
{
	g_return_val_if_fail (self != NULL, TRUE);
	return self->is_error;
}

gboolean
lrg_shader_diagnostic_is_in_preamble (const LrgShaderDiagnostic *self)
{
	g_return_val_if_fail (self != NULL, FALSE);
	return self->in_preamble;
}

const gchar *
lrg_shader_diagnostic_get_message (const LrgShaderDiagnostic *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->message;
}

gchar *
lrg_shader_diagnostic_to_string (const LrgShaderDiagnostic *self)
{
	const gchar *kind;

	g_return_val_if_fail (self != NULL, NULL);

	kind = self->is_error ? "error" : "warning";
	if (self->line == 0)
		return g_strdup_printf ("%s: %s: %s", self->source, kind, self->message);
	if (self->column == 0)
		return g_strdup_printf ("%s:%u: %s: %s", self->source, self->line, kind, self->message);
	return g_strdup_printf ("%s:%u:%u: %s: %s", self->source, self->line, self->column,
	                        kind, self->message);
}

/* the log line patterns of the common drivers; group names are shared */
static const gchar * const log_patterns[] = {
	/* Mesa: "0:12(5): error: message" */
	"^\\s*\\d+:(?<line>\\d+)\\((?<col>\\d+)\\):\\s*(?<kind>error|warning)\\s*:?\\s*(?<msg>.*)$",
	/* NVIDIA: "0(12) : error C1008: message" */
	"^\\s*\\d+\\((?<line>\\d+)\\)\\s*:\\s*(?<kind>error|warning)\\s*(?:[A-Z]\\d+)?\\s*:\\s*(?<msg>.*)$",
	/* AMD, Intel on Windows, Apple: "ERROR: 0:12: message" */
	"^\\s*(?<kind>ERROR|WARNING)\\s*:\\s*\\d+:(?<line>\\d+)\\s*:\\s*(?<msg>.*)$",
	NULL
};

/* a diagnostic from parsed parts, mapping the line through the preamble */
static LrgShaderDiagnostic *
diagnostic_new (LrgShaderStage  stage,
                guint           line,
                guint           column,
                gboolean        is_error,
                const gchar    *message,
                guint           preamble_lines,
                const gchar    *source_name)
{
	LrgShaderDiagnostic *diag = g_new0 (LrgShaderDiagnostic, 1);

	diag->stage = stage;
	diag->column = column;
	diag->is_error = is_error;
	diag->message = g_strdup (message);
	if (line == 0)
	{
		diag->source = g_strdup (source_name);
	}
	else if (line > preamble_lines)
	{
		diag->line = line - preamble_lines;
		diag->source = g_strdup (source_name);
	}
	else
	{
		diag->line = line;
		diag->in_preamble = TRUE;
		diag->source = g_strdup ("<preamble>");
	}
	return diag;
}

GPtrArray *
lrg_shader_parse_info_log (const gchar    *log,
                           LrgShaderStage  stage,
                           guint           preamble_lines,
                           const gchar    *source_name)
{
	GPtrArray *result;
	g_auto(GStrv) lines = NULL;
	GRegex *regexes[3] = { NULL, NULL, NULL };
	guint i;
	guint r;

	result = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_shader_diagnostic_free);
	if (log == NULL)
		return result;
	if (source_name == NULL)
		source_name = "<source>";

	for (r = 0; log_patterns[r] != NULL; r++)
		regexes[r] = g_regex_new (log_patterns[r], G_REGEX_CASELESS, 0, NULL);

	/* one diagnostic per non-empty log line */
	lines = g_strsplit (log, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		g_autofree gchar *text = g_strstrip (g_strdup (lines[i]));
		gboolean matched = FALSE;

		if (text[0] == '\0')
			continue;

		for (r = 0; r < G_N_ELEMENTS (regexes) && !matched; r++)
		{
			g_autoptr(GMatchInfo) info = NULL;

			if (regexes[r] == NULL || !g_regex_match (regexes[r], text, 0, &info))
				continue;
			{
				g_autofree gchar *line_text = g_match_info_fetch_named (info, "line");
				g_autofree gchar *col_text = g_match_info_fetch_named (info, "col");
				g_autofree gchar *kind = g_match_info_fetch_named (info, "kind");
				g_autofree gchar *msg = g_match_info_fetch_named (info, "msg");
				guint line = (guint) g_ascii_strtoull (line_text != NULL ? line_text : "0", NULL, 10);
				guint col = (guint) g_ascii_strtoull (col_text != NULL && col_text[0] != '\0' ? col_text : "0",
				                                      NULL, 10);
				gboolean is_error = kind == NULL || g_ascii_strcasecmp (kind, "warning") != 0;

				g_ptr_array_add (result, diagnostic_new (stage, line, col, is_error,
				                                         msg != NULL ? g_strstrip (msg) : "",
				                                         preamble_lines, source_name));
				matched = TRUE;
			}
		}
		if (!matched)
			g_ptr_array_add (result, diagnostic_new (stage, 0, 0, TRUE, text,
			                                         preamble_lines, source_name));
	}

	for (r = 0; r < G_N_ELEMENTS (regexes); r++)
		if (regexes[r] != NULL)
			g_regex_unref (regexes[r]);
	return result;
}

/* ========================================================================== */
/* Description                                                                */
/* ========================================================================== */

#define N_STAGES (2)

static const gchar * const blend_modes[] = { "opaque", "cutout", "alpha", "additive", "multiply", NULL };
static const gchar * const cull_modes[] = { "back", "front", "none", NULL };

/* the interned table entry equal to @text, or NULL */
static const gchar *
vocabulary_lookup (const gchar * const *table,
                   const gchar         *text)
{
	guint i;

	if (text == NULL)
		return NULL;
	for (i = 0; table[i] != NULL; i++)
		if (g_strcmp0 (table[i], text) == 0)
			return table[i];
	return NULL;
}

/*
 * ShaderDesc:
 * Everything a document says.  Owned by one asset at a time; reload
 * builds a fresh one and swaps it in only after it compiled.
 */
typedef struct
{
	gchar       *name;
	gboolean     lit;
	gchar       *source[N_STAGES];      /* user text, NULL = unset */
	gchar       *source_path[N_STAGES]; /* as written, NULL = inline */
	const gchar *blend_mode;            /* entry of blend_modes */
	const gchar *cull_mode;             /* entry of cull_modes */
	gboolean     depth_test;
	gboolean     depth_write;
	gfloat       alpha_cutoff;
	GPtrArray   *uniforms;              /* LrgShaderUniform, document order */
	YamlMapping *extra;                 /* unknown top-level keys, nullable */
} ShaderDesc;

static ShaderDesc *
shader_desc_new (void)
{
	ShaderDesc *desc = g_new0 (ShaderDesc, 1);

	desc->name = g_strdup ("untitled");
	desc->blend_mode = blend_modes[0];
	desc->cull_mode = cull_modes[0];
	desc->depth_test = TRUE;
	desc->depth_write = TRUE;
	desc->alpha_cutoff = 0.5f;
	desc->uniforms = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_shader_uniform_free);
	return desc;
}

static void
shader_desc_free (ShaderDesc *desc)
{
	guint s;

	if (desc == NULL)
		return;
	g_free (desc->name);
	for (s = 0; s < N_STAGES; s++)
	{
		g_free (desc->source[s]);
		g_free (desc->source_path[s]);
	}
	g_ptr_array_unref (desc->uniforms);
	if (desc->extra != NULL)
		yaml_mapping_unref (desc->extra);
	g_free (desc);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ShaderDesc, shader_desc_free)

static LrgShaderUniform *
shader_desc_find_uniform (ShaderDesc  *desc,
                          const gchar *name,
                          guint       *out_index)
{
	guint i;

	for (i = 0; i < desc->uniforms->len; i++)
	{
		LrgShaderUniform *u = g_ptr_array_index (desc->uniforms, i);

		if (g_strcmp0 (u->name, name) == 0)
		{
			if (out_index != NULL)
				*out_index = i;
			return u;
		}
	}
	return NULL;
}

/* ========================================================================== */
/* Object                                                                     */
/* ========================================================================== */

/* the stored value of one uniform */
typedef struct
{
	LrgShaderUniformType  type;
	gfloat                value[4];
	GrlTexture           *texture;   /* set texture (ref), nullable */
} UniformValue;

static void
uniform_value_free (UniformValue *value)
{
	if (value == NULL)
		return;
	g_clear_object (&value->texture);
	g_free (value);
}

struct _LrgShaderAsset
{
	GObject       parent_instance;

	gchar        *path;              /* document path, nullable */
	gchar        *base_dir;          /* stage paths resolve here */
	ShaderDesc   *desc;
	gchar        *library_override;  /* nullable */

	GHashTable   *values;            /* name -> UniformValue */
	gfloat        time;

	GrlShader    *shader;            /* compiled program, nullable */
	GHashTable   *locations;         /* name -> GINT_TO_POINTER (loc + 1) */
	GHashTable   *default_textures;  /* name -> GrlTexture */
	GPtrArray    *diagnostics;       /* LrgShaderDiagnostic */
	GPtrArray    *light_sets;        /* GWeakRef to LrgLightSet bound to the program */

	gboolean      watch;
	GPtrArray    *monitors;          /* GFileMonitor */
	GPtrArray    *watched_files;     /* GFile */
	GSource      *pending;           /* debounce timeout, nullable */
	GMainContext *context;           /* where reloads run */
};

G_DEFINE_FINAL_TYPE (LrgShaderAsset, lrg_shader_asset, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_PATH,
	PROP_LIT,
	PROP_COMPILED,
	PROP_WATCH,
	PROP_BLEND_MODE,
	PROP_CULL_MODE,
	PROP_DEPTH_TEST,
	PROP_DEPTH_WRITE,
	PROP_ALPHA_CUTOFF,
	PROP_TIME,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_RELOADED,
	SIGNAL_RELOAD_FAILED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void stop_watching (LrgShaderAsset *self);
static gboolean start_watching (LrgShaderAsset *self, GError **error);

static void
weak_ref_free (GWeakRef *ref)
{
	g_weak_ref_clear (ref);
	g_free (ref);
}

/* drops the program and everything derived from it; light sets that
 * cached the program's locations forget it first (its id may be reused) */
static void
clear_program (LrgShaderAsset *self)
{
	guint i;

	if (self->shader != NULL)
		for (i = 0; i < self->light_sets->len; i++)
		{
			g_autoptr(LrgLightSet) lights = g_weak_ref_get (g_ptr_array_index (self->light_sets, i));

			if (lights != NULL)
				lrg_light_set_forget_shader (lights, self->shader);
		}
	g_ptr_array_set_size (self->light_sets, 0);
	g_clear_object (&self->shader);
	g_hash_table_remove_all (self->locations);
	g_hash_table_remove_all (self->default_textures);
}

/* the stored value of @name for the live description's type, created at
 * the default when missing */
static void
reset_value (LrgShaderAsset   *self,
             LrgShaderUniform *uniform)
{
	UniformValue *value = g_new0 (UniformValue, 1);

	value->type = uniform->type;
	memcpy (value->value, uniform->value, sizeof (value->value));
	g_hash_table_replace (self->values, g_strdup (uniform->name), value);
}

/* keeps stored values whose uniform kept its name and type, resets the
 * others and drops values of removed uniforms */
static void
merge_values (LrgShaderAsset *self)
{
	g_autoptr(GHashTable) old = self->values;
	guint i;

	self->values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                      (GDestroyNotify) uniform_value_free);
	for (i = 0; i < self->desc->uniforms->len; i++)
	{
		LrgShaderUniform *u = g_ptr_array_index (self->desc->uniforms, i);
		UniformValue *previous = g_hash_table_lookup (old, u->name);

		if (previous != NULL && previous->type == u->type)
		{
			g_hash_table_steal (old, u->name);
			g_hash_table_replace (self->values, g_strdup (u->name), previous);
		}
		else
		{
			reset_value (self, u);
		}
	}
}

static void
lrg_shader_asset_dispose (GObject *object)
{
	LrgShaderAsset *self = LRG_SHADER_ASSET (object);

	stop_watching (self);
	clear_program (self);
	G_OBJECT_CLASS (lrg_shader_asset_parent_class)->dispose (object);
}

static void
lrg_shader_asset_finalize (GObject *object)
{
	LrgShaderAsset *self = LRG_SHADER_ASSET (object);

	g_free (self->path);
	g_free (self->base_dir);
	g_free (self->library_override);
	shader_desc_free (self->desc);
	g_hash_table_unref (self->values);
	g_hash_table_unref (self->locations);
	g_hash_table_unref (self->default_textures);
	g_ptr_array_unref (self->diagnostics);
	g_ptr_array_unref (self->light_sets);
	g_ptr_array_unref (self->monitors);
	g_ptr_array_unref (self->watched_files);
	if (self->context != NULL)
		g_main_context_unref (self->context);
	G_OBJECT_CLASS (lrg_shader_asset_parent_class)->finalize (object);
}

static void
lrg_shader_asset_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgShaderAsset *self = LRG_SHADER_ASSET (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, self->desc->name);
		break;
	case PROP_PATH:
		g_value_set_string (value, self->path);
		break;
	case PROP_LIT:
		g_value_set_boolean (value, self->desc->lit);
		break;
	case PROP_COMPILED:
		g_value_set_boolean (value, self->shader != NULL);
		break;
	case PROP_WATCH:
		g_value_set_boolean (value, self->watch);
		break;
	case PROP_BLEND_MODE:
		g_value_set_string (value, self->desc->blend_mode);
		break;
	case PROP_CULL_MODE:
		g_value_set_string (value, self->desc->cull_mode);
		break;
	case PROP_DEPTH_TEST:
		g_value_set_boolean (value, self->desc->depth_test);
		break;
	case PROP_DEPTH_WRITE:
		g_value_set_boolean (value, self->desc->depth_write);
		break;
	case PROP_ALPHA_CUTOFF:
		g_value_set_float (value, self->desc->alpha_cutoff);
		break;
	case PROP_TIME:
		g_value_set_float (value, self->time);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shader_asset_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgShaderAsset *self = LRG_SHADER_ASSET (object);

	switch (prop_id)
	{
	case PROP_NAME:
		lrg_shader_asset_set_name (self, g_value_get_string (value));
		break;
	case PROP_LIT:
		lrg_shader_asset_set_lit (self, g_value_get_boolean (value));
		break;
	case PROP_WATCH:
		lrg_shader_asset_set_watch (self, g_value_get_boolean (value), NULL);
		break;
	case PROP_BLEND_MODE:
		if (!lrg_shader_asset_set_blend_mode (self, g_value_get_string (value)))
			g_warning ("LrgShaderAsset: unknown blend mode '%s'", g_value_get_string (value));
		break;
	case PROP_CULL_MODE:
		if (!lrg_shader_asset_set_cull_mode (self, g_value_get_string (value)))
			g_warning ("LrgShaderAsset: unknown cull mode '%s'", g_value_get_string (value));
		break;
	case PROP_DEPTH_TEST:
		lrg_shader_asset_set_depth_test (self, g_value_get_boolean (value));
		break;
	case PROP_DEPTH_WRITE:
		lrg_shader_asset_set_depth_write (self, g_value_get_boolean (value));
		break;
	case PROP_ALPHA_CUTOFF:
		lrg_shader_asset_set_alpha_cutoff (self, g_value_get_float (value));
		break;
	case PROP_TIME:
		lrg_shader_asset_set_time (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shader_asset_class_init (LrgShaderAssetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags rw = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;
	GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS;

	object_class->dispose = lrg_shader_asset_dispose;
	object_class->finalize = lrg_shader_asset_finalize;
	object_class->get_property = lrg_shader_asset_get_property;
	object_class->set_property = lrg_shader_asset_set_property;

	/**
	 * LrgShaderAsset:name:
	 *
	 * The display name (the `name:` key).
	 */
	properties[PROP_NAME] = g_param_spec_string ("name", "Name", "Display name",
	                                             "untitled", rw);
	/**
	 * LrgShaderAsset:path:
	 *
	 * The `.rshader` file the asset was loaded from, or %NULL.
	 */
	properties[PROP_PATH] = g_param_spec_string ("path", "Path", "Document path", NULL, ro);
	/**
	 * LrgShaderAsset:lit:
	 *
	 * Whether the fragment stage gets the lit library (`lit:` key).
	 */
	properties[PROP_LIT] = g_param_spec_boolean ("lit", "Lit", "Prepend the lit library",
	                                             FALSE, rw);
	/**
	 * LrgShaderAsset:compiled:
	 *
	 * Whether a program exists.
	 */
	properties[PROP_COMPILED] = g_param_spec_boolean ("compiled", "Compiled",
	                                                  "A program exists", FALSE, ro);
	/**
	 * LrgShaderAsset:watch:
	 *
	 * Whether the document and stage files are watched for hot reload.
	 */
	properties[PROP_WATCH] = g_param_spec_boolean ("watch", "Watch", "Hot reload",
	                                               FALSE, rw);
	/**
	 * LrgShaderAsset:blend-mode:
	 *
	 * The default blend mode: `opaque`, `cutout`, `alpha`, `additive`
	 * or `multiply`.
	 */
	properties[PROP_BLEND_MODE] = g_param_spec_string ("blend-mode", "Blend mode",
	                                                   "Default blend mode", "opaque", rw);
	/**
	 * LrgShaderAsset:cull-mode:
	 *
	 * The default face culling: `back`, `front` or `none`.
	 */
	properties[PROP_CULL_MODE] = g_param_spec_string ("cull-mode", "Cull mode",
	                                                  "Default face culling", "back", rw);
	/**
	 * LrgShaderAsset:depth-test:
	 *
	 * Whether depth testing is on by default.
	 */
	properties[PROP_DEPTH_TEST] = g_param_spec_boolean ("depth-test", "Depth test",
	                                                    "Default depth test", TRUE, rw);
	/**
	 * LrgShaderAsset:depth-write:
	 *
	 * Whether depth writes are on by default.
	 */
	properties[PROP_DEPTH_WRITE] = g_param_spec_boolean ("depth-write", "Depth write",
	                                                     "Default depth write", TRUE, rw);
	/**
	 * LrgShaderAsset:alpha-cutoff:
	 *
	 * The default alpha cutoff of the `cutout` blend mode.
	 */
	properties[PROP_ALPHA_CUTOFF] = g_param_spec_float ("alpha-cutoff", "Alpha cutoff",
	                                                    "Default alpha cutoff",
	                                                    0.0f, 1.0f, 0.5f, rw);
	/**
	 * LrgShaderAsset:time:
	 *
	 * The value uploaded as the built-in `lrgTime` uniform.
	 */
	properties[PROP_TIME] = g_param_spec_float ("time", "Time", "lrgTime value",
	                                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);
	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgShaderAsset::reloaded:
	 * @self: the asset
	 *
	 * Emitted after lrg_shader_asset_reload() (or a watched file change)
	 * replaced the description and, when compiled, the program.  Look
	 * the shader up again with lrg_shader_asset_get_shader().
	 */
	signals[SIGNAL_RELOADED] = g_signal_new ("reloaded", G_TYPE_FROM_CLASS (klass),
	                                         G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                         G_TYPE_NONE, 0);
	/**
	 * LrgShaderAsset::reload-failed:
	 * @self: the asset
	 * @error: the parse, I/O or compile error
	 *
	 * Emitted when a reload failed; the previous description and program
	 * stay in use (keep-last-good).  Compile errors carry the mapped
	 * diagnostics, also in lrg_shader_asset_get_diagnostics().
	 */
	signals[SIGNAL_RELOAD_FAILED] = g_signal_new ("reload-failed", G_TYPE_FROM_CLASS (klass),
	                                              G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                              G_TYPE_NONE, 1, G_TYPE_ERROR);
}

static void
lrg_shader_asset_init (LrgShaderAsset *self)
{
	self->desc = shader_desc_new ();
	self->values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                      (GDestroyNotify) uniform_value_free);
	self->locations = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->default_textures = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                                g_object_unref);
	self->diagnostics = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_shader_diagnostic_free);
	self->light_sets = g_ptr_array_new_with_free_func ((GDestroyNotify) weak_ref_free);
	self->monitors = g_ptr_array_new_with_free_func (g_object_unref);
	self->watched_files = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgShaderAsset *
lrg_shader_asset_new (void)
{
	return g_object_new (LRG_TYPE_SHADER_ASSET, NULL);
}

/* ========================================================================== */
/* Parsing                                                                    */
/* ========================================================================== */

static gboolean
parse_fail (GError      **error,
            const gchar  *source,
            const gchar  *where,
            const gchar  *message)
{
	g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_PARSE,
	             "%s: %s: %s", source, where, message);
	return FALSE;
}

/* a number scalar; FALSE when @node is not one */
static gboolean
node_number (YamlNode *node,
             gfloat   *out)
{
	const gchar *text;
	gchar *end = NULL;
	gdouble value;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	text = yaml_node_get_scalar (node);
	if (text == NULL || text[0] == '\0')
		return FALSE;
	value = g_ascii_strtod (text, &end);
	if (end == NULL || *end != '\0')
		return FALSE;
	*out = (gfloat) value;
	return TRUE;
}

/* a boolean scalar (true/false/yes/no/on/off); FALSE when not one */
static gboolean
node_boolean (YamlNode *node,
              gboolean *out)
{
	const gchar *text;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	text = yaml_node_get_scalar (node);
	if (text == NULL)
		return FALSE;
	if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0 ||
	    g_ascii_strcasecmp (text, "on") == 0)
	{
		*out = TRUE;
		return TRUE;
	}
	if (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0 ||
	    g_ascii_strcasecmp (text, "off") == 0)
	{
		*out = FALSE;
		return TRUE;
	}
	return FALSE;
}

/* a non-empty string scalar or NULL */
static const gchar *
node_string (YamlNode *node)
{
	const gchar *text;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	text = yaml_node_get_scalar (node);
	return (text != NULL && text[0] != '\0') ? text : NULL;
}

/* a number, or a sequence of 1 .. 4 numbers; the count read goes to
 * @out_n */
static gboolean
node_numbers (YamlNode *node,
              gfloat   *out,
              guint    *out_n)
{
	YamlSequence *seq;
	guint n;
	guint i;

	if (node_number (node, &out[0]))
	{
		*out_n = 1;
		return TRUE;
	}
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return FALSE;
	seq = yaml_node_get_sequence (node);
	n = seq != NULL ? yaml_sequence_get_length (seq) : 0;
	if (n < 1 || n > 4)
		return FALSE;
	for (i = 0; i < n; i++)
		if (!node_number (yaml_sequence_get_element (seq, i), &out[i]))
			return FALSE;
	*out_n = n;
	return TRUE;
}

/* is @name a GLSL identifier the engine lets users declare? */
static gboolean
valid_identifier (const gchar *name)
{
	const gchar *p;

	if (name == NULL || name[0] == '\0' || g_ascii_isdigit (name[0]))
		return FALSE;
	if (g_str_has_prefix (name, "gl_"))
		return FALSE;
	for (p = name; *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '_')
			return FALSE;
	return strlen (name) <= 64;
}

/* one entry of `uniforms:` */
static gboolean
parse_uniform (const gchar  *name,
               YamlNode     *node,
               ShaderDesc   *desc,
               const gchar  *source,
               GError      **error)
{
	g_autofree gchar *where = g_strdup_printf ("/uniforms/%s", name);
	g_autoptr(LrgShaderUniform) uniform = NULL;
	LrgShaderUniformType type;
	YamlMapping *map;
	YamlNode *member;
	guint i;

	if (!valid_identifier (name))
		return parse_fail (error, source, where, "not a valid GLSL identifier");
	if (shader_desc_find_uniform (desc, name, NULL) != NULL)
		return parse_fail (error, source, where, "declared twice");
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return parse_fail (error, source, where, "expected a mapping with at least 'type'");
	map = yaml_node_get_mapping (node);

	/* every key must be known: catches typos such as `defualt` */
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);

		if (g_strcmp0 (key, "type") != 0 && g_strcmp0 (key, "default") != 0 &&
		    g_strcmp0 (key, "range") != 0 && g_strcmp0 (key, "hint") != 0)
		{
			g_autofree gchar *msg = g_strdup_printf ("unknown key '%s' (expected type, default, range, hint)",
			                                         key);
			return parse_fail (error, source, where, msg);
		}
	}

	if (!uniform_type_from_nick (node_string (yaml_mapping_get_member (map, "type")), &type))
		return parse_fail (error, source, where,
		                   "'type' must be float, vec2, vec3, vec4, color, int, bool or sampler2D");
	uniform = lrg_shader_uniform_new (name, type);

	member = yaml_mapping_get_member (map, "default");
	if (member != NULL && yaml_node_get_node_type (member) != YAML_NODE_NULL)
	{
		if (type == LRG_SHADER_UNIFORM_SAMPLER2D)
		{
			const gchar *path = node_string (member);

			if (path == NULL)
				return parse_fail (error, source, where, "a sampler default is a texture path");
			lrg_shader_uniform_set_default_texture (uniform, path);
		}
		else if (type == LRG_SHADER_UNIFORM_BOOL)
		{
			gboolean flag;
			gfloat value;

			if (!node_boolean (member, &flag))
				return parse_fail (error, source, where, "a bool default is true or false");
			value = flag ? 1.0f : 0.0f;
			lrg_shader_uniform_set_default (uniform, &value, 1);
		}
		else
		{
			gfloat values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			guint n = 0;
			guint want = lrg_shader_uniform_type_get_components (type);

			if (!node_numbers (member, values, &n) || n != want)
			{
				g_autofree gchar *msg = g_strdup_printf ("'default' needs %u number%s", want,
				                                         want == 1 ? "" : "s");
				return parse_fail (error, source, where, msg);
			}
			lrg_shader_uniform_set_default (uniform, values, n);
		}
	}

	member = yaml_mapping_get_member (map, "range");
	if (member != NULL && yaml_node_get_node_type (member) != YAML_NODE_NULL)
	{
		gfloat values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		guint n = 0;

		if (!node_numbers (member, values, &n) || n != 2 || values[0] > values[1])
			return parse_fail (error, source, where, "'range' is [min, max] with min <= max");
		lrg_shader_uniform_set_range (uniform, values[0], values[1]);
	}

	member = yaml_mapping_get_member (map, "hint");
	if (member != NULL && yaml_node_get_node_type (member) != YAML_NODE_NULL)
	{
		if (node_string (member) == NULL)
			return parse_fail (error, source, where, "'hint' is a word");
		lrg_shader_uniform_set_hint (uniform, node_string (member));
	}

	g_ptr_array_add (desc->uniforms, g_steal_pointer (&uniform));
	return TRUE;
}

/* the `render_state:` block */
static gboolean
parse_render_state (YamlNode     *node,
                    ShaderDesc   *desc,
                    const gchar  *source,
                    GError      **error)
{
	YamlMapping *map;
	guint i;

	if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return parse_fail (error, source, "/render_state", "expected a mapping");
	map = yaml_node_get_mapping (node);
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);
		g_autofree gchar *where = g_strdup_printf ("/render_state/%s", key);

		if (g_strcmp0 (key, "blend") == 0)
		{
			desc->blend_mode = vocabulary_lookup (blend_modes, node_string (value));
			if (desc->blend_mode == NULL)
				return parse_fail (error, source, where,
				                   "expected opaque, cutout, alpha, additive or multiply");
		}
		else if (g_strcmp0 (key, "cull") == 0)
		{
			desc->cull_mode = vocabulary_lookup (cull_modes, node_string (value));
			if (desc->cull_mode == NULL)
				return parse_fail (error, source, where, "expected back, front or none");
		}
		else if (g_strcmp0 (key, "depth_test") == 0)
		{
			if (!node_boolean (value, &desc->depth_test))
				return parse_fail (error, source, where, "expected true or false");
		}
		else if (g_strcmp0 (key, "depth_write") == 0)
		{
			if (!node_boolean (value, &desc->depth_write))
				return parse_fail (error, source, where, "expected true or false");
		}
		else if (g_strcmp0 (key, "alpha_cutoff") == 0)
		{
			if (!node_number (value, &desc->alpha_cutoff) ||
			    desc->alpha_cutoff < 0.0f || desc->alpha_cutoff > 1.0f)
				return parse_fail (error, source, where, "expected a number from 0 to 1");
		}
		else
		{
			return parse_fail (error, source, where,
			                   "unknown key (expected blend, cull, depth_test, depth_write, alpha_cutoff)");
		}
	}
	return TRUE;
}

/* resolves a stage path against @base_dir */
static gchar *
resolve_path (const gchar *base_dir,
              const gchar *path)
{
	if (g_path_is_absolute (path) || base_dir == NULL)
		return g_strdup (path);
	return g_build_filename (base_dir, path, NULL);
}

/* reads a stage file into desc->source[stage] */
static gboolean
load_stage_file (ShaderDesc      *desc,
                 LrgShaderStage   stage,
                 const gchar     *path,
                 const gchar     *base_dir,
                 GError         **error)
{
	g_autofree gchar *resolved = resolve_path (base_dir, path);
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	if (!g_file_get_contents (resolved, &text, NULL, &local))
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_IO,
		             "cannot read the %s stage '%s': %s",
		             stage == LRG_SHADER_STAGE_VERTEX ? "vertex" : "fragment",
		             resolved, local->message);
		return FALSE;
	}
	g_free (desc->source[stage]);
	g_free (desc->source_path[stage]);
	desc->source[stage] = g_steal_pointer (&text);
	desc->source_path[stage] = g_strdup (path);
	return TRUE;
}

/* a stage key: multi-line text is inline GLSL, one line is a path */
static gboolean
parse_stage (YamlNode        *node,
             LrgShaderStage   stage,
             ShaderDesc      *desc,
             const gchar     *base_dir,
             const gchar     *source,
             GError         **error)
{
	const gchar *where = stage == LRG_SHADER_STAGE_VERTEX ? "/vertex" : "/fragment";
	const gchar *text = node_string (node);

	if (yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return TRUE;
	if (text == NULL)
		return parse_fail (error, source, where, "expected a file path or a literal block of GLSL");
	if (strchr (text, '\n') != NULL)
	{
		desc->source[stage] = g_strdup (text);
		return TRUE;
	}
	return load_stage_file (desc, stage, text, base_dir, error);
}

static ShaderDesc *
desc_from_parser (YamlParser   *parser,
                  const gchar  *base_dir,
                  const gchar  *source,
                  GError      **error)
{
	g_autoptr(ShaderDesc) desc = shader_desc_new ();
	YamlNode *root = yaml_parser_get_root (parser);
	YamlMapping *map;
	guint i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		parse_fail (error, source, "/", "a .rshader document is a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (root);

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);

		if (g_strcmp0 (key, "version") == 0)
		{
			gfloat version = 0.0f;

			if (!node_number (value, &version) || version != 1.0f)
			{
				parse_fail (error, source, "/version", "only version 1 is supported");
				return NULL;
			}
		}
		else if (g_strcmp0 (key, "name") == 0)
		{
			if (node_string (value) == NULL)
			{
				parse_fail (error, source, "/name", "expected a string");
				return NULL;
			}
			g_free (desc->name);
			desc->name = g_strdup (node_string (value));
		}
		else if (g_strcmp0 (key, "lit") == 0)
		{
			if (!node_boolean (value, &desc->lit))
			{
				parse_fail (error, source, "/lit", "expected true or false");
				return NULL;
			}
		}
		else if (g_strcmp0 (key, "vertex") == 0)
		{
			if (!parse_stage (value, LRG_SHADER_STAGE_VERTEX, desc, base_dir, source, error))
				return NULL;
		}
		else if (g_strcmp0 (key, "fragment") == 0)
		{
			if (!parse_stage (value, LRG_SHADER_STAGE_FRAGMENT, desc, base_dir, source, error))
				return NULL;
		}
		else if (g_strcmp0 (key, "render_state") == 0)
		{
			if (!parse_render_state (value, desc, source, error))
				return NULL;
		}
		else if (g_strcmp0 (key, "uniforms") == 0)
		{
			YamlMapping *uniforms;
			guint u;

			if (yaml_node_get_node_type (value) == YAML_NODE_NULL)
				continue;
			if (yaml_node_get_node_type (value) != YAML_NODE_MAPPING)
			{
				parse_fail (error, source, "/uniforms", "expected a mapping of name: description");
				return NULL;
			}
			uniforms = yaml_node_get_mapping (value);
			for (u = 0; u < yaml_mapping_get_size (uniforms); u++)
				if (!parse_uniform (yaml_mapping_get_key (uniforms, u),
				                    yaml_mapping_get_value (uniforms, u), desc, source, error))
					return NULL;
		}
		else
		{
			/* unknown keys are kept and written back in place */
			if (desc->extra == NULL)
				desc->extra = yaml_mapping_new ();
			yaml_mapping_set_member (desc->extra, key, value);
		}
	}

	if (desc->source[LRG_SHADER_STAGE_FRAGMENT] == NULL)
	{
		parse_fail (error, source, "/fragment", "a shader asset needs a fragment stage");
		return NULL;
	}
	return g_steal_pointer (&desc);
}

/* parses @path into a description; @out_base_dir gets its directory */
static ShaderDesc *
desc_from_file (const gchar  *path,
                GError      **error)
{
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autofree gchar *base_dir = g_path_get_dirname (path);
	g_autoptr(GError) local = NULL;

	if (!yaml_parser_load_from_file (parser, path, &local))
	{
		gint code = g_error_matches (local, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
		            local->domain == G_FILE_ERROR || local->domain == G_IO_ERROR
		            ? LRG_SHADER_ASSET_ERROR_IO : LRG_SHADER_ASSET_ERROR_PARSE;

		g_set_error (error, LRG_SHADER_ASSET_ERROR, code, "%s: %s", path, local->message);
		return NULL;
	}
	return desc_from_parser (parser, base_dir, path, error);
}

LrgShaderAsset *
lrg_shader_asset_new_from_file (const gchar  *path,
                                GError      **error)
{
	g_autoptr(LrgShaderAsset) self = NULL;
	ShaderDesc *desc;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	desc = desc_from_file (path, error);
	if (desc == NULL)
		return NULL;

	self = lrg_shader_asset_new ();
	shader_desc_free (self->desc);
	self->desc = desc;
	self->path = g_strdup (path);
	self->base_dir = g_path_get_dirname (path);
	merge_values (self);
	return g_steal_pointer (&self);
}

LrgShaderAsset *
lrg_shader_asset_new_from_data (const gchar  *data,
                                gssize        length,
                                const gchar  *base_dir,
                                GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(LrgShaderAsset) self = NULL;
	g_autoptr(GError) local = NULL;
	ShaderDesc *desc;

	g_return_val_if_fail (data != NULL || length == 0, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, &local))
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_PARSE,
		             "<data>: %s", local->message);
		return NULL;
	}
	desc = desc_from_parser (parser, base_dir, "<data>", error);
	if (desc == NULL)
		return NULL;

	self = lrg_shader_asset_new ();
	shader_desc_free (self->desc);
	self->desc = desc;
	self->base_dir = g_strdup (base_dir);
	merge_values (self);
	return g_steal_pointer (&self);
}

/* ========================================================================== */
/* Writing                                                                    */
/* ========================================================================== */

/* the shortest decimal text that reads back as @value; whole numbers are
 * written without an exponent (200, not 2e+02) */
static gchar *
number_text (gfloat value)
{
	gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];
	gchar format[8];
	gint precision;

	if (isfinite (value) && value == floorf (value) && fabsf (value) < 1e9f)
		return g_strdup_printf ("%.0f", (gdouble) value);
	for (precision = 1; precision <= 9; precision++)
	{
		g_snprintf (format, sizeof (format), "%%.%dg", precision);
		g_ascii_formatd (buffer, sizeof (buffer), format, (gdouble) value);
		if ((gfloat) g_ascii_strtod (buffer, NULL) == value)
			break;
	}
	return g_strdup (buffer);
}

/* a plain number scalar in number_text() form */
static YamlNode *
number_node (gfloat value)
{
	g_autofree gchar *text = number_text (value);

	return yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN);
}

/* a sequence of @n float nodes */
static YamlNode *
floats_node (const gfloat *values,
             guint         n)
{
	g_autoptr(YamlSequence) seq = yaml_sequence_new ();
	guint i;

	for (i = 0; i < n; i++)
	{
		g_autoptr(YamlNode) item = number_node (values[i]);

		yaml_sequence_add_element (seq, item);
	}
	return yaml_node_new_sequence (seq);
}

static YamlNode *
uniform_to_node (const LrgShaderUniform *u)
{
	g_autoptr(YamlMapping) map = yaml_mapping_new ();
	g_autoptr(YamlNode) type = lrg_data_yaml_string (uniform_type_nick (u->type));
	gfloat fresh[4];
	guint n = lrg_shader_uniform_type_get_components (u->type);

	yaml_mapping_set_member (map, "type", type);

	/* the default only when it differs from a fresh uniform's */
	uniform_type_default (u->type, fresh);
	if (u->type == LRG_SHADER_UNIFORM_SAMPLER2D)
	{
		if (u->texture != NULL)
		{
			g_autoptr(YamlNode) node = lrg_data_yaml_string (u->texture);

			yaml_mapping_set_member (map, "default", node);
		}
	}
	else if (memcmp (u->value, fresh, sizeof (gfloat) * n) != 0)
	{
		g_autoptr(YamlNode) node = NULL;

		if (u->type == LRG_SHADER_UNIFORM_BOOL)
			node = lrg_data_yaml_boolean (u->value[0] != 0.0f);
		else if (u->type == LRG_SHADER_UNIFORM_INT)
			node = lrg_data_yaml_int ((gint64) floorf (u->value[0] + 0.5f));
		else if (n == 1)
			node = number_node (u->value[0]);
		else
			node = floats_node (u->value, n);
		yaml_mapping_set_member (map, "default", node);
	}
	if (u->has_range)
	{
		gfloat range[2];
		g_autoptr(YamlNode) node = NULL;

		range[0] = u->range_min;
		range[1] = u->range_max;
		node = floats_node (range, 2);
		yaml_mapping_set_member (map, "range", node);
	}
	if (u->hint != NULL)
	{
		g_autoptr(YamlNode) node = lrg_data_yaml_string (u->hint);

		yaml_mapping_set_member (map, "hint", node);
	}
	return yaml_node_new_mapping (map);
}

/* a stage value: its path, or its text as a literal block */
static YamlNode *
stage_to_node (ShaderDesc     *desc,
               LrgShaderStage  stage)
{
	YamlNode *node;

	if (desc->source_path[stage] != NULL)
		return lrg_data_yaml_string (desc->source_path[stage]);
	node = lrg_data_yaml_string (desc->source[stage]);
	if (strchr (desc->source[stage], '\n') != NULL)
		yaml_node_set_scalar_style (node, YAML_SCALAR_STYLE_LITERAL);
	return node;
}

static YamlNode *
desc_to_node (ShaderDesc *desc)
{
	g_autoptr(YamlMapping) root = yaml_mapping_new ();
	g_autoptr(YamlNode) version = lrg_data_yaml_int (1);
	g_autoptr(YamlNode) name = lrg_data_yaml_string (desc->name);
	guint s;
	guint i;

	yaml_mapping_set_member (root, "version", version);
	yaml_mapping_set_member (root, "name", name);
	if (desc->lit)
	{
		g_autoptr(YamlNode) lit = lrg_data_yaml_boolean (TRUE);

		yaml_mapping_set_member (root, "lit", lit);
	}

	for (s = 0; s < N_STAGES; s++)
	{
		g_autoptr(YamlNode) node = NULL;

		if (desc->source[s] == NULL)
			continue;
		node = stage_to_node (desc, (LrgShaderStage) s);
		yaml_mapping_set_member (root, s == LRG_SHADER_STAGE_VERTEX ? "vertex" : "fragment", node);
	}

	/* render state: only keys that differ from the defaults */
	{
		g_autoptr(YamlMapping) state = yaml_mapping_new ();

		if (desc->blend_mode != blend_modes[0])
		{
			g_autoptr(YamlNode) node = lrg_data_yaml_string (desc->blend_mode);

			yaml_mapping_set_member (state, "blend", node);
		}
		if (desc->cull_mode != cull_modes[0])
		{
			g_autoptr(YamlNode) node = lrg_data_yaml_string (desc->cull_mode);

			yaml_mapping_set_member (state, "cull", node);
		}
		if (!desc->depth_test)
		{
			g_autoptr(YamlNode) node = lrg_data_yaml_boolean (FALSE);

			yaml_mapping_set_member (state, "depth_test", node);
		}
		if (!desc->depth_write)
		{
			g_autoptr(YamlNode) node = lrg_data_yaml_boolean (FALSE);

			yaml_mapping_set_member (state, "depth_write", node);
		}
		if (desc->alpha_cutoff != 0.5f)
		{
			g_autoptr(YamlNode) node = number_node (desc->alpha_cutoff);

			yaml_mapping_set_member (state, "alpha_cutoff", node);
		}
		if (yaml_mapping_get_size (state) > 0)
		{
			g_autoptr(YamlNode) node = yaml_node_new_mapping (state);

			yaml_mapping_set_member (root, "render_state", node);
		}
	}

	if (desc->uniforms->len > 0)
	{
		g_autoptr(YamlMapping) uniforms = yaml_mapping_new ();
		g_autoptr(YamlNode) node = NULL;

		for (i = 0; i < desc->uniforms->len; i++)
		{
			LrgShaderUniform *u = g_ptr_array_index (desc->uniforms, i);
			g_autoptr(YamlNode) entry = uniform_to_node (u);

			yaml_mapping_set_member (uniforms, u->name, entry);
		}
		node = yaml_node_new_mapping (uniforms);
		yaml_mapping_set_member (root, "uniforms", node);
	}

	/* unknown keys of the loaded document, in their order */
	if (desc->extra != NULL)
		for (i = 0; i < yaml_mapping_get_size (desc->extra); i++)
			yaml_mapping_set_member (root, yaml_mapping_get_key (desc->extra, i),
			                         yaml_mapping_get_value (desc->extra, i));

	return yaml_node_new_mapping (root);
}

gchar *
lrg_shader_asset_save_to_data (LrgShaderAsset  *self,
                               gsize           *length,
                               GError         **error)
{
	g_autoptr(YamlNode) root = NULL;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	root = desc_to_node (self->desc);
	return lrg_data_yaml_to_data (root, length, error);
}

gboolean
lrg_shader_asset_save_to_file (LrgShaderAsset  *self,
                               const gchar     *path,
                               GError         **error)
{
	g_autoptr(YamlNode) root = NULL;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	root = desc_to_node (self->desc);
	return lrg_data_yaml_to_file (root, path, error);
}

/* ========================================================================== */
/* Accessors                                                                  */
/* ========================================================================== */

const gchar *
lrg_shader_asset_get_path (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	return self->path;
}

const gchar *
lrg_shader_asset_get_name (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	return self->desc->name;
}

void
lrg_shader_asset_set_name (LrgShaderAsset *self,
                           const gchar    *name)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));
	g_return_if_fail (name != NULL);

	if (g_strcmp0 (self->desc->name, name) == 0)
		return;
	g_free (self->desc->name);
	self->desc->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

gboolean
lrg_shader_asset_get_lit (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	return self->desc->lit;
}

void
lrg_shader_asset_set_lit (LrgShaderAsset *self,
                          gboolean        lit)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));

	lit = !!lit;
	if (self->desc->lit == lit)
		return;
	self->desc->lit = lit;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIT]);
}

void
lrg_shader_asset_set_library_source (LrgShaderAsset *self,
                                     const gchar    *glsl)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));
	g_free (self->library_override);
	self->library_override = g_strdup (glsl);
}

const gchar *
lrg_shader_asset_get_library_source (LrgShaderAsset *self)
{
	const gchar *library;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);

	if (self->library_override != NULL)
		return self->library_override;
	library = lrg_light_set_get_glsl_library ();
	return library != NULL ? library : "";
}

void
lrg_shader_asset_set_source (LrgShaderAsset *self,
                             LrgShaderStage  stage,
                             const gchar    *source)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));
	g_return_if_fail (stage == LRG_SHADER_STAGE_VERTEX || stage == LRG_SHADER_STAGE_FRAGMENT);

	g_free (self->desc->source[stage]);
	g_clear_pointer (&self->desc->source_path[stage], g_free);
	self->desc->source[stage] = g_strdup (source);
}

const gchar *
lrg_shader_asset_get_source (LrgShaderAsset *self,
                             LrgShaderStage  stage)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	g_return_val_if_fail (stage == LRG_SHADER_STAGE_VERTEX || stage == LRG_SHADER_STAGE_FRAGMENT, NULL);
	return self->desc->source[stage];
}

gboolean
lrg_shader_asset_set_source_path (LrgShaderAsset  *self,
                                  LrgShaderStage   stage,
                                  const gchar     *path,
                                  GError         **error)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (stage == LRG_SHADER_STAGE_VERTEX || stage == LRG_SHADER_STAGE_FRAGMENT, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!load_stage_file (self->desc, stage, path, self->base_dir, error))
		return FALSE;
	/* a watched asset follows the new file too */
	if (self->watch)
	{
		stop_watching (self);
		start_watching (self, NULL);
	}
	return TRUE;
}

const gchar *
lrg_shader_asset_get_source_path (LrgShaderAsset *self,
                                  LrgShaderStage  stage)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	g_return_val_if_fail (stage == LRG_SHADER_STAGE_VERTEX || stage == LRG_SHADER_STAGE_FRAGMENT, NULL);
	return self->desc->source_path[stage];
}

const gchar *
lrg_shader_asset_get_blend_mode (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	return self->desc->blend_mode;
}

gboolean
lrg_shader_asset_set_blend_mode (LrgShaderAsset *self,
                                 const gchar    *mode)
{
	const gchar *entry;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);

	entry = vocabulary_lookup (blend_modes, mode);
	if (entry == NULL)
		return FALSE;
	if (entry != self->desc->blend_mode)
	{
		self->desc->blend_mode = entry;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
	}
	return TRUE;
}

const gchar *
lrg_shader_asset_get_cull_mode (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	return self->desc->cull_mode;
}

gboolean
lrg_shader_asset_set_cull_mode (LrgShaderAsset *self,
                                const gchar    *mode)
{
	const gchar *entry;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);

	entry = vocabulary_lookup (cull_modes, mode);
	if (entry == NULL)
		return FALSE;
	if (entry != self->desc->cull_mode)
	{
		self->desc->cull_mode = entry;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CULL_MODE]);
	}
	return TRUE;
}

gboolean
lrg_shader_asset_get_depth_test (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), TRUE);
	return self->desc->depth_test;
}

void
lrg_shader_asset_set_depth_test (LrgShaderAsset *self,
                                 gboolean        depth_test)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));

	depth_test = !!depth_test;
	if (self->desc->depth_test == depth_test)
		return;
	self->desc->depth_test = depth_test;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEPTH_TEST]);
}

gboolean
lrg_shader_asset_get_depth_write (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), TRUE);
	return self->desc->depth_write;
}

void
lrg_shader_asset_set_depth_write (LrgShaderAsset *self,
                                  gboolean        depth_write)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));

	depth_write = !!depth_write;
	if (self->desc->depth_write == depth_write)
		return;
	self->desc->depth_write = depth_write;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEPTH_WRITE]);
}

gfloat
lrg_shader_asset_get_alpha_cutoff (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), 0.5f);
	return self->desc->alpha_cutoff;
}

void
lrg_shader_asset_set_alpha_cutoff (LrgShaderAsset *self,
                                   gfloat          cutoff)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));

	cutoff = CLAMP (cutoff, 0.0f, 1.0f);
	if (self->desc->alpha_cutoff == cutoff)
		return;
	self->desc->alpha_cutoff = cutoff;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALPHA_CUTOFF]);
}

/* ========================================================================== */
/* Uniforms                                                                   */
/* ========================================================================== */

void
lrg_shader_asset_add_uniform (LrgShaderAsset         *self,
                              const LrgShaderUniform *uniform)
{
	LrgShaderUniform *copy;
	guint index;

	g_return_if_fail (LRG_IS_SHADER_ASSET (self));
	g_return_if_fail (uniform != NULL);

	copy = lrg_shader_uniform_copy (uniform);
	if (shader_desc_find_uniform (self->desc, uniform->name, &index) != NULL)
	{
		g_ptr_array_insert (self->desc->uniforms, (gint) index, copy);
		g_ptr_array_remove_index (self->desc->uniforms, index + 1);
	}
	else
	{
		g_ptr_array_add (self->desc->uniforms, copy);
	}
	reset_value (self, copy);
}

gboolean
lrg_shader_asset_remove_uniform (LrgShaderAsset *self,
                                 const gchar    *name)
{
	guint index;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (shader_desc_find_uniform (self->desc, name, &index) == NULL)
		return FALSE;
	g_ptr_array_remove_index (self->desc->uniforms, index);
	g_hash_table_remove (self->values, name);
	return TRUE;
}

guint
lrg_shader_asset_get_n_uniforms (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), 0);
	return self->desc->uniforms->len;
}

const LrgShaderUniform *
lrg_shader_asset_get_uniform_at (LrgShaderAsset *self,
                                 guint           index)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);

	if (index >= self->desc->uniforms->len)
		return NULL;
	return g_ptr_array_index (self->desc->uniforms, index);
}

const LrgShaderUniform *
lrg_shader_asset_get_uniform (LrgShaderAsset *self,
                              const gchar    *name)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	return shader_desc_find_uniform (self->desc, name, NULL);
}

/* the stored value of @name when its type has @components components (0:
 * any one-component number) */
static UniformValue *
lookup_value (LrgShaderAsset *self,
              const gchar    *name,
              guint           components)
{
	UniformValue *value;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	value = g_hash_table_lookup (self->values, name);
	if (value == NULL || value->type == LRG_SHADER_UNIFORM_SAMPLER2D)
		return NULL;
	if (lrg_shader_uniform_type_get_components (value->type) != components)
		return NULL;
	return value;
}

gboolean
lrg_shader_asset_set_uniform_float (LrgShaderAsset *self,
                                    const gchar    *name,
                                    gfloat          value)
{
	UniformValue *slot = lookup_value (self, name, 1);

	if (slot == NULL)
		return FALSE;
	slot->value[0] = value;
	return TRUE;
}

gboolean
lrg_shader_asset_set_uniform_int (LrgShaderAsset *self,
                                  const gchar    *name,
                                  gint            value)
{
	return lrg_shader_asset_set_uniform_float (self, name, (gfloat) value);
}

gboolean
lrg_shader_asset_set_uniform_vec2 (LrgShaderAsset *self,
                                   const gchar    *name,
                                   gfloat          x,
                                   gfloat          y)
{
	UniformValue *slot = lookup_value (self, name, 2);

	if (slot == NULL)
		return FALSE;
	slot->value[0] = x;
	slot->value[1] = y;
	return TRUE;
}

gboolean
lrg_shader_asset_set_uniform_vec3 (LrgShaderAsset *self,
                                   const gchar    *name,
                                   gfloat          x,
                                   gfloat          y,
                                   gfloat          z)
{
	UniformValue *slot = lookup_value (self, name, 3);

	if (slot == NULL)
		return FALSE;
	slot->value[0] = x;
	slot->value[1] = y;
	slot->value[2] = z;
	return TRUE;
}

gboolean
lrg_shader_asset_set_uniform_vec4 (LrgShaderAsset *self,
                                   const gchar    *name,
                                   gfloat          x,
                                   gfloat          y,
                                   gfloat          z,
                                   gfloat          w)
{
	UniformValue *slot = lookup_value (self, name, 4);

	if (slot == NULL)
		return FALSE;
	slot->value[0] = x;
	slot->value[1] = y;
	slot->value[2] = z;
	slot->value[3] = w;
	return TRUE;
}

gboolean
lrg_shader_asset_set_uniform_texture (LrgShaderAsset *self,
                                      const gchar    *name,
                                      GrlTexture     *texture)
{
	UniformValue *slot;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (texture == NULL || GRL_IS_TEXTURE (texture), FALSE);

	slot = g_hash_table_lookup (self->values, name);
	if (slot == NULL || slot->type != LRG_SHADER_UNIFORM_SAMPLER2D)
		return FALSE;
	g_set_object (&slot->texture, texture);
	return TRUE;
}

gboolean
lrg_shader_asset_get_uniform_value (LrgShaderAsset *self,
                                    const gchar    *name,
                                    gfloat         *out_values)
{
	UniformValue *slot;
	guint n;
	guint i;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (out_values != NULL, FALSE);

	memset (out_values, 0, sizeof (gfloat) * 4);
	slot = g_hash_table_lookup (self->values, name);
	if (slot == NULL)
		return FALSE;
	if (slot->type == LRG_SHADER_UNIFORM_SAMPLER2D)
		return TRUE;
	n = lrg_shader_uniform_type_get_components (slot->type);
	for (i = 0; i < n; i++)
		out_values[i] = slot->value[i];
	return TRUE;
}

void
lrg_shader_asset_reset_uniforms (LrgShaderAsset *self)
{
	guint i;

	g_return_if_fail (LRG_IS_SHADER_ASSET (self));

	g_hash_table_remove_all (self->values);
	for (i = 0; i < self->desc->uniforms->len; i++)
		reset_value (self, g_ptr_array_index (self->desc->uniforms, i));
}

void
lrg_shader_asset_set_time (LrgShaderAsset *self,
                           gfloat          seconds)
{
	g_return_if_fail (LRG_IS_SHADER_ASSET (self));

	if (self->time == seconds)
		return;
	self->time = seconds;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
}

gfloat
lrg_shader_asset_get_time (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), 0.0f);
	return self->time;
}

/* ========================================================================== */
/* Stage text                                                                 */
/* ========================================================================== */

static const gchar default_vertex_source[] =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"in vec3 vertexNormal;\n"
	"in vec4 vertexColor;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 matModel;\n"
	"uniform mat4 matNormal;\n"
	"out vec3 fragPosition;\n"
	"out vec2 fragTexCoord;\n"
	"out vec4 fragColor;\n"
	"out vec3 fragNormal;\n"
	"void main()\n"
	"{\n"
	"    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
	"    fragTexCoord = vertexTexCoord;\n"
	"    fragColor = vertexColor;\n"
	"    fragNormal = vec3(matNormal * vec4(vertexNormal, 0.0));\n"
	"    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
	"}\n";

const gchar *
lrg_shader_asset_get_default_vertex_source (void)
{
	return default_vertex_source;
}

/* fallback lighting used by lit assets while the library defines no
 * lrg_light_pbr(): a fixed sun from above plus a quarter ambient */
static const gchar fallback_light_pbr[] =
	"vec3 lrg_light_pbr(vec3 p, vec3 n, vec3 v, vec4 albedo, float metal, float rough, float ao)\n"
	"{\n"
	"    float ndl = max(dot(normalize(n), normalize(vec3(0.3, 1.0, 0.2))), 0.0);\n"
	"    return albedo.rgb * (0.25 * ao + 0.75 * ndl);\n"
	"}\n";

/* sRGB as gamma 2.2, the light set's convention */
static const gchar to_linear_function[] =
	"vec3 lrg_to_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }\n";
static const gchar to_display_function[] =
	"vec3 lrg_to_display(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }\n";

/* the eye the light set uploads */
static const gchar view_position_camera_function[] =
	"vec3 lrg_view_position()\n"
	"{\n"
	"    return mapCamera;\n"
	"}\n";

/* the camera position from the view matrix (rigid transform) */
static const gchar view_position_function[] =
	"vec3 lrg_view_position()\n"
	"{\n"
	"    return -(transpose(mat3(matView)) * matView[3].xyz);\n"
	"}\n";

/* one engine declaration of the preamble */
typedef struct
{
	const gchar *name;
	const gchar *declaration;
} PreambleDecl;

static const PreambleDecl vertex_decls[] = {
	{ "vertexPosition", "in vec3 vertexPosition;\n" },
	{ "vertexTexCoord", "in vec2 vertexTexCoord;\n" },
	{ "vertexNormal", "in vec3 vertexNormal;\n" },
	{ "vertexColor", "in vec4 vertexColor;\n" },
	{ "vertexTangent", "in vec4 vertexTangent;\n" },
	{ "vertexTexCoord2", "in vec2 vertexTexCoord2;\n" },
	{ "mvp", "uniform mat4 mvp;\n" },
	{ "matModel", "uniform mat4 matModel;\n" },
	{ "matView", "uniform mat4 matView;\n" },
	{ "matProjection", "uniform mat4 matProjection;\n" },
	{ "matNormal", "uniform mat4 matNormal;\n" },
	{ "lrgTime", "uniform float lrgTime;\n" },
	{ "fragPosition", "out vec3 fragPosition;\n" },
	{ "fragTexCoord", "out vec2 fragTexCoord;\n" },
	{ "fragColor", "out vec4 fragColor;\n" },
	{ "fragNormal", "out vec3 fragNormal;\n" },
	{ NULL, NULL }
};

static const PreambleDecl fragment_decls[] = {
	{ "fragPosition", "in vec3 fragPosition;\n" },
	{ "fragTexCoord", "in vec2 fragTexCoord;\n" },
	{ "fragColor", "in vec4 fragColor;\n" },
	{ "fragNormal", "in vec3 fragNormal;\n" },
	{ "finalColor", "out vec4 finalColor;\n" },
	{ "texture0", "uniform sampler2D texture0;\n" },
	{ "colDiffuse", "uniform vec4 colDiffuse;\n" },
	{ "matView", "uniform mat4 matView;\n" },
	{ "lrgTime", "uniform float lrgTime;\n" },
	{ NULL, NULL }
};

/* library functions whose presence defines a feature macro */
typedef struct
{
	const gchar *macro;
	const gchar *pattern;
} LibraryFeature;

static const LibraryFeature library_features[] = {
	{ "LRG_HAS_DECALS", "\\bvoid\\s+lrg_decals_apply\\s*\\(" },
	{ "LRG_HAS_OVERLAY", "\\bvec3\\s+lrg_overlay_apply\\s*\\(" },
	{ "LRG_HAS_AIR", "\\bfloat\\s+lrg_air\\s*\\(" },
	{ "LRG_HAS_FOG", "\\bfloat\\s+lrg_fog_amount\\s*\\(" },
	{ NULL, NULL }
};

/* adds every name @text declares as `in`, `out` or `uniform` to @names */
static void
collect_declarations (const gchar *text,
                      GHashTable  *names)
{
	static GRegex *regex = NULL;
	g_autoptr(GMatchInfo) info = NULL;

	if (text == NULL)
		return;
	if (g_once_init_enter_pointer (&regex))
	{
		GRegex *compiled = g_regex_new (
			"\\b(?:uniform|in|out)\\s+(?:(?:highp|mediump|lowp|flat|smooth|noperspective)\\s+)*"
			"\\w+\\s+(\\w+)\\s*(?:\\[[^\\]]*\\])?\\s*;",
			G_REGEX_OPTIMIZE, 0, NULL);
		g_once_init_leave_pointer (&regex, compiled);
	}
	g_regex_match (regex, text, 0, &info);
	while (g_match_info_matches (info))
	{
		g_hash_table_add (names, g_match_info_fetch (info, 1));
		g_match_info_next (info, NULL);
	}
}

/* appends an error diagnostic for every name @user declares that the lit
 * library already declares (GLSL would report a bare "redefinition");
 * returns TRUE when there was any */
static gboolean
check_library_redeclarations (const gchar *user,
                              const gchar *library,
                              const gchar *source_name,
                              GPtrArray   *diagnostics)
{
	g_autoptr(GHashTable) library_names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(GRegex) regex = NULL;
	g_autoptr(GMatchInfo) info = NULL;
	gboolean found = FALSE;

	if (user == NULL || library == NULL || library[0] == '\0')
		return FALSE;
	collect_declarations (library, library_names);
	regex = g_regex_new ("\\b(?:uniform|in|out)\\s+(?:(?:highp|mediump|lowp|flat|smooth|noperspective)\\s+)*"
	                     "\\w+\\s+(\\w+)\\s*(?:\\[[^\\]]*\\])?\\s*;", 0, 0, NULL);
	g_regex_match (regex, user, 0, &info);
	while (g_match_info_matches (info))
	{
		g_autofree gchar *name = g_match_info_fetch (info, 1);
		gint start = 0;

		if (g_hash_table_contains (library_names, name))
		{
			g_autofree gchar *message = g_strdup_printf ("'%s' is declared by the lit library "
			                                             "(lrg_light_set_get_glsl_library()); remove this declaration",
			                                             name);
			guint line = 1;
			gint k;

			g_match_info_fetch_pos (info, 0, &start, NULL);
			for (k = 0; k < start; k++)
				if (user[k] == '\n')
					line++;
			g_ptr_array_add (diagnostics, diagnostic_new (LRG_SHADER_STAGE_FRAGMENT, line, 0, TRUE,
			                                              message, 0, source_name));
			found = TRUE;
		}
		g_match_info_next (info, NULL);
	}
	return found;
}

/* does @text define a `vec3 @function (` ? */
static gboolean
defines_function (const gchar *text,
                  const gchar *function)
{
	g_autofree gchar *pattern = NULL;

	if (text == NULL)
		return FALSE;
	pattern = g_strdup_printf ("\\bvec3\\s+%s\\s*\\(", function);
	return g_regex_match_simple (pattern, text, 0, 0);
}

/* @source with a leading `#version` line blanked (line count kept) */
static gchar *
strip_version (const gchar *source)
{
	gchar *copy = g_strdup (source);
	gchar *p = copy;

	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (g_str_has_prefix (p, "#version"))
		while (*p != '\0' && *p != '\n')
			*p++ = ' ';
	return copy;
}

/* the name diagnostics of @stage's user source report */
static gchar *
stage_source_name (LrgShaderAsset *self,
                   ShaderDesc     *desc,
                   LrgShaderStage  stage)
{
	const gchar *suffix = stage == LRG_SHADER_STAGE_VERTEX ? "vertex" : "fragment";

	if (desc->source_path[stage] != NULL)
		return resolve_path (self->base_dir, desc->source_path[stage]);
	if (desc->source[stage] == NULL)
		return g_strdup_printf ("<default %s>", suffix);
	return g_strdup_printf ("%s#%s", self->path != NULL ? self->path : desc->name, suffix);
}

/* the full text of @stage for @desc; @out_preamble gets the line count
 * before the user's source */
static gchar *
build_stage (LrgShaderAsset *self,
             ShaderDesc     *desc,
             LrgShaderStage  stage,
             guint          *out_preamble)
{
	g_autoptr(GString) text = g_string_new (NULL);
	g_autoptr(GHashTable) declared = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_autofree gchar *user = NULL;
	const PreambleDecl *decls;
	const gchar *library = NULL;
	const gchar *p;
	guint lines = 0;
	guint i;

	/* a vertex stage the user did not write is a complete built-in one */
	if (stage == LRG_SHADER_STAGE_VERTEX && desc->source[stage] == NULL)
	{
		const gchar *lit_vertex = desc->lit ? lrg_light_set_get_vertex_source () : NULL;

		if (out_preamble != NULL)
			*out_preamble = 0;
		if (lit_vertex != NULL && lit_vertex[0] != '\0')
		{
			if (strstr (lit_vertex, "#version") != NULL)
				return g_strdup (lit_vertex);
			if (out_preamble != NULL)
				*out_preamble = 1;
			return g_strconcat ("#version 330\n", lit_vertex, NULL);
		}
		return g_strdup (default_vertex_source);
	}

	user = strip_version (desc->source[stage] != NULL ? desc->source[stage] : "");
	if (stage == LRG_SHADER_STAGE_FRAGMENT && desc->lit)
		library = lrg_shader_asset_get_library_source (self);

	/* what the library and the user already declare is not repeated */
	collect_declarations (user, declared);
	collect_declarations (library, declared);

	g_string_append (text, "#version 330\n");
	g_string_append (text, "#define LRG_SHADER_ASSET 1\n");
	g_string_append (text, stage == LRG_SHADER_STAGE_VERTEX ? "#define LRG_VERTEX_STAGE 1\n"
	                                                         : "#define LRG_FRAGMENT_STAGE 1\n");
	if (desc->lit)
		g_string_append (text, "#define LRG_LIT 1\n");

	decls = stage == LRG_SHADER_STAGE_VERTEX ? vertex_decls : fragment_decls;
	for (i = 0; decls[i].name != NULL; i++)
		if (!g_hash_table_contains (declared, decls[i].name))
		{
			g_string_append (text, decls[i].declaration);
			g_hash_table_add (declared, g_strdup (decls[i].name));
		}

	if (library != NULL && library[0] != '\0')
	{
		g_string_append (text, library);
		if (text->str[text->len - 1] != '\n')
			g_string_append_c (text, '\n');
	}
	if (stage == LRG_SHADER_STAGE_FRAGMENT)
	{
		/* colour space helpers (the light-set library has them) */
		if (!defines_function (library, "lrg_to_linear") && !defines_function (user, "lrg_to_linear"))
			g_string_append (text, to_linear_function);
		if (!defines_function (library, "lrg_to_display") && !defines_function (user, "lrg_to_display"))
			g_string_append (text, to_display_function);
		if (desc->lit && !defines_function (library, "lrg_light_pbr") &&
		    !defines_function (user, "lrg_light_pbr"))
		{
			g_string_append (text, "#define LRG_LIGHT_FALLBACK 1\n");
			g_string_append (text, fallback_light_pbr);
		}
		/* feature macros generated code tests with #ifdef: which of the
		 * light set's passes the library provides */
		for (i = 0; library != NULL && library_features[i].macro != NULL; i++)
			if (g_regex_match_simple (library_features[i].pattern, library, 0, 0))
				g_string_append_printf (text, "#define %s 1\n", library_features[i].macro);
		if (library != NULL && g_hash_table_contains (declared, "outputLinear") &&
		    strstr (library, "outputLinear") != NULL)
			g_string_append (text, "#define LRG_HAS_OUTPUT_LINEAR 1\n");
		if (!defines_function (library, "lrg_view_position") &&
		    !defines_function (user, "lrg_view_position"))
		{
			/* the light set uploads its eye as mapCamera; others use matView */
			if (library != NULL && strstr (library, "mapCamera") != NULL &&
			    g_hash_table_contains (declared, "mapCamera"))
				g_string_append (text, view_position_camera_function);
			else
				g_string_append (text, view_position_function);
		}
	}

	/* the described uniforms */
	for (i = 0; i < desc->uniforms->len; i++)
	{
		LrgShaderUniform *u = g_ptr_array_index (desc->uniforms, i);

		if (g_hash_table_contains (declared, u->name))
			continue;
		g_string_append_printf (text, "uniform %s %s;\n",
		                        lrg_shader_uniform_type_get_glsl (u->type), u->name);
	}

	for (p = text->str; *p != '\0'; p++)
		if (*p == '\n')
			lines++;
	if (out_preamble != NULL)
		*out_preamble = lines;

	g_string_append (text, user);
	if (text->len > 0 && text->str[text->len - 1] != '\n')
		g_string_append_c (text, '\n');
	return g_string_free (g_steal_pointer (&text), FALSE);
}

gchar *
lrg_shader_asset_get_generated_source (LrgShaderAsset *self,
                                       LrgShaderStage  stage,
                                       guint          *out_preamble_lines)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	g_return_val_if_fail (stage == LRG_SHADER_STAGE_VERTEX || stage == LRG_SHADER_STAGE_FRAGMENT, NULL);
	return build_stage (self, self->desc, stage, out_preamble_lines);
}

/* ========================================================================== */
/* Compiling                                                                  */
/* ========================================================================== */

#define LRG_GL_FRAGMENT_SHADER  (0x8B30)
#define LRG_GL_VERTEX_SHADER    (0x8B31)
#define LRG_GL_COMPILE_STATUS   (0x8B81)
#define LRG_GL_LINK_STATUS      (0x8B82)
#define LRG_GL_INFO_LOG_LENGTH  (0x8B84)

typedef unsigned int (*LrgGlCreateShader) (unsigned int type);
typedef void (*LrgGlShaderSource) (unsigned int shader, int count, const char * const *strings,
                                   const int *lengths);
typedef void (*LrgGlCompileShader) (unsigned int shader);
typedef void (*LrgGlGetShaderiv) (unsigned int shader, unsigned int pname, int *params);
typedef void (*LrgGlGetShaderInfoLog) (unsigned int shader, int size, int *length, char *log);
typedef void (*LrgGlDeleteShader) (unsigned int shader);
typedef unsigned int (*LrgGlCreateProgram) (void);
typedef void (*LrgGlAttachShader) (unsigned int program, unsigned int shader);
typedef void (*LrgGlBindAttribLocation) (unsigned int program, unsigned int index, const char *name);
typedef void (*LrgGlLinkProgram) (unsigned int program);
typedef void (*LrgGlGetProgramiv) (unsigned int program, unsigned int pname, int *params);
typedef void (*LrgGlGetProgramInfoLog) (unsigned int program, int size, int *length, char *log);
typedef void (*LrgGlDeleteProgram) (unsigned int program);

/* the GL entry points the diagnostic compile needs */
typedef struct
{
	LrgGlCreateShader       create_shader;
	LrgGlShaderSource       shader_source;
	LrgGlCompileShader      compile_shader;
	LrgGlGetShaderiv        get_shaderiv;
	LrgGlGetShaderInfoLog   get_shader_info_log;
	LrgGlDeleteShader       delete_shader;
	LrgGlCreateProgram      create_program;
	LrgGlAttachShader       attach_shader;
	LrgGlBindAttribLocation bind_attrib_location;
	LrgGlLinkProgram        link_program;
	LrgGlGetProgramiv       get_programiv;
	LrgGlGetProgramInfoLog  get_program_info_log;
	LrgGlDeleteProgram      delete_program;
} GlFunctions;

/* resolves the entry points through raylib's loader; FALSE when any is
 * missing */
static gboolean
gl_functions_load (GlFunctions *gl)
{
	gl->create_shader = (LrgGlCreateShader) rlGetProcAddress ("glCreateShader");
	gl->shader_source = (LrgGlShaderSource) rlGetProcAddress ("glShaderSource");
	gl->compile_shader = (LrgGlCompileShader) rlGetProcAddress ("glCompileShader");
	gl->get_shaderiv = (LrgGlGetShaderiv) rlGetProcAddress ("glGetShaderiv");
	gl->get_shader_info_log = (LrgGlGetShaderInfoLog) rlGetProcAddress ("glGetShaderInfoLog");
	gl->delete_shader = (LrgGlDeleteShader) rlGetProcAddress ("glDeleteShader");
	gl->create_program = (LrgGlCreateProgram) rlGetProcAddress ("glCreateProgram");
	gl->attach_shader = (LrgGlAttachShader) rlGetProcAddress ("glAttachShader");
	gl->bind_attrib_location = (LrgGlBindAttribLocation) rlGetProcAddress ("glBindAttribLocation");
	gl->link_program = (LrgGlLinkProgram) rlGetProcAddress ("glLinkProgram");
	gl->get_programiv = (LrgGlGetProgramiv) rlGetProcAddress ("glGetProgramiv");
	gl->get_program_info_log = (LrgGlGetProgramInfoLog) rlGetProcAddress ("glGetProgramInfoLog");
	gl->delete_program = (LrgGlDeleteProgram) rlGetProcAddress ("glDeleteProgram");

	return gl->create_shader != NULL && gl->shader_source != NULL && gl->compile_shader != NULL &&
	       gl->get_shaderiv != NULL && gl->get_shader_info_log != NULL && gl->delete_shader != NULL &&
	       gl->create_program != NULL && gl->attach_shader != NULL &&
	       gl->bind_attrib_location != NULL && gl->link_program != NULL &&
	       gl->get_programiv != NULL && gl->get_program_info_log != NULL &&
	       gl->delete_program != NULL;
}

/* compiles one stage; returns the shader id (0 on failure) and appends
 * its log's diagnostics to @diagnostics */
static guint
diagnostic_compile (GlFunctions    *gl,
                    const gchar    *text,
                    LrgShaderStage  stage,
                    guint           preamble,
                    const gchar    *source_name,
                    GPtrArray      *diagnostics)
{
	guint id;
	gint status = 0;
	gint length = 0;

	id = gl->create_shader (stage == LRG_SHADER_STAGE_VERTEX ? LRG_GL_VERTEX_SHADER
	                                                         : LRG_GL_FRAGMENT_SHADER);
	gl->shader_source (id, 1, &text, NULL);
	gl->compile_shader (id);
	gl->get_shaderiv (id, LRG_GL_COMPILE_STATUS, &status);
	gl->get_shaderiv (id, LRG_GL_INFO_LOG_LENGTH, &length);
	if (length > 1)
	{
		g_autofree gchar *log = g_malloc0 ((gsize) length + 1);
		g_autoptr(GPtrArray) parsed = NULL;
		guint i;

		gl->get_shader_info_log (id, length, NULL, log);
		parsed = lrg_shader_parse_info_log (log, stage, preamble, source_name);
		for (i = 0; i < parsed->len; i++)
			g_ptr_array_add (diagnostics, lrg_shader_diagnostic_copy (g_ptr_array_index (parsed, i)));
	}
	if (!status)
	{
		/* a failed compile with an empty log still reports something */
		if (length <= 1)
			g_ptr_array_add (diagnostics, diagnostic_new (stage, 0, 0, TRUE, "compile failed",
			                                              preamble, source_name));
		gl->delete_shader (id);
		return 0;
	}
	return id;
}

/* links the compiled stages with raylib's attribute bindings; FALSE (with
 * the link log in @diagnostics) when they do not link */
static gboolean
diagnostic_link (GlFunctions *gl,
                 guint        vs,
                 guint        fs,
                 const gchar *source_name,
                 GPtrArray   *diagnostics)
{
	guint program = gl->create_program ();
	gint status = 0;
	gint length = 0;

	gl->attach_shader (program, vs);
	gl->attach_shader (program, fs);
	gl->bind_attrib_location (program, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION,
	                          "vertexPosition");
	gl->bind_attrib_location (program, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD,
	                          "vertexTexCoord");
	gl->bind_attrib_location (program, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL,
	                          "vertexNormal");
	gl->bind_attrib_location (program, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR,
	                          "vertexColor");
	gl->bind_attrib_location (program, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT,
	                          "vertexTangent");
	gl->bind_attrib_location (program, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2,
	                          "vertexTexCoord2");
	gl->link_program (program);
	gl->get_programiv (program, LRG_GL_LINK_STATUS, &status);
	if (!status)
	{
		gl->get_programiv (program, LRG_GL_INFO_LOG_LENGTH, &length);
		if (length > 1)
		{
			g_autofree gchar *log = g_malloc0 ((gsize) length + 1);
			g_autoptr(GPtrArray) parsed = NULL;
			guint i;

			gl->get_program_info_log (program, length, NULL, log);
			parsed = lrg_shader_parse_info_log (log, LRG_SHADER_STAGE_FRAGMENT, 0, source_name);
			for (i = 0; i < parsed->len; i++)
				g_ptr_array_add (diagnostics,
				                 lrg_shader_diagnostic_copy (g_ptr_array_index (parsed, i)));
		}
		else
		{
			g_ptr_array_add (diagnostics, diagnostic_new (LRG_SHADER_STAGE_FRAGMENT, 0, 0, TRUE,
			                                              "link failed", 0, source_name));
		}
	}
	gl->delete_program (program);
	return status != 0;
}

/* the GError text of @diagnostics (errors only, one per line) */
static gchar *
diagnostics_to_message (GPtrArray *diagnostics)
{
	g_autoptr(GString) text = g_string_new (NULL);
	guint i;

	for (i = 0; i < diagnostics->len; i++)
	{
		LrgShaderDiagnostic *diag = g_ptr_array_index (diagnostics, i);
		g_autofree gchar *line = NULL;

		if (!diag->is_error)
			continue;
		line = lrg_shader_diagnostic_to_string (diag);
		if (text->len > 0)
			g_string_append_c (text, '\n');
		g_string_append (text, line);
	}
	return g_string_free (g_steal_pointer (&text), FALSE);
}

/*
 * compile_desc:
 * Compiles @desc into a new GrlShader.  The diagnostic compile runs
 * first so that failures come with logs; @diagnostics receives every
 * message (warnings too).
 */
static GrlShader *
compile_desc (LrgShaderAsset  *self,
              ShaderDesc      *desc,
              GPtrArray       *diagnostics,
              GError         **error)
{
	g_autofree gchar *vs_text = NULL;
	g_autofree gchar *fs_text = NULL;
	g_autofree gchar *vs_name = NULL;
	g_autofree gchar *fs_name = NULL;
	GlFunctions gl;
	GrlShader *shader;
	Shader *handle;
	guint vs_preamble = 0;
	guint fs_preamble = 0;
	guint vs;
	guint fs;
	gboolean linked;

	/* a lit fragment stage must not redeclare what the library declares */
	fs_name = stage_source_name (self, desc, LRG_SHADER_STAGE_FRAGMENT);
	if (desc->lit &&
	    check_library_redeclarations (desc->source[LRG_SHADER_STAGE_FRAGMENT],
	                                  lrg_shader_asset_get_library_source (self), fs_name, diagnostics))
	{
		g_autofree gchar *message = diagnostics_to_message (diagnostics);

		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_COMPILE, "%s", message);
		return NULL;
	}

	if (!IsWindowReady ())
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_NO_CONTEXT,
		             "%s: compiling a shader needs an open window", desc->name);
		return NULL;
	}
	if (desc->source[LRG_SHADER_STAGE_FRAGMENT] == NULL)
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_PARSE,
		             "%s: the asset has no fragment stage", desc->name);
		return NULL;
	}

	vs_text = build_stage (self, desc, LRG_SHADER_STAGE_VERTEX, &vs_preamble);
	fs_text = build_stage (self, desc, LRG_SHADER_STAGE_FRAGMENT, &fs_preamble);
	vs_name = stage_source_name (self, desc, LRG_SHADER_STAGE_VERTEX);

	/* diagnostic compile + link: exact logs, nothing kept */
	if (gl_functions_load (&gl))
	{
		vs = diagnostic_compile (&gl, vs_text, LRG_SHADER_STAGE_VERTEX, vs_preamble, vs_name,
		                         diagnostics);
		fs = diagnostic_compile (&gl, fs_text, LRG_SHADER_STAGE_FRAGMENT, fs_preamble, fs_name,
		                         diagnostics);
		linked = FALSE;
		if (vs != 0 && fs != 0)
			linked = diagnostic_link (&gl, vs, fs, fs_name, diagnostics);
		if (vs != 0)
			gl.delete_shader (vs);
		if (fs != 0)
			gl.delete_shader (fs);
		if (vs == 0 || fs == 0 || !linked)
		{
			g_autofree gchar *message = diagnostics_to_message (diagnostics);

			g_set_error (error, LRG_SHADER_ASSET_ERROR,
			             (vs == 0 || fs == 0) ? LRG_SHADER_ASSET_ERROR_COMPILE
			                                  : LRG_SHADER_ASSET_ERROR_LINK,
			             "%s", message);
			return NULL;
		}
	}

	/* the program in use: raylib's, with its attribute and location setup */
	shader = grl_shader_new_from_memory (vs_text, fs_text, NULL);
	handle = shader != NULL ? grl_shader_get_handle (shader) : NULL;
	if (handle == NULL || handle->id == 0 || handle->id == rlGetShaderIdDefault ())
	{
		g_clear_object (&shader);
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_COMPILE,
		             "%s: the shader did not compile (see the raylib log)", fs_name);
		return NULL;
	}
	return shader;
}

/* loads the plain-path default textures of sampler uniforms */
static void
load_default_textures (LrgShaderAsset *self)
{
	guint i;

	g_hash_table_remove_all (self->default_textures);
	for (i = 0; i < self->desc->uniforms->len; i++)
	{
		LrgShaderUniform *u = g_ptr_array_index (self->desc->uniforms, i);
		g_autofree gchar *path = NULL;
		GrlTexture *texture;

		if (u->type != LRG_SHADER_UNIFORM_SAMPLER2D || u->texture == NULL ||
		    g_str_has_prefix (u->texture, "guid:"))
			continue;
		path = resolve_path (self->base_dir, u->texture);
		if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
		{
			lrg_message (LRG_LOG_DOMAIN_CORE, "shader asset '%s': default texture '%s' not found, using white",
			             self->desc->name, path);
			continue;
		}
		texture = grl_texture_new_from_file (path);
		if (texture == NULL || !grl_texture_is_valid (texture))
		{
			g_clear_object (&texture);
			continue;
		}
		g_hash_table_replace (self->default_textures, g_strdup (u->name), texture);
	}
}

/* installs @shader as the program in use */
static void
install_program (LrgShaderAsset *self,
                 GrlShader      *shader)
{
	gboolean was_compiled = self->shader != NULL;

	clear_program (self);
	self->shader = shader;
	load_default_textures (self);
	if (!was_compiled)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPILED]);
}

gboolean
lrg_shader_asset_compile (LrgShaderAsset  *self,
                          GError         **error)
{
	GrlShader *shader;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_ptr_array_set_size (self->diagnostics, 0);
	shader = compile_desc (self, self->desc, self->diagnostics, error);
	if (shader == NULL)
		return FALSE;
	install_program (self, shader);
	return TRUE;
}

gboolean
lrg_shader_asset_is_compiled (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	return self->shader != NULL;
}

GrlShader *
lrg_shader_asset_get_shader (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	return self->shader;
}

guint
lrg_shader_asset_get_shader_id (LrgShaderAsset *self)
{
	Shader *handle;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), 0);

	if (self->shader == NULL)
		return 0;
	handle = grl_shader_get_handle (self->shader);
	return handle != NULL ? handle->id : 0;
}

GPtrArray *
lrg_shader_asset_get_diagnostics (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), NULL);
	return self->diagnostics;
}

gint
lrg_shader_asset_get_uniform_location (LrgShaderAsset *self,
                                       const gchar    *name)
{
	gpointer cached;
	gint location;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), -1);
	g_return_val_if_fail (name != NULL, -1);

	if (self->shader == NULL)
		return -1;
	if (g_hash_table_lookup_extended (self->locations, name, NULL, &cached))
		return GPOINTER_TO_INT (cached) - 1;
	location = rlGetLocationUniform (lrg_shader_asset_get_shader_id (self), name);
	g_hash_table_insert (self->locations, g_strdup (name), GINT_TO_POINTER (location + 1));
	return location;
}

/* uploads @values (of @type) at @location of the bound program */
static void
upload_value (gint                  location,
              LrgShaderUniformType  type,
              const gfloat         *values)
{
	gint as_int;

	switch (type)
	{
	case LRG_SHADER_UNIFORM_FLOAT:
		rlSetUniform (location, values, RL_SHADER_UNIFORM_FLOAT, 1);
		break;
	case LRG_SHADER_UNIFORM_VEC2:
		rlSetUniform (location, values, RL_SHADER_UNIFORM_VEC2, 1);
		break;
	case LRG_SHADER_UNIFORM_VEC3:
		rlSetUniform (location, values, RL_SHADER_UNIFORM_VEC3, 1);
		break;
	case LRG_SHADER_UNIFORM_VEC4:
	case LRG_SHADER_UNIFORM_COLOR:
		rlSetUniform (location, values, RL_SHADER_UNIFORM_VEC4, 1);
		break;
	case LRG_SHADER_UNIFORM_INT:
	case LRG_SHADER_UNIFORM_BOOL:
		as_int = (gint) floorf (values[0] + 0.5f);
		rlSetUniform (location, &as_int, RL_SHADER_UNIFORM_INT, 1);
		break;
	case LRG_SHADER_UNIFORM_SAMPLER2D:
	default:
		break;
	}
}

/* binds @texture_id on *@unit for @location and advances the unit */
static void
bind_sampler (gint  location,
              guint texture_id,
              gint *unit)
{
	gint value = *unit;

	rlActiveTextureSlot (value);
	rlEnableTexture (texture_id != 0 ? texture_id : rlGetTextureIdDefault ());
	rlSetUniform (location, &value, RL_SHADER_UNIFORM_INT, 1);
	rlActiveTextureSlot (0);
	(*unit)++;
}

/* the GL id of a graylib texture */
static guint
texture_id_of (GrlTexture *texture)
{
	Texture2D *handle;

	if (texture == NULL)
		return 0;
	handle = grl_texture_get_handle (texture);
	return handle != NULL ? handle->id : 0;
}

gboolean
lrg_shader_asset_apply (LrgShaderAsset *self,
                        gint           *texture_unit)
{
	gint first_unit = LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT;
	gint *unit = texture_unit != NULL ? texture_unit : &first_unit;
	gint location;
	guint i;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);

	if (self->shader == NULL)
		return FALSE;
	rlEnableShader (lrg_shader_asset_get_shader_id (self));

	for (i = 0; i < self->desc->uniforms->len; i++)
	{
		LrgShaderUniform *u = g_ptr_array_index (self->desc->uniforms, i);
		UniformValue *value = g_hash_table_lookup (self->values, u->name);

		location = lrg_shader_asset_get_uniform_location (self, u->name);
		if (location < 0 || value == NULL)
			continue;
		if (u->type == LRG_SHADER_UNIFORM_SAMPLER2D)
		{
			GrlTexture *texture = value->texture != NULL
			                      ? value->texture
			                      : g_hash_table_lookup (self->default_textures, u->name);

			bind_sampler (location, texture_id_of (texture), unit);
		}
		else
		{
			upload_value (location, u->type, value->value);
		}
	}

	location = lrg_shader_asset_get_uniform_location (self, "lrgTime");
	if (location >= 0)
		rlSetUniform (location, &self->time, RL_SHADER_UNIFORM_FLOAT, 1);
	return TRUE;
}

gboolean
lrg_shader_asset_apply_uniform (LrgShaderAsset  *self,
                                const gchar     *name,
                                const gfloat    *values,
                                guint            n_values,
                                GError         **error)
{
	LrgShaderUniform *u;
	gint location;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (values != NULL || n_values == 0, FALSE);

	u = shader_desc_find_uniform (self->desc, name, NULL);
	if (u == NULL || u->type == LRG_SHADER_UNIFORM_SAMPLER2D ||
	    lrg_shader_uniform_type_get_components (u->type) != n_values)
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_UNIFORM,
		             "%s: no %u-component uniform '%s'", self->desc->name, n_values, name);
		return FALSE;
	}
	if (self->shader == NULL)
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_NO_CONTEXT,
		             "%s: not compiled", self->desc->name);
		return FALSE;
	}
	location = lrg_shader_asset_get_uniform_location (self, name);
	if (location < 0)
		return TRUE;
	rlEnableShader (lrg_shader_asset_get_shader_id (self));
	upload_value (location, u->type, values);
	return TRUE;
}

gboolean
lrg_shader_asset_apply_uniform_texture (LrgShaderAsset  *self,
                                        const gchar     *name,
                                        guint            texture_id,
                                        gint            *texture_unit,
                                        GError         **error)
{
	gint first_unit = LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT;
	gint *unit = texture_unit != NULL ? texture_unit : &first_unit;
	LrgShaderUniform *u;
	gint location;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	u = shader_desc_find_uniform (self->desc, name, NULL);
	if (u == NULL || u->type != LRG_SHADER_UNIFORM_SAMPLER2D)
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_UNIFORM,
		             "%s: no sampler2D uniform '%s'", self->desc->name, name);
		return FALSE;
	}
	if (self->shader == NULL)
	{
		g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_NO_CONTEXT,
		             "%s: not compiled", self->desc->name);
		return FALSE;
	}
	location = lrg_shader_asset_get_uniform_location (self, name);
	if (location < 0)
		return TRUE;
	rlEnableShader (lrg_shader_asset_get_shader_id (self));
	bind_sampler (location, texture_id, unit);
	return TRUE;
}

gboolean
lrg_shader_asset_bind_lights (LrgShaderAsset *self,
                              LrgLightSet    *lights)
{
	gboolean known = FALSE;
	guint i;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_LIGHT_SET (lights), FALSE);

	if (self->shader == NULL)
		return FALSE;

	/* remember the light set so it can forget this program later */
	for (i = 0; i < self->light_sets->len && !known; i++)
	{
		g_autoptr(LrgLightSet) other = g_weak_ref_get (g_ptr_array_index (self->light_sets, i));

		known = other == lights;
	}
	if (!known)
	{
		GWeakRef *ref = g_new0 (GWeakRef, 1);

		g_weak_ref_init (ref, lights);
		g_ptr_array_add (self->light_sets, ref);
	}
	return lrg_light_set_bind_shader (lights, self->shader);
}

void
lrg_shader_asset_release (LrgShaderAsset *self)
{
	gboolean was_compiled;

	g_return_if_fail (LRG_IS_SHADER_ASSET (self));

	was_compiled = self->shader != NULL;
	clear_program (self);
	if (was_compiled)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPILED]);
}

/* ========================================================================== */
/* Hot reload                                                                 */
/* ========================================================================== */

gboolean
lrg_shader_asset_reload (LrgShaderAsset  *self,
                         GError         **error)
{
	g_autoptr(ShaderDesc) fresh = NULL;
	g_autoptr(GError) local = NULL;
	g_autoptr(GPtrArray) diagnostics = NULL;
	GrlShader *shader = NULL;
	ShaderDesc *old;

	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* an asset built in code has nothing to re-read: recompile only */
	if (self->path == NULL)
	{
		if (self->shader != NULL && !lrg_shader_asset_compile (self, &local))
			goto failed;
		g_signal_emit (self, signals[SIGNAL_RELOADED], 0);
		return TRUE;
	}

	fresh = desc_from_file (self->path, &local);
	if (fresh == NULL)
		goto failed;

	/* keep-last-good: compile before touching anything live */
	if (self->shader != NULL)
	{
		diagnostics = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_shader_diagnostic_free);
		shader = compile_desc (self, fresh, diagnostics, &local);
		g_ptr_array_set_size (self->diagnostics, 0);
		g_ptr_array_extend_and_steal (self->diagnostics, g_steal_pointer (&diagnostics));
		if (shader == NULL)
			goto failed;
	}

	old = self->desc;
	self->desc = g_steal_pointer (&fresh);
	shader_desc_free (old);
	merge_values (self);
	if (shader != NULL)
		install_program (self, shader);

	/* stage paths may have changed: follow the new files */
	if (self->watch)
	{
		stop_watching (self);
		start_watching (self, NULL);
	}

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
	g_signal_emit (self, signals[SIGNAL_RELOADED], 0);
	return TRUE;

failed:
	/* not a warning: the failure is reported through the signal and
	 * @error, and editing a file through broken states is normal */
	lrg_info (LRG_LOG_DOMAIN_CORE, "shader asset '%s' reload failed: %s",
	             self->desc->name, local->message);
	g_signal_emit (self, signals[SIGNAL_RELOAD_FAILED], 0, local);
	g_propagate_error (error, g_steal_pointer (&local));
	return FALSE;
}

/* debounced reload */
static gboolean
pending_reload (gpointer data)
{
	LrgShaderAsset *self = LRG_SHADER_ASSET (data);
	g_autoptr(LrgShaderAsset) keep = g_object_ref (self);
	GSource *source = self->pending;

	self->pending = NULL;
	g_source_unref (source);
	lrg_shader_asset_reload (self, NULL);
	return G_SOURCE_REMOVE;
}

static gboolean
is_watched_file (LrgShaderAsset *self,
                 GFile          *file)
{
	guint i;

	if (file == NULL)
		return FALSE;
	for (i = 0; i < self->watched_files->len; i++)
		if (g_file_equal (file, g_ptr_array_index (self->watched_files, i)))
			return TRUE;
	return FALSE;
}

static void
on_file_changed (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event_type,
                 gpointer           data)
{
	LrgShaderAsset *self = LRG_SHADER_ASSET (data);

	if (!is_watched_file (self, file) && !is_watched_file (self, other_file))
		return;

	switch (event_type)
	{
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_MOVED:
	case G_FILE_MONITOR_EVENT_RENAMED:
	case G_FILE_MONITOR_EVENT_MOVED_IN:
		/* editors write in bursts: coalesce for 100 ms */
		if (self->pending != NULL)
		{
			g_source_destroy (self->pending);
			g_source_unref (self->pending);
		}
		self->pending = g_timeout_source_new (100);
		g_source_set_callback (self->pending, pending_reload, self, NULL);
		g_source_attach (self->pending, self->context);
		break;
	default:
		break;
	}
}

static void
stop_watching (LrgShaderAsset *self)
{
	guint i;

	if (self->pending != NULL)
	{
		g_source_destroy (self->pending);
		g_clear_pointer (&self->pending, g_source_unref);
	}
	for (i = 0; i < self->monitors->len; i++)
	{
		GFileMonitor *monitor = g_ptr_array_index (self->monitors, i);

		g_signal_handlers_disconnect_by_data (monitor, self);
		g_file_monitor_cancel (monitor);
	}
	g_ptr_array_set_size (self->monitors, 0);
	g_ptr_array_set_size (self->watched_files, 0);
}

/* monitors the directories of the document and its stage files */
static gboolean
start_watching (LrgShaderAsset  *self,
                GError         **error)
{
	g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_object_unref);
	guint s;
	guint i;
	guint d;

	g_ptr_array_add (self->watched_files, g_file_new_for_path (self->path));
	for (s = 0; s < N_STAGES; s++)
		if (self->desc->source_path[s] != NULL)
		{
			g_autofree gchar *path = resolve_path (self->base_dir, self->desc->source_path[s]);

			g_ptr_array_add (self->watched_files, g_file_new_for_path (path));
		}

	if (self->context == NULL)
		self->context = g_main_context_ref_thread_default ();

	for (i = 0; i < self->watched_files->len; i++)
	{
		g_autoptr(GFile) parent = g_file_get_parent (g_ptr_array_index (self->watched_files, i));
		gboolean seen = FALSE;
		GFileMonitor *monitor;

		for (d = 0; d < dirs->len && !seen; d++)
			seen = g_file_equal (parent, g_ptr_array_index (dirs, d));
		if (seen)
			continue;
		monitor = g_file_monitor_directory (parent, G_FILE_MONITOR_WATCH_MOVES, NULL, error);
		if (monitor == NULL)
		{
			stop_watching (self);
			return FALSE;
		}
		g_signal_connect (monitor, "changed", G_CALLBACK (on_file_changed), self);
		g_ptr_array_add (self->monitors, monitor);
		g_ptr_array_add (dirs, g_object_ref (parent));
	}
	return TRUE;
}

gboolean
lrg_shader_asset_set_watch (LrgShaderAsset  *self,
                            gboolean         watch,
                            GError         **error)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	watch = !!watch;
	if (self->watch == watch)
		return TRUE;
	if (watch)
	{
		if (self->path == NULL)
		{
			g_set_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_IO,
			             "%s: only an asset loaded from a file can be watched", self->desc->name);
			return FALSE;
		}
		if (!start_watching (self, error))
			return FALSE;
	}
	else
	{
		stop_watching (self);
	}
	self->watch = watch;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WATCH]);
	return TRUE;
}

gboolean
lrg_shader_asset_get_watch (LrgShaderAsset *self)
{
	g_return_val_if_fail (LRG_IS_SHADER_ASSET (self), FALSE);
	return self->watch;
}
