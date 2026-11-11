#!/usr/bin/env python3
"""Exercise UI fixtures over USB, clearing RAM history. Injection is not physical-touch proof."""
import argparse
import json
from pathlib import Path
import time
from device import connected

ANONYMOUS_LOGIN = 'anonymousviewer_name_'


def check(condition, reason):
    if not condition:
        raise AssertionError(reason)


def text_fixtures():
    return [
        ('max-ascii', {'name': ANONYMOUS_LOGIN, 'user_login': ANONYMOUS_LOGIN,
                       'text': ('SYNTHETIC ' + 'readable ' * 64)[:500]}),
        ('max-emoji', {'name': ANONYMOUS_LOGIN, 'user_login': ANONYMOUS_LOGIN, 'text': '😀' * 500}),
        ('mixed-unicode', {'name': ANONYMOUS_LOGIN, 'user_login': ANONYMOUS_LOGIN,
                           'text': 'SYNTHETIC Hello 👋 😀 😂 😭 🔥 💜 👍 ✨ 🌍 🎉 🤔 🥹 ❤️ 🚀 🦄 — café naïve résumé'}),
        ('max-display-name', {'name': '😀' * 25, 'user_login': ANONYMOUS_LOGIN,
                              'text': 'SYNTHETIC maximum sender name: 25 Unicode codepoints / 100 UTF-8 bytes.'}),
    ]


def fallback_fixtures():
    base = {'name': '中' * 25, 'user_login': ANONYMOUS_LOGIN,
            'text': 'SYNTHETIC sender fallback must preserve exact attribution.'}
    return base, {**base, 'name': ANONYMOUS_LOGIN}


def positioned_emote_fixture():
    prefix = 'SYNTHETIC 😀 Kappa literal; '
    return {'name': ANONYMOUS_LOGIN, 'user_login': ANONYMOUS_LOGIN,
            'text': prefix + 'Kappa' + ' / Kappa literal.',
            'emotes': [{'id': '25', 'text': 'Kappa', 'byte_offset': len(prefix.encode('utf-8')),
                        'byte_length': 5, 'position_valid': True}]}


def capacity_emote_fixture():
    prefix = 'SYNTHETIC '
    occurrences = []
    for index in range(10):
        occurrences.append({'id': '25', 'text': 'Kappa', 'byte_offset': len(prefix.encode('utf-8')) + index * 6,
                            'byte_length': 5, 'position_valid': True})
    return {'name': ANONYMOUS_LOGIN, 'user_login': ANONYMOUS_LOGIN,
            'text': prefix + ' '.join(['Kappa'] * 10), 'emotes': occurrences}


def images_equal(first, second):
    from PIL import Image, ImageChops
    with Image.open(first) as a, Image.open(second) as b:
        return a.size == b.size and ImageChops.difference(a.convert('RGB'), b.convert('RGB')).getbbox() is None


def run_checks(d, output, report):
    def state(label):
        value = d.command('status')
        report['samples'].append({'label': label, **value})
        return value

    def clear_fixture():
        d.command('fixture_mode', enabled=0)
        d.command('fixture_mode', enabled=1)
        d.command('live')

    def append_fixture(label, fixture):
        before = state(label + '-before')
        d.command('message', **fixture)
        time.sleep(.2)
        after = state(label + '-after')
        check(after['ui']['appended'] > before['ui']['appended'], 'Fixture not retained: ' + label)
        check(after['ui']['rejected'] == before['ui']['rejected'], 'Fixture rejected: ' + label)
        return after

    initial = state('initial')
    check(initial['board']['touch_present'], 'Touch controller did not respond')
    check(initial['board']['flush_errors'] == 0, 'Panel flush failure')
    initial_fixture = initial['ui'].get('fixture_mode', False)
    initial_paused = initial['ui']['paused']
    try:
        clear_fixture()
        check(state('fixture-entry')['ui'].get('fixture_mode') is True, 'Explicit synthetic fixture mode did not activate')
        d.command('burst', count=50)
        time.sleep(.15)
        d.command('pause'); d.command('scroll', pixels=-350)
        before = state('paused-before-arrivals')
        d.command('burst', count=25)
        time.sleep(.2)
        after = state('paused-after-arrivals')
        for key in ('anchor_sequence', 'anchor_offset', 'scroll_y'):
            check(after['ui'][key] == before['ui'][key], 'Paused ' + key + ' moved on arrival')
        check(after['ui']['paused'], 'Paused history resumed unexpectedly')
        check(after['ui']['unread'] - before['ui']['unread'] == after['ui']['appended'] - before['ui']['appended'],
              'Unread count differs from new arrivals')
        report['checks']['pause_anchor_and_unread'] = True
        idle = 0
        for index in range(20):
            d.command('pause'); before = state('go-live-before-' + str(index))
            d.gesture([(282, 445)], step=.075)
            time.sleep(.09)
            after = state('go-live-after-' + str(index))
            check(not after['ui']['paused'], 'Go live did not remain live after one tap at cycle ' + str(index))
            if before['ui']['appended'] == after['ui']['appended']:
                idle += 1
        check(idle >= 15, 'Too few Go live checks without incoming messages')
        report['checks']['go_live_twenty_single_taps'] = True
        report['checks']['go_live_without_new_messages_cycles'] = idle
        d.command('live'); before = state('gesture-before')
        d.gesture([(150, y) for y in range(145, 370, 12)], step=.035)
        time.sleep(.15); after = state('gesture-after')
        check(after['ui']['paused'] and after['ui']['scroll_y'] < before['ui']['scroll_y'], 'Drag did not pause and scroll back')
        check(not after['ui']['selection_visible'], 'Drag opened message selection')
        report['checks']['drag_pauses_and_scrolls_without_selecting'] = True

        for label, fixture in text_fixtures():
            clear_fixture()
            append_fixture(label, fixture)
            report['checks'][label] = {'characters': len(fixture['text']), 'utf8_bytes': len(fixture['text'].encode()),
                'display_name_codepoints': len(fixture['name']), 'display_name_utf8_bytes': len(fixture['name'].encode()), 'accepted': True}
            d.screen(output.parent / ('native-' + label + '.png'))

        unsupported, reference = fallback_fixtures()
        captures = []
        for label, fixture in [('unsupported-name', unsupported), ('ascii-login-reference', reference)]:
            clear_fixture(); append_fixture(label, fixture)
            capture = output.parent / ('native-' + label + '.png')
            d.screen(capture); captures.append(capture)
        check(images_equal(*captures), 'Unsupported sender did not pixel-match its exact ASCII login fallback')
        report['checks']['unsupported_sender_exact_login_fallback'] = {'pixel_identical': True, 'login_bytes': len(ANONYMOUS_LOGIN)}

        clear_fixture()
        unpositioned = {'name': ANONYMOUS_LOGIN, 'user_login': ANONYMOUS_LOGIN, 'text': 'SYNTHETIC Kappa remains literal.',
                        'emotes': [{'id': '25', 'text': 'Kappa'}]}
        unpositioned_state = append_fixture('unpositioned-emote', unpositioned)
        check(unpositioned_state['ui'].get('visible_emote_images', 0) == 0, 'Unpositioned token was replaced by an image')
        report['checks']['missing_emote_positions_remain_literal'] = True
        clear_fixture()
        append_fixture('positioned-emote', positioned_emote_fixture())
        deadline = time.monotonic() + 20
        while True:
            emote_state = state('emote-cache')
            if emote_state.get('emotes', {}).get('ready', 0) > 0 and emote_state['ui'].get('visible_emote_images', 0) == 1:
                break
            check(time.monotonic() < deadline, 'Positioned static emote did not download/decode/render exactly once')
            time.sleep(.5)
        report['checks']['positioned_static_emote'] = {'ready': emote_state['emotes']['ready'],
            'downloads': emote_state['emotes']['downloads'], 'visible_images': 1, 'literal_duplicates': 2}
        d.screen(output.parent / 'native-emote.png')
        clear_fixture()
        capped = append_fixture('emote-occurrence-cap', capacity_emote_fixture())
        check(capped['ui'].get('visible_emote_images', 0) == 8, 'Emote occurrence cap did not render exactly eight positioned images')
        report['checks']['positioned_emote_occurrence_cap'] = {'provided': 10, 'rendered': 8, 'literal_overflow': 2}
        d.screen(output.parent / 'native-emote-cap.png')

        clear_fixture(); d.command('burst', count=100); time.sleep(.1)
        start = state('scroll-load-start'); begun = time.monotonic()
        for count in range(10):
            d.gesture([(150, y) for y in (range(140, 371, 16) if count % 2 == 0 else range(370, 139, -16))], step=.018)
        duration = time.monotonic() - begun; end = state('scroll-load-end')
        report['checks']['scroll_load'] = {'duration_seconds': duration, 'presented_frames': end['board']['frames'] - start['board']['frames'],
            'observed_frames_per_second': (end['board']['frames'] - start['board']['frames']) / duration,
            'ui_frame_p95_us': end.get('ui_frame_p95_us'), 'flush_p95_us': end['board']['recent_frame_p95_us']}
        check(end['board']['flush_errors'] == 0 and end['board']['touch_errors'] == 0, 'Panel/I2C errors during scroll load')
        check(end['twitch']['moderation_requests'] == initial['twitch']['moderation_requests'], 'Unexpected moderation request during UI-only fixtures')
        report['checks']['no_new_moderation_requests'] = True
        d.command('live'); d.screen(output.parent / 'native-chat.png')
        report['final'] = state('final')
    finally:
        failures = []
        for command, fields in [('touch', {'x': 0, 'y': 0, 'pressed': 0, 'enabled': 0}),
                                 ('fixture_mode', {'enabled': int(initial_fixture)}), ('pause' if initial_paused else 'live', {})]:
            try:
                d.command(command, **fields)
            except Exception:
                failures.append(command)
        check(not failures, 'Could not confirm fixture cleanup: ' + ', '.join(failures))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    report = {'physical_touch_proof': False, 'input_injected': True,
              'input_method': 'explicit LVGL diagnostic injection; controller reads remain separate', 'checks': {}, 'samples': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with connected(args.port) as device:
            run_checks(device, args.output, report)
        report['passed'] = True
    except Exception as error:
        report['passed'] = False; report['error'] = str(error)
        raise
    finally:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': report['passed'], 'checks': report['checks']}, indent=2))


if __name__ == '__main__':
    main()
