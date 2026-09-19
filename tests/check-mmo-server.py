#!/usr/bin/env python3
"""Opt-in loopback integration: real TLS, token ingress, metrics, backup and drain.
SPDX-License-Identifier: AGPL-3.0-or-later
"""
import os
from pathlib import Path
import re
import selectors
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import urllib.request


def receive(stream, count):
    result = b''
    while len(result) < count:
        part = stream.recv(count - len(result))
        if not part:
            raise AssertionError('Unexpected EOF')
        result += part
    return result


def request(stream, opcode, sequence, payload=b''):
    frame = struct.pack('!4sBBHQII', b'LRGM', 1, 0, opcode, sequence,
                        len(payload), len(payload)) + payload
    stream.sendall(struct.pack('!BBIIIqI', 1, 0, 999, 0, 0, 0, len(frame)) + frame)
    header = receive(stream, 26)
    length = struct.unpack('!I', header[-4:])[0]
    assert 24 <= length <= 1048576
    response = receive(stream, length)
    magic, version, flags, reply, seq, raw, stored = struct.unpack('!4sBBHQII', response[:24])
    assert (magic, version, flags, reply, seq) == (b'LRGM', 1, 0, opcode | 0x8000, sequence)
    assert raw == stored == len(response) - 24
    return response[24:]


def main():
    executable = str(Path(sys.argv[1]).resolve())
    fixtures = Path(__file__).resolve().parent / 'fixtures'
    with tempfile.TemporaryDirectory(prefix='libregnum-host-') as directory:
        directory = Path(directory)
        database = directory / 'world.db'
        password = directory / 'password'
        password.write_text('test-only-password-1234\n')
        password.chmod(0o600)
        base = [executable, '--database', str(database)]
        subprocess.run(base + ['--register', 'alice', '--password-file', str(password)], check=True)
        token = subprocess.check_output(base + ['--login', 'alice', '--password-file', str(password)]).strip()
        assert len(token) == 64
        with (directory / 'server.log').open('w+') as errors:
            host = subprocess.Popen(base + ['--certificate', str(fixtures / 'mmo-test-cert.pem'),
                                    '--key', str(fixtures / 'mmo-test-key.pem'), '--port', '0',
                                    '--metrics-port', '0'], stdout=subprocess.PIPE, stderr=errors)
            try:
                with selectors.DefaultSelector() as selector:
                    selector.register(host.stdout, selectors.EVENT_READ)
                    assert selector.select(10), 'Host startup timeout'
                ready = host.stdout.readline().decode()
                match = re.fullmatch(r'READY tls-port=(\d+) metrics-port=(\d+)\n', ready)
                assert match, ready
                port, metrics = map(int, match.groups())
                context = ssl.create_default_context(cafile=str(fixtures / 'mmo-test-cert.pem'))
                def connect():
                    return context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=5),
                                               server_hostname='localhost')
                with connect() as stream:
                    reply = request(stream, 1, 1, token)
                    assert reply == b'\x01alice\0', repr(reply)
                    assert request(stream, 2, 2) == b'\x01pong\0'
                    with urllib.request.urlopen(f'http://127.0.0.1:{metrics}/healthz', timeout=5) as response:
                        assert response.read() == b'ready\n'
                    with urllib.request.urlopen(f'http://127.0.0.1:{metrics}/metrics', timeout=5) as response:
                        assert b'libregnum_sessions 1\n' in response.read()
                    # Replayed command closes the connection, never executes it twice.
                    frame = struct.pack('!4sBBHQII', b'LRGM', 1, 0, 2, 2, 0, 0)
                    stream.sendall(struct.pack('!BBIIIqI', 1, 0, 0, 0, 0, 0, len(frame)) + frame)
                    assert stream.recv(1) == b''
                with connect() as stream:
                    assert request(stream, 1, 1, b'0' * 64).startswith(b'\0Request rejected')
                subprocess.run(base + ['--backup', str(directory / 'backup.db')], check=True)
                assert (directory / 'backup.db').stat().st_size > 0
            finally:
                host.terminate()
                try:
                    code = host.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait()
                    raise AssertionError('Graceful shutdown timeout')
                host.stdout.close()
                errors.seek(0)
                log = errors.read()
                assert code == 0, log
                assert 'CRITICAL' not in log and 'WARNING' not in log, log
    print('PASS: TLS host authentication, replay rejection, metrics, backup and shutdown')


if __name__ == '__main__':
    main()
