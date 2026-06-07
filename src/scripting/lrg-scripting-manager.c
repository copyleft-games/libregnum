/* lrg-scripting-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Registry of available scripting-language backends.
 */

#include "lrg-scripting-manager.h"
#include "lrg-scripting.h"

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
	}
};

#define N_BACKENDS (G_N_ELEMENTS (backends))

struct _LrgScriptingManager
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgScriptingManager, lrg_scripting_manager, G_TYPE_OBJECT)

static void
lrg_scripting_manager_class_init (LrgScriptingManagerClass *klass)
{
}

static void
lrg_scripting_manager_init (LrgScriptingManager *self)
{
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
	return (desc != NULL) ? desc->available : FALSE;
}

LrgScripting *
lrg_scripting_manager_create_context (LrgScriptingManager *self,
                                      LrgScriptLanguage    language)
{
	const BackendDesc *desc;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	desc = find_backend (language);
	if (desc == NULL || !desc->available || desc->factory == NULL)
		return NULL;

	return desc->factory ();
}

const gchar *
lrg_scripting_manager_get_display_name (LrgScriptingManager *self,
                                        LrgScriptLanguage    language)
{
	const BackendDesc *desc;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	desc = find_backend (language);
	return (desc != NULL) ? desc->display_name : NULL;
}

const gchar *
lrg_scripting_manager_get_extension (LrgScriptingManager *self,
                                     LrgScriptLanguage    language)
{
	const BackendDesc *desc;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	desc = find_backend (language);
	return (desc != NULL) ? desc->extension : NULL;
}

guint
lrg_scripting_manager_get_available_count (LrgScriptingManager *self)
{
	guint i, count = 0;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), 0);

	for (i = 0; i < N_BACKENDS; i++)
		if (backends[i].available)
			count++;

	return count;
}

LrgScriptLanguage *
lrg_scripting_manager_get_available (LrgScriptingManager *self,
                                     guint               *n_languages)
{
	LrgScriptLanguage *out;
	guint              i, count = 0;

	g_return_val_if_fail (LRG_IS_SCRIPTING_MANAGER (self), NULL);

	for (i = 0; i < N_BACKENDS; i++)
		if (backends[i].available)
			count++;

	out = g_new0 (LrgScriptLanguage, count > 0 ? count : 1);
	count = 0;
	for (i = 0; i < N_BACKENDS; i++)
		if (backends[i].available)
			out[count++] = backends[i].language;

	if (n_languages != NULL)
		*n_languages = count;

	return out;
}
