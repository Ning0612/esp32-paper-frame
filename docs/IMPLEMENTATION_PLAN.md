# PaperFrame MVP 實作計畫

狀態：Phase 5、Phase 6、Phase 7 程式實作完成；Phase 3／4 的部分實機收尾、
Phase 5 實機圖片輪播驗證、Phase 6 全部實機驗證（SNTP、HTTPS、狀態列視覺
結果）與 Phase 7 全部實機驗證（DHT22 讀值、光敏 threshold 校正、
AWAY/PRESENT 轉換）仍待完成，暫不部署大功能版本

需求基線：`Guild.md` v0.1

目標平台：ESP32-S3、16 MB Flash、PSRAM、7.3 吋 800×480 E6 e-Paper

## 1. 目標、範圍與交付策略

本計畫把草案「十二、建議的第一版開發範圍」整理成可依序 build、test、
review 與 commit 的垂直階段。MVP 完成時，即使溫溼度與光敏感測器都未安裝，
裝置仍可完成 AP 配網、受保護的本機管理、圖片處理與交易式上傳、六色輪播、
天氣快取、面板 sleep，以及基本診斷。

MVP 不包含草案第十節的 P1 功能，例如多 Wi-Fi profile、批次上傳、週排程、
歷史圖表、Discord 通知與自動清圖；也不包含 MQTT、蜂鳴器、音效或 AI 功能。

交付原則：

- 先建立能持續編譯與測試的最小骨架，再加入硬體功能。
- 優先把資料格式、狀態機、排程、防抖與交易復原寫成可在 host 測試的純邏輯。
- 每段以一個可回退、可驗證的 Conventional Commit 完成；功能與直接相關測試
  放在同一 commit。
- 硬體相依功能必須同時保留 fake/null implementation，讓無周邊環境可測。
- 每一階段通過其 exit criteria 後才進入下一階段；不得用後續階段補齊前段
  遺漏的安全、錯誤處理或測試。

## 2. 開工前決策 Gate

以下項目在對應階段開始前必須以實機資料、官方文件或 ADR 固定，不能直接以
草案中的「建議」當作已驗證事實：

| Gate | 最晚完成 | 需要固定的決策 |
| --- | --- | --- |
| G1 開發板 | Phase 1 | 精確模組／board ID、Flash/PSRAM 模式與容量、USB/UART 上傳方式 |
| G2 Pin map | Phase 2 | e-Paper SPI/BUSY/RST/DC/CS、ADC、DHT/I²C GPIO；避開 GPIO35–37 |
| G3 Driver | Phase 2 | `epd7in3e` 來源、版本、授權、六色 mapping、BUSY polarity 與 timeout |
| G4 PFR1 | Phase 4 | byte order、header 固定長度、enum/flags、CRC 涵蓋範圍與版本遷移規則 |
| G5 Partition | Phase 1 開發版；第一個可保存圖片的 Phase 5 build 前凍結發行 layout | 雙 OTA、NVS、webfs、imagefs、coredump 的 offset/size；後續變更的資料保護與 migration |
| G6 Security | Phase 3（PBKDF2 參數已由 `docs/adr/0007-auth-pbkdf2-iterations-and-sync-login.md` 固定，2026-08-01） | PBKDF2 參數、session token entropy/storage、首次設定與 Recovery AP 流程；密碼至少 8 字元、idle 30 分鐘、absolute 24 小時是需求固定值 |
| G7 Weather | Phase 6（已由 `docs/adr/0005-weather-worker-and-status-bar.md` 固定，2026-07-31） | OpenWeatherMap endpoint/version、TLS trust、cache schema 與 rate limit |

若缺少實體硬體，可先完成 host-tested interface 與 fake driver，但該階段不得
標成 hardware complete。

## 3. 共通技術基線

### 3.1 Runtime 邊界

```text
HTTP handlers ─┐
WeatherWorker ─┼─> command queue -> RuntimeCoordinator -> owner task
SensorTask ────┘                        │
                                       └─> immutable/read-only snapshot

DisplayTask: panel/SPI 唯一 owner
StorageWorker: imagefs/catalog/OTA 寫入序列化
NetworkServiceTask: STA/AP/SNTP/mDNS 狀態 owner
```

HTTP request 不得等待 25 秒面板刷新。長操作回傳 accepted/result ID，狀態由
snapshot 或 result endpoint 取得。Display、storage 與 OTA 共享的臨界區需明確
定義 lock order，避免 flash cache 關閉期間存取 PSRAM。

### 3.2 測試層次

- Host unit：PFR1、CRC、檔名驗證、catalog recovery、輪播、防抖、時間計算、
  設定遷移、session/CSRF 純邏輯。
- Component/integration：NVS、LittleFS、HTTP route、queue、weather parser。
- On-device smoke：boot、PSRAM、partition、AP/STA、mDNS、panel、ADC、OTA。
- Fault injection：CRC 錯誤、空間不足、`.part`／`.bak` 殘留、BUSY timeout、
  Wi-Fi 密碼錯誤、DNS/API/TLS 失敗、sensor absent。
- Browser：圖片轉換順序、四種 fit、四種 dithering、PFR1 golden vectors、
  responsive layout、離線資產與登入/CSRF 流程。

### 3.3 每段標準檢查

1. 需求與差異可追溯。
2. 最小相關測試先通過。
3. `pio run` 通過；若該階段尚無可 build 的 PlatformIO 骨架，需明列原因。
4. staged diff 只含本段內容，沒有 secrets 或 generated output。
5. 獨立 review 無 Critical／High finding。
6. 硬體未驗證項目記入該階段紀錄，不以模擬結果冒充實機結果。

## 4. 分階段計畫

### Phase 0 — Repository baseline

需求來源：草案第一、二、十二節。

交付物：

- 保留原始需求草案並建立專案級 `AGENTS.md`。
- 建立本實作計畫、README、license 決策與 PlatformIO/ESP-IDF 專用
  `.gitignore`。
- 記錄參考專案的精確 URL、採用版本與 license；只移植經授權的概念或程式。
- 建立 ADR 目錄與硬體驗證紀錄格式。

驗收：

- Git branch 為 `main`，每份初始化文件各有清楚 commit。
- 工作樹乾淨，未設定未經授權的 remote。
- 文件清楚區分需求、已決策項目與待驗證假設。

建議 commits：

1. `docs: add initial PaperFrame requirements draft`
2. `docs: define repository working agreements`
3. `docs: add phased MVP implementation plan`
4. `docs: add project overview and reference provenance`
5. `chore: add PlatformIO repository ignores`

### Phase 1 — Buildable ESP-IDF skeleton and persistence foundations

需求來源：草案 4.1、八、九、十二之 1–2。

交付物：

- `platformio.ini`、原生 ESP-IDF `app_main`、component 目錄與 logging 基線。
- G1 board 設定、PSRAM 檢查與可降級 self-test snapshot。
- 初版雙 OTA partition table，分離 `webfs`／`imagefs`。
- NVS schema version、ConfigManager、FileSystemManager 與啟動復原入口。
- RuntimeCoordinator 的 command/result queue 與 snapshot 最小 contract。
- 最小 `esp_http_server` 與公開 health handler/route contract，供後續長操作
  responsiveness 測試；Phase 1 只初始化 httpd 所需的 TCP/IP／esp-netif
  runtime，不建立 `esp_netif_t` interface、不啟動 AP/STA，也不得暴露設定
  或管理操作。

驗收：

- clean checkout 可完成 `pio run`。
- 裝置可 boot，不因缺少面板或感測器而 boot loop。
- partition offset/size 不重疊，實機可掛載兩個 filesystem。
- NVS 空白、schema 相同、可遷移版本與不支援版本都有測試。
- health serializer/handler 在 runtime queue 閒置或忙碌時都不等待 queue、
  filesystem 或硬體，且只輸出非敏感資料。實際經網路存取延至 Phase 3。

建議 commits：

1. `build: scaffold native ESP-IDF firmware`
2. `feat(storage): add versioned NVS configuration`
3. `feat(storage): mount isolated web and image filesystems`
4. `feat(runtime): add command queues and status snapshots`
5. `feat(web): expose a minimal device health endpoint`

### Phase 2 — Display driver, renderer and refresh ownership

需求來源：草案三、4.3、9 DisplayTask、十一的 renderer/BUSY/sleep 驗收、
十二之 3、9–10。

交付物：

- 經 G2/G3 固定的 `epd7in3e` component 與 pin configuration。
- 六色 palette v1、packed 4-bit pixel primitive 與 golden vector tests。
- 橫向 800×440、直向 480×760 renderer，以及邏輯畫面到面板資料的旋轉。
- DisplayTask command/result contract、BUSY timeout、refresh 後 sleep。
- 30 分鐘預設／5 分鐘下限的輪播核心；空圖庫 welcome/status frame。

驗收：

- packed framebuffer 大小與草案一致：全畫面 192,000 bytes、橫向圖片區
  176,000 bytes、直向圖片區 182,400 bytes。
- pattern/golden test 驗證六色、邊界 pixel、直向旋轉與狀態列位置。
- fake panel 驗證只有 DisplayTask 觸發 SPI lifecycle。
- fake/concurrency test 驗證 BUSY timeout 不阻塞 Phase 1 health handler；
  實機經網路存取在 Phase 3 驗證。刷新完成後 panel sleep。

建議 commits：

1. `feat(display): add six-color packed framebuffer primitives`
2. `feat(display): integrate the epd7in3e panel driver`
3. `feat(display): render landscape and portrait frames`
4. `feat(display): serialize refresh commands in DisplayTask`
5. `feat(carousel): schedule full-refresh image rotation`

### Phase 3 — Provisioning, authentication and WebUI shell

需求來源：草案 4.1–4.2、五、六、七的 auth/wifi endpoints、十一的
AP/STA/401/CSRF 驗收、十二之 4–6。

交付物：

- NetworkServiceTask：STA/AP、timeout、重試、SNTP 與 mDNS 狀態。
- AP 顯示 command 與離線 provisioning WebUI；scan 結果去重、排序及過濾。
- AP 畫面 payload 包含 SSID、password、`192.168.4.1`、操作步驟、
  Wi-Fi/WebUI QR code 與裝置尾碼；內容未變時不得重複刷新。
- 至少 8 字元密碼、強雜湊、單一 server-side session、30 分鐘 idle、
  24 小時 absolute expiry 與 CSRF。
- 共用 `style.css`／`ui.js`／favicon、login 與 responsive navigation shell。
- `/api/v1/device`、health、auth、wifi config 與 masked config endpoints；
  route policy 明確區分永久公開、首次 bootstrap 與登入後管理面。
- Dashboard 初版以 runtime snapshot 顯示裝置、網路、SNTP、容量、版本、
  uptime 與可取得的 carousel/display 狀態；後續 phase 增量加入圖片、
  weather 與 sensor 欄位。

驗收：

- 空白 NVS 進入 AP；有效憑證進 STA；錯誤密碼 timeout 後回 AP。
- AP 畫面有 golden payload test；相同內容不刷新。credential 以交易方式
  保存，成功 response 後約 1 秒 reboot，連線失敗可再次進 AP。
- DNS、weather 或 Internet 錯誤只改變 Internet 狀態，不進 AP。
- credential 不出現在 URL、log、response、snapshot 或診斷包。
- `<8` 字元密碼被拒絕；fake-clock 測試涵蓋 30 分鐘 idle 與 24 小時
  absolute expiry；新登入撤銷舊 session。
- 永久公開面只含安全的 device/health、auth status 與 login／首次建密碼。
  尚無管理密碼且位於首次 provisioning AP 時，wifi scan/config 可作為
  bootstrap 例外；完成建密碼後（含 Recovery AP）相同 route 需 session，
  wifi config 亦需 CSRF。
- 其餘未登入的 GET 與 mutation 都回 401；需保護的 POST、PUT、DELETE
  缺少／錯誤 CSRF 時被拒絕。公開 endpoint 只回安全資料。
- AP 無 Internet 時所有 WebUI asset 仍可載入。
- 經 AP 與 STA 實機驗證 health endpoint 在 display command 忙碌或 timeout
  時仍可回應。

建議 commits：

1. `feat(network): add AP and STA connection state machine`
2. `feat(web): add offline provisioning portal`
3. `feat(auth): protect management sessions and mutations`
4. `feat(web): add shared responsive management shell`

### Phase 4 — Browser image pipeline and PFR1 contract

需求來源：草案 4.5–4.6、五的圖片頁、十二之 7。

交付物：

- G4 的 `docs/formats/PFR1.md` 與跨 C++/JavaScript golden vectors。
- 瀏覽器載圖、EXIF orientation、透明白底、鏡像、旋轉、fit/crop、縮放。
- Floyd–Steinberg、Atkinson、Bayer 4×4、nearest-color 六色量化 Web Worker。
- 原圖／處理後／六色／狀態列預覽與輸出尺寸、方向、大小提示。
- JavaScript PFR1 packer 與韌體端 streaming parser/validator。

固定轉換順序：

1. 水平鏡像。
2. 垂直鏡像。
3. 順時針旋轉。
4. 裁切或 fit。
5. 縮放。
6. 六色量化與 dithering。

驗收：

- 同一 golden input 在 browser 與 firmware 得到相同 header、payload、CRC。
- landscape/portrait、透明圖、EXIF、四種 fit 與四種 dithering 有測試。
- 拒絕錯誤 magic/version/orientation/palette/dimensions/length/CRC/filename。
- 瀏覽器只上傳受限制的 packed PFR1，不上傳任意大小 RGB framebuffer。

建議 commits：

1. `docs(format): define the PFR1 binary contract`
2. `feat(image): validate streamed PFR1 payloads`
3. `feat(web): transform and crop source images`
4. `feat(web): quantize previews with a dithering worker`
5. `feat(web): pack PFR1 images from the browser`

### Phase 5 — Transactional image storage, catalog and library

需求來源：草案 4.6–4.7、七的 image endpoints、八的 imagefs/catalog、
十一的斷電/CRC/507/目前圖片驗收、十二之 8。

交付物：

- 在第一個可保存使用者圖片的 build 前凍結 G5 發行 partition layout，
  記錄 checksum 與後續 migration 規則。
- StorageWorker streaming upload 與容量預檢。
- `.part`／`.bak` transaction、開機 recovery 與 catalog atomic update。
- Catalog schema：名稱、時間、方向、大小、enabled/current/corrupt 狀態與順序。
- 圖片 list/upload/delete/activate/advance/order API。
- 受保護的 `GET /api/v1/images/{name}/download`，以固定 PFR1 MIME、
  安全的 `Content-Disposition` 與檔名回傳已處理圖片。
- 圖片庫 UI、storage usage、排序、批次刪除與 processed file download；目前完成
  catalog 顯示、processed file download、PFR1 upload、delete／activate／排序 UI、
  async API 與 carousel runtime 讀圖／橫直向 framebuffer 組合已完成。

驗收：

- fault injection 涵蓋每個 rename/catalog 邊界；重開後只保留舊或新完整版本。
- 發行 partition table 與 G5 記錄一致，升級測試證明不會重格式化或位移
  `imagefs`。
- CRC 錯誤不覆寫既有圖片；空間不足回一致的 HTTP 507 error contract。
- 所有 image route（含 download）都需登入；download 不得由未受保護的
  imagefs 靜態路徑繞過 session。
- 刪除 current image 時先更新 catalog/runtime，再刪檔並選下一張有效圖片。
- 損毀／遺失圖自 playlist 排除，新圖及手動啟用圖至少完整顯示一個週期。

建議 commits：

1. `build: freeze the image-preserving partition layout`
2. `feat(storage): persist images transactionally`
3. `feat(storage): recover interrupted image transactions`
4. `feat(images): maintain a durable image catalog`
5. `feat(api): manage images and carousel order`
6. `feat(web): add the image library workflow`

### Phase 6 — Weather, time and status rendering

需求來源：草案 4.3–4.4、七的 weather endpoint、九 WeatherWorker、
十一的 weather offline 驗收、十二之 11。

交付物：

- WeatherWorker HTTPS client、G7 response parser、cache schema 與退避。
- API key masked config、test endpoint、最後成功時間與 stale 狀態。
- SNTP 未同步、同步中、有效時間與時區處理。
- 日期、星期、weather icon/temperature/stale 標記的狀態列 renderer。
- 顯示 deadline 提前約實測 refresh duration，完成時間貼近排程邊界。
- Dashboard 增量顯示 weather cache、Internet 與 SNTP 狀態。

驗收：

- API key invalid、DNS/TLS/timeout、HTTP error、parse error 有不同診斷狀態。
- 失敗保留最後有效 cache；完全無 cache 顯示 `--°` 與離線狀態。
- weather cache 更新不直接刷新面板，只在下一個 display command 使用。
- 未同步時間不產生 1970 或其他假日期。

建議 commits：

1. `feat(weather): cache OpenWeatherMap observations`
2. `feat(time): track SNTP and timezone state`
3. `feat(display): render weather and time status`
4. `feat(web): configure and test weather access`

### Phase 7 — Optional sensors and presence behavior

需求來源：草案 4.8–4.9、五的環境頁、七 sensors endpoint、九 SensorTask、
十一的 sensor/presence 驗收、十二之 12–13。

交付物：

- EnvironmentSensor interface 與 null driver（`LightSensor` 抽象介面／
  `NullLightSensor` 已於 2026-08 移除：production 從未透過它讀值，
  `SensorTask::sample_light()` 直接呼叫 `adc_oneshot_read()`，是零實作的
  死抽象層；`LightSensorStatus`／`MovingAverageFilter` 不受影響，仍在用）。
- DHT22 driver 的讀取頻率、range validation、stale cache 與 backoff。
- ADC sampling/filter、threshold、saturated/error 判斷與可調防抖。
- `UNKNOWN`／`PRESENT`／`AWAY` state machine。
- 離席只白屏一次並 sleep；返回立即重繪且重設輪播 deadline。
- 環境 API/UI：driver、GPIO、reading、stale、ADC、threshold、presence。
- Dashboard 增量顯示環境、light sensor 與 presence 狀態。

驗收：

- 未安裝／未啟用時裝置正常輪播；API 缺值為 `null`，presence 為 `UNKNOWN`。
- 浮動、saturated 或 error ADC 不得觸發離席。
- 只有條件連續滿足 180 秒／30 秒才切換 AWAY／PRESENT。
- task 重啟或漏失 transition 後，輸出仍依穩定狀態收斂到正確 panel 狀態。

建議 commits：

1. `feat(sensors): add optional environment sensor contracts`
2. `feat(sensors): sample and classify ambient light`
3. `feat(presence): debounce away and return states`
4. `feat(display): clear and restore frames for presence`
5. `feat(web): expose sensor and presence status`

### Phase 8 — Diagnostics, OTA hardening and MVP acceptance

需求來源：草案五的系統頁、七 system endpoints、八、十、十一、
十二之 14。

交付物：

- 結構化 ring-buffer events 與 masked diagnostics export。
- `/api/v1/status`、events、reboot、Recovery AP；OTA 僅能使用 Phase 5
  前已凍結的 G5 partition layout，不在本階段重新分割既有裝置。
- 系統頁：BUSY timeout、Wi-Fi reconnect、weather/sensor error、容量、版本、
  uptime、最近 refresh result。
- 完成 Dashboard 的目前圖片、下次輪播、network/SNTP、weather、display、
  sensors、Flash/PSRAM/imagefs、版本與 uptime 欄位。
- watchdog、queue saturation、lock timeout 與 reboot reason 可觀測性。
- 完整 MVP test matrix、實機 soak/power-loss 測試與 release checklist。

驗收：

- 診斷資料不含 secrets、session token、Wi-Fi password 或完整 API key。
- OTA A/B 可回滾，且不改寫 `imagefs`；WebUI 更新只改寫 `webfs`。
- 面板 timeout、weather failure、sensor failure 不影響 HTTP 管理介面。
- Dashboard 每個欄位來自同一 runtime snapshot，缺少可選資料時顯示明確
  unknown/stale 狀態，不偽造數值。
- 草案第十一節每個情境都有自動測試或可重現實機測試證據。
- 重新啟動保留設定、圖片、順序與目前圖片。

建議 commits：

1. `feat(diagnostics): expose bounded runtime events`
2. `feat(system): add protected recovery controls`
3. `feat(ota): update firmware without touching images`
4. `test: cover the PaperFrame MVP acceptance matrix`
5. `docs: add hardware validation and release checklist`

## 5. MVP 驗收追蹤

| 草案第十一節情境 | 主要 Phase | 最低證據 |
| --- | --- | --- |
| NVS 無 Wi-Fi／AP 掃描／儲存／密碼錯誤 | 3 | on-device integration |
| OpenWeatherMap 離線 | 6 | host fake + on-device network fault |
| 溫溼度未安裝 | 7 | host null driver + on-device boot |
| 光敏未安裝 | 7 | host state machine + floating ADC check |
| 離席／返回防抖 | 7 | deterministic fake-clock tests |
| 離席白屏 sleep／返回重繪 | 7 | fake panel + on-device display |
| 橫向／直向圖片 | 2、4 | golden vectors + panel patterns |
| 上傳斷電復原／CRC／空間不足 | 5 | filesystem fault injection |
| BUSY timeout | 2、8 | fake timeout + on-device forced BUSY |
| 新圖片手動啟用 | 5 | carousel fake-clock test |
| 未登入／缺少 CSRF | 3 | HTTP integration tests |
| 刷新後 sleep | 2 | driver trace + power/current observation |
| reboot persistence | 1、5、8 | repeated on-device reboot test |

## 6. 里程碑與停止條件

- M0：Phase 0；需求、工作規則與交付路徑可追溯。
- M1：Phase 1–2；韌體可 build、boot 並安全控制面板。
- M2：Phase 3；可離線配網與安全管理。
- M3：Phase 4–5；圖片可在 browser 轉換並可靠保存、輪播。
- M4：Phase 6–7；weather 與可選感測器完成降級路徑。
- MVP：Phase 8；完整驗收矩陣有證據且無阻擋 finding。

遇到以下情況停止擴張 scope，回到需求／ADR：

- 精確 board、pin map、driver license 或 PFR1 contract 未定。
- partition 變更可能清除既有 imagefs。
- 需要 HTTP handler 直接操作硬體才能完成設計。
- 測試要求依賴真實秘密、外部 CDN 或不可重現的手動狀態。
- 同一失敗在相近條件重現兩次而沒有新證據。

## 7. 目前 checkpoint

- [x] 原始 `Guild.md` 納入版本控制。
- [x] 初始化 `main` branch。
- [x] 建立專案級 `AGENTS.md`。
- [x] 建立分階段 MVP 計畫與 commit 邊界。
- [x] 保留 `Guild.md` 作為原始匯入快照；其跳脫 Markdown 不在初始化時
  原地改寫，若需可讀版將另建衍生需求文件。
- [x] 補齊 Phase 0 的 README、reference provenance、license 決策與 `.gitignore`。
- [x] 完成 G1 board 決策與 Phase 1 build、host／embedded tests、實機
  boot／mount 驗證。
- [x] 完成 G2 pin map 與 G3 display driver 決策後開始 Phase 2。
- [x] `epd7in3e` driver 通過 host/build 與實機六色 pattern 驗證；refresh
  時間、panel sleep 電流與 forced-BUSY 隔離治具仍列為硬體待驗證項。
- [x] 完成 Phase 2 renderer、DisplayTask owner contract、30 分鐘／5 分鐘
  輪播核心與空圖庫 welcome frame；catalog-backed 圖片載入、離線輪播與
  橫直向 PFR1 framebuffer 組合已在 Phase 5 接入。
- [ ] Phase 3：AP／STA 純狀態機、provisioning portal、credential transaction、
  auth/CSRF 與 WebUI shell 已完成程式、host test、build、STA 啟動 smoke，
  以及正常 STA 登入後 Dashboard、Wi-Fi scan 與桌面版寬度驗證；最新
  artifact 的 blank-NVS／Recovery AP 瀏覽器流程、SNTP/mDNS 仍待實機收尾，
  故尚未標記 M2 完成。
- [ ] Phase 4：PFR1 contract、韌體 validator、browser image pipeline、
  quantizer、packer 與 host tests 已完成；可正常選檔的 browser 實機圖片
  產出／下載仍待補驗，未把 `image_02_05.png` 納入版本控制。
- [x] Phase 5：partition layout、catalog、transaction upload、boot recovery
  與 `StorageWorker::start()` 的 imagefs 啟動接線已完成並通過 host／firmware
  build；受保護的 image list/upload API、serializer、PFR1 download route、圖片庫
  catalog/download/upload/delete/activate/order UI、async API 與 carousel runtime
  讀圖／面板提交已完成。離線輪播不依賴 Wi-Fi；硬體長時間輪播與斷電後實機驗證
  仍保留在後續 acceptance。
  `image_02_05.png` 僅作本地測試，不得 commit。
- [ ] Phase 6：G7 已由 `docs/adr/0005-weather-worker-and-status-bar.md` 固定。
  程式已完成：`pf_weather` parser/cache 與設定持久化／masked config API
  （`af4a2c5`、`56d5644`、`14d787f`）；NetworkServiceTask 最小 SNTP 啟動與
  `RuntimeSnapshot.time_sync`；`pf_weather_worker`（2026-08 已併入
  `pf_weather`，見下方 Phase 7 之後的過度設計整併記錄）（`esp_http_client` +
  `esp_crt_bundle_attach` 實際 HTTPS 抓取、interval-aware 排程、
  `report_internet_state` 回報）；`RuntimeSnapshot`／`RuntimeCoordinator`
  天氣欄位；Dashboard `weather` JSON 三態（available/stale/unavailable）；
  `pf_display` 自製 bitmap font／9 種天氣圖示／狀態列 renderer；已接線進
  carousel 圖片幀與 welcome frame。`pio run` 與 `pio test -e native`
  （192/192）全綠，`test_runtime_coordinator`／`test_display_task`
  embedded test 通過 build-only 驗證。**尚未做任何實機驗證**：SNTP 實機
  同步、WeatherWorker 四種 HTTPS 診斷狀態（含 TLS 憑證驗證）、
  Internet 可達性訊號、狀態列在真實面板上的視覺結果，全部列在
  `docs/hardware/VALIDATION.md` 2026-07-31 待驗證清單。
- [ ] Phase 7：DHT22 driver 來源、讀取頻率/範圍/backoff、光敏濾波方式、
  presence debounce 機制、`CarouselScheduler` 缺口方案與 sensors API
  schema 已由 `docs/adr/0006-sensor-drivers-and-presence.md` 固定。
  程式已完成：`pf_sensors` 純邏輯 component（`EnvironmentCache`／
  `DailyStats`／`MovingAverageFilter`／`PresenceTracker`）；
  `pf_config::SensorSettings` 持久化；`RuntimeSnapshot`／
  `RuntimeCoordinator` 感測器欄位；移植自 `UncleRus/esp-idf-lib`
  （commit `162af418d4702791fd3bf3e5d1577aea9ec5539c`，BSD-3-Clause）的
  `pf_dht22` driver；`pf_sensor_task`（DHT22 週期讀取＋ADC 光敏取樣＋
  presence debounce，接線進 `app_main.cpp`）——`pf_dht22`／`pf_sensor_task`
  皆已於 2026-08 併入 `pf_sensors`，見下方過度設計整併記錄；`CarouselScheduler::
  force_immediate` 與 `render_blank_frame`，離席時暫停輪播、返回時立即
  重繪；Dashboard `sensors` JSON 三態；`GET /api/v1/sensors`、
  `/api/v1/sensors/config` API 與 WebUI 環境頁。`pio run` 與
  `pio test -e native`（225/225）全綠，`node --check data/web/ui.js`
  通過。**尚未做任何實機驗證**：DHT22 實際讀值、光敏 ADC 實測與
  threshold 校正、AWAY/PRESENT 實機轉換、離席全白刷新＋sleep 電流、
  返回重繪與 deadline 重設、WebUI 環境頁瀏覽器行為，全部列在
  `docs/hardware/VALIDATION.md` 2026-07-31 Phase 7 待驗證清單。
- [x] 2026-08 過度設計整併（下一輪，網路/安全性之後）：`pf_dht22`＋
  `pf_sensor_task` 併入 `pf_sensors`，`pf_weather_worker` 併入 `pf_weather`，
  namespace 統一；刪除死抽象層 `pf_sensors::LightSensor`／
  `NullLightSensor`（production 從未透過它讀值，直接呼叫
  `adc_oneshot_read()`）。純重構，不改變執行期行為。元件數 14→11。
  `pf_carousel`／`pf_image` 折疊評估後判定會製造循環依賴，本輪維持現狀
  （見 `docs/hardware/VALIDATION.md` 對應記錄）。`pio run` 與
  `pio test -e native`（226/226）全綠，codex-cowork 審查通過。
