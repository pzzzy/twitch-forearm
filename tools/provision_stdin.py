#!/usr/bin/env python3
"""Receive setup fields via stdin and deliver them to an identified USB device."""
import argparse
import json
import re
import sys

from device import connected


def read_configuration(stream):
    text = stream.read(4097)
    if len(text.encode('utf-8')) > 4096:
        raise ValueError('Configuration exceeds limit')
    value = json.loads(text)
    keys = ('ssid', 'password', 'client_id', 'channel')
    if not isinstance(value, dict) or any(not isinstance(value.get(k), str) for k in keys):
        raise ValueError('Invalid configuration fields')
    config = {k: value[k] for k in keys}
    config['channel'] = config['channel'].lower()
    if (not config['ssid'] or len(config['ssid'].encode('utf-8')) > 32 or
            len(config['password'].encode('utf-8')) > 63 or
            not re.fullmatch(r'[A-Za-z0-9]{1,64}', config['client_id']) or
            not re.fullmatch(r'[a-z0-9_]{1,25}', config['channel'])):
        raise ValueError('Invalid configuration fields')
    return config


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Explicit USB device port; no credentials in arguments')
    args = parser.parse_args(argv)
    try:
        value = read_configuration(sys.stdin)
        with connected(args.port) as device:
            device.command('config', **value)
        print('Configuration accepted by display')
        return 0
    except Exception:
        print('Display unavailable or configuration rejected. Retry after the USB test finishes.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
