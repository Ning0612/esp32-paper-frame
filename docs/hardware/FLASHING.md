# ESP32-S3 燒錄操作

本文件記錄 YD-ESP32-S3／ESP32-S3-N16R8 雙 USB-C 開發板在 Windows 11
上的已驗證燒錄方式。COM 編號會在重新插拔或切換 ROM／app 後改變，命令中的
`COM10` 只是 2026-07-30 實測值，不得永久寫入設定。

## 日常開發：單一命令

目前 PaperFrame app 正常執行且 native USB 沒有被 monitor 佔用時，只需：

```powershell
.\.venv\Scripts\pio.exe run -e paperframe-s3 -t upload
```

不需指定 COM、不需按 BOOT/RST，也不需將 GPIO0/GPIO46 接地。專案設定會：

1. 依 board manifest 的 VID:PID `303A:1001` 自動選擇 ESP32-S3 native USB，
   不會選到板載 CH343 或其他 USB serial device；
2. 以 esptool `usb_reset` 讓正在執行的 app 進入 ROM download mode；
3. 只擦寫 `ota_0` 的 app image（目前 offset `0x10000`）；
4. 保留 bootloader、partition table、OTA metadata、NVS、`webfs` 與
   `imagefs`，完成 hash 驗證後 hard reset。

成功輸出應包含 `Auto-detected: COM...`、`USB mode: USB-Serial/JTAG`、
`Hash of data verified.` 與 `Hard resetting via RTS pin...`。若目前是空白板、
RGB demo、native USB 被停用，或 app 已損壞到無法接受 `usb_reset`，才改用
下方「首次或復原時進入 ROM」流程。

此命令是 Phase 8 正式 OTA 導入前的開發流程；開始切換 active OTA slot 後，
必須改由 OTA contract 選擇非執行中 slot，不得直接沿用固定的 `0x10000`。

## USB 介面

| 介面 | Windows hardware ID | 用途 |
| --- | --- | --- |
| ESP32-S3 native USB Serial/JTAG | VID:PID `303A:1001` | 已驗證可進 ROM、執行 `esptool` 與監看 app console |
| 板載 CH343 UART | VID:PID `1A86:55D3` | 本次在 ROM download mode 沒有收到資料，不作為標準燒錄路徑 |

每次操作前先重新辨識：

```powershell
.\.venv\Scripts\pio.exe device list
$nativePort = 'COM10' # 以當次 VID:PID 303A:1001 的實際 COM 取代
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
`ota_1` 位於 `0x290000`。2026-07-30 實測 OTA metadata 為 sequence 1、
state `VALID`，所以 active app 是 `ota_0`。在 OTA 功能導入前，可先備份
整個 active slot，再只改寫 app：

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

若 OTA metadata 不再是上述
sequence/state，停止並重新判定 active slot；不得把 `0x10000` 當作永久
固定的 OTA 更新位置。Phase 8 導入正式 OTA 後，更新應走 OTA contract，
不得直接覆寫 running slot。

一般 app-only 燒錄不得改寫：

- `0xd000` 的 OTA metadata；
- `0x510000` 的 `webfs`；
- `0x630000` 的 `imagefs`；
- NVS、partition table 或 eFuse。

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
