# Contributing to PaperFrame

感謝參與 PaperFrame。這個專案目前仍在硬體驗證階段，請先閱讀：

1. [README](README.md)：公開基線、建置與已知限制。
2. [專案狀態](docs/PROJECT_STATUS.md)：目前已完成、待驗證與待決定事項。
3. [ADR index](docs/adr/README.md)：架構與跨模組決策。
4. 受影響功能的 current contract，例如 [authentication](docs/AUTHENTICATION.md)、
   [WebUI](docs/WEBUI.md) 或 [PFR1](docs/formats/PFR1.md)。

## 開發與驗證

- 使用原生 ESP-IDF；除非先提出並接受 ADR，不引入 Arduino framework。
- 修改韌體前先建立或更新最小可重現測試；純邏輯優先使用 host test。
- 常用檢查：

  ```powershell
  .\.venv\Scripts\pio.exe test -e native
  .\.venv\Scripts\pio.exe run
  ```

- **推送前請跑 `bash scripts/verify-like-ci.sh`**，它按順序複製 CI 與 release
  workflow 的每一個驗證步驟。上面兩個常用指令**不足以代表 CI**：`pio run`
  只編譯韌體，`test_embedded/` 的測試原始碼是由
  `pio test --without-uploading --without-testing` 編譯的，而該 environment
  開啟 `-Werror`。只跑常用指令曾讓 `-Werror=missing-field-initializers` 的
  失敗漏到 CI（2026-08-23）。

- 涉及面板、Wi-Fi、NVS、partition、OTA 或斷電復原時，必須區分 host/build
  結果與實機證據，並更新 [硬體驗證紀錄](docs/hardware/VALIDATION.md)。
- 不要提交 `.pio/`、build output、`sdkconfig`、執行期 imagefs、credential、
  secret 或真實裝置識別資料。

## Pull request

- 一個 PR 聚焦一個可說明、可驗證的目標。
- 描述變更、未變更的範圍、測試命令與尚未完成的硬體驗證。
- API、資料格式、partition、硬體接線或安全行為的變更，必須同步 current
  contract 與相關 ADR，不能只修改 README。
- Commit 使用英文 Conventional Commits，例如
  `fix(storage): recover image transactions`。

## 文件與資產

- ADR 是公開的決策歷史，不是私人工作日誌；請遵守
  [ADR 公開範圍](docs/adr/README.md#開源公開範圍)。
- 第三方資產與授權請同步 [ASSET_CREDITS.md](ASSET_CREDITS.md) 與
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
- 文件範例只能使用遮蔽過的秘密與泛化後的裝置資料。
