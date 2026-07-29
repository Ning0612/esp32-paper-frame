# PaperFrame

PaperFrame 是以 ESP32-S3 驅動 7.3 吋 800×480 E6 全彩電子紙的離線優先
圖片輪播裝置。韌體使用 PlatformIO 管理的原生 ESP-IDF，不依賴 Arduino
framework；圖片處理、設定與管理 WebUI 在區域網路或裝置 AP 內即可運作。

## 目前狀態

| 項目 | 狀態 |
| --- | --- |
| 需求與分階段計畫 | 已建立 |
| Phase 0 repository baseline | 已完成 |
| Phase 1 persistence/runtime/health foundations | build、host tests、實機 boot／mount 與 embedded runtime test 已通過 |
| Phase 2 display/renderer ownership | packed framebuffer 與 `epd7in3e` driver 已通過 host/build 驗證；renderer 待實作 |
| ESP32-S3 USB 連線 | 本次驗證以 CH343 COM11 上傳、native USB COM10 監看；重新插拔後需重新辨識 |
| 模組／Flash／PSRAM profile | ESP32-S3-N16R8；實機確認 16 MB Flash／8 MB octal PSRAM |
| 7.3 吋 e-Paper HAT (E) | 3.3 V 與 GPIO4/10–14 pin map 已固定；pattern-test 韌體可建置，實機刷新待驗證 |
| 光敏電阻 | 未接，後續必須走 absent/null 路徑 |
| 溫溼度感測器 | 未接，後續必須走 absent/null 路徑 |

目前已有可編譯並通過實機 smoke test 的原生 ESP-IDF Phase 1 韌體。板上
runtime queue test 也已通過；電子紙 driver component 已整合但尚未完成
實機 pattern test，Wi-Fi 與可選感測器尚未開始整合。

## 開發基線

- Windows 11 / PowerShell 5.1
- [uv](https://docs.astral.sh/uv/getting-started/installation/) 0.11.26
- Python 3.13 project venv
- PlatformIO Core 6.1.19
- PlatformIO Espressif 32 platform（版本由 Phase 1 build 固定）
- Native ESP-IDF

建立工具環境：

```powershell
uv venv --seed --python 3.13 .venv
uv pip install --python .\.venv\Scripts\python.exe -r requirements-dev.txt
.\.venv\Scripts\pio.exe --version
```

韌體骨架建立後的標準命令：

```powershell
.\.venv\Scripts\pio.exe run
$uploadPort = 'COMx'
$monitorPort = 'COMy'
.\.venv\Scripts\pio.exe run --target upload --upload-port $uploadPort
.\.venv\Scripts\pio.exe device monitor --port $monitorPort --baud 115200
```

每次 upload 前都必須重新確認 port 與 USB hardware ID。同一塊板的 CH343
UART 與 ESP32-S3 native USB console 可能是不同 COM port，且 native USB
在 ROM download mode 與應用程式執行時可能重新枚舉。

## 文件

- [原始需求草案](Guild.md)
- [MVP 實作計畫](docs/IMPLEMENTATION_PLAN.md)
- [參考來源與授權](docs/REFERENCES.md)
- [架構決策](docs/adr/README.md)
- [硬體驗證紀錄](docs/hardware/VALIDATION.md)
- [貢獻與自動化工作規則](AGENTS.md)

## License

本專案採用 [MIT License](LICENSE)。參考專案與未來第三方 dependency 的授權
仍需分別遵守；參考概念不代表可省略原始碼 attribution 或第三方 notice。
