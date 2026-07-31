# ADR-0007：PBKDF2 迭代次數與同步登入決策（G6 收斂）

- Status: accepted
- Date: 2026-08-01
- Supersedes: 隱含取代 `docs/AUTHENTICATION.md` 先前對 600,000
  iterations 的「provisional / 待硬體驗證」描述

## Context

Phase 3 的 G6 安全性 gate 把 PBKDF2-HMAC-SHA256 迭代次數暫定為
600,000（OWASP 對「網路暴露服務」的建議下限量級），並註記「Phase 3
實機部署時必須記錄 `hash_elapsed_ms`，確認登入延遲與 watchdog 行為可
接受後，才將此參數視為 hardware-validated」——也就是說這個值從一開始
就是待驗證的暫定值，不是最終決策。

2026-07-30 的實機驗證（`docs/hardware/VALIDATION.md` 2026-07-30
「STA 管理 WebUI 與桌面版寬度驗證」一節）量到 600,000 iterations 在真實
ESP32-S3 上耗時約 **136 秒**。這個結果明顯不可接受，但當時的處理方式是
把 WebUI 輪詢 deadline 從 65 秒延長到 180 秒（commit `4aca200`），而不是
回頭重新評估這個 gate——即 gate 自己寫的驗收條件從未被真正滿足。

136 秒的雜湊時間反過來逼出了一整套只為了不讓 HTTP handler 卡住兩分鐘的
非同步機制：`pf_auth` 內的專用 `AuthTask`、`operation_mutex_`、
`queue_`、request-token bearer 協議；`pf_web` 的
`GET /api/v1/auth/login/status` 輪詢 endpoint；前端 300ms 輪詢迴圈與
180 秒 deadline。這套機制還帶出一個真正的授權判斷 UX bug：`409`（前一個
登入操作還沒被 ack、單一 in-flight slot 被佔用）與 `401`（密碼錯誤）在
前端被顯示成同一句「登入失敗，請確認密碼」，使用者在首次建立 admin 密碼
時完全分不出「要等」還是「打錯密碼」。

## Decision

### 目標威脅模型：家用 LAN 裝置

本產品是單一使用者的家用 e-paper 相框，不對外網路暴露（見
`docs/PROVISIONING.md`／`AGENTS.md` 的 LAN/AP-only 前提，無 CORS、無
TLS）。對這個威脅模型，現實的攻擊面是：

1. 同一 LAN 內的其他裝置嘗試連線管理介面——這不是暴力破解密碼雜湊，是
   繞過網路層存取控制的問題，PBKDF2 work factor 幫不上忙。
2. 攻擊者實體拆機、讀出 flash 內容後離線嘗試密碼——這才是 PBKDF2 work
   factor 真正在防範的情境。

600,000 iterations 的量級是針對「網路服務、雜湊資料庫可能被大量離線
攻擊」校準的，套用在單一裝置、單一帳號、家用威脅模型上是不成比例的
過度設計，且已被證實在目標硬體上不可用（136 秒）。

### 決策：10,000 iterations，同步登入

- `kDefaultPbkdf2Iterations` 定為 **10,000**——PBKDF2 迭代次數安全下限
  的歷史基準值（NIST SP800-132 早期建議下限），對家用 LAN 威脅模型是
  合理折衷：仍然遠高於「不加鹽/固定次數」之類的錯誤實作，同時把雜湊
  時間壓到人類可接受的秒級（實測值需記錄於
  `docs/hardware/VALIDATION.md`，預期依既有 600,000→136s 的量測值線性
  換算約 2-3 秒）。
- `pf_config` 既有的 schema 範圍檢查 `iterations ∈ [10000, 2000000]`
  未變動，10,000 剛好落在下限——不需要修改資料格式或加 migration
  邏輯；舊裝置上以 600,000 寫入的既有 NVS 記錄，其 `iterations`
  欄位隨紀錄一起保存，仍會照原本存的值驗證，不受這次改動影響。
- 雜湊時間降到秒級後，`AuthService::login()` 直接在呼叫者（HTTP
  handler）的 task context 同步執行 PBKDF2 與（首次設定時的）NVS
  commit，拆除整套 `AuthTask`/`queue_`/`operation_mutex_`/request-token
  機制。安全性考量：拿掉 job queue 後仍需要防止「兩個瀏覽器分頁同時
  送出首次建密碼」造成兩次雜湊互相覆寫的競態，做法是用一個涵蓋整段
  `perform_login()` 呼叫的簡單 mutex（0-tick 嘗試鎖，鎖不到直接回
  `LoginStatus::busy` → HTTP `409`），這不是 queue/task，只是一個
  臨界區，符合「唯讀狀態用簡單 lock、只有 Display SPI／imagefs-NVS
  交易／OTA 才用 Command/Result queue」的專案分工原則。
- 前端同步移除輪詢迴圈，並修正 `409`/`401` 顯示成同一句錯誤訊息的 UX
  bug：`409` 顯示「已有另一個登入嘗試進行中，請稍候再試」，`401`
  顯示「密碼錯誤，請重新輸入」。

### 不做的事

- 不引入失敗次數鎖定／指數退避——探索確認整個 codebase 原本就沒有這類
  機制（單一 in-flight slot 曾經是唯一的間接節流手段，拿掉 queue 之後
  也一併消失）。家用 LAN 威脅模型下，這個決定是刻意的：唯一的節流就是
  雜湊本身的計算成本，若之後威脅模型改變（見下）需要重新評估。
- 不啟用 Flash Encryption／NVS Encryption／Secure Boot——這些仍留給
  `docs/AUTHENTICATION.md` 既有記載的 Phase 8 release security gate，
  與本 ADR 的登入延遲/併發設計無關。

## Consequences

- 若未來這個裝置的部署情境改變（例如透過 port forwarding 或 VPN 對外
  網路暴露、或改為多用戶/多裝置共享情境），10,000 iterations 與「沒有
  失敗次數限制」這兩個決策都必須重新評估——本 ADR 的前提是單一使用者
  家用 LAN，前提改變則決策失效，需要新的 superseding ADR。
- `AuthService` 對外介面從三段式
  （`submit_login`/`login_status`/`acknowledge_login`）收斂為單一
  `login()`，`pf_web` 對應的 `/api/v1/auth/login/status` route 與
  `X-Auth-Request` header 一併移除；任何依賴舊 API 形狀的外部整合
  （目前沒有，僅供內部 WebUI 使用）需要重新對接。
- `test/web/test_auth_ui_contract.mjs` 原本釘住的 `180000` 常數不再
  存在，改為斷言新的同步 contract（存在 `/api/v1/auth/login`、不存在
  `/api/v1/auth/login/status`）。

## Verification

- `pio run` 與 `pio test -e native`（含 `test_auth_form`、
  `test_auth_session`、`test_auth_tokens`、`test_management_password`、
  `test_web_access_policy`）全綠。
- `node test/web/test_auth_ui_contract.mjs` 通過新的斷言。
- 實機驗證（無法 host test，見 `docs/hardware/VALIDATION.md`）：
  - 清空 NVS 模擬真正的 blank-NVS 首次開機，走完整首次 AP
    provisioning → 建立 admin 密碼 → 登入流程，記錄實測
    `hash_elapsed_ms` 與端到端延遲。
  - 已有 STA 設定、重設密碼後登入，重新量測同步流程下的實際延遲，與
    先前 600,000 iterations／136 秒的量測值對照。
