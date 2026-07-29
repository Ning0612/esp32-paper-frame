# PaperFrame

PaperFrame 是以 ESP32-S3 驅動 7.3 吋 800×480 E6 全彩電子紙的離線優先
圖片輪播裝置。韌體使用 PlatformIO 管理的原生 ESP-IDF，不依賴 Arduino
framework；圖片處理、設定與管理 WebUI 在區域網路或裝置 AP 內即可運作。

## 目前狀態

| 項目 | 狀態 |
| --- | --- |
| 需求與分階段計畫 | 已建立 |
| Phase 0 repository baseline | 進行中 |
| ESP32-S3 USB 連線 | COM7，Espressif USB Serial/JTAG |
| 精確 board／Flash／PSRAM | 待 G1 實機驗證 |
| 7.3 吋 e-Paper HAT (E) | 使用者回報已接上；pin map 與 driver 留待 Phase 2 |
| 光敏電阻 | 未接，後續必須走 absent/null 路徑 |
| 溫溼度感測器 | 未接，後續必須走 absent/null 路徑 |

目前 repository 尚未有可燒錄韌體。請勿把 USB descriptor 當作完整 board
型號；Phase 1 會先讀取 chip/flash 資訊，再以最小診斷韌體驗證 PSRAM。

## 開發基線

- Windows 11 / PowerShell 5.1
- [uv](https://docs.astral.sh/uv/getting-started/installation/) 0.11.26
- Python 3.12 project venv
- PlatformIO Core 6.1.19
- PlatformIO Espressif 32 platform（版本由 Phase 1 build 固定）
- Native ESP-IDF

建立工具環境：

```powershell
uv venv --python 3.12 .venv
uv pip install --python .\.venv\Scripts\python.exe -r requirements-dev.txt
.\.venv\Scripts\pio.exe --version
```

韌體骨架建立後的標準命令：

```powershell
.\.venv\Scripts\pio.exe run
.\.venv\Scripts\pio.exe run --target upload --upload-port COM7
.\.venv\Scripts\pio.exe device monitor --port COM7 --baud 115200
```

只有在 COM7 仍對應 `VID_303A&PID_4001` 時才可直接使用上述 upload port；
裝置重新枚舉後需重新確認。

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
