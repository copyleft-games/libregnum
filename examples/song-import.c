/* song-import.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Turns a recording (WAV, FLAC, MP3, Ogg, Opus, M4A, ...) into a .rsong:
 * the audio resampled to the song rate and aligned to its detected tempo
 * and bars on an audio track, its sections as markers, and with
 * --transcribe an estimate of its drums, bass, melody and chords on muted
 * instrument tracks.  Edit the song in lrgeditor (cut, rearrange, add
 * instruments and effects, unmute the estimates), then render it with
 * song-render.
 *
 * Run:   ./build/release/examples/song-import track.mp3 -o imported
 *        ./build/release/examples/song-import track.flac -o imported --transcribe
 *        ./build/release/examples/song-render imported/track.rsong
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

static gchar    *opt_out = NULL;
static gchar    *opt_name = NULL;
static gchar    *opt_title = NULL;
static gchar    *opt_artist = NULL;
static gdouble   opt_bpm = 0.0;
static gdouble   opt_min_bpm = 0.0;
static gdouble   opt_max_bpm = 0.0;
static gint      opt_beats_per_bar = 4;
static gint      opt_rate = 48000;
static gint      opt_section_bars = 4;
static gboolean  opt_transcribe = FALSE;
static gboolean  opt_no_sections = FALSE;
static gboolean  opt_license = FALSE;

static GOptionEntry entries[] = {
	{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &opt_out, "Folder for the song and its audio (default: ./imported)", "DIR" },
	{ "name", 'n', 0, G_OPTION_ARG_STRING, &opt_name, "File name of the song (default: the recording's name)", "NAME" },
	{ "title", 0, 0, G_OPTION_ARG_STRING, &opt_title, "The song's title", "TEXT" },
	{ "artist", 0, 0, G_OPTION_ARG_STRING, &opt_artist, "The song's artist", "TEXT" },
	{ "bpm", 'b', 0, G_OPTION_ARG_DOUBLE, &opt_bpm, "The tempo (default: detect it)", "BPM" },
	{ "min-bpm", 0, 0, G_OPTION_ARG_DOUBLE, &opt_min_bpm, "Slowest tempo to consider (default 70)", "BPM" },
	{ "max-bpm", 0, 0, G_OPTION_ARG_DOUBLE, &opt_max_bpm, "Fastest tempo to consider (default 180)", "BPM" },
	{ "beats-per-bar", 0, 0, G_OPTION_ARG_INT, &opt_beats_per_bar, "Beats in a bar (default 4)", "N" },
	{ "rate", 'r', 0, G_OPTION_ARG_INT, &opt_rate, "The song's sample rate (default 48000)", "HZ" },
	{ "section-bars", 0, 0, G_OPTION_ARG_INT, &opt_section_bars, "Shortest section in bars (default 4)", "N" },
	{ "no-sections", 0, 0, G_OPTION_ARG_NONE, &opt_no_sections, "Do not look for sections", NULL },
	{ "transcribe", 't', 0, G_OPTION_ARG_NONE, &opt_transcribe,
	  "Also estimate drums, bass, melody and chords (muted tracks)", NULL },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ NULL }
};

/* the song's file name: --name, or the recording's name without extension */
static gchar *
song_name (const gchar *input)
{
	g_autofree gchar *base = NULL;
	gchar            *dot;

	if (opt_name != NULL)
		return g_str_has_suffix (opt_name, ".rsong") ? g_strdup (opt_name) : g_strconcat (opt_name, ".rsong", NULL);
	base = g_path_get_basename (input);
	dot = strrchr (base, '.');
	if (dot != NULL && dot != base)
		*dot = '\0';
	return g_strconcat (base, ".rsong", NULL);
}

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext)       ctx = NULL;
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSongImportOptions) opts = NULL;
	g_autoptr(LrgSongImportReport)  r = NULL;
	g_autoptr(LrgSong)              song = NULL;
	g_autoptr(GPtrArray)            issues = NULL;
	g_autofree gchar               *name = NULL;
	g_autofree gchar               *path = NULL;
	guint                           i;

	ctx = g_option_context_new ("RECORDING - make a .rsong of a recording");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  song-import track.mp3 -o imported                 # imported/track.rsong + imported/audio/track.wav\n"
		"  song-import track.flac -o imported --transcribe   # plus muted drum, bass, melody, chord estimates\n"
		"  song-import live.ogg --bpm 92 --no-sections       # a known tempo\n"
		"  song-render imported/track.rsong                  # render it (after editing) to wav and mp3\n"
		"\n"
		"Open the folder in lrgeditor (lrgeditor -p imported) to edit the song.\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("song-import - part of libregnum\n"
		         "Copyright 2026 Zach Podbielniak\n"
		         "License: AGPL-3.0-or-later <https://www.gnu.org/licenses/agpl-3.0.html>\n");
		return 0;
	}
	if (argc != 2)
	{
		g_autofree gchar *help = g_option_context_get_help (ctx, TRUE, NULL);

		g_printerr ("%s", help);
		return 2;
	}
	if (opt_out == NULL)
		opt_out = g_strdup ("imported");
	if (g_mkdir_with_parents (opt_out, 0755) != 0)
	{
		g_printerr ("cannot create %s\n", opt_out);
		return 1;
	}

	/* the import */
	opts = lrg_song_import_options_new ();
	opts->sample_rate = (guint) CLAMP (opt_rate, 8000, 192000);
	opts->bpm = opt_bpm;
	opts->min_bpm = opt_min_bpm;
	opts->max_bpm = opt_max_bpm;
	opts->beats_per_bar = (guint) CLAMP (opt_beats_per_bar, 1, 16);
	opts->sections = !opt_no_sections;
	opts->section_bars = (guint) MAX (opt_section_bars, 1);
	opts->transcribe = opt_transcribe;
	opts->title = g_strdup (opt_title);
	opts->artist = g_strdup (opt_artist);
	g_print ("%s\n", argv[1]);
	song = lrg_song_import_audio (argv[1], opt_out, opts, &r, &error);
	if (song == NULL)
	{
		g_printerr ("  %s\n", error->message);
		return 1;
	}

	/* what was found */
	g_print ("  length      %d:%02d (%u Hz, resampled to %u Hz)\n", (gint) r->duration / 60, (gint) r->duration % 60,
	         r->source_rate, opts->sample_rate);
	g_print ("  tempo       %.2f BPM (%s, confidence %.2f)\n", r->bpm, r->bpm_detected ? "detected" : "given",
	         r->confidence);
	if (r->bpm_detected && r->confidence < 0.3)
		g_print ("              low confidence: if the bars look wrong, give --bpm\n");
	if (r->alternative_bpm > 0.0)
		g_print ("              %.2f BPM fits nearly as well: try --bpm %.2f\n", r->alternative_bpm,
		         r->alternative_bpm);
	else if (r->bpm_detected)
		g_print ("              feels twice too fast or slow? --bpm %.2f or --bpm %.2f\n", r->bpm / 2.0,
		         r->bpm * 2.0);
	g_print ("  first bar   %.3f s into the recording (%.3f s of lead-in added)\n", r->first_beat, r->lead_in);
	g_print ("  key         %s\n", r->key != NULL ? r->key : "?");
	g_print ("  sections    %u\n", r->sections);
	for (i = 0; i < lrg_song_get_n_markers (song); i++)
	{
		const gchar *mname = NULL;
		gdouble      beat = 0;

		lrg_song_get_marker (song, i, &beat, &mname);
		g_print ("    bar %-4.0f %s\n", beat / opts->beats_per_bar + 1, mname);
	}
	if (opt_transcribe)
		g_print ("  transcribed %u drum hits, %u bass notes, %u melody notes, %u chords (muted tracks)\n",
		         r->drum_hits, r->bass_notes, r->melody_notes, r->chords);

	/* the song */
	name = song_name (argv[1]);
	path = g_build_filename (opt_out, name, NULL);
	if (!lrg_song_validate (song, &issues))
	{
		for (i = 0; i < issues->len; i++)
			g_printerr ("  %s\n", (const gchar *) g_ptr_array_index (issues, i));
		return 1;
	}
	if (!lrg_song_save_to_file (song, path, &error))
	{
		g_printerr ("  %s\n", error->message);
		return 1;
	}
	g_print ("  wrote       %s\n  audio       %s/%s\n\nRender it:  song-render %s\n", path, opt_out, r->audio_file,
	         path);
	return 0;
}
