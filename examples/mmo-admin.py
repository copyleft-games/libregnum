#!/usr/bin/python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Local operator console and verified-mailbox recovery delivery (AGPL-3.0-or-later)."""
import argparse
from email.message import EmailMessage
import json
from pathlib import Path
import smtplib
import ssl
import sys
import time
import gi

gi.require_version('Libregnum', '1')
from gi.repository import GLib, Libregnum as Lrg


def secret(path):
    value = Path(path)
    if value.stat().st_mode & 0o077:
        raise ValueError('Secret files must be private to their owner')
    return value.read_text().strip()


def deliver(config_path, address, purpose, capability):
    """Only implicit TLS with normal CA/hostname validation; no plaintext fallback."""
    config = json.loads(secret(config_path))
    message = EmailMessage()
    message['From'] = config['sender']
    message['To'] = address
    message['Subject'] = 'Libregnum ' + purpose
    message.set_content('Your one-use ' + purpose + ' code expires in fifteen minutes.\n\n' + capability + '\n')
    context = ssl.create_default_context(cafile=config.get('ca_file'))
    with smtplib.SMTP_SSL(config['host'], int(config.get('port', 465)),
                         context=context, timeout=10) as smtp:
        if config.get('username'):
            smtp.login(config['username'], config['password'])
        refused = smtp.send_message(message)
        if refused:
            raise ValueError('Recovery provider refused recipient')


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog='Example: mmo-admin.py --database world.db '
        'ban alice --operator moderator --reason "abuse report 42" --operation case-42. '
        'OS/database access grants authority; this is not a public administration endpoint.')
    parser.add_argument('--version', action='version', version='libregnum MMO console 1 (AGPL-3.0-or-later)')
    db = parser.add_mutually_exclusive_group(required=True)
    db.add_argument('--database')
    db.add_argument('--postgres-file')
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('ban', 'unban'):
        sub = commands.add_parser(name)
        sub.add_argument('account')
        sub.add_argument('--operator', required=True)
        sub.add_argument('--reason', required=True)
        sub.add_argument('--operation', required=True)
    event = commands.add_parser('event')
    event.add_argument('operation')
    enrollment = commands.add_parser('enroll-address')
    enrollment.add_argument('account')
    enrollment.add_argument('address')
    enrollment.add_argument('--password-file', required=True)
    enrollment.add_argument('--totp-file')
    enrollment.add_argument('--smtp-file', required=True)
    confirmation = commands.add_parser('confirm-address')
    confirmation.add_argument('--proof-file', required=True)
    delivery = commands.add_parser('deliver-recovery')
    delivery.add_argument('account')
    delivery.add_argument('--smtp-file', required=True)
    reset = commands.add_parser('reset-password')
    reset.add_argument('--proof-file', required=True)
    reset.add_argument('--password-file', required=True)
    season = commands.add_parser('create-season')
    season.add_argument('season')
    season.add_argument('--start', type=int, required=True)
    season.add_argument('--end', type=int, required=True)
    season.add_argument('--capacity', type=int, default=1000)
    result = commands.add_parser('match-result')
    for name in ('season', 'first', 'second', 'match'):
        result.add_argument(name)
    result.add_argument('--result', type=int, choices=(0, 1, 2), required=True)
    standings = commands.add_parser('standings')
    standings.add_argument('season')
    args = parser.parse_args()
    store = Lrg.MmoStore.new_postgres(secret(args.postgres_file)) if args.postgres_file else Lrg.MmoStore.new(args.database)
    auth = Lrg.MmoAuth.new(store)
    now = int(time.time())
    if args.command in ('ban', 'unban'):
        auth.moderate(args.operator, args.account, args.command == 'ban', args.reason, args.operation)
    elif args.command == 'event':
        data, revision = store.read('moderation/' + args.operation)
        value = GLib.Variant.new_from_bytes(GLib.VariantType.new('(ssbsx)'), data, False)
        if not value.is_normal_form():
            raise ValueError('Invalid moderation event')
        if sys.byteorder == 'big':
            value = value.byteswap()
        print(json.dumps({'revision': revision, 'event': value.unpack()}))
    elif args.command == 'enroll-address':
        password = secret(args.password_file)
        token = auth.login_totp(args.account, password, int(secret(args.totp_file)), now) if args.totp_file else auth.login(args.account, password, now)
        # Enrollment login is only reauthentication, not a new session to retain.
        auth.revoke(token)
        proof = auth.begin_address(args.account, args.address, now)
        deliver(args.smtp_file, args.address, 'mailbox verification', proof)
    elif args.command == 'confirm-address':
        auth.confirm_address(secret(args.proof_file), now)
    elif args.command == 'deliver-recovery':
        address, proof = auth.prepare_recovery(args.account, now).unpack()
        deliver(args.smtp_file, address, 'password recovery', proof)
    elif args.command == 'reset-password':
        auth.recover(secret(args.proof_file), secret(args.password_file), now)
    else:
        service = Lrg.MmoSeason.new(store)
        if args.command == 'create-season':
            service.create(args.season, args.start, args.end, args.capacity)
        elif args.command == 'match-result':
            service.record(args.season, args.first, args.second, args.result, args.match, now)
        else:
            print(json.dumps(service.standings(args.season).unpack()))
    print('ok')


if __name__ == '__main__':
    try:
        main()
    except (GLib.Error, ValueError, OSError, smtplib.SMTPException):
        # Avoid capability, password, provider or database credential diagnostics.
        print('Operation failed; check private configuration and authorization.', file=sys.stderr)
        sys.exit(1)
