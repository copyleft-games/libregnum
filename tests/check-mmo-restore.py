#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Restore drill for an explicitly supplied disposable PostgreSQL container."""
import hashlib
import subprocess
import sys
import tempfile
import uuid


def main():
    container = sys.argv[1]
    source = sys.argv[2]
    restored = 'lrg_restore_' + uuid.uuid4().hex
    def run(*args, **kwargs):
        return subprocess.run(['podman', 'exec', '-i', container, *args], check=True, **kwargs)
    def digest(database):
        result = hashlib.sha256()
        for table, key in [('lrg_records', 'key'), ('lrg_operations', 'id'),
                           ('lrg_audit', 'sequence'), ('lrg_leases', 'zone')]:
            sql = f"SELECT row_to_json(t) FROM {table} t ORDER BY {key};"
            result.update(run('psql', '-U', 'postgres', '-d', database, '-Atc', sql, capture_output=True).stdout)
        return result.digest()
    with tempfile.TemporaryFile() as archive:
        run('pg_dump', '-U', 'postgres', '-Fc', source, stdout=archive)
        run('createdb', '-U', 'postgres', restored)
        try:
            archive.seek(0)
            run('pg_restore', '-U', 'postgres', '-d', restored, stdin=archive)
            assert digest(source) == digest(restored)
            print('PASS: PostgreSQL dump/restore retains records, retry receipts, audit and leases')
        finally:
            run('dropdb', '-U', 'postgres', restored)


if __name__ == '__main__':
    main()
