/* lrg-hud-layout.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHudLayout: a hash table of entries by id (the player's state and,
 * for added pieces, the default placement) and an array of the added
 * ids in order.  Entries of pieces that were never added (or were
 * removed) keep the player's state so it is saved again and applies when
 * the piece comes.  Loads stage everything and apply only when the whole
 * input is valid.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "hud/lrg-hud-layout.h"

#include <errno.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <yaml-glib.h>

typedef struct
{
    gchar        *id;
    gchar        *label;
    gboolean      added;
    /* the game's default */
    LrgHudAnchor  default_anchor;
    gfloat        default_x;
    gfloat        default_y;
    /* the player's */
    gboolean      placed;
    LrgHudAnchor  anchor;
    gfloat        x;
    gfloat        y;
    gboolean      locked;
    gboolean      hidden;
} Entry;

struct _LrgHudLayout
{
    GObject     parent_instance;

    GHashTable *entries;   /* gchar * -> Entry * (owned) */
    GPtrArray  *order;     /* gchar *, ids of the added pieces */
};

G_DEFINE_TYPE (LrgHudLayout, lrg_hud_layout, G_TYPE_OBJECT)

enum
{
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
entry_free (gpointer data)
{
    Entry *entry = data;

    g_free (entry->id);
    g_free (entry->label);
    g_free (entry);
}

static Entry *
entry_new (const gchar *id)
{
    Entry *entry;

    entry = g_new0 (Entry, 1);
    entry->id = g_strdup (id);
    return entry;
}

/* the player's state carries anything worth saving */
static gboolean
entry_has_state (const Entry *entry)
{
    return entry->placed || entry->locked || entry->hidden;
}

static void
emit_changed (LrgHudLayout *self,
              const gchar  *piece_id)
{
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, piece_id);
}

static void
lrg_hud_layout_finalize (GObject *object)
{
    LrgHudLayout *self = LRG_HUD_LAYOUT (object);

    g_hash_table_unref (self->entries);
    g_ptr_array_unref (self->order);
    G_OBJECT_CLASS (lrg_hud_layout_parent_class)->finalize (object);
}

static void
lrg_hud_layout_class_init (LrgHudLayoutClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_hud_layout_finalize;

    signals[SIGNAL_CHANGED] = g_signal_new ("changed",
                                            G_TYPE_FROM_CLASS (klass),
                                            G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL,
                                            G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_hud_layout_init (LrgHudLayout *self)
{
    self->entries = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, entry_free);
    self->order = g_ptr_array_new_with_free_func (g_free);
}

LrgHudLayout *
lrg_hud_layout_new (void)
{
    return g_object_new (LRG_TYPE_HUD_LAYOUT, NULL);
}

/* ids are non-empty and free of whitespace (they are YAML keys) */
static gboolean
valid_id (const gchar *id)
{
    const gchar *p;

    if (id == NULL || *id == '\0')
        return FALSE;
    for (p = id; *p != '\0'; p++)
    {
        if (g_ascii_isspace (*p) || *p == ':' || *p == '#')
            return FALSE;
    }
    return TRUE;
}

static gboolean
valid_anchor (gint anchor)
{
    return anchor >= LRG_HUD_ANCHOR_TOP_LEFT && anchor <= LRG_HUD_ANCHOR_BOTTOM_RIGHT;
}

/* an added piece's entry, or NULL */
static Entry *
added_entry (LrgHudLayout *self,
             const gchar  *piece_id)
{
    Entry *entry;

    if (piece_id == NULL)
        return NULL;
    entry = g_hash_table_lookup (self->entries, piece_id);
    return entry != NULL && entry->added ? entry : NULL;
}

/* forgets an entry that is neither added nor carries state */
static void
drop_if_empty (LrgHudLayout *self,
               Entry        *entry)
{
    if (!entry->added && !entry_has_state (entry))
        g_hash_table_remove (self->entries, entry->id);
}

gboolean
lrg_hud_layout_add_piece (LrgHudLayout *self,
                          const gchar  *piece_id,
                          const gchar  *label,
                          LrgHudAnchor  anchor,
                          gfloat        x,
                          gfloat        y)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    if (!valid_id (piece_id) || !valid_anchor ((gint) anchor) || !isfinite (x) || !isfinite (y))
        return FALSE;
    entry = g_hash_table_lookup (self->entries, piece_id);
    if (entry != NULL && entry->added)
        return FALSE;
    if (entry == NULL)
    {
        entry = entry_new (piece_id);
        g_hash_table_insert (self->entries, entry->id, entry);
    }
    g_free (entry->label);
    entry->label = g_strdup (label != NULL ? label : piece_id);
    entry->added = TRUE;
    entry->default_anchor = anchor;
    entry->default_x = x;
    entry->default_y = y;
    g_ptr_array_add (self->order, g_strdup (piece_id));
    emit_changed (self, piece_id);
    return TRUE;
}

gboolean
lrg_hud_layout_remove_piece (LrgHudLayout *self,
                             const gchar  *piece_id)
{
    Entry *entry;
    guint i;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL)
        return FALSE;
    for (i = 0; i < self->order->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (self->order, i), piece_id) == 0)
        {
            g_ptr_array_remove_index (self->order, i);
            break;
        }
    }
    entry->added = FALSE;
    drop_if_empty (self, entry);
    emit_changed (self, piece_id);
    return TRUE;
}

gboolean
lrg_hud_layout_has_piece (LrgHudLayout *self,
                          const gchar  *piece_id)
{
    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);
    return added_entry (self, piece_id) != NULL;
}

gchar **
lrg_hud_layout_get_pieces (LrgHudLayout *self)
{
    GStrvBuilder *builder;
    guint i;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), NULL);

    builder = g_strv_builder_new ();
    for (i = 0; i < self->order->len; i++)
        g_strv_builder_add (builder, g_ptr_array_index (self->order, i));
    {
        gchar **ids = g_strv_builder_end (builder);

        g_strv_builder_unref (builder);
        return ids;
    }
}

const gchar *
lrg_hud_layout_get_label (LrgHudLayout *self,
                          const gchar  *piece_id)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), NULL);
    entry = added_entry (self, piece_id);
    return entry != NULL ? entry->label : NULL;
}

gboolean
lrg_hud_layout_get_position (LrgHudLayout *self,
                             const gchar  *piece_id,
                             LrgHudAnchor *anchor,
                             gfloat       *x,
                             gfloat       *y)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL)
        return FALSE;
    if (anchor != NULL)
        *anchor = entry->placed ? entry->anchor : entry->default_anchor;
    if (x != NULL)
        *x = entry->placed ? entry->x : entry->default_x;
    if (y != NULL)
        *y = entry->placed ? entry->y : entry->default_y;
    return TRUE;
}

gboolean
lrg_hud_layout_get_default (LrgHudLayout *self,
                            const gchar  *piece_id,
                            LrgHudAnchor *anchor,
                            gfloat       *x,
                            gfloat       *y)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL)
        return FALSE;
    if (anchor != NULL)
        *anchor = entry->default_anchor;
    if (x != NULL)
        *x = entry->default_x;
    if (y != NULL)
        *y = entry->default_y;
    return TRUE;
}

/* the placement itself; the default placement is no placement */
static void
place (Entry        *entry,
       LrgHudAnchor  anchor,
       gfloat        x,
       gfloat        y)
{
    if (anchor == entry->default_anchor && fabsf (x - entry->default_x) < 1e-4f &&
        fabsf (y - entry->default_y) < 1e-4f)
    {
        entry->placed = FALSE;
        return;
    }
    entry->placed = TRUE;
    entry->anchor = anchor;
    entry->x = x;
    entry->y = y;
}

gboolean
lrg_hud_layout_set_position (LrgHudLayout *self,
                             const gchar  *piece_id,
                             LrgHudAnchor  anchor,
                             gfloat        x,
                             gfloat        y)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL || entry->locked || !valid_anchor ((gint) anchor) ||
        !isfinite (x) || !isfinite (y))
        return FALSE;
    place (entry, anchor, x, y);
    emit_changed (self, piece_id);
    return TRUE;
}

gboolean
lrg_hud_layout_move_by (LrgHudLayout *self,
                        const gchar  *piece_id,
                        gfloat        dx,
                        gfloat        dy)
{
    LrgHudAnchor anchor;
    gfloat x;
    gfloat y;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    if (!lrg_hud_layout_get_position (self, piece_id, &anchor, &x, &y))
        return FALSE;
    return lrg_hud_layout_set_position (self, piece_id, anchor, x + dx, y + dy);
}

/* the canvas' anchor point, and the same point of a piece of a size */
static void
anchor_point (LrgHudAnchor  anchor,
              gfloat        width,
              gfloat        height,
              gfloat       *out_x,
              gfloat       *out_y)
{
    *out_x = width * 0.5f * (gfloat) ((guint) anchor % 3);
    *out_y = height * 0.5f * (gfloat) ((guint) anchor / 3);
}

void
lrg_hud_layout_resolve (LrgHudAnchor  anchor,
                        gfloat        x,
                        gfloat        y,
                        gfloat        width,
                        gfloat        height,
                        gfloat        canvas_width,
                        gfloat        canvas_height,
                        gfloat       *out_left,
                        gfloat       *out_top)
{
    gfloat cx;
    gfloat cy;
    gfloat px;
    gfloat py;

    g_return_if_fail (out_left != NULL && out_top != NULL);

    if (!valid_anchor ((gint) anchor))
        anchor = LRG_HUD_ANCHOR_TOP_LEFT;
    anchor_point (anchor, canvas_width, canvas_height, &cx, &cy);
    anchor_point (anchor, width, height, &px, &py);
    *out_left = cx + x - px;
    *out_top = cy + y - py;
}

gboolean
lrg_hud_layout_place_rect (LrgHudLayout *self,
                           const gchar  *piece_id,
                           gfloat        left,
                           gfloat        top,
                           gfloat        width,
                           gfloat        height,
                           gfloat        canvas_width,
                           gfloat        canvas_height)
{
    Entry *entry;
    LrgHudAnchor anchor;
    gfloat cx;
    gfloat cy;
    gfloat px;
    gfloat py;
    guint column;
    guint row;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL || entry->locked || !(canvas_width > 0.0f) || !(canvas_height > 0.0f) ||
        !isfinite (left) || !isfinite (top) || !isfinite (width) || !isfinite (height))
        return FALSE;

    /* --- keep the piece on the canvas --- */
    width = CLAMP (width, 0.0f, canvas_width);
    height = CLAMP (height, 0.0f, canvas_height);
    left = CLAMP (left, 0.0f, canvas_width - width);
    top = CLAMP (top, 0.0f, canvas_height - height);

    /* --- the third the centre lies in names the anchor --- */
    column = (guint) CLAMP ((gint) floorf ((left + width * 0.5f) / canvas_width * 3.0f), 0, 2);
    row = (guint) CLAMP ((gint) floorf ((top + height * 0.5f) / canvas_height * 3.0f), 0, 2);
    anchor = (LrgHudAnchor) (row * 3 + column);

    /* --- the offset that puts the piece there --- */
    anchor_point (anchor, canvas_width, canvas_height, &cx, &cy);
    anchor_point (anchor, width, height, &px, &py);
    place (entry, anchor, left + px - cx, top + py - cy);
    emit_changed (self, piece_id);
    return TRUE;
}

gboolean
lrg_hud_layout_is_modified (LrgHudLayout *self,
                            const gchar  *piece_id)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);
    entry = added_entry (self, piece_id);
    return entry != NULL && entry->placed;
}

gboolean
lrg_hud_layout_set_locked (LrgHudLayout *self,
                           const gchar  *piece_id,
                           gboolean      locked)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL)
        return FALSE;
    locked = locked != FALSE;
    if (entry->locked != locked)
    {
        entry->locked = locked;
        emit_changed (self, piece_id);
    }
    return TRUE;
}

gboolean
lrg_hud_layout_get_locked (LrgHudLayout *self,
                           const gchar  *piece_id)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);
    entry = added_entry (self, piece_id);
    return entry != NULL && entry->locked;
}

gboolean
lrg_hud_layout_set_hidden (LrgHudLayout *self,
                           const gchar  *piece_id,
                           gboolean      hidden)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL)
        return FALSE;
    hidden = hidden != FALSE;
    if (entry->hidden != hidden)
    {
        entry->hidden = hidden;
        emit_changed (self, piece_id);
    }
    return TRUE;
}

gboolean
lrg_hud_layout_get_hidden (LrgHudLayout *self,
                           const gchar  *piece_id)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);
    entry = added_entry (self, piece_id);
    return entry != NULL && entry->hidden;
}

gboolean
lrg_hud_layout_reset (LrgHudLayout *self,
                      const gchar  *piece_id)
{
    Entry *entry;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);

    entry = added_entry (self, piece_id);
    if (entry == NULL)
        return FALSE;
    entry->placed = FALSE;
    entry->locked = FALSE;
    entry->hidden = FALSE;
    emit_changed (self, piece_id);
    return TRUE;
}

/* drops every entry's state; entries of pieces not added go */
static void
clear_state (LrgHudLayout *self)
{
    GHashTableIter iter;
    gpointer value;

    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Entry *entry = value;

        entry->placed = FALSE;
        entry->locked = FALSE;
        entry->hidden = FALSE;
        if (!entry->added)
            g_hash_table_iter_remove (&iter);
    }
}

void
lrg_hud_layout_reset_all (LrgHudLayout *self)
{
    g_return_if_fail (LRG_IS_HUD_LAYOUT (self));

    clear_state (self);
    emit_changed (self, NULL);
}

/* --- state: variant --- */

/* the ids with state, sorted (stable files) */
static GList *
state_ids (LrgHudLayout *self)
{
    GHashTableIter iter;
    gpointer value;
    GList *ids;

    ids = NULL;
    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Entry *entry = value;

        if (entry_has_state (entry))
            ids = g_list_prepend (ids, entry->id);
    }
    return g_list_sort (ids, (GCompareFunc) g_strcmp0);
}

static const gchar *
anchor_nick (LrgHudAnchor anchor)
{
    GEnumClass *klass;
    GEnumValue *value;

    klass = g_type_class_ref (LRG_TYPE_HUD_ANCHOR);
    value = g_enum_get_value (klass, (gint) anchor);
    g_type_class_unref (klass);
    return value != NULL ? value->value_nick : "top-left";
}

static gboolean
anchor_from_nick (const gchar  *nick,
                  LrgHudAnchor *out)
{
    GEnumClass *klass;
    GEnumValue *value;

    if (nick == NULL)
        return FALSE;
    klass = g_type_class_ref (LRG_TYPE_HUD_ANCHOR);
    value = g_enum_get_value_by_nick (klass, nick);
    if (value != NULL)
        *out = (LrgHudAnchor) value->value;
    g_type_class_unref (klass);
    return value != NULL;
}

GVariant *
lrg_hud_layout_to_variant (LrgHudLayout *self)
{
    GVariantBuilder builder;
    GList *ids;
    GList *l;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{s(bsddbb)}"));
    ids = state_ids (self);
    for (l = ids; l != NULL; l = l->next)
    {
        Entry *entry = g_hash_table_lookup (self->entries, l->data);

        g_variant_builder_add (&builder, "{s(bsddbb)}", entry->id, entry->placed,
                               anchor_nick (entry->placed ? entry->anchor : LRG_HUD_ANCHOR_TOP_LEFT),
                               (gdouble) (entry->placed ? entry->x : 0.0f),
                               (gdouble) (entry->placed ? entry->y : 0.0f),
                               entry->locked, entry->hidden);
    }
    g_list_free (ids);
    return g_variant_builder_end (&builder);
}

/* one staged entry of a load */
typedef struct
{
    gchar        *id;
    gboolean      placed;
    LrgHudAnchor  anchor;
    gfloat        x;
    gfloat        y;
    gboolean      locked;
    gboolean      hidden;
} Staged;

static void
staged_free (gpointer data)
{
    Staged *staged = data;

    g_free (staged->id);
    g_free (staged);
}

/* replaces the state with the staged entries (all valid) */
static void
apply_staged (LrgHudLayout *self,
              GPtrArray    *staged)
{
    guint i;

    clear_state (self);
    for (i = 0; i < staged->len; i++)
    {
        Staged *s = g_ptr_array_index (staged, i);
        Entry *entry;

        entry = g_hash_table_lookup (self->entries, s->id);
        if (entry == NULL)
        {
            entry = entry_new (s->id);
            g_hash_table_insert (self->entries, entry->id, entry);
        }
        entry->placed = s->placed;
        entry->anchor = s->anchor;
        entry->x = s->x;
        entry->y = s->y;
        entry->locked = s->locked;
        entry->hidden = s->hidden;
        drop_if_empty (self, entry);
    }
    emit_changed (self, NULL);
}

static gboolean
stage (GPtrArray    *staged,
       const gchar  *id,
       gboolean      placed,
       const gchar  *anchor,
       gdouble       x,
       gdouble       y,
       gboolean      locked,
       gboolean      hidden,
       const gchar  *origin,
       GError      **error)
{
    Staged *s;
    guint i;

    if (!valid_id (id))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: invalid piece id '%s'",
                     origin, id != NULL ? id : "");
        return FALSE;
    }
    for (i = 0; i < staged->len; i++)
    {
        if (g_strcmp0 (((Staged *) g_ptr_array_index (staged, i))->id, id) == 0)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: piece '%s' twice",
                         origin, id);
            return FALSE;
        }
    }
    s = g_new0 (Staged, 1);
    s->id = g_strdup (id);
    s->placed = placed;
    s->locked = locked;
    s->hidden = hidden;
    if (placed)
    {
        if (!anchor_from_nick (anchor, &s->anchor))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "%s: piece '%s': unknown anchor '%s'", origin, id,
                         anchor != NULL ? anchor : "");
            staged_free (s);
            return FALSE;
        }
        if (!isfinite (x) || !isfinite (y))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "%s: piece '%s': the offset is not a number", origin, id);
            staged_free (s);
            return FALSE;
        }
        s->x = (gfloat) x;
        s->y = (gfloat) y;
    }
    g_ptr_array_add (staged, s);
    return TRUE;
}

gboolean
lrg_hud_layout_load_variant (LrgHudLayout  *self,
                             GVariant      *state,
                             GError       **error)
{
    g_autoptr(GPtrArray) staged = NULL;
    GVariantIter iter;
    const gchar *id;
    const gchar *anchor;
    gboolean placed;
    gboolean locked;
    gboolean hidden;
    gdouble x;
    gdouble y;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);
    g_return_val_if_fail (state != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!g_variant_is_of_type (state, G_VARIANT_TYPE ("a{s(bsddbb)}")))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "HUD layout state of type %s, a{s(bsddbb)} expected",
                     g_variant_get_type_string (state));
        return FALSE;
    }
    staged = g_ptr_array_new_with_free_func (staged_free);
    g_variant_iter_init (&iter, state);
    while (g_variant_iter_next (&iter, "{&s(b&sddbb)}", &id, &placed, &anchor, &x, &y, &locked,
                                &hidden))
    {
        if (!stage (staged, id, placed, anchor, x, y, locked, hidden, "HUD layout", error))
            return FALSE;
    }
    apply_staged (self, staged);
    return TRUE;
}

/* --- state: YAML file --- */

/* a scalar member parsed as a number; absent gives @fallback */
static gboolean
number_member (YamlMapping  *mapping,
               const gchar  *key,
               gdouble       fallback,
               gdouble      *out)
{
    YamlNode *node;
    const gchar *text;
    gchar *end;

    if (!yaml_mapping_has_member (mapping, key))
    {
        *out = fallback;
        return TRUE;
    }
    node = yaml_mapping_get_member (mapping, key);
    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
        return FALSE;
    text = yaml_node_get_scalar (node);
    if (text == NULL || *text == '\0')
        return FALSE;
    *out = g_ascii_strtod (text, &end);
    return end != NULL && *end == '\0' && isfinite (*out);
}

/* a scalar member parsed as a boolean; absent is FALSE */
static gboolean
bool_member (YamlMapping *mapping,
             const gchar *key,
             gboolean    *out)
{
    YamlNode *node;
    const gchar *text;

    *out = FALSE;
    if (!yaml_mapping_has_member (mapping, key))
        return TRUE;
    node = yaml_mapping_get_member (mapping, key);
    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
        return FALSE;
    text = yaml_node_get_scalar (node);
    if (g_strcmp0 (text, "true") == 0 || g_strcmp0 (text, "yes") == 0)
        *out = TRUE;
    else if (g_strcmp0 (text, "false") != 0 && g_strcmp0 (text, "no") != 0)
        return FALSE;
    return TRUE;
}

static const gchar * const piece_keys[] = { "anchor", "x", "y", "locked", "hidden", NULL };

/* stages one `pieces:` member */
static gboolean
stage_yaml_piece (GPtrArray    *staged,
                  const gchar  *id,
                  YamlNode     *node,
                  const gchar  *path,
                  GError      **error)
{
    YamlMapping *piece;
    GList *keys;
    GList *l;
    const gchar *anchor;
    gboolean placed;
    gboolean locked;
    gboolean hidden;
    gdouble x;
    gdouble y;

    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: piece '%s' is not a mapping",
                     path, id);
        return FALSE;
    }
    piece = yaml_node_get_mapping (node);
    keys = yaml_mapping_get_members (piece);
    for (l = keys; l != NULL; l = l->next)
    {
        if (!g_strv_contains (piece_keys, l->data))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "%s: piece '%s': unknown key '%s'", path, id, (const gchar *) l->data);
            g_list_free (keys);
            return FALSE;
        }
    }
    g_list_free (keys);

    placed = yaml_mapping_has_member (piece, "anchor");
    anchor = placed ? yaml_mapping_get_string_member (piece, "anchor") : NULL;
    if (!number_member (piece, "x", 0.0, &x) || !number_member (piece, "y", 0.0, &y) ||
        ((yaml_mapping_has_member (piece, "x") || yaml_mapping_has_member (piece, "y")) && !placed))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "%s: piece '%s': x and y must be numbers beside an anchor", path, id);
        return FALSE;
    }
    if (!bool_member (piece, "locked", &locked) || !bool_member (piece, "hidden", &hidden))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "%s: piece '%s': locked and hidden must be true or false", path, id);
        return FALSE;
    }
    return stage (staged, id, placed, anchor, x, y, locked, hidden, path, error);
}

gboolean
lrg_hud_layout_load_from_file (LrgHudLayout  *self,
                               const gchar   *path,
                               GError       **error)
{
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(GPtrArray) staged = NULL;
    g_autoptr(GError) local = NULL;
    g_autofree gchar *text = NULL;
    YamlMapping *root;
    YamlMapping *pieces;
    YamlNode *node;
    GList *keys;
    GList *l;
    gsize length;
    gdouble version;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    staged = g_ptr_array_new_with_free_func (staged_free);
    if (!g_file_get_contents (path, &text, &length, &local))
    {
        if (g_error_matches (local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
            apply_staged (self, staged);
            return TRUE;
        }
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s", path, local->message);
        return FALSE;
    }

    /* --- the document --- */
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_data (parser, text, (gssize) length, &local))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: %s", path, local->message);
        return FALSE;
    }
    node = yaml_parser_get_root (parser);
    if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
    {
        apply_staged (self, staged);
        return TRUE;
    }
    if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: not a mapping", path);
        return FALSE;
    }
    root = yaml_node_get_mapping (node);
    if (!number_member (root, "version", 1.0, &version) || version < 1.0)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: bad version", path);
        return FALSE;
    }
    if (version > (gdouble) LRG_HUD_LAYOUT_FORMAT_VERSION)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                     "%s: HUD layout version %.0f is newer than %d", path, version,
                     LRG_HUD_LAYOUT_FORMAT_VERSION);
        return FALSE;
    }
    keys = yaml_mapping_get_members (root);
    for (l = keys; l != NULL; l = l->next)
    {
        if (g_strcmp0 (l->data, "version") != 0 && g_strcmp0 (l->data, "pieces") != 0)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: unknown key '%s'", path,
                         (const gchar *) l->data);
            g_list_free (keys);
            return FALSE;
        }
    }
    g_list_free (keys);

    /* --- the pieces, staged --- */
    node = yaml_mapping_has_member (root, "pieces") ? yaml_mapping_get_member (root, "pieces") : NULL;
    if (node != NULL && yaml_node_get_node_type (node) != YAML_NODE_NULL)
    {
        if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "%s: pieces is not a mapping", path);
            return FALSE;
        }
        pieces = yaml_node_get_mapping (node);
        keys = yaml_mapping_get_members (pieces);
        for (l = keys; l != NULL; l = l->next)
        {
            if (!stage_yaml_piece (staged, l->data, yaml_mapping_get_member (pieces, l->data), path,
                                   error))
            {
                g_list_free (keys);
                return FALSE;
            }
        }
        g_list_free (keys);
    }
    apply_staged (self, staged);
    return TRUE;
}

gboolean
lrg_hud_layout_save_to_file (LrgHudLayout  *self,
                             const gchar   *path,
                             GError       **error)
{
    g_autoptr(YamlGenerator) generator = NULL;
    g_autoptr(YamlNode) root_node = NULL;
    g_autofree gchar *data = NULL;
    g_autofree gchar *dir = NULL;
    YamlMapping *root;
    YamlMapping *pieces;
    GList *ids;
    GList *l;
    gsize length;

    g_return_val_if_fail (LRG_IS_HUD_LAYOUT (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    pieces = yaml_mapping_new ();
    ids = state_ids (self);
    for (l = ids; l != NULL; l = l->next)
    {
        Entry *entry = g_hash_table_lookup (self->entries, l->data);
        YamlMapping *piece;

        piece = yaml_mapping_new ();
        if (entry->placed)
        {
            yaml_mapping_set_string_member (piece, "anchor", anchor_nick (entry->anchor));
            yaml_mapping_set_double_member (piece, "x", (gdouble) entry->x);
            yaml_mapping_set_double_member (piece, "y", (gdouble) entry->y);
        }
        if (entry->locked)
            yaml_mapping_set_boolean_member (piece, "locked", TRUE);
        if (entry->hidden)
            yaml_mapping_set_boolean_member (piece, "hidden", TRUE);
        yaml_mapping_set_mapping_member (pieces, entry->id, piece);
        yaml_mapping_unref (piece);
    }
    g_list_free (ids);

    root = yaml_mapping_new ();
    yaml_mapping_set_int_member (root, "version", LRG_HUD_LAYOUT_FORMAT_VERSION);
    yaml_mapping_set_mapping_member (root, "pieces", pieces);
    yaml_mapping_unref (pieces);
    root_node = yaml_node_new_mapping (root);
    yaml_mapping_unref (root);

    generator = yaml_generator_new ();
    yaml_generator_set_root (generator, root_node);
    data = yaml_generator_to_data (generator, &length, error);
    if (data == NULL)
        return FALSE;
    dir = g_path_get_dirname (path);
    if (g_mkdir_with_parents (dir, 0700) != 0)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "cannot create %s: %s", dir,
                     g_strerror (errno));
        return FALSE;
    }
    return g_file_set_contents (path, data, (gssize) length, error);
}
