# 管理 WebUI 與 Dashboard

Phase 3 的管理介面位於 `data/web/`，所有 HTML、CSS、JavaScript 與 favicon
都寫入 `webfs`，不依賴外部 CDN。登入後提供共用的 responsive 導覽殼層，
目前開放「總覽」與「Wi‑Fi」兩個 view；圖片、環境與系統 view 會在對應
phase 完成後啟用。

## API 路由

| 路由 | 存取 | 用途 |
| --- | --- | --- |
| `GET /api/v1/device` | 永久公開 | 固定產品／型號、API 版本、面板尺寸與安全容量資訊 |
| `GET /api/v1/health` | 永久公開 | 最小健康狀態；只讀 runtime snapshot |
| `GET /api/v1/auth/status` | 永久公開 | 管理密碼是否設定、目前 session 與 CSRF 狀態 |
| `POST /api/v1/auth/login` | 登入／首次建密碼 | 非同步提交 PBKDF 驗證，不把密碼放進 URL 或 response |
| `GET /api/v1/auth/login/status` | 登入流程 token | 取回一次性登入結果 |
| `POST /api/v1/auth/logout` | 已登入 + CSRF | 撤銷目前 session |
| `GET /api/v1/status` | 已登入 | 完整初版 runtime snapshot、容量與尚未提供功能的 `null` 狀態 |
| `GET /api/v1/config` | 已登入 | 遮蔽後設定；秘密只回傳 `*_set` 布林值 |
| `GET /api/v1/wifi/scan` | 首次 provisioning AP 或已登入 | 掃描結果 |
| `POST /api/v1/wifi/config` | 首次 provisioning AP，或已登入 + CSRF | 交易式保存 Wi‑Fi 憑證 |

所有 JSON 使用 `{ "ok": true, "data": ... }` 或 `{ "ok": false,
"error": ... }`，並設定 `Cache-Control: no-store`、`nosniff` 與同源 CSP。
`/api/v1/status` 的檔案容量與服務狀態來自同一份 RuntimeCoordinator
snapshot；handler 不等待顯示器、網路、NVS 或 filesystem。

尚未接入的圖片庫、天氣、SNTP 與感測器欄位以 `null` 或明確的
`unavailable`／`unknown` 表示，不填入零值或歷史資料。光敏電阻與溫溼度
感測器未安裝時，Dashboard 維持「未安裝／未知」狀態。

## 視覺與主題

- 米白網格背景、Teal／Coral／Mint、方角元件與硬陰影。
- 標題使用 Georgia／Noto Serif TC；狀態與控制項使用 Consolas 等寬字體。
- LIGHT／DARK 切換使用共用 localStorage key `iot-ui-theme`。
- 主內容最大寬度 1040px；窄螢幕改為上方導覽及單欄卡片。

## 更新方式

韌體 app-only upload 不會更新 WebUI。修改 `data/web/` 後，依
[ESP32-S3 燒錄操作](hardware/FLASHING.md) 的 WebUI-only 流程建置並只寫入
`0x510000` 的 `webfs` partition；不可把 `imagefs.bin` 放入同一命令。
