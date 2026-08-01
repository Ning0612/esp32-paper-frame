# Architecture Decision Records

會影響硬體接線、partition、持久資料格式、跨 task ownership、安全邊界或
對外 API 的決策，使用 ADR 固定。小型實作細節留在程式與測試，不為每個
函式建立 ADR。

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
- [ADR-0005：WeatherWorker HTTPS 契約與狀態列渲染基線](0005-weather-worker-and-status-bar.md)
- [ADR-0006：感測器 driver 來源與在場/離席判定機制](0006-sensor-drivers-and-presence.md)
- [ADR-0007：PBKDF2 迭代次數與同步登入決策（G6 收斂）](0007-auth-pbkdf2-iterations-and-sync-login.md)
- [ADR-0008：OTA 韌體來源、TLS 信任、Rollback 確認時機與 Release 慣例](0008-ota-github-releases-and-rollback.md)
- [ADR-0009：PFR1 Payload 壓縮與 PFC1 目錄容量上限](0009-pfr1-payload-compression-and-catalog-cap.md)
