#!/usr/bin/env python3
"""Local USB diagnostics/provisioning. Never prints passwords or OAuth tokens."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import secrets
import shutil
import sys
import time

import serial
from serial.tools import list_ports

USB_VENDOR_ID = 0x303A
USB_PRODUCT_ID = 0x1001
MAX_REPLY_CHUNK = 4096
MAX_REPLY_LINE = 65536


def select_port(port=None):
    """Select one native ESP32 USB endpoint, or honor an explicit public port."""
    if port:
        return port
    ports = [p.device for p in list_ports.comports()
             if p.vid == USB_VENDOR_ID and p.pid == USB_PRODUCT_ID]
    if len(ports) != 1:
        raise RuntimeError('Expected exactly one USB 303a:1001 device; use --port to select the Forearm Chat board')
    return ports[0]


class NativeUsbSerial(serial.Serial):
    # Do not toggle ESP32-S3's USB Serial/JTAG reset control lines when opening
    # a diagnostic connection. Explicit firmware flashing still uses esptool.
    def _update_dtr_state(self):
        pass
    def _update_rts_state(self):
        pass


class Device:
    def __init__(self, port=None):
        port = select_port(port)
        self._identified = False
        self.wire = NativeUsbSerial()
        try:
            self.wire.port = port
            self.wire.baudrate = 115200
            self.wire.timeout = .15
            self.wire.write_timeout = 3
            self.wire.dtr = False
            self.wire.rts = False
            self.wire.open()
            self.sequence = secrets.randbelow(100000)
            # Also allow a just-flashed application time to finish initialization.
            time.sleep(1.6)
            status = self._exchange('status', timeout=8)
            firmware = status.get('firmware')
            if not isinstance(firmware, str) or not firmware.startswith('forearm-chat/'):
                raise RuntimeError('Selected serial device is not running Forearm Chat firmware')
            self._identified = True
        except Exception:
            self.wire.close()
            raise

    def close(self):
        self._identified = False
        self.wire.close()

    def command(self, command, timeout=8, **fields):
        if not self._identified:
            raise RuntimeError('Forearm Chat firmware must be identified before sending commands')
        return self._exchange(command, timeout=timeout, **fields)

    def _exchange(self, command, timeout=8, **fields):
        self.sequence += 1
        request = {'cmd': command, 'id': self.sequence, **fields}
        payload = json.dumps(request, separators=(',', ':'), ensure_ascii=False).encode() + b'\n'
        if len(payload) > 3000:
            raise ValueError('USB command exceeds device limit')
        self.wire.write(payload)
        deadline = time.monotonic() + timeout
        pending = bytearray()
        discarding = False
        while time.monotonic() < deadline:
            # A serial read timeout can return a partial line, including a
            # split UTF-8 codepoint or response marker. Retain it until LF.
            # Bound each transport read as well as the accumulated candidate.
            raw = self.wire.readline(MAX_REPLY_CHUNK)
            if time.monotonic() >= deadline:
                break
            pending.extend(raw)
            while b'\n' in pending:
                line, _, rest = pending.partition(b'\n')
                pending = bytearray(rest)
                if discarding:
                    discarding = False
                    continue
                marker = line.find(b'@{')
                if marker < 0 or len(line) - marker > MAX_REPLY_LINE:
                    continue
                try:
                    response = json.loads(line[marker + 1:])
                except (ValueError, UnicodeDecodeError):
                    continue
                if not isinstance(response, dict) or response.get('reply') != self.sequence:
                    continue
                if not response.get('ok'):
                    raise RuntimeError('Device rejected command: ' + command)
                return response
            if discarding:
                pending.clear()
            else:
                # Logs may precede @ on the same line. Noise before the marker
                # does not need storage; preserve a trailing @ across reads.
                marker = pending.find(b'@{')
                if marker >= 0:
                    del pending[:marker]
                elif pending.endswith(b'@'):
                    pending[:] = b'@'
                else:
                    pending.clear()
                if len(pending) > MAX_REPLY_LINE:
                    pending.clear()
                    discarding = True  # Drain oversized JSON through its LF.
        raise TimeoutError('No complete USB reply to ' + command)

    def screen(self, destination):
        from PIL import Image
        destination = Path(destination)
        if destination.exists():
            raise FileExistsError(destination)
        capture = self.command('capture')
        width, height = capture['width'], capture['height']
        rgb = bytearray()
        for y in range(height):
            for attempt in range(3):
                try:
                    row = self.command('row', timeout=2, y=y, capture=capture['capture'])
                except TimeoutError:
                    if attempt == 2:
                        raise
                    continue
                data = bytes.fromhex(row['data'])
                checksum = 2166136261
                for byte in data:
                    checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
                if len(data) == width*2 and checksum == row['checksum'] and row['y'] == y:
                    break
            else:
                raise RuntimeError('Framebuffer row checksum failed')
            for x in range(width):
                color = data[x*2] | data[x*2+1] << 8
                r, g, b = (color >> 11) & 31, (color >> 5) & 63, color & 31
                rgb.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        destination.parent.mkdir(parents=True, exist_ok=True)
        Image.frombytes('RGB', (width, height), bytes(rgb)).save(destination)
        return str(destination.resolve())

    def gesture(self, points, step=.045):
        try:
            for x, y in points:
                self.command('touch', x=x, y=y, pressed=1)
                time.sleep(step)
            x, y = points[-1]
            self.command('touch', x=x, y=y, pressed=0)
            time.sleep(.08)
        finally:
            self.command('touch', x=0, y=0, pressed=0, enabled=0)


@contextlib.contextmanager
def connected(port=None):
    d = Device(port)
    try:
        yield d
    finally:
        d.close()


def setup_server(args):
    # Use the tested Node loopback server; provisioning still uses this Python
    # interpreter and the same native USB client through a private stdin pipe.
    node = shutil.which('node')
    if not node:
        raise RuntimeError('Node.js is required for the local setup form')
    script = Path(__file__).resolve().with_name('setup.mjs')
    argv = [node, str(script), '--port', str(args.listen_port), '--python', sys.executable]
    if args.port:
        argv.extend(['--device-port', args.port])
    os.execv(node, argv)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port')
    sub = p.add_subparsers(dest='action', required=True)
    for name in ('status','pause','live','login','logout'):
        sub.add_parser(name)
    c = sub.add_parser('command'); c.add_argument('json')
    c = sub.add_parser('screen'); c.add_argument('destination')
    c = sub.add_parser('burst'); c.add_argument('count', type=int)
    c = sub.add_parser('scroll'); c.add_argument('pixels', type=int)
    c = sub.add_parser('setup'); c.add_argument('--listen-port', type=int, default=8487)
    args = p.parse_args()
    if args.action == 'setup':
        setup_server(args); return
    with connected(args.port) as d:
        if args.action == 'screen':
            print(d.screen(args.destination))
        elif args.action == 'command':
            fields = json.loads(args.json); command = fields.pop('cmd')
            if command == 'config':
                raise ValueError('Use the local setup form; do not put passwords in shell arguments')
            print(json.dumps(d.command(command, **fields), indent=2))
        elif args.action == 'burst': print(json.dumps(d.command('burst', count=args.count)))
        elif args.action == 'scroll': print(json.dumps(d.command('scroll', pixels=args.pixels)))
        else: print(json.dumps(d.command(args.action), indent=2))

if __name__ == '__main__':
    main()
