# ADR-0003：固定 Phase 2 顯示器接線與 driver contract

- Status: accepted
- Date: 2026-07-29
- Supersedes: none

## Context

目標硬體是 Waveshare 7.3 吋 e-Paper HAT (E)，解析度 800×480，以 SPI
傳送 E6 全彩資料。開發板使用 ESP32-S3-N16R8；其 octal PSRAM 會占用
GPIO33–37，native USB 使用 GPIO19–20，GPIO0、GPIO3、GPIO45、GPIO46
則是 strapping pins。

使用者已依實體接線確認 e-Paper 訊號。感測器尚未安裝，但 G2 要求在
Phase 2 開始前保留 ADC、DHT 與 I²C GPIO，避免後續改動顯示器接線。

G3 的行為基線取自 Waveshare 官方 `waveshare/e-Paper` repository。官方
來源檔 header 把產品描述寫成 7.3inch e-Paper (F)，但檔名、symbol 與
Waveshare HAT (E) manual 使用的 demo target 都是 `epd7in3e`；本專案把
該描述視為 upstream 文件 typo，不把它解讀為另一個 driver。

## Decision

### G2 pin map

HAT 與 ESP32-S3 全部使用 3.3 V logic：

| HAT 訊號 | ESP32-S3 | 方向／用途 |
| --- | --- | --- |
| VCC | 3V3 | 電源 |
| GND | GND | 共地 |
| DIN | GPIO11 | SPI2 MOSI |
| CLK | GPIO12 | SPI2 SCK |
| CS | GPIO10 | SPI chip select，低有效 |
| DC | GPIO13 | data／command |
| RST | GPIO14 | panel reset，低有效 |
| BUSY | GPIO4 | panel status input，低電位為 busy |

感測器保留腳位如下；在 Phase 7 前不初始化，也不因浮動輸入改變 runtime
狀態：

| 用途 | ESP32-S3 |
| --- | --- |
| 光敏電阻 ADC | GPIO5 (`ADC1_CH4`) |
| DHT data | GPIO6 |
| I²C SDA | GPIO8 |
| I²C SCL | GPIO9 |

GPIO0、GPIO3、GPIO19、GPIO20、GPIO26–37、GPIO43–46 不分配給上述周邊。
GPIO4 已分配給 BUSY，不得再作為 light-sensor ADC。

### G3 driver contract

- Upstream：`https://github.com/waveshare/e-Paper`
- 固定 repository commit：`06e834491bf62023a1b86a481b4530978883d2c4`
- 行為來源：
  - `RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_7in3e.c`
    (`SHA-256 010E08A7053219076E707EBE2210BEF7C38ABB11EB8F975418F451EA8D9D895C`)
  - `RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_7in3e.h`
    (`SHA-256 D454685677EAC06CEEA24F9E64BE05782491E41491050C7A7B78186AEF73BD66`)
  - `RaspberryPi_JetsonNano/python/lib/waveshare_epd/epd7in3e.py`
    (`SHA-256 7E12ECECFF0A75C84EEB32B2A324B3CECA49D08E1A108175A467C8417BD64572`)
- Upstream driver 使用 MIT permission notice。ESP-IDF port 若複製或大幅改作
  command sequence，必須保留 Waveshare attribution 與 permission notice。
- Palette v1 只接受六個 native nibble：

| 色彩 | nibble |
| --- | --- |
| black | `0x0` |
| white | `0x1` |
| yellow | `0x2` |
| red | `0x3` |
| blue | `0x5` |
| green | `0x6` |

`0x4` 不屬於 palette v1，不得沿用 upstream quantizer 把它默默映射為黑色。
每個 byte 的高 nibble 是偶數 x pixel，低 nibble 是下一個奇數 x pixel。

BUSY 低電位表示 busy，高電位表示 idle。每個單獨 BUSY wait 的 hard
timeout 為 60 秒；整筆 display command 會包含多次 wait，end-to-end
上限不宣稱為 60 秒。逾時後不在同一 command 內重試或繼續送 panel
command；driver deassert CS、釋放 SPI transaction，回報 `busy_timeout`，
並把 panel sleep 狀態標為 unknown。下一個 display command 必須從
hardware reset 與完整 initialization 重新開始。

成功刷新依 upstream sequence 執行 power-on、display refresh，以及
`POWER_OFF (0x02), 0x00` 並等待 BUSY。緊接的 sleep sequence 再送一次
`POWER_OFF (0x02), 0x00`、等待 BUSY，最後送 deep-sleep
`0x07, 0xA5`。第二次 power-off 暫不省略，除非後續另有實機證據與 ADR
取代本決策；單獨的 `POWER_OFF` 不視為已進入 deep sleep。

## Consequences

- Phase 2 可在感測器未安裝時開始，且不會占用其保留 GPIO。
- Packed framebuffer primitive 不配置 PSRAM；它只操作 caller 提供的
  buffer，實際 PSRAM ownership 留給後續 renderer／DisplayTask。
- slot `0x4` 或其他保留 nibble 必須在 encoder／upload 驗證時被拒絕。
- 單一 stuck BUSY wait 最長會阻塞 DisplayTask 60 秒；整筆 command 可能
  包含多次成功 wait，不在本 ADR 保證 end-to-end 60 秒上限。HTTP handler
  不得因此阻塞；command/result queue 與健康狀態仍由後續 DisplayTask
  commit 驗證。
- 沒有 PWR control pin；BUSY timeout 時無法宣稱 panel 已 sleep。

## Verification

- Host golden tests 驗證 palette、nibble 順序、buffer 尺寸、邊界 pixel 與
  非法輸入不改寫 buffer。
- Driver fake test 必須驗證 active-low BUSY、60 秒 timeout、成功 deep sleep
  與 timeout 後不再送 command。
- 實機 pattern test 依六色區塊確認 mapping，再量測一般刷新時間。
- forced-BUSY 預設只在 fake driver 執行。實機測試僅能使用隔離治具，或
  先斷開 HAT BUSY output 並驗證不會發生 output contention；HAT 仍連接時
  不得把 ESP32 GPIO4 改為 output 強拉。未完成前保留為 Phase 2 hardware
  risk。
