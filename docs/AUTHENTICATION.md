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
PBKDF2-HMAC-SHA256，預設 work factor 為 **10,000 iterations**（實機約
2-3 秒雜湊時間，詳見 `docs/adr/0007-auth-pbkdf2-iterations-and-sync-login.md`），
搭配每筆 16-byte 隨機 salt 與 32-byte derived hash。NVS blob 同時保存
magic、record version、algorithm ID、iterations 與 CRC32；版本、演算法、
work factor 範圍或 CRC 不符時 fail closed。既有的高迭代次數紀錄（例如舊
韌體寫入的 600,000）仍會照紀錄裡存的 `iterations` 正常驗證，不需要
migration。

CRC32 只驗證 blob 是否損毀，並不提供加密或防竄改保證。目前開發 profile
尚未啟用 Flash Encryption／NVS Encryption，實體取得 flash 的攻擊者仍可
離線嘗試密碼。正式發行前需在 Phase 8 release security gate 決定並驗證
Secure Boot、Flash Encryption 與 NVS Encryption。

10,000 iterations 是 G6 針對「家用 LAN 裝置」威脅模型固定的正式決策
（`docs/adr/0007`）：主要防範對象是實體拆機讀 flash 的離線嘗試，而非
網路暴力破解；換取的是登入延遲落在使用者可接受的秒級範圍內，並讓登入
可以同步處理（見下）。若之後裝置的部署情境改變（例如對外網路暴露），
需要重新評估這個決策。

## 同步登入

`POST /api/v1/auth/login` 直接在 HTTP handler 內同步完成 PBKDF2 雜湊與
（首次設定時的）NVS commit，不經過背景 task 或 queue：

- body：`application/x-www-form-urlencoded`
- 欄位：`username=admin`、`password=<8–128 bytes>`
- 成功（登入或首次建密碼）：回 `200`，設定 session cookie，body 帶
  CSRF token。
- 錯誤密碼：`401`。不允許首次設定：`403`。格式錯誤：`400`。
- 已有另一個登入/建密碼操作正在進行中（例如兩個分頁同時送出首次建密碼）：
  `409`，前端顯示「請稍候再試」而非「密碼錯誤」——`AuthService` 用一個
  單一 mutex 涵蓋整段雜湊計算來避免併發覆寫，不使用 job queue。

password、session token 與 CSRF 都不寫入 log；離開作用域時會清零保存這些
值的固定大小 buffer。所有 auth response 使用 `no-store`。

WebUI 登入 contract 可用以下 host check 驗證：

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
| `POST /api/v1/auth/login` | 永久公開 | 同步登入或首次建密碼 |
| `POST /api/v1/auth/logout` | session + CSRF | 撤銷目前 session |
| `GET /api/v1/wifi/scan` | bootstrap 例外或 session | 掃描 Wi-Fi |
| `POST /api/v1/wifi/config` | bootstrap 例外或 session + CSRF | 保存 Wi-Fi credential |
