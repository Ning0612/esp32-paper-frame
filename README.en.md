# PaperFrame

[English](README.en.md) | [繁體中文](README.md)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-6.1.19-orange?logo=platformio)](https://platformio.org)
[![Board](https://img.shields.io/badge/Board-ESP32--S3--N16R8-red?logo=espressif)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Version](https://img.shields.io/badge/Version-0.11.0-yellow)](https://github.com/Ning0612/esp32-paper-frame/releases)
[![CI](https://github.com/Ning0612/esp32-paper-frame/actions/workflows/ci.yml/badge.svg)](https://github.com/Ning0612/esp32-paper-frame/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

PaperFrame is an offline-first photo carousel built on an ESP32-S3 driving a
7.3" 800×480 six-color (E6) e-paper panel. The firmware is native ESP-IDF
managed through PlatformIO, with no Arduino framework dependency; image
processing, configuration, and the management WebUI all work entirely within
a local network or the device's own access point.

![PaperFrame in a 3D-printed enclosure: a 7.3-inch six-color e-paper panel, with a status bar showing date, weather, IP address, and indoor temperature/humidity, and Hokusai's "The Great Wave off Kanagawa" dithered to six colors below it](docs/media/device-front.jpg)

The photo above is the real device, unretouched, with only the LAN address in
the status bar masked to `192.168.x.x`. The status bar reads, left to right:
date and weekday, current weather, the device's IP address, and indoor
temperature/humidity from the DHT22; below it is the carousel image, here the
public-domain "The Great Wave off Kanagawa" by Hokusai after six-color
dithering ([provenance](ASSET_CREDITS.md)). The enclosure CAD is included
under [`hardware/enclosure/`](hardware/enclosure/) and also ships as an asset
on every release.

> **Relationship to other e-paper projects**
>
> PaperFrame is a **separately maintained, independent project** from
> [epaper-home-display](https://github.com/Ning0612/epaper-home-display)
> (a Raspberry Pi multi-info smart-home panel) and
> [pico-paper-clock](https://github.com/Ning0612/pico-paper-clock)
> (a Pico W/MicroPython desktop clock) — different hardware platforms,
> different firmware stacks, different purposes, with no overlap or
> replacement relationship between them. This project's focus is an
> offline-first, full-color photo carousel on ESP32-S3.
>
> Its design borrows lessons learned from both of those projects and from
> [esp32-hydracup](https://github.com/Ning0612/esp32-hydracup) (low-memory
> streaming, transactional file updates, FreeRTOS ownership boundaries), with
> the exact upstream versions referenced pinned in
> [References & Licensing](docs/REFERENCES.md).

## Management console

Every WebUI asset (HTML/CSS/JS) is gzipped and compiled into the app
firmware — nothing is fetched from a CDN — so a single OTA update ships the
firmware and the frontend together
([ADR-0016](docs/adr/0016-embed-webui-assets-in-firmware.md)). **The one
exception is the map picker on the weather page**: when online, it fetches
tiles from `tile.openstreetmap.org` (which exposes the approximate
coordinates you pick to that service); when offline, the same control
falls back to a purely offline latitude/longitude grid. Every other page
needs no external network at all. The screenshots below were taken from a
device running v0.10.0. The console itself now also offers a Traditional
Chinese/English language toggle in the top bar.

| Overview | Environment & Presence |
| --- | --- |
| [![Dashboard](docs/media/webui-dashboard.png)](docs/media/webui-dashboard.png) | [![Environment & Presence](docs/media/webui-sensors.png)](docs/media/webui-sensors.png) |
| Panel status, carousel progress, capacity, and every service's health, all sourced from a single runtime snapshot. | Each of the two photoresistor channels has its own threshold and live reading, with the deciding channel called out. |

| Image processing | System & firmware |
| --- | --- |
| [![Image processing](docs/media/webui-images.png)](docs/media/webui-images.png) | [![System](docs/media/webui-system.png)](docs/media/webui-system.png) |
| Orientation, cropping, six-color quantization, and PFR1 packing all happen locally in the browser; the original image never leaves the machine. The sample image is the public-domain "The Great Wave off Kanagawa" by Hokusai ([provenance](ASSET_CREDITS.md)). | Panel and network status, capacity and version, OTA updates, admin password reset, and diagnostic events. |

[![Weather & Time](docs/media/webui-weather.png)](docs/media/webui-weather.png)

The Weather & Time page: OpenWeatherMap coordinates can be picked directly by
dragging the map; offline, the same control switches to a latitude/longitude
grid. The API key is only ever reported as "set or not" — it never appears in
any response or on screen. The coordinates in the screenshot are Taipei, not
an actual deployment location.

## Current status

The single source of truth for current status is
[Project Status](docs/PROJECT_STATUS.md), with hardware evidence in the
[Hardware Validation Log](docs/hardware/VALIDATION.md). Summary:

| Category | Current state |
| --- | --- |
| MVP | **Feature scope is complete** and in real-world use. **The power supply design is not yet done** — the device is currently hard-wired; battery/low-power operation has not been evaluated. |
| Complete | Phase 1–8 code, host tests, and firmware builds are all complete. |
| Validated on hardware | The **primary** paths of Phase 2–8 all have on-device evidence: panel refresh and sleep current, AP/STA provisioning and access boundaries, the browser image pipeline, five power-loss paths, all four weather failure classifications, end-to-end OTA plus rollback fault injection, and Phase 7's DHT22, dual-photoresistor channels, and presence detection. This doesn't mean every Phase has zero open items — see the next row. |
| Not yet validated | Four areas, eight paths, all low priority: the `SensorSettings` v1→v2 on-device migration and floating-pin behavior of an enabled-but-unwired channel; the presence-return redraw, address redraw after DHCP renewal, and the welcome-refresh retry-on-failure path; the AP-grace presence exception and the low-DMA-heap guard; and `pf_config` open failure when NVS is full. |
| Not yet decided | The production security profile (Secure Boot/Flash Encryption) and any post-MVP P1 features have not entered development. |

2026-08-23 fixed two related defects: when a refresh completed but the panel
failed to sleep, the outcome read as "the refresh failed," causing an
already-correct frame to be redrawn; and every boot's presence convergence
from `unknown` to `present` was being treated as "returning from away,"
wasting a ~31-second full refresh. See
[ADR-0019](docs/adr/0019-separate-frame-displayed-from-panel-slept.md) for
the fix. The corrected boot sequence **has been confirmed on hardware to
spend only one refresh**.

"On-device evidence is closed" describes behavior that has been verified on
real hardware — it does **not** mean the release gate is closed. The
[release checklist](docs/RELEASE_CHECKLIST.md)'s manual on-device checks
still have to run for every release, per the state of the device in hand.
Secure Boot and Flash Encryption are also not yet enabled, so this **is not**
a product you should put directly on an untrusted network; it's designed for
a trusted LAN or the device's own AP.

## Features

The parenthetical after each item is its **on-device validation status**.
Item-by-item evidence lives in [Project Status](docs/PROJECT_STATUS.md) and
the [Hardware Validation Log](docs/hardware/VALIDATION.md).

- **Offline-first photo carousel**: fully operable within a LAN or the
  device's own AP, with no dependency on an external CDN or backend
  (validated, including long-running carousels). The weather feature itself
  needs OpenWeatherMap, and the map picker needs OSM tiles — both are
  optional and can be disabled without affecting the carousel
- **PFR1, a purpose-built image format**: quantization and packing happen in
  the browser; the device only decodes and displays, avoiding heavyweight
  image processing on the MCU (validated: browser-side generation, upload,
  and download)
- **Transactional imagefs storage**: the image partition is separate from
  the firmware partition, so OTA updates never wipe existing images
  (validated, including fault injection across five power-loss paths)
- **Management WebUI**: Dashboard, image management, Wi-Fi, weather,
  environment, and System diagnostics pages, now with a **Traditional
  Chinese/English language toggle** (validated: real browser operation
  on-device)
- **Provisioning and authentication**: AP-portal provisioning with automatic
  fallback to an AP when NVS is blank or STA retries are exhausted;
  PBKDF2 with 10,000 iterations, sessions, and CSRF protection (validated:
  access boundaries and auth boundaries)
- **OTA updates with rollback**: sourced from GitHub Releases, with A/B
  partitions and a rollback-confirmation mechanism (validated: end-to-end
  update and rollback fault injection)
- **Weather information**: parsing, caching, and status-bar display
  (validated: real SNTP and all four failure classifications)
- **Optional environmental sensing**: DHT22 temperature/humidity and two
  independent photoresistor channels. Darkness is only declared once both
  read below their own threshold; either one seeing light wakes the device.
  Includes moving-average filtering, away/return debounce, and an away blank
  screen (validated: readings, degraded operation when unplugged, dual-channel
  calibration, AND semantics, and 180/30-second debounce timing)

## Documentation entry point

Start at [Documentation Index](docs/README.md); if you just want to know
where things currently stand, read [Project Status](docs/PROJECT_STATUS.md).
The authority relationship between ADRs, the current contract, hardware
evidence, and historical plans is all explained at that entry point — you
don't need to read every document from the start.

## Quick start (open source)

The development environment needs Windows 11, Python 3.13, PlatformIO Core
6.1.19, and native ESP-IDF. From a clean checkout:

```powershell
uv venv --seed --python 3.13 .venv
uv pip install --python .\.venv\Scripts\python.exe -r requirements-dev.txt
.\.venv\Scripts\pio.exe test -e native
.\.venv\Scripts\pio.exe run
```

`native` is the host test suite, which needs no hardware. Firmware upload,
the panel, Wi-Fi, NVS, OTA, and power-loss recovery are covered by
[Flashing](docs/hardware/FLASHING.md), the
[Hardware Validation Log](docs/hardware/VALIDATION.md), and the
[release checklist](docs/RELEASE_CHECKLIST.md). Don't treat a passing
host/build result as on-device validation.

**On a brand-new board, `pio run -t upload` alone won't get it booting**:
day-to-day uploads only write the app slot, not the bootloader. First-time
flashing and bootloader updates are covered in
[First flash of a new device](docs/hardware/FLASHING.md#新裝置首次燒錄含-bootloader);
OTA rollback protection depends on the bootloader version — how to check is
covered in
[Does the bootloader have rollback protection](docs/hardware/FLASHING.md#bootloader-是否具備回滾保護).

## Hardware

The full parts list, wiring, and pin constraints are in
[Hardware & Wiring](docs/hardware/HARDWARE.md); the reasoning behind the pin
assignments is pinned in
[ADR-0003](docs/adr/0003-fix-phase2-display-integration.md). The table below
is a quick-reference summary.

| Category | Component | Status |
| --- | --- | --- |
| MCU | ESP32-S3-N16R8 (16 MB flash, 8 MB octal PSRAM) | Required |
| Display | Waveshare 7.3" e-Paper HAT (E), 800×480 six-color (E6) | Required |
| Temperature/humidity | DHT22/AM2302 | Optional; validated on hardware |
| Light sensing | Two photoresistor + voltage-divider circuits, on `ADC1_CH4` and `ADC1_CH6` | Optional; validated on hardware with both wired. **If only one is wired, the other must be disabled in the WebUI** ([why](docs/hardware/HARDWARE.md)) |
| Enclosure | 3D-printed; CAD in [`hardware/enclosure/`](hardware/enclosure/) | Optional |
| Power | Direct wired power (dev board USB) | **Not yet designed**; battery/low-power operation not evaluated |

Display wiring (3.3 V logic, SPI2, mode 0, MSB-first, 2 MHz starting clock):

| HAT | DIN | CLK | CS | DC | RST | BUSY |
| --- | --- | --- | --- | --- | --- | --- |
| ESP32-S3 | GPIO11 | GPIO12 | GPIO10 | GPIO13 | GPIO14 | GPIO4 |

Sensor pins: light-sensor ADC channel 1 = GPIO5 (`ADC1_CH4`), DHT data =
GPIO6, light-sensor ADC channel 2 = GPIO7 (`ADC1_CH6`); GPIO8/GPIO9 are
reserved for a future I²C bus. Both light-sensor channels must be on ADC1 —
ADC2 is claimed by the Wi-Fi driver whenever Wi-Fi is active. The
voltage-divider wiring, polarity, and calibration procedure are in
[Hardware & Wiring](docs/hardware/HARDWARE.md); the merge/decision rule is in
[ADR-0018](docs/adr/0018-dual-photoresistor-channels.md). **Read ADR-0003's
pin constraints before changing any wiring** — octal PSRAM occupies
GPIO33–37, native USB occupies GPIO19–20, GPIO0/3/45/46 are strapping pins,
and GPIO4 is already committed to BUSY and must not be reused as an ADC pin.

### Enclosure

[`hardware/enclosure/`](hardware/enclosure/) contains a desktop photo-frame
enclosure; every part ships as both **STEP (AP214)** and **STL**: STEP for
resizing or re-exporting, STL to send straight to a slicer. The parts list,
measured mounting-frame dimensions, and known issues are in
[that directory's README](hardware/enclosure/README.md). The same content
also ships as `paperframe-enclosure.zip` on every GitHub Release's assets.

The six parts' filename prefixes are the assembly-stack order — printing and
assembly can just follow the numbers.

## Architecture overview

The firmware is split into 12 ESP-IDF components; `src/app_main.cpp` only
handles startup and wiring. Cross-component coordination is centralized in
`pf_runtime`'s `RuntimeCoordinator`: display, storage, network, and OTA each
have a clear owner and communicate by message rather than sharing mutable
state, so network or storage I/O never blocks a panel refresh.

Each component's design rationale and trade-offs are pinned in the
[ADRs](docs/adr/README.md); this section is just a map.

| Component | Responsibility |
| --- | --- |
| `pf_runtime` | `RuntimeCoordinator`, runtime snapshot, diagnostics events, reboot reason, firmware version |
| `pf_display` | epd7in3e driver (SPI2), DisplayTask, owner contract |
| `pf_carousel` | Carousel scheduling, image frames, and the welcome frame |
| `pf_image` | PFR1 format decoding |
| `pf_storage` | LittleFS backend, image store, catalog, transactions and recovery, storage worker |
| `pf_network` | AP/STA state machine, provisioning service, scan results, SNTP time sync |
| `pf_web` | HTTP server, Dashboard/health serializer, per-feature config forms, access policy |
| `pf_auth` | PBKDF2 password verification, sessions, and CSRF |
| `pf_config` | NVS config schema, credentials, and secure memory |
| `pf_ota` | GitHub Releases download, A/B partitions, rollback confirmation |
| `pf_weather` | Weather parsing, caching, and worker |
| `pf_sensors` | DHT22 and photoresistor ADC drivers, filtering, and presence detection |

Tests are split into two layers: `test/` is the host test suite that needs no
hardware (`pio test -e native`), and `test_embedded/` is the on-target suite
that does. **A passing host test is not the same as on-device validation** —
see the [Hardware Validation Log](docs/hardware/VALIDATION.md) for where that
line sits and the current state on each side of it.

## Public baseline and known limitations

- The target baseline is an ESP32-S3-N16R8 with 16 MB flash, 8 MB octal
  PSRAM, and a 7.3" 800×480 six-color (E6) e-paper panel; the light and
  temperature/humidity sensors are optional peripherals (see the hardware
  section above).
- The WebUI and image management are offline-first by design: every frontend
  asset is compiled into the firmware, and image processing happens locally
  in the browser. The only things that ever reach outside the LAN are the
  weather page's map picker (OSM tiles) and the weather fetch itself — both
  can be disabled, after which the whole feature set still works on a LAN
  with no internet access.
- The e-paper panel's full refresh defaults to every 30 minutes, adjustable
  from 10 minutes to 24 hours (1,440 minutes); a measured full refresh takes
  about 31 seconds, and the panel sleeps after every refresh. That 31 seconds
  is a physical property of the E6 panel, not a tunable software delay.
- Secure Boot and Flash Encryption are not enabled, so this is only suitable
  for a trusted LAN or the device's own AP.
- Eight low-priority on-device validation paths remain open (see Current
  status above).
- Real device test data, device-identifying information, and runtime imagefs
  contents are not part of this public repository.

## Development baseline

- Windows 11 / PowerShell 7
- [uv](https://docs.astral.sh/uv/getting-started/installation/) 0.11.26
- Python 3.13 project venv
- PlatformIO Core 6.1.19
- PlatformIO Espressif 32 platform (version pinned by the Phase 1 build)
- Native ESP-IDF

This is the combination actually used and confirmed to build.
**Linux and macOS are untested** — the toolchain itself is cross-platform,
but this project's command examples, `uv` paths, and flashing workflow are
all written for Windows; other platforms will need their own adjustments. CI
runs the host tests and firmware build on `ubuntu-latest`, so the code itself
compiles on Linux — what's untested is the Windows-oriented workflow this
document describes.

Set up the tooling environment:

```powershell
uv venv --seed --python 3.13 .venv
uv pip install --python .\.venv\Scripts\python.exe -r requirements-dev.txt
.\.venv\Scripts\pio.exe --version
```

Standard firmware build, single-command native-USB upload, and port
enumeration:

```powershell
.\.venv\Scripts\pio.exe run
.\.venv\Scripts\pio.exe run -e paperframe-s3 -t upload
.\.venv\Scripts\pio.exe device list
```

A standard upload auto-selects the ESP32-S3's native USB by VID:PID
`303A:1001`, enters the ROM via `usb_reset`, reads `otadata`, updates whichever
OTA app slot is currently booted, and resets automatically when done. Holding
GPIO0 and GPIO46 low during reset is only needed for the very first flash, an
RGB demo, or when a broken app can't auto-reset. The full safe procedure,
the app-only-slot limitation, and how to restore normal operation afterward
are in [ESP32-S3 Flashing](docs/hardware/FLASHING.md).

This path **does not update the bootloader or the partition table** — both a
new board's first flash and updating an existing device's bootloader to gain
OTA rollback protection are covered in the same document's
[First flash of a new device](docs/hardware/FLASHING.md#新裝置首次燒錄含-bootloader)
section.

## Documentation

- [Documentation Index](docs/README.md)
- [Project Status](docs/PROJECT_STATUS.md)
- [Architecture Decisions](docs/adr/README.md)
- [Hardware & Wiring](docs/hardware/HARDWARE.md)
- [Hardware Validation Log](docs/hardware/VALIDATION.md)
- [Enclosure CAD](hardware/enclosure/README.md)
- [References & Licensing](docs/REFERENCES.md)
- [Historical Requirements Draft](docs/archive/Guild.md)

## License

This project is licensed under the [MIT License](LICENSE). Referenced
projects and any future third-party dependencies remain separately subject
to their own licenses; referencing a concept doesn't exempt this project from
source attribution or third-party notices. The firmware's built-in weather
icons are redrawn from erikflowers/weather-icons (SIL OFL-1.1); attribution
is in [ASSET_CREDITS.md](ASSET_CREDITS.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
