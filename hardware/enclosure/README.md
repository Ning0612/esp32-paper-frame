# PaperFrame 相框外殼 CAD

7.3 吋 800×480 E6 電子紙桌上型相框的外殼素材，SolidWorks 2019 建模，
每個零件同時提供 **STEP（AP214）** 與 **STL**：STEP 用來改尺寸或重新出圖，
STL 直接送切片軟體列印。實機組裝外觀見
[README 的實機照片](../../docs/media/device-front.jpg)。

## 零件

尺寸是從幾何量出來的外接方框（bounding box，單位 mm），不是標稱設計值。

| 檔名 | 外接方框 (X×Y×Z) | 說明 |
| --- | --- | --- |
| `01-front-shell` | 180 × 120 × 26 | 外框本體。正面開窗、側面有連接埠開口、頂緣有開槽 |
| `02-back-cover` | 176 × 120 × 19 | 背蓋。四周有卡扣凸緣，中央一個孔 |
| `03-panel-tray` | 176 × 116 × 4 | 內部托板，帶滑軌與周邊避讓缺口 |
| `04-front-window` | 176 × 116 × 3 | 面板前方的平板 |
| `05-stand-leg` | 30 × 88 × 4 | 立架，頂端有卡槽 |
| `06-button-cap` | 16 × 11 × 16 | 圓帽狀小件，底部中空、帶凸緣 |

原始檔名對照（重新命名前）：

| 目前檔名 | 原始檔名 |
| --- | --- |
| `01-front-shell` | `epaper-screen-2` |
| `02-back-cover` | `epaper-screen-3` |
| `03-panel-tray` | `epaper-screen-4` |
| `04-front-window` | `epaper-screen-1` |
| `05-stand-leg` | `epaper-screen-6` |
| `06-button-cap` | `epaper-screen-5` |

## 已知問題

- **`06-button-cap` 的 STEP 與 STL 不是同一版**：STL 的外接方框是
  16 × 11 × 16 mm（存檔時間 2026-08-19），STEP 是 2 × 11 × 22 mm
  （2026-08-23）。其餘五個零件兩種格式的外接方框一致。以 STEP 為準時
  需要重新輸出 STL。

## 授權

與本 repository 相同，見 [LICENSE](../../LICENSE)。
