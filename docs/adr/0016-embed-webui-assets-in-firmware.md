# ADR-0016：WebUI 資產 gzip 後編入 app image，webfs 分割區轉為 reserved

- Status: accepted
- Date: 2026-08-19
- Supersedes: ADR-0008 Context 第 1 條（「OTA 只做 app 韌體，不做 webfs 的
  HTTP 上傳；webfs 更新繼續用手動 `cmake --target littlefs_webfs_bin` +
  `esptool` 流程」）
- Related: ADR-0004（凍結 partition layout，未變更）

## Context

原架構把 `data/web/*` 打包成 LittleFS image 燒進 `webfs` 分割區
（`0x510000`，1 MiB），開機由 `pf_storage::mount_all()` 掛到 `/web`，
`static_asset_handler` 以 `fopen` 逐檔供檔。

ADR-0008 明訂 OTA 只更新 app slot，因此 **OTA 後前端仍是上一次手動燒錄的
版本**。當 API 契約隨韌體演進，裝置會停在「後端新、前端舊」的狀態，而且
無法從裝置端偵測或修復——只能實體接上 USB 跑 esptool。這個故障的表現
具誤導性：`docs/hardware/VALIDATION.md` 記錄過一次實例，後端已修好、HTTP
回應已是 `200`，但裝置上的舊 `ui.js` 期待舊版回應形狀而誤判失敗，重新
整理頁面卻又能正常進入已登入畫面。

使用者決定不去「同步兩個版本」，而是消滅第二個版本：把前端編進韌體，
讓版本歪斜在結構上不可能發生。同時接受「改前端就要重編韌體」的開發流程
代價，並且**不保留 webfs 作為開發期覆蓋路徑**——保留反而會讓「線上跑的
到底是哪一份前端」重新變成不確定。

實測資料（2026-08-19）：8 個檔案共 177,074 bytes，gzip -9 後 41,913
bytes（23.7%）；`ota_0`/`ota_1` 各 2,621,440 bytes，改動前 `firmware.bin`
為 1,250,640 bytes，餘裕充足。

## Decision

### 資產編入方式

- `data/web/` 下每個檔案由 `tools/generate_web_assets.py` gzip 後產生成
  C++ translation unit（`components/pf_web/generated/web_assets_generated.{hpp,cpp}`），
  由 `pf_web` 一起編譯，落在 `.rodata`（flash DROM，實測符號位址
  `0x3c0eb38c`），不佔 RAM。生成目錄不進版控。
- **不使用 ESP-IDF 的 `EMBED_FILES` 或 `target_add_binary_data()`**。兩者
  都透過 build 階段的 CMake custom command 產生 `.S`，但 **PlatformIO 不
  執行 CMake/ninja 的 build**：它讀 CMake File API code model 取得 source
  清單，再用 SCons 自行編譯。實測結果是
  `Source '.pio/build/paperframe-s3/index.html.gz.S' not found` ——只在
  ninja build 期間才存在的檔案，對 SCons 而言不存在。改用「固定路徑的
  生成原始檔」後，該檔在 configure 時就存在，code model 看得到，SCons
  以一般 source 編譯。
- 生成器執行兩次，兩次都必要：
  1. `components/pf_web/CMakeLists.txt` 的 `execute_process`（configure 時），
     確保檔案先於 code model 存在；
  2. `tools/platformio_web_assets.py`（PlatformIO `pre:` script，每次 build），
     確保 `data/web/*` 的修改進得了韌體。**PlatformIO 只在根目錄或 `src`
     的 `CMakeLists.txt` mtime 變更時才重跑 CMake configure**，component
     層的 CMakeLists 變更不會觸發，所以少了第 2 步就會燒到舊前端。
- 壓縮結果必須具決定性（gzip header 不含檔名、`mtime=0`），且**內容未變
  就不覆寫輸出檔**。否則每次 build 都會產生不同 bytes，導致無謂重編與
  不可重現的 release 產物。實測：連續兩次 build 第二次回報 `unchanged`
  且不重新編譯。
- 不假設主機有 `gzip` 執行檔（Windows 開發環境），一律使用
  `idf_build_get_property(... PYTHON)` 取得的直譯器與 Python 標準函式庫。

### 供檔行為

- `static_asset_handler` 改為單次 `httpd_resp_send`，送出一段連續的
  flash-resident 範圍，並加上 `Content-Encoding: gzip`；不再有 512-byte
  stack chunk 迴圈，回應帶真正的 `Content-Length` 而非 chunked encoding。
- `Content-Encoding` **只加在此 handler**，不併入 `set_common_headers()`
  ——後者與 `send_json()` 及所有 JSON API 共用，那些回應不是 gzip。
- `Cache-Control: no-store`、`X-Content-Type-Options: nosniff` 與
  ADR-0014 的 CSP 完全不變。
- `webfs_unavailable`（503）錯誤路徑移除：資產缺席不再是執行期狀態，
  只保留 `asset_context`（500）作為路由接線錯誤的防禦。
- handler 無條件回 gzip，**不檢查 `Accept-Encoding`**。所有瀏覽器與預設
  `curl` 都可接受；只有明確要求 `identity` 的客戶端會拿到無法解讀的
  內容，本裝置沒有這種使用情境。這是刻意簡化，不是缺陷。

### webfs 分割區

- `partitions/paperframe-dev.csv` **完全未變更**，ADR-0004 凍結的 layout
  與其 SHA-256 `427FD414...5870` 維持有效，`test/test_partition_layout.mjs`
  持續通過。
- `webfs` 轉為 **reserved**：不掛載、不寫入、不統計、不出現在任何 API。
- 已部署裝置上的殘留 image **不做任何遷移或清除**。特別是**不得**在開機
  時 erase `0x510000`：`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 讓回滾隨時
  可能發生，而回滾後的舊韌體仍需要 `/web/*` 才能提供 WebUI；主動清除
  等於毀掉回滾後的可用性。
- 未來若要回收這 1 MiB，需另立 superseding ADR 並附既有裝置的遷移方案。

### 狀態回報

- `RuntimeSnapshot` 移除 `webfs`、`webfs_total_bytes`、`webfs_used_bytes`；
  `health_serializer::is_ready()` 移除 webfs 條件；`/health` 與
  `/api/v1/status` 移除對應欄位；WebUI 移除兩個容量列。不保留相容 stub。
- 保留 `webfs: "ready"` 會描述一個並未掛載的檔案系統；保留為 `degraded`
  會讓 `/health` 永遠是 `degraded`。兩者都違反本專案「不得偽造狀態」的
  既有原則，因此選擇移除。

### 測試

- 不新增 Unity host test：改造後的 handler 沒有可抽出的純邏輯（指標與
  長度、三行標頭設定），且 `[env:native]` 沒有 ESP-IDF component graph
  可連結 `esp_http_server`。
- 改以 `test/web/test_embedded_web_assets.mjs`（沿用既有 `test/web/*.mjs`
  的純字面比對慣例）交叉比對 `data/web/` 實際檔案清單、`health_server.cpp`
  的 `StaticAsset` 接線與 HTTP route，並釘住 `Content-Encoding: gzip`、
  `Cache-Control: no-store` 與「`Content-Encoding` 不得出現在
  `set_common_headers()`」等不變量。**新增前端檔案卻忘了註冊 route 會讓
  這個測試失敗**（已實測驗證），這是本次唯一需要人記得的同步點。

## Consequences

- **OTA 一次更新韌體與前端**，「前端版本落後於韌體」在結構上消失。
- WebUI 首次載入傳輸量由 177 KB 降至 42 KB；`Cache-Control: no-store`
  表示每次載入都受惠。
- app image 由 1,250,640 增至約 1,292,285 bytes，OTA slot 使用率
  **49.3%**（餘裕約 1.33 MB）。
- 資產進 flash DROM 而非 RAM，靜態 RAM 用量不受影響；移除 webfs 掛載
  另外省下該 LittleFS 實例的 heap（cache/lookahead 等，約數 KB 量級，
  尚未實機量測）與一個 VFS slot。移除 handler 的 512-byte stack buffer
  是 httpd worker 的 stack 餘裕改善，**不是 RAM 節省**。
- **破壞性 API 變更**：`/health` 與 `/api/v1/status` 移除 `webfs` 欄位。
  因前端與韌體現為同一份 binary，不存在版本落差，故不提供相容期。
- 開發代價：只改一個 CSS 字元也要重編並重燒整份韌體。相對地，
  `scripts/flash-app-and-webfs.ps1`、`littlefs_webfs_bin` target、release
  的 `paperframe-webfs.bin` 資產與整套「只更新 webfs」流程全部消失，
  `pio run --target upload` 即是完整部署。
- `pio run --target uploadfs` 的危害**不變**：`data/` 與
  `board_build.filesystem = littlefs` 仍在，`fetch_fs_size()` 仍會挑到
  CSV 中最後一個 spiffs 分割區（`imagefs`，`0x630000`）並把 `data/web/*`
  寫進去，清空使用者圖片。且此後不再有任何正當用途，警告必須保留。

## Verification

- `pio run -e paperframe-s3` 成功，Flash 49.3%（1,292,285 / 2,621,440）。
- `pio test -e native` 304/304 通過。
- `test/web/*.mjs` 全數通過（含新增的 `test_embedded_web_assets.mjs`）。
- `node test/test_partition_layout.mjs` 通過——證明 ADR-0004 的凍結未被破壞。
- `pio test --project-conf platformio-embedded.ini -e paperframe-s3-embedded-test
  --without-uploading --without-testing` build 通過。
- 觸發鏈實測：修改 `data/web/style.css` 後（未 touch 任何 CMakeLists）重跑
  `pio run`，生成器回報 `updated`、`web_assets_generated.cpp` 重新編譯、
  `firmware.bin` 大小改變；還原後再跑兩次，第二次回報 `unchanged` 且無重編。
- 未註冊資產的防護實測：於 `data/web/` 放入未接線的檔案後，
  `test_embedded_web_assets.mjs` 以 `... is embedded but never wired into a
  StaticAsset` 失敗。
- `littlefs_imagefs_bin` target 仍可產出 10,289,152 bytes（`0x9D0000`）的
  image，factory provisioning 流程未受影響。
- **尚未實機驗證**（列入 `docs/hardware/VALIDATION.md`）：瀏覽器對
  `Content-Encoding: gzip` 的實際解碼（含 `new Worker()` 載入的
  `image_quantize_worker.js` 與 `favicon.svg`）、`/health` 實機回應形狀、
  移除 webfs 掛載後的 heap 差值、以及一次真實 OTA 後前端同步換版。
