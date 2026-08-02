# ADR-0012：RAM 重構後重新拉高 PFC1 目錄容量上限至 64 筆

- Status: accepted
- Date: 2026-08-02
- Supersedes: ADR-0010（`kCatalogMaxEntries` 部分；ADR-0010 的根因分析與
  「未來要調整必須先實機驗證」的教訓繼續有效，本 ADR 是在完成那個前提
  之後的後續決策）
- Related: ADR-0009（原始壓縮＋目錄上限提案）、ADR-0011（PSRAM／
  flash cache-disable 安全性調查——本 ADR 的 Phase 1/2 前提之一，但只是
  確認既有 PSRAM 用法安全，不是本次容量上限的直接證據）

## Context

ADR-0010 把 `kCatalogMaxEntries` 從 96 撤回到 48，原因是 96（甚至 64）
會讓圖片上傳／mutation task 的「有請求才動態配置」internal RAM stack
配置在真機上失敗，而 `pio run` 的靜態 RAM 百分比完全看不出這個問題。
ADR-0010 明確把「未來要再調整這個常數，必須先完成一輪獨立的韌體 RAM
預算研究」列為前提。

RAM 重構的 Phase 1（見 Phase 1a/1b/1c 三個 commit：
`RecoveryWorkspace` 改開機暫時配置、weather/sensor config task stack
改按需配置、`carousel_status` 改 PSRAM＋fallback）累計回收 57,824 bytes
internal DRAM，且已完成 Phase 1 自己的實機驗證（開機復原、config 表單
冷開機首次送出、upload/mutation/carousel 皆通過）。這構成了 ADR-0010
要求的前提，因此本次重新評估目錄上限。

## Investigation：真機兩點風險探測（不是完整 bisection）

在 Phase 1 落地後的裝置上，測試了 64 與 80 兩個候選值，每個數字都在
WiFi 已連線、weather worker 有週期性活動的條件下，透過 WebUI 實際操作
上傳／設為目前／刪除／排序，觀察 `image_upload_stack_free_bytes`／
`image_mutation_stack_free_bytes` 這兩個既有的診斷 log。

**量測方式的重要限制（首次撰寫本 ADR 時誤判，經 codex-cowork 審查發現
並修正）**：這兩個 log 呼叫的是 `uxTaskGetStackHighWaterMark(nullptr)`
（`health_server.cpp:2460`、`:2616`），回傳的是**該 task 自建立以來
歷史最低的剩餘 stack 空間**，不是每次呼叫當下的即時剩餘量。`pf_image_
up`／`pf_image_mut` 這兩個 task 在同一次開機期間只建立一次、之後重複
使用（`health_server.cpp:233`：`task_handle != nullptr` 就直接
`return ESP_OK`，不重新配置），所以同一次開機期間看到的多筆
`free_bytes` 數字，反映的是「目前為止哪一次操作把 stack 用到最深」，
不是「隨著操作次數增加、餘裕正在持續流失」——同一個 task 若之後又跑了
用 stack 較淺的操作，high-water-mark 數字不會回升，但這不代表發生了
洩漏。

| `kCatalogMaxEntries` | internal RAM（`pio run`） | 上傳（16 KB stack） | Mutation（24 KB stack） |
| ---: | ---: | --- | --- |
| 64 | 32.4%（106,080 bytes） | 成功，`image_upload_stack_free_bytes=2120～2124` | 成功，`image_mutation_stack_free_bytes=5608～5912`（4 次操作記錄到的歷史最低餘裕落在這個範圍內，是健康餘裕） |
| 80 | 33.8%（110,624 bytes） | 成功，`image_upload_stack_free_bytes=2124` | 成功，但 4 次操作記錄到的歷史最低餘裕依序為 `1492 → 1492 → 1428 → 1188`——不能解讀為「持續惡化的趨勢」（那需要即時量測而非 high-water-mark），正確的解讀是：**這 4 次操作裡，最深的一次只剩 1188 bytes 餘裕** |

**80 筆的正確結論**：不是「觀察到惡化趨勢」，而是「本輪測試觀察到的
mutation task 最低餘裕（1188 bytes）低於保守安全門檻」。同一顆 24,576
byte 的 mutation stack，64 筆時記錄到的最低餘裕是 5608~5912 bytes
（約 23%~24% 餘裕），80 筆時掉到 1188 bytes（約 4.8% 餘裕）——這是
同一個 task、同一個指標的前後對照，不需要借用 upload task 的數字就足以
判斷 80 的餘裕明顯偏薄。**沒有證據顯示 80 會在後續操作中確定觸發
`deferred_task_stack_alloc_failed`**，只是餘裕已經薄到不足以保守採用。

**只測了 64／80 兩個點，不構成完整 bisection**：65–79 與 96 都沒有
測試過，不能宣稱「64 是實際臨界值」「80 已被證明不穩定」或「往上測
沒有意義」。準確的說法是：**64 是本輪唯一通過既定安全門檻的候選值**；
80 因為觀測到的最低餘裕不足而保守拒絕，但這不是「80 必然失敗」的
證明；96 未測試，不知道結果。若未來要更精確定位真正的臨界值，需要
補測 65–79 之間的候選值，且應該用即時量測（例如在每次 mutation
呼叫前後主動 log `heap_caps_get_largest_free_block`，而不是只依賴
task 自帶的 high-water-mark）取代本輪這種間接量測方式。

## Decision

`kCatalogMaxEntries` 訂為 **64**（`kCatalogMaxBytes` 隨之變為
`32 + 64 × 128 = 8,224` bytes）。這是**本輪唯一通過保守安全門檻的候選
值**——上傳與 mutation 在多次操作中記錄到的最低餘裕（mutation
5,608~5,912 bytes）遠高於 ADR-0010 記錄的「單薄」門檻（upload
~2,000～2,100 bytes，兩者是不同 task 的指標，不直接比較數字大小，但
量級上明顯更健康）。

80 不採用：本輪測試觀察到 mutation task 的最低餘裕僅 1188 bytes，低於
保守安全門檻，因此拒絕採用；**但這不是「80 已被證明會失敗」的結論**，
只有 64／80 兩個測試點，不足以確定 80 是否真的不安全，只能說本輪測試
沒有觀察到足夠的安全餘裕支持採用它。

**與 Phase 1 回收空間的對照**：Phase 1 回收 57,824 bytes，理論上足以
支撐到接近 96；但實測顯示 64 到 80 之間 mutation task 記錄到的最低
餘裕就已經從健康的 ~5,700 bytes 降到偏薄的 ~1,200 bytes，代表「回收的
靜態 bytes 總量」與「執行期動態配置需要的連續記憶體餘裕」之間的關係
不是線性的、也不能從 Phase 1 回收的總量直接反推能拉到多高——這跟
ADR-0010 的核心教訓完全一致，再次確認：任何目錄上限的調整都必須用
真機測試驗證，不能用算術推算。**本輪只做了兩點探測，不是完整
bisection**，65–79 與 96 的實際表現仍然未知。

## Consequences

- `components/pf_storage/include/pf_storage/catalog.hpp` 的
  `kCatalogMaxEntries` 從 48 改為 `64U`；`kCatalogMaxBytes` 隨之變為
  8,224 bytes。
- `docs/formats/PFC1.md` 的容量描述（64 筆、8,224 bytes）已同步更新，
  並指向本 ADR。
- **降版相容性**：跟 ADR-0010 撤銷 96→48 時的情況類似，把上限從 48
  拉到 64 是**放寬**（不是收緊），既有 48 筆以內的 catalog 在新韌體下
  仍然合法，不需要遷移程式碼；但反過來——若未來又要把上限**調低**（例如
  發現 64 也不夠穩定、改回 48），任何在 64 筆版本上實際寫入超過新上限
  的既有 catalog，**不會**走到 `CatalogError::invalid_count`（那是
  catalog parser 內部的欄位驗證錯誤）。實際路徑更早：
  `recover_image_transactions()`（`recovery.cpp:101`、`:112-128` 讀取
  catalog 時，會先用等於新韌體 `kCatalogMaxBytes` 大小的固定 buffer
  讀取檔案，讀滿後再多讀 1 byte 探測是否還有剩餘資料——舊版 catalog
  檔案若真的比新上限大，這個探測會讀到非零位元組，導致
  `read_ok = false`，函式回傳 `false`（對應 `read_failed` 情境），
  在 `storage_worker.cpp:231` 轉成 `StorageWorkerError::recovery_
  failed`，`StorageWorker::ready()` 保持 `false`，imagefs 進入
  degraded、不提供圖片服務。fail-closed 的方向與 ADR-0010 記錄的邏輯
  一致（不會猜測性接受、不會 silently truncate），但具體錯誤碼是
  `recovery_failed`／`read_failed`，不是 `invalid_count`——差異在於
  「檔案長度超出讀取 buffer」這個問題在進入 catalog 欄位解析之前就先被
  攔截了。任何 rollback 到 48 上限韌體都有這個操作風險，不只是「未來
  重新修改常數時才有風險」，屆時需要重新評估當下是否有真實超過新上限
  的 catalog 存在。
- 未來若要再往上調整（例如 80 或 96），本輪測試沒有觀察到足夠的安全
  餘裕支持直接採用；比較務實的路徑是先進一步降低 upload/mutation task
  的記憶體需求（例如縮小 stack 需求本身，或針對
  `StorageWorker::catalog_buffer_` 做按需配置——這是 RAM 重構計畫留下的
  待確認決策 #2，本次未處理），或補測 65–79 之間的候選值找出實際可行
  的範圍，兩者都需要新一輪實機驗證才能下結論，不能只靠本輪的兩點測試
  推算。

## Verification

- `pio test -e native`：`test_catalog`／`test_image_store`／
  `test_storage_worker` 皆已在 `kCatalogMaxEntries=64` 下重新確認通過
  （279/279）。
- `pio run -e paperframe-s3`：internal RAM 32.4%（106,080 bytes）。
- **實機驗證（已完成，非待辦）**：燒錄到實體 ESP32-S3-N16R8（透過
  `scripts\flash-app-and-webfs.ps1`，`imagefs` 全程未被觸碰），在 WiFi
  已連線、weather worker 有活動的條件下，透過即時序列監看與使用者
  實際 WebUI 操作交叉確認：
  - 64 筆：上傳／設為目前／刪除／排序多次操作皆成功，`free_bytes`
    （`uxTaskGetStackHighWaterMark`，task 存活期間的歷史最低值）落在
    上傳 2120~2124、mutation 5608~5912 這個範圍，屬於健康餘裕。
  - 80 筆：上傳成功，mutation 4 次操作記錄到的歷史最低餘裕為
    1188 bytes，低於保守安全門檻，因此拒絕採用；並未觀察到實際的
    `deferred_task_stack_alloc_failed`。
  - 最終定案 64 之後，已重新燒錄 64 筆版本作為裝置上的最終狀態（不是
    停在 80 筆的測試狀態）。
- **本輪未做、留給未來**：65–79 與 96 未測試；若要更精確定位臨界值，
  建議改用即時量測（`heap_caps_get_largest_free_block` 搭配明確的
  before/after log）取代 `uxTaskGetStackHighWaterMark` 這種累積指標。
