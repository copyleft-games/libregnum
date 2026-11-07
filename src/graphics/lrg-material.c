/* lrg-material.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMaterial: the runtime material asset (`.rmat`).
 *
 * Storage model: every value (a "param") has a set bit. A getter walks
 * self -> parent -> grandparent ... and returns the first material that
 * sets the value, else the default. Texture slots and custom uniforms
 * follow the same rule. Saving writes only what the material sets
 * itself, so an instance file holds just its overrides.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <raylib.h>
#include <rlgl.h>
#include <yaml-glib.h>

#include "lrg-material.h"
#include "../core/lrg-asset-manager.h"

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

#define DEFINE_ENUM_TYPE(func, name, ...) \
	GType \
	func (void) \
	{ \
		static gsize type_id = 0; \
		if (g_once_init_enter (&type_id)) \
		{ \
			static const GEnumValue values[] = { __VA_ARGS__, { 0, NULL, NULL } }; \
			GType id = g_enum_register_static (g_intern_static_string (name), values); \
			g_once_init_leave (&type_id, id); \
		} \
		return (GType) type_id; \
	}

DEFINE_ENUM_TYPE (lrg_shading_model_get_type, "LrgShadingModel",
	{ LRG_SHADING_MODEL_LIT, "LRG_SHADING_MODEL_LIT", "lit" },
	{ LRG_SHADING_MODEL_UNLIT, "LRG_SHADING_MODEL_UNLIT", "unlit" },
	{ LRG_SHADING_MODEL_CUSTOM, "LRG_SHADING_MODEL_CUSTOM", "custom" })

DEFINE_ENUM_TYPE (lrg_material_blend_mode_get_type, "LrgMaterialBlendMode",
	{ LRG_MATERIAL_BLEND_OPAQUE, "LRG_MATERIAL_BLEND_OPAQUE", "opaque" },
	{ LRG_MATERIAL_BLEND_CUTOUT, "LRG_MATERIAL_BLEND_CUTOUT", "cutout" },
	{ LRG_MATERIAL_BLEND_ALPHA, "LRG_MATERIAL_BLEND_ALPHA", "alpha" },
	{ LRG_MATERIAL_BLEND_ADDITIVE, "LRG_MATERIAL_BLEND_ADDITIVE", "additive" },
	{ LRG_MATERIAL_BLEND_MULTIPLY, "LRG_MATERIAL_BLEND_MULTIPLY", "multiply" })

DEFINE_ENUM_TYPE (lrg_material_cull_mode_get_type, "LrgMaterialCullMode",
	{ LRG_MATERIAL_CULL_BACK, "LRG_MATERIAL_CULL_BACK", "back" },
	{ LRG_MATERIAL_CULL_FRONT, "LRG_MATERIAL_CULL_FRONT", "front" },
	{ LRG_MATERIAL_CULL_NONE, "LRG_MATERIAL_CULL_NONE", "none" })

DEFINE_ENUM_TYPE (lrg_material_render_queue_get_type, "LrgMaterialRenderQueue",
	{ LRG_MATERIAL_RENDER_QUEUE_AUTO, "LRG_MATERIAL_RENDER_QUEUE_AUTO", "auto" },
	{ LRG_MATERIAL_RENDER_QUEUE_OPAQUE, "LRG_MATERIAL_RENDER_QUEUE_OPAQUE", "opaque" },
	{ LRG_MATERIAL_RENDER_QUEUE_CUTOUT, "LRG_MATERIAL_RENDER_QUEUE_CUTOUT", "cutout" },
	{ LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT, "LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT", "transparent" })

DEFINE_ENUM_TYPE (lrg_material_slot_get_type, "LrgMaterialSlot",
	{ LRG_MATERIAL_SLOT_ALBEDO, "LRG_MATERIAL_SLOT_ALBEDO", "albedo" },
	{ LRG_MATERIAL_SLOT_NORMAL, "LRG_MATERIAL_SLOT_NORMAL", "normal" },
	{ LRG_MATERIAL_SLOT_ORM, "LRG_MATERIAL_SLOT_ORM", "orm" },
	{ LRG_MATERIAL_SLOT_EMISSION, "LRG_MATERIAL_SLOT_EMISSION", "emission" },
	{ LRG_MATERIAL_SLOT_HEIGHT, "LRG_MATERIAL_SLOT_HEIGHT", "height" },
	{ LRG_MATERIAL_SLOT_METALLIC_GLOSS, "LRG_MATERIAL_SLOT_METALLIC_GLOSS", "metallic-gloss" },
	{ LRG_MATERIAL_N_SLOTS, "LRG_MATERIAL_N_SLOTS", "n-slots" })

DEFINE_ENUM_TYPE (lrg_material_uniform_type_get_type, "LrgMaterialUniformType",
	{ LRG_MATERIAL_UNIFORM_FLOAT, "LRG_MATERIAL_UNIFORM_FLOAT", "float" },
	{ LRG_MATERIAL_UNIFORM_VEC2, "LRG_MATERIAL_UNIFORM_VEC2", "vec2" },
	{ LRG_MATERIAL_UNIFORM_VEC3, "LRG_MATERIAL_UNIFORM_VEC3", "vec3" },
	{ LRG_MATERIAL_UNIFORM_VEC4, "LRG_MATERIAL_UNIFORM_VEC4", "vec4" },
	{ LRG_MATERIAL_UNIFORM_COLOR, "LRG_MATERIAL_UNIFORM_COLOR", "color" },
	{ LRG_MATERIAL_UNIFORM_INT, "LRG_MATERIAL_UNIFORM_INT", "int" },
	{ LRG_MATERIAL_UNIFORM_TEXTURE, "LRG_MATERIAL_UNIFORM_TEXTURE", "texture" })

DEFINE_ENUM_TYPE (lrg_material_error_get_type, "LrgMaterialError",
	{ LRG_MATERIAL_ERROR_PARSE, "LRG_MATERIAL_ERROR_PARSE", "parse" },
	{ LRG_MATERIAL_ERROR_INVALID, "LRG_MATERIAL_ERROR_INVALID", "invalid" },
	{ LRG_MATERIAL_ERROR_VERSION, "LRG_MATERIAL_ERROR_VERSION", "version" },
	{ LRG_MATERIAL_ERROR_NOT_FOUND, "LRG_MATERIAL_ERROR_NOT_FOUND", "not-found" },
	{ LRG_MATERIAL_ERROR_CYCLE, "LRG_MATERIAL_ERROR_CYCLE", "cycle" })

G_DEFINE_QUARK (lrg-material-error-quark, lrg_material_error)

/* ==========================================================================
 * Parameter table
 * ========================================================================== */

typedef enum
{
	P_SHADING_MODEL,
	P_SHADER,
	P_BASE_COLOR,
	P_METALLIC,
	P_ROUGHNESS,
	P_OCCLUSION,
	P_NORMAL_SCALE,
	P_EMISSION_COLOR,
	P_EMISSION_STRENGTH,
	P_ALPHA_CUTOFF,
	P_HEIGHT_SCALE,
	P_BLEND,
	P_CULL,
	P_DEPTH_TEST,
	P_DEPTH_WRITE,
	P_DOUBLE_SIDED,
	P_RENDER_QUEUE,
	N_PARAMS
} ParamId;

typedef enum
{
	KIND_ENUM,
	KIND_STRING,
	KIND_COLOR4,
	KIND_COLOR3,
	KIND_FLOAT,
	KIND_BOOL
} ParamKind;

typedef struct
{
	const gchar *key;       /* the `.rmat` key (and override name) */
	ParamKind    kind;
	gfloat       def[4];    /* default (floats, colours, bools as 0/1, enums) */
	gfloat       min;       /* KIND_FLOAT clamp */
	gfloat       max;
	gboolean     in_state;  /* written under render_state: */
} ParamInfo;

static const ParamInfo param_info[N_PARAMS] = {
	{ "shading_model",      KIND_ENUM,   { 0.0f }, 0.0f, 0.0f, FALSE },
	{ "shader",             KIND_STRING, { 0.0f }, 0.0f, 0.0f, FALSE },
	{ "base_color",         KIND_COLOR4, { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f, FALSE },
	{ "metallic",           KIND_FLOAT,  { LRG_MATERIAL_DEFAULT_METALLIC }, 0.0f, 1.0f, FALSE },
	{ "roughness",          KIND_FLOAT,  { LRG_MATERIAL_DEFAULT_ROUGHNESS }, 0.0f, 1.0f, FALSE },
	{ "occlusion_strength", KIND_FLOAT,  { LRG_MATERIAL_DEFAULT_OCCLUSION_STRENGTH }, 0.0f, 1.0f, FALSE },
	{ "normal_scale",       KIND_FLOAT,  { LRG_MATERIAL_DEFAULT_NORMAL_SCALE }, 0.0f, 8.0f, FALSE },
	{ "emission_color",     KIND_COLOR3, { 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f, FALSE },
	{ "emission_strength",  KIND_FLOAT,  { 0.0f }, 0.0f, 1000.0f, FALSE },
	{ "alpha_cutoff",       KIND_FLOAT,  { LRG_MATERIAL_DEFAULT_ALPHA_CUTOFF }, 0.0f, 1.0f, FALSE },
	{ "height_scale",       KIND_FLOAT,  { LRG_MATERIAL_DEFAULT_HEIGHT_SCALE }, 0.0f, 1.0f, FALSE },
	{ "blend",              KIND_ENUM,   { 0.0f }, 0.0f, 0.0f, TRUE },
	{ "cull",               KIND_ENUM,   { 0.0f }, 0.0f, 0.0f, TRUE },
	{ "depth_test",         KIND_BOOL,   { 1.0f }, 0.0f, 0.0f, TRUE },
	{ "depth_write",        KIND_BOOL,   { 1.0f }, 0.0f, 0.0f, TRUE },
	{ "double_sided",       KIND_BOOL,   { 0.0f }, 0.0f, 0.0f, TRUE },
	{ "render_queue",       KIND_ENUM,   { 0.0f }, 0.0f, 0.0f, FALSE },
};

/* The `textures:` keys, in LrgMaterialSlot order. */
static const gchar * const slot_keys[LRG_MATERIAL_N_SLOTS] = {
	"albedo", "normal", "orm", "emission", "height", "metallic_gloss"
};

/* The texture unit (= raylib material map index) of each slot. */
static const gint slot_units[LRG_MATERIAL_N_SLOTS] = {
	LRG_MATERIAL_UNIT_ALBEDO, LRG_MATERIAL_UNIT_NORMAL, LRG_MATERIAL_UNIT_ORM,
	LRG_MATERIAL_UNIT_EMISSION, LRG_MATERIAL_UNIT_HEIGHT, LRG_MATERIAL_UNIT_METALLIC_GLOSS
};

/* The sampler uniform of each slot. */
static const gchar * const slot_samplers[LRG_MATERIAL_N_SLOTS] = {
	"texture0", "normalMap", "ormMap", "emissionMap", "heightMap", "metallicMap"
};

static GType
param_enum_type (ParamId id)
{
	switch (id)
	{
	case P_SHADING_MODEL: return LRG_TYPE_SHADING_MODEL;
	case P_BLEND:         return LRG_TYPE_MATERIAL_BLEND_MODE;
	case P_CULL:          return LRG_TYPE_MATERIAL_CULL_MODE;
	case P_RENDER_QUEUE:  return LRG_TYPE_MATERIAL_RENDER_QUEUE;
	default:              return G_TYPE_INVALID;
	}
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

typedef struct
{
	gchar      *ref;       /* nullable: NULL with set = "no texture" */
	gboolean    set;       /* the reference is this material's own */
	GrlTexture *runtime;   /* lrg_material_set_texture(), wins */
	GrlTexture *loaded;    /* the reference, loaded (owned ref) */
	gboolean    failed;    /* the reference did not load; not retried */
} Slot;

typedef struct
{
	LrgMaterialUniformType  type;
	gfloat                  v[4];
	gchar                  *ref;      /* texture uniforms */
	GrlTexture             *loaded;
	gboolean                failed;
} Uniform;

/* Uniform locations of the contract, cached per program. */
enum
{
	LOC_COL_DIFFUSE,
	LOC_PBR_PARAMS,
	LOC_MAP_FLAGS,
	LOC_EMISSION_OWN,
	LOC_ALPHA_CUTOFF,
	LOC_HEIGHT_SCALE,
	LOC_SAMPLER_FIRST,
	N_LOCS = LOC_SAMPLER_FIRST + LRG_MATERIAL_N_SLOTS
};

struct _LrgMaterial
{
	GObject          parent_instance;

	gchar           *name;
	gchar           *path;
	gchar           *base_dir;
	LrgAssetManager *manager;      /* weak */

	LrgMaterial     *parent;
	gchar           *parent_ref;
	gulong           parent_changed_id;

	guint32          set;          /* 1 << ParamId */
	gfloat           values[N_PARAMS][4];
	gchar           *shader;

	Slot             slots[LRG_MATERIAL_N_SLOTS];
	GHashTable      *uniforms;     /* name -> Uniform* (own) */
	YamlMapping     *extra;        /* unknown top-level keys, nullable */

	guint            loc_shader;   /* program the locations belong to */
	gint             locs[N_LOCS];
	GHashTable      *custom_locs;  /* name -> GINT_TO_POINTER (loc + 2) */
};

G_DEFINE_FINAL_TYPE (LrgMaterial, lrg_material, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_PATH,
	PROP_PARENT,
	PROP_ASSET_MANAGER,
	PROP_SHADING_MODEL,
	PROP_SHADER,
	PROP_BASE_COLOR_R,
	PROP_BASE_COLOR_G,
	PROP_BASE_COLOR_B,
	PROP_BASE_COLOR_A,
	PROP_METALLIC,
	PROP_ROUGHNESS,
	PROP_OCCLUSION_STRENGTH,
	PROP_NORMAL_SCALE,
	PROP_EMISSION_COLOR_R,
	PROP_EMISSION_COLOR_G,
	PROP_EMISSION_COLOR_B,
	PROP_EMISSION_STRENGTH,
	PROP_ALPHA_CUTOFF,
	PROP_HEIGHT_SCALE,
	PROP_BLEND_MODE,
	PROP_CULL_MODE,
	PROP_DEPTH_TEST,
	PROP_DEPTH_WRITE,
	PROP_DOUBLE_SIDED,
	PROP_RENDER_QUEUE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* The first property of each param, and how many (colours have 3 / 4). */
static const struct { gint prop; gint count; } param_props[N_PARAMS] = {
	{ PROP_SHADING_MODEL, 1 },
	{ PROP_SHADER, 1 },
	{ PROP_BASE_COLOR_R, 4 },
	{ PROP_METALLIC, 1 },
	{ PROP_ROUGHNESS, 1 },
	{ PROP_OCCLUSION_STRENGTH, 1 },
	{ PROP_NORMAL_SCALE, 1 },
	{ PROP_EMISSION_COLOR_R, 3 },
	{ PROP_EMISSION_STRENGTH, 1 },
	{ PROP_ALPHA_CUTOFF, 1 },
	{ PROP_HEIGHT_SCALE, 1 },
	{ PROP_BLEND_MODE, 1 },
	{ PROP_CULL_MODE, 1 },
	{ PROP_DEPTH_TEST, 1 },
	{ PROP_DEPTH_WRITE, 1 },
	{ PROP_DOUBLE_SIDED, 1 },
	{ PROP_RENDER_QUEUE, 1 },
};

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static void
uniform_free (gpointer data)
{
	Uniform *u = data;

	g_free (u->ref);
	g_clear_object (&u->loaded);
	g_free (u);
}

static void
slot_clear (Slot *slot)
{
	g_clear_pointer (&slot->ref, g_free);
	g_clear_object (&slot->runtime);
	g_clear_object (&slot->loaded);
	slot->set = FALSE;
	slot->failed = FALSE;
}

/* Drops every loaded texture (slots and texture uniforms) so they load
 * again, for example through a new asset manager. */
static void
drop_loaded_textures (LrgMaterial *self)
{
	GHashTableIter iter;
	gpointer       value;
	guint          i;

	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
	{
		g_clear_object (&self->slots[i].loaded);
		self->slots[i].failed = FALSE;
	}
	g_hash_table_iter_init (&iter, self->uniforms);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		Uniform *u = value;

		g_clear_object (&u->loaded);
		u->failed = FALSE;
	}
}

static void
emit_changed (LrgMaterial *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* Notifies the properties of @id and emits ::changed. */
static void
param_changed (LrgMaterial *self,
               ParamId      id)
{
	gint k;

	for (k = 0; k < param_props[id].count; k++)
		g_object_notify_by_pspec (G_OBJECT (self), properties[param_props[id].prop + k]);
	emit_changed (self);
}

/* The material of the chain that sets @id, or NULL (default applies). */
static LrgMaterial *
param_owner (LrgMaterial *self,
             ParamId      id)
{
	LrgMaterial *m;

	for (m = self; m != NULL; m = m->parent)
		if (m->set & (1u << id))
			return m;
	return NULL;
}

/* The effective float components of @id. */
static const gfloat *
param_values (LrgMaterial *self,
              ParamId      id)
{
	LrgMaterial *owner = param_owner (self, id);

	return owner != NULL ? owner->values[id] : param_info[id].def;
}

static gint
param_int (LrgMaterial *self,
           ParamId      id)
{
	return (gint) param_values (self, id)[0];
}

/* Sets @n components of @id (missing ones keep their value); clamps
 * floats and colours to the param's range. */
static void
set_param (LrgMaterial  *self,
           ParamId       id,
           const gfloat *v,
           guint         n)
{
	gfloat   next[4];
	guint    i;
	gboolean was_set = (self->set & (1u << id)) != 0;

	memcpy (next, was_set ? self->values[id] : param_values (self, id), sizeof next);
	for (i = 0; i < n && i < 4; i++)
	{
		gfloat x = isfinite (v[i]) ? v[i] : 0.0f;

		if (param_info[id].kind == KIND_FLOAT || param_info[id].kind == KIND_COLOR3 ||
		    param_info[id].kind == KIND_COLOR4)
			x = CLAMP (x, param_info[id].min, param_info[id].max);
		next[i] = x;
	}
	if (was_set && memcmp (next, self->values[id], sizeof next) == 0)
		return;
	memcpy (self->values[id], next, sizeof next);
	self->set |= 1u << id;
	param_changed (self, id);
}

static void
set_param_int (LrgMaterial *self,
               ParamId      id,
               gint         value)
{
	gfloat v = (gfloat) value;

	set_param (self, id, &v, 1);
}

/* Parses a slot key ("albedo", "metallic_gloss"); -1 when unknown. */
static gint
slot_from_key (const gchar *key)
{
	guint i;

	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
		if (g_strcmp0 (key, slot_keys[i]) == 0)
			return (gint) i;
	return -1;
}

static gint
param_from_key (const gchar *key)
{
	guint i;

	for (i = 0; i < N_PARAMS; i++)
		if (g_strcmp0 (key, param_info[i].key) == 0)
			return (gint) i;
	return -1;
}

static guint
uniform_components (LrgMaterialUniformType type)
{
	switch (type)
	{
	case LRG_MATERIAL_UNIFORM_VEC2:  return 2;
	case LRG_MATERIAL_UNIFORM_VEC3:  return 3;
	case LRG_MATERIAL_UNIFORM_VEC4:
	case LRG_MATERIAL_UNIFORM_COLOR: return 4;
	case LRG_MATERIAL_UNIFORM_TEXTURE: return 0;
	case LRG_MATERIAL_UNIFORM_FLOAT:
	case LRG_MATERIAL_UNIFORM_INT:
	default:                         return 1;
	}
}

/* The Uniform @name of the chain, or NULL. */
static Uniform *
uniform_lookup (LrgMaterial  *self,
                const gchar  *name,
                LrgMaterial **owner)
{
	LrgMaterial *m;

	for (m = self; m != NULL; m = m->parent)
	{
		Uniform *u = g_hash_table_lookup (m->uniforms, name);

		if (u != NULL)
		{
			if (owner != NULL)
				*owner = m;
			return u;
		}
	}
	return NULL;
}

/* The material of the chain whose slot @slot decides the texture. */
static LrgMaterial *
slot_owner (LrgMaterial     *self,
            LrgMaterialSlot  slot)
{
	LrgMaterial *m;

	for (m = self; m != NULL; m = m->parent)
		if (m->slots[slot].set || m->slots[slot].runtime != NULL)
			return m;
	return NULL;
}

static const gchar *
enum_nick (GType type,
           gint  value)
{
	GEnumClass *klass = g_type_class_ref (type);
	GEnumValue *ev = g_enum_get_value (klass, value);
	const gchar *nick = ev != NULL ? ev->value_nick : NULL;

	g_type_class_unref (klass);
	return nick;
}

static gboolean
enum_from_nick (GType        type,
                const gchar *nick,
                gint        *out)
{
	GEnumClass *klass;
	GEnumValue *ev;

	if (nick == NULL)
		return FALSE;
	klass = g_type_class_ref (type);
	ev = g_enum_get_value_by_nick (klass, nick);
	if (ev != NULL)
		*out = ev->value;
	g_type_class_unref (klass);
	return ev != NULL;
}

/* ==========================================================================
 * Reference resolution and texture loading
 * ========================================================================== */

gchar *
lrg_material_resolve_reference (LrgMaterial *self,
                                const gchar *ref)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	g_return_val_if_fail (ref != NULL, NULL);

	/* The asset manager knows guids and search paths. */
	if (self->manager != NULL)
	{
		gchar *path = lrg_asset_manager_resolve_reference (self->manager, ref);

		if (path != NULL)
			return path;
	}
	if (g_str_has_prefix (ref, LRG_ASSET_GUID_PREFIX))
		return NULL;

	/* The fallback resolver: absolute as is, otherwise the base dir. */
	if (g_path_is_absolute (ref))
		return g_file_test (ref, G_FILE_TEST_EXISTS) ? g_canonicalize_filename (ref, NULL) : NULL;
	{
		g_autofree gchar *path = g_canonicalize_filename (ref, self->base_dir);

		if (g_file_test (path, G_FILE_TEST_EXISTS))
			return g_steal_pointer (&path);
	}
	return NULL;
}

/* Loads @ref as a texture for @owner (whose manager and base dir apply).
 * Returns a new reference, or NULL with @error set. */
static GrlTexture *
load_texture_ref (LrgMaterial  *owner,
                  const gchar  *ref,
                  GError      **error)
{
	g_autofree gchar *path = NULL;
	GrlTexture       *texture;

	if (!IsWindowReady ())
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
		                     "A graphics context is required to load material textures");
		return NULL;
	}
	if (owner->manager != NULL)
	{
		texture = lrg_asset_manager_load_texture (owner->manager, ref, NULL);
		if (texture != NULL)
			return g_object_ref (texture);
	}
	path = lrg_material_resolve_reference (owner, ref);
	if (path == NULL)
	{
		g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_NOT_FOUND,
		             "Material texture not found: %s", ref);
		return NULL;
	}
	texture = grl_texture_new_from_file (path);
	if (texture == NULL || !grl_texture_is_valid (texture))
	{
		g_clear_object (&texture);
		g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_NOT_FOUND,
		             "Material texture could not be loaded: %s", path);
		return NULL;
	}
	return texture;
}

/* The texture of @slot without loading anything (headless). */
static GrlTexture *
peek_texture (LrgMaterial     *self,
              LrgMaterialSlot  slot)
{
	LrgMaterial *owner = slot_owner (self, slot);

	if (owner == NULL)
		return NULL;
	if (owner->slots[slot].runtime != NULL)
		return owner->slots[slot].runtime;
	return owner->slots[slot].loaded;
}

/* Loads the slot of @owner once; a failure is remembered (except for a
 * missing graphics context, which may come later). */
static gboolean
ensure_slot_loaded (LrgMaterial      *owner,
                    LrgMaterialSlot   slot,
                    GError          **error)
{
	Slot   *s = &owner->slots[slot];
	GError *local = NULL;

	if (s->runtime != NULL || s->loaded != NULL || s->ref == NULL)
		return TRUE;
	if (s->failed)
	{
		g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_NOT_FOUND,
		             "Material texture not found: %s", s->ref);
		return FALSE;
	}
	s->loaded = load_texture_ref (owner, s->ref, &local);
	if (s->loaded == NULL)
	{
		if (!g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED))
		{
			s->failed = TRUE;
			g_debug ("LrgMaterial: %s", local->message);
		}
		g_propagate_error (error, local);
		return FALSE;
	}
	return TRUE;
}

static gboolean
ensure_uniform_loaded (LrgMaterial  *owner,
                       Uniform      *u,
                       GError      **error)
{
	GError *local = NULL;

	if (u->type != LRG_MATERIAL_UNIFORM_TEXTURE || u->ref == NULL || u->loaded != NULL)
		return TRUE;
	if (u->failed)
	{
		g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_NOT_FOUND,
		             "Material texture not found: %s", u->ref);
		return FALSE;
	}
	u->loaded = load_texture_ref (owner, u->ref, &local);
	if (u->loaded == NULL)
	{
		if (!g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED))
			u->failed = TRUE;
		g_propagate_error (error, local);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
on_parent_changed (LrgMaterial *parent,
                   gpointer     user_data)
{
	emit_changed (LRG_MATERIAL (user_data));
}

static void
lrg_material_dispose (GObject *object)
{
	LrgMaterial *self = LRG_MATERIAL (object);
	guint        i;

	if (self->parent != NULL && self->parent_changed_id != 0)
		g_signal_handler_disconnect (self->parent, self->parent_changed_id);
	self->parent_changed_id = 0;
	g_clear_object (&self->parent);
	if (self->manager != NULL)
		g_object_remove_weak_pointer (G_OBJECT (self->manager), (gpointer *) &self->manager);
	self->manager = NULL;
	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
		slot_clear (&self->slots[i]);
	if (self->uniforms != NULL)
		g_hash_table_remove_all (self->uniforms);

	G_OBJECT_CLASS (lrg_material_parent_class)->dispose (object);
}

static void
lrg_material_finalize (GObject *object)
{
	LrgMaterial *self = LRG_MATERIAL (object);

	g_free (self->name);
	g_free (self->path);
	g_free (self->base_dir);
	g_free (self->parent_ref);
	g_free (self->shader);
	g_clear_pointer (&self->uniforms, g_hash_table_unref);
	g_clear_pointer (&self->custom_locs, g_hash_table_unref);
	g_clear_pointer (&self->extra, yaml_mapping_unref);

	G_OBJECT_CLASS (lrg_material_parent_class)->finalize (object);
}

static void
lrg_material_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgMaterial *self = LRG_MATERIAL (object);

	switch (prop_id)
	{
	case PROP_NAME:          g_value_set_string (value, self->name); break;
	case PROP_PATH:          g_value_set_string (value, self->path); break;
	case PROP_PARENT:        g_value_set_object (value, self->parent); break;
	case PROP_ASSET_MANAGER: g_value_set_object (value, self->manager); break;
	case PROP_SHADING_MODEL: g_value_set_enum (value, lrg_material_get_shading_model (self)); break;
	case PROP_SHADER:        g_value_set_string (value, lrg_material_get_shader (self)); break;
	case PROP_BASE_COLOR_R:
	case PROP_BASE_COLOR_G:
	case PROP_BASE_COLOR_B:
	case PROP_BASE_COLOR_A:
		g_value_set_float (value, param_values (self, P_BASE_COLOR)[prop_id - PROP_BASE_COLOR_R]);
		break;
	case PROP_METALLIC:           g_value_set_float (value, lrg_material_get_metallic (self)); break;
	case PROP_ROUGHNESS:          g_value_set_float (value, lrg_material_get_roughness (self)); break;
	case PROP_OCCLUSION_STRENGTH: g_value_set_float (value, lrg_material_get_occlusion_strength (self)); break;
	case PROP_NORMAL_SCALE:       g_value_set_float (value, lrg_material_get_normal_scale (self)); break;
	case PROP_EMISSION_COLOR_R:
	case PROP_EMISSION_COLOR_G:
	case PROP_EMISSION_COLOR_B:
		g_value_set_float (value, param_values (self, P_EMISSION_COLOR)[prop_id - PROP_EMISSION_COLOR_R]);
		break;
	case PROP_EMISSION_STRENGTH: g_value_set_float (value, lrg_material_get_emission_strength (self)); break;
	case PROP_ALPHA_CUTOFF:      g_value_set_float (value, lrg_material_get_alpha_cutoff (self)); break;
	case PROP_HEIGHT_SCALE:      g_value_set_float (value, lrg_material_get_height_scale (self)); break;
	case PROP_BLEND_MODE:        g_value_set_enum (value, lrg_material_get_blend_mode (self)); break;
	case PROP_CULL_MODE:         g_value_set_enum (value, param_int (self, P_CULL)); break;
	case PROP_DEPTH_TEST:        g_value_set_boolean (value, lrg_material_get_depth_test (self)); break;
	case PROP_DEPTH_WRITE:       g_value_set_boolean (value, lrg_material_get_depth_write (self)); break;
	case PROP_DOUBLE_SIDED:      g_value_set_boolean (value, lrg_material_get_double_sided (self)); break;
	case PROP_RENDER_QUEUE:      g_value_set_enum (value, lrg_material_get_render_queue (self)); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_material_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgMaterial *self = LRG_MATERIAL (object);
	gfloat       f;

	switch (prop_id)
	{
	case PROP_NAME:          lrg_material_set_name (self, g_value_get_string (value)); break;
	case PROP_PARENT:        lrg_material_set_parent (self, g_value_get_object (value)); break;
	case PROP_ASSET_MANAGER: lrg_material_set_asset_manager (self, g_value_get_object (value)); break;
	case PROP_SHADING_MODEL: lrg_material_set_shading_model (self, g_value_get_enum (value)); break;
	case PROP_SHADER:        lrg_material_set_shader (self, g_value_get_string (value)); break;
	case PROP_BASE_COLOR_R:
	case PROP_BASE_COLOR_G:
	case PROP_BASE_COLOR_B:
	case PROP_BASE_COLOR_A:
		{
			gfloat v[4];

			memcpy (v, param_values (self, P_BASE_COLOR), sizeof v);
			v[prop_id - PROP_BASE_COLOR_R] = g_value_get_float (value);
			set_param (self, P_BASE_COLOR, v, 4);
		}
		break;
	case PROP_METALLIC:           lrg_material_set_metallic (self, g_value_get_float (value)); break;
	case PROP_ROUGHNESS:          lrg_material_set_roughness (self, g_value_get_float (value)); break;
	case PROP_OCCLUSION_STRENGTH: lrg_material_set_occlusion_strength (self, g_value_get_float (value)); break;
	case PROP_NORMAL_SCALE:       lrg_material_set_normal_scale (self, g_value_get_float (value)); break;
	case PROP_EMISSION_COLOR_R:
	case PROP_EMISSION_COLOR_G:
	case PROP_EMISSION_COLOR_B:
		{
			gfloat v[4];

			memcpy (v, param_values (self, P_EMISSION_COLOR), sizeof v);
			v[prop_id - PROP_EMISSION_COLOR_R] = g_value_get_float (value);
			set_param (self, P_EMISSION_COLOR, v, 3);
		}
		break;
	case PROP_EMISSION_STRENGTH:
		f = g_value_get_float (value);
		lrg_material_set_emission_strength (self, f);
		break;
	case PROP_ALPHA_CUTOFF:  lrg_material_set_alpha_cutoff (self, g_value_get_float (value)); break;
	case PROP_HEIGHT_SCALE:  lrg_material_set_height_scale (self, g_value_get_float (value)); break;
	case PROP_BLEND_MODE:    lrg_material_set_blend_mode (self, g_value_get_enum (value)); break;
	case PROP_CULL_MODE:     lrg_material_set_cull_mode (self, g_value_get_enum (value)); break;
	case PROP_DEPTH_TEST:    lrg_material_set_depth_test (self, g_value_get_boolean (value)); break;
	case PROP_DEPTH_WRITE:   lrg_material_set_depth_write (self, g_value_get_boolean (value)); break;
	case PROP_DOUBLE_SIDED:  lrg_material_set_double_sided (self, g_value_get_boolean (value)); break;
	case PROP_RENDER_QUEUE:  lrg_material_set_render_queue (self, g_value_get_enum (value)); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GParamSpec *
float_prop (const gchar *name,
            const gchar *blurb,
            gfloat       min,
            gfloat       max,
            gfloat       def)
{
	return g_param_spec_float (name, NULL, blurb, min, max, def,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
	                           G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_material_class_init (LrgMaterialClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_material_dispose;
	object_class->finalize = lrg_material_finalize;
	object_class->get_property = lrg_material_get_property;
	object_class->set_property = lrg_material_set_property;

	/**
	 * LrgMaterial:name:
	 *
	 * Display name (`name:`). Not inherited.
	 */
	properties[PROP_NAME] = g_param_spec_string ("name", NULL, "Display name", NULL, rw);

	/**
	 * LrgMaterial:path:
	 *
	 * The file the material was loaded from or last saved to.
	 */
	properties[PROP_PATH] = g_param_spec_string ("path", NULL, "Source file", NULL,
	                                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgMaterial:parent:
	 *
	 * The material unset values fall through to (instances).
	 */
	properties[PROP_PARENT] = g_param_spec_object ("parent", NULL, "Parent material",
	                                               LRG_TYPE_MATERIAL, rw);

	/**
	 * LrgMaterial:asset-manager:
	 *
	 * The asset manager references resolve and textures load through
	 * (held weakly).
	 */
	properties[PROP_ASSET_MANAGER] = g_param_spec_object ("asset-manager", NULL, "Asset manager",
	                                                      LRG_TYPE_ASSET_MANAGER, rw);

	/**
	 * LrgMaterial:shading-model:
	 *
	 * The effective shading model.
	 */
	properties[PROP_SHADING_MODEL] = g_param_spec_enum ("shading-model", NULL, "Shading model",
	                                                    LRG_TYPE_SHADING_MODEL,
	                                                    LRG_SHADING_MODEL_LIT, rw);

	/**
	 * LrgMaterial:shader:
	 *
	 * The effective user shader reference of a custom material.
	 */
	properties[PROP_SHADER] = g_param_spec_string ("shader", NULL, "Custom shader asset", NULL, rw);

	/**
	 * LrgMaterial:base-color-r:
	 *
	 * Red of the effective base colour (sRGB, 0 .. 1).
	 */
	properties[PROP_BASE_COLOR_R] = float_prop ("base-color-r", "Base colour red", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMaterial:base-color-g:
	 *
	 * Green of the effective base colour.
	 */
	properties[PROP_BASE_COLOR_G] = float_prop ("base-color-g", "Base colour green", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMaterial:base-color-b:
	 *
	 * Blue of the effective base colour.
	 */
	properties[PROP_BASE_COLOR_B] = float_prop ("base-color-b", "Base colour blue", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMaterial:base-color-a:
	 *
	 * Alpha of the effective base colour.
	 */
	properties[PROP_BASE_COLOR_A] = float_prop ("base-color-a", "Base colour alpha", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgMaterial:metallic:
	 *
	 * The effective metallic factor.
	 */
	properties[PROP_METALLIC] = float_prop ("metallic", "Metallic", 0.0f, 1.0f,
	                                        LRG_MATERIAL_DEFAULT_METALLIC);

	/**
	 * LrgMaterial:roughness:
	 *
	 * The effective roughness factor.
	 */
	properties[PROP_ROUGHNESS] = float_prop ("roughness", "Roughness", 0.0f, 1.0f,
	                                         LRG_MATERIAL_DEFAULT_ROUGHNESS);

	/**
	 * LrgMaterial:occlusion-strength:
	 *
	 * The effective occlusion strength.
	 */
	properties[PROP_OCCLUSION_STRENGTH] = float_prop ("occlusion-strength", "Occlusion strength",
	                                                  0.0f, 1.0f,
	                                                  LRG_MATERIAL_DEFAULT_OCCLUSION_STRENGTH);

	/**
	 * LrgMaterial:normal-scale:
	 *
	 * The effective normal map scale.
	 */
	properties[PROP_NORMAL_SCALE] = float_prop ("normal-scale", "Normal scale", 0.0f, 8.0f,
	                                            LRG_MATERIAL_DEFAULT_NORMAL_SCALE);

	/**
	 * LrgMaterial:emission-color-r:
	 *
	 * Red of the effective emission colour.
	 */
	properties[PROP_EMISSION_COLOR_R] = float_prop ("emission-color-r", "Emission red", 0.0f, 1.0f, 0.0f);

	/**
	 * LrgMaterial:emission-color-g:
	 *
	 * Green of the effective emission colour.
	 */
	properties[PROP_EMISSION_COLOR_G] = float_prop ("emission-color-g", "Emission green", 0.0f, 1.0f, 0.0f);

	/**
	 * LrgMaterial:emission-color-b:
	 *
	 * Blue of the effective emission colour.
	 */
	properties[PROP_EMISSION_COLOR_B] = float_prop ("emission-color-b", "Emission blue", 0.0f, 1.0f, 0.0f);

	/**
	 * LrgMaterial:emission-strength:
	 *
	 * The effective emission strength.
	 */
	properties[PROP_EMISSION_STRENGTH] = float_prop ("emission-strength", "Emission strength",
	                                                 0.0f, 1000.0f, 0.0f);

	/**
	 * LrgMaterial:alpha-cutoff:
	 *
	 * The effective alpha cutoff of cutout materials.
	 */
	properties[PROP_ALPHA_CUTOFF] = float_prop ("alpha-cutoff", "Alpha cutoff", 0.0f, 1.0f,
	                                            LRG_MATERIAL_DEFAULT_ALPHA_CUTOFF);

	/**
	 * LrgMaterial:height-scale:
	 *
	 * The effective parallax height scale.
	 */
	properties[PROP_HEIGHT_SCALE] = float_prop ("height-scale", "Height scale", 0.0f, 1.0f,
	                                            LRG_MATERIAL_DEFAULT_HEIGHT_SCALE);

	/**
	 * LrgMaterial:blend-mode:
	 *
	 * The effective blend mode.
	 */
	properties[PROP_BLEND_MODE] = g_param_spec_enum ("blend-mode", NULL, "Blend mode",
	                                                 LRG_TYPE_MATERIAL_BLEND_MODE,
	                                                 LRG_MATERIAL_BLEND_OPAQUE, rw);

	/**
	 * LrgMaterial:cull-mode:
	 *
	 * The effective cull mode as set (see lrg_material_get_cull_mode() for
	 * the one double-sided materials use).
	 */
	properties[PROP_CULL_MODE] = g_param_spec_enum ("cull-mode", NULL, "Cull mode",
	                                                LRG_TYPE_MATERIAL_CULL_MODE,
	                                                LRG_MATERIAL_CULL_BACK, rw);

	/**
	 * LrgMaterial:depth-test:
	 *
	 * Whether fragments are depth tested.
	 */
	properties[PROP_DEPTH_TEST] = g_param_spec_boolean ("depth-test", NULL, "Depth test", TRUE, rw);

	/**
	 * LrgMaterial:depth-write:
	 *
	 * Whether fragments write depth.
	 */
	properties[PROP_DEPTH_WRITE] = g_param_spec_boolean ("depth-write", NULL, "Depth write", TRUE, rw);

	/**
	 * LrgMaterial:double-sided:
	 *
	 * Whether both faces are drawn.
	 */
	properties[PROP_DOUBLE_SIDED] = g_param_spec_boolean ("double-sided", NULL, "Double sided",
	                                                      FALSE, rw);

	/**
	 * LrgMaterial:render-queue:
	 *
	 * The render queue hint.
	 */
	properties[PROP_RENDER_QUEUE] = g_param_spec_enum ("render-queue", NULL, "Render queue",
	                                                   LRG_TYPE_MATERIAL_RENDER_QUEUE,
	                                                   LRG_MATERIAL_RENDER_QUEUE_AUTO, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgMaterial::changed:
	 * @self: the material
	 *
	 * Emitted when any value, texture, uniform or the parent of @self
	 * changes, and when its parent emits ::changed (so instances follow
	 * edits of their parent).
	 */
	signals[SIGNAL_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass),
	                                        G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                        G_TYPE_NONE, 0);
}

static void
lrg_material_init (LrgMaterial *self)
{
	self->uniforms = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, uniform_free);
	self->custom_locs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->loc_shader = 0;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgMaterial *
lrg_material_new (void)
{
	return g_object_new (LRG_TYPE_MATERIAL, NULL);
}

LrgMaterial *
lrg_material_new_instance (LrgMaterial *parent)
{
	LrgMaterial *self;

	g_return_val_if_fail (LRG_IS_MATERIAL (parent), NULL);

	self = lrg_material_new ();
	lrg_material_set_asset_manager (self, parent->manager);
	lrg_material_set_base_dir (self, parent->base_dir);
	lrg_material_set_parent (self, parent);
	return self;
}

LrgMaterial *
lrg_material_copy (LrgMaterial *self)
{
	LrgMaterial    *copy;
	GHashTableIter  iter;
	gpointer        key;
	gpointer        value;
	guint           i;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);

	copy = lrg_material_new ();
	copy->name = g_strdup (self->name);
	copy->base_dir = g_strdup (self->base_dir);
	lrg_material_set_asset_manager (copy, self->manager);
	if (self->parent != NULL)
		lrg_material_set_parent (copy, self->parent);
	g_free (copy->parent_ref);
	copy->parent_ref = g_strdup (self->parent_ref);
	copy->set = self->set;
	memcpy (copy->values, self->values, sizeof copy->values);
	copy->shader = g_strdup (self->shader);
	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
	{
		copy->slots[i].ref = g_strdup (self->slots[i].ref);
		copy->slots[i].set = self->slots[i].set;
		if (self->slots[i].runtime != NULL)
			copy->slots[i].runtime = g_object_ref (self->slots[i].runtime);
	}
	g_hash_table_iter_init (&iter, self->uniforms);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		Uniform *src = value;
		Uniform *u = g_new0 (Uniform, 1);

		u->type = src->type;
		memcpy (u->v, src->v, sizeof u->v);
		u->ref = g_strdup (src->ref);
		g_hash_table_insert (copy->uniforms, g_strdup (key), u);
	}
	if (self->extra != NULL)
		copy->extra = yaml_mapping_ref (self->extra);
	return copy;
}

const gchar *
lrg_material_get_path (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	return self->path;
}

const gchar *
lrg_material_get_name (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	return self->name;
}

void
lrg_material_set_name (LrgMaterial *self,
                       const gchar *name)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));

	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
	emit_changed (self);
}

LrgAssetManager *
lrg_material_get_asset_manager (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	return self->manager;
}

void
lrg_material_set_asset_manager (LrgMaterial     *self,
                                LrgAssetManager *manager)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (self->manager == manager)
		return;
	if (self->manager != NULL)
		g_object_remove_weak_pointer (G_OBJECT (self->manager), (gpointer *) &self->manager);
	self->manager = manager;
	if (manager != NULL)
		g_object_add_weak_pointer (G_OBJECT (manager), (gpointer *) &self->manager);
	drop_loaded_textures (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

const gchar *
lrg_material_get_base_dir (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	return self->base_dir;
}

void
lrg_material_set_base_dir (LrgMaterial *self,
                           const gchar *base_dir)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));

	if (g_strcmp0 (self->base_dir, base_dir) == 0)
		return;
	g_free (self->base_dir);
	self->base_dir = g_strdup (base_dir);
	drop_loaded_textures (self);
}

/* ==========================================================================
 * Instances
 * ========================================================================== */

LrgMaterial *
lrg_material_get_parent (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	return self->parent;
}

/* The reference saved for @parent: its guid when the manager indexes its
 * file, else its path, else NULL (an in-memory parent). */
static gchar *
reference_for (LrgMaterial *self,
               LrgMaterial *parent)
{
	if (parent->path == NULL)
		return NULL;
	if (self->manager != NULL)
	{
		g_autofree gchar *guid = lrg_asset_manager_lookup_guid (self->manager, parent->path);

		if (guid != NULL)
			return g_strconcat (LRG_ASSET_GUID_PREFIX, guid, NULL);
	}
	return g_strdup (parent->path);
}

/* Re-parents without touching parent_ref. */
static void
set_parent_internal (LrgMaterial *self,
                     LrgMaterial *parent)
{
	if (self->parent == parent)
		return;
	if (self->parent != NULL && self->parent_changed_id != 0)
		g_signal_handler_disconnect (self->parent, self->parent_changed_id);
	self->parent_changed_id = 0;
	g_clear_object (&self->parent);
	if (parent != NULL)
	{
		self->parent = g_object_ref (parent);
		self->parent_changed_id = g_signal_connect (parent, "changed",
		                                            G_CALLBACK (on_parent_changed), self);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARENT]);
	emit_changed (self);
}

static gboolean
would_cycle (LrgMaterial *self,
             LrgMaterial *parent)
{
	LrgMaterial *m;

	for (m = parent; m != NULL; m = m->parent)
		if (m == self)
			return TRUE;
	return FALSE;
}

void
lrg_material_set_parent (LrgMaterial *self,
                         LrgMaterial *parent)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (parent == NULL || LRG_IS_MATERIAL (parent));
	g_return_if_fail (parent == NULL || !would_cycle (self, parent));

	g_clear_pointer (&self->parent_ref, g_free);
	if (parent != NULL)
		self->parent_ref = reference_for (self, parent);
	set_parent_internal (self, parent);
}

const gchar *
lrg_material_get_parent_ref (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	return self->parent_ref;
}

static LrgMaterial *new_from_file_internal (const gchar      *path,
                                            LrgAssetManager  *manager,
                                            GError          **error);

gboolean
lrg_material_set_parent_ref (LrgMaterial  *self,
                             const gchar  *ref,
                             GError      **error)
{
	g_autofree gchar         *path = NULL;
	g_autoptr(LrgMaterial)    parent = NULL;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (ref == NULL || *ref == '\0')
	{
		g_clear_pointer (&self->parent_ref, g_free);
		set_parent_internal (self, NULL);
		return TRUE;
	}

	path = lrg_material_resolve_reference (self, ref);
	if (path == NULL)
	{
		g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_NOT_FOUND,
		             "Parent material not found: %s", ref);
		return FALSE;
	}

	/* Through the manager's cache, so instances share one parent. */
	if (self->manager != NULL)
	{
		LrgMaterial *cached = lrg_asset_manager_load_material (self->manager, path, error);

		if (cached == NULL)
			return FALSE;
		parent = g_object_ref (cached);
	}
	else
	{
		parent = new_from_file_internal (path, NULL, error);
		if (parent == NULL)
			return FALSE;
	}
	if (would_cycle (self, parent))
	{
		g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_CYCLE,
		             "Material parent chain loops at %s", ref);
		return FALSE;
	}
	g_free (self->parent_ref);
	self->parent_ref = g_strdup (ref);
	set_parent_internal (self, parent);
	return TRUE;
}

/* Splits "textures.<slot>" / "uniforms.<name>" keys. */
gboolean
lrg_material_is_overridden (LrgMaterial *self,
                            const gchar *name)
{
	gint id;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (g_str_has_prefix (name, "textures."))
	{
		id = slot_from_key (name + strlen ("textures."));
		return id >= 0 && (self->slots[id].set || self->slots[id].runtime != NULL);
	}
	if (g_str_has_prefix (name, "uniforms."))
		return g_hash_table_contains (self->uniforms, name + strlen ("uniforms."));
	id = param_from_key (name);
	return id >= 0 && (self->set & (1u << id)) != 0;
}

gboolean
lrg_material_revert (LrgMaterial *self,
                     const gchar *name)
{
	gint id;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (!lrg_material_is_overridden (self, name))
		return FALSE;
	if (g_str_has_prefix (name, "textures."))
	{
		slot_clear (&self->slots[slot_from_key (name + strlen ("textures."))]);
		emit_changed (self);
		return TRUE;
	}
	if (g_str_has_prefix (name, "uniforms."))
	{
		g_hash_table_remove (self->uniforms, name + strlen ("uniforms."));
		emit_changed (self);
		return TRUE;
	}
	id = param_from_key (name);
	self->set &= ~(1u << id);
	if (id == P_SHADER)
		g_clear_pointer (&self->shader, g_free);
	param_changed (self, (ParamId) id);
	return TRUE;
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* The own uniform names, sorted (borrowed strings). */
static GPtrArray *
sorted_own_uniforms (LrgMaterial *self)
{
	GPtrArray      *names = g_ptr_array_new ();
	GHashTableIter  iter;
	gpointer        key;

	g_hash_table_iter_init (&iter, self->uniforms);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (names, key);
	g_ptr_array_sort (names, compare_strings);
	return names;
}

gchar **
lrg_material_list_overrides (LrgMaterial *self)
{
	GStrvBuilder        *builder;
	g_autoptr(GPtrArray) names = NULL;
	gchar              **result;
	guint                i;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);

	builder = g_strv_builder_new ();
	for (i = 0; i < N_PARAMS; i++)
		if (self->set & (1u << i))
			g_strv_builder_add (builder, param_info[i].key);
	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
		if (self->slots[i].set || self->slots[i].runtime != NULL)
			g_strv_builder_take (builder, g_strconcat ("textures.", slot_keys[i], NULL));
	names = sorted_own_uniforms (self);
	for (i = 0; i < names->len; i++)
		g_strv_builder_take (builder, g_strconcat ("uniforms.", (const gchar *) g_ptr_array_index (names, i), NULL));
	result = g_strv_builder_end (builder);
	g_strv_builder_unref (builder);
	return result;
}

/* ==========================================================================
 * Parameters
 * ========================================================================== */

LrgShadingModel
lrg_material_get_shading_model (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), LRG_SHADING_MODEL_LIT);
	return (LrgShadingModel) param_int (self, P_SHADING_MODEL);
}

void
lrg_material_set_shading_model (LrgMaterial     *self,
                                LrgShadingModel  model)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (model <= LRG_SHADING_MODEL_CUSTOM);
	set_param_int (self, P_SHADING_MODEL, (gint) model);
}

const gchar *
lrg_material_get_shader (LrgMaterial *self)
{
	LrgMaterial *owner;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	owner = param_owner (self, P_SHADER);
	return owner != NULL ? owner->shader : NULL;
}

void
lrg_material_set_shader (LrgMaterial *self,
                         const gchar *shader_ref)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));

	if ((self->set & (1u << P_SHADER)) && g_strcmp0 (self->shader, shader_ref) == 0)
		return;
	g_free (self->shader);
	self->shader = g_strdup (shader_ref);
	self->set |= 1u << P_SHADER;
	param_changed (self, P_SHADER);
}

void
lrg_material_get_base_color (LrgMaterial *self,
                             gfloat      *out)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (out != NULL);
	memcpy (out, param_values (self, P_BASE_COLOR), 4 * sizeof (gfloat));
}

void
lrg_material_set_base_color (LrgMaterial *self,
                             gfloat       r,
                             gfloat       g,
                             gfloat       b,
                             gfloat       a)
{
	gfloat v[4];

	g_return_if_fail (LRG_IS_MATERIAL (self));
	v[0] = r;
	v[1] = g;
	v[2] = b;
	v[3] = a;
	set_param (self, P_BASE_COLOR, v, 4);
}

#define FLOAT_ACCESSORS(lower, ID) \
	gfloat \
	lrg_material_get_##lower (LrgMaterial *self) \
	{ \
		g_return_val_if_fail (LRG_IS_MATERIAL (self), param_info[ID].def[0]); \
		return param_values (self, ID)[0]; \
	} \
	void \
	lrg_material_set_##lower (LrgMaterial *self, \
	                          gfloat       value) \
	{ \
		g_return_if_fail (LRG_IS_MATERIAL (self)); \
		set_param (self, ID, &value, 1); \
	}

FLOAT_ACCESSORS (metallic, P_METALLIC)
FLOAT_ACCESSORS (roughness, P_ROUGHNESS)
FLOAT_ACCESSORS (occlusion_strength, P_OCCLUSION)
FLOAT_ACCESSORS (normal_scale, P_NORMAL_SCALE)
FLOAT_ACCESSORS (emission_strength, P_EMISSION_STRENGTH)
FLOAT_ACCESSORS (alpha_cutoff, P_ALPHA_CUTOFF)
FLOAT_ACCESSORS (height_scale, P_HEIGHT_SCALE)

void
lrg_material_get_emission_color (LrgMaterial *self,
                                 gfloat      *out)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (out != NULL);
	memcpy (out, param_values (self, P_EMISSION_COLOR), 3 * sizeof (gfloat));
}

void
lrg_material_set_emission_color (LrgMaterial *self,
                                 gfloat       r,
                                 gfloat       g,
                                 gfloat       b)
{
	gfloat v[3];

	g_return_if_fail (LRG_IS_MATERIAL (self));
	v[0] = r;
	v[1] = g;
	v[2] = b;
	set_param (self, P_EMISSION_COLOR, v, 3);
}

/* ==========================================================================
 * Render state
 * ========================================================================== */

LrgMaterialBlendMode
lrg_material_get_blend_mode (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), LRG_MATERIAL_BLEND_OPAQUE);
	return (LrgMaterialBlendMode) param_int (self, P_BLEND);
}

void
lrg_material_set_blend_mode (LrgMaterial          *self,
                             LrgMaterialBlendMode  mode)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (mode <= LRG_MATERIAL_BLEND_MULTIPLY);
	set_param_int (self, P_BLEND, (gint) mode);
}

LrgMaterialCullMode
lrg_material_get_cull_mode (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), LRG_MATERIAL_CULL_BACK);
	if (lrg_material_get_double_sided (self))
		return LRG_MATERIAL_CULL_NONE;
	return (LrgMaterialCullMode) param_int (self, P_CULL);
}

void
lrg_material_set_cull_mode (LrgMaterial         *self,
                            LrgMaterialCullMode  mode)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (mode <= LRG_MATERIAL_CULL_NONE);
	set_param_int (self, P_CULL, (gint) mode);
}

#define BOOL_ACCESSORS(lower, ID) \
	gboolean \
	lrg_material_get_##lower (LrgMaterial *self) \
	{ \
		g_return_val_if_fail (LRG_IS_MATERIAL (self), param_info[ID].def[0] != 0.0f); \
		return param_values (self, ID)[0] != 0.0f; \
	} \
	void \
	lrg_material_set_##lower (LrgMaterial *self, \
	                          gboolean     value) \
	{ \
		g_return_if_fail (LRG_IS_MATERIAL (self)); \
		set_param_int (self, ID, value ? 1 : 0); \
	}

BOOL_ACCESSORS (depth_test, P_DEPTH_TEST)
BOOL_ACCESSORS (depth_write, P_DEPTH_WRITE)
BOOL_ACCESSORS (double_sided, P_DOUBLE_SIDED)

LrgMaterialRenderQueue
lrg_material_get_render_queue (LrgMaterial *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL (self), LRG_MATERIAL_RENDER_QUEUE_AUTO);
	return (LrgMaterialRenderQueue) param_int (self, P_RENDER_QUEUE);
}

void
lrg_material_set_render_queue (LrgMaterial            *self,
                               LrgMaterialRenderQueue  queue)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (queue <= LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT);
	set_param_int (self, P_RENDER_QUEUE, (gint) queue);
}

LrgMaterialRenderQueue
lrg_material_resolve_render_queue (LrgMaterial *self)
{
	LrgMaterialRenderQueue queue;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), LRG_MATERIAL_RENDER_QUEUE_OPAQUE);

	queue = lrg_material_get_render_queue (self);
	if (queue != LRG_MATERIAL_RENDER_QUEUE_AUTO)
		return queue;
	switch (lrg_material_get_blend_mode (self))
	{
	case LRG_MATERIAL_BLEND_OPAQUE: return LRG_MATERIAL_RENDER_QUEUE_OPAQUE;
	case LRG_MATERIAL_BLEND_CUTOUT: return LRG_MATERIAL_RENDER_QUEUE_CUTOUT;
	case LRG_MATERIAL_BLEND_ALPHA:
	case LRG_MATERIAL_BLEND_ADDITIVE:
	case LRG_MATERIAL_BLEND_MULTIPLY:
	default:                        return LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT;
	}
}

/* ==========================================================================
 * Textures
 * ========================================================================== */

const gchar *
lrg_material_get_texture_ref (LrgMaterial     *self,
                              LrgMaterialSlot  slot)
{
	LrgMaterial *m;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	g_return_val_if_fail (slot < LRG_MATERIAL_N_SLOTS, NULL);

	for (m = self; m != NULL; m = m->parent)
		if (m->slots[slot].set)
			return m->slots[slot].ref;
	return NULL;
}

void
lrg_material_set_texture_ref (LrgMaterial     *self,
                              LrgMaterialSlot  slot,
                              const gchar     *ref)
{
	Slot *s;

	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (slot < LRG_MATERIAL_N_SLOTS);

	s = &self->slots[slot];
	if (s->set && g_strcmp0 (s->ref, ref) == 0)
		return;
	g_free (s->ref);
	s->ref = (ref != NULL && *ref != '\0') ? g_strdup (ref) : NULL;
	s->set = TRUE;
	g_clear_object (&s->loaded);
	s->failed = FALSE;
	emit_changed (self);
}

void
lrg_material_set_texture (LrgMaterial     *self,
                          LrgMaterialSlot  slot,
                          GrlTexture      *texture)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (slot < LRG_MATERIAL_N_SLOTS);
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	if (g_set_object (&self->slots[slot].runtime, texture))
		emit_changed (self);
}

GrlTexture *
lrg_material_get_texture (LrgMaterial     *self,
                          LrgMaterialSlot  slot)
{
	LrgMaterial *owner;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	g_return_val_if_fail (slot < LRG_MATERIAL_N_SLOTS, NULL);

	owner = slot_owner (self, slot);
	if (owner == NULL)
		return NULL;
	ensure_slot_loaded (owner, slot, NULL);
	return peek_texture (self, slot);
}

gboolean
lrg_material_load_textures (LrgMaterial  *self,
                            GError      **error)
{
	g_auto(GStrv) names = NULL;
	gboolean      ok = TRUE;
	guint         i;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
	{
		LrgMaterial *owner = slot_owner (self, (LrgMaterialSlot) i);

		if (owner != NULL && !ensure_slot_loaded (owner, (LrgMaterialSlot) i, ok ? error : NULL))
			ok = FALSE;
	}
	names = lrg_material_list_uniforms (self);
	for (i = 0; names[i] != NULL; i++)
	{
		LrgMaterial *owner = NULL;
		Uniform     *u = uniform_lookup (self, names[i], &owner);

		if (u != NULL && !ensure_uniform_loaded (owner, u, ok ? error : NULL))
			ok = FALSE;
	}
	return ok;
}

/* ==========================================================================
 * Custom uniforms
 * ========================================================================== */

static void
put_uniform (LrgMaterial *self,
             const gchar *name,
             Uniform     *u)
{
	g_hash_table_replace (self->uniforms, g_strdup (name), u);
	emit_changed (self);
}

void
lrg_material_set_uniform (LrgMaterial            *self,
                          const gchar            *name,
                          LrgMaterialUniformType  type,
                          const gfloat           *values,
                          guint                   n_values)
{
	Uniform *u;
	guint    i;

	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (type != LRG_MATERIAL_UNIFORM_TEXTURE);
	g_return_if_fail (values != NULL || n_values == 0);

	u = g_new0 (Uniform, 1);
	u->type = type;
	for (i = 0; i < n_values && i < 4; i++)
		u->v[i] = isfinite (values[i]) ? values[i] : 0.0f;
	put_uniform (self, name, u);
}

void
lrg_material_set_uniform_float (LrgMaterial *self,
                                const gchar *name,
                                gfloat       value)
{
	lrg_material_set_uniform (self, name, LRG_MATERIAL_UNIFORM_FLOAT, &value, 1);
}

void
lrg_material_set_uniform_texture (LrgMaterial *self,
                                  const gchar *name,
                                  const gchar *ref)
{
	Uniform *u;

	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (ref != NULL);

	u = g_new0 (Uniform, 1);
	u->type = LRG_MATERIAL_UNIFORM_TEXTURE;
	u->ref = g_strdup (ref);
	put_uniform (self, name, u);
}

gboolean
lrg_material_get_uniform (LrgMaterial            *self,
                          const gchar            *name,
                          LrgMaterialUniformType *type,
                          gfloat                 *values)
{
	Uniform *u;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	u = uniform_lookup (self, name, NULL);
	if (u == NULL)
		return FALSE;
	if (type != NULL)
		*type = u->type;
	if (values != NULL)
		memcpy (values, u->v, sizeof u->v);
	return TRUE;
}

const gchar *
lrg_material_get_uniform_texture_ref (LrgMaterial *self,
                                      const gchar *name)
{
	Uniform *u;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	u = uniform_lookup (self, name, NULL);
	return u != NULL ? u->ref : NULL;
}

gchar **
lrg_material_list_uniforms (LrgMaterial *self)
{
	g_autoptr(GHashTable) seen = NULL;
	g_autoptr(GPtrArray)  names = NULL;
	LrgMaterial          *m;
	GHashTableIter        iter;
	gpointer              key;
	GStrvBuilder         *builder;
	gchar               **result;
	guint                 i;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);

	seen = g_hash_table_new (g_str_hash, g_str_equal);
	names = g_ptr_array_new ();
	for (m = self; m != NULL; m = m->parent)
	{
		g_hash_table_iter_init (&iter, m->uniforms);
		while (g_hash_table_iter_next (&iter, &key, NULL))
			if (g_hash_table_add (seen, key))
				g_ptr_array_add (names, key);
	}
	g_ptr_array_sort (names, compare_strings);
	builder = g_strv_builder_new ();
	for (i = 0; i < names->len; i++)
		g_strv_builder_add (builder, g_ptr_array_index (names, i));
	result = g_strv_builder_end (builder);
	g_strv_builder_unref (builder);
	return result;
}

/* ==========================================================================
 * Binding
 * ========================================================================== */

gint
lrg_material_get_map_flags (LrgMaterial *self)
{
	gint flags = 0;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), 0);

	if (peek_texture (self, LRG_MATERIAL_SLOT_NORMAL) != NULL)
		flags |= LRG_MATERIAL_MAP_FLAG_NORMAL;
	if (peek_texture (self, LRG_MATERIAL_SLOT_METALLIC_GLOSS) != NULL)
		flags |= LRG_MATERIAL_MAP_FLAG_METALLIC_GLOSS;
	if (peek_texture (self, LRG_MATERIAL_SLOT_HEIGHT) != NULL)
		flags |= LRG_MATERIAL_MAP_FLAG_HEIGHT;
	if (peek_texture (self, LRG_MATERIAL_SLOT_ORM) != NULL)
		flags |= LRG_MATERIAL_MAP_FLAG_ORM;
	if (peek_texture (self, LRG_MATERIAL_SLOT_EMISSION) != NULL)
		flags |= LRG_MATERIAL_MAP_FLAG_EMISSION;
	if (lrg_material_get_shading_model (self) == LRG_SHADING_MODEL_UNLIT)
		flags |= LRG_MATERIAL_MAP_FLAG_UNLIT;
	return flags;
}

void
lrg_material_get_pbr_params (LrgMaterial *self,
                             gfloat      *out)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (out != NULL);

	out[0] = lrg_material_get_metallic (self);
	out[1] = lrg_material_get_roughness (self);
	out[2] = lrg_material_get_occlusion_strength (self);
	out[3] = lrg_material_get_normal_scale (self);
}

void
lrg_material_get_emission_own (LrgMaterial *self,
                               gfloat      *out)
{
	const gfloat *c;
	gfloat        s;

	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (out != NULL);

	c = param_values (self, P_EMISSION_COLOR);
	s = lrg_material_get_emission_strength (self);
	out[0] = c[0] * s;
	out[1] = c[1] * s;
	out[2] = c[2] * s;
	out[3] = 1.0f;
}

static guint
texture_id (GrlTexture *texture)
{
	Texture2D *handle;

	if (texture == NULL)
		return 0;
	handle = grl_texture_get_handle (texture);
	return handle != NULL ? handle->id : 0;
}

/* Looks the contract's locations up once per program. */
static void
cache_locations (LrgMaterial *self,
                 guint        shader_id)
{
	guint i;

	if (self->loc_shader == shader_id)
		return;
	self->loc_shader = shader_id;
	self->locs[LOC_COL_DIFFUSE] = rlGetLocationUniform (shader_id, "colDiffuse");
	self->locs[LOC_PBR_PARAMS] = rlGetLocationUniform (shader_id, "pbrParams");
	self->locs[LOC_MAP_FLAGS] = rlGetLocationUniform (shader_id, "mapFlags");
	self->locs[LOC_EMISSION_OWN] = rlGetLocationUniform (shader_id, "emissionOwn");
	self->locs[LOC_ALPHA_CUTOFF] = rlGetLocationUniform (shader_id, "alphaCutoff");
	self->locs[LOC_HEIGHT_SCALE] = rlGetLocationUniform (shader_id, "heightScale");
	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
		self->locs[LOC_SAMPLER_FIRST + i] = rlGetLocationUniform (shader_id, slot_samplers[i]);
	g_hash_table_remove_all (self->custom_locs);
}

static void
bind_unit (gint  unit,
           guint id,
           gint  sampler_loc)
{
	rlActiveTextureSlot (unit);
	rlEnableTexture (id);
	if (sampler_loc >= 0)
		rlSetUniform (sampler_loc, &unit, RL_SHADER_UNIFORM_INT, 1);
}

static void
set_uniform_checked (gint          loc,
                     gconstpointer value,
                     gint          type)
{
	if (loc >= 0)
		rlSetUniform (loc, value, type, 1);
}

/* GL_MAX_TEXTURE_IMAGE_UNITS and the light set's first spot/shadow unit */
#define TEST_MAX_TEXTURE_IMAGE_UNITS (0x8872)
#define LIGHT_SET_RESERVED_FIRST     (11)

typedef void (*MaterialGetIntegervFn) (guint pname, gint *data);
typedef void (*MaterialGetUniformivFn) (guint program, gint location, gint *params);

/* The driver's fragment texture units (16, the GL 3.3 minimum, until a
 * context answers), capped at 32 like the light set's hook window. */
static gint
max_texture_units (void)
{
	static gint units = -1;

	if (units < 0 && IsWindowReady ())
	{
		MaterialGetIntegervFn get = (MaterialGetIntegervFn) rlGetProcAddress ("glGetIntegerv");
		gint                  n = 16;

		if (get != NULL)
			get (TEST_MAX_TEXTURE_IMAGE_UNITS, &n);
		units = CLAMP (n, 16, 32);
	}
	return units > 0 ? units : 16;
}

/* Whether @shader_id is a lit program (it declares the light set's
 * uniforms), so the light set's units are taken. */
static gboolean
program_is_lit (guint shader_id)
{
	return rlGetLocationUniform (shader_id, "sunDirection") >= 0 ||
	       rlGetLocationUniform (shader_id, "lightCount") >= 0;
}

/*
 * custom_unit_allowed:
 *
 * The custom texture budget, shared with LrgShaderAsset (whose samplers
 * start at LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT = 7):
 * - units 0 .. 6 are the material maps, never custom;
 * - unlit programs: 7 .. GL_MAX_TEXTURE_IMAGE_UNITS - 1 (at most 31);
 * - lit programs: 11 .. 15 are the light set's shadow atlas, shadow maps
 *   and light grid, and the hooks (environment, SSAO, decals) take 16 and
 *   up on a driver with more than 16 units, or 7 .. 10 on a 16-unit one;
 *   so custom textures get 7 .. 10 when the driver has more than 16
 *   units, and none on a 16-unit driver.
 */
static gboolean
custom_unit_allowed (guint shader_id,
                     gint  unit,
                     gboolean lit)
{
	gint units = max_texture_units ();

	if (unit < LRG_MATERIAL_CUSTOM_TEXTURE_UNIT || unit >= units)
		return FALSE;
	if (!lit)
		return TRUE;
	return units > 16 && unit < LIGHT_SET_RESERVED_FIRST;
}

/*
 * custom_texture_unit:
 *
 * The unit a custom sampler at @loc binds to. A sampler the program
 * already points at an allowed unit (a shader asset's own sampler
 * allocation, lrg_shader_asset_apply()) keeps it, so the material's
 * texture replaces the asset's default in place. Otherwise the next
 * allowed unit from *@next, which is advanced past it. -1 when the
 * budget is used up.
 */
static gint
custom_texture_unit (guint  shader_id,
                     gint   loc,
                     gint  *next)
{
	MaterialGetUniformivFn get = (MaterialGetUniformivFn) rlGetProcAddress ("glGetUniformiv");
	gboolean               lit = program_is_lit (shader_id);
	gint                   current = 0;
	gint                   unit;

	if (get != NULL)
		get (shader_id, loc, &current);
	if (custom_unit_allowed (shader_id, current, lit))
		return current;

	for (unit = MAX (*next, LRG_MATERIAL_CUSTOM_TEXTURE_UNIT); unit < 32; unit++)
		if (custom_unit_allowed (shader_id, unit, lit))
		{
			*next = unit + 1;
			return unit;
		}
	return -1;
}

void
lrg_material_apply_custom_uniforms (LrgMaterial *self,
                                    guint        shader_id,
                                    gint        *texture_unit)
{
	g_auto(GStrv) names = NULL;
	guint         i;

	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (texture_unit != NULL);

	if (shader_id == 0 || !IsWindowReady ())
		return;
	cache_locations (self, shader_id);
	rlEnableShader (shader_id);

	names = lrg_material_list_uniforms (self);
	for (i = 0; names[i] != NULL; i++)
	{
		LrgMaterial *owner = NULL;
		Uniform     *u = uniform_lookup (self, names[i], &owner);
		gpointer     cached;
		gint         loc;

		/* locations by name, cached per program (+2 so -1 is storable) */
		if (g_hash_table_lookup_extended (self->custom_locs, names[i], NULL, &cached))
			loc = GPOINTER_TO_INT (cached) - 2;
		else
		{
			loc = rlGetLocationUniform (shader_id, names[i]);
			g_hash_table_insert (self->custom_locs, g_strdup (names[i]), GINT_TO_POINTER (loc + 2));
		}
		if (loc < 0 || u == NULL)
			continue;

		switch (u->type)
		{
		case LRG_MATERIAL_UNIFORM_FLOAT:
			rlSetUniform (loc, u->v, RL_SHADER_UNIFORM_FLOAT, 1);
			break;
		case LRG_MATERIAL_UNIFORM_VEC2:
			rlSetUniform (loc, u->v, RL_SHADER_UNIFORM_VEC2, 1);
			break;
		case LRG_MATERIAL_UNIFORM_VEC3:
			rlSetUniform (loc, u->v, RL_SHADER_UNIFORM_VEC3, 1);
			break;
		case LRG_MATERIAL_UNIFORM_VEC4:
		case LRG_MATERIAL_UNIFORM_COLOR:
			rlSetUniform (loc, u->v, RL_SHADER_UNIFORM_VEC4, 1);
			break;
		case LRG_MATERIAL_UNIFORM_INT:
			{
				gint iv = (gint) u->v[0];

				rlSetUniform (loc, &iv, RL_SHADER_UNIFORM_INT, 1);
			}
			break;
		case LRG_MATERIAL_UNIFORM_TEXTURE:
			{
				guint id;

				gint  unit;

				ensure_uniform_loaded (owner, u, NULL);
				id = u->loaded != NULL ? texture_id (u->loaded) : rlGetTextureIdDefault ();
				unit = custom_texture_unit (shader_id, loc, texture_unit);
				if (unit < 0)
				{
					g_warning ("LrgMaterial '%s': no free texture unit for sampler '%s' "
					           "(the custom texture budget of this program is used up); "
					           "not bound", self->name != NULL ? self->name : "(unnamed)",
					           names[i]);
					continue;
				}
				bind_unit (unit, id, loc);
			}
			break;
		default:
			break;
		}
	}
	rlActiveTextureSlot (0);
}

void
lrg_material_apply (LrgMaterial *self,
                    guint        shader_id)
{
	gfloat color[4];
	gfloat params[4];
	gfloat emission[4];
	gfloat cutoff;
	gfloat height;
	gint   flags;
	gint   unit;
	guint  i;

	g_return_if_fail (LRG_IS_MATERIAL (self));

	if (shader_id == 0 || !IsWindowReady ())
		return;

	/* Textures first: loading them decides the map flags. */
	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
		lrg_material_get_texture (self, (LrgMaterialSlot) i);

	cache_locations (self, shader_id);
	rlEnableShader (shader_id);

	/* Maps: albedo falls back to white, emission to white (the tint
	 * then decides: emissionOwn = 0 gives no glow), the rest only when
	 * present (their mapFlags bit stays off otherwise). */
	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
	{
		GrlTexture *texture = peek_texture (self, (LrgMaterialSlot) i);
		guint       id = texture_id (texture);

		if (id == 0 && (i == LRG_MATERIAL_SLOT_ALBEDO || i == LRG_MATERIAL_SLOT_EMISSION))
			id = rlGetTextureIdDefault ();
		if (id != 0)
			bind_unit (slot_units[i], id, self->locs[LOC_SAMPLER_FIRST + i]);
	}
	rlActiveTextureSlot (0);

	/* The scalar uniforms of the contract. */
	lrg_material_get_base_color (self, color);
	lrg_material_get_pbr_params (self, params);
	lrg_material_get_emission_own (self, emission);
	flags = lrg_material_get_map_flags (self);
	cutoff = lrg_material_get_blend_mode (self) == LRG_MATERIAL_BLEND_CUTOUT
	         ? lrg_material_get_alpha_cutoff (self) : 0.0f;
	height = lrg_material_get_height_scale (self);
	set_uniform_checked (self->locs[LOC_COL_DIFFUSE], color, RL_SHADER_UNIFORM_VEC4);
	set_uniform_checked (self->locs[LOC_PBR_PARAMS], params, RL_SHADER_UNIFORM_VEC4);
	set_uniform_checked (self->locs[LOC_MAP_FLAGS], &flags, RL_SHADER_UNIFORM_INT);
	set_uniform_checked (self->locs[LOC_EMISSION_OWN], emission, RL_SHADER_UNIFORM_VEC4);
	set_uniform_checked (self->locs[LOC_ALPHA_CUTOFF], &cutoff, RL_SHADER_UNIFORM_FLOAT);
	set_uniform_checked (self->locs[LOC_HEIGHT_SCALE], &height, RL_SHADER_UNIFORM_FLOAT);

	unit = LRG_MATERIAL_CUSTOM_TEXTURE_UNIT;
	lrg_material_apply_custom_uniforms (self, shader_id, &unit);
}

void
lrg_material_reset_uniforms (guint shader_id)
{
	static guint  cached_shader = 0;
	static gint   locs[4];
	static const gfloat params[4] = {
		LRG_MATERIAL_DEFAULT_METALLIC, LRG_MATERIAL_DEFAULT_ROUGHNESS,
		LRG_MATERIAL_DEFAULT_OCCLUSION_STRENGTH, LRG_MATERIAL_DEFAULT_NORMAL_SCALE
	};
	static const gfloat none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const gint units[] = {
		LRG_MATERIAL_UNIT_METALLIC_GLOSS, LRG_MATERIAL_UNIT_NORMAL, LRG_MATERIAL_UNIT_ORM,
		LRG_MATERIAL_UNIT_EMISSION, LRG_MATERIAL_UNIT_HEIGHT
	};
	gint   flags = 0;
	gfloat cutoff = 0.0f;
	guint  i;

	if (shader_id == 0 || !IsWindowReady ())
		return;
	if (cached_shader != shader_id)
	{
		cached_shader = shader_id;
		locs[0] = rlGetLocationUniform (shader_id, "pbrParams");
		locs[1] = rlGetLocationUniform (shader_id, "mapFlags");
		locs[2] = rlGetLocationUniform (shader_id, "emissionOwn");
		locs[3] = rlGetLocationUniform (shader_id, "alphaCutoff");
	}
	rlEnableShader (shader_id);
	set_uniform_checked (locs[0], params, RL_SHADER_UNIFORM_VEC4);
	set_uniform_checked (locs[1], &flags, RL_SHADER_UNIFORM_INT);
	set_uniform_checked (locs[2], none, RL_SHADER_UNIFORM_VEC4);
	set_uniform_checked (locs[3], &cutoff, RL_SHADER_UNIFORM_FLOAT);

	/* Empty the material units (not 0, which every draw rebinds): the
	 * lit shader always samples emissionMap, and an empty unit reads
	 * black, as draws without a material expect. */
	for (i = 0; i < G_N_ELEMENTS (units); i++)
	{
		rlActiveTextureSlot (units[i]);
		rlDisableTexture ();
	}
	rlActiveTextureSlot (0);
}

void
lrg_material_apply_state (LrgMaterial *self)
{
	g_return_if_fail (LRG_IS_MATERIAL (self));

	if (!IsWindowReady ())
		return;
	rlDrawRenderBatchActive ();

	switch (lrg_material_get_blend_mode (self))
	{
	case LRG_MATERIAL_BLEND_ADDITIVE:
		rlSetBlendMode (RL_BLEND_ADDITIVE);
		break;
	case LRG_MATERIAL_BLEND_MULTIPLY:
		rlSetBlendMode (RL_BLEND_MULTIPLIED);
		break;
	case LRG_MATERIAL_BLEND_OPAQUE:
	case LRG_MATERIAL_BLEND_CUTOUT:
	case LRG_MATERIAL_BLEND_ALPHA:
	default:
		rlSetBlendMode (RL_BLEND_ALPHA);
		break;
	}

	switch (lrg_material_get_cull_mode (self))
	{
	case LRG_MATERIAL_CULL_NONE:
		rlDisableBackfaceCulling ();
		break;
	case LRG_MATERIAL_CULL_FRONT:
		rlEnableBackfaceCulling ();
		rlSetCullFace (RL_CULL_FACE_FRONT);
		break;
	case LRG_MATERIAL_CULL_BACK:
	default:
		rlEnableBackfaceCulling ();
		rlSetCullFace (RL_CULL_FACE_BACK);
		break;
	}

	if (lrg_material_get_depth_test (self))
		rlEnableDepthTest ();
	else
		rlDisableDepthTest ();
	if (lrg_material_get_depth_write (self))
		rlEnableDepthMask ();
	else
		rlDisableDepthMask ();
}

void
lrg_material_restore_state (LrgMaterial *self)
{
	g_return_if_fail (self == NULL || LRG_IS_MATERIAL (self));

	if (!IsWindowReady ())
		return;
	rlDrawRenderBatchActive ();
	rlSetBlendMode (RL_BLEND_ALPHA);
	rlEnableBackfaceCulling ();
	rlSetCullFace (RL_CULL_FACE_BACK);
	rlEnableDepthTest ();
	rlEnableDepthMask ();
}

void
lrg_material_fill_raylib_maps (LrgMaterial *self,
                               gpointer     maps)
{
	MaterialMap *m = maps;
	gfloat       color[4];
	guint        i;

	g_return_if_fail (LRG_IS_MATERIAL (self));
	g_return_if_fail (maps != NULL);

	/* The same textures lrg_material_apply() binds, unit = map index. */
	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
	{
		GrlTexture *texture = lrg_material_get_texture (self, (LrgMaterialSlot) i);
		gint        unit = slot_units[i];

		if (texture != NULL && texture_id (texture) != 0)
			m[unit].texture = *(Texture2D *) grl_texture_get_handle (texture);
		else if (i == LRG_MATERIAL_SLOT_ALBEDO || i == LRG_MATERIAL_SLOT_EMISSION)
		{
			m[unit].texture.id = rlGetTextureIdDefault ();
			m[unit].texture.width = 1;
			m[unit].texture.height = 1;
			m[unit].texture.mipmaps = 1;
			m[unit].texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
		}
	}

	/* DrawMesh() uploads colDiffuse from the albedo map's colour. */
	lrg_material_get_base_color (self, color);
	m[MATERIAL_MAP_ALBEDO].color.r = (unsigned char) lrintf (color[0] * 255.0f);
	m[MATERIAL_MAP_ALBEDO].color.g = (unsigned char) lrintf (color[1] * 255.0f);
	m[MATERIAL_MAP_ALBEDO].color.b = (unsigned char) lrintf (color[2] * 255.0f);
	m[MATERIAL_MAP_ALBEDO].color.a = (unsigned char) lrintf (color[3] * 255.0f);
}

/* ==========================================================================
 * `.rmat` parsing
 * ========================================================================== */

/* Paths currently being loaded (cycle detection across nested loads). */
static GPtrArray *loading_paths = NULL;

static gboolean
scalar_to_double (YamlNode *node,
                  gdouble  *out)
{
	const gchar *s;
	gchar       *end = NULL;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	s = yaml_node_get_scalar (node);
	if (s == NULL || *s == '\0')
		return FALSE;
	*out = g_ascii_strtod (s, &end);
	return end != NULL && *end == '\0' && isfinite (*out);
}

static gboolean
scalar_to_bool (YamlNode *node,
                gboolean *out)
{
	const gchar *s;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	s = yaml_node_get_scalar (node);
	if (g_strcmp0 (s, "true") == 0 || g_strcmp0 (s, "yes") == 0 || g_strcmp0 (s, "on") == 0 ||
	    g_strcmp0 (s, "1") == 0)
		*out = TRUE;
	else if (g_strcmp0 (s, "false") == 0 || g_strcmp0 (s, "no") == 0 || g_strcmp0 (s, "off") == 0 ||
	         g_strcmp0 (s, "0") == 0)
		*out = FALSE;
	else
		return FALSE;
	return TRUE;
}

static const gchar *
scalar_string (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
is_null (YamlNode *node)
{
	const gchar *s;

	if (node == NULL)
		return TRUE;
	if (yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return TRUE;
	s = scalar_string (node);
	return s != NULL && (*s == '\0' || g_strcmp0 (s, "null") == 0 || g_strcmp0 (s, "~") == 0);
}

/* Reads 1 .. 4 numbers (a scalar or a sequence) into @out. */
static gint
read_numbers (YamlNode *node,
              gfloat   *out)
{
	gdouble d;

	if (scalar_to_double (node, &d))
	{
		out[0] = (gfloat) d;
		return 1;
	}
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);
		guint         n = yaml_sequence_get_length (seq);
		guint         i;

		if (n == 0 || n > 4)
			return -1;
		for (i = 0; i < n; i++)
		{
			if (!scalar_to_double (yaml_sequence_get_element (seq, i), &d))
				return -1;
			out[i] = (gfloat) d;
		}
		return (gint) n;
	}
	return -1;
}

static gboolean
invalid (GError      **error,
         const gchar  *key)
{
	g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_INVALID,
	             "Invalid value for material key '%s'", key);
	return FALSE;
}

/* One param from @node (a known key). */
static gboolean
parse_param (LrgMaterial  *self,
             ParamId       id,
             YamlNode     *node,
             GError      **error)
{
	const ParamInfo *info = &param_info[id];
	gfloat           v[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	gdouble          d;
	gboolean         b;
	gint             e;
	gint             n;

	switch (info->kind)
	{
	case KIND_ENUM:
		if (!enum_from_nick (param_enum_type (id), scalar_string (node), &e))
			return invalid (error, info->key);
		set_param_int (self, id, e);
		return TRUE;
	case KIND_STRING:
		if (id == P_SHADER)
		{
			lrg_material_set_shader (self, is_null (node) ? NULL : scalar_string (node));
			return TRUE;
		}
		return invalid (error, info->key);
	case KIND_COLOR4:
	case KIND_COLOR3:
		n = read_numbers (node, v);
		if (n < 3 || (info->kind == KIND_COLOR3 && n != 3))
			return invalid (error, info->key);
		set_param (self, id, v, info->kind == KIND_COLOR4 ? 4 : 3);
		return TRUE;
	case KIND_FLOAT:
		if (!scalar_to_double (node, &d))
			return invalid (error, info->key);
		v[0] = (gfloat) d;
		set_param (self, id, v, 1);
		return TRUE;
	case KIND_BOOL:
		if (!scalar_to_bool (node, &b))
			return invalid (error, info->key);
		set_param_int (self, id, b ? 1 : 0);
		return TRUE;
	default:
		return invalid (error, info->key);
	}
}

static gboolean
parse_textures (LrgMaterial  *self,
                YamlNode     *node,
                GError      **error)
{
	YamlMapping *map;
	guint        i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return invalid (error, "textures");
	map = yaml_node_get_mapping (node);
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *value = yaml_mapping_get_value (map, i);
		gint         slot = slot_from_key (key);

		if (slot < 0)
		{
			g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_INVALID,
			             "Unknown material texture slot '%s'", key);
			return FALSE;
		}
		if (is_null (value))
			lrg_material_set_texture_ref (self, (LrgMaterialSlot) slot, NULL);
		else if (scalar_string (value) != NULL)
			lrg_material_set_texture_ref (self, (LrgMaterialSlot) slot, scalar_string (value));
		else
			return invalid (error, key);
	}
	return TRUE;
}

static gboolean
parse_render_state (LrgMaterial  *self,
                    YamlNode     *node,
                    GError      **error)
{
	YamlMapping *map;
	guint        i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return invalid (error, "render_state");
	map = yaml_node_get_mapping (node);
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		gint         id = param_from_key (key);

		if (id < 0 || !param_info[id].in_state)
		{
			g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_INVALID,
			             "Unknown render_state key '%s'", key);
			return FALSE;
		}
		if (!parse_param (self, (ParamId) id, yaml_mapping_get_value (map, i), error))
			return FALSE;
	}
	return TRUE;
}

/* `name: { type: vec3, value: [..] }`, or a bare number / sequence. */
static gboolean
parse_uniforms (LrgMaterial  *self,
                YamlNode     *node,
                GError      **error)
{
	YamlMapping *map;
	guint        i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return invalid (error, "uniforms");
	map = yaml_node_get_mapping (node);
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar            *name = yaml_mapping_get_key (map, i);
		YamlNode               *value = yaml_mapping_get_value (map, i);
		YamlNode               *payload = value;
		gfloat                  v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		gint                    type = -1;
		gint                    n;

		if (value != NULL && yaml_node_get_node_type (value) == YAML_NODE_MAPPING)
		{
			YamlMapping *u = yaml_node_get_mapping (value);

			if (!enum_from_nick (LRG_TYPE_MATERIAL_UNIFORM_TYPE,
			                     scalar_string (yaml_mapping_get_member (u, "type")), &type))
				return invalid (error, name);
			payload = yaml_mapping_get_member (u, "value");
		}
		if (type == LRG_MATERIAL_UNIFORM_TEXTURE)
		{
			if (scalar_string (payload) == NULL || is_null (payload))
				return invalid (error, name);
			lrg_material_set_uniform_texture (self, name, scalar_string (payload));
			continue;
		}
		n = read_numbers (payload, v);
		if (n < 0)
			return invalid (error, name);
		if (type < 0)
		{
			/* shorthand: the component count picks float .. vec4 */
			static const LrgMaterialUniformType by_count[] = {
				LRG_MATERIAL_UNIFORM_FLOAT, LRG_MATERIAL_UNIFORM_VEC2,
				LRG_MATERIAL_UNIFORM_VEC3, LRG_MATERIAL_UNIFORM_VEC4
			};

			type = (gint) by_count[n - 1];
		}
		if ((guint) n != uniform_components ((LrgMaterialUniformType) type))
			return invalid (error, name);
		lrg_material_set_uniform (self, name, (LrgMaterialUniformType) type, v, (guint) n);
	}
	return TRUE;
}

static gboolean
parse_root (LrgMaterial  *self,
            YamlNode     *root,
            GError      **error)
{
	YamlMapping *map;
	const gchar *parent_ref = NULL;
	guint        i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_PARSE,
		                     "A material file must be a YAML mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (root);

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *value = yaml_mapping_get_value (map, i);
		gint         id;
		gdouble      d;

		if (g_strcmp0 (key, "version") == 0)
		{
			if (!scalar_to_double (value, &d))
				return invalid (error, key);
			if (d > LRG_MATERIAL_FORMAT_VERSION)
			{
				g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_VERSION,
				             "Material format version %g is newer than %d", d,
				             LRG_MATERIAL_FORMAT_VERSION);
				return FALSE;
			}
		}
		else if (g_strcmp0 (key, "name") == 0)
			lrg_material_set_name (self, scalar_string (value));
		else if (g_strcmp0 (key, "parent") == 0)
			parent_ref = is_null (value) ? NULL : scalar_string (value);
		else if (g_strcmp0 (key, "textures") == 0)
		{
			if (!parse_textures (self, value, error))
				return FALSE;
		}
		else if (g_strcmp0 (key, "render_state") == 0)
		{
			if (!parse_render_state (self, value, error))
				return FALSE;
		}
		else if (g_strcmp0 (key, "uniforms") == 0)
		{
			if (!parse_uniforms (self, value, error))
				return FALSE;
		}
		else if ((id = param_from_key (key)) >= 0 && !param_info[id].in_state)
		{
			if (!parse_param (self, (ParamId) id, value, error))
				return FALSE;
		}
		else
		{
			/* Unknown keys survive a load/save round trip. */
			if (self->extra == NULL)
				self->extra = yaml_mapping_new ();
			yaml_mapping_set_member (self->extra, key, value);
		}
	}

	/* The parent last: its resolution needs the base dir and manager. */
	if (parent_ref != NULL && !lrg_material_set_parent_ref (self, parent_ref, error))
		return FALSE;
	return TRUE;
}

static LrgMaterial *
new_from_root (YamlNode         *root,
               const gchar      *base_dir,
               LrgAssetManager  *manager,
               GError          **error)
{
	g_autoptr(LrgMaterial) self = lrg_material_new ();

	self->base_dir = g_strdup (base_dir);
	lrg_material_set_asset_manager (self, manager);
	if (!parse_root (self, root, error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgMaterial *
lrg_material_new_from_data (const gchar      *data,
                            gssize            length,
                            const gchar      *base_dir,
                            LrgAssetManager  *manager,
                            GError          **error)
{
	g_autoptr(YamlParser) parser = NULL;
	GError               *local = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_PARSE,
		             "Material YAML: %s", local->message);
		g_error_free (local);
		return NULL;
	}
	return new_from_root (yaml_parser_get_root (parser), base_dir, manager, error);
}

static LrgMaterial *
new_from_file_internal (const gchar      *path,
                        LrgAssetManager  *manager,
                        GError          **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autofree gchar     *canonical = g_canonicalize_filename (path, NULL);
	g_autofree gchar     *dir = g_path_get_dirname (canonical);
	GError               *local = NULL;
	LrgMaterial          *self;
	guint                 i;

	/* A file already being loaded further up the stack is a cycle. */
	if (loading_paths == NULL)
		loading_paths = g_ptr_array_new ();
	for (i = 0; i < loading_paths->len; i++)
		if (g_strcmp0 (g_ptr_array_index (loading_paths, i), canonical) == 0)
		{
			g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_CYCLE,
			             "Material parent chain loops at %s", canonical);
			return NULL;
		}

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, canonical, &local))
	{
		if (g_error_matches (local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_NOT_FOUND,
			             "Material file not found: %s", canonical);
		else
			g_set_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_PARSE,
			             "Material %s: %s", canonical, local->message);
		g_error_free (local);
		return NULL;
	}

	g_ptr_array_add (loading_paths, canonical);
	self = new_from_root (yaml_parser_get_root (parser), dir, manager, error);
	g_ptr_array_remove_index (loading_paths, loading_paths->len - 1);
	if (self != NULL)
		self->path = g_steal_pointer (&canonical);
	return self;
}

LrgMaterial *
lrg_material_new_from_file (const gchar      *path,
                            LrgAssetManager  *manager,
                            GError          **error)
{
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return new_from_file_internal (path, manager, error);
}

/* ==========================================================================
 * `.rmat` writing
 * ========================================================================== */

static YamlSequence *
float_sequence (const gfloat *v,
                guint         n)
{
	YamlSequence *seq = yaml_sequence_new ();
	guint         i;

	for (i = 0; i < n; i++)
		yaml_sequence_add_double_element (seq, (gdouble) v[i]);
	return seq;
}

/* Writes param @id into @map (the material sets it). */
static void
write_param (LrgMaterial *self,
             YamlMapping *map,
             ParamId      id)
{
	const ParamInfo *info = &param_info[id];

	switch (info->kind)
	{
	case KIND_ENUM:
		yaml_mapping_set_string_member (map, info->key,
		                                enum_nick (param_enum_type (id), (gint) self->values[id][0]));
		break;
	case KIND_STRING:
		if (self->shader != NULL)
			yaml_mapping_set_string_member (map, info->key, self->shader);
		else
			yaml_mapping_set_null_member (map, info->key);
		break;
	case KIND_COLOR4:
	case KIND_COLOR3:
		{
			g_autoptr(YamlSequence) seq = float_sequence (self->values[id],
			                                              info->kind == KIND_COLOR4 ? 4 : 3);

			yaml_mapping_set_sequence_member (map, info->key, seq);
		}
		break;
	case KIND_FLOAT:
		yaml_mapping_set_double_member (map, info->key, (gdouble) self->values[id][0]);
		break;
	case KIND_BOOL:
		yaml_mapping_set_boolean_member (map, info->key, self->values[id][0] != 0.0f);
		break;
	default:
		break;
	}
}

static YamlNode *
build_root (LrgMaterial *self)
{
	g_autoptr(YamlMapping) map = yaml_mapping_new ();
	g_autoptr(YamlMapping) textures = yaml_mapping_new ();
	g_autoptr(YamlMapping) state = yaml_mapping_new ();
	g_autoptr(YamlMapping) uniforms = yaml_mapping_new ();
	g_autoptr(GPtrArray)   names = NULL;
	guint                  i;

	yaml_mapping_set_int_member (map, "version", LRG_MATERIAL_FORMAT_VERSION);
	if (self->name != NULL)
		yaml_mapping_set_string_member (map, "name", self->name);
	if (self->parent_ref != NULL)
		yaml_mapping_set_string_member (map, "parent", self->parent_ref);

	/* Values (render state ones grouped), in table order. */
	for (i = 0; i < N_PARAMS; i++)
	{
		if ((self->set & (1u << i)) == 0)
			continue;
		if (param_info[i].in_state)
			write_param (self, state, (ParamId) i);
		else if (i != P_RENDER_QUEUE)
			write_param (self, map, (ParamId) i);
	}

	for (i = 0; i < LRG_MATERIAL_N_SLOTS; i++)
	{
		if (!self->slots[i].set)
			continue;
		if (self->slots[i].ref != NULL)
			yaml_mapping_set_string_member (textures, slot_keys[i], self->slots[i].ref);
		else
			yaml_mapping_set_null_member (textures, slot_keys[i]);
	}
	if (yaml_mapping_get_size (textures) > 0)
		yaml_mapping_set_mapping_member (map, "textures", textures);
	if (yaml_mapping_get_size (state) > 0)
		yaml_mapping_set_mapping_member (map, "render_state", state);
	if (self->set & (1u << P_RENDER_QUEUE))
		write_param (self, map, P_RENDER_QUEUE);

	names = sorted_own_uniforms (self);
	for (i = 0; i < names->len; i++)
	{
		const gchar            *name = g_ptr_array_index (names, i);
		Uniform                *u = g_hash_table_lookup (self->uniforms, name);
		g_autoptr(YamlMapping)  entry = yaml_mapping_new ();

		yaml_mapping_set_string_member (entry, "type",
		                                enum_nick (LRG_TYPE_MATERIAL_UNIFORM_TYPE, (gint) u->type));
		if (u->type == LRG_MATERIAL_UNIFORM_TEXTURE)
			yaml_mapping_set_string_member (entry, "value", u->ref);
		else if (uniform_components (u->type) == 1)
			yaml_mapping_set_double_member (entry, "value", (gdouble) u->v[0]);
		else
		{
			g_autoptr(YamlSequence) seq = float_sequence (u->v, uniform_components (u->type));

			yaml_mapping_set_sequence_member (entry, "value", seq);
		}
		yaml_mapping_set_mapping_member (uniforms, name, entry);
	}
	if (yaml_mapping_get_size (uniforms) > 0)
		yaml_mapping_set_mapping_member (map, "uniforms", uniforms);

	/* Unknown keys the file had, after the known ones. */
	if (self->extra != NULL)
	{
		for (i = 0; i < yaml_mapping_get_size (self->extra); i++)
		{
			const gchar *key = yaml_mapping_get_key (self->extra, i);

			if (!yaml_mapping_has_member (map, key))
				yaml_mapping_set_member (map, key, yaml_mapping_get_value (self->extra, i));
		}
	}
	return yaml_node_new_mapping (map);
}

gchar *
lrg_material_save_to_data (LrgMaterial *self,
                           gsize       *length)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), NULL);

	root = build_root (self);
	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, length, NULL);
}

gboolean
lrg_material_save_to_file (LrgMaterial  *self,
                           const gchar  *path,
                           GError      **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_MATERIAL (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	data = lrg_material_save_to_data (self, &length);
	if (data == NULL)
	{
		g_set_error_literal (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_INVALID,
		                     "Failed to serialize the material");
		return FALSE;
	}
	if (!g_file_set_contents (path, data, (gssize) length, error))
		return FALSE;
	g_free (self->path);
	self->path = g_canonicalize_filename (path, NULL);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PATH]);
	return TRUE;
}
