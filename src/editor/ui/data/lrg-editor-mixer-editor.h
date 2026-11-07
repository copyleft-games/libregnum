/* lrg-editor-mixer-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMixerEditor: the lrgeditor data editor of `.rmixer` mixer
 * layouts (#LrgMixerAsset files).
 *
 * The top of the tab is a row of channel strips, one per bus in file
 * order (master first): the bus name and its parent, the effect chain
 * (click an effect to select it, its dot to bypass it), the sends with
 * their levels (drag sideways), a peak/RMS meter with a gain-reduction
 * bar beside a volume fader (drag; double-click for 0 dB), the level in
 * dB and Mute / Solo buttons. Below it three columns: the selected bus
 * or effect (a property grid: parent, volume, mute, solo, or the effect's
 * enabled flag and every parameter of its type with its ranges, plus
 * "Add effect", "Add send", "Add bus" and "Remove"), the snapshots (list,
 * blend time, default, Capture, Preview and a property grid of the
 * selected snapshot's overrides: bus volumes, send levels and numeric
 * effect parameters, each one resettable), and the ducking rules (list
 * plus the selected rule's threshold, depth, attack and release).
 *
 * The document text is the source of truth: every edit parses the text
 * into an #LrgMixerAsset, changes a copy through the engine API (which
 * validates routing, ranges and names) and writes the copy back with
 * lrg_mixer_asset_to_data() as one undo group, so saves stay byte-stable
 * and undo, dirty state, revert and external changes come from the data
 * editor base.
 *
 * Preview: "Preview" builds an offline #LrgAudioMixer from the layout
 * (lrg_mixer_asset_create_mixer()) and plays a looping test signal on
 * every bus without child buses (a tone pulsing at a per-bus rate), and
 * renders it every frame without an audio device, so the meters, solo,
 * mute, ducking and snapshot blends can be watched while editing. While
 * play-in-editor runs, the strips meter the audio manager's game mixer
 * instead.
 *
 * Opened by lrg_editor_data_editor_open() for `.rmixer` files (format
 * MIXER) and by the mixer.* IPC methods, which
 * lrg_editor_mixer_editor_install() registers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../audio/lrg-mixer-asset.h"
#include "../../../audio/lrg-audio-mixer.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

/**
 * LrgEditorMixerStripPart:
 * @LRG_EDITOR_MIXER_STRIP_HEADER: the bus name at the top of a strip
 *   (click selects the bus)
 * @LRG_EDITOR_MIXER_STRIP_EFFECT: one row of the effect chain (index:
 *   the effect's position)
 * @LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE: the bypass dot of an effect row
 * @LRG_EDITOR_MIXER_STRIP_SEND: one send row (index: the send's
 *   position); dragging it sideways changes the level
 * @LRG_EDITOR_MIXER_STRIP_FADER: the volume fader track
 * @LRG_EDITOR_MIXER_STRIP_METER: the peak/RMS meter beside the fader
 * @LRG_EDITOR_MIXER_STRIP_MUTE: the Mute button
 * @LRG_EDITOR_MIXER_STRIP_SOLO: the Solo button
 *
 * The parts of a channel strip, for
 * lrg_editor_mixer_strips_get_part_rect().
 */
typedef enum
{
	LRG_EDITOR_MIXER_STRIP_HEADER,
	LRG_EDITOR_MIXER_STRIP_EFFECT,
	LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE,
	LRG_EDITOR_MIXER_STRIP_SEND,
	LRG_EDITOR_MIXER_STRIP_FADER,
	LRG_EDITOR_MIXER_STRIP_METER,
	LRG_EDITOR_MIXER_STRIP_MUTE,
	LRG_EDITOR_MIXER_STRIP_SOLO
} LrgEditorMixerStripPart;

/**
 * lrg_editor_mixer_strip_part_get_type:
 *
 * Returns: the #GType of #LrgEditorMixerStripPart
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_mixer_strip_part_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_MIXER_STRIP_PART (lrg_editor_mixer_strip_part_get_type ())

/**
 * LRG_EDITOR_MIXER_FADER_MIN_DB:
 *
 * The level at the bottom of a fader's travel; dragging below it sets
 * silence (%LRG_AUDIO_SILENCE_DB).
 */
#define LRG_EDITOR_MIXER_FADER_MIN_DB (-60.0)

/**
 * LRG_EDITOR_MIXER_PREVIEW_RATE:
 *
 * The sample rate of the editor's offline preview mixer, in Hz.
 */
#define LRG_EDITOR_MIXER_PREVIEW_RATE (48000)

#define LRG_TYPE_EDITOR_MIXER_EDITOR (lrg_editor_mixer_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorMixerEditor, lrg_editor_mixer_editor, LRG, EDITOR_MIXER_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_mixer_editor_new:
 * @app: (nullable): the editor application
 * @document: a mixer document
 *
 * Creates an editor for @document without docking it (the data editor
 * registry and lrg_editor_data_editor_open() normally do that).
 *
 * Returns: (transfer full): a new #LrgEditorMixerEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMixerEditor * lrg_editor_mixer_editor_new (LrgEditorApp      *app,
                                                    LrgEditorDocument *document);

/**
 * lrg_editor_mixer_editor_install:
 * @app: the editor application
 *
 * Registers the mixer.* IPC methods (mixer.show, mixer.open,
 * mixer.create, mixer.set_default, mixer.get, mixer.effect_types,
 * mixer.add_bus, mixer.remove_bus, mixer.set_bus, mixer.set_send,
 * mixer.remove_send, mixer.add_effect, mixer.remove_effect,
 * mixer.move_effect, mixer.set_effect, mixer.add_snapshot,
 * mixer.remove_snapshot, mixer.set_snapshot, mixer.capture_snapshot,
 * mixer.set_duck, mixer.remove_duck, mixer.preview, mixer.meters,
 * mixer.save) and the "mixer.new" and "mixer.open" actions on @app.
 * Safe to call more than once; the Mixer panel and every mixer editor
 * call it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_mixer_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_mixer_editor_get_asset:
 * @self: an #LrgEditorMixerEditor
 *
 * The layout parsed from the document's current text (unsaved edits
 * included). It is replaced after every change of the document: copy it
 * (lrg_mixer_asset_copy()) to edit, then hand the copy to
 * lrg_editor_mixer_editor_commit().
 *
 * Returns: (transfer none) (nullable): the layout, %NULL while the text
 *   does not load (lrg_editor_mixer_editor_get_load_error())
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_editor_mixer_editor_get_asset (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_load_error:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (nullable): why the document text does not load as a
 *   layout, or %NULL when it does
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_mixer_editor_get_load_error (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_commit:
 * @self: an #LrgEditorMixerEditor
 * @edited: a changed copy of lrg_editor_mixer_editor_get_asset()
 * @label: (nullable): the undo label, e.g. "Set music volume"
 * @gesture: 0, or a gesture id from lrg_editor_data_editor_new_gesture():
 *   commits with the same id merge into one undo step (a fader drag)
 * @error: (nullable): return location for an error
 *
 * Validates @edited (lrg_mixer_asset_validate()) and writes it as the
 * whole document in one undoable step. Nothing is written when the text
 * does not change.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_editor_commit (LrgEditorMixerEditor  *self,
                                         LrgMixerAsset         *edited,
                                         const gchar           *label,
                                         guint                  gesture,
                                         GError               **error);

/**
 * lrg_editor_mixer_editor_get_selected_bus:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (nullable): the selected bus, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_mixer_editor_get_selected_bus (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_selected_effect:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (nullable): the id of the selected effect on the selected
 *   bus, %NULL when the bus itself is selected
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_mixer_editor_get_selected_effect (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_select:
 * @self: an #LrgEditorMixerEditor
 * @bus: (nullable): a bus name, %NULL for none
 * @effect: (nullable): an effect id on @bus, %NULL for the bus itself
 *
 * Selects what the left property grid shows.
 *
 * Returns: %TRUE when @bus (and @effect) exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_editor_select (LrgEditorMixerEditor *self,
                                         const gchar          *bus,
                                         const gchar          *effect);

/**
 * lrg_editor_mixer_editor_get_selected_snapshot:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (nullable): the snapshot shown in the snapshot grid
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_mixer_editor_get_selected_snapshot (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_select_snapshot:
 * @self: an #LrgEditorMixerEditor
 * @name: (nullable): a snapshot name, %NULL for none
 *
 * Returns: %TRUE when @name exists (or is %NULL)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_editor_select_snapshot (LrgEditorMixerEditor *self,
                                                  const gchar          *name);

/**
 * lrg_editor_mixer_editor_capture_snapshot:
 * @self: an #LrgEditorMixerEditor
 * @name: a snapshot name; created when missing
 * @error: (nullable): return location for an error
 *
 * Stores the base layout's current bus volumes and send levels as the
 * snapshot's overrides (one undo step): set the faders, capture, then
 * set them back. Effect-parameter overrides are kept.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_editor_capture_snapshot (LrgEditorMixerEditor  *self,
                                                   const gchar           *name,
                                                   GError               **error);

/**
 * lrg_editor_mixer_editor_set_previewing:
 * @self: an #LrgEditorMixerEditor
 * @previewing: whether the offline preview runs
 *
 * Starts or stops the preview mixer and its test signal.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_mixer_editor_set_previewing (LrgEditorMixerEditor *self,
                                             gboolean              previewing);

/**
 * lrg_editor_mixer_editor_get_previewing:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: whether the offline preview runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_editor_get_previewing (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_preview_mixer:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (transfer none) (nullable): the preview mixer while previewing
 */
LRG_AVAILABLE_IN_ALL
LrgAudioMixer * lrg_editor_mixer_editor_get_preview_mixer (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_preview_render:
 * @self: an #LrgEditorMixerEditor
 * @seconds: how much audio to render, 0 .. 1
 *
 * Renders @seconds of the preview mixer now (the widget does this every
 * frame with the frame time), so meters and ramps advance. Does nothing
 * while not previewing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_mixer_editor_preview_render (LrgEditorMixerEditor *self,
                                             gdouble               seconds);

/**
 * lrg_editor_mixer_editor_preview_snapshot:
 * @self: an #LrgEditorMixerEditor
 * @name: (nullable): a snapshot, %NULL for the base layout
 * @blend: the blend time in seconds; negative uses the snapshot's own
 * @error: (nullable): return location for an error
 *
 * Ramps the preview mixer to a snapshot (starting the preview when it
 * does not run). The snapshot stays applied across edits until another
 * one (or %NULL) is previewed.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_editor_preview_snapshot (LrgEditorMixerEditor  *self,
                                                   const gchar           *name,
                                                   gdouble                blend,
                                                   GError               **error);

/**
 * lrg_editor_mixer_editor_preview_blend:
 * @self: an #LrgEditorMixerEditor
 * @names: (array length=n_snapshots): snapshot names
 * @weights: (array length=n_snapshots): their weights
 * @n_snapshots: the number of snapshots
 * @blend: the blend time in seconds
 * @error: (nullable): return location for an error
 *
 * Ramps the preview mixer to a weighted mix of snapshots
 * (lrg_mixer_asset_blend_snapshots()).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_editor_preview_blend (LrgEditorMixerEditor  *self,
                                                const gchar * const   *names,
                                                const gdouble         *weights,
                                                guint                  n_snapshots,
                                                gdouble                blend,
                                                GError               **error);

/**
 * lrg_editor_mixer_editor_get_preview_snapshot:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (nullable): the snapshot the preview was last ramped to
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_mixer_editor_get_preview_snapshot (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_strips:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (transfer none): the channel strip widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_mixer_editor_get_strips (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_bus_grid:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (transfer none): the grid of the selected bus or effect
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_mixer_editor_get_bus_grid (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_snapshot_grid:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (transfer none): the grid of the selected snapshot
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_mixer_editor_get_snapshot_grid (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_duck_grid:
 * @self: an #LrgEditorMixerEditor
 *
 * Returns: (transfer none): the grid of the selected ducking rule
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_mixer_editor_get_duck_grid (LrgEditorMixerEditor *self);

/**
 * lrg_editor_mixer_editor_get_meter_mixer:
 * @app: the editor application
 * @fallback: (nullable): a mixer editor whose preview to use when the
 *   game does not run
 * @out_source: (out) (optional) (transfer none): "play", "preview" or
 *   "none"
 *
 * The mixer whose meters the strips show: the audio manager's game mixer
 * while play-in-editor runs, else @fallback's preview mixer while it
 * previews.
 *
 * Returns: (transfer none) (nullable): the mixer, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
LrgAudioMixer * lrg_editor_mixer_editor_get_meter_mixer (LrgEditorApp         *app,
                                                         LrgEditorMixerEditor *fallback,
                                                         const gchar         **out_source);

/**
 * lrg_editor_mixer_editor_find_for_path:
 * @app: the editor application
 * @path: (type filename): a `.rmixer` path (project-relative or absolute)
 * @open: whether to open the editor tab when it is not open
 * @error: (nullable): return location for an error
 *
 * The open mixer editor of @path, opened (and docked) when @open is set.
 *
 * Returns: (transfer none) (nullable): the editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMixerEditor * lrg_editor_mixer_editor_find_for_path (LrgEditorApp  *app,
                                                              const gchar   *path,
                                                              gboolean       open,
                                                              GError       **error);

/**
 * lrg_editor_mixer_editor_project_mixer_path:
 * @app: the editor application
 *
 * The project's default mixer (`audio.mixer` in project.ryaml) as an
 * absolute path.
 *
 * Returns: (transfer full) (nullable) (type filename): the path, %NULL
 *   without a project or a default mixer
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_mixer_editor_project_mixer_path (LrgEditorApp *app);

/* ==========================================================================
 * Channel strips (shared with the Mixer panel)
 * ========================================================================== */

/**
 * LrgEditorMixerStripsAssetFunc:
 * @user_data: the data given to lrg_editor_mixer_strips_new()
 *
 * Returns the layout the strips show.
 *
 * Returns: (transfer none) (nullable): the layout
 */
typedef LrgMixerAsset * (*LrgEditorMixerStripsAssetFunc) (gpointer user_data);

/**
 * LrgEditorMixerStripsEditorFunc:
 * @open: %TRUE when an edit is about to be committed (open the editor
 *   when it is not open), %FALSE to only look it up (selection, meters)
 * @user_data: the data given to lrg_editor_mixer_strips_new()
 *
 * Returns the editor that edits from the strips are committed to.
 *
 * Returns: (transfer none) (nullable): the editor, %NULL when there is
 *   none (and, with @open, the strips are read-only)
 */
typedef LrgEditorMixerEditor * (*LrgEditorMixerStripsEditorFunc) (gboolean open,
                                                                  gpointer user_data);

/**
 * lrg_editor_mixer_strips_new: (skip)
 * @app: (nullable): the editor application (meters while playing)
 * @compact: %TRUE for the Mixer panel's strips (name, meter, fader, Mute
 *   and Solo only), %FALSE for the editor's (effects and sends too)
 * @asset_func: (scope notified) (closure user_data): returns the layout shown
 * @editor_func: (scope notified) (closure user_data): returns the editor to commit to
 * @user_data: data for both functions
 * @destroy: (nullable): frees @user_data
 *
 * Creates a row of channel strips. The strips repaint every frame while
 * a meter source runs (lrg_editor_mixer_editor_get_meter_mixer()).
 *
 * Returns: (transfer full): the strips widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_mixer_strips_new (LrgEditorApp                   *app,
                                         gboolean                        compact,
                                         LrgEditorMixerStripsAssetFunc   asset_func,
                                         LrgEditorMixerStripsEditorFunc  editor_func,
                                         gpointer                        user_data,
                                         GDestroyNotify                  destroy);

/**
 * lrg_editor_mixer_strips_get_part_rect:
 * @strips: a widget from lrg_editor_mixer_strips_new()
 * @bus: a bus name
 * @part: the part of its strip
 * @index: the row of %LRG_EDITOR_MIXER_STRIP_EFFECT,
 *   %LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE and %LRG_EDITOR_MIXER_STRIP_SEND
 *   (ignored otherwise)
 * @x: (out): left edge in canvas coordinates
 * @y: (out): top edge
 * @width: (out): width
 * @height: (out): height
 *
 * Where a part of a strip was laid out (for tests and tools driving the
 * strips through the canvas).
 *
 * Returns: %TRUE when the bus and part exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_mixer_strips_get_part_rect (LrgWidget               *strips,
                                                const gchar             *bus,
                                                LrgEditorMixerStripPart  part,
                                                guint                    index,
                                                gfloat                  *x,
                                                gfloat                  *y,
                                                gfloat                  *width,
                                                gfloat                  *height);

/**
 * lrg_editor_mixer_fader_db_to_position:
 * @db: a level in dB
 *
 * The fader law of the strips: the position (0 bottom .. 1 top) of a
 * level, linear in dB over three segments: %LRG_EDITOR_MIXER_FADER_MIN_DB
 * at 0.05, -12 dB at 0.45, 0 dB at 0.75 and %LRG_AUDIO_MIXER_MAX_GAIN_DB
 * at 1. Levels below %LRG_EDITOR_MIXER_FADER_MIN_DB (silence included)
 * sit at 0.
 *
 * Returns: the position, 0 for silence
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_mixer_fader_db_to_position (gdouble db);

/**
 * lrg_editor_mixer_fader_position_to_db:
 * @position: a fader position, 0 .. 1
 *
 * The inverse of lrg_editor_mixer_fader_db_to_position(); a position
 * below 0.05 is silence (%LRG_AUDIO_SILENCE_DB).
 *
 * Returns: the level in dB
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_mixer_fader_position_to_db (gdouble position);

G_END_DECLS
