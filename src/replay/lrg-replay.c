/* lrg-replay.c - Recorded per-tick input with a versioned file format
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * File layout (format version 1, body encoding "delta-v1"):
 *
 *   %YAML 1.2
 *   ---
 *   format: lrg-replay
 *   version: 1
 *   ...header keys (see docs/modules/replay/replay-format.org)...
 *   ...
 *   <binary body, body-size bytes>
 *
 * The header is a complete YAML document ended by the "..." marker, so YAML
 * tools can read it with the body cut off. The body is, per tick, one flags
 * byte (bit 0: a checksum follows the player records) and one record per
 * player: a flags byte (0: same input as the previous tick; bit 0: only the
 * held mask changed, a u64 follows; bit 1: a full snapshot follows), then
 * the u64 checksum when flagged. All integers are little-endian.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_REPLAY

#include "lrg-replay.h"
#include "../lrg-log.h"

#include <yaml-glib.h>
#include <gio/gio.h>
#include <errno.h>
#include <string.h>

#define LRG_REPLAY_FORMAT_NAME    "lrg-replay"
#define LRG_REPLAY_BODY_ENCODING  "delta-v1"
#define LRG_GOLDEN_FORMAT_NAME    "lrg-golden"
#define LRG_GOLDEN_FORMAT_VERSION (1)

/* Header terminator: the YAML document end marker on its own line */
#define LRG_REPLAY_HEADER_END     "\n...\n"

/* Body flag bits */
#define TICK_FLAG_CHECKSUM   (1u << 0)
#define INPUT_FLAG_DOWN      (1u << 0)
#define INPUT_FLAG_FULL      (1u << 1)

struct _LrgReplay
{
    GObject      parent_instance;

    guint64      seed;
    gdouble      fixed_step;
    guint        n_players;
    GStrv        actions;
    guint64      start_tick;
    gchar       *initial_state;
    gboolean     has_initial_checksum;
    guint64      initial_checksum;
    GHashTable  *metadata;      /* gchar* -> gchar* */

    GPtrArray   *inputs;        /* LrgInputSnapshot*, tick-major */
    GArray      *checksums;     /* guint64 per tick */
    GArray      *has_checksum;  /* guint8 per tick */
};

G_DEFINE_FINAL_TYPE (LrgReplay, lrg_replay, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-replay-error-quark, lrg_replay_error)

GType
lrg_replay_error_get_type (void)
{
    static gsize g_type_id = 0;

    if (g_once_init_enter (&g_type_id))
    {
        static const GEnumValue values[] = {
            { LRG_REPLAY_ERROR_FAILED, "LRG_REPLAY_ERROR_FAILED", "failed" },
            { LRG_REPLAY_ERROR_PARSE, "LRG_REPLAY_ERROR_PARSE", "parse" },
            { LRG_REPLAY_ERROR_VERSION, "LRG_REPLAY_ERROR_VERSION", "version" },
            { LRG_REPLAY_ERROR_INCOMPATIBLE, "LRG_REPLAY_ERROR_INCOMPATIBLE", "incompatible" },
            { LRG_REPLAY_ERROR_INITIAL_STATE, "LRG_REPLAY_ERROR_INITIAL_STATE", "initial-state" },
            { LRG_REPLAY_ERROR_DESYNC, "LRG_REPLAY_ERROR_DESYNC", "desync" },
            { LRG_REPLAY_ERROR_GOLDEN_MISMATCH, "LRG_REPLAY_ERROR_GOLDEN_MISMATCH", "golden-mismatch" },
            { LRG_REPLAY_ERROR_NOT_FOUND, "LRG_REPLAY_ERROR_NOT_FOUND", "not-found" },
            { 0, NULL, NULL }
        };
        GType type_id;

        type_id = g_enum_register_static (g_intern_static_string ("LrgReplayError"), values);
        g_once_init_leave (&g_type_id, type_id);
    }
    return g_type_id;
}

static void
lrg_replay_finalize (GObject *object)
{
    LrgReplay *self = LRG_REPLAY (object);

    g_clear_pointer (&self->actions, g_strfreev);
    g_clear_pointer (&self->initial_state, g_free);
    g_clear_pointer (&self->metadata, g_hash_table_unref);
    g_clear_pointer (&self->inputs, g_ptr_array_unref);
    g_clear_pointer (&self->checksums, g_array_unref);
    g_clear_pointer (&self->has_checksum, g_array_unref);

    G_OBJECT_CLASS (lrg_replay_parent_class)->finalize (object);
}

static void
lrg_replay_class_init (LrgReplayClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_replay_finalize;
}

static void
lrg_replay_init (LrgReplay *self)
{
    self->fixed_step = 1.0 / 60.0;
    self->n_players = 1;
    self->actions = g_new0 (gchar *, 1);
    self->metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    self->inputs = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_input_snapshot_free);
    self->checksums = g_array_new (FALSE, TRUE, sizeof (guint64));
    self->has_checksum = g_array_new (FALSE, TRUE, sizeof (guint8));
}

LrgReplay *
lrg_replay_new (guint64             seed,
                gdouble             fixed_step,
                guint               n_players,
                const gchar *const *actions)
{
    LrgReplay *self;

    g_return_val_if_fail (fixed_step > 0.0, NULL);
    g_return_val_if_fail (n_players >= 1 && n_players <= LRG_SIMULATION_MAX_PLAYERS, NULL);
    g_return_val_if_fail (actions != NULL, NULL);

    self = g_object_new (LRG_TYPE_REPLAY, NULL);
    self->seed = seed;
    self->fixed_step = fixed_step;
    self->n_players = n_players;
    g_strfreev (self->actions);
    self->actions = g_strdupv ((gchar **) actions);
    return self;
}

LrgReplay *
lrg_replay_new_for_simulation (LrgSimulation *simulation,
                               const gchar   *initial_state)
{
    LrgReplay *self;

    g_return_val_if_fail (LRG_IS_SIMULATION (simulation), NULL);

    self = lrg_replay_new (lrg_simulation_get_seed (simulation),
                           lrg_simulation_get_fixed_step (simulation),
                           lrg_simulation_get_n_players (simulation),
                           lrg_simulation_get_actions (simulation));
    self->start_tick = lrg_simulation_get_tick (simulation);
    self->initial_state = g_strdup (initial_state);
    self->has_initial_checksum = TRUE;
    self->initial_checksum = lrg_simulation_checksum (simulation);
    return self;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

guint64
lrg_replay_get_seed (LrgReplay *self)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), 0);
    return self->seed;
}

gdouble
lrg_replay_get_fixed_step (LrgReplay *self)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), 0.0);
    return self->fixed_step;
}

guint
lrg_replay_get_n_players (LrgReplay *self)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), 0);
    return self->n_players;
}

const gchar * const *
lrg_replay_get_actions (LrgReplay *self)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), NULL);
    return (const gchar * const *) self->actions;
}

guint64
lrg_replay_get_start_tick (LrgReplay *self)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), 0);
    return self->start_tick;
}

guint
lrg_replay_get_n_ticks (LrgReplay *self)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), 0);
    return self->checksums->len;
}

const gchar *
lrg_replay_get_initial_state (LrgReplay *self)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), NULL);
    return self->initial_state;
}

void
lrg_replay_set_initial_state (LrgReplay   *self,
                              const gchar *initial_state)
{
    g_return_if_fail (LRG_IS_REPLAY (self));

    g_free (self->initial_state);
    self->initial_state = g_strdup (initial_state);
}

gboolean
lrg_replay_get_initial_checksum (LrgReplay *self,
                                 guint64   *checksum)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), FALSE);

    if (self->has_initial_checksum && checksum != NULL)
        *checksum = self->initial_checksum;
    return self->has_initial_checksum;
}

void
lrg_replay_set_initial_checksum (LrgReplay *self,
                                 guint64    checksum)
{
    g_return_if_fail (LRG_IS_REPLAY (self));

    self->has_initial_checksum = TRUE;
    self->initial_checksum = checksum;
}

const gchar *
lrg_replay_get_metadata (LrgReplay   *self,
                         const gchar *key)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->metadata, key);
}

static gboolean
metadata_key_valid (const gchar *key)
{
    const gchar *p;

    if (*key == '\0')
        return FALSE;
    for (p = key; *p != '\0'; p++)
    {
        if (!g_ascii_isalnum (*p) && *p != '-' && *p != '_')
            return FALSE;
    }
    return TRUE;
}

void
lrg_replay_set_metadata (LrgReplay   *self,
                         const gchar *key,
                         const gchar *value)
{
    g_return_if_fail (LRG_IS_REPLAY (self));
    g_return_if_fail (key != NULL && metadata_key_valid (key));

    if (value == NULL)
        g_hash_table_remove (self->metadata, key);
    else
        g_hash_table_insert (self->metadata, g_strdup (key), g_strdup (value));
}

void
lrg_replay_append_tick (LrgReplay               *self,
                        LrgInputSnapshot *const *inputs,
                        guint                    n_inputs,
                        gboolean                 has_checksum,
                        guint64                  checksum)
{
    guint8 flag;
    guint i;

    g_return_if_fail (LRG_IS_REPLAY (self));
    g_return_if_fail (inputs != NULL);
    g_return_if_fail (n_inputs == self->n_players);

    for (i = 0; i < n_inputs; i++)
        g_ptr_array_add (self->inputs, lrg_input_snapshot_copy (inputs[i]));

    flag = has_checksum ? 1 : 0;
    if (!has_checksum)
        checksum = 0;
    g_array_append_val (self->checksums, checksum);
    g_array_append_val (self->has_checksum, flag);
}

const LrgInputSnapshot *
lrg_replay_get_input (LrgReplay *self,
                      guint      index,
                      guint      player)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), NULL);

    if (index >= self->checksums->len || player >= self->n_players)
        return NULL;
    return g_ptr_array_index (self->inputs, index * self->n_players + player);
}

gboolean
lrg_replay_get_checksum (LrgReplay *self,
                         guint      index,
                         guint64   *checksum)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), FALSE);

    if (index >= self->checksums->len ||
        g_array_index (self->has_checksum, guint8, index) == 0)
        return FALSE;
    if (checksum != NULL)
        *checksum = g_array_index (self->checksums, guint64, index);
    return TRUE;
}

/* ==========================================================================
 * Encoding
 * ========================================================================== */

/*
 * append_yaml_string:
 *
 * Appends @value as a YAML double-quoted scalar. Quotes, backslashes and
 * control characters are escaped (\xNN); UTF-8 passes through.
 */
static void
append_yaml_string (GString     *out,
                    const gchar *value)
{
    const guchar *p;

    g_string_append_c (out, '"');
    for (p = (const guchar *) value; *p != '\0'; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            g_string_append_c (out, '\\');
            g_string_append_c (out, (gchar) *p);
        }
        else if (*p < 0x20 || *p == 0x7f)
            g_string_append_printf (out, "\\x%02x", *p);
        else
            g_string_append_c (out, (gchar) *p);
    }
    g_string_append_c (out, '"');
}

/*
 * encode_body:
 *
 * Delta-encodes the ticks: each player record is relative to the same
 * player's input on the previous tick (nothing held before the first).
 */
static void
encode_body (LrgReplay      *self,
             LrgStateBuffer *body)
{
    g_autoptr(LrgInputSnapshot) empty = NULL;
    g_autoptr(LrgInputSnapshot) down_only = NULL;
    guint n_ticks;
    guint t;
    guint p;

    empty = lrg_input_snapshot_new ();
    down_only = lrg_input_snapshot_new ();
    n_ticks = self->checksums->len;

    for (t = 0; t < n_ticks; t++)
    {
        gboolean has_checksum;

        has_checksum = g_array_index (self->has_checksum, guint8, t) != 0;
        lrg_state_buffer_write_uint8 (body, has_checksum ? TICK_FLAG_CHECKSUM : 0);

        for (p = 0; p < self->n_players; p++)
        {
            const LrgInputSnapshot *cur;
            const LrgInputSnapshot *prev;

            cur = g_ptr_array_index (self->inputs, t * self->n_players + p);
            prev = t > 0 ? g_ptr_array_index (self->inputs, (t - 1) * self->n_players + p)
                         : empty;

            if (lrg_input_snapshot_equal (cur, prev))
            {
                lrg_state_buffer_write_uint8 (body, 0);
                continue;
            }

            /* Only the held bits changed (the common digital case)? */
            lrg_input_snapshot_assign (down_only, prev);
            lrg_input_snapshot_set_down_mask (down_only, lrg_input_snapshot_get_down_mask (cur));
            if (lrg_input_snapshot_equal (cur, down_only))
            {
                lrg_state_buffer_write_uint8 (body, INPUT_FLAG_DOWN);
                lrg_state_buffer_write_uint64 (body, lrg_input_snapshot_get_down_mask (cur));
            }
            else
            {
                lrg_state_buffer_write_uint8 (body, INPUT_FLAG_FULL);
                lrg_input_snapshot_write (cur, body);
            }
        }

        if (has_checksum)
            lrg_state_buffer_write_uint64 (body, g_array_index (self->checksums, guint64, t));
    }
}

GBytes *
lrg_replay_to_bytes (LrgReplay *self)
{
    g_autoptr(LrgStateBuffer) body = NULL;
    g_autoptr(GList) keys = NULL;
    GString *out;
    gchar step[G_ASCII_DTOSTR_BUF_SIZE];
    const guint8 *body_data;
    gsize body_size;
    GList *l;
    guint i;

    g_return_val_if_fail (LRG_IS_REPLAY (self), NULL);

    body = lrg_state_buffer_new ();
    encode_body (self, body);
    body_data = lrg_state_buffer_get_data (body, &body_size);

    /* Header: a self-contained YAML document */
    out = g_string_new ("%YAML 1.2\n---\n");
    g_string_append (out, "format: " LRG_REPLAY_FORMAT_NAME "\n");
    g_string_append_printf (out, "version: %d\n", LRG_REPLAY_FORMAT_VERSION);
    g_string_append (out, "body-encoding: " LRG_REPLAY_BODY_ENCODING "\n");
    g_string_append (out, "engine-version: ");
    append_yaml_string (out, LRG_VERSION_STRING);
    g_string_append_printf (out, "\nseed: %" G_GUINT64_FORMAT "\n", self->seed);
    /* g_ascii_dtostr() gives the shortest string that reads back exactly */
    g_string_append_printf (out, "fixed-step: %s\n",
                            g_ascii_dtostr (step, sizeof (step), self->fixed_step));
    g_string_append_printf (out, "players: %u\n", self->n_players);
    g_string_append_printf (out, "start-tick: %" G_GUINT64_FORMAT "\n", self->start_tick);
    g_string_append_printf (out, "ticks: %u\n", self->checksums->len);
    if (self->initial_state != NULL)
    {
        g_string_append (out, "initial-state: ");
        append_yaml_string (out, self->initial_state);
        g_string_append_c (out, '\n');
    }
    if (self->has_initial_checksum)
        g_string_append_printf (out, "initial-checksum: 0x%016" G_GINT64_MODIFIER "x\n",
                                self->initial_checksum);

    g_string_append (out, "actions:");
    if (self->actions[0] == NULL)
        g_string_append (out, " []");
    g_string_append_c (out, '\n');
    for (i = 0; self->actions[i] != NULL; i++)
    {
        g_string_append (out, "  - ");
        append_yaml_string (out, self->actions[i]);
        g_string_append_c (out, '\n');
    }

    /* Metadata in sorted key order, so equal replays give equal files */
    if (g_hash_table_size (self->metadata) > 0)
    {
        g_string_append (out, "metadata:\n");
        keys = g_list_sort (g_hash_table_get_keys (self->metadata), (GCompareFunc) strcmp);
        for (l = keys; l != NULL; l = l->next)
        {
            g_string_append_printf (out, "  %s: ", (const gchar *) l->data);
            append_yaml_string (out, g_hash_table_lookup (self->metadata, l->data));
            g_string_append_c (out, '\n');
        }
    }

    g_string_append_printf (out, "body-size: %" G_GSIZE_FORMAT "\n", body_size);
    g_string_append_printf (out, "body-checksum: 0x%016" G_GINT64_MODIFIER "x",
                            lrg_state_hash_data (body_data, body_size, 0));
    g_string_append (out, LRG_REPLAY_HEADER_END);

    /* Binary body */
    g_string_append_len (out, (const gchar *) body_data, (gssize) body_size);

    return g_string_free_to_bytes (out);
}

gboolean
lrg_replay_save_to_file (LrgReplay    *self,
                         const gchar  *path,
                         GError      **error)
{
    g_autoptr(GBytes) bytes = NULL;
    gconstpointer data;
    gsize size;

    g_return_val_if_fail (LRG_IS_REPLAY (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    bytes = lrg_replay_to_bytes (self);
    data = g_bytes_get_data (bytes, &size);
    return g_file_set_contents (path, data, (gssize) size, error);
}

/* ==========================================================================
 * Decoding
 * ========================================================================== */

static const gchar *
scalar_member (YamlMapping *map,
               const gchar *key)
{
    YamlNode *node;

    if (!yaml_mapping_has_member (map, key))
        return NULL;
    node = yaml_mapping_get_member (map, key);
    if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
        return NULL;
    return yaml_node_get_scalar (node);
}

/*
 * parse_uint_member:
 *
 * Reads an unsigned integer scalar (decimal or 0x-hex) exactly; YAML int
 * accessors go through gint64, which would mangle seeds and checksums at
 * or above 2^63.
 */
static gboolean
parse_uint_member (YamlMapping  *map,
                   const gchar  *key,
                   gboolean      required,
                   guint64      *out,
                   GError      **error)
{
    const gchar *text;
    gchar *end;
    guint64 value;

    text = scalar_member (map, key);
    if (text == NULL)
    {
        if (!required)
            return TRUE;
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                     "Missing header key '%s'", key);
        return FALSE;
    }

    errno = 0;
    value = g_ascii_strtoull (text, &end, 0);
    if (errno != 0 || end == text || *end != '\0')
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                     "Header key '%s' is not an unsigned integer: '%s'", key, text);
        return FALSE;
    }
    *out = value;
    return TRUE;
}

static gboolean
decode_body (LrgReplay       *self,
             LrgStateBuffer  *body,
             guint            n_ticks,
             GError         **error)
{
    g_autoptr(LrgInputSnapshot) empty = NULL;
    guint t;
    guint p;

    empty = lrg_input_snapshot_new ();
    for (t = 0; t < n_ticks; t++)
    {
        guint8 tick_flags;
        guint64 checksum = 0;
        guint8 has_checksum;

        tick_flags = lrg_state_buffer_read_uint8 (body);
        for (p = 0; p < self->n_players; p++)
        {
            const LrgInputSnapshot *prev;
            LrgInputSnapshot *cur;
            guint8 flags;

            prev = t > 0 ? g_ptr_array_index (self->inputs, (t - 1) * self->n_players + p)
                         : empty;
            cur = lrg_input_snapshot_copy (prev);
            g_ptr_array_add (self->inputs, cur);

            flags = lrg_state_buffer_read_uint8 (body);
            if (flags == INPUT_FLAG_DOWN)
                lrg_input_snapshot_set_down_mask (cur, lrg_state_buffer_read_uint64 (body));
            else if (flags == INPUT_FLAG_FULL)
                lrg_input_snapshot_read (cur, body);
            else if (flags != 0)
            {
                g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                             "Bad input record flags 0x%02x at tick %u", flags, t);
                return FALSE;
            }
        }

        has_checksum = (tick_flags & TICK_FLAG_CHECKSUM) != 0 ? 1 : 0;
        if (has_checksum)
            checksum = lrg_state_buffer_read_uint64 (body);
        g_array_append_val (self->checksums, checksum);
        g_array_append_val (self->has_checksum, has_checksum);

        if (!lrg_state_buffer_check (body, NULL))
        {
            g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                         "Replay body ends inside tick %u of %u", t, n_ticks);
            return FALSE;
        }
    }

    if (lrg_state_buffer_get_remaining (body) != 0)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                     "%" G_GSIZE_FORMAT " trailing bytes after the replay body",
                     lrg_state_buffer_get_remaining (body));
        return FALSE;
    }
    return TRUE;
}

LrgReplay *
lrg_replay_load_from_bytes (GBytes  *bytes,
                            GError **error)
{
    g_autoptr(LrgReplay) self = NULL;
    g_autoptr(YamlParser) parser = NULL;
    g_autoptr(LrgStateBuffer) body = NULL;
    YamlDocument *document;
    YamlNode *root;
    YamlMapping *map;
    YamlSequence *actions;
    YamlMapping *metadata;
    const gchar *data;
    const gchar *header_end;
    const gchar *text;
    gsize size;
    gsize header_size;
    guint64 value;
    guint64 body_size;
    guint64 body_checksum;
    guint64 n_ticks;
    guint i;

    g_return_val_if_fail (bytes != NULL, NULL);

    /* 1. Split header and body at the document end marker */
    data = g_bytes_get_data (bytes, &size);
    header_end = data != NULL ? g_strstr_len (data, (gssize) size, LRG_REPLAY_HEADER_END) : NULL;
    if (header_end == NULL)
    {
        g_set_error_literal (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                             "Not a replay file (no YAML header)");
        return NULL;
    }
    header_size = (gsize) (header_end - data) + 1;

    /* 2. Parse the header document */
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_data (parser, data, (gssize) header_size, error))
        return NULL;
    document = yaml_parser_get_document (parser, 0);
    root = document != NULL ? yaml_document_get_root (document) : NULL;
    map = root != NULL ? yaml_node_get_mapping (root) : NULL;
    if (map == NULL || g_strcmp0 (scalar_member (map, "format"), LRG_REPLAY_FORMAT_NAME) != 0)
    {
        g_set_error_literal (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                             "Not a replay file (format is not " LRG_REPLAY_FORMAT_NAME ")");
        return NULL;
    }

    if (!parse_uint_member (map, "version", TRUE, &value, error))
        return NULL;
    if (value != LRG_REPLAY_FORMAT_VERSION ||
        g_strcmp0 (scalar_member (map, "body-encoding"), LRG_REPLAY_BODY_ENCODING) != 0)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_VERSION,
                     "Unsupported replay version %" G_GUINT64_FORMAT " / encoding '%s'",
                     value, scalar_member (map, "body-encoding"));
        return NULL;
    }

    self = g_object_new (LRG_TYPE_REPLAY, NULL);

    if (!parse_uint_member (map, "seed", TRUE, &self->seed, error) ||
        !parse_uint_member (map, "players", TRUE, &value, error))
        return NULL;
    if (value < 1 || value > LRG_SIMULATION_MAX_PLAYERS)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                     "Bad player count %" G_GUINT64_FORMAT, value);
        return NULL;
    }
    self->n_players = (guint) value;

    text = scalar_member (map, "fixed-step");
    self->fixed_step = text != NULL ? g_ascii_strtod (text, NULL) : 0.0;
    if (!(self->fixed_step > 0.0))
    {
        g_set_error_literal (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                             "Missing or bad fixed-step");
        return NULL;
    }

    n_ticks = 0;
    body_size = 0;
    body_checksum = 0;
    if (!parse_uint_member (map, "start-tick", FALSE, &self->start_tick, error) ||
        !parse_uint_member (map, "ticks", TRUE, &n_ticks, error) ||
        !parse_uint_member (map, "body-size", TRUE, &body_size, error) ||
        !parse_uint_member (map, "body-checksum", TRUE, &body_checksum, error))
        return NULL;
    if (n_ticks > G_MAXUINT / LRG_SIMULATION_MAX_PLAYERS)
    {
        g_set_error_literal (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                             "Tick count out of range");
        return NULL;
    }

    if (scalar_member (map, "initial-checksum") != NULL)
    {
        if (!parse_uint_member (map, "initial-checksum", TRUE, &self->initial_checksum, error))
            return NULL;
        self->has_initial_checksum = TRUE;
    }
    self->initial_state = g_strdup (scalar_member (map, "initial-state"));

    /* Action table */
    actions = yaml_mapping_has_member (map, "actions")
              ? yaml_mapping_get_sequence_member (map, "actions") : NULL;
    if (actions != NULL)
    {
        guint n_actions = yaml_sequence_get_length (actions);

        if (n_actions > LRG_INPUT_SNAPSHOT_MAX_ACTIONS)
        {
            g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                         "Too many actions (%u)", n_actions);
            return NULL;
        }
        g_strfreev (self->actions);
        self->actions = g_new0 (gchar *, n_actions + 1);
        for (i = 0; i < n_actions; i++)
        {
            const gchar *name;

            name = yaml_node_get_string (yaml_sequence_get_element (actions, i));
            if (name == NULL)
            {
                g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                             "Action %u is not a string", i);
                return NULL;
            }
            self->actions[i] = g_strdup (name);
        }
    }

    /* Free-form metadata */
    metadata = yaml_mapping_has_member (map, "metadata")
               ? yaml_mapping_get_mapping_member (map, "metadata") : NULL;
    if (metadata != NULL)
    {
        g_autoptr(GList) keys = yaml_mapping_get_members (metadata);
        GList *l;

        for (l = keys; l != NULL; l = l->next)
        {
            const gchar *v = scalar_member (metadata, l->data);

            if (v != NULL)
                g_hash_table_insert (self->metadata, g_strdup (l->data), g_strdup (v));
        }
    }

    /* 3. Verify and decode the body */
    header_size += strlen (LRG_REPLAY_HEADER_END) - 1;
    if (size - header_size != body_size ||
        lrg_state_hash_data ((const guint8 *) data + header_size, (gsize) body_size, 0) != body_checksum)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                     "Replay body is truncated or corrupt (%" G_GSIZE_FORMAT
                     " bytes, header says %" G_GUINT64_FORMAT ")",
                     size - header_size, body_size);
        return NULL;
    }
    body = lrg_state_buffer_new_from_data ((const guint8 *) data + header_size, (gsize) body_size);
    if (!decode_body (self, body, (guint) n_ticks, error))
        return NULL;

    return g_steal_pointer (&self);
}

LrgReplay *
lrg_replay_load_from_file (const gchar  *path,
                           GError      **error)
{
    g_autoptr(GBytes) bytes = NULL;
    gchar *contents;
    gsize length;

    g_return_val_if_fail (path != NULL, NULL);

    if (!g_file_get_contents (path, &contents, &length, error))
        return NULL;
    bytes = g_bytes_new_take (contents, length);
    return lrg_replay_load_from_bytes (bytes, error);
}

/* ==========================================================================
 * Playback
 * ========================================================================== */

gboolean
lrg_replay_check_compatible (LrgReplay      *self,
                             LrgSimulation  *simulation,
                             GError        **error)
{
    const gchar * const *sim_actions;
    guint i;

    g_return_val_if_fail (LRG_IS_REPLAY (self), FALSE);
    g_return_val_if_fail (LRG_IS_SIMULATION (simulation), FALSE);

    if (lrg_simulation_get_seed (simulation) != self->seed ||
        lrg_simulation_get_fixed_step (simulation) != self->fixed_step ||
        lrg_simulation_get_n_players (simulation) != self->n_players)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_INCOMPATIBLE,
                     "Simulation (seed %" G_GUINT64_FORMAT ", step %g, %u players) does not "
                     "match the replay (seed %" G_GUINT64_FORMAT ", step %g, %u players)",
                     lrg_simulation_get_seed (simulation),
                     lrg_simulation_get_fixed_step (simulation),
                     lrg_simulation_get_n_players (simulation),
                     self->seed, self->fixed_step, self->n_players);
        return FALSE;
    }

    sim_actions = lrg_simulation_get_actions (simulation);
    for (i = 0; self->actions[i] != NULL || sim_actions[i] != NULL; i++)
    {
        if (g_strcmp0 (self->actions[i], sim_actions[i]) != 0)
        {
            g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_INCOMPATIBLE,
                         "Action %u differs: replay '%s', simulation '%s'", i,
                         self->actions[i] != NULL ? self->actions[i] : "(none)",
                         sim_actions[i] != NULL ? sim_actions[i] : "(none)");
            return FALSE;
        }
    }

    if (lrg_simulation_get_tick (simulation) != self->start_tick)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_INITIAL_STATE,
                     "Simulation is at tick %" G_GUINT64_FORMAT ", replay starts at %"
                     G_GUINT64_FORMAT, lrg_simulation_get_tick (simulation), self->start_tick);
        return FALSE;
    }

    if (self->has_initial_checksum &&
        lrg_simulation_checksum (simulation) != self->initial_checksum)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_INITIAL_STATE,
                     "Simulation state (checksum %016" G_GINT64_MODIFIER "x) is not the "
                     "replay's initial state (%016" G_GINT64_MODIFIER "x, '%s')",
                     lrg_simulation_checksum (simulation), self->initial_checksum,
                     self->initial_state != NULL ? self->initial_state : "");
        return FALSE;
    }
    return TRUE;
}

gboolean
lrg_replay_step (LrgReplay      *self,
                 LrgSimulation  *simulation,
                 GError        **error)
{
    guint64 position;
    guint64 expected;
    guint index;

    g_return_val_if_fail (LRG_IS_REPLAY (self), FALSE);
    g_return_val_if_fail (LRG_IS_SIMULATION (simulation), FALSE);

    position = lrg_simulation_get_tick (simulation);
    if (position < self->start_tick ||
        position - self->start_tick >= self->checksums->len)
        return FALSE;
    index = (guint) (position - self->start_tick);

    lrg_simulation_step (simulation,
                         (LrgInputSnapshot * const *) self->inputs->pdata + index * self->n_players,
                         self->n_players);

    /* Desync detector: compare with the checksum recorded after this tick */
    if (lrg_replay_get_checksum (self, index, &expected) &&
        !lrg_simulation_verify_checksum (simulation, lrg_simulation_get_tick (simulation), expected))
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_DESYNC,
                     "Desync at tick %" G_GUINT64_FORMAT ": state checksum differs from the recording",
                     lrg_simulation_get_tick (simulation));
        return FALSE;
    }
    return TRUE;
}

/*
 * play_collecting:
 *
 * Plays the whole replay; when @collected is set, appends the state
 * checksum every @interval ticks (and always after the last tick) as
 * (tick, checksum) pairs.
 */
static gboolean
play_collecting (LrgReplay      *self,
                 LrgSimulation  *simulation,
                 guint           interval,
                 GArray         *collected,
                 GError        **error)
{
    guint i;

    if (!lrg_replay_check_compatible (self, simulation, error))
        return FALSE;

    for (i = 0; i < self->checksums->len; i++)
    {
        GError *step_error = NULL;

        if (!lrg_replay_step (self, simulation, &step_error))
        {
            if (step_error != NULL)
                g_propagate_error (error, step_error);
            else
                g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_FAILED,
                             "Replay stopped early at tick %u", i);
            return FALSE;
        }

        if (collected != NULL &&
            ((interval > 0 && (i + 1) % interval == 0) || i + 1 == self->checksums->len))
        {
            guint64 pair[2];

            pair[0] = lrg_simulation_get_tick (simulation);
            pair[1] = lrg_simulation_checksum (simulation);
            g_array_append_vals (collected, pair, 2);
        }
    }
    return TRUE;
}

gboolean
lrg_replay_play (LrgReplay      *self,
                 LrgSimulation  *simulation,
                 GError        **error)
{
    g_return_val_if_fail (LRG_IS_REPLAY (self), FALSE);
    g_return_val_if_fail (LRG_IS_SIMULATION (simulation), FALSE);

    return play_collecting (self, simulation, 0, NULL, error);
}

/* ==========================================================================
 * Golden files
 * ========================================================================== */

static gboolean
golden_update_requested (void)
{
    const gchar *value = g_getenv ("LRG_UPDATE_GOLDEN");

    return value != NULL && *value != '\0' && g_strcmp0 (value, "0") != 0;
}

static gboolean
write_golden (const gchar  *path,
              guint         interval,
              GArray       *collected,
              GError      **error)
{
    g_autoptr(GString) out = NULL;
    guint n_pairs;
    guint i;

    n_pairs = collected->len / 2;
    out = g_string_new ("%YAML 1.2\n---\n");
    g_string_append (out, "format: " LRG_GOLDEN_FORMAT_NAME "\n");
    g_string_append_printf (out, "version: %d\n", LRG_GOLDEN_FORMAT_VERSION);
    g_string_append_printf (out, "interval: %u\n", interval);
    g_string_append (out, "checksums:\n");
    for (i = 0; i < n_pairs; i++)
    {
        g_string_append_printf (out, "  - tick: %" G_GUINT64_FORMAT "\n",
                                g_array_index (collected, guint64, 2 * i));
        g_string_append_printf (out, "    checksum: 0x%016" G_GINT64_MODIFIER "x\n",
                                g_array_index (collected, guint64, 2 * i + 1));
    }
    return g_file_set_contents (path, out->str, (gssize) out->len, error);
}

gboolean
lrg_replay_check_golden (LrgReplay      *self,
                         LrgSimulation  *simulation,
                         const gchar    *golden_path,
                         guint           interval,
                         GError        **error)
{
    g_autoptr(GArray) collected = NULL;
    g_autoptr(YamlParser) parser = NULL;
    YamlDocument *document;
    YamlNode *root;
    YamlMapping *map;
    YamlSequence *entries;
    guint64 golden_interval = 0;
    guint n_entries;
    guint n_pairs;
    guint i;

    g_return_val_if_fail (LRG_IS_REPLAY (self), FALSE);
    g_return_val_if_fail (LRG_IS_SIMULATION (simulation), FALSE);
    g_return_val_if_fail (golden_path != NULL, FALSE);

    /* 1. Play headless, collecting checksums */
    collected = g_array_new (FALSE, FALSE, sizeof (guint64));
    if (!play_collecting (self, simulation, interval, collected, error))
        return FALSE;
    n_pairs = collected->len / 2;

    /* 2. Update mode: write and pass */
    if (golden_update_requested ())
    {
        lrg_log_message ("Updating golden file %s (%u checksums)", golden_path, n_pairs);
        return write_golden (golden_path, interval, collected, error);
    }

    /* 3. Compare with the stored file */
    if (!g_file_test (golden_path, G_FILE_TEST_EXISTS))
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_NOT_FOUND,
                     "Golden file %s does not exist (run with LRG_UPDATE_GOLDEN=1 to create it)",
                     golden_path);
        return FALSE;
    }
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, golden_path, error))
        return FALSE;
    document = yaml_parser_get_document (parser, 0);
    root = document != NULL ? yaml_document_get_root (document) : NULL;
    map = root != NULL ? yaml_node_get_mapping (root) : NULL;
    if (map == NULL || g_strcmp0 (scalar_member (map, "format"), LRG_GOLDEN_FORMAT_NAME) != 0 ||
        !yaml_mapping_has_member (map, "checksums"))
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                     "%s is not a golden checksum file", golden_path);
        return FALSE;
    }
    if (!parse_uint_member (map, "interval", TRUE, &golden_interval, error))
        return FALSE;
    if (golden_interval != interval)
    {
        g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_GOLDEN_MISMATCH,
                     "%s was written with interval %" G_GUINT64_FORMAT ", checked with %u",
                     golden_path, golden_interval, interval);
        return FALSE;
    }

    entries = yaml_mapping_get_sequence_member (map, "checksums");
    n_entries = entries != NULL ? yaml_sequence_get_length (entries) : 0;
    for (i = 0; i < MAX (n_entries, n_pairs); i++)
    {
        YamlMapping *entry;
        guint64 tick = 0;
        guint64 checksum = 0;

        if (i >= n_entries || i >= n_pairs)
        {
            g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_GOLDEN_MISMATCH,
                         "%s has %u checksums, the run produced %u (replay length changed?)",
                         golden_path, n_entries, n_pairs);
            return FALSE;
        }

        entry = yaml_node_get_mapping (yaml_sequence_get_element (entries, i));
        if (entry == NULL ||
            !parse_uint_member (entry, "tick", TRUE, &tick, error) ||
            !parse_uint_member (entry, "checksum", TRUE, &checksum, error))
        {
            if (error != NULL && *error == NULL)
                g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE,
                             "Bad checksum entry %u in %s", i, golden_path);
            return FALSE;
        }

        if (tick != g_array_index (collected, guint64, 2 * i) ||
            checksum != g_array_index (collected, guint64, 2 * i + 1))
        {
            g_set_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_GOLDEN_MISMATCH,
                         "Golden mismatch at tick %" G_GUINT64_FORMAT ": expected %016"
                         G_GINT64_MODIFIER "x, got %016" G_GINT64_MODIFIER "x (%s)",
                         g_array_index (collected, guint64, 2 * i), checksum,
                         g_array_index (collected, guint64, 2 * i + 1), golden_path);
            return FALSE;
        }
    }
    return TRUE;
}
