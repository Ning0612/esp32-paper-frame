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

Runtime on-device tests 使用獨立 environment，避免不同 test app 共用
ESP-IDF CMake scaffold。Windows 上先完成 build，再重用同一 build graph
做 upload/test：

```powershell
.\.venv\Scripts\pio.exe test `
  --project-conf platformio-embedded.ini `
  -e paperframe-s3-embedded-test `
  --without-uploading --without-testing

.\.venv\Scripts\pio.exe test `
  --project-conf platformio-embedded.ini `
  -e paperframe-s3-embedded-test `
  --without-building
```

DisplayTask 實機測試使用另一個 environment，流程相同：

```powershell
.\.venv\Scripts\pio.exe test `
  --project-conf platformio-embedded.ini `
  -e paperframe-s3-display-test `
  --without-uploading --without-testing

.\.venv\Scripts\pio.exe test `
  --project-conf platformio-embedded.ini `
  -e paperframe-s3-display-test `
  --without-building
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

### Phase 2 G2/G3 決策

G2/G3 已由 [ADR-0003](../adr/0003-fix-phase2-display-integration.md) 固定：

- e-Paper 使用 3.3 V；DIN/CLK/CS/DC/RST/BUSY 分別為
  GPIO11/12/10/13/14/4。
- GPIO5 保留給 ADC、GPIO6 保留給 DHT、GPIO8/9 保留給 I²C SDA/SCL。
- GPIO33–37 不配置給任何周邊。
- `epd7in3e` 固定到 Waveshare upstream commit `06e8344`、MIT permission
  notice、六色 native mapping、active-low BUSY 與 60 秒 hard timeout。

這些是接線與實作 gate，不是面板已刷新成功的證據。仍待 Phase 2 driver
完成後執行六色 pattern、刷新時間、deep sleep 與 forced-BUSY 實機驗證。

### 2026-07-30 — Phase 2 panel-driver 驗證

- `pio test -e native` 通過全部 25 個 host test，其中 7 個覆蓋
  `epd7in3e` command trace、active-low BUSY、timeout、錯誤中止與 deep
  sleep 狀態。
- `pio run -e paperframe-s3` 成功，ESP-IDF adapter 已編入正式韌體。
- 六色直條 pattern 的 embedded test firmware 可成功建置。
- 當次連線辨識為 CH343 UART `COM11`（VID:PID `1A86:55D3`）與 ESP32-S3
  native USB `COM10`（VID:PID `303A:1001`）。
- CH343 自動 reset、手動 BOOT/RST 後的 no-reset UART 連線，以及 native
  USB no-reset 連線都未收到 ROM download response；最後分別回報
  `No serial data received` 與 `Write timeout`。
- OTA metadata read 也在 ROM handshake 前失敗，因此沒有讀取或改寫 flash，
  測試韌體未上傳，既有 app、NVS、webfs 與 imagefs 均未變更。

本次不能宣稱六色 mapping、實際刷新時間或 refresh 後 panel sleep 已在實機
通過。下一次硬體驗證必須先恢復可重現的 ROM download path，再上傳既有
embedded pattern test；forced-BUSY 仍只允許 fake 或隔離治具測試。

### 2026-07-30 — native USB ROM 與六色 pattern 實機通過

前一節的 ROM blocker 已用以下不同方法解決：

1. `GPIO0` 與 `GPIO46` 同時固定接到 `GND` 後按 `RST`。
2. CH343 `COM11` 仍未輸出 ROM 資料；ESP32-S3 native USB `COM10`
   則明確輸出：

   ```text
   boot:0x0 (DOWNLOAD(USB/UART0))
   waiting for download
   ```

3. 關閉占用 COM10 的 monitor 後，`esptool --before no-reset` 經 native
   USB 成功連線並讀取 OTA metadata。

OTA metadata 第一份 entry 為 sequence 1、state `VALID`，第二份 entry
全為 erased bytes，因此本次 active app 是 `ota_0`（`0x10000`，
`0x280000` bytes）。操作先完整備份 `ota_0`，再只把 218,944-byte
embedded pattern-test app 寫到 `0x10000`；未改寫 bootloader、partition
table、OTA metadata、NVS、webfs 或 imagefs。`esptool` 寫入後 hash 驗證
通過。

拔除 GPIO0/GPIO46 接地線並按 RST 後，使用者確認面板依序正確顯示六條
垂直色帶：黑、黃、紅、藍、綠、白。這完成 palette/nibble/SPI/panel
command 的端到端視覺驗證。當次 console monitor 在 USB 交接時斷線，未保留
Unity 尾端輸出，因此不把視覺結果延伸宣稱為 deep-sleep 電流已量測。

測試 app 執行後，native USB `--before usb-reset` 已可免接 straps 自動進
ROM。最後將測試前的完整 `ota_0` 備份原樣寫回，written data hash 驗證
通過並 hard reset；裝置已恢復測試前 app。可重現步驟見
[ESP32-S3 燒錄操作](FLASHING.md)。

### 2026-07-30 — PlatformIO native USB 單命令燒錄通過

專案將 PlatformIO esptool reset 設為 `usb_reset`，並以 post extra script
把 routine upload 限制為 app image。`envdump` 確認最終 uploader flags
沒有 bootloader、partition table 或 `ota_data_initial.bin`。

在 GPIO0/GPIO46 都未接地、native USB 與 CH343 同時存在的情況下，執行：

```powershell
.\.venv\Scripts\pio.exe run -e paperframe-s3 -t upload
```

PlatformIO 依 VID:PID `303A:1001` 自動選到 native USB `COM10`，esptool
確認 `USB mode: USB-Serial/JTAG`，只擦除 `0x10000`–`0x69fff` 並寫入
365,632-byte app image。寫入後 data hash 驗證成功並自動 hard reset；
裝置重新枚舉為同一 native USB hardware ID。未改寫 bootloader、partition
table、OTA metadata、NVS、`webfs` 或 `imagefs`。

仍待驗證：

- 一般刷新時間的實測數值；
- refresh 後 panel sleep 的電流量測；
- forced-BUSY 隔離治具測試。

### 2026-07-30 — DisplayTask lifecycle 實機通過

Phase 2 DisplayTask 已整合為正式 app 的唯一 panel/SPI owner。Frame data
使用兩個 192,000-byte PSRAM slot 與 generation token；producer 提交後不再
持有可寫 lease，FreeRTOS queue 不保存 framebuffer pointer 或內容。

驗證結果：

- `pio test -e native`：36/36 通過；新增測試涵蓋 lease transfer、stale
  token、queue rollback、pool reset、BUSY/transport result mapping，以及
  display worker 阻塞時 health serialization 仍可完成。
- `test_runtime_coordinator` embedded test：1/1 通過；驗證 command queue
  overflow、queued/refreshing/failed snapshot transition 與共享
  flash/display gate。
- `test_display_task` embedded test：1/1 通過，總耗時 36.792 秒。提交呼叫
  在 100 ms gate 內返回；DisplayTask 對實際面板送出六色 full frame，最後
  result 為 `completed/refreshed_and_slept`，runtime snapshot 為
  `deep_sleep`。
- 測試 app 完成後執行標準單命令 upload。PlatformIO 自動選到 native USB
  `COM10`，只擦除 `0x10000`–`0x75fff`、寫入 414,960-byte 正式 app，
  data hash 驗證成功並 hard reset。bootloader、partition、OTA metadata、
  NVS、`webfs` 與 `imagefs` 均未改寫。

本次 `deep_sleep` 證據來自完整 command/result contract 與 driver state；
尚未以電流表量測面板 sleep 功耗。forced-BUSY 仍只在 fake driver 通過，
實機測試仍需隔離治具。
