# 硬體驗證紀錄

## 2026-07-29 — 初始 USB 盤點

### 使用者提供狀態

- 模組標示為 `ESP32-S3-N16R8`。
- ESP32-S3 與 7.3 吋 e-Paper HAT (E) 已接上。
- 光敏電阻未接。
- 溫溼度感測器未接。

未接感測器是預期測試條件，不是 boot failure。Phase 1 不初始化這兩種
感測器；Phase 7 才加入 null/absent contract 與實體 driver。

### Windows 偵測結果

| Port | PNP ID | 判定 |
| --- | --- | --- |
| COM7 | `USB\VID_303A&PID_4001&MI_00` | Espressif USB Serial/JTAG；Phase 1 target |
| COM6 | `USB\VID_2E8A&PID_0005&MI_00` | 非 Espressif target；用途未在本專案判定 |

USB VID/PID 只能確認 COM7 是 Espressif native USB 介面，不能單獨證明
開發板型號、Flash 容量或 PSRAM 模式。

### G1 待驗證

- [ ] `esptool` 讀取 chip family、revision、features 與 crystal。
- [ ] `esptool flash-id` 讀取 Flash manufacturer/device/capacity。
- [ ] 固定 PlatformIO board ID 與 upload/monitor port。
- [ ] 最小韌體 boot 並記錄 ESP-IDF、reset reason、Flash 實測容量。
- [ ] 最小韌體以 capability heap API 驗證 PSRAM 可用容量。
- [ ] 驗證 USB reset 後 COM port 是否保持為 COM7。

### G1 連線嘗試

`esptool 5.3.0 --port COM7 chip-id` 可開啟 COM7，但 default reset、手動
BOOT/RESET 後的 no-reset，以及 native USB 的 usb-reset 都沒有收到 ROM
serial data。此結果可能涉及 boot strapping、reset control、USB 接孔／driver、
外接周邊或連線時序；未執行任何 erase/write。停止重複 esptool 嘗試，先用
N16R8 profile 完成 clean build，後續以持續按住 BOOT 或改接板上 UART USB
介面建立新證據。

### Phase 1 尚未實機驗證

- NVS 空白初始化與重開機後 schema／設定保存。
- NVS 初始化失敗時，韌體維持 degraded boot 且不自動擦除 partition。
- `webfs`／`imagefs` 各自製作 LittleFS image 後可獨立掛載。
- 任一 filesystem 缺失或損壞時不自動格式化，另一個仍可掛載且系統繼續 boot。
- Runtime command／result queues 在滿載時立即失敗，不阻塞呼叫端。
- Snapshot 在 queue 忙碌時仍可讀取，且只呈現已發布的完整狀態。

Runtime on-device test 使用獨立設定，避免與 host test 或一般 firmware
`app_main` 混合：

```powershell
.\.venv\Scripts\pio.exe test `
  --project-conf platformio-embedded.ini `
  -e paperframe-s3-embedded-test `
  -f test_runtime_coordinator
```

先完成韌體 build，再以相同 CMake build graph 產生尺寸與 partition table
一致的 factory images：

```powershell
.\.pio\packages\tool-cmake\bin\cmake.exe --build `
  .\.pio\build\paperframe-s3 `
  --target littlefs_webfs_bin littlefs_imagefs_bin
```

成功後會產生 `.pio\build\paperframe-s3\webfs.bin` 與
`.pio\build\paperframe-s3\imagefs.bin`。只有新機 factory provisioning 或
明確要清空兩個 filesystem 時，才可執行：

```powershell
.\.venv\Scripts\python.exe -m esptool --chip esp32s3 --port <COM> write-flash `
  0x510000 .pio\build\paperframe-s3\webfs.bin `
  0x630000 .pio\build\paperframe-s3\imagefs.bin
```

此命令會覆寫 `webfs`，並以空 image 覆寫 `imagefs`、清除所有使用者圖片；
一般 firmware upload／OTA 不得包含這兩個 image。

### Phase 2 前仍需完成

- e-Paper SPI、BUSY、RST、DC、CS 精確 pin map。
- 面板電源與 logic level。
- `epd7in3e` driver 版本、授權、BUSY polarity 與 timeout。
- 實機確認 GPIO35、GPIO36、GPIO37 未被配置給周邊。
