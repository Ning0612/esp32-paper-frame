# PaperFrame 專案狀態

- 最後整理：2026-08-20（目前發布版本：`v0.9.2`）
- 本文件是目前進度的唯一摘要入口；詳細實機證據仍保留在
  [硬體驗證紀錄](hardware/VALIDATION.md)。
- `已完成` 代表程式／host/build 已完成；只有明確標成 `已驗證` 才代表有實機
  證據。`待決定` 不等於 bug，也不應在決策前自行擴張 scope。

## 一分鐘結論

Phase 1–8 的主要程式、host tests 與韌體 build 已完成。截至 2026-08-20，
Phase 2／3／4／5／6／8 的實機證據全數閉環——涵蓋 OTA 端到端與 rollback fault
injection、五種斷電路徑、AP provisioning 與存取邊界、browser 出圖管線、天氣四種
失敗分類、forced-BUSY 隔離與面板 sleep 電流。Phase 7 感測器已於 2026-08-23 接線
並完成主要實機驗證（DHT22 讀值、雙光敏通道校正、AWAY/PRESENT 轉換、白屏與
返回重繪）。**剩餘的硬體驗證只有三個小項與兩個低風險項**。其餘
待辦是公開 release security profile 與 MVP 以外功能的產品決策。

## 已完成或已決定

| 領域 | 狀態 | 權威文件 |
| --- | --- | --- |
| Phase 1 runtime／storage／health foundation | 程式、host／embedded test、build、boot／mount 已完成或驗證 | [歷史 Implementation Plan](archive/IMPLEMENTATION_PLAN.md) |
| Phase 2 display／renderer／DisplayTask | renderer、owner contract、carousel core、welcome lifecycle 已完成；六色 pattern 有實機證據 | [Display ADR](adr/0003-fix-phase2-display-integration.md) |
| Phase 3 provisioning／auth／WebUI | AP／STA、portal、sync auth／CSRF、管理 shell 與 Dashboard 已完成程式、host/build、STA smoke | [Auth](AUTHENTICATION.md)、[WebUI](WEBUI.md) |
| Phase 4 PFR1 | format、validator、browser pipeline、quantizer、packer 與 host tests 已完成 | [PFR1](formats/PFR1.md) |
| Phase 5 storage／catalog／carousel | partition、transaction、catalog、image API 與 runtime 接線已完成 | [Storage](STORAGE.md) |
| Phase 6 weather | parser、cache、設定與 worker 程式已完成；實機證據另列於下表 | [Weather](WEATHER.md) |
| Phase 7 sensors／presence | optional sensor contract、driver、filter、debounce 與 WebUI schema 已完成；2026-08-23 擴充為兩個獨立光敏通道（兩顆都暗才判定為暗）並完成接線與主要實機驗證 | [ADR-0018](adr/0018-dual-photoresistor-channels.md)、[歷史 Implementation Plan](archive/IMPLEMENTATION_PLAN.md) |
| Phase 8 diagnostics／OTA | diagnostics、System UI、OTA worker 與 release checklist 已完成程式；實機 release gate 未關閉 | [OTA ADR](adr/0008-ota-github-releases-and-rollback.md) |
| 認證 | PBKDF2 10,000 iterations、同步登入、session／CSRF contract 已決定 | [ADR-0007](adr/0007-auth-pbkdf2-iterations-and-sync-login.md) |
| OTA／partition | GitHub Releases、A/B rollback、`imagefs` preservation 已決定 | [ADR-0004](adr/0004-freeze-image-preserving-partitions.md)、[ADR-0008](adr/0008-ota-github-releases-and-rollback.md) |
| WebUI 交付 | 前端 gzip 後編入 app image，一次 OTA 同時更新韌體與前端；`webfs` 轉為 reserved | [ADR-0016](adr/0016-embed-webui-assets-in-firmware.md) |
| 手動 Recovery AP | 已移除；保留 blank-NVS 與 STA retry exhaustion 的 automatic fallback AP | [WebUI](WEBUI.md)、[Release Checklist](RELEASE_CHECKLIST.md) |

## 已實作但待驗證

| 領域 | 尚未閉環的實機證據 |
| --- | --- |
| Phase 7 sensors | 剩 DHT22 拔除後回 null、AND 合併語意的行為驗證、正式 180/30 debounce 計時、v1→v2 設定遷移的實機路徑；其餘已於 2026-08-23 實機閉環 |
| AP grace policy | presence 例外（需感測器）、低 DMA heap guard（低優先） |
| 設定降級邊界 | NVS 滿導致 `pf_config` 開啟失敗（低風險） |

已知遺留缺陷（有記錄、尚未修）：`DisplayOutcome` 把「畫面已刷上去」與「面板已
成功 sleep」混為一談，導致 sleep 失敗時會重刷一張已經正確的畫面。既有行為，
影響已由 welcome 重試的指數退避壓制；正確修法需拆開結果契約並取代 ADR-0003 的
driver contract，詳見
[ADR-0015 Update 2026-08-23](adr/0015-first-image-waits-for-ntp-and-weather.md)。

2026-08-20 已閉環（證據見[硬體驗證紀錄](hardware/VALIDATION.md)同日段落）：
OTA 端到端與 rollback confirmation、WebUI 隨韌體換版、reboot persistence、
OTA worker stack high-water、active OTA upload wrapper 的 slot 選擇、面板刷新
耗時（31.2 s）、真實 SNTP、`409 config_read_only`、認證邊界、System 頁瀏覽器
操作，以及 OTA／面板刷新／天氣三者併發下的 heap（天氣在該情境下會因 SSL
配置失敗而降級，屬容量限制而非缺陷）、browser 出圖與下載、webfs heap 差值、
天氣四種失敗分類，以及由使用者實機確認的面板狀態列視覺與長時間輪播。
`mDNS` 從未實作，已不列為待驗證項。

每完成一項，先更新 [硬體驗證紀錄](hardware/VALIDATION.md) 的頂端未完成索引，
再同步本表；不要只把 checkbox 改成完成。

## 待決定

以下項目不是目前的實作 blocker，必須先有產品／release 決策：

1. **Production security profile**：是否啟用 Secure Boot、Flash Encryption／
   NVS Encryption，以及對應的燒錄、key custody、recovery 與 release 流程。
2. **MVP release gate**：是否要求所有上表硬體證據關閉後才發布第一個公開版。
3. **MVP 以外的 P1 功能**：多 Wi-Fi profile、批次上傳、週排程、歷史圖表、
   Discord 通知、自動清圖、MQTT、蜂鳴器、音效與 AI 功能是否要進入後續 roadmap。

在這些決策完成前，不新增對應設定旗標、API 或預留式抽象層。

## 已決定不做

- **舊 bootloader 缺回滾保護的對外說明與開機示警**（2026-08-20 決定）：
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 於 2026-08-01 才加入，早於此燒錄
  bootloader 的裝置沒有回滾保護，且 OTA 不更新 bootloader。決定不加 release note
  警語、也不加開機示警——目前僅擁有者本人使用，該裝置的 bootloader 已於同日更新，
  而後續使用者都會從新裝置開始，首次燒錄即包含當前 bootloader。判斷方式與更新
  步驟已寫入 [FLASHING.md](hardware/FLASHING.md)，release checklist 也已加入前置
  確認項，這兩者保留。

## 明確不納入目前 MVP

目前不實作上述 P1 功能，也不納入 MQTT、蜂鳴器、音效或 AI。這些項目保留在
需求與 roadmap 脈絡中，但不應被誤讀成「目前待驗證」。

## 權威關係

- 現行外部行為與資料格式：未被取代的 ADR、[current contracts](AUTHENTICATION.md)。
- 進度摘要：本文件。
- 實機證據與風險：[硬體驗證紀錄](hardware/VALIDATION.md)。
- 發布門檻：[Release Checklist](RELEASE_CHECKLIST.md)。
- 原始需求與歷史計畫：[Guild.md](archive/Guild.md)、[Implementation Plan](archive/IMPLEMENTATION_PLAN.md)。
