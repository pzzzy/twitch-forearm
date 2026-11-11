"""Synthetic fixture contracts only; these checks never connect to hardware."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hardware_check


class HardwareFixtureTests(unittest.TestCase):
    def test_maximum_text_and_sender_fixtures_fit_usb_and_utf8_bounds(self):
        fixtures = dict(hardware_check.text_fixtures())
        self.assertEqual(len(fixtures['max-ascii']['text']), 500)
        self.assertEqual(len(fixtures['max-emoji']['text']), 500)
        self.assertEqual(len(fixtures['max-emoji']['text'].encode()), 2000)
        self.assertEqual(len(fixtures['max-display-name']['name']), 25)
        self.assertEqual(len(fixtures['max-display-name']['name'].encode()), 100)
        for fixture in fixtures.values():
            self.assertEqual(fixture['user_login'], 'anonymousviewer_name_')
            self.assertLessEqual(len(fixture['user_login']), 25)
            self.assertTrue(fixture['user_login'].isascii())
            wire = json.dumps({'cmd': 'message', 'id': 1, **fixture}, ensure_ascii=False).encode() + b'\n'
            self.assertLessEqual(len(wire), 3000)
            self.assertLessEqual(len(fixture['text'].encode()), 2048)

    def test_only_the_positioned_occurrence_matches_metadata(self):
        fixture = hardware_check.positioned_emote_fixture()
        self.assertEqual(len(fixture['emotes']), 1)
        emote = fixture['emotes'][0]
        encoded = fixture['text'].encode()
        start, length = emote['byte_offset'], emote['byte_length']
        self.assertTrue(emote['position_valid'])
        self.assertEqual(encoded[start:start+length].decode(), emote['text'])
        self.assertEqual(encoded.count(b'Kappa'), 3)
        self.assertLess(encoded.find(b'Kappa'), start)
        self.assertGreater(encoded.rfind(b'Kappa'), start)
        # The non-ASCII prefix makes a codepoint index incorrect here.
        self.assertNotEqual(start, len(encoded[:start].decode()))

    def test_over_capacity_fixture_has_ten_distinct_exact_occurrences(self):
        fixture = hardware_check.capacity_emote_fixture()
        self.assertEqual(len(fixture['emotes']), 10)
        encoded = fixture['text'].encode()
        positions = []
        for emote in fixture['emotes']:
            offset, length = emote['byte_offset'], emote['byte_length']
            self.assertEqual(encoded[offset:offset+length], b'Kappa')
            self.assertTrue(emote['position_valid'])
            positions.append(offset)
        self.assertEqual(len(set(positions)), 10)
        self.assertEqual(len(fixture['emotes'][:8]), 8)
        self.assertLessEqual(len(json.dumps({'cmd': 'message', 'id': 1, **fixture}).encode()), 3000)

    def test_unsupported_name_reference_changes_only_display_name(self):
        unsupported, reference = hardware_check.fallback_fixtures()
        self.assertEqual(len(unsupported['name']), 25)
        self.assertGreater(len(unsupported['name'].encode()), 25)
        self.assertEqual(reference['name'], unsupported['user_login'])
        self.assertEqual({k: v for k, v in unsupported.items() if k != 'name'},
                         {k: v for k, v in reference.items() if k != 'name'})

    def test_pixel_comparison_detects_one_pixel_and_geometry_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / 'first.png', Path(directory) / 'second.png'
            image = Image.new('RGB', (3, 2), (12, 34, 56))
            image.save(first); image.save(second)
            self.assertTrue(hardware_check.images_equal(first, second))
            image.putpixel((2, 1), (12, 34, 57)); image.save(second)
            self.assertFalse(hardware_check.images_equal(first, second))
            Image.new('RGB', (2, 3)).save(second)
            self.assertFalse(hardware_check.images_equal(first, second))

    def test_fixture_failure_still_disables_injection_and_restores_original_mode(self):
        class Fake:
            def __init__(self):
                self.calls, self.fixture = [], False

            def command(self, name, **fields):
                self.calls.append((name, fields))
                if name == 'status':
                    return {'board': {'touch_present': True, 'flush_errors': 0},
                            'ui': {'paused': True, 'fixture_mode': self.fixture}}
                if name == 'fixture_mode':
                    self.fixture = bool(fields['enabled'])
                if name == 'burst':
                    raise RuntimeError('SYNTHETIC failure')
                return {'ok': True}

        fake = Fake()
        with self.assertRaisesRegex(RuntimeError, 'SYNTHETIC failure'):
            hardware_check.run_checks(fake, Path('unused-report.json'), {'samples': [], 'checks': {}})
        self.assertEqual(fake.calls[-3:], [
            ('touch', {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0}),
            ('fixture_mode', {'enabled': 0}), ('pause', {})])
        self.assertFalse(fake.fixture)
        self.assertLessEqual({name for name, _ in fake.calls}, {'status', 'fixture_mode', 'live', 'burst', 'touch', 'pause'})


if __name__ == '__main__':
    unittest.main()
