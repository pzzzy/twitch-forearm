#!/usr/bin/env python3
"""Check the Git source candidate without opening ignored private development data.

This complements secret scanning and manual review; it cannot recognize every
credential or determine whether a screenshot contains private chat.
"""
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PURCHASE_URL = 'https://www.aliexpress.us/item/3256807380017698.html'
REQUIRED = ('LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'CONTRIBUTING.md',
            'SECURITY.md', 'docs/releasing.md', '.github/workflows/ci.yml')
PRIVATE_PARTS = {'.local', '.venv', '.env', '__pycache__', 'node_modules',
                 'managed_components', '.font-venv', '.git'}
PRIVATE_SUFFIXES = {'.bin', '.elf', '.map', '.log', '.pyc', '.pcap', '.pcapng'}
PERSONAL_PATH = re.compile(r'/(?:Users|home)/[^/\s]+/')
DEVICE_ID = re.compile(r'(?i)(?<![0-9a-f])(?:[0-9a-f]{2}:){5}[0-9a-f]{2}(?![0-9a-f])')


def candidates():
    result = subprocess.run(['git', 'ls-files', '--cached', '--others',
                             '--exclude-standard', '-z'], cwd=ROOT,
                            check=True, stdout=subprocess.PIPE)
    return sorted(set(result.stdout.decode().rstrip('\0').split('\0')) - {''})


def main():
    names = candidates()
    errors = []
    for required in REQUIRED:
        if required not in names:
            errors.append(required + ': missing public project file')
    for name in names:
        relative = PurePosixPath(name)
        source = ROOT / name
        if (relative.is_absolute() or '..' in relative.parts or
                PRIVATE_PARTS.intersection(relative.parts) or
                relative.name.startswith('.env') or
                relative.name in ('sdkconfig', 'sdkconfig.old') or
                relative.suffix.lower() in PRIVATE_SUFFIXES):
            errors.append(name + ': private/generated path must stay outside Git')
            continue
        if source.is_symlink():
            errors.append(name + ': review and replace symlink before publication')
            continue
        if not source.is_file():
            errors.append(name + ': missing candidate file; stage its removal')
            continue
        try:
            contents = source.read_text(encoding='utf-8')
        except UnicodeError:
            continue
        personal_paths = PERSONAL_PATH.findall(contents)
        if name == 'tools/tests/test_release_bundle.py':
            # Exact synthetic adversarial fixtures, not a general tests exemption.
            personal_paths = [item for item in personal_paths if item.split('/')[2] != 'synthetic-owner']
        if personal_paths or DEVICE_ID.search(contents):
            errors.append(name + ': personal host path or device identity needs review')
    for name in ('README.md', 'docs/hardware-reference.md', 'hardware/board.json'):
        if PURCHASE_URL not in (ROOT / name).read_text():
            errors.append(name + ': exact hardware purchase reference is missing')
    board = json.loads((ROOT / 'hardware/board.json').read_text())
    if not board['physical_observations']['new_firmware_uploaded']:
        errors.append('hardware/board.json: stale preparation-only state')
    if errors:
        # Report paths and reasons, never the matched private text.
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('Public-tree checks passed for %d source files; secret scan and manual review remain separate.' % len(names))
    return 0


if __name__ == '__main__':
    sys.exit(main())
