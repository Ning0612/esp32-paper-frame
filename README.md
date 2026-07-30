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
| Phase 2 display/renderer ownership | packed framebuffer、renderer、`epd7in3e` driver、DisplayTask 與 carousel core/welcome lifecycle 已通過 |
| Phase 3 provisioning/auth/WebUI | AP／STA、離線 portal、async auth/session/CSRF、responsive management shell 與初版 Dashboard 已完成 host/build；實機部署與完整 Phase 3 驗證待進行 |
| ESP32-S3 USB 連線 | native USB 已驗證 ROM 燒錄與 app console；首次復原需 GPIO0/GPIO46 同時接地，COM 需重新辨識 |
| 模組／Flash／PSRAM profile | ESP32-S3-N16R8；實機確認 16 MB Flash／8 MB octal PSRAM |
| 7.3 吋 e-Paper HAT (E) | GPIO4/10–14 driver 實機顯示黑／黃／紅／藍／綠／白正確；sleep 電流量測待驗證 |
| 光敏電阻 | 未接，後續必須走 absent/null 路徑 |
| 溫溼度感測器 | 未接，後續必須走 absent/null 路徑 |

目前已有可編譯並通過實機 smoke test 的原生 ESP-IDF Phase 3 開發中韌體。
板上 runtime queue、DisplayTask lifecycle、電子紙六色 pattern、空圖庫
welcome refresh 與 provisioning AP 均已通過；catalog-backed 圖片輪播在
Phase 5 接入；provisioning portal 已部署，async auth／CSRF 已完成
host/build 且待隨管理 shell 一起部署，Phase 3 管理 shell 仍在進行；可選
感測器尚未整合。

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

韌體的標準 build、native USB 單命令 upload 與 port 盤點命令：

```powershell
.\.venv\Scripts\pio.exe run
.\.venv\Scripts\pio.exe run -e paperframe-s3 -t upload
.\.venv\Scripts\pio.exe device list
```

標準 upload 會依 VID:PID `303A:1001` 自動選擇 ESP32-S3 native USB，以
`usb_reset` 進入 ROM、只更新目前開發用 app offset，完成後自動重置。首次、
RGB demo 或 app 損壞無法自動 reset 時，才需在 reset 同時拉低
GPIO0、GPIO46。完整安全步驟、app-only slot 限制與測試後恢復方式見
[ESP32-S3 燒錄操作](docs/hardware/FLASHING.md)。

## 文件

- [原始需求草案](Guild.md)
- [MVP 實作計畫](docs/IMPLEMENTATION_PLAN.md)
- [參考來源與授權](docs/REFERENCES.md)
- [架構決策](docs/adr/README.md)
- [硬體驗證紀錄](docs/hardware/VALIDATION.md)
- [ESP32-S3 燒錄操作](docs/hardware/FLASHING.md)
- [Wi-Fi provisioning contract](docs/PROVISIONING.md)
- [管理認證與 CSRF contract](docs/AUTHENTICATION.md)
- [管理 WebUI 與 Dashboard](docs/WEBUI.md)
- [貢獻與自動化工作規則](AGENTS.md)

## License

本專案採用 [MIT License](LICENSE)。參考專案與未來第三方 dependency 的授權
仍需分別遵守；參考概念不代表可省略原始碼 attribution 或第三方 notice。
