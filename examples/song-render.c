/* song-render.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Renders .rsong files to audio: loads each song, checks every reference,
 * mixes it offline at streaming loudness (-14 LUFS, -1 dBTP), prints the
 * measurements and writes the formats asked for.  With --info it only
 * loads, validates and describes.  The songs in examples/data/music/ are
 * all made for it (see examples/data/music/00_index.org).
 *
 * Run:   ./build/release/examples/song-render examples/data/music/neon-drive.rsong
 *        ./build/release/examples/song-render -o out -f mp3,flac examples/data/music/iron-choir.rsong
 *        ./build/release/examples/song-render --info examples/data/music/thunder-grid.rsong
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

static gchar    *opt_out = NULL;
static gchar    *opt_formats = NULL;
static gboolean  opt_stems = FALSE;
static gboolean  opt_info = FALSE;
static gboolean  opt_license = FALSE;

static GOptionEntry entries[] = {
	{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &opt_out, "Folder for the audio files (default: ./songs)", "DIR" },
	{ "formats", 'f', 0, G_OPTION_ARG_STRING, &opt_formats,
	  "Comma-separated formats: wav aiff flac mp3 ogg opus aac alac wavpack, or all (default: wav,mp3)", "LIST" },
	{ "stems", 's', 0, G_OPTION_ARG_NONE, &opt_stems, "Also write one WAV per track", NULL },
	{ "info", 'i', 0, G_OPTION_ARG_NONE, &opt_info, "Only load, validate and describe the songs", NULL },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ NULL }
};

static void
progress (gdouble  fraction,
          gpointer user_data)
{
	g_printerr ("\r  %s %3.0f%%", (const gchar *) user_data, fraction * 100.0);
}

/* the file name without folder and extension: "iron-choir" */
static gchar *
stem_of (const gchar *path)
{
	g_autofree gchar *base = g_path_get_basename (path);
	gchar            *dot = strrchr (base, '.');

	if (dot != NULL)
		*dot = '\0';
	return g_strdup (base);
}

/* writes @mix in every format of @formats into @dir as @stem.<ext> */
static void
write_formats (LrgWaveData            *mix,
               LrgAudioEncodeOptions  *eopts,
               gchar                 **formats,
               const gchar            *dir,
               const gchar            *stem)
{
	g_autoptr(GEnumClass) bk = g_type_class_ref (LRG_TYPE_AUDIO_ENCODER_BACKEND);
	guint                 i;

	for (i = 0; formats[i] != NULL; i++)
	{
		g_autoptr(GError)       error = NULL;
		g_autofree gchar       *name = NULL;
		g_autofree gchar       *path = NULL;
		LrgAudioFileFormat      fmt;
		LrgAudioEncoderBackend  backend;
		GStatBuf                st;

		if (!lrg_audio_file_format_from_name (g_strstrip (formats[i]), &fmt))
		{
			g_printerr ("  unknown format '%s'\n", formats[i]);
			continue;
		}
		backend = lrg_audio_encoder_get_backend (fmt);
		if (backend == LRG_AUDIO_ENCODER_BACKEND_NONE)
		{
			g_print ("  %-8s (install ffmpeg to write it)\n", formats[i]);
			continue;
		}
		eopts->bit_depth = lrg_audio_file_format_is_lossless (fmt) ? 24 : 16;
		name = g_strconcat (stem, lrg_audio_file_format_get_extension (fmt), NULL);
		path = g_build_filename (dir, name, NULL);
		if (!lrg_audio_encode_wave (mix, fmt, eopts, path, NULL, &error))
		{
			g_print ("  %-8s failed: %s\n", formats[i], error->message);
			continue;
		}
		g_stat (path, &st);
		g_print ("  %-8s %-8s %8.1fM  %s\n", formats[i], g_enum_get_value (bk, backend)->value_nick,
		         st.st_size / 1048576.0, path);
	}
}

/* loads, validates and (unless --info) renders and writes one song;
 * FALSE when it does not load or validate */
static gboolean
render_one (const gchar  *file,
            gchar       **formats)
{
	g_autoptr(GError)                error = NULL;
	g_autoptr(LrgSong)               song = NULL;
	g_autoptr(GPtrArray)             issues = NULL;
	g_autoptr(LrgSongRenderOptions)  ropts = NULL;
	g_autoptr(LrgAudioEncodeOptions) eopts = NULL;
	g_autoptr(LrgWaveData)           mix = NULL;
	g_autoptr(LrgAudioAnalysis)      a = NULL;
	g_autofree gchar                *stem = stem_of (file);
	g_autofree gchar                *title = NULL;
	g_autofree gchar                *artist = NULL;
	g_auto(GStrv)                    tracks = NULL;
	gdouble                          bpm = 0;
	guint                            i;

	/* load and check every reference (instruments, patterns, buses) */
	song = lrg_song_new_from_file (file, &error);
	if (song == NULL)
	{
		g_printerr ("%s: %s\n", file, error->message);
		return FALSE;
	}
	g_object_get (song, "title", &title, "artist", &artist, "bpm", &bpm, NULL);
	tracks = lrg_song_list_tracks (song);
	g_print ("%s - %s\n  %g BPM, %d:%02d, %u tracks, %u markers\n", title, artist, bpm,
	         (gint) lrg_song_get_duration (song) / 60, (gint) lrg_song_get_duration (song) % 60,
	         g_strv_length (tracks), lrg_song_get_n_markers (song));
	if (!lrg_song_validate (song, &issues))
	{
		for (i = 0; i < issues->len; i++)
			g_printerr ("  %s\n", (const gchar *) g_ptr_array_index (issues, i));
		return FALSE;
	}
	for (i = 0; issues != NULL && i < issues->len; i++)
		g_print ("  %s\n", (const gchar *) g_ptr_array_index (issues, i));
	if (opt_info)
		return TRUE;

	/* the mixdown, at streaming loudness */
	ropts = lrg_song_render_options_new ();
	ropts->normalize = LRG_SONG_NORMALIZE_LOUDNESS;
	ropts->target = -14.0;
	ropts->true_peak_ceiling = -1.0;
	mix = lrg_song_render (song, ropts, NULL, progress, (gpointer) "rendering", &error);
	g_printerr ("\r");
	if (mix == NULL)
	{
		g_printerr ("  %s\n", error->message);
		return FALSE;
	}
	a = lrg_audio_analyze_wave (mix);
	g_print ("  loudness %.1f LUFS, true peak %.2f dBTP, loudness range %.1f LU\n", a->integrated_lufs,
	         a->true_peak_db, a->loudness_range);

	/* the files, tagged */
	eopts = lrg_audio_encode_options_new ();
	eopts->title = g_strdup (title);
	eopts->artist = g_strdup (artist);
	eopts->album = g_strdup ("libregnum examples");
	eopts->date = g_strdup ("2026");
	eopts->bitrate_kbps = 256;
	write_formats (mix, eopts, formats, opt_out, stem);

	/* stems: every track alone at the mix's gain */
	if (opt_stems)
	{
		g_autofree gchar     *dir = g_build_filename (opt_out, stem, NULL);
		g_autoptr(GPtrArray)  stems = NULL;
		g_auto(GStrv)         names = NULL;

		g_mkdir_with_parents (dir, 0755);
		stems = lrg_song_render_stems (song, ropts, NULL, progress, (gpointer) "stems", &names, &error);
		g_printerr ("\r");
		if (stems == NULL)
		{
			g_printerr ("  %s\n", error->message);
			return FALSE;
		}
		eopts->bit_depth = 24;
		for (i = 0; i < stems->len; i++)
		{
			g_autofree gchar *name = g_strconcat (names[i], ".wav", NULL);
			g_autofree gchar *path = g_build_filename (dir, name, NULL);

			if (lrg_audio_encode_wave (g_ptr_array_index (stems, i), LRG_AUDIO_FILE_FORMAT_WAV, eopts, path, NULL,
			                           &error))
				g_print ("  stem     %s\n", path);
			g_clear_error (&error);
		}
	}
	return TRUE;
}

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_auto(GStrv)             formats = NULL;
	gint                      i;
	gint                      failed = 0;

	ctx = g_option_context_new ("SONG.rsong... - render songs to audio files");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  song-render examples/data/music/*.rsong              # wav and mp3 of every song in ./songs\n"
		"  song-render -o out -f all examples/data/music/night-runner.rsong\n"
		"  song-render -f flac --stems examples/data/music/thunder-grid.rsong\n"
		"  song-render --info examples/data/music/*.rsong       # check and describe, no audio\n"
		"\n"
		"Songs open in lrgeditor's song editor; examples/data/music/00_index.org describes them.\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("song-render - part of libregnum\n"
		         "Copyright 2026 Zach Podbielniak\n"
		         "License: AGPL-3.0-or-later <https://www.gnu.org/licenses/agpl-3.0.html>\n");
		return 0;
	}
	if (argc < 2)
	{
		g_autofree gchar *help = g_option_context_get_help (ctx, TRUE, NULL);

		g_printerr ("%s", help);
		return 2;
	}
	if (opt_out == NULL)
		opt_out = g_strdup ("songs");
	if (opt_formats != NULL && g_str_equal (opt_formats, "all"))
		formats = g_strsplit ("wav,aiff,flac,mp3,ogg,opus,aac,alac,wavpack", ",", -1);
	else
		formats = g_strsplit (opt_formats != NULL ? opt_formats : "wav,mp3", ",", -1);
	if (!opt_info && g_mkdir_with_parents (opt_out, 0755) != 0)
	{
		g_printerr ("cannot create %s\n", opt_out);
		return 1;
	}

	for (i = 1; i < argc; i++)
	{
		if (!render_one (argv[i], formats))
			failed++;
		g_print ("\n");
	}
	return failed > 0 ? 1 : 0;
}
