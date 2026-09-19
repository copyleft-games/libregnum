#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""TLS reference game: server authority, fenced persistence and acknowledged views."""
import argparse
import asyncio
from concurrent.futures import ThreadPoolExecutor
import functools
import hashlib
import json
import math
from pathlib import Path
import signal
import ssl
import struct
import time
import uuid
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Gio, Libregnum as Lrg

SNAPSHOT = '(ta(tssdddduududtttt))'


def wire(value):
    return json.dumps(value, separators=(',', ':'), allow_nan=False).encode()


def canonical(value):
    return value.byteswap() if __import__('sys').byteorder == 'big' else value


def read_variant(store, key, signature):
    try:
        data, revision = store.read(key)
    except GLib.Error as error:
        if error.matches(Gio.io_error_quark(), Gio.IOErrorEnum.NOT_FOUND):
            return None, 0
        raise
    value = GLib.Variant.new_from_bytes(GLib.VariantType.new(signature), data, False)
    if not value.is_normal_form():
        raise ValueError('Malformed stored state')
    return canonical(value), revision


def change(key, revision, value):
    return key, revision, canonical(value).get_data_as_bytes().get_data()


def open_store(args):
    if args.postgres_file:
        return Lrg.MmoStore.new_postgres(Path(args.postgres_file).read_text().strip())
    return Lrg.MmoStore.new(args.database)


class World:
    """All instances and their GObjects stay on one dedicated worker thread."""
    def __init__(self, args):
        self.args = args
        self.store = open_store(args)
        self.auth = Lrg.MmoAuth.new(self.store)
        self.directory = Lrg.MmoShardDirectory.new(self.store)
        self.zone, self.owner = args.zone, uuid.uuid4().hex
        self.endpoint = args.endpoint
        self.fence = 0
        self.key = 'runtime/world/' + self.zone
        self.roster = json.loads(Path(args.roster).read_text())
        if not isinstance(self.roster, list) or not 1 <= len(self.roster) <= 128:
            raise ValueError('Roster requires 1 to 128 characters')
        self.sim = None
        self.active = False
        self.clients = {}

    def maintain(self):
        try:
            return self._maintain()
        except GLib.Error as error:
            self.active = False
            if not error.matches(Gio.io_error_quark(), Gio.IOErrorEnum.PERMISSION_DENIED):
                # Never replay an ambiguous write: reopen and reacquire, then read
                # the durable checkpoint and receipt before accepting commands.
                self.store = open_store(self.args)
                self.auth = Lrg.MmoAuth.new(self.store)
                self.directory = Lrg.MmoShardDirectory.new(self.store)
            return False

    def _maintain(self):
        if self.active:
            try:
                self.directory.renew(self.zone, self.owner, self.fence, 5)
            except GLib.Error:
                self.active = False
                self.clients.clear()
                raise
            return True
        # Acquire before reading: the prior owner can no longer save a newer state.
        try:
            self.fence = self.directory.acquire(self.zone, self.owner, self.endpoint, 5)
        except GLib.Error as error:
            if error.matches(Gio.io_error_quark(), Gio.IOErrorEnum.BUSY):
                return False
            raise
        self.sim = Lrg.MmoSimulation.new(128)
        snapshot, self.revision = read_variant(self.store, self.key, SNAPSHOT)
        if snapshot:
            self.sim.restore(snapshot)
        else:
            for row in self.roster:
                self.sim.spawn(row['id'], row['account'], self.zone,
                               *row.get('position', [0., 0., 0.]), 0.25,
                               row.get('health', 100), row.get('faction', row['id']),
                               row.get('speed', 5.), row.get('damage', 10),
                               row.get('range', 3.), row.get('cooldown', 10))
            self.persist([])
        self.rep = Lrg.MmoReplicator.new(128, 64, 32.)
        self.refresh()
        self.clients.clear()
        self.clock = time.monotonic()
        self.active = True
        return True

    def persist(self, extra):
        snapshot = self.sim.snapshot()
        changes = [change(self.key, self.revision, snapshot)] + extra
        try:
            self.store.commit_fenced(self.zone, self.owner, self.fence,
                                     GLib.Variant('a(stay)', changes))
        except GLib.Error:
            self.active = False
            raise
        self.revision += 1

    def refresh(self):
        self.owners = {}
        for row in self.sim.snapshot().unpack()[1]:
            entity, account, zone = row[:3]
            if zone != self.zone:
                raise ValueError('Checkpoint contains another zone')
            self.owners[entity] = account
            state = GLib.Variant('(ut)', (row[7], row[14]))
            self.rep.upsert(entity, zone, *row[3:6], canonical(state).get_data_as_bytes())

    def disconnect(self, viewer):
        self.clients.pop(viewer, None)
        if self.sim is not None and hasattr(self, 'rep'):
            self.rep.forget(viewer)

    def execute(self, viewer, token, request):
        if not self.active:
            raise ValueError('Zone is recovering')
        lease = self.directory.lookup(self.zone).unpack()
        if lease[:2] != (self.fence, self.owner):
            self.active = False
            raise ValueError('Lease authority lost')
        # Every request revalidates the account generation, bans and token expiry.
        account = self.auth.verify(token, int(time.time()))
        entity = request.get('id')
        if type(entity) is not int or self.owners.get(entity) != account:
            raise ValueError('Character ownership required')
        op = request.get('op')
        if op == 'view':
            ack = request.get('ack', 0)
            if type(ack) is not int or not 0 <= ack < 2**64:
                raise ValueError('Invalid acknowledgment')
            previous = self.clients.get(viewer, 0)
            if ack and ack != previous:
                self.rep.acknowledge(viewer, ack)
                self.clients[viewer] = ack
            x, y, z, health, seq = self.sim.lookup(entity).unpack()
            delta, more = self.rep.build_page(viewer, self.zone, x, y, z, 64., 2048)
            return {'stream': self.owner + ':' + str(self.fence), 'delta': delta.unpack(),
                    'more': more, 'character': [x, y, z, health, seq]}
        sequence = request.get('sequence')
        if type(sequence) is not int or not 0 < sequence < 2**64:
            raise ValueError('Invalid command sequence')
        # One bounded latest receipt per character; older commands remain rejected
        # by the simulation's persistent sequence, even after process replacement.
        receipt_key = 'runtime/receipt/' + self.zone + '/' + str(entity)
        digest = hashlib.sha256(wire(request)).hexdigest()
        receipt, revision = read_variant(self.store, receipt_key, '(tss)')
        if receipt:
            old_seq, old_digest, old_result = receipt.unpack()
            if old_seq == sequence:
                if old_digest != digest:
                    raise ValueError('Sequence reused with different command')
                return json.loads(old_result)
        before = self.sim.snapshot()
        old_clock = self.clock
        elapsed = int((time.monotonic() - self.clock) / .05)
        try:
            if elapsed:
                self.sim.advance(min(elapsed, 20))
                self.clock = time.monotonic()
            if op == 'move':
                position = request.get('position')
                if not isinstance(position, list) or len(position) != 3 or any(
                        type(x) not in (int, float) or not math.isfinite(x) for x in position):
                    raise ValueError('Invalid position')
                self.sim.move(account, entity, sequence, *position)
            elif op == 'attack':
                target = request.get('target')
                if type(target) is not int or not 0 < target < 2**64:
                    raise ValueError('Invalid target')
                self.sim.attack(account, entity, sequence, target)
            else:
                raise ValueError('Unknown game command')
            result = {'character': self.sim.lookup(entity).unpack()}
            self.persist([change(receipt_key, revision,
                                 GLib.Variant('(tss)', (sequence, digest, wire(result).decode())))])
        except Exception:
            self.sim.restore(before)
            self.clock = old_clock
            raise
        self.refresh()
        return result

    def close(self):
        if self.active:
            self.directory.release(self.zone, self.owner, self.fence)
        self.active = False


async def serve(args):
    loop = asyncio.get_running_loop()
    pool = ThreadPoolExecutor(max_workers=1, thread_name_prefix='world')
    async def worker(function, *values):
        return await loop.run_in_executor(pool, functools.partial(function, *values))
    world = await worker(World, args)
    stopping = asyncio.Event()
    connections = set()
    handlers = set()
    viewer = 0
    gate = Lrg.MmoGate.new(4096, 20, 10.)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.certificate, args.key)

    async def client(reader, writer):
        nonlocal viewer
        viewer += 1
        current = viewer
        if len(connections) >= 64:
            writer.close()
            return
        connections.add(writer)
        handlers.add(asyncio.current_task())
        address = writer.get_extra_info('peername')[0]
        try:
            while not stopping.is_set():
                header = await asyncio.wait_for(reader.readexactly(4), timeout=30)
                length, = struct.unpack('!I', header)
                if not 1 <= length <= 8192:
                    break
                raw = await asyncio.wait_for(reader.readexactly(length), timeout=10)
                if not gate.admit(address, GLib.get_monotonic_time()):
                    break
                request = json.loads(raw, parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
                if not isinstance(request, dict):
                    break
                token = request.pop('token', '')
                if not isinstance(token, str) or len(token) != 64:
                    break
                try:
                    result = await worker(world.execute, current, token, request)
                    reply = {'ok': True, 'result': result}
                except (GLib.Error, ValueError, OverflowError, TypeError):
                    reply = {'ok': False, 'error': 'Command rejected or zone unavailable'}
                data = wire(reply)
                writer.write(struct.pack('!I', len(data)) + data)
                await asyncio.wait_for(writer.drain(), timeout=5)
        except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError,
                ValueError, UnicodeError, GLib.Error):
            pass
        finally:
            connections.discard(writer)
            await worker(world.disconnect, current)
            writer.close()
            try:
                await writer.wait_closed()
            except (OSError, ssl.SSLError):
                pass
            handlers.discard(asyncio.current_task())

    async def maintain():
        while not stopping.is_set():
            try:
                await worker(world.maintain)
            except (GLib.Error, ValueError):
                # Fail closed; acquire a fresh lease and reread state on retry.
                pass
            try:
                await asyncio.wait_for(stopping.wait(), timeout=1)
            except asyncio.TimeoutError:
                pass

    server = await asyncio.start_server(client, args.bind, args.port, ssl=context,
                                        ssl_handshake_timeout=5, limit=16384)
    task = asyncio.create_task(maintain())
    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, stopping.set)
    print('READY port=' + str(server.sockets[0].getsockname()[1]), flush=True)
    try:
        await stopping.wait()
    finally:
        server.close()
        await server.wait_closed()
        for connection in list(connections):
            connection.close()
        await task
        if handlers:
            await asyncio.gather(*list(handlers))
        await worker(world.close)
        pool.shutdown(wait=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog='AGPL-3.0-or-later. Example: '
        'mmo-runtime.py --database world.db --roster roster.json --certificate cert.pem --key key.pem')
    parser.add_argument('--version', action='version', version='libregnum MMO runtime 1 (AGPL-3.0-or-later)')
    database = parser.add_mutually_exclusive_group(required=True)
    database.add_argument('--database')
    database.add_argument('--postgres-file')
    parser.add_argument('--roster', required=True)
    parser.add_argument('--certificate', required=True)
    parser.add_argument('--key', required=True)
    parser.add_argument('--zone', default='world')
    parser.add_argument('--endpoint', default='tls://localhost:7778')
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=7778)
    asyncio.run(serve(parser.parse_args()))


if __name__ == '__main__':
    main()
