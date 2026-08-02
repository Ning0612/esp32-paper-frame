# ADR-0014：天氣更新改為面板刷新觸發，WebUI 改用地圖選點

- Status: accepted
- Date: 2026-08-02
- Supersedes: ADR-0005（僅「Rate limit / 更新頻率」子決策與
  `WeatherSettings` 的 `location`/`language` 欄位；該 ADR 其餘內容如
  endpoint、TLS trust、cache schema、SNTP、Internet 可達性訊號不受影響）

## Context

ADR-0005 當初刻意選擇讓天氣更新頻率由使用者設定的
`WeatherSettings.update_interval_minutes`（10–1440 分鐘）驅動，並記錄
「天氣本身 10–1440 分鐘的更新頻率與既有 5–30 分鐘 carousel refresh 週期
已經匹配，此設計為刻意簡化，非遺漏」。同時 `WeatherSettings` 還保存了
使用者可填的顯示地點（`location`，僅供 WebUI 顯示用，從未送進
OpenWeatherMap 請求或狀態列渲染）與語言（`language`，直接送進
OpenWeatherMap 的 `lang` 查詢參數）。

使用者決定重新取捨：WebUI 天氣頁面移除顯示地點、語言、更新間隔三個欄位，
理由是（1）語言固定用英文可以讓韌體內部天氣狀況/圖示對應只需維護一份
字串表，不需要處理多語系;（2）顯示地點欄位從未影響任何實際行為，純粹是
畫面裝飾，移除後簡化表單;（3）更新間隔改為 carousel 每次面板刷新被接受
後觸發一次天氣更新，不再需要獨立的使用者可調時間間隔。此外，WebUI 新增
一個地圖區塊，讓使用者可以透過拖曳地圖、放置圖釘來決定經緯度，取代純手動
輸入兩個數字欄位的既有方式（數字欄位仍保留，雙向同步）。

專案的離線優先原則（見 `CLAUDE.md`：WebUI／圖片／設定必須能在沒有外部
CDN 或雲端後端時完整運作）與「可以載入真實地圖圖磚」的產品期望之間需要
取捨：真正可辨識地理位置的底圖資料（海岸線、國界）不可能在不引入授權
素材或大幅增加 `webfs`（1 MiB 分割區，目前用量約 157 KB）用量的前提下
離線提供。使用者決定接受「連線時載入外部地圖圖磚（OpenStreetMap XYZ
raster tiles），離線時降級為不含地理外框的經緯度格線」這個混合方案，
而非為了維持純離線而放棄真實地圖。

## Decision

### 更新頻率：面板刷新觸發，不再有使用者可調的定時器

- `WeatherSettings`/`WeatherSettingsBlob`（`components/pf_config/include/
  pf_config/weather_settings.hpp`）移除 `update_interval_minutes`、
  `location`、`language` 三個欄位，`kWeatherSettingsVersion` 由 1 升到 2。
  舊（較大的）v1 blob 讀回時會因為 `nvs_get_blob()` 的長度不符而讓
  `load_weather_settings()`（`config_manager.cpp`）回傳非 `ESP_OK`；該
  函式在錯誤路徑回傳的 `WeatherSettingsLoadResult.settings` 欄位仍是
  `WeatherSettings{}`（帶預設值的聚合初始化，等同安全的 Taipei/metric
  預設）,呼叫端只要不因為 `error != ESP_OK` 就整包放棄使用
  `settings`,就能拿到正確預設值。`app_main.cpp` 啟動路徑本來就是這樣
  處理（log「using defaults」後照樣使用該欄位）；本次一併修正
  `WeatherWorker::fetch_once()`（先前發現的既有缺口，見下方
  Consequences）比照辦理，但只在 `load_weather_settings()` 回傳
  `ESP_ERR_INVALID_CRC` 或 `ESP_ERR_INVALID_SIZE`（`config_manager.cpp`
  已把 NVS 專屬的 `ESP_ERR_NVS_INVALID_LENGTH` 正規化成後者,讓
  `pf_weather` 端不需要認得 NVS 特定錯誤碼）時才視為「可預期的 schema
  不相容,套用預設值繼續」；其餘錯誤（例如 NVS namespace 打不開）仍照
  舊行為視為 `Failure::network`,避免把真正的儲存層故障誤判成單純的
  API key 問題而蓋掉根因。目前僅有開發機、尚無其他實機使用者存量資料
  需要保留，因此不寫欄位級的遷移路徑,只確保「版本不符仍能拿到可用
  預設值」這件事名副其實。
- `WeatherWorker::fetch_once()`（`components/pf_weather/
  weather_worker_esp_idf.cpp`）呼叫 `pf_weather::record_success()` 時，
  `interval_ms` 參數固定傳入 `UINT64_MAX`（飽和後的「無自動排程」
  sentinel），取代原本的 `settings.update_interval_minutes * 60000`。
  `task_main()` 的等待邏輯在偵測到 `next_attempt_ms == UINT64_MAX` 時直接
  `wait_ticks = portMAX_DELAY`（避免對 `UINT64_MAX` 呼叫
  `pdMS_TO_TICKS()` 造成的整數溢位),之後只靠
  `request_immediate_refresh()` 喚醒。失敗重試（`record_failure`/
  `retry_due` 的指數 backoff，10 秒到 60 分鐘）維持不變，與「更新間隔」
  無關,是獨立於本決策的既有韌性機制。`fetch_once()` 額外新增一個前置
  判斷：`api_key` 為空字串（尚未設定）時完全不建立 HTTP client、直接記
  `Failure::api_key_invalid` 並套用同一顆「無自動重試」sentinel——反正
  空 key 保證得到 401,沒必要真的發一次 HTTPS 請求才知道,且使用者存入
  有效 key 後一樣會被既有的 `request_immediate_refresh()` 喚醒。
- `src/app_main.cpp` 的 carousel 主迴圈在 welcome/image 兩條路徑各自的
  `try_submit_refresh()` 回傳 `SubmitStatus::accepted`（也就是面板刷新
  真的被送出，不是即將刷新的 `CarouselDecision` 本身）時，才呼叫
  `pf_weather::weather_worker().request_immediate_refresh()`。刻意不掛在
  `carousel.poll()` 產生非 `wait` decision 的當下：那個時間點涵蓋了 frame
  pool 忙碌、catalog 找不到條目、`try_submit_refresh()` 被拒絕等失敗
  分支，這些分支會呼叫 `carousel.abandon(decision, now_ms +
  kCarouselRetryMs)`（`kCarouselRetryMs = 1000`），若天氣觸發也掛在那裡，
  失敗會在主迴圈每秒重試期間變成每秒呼叫一次
  `request_immediate_refresh()`，違背「一次真正的面板刷新對應一次天氣
  喚醒」的本意並對 OpenWeatherMap 造成不必要的請求量。只在真正送出成功時
  觸發,才能維持
  「一次真正的面板刷新對應一次天氣喚醒」。這一次的面板刷新仍使用目前
  快取的天氣資料（HTTPS fetch 是非同步的，不會等待完成才繪製），效果是
  「下一次」面板刷新時天氣資料更有機會是新的——與 ADR-0005「狀態列內容
  更新沿用既有整頁 refresh 節奏」的既有精神一致，只是把排程來源從獨立
  定時器換成面板刷新事件本身。

### 語言：固定英文

- `WeatherWorker::fetch_once()` 組出的 OpenWeatherMap URL 直接寫死
  `&lang=en`，不再從設定讀取。理由見 Context：內部天氣狀況/圖示對應只需
  維護一份字串表。

### WebUI：地圖選點（線上圖磚 + 離線格線降級)

- `data/web/index.html` 天氣表單新增地圖區塊（`#weather-map` 及其子
  元素），緯度/經度數字輸入框保留、雙向同步。
- `components/pf_web/health_server.cpp` 的 `set_common_headers()` 放寬
  `Content-Security-Policy` 的 `img-src`，從單純 `'self'` 改成
  `'self' https://tile.openstreetmap.org`。這個 CSP 套用在包含
  `index.html` 本身的所有回應上，實作初版沒有同步放寬時，瀏覽器會
  無條件擋下所有 tile 請求（連線探測與正式圖磚都一樣），導致地圖
  100% 卡在離線模式，且現象與真實網路狀態無關、無法透過調整
  `navigator.onLine` 判斷時機修正——開發過程中由使用者實測發現。
  `connect-src` 刻意不放寬，因為地圖只用 `<img>`/`Image()` 載入圖磚
  （受 `img-src` 管轄），從未對 tile host 發出 `fetch`/`XHR`。同一次
  安全複核順便補上原本缺的 `frame-ancestors 'none'`、`base-uri 'none'`、
  `form-action 'self'`、`object-src 'none'`（防點擊劫持/嵌入、限制表單
  提交目標），與地圖功能無關但屬同一顆 header、順手一併加固；刻意不加
  `upgrade-insecure-requests`，因為裝置本身以 HTTP 提供服務，該指令會
  讓瀏覽器嘗試把自身的相對資源升級到不存在的 HTTPS 來源而壞掉。
- `data/web/ui.js` 新增 `weatherMap` 模組：
  - 連線模式：以標準 Web Mercator 公式手刻最小 slippy map（無第三方地圖
    函式庫依賴，只有 tile 圖片走外部 HTTP），從
    `https://tile.openstreetmap.org/{z}/{x}/{y}.png` 抓取 XYZ raster
    tiles;固定在容器正中央的圖釘不動，使用者拖曳地圖本體來決定圖釘下方
    的座標（常見於 Google Maps／Uber 的「選擇地點」UI），這樣不需要處理
    自由移動圖釘與圖磚座標系的疊加運算。`+`/`-` 按鈕縮放（zoom 0–12,
    以中心點錨定)。
  - 離線降級：頁面載入時先以 `navigator.onLine` 快速判斷,或嘗試載入
    一張 tile 探測連線;任一途徑判定為離線時,永久切換成 canvas 手繪的
    等距圓柱投影（equirectangular）經緯度格線（每 30 度一條線 + 度數
    標籤,紅線標示赤道/本初子午線),使用者可以自由點擊或拖曳畫出的圖釘。
    這個格線完全是程式繪製（字型走 canvas 內建、線條走向量繪圖）,不含
    任何點陣圖或第三方素材,因此不需要授權素材、不需要在
    `ASSET_CREDITS.md`／`THIRD_PARTY_NOTICES.md` 新增條目,也不佔用
    `webfs` 空間。
  - `#weather-map-attribution` 是疊在地圖右下角的連結（連到
    `https://www.openstreetmap.org/copyright`），只在連線模式顯示;
    OSM tile usage policy 要求署名清楚顯示在地圖本體上，不能只放在地圖
    外的說明文字，因此改成 overlay 而非純文字段落。
  - 兩種模式都會把最終座標寫回既有的 `weather-latitude`/
    `weather-longitude` 數字輸入框,送出到後端的 API 契約（`latitude_e6`/
    `longitude_e6`）完全不變。拖曳跨越 antimeridian（±180°經線）時
    `centerLon` 會 wrap 回 `[-180, 180)` 再寫回輸入框，避免產生超出
    `kMinimumLongitudeE6`/`kMaximumLongitudeE6` 範圍而被後端 422 拒絕的值;
    緯度統一以 Web Mercator 可用範圍 `±85.0511°` 為準（包含初始從輸入框
    讀入時），不使用到 `±90°` 就會與圖磚實際能繪製的範圍不一致。

## Consequences

- `WeatherSettings` 的 NVS schema 版本升級（1→2）;現有開發機下次開機會
  回退到安全預設值,需要重新輸入 API key 與座標一次。可接受,因為尚未有
  實機使用者存量資料。
- `MaskedConfig`（`dashboard_serializer.hpp`）與
  `/api/v1/weather/config` 回應不再包含 `interval_minutes`、`location`、
  `language` 欄位;任何依賴這些欄位的既有前端程式碼或第三方整合都需要
  同步更新（本次一併更新了 `data/web/index.html`/`ui.js` 與
  `test/web/test_weather_ui_contract.mjs`）。
- WebUI 天氣頁面在「連線」時會直接向 `tile.openstreetmap.org` 發出
  請求,這是本專案 WebUI 第一次允許瀏覽器端主動連外部 CDN/服務（先前僅
  `data/web/*` 本身離線可用,不代表禁止使用者裝置在有網路時額外載入內容）。
  離線裝置/瀏覽器完全不受影響,自動降級為純本地繪製的格線,不影響任何
  既有離線可用性保證。
- 這也是本專案第一次為了功能需要而放寬 `Content-Security-Policy`
  （`img-src` 新增 `https://tile.openstreetmap.org` 這個單一白名單來源）。
  這是刻意、範圍受限的例外（僅此一個外部 host、僅 `img-src`），不是
  移除或全面放寬 CSP;其餘所有回應與所有其他 CSP 指令維持不變。
- 未來若要改變天氣更新的觸發模型、語言策略,或地圖選點的資料來源（例如
  改用離線可用的向量地圖素材）,需要新的 superseding ADR。

## Verification

- `pio run`（`paperframe-s3`）與 `pio test -e native`（含
  `test_weather_settings`、`test_weather_config_form`、
  `test_dashboard_serializer`、`test_weather`）全綠。
- `test/web/test_weather_ui_contract.mjs` 確認新地圖 DOM 元素存在、
  舊欄位（`weather-interval`/`weather-location`/`weather-language`）已
  移除、`ui.js` 含 `tile.openstreetmap.org` 與 `drawGraticule`。
- 尚未驗證項目（記錄於 `docs/hardware/VALIDATION.md`）：實機瀏覽器連上
  WebUI 後,線上地圖 tile 載入與離線降級的實際切換行為、面板刷新觸發的
  天氣更新在真實 carousel 週期下的時序。
