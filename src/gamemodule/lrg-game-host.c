/* lrg-game-host.c - Host abstraction that drives a loaded game
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_GAMEMODULE

#include "lrg-game-host.h"
#include "../core/lrg-data-loader.h"
#include "../core/lrg-engine.h"
#include "../lrg-log.h"

#include <yaml-glib.h>

G_DEFINE_INTERFACE (LrgGameHost, lrg_game_host, G_TYPE_OBJECT)

static void
lrg_game_host_default_init (LrgGameHostInterface *iface)
{
    (void) iface;
}

/* ==========================================================================
 * Public API (dispatch wrappers)
 * ========================================================================== */

LrgEngine *
lrg_game_host_get_engine (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), NULL);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    g_return_val_if_fail (iface->get_engine != NULL, NULL);

    return iface->get_engine (self);
}

LrgWindow *
lrg_game_host_get_window (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), NULL);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_window == NULL)
        return NULL;

    return iface->get_window (self);
}

gboolean
lrg_game_host_get_owns_window (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), FALSE);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_owns_window == NULL)
        return FALSE;

    return iface->get_owns_window (self);
}

void
lrg_game_host_begin_frame (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    g_return_if_fail (iface->begin_frame != NULL);

    iface->begin_frame (self);
}

void
lrg_game_host_clear (LrgGameHost *self,
                     GrlColor    *color)
{
    LrgGameHostInterface *iface;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->clear != NULL)
        iface->clear (self, color);
}

void
lrg_game_host_end_frame (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    g_return_if_fail (iface->end_frame != NULL);

    iface->end_frame (self);
}

void
lrg_game_host_get_render_size (LrgGameHost *self,
                               gint        *width,
                               gint        *height)
{
    LrgGameHostInterface *iface;

    if (width != NULL)
        *width = 0;
    if (height != NULL)
        *height = 0;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_render_size != NULL)
        iface->get_render_size (self, width, height);
}

gdouble
lrg_game_host_get_frame_delta (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), 0.0);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_frame_delta == NULL)
        return 0.0;

    return iface->get_frame_delta (self);
}

LrgInputSoftware *
lrg_game_host_get_input_source (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), NULL);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_input_source == NULL)
        return NULL;

    return iface->get_input_source (self);
}

GPtrArray *
lrg_game_host_load_autoloads (LrgGameHost  *self,
                              const gchar  *project_dir,
                              GError      **error)
{
	g_autofree gchar     *manifest = NULL;
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GPtrArray)  out = NULL;
	LrgEngine            *engine;
	LrgDataLoader        *loader;
	YamlNode             *root;
	YamlNode             *node;
	guint                 i;
	guint                 n;

	g_return_val_if_fail (LRG_IS_GAME_HOST (self), NULL);
	g_return_val_if_fail (project_dir != NULL, NULL);

	engine = lrg_game_host_get_engine (self);
	if (engine == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "Game host has no engine");
		return NULL;
	}
	loader = lrg_engine_get_data_loader (engine);
	if (!LRG_IS_DATA_LOADER (loader))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "Engine data loader is not available");
		return NULL;
	}
	manifest = g_build_filename (project_dir, "project.ryaml", NULL);
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, manifest, error))
		return NULL;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     "project.ryaml is not a mapping");
		return NULL;
	}

	out = g_ptr_array_new_with_free_func (g_object_unref);
	node = yaml_mapping_get_member (yaml_node_get_mapping (root), "autoloads");
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return g_steal_pointer (&out);

	n = yaml_sequence_get_length (yaml_node_get_sequence (node));
	for (i = 0; i < n; i++)
	{
		YamlNode         *el;
		const gchar      *rel;
		g_autofree gchar *path = NULL;
		GObject          *obj;

		el = yaml_sequence_get_element (yaml_node_get_sequence (node), i);
		if (el == NULL || yaml_node_get_node_type (el) != YAML_NODE_SCALAR)
			continue;
		rel = yaml_node_get_scalar (el);
		if (rel == NULL || rel[0] == '\0')
			continue;
		if (g_path_is_absolute (rel))
			path = g_strdup (rel);
		else
			path = g_build_filename (project_dir, rel, NULL);
		obj = lrg_data_loader_load_file (loader, path, error);
		if (obj == NULL)
			return NULL;
		g_ptr_array_add (out, obj);
	}
	return g_steal_pointer (&out);
}
