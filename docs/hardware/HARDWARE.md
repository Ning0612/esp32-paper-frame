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
| 溫溼度感測器 | DHT22／AM2302 | 3.3 V 供電，單線資料 | **可選**。2026-08-23 已完成實機驗證（讀值與拔除降級） |
| 光感測器 | 光敏電阻（LDR）＋分壓電路 **×2** | 通道 1 輸出接 `ADC1_CH4`、通道 2 接 `ADC1_CH6` | **可選**，但只接一顆時必須在 WebUI 停用另一顆（見下方「只接一顆」）。兩顆同時接線已於 2026-08-23 完成實機驗證 |
| 外殼 | 3D 列印外殼 | — | 可選。STEP／STL 見 [`hardware/enclosure/`](../../hardware/enclosure/README.md)，並附在每個 GitHub Release 的 assets |
| 供電 | — | — | **尚未設計**。目前直接接線供電（開發板 USB），電池／UPS 與低功耗運行模式都未評估 |

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

#### 只接一顆光敏電阻時

**必須在 WebUI 的環境頁把沒有接線的那個通道取消勾選。** 這不是最佳化，
是必要步驟：

- 停用是 `SensorTask` 取樣迴圈的第一個分支，該通道的 ADC **完全不會被讀**，
  浮接與否都無所謂；合併判定也會忽略它（`combine_light_channels()` 在還有
  其他 online 通道時跳過非 online 的通道）。
- 啟用但沒接線時，唯一的防線是飽和判定（`raw ≤ 10` 或 `raw ≥ 4085` 判為
  `saturated` 且不參與判定）。**未接線的 ADC 腳不保證落在這個區間**——
  ESP32-S3 的 ADC 輸入沒有內部上下拉，分壓電路不在時該腳是高阻抗，實際
  讀到的是飄動的中間值，韌體無法與真實讀值區分。
- 失效模式是不對稱的。合併規則是 AND（兩顆都低於各自 threshold 才算暗），
  所以浮接值若飄在 threshold **之上**，那顆會永遠看起來「有光」，
  **裝置將永遠不會進入離席白屏，而且不會有任何錯誤訊息**。飄在 threshold
  之下反而無害，因為真正接著的那顆說了算。

每通道各自的 enable 就是為了這個情境而存在（[ADR-0018](../adr/0018-dual-photoresistor-channels.md)）。

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
訊號，兩組 threshold 與 AND 判定就沒有意義了。

可選的抑制雜訊電容也是各自一顆：由各自的 ADC 節點對 GND 接 100nF，
貼近 ESP32 的腳位。

**上下順序不可反接**：韌體判定為 `filtered_raw < threshold → 暗`，因此必須
「亮→讀值高、暗→讀值低」。LDR 在上、固定電阻在下正好符合；反接會使閾值語意
顛倒。

`R_fix` 目前採用 **100 kΩ**（兩通道皆同值；2026-08-27 由 47 kΩ 換上——
兩通道各自獨立接線，不是共用電阻，只是剛好又選到同一個值）。

2026-08-23 曾以 10 kΩ 驗證，足以分辨開燈與關燈，但全暗環境會貼到 ADC
底部（實測 raw ≤23，其中一通道直接 `saturated`），presence 因此在深暗
環境有塌成 `unknown`、離席白屏被取消的風險。2026-08-24 換成 47 kΩ，
當時實測全暗 777–781、兩端都有餘裕。**但這組 47 kΩ 的餘裕在同一批 LDR
上並不穩定**：2026-08-27 實機診斷「presence 偶爾卡在持續刷新、不轉白畫面」
時發現，同一台裝置在 47 kΩ 下全暗實測 channel 1（GPIO5）已經長時間貼在
`saturated`（raw ≤10）、channel 2（GPIO7）也只剩 15–27，離低端飽和門檻幾乎
沒有餘裕——跟 2026-08-24 量到的 777–781 差了兩個數量級。兩次量測的差異
最可能來自 LDR 批次差異或環境「全暗」程度不同（例如百葉窗、遮光程度），
不代表 47 kΩ 這個數字本身寫錯；重點是**同一個 R_fix 在不同 LDR／環境下
不保證有相同餘裕，必須用實機當下量到的數字校正，不能照搬文件裡的歷史值**。

換成 100 kΩ 並重新校正 threshold（兩通道皆設 2000）後，2026-08-27 實測
（詳見 [VALIDATION.md](VALIDATION.md) 對應段落）：

| 條件 | GPIO5 raw | GPIO7 raw |
| --- | --- | --- |
| 室內光 | 4040 | 3827 |
| 手遮 | 3540 | 2736 |
| 全暗（關燈後靜置約 2 分鐘） | 91 | 105 |
| 直射光 | `saturated`（≥4085） | `saturated`（≥4085） |

全暗讀值離低端飽和門檻（≤10）有 80+ counts 餘裕，比 47 kΩ 那次量到的
狀態好得多，但代價是室內光已經逼近高端飽和門檻（4085）只剩不到 50
counts，且直射光下**兩顆都會飽和**（47 kΩ 時只有一顆會）。這是同一個
分壓電路無法迴避的取捨：`R_fix` 越大，全暗餘裕越多、但亮處餘裕越少，
反之亦然。目前刻意偏向保留全暗端的餘裕，理由見下方「為什麼優先保暗端」。

韌體把 raw ≤ 10 或 ≥ 4085 判為 `saturated` 並且**不觸發離席**，所以你在意
的整個光照範圍都必須落在 raw 11–4084 之間；實際校正時若讀值頂到界線就要
換值：

- 正常室內光就頂到 4085 → `R_fix` 太大，換小一點
- 全暗時掉到 10 以下 → `R_fix` 太小，換大一點

另外，LDR 關燈後暗電阻是緩慢爬升的（「dark recovery」），關燈當下量到的
raw 不是穩態值，會在之後 1–2 分鐘內持續下降；校正「全暗」這一列時，
等讀值不再明顯變動再記錄，不要用剛關燈那幾秒的數字。

校正流程：WebUI「環境與在場」頁同時顯示兩顆的即時 raw 與各自的 threshold，
在正常室內／手遮／全暗／直射四種條件下讀值後再決定兩組 threshold。兩顆位置
不同就會看到不同的光，**不要共用同一個閾值**。

#### `R_fix` 選擇計算方法

分壓電路的輸出電壓（進而 ADC raw）由 `R_fix` 與 LDR 電阻 `R_LDR` 的比例
決定：

```
raw ≈ 4095 × R_fix / (R_fix + R_LDR)
```

LDR 在上、`R_fix` 在下（本文件「光敏電阻分壓接法」一節的接法），所以
`R_LDR` 越小（越亮）raw 越高，越大（越暗）raw 越低。選 `R_fix` 的目的
是讓你在意的整個光照範圍（部署環境實際會出現的「最暗」到「最亮」）都落在
raw 11–4084 之間，不要卡進兩端的飽和保護。

步驟：

1. **量測兩個邊界條件下的 `R_LDR`**：用三用電表直接量 LDR 兩端電阻，
   分別在部署環境最暗（例如關燈、拉上窗簾）與最亮（例如直射光）時量一次。
   沒有電表也可以先接一個已知的 `R_fix`（例如 10 kΩ）量出 raw，
   反推 `R_LDR = R_fix × (4095 − raw) / raw`。
2. **用幾何平均估算起始值**：`R_fix ≈ √(R_暗 × R_亮)`。幾何平均而非算術
   平均，是因為 raw 對 `R_LDR`是非線性（雙曲線）反應，幾何平均能讓兩端
   離各自的飽和門檻在對數尺度上大致對稱，而不是算術尺度上對稱。
3. **依風險不對稱調整，不要只套公式**：本裝置的「全暗才休眠、任一亮就
   喚醒」（ADR-0018）代表全暗端的飽和（`saturated` 且不觸發離席）比亮端
   飽和代價高得多——亮端飽和的後果只是「這顆讀值失真但另一顆通常還在
   `online`，presence 照樣能判斷」，全暗端飽和的後果卻是**兩顆同時卡住
   時 presence 塌回 `unknown`、原本累積的 180 秒離席倒數整個歸零**（見
   `components/pf_sensors/include/pf_sensors/presence.hpp` 的
   `update_presence()`）。因此起始值算出來後，實務上會刻意再把 `R_fix`
   往大調一格，用亮端的一些餘裕換全暗端更多餘裕。
4. **實機迭代校正，公式只是起點**：LDR 就算同型號也有批次差異，加上
   實際擺放位置（外殼遮光、感測器角度）會讓同一顆 LDR 表現得完全不同，
   公式算出來的值幾乎不會一次就對。流程是換上候選電阻 → 在 WebUI
   「環境與在場」頁的即時 raw 顯示下走過室內光／手遮／全暗／直射光
   四種條件 → 依上面「頂到 4085 就減小、掉到 10 以下就加大」調整 →
   重複，直到兩端都有餘裕（本文件的 47 kΩ → 100 kΩ 就是一次這樣的
   迭代，且 2026-08-27 那次還發現同一個 `R_fix` 在不同時間點的餘裕
   並不穩定，所以校正後仍要照下方「已知限制」持續觀察）。

##### 為什麼優先保暗端

兩端沒辦法同時留最大餘裕時，本專案選擇犧牲亮端、保住暗端，原因：

- **AND 語意本身就是為了讓誤判離席變難**（ADR-0018：「誤判離席（使用者
  在場卻白屏）比漏判離席（該睡沒睡）代價高得多」）。如果因為 `R_fix`
  太小又讓暗端常態貼著飽和門檻，等於用電路設計把 ADR 已經做的取捨又
  賠回去。
- 亮端飽和的實際影響很小：室內光與手遮這兩個「裝置多數時間會遇到」的
  條件都還在飽和門檻以內（見上表），只有「直射光」這種罕見邊界情境才會
  兩顆一起飽和，而且那種情境本來就已經確定是「亮」，presence 判斷退化
  成 `unknown` 不影響實際使用（不會誤白屏，只是暫時看不出是哪顆在
  主導判定）。

判定規則：**每一顆已啟用且 `online` 的通道都低於自己的 threshold 才算暗**——等價於「任一顆看到光就喚醒」
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

- **感測器已於 2026-08-23 完成實機驗證**：DHT22 bit-bang 時序與雙通道光敏
  ADC 校正無法用 host test 覆蓋，已改以實機證據閉環（ADR-0006、ADR-0018）。
  目前狀態仍以 [VALIDATION.md](VALIDATION.md) 為準。
- 光敏分壓的 `R_fix` 已於 2026-08-27 由 47 kΩ 換成 **100 kΩ**：47 kΩ 在
  2026-08-24 校正當時全暗實測 777–781、餘裕充足，但同一台裝置在
  2026-08-27 診斷「presence 偶爾卡在持續刷新、不轉白畫面」時，同樣 47 kΩ
  下全暗實測已經跌到 channel 1 常態 `saturated`、channel 2 只剩 15–27——
  兩顆同時偶爾飽和會讓 `update_presence()` 塌回 `unknown`，180 秒離席
  倒數整個歸零，這正是持續刷新不轉白畫面的成因。100 kΩ 實測全暗回升到
  91–105、兩端仍在飽和門檻內，見上方「光敏電阻分壓接法」一節的實測表與
  「`R_fix` 選擇計算方法」小節。**同一個 `R_fix` 數值在不同時間點測到的
  餘裕並不保證穩定**（原因未確認，推測與 LDR 批次或環境全暗程度有關），
  之後若又出現離席轉換異常，先用 WebUI 即時 raw 確認是否又貼上飽和門檻，
  而不是直接假設是判定邏輯的問題。
- **供電方案尚未設計**：目前直接接線。面板每次刷新後會 sleep，但主控
  未做 deep sleep，因此目前的功耗特性不適合電池運行。外殼已有可列印的
  CAD，但尚未定義列印材料與參數。
