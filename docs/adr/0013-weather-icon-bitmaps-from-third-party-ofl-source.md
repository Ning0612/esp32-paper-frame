# ADR-0013：天氣圖示改用轉檔自第三方 OFL-1.1 素材的點陣圖

- Status: accepted
- Date: 2026-08-02
- Supersedes: ADR-0005（僅「字型與圖示授權」段落；該 ADR 其餘內容如
  WeatherWorker HTTPS 契約、SNTP、Internet 可達性訊號等不受影響）

## Context

ADR-0005 當初刻意選擇「自製最小點陣字型與 9 組簡化天氣圖示分類，皆為本
專案原創點陣資料，不引入任何第三方字型或圖示檔案」，以避免授權查證與
再散布限制的負擔。程序化繪製版本用圓形＋線段組成圖示，形狀較粗略。

姊妹專案 pico-paper-clock 對同一問題採用不同取捨：從
`erikflowers/weather-icons`（SIL OFL-1.1）轉檔出點陣圖示，視覺精細度較
高，並透過 `ASSET_CREDITS.md`／`THIRD_PARTY_NOTICES.md` 正確承接 OFL-1.1
義務。使用者決定把同一份素材帶進本專案，翻轉 ADR-0005 當初「刻意不引入」
的決策，比照 pico-paper-clock 的授權處理方式。

## Decision

- 天氣狀態列圖示改用轉檔自 `erikflowers/weather-icons` 的點陣圖，取代
  ADR-0005 的程序化繪製（`components/pf_display/include/pf_display/weather_icons.hpp`
  的 `detail::fill_circle`／`draw_line`／`draw_cloud` 等機制已移除）。
- **配色**：純黑白單色。來源 SVG 是單色 glyph（單一路徑、單一填色），無法
  直接裁切出多色分區；維持程序化版本的太陽黃／雲黑／雨滴藍多色風格需要
  手動重新設計，不算「裁切原圖」，故不採用。
- **分類數量**：沿用 ADR-0005 既有的 9 組（`clear`／`few_clouds`／
  `clouds`／`overcast`／`shower_rain`／`rain`／`thunderstorm`／`snow`／
  `mist`，對應 OpenWeatherMap icon code 前兩碼）。不擴充到 pico-paper-clock
  的 14 種細分——OWM 的 Dust/Sand/Smoke/Squall/Tornado/Haze 在 icon code
  層級全部共用 `50`，要細分須額外讀取 `weather id` 數字碼並改動
  `pf_weather` 呼叫端契約，範圍明顯超出本次「帶入既有素材」的目的。
- **尺寸與格式**：每個圖示烘焙成固定 32×32、1 bit-per-pixel 點陣（4
  bytes/row × 32 rows = 128 bytes/icon，9 個圖示共約 1.15 KB flash），
  沿用 `bitmap_font.hpp` 既有的 `rows[]` 陣列風格。`draw_weather_icon`
  維持既有簽章，改為 nearest-neighbor 取樣縮放進呼叫端指定的
  `size × size` 目的框（唯一呼叫點 `status_bar_renderer.hpp` 固定傳
  `kIconSize = 32`，1:1 直接對映）。
- **來源對映**（釘選 `erikflowers/weather-icons` commit
  `bb80982bf1f43f2d57f9dd753e7413bf88beb9ed`）：

  | `WeatherIconId` | 來源 SVG |
  |---|---|
  | `clear` | `svg/wi-day-sunny.svg` |
  | `few_clouds` | `svg/wi-day-cloudy.svg` |
  | `clouds` | `svg/wi-cloud.svg` |
  | `overcast` | `svg/wi-cloudy.svg` |
  | `shower_rain` | `svg/wi-showers.svg` |
  | `rain` | `svg/wi-rain.svg` |
  | `thunderstorm` | `svg/wi-thunderstorm.svg` |
  | `snow` | `svg/wi-snow.svg` |
  | `mist` | `svg/wi-fog.svg` |

- **不 commit 原始素材**：原始 SVG（與任何中間 rasterize 產物，如 PNG）
  一律不進版控；唯一 commit 的產物是
  `components/pf_display/include/pf_display/weather_icon_bitmaps.hpp`
  這份 packed C++ header，由 `scripts/generate_weather_icons.py`（同樣
  commit 進 repo）產生，腳本檔頭記錄上游 URL、釘選 commit、確切檔名與
  重新產生步驟，確保流程可稽核、可重現。

## Consequences

- 本專案的 `LICENSE`（MIT）維持不變，但 `weather_icon_bitmaps.hpp` 內的
  點陣資料實質上是 OFL-1.1 授權的衍生物，**不隨 MIT 一併授權**——
  `ASSET_CREDITS.md` 與 `THIRD_PARTY_NOTICES.md` 記錄這個疊加義務，任何
  下游重新散布（含 release firmware、OTA image）都要保留這兩份文件的
  對應聲明。
- 圖示視覺精細度提升，但失去程序化版本的多色分區與「零第三方授權表面」
  這兩個 ADR-0005 原本看重的特性；若未來要再擴充分類或改配色，需要新的
  superseding ADR，不能直接改程式碼繞過本決策。
- `weather_icon_bitmaps.hpp` 是產生檔（generated），不得手動編輯；要調整
  圖示須重新跑 `scripts/generate_weather_icons.py`。

## Verification

- `pio test -e native -f test_weather_icons` 全綠（含新增的
  `test_non_native_size_scales_within_requested_box`，驗證非 32 的 size
  透過 nearest-neighbor 縮放後仍維持在呼叫端指定的框內）。
- `pio run`（env `paperframe-s3`）編譯乾淨。
- 實作時已用 ASCII art 印出 `clear`／`rain`／`thunderstorm`／`snow` 四個
  圖示肉眼核對形狀正確（太陽、雲＋雨滴、雲＋閃電、雲＋雪花），未進版控。
