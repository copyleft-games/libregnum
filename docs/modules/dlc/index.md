# DLC System

The DLC module provides functionality for managing downloadable content (DLC) including ownership verification, content gating, trial support, and platform integration.

## Overview

DLC is treated as a specialized type of mod (`LrgDlc` extends `LrgMod`) with additional capabilities:

- **Ownership Verification**: Multiple backends (Steam, license file, manifest)
- **Content Gating**: Restrict content based on ownership state
- **Trial Support**: Allow limited access to DLC content
- **Store Integration**: Direct links to purchase pages

## Types

### Class Hierarchy

```
LrgMod (derivable)
└── LrgDlc (derivable)
    ├── LrgExpansionPack (final)
    ├── LrgCosmeticPack (final)
    ├── LrgQuestPack (final)
    ├── LrgItemPack (final)
    ├── LrgCharacterPack (final)
    └── LrgMapPack (final)
```

### LrgDlcOwnership Interface

Interface for DLC ownership verification backends.

**Methods:**

| Method | Description |
|--------|-------------|
| check_ownership | Verify ownership of a specific DLC ID |
| refresh_ownership | Refresh ownership cache |
| get_backend_id | Returns backend identifier string |

**Implementations:**

| Type | Description |
|------|-------------|
| LrgDlcOwnershipSteam | Steam API ownership verification |
| LrgDlcOwnershipLicense | License file verification |
| LrgDlcOwnershipManifest | Simple flag-based verification |

### LrgDlc Base Class

Base class for all DLC types with ownership and store integration.

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| dlc-type | LrgDlcType | Type of DLC content |
| price-string | string | Display price (e.g., "$4.99") |
| currency | string | Currency code (e.g., "USD") |
| steam-app-id | guint32 | Steam App ID for DLC |
| store-id | string | Generic store identifier |
| release-date | GDateTime | Release date |
| min-game-version | string | Minimum game version required |
| ownership-state | LrgDlcOwnershipState | Current ownership state |
| trial-enabled | gboolean | Whether trial access is available |

**Signals:**

| Signal | Parameters | Description |
|--------|------------|-------------|
| ownership-changed | state (LrgDlcOwnershipState) | Ownership state changed |
| purchase-prompted | - | Purchase dialog was shown |

### DLC Subclasses

#### LrgExpansionPack

Major content expansions with new campaigns.

| Property | Type | Description |
|----------|------|-------------|
| campaign-name | string | Main campaign/storyline name |
| level-cap-increase | guint | Level cap increase amount |

#### LrgCosmeticPack

Visual customization content.

| Property | Type | Description |
|----------|------|-------------|
| preview-image | string | Path to preview image |

Methods: `add_skin_id()`, `add_effect_id()`, `get_skin_ids()`, `get_effect_ids()`

#### LrgQuestPack

Additional quests and storylines.

| Property | Type | Description |
|----------|------|-------------|
| estimated-hours | guint | Estimated playtime in hours |

Methods: `add_quest_id()`, `get_quest_ids()`

#### LrgItemPack

Equipment and item bundles.

Methods: `add_item_id()`, `get_item_ids()`, `add_equipment_slot()`, `get_equipment_slots()`

#### LrgCharacterPack

Playable characters or companions.

| Property | Type | Description |
|----------|------|-------------|
| is-playable | gboolean | Contains playable characters |
| is-companion | gboolean | Contains companion characters |

Methods: `add_character_id()`, `get_character_ids()`

#### LrgMapPack

Additional maps or levels.

| Property | Type | Description |
|----------|------|-------------|
| biome-type | string | Primary biome/environment type |

Methods: `add_map_id()`, `get_map_ids()`

## Enums

### LrgDlcType

Types of DLC content.

| Value | Description |
|-------|-------------|
| LRG_DLC_TYPE_EXPANSION | Major content expansion |
| LRG_DLC_TYPE_COSMETIC | Visual customizations |
| LRG_DLC_TYPE_QUEST | Additional quests |
| LRG_DLC_TYPE_ITEM | Equipment/items |
| LRG_DLC_TYPE_CHARACTER | Characters |
| LRG_DLC_TYPE_MAP | Maps/levels |

### LrgDlcOwnershipState

Current ownership state.

| Value | Description |
|-------|-------------|
| LRG_DLC_OWNERSHIP_UNKNOWN | Not yet verified |
| LRG_DLC_OWNERSHIP_NOT_OWNED | User doesn't own |
| LRG_DLC_OWNERSHIP_OWNED | User has full access |
| LRG_DLC_OWNERSHIP_TRIAL | User has trial access |
| LRG_DLC_OWNERSHIP_ERROR | Verification failed |

### LrgDlcError

Error codes for the DLC system.

| Value | Description |
|-------|-------------|
| LRG_DLC_ERROR_FAILED | Generic DLC error |
| LRG_DLC_ERROR_NOT_OWNED | DLC not owned |
| LRG_DLC_ERROR_VERIFICATION_FAILED | Ownership check failed |
| LRG_DLC_ERROR_INVALID_LICENSE | License file invalid |
| LRG_DLC_ERROR_STEAM_UNAVAILABLE | Steam API unavailable |
| LRG_DLC_ERROR_CONTENT_GATED | Content locked by ownership |

## Manifest Format

DLC is defined in mod manifests with a `dlc:` section:

```yaml
id: my-expansion-pack
name: "The Dark Realm Expansion"
version: "1.0.0"
type: expansion

dlc:
  type: expansion
  steam_app_id: 12345
  store_id: "my-game-expansion-1"
  price: "$14.99"
  min_game_version: "1.5.0"
  release_date: "2025-03-15"

  ownership:
    method: steam  # or: license, manifest, none

  trial:
    enabled: true
    content_ids:
      - "realm-entrance"
      - "first-quest"

# Expansion-specific fields
expansion:
  campaign_name: "The Dark Realm"
  level_cap_increase: 10
  new_areas:
    - "dark-realm-entrance"
    - "shadow-castle"
    - "void-citadel"
```

## Usage Examples

### Loading DLC via ModLoader

```c
/* DLCs are automatically detected and loaded */
LrgModLoader *loader = lrg_mod_loader_new ();
lrg_mod_loader_add_search_path (loader, "dlc/");

LrgMod *mod = lrg_mod_loader_load_mod (loader, "dlc/expansion-pack", &error);

if (LRG_IS_DLC (mod))
{
    LrgDlc *dlc = LRG_DLC (mod);
    g_print ("Loaded DLC: %s (type: %d)\n",
             lrg_mod_get_id (mod),
             lrg_dlc_get_dlc_type (dlc));
}
```

### Querying DLCs via ModManager

```c
LrgModManager *manager = lrg_mod_manager_get_default ();

/* Discover and load all mods/DLCs */
lrg_mod_manager_discover (manager, &error);
lrg_mod_manager_load_all (manager, &error);

/* Get all DLCs */
g_autoptr(GPtrArray) dlcs = lrg_mod_manager_get_dlcs (manager);
g_print ("Found %u DLCs\n", dlcs->len);

/* Get owned DLCs */
g_autoptr(GPtrArray) owned = lrg_mod_manager_get_owned_dlcs (manager);
g_print ("%u DLCs owned\n", owned->len);

/* Get DLCs by type */
g_autoptr(GPtrArray) expansions = lrg_mod_manager_get_dlcs_by_type (manager,
                                                                      LRG_DLC_TYPE_EXPANSION);

/* Get a specific DLC */
LrgDlc *dlc = lrg_mod_manager_get_dlc (manager, "my-expansion-pack");
```

### Verifying Ownership

```c
LrgDlc *dlc = /* ... */;

/* Set up ownership checker */
LrgDlcOwnershipSteam *checker = lrg_dlc_ownership_steam_new ();
lrg_dlc_ownership_steam_set_steam_service (checker, steam_service);
lrg_dlc_ownership_steam_register_dlc (checker, "my-expansion", 12345);

lrg_dlc_set_ownership_checker (dlc, LRG_DLC_OWNERSHIP (checker));

/* Verify ownership */
LrgDlcOwnershipState state = lrg_dlc_verify_ownership (dlc, &error);

switch (state)
{
case LRG_DLC_OWNERSHIP_OWNED:
    g_print ("User owns this DLC\n");
    break;
case LRG_DLC_OWNERSHIP_TRIAL:
    g_print ("User has trial access\n");
    break;
case LRG_DLC_OWNERSHIP_NOT_OWNED:
    g_print ("User doesn't own this DLC\n");
    break;
default:
    break;
}

/* Quick check */
if (lrg_dlc_is_owned (dlc))
    enable_dlc_content (dlc);
```

### Checking Content Access

```c
/* Check if specific content is accessible */
if (lrg_dlc_is_content_accessible (dlc, "dark-realm-entrance"))
{
    /* Content available (owned or trial) */
    load_area ("dark-realm-entrance");
}
else
{
    /* Show purchase prompt */
    show_dlc_purchase_dialog (dlc);
}
```

### Opening Store Page

```c
/* Get store URL */
g_autofree gchar *url = lrg_dlc_get_store_url (dlc);
g_print ("Store URL: %s\n", url);

/* Open in browser */
lrg_dlc_open_store_page (dlc, &error);
```

### Handling Ownership Changes

```c
static void
on_ownership_changed (LrgDlc              *dlc,
                      LrgDlcOwnershipState state,
                      gpointer             user_data)
{
    Game *game = (Game *)user_data;

    if (state == LRG_DLC_OWNERSHIP_OWNED)
    {
        /* User just purchased! Enable content */
        show_thank_you_dialog (game);
        enable_dlc_content (game, dlc);
    }
}

g_signal_connect (dlc, "ownership-changed",
                  G_CALLBACK (on_ownership_changed), game);
```

### Using Expansion Pack

```c
LrgExpansionPack *expansion = LRG_EXPANSION_PACK (dlc);

/* Get expansion info */
const gchar *campaign = lrg_expansion_pack_get_campaign_name (expansion);
guint level_increase = lrg_expansion_pack_get_level_cap_increase (expansion);

g_print ("Campaign: %s (+%u levels)\n", campaign, level_increase);

/* Get new areas */
GPtrArray *areas = lrg_expansion_pack_get_new_areas (expansion);
for (guint i = 0; i < areas->len; i++)
{
    const gchar *area = g_ptr_array_index (areas, i);
    register_area (area);
}
```

### Using Quest Pack

```c
LrgQuestPack *quest_pack = LRG_QUEST_PACK (dlc);

/* Add quests */
lrg_quest_pack_add_quest_id (quest_pack, "quest-dragon-hunt");
lrg_quest_pack_add_quest_id (quest_pack, "quest-treasure-vault");

/* Get quest info */
GPtrArray *quests = lrg_quest_pack_get_quest_ids (quest_pack);
guint hours = lrg_quest_pack_get_estimated_hours (quest_pack);

g_print ("%u quests, ~%u hours of content\n", quests->len, hours);
```

### Implementing Custom Ownership Backend

```c
G_DEFINE_TYPE_WITH_CODE (MyOwnershipBackend, my_ownership_backend, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DLC_OWNERSHIP,
                                                my_ownership_init))

static gboolean
my_ownership_check (LrgDlcOwnership  *ownership,
                    const gchar      *dlc_id,
                    GError          **error)
{
    MyOwnershipBackend *self = MY_OWNERSHIP_BACKEND (ownership);

    /* Check your entitlement system */
    return check_entitlement (self->api, dlc_id);
}

static const gchar *
my_ownership_get_backend_id (LrgDlcOwnership *ownership)
{
    return "my-backend";
}

static void
my_ownership_init (LrgDlcOwnershipInterface *iface)
{
    iface->check_ownership = my_ownership_check;
    iface->get_backend_id = my_ownership_get_backend_id;
}
```

## Ownership Verification Methods

### Steam

Uses Steamworks API to verify DLC ownership.

```c
LrgDlcOwnershipSteam *checker = lrg_dlc_ownership_steam_new ();
lrg_dlc_ownership_steam_set_steam_service (checker, steam_service);
lrg_dlc_ownership_steam_register_dlc (checker, "dlc-id", STEAM_DLC_APP_ID);
```

### License File

Verifies ownership via encrypted license files.

```c
LrgDlcOwnershipLicense *checker = lrg_dlc_ownership_license_new ("dlc/license.dat");
```

### Manifest

Simple flag-based ownership for DRM-free releases.

```c
LrgDlcOwnershipManifest *checker = lrg_dlc_ownership_manifest_new ();

/* Mark DLCs as owned */
lrg_dlc_ownership_manifest_set_owned (checker, "dlc-1", TRUE);
lrg_dlc_ownership_manifest_set_owned (checker, "dlc-2", TRUE);

/* Or mark all as owned (for complete editions) */
lrg_dlc_ownership_manifest_set_all_owned (checker, TRUE);
```

## Best Practices

1. **Verify Early**: Check DLC ownership at startup, not when accessing content.

2. **Cache Results**: Ownership verification can be slow; cache and refresh periodically.

3. **Graceful Degradation**: Handle verification failures gracefully (assume not owned).

4. **Trial Content**: Provide meaningful trial content to encourage purchases.

5. **Clear Messaging**: Show what content is locked and how to unlock it.

6. **Cross-Platform**: Support multiple ownership backends for different platforms.

## Integration with Demo System

DLC and Demo systems work together:

```c
LrgDemoManager *demo = lrg_demo_manager_get_default ();
LrgDlc *dlc = /* ... */;

/* Gate DLC content that isn't in trial */
GPtrArray *trial_ids = lrg_dlc_get_trial_content_ids (dlc);
GPtrArray *all_content = get_dlc_content_ids (dlc);

for (guint i = 0; i < all_content->len; i++)
{
    const gchar *id = g_ptr_array_index (all_content, i);

    if (!is_in_array (trial_ids, id))
        lrg_demo_manager_gate_content (demo, id);
}
```

## See Also

- [Mod System](../mod/index.md) - Base mod functionality
- [Steam Module](../steam/index.md) - Steam integration
- [Demo System](../demo/index.md) - Demo/trial support
