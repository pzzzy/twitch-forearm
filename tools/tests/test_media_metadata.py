"""Pinned public emoji filename metadata; no artwork download or device access."""
import hashlib
from pathlib import Path
import re
import unittest


class MediaMetadataTests(unittest.TestCase):
    def test_all_4009_pinned_twemoji_names_and_canonical_mapping_match_primary_tree(self):
        source = (Path(__file__).resolve().parents[2] / 'core/include/forearm/twemoji_keys.h').read_text()
        pairs = re.findall(r'\{"([a-f0-9-]+)", "([a-f0-9-]+)"\}', source)
        self.assertEqual(len(pairs), 4009)
        self.assertEqual(pairs, sorted(pairs))
        self.assertEqual(len({canonical for canonical, _ in pairs}), 4009)
        names = sorted(asset for _, asset in pairs)
        self.assertEqual(hashlib.sha256(('\n'.join(names) + '\n').encode()).hexdigest(),
                         '3b067a7cc0c6a309d31d7bb9175c9d7fd994fa9022cce9c7ac6f0615c6b1388e')
        for canonical, asset in pairs:
            self.assertEqual(canonical, '-'.join(part for part in asset.split('-') if part != 'fe0f'))
            self.assertLessEqual(len('emoji:' + asset), 128)
        self.assertIn('388f518b756404152f128a3539f5e7f9a6f1ebd6', source)


if __name__ == '__main__':
    unittest.main()
