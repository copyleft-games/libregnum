#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fetch a signed release into a new version directory; never overwrite live assets."""
import argparse
from pathlib import Path
import shutil
import tempfile
import urllib.parse
import urllib.request
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Libregnum


def install(base, release, key, destination):
    if urllib.parse.urlparse(base).scheme != 'https':
        raise ValueError('HTTPS release URL required')
    destination = Path(destination).absolute()
    if destination.exists():
        raise ValueError('Destination already exists; use a new version directory')
    def open_url(path):
        response = urllib.request.urlopen(base.rstrip('/') + '/' + urllib.parse.quote(path, safe='/'), timeout=30)
        if urllib.parse.urlparse(response.url).scheme != 'https':
            response.close()
            raise ValueError('Refusing redirect away from HTTPS')
        return response
    def read(path, limit):
        with open_url(path) as response:
            data = response.read(limit + 1)
            if len(data) > limit:
                raise ValueError('Remote metadata exceeds size limit')
            return GLib.Bytes.new(data)
    verified = Libregnum.mmo_content_verify(read('manifest.bin', 1048576), read('manifest.sig', 64),
                                           GLib.Bytes.new(Path(key).read_bytes()))
    version, actual_release, entries = verified.unpack()
    if actual_release != release:
        raise ValueError('Signed release does not match pinned release ID')
    staging = Path(tempfile.mkdtemp(prefix='.lrg-stage-', dir=destination.parent))
    try:
        for path, size, digest in entries:
            output = staging / path
            output.parent.mkdir(parents=True, exist_ok=True)
            received = 0
            with open_url(path) as response, output.open('xb') as target:
                while True:
                    chunk = response.read(min(65536, size - received + 1))
                    if not chunk:
                        break
                    received += len(chunk)
                    if received > size:
                        raise ValueError('Content exceeds declared size')
                    target.write(chunk)
            if received != size:
                raise ValueError('Incomplete content download')
        Libregnum.mmo_content_verify_directory(verified, str(staging))
        # The caller controls this private parent directory. Version selection is
        # separate so a failed download never switches an active client's assets.
        staging.rename(destination)
        staging = None
    finally:
        if staging is not None:
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', action='version', version='Libregnum content installer 1; AGPL-3.0-or-later')
    parser.add_argument('url', help='HTTPS directory containing manifest.bin, manifest.sig and release files')
    parser.add_argument('release', help='Expected signed release ID; pin this through trusted application policy')
    parser.add_argument('key', help='Trusted raw 32-byte Ed25519 public key')
    parser.add_argument('destination', help='New version directory under a private existing parent')
    args = parser.parse_args()
    install(args.url, args.release, args.key, args.destination)


if __name__ == '__main__':
    main()
