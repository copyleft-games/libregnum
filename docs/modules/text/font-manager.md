# Font Manager

`LrgFontManager` is a singleton that handles font loading, caching, and provides access to loaded fonts.

## Type

```c
#define LRG_TYPE_FONT_MANAGER (lrg_font_manager_get_type ())
G_DECLARE_FINAL_TYPE (LrgFontManager, lrg_font_manager, LRG, FONT_MANAGER, GObject)
```

## Singleton Access

```c
LrgFontManager *lrg_font_manager_get_default (void);
```

Returns the singleton instance. The manager is created on first access.

## Font Loading

```c
gboolean lrg_font_manager_load (LrgFontManager *self,
                                 const gchar    *name,
                                 const gchar    *path,
                                 gint            size);

gboolean lrg_font_manager_load_from_memory (LrgFontManager *self,
                                             const gchar    *name,
                                             const guint8   *data,
                                             gsize           size,
                                             gint            font_size);
```

## Font Retrieval

```c
GrlFont *lrg_font_manager_get (LrgFontManager *self, const gchar *name);
GrlFont *lrg_font_manager_get_default_font (LrgFontManager *self);
```

## Management

```c
void lrg_font_manager_unload (LrgFontManager *self, const gchar *name);
void lrg_font_manager_clear (LrgFontManager *self);
gboolean lrg_font_manager_has (LrgFontManager *self, const gchar *name);
GList *lrg_font_manager_get_names (LrgFontManager *self);
```

## Default Font

```c
void lrg_font_manager_set_default_name (LrgFontManager *self, const gchar *name);
const gchar *lrg_font_manager_get_default_name (LrgFontManager *self);
```

---

## Example: Basic Usage

```c
LrgFontManager *fonts = lrg_font_manager_get_default ();

/* Load fonts */
lrg_font_manager_load (fonts, "ui", "fonts/roboto.ttf", 18);
lrg_font_manager_load (fonts, "title", "fonts/roboto-bold.ttf", 32);
lrg_font_manager_load (fonts, "mono", "fonts/firacode.ttf", 14);

/* Set default */
lrg_font_manager_set_default_name (fonts, "ui");

/* Retrieve font */
GrlFont *font = lrg_font_manager_get (fonts, "title");
```

---

## Example: Multiple Sizes

Load the same font at different sizes for different purposes:

```c
LrgFontManager *fonts = lrg_font_manager_get_default ();

/* Same font, different sizes */
lrg_font_manager_load (fonts, "body", "fonts/roboto.ttf", 16);
lrg_font_manager_load (fonts, "heading", "fonts/roboto.ttf", 24);
lrg_font_manager_load (fonts, "title", "fonts/roboto.ttf", 48);

/* Use in rich text */
g_autoptr(LrgRichText) text = lrg_rich_text_new ();
lrg_rich_text_set_font_name (text, "heading");
lrg_rich_text_set_markup (text, "Chapter 1");
```

---

## Example: Embedded Fonts

Load fonts from embedded resources:

```c
#include "embedded_fonts.h"  /* Generated resource file */

LrgFontManager *fonts = lrg_font_manager_get_default ();

/* Load from memory */
lrg_font_manager_load_from_memory (fonts, "ui",
                                    embedded_roboto_ttf,
                                    embedded_roboto_ttf_len,
                                    18);
```

---

## Font Caching

The font manager caches loaded fonts by name. Calling `lrg_font_manager_load()` with the same name will:

1. **Same path/size**: Return success (no-op)
2. **Different path/size**: Unload old font, load new one

To check if a font is already loaded:

```c
if (!lrg_font_manager_has (fonts, "ui"))
{
    lrg_font_manager_load (fonts, "ui", "fonts/roboto.ttf", 18);
}
```

---

## Cleanup

Fonts are automatically cleaned up when the manager is finalized. For manual cleanup:

```c
/* Unload specific font */
lrg_font_manager_unload (fonts, "temp_font");

/* Unload all fonts */
lrg_font_manager_clear (fonts);
```
