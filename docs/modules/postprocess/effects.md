# Built-in Effects

This page documents all built-in post-processing effects.

## LrgBloom

Adds glow around bright areas. Great for fire, magic, and light sources.

```c
#define LRG_TYPE_BLOOM (lrg_bloom_get_type ())
G_DECLARE_FINAL_TYPE (LrgBloom, lrg_bloom, LRG, BLOOM, LrgPostEffect)
```

### Functions

```c
LrgBloom *lrg_bloom_new (void);

/* Brightness threshold for bloom extraction */
gfloat lrg_bloom_get_threshold (LrgBloom *self);
void lrg_bloom_set_threshold (LrgBloom *self, gfloat threshold);  /* 0.0 - 10.0 */

/* Bloom brightness multiplier */
gfloat lrg_bloom_get_intensity (LrgBloom *self);
void lrg_bloom_set_intensity (LrgBloom *self, gfloat intensity);  /* 0.0 - 5.0 */

/* Blur kernel size */
gfloat lrg_bloom_get_blur_size (LrgBloom *self);
void lrg_bloom_set_blur_size (LrgBloom *self, gfloat blur_size);  /* 1.0 - 20.0 */

/* Number of blur passes */
guint lrg_bloom_get_iterations (LrgBloom *self);
void lrg_bloom_set_iterations (LrgBloom *self, guint iterations);  /* 1 - 8 */

/* Threshold softness */
gfloat lrg_bloom_get_soft_knee (LrgBloom *self);
void lrg_bloom_set_soft_knee (LrgBloom *self, gfloat soft_knee);  /* 0.0 - 1.0 */

/* Color tint */
void lrg_bloom_get_tint (LrgBloom *self, gfloat *r, gfloat *g, gfloat *b);
void lrg_bloom_set_tint (LrgBloom *self, gfloat r, gfloat g, gfloat b);
```

### Example

```c
g_autoptr(LrgBloom) bloom = lrg_bloom_new ();
lrg_bloom_set_threshold (bloom, 0.8f);
lrg_bloom_set_intensity (bloom, 1.5f);
lrg_bloom_set_blur_size (bloom, 5.0f);
lrg_bloom_set_iterations (bloom, 4);
```

---

## LrgVignette

Darkens the edges of the screen for a cinematic look.

```c
#define LRG_TYPE_VIGNETTE (lrg_vignette_get_type ())
G_DECLARE_FINAL_TYPE (LrgVignette, lrg_vignette, LRG, VIGNETTE, LrgPostEffect)
```

### Functions

```c
LrgVignette *lrg_vignette_new (void);

/* Vignette strength */
gfloat lrg_vignette_get_intensity (LrgVignette *self);
void lrg_vignette_set_intensity (LrgVignette *self, gfloat intensity);  /* 0.0 - 1.0 */

/* Inner radius where vignette starts */
gfloat lrg_vignette_get_radius (LrgVignette *self);
void lrg_vignette_set_radius (LrgVignette *self, gfloat radius);  /* 0.0 - 1.0 */

/* Edge softness */
gfloat lrg_vignette_get_smoothness (LrgVignette *self);
void lrg_vignette_set_smoothness (LrgVignette *self, gfloat smoothness);  /* 0.0 - 1.0 */

/* Shape: 1.0 = circular, 0.0 = follows screen aspect */
gfloat lrg_vignette_get_roundness (LrgVignette *self);
void lrg_vignette_set_roundness (LrgVignette *self, gfloat roundness);  /* 0.0 - 1.0 */

/* Vignette color (default: black) */
void lrg_vignette_get_color (LrgVignette *self, gfloat *r, gfloat *g, gfloat *b);
void lrg_vignette_set_color (LrgVignette *self, gfloat r, gfloat g, gfloat b);
```

### Example

```c
g_autoptr(LrgVignette) vignette = lrg_vignette_new ();
lrg_vignette_set_intensity (vignette, 0.4f);
lrg_vignette_set_radius (vignette, 0.8f);
lrg_vignette_set_smoothness (vignette, 0.5f);
```

---

## LrgColorGrade

Professional color grading with lift/gamma/gain controls.

```c
#define LRG_TYPE_COLOR_GRADE (lrg_color_grade_get_type ())
G_DECLARE_FINAL_TYPE (LrgColorGrade, lrg_color_grade, LRG, COLOR_GRADE, LrgPostEffect)
```

### Functions

```c
LrgColorGrade *lrg_color_grade_new (void);

/* Basic adjustments */
gfloat lrg_color_grade_get_exposure (LrgColorGrade *self);
void lrg_color_grade_set_exposure (LrgColorGrade *self, gfloat exposure);  /* -5.0 - 5.0 */

gfloat lrg_color_grade_get_contrast (LrgColorGrade *self);
void lrg_color_grade_set_contrast (LrgColorGrade *self, gfloat contrast);  /* 0.0 - 2.0 */

gfloat lrg_color_grade_get_saturation (LrgColorGrade *self);
void lrg_color_grade_set_saturation (LrgColorGrade *self, gfloat saturation);  /* 0.0 - 2.0 */

/* White balance */
gfloat lrg_color_grade_get_temperature (LrgColorGrade *self);
void lrg_color_grade_set_temperature (LrgColorGrade *self, gfloat temp);  /* -1.0 - 1.0 */

gfloat lrg_color_grade_get_tint (LrgColorGrade *self);
void lrg_color_grade_set_tint (LrgColorGrade *self, gfloat tint);  /* -1.0 - 1.0 */

/* Lift (shadows) */
void lrg_color_grade_get_lift (LrgColorGrade *self, gfloat *r, gfloat *g, gfloat *b);
void lrg_color_grade_set_lift (LrgColorGrade *self, gfloat r, gfloat g, gfloat b);

/* Gamma (midtones) */
void lrg_color_grade_get_gamma (LrgColorGrade *self, gfloat *r, gfloat *g, gfloat *b);
void lrg_color_grade_set_gamma (LrgColorGrade *self, gfloat r, gfloat g, gfloat b);

/* Gain (highlights) */
void lrg_color_grade_get_gain (LrgColorGrade *self, gfloat *r, gfloat *g, gfloat *b);
void lrg_color_grade_set_gain (LrgColorGrade *self, gfloat r, gfloat g, gfloat b);
```

### Example

```c
g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();

/* Warm, slightly desaturated look */
lrg_color_grade_set_temperature (grade, 0.2f);
lrg_color_grade_set_saturation (grade, 0.9f);
lrg_color_grade_set_contrast (grade, 1.1f);

/* Teal shadows, orange highlights (popular film look) */
lrg_color_grade_set_lift (grade, 0.0f, 0.05f, 0.1f);    /* Teal shadows */
lrg_color_grade_set_gain (grade, 0.1f, 0.05f, 0.0f);    /* Orange highlights */
```

---

## LrgFxaa

Fast Approximate Anti-Aliasing for smooth edges.

```c
#define LRG_TYPE_FXAA (lrg_fxaa_get_type ())
G_DECLARE_FINAL_TYPE (LrgFxaa, lrg_fxaa, LRG, FXAA, LrgPostEffect)
```

### Quality Presets

```c
typedef enum {
    LRG_FXAA_QUALITY_LOW,     /* Fastest */
    LRG_FXAA_QUALITY_MEDIUM,
    LRG_FXAA_QUALITY_HIGH,
    LRG_FXAA_QUALITY_ULTRA    /* Best quality */
} LrgFxaaQuality;
```

### Functions

```c
LrgFxaa *lrg_fxaa_new (void);

LrgFxaaQuality lrg_fxaa_get_quality (LrgFxaa *self);
void lrg_fxaa_set_quality (LrgFxaa *self, LrgFxaaQuality quality);

/* Subpixel quality (higher = more blur) */
gfloat lrg_fxaa_get_subpixel_quality (LrgFxaa *self);
void lrg_fxaa_set_subpixel_quality (LrgFxaa *self, gfloat quality);  /* 0.0 - 1.0 */

/* Edge detection threshold */
gfloat lrg_fxaa_get_edge_threshold (LrgFxaa *self);
void lrg_fxaa_set_edge_threshold (LrgFxaa *self, gfloat threshold);  /* 0.0 - 0.5 */

/* Minimum edge threshold (dark pixels) */
gfloat lrg_fxaa_get_edge_threshold_min (LrgFxaa *self);
void lrg_fxaa_set_edge_threshold_min (LrgFxaa *self, gfloat threshold);  /* 0.0 - 0.1 */
```

### Example

```c
g_autoptr(LrgFxaa) fxaa = lrg_fxaa_new ();
lrg_fxaa_set_quality (fxaa, LRG_FXAA_QUALITY_HIGH);
lrg_fxaa_set_subpixel_quality (fxaa, 0.75f);

/* Apply last in the chain */
lrg_post_effect_set_priority (LRG_POST_EFFECT (fxaa), 1000);
```

---

## LrgFilmGrain

Adds animated film grain texture.

```c
#define LRG_TYPE_FILM_GRAIN (lrg_film_grain_get_type ())
G_DECLARE_FINAL_TYPE (LrgFilmGrain, lrg_film_grain, LRG, FILM_GRAIN, LrgPostEffect)
```

### Functions

```c
LrgFilmGrain *lrg_film_grain_new (void);

/* Grain visibility */
gfloat lrg_film_grain_get_intensity (LrgFilmGrain *self);
void lrg_film_grain_set_intensity (LrgFilmGrain *self, gfloat intensity);  /* 0.0 - 1.0 */

/* Grain coarseness */
gfloat lrg_film_grain_get_size (LrgFilmGrain *self);
void lrg_film_grain_set_size (LrgFilmGrain *self, gfloat size);  /* 1.0 - 5.0 */

/* Animation speed */
gfloat lrg_film_grain_get_speed (LrgFilmGrain *self);
void lrg_film_grain_set_speed (LrgFilmGrain *self, gfloat speed);

/* Color or monochrome grain */
gboolean lrg_film_grain_get_colored (LrgFilmGrain *self);
void lrg_film_grain_set_colored (LrgFilmGrain *self, gboolean colored);

/* Grain response to brightness (0 = uniform, 1 = more in darks) */
gfloat lrg_film_grain_get_luminance_response (LrgFilmGrain *self);
void lrg_film_grain_set_luminance_response (LrgFilmGrain *self, gfloat response);

void lrg_film_grain_update (LrgFilmGrain *self, gfloat delta_time);
```

### Example

```c
g_autoptr(LrgFilmGrain) grain = lrg_film_grain_new ();
lrg_film_grain_set_intensity (grain, 0.15f);
lrg_film_grain_set_size (grain, 2.0f);
lrg_film_grain_set_colored (grain, FALSE);  /* Monochrome */
lrg_film_grain_set_luminance_response (grain, 0.5f);
```

---

## LrgScreenShake

Trauma-based screen shake for impacts and explosions.

```c
#define LRG_TYPE_SCREEN_SHAKE (lrg_screen_shake_get_type ())
G_DECLARE_FINAL_TYPE (LrgScreenShake, lrg_screen_shake, LRG, SCREEN_SHAKE, LrgPostEffect)
```

### Functions

```c
LrgScreenShake *lrg_screen_shake_new (void);

/* Add trauma (accumulative) */
void lrg_screen_shake_add_trauma (LrgScreenShake *self, gfloat amount);  /* 0.0 - 1.0 */

/* Direct trauma control */
gfloat lrg_screen_shake_get_trauma (LrgScreenShake *self);
void lrg_screen_shake_set_trauma (LrgScreenShake *self, gfloat trauma);

/* Decay rate per second */
gfloat lrg_screen_shake_get_decay (LrgScreenShake *self);
void lrg_screen_shake_set_decay (LrgScreenShake *self, gfloat decay);

/* Maximum shake offset in pixels */
void lrg_screen_shake_get_max_offset (LrgScreenShake *self, gfloat *x, gfloat *y);
void lrg_screen_shake_set_max_offset (LrgScreenShake *self, gfloat x, gfloat y);

/* Maximum rotation in degrees */
gfloat lrg_screen_shake_get_max_rotation (LrgScreenShake *self);
void lrg_screen_shake_set_max_rotation (LrgScreenShake *self, gfloat degrees);

/* Shake frequency */
gfloat lrg_screen_shake_get_frequency (LrgScreenShake *self);
void lrg_screen_shake_set_frequency (LrgScreenShake *self, gfloat frequency);

/* Update (decays trauma, updates noise) */
void lrg_screen_shake_update (LrgScreenShake *self, gfloat delta_time);

/* Current frame values */
void lrg_screen_shake_get_current_offset (LrgScreenShake *self, gfloat *x, gfloat *y);
gfloat lrg_screen_shake_get_current_rotation (LrgScreenShake *self);
```

### Trauma System

Trauma is squared to calculate shake intensity, creating smooth falloff:

| Trauma | Shake Intensity |
|--------|-----------------|
| 0.0 | 0% |
| 0.5 | 25% |
| 0.7 | 49% |
| 1.0 | 100% |

### Example

```c
g_autoptr(LrgScreenShake) shake = lrg_screen_shake_new ();
lrg_screen_shake_set_max_offset (shake, 15.0f, 15.0f);
lrg_screen_shake_set_max_rotation (shake, 3.0f);
lrg_screen_shake_set_decay (shake, 2.0f);  /* Full decay in 0.5 seconds */

/* On explosion */
lrg_screen_shake_add_trauma (shake, 0.8f);

/* On small hit */
lrg_screen_shake_add_trauma (shake, 0.2f);
```
