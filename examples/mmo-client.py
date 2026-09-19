#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Interactive headless RPG client: view, move X Y Z, attack ID, quit."""
import argparse
import json
from pathlib import Path
import socket
import ssl
import struct
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Libregnum as Lrg


def receive(stream, count):
    data = b''
    while len(data) < count:
        part = stream.recv(count - len(data))
        if not part:
            raise ConnectionError('Server disconnected')
        data += part
    return data


def request(stream, command):
    data = json.dumps(command, allow_nan=False).encode()
    if len(data) > 8192:
        raise ValueError('Command too large')
    stream.sendall(struct.pack('!I', len(data)) + data)
    size, = struct.unpack('!I', receive(stream, 4))
    if not 0 < size <= 65536:
        raise ValueError('Invalid response size')
    return json.loads(receive(stream, size))


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog='AGPL-3.0-or-later. Example: '
        'mmo-client.py localhost 7778 --ca ca.pem --token-file token --id 1')
    parser.add_argument('--version', action='version', version='libregnum MMO client 1 (AGPL-3.0-or-later)')
    parser.add_argument('host')
    parser.add_argument('port', type=int)
    parser.add_argument('--ca', required=True)
    parser.add_argument('--token-file', required=True)
    parser.add_argument('--id', required=True, type=int)
    args = parser.parse_args()
    token_path = Path(args.token_file)
    if token_path.stat().st_mode & 0o077:
        parser.error('Token file must be private')
    token = token_path.read_text().strip()
    context = ssl.create_default_context(cafile=args.ca)
    replica = Lrg.MmoReplica.new(128)
    stream_id, ack, sequence = None, 0, 0
    with context.wrap_socket(socket.create_connection((args.host, args.port), timeout=10),
                             server_hostname=args.host) as stream:
        while True:
            reply = request(stream, {'op': 'view', 'id': args.id, 'token': token, 'ack': ack})
            if not reply['ok']:
                raise ValueError(reply['error'])
            state = reply['result']
            if stream_id != state['stream']:
                stream_id, ack = state['stream'], 0
                replica.reset(stream_id)
            delta = GLib.Variant('(ta(ttddday)at)', state['delta'])
            replica.apply(stream_id, delta)
            ack = state['delta'][0]
            sequence = max(sequence, state['character'][4])
            print('character:', state['character'])
            for row in state['delta'][1]:
                print('visible:', row[0], replica.lookup(row[0]).unpack())
            if state['more']:
                continue
            try:
                words = input('view / move X Y Z / attack ID / quit> ').split()
            except EOFError:
                break
            if words == ['quit']:
                break
            if words == ['view'] or not words:
                continue
            command = {'id': args.id, 'token': token, 'sequence': sequence + 1}
            if words[0] == 'move' and len(words) == 4:
                command.update(op='move', position=list(map(float, words[1:])))
            elif words[0] == 'attack' and len(words) == 2:
                command.update(op='attack', target=int(words[1]))
            else:
                print('Invalid command')
                continue
            result = request(stream, command)
            print(result)
            if result['ok']:
                sequence += 1


if __name__ == '__main__':
    main()
