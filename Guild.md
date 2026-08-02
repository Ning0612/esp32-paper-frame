# ESP32-S3 電子紙圖片輪播專案需求草案 v0.1

## 一、專案名稱建議

| 顯示名稱             | Repository 名稱          | 定位                             |
| ---------------- | ---------------------- | ------------------------------ |
| **PaperFrame**   | `esp32-paper-frame`    | 最直觀，適合延續 `esp32-hydracup` 命名方式 |
| SpectraFrame     | `esp32-spectra-frame`  | 呼應 E Ink Spectra 6 面板          |
| PaperCarousel    | `esp32-paper-carousel` | 強調圖片輪播                         |
| InkFrame S3      | `esp32-inkframe-s3`    | 強調 ESP32-S3 與電子紙               |
| PaperGallery     | `esp32-paper-gallery`  | 強調圖片庫管理                        |
| Home Paper Frame | `epaper-home-frame`    | 與 `epaper-home-display` 系列感較強  |
**建議採用：**
* 產品名稱：**PaperFrame**
* Repository：**`esp32-paper-frame`**
* 裝置識別字串：`paperframe-s3`
* 預設 mDNS：`paperframe.local`
* AP SSID：`PaperFrame-Setup-XXXX`

名稱簡單、用途明確，也不會把專案綁死在特定尺寸或特定面板世代。

---

# 二、三個參考專案的定位與可移植內容

## 2.1 `epaper-home-display`

這是功能最完整的顯示端參考，包含：

* OpenWeatherMap 即時天氣與預報。
* 電子紙 Dashboard renderer。
* AP Mode 引導畫面。
* 圖片上傳、旋轉、翻轉、裁切、Contain、Stretch、抖動預覽。
* 圖片庫與順序／隨機輪播。
* WebUI 登入、session、CSRF、深淺色主題。
* 系統狀態、事件日誌與環境資料分析。
* 將硬體、業務邏輯、服務、狀態與 WebUI 分層。

圖片處理已定義一致的轉換順序：

1. 水平鏡像。
2. 垂直鏡像。
3. 順時針旋轉。
4. 裁切或 Fit。
5. 縮放。
6. 電子紙色盤量化與 Floyd–Steinberg dithering。
**移植方式：**

不直接移植 FastAPI、Pillow、SQLite 等 Raspberry Pi 元件，而是移植：

* 頁面資訊架構。
* WebUI 視覺設計。
* 圖片操作語意。
* 顯示排程與狀態模型。
* 圖片輪播規則。
* 感測器防抖邏輯。

---

## 2.2 `pico-paper-clock`

這個專案主要提供資源受限裝置的實作參考：

* 圖片串流上傳，不把整張圖片載入 RAM。
* PPC1 圖片格式與固定大小 row buffer。
* 圖片 `.part`、`.bak` 交易式寫入及開機復原。
* 圖片庫、事件圖片與一次性面板預覽。
* 多 Wi-Fi profile。
* 設定檔 schema version 與遷移。
* 光敏電阻的離席／返回防抖。
* 離席後清屏並讓面板進入睡眠。
* 溫溼度感測器讀取失敗時保留最後有效資料，而不是寫入假資料。

圖片 API 採用先寫入暫存檔、驗證大小與解碼結果，再原子替換正式檔案；中途斷電可於下次開機復原。

光敏電阻則使用可調整的離席與返回時間，只有條件持續成立才改變穩定狀態。

**移植方式：**
* 圖片串流與交易式寫入。
* 圖片檔案 header、CRC、版本管理。
* 感測器缺席與錯誤處理。
* 狀態驅動的清屏與睡眠。
* 儲存空間受限時的圖片管理策略。

---

## 2.3 `esp32-hydracup`

這是新專案最適合採用的韌體架構基底：

* PlatformIO＋native ESP-IDF。
* FreeRTOS task 分工。
* NVS 儲存設定。
* LittleFS 儲存 WebUI 與使用者資料。
* AP Mode Config Portal。
* Wi-Fi 掃描 API。
* Normal Mode DashboardServer。
* 單一 server-side session、CSRF 與密碼雜湊。
* HTTP handler 不直接操作即時硬體，而是透過 snapshot 與 command queue 溝通。
* 硬體或周邊初始化失敗時採降級運作，不中止整個裝置。

HydraCup 的 AP Mode 已具備掃描附近 Wi-Fi、手動輸入隱藏 SSID、填寫密碼、儲存並重新啟動的完整流程。

**結論：**

新專案應以 **HydraCup 的 ESP-IDF 架構為主體**，加入：

* `epaper-home-display` 的 renderer、圖片輪播與 WebUI 功能。
* `pico-paper-clock` 的圖片交易、感測器缺席處理與低記憶體設計。

---

# 三、硬體限制與核心設計決策

指定的 7.3 吋 e-Paper HAT (E) 為 800×480、SPI、E6 Full Color 面板，官方標示完整刷新約需 25 秒。官方也建議刷新間隔至少 180 秒，刷新後應讓面板進入 sleep 或切斷電源。

因此本專案採取以下原則：

1. **不設計分鐘級時鐘更新。**
2. 每次圖片輪播都是完整刷新。
3. 預設輪播間隔 30 分鐘。
4. 最短允許間隔建議設定為 5 分鐘。
5. 天氣資料可以每 10 分鐘更新快取，但面板只在下一次圖片刷新時呈現。
6. 每次刷新後立即呼叫面板 sleep。
7. 顯示工作開始時間應提前約 25～30 秒，使完成時間接近排定的整點邊界。

官方 `epd7in3e` driver 以 4-bit 表示一個像素，兩個像素打包成一個 byte；完整 800×480 framebuffer 為 192,000 bytes。現行 driver 的有效色槽為黑、白、黃、紅、藍、綠，Orange slot 目前被註解並映射為黑色。

因此圖片格式必須包含 `palette_version`，MVP 先採官方 driver 實際支援的六個有效色彩，避免 WebUI 預覽與實機顯示不一致。

ESP32-S3 的圖片 framebuffer、縮圖解碼工作區與 WebUI 暫存資料應放入 PSRAM；DMA descriptor、task stack 與 flash 寫入期間必要的工作資料仍放在內部 RAM。ESP-IDF 文件指出 flash cache 關閉時 PSRAM 也不可存取，因此 imagefs 寫入、OTA 與顯示 framebuffer 操作必須透過 storage/display mutex 錯開執行。

開發板版本可能影響可用 GPIO。官方資料指出使用 Octal flash／PSRAM 的模組會占用 GPIO35、GPIO36、GPIO37，不應將這三個腳位配置給電子紙或感測器。

---

# 四、完整功能需求

## 4.1 開機與運作模式

裝置狀態至少包含：

```text
BOOT
 ├─ SELF_TEST
 ├─ CONNECTING_WIFI
 │    ├─ NORMAL
 │    └─ PROVISIONING_AP
 ├─ OFFLINE_RETRY
 ├─ DISPLAY_UPDATING
 ├─ SENSOR_SLEEP
 └─ OTA_UPDATING
```

開機流程：

1. 初始化 NVS。
2. 掛載 `webfs` 與 `imagefs`。
3. 復原未完成的圖片或設定交易。
4. 檢查 Flash、PSRAM 與電子紙 driver。
5. 初始化可選感測器。
6. 載入圖片 catalog。
7. 嘗試連接最後成功的 Wi-Fi。
8. 成功則同步 SNTP、啟動 Normal Mode WebUI。
9. 失敗或未設定 Wi-Fi 則進入 AP Mode。
10. 個別感測器或面板初始化失敗不得造成 boot loop。

Wi-Fi 與 Internet 狀態必須分開：

* Wi-Fi 未連線：進入 AP Mode。
* Wi-Fi 已連線但 OpenWeatherMap 無法存取：保持 Normal Mode並使用最後有效天氣快取。
* DNS、API Key 或 OpenWeatherMap 錯誤不能觸發 AP Mode。

---

## 4.2 AP Mode 與 Wi-Fi 設定

### AP 顯示畫面

電子紙停止圖片輪播，顯示：

* 專案名稱與 AP Mode 標題。
* AP SSID。
* AP Password。
* 固定 IP：`192.168.4.1`。
* 操作步驟。
* Wi-Fi 連線 QR Code。
* WebUI URL 或入口 QR Code。
* 裝置識別碼後四碼。

AP 畫面只在內容改變時更新，不可每 30 秒刷新。

### AP WebUI

AP Mode 頁面包含：

* 掃描 Wi-Fi 按鈕。
* 掃描中狀態。
* SSID 下拉選單。
* RSSI／訊號強度。
* 安全性類型。
* 手動輸入 SSID，支援隱藏網路。
* Wi-Fi Password。
* 顯示／隱藏密碼。
* 儲存並重新啟動。
* 連線失敗原因與再次設定入口。

掃描結果應：

* 依 RSSI 排序。
* 合併重複 SSID。
* 保留目前已選取 SSID。
* 對空 SSID 與無法識別的項目做過濾。
* 不把密碼寫入 URL、log 或 HTTP response。

設定成功後：

1. 交易式寫入 Wi-Fi 憑證。
2. 回傳成功訊息。
3. 延遲約 1 秒重新啟動。
4. 重新嘗試 STA。
5. 失敗則再次進入 AP Mode。

首次尚未建立管理密碼時可直接設定 Wi-Fi；已完成初始設定後的 Recovery AP 必須先登入管理帳號。

---

## 4.3 Normal Mode 圖片顯示

### 橫向模式

* 邏輯解析度：800×480。
* 狀態列：800×40。
* 圖片區：800×440。
* 圖片區 packed payload：176,000 bytes。

### 直向模式

* 邏輯解析度：480×800。
* 狀態列：480×40。
* 圖片區：480×760。
* 圖片區 packed payload：182,400 bytes。
* 完成邏輯畫面後再旋轉成面板原生 800×480 資料。

### 狀態列

預設顯示：

* 日期及星期。
* OpenWeatherMap 天氣圖示。
* 室外即時溫度。
* 天氣資料過期或離線標記。
* 選用：室內溫度與溼度。
* 選用：上次更新時間。

「即時天氣」定義為最近成功取得並快取的資料，不代表面板每分鐘刷新。

### 輪播規則

* 預設間隔：30 分鐘。
* 最短間隔：5 分鐘。
* 模式：順序、隨機。
* 新圖片加入後不得在尚未顯示一次前被自動跳過。
* 手動切換圖片後，該圖片至少顯示一個完整週期。
* 圖片遺失或損毀時自動從有效 playlist 排除。
* 圖片庫為空時顯示內建歡迎／裝置狀態頁。

顯示操作必須由單一 `DisplayTask` 管理；WebUI handler 只能提交 command，不可直接操作 SPI 或面板。

---

## 4.4 OpenWeatherMap 與時間

設定欄位：

* API Key。
* 緯度。
* 經度。
* 顯示地點名稱。
* 單位：攝氏／華氏。
* 語言。
* 天氣更新間隔。
* 時區，預設 `Asia/Taipei`。
* NTP server。

WebUI 必須提供：

* API Key 已設定狀態，不回傳原始值。
* 測試 API 按鈕。
* 測試結果、錯誤原因與取得時間。
* 目前天氣預覽。
* 最後成功更新時間。
* 快取是否過期。

錯誤處理：

* 指數退避重試。
* API Key 無效與一般網路失敗分開顯示。
* 失敗時保留最後有效資料。
* 沒有任何快取時顯示 `--°C` 與離線圖示。
* SNTP 未同步前不得產生錯誤日期；可暫時顯示「時間同步中」。

---

## 4.5 瀏覽器端圖片編輯器

圖片處理應優先完全放在使用者瀏覽器執行，ESP32 不負責處理大型原始照片。

技術方式：

* HTML Canvas。
* Web Worker 執行 dithering，避免主 UI 凍結。
* 所有 JavaScript、CSS、字型及圖示存於 `webfs`。
* 不依賴 CDN，AP Mode 離線時也能使用。

支援功能：

* 拖曳或選擇圖片。
* JPEG、PNG、WebP。
* 自動套用 EXIF orientation。
* 透明區域合成白色背景。
* 順時針旋轉 90°。
* 逆時針旋轉 90°。
* 水平鏡像。
* 垂直鏡像。
* 縮放。
* 拖曳位置。
* 復原。
* 重設。

Fit 模式：

| 模式           | 行為           |
| ------------ | ------------ |
| 填滿 Cover     | 保持比例，裁掉超出區域  |
| 完整顯示 Contain | 保持比例，以白色補足空間 |
| 拉伸 Stretch   | 忽略比例，直接填滿    |
| 手動裁切 Crop    | 使用固定目標比例裁切框  |

Dithering 模式：

* Floyd–Steinberg，預設。
* Atkinson。

Crop 與 Cover 的處理後預覽可用滑鼠／觸控拖曳圖片調整裁切位置，也可用方向鍵
微調；預設位置為置中。來源圖片最多 6,400 萬像素，檔案大小仍限制為 32 MB。

預覽必須同時顯示：

* 原圖。
* 處理後圖片。
* 六色電子紙量化結果。
* 實際狀態列。
* 目前方向與輸出解析度。
* 預估儲存大小。

產生結果後由瀏覽器打包為面板資料，再上傳至裝置。不得上傳未限制大小的原始 RGB framebuffer。

---

## 4.6 圖片資料格式

建議建立專案專用格式，例如 `PFR1`：

```text
magic             4 bytes   "PFR1"
format_version    1 byte
palette_version   1 byte
orientation       1 byte
flags             1 byte
width             2 bytes
height            2 bytes
payload_length    4 bytes
crc32             4 bytes
payload           N bytes
```

必要驗證：

* Magic。
* 版本。
* Width／Height。
* Orientation。
* Palette version。
* Payload length。
* CRC32。
* 剩餘儲存空間。
* 檔名合法性。

圖片寫入流程：

```text
upload.part
  → 驗證 header
  → 串流計算 CRC
  → 驗證 payload
  → 原檔改名為 .bak
  → .part 改名為正式檔
  → 更新 catalog
  → 刪除 .bak
```

開機時必須復原殘留的 `.part` 與 `.bak`。

---

## 4.7 圖片庫

圖片庫頁面包含：

* 縮圖。
* 圖片名稱。
* 建立日期。
* Orientation。
* 圖片大小。
* 儲存空間占用。
* 是否在輪播清單。
* 是否為目前圖片。
* 圖片損毀狀態。

可執行操作：

* 設為目前圖片。
* 立即於下次 display command 顯示。
* 啟用／停用輪播。
* 重新命名。
* 刪除。
* 拖曳調整順序。
* 全選／批次刪除。
* 下載已處理檔案。
* 顯示 storage 使用量。

刪除目前圖片時：

1. 先從 catalog 移除。
2. 選擇下一張有效圖片。
3. 更新 runtime snapshot。
4. 最後刪除實體檔案。

如此即使刪檔失敗，也不會留下指向不存在圖片的 runtime state。

---

## 4.8 溫溼度感測器

溫溼度感測器先完成軟體介面，但預設為未啟用或未安裝。

建議介面：

```cpp
class EnvironmentSensor {
public:
    virtual SensorStatus probe() = 0;
    virtual bool read(EnvironmentReading& output) = 0;
};
```

初期 driver：

* `NullEnvironmentSensor`
* `DHT22EnvironmentSensor`

後續可以增加：

* SHT30／SHT31。
* BME280。
* 其他 I²C 感測器。

狀態模型：

| 狀態             | 說明               |
| -------------- | ---------------- |
| `disabled`     | WebUI 尚未啟用       |
| `probing`      | 正在尋找或等待有效讀值      |
| `online`       | 已取得有效資料          |
| `stale`        | 有舊資料，但近期讀取失敗     |
| `not_detected` | 已啟用但尚未偵測到        |
| `error`        | driver 或 GPIO 錯誤 |

未找到感測器時：

* 裝置正常開機。
* 圖片正常輪播。
* WebUI 顯示「未安裝／未偵測到」。
* API 回傳 `null`，不得回傳 `0`。
* 狀態列隱藏室內資料或顯示 `--`。
* 不建立假的環境歷史紀錄。
* 每隔一段時間低頻重新 probe。
* 保留最後一次有效值，但標記為 stale。

參考專案的 DHT22 實作會限制讀取頻率、驗證數值範圍，失敗時保留最後成功資料並延長重試間隔。

---

## 4.9 光敏電阻與在場機制

光敏電阻同樣採可選 driver：

```cpp
class LightSensor {
public:
    virtual SensorStatus probe() = 0;
    virtual bool readRaw(uint16_t& output) = 0;
};
```

狀態：

* `disabled`
* `online`
* `not_detected`
* `saturated`
* `error`

光敏電阻未安裝或未啟用時：

* `presence = UNKNOWN`。
* 不清除面板。
* 不暫停圖片輪播。
* 不把 ADC 浮動值視為離席。

啟用且正常時：

1. 定期讀取 ADC。
2. 套用 moving average 或 median filter。
3. 與可調整 threshold 比較。
4. 狀態持續達指定時間後才切換。

預設值建議：

* 離席持續時間：180 秒。
* 返回持續時間：30 秒。
* WebUI 可調整 threshold、離席時間與返回時間。

離席後：

* 只執行一次全白刷新。
* 面板進入 sleep。
* 暫停圖片輪播與天氣畫面刷新。
* 感測器、Wi-Fi 與 WebUI 繼續運作。

返回後：

* 立即喚醒並重新初始化面板。
* 使用目前應顯示的圖片與最新狀態資料完整重繪。
* 輪播計時重新開始。
* 不使用離席前殘留的刷新 deadline。

這與兩個電子紙參考專案的狀態驅動模式一致：必須根據目前穩定狀態收斂到清屏／睡眠，而不能只處理狀態切換瞬間。

WebUI 應提供即時 ADC 數值、threshold 標記及目前判定狀態，方便安裝後校正。

---

# 五、WebUI 資訊架構

## 5.1 頁面

### Dashboard

顯示：

* 裝置運作狀態。
* 目前圖片。
* 下次輪播時間。
* Wi-Fi、Internet、SNTP。
* 天氣快取狀態。
* 電子紙狀態。
* 溫溼度感測器狀態。
* 光敏電阻與在場狀態。
* Flash、PSRAM、imagefs 使用量。
* 韌體版本與 uptime。

### 圖片

分成兩個分頁：

* 圖片編輯與上傳。
* 圖片庫與輪播管理。

### 環境

即使尚未裝感測器也保留此頁面：

* 感測器安裝狀態。
* GPIO 與 driver 類型。
* 目前溫溼度。
* 今日 min／max／avg。
* 光線 ADC 與 threshold。
* 在場狀態。
* 後續可加入日／週／月趨勢圖。

### 設定

分類：

* 顯示器。
* 圖片輪播。
* 天氣。
* 時間與時區。
* Wi-Fi。
* 溫溼度感測器。
* 光敏電阻。
* 帳號與密碼。
* 系統與更新。

### 系統

* 系統事件。
* 最近刷新結果。
* BUSY timeout 次數。
* Wi-Fi 重連次數。
* Weather API 錯誤。
* 感測器錯誤。
* 下載診斷資料。
* 重新啟動。
* 進入 Recovery AP。
* OTA 更新。

---

## 5.2 統一視覺設計

延續三個專案既有風格：

* 米白網格背景。
* Teal、Coral、Mint 主色。
* 方角元件。
* 卡片硬陰影。
* Georgia／`Noto Serif TC` 標題。
* Consolas／monospace 數字與控制項。
* LIGHT／DARK 切換。
* 共用 localStorage key：`iot-ui-theme`。
* 1040px 主內容寬度。
* 行動裝置 responsive layout。

新專案應將這套樣式抽成：

```text
data/
├── style.css
├── ui.js
├── favicon.svg
├── login.html
├── index.html
├── images.html
├── environment.html
└── settings.html
```

不得在每個 HTML 內複製完整 CSS。

---

# 六、認證與安全需求

沿用 HydraCup 與兩個電子紙專案的模式：

* 固定帳號 `admin`。
* 首次使用建立至少 8 字元密碼。
* PBKDF2-HMAC-SHA256 或同等強度密碼雜湊。
* 單一 server-side session。
* 新登入取代舊 session。
* 閒置 30 分鐘失效。
* 最長 24 小時失效。
* `HttpOnly`。
* `SameSite=Strict`。
* 所有 POST、PUT、DELETE 需要 CSRF token。
* Secret 欄位只回傳 `*_set: true/false`。
* 不提供 CORS。
* `/api/v1/device` 與 health endpoint 可不登入。
* Recovery AP 已設定密碼時必須先登入。

WebUI 預設為區域網路 HTTP，因此頁面需註明只能在可信任 LAN 或裝置 AP 使用。

---

# 七、建議 REST API

| Method | Path                             | 用途                  |
| ------ | -------------------------------- | ------------------- |
| GET    | `/api/v1/device`                 | 裝置識別、API version、容量 |
| GET    | `/api/v1/status`                 | 完整 runtime snapshot |
| GET    | `/api/v1/auth/status`            | 登入狀態與 CSRF          |
| POST   | `/api/v1/auth/login`             | 建立密碼或登入             |
| POST   | `/api/v1/auth/logout`            | 登出                  |
| GET    | `/api/v1/wifi/scan`              | 掃描 Wi-Fi            |
| POST   | `/api/v1/wifi/config`            | 儲存 Wi-Fi 並重啟        |
| GET    | `/api/v1/config`                 | 讀取遮蔽後的設定            |
| POST   | `/api/v1/config`                 | 更新設定                |
| POST   | `/api/v1/weather/test`           | 測試 OpenWeatherMap   |
| GET    | `/api/v1/images`                 | 圖片 catalog          |
| PUT    | `/api/v1/images/{name}`          | 串流上傳已處理圖片           |
| DELETE | `/api/v1/images/{name}`          | 刪除圖片                |
| POST   | `/api/v1/images/{name}/activate` | 設為目前圖片              |
| POST   | `/api/v1/images/advance`         | 手動下一張               |
| POST   | `/api/v1/images/order`           | 更新輪播順序              |
| GET    | `/api/v1/sensors`                | 感測器與讀值              |
| GET    | `/api/v1/events`                 | 系統事件                |
| POST   | `/api/v1/system/reboot`          | 重新啟動                |
| POST   | `/api/v1/system/recovery-ap`     | 進入 Recovery AP      |
| POST   | `/api/v1/system/ota`             | 本機 OTA              |

統一回應格式：

```json
{
  "ok": true,
  "data": {}
}
```

錯誤格式：

```json
{
  "ok": false,
  "error": "storage_full",
  "message": "圖片儲存空間不足"
}
```

---

# 八、儲存與 Partition 規劃

16 MB Flash 建議至少分為：

* NVS。
* OTA metadata。
* App Slot A。
* App Slot B。
* `webfs`。
* `imagefs`。
* 選用 coredump。

`webfs` 與 `imagefs` 必須分離：

* 更新 WebUI 不得刪除使用者圖片。
* OTA 不得覆寫圖片。
* 圖片格式更新需由 migration 處理。
* `uploadfs` 只更新 `webfs`。

實際 partition 大小應在第一次完整編譯後，依韌體大小決定；但必須從第一版就保留雙 OTA slot，避免日後修改 partition table 導致使用者圖片被清除。

設定使用 NVS，並包含：

* `schema_version`
* Wi-Fi
* WebUI auth
* display
* carousel
* weather
* timezone
* environment sensor
* light sensor
* last successful image
* boot／OTA state

圖片 catalog 存於 `imagefs`，採交易式 JSON 或小型 binary index。

---

# 九、FreeRTOS 架構

```text
app_main
 ├─ ConfigManager
 ├─ FileSystemManager
 ├─ RuntimeCoordinator
 ├─ DisplayTask
 ├─ SensorTask
 ├─ NetworkServiceTask
 ├─ WeatherWorker
 ├─ StorageWorker
 └─ esp_http_server
```

### DisplayTask

* 電子紙唯一 owner。
* 負責 SPI、BUSY、reset、sleep。
* 負責組合狀態列與圖片資料。
* BUSY 必須有 timeout。
* 刷新過程不得阻塞 HTTP server。

### SensorTask

* 溫溼度 probe／read。
* 光線 ADC 取樣與濾波。
* 在場防抖。
* 發送 `presence_return`／`presence_away` command。

### NetworkServiceTask

* Wi-Fi STA／AP。
* SNTP。
* mDNS。
* 重連與連線健康狀態。

### WeatherWorker

* OpenWeatherMap HTTPS。
* 快取與退避。
* 不直接操作面板。

### StorageWorker

* 圖片串流寫入。
* CRC。
* Catalog 更新。
* OTA／NVS／LittleFS 操作序列化。

### RuntimeCoordinator

* 唯一 runtime snapshot。
* Command queue。
* Result queue。
* HTTP server 只讀 snapshot 或送 command。

---

# 十、建議加入但可排在第二階段的功能

## P1

* 多 Wi-Fi profile。
* WebUI OTA。
* 設定與圖片 catalog 匯出／匯入。
* 每張圖片自訂顯示時間。
* 每週顯示排程與安靜時段。
* 圖片批次上傳。
* 圖片標籤與篩選。
* 溫溼度歷史圖表。
* 光線與在場歷史。
* Discord 裝置上線／錯誤通知。
* 自動清理最舊圖片。
* GPIO pin map WebUI 顯示。
* 面板測試色塊與接線診斷頁。

## 暫不納入

下列功能雖存在於參考專案，但與本專案核心用途關聯較低：

* Claude／Codex 使用量。
* HydraCup MQTT。
* Bambu Lab MQTT。
* 蜂鳴器與整點提示。
* 音效播放。
* 完整書桌前年度熱力圖。
* AI 語音提醒。

---

# 十一、MVP 驗收條件

| 情境                | 驗收結果                            |
| ----------------- | ------------------------------- |
| NVS 無 Wi-Fi       | 自動進入 AP，面板顯示 SSID、密碼及 IP        |
| AP 掃描 Wi-Fi       | 清單正確顯示並可帶入 SSID                 |
| 儲存 Wi-Fi          | 回應成功後重啟並連上 STA                  |
| Wi-Fi 密碼錯誤        | 連線逾時後重新進入 AP                    |
| OpenWeatherMap 離線 | 圖片仍輪播，顯示最後快取與 stale 狀態          |
| 溫溼度感測器未安裝         | WebUI 顯示未偵測，API 為 `null`，裝置正常運作 |
| 光敏電阻未安裝           | `presence=UNKNOWN`，不得清屏或暫停輪播    |
| 光線持續低於／高於條件       | 達防抖時間後才切換在場狀態                   |
| 判定離席              | 全白刷新一次並 sleep                   |
| 判定返回              | 立即重繪目前圖片                        |
| 橫向圖片              | 精確輸出 800×440 圖片區                |
| 直向圖片              | 精確輸出 480×760 圖片區並正確旋轉           |
| 圖片上傳中斷電           | 下次開機不出現半張圖片，能復原 catalog         |
| CRC 錯誤            | 拒絕圖片且不覆寫既有檔案                    |
| imagefs 空間不足      | 明確回傳 507 類型錯誤                   |
| 面板 BUSY timeout   | WebUI 保持可用並記錄錯誤                 |
| 新圖片手動啟用           | 至少完整顯示一個輪播週期                    |
| 未登入修改設定           | 回傳 401                          |
| 缺少 CSRF 修改設定      | 拒絕請求                            |
| 刷新完成              | 面板進入 sleep                      |
| 裝置重新啟動            | 圖片、順序、設定與目前圖片完整保留               |

---

# 十二、建議的第一版開發範圍

第一版應完成：

1. ESP-IDF／PlatformIO 專案骨架。
2. NVS、`webfs`、`imagefs` 與雙 OTA partition。
3. `epd7in3e` ESP-IDF driver。
4. AP／STA 狀態機。
5. 登入、session 與 CSRF。
6. 統一 WebUI theme。
7. 圖片瀏覽器端編輯、六色 dithering 與 PFR1 打包。
8. 圖片交易式上傳與 catalog。
9. 橫向／直向 renderer。
10. 30 分鐘輪播排程。
11. OpenWeatherMap 與 SNTP。
12. 溫溼度及光敏電阻的 Null driver、狀態與 WebUI。
13. 光敏電阻防抖、離席清屏、返回重繪。
14. 系統狀態與基本診斷 API。

這個範圍完成後，即使兩個感測器都尚未安裝，裝置仍是一個完整可用的電子紙圖片輪播系統；感測器接上後只需啟用對應 driver 與 GPIO 設定，不需重構 WebUI、API 或主流程。
