# ADR-0011：PFR1 壓縮功能的 PSRAM／flash cache-disable 交錯安全性調查

- Status: accepted
- Date: 2026-08-02
- Supersedes: none
- Related: ADR-0009（壓縮功能決策）、ADR-0010（`kCatalogMaxEntries`
  撤銷，過程中發現本次調查的觸發點）

## Context

ADR-0010 的 RAM 重構過程中發現：ADR-0009 已上線的 PFR1 壓縮功能
（`components/pf_storage/storage_worker.cpp` 的
`inflate_compressed_scratch_`／`inflate_output_scratch_`，配置在 PSRAM）
在圖片上傳／串流迴圈裡跟 flash 讀寫在同一個 task、同一個迴圈本體裡交替
執行，而這個 pattern 從未被拿去跟本專案 OTA 那次的教訓對照過——OTA 曾經
因為「同一 task 讀 PSRAM 又寫 flash，cache-disable 期間 PSRAM 存取安全性
未驗證」而完全撤回一個功能（`github_response_buffer_` 相關）。「目前沒
出事」不等於「已驗證安全」：ESP32-S3 上 flash 與 PSRAM 共用同一個
cache-arbitration 機制（`spi_flash_disable_cache()` →
`esp_cache_suspend_ext_mem_cache()` → `cache_hal_suspend(
CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_ALL)`，`CACHE_TYPE_ALL` 涵蓋 flash 與
PSRAM 兩者），且本專案 `sdkconfig.paperframe-s3` 確認
`CONFIG_SOC_MEMSPI_CORE_CLK_SHARED_WITH_PSRAM=y`，代表這是晶片層級的真實
限制，不是舊晶片才有的過時考量。本 ADR 記錄針對這個具體風險的原始碼追蹤
結果與結論。

## Investigation

### 1. `esp_partition_write()`／`esp_partition_read()` 的 cache-disable 實際範圍

呼叫鏈：`littlefs_esp_part_write()`/`littlefs_esp_part_read()`
（`managed_components/joltwallet__littlefs/src/littlefs_esp_part.c`）
→ `esp_partition_write()`/`esp_partition_read()`
（`.pio/packages/framework-espidf/components/esp_partition/
partition_target.c`）→ `esp_flash_write()`/`esp_flash_read()`
（`.pio/packages/framework-espidf/components/spi_flash/esp_flash_api.c`）。

`esp_flash_write()`（`esp_flash_api.c:1095-1215`）與 `esp_flash_read()`
（`esp_flash_api.c:931-1005`）都是**同步、阻塞**呼叫：`start(chip)` 進入
cache-disable 臨界區、`chip_drv->write/read(...)` 執行實際 SPI 傳輸、
`end(chip, err)` 離開臨界區並重新啟用 cache，全部包在單一函式呼叫內部，
呼叫者拿到回傳值時 cache 保證已重新啟用——不存在「函式已 return 但 cache
還沒恢復」的情況。

**兩者都有條件性的 DRAM 防禦層——條件是這個專案實際用的 flash host／
driver**：`esp_flash_write()`／`esp_flash_read()` 是否經由 DRAM 暫存區
中轉，取決於 `chip->host->driver->supports_direct_write/read(...)`
（`esp_flash_api.c:947`、`:1118`）的回傳值，這個函式指標依 host 而不同，
**不是所有 driver 都會拒絕直接存取非 DRAM 緩衝區**：
`esp_hal_mspi/spi_flash_hal_gpspi.c` 的 GPSPI driver 對 direct
read/write 一律回傳 `true`（不檢查 buffer 指標，允許直接存取），但這個
driver 是給接在 GPSPI host 上的外接 flash 晶片用，不是這個專案的內部
主 flash 路徑。本專案 `webfs`／`imagefs`／`nvs` 等 partition 走的是內部
主 flash，經 `esp_hal_mspi/spi_flash_hal.c` 的 `spi_flash_hal_supports_
direct_write/read()`（第 154-166 行），邏輯是
`(host->spi != spi_flash_ll_get_hw(SPI1_HOST))`——內部主 flash 固定用
`SPI1_HOST`，這個判斷式恆為 `false`，也就是**在這個專案實際的硬體路徑
下**，direct access 永遠不被支援，DRAM 暫存中轉一定會被觸發。下方結論
限定在「本專案內部主 flash（SPI1_HOST）」這個前提下成立，不是
`esp_flash_write()`/`esp_flash_read()` 的通用保證。

在這個前提成立的條件下：`esp_flash_write()` 用
`esp_ptr_in_dram(buffer)`（`esp_flash_api.c:1116`）判斷來源緩衝區是否為
內部 DRAM；若不是（例如 PSRAM），會在**進入 cache-disable 之前**先把
資料 `memcpy` 進一塊保證在 DRAM 的暫存區（`esp_flash_api.c:1113` 註解：
「when the cache is disabled, only the DRAM can be read, check whether
we need to copy the data first」），disable 期間實際 SPI 寫入用的緩衝區
一定是這塊 DRAM 暫存。`esp_flash_read()` 對稱：非 DRAM 目的緩衝區會先讀
進 DRAM `temp_buffer`（`esp_flash_api.c:948-963`），`end()` 重新啟用
cache **之後**才 `memcpy` 回原始（可能是 PSRAM）緩衝區
（`esp_flash_api.c:987-991`）。另外 `esp_ptr_in_dram()` 只檢查緩衝區
**起始位址**是否落在 DRAM 範圍，不驗證整個緩衝區長度都在範圍內——本 ADR
下方引用的所有緩衝區都是固定大小的 stack 陣列（見第 2 節），起始位址
落在 DRAM 即代表整段都在同一個 stack frame 內，這個邊界情況對本次調查
的緩衝區不構成風險，但若未來有跨越 DRAM 邊界的緩衝區傳入，需要另外
評估。

`cache_utils.c:81-86` 的官方註解進一步證實 PSRAM cache 與 flash cache
共用同一份、非 per-CPU 的實體快取（"the psram cache is *not*
[separate]"），`cache_utils.c:206`：「Re-enable cache. After this, cache
(flash and external RAM) should work again」明確把 flash 與 external
RAM（PSRAM）並列同一句——確認 `EXT_MEM` 這個 cache level 是兩者共用的
實體硬體，OTA 那次的顧慮是晶片層級真實限制。

cache-disable 視窗的精確時長（微秒等級）無法單從原始碼靜態確認，需要
示波器或邏輯分析儀量測；但這不影響下方結論，因為結論不依賴視窗大小。

### 2. 三條 PFR1 相關存取路徑的實際存取模式

本次調查涵蓋這個功能實際會用到 PSRAM scratch buffer 的**全部三條**
路徑（上傳寫入、carousel 顯示讀取、開機復原讀取），讀完
`components/pf_storage/image_store.cpp:202-299`（上傳／寫入）、
`components/pf_storage/storage_worker.cpp:312-410`（`stream_image`，
carousel 顯示的讀取路徑）與 `components/pf_storage/recovery.cpp:
139-197`（`read_image`，開機復原時驗證既有圖片的讀取路徑）三段完整
實作後確認：**三條路徑裡，`filesystem.write()`／`filesystem.read()`
從未直接拿到過 PSRAM 指標**，且各自呼叫端所在的 task stack 都已確認
是 internal DRAM（見下方「task stack 位置佐證」）。

**寫入路徑**（`image_store.cpp`，執行於 `pf_image_up`/`pf_image_mut`
task）：
- `image_store.cpp:237`：`std::uint8_t buffer[kImageStoreChunkBytes]{};`
  是 stack 區域變數。
- `image_store.cpp:241-245`：`reader.read()` 把 HTTP body chunk 填進這個
  stack buffer。
- `image_store.cpp:263`：`validator.feed(buffer, amount)` 傳入的是這個
  stack buffer；`Pfr1Validator::feed()` 內部才把資料**寫入**
  PSRAM 的 `inflate_buffers_->compressed`。
- `image_store.cpp:271`：`filesystem.write(handle, buffer, amount)`
  傳入的同樣是這個 stack buffer，不是任何 PSRAM scratch buffer。
- `image_store.cpp:288`：`validator.finish()` 才**一次性**呼叫
  `tinfl_decompress_mem_to_mem` 把 PSRAM 的 `compressed` 解壓縮進 PSRAM
  的 `output`，這個解壓縮發生在整個上傳串流迴圈跑完之後，跟迴圈內逐
  chunk 的 `filesystem.write()` 呼叫在時間上完全不重疊。

**讀取路徑**（`storage_worker.cpp`，carousel 顯示用，即 Phase 1c 剛動到
的 `carousel_payload`/`carousel_status`/`carousel_inflate_*` 所在路徑，
執行於 main task）架構對稱：
- `storage_worker.cpp:373`：`std::uint8_t buffer[kImageStoreChunkBytes]
  {};` 同樣是 stack 變數。
- `storage_worker.cpp:379-383`：`filesystem_->read(handle, buffer,
  capacity, bytes_read)` 讀進這個 stack buffer。
- `storage_worker.cpp:390`：`visitor(context, buffer, bytes_read)`
  （`app_main.cpp` 的 `feed_carousel_pfr1` → `decoder.feed(...)`）才把
  這個 stack buffer 的內容複製進 PSRAM 的 `carousel_payload`／
  `carousel_inflate_compressed`；同樣，這個複製動作完全在
  `filesystem_->read()` 呼叫**之外**執行，不在任何 cache-disable 窗口內。

**開機復原讀取路徑**（`recovery.cpp`，由 `StorageWorker::start()`
於 `storage_worker.cpp:217-233` 啟動，執行於 main task，跟 carousel
顯示共用同一個 task）——這條路徑先前的調查草稿遺漏了，補上：
- `recovery.cpp:158`：`std::uint8_t buffer[kImageChunkBytes]{};` 同樣是
  stack 變數。
- `recovery.cpp:163-168`：`filesystem.read(handle, buffer, sizeof
  (buffer), amount)` 讀進這個 stack buffer。
- `recovery.cpp:176`：`validator.feed(buffer, amount)` 傳入的是這個
  stack buffer；跟寫入路徑一樣，`inflate_buffers`（PSRAM）只在
  `Pfr1Validator` 內部被寫入，不經過 `filesystem.read()`。
- 這條路徑與另外兩條是同一套模式的第三個獨立實例，強化（而非改變）下方
  的架構結論。

`catalog_buffer_`（`StorageWorker` 另一個常駐成員，
`filesystem.write(catalog_handle, catalog_buffer, catalog_bytes)` 於
`image_store.cpp:382`、`:500`）是 internal RAM 的
`std::uint8_t catalog_buffer_[kCatalogMaxBytes]`，非 PSRAM，同樣安全。

**task stack 位置佐證**（回應「stack 變數不等於 internal DRAM」這個
必要前提）：
- 上傳／mutation 路徑：`pf_image_up`/`pf_image_mut` 的 task stack 由
  `health_server.cpp` 的 `start_deferred_task()` 明確以
  `heap_caps_calloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`
  配置（`health_server.cpp:236-240`），保證 internal DRAM，不是預設值
  推論。
- carousel 顯示與開機復原路徑：兩者都執行於 ESP-IDF 的 main task
  （`app_main()` 本身），`sdkconfig.paperframe-s3` 沒有設定
  `CONFIG_ESP_MAIN_TASK_STACK_IN_EXT_MEM`（或等義的 PSRAM-stack
  選項），只有一般的 `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768`，代表
  main task 的 stack 使用 ESP-IDF 預設配置方式（internal DRAM），不是
  被本專案顯式搬去 PSRAM。

## Decision

**結論：安全（結論 A），限定在本專案內部主 flash（SPI1_HOST）這個實際
硬體路徑下成立**。目前已上線的 PFR1 壓縮功能，涵蓋 `pf_image_up`/
`pf_image_mut`（寫入路徑）、carousel 顯示（`carousel_payload`/
`carousel_status`/`carousel_inflate_*`，讀取路徑）、開機復原
（`recovery.cpp` 的 `read_image`，讀取路徑）三條路徑，都不會在 flash
cache-disable 期間存取 PSRAM。雙重保障，任一成立就足夠，這裡兩者都成立：

1. **架構層面（主要理由，不依賴 driver 前提）**：本專案的
   `StorageWorker`／`recovery.cpp` 讀寫路徑一律先把資料經過 stack
   buffer 中轉，`filesystem.write()`／`filesystem.read()` 從未直接拿到
   PSRAM 指標；PSRAM scratch buffer（`inflate_compressed_scratch_`/
   `inflate_output_scratch_`/`carousel_payload`/`carousel_inflate_*`）
   只在 `Pfr1Validator`／`Pfr1FrameDecoder` 內部被存取，且都跟任何
   flash I/O 呼叫在時間上不重疊。這條理由不依賴 driver 是否支援 direct
   access，即使 driver 允許直接存取非 DRAM 緩衝區，本專案的呼叫模式也
   從來沒有把 PSRAM 指標傳給 `filesystem.write()`／`filesystem.read()`。
2. **防禦層（條件性，僅在本專案實際使用的 SPI1_HOST／memspi driver
   下成立，不是通用保證）**：見上方 Investigation 第 1 節——本專案
   `webfs`／`imagefs` 等 partition 走的內部主 flash 固定用 `SPI1_HOST`，
   `spi_flash_hal_supports_direct_write/read()` 對 `SPI1_HOST` 恆回傳
   `false`，因此 `esp_flash_write()`／`esp_flash_read()` 對非 DRAM 緩衝區
   一定會經由 `esp_ptr_in_dram()` 判斷並中轉 DRAM 暫存區。**這個防禦層
   不是 ESP-IDF 對所有 flash host 的通用保證**——`esp_hal_mspi/
   spi_flash_hal_gpspi.c` 的 GPSPI driver 對 direct access 一律回傳
   `true`，若本專案未來改用 GPSPI host（例如外接 flash），這層防禦不會
   自動生效，必須重新驗證。

因此**不需要**把 `Pfr1InflateBuffers`（`inflate_compressed_scratch_`／
`inflate_output_scratch_`／`carousel_inflate_compressed`／
`carousel_inflate_output`）或 `carousel_payload`／`carousel_status` 改回
internal RAM——目前的 PSRAM 化是安全的，不是本次 RAM 重構要處理的問題。

**與 OTA 那次撤回案例的差異**：OTA 那次是 codex-cowork 審查指出
「同一 task 也執行 `esp_https_ota` flash 寫入，PSRAM 在 flash
cache-disable 期間的存取安全性未經驗證」這個**未經驗證的風險**後，選擇
直接撤回、不深究（見 `docs/hardware/VALIDATION.md` 2026-08-01 該筆
紀錄），並不是已經證實 `github_response_buffer_` 真的在 cache-disable
窗口內被存取過。本次調查等於是把 OTA 當時沒有深入驗證的那個問題，針對
PFR1 壓縮功能實際做了一次原始碼層級的追蹤，結論是這次的資料流設計從
一開始就不是同一類風險——PSRAM scratch buffer 從未直接參與跟 flash
操作交錯的呼叫。

## Scope note

本次調查涵蓋 `pf_image_up`/`pf_image_mut`（寫入路徑）、carousel 顯示與
開機復原（兩者皆讀取路徑，共三條）。`spi_flash_disable_cache()` 期間
cache-disable 視窗的精確時長（微秒等級）未經實測量測，但如上所述，這
不影響本 ADR 的結論，因為安全性不依賴視窗大小——是架構上根本沒有在
視窗內存取 PSRAM。

本結論**限定在**：(1) 本專案內部主 flash 走 `SPI1_HOST`／memspi
driver 這個實際硬體配置；(2) 目前這三條路徑的資料流架構（一律經 stack
buffer 中轉，PSRAM 指標從不直接傳給 `filesystem.write()`／
`filesystem.read()`）。若未來新增其他會把 PSRAM 指標直接傳給
`filesystem.write()`／`filesystem.read()`（或任何其他直接呼叫
`esp_flash_write()`/`esp_flash_read()` 的路徑）的程式碼，或本專案的
flash host 配置改變（例如改用 GPSPI），**都必須重新對照本 ADR 的兩個
前提是否仍然成立，不能直接援引本結論**——尤其防禦層那條理由（見
Decision 第 2 點）明確不是 driver-無關的通用保證。

## Consequences

- 本次調查未觸發任何程式碼修改：`inflate_compressed_scratch_`／
  `inflate_output_scratch_`（`storage_worker.cpp`）、`carousel_payload`／
  `carousel_status`／`carousel_inflate_*`（`app_main.cpp`，含 Phase 1c
  剛完成的 `carousel_status` PSRAM 化）維持現狀。
- `docs/hardware/VALIDATION.md` 的「PSRAM 在 flash cache-disable 期間的
  存取安全性未經驗證」項目更新為：「已完成原始碼層級驗證（ADR-0011），
  結論為安全；cache-disable 視窗精確時長仍未實測量測，但不影響結論」。
- 未來任何新增的 PSRAM 使用若涉及跟 flash I/O 交錯，應先確認是否符合
  本 ADR 記錄的「經 DRAM stack buffer 中轉」架構前提，不符合則需要
  重新走一次本次的原始碼追蹤流程，不能假設 ESP-IDF 的防禦層必然兜底。

## Verification

- 原始碼追蹤：`esp_flash_api.c`（`esp_flash_write`/`esp_flash_read`）、
  `cache_utils.c`（cache-disable/enable 範圍與 PSRAM/flash 共用關係）、
  `esp_hal_mspi/spi_flash_hal.c` 與 `spi_flash_hal_gpspi.c`（driver 層
  `supports_direct_write/read` 的實際行為差異）、`littlefs_esp_part.c`
  （littlefs HAL 呼叫鏈）、`image_store.cpp:202-299`（寫入路徑完整
  迴圈）、`storage_worker.cpp:312-410`（carousel 讀取路徑完整
  `stream_image`）、`recovery.cpp:139-197`（開機復原讀取路徑完整
  `read_image`）、`health_server.cpp:236-240`（upload/mutation task
  stack 的 internal DRAM 配置佐證）、`sdkconfig.paperframe-s3`（main
  task stack 設定，確認未搬去 PSRAM）。
- 未做：cache-disable 視窗的實機示波器/邏輯分析儀量測；連續壓縮上傳＋
  獨立 checksum 核對的實機壓力測試。這兩項**明確保留為未驗證項目**（見
  `docs/hardware/VALIDATION.md` 對應段落）——本 ADR 的原始碼層級結論可以
  成立，但不能取代實機量測與壓力測試，讀者不應把本 ADR 誤讀為「已有
  硬體驗證」。若未來要提高信心，應優先執行這兩項。
