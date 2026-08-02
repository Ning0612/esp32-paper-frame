# 管理 WebUI 與 Dashboard

管理介面位於 `data/web/`，所有 HTML、CSS、JavaScript 與 favicon都寫入
`webfs`，不依賴外部 CDN。登入後提供共用的 responsive 導覽殼層，開放上方
導覽的「總覽」、「Wi‑Fi」、「天氣」、圖片處理／圖片庫、「環境」與
「系統」view。圖片在瀏覽器本機處理成 PFR1 後，可由登入且帶 CSRF
的請求非同步上傳到裝置 imagefs。

「系統」view（Phase 8）顯示面板/網路/reboot reason/容量/版本/uptime/OTA
狀態，並提供四個系統操作：重新啟動裝置、重設管理密碼、檢查 GitHub
Releases 更新、立即下載並安裝更新。其中重新啟動、重設密碼與立即更新
需要登入 + CSRF；OTA 檢查是已登入的唯讀操作。需要 mutation 的操作皆有
`window.confirm()` 或表單確認。OTA 只更新 app 韌體
（`ota_0`/`ota_1`），webfs 更新仍是獨立的手動流程，見本檔「更新
方式」一節；OTA 決策見
[`docs/adr/0008-ota-github-releases-and-rollback.md`](adr/0008-ota-github-releases-and-rollback.md)。

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
| `POST /api/v1/auth/login` | 登入／首次建密碼 | 非同步提交 PBKDF 驗證，不把密碼放進 URL 或 response |
| `POST /api/v1/auth/password` | 已登入 + CSRF | 以兩個 form 欄位驗證並重設管理密碼；成功後撤銷 session |
| `GET /api/v1/auth/login/status` | 登入流程 token | 取回一次性登入結果 |
| `POST /api/v1/auth/logout` | 已登入 + CSRF | 撤銷目前 session |
| `GET /api/v1/status` | 已登入 | 完整初版 runtime snapshot、容量與尚未提供功能的 `null` 狀態 |
| `GET /api/v1/config` | 已登入 | 遮蔽後設定；秘密只回傳 `*_set` 布林值 |
| `POST /api/v1/config` | 已登入 + CSRF | 以 `random=true|false` 非同步保存隨機輪播設定 |
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
| `GET /api/v1/events` | 已登入 | 讀取 diagnostics ring buffer（`?since=<sequence_id>` 分頁；`since` 存在但無效回 400） |
| `POST /api/v1/system/reboot` | 已登入 + CSRF | 排程約 500ms 後重開機（`schedule_reboot()`），成功才回 `{"ok":true}` |
| `GET /api/v1/system/ota/status` | 已登入 | 讀取 OTA 檢查／更新狀態、進度、最後錯誤 |
| `POST /api/v1/system/ota/check` | 已登入 | 觸發 GitHub Releases 版本檢查（唯讀，不寫 flash） |
| `POST /api/v1/system/ota/update` | 已登入 + CSRF | 觸發下載並寫入韌體；已在進行中回 `409 Conflict` |

所有 JSON 使用 `{ "ok": true, "data": ... }` 或 `{ "ok": false,
"error": ... }`，並設定 `Cache-Control: no-store`、`nosniff` 與同源 CSP。
`/api/v1/status` 的檔案容量與服務狀態來自同一份 RuntimeCoordinator
snapshot；handler 不等待顯示器、網路、NVS 或 filesystem。

缺少的可選資料一律以 `null` 或明確的 `unavailable`／`unknown` 表示，不填入
零值或歷史資料：光敏電阻與溫溼度感測器未安裝時 Dashboard 維持「未安裝／
未知」狀態；`current_image`/`next_refresh_ms` 在裝置從未成功輪播過（開機
後尚未完成任何一次刷新）時回 `null`，成功輪播後才是真實圖片 id 與距下次
刷新的毫秒數。

圖片頁的 `GET /api/v1/config` 會在 `data.display.random` 回傳目前輪播模式；
`POST /api/v1/config` 的寫入在 deferred worker 執行，完成 NVS 保存後才向
RuntimeCoordinator 發出模式變更請求，carousel 正在刷新時會等安全時機套用。

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

圖片庫的「Random／隨機輪播」可由圖片頁開關，設定保存於裝置並在安全時機套用到
carousel scheduler；關閉時依圖片庫排序輪播。

`data/web/image_pfr1.js` 將固定 profile 的 quantized result 打包成
`application/vnd.paperframe.pfr1`，重用同一組 filename、flags、dithering、
little-endian 與 CRC32 契約；瀏覽器只會產生受尺寸與 palette 限制的 packed
payload。跨語言 golden vector 與欄位定義見
[`docs/formats/PFR1.md`](formats/PFR1.md)。

打包完 nibble payload 後，`packPfr1()`（已改為 `async`）會嘗試用瀏覽器原生
`CompressionStream('deflate-raw')` 壓縮：壓縮後比未壓縮小才採用（設定
`Pfr1Flags.compressed` bit 並改存壓縮後 bytes），壓縮沒幫助、或執行環境
沒有 `CompressionStream`（例如較舊的瀏覽器）時，直接退回今天的未壓縮輸出
——保證新路徑不會比原本差。這個步驟刻意留在主執行緒、不搬進
`image_quantize_worker.js`：payload 最大 182 KB，`CompressionStream` 壓縮
是個位數 ms 等級，不值得為此擴充 worker 的訊息協定；打包（`packPfr1`）
本身今天也是主執行緒同步呼叫，壓縮只是延續同一個階段。呼叫端如果需要
強制輸出未壓縮版本（例如重現文件中的 golden vector），可以傳
`{ compress: false }`。詳細的壓縮 payload 語意（`payload_length` 改為壓縮
後 byte 數、CRC32 涵蓋範圍不變）見 [`docs/formats/PFR1.md`](formats/PFR1.md)
的「Payload 壓縮」段落。

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

## 更新方式

韌體 app-only upload 不會更新 WebUI。修改 `data/web/` 後，依
[ESP32-S3 燒錄操作](hardware/FLASHING.md) 的 WebUI-only 流程建置並只寫入
`0x510000` 的 `webfs` partition；不可把 `imagefs.bin` 放入同一命令。
