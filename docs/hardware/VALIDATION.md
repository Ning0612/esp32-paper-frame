# 硬體驗證紀錄

本檔是可公開的驗證摘要；公開版已移除本機 COM port、USB PNP ID、私人網路
位址、個人工作站路徑與其他裝置識別細節。這些刪節不改變測試結論；需要重現
測試時，請以目前硬體 profile、命令與 acceptance 描述為準。歷史段落保留時間
順序與測試結論；後續新增紀錄仍採 append-only，頂端索引代表目前狀態。

## Current unresolved hardware evidence（2026-08-20）

本檔以下內容是 append-only 的歷史證據；本節只整理目前仍未閉環的項目，
避免把早期「尚未驗證」誤讀成目前狀態，也避免把 host/build 結果當成實機
證據。新紀錄若與本索引衝突，依最新日期的驗證結果更新本節。

| 領域 | 目前仍待驗證 | 主要歷史證據／對照段落 |
| --- | --- | --- |
| Phase 7 sensors | DHT22 讀值、**兩個光敏通道各自的 ADC 校正**、AWAY/PRESENT 實機轉換、**「任一通道變暗即判定為暗」的實機行為**、白屏 sleep／返回重繪與環境頁 browser 行為 | 2026-07-31 Phase 7；2026-08-23 雙光敏通道；`docs/archive/IMPLEMENTATION_PLAN.md` Phase 7 checkpoint |
| AP grace policy | presence 例外（需感測器）、低 DMA heap guard（低優先；5 分鐘切換、SSID 可讀性與 AP/Wi-Fi 併發刷新均已於 2026-08-20 處理） | 2026-08-20 AP 併發刷新；2026-08-20 破壞性測試 |
| 設定降級邊界 | NVS 滿導致 `pf_config` 開啟失敗（低風險；`409 config_read_only` 已閉環，`nvs_flash_init()` 失敗經實測為不可觸發的防禦性分支） | 2026-08-20 破壞性測試；2026-08-20 設定降級邊界修正 |

已自本索引移除的項目：`active OTA upload wrapper`（2026-08-20 以 `ota_0`／`ota_1`
兩種 otadata 狀態各驗一次，寫入位址隨 active slot 改變）、`嵌入式 WebUI`
（2026-08-20 完成 webfs heap 差值量化）、`Phase 6 weather`（2026-08-20 關閉四種
失敗分類，面板狀態列視覺由使用者確認）、`Phase 5 storage` 與 `Phase 8 OTA`
（2026-08-20 完成斷電故障注入與 rollback fault injection）、`Phase 3/4 WebUI`
（2026-08-20 完成 SNTP 失敗側，該領域全數閉環）、`Phase 2 display`
（2026-08-20 完成 forced-BUSY 與 sleep 電流量測）。
`mDNS` 從未實作，不列為待驗證項——詳見 2026-08-20 段落。

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
| `<NATIVE_USB_PORT>` | `<USB_PNP_ID_REDACTED>` | Espressif USB Serial/JTAG；不是固定 port |
| `<OTHER_PORT>` | `<USB_PNP_ID_REDACTED>` | 非本專案 target；用途未在本專案判定 |

USB VID/PID 只能確認該 port 是 Espressif native USB 介面，不能單獨證明
開發板型號、Flash 容量或 PSRAM 模式。

### G1 驗證結果

- [x] `esptool` 讀取 chip family、revision、features 與 crystal。
- [x] `esptool flash-id` 讀取 Flash manufacturer/device/capacity。
- [x] 固定 PlatformIO profile；upload/monitor port 每次依 USB hardware ID 辨識。
- [x] 最小韌體 boot 並記錄 ESP-IDF、reset reason、Flash 實測容量。
- [x] 最小韌體以 capability heap API 驗證 PSRAM 可用容量。
- [x] 確認 native USB 在 ROM／應用程式模式可能重新枚舉，不固定為單一 port。

### Board A：G1 連線診斷

`esptool 5.3.0 --port <PORT> chip-id` 可開啟該 port，但 default reset、手動
BOOT/RESET 後的 no-reset，以及 native USB 的 usb-reset 都沒有收到 ROM
serial data。此結果可能涉及 boot strapping、reset control、USB 接孔／driver、
外接周邊或連線時序；未執行任何 erase/write。停止重複 esptool 嘗試，先用
N16R8 profile 完成 clean build，後續以持續按住 BOOT 或改接板上 UART USB
介面建立新證據。

### G1 UART 與 boot strap 診斷

後續改接板上 USB-to-UART adapter；adapter identifier 與本機 port 已省略。
正常啟動時該 adapter 可穩定收到 ESP32-S3 ROM
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

採用第 3 項後，另一塊同型板成功建立 ROM sync。Board A 的 transport
問題仍未歸因，不把另一塊板成功誤記為 Board A 已修復。

### Board B：G1 與 Phase 1 實機驗證

Board B `ESP32-S3-N16R8` 以板載 USB-to-UART adapter 進入 ROM bootloader；
PaperFrame 應用程式則使用 native USB Serial/JTAG。實際 port 編號已省略，
重新插拔後必須重新辨識。

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
- 當次連線辨識為 USB-to-UART adapter 與 ESP32-S3 native USB；adapter identifier
  與本機 port 已省略。
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
2. USB-to-UART adapter 仍未輸出 ROM 資料；ESP32-S3 native USB port
   則明確輸出：

   ```text
   boot:0x0 (DOWNLOAD(USB/UART0))
   waiting for download
   ```

3. 關閉占用 native USB port 的 monitor 後，`esptool --before no-reset` 經 native
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

PlatformIO 自動選到 native USB port，esptool
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
  port，只擦除 `0x10000`–`0x75fff`、寫入 414,960-byte 正式 app，
  data hash 驗證成功並 hard reset。bootloader、partition、OTA metadata、
  NVS、`webfs` 與 `imagefs` 均未改寫。

本次 `deep_sleep` 證據來自完整 command/result contract 與 driver state；
尚未以電流表量測面板 sleep 功耗。forced-BUSY 仍只在 fake driver 通過，
實機測試仍需隔離治具。

### 2026-07-30 — Phase 2 carousel welcome lifecycle 實機通過

輪播核心固定 30 分鐘預設、10 分鐘至 24 小時設定範圍，支援順序／隨機、新圖片
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
- 標準單命令 upload 自動選到 native USB port，只擦除
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
- native USB 單命令 upload 自動選到 port，只擦除
  `0x10000`–`0xebfff` 並寫入 898,000-byte app；data hash 驗證成功。
- 空白 credential 實機啟動 `PaperFrame-Setup-[masked]`，固定 IP
  `192.168.4.1`，DHCP server 成功啟動；log 未輸出 AP password 或任何
  STA credential。

第一次實機整合在 Wi-Fi 射頻啟動與電子紙 welcome 同時刷新時觸發
brownout。這項歷史競態目前以 AP presenter／carousel／presence blank 的
submission gate 序列化處理；AP page 在空圖片庫時持續接管面板，有圖片則
在 AP ready 後保留 5 分鐘再恢復圖片。重新燒錄後連續監看 30 秒未再重啟的
舊結果不等同於本次 AP grace policy 的實機驗證。
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
- 標準 PlatformIO 單命令 upload 自動選到 native USB port，只擦除
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
- 以 PlatformIO native USB app-only upload 部署正式韌體；USB-Serial/JTAG
  連線、app hash 驗證與 hard reset 均成功。
- 隨後使用 RTS-only reset 讀取啟動日誌，確認：

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
`docs/archive/IMPLEMENTATION_PLAN.md` checkpoint 標記完成；本檔先前未同步更新，
硬體長時間輪播與斷電後行為仍列為後續 acceptance 待驗證項，不是「完全
未實作」。

### 2026-07-31 — Phase 6 WeatherWorker／狀態列渲染：僅完成 host 驗證

本段（見 `docs/adr/0005-weather-worker-and-status-bar.md`）新增
NetworkServiceTask 最小 SNTP 啟動、`pf_weather_worker`（2026-08 已併入
`pf_weather`，component 名稱僅為當時撰寫時的名稱）
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
參考。（2026-08：`draw_line`／`draw_cloud`／`fill_circle` 等程序化繪製
機制已被 ADR-0013 移除，天氣圖示改為轉檔點陣圖 blit，本段落的
Bresenham 死角不再適用於目前程式碼，僅保留作歷史記錄。）

### 2026-07-31 — Phase 7 DHT22／光敏在場偵測：僅完成 host 驗證

本段（見 `docs/adr/0006-sensor-drivers-and-presence.md`）新增
`pf_sensors` 純邏輯 component（`EnvironmentCache`／`DailyStats`／
`MovingAverageFilter`／`PresenceTracker`）、移植自 `UncleRus/esp-idf-lib`
（commit `162af418d4702791fd3bf3e5d1577aea9ec5539c`，BSD-3-Clause）的
`pf_dht22` driver、新的 `pf_sensor_task`（DHT22 讀取＋ADC 光敏取樣＋
presence debounce）——`pf_dht22`／`pf_sensor_task` 2026-08 已併入
`pf_sensors`，component 名稱僅為當時撰寫時的名稱——、`RuntimeSnapshot`
感測器欄位、Dashboard `sensors`
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

- 韌體上傳：USB-to-UART adapter 沒有回應 ROM download handshake（與此板
  先前記錄的已知問題一致），改用 ESP32-S3 native USB 成功上傳。
- 開機驗證：`carousel_request=1 outcome=1` 於開機後約 32 秒出現，早於
  先前修復的 boot-loop 崩潰點，開機穩定。
這次測試連續發現並修好三個獨立問題，記錄下來避免下次重踩：

**Bug 1 — HTTP handler 卡住等面板刷新（>1 分鐘）**

在 WebUI 建立密碼頁輸入新密碼並送出，「驗證中」卡住超過一分鐘才回
「登入失敗」；第二次嘗試同樣卡住超過一分鐘。根因：`perform_login()`
建密碼分支呼叫的 `runtime_->lock_flash_display(portMAX_DELAY)` 與
`DisplayTask` 刷新面板時持有的是同一個 mutex；這段程式碼原本跑在背景
`AuthTask` 沒有影響，同步化後第一次真正卡住 HTTP handler。修復為非阻塞
`lock_flash_display(0U)`，拿不到鎖立即回 503，並修正前端訊息（見
`docs/adr/0007` Consequences 段落）。經 codex-cowork 兩輪審查（第一輪
High：bounded wait 仍違反「不等待面板刷新」規則，已改為完全非阻塞；
第二輪阻斷性零問題）。

**Bug 2 — HTTP server worker task stack overflow（Guru Meditation）**

修完 Bug 1 重新測試，這次「登入失敗」出現得很快，但緊接著面板刷新、
WiFi AP 關閉——實機 console log 顯示：

```text
Guru Meditation Error: Core  0 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered ()
```

根因：`start_health_server()` 用 `HTTPD_DEFAULT_CONFIG()` 沒有覆寫
`stack_size`，沿用 ESP-IDF 預設值（4096 bytes）。PBKDF2/PSA crypto
（`perform_login()`）原本專屬的 `AuthTask` stack 是 4096 words＝
16384 bytes；同步化後這段運算直接跑在 httpd worker task 上，4096 bytes
明顯不夠，overflow 觸發 `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK`
（見稍早的 boot-loop 修復，同一個防呆機制這次抓到了另一個 task 的
overflow）panic → reboot。「登入失敗」是連線在 panic 瞬間斷掉；面板
刷新是重開機後的正常開機畫面；AP 關閉是重開機時 WiFi 電台重置造成的
暫時斷線。

修復：`configuration.stack_size` 明確設為 24576（比對照的舊 `AuthTask`
16384 bytes 再留安全餘裕，因為 httpd worker 還要額外承擔
`esp_http_server` 自己的 request parsing／routing frame——經
codex-cowork 審查指出 16384 剛好等於舊預算、沒算進這部分開銷，才調高）。

**Bug 3 — `webfs` 沒有隨 `pio run --target upload` 更新，前端跑舊版**

修完 Bug 1／2 重新測試，「登入失敗」訊息還是出現，但**重新整理頁面後
卻能直接進入已登入畫面**。這個矛盾（後端顯然成功、前端卻回報失敗）
指向前端本身的問題：`data/web/ui.js` 打包進獨立的 `webfs` LittleFS
image，而一般的 `pio run --target upload` 只燒錄 app 韌體本體，不含
`webfs`／`imagefs`（見 `CLAUDE.md` 「Factory filesystem image」一節）。
這次 session 的所有 `ui.js` 改動（拿掉輪詢改單次 fetch、新增
409/401/503 訊息）從未真的送到裝置——裝置一直在跑舊版前端，還在等
已經被拿掉的 `202 + request_token` 回應形狀，收到新後端的 `200` 直接
成功回應時判斷失敗，丟出通用「登入失敗」訊息；但 `Set-Cookie` header
是在 HTTP 層生效，與 JS 是否正確解析 body 無關，所以 session 其實已經
建立，重新整理頁面就直接進去了。

修復：只重新建置＋燒錄 `webfs`（絕不動 `imagefs`，避免清空使用者圖片）：

```powershell
$env:IDF_PATH = "$PWD\.pio\packages\framework-espidf"
.\.pio\packages\tool-cmake\bin\cmake.exe --build .\.pio\build\paperframe-s3 --target littlefs_webfs_bin
.\.venv\Scripts\python.exe -m esptool --chip esp32s3 --port <COM> write-flash 0x510000 .pio\build\paperframe-s3\webfs.bin
```

`CLAUDE.md` 原本記載的 `cmake --build ... --target littlefs_webfs_bin`
命令若不先設定 `IDF_PATH`，會直接因為找不到
`/tools/cmake/project.cmake` 而失敗（PlatformIO 平常呼叫 cmake 時會
自動注入這個環境變數，直接手動呼叫 cmake 二進位檔則不會）；改好之後
只會有一個無害的 `ESP_ROM_ELF_DIR` gdbinit 警告，不影響建置結果。

修好 webfs 並強制重新整理瀏覽器頁面（避開快取的舊 `ui.js`）後，使用者
確認首次建立密碼流程正確運作，同步登入、CSRF、cookie 全部符合預期。

仍待驗證：

- blank-NVS（完全清空 NVS）的真正首次開機路徑仍未驗證，本次是在既有
  NVS（有 Wi-Fi 憑證、無 admin 密碼）狀態下測試；
- Recovery AP、401／CSRF 各種組合、masked config、credential save／
  STA reconnect 全流程；
- 面板刷新中送出首次建密碼（應該立即收到「裝置忙碌中」503，而非成功）
  尚未在實機刻意重現驗證。

### 2026-08-01 — 過度設計整併第二輪：pf_sensors／pf_weather 三合一與二合一

`pf_dht22`＋`pf_sensor_task` 併入 `pf_sensors`、`pf_weather_worker` 併入
`pf_weather`（namespace 統一、CMakeLists SRCS/REQUIRES 合併），並刪除死
抽象層 `pf_sensors::LightSensor`／`NullLightSensor`。純重構（搬檔案＋改
namespace＋改 CMakeLists），不改變任何執行期行為，`pio run` 與
`pio test -e native`（226/226）全綠。

過程中 codex-cowork 審查抓到一個真實問題：合併後 `pf_sensors`／
`pf_weather` 的 `SensorTask`/`WeatherWorker` 需要 `pf_runtime::
RuntimeCoordinator`，而 `pf_runtime` 本身又需要 `pf_sensors`／`pf_weather`
的純邏輯型別（`RuntimeSnapshot` 欄位），形成新的循環依賴——上一輪的三
元件切分（型別／driver／task 各自獨立）原本正是為了避免這個循環，這次
合併時沒注意到。修法：`pf_runtime` 這個相依在 `pf_sensors`／`pf_weather`
的 CMakeLists 改成 `PRIV_REQUIRES`（兩個元件的公開 header 都只是
forward-declare `RuntimeCoordinator`，`.cpp` 才需要完整定義），縮小
transitive 曝光範圍；ESP-IDF 6.0 目前仍可解析這個循環並成功建置，但屬於
刻意接受的架構債，未來若要徹底消除需要拆出 runtime 發布用的 adapter
介面。

`pf_carousel`／`pf_image` 折疊評估後放棄（見
`docs/archive/IMPLEMENTATION_PLAN.md` 對應記錄）：`pf_image` 已 `REQUIRES
pf_display`，若強行把 `pf_carousel` 折進 `pf_display` 會形成
`pf_display → pf_image → pf_display` 的環狀依賴，解法只有連 `pf_image`
一起吃進 `pf_display`，但 `pf_image` 同時被 `pf_storage` 大量依賴，屆時
`pf_storage` 會被迫背上 `pf_display` 的 SPI/EPD 硬體驅動相依鏈，用元件數
換來更糟的依賴方向，本輪維持現狀。

**未執行硬體重新驗證**：本輪是純重構（無邏輯變更），`SensorTask`／
`WeatherWorker` 的行為與上一輪已驗證版本相同，理論上不需要重新開機驗證；
但尚未實際重新燒錄／開機確認 DHT22、光敏電阻、天氣抓取三個 task 在新的
元件邊界下仍正常啟動，留待下次有硬體在手時一併確認。

### 2026-08-01 — Phase 8 診斷／OTA／System 頁：僅完成 host 驗證

本段（見 `docs/adr/0008-ota-github-releases-and-rollback.md`）新增
`pf_runtime` 診斷 ring buffer（`diagnostics_event.hpp`）、reboot reason
分類（`reboot_reason.hpp`）、韌體版本比對（`firmware_version.hpp`）、
queue/lock 失敗計數器（與對應診斷事件在同一 critical section 內原子
關聯）、`schedule_reboot()`（timer 於 `RuntimeCoordinator::initialize()`
單執行緒建立，避免跨 task 首次建立的競態）；新 component `pf_ota`
（GitHub Releases 版本檢查與 `esp_https_ota` stepwise 下載，admin
觸發、無背景輪詢）；`pf_web` 新增 `GET /api/v1/events`、
`POST /api/v1/system/reboot`、`POST /api/v1/system/recovery-ap`、
`GET/POST /api/v1/system/ota/*`；`pf_network` 新增
`request_recovery_ap()`；Dashboard `current_image`/`next_refresh_ms`
改為真實值；WebUI 新增「系統」頁。全部變更通過 `pio run`（RAM
232,944 / 327,680 bytes 71.1%，Flash 1,240,681 / 2,621,440 bytes
47.3%）與 `pio test -e native`（265/265），`node --check data/web/ui.js`
與 `test/web/test_system_ui_contract.mjs` 通過，
`test_embedded/test_runtime_coordinator` 以 `--without-uploading
--without-testing` 完成 build-only 驗證。經 4 輪 codex-cowork 審查
（診斷/OTA 3 輪、Dashboard/System 頁 1 輪）收斂至阻斷性零問題。
**本段沒有任何實機（開機、Wi-Fi、面板、真實 OTA）驗證**，以下項目仍
完全待補：

- **RAM 餘裕**：`pf_ota` 的 24576-byte task stack 與 8192-byte GitHub
  API response buffer 是本次 RAM 用量從 61.0% 跳到 71.1% 的主因（純
  靜態配置，不論是否觸發過 OTA 都佔用）。`OtaWorker::task_main()` 已在
  每次命令處理完後 log `ota_worker_stack_free_bytes`，需在真機上分別
  跑過「檢查更新」與「立即更新」兩條路徑，確認實際 high-water-mark
  剩餘空間 ≥ 4KB（比照本專案既有的 httpd worker stack 校準基準）；若
  不足需重新評估配置策略，不能只靠這次的靜態估算放行。
- **GitHub 連線與版本比對**：真實 HTTPS 連線到
  `api.github.com`／`github.com`（含 `esp_crt_bundle_attach` 憑證驗證
  是否成功）、`tag_name` 解析與 `compare_semver` 對真實 release tag
  的判斷結果、GitHub API rate limit（403）情境下 `ota_check_state`
  是否正確回報 `check_failed`。
- **真實 OTA 下載與 rollback**：完整走一次「檢查更新→立即更新→自動
  重開機→開機後 `esp_ota_mark_app_valid_cancel_rollback()` 確認」流程；
  刻意發布一個會在確認呼叫之前就 crash-loop 的版本，驗證
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 是否真的讓 bootloader 自動
  切回舊分割區；OTA 寫入過程中斷電，確認開機後仍在舊版有效分割區，
  `webfs`／`imagefs` 完全未受影響。
- **無網路時的優雅失敗**：裝置僅有 provisioning AP、無 Internet 時按
  「檢查更新」是否正確回報失敗而非卡住或崩潰。
- **Reboot／Recovery AP 端到端**：`POST /api/v1/system/reboot` 是否
  真的在收到回應後約 500ms 重開機且保留設定／圖片／順序／目前圖片；
  `POST /api/v1/system/recovery-ap` 是否真的讓已連線 STA 的裝置切換
  回 AP 模式，且符合 `docs/PROVISIONING.md` 既有的 auth 合約（scan 需
  登入、config 需登入+CSRF）。
- **併發 heap 風險**：`pf_weather`（GitHub API 之外的另一個獨立 HTTPS
  client）與 `pf_ota` 理論上可能同時活躍；需實機刻意同時觸發天氣抓取
  與 OTA 下載，量測 heap 低點，確認沒有因為併發 TLS session 而 OOM。
- **`/api/v1/events` 在真實 queue 飽和下的行為**：連續觸發多次 carousel
  refresh 讓 4-slot command queue 溢出，確認
  `command_queue_rejected_count` 與對應診斷事件都正確出現在
  `/api/v1/events` 回應中。
- **System 頁瀏覽器實際行為**：四個操作按鈕（重新啟動、強制配網 AP、
  檢查更新、立即更新）的 `window.confirm()` 二次確認、CSRF 是否正確
  夾帶、按鈕點擊後裝置斷線期間的 UI 狀態是否合理（非誤導成「失敗」）。
- **既有回歸（與 Phase 8 無關，附帶發現，已修正）**：
  `test/web/test_image_download_contract.mjs` 曾在本分支基底 commit
  （`b79c29b`，屬於 `fix/auth-simplify-network-merge`）斷言失敗，
  與 upload keep-alive 重構把 `close_session` 判斷式改寫成呼叫
  `drain_image_upload_body(...)` 但測試字面比對未同步更新有關；現行
  contract 已改為檢查 drain 行為，並由 GitHub CI/release workflow 執行。

### 2026-08-01 — 發現 `pio run -t uploadfs` 會把 webfs 誤燒進 imagefs

**根因**：`.pio/platforms/espressif32/builder/main.py` 的
`fetch_fs_size()` 解析 `partitions/paperframe-dev.csv` 時，逐筆覆寫、
用**最後一個** `type=data` 且 `subtype` 屬於 `spiffs/fat/littlefs` 的
分割區當作 `uploadfs` 的寫入目標；`webfs`／`imagefs` 都是 `spiffs`
subtype，`imagefs` 排在 CSV 後面，所以 PlatformIO 選到的目標 offset 是
`imagefs`（`0x630000`）。但 `DataToBin` builder 的內容來源固定是
`$PROJECT_DATA_DIR`（頂層 `data/`，這個專案裡只有 `data/web/*`）。兩者
疊加的結果：每次跑 `pio run -t uploadfs`（任何 env）都會把 WebUI 檔案
寫進 `imagefs` 的位置，等於用前端資產覆蓋使用者圖片儲存區。這就是先前
「每次燒完好像都要重新傳圖片」的實際原因，不是預期行為。

**處理**：
1. 用 `littlefs_imagefs_bin`（空白 image）＋
   `esptool ... write-flash 0x630000` 清空被誤燒污染的 `imagefs`
   （native USB，10,289,152 bytes，寫入與 hash 驗證皆通過）。
2. 新增 `scripts/flash-app-and-webfs.ps1`：只呼叫 `pio run -t upload`
   （app-only，經 `tools/platformio_native_usb_upload.py` 裁剪成純
   app 燒錄）＋ `littlefs_webfs_bin` 重建 ＋ `esptool write-flash
   0x510000` 手燒 `webfs`，全程不呼叫 `uploadfs`，也不碰 `imagefs`。
3. 實機驗證：native USB 上完整跑一次腳本，三步驟（app upload 14.47s、
   webfs 重建 8 個檔案、`write-flash 0x510000` 3.0s）全部成功，
   exit code 0；`nvs`（`0x9000`，管理密碼／Wi-Fi 憑證／sensor／weather
   設定）不在這次任何寫入 offset 範圍內，確認未受影響。
4. `CLAUDE.md` 已加入對應警告與腳本使用說明（本檔案未納入版控，
   `.gitignore:29`）。

**結論**：往後一律用 `scripts\flash-app-and-webfs.ps1`；`pio run -t
uploadfs` 不得用於這個專案的任何 environment。imagefs 目前是空的，
待重新從 WebUI 上傳圖片。

### 2026-08-01 — Phase 8 實機首測：觸發「強制進入配網 AP」導致 Guru Meditation 崩潰迴圈

上述 webfs/imagefs 問題排除後，使用者以本次 Phase 8 build 實機測試，
連上既有 STA 後觸發新的「強制進入配網 AP」功能（WebUI 系統頁按鈕，
對應 `POST /api/v1/system/recovery-ap` →
`NetworkEvent::enter_recovery_ap`）。這是 `NetworkAction::start_ap`
第一次在 STA 已連線狀態下被呼叫——先前僅有的兩個呼叫端（blank-NVS
首次開機、STA 重試耗盡後 fallback）都是從「STA 未連線」狀態進入，
從未在有作用中連線時測過。

**症狀**：console log 顯示 `provisioning_screen_ready request=2` 後，
WiFi 切換到 `sta + softAP` combo 模式，接著兩次
`wifi:alloc eb len=752 type=4 fail`（752-byte DMA-capable buffer 配置
失敗），緊接 `Guru Meditation Error: Core 0 panic'ed (LoadProhibited)`，
`EXCVADDR: 0x0000002c`，之後持續重複重開機。

**根因分析**：用本機 `xtensa-esp32s3-elf-addr2line`
（`.pio/packages/toolchain-xtensa-esp-elf/bin/`）對照 SHA256 完全吻合
崩潰韌體的 `.pio/build/paperframe-s3/firmware.elf`，backtrace 解出：

```text
ieee80211_hostap_attach ← wifi_softap_start ← _do_wifi_start
  ← wifi_start_process ← ieee80211_ioctl_process ← ppTask
```

全部落在 Espressif 閉源 WiFi blob 內，無法從應用層修補。
`EXCVADDR=0x2c`（極低位址，典型 null pointer + 小 offset）搭配前一行
的配置失敗 log，指向「752-byte DMA buffer 配置失敗時，blob 內部沒有
正確處理失敗、繼續解參考一個 null 指標」。**這是高可信度假說，非已
證實根因**——沒有 allocation-failure hook 或 heap trace的實測資料能
直接證明，只有 backtrace 與 log 時序上的相關性。Phase 8 新增的靜態
配置（尤其 `pf_ota` 的 24576-byte task stack）把可用 internal DMA
heap 從原本的餘裕壓縮到臨界值，很可能是促成這次崩潰的因素之一，但
也可能這條路徑本來就從未在任何 heap 水位下測過。

**修正**（經 3 輪 codex-cowork 審查收斂）：

1. `NetworkService::start_access_point()` 在**函式最開頭、
   `esp_wifi_stop()` 成功之後、觸碰任何 `esp_wifi_set_mode`／
   `esp_wifi_set_config` 之前**，新增
   `heap_caps_get_largest_free_block(MALLOC_CAP_DMA)` 檢查（門檻暫定
   40KB，**未經實機校準的保守猜測值**）。低於門檻時記錄診斷事件
   （`ap_start_low_heap`）並回傳 `ESP_ERR_NO_MEM`，driver 保持單純
   stopped 狀態（不會有「設定已改但沒 start」的中間態疑慮）。這個失敗
   會經由既有的 `perform_action_chain()` → `ap_start_failed` →
   `NetworkStateMachine::handle()` 走進本來就存在的重試（最多 3 次，
   每次間隔 1 秒）／降級邏輯，不是新的錯誤處理路徑，同時也保護了另外
   兩個既有呼叫 `start_ap` 的路徑。**誠實聲明這個修正的邊界**：只把
   「不可恢復的崩潰」換成「有界的優雅失敗」，不保證 AP 一定能成功
   啟動——1 秒重試只有在其他 task 剛好釋放 DMA heap 時才可能有幫助，
   持續性的靜態記憶體壓力不會在重試後自行消失。
2. 曾嘗試把 `pf_ota::OtaWorker::github_response_buffer_`（8192 bytes）
   從靜態 internal-DRAM array 改成 PSRAM 配置以騰出 heap 空間，但
   codex-cowork 審查指出：同一個 task 也執行 `esp_https_ota` flash
   寫入，PSRAM 在 flash cache-disable 期間的存取安全性未經驗證，屬於
   本專案過去吃過虧的同一類「未充分推導記憶體/時序交互作用」風險。
   **已完全撤回這項變更**，`github_response_buffer_` 改回原本的
   static internal-DRAM array；RAM 使用率因此維持在 71.1%
   （232,944 bytes）而非降到 68.6%——刻意的取捨，不用未經驗證的風險
   換 8KB。
3. 審查過程中另外發現一個獨立的真實 bug：`OtaWorker::
   request_check_for_update()`／`request_update_now()` 若在
   `OtaWorker::start()` 從未成功（`task_handle_` 為 null）時被呼叫，
   會回傳 `true`（誤導呼叫端）且把 `busy_` 永久卡在 `true`（沒有 task
   會把它重置），導致之後所有請求永久被拒。已在兩個函式開頭加上
   `task_handle_ == nullptr` 檢查修正。

`pio run`（RAM 回到 71.1%／232,944 bytes，Flash 47.3%）與
`pio test -e native`（265/265）皆已通過；
`test_embedded/test_runtime_coordinator` 以 `--without-uploading
--without-testing` 完成 build-only 驗證。

**2026-08-01 實機複測結果（修正確認）**：重新燒錄後在已連線 STA 狀態下
重新觸發「強制進入配網 AP」，console log：

```text
E (45861) pf_network: ap_start_low_heap free_dma_bytes=2671 largest_free_dma_block=1728 minimum_bytes=40960
E (45861) pf_network: network_action_failed action=3 error=ESP_ERR_NO_MEM
```

連續 3 次（每次間隔 1 秒，符合 `kActionRetryDelayTicks`／
`maximum_ap_attempts_=3`），之後進入 `NetworkStateMachine` 的
`enter_failed()`。**確認結果**：

- (a) **不再崩潰、不再重開機迴圈**——heap guard 生效，修正核心目標達成。
- (b) 40KB 門檻遠高於實際：real largest free DMA block 只有
  **1728 bytes**、total free DMA heap 只有 **2671 bytes**，比 40KB
  低了一個數量級以上。這代表「已連線 STA 時強制進入配網 AP」這個功能
  **目前實質不可用**——不是門檻設定過嚴的問題，是系統整體在這個時間點
  （開機約 45 秒、STA 已連線）DMA-capable heap 餘裕已經被壓縮到極度
  危險的水位，就算把門檻降到遠低於 40KB，esp_wifi_start() 實際需要的
  總配置量（beacon buffer 之外還有 AP 介面的 RX/TX buffer、netif
  結構等）很可能還是拿不到足夠連續空間。
- (c) 優雅失敗路徑確認正確：`network_action_failed action=3
  error=ESP_ERR_NO_MEM` 三次後靜止（進入 `failed` 狀態），沒有再次崩潰
  或無限重試。

**後續影響評估**：這次 heap guard 修正的直接目標（防止崩潰）已達成並
實機驗證；但揭露了一個範圍更大的既有問題——本裝置在正常執行時（開機
45 秒、STA 已連線）的 DMA-capable heap 餘裕遠低於「安全啟動 AP+STA
combo 模式」所需，這不是 Phase 8 單獨造成的（Phase 7 baseline 在這個
分支本來就已經有相當高的靜態 RAM 使用率），但 Phase 8 的新增配置
可能讓餘裕更緊繃。要讓「強制進入配網 AP」在已連線 STA 狀態下真正可用
（而不只是安全地拒絕），需要全系統範圍的 RAM 稽核與縮減，不是調整
單一門檻數字可以解決，列為後續獨立工作項目。

## 2026-08-02 — PFR1 壓縮 payload 顯示路徑：尚未實機驗證

`docs/adr/0009-pfr1-payload-compression-and-catalog-cap.md` 引入的 PFR1
`compressed` flag（raw DEFLATE payload，ESP32-S3 ROM miniz
`tinfl_decompress_mem_to_mem` 解壓縮）已接到 carousel 顯示路徑
（`Pfr1FrameDecoder`／`render_carousel_image`，`src/app_main.cpp` 新增
`carousel_inflate_buffers` 這對 PSRAM scratch buffer）。目前狀態：

- **Host 測試已覆蓋**：`test/test_image_frame` 新增
  `test_compressed_file_decodes_and_composes_same_as_uncompressed`
  （壓縮與未壓縮輸入組出逐 byte 相同的 framebuffer）、
  `test_compressed_file_without_inflate_buffers_fails_closed`、
  `test_corrupt_compressed_stream_fails_closed_without_partial_frame`
  （損毀壓縮流不會寫出半殘 framebuffer），三個都通過；`pio run
  -e paperframe-s3` 也確認連結到真實 ROM miniz 符號成功。
- **沒有 `test_embedded`（真機）覆蓋**：`test_embedded/` 目前只有
  `test_display_task`、`test_epd7in3e_driver`、`test_runtime_coordinator`
  三個套件，皆不涉及 PFR1 解碼——這條「壓縮 PFR1 檔案→inflate→組框
  →SPI 刷新」的路徑完全沒有 on-device 驗證。
- **真機面板顯示尚未驗證**：上傳一張真的壓縮 PFR1（目前只能用測試腳本手
  動產生，因為瀏覽器端壓縮要到 commit 7 才實作）、確認 carousel 正確在
  真實 e-Paper 面板上刷新、且視覺內容與同一來源圖片的未壓縮版本一致。
- **inflate 對刷新延遲的影響尚未量測**：`tinfl_decompress_mem_to_mem`
  在真實 ESP32-S3 時脈下對一張最大 182,400 bytes 的 payload 解壓縮要花
  多少時間、是否會讓 carousel 換圖出現可感知的延遲，目前只有 host（PC
  CPU）上跑測試的間接印象，沒有真機量測數字。
- **`carousel_inflate_compressed`/`carousel_inflate_output` 這兩份新增
  PSRAM buffer（各 182,400 bytes）與 `StorageWorker` 那兩份加總後對
  PSRAM 總占用的影響**：`pio run` 的 RAM/Flash 百分比不含 PSRAM heap
  配置，需要實機用 `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` 之類的
  API 確認開機後與長時間執行後的 PSRAM 餘裕仍然健康。

待補測項目（有硬體時執行）：
1. 燒錄含本 ADR 全部 commit 的韌體，確認開機 log 沒有
   `carousel_inflate_scratch_alloc_failed`（PSRAM 配置成功）。
2. 用測試腳本產生一個壓縮 PFR1，透過既有的圖片上傳流程（或直接寫入
   `imagefs`）讓它進入 catalog，觸發 carousel 顯示，肉眼比對面板畫面與
   同一來源圖片的未壓縮版本是否一致。
3. 量測該張圖從「carousel 決定要換圖」到「面板刷新完成」的耗時，與同一
   張圖的未壓縮版本比較，確認 inflate 沒有造成有感延遲。
4. 長時間跑 carousel（多次換圖，含壓縮與未壓縮混合），觀察 PSRAM 餘裕
   曲線，確認沒有洩漏或碎片化問題。

## 2026-08-02 — PFR1 壓縮＋目錄容量上限：完整功能驗證狀態彙整

`docs/adr/0009-pfr1-payload-compression-and-catalog-cap.md` 的 6 個實作
commit（pf_image 壓縮 payload 驗證、PFC1 驗證放寬、ingest/recovery 接入、
display path 接入＋既有 bug 修正、目錄上限 48→96、瀏覽器端壓縮）已全部
完成並各自通過 codex-cowork 審查（阻斷性零問題）。彙整目前驗證狀態，收斂
本次功能的 VALIDATION.md 紀錄。

### 已驗證（自動化）

- **Host test**：`pio test -e native` 全數通過（含本次新增／擴充的
  `test_pfr1_validator`、`test_catalog`、`test_image_store`、
  `test_storage_worker`、`test_image_frame`，以及回歸確認過的其餘既有
  套件）。
- **Embedded 編譯**：`pio run -e paperframe-s3` 每個 commit 落地前都跑過
  一次，確認連結到真實 ESP32-S3 ROM miniz 符號成功；最終 flash 使用率
  47.4%（含全部本次異動），internal DRAM/BSS 使用率因目錄上限拉高從
  48.6% 漲到 62.6%（見 2026-08-02 較早的那筆紀錄，含詳細成長來源拆解），
  仍有 37% 餘裕。
- **瀏覽器端（Node 模擬）**：`node test/web/test_pfr1_packer.mjs`（7 個
  測試，含壓縮生效、壓縮環境不可用時退回未壓縮、拒絕呼叫端主張壓縮
  flag、cross-language golden compressed vector）與既有
  `test_image_pipeline.mjs`／`test_image_quantizer.mjs`／
  `test_image_ui_contract.mjs` 皆通過、無回歸；`node --check` 對全部有
  異動的 `data/web/*.js` 檔案通過。
- **Cross-language 一致性**：C++（`test_pfr1_validator`）與 JS
  （`test_pfr1_packer.mjs`）各自用同一組 `zlib.deflateRawSync`
  construction 產生的固定 188-byte 壓縮 golden vector，獨立確認能正確
  解壓縮回 176,000 bytes 全白 payload 且 CRC32 相符——證明韌體端（ROM
  miniz／host zlib）與瀏覽器端假設的 raw DEFLATE framing 一致。

### 實機驗證結果（2026-08-02，同日追加；⚠️ 本節全部是 Phase 1 RAM 重構
**之前**的歷史紀錄，`kCatalogMaxEntries` 目前的值與現況請見下方
「Phase 3」那筆紀錄與 [ADR-0012](../adr/0012-raise-catalog-cap-after-ram-reclaim.md)，
不要把本節任何「已改回 48」「目前」等描述當作現在的狀態）

在拿到硬體後實際燒錄（`scripts\flash-app-and-webfs.ps1`）並透過 WebUI
操作，發現並修正了一個 `pio run` 靜態報告完全看不出來的真實問題，詳見
[ADR-0010](../adr/0010-revert-catalog-cap-raise-ram-constraint.md)：

- **`kCatalogMaxEntries` 拉到 96（甚至 64）會讓圖片上傳／mutation
  （activate／reorder／delete）的 task stack 動態配置在真機上失敗**
  （`deferred_task_stack_alloc_failed task=pf_image_mut bytes=24576`）。
  當時改回 48（原始基準），實機重新驗證：
  - 上傳成功，`image_upload_stack_free_bytes=2116~2120`（與 Phase 8
    時期記錄的 ~6,864 bytes 餘裕基準同數量級，仍偏薄但穩定）。
  - 設為目前／刪除（mutation task）成功，
    `image_mutation_stack_free_bytes=10020~10324`（比 Phase 8 記錄的
    ~2,096 bytes 基準餘裕更健康，此次測試時 WiFi/heap 狀態較寬鬆）。
  - 這代表原先「尚未驗證：真機開機 stack high-water-mark」與「尚未驗證：
    接近 96 筆容量時的行為」兩項風險，其中後者已經因為撤銷 96 而不再
    適用（catalog 上限與其相依的靜態 RAM 預算回到拉高前基準，不需要另外
    驗證「接近上限」情境；但本次功能仍新增了 PFR1 壓縮相關的程式碼路徑，
    不是「完全沒有異動」）；前者則已用 upload/mutation task 各自的
    free-bytes log 間接驗證過健康，但尚未逐一檢查 `app_main` 主線與
    `esp_http_server` 其他 worker task 的 high-water-mark（見下方仍待驗證
    清單）。
  - **（Phase 1 RAM 重構前）**只實測過 48／64／96 三個數字：48 是唯一
    已實測穩定的設定，64 與 96 都已實測失敗（64 筆時上傳勉強成功但
    mutation 失敗；96 筆時上傳本身就失敗）。49–63 之間沒有實測過，不能
    推論哪個數字是實際的臨界點。**這個結論的前提是 Phase 1 之前的 RAM
    預算**；Phase 1 回收 57.8KB 之後重新測試，64 已經穩定通過，見下方
    2026-08-02「Phase 3」那筆紀錄與
    [ADR-0012](../adr/0012-raise-catalog-cap-after-ram-reclaim.md)，
    不要把這裡的舊結論當作目前狀態。
- **真實瀏覽器上傳流程端對端驗證成功**：使用者透過實際瀏覽器（非測試
  腳本）連上 WebUI、上傳圖片、設為目前、刪除，整個流程在 48 筆基準下
  多次重複皆成功，過程中沒有出現 `Failed to fetch` 或
  `upload_unavailable`（這兩個錯誤先前在 96 筆／64 筆版本上分別重現過，
  確認與 `kCatalogMaxEntries` 直接相關，不是隨機的網路問題）。
- **壓縮確實生效的端對端確認（追加，同日）**：使用者透過真實瀏覽器上傳
  3 張圖片，回報壓縮後 PFR1 檔案大小分別為 28 KB、46 KB、63 KB，顯示
  正常無異狀。三個數字都遠小於未壓縮的固定大小（landscape 176,000
  bytes／172 KB、portrait 182,400 bytes／178 KB）——換算省下約
  63%～84% 空間（63 KB／172 KB ≈ 省 63%；46 KB／172 KB ≈ 省 73%；
  28 KB／172 KB ≈ 省 84%），比先前用合成測試圖估計的
  floyd-steinberg/atkinson 保守情境（省 40–47%）好上不少，且由於未壓縮
  PFR1 payload 大小是固定常數，這三個檔案明顯小於該常數本身就是
  `Pfr1Flags.compressed` 有被瀏覽器端設定、且韌體端正確解壓縮顯示的
  間接證據，不需要另外用瀏覽器開發者工具逐位元組核對 flag bit。
  **仍未核實的部分**：沒有記錄這三張圖各自使用的 dithering 模式，也沒有
  跟同一張來源圖片的未壓縮版本做逐像素或肉眼並排比對（只確認「顯示正常
  無異狀」，不是「與未壓縮版本完全一致」）。

### 尚未驗證（需要實機，明確列為風險，不得視為已完成）

以下項目在拿到硬體前都無法用 host test 或 `pio run` 涵蓋，任何一項在
交付前都不能被略過：

1. **真機開機 stack high-water-mark（全面）**：目前只確認了 upload／
   mutation 這兩個「有請求才配置」的 task 在 48 筆基準下餘裕健康；
   `app_main` 主線、`esp_http_server` 其他 worker task、新增的
   `carousel_inflate_compressed`/`carousel_inflate_output` PSRAM buffer
   的配置成功與否，都還沒有逐一在真機 log 上確認過。
2. **carousel 顯示延遲量測**：inflate 步驟是否讓壓縮圖片的 carousel
   換圖出現可感知延遲，尚未實際量測，也沒有跟未壓縮版本比較。
3. **斷電／recovery 對壓縮圖片的處理**：模擬交易中斷電，確認壓縮圖片的
   candidate 能在重開機後正確驗證並復原（host test 已用 fake filesystem
   驗證邏輯正確，但真實 LittleFS 斷電行為需要真機確認）。

以上 3 項若在下一輪有硬體時仍未執行，必須繼續留在本檔案的待驗證清單，
不得從交付說明中省略。

## 2026-08-02 — PSRAM／flash cache-disable 交錯安全性：原始碼層級已驗證

上方 2026-08-01 OTA 那筆紀錄提到「PSRAM 在 flash cache-disable 期間的
存取安全性未經驗證」，當時只是 OTA 那次撤回的個案結論，沒有針對本專案
其他已上線的 PSRAM 用法（PFR1 壓縮功能）做過對照審查。RAM 重構過程中
補做了這個原始碼層級的追蹤，完整記錄見
[ADR-0011](../adr/0011-psram-flash-cache-disable-safety.md)。

結論：`pf_image_up`/`pf_image_mut`（上傳／mutation 寫入路徑）、
carousel 顯示（`carousel_payload`/`carousel_status`/
`carousel_inflate_*`）與開機復原（`recovery.cpp` 的 `read_image`）三條
讀寫路徑都**不會**在 flash cache-disable 期間存取 PSRAM——三條路徑都
先把資料經過 stack buffer 中轉（且各自所在的 task stack 已確認為
internal DRAM），PSRAM scratch buffer 只在 `Pfr1Validator`／
`Pfr1FrameDecoder` 內部被存取，且跟任何 flash I/O 呼叫在時間上不重疊；
`esp_flash_write()`／`esp_flash_read()` 本身也有對稱的
`esp_ptr_in_dram()` 防禦層兜底，**但這層防禦僅在本專案內部主 flash 走
`SPI1_HOST`／memspi driver 這個實際硬體配置下成立**，不是所有 flash
host 的通用保證（細節見 ADR-0011）。**不需要**把這些 PSRAM buffer 改回
internal RAM。

未做的部分（明確列為未涵蓋，不是遺漏）：cache-disable 視窗的精確時長
（微秒等級）沒有用示波器/邏輯分析儀實測量測；連續壓縮上傳＋獨立
checksum 核對的實機壓力測試也沒有執行。這兩項**維持在待驗證清單**，
ADR-0011 的原始碼層級結論可以成立，但不能取代實機量測與壓力測試；若
未來相關程式碼的資料流架構或 flash host 配置改變（例如 PSRAM 指標開始
被直接傳給 `filesystem.write()`/`filesystem.read()`，或改用非
SPI1_HOST 的 flash host），必須重新評估，不能直接沿用本結論。

## 2026-08-02 — Phase 1 RAM 回收（1a/1b/1c）：實機驗證第一輪

燒錄含 Phase 1a（`RecoveryWorkspace` 開機暫時配置）、Phase 1b
（weather/sensor config task stack 按需配置）、Phase 1c（`carousel_status`
PSRAM＋fallback）三個 commit 的韌體到實體 ESP32-S3-N16R8，累計回收
57,824 bytes internal DRAM（見 ADR-0010 之後的 RAM 重構計畫）。實機 log
擷取：

```text
I (32541) paperframe: carousel_request=1 outcome=1 next_due_ms=1831847
I (70301) pf_web: image_upload_stack_free_bytes=2120
I (77621) pf_web: image_mutation_stack_free_bytes=10324
I (78911) paperframe: carousel_image_queued id=7 request=2
I (109031) pf_web: image_mutation_stack_free_bytes=10020
I (110991) paperframe: carousel_request=2 outcome=1 next_due_ms=1910297
```

**已確認**：

- **上傳／mutation task stack 餘裕未受影響**：`image_upload_stack_free_
  bytes=2120`、`image_mutation_stack_free_bytes=10324`/`10020`，跟
  ADR-0010 原始 48 筆基準（上傳 2116~2120、mutation 10020~10324）幾乎
  完全一致——證實 Phase 1 回收的 57.8KB 沒有換到這兩個既有 task 的餘裕，
  是從別的地方（`RecoveryWorkspace`、weather/sensor config 常駐 stack、
  `carousel_status`）騰出來的，符合 Phase 1 的設計目標。
- **WiFi 連線後的上傳／切換（activate）／刪除圖片流程**：使用者透過
  WebUI 實際操作，回報「看起來沒問題」，跟上面的 stack free-bytes log
  對應一致。
- **carousel 顯示（Phase 1c 相關）**：`carousel_request` 兩次
  `outcome=1`（成功）、`carousel_image_queued id=7` 成功排入刷新佇列，
  代表 `render_carousel_image()` 用到的 `carousel_status`（Phase 1c
  改成 PSRAM＋fallback 配置）在真機上正常運作，沒有出現
  `carousel_status_alloc_failed` 或顯示失敗。

**尚未涵蓋**（第一輪時的狀態，第二輪已補上，見下一節）：

1. Phase 1a：重開機後，既有圖片與目錄內容是否還在、驗證通過。
2. Phase 1b：`weather_config`/`sensor_config` 表單在冷開機後第一次送出
   是否成功。
3. 上方既有清單第 1 項（`app_main` 主線與其他 worker task 的完整
   high-water-mark）本輪仍未逐一確認，繼續留在待驗證清單。

## 2026-08-02 — Phase 1 RAM 回收（1a/1b）：實機驗證第二輪，補齊剩餘項目

用即時序列監看（native USB console）盯著使用者操作，補測第一輪
遺漏的兩項。

**Phase 1a — 重開機復原**：使用者觸發實體重置，log 擷取：

```text
rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
...
I (699) main_task: Calling app_main()
I (779) paperframe: storage_worker_ready recovery=none action=no_change
```

`recovery=none action=no_change` 代表 `StorageWorker::start()` 開機
復原路徑跑完，沒有偵測到需要 rollback 的中斷交易，也沒有觸發
`recovery_workspace_alloc_failed`／`storage_worker_start_failed`——確認
Phase 1a 把 `RecoveryWorkspace` 改成開機暫時配置（用完即釋放）之後，
復原邏輯本身沒有壞。使用者隨後在 WebUI／面板上確認圖片庫內容還在、
correct（逐張確認過，不是只看數量）。**Phase 1a 完整驗證通過。**

**Phase 1b — 冷開機後 config 表單首次送出**：使用者重開機後（同一次
開機期間，未先觸發過 weather/sensor config 相關 task），透過瀏覽器
送出設定表單，回報「沒有跳出錯誤」。序列監看期間沒有出現
`deferred_task_stack_alloc_failed task=pf_weather_cfg`／
`task=pf_sensor_cfg` 或 `weather_config_unavailable`／
`sensor_config_unavailable`（這兩個 task 走 `start_deferred_task()`，
成功路徑本身不印訊息，因此「沒有失敗 log ＋瀏覽器沒有錯誤」是這條路徑
成功的正確判讀方式，不是遺漏了成功訊息）。**Phase 1b 完整驗證通過。**

至此，Phase 1（1a／1b／1c）三個子項全部完成實機驗證，回收 57,824 bytes
internal DRAM，upload/mutation/carousel/config 表單/開機復原全部在真機
上確認正常，無回歸。`app_main` 主線與其他 worker task 的完整
high-water-mark（既有清單第 1 項）仍未逐一量測，維持待驗證，但不阻塞
Phase 3 的啟動評估。

## 2026-08-02 — Phase 3：重新拉高 `kCatalogMaxEntries`，真機兩點風險探測

Phase 1 完整實機驗證通過後，測試了 64 與 80 兩個候選值（不是完整
bisection，65–79 與 96 都沒測）。用即時序列監看盯著使用者在 WiFi 已
連線、weather worker 有活動的條件下操作 WebUI（上傳／設為目前／
刪除／排序）。完整記錄見
[ADR-0012](../adr/0012-raise-catalog-cap-after-ram-reclaim.md)。

**量測方式的限制**：`image_upload_stack_free_bytes`／
`image_mutation_stack_free_bytes` 底層用的是
`uxTaskGetStackHighWaterMark()`，回傳的是該 task 自建立以來的歷史
最低剩餘 stack，不是每次呼叫當下的即時剩餘量；`pf_image_up`／
`pf_image_mut` 這兩個 task 在同一次開機期間只建立一次、之後重複使用，
所以下面同一輪測試裡的多筆數字反映的是「目前為止哪一次操作把 stack
用到最深」，不是逐次量測的即時趨勢。

**64 筆**：`pio run` internal RAM 32.4%（106,080 bytes）。多次上傳與
mutation 操作全部成功，記錄到的歷史最低餘裕：

```text
image_upload_stack_free_bytes=2120
image_upload_stack_free_bytes=2124
image_mutation_stack_free_bytes=5912
image_mutation_stack_free_bytes=5608
image_mutation_stack_free_bytes=5608
```

**80 筆**：`pio run` internal RAM 33.8%（110,624 bytes）。上傳成功
（`2124`），mutation 也全部成功，但記錄到的歷史最低餘裕明顯更薄：

```text
image_mutation_stack_free_bytes=1492
image_mutation_stack_free_bytes=1492
image_mutation_stack_free_bytes=1428
image_mutation_stack_free_bytes=1188
```

同一個 24,576-byte 的 mutation stack，64 筆時最低餘裕落在
5608~5912 bytes（約 23%~24%），80 筆時掉到 1188 bytes（約 4.8%）——
這是同一 task、同一指標的前後對照，不需要跟 upload task 的數字比較。
這四次操作都沒有真正觸發 `deferred_task_stack_alloc_failed`，**不能
解讀為「持續惡化的趨勢」**（high-water-mark 本身只會持平或下降，不是
即時量測），正確的解讀是「本輪測試裡最深的一次操作只剩 1188 bytes
餘裕」，已經薄到不足以保守採用，因此拒絕 80，但**沒有證據證明 80
必然會失敗**——只有兩個測試點，證據強度僅止於此。

**結論（定案，見 ADR-0012）**：`kCatalogMaxEntries` 訂為 **64**——本輪
唯一通過保守安全門檻的候選值。80 因為記錄到的最低餘裕不足而拒絕採用，
但不是「已被證明會失敗」；96 未測試，結果未知；65–79 之間也未測試，
不知道實際臨界值在哪裡。裝置已重新燒錄回 64 筆版本作為最終狀態（不是
停在 80 筆的測試狀態）。

### 2026-08-02 — ADR-0013 天氣圖示改為轉檔點陣圖：僅完成開機穩定性驗證

本段（見 `docs/adr/0013-weather-icon-bitmaps-from-third-party-ofl-source.md`）
把狀態列天氣圖示從程序化繪製改為轉檔自 `erikflowers/weather-icons` 的
32×32 單色點陣圖，`pio run` 編譯乾淨（Flash 1,242,997 / 2,621,440
bytes 47.4%，RAM 106,080 / 327,680 bytes 32.4%）、`pio test -e native`
282/282 全綠後，燒錄到實機（native USB，`esptool` 回報 hash 驗證成功）。

實機驗證範圍與結果：

- 燒錄後裝置經 `esptool` hard reset 開機，串列主控台可見
  `esp_image: segment 3` 等正常開機 log，隨後進入應用層並持續運作
  （觀察到 `carousel_request=1 outcome=1 next_due_ms=1832717` 這類正常
  排程 log，顯示 carousel scheduler 有在跑）。
- 另開一個獨立 120 秒監看視窗（不含開機那次連線），全程掃描開機 banner
  （`rst:0x`）與常見 panic 訊號（`Guru Meditation`／`abort() was
  called`／`Backtrace:`／`CORRUPT HEAP`），**0 次出現、0 行輸出**——
  代表這 120 秒內裝置完全靜默運作、沒有任何重開機或當機。

**明確未驗證**：這次驗證只確認「燒進去會開機、120 秒內不會自發重開機／
當機」，**沒有**用肉眼確認 9 種天氣圖示、狀態列文字在實體面板上的實際
視覺呈現——這正是第 499–501 行記錄的既有風險項，本輪未縮小範圍，仍待
補齊（需要有天氣資料可顯示、且有人在裝置前用肉眼核對面板畫面）。

### 2026-08-03 — 第一個 GitHub Release（v0.8.0）發布後，實機「檢查更新」必然失敗：真實 bug 與修正

發布 `v0.8.0` release 後，使用者在實機 WebUI 按「檢查更新」回報
`檢查失敗`／`最新版本：unknown`。根因調查：`curl` 實際取得的
`GET /repos/<owner>/<repo>/releases/latest` 回應長度為 **11285
bytes**，遠超過 `OtaWorker::github_response_buffer_` 的 8192-byte
容量；`tag_name` 欄位實際出現在第 1533 byte（在被截斷的範圍內，本可
正確解析），但舊版 `check_for_update()` 邏輯是「只要回應被截斷就直接
判定 `check_failed`」，完全沒有先嘗試解析 `tag_name`——也就是說，只要
release 的 assets 數量或說明文字讓回應超過 8192 bytes（這個 repo的第
一個 release 就已經超過），「檢查更新」在真機上永遠回報失敗，跟網路
或版本狀態無關。

修正（`components/pf_ota/ota_worker_esp_idf.cpp`）：改成先嘗試
`extract_tag_name()`，只有在解析失敗（`tag.ok == false`）時才依
`github_response_truncated_` 決定要記錄「截斷」還是「欄位缺失／格式
錯誤」；只要 `tag_name` 在截斷前已完整解析出來，即使回應其餘部分
（`assets`／`body`）被截斷也視為成功，因為這兩個大欄位在 GitHub 回應
中原本就排在 `tag_name` 之後。

`codex-cowork` 審查（`gpt-5.6-luna`／`xhigh`）確認這個重排序本身安全
（`extract_tag_name` 只有讀到完整 closing quote 才會回傳
`ok=true`，截斷只會導致 `ok=false`，不會取得部分／損毀的 tag），但
額外發現一個**與本次修正無關的既有 parser bug**：任何 top-level 字串
都會被當成候選 key 比對，若某欄位的**值**恰好等於 `"tag_name"`（例如
`{"name":"tag_name","tag_name":"v1.2.3"}`），會誤判為 key、發現後面
不是 `:` 就直接中止整個掃描，永遠掃不到真正的欄位。已一併修正
（`github_release_check.hpp` 新增 `prev_significant` 追蹤最近一個
結構字元，只有前一個字元是 `{`／`,` 時才視為 key 候選），並補上
`test_value_matching_key_text_does_not_abort_the_scan` 回歸測試。

**殘留風險（已評估，決定不修改程式碼，僅記錄）**：GitHub release JSON
的欄位順序（`tag_name` 排在 `assets`／`body` 之前）不是 API 正式契約
保證的行為，只是目前實測與 GitHub 公開 schema 多年來的實際順序；若
未來 GitHub 調整欄位順序把 `tag_name` 排到 8192-byte prefix 之外，會
重新出現 false negative。加大 buffer 只能降低機率、不能根治，且目前
專案對 OTA task 的靜態記憶體佔用已經很謹慎（見 2026-08-01 段落的
task stack／PSRAM 放置討論），不打算為此臆測性風險加大配置。若未來
真的觀察到欄位順序改變導致的 `check_failed`，才需要改成累進式掃描
（不管 buffer 容量為何持續讀到抓到 `tag_name` 為止）。

同一批修正也處理了使用者回報的第二個問題：WebUI 按下「檢查更新」後
只會顯示「已送出檢查要求，稍後重新整理查看結果」，且只排一次
3 秒後的 `setTimeout`，若 GitHub API 呼叫接近 10 秒逾時上限，畫面會
停在舊狀態直到使用者手動重新整理。改成呼叫既有的
`startSystemOtaPoll()`（沿用「立即更新」流程本來就有的 3 秒輪詢，
上限 200 次≈10 分鐘），並將 `loadSystemStatus()` 判斷是否停止輪詢的
條件從「只看 `update_state`」擴大為「`check_state === checking` 或
`update_state` 為 downloading/writing 都視為進行中」。`codex-cowork`
再指出兩個 Medium 競態並已修正：(1) 輪詢期間較舊的 `loadSystemStatus()`
非同步回應可能晚到，用過期資料覆寫較新操作的狀態、或誤停剛啟動的
新輪詢——加入 request generation token（`systemOtaStatusRequestId`），
只有最新一次呼叫可以套用結果；(2) 離開 System 頁再返回時，若當下仍在
`checking`／`downloading`，原本只呼叫一次 `loadSystemStatus()` 不會
恢復輪詢，畫面會停住直到手動整理——改成同一段邏輯只要偵測到仍在進行
中且 `systemOtaPollTimer` 目前是 `null` 就自動呼叫
`startSystemOtaPoll()` 恢復輪詢。

**驗證**：`pio run`／`pio test -e native`（297 cases，含新增的
`test_value_matching_key_text_does_not_abort_the_scan`）全綠；
`node --check data/web/ui.js` 與 `test/web/test_system_ui_contract.mjs`
通過。使用者在真機上重新驗證，「檢查更新」正確顯示「已是最新／最新
版本 v0.8.0」（裝置當時韌體與 release 版本相同）。**真正的「立即更新」
端到端流程（下載、寫入、自動重開機、rollback 確認）仍未實機驗證**，
待下一個 release（`v0.8.1`，已同步把 `kFirmwareVersion` 對齊）發布後
測試。

### 2026-08-03（續）— `v0.8.1` 實機「立即更新」：兩個真正的根因，逐一修正後成功

`v0.8.1` 發布後，使用者在真機上按「立即更新」，回報 `https_error`，
console log 顯示 `esp-tls: Failed to create socket (family 2 socktype 1
protocol 0)`。用臨時把 `kFirmwareVersion` 改回 `v0.8.0`（不 commit，
純本地燒錄）反覆重現＋加診斷 log 的方式定位，兩輪都是真正的根因，不是
臆測：

**根因 1：`CONFIG_LWIP_MAX_SOCKETS=10`（ESP-IDF 預設值）在多個 client
同時搶 socket 額度時不夠用。** 在 `update_now()` 加入
`heap_caps_get_free_size`／`heap_caps_get_largest_free_block` 診斷 log
後重現，實測數字：`update_now_start`／`before_esp_https_ota_begin`／
`ota_update_begin_failed` 三個時間點的 `internal_largest`／
`dma_largest`（最大連續可用區塊）**完全相同、全程 31744 bytes 沒變過**
——直接排除記憶體不足的可能（`socket()` 需要的記憶體遠小於這個數字）。
`components/pf_web/health_server.cpp` 的 httpd 自己就保留
`max_open_sockets = 7`，扣掉之後全系統只剩 3 個 socket 額度要分給
mDNS、SNTP、weather worker，以及這次 OTA client（且 OTA 這次還需要
連續開 2-3 個連線，因為 GitHub release 下載網址是同主機再跨主機的
多段 redirect，見下方根因 2）；WebUI 的 OTA 狀態輪詢又會每 3 秒用
`Promise.all` 同時開 4 個平行連線。把 `sdkconfig.defaults` 的
`CONFIG_LWIP_MAX_SOCKETS` 從 10 提高到 16 後，`Failed to create socket`
不再出現，redirect chain 順利完成到第 3 段 TLS handshake。

**根因 2：`esp_http_client_config_t::buffer_size_tx` 預設 512 bytes，
裝不下 GitHub release asset 的簽章網址。** 根因 1 修正後改出現
`HTTP_CLIENT: Out of buffer`／`esp_https_ota: Failed to open HTTP
connection: ESP_FAIL`。查 ESP-IDF `esp_http_client.c` 原始碼確認
這行 log 來自組「送出去的 request line」（`GET <path?query>
HTTP/1.1`）時的 buffer，由 `buffer_size_tx` 控制，**不是**一開始猜測
的收訊 buffer（`buffer_size`，第一次只改這個沒有解決問題）。實測
GitHub redirect 到 `release-assets.githubusercontent.com` 的目標網址
（Azure Blob SAS URL，含長 JWT）：path+query 883 bytes，完整 request
line 896 bytes，遠超過 512 bytes 預設值。把 `buffer_size` 與
`buffer_size_tx` 都設為 2048 bytes 後（heap 餘裕早已證實充足，
>85 KB free、31 KB 最大連續區塊，加大這兩個 buffer 成本可忽略）
問題解決，**實機測試當下用的就是 2048**。`codex-cowork` 審查後建議
再加大到 4096（GitHub/Azure 的簽章與 JWT 長度不受本專案控制，未來可能
變長；成本仍可忽略），已採用並隨 `v0.8.2` 一起發布，但**這個 4096 的
數字本身尚未經過另一次實機測試**，只是 2048 的嚴格超集、理論上只會
更寬裕不會更緊，不視為需要額外驗證的風險。

**實機結果：使用者確認「成功更新了」**，並確認更新後裝置**自動重開機**、
`/api/v1/device` 的 `firmware` 欄位正確顯示為新版本 `v0.8.1`——這是本
專案第一次真正驗證通過的 OTA 完整流程（檢查→下載→寫入→自動重開機→
版本確實切換）。比 `docs/RELEASE_CHECKLIST.md` 原本要求的驗收項目少
一項明確證據：console log 是否出現 `esp_ota_mark_app_valid_cancel_
rollback` 成功訊息（使用者只回報版本已切換，未附上這行 log），留待
下次 release 或使用者後續回報補齊；裝置版本已正確切換這件事本身已
隱含開機沒有 crash-loop（否則 bootloader 會自動 rollback 回舊分割區，
`/api/v1/device` 就不會顯示新版本）。

**重要限制，需在下一輪 release 溝通清楚**：這次測試時，「下載端」
（負責發起 `esp_https_ota` 的那個韌體）是本地暫改版號、含這兩個修正
的 `v0.8.0` 燒錄版本；被下載＋寫入的「目標端」是已發布、**不含**這兩個
修正的官方 `v0.8.1` release。也就是說 socket／buffer 的修正只需要存在
於「發起下載的那份韌體」，跟被下載的目標版本內容無關。但這也代表：
裝置現在跑的 `v0.8.1` 本身**沒有**這兩個修正，之後若想再用 OTA 從
`v0.8.1` 更新到 `v0.8.2`，會重新踩到同一組 `Failed to create socket`／
`Out of buffer` 錯誤——因為這次是 `v0.8.1` 自己要發起下載，而它沒有
修正。使用者需要用**有線燒錄**方式把 `v0.8.2` 直接裝上去一次；在那
之後，因為 `v0.8.2` 本身就含這兩個修正，未來從 `v0.8.2` 開始的 OTA
更新才能真正透過 OTA 完成，不需要再靠有線介入。

兩個修正（`CONFIG_LWIP_MAX_SOCKETS=16`、`buffer_size`/`buffer_size_tx`
=4096）與新增的 heap headroom 診斷 log 隨 `v0.8.2` 一併發布。

### 2026-08-03 — AP Mode 電子紙顯示 grace policy：僅完成 host/build 驗證

本次修正補足固定 SSID `PaperFrame-Setup-XXXX` 所需的小寫 `t`／`u` 字型，
並將 AP 顯示行為固定為：空圖片庫持續顯示 AP page；有 `enabled` 且未損毀
圖片時，從 `wifi=provisioning` 的 AP ready 狀態起等待 5 分鐘後才排入
carousel。AP presenter 與其他 display producer 共享 submission gate，
presence away 期間不會在 AP page 上執行白屏。

驗證結果：

- `.\\.venv\\Scripts\\pio.exe test -e native`：300/300 通過；AP screen
  timing/payload tests 6/6 通過。
- `.\\.venv\\Scripts\\pio.exe run -e paperframe-s3`：成功；RAM 106,688
  bytes（32.6%），Flash 1,249,861 bytes（47.7%）。

尚未完成實機驗證：SSID 實際像素可讀性、AP page 與 Wi-Fi 啟動的併發刷新、
5 分鐘切換時序、presence 例外及低 DMA heap guard 下的 AP 啟動結果。

### 2026-08-03 — PlatformIO app upload 改為依 active OTA slot 寫入

先前 `tools/platformio_native_usb_upload.py` 只保留 esptool flags，PlatformIO
最後固定把 firmware 寫到 `0x10000`（`ota_0`）。本機從 `otadata` 讀到 sequence
1 與 2 後確認裝置實際由 `0x290000`（`ota_1`）開機，因此舊流程會成功驗證
hash，卻仍執行舊 slot 的韌體。

現已新增 `tools/platformio_active_ota_upload.py`：PlatformIO app-only upload
先讀取 `0xd000` 的 0x2000-byte OTA metadata，驗證 ESP-IDF CRC、排除
`INVALID`／`ABORTED` entry，再依最高 `ota_seq` 對應 partition CSV 的
`ota_N` offset 寫入。metadata 無法判定時會 fail closed，不會盲寫 `ota_0`；
`webfs`、`imagefs`、NVS 與 partition table 仍不在寫入範圍。

驗證結果：

- wrapper parser 的 synthetic OTA metadata test 通過，包含 `ota_1` 選擇、CRC
  及 invalid state 排除。
- `\.venv\Scripts\python.exe -m py_compile` 通過；`pio run -e paperframe-s3`
  build 通過（RAM 106,688 bytes／32.6%，Flash 1,249,861 bytes／47.7%）。
- 使用不存在的 `<NON_EXISTENT_PORT>` 執行 PlatformIO upload command smoke，確認實際呼叫
  wrapper 並先嘗試讀 metadata；因預期無此連接埠而失敗，未對硬體寫入。
- 目前實機曾以手動 esptool 將相同 firmware 寫入 active `ota_1`，重開機 log
  確認載入 `0x290000` 且版本為 `v0.8.2`；本次 wrapper 沒有再次對 native USB
  執行實際寫入。

### 2026-08-03 — AP Mode 專用字型補齊完整小寫英文字母

AP Mode 的 5×7 專用字型已從原本只涵蓋實際 SSID 所需的小寫字母，補齊為
完整 `a`–`z` 26 個小寫 glyph；既有 `A`–`Z`、`0`–`9` 與必要標點維持不變。
host test 逐一查詢 renderer 共用的 glyph table，使用 `abcdefghijklmnopqrstuvwxyz`
確認每個字母的 5×7 bitmap 都符合預期，且不是 `?` fallback；ESP32-S3 build
另確認 AP renderer 的實際編譯連結路徑使用同一份 table。

## 2026-08-19 — WebUI 編入 app image（僅 host／build 驗證）

`data/web/*` 改為在 build 時 gzip 後編入 app image，`webfs` 分割區轉為
reserved，詳見
[ADR-0016](../adr/0016-embed-webui-assets-in-firmware.md)。**本段沒有任何
實機驗證。**

已完成（host／build）：

- `pio run -e paperframe-s3` 成功；Flash 由 1,250,640 增至 1,292,285 bytes
  （OTA slot 的 49.3%，餘裕約 1.33 MB），RAM 32.6%（106,688 bytes）。
- 8 個資產原始 177,074 bytes、gzip -9 後 41,913 bytes（23.7%）。
- 嵌入資料落在 flash DROM（`pf_web::web_assets::kUiJsGz` 位於
  `0x3c0eb38c`），不佔 RAM。
- `pio test -e native` 304/304 通過；`test/web/*.mjs` 全通過；
  `node test/test_partition_layout.mjs` 通過（partition table 未變更，
  ADR-0004 凍結的 SHA-256 仍有效）；embedded test 專案 build-only 通過。
- 觸發鏈：修改 `data/web/style.css` 後未 touch 任何 CMakeLists 重跑
  `pio run`，生成器回報 `updated` 並重新編譯、`firmware.bin` 大小改變；
  還原後第二次 build 回報 `unchanged` 且無重編（決定性壓縮生效）。
- 防護測試：於 `data/web/` 放入未接線的檔案後，
  `test/web/test_embedded_web_assets.mjs` 以「is embedded but never wired
  into a StaticAsset」失敗。
- `littlefs_imagefs_bin` 仍可產出 10,289,152 bytes（`0x9D0000`）的 image，
  factory provisioning 未受影響；`webfs.bin` 不再產生。

待實機驗證：

- 瀏覽器對 `Content-Encoding: gzip` 的實際解碼，特別是由 `new Worker()`
  載入的 `image_quantize_worker.js` 與 `favicon.svg`（SVG + gzip + CSP
  `img-src 'self'` 的交互）。
- `/health` 實機回應不再含 `webfs` 欄位且 `status` 為 `ready`。
- 移除 webfs LittleFS 掛載後的 heap 差值（預期釋出數 KB 量級）。
- 一次真實 OTA 後，瀏覽器載入的前端即為新版（不需另外燒錄）。

## 2026-08-19 — 嵌入式 WebUI 實機驗證

補上同日「WebUI 編入 app image（僅 host／build 驗證）」段落所列的待驗證
項。硬體：ESP32-S3-N16R8，native USB Serial/JTAG（`VID_303A&PID_1001`），
裝置取得區網 IP（本紀錄省略實際位址）。

### 燒錄

`pio run --target upload` 寫入 **active `ota_1` slot（`0x290000`）**，
1,292,688 bytes，`Hash of data verified.`，hard reset 成功。未觸碰
`webfs`、`imagefs`、NVS 或 OTA metadata——本次不需要任何額外的 filesystem
燒錄步驟，這正是本變更的目的。

### 開機 log

- `filesystem=imagefs mounted=true total=10289152 used=573440
  mount_status=ESP_OK info_status=ESP_OK`
  ——**只有 imagefs 一行，webfs 掛載訊息已消失**，確認 `mount_all()` 不再
  掛載 webfs。
- `storage_worker_ready recovery=none action=no_change`
- `health_server_ready route=/api/v1/health`
- `rollback_confirmed=ESP_OK`
- heap_init：`207 KiB` + `21 KiB` + `32 KiB` DRAM + `7 KiB` RTCRAM，PSRAM
  8 MB 併入 heap。**未取得改動前的同條件對照值，因此「移除 webfs 掛載省下
  多少 heap」仍未量化**，僅確認開機正常、無退化跡象。

### HTTP 契約

`GET /api/v1/health` → `200`：

```json
{"status":"ready","sequence":126,"uptime_ms":112076,
 "services":{"flash":"ready","psram":"ready","config":"ready","imagefs":"ready"},
 "network":{"wifi":"connected","internet":"reachable"}}
```

**`status` 為 `ready`（不是 `degraded`），且 `services` 不含 `webfs`**，確認
`is_ready()` 移除 webfs 條件後行為正確。`GET /api/v1/device` 回報
`firmware":"v0.8.2"`。

### gzip 供檔（8 個資產全部驗證）

每個資產皆 `200` + `Content-Encoding: gzip`，解壓後長度與 repo 內
`data/web/` 的檔案大小**逐一完全相符**，證明裝置提供的就是本次 build 的
前端：

| 路徑 | 傳輸 (gzip) | 解壓後 | 本機檔案 |
| --- | ---: | ---: | ---: |
| `/` | 6,974 | 31,849 | 31,849 |
| `/ui.js` | 22,353 | 94,924 | 94,924 |
| `/style.css` | 4,270 | 18,856 | 18,856 |
| `/image_pipeline.js` | 3,572 | 17,158 | 17,158 |
| `/image_quantizer.js` | 1,353 | 4,936 | 4,936 |
| `/image_pfr1.js` | 2,598 | 7,396 | 7,396 |
| `/image_quantize_worker.js` | 500 | 1,095 | 1,095 |
| `/favicon.svg` | 235 | 326 | 326 |

`Cache-Control: no-store`、`X-Content-Type-Options: nosniff` 與 ADR-0014 的
CSP 均原樣保留。注意 route 只註冊 `HTTP_GET`，因此 `curl -I`（HEAD）不會有
回應——這是既有行為，非本次變更所致。

### 瀏覽器行為

- 登入頁完整渲染（版面、字型、配色正常），**console 0 error／0 warning**。
- 網路面板：`/`、`style.css`、`image_pipeline.js`、`image_quantizer.js`、
  `image_pfr1.js`、`ui.js`、`favicon.svg` 與 `/api/v1/auth/status` 全部
  `200`。
- **Web Worker**：於頁面內以 `new Worker('/image_quantize_worker.js')`
  建立，worker 不只成功載入，且**實際執行**——對探測訊息回覆了它自身的
  參數檢查結果 `{"ok":false,"error":"worker request must include raster
  dimensions and data"}`。這同時驗證了 gzip 解碼、`importScripts` 以及 CSP
  在沒有 `worker-src` 時回退到 `script-src 'self'` 的行為。

### 仍未驗證

- ~~登入後 Dashboard／System 頁的容量欄位視覺確認~~ **已於 2026-08-19 由使用者
  在實機確認：兩頁皆不再顯示 webfs 容量。**
- 一次真實 OTA 後前端同步換版（需要一個新的 GitHub Release 才能端到端驗證）。
- 移除 webfs 掛載後的 heap 差值量化（缺改動前的對照值）。

## 2026-08-19 — 直式圖片方向修正（實機確認）

### 症狀

實機直立擺放時，直式（portrait）圖片在面板上呈現上下顛倒，與實際放置
方向差 180 度。橫式圖片正常。

### 根因

`pf_display::compose_portrait()` 對 `PortraitRotation::clockwise` 與
`counter_clockwise` 的座標映射互為點對稱：

- clockwise：`native_x = (kPortraitLogicalHeight - 1) - logical_y`、
  `native_y = logical_x`
- counter_clockwise：`native_x = logical_y`、
  `native_y = (kPortraitImageWidth - 1) - logical_x`

因此兩者恰好相差 180 度。renderer 本身兩個方向都正確且早有測試覆蓋
（`test_frame_renderer` 的
`test_portrait_rotates_all_placement_and_direction_combinations`）——錯的
是 `pf_carousel::Pfr1FrameDecoder::finish_and_compose()` 的預設值選了
`clockwise`，而本裝置的實際擺放需要 `counter_clockwise`。production 程式碼沒有任何呼叫端
覆寫這個參數（只有 host test 為了比對兩個方向而明確傳值），所以預設值
就是實際行為。

### 修正

`components/pf_carousel/include/pf_carousel/image_frame.hpp` 的
`portrait_rotation` 預設值改為
`pf_display::PortraitRotation::counter_clockwise`。`compose_portrait()`
旋轉的是整個 portrait 邏輯畫布，狀態列會隨圖片一起翻轉，不會留在原處。
橫式圖片不受影響（`compose_landscape()` 不做旋轉）。

### 驗證

- 新增 `test_image_frame` 的
  `test_portrait_default_rotation_matches_physical_mounting`：以同一個
  PFR1 檔案分別用「預設」「明確 counter_clockwise」「明確 clockwise」合成
  三次，斷言預設等於 counter_clockwise，並額外斷言兩個方向的輸出確實不同
  （避免斷言在兩者等價時變成空的）。
- **修改前該測試為紅**：`Memory Mismatch. Byte 1209 Expected 0x00 Was
  0x11`；修改後轉綠。
- `pio test -e native` 305/305 通過；`pio run` 成功（Flash 49.3%、
  1,292,285 bytes；RAM 32.6%）。
- 燒錄後實機重啟，log 出現 `carousel_request=1 outcome=1
  next_due_ms=1838647`，面板完成一次刷新；**使用者於實機確認直式圖片方向
  已轉正**。

### 備註

此預設值編碼的是本裝置的實體擺放方向。若日後支援不同的安裝方向，應改為
可設定項而非再次翻轉預設值。

## 2026-08-19 — v0.9.0 release 與 OTA 端到端驗證

關閉「一次真實 OTA 後前端同步換版」這項待驗證，同時取得 ADR-0008 的真實
GitHub 下載與 rollback confirmation 證據。

### Release

tag `v0.9.0` 推送後 release workflow 成功（4m56s）。資產：

| 資產 | 大小 | 備註 |
| --- | ---: | --- |
| `paperframe-firmware.bin` | 1,292,608 | OTA 使用 |
| `paperframe-partitions.csv` | 414 | SHA-256 `427fd414…5870`，**與 ADR-0004 凍結值一致** |
| `paperframe-licenses.zip` | 4,556 | 第三方素材授權 |
| `SHA256SUMS` | 272 | |

**不再產生 `paperframe-webfs.bin`**，確認 ADR-0016 的發佈側清理生效。

### 測試方法

刻意**不降回舊架構**：v0.8.2 的 upload 走的不是 active-slot wrapper，會寫到
`0x10000`（ota_0），而 otadata 指向 ota_1，重啟後仍執行原韌體，降版根本不會
生效。改為以目前程式碼建置一份「版本號 `v0.8.9` + `index.html` 開頭插入
`<!-- OTA-PROBE-LOCAL-BUILD -->` 標記」的測試韌體燒入裝置。這個標記讓「前端
是否真的被換掉」成為可直接觀測的事實，而不是從「前端在 app image 內」推論。

### 結果

| 檢查 | 結果 |
| --- | --- |
| OTA 前版本 | `v0.8.9`（測試韌體），前端含 `OTA-PROBE-LOCAL-BUILD` |
| 管理員於 WebUI 觸發檢查更新 → 立即更新 | 成功，裝置自動重開機 |
| OTA 後版本 | **`v0.9.0`** |
| 前端標記 | **已消失**——前端確實隨韌體一併換成 release 版本 |
| `/api/v1/health` | `status":"ready"`，`services` 無 `webfs` |
| 資產供檔 | `/`、`/ui.js`、`/image_quantize_worker.js` 皆 `Content-Encoding: gzip`，解壓後長度與 release 版一致（31,849 / 94,924 / 1,095） |
| 開機 log | `rollback_confirmed=ESP_OK`；`filesystem=imagefs mounted=true`（無 webfs 掛載） |
| **再次重開機** | 仍為 `v0.9.0`，標記仍不存在——**rollback confirmation 確實生效，未被回滾** |
| imagefs | `used=1,257,472`，使用者圖片完好，OTA 未觸碰 |

「再次重開機仍是新版」是關鍵的一步：若
`esp_ota_mark_app_valid_cancel_rollback()` 未生效，bootloader 會在下次開機
回滾到 `v0.8.9`，只做一次 OTA 是看不出來的。

### 仍未驗證

- 移除 webfs 掛載後的 heap 差值量化（缺改動前的同條件對照值）。
- rollback fault injection（刻意燒一個在 confirmation 前 crash-loop 的版本）。
- OTA 下載途中斷電。

## 2026-08-20 — 設定降級邊界修正（實機紅綠）

修正兩個既有缺陷，詳見
[ADR-0017](../adr/0017-config-degradation-boundaries.md)。兩者都只在未來某次
`kCurrentSchemaVersion` 升級時才會引爆，且症狀都出現在 OTA 之後——最難歸因的
時機。

### 缺陷 1：遷移會丟棄既有欄位

`make_startup_plan()` 在遷移分支 early return **之前**複製了
`refresh_minutes`，卻在其後才讀取 `carousel_random`。遷移路徑會設定
`write_required`，因此回傳的記錄會被寫回 NVS——沒被複製到的欄位就被預設值
永久取代。既有的 v1 測試斷言「隨機播放預設關閉」是正確的（那筆 v1 記錄本來
就沒有這個欄位）；缺陷在於遷移路徑不會**保留**欄位，只有當來源版本已帶著它
時才會顯現，也就是下一次 bump。

修正：所有欄位在任何 early return 之前完成複製。新增 host test 以「已帶有該
欄位的記錄」作為輸入（即下次 bump 的形狀），修改前為紅
（`Expected TRUE Was FALSE`）。

### 缺陷 2：schema 不可解讀時，不相關的設定與服務一起停擺

以 `kCurrentSchemaVersion = 1` 建置，讓 NVS 中既有的 version 2 記錄看起來像
未來版本，精確重現 OTA rollback 情境。

| | 修正前 | 修正後 |
| --- | --- | --- |
| serial | `provisioning_ap_ready ssid=PaperFrame-Setup-...` | 正常連線 |
| `/api/v1/health` | 無法連上 | `wifi: "connected"`、`config: "degraded"` |
| 錯誤密碼登入 | （服務未啟動，503） | **401**（服務已啟動並驗證憑證） |

還原為 version 2 後裝置回到 `config: "ready"`、`wifi: "connected"`。

**驗證方法的教訓**：第一次實機驗證曾以 `/api/v1/auth/status` 回報
`password_configured: true` 作為「管理密碼載入成功」的證據，這是**假陽性**
——`AuthService::authenticate_request()` 在服務未初始化時刻意 fail-closed
回傳 `true`，也就是說那個 `true` 正是「服務沒啟動」的表現。能區分兩種狀態的
探針是「用錯誤密碼登入」：401 代表服務已啟動並實際驗證，503 代表服務不可用。

### 仍未驗證

- 登入後修改設定被拒（`409 config_read_only`）的端到端路徑，需要管理密碼。
- `nvs_flash_init()` 失敗、以及 NVS 滿導致 `pf_config` 開啟失敗這兩條路徑。
- 真實的 OTA rollback fault injection（刻意燒一個在 rollback confirmation 前
  crash-loop 的版本）——這仍是 ADR-0008 從 Phase 8 起就列著的待驗證項。

## 2026-08-20 — v0.9.1 收尾驗證：OTA 端到端、upload wrapper、刷新耗時

本次一併關閉 `active OTA upload wrapper`、Phase 8 的 rollback confirmation 與
OTA worker stack、Phase 2 的 refresh duration、Phase 3/4 的真實 SNTP，以及
2026-08-20 設定降級段落遺留的 `409 config_read_only` 端到端路徑。

### 測試環境

- 裝置韌體 `v0.9.1`（ESP-IDF 6.0.0），ESP32-S3 rev v0.2、flash boya QIO 16 MB、
  PSRAM 8 MB octal @80 MHz，7.3 吋 E6 面板已接。
- 應用程式 console 為 native USB（`VID_303A&PID_1001`）；同一台工作站上另有一台
  **非本專案**的裝置佔用 CP210x 埠，辨識 port 時必須依 USB hardware ID 確認，
  不能只看埠號。
- serial 擷取腳本開埠前先設 `dtr=False`／`rts=False`：pyserial 預設開埠會拉
  DTR/RTS，等同對 ESP32 送 reset（本次因此意外重置過一台鄰近裝置）。

### 離線／自動化結果

| 項目 | 結果 |
| --- | --- |
| `pio run -e paperframe-s3` | 成功；RAM 106,688 bytes（32.6%）、Flash 1,293,233 bytes（49.3%） |
| `pio test -e native` | 308/308 通過 |
| embedded build gate（`paperframe-s3-embedded-test`） | `test_runtime_coordinator` build 通過 |
| embedded build gate（`paperframe-s3-display-test`） | `test_display_task` build 通過 |
| `test_epd7in3e_driver`（需 `-f` 指定） | build 通過 |
| `node --check data/web/*.js` | 5/5 |
| `test/web/*.mjs` | 9/9 |
| `node test/test_partition_layout.mjs` | 3/3 |
| `test/test_active_ota_upload.py`（需 `PYTHONPATH=.`） | 4/4 |
| release 資產 | `v0.9.1` 四個資產齊全，檔名符合 OTA 需求 |
| OTA 固定 URL | `releases/latest/download/paperframe-firmware.bin` → HTTP 200、1,293,616 bytes、SHA-256 與 `SHA256SUMS` 相符 |
| partition CSV | release 版與本機 `partitions/paperframe-dev.csv` 位元相同，SHA-256 為 ADR-0004 凍結值 |

`pio test -e native` 一度回報 `test_storage_worker ERRORED`，根因是同時間另有一個
PlatformIO 指令在跑，清 `.pio/build` 觸發 `WinError 145`。單獨重跑該套件 8/8，
完整套件乾淨重跑 308/308。**PlatformIO 指令不可併發執行。**

### active OTA upload slot wrapper（本項自此關閉）

`tools/platformio_active_ota_upload.py` 先前只有 synthetic metadata 測試與一次
手動 esptool 寫入，缺少「wrapper 本身依 otadata 改變寫入位址」的實機證據。

| active slot | wrapper 輸出 | esptool 實際寫入 |
| --- | --- | --- |
| `ota_0` | `Uploading active ota_0 app slot at 0x10000` | `0x00010000` |
| `ota_1`（經一次真實 OTA 切換後） | `Uploading active ota_1 app slot at 0x290000` | `Wrote 1293072 bytes ... at 0x00290000` |

同一條 `pio run --target upload` 指令在兩種 otadata 狀態下寫到不同位址，且開機
log 的 `Loaded app from partition at offset ...` 與 wrapper 選定的 slot 一致，
排除了「固定寫 PlatformIO 預設 `0x10000`」的可能。本次共執行七次 upload
（含臨時 instrumentation、OTA probe 與降級測試韌體），每次之後 `imagefs`、
NVS 與 partition table 都未被觸碰。

### OTA 端到端（v0.9.1 的 release checklist §3）

測試韌體以目前程式碼建置，版本字串改為 `v0.9.0-probe`、`index.html` 開頭插入
`<!-- OTA-PROBE-LOCAL-BUILD -->` 標記，使「前端是否真的換版」成為可直接觀測的事實。

| 檢查 | 結果 |
| --- | --- |
| 檢查更新 | `check_state=update_available`、`latest_version=v0.9.1` |
| 立即更新 | 回 202；`downloading` → `writing` 1%→94%，約 30 秒 |
| 寫入位置 | `esp_https_ota: Writing to <ota_1> partition at offset 0x290000` |
| 重開後版本 | `v0.9.1`，`Loaded app from partition at offset 0x290000` |
| 前端標記 | **已消失**——WebUI 隨韌體整包換版 |
| boot validation | `rollback_confirmed=ESP_OK` |
| **再次重開機** | 仍為 `v0.9.1`、仍從 `0x290000` 開機、標記仍不存在 → rollback confirmation 確實生效 |
| 圖片庫 | 14 張，id 集合與 `file_bytes`／尺寸／`order`／`enabled` 全部不變；唯一差異是 `current` 由 id 25 移到 27（輪播正常換圖） |
| imagefs | `used=1257472` 在七次 upload、一次 OTA 與多次重開機後皆不變 |
| OTA worker stack | check 後 `ota_worker_stack_free_bytes=12140`、update 後 `=11948`（配置 16,384，峰值用量約 27%） |
| OTA heap headroom | `point=update_now_start internal_free=116051 internal_largest=31744 dma_free=108167 dma_largest=31744`；`point=before_esp_https_ota_begin internal_free=112927 dma_free=105139` |

reboot persistence 另外單獨驗過：API 觸發重啟後 `/api/v1/images` 與
`/api/v1/config` 回應位元相同，`reboot_reason` 正確轉為 `software_reset`。

### 面板刷新耗時（Phase 2 的 refresh duration 自此關閉）

程式碼原本沒有任何刷新計時輸出。量測方式是在
`components/pf_display/include/pf_display/display_task_esp_idf.hpp` 的
`EpdPanel::refresh_and_sleep` 前後包 `esp_timer_get_time()` 並加一行 log，
**不可以加在 `epd7in3e.hpp`**——那個 header 也被 native env 編譯，加
`ESP_LOGI` 會弄壞 host build。量測後已還原並重燒乾淨韌體確認 log 消失。

| 樣本 | 圖片 id | 結果 |
| --- | --- | --- |
| 1 | 25 | `panel_refresh_ms=31203 status=0 stage=10` |
| 2 | 25 | `panel_refresh_ms=31221 status=0 stage=10` |
| 3 | 15 | `panel_refresh_ms=31244 status=0 stage=10` |

全刷加 deep sleep 約 **31.2 秒**，三次差距 ±20 ms，且不同圖片耗時幾乎相同，
表示耗時由面板本身主導、與影像內容無關。刷新間隔的實際下限是
`pf_config::kMinimumRefreshMinutes = 10` 分鐘（WebUI 的
`#image-carousel-refresh-minutes` 也是 `min="10" max="1440"`，兩者一致），
因此有約 19 倍餘裕。**注意 `CLAUDE.md` 目前寫「下限 5 分鐘」，與程式碼和
WebUI 都不符**，應以 `schema.hpp` 為準。

`carousel_request=<id> outcome=<n>` 的 `outcome` 對應
`pf_runtime::DisplayOutcome`：0 `none`、**1 `refreshed_and_slept`**、
2 `invalid_lease`、3 `busy_timeout`、4 `transport_error`、5 `panel_state_error`。
成功刷新是 `outcome=1`，不是 0。

### 設定降級邊界的 409 路徑（2026-08-20 前一段的遺留項）

以 `kCurrentSchemaVersion = 1` 建置，使 NVS 中既有的 version 2 記錄看起來像未來
版本，精確重現 OTA rollback 情境；測後已還原為 2 並重燒，裝置回到
`config_schema=2 action=use_current`。

| 檢查 | 結果 |
| --- | --- |
| `/api/v1/health` | `status=degraded`、`config=degraded`，`wifi=connected`、`flash`／`psram`／`imagefs` 皆 ready |
| 錯誤密碼登入 | **401** `invalid_credentials`（服務已啟動並實際驗證，不是 503） |
| 正確密碼登入 | 成功取得 session 與 CSRF token |
| `POST /api/v1/config` | **409** `{"ok":false,"error":"config_read_only"}` |
| `GET /api/v1/config` | 200，但 `refresh_minutes: null`、`timezone: "unknown"`——缺值誠實標示，不偽造預設 |
| serial | 無 `provisioning_ap_ready`；`network_credentials_configured=true`、`management_password_configured=true`，STA 正常連線 |

### 認證邊界與公開面

| 請求 | 結果 |
| --- | --- |
| `POST /api/v1/auth/login` 錯誤密碼 | 401 `invalid_credentials` |
| `GET /api/v1/wifi/scan` 未登入（已建密碼） | 401 `authentication_required` |
| `POST /api/v1/system/reboot` 未登入 | 401 |
| `GET /api/v1/status`／`/images`／`/sensors`／`/system/ota/status` 未登入 | 401 |
| `GET /api/v1/health`／`/api/v1/device`／`/api/v1/auth/status` | 200（公開面僅此三者） |
| `HEAD /` | 405（靜態 route 只註冊 `HTTP_GET`） |

### WebUI 資產與真實瀏覽器

裝置端 8 個資產（`/`、`/style.css`、`/ui.js`、`/image_pipeline.js`、
`/image_quantizer.js`、`/image_pfr1.js`、`/image_quantize_worker.js`、
`/favicon.svg`）全部 200 且 `Content-Encoding: gzip`，解壓後與 `data/web/*`
**位元相同**。

Chromium 實測：登入流程正常；Dashboard 顯示韌體 v0.9.1、Wi-Fi 已連線、
Internet 可連線、SNTP synced、面板「休眠」、最後刷新 `refreshed_and_slept`、
imagefs 1.2 MB / 9.8 MB、四服務皆正常、天氣「可用」。System 頁四個操作
（重新啟動、重設管理密碼、檢查更新、立即更新）都在；點「檢查更新」得到
`up_to_date`、`latest_version=v0.9.1`，「立即更新」正確維持 disabled。
**全站沒有任何手動 Recovery AP 入口**，與既定設計一致。診斷事件面板顯示
「尚無診斷事件」，與 API 的 counter 全為 0 一致。

console 出現兩則 `Applying inline style violates ... 'style-src 'self''`，
但那兩則沒有檔名，且 `ui.js` 的 OTA 進度條實測正常
（`el.style.width='42%'` 後 `getComputedStyle` 亦為 `42%`）——CSSOM 寫入不受
`style-src` 限制，是測試工具注入造成的假陽性，不是韌體端問題。

### 真實 SNTP

`/api/v1/status` 回 `"sntp":"synced"`，重開機後 15 秒內恢復；OTA 檢查的
`last_check_epoch_s` 與天氣的 `observed_at_epoch_s` 都是當下真實時間。
**mDNS 沒有實作**（`grep -rn mdns components/ src/` 零命中，`paperframe.local`
只出現在 `docs/archive/Guild.md`），因此「SNTP/mDNS」這一項自此只保留 SNTP 的
失敗側；mDNS 屬於未實作功能，不是待驗證項。

### 本次新發現

1. **CI 的 embedded build gate 只涵蓋三個 embedded 測試中的一個**。
   `.github/workflows/ci.yml` 只 build `-e paperframe-s3-embedded-test`，而該 env 的
   `test_filter = test_runtime_coordinator`；`paperframe-s3-display-test`
   （`test_display_task`）沒有在 CI 跑，`test_embedded/test_epd7in3e_driver`
   **沒有任何 env 的 filter 涵蓋**。三者本機手動 build 都通過，所以不是壞掉，
   是覆蓋缺口。
2. **Dashboard 有寫死的過時文案（已於同日修正）**：`data/web/index.html` 的
   `<div><dt>SNTP</dt><dd>尚未接入</dd></div>` 沒有 id，`ui.js` 不會更新它，
   而同一畫面的 `#dashboard-sntp`（`ui.js` 會更新）已顯示 `SNTP：synced`——
   同頁同時出現「synced」與「尚未接入」。同卡片的「光敏電阻：未安裝」同樣是
   寫死的，Phase 7 接上感測器後也會是錯的。
   修正：刪除重複的 SNTP 列（NETWORK 卡片已有同一資訊，留兩處只會再次不同步），
   光敏電阻改為 `id="light-sensor-state"` 並由 `labelSensorStatus(sensors.light_status)`
   更新。新增 `test/web/test_dashboard_ui_contract.mjs` 防止同類漂移：斷言 `ui.js`
   的每個 `$("#id")` 都存在於 `index.html`（否則 `null.textContent` 會中斷整個 render
   函式，該行之後的欄位全部停止更新），並斷言該卡片內每個 `<dd>` 都帶 id。
   該測試以還原舊標記的方式做過紅綠驗證。實機複驗：全頁 SNTP 只剩
   `SNTP：synced`，卡片顯示「天氣快取: 可用 / 溫溼度: 未啟用 / 光敏電阻: 未啟用」，
   最後一項由 `light_status="disabled"` 動態產生。
3. **`reboot_reason` 在 esptool USB reset 之後回報 `brownout`**；同一台裝置經 API
   觸發重啟時正確回報 `software_reset`，另一次 upload 後回報 `other`。未進一步
   歸因，僅在日後看到 `brownout` **且伴隨刷新失敗**時才需追查供電。
4. **本機重建的 image 與 release 資產非位元一致**：同一 commit 下本機
   `firmware.bin` 為 1,293,648 bytes、release 資產為 1,293,616 bytes。未歸因；
   OTA 使用的是 release 資產且校驗值已驗過，不影響本次結論，但若要主張
   reproducible build 需另行調查。

### OTA、面板刷新與天氣併發時的 heap（Phase 8 的 weather+OTA heap，本項自此關閉）

**測法的前提要先講清楚**：天氣沒有週期計時器（ADR-0014）。
`WeatherWorker` 成功後把 `next_attempt_ms` 設為飽和值，只有
`request_immediate_refresh()` 能喚醒它，而那是 `app_main` 在 **carousel 刷新
被接受的當下**呼叫的。所以「等 10 分鐘讓天氣自己抓」是錯的測法——第一次嘗試
就是這樣落空的：OTA 下載窗口內完全沒有天氣連線，那批數據不算數。

改用強制對齊：先觸發 OTA，6 秒後（下載已進入 writing）呼叫
`POST /api/v1/images/<name>/activate`。啟用圖片會讓 app_main 走
`carousel.force_immediate()`，提交被接受後隨即觸發天氣抓取，於是三件事同時在飛：
HTTPS 下載寫入非 active slot、31 秒的面板 SPI 全刷、天氣的 TLS session。

| 時間點 | 事件 | internal_free | internal_largest | dma_free | dma_largest |
| --- | --- | ---: | ---: | ---: | ---: |
| OTA 起始 | `ota_heap_headroom point=update_now_start` | 112,431 | 31,744 | 104,547 | 31,744 |
| OTA TLS 前 | `point=before_esp_https_ota_begin` | 110,479 | 31,744 | 102,983 | 31,744 |
| 刷新已排入、OTA 寫入中 | `weather_fetch_failed`（第 1 次） | **24,547** | **7,680** | 16,759 | 5,376 |
| 同上，11 秒後 | `weather_fetch_failed`（第 2 次） | **11,855** | **5,376** | 11,759 | 5,376 |

**結果：天氣抓取在併發下失敗兩次**，錯誤鏈為
`mbedtls_ssl_setup returned -0x008D`（`MBEDTLS_ERR_SSL_ALLOC_FAILED`）→
`create_ssl_handle failed` → `ESP_ERR_HTTP_CONNECT`。largest free block 從
31,744 掉到 5,376，不足以建立 SSL context。

判定：**這是容量限制，不是缺陷**——降級行為正確。天氣以 `W` 級記錄
`weather_fetch_failed=`、保留既有快取、走既有退避重試，沒有崩潰、沒有偽造數值，
也沒有觸發 provisioning AP；OTA 本身全程不受影響，`ota_worker_stack_free_bytes=11944`
與單獨執行時（11,948）幾乎相同，下載完成後正常重開機並載入新版本。
若日後要讓天氣在這種時刻也能成功，需要的是預留 SSL context 的記憶體或把兩者
互斥，而不是調整重試邏輯。

### 併發測試順帶暴露的兩點

1. **進行中的面板刷新會被 OTA 的重開機截斷**（**已於同日修正**）。
   `carousel_image_queued id=15 request=2` 之後沒有對應的
   `carousel_request=2 outcome=`：刷新需要約 31 秒，而 OTA 在其後約 25 秒就
   重開機了，使用者會看到殘影或半更新畫面。
2. **`network_action_failed action=2 error=ESP_ERR_WIFI_NOT_STARTED` 出現在
   每次重開機路徑上**（**已於同日修正**，兩次獨立 OTA 都重現過）。`action=2` 是
   `NetworkAction::retry_sta`：關閉 Wi-Fi 產生的 disconnect 事件讓狀態機排了一次
   STA 重試，而 Wi-Fi 已經停止。功能上無害，但以 **ERROR 級**記在正常關機路徑上，
   日後看 log 會被誤判為真的網路故障。

### 上述兩點的修正與驗證（2026-08-20）

兩者同源：重開機不知道系統正在忙。`schedule_reboot()`（管理員觸發與 OTA 完成
共用）現在會在 timer 到期時檢查 runtime snapshot，若面板 `refreshing`／`queued`、
有 active request 或仍有排隊刷新，就每 500 ms 重試一次，直到面板閒置；上限
`kMaxRebootDeferrals`（90 次，最壞約 45.5 秒）之後無條件重開，避免卡死的面板
讓裝置無法重啟。決策邏輯抽成 header-only 的 `pf_runtime::should_defer_reboot()`
並有 5 個 host test（含「面板卡死仍會重開」與「上限必須大於一次完整刷新」）。

同時新增 `pf_runtime::reboot_pending()`：`schedule_reboot()` 確認 timer armed 後
才設定。網路側據此停止發出注定失敗的動作——`perform_action_chain()` 直接返回、
`begin_scan()` fail closed、`maybe_start_sntp()` 略過，`/api/v1/wifi/scan` 也回
`503 rebooting`（否則前端收到 202 會每 900 ms 重試到裝置重開為止）。

實機驗證：activate 觸發刷新 → 3.6 秒後確認 `display state=refreshing
active_request=2` → 觸發 reboot → 裝置直到 **t+37.1 秒**才下線。serial 依序為
`reboot_deferred_for_display request=2 queued=0`、
`reboot_proceeding deferrals=58 display_busy=0`、
`network_action_skipped_for_reboot action=2`，**全程無任何 E 級 log**，
重開後 `reboot_reason=software_reset` 且 Wi-Fi 正常重連。

## 2026-08-20 — 面板 sleep 電流（Phase 2 本項自此關閉）

以 USB 電流電壓表串在 ESP32 供電端量測，因此是**整機電流**（ESP32 + Wi-Fi +
面板），不是面板單獨。有意義的是各狀態之間的差值，不是絕對值。

| 狀態 | 電流 | 相對閒置的增量 |
| --- | ---: | ---: |
| 閒置（面板 `deep_sleep`、Wi-Fi 連線） | 9 mA | — |
| 刷新中（最低） | 30 mA | +21 mA |
| 刷新中（最高） | **110 mA** | +101 mA |
| 剛刷完（deep sleep 指令送出後） | 12 mA | +3 mA |
| 刷完一分鐘後 | **10 mA** | +1 mA |

**`0x07` + `0xA5` 的 deep sleep 序列確實生效**：刷新峰值 110 mA 在結束後回到
10 mA，與閒置基線相同量級。若該指令未生效，面板會維持在刷新等級的耗電而不會
降回來——這正是本項要排除的失效模式。

閒置的 9 mA 本身也是一個佐證：ESP32-S3 維持 Wi-Fi 連線通常是數十 mA，9 mA 表示
modem sleep 有生效（開機 log 的 `wifi:pm start, type: 1`）。

### 耗電結構

以刷新平均約 70 mA、單次 31 秒估算，一次刷新約 **0.6 mAh**；閒置則是 9 mA 持續，
即 **9 mAh/小時**。預設 30 分鐘刷新一次的話，每小時兩次刷新約 1.2 mAh——
**待機耗電是刷新的七倍以上**。這符合 e-paper 的特性（畫面本身不耗電），也代表
若要省電，方向在 MCU／Wi-Fi 待機而不是降低刷新頻率。

量測限制：整機讀數含 ESP32 與 Wi-Fi，無法分離面板單獨功耗；Wi-Fi TX 尖峰也會
疊加在讀數上。要取得面板單獨數據需將電表串在 HAT 的 3.3 V 供電線上。

## 2026-08-20 — AP page 與 Wi-Fi 啟動的併發刷新：實作上是序列化的

2026-08-03 列出這一項時的疑慮是：AP radio 啟動需要大量 DMA-capable heap
（2026-08-01 曾因此在 Espressif 閉源 blob 內崩潰），而面板刷新同時持有 DMA
buffer，兩者若併發可能重演。這次以實機量測時序回答。

### 時序：radio 在面板刷完之後才啟動

由使用者以手機完成完整 provisioning（連上 AP、建立管理密碼、填入自己的
Wi-Fi 憑證並送出；密碼由使用者在手機端輸入，未經過本紀錄）。

| 時間 | 事件 |
| --- | --- |
| 1,071 ms | `display_task: panel transport ready` |
| 2,431 ms | `ap_mode_display_waiting_for_ready` |
| 32,561 ms | `provisioning_screen_ready request=1`（面板刷新完成，約 31 s） |
| **32,691 ms** | `wifi:mode : sta + softAP` → `provisioning_ap_ready`（radio 啟動） |

**AP radio 在面板刷新完成後 130 ms 才切換模式**，兩者並未同時競爭 DMA。這是刻意
設計——先把 SSID 與密碼顯示在面板上，再開放連線，否則使用者連上時還讀不到憑證。
因此原本擔心的「AP 啟動與面板刷新併發」在目前實作下不會發生。

### 真正的併發已在別處驗證

「AP radio 運作中同時進行面板刷新」確實會發生，但時機是 grace window 過期之後：
同日的 AP grace window 測試裡，裝置持續處於 AP 模式（無憑證），window 到期時
`carousel_image_queued` → `carousel_request outcome=1`，刷新在 AP radio 活躍狀態下
正常完成。那次也是修好 submission gate 缺陷之後的驗證。

### 本次觀察到的訊息

- 全程**無 SPI／transport 錯誤、無 `ap_start_low_heap`、無 DMA 相關告警**。
- 提交 Wi-Fi 後裝置以 `software_reset` 重開，重開後才連線 STA
  （2,327 → 3,377 ms 完成，`sta_network ip=…`），因此「AP 與 STA 併存並同時刷新」
  也不會在這條路徑上出現。
- 唯一的 W 級是手機離開 AP 後的 `httpd_sock_err: error in recv : 113`
  （EHOSTUNREACH）三次，屬 socket 清理，非缺陷。

結論：本項不再列為待驗證——併發被實作刻意排除，而唯一真實存在的併發情境
（AP 活躍時的輪播刷新）已有成功證據。

## 2026-08-20 — forced-BUSY isolation 與 SNTP 失敗側

### forced-BUSY：面板卡死時 HTTP 必須照常回應

把面板的 BUSY 訊號從 `GPIO4`（`kPanelBusyGpio`）改接到 GND，驅動的
`gpio_get_level(kBusyPin) != 0` 因此永遠讀不到 idle。觸發一次刷新，同時每秒
探測 `/api/v1/health`。

| 檢查 | 結果 |
| --- | --- |
| 逾時時間 | serial `carousel_image_queued` → `carousel_request` 相隔 **61.0 s**，符合 `kBusyTimeoutMs = 60'000` |
| 失敗分類 | `outcome=3`（`busy_timeout`）、`stage=2`（`DriverStage::initial_busy`，reset 後等待面板回報 idle 的第一步） |
| **HTTP 可用性** | 60 秒期間 **80 次探測全部 200，零非 200** |
| 系統狀態 | 面板 `failed`，未崩潰、未 boot loop、Wi-Fi 與其他服務不受影響 |

**這是 AGENTS.md「HTTP handler 不得等待面板刷新」那條邊界的直接證據**：
DisplayTask 整整卡住一分鐘，HTTP 完全沒有被拖累。

同一條邊界的另一面在測試前意外出現：面板刷新期間送出
`POST /api/v1/weather/config` 得到 **`503 flash_busy`**——handler 立即回報而不是
等待 flash 鎖釋放。

**恢復**：把 BUSY 接回 `GPIO4` 後，下一次刷新即
`outcome=1 refreshed_and_slept`、`stage=10 deep_sleep`，面板自行恢復，不需要
任何額外操作。

**順帶涵蓋到的情境**：測試過程中 BUSY 曾一度既未接地也未接回 GPIO4（浮空）。
對韌體而言浮空與卡在低電位是同一種故障——**BUSY 線鬆脫等同於面板卡死**——
連續四次刷新都乾淨地在 60 秒後回報 `busy_timeout`，包含重開機之後。也就是說
排線鬆脫的症狀是「畫面不再更新，但裝置一切照常回應」，而不是裝置失去回應。
此時 `carousel.current_image` 維持 `null`，符合「沒成功就不謊報」的原則。

### SNTP 失敗側

把 `ntp_server` 設為 `192.0.2.1`（TEST-NET-1，保證無回應）後重開機：

| 檢查 | 結果 |
| --- | --- |
| 時間同步 | `sntp: "syncing"`，超過 `kTimeSyncTimeoutMs`(300 s) 後仍是 `syncing`——持續嘗試而不進入終態失敗 |
| 其他服務 | `flash`／`psram`／`config`／`imagefs` 全 ready，Wi-Fi connected，`/api/v1/images` 200 |
| 面板 | 開機刷新正常完成（`refreshed_and_slept`），輪播照常 |
| 天氣 | serial `time_sync_wait_timeout=300000ms; attempting fetch anyway`，隨後**抓取成功**（`state=available`、`last_failure=none`） |

值得記下的一點：即使本地時鐘未同步，`observed_at_epoch_s` 仍是正確的
`2026-08-20T14:15:18`——那個值取自 OpenWeather 回應的 `dt` 欄位（伺服器時間），
不依賴裝置時鐘。因此 SNTP 失效不會讓天氣顯示錯誤的觀測時間。

測後 `ntp_server` 已還原為 `pool.ntp.org`。

## 2026-08-20 — 斷電故障注入：PFR1 上傳、catalog 交易、OTA 下載

由使用者實際拔除 USB 電源，共五次，每次事後比對同一組基線：14 張圖、
id 集合 `[15,16,17,19,20,21,22,23,25,26,27,28,29,30]`、
`imagefs_used_bytes = 1257472`。

### 測試檔與命中時機的做法

上傳測試用一份**未壓縮**的 PFR1（176,051 bytes，800×440）：壓縮版只有 90 KB，
在區網上傳太快、來不及拔電；未壓縮的 payload 又正好是原始 packed nibbles，
順帶驗證韌體對「宣告尺寸 vs 實際 payload 長度」的檢查。以
`curl --limit-rate 1500` 把單次上傳拉長到約 117 秒，拔電時機才可控。

catalog 的單次寫入只有幾毫秒，無法瞄準，因此改成每 0.4 秒交替兩種排序連續改寫，
讓 `.catalog.pfc1.part` → rename 交易成為裝置大部分時間都在做的事。

### 結果

| # | 情境 | 中斷點 | 結果 |
| --- | --- | --- | --- |
| 1 | PFR1 上傳 | 64.4 s（約 55%） | catalog 14 張不變、無 `powerfail` 項目洩漏、無 corrupt、`imagefs_used` 回到 1257472 |
| 2 | PFR1 上傳 | 69.4 s（約 59%） | 同上；開機 log 為 `storage_worker_ready recovery=none action=no_change` |
| 3 | PFR1 上傳 | 前段 | 同上，`recovery=none` |
| 4 | catalog 交易 | 第 83 次寫入（前 82 次已完成） | 排序落在**原始順序**——兩個有效狀態之一，不是半套；metadata 零漂移、無 corrupt、`recovery=none` |
| 5 | OTA 下載 | 寫入 `ota_1` 期間 | 復電後**仍是 `v0.9.1-probe`**，半寫入的 slot 未被啟用；`update_state=idle` 沒有卡住；圖片與 imagefs 不受影響 |

第 5 項另外驗證了「中斷不會讓 OTA 永久壞掉」：緊接著重新觸發更新，
0%→100% 正常完成，重開後 `Loaded app from partition at offset 0x290000`、
`reboot_reason=software_reset`、`rollback_confirmed=ESP_OK`、版本為 `v0.9.2`。

### 值得記下的一點

三次 PFR1 上傳中斷後，開機都是 `recovery=none action=no_change`——**不是**「recovery
清掉了殘留」，而是根本沒有殘留可清。`.upload.part` 的內容在通過驗證前不會進入
catalog，因此中途斷電連痕跡都不留。這比「靠開機清理來補救」更強：清理路徑只會
在更窄的時窗（檔案已建立但未完成驗證）才需要，而正常的斷電不會走到那裡。
`imagefs_used_bytes` 五次都精確回到 1,257,472，沒有任何空間洩漏。

## 2026-08-20 — OTA rollback fault injection：**保護原本並未生效**

ADR-0008 從 Phase 8 起就列著這一項。這次以刻意崩潰的韌體實測，結果分成兩段：
**同一台裝置先失敗、更新 bootloader 後才成功**。

### 測法

在拋棄式的公開測試 repo 發布一份 crash 韌體（版本 `v0.9.3`，在
`src/app_main.cpp` 的 `esp_ota_mark_app_valid_cancel_rollback()` **之前**插入
`abort()`），並把韌體的 `kGithubRepo` 暫時指向該 repo，因此正式 release 串流
完全未被觸碰。裝置端照常「檢查更新 → 立即更新」。測後 repo 與所有臨時改動都已
移除。

`abort()` 的位置是關鍵：otadata 會停在 `PENDING_VERIFY`，因此**只需要一次崩潰
加一次重開**就足以觸發回滾，不必製造長時間 crash-loop。

### 第一次：無限 crash-loop，完全沒有回滾

| 觀察 | 結果 |
| --- | --- |
| OTA 寫入 | `esp_https_ota: Writing to <ota_1> partition at offset 0x290000` |
| 重開後 | `Loaded app from partition at offset 0x290000` → `FAULT_INJECTION` → `abort()` → `Rebooting...` |
| 再重開 | **仍然** `Loaded app from partition at offset 0x290000`，再次 crash |
| 結果 | 至少三輪且未停止；裝置無法自行恢復，必須以 esptool 救援 |

**根因**：回滾邏輯在 **bootloader** 裡，而 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
是 2026-08-01 的 commit `22b1898`（Phase 8 OTA）才加入 `sdkconfig.defaults` 的。
這台裝置的 bootloader 是 2026-07-29／30 初次 provisioning 時燒錄的，早於該設定。
讀回 `0x0` 的 32 KB 與本機建置的 `bootloader.bin` 比對，**31.6% 的位元組不同**
（22,784 bytes 中有 7,203 bytes 相異）——裝置上的 bootloader 從未包含回滾邏輯。

**而 OTA 只寫 app slot，從不更新 bootloader**，所以這個缺口無法靠 OTA 自行修復。

**這推翻了先前的推論**：本檔多處記錄的 `rollback_confirmed=ESP_OK` 只證明
**app 呼叫了確認 API**，完全不代表 bootloader 會在異常時回滾。兩件事在此之前
被當成同一件事看待。

### 第二次：手動更新 bootloader 後，回滾正常運作

以 esptool 寫入 `0x0` 的新 bootloader 與 `0x8000` 的 partition table（`nvs` 與
`imagefs` 未觸碰），重跑同一個測試：

| 時序 | 事件 |
| --- | --- |
| OTA | `Writing to <ota_1> partition at offset 0x290000` |
| 重開 1 | `Loaded app from partition at offset 0x290000` → `FAULT_INJECTION: aborting before rollback confirmation` → `abort() was called` → `Rebooting...` |
| 重開 2 | **`Loaded app from partition at offset 0x10000`** ← 回滾到前一版 |
| 回滾後 | `reboot_reason=panic_or_watchdog`、`rollback_confirmed=ESP_OK`、`/api/v1/device` 回 `v0.9.2` |
| 資料 | 圖片庫 14 張完好（僅 `current` 因輪播變動），設定與 Wi-Fi 全數保留 |

**ADR-0008 的回滾保護在 bootloader 正確的前提下確實有效**，且如預期只需一次崩潰
加一次重開。

### 待決定（產品層級，非實作問題）

1. 現場任何在 2026-08-01 之前燒過 bootloader 的裝置**都沒有回滾保護**，且無法
   透過 OTA 補救。release note 是否需要說明、是否提供重新燒錄 bootloader 的指引？
2. 是否要在開機時偵測 bootloader 是否具備回滾能力並示警？
3. `RELEASE_CHECKLIST.md` 的 rollback 項目是否要加上「確認裝置 bootloader 版本」
   這個前提？

## 2026-08-20 — AP portal 流程與 STA retry exhaustion

延續前一段的 blank-NVS 情境：抹除 `nvs` 後由使用者讀出面板上的 AP 密碼（隨機產生、
只顯示在面板），PC 以 `netsh wlan` 建立 profile 連上 `PaperFrame-Setup-xxxx`
（取得 `192.168.4.2`），直接對 `192.168.4.1` 測試。測後 profile 連同其中的密碼已刪除，
NVS 也已寫回原始映像。

### 存取邊界（安全規則的精確驗證）

| 請求 | 未建立管理密碼 | 建立管理密碼後 |
| --- | --- | --- |
| `GET /api/v1/auth/status` | 200，`password_configured: false` | 200，`password_configured: true` |
| `GET /api/v1/device` | 200 | — |
| `GET /api/v1/health` | 200 | — |
| **`GET /api/v1/wifi/scan`（未登入）** | **202 scanning（bootstrap 例外生效）** | **401 `authentication_required`** |
| `GET /api/v1/wifi/scan`（已登入） | — | 200，回傳網路清單 |
| `GET /api/v1/status`（未登入） | 401 | 401 |
| `GET /api/v1/images`（未登入） | 401 | 401 |

這正是「wifi scan/config 只有在尚未建立管理密碼且位於首次 provisioning AP 時才可
未登入 bootstrap」的實機證據：同一個 endpoint 在建立密碼前後分別是 202 與 401。

掃描結果包含實際環境中的兩個網路（含目標 AP，RSSI −29），格式為
`{ssid, rssi, security}`。首次建立管理密碼經 `POST /api/v1/auth/login` 完成，
回應 `password_created: true`。

### STA retry exhaustion → 自動 fallback AP

透過 portal 填入**正確 SSID 與刻意錯誤的密碼**（`POST /api/v1/wifi/config` 回
`202 saving`），裝置寫入憑證後軟體重開（`reboot_reason=software_reset`），接著：

| 時間 | 事件 |
| --- | --- |
| 2,337 → 5,367 ms | 第一次連線：`init → auth → assoc → run`，隨後 `run → init (0xf00)` |
| 7,787 → 10,827 ms | 第二次連線：同樣的循環與失敗 |
| 11,137 ms | `ap_mode_display_waiting_for_ready`——放棄 STA，開始 AP 流程 |
| 42,287 ms | `provisioning_screen_ready`（面板刷完 AP 指示頁，約 31 s） |
| 42,377 ms | `provisioning_ap_ready ssid=... ip=192.168.4.1` + DHCP server 啟動 |

**自動 fallback 行為正確**：連不上時不會卡住，會回到可再次配網的 AP，且同樣是
先把 AP 指示頁刷上面板才啟動 AP。

**待釐清的觀察**：實機只嘗試了 **2 次**連線就 fallback，而
`NetworkPolicy::maximum_sta_attempts` 為 3，`test_network_state_machine` 也斷言
`sta_attempt()` 會遞增到 3。兩次 `run → init (0xf00)` 之間沒有第三次 auth 循環，
第二次失敗後 310 ms 就進入 AP 流程。可能是一次 WPA2 認證失敗產生多個 disconnect
事件讓狀態機計數快進，尚未查證。影響輕微（少一次重試等於更快 fallback），但設定值
與實際行為不一致，值得後續確認。

### 仍未驗證

面板狀態列在 AP 頁上的視覺結果由使用者確認（見同日另一段）；AP/Wi-Fi 併發刷新、
presence 例外與低 DMA heap guard 仍待驗。

## 2026-08-20 — 破壞性測試：blank NVS、AP grace window、NVS 損壞

### 讓破壞性測試可逆

先以 `esptool read-flash 0x9000 0x4000` 把整個 `nvs` 分割區 dump 成映像
（16,384 bytes，3 頁有資料），測完再 `write-flash` 寫回。因此不需要知道 Wi-Fi
密碼——API 只回報 `ssid_set`，讀不到 SSID 與密碼——也不必事後重新配網。
還原後 `/api/v1/config`、`/api/v1/weather/config`、`/api/v1/sensors/config`、
`/api/v1/images` 四個回應與備份**位元相同**，裝置連回原 IP，管理密碼可用。
圖片不受影響（`imagefs` 在 `0x630000`，與 `nvs` 無關）。

### blank NVS → provisioning AP（Phase 3/4，部分關閉）

| 檢查 | 結果 |
| --- | --- |
| 啟動決策 | `config_schema=2 action=initialize_defaults` |
| 憑證狀態 | `network_credentials_configured=false`、`management_password_configured=false` |
| 面板 | `ap_mode_display_waiting_for_ready` → `provisioning_screen_ready request=1`（32.7 s，一次完整刷新） |
| AP 啟動 | `provisioning_ap_ready ssid=PaperFrame-Setup-3AED ip=192.168.4.1`，**在面板刷新完成之後**才啟動 |
| 對外可見 | PC `netsh wlan show networks` 掃到該 SSID，**WPA2-Personal + CCMP**（非開放網路） |
| 密碼保護 | AP 密碼由 `esp_fill_random()` 產生 12 bytes 熵（`PF-` + 12 字元，32 字母表），**不可由 MAC／SSID 推導**，且不出現在 log 中 |

**仍未驗證**：連上 AP 之後的 portal 流程（未建密碼時的 `auth/status`、bootstrap
未登入 `wifi/scan`、建立管理密碼、填入 Wi-Fi）。AP 密碼只顯示在面板上，自動化
測試讀不到，需要有人看著面板操作。

### AP grace window（AP grace policy，5 分鐘切換關閉）

`ap_mode_display_window_started` @ 33,559 ms → `ap_mode_display_window_expired`
@ 333,559 ms，**正好 300,000 ms**，與 `kApModeImageTimeoutMs` 一致。

**但這次實測抓到 grace window 從來沒有真正生效**（詳見下節）。修正後同一情境：
window 過期後 **270 ms** 就 `carousel_image_queued id=15 request=2`，接著
`carousel_request=2 outcome=1` 完成刷新。

`docs/hardware/VALIDATION.md` 2026-08-03 段落當時只做 host/build 驗證，抓不到
這個缺陷——它需要一台 blank-NVS 裝置在 AP 模式停留超過五分鐘才會現形。

### NVS 損壞：`nvs_flash_init()` 失敗實際上無法觸發

ADR-0017 的 `nvs_initialized == false` 分支試了兩種破壞方式：

| 寫入 `0x9000` 的內容 | 結果 |
| --- | --- |
| 16 KB 隨機資料 | `action=initialize_defaults`，ESP-IDF 自動格式化 |
| 16 KB 全 `0x00` | 同上 |

兩次都伴隨 `phy_init: failed to load RF calibration data (0x1102)`，佐證 NVS 確實
被重寫過。結論：**`nvs_flash_init()` 會把任何無效內容視為未初始化並自我修復**，
因此在凍結的 partition 配置下該分支不可觸發，屬防禦性程式碼。要真正觸發需要
分割區本身缺失或大小為 0，而 partition table 由 ADR-0004 凍結。

「NVS 滿導致 `pf_config` 開啟失敗」仍未驗證：需要構造一份塞滿的 NVS 映像或
用縮小 `nvs` 的自訂 partition 建置測試韌體。考量 16 KB 只存少量設定、該情境
實務罕見，暫列為已知未驗證的低風險項。

## 2026-08-20 — 面板狀態列視覺與長時間輪播（使用者實機確認）

以下兩項由使用者在實機上確認通過，本節記錄結論與其證據粒度。

| 項目 | 結論 | 證據粒度 |
| --- | --- | --- |
| 面板狀態列視覺結果（Phase 6） | 通過 | 使用者目視確認；未留存照片或分狀態（正常／失敗／無網路）的逐項對照 |
| 長時間圖片輪播（Phase 5） | 通過 | 使用者實機確認；未留存 serial log、實際時長與 heap 走勢 |

**這兩筆與本檔其他段落的證據強度不同**，據實標示以免日後被當成有完整量測依據：
沒有 serial log、時長數字或 heap 趨勢可回溯。若之後要把長時間輪播升級為可追溯的
證據，需要的是連續錄製的 serial log（`carousel_request=<id> outcome=1` 是否每期
不漏拍、internal heap 是否單向下滑）與實際運行時數；面板狀態列則需要天氣正常、
天氣失敗與無網路三種狀態各一張照片。

## 2026-08-20 — 天氣失敗分類（Phase 6 本項自此關閉，並修掉一個誤報）

### 測法

`api_key_invalid` 用真實 OpenWeather（真實 TLS）觸發；其餘三種真實 API 不可能
按需產生，改用臨時測試韌體把 `weather_worker_esp_idf.cpp` 的 URL 指向本機 mock
端點，回應模式由 mock 的 `/set?mode=` 遠端切換，因此整組只需燒一次測試韌體。
測完 URL 已還原成正式端點（僅保留下述修正）。

天氣沒有週期計時器（ADR-0014），每個案例都以「啟用一張非目前圖片」觸發面板刷新，
再由 `request_immediate_refresh()` 帶動抓取。

### 結果

| 情境 | 來源 | `last_failure` | `internet` | serial |
| --- | --- | --- | --- | --- |
| 有效 key | 真實 API | `none`（`state=available`、29.4 °C） | reachable | — |
| 無效 key | 真實 API | `api_key_invalid` | reachable | `weather_http_status=401` |
| HTTP 500 | mock | `http_error` | reachable | `weather_http_status=500` |
| 200 但缺 `main`／`weather` | mock | `parse_error` | reachable | — |
| 200 但 JSON 語法壞掉 | mock | `parse_error` | reachable | — |
| 連線被拒（mock 停止） | — | `network` | unreachable | `weather_fetch_failed=ESP_ERR_HTTP_CONNECT` |

所有失敗情境下既有快取都保留（`state` 仍為 `available`），符合「不得偽造或丟棄
既有觀測」的原則。

### 過程中發現並修正的缺陷

**`classify_http_status()` 的 401 分支在真實情境下永遠執行不到。** ESP-IDF 的
`esp_http_client_perform()` 對「401 且回應未帶 `WWW-Authenticate`」直接回
`ESP_ERR_NOT_SUPPORTED`（`esp_http_client.c` 中 `auth_header == NULL` 的分支），
而 OpenWeather 的無效 key 回應正是這種——perform 在狀態碼被檢查之前就失敗，
於是走了 `apply_failure(Failure::network)`。

後果不只是標籤錯：同一分支還呼叫 `report_internet(false)`，**因此打錯 API key 會讓
Dashboard 顯示「Internet 無法連線」**，把除錯方向引去查 Wi-Fi 而不是剛輸入的 key。
修正前後實測對照：

| | 修正前 | 修正後 |
| --- | --- | --- |
| `last_failure` | `network` | `api_key_invalid` |
| `internet` | `unreachable` | `reachable` |
| serial | 僅 `weather_fetch_failed=ESP_ERR_NOT_SUPPORTED` | 另有 `weather_http_status=401` |

第一版修正還漏掉一個案例（由 codex 審查指出）：伺服器回 2xx 但 body 中途中斷時
`classify_http_status(200)` 為 `none`，仍會 fallback 成 `network` 並回報
unreachable。現行規則改為單一判準——**收到任何狀態碼就代表請求離開了 LAN**，
只有完全沒有回應（status 維持 0）才算 `network`；2xx／殘留 3xx 配上失敗的 perform
歸為 `http_error`（與既有的「回應被截斷」處理一致）。低於 400 的狀態碼不再送進
`classify_http_status()`，因為 ESP-IDF 可能留下它內部處理過的 3xx，殘留值不該被
報成伺服器錯誤。

判定邏輯已抽成 `pf_weather::classify_perform_failure()` 並有 5 個 host test；先前
放在 worker 內時，native 套件編不到那條 ESP-IDF 路徑，等於修正本身沒有測試守著。

## 2026-08-20 — browser 出圖管線、webfs heap 差值與 CI 覆蓋

### 瀏覽器出圖與下載（Phase 3/4 本項自此關閉）

以無外部函式庫產生的 PNG 測試圖（漸層＋飽和色塊＋格線，讓 dither、palette
snapping 與方向錯誤都可見）在 Chromium 實際跑完整條管線。

| 檢查 | 橫向來源 1200×800 | 直向來源 800×1200 |
| --- | --- | --- |
| 處理後 canvas | 800×440 | 480×760（需在 `#image-orientation` 選 portrait） |
| frame 預覽 | 800×480 | 480×800 |
| 上傳結果 | ID 31 `landscape.pfr1`、46,070 bytes | ID 32 `portrait.pfr1`、54,000 bytes |
| catalog metadata | 800×440、`orientation=landscape`、`corrupt=false` | 480×760、`orientation=portrait`、`corrupt=false` |
| 下載 `/download` | HTTP 200，位元組數與 catalog 一致 | 同左 |
| PFR1 header | magic `PFR1`、version 1、header_size 32、palette 1、dithering 1、reserved 皆 0 | 同左 |
| `header_crc32` / `payload_crc32` | 皆通過 | 皆通過 |
| 壓縮 | `flags=0x0008`，解壓後 **176,000** = 800×440/2 | 解壓後 **182,400** = 480×760/2 |
| 壓縮率 | 26.2% | — |

palette index 僅出現 0–6，符合 E6 六色。輸出 profile **不會依來源圖方向自動切換**，
必須在 UI 選擇；直向來源在預設的 landscape profile 下會被 fit 成 800×440，這是既有
設計而非缺陷。

測後兩張測試圖已刪除，catalog 回到原本 14 張、`current=25`，
`imagefs_used_bytes` 回到 1,257,472（與上傳前完全相同），順帶確認刪除交易會正確
回收空間。

過程中踩到一次 session 失效：以 curl 登入會撤銷瀏覽器既有 session。這是
[AUTHENTICATION.md](../AUTHENTICATION.md) 已載明的設計（裝置只保留一組 server-side
session），不是缺陷——但混用 curl 與瀏覽器測試時必須注意。

### 移除 webfs 掛載後的 heap 差值（嵌入式 WebUI 本項自此關閉）

在兩個版本的 `src/app_main.cpp` 相同位置（`rollback_confirmed` 之後、所有服務已啟動、
尚未服務任何 WebUI 請求）插入同一段臨時量測，各燒錄開機一次；量完即還原。

| 版本 | webfs | internal_free | internal_largest | dma_free | psram_free |
| --- | --- | ---: | ---: | ---: | ---: |
| `v0.8.2` | 掛載（`total=1048576 used=200704`） | 134,755 | 61,440 | 126,967 | 7,632,104 |
| `v0.9.2` | 未掛載 | 136,327 | 63,488 | 128,539 | 7,632,168 |
| 差值 | | **+1,572** | **+2,048** | +1,572 | +64 |

移除 webfs 掛載後 internal free heap 多出約 **1.5 KB**，最大連續區塊多出 2 KB
（LittleFS 的 cache buffer 配置在 internal heap，PSRAM 幾乎不受影響）。

**這個數字的限制要講清楚**：它是 `v0.8.2` 到 `v0.9.2` 的**淨差**，其間還包含
OTA、WebUI 與重開機邏輯等其他變更，不是 webfs 掛載的單獨貢獻。要精確隔離必須在
同一 commit 上做「有／無掛載」的 A/B，那需要把已被 ADR-0016 移除的掛載程式碼加回
來，成本高於這個數字的價值。

### CI 覆蓋缺口（已修）

`.github/workflows/ci.yml` 與 `release.yml` 原本只 build 一個 embedded 測試環境。
每個 environment 有各自的 `test_filter`，因此 `test_display_task` 從未在 CI 編譯，
而 `test_epd7in3e_driver` **不被任何 environment 的 filter 涵蓋**，必須加 `-f` 才會
被建置——它自撰寫以來從未在 CI 編譯過。另外 `test/*.mjs`（如
`test_partition_layout.mjs`）既不在 `test/web/*.mjs` 的迴圈裡，也不是
`pio test -e native` 會執行的 Unity 套件；`test/test_active_ota_upload.py` 同樣沒有
被任何 CI 步驟涵蓋。

三者本機都能通過，所以是覆蓋缺口而非壞掉。修正後 CI 實跑確認六個新步驟全部
success（run 32347525634）。

## 2026-08-20 — v0.9.2 release 與 OTA 驗證

本次 release 包含 Dashboard 狀態顯示修正、天氣定期抓取遺留清理，以及重開機
延後與網路關機路徑修正。

### Release 資產

| 檢查 | 結果 |
| --- | --- |
| CI / Release workflow | 皆 success |
| 資產 | `paperframe-firmware.bin`、`paperframe-partitions.csv`、`paperframe-licenses.zip`、`SHA256SUMS` |
| OTA 固定 URL | `releases/latest/download/paperframe-firmware.bin` → HTTP 200、1,294,448 bytes |
| firmware SHA-256 | `378312c6ddaeb6ebd30e47a6c0db8051b976dba148fd10c2650ca5f29b768405`，與 `SHA256SUMS` 相符 |
| partition CSV SHA-256 | `427fd414…5870`，仍等於 ADR-0004 凍結值 |
| `kFirmwareVersion` | `v0.9.2`，與 tag 一致 |

### 端到端 OTA（v0.9.1 → v0.9.2）

| 檢查 | 結果 |
| --- | --- |
| 檢查更新 | `update_available`、`latest_version=v0.9.2` |
| 下載與寫入 | 0%→92%，約 26 秒；`Writing to <ota_0> partition at offset 0x10000` |
| 重開後版本 | **`v0.9.2`**，`Loaded app from partition at offset 0x10000` |
| boot validation | `rollback_confirmed=ESP_OK` |
| **再次重開機** | 仍為 `v0.9.2`、仍自 `0x10000` 開機 → rollback confirmation 生效 |
| 圖片庫 | 14 張，重開前後 id 集合一致；`imagefs used=1257472` 不變 |
| 前端換版 | 裝置供出的 `/` 含本次新增的 `light-sensor-state`，確認 WebUI 隨韌體換版 |
| **本次修正在 OTA 路徑生效** | 重開機時出現 `network_action_skipped_for_reboot action=2`，**全程無任何 E 級 log**（修正前此處必定出現 `E network_action_failed action=2`） |

這是重開機修正第一次走真實 OTA 路徑，確認延後與網路抑制邏輯在 OTA 觸發的重開機
上與管理員觸發的行為一致。

### 仍未驗證

- Phase 2：panel sleep 電流、forced-BUSY isolation。
- Phase 3/4：blank-NVS／fallback AP 的瀏覽器流程、browser 出圖與下載收尾、
  SNTP 失敗側。
- Phase 5：壓縮 PFR1 交易中斷電、catalog 交易中斷電、長時間輪播、
  imagefs preservation 的 fault injection（本次只證明正常 upload／OTA 不動它）。
- Phase 6：weather 的 `api_key_invalid`／`network`／`http_error`／`parse_error`
  四種分類、面板狀態列視覺結果。
- Phase 7：整段（感測器尚未接上）。
- Phase 8：**rollback fault injection**（仍是唯一沒有證據的核心保護機制）、
  OTA 下載途中斷電。
- AP grace policy：整段。
- 嵌入式 WebUI：移除 webfs 掛載後的 heap 差值量化（缺改動前對照值）。
- 設定降級：`nvs_flash_init()` 失敗、NVS 滿導致 `pf_config` 開啟失敗。

## 2026-08-23 — 光敏電阻擴充為兩個獨立通道：僅完成 host／build 驗證

### 變更範圍

依 [ADR-0018](../adr/0018-dual-photoresistor-channels.md) 把單一光敏電阻改為
兩個獨立通道：通道 1 維持 GPIO5（`ADC1_CH4`），新增通道 2 為 GPIO7
（`ADC1_CH6`）。兩顆各自有 enable 與 threshold，判定為 OR——任一顆已啟用且
`online` 的通道低於自己的 threshold 就算暗。`SensorSettings` NVS blob 升到 v2
並保留 v1 遷移；`GET /api/v1/sensors` 的 `light` 物件改為攜帶 `channels[]` 與
`deciding_channel`；WebUI 環境頁改為兩組設定與兩組即時讀值。

### 已通過的驗證

| 項目 | 結果 |
| --- | --- |
| `pio run` | ✅ SUCCESS（RAM 32.6%、Flash 49.5%） |
| `pio test -e native` | ✅ 333/333（此前 308，本次新增 25 個 case） |
| `test/web/*.mjs`（11 支） | ✅ 全數通過，含新增的 `test_sensor_form_contract.mjs` |
| embedded build ×3 | ✅ `test_runtime_coordinator`、`test_display_task`、`test_epd7in3e_driver` |
| 新測試的 red-capable 驗證 | ✅ 把 ui.js 的 `light2_threshold` 改名為 `light_threshold_2` 後，`test_sensor_form_contract.mjs` 如預期報 `the firmware accepts "light2_threshold" but ui.js never submits it`；還原後恢復通過 |

新增的 `test_sensor_form_contract.mjs` 是針對一個既有的結構性缺口：表單欄位名
是 ui.js 與 C++ `parse_sensor_config_form()` 之間的純字串契約，任一邊改名都
不會有任何編譯或測試失敗，只會在實機上變成 400 `unknown_field`。該測試雙向
比對兩邊的欄位集合。

### 尚未驗證（需要實體光敏電阻）

**本段沒有任何實機驗證。** 硬體尚未接線，以下全部仍為未驗證風險：

- **兩個通道的 ADC 校正**：GPIO5／GPIO7 在正常室內、手遮、全暗、直射四種
  條件下的原始讀值範圍，以及據此決定的兩組 threshold 與 `R_fix`。韌體把
  raw ≤ 10 或 ≥ 4085 判為 `saturated` 且不觸發離席，因此整個關心的光照範圍
  必須落在 raw 11–4084 之間——這一點只能實機確認。
- **「任一通道變暗」的實機行為**：遮住通道 2、通道 1 維持亮的情況下是否
  如預期進入 AWAY；`deciding_channel` 是否正確指向被遮的那顆。
- **單通道降級**：只接一顆、或第二顆中途拔除時，另一顆是否照常運作
  （host test 已覆蓋合併邏輯，但 ADC 在實體未接線時的浮動讀值行為未知——
  未接線的 ADC 腳未必落在 saturated 區間，這正是每通道 enable 存在的理由）。
- **v1 → v2 設定遷移的實機路徑**：既有裝置 NVS 內若存有 v1 記錄，OTA 後是否
  保住原本的 threshold 與 durations。目前只有 host test 覆蓋解碼與長度判別，
  沒有在真實 NVS 上跑過。
- AWAY／PRESENT debounce 的實機時序、白屏 sleep 與返回重繪、環境頁的
  browser 行為——這些在 2026-07-31 就已列為未驗證，本次變更未改變其狀態。

## 2026-08-23 — 新板初始化燒錄：完整流程實機通過

第一次依 [FLASHING.md](FLASHING.md)「新裝置首次燒錄」對一塊全新
ESP32-S3-N16R8 走完整流程，該節文件在本次之前只有推導、沒有實跑證據。

### 進 ROM 的實際情況

新板出廠韌體用的是 **USB-OTG（TinyUSB CDC，`303A:4001`）**，不是內建的
USB Serial/JTAG，因此 esptool 的 `--before usb-reset`／`default-reset`／
`no-reset` 三種方式**全部失敗**（`No serial data received`）。必須依
FLASHING.md「首次或復原時進入 ROM」用 BOOT+RST 手動進 ROM；進入後 port
重新枚舉為 `303A:1001`（USB Serial/JTAG），COM 編號也換了一個。

**同一個坑要記住**：進 ROM 用的 GPIO0 接地線若沒拔掉，後續 hard reset 會
再次落回 download mode（`boot:0x21 (DOWNLOAD(USB/UART0))`），看起來像燒錄
失敗，其實燒錄早就成功了。拔掉後為 `boot:0x29 (SPI_FAST_FLASH_BOOT)`。

### 燒錄與驗證結果

| 區段 | 位址 | 結果 |
| --- | --- | --- |
| bootloader | `0x0` | ✅ `Hash of data verified.` |
| partition table | `0x8000` | ✅ `Hash of data verified.` |
| firmware（`ota_0`） | `0x10000` | ✅ `Hash of data verified.` |
| OTA metadata | `0xd000` | ✅ `erase-region 0xd000 0x2000` |
| factory imagefs | `0x630000` | ✅ 10,289,152 bytes = `0x9D0000`，與 partition 大小完全吻合 |

燒錄前以 esptool 驗證晶片：ESP32-S3、**16 MB flash**、**Embedded PSRAM 8MB
(AP_3v3)**。partition CSV 的 SHA-256 為 `427fd414…5870`，與 ADR-0004 凍結值
一致。

開機 log 確認：QIO / 80 MHz / 16 MB；octal PSRAM 8 MB @ 80 MHz、
`SPI SRAM memory test OK`；partition table 八個分割區與 CSV 完全一致；
`Loaded app from partition at offset 0x10000`；
`filesystem=imagefs mounted=true total=10289152 used=8192`；
`rollback_confirmed=ESP_OK`；`carousel_request=1 outcome=1`
（`refreshed_and_slept`，面板實際完成一次全刷）；
`provisioning_ap_ready ssid=PaperFrame-Setup-… ip=192.168.4.1`。

### 這塊板從出廠就有回滾保護

已確認燒進去的 bootloader binary 是由目前設定建置：
`.pio/build/paperframe-s3/bootloader/config/sdkconfig.h:339` 有
`#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1`。舊板需要手動補燒
bootloader 才有回滾保護（見 2026-08-20 rollback fault injection 段落），
**在 2026-08-01 之後才首次燒錄的新板不需要**。

### 本次發現的產品缺陷（已修，尚未實機驗證）

空圖庫的裝置永遠看不到自己的區網 IP：welcome frame 的狀態列印著 IP，但
開機時 Wi-Fi 還沒拿到位址，而 `CarouselScheduler::poll()` 在圖庫為空且
welcome 已顯示後會永久停住，不再刷新。使用者因此無法得知位址、進不了
WebUI 上傳第一張圖。修正見
[ADR-0015 Update 2026-08-23](../adr/0015-first-image-waits-for-ntp-and-weather.md)。
**修正本身只有 host test（340/340）與 mutation 驗證，配網 → 取得 IP →
面板重畫的完整流程尚未在裝置上跑過。**

### 同日補充：welcome frame 取得 IP 後重畫 — 實機驗證通過

本文件同日稍早那段記錄「修正本身只有 host test 與 mutation 驗證，配網 →
取得 IP → 面板重畫的完整流程尚未在裝置上跑過」——該項**已於同日完成實機驗證**，
以本段為準。

裝置：新板（已完成首次燒錄），已設定 Wi-Fi 與管理密碼，**圖庫為空**
（`imagefs used=8192`），韌體 `v0.9.3-4-g62766c9-dirty`。

```
I (808)   paperframe: carousel_welcome_queued request=1
I (1968)  wifi:state: init -> auth
I (3038)  esp_netif_handlers: sta ip: <REDACTED>, mask: 255.255.255.0
I (31878) paperframe: carousel_request=1 outcome=1 next_due_ms=1831178
I (31878) paperframe: carousel_welcome_redraw_for_ip ip=<REDACTED>
I (31898) paperframe: carousel_welcome_queued request=2
I (63898) paperframe: carousel_request=2 outcome=1 next_due_ms=1863198
```

確認的行為：

| 觀察 | 意義 |
| --- | --- |
| 第一張 welcome 在 t≈0.8s 送出，IP 在 t≈3.0s 才到 | 重現了原始缺陷的成因：畫面畫好時位址還不存在 |
| `carousel_welcome_redraw_for_ip` 出現在 t=31.878s，與第一次刷新完成同一時刻 | 刷新進行中 `request_welcome_redraw()` 依 in-flight 保護拒絕，下一個 tick 才成功——重試路徑如設計運作 |
| 第二次 `outcome=1`（`refreshed_and_slept`） | 帶著 IP 的畫面確實刷上面板 |
| `next_due_ms=1863198`（約 30 分鐘後） | 重畫後排程器重新停住，**一次位址變更只換一次刷新**，沒有反覆刷 |

修正前的行為是：第一次刷新之後 `poll()` 永久回傳
`wait_decision(UINT64_MAX)`，面板永遠停在沒有 IP 的歡迎畫面。

使用者於同日目視確認面板實際顯示內容：**IP、天氣資訊與日期均正確顯示**。

**仍未驗證**：presence 返回時的 welcome 重畫（需要光敏電阻，見 Phase 7 待驗證
項）；DHCP 續約導致位址變更時的重畫。
