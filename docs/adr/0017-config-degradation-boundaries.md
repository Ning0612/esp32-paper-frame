# ADR-0017：設定載入的降級邊界與中央記錄的寫入規則

- Status: accepted
- Date: 2026-08-20
- Related: ADR-0008（OTA 與 rollback）、ADR-0016（WebUI 編入 app image）

## Context

`pf_config::initialize()` 讀取中央設定記錄（`pf_config` namespace：
`refresh_minutes`、`timezone`、`carousel_random`）並回傳一個 `SchemaAction`。
其餘設定各自存放於獨立 namespace：`pf_wifi`（Wi-Fi 憑證）、認證（管理密碼）、
weather、sensor，各有自己的驗證與 CRC。

實作上卻把兩者綁在一起：`src/app_main.cpp` 有四處設定載入與兩處服務啟動
（`ProvisioningService`、`AuthService`）全部 gate 在 `config_result.error ==
ESP_OK`。中央 schema 一旦無法解讀，這些完全不相關的設定與服務就一起停擺。

**這個耦合最糟的引爆點正是 OTA rollback。** 新韌體 bump
`kCurrentSchemaVersion` 後若在
`esp_ota_mark_app_valid_cancel_rollback()` 之前 crash-loop，bootloader 會依
設計回滾到舊韌體——而舊韌體把新版 schema 判為 `reject_future`
（`schema.hpp`）→ `ESP_ERR_NOT_SUPPORTED`（`config_manager.cpp`）→ 跳過上述
全部載入與服務。裝置開機後進入 provisioning AP，看起來像被 factory reset，
實際上每一項設定都完好地躺在 NVS 裡。**本應救援裝置的回滾機制，表現得像是
清空了裝置。**

同時發現：中央記錄的寫入路徑（`save_config()`，唯一呼叫端是 carousel 設定
handler）完全不檢查目前 schema 狀態，會把執行中韌體的
`kCurrentSchemaVersion` 蓋到記錄上。

## Decision

### 1. 獨立 namespace 的載入與服務啟動，只 gate 在「NVS 子系統是否可用」

`StartupResult` 新增 `bool nvs_initialized`，**只有** `nvs_flash_init()` 本身
失敗時為 `false`。`app_main` 的四處載入與兩處服務啟動改用它。

不使用 `SchemaAction::unavailable` 作為判準：該 action 同時涵蓋
「`nvs_flash_init()` 失敗」與「僅 `pf_config` namespace 開啟失敗」（例如
NVS 滿），而兩者需要相反的反應——前者代表沒有任何 namespace 可讀，後者對
其他 namespace 毫無指涉。

`reject_future`／`reject_corrupt` 意味著 schema 無法解讀，不代表 NVS 不可用。
每個 loader 各自驗證自己的 blob 並回報自己的錯誤，因此這個判斷若有偏差，
代價是一筆 log 而非壞資料。

### 2. 只有本韌體確實解析成功的記錄，才可以被寫回

`schema_allows_write(SchemaAction)`（header-only `constexpr`，逐 action 有
host test）只允許 `use_current`、`migrate_v0`、`migrate_v1`、
`initialize_defaults`。`initialize()` 依 `plan.action` latch 一個
`std::atomic<bool>`（進入時先清除，使早退路徑無法繼承前次權限），
`save_config()` 在不可寫時回 `ESP_ERR_INVALID_STATE`。

- **`reject_future` 不可寫**：該記錄對新韌體完全有效，覆寫等於降版破壞。
- **`reject_corrupt` 不可寫**：這一條經過反覆修正才定案。允許寫入看似能提供
  修復途徑，實際上做不到——被拒絕的 plan 回報 `record_available=false`，
  `app_main` 因此丟棄 `make_startup_plan()` **已成功解析**的欄位並改用
  placeholder（`timezone` 變成字面值 `"unknown"`）。carousel handler 是從
  這份 runtime 狀態組出要儲存的記錄，所以使用者只要改一次輪播間隔，就會把
  `"unknown"` 寫到原本完全有效的 timezone 上，而 UI 沒有任何地方能改回來。
  **修好一個損壞欄位卻靜默毀掉旁邊的欄位，比不修更糟。**

### 3. 拒絕儲存回 `409 config_read_only`

與真正的儲存故障（`503 storage_unavailable`）區分，讓使用者停止重試一個
不可能成功的操作。回應**不猜測**原因：`ESP_ERR_INVALID_STATE` 對
future schema 與 corrupt 資料皆會回傳，確切原因在開機 log。

## Consequences

- OTA rollback 回到舊韌體時，Wi-Fi、管理密碼、weather、sensor 設定全部保留，
  裝置可正常連線與登入；只有中央設定顯示 `config: degraded` 且暫時唯讀。
  這正是自動回滾原本該有的行為。
- **已知限制**：中央記錄損壞（`reject_corrupt`）時維持唯讀，沒有正常的修復
  路徑，需要清除 NVS。真正的修復需要一個「從預設值出發」的重置流程，而不是
  從半殘的 runtime 狀態出發；本 ADR 不涵蓋該流程。
- `save_config()` 目前唯一呼叫端是 carousel 設定 handler。未來新增呼叫端時，
  這個限制同樣適用，錯誤處理需比照。
- `app_main` 無法 host-test，因此本決策的行為驗證依賴實機重現（見下）。

## Verification

- `pio test -e native` 308/308，含 `schema_allows_write` 逐 action 的 host
  test，以及「遷移必須保留既有欄位」的回歸測試。
- `pio run` 成功；全部 `test/web/*.mjs` 通過。
- **實機紅綠**（2026-08-20）：以 `kCurrentSchemaVersion = 1` 建置，讓 NVS 中
  既有的 version 2 記錄看起來像未來版本，精確重現 rollback 情境。
  - 修正前：serial log 出現
    `provisioning_ap_ready ssid=PaperFrame-Setup-...`，設定全數失效。
  - 修正後：`/api/v1/health` 回 `wifi: "connected"`、`config: "degraded"`；
    以**錯誤密碼** `POST /api/v1/auth/login` 回 **401**（服務已啟動並實際
    驗證憑證）而非 503。
  - 還原為 version 2 後裝置回到 `config: "ready"`。
- **驗證方法的教訓**：先前曾以 `/api/v1/auth/status` 回報
  `password_configured: true` 作為「管理密碼載入成功」的證據，這是假陽性
  ——`AuthService::authenticate_request()` 在服務未初始化時刻意 fail-closed
  回傳 `true`。能區分兩種狀態的探針是「用錯誤密碼登入」（401 vs 503）。
- **未驗證**：登入後修改設定被拒（`409 config_read_only`）的端到端路徑，
  需要管理密碼；`nvs_flash_init()` 失敗與 NVS 滿導致 `pf_config` 開啟失敗
  這兩條路徑未在實機重現。
