/* synthwave-song.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * "Neon Drive": a complete 2:40 synthwave song made entirely in code with
 * libregnum's music module, no samples or data files.  It shows every part
 * of the song API:
 *
 *   - synthesizer presets (pad, arpeggio, octave bass, supersaw lead,
 *     brass stabs, noise riser) and an 808 drum machine;
 *   - a Foley patch (an impact) played as an instrument on the drops;
 *   - patterns of notes and drum step lanes, clips arranged into intro,
 *     verse, build, chorus, bridge, chorus and outro, with markers;
 *   - automation: filter sweeps, a riser swell, the final fade;
 *   - a mixer: buses for kick, drums, bass, synths, pad, lead and a reverb
 *     return; the kick ducks the pad and the bass (sidechain pumping); an
 *     echo on the lead, a chorus on the pad, a widener, and a master chain
 *     of EQ, glue compressor and limiter;
 *   - rendering normalised to -14 LUFS with a -1 dBTP ceiling, export to
 *     every format this machine can write, optional stems, and the song
 *     saved as .rsong (open it in lrgeditor) and .mid.
 *
 * Run:   ./build/release/examples/synthwave-song --out neon-drive
 *        ./build/release/examples/synthwave-song --formats mp3,flac --stems
 *        ./build/release/examples/synthwave-song --no-render --out examples/data/music
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#define BPM         (108.0)
#define SONG_BEATS  (288.0)

/* the sections, in beats */
#define INTRO       (0.0)
#define VERSE       (32.0)
#define BUILD       (96.0)
#define CHORUS      (128.0)
#define BRIDGE      (192.0)
#define CHORUS2     (224.0)
#define OUTRO       (256.0)

static gchar    *opt_out = NULL;
static gchar    *opt_formats = NULL;
static gboolean  opt_stems = FALSE;
static gboolean  opt_no_render = FALSE;
static gboolean  opt_license = FALSE;

static GOptionEntry entries[] = {
	{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &opt_out, "Folder to write into (default: neon-drive)", "DIR" },
	{ "formats", 'f', 0, G_OPTION_ARG_STRING, &opt_formats,
	  "Comma-separated formats: wav,aiff,flac,mp3,ogg,opus,aac,alac,wavpack or all "
	  "(default: wav,flac,mp3,ogg,opus,aac)", "LIST" },
	{ "stems", 's', 0, G_OPTION_ARG_NONE, &opt_stems, "Also write every track alone as a WAV stem", NULL },
	{ "no-render", 'n', 0, G_OPTION_ARG_NONE, &opt_no_render, "Only write the .rsong, .rfoley and .mid", NULL },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ NULL }
};

/* ==========================================================================
 * Building blocks
 * ========================================================================== */

/*
 * add_notes:
 * Adds the notes of @text, "beat key length [velocity]" entries separated
 * by ';' (keys as note names), to pattern @id.
 */
static void
add_notes (LrgSong     *song,
           const gchar *id,
           const gchar *text)
{
	g_auto(GStrv) items = g_strsplit (text, ";", -1);
	guint         i;

	for (i = 0; items[i] != NULL; i++)
	{
		g_auto(GStrv) f = NULL;
		gchar        *item = g_strstrip (items[i]);
		gdouble       vel = 0.85;

		if (*item == '\0')
			continue;
		f = g_strsplit_set (item, " ", -1);
		if (g_strv_length (f) >= 4)
			vel = g_ascii_strtod (f[3], NULL);
		g_assert (g_strv_length (f) >= 3);
		lrg_song_pattern_add_note (song, id, g_ascii_strtod (f[0], NULL), lrg_music_key_from_name (f[1]),
		                           g_ascii_strtod (f[2], NULL), vel);
	}
}

/* sets a numeric effect parameter in the mixer layout */
static void
fx_param (LrgMixerAsset *mixer,
          const gchar   *bus,
          const gchar   *effect,
          const gchar   *param,
          gdouble        value)
{
	g_autoptr(GError) error = NULL;
	GValue            v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, value);
	if (!lrg_mixer_asset_set_effect_param (mixer, bus, effect, param, &v, &error))
		g_warning ("%s/%s.%s: %s", bus, effect, param, error->message);
	g_value_unset (&v);
}

static void
fx_add (LrgMixerAsset *mixer,
        const gchar   *bus,
        const gchar   *type,
        const gchar   *id)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_mixer_asset_add_effect (mixer, bus, type, id, &error))
		g_warning ("%s: %s", bus, error->message);
}

/* a pattern from a list of drum lanes: "piece:steps" separated by ';' */
static void
drum_pattern (LrgSong     *song,
              const gchar *id,
              const gchar *lanes)
{
	g_auto(GStrv) items = g_strsplit (lanes, ";", -1);
	guint         i;

	lrg_song_add_pattern (song, id, 4);
	lrg_song_pattern_set_steps (song, id, 16);
	for (i = 0; items[i] != NULL; i++)
	{
		g_auto(GStrv) kv = g_strsplit (g_strstrip (items[i]), ":", 2);

		if (g_strv_length (kv) == 2)
			lrg_song_pattern_set_lane (song, id, kv[0], kv[1]);
	}
}

static void
clip (LrgSong     *song,
      const gchar *track,
      const gchar *pattern,
      gdouble      start,
      gdouble      end)
{
	if (lrg_song_add_clip (song, track, pattern, start, end - start) < 0)
		g_warning ("clip %s on %s at %g failed", pattern, track, start);
}

/* ==========================================================================
 * The music
 * ========================================================================== */

/*
 * The progression is Am - F - C - G, one bar each (16 beats).  The chord
 * voicings stay close together so the pad moves smoothly.
 */
static void
write_patterns (LrgSong *song)
{
	static const gchar *bass_roots[4] = { "A1", "F1", "C2", "G1" };
	static const gint   chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
	static const gint   arp_order[16] = { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 1 };
	guint               bar, i;

	/* pad: whole-bar chords */
	lrg_song_add_pattern (song, "chords", 16);
	for (bar = 0; bar < 4; bar++)
		for (i = 0; i < 3; i++)
			lrg_song_pattern_add_note (song, "chords", bar * 4.0, chords[bar][i], 3.95, 0.7);

	/* bass: the 80s octave bass, eighths jumping between root and octave */
	lrg_song_add_pattern (song, "bass-octaves", 16);
	lrg_song_add_pattern (song, "bass-roots", 16);
	for (bar = 0; bar < 4; bar++)
	{
		gint root = lrg_music_key_from_name (bass_roots[bar]);

		for (i = 0; i < 8; i++)
			lrg_song_pattern_add_note (song, "bass-octaves", bar * 4.0 + i * 0.5, root + (i % 2 ? 12 : 0), 0.42,
			                           i == 0 ? 1.0 : i % 2 ? 0.72 : 0.86);
		lrg_song_pattern_add_note (song, "bass-roots", bar * 4.0, root + 12, 3.9, 0.9);
	}

	/* arpeggio: sixteenths over the chord an octave up */
	lrg_song_add_pattern (song, "arp", 16);
	for (bar = 0; bar < 4; bar++)
		for (i = 0; i < 16; i++)
		{
			gint idx = arp_order[i];
			gint key = idx == 3 ? chords[bar][0] + 24 : chords[bar][idx] + 12;

			lrg_song_pattern_add_note (song, "arp", bar * 4.0 + i * 0.25, key, 0.2, i % 4 == 0 ? 0.95 : 0.7);
		}

	/* brass stabs on the one and the and-of-two */
	lrg_song_add_pattern (song, "stabs", 16);
	for (bar = 0; bar < 4; bar++)
		for (i = 0; i < 3; i++)
		{
			lrg_song_pattern_add_note (song, "stabs", bar * 4.0, chords[bar][i] + 12, 0.45, 0.9);
			lrg_song_pattern_add_note (song, "stabs", bar * 4.0 + 2.5, chords[bar][i] + 12, 0.45, 0.75);
		}

	/* the chorus melody: a question (a) and its answer (b) */
	lrg_song_add_pattern (song, "lead-a", 16);
	add_notes (song, "lead-a",
	           "0 E5 1.5 0.95; 1.5 D5 0.5; 2 C5 1; 3 A4 1;"
	           "4 C5 1.5 0.9; 5.5 D5 0.5; 6 E5 1; 7 F5 1;"
	           "8 G5 1.5 0.95; 9.5 F5 0.5; 10 E5 1; 11 C5 1;"
	           "12 D5 2 0.9; 14 B4 1; 15 D5 1");
	lrg_song_add_pattern (song, "lead-b", 16);
	add_notes (song, "lead-b",
	           "0 A5 1.5 1.0; 1.5 G5 0.5; 2 E5 1.5; 3.5 C5 0.5;"
	           "4 F5 1.5 0.95; 5.5 E5 0.5; 6 C5 2;"
	           "8 E5 1; 9 G5 1; 10 C6 1.5 1.0; 11.5 B5 0.5;"
	           "12 B5 1 0.95; 13 A5 1; 14 G5 2");

	/* the riser: one long note under the build */
	lrg_song_add_pattern (song, "swell", 16);
	lrg_song_pattern_add_note (song, "swell", 0, lrg_music_key_from_name ("A3"), 15.75, 1.0);

	/* the impact on the drops */
	lrg_song_add_pattern (song, "hit", 4);
	lrg_song_pattern_add_note (song, "hit", 0, 60, 1, 1.0);

	/* drums, sixteen steps a bar */
	drum_pattern (song, "kick-4", "kick:X...x...x...x...");
	drum_pattern (song, "kick-half", "kick:x.......x.......");
	drum_pattern (song, "groove", "snare:....x.......x...; closed-hat:g.x.g.x.g.x.g.x.");
	drum_pattern (song, "groove-light", "closed-hat:..x...x...x...x.");
	drum_pattern (song, "groove-chorus",
	              "snare:....X.......X...; clap:....x.......x...; closed-hat:x...x...x...x...;"
	              "open-hat:..o...o...o...o.");
	drum_pattern (song, "fill", "snare:....x.......o.x.; high-tom:........X..x....; mid-tom:..........x..x..;"
	              "low-tom:............x..X");
	drum_pattern (song, "roll-8", "snare:o.o.o.o.x.x.x.x.; closed-hat:..x...x...x...x.");
	drum_pattern (song, "roll-16", "snare:xxxxxxxxXXXXXXXX");
	drum_pattern (song, "hats-only", "closed-hat:x.x.x.x.x.x.x.x.; rim:....x.......x...");
	drum_pattern (song, "tom-fill", "high-tom:x.x.x.x.........; mid-tom:........x.x.....; low-tom:............x.X.");
	drum_pattern (song, "crash", "crash:X...............");
}

static void
write_mixer (LrgSong *song)
{
	LrgMixerAsset *m = lrg_song_get_mixer (song);

	lrg_mixer_asset_add_bus (m, "kick", NULL);
	lrg_mixer_asset_add_bus (m, "drums", NULL);
	lrg_mixer_asset_add_bus (m, "bass", NULL);
	lrg_mixer_asset_add_bus (m, "synths", NULL);
	lrg_mixer_asset_add_bus (m, "pad", "synths");
	lrg_mixer_asset_add_bus (m, "lead", "synths");
	lrg_mixer_asset_add_bus (m, "fx", NULL);

	/* levels */
	lrg_mixer_asset_set_bus_volume (m, "kick", 0.0);
	lrg_mixer_asset_set_bus_volume (m, "drums", -3.0);
	lrg_mixer_asset_set_bus_volume (m, "bass", -2.0);
	lrg_mixer_asset_set_bus_volume (m, "pad", -3.0);
	lrg_mixer_asset_set_bus_volume (m, "lead", -1.5);
	lrg_mixer_asset_set_bus_volume (m, "fx", -4.0);

	/* the reverb return and what goes into it */
	fx_add (m, "fx", "reverb", "hall");
	fx_param (m, "fx", "hall", "room-size", 0.88);
	fx_param (m, "fx", "hall", "damping", 0.35);
	fx_param (m, "fx", "hall", "wet", 1.0);
	fx_param (m, "fx", "hall", "dry", 0.0);
	lrg_mixer_asset_set_send (m, "pad", "fx", -8.0);
	lrg_mixer_asset_set_send (m, "lead", "fx", -9.0);
	lrg_mixer_asset_set_send (m, "drums", "fx", -16.0);

	/* per-bus colour */
	fx_add (m, "drums", "compressor", "punch");
	fx_param (m, "drums", "punch", "threshold", -16.0);
	fx_param (m, "drums", "punch", "ratio", 3.0);
	fx_param (m, "drums", "punch", "attack", 0.006);
	fx_param (m, "drums", "punch", "release", 0.1);
	fx_add (m, "bass", "equalizer", "eq");
	fx_param (m, "bass", "eq", "low-cut", 28.0);
	fx_param (m, "bass", "eq", "mid1-freq", 260.0);
	fx_param (m, "bass", "eq", "mid1-gain", -2.5);
	fx_add (m, "pad", "chorus", "chorus");
	fx_add (m, "lead", "echo", "delay");
	fx_param (m, "lead", "delay", "delay", 0.75 * 60.0 / BPM);      /* a dotted eighth */
	fx_param (m, "lead", "delay", "feedback", 0.38);
	fx_param (m, "lead", "delay", "dry", 1.0);
	fx_param (m, "lead", "delay", "wet", 0.28);
	fx_add (m, "synths", "stereo-widener", "wide");
	fx_param (m, "synths", "wide", "width", 1.3);
	fx_param (m, "synths", "wide", "bass-mono", 150.0);

	/* the master chain */
	fx_add (m, "master", "equalizer", "eq");
	fx_param (m, "master", "eq", "low-cut", 25.0);
	fx_param (m, "master", "eq", "high-gain", 1.5);
	fx_add (m, "master", "compressor", "glue");
	fx_param (m, "master", "glue", "threshold", -12.0);
	fx_param (m, "master", "glue", "ratio", 2.0);
	fx_param (m, "master", "glue", "attack", 0.02);
	fx_param (m, "master", "glue", "release", 0.25);
	fx_param (m, "master", "glue", "makeup", 1.0);
	fx_add (m, "master", "limiter", "limit");
	fx_param (m, "master", "limit", "ceiling", -1.0);

	/* sidechain: the kick pumps the pad and the bass */
	lrg_mixer_asset_set_duck (m, "kick", "pad", -30.0, -7.0, 0.003, 0.18);
	lrg_mixer_asset_set_duck (m, "kick", "bass", -30.0, -5.0, 0.002, 0.12);
}

static void
write_arrangement (LrgSong *song)
{
	const gdouble pad_cut[] = { INTRO, 400, VERSE, 2200, OUTRO, 2200, SONG_BEATS, 600 };
	const gdouble arp_cut[] = { 16, 300, VERSE, 1100, BRIDGE, 700, CHORUS2, 1300 };
	const gdouble riser_vol[] = { 112, -34, 127.5, -8 };
	const gdouble fade[] = { 0, 0, 268, 0, SONG_BEATS, -40 };

	/* instruments */
	lrg_song_set_instrument (song, "pad", "synth", "synthwave-pad");
	lrg_song_set_instrument (song, "arp", "synth", "arp-pluck");
	lrg_song_set_instrument (song, "bass", "synth", "synthwave-bass");
	lrg_song_set_instrument (song, "lead", "synth", "synthwave-lead");
	lrg_song_set_instrument (song, "brass", "synth", "brass-stab");
	lrg_song_set_instrument (song, "riser", "synth", "noise-riser");
	lrg_song_set_instrument (song, "kit", "drums", "808");
	lrg_song_set_instrument (song, "impact", "foley", "sfx/impact.rfoley");
	lrg_song_set_instrument_param (song, "impact", "pitch-track", 0.0);

	/* tracks: id, instrument, bus, volume, pan */
	lrg_song_add_track (song, "pad", LRG_SONG_TRACK_INSTRUMENT, "pad");
	lrg_song_set_track (song, "pad", "bus", "pad", 0);
	lrg_song_add_track (song, "arp", LRG_SONG_TRACK_INSTRUMENT, "arp");
	lrg_song_set_track (song, "arp", "bus", "synths", 0);
	lrg_song_set_track (song, "arp", "volume", NULL, -4.0);
	lrg_song_set_track (song, "arp", "pan", NULL, -0.25);
	lrg_song_add_track (song, "bass", LRG_SONG_TRACK_INSTRUMENT, "bass");
	lrg_song_set_track (song, "bass", "bus", "bass", 0);
	lrg_song_add_track (song, "lead", LRG_SONG_TRACK_INSTRUMENT, "lead");
	lrg_song_set_track (song, "lead", "bus", "lead", 0);
	lrg_song_add_track (song, "brass", LRG_SONG_TRACK_INSTRUMENT, "brass");
	lrg_song_set_track (song, "brass", "bus", "synths", 0);
	lrg_song_set_track (song, "brass", "volume", NULL, -5.0);
	lrg_song_set_track (song, "brass", "pan", NULL, 0.2);
	lrg_song_add_track (song, "riser", LRG_SONG_TRACK_INSTRUMENT, "riser");
	lrg_song_set_track (song, "riser", "bus", "synths", 0);
	lrg_song_add_track (song, "kick", LRG_SONG_TRACK_INSTRUMENT, "kit");
	lrg_song_set_track (song, "kick", "bus", "kick", 0);
	lrg_song_add_track (song, "drums", LRG_SONG_TRACK_INSTRUMENT, "kit");
	lrg_song_set_track (song, "drums", "bus", "drums", 0);
	lrg_song_add_track (song, "cymbals", LRG_SONG_TRACK_INSTRUMENT, "kit");
	lrg_song_set_track (song, "cymbals", "bus", "drums", 0);
	lrg_song_set_track (song, "cymbals", "volume", NULL, -4.0);
	lrg_song_add_track (song, "impact", LRG_SONG_TRACK_INSTRUMENT, "impact");
	lrg_song_set_track (song, "impact", "bus", "fx", 0);
	lrg_song_set_track (song, "impact", "volume", NULL, -2.0);

	/* pad through the whole song, the filter opening over the intro */
	clip (song, "pad", "chords", INTRO, SONG_BEATS);
	lrg_song_set_automation (song, "pad", "param.cutoff", pad_cut, G_N_ELEMENTS (pad_cut), FALSE);

	/* arpeggio: second half of the intro and the verse, the bridge, the outro */
	clip (song, "arp", "arp", 16, BUILD);
	clip (song, "arp", "arp", BRIDGE, CHORUS2);
	clip (song, "arp", "arp", OUTRO, SONG_BEATS);
	lrg_song_set_automation (song, "arp", "param.cutoff", arp_cut, G_N_ELEMENTS (arp_cut), FALSE);

	/* bass: octaves in the verse and choruses, roots in the build and bridge */
	clip (song, "bass", "bass-octaves", VERSE, BUILD);
	clip (song, "bass", "bass-roots", BUILD, CHORUS);
	clip (song, "bass", "bass-octaves", CHORUS, BRIDGE);
	clip (song, "bass", "bass-roots", BRIDGE, CHORUS2);
	clip (song, "bass", "bass-octaves", CHORUS2, OUTRO);
	clip (song, "bass", "bass-roots", OUTRO, OUTRO + 16);

	/* lead: question and answer through the choruses */
	clip (song, "lead", "lead-a", CHORUS, CHORUS + 16);
	clip (song, "lead", "lead-b", CHORUS + 16, CHORUS + 32);
	clip (song, "lead", "lead-a", CHORUS + 32, CHORUS + 48);
	clip (song, "lead", "lead-b", CHORUS + 48, BRIDGE);
	clip (song, "lead", "lead-a", CHORUS2, CHORUS2 + 16);
	clip (song, "lead", "lead-b", CHORUS2 + 16, OUTRO);

	/* brass in the choruses */
	clip (song, "brass", "stabs", CHORUS, BRIDGE);
	clip (song, "brass", "stabs", CHORUS2, OUTRO);

	/* the riser over the last half of the build, swelling in */
	clip (song, "riser", "swell", 112, CHORUS);
	lrg_song_set_automation (song, "riser", "volume", riser_vol, G_N_ELEMENTS (riser_vol), FALSE);

	/* kick: four on the floor, half time in the build, a bar of silence
	 * before the chorus drops */
	clip (song, "kick", "kick-4", VERSE, BUILD);
	clip (song, "kick", "kick-half", BUILD, 124);
	clip (song, "kick", "kick-4", CHORUS, BRIDGE);
	clip (song, "kick", "kick-4", CHORUS2, OUTRO + 16);

	/* the rest of the kit */
	clip (song, "drums", "groove-light", 24, VERSE);
	clip (song, "drums", "groove", VERSE, 92);
	clip (song, "drums", "fill", 92, BUILD);
	clip (song, "drums", "groove-light", BUILD, 112);
	clip (song, "drums", "roll-8", 112, 120);
	clip (song, "drums", "roll-16", 120, 124);
	clip (song, "drums", "groove-chorus", CHORUS, 188);
	clip (song, "drums", "fill", 188, BRIDGE);
	clip (song, "drums", "hats-only", BRIDGE, 220);
	clip (song, "drums", "tom-fill", 220, CHORUS2);
	clip (song, "drums", "groove-chorus", CHORUS2, 252);
	clip (song, "drums", "fill", 252, OUTRO);
	clip (song, "drums", "groove-light", OUTRO, OUTRO + 12);

	/* crashes and impacts on the section downbeats */
	clip (song, "cymbals", "crash", VERSE, VERSE + 4);
	clip (song, "cymbals", "crash", CHORUS, CHORUS + 4);
	clip (song, "cymbals", "crash", CHORUS + 32, CHORUS + 36);
	clip (song, "cymbals", "crash", CHORUS2, CHORUS2 + 4);
	clip (song, "cymbals", "crash", OUTRO, OUTRO + 4);
	clip (song, "impact", "hit", CHORUS, CHORUS + 4);
	clip (song, "impact", "hit", CHORUS2, CHORUS2 + 4);

	/* the song-wide fade at the end */
	lrg_song_set_automation (song, NULL, "bus.master.volume", fade, G_N_ELEMENTS (fade), FALSE);

	lrg_song_add_marker (song, INTRO, "Intro");
	lrg_song_add_marker (song, VERSE, "Verse");
	lrg_song_add_marker (song, BUILD, "Build");
	lrg_song_add_marker (song, CHORUS, "Chorus");
	lrg_song_add_marker (song, BRIDGE, "Bridge");
	lrg_song_add_marker (song, CHORUS2, "Chorus 2");
	lrg_song_add_marker (song, OUTRO, "Outro");
}

/* the impact: a falling sub boom, a noise burst closing down, a wash */
static LrgFoleyPatch *
impact_patch (void)
{
	LrgFoleyPatch *p = lrg_foley_patch_new ();
	gint           l;

	g_object_set (p, "name", "impact", "duration", 1.6, "sample-rate", 48000u, NULL);
	l = lrg_foley_patch_add_layer (p, LRG_FOLEY_LAYER_BODY);
	lrg_foley_patch_set_layer_param (p, (guint) l, "freq", 62.0);
	lrg_foley_patch_set_layer_param (p, (guint) l, "sweep", 0.55);
	lrg_foley_patch_set_layer_param (p, (guint) l, "decay", 0.28);
	l = lrg_foley_patch_add_layer (p, LRG_FOLEY_LAYER_NOISE);
	lrg_foley_patch_set_layer_param (p, (guint) l, "length", 1.4);
	lrg_foley_patch_set_layer_param (p, (guint) l, "attack", 0.001);
	lrg_foley_patch_set_layer_param (p, (guint) l, "decay", 0.32);
	lrg_foley_patch_set_layer_param (p, (guint) l, "cutoff", 7000.0);
	lrg_foley_patch_set_layer_param (p, (guint) l, "cutoff-end", 180.0);
	lrg_foley_patch_set_layer_param (p, (guint) l, "amp", 0.55);
	l = lrg_foley_patch_add_layer (p, LRG_FOLEY_LAYER_WASH);
	lrg_foley_patch_set_layer_param (p, (guint) l, "decay", 0.22);
	lrg_foley_patch_set_layer_param (p, (guint) l, "cutoff", 1800.0);
	lrg_foley_patch_set_layer_param (p, (guint) l, "amp", 0.35);
	return p;
}

static LrgSong *
build_song (void)
{
	LrgSong *song = lrg_song_new ();

	g_object_set (song, "title", "Neon Drive", "artist", "libregnum", "bpm", BPM, "sample-rate", 48000u,
	              "seed", 1985u, "length", SONG_BEATS, NULL);
	write_patterns (song);
	write_mixer (song);
	write_arrangement (song);
	return song;
}

/* ==========================================================================
 * Main
 * ========================================================================== */

static void
progress (gdouble  fraction,
          gpointer user_data)
{
	g_printerr ("\r  %s %3.0f%%", (const gchar *) user_data, fraction * 100.0);
}

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext)       ctx = NULL;
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              song = NULL;
	g_autoptr(LrgFoleyPatch)        impact = NULL;
	g_autoptr(LrgSongRenderOptions) ropts = NULL;
	g_autoptr(LrgAudioEncodeOptions) eopts = NULL;
	g_autoptr(LrgWaveData)          mix = NULL;
	g_autoptr(LrgAudioAnalysis)     a = NULL;
	g_autofree gchar               *sfx_dir = NULL;
	g_auto(GStrv)                   formats = NULL;
	guint                           i;

	ctx = g_option_context_new ("- render \"Neon Drive\", a synthwave song made in code");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  synthwave-song                         # wav flac mp3 ogg opus m4a in ./neon-drive\n"
		"  synthwave-song -o out -f mp3,flac      # two formats\n"
		"  synthwave-song --stems                 # plus one WAV per track\n"
		"  synthwave-song -n -o examples/data/music   # only the .rsong, .rfoley and .mid\n"
		"\n"
		"Open neon-drive/neon-drive.rsong in lrgeditor to see and edit the arrangement.\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 1;
	}
	if (opt_license)
	{
		g_print ("synthwave-song - part of libregnum\n"
		         "Copyright 2026 Zach Podbielniak\n"
		         "License: AGPL-3.0-or-later <https://www.gnu.org/licenses/agpl-3.0.html>\n");
		return 0;
	}
	if (opt_out == NULL)
		opt_out = g_strdup ("neon-drive");
	formats = g_strsplit (opt_formats != NULL ? opt_formats : "wav,flac,mp3,ogg,opus,aac", ",", -1);
	if (opt_formats != NULL && g_str_equal (opt_formats, "all"))
	{
		g_strfreev (formats);
		formats = g_strsplit ("wav,aiff,flac,mp3,ogg,opus,aac,alac,wavpack", ",", -1);
	}

	/* the song and its sound effect, saved first: the song names the
	 * .rfoley relative to its own folder */
	sfx_dir = g_build_filename (opt_out, "sfx", NULL);
	if (g_mkdir_with_parents (sfx_dir, 0755) != 0)
	{
		g_printerr ("cannot create %s\n", sfx_dir);
		return 1;
	}
	g_print ("Neon Drive\n");
	impact = impact_patch ();
	song = build_song ();
	g_object_set (song, "base-path", opt_out, NULL);
	{
		g_autofree gchar *sfx = g_build_filename (opt_out, "sfx", "impact.rfoley", NULL);
		g_autofree gchar *rsong = g_build_filename (opt_out, "neon-drive.rsong", NULL);
		g_autofree gchar *mid = g_build_filename (opt_out, "neon-drive.mid", NULL);

		if (!lrg_foley_patch_save_to_file (impact, sfx, &error) || !lrg_song_save_to_file (song, rsong, &error)
		    || !lrg_song_save_midi (song, mid, &error))
		{
			g_printerr ("%s\n", error->message);
			return 1;
		}
		g_print ("  wrote %s\n  wrote %s\n  wrote %s\n", sfx, rsong, mid);
	}
	{
		g_autoptr(GPtrArray) issues = NULL;

		if (!lrg_song_validate (song, &issues))
		{
			for (i = 0; i < issues->len; i++)
				g_printerr ("  %s\n", (const gchar *) g_ptr_array_index (issues, i));
			return 1;
		}
	}
	g_print ("  %g beats at %g BPM: %d:%02d\n", SONG_BEATS, BPM, (gint) lrg_song_get_duration (song) / 60,
	         (gint) lrg_song_get_duration (song) % 60);
	if (opt_no_render)
		return 0;

	/* the mixdown, at streaming loudness */
	ropts = lrg_song_render_options_new ();
	ropts->normalize = LRG_SONG_NORMALIZE_LOUDNESS;
	ropts->target = -14.0;
	ropts->true_peak_ceiling = -1.0;
	mix = lrg_song_render (song, ropts, NULL, progress, (gpointer) "rendering", &error);
	g_printerr ("\n");
	if (mix == NULL)
	{
		g_printerr ("%s\n", error->message);
		return 1;
	}
	a = lrg_audio_analyze_wave (mix);
	g_print ("\n  length          %.2f s\n  loudness        %.1f LUFS (short-term max %.1f)\n"
	         "  true peak       %.2f dBTP\n  loudness range  %.1f LU\n\n",
	         a->duration, a->integrated_lufs, a->short_term_max_lufs, a->true_peak_db, a->loudness_range);

	/* every format asked for that this machine can write */
	eopts = lrg_audio_encode_options_new ();
	eopts->title = g_strdup ("Neon Drive");
	eopts->artist = g_strdup ("libregnum");
	eopts->album = g_strdup ("libregnum examples");
	eopts->date = g_strdup ("2026");
	eopts->bitrate_kbps = 256;
	g_print ("  %-8s %-8s %10s  %s\n", "format", "backend", "size", "file");
	for (i = 0; formats[i] != NULL; i++)
	{
		g_autoptr(GError)  ferr = NULL;
		g_autoptr(GEnumClass) bk = g_type_class_ref (LRG_TYPE_AUDIO_ENCODER_BACKEND);
		LrgAudioFileFormat fmt;
		g_autofree gchar  *name = NULL;
		g_autofree gchar  *path = NULL;
		LrgAudioEncoderBackend backend;
		GStatBuf           st;

		if (!lrg_audio_file_format_from_name (g_strstrip (formats[i]), &fmt))
		{
			g_printerr ("  unknown format '%s'\n", formats[i]);
			continue;
		}
		backend = lrg_audio_encoder_get_backend (fmt);
		if (backend == LRG_AUDIO_ENCODER_BACKEND_NONE)
		{
			g_print ("  %-8s %-8s %10s  (install ffmpeg to write it)\n", formats[i], "none", "-");
			continue;
		}
		eopts->bit_depth = lrg_audio_file_format_is_lossless (fmt) ? 24 : 16;
		name = g_strconcat ("neon-drive", lrg_audio_file_format_get_extension (fmt), NULL);
		path = g_build_filename (opt_out, name, NULL);
		if (!lrg_audio_encode_wave (mix, fmt, eopts, path, NULL, &ferr))
		{
			g_print ("  %-8s failed: %s\n", formats[i], ferr->message);
			continue;
		}
		g_stat (path, &st);
		g_print ("  %-8s %-8s %9.1fM  %s\n", formats[i], g_enum_get_value (bk, backend)->value_nick,
		         st.st_size / 1048576.0, path);
	}

	/* stems: every track alone, at the mix's gain */
	if (opt_stems)
	{
		g_autofree gchar     *stem_dir = g_build_filename (opt_out, "stems", NULL);
		g_autoptr(GPtrArray)  stems = NULL;
		g_auto(GStrv)         names = NULL;

		g_mkdir_with_parents (stem_dir, 0755);
		stems = lrg_song_render_stems (song, ropts, NULL, progress, (gpointer) "stems", &names, &error);
		g_printerr ("\n");
		if (stems == NULL)
		{
			g_printerr ("%s\n", error->message);
			return 1;
		}
		for (i = 0; i < stems->len; i++)
		{
			g_autofree gchar *file = g_strconcat (names[i], ".wav", NULL);
			g_autofree gchar *path = g_build_filename (stem_dir, file, NULL);

			eopts->bit_depth = 24;
			if (lrg_audio_encode_wave (g_ptr_array_index (stems, i), LRG_AUDIO_FILE_FORMAT_WAV, eopts, path, NULL,
			                           &error))
				g_print ("  stem     %s\n", path);
		}
	}
	return 0;
}
