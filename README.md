# PaperFrame

[![PlatformIO](https://img.shields.io/badge/PlatformIO-6.1.19-orange?logo=platformio)](https://platformio.org)
[![Board](https://img.shields.io/badge/Board-ESP32--S3--N16R8-red?logo=espressif)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Version](https://img.shields.io/badge/Version-0.8.2-yellow)](https://github.com/Ning0612/esp32-paper-frame/releases)
[![CI](https://github.com/Ning0612/esp32-paper-frame/actions/workflows/ci.yml/badge.svg)](https://github.com/Ning0612/esp32-paper-frame/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

PaperFrame 是以 ESP32-S3 驅動 7.3 吋 800×480 E6 全彩電子紙的離線優先
圖片輪播裝置。韌體使用 PlatformIO 管理的原生 ESP-IDF，不依賴 Arduino
framework；圖片處理、設定與管理 WebUI 在區域網路或裝置 AP 內即可運作。

> **與其他 e-paper 專案的關係**
>
> PaperFrame 與 [epaper-home-display](https://github.com/Ning0612/epaper-home-display)
> （Raspberry Pi 多資訊智慧家庭面板）、[pico-paper-clock](https://github.com/Ning0612/pico-paper-clock)
> （Pico W／MicroPython 桌面時鐘）是**各自獨立維護的不同專案**，硬體平台、
> 韌體堆疊與用途都不同，彼此沒有取代關係。本專案的定位是 ESP32-S3 上的
> 離線優先全彩圖片輪播。
>
> 設計上借鑑了上述兩者與 [esp32-hydracup](https://github.com/Ning0612/esp32-hydracup)
> 的既有經驗（低記憶體串流、交易式檔案更新、FreeRTOS owner 邊界等），
> 採用的來源版本固定於 [參考來源與授權](docs/REFERENCES.md)。

## 目前狀態

目前狀態的唯一進度入口是 [專案狀態](docs/PROJECT_STATUS.md)。摘要如下：

| 分類 | 目前結論 |
| --- | --- |
| 已完成 | Phase 1–8 的主要程式、host tests 與韌體 build 已完成；部分 boot、mount、面板 pattern 與 STA smoke 已有實機證據。 |
| 待驗證 | WebUI 瀏覽器流程、PFR1／imagefs 斷電、長時間輪播、天氣、感測器、OTA rollback 與 release gate 仍有實機缺口。 |
| 待決定 | production security profile（Secure Boot／Flash Encryption）與 MVP 以外的 P1 功能尚未納入目前開發。 |

在硬體驗證缺口關閉前，不宣稱 MVP release 或 production-ready。

## 功能

括號內是**實機驗證狀態**。「程式完成」代表程式與 host/build test 已完成，
**不代表已在實機驗證**——逐項證據以 [專案狀態](docs/PROJECT_STATUS.md) 與
[硬體驗證紀錄](docs/hardware/VALIDATION.md) 為準。

- **離線優先圖片輪播**：區網或裝置 AP 內即可完成全部操作，不依賴外部 CDN
  或後端服務（程式完成；長時間輪播實機未驗證）
- **PFR1 自有圖片格式**：瀏覽器端完成量化與打包，裝置只負責解碼與顯示，
  避免在 MCU 上做重量級影像處理（程式完成；browser 產出與下載未驗證）
- **交易式 imagefs 儲存**：圖片分區與韌體分區分離，OTA 更新不會清除既有圖片
  （程式完成；斷電 fault injection 未驗證）
- **管理 WebUI**：Dashboard、圖片管理、設定與 System 診斷頁（程式完成；
  瀏覽器流程未驗證）
- **配網與認證**：AP portal 配網，blank-NVS 與 STA 重試耗盡時自動 fallback AP；
  PBKDF2 10,000 iterations、session 與 CSRF 保護（程式完成；STA smoke 已驗證）
- **OTA 更新與 rollback**：以 GitHub Releases 為來源，A/B 分區與 rollback
  確認機制（程式完成；真實下載與 rollback fault injection 未驗證）
- **天氣資訊**：解析、快取與面板狀態列顯示（程式完成；真實 SNTP 與 TLS
  失敗分類未驗證）
- **可選環境感測**：DHT22 溫溼度與光敏電阻，含濾波、presence debounce 與
  離席白屏（程式完成；**硬體尚未接入**）

## 文件入口

請先讀 [文件入口與分類](docs/README.md)；若只想知道現在做到哪裡，讀
[專案狀態](docs/PROJECT_STATUS.md)。ADR、current contract、硬體證據與歷史
計畫的權威關係都集中在該入口說明，不需要從所有文件開始閱讀。

## 開源快速開始

開發環境需要 Windows 11、Python 3.13、PlatformIO Core 6.1.19 與原生 ESP-IDF。
在乾淨 checkout 中執行：

```powershell
uv venv --seed --python 3.13 .venv
uv pip install --python .\.venv\Scripts\python.exe -r requirements-dev.txt
.\.venv\Scripts\pio.exe test -e native
.\.venv\Scripts\pio.exe run
```

`native` 是不需要硬體的 host test；韌體 upload、面板、Wi-Fi、NVS、OTA 與
斷電復原則依 [燒錄操作](docs/hardware/FLASHING.md)、[硬體驗證紀錄](docs/hardware/VALIDATION.md)
與 [release checklist](docs/RELEASE_CHECKLIST.md) 執行。請勿把 host/build 結果
當成實機驗證。

## 硬體

完整元件清單、接線與腳位限制見 [硬體規格與接線](docs/hardware/HARDWARE.md)；
腳位分配的決策與理由固定在 [ADR-0003](docs/adr/0003-fix-phase2-display-integration.md)。
以下為常用摘要。

| 類別 | 元件 | 狀態 |
| --- | --- | --- |
| 主控 | ESP32-S3-N16R8（16 MB Flash、8 MB octal PSRAM） | 必要 |
| 顯示器 | Waveshare 7.3 吋 e-Paper HAT (E)，800×480 E6 六色 | 必要 |
| 溫溼度 | DHT22／AM2302 | 可選；driver 已實作，**實機未驗證** |
| 光感測 | 光敏電阻＋分壓，接 `ADC1_CH4` | 可選；driver 已實作，**實機未驗證** |
| 外殼 | 3D 列印 | **尚未設計** |

顯示器接線（3.3 V logic、SPI2、mode 0、MSB-first、起始 clock 2 MHz）：

| HAT | DIN | CLK | CS | DC | RST | BUSY |
| --- | --- | --- | --- | --- | --- | --- |
| ESP32-S3 | GPIO11 | GPIO12 | GPIO10 | GPIO13 | GPIO14 | GPIO4 |

感測器腳位：光敏 ADC = GPIO5（`ADC1_CH4`）、DHT data = GPIO6；GPIO8／GPIO9
保留給未來 I²C。**改接線前務必先讀 ADR-0003 的腳位限制**——octal PSRAM 佔用
GPIO33–37、native USB 佔用 GPIO19–20，GPIO0／3／45／46 是 strapping pins，
GPIO4 已給 BUSY 不得再作 ADC。

## 架構概覽

韌體拆成 12 個 ESP-IDF component，`src/app_main.cpp` 只負責啟動與接線。
跨 component 的協調集中在 `pf_runtime` 的 `RuntimeCoordinator`：顯示、儲存、
網路與 OTA 各自有明確的 owner，彼此以訊息往來而不是共享可變狀態，避免網路或
儲存 I/O 阻塞面板刷新。

各 component 的設計理由與取捨固定在 [ADR](docs/adr/README.md)，本節只是導覽。

| Component | 職責 |
| --- | --- |
| `pf_runtime` | `RuntimeCoordinator`、runtime snapshot、diagnostics event、reboot reason、韌體版本 |
| `pf_display` | epd7in3e driver（SPI2）、DisplayTask、owner contract |
| `pf_carousel` | 輪播排程、image frame 與 welcome frame |
| `pf_image` | PFR1 格式解碼 |
| `pf_storage` | LittleFS backend、image store、catalog、transaction 與 recovery、storage worker |
| `pf_network` | AP／STA 狀態機、配網服務、掃描結果、SNTP 時間同步 |
| `pf_web` | HTTP server、Dashboard／health serializer、各設定表單、access policy |
| `pf_auth` | PBKDF2 密碼驗證、session 與 CSRF |
| `pf_config` | NVS 設定 schema、憑證與 secure memory |
| `pf_ota` | GitHub Releases 下載、A/B 分區與 rollback 確認 |
| `pf_weather` | 天氣解析、快取與 worker |
| `pf_sensors` | DHT22 與光敏 ADC driver、濾波與 presence 判定 |

測試分兩層：`test/` 是不需要硬體的 host test（`pio test -e native`），
`test_embedded/` 是需要實機的 on-target test。**host test 通過不等於實機驗證**，
兩者的分界與現況見 [硬體驗證紀錄](docs/hardware/VALIDATION.md)。

## 公開基線與已知限制

- 目標基線是 ESP32-S3-N16R8、16 MB Flash、8 MB octal PSRAM 與 7.3 吋
  800×480 E6 全彩電子紙；光敏與溫溼度感測器是可選周邊（見上方硬體段）。
- WebUI 與圖片管理以離線優先為原則，不依賴外部 CDN 或後端服務。
- 目前仍有顯示器、WebUI、PFR1／imagefs 斷電、天氣、感測器與 OTA 的實機
  驗證缺口；在 [硬體驗證紀錄](docs/hardware/VALIDATION.md) 的目前未完成索引
  關閉前，不宣稱 MVP release 或 production-ready。
- 真實裝置測試資料、裝置識別資訊與執行期 imagefs 不屬於公開 repository 內容。

## 開發基線

- Windows 11 / PowerShell 5.1
- [uv](https://docs.astral.sh/uv/getting-started/installation/) 0.11.26
- Python 3.13 project venv
- PlatformIO Core 6.1.19
- PlatformIO Espressif 32 platform（版本由 Phase 1 build 固定）
- Native ESP-IDF

上列是實際使用並通過 build 的組合。**Linux 與 macOS 未測試**——工具鏈本身跨平台，
但本專案的指令範例、`uv` 路徑與燒錄流程都以 Windows 為準，其他平台需自行調整。

建立工具環境：

```powershell
uv venv --seed --python 3.13 .venv
uv pip install --python .\.venv\Scripts\python.exe -r requirements-dev.txt
.\.venv\Scripts\pio.exe --version
```

韌體的標準 build、native USB 單命令 upload 與 port 盤點命令：

```powershell
.\.venv\Scripts\pio.exe run
.\.venv\Scripts\pio.exe run -e paperframe-s3 -t upload
.\.venv\Scripts\pio.exe device list
```

標準 upload 會依 VID:PID `303A:1001` 自動選擇 ESP32-S3 native USB，以
`usb_reset` 進入 ROM，讀取 `otadata` 後更新目前啟動的 OTA app slot，完成後
自動重置。首次、RGB demo 或 app 損壞無法自動 reset 時，才需在 reset 同時拉低
GPIO0、GPIO46。完整安全步驟、app-only slot 限制與測試後恢復方式見
[ESP32-S3 燒錄操作](docs/hardware/FLASHING.md)。

## 文件

- [文件入口與分類](docs/README.md)
- [專案狀態](docs/PROJECT_STATUS.md)
- [架構決策](docs/adr/README.md)
- [硬體規格與接線](docs/hardware/HARDWARE.md)
- [硬體驗證紀錄](docs/hardware/VALIDATION.md)
- [參考來源與授權](docs/REFERENCES.md)
- [歷史需求草案](docs/archive/Guild.md)

## License

本專案採用 [MIT License](LICENSE)。參考專案與未來第三方 dependency 的授權
仍需分別遵守；參考概念不代表可省略原始碼 attribution 或第三方 notice。
