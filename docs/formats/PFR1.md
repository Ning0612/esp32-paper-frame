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

`flags` 只記錄 browser pipeline 已套用的幾何操作，供診斷使用：

| Bit | Name | Meaning |
| ---: | --- | --- |
| `0x0001` | `mirror_x` | 水平鏡像已套用 |
| `0x0002` | `mirror_y` | 垂直鏡像已套用 |
| `0x0004` | `rotate_90_cw` | 順時針旋轉 90° 已套用 |
| `0x0008` | `compressed` | payload 是 raw DEFLATE stream，見下方「Payload 壓縮」 |

Browser 會先正規化 EXIF 方向，再依管理介面按鈕的點擊順序把水平鏡像、垂直
鏡像或順時針旋轉 90° 套用到目前的工作影像；每次點擊只套用一次，之後才進行
裁切或 fit、縮放、六色量化與 dithering。`mirror_x`/`mirror_y`/`rotate_90_cw`
只記錄該類幾何操作是否曾經套用，不編碼點擊次數或順序，也不要求韌體再次
套用幾何操作；它們只是已輸出 raster 的診斷 metadata。旋轉後的最終尺寸必須
仍符合 orientation profile。`compressed` 則會改變 payload 的實際內容與
`payload_length` 語意，見下方說明。

## Payload 壓縮

`compressed`（`0x0008`）未設時，`payload_length` 必須精確等於該 orientation
profile 的 packed payload 大小（見下方尺寸表），payload 是未壓縮的 packed
nibble bytes——這是 v1 原本、也是目前唯一被廣泛部署的行為，此 bit 未設時
解析與驗證邏輯完全不變。

`compressed` 設定時：

- Payload 是一段 **raw DEFLATE stream**（RFC 1951，不含 zlib 2-byte
  header／Adler32 trailer，也不含 gzip wrapper），解壓縮後必須精確等於該
  orientation profile 的未壓縮 packed payload。
- `payload_length` 改為「壓縮後實際 byte 數」，限制為
  `0 < payload_length ≤` 該 profile 的未壓縮大小；等於未壓縮大小在格式上
  合法（解壓縮器仍可正確處理），但瀏覽器 packer 應該在壓縮沒有縮小 payload
  時改用未壓縮格式（清除此 bit），因為壓縮版本此時除了多一次解壓縮成本外
  沒有任何好處。
- `payload_crc32` 語意不變：永遠是「檔案裡實際儲存的 payload bytes」的
  CRC32——`compressed` 設定時就是壓縮後 bytes 的 CRC，未設時是原始 nibble
  bytes 的 CRC。不會因為壓縮而新增第二個 CRC 欄位。
- 未壓縮情況下才驗證的「每個 payload byte 的兩個 nibble 都是合法 palette
  code」規則，在壓縮情況下改為「解壓縮後的 bytes」才驗證；壓縮後的 bytes
  本身不是 nibble-coded pixel，不適用這條規則。
- 只有瀏覽器端會產生壓縮 payload；韌體只做解壓縮，不做壓縮。壓縮／回退成
  未壓縮的判斷（比較壓縮前後大小，取較小者）完全由瀏覽器 packer 負責。

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

## Cross-language golden vector（compressed）

壓縮 payload 的 bytes 本身是每個 raw-DEFLATE encoder 的實作細節（不同
encoder 對同一份輸入可能產生不同、但同樣合法的 bit-exact 輸出），因此不像
未壓縮 golden vector 那樣要求瀏覽器 packer 重現逐 byte 相同的壓縮結果。這裡
固定的是一組**已知合法**的壓縮 bytes，用來驗證「decoder 端」（C++
`Pfr1Validator`、JS `zlib.inflateRawSync`）能正確還原成同一份未壓縮
payload，藉此交叉驗證雙方對 raw DEFLATE framing 的理解一致：

- profile：landscape `800×440`，`orientation=0`。
- filename：ASCII `golden-compressed.pfr1`（22 bytes）。
- flags：`compressed`（`0x0008`）。
- dithering：`nearest`（`0`）。
- 來源（解壓縮後）payload：176,000 bytes，全為 `0x11`（兩個 white native
  code）——與未壓縮 golden vector 使用同一份參考內容。
- 壓縮方式：raw DEFLATE（`zlib.deflateRawSync`，level 9，無 zlib/gzip
  wrapper）。
- 壓縮後 payload：188 bytes。
- 完整檔案長度：242 bytes。
- payload CRC32（壓縮後 bytes 的 CRC）：`0xBFA93827`。
- header CRC32：`0xBD56BA9A`。

C++ 端於 `test/test_pfr1_validator/test_main.cpp` 的
`test_cross_language_golden_compressed_vector_matches_documented_crcs`
驗證這組固定壓縮 bytes 能被 `Pfr1Validator` 正確解壓、CRC 相符、且還原出
176,000 bytes 全為 `0x11` 的 payload。
