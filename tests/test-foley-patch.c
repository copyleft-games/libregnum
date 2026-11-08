/* test-foley-patch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgFoleyPatch: the built-in kinds render the bytes 0.9's
 * lrg_foley_synthesize() produced (SHA-256 of every kind, four variants,
 * two rates, captured before the refactor), .rfoley files round-trip,
 * the new tone and noise layers render, parameters clamp and the loaders
 * report bad input by path.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

typedef struct
{
	guint        kind;
	guint        variant;
	guint        rate;
	const gchar *sha256;
} Pinned;

/* lrg_foley_synthesize () of libregnum 0.9.2 */
static const Pinned pinned[] = {
	{ 0, 0, 44100, "b6aee06b94a22ae6e1a2fe3a9a5ddda03154e79b152d9a1ca88ad0c952d9342a" },
	{ 0, 1, 44100, "e822523470198b2b00d50782c3e828b8c7ebba92655d10454cc154f1a3f20964" },
	{ 0, 2, 44100, "bc48843709c53be0d3f76a5fc22876686538d6bf2da646ab88c0f1d2e07f07bb" },
	{ 0, 3, 44100, "605a446944185a23f31b0c5db154866e569a7a60a43de923e1fe124f334266ac" },
	{ 1, 0, 44100, "a981cc8e9cae5ac9621e8e7b0399c63827fd856b58a32b3c8359ae64ea7f584a" },
	{ 1, 1, 44100, "96014c67c049915071fa9e53f61b15a8b70ead0f7641ba7ce110953a28577b23" },
	{ 1, 2, 44100, "514d1572ae8cae1ac1ccad7022cdc68094255069571bc3458b843c7a44055764" },
	{ 1, 3, 44100, "46a6ad2566ef35b39fad189a14dee99b9382940d3d7fe92deffe57dbe51ed809" },
	{ 2, 0, 44100, "ce82c54d0c7479747b88fd3b076245e44c35d3f8d09aeadcaa89d4c0e17d632d" },
	{ 2, 1, 44100, "275a4090e1e0a98350e0c024d92ddcdb289beeaaa0f872d3296d3f984bee32fc" },
	{ 2, 2, 44100, "c92f63e1d09fc0affa75f78cdf9b14e6bcffcf78c93c5f8ee7e705bce4e0dedf" },
	{ 2, 3, 44100, "5f59f1ec4783f8a935fc0e8c3d7a27d904f6f4a0ff66aaeca1635fa3ad450b5b" },
	{ 3, 0, 44100, "66e6a4d25ecebbc97c3fc7f28954a6c03e69b9cf980d93243211cc83436b2aa1" },
	{ 3, 1, 44100, "64169af0f96b7a37cc35543dfa9799409d16ef41b03e4e22e0f51dfbb235dc61" },
	{ 3, 2, 44100, "d2caee0ebf2003cf6a5ef7dfd43bdc5c193a7511e8eb0432fc20d91942199606" },
	{ 3, 3, 44100, "8ce78bd090faf807c3c1cd157da80e560888b23cc9af413beff5069c003e7b2e" },
	{ 4, 0, 44100, "a3c45e6690c0216ddda891e9c0600e3522fca53dd4059d4418cb5360a30a6e94" },
	{ 4, 1, 44100, "b3f1649dedd2f6676efa922098765d8aa71b1b702de44ba6e758d591bf26f0a5" },
	{ 4, 2, 44100, "1b4e1d47ac0584ba6838fa106cbaba0f33e0063c46b04b1696c78586934e0dd2" },
	{ 4, 3, 44100, "fe6fa37ec732eef524b52deebc32f1514ae96f0276c856ffc2c35cb6db3ab946" },
	{ 5, 0, 44100, "1cd29343dc4d6cbe9a8e9a3f4bc19fa265556a52ce5cee1751d5a44553f04637" },
	{ 5, 1, 44100, "52762bc9d38e099a218343ae971f369a7bacc5de0fc26c45bef2dab4d645e472" },
	{ 5, 2, 44100, "5222b5f50777755949937fb5a9a171ee7ddfa35367a0e3017a62a2397e4c81e8" },
	{ 5, 3, 44100, "010f825a895086df2aaff96840730ed66a895eda145dd4a180ba272134bd7fbe" },
	{ 6, 0, 44100, "84e8ce0824b964ac467900182812a3a1bfc7e32511d1ddcc2fd581bdceb3b158" },
	{ 6, 1, 44100, "2c190f7eb74d7d0fd206940a8dd8a5704c2cc5df8df4f8372612d9a46b59a527" },
	{ 6, 2, 44100, "78b5d346d84e5695ebbecbccc5149f8020cf68bec0b5a9e44bc62c97631ea38e" },
	{ 6, 3, 44100, "d3df71fb4d31c5aad474eca39619852d45c2a864c7f0d3b44fd076ca259df00d" },
	{ 7, 0, 44100, "40815a39b2da9ea9643e5613a9312b85e6a8c65fe1af44e187ddf77881898249" },
	{ 7, 1, 44100, "d919fb739515eb63cf5b8c9f84153d17bf5ce7d6e841ea77a5c06f6430463956" },
	{ 7, 2, 44100, "ea83b0195d23dcba2fba25de113c04e92c41d1d51a20260612d151eeda58a61f" },
	{ 7, 3, 44100, "d31125e2fc60f4627691314a4798a7804ffed50a2d0f24ef75af6241ff192f94" },
	{ 8, 0, 44100, "6e4a497e4c789d4f9ed35a644d67a4564e2660f586a389b569bb9c9ecf205474" },
	{ 8, 1, 44100, "be58bbd71d279167a7625dc11d1247b9bc0ad741e64a69f4217b75ae02d5fa2f" },
	{ 8, 2, 44100, "3d9d5a315efdd6643351ffc606b844734bb1b58ddf87de73c1d3dc45490e8d1c" },
	{ 8, 3, 44100, "61f0f2cf4061f4fabbeac7d3230187fd0c92b4d54bc4d9274522831d2bdb9acd" },
	{ 9, 0, 44100, "1ea20eddb0cc9077aeb6cfe0319ca414447baacf38baada9c1b9882ce0d5006c" },
	{ 9, 1, 44100, "3baccb9f5beef66e3a8f2e60563202cd95f3f813033c67cc5b7c5cd2e8923665" },
	{ 9, 2, 44100, "ce88039ade4ac8c0af702d59fb3ed692ecbdeda8c5186f6f08edb22b001b6d36" },
	{ 9, 3, 44100, "2727649f10b6c6b0e1079eb07cade2a642545aff6abf0d9a0e080ab2290ee693" },
	{ 0, 0, 22050, "3f055d89f8f606b32e9d30006f875734b0e2a03f73bdb6b67b2a24e3f35e3ea4" },
	{ 0, 1, 22050, "af2f076bf830b5fcff0affa39afb2099319b62010f720d6c40ce7380af0327f8" },
	{ 0, 2, 22050, "8208adb3d756c7173ccb97259efb83a01634f99ed3bf23dc7fa6b1d63483d6e6" },
	{ 0, 3, 22050, "6d3b15a6e1c25fd53a43badbf3bfa6b087955053aaba6003d69e4c316afab9ce" },
	{ 1, 0, 22050, "1612f62cb5fbc26a0275db4a96ff9da9631657c255f99755fd33e36d8fc17bd6" },
	{ 1, 1, 22050, "dd3f7e74877a29356758ad9d532cda18e3a4985137157b85bc410e44028e9494" },
	{ 1, 2, 22050, "af0ecd18c41b55a854636eb457c6e34d06f9c1c4655360e0761a9ec1fd8e8b75" },
	{ 1, 3, 22050, "f5e9526b8045f39b581364986de01162492865bd87067332f718f73efb11ba40" },
	{ 2, 0, 22050, "2eceee5f208c0089a8c85676ddb9a50462305992c3ef035eb0067da59e1f8ae9" },
	{ 2, 1, 22050, "26ba03de8c62ba9a23d056f8136e5f9cc462096ac44db25b138f160ad2050c26" },
	{ 2, 2, 22050, "59a8609d1bf6c80519f3f5793f0d2972ae568259631cb81fc6e7ac0e990e4f1b" },
	{ 2, 3, 22050, "714b5f61c5aa5e78572e099b9e5561b9758c7df29ea931ead59f49989a1c317f" },
	{ 3, 0, 22050, "d8e2233ee225c1a19b7fdfd55f22af4949fc2eab5c45e2a56d32b6e6f2320208" },
	{ 3, 1, 22050, "d96d03d3fc4e7c0cf7e8f56904f1931ffa7cf7ed50f01befe09f557d5ce24c00" },
	{ 3, 2, 22050, "0f872f5f83766a35c03d5b4aeec6fa80385710ac47d3ded9e62bcb69058ad93b" },
	{ 3, 3, 22050, "4f00949798dfbe68708a8724f26815175f755b562ea897a7047d9dc69114abae" },
	{ 4, 0, 22050, "6ac90fb2553340b314b3f9d341a12a191740a5d92d69775935e36c0748270a0d" },
	{ 4, 1, 22050, "9d5c56515d54bd0c4fcecc30b4942359e58d223013ee8fae69da66a9fb186f04" },
	{ 4, 2, 22050, "5143e2e81c1229f29a171975464e736ff38cf0d733ed6f3ef9524c8d4936ca00" },
	{ 4, 3, 22050, "61a4539a6629f1dc7218b17c1c61243bc3167ee316bcd6285ca60f079a73c21e" },
	{ 5, 0, 22050, "a068d792c0bf54b04c0fee8678eabe248a160cb404c107f29d35c9d5cc0ae192" },
	{ 5, 1, 22050, "c20868cfe14fee1578eb326af84f67b476536aa321b25dfaa70d0d0e35f9eda5" },
	{ 5, 2, 22050, "a1ca8e488fca29d978d9be3a79da296a0d404298ceb3edbdac5c15d0018a9bc1" },
	{ 5, 3, 22050, "ff71ce52c3503d44a988749a2e0bde44822c5dd99bd6db8738742cecc240d459" },
	{ 6, 0, 22050, "b2c62b9c6717010d5ffc5c5c8fc582860734d9d36953d0f306f3ea5e199f0c78" },
	{ 6, 1, 22050, "22a811b56f22e54abc7a11b362b74f06bff401d39c54755dda71fb9a64e754a8" },
	{ 6, 2, 22050, "57af6c73368e238d433e18c0a4db0d632a7a1f430dae3237f4bcf4cde5a9536c" },
	{ 6, 3, 22050, "7ffb7b812b619892577c6543fe22288464f1588867e49c490eb6a852971cc820" },
	{ 7, 0, 22050, "f1bd0d4e289a9383d9ef1d584f5aea61665ea44908d03adae5a56f27d050847e" },
	{ 7, 1, 22050, "0d138d7825a954c187b286b40954e30328e3472d8d5d381d4148b1814e628a8c" },
	{ 7, 2, 22050, "691ba0974d4dba6e5232ea34c02fcab529e2904f5ec1e02f620099849bd1cfc4" },
	{ 7, 3, 22050, "0eceaf6ad9bd964abd4a020938b4599c6dce5b0ea0cff6e7161511fa3a0af0cb" },
	{ 8, 0, 22050, "6e82e6f6b8e6c30974e7fd4251474d55f936f4a6a79d2b22c01c895810971b05" },
	{ 8, 1, 22050, "b6cd6491b3f1609fc94b3d32986ecfa6bc8abaad75bc9cfd01e1c5e6a2d0e080" },
	{ 8, 2, 22050, "2efb1f08926fa4bd3c9d253edaa32addb1bbdd31918f7b3b1b1c4de0c4823cb8" },
	{ 8, 3, 22050, "d925784014b7765ae617c594aa24e64a2322ae752d33f6d9d672fed187684271" },
	{ 9, 0, 22050, "39c507e38bb596491c1ceb170503b65f04b8d4a8a09037f78c1b240054e0e1a0" },
	{ 9, 1, 22050, "ed9609064b9d72176d18f6290ea1c06b346b72ed93dd83ad3900ffb2824824d9" },
	{ 9, 2, 22050, "15a281430c9c746a51ef84a889c9b615dac85e4788c9a399bc17535105359f61" },
	{ 9, 3, 22050, "711df7f05fe71441038ab6ae604ebcabbb5a908c0ccad0c26499fb1b37970f61" },
};

static void
test_pinned_bytes (void)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (pinned); i++)
	{
		g_autoptr(GError)        error = NULL;
		g_autoptr(GBytes)        legacy = NULL;
		g_autoptr(GBytes)        patched = NULL;
		g_autoptr(LrgFoleyPatch) patch = NULL;
		g_autofree gchar        *sum = NULL;

		legacy = lrg_foley_synthesize ((LrgFoleyKind) pinned[i].kind, pinned[i].variant, pinned[i].rate, &error);
		g_assert_no_error (error);
		sum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, legacy);
		g_assert_cmpstr (sum, ==, pinned[i].sha256);

		patch = lrg_foley_patch_new_from_kind ((LrgFoleyKind) pinned[i].kind, pinned[i].variant, pinned[i].rate,
		                                       &error);
		g_assert_no_error (error);
		patched = lrg_foley_patch_render_wav (patch, &error);
		g_assert_no_error (error);
		g_assert_true (g_bytes_equal (legacy, patched));
	}
}

/* a patch written and read back writes the same text and the same sound */
static void
test_round_trip (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgFoleyPatch) a = NULL;
	g_autoptr(LrgFoleyPatch) b = NULL;
	g_autofree gchar        *text_a = NULL;
	g_autofree gchar        *text_b = NULL;
	g_autofree gfloat       *pcm_a = NULL;
	g_autofree gfloat       *pcm_b = NULL;
	gsize                    na = 0, nb = 0, i;

	a = lrg_foley_patch_new_from_kind (LRG_FOLEY_FOOTSTEP_GRAVEL, 3, 0, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_foley_patch_get_n_layers (a), >, 7);
	text_a = lrg_foley_patch_to_data (a, NULL, &error);
	g_assert_no_error (error);
	b = lrg_foley_patch_new_from_data (text_a, -1, &error);
	g_assert_no_error (error);
	text_b = lrg_foley_patch_to_data (b, NULL, &error);
	g_assert_cmpstr (text_a, ==, text_b);

	/* the text keeps every float exactly, so the sound is the same */
	pcm_a = lrg_foley_patch_render (a, &na, &error);
	pcm_b = lrg_foley_patch_render (b, &nb, &error);
	g_assert_cmpuint (na, ==, nb);
	for (i = 0; i < na; i++)
		g_assert_cmpfloat_with_epsilon (pcm_a[i], pcm_b[i], 1e-6);
}

static void
test_tone_and_noise (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgFoleyPatch) patch = lrg_foley_patch_new ();
	g_autoptr(LrgWaveData)   wave = NULL;
	g_autofree gfloat       *pcm = NULL;
	gsize                    n = 0, i;
	gint                     tone, burst;
	gdouble                  peak = 0.0;

	/* a laser: a square sweeping 1800 -> 200 Hz, and a noise whoosh */
	tone = lrg_foley_patch_add_layer (patch, LRG_FOLEY_LAYER_TONE);
	g_assert_cmpint (tone, ==, 0);
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) tone, "freq", 1800.0));
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) tone, "freq-end", 200.0));
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) tone, "waveform", 2.0));
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) tone, "decay", 0.1));
	burst = lrg_foley_patch_add_layer (patch, LRG_FOLEY_LAYER_NOISE);
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) burst, "cutoff-end", 300.0));
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) burst, "amp", 0.5));

	pcm = lrg_foley_patch_render (patch, &n, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (n, ==, 22050);
	for (i = 0; i < n; i++)
	{
		g_assert_true (isfinite (pcm[i]));
		peak = MAX (peak, fabs (pcm[i]));
	}
	/* normalised to the patch peak */
	g_assert_cmpfloat_with_epsilon (peak, 0.8, 1e-4);

	/* the same patch renders the same samples */
	{
		g_autofree gfloat *again = lrg_foley_patch_render (patch, &n, &error);

		g_assert_cmpmem (pcm, n * sizeof (gfloat), again, n * sizeof (gfloat));
	}

	wave = lrg_foley_patch_render_wave (patch, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_wave_data_get_frame_count (wave), ==, 22050);
	g_assert_cmpuint (lrg_wave_data_get_channels (wave), ==, 1);
}

static void
test_scatter_and_seed (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgFoleyPatch) patch = lrg_foley_patch_new ();
	g_autofree gfloat       *one = NULL;
	g_autofree gfloat       *two = NULL;
	gsize                    n = 0;
	gint                     layer;

	layer = lrg_foley_patch_add_layer (patch, LRG_FOLEY_LAYER_GRAIN);
	lrg_foley_patch_set_layer_param (patch, (guint) layer, "count", 12.0);
	lrg_foley_patch_set_layer_param (patch, (guint) layer, "spread", 0.4);
	lrg_foley_patch_set_layer_param (patch, (guint) layer, "jitter", 0.5);
	one = lrg_foley_patch_render (patch, &n, &error);
	g_assert_no_error (error);

	/* another patch seed scatters differently */
	g_object_set (patch, "seed", 99u, NULL);
	two = lrg_foley_patch_render (patch, &n, &error);
	g_assert_true (memcmp (one, two, n * sizeof (gfloat)) != 0);
}

static void
test_params (void)
{
	g_autoptr(LrgFoleyPatch) patch = lrg_foley_patch_new ();
	g_auto(GStrv)            names = NULL;
	gdouble                  min = 0, max = 0, def = 0;
	const gchar             *blurb = NULL;
	gint                     layer;

	layer = lrg_foley_patch_add_layer (patch, LRG_FOLEY_LAYER_BODY);
	/* clamped, not refused */
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) layer, "freq", 1e9));
	g_assert_cmpfloat (lrg_foley_patch_get_layer_param (patch, (guint) layer, "freq"), ==, 20000.0);
	/* unknown parameter, layer and non-finite value */
	g_assert_false (lrg_foley_patch_set_layer_param (patch, (guint) layer, "nope", 1.0));
	g_assert_false (lrg_foley_patch_set_layer_param (patch, 9, "freq", 1.0));
	g_assert_false (lrg_foley_patch_set_layer_param (patch, (guint) layer, "freq", NAN));
	g_assert_true (isnan (lrg_foley_patch_get_layer_param (patch, (guint) layer, "nope")));
	/* the 'at' of a body is not rounded (only integer parameters are) */
	g_assert_true (lrg_foley_patch_set_layer_param (patch, (guint) layer, "at", 0.125));
	g_assert_cmpfloat (lrg_foley_patch_get_layer_param (patch, (guint) layer, "at"), ==, 0.125);

	names = lrg_foley_layer_type_list_params (LRG_FOLEY_LAYER_TONE);
	g_assert_cmpstr (names[0], ==, "at");
	g_assert_true (g_strv_contains ((const gchar * const *) names, "freq-end"));
	g_assert_true (lrg_foley_layer_type_get_param_info (LRG_FOLEY_LAYER_TONE, "waveform", &min, &max, &def, &blurb));
	g_assert_cmpfloat (max, ==, 3.0);
	g_assert_nonnull (blurb);

	/* globals */
	g_assert_true (lrg_foley_patch_set_global (patch, "duration", 100.0));
	g_assert_cmpfloat (lrg_foley_patch_get_duration (patch), ==, LRG_FOLEY_PATCH_MAX_DURATION);
	g_assert_false (lrg_foley_patch_set_global (patch, "tempo", 1.0));

	/* layers move and go */
	lrg_foley_patch_add_layer (patch, LRG_FOLEY_LAYER_WASH);
	g_assert_true (lrg_foley_patch_move_layer (patch, 1, 0));
	g_assert_cmpint (lrg_foley_patch_get_layer_type (patch, 0), ==, LRG_FOLEY_LAYER_WASH);
	g_assert_true (lrg_foley_patch_remove_layer (patch, 0));
	g_assert_false (lrg_foley_patch_remove_layer (patch, 5));
	g_assert_cmpuint (lrg_foley_patch_get_n_layers (patch), ==, 1);
}

static void
test_errors (void)
{
	g_autoptr(GError) error = NULL;
	LrgFoleyPatch    *patch;

	patch = lrg_foley_patch_new_from_data ("layers:\n  - type: laser\n", -1, &error);
	g_assert_null (patch);
	g_assert_error (error, LRG_FOLEY_PATCH_ERROR, LRG_FOLEY_PATCH_ERROR_INVALID_VALUE);
	g_assert_nonnull (strstr (error->message, "layers[0].type"));
	g_clear_error (&error);

	patch = lrg_foley_patch_new_from_data ("layers:\n  - type: body\n    freq: -5\n", -1, &error);
	g_assert_null (patch);
	g_assert_nonnull (strstr (error->message, "layers[0].freq"));
	g_clear_error (&error);

	patch = lrg_foley_patch_new_from_data ("layers:\n  - type: body\n    bogus: 1\n", -1, &error);
	g_assert_null (patch);
	g_assert_nonnull (strstr (error->message, "bogus"));
	g_clear_error (&error);

	patch = lrg_foley_patch_new_from_data ("{ unclosed", -1, &error);
	g_assert_null (patch);
	g_assert_error (error, LRG_FOLEY_PATCH_ERROR, LRG_FOLEY_PATCH_ERROR_PARSE);
	g_clear_error (&error);

	patch = lrg_foley_patch_new_from_kind ((LrgFoleyKind) 99, 0, 0, &error);
	g_assert_null (patch);
	g_clear_error (&error);
	patch = lrg_foley_patch_new_from_kind (LRG_FOLEY_FOOTSTEP_DIRT, 0, 100, &error);
	g_assert_null (patch);
}

static void
test_file (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgFoleyPatch) a = lrg_foley_patch_new_from_kind (LRG_FOLEY_RAIN_LOOP, 1, 22050, NULL);
	g_autoptr(LrgFoleyPatch) b = NULL;
	g_autofree gchar        *dir = g_dir_make_tmp ("lrg-foley-XXXXXX", NULL);
	g_autofree gchar        *path = g_build_filename (dir, "rain.rfoley", NULL);
	g_autoptr(GBytes)        wa = NULL;
	g_autoptr(GBytes)        wb = NULL;

	g_assert_true (lrg_foley_patch_save_to_file (a, path, &error));
	g_assert_no_error (error);
	b = lrg_foley_patch_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_foley_patch_get_name (b), ==, "rain-loop");
	wa = lrg_foley_patch_render_wav (a, NULL);
	wb = lrg_foley_patch_render_wav (b, NULL);
	g_assert_cmpuint (g_bytes_get_size (wa), ==, g_bytes_get_size (wb));
	g_unlink (path);
	g_rmdir (dir);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/foley-patch/pinned-bytes", test_pinned_bytes);
	g_test_add_func ("/foley-patch/round-trip", test_round_trip);
	g_test_add_func ("/foley-patch/tone-and-noise", test_tone_and_noise);
	g_test_add_func ("/foley-patch/scatter-and-seed", test_scatter_and_seed);
	g_test_add_func ("/foley-patch/params", test_params);
	g_test_add_func ("/foley-patch/errors", test_errors);
	g_test_add_func ("/foley-patch/file", test_file);
	return g_test_run ();
}
