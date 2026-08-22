# ADR-0006：感測器 driver 來源與在場/離席判定機制

- Status: accepted（部分被取代）
- Date: 2026-07-31
- Supersedes: none
- Superseded in part by: [ADR-0018](0018-dual-photoresistor-channels.md)
  ——光敏電阻擴充為兩個獨立通道後，本 ADR 的
  「溫濕度與光敏電阻在本階段维持各自獨立判定，不合併」條款與
  `GET /api/v1/sensors` 的 `light` schema 已失效。濾波方式（moving
  average）、duration-based debounce、非 `online` ADC 不得觸發離席、
  presence 經 snapshot setter 傳遞等其餘決策**均維持有效**。

## Context

Phase 7（`docs/archive/IMPLEMENTATION_PLAN.md`）要加入 DHT22 溫溼度感測器與光敏
電阻在場/離席偵測。`docs/archive/Guild.md` 4.8/4.9 節只給出定性描述與少數「建議值」
（離席 180 秒／返回 30 秒，WebUI 可調），沒有精確的讀取頻率、範圍驗證
數字、濾波演算法選擇、debounce 實作機制或 `GET /api/v1/sensors` 的
JSON schema；這些都必須在實作前固定，不能直接當作已驗證事實。

掃描確認：全 repo 目前沒有任何 sensor/presence 相關程式碼；GPIO pin map
已由 [ADR-0003](0003-fix-phase2-display-integration.md) 固定（光敏 ADC＝
GPIO5／`ADC1_CH4`，DHT data＝GPIO6，I²C SDA/SCL＝GPIO8/9 留給未來
I²C 感測器，GPIO4 已被 BUSY 佔用）；`CarouselScheduler` 目前沒有任何
「立即到期」或「重設 deadline」的方法，而原始需求草案明確要求返回時
「輪播計時重新開始…不使用離席前殘留的刷新 deadline」，這是必須新增的
API 缺口。

## Decision

### DHT22 driver 來源與授權

移植 `UncleRus/esp-idf-lib`（<https://github.com/UncleRus/esp-idf-lib>）
的 `components/dht` 與 `components/esp_idf_lib_helpers`，commit
`162af418d4702791fd3bf3e5d1577aea9ec5539c`（2026-02-18，查證於
2026-07-31）。授權為 BSD-3-Clause（Copyright (c) 2016 Jonathan
Hartsuiker、Copyright (c) 2018 Ruslan V. Uss），原生支援
`DHT_TYPE_AM2301`（即 DHT22/AM2302），公開 API 為同步阻塞式
`esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type,
gpio_num_t pin, float* humidity, float* temperature)`。

移植進 `components/pf_dht22/third_party/`，保留原始授權標頭，並附上
`LICENSE-dht.txt`／`LICENSE-esp_idf_lib_helpers.txt`（比照 `epd7in3e`
面板 driver 移植 Waveshare 上游程式碼＋ADR 記錄來源的既有做法）。單線
bit-bang 時序邏輯本身**不修改**——這段程式碼已被社群專案長期驗證過，
本專案沒有硬體可重新驗證微秒級時序正確性，重寫風險遠高於移植風險。

實際移植時發現並修正一處建置可攜性問題（非時序邏輯）：`dht.c` 上游寫
`#include <ets_sys.h>`，但本專案使用的 ESP-IDF 6.0.0 沒有這個裸檔名的
相容 shim，`ets_delay_us()` 只在 target-scoped 的 `<rom/ets_sys.h>`
（`esp_rom` component）宣告；已改為 `#include <rom/ets_sys.h>` 並在
移植檔案內加註記，不影響任何時序數值或協定邏輯。CMakeLists 依實測
REQUIRES `esp_driver_gpio esp_rom freertos pf_sensors`（`PRIV_REQUIRES
log`）：上游的 esp8266 分支已移除，`driver` 改用本專案既有慣例的
`esp_driver_gpio`；`ets_sys.h` 需要額外 REQUIRES `esp_rom`，這點是移植
時透過實際編譯錯誤查證得到，不是原始上游文件記載的依賴。

### DHT22 讀取頻率、範圍驗證與 backoff

- 有效範圍：溫度 -40°C ~ 80°C、濕度 0% ~ 100%（DHT22/AM2302 datasheet
  規格，非原始需求草案指定，本 ADR 自行決定）。
- 正常輪詢間隔與失敗後的指數退避／上限，沿用 `pf_weather::Cache` 的
  `record_success/record_failure/retry_due/stale` 設計形狀——這套模式
  已在 Phase 6 實作並通過 codex-cowork 審查，直接複用降低設計風險，不
  重新發明。`pf_sensors::EnvironmentCache` 的 interval/backoff 常數可與
  `pf_weather` 的數值不同（DHT22 datasheet 下限為每 2 秒一次，遠比天氣
  API 頻繁，正常輪詢間隔取遠高於此下限的值，避免感測器過熱誤差與匯流排
  雜訊累積）。

### 光敏電阻濾波方式

採用 moving average（原始需求草案 4.9 允許 moving average 或 median 擇一）。
選擇理由：固定大小 ring buffer 記憶體與計算成本都低於 median（不需
排序），且本產品的環境光是「緩慢漸變」場景（自然採光隨時間漸變，非
瞬間尖峰雜訊為主），moving average 已足夠平滑；median filter 對脈衝雜訊
的優勢在此場景效益有限。

### Presence debounce 機制

時間追蹤式（duration-based），不是離散事件驅動狀態機（不比照
`NetworkStateMachine`）：`PresenceTracker` 記錄目前穩定 `state` 與目前
候選 `candidate`／候選起始時間 `candidate_since_ms`；每次取樣得到的
瞬時判定若與 `candidate` 不同就重設候選與起始時間；候選持續維持超過
對應 duration（離席用 `away_duration_s`，返回用 `return_duration_s`，
預設 180／30 秒，`SensorSettings` 可調）才把 `state` 切為候選值。

`LightSensorStatus` 為 `disabled`／`not_detected`／`error`／`saturated`
時**一律不推進候選判定**，直接視為 `PresenceState::unknown`——對應
原始需求草案 4.9「不把 ADC 浮動值視為離席」與十一節「浮動、saturated 或
error ADC 不得觸發離席」。只有 `LightSensorStatus::online` 時才進行
threshold 比較與 debounce。

不額外加入 threshold 之外的 hysteresis band：原始需求草案唯一指定的抗頻閃
機制就是 duration debounce 本身，額外加窄化/加寬 threshold 屬於超出
需求的臆測設計，不列入本階段範圍。

### CarouselScheduler API 缺口

新增一個公開方法（精確簽名於實作時依測試驅動定案），語意為「讓下一次
`poll()` 立即判定到期」，用於 presence 從 away／unknown 轉為 present 時
呼叫，滿足原始需求草案「返回後…輪播計時重新開始…不使用離席前殘留的刷新
deadline」。新增時必須保留 `in_flight_`／`manual_pending_` 既有不變量
（不可讓一次強制到期繞過 in-flight 保護，否則會與既有的 request/complete/
abandon 生命週期衝突），並補上對應 host test。

### Presence 狀態傳遞路徑

不擴充 `RuntimeCoordinator` 的 `CommandKind`——Phase 6 已確認這個 queue
是 display-refresh-only 的形狀（`try_submit_command` 內部有
`command.kind == CommandKind::refresh_display` 的特例分支），硬塞第二種
command 需要對應特例處理，徒增複雜度。改用 Phase 6 為 weather／internet
reachability 建立的相同模式：`SensorTask` 直接呼叫
`RuntimeCoordinator::update_environment(...)`／
`update_light_and_presence(...)` 等 setter 寫入 snapshot，`app_main.cpp`
的 carousel poll loop 每輪比較前一輪觀察到的 presence 與目前 snapshot
的 presence，偵測轉換並反應——這與現有 carousel/display 決策迴圈的輪詢
架構一致，不需要新的跨 task 訊息通道。

### `GET /api/v1/sensors` JSON schema

原始需求草案只有「感測器與讀值」一行說明，schema 需自訂：

```json
{"ok":true,"data":{
  "environment":{"status":"online","gpio":6,"driver":"dht22",
    "temperature_c":24.4,"humidity_percent":62.5,"stale":false,
    "today":{"temperature_min_c":18.2,"temperature_max_c":26.1,
      "temperature_avg_c":22.4,"humidity_min_percent":40.0,
      "humidity_max_percent":70.0,"humidity_avg_percent":55.0}},
  "light":{"status":"online","gpio":5,"raw":1234,"threshold":2000,
    "saturated":false},
  "presence":"present"}}
```

未安裝／未啟用時對應欄位為 JSON `null`，不得回傳 `0`（呼應原始需求草案
「API 回傳 `null`，不得回傳 `0`」的明確要求）。

## Consequences

- `pf_dht22` 是本專案第一個包含第三方原始碼移植（而非重寫）的
  component；其 `LICENSE` 檔與程式碼內授權標頭必須隨版控保留，未來
  升級上游版本需重新記錄 commit hash。
- `pf_sensors` 純邏輯與 `pf_dht22`／未來的 `pf_sensor_task` 分離，維持
  「host-testable 純邏輯 vs ESP-IDF 相依 side effect」的既有分層慣例
  （與 `pf_weather` / `pf_weather_worker` 的分離方式一致）。
- `CarouselScheduler` 新增的強制到期方法是本階段對既有已測試模組的
  介面擴充，需要額外注意不破壞 Phase 2 既有的 in-flight／manual
  pending 行為，屬於本階段風險最高的既有程式碼改動點。
- 溫濕度與光敏電阻在本階段维持各自獨立判定，不合併；未來若要加入
  多感測器合併的 presence 判斷邏輯，需要新的 superseding ADR。

## Verification

- `pio run` 與 `pio test -e native` 全綠是本 ADR 所涵蓋所有變更的最低
  驗證門檻；DHT22 bit-bang 時序與光敏 ADC 實際校正無法 host test，須在
  `docs/hardware/VALIDATION.md` 記錄實機驗證結果。
- `components/pf_sensors/third_party/LICENSE-dht.txt`（2026-08 前路徑為
  `components/pf_dht22/third_party/LICENSE-dht.txt`；本行原始撰寫時誤寫
  為 `pf_dht22/LICENSE`，一併修正）內容須與
  `https://github.com/UncleRus/esp-idf-lib/blob/162af418d4702791fd3bf3e5d1577aea9ec5539c/components/dht/LICENSE`
  逐字一致，作為移植合規性的可重現查核點。

## Update (2026-08)

反過度設計整併：`pf_dht22` 與 `pf_sensor_task` 兩個 component 已併入
`pf_sensors`，namespace 統一為 `pf_sensors`（`Dht22EnvironmentSensor`／
`SensorTask` 類別本體與本 ADR 記錄的所有決策不變，純粹是元件邊界調整）。
移植授權檔隨目錄搬遷到 `components/pf_sensors/third_party/`，內容與逐字
查核要求不變。同一輪也刪除了未被 production 使用的死抽象層
`pf_sensors::LightSensor`／`NullLightSensor`（`SensorTask::sample_light()`
一直是直接呼叫 `adc_oneshot_read()`，從未透過這個介面）；
`LightSensorStatus`／`MovingAverageFilter` 不受影響。本文其餘內容提到的
`pf_dht22`／`pf_sensor_task` 是撰寫當下的元件名稱，保留作歷史紀錄。
