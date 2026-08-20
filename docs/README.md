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

## 硬體規格

- [hardware/HARDWARE.md](hardware/HARDWARE.md)：元件清單、接線與腳位限制，
  回答「要買什麼、怎麼接」。**它不是決策權威**——腳位分配的理由固定在
  [ADR-0003](adr/0003-fix-phase2-display-integration.md)、感測器行為在
  [ADR-0006](adr/0006-sensor-drivers-and-presence.md)，衝突時以 ADR 為準。
  根目錄 README 的「硬體」段是本檔摘要；**重要資訊刻意在兩處重複**，
  讓讀者不必先點進 docs 才知道要買什麼板子。

## 證據與操作

- [hardware/VALIDATION.md](hardware/VALIDATION.md)：append-only 實機證據與目前未閉環項目。
- [hardware/FLASHING.md](hardware/FLASHING.md)：燒錄與 imagefs 保護操作，
  包含**新裝置首次燒錄**（日常 upload 只寫 app slot，不含 bootloader，
  空白板無法只靠它開機）與**判斷 bootloader 是否具備 OTA 回滾保護**。

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

新增文件前，先確認內容不能放入上述既有分類；避免建立另一份**進度表**或重複的
**API／硬體驗證說明**。

**重複的界線**：常用且會影響「要不要動手」的資訊——例如買什麼板子、接哪幾支腳、
現在能不能宣稱 production-ready——**允許在根目錄 README 與 `docs/` 同時出現**，
讓讀者不必層層點進來才看得到。但必須符合兩個條件：

1. 明確標示哪一份是權威（決策以 ADR 為準、進度以 `PROJECT_STATUS.md` 為準、
   實機證據以 `VALIDATION.md` 為準），摘要處註明它是摘要；
2. 改動時兩處一起改——摘要與權威來源不一致，比沒有摘要更糟。

不適用於進度狀態與實機驗證結果：那兩類只在單一入口維護，其他地方一律用連結。
