# Wi-Fi provisioning contract

## 模式與存取邊界

PaperFrame 將「首次設定」與「Recovery AP」視為不同的信任邊界：

| 狀態 | Wi-Fi scan | Wi-Fi config |
| --- | --- | --- |
| 首次 provisioning AP、尚無 Wi-Fi credential、尚無管理密碼 | bootstrap 例外可用 | bootstrap 例外可用 |
| 升級後已有 credential、尚無管理密碼的 password bootstrap AP | 建立管理密碼並取得 session 後可用 | 建立管理密碼並取得 session／CSRF 後可用 |
| Recovery AP | 需有效管理 session | 需有效管理 session 與 CSRF |
| STA／一般模式 | 需有效管理 session | 需有效管理 session 與 CSRF |

判斷首次 bootstrap 時，credential 或管理密碼狀態只要讀取失敗就 fail
closed，不會把錯誤當成「尚未設定」。管理 session、CSRF、首次建密碼與
Recovery AP 登入流程見
[管理認證與 CSRF contract](AUTHENTICATION.md)。

## AP 與電子紙畫面

- SSID 為 `PaperFrame-Setup-XXXX`，尾碼來自裝置識別碼後四碼。
- AP password 每次開機以硬體 entropy 產生，格式為 `PF-` 加 12 個不易混淆
  字元，共 60-bit entropy；不由 SSID、MAC 或其他可預測資料推導。
- password 只保留於運行所需的 RAM、電子紙文字與 Wi-Fi QR payload，不寫入
  log、URL、HTTP response 或 runtime snapshot。
- AP radio 只有在電子紙 command 回報 `refreshed_and_slept` 後才啟動；顯示
  失敗時延後 AP start 並重試。
- payload 未變時跳過重複電子紙刷新——前提是同一 payload 已曾得到
  `refreshed_and_slept`，且自此沒有任何其他 frame 被接受／上屏；否則視為
  未顯示，重新刷新。
- AP page 是 provisioning 期間的電子紙 owner：圖片庫沒有 `enabled` 且未
  損毀的圖片時持續顯示 AP page，不提交 welcome frame；若有可顯示圖片，
  從 AP 狀態進入 `provisioning`（AP radio ready）起等待 5 分鐘，期限到達
  後才讓 carousel 排入圖片。AP 期間新增圖片會在下一輪 catalog 讀取時
  被納入；超過 5 分鐘才新增時可立即排入。AP presenter、carousel 與
  presence blank submit 透過同一 submission gate 序列化，避免 AP page 被
  後續 frame race 覆蓋；判斷「AP page 是否仍擁有面板」一律以
  `RuntimeCoordinator` 發佈的當下 AP session 為準（`pf_network::
  classify_ap_mode_window()`／`submission_gate_denies_for_ap_session()`），
  不得沿用呼叫端在取得 submission mutex 之前算出的舊值。
- WebUI URL 固定為 `http://192.168.4.1/`；HTML、CSS、JavaScript 與 favicon
  全部編入 app image（ADR-0016），不依賴 CDN。

## API 流程

### `GET /api/v1/wifi/scan`

第一次請求或帶 `?refresh=1` 時只排入 scan request 並回 `202`。實際
`esp_wifi_scan_start(..., true)` 在唯一的 `NetworkServiceTask` 內執行；
HTTP handler 不等待掃描。WebUI 輪詢同一路徑，完成後取得去除空值／無效
UTF-8、依 SSID 去重並按 RSSI 由強到弱排序的結果。

blocking scan 不使用 `WIFI_EVENT_SCAN_DONE`，避免晚到事件被錯認為下一次
掃描；同一時間的重複請求會合併。

### `POST /api/v1/wifi/config`

body 使用 `application/x-www-form-urlencoded` 的 `ssid` 與 `password`；
credential 不得放在 query string。SSID 最長 32 bytes；password 必須為空
（open network）或 8–63 bytes。body 上限 319 bytes，接收有 15 秒 absolute
deadline 與最多三次 idle timeout。

成功排入時回 `202` 與非秘密的 `request_id`。WebUI 接著輪詢：

```text
GET /api/v1/wifi/config/status?request_id=<id>
```

credential commit 由單一 worker 執行，並透過 runtime flash/display gate
與面板 framebuffer 操作序列化。同一時間只接受一筆 request：

1. 以 versioned blob 與 CRC32 寫入 NVS，`nvs_commit()` 成功才標記
   `committed`。
2. status response 成功送到 client 後，約 1 秒重新啟動。
3. client 沒有讀取 status 時，20 秒 fallback 後仍會重新啟動。
4. commit 失敗時回固定錯誤，不洩漏底層錯誤或 credential；失敗狀態經 client
   acknowledgement 或 20 秒 fallback 才釋放下一筆 request。

所有 HTTP body、parser candidate、credential blob、queue copy 與 worker
copy 都在離開 scope 時明確清零。

## 儲存安全說明

目前開發 profile 的 Wi-Fi credential blob 使用 CRC32 驗證完整性；CRC32
不是加密，也不是認證碼。未啟用 flash encryption／NVS encryption 的開發板
若被實體讀取 flash，credential 仍可能被取得。正式發行前必須依 Phase 8
release security gate 決定並驗證 Secure Boot、Flash Encryption 與 NVS
Encryption；在此之前不得把 credential 描述為「靜態加密」。
