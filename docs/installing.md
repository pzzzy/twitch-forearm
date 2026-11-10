# Installing the hardware beta

This package targets **JC3248W535, ESP32-S3, 16 MB flash / 8 MB PSRAM, 320 × 480 portrait**. Match the actual board to the project's hardware reference before flashing. Purchase reference: https://www.aliexpress.us/item/3256807380017698.html. Variants and other ESP32 display boards are not interchangeable.

This is a hardware beta. Battery, charging, skin temperature, runtime and forearm mounting are **not qualified**. Use USB power for initial testing. Read the version's release notes for measured performance and known limits. The app does not mount or format microSD and does not program security eFuses.

## Verify and prepare

Download the ZIP and its `.sha256` file from the same project GitHub release. Compare the ZIP checksum before opening it (`shasum -a256 -c FILE.zip.sha256` on macOS; `sha256sum -c FILE.zip.sha256` on Linux). After unpacking, check `SHA256SUMS` inside the package too. Checksums detect changed files; obtain them from the trusted project release.

Use Python 3.11 or newer and the pinned ESP-IDF **5.3.6** development environment, which includes esptool **4.13.0**. Close serial monitors and other device tools. Identify the ESP32-S3's serial port and set the shell variable `FOREARM_PORT` to that port. The examples below run from the unpacked package directory; all arguments are public paths, not credentials.

## First installation from factory firmware

First installation replaces the vendor application and partition layout. Capture a **private, per-device 16 MiB flash backup** before writing:

```sh
python -m esptool --chip esp32s3 --port "$FOREARM_PORT" --after no_reset read_flash 0 0x1000000 factory-backup.bin
python -m esptool --chip esp32s3 --port "$FOREARM_PORT" --before no_reset verify_flash 0 factory-backup.bin
```

The read holds the board in the bootloader until verification so the factory app cannot change NVS between those operations. Keep this image private: a factory or user image can contain credentials. It is not a release artifact and is not transferable to another unit. The backup excludes microSD. Record its SHA-256 and keep it somewhere separate from this package.

The new default/configuration and token NVS areas can overlap factory application bytes. **Only for the first migration**, after verifying the backup, initialize the project's NVS region:

```sh
python -m esptool --chip esp32s3 --port "$FOREARM_PORT" erase_region 0x9000 0x19000
python -m esptool --chip esp32s3 --port "$FOREARM_PORT" write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 images/bootloader.bin 0x8000 images/partition-table.bin 0xf000 images/ota_data_initial.bin 0x30000 images/forearm_chat.bin
```

That erase clears the new project configuration/token/PHY/OTA selector area; it does not preserve the factory application. Do not run it for an ordinary paired-device update. Firmware reports unknown NVS initialization failures instead of silently erasing stored data.

## Updating an existing Forearm Chat device

For this unchanged partition layout, an application-only update preserves Wi-Fi and saved Twitch login:

```sh
python -m esptool --chip esp32s3 --port "$FOREARM_PORT" write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x30000 images/forearm_chat.bin
```

Do not erase NVS or write a complete flash dump. Follow separate migration instructions if a future release changes partitions. Reserved OTA slots do not provide an OTA update service.

## Configure and test

Clone the source at the package's `manifest.json` commit. In a Python 3.11+ virtual environment, install `requirements-tools.txt`, ensure Node.js 22+ is available, then run:

```sh
python tools/device.py --port "$FOREARM_PORT" setup
```

Open the printed loopback URL. Enter 2.4 GHz Wi-Fi/hotspot credentials, your own Public Twitch Client ID and channel. Stop the helper after setup. Wi-Fi credentials and OAuth tokens are stored only on the device in unencrypted flash. Use the displayed Twitch link/QR to authorize; no Twitch password or Client Secret belongs in the form or terminal.

Check readable output and touch alignment, pause, scroll, Go live and message → Back before use. Moderation must show the correct selected identity and a separate confirmation. Use an owned disposable account for tests and undo any test ban/timeout. Do not treat an ambiguous network result as confirmed failure or resend it automatically.

## Recovery

Reconnect over USB, close other serial clients, and use the board's documented BOOT/reset procedure if normal flashing is unavailable. To restore the complete original device image, verify its saved checksum and flash **that device's own** verified 16 MiB backup at offset `0x0`. This replaces the project layout and removes its saved setup. Never restore someone else's image or publish your backup. If recovery fails, stop before changing eFuses or formatting microSD and seek board-specific help.
