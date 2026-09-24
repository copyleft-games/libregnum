/* lrg-scripting-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Registry of available scripting-language backends.
 */

#include <gmodule.h>
#include <string.h>

#include "lrg-scripting-manager.h"
#include "lrg-scripting.h"
#include "lrg-scripting-native.h"

#ifdef LRG_HAS_LUAJIT
#include "lrg-scripting-lua.h"
#endif
#ifdef LRG_HAS_PYTHON
#include "lrg-scripting-python.h"
#endif
#ifdef LRG_HAS_GJS
#include "lrg-scripting-gjs.h"
#endif
#ifdef LRG_HAS_CRISPY
#include "lrg-scripting-crispy.h"
#endif

typedef LrgScripting * (*LrgScriptingFactory) (void);

typedef struct
{
	LrgScriptLanguage    language;
	const gchar         *display_name;
	const gchar         *extension;
	gboolean             available;
	LrgScriptingFactory  factory;
} BackendDesc;

/* A backend registered at runtime by an embedder (owns its strings/user_data). */
typedef struct
{
	LrgScriptLanguage        language;
	gchar                   *display_name;
	gchar                   *extension;
	LrgScriptingFactoryFunc  factory;
	gpointer                 user_data;
	GDestroyNotify           destroy;
} DynamicBackend;

#ifdef LRG_HAS_LUAJIT
static LrgScripting *
make_lua (void)
{
	return LRG_SCRIPTING (lrg_scripting_lua_new ());
}
#endif

#ifdef LRG_HAS_PYTHON
static LrgScripting *
make_python (void)
{
	return LRG_SCRIPTING (lrg_scripting_python_new ());
}
#endif

#ifdef LRG_HAS_GJS
static LrgScripting *
make_gjs (void)
{
	return LRG_SCRIPTING (lrg_scripting_gjs_new ());
}
#endif

#ifdef LRG_HAS_CRISPY
static LrgScripting *
make_crispy (void)
{
	return LRG_SCRIPTING (lrg_scripting_crispy_new ());
}
#endif

static LrgScripting *
make_native (void)
{
	return LRG_SCRIPTING (lrg_scripting_native_new ());
}

static const BackendDesc backends[] = {
	{ LRG_SCRIPT_LANGUAGE_LUA, "Lua", "lua",
#ifdef LRG_HAS_LUAJIT
	  TRUE, make_lua
#else
	  FALSE, NULL
#endif
	},
	{ LRG_SCRIPT_LANGUAGE_PYTHON, "Python", "py",
#ifdef LRG_HAS_PYTHON
	  TRUE, make_python
#else
	  FALSE, NULL
#endif
	},
	{ LRG_SCRIPT_LANGUAGE_GJS, "JavaScript (Gjs)", "js",
#ifdef LRG_HAS_GJS
	  TRUE, make_gjs
#else
	  FALSE, NULL
#endif
	},
	{ LRG_SCRIPT_LANGUAGE_CRISPY, "Crispy", "c",
#ifdef LRG_HAS_CRISPY
	  TRUE, make_crispy
#else
	  FALSE, NULL
#endif
	},
	/* Prebuilt shared objects: always available where GModule is */
	{ LRG_SCRIPT_LANGUAGE_NATIVE, "Native", G_MODULE_SUFFIX,
	  TRUE, make_native
	}
};

#define N_BACKENDS (G_N_ELEMENTS (backends))

struct _LrgScriptingManager
{
	GObject parent_instance;

	GArray *dynamic;   /* array of DynamicBackend, registered at runtime */
};

G_DEFINE_FINAL_TYPE (LrgScriptingManager, lrg_scripting_manager, G_TYPE_OBJECT)

static void
dynamic_backend_clear (gpointer data)
{
	DynamicBackend *d = data;

	if (d->destroy != NULL)
		d->destroy (d->user_data);
	g_clear_pointer (&d->display_name, g_free);
	g_clear_pointer (&d->extension, g_free);
}

static void
lrg_scripting_manager_finalize (GObject *object)
{
	LrgScriptingManager *self = LRG_SCRIPTING_MANAGER (object);

	g_clear_pointer (&self->dynamic, g_array_unref);

	G_OBJECT_CLASS (lrg_scripting_manager_parent_class)->finalize (object);
}

static void
lrg_scripting_manager_class_init (LrgScriptingManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_scripting_manager_finalize;
}

static void
lrg_scripting_manager_init (LrgScriptingManager *self)
{
	self->dynamic = g_array_new (FALSE, FALSE, sizeof (DynamicBackend));
	g_array_set_clear_func (self->dynamic, dynamic_backend_clear);
}

static DynamicBackend *
find_dynamic (LrgScriptingManager *self,
              LrgScriptLanguage    language)
{
	guint i;

	if (self->dynamic == NULL)
		return NULL;

	for (i = 0; i < self->dynamic->len; i++)
	{
		DynamicBackend *d = &g_array_index (self->dynamic, DynamicBackend, i);
		if (d->language == language)
			return d;
	}

	return NULL;
}

LrgScriptingManager *
lrg_scripting_manager_get_default (void)
{
	static LrgScriptingManager *singleton = NULL;

	if (g_once_init_enter (&singleton))
	{
		LrgScriptingManager *m = g_object_new (LRG_TYPE_SCRIPTING_MANAGER, NULL);
		g_once_init_leave (&singleton, m);
	}

	return singleton;
}

static const BackendDesc *
find_backend (LrgScriptLanguage language)
{
	guint i;

	for (i = 0; i < N_BACKENDS; i++)
		if (backends[i].language == language)
			return &backends[i];

	return NULL;
}

gboolean
lrg_scripting_manager_is_available (LrgScriptingManager *self,
                                    LrgScriptLanguage    language)
{
	const BackendDesc *desc;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), FALSE);

	desc = find_backend (language);
	if (desc != NULL)
		return desc->available;

	return find_dynamic (self, language) != NULL;
}

LrgScripting *
lrg_scripting_manager_create_context (LrgScriptingManager *self,
                                      LrgScriptLanguage    language)
{
	const BackendDesc *desc;
	DynamicBackend    *dyn;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	desc = find_backend (language);
	if (desc != NULL)
		return (desc->available && desc->factory != NULL) ? desc->factory () : NULL;

	dyn = find_dynamic (self, language);
	if (dyn != NULL)
		return dyn->factory (dyn->user_data);

	return NULL;
}

const gchar *
lrg_scripting_manager_get_display_name (LrgScriptingManager *self,
                                        LrgScriptLanguage    language)
{
	const BackendDesc *desc;
	DynamicBackend    *dyn;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	desc = find_backend (language);
	if (desc != NULL)
		return desc->display_name;

	dyn = find_dynamic (self, language);
	return (dyn != NULL) ? dyn->display_name : NULL;
}

const gchar *
lrg_scripting_manager_get_extension (LrgScriptingManager *self,
                                     LrgScriptLanguage    language)
{
	const BackendDesc *desc;
	DynamicBackend    *dyn;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	desc = find_backend (language);
	if (desc != NULL)
		return desc->extension;

	dyn = find_dynamic (self, language);
	return (dyn != NULL) ? dyn->extension : NULL;
}

guint
lrg_scripting_manager_get_available_count (LrgScriptingManager *self)
{
	guint i, count = 0;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), 0);

	for (i = 0; i < N_BACKENDS; i++)
		if (backends[i].available)
			count++;

	if (self->dynamic != NULL)
		count += self->dynamic->len;

	return count;
}

LrgScriptLanguage *
lrg_scripting_manager_get_available (LrgScriptingManager *self,
                                     guint               *n_languages)
{
	LrgScriptLanguage *out;
	guint              i, count, dyn_len;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	dyn_len = (self->dynamic != NULL) ? self->dynamic->len : 0;

	count = 0;
	for (i = 0; i < N_BACKENDS; i++)
		if (backends[i].available)
			count++;
	count += dyn_len;

	out = g_new0 (LrgScriptLanguage, count > 0 ? count : 1);
	count = 0;
	for (i = 0; i < N_BACKENDS; i++)
		if (backends[i].available)
			out[count++] = backends[i].language;
	for (i = 0; i < dyn_len; i++)
		out[count++] = g_array_index (self->dynamic, DynamicBackend, i).language;

	if (n_languages != NULL)
		*n_languages = count;

	return out;
}

gboolean
lrg_scripting_manager_register_backend (LrgScriptingManager     *self,
                                        LrgScriptLanguage        language,
                                        const gchar             *display_name,
                                        const gchar             *extension,
                                        LrgScriptingFactoryFunc  factory,
                                        gpointer                 user_data,
                                        GDestroyNotify           destroy)
{
	DynamicBackend *existing;
	DynamicBackend  entry;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), FALSE);
	g_return_val_if_fail (factory != NULL, FALSE);

	/* Must not be NONE or collide with a compiled-in backend. */
	if (language == LRG_SCRIPT_LANGUAGE_NONE || find_backend (language) != NULL)
	{
		if (destroy != NULL)
			destroy (user_data);
		return FALSE;
	}

	/* Replace an existing dynamic registration for the same language. */
	existing = find_dynamic (self, language);
	if (existing != NULL)
	{
		if (existing->destroy != NULL)
			existing->destroy (existing->user_data);
		g_clear_pointer (&existing->display_name, g_free);
		g_clear_pointer (&existing->extension, g_free);
		existing->display_name = g_strdup (display_name);
		existing->extension = g_strdup (extension);
		existing->factory = factory;
		existing->user_data = user_data;
		existing->destroy = destroy;
		return TRUE;
	}

	entry.language = language;
	entry.display_name = g_strdup (display_name);
	entry.extension = g_strdup (extension);
	entry.factory = factory;
	entry.user_data = user_data;
	entry.destroy = destroy;
	g_array_append_val (self->dynamic, entry);

	return TRUE;
}

LrgScriptLanguage
lrg_scripting_manager_language_for_path (LrgScriptingManager *self,
                                         const gchar         *path)
{
	const gchar *dot;
	guint        i;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), LRG_SCRIPT_LANGUAGE_NONE);
	g_return_val_if_fail (path != NULL, LRG_SCRIPT_LANGUAGE_NONE);

	/* Only the final extension counts, and only after the last separator */
	dot = strrchr (path, '.');
	if (dot == NULL || strchr (dot, G_DIR_SEPARATOR) != NULL || dot[1] == '\0')
		return LRG_SCRIPT_LANGUAGE_NONE;
	dot++;

	/* Compiled-in backends first, whether or not they were built */
	for (i = 0; i < N_BACKENDS; i++)
		if (g_ascii_strcasecmp (backends[i].extension, dot) == 0)
			return backends[i].language;

	/* Then anything an embedder registered */
	if (self->dynamic != NULL)
	{
		for (i = 0; i < self->dynamic->len; i++)
		{
			DynamicBackend *d = &g_array_index (self->dynamic, DynamicBackend, i);

			if (d->extension != NULL && g_ascii_strcasecmp (d->extension, dot) == 0)
				return d->language;
		}
	}

	return LRG_SCRIPT_LANGUAGE_NONE;
}
