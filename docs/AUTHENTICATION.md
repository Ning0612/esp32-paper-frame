# 管理認證與 CSRF contract

## 信任邊界

- 固定管理帳號為 `admin`，密碼長度為 8–128 bytes。
- 只有裝置啟動時確認「尚無管理密碼」，而且目前位於本機 provisioning AP
  時可以建立第一組管理密碼。全新裝置與從尚未支援 auth 的舊版升級都走此
  password bootstrap。
- 升級裝置若已有 Wi-Fi credential 但尚無管理密碼，韌體保留 NVS
  credential、不嘗試 STA，暫時啟動本機 AP。建立密碼後可重新提交 Wi-Fi
  設定；未登入前仍不能使用 Wi-Fi scan/config 的 bootstrap 例外。
- Recovery AP 與 STA 模式都不能使用首次建密碼例外。未登入的管理 route
  回 `401`；已登入但 mutation 缺少或帶錯 CSRF 時回 `403`。
- 永久公開面限安全的 health、device、auth status 與 login。Wi-Fi scan 與
  config 的 bootstrap 例外另依
  [provisioning contract](PROVISIONING.md) 判斷。

## 密碼儲存

管理密碼不保存原文。韌體使用 PSA Crypto 的
PBKDF2-HMAC-SHA256，預設 work factor 為 600,000 iterations，搭配每筆
16-byte 隨機 salt 與 32-byte derived hash。NVS blob 同時保存 magic、
record version、algorithm ID、iterations 與 CRC32；版本、演算法、work
factor 範圍或 CRC 不符時 fail closed。

CRC32 只驗證 blob 是否損毀，並不提供加密或防竄改保證。目前開發 profile
尚未啟用 Flash Encryption／NVS Encryption，實體取得 flash 的攻擊者仍可
離線嘗試密碼。正式發行前需在 Phase 8 release security gate 決定並驗證
Secure Boot、Flash Encryption 與 NVS Encryption。

600,000 iterations 是 G6 固定的開發預設值；Phase 3 實機部署時必須記錄
ESP32-S3 的 `hash_elapsed_ms`，確認登入延遲與 watchdog 行為可接受後，才將
此參數視為 hardware-validated。

## 非同步登入

PBKDF2 與首次密碼的 NVS commit 都在單一 `AuthTask` 執行，HTTP handler
不等待雜湊或 flash 寫入。同時只允許一筆登入工作，完成結果保留 180 秒；WebUI
最多輪詢 180 秒，以涵蓋 ESP32-S3 實機的 PBKDF2 延遲：

1. `POST /api/v1/auth/login`
   - body：`application/x-www-form-urlencoded`
   - 欄位：`username=admin`、`password=<8–128 bytes>`
   - 接受後回 `202` 與 256-bit 隨機 `request_token`。
2. `GET /api/v1/auth/login/status`
   - 將 token 放在 `X-Auth-Request` header，不放在 URL。
   - 驗證中回 `202`；錯誤密碼回 `401`；不允許首次設定回 `403`。
   - 成功時設定 session cookie，response 只回 CSRF token 與安全狀態。

request token 是短期 bearer secret。所有 auth response 使用 `no-store`，
password、request token、session token 與 CSRF 都不寫入 log；離開作用域
時會清零保存這些值的固定大小 buffer。

WebUI 登入 deadline contract 可用以下 host check 驗證：

```powershell
node test\web\test_auth_ui_contract.mjs
```

## Session 與 CSRF

- session token 與 CSRF token 各使用 32-byte（256-bit）硬體隨機值，以
  64 字元 hexadecimal 編碼傳輸。
- 裝置只保留一組 server-side session；新登入立即撤銷舊 session。
- idle expiry 為 30 分鐘，absolute expiry 為 24 小時；fake-clock tests
  覆蓋精確邊界與 clock rollback。
- cookie 為 `Path=/; HttpOnly; SameSite=Strict`。管理介面目前只提供裝置
  LAN／AP 的 HTTP，因此未宣稱 TLS，也不能設定只允許 HTTPS 傳輸的
  `Secure` attribute。
- 需保護的 POST、PUT、DELETE 必須同時通過 session cookie 與
  `X-CSRF-Token`。logout 也套用相同規則，成功後撤銷 server-side session
  並清除 cookie。

## API 摘要

| Route | 公開條件 | 說明 |
| --- | --- | --- |
| `GET /api/v1/auth/status` | 永久公開 | 回 password/session 狀態；已有有效 session 時回目前 CSRF |
| `POST /api/v1/auth/login` | 永久公開 | 排入登入或首次建密碼工作 |
| `GET /api/v1/auth/login/status` | 持有短期 request token | 取得非同步登入結果 |
| `POST /api/v1/auth/logout` | session + CSRF | 撤銷目前 session |
| `GET /api/v1/wifi/scan` | bootstrap 例外或 session | 掃描 Wi-Fi |
| `POST /api/v1/wifi/config` | bootstrap 例外或 session + CSRF | 保存 Wi-Fi credential |
