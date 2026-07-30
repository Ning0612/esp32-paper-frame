# 影像儲存交易

本文件記錄 Phase 5 目前已完成的 StorageWorker 核心邊界。實作位於
`components/pf_storage`；它只依賴 `StorageFileSystem`，因此可以在 host 以
fake filesystem 測試，也可以接到 ESP-IDF LittleFS backend。HTTP route、
carousel runtime 與開機 recovery 尚未在本段接入。

## 上傳流程

`store_image_transactionally` 接收一個有明確 `Content-Length` 的 PFR1 stream，
按 1024 bytes 分段執行下列步驟：

`current_catalog` 與 `updated_catalog` 必須是不同物件；API 會拒絕 alias，避免
讀回驗證或 rollback 期間把尚未提交的結果寫回目前狀態。

1. 驗證 reader、catalog、大小上限，並以檔案長度加上最大 PFC1 大小做保守的
   free-space 預檢。
2. 將 bytes 寫入 `/images/.upload.part`，同時由 PFR1 validator 驗證 magic、
   header、檔名、payload 長度與 CRC。任何 stream、格式或寫入錯誤都清除
   暫存檔，不會覆蓋既有圖片。
3. 以 catalog candidate 建立新 entry，序列化到
   `/images/.catalog.pfc1.part`。寫完後重新讀回整個 `.part`，再解析並比對
   candidate；讀回失敗或驗證不一致時，兩個暫存檔都會被清除。
4. 先把圖片暫存檔改名為 `<safe-name>.pfr1.part`，再把舊 catalog 改名為
   `/images/.catalog.pfc1.bak`，最後依序提交圖片正式檔與 canonical catalog。
5. 任一 rename 邊界失敗時，會移除已提交的新圖片並嘗試把 `.bak` 還原；
   無法完成還原時回報 `rollback_failed`。成功後才把 candidate 複製到
   `updated_catalog`。

固定的內部路徑由 component 擁有，檔名由 PFR1 validator 驗證後才進行 path
mapping；呼叫端不可自行拼接 imagefs 路徑。`StorageFileSystem` 的 backend 必須
由單一 StorageWorker 擁有，`remove_if_exists` 必須是冪等操作，`close_write`
必須在返回前 flush 所有 bytes。

## 尚未宣稱完成的邊界

- `recover_image_transactions` 已提供 boot-time recovery decision/executor：會讀回
  canonical、`.bak`、`.part` catalog，驗證新增 PFR1，依 rename 邊界完成新版本
  或還原舊版本，並清除可安全判定的孤兒暫存檔。它尚未由實際 StorageWorker
  startup 呼叫；接線與 boot smoke 會在 task integration 段完成。
- free-space 預檢使用 backend 回報的 raw bytes 加上 PFC1 上限，尚未把 LittleFS
  block rounding、metadata overhead 與 `ENOSPC` 映射成 HTTP 507；這會在 API
  與 filesystem error contract 段處理。
- component 尚未接入 StorageWorker task、HTTP upload route 或 carousel
  runtime。接入前需將 catalog/stream workspace 放到 task-owned persistent
  storage，並加入 stack watermark，避免在 HTTP handler stack 上建立大型
  `Catalog`。
- 清理暫存檔是 best effort；recovery 會把清理失敗視為可診斷的殘留狀態，不能
  以「檔案不存在」假設交易已完成。

Recovery 遇到無法證明是「base catalog 單一 append」、image CRC/metadata 不符、
或 rename rollback 失敗時會回報 `ambiguous`／`image_invalid`／
`rollback_failed`，保留相關檔案供下一次重試或診斷，不會猜測要刪除哪個完整版本。
Recovery 若已把 image `.part` 改成正式檔、但後續 catalog rename 失敗，會把它
改回 `.part`；若正式檔在本次 recovery 前就已存在則保留，讓下一次啟動可以完成
同一 candidate。目錄列舉或 `stat`/`closedir` 發生 I/O 錯誤時則 fail closed。

## 驗證

```powershell
.\.venv\Scripts\pio.exe test -e native -f test_image_store
.\.venv\Scripts\pio.exe test -e native
.\.venv\Scripts\pio.exe run -e paperframe-s3
node test\test_partition_layout.mjs
```

`test_image_store` 覆蓋完整上傳、空間預檢、格式／長度錯誤、重複檔名、catalog
讀回腐敗，以及圖片／catalog rename 邊界的 rollback。真正的 LittleFS 斷電與
實機 power-loss 測試仍列為 Phase 5 hardware acceptance，不能以 host fake
結果代替。
