/* lrg-midi-output-alsa.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The ALSA sequencer output (see lrg-midi-output-alsa.h and
 * docs/modules/audio/midi-output.org).
 *
 * One sequencer client per output with one source port connected to the
 * destination, and one queue started at construction.  A message is
 * encoded into a sequencer event (snd_midi_event_encode(), so the base
 * class's validated bytes are all it ever reads) and either sent direct
 * or scheduled on the queue at a relative real time.  A flush removes the
 * client's events still waiting in its output buffer and on the queue.
 * Without LRG_HAS_ALSA the constructor reports UNSUPPORTED.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>

#ifdef LRG_HAS_ALSA
#include <alsa/asoundlib.h>
#endif

#include "lrg-midi-output-alsa.h"
#include "../lrg-log.h"

struct _LrgMidiOutputAlsa
{
    LrgMidiOutput     parent_instance;

    gchar            *destination;   /* "client:port" */
#ifdef LRG_HAS_ALSA
    snd_seq_t        *seq;
    gint              port;
    gint              queue;
    snd_midi_event_t *encoder;
#endif
};

G_DEFINE_FINAL_TYPE (LrgMidiOutputAlsa, lrg_midi_output_alsa, LRG_TYPE_MIDI_OUTPUT)

gboolean
lrg_midi_output_alsa_is_available (void)
{
#ifdef LRG_HAS_ALSA
    return TRUE;
#else
    return FALSE;
#endif
}

#ifdef LRG_HAS_ALSA

/* ==========================================================================
 * Ports
 * ========================================================================== */

/* a port we can write to (and subscribe to), not our own */
static gboolean
port_accepts_output (snd_seq_t           *seq,
                     snd_seq_port_info_t *port)
{
    guint caps;

    caps = snd_seq_port_info_get_capability (port);
    if ((caps & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) !=
        (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
        return FALSE;
    if ((caps & SND_SEQ_PORT_CAP_NO_EXPORT) != 0)
        return FALSE;
    return snd_seq_port_info_get_client (port) != snd_seq_client_id (seq);
}

/*
 * walk_ports:
 * Every port that accepts output, in client order.  With @lines the
 * description of each is appended; with @out_auto the "auto" pick is
 * made: the first port that is not the kernel's Midi Through, else Midi
 * Through itself.
 */
static void
walk_ports (snd_seq_t     *seq,
            GStrvBuilder  *lines,
            snd_seq_addr_t *out_auto,
            gboolean       *out_found)
{
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t   *pinfo;
    gboolean               have_through;
    snd_seq_addr_t         through;

    snd_seq_client_info_alloca (&cinfo);
    snd_seq_port_info_alloca (&pinfo);
    have_through = FALSE;
    memset (&through, 0, sizeof (through));
    if (out_found != NULL)
        *out_found = FALSE;

    snd_seq_client_info_set_client (cinfo, -1);
    while (snd_seq_query_next_client (seq, cinfo) >= 0)
    {
        gint         client;
        const gchar *client_name;

        client = snd_seq_client_info_get_client (cinfo);
        if (client == SND_SEQ_CLIENT_SYSTEM)
            continue;
        client_name = snd_seq_client_info_get_name (cinfo);
        snd_seq_port_info_set_client (pinfo, client);
        snd_seq_port_info_set_port (pinfo, -1);
        while (snd_seq_query_next_port (seq, pinfo) >= 0)
        {
            gboolean is_through;

            if (!port_accepts_output (seq, pinfo))
                continue;
            if (lines != NULL)
            {
                g_autofree gchar *line = NULL;

                line = g_strdup_printf ("%d:%d %s:%s", client, snd_seq_port_info_get_port (pinfo),
                                        client_name, snd_seq_port_info_get_name (pinfo));
                g_strv_builder_add (lines, line);
            }
            if (out_auto == NULL || out_found == NULL || *out_found)
                continue;
            is_through = g_str_has_prefix (client_name, "Midi Through");
            if (is_through)
            {
                if (!have_through)
                {
                    through.client = (guchar)client;
                    through.port = (guchar)snd_seq_port_info_get_port (pinfo);
                    have_through = TRUE;
                }
                continue;
            }
            out_auto->client = (guchar)client;
            out_auto->port = (guchar)snd_seq_port_info_get_port (pinfo);
            *out_found = TRUE;
        }
    }
    if (out_auto != NULL && out_found != NULL && !*out_found && have_through)
    {
        *out_auto = through;
        *out_found = TRUE;
    }
}

static snd_seq_t *
open_sequencer (GError **error)
{
    snd_seq_t *seq;
    gint       err;

    seq = NULL;
    err = snd_seq_open (&seq, "default", SND_SEQ_OPEN_OUTPUT, 0);
    if (err < 0)
    {
        g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_FAILED,
                     "cannot open the ALSA sequencer: %s", snd_strerror (err));
        return NULL;
    }
    return seq;
}

/* ==========================================================================
 * Sending
 * ========================================================================== */

static gboolean
lrg_midi_output_alsa_send (LrgMidiOutput *output,
                           const guint8  *message,
                           gsize          length,
                           gdouble        delay)
{
    LrgMidiOutputAlsa *self = LRG_MIDI_OUTPUT_ALSA (output);
    snd_seq_event_t    ev;
    long               used;

    snd_seq_ev_clear (&ev);
    snd_midi_event_reset_encode (self->encoder);
    used = snd_midi_event_encode (self->encoder, message, (long)length, &ev);
    if (used <= 0 || ev.type == SND_SEQ_EVENT_NONE)
        return FALSE;

    snd_seq_ev_set_source (&ev, self->port);
    snd_seq_ev_set_subs (&ev);
    if (delay > 0.0)
    {
        snd_seq_real_time_t when;

        when.tv_sec = (guint)delay;
        when.tv_nsec = (guint)((delay - (gdouble)when.tv_sec) * 1e9);
        snd_seq_ev_schedule_real (&ev, self->queue, 1, &when);
    }
    else
    {
        snd_seq_ev_set_direct (&ev);
    }
    if (snd_seq_event_output (self->seq, &ev) < 0)
        return FALSE;
    return snd_seq_drain_output (self->seq) >= 0;
}

static void
lrg_midi_output_alsa_flush (LrgMidiOutput *output)
{
    LrgMidiOutputAlsa      *self = LRG_MIDI_OUTPUT_ALSA (output);
    snd_seq_remove_events_t *remove;

    (void)snd_seq_drop_output (self->seq);
    if (snd_seq_remove_events_malloc (&remove) < 0)
        return;
    snd_seq_remove_events_set_condition (remove, SND_SEQ_REMOVE_OUTPUT);
    snd_seq_remove_events_set_queue (remove, self->queue);
    (void)snd_seq_remove_events (self->seq, remove);
    snd_seq_remove_events_free (remove);
}

#endif /* LRG_HAS_ALSA */

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_midi_output_alsa_finalize (GObject *object)
{
    LrgMidiOutputAlsa *self = LRG_MIDI_OUTPUT_ALSA (object);

#ifdef LRG_HAS_ALSA
    if (self->seq != NULL)
    {
        if (self->queue >= 0)
        {
            (void)snd_seq_stop_queue (self->seq, self->queue, NULL);
            (void)snd_seq_drain_output (self->seq);
            (void)snd_seq_free_queue (self->seq, self->queue);
        }
        snd_seq_close (self->seq);
    }
    if (self->encoder != NULL)
        snd_midi_event_free (self->encoder);
#endif
    g_free (self->destination);

    G_OBJECT_CLASS (lrg_midi_output_alsa_parent_class)->finalize (object);
}

static void
lrg_midi_output_alsa_class_init (LrgMidiOutputAlsaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_midi_output_alsa_finalize;
#ifdef LRG_HAS_ALSA
    LRG_MIDI_OUTPUT_CLASS (klass)->send = lrg_midi_output_alsa_send;
    LRG_MIDI_OUTPUT_CLASS (klass)->flush = lrg_midi_output_alsa_flush;
#endif
}

static void
lrg_midi_output_alsa_init (LrgMidiOutputAlsa *self)
{
#ifdef LRG_HAS_ALSA
    self->port = -1;
    self->queue = -1;
#else
    (void)self;
#endif
}

/*
 * lrg_midi_output_alsa_new:
 * 1. open the sequencer and name the client; 2. resolve the destination
 *    ("auto" through walk_ports(), anything else through
 *    snd_seq_parse_address(), which takes numbers and client names);
 * 3. the source port, the connection, the queue and the encoder.
 *    Any failure frees what was made.
 */
LrgMidiOutputAlsa *
lrg_midi_output_alsa_new (const gchar  *port,
                          GError      **error)
{
#ifdef LRG_HAS_ALSA
    g_autoptr(LrgMidiOutputAlsa) self = NULL;
    snd_seq_addr_t               dest;
    gint                         err;

    g_return_val_if_fail (port != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    self = g_object_new (LRG_TYPE_MIDI_OUTPUT_ALSA, NULL);

    /* 1. the client */
    self->seq = open_sequencer (error);
    if (self->seq == NULL)
        return NULL;
    (void)snd_seq_set_client_name (self->seq, "libregnum");

    /* 2. the destination */
    memset (&dest, 0, sizeof (dest));
    if (g_ascii_strcasecmp (port, "auto") == 0)
    {
        gboolean found;

        walk_ports (self->seq, NULL, &dest, &found);
        if (!found)
        {
            g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_NOT_FOUND,
                         "no ALSA sequencer port accepts MIDI output");
            return NULL;
        }
    }
    else
    {
        err = port[0] != '\0' ? snd_seq_parse_address (self->seq, &dest, port) : -EINVAL;
        if (err < 0)
        {
            g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_NOT_FOUND,
                         "no ALSA sequencer port \"%s\": %s", port, snd_strerror (err));
            return NULL;
        }
    }

    /* 3. port, connection, queue, encoder */
    self->port = snd_seq_create_simple_port (self->seq, "out",
                                             SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                             SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (self->port < 0)
    {
        g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_FAILED,
                     "cannot create an ALSA sequencer port: %s", snd_strerror (self->port));
        return NULL;
    }
    err = snd_seq_connect_to (self->seq, self->port, dest.client, dest.port);
    if (err < 0)
    {
        g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_FAILED,
                     "cannot connect to ALSA sequencer port %d:%d: %s",
                     dest.client, dest.port, snd_strerror (err));
        return NULL;
    }
    self->queue = snd_seq_alloc_named_queue (self->seq, "libregnum");
    if (self->queue < 0)
    {
        g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_FAILED,
                     "cannot allocate an ALSA sequencer queue: %s", snd_strerror (self->queue));
        return NULL;
    }
    (void)snd_seq_start_queue (self->seq, self->queue, NULL);
    (void)snd_seq_drain_output (self->seq);
    if (snd_midi_event_new (16, &self->encoder) < 0)
    {
        g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_FAILED,
                     "cannot create a MIDI event encoder");
        return NULL;
    }
    snd_midi_event_no_status (self->encoder, 1);

    self->destination = g_strdup_printf ("%d:%d", dest.client, dest.port);
    lrg_info (LRG_LOG_DOMAIN_AUDIO, "MIDI out: connected to ALSA port %s", self->destination);
    return g_steal_pointer (&self);
#else
    g_return_val_if_fail (port != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_UNSUPPORTED,
                 "this libregnum was built without ALSA (Fedora: alsa-lib-devel)");
    return NULL;
#endif
}

GStrv
lrg_midi_output_alsa_list_ports (GError **error)
{
#ifdef LRG_HAS_ALSA
    snd_seq_t    *seq;
    GStrvBuilder *builder;
    GStrv         lines;

    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    seq = open_sequencer (error);
    if (seq == NULL)
        return NULL;
    builder = g_strv_builder_new ();
    walk_ports (seq, builder, NULL, NULL);
    snd_seq_close (seq);
    lines = g_strv_builder_end (builder);
    g_strv_builder_unref (builder);
    return lines;
#else
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    g_set_error (error, LRG_MIDI_OUTPUT_ERROR, LRG_MIDI_OUTPUT_ERROR_UNSUPPORTED,
                 "this libregnum was built without ALSA (Fedora: alsa-lib-devel)");
    return NULL;
#endif
}

const gchar *
lrg_midi_output_alsa_get_destination (LrgMidiOutputAlsa *self)
{
    g_return_val_if_fail (LRG_IS_MIDI_OUTPUT_ALSA (self), NULL);

    return self->destination;
}
