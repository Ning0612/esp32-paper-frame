# ADR-0010：撤銷 PFC1 目錄容量上限拉高，維持 48 筆

- Status: accepted
- Date: 2026-08-02
- Supersedes: ADR-0009（僅 `kCatalogMaxEntries` 部分；PFR1 payload 壓縮
  決策不受影響，繼續有效）

## Context

ADR-0009 決定把 `kCatalogMaxEntries` 從 48 拉到 96，理由是壓縮省下的
flash 空間需要更高的目錄筆數上限才用得到，且 `pio run` 的靜態 RAM 報告
顯示拉到 96 後 internal DRAM/BSS 使用率從 48.6%（159,376 bytes）漲到
62.6%（205,264 bytes），仍有 37% 餘裕，判斷可接受。

實機驗證（燒錄 96 筆版本到 ESP32-S3-N16R8 並實際透過 WebUI 操作）發現這個
判斷是錯的。`components/pf_web/health_server.cpp` 的圖片上傳
（`pf_image_up`，16,384 bytes stack）與 mutation
（`pf_image_mut`，activate/reorder/delete 共用，24,576 bytes stack）task
是刻意設計成「有請求才用 `heap_caps_calloc(..., MALLOC_CAP_INTERNAL |
MALLOC_CAP_8BIT)` 動態配置，平常不佔用」（見 `health_server.cpp` 該段落
既有註解，記錄了 Phase 8 時期的實機調校：48 筆基準下 upload stack 約有
6,864 bytes 餘裕、mutation stack 只有約 2,096 bytes 餘裕——後者本來就已經
很緊）。`pio run` 的靜態百分比完全不涵蓋這種「執行期才動態配置、且要求
連續記憶體區塊」的成本；`kCatalogMaxEntries` 拉高帶來的 46 KB 額外靜態
佔用，恰好就是從這個「動態配置｜連續區塊」的可用空間裡扣的。

**實機測試結果**（同一顆裝置，同一次 session，逐步調整
`kCatalogMaxEntries` 並重新燒錄）：

| `kCatalogMaxEntries` | internal RAM（`pio run`） | 上傳（16 KB stack） | Mutation（24 KB stack） |
| ---: | ---: | --- | --- |
| 96 | 62.6%（205,264 bytes） | 未測（先發現 mutation 已壞） | **失敗**：`deferred_task_stack_alloc_failed task=pf_image_mut bytes=24576` |
| 64 | 53.3%（174,672 bytes） | 成功（`image_upload_stack_free_bytes=2124`，餘裕很薄） | **失敗**：同上錯誤 |
| 48（原始基準） | 48.6%（159,376 bytes） | 成功（`image_upload_stack_free_bytes=2116～2120`） | 成功（`image_mutation_stack_free_bytes=10020～10324`，餘裕健康） |

64 筆時上傳雖然「成功」，但 2,124 bytes 的餘裕比 96 筆之前記錄的既有
mutation 基準（2,096 bytes）還單薄，且同一次測試 mutation 已經在 64 筆
就失敗。**只實測過 48／64／96 三個數字，49–63 之間沒有測過**，不能推論
64 到 96 之間哪個數字才是真正的臨界點；能確定的只有「48 已實測穩定，
64 與 96 已實測失敗」。

## Decision

`kCatalogMaxEntries` 改回 **48**（撤銷 ADR-0009 的 96）。這不是隨便挑的
保守值，而是目前這個 RAM 預算下**唯一已實測穩定**的設定；但也不代表
49–63 之間必然都不安全，只是還沒有驗證過，不值得在沒有更多實機測試的
情況下去猜一個「可能更高、也可能一樣會壞」的中間值。除非另外做一輪韌體
RAM 預算重構，否則維持 48 是目前唯一有實測依據的選擇。

**PSRAM 化為何不在本次範圍內處理**：把 task stack 或 Catalog 結構搬去
PSRAM 能不能解決這個問題，理論上可以，但這類改動會直接命中本專案先前
在 OTA 那次已經完整撤回過的同一類風險——`docs/hardware/VALIDATION.md`
記錄過：「PSRAM 在 flash cache-disable 期間的存取安全性未經驗證」，
且 upload/mutation task 本身會在同一個 task 上做 flash 寫入
（`store_image_transactionally`／`persist_catalog_transactionally`）；
如果連 task 自己的 call stack 都放在 PSRAM，flash 寫入期間 PSRAM
不可存取時，連正常的函式呼叫（push/pop stack frame）都會失敗，風險比
先前那次「只是一個資料 buffer 放 PSRAM」更直接、更嚴重。**但「搬去
PSRAM」不是唯一的後續方向**：也可以評估縮小或改成按需配置
`RecoveryWorkspace`（目前是 4 份 `Catalog` 常駐副本）、降低 upload／
mutation task 的 stack 大小需求本身、或只把「確定不會跨 flash
cache-disable 視窗被存取」的那部分資料搬 PSRAM，而不是整條 call stack
都搬。這些替代方向都需要獨立的可行性研究與實機驗證，不屬於這次「加壓縮
＋順便拉高目錄上限」的範圍，留待未來需要更高目錄上限時再開獨立 ADR
處理。

**壓縮功能本身不受影響、仍然有效**：ADR-0009 的核心價值——同樣 48 張
圖片，壓縮後每張佔用的 flash 更少——完全不需要拉高目錄筆數上限就能
兌現。使用者受益從「能存更多張圖」限縮為「同樣 48 張的前提下，flash
用量更省、有更多餘裕」，這是本次撤銷後唯一需要向使用者說明的行為調整。

## Consequences

- `components/pf_storage/include/pf_storage/catalog.hpp` 的
  `kCatalogMaxEntries` 改回 `48U`；`kCatalogMaxBytes` 回到 6,176 bytes。
- `components/pf_web/health_server.cpp` 的 `kImageOrderBodyCapacity`
  （ADR-0009 引入、從 `kCatalogMaxEntries` 推導）不需要任何程式碼改動，
  常數本身會隨著 `kCatalogMaxEntries` 改回 48 自動縮小，這正是當初把它
  設計成「從常數推導而非寫死」的價值所在。
- `docs/formats/PFC1.md` 的容量描述（96 筆、12,320 bytes）需要改回 48
  筆、6,176 bytes，並補一句指向本 ADR 說明為何沒有維持 96。
- `docs/hardware/VALIDATION.md` 的「尚未驗證」清單中「接近 96 筆容量時的
  行為」這一項連同其他跟目錄上限相關的待驗證項目一併移除或標記為
  「已驗證：上限維持 48，未擴大」，因為已經有明確的負向實機驗證結果，
  不再是「未知」。
- 未來若要重新嘗試拉高 `kCatalogMaxEntries`，必須先完成一輪獨立的韌體
  RAM 預算研究（是否要把 task stack／Catalog 結構搬去 PSRAM、如何安全
  處理 flash 寫入期間的 PSRAM 不可用視窗），不能只看 `pio run` 的靜態
  百分比就直接調整常數——這是本 ADR 留給未來維護者最重要的教訓。
- **96-entry catalog 的降版相容性**：`validate_catalog`／`parse_catalog`
  對 `count` 的檢查是 `count > kCatalogMaxEntries` 才視為無效，這個
  邊界本來就會隨常數改變——把 `kCatalogMaxEntries` 從 96 降回 48，
  等於任何「當初在 96 版韌體上被建立、且 `count` 介於 49–96 之間」的
  既有 catalog，降版後會被判定為 `CatalogError::invalid_count`，觸發
  現有的 catalog-invalid fail-closed 路徑（不是新的程式碼路徑，是既有
  「catalog 驗證失敗」處理邏輯的其中一種觸發條件，行為與其他 catalog
  損毀情境一致：裝置不會猜測性接受，而是把 imagefs 標為 degraded，不
  提供圖片服務，直到人工介入）。**本次撤銷刻意不為這個情境另外寫遷移
  程式碼**，理由是範圍評估後確認風險為零：96-entry 版韌體從未發布或
  部署到本次測試裝置以外的任何地方，且本次測試裝置在 96/64-entry 版本
  執行期間，圖片上傳本身大多數時候就直接失敗（96 筆版本連上傳都會因
  `pf_image_up` stack 配置失敗而中止，64 筆版本上傳雖然成功過，但當時
  裝置上的既有圖片數遠低於 48），因此**沒有任何實際存在的 catalog
  曾經被寫到超過 48 筆**，這個相容性缺口目前是純理論風險、沒有真實資料
  需要遷移。若未來 96（或任何 &gt;48）的目錄上限真的要對外發布，必須
  在那個時間點補上明確的降版行為定義與測試（例如：偵測到
  `count > kCatalogMaxEntries` 時，是否要嘗試保留前 48 筆並標記
  `corrupt`，而不是整份拒絕），不能沿用本次「因為沒有真實資料所以不用
  處理」的豁免理由。

## Verification

- `pio test -e native`（`test_catalog`／`test_image_store`／
  `test_storage_worker` 皆已在 `kCatalogMaxEntries=48` 下重新確認通過）。
- `pio run -e paperframe-s3`：internal RAM 回到 48.6%（159,376 bytes），
  與拉高上限前的原始基準完全一致。
- **實機驗證（已完成，非待辦）**：燒錄到實體 ESP32-S3-N16R8（透過
  `scripts\flash-app-and-webfs.ps1`），經 WebUI 實際操作確認：
  - 上傳圖片成功，log 顯示 `image_upload_stack_free_bytes=2116~2120`。
  - 設為目前（activate）與刪除（mutation task）成功，log 顯示
    `image_mutation_stack_free_bytes=10020~10324`。
  - 同一次 session 先在 96 筆與 64 筆版本上重現過
    `deferred_task_stack_alloc_failed` 錯誤（96 筆是 mutation 直接失敗；
    64 筆時上傳勉強成功但 mutation 仍失敗），確認問題與
    `kCatalogMaxEntries` 直接相關，回到 48 後兩者都穩定成功，形成完整
    的正反對照。
