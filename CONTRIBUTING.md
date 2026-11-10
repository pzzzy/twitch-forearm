# Contributing

Forearm Chat is a development preview for the JC3248W535 board family. Check the current [implementation plan](docs/implementation-plan.md) before starting a larger change. Keep fixes focused and describe the user-visible behavior and validation in the PR.

## Local checks

Use the pinned ESP-IDF environment for firmware, Python 3.11 or newer for host tools, Node.js 22 or newer, and a C/C++ compiler with CMake. Install host requirements in a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-tools.txt
cmake -S core -B build/core -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core
ctest --test-dir build/core --output-on-failure
.venv/bin/python -m unittest discover -s tools/tests -v
.venv/bin/python -m unittest discover -s tools/tests_licenses -v
node --check tools/setup.mjs
idf.py -C firmware build
sh firmware/components/forearm_fonts/tools/check-font.sh
python3 firmware/components/forearm_panel/tests/run_host.py
python3 tools/tests_ui/run_host.py
python3 tools/check_public_tree.py
```

The font and composition checks use the pinned LVGL dependency resolved by the firmware build. An offline firmware build can be checked separately with `idf.py -C firmware -B build-offline -DFOREARM_NETWORK=OFF build`. No account or hardware is required for these build/host checks. Device replay requires a deliberately configured demo session; it must not be confused with physical touch validation.

## Hardware and interaction changes

Record the board revision, orientation, firmware revision and test method. Distinguish real finger interaction, injected input, framebuffer captures and photographs. Report frame rate together with workload and measurement interval. A clean build does not establish display timing, touch alignment or battery safety.

Keep LVGL on its UI owner, network work off the UI thread, memory bounded, and selection tied to immutable message/user IDs. Driver changes must preserve the panel's complete-frame transfer protocol unless a replacement has been qualified. Preserve upstream copyright/license notices and document modifications.

Do not enable real moderation as an incidental change. It needs the intended permission grant, verified target identities, deliberate confirmation and explicit validation against a chosen test target. Never send real chat or moderation actions as an unattended test.

## Privacy and collaboration

Put local configuration, credentials, flash backups and real chat recordings under ignored `.local/`. Never add Wi-Fi passwords, OAuth tokens, client secrets, screenshots containing private chat, device identities or personal host paths to a commit or issue. A Public Twitch Client ID is not a secret, but contributors must provision their own application rather than hardcode one.

Use [the security policy](SECURITY.md) for vulnerabilities. Be respectful, assume good intent and keep discussion about the work. Contributions to original project code are submitted under Apache-2.0; third-party additions need compatible, explicit provenance and license notices. Font assets retain their font licenses.
