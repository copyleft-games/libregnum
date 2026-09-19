#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Opt-in isolated Patroni/etcd HA drill. Creates and removes only its own containers."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Libregnum as Lrg

IMAGE = 'localhost/libregnum-patroni:4.1.5'
ETCD = 'quay.io/coreos/etcd:v3.5.21'


def run(*args, check=True):
    return subprocess.run(['podman', *args], check=check, capture_output=True, text=True, timeout=90)


def wait(check, description, timeout=100):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(1)
    raise AssertionError('Timed out: ' + description)


def main():
    suffix = uuid.uuid4().hex[:10]
    network = 'lrg-ha-' + suffix
    etcds = [network + '-e' + str(i) for i in range(3)]
    databases = [network + '-p' + str(i) for i in range(3)]
    created = []
    password = uuid.uuid4().hex
    run('network', 'create', network)
    try:
        with tempfile.TemporaryDirectory(prefix='lrg-ha-') as directory:
            cluster = ','.join(name + '=http://' + name + ':2380' for name in etcds)
            for name in etcds:
                run('run', '-d', '--name', name, '--network', network, ETCD,
                    'etcd', '--name', name, '--data-dir', '/etcd-data',
                    '--listen-client-urls', 'http://0.0.0.0:2379',
                    '--advertise-client-urls', 'http://' + name + ':2379',
                    '--listen-peer-urls', 'http://0.0.0.0:2380',
                    '--initial-advertise-peer-urls', 'http://' + name + ':2380',
                    '--initial-cluster', cluster, '--initial-cluster-token', suffix)
                created.append(name)
            for name in databases:
                config = {
                    'scope': network, 'name': name,
                    'restapi': {'listen': '0.0.0.0:8008', 'connect_address': name + ':8008'},
                    'etcd3': {'hosts': [x + ':2379' for x in etcds]},
                    'bootstrap': {'dcs': {'ttl': 20, 'loop_wait': 2, 'retry_timeout': 5,
                        'synchronous_mode': True, 'synchronous_mode_strict': True,
                        'synchronous_node_count': 1, 'failsafe_mode': False,
                        'postgresql': {'use_pg_rewind': True, 'parameters': {
                            'synchronous_commit': 'remote_apply', 'wal_log_hints': 'on'}}},
                        'initdb': [{'encoding': 'UTF8'}, 'data-checksums'],
                        'pg_hba': ['host replication replicator all scram-sha-256',
                                   'host all all all scram-sha-256']},
                    'postgresql': {'listen': '0.0.0.0:5432', 'connect_address': name + ':5432',
                        'data_dir': '/var/lib/postgresql/data/pgdata',
                        'authentication': {'superuser': {'username': 'postgres', 'password': password},
                                           'replication': {'username': 'replicator', 'password': password}},
                        'parameters': {'unix_socket_directories': '/tmp'}},
                    # A container crash drill cannot validate host hardware watchdogs.
                    'watchdog': {'mode': 'off'}}
                path = Path(directory) / (name + '.json')
                path.write_text(json.dumps(config))
                path.chmod(0o644)
                run('run', '-d', '--name', name, '--network', network,
                    '-p', '127.0.0.1::5432', '-v', str(path) + ':/patroni.json:ro,Z',
                    IMAGE, '/patroni.json')
                created.append(name)
            def sql(name, statement):
                return run('exec', '-e', 'PGPASSWORD=' + password, name,
                           'psql', '-h', '127.0.0.1', '-U', 'postgres', '-Atc', statement, check=False)
            def primary(excluded=()):
                candidates = [name for name in databases if name not in excluded and
                              sql(name, 'select pg_is_in_recovery()').stdout.strip() == 'f']
                return candidates[0] if len(candidates) == 1 else None
            leader = wait(primary, 'initial primary election')
            wait(lambda: sql(leader, "select count(*) from pg_stat_replication where sync_state='sync'").stdout.strip() == '1',
                 'synchronous replica')
            wait(lambda: json.loads(run('exec', etcds[0], 'etcdctl', 'get',
                '/service/' + network + '/sync', '--print-value-only').stdout or '{}').get('sync_standby'),
                'synchronous standby registered in DCS')
            ports = [run('port', name, '5432').stdout.strip().rsplit(':', 1)[1] for name in databases]
            connection = 'host=' + ','.join(['127.0.0.1'] * 3) + ' port=' + ','.join(ports) + \
                         ' user=postgres dbname=postgres password=' + password + \
                         ' sslmode=disable target_session_attrs=read-write connect_timeout=2'
            def open_store():
                try:
                    return Lrg.MmoStore.new_postgres(connection)
                except GLib.Error:
                    return None
            store = wait(open_store, 'library connection')
            batch = GLib.Variant('a(stay)', [('ha-proof', 0, b'acknowledged before primary loss')])
            assert tuple(store.commit_once('ha-operation', batch)) == (True, False)
            directory_service = Lrg.MmoShardDirectory.new(store)
            fence = directory_service.acquire('ha-zone', 'worker-a', 'tls://a', 120)
            print('PRIMARY ' + leader + ' synchronous commit acknowledged', flush=True)
            run('kill', '--signal', 'KILL', leader)
            assert run('inspect', '--format', '{{.State.Running}}', leader).stdout.strip() == 'false'
            replacement = wait(lambda: primary([leader]), 'automatic primary replacement')
            store = wait(open_store, 'library reconnect to new writable primary')
            data, revision = store.read('ha-proof')
            assert data.get_data() == b'acknowledged before primary loss' and revision == 1
            assert tuple(store.commit_once('ha-operation', batch)) == (True, True)
            directory_service = Lrg.MmoShardDirectory.new(store)
            next_fence = directory_service.handoff('ha-zone', 'worker-a', fence, 'worker-b',
                                                    'tls://b', GLib.Bytes.new(b'checkpoint'), 120)
            try:
                store.commit_fenced('ha-zone', 'worker-a', fence,
                                    GLib.Variant('a(stay)', [('stale', 0, b'forbidden')]))
            except GLib.Error:
                pass
            else:
                raise AssertionError('Stale worker fence accepted after primary replacement')
            assert next_fence > fence
            run('start', leader)
            wait(lambda: sql(leader, 'select pg_is_in_recovery()').stdout.strip() == 't', 'old primary rejoins as replica')
            print('PASS primary election, acknowledged data, receipts, shard fencing and old-primary rejoin', flush=True)
            # Remove DCS majority. Patroni must demote rather than continue writes.
            for name in etcds[:2]:
                run('stop', '-t', '5', name)
            time.sleep(25)
            for name in databases:
                response = sql(name, "set statement_timeout='2s'; create table forbidden_after_quorum_loss (id int)")
                assert response.returncode != 0, name + ' remained writable without DCS majority'
            print('PASS quorum loss stops writes on every database node', flush=True)
            for name in etcds[:2]:
                run('start', name)
            wait(primary, 'election after quorum restored')
            store = wait(open_store, 'library reconnect after quorum restoration')
            assert store.read('ha-proof')[0].get_data() == b'acknowledged before primary loss'
            # The process/game integration can be pointed at this cluster separately.
            print('PASS restored quorum retains acknowledged state', flush=True)
            subprocess.run([sys.executable, str(Path(__file__).with_name('check-mmo-runtime.py'))],
                           env=dict(os.environ, LRG_TEST_POSTGRES=connection), check=True, timeout=90)
            print('PASS gameplay recovery against the restored PostgreSQL cluster', flush=True)
    except Exception:
        for name in created:
            print('LOG ' + name + '\n' + run('logs', '--tail', '25', name, check=False).stderr)
        raise
    finally:
        for name in reversed(created):
            run('stop', '-t', '5', name, check=False)
            run('rm', '-v', name)
        run('network', 'rm', network)


if __name__ == '__main__':
    main()
