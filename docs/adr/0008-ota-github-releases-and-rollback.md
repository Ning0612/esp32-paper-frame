# ADR-0008：OTA 韌體來源、TLS 信任、Rollback 確認時機與 Release 慣例

- Status: accepted
- Date: 2026-08-01
- Supersedes: none

## Context

`docs/archive/IMPLEMENTATION_PLAN.md` Phase 8 要求「OTA 僅能使用 Phase 5 前已凍結的
G5 partition layout，不在本階段重新分割既有裝置」且「OTA A/B 可回滾，且不
改寫 imagefs；WebUI 更新只改寫 webfs」。掃描確認：全專案目前沒有任何
`esp_ota_*`/`esp_https_ota` 用例，`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
未啟用，`partitions/paperframe-dev.csv` 已由 ADR-0004 凍結（`ota_0`/`ota_1`
各 0x280000 bytes，`otadata` 0xD000/0x2000）。本 ADR 記錄 OTA
（`pf_ota` component）實作前必須固定的決策；partition table 本身不在本
ADR 變更範圍內。

以下範圍決策已與使用者確認（不可重新討論，記錄於此供後續維護參考）：

1. OTA 只做 app 韌體（`ota_0`/`ota_1`），不做 webfs 的 HTTP 上傳；webfs
   更新繼續用 `CLAUDE.md` 已記載的手動 `cmake --target littlefs_webfs_bin`
   + `esptool` 流程。
2. OTA 韌體來源＝裝置直連 GitHub Releases（不是瀏覽器上傳本機檔案）。
3. 更新動作由管理員手動觸發（「檢查更新」「立即更新」），不做背景輪詢。
4. Rollback 確認採開機自動模式，不做管理員手動確認 UI。

## Decision

### OTA 來源：GitHub Releases，固定檔名 redirect

- 韌體 `.bin` 透過
  `https://github.com/<owner>/<repo>/releases/latest/download/<asset-name>`
  的固定檔名 redirect 取得，**不解析 assets JSON 陣列**——只要求每個
  release 都附上檔名完全是 `paperframe-firmware.bin` 的資產（見下方
  release 慣例），避免「release 有多個資產時選哪個」的歧義。
- 「檢查更新」只解析
  `https://api.github.com/repos/<owner>/<repo>/releases/latest` 回應裡的
  單一欄位 `tag_name`（`pf_ota::extract_tag_name`，`components/pf_ota/
  include/pf_ota/github_release_check.hpp`），用跟 `pf_weather::weather.cpp`
  一樣的 bounded flat-key-scan 手法，不新增 JSON 解析能力。
- `owner`/`repo`/`asset-name` 是編譯期常數（`ota_worker_esp_idf.cpp` 內
  `kGithubOwner = "Ning0612"`、`kGithubRepo = "esp32-paper-frame"`、
  `kReleaseAssetName = "paperframe-firmware.bin"`），非使用者可設定值。
  目前本機 repo 尚未設定 git remote，`owner`/`repo` 是使用者發布時的
  預期值，之後若實際 repo 位置不同，直接改這三個常數即可，不影響其他
  設計決策。

### TLS 信任：沿用 ADR-0005 的 `esp_crt_bundle_attach`

- GitHub API 呼叫與 `esp_https_ota` 下載都使用
  `esp_crt_bundle_attach`，與 WeatherWorker 對 `api.openweathermap.org`
  的信任模式一致，不釘選、不內嵌自訂憑證。
- `esp_https_ota` 走 stepwise API（`esp_https_ota_begin` → 迴圈
  `esp_https_ota_perform` → `esp_https_ota_is_complete_data_received` →
  `esp_https_ota_finish`），不用一次性 blocking helper、也不手刻
  `esp_ota_write`：讓 ESP-IDF 處理 partition 選擇
  （`esp_ota_get_next_update_partition`）與 image header 驗證，同時讓
  worker task 能在迴圈間更新 `RuntimeSnapshot` 進度、自行檢查整體 deadline
  （5 分鐘，`OtaWorker::kUpdateOverallDeadlineMs`）。

### 版本比對：單一來源 + SemVer 2.0.0 precedence

- `pf_runtime::kFirmwareVersion`（`firmware_version.hpp`）是韌體版本字串
  的唯一來源，同時供 `serialize_device`/`serialize_status` 顯示與
  `pf_ota::OtaWorker::check_for_update()` 跟 GitHub `tag_name` 比對使用，
  避免兩處各自維護版本字串而漂移。
- `compare_semver` 容忍缺 `v` 前綴、忽略 `+build` metadata；對
  `-prerelease` suffix 依 SemVer 2.0.0 §11.4 判定 precedence（同號碼下
  無 prerelease 的一方較新），確保同號碼 prerelease 不會被誤判為可升級的
  正式版；任何無法解析的 tag（含數值 overflow）一律回傳
  `comparable=false`，由呼叫端對應到 `check_failed`，不得誤判為
  「沒有新版本」。

### 更新流程：管理員手動觸發，無背景輪詢

- WebUI「系統」頁提供「檢查更新」（`POST /api/v1/system/ota/check`）與
  「立即更新」（`POST /api/v1/system/ota/update`）兩個獨立動作；
  `OtaWorker` 用一個 atomic `busy_` flag（compare-and-swap）擋掉重疊請求，
  回傳 `false` 時 handler 回 `409 Conflict`，不排隊、不覆蓋前一個請求。
- 沒有任何排程/計時器主動觸發檢查或更新，符合本專案對「會重開機／寫
  flash 的操作一律由管理員明確觸發」的既有慣例。

### Rollback 確認：開機自動模式

- `sdkconfig.defaults` 新增 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`。
- `app_main.cpp` 在所有核心服務（config/storage/runtime/network/weather/
  sensors/provisioning/auth/health server/OTA worker）都已嘗試啟動之後，
  呼叫 `esp_ota_mark_app_valid_cancel_rollback()` 確認開機成功；若新韌體
  在這行程式碼執行前就 crash-loop，bootloader 會在下次開機自動切回舊
  分割區，不需要額外的 watchdog 專屬程式碼。
- 不做管理員手動確認 UI：這是與使用者確認過的範圍決策，權衡是「萬一新
  韌體核心服務都能啟動、但更深層的功能（例如面板刷新）才出問題」不會
  觸發自動 rollback；此風險由 Milestone 5 的「刻意燒一個延遲崩潰版本」
  實機測試項覆蓋，若證明不夠，再回頭考慮改為手動確認。

### Release 慣例（新增硬性規定）

- 每個 GitHub Release **必須**附上檔名完全是 `paperframe-firmware.bin`
  的資產，否則「立即更新」會遇到 404（分類為 `download_failed`，不會
  崩潰或卡死，但功能上等於該次 release 無法透過 OTA 取得）。
- `pf_runtime::kFirmwareVersion` 需與推送的 git tag 同步更新，寫入
  release checklist（Milestone 5）。

## Consequences

- 新 component `pf_ota`（`REQUIRES app_update esp-tls esp_http_client
  esp_https_ota esp_timer freertos mbedtls`）是本專案第一個呼叫
  `esp_ota_*`/`esp_https_ota` API 的地方；`src/CMakeLists.txt` 新增
  `app_update`/`pf_ota` 依賴以呼叫 `esp_ota_mark_app_valid_cancel_rollback`。
- OTA worker task 靜態 stack 訂為 24576 bytes（比照本專案「TLS/crypto
  相關 task」下限，即 httpd worker 因 PBKDF2 撞牆後調到的值），加上
  8192-byte GitHub API response buffer，是一次性、永久佔用的連結期成本
  （不論是否使用過 OTA 都會佔用）。實測：加入 `pf_ota` 後
  `pio run -e paperframe-s3` 的 RAM 使用從約 61.0%（199,744 bytes）跳到
  約 71.1%（232,944 bytes），與靜態配置大小吻合。**這個數字尚未經過實機
  high-water-mark 量測驗證**，`OtaWorker::task_main()` 已在每次命令處理
  完後記錄 `ota_worker_stack_free_bytes`，需在 Milestone 5 的實機驗收中
  確認剩餘空間，必要時調降或改變配置策略。
- `pf_weather`（GitHub API 檢查）與 `pf_ota`（`esp_https_ota` 下載）是
  兩個獨立的 HTTPS client，理論上可能同時活著；mbedtls/TLS session 的
  heap 成本（估計數十 KB）併發風險尚未實機量測，列入 Milestone 5
  待驗證清單，不假設併發安全。
- `partitions/paperframe-dev.csv` 完全未變更；OTA 只寫入
  `esp_ota_get_next_update_partition()` 選出的分割區，從不觸碰
  `webfs`/`imagefs`。
- 未來若要改變 OTA 來源（例如改用自架 update server）、TLS 策略或
  rollback 確認時機，需要新的 superseding ADR。

## Verification

- `pio run` 與 `pio test -e native` 全綠是本 ADR 所涵蓋所有變更的最低
  驗證門檻：`test_github_release_check`（tag_name 解析）、
  `test_firmware_version`（含新增的 overflow／prerelease precedence
  case）、`test_web_access_policy`（`ota_check_allowed`/
  `ota_update_allowed`）。
- `esp_https_ota`/真實 GitHub 連線/真實 rollback 無法 host test，須在
  `docs/hardware/VALIDATION.md` 記錄：真實下載＋寫入＋開機切換分割區、
  刻意燒一個會在 `esp_ota_mark_app_valid_cancel_rollback()` 之前就
  crash-loop 的版本以驗證自動 rollback、OTA 寫入中斷電後開機是否還在
  舊版有效分割區、無網路時「檢查更新」是否優雅失敗、OTA worker 實機
  stack high-water-mark、weather+OTA 併發時的 heap 低點。
