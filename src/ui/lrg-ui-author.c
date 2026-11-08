/* lrg-ui-author.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rewrites a named widget's x/y in a UI layout document and samples a
 * linear UI animation. The canvas drag and the timeline painter stay
 * in the editor; this is the document half they call.
 */

#include "config.h"
#include "lrg-ui-author.h"

#include <string.h>
#include <yaml-glib.h>

typedef struct
{
	gchar *widget;
	gchar *prop;
	gfloat time;
	gfloat value;
} UiKey;

struct _LrgUiAnimation
{
	GObject parent_instance;
	GArray *keys;
};

G_DEFINE_FINAL_TYPE (LrgUiAnimation, lrg_ui_animation, G_TYPE_OBJECT)

typedef struct
{
	const gchar *widget;
	gfloat       x;
	gfloat       y;
	gboolean     found;
} Place;

static void place_node (YamlNode *node, Place *place);

static void
place_member (YamlMapping *mapping,
              const gchar *member_name,
              YamlNode    *member_node,
              gpointer     user_data)
{
	(void) mapping;
	(void) member_name;
	place_node (member_node, user_data);
}

/* Sets x and y on the first mapping whose name matches, then stops. */
static void
place_node (YamlNode *node,
            Place    *place)
{
	if (place->found || node == NULL)
		return;

	if (yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		YamlMapping *map = yaml_node_get_mapping (node);
		const gchar *name = yaml_mapping_get_string_member (map, "name");

		if (g_strcmp0 (name, place->widget) == 0)
		{
			yaml_mapping_set_double_member (map, "x", place->x);
			yaml_mapping_set_double_member (map, "y", place->y);
			place->found = TRUE;
			return;
		}
		yaml_mapping_foreach_member (map, place_member, place);
	}
	else if (yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);
		guint         i;
		guint         n = yaml_sequence_get_length (seq);

		for (i = 0; i < n && !place->found; i++)
			place_node (yaml_sequence_get_element (seq, i), place);
	}
}

gchar *
lrg_ui_layout_place (const gchar  *yaml,
                     const gchar  *widget,
                     gfloat        x,
                     gfloat        y,
                     GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	YamlNode *root;
	Place     place;

	g_return_val_if_fail (yaml != NULL, NULL);
	g_return_val_if_fail (widget != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, yaml, -1, error))
		return NULL;
	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "UI layout has no document");
		return NULL;
	}

	place.widget = widget;
	place.x = x;
	place.y = y;
	place.found = FALSE;
	place_node (root, &place);
	if (!place.found)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "no widget named %s", widget);
		return NULL;
	}

	/* The parser still owns the tree. The generator borrows it. */
	gen = yaml_generator_new ();
	yaml_generator_set_root (gen, root);
	return yaml_generator_to_data (gen, NULL, error);
}

static void
ui_key_clear (gpointer data)
{
	UiKey *key = data;

	g_free (key->widget);
	g_free (key->prop);
}

static void
lrg_ui_animation_finalize (GObject *object)
{
	LrgUiAnimation *self = LRG_UI_ANIMATION (object);

	g_clear_pointer (&self->keys, g_array_unref);
	G_OBJECT_CLASS (lrg_ui_animation_parent_class)->finalize (object);
}

static void
lrg_ui_animation_class_init (LrgUiAnimationClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_ui_animation_finalize;
}

static void
lrg_ui_animation_init (LrgUiAnimation *self)
{
	self->keys = g_array_new (FALSE, FALSE, sizeof (UiKey));
	g_array_set_clear_func (self->keys, ui_key_clear);
}

LrgUiAnimation *
lrg_ui_animation_new (void)
{
	return g_object_new (LRG_TYPE_UI_ANIMATION, NULL);
}

void
lrg_ui_animation_add_key (LrgUiAnimation *self,
                          const gchar    *widget,
                          const gchar    *prop,
                          gfloat          time,
                          gfloat          value)
{
	UiKey key;

	g_return_if_fail (LRG_IS_UI_ANIMATION (self));
	g_return_if_fail (widget != NULL);
	g_return_if_fail (prop != NULL);

	key.widget = g_strdup (widget);
	key.prop = g_strdup (prop);
	key.time = time;
	key.value = value;
	g_array_append_val (self->keys, key);
}

gboolean
lrg_ui_animation_sample (LrgUiAnimation *self,
                         const gchar    *widget,
                         const gchar    *prop,
                         gfloat          time,
                         gfloat         *out)
{
	gboolean have_before = FALSE;
	gboolean have_after = FALSE;
	gfloat   before_t = 0.0f;
	gfloat   before_v = 0.0f;
	gfloat   after_t = 0.0f;
	gfloat   after_v = 0.0f;
	guint    i;

	g_return_val_if_fail (LRG_IS_UI_ANIMATION (self), FALSE);
	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (prop != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	for (i = 0; i < self->keys->len; i++)
	{
		UiKey *key = &g_array_index (self->keys, UiKey, i);

		if (g_strcmp0 (key->widget, widget) != 0 || g_strcmp0 (key->prop, prop) != 0)
			continue;
		if (key->time <= time && (!have_before || key->time >= before_t))
		{
			have_before = TRUE;
			before_t = key->time;
			before_v = key->value;
		}
		if (key->time >= time && (!have_after || key->time <= after_t))
		{
			have_after = TRUE;
			after_t = key->time;
			after_v = key->value;
		}
	}

	if (!have_before && !have_after)
		return FALSE;
	if (!have_before || after_t == before_t)
		*out = have_before ? before_v : after_v;
	else if (!have_after)
		*out = before_v;
	else
		*out = before_v + (after_v - before_v) * ((time - before_t) / (after_t - before_t));
	return TRUE;
}

gchar *
lrg_ui_animation_save (LrgUiAnimation *self)
{
	g_autoptr(YamlMapping) root = NULL;
	g_autoptr(YamlSequence) keys = NULL;
	g_autoptr(YamlNode) node = NULL;
	g_autoptr(YamlDocument) doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	guint i;

	g_return_val_if_fail (LRG_IS_UI_ANIMATION (self), NULL);

	root = yaml_mapping_new ();
	keys = yaml_sequence_new ();
	for (i = 0; i < self->keys->len; i++)
	{
		UiKey *key = &g_array_index (self->keys, UiKey, i);
		g_autoptr(YamlMapping) map = yaml_mapping_new ();

		yaml_mapping_set_string_member (map, "widget", key->widget);
		yaml_mapping_set_string_member (map, "prop", key->prop);
		yaml_mapping_set_double_member (map, "t", key->time);
		yaml_mapping_set_double_member (map, "v", key->value);
		yaml_sequence_add_mapping_element (keys, map);
	}
	yaml_mapping_set_sequence_member (root, "keys", keys);
	node = yaml_node_new_mapping (root);
	doc = yaml_document_new_with_root (node);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, NULL);
}

LrgUiAnimation *
lrg_ui_animation_load (const gchar  *yaml,
                       GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	LrgUiAnimation *self;
	YamlNode       *root;
	YamlNode       *keys_node;
	guint           i;
	guint           n;

	g_return_val_if_fail (yaml != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, yaml, -1, error))
		return NULL;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "UI animation has no mapping");
		return NULL;
	}

	self = lrg_ui_animation_new ();
	keys_node = yaml_mapping_get_member (yaml_node_get_mapping (root), "keys");
	if (keys_node == NULL || yaml_node_get_node_type (keys_node) != YAML_NODE_SEQUENCE)
		return self;

	n = yaml_sequence_get_length (yaml_node_get_sequence (keys_node));
	for (i = 0; i < n; i++)
	{
		YamlNode    *element = yaml_sequence_get_element (yaml_node_get_sequence (keys_node), i);
		YamlMapping *map;
		const gchar *widget;
		const gchar *prop;

		if (element == NULL || yaml_node_get_node_type (element) != YAML_NODE_MAPPING)
			continue;
		map = yaml_node_get_mapping (element);
		widget = yaml_mapping_get_string_member (map, "widget");
		prop = yaml_mapping_get_string_member (map, "prop");
		if (widget == NULL || prop == NULL)
			continue;
		lrg_ui_animation_add_key (self, widget, prop,
		                          (gfloat) yaml_mapping_get_double_member (map, "t"),
		                          (gfloat) yaml_mapping_get_double_member (map, "v"));
	}
	return self;
}
