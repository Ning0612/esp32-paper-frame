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
| COM7 | `USB\VID_303A&PID_4001&MI_00` | 第一塊板的 Espressif USB Serial/JTAG；不是固定 port |
| COM6 | `USB\VID_2E8A&PID_0005&MI_00` | 非 Espressif target；用途未在本專案判定 |

USB VID/PID 只能確認 COM7 是 Espressif native USB 介面，不能單獨證明
開發板型號、Flash 容量或 PSRAM 模式。

### G1 驗證結果

- [x] `esptool` 讀取 chip family、revision、features 與 crystal。
- [x] `esptool flash-id` 讀取 Flash manufacturer/device/capacity。
- [x] 固定 PlatformIO profile；upload/monitor port 每次依 USB hardware ID 辨識。
- [x] 最小韌體 boot 並記錄 ESP-IDF、reset reason、Flash 實測容量。
- [x] 最小韌體以 capability heap API 驗證 PSRAM 可用容量。
- [x] 確認 native USB 在 ROM／應用程式模式可能重新枚舉，不固定為 COM7。

### 第一塊板：G1 連線診斷

`esptool 5.3.0 --port COM7 chip-id` 可開啟 COM7，但 default reset、手動
BOOT/RESET 後的 no-reset，以及 native USB 的 usb-reset 都沒有收到 ROM
serial data。此結果可能涉及 boot strapping、reset control、USB 接孔／driver、
外接周邊或連線時序；未執行任何 erase/write。停止重複 esptool 嘗試，先用
N16R8 profile 完成 clean build，後續以持續按住 BOOT 或改接板上 UART USB
介面建立新證據。

### G1 UART 與 boot strap 診斷

後續改接板上 CH343 USB-to-UART，Windows 枚舉為 `COM9`
（VID `1A86`、PID `55D3`）。正常啟動時 `COM9` 可穩定收到 ESP32-S3 ROM
與既有 factory demo 的 log：

```text
ESP-ROM:esp32s3-20210327
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
SHA-256 comparison failed:
Attempting to boot anyway...
RGB Demo
```

這證明 ESP32-S3 UART0 TX 到 CH343 RX 與 Windows 接收方向可用。boot log
回報 SHA-256 comparison failed，但未對 Flash 做 readback；該 image 仍能
進入 RGB Demo。此結果不是 PaperFrame 韌體 boot 證據。

排除 e-Paper HAT 後，以 BOOT 按鍵與實體 `GPIO0` 到 `GND` 跳線分別重做
manual download。GPIO0 接地後 RGB Demo 與正常 boot log 停止，但
`esptool 5.3.0` 經 `no-reset` 與 `default-reset` 仍未收到 ROM sync。
將已知可傳資料的 USB 線改接 native USB 後，Windows 未枚舉 Espressif 或
Unknown USB 裝置。未執行任何 erase、write 或 eFuse 操作。

依 Espressif 的 ESP32-S3 boot mode contract，reset 時 GPIO0 為低且 GPIO46
浮接或為低，才會進入 ROM serial bootloader。目前尚缺電氣量測，不能只由
RGB Demo 停止推定 GPIO0、EN 與 GPIO46 的實際 reset 電位。下一個不同證據
必須是以下其中之一：

1. 量測 reset 後 `GPIO0 ≈ 0 V`、`EN ≈ 3.3 V`，並確認 GPIO46 浮接或為低。
2. 使用獨立 3.3 V USB-to-TTL adapter 交叉驗證 UART0 RX/TX，不由板載
   CH343 傳送；板子與 adapter 必須共地、RX/TX 交叉，板子已供電時不得接
   adapter 的 5 V/VCC。
3. 改用另一塊已知可進 ROM bootloader 的同型板交叉驗證。

採用第 3 項後，第二塊同型板成功建立 ROM sync。第一塊板的 transport
問題仍未歸因，不把第二塊板成功誤記為第一塊板已修復。

### 第二塊板：G1 與 Phase 1 實機驗證

第二塊 `ESP32-S3-N16R8` 以板載 CH343 UART 進入 ROM bootloader。本次
Windows 枚舉為 upload `COM11`；PaperFrame 應用程式的 native USB
Serial/JTAG console 枚舉為 `COM10`。這些編號只記錄本次操作，重新插拔後
必須重新辨識。

唯讀探測結果：

- ESP32-S3 QFN56 revision v0.2，40 MHz crystal。
- physical Flash 16 MB、quad I/O、3.3 V。
- embedded PSRAM 8 MB；韌體以 octal 80 MHz 初始化並通過 memory test。
- Secure Boot disabled、Flash Encryption disabled；未寫入 eFuse。

一般韌體與 factory filesystem 的寫入順序如下：

1. PlatformIO upload 寫入 bootloader `0x0`、partition table `0x8000`、
   OTA data `0xd000` 與 application `0x10000`，各區段 hash 驗證成功。
2. 明確執行 factory provisioning，將 1,048,576-byte `webfs.bin` 寫入
   `0x510000`，將 10,289,152-byte 空白 `imagefs.bin` 寫入 `0x630000`，
   兩區段 hash 驗證成功。
3. 後續一般 firmware upload 未重寫 `webfs` 或 `imagefs`。

首次監看 PaperFrame console 時，Flash 與 PSRAM self-test 通過，未設定的
感測器被略過，NVS schema 1 以 `use_current` 載入；`webfs` 與 `imagefs`
分別掛載成功。啟動 health server 前未初始化 lwIP，觸發
`tcpip_send_msg_wait_sem` 的 `Invalid mbox` assert。backtrace 定位到
`httpd_start`，修正為先呼叫 `esp_netif_init()`，失敗時 health server
降級停用而不是呼叫 socket API。修正版重新 build、upload 並完成板上測試。

RuntimeCoordinator embedded test 以獨立 test firmware 驗證 command/result
queue capacity、non-blocking overflow 與 snapshot publish/read，結果 1/1
通過；測試後已恢復一般韌體，未重寫兩個 filesystem。

### Phase 1 仍保留的 fault-injection 項目

- NVS 空白初始化的實機 log 尚未留存；schema 相同的 `use_current` 已觀察，
  空白、遷移與不支援版本由 host tests 覆蓋。
- NVS 初始化失敗時，韌體維持 degraded boot 且不自動擦除 partition。
- 任一 filesystem 缺失或損壞時不自動格式化，另一個仍可掛載且系統繼續 boot。

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
