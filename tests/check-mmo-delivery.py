#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Local SMTP-over-TLS mailbox proof, recovery and operator-console integration."""
from email import policy
from email.parser import BytesParser
import json
from pathlib import Path
import queue
import re
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import gi

gi.require_version('Libregnum', '1')
from gi.repository import Libregnum as Lrg

ROOT = Path(__file__).resolve().parents[1]


class Mailbox(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def get_request(self):
        stream, address = super().get_request()
        stream.settimeout(5)
        try:
            return self.context.wrap_socket(stream, server_side=True), address
        except Exception:
            stream.close()
            raise


class SMTP(socketserver.StreamRequestHandler):
    def handle(self):
        self.wfile.write(b'220 localhost test mailbox\r\n')
        recipients = []
        while True:
            line = self.rfile.readline(4096)
            if not line:
                break
            command = line.split(b' ', 1)[0].strip().upper()
            if command in (b'EHLO', b'HELO', b'MAIL', b'RSET'):
                self.wfile.write(b'250 localhost\r\n')
            elif command == b'RCPT':
                if self.server.reject:
                    self.wfile.write(b'550 recipient unavailable\r\n')
                else:
                    recipients.append(line.decode().strip())
                    self.wfile.write(b'250 recipient accepted\r\n')
            elif command == b'DATA':
                self.wfile.write(b'354 send message\r\n')
                parts = []
                while True:
                    part = self.rfile.readline(4096)
                    if part == b'.\r\n':
                        break
                    if not part or sum(map(len, parts)) > 16384:
                        return
                    parts.append(part)
                self.server.messages.put((recipients, b''.join(parts)))
                self.wfile.write(b'250 queued\r\n')
            elif command == b'QUIT':
                self.wfile.write(b'221 goodbye\r\n')
                break
            else:
                self.wfile.write(b'500 unknown command\r\n')


def main():
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    cert = ROOT / 'tests/fixtures/mmo-test-cert.pem'
    context.load_cert_chain(cert, ROOT / 'tests/fixtures/mmo-test-key.pem')
    with tempfile.TemporaryDirectory(prefix='lrg-mailbox-') as directory, Mailbox(('127.0.0.1', 0), SMTP) as server:
        server.context = context
        server.messages = queue.Queue()
        server.reject = False
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            directory = Path(directory)
            def private(name, content):
                path = directory / name
                path.write_text(content)
                path.chmod(0o600)
                return str(path)
            database = directory / 'world.db'
            store = Lrg.MmoStore.new(str(database))
            auth = Lrg.MmoAuth.new(store)
            auth.register('alice', 'original-password')
            password = private('password', 'original-password')
            config = {'host': 'localhost', 'port': server.server_address[1],
                      'sender': 'support@example.test', 'ca_file': str(cert)}
            smtp = private('smtp.json', json.dumps(config))
            def cli(*args, success=True):
                result = subprocess.run([sys.executable, str(ROOT / 'examples/mmo-admin.py'),
                    '--database', str(database), *map(str, args)], capture_output=True, text=True, timeout=20)
                assert (result.returncode == 0) == success, result.stderr
                assert not re.search(r'\b[0-9a-f]{64}\b', result.stdout + result.stderr), 'Capability leaked to output'
                return result.stdout
            def proof():
                recipients, data = server.messages.get(timeout=5)
                assert [value.lower() for value in recipients] == ['rcpt to:<alice@example.test>'], recipients
                message = BytesParser(policy=policy.default).parsebytes(data)
                assert message['To'] == 'alice@example.test'
                return re.search(r'\b[0-9a-f]{64}\b', message.get_content()).group()
            cli('enroll-address', 'alice', 'alice@example.test', '--password-file', password, '--smtp-file', smtp)
            address_proof = private('proof', proof())
            cli('confirm-address', '--proof-file', address_proof)
            cli('confirm-address', '--proof-file', address_proof, success=False)
            cli('deliver-recovery', 'alice', '--smtp-file', smtp)
            recovery = private('reset', proof())
            replacement = private('replacement', 'replacement-password')
            cli('reset-password', '--proof-file', recovery, '--password-file', replacement)
            cli('reset-password', '--proof-file', recovery, '--password-file', replacement, success=False)
            assert auth.login('alice', 'replacement-password', int(time.time()))
            server.reject = True
            cli('enroll-address', 'alice', 'alice@example.test', '--password-file', replacement,
                '--smtp-file', smtp, success=False)
            server.reject = False
            config.pop('ca_file')
            untrusted = private('untrusted.json', json.dumps(config))
            cli('enroll-address', 'alice', 'alice@example.test', '--password-file', replacement,
                '--smtp-file', untrusted, success=False)
            cli('ban', 'alice', '--operator', 'moderator', '--reason', 'case 42', '--operation', 'case-42')
            event = cli('event', 'case-42')
            assert 'case 42' in event and 'moderator' in event
            cli('unban', 'alice', '--operator', 'moderator', '--reason', 'appeal', '--operation', 'appeal-42')
            now = int(time.time())
            cli('create-season', 'season1', '--start', now - 60, '--end', now + 3600)
            cli('match-result', 'season1', 'alice', 'bob', 'match1', '--result', 1)
            assert 'alice' in cli('standings', 'season1')
            print('PASS verified SMTP delivery, one-use recovery, provider failure, trust rejection and operator console')
        finally:
            server.shutdown()
            thread.join(timeout=5)


if __name__ == '__main__':
    main()
