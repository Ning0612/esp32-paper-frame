# ADR-0019：把「畫面已上到面板」與「面板已進入 deep sleep」分開回報

- Status: accepted
- Date: 2026-08-23
- Supersedes: [ADR-0003](0003-fix-phase2-display-integration.md) Verification 段的
  「成功 result 只能在 driver 回報 deep sleep 後發布」——該條款是本問題的來源。
  ADR-0003 的 **driver 行為**（完整送出 `0x07, 0xA5` 才標記 deep sleep、
  BUSY timeout 後 panel state 設為 unknown、下一筆 command 從 hardware reset
  重來）**維持不變**，本 ADR 只改變 driver 結果如何被**回報**給上層。

## Context

`DisplayTask::process()` 把 driver 結果映射成 `DisplayOutcome` 時，只有
「refresh 完成**且** panel 確實進入 deep sleep」才回報 `refreshed_and_slept`；
其餘一律是錯誤值。問題在於刷新序列的後段：

```
3. DISPLAY_REFRESH (0x12), BUSY wait   ← 走完這步，畫面已經在面板上
4. POWER_OFF (0x02), BUSY wait
5. POWER_OFF (0x02), BUSY wait
6. DEEP_SLEEP (0x07), 0xA5
```

第 3 步之後**畫面就已經正確顯示**。若第 4–6 步任一失敗，上層收到的是
`panel_state_error`／`busy_timeout`，而這個結果讀起來像「這次刷新沒有成功」——
於是上層安排重刷。**重刷的是一張已經正確的畫面**，代價是又一次 31 秒全刷與
對應的面板壽命消耗。

`pf_carousel` 的 welcome 重試退避註解已經記錄了這個問題並用指數退避壓制它
（否則固定 30 秒重試會變成每天約 1,400 次刷新）。那是症狀處理：退避降低了
損害頻率，但每次退避結束仍會重刷一張正確的畫面，而且它只保護 welcome frame，
不保護一般輪播與離席白屏。

`docs/PROJECT_STATUS.md` 自 2026-08-23 起把這件事列為已知遺留缺陷，並記載
「正確修法需拆開結果契約並取代 ADR-0003 的 driver contract」。本 ADR 即為該文件。

## Decision

### 在 `RuntimeResult` 增加 `frame_on_panel`

```cpp
struct RuntimeResult {
    std::uint32_t request_id;
    ResultStatus status;
    RuntimeError error;
    DisplayOutcome display_outcome;
    std::uint8_t driver_stage;
    bool frame_on_panel;      // 新增
};
```

`frame_on_panel` 回答「這張畫面是否已經送上面板」，與 `display_outcome`
回答的「這次 command 發生了什麼」正交。

**不採用**「新增 `DisplayOutcome::refreshed_not_slept` 列舉值」的做法。理由：
那會讓一個列舉同時承載兩個維度，錯誤分類會被吃掉——例如 sleep 階段的
BUSY timeout 就無法同時表達「畫面已顯示」與「這是 timeout 而非 panel state
錯誤」。`display_outcome` 的字串詞彙也是 `GET /api/v1/status` 的公開介面，
維持不變可避免既有消費者需要跟著改。

### 判定門檻：`DriverStage` 走過 `refresh` 才算

```cpp
frame_on_panel = driver.stage >= DriverStage::refresh_power_off;
```

`DriverResult::stage` 在失敗時代表失敗發生的階段。門檻取
`refresh_power_off`（而非 `refresh`）是**刻意保守**：`refresh` 階段的
BUSY timeout 代表面板從未確認刷新完成，此時不能宣稱畫面已顯示。

### 上層規則：畫面已在面板上就不重刷

- `frame_on_panel == true` 且 `display_outcome != refreshed_and_slept`：
  **不重新提交這張畫面**。面板停留在 active 而非 deep sleep，代價是耗電，
  不是顯示錯誤。下一次刷新會從 hardware reset 重來（ADR-0003 既有行為），
  屆時面板狀態恢復——**但那次刷新不保證會發生**，見 Consequences。
  `RuntimeCoordinator` 對任何非 `refreshed_and_slept` 的結果都會記一筆
  display diagnostic event，所以這個狀態在 System 頁是可觀察的。
- `frame_on_panel == false`：維持既有的重試／退避行為。

## Consequences

- 刷新成功但 sleep 失敗時，不再浪費一次 31 秒全刷。這是本 ADR 的主要目的。
- **面板可能停留在 active 狀態直到下一次刷新，而有兩個狀態不保證會有下一次**：
  （1）空圖片庫時 welcome frame 一旦成功顯示，`CarouselScheduler` 就把
  `next_due_ms` 設為 `UINT64_MAX` 永久停等；（2）provisioning AP 畫面在
  payload 未變時會跳過刷新。第（2）點在本 ADR 的實作中不成立——AP 路徑
  刻意維持以 `refreshed_and_slept` 為準（見 PROVISIONING.md 的「AP radio
  只有在回報 `refreshed_and_slept` 後才啟動」），sleep 失敗時會重試整次
  刷新。第（1）點是**已知且未解的殘留風險**：空圖片庫 + welcome sleep 失敗
  時，面板會維持 active 直到下次開機或使用者上傳圖片。觸發條件罕見，
  且 diagnostic event 會記錄，但這是真實的功耗與面板壽命風險。
- 本 ADR **不**新增「只 sleep 不刷新」的 command：ADR-0003 的 driver 只暴露
  `refresh_and_sleep()`，新增 sleep-only 進入點會擴大 driver 契約與
  DisplayTask 的狀態機。那正是上一點殘留風險的正解，留待實機證據顯示這個
  代價顯著時再以新 ADR 處理。
- `pf_carousel` 的 welcome 指數退避**保留**。它現在保護的是真正沒上到面板的
  失敗（transport error、refresh 階段 timeout），退避理由仍然成立。
- `RuntimeResult` 由 8 bytes 增為 12 bytes（含 padding）。它是 static queue
  的元素，佇列深度小，記憶體影響可忽略；`std::is_trivially_copyable_v`
  仍然成立。
- `GET /api/v1/status` 的 `display.last_outcome` 字串集合不變。

## Verification

- Host test：driver 在 `refresh_power_off`、`sleep_power_off`、`deep_sleep`
  階段失敗時，`frame_on_panel` 為 true；在 `validate`、`reset`、`frame_write`、
  `refresh` 階段失敗時為 false。
- Host test：`frame_on_panel == true` 的非成功結果不觸發重新提交。
- 既有的 DisplayTask host/embedded test 對 result mapping 的斷言全數維持通過
  ——`display_outcome` 的映射沒有改變。
