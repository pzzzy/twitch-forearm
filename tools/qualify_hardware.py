#!/usr/bin/env python3
"""USB-only qualification. Generates labeled UI fixtures; never posts to Twitch.

Default smoke run is short. --profile full adds 20 messages/s for 10 minutes,
100/s for 5 seconds, repeated recovery and an active one-hour soak. The soak
adds 20/s load and injected input every minute and recovery every ten minutes.
Reports contain secret-free device status and stay under ignored .local by default.
"""
import argparse
import json
import math
from pathlib import Path
import statistics
import time

from device import connected


def percentile(values, fraction):
    if not values:
        return 0
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def frame_summary(before, after, seconds):
    frames = after['board']['frames'] - before['board']['frames']
    return {'frames': frames, 'elapsed_seconds': seconds,
            'average_fps': frames / seconds if seconds > 0 else 0,
            'ui_frame_p95_us': after.get('ui_frame_p95_us'),
            'flush_p95_us': after['board'].get('recent_frame_p95_us')}


class Qualification:
    def __init__(self, device, destination, clock=None, sleeper=None):
        self.device, self.destination = device, destination
        self.now, self.sleep = clock or time.monotonic, sleeper or time.sleep
        self.last_uptime = 0
        self.initial_paused = None
        self.roundtrips = []
        self.report = {'schema': 1, 'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                       'status': 'running', 'checks': [], 'samples': [],
                       'scope': 'Synthetic UI load and network recovery only; no chat posts or moderation'}

    def save(self):
        self.destination.parent.mkdir(parents=True, exist_ok=True)
        self.destination.write_text(json.dumps(self.report, indent=2) + '\n')

    def command(self, name, **fields):
        started = self.now()
        result = self.device.command(name, **fields)
        self.roundtrips.append((self.now() - started) * 1000)
        return result

    def snapshot(self, label):
        value = self.command('status')
        if value['uptime_ms'] < self.last_uptime:
            raise AssertionError('Device reset during qualification')
        self.last_uptime = value['uptime_ms']
        if self.initial_paused is None and 'ui' in value:
            self.initial_paused = value['ui']['paused']
        self.report['samples'].append({'label': label, 'host_monotonic': self.now(), 'device': value})
        self.save()
        return value

    def check(self, name, passed, **details):
        self.report['checks'].append({'name': name, 'passed': bool(passed), **details})
        self.save()
        if not passed:
            raise AssertionError(name)

    def wait_connected(self, label, timeout=90):
        deadline = self.now() + timeout
        while True:
            state = self.snapshot(label)
            net = state['twitch']
            if net['wifi_connected'] and net['authenticated'] and net['chat_connected']:
                return state
            if self.now() >= deadline:
                raise TimeoutError('Saved Twitch session did not reconnect')
            self.sleep(min(1, max(0, deadline - self.now())))

    def wait_load(self, label, poll=2, timeout=615):
        deadline = self.now() + timeout
        while True:
            value = self.snapshot(label)
            if not value['fixtures']['load_active']:
                return value
            if self.now() >= deadline:
                raise TimeoutError('Synthetic load did not finish within its bounded duration')
            self.sleep(min(poll, max(0, deadline - self.now())))

    def load(self, rate, seconds, length=500, require_no_drop=True):
        self.command('fixture_mode', enabled=0)
        self.command('fixture_mode', enabled=1)
        self.command('live')
        self.command('perf_reset')
        before = self.snapshot(f'load-{rate}-before')
        self.command('load_start', rate=rate, duration_ms=round(seconds * 1000), length=length, pattern='cycle')
        started = self.now()
        after = self.wait_load(f'load-{rate}', min(2, seconds / 3), seconds + 15)
        fixture = after['fixtures']
        expected = rate * round(seconds * 1000) // 1000
        passed = fixture['generated'] + fixture['schedule_dropped'] == expected and fixture['append_rejected'] == 0
        if require_no_drop:
            passed = passed and fixture['schedule_dropped'] == 0
        self.check(f'{rate}/s synthetic load', passed, expected=expected, fixtures=fixture,
                   performance=frame_summary(before, after, self.now() - started),
                   schedule_drops_allowed=not require_no_drop)

    def pause_check(self):
        self.command('fixture_mode', enabled=1)
        self.command('load_start', rate=20, duration_ms=4000, length=160, pattern='mixed')
        self.sleep(1)
        self.command('pause')
        before = self.snapshot('pause-before')
        self.sleep(.65)
        after = self.snapshot('pause-after')
        a, b = before['ui'], after['ui']
        self.check('Paused anchor and unread', a['paused'] and b['paused'] and b['unread'] > a['unread'] and
                   (a['anchor_sequence'], a['anchor_offset']) == (b['anchor_sequence'], b['anchor_offset']),
                   before=a, after=b)
        self.command('live')
        live = self.snapshot('go-live')['ui']
        self.check('Go live clears unread', not live['paused'] and live['unread'] == 0)
        self.command('load_stop')

    def recover(self, command, duration_ms, iteration):
        before = self.wait_connected(f'{command}-{iteration}-before')
        self.command(command, duration_ms=duration_ms)
        # The worker may be finishing a bounded TLS request when the command
        # arrives. Observe entry rather than assuming it happened within 1s.
        entry_deadline = self.now() + 30
        while True:
            down = self.snapshot(f'{command}-{iteration}-disconnected')
            entered = not down['twitch']['chat_connected'] and (command != 'wifi_disconnect' or not down['twitch']['wifi_connected'])
            if entered or self.now() >= entry_deadline:
                self.check(f'{command} entered fault {iteration}', entered)
                break
            self.sleep(min(.25, max(0, entry_deadline - self.now())))
        started = self.now()
        after = self.wait_connected(f'{command}-{iteration}-restored')
        self.check(f'{command} recovered {iteration}', after['twitch']['login'] == before['twitch']['login'] and
                   after['twitch']['channel'] == before['twitch']['channel'], recovery_seconds=self.now() - started)

    def refresh_once(self, timeout=90):
        """Submit once, then observe. Never replay a rotating-token request."""
        before = self.snapshot('refresh-before')['twitch']
        self.command('refresh_login')
        deadline = self.now() + timeout
        while True:
            after = self.snapshot('refresh-wait')['twitch']
            if after['refresh_failed'] > before['refresh_failed']:
                raise AssertionError('Explicit saved-token refresh failed; it was not retried')
            if after['refresh_succeeded'] > before['refresh_succeeded']:
                return self.wait_connected('refresh-restored')
            if self.now() >= deadline:
                raise TimeoutError('Saved-token refresh outcome unknown; it was not retried')
            self.sleep(min(1, max(0, deadline - self.now())))

    def inject(self, points, step=.06):
        """Explicit synthetic input. Always releases and disables injection."""
        try:
            for x, y in points:
                self.command('touch', x=x, y=y, pressed=1, enabled=1)
                self.sleep(step)
            x, y = points[-1]
            self.command('touch', x=x, y=y, pressed=0, enabled=1)
            self.sleep(.12)
        finally:
            self.command('touch', x=0, y=0, pressed=0, enabled=0)

    def activity_pulse(self, iteration, duration=5):
        """Synthetic load plus injected pause, drag and Go live; no live posts."""
        self.command('fixture_mode', enabled=0)
        self.command('fixture_mode', enabled=1)
        self.command('live')
        self.command('load_start', rate=20, duration_ms=round(duration * 1000), length=500, pattern='cycle')
        self.sleep(min(1, duration / 4))
        self.inject([(284, 444)], .12)  # Portrait pause circle / Go live pill.
        paused = self.snapshot(f'soak-{iteration}-injected-pause')
        self.check(f'Injected pause {iteration}', paused['ui']['paused'], input='injected')
        self.inject([(140, y) for y in range(120, 321, 25)])
        before = self.snapshot(f'soak-{iteration}-injected-scroll')
        self.sleep(.4)
        after = self.snapshot(f'soak-{iteration}-paused-arrivals')
        a, b = before['ui'], after['ui']
        self.check(f'Injected scroll and paused anchor {iteration}', b['paused'] and
                   b['scrolls'] > paused['ui']['scrolls'] and b['unread'] > a['unread'] and
                   (a['anchor_sequence'], a['anchor_offset']) == (b['anchor_sequence'], b['anchor_offset']), input='injected')
        self.inject([(284, 444)], .12)
        live = self.snapshot(f'soak-{iteration}-injected-go-live')
        self.check(f'Injected Go live {iteration}', not live['ui']['paused'] and live['ui']['unread'] == 0,
                   input='injected', physical_touch_latency_measured=False)
        final = self.wait_load(f'soak-{iteration}-load-complete', .5, duration + 15)
        fixture = final['fixtures']
        expected = 20 * round(duration * 1000) // 1000
        self.check(f'Soak pulse load {iteration}', fixture['generated'] == expected and
                   fixture['schedule_dropped'] == 0 and fixture['append_rejected'] == 0, fixtures=fixture)
        return final

    def active_soak(self, seconds, activity_period=60, recovery_period=600):
        # Warm up allocation/render paths before taking a comparable baseline.
        self.activity_pulse('warmup')
        baseline = self.snapshot('soak-warm-baseline')
        started = self.now()
        deadline = started + seconds
        next_activity, next_recovery = started, started + recovery_period
        pulses = recoveries = 0
        sample_start = len(self.report['samples']) - 1
        errors = (('board', 'flush_errors'), ('board', 'touch_errors'), ('board', 'touch_queue_overflows'),
                  ('twitch', 'dropped_events'), ('twitch', 'malformed_frames'))

        def healthy(state, require_online=True):
            for group, name in errors:
                if state[group].get(name, 0) != baseline[group].get(name, 0):
                    self.check('No new soak ' + name, False, before=baseline[group].get(name, 0), after=state[group].get(name, 0))
            if require_online:
                net = state['twitch']
                self.check('Soak TLS/session remains connected', net['wifi_connected'] and net['authenticated'] and net['chat_connected'])
            self.check('Bounded soak retention', state['ui']['retained'] <= 512, retained=state['ui']['retained'])

        try:
            while self.now() < deadline:
                if self.now() >= next_recovery:
                    # Recovery actions are sequential. Avoid starting a new
                    # outage when the qualification deadline is too close.
                    if deadline - self.now() >= 30:
                        recoveries += 1
                        for fault in ('network_disconnect', 'wifi_disconnect'):
                            self.recover(fault, 3000, f'soak-{recoveries}')
                    next_recovery += recovery_period
                if self.now() >= deadline:
                    break
                if self.now() >= next_activity and deadline - self.now() >= 5:
                    pulses += 1
                    healthy(self.activity_pulse(pulses))
                    # Absolute schedule: activities count toward the hour and
                    # missed slots never cause unbounded catch-up bursts.
                    next_activity = started + (math.floor((self.now() - started) / activity_period) + 1) * activity_period
                elif deadline - self.now() < 5:
                    next_activity = deadline
                state = self.snapshot('soak-health')
                healthy(state)
                wake = min(deadline, next_activity, next_recovery, self.now() + 30)
                self.sleep(max(.01, wake - self.now()))
        finally:
            # A slow USB or recovery operation may cross the deadline. Drain
            # the bounded load and leave an explicit final health sample.
            self.command('load_stop')
            self.command('touch', x=0, y=0, pressed=0, enabled=0)
            final = self.snapshot('soak-final-before-cleanup')
            samples = [sample['device'] for sample in self.report['samples'][sample_start:]]
            memory = {}
            for key in ('free_internal', 'largest_dma_block', 'free_psram'):
                values = [sample['board'][key] for sample in samples]
                memory[key] = {'warm_baseline': baseline['board'][key], 'minimum': min(values),
                               'final': final['board'][key], 'drift_bytes': final['board'][key] - baseline['board'][key]}
            retained = [sample['ui']['retained'] for sample in samples]
            self.report['active_soak'] = {'requested_seconds': seconds, 'elapsed_seconds': self.now() - started,
                'activity_period_seconds': activity_period, 'recovery_period_seconds': recovery_period,
                'activity_pulses': pulses, 'recovery_pairs': recoveries, 'input': 'injected',
                'memory_bytes': memory, 'retention': {'minimum': min(retained), 'maximum': max(retained), 'final': retained[-1]},
                'physical_touch_latency_measured': False}
            self.save()
        healthy(final)
        self.check('Active soak reached monotonic duration', self.now() - started >= seconds)

    def cleanup(self):
        failures = []
        for name, fields in [('load_stop', {}), ('touch', {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0}),
                             ('fixture_mode', {'enabled': 0}), ('pause' if self.initial_paused else 'live', {})]:
            try:
                self.command(name, **fields)
            except Exception as error:
                failures.append((name, error))
        if failures:
            raise RuntimeError('Cleanup could not confirm: ' + ', '.join(name for name, _ in failures)) from failures[0][1]

    def finish(self, success):
        self.report['status'] = 'passed' if success else 'failed'
        self.report['command_roundtrip_ms'] = {'samples': len(self.roundtrips),
            'median': statistics.median(self.roundtrips) if self.roundtrips else 0,
            'p95': percentile(self.roundtrips, .95), 'max': max(self.roundtrips, default=0),
            'meaning': 'Host USB command roundtrip; this does not measure physical touch-to-photon latency'}
        self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--profile', choices=('smoke', 'load', 'soak', 'full'), default='smoke')
    parser.add_argument('--smoke-seconds', type=float, default=5)
    parser.add_argument('--load-seconds', type=float, default=600)
    parser.add_argument('--soak-seconds', type=float, default=3600)
    parser.add_argument('--recoveries', type=int, default=3)
    parser.add_argument('--refresh', action='store_true', help='Also rotate the saved OAuth token once, without revealing it')
    parser.add_argument('--capture', action='store_true', help='Save a final screenshot after stress has stopped')
    parser.add_argument('--output', type=Path, default=Path('.local') / ('qualification-' + time.strftime('%Y%m%d-%H%M%S')) / 'report.json')
    args = parser.parse_args()
    if not (2 <= args.smoke_seconds <= 60 and 1 <= args.load_seconds <= 600 and
            1 <= args.soak_seconds <= 86400 and 1 <= args.recoveries <= 100):
        parser.error('Durations or recovery count outside bounded ranges')
    if args.output.exists():
        raise FileExistsError(args.output)
    success = False
    with connected(args.port) as device:
        run = Qualification(device, args.output)
        try:
            initial = run.wait_connected('initial')
            run.pause_check()
            run.load(20, args.smoke_seconds if args.profile == 'smoke' else args.load_seconds)
            run.load(100, min(5, args.smoke_seconds), require_no_drop=False)
            for index in range(1 if args.profile == 'smoke' else args.recoveries):
                for fault in ('network_disconnect', 'wifi_disconnect'):
                    run.recover(fault, 2000 if args.profile == 'smoke' else 5000, index + 1)
            if args.refresh:
                run.refresh_once()
            if args.profile in ('soak', 'full'):
                run.active_soak(args.soak_seconds)
            run.command('fixture_mode', enabled=0)
            final = run.wait_connected('final')
            for key in ('flush_errors', 'touch_errors'):
                run.check('No new ' + key, final['board'][key] == initial['board'][key],
                          before=initial['board'][key], after=final['board'][key])
            if args.capture:
                device.screen(args.output.with_name('final-screen.png'))
            success = True
        except BaseException as error:
            run.report['error'] = type(error).__name__ + ': ' + str(error)
            raise
        finally:
            try:
                run.cleanup()
            except Exception as cleanup_error:
                success = False
                run.report['cleanup_error'] = type(cleanup_error).__name__ + ': ' + str(cleanup_error)
            finally:
                run.finish(success)
                print(args.output.resolve())


if __name__ == '__main__':
    main()
