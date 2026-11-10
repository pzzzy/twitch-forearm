"""Host-only USB client checks. Every serial endpoint is a fake."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import device
import provision_stdin


def usb_port(name, vid=0x303A, pid=0x1001):
    return SimpleNamespace(device=name, vid=vid, pid=pid, serial_number='unimportant')


class FakeSerial:
    def __init__(self, firmware='forearm-chat/0.3.2'):
        self.firmware = firmware
        self.requests = []
        self.lines = []
        self.opened = False
        self.closed = False

    def open(self):
        self.opened = True

    def close(self):
        self.closed = True

    def write(self, payload):
        request = json.loads(payload)
        self.requests.append(request)
        reply = {'reply': request['id'], 'ok': True}
        if request['cmd'] == 'status':
            reply['firmware'] = self.firmware
        self.lines.append(b'boot log @' + json.dumps(reply).encode() + b'\n')

    def readline(self, size=-1):
        if not self.lines:
            return b''
        chunk = self.lines.pop(0)
        if size >= 0 and len(chunk) > size:
            self.lines.insert(0, chunk[size:])
            chunk = chunk[:size]
        return chunk


class ScriptedReplyWire(FakeSerial):
    """Timeout-sized byte chunks with a deterministic, virtual deadline."""
    def __init__(self, script, tick=.01):
        super().__init__()
        self.script, self.tick, self.now = script, tick, 0
        self.read_sizes = []

    def write(self, payload):
        request = json.loads(payload)
        self.requests.append(request)
        self.lines.extend(self.script(request))

    def readline(self, size=-1):
        self.read_sizes.append(size)
        self.now += self.tick
        return super().readline(size)


def wire_reply(request, **fields):
    return b'@' + json.dumps({'reply': request['id'], 'ok': True, **fields}, ensure_ascii=False).encode() + b'\n'


class FragmentedReplyTests(unittest.TestCase):
    def client(self, wire):
        # Constructor/firmware identification is tested separately above. This
        # test boundary uses only a fake wire and cannot open a serial device.
        client = object.__new__(device.Device)
        client.wire, client.sequence, client._identified = wire, 100, True
        return client

    def exchange(self, wire, name='status', timeout=2):
        with patch.object(device.time, 'monotonic', side_effect=lambda: wire.now):
            return self.client(wire).command(name, timeout=timeout)

    def test_timeout_fragments_preserve_split_marker_json_and_utf8(self):
        def chunks(request):
            reply = wire_reply(request, text='café 😀')
            # Every byte can be separated by a read timeout, including UTF-8.
            return [b'boot log @', b'', b''] + [piece for byte in reply[1:] for piece in (bytes([byte]), b'')]

        wire = ScriptedReplyWire(chunks, tick=.001)
        result = self.exchange(wire, 'pause')
        self.assertEqual(result['text'], 'café 😀')
        self.assertEqual([request['cmd'] for request in wire.requests], ['pause'])
        self.assertTrue(all(size == device.MAX_REPLY_CHUNK for size in wire.read_sizes))

    def test_large_unterminated_log_noise_recovers_at_marker_without_retry(self):
        wire = ScriptedReplyWire(lambda request: [b'x' * (device.MAX_REPLY_LINE * 3), wire_reply(request)])
        result = self.exchange(wire)
        self.assertTrue(result['ok'])
        self.assertEqual(len(wire.requests), 1)

    def test_oversized_json_line_is_drained_before_next_complete_reply(self):
        wire = ScriptedReplyWire(lambda request: [b'@{' + b'x' * (device.MAX_REPLY_LINE + 5000), b'\n', wire_reply(request)])
        self.assertTrue(self.exchange(wire)['ok'])
        self.assertEqual(len(wire.requests), 1)

    def test_malformed_stale_and_fragmented_replies_recover_without_resubmission(self):
        def chunks(request):
            stale = wire_reply({**request, 'id': request['id'] - 1})
            actual = wire_reply(request)
            return [b'noise @not-json\n', stale, b'@{broken}\n', actual[:8], b'', actual[8:]]

        wire = ScriptedReplyWire(chunks)
        self.assertTrue(self.exchange(wire, 'moderation-confirm-fixture')['ok'])
        self.assertEqual([request['cmd'] for request in wire.requests], ['moderation-confirm-fixture'])

    def test_fragmented_rejection_is_reported_once(self):
        def chunks(request):
            raw = b'@' + json.dumps({'reply': request['id'], 'ok': False}).encode() + b'\n'
            return [raw[:7], b'', raw[7:]]

        wire = ScriptedReplyWire(chunks)
        with self.assertRaisesRegex(RuntimeError, 'Device rejected command: fixture'):
            self.exchange(wire, 'fixture')
        self.assertEqual(len(wire.requests), 1)

    def test_partial_reply_obeys_deadline_and_next_command_can_resynchronize(self):
        def chunks(request):
            reply = wire_reply(request)
            if len(wire.requests) == 1:
                return [reply[:-2]]  # A complete-looking prefix is not a line.
            return [b'}\n', wire_reply({**request, 'id': request['id'] - 1}), reply]

        wire = ScriptedReplyWire(chunks, tick=.1)
        client = self.client(wire)
        with patch.object(device.time, 'monotonic', side_effect=lambda: wire.now):
            with self.assertRaisesRegex(TimeoutError, 'No complete USB reply'):
                client.command('pause', timeout=.5)
            self.assertLessEqual(wire.now, .6)
            self.assertEqual(len(wire.requests), 1)
            self.assertTrue(client.command('status', timeout=1)['ok'])
        self.assertEqual([request['cmd'] for request in wire.requests], ['pause', 'status'])


class PortSelectionTests(unittest.TestCase):
    def test_one_matching_usb_ignores_other_devices_and_owner_identity(self):
        ports = [usb_port('other', 0x1234), usb_port('selected'), usb_port('different-pid', pid=0x1002)]
        with patch.object(device.list_ports, 'comports', return_value=ports):
            self.assertEqual(device.select_port(), 'selected')

    def test_no_matching_device_requires_explicit_port_without_opening(self):
        with patch.object(device.list_ports, 'comports', return_value=[usb_port('other', 0x1234)]), \
                patch.object(device, 'NativeUsbSerial') as wire:
            with self.assertRaisesRegex(RuntimeError, '--port'):
                device.Device()
            wire.assert_not_called()

    def test_multiple_matching_devices_require_explicit_port(self):
        with patch.object(device.list_ports, 'comports', return_value=[usb_port('one'), usb_port('two')]), \
                patch.object(device, 'NativeUsbSerial') as wire:
            with self.assertRaisesRegex(RuntimeError, '--port'):
                device.Device()
            wire.assert_not_called()

    def test_explicit_port_does_not_enumerate(self):
        with patch.object(device.list_ports, 'comports', side_effect=AssertionError('must not enumerate')):
            self.assertEqual(device.select_port('COM12'), 'COM12')


class FirmwareIdentificationTests(unittest.TestCase):
    def connect(self, wire, port='fixture-port'):
        with patch.object(device, 'NativeUsbSerial', return_value=wire), patch.object(device.time, 'sleep'):
            return device.Device(port)

    def test_identifies_before_any_mutating_command_and_preserves_control_lines(self):
        wire = FakeSerial()
        client = self.connect(wire)
        self.assertEqual([x['cmd'] for x in wire.requests], ['status'])
        self.assertFalse(wire.dtr)
        self.assertFalse(wire.rts)
        client.command('pause')
        self.assertEqual([x['cmd'] for x in wire.requests], ['status', 'pause'])
        client.close()
        with self.assertRaisesRegex(RuntimeError, 'identified'):
            client.command('config', password='fake-password')
        self.assertEqual(len(wire.requests), 2)

    def test_wrong_missing_or_non_string_firmware_closes_without_config(self):
        for firmware in ('factory-demo/1', 'forearm-chatty/1', '', None, 123):
            with self.subTest(firmware=firmware):
                wire = FakeSerial(firmware)
                with self.assertRaisesRegex(RuntimeError, 'Forearm Chat firmware'):
                    self.connect(wire)
                self.assertTrue(wire.closed)
                self.assertEqual([x['cmd'] for x in wire.requests], ['status'])

    def test_nonresponding_device_is_closed(self):
        wire = FakeSerial()
        with patch.object(device.Device, '_exchange', side_effect=TimeoutError('no firmware')):
            with self.assertRaises(TimeoutError):
                self.connect(wire)
        self.assertTrue(wire.closed)

    def test_connected_port_context_is_backward_compatible_and_always_closes(self):
        client = Mock()
        with patch.object(device, 'Device', return_value=client) as constructor:
            with self.assertRaises(ValueError):
                with device.connected('COM12') as opened:
                    self.assertIs(opened, client)
                    raise ValueError('fixture failure')
            constructor.assert_called_once_with('COM12')
            client.close.assert_called_once()

    def test_native_usb_control_line_updates_do_not_call_serial_reset_methods(self):
        wire = Mock()
        with patch.object(device.serial.Serial, '_update_dtr_state') as dtr, \
                patch.object(device.serial.Serial, '_update_rts_state') as rts:
            device.NativeUsbSerial._update_dtr_state(wire)
            device.NativeUsbSerial._update_rts_state(wire)
            dtr.assert_not_called()
            rts.assert_not_called()


class ProvisioningTests(unittest.TestCase):
    config = {'ssid': 'fixture-network', 'password': 'fixture-password',
              'client_id': 'PublicClient123', 'channel': 'Stream_Test'}

    def test_setup_launcher_forwards_only_explicit_public_device_port(self):
        args = argparse.Namespace(port='COM12', listen_port=8487)
        with patch.object(device.shutil, 'which', return_value='/test/node'), \
                patch.object(device.os, 'execv') as execute:
            device.setup_server(args)
        executable, arguments = execute.call_args.args
        self.assertEqual(executable, '/test/node')
        self.assertEqual(arguments[-2:], ['--device-port', 'COM12'])
        self.assertNotIn('fixture-password', arguments)

    def test_stdin_configuration_uses_selected_port_and_no_credential_output(self):
        client = Mock()
        connection = Mock()
        connection.__enter__ = Mock(return_value=client)
        connection.__exit__ = Mock(return_value=False)
        output = io.StringIO()
        with patch.object(provision_stdin, 'connected', return_value=connection) as connect, \
                patch.object(provision_stdin.sys, 'stdin', io.StringIO(json.dumps(self.config))), \
                contextlib.redirect_stdout(output):
            self.assertEqual(provision_stdin.main(['--port', 'COM12']), 0)
        connect.assert_called_once_with('COM12')
        expected = {**self.config, 'channel': 'stream_test'}
        client.command.assert_called_once_with('config', **expected)
        for value in self.config.values():
            self.assertNotIn(value, output.getvalue())

    def test_invalid_fields_do_not_open_usb(self):
        cases = [{**self.config, 'client_id': ''}, {**self.config, 'channel': ''},
                 {**self.config, 'password': 'x' * 64}, {**self.config, 'ssid': 'é' * 17}]
        for value in cases:
            with self.subTest(value=list(value)):
                with patch.object(provision_stdin, 'connected') as connect, \
                        patch.object(provision_stdin.sys, 'stdin', io.StringIO(json.dumps(value))), \
                        contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(provision_stdin.main([]), 1)
                connect.assert_not_called()

    def test_failed_identification_does_not_echo_credentials(self):
        error = io.StringIO()
        with patch.object(provision_stdin, 'connected', side_effect=RuntimeError('not Forearm Chat')), \
                patch.object(provision_stdin.sys, 'stdin', io.StringIO(json.dumps(self.config))), \
                contextlib.redirect_stderr(error):
            self.assertEqual(provision_stdin.main([]), 1)
        for value in self.config.values():
            self.assertNotIn(value, error.getvalue())


if __name__ == '__main__':
    unittest.main()
