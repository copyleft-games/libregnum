/* lrg-scripting-crispy.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Crispy (compiled-C) scripting backend.
 */

#include "lrg-scripting-crispy.h"
#include "../lrg-enums.h"
#include <crispy.h>

struct _LrgScriptingCrispy
{
	LrgScripting parent_instance;

	CrispyGccCompiler *compiler;   /* lazily created GCC compiler */
	CrispyFileCache   *cache;      /* lazily created compile cache */
	CrispyScript      *script;     /* most recently loaded script */
};

G_DEFINE_FINAL_TYPE (LrgScriptingCrispy, lrg_scripting_crispy, LRG_TYPE_SCRIPTING)

/* Lazily create the GCC compiler + file cache the script ctors require (they
 * assert both are non-NULL).  Returns FALSE on error. */
static gboolean
crispy_ensure_toolchain (LrgScriptingCrispy *self, GError **error)
{
	if (self->compiler == NULL)
		self->compiler = crispy_gcc_compiler_new (error);
	if (self->compiler == NULL)
		return FALSE;
	if (self->cache == NULL)
		self->cache = crispy_file_cache_new ();
	return self->cache != NULL;
}

static gboolean
crispy_load_file (LrgScripting  *scripting,
                  const gchar   *path,
                  GError       **error)
{
	LrgScriptingCrispy *self = LRG_SCRIPTING_CRISPY (scripting);
	CrispyScript       *script;

	if (!crispy_ensure_toolchain (self, error))
		return FALSE;
	script = crispy_script_new_from_file (path, CRISPY_COMPILER (self->compiler),
	                                      CRISPY_CACHE_PROVIDER (self->cache),
	                                      CRISPY_FLAG_NONE, error);
	if (script == NULL)
		return FALSE;

	g_clear_object (&self->script);
	self->script = script;

	return crispy_script_execute (self->script, 0, NULL, error) == 0;
}

static gboolean
crispy_load_string (LrgScripting  *scripting,
                    const gchar   *name,
                    const gchar   *code,
                    GError       **error)
{
	LrgScriptingCrispy *self = LRG_SCRIPTING_CRISPY (scripting);
	CrispyScript       *script;

	(void) name;
	if (!crispy_ensure_toolchain (self, error))
		return FALSE;
	script = crispy_script_new_from_inline (code, NULL,
	                                        CRISPY_COMPILER (self->compiler),
	                                        CRISPY_CACHE_PROVIDER (self->cache),
	                                        CRISPY_FLAG_NONE, error);
	if (script == NULL)
		return FALSE;

	g_clear_object (&self->script);
	self->script = script;

	return crispy_script_execute (self->script, 0, NULL, error) == 0;
}

static gboolean
crispy_call_function (LrgScripting  *scripting,
                      const gchar   *func_name,
                      GValue        *return_value,
                      guint          n_args,
                      const GValue  *args,
                      GError       **error)
{
	/* Crispy scripts execute their entry point at load time and do not expose
	 * per-function entry points, so lifecycle-hook calls are accepted as
	 * no-ops (the script's effect already ran on load). */
	return TRUE;
}

static gboolean
crispy_register_function (LrgScripting           *scripting,
                          const gchar            *name,
                          LrgScriptingCFunction   func,
                          gpointer                user_data,
                          GError                **error)
{
	g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED,
	             "Crispy backend does not support registering C functions");
	return FALSE;
}

static gboolean
crispy_get_global (LrgScripting  *scripting,
                   const gchar   *name,
                   GValue        *value,
                   GError       **error)
{
	g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED,
	             "Crispy backend does not support global variables");
	return FALSE;
}

static gboolean
crispy_set_global (LrgScripting  *scripting,
                   const gchar   *name,
                   const GValue  *value,
                   GError       **error)
{
	g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED,
	             "Crispy backend does not support global variables");
	return FALSE;
}

static void
crispy_reset (LrgScripting *scripting)
{
	LrgScriptingCrispy *self = LRG_SCRIPTING_CRISPY (scripting);

	g_clear_object (&self->script);
}

static void
lrg_scripting_crispy_finalize (GObject *object)
{
	LrgScriptingCrispy *self = LRG_SCRIPTING_CRISPY (object);

	g_clear_object (&self->script);
	g_clear_object (&self->compiler);
	g_clear_object (&self->cache);

	G_OBJECT_CLASS (lrg_scripting_crispy_parent_class)->finalize (object);
}

static void
lrg_scripting_crispy_class_init (LrgScriptingCrispyClass *klass)
{
	GObjectClass     *object_class = G_OBJECT_CLASS (klass);
	LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);

	object_class->finalize = lrg_scripting_crispy_finalize;

	scripting_class->load_file         = crispy_load_file;
	scripting_class->load_string       = crispy_load_string;
	scripting_class->call_function     = crispy_call_function;
	scripting_class->register_function = crispy_register_function;
	scripting_class->get_global        = crispy_get_global;
	scripting_class->set_global        = crispy_set_global;
	scripting_class->reset             = crispy_reset;
}

static void
lrg_scripting_crispy_init (LrgScriptingCrispy *self)
{
	self->script = NULL;
}

LrgScriptingCrispy *
lrg_scripting_crispy_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPTING_CRISPY, NULL);
}
