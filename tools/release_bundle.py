#!/usr/bin/env python3
"""Package reviewed build outputs; never reads device flash or publishes files."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import zipfile


IMAGES = {0x0: 'bootloader.bin', 0x8000: 'partition-table.bin',
          0xf000: 'ota_data_initial.bin', 0x30000: 'forearm_chat.bin'}
VERSION = re.compile(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?')
HOST_PATH = re.compile(rb'/(?:Users|home)/[^/\s\x00]+/')
HOST_METADATA_PARTS = {'__macosx', 'thumbs.db', 'desktop.ini'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def license_path(name):
    """License payloads use normal source paths, never hidden/host sidecars."""
    if not isinstance(name, str):
        return False
    path = PurePosixPath(name)
    return (len(path.parts) > 1 and path.parts[0] == 'LICENSES' and
            path.as_posix() == name and '\\' not in name and '..' not in path.parts and
            all(not part.startswith('.') and part.casefold() not in HOST_METADATA_PARTS and
                not any(ord(char) < 32 or ord(char) == 127 for char in part)
                for part in path.parts[1:]))


def contained(root, value):
    relative = Path(value)
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError('Artifact paths must be relative and contained')
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError('Artifact symlinks are not permitted')
    candidate = current.resolve()
    if not candidate.is_relative_to(root.resolve()) or not candidate.is_file():
        raise ValueError('Artifact is missing or outside its build/source directory')
    if (root / value).is_symlink():
        raise ValueError('Artifact symlinks are not permitted')
    return candidate


def app_description(data):
    if len(data) < 256 or data[0] != 0xe9 or struct.unpack_from('<H', data, 12)[0] != 9:
        raise ValueError('Expected an ESP32-S3 application image')
    if struct.unpack_from('<I', data, 32)[0] != 0xabcd5432:
        raise ValueError('Application description is missing')
    return {name: data[offset:offset+32].split(b'\0', 1)[0].decode('ascii')
            for name, offset in [('version', 48), ('project', 80), ('idf', 144)]}


def collect_images(build):
    args = json.loads((build / 'flasher_args.json').read_text())
    if args['extra_esptool_args']['chip'] != 'esp32s3':
        raise ValueError('Wrong target chip')
    if args['flash_settings'] != {'flash_mode': 'dio', 'flash_size': '16MB', 'flash_freq': '80m'}:
        raise ValueError('Unqualified flash settings')
    mapping = {int(offset, 0): name for offset, name in args['flash_files'].items()}
    if set(mapping) != set(IMAGES):
        raise ValueError('Only bootloader, partition table, OTA selector and application may be packaged; never NVS')
    result = {}
    for offset, destination in IMAGES.items():
        source = contained(build, mapping[offset])
        if source.name != destination:
            raise ValueError('Unexpected image name at qualified flash offset')
        data = source.read_bytes()
        if not data or len(data) > (0x400000 if offset == 0x30000 else 0x10000):
            raise ValueError('Image has an unexpected size')
        if HOST_PATH.search(data):
            raise ValueError('Image contains a personal source path; enable reproducible/path-hiding build')
        result[destination] = data
    return result


def verify_bundle(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) > 1024 or sum(item.file_size for item in archive.infolist()) > 64 * 1024 * 1024:
            raise ValueError('Package exceeds qualified size limits')
        if len(names) != len(set(names)):
            raise ValueError('Duplicate package entries')
        for name in names:
            p = PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or '\\' in name or p.as_posix() != name:
                raise ValueError('Unsafe package path')
            if name.startswith('LICENSES/') and not license_path(name):
                raise ValueError('Hidden files or host metadata are not license payloads')
            mode = archive.getinfo(name).external_attr >> 16
            if mode & 0o170000 != 0o100000:
                raise ValueError('Only regular package files are permitted')
        entries = {name: archive.read(name) for name in names}
    required = {'manifest.json', 'SHA256SUMS', 'license-manifest.json', 'README_FIRST.md',
                'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'firmware/dependencies.lock'}
    required.update('images/' + name for name in IMAGES.values())
    if not required.issubset(entries):
        raise ValueError('Required package files are missing')
    sums = {}
    for line in entries['SHA256SUMS'].decode('ascii').splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
        if not match or match[2] in sums:
            raise ValueError('Invalid checksum manifest')
        sums[match[2]] = match[1]
    if set(sums) != set(entries) - {'SHA256SUMS'}:
        raise ValueError('Package checksum coverage is incomplete')
    if any(digest(entries[name]) != value for name, value in sums.items()):
        raise ValueError('Package checksum mismatch')
    manifest = json.loads(entries['manifest.json'])
    if (manifest.get('schema_version') != 1 or manifest.get('chip') != 'esp32s3' or
            not VERSION.fullmatch(manifest.get('version', '')) or
            manifest.get('release_tag') != 'v' + manifest['version'] or
            not re.fullmatch(r'[0-9a-f]{40}', manifest.get('source_commit', '')) or
            manifest.get('flash_settings') != {'flash_mode': 'dio', 'flash_size': '16MB', 'flash_freq': '80m'}):
        raise ValueError('Unqualified release metadata')
    allowed = {'manifest.json', 'SHA256SUMS', 'license-manifest.json', 'README_FIRST.md',
               'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'firmware/dependencies.lock'}
    if any(name not in allowed and not name.startswith('LICENSES/') and
           name not in {'images/' + item for item in IMAGES.values()} for name in entries):
        raise ValueError('Unexpected package content')
    expected = {'images/' + name for name in IMAGES.values()}
    if len(manifest['images']) != len(IMAGES) or {i['file'] for i in manifest['images']} != expected:
        raise ValueError('Unqualified image list')
    for item in manifest['images']:
        offset = int(item['offset'], 0)
        if IMAGES.get(offset) != PurePosixPath(item['file']).name:
            raise ValueError('Unqualified flash offset')
        image = entries[item['file']]
        if not image or len(image) > (0x400000 if offset == 0x30000 else 0x10000):
            raise ValueError('Image has an unexpected size')
        if HOST_PATH.search(image):
            raise ValueError('Image contains a personal source path')
        if type(item.get('bytes')) is not int or item['bytes'] != len(image):
            raise ValueError('Image byte count differs from manifest')
        if digest(image) != item['sha256']:
            raise ValueError('Image manifest mismatch')
    desc = app_description(entries['images/forearm_chat.bin'])
    if desc['version'] != manifest['version'] or desc['project'] != 'forearm_chat' or desc['idf'] != manifest['sdk']:
        raise ValueError('Application version does not match release')
    if manifest['source_commit'].encode() + b'\0' not in entries['images/forearm_chat.bin']:
        raise ValueError('Embedded source commit differs from release')
    audit = json.loads(entries['license-manifest.json'])
    if (audit.get('complete') is not True or audit.get('missing') or
            audit['application_sha256'] != digest(entries['images/forearm_chat.bin']) or
            audit['bootloader_sha256'] != digest(entries['images/bootloader.bin'])):
        raise ValueError('Package license coverage does not match its images')
    if audit.get('sdk_version') != desc['idf'].removeprefix('v'):
        raise ValueError('License SDK version differs from release')
    if not audit.get('license_files') or any(name not in entries or not name.startswith('LICENSES/') for name in audit['license_files']):
        raise ValueError('Required license files are missing from the package')
    return manifest


def package(source, build, license_manifest, version, output):
    source, build = source.resolve(), build.resolve()
    if not VERSION.fullmatch(version):
        raise ValueError('Invalid release version')
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()
    commit = git('rev-parse', 'HEAD')
    if git('status', '--porcelain', '--untracked-files=all'):
        raise ValueError('Commit the reviewed source before packaging; working tree must be clean')
    description = json.loads((build / 'project_description.json').read_text())
    if Path(description['project_path']).resolve() != source / 'firmware':
        raise ValueError('Build was not made from this reviewed source tree')
    if description['project_version'] != version or description['target'] != 'esp32s3':
        raise ValueError('Build version or target differs from release')
    config = Path(description['config_file']).read_text()
    for required in ['CONFIG_APP_REPRODUCIBLE_BUILD=y', 'CONFIG_COMPILER_HIDE_PATHS_MACROS=y']:
        if required not in config.splitlines():
            raise ValueError('Reproducible build and path hiding are required')
    images = collect_images(build)
    desc = app_description(images['forearm_chat.bin'])
    if desc['version'] != version or desc['project'] != 'forearm_chat':
        raise ValueError('Compiled application metadata differs from release')
    if commit.encode() + b'\0' not in images['forearm_chat.bin']:
        raise ValueError('Reconfigure/rebuild after committing: embedded source commit does not match')
    licenses = json.loads(license_manifest.read_text())
    if licenses.get('complete') is not True or licenses.get('missing'):
        raise ValueError('Binary license audit is incomplete')
    if (licenses['application_sha256'] != digest(images['forearm_chat.bin']) or
            licenses['bootloader_sha256'] != digest(images['bootloader.bin'])):
        raise ValueError('License audit does not cover these exact images')
    # The clean index matches HEAD, but ignored host files can still exist on
    # disk. Enumerate the reviewed index, never the working-directory tree.
    tracked_licenses = {}
    for record in git('ls-files', '--stage', '-z', '--', 'LICENSES/').split('\0'):
        if not record:
            continue
        metadata, name = record.split('\t', 1)
        mode, _object, stage = metadata.split()
        if mode not in ('100644', '100755') or stage != '0':
            raise ValueError('Only committed regular license files may be packaged')
        if not license_path(name):
            raise ValueError('Hidden files or host metadata are not license payloads')
        tracked_licenses[name] = contained(source, name)
    for name in licenses['license_files']:
        if not license_path(name) or name not in tracked_licenses:
            raise ValueError('Required license file must be committed inside LICENSES')
    entries = {'images/' + name: data for name, data in images.items()}
    for name in ['LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'firmware/dependencies.lock']:
        entries[name] = contained(source, name).read_bytes()
    for name, path in sorted(tracked_licenses.items()):
        entries[name] = path.read_bytes()
    entries['license-manifest.json'] = (json.dumps(licenses, indent=2, sort_keys=True) + '\n').encode()
    entries['README_FIRST.md'] = contained(source, 'docs/installing.md').read_bytes()
    manifest = {'schema_version': 1, 'version': version, 'release_tag': 'v' + version,
                'source_commit': commit, 'chip': 'esp32s3', 'sdk': desc['idf'],
                'hardware': 'JC3248W535, 16 MB flash / 8 MB PSRAM, portrait',
                'hardware_purchase_url': 'https://www.aliexpress.us/item/3256807380017698.html',
                'scope': 'hardware beta; battery, charging, thermals and forearm mounting unqualified',
                'flash_settings': {'flash_mode': 'dio', 'flash_size': '16MB', 'flash_freq': '80m'},
                'images': [{'offset': hex(offset), 'file': 'images/' + name,
                            'sha256': digest(images[name]), 'bytes': len(images[name])}
                           for offset, name in IMAGES.items()]}
    entries['manifest.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    entries['SHA256SUMS'] = ''.join(digest(data) + '  ' + name + '\n'
                                   for name, data in sorted(entries.items())).encode()
    output.mkdir(parents=True, exist_ok=True)
    destination = output / ('forearm-chat-' + version + '-esp32s3.zip')
    checksum = destination.with_suffix('.zip.sha256')
    if destination.exists() or checksum.exists():
        raise FileExistsError(destination)
    staging = destination.with_suffix('.zip.partial')
    try:
        with zipfile.ZipFile(staging, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(entries.items()):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, data)
        verify_bundle(staging)
        staging.rename(destination)
    finally:
        staging.unlink(missing_ok=True)
    checksum.write_text(digest(destination.read_bytes()) + '  ' + destination.name + '\n')
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--license-manifest', type=Path)
    parser.add_argument('--version')
    parser.add_argument('--output', type=Path, default=Path('.local/dist'))
    parser.add_argument('--verify', type=Path)
    args = parser.parse_args()
    if args.verify:
        print(json.dumps(verify_bundle(args.verify), indent=2)); return
    if not all([args.build_dir, args.license_manifest, args.version]):
        parser.error('--build-dir, --license-manifest and --version are required')
    print(package(args.source_root, args.build_dir, args.license_manifest, args.version, args.output))


if __name__ == '__main__':
    main()
