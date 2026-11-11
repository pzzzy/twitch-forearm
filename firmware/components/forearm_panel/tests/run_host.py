#!/usr/bin/env python3
"""Check pinned LVGL8 direct composition independently of the ESP32 hardware."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import re

here = Path(__file__).resolve().parent
firmware = here.parents[2]
lvgl = firmware / "managed_components/lvgl__lvgl"
output = here / "build"
output.mkdir(exist_ok=True)
touch_test = output / "touch-state-test"
subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I" + str(here.parent / "include"), str(here / "touch_state_test.cpp"),
                "-o", str(touch_test)], check=True)
subprocess.run([str(touch_test)], check=True)
cadence_test = output / "touch-sample-cadence-test"
subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I" + str(here.parent / "include"), str(here / "touch_sample_cadence_test.cpp"),
                "-o", str(cadence_test)], check=True)
subprocess.run([str(cadence_test)], check=True)
latency_test = output / "touch-frame-latency-test"
subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I" + str(here.parent / "include"), str(here / "touch_frame_latency_test.cpp"),
                "-o", str(latency_test)], check=True)
subprocess.run([str(latency_test)], check=True)
swap_test = output / "rgb565-transport-test"
subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I" + str(here.parent / "include"), str(here / "rgb565_transport_test.cpp"),
                "-o", str(swap_test)], check=True)
subprocess.run([str(swap_test)], check=True)
codegen_object = output / "rgb565-codegen.o"
subprocess.run(["c++", "-std=c++17", "-O2", "-fno-builtin-memcpy",
                "-I" + str(here.parent / "include"), "-c", str(here / "rgb565_codegen.cpp"),
                "-o", str(codegen_object)], check=True)
undefined = subprocess.check_output(["nm", "-u", str(codegen_object)], text=True)
assert not re.search(r"\b_?memcpy\b", undefined), "per-pixel conversion must inline fixed-size loads/stores"
print("RGB565 -fno-builtin-memcpy code generation: PASS (no memcpy call)")
flags = ["cc", "-std=c99", "-O1", "-DLV_CONF_INCLUDE_SIMPLE",
         "-I" + str(firmware / "main"), "-I" + str(lvgl)]


def build(source):
    target = output / (str(source.relative_to(lvgl / "src")).replace("/", "_") + ".o")
    subprocess.run(flags + ["-c", str(source), "-o", str(target)], check=True)
    return str(target)


with ThreadPoolExecutor(max_workers=8) as pool:
    objects = list(pool.map(build, (lvgl / "src").rglob("*.c")))
for filename in ["direct_mode_composition.c", "internal_strip_composition.c", "async_direct_ownership.c",
                 "render_profile_order.c"]:
    executable = output / Path(filename).stem
    subprocess.run(flags + [str(here / filename)] + objects +
                   ["-lm", "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
