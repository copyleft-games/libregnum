#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reproducible local TLS load sample, not a production capacity guarantee."""
import concurrent.futures
import importlib.util
import json
import os
from pathlib import Path
import re
import selectors
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('mmo_smoke', Path(__file__).with_name('check-mmo-server.py'))
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def main():
    executable = str(Path(sys.argv[1]).resolve())
    fixtures = Path(__file__).resolve().parent / 'fixtures'
    clients = 6
    requests = 20
    with tempfile.TemporaryDirectory(prefix='lrg-load-') as directory:
        directory = Path(directory)
        password = directory / 'password'
        password.write_text('load-test-only-password')
        password.chmod(0o600)
        prefix = ''
        if os.environ.get('LRG_TEST_POSTGRES'):
            connection = directory / 'connection'
            connection.write_text(os.environ['LRG_TEST_POSTGRES'])
            connection.chmod(0o600)
            base = [executable, '--postgres-file', str(connection)]
            prefix = uuid.uuid4().hex
        else:
            base = [executable, '--database', str(directory / 'world.db')]
        tokens = []
        for i in range(clients):
            account = f'{prefix}load{i}'
            subprocess.run(base + ['--register', account, '--password-file', str(password)], check=True)
            tokens.append(subprocess.check_output(base + ['--login', account, '--password-file', str(password)]).strip())
        with (directory / 'host.log').open('w+') as log:
            server = subprocess.Popen(base + ['--certificate', str(fixtures / 'mmo-test-cert.pem'),
                                      '--key', str(fixtures / 'mmo-test-key.pem'), '--port', '0', '--metrics-port', '0'],
                                      stdout=subprocess.PIPE, stderr=log)
            try:
                with selectors.DefaultSelector() as selector:
                    selector.register(server.stdout, selectors.EVENT_READ)
                    assert selector.select(10), 'Server startup timeout'
                ready = server.stdout.readline().decode()
                port = int(re.fullmatch(r'READY tls-port=(\d+) metrics-port=(\d+)\n', ready)[1])
                context = ssl.create_default_context(cafile=str(fixtures / 'mmo-test-cert.pem'))
                def run(token):
                    samples = []
                    with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=10),
                                             server_hostname='localhost') as connection:
                        assert smoke.request(connection, 1, 1, token)[0] == 1
                        for sequence in range(2, requests + 2):
                            start = time.perf_counter()
                            assert smoke.request(connection, 2, sequence) == b'\x01pong\0'
                            samples.append((time.perf_counter() - start) * 1000)
                    return samples
                start = time.perf_counter()
                with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as pool:
                    samples = sorted(value for batch in pool.map(run, tokens) for value in batch)
                elapsed = time.perf_counter() - start
                print(json.dumps({'clients': clients, 'successful_requests': len(samples),
                                  'elapsed_seconds': round(elapsed, 3), 'p50_ms': round(samples[len(samples)//2], 3),
                                  'p95_ms': round(samples[int(len(samples)*.95)-1], 3), 'errors': 0}))
            finally:
                server.terminate()
                try:
                    code = server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
                    raise AssertionError('Shutdown timeout')
                server.stdout.close()
                log.seek(0)
                errors = log.read()
                assert code == 0 and 'CRITICAL' not in errors and 'WARNING' not in errors, errors


if __name__ == '__main__':
    main()
