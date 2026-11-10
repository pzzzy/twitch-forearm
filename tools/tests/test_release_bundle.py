"""Release safety boundaries using only disposable synthetic git/build trees."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release_bundle


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class ReleaseFixture:
    version = '9.8.7-beta.1'

    def __init__(self, directory):
        self.root = Path(directory)
        self.source, self.build = self.root / 'source', self.root / 'build'
        self.source.mkdir()
        self.build.mkdir()
        for name, text in {
            'LICENSE': 'Synthetic application license.\n',
            'NOTICE': 'Synthetic notices.\n',
            'THIRD_PARTY_NOTICES.md': 'Synthetic dependency notice.\n',
            'LICENSES/example.txt': 'Synthetic third-party license.\n',
            'firmware/dependencies.lock': 'Synthetic pinned dependencies.\n',
            'docs/installing.md': 'Install these synthetic images only on a test fixture.\n',
        }.items():
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        self.git('init', '-q')
        self.commit()
        self.image_names = {
            '0x0': 'bootloader.bin', '0x8000': 'partition-table.bin',
            '0xf000': 'ota_data_initial.bin', '0x30000': 'forearm_chat.bin',
        }
        for name in self.image_names.values():
            (self.build / name).write_bytes(('Synthetic ' + name).encode())
        self.write_app()
        self.flasher = {
            'extra_esptool_args': {'chip': 'esp32s3'},
            'flash_settings': {'flash_mode': 'dio', 'flash_size': '16MB', 'flash_freq': '80m'},
            'flash_files': dict(self.image_names),
        }
        self.save_flasher()
        (self.build / 'sdkconfig').write_text(
            'CONFIG_APP_REPRODUCIBLE_BUILD=y\nCONFIG_COMPILER_HIDE_PATHS_MACROS=y\n')
        self.description = {
            'project_path': str(self.source / 'firmware'), 'project_version': self.version,
            'target': 'esp32s3', 'config_file': str(self.build / 'sdkconfig'),
        }
        self.save_description()
        self.audit_path = self.root / 'license-manifest.json'
        self.audit = {
            'complete': True, 'missing': [], 'license_files': ['LICENSES/example.txt'],
            'sdk_version': '5.3.6',
            'application_sha256': sha256((self.build / 'forearm_chat.bin').read_bytes()),
            'bootloader_sha256': sha256((self.build / 'bootloader.bin').read_bytes()),
        }
        self.save_audit()

    def git(self, *args):
        return subprocess.check_output(
            ['git', '-C', str(self.source), '-c', 'user.name=Synthetic Release Test',
             '-c', 'user.email=release-test@example.invalid', '-c', 'commit.gpgsign=false',
             '-c', 'core.hooksPath=' + str(self.root / 'disabled-hooks'), *args], text=True).strip()

    def commit(self):
        self.git('add', '.')
        self.git('commit', '-qm', 'Synthetic reviewed source')
        self.commit_id = self.git('rev-parse', 'HEAD')

    def write_app(self, version=None, project='forearm_chat', chip=9, commit=None):
        image = bytearray(512)
        image[0] = 0xe9
        struct.pack_into('<H', image, 12, chip)
        struct.pack_into('<I', image, 32, 0xabcd5432)
        for offset, value in [(48, version or self.version), (80, project), (144, 'v5.3.6')]:
            value = value.encode('ascii')
            image[offset:offset + len(value)] = value
        value = (commit or self.commit_id).encode('ascii') + b'\0'
        image[256:256 + len(value)] = value
        (self.build / 'forearm_chat.bin').write_bytes(image)

    def save_flasher(self):
        (self.build / 'flasher_args.json').write_text(json.dumps(self.flasher))

    def save_description(self):
        (self.build / 'project_description.json').write_text(json.dumps(self.description))

    def save_audit(self):
        self.audit_path.write_text(json.dumps(self.audit))

    def refresh_audit_hashes(self):
        self.audit['application_sha256'] = sha256((self.build / 'forearm_chat.bin').read_bytes())
        self.audit['bootloader_sha256'] = sha256((self.build / 'bootloader.bin').read_bytes())
        self.save_audit()

    def package(self, output='dist'):
        return release_bundle.package(self.source, self.build, self.audit_path,
                                      self.version, self.root / output)


@unittest.skipUnless(shutil.which('git'), 'release fixture requires git')
class ReleaseBundleTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='forearm-release-test-')
        self.addCleanup(directory.cleanup)
        self.fixture = ReleaseFixture(directory.name)
        self.rewrite_counter = 0

    def reject_package(self):
        with self.assertRaises((ValueError, KeyError, FileNotFoundError)):
            self.fixture.package()

    def rewrite(self, changes, rehash=True, attributes=None):
        self.rewrite_counter += 1
        original = self.fixture.package('original-' + str(self.rewrite_counter))
        with zipfile.ZipFile(original) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        changes(entries)
        if rehash:
            entries['SHA256SUMS'] = ''.join(
                sha256(data) + '  ' + name + '\n' for name, data in sorted(entries.items())
                if name != 'SHA256SUMS').encode('ascii')
        result = self.fixture.root / 'modified.zip'
        with zipfile.ZipFile(result, 'w') as archive:
            for name, data in entries.items():
                info = zipfile.ZipInfo(name)
                info.external_attr = (attributes or {}).get(name, 0o100644) << 16
                archive.writestr(info, data)
        return result

    @staticmethod
    def json_change(entries, filename, modify):
        value = json.loads(entries[filename])
        modify(value)
        entries[filename] = json.dumps(value).encode('utf-8')

    def reject_modified(self, changes, **kwargs):
        path = self.rewrite(changes, **kwargs)
        with self.assertRaises((ValueError, KeyError, zipfile.BadZipFile)):
            release_bundle.verify_bundle(path)

    def test_reviewed_images_package_deterministically_and_verify_independently(self):
        first = self.fixture.package('one')
        second = self.fixture.package('two')
        self.assertEqual(first.read_bytes(), second.read_bytes())
        manifest = release_bundle.verify_bundle(first)
        self.assertEqual(manifest['source_commit'], self.fixture.commit_id)
        self.assertEqual(manifest['version'], self.fixture.version)
        self.assertEqual(manifest['chip'], 'esp32s3')
        self.assertEqual({item['offset'] for item in manifest['images']},
                         {'0x0', '0x8000', '0xf000', '0x30000'})
        with zipfile.ZipFile(first) as archive:
            self.assertEqual(archive.read('README_FIRST.md'),
                             (self.fixture.source / 'docs/installing.md').read_bytes())
            self.assertEqual(archive.read('images/forearm_chat.bin'),
                             (self.fixture.build / 'forearm_chat.bin').read_bytes())
            for info in archive.infolist():
                self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                self.assertEqual((info.external_attr >> 16) & 0o170000, 0o100000)
            self.assertFalse(any('nvs' in name.lower() for name in archive.namelist()))
        self.assertEqual(first.with_suffix('.zip.sha256').read_text(),
                         sha256(first.read_bytes()) + '  ' + first.name + '\n')

    def test_rejects_nvs_and_unknown_flash_offsets(self):
        for offset, name in [('0x9000', 'nvs.bin'), ('0x500000', 'unexpected.bin')]:
            with self.subTest(offset=offset):
                self.fixture.flasher['flash_files'][offset] = name
                (self.fixture.build / name).write_bytes(b'Synthetic private partition')
                self.fixture.save_flasher()
                with self.assertRaises(ValueError):
                    release_bundle.collect_images(self.fixture.build)
                del self.fixture.flasher['flash_files'][offset]

    def test_rejects_missing_required_image(self):
        del self.fixture.flasher['flash_files']['0xf000']
        self.fixture.save_flasher()
        self.reject_package()

    def test_rejects_unqualified_flash_settings_and_target(self):
        for field, bad in [('flash_mode', 'qio'), ('flash_size', '8MB'), ('flash_freq', '120m')]:
            with self.subTest(field=field):
                previous = self.fixture.flasher['flash_settings'][field]
                self.fixture.flasher['flash_settings'][field] = bad
                self.fixture.save_flasher()
                with self.assertRaises(ValueError):
                    release_bundle.collect_images(self.fixture.build)
                self.fixture.flasher['flash_settings'][field] = previous
        self.fixture.flasher['extra_esptool_args']['chip'] = 'esp32'
        self.fixture.save_flasher()
        self.reject_package()

    def test_rejects_image_path_escape(self):
        outside = self.fixture.root / 'bootloader.bin'
        outside.write_bytes(b'Outside build tree')
        self.fixture.flasher['flash_files']['0x0'] = '../bootloader.bin'
        self.fixture.save_flasher()
        self.reject_package()

    def test_rejects_image_symlink_even_when_target_is_inside_build(self):
        image = self.fixture.build / 'bootloader.bin'
        image.rename(self.fixture.build / 'actual-bootloader.bin')
        image.symlink_to('actual-bootloader.bin')
        self.reject_package()

    def test_rejects_image_through_symlink_directory(self):
        (self.fixture.build / 'alias').symlink_to(self.fixture.build, target_is_directory=True)
        self.fixture.flasher['flash_files']['0x0'] = 'alias/bootloader.bin'
        self.fixture.save_flasher()
        self.reject_package()

    def test_rejects_committed_installation_readme_symlink(self):
        outside = self.fixture.root / 'private-instructions.txt'
        outside.write_text('Synthetic content outside reviewed source.\n')
        readme = self.fixture.source / 'docs/installing.md'
        readme.unlink()
        readme.symlink_to(outside)
        self.fixture.commit()
        self.fixture.write_app()
        self.fixture.audit['application_sha256'] = sha256((self.fixture.build / 'forearm_chat.bin').read_bytes())
        self.fixture.save_audit()
        self.reject_package()

    def test_rejects_dirty_reviewed_source(self):
        (self.fixture.source / 'NOTICE').write_text('Unreviewed change')
        self.reject_package()

    def test_rejects_untracked_source(self):
        (self.fixture.source / 'unreviewed.txt').write_text('Unreviewed file')
        self.reject_package()

    def test_rejects_foreign_build_tree(self):
        self.fixture.description['project_path'] = str(self.fixture.root / 'other/firmware')
        self.fixture.save_description()
        self.reject_package()

    def test_rejects_wrong_build_version(self):
        self.fixture.description['project_version'] = '1.2.3'
        self.fixture.save_description()
        self.reject_package()

    def test_rejects_wrong_build_target(self):
        self.fixture.description['target'] = 'esp32'
        self.fixture.save_description()
        self.reject_package()

    def test_rejects_nonreproducible_build(self):
        (self.fixture.build / 'sdkconfig').write_text('CONFIG_APP_REPRODUCIBLE_BUILD=y\n')
        self.reject_package()

    def test_rejects_app_wrong_version_project_or_chip(self):
        for fields in [{'version': '1.2.3'}, {'project': 'other_project'}, {'chip': 0}]:
            with self.subTest(fields=fields):
                self.fixture.write_app(**fields)
                self.fixture.refresh_audit_hashes()
                self.reject_package()

    def test_rejects_stale_embedded_source_commit(self):
        self.fixture.write_app(commit='a' * 40)
        self.fixture.refresh_audit_hashes()
        self.reject_package()

    def test_rejects_unterminated_commit_match(self):
        path = self.fixture.build / 'forearm_chat.bin'
        image = path.read_bytes().replace(self.fixture.commit_id.encode() + b'\0',
                                          self.fixture.commit_id.encode() + b'x')
        path.write_bytes(image)
        self.fixture.refresh_audit_hashes()
        self.reject_package()

    def test_rejects_personal_build_paths_in_any_image(self):
        for name in self.fixture.image_names.values():
            with self.subTest(image=name):
                path = self.fixture.build / name
                original = path.read_bytes()
                path.write_bytes(original + b'\0/Users/synthetic-owner/private/source.cpp\0')
                self.fixture.refresh_audit_hashes()
                self.reject_package()
                path.write_bytes(original)

    def test_rejects_incomplete_license_audit(self):
        self.fixture.audit['complete'] = False
        self.fixture.save_audit()
        self.reject_package()

    def test_rejects_missing_license_coverage(self):
        self.fixture.audit['missing'] = ['synthetic-uncovered-object']
        self.fixture.save_audit()
        self.reject_package()

    def test_rejects_stale_license_audit_for_either_binary(self):
        for field in ['application_sha256', 'bootloader_sha256']:
            with self.subTest(field=field):
                original = self.fixture.audit[field]
                self.fixture.audit[field] = '0' * 64
                self.fixture.save_audit()
                self.reject_package()
                self.fixture.audit[field] = original

    def test_rejects_license_path_escape(self):
        (self.fixture.root / 'outside.txt').write_text('Outside reviewed source')
        self.fixture.audit['license_files'] = ['LICENSES/../../outside.txt']
        self.fixture.save_audit()
        self.reject_package()

    def test_rejects_changed_binary_without_updated_checksums(self):
        self.reject_modified(lambda entries: entries.__setitem__('images/forearm_chat.bin',
                            entries['images/forearm_chat.bin'] + b'corruption'), rehash=False)

    def test_rejects_invalid_zip(self):
        path = self.fixture.root / 'invalid.zip'
        path.write_bytes(b'This is not a ZIP archive')
        with self.assertRaises((ValueError, zipfile.BadZipFile)):
            release_bundle.verify_bundle(path)

    def test_rejects_extra_entry_even_with_valid_checksums(self):
        self.reject_modified(lambda entries: entries.__setitem__('private-token.txt', b'Synthetic secret'))

    def test_rejects_nvs_entry_even_with_valid_checksums(self):
        self.reject_modified(lambda entries: entries.__setitem__('images/nvs.bin', b'Synthetic partition'))

    def test_rejects_zip_path_escape_even_with_valid_checksums(self):
        self.reject_modified(lambda entries: entries.__setitem__('../escaped.txt', b'Synthetic content'))

    def test_rejects_zip_symlink_metadata(self):
        self.reject_modified(lambda entries: None, attributes={'LICENSE': 0o120777})

    def test_rejects_duplicate_zip_entry(self):
        path = self.fixture.package()
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            with zipfile.ZipFile(path, 'a') as archive:
                archive.writestr('NOTICE', b'Duplicate synthetic notice')
        with self.assertRaises(ValueError):
            release_bundle.verify_bundle(path)

    def test_rejects_manifest_unknown_offset_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'manifest.json',
            lambda manifest: manifest['images'][0].__setitem__('offset', '0x9000')))

    def test_rejects_manifest_wrong_chip_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'manifest.json',
            lambda manifest: manifest.__setitem__('chip', 'esp32')))

    def test_rejects_manifest_unqualified_flash_settings_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'manifest.json',
            lambda manifest: manifest['flash_settings'].__setitem__('flash_size', '8MB')))

    def test_rejects_manifest_wrong_version_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'manifest.json',
            lambda manifest: manifest.__setitem__('version', '1.2.3')))

    def test_rejects_missing_license_text_even_after_rehash(self):
        self.reject_modified(lambda entries: entries.pop('LICENSES/example.txt'))

    def test_rejects_incomplete_bundled_audit_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'license-manifest.json',
            lambda audit: audit.__setitem__('complete', False)))

    def test_rejects_stale_bundled_audit_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'license-manifest.json',
            lambda audit: audit.__setitem__('application_sha256', '0' * 64)))

    def test_rejects_missing_required_release_files_even_after_rehash(self):
        for name in ['LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'README_FIRST.md',
                     'firmware/dependencies.lock']:
            with self.subTest(name=name):
                self.reject_modified(lambda entries: entries.pop(name))

    def test_rejects_manifest_image_byte_count_mismatch_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'manifest.json',
            lambda manifest: manifest['images'][0].__setitem__('bytes', 0)))

    def test_rejects_manifest_source_commit_mismatch_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'manifest.json',
            lambda manifest: manifest.__setitem__('source_commit', 'a' * 40)))

    def test_rejects_bundled_audit_wrong_sdk_even_after_rehash(self):
        self.reject_modified(lambda entries: self.json_change(entries, 'license-manifest.json',
            lambda audit: audit.__setitem__('sdk_version', '5.3.5')))

    def test_rejects_personal_path_in_rehashed_partition_image(self):
        def modify(entries):
            name = 'images/partition-table.bin'
            entries[name] += b'\0/home/synthetic-owner/private/project\0'
            def update_image(manifest):
                image = next(item for item in manifest['images'] if item['file'] == name)
                image.update(sha256=sha256(entries[name]), bytes=len(entries[name]))
            self.json_change(entries, 'manifest.json', update_image)
        self.reject_modified(modify)

    def test_rejects_empty_rehashed_partition_image(self):
        def modify(entries):
            name = 'images/partition-table.bin'
            entries[name] = b''
            def update_image(manifest):
                image = next(item for item in manifest['images'] if item['file'] == name)
                image.update(sha256=sha256(entries[name]), bytes=0)
            self.json_change(entries, 'manifest.json', update_image)
        self.reject_modified(modify)

    def test_failed_final_verification_leaves_no_distributable_or_partial_zip(self):
        # This audit passes its hash/coverage checks but fails final SDK binding.
        self.fixture.audit['sdk_version'] = '5.3.5'
        self.fixture.save_audit()
        self.reject_package()
        output = self.fixture.root / 'dist'
        self.assertFalse(list(output.glob('*')) if output.exists() else [])

    def test_existing_release_is_preserved_instead_of_overwritten(self):
        path = self.fixture.package()
        original = path.read_bytes()
        original_checksum = path.with_suffix('.zip.sha256').read_bytes()
        with self.assertRaises(FileExistsError):
            self.fixture.package()
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(path.with_suffix('.zip.sha256').read_bytes(), original_checksum)


if __name__ == '__main__':
    unittest.main()
