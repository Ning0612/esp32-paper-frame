# ADR-0015：開機首張真實圖片等待 NTP／天氣就緒或逾時

- Status: accepted
- Date: 2026-08-03
- Supersedes: none

## Context

`docs/adr/0014-weather-panel-refresh-cadence-and-map-picker.md` 把天氣更新
改成 carousel 面板刷新被接受後才觸發，不再有獨立定時器。這個模型下，
開機後第一張面板刷新（不論是 welcome frame 還是第一張真實圖片）一定會
在狀態列缺少時間／天氣資料的狀態下先畫一次，之後才由 WeatherWorker 的
第一次嘗試補上——對 welcome frame（圖庫是空的）而言這是可接受的，但對
第一張「真實圖片」而言，使用者觀察到：如果 NTP／天氣其實只需要再等
幾秒鐘就會就緒，讓第一張圖片直接帶著 unknown/未同步的狀態列刷新一次，
之後很快又要再刷新一次補正確資料，體驗不佳。使用者決定：welcome
畫面維持現狀立即顯示；只有第一張「真實圖片」值得多等一下。

## Decision

- `src/app_main.cpp` 新增 `first_image_ready_or_timed_out(now_ms,
  boot_ms)`：在 `kFirstImageReadyTimeoutMs`（60 秒，`boot_ms` 是主迴圈
  開始前記錄的時間點，也就是核心 subsystem 初始化完成之後——這個 60 秒
  上限只涵蓋「等待本身」，不含前面 NVS／filesystem／Display／Network／
  Weather／OTA 等初始化耗時）內，要求 `RuntimeSnapshot.time_sync ==
  synced` 且天氣已經有一次結果（`weather.has_observation` 成功，或
  `weather.last_failure != none` 的失敗，兩者都算——包含 API key 未設定
  時 ADR-0014 新增的「空 key 直接判定 api_key_invalid、不發送 HTTPS
  請求」那條路徑）才算就緒；60 秒後不論是否就緒都直接放行。
- 這個判斷只套用在 carousel 主迴圈既有 decision 分派鏈裡的
  `DecisionKind::display_image` 分支，且只在
  `first_real_image_pending`（一個新增的、只在開機後第一次真實圖片顯示
  前為 true 的旗標）成立時生效；一旦真的送出成功一次，旗標永久轉
  `false`，之後的每次輪播都不再等待。`DecisionKind::display_welcome`
  完全不受影響，一律照舊立即渲染。
- `DecisionReason::manual`（使用者在 WebUI 手動指定要顯示的圖片）明確
  排除在這個等待邏輯之外：這個 gate 是為了「無人值守的自動開機」設計，
  使用者主動操作時應該立即反映，不應該被這個等待邏輯拖慢。
- 未就緒時的處理方式沿用既有的暫時性失敗模式：呼叫
  `carousel.abandon(decision, now_ms + kCarouselRetryMs)`（1 秒後重試），
  不直接動 `CarouselScheduler` 內部狀態，讓下一輪主迴圈重新 poll、重新
  檢查就緒條件。

## Consequences

- 開機後「等待本身」最多多花 60 秒才顯示第一張真實圖片；裝置永久無法
  連上網路（Wi-Fi 連不上、SNTP 永遠不同步）的最壞情況下，這 60 秒會被
  完整用滿一次（僅此一次，不是每張圖片都要等），之後照常運作。這是
  刻意的取捨，不是缺陷。
- 這個邏輯目前只存在 `src/app_main.cpp`（ESP-IDF 整合層），沒有拆出
  host-testable 的純函式版本——`app_main.cpp` 本來就是薄整合層，這條
  dispatch chain 上其餘既有的 accept/abandon/retry 邏輯同樣沒有獨立
  host test，此處維持與既有程式碼一致的慣例，而非引入新的落差。
- 未來若要改變就緒判定條件、逾時秒數，或讓手動啟用之外的其他情境也
  豁免等待，需要新的 superseding ADR。

## Verification

- `pio run`（`paperframe-s3`）與 `pio test -e native`（282 個既有 host
  test，這個功能本身沒有新增可 host-test 的純邏輯）全綠。
- codex-cowork 審查（luna xhigh）：0 Critical，1 High（`boot_ms` 的計時
  起點語意已在程式碼註解澄清，非缺陷）、3 Medium（手動啟用豁免已修正；
  永久離線的一次性 60 秒延遲與缺乏測試覆蓋為已知、可接受的取捨）。
- 尚未驗證項目（記錄於 `docs/hardware/VALIDATION.md`）：實機開機時序
  下這個 60 秒視窗與 welcome/手動啟用互動的實際行為。

> **2026-08-23 後續**：本節描述的 `DisplayOutcome` 契約問題已由
> [ADR-0019](0019-separate-frame-displayed-from-panel-slept.md) 解決
> （`RuntimeResult` 新增 `frame_on_panel`，把「畫面已上到面板」與
> 「面板已 sleep」分開回報）。以下段落保留當時的分析與退避理由——
> 退避本身仍然有效，只是現在只保護真正沒上到面板的失敗。

## Update 2026-08-23：空圖庫的 welcome frame 在取得區網 IP 後重畫一次

### Context

本 ADR 原本的取捨是「welcome frame 帶著未就緒的狀態列先畫一次也可以接受，
反正之後很快會再刷新」。這個前提對「圖庫裡有圖」的裝置成立，但對**全新、
圖庫是空的**裝置不成立：

`CarouselScheduler::poll()` 在圖庫為空且 welcome 已成功顯示後，會回傳
`wait_decision(UINT64_MAX)` 永久停住——這本身是對的，沒有圖片時不該每 30
分鐘重刷同一張靜態畫面（一次全刷約 31 秒）。但 welcome frame 的狀態列上
印著裝置的區網 IP，而開機時 Wi-Fi 還沒拿到位址，於是那台裝置的面板會
永遠顯示沒有 IP 的 welcome frame。使用者因此拿不到位址，也就進不了 WebUI
上傳第一張圖——「之後很快會再刷新」的假設在這條路徑上永遠不會發生。

使用者於 2026-08-23 實機初始化新板時回報此問題。

### Decision

- `CarouselScheduler` 新增 `welcome_stale_` 旗標與
  `request_welcome_redraw(now_ms)`。poll 的永久等待條件改為
  `has_current_ && current_is_welcome_ && !welcome_stale_`；`complete()`
  在 welcome 成功顯示後清除該旗標，因此一次位址變更恰好換得一次刷新。
- `request_welcome_redraw()` 沿用 `force_immediate()` 的 in-flight 保護
  （`complete()`／`abandon()` 依賴它），並在**面板上是真實圖片**時拒絕——
  真實圖片每次輪播都會重畫自己的狀態列，不需要這條路徑。
- `src/app_main.cpp` 記錄目前面板上那張 welcome frame **實際印出的 IP**
  （取自剛渲染的 `StatusBarContent`，不另外再讀一次 snapshot，避免兩次
  讀取之間位址抵達而造成多餘重繪），每個 tick 與現況比對後決定是否請求
  重畫。
- **只有「位址存在且與畫面上的不同」才觸發**。位址消失（Wi-Fi 斷線）
  不觸發：面板留著一個已離開的位址不比留著空白更糟，而每次網路抖動都花
  掉一次 31 秒全刷則是實際成本。
- **welcome 刷新失敗改用短重試**（`kWelcomeRetryMs` = 30 秒，約一次全刷
  時間），不再沿用完整輪播間隔。理由：刷新失敗的 welcome 代表面板上是
  空白（開機時尚未畫過任何東西）或是一張**已知錯誤**的畫面（過期位址、
  或離席留下的白屏）；照原本的間隔要枯等 10 分鐘到 24 小時。
  **圖片刷新失敗維持原本的完整間隔**——面板上還留著前一張圖，內容完好，
  沒有東西要搶救，硬重試只會對一塊剛失敗的面板連續下刷新指令。
- `request_welcome_redraw()` 在「尚未顯示過任何東西」時**允許**（只有
  「面板上是真實圖片」才拒絕）。那正是第一張 welcome 刷新失敗後的狀態，
  也正是最需要再試一次的時候。
- **連續失敗指數退避，上限為正常輪播間隔**（30 秒 → 60 → 120 → …，成功即
  歸零）。理由見下方「已知遺留缺陷」：`refreshed_and_slept` 以外的 outcome
  **不代表畫面沒刷上去**，可能只是 deep sleep 那一步失敗；此時每次重試都是
  對一張已經正確的畫面再做一次全刷。固定 30 秒重試會變成每天約 1,400 次全刷，
  對有刷新壽命的面板是實際傷害。退避上限取正常間隔，等於持續故障時退回裝置
  本來就會用的節奏，同時保留「一次性失敗 30 秒內恢復」。
- **失敗退避不得被重畫請求往前拉**（`welcome_retry_backoff_`）。呼叫端每個
  tick（約 1 秒）都會重新評估條件，若不保護，永久失敗的面板會在每次失敗後
  一秒內就被重設為立即重試，退避形同虛設，變成接近 100% duty cycle 的刷新
  迴圈——對有寫入壽命的電子紙是實際傷害。**一般輪播間隔仍照常被往前拉**，
  那正是位址變更時要重畫的目的。旗標在 `complete()` 失敗且為 welcome 時設定，
  成功時清除，`issue()` 送出重試時清除；`abandon()` 不需處理，因為它只會發生
  在 `issue()` 之後（該處已清除旗標）。
- **presence 返回是刻意的例外**：該路徑同時呼叫 `force_immediate()`，而後者
  不受退避保護，等於一次性洗掉剩餘退避。這是可接受的——presence 轉換本身已由
  duration debounce 限流（預設離席 180 秒／返回 30 秒），不會形成迴圈，而使用者
  剛回到裝置前正是最該立刻重畫的時刻。
- 本 ADR 原本的決定「welcome 不套用 60 秒 NTP／天氣等待、一律立即渲染」
  **維持不變**。這次新增的是顯示之後的重畫條件，不是顯示前的等待條件。

### Consequences

- 空圖庫裝置在取得區網 IP 後會多花一次 31 秒全刷。這是必要成本：沒有它
  使用者無法得知位址。
- 有圖片的裝置行為完全不變（`request_welcome_redraw()` 在該情況下回傳
  false）。
- 未來若要讓「位址消失」也觸發重畫，或把重畫擴大到其他狀態列欄位
  （天氣、時間），需要重新評估刷新頻率的成本。

### 已知遺留缺陷（本次未修，需獨立處理）

**`DisplayOutcome` 把「畫面已刷上去」與「面板已成功 sleep」混為一談。**

依 [ADR-0003](0003-fix-phase2-display-integration.md)，driver 只有完整送出
`DEEP_SLEEP (0x07, 0xA5)` 才標記 deep sleep；`DISPLAY_REFRESH` 之後的
power-off／sleep 序列若失敗，回報的是 `busy_timeout`／`transport_error`。
而 `src/app_main.cpp` 只把 `refreshed_and_slept` 視為成功。因此「畫面確實
刷新成功，但 sleep 那一步失敗」會被當成完全失敗：`welcome_frame_ip` 不提升、
`welcome_stale_` 不清除，於是排程器不斷重刷一張**已經正確的畫面**。

這是**既有行為**，不是本次變更引入的——變更前同樣會重刷，只是每 10–1440
分鐘一次。本次的短重試會放大這個循環，所以加了上述指數退避把它壓回原本的
節奏；但根因沒有解決。

正確的修法是拆開結果契約：「frame 已送達面板」決定 `welcome_frame_ip` 與
scheduler 的 current state，「panel 已成功 sleep」則是另一件事，其復原不應該
再做一次完整 framebuffer 刷新。這會動到 driver → DisplayTask → runtime result
→ app_main → health serializer，且 ADR-0003 已凍結 driver contract，**需要新的
superseding ADR**，因此不併入本次變更。

### Verification

- `pio test -e native`：`test_carousel_scheduler` 由 22 個 case 增為 33 個，
  新增涵蓋——重畫後重新停住、abandon 後仍待處理、面板上是真實圖片時拒絕、
  in-flight 時拒絕、新圖片出現時不被 welcome 蓋掉；兩條刷新失敗路徑
  （初次 welcome 失敗、重畫失敗）走短重試而圖片失敗仍走完整間隔；
  退避保護（呼叫端每 tick 持續請求時退避不被重設、退避到期後請求照常生效、
  一般間隔仍可被往前拉）；連續失敗的指數退避與上限，以及成功後歸零。
  並保留「圖庫持續為空時 welcome 只顯示一次」的回歸測試。全專案 349/349 通過。
- 已用 mutation 驗證新測試確實會變紅：移除 poll 的 `&& !welcome_stale_` 後
  3 個測試失敗；移除失敗短重試分支後 2 個測試失敗（`Expected 30100 Was
  1800100`）；移除退避保護後 2 個測試失敗（`Expected 30100 Was 101`）。
- **實機驗證通過（2026-08-23）**：空圖庫裝置開機後，welcome frame 在
  t≈0.8s 送出（此時尚無位址），IP 於 t≈3.0s 抵達，第一次刷新於 t≈31.9s
  完成，`carousel_welcome_redraw_for_ip` 於同一時刻觸發（刷新進行中依
  in-flight 保護被拒，下一個 tick 才成功），第二次刷新於 t≈63.9s 完成且
  `outcome=1`，之後 `next_due_ms` 回到正常輪播間隔——**一次位址變更只換
  一次刷新**。使用者目視確認面板上 IP、天氣與日期均正確顯示。完整 log 見
  [VALIDATION.md](../hardware/VALIDATION.md) 2026-08-23 段落。
- **仍未驗證**：presence 返回時的 welcome 重畫（需要光敏電阻）；DHCP 續約
  導致位址變更時的重畫；**welcome 刷新失敗後的 30 秒短重試**（需要刻意讓
  面板刷新失敗才能重現，只有 host test 覆蓋）。
