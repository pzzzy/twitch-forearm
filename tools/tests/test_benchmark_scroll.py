"""Fake-device benchmark tests; never opens a USB/serial endpoint."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import io
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import benchmark_scroll
from device import Device


class FakeClock:
    def __init__(self):
        self.value, self.sleeps = 1.0, []

    def now(self):
        return self.value

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.value += seconds


class FakeDevice:
    gesture = Device.gesture  # Exercise the actual release and 80 ms idle path.

    def __init__(self, clock):
        self.clock, self.calls = clock, []
        self.frames, self.compositions = 900, 1200
        self.draws = self.queued = self.pending_polls = self.pending_seen = 0
        self.fixture = self.paused = self.held = False
        self.sprite = self.author = self.culling = False
        self.staged = True
        self.load_until, self.generated, self.length = 0, 0, 0
        self.moderation_requests = self.moderation_submitted = self.moderation_unknown = self.touch_errors = 0
        self.fail_when = None
        self.never_drain = False

    def command(self, name, **fields):
        self.calls.append((name, dict(fields)))
        self.clock.value += .001
        if self.fail_when is not None:
            self.fail_when(name, fields)
        if name == 'fixture_mode':
            self.fixture = bool(fields['enabled'])
        elif name in ('pause', 'live'):
            self.paused = name == 'pause'
        elif name == 'sprite_cache':
            self.sprite = bool(fields['enabled'])
        elif name == 'author_cache':
            self.author = bool(fields['enabled'])
        elif name == 'backdrop_culling':
            self.culling = bool(fields['enabled'])
        elif name == 'transport_copy':
            self.staged = bool(fields['staged'])
        elif name == 'load_start':
            self.load_until = self.clock.now() + fields['duration_ms'] / 1000
            self.generated = fields['rate'] * fields['duration_ms'] // 1000
            self.length = fields['length']
        elif name == 'load_stop':
            self.load_until = 0
        elif name == 'touch':
            self.held = bool(fields['pressed']) and fields.get('enabled', 1) != 0
        if name in ('touch', 'scroll', 'burst'):
            self.draws += 1
            # Intentionally coalesce commands: fps must use frames, not steps.
            if self.draws % 3 == 0:
                self.queued += 1
            self.pending_polls = 1
        if name != 'status':
            return {'ok': True}
        pending = self.never_drain or self.pending_polls > 0
        if pending:
            self.pending_seen += 1
            self.pending_polls = max(0, self.pending_polls - 1)
        else:
            self.frames += self.queued
            self.compositions += self.queued + bool(self.queued)
            self.queued = 0
        return {
            'uptime_ms': int(self.clock.now() * 1000), 'ui_frame_p95_us': 31000,
            'board': {'frames': self.frames, 'render_submissions': self.compositions,
                      'display_transfer_pending': pending, 'flush_errors': 0,
                      'touch_errors': self.touch_errors, 'touch_queue_overflows': 0,
                      'display_fault': False, 'staged_transport_copy': self.staged,
                      'test_held': self.held, 'test_monotonic': self.clock.now()},
            'ui': {'fixture_mode': self.fixture, 'paused': self.paused,
                   'sprite_cache_enabled': self.sprite, 'author_cache_enabled': self.author,
                   'backdrop_culling_enabled': self.culling, 'moderation_pending': False,
                   'moderation_confirmation_visible': False, 'moderation_submitted': self.moderation_submitted},
            'fixtures': {'enabled': self.fixture, 'load_active': self.clock.now() < self.load_until,
                         'generated': self.generated, 'schedule_dropped': 0,
                         'append_rejected': 0, 'length_bytes': self.length},
            'twitch': {'channel': 'synthetic_room', 'read_only': True,
                       'moderation_requests': self.moderation_requests, 'moderation_unknown': self.moderation_unknown},
        }


class BenchmarkTests(unittest.TestCase):
    def make_run(self, directory, workload):
        clock = FakeClock()
        device = FakeDevice(clock)
        run = benchmark_scroll.ScrollBenchmark(device, workload, destination=Path(directory) / 'report.json',
                                                clock=clock.now, sleeper=clock.sleep)
        return run, device, clock

    def test_warm_schema_and_fps_use_completed_frame_delta_at_drained_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            run, device, clock = self.make_run(directory, 'warm')
            report = run.run()
            self.assertEqual(report['status'], 'passed')
            self.assertEqual(len(report['runs']), 2)
            for index, result in enumerate(report['runs']):
                self.assertEqual(result['index'], index)
                self.assertTrue(result['culling'])
                self.assertFalse(result['staged'])
                self.assertIn('physical_frames', result)
                self.assertNotIn('frames', result)
                before, after = result['before']['board'], result['after']['board']
                self.assertFalse(before['display_transfer_pending'])
                self.assertFalse(after['display_transfer_pending'])
                self.assertEqual(result['physical_frames'], after['frames'] - before['frames'])
                self.assertAlmostEqual(result['seconds'], after['test_monotonic'] - before['test_monotonic'])
                self.assertEqual(result['fps'], result['physical_frames'] / result['seconds'])
                self.assertLess(result['physical_frames'], 500)  # Coalesced fake frames.
            self.assertGreater(device.pending_seen, 0)
            self.assertIn(('load_start', {'rate': 20, 'duration_ms': 1500, 'length': 500, 'pattern': 'cycle'}), device.calls)
            self.assertEqual(clock.sleeps.count(2), 1)
            saved = json.loads((Path(directory) / 'report.json').read_text())
            self.assertEqual(saved, report)
            self.assertFalse(report['physical_touch_latency_measured'])
            self.assertIn('PRIVATE', report['privacy'])

    def test_dense_held_contact_excludes_release_but_ten_gestures_include_real_idle(self):
        with tempfile.TemporaryDirectory() as directory:
            run, device, clock = self.make_run(directory, 'dense')
            with patch('device.time.sleep', clock.sleep):
                report = run.run()
            self.assertEqual([r['kind'] for r in report['runs']],
                             ['programmatic', 'continuous-drag', 'ten-gestures'])
            for result in report['runs']:
                self.assertIn('frames', result)
                self.assertIn('compositions', result)
                self.assertNotIn('physical_frames', result)
                self.assertEqual(result['fps'], result['frames'] / result['seconds'])
                self.assertFalse(result['after']['board']['display_transfer_pending'])
            held, gestures = report['runs'][1:]
            self.assertTrue(held['before']['board']['test_held'])
            self.assertTrue(held['after']['board']['test_held'])
            self.assertTrue(held['contact_held_through_final_status'])
            self.assertFalse(gestures['after']['board']['test_held'])
            self.assertTrue(gestures['includes_release_idle'])
            self.assertEqual(gestures['release_idle_seconds_total'], .8)
            self.assertEqual(clock.sleeps.count(.018), 150)
            self.assertEqual(clock.sleeps.count(.08), 10)
            self.assertGreater(gestures['seconds'], 150 * .018 + 10 * .08)
            method = report['method']['ten_gestures']
            self.assertIn('not sustained held-contact fps', method['meaning'])
            self.assertEqual(method['points_per_gesture'], 15)
            self.assertEqual(min(benchmark_scroll.continuous_points()), 180)
            self.assertEqual(max(benchmark_scroll.continuous_points()), 370)

    def test_commands_never_change_saved_profile_or_send_network_actions(self):
        allowed = {'status', 'touch', 'load_stop', 'fixture_mode', 'author_cache',
                   'sprite_cache', 'backdrop_culling', 'transport_copy', 'burst',
                   'pause', 'live', 'scroll', 'perf_reset'}
        with tempfile.TemporaryDirectory() as directory:
            run, device, clock = self.make_run(directory, 'dense')
            with patch('device.time.sleep', clock.sleep):
                report = run.run()
            self.assertLessEqual({name for name, _ in device.calls}, allowed)
            self.assertEqual(report['initial']['twitch'], report['final']['twitch'])
            self.assertFalse(device.fixture)
            self.assertFalse(device.held)
            self.assertFalse(device.paused)
            self.assertTrue(all(check['passed'] for check in report['checks']))

    def test_operation_and_release_failure_still_attempt_all_cleanup_without_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            run, device, _ = self.make_run(directory, 'warm')
            operation_failed = False

            def fail(name, fields):
                nonlocal operation_failed
                if name == 'load_start':
                    operation_failed = True
                    raise TimeoutError('SYNTHETIC uncertain command')
                if operation_failed and name == 'touch':
                    raise RuntimeError('SYNTHETIC release failure')

            device.fail_when = fail
            with self.assertRaisesRegex(TimeoutError, 'uncertain command'):
                run.run()
            self.assertEqual(sum(name == 'load_start' for name, _ in device.calls), 1)
            self.assertEqual(device.calls[-5:-1], [
                ('touch', {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0}),
                ('load_stop', {}), ('fixture_mode', {'enabled': 0}), ('live', {})])
            self.assertEqual(device.calls[-1][0], 'status')
            saved = json.loads((Path(directory) / 'report.json').read_text())
            self.assertEqual(saved['status'], 'failed')
            self.assertEqual([entry['ok'] for entry in saved['cleanup']], [False, True, True, True])
            self.assertFalse(saved['final']['ui']['paused'])

    def test_transfer_drain_has_a_deadline_and_cleanup_still_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            run, device, clock = self.make_run(directory, 'warm')
            device.never_drain = True
            with self.assertRaisesRegex(TimeoutError, 'did not drain'):
                run.run()
            self.assertLess(clock.now(), 22)
            self.assertIn(('live', {}), device.calls)
            self.assertFalse(device.fixture)
            self.assertEqual(run.report['status'], 'failed')
            self.assertEqual(run.report['cleanup'][-1]['command'], 'final-status')

    def test_historical_acknowledged_requests_do_not_force_a_reboot(self):
        with tempfile.TemporaryDirectory() as directory:
            run, device, _ = self.make_run(directory, 'warm')
            device.moderation_requests, device.moderation_submitted = 5, 7
            report = run.run()
            self.assertEqual(report['status'], 'passed')
            self.assertEqual(report['initial_moderation_counters'], {'requests': 5, 'ui_submitted': 7})
            self.assertEqual(report['initial']['twitch'], report['final']['twitch'])

    def test_new_moderation_unknown_outcomes_and_touch_errors_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            run, device, _ = self.make_run(directory, 'warm')

            def introduce_request(name, fields):
                if name == 'scroll' and device.draws > 20:
                    device.moderation_requests += 1

            device.fail_when = introduce_request
            with self.assertRaisesRegex(AssertionError, 'Additional moderation'):
                run.run()
            self.assertEqual(run.report['status'], 'failed')
        with tempfile.TemporaryDirectory() as directory:
            run, device, _ = self.make_run(directory, 'warm')
            device.moderation_unknown = 1
            with self.assertRaisesRegex(AssertionError, 'zero unknown'):
                run.run()
            self.assertNotIn(('fixture_mode', {'enabled': 1}), device.calls)
            self.assertEqual(run.report['status'], 'failed')
        with tempfile.TemporaryDirectory() as directory:
            run, device, _ = self.make_run(directory, 'warm')

            def introduce_fault(name, fields):
                if name == 'scroll' and device.draws > 20:
                    device.touch_errors = 1

            device.fail_when = introduce_fault
            with self.assertRaisesRegex(AssertionError, 'touch_errors'):
                run.run()
            self.assertEqual(run.report['status'], 'failed')
            self.assertIn(('fixture_mode', {'enabled': 0}), device.calls)
            self.assertIn(('live', {}), device.calls)

    def test_path_and_workload_bounds_fail_before_connecting(self):
        with patch('benchmark_scroll.connected') as connect, patch('sys.stderr', new_callable=io.StringIO):
            for argv in ([], ['--workload', 'warm', '--steps', '499', '--output', '.local/not-created'],
                         ['--workload', 'dense', '--output', 'public-report']):
                with self.subTest(argv=argv), self.assertRaises(SystemExit):
                    benchmark_scroll.main(argv)
            connect.assert_not_called()
        for steps in (499, 2001, True, 500.0):
            with self.subTest(steps=steps), self.assertRaises(ValueError):
                benchmark_scroll.ScrollBenchmark(None, 'warm', steps)
        self.assertEqual(benchmark_scroll.step_count('2000'), 2000)


if __name__ == '__main__':
    unittest.main()
