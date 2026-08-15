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
| 光感測器 | 光敏電阻（LDR）＋分壓電路 | 輸出接 `ADC1_CH4` | **可選**。driver 已實作，**實機未驗證**；分壓電阻值依實際模組而定，本專案尚未固定 |
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
| 光敏電阻 ADC | GPIO5（`ADC1_CH4`） |
| DHT data | GPIO6 |
| I²C SDA | GPIO8（保留給未來 I²C 感測器） |
| I²C SCL | GPIO9（保留給未來 I²C 感測器） |

### 為什麼是這些腳位

ESP32-S3-N16R8 上有一批 GPIO 不可自由使用，腳位是在扣除它們之後選定的：

| GPIO | 佔用原因 |
| --- | --- |
| GPIO33–37 | octal PSRAM |
| GPIO19–20 | native USB |
| GPIO0、GPIO3、GPIO45、GPIO46 | strapping pins |
| GPIO4 | 已分配給面板 BUSY，**不得再作為 light-sensor ADC** |

GPIO0、3、19、20、26–37、43–46 不分配給上述周邊。改接線前先讀 ADR-0003 的
限制，否則可能與 PSRAM 或開機模式衝突。

## 與程式碼的對應

腳位常數定義在
[`components/pf_display/include/pf_display/epd7in3e.hpp`](../../components/pf_display/include/pf_display/epd7in3e.hpp)
（`kPanelMosiGpio` 等），感測器腳位在
[`components/pf_sensors/sensor_task_esp_idf.cpp`](../../components/pf_sensors/sensor_task_esp_idf.cpp)
（`kDhtPin`、`kLightAdcChannel`）。

面板腳位另有 host test 斷言（`test/test_epd7in3e_driver/`），改動腳位常數會
讓 `pio test -e native` 失敗——這是刻意的，避免腳位在無人察覺下被改掉。

## 已知限制

- **感測器與外殼皆未完成實機驗證**：driver 已實作並通過 host test，但
  DHT22 bit-bang 時序與光敏 ADC 校正**無法用 host test 覆蓋**，須在實機補驗
  （ADR-0006）。目前狀態以 [VALIDATION.md](VALIDATION.md) 為準。
- 分壓電路、供電方案與外殼尚未固定，本文件不提供尚未驗證的建議值。
