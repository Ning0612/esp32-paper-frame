# Release Checklist

發布新版本（打 git tag、推送 GitHub Release）前必須依序完成以下項目。
本清單是 Phase 8 OTA（見
[`docs/adr/0008-ota-github-releases-and-rollback.md`](adr/0008-ota-github-releases-and-rollback.md)）
的直接前提：漏掉任一硬性規定會讓裝置端「檢查更新」／「立即更新」失效或
誤判，且錯誤只會在使用者實際嘗試更新時才暴露。

## 0. GitHub automation

- `.github/workflows/ci.yml` runs on pull requests targeting `main` and pushes
  to `main`. It runs the native tests, the ESP32-S3 embedded build-only gate,
  all WebUI contract tests, JavaScript syntax checks, and the firmware build.
- `.github/workflows/release.yml` runs only for pushed `v*` tags. It requires
  the tag to match `kFirmwareVersion` and point to a commit reachable from
  `main` before publishing a release.
- The release workflow publishes `paperframe-firmware.bin` for device OTA,
  the partition CSV, and `SHA256SUMS`. The WebUI is compiled into the
  firmware image, so there is no separate WebFS asset to flash. It does not
  replace the manual hardware validation below.

## 1. Release 資產硬性規定（OTA 依賴，不可省略）

- [ ] 建置好的韌體 app image（例如 `.pio/build/paperframe-s3/firmware.bin`）
  以**檔名完全是 `paperframe-firmware.bin`** 的形式附加到 GitHub
  Release 的 assets。裝置的 `pf_ota::OtaWorker` 透過
  `https://github.com/<owner>/<repo>/releases/latest/download/paperframe-firmware.bin`
  固定檔名 redirect 抓取，檔名錯誤或漏傳都會讓「立即更新」得到 404
  （分類為 `download_failed`，不會崩潰，但功能上等於該版本無法透過
  OTA 取得）。
- [ ] 推送的 git tag 格式為 `vMAJOR.MINOR.PATCH` 或
  `vMAJOR.MINOR.PATCH-prerelease`（`pf_runtime::compare_semver` 的容忍
  格式；純數字或缺 `v` 前綴也能解析，但團隊慣例統一用 `v` 前綴）。
- [ ] `components/pf_runtime/include/pf_runtime/firmware_version.hpp` 的
  `kFirmwareVersion` 已更新為與這次推送的 tag 相同版本號，並已重新
  `pio run` 確認編譯進最終 image——這是唯一的版本字串來源，同時決定
  `/api/v1/device` 顯示的版本與「檢查更新」的比對基準；沒同步會讓裝置
  誤判自己「已是最新」或錯誤判斷新舊。

## 2. 建置與測試（發布前必過）

- [ ] `.venv/Scripts/pio.exe run` 成功（韌體完整編譯，記錄 RAM/Flash
  使用率；若較上次 release 有明顯跳動，需在 release note 或
  `docs/hardware/VALIDATION.md` 記錄原因）。
- [ ] `.venv/Scripts/pio.exe test -e native` 全綠。
- [ ] 三個 embedded 測試都至少完成 build-only 驗證；有硬體時應盡量跑實際測試。
  CI 與 release workflow 自 2026-08-20 起都會跑這三個（在此之前只跑第一個）：
  - `pio test --project-conf platformio-embedded.ini -e paperframe-s3-embedded-test
    --without-uploading --without-testing`（`test_filter = test_runtime_coordinator`）
  - 同上但 `-e paperframe-s3-display-test`（`test_filter = test_display_task`）
  - 同上但 `-e paperframe-s3-embedded-test -f test_epd7in3e_driver`——
    `test_embedded/test_epd7in3e_driver` **沒有任何 env 的 `test_filter` 涵蓋它**，
    不加 `-f` 就永遠不會被編譯。新增 embedded 測試時記得同步這三個步驟。
- [ ] `node --check data/web/ui.js`（以及其他 `data/web/*.js`）。
- [ ] `for f in test/web/*.mjs; do node "$f"; done` 全部通過。
- [ ] `for f in test/*.mjs; do node "$f"; done` 全部通過（`test/` 根目錄的
  contract test，例如 `test_partition_layout.mjs`；它不在 `test/web/` 的迴圈裡，
  也不是 `pio test -e native` 會執行的 Unity 套件）。
- [ ] `PYTHONPATH=. python test/test_active_ota_upload.py` 通過（`pio test` 不會
  跑這個 PlatformIO 工具測試）。
- 上述三項由 `.github/workflows/ci.yml` 與 `release.yml` 自動執行。

## 3. 手動 on-device 檢查清單

對照 `docs/hardware/VALIDATION.md` 頂端的 `Current unresolved hardware evidence`
索引逐一確認，至少涵蓋：

- [ ] 全新開機（reboot persistence：設定、圖片、順序、目前圖片都保留）。
- [ ] OTA functional：檢查更新 → 立即更新 → 自動重開機，確認新版本正常啟動。
- [ ] Boot validation：確認開機後 `rollback_confirmed=ESP_OK` 出現在 console log。
- [ ] **先確認測試裝置的 bootloader 具備回滾能力**，再做下一項。回滾邏輯在
  bootloader，而 OTA 與日常 upload 都不會更新它：`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
  於 2026-08-01 才加入，早於此燒錄的裝置會在新韌體崩潰時無限 crash-loop 而不回滾
  （2026-08-20 實測）。判斷與更新方式見
  [FLASHING.md](hardware/FLASHING.md#bootloader-是否具備回滾保護)。
- [ ] Rollback fault injection（**條件觸發，非每次 release 都要做**）：刻意讓新版本
  在確認前 crash-loop，驗證 bootloader 真的會回滾，並記錄斷電／網路中斷等必要失敗
  路徑。這項驗證的是**該測試裝置 bootloader 本身**的回滾能力，OTA／一般 upload
  不會動到 bootloader（`0x0`）或 partition table（`0x8000`），所以能力一旦在某台
  裝置上確認過就不會因為燒新的 app 版本而退化。只在下列任一情況才需要重跑：
  - 換一台 bootloader 回滾能力**尚未有實機驗證紀錄**的裝置；
  - 這次 release 刻意重燒了 bootloader 或 partition table；
  - 這次 release 改動了 `sdkconfig.defaults` 的 rollback 相關設定，或
    `app_main.cpp` 呼叫 `esp_ota_mark_app_valid_cancel_rollback()` 的位置／時機。

  已用同一台裝置驗證過的紀錄見 `docs/hardware/VALIDATION.md`
  「2026-08-20 — OTA rollback fault injection」段落；不在上述任一情況時，
  在這裡記一句「沿用 YYYY-MM-DD 該裝置的驗證紀錄，本次未觸及 bootloader/rollback
  相關程式碼」即可跳過，不必重新注入。**注意 `rollback_confirmed=ESP_OK` 只代表
  app 呼叫了確認 API，不代表 bootloader 會在異常時回滾**——兩者必須分別驗證，
  這正是 2026-08-20 那次測試發現的落差。
- [ ] WebUI 版本一致性：確認 OTA 完成後瀏覽器載入的就是本次 release 的
  前端（前端已編入 app image，見 ADR-0016，不需也不應另外燒錄）。
- [ ] System 頁四個 current 操作（重新啟動、重設管理密碼、檢查更新、立即
  更新）在真實瀏覽器中可正常觸發且回應符合預期。
- [ ] STA 已連線時不提供手動 Recovery AP；Wi-Fi 失敗只走既有的自動 fallback AP。

## 4. Release note 內容

- [ ] 列出本次 app 韌體版本（`kFirmwareVersion`）與對應 git tag。
- [ ] 註明 WebUI 與韌體同版：前端已編入 app image（ADR-0016），一次
  OTA 同時更新兩者，既不需要、也不應該另外燒錄 WebUI。
- [ ] 若本次變更了 partition layout（理論上不應該——見 ADR-0004），
  必須有新的 superseding ADR 並在 release note 高亮警告。
- [ ] 附上 `docs/hardware/VALIDATION.md` 對應段落的連結或摘要，讓使用
  這個 release 的人知道哪些情境還沒經過實機驗證。
