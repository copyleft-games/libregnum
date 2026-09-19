#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
import functools
import hashlib
import http.server
import importlib.util
import os
import ssl
import threading
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Libregnum


class Content(unittest.TestCase):
    def test_signature_paths_and_files(self):
        with tempfile.TemporaryDirectory(prefix='lrg-content-') as directory:
            directory = Path(directory)
            private = directory / 'private.pem'
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'ED25519', '-out', str(private)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            der = subprocess.check_output(['openssl', 'pkey', '-in', str(private), '-pubout', '-outform', 'DER'])
            self.assertEqual(der[:12], bytes.fromhex('302a300506032b6570032100'))
            key = GLib.Bytes.new(der[-32:])
            data = b'game asset'
            (directory / 'asset').write_bytes(data)
            def signed(path):
                variant = GLib.Variant('(tsa(stay))', (1, 'release1', [(path, len(data), list(hashlib.sha256(data).digest()))]))
                if sys.byteorder == 'big':
                    variant = variant.byteswap()
                manifest = variant.get_data_as_bytes()
                (directory / 'manifest').write_bytes(manifest.get_data())
                subprocess.run(['openssl', 'pkeyutl', '-sign', '-rawin', '-inkey', str(private),
                                '-in', str(directory / 'manifest'), '-out', str(directory / 'signature')], check=True)
                return manifest, GLib.Bytes.new((directory / 'signature').read_bytes())
            manifest, signature = signed('asset')
            verified = Libregnum.mmo_content_verify(manifest, signature, key)
            self.assertTrue(Libregnum.mmo_content_verify_directory(verified, str(directory)))
            # Serve the signed release over real TLS and exercise staged installation.
            (directory / 'manifest.bin').write_bytes(manifest.get_data())
            (directory / 'manifest.sig').write_bytes(signature.get_data())
            (directory / 'public.raw').write_bytes(der[-32:])
            fixture = Path(__file__).resolve().parent / 'fixtures'
            class QuietHandler(http.server.SimpleHTTPRequestHandler):
                def log_message(self, *args):
                    pass
            server = http.server.HTTPServer(('127.0.0.1', 0), functools.partial(QuietHandler, directory=str(directory)))
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(str(fixture / 'mmo-test-cert.pem'), str(fixture / 'mmo-test-key.pem'))
            server.socket = context.wrap_socket(server.socket, server_side=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            original_ca = os.environ.get('SSL_CERT_FILE')
            os.environ['SSL_CERT_FILE'] = str(fixture / 'mmo-test-cert.pem')
            sys.dont_write_bytecode = True
            spec = importlib.util.spec_from_file_location('installer', Path(__file__).resolve().parents[1] / 'examples/mmo-content.py')
            installer = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(installer)
            try:
                base = f'https://localhost:{server.server_port}'
                installer.install(base, 'release1', directory / 'public.raw', directory / 'installed')
                self.assertEqual((directory / 'installed/asset').read_bytes(), data)
                with self.assertRaises(ValueError):
                    installer.install(base, 'wrong-release', directory / 'public.raw', directory / 'wrong')
                with self.assertRaises(ValueError):
                    installer.install(base, 'release1', directory / 'public.raw', directory / 'installed')
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=5)
                if original_ca is None:
                    os.environ.pop('SSL_CERT_FILE', None)
                else:
                    os.environ['SSL_CERT_FILE'] = original_ca
            with self.assertRaises(GLib.Error):
                Libregnum.mmo_content_verify(manifest, GLib.Bytes.new(bytes(64)), key)
            (directory / 'asset').write_bytes(b'bad')
            with self.assertRaises(GLib.Error):
                Libregnum.mmo_content_verify_directory(verified, str(directory))
            (directory / 'asset').unlink()
            (directory / 'asset').symlink_to(private)
            with self.assertRaises(GLib.Error):
                Libregnum.mmo_content_verify_directory(verified, str(directory))
            manifest, signature = signed('../escape')
            with self.assertRaises(GLib.Error):
                Libregnum.mmo_content_verify(manifest, signature, key)


if __name__ == '__main__':
    unittest.main()
