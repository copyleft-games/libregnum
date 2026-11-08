/* lrg-mixer-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMixerAsset: the `.rmixer` mixer layout.  Buses (volume, mute, solo,
 * parent), post-fader sends, effect chains with parameters, snapshots
 * with blend times and side-chain ducking rules, loaded from and saved
 * to YAML, and applied to an #LrgAudioMixer at runtime.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-audio-mixer.h"

G_BEGIN_DECLS

/**
 * LRG_MIXER_ASSET_FORMAT_VERSION:
 *
 * The `.rmixer` format version this build writes and the newest it reads.
 */
#define LRG_MIXER_ASSET_FORMAT_VERSION (1)

/**
 * LRG_MIXER_ASSET_ERROR:
 *
 * Error domain of #LrgMixerAsset.  Errors are #LrgMixerAssetError codes.
 */
#define LRG_MIXER_ASSET_ERROR (lrg_mixer_asset_error_quark ())

/**
 * LrgMixerAssetError:
 * @LRG_MIXER_ASSET_ERROR_PARSE: not YAML, or the wrong shape (a list
 *   where a mapping belongs, a missing required key)
 * @LRG_MIXER_ASSET_ERROR_INVALID_VALUE: a value out of range or of the
 *   wrong type
 * @LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS: a parent, send, snapshot or ducking
 *   rule names a bus that does not exist, or a bus name is used twice
 * @LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT: an unknown effect type or effect id
 * @LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM: a parameter the effect does not have
 * @LRG_MIXER_ASSET_ERROR_ROUTING: a parent loop, a send or ducking rule
 *   that would make the graph cyclic, or other routing the mixer rejects
 * @LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT: no snapshot of that name
 * @LRG_MIXER_ASSET_ERROR_IO: reading or writing a file failed
 *
 * Errors of #LrgMixerAsset.  Messages start with the source (file path or
 * `<data>`) and the key path of the offending value, such as
 * `music.rmixer: /buses/2/effects/0/params/cutoff: ...`.
 */
typedef enum
{
	LRG_MIXER_ASSET_ERROR_PARSE,
	LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
	LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS,
	LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT,
	LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM,
	LRG_MIXER_ASSET_ERROR_ROUTING,
	LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT,
	LRG_MIXER_ASSET_ERROR_IO
} LrgMixerAssetError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_mixer_asset_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_mixer_asset_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MIXER_ASSET_ERROR (lrg_mixer_asset_error_get_type ())

#define LRG_TYPE_MIXER_ASSET (lrg_mixer_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMixerAsset, lrg_mixer_asset, LRG, MIXER_ASSET, GObject)

/* ==========================================================================
 * Construction, load and save
 * ========================================================================== */

/**
 * lrg_mixer_asset_new:
 *
 * Creates a layout with only the master bus at 0 dB.
 *
 * Returns: (transfer full): a new #LrgMixerAsset
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_mixer_asset_new (void);

/**
 * lrg_mixer_asset_new_from_data:
 * @data: `.rmixer` YAML text
 * @length: length of @data, or -1 when it is NUL-terminated
 * @error: (nullable): return location for an error
 *
 * Parses a layout.  Validation is strict (see #LrgMixerAssetError); keys
 * this version does not know are kept and written back by
 * lrg_mixer_asset_to_data().
 *
 * Returns: (transfer full) (nullable): the layout, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_mixer_asset_new_from_data (const gchar  *data,
                                               gssize        length,
                                               GError      **error);

/**
 * lrg_mixer_asset_new_from_file:
 * @path: (type filename): a `.rmixer` file
 * @error: (nullable): return location for an error
 *
 * Loads a layout from a file; see lrg_mixer_asset_new_from_data().
 *
 * Returns: (transfer full) (nullable): the layout, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_mixer_asset_new_from_file (const gchar  *path,
                                               GError      **error);

/**
 * lrg_mixer_asset_to_data:
 * @self: an #LrgMixerAsset
 * @length: (out) (optional): length of the text
 * @error: (nullable): return location for an error
 *
 * Writes the layout as YAML in a fixed key order with unknown keys kept,
 * so saving a layout loaded from saved text gives the same bytes.
 *
 * Returns: (transfer full) (nullable): the YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_mixer_asset_to_data (LrgMixerAsset  *self,
                                 gsize          *length,
                                 GError        **error);

/**
 * lrg_mixer_asset_save_to_file:
 * @self: an #LrgMixerAsset
 * @path: (type filename): the file to write
 * @error: (nullable): return location for an error
 *
 * Writes lrg_mixer_asset_to_data() to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_save_to_file (LrgMixerAsset  *self,
                                       const gchar    *path,
                                       GError        **error);

/**
 * lrg_mixer_asset_copy:
 * @self: an #LrgMixerAsset
 *
 * Returns: (transfer full): a deep copy (an editor's undo snapshot)
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_mixer_asset_copy (LrgMixerAsset *self);

/**
 * lrg_mixer_asset_validate:
 * @self: an #LrgMixerAsset
 * @error: (nullable): return location for an error
 *
 * Checks that every parent, send, snapshot entry and ducking rule names
 * an existing bus and effect, that parents form a tree and that sends
 * and ducking rules keep the graph acyclic: the checks the loader runs,
 * for a layout edited through the API.
 *
 * Returns: %TRUE when the layout can be applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_validate (LrgMixerAsset  *self,
                                   GError        **error);

/**
 * lrg_mixer_asset_get_name:
 * @self: an #LrgMixerAsset
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mixer_asset_get_name (LrgMixerAsset *self);

/**
 * lrg_mixer_asset_set_name:
 * @self: an #LrgMixerAsset
 * @name: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
void lrg_mixer_asset_set_name (LrgMixerAsset *self,
                               const gchar   *name);

/* ==========================================================================
 * Buses and sends
 * ========================================================================== */

/**
 * lrg_mixer_asset_add_bus:
 * @self: an #LrgMixerAsset
 * @name: a new, nonempty bus name
 * @parent: (nullable): the bus it outputs to, %NULL for master
 *
 * Appends a bus at 0 dB.
 *
 * Returns: %TRUE when added (%FALSE for a duplicate name or unknown parent)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_add_bus (LrgMixerAsset *self,
                                  const gchar   *name,
                                  const gchar   *parent);

/**
 * lrg_mixer_asset_remove_bus:
 * @self: an #LrgMixerAsset
 * @name: a bus other than master
 *
 * Removes a bus with no child buses, and every send, snapshot entry and
 * ducking rule naming it.
 *
 * Returns: %TRUE when removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_remove_bus (LrgMixerAsset *self,
                                     const gchar   *name);

/**
 * lrg_mixer_asset_get_bus_names:
 * @self: an #LrgMixerAsset
 *
 * Returns: (transfer full) (array zero-terminated=1): the bus names in
 *   file order, master first
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_mixer_asset_get_bus_names (LrgMixerAsset *self);

/**
 * lrg_mixer_asset_has_bus:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 *
 * Returns: whether the bus exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_has_bus (LrgMixerAsset *self,
                                  const gchar   *name);

/**
 * lrg_mixer_asset_get_bus_parent:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 *
 * Returns: (transfer none) (nullable): the parent name, %NULL for master
 *   and unknown buses
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mixer_asset_get_bus_parent (LrgMixerAsset *self,
                                              const gchar   *name);

/**
 * lrg_mixer_asset_set_bus_parent:
 * @self: an #LrgMixerAsset
 * @name: a bus other than master
 * @parent: (nullable): the new parent, %NULL for master
 *
 * Re-parents a bus.  A parent that is the bus itself or one of its
 * descendants is rejected.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_bus_parent (LrgMixerAsset *self,
                                         const gchar   *name,
                                         const gchar   *parent);

/**
 * lrg_mixer_asset_set_bus_volume:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 * @volume_db: the fader in dB, %LRG_AUDIO_SILENCE_DB to
 *   %LRG_AUDIO_MIXER_MAX_GAIN_DB (clamped)
 *
 * Returns: %TRUE for a known bus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_bus_volume (LrgMixerAsset *self,
                                         const gchar   *name,
                                         gdouble        volume_db);

/**
 * lrg_mixer_asset_get_bus_volume:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 *
 * Returns: the fader in dB (%LRG_AUDIO_SILENCE_DB for an unknown bus)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_mixer_asset_get_bus_volume (LrgMixerAsset *self,
                                        const gchar   *name);

/**
 * lrg_mixer_asset_set_bus_mute:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 * @mute: whether the bus is muted
 *
 * Returns: %TRUE for a known bus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_bus_mute (LrgMixerAsset *self,
                                       const gchar   *name,
                                       gboolean       mute);

/**
 * lrg_mixer_asset_get_bus_mute:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 *
 * Returns: whether the bus is muted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_bus_mute (LrgMixerAsset *self,
                                       const gchar   *name);

/**
 * lrg_mixer_asset_set_bus_solo:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 * @solo: whether the bus is soloed (lrg_audio_mixer_set_bus_solo())
 *
 * Returns: %TRUE for a known bus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_bus_solo (LrgMixerAsset *self,
                                       const gchar   *name,
                                       gboolean       solo);

/**
 * lrg_mixer_asset_get_bus_solo:
 * @self: an #LrgMixerAsset
 * @name: a bus name
 *
 * Returns: whether the bus is soloed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_bus_solo (LrgMixerAsset *self,
                                       const gchar   *name);

/**
 * lrg_mixer_asset_set_send:
 * @self: an #LrgMixerAsset
 * @source: a bus other than master
 * @target: another bus
 * @level_db: the post-fader send level in dB (clamped like a volume)
 *
 * Adds or edits a send; lrg_mixer_asset_validate() checks routing.
 *
 * Returns: %TRUE when both buses exist and differ
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_send (LrgMixerAsset *self,
                                   const gchar   *source,
                                   const gchar   *target,
                                   gdouble        level_db);

/**
 * lrg_mixer_asset_remove_send:
 * @self: an #LrgMixerAsset
 * @source: the source bus
 * @target: the target bus
 *
 * Returns: %TRUE when the send existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_remove_send (LrgMixerAsset *self,
                                      const gchar   *source,
                                      const gchar   *target);

/**
 * lrg_mixer_asset_get_send:
 * @self: an #LrgMixerAsset
 * @source: the source bus
 * @target: the target bus
 * @out_level_db: (out) (optional): the level in dB
 *
 * Returns: whether the send exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_send (LrgMixerAsset *self,
                                   const gchar   *source,
                                   const gchar   *target,
                                   gdouble       *out_level_db);

/**
 * lrg_mixer_asset_get_send_targets:
 * @self: an #LrgMixerAsset
 * @source: a bus name
 *
 * Returns: (transfer full) (array zero-terminated=1): the targets of the
 *   sends of @source in file order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_mixer_asset_get_send_targets (LrgMixerAsset *self,
                                           const gchar   *source);

/* ==========================================================================
 * Effects
 * ========================================================================== */

/**
 * lrg_mixer_asset_register_effect_type:
 * @name: the `type:` name used in `.rmixer` files
 * @type: a concrete #LrgAudioEffect subclass
 *
 * Makes an application effect usable in layouts.  Its parameters are its
 * writable properties.  The built-in names are `low-pass`, `high-pass`,
 * `echo`, `reverb`, `chorus` and `pitch-shift`.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mixer_asset_register_effect_type (const gchar *name,
                                           GType        type);

/**
 * lrg_mixer_asset_lookup_effect_type:
 * @name: a registered effect name, or a GType name
 *
 * Returns: the effect type, or %G_TYPE_INVALID
 */
LRG_AVAILABLE_IN_ALL
GType lrg_mixer_asset_lookup_effect_type (const gchar *name);

/**
 * lrg_mixer_asset_list_effect_types:
 *
 * Returns: (transfer full) (array zero-terminated=1): the registered
 *   effect names, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_mixer_asset_list_effect_types (void);

/**
 * lrg_mixer_asset_add_effect:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @type_name: a registered effect name
 * @id: (nullable): the effect's id, unique on the bus; %NULL uses
 *   @type_name
 * @error: (nullable): return location for an error
 *
 * Appends an effect with default parameters to the bus chain.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_add_effect (LrgMixerAsset  *self,
                                     const gchar    *bus,
                                     const gchar    *type_name,
                                     const gchar    *id,
                                     GError        **error);

/**
 * lrg_mixer_asset_remove_effect:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 *
 * Removes the effect and the snapshot parameters naming it.
 *
 * Returns: %TRUE when removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_remove_effect (LrgMixerAsset *self,
                                        const gchar   *bus,
                                        const gchar   *id);

/**
 * lrg_mixer_asset_move_effect:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 * @index: its new position in the chain (clamped to the end)
 *
 * Returns: %TRUE when the effect exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_move_effect (LrgMixerAsset *self,
                                      const gchar   *bus,
                                      const gchar   *id,
                                      guint          index);

/**
 * lrg_mixer_asset_get_effect_ids:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 *
 * Returns: (transfer full) (array zero-terminated=1): the effect ids of
 *   the bus chain in order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_mixer_asset_get_effect_ids (LrgMixerAsset *self,
                                         const gchar   *bus);

/**
 * lrg_mixer_asset_get_effect_type:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 *
 * Returns: (transfer none) (nullable): the effect's type name as written
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mixer_asset_get_effect_type (LrgMixerAsset *self,
                                               const gchar   *bus,
                                               const gchar   *id);

/**
 * lrg_mixer_asset_set_effect_enabled:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 * @enabled: whether the effect processes (#LrgAudioEffect:enabled)
 *
 * Returns: %TRUE when the effect exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_effect_enabled (LrgMixerAsset *self,
                                             const gchar   *bus,
                                             const gchar   *id,
                                             gboolean       enabled);

/**
 * lrg_mixer_asset_get_effect_enabled:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 *
 * Returns: whether the effect processes (%FALSE when it does not exist)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_effect_enabled (LrgMixerAsset *self,
                                             const gchar   *bus,
                                             const gchar   *id);

/**
 * lrg_mixer_asset_set_effect_param:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 * @param: a writable property of the effect (kebab or snake case)
 * @value: the value, of a type the property's value converts from
 *   (numbers, booleans, enum nicks as strings)
 * @error: (nullable): return location for an error
 *
 * Stores a parameter (validated against the property's range; values
 * outside it are an error).  Parameters are applied in the order they
 * were first set.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_effect_param (LrgMixerAsset  *self,
                                           const gchar    *bus,
                                           const gchar    *id,
                                           const gchar    *param,
                                           const GValue   *value,
                                           GError        **error);

/**
 * lrg_mixer_asset_get_effect_param:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 * @param: a parameter name
 * @out_value: (out caller-allocates): an uninitialised #GValue that
 *   receives the stored value
 *
 * Returns: %TRUE when the parameter is set in the layout (an unset
 *   parameter keeps the effect's default)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_effect_param (LrgMixerAsset *self,
                                           const gchar   *bus,
                                           const gchar   *id,
                                           const gchar   *param,
                                           GValue        *out_value);

/**
 * lrg_mixer_asset_unset_effect_param:
 * @self: an #LrgMixerAsset
 * @bus: a bus name
 * @id: an effect id
 * @param: a parameter name
 *
 * Returns: %TRUE when the parameter was set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_unset_effect_param (LrgMixerAsset *self,
                                             const gchar   *bus,
                                             const gchar   *id,
                                             const gchar   *param);

/* ==========================================================================
 * Snapshots
 * ========================================================================== */

/**
 * lrg_mixer_asset_add_snapshot:
 * @self: an #LrgMixerAsset
 * @name: a new, nonempty snapshot name
 * @blend_seconds: the default transition time into it, 0 or more
 *
 * Adds an empty snapshot.  A snapshot overrides bus volumes, send levels
 * and numeric effect parameters; whatever it does not override takes
 * the layout's own (base) value when it is applied.
 *
 * Returns: %TRUE when added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_add_snapshot (LrgMixerAsset *self,
                                       const gchar   *name,
                                       gdouble        blend_seconds);

/**
 * lrg_mixer_asset_remove_snapshot:
 * @self: an #LrgMixerAsset
 * @name: a snapshot name
 *
 * Returns: %TRUE when removed (the default snapshot is cleared with it)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_remove_snapshot (LrgMixerAsset *self,
                                          const gchar   *name);

/**
 * lrg_mixer_asset_get_snapshot_names:
 * @self: an #LrgMixerAsset
 *
 * Returns: (transfer full) (array zero-terminated=1): the snapshot names
 *   in file order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_mixer_asset_get_snapshot_names (LrgMixerAsset *self);

/**
 * lrg_mixer_asset_set_snapshot_blend:
 * @self: an #LrgMixerAsset
 * @name: a snapshot name
 * @blend_seconds: transition time, 0 or more
 *
 * Returns: %TRUE for a known snapshot
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_snapshot_blend (LrgMixerAsset *self,
                                             const gchar   *name,
                                             gdouble        blend_seconds);

/**
 * lrg_mixer_asset_get_snapshot_blend:
 * @self: an #LrgMixerAsset
 * @name: a snapshot name
 *
 * Returns: the transition time, -1 for an unknown snapshot
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_mixer_asset_get_snapshot_blend (LrgMixerAsset *self,
                                            const gchar   *name);

/**
 * lrg_mixer_asset_set_snapshot_volume:
 * @self: an #LrgMixerAsset
 * @snapshot: a snapshot name
 * @bus: a bus name
 * @volume_db: the bus volume in the snapshot; NaN removes the override
 *
 * Returns: %TRUE when the snapshot and bus exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_snapshot_volume (LrgMixerAsset *self,
                                              const gchar   *snapshot,
                                              const gchar   *bus,
                                              gdouble        volume_db);

/**
 * lrg_mixer_asset_get_snapshot_volume:
 * @self: an #LrgMixerAsset
 * @snapshot: a snapshot name
 * @bus: a bus name
 * @out_volume_db: (out) (optional): the overriding volume
 *
 * Returns: whether the snapshot overrides the bus volume
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_snapshot_volume (LrgMixerAsset *self,
                                              const gchar   *snapshot,
                                              const gchar   *bus,
                                              gdouble       *out_volume_db);

/**
 * lrg_mixer_asset_set_snapshot_send:
 * @self: an #LrgMixerAsset
 * @snapshot: a snapshot name
 * @source: the source bus
 * @target: the target bus
 * @level_db: the send level in the snapshot; NaN removes the override
 *
 * A snapshot may also open a send the base layout does not have (its
 * base level is silence).
 *
 * Returns: %TRUE when the snapshot and buses exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_snapshot_send (LrgMixerAsset *self,
                                            const gchar   *snapshot,
                                            const gchar   *source,
                                            const gchar   *target,
                                            gdouble        level_db);

/**
 * lrg_mixer_asset_get_snapshot_send:
 * @self: an #LrgMixerAsset
 * @snapshot: a snapshot name
 * @source: the source bus
 * @target: the target bus
 * @out_level_db: (out) (optional): the overriding level
 *
 * Returns: whether the snapshot overrides the send
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_snapshot_send (LrgMixerAsset *self,
                                            const gchar   *snapshot,
                                            const gchar   *source,
                                            const gchar   *target,
                                            gdouble       *out_level_db);

/**
 * lrg_mixer_asset_set_snapshot_param:
 * @self: an #LrgMixerAsset
 * @snapshot: a snapshot name
 * @bus: a bus name
 * @effect: an effect id on @bus
 * @param: a numeric parameter of the effect
 * @value: the value in the snapshot; NaN removes the override
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_snapshot_param (LrgMixerAsset  *self,
                                             const gchar    *snapshot,
                                             const gchar    *bus,
                                             const gchar    *effect,
                                             const gchar    *param,
                                             gdouble         value,
                                             GError        **error);

/**
 * lrg_mixer_asset_get_snapshot_param:
 * @self: an #LrgMixerAsset
 * @snapshot: a snapshot name
 * @bus: a bus name
 * @effect: an effect id
 * @param: a parameter name
 * @out_value: (out) (optional): the overriding value
 *
 * Returns: whether the snapshot overrides the parameter
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_snapshot_param (LrgMixerAsset *self,
                                             const gchar   *snapshot,
                                             const gchar   *bus,
                                             const gchar   *effect,
                                             const gchar   *param,
                                             gdouble       *out_value);

/**
 * lrg_mixer_asset_set_default_snapshot:
 * @self: an #LrgMixerAsset
 * @name: (nullable): a snapshot applied (without a blend) by
 *   lrg_mixer_asset_apply(), %NULL for none
 *
 * Returns: %TRUE when @name is %NULL or a known snapshot
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_default_snapshot (LrgMixerAsset *self,
                                               const gchar   *name);

/**
 * lrg_mixer_asset_get_default_snapshot:
 * @self: an #LrgMixerAsset
 *
 * Returns: (transfer none) (nullable): the default snapshot
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mixer_asset_get_default_snapshot (LrgMixerAsset *self);

/* ==========================================================================
 * Ducking
 * ========================================================================== */

/**
 * lrg_mixer_asset_set_duck:
 * @self: an #LrgMixerAsset
 * @trigger: the side-chain bus
 * @target: the bus that is turned down
 * @threshold_db: trigger level that starts the ducking
 * @depth_db: target gain while ducked, -144 to 0 dB
 * @attack: seconds to duck, 0 to 10
 * @release: seconds to recover, 0 to 10
 *
 * Adds or edits a ducking rule (see lrg_audio_mixer_set_duck()).
 *
 * Returns: %TRUE when both buses exist, differ and the values are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_set_duck (LrgMixerAsset *self,
                                   const gchar   *trigger,
                                   const gchar   *target,
                                   gdouble        threshold_db,
                                   gdouble        depth_db,
                                   gdouble        attack,
                                   gdouble        release);

/**
 * lrg_mixer_asset_remove_duck:
 * @self: an #LrgMixerAsset
 * @trigger: the side-chain bus
 * @target: the ducked bus
 *
 * Returns: %TRUE when the rule existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_remove_duck (LrgMixerAsset *self,
                                      const gchar   *trigger,
                                      const gchar   *target);

/**
 * lrg_mixer_asset_get_duck_count:
 * @self: an #LrgMixerAsset
 *
 * Returns: the number of ducking rules
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mixer_asset_get_duck_count (LrgMixerAsset *self);

/**
 * lrg_mixer_asset_get_duck:
 * @self: an #LrgMixerAsset
 * @index: 0 .. lrg_mixer_asset_get_duck_count() - 1
 * @out_trigger: (out) (optional) (transfer none): the side-chain bus
 * @out_target: (out) (optional) (transfer none): the ducked bus
 * @out_threshold_db: (out) (optional): the threshold
 * @out_depth_db: (out) (optional): the depth
 * @out_attack: (out) (optional): the attack time
 * @out_release: (out) (optional): the release time
 *
 * Returns: %TRUE for a valid index
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_get_duck (LrgMixerAsset  *self,
                                   guint           index,
                                   const gchar   **out_trigger,
                                   const gchar   **out_target,
                                   gdouble        *out_threshold_db,
                                   gdouble        *out_depth_db,
                                   gdouble        *out_attack,
                                   gdouble        *out_release);

/* ==========================================================================
 * Runtime
 * ========================================================================== */

/**
 * lrg_mixer_asset_create_mixer:
 * @self: an #LrgMixerAsset
 * @sample_rate: the mixer rate in Hz
 * @channels: 1 or 2
 * @error: (nullable): return location for an error
 *
 * Builds a new mixer and applies the layout to it.
 *
 * Returns: (transfer full) (nullable): the mixer, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAudioMixer * lrg_mixer_asset_create_mixer (LrgMixerAsset  *self,
                                              guint           sample_rate,
                                              guint           channels,
                                              GError        **error);

/**
 * lrg_mixer_asset_apply:
 * @self: an #LrgMixerAsset
 * @mixer: a mixer
 * @error: (nullable): return location for an error
 *
 * Makes @mixer match the layout: adds missing buses (a bus the mixer has
 * under another parent is an error), sets volumes, mutes and solos,
 * replaces the sends, the effect chains (fresh effect instances: tails
 * restart) and the ducking rules, then applies the default snapshot
 * without a blend.  Buses of the mixer the layout does not name are
 * left alone.  Voices keep playing.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_apply (LrgMixerAsset  *self,
                                LrgAudioMixer  *mixer,
                                GError        **error);

/**
 * lrg_mixer_asset_apply_snapshot:
 * @self: an #LrgMixerAsset
 * @mixer: a mixer the layout was applied to
 * @name: (nullable): a snapshot, or %NULL for the base layout
 * @blend_seconds: transition time; negative uses the snapshot's own
 *   (0 for the base layout)
 * @error: (nullable): return location for an error
 *
 * Starts ramps (lrg_audio_mixer_ramp_bus_gain(), _ramp_send() and
 * _ramp_effect_param()) from the mixer's current values to the
 * snapshot's: its overrides over the base layout.  Every value any
 * snapshot overrides is ramped, so leaving a snapshot returns what it
 * changed.  Bus gains and sends move linearly in gain per frame, effect
 * parameters per block.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_apply_snapshot (LrgMixerAsset  *self,
                                         LrgAudioMixer  *mixer,
                                         const gchar    *name,
                                         gdouble         blend_seconds,
                                         GError        **error);

/**
 * lrg_mixer_asset_blend_snapshots:
 * @self: an #LrgMixerAsset
 * @mixer: a mixer the layout was applied to
 * @names: (array length=n_snapshots): snapshot names (an empty string
 *   is the base layout)
 * @weights: (array length=n_snapshots): non-negative weights, not all 0
 * @n_snapshots: the number of snapshots
 * @blend_seconds: transition time, 0 or more
 * @error: (nullable): return location for an error
 *
 * Ramps to the weighted mean of several snapshots: volumes and send
 * levels average in dB, parameters linearly.  With one snapshot of
 * weight 1 this is lrg_mixer_asset_apply_snapshot().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mixer_asset_blend_snapshots (LrgMixerAsset       *self,
                                          LrgAudioMixer       *mixer,
                                          const gchar * const *names,
                                          const gdouble       *weights,
                                          guint                n_snapshots,
                                          gdouble              blend_seconds,
                                          GError             **error);

/* ==========================================================================
 * Project default mixer
 * ========================================================================== */

/**
 * lrg_mixer_asset_find_project_mixer:
 * @start: (type filename): a file or directory inside a project
 *
 * Finds the `project.ryaml` above @start (as
 * lrg_physics_settings_find_project() does) and reads its default mixer
 * reference, `audio: { mixer: <path> }` (at the root or inside
 * `project:`), resolved against the project directory.
 *
 * Returns: (transfer full) (nullable) (type filename): the `.rmixer`
 *   path, or %NULL when there is no project or no reference
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_mixer_asset_find_project_mixer (const gchar *start);

/**
 * lrg_mixer_asset_new_for_project:
 * @start: (type filename): a file or directory inside a project
 * @error: (nullable): return location for an error
 *
 * Loads the project's default mixer
 * (lrg_mixer_asset_find_project_mixer()).
 *
 * Returns: (transfer full) (nullable): the layout; %NULL with no error
 *   set when the project names none, %NULL with an error when the file
 *   does not load
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_mixer_asset_new_for_project (const gchar  *start,
                                                 GError      **error);

G_END_DECLS
