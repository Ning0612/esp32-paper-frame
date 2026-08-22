# ESP32-S3 燒錄操作

本文件記錄 YD-ESP32-S3／ESP32-S3-N16R8 雙 USB-C 開發板在 Windows 11
上的已驗證燒錄方式。COM 編號會在重新插拔或切換 ROM／app 後改變；實際 port
必須每次重新辨識，不得永久寫入設定。

## 日常開發：單一命令

目前 PaperFrame app 正常執行且 native USB 沒有被 monitor 佔用時，只需：

```powershell
.\.venv\Scripts\pio.exe run -e paperframe-s3 -t upload
```

不需指定 COM、不需按 BOOT/RST，也不需將 GPIO0/GPIO46 接地。專案設定會：

1. 依 board manifest 的 Espressif native USB hardware ID 自動選擇 ESP32-S3 native USB，
   不會選到板載 CH343 或其他 USB serial device；
2. 以 esptool `usb_reset` 讓正在執行的 app 進入 ROM download mode；
3. 讀取 `otadata`，依 ESP-IDF bootloader 的 `ota_seq` 規則選出目前啟動的
   `ota_N`，只擦寫該 app image；不再假設固定 offset `0x10000`；
4. 保留 bootloader、partition table、OTA metadata、NVS、reserved 的
   `webfs` 與 `imagefs`，完成 hash 驗證後 hard reset。

成功輸出應包含 `Auto-detected: COM...`、`USB mode: USB-Serial/JTAG`、
`Hash of data verified.` 與 `Hard resetting via RTS pin...`。若目前是空白板、
RGB demo、native USB 被停用，或 app 已損壞到無法接受 `usb_reset`，才改用
下方「首次或復原時進入 ROM」流程。

這個 app-only 開發流程會更新目前啟動中的 slot，讓重置後直接執行剛編譯的
韌體；正式 OTA 更新仍須由 OTA contract 寫入非執行中的 slot，不能把這個
開發 uploader 當成正式 OTA 實作。

`data/web/*`（前端）在 build 時 gzip 後編入 app image
（[ADR-0016](../adr/0016-embed-webui-assets-in-firmware.md)），所以上面這一個
命令就是完整部署：改前端不需要任何額外的燒錄步驟，每次 build 前都會自動
重新產生嵌入資產。

**絕對不要用 `pio run -t uploadfs`（任何 env）**：這個 partition table
有兩個 `spiffs` subtype 分割區（`webfs`、`imagefs`），PlatformIO 內建
`uploadfs` 的分割區選取邏輯（`fetch_fs_size()`，逐筆覆寫、取 CSV 裡最後
一個符合的分割區）會選到排序在後面的 `imagefs`（`0x630000`），但內容
來源固定是 `data/web/*`——等於把 WebUI 檔案寫進使用者的圖片儲存區。
2026-08-01 已實測確認並清空受污染的 `imagefs`，詳見
`docs/hardware/VALIDATION.md` 對應日期記錄。WebUI 改為編入韌體之後，
這個指令**更沒有任何正當用途**：跑它只會毀掉 `imagefs`。

## 新裝置首次燒錄（含 bootloader）

**日常命令不會寫 bootloader。** `tools/platformio_native_usb_upload.py` 刻意
把 PlatformIO 原本附加在 flash-size 之後的 bootloader、partition table 與初始
OTA metadata 全部移除，只留下 app image 的 esptool flags——這正是 active-slot
上傳的前提。因此：

- 全新或已抹除的板子**只跑 `pio run -t upload` 會燒不起來**：flash 裡沒有
  bootloader，晶片無從啟動。
- 既有裝置**無論更新幾次韌體、做過幾次 OTA，bootloader 都不會改變**。它只來自
  這一節的首次燒錄。

新板子（或 bootloader 需要更新時）用下列流程。先建置，再一次寫入三個區段：

```powershell
.\.venv\Scripts\pio.exe run -e paperframe-s3
if ($LASTEXITCODE -ne 0) {
  throw 'Firmware build failed; do not continue to write-flash.'
}

.\.venv\Scripts\python.exe -m esptool --chip esp32s3 --port <COM> `
  --before usb-reset --after hard-reset write-flash `
  0x0 .pio\build\paperframe-s3\bootloader.bin `
  0x8000 .pio\build\paperframe-s3\partitions.bin `
  0x10000 .pio\build\paperframe-s3\firmware.bin
```

三個區段都應回報 `Hash of data verified.`。這個範圍**刻意不含**
`nvs`（`0x9000`）與 `imagefs`（`0x630000`）：既有裝置照這個流程更新 bootloader
不會遺失 Wi-Fi 憑證、管理密碼、天氣設定或使用者圖片。

全新板子還需要兩件事：

1. **重設 OTA metadata**，讓 bootloader 從 `ota_0` 啟動（既有裝置若 otadata
   指向一個已被覆寫的 slot 也需要）：
   ```powershell
   .\.venv\Scripts\python.exe -m esptool --chip esp32s3 --port <COM> `
     --before usb-reset --after hard-reset erase-region 0xd000 0x2000
   ```
2. **factory imagefs image**（只有新機或明確要清空圖片時才做，見本文件的
   imagefs 章節）。

之後的日常更新就回到開頭那一個 PlatformIO 命令。

## bootloader 是否具備回滾保護

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 在 2026-08-01 才加入
`sdkconfig.defaults`。**回滾邏輯位於 bootloader，而 OTA 與日常 upload 都不會更新
bootloader**，所以在那之前燒錄過 bootloader 的裝置沒有回滾保護：OTA 裝上一個會在
`esp_ota_mark_app_valid_cancel_rollback()` 之前崩潰的韌體時，它會無限 crash-loop
而不是退回舊版，只能靠 esptool 救援。2026-08-20 已在實機上重現這個情形，並確認
更新 bootloader 後回滾恢復正常（見
[VALIDATION.md](VALIDATION.md) 同日的 rollback fault injection 段落）。

要判斷手上的裝置是否受保護，把 flash 開頭讀回來與本機建置的 bootloader 比對：

```powershell
.\.venv\Scripts\pio.exe run -e paperframe-s3
.\.venv\Scripts\python.exe -m esptool --chip esp32s3 --port <COM> `
  --before usb-reset --after no-reset read-flash 0x0 0x8000 device_bootloader.bin

.\.venv\Scripts\python.exe -c @'
import hashlib, pathlib
local = pathlib.Path(r'.pio/build/paperframe-s3/bootloader.bin').read_bytes()
device = pathlib.Path('device_bootloader.bin').read_bytes()[:len(local)]
print('match:', device == local)
print('local :', hashlib.sha256(local).hexdigest()[:32])
print('device:', hashlib.sha256(device).hexdigest()[:32])
'@
```

讀回的是整個 `0x8000` 區塊，因此只比對前 `len(bootloader.bin)` bytes。
`match: False` 代表 bootloader 與目前程式碼不同步——若該裝置需要回滾保護，就依
上一節重寫 `0x0`。注意這個比對只回答「是否與目前建置相同」，不單獨回答「是否
啟用回滾」；兩者不同步時最省事的處理就是直接更新。

## USB 介面

| 介面 | Windows hardware ID | 用途 |
| --- | --- | --- |
| ESP32-S3 native USB Serial/JTAG | Espressif native USB hardware ID | 已驗證可進 ROM、執行 `esptool` 與監看 app console |
| 板載 USB-to-UART adapter | adapter hardware ID（已省略） | 本次在 ROM download mode 沒有收到資料，不作為標準燒錄路徑 |

每次操作前先重新辨識：

```powershell
.\.venv\Scripts\pio.exe device list
$nativePort = '<NATIVE_USB_PORT>' # 以當次 hardware ID 的實際 COM 取代
$flashRunId = Get-Date -Format 'yyyyMMdd-HHmmss'
```

以下命令應在同一個 PowerShell session 依序執行。任何命令丟出錯誤時立即
停止，不得跳過 guard 繼續燒錄。

## 首次或復原時進入 ROM

若目前 app 無法由 USB 自動重置進 ROM：

1. 使用可傳資料的 USB 線連接 ESP32-S3 native USB port。
2. 將 `GPIO0` 固定接到 `GND`。
3. 將 `GPIO46` 固定接到 `GND`。
4. 短按 `RST`；燒錄完成前保留兩條接地線。
5. 使用 native USB COM 驗證 ROM，而不是 CH343 COM。

正確 ROM log 包含：

```text
boot:0x0 (DOWNLOAD(USB/UART0))
waiting for download
```

進入 ROM 後，以唯讀命令確認連線：

```powershell
$otaData = Join-Path $env:TEMP "paperframe-otadata-$flashRunId.bin"
if (Test-Path -LiteralPath $otaData) {
  throw "Refusing existing OTA metadata file: $otaData"
}

.\.venv\Scripts\python.exe -m esptool `
  --chip esp32s3 `
  --port $nativePort `
  --before no-reset `
  --after no-reset `
  read-flash 0xd000 0x2000 $otaData

if ($LASTEXITCODE -ne 0) {
  throw 'Reading OTA metadata failed; do not continue to write-flash.'
}
if ((Get-Item -LiteralPath $otaData -ErrorAction Stop).Length -ne 0x2000) {
  throw 'OTA metadata length is not 0x2000; do not continue.'
}
```

若 native USB 回報 `Write timeout`，先確認沒有 monitor 占用同一 COM，並從
ROM log 確認晶片確實是 download mode。不要改用 `erase-flash`、eFuse 或
反覆嘗試 CH343。

## 已啟用 native USB 的 app

PaperFrame test firmware 保留 USB Serial/JTAG；app 正常執行時，以下命令已
驗證可免接 GPIO，自動重置進 ROM：

```powershell
$autoOtaData = Join-Path $env:TEMP "paperframe-otadata-auto-$flashRunId.bin"
if (Test-Path -LiteralPath $autoOtaData) {
  throw "Refusing existing OTA metadata file: $autoOtaData"
}

.\.venv\Scripts\python.exe -m esptool `
  --chip esp32s3 `
  --port $nativePort `
  --before usb-reset `
  --after no-reset `
  read-flash 0xd000 0x2000 $autoOtaData

if ($LASTEXITCODE -ne 0) {
  throw 'USB auto-reset or OTA metadata read failed.'
}
if ((Get-Item -LiteralPath $autoOtaData -ErrorAction Stop).Length -ne 0x2000) {
  throw 'OTA metadata length is not 0x2000; do not continue.'
}
```

若失敗，回到前一節同時拉低 `GPIO0`、`GPIO46` 的流程。

## App-only 開發燒錄

日常操作使用本文件開頭的 PlatformIO 單一命令。下列低階流程保留給首次
bring-up、備份／還原與 uploader 診斷。

先建立正式韌體：

```powershell
.\.venv\Scripts\pio.exe run -e paperframe-s3
if ($LASTEXITCODE -ne 0) {
  throw 'Firmware build failed; do not continue to write-flash.'
}
```

目前開發 partition table 的 `ota_0` 位於 `0x10000`、大小 `0x280000`，
`ota_1` 位於 `0x290000`。日常命令會自動讀取 OTA metadata；以下低階範例
仍以 `ota_0` 為例，只有在讀取結果確認 `ota_0` 是 active app 時才可照做。
若 active slot 是 `ota_1`，必須把範例中的 `ota_0`／`0x10000` 換成
`ota_1`／`0x290000`：

```powershell
$backup = Join-Path $env:TEMP "paperframe-ota0-backup-$flashRunId.bin"
if (Test-Path -LiteralPath $backup) {
  throw "Refusing existing backup file: $backup"
}

.\.venv\Scripts\python.exe -m esptool `
  --chip esp32s3 `
  --port $nativePort `
  --before no-reset `
  --after no-reset `
  read-flash 0x10000 0x280000 $backup

if ($LASTEXITCODE -ne 0) {
  throw 'Backing up ota_0 failed; do not continue to write-flash.'
}
$backupFile = Get-Item -LiteralPath $backup -ErrorAction Stop
if ($backupFile.Length -ne 0x280000) {
  throw 'ota_0 backup length is not 0x280000; do not continue.'
}
$backupSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $backup).Hash
Write-Host "Backup: $backup"
Write-Host "SHA-256: $backupSha256"

$firmware = Get-Item -LiteralPath `
  '.pio\build\paperframe-s3\firmware.bin' -ErrorAction Stop

.\.venv\Scripts\python.exe -m esptool `
  --chip esp32s3 `
  --port $nativePort `
  --baud 460800 `
  --before no-reset `
  --after no-reset `
  write-flash `
  --flash-mode dio `
  --flash-freq 80m `
  --flash-size 16MB `
  0x10000 $firmware.FullName

if ($LASTEXITCODE -ne 0) {
  throw 'Writing the app failed; keep the backup and stop.'
}
```

`esptool` 必須回報 `Hash of data verified.`。以 manual straps 進 ROM
時，app-only 操作完成後要拔除 GPIO0/GPIO46 接地線並短按 `RST`；若使用
`--before usb-reset`，則依命令的 `--after` 行為 reset。

若 OTA metadata 無法判定 active slot，停止並重新讀取／診斷；不得把
`0x10000` 當作永久固定的 OTA 更新位置。正式 OTA 更新應走 OTA contract，
不得直接覆寫 running slot。

一般 app-only 燒錄不得改寫：

- `0xd000` 的 OTA metadata；
- `0x510000` 的 `webfs`（reserved，見 ADR-0016）；
- `0x630000` 的 `imagefs`；
- NVS、partition table 或 eFuse。

## WebUI 更新

WebUI 沒有獨立的燒錄流程：`data/web/` 的內容在 build 時 gzip 後編入 app
image，因此一般 app upload 或一次 OTA 就會同時更新韌體與前端，見
[ADR-0016](../adr/0016-embed-webui-assets-in-firmware.md)。

`0x510000` 的 `webfs` 分割區仍保留在 partition table 中（ADR-0004 凍結的
layout 未變），但已不掛載、不寫入。既有裝置上的殘留內容**刻意不清除**：
bootloader 回滾到舊版韌體時，舊版仍需要那份 WebUI 才能運作。

## Embedded e-Paper pattern test

建立六色直條測試韌體：

```powershell
.\.venv\Scripts\pio.exe test `
  --project-conf platformio-embedded.ini `
  -e paperframe-s3-embedded-test `
  -f test_epd7in3e_driver `
  --without-uploading `
  --without-testing

if ($LASTEXITCODE -ne 0) {
  throw 'Embedded pattern-test build failed; do not continue.'
}
```

依前節備份 `ota_0`，再把以下檔案寫到 `0x10000`：

```powershell
$currentBackupFile = Get-Item -LiteralPath $backup -ErrorAction Stop
if ($currentBackupFile.Length -ne 0x280000) {
  throw 'ota_0 backup length changed; do not write the test app.'
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $backup).Hash -ne
    $backupSha256) {
  throw 'ota_0 backup hash changed; do not write the test app.'
}
$testFirmware = Get-Item -LiteralPath `
  '.pio\build\paperframe-s3-embedded-test\firmware.bin' -ErrorAction Stop

.\.venv\Scripts\python.exe -m esptool `
  --chip esp32s3 `
  --port $nativePort `
  --baud 460800 `
  --before no-reset `
  --after no-reset `
  write-flash `
  --flash-mode dio `
  --flash-freq 80m `
  --flash-size 16MB `
  0x10000 $testFirmware.FullName

if ($LASTEXITCODE -ne 0) {
  throw 'Writing the pattern-test app failed; keep the backup and stop.'
}
```

寫入完成後：

1. 拔除 `GPIO0`、`GPIO46` 的接地線。
2. 短按 `RST`。
3. 確認面板依序顯示垂直黑、黃、紅、藍、綠、白色帶。
4. 測試後重新進 ROM；若用 straps 進入，確認 ROM sync 後先拔除兩條接地
   線，晶片會留在 ROM，restore 完成時的 hard reset 才能啟動 app。
5. 把備份寫回 `0x10000`：

```powershell
$restoreBackupFile = Get-Item -LiteralPath $backup -ErrorAction Stop
if ($restoreBackupFile.Length -ne 0x280000) {
  throw 'ota_0 backup length changed; refusing restore.'
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $backup).Hash -ne
    $backupSha256) {
  throw 'ota_0 backup hash changed; refusing restore.'
}

.\.venv\Scripts\python.exe -m esptool `
  --chip esp32s3 `
  --port $nativePort `
  --baud 460800 `
  --before no-reset `
  --after hard-reset `
  write-flash `
  --flash-mode dio `
  --flash-freq 80m `
  --flash-size 16MB `
  0x10000 $backup

if ($LASTEXITCODE -ne 0) {
  throw 'Restoring ota_0 failed; keep the backup and stop.'
}
```

恢復命令也必須回報 `Hash of data verified.`。確認一般 app 已重新枚舉後，
才可刪除 backup；電子紙會保留最後畫面，
恢復一般 app 不會立即清除已顯示的六色色帶。

## 實測補充（2026-08-23，第一次走完整新板流程）

- **新板不一定聽 esptool 的自動重置**。出廠韌體若用 USB-OTG（TinyUSB CDC，
  hardware ID `303A:4001`）而非內建 USB Serial/JTAG（`303A:1001`），
  `--before usb-reset`、`default-reset`、`no-reset` 三種都會失敗並回報
  `No serial data received`。這不是線材或 port 選錯，直接改用下方「首次或
  復原時進入 ROM」的 BOOT+RST 手動流程即可。進 ROM 後 port 會重新枚舉成
  `303A:1001`，**COM 編號會變**，要重新辨識。
- **燒完記得把 GPIO0 的接地線拔掉**。留著的話後續 hard reset 仍會落回
  `boot:0x21 (DOWNLOAD(USB/UART0))`，開機 log 只有 `waiting for download`，
  看起來像燒錄失敗——實際上四個區段可能早就 `Hash of data verified.` 了。
  正常啟動應為 `boot:0x29 (SPI_FAST_FLASH_BOOT)`。
- 已在 ROM 裡時，後續 esptool 命令用 `--before no-reset` 即可，不需要再
  觸發一次 usb-reset。
- 新板**必須**燒 factory imagefs image：`filesystem_manager.cpp` 的
  littlefs 設定是 `format_if_mount_failed = false`，空白 flash 不會被自動
  格式化，不燒的話 `imagefs` 會掛載失敗。

完整的一次實測紀錄（含晶片驗證、開機 log 與各區段 hash 結果）見
[VALIDATION.md](VALIDATION.md) 的 2026-08-23 段落。
