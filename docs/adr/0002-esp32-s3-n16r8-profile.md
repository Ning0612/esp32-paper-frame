# ADR-0002：固定 ESP32-S3-N16R8 開發 profile

- Status: accepted
- Date: 2026-07-29
- Supersedes: none

## Context

使用者確認模組標示為 `ESP32-S3-N16R8`。Windows 將 native USB
Serial/JTAG 枚舉為 COM7（VID `303A`、PID `4001`），但截至本 ADR 建立時，
esptool 的 default/no-reset/usb-reset 都尚未收到 ROM serial data，因此
Flash/PSRAM 仍需由韌體 self-test 交叉驗證。

## Decision

- PlatformIO board ID 使用官方 `esp32-s3-devkitc-1` 作為 ESP32-S3 原生
  ESP-IDF 基線。
- 明確覆寫 16 MB Flash、QIO 80 MHz，以及 8 MB octal PSRAM 的
  `sdkconfig.defaults`。
- 不把 COM7 寫入 `platformio.ini`；每次 upload 前重新辨識 port。
- Phase 1 不配置任何 e-Paper 或 sensor GPIO。
- 若 boot self-test 與 N16R8 不符，停止燒錄後續功能並修正 profile。

## Consequences

- PlatformIO 內建 board manifest 的預設 N8/no-PSRAM 值不能直接使用。
- generic board ID 不代表已確認實體 DevKit PCB 型號；它只固定 build target。
- ESP-IDF 6 的 QIO image 仍以 esptool `dio` mode 寫入 bootloader，再由
  bootloader 切換至 quad mode；因此以 `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`
  判斷 build profile，不以 flasher argument 的 `dio` 字串誤判。
- native USB 連線問題與 e-Paper pin map 分別保留為 G1/G2 驗證項。

## Verification

- clean `pio run` 必須使用 16 MB partition table 並啟用 octal PSRAM。
- boot log 必須顯示 16 MB physical Flash 與至少 8 MB SPIRAM capability heap。
- `esptool flash-id` 與 security info 仍需在 ROM download mode 可連線後補記。
