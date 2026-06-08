/* lrg-reel-loader.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-loader.h"
#include "lrg-reel.h"
#include "lrg-reel-clip.h"
#include "lrg-reel-solid-clip.h"
#include "lrg-reel-gradient-clip.h"
#include "lrg-reel-image-clip.h"
#include "lrg-reel-text-clip.h"
#include "lrg-reel-shape-clip.h"
#include "lrg-reel-caption-clip.h"
#include "../core/lrg-registry.h"
#include <graylib.h>
#include <yaml-glib.h>
#include <string.h>

#define LRG_REEL_LOADER_ERROR (g_quark_from_static_string ("lrg-reel-loader"))

void
lrg_reel_register_types (LrgRegistry *registry)
{
    g_return_if_fail (LRG_IS_REGISTRY (registry));

    lrg_registry_register (registry, "solid-clip", LRG_TYPE_REEL_SOLID_CLIP);
    lrg_registry_register (registry, "gradient-clip", LRG_TYPE_REEL_GRADIENT_CLIP);
    lrg_registry_register (registry, "image-clip", LRG_TYPE_REEL_IMAGE_CLIP);
    lrg_registry_register (registry, "text-clip", LRG_TYPE_REEL_TEXT_CLIP);
    lrg_registry_register (registry, "shape-clip", LRG_TYPE_REEL_SHAPE_CLIP);
    lrg_registry_register (registry, "caption-clip", LRG_TYPE_REEL_CAPTION_CLIP);
}

/* Parse "#rgb", "#rrggbb" or "#rrggbbaa" into a GrlColor (alpha defaults 255). */
static gboolean
reel_loader_parse_color (const gchar *text,
                         GrlColor    *out)
{
    guint64 v;
    gsize   len;

    if (text == NULL)
        return FALSE;
    if (*text == '#')
        text++;
    len = strlen (text);
    v = g_ascii_strtoull (text, NULL, 16);

    if (len == 6)
    {
        out->r = (guint8) ((v >> 16) & 0xff);
        out->g = (guint8) ((v >> 8) & 0xff);
        out->b = (guint8) (v & 0xff);
        out->a = 255;
        return TRUE;
    }
    if (len == 8)
    {
        out->r = (guint8) ((v >> 24) & 0xff);
        out->g = (guint8) ((v >> 16) & 0xff);
        out->b = (guint8) ((v >> 8) & 0xff);
        out->a = (guint8) (v & 0xff);
        return TRUE;
    }
    return FALSE;
}

/* Build a GValue of the property's type from a YAML scalar string. */
static gboolean
reel_loader_value_from_string (GParamSpec  *pspec,
                               const gchar *str,
                               GValue      *out)
{
    GType pt = G_PARAM_SPEC_VALUE_TYPE (pspec);

    if (pt == G_TYPE_INT)
    {
        g_value_init (out, G_TYPE_INT);
        g_value_set_int (out, (gint) g_ascii_strtoll (str, NULL, 10));
    }
    else if (pt == G_TYPE_UINT)
    {
        g_value_init (out, G_TYPE_UINT);
        g_value_set_uint (out, (guint) g_ascii_strtoull (str, NULL, 10));
    }
    else if (pt == G_TYPE_INT64)
    {
        g_value_init (out, G_TYPE_INT64);
        g_value_set_int64 (out, g_ascii_strtoll (str, NULL, 10));
    }
    else if (pt == G_TYPE_DOUBLE)
    {
        g_value_init (out, G_TYPE_DOUBLE);
        g_value_set_double (out, g_ascii_strtod (str, NULL));
    }
    else if (pt == G_TYPE_FLOAT)
    {
        g_value_init (out, G_TYPE_FLOAT);
        g_value_set_float (out, (gfloat) g_ascii_strtod (str, NULL));
    }
    else if (pt == G_TYPE_BOOLEAN)
    {
        gboolean b = (g_ascii_strcasecmp (str, "true") == 0 ||
                      g_ascii_strcasecmp (str, "yes") == 0 ||
                      g_strcmp0 (str, "1") == 0);
        g_value_init (out, G_TYPE_BOOLEAN);
        g_value_set_boolean (out, b);
    }
    else if (pt == G_TYPE_STRING)
    {
        g_value_init (out, G_TYPE_STRING);
        g_value_set_string (out, str);
    }
    else if (G_TYPE_IS_ENUM (pt))
    {
        GEnumClass *ec = g_type_class_ref (pt);
        GEnumValue *ev = g_enum_get_value_by_nick (ec, str);

        if (ev == NULL)
            ev = g_enum_get_value_by_name (ec, str);
        g_value_init (out, pt);
        g_value_set_enum (out, ev != NULL ? ev->value : 0);
        g_type_class_unref (ec);
    }
    else if (pt == GRL_TYPE_COLOR)
    {
        GrlColor c = { 0, 0, 0, 255 };

        if (!reel_loader_parse_color (str, &c))
            return FALSE;
        g_value_init (out, GRL_TYPE_COLOR);
        g_value_set_boxed (out, &c);
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

static LrgReelClip *
reel_loader_build_clip (YamlMapping *map,
                        LrgRegistry *registry,
                        GError     **error)
{
    YamlNode    *type_node;
    const gchar *type_name;
    GType        gtype;
    GList        *keys;
    GList        *l;
    GArray       *values;
    GPtrArray    *names;
    GObject      *obj;
    GObjectClass *oclass;
    guint         i;

    type_node = yaml_mapping_get_member (map, "type");
    if (type_node == NULL)
    {
        g_set_error_literal (error, LRG_REEL_LOADER_ERROR, 1,
                             "clip is missing a 'type' field");
        return NULL;
    }
    type_name = yaml_node_get_string (type_node);
    gtype = lrg_registry_lookup (registry, type_name);
    if (gtype == G_TYPE_INVALID || !g_type_is_a (gtype, LRG_TYPE_REEL_CLIP))
    {
        g_set_error (error, LRG_REEL_LOADER_ERROR, 2,
                     "unknown reel clip type '%s'", type_name ? type_name : "(null)");
        return NULL;
    }

    /* Collect every member (except 'type') as a construct/normal property so
     * construct-only properties (e.g. a shape's 'kind') are honoured. */
    oclass = g_type_class_ref (gtype);
    names = g_ptr_array_new ();
    values = g_array_new (FALSE, FALSE, sizeof (GValue));
    keys = yaml_mapping_get_members (map);

    for (l = keys; l != NULL; l = l->next)
    {
        const gchar *key = l->data;
        YamlNode    *vnode;
        const gchar *vstr;
        GParamSpec  *pspec;
        GValue       gv = G_VALUE_INIT;

        if (g_strcmp0 (key, "type") == 0)
            continue;

        vnode = yaml_mapping_get_member (map, key);
        if (vnode == NULL || yaml_node_get_node_type (vnode) != YAML_NODE_SCALAR)
            continue;
        vstr = yaml_node_get_string (vnode);

        pspec = g_object_class_find_property (oclass, key);
        if (pspec == NULL)
            continue;
        if (!reel_loader_value_from_string (pspec, vstr, &gv))
            continue;

        g_ptr_array_add (names, (gpointer) g_intern_string (key));
        g_array_append_val (values, gv);
    }
    g_list_free (keys);

    obj = g_object_new_with_properties (gtype, names->len,
                                        (const gchar **) names->pdata,
                                        (const GValue *) values->data);

    for (i = 0; i < values->len; i++)
        g_value_unset (&g_array_index (values, GValue, i));
    g_array_unref (values);
    g_ptr_array_unref (names);
    g_type_class_unref (oclass);

    return LRG_REEL_CLIP (obj);
}

static LrgReel *
reel_loader_build (YamlNode    *root,
                   LrgRegistry *registry,
                   GError     **error)
{
    YamlMapping *map;
    YamlNode    *node;
    LrgReel     *reel;
    const gchar *id = "reel";
    gint         width = 1920;
    gint         height = 1080;
    gdouble      fps = 30.0;
    gint         duration = 1;
    YamlNode    *clips_node;

    if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
    {
        g_set_error_literal (error, LRG_REEL_LOADER_ERROR, 3,
                             "reel document root must be a mapping");
        return NULL;
    }
    map = yaml_node_get_mapping (root);

    if ((node = yaml_mapping_get_member (map, "id")) != NULL)
        id = yaml_node_get_string (node);
    if ((node = yaml_mapping_get_member (map, "width")) != NULL)
        width = (gint) g_ascii_strtoll (yaml_node_get_string (node), NULL, 10);
    if ((node = yaml_mapping_get_member (map, "height")) != NULL)
        height = (gint) g_ascii_strtoll (yaml_node_get_string (node), NULL, 10);
    if ((node = yaml_mapping_get_member (map, "fps")) != NULL)
        fps = g_ascii_strtod (yaml_node_get_string (node), NULL);
    if ((node = yaml_mapping_get_member (map, "duration-in-frames")) != NULL)
        duration = (gint) g_ascii_strtoll (yaml_node_get_string (node), NULL, 10);

    reel = lrg_reel_new (id, width, height, fps, duration);

    clips_node = yaml_mapping_get_member (map, "clips");
    if (clips_node != NULL && yaml_node_get_node_type (clips_node) == YAML_NODE_SEQUENCE)
    {
        YamlSequence *seq = yaml_node_get_sequence (clips_node);
        guint         n = yaml_sequence_get_length (seq);
        guint         i;

        for (i = 0; i < n; i++)
        {
            YamlNode    *elem = yaml_sequence_get_element (seq, i);
            LrgReelClip *clip;

            if (elem == NULL || yaml_node_get_node_type (elem) != YAML_NODE_MAPPING)
                continue;

            clip = reel_loader_build_clip (yaml_node_get_mapping (elem), registry, error);
            if (clip == NULL)
            {
                g_object_unref (reel);
                return NULL;
            }
            lrg_reel_add_clip (reel, clip);
            g_object_unref (clip);
        }
    }

    return reel;
}

static LrgRegistry *
reel_loader_default_registry (LrgRegistry *registry)
{
    if (registry != NULL)
        return g_object_ref (registry);

    registry = lrg_registry_new ();
    lrg_reel_register_types (registry);
    return registry;
}

LrgReel *
lrg_reel_load_yaml (const gchar  *path,
                    LrgRegistry  *registry,
                    GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(LrgRegistry) reg = NULL;

    g_return_val_if_fail (path != NULL, NULL);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, error))
        return NULL;

    reg = reel_loader_default_registry (registry);
    return reel_loader_build (yaml_parser_get_root (parser), reg, error);
}

LrgReel *
lrg_reel_load_yaml_string (const gchar  *yaml,
                           LrgRegistry  *registry,
                           GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(LrgRegistry) reg = NULL;

    g_return_val_if_fail (yaml != NULL, NULL);

    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_data (parser, yaml, -1, error))
        return NULL;

    reg = reel_loader_default_registry (registry);
    return reel_loader_build (yaml_parser_get_root (parser), reg, error);
}
