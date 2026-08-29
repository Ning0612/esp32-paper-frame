# ADR-0020：光敏電阻 ADC 飽和只是診斷資訊，不再排除離席判定

- Status: accepted
- Date: 2026-08-29
- Supersedes: [ADR-0006](0006-sensor-drivers-and-presence.md) 與
  [ADR-0018](0018-dual-photoresistor-channels.md) 的「`saturated`（含其
  `low_clipped`／`high_clipped` 拆分前的原始形態）不得觸發離席判定」條款，
  對應原始需求草案 Guild.md 十一節「浮動、saturated 或 error ADC 不得觸發
  離席」——本 ADR 對 saturated／clipped 這一種情形明確修正該條款，其餘（
  未偵測到、讀取錯誤、通道停用）維持原條款不變。

## Context

ADR-0006 定下「`LightSensorStatus` 非 `online` 時一律不推進 presence 候選、
塌回 `unknown`」的規則，把 `saturated`（ADC 讀值頂到 `raw ≤ 10` 或
`raw ≥ 4085`）與未偵測到、讀取錯誤、通道停用一視同仁地排除在判定之外。
理由是「不確定的讀值不該驅動離席」。

`R_fix` 分壓電阻的選型必須讓部署環境全部的光照範圍落在 raw 11–4084 之間，
才能避免任一端頂到飽和門檻（HARDWARE.md「`R_fix` 選擇計算方法」）。但兩顆
獨立光敏電阻、各自擺放位置不同、環境全暗程度也會隨百葉窗／遮光物變化，
「兩端都留餘裕」在實務上只能盡量逼近，不能保證永遠成立。

2026-08-27 實機診斷「presence 偶爾卡在持續刷新、不轉白畫面」時發現：當時
`R_fix=47kΩ`，全暗環境下 channel 1（GPIO5）已經常態貼在 `saturated`
（raw ≤10），channel 2（GPIO7）餘裕也所剩無幾（15–27）。兩顆同時（或交替）
貼上飽和門檻時，`combine_light_channels()` 找不到任何 `online` 通道，
回報 `saturated`，`update_presence()` 因此塌回 `unknown`——即使兩顆的原始
讀值明明白白地說「暗」。每次塌回 `unknown` 都清空累積中的 180 秒離席倒數，
持續全暗的房間因此永遠等不到倒數計滿，面板只能反覆刷新、不會白屏休眠。
換成 `R_fix=100kΩ` 只是把餘裕重新分配（見 HARDWARE.md），沒有改掉這個
判定邏輯本身的缺陷：換一批 LDR、換一個更暗的部署環境，一樣可以重現。

追根究底，`saturated` 混淆了兩件不同的事：「這個讀值不確定該怎麼比較
threshold」（例如通道故障、讀取逾時、根本沒接線）與「這個讀值太明確反而
超出 ADC 的線性範圍，但方向已經不可能反過來」。後者——ADC 頂到極限——不是
「不知道是暗是亮」，而是「暗到／亮到量不出精確數字，但暗還是亮已經沒有
懸念」。用同一個「排除判定」規則處理這兩種情況，等於把後者的確定性硬是
丟棄了。

## Decision

### `LightSensorStatus` 拆分 `saturated` 為 `low_clipped`／`high_clipped`

```
raw ≤ 10  → low_clipped   （原 saturated 的下半段）
raw ≥ 4085 → high_clipped （原 saturated 的上半段）
```

不保留 `saturated` 這個列舉值（沒有 NVS 或其他持久化格式依賴它，純執行期
計算與 API 字串，比照 ADR-0018「沒有保留舊名相容」的既有慣例）。

新增 `pf_sensors::light_status_is_decision_capable(status)`，回傳
`status == online || status == low_clipped || status == high_clipped`。
這是本 ADR 的核心判準，`combine_light_channels()` 與 `update_presence()`
共用同一個函式，避免兩處各自定義判準而漂移。

### presence 與合併判定

- `update_presence()`：只有 `light_status_is_decision_capable()` 為
  false（`disabled`／`not_detected`／`error`——真的沒有讀值可用）才塌回
  `unknown`。`low_clipped`／`high_clipped` 正常推進候選、正常走 duration
  debounce，但**不透過 `raw < threshold` 比較決定方向**——直接由 status
  本身決定：`low_clipped` 一律讀為 `away`，`high_clipped` 一律讀為
  `present`。
  草案階段曾經假設「ADR-0018 已經要求校正時把整個關心的光照範圍落在 raw
  11–4084 之間，所以 threshold 本身必然落在這個區間內，clip 後的比較
  方向不會反轉」，但這個假設不成立：`threshold` 是使用者可調欄位
  （`pf_config::kMinLightThreshold`／`kMaxLightThreshold` = 0–4095，
  WebUI `light1-threshold`／`light2-threshold` input 的 `min="0"
  max="4095"`），沒有任何驗證把它限制在 11–4084——ADR-0018 那句話講的是
  「部署環境實際會出現的原始讀值範圍」，不是「threshold 設定值的合法範圍」。
  若使用者把 threshold 設在或超過某個 rail（例如 `threshold=4095`），舊的
  `raw < threshold` 比較會把 `high_clipped` 的 `raw=4085` 判成
  `4085 < 4095 → away`——最亮的可能讀值被讀成「暗」，方向整個反過來（
  2026-08-29 codex-cowork 審查抓出的 High 級問題，已在合入前修正，改為
  status 直接決定方向，不再依賴任何 threshold 位置假設）。
- `combine_light_channels()`：decision-capable 的判斷從
  `status == online` 放寬為
  `light_status_is_decision_capable(status)`。合併結果的 `status` 改回報
  贏家通道自己的實際狀態（可能是 `online`／`low_clipped`／
  `high_clipped`），不再硬性寫死成 `online`——讓 WebUI 與 API 消費者看得
  出「目前主導判定的這顆正貼著哪一端」。
  只有沒有任何 decision-capable 通道時才退回舊有的故障排序
  （`error` > `not_detected` > `disabled`），此時 presence 才塌回
  `unknown`。

  **贏家選擇不是單純比 margin**（2026-08-29 codex-cowork 第 2 輪抓到的
  第二個 High 級問題，屬於跟 `update_presence()` 那個問題同源的同一類
  bug，合入前一併修正）：新增 `light_channel_reads_present()`，用跟
  `update_presence()` 完全一樣的規則（`low_clipped`→away、`high_clipped`
  →present、`online` 才比較 raw 與 threshold）先把每個 decision-capable
  通道分成 present／away 兩側，**任何通道落在 present 側就必定從 present
  側選**，margin 只在同一側內部當 tie-break（原本挑「最有代表性」那顆的
  邏輯，不影響 present／away 這個結果本身）。原本純比 margin 的版本在
  threshold 貼著 rail 時一樣會反轉：`high_clipped(raw=4085,
  threshold=4095)` 是 margin `-10` 的 present 通道，若旁邊有個
  `online(raw=0, threshold=1)` 是 margin `-1` 的 away 通道，純比 margin
  會選到那個 away 通道，讓合併結果（進而整個 presence）誤判成 away，即使
  有一顆通道明明是亮的。這正是 ADR-0018「任一顆看到光就喚醒」要保證卻被
  破壞的那個不變量。

### 感測取樣：clip 期間持續餵入濾波器

`SensorTask::sample_light()` 原本在非 `online` 時整個跳過濾波器並清空
歷史（避免拿舊讀值混淆復原後的第一筆樣本）。`low_clipped`／`high_clipped`
現在改為**跟 `online` 一樣持續 push 進 `MovingAverageFilter`**，理由是
ADC 卡在同一個物理極限時，這不是「感測器剛復原、歷史值該丟棄」的情境，
而是連續發生的真實讀值——清空濾波器只會讓每一筆 clip 樣本都獨立、增加雜訊
敏感度，沒有任何好處。只有真的變成 `disabled`／`not_detected`／`error`
（通道被關閉、拔線、讀取失敗）才清空濾波器歷史，維持 ADR-0006 原本「不
拿故障前的舊值混進復原後第一筆樣本」的理由。

### API／WebUI

`GET /api/v1/sensors` 的 `light` 物件與每個 channel 物件的 `status`
欄位，字串值多了 `"low_clipped"`／`"high_clipped"`、不再有
`"saturated"`；`raw`／`threshold` 欄位在這兩個狀態下**照常回報實際數字**，
不再是 `null`（因為現在是 decision-capable，跟 `online` 走同一條序列化
路徑）。頂層 `saturated` boolean **維持欄位存在**，語意改為
「目前主導判定的通道是 `low_clipped` 或 `high_clipped` 其中之一」——
方向本身已經在 `status` 字串裡看得到，這個欄位純粹是「要不要提醒使用者去
校正」的快速旗標，不需要新增欄位表達方向。

WebUI（`ui.js`／`i18n.js`）新增 `low_clipped`／`high_clipped` 的
zh-Hant／en 翻譯（「偏暗飽和」／「偏亮飽和」，"Low clipped"／
"High clipped"），拿掉 `saturated` 對應的翻譯 key。

## Consequences

- 2026-08-27 那次「presence 偶爾卡在持續刷新、不轉白畫面」的根因在判定層
  修掉：往後同一個 `R_fix` 在不同 LDR／環境下若又貼著某一端的飽和門檻，
  presence 依然能正確判斷方向，不會再塌回 `unknown`、不會再清空離席倒數。
  HARDWARE.md「為什麼歷史上優先保暗端」一節記錄的風險不對稱因此不再是
  `R_fix` 選型的必要考量，兩端可以用同等權重取幾何平均。
- **殘留風險（刻意接受）**：一個啟用但完全沒接線的通道，若懸空讀值恰好落
  在 `raw ≤ 10` 或 `raw ≥ 4085` 的區間，過去會被排除在判定外（該通道對
  presence 沒有影響力）；現在會被當成 `low_clipped`／`high_clipped`，
  以那個極限值正常參與判定。這與「懸空讀值落在中間值」的既有已知風險
  （HARDWARE.md「只接一顆光敏電阻時」）是同一類問題，兩者都要求使用者
  在 WebUI 停用未接線的通道——這個操作步驟本來就是必要的，不是本 ADR
  新增的義務，只是懸空「剛好落在極限」不再享有意外的保護。
  未接線通道保持停用即可完全迴避，風險範圍限定在使用者未依文件操作的
  誤用情境。
- `LightSensorStatus` 的列舉值數量從 5 個變成 6 個，`to_string()` 與
  `light_status_rank()` 的 `switch` 都需要窮舉新值，兩者都在本次改動中
  一併更新，靠編譯器對未窮舉 `switch` 的警告把關（無 `default` 分支）。
- API schema 變更：`raw`／`threshold` 在 clip 狀態下不再是 `null`，前端
  或未來的其他消費者若原本假設「非 `online` 就是 `null`」需要更新——本次
  一併同步了 `ui.js`（本專案唯一的 API 消費者）。

## Verification

- Host tests（`pio test -e native`）全綠（392 test cases），新增／調整：
  - `test_light_sensor_filter`：`test_a_clipped_channel_decides_like_an_online_one`
    （clipped 通道跟 online 一樣能贏過 disabled 對手）、
    `test_both_channels_clipped_dark_still_decides`（重現 2026-08-27 的
    兩顆同時貼暗端場景，驗證 decision 仍然正確且不落回故障排序）、
    `test_a_present_channel_always_beats_an_away_one_regardless_of_margin`／
    `test_an_online_present_channel_beats_a_low_clipped_away_one`（涵蓋
    codex-cowork 第 2 輪抓到的合併器 margin 反轉問題：present 側必須贏過
    away 側，不論各自的 margin 數字）、
    `test_two_present_channels_break_ties_by_margin`（同側時 margin 仍照舊
    當 tie-break，涵蓋第 3 輪指出的測試缺口）；
    `test_a_working_channel_survives_a_dead_one` 移除 `saturated` 分支
    （clipped 不再屬於「dead」通道那一類）。
  - `test_presence`：`test_not_detected_error_and_disabled_never_trigger_away`
    （改名，拿掉 saturated）、
    `test_low_clipped_reading_advances_the_away_candidate`／
    `test_high_clipped_reading_advances_the_present_candidate`（clip 狀態
    正常推進 debounce 候選並達成穩定 state）、
    `test_clipped_direction_holds_even_at_pathological_thresholds`
    （threshold 設在 rail 上或超過 rail 時，clip 方向仍然正確——涵蓋
    review 抓到的 High 級問題）。
  - `test_dashboard_serializer`：
    `test_sensors_report_reading_when_light_clipped`／
    `test_serialize_sensors_reports_reading_when_light_clipped`（compact
    與完整 sensors 端點在 clip 狀態下都回報真實 raw，而非 null）；
    `test_sensors_report_null_readings_when_disabled_or_not_online` 改用
    `error` 取代 `saturated` 驗證故障排序（該測試的意圖——非 decision-capable
    通道回報 null——用 `error` 一樣成立，且不再與新語意衝突）。
- Web contract（`node test/web/test_dashboard_ui_contract.mjs`、
  `node test/web/test_i18n_contract.mjs`）：`labelSensorStatus` 覆蓋
  `low_clipped`／`high_clipped`，i18n 字典 zh-Hant／en 對應鍵值齊全。
- `pio run`（`paperframe-s3` 韌體）與
  `pio test --project-conf platformio-embedded.ini -e paperframe-s3-embedded-test --without-uploading --without-testing`
  （`-Werror` 編譯門檻）均通過。
- 實機驗證：本次改動未接觸 ADC 讀取或接線本身，行為變更集中在純邏輯層
  （`pf_sensors`／`pf_web` 的 host-testable 部分），依 AGENTS.md 的分層
  慣例可用 host test 完整覆蓋；下次在深暗環境觀察到 clip 狀態時，用
  WebUI 即時 raw 確認 presence 是否正確判斷為 `away`，記錄於
  [VALIDATION.md](../hardware/VALIDATION.md)。
