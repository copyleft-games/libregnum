#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Public binding regressions; run with make test-followups-gi."""
import struct
import tempfile
from pathlib import Path
import unittest

import gi

gi.require_version("Libregnum", "1")
from gi.repository import Libregnum


class FollowupBindings(unittest.TestCase):
    def test_random_snapshot(self):
        stream = Libregnum.RandomStream.new(42, 54)
        self.assertEqual(stream.next_uint(), 0xa15c02b7)
        state = stream.snapshot()
        expected = stream.next_double()
        self.assertTrue(stream.restore(state))
        self.assertEqual(stream.next_double(), expected)
        self.assertLess(stream.bounded(7), 7)

    def test_asset_dependencies(self):
        registry = Libregnum.Registry.new()
        registry.register_builtin()
        loader = Libregnum.DataLoader.new()
        loader.set_registry(registry)
        assets = Libregnum.AssetManager.new()
        assets.set_data_loader(loader)
        with tempfile.TemporaryDirectory() as directory:
            assets.add_search_path(directory)
            for name in ("source.yaml", "consumer.yaml"):
                Path(directory, name).write_text("type: item-def\nvalue: 1\n")
                self.assertIsNotNone(assets.load_object(name))
            self.assertTrue(assets.add_object_dependency("consumer.yaml", "source.yaml"))
            received = []
            assets.connect("object-reloaded", lambda obj, name, old, new: received.append(name))
            self.assertTrue(assets.reload_object("source.yaml"))
            self.assertEqual(received, ["source.yaml", "consumer.yaml"])
            self.assertTrue(assets.remove_object_dependency("consumer.yaml", "source.yaml"))
            assets.unload_all()

    def test_mesh_arrays(self):
        mesh = Libregnum.NavMesh.new()
        self.assertTrue(mesh.bake(
            [0., 0., 0., 2., 0., 0., 2., 0., 2., 0., 0., 2.],
            [0, 1, 2, 0, 2, 3], 45.))
        # Regression: the fixed-size output array must be caller-allocated.
        success, projected, polygon = mesh.project([1., 1., .25], 1.)
        self.assertTrue(success)
        self.assertEqual(projected, [1., 0., .25])
        self.assertEqual(polygon, 0)
        path = mesh.find_path([1.5, 0., .25], [.25, 0., 1.5], .01)
        self.assertEqual(len(path), 9)
        self.assertEqual(path[3:6], [1., 0., 1.])
        self.assertTrue(mesh.set_enabled(1, False))

    def test_mixer_samples(self):
        mixer = Libregnum.AudioMixer.new(8000, 1)
        self.assertTrue(mixer.add_bus("sfx", "master"))
        self.assertTrue(mixer.set_bus("sfx", .5, False))
        self.assertEqual(mixer.render(4), [0., 0., 0., 0.])
        wave = Libregnum.WaveData.new_from_samples(
            8000, 32, 1, struct.pack("=4f", .5, -.5, .25, -.25))
        voice = mixer.play(wave, "sfx", 1., False)
        self.assertGreater(voice, 0)
        del wave
        self.assertEqual(mixer.render(4), [.25, -.25, .125, -.125])
        self.assertEqual(mixer.get_voice_count(), 0)


if __name__ == "__main__":
    unittest.main()
