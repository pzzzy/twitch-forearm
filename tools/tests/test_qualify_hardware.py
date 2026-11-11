"""Host-only qualification helper tests; never opens a serial endpoint."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qualify_hardware import Qualification, frame_summary, percentile


class FakeClock:
    def __init__(self):
        self.value = 1
        self.sleeps = []

    def now(self):
        return self.value

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.value += seconds


class FakeSoakDevice:
    def __init__(self, clock):
        self.clock, self.calls, self.samples = clock, [], 0
        self.fixture_enabled = self.paused = self.injection_enabled = False

    def command(self, name, **fields):
        self.calls.append((name, fields))
        if name == 'fixture_mode':
            self.fixture_enabled = bool(fields['enabled'])
        if name in ('pause', 'live'):
            self.paused = name == 'pause'
        if name == 'touch':
            self.injection_enabled = bool(fields['enabled'])
        if name != 'status':
            return {'ok': True}
        self.samples += 1
        return {'uptime_ms': round(self.clock.now() * 1000),
                'board': {'frames': self.samples, 'flush_errors': 0, 'touch_errors': 0,
                          'touch_queue_overflows': 0, 'free_internal': 100000 - self.samples,
                          'largest_dma_block': 50000 - self.samples, 'free_psram': 6000000 - self.samples},
                'ui': {'paused': self.paused, 'retained': 100},
                'fixtures': {'enabled': self.fixture_enabled},
                'twitch': {'wifi_connected': True, 'authenticated': True, 'chat_connected': True,
                           'received_messages': 1000 + self.samples * 3,
                           'dropped_events': 0, 'malformed_frames': 0}}


class QualificationTests(unittest.TestCase):
    def test_percentile_uses_nearest_rank_without_mutating_samples(self):
        samples = [3, 1, 9, 2, 5]
        self.assertEqual(percentile(samples, .95), 9)
        self.assertEqual(percentile(samples, .5), 3)
        self.assertEqual(percentile([], .95), 0)
        self.assertEqual(samples, [3, 1, 9, 2, 5])

    def test_frame_rate_uses_frame_delta_and_observed_wall_time(self):
        result = frame_summary({'board': {'frames': 100}},
                               {'board': {'frames': 160, 'recent_frame_p95_us': 33000}, 'ui_frame_p95_us': 64000}, 4)
        self.assertEqual(result['frames'], 60)
        self.assertEqual(result['average_fps'], 15)
        self.assertEqual(result['flush_p95_us'], 33000)
        self.assertEqual(result['ui_frame_p95_us'], 64000)

    def test_snapshots_detect_reboots_and_preserve_the_last_complete_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            fake = Mock()
            fake.command.side_effect = [{'uptime_ms': 5000}, {'uptime_ms': 100}]
            run = Qualification(fake, path)
            run.snapshot('before')
            with self.assertRaisesRegex(AssertionError, 'reset'):
                run.snapshot('after')
            saved = json.loads(path.read_text())
            self.assertEqual(len(saved['samples']), 1)
            self.assertEqual(saved['samples'][0]['label'], 'before')
            self.assertEqual([c.args for c in fake.command.call_args_list], [('status',), ('status',)])

    def test_failed_check_is_durable_before_it_raises(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            run = Qualification(Mock(), path)
            with self.assertRaisesRegex(AssertionError, 'offline'):
                run.check('offline', False, detail='not connected')
            run.finish(False)
            saved = json.loads(path.read_text())
            self.assertEqual(saved['status'], 'failed')
            self.assertFalse(saved['checks'][0]['passed'])
            self.assertIn('does not measure physical', saved['command_roundtrip_ms']['meaning'])

    def test_active_hour_includes_sixty_load_pulses_and_five_sequential_recovery_pairs(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
            # File persistence is checked separately. Keep the virtual hour
            # test focused on activity scheduling rather than disk speed.
            run.save = Mock()

            def pulse(iteration):
                clock.sleep(5)
                return run.snapshot('pulse-' + str(iteration))

            def recovery(command, duration_ms, iteration):
                clock.sleep(4)
                run.snapshot('recovery-' + command)

            run.activity_pulse = Mock(side_effect=pulse)
            run.recover = Mock(side_effect=recovery)
            run.active_soak(3600)
            report = run.report['active_soak']
            self.assertEqual(report['elapsed_seconds'], 3600)
            self.assertEqual(report['activity_pulses'], 60)
            self.assertEqual(run.activity_pulse.call_count, 61)  # Also warms up before baseline.
            self.assertEqual(report['recovery_pairs'], 5)
            self.assertEqual([call.args[0] for call in run.recover.call_args_list],
                             ['network_disconnect', 'wifi_disconnect'] * 5)
            self.assertLessEqual(max(clock.sleeps), 30)
            self.assertEqual(report['input'], 'injected')
            self.assertFalse(report['physical_touch_latency_measured'])
            self.assertEqual(report['retention']['maximum'], 100)
            for field in report['memory_bytes'].values():
                self.assertLess(field['drift_bytes'], 0)
                self.assertEqual(field['minimum'], field['final'])

    def test_soak_finishes_at_short_deadline_and_always_drains_load_and_injection(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
            run.save = Mock()

            def pulse(iteration):
                clock.sleep(5)
                return run.snapshot('pulse')

            run.activity_pulse = Mock(side_effect=pulse)
            run.active_soak(2)
            self.assertEqual(run.report['active_soak']['elapsed_seconds'], 2)
            self.assertEqual(run.report['active_soak']['activity_pulses'], 0)
            self.assertIn(('load_stop', {}), device.calls)
            self.assertIn(('touch', {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0}), device.calls)

    def test_live_between_pulses_restores_actual_chat_before_every_idle_and_recovery(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            idle_observations = []

            def idle(seconds):
                idle_observations.append((device.fixture_enabled, device.paused, device.injection_enabled))
                self.assertFalse(device.fixture_enabled)
                self.assertFalse(device.paused)
                self.assertFalse(device.injection_enabled)
                clock.sleep(seconds)

            run = Qualification(device, Path(directory) / 'report.json', clock.now, idle,
                                live_between_pulses=True)
            run.save = Mock()

            def fixture_pulse(iteration, duration):
                run.command('fixture_mode', enabled=1)
                run.command('touch', x=140, y=160, pressed=1, enabled=1)
                run.command('pause')
                clock.sleep(duration)
                return run.snapshot('fixture-' + str(iteration))

            def recovery(command, duration_ms, iteration):
                self.assertFalse(device.fixture_enabled)
                self.assertFalse(device.paused)
                clock.sleep(4)
                run.snapshot('recovered-' + command)

            run._fixture_activity_pulse = Mock(side_effect=fixture_pulse)
            run.recover = Mock(side_effect=recovery)
            run.active_soak(3600)
            report = run.report['active_soak']
            self.assertEqual(report['elapsed_seconds'], 3600)
            self.assertEqual(report['activity_pulses'], 60)
            self.assertEqual(run._fixture_activity_pulse.call_count, 61)
            self.assertEqual(report['recovery_pairs'], 5)
            self.assertEqual([call.args[0] for call in run.recover.call_args_list],
                             ['network_disconnect', 'wifi_disconnect'] * 5)
            self.assertTrue(idle_observations)
            self.assertLessEqual(max(clock.sleeps), 30)
            self.assertEqual(report['display_mode'], 'live-between-fixture-pulses')
            self.assertTrue(run.report['live_between_pulses'])
            self.assertEqual(report['actual_received_delta'],
                             report['actual_received_after'] - report['actual_received_before'])
            self.assertGreater(report['actual_received_delta'], 0)
            self.assertIn('excluding generated fixtures', report['actual_received_meaning'])
            self.assertIn('prevents a leak conclusion', report['memory_comparison'])
            self.assertFalse(report['physical_touch_latency_measured'])
            self.assertTrue(all(check['passed'] for check in run.report['checks']))
            resumes = [sample['device'] for sample in run.report['samples']
                       if sample['label'].endswith('-live-between-pulses')]
            self.assertEqual(len(resumes), 61) # Warm-up included, no fixture during baseline.
            self.assertTrue(all(not state['fixtures']['enabled'] and not state['ui']['paused'] for state in resumes))
            self.assertFalse(device.fixture_enabled)
            self.assertFalse(device.injection_enabled)

    def test_live_pulse_failure_restores_chat_and_preserves_the_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            device.fixture_enabled = device.paused = device.injection_enabled = True
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep,
                                live_between_pulses=True)
            run.save = Mock()
            run._fixture_activity_pulse = Mock(side_effect=AssertionError('pulse failed'))
            with self.assertRaisesRegex(AssertionError, 'pulse failed'):
                run.activity_pulse('warmup')
            self.assertEqual([name for name, _ in device.calls],
                             ['load_stop', 'touch', 'fixture_mode', 'live', 'status'])
            self.assertFalse(device.fixture_enabled)
            self.assertFalse(device.injection_enabled)
            self.assertFalse(device.paused)

    def test_live_pulse_cleanup_attempts_all_restoration_steps_after_stop_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            device.fixture_enabled = device.paused = device.injection_enabled = True
            original = device.command

            def command(name, **fields):
                value = original(name, **fields)
                if name == 'load_stop':
                    raise TimeoutError('stop timeout')
                return value

            device.command = command
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep,
                                live_between_pulses=True)
            run._fixture_activity_pulse = Mock(return_value={})
            with self.assertRaisesRegex(RuntimeError, 'load_stop'):
                run.activity_pulse(1)
            self.assertEqual([name for name, _ in device.calls],
                             ['load_stop', 'touch', 'fixture_mode', 'live'])
            self.assertFalse(device.fixture_enabled)
            self.assertFalse(device.injection_enabled)
            self.assertFalse(device.paused)

    def test_live_mode_cleanup_keeps_explicit_monitor_live_even_if_initially_paused(self):
        with tempfile.TemporaryDirectory() as directory:
            device = Mock()
            run = Qualification(device, Path(directory) / 'report.json', live_between_pulses=True)
            run.initial_paused = True
            run.cleanup()
            self.assertEqual([call.args[0] for call in device.command.call_args_list],
                             ['load_stop', 'touch', 'fixture_mode', 'live'])

    def test_live_mode_preserves_zero_error_soak_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep,
                                live_between_pulses=True)
            run.save = Mock()

            def fixture_pulse(iteration, duration):
                clock.sleep(duration)
                state = run.snapshot('fixture-' + str(iteration))
                if iteration != 'warmup':
                    state['board']['touch_errors'] = 1
                return state

            run._fixture_activity_pulse = Mock(side_effect=fixture_pulse)
            with self.assertRaisesRegex(AssertionError, 'No new soak touch_errors'):
                run.active_soak(20)
            self.assertFalse(device.fixture_enabled)
            self.assertFalse(device.injection_enabled)
            self.assertFalse(device.paused)
            self.assertFalse(next(check for check in run.report['checks']
                                  if check['name'] == 'No new soak touch_errors')['passed'])

    def test_injection_is_explicit_and_release_runs_after_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = Mock()
            device.command.side_effect = [RuntimeError('synthetic failure'), {'ok': True}]
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
            with self.assertRaisesRegex(RuntimeError, 'synthetic failure'):
                run.inject([(140, 120)])
            first, last = device.command.call_args_list
            self.assertEqual(first.args, ('touch',))
            self.assertEqual(first.kwargs, {'x': 140, 'y': 120, 'pressed': 1, 'enabled': 1})
            self.assertEqual(last.kwargs, {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0})

    def test_cleanup_restores_original_reading_preference(self):
        with tempfile.TemporaryDirectory() as directory:
            for paused, final in ((True, 'pause'), (False, 'live')):
                device = Mock()
                run = Qualification(device, Path(directory) / 'report.json')
                run.initial_paused = paused
                run.cleanup()
                self.assertEqual([call.args[0] for call in device.command.call_args_list],
                                 ['load_stop', 'touch', 'fixture_mode', final])

    def test_cleanup_attempts_release_and_restore_even_when_stop_times_out(self):
        with tempfile.TemporaryDirectory() as directory:
            device = Mock()
            device.command.side_effect = [TimeoutError('stop'), {}, {}, {}]
            run = Qualification(device, Path(directory) / 'report.json')
            with self.assertRaisesRegex(RuntimeError, 'load_stop'):
                run.cleanup()
            self.assertEqual([call.args[0] for call in device.command.call_args_list],
                             ['load_stop', 'touch', 'fixture_mode', 'live'])

    def test_activity_uses_only_bounded_ui_fixture_and_injected_input_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = Mock()
            states = iter([
                {'ui': {'paused': True, 'scrolls': 10}},
                {'ui': {'paused': True, 'scrolls': 11, 'unread': 3, 'anchor_sequence': 7, 'anchor_offset': 12}},
                {'ui': {'paused': True, 'scrolls': 11, 'unread': 8, 'anchor_sequence': 7, 'anchor_offset': 12}},
                {'ui': {'paused': False, 'unread': 0}},
                {'fixtures': {'load_active': False, 'generated': 100, 'schedule_dropped': 0, 'append_rejected': 0}},
            ])

            def command(name, **fields):
                if name != 'status':
                    return {'ok': True}
                return {'uptime_ms': round(clock.now() * 1000), **next(states)}

            device.command.side_effect = command
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
            run.save = Mock()
            run.activity_pulse(1)
            calls = device.command.call_args_list
            self.assertLessEqual(set(call.args[0] for call in calls),
                                 {'fixture_mode', 'live', 'load_start', 'touch', 'status'})
            load = next(call for call in calls if call.args[0] == 'load_start')
            self.assertEqual(load.kwargs, {'rate': 20, 'duration_ms': 5000, 'length': 500, 'pattern': 'cycle'})
            touches = [call.kwargs for call in calls if call.args[0] == 'touch']
            self.assertTrue(all(0 <= event['x'] < 320 and 0 <= event['y'] < 480 for event in touches))
            self.assertEqual(touches[-1], {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0})
            self.assertTrue(all(check['passed'] for check in run.report['checks']))
            self.assertEqual(run.report['checks'][-2]['input'], 'injected')

    def test_stuck_load_times_out_without_unbounded_polling(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            original_command = device.command

            def command(name, **fields):
                result = original_command(name, **fields)
                if name == 'status':
                    result['fixtures'] = {'load_active': True}
                return result

            device.command = command
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
            run.save = Mock()
            with self.assertRaisesRegex(TimeoutError, 'bounded duration'):
                run.wait_load('stuck', poll=.5, timeout=2)
            self.assertEqual(clock.now(), 3)
            self.assertEqual(device.samples, 5)
            self.assertLessEqual(max(clock.sleeps), .5)

    def test_recovery_observes_delayed_worker_fault_entry_and_restoration(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            original_command = device.command
            fault_at = None

            def command(name, **fields):
                nonlocal fault_at
                result = original_command(name, **fields)
                if name == 'wifi_disconnect':
                    fault_at = clock.now() + 2  # A bounded request was already in progress.
                if name == 'status':
                    net = result['twitch']
                    net.update(login='operator', channel='channel')
                    if fault_at is not None and fault_at <= clock.now() < fault_at + 2:
                        net.update(wifi_connected=False, chat_connected=False)
                return result

            device.command = command
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
            run.save = Mock()
            run.recover('wifi_disconnect', 2000, 1)
            self.assertEqual(clock.now(), 5)
            self.assertTrue(all(check['passed'] for check in run.report['checks']))
            faults = [call for call in device.calls if call[0] == 'wifi_disconnect']
            self.assertEqual(faults, [('wifi_disconnect', {'duration_ms': 2000})])

    def test_unobserved_fault_entry_fails_after_bounded_wait(self):
        with tempfile.TemporaryDirectory() as directory:
            clock = FakeClock()
            device = FakeSoakDevice(clock)
            run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
            run.save = Mock()
            with self.assertRaisesRegex(AssertionError, 'entered fault'):
                run.recover('wifi_disconnect', 2000, 1)
            self.assertEqual(clock.now(), 31)
            self.assertFalse(run.report['checks'][-1]['passed'])

    def test_refresh_submits_once_and_observes_delayed_success_failure_or_unknown(self):
        for outcome, error in [('success', None), ('failure', AssertionError),
                               ('unknown', TimeoutError), ('mixed', AssertionError), ('rejected', RuntimeError)]:
            with self.subTest(outcome=outcome), tempfile.TemporaryDirectory() as directory:
                clock = FakeClock()
                device = FakeSoakDevice(clock)
                original_command = device.command
                queued_at = None

                def command(name, **fields):
                    nonlocal queued_at
                    result = original_command(name, **fields)
                    if name == 'refresh_login':
                        queued_at = clock.now()
                        if outcome == 'rejected':
                            raise RuntimeError('Device rejected command: refresh_login')
                    if name == 'status':
                        age = clock.now() - queued_at if queued_at is not None else -1
                        # The existing serialized worker operation takes two
                        # seconds before this request can start, then finishes.
                        result['twitch'].update(refresh_attempts=int(age >= 2),
                            refresh_succeeded=int(age >= 4 and outcome in ('success', 'mixed')),
                            refresh_failed=int(age >= 4 and outcome in ('failure', 'mixed')))
                    return result

                device.command = command
                run = Qualification(device, Path(directory) / 'report.json', clock.now, clock.sleep)
                run.save = Mock()
                if error:
                    with self.assertRaises(error):
                        run.refresh_once(timeout=5)
                else:
                    run.refresh_once(timeout=5)
                    self.assertEqual(clock.now(), 5)
                    self.assertEqual(run.report['samples'][-1]['label'], 'refresh-restored')
                self.assertEqual([name for name, _ in device.calls].count('refresh_login'), 1)
                self.assertLessEqual(set(name for name, _ in device.calls), {'status', 'refresh_login'})
                if outcome == 'unknown':
                    self.assertEqual(clock.now(), 6)  # Deadline ends observation, never replays the request.
                if outcome == 'rejected':
                    self.assertEqual(clock.now(), 1)


if __name__ == '__main__':
    unittest.main()
