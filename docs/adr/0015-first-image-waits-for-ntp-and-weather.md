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
