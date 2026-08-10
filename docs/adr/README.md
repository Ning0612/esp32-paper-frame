# Architecture Decision Records

會影響硬體接線、partition、持久資料格式、跨 task ownership、安全邊界或
對外 API 的決策，使用 ADR 固定。小型實作細節留在程式與測試，不為每個
函式建立 ADR。

## 與其他文件的權威關係

已接受且未被取代的 ADR 是跨模組、硬體、持久格式與安全決策的權威來源；
新 ADR 取代舊決策時，必須保留歷史檔案並明確標示 `Supersedes`。目前 API、
WebUI、儲存、天氣、認證與 binary format 的操作契約位於 `docs/` 主題文件；
`docs/archive/IMPLEMENTATION_PLAN.md` 只做追溯，`hardware/VALIDATION.md` 只做證據
紀錄，`docs/archive/Guild.md` 只保留原始需求 provenance。

## 開源公開範圍

ADR 是本專案的公開設計決策歷史，建議保留在開源 repository。公開 ADR 應
說明已接受、被取代或拒絕的決策、影響範圍與理由，讓貢獻者理解「為什麼」
而不只看到目前的實作。

ADR 不應包含密碼、API key、session token、Wi-Fi credential、真實裝置識別、
私人路徑或未脫敏的內部 log。原始討論、私人測試紀錄與尚未形成決策的草稿
應留在 repository 外；被取代的 ADR 可保留，但必須標明 superseded 與取代它的
文件。開源貢獻流程請見根目錄的 `CONTRIBUTING.md`。

## 命名

```text
NNNN-short-imperative-title.md
```

編號遞增且不重用。已接受 ADR 不原地改寫結論；若決策改變，以新 ADR
取代並在兩份文件互相連結。

## 必要段落

```markdown
# ADR-NNNN：標題

- Status: proposed | accepted | superseded
- Date: YYYY-MM-DD
- Supersedes: none | ADR-NNNN

## Context
## Decision
## Consequences
## Verification
```

## Index

- [ADR-0001：建立專案技術與授權基線](0001-project-foundations.md)
- [ADR-0002：固定 ESP32-S3-N16R8 開發 profile](0002-esp32-s3-n16r8-profile.md)
- [ADR-0003：固定 Phase 2 顯示器接線與 driver contract](0003-fix-phase2-display-integration.md)
- [ADR-0004：凍結 imagefs-preserving partition layout](0004-freeze-image-preserving-partitions.md)
- [ADR-0005：WeatherWorker HTTPS 契約與狀態列渲染基線](0005-weather-worker-and-status-bar.md)（「字型與圖示授權」的圖示部分已被 ADR-0013 取代；「Rate limit / 更新頻率」子決策與 location/language 欄位已被 ADR-0014 取代）
- [ADR-0006：感測器 driver 來源與在場/離席判定機制](0006-sensor-drivers-and-presence.md)
- [ADR-0007：PBKDF2 迭代次數與同步登入決策（G6 收斂）](0007-auth-pbkdf2-iterations-and-sync-login.md)
- [ADR-0008：OTA 韌體來源、TLS 信任、Rollback 確認時機與 Release 慣例](0008-ota-github-releases-and-rollback.md)
- [ADR-0009：PFR1 Payload 壓縮與 PFC1 目錄容量上限](0009-pfr1-payload-compression-and-catalog-cap.md)（`kCatalogMaxEntries` 部分已被 ADR-0010 取代）
- [ADR-0010：撤銷 PFC1 目錄容量上限拉高，維持 48 筆](0010-revert-catalog-cap-raise-ram-constraint.md)
- [ADR-0011：PFR1 壓縮功能的 PSRAM／flash cache-disable 交錯安全性調查](0011-psram-flash-cache-disable-safety.md)
- [ADR-0012：RAM 重構後重新拉高 PFC1 目錄容量上限至 64 筆](0012-raise-catalog-cap-after-ram-reclaim.md)（取代 ADR-0010 的 `kCatalogMaxEntries` 部分）
- [ADR-0013：天氣圖示改用轉檔自第三方 OFL-1.1 素材的點陣圖](0013-weather-icon-bitmaps-from-third-party-ofl-source.md)（取代 ADR-0005「字型與圖示授權」的圖示部分）
- [ADR-0014：天氣更新改為面板刷新觸發，WebUI 改用地圖選點](0014-weather-panel-refresh-cadence-and-map-picker.md)（取代 ADR-0005「Rate limit / 更新頻率」子決策與 location/language 欄位）
- [ADR-0015：開機首張真實圖片等待 NTP／天氣就緒或逾時](0015-first-image-waits-for-ntp-and-weather.md)
