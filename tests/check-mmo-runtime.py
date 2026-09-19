#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Opt-in TLS gameplay/recovery integration. Uses an isolated local world."""
import importlib.util
import json
import os
from pathlib import Path
import selectors
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Libregnum as Lrg

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('client', ROOT / 'examples/mmo-client.py')
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


def main():
    with tempfile.TemporaryDirectory(prefix='lrg-runtime-') as directory:
        directory = Path(directory)
        database = directory / 'world.db'
        roster = directory / 'roster.json'
        alice, bob = 'alice' + uuid.uuid4().hex, 'bob' + uuid.uuid4().hex
        roster.write_text(json.dumps([{'id': 1, 'account': alice, 'position': [0., 0., 0.]},
                                      {'id': 2, 'account': bob, 'position': [2., 0., 0.]}]))
        options = ['--database', str(database)]
        if os.environ.get('LRG_TEST_POSTGRES'):
            connection = directory / 'connection'
            connection.write_text(os.environ['LRG_TEST_POSTGRES'])
            connection.chmod(0o600)
            options = ['--postgres-file', str(connection)]
            store = Lrg.MmoStore.new_postgres(os.environ['LRG_TEST_POSTGRES'])
        else:
            store = Lrg.MmoStore.new(str(database))
        auth = Lrg.MmoAuth.new(store)
        for account in (alice, bob):
            auth.register(account, 'private-test-password')
        token = auth.login(alice, 'private-test-password', int(time.time()))
        bob_token = auth.login(bob, 'private-test-password', int(time.time()))
        cert = ROOT / 'tests/fixtures/mmo-test-cert.pem'
        key = ROOT / 'tests/fixtures/mmo-test-key.pem'
        context = ssl.create_default_context(cafile=str(cert))
        hosts = []
        logs = []
        def start():
            log = (directory / ('host-' + str(len(hosts)) + '.log')).open('w+')
            logs.append(log)
            process = subprocess.Popen([sys.executable, str(ROOT / 'examples/mmo-runtime.py'),
                *options, '--roster', str(roster), '--certificate', str(cert), '--key', str(key),
                '--zone', alice, '--port', '0'], stdout=subprocess.PIPE, stderr=log)
            hosts.append(process)
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                assert selector.select(20), 'Startup timeout'
            line = process.stdout.readline().decode().strip()
            assert line.startswith('READY port='), line
            return process, int(line.split('=')[1])
        def connect(port):
            return context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=10),
                                       server_hostname='localhost')
        def request(stream, op, **kwargs):
            return client.request(stream, {'op': op, 'token': token, 'id': 1, **kwargs})
        try:
            first, port = start()
            with connect(port) as stream:
                deadline = time.monotonic() + 15
                while True:
                    reply = request(stream, 'view')
                    if reply['ok']:
                        break
                    assert time.monotonic() < deadline, 'Owner did not start'
                    time.sleep(.2)
                initial = reply['result']
                assert len(initial['delta'][1]) == 2
                replica = Lrg.MmoReplica.new(128)
                replica.reset(initial['stream'])
                replica.apply(initial['stream'], GLib.Variant('(ta(ttddday)at)', initial['delta']))
                assert replica.lookup(2) is not None
                assert not request(stream, 'move', sequence=1, position=[999., 0., 0.])['ok']
                assert not request(stream, 'attack', id=2, sequence=1, target=1)['ok']
                time.sleep(.15)
                move = request(stream, 'move', sequence=1, position=[.5, 0., 0.])
                assert move['ok'], move
                assert request(stream, 'move', sequence=1, position=[.5, 0., 0.]) == move
                assert not request(stream, 'move', sequence=1, position=[.6, 0., 0.])['ok']
                time.sleep(.5)
                assert request(stream, 'attack', sequence=2, target=2)['ok']
                assert not request(stream, 'attack', sequence=3, target=2)['ok']
                view = request(stream, 'view', ack=initial['delta'][0])
                assert view['ok'], view
                replica.apply(initial['stream'], GLib.Variant('(ta(ttddday)at)', view['result']['delta']))
                public = GLib.Variant.new_from_bytes(GLib.VariantType.new('(ut)'),
                    GLib.Bytes.new(bytes(replica.lookup(2).unpack()[4])), False)
                if sys.byteorder == 'big':
                    public = public.byteswap()
                assert public.unpack()[0] == 90
                second, second_port = start()
                with connect(second_port) as standby:
                    assert not request(standby, 'view')['ok']
                first.kill()
                first.wait(timeout=10)
            with connect(second_port) as stream:
                deadline = time.monotonic() + 15
                while True:
                    recovered = request(stream, 'view')
                    if recovered['ok']:
                        break
                    assert time.monotonic() < deadline, 'Standby did not recover'
                    time.sleep(.5)
                state = recovered['result']
                assert state['stream'] != initial['stream']
                assert state['character'] == [.5, 0., 0., 100, 2], state
                time.sleep(.5)
                assert request(stream, 'attack', sequence=2, target=2)['ok']
                # Durable retry after owner replacement must not apply damage twice.
                with connect(second_port) as other:
                    bob_state = client.request(other, {'op': 'view', 'id': 2, 'token': bob_token})
                    assert bob_state['result']['character'][3] == 90
                auth.moderate('operator', alice, True, 'integration ban', 'ban' + uuid.uuid4().hex)
                assert not request(stream, 'view')['ok']
            print('PASS gameplay ownership, limits, combat, replicas, durable retry, kill/recovery and live ban')
        finally:
            for process in hosts:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            for log in logs:
                log.seek(0)
                diagnostics = log.read()
                if diagnostics:
                    print(diagnostics, file=sys.stderr)
                log.close()


if __name__ == '__main__':
    main()
