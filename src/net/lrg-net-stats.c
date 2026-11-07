/* lrg-net-stats.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNetStats: network traffic statistics.
 *
 * Layout: one "track" (running totals plus a ring of one-second buckets)
 * for all traffic, one per message type and one per peer; a peer also
 * keeps per-type totals (no history: the per-type history is global),
 * its RTT readings and the send times of pings still waiting for their
 * pong. A bucket is reused when its second comes round again: the ring
 * index is second % window and the bucket remembers which second it
 * holds, so a stale bucket reads as empty without any sweeping.
 *
 * The networking layers feed the default instance through the
 * _lrg_net_stats_hook_*() functions (lrg-net-buffer-private.h). They
 * test one atomic integer first, so a disabled default costs a load
 * and a branch per message.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-net-stats.h"
#include "lrg-net-buffer-private.h"
#include "../lrg-enums.h"
#include <string.h>

#define PENDING_PING_LIMIT (256)   /* pings without a pong kept per peer */
#define RTT_ALPHA          (0.125) /* smoothing of the average (RFC 6298) */
#define UNKNOWN_TYPE       "unknown"

/* ==========================================================================
 * Enum and boxed types
 * ========================================================================== */

GType
lrg_net_stats_metric_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_NET_STATS_METRIC_BYTES_IN, "LRG_NET_STATS_METRIC_BYTES_IN", "bytes-in" },
			{ LRG_NET_STATS_METRIC_BYTES_OUT, "LRG_NET_STATS_METRIC_BYTES_OUT", "bytes-out" },
			{ LRG_NET_STATS_METRIC_PACKETS_IN, "LRG_NET_STATS_METRIC_PACKETS_IN", "packets-in" },
			{ LRG_NET_STATS_METRIC_PACKETS_OUT, "LRG_NET_STATS_METRIC_PACKETS_OUT", "packets-out" },
			{ LRG_NET_STATS_METRIC_RTT, "LRG_NET_STATS_METRIC_RTT", "rtt" },
			{ LRG_NET_STATS_METRIC_LOST, "LRG_NET_STATS_METRIC_LOST", "lost" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgNetStatsMetric", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

LrgNetCounters *
lrg_net_counters_copy (const LrgNetCounters *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return (LrgNetCounters *) g_memdup2 (self, sizeof (LrgNetCounters));
}

void
lrg_net_counters_free (LrgNetCounters *self)
{
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgNetCounters, lrg_net_counters, lrg_net_counters_copy, lrg_net_counters_free)

/* ==========================================================================
 * Tracks (totals + per-second ring)
 * ========================================================================== */

typedef struct
{
	gint64         second;   /* the second this bucket holds, G_MININT64 = none */
	LrgNetCounters c;
	gdouble        rtt_sum;
	guint          rtt_n;
} Bucket;

typedef struct
{
	LrgNetCounters totals;
	Bucket        *ring;     /* window entries */
} Track;

typedef struct
{
	Track       track;
	GHashTable *types;       /* name -> LrgNetCounters* (totals only) */
	gboolean    connected;
	gboolean    has_rtt;
	gdouble     rtt_last;
	gdouble     rtt_avg;
	gdouble     rtt_min;
	gdouble     rtt_max;
	GHashTable *pings;       /* GUINT_TO_POINTER (sequence) -> gint64* sent at (us) */
} Peer;

struct _LrgNetStats
{
	GObject     parent_instance;

	GMutex      lock;
	gboolean    enabled;
	guint       window;
	gint64      manual_time; /* < 0: g_get_monotonic_time () */

	Track       all;
	GHashTable *peers;       /* key -> Peer* */
	GHashTable *types;       /* name -> Track* */
};

G_DEFINE_FINAL_TYPE (LrgNetStats, lrg_net_stats, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_ENABLED,
	PROP_WINDOW,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static LrgNetStats *default_stats = NULL;
static gint         hooks_state = -1;   /* -1 unknown, 0 off, 1 on (default enabled) */

/* Allocates an empty ring of @window buckets. */
static Bucket *
ring_new (guint window)
{
	Bucket *ring = g_new0 (Bucket, window);
	guint   i;

	for (i = 0; i < window; i++)
		ring[i].second = G_MININT64;
	return ring;
}

static void
track_init (Track *track,
            guint  window)
{
	memset (&track->totals, 0, sizeof track->totals);
	track->ring = ring_new (window);
}

static void
track_clear (Track *track)
{
	g_clear_pointer (&track->ring, g_free);
}

static Track *
track_new (guint window)
{
	Track *track = g_new0 (Track, 1);

	track_init (track, window);
	return track;
}

static void
track_free (gpointer data)
{
	Track *track = data;

	track_clear (track);
	g_free (track);
}

/* The bucket of @second, recycled when it still holds an older second. */
static Bucket *
track_bucket (Track  *track,
              guint   window,
              gint64  second)
{
	Bucket *b = &track->ring[(guint64) second % window];

	if (b->second != second)
	{
		memset (b, 0, sizeof *b);
		b->second = second;
	}
	return b;
}

/* Adds signed @delta to an unsigned counter without going below zero. */
static void
add_clamped (guint64 *value,
             gint64   delta)
{
	if (delta >= 0)
		*value += (guint64) delta;
	else if ((guint64) (-delta) >= *value)
		*value = 0;
	else
		*value -= (guint64) (-delta);
}

/* Applies one traffic event to a counter set. */
static void
counters_add (LrgNetCounters *c,
              gboolean        sent,
              gsize           bytes)
{
	if (sent)
	{
		c->packets_out++;
		c->bytes_out += bytes;
	}
	else
	{
		c->packets_in++;
		c->bytes_in += bytes;
	}
}

static void
track_add (Track    *track,
           guint     window,
           gint64    second,
           gboolean  sent,
           gsize     bytes)
{
	counters_add (&track->totals, sent, bytes);
	counters_add (&track_bucket (track, window, second)->c, sent, bytes);
}

/* Sums the last @seconds buckets (ending at @now) into @out. */
static void
track_window (Track          *track,
              guint           window,
              gint64          now,
              guint           seconds,
              LrgNetCounters *out)
{
	guint i;

	memset (out, 0, sizeof *out);
	seconds = CLAMP (seconds, 1u, window);
	for (i = 0; i < seconds; i++)
	{
		gint64  sec = now - (gint64) i;
		Bucket *b = &track->ring[(guint64) sec % window];

		if (b->second != sec)
			continue;
		out->packets_in += b->c.packets_in;
		out->packets_out += b->c.packets_out;
		out->bytes_in += b->c.bytes_in;
		out->bytes_out += b->c.bytes_out;
		out->lost += b->c.lost;
	}
}

/* ==========================================================================
 * Peers
 * ========================================================================== */

static Peer *
peer_new (guint window)
{
	Peer *peer = g_new0 (Peer, 1);

	track_init (&peer->track, window);
	peer->types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	peer->pings = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	peer->connected = TRUE;
	return peer;
}

static void
peer_free (gpointer data)
{
	Peer *peer = data;

	track_clear (&peer->track);
	g_hash_table_unref (peer->types);
	g_hash_table_unref (peer->pings);
	g_free (peer);
}

/* Finds or creates @key's record (caller holds the lock). */
static Peer *
peer_get (LrgNetStats *self,
          const gchar *key)
{
	Peer *peer = g_hash_table_lookup (self->peers, key);

	if (peer == NULL)
	{
		peer = peer_new (self->window);
		g_hash_table_insert (self->peers, g_strdup (key), peer);
	}
	return peer;
}

static Track *
type_get (LrgNetStats *self,
          const gchar *name)
{
	Track *track = g_hash_table_lookup (self->types, name);

	if (track == NULL)
	{
		track = track_new (self->window);
		g_hash_table_insert (self->types, g_strdup (name), track);
	}
	return track;
}

/* The current time in whole seconds and microseconds (lock held). */
static gint64
now_us (LrgNetStats *self)
{
	return self->manual_time >= 0 ? self->manual_time : g_get_monotonic_time ();
}

static gint64
now_second (LrgNetStats *self)
{
	return now_us (self) / G_USEC_PER_SEC;
}

/* Adds an RTT sample to @peer and the current second (lock held). */
static void
peer_add_rtt (LrgNetStats *self,
              Peer        *peer,
              gdouble      rtt_ms)
{
	gint64  second = now_second (self);
	Bucket *b;

	rtt_ms = MAX (0.0, rtt_ms);
	if (!peer->has_rtt)
	{
		peer->has_rtt = TRUE;
		peer->rtt_avg = rtt_ms;
		peer->rtt_min = rtt_ms;
		peer->rtt_max = rtt_ms;
	}
	else
	{
		peer->rtt_avg += RTT_ALPHA * (rtt_ms - peer->rtt_avg);
		peer->rtt_min = MIN (peer->rtt_min, rtt_ms);
		peer->rtt_max = MAX (peer->rtt_max, rtt_ms);
	}
	peer->rtt_last = rtt_ms;

	b = track_bucket (&peer->track, self->window, second);
	b->rtt_sum += rtt_ms;
	b->rtt_n++;
	b = track_bucket (&self->all, self->window, second);
	b->rtt_sum += rtt_ms;
	b->rtt_n++;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_net_stats_finalize (GObject *object)
{
	LrgNetStats *self = LRG_NET_STATS (object);

	track_clear (&self->all);
	g_hash_table_unref (self->peers);
	g_hash_table_unref (self->types);
	g_mutex_clear (&self->lock);
	G_OBJECT_CLASS (lrg_net_stats_parent_class)->finalize (object);
}

static void
lrg_net_stats_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgNetStats *self = LRG_NET_STATS (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		g_value_set_boolean (value, lrg_net_stats_get_enabled (self));
		break;
	case PROP_WINDOW:
		g_value_set_uint (value, lrg_net_stats_get_window (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_net_stats_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgNetStats *self = LRG_NET_STATS (object);

	switch (prop_id)
	{
	case PROP_ENABLED:
		lrg_net_stats_set_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_WINDOW:
		lrg_net_stats_set_window (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_net_stats_class_init (LrgNetStatsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_net_stats_finalize;
	object_class->get_property = lrg_net_stats_get_property;
	object_class->set_property = lrg_net_stats_set_property;

	/**
	 * LrgNetStats:enabled:
	 *
	 * Whether record calls (and, for the default instance, the
	 * networking hooks) are counted.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNetStats:window:
	 *
	 * History length in seconds (1 .. %LRG_NET_STATS_MAX_WINDOW).
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_WINDOW] =
		g_param_spec_uint ("window", NULL, NULL, 1, LRG_NET_STATS_MAX_WINDOW, LRG_NET_STATS_DEFAULT_WINDOW,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_net_stats_init (LrgNetStats *self)
{
	g_mutex_init (&self->lock);
	self->enabled = TRUE;
	self->window = LRG_NET_STATS_DEFAULT_WINDOW;
	self->manual_time = -1;
	track_init (&self->all, self->window);
	self->peers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, peer_free);
	self->types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, track_free);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgNetStats *
lrg_net_stats_new (void)
{
	return g_object_new (LRG_TYPE_NET_STATS, NULL);
}

LrgNetStats *
lrg_net_stats_get_default (void)
{
	static gsize once = 0;

	if (g_once_init_enter (&once))
	{
		const gchar *env = g_getenv ("LRG_NET_STATS");

		default_stats = lrg_net_stats_new ();
		default_stats->enabled = (g_strcmp0 (env, "1") == 0);
		g_atomic_int_set (&hooks_state, default_stats->enabled ? 1 : 0);
		g_once_init_leave (&once, 1);
	}
	return default_stats;
}

gboolean
lrg_net_stats_get_enabled (LrgNetStats *self)
{
	gboolean enabled;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), FALSE);
	g_mutex_lock (&self->lock);
	enabled = self->enabled;
	g_mutex_unlock (&self->lock);
	return enabled;
}

void
lrg_net_stats_set_enabled (LrgNetStats *self,
                           gboolean     enabled)
{
	gboolean changed;

	g_return_if_fail (LRG_IS_NET_STATS (self));

	enabled = !!enabled;
	g_mutex_lock (&self->lock);
	changed = (self->enabled != enabled);
	self->enabled = enabled;
	g_mutex_unlock (&self->lock);

	/* The default instance gates the networking hooks */
	if (self == default_stats)
		g_atomic_int_set (&hooks_state, enabled ? 1 : 0);
	if (changed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

guint
lrg_net_stats_get_window (LrgNetStats *self)
{
	g_return_val_if_fail (LRG_IS_NET_STATS (self), 0);
	return self->window;
}

void
lrg_net_stats_set_window (LrgNetStats *self,
                          guint        seconds)
{
	GHashTableIter iter;
	gpointer       value;

	g_return_if_fail (LRG_IS_NET_STATS (self));

	seconds = CLAMP (seconds, 1u, (guint) LRG_NET_STATS_MAX_WINDOW);
	g_mutex_lock (&self->lock);
	if (seconds == self->window)
	{
		g_mutex_unlock (&self->lock);
		return;
	}

	/* New, empty rings everywhere; totals are kept */
	self->window = seconds;
	g_free (self->all.ring);
	self->all.ring = ring_new (seconds);
	g_hash_table_iter_init (&iter, self->peers);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		Peer *peer = value;

		g_free (peer->track.ring);
		peer->track.ring = ring_new (seconds);
	}
	g_hash_table_iter_init (&iter, self->types);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		Track *track = value;

		g_free (track->ring);
		track->ring = ring_new (seconds);
	}
	g_mutex_unlock (&self->lock);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WINDOW]);
}

void
lrg_net_stats_set_time (LrgNetStats *self,
                        gint64       monotonic_us)
{
	g_return_if_fail (LRG_IS_NET_STATS (self));
	g_mutex_lock (&self->lock);
	self->manual_time = monotonic_us < 0 ? -1 : monotonic_us;
	g_mutex_unlock (&self->lock);
}

void
lrg_net_stats_reset (LrgNetStats *self)
{
	g_return_if_fail (LRG_IS_NET_STATS (self));
	g_mutex_lock (&self->lock);
	track_clear (&self->all);
	track_init (&self->all, self->window);
	g_hash_table_remove_all (self->peers);
	g_hash_table_remove_all (self->types);
	g_mutex_unlock (&self->lock);
}

/* Shared body of record_sent / record_received. */
static void
record (LrgNetStats *self,
        const gchar *peer,
        const gchar *message_type,
        gsize        bytes,
        gboolean     sent)
{
	gint64 second;

	g_mutex_lock (&self->lock);
	if (!self->enabled)
	{
		g_mutex_unlock (&self->lock);
		return;
	}
	if (message_type == NULL || *message_type == '\0')
		message_type = UNKNOWN_TYPE;
	second = now_second (self);

	/* Everything, the message type, and the peer with its per-type totals */
	track_add (&self->all, self->window, second, sent, bytes);
	track_add (type_get (self, message_type), self->window, second, sent, bytes);
	if (peer != NULL)
	{
		Peer           *p = peer_get (self, peer);
		LrgNetCounters *c = g_hash_table_lookup (p->types, message_type);

		track_add (&p->track, self->window, second, sent, bytes);
		if (c == NULL)
		{
			c = g_new0 (LrgNetCounters, 1);
			g_hash_table_insert (p->types, g_strdup (message_type), c);
		}
		counters_add (c, sent, bytes);
	}
	g_mutex_unlock (&self->lock);
}

void
lrg_net_stats_record_sent (LrgNetStats *self,
                           const gchar *peer,
                           const gchar *message_type,
                           gsize        bytes)
{
	g_return_if_fail (LRG_IS_NET_STATS (self));
	record (self, peer, message_type, bytes, TRUE);
}

void
lrg_net_stats_record_received (LrgNetStats *self,
                               const gchar *peer,
                               const gchar *message_type,
                               gsize        bytes)
{
	g_return_if_fail (LRG_IS_NET_STATS (self));
	record (self, peer, message_type, bytes, FALSE);
}

void
lrg_net_stats_record_rtt (LrgNetStats *self,
                          const gchar *peer,
                          gdouble      rtt_ms)
{
	g_return_if_fail (LRG_IS_NET_STATS (self));
	g_return_if_fail (peer != NULL);

	g_mutex_lock (&self->lock);
	if (self->enabled)
		peer_add_rtt (self, peer_get (self, peer), rtt_ms);
	g_mutex_unlock (&self->lock);
}

void
lrg_net_stats_record_lost (LrgNetStats *self,
                           const gchar *peer,
                           gint         count)
{
	gint64 second;

	g_return_if_fail (LRG_IS_NET_STATS (self));

	g_mutex_lock (&self->lock);
	if (!self->enabled || count == 0)
	{
		g_mutex_unlock (&self->lock);
		return;
	}
	second = now_second (self);
	add_clamped (&self->all.totals.lost, count);
	add_clamped (&track_bucket (&self->all, self->window, second)->c.lost, count);
	if (peer != NULL)
	{
		Peer *p = peer_get (self, peer);

		add_clamped (&p->track.totals.lost, count);
		add_clamped (&track_bucket (&p->track, self->window, second)->c.lost, count);
	}
	g_mutex_unlock (&self->lock);
}

void
lrg_net_stats_record_ping (LrgNetStats *self,
                           const gchar *peer,
                           guint32      sequence)
{
	Peer *p;

	g_return_if_fail (LRG_IS_NET_STATS (self));
	g_return_if_fail (peer != NULL);

	g_mutex_lock (&self->lock);
	if (self->enabled)
	{
		gint64 *at = g_new (gint64, 1);

		p = peer_get (self, peer);
		/* Pongs that never come must not grow the table forever */
		if (g_hash_table_size (p->pings) >= PENDING_PING_LIMIT)
			g_hash_table_remove_all (p->pings);
		*at = now_us (self);
		g_hash_table_insert (p->pings, GUINT_TO_POINTER (sequence), at);
	}
	g_mutex_unlock (&self->lock);
}

void
lrg_net_stats_record_pong (LrgNetStats *self,
                           const gchar *peer,
                           guint32      sequence)
{
	Peer   *p;
	gint64 *at;

	g_return_if_fail (LRG_IS_NET_STATS (self));
	g_return_if_fail (peer != NULL);

	g_mutex_lock (&self->lock);
	p = self->enabled ? g_hash_table_lookup (self->peers, peer) : NULL;
	at = (p != NULL) ? g_hash_table_lookup (p->pings, GUINT_TO_POINTER (sequence)) : NULL;
	if (at != NULL)
	{
		peer_add_rtt (self, p, (gdouble) (now_us (self) - *at) / 1000.0);
		g_hash_table_remove (p->pings, GUINT_TO_POINTER (sequence));
	}
	g_mutex_unlock (&self->lock);
}

void
lrg_net_stats_set_peer_connected (LrgNetStats *self,
                                  const gchar *peer,
                                  gboolean     connected)
{
	g_return_if_fail (LRG_IS_NET_STATS (self));
	g_return_if_fail (peer != NULL);

	g_mutex_lock (&self->lock);
	if (self->enabled)
	{
		Peer *p = peer_get (self, peer);

		p->connected = !!connected;
		if (!connected)
			g_hash_table_remove_all (p->pings);
	}
	g_mutex_unlock (&self->lock);
}

/* The sorted keys of @table as a GStrv (lock held). */
static gchar **
sorted_keys (GHashTable *table)
{
	GPtrArray     *keys = g_ptr_array_new ();
	GHashTableIter iter;
	gpointer       key;

	g_hash_table_iter_init (&iter, table);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (keys, g_strdup (key));
	g_ptr_array_sort_values (keys, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (keys, NULL);
	return (gchar **) g_ptr_array_free (keys, FALSE);
}

gchar **
lrg_net_stats_get_peers (LrgNetStats *self)
{
	gchar **out;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), NULL);
	g_mutex_lock (&self->lock);
	out = sorted_keys (self->peers);
	g_mutex_unlock (&self->lock);
	return out;
}

gchar **
lrg_net_stats_get_message_types (LrgNetStats *self,
                                 const gchar *peer)
{
	gchar **out;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), NULL);
	g_mutex_lock (&self->lock);
	if (peer == NULL)
		out = sorted_keys (self->types);
	else
	{
		Peer *p = g_hash_table_lookup (self->peers, peer);

		out = (p != NULL) ? sorted_keys (p->types) : g_new0 (gchar *, 1);
	}
	g_mutex_unlock (&self->lock);
	return out;
}

gboolean
lrg_net_stats_get_peer_connected (LrgNetStats *self,
                                  const gchar *peer)
{
	Peer    *p;
	gboolean connected;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), FALSE);
	g_return_val_if_fail (peer != NULL, FALSE);
	g_mutex_lock (&self->lock);
	p = g_hash_table_lookup (self->peers, peer);
	connected = (p != NULL && p->connected);
	g_mutex_unlock (&self->lock);
	return connected;
}

gboolean
lrg_net_stats_get_totals (LrgNetStats    *self,
                          const gchar    *peer,
                          const gchar    *message_type,
                          LrgNetCounters *out)
{
	const LrgNetCounters *src = NULL;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	memset (out, 0, sizeof *out);
	g_mutex_lock (&self->lock);
	if (peer != NULL)
	{
		Peer *p = g_hash_table_lookup (self->peers, peer);

		if (p != NULL)
			src = (message_type != NULL) ? g_hash_table_lookup (p->types, message_type) : &p->track.totals;
	}
	else if (message_type != NULL)
	{
		Track *t = g_hash_table_lookup (self->types, message_type);

		if (t != NULL)
			src = &t->totals;
	}
	else
		src = &self->all.totals;
	if (src != NULL)
		*out = *src;
	g_mutex_unlock (&self->lock);
	return src != NULL;
}

/* The track a (peer, type) query reads its history from (lock held). */
static Track *
history_track (LrgNetStats *self,
               const gchar *peer,
               const gchar *message_type)
{
	if (peer != NULL)
	{
		Peer *p = g_hash_table_lookup (self->peers, peer);

		return p != NULL ? &p->track : NULL;
	}
	if (message_type != NULL)
		return g_hash_table_lookup (self->types, message_type);
	return &self->all;
}

gboolean
lrg_net_stats_get_window_counters (LrgNetStats    *self,
                                   const gchar    *peer,
                                   const gchar    *message_type,
                                   guint           seconds,
                                   LrgNetCounters *out)
{
	Track *track;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	memset (out, 0, sizeof *out);
	g_mutex_lock (&self->lock);
	track = history_track (self, peer, message_type);
	if (track != NULL)
		track_window (track, self->window, now_second (self), seconds, out);
	g_mutex_unlock (&self->lock);
	return track != NULL;
}

gdouble *
lrg_net_stats_get_history (LrgNetStats       *self,
                           const gchar       *peer,
                           const gchar       *message_type,
                           LrgNetStatsMetric  metric,
                           guint             *n_values)
{
	Track   *track;
	gdouble *values;
	gint64   now;
	guint    i;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), NULL);
	g_return_val_if_fail (n_values != NULL, NULL);

	g_mutex_lock (&self->lock);
	*n_values = self->window;
	values = g_new0 (gdouble, self->window);
	track = history_track (self, peer, message_type);
	now = now_second (self);
	for (i = 0; track != NULL && i < self->window; i++)
	{
		gint64  sec = now - (gint64) (self->window - 1 - i);
		Bucket *b = &track->ring[(guint64) sec % self->window];

		if (b->second != sec)
			continue;
		switch (metric)
		{
		case LRG_NET_STATS_METRIC_BYTES_IN:
			values[i] = (gdouble) b->c.bytes_in;
			break;
		case LRG_NET_STATS_METRIC_BYTES_OUT:
			values[i] = (gdouble) b->c.bytes_out;
			break;
		case LRG_NET_STATS_METRIC_PACKETS_IN:
			values[i] = (gdouble) b->c.packets_in;
			break;
		case LRG_NET_STATS_METRIC_PACKETS_OUT:
			values[i] = (gdouble) b->c.packets_out;
			break;
		case LRG_NET_STATS_METRIC_RTT:
			values[i] = b->rtt_n > 0 ? b->rtt_sum / (gdouble) b->rtt_n : 0.0;
			break;
		case LRG_NET_STATS_METRIC_LOST:
		default:
			values[i] = (gdouble) b->c.lost;
			break;
		}
	}
	g_mutex_unlock (&self->lock);
	return values;
}

gboolean
lrg_net_stats_get_rtt (LrgNetStats *self,
                       const gchar *peer,
                       gdouble     *last_ms,
                       gdouble     *average_ms,
                       gdouble     *min_ms,
                       gdouble     *max_ms)
{
	Peer    *p;
	gboolean has;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), FALSE);
	g_return_val_if_fail (peer != NULL, FALSE);

	g_mutex_lock (&self->lock);
	p = g_hash_table_lookup (self->peers, peer);
	has = (p != NULL && p->has_rtt);
	if (last_ms != NULL)
		*last_ms = has ? p->rtt_last : 0.0;
	if (average_ms != NULL)
		*average_ms = has ? p->rtt_avg : 0.0;
	if (min_ms != NULL)
		*min_ms = has ? p->rtt_min : 0.0;
	if (max_ms != NULL)
		*max_ms = has ? p->rtt_max : 0.0;
	g_mutex_unlock (&self->lock);
	return has;
}

gdouble
lrg_net_stats_get_loss (LrgNetStats *self,
                        const gchar *peer)
{
	LrgNetCounters c;
	gdouble        denominator;

	g_return_val_if_fail (LRG_IS_NET_STATS (self), 0.0);

	lrg_net_stats_get_totals (self, peer, NULL, &c);
	denominator = (gdouble) c.lost + (gdouble) c.packets_in;
	return denominator > 0.0 ? (gdouble) c.lost / denominator : 0.0;
}

/* ==========================================================================
 * Hooks for the networking layers (lrg-net-buffer-private.h)
 * ========================================================================== */

gboolean
_lrg_net_stats_hooks_active (void)
{
	gint state = g_atomic_int_get (&hooks_state);

	/* First use: create the default, which reads LRG_NET_STATS */
	if (G_UNLIKELY (state < 0))
	{
		lrg_net_stats_get_default ();
		state = g_atomic_int_get (&hooks_state);
	}
	return state > 0;
}

/* The wire name of a transport message type. */
static const gchar *
message_type_name (LrgNetMessageType type)
{
	switch (type)
	{
	case LRG_NET_MESSAGE_TYPE_HANDSHAKE:
		return "handshake";
	case LRG_NET_MESSAGE_TYPE_DATA:
		return "data";
	case LRG_NET_MESSAGE_TYPE_PING:
		return "ping";
	case LRG_NET_MESSAGE_TYPE_PONG:
		return "pong";
	case LRG_NET_MESSAGE_TYPE_DISCONNECT:
		return "disconnect";
	default:
		return UNKNOWN_TYPE;
	}
}

void
_lrg_net_stats_hook_message (const gchar         *peer,
                             const LrgNetMessage *message,
                             gsize                bytes,
                             gboolean             sent)
{
	LrgNetStats      *stats;
	LrgNetMessageType type;

	if (!_lrg_net_stats_hooks_active () || message == NULL)
		return;
	stats = lrg_net_stats_get_default ();
	type = lrg_net_message_get_message_type (message);
	record (stats, peer, message_type_name (type), bytes, sent);

	/* Passive RTT: a ping out and the pong echoing its sequence back */
	if (peer == NULL)
		return;
	if (sent && type == LRG_NET_MESSAGE_TYPE_PING)
		lrg_net_stats_record_ping (stats, peer, lrg_net_message_get_sequence (message));
	else if (!sent && type == LRG_NET_MESSAGE_TYPE_PONG)
		lrg_net_stats_record_pong (stats, peer, lrg_net_message_get_sequence (message));
}

void
_lrg_net_stats_hook_frame (const gchar *peer,
                           const gchar *message_type,
                           gsize        bytes,
                           gboolean     sent)
{
	if (!_lrg_net_stats_hooks_active ())
		return;
	record (lrg_net_stats_get_default (), peer, message_type, bytes, sent);
}

void
_lrg_net_stats_hook_peer (const gchar *peer,
                          gboolean     connected)
{
	if (!_lrg_net_stats_hooks_active () || peer == NULL)
		return;
	lrg_net_stats_set_peer_connected (lrg_net_stats_get_default (), peer, connected);
}

void
_lrg_net_stats_hook_rtt (const gchar *peer,
                         gdouble      rtt_ms)
{
	if (!_lrg_net_stats_hooks_active () || peer == NULL)
		return;
	lrg_net_stats_record_rtt (lrg_net_stats_get_default (), peer, rtt_ms);
}

void
_lrg_net_stats_hook_lost (const gchar *peer,
                          gint         count)
{
	if (!_lrg_net_stats_hooks_active ())
		return;
	lrg_net_stats_record_lost (lrg_net_stats_get_default (), peer, count);
}
