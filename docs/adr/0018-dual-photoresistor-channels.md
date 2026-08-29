# ADR-0018：光敏電阻擴充為兩個獨立通道，兩顆都暗才判定為暗

- Status: accepted
- Date: 2026-08-23
- Supersedes: [ADR-0006](0006-sensor-drivers-and-presence.md) 的
  「溫濕度與光敏電阻在本階段维持各自獨立判定，不合併」條款與
  `GET /api/v1/sensors` 的 `light` schema
- Amends: [ADR-0003](0003-fix-phase2-display-integration.md) 的 G2
  感測器保留腳位表（新增 GPIO7）
- Superseded in part by:
  [ADR-0020](0020-light-clip-is-diagnostic-not-presence-gate.md)——
  `saturated`（下方多處提及的 ADC 飽和不觸發離席）已改為診斷資訊並正常
  參與判定，`LightSensorStatus::saturated` 拆分為
  `low_clipped`／`high_clipped`。本文件其餘的雙通道接線、AND 合併規則、
  `SensorSettings` v2 與 API schema 的其他欄位**均維持有效**。

## Context

原設計只有一顆光敏電阻（GPIO5／`ADC1_CH4`）。單顆感測器的偵測結果完全取決於
它的擺放位置：被外殼陰影、支架或灰塵遮住時無法區分「環境真的變暗」與「這顆
被擋住了」，而裝置會據此白屏休眠。

使用者要求改為兩顆光敏電阻，各自設定閾值與顯示目前讀值。

**合併規則最初定為 OR**（任一顆低於自己的閾值就休眠），目的是提高「偵測到
遮蔽」的靈敏度。實機接線後當天即改為 **AND**（兩顆都暗才休眠），理由與實測
數據見本文件末的「修訂 2026-08-23：OR → AND」。以下 Decision 描述的是現行的
AND 規則。

ADR-0006 的 Consequences 明文要求「未來若要加入多感測器合併的 presence 判斷
邏輯，需要新的 superseding ADR」——本 ADR 即為該文件。

## Decision

### 第二通道腳位：GPIO7（`ADC1_CH6`）

**必須是 ADC1**：ESP32-S3 的 ADC2 在 Wi-Fi 啟用時由 Wi-Fi driver 佔用，
`adc_oneshot_read()` 會回 `ESP_ERR_TIMEOUT`。本裝置 Wi-Fi 常駐，ADC2 等於
不可用。ADC1 只涵蓋 GPIO1–GPIO10（CH0–CH9），扣掉已佔用與保留者：

| GPIO | ADC1 通道 | 狀態 |
| --- | --- | --- |
| 1 | CH0 | 空（備援） |
| 2 | CH1 | 空（備援） |
| 3 | CH2 | strapping，不可用 |
| 4 | CH3 | 面板 BUSY（ADR-0003 明文禁止改作光感） |
| 5 | CH4 | 光敏電阻通道 1（既有） |
| 6 | CH5 | DHT22 data |
| 7 | CH6 | **光敏電阻通道 2（本 ADR）** |
| 8 | CH7 | 保留 I²C SDA |
| 9 | CH8 | 保留 I²C SCL |
| 10 | CH9 | 面板 CS |

選 GPIO7 是為了讓感測器區塊維持在 GPIO5／6／7 連號，且不動任何既有保留腳位。
GPIO1／GPIO2 保留為備援：若板子排針佈局不便可改用它們，但必須**同時**更換
`sensor_task_esp_idf.cpp` 的 `kLightAdcChannels`（實際讀取的 ADC 通道）與
`light_sensor.hpp` 的 `kLightChannelGpios`（API 與 WebUI 回報的腳位）。兩者是
同一支腳的兩種說法，只改一邊會讓顯示與實際接線不符。除此之外不影響其餘設計。

### 分壓接法與極性

每顆光敏電阻各自一組分壓，**不共用固定電阻**（共用會讓兩路互相拉動，讀值
失去獨立性）：

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
訊號，雙通道各自的 threshold 與合併判定就失去意義。

可選的抑制雜訊電容也是各自一顆：由各自的 ADC 節點對 GND 接 100nF，
貼近 ESP32 的腳位。

LDR 在上、固定電阻在下的順序是**強制**的：韌體的判定是
`filtered_raw < threshold → away`，因此必須「亮→讀值高、暗→讀值低」。
反接會使閾值語意顛倒。

`R_fix` 沒有在本 ADR 固定數值。韌體以 raw ≤ 10 或 ≥ 4085 判為
`low_clipped`／`high_clipped`（`SensorTask::kSaturationLowRaw`／
`kSaturationHighRaw`；2026-08-29 前為單一 `saturated` 值，且會**不觸發
離席**——已被 [ADR-0020](0020-light-clip-is-diagnostic-not-presence-gate.md)
取代，clip 現在正常參與判定），但貼著極限仍然代表解析度變差，所以整個
關心的光照範圍最好還是落在 raw 11–4084 之間；正確值取決於實際 LDR 型號
與擺放位置，只能實機校正。10 kΩ 為起始建議值，見
[HARDWARE.md](../hardware/HARDWARE.md)。

### 合併規則

`pf_sensors::combine_light_channels()` 把兩個通道 reduce 成 presence
debounce 消費的單一 `(status, raw, threshold)` triple：

- **沒有可用讀值的通道被忽略**，只要還有其他通道有讀值。未安裝或故障的
  第二顆不得讓正常運作的第一顆失效（AGENTS.md：感測器是可選的，必須降級而非
  讓功能整個失敗）。（2026-08-29 更新：原文寫「非 `online` 的通道被忽略」，
  已被 [ADR-0020](0020-light-clip-is-diagnostic-not-presence-gate.md)
  取代——判準是 `light_status_is_decision_capable()`，`low_clipped`／
  `high_clipped` 不算「被忽略」的那一類，只有 `disabled`／`not_detected`／
  `error` 才會被忽略。）
- **先分 present／away 兩側，任何通道落在 present 側就必定從 present 側
  選；margin（`raw - threshold`）只在同一側內部當 tie-break，挑「最有
  代表性」的那顆**（`low_clipped` 一律 away 側、`high_clipped` 一律
  present 側、`online` 用 `raw >= threshold` 分側）。因為 presence 是
  單一門檻比較，這個選擇同時決定了兩個方向：**每個通道都判為暗才算暗，
  任何一顆判為亮就算亮**。換句話說——兩顆都同意暗，面板才休眠；任一顆見光
  就喚醒。回報的是**主導判定的那顆**，也就是目前讓裝置保持清醒（或確認
  全暗）的感測器，那正是「為什麼還沒睡」最有用的答案。
  （2026-08-29 更新：原文寫「`online` 通道中取 signed margin 最大者」，
  單純比 margin 對 clipped 通道並不可靠——`high_clipped` 的 margin 可以是
  很大的負值（例如 threshold 設在 4095），若旁邊有個 margin 沒那麼負但
  實際是暗的 `online` 通道，單純比 margin 會選到暗的那顆，讓合併結果誤判
  成 away，即使有一顆通道明明是亮的。2026-08-29 codex-cowork 第 2 輪抓到
  這個問題，改為上面「先分側」的版本；第 3 輪確認分側規則本身正確、沒有
  遺漏。詳見 ADR-0020「presence 與合併判定」與
  `light_sensor.hpp` 的 `light_channel_reads_present()`。）
- **完全沒有 decision-capable（`online`／`low_clipped`／`high_clipped`）
  通道時**回報最需要處理的狀態，排序為
  `error` > `not_detected` > `disabled`，且不附帶任何讀值
  （`channel_index == kLightChannelCount`，`raw`／`threshold` 皆為 `null`）。
  presence 在這種情況下一律塌回 `unknown`——沿用 ADR-0006 的既有規則，
  浮動或錯誤的 ADC 不得觸發離席。（2026-08-29 更新：`saturated` 原本也在
  這個排序與塌回規則裡，已被
  [ADR-0020](0020-light-clip-is-diagnostic-not-presence-gate.md) 取代——
  拆分後的 `low_clipped`／`high_clipped` 改為 decision-capable，不再落入
  這個分支。）

`update_presence()` 的簽名與 debounce 行為**不變**：合併發生在它之前，
duration debounce（`away_duration_s`／`return_duration_s`）仍是唯一的抗頻閃
機制，兩個通道共用同一組時間參數與同一個 `PresenceTracker`。

### 設定持久化：`SensorSettings` v2

NVS blob 版本由 1 升為 2，欄位改為每通道獨立：

| v1 | v2 |
| --- | --- |
| `light_enabled` | `light1_enabled`、`light2_enabled` |
| `light_threshold` | `light1_threshold`、`light2_threshold` |

`away_duration_s`／`return_duration_s`／`environment_enabled` 不變。

**v1 記錄會被遷移而非丟棄**：`load_sensor_settings()` 以
`nvs_get_blob` 回報的長度區分版本（v1 = 24 bytes、v2 = 28 bytes，兩個 layout
都刻意無 padding 並有 `static_assert` 把關），v1 的單通道設定併入通道 1，
通道 2 以停用＋預設閾值開始——那是「第二顆還沒接」的誠實狀態。載入路徑維持
唯讀，v2 改寫發生在下次儲存時，避免一次失敗的 flash 寫入把原本可讀的設定
變成載入錯誤。`sensor_settings_v1_crc32()` 必須維持 v1 的欄位雜湊順序，
改動它會使所有既有 v1 記錄 CRC 失敗，正好毀掉遷移要保住的東西。

### API 與 WebUI

`GET /api/v1/sensors` 的 `light` 物件改為：

```json
"light":{"status":"online","raw":1234,"threshold":2000,"saturated":false,
  "deciding_channel":1,
  "channels":[
    {"channel":1,"gpio":5,"status":"online","raw":1234,"threshold":2000},
    {"channel":2,"gpio":7,"status":"not_detected","raw":null,"threshold":2500}]}
```

- 頂層 `status`／`raw`／`threshold` 是合併結果，`deciding_channel` 指出來源
  （1-based）。沒有任何 decision-capable（`online`／`low_clipped`／
  `high_clipped`，2026-08-29 由 ADR-0020 從單純的 `online` 放寬）通道時
  三者皆為 `null`——0 會看起來像真的設定值。原本頂層的 `"gpio":5` 移除，
  GPIO 改由每個 channel 自報。
- 每通道的 `threshold` **即使該通道停用或故障也照常回報**：那是使用者設定的
  值，也是校正過程中唯一的參照。只有 snapshot 整個讀不到時才是 `null`。

`GET /api/v1/sensors/config` 與 `POST` 表單欄位同步改名為 `light1_*`／
`light2_*`。**沒有保留舊名相容**：這是編在同一份韌體裡的 WebUI，前後端一起
升級；舊名現在會被 `parse_sensor_config_form()` 當成 `unknown_field` 退回，
比靜默接受一個沒有通道歸屬的欄位安全。表單欄位名是 ui.js 與 C++ parser 之間
的字串契約，兩邊都不會編譯期檢查對方，因此由
`test/web/test_sensor_form_contract.mjs` 雙向比對把關。

## Consequences

- **AND 語意會降低偵測遮蔽的靈敏度**：單顆被外殼陰影或灰塵擋住時不會休眠。
  這是刻意的取捨——誤判離席（使用者在場卻白屏）比漏判離席（該睡沒睡）代價高
  得多。若某一顆長期被燈直射或故障卡在亮值，會導致永遠不休眠；症狀明顯
  （一直不睡），且 WebUI 的 `deciding_channel` 會直接指出是哪一顆。
- **兩顆感測器各自需要校正**：擺放位置不同就看到不同的光，共用一組閾值沒有
  意義。WebUI 同時顯示兩顆的即時 raw 與各自 threshold 就是為了支援這個流程。
- `RuntimeSnapshot` 改存 `light_channels[2]` 與 `light_decision`，取代原本的
  三個平坦欄位。snapshot 仍是 trivially-copyable POD
  （`runtime_coordinator.hpp` 的 `static_assert` 未變）。
- `SensorSettings` v2 的 blob 長度成為版本判別依據，因此兩個 layout 的大小
  **不得相同**，也不得引入 padding。這由 header 的 `static_assert` 與
  `test_sensor_settings` 的尺寸測試同時把關。
- `kSensorConfigBodyCapacity` 由 160 提高到 224：新表單在所有數值欄位都填滿
  7 位數時為 162 bytes，原上限會回 413。
- 實機驗證已於 2026-08-23 完成主要項目（ADC 校正、AWAY／PRESENT 轉換、
  合併判定、離席白屏與返回重繪）；剩餘項目與已接受的風險以
  [VALIDATION.md](../hardware/VALIDATION.md) 為準。

## Verification

- Host tests（`pio test -e native`）：
  - `test_light_sensor_filter` — `combine_light_channels()` 的通道選擇
    （較亮者勝、單一亮通道即維持亮、全部暗才算暗、各自比對自己的閾值）、
    單通道故障時另一通道存活、無 online 通道時的狀態排序與空讀值。
  - `test_sensor_settings` — v2 round-trip 與 CRC、每通道 boolean 驗證、
    v1 遷移、v1／v2 不得互相解碼、兩個 blob 尺寸不得相同。
  - `test_sensor_config_form` — 兩組獨立 enable、第二個 threshold 為必填、
    舊欄位名視為 `unknown_field`、逾長數值拒收。
  - `test_dashboard_serializer` — 兩通道 JSON、`deciding_channel`、單一亮
    通道主導判定、缺席通道被忽略、snapshot 未 publish 時 threshold 為 `null`。
- Web contract（`node test/web/test_sensor_form_contract.mjs`）：ui.js 送出的
  欄位名與 C++ parser 接受的欄位名雙向一致；兩組 threshold input 的
  `min`／`max`／`required` 與韌體的 ADC 範圍一致；每通道讀值元素存在且由
  ui.js 更新。已用 mutation 驗證此測試會變紅。
- Embedded build（`test_runtime_coordinator`）：兩通道寫入 snapshot 後
  `light_decision` 指向合併規則選出的通道。
- **實機驗證（2026-08-23）**：兩通道與 DHT22 接線後完成 ADC 校正、
  AWAY／PRESENT 轉換、離席白屏與返回重繪。完整數據見
  [VALIDATION.md](../hardware/VALIDATION.md) 同日段落。接線與校正步驟見
  [HARDWARE.md](../hardware/HARDWARE.md)。

## 修訂 2026-08-23：OR → AND（同日，實機接線後）

### 為什麼改

本 ADR 原本定為 OR（任一顆低於自己的閾值就休眠）。感測器實際接上後，同一天
的實機量測推翻了這個選擇：

```
GPIO5 2442（亮）   GPIO7 1022   共用門檻 1400   → presence=away
```

燈是亮的、GPIO5 讀到 2442，但 GPIO7 擺在較暗的位置（同環境下只有 GPIO5 的
約 42%），單獨低於門檻就把整台拖進離席。OR 邏輯本身沒做錯——GPIO7 確實低於
它自己的閾值——問題在於**兩顆感測器看到的光本來就不同**，而 OR 讓任何一顆的
局部條件都能代表整個房間。

使用者據此改變需求，以「**兩顆都暗才睡、任一顆亮就醒**」重新表述。

### 決定

`combine_light_channels()` 由取**最小** margin 改為取**最大** margin。因為
presence 是單一門檻比較，這兩個方向必然互補——不可能同時要「任一顆暗就睡」
又「任一顆亮就醒」，那在一亮一暗時會自相矛盾。

理由：

- **語意正確**：要偵測的是「房間暗了」，那是全域狀態。兩顆擺在不同位置本來
  就會看到不同的光，「兩顆都說暗」才等於房間暗了。
- **錯誤代價不對稱**：誤判離席（使用者在場卻白屏）遠比漏判離席（該睡沒睡）
  惱人。AND 減少的正是比較貴的那一種。
- **不犧牲真實偵測力**：實測關燈時兩顆都掉到 30 以下，AND 照樣觸發。

### 影響範圍

實作上只是比較方向反轉，其餘規則完全不變：非 `online` 通道仍被忽略（單顆
故障不影響功能）、狀態排序不變、`deciding_channel` 仍然回報主導的那顆——只是
語意由「最暗的那顆」變成「**最亮的那顆**，也就是讓裝置保持清醒的原因」，
對診斷反而更有用。

host tests 的斷言方向同步反轉，並新增「單一亮通道維持亮」與「全部暗才算暗」
兩個案例；使用者可見文案（WebUI 環境頁、README、HARDWARE.md）一併更新。
