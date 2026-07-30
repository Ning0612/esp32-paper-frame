# PFR1 圖片檔案格式

PFR1（PaperFrame Raster 1）是 PaperFrame browser pipeline 與韌體
`imagefs` 之間交換已處理圖片的固定格式。檔案必須已完成鏡像、旋轉、裁切／
fit、縮放、六色量化與 dithering；韌體不接受任意 RGB framebuffer，也不在
StorageWorker 內重新解讀原圖。

## 版本與 byte order

- 所有多位元整數為 little-endian unsigned integer。
- magic 固定為 ASCII `PFR1`（bytes `50 46 52 31`）。
- v1 的 header 固定 32 bytes；`header_size` 必須為 `32`。
- v1 的 maximum filename length 為 96 bytes，maximum payload length 為
  182,400 bytes；完整檔案不得超過 182,528 bytes。
- 未來版本可增加 header，但不得讓 v1 parser 靜默接受未知版本或縮短的
  header。版本不相容時必須回報錯誤並保留既有圖片。

## Layout

Header 的 byte offset 如下：

| Offset | Size | Field | v1 contract |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | `PFR1` |
| 4 | 1 | `version` | `1` |
| 5 | 1 | `header_size` | `32` |
| 6 | 2 | `flags` | 只有下列定義 bit 可用，其餘必須為 0 |
| 8 | 2 | `width` | 橫向 `800`；直向 `480` |
| 10 | 2 | `height` | 橫向 `440`；直向 `760` |
| 12 | 1 | `orientation` | `0=landscape`、`1=portrait` |
| 13 | 1 | `palette` | `1=E6 native palette v1` |
| 14 | 1 | `dithering` | `0=nearest`、`1=Floyd-Steinberg`、`2=Atkinson`、`3=Bayer 4×4` |
| 15 | 1 | `reserved` | 必須為 0 |
| 16 | 4 | `payload_length` | 必須等於該尺寸的 packed byte 數 |
| 20 | 2 | `filename_length` | 1–96 bytes；不是字元數 |
| 22 | 2 | `reserved2` | 必須為 0 |
| 24 | 4 | `payload_crc32` | CRC32 of payload only |
| 28 | 4 | `header_crc32` | CRC32 of bytes `0..23` |

Header 後緊接 `filename_length` bytes 的 UTF-8 basename，再緊接
`payload_length` bytes 的 packed payload。檔案總長度必須精確等於：

```text
header_size + filename_length + payload_length
```

## Flags 與轉換 metadata

`flags` 只記錄 browser pipeline 已套用的幾何操作，供診斷與重現使用：

| Bit | Name | Meaning |
| ---: | --- | --- |
| `0x0001` | `mirror_x` | 水平鏡像已套用 |
| `0x0002` | `mirror_y` | 垂直鏡像已套用 |
| `0x0004` | `rotate_90_cw` | 順時針旋轉 90° 已套用 |

Browser 的固定處理順序為：水平鏡像、垂直鏡像、順時針旋轉、裁切或 fit、
縮放、六色量化與 dithering。flags 不要求韌體再次套用這些操作；它們只是
已輸出的 raster metadata。旋轉後的最終尺寸必須仍符合 orientation profile。

## 尺寸、palette 與 payload

v1 只有兩個輸出 profile：

| `orientation` | Width × height | Packed payload |
| ---: | ---: | ---: |
| `0` landscape | `800 × 440` | `176,000` bytes |
| `1` portrait | `480 × 760` | `182,400` bytes |

每一 byte 儲存兩個 pixel：偶數 x 的 pixel 在 high nibble，奇數 x 的 pixel
在 low nibble；row 不跨 row padding。E6 native palette code 與
`pf_display::Color` 相同：

| Color | Native nibble |
| --- | ---: |
| black | `0x0` |
| white | `0x1` |
| yellow | `0x2` |
| red | `0x3` |
| blue | `0x5` |
| green | `0x6` |

`0x4` 與 `0x7`–`0xF` 都是 invalid nibble。v1 寬度皆為偶數，因此不存在
未使用的 trailing nibble；parser 仍必須逐 nibble 驗證 palette。

## Filename contract

檔名是 UTF-8 basename，不得為空、`.` 或 `..`，不得以 `.` 開頭或結尾，
不得包含 `/`、`\\`、NUL、ASCII control（`<0x20` 或 `0x7F`），也不得以
space 結尾。檔名長度以 UTF-8 bytes 計算，最多 96 bytes。這項限制同時是
PFR1 parser 的安全檢查與未來 imagefs path mapping 的輸入契約。

## CRC32

兩個 CRC 都使用 CRC-32/ISO-HDLC：

- polynomial `0xEDB88320`（reflected）
- initial `0xFFFFFFFF`
- final XOR `0xFFFFFFFF`
- no byte or word swapping beyond the little-endian field encoding

`header_crc32` 驗證 header bytes `0..23`；`payload_crc32` 驗證 filename 後的
payload bytes。Streaming validator 必須在收到所有 bytes 後才接受檔案；任何
magic、version、保留欄位、尺寸、長度、filename、palette nibble 或 CRC 錯誤
都回傳明確 invalid reason，不得寫入正式 image path。

## MIME 與 upload boundary

- MIME：`application/vnd.paperframe.pfr1`。
- Browser 只送 packed PFR1，並在送出前限制完整檔案長度與 payload checksum。
- HTTP layer／StorageWorker 必須以 streaming parser 驗證，不能把未限制的
  request body 當作 RGB 或任意 binary 直接保存。
- 本格式不包含 secret、Wi-Fi 資訊、session token 或使用者帳號資料。

## Cross-language golden vector

下列向量同時由 C++ validator 與 browser packer 驗證，固定用來防止 byte
order、nibble 順序或 CRC 漂移：

- profile：landscape `800×440`，`orientation=0`。
- filename：ASCII `golden.pfr1`（11 bytes）。
- flags：`mirror_x`（`0x0001`）。
- dithering：`nearest`（`0`）。
- payload：176,000 bytes，全為 `0x11`（兩個 white native code）。
- 完整檔案長度：176,043 bytes。
- payload CRC32：`0xAF00B5BD`（header little-endian bytes `BD B5 00 AF`）。
- header CRC32：`0xC96F698B`（header little-endian bytes `8B 69 6F C9`）。

Browser packer 的輸入必須是已量化的固定 profile raster；它只輸出 packed
PFR1 `Uint8Array`，不接受任意尺寸或未量化 RGB。host 驗證命令：

```powershell
node test\web\test_pfr1_packer.mjs
node --check data\web\image_pfr1.js
```
