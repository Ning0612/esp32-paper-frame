# Security Policy

PaperFrame 是區域網路／裝置 AP 使用的離線優先裝置。安全問題請不要直接
公開成 issue，尤其是可能暴露管理 session、Wi-Fi credential、API key、OTA
驗證、CSRF、任意檔案寫入或跨 `webfs`／`imagefs` 邊界的問題。

## 回報方式

優先使用 GitHub repository 的 private vulnerability reporting 或 Security
Advisory。若 repository 尚未啟用該功能，請透過 repository owner 的公開聯絡
方式私下聯繫維護者；在收到回覆前不要公開細節。

請提供：

- 受影響的 commit、release 或硬體／韌體 profile；
- 可重現步驟、預期與實際結果；
- 影響範圍與可能的緩解方式；
- 必要的 log 或封包時，先移除 password、token、API key、SSID、IP、MAC、序號
  與其他裝置識別資料。

## 公開安全限制

- 管理介面只適合可信任 LAN 或裝置 AP，不啟用 CORS。
- 密碼只儲存強雜湊；secret API 欄位不回傳原值。
- OTA 與 WebUI 更新不得清除 `imagefs`。
- Secure Boot、Flash Encryption／NVS Encryption 與部分硬體 release gate
  尚未完成公開驗證；請勿將目前開發 profile 宣稱為 production-secure。

安全設計的正式決策以 [authentication contract](docs/AUTHENTICATION.md)、
[provisioning contract](docs/PROVISIONING.md) 與相關 ADR 為準。
