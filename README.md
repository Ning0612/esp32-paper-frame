# PaperFrame

PaperFrame 是以 ESP32-S3 驅動 7.3 吋 800×480 E6 全彩電子紙的離線優先
圖片輪播裝置。韌體使用 PlatformIO 管理的原生 ESP-IDF，不依賴 Arduino
framework；圖片處理、設定與管理 WebUI 在區域網路或裝置 AP 內即可運作。

## 目前狀態

目前狀態的唯一進度入口是 [專案狀態](docs/PROJECT_STATUS.md)。摘要如下：

| 分類 | 目前結論 |
| --- | --- |
| 已完成 | Phase 1–8 的主要程式、host tests 與韌體 build 已完成；部分 boot、mount、面板 pattern 與 STA smoke 已有實機證據。 |
| 待驗證 | WebUI 瀏覽器流程、PFR1／imagefs 斷電、長時間輪播、天氣、感測器、OTA rollback 與 release gate 仍有實機缺口。 |
| 待決定 | production security profile（Secure Boot／Flash Encryption）與 MVP 以外的 P1 功能尚未納入目前開發。 |

在硬體驗證缺口關閉前，不宣稱 MVP release 或 production-ready。

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

## 公開基線與已知限制

- 目標基線是 ESP32-S3-N16R8、16 MB Flash、8 MB octal PSRAM 與 7.3 吋
  800×480 E6 全彩電子紙；光敏與溫溼度感測器是可選周邊。
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
- [硬體驗證紀錄](docs/hardware/VALIDATION.md)
- [參考來源與授權](docs/REFERENCES.md)
- [歷史需求草案](docs/archive/Guild.md)

## License

本專案採用 [MIT License](LICENSE)。參考專案與未來第三方 dependency 的授權
仍需分別遵守；參考概念不代表可省略原始碼 attribution 或第三方 notice。
