# Health API contract

`/api/v1/device` 是永久公開的安全裝置描述；`/api/v1/status` 是登入後的
Dashboard snapshot，會在後續 phase 增量加入圖片、weather 與 sensor 欄位。
兩者都不回傳 MAC、SSID、IP、credential 或 API key。

## `GET /api/v1/health`

此 route 是永久公開的最小健康狀態，不要求登入。Phase 3 會在
`esp_netif`、NetworkService 與 runtime coordinator 初始化後啟動
`esp_http_server`；可從 provisioning AP 或已連線的 STA 存取。handler
本身仍只讀 runtime snapshot，不依賴 AP／STA 或 Internet 成功狀態。

成功時回傳 `200 application/json` 與 `Cache-Control: no-store`：

```json
{
  "status": "ready",
  "sequence": 12,
  "uptime_ms": 3456,
  "services": {
    "flash": "ready",
    "psram": "ready",
    "config": "ready",
    "imagefs": "ready"
  },
  "network": {
    "wifi": "connected",
    "internet": "unknown"
  }
}
```

- service state 只有 `unknown`、`ready`、`degraded`。
- `services` 不含 `webfs`：WebUI 資產已編入 app image，`webfs` 分割區
  轉為 reserved（不掛載、不寫入、不統計），因此沒有對應的執行期狀態可報。
  見 [ADR-0016](adr/0016-embed-webui-assets-in-firmware.md)。

- 任一 service 不是 `ready` 時，頂層 `status` 為 `degraded`。
- snapshot 尚未發布時，頂層與各 service 都回傳 `unknown`，不得以 `0` 或
  歷史值偽裝；已發布 snapshot 內有 unknown service 時，頂層為 `degraded`。
- payload 不包含 MAC、SSID、IP、credential、password、API key、檔名或
  其他裝置／使用者識別資料。
- `network.wifi` 只有 `unknown`、`connecting`、`connected`、
  `starting_ap`、`provisioning`、`failed`；`network.internet` 只有
  `unknown`、`reachable`、`unreachable`。兩者是獨立狀態。
- handler 只複製目前 runtime snapshot 並讀取 monotonic uptime；不得等待
  command queue、filesystem、硬體或外部網路。

已登入的管理端可透過 `/api/v1/weather/config` 讀取遮罩後的天氣與 NTP
設定，並以 CSRF 保護的 POST 保存設定；API key 僅以 `api_key_set` 布林值
表示是否存在。
