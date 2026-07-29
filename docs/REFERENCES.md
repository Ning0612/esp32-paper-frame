# 參考來源與授權

本文件固定 `Guild.md` 所述三個參考專案的來源版本。Phase 0 只採用設計概念
與已驗證的工作模式；尚未複製任何參考專案原始碼。未來若移植程式或資產，
該 commit 必須保留來源、license notice，並再次檢查來源版本。

## 採用基線

| 專案 | 固定版本 | License | 本專案採用內容 |
| --- | --- | --- | --- |
| [epaper-home-display](https://github.com/Ning0612/epaper-home-display) | `db561872b2389c0ed11d1ae55e4a01b7ec3a6753` | MIT | WebUI 資訊架構、圖片操作語意、renderer／輪播／狀態模型 |
| [pico-paper-clock](https://github.com/Ning0612/pico-paper-clock) | `6b042bbe5bdf0cd7e396f2e70797c201e36a45b3` | MIT | 低記憶體串流、交易式檔案更新、設定遷移、感測器缺席與防抖 |
| [esp32-hydracup](https://github.com/Ning0612/esp32-hydracup) | `2103b4376218e30e9bfae9320b2b48ba1e55ccf8` | MIT | PlatformIO＋原生 ESP-IDF、FreeRTOS owner 邊界、NVS/LittleFS、AP 與 Web server |

固定版本取自 2026-07-29 本機 checkout 的 `HEAD`，remote 均為表中 GitHub
repository。這些 commit 是需求來源的追溯基線，不是 Git submodule 或自動
同步來源。

## 專案授權決策

PaperFrame 採 MIT License，與三個同作者參考專案一致。MIT 相容不代表可以
刪除第三方 copyright 或 notice：

- 複製或大幅改作來源程式時，在檔案或 `NOTICE` 中保留來源與 license。
- 字型、圖示、圖片、JavaScript library、電子紙 driver 分別核對其授權。
- Waveshare／Espressif 官方範例若在 Phase 2 引入，先固定來源版本與授權。
- 只有設計概念而未複製表達形式時，仍在 ADR 或 commit 中記錄 provenance。

## 工具與 SDK 文件

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/index.html)
- [PlatformIO Espressif 32](https://github.com/platformio/platform-espressif32)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)

Phase 1 會在 `platformio.ini` 固定實際通過 build 與實機 smoke 的 platform
版本；文件中的 `latest` 連結只供查閱，不作為可重現版本規格。
