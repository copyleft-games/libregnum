/* lrg-3d-enums.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-3d-enums.h"

GType
lrg_arrangement_kind_get_type (void)
{
	static gsize g_define_type_id__volatile = 0;

	if (g_once_init_enter (&g_define_type_id__volatile))
	{
		static const GEnumValue values[] = {
			{ LRG_ARRANGEMENT_KIND_SINGLE_PANEL, "LRG_ARRANGEMENT_KIND_SINGLE_PANEL", "single-panel" },
			{ LRG_ARRANGEMENT_KIND_PER_WINDOW, "LRG_ARRANGEMENT_KIND_PER_WINDOW", "per-window" },
			{ LRG_ARRANGEMENT_KIND_FREE, "LRG_ARRANGEMENT_KIND_FREE", "free" },
			{ LRG_ARRANGEMENT_KIND_CAROUSEL, "LRG_ARRANGEMENT_KIND_CAROUSEL", "carousel" },
			{ LRG_ARRANGEMENT_KIND_GIT_DEPTH, "LRG_ARRANGEMENT_KIND_GIT_DEPTH", "git-depth" },
			{ 0, NULL, NULL }
		};
		GType g_define_type_id =
			g_enum_register_static (g_intern_static_string ("LrgArrangementKind"),
									values);
		g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
	}

	return g_define_type_id__volatile;
}

const gchar *
lrg_arrangement_kind_to_string (LrgArrangementKind kind)
{
	switch (kind)
	{
	case LRG_ARRANGEMENT_KIND_PER_WINDOW:
		return "per-window";
	case LRG_ARRANGEMENT_KIND_FREE:
		return "free";
	case LRG_ARRANGEMENT_KIND_CAROUSEL:
		return "carousel";
	case LRG_ARRANGEMENT_KIND_GIT_DEPTH:
		return "git-depth";
	case LRG_ARRANGEMENT_KIND_SINGLE_PANEL:
	default:
		return "single-panel";
	}
}

gboolean
lrg_arrangement_kind_from_string (const gchar        *str,
								  LrgArrangementKind *out_kind)
{
	LrgArrangementKind kind = LRG_ARRANGEMENT_KIND_SINGLE_PANEL;
	gboolean known = TRUE;

	if (str == NULL || *str == '\0'
		|| g_ascii_strcasecmp (str, "single-panel") == 0
		|| g_ascii_strcasecmp (str, "single") == 0)
		kind = LRG_ARRANGEMENT_KIND_SINGLE_PANEL;
	else if (g_ascii_strcasecmp (str, "per-window") == 0
			 || g_ascii_strcasecmp (str, "windows") == 0)
		kind = LRG_ARRANGEMENT_KIND_PER_WINDOW;
	else if (g_ascii_strcasecmp (str, "free") == 0)
		kind = LRG_ARRANGEMENT_KIND_FREE;
	else if (g_ascii_strcasecmp (str, "carousel") == 0
			 || g_ascii_strcasecmp (str, "rolodex") == 0)
		kind = LRG_ARRANGEMENT_KIND_CAROUSEL;
	else if (g_ascii_strcasecmp (str, "git-depth") == 0
			 || g_ascii_strcasecmp (str, "git") == 0)
		kind = LRG_ARRANGEMENT_KIND_GIT_DEPTH;
	else
		known = FALSE;

	if (out_kind != NULL)
		*out_kind = kind;

	return known;
}

GType
lrg_environment_kind_get_type (void)
{
	static gsize g_define_type_id__volatile = 0;

	if (g_once_init_enter (&g_define_type_id__volatile))
	{
		static const GEnumValue values[] = {
			{ LRG_ENVIRONMENT_KIND_VOID, "LRG_ENVIRONMENT_KIND_VOID", "void" },
			{ LRG_ENVIRONMENT_KIND_WORKSHOP, "LRG_ENVIRONMENT_KIND_WORKSHOP", "workshop" },
			{ LRG_ENVIRONMENT_KIND_COCKPIT, "LRG_ENVIRONMENT_KIND_COCKPIT", "cockpit" },
			{ 0, NULL, NULL }
		};
		GType g_define_type_id =
			g_enum_register_static (g_intern_static_string ("LrgEnvironmentKind"),
									values);
		g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
	}

	return g_define_type_id__volatile;
}

const gchar *
lrg_environment_kind_to_string (LrgEnvironmentKind kind)
{
	switch (kind)
	{
	case LRG_ENVIRONMENT_KIND_WORKSHOP:
		return "workshop";
	case LRG_ENVIRONMENT_KIND_COCKPIT:
		return "cockpit";
	case LRG_ENVIRONMENT_KIND_VOID:
	default:
		return "void";
	}
}

gboolean
lrg_environment_kind_from_string (const gchar        *str,
								  LrgEnvironmentKind *out_kind)
{
	LrgEnvironmentKind kind = LRG_ENVIRONMENT_KIND_VOID;
	gboolean known = TRUE;

	if (str == NULL || *str == '\0'
		|| g_ascii_strcasecmp (str, "void") == 0
		|| g_ascii_strcasecmp (str, "none") == 0)
		kind = LRG_ENVIRONMENT_KIND_VOID;
	else if (g_ascii_strcasecmp (str, "workshop") == 0)
		kind = LRG_ENVIRONMENT_KIND_WORKSHOP;
	else if (g_ascii_strcasecmp (str, "cockpit") == 0)
		kind = LRG_ENVIRONMENT_KIND_COCKPIT;
	else
		known = FALSE;

	if (out_kind != NULL)
		*out_kind = kind;

	return known;
}
