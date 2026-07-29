# ADR-0001：建立專案技術與授權基線

- Status: accepted
- Date: 2026-07-29
- Supersedes: none

## Context

PaperFrame 需在 ESP32-S3 的記憶體與 flash 限制內提供圖片輪播、離線 WebUI、
持久設定與後續 OTA。需求草案同時引用 Raspberry Pi、Pico W 與 ESP32
專案，不能直接把各自的 framework 或 host-only dependency 混入韌體。

## Decision

- 產品與 repository 名稱採 PaperFrame／`esp32-paper-frame`。
- 韌體由 PlatformIO 管理，但只使用原生 ESP-IDF API，不引入 Arduino
  framework。
- PlatformIO Core 固定為 6.1.19；Espressif 32 platform 由 Phase 1 的
  clean build 與實機 smoke 固定。
- WebUI 必須可離線運作，所有 runtime asset 存入 `webfs`。
- `webfs` 與使用者 `imagefs` 分離；從第一個版本保留雙 OTA slot。
- 感測器為可選功能。未接上的光敏與溫溼度感測器不得阻擋 boot 或圖片輪播。
- 專案採 MIT License；參考來源依 `docs/REFERENCES.md` 固定。

## Consequences

- 不能直接移植 FastAPI、Pillow、SQLite 或 MicroPython runtime。
- 純邏輯需保持可做 host test；硬體 owner 由 FreeRTOS task 與 queue 隔離。
- SDK、driver、字型與前端 dependency 必須逐一固定版本與授權。
- exact board、Flash/PSRAM 模式仍由 G1 實機驗證，不在本 ADR 猜測。

## Verification

- `requirements-dev.txt` 可重建 PlatformIO 6.1.19。
- Phase 1 clean build 顯示 framework 為 ESP-IDF，且不含 Arduino framework。
- Phase 1 boot log 記錄 chip、Flash 與 PSRAM 實測結果。
