#!/usr/bin/env python3
"""Build isolated pinned-LVGL startup lifecycle/render checks; no hardware/network."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
project = here.parents[1]
lvgl = project / 'firmware/managed_components/lvgl__lvgl'
output = project / '.local/startup-animation-host'
output.mkdir(parents=True, exist_ok=True)
flags = ['-O1', '-DLV_CONF_INCLUDE_SIMPLE', '-I' + str(here / 'include'),
         '-I' + str(project / 'firmware/main'), '-I' + str(lvgl)]

def build(source):
    destination = output / (str(source.relative_to(lvgl / 'src')).replace('/', '_') + '.o')
    subprocess.run(['cc', '-std=c99', *flags, '-c', str(source), '-o', str(destination)], check=True)
    return str(destination)

with ThreadPoolExecutor(max_workers=4) as pool:
    objects = list(pool.map(build, (lvgl / 'src').rglob('*.c')))
executable = output / 'startup_animation_test'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                str(here / 'startup_animation_test.cpp'), str(project / 'firmware/main/startup_animation.cpp'),
                *objects, '-lm', '-o', str(executable)], check=True)
subprocess.run([str(executable), str(output)], check=True)
