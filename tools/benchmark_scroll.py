#!/usr/bin/env python3
"""Reproduce warm or dense native scrolling over trusted USB diagnostics.

Uses synthetic fixtures and injected input, never Twitch posts/moderation or
profile changes. Clears RAM history and finishes LIVE with fixtures/injection
off. Reports contain full private status; keep them under ignored .local/.
Completed-frame rates are not physical-finger or photon-latency measurements.
A passed report means the workload/transport checks completed; evaluate its
reported FPS and frame times against the release performance gates separately.
"""
import argparse
import json
from pathlib import Path
import sys
import time

from device import connected


PRIVACY_WARNING = ('PRIVATE: report.json contains complete device status, which may include '
                   'account/channel and device identity. Keep it under ignored .local/.')
WARM_STATES = 30


def continuous_points():
    """Original held-contact path; warm-up ends at its first 180 px point."""
    return tuple(range(180, 371, 12)) + tuple(range(370, 179, -12))


def gesture_points(index):
    # Preserve the original 15-point gestures. Device.gesture adds an 80 ms
    # release idle after each one; its wall time belongs to this workload.
    return [(150, y) for y in (range(140, 371, 16) if index % 2 == 0 else range(370, 139, -16))]


def frame_result(before, after, seconds, *, dense=False):
    if seconds <= 0:
        raise AssertionError('Benchmark requires positive observed elapsed time')
    frames = after['board']['frames'] - before['board']['frames']
    compositions = after['board']['render_submissions'] - before['board']['render_submissions']
    if frames < 0 or compositions < 0:
        raise AssertionError('Frame/composition counters moved backwards')
    result = {'seconds': seconds, 'fps': frames / seconds, 'before': before, 'after': after}
    if dense:
        result.update(frames=frames, compositions=compositions)
    else:
        result['physical_frames'] = frames
    return result


class ScrollBenchmark:
    def __init__(self, device, workload, steps=500, destination=None, clock=None, sleeper=None):
        if workload not in ('warm', 'dense') or isinstance(steps, bool) or not isinstance(steps, int) or not 500 <= steps <= 2000:
            raise ValueError('Choose warm/dense and 500..2000 steps')
        self.device, self.workload, self.steps = device, workload, steps
        self.destination = Path(destination) if destination is not None else None
        self.now, self.sleep = clock or time.monotonic, sleeper or time.sleep
        self.last_uptime = 0
        self.profile = None
        self.action_counts = None
        self.report = {
            'schema': 1, 'workload': workload, 'steps': steps, 'status': 'running',
            'status_meaning': 'passed means the workload and safety/cleanup checks completed; inspect fps separately',
            'privacy': PRIVACY_WARNING, 'input': 'injected USB diagnostics',
            'physical_touch_latency_measured': False, 'photon_latency_measured': False,
            'scope': 'Synthetic native UI scrolling; no Twitch posts, mutations or profile changes',
            'method': {
                'body_cache': True, 'author_cache': True, 'culling': True, 'staged': False,
                'timing': 'Host monotonic time; before/after completed-transfer boundaries',
                'frames': 'Delta of completed physical panel frame transfers, not scroll commands',
                'programmatic_step_pixels': 12,
                'warm_fixture': {'messages': 30, 'length_bytes': 500, 'rate_per_second': 20,
                                 'duration_ms': 1500, 'wait_after_start_seconds': 2},
                'dense_fixture': {'burst_count': 100, 'source': 'Firmware built-in synthetic demo messages'},
                'continuous_drag': {'warmup_y': [140, 160, 180], 'timed_y_range': [180, 370],
                                    'step_pixels': 12, 'release': 'Only after the final timed status'},
                'ten_gestures': {'count': 10, 'points_per_gesture': 15, 'point_sleep_seconds': .018,
                                 'release_idle_seconds_each': .08, 'release_idle_seconds_total': .8,
                                 'meaning': 'Includes Device.gesture release/80 ms idle and USB overhead; not sustained held-contact fps'},
            },
            'runs': [], 'checks': [], 'cleanup': [],
        }

    def save(self):
        if self.destination is not None:
            temporary = self.destination.with_suffix(self.destination.suffix + '.tmp')
            temporary.write_text(json.dumps(self.report, indent=2) + '\n')
            temporary.replace(self.destination)

    def command(self, name, **fields):
        # Each command is sent once. In particular there is no timeout replay.
        return self.device.command(name, **fields)

    def status(self, timeout=None):
        state = self.command('status', **({'timeout': timeout} if timeout is not None else {}))
        if state['uptime_ms'] < self.last_uptime:
            raise AssertionError('Device reset during benchmark')
        self.last_uptime = state['uptime_ms']
        for key in ('flush_errors', 'touch_errors', 'touch_queue_overflows'):
            if state['board'][key] != 0:
                raise AssertionError('Benchmark requires zero board ' + key)
        if state['board']['display_fault']:
            raise AssertionError('Display fault during benchmark')
        actions = (state['twitch']['moderation_requests'], state['ui']['moderation_submitted'])
        if self.action_counts is None:
            self.action_counts = actions
            self.report['initial_moderation_counters'] = {'requests': actions[0], 'ui_submitted': actions[1]}
        elif actions != self.action_counts:
            raise AssertionError('Additional moderation request/submission during benchmark')
        if state['twitch']['moderation_unknown'] != 0:
            raise AssertionError('Benchmark requires zero unknown moderation outcomes')
        if state['ui']['moderation_pending'] or state['ui']['moderation_confirmation_visible']:
            raise AssertionError('Moderation pending/confirmation must be closed before benchmarking')
        profile = (state['twitch']['channel'], state['twitch']['read_only'])
        if self.profile is None:
            self.profile = profile
        elif profile != self.profile:
            raise AssertionError('Channel/read-only profile changed during benchmark')
        return state

    def settled(self, timeout=10):
        deadline = self.now() + timeout
        while True:
            remaining = deadline - self.now()
            if remaining <= 0:
                raise TimeoutError('Display transfer did not drain within benchmark deadline')
            state = self.status(timeout=min(8, remaining))
            if self.now() > deadline:
                raise TimeoutError('Display transfer did not drain within benchmark deadline')
            if not state['board']['display_transfer_pending']:
                return state
            if self.now() >= deadline:
                raise TimeoutError('Display transfer did not drain within benchmark deadline')
            self.sleep(min(.005, max(0, deadline - self.now())))

    def check(self, name, passed):
        self.report['checks'].append({'name': name, 'passed': bool(passed)})
        if not passed:
            raise AssertionError(name)

    def measure(self, kind, index=0):
        self.settled()
        self.command('perf_reset')
        before = self.settled()
        self.check_rendering(before)
        started = self.now()
        if kind == 'programmatic':
            for _ in range(self.steps):
                self.command('scroll', pixels=12)
        elif kind == 'continuous-drag':
            points = continuous_points()
            for i in range(self.steps):
                self.command('touch', x=150, y=points[i % len(points)], pressed=1)
        else:
            for i in range(10):
                self.device.gesture(gesture_points(i), step=.018)
        # A held contact is deliberately still down through the final timed
        # status. Releasing first would mix an extra release/idle into its fps.
        after = self.settled()
        seconds = self.now() - started
        self.check_rendering(after)
        result = frame_result(before, after, seconds, dense=self.workload == 'dense')
        if self.workload == 'warm':
            result.update(index=index, culling=True, staged=False)
        else:
            result['kind'] = kind
            if kind == 'continuous-drag':
                result['contact_held_through_final_status'] = True
            if kind == 'ten-gestures':
                result['includes_release_idle'] = True
                result['release_idle_seconds_total'] = .8
        self.report['runs'].append(result)
        self.save()

    def check_rendering(self, state):
        if not (state['fixtures']['enabled'] and state['ui']['fixture_mode'] and
                state['ui']['sprite_cache_enabled'] and state['ui']['author_cache_enabled'] and
                state['ui']['backdrop_culling_enabled'] and not state['board']['staged_transport_copy']):
            raise AssertionError('Fixture/rendering configuration changed during benchmark')

    def configure(self):
        self.command('touch', x=0, y=0, pressed=0, enabled=0)
        self.command('load_stop')
        self.command('fixture_mode', enabled=0)
        self.command('fixture_mode', enabled=1)
        self.command('author_cache', enabled=1)
        self.command('sprite_cache', enabled=1)
        self.command('backdrop_culling', enabled=1)
        self.command('transport_copy', staged=0)

    def warm(self):
        self.command('load_start', rate=20, duration_ms=1500, length=500, pattern='cycle')
        self.sleep(2)
        prepared = self.settled()
        self.report['prepared'] = prepared
        fixture = prepared['fixtures']
        self.check('30 synthetic 500-byte messages prepared without drops',
                   not fixture['load_active'] and fixture['generated'] == WARM_STATES and
                   fixture['schedule_dropped'] == 0 and fixture['append_rejected'] == 0 and
                   fixture['length_bytes'] == 500)
        self.command('pause')
        for index in range(2):
            self.command('scroll', pixels=-20000)
            self.sleep(.25)
            for _ in range(6):
                self.command('scroll', pixels=12)
            self.measure('programmatic', index)

    def dense(self):
        self.command('burst', count=100)
        self.command('pause')
        for kind in ('programmatic', 'continuous-drag', 'ten-gestures'):
            self.command('scroll', pixels=-6000)
            self.sleep(.2)
            if kind == 'continuous-drag':
                for y in (140, 160, 180):
                    self.command('touch', x=150, y=y, pressed=1)
            self.measure(kind)
            self.command('touch', x=0, y=0, pressed=0, enabled=0)
            self.sleep(.2)

    def run(self):
        failure = None
        try:
            self.save()
            self.report['initial'] = self.status()
            self.configure()
            configured = self.settled()
            self.check('Fixture and measured rendering configuration active',
                       configured['fixtures']['enabled'] and configured['ui']['fixture_mode'] and
                       configured['ui']['sprite_cache_enabled'] and configured['ui']['author_cache_enabled'] and
                       configured['ui']['backdrop_culling_enabled'] and not configured['board']['staged_transport_copy'])
            getattr(self, self.workload)()
        except BaseException as error:
            failure = error
            self.report['error'] = type(error).__name__ + ': ' + str(error)
        finally:
            # A failed release must not prevent load_stop, fixture exit or LIVE.
            # None of these cleanup commands is automatically retried.
            for name, fields in (
                ('touch', {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0}),
                ('load_stop', {}), ('fixture_mode', {'enabled': 0}), ('live', {}),
            ):
                entry = {'command': name, 'ok': False}
                try:
                    self.command(name, **fields)
                    entry['ok'] = True
                except BaseException as error:
                    entry['error'] = type(error).__name__ + ': ' + str(error)
                    if failure is None:
                        failure = error
                self.report['cleanup'].append(entry)
            try:
                final = self.settled()
                self.report['final'] = final
                self.check('Cleanup restored fixture off, load stopped and LIVE',
                           not final['fixtures']['enabled'] and not final['fixtures']['load_active'] and
                           not final['ui']['fixture_mode'] and not final['ui']['paused'])
                self.check('Moderation counters unchanged; zero unknown outcomes/display/touch faults; profile unchanged', True)
            except BaseException as error:
                self.report['cleanup'].append({'command': 'final-status', 'ok': False,
                                                'error': type(error).__name__ + ': ' + str(error)})
                if failure is None:
                    failure = error
            self.report['status'] = 'failed' if failure is not None else 'passed'
            self.save()
        if failure is not None:
            raise failure
        return self.report


def step_count(value):
    number = int(value)
    if not 500 <= number <= 2000:
        raise argparse.ArgumentTypeError('steps must be between 500 and 2000')
    return number


def private_output(value):
    destination = Path(value).resolve()
    private_root = Path(__file__).resolve().parents[1] / '.local'
    if destination == private_root.resolve() or not destination.is_relative_to(private_root.resolve()):
        raise argparse.ArgumentTypeError('output must be a new private directory inside this checkout\'s ignored .local/')
    return destination


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='explicit USB port; otherwise exactly one 303a:1001 is required')
    parser.add_argument('--workload', required=True, choices=('warm', 'dense'))
    parser.add_argument('--steps', type=step_count, default=500,
                        help='programmatic/held-contact steps, 500..2000 (ten gestures stay fixed)')
    parser.add_argument('--output', required=True, type=private_output,
                        help='new private output directory under ignored .local/')
    args = parser.parse_args(argv)
    print(PRIVACY_WARNING, file=sys.stderr)
    print('Synthetic fixtures clear RAM history. Input is injected; no physical/photon latency is measured.', file=sys.stderr)
    args.output.mkdir(parents=True, exist_ok=False)
    with connected(args.port) as device:
        report = ScrollBenchmark(device, args.workload, args.steps, args.output / 'report.json').run()
    # Console summary deliberately omits full status and identity fields.
    print(json.dumps({'status': report['status'], 'workload': report['workload'],
                      'runs': [{key: run[key] for key in ('index', 'kind', 'seconds', 'physical_frames', 'frames', 'fps') if key in run}
                               for run in report['runs']]}))


if __name__ == '__main__':
    main()
