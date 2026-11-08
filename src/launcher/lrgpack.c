/* lrgpack.c - pack a libregnum project and export a bundle
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 *   lrgpack pack --project DIR --output game.rres
 *   lrgpack export --project DIR --profile linux-tar --output game.tar.gz
 *   lrgpack template --id empty --dest DIR --name "My Game"
 *   lrgpack refs --project DIR --path assets/hero.png
 *   lrgpack rename --project DIR --from assets/a.png --to assets/b.png
 */

#include <libregnum.h>
#include <string.h>

static const gchar *license_text =
	"lrgpack is part of Libregnum.\n"
	"SPDX-License-Identifier: AGPL-3.0-or-later\n"
	"Copyright 2026 Zach Podbielniak\n";

static void
print_help (void)
{
	g_print (
		"lrgpack - pack a libregnum project\n"
		"\n"
		"Usage:\n"
		"  lrgpack pack --project DIR --output FILE.rres\n"
		"  lrgpack export --project DIR --profile PROFILE --output PATH\n"
		"  lrgpack template --id empty|mod --dest DIR [--name NAME]\n"
		"  lrgpack refs --project DIR --path REL\n"
		"  lrgpack rename --project DIR --from OLD --to NEW\n"
		"  lrgpack --help\n"
		"  lrgpack --license\n"
		"  lrgpack --version\n"
		"\n"
		"Profiles: rres, vfs, linux-tar, flatpak, appimage, windows-zip\n"
		"\n"
		"Examples:\n"
		"  lrgpack pack --project ./game --output build/game.rres\n"
		"  lrgpack export --project ./game --profile linux-tar --output build/game.tar.gz\n"
		"  lrgpack template --id mod --dest ./my-mod --name \"My Mod\"\n"
		"  lrgpack refs --project ./game --path assets/hero.png\n");
}

static gboolean
cmd_pack (int    argc,
          char **argv,
          gboolean as_export)
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPackWriter) writer = NULL;
	gchar *project = NULL;
	gchar *output = NULL;
	gchar *profile = NULL;
	gboolean cook = TRUE;
	GOptionEntry entries[] = {
		{ "project", 'p', 0, G_OPTION_ARG_FILENAME, &project, "Project root", "DIR" },
		{ "output", 'o', 0, G_OPTION_ARG_FILENAME, &output, "Destination file or directory", "PATH" },
		{ "profile", 't', 0, G_OPTION_ARG_STRING, &profile, "Export profile", "NAME" },
		{ "no-cook", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &cook, "Pack original bytes", NULL },
		{ NULL }
	};

	ctx = g_option_context_new (as_export ? "- export a bundle" : "- write an rres pack");
	g_option_context_add_main_entries (ctx, entries, NULL);
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return FALSE;
	}
	if (project == NULL || output == NULL || (as_export && profile == NULL))
	{
		g_printerr ("pack/export needs --project and --output%s\n",
		            as_export ? " and --profile" : "");
		g_free (project);
		g_free (output);
		g_free (profile);
		return FALSE;
	}
	writer = lrg_pack_writer_new ();
	lrg_pack_writer_set_project (writer, project);
	lrg_pack_writer_set_cook (writer, cook);
	if (!lrg_pack_writer_scan (writer, &error) ||
	    !lrg_pack_writer_export (writer, as_export ? profile : "rres", output, &error))
	{
		g_printerr ("%s\n", error->message);
		g_free (project);
		g_free (output);
		g_free (profile);
		return FALSE;
	}
	g_print ("%s\n", lrg_pack_writer_get_log (writer));
	g_print ("wrote %s\n", output);
	g_free (project);
	g_free (output);
	g_free (profile);
	return TRUE;
}

static gboolean
cmd_template (int    argc,
              char **argv)
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError) error = NULL;
	gchar *id = NULL;
	gchar *dest = NULL;
	gchar *name = NULL;
	GOptionEntry entries[] = {
		{ "id", 'i', 0, G_OPTION_ARG_STRING, &id, "Template id (empty or mod)", "ID" },
		{ "dest", 'd', 0, G_OPTION_ARG_FILENAME, &dest, "Destination directory", "DIR" },
		{ "name", 'n', 0, G_OPTION_ARG_STRING, &name, "Project name", "NAME" },
		{ NULL }
	};
	gboolean ok;

	ctx = g_option_context_new ("- write a project from a template");
	g_option_context_add_main_entries (ctx, entries, NULL);
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return FALSE;
	}
	ok = id != NULL && dest != NULL &&
	     lrg_project_template_instantiate (id, dest, name, &error);
	if (!ok)
		g_printerr ("%s\n", (error != NULL) ? error->message : "template needs --id and --dest");
	else
		g_print ("wrote %s\n", dest);
	g_free (id);
	g_free (dest);
	g_free (name);
	return ok;
}

static gboolean
cmd_refs (int    argc,
          char **argv,
          gboolean rename)
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgAssetRefs) refs = NULL;
	gchar *project = NULL;
	gchar *path = NULL;
	gchar *from = NULL;
	gchar *to = NULL;
	GOptionEntry entries[] = {
		{ "project", 'p', 0, G_OPTION_ARG_FILENAME, &project, "Project root", "DIR" },
		{ "path", 0, 0, G_OPTION_ARG_STRING, &path, "Relative path to query", "REL" },
		{ "from", 0, 0, G_OPTION_ARG_STRING, &from, "Old relative path", "OLD" },
		{ "to", 0, 0, G_OPTION_ARG_STRING, &to, "New relative path", "NEW" },
		{ NULL }
	};
	gboolean ok = FALSE;

	ctx = g_option_context_new (rename ? "- rename a file and its quotes" : "- list files that quote a path");
	g_option_context_add_main_entries (ctx, entries, NULL);
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return FALSE;
	}
	if (project == NULL)
	{
		g_printerr ("refs/rename needs --project\n");
		goto out;
	}
	refs = lrg_asset_refs_new ();
	if (!lrg_asset_refs_scan (refs, project, &error))
	{
		g_printerr ("%s\n", error->message);
		goto out;
	}
	if (rename)
	{
		if (from == NULL || to == NULL)
		{
			g_printerr ("rename needs --from and --to\n");
			goto out;
		}
		ok = lrg_asset_refs_rename (refs, from, to, &error);
		if (!ok)
			g_printerr ("%s\n", error->message);
		else
			g_print ("renamed %s -> %s\n", from, to);
	}
	else
	{
		g_auto(GStrv) who = NULL;
		guint i;

		if (path == NULL)
		{
			g_printerr ("refs needs --path\n");
			goto out;
		}
		who = lrg_asset_refs_get_referrers (refs, path);
		for (i = 0; who[i] != NULL; i++)
			g_print ("%s\n", who[i]);
		ok = TRUE;
	}
out:
	g_free (project);
	g_free (path);
	g_free (from);
	g_free (to);
	return ok;
}

int
main (int argc, char **argv)
{
	const gchar *verb;

	if (argc < 2 || g_strcmp0 (argv[1], "--help") == 0 || g_strcmp0 (argv[1], "-h") == 0)
	{
		print_help ();
		return (argc < 2) ? 1 : 0;
	}
	if (g_strcmp0 (argv[1], "--license") == 0)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (g_strcmp0 (argv[1], "--version") == 0)
	{
		g_print ("%s\n", LRG_VERSION_STRING);
		return 0;
	}
	verb = argv[1];
	argv[1] = argv[0];
	if (g_strcmp0 (verb, "pack") == 0)
		return cmd_pack (argc - 1, argv + 1, FALSE) ? 0 : 1;
	if (g_strcmp0 (verb, "export") == 0)
		return cmd_pack (argc - 1, argv + 1, TRUE) ? 0 : 1;
	if (g_strcmp0 (verb, "template") == 0)
		return cmd_template (argc - 1, argv + 1) ? 0 : 1;
	if (g_strcmp0 (verb, "refs") == 0)
		return cmd_refs (argc - 1, argv + 1, FALSE) ? 0 : 1;
	if (g_strcmp0 (verb, "rename") == 0)
		return cmd_refs (argc - 1, argv + 1, TRUE) ? 0 : 1;
	g_printerr ("Unknown command '%s' (try --help)\n", verb);
	return 1;
}
