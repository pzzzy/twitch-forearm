#!/usr/bin/env python3
"""Exercise the native UI with real pinned LVGL and a fake network callback.

No serial, Wi-Fi, Twitch API, or moderation service is used. The pointer driver
feeds complete contacts through LVGL; tests do not bypass confirmation APIs.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
project = here.parents[1]
lvgl = project / "firmware/managed_components/lvgl__lvgl"
output = here / "build"
output.mkdir(exist_ok=True)
flags = ["-O1", "-DLV_CONF_INCLUDE_SIMPLE", "-I" + str(project / "firmware/main"),
         "-I" + str(lvgl), "-I" + str(project / "firmware/components/forearm_fonts/include")]


def build(source):
    target = output / (str(source.relative_to(lvgl / "src")).replace("/", "_") + ".o")
    subprocess.run(["cc", "-std=c99"] + flags + ["-c", str(source), "-o", str(target)], check=True)
    return str(target)


with ThreadPoolExecutor(max_workers=8) as pool:
    objects = list(pool.map(build, (lvgl / "src").rglob("*.c")))
for source in (project / "firmware/components/forearm_fonts").glob("*.c"):
    target = output / ("font_" + source.name + ".o")
    subprocess.run(["cc", "-std=c99"] + flags + ["-c", str(source), "-o", str(target)], check=True)
    objects.append(str(target))
for source, extra in [("moderation_ui_test.cpp", []),
                      ("inline_layout_test.cpp", []),
                      ("text_scale_test.cpp", []),
                      ("backdrop_pixels_test.cpp", ["-DFOREARM_UI_TEST_HEAP"])]:
    executable = output / Path(source).stem
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror"] + flags + extra +
                   ["-I" + str(here / "include"), "-I" + str(project / "core/include"),
                    str(here / source), str(project / "firmware/main/app_ui.cpp")] +
                   objects + ["-lm", "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
