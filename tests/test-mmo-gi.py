#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Binding coverage for owned snapshots, out parameters and service transactions."""
import unittest
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Libregnum


class MmoBindings(unittest.TestCase):
    def test_services_and_receipts(self):
        store = Libregnum.MmoStore.new(':memory:')
        market = Libregnum.MmoMarket.new(store)
        self.assertTrue(market.grant('alice', 'coins', 10, 'reward'))
        self.assertTrue(market.grant('alice', 'coins', 10, 'reward'))
        self.assertEqual(market.get_balance('alice', 'coins'), 10)
        changes = GLib.Variant('a(stay)', [('character', 0, [1, 2, 3])])
        self.assertEqual(tuple(store.commit_once('save', changes)), (True, False))
        self.assertEqual(tuple(store.commit_once('save', changes)), (True, True))
        self.assertEqual(len(store.read_audit(0, 10).unpack()), 2)

    def test_wire_and_replication(self):
        payload = GLib.Bytes.new(b'hello')
        frame = Libregnum.mmo_protocol_encode(7, 1, payload, True)
        decoded, opcode, sequence = Libregnum.mmo_protocol_decode(frame)
        self.assertEqual(decoded.get_data(), b'hello')
        self.assertEqual((opcode, sequence), (7, 1))
        prediction = Libregnum.MmoPrediction.new(4)
        self.assertTrue(prediction.push(1, 2., 0., 0.))
        self.assertTrue(prediction.reconcile(1, 1., 0., 0.))
        self.assertEqual(prediction.get_position().unpack(), (1., 0., 0.))
        replica = Libregnum.MmoReplica.new(4)
        replica.reset('world-1')
        delta = GLib.Variant('(ta(ttddday)at)', (1, [(1, 1, 2., 3., 4., [5])], []))
        self.assertTrue(replica.apply('world-1', delta))
        self.assertEqual(replica.lookup(1).unpack(), (1, 2., 3., 4., [5]))


if __name__ == '__main__':
    unittest.main()
