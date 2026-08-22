# 硬體規格與接線

本文件回答「要買什麼、怎麼接」。**它不是設計決策的權威來源**——腳位分配、
SPI 參數與 driver 契約的決策與理由固定在
[ADR-0003](../adr/0003-fix-phase2-display-integration.md)，感測器行為固定在
[ADR-0006](../adr/0006-sensor-drivers-and-presence.md)。兩者衝突時以 ADR 為準。

實機驗證狀態見 [硬體驗證紀錄](VALIDATION.md)，燒錄步驟見
[燒錄操作](FLASHING.md)。

## 元件清單

| 類別 | 元件 | 規格 | 狀態 |
| --- | --- | --- | --- |
| 主控 | ESP32-S3 開發板 | **ESP32-S3-N16R8**：16 MB Flash、8 MB octal PSRAM | 必要。octal PSRAM 是目標基線，framebuffer 與圖片處理依賴它 |
| 顯示器 | Waveshare 7.3 吋 e-Paper HAT (E) | 800×480、E6 六色（黑／白／黃／紅／藍／綠） | 必要。產品與接線說明見 [官方 manual](https://www.waveshare.com/wiki/7.3inch_e-Paper_HAT_%28E%29_Manual) |
| 溫溼度感測器 | DHT22／AM2302 | 3.3 V 供電，單線資料 | **可選**。driver 已實作，**實機未驗證** |
| 光感測器 | 光敏電阻（LDR）＋分壓電路 **×2** | 通道 1 輸出接 `ADC1_CH4`、通道 2 接 `ADC1_CH6` | **可選**（可只接一顆）。driver 已實作，**實機未驗證**；分壓電阻值依實際模組而定，本專案尚未固定 |
| 外殼 | 3D 列印外殼 | — | **尚未設計**。設計完成後於此補上列印檔與說明 |
| 供電 | — | — | **尚未記錄**。目前僅以開發板 USB 供電進行開發，長時間運行的供電方案未評估 |

> 標為「可選」的周邊未安裝時，韌體回傳 JSON `null` 而非 `0`，Dashboard 顯示
> 「未安裝／未啟用」——這是 [ADR-0006](../adr/0006-sensor-drivers-and-presence.md)
> 的明確決策，不要用 `0` 代替缺席。

## 接線

HAT 與 ESP32-S3 全部使用 **3.3 V logic**。

### 顯示器（SPI2）

| HAT 訊號 | ESP32-S3 | 用途 |
| --- | --- | --- |
| VCC | 3V3 | 電源 |
| GND | GND | 共地 |
| DIN | GPIO11 | SPI2 MOSI |
| CLK | GPIO12 | SPI2 SCK |
| CS | GPIO10 | chip select，低有效（由 SPI master hardware 控制） |
| DC | GPIO13 | data／command |
| RST | GPIO14 | panel reset，低有效 |
| BUSY | GPIO4 | panel status input，低電位為 busy |

SPI 參數：SPI2、mode 0、MSB-first、起始 clock 2 MHz。2 MHz 是 upstream 各平台
設定不一致時採取的保守值；**只有實機波形與刷新驗證通過後才能提高**（ADR-0003）。

### 感測器腳位

| 用途 | ESP32-S3 |
| --- | --- |
| 光敏電阻 ADC 通道 1 | GPIO5（`ADC1_CH4`） |
| DHT data | GPIO6 |
| 光敏電阻 ADC 通道 2 | GPIO7（`ADC1_CH6`）（ADR-0018） |
| I²C SDA | GPIO8（保留給未來 I²C 感測器） |
| I²C SCL | GPIO9（保留給未來 I²C 感測器） |

兩個光敏通道**都必須在 ADC1**：ESP32-S3 的 ADC2 在 Wi-Fi 啟用時由 Wi-Fi
driver 佔用，`adc_oneshot_read()` 會回 `ESP_ERR_TIMEOUT`。本裝置 Wi-Fi 常駐，
ADC2 等於不可用。ADC1 只涵蓋 GPIO1–GPIO10。若 GPIO7 因排針佈局不便，備援是
GPIO1（`ADC1_CH0`）或 GPIO2（`ADC1_CH1`）。改腳位要**同時**改兩處：
`sensor_task_esp_idf.cpp` 的 `kLightAdcChannels`（實際讀哪個 ADC 通道）與
`light_sensor.hpp` 的 `kLightChannelGpios`（API 與 WebUI 回報哪支腳）。
兩者是同一支腳的兩種說法，只改一邊會讓顯示與實際接線不符。

#### 光敏電阻分壓接法

兩顆各自一組分壓，**不共用固定電阻**——共用會讓兩路互相拉動，讀值失去獨立性：

```
   通道 1                        通道 2
     3V3                           3V3
      │                             │
    LDR-1                         LDR-2
      │                             │
      ├──────► GPIO5                ├──────► GPIO7
      │                             │
    R_fix-1                       R_fix-2
      │                             │
     GND                           GND
```

兩路完全分離：各自一顆 LDR、各自一顆 `R_fix`，只共用 3V3 與 GND。
**同一個分壓節點不可同時接到 GPIO5 與 GPIO7**——那樣兩個 ADC 讀到的是同一個
訊號，兩組 threshold 與 OR 判定就沒有意義了。

可選的抑制雜訊電容也是各自一顆：由各自的 ADC 節點對 GND 接 100nF，
貼近 ESP32 的腳位。

**上下順序不可反接**：韌體判定為 `filtered_raw < threshold → 暗`，因此必須
「亮→讀值高、暗→讀值低」。LDR 在上、固定電阻在下正好符合；反接會使閾值語意
顛倒。

`R_fix` 目前採用 **10 kΩ**，來源是對實際手上的 LDR 取
`R_fix ≈ √(R_亮 × R_暗)`（幾何平均，讓分壓在亮暗兩端都不貼近軌）。

**這是計算值，不是量測值**：尚未以實機 ADC 讀值確認。韌體把 raw ≤ 10 或
≥ 4085 判為 `saturated` 並且**不觸發離席**，所以你在意的整個光照範圍都必須
落在 raw 11–4084 之間；實際校正時若讀值頂到界線就要換值：

- 正常室內光就頂到 4085 → `R_fix` 太大，換 4.7 kΩ
- 全暗時掉到 10 以下 → `R_fix` 太小，換 22 kΩ 或 47 kΩ

校正流程：WebUI「環境與在場」頁同時顯示兩顆的即時 raw 與各自的 threshold，
在正常室內／手遮／全暗／直射四種條件下讀值後再決定兩組 threshold。兩顆位置
不同就會看到不同的光，**不要共用同一個閾值**。

判定規則是 OR：**任一顆已啟用且 `online` 的通道低於自己的 threshold 就算暗**
（ADR-0018）。未啟用、未偵測到、飽和或讀取錯誤的通道會被忽略，不影響另一顆。

### 為什麼是這些腳位

ESP32-S3-N16R8 上有一批 GPIO 不可自由使用，腳位是在扣除它們之後選定的：

| GPIO | 佔用原因 |
| --- | --- |
| GPIO33–37 | octal PSRAM |
| GPIO19–20 | native USB |
| GPIO0、GPIO3、GPIO45、GPIO46 | strapping pins |
| GPIO4 | 已分配給面板 BUSY，**不得再作為 light-sensor ADC** |
| GPIO10–14 | 面板 SPI2（CS／DIN／CLK／DC／RST） |
| ADC2 全部通道 | Wi-Fi 啟用時由 Wi-Fi driver 佔用，讀取回 `ESP_ERR_TIMEOUT` |

GPIO0、3、19、20、26–37、43–46 不分配給上述周邊。改接線前先讀 ADR-0003 的
限制，否則可能與 PSRAM 或開機模式衝突。

## 與程式碼的對應

腳位常數定義在
[`components/pf_display/include/pf_display/epd7in3e.hpp`](../../components/pf_display/include/pf_display/epd7in3e.hpp)
（`kPanelMosiGpio` 等），感測器腳位在
[`components/pf_sensors/sensor_task_esp_idf.cpp`](../../components/pf_sensors/sensor_task_esp_idf.cpp)
（`kDhtPin`、`kLightAdcChannels`）。兩個光敏通道的合併規則在
[`components/pf_sensors/include/pf_sensors/light_sensor.hpp`](../../components/pf_sensors/include/pf_sensors/light_sensor.hpp)
的 `combine_light_channels()`，有 host test 覆蓋。

面板腳位另有 host test 斷言（`test/test_epd7in3e_driver/`），改動腳位常數會
讓 `pio test -e native` 失敗——這是刻意的，避免腳位在無人察覺下被改掉。

## 已知限制

- **感測器與外殼皆未完成實機驗證**：driver 已實作並通過 host test，但
  DHT22 bit-bang 時序與光敏 ADC 校正**無法用 host test 覆蓋**，須在實機補驗
  （ADR-0006）。目前狀態以 [VALIDATION.md](VALIDATION.md) 為準。
- 供電方案與外殼尚未固定。光敏分壓的 `R_fix` = 10 kΩ 是**由 LDR 亮/暗電阻
  計算得出的值，尚未以實機 ADC 讀值驗證**；驗證前不要當成已確認的規格。
