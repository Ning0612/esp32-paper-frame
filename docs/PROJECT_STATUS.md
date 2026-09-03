# PaperFrame 專案狀態

- 最後整理：2026-09-03（目前發布版本：`v0.11.3`）
- 本文件是目前進度的唯一摘要入口；詳細實機證據仍保留在
  [硬體驗證紀錄](hardware/VALIDATION.md)。
- `已完成` 代表程式／host/build 已完成；只有明確標成 `已驗證` 才代表有實機
  證據。`待決定` 不等於 bug，也不應在決策前自行擴張 scope。

## 一分鐘結論

**MVP 功能範圍已完成**（2026-08-23 由專案擁有者判定）：Phase 1–8 的功能、
WebUI、OTA、感測器與外殼 CAD 都已交付，並在實機上使用。這是**功能範圍**
的判定，不代表每條路徑都有實機證據——剩餘缺口見下方兩節，都是低優先項。

**電源方案尚未設計**：目前直接接線供電，沒有評估過電池、UPS 或低功耗
運行模式。這是 MVP 之後的第一個硬體待辦。

Phase 1–8 的主要程式、host tests 與韌體 build 已完成。截至 2026-08-20，
Phase 2／3／4／5／6／8 的主要實機證據已閉環——涵蓋 OTA 端到端與 rollback fault
injection、五種斷電路徑、AP provisioning 與存取邊界、browser 出圖管線、天氣四種
失敗分類、forced-BUSY 隔離與面板 sleep 電流。Phase 7 感測器已於 2026-08-23 接線
並完成主要實機驗證（DHT22 讀值、雙光敏通道校正、AWAY/PRESENT 轉換、白屏與
返回重繪）。**剩餘的硬體驗證是七條低優先路徑**，分佈在四個領域（原第八條
「低 DMA heap guard」已於 2026-08-25 確認唯一路徑已移除，改記為無現存路徑
可測，見[硬體驗證紀錄](hardware/VALIDATION.md)）。其餘待辦是公開 release
security profile 與 MVP 以外功能的產品決策。

## 已完成或已決定

| 領域 | 狀態 | 權威文件 |
| --- | --- | --- |
| Phase 1 runtime／storage／health foundation | 程式、host／embedded test、build、boot／mount 已完成或驗證 | [歷史 Implementation Plan](archive/IMPLEMENTATION_PLAN.md) |
| Phase 2 display／renderer／DisplayTask | renderer、owner contract、carousel core、welcome lifecycle 已完成；六色 pattern 有實機證據 | [Display ADR](adr/0003-fix-phase2-display-integration.md) |
| Phase 3 provisioning／auth／WebUI | AP／STA、portal、sync auth／CSRF、管理 shell 與 Dashboard 已完成程式、host/build、STA smoke；2026-08-25 新增繁體中文／英文語言切換，已完成程式並實機驗證（6 個 view × 兩種語言） | [Auth](AUTHENTICATION.md)、[WebUI](WEBUI.md) |
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
| Phase 7 sensors | 剩 `SensorSettings` v1→v2 遷移的實機路徑，以及**啟用但實體未接線**的通道會讀到什麼（浮接 ADC）；兩顆都接線、以及只接一顆並正確停用另一顆的行為，均已於 2026-08-23 實機閉環 |
| Welcome／重繪生命週期 | presence 返回時的 welcome 重畫、DHCP 續約後的位址重畫、welcome 刷新失敗後的 30 秒短重試（見 [ADR-0015](adr/0015-first-image-waits-for-ntp-and-weather.md)） |
| AP grace policy | presence 例外（需感測器） |
| 設定降級邊界 | NVS 滿導致 `pf_config` 開啟失敗（低風險） |

**2026-08-25 已修**：AP 畫面呈現完成到 AP radio 實際啟動之間的空窗——
`present_access_point_screen()` 成功後釋放 `display_submission_mutex` 並回傳
`ESP_OK`，NetworkService 才繼續啟動 radio；這段期間 carousel 可能讀到落後的
`ap_screen_owns_panel` verdict 取得 gate 並把面板換成輪播畫面（若 radio 啟動失敗，
`presentation_confirmed_` 已為 true，後續重試還會跳過 presenter 沿用被覆寫的面板）。

修法不是逐一列舉交錯順序，而是讓 `RuntimeCoordinator` 發佈一個單調遞增的
`ap_session_id`（每次真正進入新的 `starting_ap` 都 +1，含同一 AP mode 內從
`provisioning` 退回 `starting_ap` 的重入）；`app_main` 主迴圈與
`try_lock_carousel_submission_gate()` 都改成比對這個 session id，不等即代表
「有變化，不管中間細節，一律安全地拒絕／重新同步」。`presentation_confirmed_`
本身也已移除——`start_access_point()` 現在無條件呼叫 presenter，改由
`AccessPointScreenCache::shows()`（已正確處理 `mark_superseded()`）決定要不要
真正重繪。決策邏輯抽成 `pf_network::classify_ap_mode_window()` 與
`submission_gate_denies_for_ap_session()` 兩個純函式（`ap_screen.hpp`），有
host test 覆蓋。經 codex-cowork 三輪審查（2026-08-25，`gpt-5.6-terra`×high）：
第一輪與第二輪各抓到一個 High 並已修正（第二輪的 High 是修第一輪時自己引入的
迴歸——離開 AP mode 後本地 session id 歸零，但共享 snapshot 的 id 不會歸零，
導致 carousel 被永久拒絕；修法是把 session id 比對限定在「fresh snapshot 顯示
仍在 AP mode」時才生效）；第三輪回報的 High（同一 session 內
`starting_ap→provisioning` 的 ready 轉換可能被漏擋）經逐行追蹤程式碼與新增的
`test_became_ready_transition_still_denies_within_same_tick` host test 驗證
**無法在目前程式碼重現**——`became_ready` 分支把 `ap_mode_ready` 與
`ap_mode_started_ms` 原子綁在同一個 tick 設定，該 tick 內任何後續 gate 呼叫都會
用到剛更新的值，不會出現 codex 描述的「verdict 已是 false」狀態；判斷為 codex
只看 diff hunk、未看到未變動的 `ap_screen_owns_panel` lambda 全貌所致的誤判，
已記錄不採用理由並以可執行測試佐證（單批審查輪數上限已達 3 輪）。
一個 Low（`ap_session_id` 是 `uint32_t`，回繞需約 42 億次進入 `starting_ap`）
未修，比照 `diagnostics_event.hpp` 既有的 `sequence_id` wraparound
「Known limitation (accepted, not fixed)」先例。
`pio test -e native`（含新增測試）與 `bash scripts/verify-like-ci.sh` 全綠；
**同日以實機驗證 AP fallback 路徑**（WebUI 故意存入錯誤 Wi-Fi 密碼並重開機，
觸發 STA 重試耗盡 → provisioning AP，serial log 確認 radio 嚴格晚於
`provisioning_screen_ready` 才啟動，使用者目視確認面板正確顯示
SSID／密碼，未被輪播畫面覆寫）；原始 race 本身（跨 task 排程時序）仍未在
實機刻意重現，細節見[硬體驗證紀錄](hardware/VALIDATION.md)。PROVISIONING.md
的既有契約文字不變（修的是實作與 snapshot 之間的同步，不是放寬或更動契約
本身）。

**2026-08-25 新增並實機驗證**：WebUI 新增繁體中文／英文語言切換
（`data/web/i18n.js`，切換即整頁 reload，字典與套用機制細節見
[WebUI](WEBUI.md)）。實機測試（登入後對 6 個 view × 兩種語言逐一操作）
額外發現並修掉三個既有排版缺陷，都不是語言切換造成的：
(1) `weather-form`／`timezone-form` 的 `style="display: contents"`
被裝置實際送出的 CSP（`style-src 'self'`，無 unsafe-inline）擋掉，改用
`.form-contents` class；(2) 登入後 nav bar 出現時 `.topbar-actions` 擠壓
`.brand-lockup`，標題文字換行到只剩最長單字寬度——中文標題早就有這個問題
（登入後會斷成 2–3 行），只是字短不顯眼，英文標題斷行才被使用者發現，修法是
幫 `.topbar` 加 `flex-wrap: wrap` 讓標題與 nav 整體掉成兩行而非文字內部斷行；
(3) 頂端 nav 按鈕的 `justify-content: space-between` 在無多餘空間可分配時
（英文標籤較長）會讓文字貼齊徽章數字，加 `gap: 6px` 保底。以上三項與語言
字典本身均已在實機用 playwright 截圖與 DOM 量測驗證。

**2026-08-23 發現並修復**：provisioning AP 畫面的 payload cache
一旦設立就不再重設（`src/app_main.cpp` 的 `payload_valid` 只有設為 `true` 的
路徑，全專案沒有任何地方把它設回 `false`）。同一次開機內 AP password 固定，
所以第二次進入 AP（例如 STA 失敗 fallback）時 payload 相同會命中 cache，
函式開頭直接 `return ESP_OK` 跳過刷新——**AP radio 會啟動，但電子紙可能還顯示
著 carousel 圖片，使用者看不到 SSID、密碼與 QR code**，Recovery AP 因此可能
無法使用。此缺陷早於今日的顯示結果契約變更，由 2026-08-23 的 codex 審查發現。
修法：把那兩個欄位收攏成 `pf_network::AccessPointScreenCache`，`invalidate()`
成為可被找到與測試的呼叫點，並在任何其他畫面上到面板時呼叫它。

**2026-08-23 已修**（同日發現、同日修）：`DisplayOutcome` 把「畫面已刷上去」
與「面板已成功 sleep」混為一談，sleep 失敗時會重刷一張已經正確的畫面；以及
開機時 presence 由 `unknown` 收斂到 `present` 被當成「返回」而多觸發一次 31 秒
全刷，裝置其實從未離席（兩次重開機完整重現，見[硬體驗證紀錄](hardware/VALIDATION.md)
同日 release gate 段落）。前者是後者浪費一次刷新的成因之一。修法見
[ADR-0019](adr/0019-separate-frame-displayed-from-panel-slept.md)：`RuntimeResult`
新增 `frame_on_panel` 把兩個事實分開回報，離席／返回的面板動作改由可 host-test
的 `pf_sensors::presence_panel_action()` 決定。修正後的開機序列**已實機確認只剩一次全刷**（見[硬體驗證紀錄](hardware/VALIDATION.md)
同日「開機多餘全刷的修正驗證」段落）。審查過程中另修掉數個併發情境，那些需要
刻意製造的時序，僅由 host test 與審查覆蓋，未在實機重現。

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
2. **~~MVP release gate~~**（2026-08-23 已決定）：不要求所有硬體證據關閉才
   發布。剩餘七條路徑都是低優先且觸發條件罕見，逐項列在上表與
   [硬體驗證紀錄](hardware/VALIDATION.md)，發布不隱藏它們。
3. **電源方案**：目前直接接線供電。是否要做電池／UPS、目標續航、以及對應的
   低功耗運行模式（面板本身已在每次刷新後 sleep，主控未做 deep sleep）。
4. **MVP 以外的 P1 功能**：多 Wi-Fi profile、批次上傳、週排程、歷史圖表、
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
