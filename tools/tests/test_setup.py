"""Run Node's loopback-handler security tests with a mocked provision child."""
from pathlib import Path
import shutil
import subprocess
import unittest

NODE = shutil.which('node')


@unittest.skipUnless(NODE, 'Node.js is required for loopback setup tests')
class SetupServerTests(unittest.TestCase):
    def test_loopback_security_and_private_stdin_provisioning(self):
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [NODE, '--test', str(Path(__file__).with_name('setup.test.mjs'))],
            cwd=root, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
