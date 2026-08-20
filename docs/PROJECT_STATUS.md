# PaperFrame 專案狀態

- 最後整理：2026-08-20（目前發布版本：`v0.9.2`）
- 本文件是目前進度的唯一摘要入口；詳細實機證據仍保留在
  [硬體驗證紀錄](hardware/VALIDATION.md)。
- `已完成` 代表程式／host/build 已完成；只有明確標成 `已驗證` 才代表有實機
  證據。`待決定` 不等於 bug，也不應在決策前自行擴張 scope。

## 一分鐘結論

Phase 1–8 的主要程式、host tests 與韌體 build 已完成。截至 2026-08-20，OTA
端到端（含 rollback confirmation 與 WebUI 隨韌體換版）、active-slot upload
wrapper、面板刷新耗時、真實 SNTP、設定降級的 `409 config_read_only` 路徑與
認證邊界都已有實機證據。專案目前仍不是 release-ready；剩餘工作主要是**故障
注入類**的實機 acceptance（rollback fault injection、各種斷電路徑）、尚未接線
的感測器整段、公開 release security profile，以及 MVP 以外功能的產品決策。

## 已完成或已決定

| 領域 | 狀態 | 權威文件 |
| --- | --- | --- |
| Phase 1 runtime／storage／health foundation | 程式、host／embedded test、build、boot／mount 已完成或驗證 | [歷史 Implementation Plan](archive/IMPLEMENTATION_PLAN.md) |
| Phase 2 display／renderer／DisplayTask | renderer、owner contract、carousel core、welcome lifecycle 已完成；六色 pattern 有實機證據 | [Display ADR](adr/0003-fix-phase2-display-integration.md) |
| Phase 3 provisioning／auth／WebUI | AP／STA、portal、sync auth／CSRF、管理 shell 與 Dashboard 已完成程式、host/build、STA smoke | [Auth](AUTHENTICATION.md)、[WebUI](WEBUI.md) |
| Phase 4 PFR1 | format、validator、browser pipeline、quantizer、packer 與 host tests 已完成 | [PFR1](formats/PFR1.md) |
| Phase 5 storage／catalog／carousel | partition、transaction、catalog、image API 與 runtime 接線已完成 | [Storage](STORAGE.md) |
| Phase 6 weather | parser、cache、設定與 worker 程式已完成；實機證據另列於下表 | [Weather](WEATHER.md) |
| Phase 7 sensors／presence | optional sensor contract、driver、filter、debounce 與 WebUI schema 已完成；硬體未接入 | [歷史 Implementation Plan](archive/IMPLEMENTATION_PLAN.md) |
| Phase 8 diagnostics／OTA | diagnostics、System UI、OTA worker 與 release checklist 已完成程式；實機 release gate 未關閉 | [OTA ADR](adr/0008-ota-github-releases-and-rollback.md) |
| 認證 | PBKDF2 10,000 iterations、同步登入、session／CSRF contract 已決定 | [ADR-0007](adr/0007-auth-pbkdf2-iterations-and-sync-login.md) |
| OTA／partition | GitHub Releases、A/B rollback、`imagefs` preservation 已決定 | [ADR-0004](adr/0004-freeze-image-preserving-partitions.md)、[ADR-0008](adr/0008-ota-github-releases-and-rollback.md) |
| WebUI 交付 | 前端 gzip 後編入 app image，一次 OTA 同時更新韌體與前端；`webfs` 轉為 reserved | [ADR-0016](adr/0016-embed-webui-assets-in-firmware.md) |
| 手動 Recovery AP | 已移除；保留 blank-NVS 與 STA retry exhaustion 的 automatic fallback AP | [WebUI](WEBUI.md)、[Release Checklist](RELEASE_CHECKLIST.md) |

## 已實作但待驗證

| 領域 | 尚未閉環的實機證據 |
| --- | --- |
| Phase 2 display | panel sleep 電流、forced-BUSY isolation |
| Phase 3／4 WebUI | blank-NVS／fallback AP browser flow、SNTP 失敗側 |
| Phase 5 storage | compressed PFR1 與 catalog transaction 中斷電、長時間輪播、imagefs preservation fault injection |
| Phase 6 weather | 面板狀態列視覺結果 |
| Phase 7 sensors | DHT22 讀值、ADC threshold 校正、AWAY/PRESENT、白屏 sleep／返回重繪與環境頁 browser 行為（硬體尚未接線） |
| Phase 8 OTA | **rollback fault injection**、OTA 下載途中斷電 |
| AP grace policy | SSID 可讀性、AP/Wi-Fi 併發刷新、5 分鐘切換、presence 例外與低 DMA heap guard |
| 設定降級邊界 | `nvs_flash_init()` 失敗、NVS 滿導致 `pf_config` 開啟失敗 |

2026-08-20 已閉環（證據見[硬體驗證紀錄](hardware/VALIDATION.md)同日段落）：
OTA 端到端與 rollback confirmation、WebUI 隨韌體換版、reboot persistence、
OTA worker stack high-water、active OTA upload wrapper 的 slot 選擇、面板刷新
耗時（31.2 s）、真實 SNTP、`409 config_read_only`、認證邊界、System 頁瀏覽器
操作，以及 OTA／面板刷新／天氣三者併發下的 heap（天氣在該情境下會因 SSL
配置失敗而降級，屬容量限制而非缺陷）。`mDNS` 從未實作，已不列為待驗證項。

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

## 明確不納入目前 MVP

目前不實作上述 P1 功能，也不納入 MQTT、蜂鳴器、音效或 AI。這些項目保留在
需求與 roadmap 脈絡中，但不應被誤讀成「目前待驗證」。

## 權威關係

- 現行外部行為與資料格式：未被取代的 ADR、[current contracts](AUTHENTICATION.md)。
- 進度摘要：本文件。
- 實機證據與風險：[硬體驗證紀錄](hardware/VALIDATION.md)。
- 發布門檻：[Release Checklist](RELEASE_CHECKLIST.md)。
- 原始需求與歷史計畫：[Guild.md](archive/Guild.md)、[Implementation Plan](archive/IMPLEMENTATION_PLAN.md)。
