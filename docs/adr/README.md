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
