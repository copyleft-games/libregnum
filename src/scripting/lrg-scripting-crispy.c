/* lrg-scripting-crispy.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Crispy (compiled-C) scripting backend.
 *
 * Each load compiles (or reuses a cached build of) one C source with Crispy,
 * loads the shared object without calling main(), and hands the script to
 * LrgScriptingNative as a unit.  Symbol lookups and the optional main() then
 * follow the native backend's rules.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-scripting-crispy.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"
#include <crispy.h>
#include <glib/gstdio.h>
#include <unistd.h>

struct _LrgScriptingCrispy
{
	LrgScriptingNative parent_instance;

	CrispyGccCompiler *compiler;   /* lazily created GCC compiler */
	CrispyFileCache   *cache;      /* lazily created compile cache */
	GString           *cflags;     /* flags added with add_cflags() */
	gchar             *cache_dir;  /* NULL means Crispy's default */
};

G_DEFINE_FINAL_TYPE (LrgScriptingCrispy, lrg_scripting_crispy, LRG_TYPE_SCRIPTING_NATIVE)

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
		self->cache = crispy_file_cache_new_with_dir (self->cache_dir);
	return self->cache != NULL;
}

/* Symbol lookup for a loaded CrispyScript unit */
static gpointer
crispy_unit_lookup (gpointer unit, const gchar *symbol_name)
{
	gpointer symbol;

	symbol = NULL;
	if (!crispy_script_lookup_symbol (CRISPY_SCRIPT (unit), symbol_name, &symbol))
		return NULL;
	return symbol;
}

/*
 * Build the per-script compiler flags: every search path becomes a quoted
 * -I directory, followed by the flags added with add_cflags().
 */
static gchar *
crispy_build_flags (LrgScriptingCrispy *self)
{
	const gchar * const *paths;
	GString             *flags;
	guint                i;

	flags = g_string_new (NULL);
	paths = lrg_scripting_native_get_search_paths (LRG_SCRIPTING_NATIVE (self));
	for (i = 0; paths[i] != NULL; i++)
	{
		g_autofree gchar *quoted = g_shell_quote (paths[i]);

		if (flags->len > 0)
			g_string_append_c (flags, ' ');
		g_string_append (flags, "-I");
		g_string_append (flags, quoted);
	}
	if (self->cflags->len > 0)
	{
		if (flags->len > 0)
			g_string_append_c (flags, ' ');
		g_string_append (flags, self->cflags->str);
	}

	return g_string_free (flags, FALSE);
}

/*
 * Compile + load @script, then register it as a native unit (which runs an
 * exported main() when present).  Takes ownership of @script.
 */
static gboolean
crispy_adopt_script (LrgScriptingCrispy  *self,
                     CrispyScript        *script,
                     const gchar         *label,
                     GError             **error)
{
	g_autofree gchar *flags = NULL;
	g_autoptr(GError) local_error = NULL;

	flags = crispy_build_flags (self);
	if (flags[0] != '\0')
		crispy_script_set_extra_flags (script, flags);

	if (!crispy_script_load (script, &local_error))
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX,
		             "Failed to compile '%s': %s", label,
		             local_error != NULL ? local_error->message : "unknown error");
		g_object_unref (script);
		return FALSE;
	}

	lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loaded Crispy script: %s", label);

	return lrg_scripting_native_add_unit (LRG_SCRIPTING_NATIVE (self), script,
	                                      crispy_unit_lookup, g_object_unref,
	                                      error);
}

static gboolean
crispy_load_file (LrgScripting  *scripting,
                  const gchar   *path,
                  GError       **error)
{
	LrgScriptingCrispy *self = LRG_SCRIPTING_CRISPY (scripting);
	g_autoptr(GError)   local_error = NULL;
	CrispyScript       *script;

	g_return_val_if_fail (path != NULL, FALSE);

	if (!crispy_ensure_toolchain (self, &local_error))
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
		             "Crispy toolchain unavailable: %s",
		             local_error != NULL ? local_error->message : "unknown error");
		return FALSE;
	}

	script = crispy_script_new_from_file (path, CRISPY_COMPILER (self->compiler),
	                                      CRISPY_CACHE_PROVIDER (self->cache),
	                                      CRISPY_FLAG_NONE, &local_error);
	if (script == NULL)
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
		             "Failed to read '%s': %s", path,
		             local_error != NULL ? local_error->message : "unknown error");
		return FALSE;
	}

	return crispy_adopt_script (self, script, path, error);
}

static gboolean
crispy_load_string (LrgScripting  *scripting,
                    const gchar   *name,
                    const gchar   *code,
                    GError       **error)
{
	LrgScriptingCrispy *self = LRG_SCRIPTING_CRISPY (scripting);
	g_autoptr(GError)   local_error = NULL;
	g_autofree gchar   *path = NULL;
	CrispyScript       *script;
	gint                fd;

	g_return_val_if_fail (code != NULL, FALSE);

	if (!crispy_ensure_toolchain (self, &local_error))
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED,
		             "Crispy toolchain unavailable: %s",
		             local_error != NULL ? local_error->message : "unknown error");
		return FALSE;
	}

	/*
	 * Source strings are complete translation units (with their own
	 * includes and exports), not Crispy's inline main() bodies, so they are
	 * written to a temporary file and compiled like load_file().
	 */
	fd = g_file_open_tmp ("lrg-crispy-XXXXXX.c", &path, &local_error);
	if (fd < 0)
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
		             "Cannot stage '%s': %s", name != NULL ? name : "(unnamed)",
		             local_error->message);
		return FALSE;
	}
	close (fd);
	if (!g_file_set_contents (path, code, -1, &local_error))
	{
		g_unlink (path);
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
		             "Cannot stage '%s': %s", name != NULL ? name : "(unnamed)",
		             local_error->message);
		return FALSE;
	}

	script = crispy_script_new_from_file (path, CRISPY_COMPILER (self->compiler),
	                                      CRISPY_CACHE_PROVIDER (self->cache),
	                                      CRISPY_FLAG_NONE, &local_error);
	if (script == NULL)
	{
		g_unlink (path);
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
		             "Failed to read '%s': %s", name != NULL ? name : "(unnamed)",
		             local_error->message);
		return FALSE;
	}

	/* The module is loaded from the cache, so the staged source can go */
	if (!crispy_adopt_script (self, script, name != NULL ? name : path, error))
	{
		g_unlink (path);
		return FALSE;
	}
	g_unlink (path);
	return TRUE;
}

static void
lrg_scripting_crispy_finalize (GObject *object)
{
	LrgScriptingCrispy *self = LRG_SCRIPTING_CRISPY (object);

	/* Units (and their modules) go first, in the parent's finalize, after
	 * which the toolchain objects are no longer referenced by scripts. */
	G_OBJECT_CLASS (lrg_scripting_crispy_parent_class)->finalize (object);

	g_clear_object (&self->compiler);
	g_clear_object (&self->cache);
	g_string_free (self->cflags, TRUE);
	g_free (self->cache_dir);
}

static void
lrg_scripting_crispy_class_init (LrgScriptingCrispyClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);

	object_class->finalize = lrg_scripting_crispy_finalize;

	scripting_class->load_file   = crispy_load_file;
	scripting_class->load_string = crispy_load_string;
}

static void
lrg_scripting_crispy_init (LrgScriptingCrispy *self)
{
	self->cflags = g_string_new (NULL);
}

LrgScriptingCrispy *
lrg_scripting_crispy_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPTING_CRISPY, NULL);
}

/*
 * lrg_scripting_crispy_add_cflags:
 * @self: an #LrgScriptingCrispy
 * @cflags: extra compiler flags, in shell syntax
 *
 * Appends compiler flags used for every later load.
 */
void
lrg_scripting_crispy_add_cflags (LrgScriptingCrispy *self,
                                 const gchar        *cflags)
{
	g_return_if_fail (LRG_IS_SCRIPTING_CRISPY (self));
	g_return_if_fail (cflags != NULL);

	if (self->cflags->len > 0)
		g_string_append_c (self->cflags, ' ');
	g_string_append (self->cflags, cflags);
}

/*
 * lrg_scripting_crispy_set_cache_dir:
 * @self: an #LrgScriptingCrispy
 * @cache_dir: (type filename) (nullable): the cache directory
 *
 * Chooses where compiled objects are cached.
 */
void
lrg_scripting_crispy_set_cache_dir (LrgScriptingCrispy *self,
                                    const gchar        *cache_dir)
{
	g_return_if_fail (LRG_IS_SCRIPTING_CRISPY (self));

	g_free (self->cache_dir);
	self->cache_dir = g_strdup (cache_dir);
	g_clear_object (&self->cache);
}
