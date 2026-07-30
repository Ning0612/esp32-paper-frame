# ADR-0004：凍結 imagefs-preserving partition layout

- Status: accepted
- Date: 2026-07-30
- Supersedes: none

## Context

Phase 5 會第一次把使用者提供的 PFR1 檔案寫入 `imagefs`。在此之後，
任何 app-only upload、WebUI 更新或 OTA 都不能因重新切割 partition 而清除
使用者圖片。開發板是 16 MB Flash，現有雙 OTA layout 已被實機掛載與
app-only upload 驗證；目前尚未有可移動圖片的正式版本，因此此刻是凍結
G5 layout 的最後安全點。

## Decision

以 `partitions/paperframe-dev.csv` 作為目前開發／第一個可保存圖片 build 的
唯一 partition table，維持以下不可移動的資料區：

| Name | Offset | Size | End (exclusive) | 用途 |
| --- | ---: | ---: | ---: | --- |
| `nvs` | `0x009000` | `0x004000` | `0x00D000` | 設定與管理認證 |
| `otadata` | `0x00D000` | `0x002000` | `0x00F000` | OTA metadata |
| `phy_init` | `0x00F000` | `0x001000` | `0x010000` | RF calibration |
| `ota_0` | `0x010000` | `0x280000` | `0x290000` | app slot A |
| `ota_1` | `0x290000` | `0x280000` | `0x510000` | app slot B |
| `webfs` | `0x510000` | `0x100000` | `0x610000` | immutable-at-runtime WebUI asset image |
| `coredump` | `0x610000` | `0x020000` | `0x630000` | bounded crash data |
| `imagefs` | `0x630000` | `0x9D0000` | `0x1000000` | user PFR1 files and catalog |

The exact UTF-8 bytes of the CSV currently have SHA-256:

```text
427FD4144CAF3D8A2F0F0622317F42D6236EA2D7997BAFC42AF13C9CA8565870
```

`imagefs` uses the existing LittleFS partition label and mount point; no factory
image or routine app upload may format or rewrite it. The catalog and image files
must include their own schema/version markers so future data migrations can be
performed in place without moving the partition.

## Consequences

- Phase 5 storage code may rely on the fixed `imagefs` capacity reported by
  `esp_littlefs_info`, but must still preflight free space before writes.
- Changing any offset, size, or partition subtype requires a new superseding ADR,
  an explicit migration/backup plan, and a release gate; it is not a routine build
  change.
- `webfs` updates remain separate from `imagefs`; a WebUI-only flash command must
  target only `0x510000` and never include `imagefs.bin`.
- Development and release partition tables must not silently diverge. A build
  check should compare the selected CSV with this frozen contract before the first
  image-preserving release.

## Verification

- `Get-FileHash -Algorithm SHA256 partitions/paperframe-dev.csv` matches the hash
  above.
- `pio run -e paperframe-s3` uses the same CSV and produces a 16 MB layout.
- Existing hardware validation recorded successful mounting of `webfs` and
  `imagefs`, and app-only upload did not rewrite either partition.
- Before Phase 5 image upload is enabled, add host coverage for non-overlap and
  the imagefs-preserving flash command; any mismatch must fail closed.
