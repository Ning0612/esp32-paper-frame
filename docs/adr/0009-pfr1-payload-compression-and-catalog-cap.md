# ADR-0009：PFR1 Payload 壓縮與 PFC1 目錄容量上限

- Status: accepted
- Date: 2026-08-02
- Supersedes: none

## Context

`docs/formats/PFR1.md` 目前完全沒有壓縮：每張圖固定 176,000（landscape
800×440）或 182,400（portrait 480×760）bytes 的 4bpp nibble payload，6 色
palette 只用到 16 種 nibble 值中的 6 種。分析發現兩個獨立但相關的問題：

1. **今天的實際容量瓶頸是 PFC1 的 48 筆目錄上限
   （`kCatalogMaxEntries`），不是 `imagefs` 分割區的 flash 位元組數**。
   `imagefs` 是 `0x9d0000`（10,289,152 bytes，9.81 MiB），但 48 張
   landscape 圖只吃掉其中 82%（8.06 MiB），portrait 85%（8.36 MiB）——
   代表即使完全不動 payload 格式，光拉高目錄上限就能立即多存幾張圖。
2. **使用者的實際圖片庫主要使用 `floyd-steinberg`/`atkinson`
   dithering**，這剛好是壓縮效果最差的模式：error diffusion 刻意讓相鄰
   pixel 去相關化以模擬更多顏色，這對通用壓縮演算法是對抗性的。用本專案
   實際的 `image_quantizer.js` 對代表性合成圖片做的實測（Node
   `zlib.deflateRawSync`，對照四種 dithering 模式）顯示：
   - `floyd-steinberg`/`atkinson`：deflate 只省 ~40–47%，RLE 甚至讓資料
     變大 190–254%。
   - `nearest`/`bayer-4x4`：deflate 可省 75–99%（沒有 error diffusion
     去相關化）。
   規劃容量增益必須照 40–47% 這個保守數字算，不能套用最佳情境。

兩個問題需要一起解決才有意義：只拉高目錄上限，48→96 張圖片在最差情況
（全部未壓縮）下會直接把 `imagefs` 撐爆（96 張 landscape 未壓縮
≈ 16.1 MiB > 9.81 MiB）；只加壓縮而不拉高目錄上限，使用者依然卡在 48
張的硬上限，壓縮省下的空間根本用不到。

依專案 `CLAUDE.md` 規則，「持久格式」（PFR1 payload 語意、PFC1 catalog
entry 驗證邊界）決策需要先開 ADR。以下範圍決策已與使用者確認：

1. 壓縮只能在瀏覽器端做（利用終端裝置的 CPU 資源，且順帶省上傳頻寬），
   ESP32 韌體只做解壓縮，不做壓縮。
2. `kCatalogMaxEntries` 目標值 96（doubling），可依實際使用回饋再調整。

## Decision

### PFR1 新增 `kCompressed` flag bit，payload 語意從嚴格相等改成有界範圍

- `Pfr1Flags` 新增 `kCompressed = 0x0008U`（目前只用到
  `mirror_x`/`mirror_y`/`rotate_90_cw` 三個 bit，16 bit 欄位還有 13 個
  spare bit，加這個 bit 不需要動 header layout 或 bump `version`）。
- `payload_length` 欄位語意：沒有 `kCompressed` 時維持今天的「必須精確
  等於該 orientation profile 的 uncompressed 大小」（176,000/182,400
  bytes），行為完全不變；有 `kCompressed` 時放寬成
  `0 < payload_length ≤ expected_payload_length(width, height)`——
  payload 是實際壓縮後的 byte 數，且照定義不可能大於等於未壓縮大小
  （壓縮沒幫助時瀏覽器端就該回退成不壓縮，見下方「瀏覽器端」小節）。
- `payload_crc32` 語意不變：永遠是「檔案裡實際儲存的 payload bytes」的
  CRC——有壓縮時是壓縮後 bytes 的 CRC，沒壓縮時和今天一樣是原始 nibble
  bytes 的 CRC。不會因為加了壓縮而需要新增第二個 CRC 欄位。
- 因為 `payload_length` 不再保證等於解壓縮後大小，解壓縮後的目標大小
  永遠由 `expected_payload_length(width, height)` 這個既有函式決定
  （只有兩種合法值，由 header 裡已有的 width/height/orientation 決定），
  不需要在 header 裡新增一個「uncompressed_length」欄位。

### 壓縮框架：raw DEFLATE，不含 zlib/gzip wrapper

- 瀏覽器端用 Web 原生 `CompressionStream('deflate-raw')`（無 CDN 依賴，
  符合本專案 WebUI 不依賴外部函式庫的慣例）。
- 韌體端解壓縮用 ESP32-S3 mask ROM 內建的 miniz `tinfl_decompress*`
  （`esp_rom` 元件提供 `miniz.h`，ROM 已經有連結好的
  `tinfl_decompress`/`tinfl_decompress_mem_to_mem` 等符號，零額外函式庫
  或 Kconfig 成本）。呼叫時不加 `TINFL_FLAG_PARSE_ZLIB_HEADER`，對應
  `deflate-raw` 沒有 zlib 2-byte header／Adler32 trailer 的 framing。
- **考慮過但捨棄**：zlib/gzip wrapper——ROM 只乾淨支援 raw inflate，
  wrapper 除了多 6+ bytes 開銷沒有額外價值；LZ4——ROM 沒有內建 LZ4
  decoder，得額外引入並維護一份函式庫；把壓縮搬到 Web Worker——payload
  最大 182 KB，`CompressionStream` 在主執行緒跑是個位數 ms 等級，不值得
  為此重構 `image_quantize_worker.js` 的訊息協定。
- **韌體端 miniz 的 `mz_crc32` 是 `esp_rom_crc32_le` 的 alias，跟本專案
  既有的 bit-reflected CRC-32/ISO-HDLC 實作是不同的 table/慣例**——只借
  miniz 的 inflate 函式，PFR1 的 CRC32 繼續用專案既有的自訂實作，不混用。

### PFC1 目錄驗證：從嚴格相等改成有界範圍，wire format 不變

- `CatalogEntry.payload_bytes`/`file_bytes` 在 wire format 上本來就是
  「該檔案實際儲存的 payload 大小」（per-entry 存的值，不是格式假設的
  固定常數），只有 `validate_catalog_entry` 的驗證邏輯從
  `payload_bytes != expected_payload_length(...)` 改成
  `payload_bytes == 0 || payload_bytes > expected_payload_length(...)`
  才算 invalid。
- `count` header 欄位本身是 2 bytes（撐得住遠大於 96），因此**不需要
  bump PFC1 版本或動 wire layout**，只是把 `kCatalogMaxEntries` 這個
  policy 常數從 48 調到 96。

### `kCatalogMaxEntries`：48 → 96

- `kCatalogMaxBytes` 從 6,176 bytes 變成 12,320 bytes；BSS 成長集中在
  `RecoveryWorkspace`（含 4 份 `Catalog` 副本），約 32 KB → 64 KB，在
  8 MB PSRAM 的 ESP32-S3-N16R8 上可忽略。
- 真正防止 `imagefs` 塞爆的是 `store_image_transactionally` 既有的
  runtime free-space precheck（用實際 `content_length` 而非固定假設
  的 per-image 大小），拉高筆數上限不會繞過這個安全網——即使某使用者的
  圖片庫完全不可壓縮，上傳到接近容量時 precheck 仍會正確擋下超量請求。
- 96 是 doubling，容易推理；配合 40–47% 的保守壓縮估計，原本 48 張的
  使用者換算後可望存到約 70–95 張（依實際圖片內容浮動），仍有餘裕。
  若之後實測發現太保守或太激進，可以再開一個 superseding ADR 調整，
  因為 `count` 欄位本身沒有更往上的硬限制。
- 有一個獨立於 `kCatalogMaxEntries` 的裸常數 `512`（`health_server.cpp`
  的 reorder body guard/buffer）需要跟著改成從常數推導的公式，否則滿到
  96 筆時 reorder API 會失敗；這是實作細節，記錄於實作計畫，不在此 ADR
  重複列出。

### 範圍外決策

- 不替 `CatalogEntry.flags` 加「compressed」旗標（給 WebUI 顯示用）——
  `payload_bytes < expected_payload_length(...)` 已經能推得出來，不值得
  為純 UI 呈現目的再動 PFC1 wire contract；未來若要做，屬於獨立的
  follow-up ADR 或直接視為非格式異動的小改動。

## Consequences

- `pf_image`、`pf_storage`、`pf_carousel` 三個元件的 `CMakeLists.txt`
  需要加 `esp_rom` 依賴（`miniz.h` 的來源）。`esp_rom` 是 ESP-IDF 每個
  app 本來就隱含依賴的核心元件，不算新增外部依賴。
- Host（`pio test -e native`）測試環境是純 `-I` header-only build，連不到
  ROM-linked 的 `tinfl` 符號，需要為 host test 另外 vendor 一份 portable
  的 tinfl 實作（miniz 的 `tinfl.c/h` 是 public-domain 風格單檔程式碼），
  ESP-IDF 端繼續用 ROM 版本；這是實作期需要驗證可行性的技術風險，若
  vendoring 不順利，退路是讓 host test 只驗證 validator 的控制流程，
  靠 `pio run` 的實際連結把關 ROM 版正確性。
- 既有的 `Pfr1FrameDecoder::finish_and_compose`
  （`components/pf_carousel/include/pf_carousel/image_frame.hpp`）目前
  把 `header.payload_length` 當作已解碼 payload 長度傳給
  `compose_landscape`/`compose_portrait`；一旦 `payload_length` 可能是
  壓縮後長度，這裡若不改就會用錯誤長度組框——這是本 ADR 觸發下必須修正
  的既有 bug，非本 ADR 引入的新問題。
- 上傳／recovery 路徑（`image_store.cpp`、`recovery.cpp`）為了維持「壞
  資料絕不寫入正式 image path」的既有保證，需要各自新增兩份
  static（BSS，非 stack）scratch buffer 做完整 inflate+nibble 驗證，不能
  只驗 CRC 就信任壓縮內容：一份暫存 feed() 時逐步收到的壓縮 bytes，一份接
  收 finish() 時一次性解壓縮的輸出——兩者都必須至少
  `kPfr1MaxPayloadBytes`（182,400 bytes），因為 `Pfr1Validator` 是把整段
  壓縮 payload 收完後才呼叫一次 `tinfl_decompress_mem_to_mem`／zlib 對應
  API 一次性解壓縮，不是用 32 KB 滑動窗口做逐段解壓縮。
- 新增的 scratch buffer（ingest、recovery、display 各兩份，每份
  182,400 bytes）+ 拉高目錄上限後的 BSS 成長，都必須在 `pio run` 後檢查
  沒有把 app image 推過 `ota_0`/`ota_1` 的 `0x280000` 分割區預算，且需要
  在實機驗證 stack high-water-mark（呼應 `docs/hardware/VALIDATION.md`
  記錄過的 `CONFIG_ESP_MAIN_TASK_STACK_SIZE` 與 httpd worker stack
  overflow 教訓——所有新 buffer 一律 static/heap/PSRAM 配置，不上
  stack）。
- 未來若要改變壓縮演算法、framing 或再次調整 `kCatalogMaxEntries`，
  需要新的 superseding ADR。

## Verification

- `pio run` 與 `pio test -e native` 全綠是本 ADR 所涵蓋所有變更的最低
  驗證門檻：`test_pfr1_validator`（壓縮 payload 驗證、golden vector）、
  `test_catalog`（有界範圍驗證）、`test_image_store`/`test_storage_worker`
  （壓縮上傳與 recovery 場景）、`test_image_frame`（壓縮 payload 解碼與
  組框）。
- `test/web/test_pfr1_packer.mjs`：瀏覽器端壓縮/fallback 邏輯與
  cross-language golden vector（與 C++ 端用同一組 fixture 互相驗證）。
- 無法 host test 的部分（真實 `CompressionStream`/`tinfl` 在瀏覽器與
  真機上的實際互通性、真機 stack high-water-mark、真機 carousel 顯示
  是否 pixel-exact、接近 96 筆容量時的 reorder/free-space 行為、斷電
  recovery 對壓縮圖片的處理）須在 `docs/hardware/VALIDATION.md` 記錄
  結果或明確列為未驗證風險。
