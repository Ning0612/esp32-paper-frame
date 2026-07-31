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

### 2026-07-30 — Phase 2 carousel welcome lifecycle 實機通過

輪播核心固定 30 分鐘預設與 5 分鐘硬下限，支援順序／隨機、新圖片
`shown_once` 優先、手動選擇保持完整週期，以及 invalid/disabled 圖片排除。
空圖庫只排入一次內建 welcome/status frame；完成後不週期性重刷相同畫面。
實際 catalog 與 PFR1 frame source 依計畫在 Phase 4–5 接入。

驗證結果：

- `pio test -e native`：50/50 通過，其中 14 個 carousel tests 使用 fake
  monotonic clock 覆蓋 interval、空圖庫、順序／隨機、新圖、手動、失敗
  退避、未提交重試、每次開機 welcome 與 stale decision；welcome frame
  幾何與無效 buffer 亦有 golden checks。
- `test_runtime_coordinator` embedded test：1/1 通過；除 queue/snapshot 外，
  驗證全域 request ID、接受命令前保留 terminal-result slot、容量耗盡時
  拒絕新命令，以及按 ID 完成／消費與釋放後恢復；較新的完成結果不會使
  較早的 carousel request 永久卡在 in-flight。
- ESP-IDF 6 啟用 `MINIMAL_BUILD`，只編譯 `main` 與明確相依元件，排除
  未使用的 RGB LCD driver；`pio run -e paperframe-s3` 成功，RAM
  27,148 bytes（8.3%），Flash 414,277 bytes（15.8%）。
- 標準單命令 upload 自動選到 native USB `COM10`，只擦除
  `0x10000`–`0x75fff` 並寫入 414,688-byte app；data hash 驗證成功並
  hard reset，未改寫其他 partition。
- 實機 console 回報
  `carousel_request=1 outcome=1 next_due_ms=1832147`；`outcome=1` 對應
  `refreshed_and_slept`，deadline 約為實際完成時間後 30 分鐘。

內建畫面預期為藍色外框、六色狀態條與黑色 `PF` 標記。console 與
DisplayTask lifecycle 已端到端通過；本次沒有以電流表量測 sleep 功耗，
也未把未接入 catalog 的真實圖片輪播誤記為實機通過。

### 2026-07-30 — Phase 3 provisioning AP state machine 實機通過

本段加入 `NetworkServiceTask` 與可在 host 驗證的 AP／STA 狀態機。
ESP-IDF Wi-Fi event callback 只投遞事件，STA/AP mode 切換、連線 timeout
及有限次 retry 都由 owner task 執行。Wi-Fi 與 Internet 狀態分開保存在
runtime snapshot；Internet 不可用不會把已連線 STA 切回 provisioning AP。

驗證結果：

- `pio test -e native`：59/59 通過，其中 9 個 network-state tests 覆蓋
  空白 credential、STA 成功、Internet unreachable、錯誤密碼等價的
  timeout/disconnect retry、次數耗盡 fallback AP、Recovery AP、AP action
  failure 有限重試、Wi-Fi 初始化失敗的明確 failed 狀態與無效 policy。
- `test_runtime_coordinator` embedded test：1/1 通過，包含
  `wifi=provisioning`／`internet=unknown` 的原子 snapshot update。
- `pio run -e paperframe-s3` 成功；RAM 52,384 bytes（16.0%），Flash
  897,589 bytes（34.2%）。
- native USB 單命令 upload 自動選到 `COM10`，只擦除
  `0x10000`–`0xebfff` 並寫入 898,000-byte app；data hash 驗證成功。
- 空白 credential 實機啟動 `PaperFrame-Setup-[masked]`，固定 IP
  `192.168.4.1`，DHCP server 成功啟動；log 未輸出 AP password 或任何
  STA credential。

第一次實機整合在 Wi-Fi 射頻啟動與電子紙 welcome 同時刷新時觸發
brownout。依產品狀態規則修正為 provisioning／offline retry 暫停 carousel，
只有 `wifi=connected` 才排入圖片刷新；重新燒錄後連續監看 30 秒未再重啟。
AP 專用引導畫面、portal、credential 儲存與 STA 成功路徑仍屬後續 Phase 3
commit，不在本段宣稱完成。Windows WLAN 掃描因系統 Location 權限關閉而
無法從主機列舉 SSID；AP 啟動證據來自 ESP-IDF mode、DHCP 與 service log。

### 2026-07-30 — Offline provisioning portal 部署與 STA smoke 通過

本段對應 Phase 3 的第二個 commit 邊界，只包含 AP 引導畫面、離線 portal、
Wi-Fi scan/config pipeline、credential commit lifecycle 與 bootstrap route
policy；不把尚未實作的管理 session、CSRF 或 Recovery AP login 宣稱完成。

驗證結果：

- `pio test -e native`：88/88 通過；包含隨機 AP password formatter、AP
  golden payload／QR escaping、credential codec 與 secret guard、表單解析、
  HTTP receive deadline、request/status lifecycle、bootstrap access policy、
  scan 去重／排序／過濾及既有 Phase 1–2 regression tests。
- `pio run -e paperframe-s3` clean build 成功；RAM 56,560 bytes（17.3%），
  Flash 934,649 bytes（35.7%）。新增 component dependency 後第一次 incremental
  build 沿用舊 CMake graph 而缺 include path；依已驗證修法 clean 後成功，
  不是 source compile failure。
- `littlefs_webfs_bin` 產生 1,048,576-byte `webfs.bin`，SHA-256
  `050AC7BAA87E46606A9EC0DD30C33C1AC204E6917C62A1A19B9D7CC57D0D509F`。
  只寫入 `0x510000`–`0x60ffff`，esptool data hash 驗證通過；未改寫
  `imagefs`、NVS、app 或 OTA metadata。
- 標準 PlatformIO 單命令 upload 自動選到 native USB `COM10`，只擦除
  `0x10000`–`0xf4fff` 並寫入 935,056-byte app；data hash 驗證通過並
  hard reset。
- 裝置保留既有 Wi-Fi credential 並成功進 STA；以遮蔽後的晶片 hardware
  ID 對應私有 DHCP 位址。實機
  `GET /api/v1/health` 回 `200`、`status=ready`、五個 service 都為
  `ready`、`wifi=connected`。
- 實機 `/`、`style.css`、`ui.js`、`favicon.svg` 均回 `200`，回傳大小分別
  3,529、7,013、6,637、326 bytes，與本次 `data/web` 完全一致。STA
  未登入的 scan GET 與虛構 config POST 均回 `401`、`Cache-Control:
  no-store`，config request 在 access check 即被拒絕，未改動 NVS。

本次沒有清除使用者既有 NVS 來強迫進首次 AP，因此最新 artifact 的
blank-NVS scan／credential save／約 1 秒 reboot 尚未做板上端到端驗證；
先前版本已驗證 AP screen 在 radio 前完成、AP/DHCP 可啟動。待 Phase 3
auth commit 可安全操作 Recovery AP 後，再補最新 artifact 的 AP scan、
登入、CSRF、credential save 與 STA reconnect 全流程。

### 2026-07-30 — Phase 3 auth／WebUI shell build 與安全啟動 smoke

本段記錄管理 session、CSRF route policy、共用 responsive shell、Dashboard
初版 API 與安全日誌修正的部署結果。WebUI 以獨立 `webfs` image 部署；一般
PlatformIO app upload 沒有重寫 `webfs`、`imagefs`、NVS 或 OTA metadata。

驗證結果：

- `pio test -e native`：105/105 通過；新增 dashboard serializer 與既有
  auth／CSRF、network、portal 及 Phase 1–2 regression tests 均納入同一次
  完整執行。
- `node --check data/web/ui.js` 通過；`pio run -e paperframe-s3` 成功，
  RAM 61,616 / 327,680 bytes（18.8%），Flash 約 960,733 /
  2,621,440 bytes（36.6%）。
- `littlefs_webfs_bin` 產生 1,048,576-byte image，SHA-256
  `A1B8F3A6881390812E263D8366E7A0D2547E25867E281AAE22AF3305BD0F5803`；
  以 `esptool` 只寫入 `0x510000`–`0x60ffff`，data hash 驗證通過。
- 以 PlatformIO native USB app-only upload 部署正式韌體；COM10 的
  `303A:1001` USB-Serial/JTAG 連線、app hash 驗證與 hard reset 均成功。
- 隨後使用 RTS-only reset 讀取 COM10 啟動日誌，確認：

  ```text
  rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)
  flash_bytes=16777216 expected=16777216 status=ready
  psram_initialized=true psram_bytes=8388608 expected=8388608 status=ready
  filesystem=webfs mounted=true total=1048576 used=53248
  filesystem=imagefs mounted=true total=10289152 used=8192
  health_server_ready route=/api/v1/health
  ```

- 同一段啟動輸出未出現 `QRCODE`、`WIFI:T`、AP password、SSID 或其他
  credential。這驗證了 `QRCODE` logger 降級修正；但不等同於瀏覽器登入或
  blank-NVS provisioning 流程已完成。

仍待驗證：

- 最新 artifact 在 blank-NVS／Recovery AP 的 scan、首次建密碼、login、
  401／CSRF、masked config 與 credential save／STA reconnect 全流程；
- Dashboard 在實機瀏覽器的 responsive／dark-mode 互動；
- SNTP 與 mDNS 狀態（目前 API 明確回傳 `unknown`／尚未整合）。

### 2026-07-30 — STA 管理 WebUI 與桌面版寬度驗證

本段在保留既有 Wi-Fi、`webfs`、`imagefs` 與其他 NVS 設定的前提下，
只重設管理帳號 `admin` 的密碼雜湊，並以正常 STA 模式驗證登入後的管理
介面。測試使用暫時密碼，測試結束後由使用者自行更換；密碼本身不寫入
原始碼、文件、log 或 Git。

驗證結果：

- 啟動後 health endpoint 回 `200`、`status=ready`；Flash、PSRAM、
  `webfs`、`imagefs` 與 Wi-Fi service 均為 ready/connected。原有網路設定
  未被清除，也沒有重建任何 filesystem。
- 實機登入成功。ESP32-S3 console 記錄
  `authentication_result=authenticated`，本次 PBKDF2 驗證約 136 秒，
  全程沒有 watchdog reset；因此 server-side login result 與 WebUI polling
  deadline 延長至 180 秒。
- 登入後 Dashboard 可載入 runtime snapshot，重新整理按鈕可完成一次
  refresh；Wi-Fi 頁在已連線 STA 模式可完成掃描並回傳去重後的結果。修正前
  非 provisioning mode 的 scan request 會停在 `scanning`，現已改為允許
  normal STA scan，其他不合法 mode 明確回報失敗。
- 桌面瀏覽器 viewport `1280` 下，頁面寬度 `1040`、sidebar `190`、
  authenticated content `822`，文件根節點 `scrollWidth` 小於 viewport，
  沒有水平溢出。未登入時隱藏 sidebar 會讓 content 跨滿 workspace，
  管理員登入卡片維持置中且不再被錯誤限制在第二欄。
- `image_02_05.png` 在本機檔案檢查為可解碼的 311×199 RGB PNG，未加入任何
  commit。Host image pipeline、quantizer、PFR1 packer 與 WebUI contract
  測試均通過；但本次 in-app browser 的 file chooser 只填入檔名、實際
  `FileList` 為空，畫面回報「無法讀取圖片」，所以不能把這次操作宣稱為
  瀏覽器端圖片成功處理。這是測試工具的檔案注入限制，需在可正常選檔的
  瀏覽器工作階段重跑圖片頁驗證。

仍待驗證：

- 最新 artifact 在 blank-NVS／Recovery AP 的 scan、首次建密碼、401／CSRF、
  masked config、credential save 與 STA reconnect 全流程；
- SNTP、mDNS、dark-mode 與可正常選檔的瀏覽器端 `image_02_05.png` PFR1
  產出／下載。

上面「Phase 5 imagefs transactional upload、catalog、斷電復原與圖片輪播」
一項已在後續 commit（`519b6c0`…`7ad7cbe`）完成程式與 host test，並在
`docs/IMPLEMENTATION_PLAN.md` checkpoint 標記完成；本檔先前未同步更新，
硬體長時間輪播與斷電後行為仍列為後續 acceptance 待驗證項，不是「完全
未實作」。

### 2026-07-31 — Phase 6 WeatherWorker／狀態列渲染：僅完成 host 驗證

本段（見 `docs/adr/0005-weather-worker-and-status-bar.md`）新增
NetworkServiceTask 最小 SNTP 啟動、`pf_weather_worker`
HTTPS fetch、`RuntimeSnapshot` 天氣欄位、Dashboard 天氣 JSON，以及
`pf_display` 的 bitmap font／weather icon／狀態列 renderer，並接線進
carousel 與 welcome frame。全部變更通過 `pio run`（韌體完整編譯，RAM
158,848 / 327,680 bytes 48.5%，Flash 1,193,713 / 2,621,440 bytes
45.5%）與 `pio test -e native`（192/192），`test_runtime_coordinator`／
`test_display_task` embedded test 以 `--without-uploading
--without-testing` 完成 build-only 驗證。**本段沒有任何實機（開機、
Wi-Fi、面板）驗證**，以下項目仍完全待補：

- SNTP 實機同步：`esp_netif_sntp_init` 是否真的在 STA 連線後於合理時間
  內取得有效牆鐘時間，`RuntimeSnapshot.time_sync` 是否正確轉為
  `synced`。
- WeatherWorker HTTPS 四種診斷狀態：API key invalid（401）、DNS 解析
  失敗、TLS handshake 失敗（含 `esp_crt_bundle_attach` 對
  `api.openweathermap.org` 憑證的實際驗證）、逾時；目前只有
  `classify_http_status` 的純邏輯分類有 host test，實際
  `esp_http_client` 行為完全未在硬體上跑過。
- `NetworkService::report_internet_state` 觸發後，`internet` 狀態是否
  真的從永遠 `unknown` 開始正確反映 WeatherWorker 的抓取結果。
- 狀態列在真實面板上的視覺結果：3x5 點陣字型、9 種天氣圖示、
  stale marker 在 800×40／480×40 status bar 上的實際可讀性——目前只有
  golden-vector 像素比對，沒有人眼在面板上看過。
- Dashboard `weather` JSON 區塊在瀏覽器實際顯示。
- welcome frame 疊加狀態列後，原本的邊框／`PF` 標記是否仍清楚可辨（新
  邏輯會覆寫最上方 40 列）。

開發過程中在 host test 階段抓到一個 `draw_line`（Bresenham 演算法）的
符號計算錯誤，會在向上方向畫線時造成無窮迴圈（太陽圖示的向上射線觸發），
已修正並加上步數上限防呆；記錄於此供未來類似 pure-geometry 程式碼審查
參考。

### 2026-07-31 — Phase 7 DHT22／光敏在場偵測：僅完成 host 驗證

本段（見 `docs/adr/0006-sensor-drivers-and-presence.md`）新增
`pf_sensors` 純邏輯 component（`EnvironmentCache`／`DailyStats`／
`MovingAverageFilter`／`PresenceTracker`）、移植自 `UncleRus/esp-idf-lib`
（commit `162af418d4702791fd3bf3e5d1577aea9ec5539c`，BSD-3-Clause）的
`pf_dht22` driver、新的 `pf_sensor_task`（DHT22 讀取＋ADC 光敏取樣＋
presence debounce）、`RuntimeSnapshot` 感測器欄位、Dashboard `sensors`
JSON、`GET /api/v1/sensors` 與 `/api/v1/sensors/config` 路由，以及
WebUI 環境頁。全部變更通過 `pio run`（韌體完整編譯，RAM
167,920 / 327,680 bytes 51.2%，Flash 1,213,025 / 2,621,440 bytes
46.3%）與 `pio test -e native`（225/225），`node --check data/web/ui.js`
通過。**本段沒有任何實機（DHT22、光敏電阻、實際 presence 轉換）驗證**，
以下項目仍完全待補：

- DHT22 實際讀值正確性：GPIO6 bit-bang 時序在真實 ESP32-S3 上是否穩定
  讀到有效溫濕度，`Dht22EnvironmentSensor` 對 `dht_read_float_data`
  各種失敗回傳（timeout、checksum mismatch、感測器未接）轉換出的
  `SensorStatus` 是否符合預期；目前只有 fake adapter 的介面契約有
  host test。
- 光敏電阻 ADC 實測：GPIO5／`ADC1_CH4` 在真實環境光下的原始讀值範圍、
  `MovingAverageFilter` 平滑後的數值是否適合作為 threshold 校正基準；
  預設 `light_threshold=2000` 未經實機校正。
- AWAY／PRESENT 實機轉換：真的遮蔽／露出光敏電阻後，`PresenceTracker`
  是否在設定的 `away_duration_s`（預設 180）／`return_duration_s`
  （預設 30）後正確切換狀態；saturated／error 讀值是否確實不觸發離席。
- 離席全白刷新與 sleep：`render_blank_frame` 送到面板後的實際視覺結果、
  刷新後 panel sleep 的電流量測。
- 返回後重繪與 deadline 重設：`CarouselScheduler::force_immediate` 觸發
  的下一輪 `poll()` 是否真的在返回後立即重繪目前圖片，而不是延用離席前
  殘留的 30 分鐘 deadline。
### 2026-07-31 — Phase 7 開機不斷重複重開機：main task stack 二次調大

前一版修復（`49263cc`）已將 `CONFIG_ESP_MAIN_TASK_STACK_SIZE` 從 ESP-IDF
預設 3584 調到 8192，並在實機確認解決了 `pf_storage::mount_all()` 附近
的 `InstrFetchProhibited` guru meditation。但實機重新開機驗證時，8192
仍不足，改為在 STA Wi-Fi 初始化之後崩潰，backtrace 落在
`vfs_littlefs_unlink` 附近，同樣是 main task stack overflow 的訊號
（Phase 7 新增的 sensor task 設定路徑在 `app_main` 上又多用了一些
stack）。本次調整：

- `CONFIG_ESP_MAIN_TASK_STACK_SIZE` 8192 → 16384。
- 新增 `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y`，往後若再次逼近
  stack 上限會直接以 watchpoint 觸發明確的 stack overflow panic，而不是
  overflow 覆寫回傳位址後跳到隨機指令位置（更難判讀的
  `InstrFetchProhibited`）。

實機重新開機測試已確認：開機不再重複重開機，流程可正常跑完
config／filesystem／runtime／netif／health server 啟動序列。

未待驗證項：16384 是否對 Phase 8（OTA、diagnostics）之後可能新增的
task 仍有餘裕；後續每次大幅擴充 `app_main` 起始序列（新增 task、大型
stack-allocated 結構）應重新檢查 high-water mark，不要只靠「這次夠用」
延伸判斷。

- WebUI 環境頁在瀏覽器的實際顯示與表單保存流程（`environment-form`
  submit → `POST /api/v1/sensors/config` → 重新載入）。

### 2026-08-01 — 首次建立 admin 密碼：第一次實機端到端驗證，發現並修復真實 bug

`docs/adr/0007-auth-pbkdf2-iterations-and-sync-login.md` 把登入從非同步
（背景 `AuthTask`）改成同步（PBKDF2 迭代次數同時降到 10,000）後，上傳到
實機測試首次建立 admin 密碼——這是本專案第一次真正在硬體上走到這條路徑
（先前每一筆相關 VALIDATION.md 紀錄都把它列為未驗證）。

- 韌體上傳：CH343 UART（COM11）沒有回應 ROM download handshake（與此板
  先前記錄的已知問題一致），改用 ESP32-S3 native USB（COM10）成功上傳。
- 開機驗證：`carousel_request=1 outcome=1` 於開機後約 32 秒出現，早於
  先前修復的 boot-loop 崩潰點，開機穩定。
- **首次建密碼測試**：在 WebUI 建立密碼頁輸入新密碼並送出，「驗證中」
  卡住超過一分鐘才回「登入失敗」；第二次嘗試同樣卡住超過一分鐘。
  根因：`perform_login()` 建密碼分支呼叫的
  `runtime_->lock_flash_display(portMAX_DELAY)` 與 `DisplayTask` 刷新
  面板時持有的是同一個 mutex；這段程式碼原本跑在背景 `AuthTask` 沒有
  影響，同步化後第一次真正卡住 HTTP handler。已修復為非阻塞
  `lock_flash_display(0U)`，拿不到鎖立即回 503，並修正前端訊息（見
  `docs/adr/0007` Consequences 段落）。修復後 `pio run`／
  `pio test -e native`（227/227）通過，並經 codex-cowork 兩輪審查
  （第一輪 High：bounded wait 仍違反「不等待面板刷新」規則，已改為
  完全非阻塞；第二輪阻斷性零問題）。

仍待驗證：

- 修復後的首次建密碼流程尚未在實機重新確認成功（含面板未在刷新時的
  正常路徑，以及刷新中重試後成功的路徑）；
- blank-NVS（完全清空 NVS）的真正首次開機路徑仍未驗證，本次是在既有
  NVS（有 Wi-Fi 憑證、無 admin 密碼）狀態下測試；
- Recovery AP、401／CSRF、masked config、credential save／STA
  reconnect 全流程。
