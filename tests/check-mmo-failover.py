#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Kill the active zone worker and verify standby checkpoint takeover."""
from pathlib import Path
import os
import re
import selectors
import subprocess
import sys
import tempfile
import time
import uuid


def owner(process):
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        assert selector.select(12), 'No ownership acquired before deadline'
    line = process.stdout.readline().decode()
    match = re.fullmatch(r'OWNER ([\w-]+) fence=(\d+) revision=(\d+)\n', line)
    assert match, line
    return int(match[2]), int(match[3])


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='lrg-failover-') as directory:
        directory = Path(directory)
        if os.environ.get('LRG_TEST_POSTGRES'):
            connection = directory / 'connection'
            connection.write_text(os.environ['LRG_TEST_POSTGRES'])
            connection.chmod(0o600)
            backend = ['--postgres-file', str(connection)]
        else:
            backend = ['--database', str(directory / 'world.db')]
        base = [executable, *backend, '--zone', str(uuid.uuid4()), '--endpoint', 'tls://localhost:7777']
        with (directory / 'workers.log').open('w+') as log:
            first = subprocess.Popen(base + ['--owner', 'worker-a'], stdout=subprocess.PIPE, stderr=log)
            second = None
            try:
                original, _ = owner(first)
                # Acquisition emits before the first synchronous checkpoint completes.
                time.sleep(1.2)
                second = subprocess.Popen(base + ['--owner', 'worker-b'], stdout=subprocess.PIPE, stderr=log)
                first.kill()
                first.wait(timeout=5)
                replacement, revision = owner(second)
                assert replacement > original and revision > 0, (replacement, original, revision)
                second.terminate()
                assert second.wait(timeout=10) == 0
                print(f'PASS: killed worker fence={original}; standby fence={replacement}, restored revision={revision}')
            finally:
                for process in (first, second):
                    if process is not None:
                        if process.poll() is None:
                            process.kill()
                            process.wait(timeout=5)
                        process.stdout.close()
                log.seek(0)
                errors = log.read()
                assert 'CRITICAL' not in errors and 'WARNING' not in errors, errors


if __name__ == '__main__':
    main()
