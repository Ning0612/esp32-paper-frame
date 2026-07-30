# PFC1 影像目錄格式

PFC1（PaperFrame Catalog 1）是 `imagefs` 上持久化影像目錄的固定格式。
它只保存已通過 [PFR1](PFR1.md) 驗證的檔案 metadata；影像 payload 仍存放在
獨立的 image path，不會嵌入目錄。v1 parser 對未知版本、保留欄位、CRC 或
尾端資料一律拒絕，避免以不完整目錄覆蓋既有資料。

## 版本與限制

- 所有多位元整數都是 little-endian unsigned integer。
- magic 固定為 ASCII `PFC1`（bytes `50 46 43 31`）。
- v1 header 固定 32 bytes，entry 固定 128 bytes。
- 目錄最多 48 筆，序列化後最多 `32 + 48 × 128 = 6,176` bytes。
- 空目錄仍有 32-byte header；`next_id` 從 `1` 開始，`generation` 在每次
  成功的目錄 mutation 後遞增。`UINT32_MAX` 是持久化的 exhausted marker，
  表示不再自動配置 id；實際 entry id 永遠小於此值。
- `created_at_epoch_s = 0` 表示尚未取得可信的 wall-clock（例如 SNTP 尚未
  同步），不是偽造的 Unix epoch。

## Header

| Offset | Size | Field | v1 contract |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | `PFC1` |
| 4 | 1 | `version` | `1` |
| 5 | 1 | `header_size` | `32` |
| 6 | 2 | `reserved` | 必須為 0 |
| 8 | 4 | `generation` | 目錄 mutation generation |
| 12 | 4 | `next_id` | 下一個自動配置的非零 entry id；`UINT32_MAX` 表示 exhausted |
| 16 | 2 | `count` | `0..48` |
| 18 | 2 | `entry_size` | `128` |
| 20 | 4 | `payload_length` | 必須等於 `count × 128` |
| 24 | 4 | `payload_crc32` | CRC32 of all entry records |
| 28 | 4 | `header_crc32` | CRC32 of header bytes `0..23` |

Header CRC 的輸入包含 `payload_length`，但不包含兩個 CRC 欄位。payload CRC
涵蓋 offset 32 起的所有固定長度 entry record；因此 parser 可以先驗證完整
目錄，再交給 catalog mutation 或 carousel 使用。

## Entry record

| Offset | Size | Field | v1 contract |
| ---: | ---: | --- | --- |
| 0 | 4 | `id` | 非零且在 `next_id` 之前已配置 |
| 4 | 8 | `created_at_epoch_s` | Unix seconds；`0` 表示 unknown |
| 12 | 4 | `file_bytes` | `32 + filename_length + payload_bytes` |
| 16 | 4 | `payload_bytes` | 必須符合 PFR1 尺寸 profile |
| 20 | 2 | `width` | PFR1 landscape `800` 或 portrait `480` |
| 22 | 2 | `height` | PFR1 landscape `440` 或 portrait `760` |
| 24 | 1 | `orientation` | `0=landscape`、`1=portrait` |
| 25 | 1 | `flags` | 只允許下列定義 bits |
| 26 | 2 | `order` | `0..count-1`，不可重複 |
| 28 | 2 | `filename_length` | UTF-8 bytes，`1..96` |
| 30 | 2 | `reserved` | 必須為 0 |
| 32 | 96 | `filename` + padding | basename；未使用 bytes 必須為 0 |

`filename` 使用 PFR1 的 basename contract，且同一目錄內不得重複。entry 的
`payload_bytes` 與 `file_bytes` 必須能由 PFR1 header／尺寸重新推導，避免
目錄 metadata 與實際檔案大小分離。

## Flags 與目錄 invariants

| Bit | Name | Meaning |
| ---: | --- | --- |
| `0x01` | `enabled` | 使用者啟用偏好；carousel 仍須另外排除 corrupt |
| `0x02` | `current` | 目前顯示影像；整個目錄最多一筆 |
| `0x04` | `corrupt` | 檔案驗證失敗，保留 metadata 供診斷 |

下列 invariants 由 host 與韌體共用的 `pf_storage::validate_catalog` 強制：

- `current` 必須同時 `enabled` 且不可 `corrupt`；`enabled | corrupt` 可暫存，
  但永遠不得送入 carousel eligible set。
- `order` 必須是沒有缺口的 `0..count-1`。
- id、filename 不可重複；entry 數不得超過 48。
- 任何驗證失敗都不得序列化成可提交的 catalog。

## 交易邊界

PFC1 本身是單一固定長度 blob，寫入 imagefs 時必須使用專案的 `.part`／
`.bak` 交易流程：先寫完整 bytes、重新讀回並驗證 magic／header／entry／兩個
CRC，再以 rename/replace 提交正式 catalog。開機若發現 `.part` 或 `.bak`，
後續 StorageWorker recovery 會依驗證結果選擇最新完整版本；本文件不把未完成
的 recovery 行為宣稱為已實作。`remove_catalog_entry` 只負責原子地移除 metadata
與重編順序；若移除 current，transaction coordinator 必須在同一 catalog/runtime
更新中先選擇下一筆 `enabled && !corrupt` entry，無候選時才清除 current。

## 驗證命令

```powershell
.\.venv\Scripts\pio.exe test -e native -f test_catalog
.\.venv\Scripts\pio.exe run -e paperframe-s3
```
