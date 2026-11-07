/* lrg-prefab.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Prefabs: reusable saved node subtrees, and linked prefab instances.
 *
 * Overview of the linked-instance model (see docs/editor/prefabs.org):
 *
 *  - An instance root carries an #LrgPrefabLink: the source path (and asset
 *    guid), a map instance-node-guid -> source-node-guid covering every node
 *    that came from the source, and a set of override keys.
 *
 *  - An override key is a property path relative to one node of the
 *    instance: a bare path addresses the root, `<guid>:path` any other node.
 *    Paths are hierarchical ("visual" covers "visual/asset").
 *
 *  - Everything is expressed through two primitives: diff_node_state()
 *    lists the paths at which two nodes differ, and copy_path() copies the
 *    value at one path from one node to another. Sync, compute, apply and
 *    revert are walks over the instance/source pair built on those two.
 *
 *  - Non-root instance nodes get guids derived from the instance root guid
 *    and the source node guid, so re-creating a node during a sync (or
 *    re-resolving a nested prefab) gives it the same guid every time.
 */

#include "lrg-prefab.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-node-tilemap.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "lrg-level.h"
#include "lrg-level-serializer.h"
#include "lrg-editor-value-private.h"
#include "../scene/lrg-material3d.h"
#include "../lrg-enums.h"
#include <graylib.h>
#include <gio/gio.h>
#include <string.h>

#define PREFAB_MARKER_PARAM  "prefab:source"
#define PREFAB_MAX_DEPTH     (8)

/* ==========================================================================
 * LrgPrefabLink
 * ========================================================================== */

struct _LrgPrefabLink
{
	GObject     parent_instance;

	gchar      *source_path;
	gchar      *source_guid;
	GHashTable *map;        /* instance guid -> source guid (owned strings) */
	GHashTable *rmap;       /* source guid -> instance guid (owned strings) */
	GHashTable *overrides;  /* set of keys (owned strings) */
};

G_DEFINE_FINAL_TYPE (LrgPrefabLink, lrg_prefab_link, G_TYPE_OBJECT)

enum
{
	LINK_PROP_0,
	LINK_PROP_SOURCE_PATH,
	LINK_PROP_SOURCE_GUID,
	LINK_N_PROPS
};

static GParamSpec *link_properties[LINK_N_PROPS];

static void
lrg_prefab_link_finalize (GObject *object)
{
	LrgPrefabLink *self = LRG_PREFAB_LINK (object);

	g_clear_pointer (&self->source_path, g_free);
	g_clear_pointer (&self->source_guid, g_free);
	g_clear_pointer (&self->map, g_hash_table_unref);
	g_clear_pointer (&self->rmap, g_hash_table_unref);
	g_clear_pointer (&self->overrides, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_prefab_link_parent_class)->finalize (object);
}

static void
lrg_prefab_link_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgPrefabLink *self = LRG_PREFAB_LINK (object);

	switch (prop_id)
	{
	case LINK_PROP_SOURCE_PATH:
		g_value_set_string (value, self->source_path);
		break;
	case LINK_PROP_SOURCE_GUID:
		g_value_set_string (value, self->source_guid);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_prefab_link_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgPrefabLink *self = LRG_PREFAB_LINK (object);

	switch (prop_id)
	{
	case LINK_PROP_SOURCE_PATH:
		lrg_prefab_link_set_source_path (self, g_value_get_string (value));
		break;
	case LINK_PROP_SOURCE_GUID:
		lrg_prefab_link_set_source_guid (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_prefab_link_class_init (LrgPrefabLinkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_prefab_link_finalize;
	object_class->get_property = lrg_prefab_link_get_property;
	object_class->set_property = lrg_prefab_link_set_property;

	/**
	 * LrgPrefabLink:source-path:
	 *
	 * Path of the source `.rprefab` file.
	 *
	 * Since: 0.8.1
	 */
	link_properties[LINK_PROP_SOURCE_PATH] =
		g_param_spec_string ("source-path", "Source Path",
		                     "Path of the source prefab",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPrefabLink:source-guid:
	 *
	 * Asset guid of the source prefab, or %NULL when unknown.
	 *
	 * Since: 0.8.1
	 */
	link_properties[LINK_PROP_SOURCE_GUID] =
		g_param_spec_string ("source-guid", "Source GUID",
		                     "Asset guid of the source prefab",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, LINK_N_PROPS, link_properties);
}

static void
lrg_prefab_link_init (LrgPrefabLink *self)
{
	self->map       = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->rmap      = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->overrides = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

LrgPrefabLink *
lrg_prefab_link_new (const gchar *source_path,
                     const gchar *source_guid)
{
	return g_object_new (LRG_TYPE_PREFAB_LINK,
	                     "source-path", source_path,
	                     "source-guid", source_guid,
	                     NULL);
}

LrgPrefabLink *
lrg_prefab_link_copy (LrgPrefabLink *self)
{
	LrgPrefabLink  *copy;
	GHashTableIter  iter;
	gpointer        key;
	gpointer        value;

	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), NULL);

	copy = lrg_prefab_link_new (self->source_path, self->source_guid);
	g_hash_table_iter_init (&iter, self->map);
	while (g_hash_table_iter_next (&iter, &key, &value))
		lrg_prefab_link_map_set (copy, key, value);
	g_hash_table_iter_init (&iter, self->overrides);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_hash_table_add (copy->overrides, g_strdup (key));

	return copy;
}

/* TRUE when both string->string tables hold the same pairs. */
static gboolean
str_tables_equal (GHashTable *a,
                  GHashTable *b)
{
	GHashTableIter iter;
	gpointer       key;
	gpointer       value;

	if (g_hash_table_size (a) != g_hash_table_size (b))
		return FALSE;

	g_hash_table_iter_init (&iter, a);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		gpointer other;

		if (!g_hash_table_lookup_extended (b, key, NULL, &other))
			return FALSE;
		if (g_strcmp0 (value, other) != 0)
			return FALSE;
	}

	return TRUE;
}

gboolean
lrg_prefab_link_equal (LrgPrefabLink *a,
                       LrgPrefabLink *b)
{
	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;

	/* The override set stores NULL values, so str_tables_equal() compares
	 * keys only for it. */
	return g_strcmp0 (a->source_path, b->source_path) == 0 &&
	       g_strcmp0 (a->source_guid, b->source_guid) == 0 &&
	       str_tables_equal (a->map, b->map) &&
	       str_tables_equal (a->overrides, b->overrides);
}

const gchar *
lrg_prefab_link_get_source_path (LrgPrefabLink *self)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), NULL);

	return self->source_path;
}

void
lrg_prefab_link_set_source_path (LrgPrefabLink *self,
                                 const gchar   *path)
{
	g_return_if_fail (LRG_IS_PREFAB_LINK (self));

	if (g_strcmp0 (self->source_path, path) == 0)
		return;

	g_free (self->source_path);
	self->source_path = g_strdup (path);
	g_object_notify_by_pspec (G_OBJECT (self), link_properties[LINK_PROP_SOURCE_PATH]);
}

const gchar *
lrg_prefab_link_get_source_guid (LrgPrefabLink *self)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), NULL);

	return self->source_guid;
}

void
lrg_prefab_link_set_source_guid (LrgPrefabLink *self,
                                 const gchar   *guid)
{
	g_return_if_fail (LRG_IS_PREFAB_LINK (self));

	if (g_strcmp0 (self->source_guid, guid) == 0)
		return;

	g_free (self->source_guid);
	self->source_guid = g_strdup (guid);
	g_object_notify_by_pspec (G_OBJECT (self), link_properties[LINK_PROP_SOURCE_GUID]);
}

void
lrg_prefab_link_map_set (LrgPrefabLink *self,
                         const gchar   *instance_guid,
                         const gchar   *source_guid)
{
	const gchar *old_src;
	const gchar *old_inst;

	g_return_if_fail (LRG_IS_PREFAB_LINK (self));
	g_return_if_fail (instance_guid != NULL);
	g_return_if_fail (source_guid != NULL);

	/* Keep the map a bijection: drop the old pairing of either side. */
	old_src = g_hash_table_lookup (self->map, instance_guid);
	if (old_src != NULL)
		g_hash_table_remove (self->rmap, old_src);
	old_inst = g_hash_table_lookup (self->rmap, source_guid);
	if (old_inst != NULL)
		g_hash_table_remove (self->map, old_inst);

	g_hash_table_insert (self->map, g_strdup (instance_guid), g_strdup (source_guid));
	g_hash_table_insert (self->rmap, g_strdup (source_guid), g_strdup (instance_guid));
}

gboolean
lrg_prefab_link_map_remove (LrgPrefabLink *self,
                            const gchar   *instance_guid)
{
	const gchar *src;

	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), FALSE);
	g_return_val_if_fail (instance_guid != NULL, FALSE);

	src = g_hash_table_lookup (self->map, instance_guid);
	if (src == NULL)
		return FALSE;

	g_hash_table_remove (self->rmap, src);
	g_hash_table_remove (self->map, instance_guid);
	return TRUE;
}

const gchar *
lrg_prefab_link_map_lookup (LrgPrefabLink *self,
                            const gchar   *instance_guid)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), NULL);

	if (instance_guid == NULL)
		return NULL;
	return g_hash_table_lookup (self->map, instance_guid);
}

const gchar *
lrg_prefab_link_map_lookup_instance (LrgPrefabLink *self,
                                     const gchar   *source_guid)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), NULL);

	if (source_guid == NULL)
		return NULL;
	return g_hash_table_lookup (self->rmap, source_guid);
}

guint
lrg_prefab_link_get_map_size (LrgPrefabLink *self)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), 0);

	return g_hash_table_size (self->map);
}

/* The keys of @table as a sorted, NULL-terminated string vector. */
static GStrv
sorted_keys (GHashTable *table)
{
	GPtrArray      *arr = g_ptr_array_new ();
	GHashTableIter  iter;
	gpointer        key;

	g_hash_table_iter_init (&iter, table);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (arr, g_strdup (key));
	g_ptr_array_sort_values (arr, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (arr, NULL);

	return (GStrv) g_ptr_array_free (arr, FALSE);
}

GStrv
lrg_prefab_link_get_mapped_guids (LrgPrefabLink *self)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), NULL);

	return sorted_keys (self->map);
}

void
lrg_prefab_link_clear_map (LrgPrefabLink *self)
{
	g_return_if_fail (LRG_IS_PREFAB_LINK (self));

	g_hash_table_remove_all (self->map);
	g_hash_table_remove_all (self->rmap);
}

gboolean
lrg_prefab_link_add_override (LrgPrefabLink *self,
                              const gchar   *key)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), FALSE);
	g_return_val_if_fail (key != NULL && *key != '\0', FALSE);

	if (g_hash_table_contains (self->overrides, key))
		return FALSE;

	g_hash_table_add (self->overrides, g_strdup (key));
	return TRUE;
}

gboolean
lrg_prefab_link_remove_override (LrgPrefabLink *self,
                                 const gchar   *key)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	return g_hash_table_remove (self->overrides, key);
}

/*
 * split_key:
 *
 * Splits an override key into its node guid part (NULL for the root) and
 * its path. A key is node-qualified when it holds a ':' with no '/' before
 * it, so root paths such as "visual/params/prefab:source" are not split.
 * Returns the path pointer inside @key; *@guid_out is newly allocated.
 */
static const gchar *
split_key (const gchar  *key,
           gchar       **guid_out)
{
	const gchar *colon = strchr (key, ':');
	const gchar *slash = strchr (key, '/');

	if (colon != NULL && colon != key && (slash == NULL || slash > colon))
	{
		*guid_out = g_strndup (key, (gsize) (colon - key));
		return colon + 1;
	}

	*guid_out = NULL;
	return key;
}

gboolean
lrg_prefab_link_has_override (LrgPrefabLink *self,
                              const gchar   *key)
{
	g_autofree gchar *guid = NULL;
	const gchar      *path;
	const gchar      *p;

	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	if (g_hash_table_size (self->overrides) == 0)
		return FALSE;
	if (g_hash_table_contains (self->overrides, key))
		return TRUE;

	/* Parent paths of the same node cover their children. */
	path = split_key (key, &guid);
	for (p = strchr (path, '/'); p != NULL; p = strchr (p + 1, '/'))
	{
		g_autofree gchar *prefix = g_strndup (path, (gsize) (p - path));
		g_autofree gchar *candidate = (guid != NULL)
		                              ? g_strdup_printf ("%s:%s", guid, prefix)
		                              : g_strdup (prefix);

		if (g_hash_table_contains (self->overrides, candidate))
			return TRUE;
	}

	return FALSE;
}

gboolean
lrg_prefab_link_contains_override (LrgPrefabLink *self,
                                   const gchar   *key)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	return g_hash_table_contains (self->overrides, key);
}

GStrv
lrg_prefab_link_get_overrides (LrgPrefabLink *self)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), NULL);

	return sorted_keys (self->overrides);
}

guint
lrg_prefab_link_get_n_overrides (LrgPrefabLink *self)
{
	g_return_val_if_fail (LRG_IS_PREFAB_LINK (self), 0);

	return g_hash_table_size (self->overrides);
}

void
lrg_prefab_link_clear_overrides (LrgPrefabLink *self)
{
	g_return_if_fail (LRG_IS_PREFAB_LINK (self));

	g_hash_table_remove_all (self->overrides);
}

/* Removes every key equal to @key or below it ("key/..."). */
static void
remove_keys_under (LrgPrefabLink *link,
                   const gchar   *key)
{
	g_autofree gchar *prefix = g_strconcat (key, "/", NULL);
	GHashTableIter    iter;
	gpointer          k;

	g_hash_table_iter_init (&iter, link->overrides);
	while (g_hash_table_iter_next (&iter, &k, NULL))
		if (g_strcmp0 (k, key) == 0 || g_str_has_prefix (k, prefix))
			g_hash_table_iter_remove (&iter);
}

/*
 * purge_guid:
 *
 * Forgets an instance node that left the instance: its map entry, every
 * key qualified with its guid, and any "children/+<guid>" key naming it.
 */
static void
purge_guid (LrgPrefabLink *link,
            const gchar   *guid)
{
	g_autofree gchar *qual = g_strconcat (guid, ":", NULL);
	g_autofree gchar *added = g_strconcat ("children/+", guid, NULL);
	GHashTableIter    iter;
	gpointer          k;

	lrg_prefab_link_map_remove (link, guid);

	g_hash_table_iter_init (&iter, link->overrides);
	while (g_hash_table_iter_next (&iter, &k, NULL))
	{
		g_autofree gchar *kg = NULL;
		const gchar      *path = split_key (k, &kg);

		if (g_str_has_prefix (k, qual) || g_strcmp0 (path, added) == 0)
			g_hash_table_iter_remove (&iter);
	}
}

/* ==========================================================================
 * Guid helpers
 * ========================================================================== */

/*
 * derive_guid:
 *
 * A name-based (UUIDv5-shaped) guid from two strings: the same inputs
 * always give the same guid, so instance nodes re-created from the same
 * source node under the same instance root keep their identity.
 */
static gchar *
derive_guid (const gchar *a,
             const gchar *b)
{
	g_autofree gchar *seed = g_strconcat (a, "/", b, NULL);
	g_autofree gchar *hex = g_compute_checksum_for_string (G_CHECKSUM_SHA1, seed, -1);
	static const gchar digits[] = "0123456789abcdef";
	gint               variant;

	variant = (gint) g_ascii_xdigit_value (hex[16]);
	variant = (variant & 0x3) | 0x8;

	return g_strdup_printf ("%.8s-%.4s-5%.3s-%c%.3s-%.12s",
	                        hex, hex + 8, hex + 13, digits[variant],
	                        hex + 17, hex + 20);
}

static gchar *
canonical_path (const gchar *path)
{
	return g_canonicalize_filename (path, NULL);
}

/* ==========================================================================
 * Subtree cloning with guid remapping
 * ========================================================================== */

/* How clone_tree() picks the guid of each copy. */
typedef enum
{
	GUID_RANDOM,   /* a fresh random guid */
	GUID_KEEP,     /* the source node's guid (exact copy) */
	GUID_DERIVE    /* derive_guid (seed, source guid) */
} GuidMode;

/*
 * clone_tree:
 * @src: subtree to copy
 * @mode: guid policy
 * @seed: seed for %GUID_DERIVE
 * @old_to_new: filled with src guid -> copy guid
 *
 * Deep-copies the editable state of every node (not links; see
 * copy_links()).
 */
static LrgNode *
clone_tree (LrgNode     *src,
            GuidMode     mode,
            const gchar *seed,
            GHashTable  *old_to_new)
{
	LrgNode   *dst;
	GPtrArray *children;
	guint      i;

	dst = lrg_node_new (lrg_node_get_name (src));
	lrg_node_copy_state (dst, src);

	if (mode == GUID_KEEP)
		lrg_node_set_guid (dst, lrg_node_get_guid (src));
	else if (mode == GUID_DERIVE)
	{
		g_autofree gchar *g = derive_guid (seed, lrg_node_get_guid (src));

		lrg_node_set_guid (dst, g);
	}

	if (old_to_new != NULL)
		g_hash_table_insert (old_to_new, g_strdup (lrg_node_get_guid (src)),
		                     g_strdup (lrg_node_get_guid (dst)));

	children = lrg_node_get_children (src);
	for (i = 0; i < children->len; i++)
	{
		g_autoptr(LrgNode) child = clone_tree (g_ptr_array_index (children, i),
		                                       mode, seed, old_to_new);

		lrg_node_add_child (dst, child);
	}

	return dst;
}

/*
 * remap_link:
 *
 * Copies @link rewriting the instance-side guids (map keys and the guid
 * part of override keys) through @old_to_new; guids not in the table are
 * kept. Source-side guids (map values) never change.
 */
static LrgPrefabLink *
remap_link (LrgPrefabLink *link,
            GHashTable    *old_to_new)
{
	LrgPrefabLink  *copy;
	GHashTableIter  iter;
	gpointer        key;
	gpointer        value;

	copy = lrg_prefab_link_new (link->source_path, link->source_guid);

	g_hash_table_iter_init (&iter, link->map);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		const gchar *nk = g_hash_table_lookup (old_to_new, key);

		lrg_prefab_link_map_set (copy, nk != NULL ? nk : key, value);
	}

	g_hash_table_iter_init (&iter, link->overrides);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		g_autofree gchar *guid = NULL;
		const gchar      *path = split_key (key, &guid);
		const gchar      *ng;

		if (guid == NULL)
		{
			g_hash_table_add (copy->overrides, g_strdup (key));
			continue;
		}
		ng = g_hash_table_lookup (old_to_new, guid);
		g_hash_table_add (copy->overrides,
		                  g_strdup_printf ("%s:%s", ng != NULL ? ng : guid, path));
	}

	return copy;
}

/*
 * copy_links:
 *
 * Walks the structurally identical trees @src and @dst in parallel and
 * gives every @dst node a remapped copy of its @src counterpart's link.
 * The link of @src itself is skipped when @skip_root.
 */
static void
copy_links (LrgNode    *src,
            LrgNode    *dst,
            GHashTable *old_to_new,
            gboolean    skip_root)
{
	GPtrArray     *sc;
	GPtrArray     *dc;
	LrgPrefabLink *link;
	guint          i;

	link = lrg_node_get_prefab_link (src);
	if (link != NULL && !skip_root)
	{
		g_autoptr(LrgPrefabLink) copy = remap_link (link, old_to_new);

		lrg_node_set_prefab_link (dst, copy);
	}

	sc = lrg_node_get_children (src);
	dc = lrg_node_get_children (dst);
	for (i = 0; i < sc->len && i < dc->len; i++)
		copy_links (g_ptr_array_index (sc, i), g_ptr_array_index (dc, i),
		            old_to_new, FALSE);
}

static GHashTable *
new_guid_table (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

LrgNode *
lrg_prefab_clone (LrgNode *node)
{
	g_autoptr(GHashTable) table = NULL;
	LrgNode              *dst;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	/* Own state (name, flags, transform, visual, components, scripts) is
	 * deep-copied; every copy gets a fresh random GUID, and links inside
	 * the subtree follow the new guids. */
	table = new_guid_table ();
	dst = clone_tree (node, GUID_RANDOM, NULL, table);
	copy_links (node, dst, table, FALSE);

	return dst;
}

LrgNode *
lrg_prefab_copy (LrgNode  *node,
                 gboolean  keep_root_link)
{
	g_autoptr(GHashTable) table = NULL;
	LrgNode              *dst;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	/* Same guids, so the guid table is the identity; links are copied as
	 * they are (the root's only when asked). */
	table = new_guid_table ();
	dst = clone_tree (node, GUID_KEEP, NULL, table);
	copy_links (node, dst, table, !keep_root_link);
	return dst;
}

LrgNode *
lrg_prefab_instantiate (LrgNode *prefab)
{
	LrgNode *inst;

	g_return_val_if_fail (LRG_IS_NODE (prefab), NULL);

	/* Unlinked copy: the root does not inherit any link of the prefab. */
	inst = lrg_prefab_clone (prefab);
	lrg_node_set_prefab_link (inst, NULL);
	return inst;
}

static gboolean
regen_guid (LrgNode  *node,
            gpointer  user_data)
{
	GHashTable       *table = user_data;
	g_autofree gchar *guid = g_uuid_string_random ();

	g_hash_table_insert (table, g_strdup (lrg_node_get_guid (node)), g_strdup (guid));
	lrg_node_set_guid (node, guid);
	return TRUE;
}

static gboolean
regen_link (LrgNode  *node,
            gpointer  user_data)
{
	LrgPrefabLink *link = lrg_node_get_prefab_link (node);

	if (link != NULL)
	{
		g_autoptr(LrgPrefabLink) copy = remap_link (link, user_data);

		lrg_node_set_prefab_link (node, copy);
	}
	return TRUE;
}

void
lrg_prefab_regenerate_guids (LrgNode *root)
{
	g_autoptr(GHashTable) table = NULL;

	g_return_if_fail (LRG_IS_NODE (root));

	table = new_guid_table ();
	lrg_node_foreach (root, regen_guid, table);
	lrg_node_foreach (root, regen_link, table);
}

/* ==========================================================================
 * Prefab source marker (0.8.0 compatibility)
 * ========================================================================== */

const gchar *
lrg_prefab_get_marker_source (LrgNode *node)
{
	LrgNodeVisual *visual;
	const GValue  *v;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	visual = lrg_node_get_visual (node);
	if (visual == NULL)
		return NULL;

	v = lrg_node_visual_get_param (visual, PREFAB_MARKER_PARAM);
	if (v != NULL && G_VALUE_HOLDS_STRING (v))
		return g_value_get_string (v);

	if (lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PREFAB_INSTANCE)
		return lrg_node_visual_get_asset (visual);

	return NULL;
}

/* A visual that only marks "this is a prefab instance" (0.8.0 style). */
static gboolean
is_marker_visual (LrgNodeVisual *visual)
{
	return visual != NULL &&
	       lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PREFAB_INSTANCE;
}

/*
 * ensure_marker:
 *
 * Records @path on an instance root the way 0.8.0 did: a prefab-instance
 * visual when the root has no visual, else the `prefab:source` param.
 * Returns TRUE when something changed.
 */
static gboolean
ensure_marker (LrgNode     *root,
               const gchar *path)
{
	LrgNodeVisual *visual = lrg_node_get_visual (root);
	const GValue  *v;
	GValue         src = G_VALUE_INIT;

	if (path == NULL)
		return FALSE;

	if (visual == NULL)
	{
		g_autoptr(LrgNodeVisual) marker = lrg_node_visual_new (LRG_NODE_VISUAL_PREFAB_INSTANCE);

		lrg_node_visual_set_asset (marker, path);
		lrg_node_set_visual (root, marker);
		return TRUE;
	}

	if (is_marker_visual (visual))
	{
		if (g_strcmp0 (lrg_node_visual_get_asset (visual), path) == 0)
			return FALSE;
		lrg_node_visual_set_asset (visual, path);
		return TRUE;
	}

	v = lrg_node_visual_get_param (visual, PREFAB_MARKER_PARAM);
	if (v != NULL && G_VALUE_HOLDS_STRING (v) && g_strcmp0 (g_value_get_string (v), path) == 0)
		return FALSE;

	g_value_init (&src, G_TYPE_STRING);
	g_value_set_string (&src, path);
	lrg_node_visual_set_param (visual, PREFAB_MARKER_PARAM, &src);
	g_value_unset (&src);
	return TRUE;
}

/* ==========================================================================
 * Override keys and ownership
 * ========================================================================== */

LrgNode *
lrg_prefab_find_owner (LrgNode *node)
{
	LrgNode     *best = NULL;
	LrgNode     *n;
	const gchar *guid;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	/* The outermost linked ancestor that maps @node (or is @node) wins:
	 * in a level, edits of a nested prefab's nodes are overrides of the
	 * level instance, not of the nested one. */
	guid = lrg_node_get_guid (node);
	for (n = node; n != NULL; n = lrg_node_get_parent (n))
	{
		LrgPrefabLink *link = lrg_node_get_prefab_link (n);

		if (link == NULL)
			continue;
		if (n == node || lrg_prefab_link_map_lookup (link, guid) != NULL)
			best = n;
	}

	return best;
}

gchar *
lrg_prefab_make_override_key (LrgNode     *owner,
                              LrgNode     *node,
                              const gchar *path)
{
	g_return_val_if_fail (LRG_IS_NODE (owner), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if (owner == node)
		return g_strdup (path);

	return g_strdup_printf ("%s:%s", lrg_node_get_guid (node), path);
}

gboolean
lrg_prefab_is_overridden (LrgNode     *node,
                          const gchar *path)
{
	LrgNode          *owner;
	g_autofree gchar *key = NULL;

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	owner = lrg_prefab_find_owner (node);
	if (owner == NULL)
		return FALSE;

	key = lrg_prefab_make_override_key (owner, node, path);
	return lrg_prefab_link_has_override (lrg_node_get_prefab_link (owner), key);
}

gboolean
lrg_prefab_mark_override (LrgNode     *node,
                          const gchar *path)
{
	LrgNode          *owner;
	LrgPrefabLink    *link;
	g_autofree gchar *key = NULL;

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	owner = lrg_prefab_find_owner (node);
	if (owner == NULL)
		return FALSE;

	link = lrg_node_get_prefab_link (owner);
	key = lrg_prefab_make_override_key (owner, node, path);
	if (lrg_prefab_link_has_override (link, key))
		return FALSE;

	return lrg_prefab_link_add_override (link, key);
}

/* ==========================================================================
 * Keyed components / scripts
 *
 * Components are addressed by type name and scripts by script path; the
 * n-th (n >= 2) duplicate gets a "#n" suffix.
 * ========================================================================== */

typedef struct
{
	gchar    *key;
	gpointer  item;   /* borrowed LrgComponentDesc* / LrgScriptBinding* */
	guint     index;  /* position in the node's array */
} Keyed;

static void
keyed_free (gpointer data)
{
	Keyed *k = data;

	g_free (k->key);
	g_free (k);
}

static GPtrArray *
keyed_list (GPtrArray *items,
            gboolean   scripts)
{
	GPtrArray             *out = g_ptr_array_new_with_free_func (keyed_free);
	g_autoptr(GHashTable)  seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	guint                  i;

	for (i = 0; i < items->len; i++)
	{
		gpointer     item = g_ptr_array_index (items, i);
		const gchar *base;
		guint        n;
		Keyed       *k;

		base = scripts ? lrg_script_binding_get_script (item)
		               : lrg_component_desc_get_type_name (item);
		if (base == NULL)
			base = "?";

		n = GPOINTER_TO_UINT (g_hash_table_lookup (seen, base)) + 1;
		g_hash_table_insert (seen, g_strdup (base), GUINT_TO_POINTER (n));

		k = g_new0 (Keyed, 1);
		k->key = (n == 1) ? g_strdup (base) : g_strdup_printf ("%s#%u", base, n);
		k->item = item;
		k->index = i;
		g_ptr_array_add (out, k);
	}

	return out;
}

static Keyed *
keyed_find (GPtrArray   *list,
            const gchar *key)
{
	guint i;

	for (i = 0; i < list->len; i++)
	{
		Keyed *k = g_ptr_array_index (list, i);

		if (g_strcmp0 (k->key, key) == 0)
			return k;
	}

	return NULL;
}

/* ==========================================================================
 * Diff
 * ========================================================================== */

static gboolean
vec3_differs (const GrlVector3 *a,
              const GrlVector3 *b)
{
	return a->x != b->x || a->y != b->y || a->z != b->z;
}

static void
add_path (GPtrArray   *out,
          const gchar *fmt,
          ...) G_GNUC_PRINTF (2, 3);

static void
add_path (GPtrArray   *out,
          const gchar *fmt,
          ...)
{
	va_list ap;

	va_start (ap, fmt);
	g_ptr_array_add (out, g_strdup_vprintf (fmt, ap));
	va_end (ap);
}

static void
diff_material (LrgMaterial3D *a,
               LrgMaterial3D *b,
               GPtrArray     *out)
{
	gfloat ca[4], cb[4];

	if (a == NULL && b == NULL)
		return;
	if (a == NULL || b == NULL)
	{
		add_path (out, "visual/material");
		return;
	}

	lrg_material3d_get_color (a, &ca[0], &ca[1], &ca[2], &ca[3]);
	lrg_material3d_get_color (b, &cb[0], &cb[1], &cb[2], &cb[3]);
	if (memcmp (ca, cb, sizeof ca) != 0)
		add_path (out, "visual/material/color");
	if (lrg_material3d_get_roughness (a) != lrg_material3d_get_roughness (b))
		add_path (out, "visual/material/roughness");
	if (lrg_material3d_get_metallic (a) != lrg_material3d_get_metallic (b))
		add_path (out, "visual/material/metallic");
	lrg_material3d_get_emission_color (a, &ca[0], &ca[1], &ca[2], &ca[3]);
	lrg_material3d_get_emission_color (b, &cb[0], &cb[1], &cb[2], &cb[3]);
	if (memcmp (ca, cb, sizeof ca) != 0)
		add_path (out, "visual/material/emission_color");
	if (lrg_material3d_get_emission_strength (a) != lrg_material3d_get_emission_strength (b))
		add_path (out, "visual/material/emission_strength");
}

/* Adds every key of @from whose value is missing/different in @to. */
static void
diff_param_names (LrgNodeVisual *from,
                  LrgNodeVisual *to,
                  GHashTable    *seen,
                  GPtrArray     *out)
{
	GList *names = lrg_node_visual_get_param_names (from);
	GList *l;

	for (l = names; l != NULL; l = l->next)
	{
		const gchar *name = l->data;

		if (g_strcmp0 (name, PREFAB_MARKER_PARAM) == 0 ||
		    g_hash_table_contains (seen, name))
			continue;
		g_hash_table_add (seen, g_strdup (name));
		if (!lrg_editor_value_equal (lrg_node_visual_get_param (from, name),
		                             lrg_node_visual_get_param (to, name)))
			add_path (out, "visual/params/%s", name);
	}
	g_list_free (names);
}

static void
diff_visual (LrgNodeVisual *a,
             LrgNodeVisual *b,
             GPtrArray     *out)
{
	g_autoptr(GHashTable) seen = NULL;

	if (a == NULL && b == NULL)
		return;
	if (a == NULL || b == NULL)
	{
		add_path (out, "visual");
		return;
	}

	if (lrg_node_visual_get_kind (a) != lrg_node_visual_get_kind (b))
		add_path (out, "visual/kind");
	if (lrg_node_visual_get_primitive (a) != lrg_node_visual_get_primitive (b))
		add_path (out, "visual/primitive");
	if (g_strcmp0 (lrg_node_visual_get_asset (a), lrg_node_visual_get_asset (b)) != 0)
		add_path (out, "visual/asset");

	diff_material (lrg_node_visual_get_material (a), lrg_node_visual_get_material (b), out);

	seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	diff_param_names (a, b, seen, out);
	diff_param_names (b, a, seen, out);

	if (!lrg_node_tilemap_equal (lrg_node_visual_get_tilemap (a), lrg_node_visual_get_tilemap (b)))
		add_path (out, "visual/tilemap");
}

/* Props of one component pair that differ (union of both key sets). */
static void
diff_component_props (const gchar      *key,
                      LrgComponentDesc *a,
                      LrgComponentDesc *b,
                      GPtrArray        *out)
{
	GList *keys = lrg_component_desc_get_sorted_keys (a);
	GList *more = lrg_component_desc_get_sorted_keys (b);
	GList *l;

	for (l = keys; l != NULL; l = l->next)
		if (!lrg_editor_value_equal (lrg_component_desc_get_value (a, l->data),
		                             lrg_component_desc_get_value (b, l->data)))
			add_path (out, "components/%s/%s", key, (const gchar *) l->data);
	for (l = more; l != NULL; l = l->next)
		if (!lrg_component_desc_has (a, l->data))
			add_path (out, "components/%s/%s", key, (const gchar *) l->data);

	g_list_free (keys);
	g_list_free (more);
}

static void
diff_components (LrgNode   *a,
                 LrgNode   *b,
                 GPtrArray *out)
{
	g_autoptr(GPtrArray) ka = keyed_list (lrg_node_get_components (a), FALSE);
	g_autoptr(GPtrArray) kb = keyed_list (lrg_node_get_components (b), FALSE);
	guint                i;

	for (i = 0; i < ka->len; i++)
	{
		Keyed *k = g_ptr_array_index (ka, i);
		Keyed *o = keyed_find (kb, k->key);

		if (o == NULL)
			add_path (out, "components/+%s", k->key);
		else
			diff_component_props (k->key, k->item, o->item, out);
	}
	for (i = 0; i < kb->len; i++)
	{
		Keyed *k = g_ptr_array_index (kb, i);

		if (keyed_find (ka, k->key) == NULL)
			add_path (out, "components/-%s", k->key);
	}
}

static void
diff_scripts (LrgNode   *a,
              LrgNode   *b,
              GPtrArray *out)
{
	g_autoptr(GPtrArray) ka = keyed_list (lrg_node_get_scripts (a), TRUE);
	g_autoptr(GPtrArray) kb = keyed_list (lrg_node_get_scripts (b), TRUE);
	guint                i;

	for (i = 0; i < ka->len; i++)
	{
		Keyed *k = g_ptr_array_index (ka, i);
		Keyed *o = keyed_find (kb, k->key);

		if (o == NULL)
		{
			add_path (out, "scripts/+%s", k->key);
			continue;
		}
		if (lrg_script_binding_get_language (k->item) != lrg_script_binding_get_language (o->item))
			add_path (out, "scripts/%s/language", k->key);
		if (!lrg_script_binding_get_enabled (k->item) != !lrg_script_binding_get_enabled (o->item))
			add_path (out, "scripts/%s/enabled", k->key);
	}
	for (i = 0; i < kb->len; i++)
	{
		Keyed *k = g_ptr_array_index (kb, i);

		if (keyed_find (ka, k->key) == NULL)
			add_path (out, "scripts/-%s", k->key);
	}
}

/* The visual of @node as it counts for diffing: an instance root's pure
 * marker visual counts as "no visual". */
static LrgNodeVisual *
effective_visual (LrgNode  *node,
                  gboolean  is_root)
{
	LrgNodeVisual *v = lrg_node_get_visual (node);

	if (is_root && is_marker_visual (v))
		return NULL;
	return v;
}

/* Appends to @out (owned strings) the paths at which @inst and @src differ. */
static void
diff_state (LrgNode   *inst,
            LrgNode   *src,
            gboolean   is_root,
            GPtrArray *out)
{
	if (!is_root && g_strcmp0 (lrg_node_get_name (inst), lrg_node_get_name (src)) != 0)
		add_path (out, "name");
	if (!lrg_node_get_visible (inst) != !lrg_node_get_visible (src))
		add_path (out, "visible");
	if (!lrg_node_get_is_2d (inst) != !lrg_node_get_is_2d (src))
		add_path (out, "is_2d");

	if (!is_root && vec3_differs (lrg_node_get_location (inst), lrg_node_get_location (src)))
		add_path (out, "transform/location");
	if (!is_root && vec3_differs (lrg_node_get_rotation (inst), lrg_node_get_rotation (src)))
		add_path (out, "transform/rotation");
	if (vec3_differs (lrg_node_get_scale (inst), lrg_node_get_scale (src)))
		add_path (out, "transform/scale");

	diff_visual (effective_visual (inst, is_root), effective_visual (src, is_root), out);
	diff_components (inst, src, out);
	diff_scripts (inst, src, out);
}

GStrv
lrg_prefab_diff_node_state (LrgNode  *instance,
                            LrgNode  *source,
                            gboolean  is_instance_root)
{
	GPtrArray *out;

	g_return_val_if_fail (LRG_IS_NODE (instance), NULL);
	g_return_val_if_fail (LRG_IS_NODE (source), NULL);

	out = g_ptr_array_new ();
	diff_state (instance, source, is_instance_root, out);
	g_ptr_array_add (out, NULL);
	return (GStrv) g_ptr_array_free (out, FALSE);
}

/* ==========================================================================
 * copy_path: copy the value at one property path from @src to @dst
 * ========================================================================== */

typedef struct
{
	gboolean     to_source;    /* @dst is the source (apply); else the instance */
	gboolean     root;         /* the pair is (instance root, source root) */
	const gchar *marker_path;  /* syncing an instance root: its source path */
} CopyCtx;

static LrgMaterial3D *
material_copy_or_null (LrgMaterial3D *m)
{
	return (m != NULL) ? lrg_material3d_copy (m) : NULL;
}

static void
copy_material_field (LrgMaterial3D *dst,
                     LrgMaterial3D *src,
                     const gchar   *field)
{
	gfloat c[4];

	if (g_strcmp0 (field, "color") == 0)
	{
		lrg_material3d_get_color (src, &c[0], &c[1], &c[2], &c[3]);
		lrg_material3d_set_color (dst, c[0], c[1], c[2], c[3]);
	}
	else if (g_strcmp0 (field, "roughness") == 0)
		lrg_material3d_set_roughness (dst, lrg_material3d_get_roughness (src));
	else if (g_strcmp0 (field, "metallic") == 0)
		lrg_material3d_set_metallic (dst, lrg_material3d_get_metallic (src));
	else if (g_strcmp0 (field, "emission_color") == 0)
	{
		lrg_material3d_get_emission_color (src, &c[0], &c[1], &c[2], &c[3]);
		lrg_material3d_set_emission_color (dst, c[0], c[1], c[2], c[3]);
	}
	else if (g_strcmp0 (field, "emission_strength") == 0)
		lrg_material3d_set_emission_strength (dst, lrg_material3d_get_emission_strength (src));
}

/* Copies (or clears) the whole visual, honouring the root marker. */
static void
copy_whole_visual (LrgNode       *dst,
                   LrgNodeVisual *src_visual)
{
	g_autoptr(LrgNodeVisual) copy = NULL;

	if (src_visual != NULL)
		copy = lrg_node_visual_copy (src_visual);
	lrg_node_set_visual (dst, copy);
}

/* After a visual edit, restore the root's marker (sync) or strip it from
 * the source (apply). */
static void
fix_marker (LrgNode       *dst,
            const CopyCtx *ctx)
{
	LrgNodeVisual *v;

	if (!ctx->root)
		return;

	if (!ctx->to_source)
	{
		ensure_marker (dst, ctx->marker_path);
		return;
	}

	v = lrg_node_get_visual (dst);
	if (v == NULL)
		return;
	if (is_marker_visual (v))
		lrg_node_set_visual (dst, NULL);
	else
		lrg_node_visual_remove_param (v, PREFAB_MARKER_PARAM);
}

static void
copy_visual_path (LrgNode       *dst,
                  LrgNode       *src,
                  const gchar   *sub,
                  const CopyCtx *ctx)
{
	LrgNodeVisual *sv;
	LrgNodeVisual *dv;

	/* For the source side of the comparison the root marker is "no
	 * visual"; for the destination we edit whatever is there. */
	sv = effective_visual (src, ctx->root);
	dv = effective_visual (dst, ctx->root);

	if (sub == NULL || sv == NULL || dv == NULL)
	{
		/* Whole visual (or one side has none): copy everything. */
		copy_whole_visual (dst, sv);
		fix_marker (dst, ctx);
		return;
	}

	if (g_strcmp0 (sub, "kind") == 0)
		lrg_node_visual_set_kind (dv, lrg_node_visual_get_kind (sv));
	else if (g_strcmp0 (sub, "primitive") == 0)
		lrg_node_visual_set_primitive (dv, lrg_node_visual_get_primitive (sv));
	else if (g_strcmp0 (sub, "asset") == 0)
		lrg_node_visual_set_asset (dv, lrg_node_visual_get_asset (sv));
	else if (g_strcmp0 (sub, "material") == 0)
	{
		g_autoptr(LrgMaterial3D) m = material_copy_or_null (lrg_node_visual_get_material (sv));

		lrg_node_visual_set_material (dv, m);
	}
	else if (g_str_has_prefix (sub, "material/"))
	{
		LrgMaterial3D *sm = lrg_node_visual_get_material (sv);
		LrgMaterial3D *dm = lrg_node_visual_get_material (dv);

		if (sm == NULL || dm == NULL)
		{
			g_autoptr(LrgMaterial3D) m = material_copy_or_null (sm);

			lrg_node_visual_set_material (dv, m);
		}
		else
			copy_material_field (dm, sm, sub + strlen ("material/"));
	}
	else if (g_str_has_prefix (sub, "params/"))
	{
		const gchar  *name = sub + strlen ("params/");
		const GValue *v = lrg_node_visual_get_param (sv, name);

		if (v != NULL)
			lrg_node_visual_set_param (dv, name, v);
		else
			lrg_node_visual_remove_param (dv, name);
	}
	else if (g_strcmp0 (sub, "tilemap") == 0)
	{
		LrgNodeTilemap            *tm = lrg_node_visual_get_tilemap (sv);
		g_autoptr(LrgNodeTilemap)  copy = (tm != NULL) ? lrg_node_tilemap_copy (tm) : NULL;

		lrg_node_visual_set_tilemap (dv, copy);
	}
	else
	{
		/* Unknown sub-path: fall back to the whole visual. */
		copy_whole_visual (dst, sv);
	}
	fix_marker (dst, ctx);
}

/* Copies (or removes) the whole keyed item @key from @src to @dst. */
static void
copy_keyed_item (LrgNode     *dst,
                 LrgNode     *src,
                 const gchar *key,
                 gboolean     scripts)
{
	g_autoptr(GPtrArray) kd = keyed_list (scripts ? lrg_node_get_scripts (dst)
	                                              : lrg_node_get_components (dst), scripts);
	g_autoptr(GPtrArray) ks = keyed_list (scripts ? lrg_node_get_scripts (src)
	                                              : lrg_node_get_components (src), scripts);
	Keyed               *d = keyed_find (kd, key);
	Keyed               *s = keyed_find (ks, key);
	gint                 index = -1;

	if (d != NULL)
	{
		index = (gint) d->index;
		if (scripts)
			lrg_node_remove_script_at (dst, d->index);
		else
			lrg_node_remove_component_at (dst, d->index);
	}

	if (s == NULL)
		return;

	if (scripts)
	{
		g_autoptr(LrgScriptBinding) copy = lrg_script_binding_copy (s->item);

		lrg_node_insert_script (dst, copy, index);
	}
	else
	{
		g_autoptr(LrgComponentDesc) copy = lrg_component_desc_copy (s->item);

		lrg_node_insert_component (dst, copy, index);
	}
}

static void
copy_component_path (LrgNode     *dst,
                     LrgNode     *src,
                     const gchar *sub)
{
	g_autofree gchar    *key = NULL;
	const gchar         *slash;
	g_autoptr(GPtrArray) kd = NULL;
	g_autoptr(GPtrArray) ks = NULL;
	Keyed               *d;
	Keyed               *s;
	const GValue        *v;

	if (sub[0] == '+' || sub[0] == '-')
	{
		copy_keyed_item (dst, src, sub + 1, FALSE);
		return;
	}

	slash = strchr (sub, '/');
	if (slash == NULL)
	{
		copy_keyed_item (dst, src, sub, FALSE);
		return;
	}

	key = g_strndup (sub, (gsize) (slash - sub));
	kd = keyed_list (lrg_node_get_components (dst), FALSE);
	ks = keyed_list (lrg_node_get_components (src), FALSE);
	d = keyed_find (kd, key);
	s = keyed_find (ks, key);
	if (d == NULL || s == NULL)
	{
		copy_keyed_item (dst, src, key, FALSE);
		return;
	}

	v = lrg_component_desc_get_value (s->item, slash + 1);
	if (v != NULL)
		lrg_component_desc_set_value (d->item, slash + 1, v);
	else
		lrg_component_desc_remove (d->item, slash + 1);
}

static void
copy_script_path (LrgNode     *dst,
                  LrgNode     *src,
                  const gchar *sub)
{
	g_autofree gchar    *key = NULL;
	const gchar         *slash;
	const gchar         *prop;
	g_autoptr(GPtrArray) kd = NULL;
	g_autoptr(GPtrArray) ks = NULL;
	Keyed               *d;
	Keyed               *s;

	if (sub[0] == '+' || sub[0] == '-')
	{
		copy_keyed_item (dst, src, sub + 1, TRUE);
		return;
	}

	/* Script paths may contain '/': only a trailing "language" or
	 * "enabled" segment names a property. */
	slash = strrchr (sub, '/');
	prop = (slash != NULL) ? slash + 1 : NULL;
	if (prop == NULL || (g_strcmp0 (prop, "language") != 0 && g_strcmp0 (prop, "enabled") != 0))
	{
		copy_keyed_item (dst, src, sub, TRUE);
		return;
	}

	key = g_strndup (sub, (gsize) (slash - sub));
	kd = keyed_list (lrg_node_get_scripts (dst), TRUE);
	ks = keyed_list (lrg_node_get_scripts (src), TRUE);
	d = keyed_find (kd, key);
	s = keyed_find (ks, key);
	if (d == NULL || s == NULL)
	{
		copy_keyed_item (dst, src, key, TRUE);
		return;
	}

	if (g_strcmp0 (prop, "language") == 0)
		lrg_script_binding_set_language (d->item, lrg_script_binding_get_language (s->item));
	else
		lrg_script_binding_set_enabled (d->item, lrg_script_binding_get_enabled (s->item));
}

/*
 * copy_path:
 *
 * Copies the value at @path (a node-state path, never "children/...") from
 * @src to @dst. Returns FALSE for an unknown path.
 */
static gboolean
copy_path (LrgNode       *dst,
           LrgNode       *src,
           const gchar   *path,
           const CopyCtx *ctx)
{
	GrlVector3 *v;

	if (g_strcmp0 (path, "name") == 0)
		lrg_node_set_name (dst, lrg_node_get_name (src));
	else if (g_strcmp0 (path, "visible") == 0)
		lrg_node_set_visible (dst, lrg_node_get_visible (src));
	else if (g_strcmp0 (path, "is_2d") == 0)
		lrg_node_set_is_2d (dst, lrg_node_get_is_2d (src));
	else if (g_strcmp0 (path, "transform") == 0)
	{
		v = lrg_node_get_location (src);
		lrg_node_set_location_xyz (dst, v->x, v->y, v->z);
		v = lrg_node_get_rotation (src);
		lrg_node_set_rotation_xyz (dst, v->x, v->y, v->z);
		v = lrg_node_get_scale (src);
		lrg_node_set_scale_xyz (dst, v->x, v->y, v->z);
	}
	else if (g_strcmp0 (path, "transform/location") == 0)
	{
		v = lrg_node_get_location (src);
		lrg_node_set_location_xyz (dst, v->x, v->y, v->z);
	}
	else if (g_strcmp0 (path, "transform/rotation") == 0)
	{
		v = lrg_node_get_rotation (src);
		lrg_node_set_rotation_xyz (dst, v->x, v->y, v->z);
	}
	else if (g_strcmp0 (path, "transform/scale") == 0)
	{
		v = lrg_node_get_scale (src);
		lrg_node_set_scale_xyz (dst, v->x, v->y, v->z);
	}
	else if (g_strcmp0 (path, "visual") == 0)
		copy_visual_path (dst, src, NULL, ctx);
	else if (g_str_has_prefix (path, "visual/"))
		copy_visual_path (dst, src, path + strlen ("visual/"), ctx);
	else if (g_str_has_prefix (path, "components/"))
		copy_component_path (dst, src, path + strlen ("components/"));
	else if (g_str_has_prefix (path, "scripts/"))
		copy_script_path (dst, src, path + strlen ("scripts/"));
	else
		return FALSE;

	return TRUE;
}

/* ==========================================================================
 * Instance walks
 * ========================================================================== */

typedef struct
{
	LrgNode       *owner;   /* the instance root */
	LrgPrefabLink *link;    /* its link */
	LrgNode       *prefab;  /* the source root */
	gboolean       changed;
} Walk;

static void
walk_init (Walk    *w,
           LrgNode *instance,
           LrgNode *prefab)
{
	w->owner = instance;
	w->link = lrg_node_get_prefab_link (instance);
	w->prefab = prefab;
	w->changed = FALSE;
}

static gchar *
walk_key (Walk        *w,
          LrgNode     *node,
          const gchar *path)
{
	return lrg_prefab_make_override_key (w->owner, node, path);
}

static gboolean
walk_overridden (Walk        *w,
                 LrgNode     *node,
                 const gchar *path)
{
	g_autofree gchar *key = walk_key (w, node, path);

	return lrg_prefab_link_has_override (w->link, key);
}

/* The source guid @node mirrors, or NULL (added in the instance). */
static const gchar *
walk_source_guid (Walk    *w,
                  LrgNode *node)
{
	return lrg_prefab_link_map_lookup (w->link, lrg_node_get_guid (node));
}

/* The direct child of @inst that mirrors source node @sc, if any. */
static LrgNode *
matched_child (Walk    *w,
               LrgNode *inst,
               LrgNode *sc)
{
	const gchar *ig = lrg_prefab_link_map_lookup_instance (w->link, lrg_node_get_guid (sc));
	GPtrArray   *children;
	guint        i;

	if (ig == NULL)
		return NULL;

	children = lrg_node_get_children (inst);
	for (i = 0; i < children->len; i++)
	{
		LrgNode *c = g_ptr_array_index (children, i);

		if (g_strcmp0 (lrg_node_get_guid (c), ig) == 0)
			return c;
	}

	return NULL;
}

/* TRUE when @sg is the guid of a direct child of @src. */
static gboolean
is_child_guid (LrgNode     *src,
               const gchar *sg)
{
	GPtrArray *children = lrg_node_get_children (src);
	guint      i;

	for (i = 0; i < children->len; i++)
		if (g_strcmp0 (lrg_node_get_guid (g_ptr_array_index (children, i)), sg) == 0)
			return TRUE;

	return FALSE;
}

static gboolean
purge_node (LrgNode  *node,
            gpointer  user_data)
{
	purge_guid (user_data, lrg_node_get_guid (node));
	return TRUE;
}

/* TRUE when any key of the link addresses a node in @node's subtree. */
static gboolean
subtree_has_keys (Walk    *w,
                  LrgNode *node)
{
	GHashTableIter iter;
	gpointer       k;

	g_hash_table_iter_init (&iter, w->link->overrides);
	while (g_hash_table_iter_next (&iter, &k, NULL))
	{
		g_autofree gchar *guid = NULL;

		split_key (k, &guid);
		if (guid != NULL)
		{
			LrgNode *n = lrg_node_find_by_guid (node, guid);

			if (n != NULL)
				return TRUE;
		}
	}

	return FALSE;
}

/*
 * create_from_source:
 *
 * Clones source subtree @sc as new instance nodes (derived guids), maps
 * every clone to its source node and copies nested links.
 */
static LrgNode *
create_from_source (Walk    *w,
                    LrgNode *sc)
{
	g_autoptr(GHashTable) table = new_guid_table ();
	GHashTableIter        iter;
	gpointer              key;
	gpointer              value;
	LrgNode              *copy;

	copy = clone_tree (sc, GUID_DERIVE, lrg_node_get_guid (w->owner), table);
	copy_links (sc, copy, table, FALSE);

	g_hash_table_iter_init (&iter, table);
	while (g_hash_table_iter_next (&iter, &key, &value))
		lrg_prefab_link_map_set (w->link, value, key);

	return copy;
}

/*
 * translate_link:
 *
 * Rewrites a nested link found in the source (keyed by source-side guids)
 * for the instance: map keys and qualified override keys go through the
 * owner's reverse map. Entries for nodes missing from the instance are
 * dropped.
 */
static LrgPrefabLink *
translate_link (Walk          *w,
                LrgPrefabLink *src_link)
{
	LrgPrefabLink  *out;
	GHashTableIter  iter;
	gpointer        key;
	gpointer        value;

	out = lrg_prefab_link_new (src_link->source_path, src_link->source_guid);

	g_hash_table_iter_init (&iter, src_link->map);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		const gchar *ig = lrg_prefab_link_map_lookup_instance (w->link, key);

		if (ig != NULL)
			lrg_prefab_link_map_set (out, ig, value);
	}

	g_hash_table_iter_init (&iter, src_link->overrides);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		g_autofree gchar *guid = NULL;
		const gchar      *path = split_key (key, &guid);
		const gchar      *ig;

		if (guid == NULL)
		{
			g_hash_table_add (out->overrides, g_strdup (key));
			continue;
		}
		ig = lrg_prefab_link_map_lookup_instance (w->link, guid);
		if (ig != NULL)
			g_hash_table_add (out->overrides, g_strdup_printf ("%s:%s", ig, path));
	}

	return out;
}

/* Makes the nested link of @inst mirror that of its source node @src. */
static void
sync_nested_link (Walk    *w,
                  LrgNode *inst,
                  LrgNode *src)
{
	LrgPrefabLink *have = lrg_node_get_prefab_link (inst);
	LrgPrefabLink *want_src = lrg_node_get_prefab_link (src);

	if (want_src == NULL)
	{
		if (have != NULL)
		{
			lrg_node_set_prefab_link (inst, NULL);
			w->changed = TRUE;
		}
		return;
	}

	{
		g_autoptr(LrgPrefabLink) want = translate_link (w, want_src);

		if (!lrg_prefab_link_equal (have, want))
		{
			lrg_node_set_prefab_link (inst, want);
			w->changed = TRUE;
		}
	}
}

static void sync_node (Walk *w, LrgNode *inst, LrgNode *src);

static gpointer
ref_copy (gconstpointer src,
          gpointer      data)
{
	return g_object_ref ((gpointer) src);
}

/* Detaches @node from its parent, keeping it alive for the caller. */
static LrgNode *
detach (LrgNode *node)
{
	LrgNode *parent = lrg_node_get_parent (node);

	g_object_ref (node);
	if (parent != NULL)
		lrg_node_remove_child (parent, node);
	return node;
}

static void
sync_children (Walk    *w,
               LrgNode *inst,
               LrgNode *src)
{
	g_autoptr(GPtrArray) snapshot = NULL;
	GPtrArray           *src_children = lrg_node_get_children (src);
	gint                 last = -1;
	guint                i;

	/* Pass 1: source order. Matched children sync recursively; missing
	 * ones are moved here (moved in the source) or created, unless the
	 * instance removed them on purpose. */
	for (i = 0; i < src_children->len; i++)
	{
		LrgNode *sc = g_ptr_array_index (src_children, i);
		LrgNode *ic = matched_child (w, inst, sc);

		if (ic == NULL)
		{
			g_autofree gchar *removed = g_strconcat ("children/-", lrg_node_get_guid (sc), NULL);
			const gchar      *ig;
			LrgNode          *elsewhere = NULL;

			if (walk_overridden (w, inst, removed))
				continue;

			ig = lrg_prefab_link_map_lookup_instance (w->link, lrg_node_get_guid (sc));
			if (ig != NULL)
				elsewhere = lrg_node_find_by_guid (w->owner, ig);

			if (elsewhere != NULL && elsewhere != w->owner &&
			    !lrg_node_is_ancestor_of (elsewhere, inst))
			{
				g_autofree gchar *added = g_strconcat ("children/+", ig, NULL);
				LrgNode          *ep = lrg_node_get_parent (elsewhere);

				/* Moved in the instance on purpose: leave it there. */
				if (ep != NULL && walk_overridden (w, ep, added))
					continue;

				detach (elsewhere);  /* holds a reference across the move */
				lrg_node_insert_child (inst, elsewhere, last + 1);
				g_object_unref (elsewhere);
				ic = elsewhere;
			}
			else
			{
				g_autoptr(LrgNode) created = create_from_source (w, sc);

				lrg_node_insert_child (inst, created, last + 1);
				last = lrg_node_get_child_index (inst, created);
				w->changed = TRUE;
				continue;
			}
			w->changed = TRUE;
		}

		last = lrg_node_get_child_index (inst, ic);
		sync_node (w, ic, sc);
	}

	/* Pass 2: instance children whose source node is gone. */
	snapshot = g_ptr_array_copy (lrg_node_get_children (inst), ref_copy, NULL);
	g_ptr_array_set_free_func (snapshot, g_object_unref);
	for (i = 0; i < snapshot->len; i++)
	{
		LrgNode          *ic = g_ptr_array_index (snapshot, i);
		const gchar      *sg = walk_source_guid (w, ic);
		g_autofree gchar *added = NULL;

		if (sg == NULL || is_child_guid (src, sg))
			continue;
		if (lrg_node_find_by_guid (w->prefab, sg) != NULL)
			continue;  /* moved in the source: handled where it lives now */

		added = g_strconcat ("children/+", lrg_node_get_guid (ic), NULL);
		if (walk_overridden (w, inst, added) || subtree_has_keys (w, ic))
		{
			g_autofree gchar *key = walk_key (w, inst, added);

			/* Edited in the instance: keep it as an instance-added
			 * child, no longer mirroring anything. */
			lrg_node_foreach (ic, purge_node, w->link);
			lrg_prefab_link_add_override (w->link, key);
		}
		else
		{
			lrg_node_foreach (ic, purge_node, w->link);
			lrg_node_remove_child (inst, ic);
		}
		w->changed = TRUE;
	}
}

static void
sync_node (Walk    *w,
           LrgNode *inst,
           LrgNode *src)
{
	gboolean             is_root = (inst == w->owner);
	g_autoptr(GPtrArray) paths = g_ptr_array_new_with_free_func (g_free);
	CopyCtx              ctx;
	guint                i;

	ctx.to_source = FALSE;
	ctx.root = is_root;
	ctx.marker_path = lrg_prefab_link_get_source_path (w->link);

	diff_state (inst, src, is_root, paths);
	for (i = 0; i < paths->len; i++)
	{
		const gchar *path = g_ptr_array_index (paths, i);

		if (walk_overridden (w, inst, path))
			continue;
		if (copy_path (inst, src, path, &ctx))
			w->changed = TRUE;
	}

	/* The root keeps its 0.8.0-style source marker. */
	if (is_root && ensure_marker (inst, ctx.marker_path))
		w->changed = TRUE;

	sync_children (w, inst, src);

	if (!is_root)
		sync_nested_link (w, inst, src);
}

gboolean
lrg_prefab_sync_instance (LrgNode *instance,
                          LrgNode *prefab)
{
	Walk w;

	g_return_val_if_fail (LRG_IS_NODE (instance), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (prefab), FALSE);

	if (lrg_node_get_prefab_link (instance) == NULL)
		return FALSE;

	walk_init (&w, instance, prefab);

	/* The root pair is always mapped (instantiate / relink do it). */
	lrg_prefab_link_map_set (w.link, lrg_node_get_guid (instance), lrg_node_get_guid (prefab));
	sync_node (&w, instance, prefab);

	return w.changed;
}

/* --------------------------------------------------------------------------
 * Instantiate linked
 * -------------------------------------------------------------------------- */

LrgNode *
lrg_prefab_instantiate_linked (LrgNode     *prefab,
                               const gchar *source_path,
                               const gchar *source_guid)
{
	g_autoptr(LrgPrefabLink) link = NULL;
	g_autofree gchar        *root_guid = NULL;
	LrgNode                 *inst;
	GPtrArray               *children;
	Walk                     w;
	guint                    i;

	g_return_val_if_fail (LRG_IS_NODE (prefab), NULL);
	g_return_val_if_fail (source_path != NULL, NULL);

	/* A random root guid; everything below derives from it. */
	inst = lrg_node_new (lrg_node_get_name (prefab));
	lrg_node_copy_state (inst, prefab);
	root_guid = g_strdup (lrg_node_get_guid (inst));

	link = lrg_prefab_link_new (source_path, source_guid);
	lrg_prefab_link_map_set (link, root_guid, lrg_node_get_guid (prefab));
	lrg_node_set_prefab_link (inst, link);

	walk_init (&w, inst, prefab);
	children = lrg_node_get_children (prefab);
	for (i = 0; i < children->len; i++)
	{
		g_autoptr(LrgNode) child = create_from_source (&w, g_ptr_array_index (children, i));

		lrg_node_add_child (inst, child);
	}

	ensure_marker (inst, source_path);
	return inst;
}

/* --------------------------------------------------------------------------
 * Compute overrides
 * -------------------------------------------------------------------------- */

static void
compute_node (Walk       *w,
              LrgNode    *inst,
              LrgNode    *src,
              GHashTable *out)
{
	g_autoptr(GPtrArray) paths = g_ptr_array_new_with_free_func (g_free);
	GPtrArray           *children;
	guint                i;

	diff_state (inst, src, inst == w->owner, paths);
	for (i = 0; i < paths->len; i++)
		g_hash_table_add (out, walk_key (w, inst, g_ptr_array_index (paths, i)));

	children = lrg_node_get_children (src);
	for (i = 0; i < children->len; i++)
	{
		LrgNode *sc = g_ptr_array_index (children, i);
		LrgNode *ic = matched_child (w, inst, sc);

		if (ic != NULL)
			compute_node (w, ic, sc, out);
		else
		{
			g_autofree gchar *p = g_strconcat ("children/-", lrg_node_get_guid (sc), NULL);

			g_hash_table_add (out, walk_key (w, inst, p));
		}
	}

	children = lrg_node_get_children (inst);
	for (i = 0; i < children->len; i++)
	{
		LrgNode     *ic = g_ptr_array_index (children, i);
		const gchar *sg = walk_source_guid (w, ic);

		if (sg == NULL || !is_child_guid (src, sg))
		{
			g_autofree gchar *p = g_strconcat ("children/+", lrg_node_get_guid (ic), NULL);

			g_hash_table_add (out, walk_key (w, inst, p));
		}
	}
}

GStrv
lrg_prefab_compute_overrides (LrgNode *instance,
                              LrgNode *prefab)
{
	g_autoptr(GHashTable) out = NULL;
	Walk                  w;

	g_return_val_if_fail (LRG_IS_NODE (instance), NULL);
	g_return_val_if_fail (LRG_IS_NODE (prefab), NULL);

	out = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	if (lrg_node_get_prefab_link (instance) != NULL)
	{
		walk_init (&w, instance, prefab);
		compute_node (&w, instance, prefab, out);
	}

	return sorted_keys (out);
}

/* --------------------------------------------------------------------------
 * Resolving keys
 * -------------------------------------------------------------------------- */

/* The instance node addressed by @key (or NULL), and its path. */
static LrgNode *
resolve_key (Walk         *w,
             const gchar  *key,
             const gchar **path_out)
{
	g_autofree gchar *guid = NULL;

	*path_out = split_key (key, &guid);
	if (guid == NULL)
		return w->owner;

	return lrg_node_find_by_guid (w->owner, guid);
}

/* The source node mirrored by @inst, or NULL. */
static LrgNode *
source_of (Walk    *w,
           LrgNode *inst)
{
	const gchar *sg = walk_source_guid (w, inst);

	return (sg != NULL) ? lrg_node_find_by_guid (w->prefab, sg) : NULL;
}

static LrgNode *
find_child_by_guid (LrgNode     *parent,
                    const gchar *guid)
{
	GPtrArray *children = lrg_node_get_children (parent);
	guint      i;

	for (i = 0; i < children->len; i++)
	{
		LrgNode *c = g_ptr_array_index (children, i);

		if (g_strcmp0 (lrg_node_get_guid (c), guid) == 0)
			return c;
	}

	return NULL;
}

/* --------------------------------------------------------------------------
 * Apply to source
 * -------------------------------------------------------------------------- */

/* Applies one key; returns TRUE when it was applied. */
static gboolean
apply_one (Walk        *w,
           const gchar *key)
{
	const gchar *path;
	LrgNode     *node;
	LrgNode     *sn;

	node = resolve_key (w, key, &path);
	if (node == NULL)
	{
		/* Stale key (its node is gone): drop it. */
		lrg_prefab_link_remove_override (w->link, key);
		return FALSE;
	}

	sn = source_of (w, node);
	if (sn == NULL)
		return FALSE;

	if (g_str_has_prefix (path, "children/+"))
	{
		LrgNode              *child = find_child_by_guid (node, path + strlen ("children/+"));
		g_autoptr(GHashTable) table = NULL;
		g_autoptr(LrgNode)    copy = NULL;
		GHashTableIter        iter;
		gpointer              k;
		gpointer              v;

		lrg_prefab_link_remove_override (w->link, key);
		if (child == NULL)
			return FALSE;

		/* The source gets a fresh-guid copy; the instance child now
		 * mirrors it (and so does its whole subtree). */
		table = new_guid_table ();
		copy = clone_tree (child, GUID_RANDOM, NULL, table);
		copy_links (child, copy, table, FALSE);
		lrg_node_add_child (sn, copy);

		lrg_node_foreach (child, purge_node, w->link);
		g_hash_table_iter_init (&iter, table);
		while (g_hash_table_iter_next (&iter, &k, &v))
			lrg_prefab_link_map_set (w->link, k, v);
		return TRUE;
	}

	if (g_str_has_prefix (path, "children/-"))
	{
		LrgNode *gone = find_child_by_guid (sn, path + strlen ("children/-"));

		lrg_prefab_link_remove_override (w->link, key);
		if (gone != NULL)
			lrg_node_remove_child (sn, gone);
		return TRUE;
	}

	{
		CopyCtx          ctx;
		g_autofree gchar *k = g_strdup (key);

		ctx.to_source = TRUE;
		ctx.root = (node == w->owner);
		ctx.marker_path = NULL;
		if (!copy_path (sn, node, path, &ctx))
			return FALSE;

		/* Landing inside a nested instance of the prefab: that nested
		 * instance now overrides its own source here. */
		lrg_prefab_mark_override (sn, path);
		remove_keys_under (w->link, k);
	}

	return TRUE;
}

guint
lrg_prefab_apply_overrides_to_source (LrgNode            *instance,
                                      LrgNode            *prefab,
                                      const gchar *const *keys)
{
	g_auto(GStrv) all = NULL;
	Walk          w;
	guint         count = 0;
	guint         i;

	g_return_val_if_fail (LRG_IS_NODE (instance), 0);
	g_return_val_if_fail (LRG_IS_NODE (prefab), 0);

	if (lrg_node_get_prefab_link (instance) == NULL)
		return 0;

	walk_init (&w, instance, prefab);
	lrg_prefab_link_map_set (w.link, lrg_node_get_guid (instance), lrg_node_get_guid (prefab));

	if (keys == NULL)
	{
		all = lrg_prefab_link_get_overrides (w.link);
		keys = (const gchar *const *) all;
	}

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		if (apply_one (&w, keys[i]))
			count++;

	return count;
}

/* --------------------------------------------------------------------------
 * Revert
 * -------------------------------------------------------------------------- */

gboolean
lrg_prefab_revert_override (LrgNode     *instance,
                            LrgNode     *prefab,
                            const gchar *key)
{
	g_autofree gchar *k = NULL;
	const gchar      *path;
	LrgNode          *node;
	Walk              w;

	g_return_val_if_fail (LRG_IS_NODE (instance), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (prefab), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	if (lrg_node_get_prefab_link (instance) == NULL)
		return FALSE;

	walk_init (&w, instance, prefab);
	if (!g_hash_table_contains (w.link->overrides, key))
		return FALSE;

	k = g_strdup (key);
	node = resolve_key (&w, k, &path);
	remove_keys_under (w.link, k);
	if (node == NULL)
		return TRUE;  /* stale key: dropping it is the revert */

	if (g_str_has_prefix (path, "children/+"))
	{
		LrgNode *child = find_child_by_guid (node, path + strlen ("children/+"));

		if (child != NULL)
		{
			lrg_node_foreach (child, purge_node, w.link);
			lrg_node_remove_child (node, child);
		}
		return TRUE;
	}

	if (g_str_has_prefix (path, "children/-"))
	{
		LrgNode *sn = source_of (&w, node);
		LrgNode *sc = (sn != NULL) ? find_child_by_guid (sn, path + strlen ("children/-")) : NULL;

		if (sc != NULL && matched_child (&w, node, sc) == NULL)
		{
			g_autoptr(LrgNode) created = create_from_source (&w, sc);
			GPtrArray         *sib = lrg_node_get_children (sn);
			gint               idx = lrg_node_get_child_index (sn, sc);
			gint               at = -1;

			/* Insert after the instance counterpart of the previous
			 * source sibling, so the order matches the source. */
			if (idx > 0)
			{
				LrgNode *prev = matched_child (&w, node, g_ptr_array_index (sib, (guint) idx - 1));

				if (prev != NULL)
					at = lrg_node_get_child_index (node, prev) + 1;
			}
			else
				at = 0;
			lrg_node_insert_child (node, created, at);
		}
		return TRUE;
	}

	{
		LrgNode *sn = source_of (&w, node);
		CopyCtx  ctx;

		ctx.to_source = FALSE;
		ctx.root = (node == w.owner);
		ctx.marker_path = lrg_prefab_link_get_source_path (w.link);
		if (sn != NULL)
			copy_path (node, sn, path, &ctx);
	}

	return TRUE;
}

guint
lrg_prefab_revert_all (LrgNode *instance,
                       LrgNode *prefab)
{
	g_auto(GStrv) keys = NULL;
	guint         count = 0;
	guint         i;

	g_return_val_if_fail (LRG_IS_NODE (instance), 0);
	g_return_val_if_fail (LRG_IS_NODE (prefab), 0);

	if (lrg_node_get_prefab_link (instance) == NULL)
		return 0;

	keys = lrg_prefab_link_get_overrides (lrg_node_get_prefab_link (instance));
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		if (lrg_prefab_revert_override (instance, prefab, keys[i]))
			count++;

	/* Values that drifted without being marked (edits made outside the
	 * editor) go back too. */
	lrg_prefab_sync_instance (instance, prefab);
	return count;
}

/* --------------------------------------------------------------------------
 * Unlink / relink
 * -------------------------------------------------------------------------- */

gboolean
lrg_prefab_unlink (LrgNode *instance)
{
	g_return_val_if_fail (LRG_IS_NODE (instance), FALSE);

	if (lrg_node_get_prefab_link (instance) == NULL)
		return FALSE;

	lrg_node_set_prefab_link (instance, NULL);
	return TRUE;
}

/* Structural matching of @inst's children against @src's (see relink). */
static void
relink_match (LrgPrefabLink *link,
              LrgNode       *inst,
              LrgNode       *src)
{
	GPtrArray            *ic = lrg_node_get_children (inst);
	GPtrArray            *sc = lrg_node_get_children (src);
	g_autoptr(GPtrArray)  left_i = g_ptr_array_new ();
	g_autoptr(GPtrArray)  left_s = g_ptr_array_new ();
	g_autofree gboolean  *used = g_new0 (gboolean, sc->len + 1);
	guint                 i, j;

	lrg_prefab_link_map_set (link, lrg_node_get_guid (inst), lrg_node_get_guid (src));

	/* By name: first unused source child with the same name. */
	for (i = 0; i < ic->len; i++)
	{
		LrgNode  *a = g_ptr_array_index (ic, i);
		gboolean  found = FALSE;

		for (j = 0; j < sc->len; j++)
		{
			LrgNode *b = g_ptr_array_index (sc, j);

			if (!used[j] && g_strcmp0 (lrg_node_get_name (a), lrg_node_get_name (b)) == 0)
			{
				used[j] = TRUE;
				found = TRUE;
				relink_match (link, a, b);
				break;
			}
		}
		if (!found)
			g_ptr_array_add (left_i, a);
	}
	for (j = 0; j < sc->len; j++)
		if (!used[j])
			g_ptr_array_add (left_s, g_ptr_array_index (sc, j));

	/* Leftovers pair by order only when both sides agree on the count. */
	if (left_i->len == left_s->len)
		for (i = 0; i < left_i->len; i++)
			relink_match (link, g_ptr_array_index (left_i, i), g_ptr_array_index (left_s, i));
}

gboolean
lrg_prefab_relink (LrgNode     *instance,
                   LrgNode     *prefab,
                   const gchar *source_path,
                   const gchar *source_guid)
{
	g_autoptr(LrgPrefabLink) link = NULL;
	g_auto(GStrv)            keys = NULL;
	guint                    i;

	g_return_val_if_fail (LRG_IS_NODE (instance), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (prefab), FALSE);

	if (source_path == NULL)
		source_path = lrg_prefab_get_marker_source (instance);
	if (source_path == NULL)
		return FALSE;

	link = lrg_prefab_link_new (source_path, source_guid);
	relink_match (link, instance, prefab);
	lrg_node_set_prefab_link (instance, link);

	/* Whatever differs now is, by definition, an instance override. */
	keys = lrg_prefab_compute_overrides (instance, prefab);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		lrg_prefab_link_add_override (link, keys[i]);

	ensure_marker (instance, source_path);
	return TRUE;
}

/* ==========================================================================
 * Persistence (reuses the .rlevel serializer)
 * ========================================================================== */

gboolean
lrg_prefab_save (LrgNode      *node,
                 const gchar  *path,
                 GError      **error)
{
	g_autoptr(LrgLevel)           level = NULL;
	g_autoptr(LrgNode)            copy = NULL;
	g_autoptr(LrgLevelSerializer) ser = NULL;

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* Copy so we never mutate the source node's parent linkage. Guids are
	 * kept: linked instances map onto them. A link on @node itself is kept
	 * (since 0.9.0): the prefab is then a variant of the link's source. */
	copy = lrg_prefab_copy (node, TRUE);

	level = lrg_level_new ("prefab");
	lrg_level_add_node (level, copy, NULL);

	ser = lrg_level_serializer_new ();
	return lrg_level_serializer_save_to_file (ser, level, path, error);
}

LrgNode *
lrg_prefab_load (const gchar  *path,
                 GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevel)           level = NULL;
	LrgNode                      *root;
	GPtrArray                    *children;
	LrgNode                      *node;

	g_return_val_if_fail (path != NULL, NULL);

	ser = lrg_level_serializer_new ();
	level = lrg_level_serializer_load_from_file (ser, path, error);
	if (level == NULL)
		return NULL;

	root = lrg_level_get_root (level);
	children = lrg_node_get_children (root);
	if (children == NULL || children->len == 0)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "Prefab file contains no node");
		return NULL;
	}

	/* Detach the first node from the (soon-discarded) level and return it.
	 * Its own link, if any, makes it a variant (since 0.9.0). */
	node = g_object_ref (g_ptr_array_index (children, 0));
	lrg_level_remove_node (level, node);

	return node;
}

/* --------------------------------------------------------------------------
 * Nested resolution and variant chains
 * -------------------------------------------------------------------------- */

static LrgNode *load_resolved (const gchar *path, GPtrArray *stack,
                               GHashTable *deps, GError **error);

/*
 * resolve_source_path:
 * @source: a source path recorded in a link
 * @from: (nullable): the file that names it
 *
 * A relative source path is tried as given (relative to the current
 * directory, the editor's convention) and then relative to the directory
 * of @from, so prefab files referencing their neighbours load from
 * anywhere. Returns the first existing candidate, else @source.
 */
static gchar *
resolve_source_path (const gchar *source,
                     const gchar *from)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *near = NULL;

	if (g_path_is_absolute (source) || g_file_test (source, G_FILE_TEST_EXISTS) || from == NULL)
		return g_strdup (source);

	dir = g_path_get_dirname (from);
	near = g_build_filename (dir, source, NULL);
	if (g_file_test (near, G_FILE_TEST_EXISTS))
		return g_steal_pointer (&near);
	return g_strdup (source);
}

/*
 * resolve_below:
 *
 * Resolves the outermost nested instances below @node (not @node). In a
 * variant, descendants mirroring the base (mapped by @root_link) were
 * already synced with the base and are only descended into, to reach
 * nested instances the variant added below them.
 */
static gboolean
resolve_below (LrgNode       *node,
               LrgPrefabLink *root_link,
               const gchar   *from,
               GPtrArray     *stack,
               GHashTable    *deps,
               GError       **error)
{
	GPtrArray *children = lrg_node_get_children (node);
	guint      i;

	for (i = 0; i < children->len; i++)
	{
		LrgNode       *child = g_ptr_array_index (children, i);
		LrgPrefabLink *link = lrg_node_get_prefab_link (child);
		gboolean       mapped = root_link != NULL &&
		                        lrg_prefab_link_map_lookup (root_link, lrg_node_get_guid (child)) != NULL;

		if (link != NULL && !mapped && lrg_prefab_link_get_source_path (link) != NULL)
		{
			g_autoptr(LrgNode) nested = NULL;
			g_autofree gchar  *src = resolve_source_path (lrg_prefab_link_get_source_path (link), from);

			g_hash_table_add (deps, canonical_path (src));
			nested = load_resolved (src, stack, deps, error);
			if (nested == NULL)
				return FALSE;
			lrg_prefab_sync_instance (child, nested);
			continue;  /* its own nesting came resolved from its source */
		}

		if (!resolve_below (child, root_link, from, stack, deps, error))
			return FALSE;
	}

	return TRUE;
}

/*
 * check_stack:
 *
 * The shared guard of nesting and variant chains: a file already on the
 * stack is a cycle, and the stack may hold at most PREFAB_MAX_DEPTH files.
 */
static gboolean
check_stack (GPtrArray    *stack,
             const gchar  *canon,
             const gchar  *path,
             GError      **error)
{
	guint i;

	for (i = 0; i < stack->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (stack, i), canon) == 0)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			             "Prefab %s contains itself (nesting or variant cycle)", path);
			return FALSE;
		}
	}
	if (stack->len >= PREFAB_MAX_DEPTH)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "Prefab nesting or variant chain deeper than %d levels at %s",
		             PREFAB_MAX_DEPTH, path);
		return FALSE;
	}
	return TRUE;
}

static LrgNode *
load_resolved (const gchar  *path,
               GPtrArray    *stack,
               GHashTable   *deps,
               GError      **error)
{
	g_autofree gchar   *canon = canonical_path (path);
	g_autoptr(LrgNode)  prefab = NULL;
	LrgPrefabLink      *base_link;
	gboolean            ok = TRUE;

	if (!check_stack (stack, canon, path, error))
		return NULL;

	prefab = lrg_prefab_load (path, error);
	if (prefab == NULL)
		return NULL;

	g_ptr_array_add (stack, g_strdup (canon));

	/* 1. A variant: resolve its base chain first, then sync the variant
	 *    against the resolved base (the variant's overrides are kept). */
	base_link = lrg_node_get_prefab_link (prefab);
	if (base_link != NULL && lrg_prefab_link_get_source_path (base_link) != NULL)
	{
		g_autofree gchar   *base_path = resolve_source_path (lrg_prefab_link_get_source_path (base_link), path);
		g_autoptr(LrgNode)  base = NULL;

		g_hash_table_add (deps, canonical_path (base_path));
		base = load_resolved (base_path, stack, deps, error);
		if (base == NULL)
			ok = FALSE;
		else
			lrg_prefab_sync_instance (prefab, base);
	}

	/* 2. Nested instances the prefab (or variant) holds itself. */
	if (ok)
		ok = resolve_below (prefab, base_link, path, stack, deps, error);
	g_ptr_array_remove_index (stack, stack->len - 1);

	return ok ? g_steal_pointer (&prefab) : NULL;
}

gboolean
lrg_prefab_resolve_nested (LrgNode  *prefab,
                           GError  **error)
{
	g_autoptr(GPtrArray)  stack = NULL;
	g_autoptr(GHashTable) deps = NULL;

	g_return_val_if_fail (LRG_IS_NODE (prefab), FALSE);

	stack = g_ptr_array_new_with_free_func (g_free);
	deps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	return resolve_below (prefab, lrg_node_get_prefab_link (prefab), NULL, stack, deps, error);
}

LrgNode *
lrg_prefab_load_resolved (const gchar  *path,
                          GStrv        *dependencies,
                          GError      **error)
{
	g_autoptr(GPtrArray)  stack = NULL;
	g_autoptr(GHashTable) deps = NULL;
	LrgNode              *prefab;

	g_return_val_if_fail (path != NULL, NULL);

	stack = g_ptr_array_new_with_free_func (g_free);
	deps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	prefab = load_resolved (path, stack, deps, error);

	if (dependencies != NULL)
		*dependencies = (prefab != NULL) ? sorted_keys (deps) : NULL;

	return prefab;
}

/* ==========================================================================
 * LrgPrefabStorage and the level setting (since 0.9.0)
 * ========================================================================== */

GType
lrg_prefab_storage_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PREFAB_STORAGE_DEFAULT, "LRG_PREFAB_STORAGE_DEFAULT", "default" },
			{ LRG_PREFAB_STORAGE_EXPANDED, "LRG_PREFAB_STORAGE_EXPANDED", "expanded" },
			{ LRG_PREFAB_STORAGE_REFERENCE, "LRG_PREFAB_STORAGE_REFERENCE", "reference" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgPrefabStorage"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

LrgPrefabStorage
lrg_prefab_storage_from_string (const gchar *text)
{
	if (text == NULL)
		return LRG_PREFAB_STORAGE_DEFAULT;
	if (g_ascii_strcasecmp (text, "expanded") == 0)
		return LRG_PREFAB_STORAGE_EXPANDED;
	if (g_ascii_strcasecmp (text, "reference") == 0)
		return LRG_PREFAB_STORAGE_REFERENCE;
	return LRG_PREFAB_STORAGE_DEFAULT;
}

const gchar *
lrg_prefab_storage_to_string (LrgPrefabStorage storage)
{
	switch (storage)
	{
	case LRG_PREFAB_STORAGE_EXPANDED:  return "expanded";
	case LRG_PREFAB_STORAGE_REFERENCE: return "reference";
	case LRG_PREFAB_STORAGE_DEFAULT:
	default:                           return "default";
	}
}

/* The level setting rides on the LrgLevel as qdata: it is a property of
 * the document that only the serializer and the prefab code care about. */
static GQuark
level_storage_quark (void)
{
	return g_quark_from_static_string ("lrg-prefab-level-storage");
}

LrgPrefabStorage
lrg_prefab_level_get_storage (LrgLevel *level)
{
	g_return_val_if_fail (LRG_IS_LEVEL (level), LRG_PREFAB_STORAGE_DEFAULT);

	return (LrgPrefabStorage) GPOINTER_TO_INT (g_object_get_qdata (G_OBJECT (level), level_storage_quark ()));
}

void
lrg_prefab_level_set_storage (LrgLevel         *level,
                              LrgPrefabStorage  storage)
{
	g_return_if_fail (LRG_IS_LEVEL (level));

	g_object_set_qdata (G_OBJECT (level), level_storage_quark (), GINT_TO_POINTER ((gint) storage));
}

/* ==========================================================================
 * Guids and variants (since 0.9.0)
 * ========================================================================== */

gchar *
lrg_prefab_derive_guid (const gchar *instance_root_guid,
                        const gchar *source_guid)
{
	g_return_val_if_fail (instance_root_guid != NULL, NULL);
	g_return_val_if_fail (source_guid != NULL, NULL);

	return derive_guid (instance_root_guid, source_guid);
}

gboolean
lrg_prefab_is_variant (LrgNode *prefab)
{
	LrgPrefabLink *link;

	g_return_val_if_fail (LRG_IS_NODE (prefab), FALSE);

	link = lrg_node_get_prefab_link (prefab);
	return link != NULL && lrg_prefab_link_get_source_path (link) != NULL;
}

GStrv
lrg_prefab_get_variant_chain (const gchar  *path,
                              GError      **error)
{
	g_autoptr(GPtrArray) chain = NULL;
	g_autoptr(GPtrArray) seen = NULL;
	g_autofree gchar    *current = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	chain = g_ptr_array_new_with_free_func (g_free);
	seen = g_ptr_array_new_with_free_func (g_free);
	current = g_strdup (path);

	/* Read each root link in turn; the shared guard catches loops and
	 * over-long chains. */
	while (current != NULL)
	{
		g_autofree gchar   *canon = canonical_path (current);
		g_autoptr(LrgNode)  prefab = NULL;
		LrgPrefabLink      *link;
		gchar              *next = NULL;

		if (!check_stack (seen, canon, current, error))
			return NULL;
		prefab = lrg_prefab_load (current, error);
		if (prefab == NULL)
			return NULL;
		g_ptr_array_add (seen, g_steal_pointer (&canon));

		link = lrg_node_get_prefab_link (prefab);
		if (link != NULL && lrg_prefab_link_get_source_path (link) != NULL)
			next = resolve_source_path (lrg_prefab_link_get_source_path (link), current);

		g_ptr_array_add (chain, g_steal_pointer (&current));
		current = next;
	}

	g_ptr_array_add (chain, NULL);
	return (GStrv) g_ptr_array_free (g_steal_pointer (&chain), FALSE);
}

/* A new variant root sits at the prefab origin. */
static void
zero_root_placement (LrgNode *root)
{
	lrg_node_set_location_xyz (root, 0.0f, 0.0f, 0.0f);
	lrg_node_set_rotation_xyz (root, 0.0f, 0.0f, 0.0f);
}

LrgNode *
lrg_prefab_create_variant (LrgNode     *base,
                           const gchar *base_path,
                           const gchar *base_guid)
{
	LrgNode *variant;

	g_return_val_if_fail (LRG_IS_NODE (base), NULL);
	g_return_val_if_fail (base_path != NULL, NULL);

	/* A variant is a linked instance of its base saved as a prefab. */
	variant = lrg_prefab_instantiate_linked (base, base_path, base_guid);
	zero_root_placement (variant);
	return variant;
}

/* Fills @table with instance guid -> copy guid for two parallel trees. */
static void
pair_guids (LrgNode    *a,
            LrgNode    *b,
            GHashTable *table)
{
	GPtrArray *ac = lrg_node_get_children (a);
	GPtrArray *bc = lrg_node_get_children (b);
	guint      i;

	g_hash_table_insert (table, g_strdup (lrg_node_get_guid (a)), g_strdup (lrg_node_get_guid (b)));
	for (i = 0; i < ac->len && i < bc->len; i++)
		pair_guids (g_ptr_array_index (ac, i), g_ptr_array_index (bc, i), table);
}

LrgNode *
lrg_prefab_derive_variant (LrgNode     *instance,
                           const gchar *variant_path,
                           const gchar *variant_guid)
{
	LrgPrefabLink            *link;
	LrgNode                  *variant;
	g_autoptr(GHashTable)     table = NULL;
	g_autoptr(LrgPrefabLink)  relinked = NULL;
	GHashTableIter            iter;
	gpointer                  key;
	gpointer                  value;

	g_return_val_if_fail (LRG_IS_NODE (instance), NULL);
	g_return_val_if_fail (variant_path != NULL, NULL);

	link = lrg_node_get_prefab_link (instance);
	if (link == NULL || lrg_prefab_link_get_source_path (link) == NULL)
		return NULL;

	/* 1. The variant: a fresh-guid copy whose root link (the instance's
	 *    link, map and overrides rewritten to the new guids) points at
	 *    the instance's source. */
	variant = lrg_prefab_clone (instance);
	zero_root_placement (variant);

	/* 2. Relink the instance to the variant: every instance node mirrors
	 *    its copy, and nothing is overridden (it matches the variant). */
	table = new_guid_table ();
	pair_guids (instance, variant, table);
	relinked = lrg_prefab_link_new (variant_path, variant_guid);
	g_hash_table_iter_init (&iter, table);
	while (g_hash_table_iter_next (&iter, &key, &value))
		lrg_prefab_link_map_set (relinked, key, value);
	lrg_node_set_prefab_link (instance, relinked);
	ensure_marker (instance, variant_path);

	return variant;
}

/* ==========================================================================
 * Reference storage (since 0.9.0)
 * ========================================================================== */

/* TRUE when some override key is qualified with @guid, or adds/moves the
 * node with that guid (`children/+<guid>` anywhere). */
static gboolean
node_has_keys (LrgPrefabLink *link,
               const gchar   *guid)
{
	g_autofree gchar *qual = g_strconcat (guid, ":", NULL);
	g_autofree gchar *added = g_strconcat ("children/+", guid, NULL);
	GHashTableIter    iter;
	gpointer          k;

	g_hash_table_iter_init (&iter, link->overrides);
	while (g_hash_table_iter_next (&iter, &k, NULL))
	{
		g_autofree gchar *kg = NULL;
		const gchar      *path = split_key (k, &kg);

		if (g_str_has_prefix (k, qual) || g_strcmp0 (path, added) == 0)
			return TRUE;
	}
	return FALSE;
}

/* Whether the sparse form must keep @node (see make_reference). */
static gboolean
ref_keeps (LrgPrefabLink *link,
           LrgNode       *node)
{
	GPtrArray *children;
	guint      i;

	if (lrg_prefab_link_map_lookup (link, lrg_node_get_guid (node)) == NULL)
		return TRUE;  /* instance-added */
	if (node_has_keys (link, lrg_node_get_guid (node)))
		return TRUE;

	children = lrg_node_get_children (node);
	for (i = 0; i < children->len; i++)
		if (ref_keeps (link, g_ptr_array_index (children, i)))
			return TRUE;
	return FALSE;
}

static LrgNode *ref_sparse (LrgPrefabLink *link, LrgNode *node, gboolean is_root);

/* The children of a kept node: kept ones recursively, the others as
 * placeholders; nothing when no child is kept. */
static void
ref_sparse_children (LrgPrefabLink *link,
                     LrgNode       *node,
                     LrgNode       *dst)
{
	GPtrArray *children = lrg_node_get_children (node);
	gboolean   any = FALSE;
	guint      i;

	for (i = 0; i < children->len && !any; i++)
		any = ref_keeps (link, g_ptr_array_index (children, i));
	if (!any)
		return;

	for (i = 0; i < children->len; i++)
	{
		LrgNode            *c = g_ptr_array_index (children, i);
		g_autoptr(LrgNode)  copy = NULL;

		if (ref_keeps (link, c))
			copy = ref_sparse (link, c, FALSE);
		else
		{
			copy = lrg_node_new (lrg_node_get_name (c));
			lrg_node_set_guid (copy, lrg_node_get_guid (c));
		}
		lrg_node_add_child (dst, copy);
	}
}

static LrgNode *
ref_sparse (LrgPrefabLink *link,
            LrgNode       *node,
            gboolean       is_root)
{
	LrgNode *dst;

	/* Instance-added subtrees are copied whole (guids, nested links). */
	if (!is_root && lrg_prefab_link_map_lookup (link, lrg_node_get_guid (node)) == NULL)
		return lrg_prefab_copy (node, TRUE);

	/* A kept mapped node: its own state (nested links come back from
	 * the source), plus its children when one of them is kept. */
	dst = lrg_node_new (lrg_node_get_name (node));
	lrg_node_copy_state (dst, node);
	lrg_node_set_guid (dst, lrg_node_get_guid (node));
	ref_sparse_children (link, node, dst);
	return dst;
}

LrgNode *
lrg_prefab_make_reference (LrgNode *instance)
{
	LrgPrefabLink            *link;
	LrgNode                  *sparse;
	g_autoptr(LrgPrefabLink)  copy = NULL;

	g_return_val_if_fail (LRG_IS_NODE (instance), NULL);

	link = lrg_node_get_prefab_link (instance);
	if (link == NULL)
		return NULL;

	sparse = ref_sparse (link, instance, TRUE);
	copy = lrg_prefab_link_copy (link);
	lrg_node_set_prefab_link (sparse, copy);
	return sparse;
}

/*
 * RefExpand: state of one lrg_prefab_instantiate_reference() call.
 */
typedef struct
{
	LrgNode       *sparse;      /* the reference (read only) */
	LrgPrefabLink *in_link;     /* its link: exceptions of the map + keys */
	LrgNode       *prefab;      /* the resolved source */
	LrgNode       *full;        /* the instance being built */
	LrgPrefabLink *out_link;    /* the full link of @full */
	const gchar   *root_guid;
} RefExpand;

/* The instance guid of source node @sg: recorded, else derived. */
static gchar *
ref_guid_for (RefExpand   *x,
              const gchar *sg)
{
	const gchar *ig = lrg_prefab_link_map_lookup_instance (x->in_link, sg);

	if (ig != NULL)
		return g_strdup (ig);
	return derive_guid (x->root_guid, sg);
}

/* The override key for @path on the instance node with guid @ig. */
static gchar *
ref_key (RefExpand   *x,
         const gchar *ig,
         const gchar *path)
{
	if (g_strcmp0 (ig, x->root_guid) == 0)
		return g_strdup (path);
	return g_strdup_printf ("%s:%s", ig, path);
}

/*
 * ref_build:
 *
 * Creates the instance copy of source subtree @src (not the root): each
 * node gets its recorded or derived guid and a map entry; children the
 * instance removed (`children/-<source guid>` on their parent) are
 * skipped.
 */
static LrgNode *
ref_build (RefExpand *x,
           LrgNode   *src)
{
	LrgNode          *node;
	g_autofree gchar *ig = ref_guid_for (x, lrg_node_get_guid (src));
	GPtrArray        *children;
	guint             i;

	node = lrg_node_new (lrg_node_get_name (src));
	lrg_node_copy_state (node, src);
	lrg_node_set_guid (node, ig);
	lrg_prefab_link_map_set (x->out_link, ig, lrg_node_get_guid (src));

	children = lrg_node_get_children (src);
	for (i = 0; i < children->len; i++)
	{
		LrgNode            *sc = g_ptr_array_index (children, i);
		g_autofree gchar   *removed = g_strconcat ("children/-", lrg_node_get_guid (sc), NULL);
		g_autofree gchar   *key = ref_key (x, ig, removed);
		g_autoptr(LrgNode)  child = NULL;

		if (lrg_prefab_link_has_override (x->in_link, key))
			continue;
		child = ref_build (x, sc);
		lrg_node_add_child (node, child);
	}
	return node;
}

/* Gives every built node whose source node is nested-linked the
 * translated link (like a sync would). */
static gboolean
ref_nested_links (LrgNode  *src,
                  gpointer  user_data)
{
	RefExpand     *x = user_data;
	LrgPrefabLink *src_link = lrg_node_get_prefab_link (src);
	const gchar   *ig;
	LrgNode       *inst;
	Walk           w;

	if (src_link == NULL || src == x->prefab)
		return TRUE;
	ig = lrg_prefab_link_map_lookup_instance (x->out_link, lrg_node_get_guid (src));
	inst = (ig != NULL) ? lrg_node_find_by_guid (x->full, ig) : NULL;
	if (inst == NULL)
		return TRUE;

	walk_init (&w, x->full, x->prefab);
	{
		g_autoptr(LrgPrefabLink) translated = translate_link (&w, src_link);

		lrg_node_set_prefab_link (inst, translated);
	}
	return TRUE;
}

/* Moves @node to index @index of @parent (any old parent). */
static void
ref_place (LrgNode *parent,
           LrgNode *node,
           gint     index)
{
	if (lrg_node_get_parent (node) == parent && lrg_node_get_child_index (parent, node) == index)
		return;
	detach (node);
	lrg_node_insert_child (parent, node, index);
	g_object_unref (node);
}

/*
 * ref_children:
 *
 * Applies the recorded children of sparse node @sp to the built node
 * with the same guid: inserts instance-added subtrees, moves nodes moved
 * on purpose (`children/+<guid>` on the parent), re-creates moved nodes
 * the old parent removed, recurses into kept mapped children and finally
 * restores the recorded order. Children the source added since the save
 * keep their source position.
 */
static void
ref_children (RefExpand *x,
              LrgNode   *sp)
{
	GPtrArray           *sc = lrg_node_get_children (sp);
	LrgNode             *fp = lrg_node_find_by_guid (x->full, lrg_node_get_guid (sp));
	g_autoptr(GPtrArray) wanted = NULL;
	g_autoptr(GPtrArray) current = NULL;
	guint                i;
	guint                k;

	if (fp == NULL || sc->len == 0)
		return;

	wanted = g_ptr_array_new ();
	for (i = 0; i < sc->len; i++)
	{
		LrgNode          *c = g_ptr_array_index (sc, i);
		const gchar      *g = lrg_node_get_guid (c);
		const gchar      *sg = lrg_prefab_link_map_lookup (x->in_link, g);
		LrgNode          *existing = lrg_node_find_by_guid (x->full, g);
		g_autofree gchar *added = g_strconcat ("children/+", g, NULL);
		g_autofree gchar *key = ref_key (x, lrg_node_get_guid (fp), added);
		gboolean          on_purpose = lrg_prefab_link_has_override (x->in_link, key);

		if (sg == NULL || lrg_node_find_by_guid (x->prefab, sg) == NULL)
		{
			/* Instance-added, or its source node is gone: keep the
			 * recorded subtree when it is added or edited. */
			if (existing == NULL && (sg == NULL || node_has_keys (x->in_link, g)))
			{
				g_autoptr(LrgNode) copy = lrg_prefab_copy (c, TRUE);

				if (sg != NULL)
				{
					/* No longer mirrors anything: an added child. */
					g_autofree gchar *k2 = ref_key (x, lrg_node_get_guid (fp), added);

					lrg_node_foreach (copy, purge_node, x->out_link);
					lrg_prefab_link_add_override (x->out_link, k2);
				}
				lrg_node_add_child (fp, copy);
				existing = copy;
			}
			if (existing != NULL)
				g_ptr_array_add (wanted, existing);
			continue;
		}

		if (existing == NULL)
		{
			/* Removed where the source has it and moved here. */
			if (!on_purpose)
				continue;
			{
				g_autoptr(LrgNode) built = ref_build (x, lrg_node_find_by_guid (x->prefab, sg));

				lrg_node_add_child (fp, built);
				existing = built;
			}
		}
		else if (lrg_node_get_parent (existing) != fp)
		{
			if (!on_purpose)
				continue;  /* the source moved it: follow the source */
			ref_place (fp, existing, -1);
		}

		g_ptr_array_add (wanted, existing);
		ref_children (x, c);
	}

	/* Restore the recorded order on the slots the recorded children
	 * occupy; other children (new in the source) stay where they are. */
	current = g_ptr_array_copy (lrg_node_get_children (fp), NULL, NULL);
	g_ptr_array_set_free_func (current, NULL);  /* borrowed: the copy inherits unref */
	k = 0;
	for (i = 0; i < current->len && k < wanted->len; i++)
	{
		LrgNode *c = g_ptr_array_index (current, i);

		if (g_ptr_array_find (wanted, c, NULL))
		{
			ref_place (fp, g_ptr_array_index (wanted, k), (gint) i);
			k++;
		}
	}
}

LrgNode *
lrg_prefab_instantiate_reference (LrgNode  *reference,
                                  LrgNode  *prefab,
                                  GError  **error)
{
	RefExpand                x;
	LrgPrefabLink           *in_link;
	g_autoptr(LrgPrefabLink) out_link = NULL;
	g_auto(GStrv)            keys = NULL;
	GPtrArray               *children;
	LrgNode                 *full;
	CopyCtx                  ctx;
	guint                    i;

	g_return_val_if_fail (LRG_IS_NODE (reference), NULL);
	g_return_val_if_fail (LRG_IS_NODE (prefab), NULL);

	in_link = lrg_node_get_prefab_link (reference);
	if (in_link == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Node %s is not a prefab reference (no link)",
		             lrg_node_get_name (reference) != NULL ? lrg_node_get_name (reference) : "?");
		return NULL;
	}

	/* 1. The root: source state, then the instance-owned fields. */
	full = lrg_node_new (lrg_node_get_name (reference));
	lrg_node_copy_state (full, prefab);
	lrg_node_set_guid (full, lrg_node_get_guid (reference));
	lrg_node_set_name (full, lrg_node_get_name (reference));
	{
		GrlVector3 *v = lrg_node_get_location (reference);

		lrg_node_set_location_xyz (full, v->x, v->y, v->z);
		v = lrg_node_get_rotation (reference);
		lrg_node_set_rotation_xyz (full, v->x, v->y, v->z);
	}
	lrg_node_set_locked (full, lrg_node_get_locked (reference));

	out_link = lrg_prefab_link_new (lrg_prefab_link_get_source_path (in_link),
	                                lrg_prefab_link_get_source_guid (in_link));
	keys = lrg_prefab_link_get_overrides (in_link);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		lrg_prefab_link_add_override (out_link, keys[i]);
	lrg_prefab_link_map_set (out_link, lrg_node_get_guid (full), lrg_node_get_guid (prefab));
	lrg_node_set_prefab_link (full, out_link);

	x.sparse = reference;
	x.in_link = in_link;
	x.prefab = prefab;
	x.full = full;
	x.out_link = out_link;
	x.root_guid = lrg_node_get_guid (full);

	/* 2. Every source child, minus the ones the instance removed. */
	children = lrg_node_get_children (prefab);
	for (i = 0; i < children->len; i++)
	{
		LrgNode            *sc = g_ptr_array_index (children, i);
		g_autofree gchar   *removed = g_strconcat ("children/-", lrg_node_get_guid (sc), NULL);
		g_autoptr(LrgNode)  child = NULL;

		if (lrg_prefab_link_has_override (in_link, removed))
			continue;
		child = ref_build (&x, sc);
		lrg_node_add_child (full, child);
	}
	lrg_node_foreach (prefab, ref_nested_links, &x);

	/* 3. Structure recorded in the reference: added subtrees, purposeful
	 *    moves, child order. */
	ref_children (&x, reference);

	/* 4. Overridden values, copied from the recorded nodes. */
	ctx.to_source = FALSE;
	ctx.marker_path = lrg_prefab_link_get_source_path (in_link);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *guid = NULL;
		const gchar      *path = split_key (keys[i], &guid);
		const gchar      *target = guid != NULL ? guid : x.root_guid;
		LrgNode          *dst;
		LrgNode          *src;

		if (g_str_has_prefix (path, "children/"))
			continue;
		dst = lrg_node_find_by_guid (full, target);
		src = lrg_node_find_by_guid (reference, target);
		if (dst == NULL || src == NULL)
			continue;
		ctx.root = (dst == full);
		copy_path (dst, src, path, &ctx);
	}

	/* 5. Recorded map entries of nodes the instance no longer has (kept by
	 *    the editor after a delete) stay in the map, as in the expanded
	 *    form. */
	{
		g_auto(GStrv) mapped = lrg_prefab_link_get_mapped_guids (in_link);

		for (i = 0; mapped != NULL && mapped[i] != NULL; i++)
		{
			const gchar *sg = lrg_prefab_link_map_lookup (in_link, mapped[i]);

			if (lrg_node_find_by_guid (full, mapped[i]) == NULL &&
			    lrg_prefab_link_map_lookup (out_link, mapped[i]) == NULL &&
			    lrg_prefab_link_map_lookup_instance (out_link, sg) == NULL)
				lrg_prefab_link_map_set (out_link, mapped[i], sg);
		}
	}

	/* 6. The 0.8.0 source marker, as every linked instance root has. */
	ensure_marker (full, lrg_prefab_link_get_source_path (in_link));
	return full;
}
