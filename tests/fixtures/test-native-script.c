/* test-native-script.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fixture for test-scripting-native: one C source exercised both as a
 * prebuilt shared object (LrgScriptingNative) and, when Crispy is built, as
 * a compiled-at-load script (LrgScriptingCrispy).  It deliberately avoids
 * libregnum headers for its own functions, as extension authors may, and
 * declares the two libregnum entry points it calls.
 */

#include <gio/gio.h>
#include <gmodule.h>

/* The lifecycle macro lives in a libregnum header */
#define LIBREGNUM_COMPILATION
#include "scripting/lrg-script-module.h"

struct _LrgScripting;

/* Resolved from the host process at load time */
extern gboolean lrg_scripting_native_call_host (struct _LrgScripting *scripting,
                                                const gchar *name, guint n_args,
                                                const GValue *args,
                                                GValue *return_value,
                                                GError **error);
extern gboolean lrg_scripting_get_global (struct _LrgScripting *scripting,
                                          const gchar *name, GValue *value,
                                          GError **error);

#define FIXTURE_ABI(name) \
	G_MODULE_EXPORT gboolean name (struct _LrgScripting *scripting, guint n_args, \
	                               const GValue *args, GValue *return_value,     \
	                               GError **error)

/* Declarations keep -Wmissing-prototypes quiet for the exports */
FIXTURE_ABI (fixture_add);
FIXTURE_ABI (fixture_greet);
FIXTURE_ABI (fixture_fail);
FIXTURE_ABI (fixture_call_back);
FIXTURE_ABI (fixture_read_global);
FIXTURE_ABI (fixture_nothing);
FIXTURE_ABI (fixture_lifecycle_count);

/* Lifecycle state for LRG_DEFINE_SCRIPT */
static gint started;
static gint detached;
static gdouble total_delta;

static void on_start (void)          { started++; }
static void on_update (double delta) { total_delta += delta; }
static void on_detach (void)         { detached++; }

LRG_DEFINE_SCRIPT (on_start, on_update, on_detach)

/* Sums every numeric argument into a double */
FIXTURE_ABI (fixture_add)
{
	gdouble sum;
	guint i;

	(void)scripting;
	(void)error;

	sum = 0.0;
	for (i = 0; i < n_args; i++)
	{
		GValue d = G_VALUE_INIT;

		g_value_init (&d, G_TYPE_DOUBLE);
		if (g_value_transform (&args[i], &d))
			sum += g_value_get_double (&d);
		g_value_unset (&d);
	}

	g_value_init (return_value, G_TYPE_DOUBLE);
	g_value_set_double (return_value, sum);
	return TRUE;
}

/* Returns "hello <name>" */
FIXTURE_ABI (fixture_greet)
{
	(void)scripting;

	if (n_args != 1 || !G_VALUE_HOLDS_STRING (&args[0]))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "greet expects one string");
		return FALSE;
	}

	g_value_init (return_value, G_TYPE_STRING);
	g_value_take_string (return_value,
	                     g_strdup_printf ("hello %s", g_value_get_string (&args[0])));
	return TRUE;
}

/* Always fails with a descriptive error */
FIXTURE_ABI (fixture_fail)
{
	(void)scripting;
	(void)n_args;
	(void)args;
	(void)return_value;

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "fixture failure");
	return FALSE;
}

/* Calls the host's "double_it" with the first argument */
FIXTURE_ABI (fixture_call_back)
{
	return lrg_scripting_native_call_host (scripting, "double_it", n_args, args,
	                                       return_value, error);
}

/* Returns the host-side global "counter" plus one */
FIXTURE_ABI (fixture_read_global)
{
	GValue counter = G_VALUE_INIT;

	(void)n_args;
	(void)args;

	if (!lrg_scripting_get_global (scripting, "counter", &counter, error))
		return FALSE;

	g_value_init (return_value, G_TYPE_INT64);
	g_value_set_int64 (return_value, g_value_get_int64 (&counter) + 1);
	g_value_unset (&counter);
	return TRUE;
}

/* Succeeds without a value */
FIXTURE_ABI (fixture_nothing)
{
	(void)scripting;
	(void)n_args;
	(void)args;
	(void)return_value;
	(void)error;
	return TRUE;
}

/* Reports started * 100 + detached * 10 + total_delta */
FIXTURE_ABI (fixture_lifecycle_count)
{
	(void)scripting;
	(void)n_args;
	(void)args;
	(void)error;

	g_value_init (return_value, G_TYPE_DOUBLE);
	g_value_set_double (return_value, started * 100 + detached * 10 + total_delta);
	return TRUE;
}
