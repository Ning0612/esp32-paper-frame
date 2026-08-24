# 管理 WebUI 與 Dashboard

管理介面的來源位於 `data/web/`，所有 HTML、CSS、JavaScript 與 favicon
在 build 時 gzip 後編入 app image，不依賴外部 CDN。登入後提供共用的 responsive 導覽殼層，開放上方
導覽的「總覽」、「Wi‑Fi」、「天氣」、圖片處理／圖片庫、「環境」與
「系統」view。圖片在瀏覽器本機處理成 PFR1 後，可由登入且帶 CSRF
的請求非同步上傳到裝置 imagefs。

「系統」view（Phase 8）顯示面板/網路/reboot reason/容量/版本/uptime/OTA
狀態，並提供四個系統操作：重新啟動裝置、重設管理密碼、檢查 GitHub
Releases 更新、立即下載並安裝更新。其中重新啟動、重設密碼與立即更新
需要登入 + CSRF；OTA 檢查是已登入的唯讀操作。需要 mutation 的操作皆有
`window.confirm()` 或表單確認。OTA 寫入 app slot（`ota_0`/`ota_1`），
而 WebUI 就在 app image 裡，所以一次 OTA 會同時更新韌體與前端，
前端不可能落後於後端；OTA 決策見
[`docs/adr/0008-ota-github-releases-and-rollback.md`](adr/0008-ota-github-releases-and-rollback.md) 與
[`docs/adr/0016-embed-webui-assets-in-firmware.md`](adr/0016-embed-webui-assets-in-firmware.md)。

**已移除**：原本規劃「強制進入配網 AP」（管理員在 STA 已連線時手動切回
provisioning AP）於 2026-08-01 實機測試時發現會在 AP+STA combo 模式
啟動瞬間讓 Espressif 閉源 WiFi blob 崩潰（DMA-capable heap 實測只剩
數 KB，遠低於安全水位），且並未對應真正的使用情境——WiFi 連不上時
既有的自動 fallback AP（Phase 3）已經涵蓋這個需求。已完整移除對應
API route、handler、access policy 與 WebUI 按鈕，細節見
`docs/hardware/VALIDATION.md` 2026-08-01 段落。

## API 路由

| 路由 | 存取 | 用途 |
| --- | --- | --- |
| `GET /api/v1/device` | 永久公開 | 固定產品／型號、API 版本、面板尺寸與安全容量資訊 |
| `GET /api/v1/health` | 永久公開 | 最小健康狀態；只讀 runtime snapshot |
| `GET /api/v1/auth/status` | 永久公開 | 管理密碼是否設定、目前 session 與 CSRF 狀態 |
| `POST /api/v1/auth/login` | 登入／首次建密碼 | 同步完成 PBKDF 驗證，不把密碼放進 URL 或 response |
| `POST /api/v1/auth/password` | 已登入 + CSRF | 以兩個 form 欄位驗證並重設管理密碼；成功後撤銷 session |
| `POST /api/v1/auth/logout` | 已登入 + CSRF | 撤銷目前 session |
| `GET /api/v1/status` | 已登入 | 完整初版 runtime snapshot、容量與尚未提供功能的 `null` 狀態 |
| `GET /api/v1/config` | 已登入 | 遮蔽後設定；秘密只回傳 `*_set` 布林值 |
| `POST /api/v1/config` | 已登入 + CSRF | 以 `random=true|false&refresh_minutes=10..1440` 非同步保存輪播模式與間隔 |
| `GET /api/v1/weather/config` | 已登入 | 天氣／NTP 設定；API key 只回傳 `api_key_set` |
| `POST /api/v1/weather/config` | 已登入 + CSRF | 以 form body 保存天氣／NTP 設定 |
| `GET /api/v1/wifi/scan` | 首次 provisioning AP 或已登入 | 掃描結果 |
| `POST /api/v1/wifi/config` | 首次 provisioning AP，或已登入 + CSRF | 交易式保存 Wi‑Fi 憑證 |
| `GET /api/v1/images` | 已登入 | 讀取目前 catalog；只回傳安全 metadata |
| `POST /api/v1/images` | 已登入 + CSRF | 非同步驗證並交易式保存 PFR1 |
| `POST /api/v1/images/{name}/activate` | 已登入 + CSRF | 非同步將圖片設為目前圖片 |
| `DELETE /api/v1/images/{name}` | 已登入 + CSRF | 非同步刪除圖片並選擇下一張有效圖片 |
| `PUT /api/v1/images/order` | 已登入 + CSRF | 非同步保存 `{ "ids": [ ... ] }` 輪播順序 |
| `GET /api/v1/images/{name}/download` | 已登入 | 下載已驗證的 PFR1 |
| `GET /api/v1/sensors` | 已登入 | 溫溼度與**兩個光敏通道**的即時讀值；每通道各自回報 `gpio`／`status`／`raw`／`threshold`，另有合併後的 `status`／`raw`／`threshold` 與 `deciding_channel`（見 [ADR-0018](adr/0018-dual-photoresistor-channels.md)） |
| `GET /api/v1/sensors/config` | 已登入 | 感測器設定；兩個光敏通道各自的 `lightN_enabled`／`lightN_threshold` |
| `POST /api/v1/sensors/config` | 已登入 + CSRF | 非同步保存感測器設定 |
| `GET /api/v1/events` | 已登入 | 讀取 diagnostics ring buffer（`?since=<sequence_id>` 分頁；`since` 存在但無效回 400） |
| `POST /api/v1/system/reboot` | 已登入 + CSRF | 排程約 500ms 後重開機（`schedule_reboot()`），成功才回 `{"ok":true}`；面板刷新進行中會延後，見下 |
| `GET /api/v1/system/ota/status` | 已登入 | 讀取 OTA 檢查／更新狀態、進度、最後錯誤 |
| `POST /api/v1/system/ota/check` | 已登入 | 觸發 GitHub Releases 版本檢查（唯讀，不寫 flash） |
| `POST /api/v1/system/ota/update` | 已登入 + CSRF | 觸發下載並寫入韌體；已在進行中回 `409 Conflict` |

**環境頁的即時讀值會自動更新**：停留在該頁時每 3 秒抓一次
`GET /api/v1/sensors`，離開該頁或 session 過期（401）就停止，分頁切到背景時
暫停送出但保留計時器。3 秒略高於裝置端 2 秒的取樣週期。**只輪詢讀值、不輪詢
設定**——重抓設定會覆蓋 threshold／持續時間輸入框，把使用者正在輸入的內容洗掉。
這個間隔對光敏校正是必要的：裝置每 2 秒取樣一次並經過 8 筆移動平均，光照改變
後約 16 秒才收斂，靠切頁取得的單次快照無法判斷閾值該設在哪。

Dashboard 與其他頁面**不**自動輪詢，維持既有的「切頁或按重新整理才抓」行為。

**重開機會等待進行中的面板刷新**（管理員觸發與 OTA 完成後的重開機共用
`schedule_reboot()`，兩者行為一致）：計時器到期時若 snapshot 顯示
`display=refreshing`、有 active request 或仍有排隊的刷新，就每 500 ms 重試一次，
直到面板閒置為止。因此 `{"ok":true}` 之後裝置**不一定**在 500 ms 內斷線——一次
完整刷新約 31 秒，實測從觸發到斷線約 36 秒。最壞情況是初始的 500 ms 再加上
`pf_runtime::kMaxRebootDeferrals`（90）次 500 ms 重試，**約 45.5 秒**，逾時仍會
無條件重開，避免卡死的面板讓裝置無法重啟。UI 或腳本若要等待裝置下線，逾時值
必須大於 45.5 秒。

所有 JSON 使用 `{ "ok": true, "data": ... }` 或 `{ "ok": false,
"error": ... }`，並設定 `Cache-Control: no-store`、`nosniff` 與同源 CSP。
`/api/v1/status` 的檔案容量與服務狀態來自同一份 RuntimeCoordinator
snapshot；handler 不等待顯示器、網路、NVS 或 filesystem。

缺少的可選資料一律以 `null` 或明確的 `unavailable`／`unknown` 表示，不填入
零值或歷史資料：光敏電阻與溫溼度感測器未安裝時 Dashboard 維持「未安裝／
未知」狀態；`current_image`/`next_refresh_ms` 在裝置從未成功輪播過（開機
後尚未完成任何一次刷新）時回 `null`，成功輪播後才是真實圖片 id 與距下次
刷新的毫秒數。

圖片頁的 `GET /api/v1/config` 會在 `data.display.random` 回傳目前輪播模式，並在
`data.display.refresh_minutes` 回傳目前間隔（10–1440 分鐘，預設 30 分鐘）；
`POST /api/v1/config` 的寫入在 deferred worker 執行，完成 NVS 保存後才向
RuntimeCoordinator 發出模式與間隔變更請求，carousel 正在刷新時會等安全時機套用。

## Phase 4 圖片處理管線

圖片頁的來源圖片除了檔案選擇按鈕，也可直接拖曳圖片到來源圖片拖放區；兩者會共用相同的
檔案大小、像素上限、EXIF 與瀏覽器解碼流程。

`data/web/image_pipeline.js` 是離線可載入、也可由 Node host test 驗證的純
RGBA raster helper。`processRaster()` 固定依序正規化 EXIF orientation 1–8、
水平鏡像、垂直鏡像、順時針 90°、fit/crop 與 nearest-neighbor resize；透明
像素先以白色背景合成。四種 fit 語意如下：

- `contain`：等比縮放並在目標畫布留白。
- `cover`：等比放大到覆蓋目標後裁切；處理後預覽可拖曳影像調整位置，並以裁切縮放滑桿放大目標。
- `fill`：直接縮放到目標尺寸，不保持比例。
- `crop`：先以目標比例裁切原圖，再等比縮放；預設置中，可拖曳調整位置並以裁切縮放滑桿放大目標。

Crop 與 Cover 的裁切縮放範圍為 100%–300%；100% 代表最大的裁切範圍。滑鼠或觸控拖曳
處理後預覽可調整裁切 anchor，縮放與位置都會即時更新預覽，放開縮放控制後才重新量化與打包。

輸出目標必須由 landscape `800×440` 或 portrait `480×760` profile 指定；
PFR1 pack 與圖片頁會在後續 Phase 4 commit 接入。Node 驗證命令：

```powershell
node test\web\test_image_pipeline.mjs
node --check data\web\image_pipeline.js
```

`data/web/image_quantizer.js` 使用相同的 E6 native palette，提供
`floyd-steinberg` 與 `atkinson` 兩種 deterministic mode；輸出
同時包含 preview RGBA 與 native palette index。`image_quantize_worker.js`
透過 transferable `ArrayBuffer` 執行量化，主執行緒不會因大型 raster 計算而
卡住；worker 失敗只回傳錯誤訊息，不會將原始 RGB buffer 直接送到韌體。

圖片頁接受來源檔案最多 32 MB、最多 6,400 萬像素；超過面板輸出所需的來源
尺寸仍由瀏覽器在本機縮放，處理過程不會把原始 RGB framebuffer 上傳到裝置。

圖片庫的「Random／隨機輪播」與「輪播間隔」可由圖片頁設定；間隔限制為 10 分鐘至
24 小時，設定保存於裝置並在安全時機套用到 carousel scheduler；關閉隨機時依圖片庫
排序輪播。

**新間隔從下一輪開始生效**：`CarouselScheduler::configure()` 只更新間隔本身，
不會重算已經排定的 `next_due_ms`，也不會在刷新進行中套用（`in_flight` 時直接
拒絕）。把 30 分鐘改成 10 分鐘後，仍會先等完當前那一輪剩下的時間，之後才改用
10 分鐘。這是刻意的：重算 deadline 等於讓「改設定」變成「立刻刷新面板」，而完整
刷新要 31 秒並消耗面板壽命。要立即換圖請用圖片庫的「設為目前圖片」，那條路徑走
`force_immediate()`。

`data/web/image_pfr1.js` 將固定 profile 的 quantized result 打包成
`application/vnd.paperframe.pfr1`，重用同一組 filename、flags、dithering、
little-endian 與 CRC32 契約；瀏覽器只會產生受尺寸與 palette 限制的 packed
payload。所有欄位、壓縮、長度與 CRC 語意以
[`docs/formats/PFR1.md`](formats/PFR1.md) 為準。

`packPfr1()` 會在瀏覽器支援且壓縮後確實較小時使用 raw DEFLATE，否則輸出
未壓縮 payload；完整 byte-level 語意與 golden vectors 只在上述格式文件維護。

量化測試與 worker 語法檢查：

```powershell
node test\web\test_image_quantizer.mjs
node --check data\web\image_quantizer.js
node --check data\web\image_quantize_worker.js
node test\web\test_image_ui_contract.mjs
node test\web\test_pfr1_packer.mjs
node --check data\web\image_pfr1.js
```

## 視覺與主題

- 米白網格背景、Teal／Coral／Mint、方角元件與硬陰影。
- 標題使用 Georgia／Noto Serif TC；狀態與控制項使用 Consolas 等寬字體。
- LIGHT／DARK 切換使用共用 localStorage key `iot-ui-theme`。
- 主內容最大寬度 1040px；窄螢幕改為上方導覽及單欄卡片。

## 語言切換

- WebUI 支援繁體中文（`zh-Hant`，預設）與英文（`en`），純前端切換，後端 API
  不涉入——`GET /api/v1/sensors` 等端點回傳的一律是語意化 enum 字串（如
  `"online"`／`"saturated"`），從來不是人話文字，翻譯全部發生在瀏覽器端。
- 字典與套用邏輯集中在 `data/web/i18n.js`（獨立的內嵌 asset，比照 `ui.js`／
  `style.css` 走 `StaticAsset`＋route 的三步驟接線），暴露
  `window.PaperFrameI18n = { t, setLang, getLang, applyI18n, STORAGE_KEY }`。
  `<script src="/i18n.js" defer>` 必須排在 `<script src="/ui.js" defer>`
  之前，讓字典在 `ui.js` 的 IIFE 開始執行前就緒。
- 語言切換使用共用 pattern 的 localStorage key `iot-ui-lang`（比照
  `iot-ui-theme`），`<html>` 的 `data-lang` 屬性驅動任何未來的 CSS hook，
  `lang` 屬性同步更新供無障礙工具讀取。
- **切換即整頁 `location.reload()`**，不做原地重新渲染——`ui.js` 本來就在
  密碼重設成功、登出這類影響更小的狀態變化上用 reload，語言切換沿用同一
  慣例最省事也最不容易漏改。`applyI18n()` 只在開機時（或明確傳入某個
  `root` 時）安全，之後若 `ui.js` 已經把即時資料寫進某個 `[data-i18n]`
  元素，重跑 `applyI18n()` 會把它洗回字典裡的 fallback 文字——這正是靠
  reload 而非原地重繪來規避的情境。
- 靜態 HTML 用 `data-i18n`／`data-i18n-aria-label`／`data-i18n-placeholder`／
  `data-i18n-title` 屬性標記，屬性擁有的元素內容本身留繁體中文原文，
  當作沒有 JS 時的 fallback。動態字串在 `ui.js` 裡改用 `t("key", vars)`
  查表，key 一律是 static string literal（即使用三元運算子選 key，兩個
  分支也都要是字面值），這是 `test/web/test_i18n_contract.mjs` 能用
  regex 靜態掃出所有呼叫點、比對字典是否有缺漏的前提。
- 新增或修改可翻譯文字時，`zh-Hant`／`en` 兩張表必須同步更新；
  `node test/web/test_i18n_contract.mjs` 驗證兩表 key 完全對稱、
  `index.html` 的 `data-i18n*` 參照與 `ui.js` 的 `t()` 呼叫全部能在
  字典裡找到對應項。

## 最低 accessibility 驗收

- 所有表單欄位、錯誤訊息與狀態更新都有可見的 label 或等價語意。
- 登入、設定、上傳、刪除與確認操作可只用鍵盤完成，focus 狀態清楚可見。
- 拖曳圖片與裁切操作都有檔案選擇或鍵盤可用的替代入口，不依賴 pointer-only。
- 文字、控制項與錯誤狀態在 LIGHT／DARK 主題中保持可讀對比；動畫不得是唯一狀態提示。

## 更新方式

WebUI 沒有獨立的更新流程。`data/web/` 的內容在 build 時 gzip 後編入 app
image，因此一般 app upload 或一次 OTA 就會同時更新韌體與前端，見
[ADR-0016](adr/0016-embed-webui-assets-in-firmware.md)。改完前端只要重新
build 並上傳韌體即可，不需要任何額外的 filesystem 燒錄步驟。

