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

---

## Automatic System Font Detection

When `lrg_font_manager_initialize()` is called (typically via `LrgTheme`), the manager automatically searches for common system fonts and loads them at preset sizes.

### Initialization

```c
LrgFontManager *fonts = lrg_font_manager_get_default ();
g_autoptr(GError) error = NULL;

if (!lrg_font_manager_initialize (fonts, &error))
{
    /* Not fatal - raylib's default bitmap font is used as fallback */
    g_warning ("Font init failed: %s", error->message);
}
```

### Automatic Font Names

After initialization, these fonts are available (if a system font was found):

| Name | Size | Purpose |
|------|------|---------|
| `ui-small` | 12px | Small UI text, tooltips |
| `ui-normal` | 16px | Default UI text (set as default) |
| `ui-large` | 24px | Headers, titles |

```c
/* Get the auto-loaded default font */
GrlFont *font = lrg_font_manager_get_default_font (fonts);

/* Or by name */
GrlFont *small = lrg_font_manager_get_font (fonts, "ui-small");
GrlFont *large = lrg_font_manager_get_font (fonts, "ui-large");
```

---

## System Font Search Paths

The manager searches platform-specific paths for common fonts.

### Linux

Search paths (in order):

```
/usr/share/fonts/liberation-sans-fonts    # Fedora
/usr/share/fonts/liberation-sans          # Debian/Ubuntu
/usr/share/fonts/truetype/liberation      # Debian/Ubuntu alternative
/usr/share/fonts/google-noto-vf           # Fedora Noto variable fonts
/usr/share/fonts/google-noto              # Other distros
/usr/share/fonts/truetype/noto            # Debian/Ubuntu
/usr/share/fonts/dejavu-sans-fonts        # Fedora DejaVu
/usr/share/fonts/truetype/dejavu          # Debian/Ubuntu
/usr/share/fonts/TTF                      # Arch Linux
/usr/share/fonts/liberation               # Generic
/usr/share/fonts/noto                     # Generic
```

Font filenames searched:

```
LiberationSans-Regular.ttf
NotoSans-Regular.ttf
DejaVuSans.ttf
```

### Windows

Search path:

```
C:/Windows/Fonts
```

Font filenames searched:

```
segoeui.ttf     # Segoe UI (preferred)
arial.ttf       # Arial
verdana.ttf     # Verdana
```

---

## Font Fallback Chain

When rendering text, libregnum uses a fallback chain:

1. **Widget-specific font** - Set via `lrg_label_set_font()`
2. **Theme default font** - From `lrg_theme_get_default_font()`
3. **raylib default** - Built-in bitmap font (last resort)

### LrgLabel Example

```c
g_autoptr(LrgLabel) label = lrg_label_new ("Hello World");

/* Option 1: Use theme default (automatic) */
/* No font set - uses theme's default font */

/* Option 2: Use a specific loaded font */
GrlFont *custom = lrg_font_manager_get_font (fonts, "my-custom");
lrg_label_set_font (label, custom);

/* Option 3: Let it fall back to raylib default */
/* If no fonts are loaded, raylib's bitmap font is used */
```

### LrgTheme Integration

`LrgTheme` lazy-initializes the font manager when first accessed:

```c
LrgTheme *theme = lrg_theme_get_default ();

/* This triggers font manager initialization if not already done */
GrlFont *font = lrg_theme_get_default_font (theme);

/* Returns NULL if no system fonts found - widgets fall back to raylib */
if (font == NULL)
{
    g_message ("Using raylib default font");
}
```

---

## Adding Custom Font Paths

If your distribution uses different font paths, you have two options:

### Option 1: Load Fonts Explicitly

Skip auto-detection and load fonts yourself:

```c
LrgFontManager *fonts = lrg_font_manager_get_default ();

/* Load from your known path */
lrg_font_manager_load_font (fonts, "ui-normal",
                            "/custom/path/MyFont.ttf",
                            16, NULL);

/* Set as default */
lrg_font_manager_set_default_font_name (fonts, "ui-normal");
```

### Option 2: Modify Source (for engine developers)

Edit `src/text/lrg-font-manager.c` and add paths to `FONT_SEARCH_PATHS`:

```c
static const gchar * const FONT_SEARCH_PATHS[] = {
    "/your/custom/font/path",        /* Add your path here */
    "/usr/share/fonts/liberation-sans-fonts",
    /* ... existing paths ... */
    NULL
};
```

Add font filenames to `FONT_CANDIDATES` if needed:

```c
static const gchar * const FONT_CANDIDATES[] = {
    "YourFont-Regular.ttf",          /* Add your font here */
    "LiberationSans-Regular.ttf",
    /* ... existing fonts ... */
    NULL
};
```

---

## Debugging Font Loading

Enable debug output to see font detection:

```bash
G_MESSAGES_DEBUG=Libregnum-Text ./your-game
```

Example output:

```
Libregnum-Text-INFO: Initializing font manager
Libregnum-Text-DEBUG: Found system font: /usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf
Libregnum-Text-DEBUG: Loaded font 'ui-small' from ... (size 12)
Libregnum-Text-DEBUG: Loaded font 'ui-normal' from ... (size 16)
Libregnum-Text-DEBUG: Loaded font 'ui-large' from ... (size 24)
Libregnum-Text-INFO: Font manager initialized with /usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf
```

If you see "No system fonts found, using raylib default", check:

1. Font packages are installed (`liberation-sans-fonts` on Fedora, `fonts-liberation` on Debian/Ubuntu)
2. Font paths match your distribution
3. Font files exist at expected locations

### Finding Fonts on Your System

```bash
# Find Liberation Sans
find /usr/share/fonts -name "LiberationSans*.ttf" 2>/dev/null

# List all TrueType fonts
fc-list : file | grep -i "\.ttf"

# Check specific font
fc-match "Liberation Sans"
```

---

## Common Font Packages

### Fedora

```bash
sudo dnf install liberation-sans-fonts google-noto-sans-fonts dejavu-sans-fonts
```

### Debian/Ubuntu

```bash
sudo apt install fonts-liberation fonts-noto fonts-dejavu
```

### Arch Linux

```bash
sudo pacman -S ttf-liberation noto-fonts ttf-dejavu
```
