#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Public binding regressions; run with make test-followups-gi."""
import struct
import unittest

import gi

gi.require_version("Libregnum", "1")
from gi.repository import Libregnum


class FollowupBindings(unittest.TestCase):
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
