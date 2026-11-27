/* lrg-lip-sync.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLipSync: timed phonemes -> viseme weight keys.  Holds a sorted list
 * of (start, end, phoneme) entries loaded from YAML or CSV alignments or
 * built from audio loudness, maps ARPAbet phonemes to the visemes of the
 * standard shape sheet and writes cross-faded weight keys into an
 * LrgSkeleton2DClip.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <yaml-glib.h>

#include "animation/lrg-lip-sync.h"
#include "animation/lrg-shape-sheet.h"
#include "audio/lrg-wave-data.h"
#include "lrg-log.h"

G_DEFINE_QUARK (lrg-lip-sync-error-quark, lrg_lip_sync_error)

/* One timed phoneme; the viseme is cached at insertion. */
typedef struct
{
	gdouble    start;
	gdouble    end;
	gchar     *phoneme;
	LrgViseme  viseme;
} Phone;

struct _LrgLipSync
{
	GObject  parent_instance;

	GArray  *phones;   /* Phone, sorted by start */
};

G_DEFINE_FINAL_TYPE (LrgLipSync, lrg_lip_sync, G_TYPE_OBJECT)

static void
phone_clear (gpointer data)
{
	Phone *p = data;

	g_clear_pointer (&p->phoneme, g_free);
}

static GArray *
phone_array_new (void)
{
	GArray *a = g_array_new (FALSE, TRUE, sizeof (Phone));

	g_array_set_clear_func (a, phone_clear);
	return a;
}

static void
lrg_lip_sync_finalize (GObject *object)
{
	LrgLipSync *self = LRG_LIP_SYNC (object);

	g_clear_pointer (&self->phones, g_array_unref);

	G_OBJECT_CLASS (lrg_lip_sync_parent_class)->finalize (object);
}

static void
lrg_lip_sync_class_init (LrgLipSyncClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_lip_sync_finalize;
}

static void
lrg_lip_sync_init (LrgLipSync *self)
{
	self->phones = phone_array_new ();
}

LrgLipSync *
lrg_lip_sync_new (void)
{
	return g_object_new (LRG_TYPE_LIP_SYNC, NULL);
}

/* ------------------------------------------------------------------ */
/* ARPAbet -> viseme                                                   */
/* ------------------------------------------------------------------ */

/* The CMU dictionary's 39 phones (stress digits removed) and the viseme
 * each shows.  Diphthongs take the shape of their first, more open part;
 * TH/DH show the tongue like L; the affricates and sibilants fall in the
 * teeth-together CDGKNRSTYZ group. */
typedef struct
{
	const gchar *phone;
	LrgViseme    viseme;
} PhoneViseme;

static const PhoneViseme arpabet[] = {
	/* vowels */
	{ "AA", LRG_VISEME_A },
	{ "AE", LRG_VISEME_A },
	{ "AH", LRG_VISEME_A },
	{ "AO", LRG_VISEME_O },
	{ "AW", LRG_VISEME_A },
	{ "AY", LRG_VISEME_A },
	{ "EH", LRG_VISEME_E },
	{ "ER", LRG_VISEME_E },
	{ "EY", LRG_VISEME_E },
	{ "IH", LRG_VISEME_I },
	{ "IY", LRG_VISEME_I },
	{ "OW", LRG_VISEME_O },
	{ "OY", LRG_VISEME_O },
	{ "UH", LRG_VISEME_U },
	{ "UW", LRG_VISEME_U },
	/* lips */
	{ "B", LRG_VISEME_MBP },
	{ "M", LRG_VISEME_MBP },
	{ "P", LRG_VISEME_MBP },
	{ "F", LRG_VISEME_FV },
	{ "V", LRG_VISEME_FV },
	{ "W", LRG_VISEME_WQ },
	/* tongue */
	{ "L", LRG_VISEME_L },
	{ "TH", LRG_VISEME_L },
	{ "DH", LRG_VISEME_L },
	/* teeth and the rest of the consonants */
	{ "CH", LRG_VISEME_CDGKNRSTYZ },
	{ "D", LRG_VISEME_CDGKNRSTYZ },
	{ "G", LRG_VISEME_CDGKNRSTYZ },
	{ "HH", LRG_VISEME_CDGKNRSTYZ },
	{ "JH", LRG_VISEME_CDGKNRSTYZ },
	{ "K", LRG_VISEME_CDGKNRSTYZ },
	{ "N", LRG_VISEME_CDGKNRSTYZ },
	{ "NG", LRG_VISEME_CDGKNRSTYZ },
	{ "R", LRG_VISEME_CDGKNRSTYZ },
	{ "S", LRG_VISEME_CDGKNRSTYZ },
	{ "SH", LRG_VISEME_CDGKNRSTYZ },
	{ "T", LRG_VISEME_CDGKNRSTYZ },
	{ "Y", LRG_VISEME_CDGKNRSTYZ },
	{ "Z", LRG_VISEME_CDGKNRSTYZ },
	{ "ZH", LRG_VISEME_CDGKNRSTYZ },
	/* silence and noise labels of the aligners */
	{ "SIL", LRG_VISEME_REST },
	{ "SP", LRG_VISEME_REST },
	{ "SPN", LRG_VISEME_REST },
};

LrgViseme
lrg_lip_sync_phoneme_to_viseme (const gchar *phoneme)
{
	g_autofree gchar *key = NULL;
	gsize             len;
	guint             i;

	if (phoneme == NULL)
		return LRG_VISEME_REST;

	/* normalise: trim, upper case, drop a Gentle position tag (_B, _I,
	 * _E, _S) and then an ARPAbet stress digit */
	key = g_ascii_strup (phoneme, -1);
	g_strstrip (key);
	len = strlen (key);
	if (len >= 2 && key[len - 2] == '_' && strchr ("BIES", key[len - 1]) != NULL)
	{
		key[len - 2] = '\0';
		len -= 2;
	}
	if (len >= 2 && key[len - 1] >= '0' && key[len - 1] <= '2')
		key[--len] = '\0';
	if (len == 0)
		return LRG_VISEME_REST;

	for (i = 0; i < G_N_ELEMENTS (arpabet); i++)
	{
		if (g_str_equal (key, arpabet[i].phone))
			return arpabet[i].viseme;
	}
	lrg_debug (LRG_LOG_DOMAIN_ANIMATION, "lip sync: unknown phoneme \"%s\", using rest", phoneme);
	return LRG_VISEME_REST;
}

/* ------------------------------------------------------------------ */
/* Phoneme list                                                        */
/* ------------------------------------------------------------------ */

static gboolean
valid_span (gdouble start,
            gdouble end)
{
	return isfinite (start) && isfinite (end) && start >= 0.0 && end >= start;
}

/* Inserts after every entry starting at or before @start (stable). */
static void
insert_phone (GArray      *phones,
              gdouble      start,
              gdouble      end,
              const gchar *phoneme)
{
	Phone p;
	guint at;

	for (at = phones->len; at > 0; at--)
	{
		if (g_array_index (phones, Phone, at - 1).start <= start)
			break;
	}
	p.start = start;
	p.end = end;
	p.phoneme = g_strdup (phoneme != NULL ? phoneme : "");
	p.viseme = lrg_lip_sync_phoneme_to_viseme (p.phoneme);
	g_array_insert_val (phones, at, p);
}

gboolean
lrg_lip_sync_add_phoneme (LrgLipSync  *self,
                          gdouble      start,
                          gdouble      end,
                          const gchar *phoneme)
{
	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), FALSE);

	if (!valid_span (start, end))
		return FALSE;
	insert_phone (self->phones, start, end, phoneme);
	return TRUE;
}

guint
lrg_lip_sync_get_phoneme_count (LrgLipSync *self)
{
	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), 0);

	return self->phones->len;
}

gboolean
lrg_lip_sync_get_phoneme (LrgLipSync   *self,
                          guint         index,
                          gdouble      *start,
                          gdouble      *end,
                          const gchar **phoneme,
                          LrgViseme    *viseme)
{
	const Phone *p;

	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), FALSE);

	if (index >= self->phones->len)
		return FALSE;
	p = &g_array_index (self->phones, Phone, index);
	if (start != NULL)
		*start = p->start;
	if (end != NULL)
		*end = p->end;
	if (phoneme != NULL)
		*phoneme = p->phoneme;
	if (viseme != NULL)
		*viseme = p->viseme;
	return TRUE;
}

gdouble
lrg_lip_sync_get_duration (LrgLipSync *self)
{
	gdouble end = 0.0;
	guint   i;

	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), 0.0);

	for (i = 0; i < self->phones->len; i++)
		end = MAX (end, g_array_index (self->phones, Phone, i).end);
	return end;
}

void
lrg_lip_sync_clear (LrgLipSync *self)
{
	g_return_if_fail (LRG_IS_LIP_SYNC (self));

	g_array_set_size (self->phones, 0);
}

/* swaps the loaded list in (the old one is freed) */
static void
replace_phones (LrgLipSync *self,
                GArray     *phones)
{
	g_array_unref (self->phones);
	self->phones = phones;
}

/* ------------------------------------------------------------------ */
/* YAML                                                                */
/* ------------------------------------------------------------------ */

static gboolean
yaml_number (YamlNode     *node,
             const gchar  *source,
             const gchar  *path,
             gdouble      *out,
             GError      **error)
{
	const gchar *text = NULL;
	gchar       *end = NULL;

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
		text = yaml_node_get_scalar (node);
	if (text == NULL || text[0] == '\0')
	{
		g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE, "%s: %s: expected a number", source, path);
		return FALSE;
	}
	*out = g_ascii_strtod (text, &end);
	if (end == NULL || *end != '\0' || !isfinite (*out))
	{
		g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE,
		             "%s: %s: expected a number, got \"%s\"", source, path, text);
		return FALSE;
	}
	return TRUE;
}

/* Parses the document into a fresh array; NULL with @error set. */
static GArray *
parse_yaml (const gchar  *source,
            const gchar  *data,
            gssize        length,
            GError      **error)
{
	static const gchar * const keys[] = { "start", "end", "phoneme", NULL };
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autoptr(GError)     local = NULL;
	g_autoptr(GArray)     phones = phone_array_new ();
	YamlNode             *root;
	YamlSequence         *seq;
	guint                 i;

	if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, &local))
	{
		g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE, "%s: %s", source, local->message);
		return NULL;
	}

	/* an empty document is an empty track */
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) == YAML_NODE_NULL)
		return g_steal_pointer (&phones);
	if (yaml_node_get_node_type (root) != YAML_NODE_SEQUENCE)
	{
		g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE,
		             "%s: /: expected a list of {start, end, phoneme}", source);
		return NULL;
	}
	seq = yaml_node_get_sequence (root);

	/* each entry: a strict {start, end, phoneme} mapping */
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *path = g_strdup_printf ("/%u", i);
		g_autofree gchar *sp = g_strdup_printf ("/%u/start", i);
		g_autofree gchar *ep = g_strdup_printf ("/%u/end", i);
		YamlNode         *node = yaml_sequence_get_element (seq, i);
		YamlMapping      *map;
		YamlNode         *ph;
		const gchar      *label = NULL;
		gdouble           start;
		gdouble           end;
		guint             k;

		if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		{
			g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE,
			             "%s: %s: expected a mapping", source, path);
			return NULL;
		}
		map = yaml_node_get_mapping (node);
		for (k = 0; k < yaml_mapping_get_size (map); k++)
		{
			const gchar *key = yaml_mapping_get_key (map, k);

			if (!g_strv_contains (keys, key))
			{
				g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_INVALID,
				             "%s: %s: unknown key \"%s\"", source, path, key);
				return NULL;
			}
		}
		for (k = 0; keys[k] != NULL; k++)
		{
			if (!yaml_mapping_has_member (map, keys[k]))
			{
				g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_INVALID,
				             "%s: %s: missing key \"%s\"", source, path, keys[k]);
				return NULL;
			}
		}
		if (!yaml_number (yaml_mapping_get_member (map, "start"), source, sp, &start, error) ||
		    !yaml_number (yaml_mapping_get_member (map, "end"), source, ep, &end, error))
			return NULL;
		if (start < 0.0)
		{
			g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_INVALID,
			             "%s: %s: a time must be 0 or more", source, sp);
			return NULL;
		}
		if (end < start)
		{
			g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_INVALID,
			             "%s: %s: the end comes before the start", source, ep);
			return NULL;
		}

		/* a null phoneme is silence; anything else must be a scalar */
		ph = yaml_mapping_get_member (map, "phoneme");
		if (ph != NULL && yaml_node_get_node_type (ph) == YAML_NODE_SCALAR)
			label = yaml_node_get_scalar (ph);
		else if (ph == NULL || yaml_node_get_node_type (ph) != YAML_NODE_NULL)
		{
			g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE,
			             "%s: %s/phoneme: expected a string", source, path);
			return NULL;
		}
		insert_phone (phones, start, end, label != NULL ? label : "sil");
	}
	return g_steal_pointer (&phones);
}

gboolean
lrg_lip_sync_load_yaml (LrgLipSync   *self,
                        const gchar  *data,
                        gssize        length,
                        GError      **error)
{
	GArray *phones;

	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	phones = parse_yaml ("<data>", data, length, error);
	if (phones == NULL)
		return FALSE;
	replace_phones (self, phones);
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* CSV                                                                 */
/* ------------------------------------------------------------------ */

/* a whole trimmed field as a finite number */
static gboolean
csv_number (const gchar *text,
            gdouble     *out)
{
	gchar *end = NULL;

	if (text[0] == '\0')
		return FALSE;
	*out = g_ascii_strtod (text, &end);
	return end != NULL && *end == '\0' && isfinite (*out);
}

static GArray *
parse_csv (const gchar  *source,
           const gchar  *data,
           gssize        length,
           GError      **error)
{
	g_autoptr(GArray)  phones = phone_array_new ();
	g_autofree gchar  *text = NULL;
	g_auto(GStrv)      lines = NULL;
	gboolean           first = TRUE;
	guint              i;

	text = length < 0 ? g_strdup (data != NULL ? data : "") : g_strndup (data, (gsize)length);
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		g_auto(GStrv) fields = NULL;
		gchar        *line = g_strstrip (lines[i]);   /* also drops a \r */
		gdouble       start;
		gdouble       end;
		guint         n;
		guint         k;

		if (line[0] == '\0' || line[0] == '#')
			continue;
		fields = g_strsplit (line, ",", -1);
		n = g_strv_length (fields);
		for (k = 0; k < n; k++)
			g_strstrip (fields[k]);

		/* the first data row may be a header: its start is not a number */
		if (first)
		{
			first = FALSE;
			if (n > 0 && !csv_number (fields[0], &start))
				continue;
		}
		if (n != 3)
		{
			g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE,
			             "%s: line %u: expected 3 fields (start,end,phoneme), got %u", source, i + 1, n);
			return NULL;
		}
		if (!csv_number (fields[0], &start) || !csv_number (fields[1], &end))
		{
			g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE,
			             "%s: line %u: expected numbers for start and end", source, i + 1);
			return NULL;
		}
		if (start < 0.0 || end < start)
		{
			g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_INVALID,
			             "%s: line %u: expected 0 <= start <= end", source, i + 1);
			return NULL;
		}
		insert_phone (phones, start, end, fields[2][0] != '\0' ? fields[2] : "sil");
	}
	return g_steal_pointer (&phones);
}

gboolean
lrg_lip_sync_load_csv (LrgLipSync   *self,
                       const gchar  *data,
                       gssize        length,
                       GError      **error)
{
	GArray *phones;

	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	phones = parse_csv ("<data>", data, length, error);
	if (phones == NULL)
		return FALSE;
	replace_phones (self, phones);
	return TRUE;
}

gboolean
lrg_lip_sync_load_file (LrgLipSync   *self,
                        const gchar  *path,
                        GError      **error)
{
	g_autofree gchar  *text = NULL;
	g_autoptr(GError)  local = NULL;
	gsize              len;
	GArray            *phones;

	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_IO, "%s: %s", path, local->message);
		return FALSE;
	}
	if (g_str_has_suffix (path, ".csv") || g_str_has_suffix (path, ".CSV"))
		phones = parse_csv (path, text, (gssize)len, error);
	else
		phones = parse_yaml (path, text, (gssize)len, error);
	if (phones == NULL)
		return FALSE;
	replace_phones (self, phones);
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* Audio fallback                                                      */
/* ------------------------------------------------------------------ */

LrgLipSync *
lrg_lip_sync_new_from_wave (LrgWaveData *wave,
                            gdouble      fps,
                            gfloat       threshold)
{
	g_autoptr(LrgLipSync) self = NULL;
	g_autofree gfloat    *samples = NULL;
	gsize                 n_samples = 0;
	guint                 channels;
	guint                 rate;
	gsize                 frames;
	gsize                 window;
	gsize                 f0;
	gint                  run_loud = -1;      /* -1 before the first window */
	gdouble               run_start = 0.0;

	g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), NULL);

	if (!lrg_wave_data_is_valid (wave) || !(fps > 0.0) || !isfinite (fps))
		return NULL;
	channels = MAX (lrg_wave_data_get_channels (wave), 1u);
	rate = lrg_wave_data_get_sample_rate (wave);
	samples = lrg_wave_data_get_samples (wave, &n_samples);
	if (samples == NULL || rate == 0)
		return NULL;
	frames = n_samples / channels;
	if (frames == 0)
		return NULL;

	/* windows of 1/fps seconds (at least one frame each) */
	window = MAX ((gsize)((gdouble)rate / fps + 0.5), (gsize)1);
	self = lrg_lip_sync_new ();

	for (f0 = 0; f0 < frames; f0 += window)
	{
		gsize   f1 = MIN (f0 + window, frames);
		gdouble sum = 0.0;
		gdouble rms;
		gint    loud;
		gsize   f;

		/* RMS of the window, each frame downmixed to the channel mean */
		for (f = f0; f < f1; f++)
		{
			gdouble m = 0.0;
			guint   c;

			for (c = 0; c < channels; c++)
				m += samples[f * channels + c];
			m /= (gdouble)channels;
			sum += m * m;
		}
		rms = sqrt (sum / (gdouble)(f1 - f0));
		loud = rms >= (gdouble)threshold ? 1 : 0;

		/* runs of equal loudness become one phoneme each */
		if (loud != run_loud)
		{
			gdouble t = (gdouble)f0 / (gdouble)rate;

			if (run_loud >= 0)
				insert_phone (self->phones, run_start, t, run_loud ? "AA" : "sil");
			run_loud = loud;
			run_start = (gdouble)f0 / (gdouble)rate;
		}
	}
	insert_phone (self->phones, run_start, (gdouble)frames / (gdouble)rate, run_loud ? "AA" : "sil");
	return g_steal_pointer (&self);
}

/* ------------------------------------------------------------------ */
/* Writing keys                                                        */
/* ------------------------------------------------------------------ */

/* One run of a single viseme on the time line. */
typedef struct
{
	gdouble   start;
	gdouble   end;
	LrgViseme viseme;
} Run;

/* Cuts [0, duration] into runs: phonemes in start order, a later one
 * cutting an earlier one short, gaps as rest, equal neighbours merged,
 * empty runs dropped. */
static GArray *
build_runs (LrgLipSync *self)
{
	GArray *runs = g_array_new (FALSE, FALSE, sizeof (Run));
	gdouble cursor = 0.0;
	guint   i;

	for (i = 0; i < self->phones->len; i++)
	{
		const Phone *p = &g_array_index (self->phones, Phone, i);
		gdouble      end = p->end;
		Run          r;

		/* a later phoneme starting inside this one cuts it short */
		if (i + 1 < self->phones->len)
			end = MIN (end, g_array_index (self->phones, Phone, i + 1).start);
		if (p->start > cursor)
		{
			r.start = cursor;
			r.end = p->start;
			r.viseme = LRG_VISEME_REST;
			g_array_append_val (runs, r);
			cursor = p->start;
		}
		if (end <= cursor)
			continue;
		r.start = cursor;
		r.end = end;
		r.viseme = p->viseme;
		g_array_append_val (runs, r);
		cursor = end;
	}

	/* merge neighbours showing the same viseme */
	for (i = 1; i < runs->len;)
	{
		Run *a = &g_array_index (runs, Run, i - 1);
		Run *b = &g_array_index (runs, Run, i);

		if (a->viseme == b->viseme)
		{
			a->end = b->end;
			g_array_remove_index (runs, i);
		}
		else
		{
			i++;
		}
	}
	return runs;
}

guint
lrg_lip_sync_write_clip (LrgLipSync        *self,
                         LrgSkeleton2DClip *clip,
                         const gchar       *slot,
                         gfloat             crossfade)
{
	g_autoptr(GArray) runs = NULL;
	g_autofree gdouble *half = NULL;
	gboolean          seen[LRG_VISEME_CDGKNRSTYZ + 1] = { FALSE };
	gdouble           duration;
	guint             written = 0;
	guint             i;

	g_return_val_if_fail (LRG_IS_LIP_SYNC (self), 0);
	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (clip), 0);
	g_return_val_if_fail (slot != NULL, 0);

	runs = build_runs (self);
	if (runs->len == 0)
		return 0;
	if (!isfinite (crossfade) || crossfade < 0.0f)
		crossfade = 0.0f;

	/* half[i]: half the blend at the boundary before run i (none before
	 * the first), no longer than half of either neighbour */
	half = g_new0 (gdouble, runs->len + 1);
	for (i = 1; i < runs->len; i++)
	{
		const Run *a = &g_array_index (runs, Run, i - 1);
		const Run *b = &g_array_index (runs, Run, i);

		half[i] = MIN ((gdouble)crossfade * 0.5, MIN ((a->end - a->start) * 0.5, (b->end - b->start) * 0.5));
	}

	/* each run: its shape rises to 1 across its start boundary, holds,
	 * and falls to 0 across its end boundary (the next run rises over
	 * the same span, so the weights sum to 1 throughout) */
	for (i = 0; i < runs->len; i++)
	{
		const Run   *r = &g_array_index (runs, Run, i);
		const gchar *shape = lrg_viseme_get_shape_name (r->viseme);
		gboolean     last = i + 1 == runs->len;
		gdouble      in = half[i];
		gdouble      out = last ? 0.0 : half[i + 1];

		if (crossfade <= 0.0f)
		{
			/* hard cuts: on at the start, off at the end; a shape first
			 * seen after time 0 is held at 0 until then (a clip holds its
			 * first key before it) */
			if (!seen[r->viseme] && r->start > 0.0)
			{
				lrg_skeleton2d_clip_add_shape_key (clip, slot, shape, 0.0f, 0.0f, LRG_SKELETON2D_CURVE_STEPPED);
				written++;
			}
			seen[r->viseme] = TRUE;
			lrg_skeleton2d_clip_add_shape_key (clip, slot, shape, (gfloat)r->start, 1.0f,
			                                   LRG_SKELETON2D_CURVE_STEPPED);
			written++;
			if (!last)
			{
				lrg_skeleton2d_clip_add_shape_key (clip, slot, shape, (gfloat)r->end, 0.0f,
				                                   LRG_SKELETON2D_CURVE_STEPPED);
				written++;
			}
			continue;
		}
		if (i > 0)
		{
			lrg_skeleton2d_clip_add_shape_key (clip, slot, shape, (gfloat)(r->start - in), 0.0f,
			                                   LRG_SKELETON2D_CURVE_LINEAR);
			written++;
		}
		lrg_skeleton2d_clip_add_shape_key (clip, slot, shape, (gfloat)(r->start + in), 1.0f,
		                                   LRG_SKELETON2D_CURVE_LINEAR);
		written++;
		if (last)
			continue;
		lrg_skeleton2d_clip_add_shape_key (clip, slot, shape, (gfloat)(r->end - out), 1.0f,
		                                   LRG_SKELETON2D_CURVE_LINEAR);
		lrg_skeleton2d_clip_add_shape_key (clip, slot, shape, (gfloat)(r->end + out), 0.0f,
		                                   LRG_SKELETON2D_CURVE_LINEAR);
		written += 2;
	}

	/* the clip lasts at least as long as the line */
	duration = g_array_index (runs, Run, runs->len - 1).end;
	if ((gdouble)lrg_skeleton2d_clip_get_duration (clip) < duration)
		lrg_skeleton2d_clip_set_duration (clip, (gfloat)duration);
	return written;
}
