# 文件入口與分類

PaperFrame 的文件分成「目前狀態」、「現行契約」、「設計決策」、「實機證據」
與「歷史背景」。一般讀者不需要從所有文件開始閱讀。

## 主要文件

按以下順序即可掌握專案：

1. [根目錄 README](../README.md)：專案用途、開源快速開始與公開限制。
2. [PROJECT_STATUS.md](PROJECT_STATUS.md)：唯一的完成／待驗證／待決定摘要。
3. [ADR index](adr/README.md)：為什麼採用目前架構、格式與安全決策。
4. [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)：公開版的 release gate。
5. [CONTRIBUTING.md](../CONTRIBUTING.md) 與 [SECURITY.md](../SECURITY.md)：如何參與與回報問題。

## 只有在修改該領域時才讀

這些文件是現行 contract，不是進度清單：

- [AUTHENTICATION.md](AUTHENTICATION.md)、[PROVISIONING.md](PROVISIONING.md)
- [WEBUI.md](WEBUI.md)、[HEALTH_API.md](HEALTH_API.md)
- [STORAGE.md](STORAGE.md)、[WEATHER.md](WEATHER.md)
- [formats/PFR1.md](formats/PFR1.md)、[formats/PFC1.md](formats/PFC1.md)

## 證據與操作

- [hardware/VALIDATION.md](hardware/VALIDATION.md)：append-only 實機證據與目前未閉環項目。
- [hardware/FLASHING.md](hardware/FLASHING.md)：燒錄與 imagefs 保護操作。

這兩份文件回答「實際測了什麼」與「如何操作」，不取代
[PROJECT_STATUS.md](PROJECT_STATUS.md) 的進度摘要。

## 歷史與參考

- [Guild.md](archive/Guild.md)：原始需求 provenance，不是現行規格。
- [IMPLEMENTATION_PLAN.md](archive/IMPLEMENTATION_PLAN.md)：階段、acceptance 與開發歷史，
  不取代 current contract 或本文件的狀態摘要。
- [REFERENCES.md](REFERENCES.md)、[ASSET_CREDITS.md](../ASSET_CREDITS.md) 與
  [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)：來源與授權追溯。

## 目前刻意保留在主文件區

- `hardware/VALIDATION.md`：頂端有目前未閉環索引，後面的 append-only 紀錄仍是可追溯的實機證據；不把整份移入 archive。
- `hardware/FLASHING.md` 與 `RELEASE_CHECKLIST.md`：仍會直接用於燒錄、驗證與發布，屬於現行操作文件。
- ADR：目前 15 份都是已接受的公開設計決策；即使部分決策後來被新 ADR 取代，也要保留決策歷史，不搬到 archive。
- `REFERENCES.md`、`ASSET_CREDITS.md`、`THIRD_PARTY_NOTICES.md`：授權與 provenance 是開源交付的一部分，不是過時開發計畫。
- `AUTHENTICATION.md`、`PROVISIONING.md`、`WEBUI.md`、`HEALTH_API.md`、`STORAGE.md`、`WEATHER.md` 與 `formats/`：描述目前仍有效的外部行為與資料格式。

若日後仍需要縮短硬體紀錄的日常閱讀範圍，可再把較早的 append-only 歷史段落拆成
`docs/archive/`，但必須保留 `VALIDATION.md` 的目前未閉環索引與指向歷史證據的連結。

新增文件前，先確認內容不能放入上述既有分類；避免建立另一份進度表或重複的
API／硬體驗證說明。
