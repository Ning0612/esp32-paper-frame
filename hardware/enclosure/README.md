# PaperFrame 相框外殼 CAD

7.3 吋 800×480 E6 電子紙桌上型相框的外殼素材，SolidWorks 2019 建模，
每個零件同時提供 **STEP（AP214）** 與 **STL**：STEP 用來改尺寸或重新出圖，
STL 直接送切片軟體列印。組裝後的實機外觀見
[README 的實機照片](https://github.com/Ning0612/esp32-paper-frame/blob/main/docs/media/device-front.jpg)。

## 組裝順序

檔名前綴就是堆疊順序，由面板側往背面依序疊上去：

```
01 螢幕背板殼
   └─ 電子紙面板
02 相框外框
03 電子零件背板
   └─ 電子零件（ESP32-S3、感測器、配線）
04 腳架旋轉件
05 相框背板
06 腳架
```

## 零件

尺寸是從 STL 幾何量出來的外接方框（bounding box，單位 mm），不是標稱設計值。

| 檔名 | 零件 | 外接方框 (X×Y×Z) |
| --- | --- | --- |
| `01-screen-back-shell` | 螢幕背板殼 | 176 × 116 × 3 |
| `02-outer-frame` | 相框外框 | 180 × 120 × 26 |
| `03-electronics-back-panel` | 電子零件背板 | 176 × 116 × 4 |
| `04-stand-pivot` | 腳架旋轉件 | 16 × 11 × 16 |
| `05-frame-back-panel` | 相框背板 | 176 × 120 × 19 |
| `06-stand` | 腳架 | 30 × 88 × 4 |

原始檔名對照（重新命名前）：

| 目前檔名 | 原始檔名 |
| --- | --- |
| `01-screen-back-shell` | `epaper-screen-1` |
| `02-outer-frame` | `epaper-screen-2` |
| `03-electronics-back-panel` | `epaper-screen-4` |
| `04-stand-pivot` | `epaper-screen-5` |
| `05-frame-back-panel` | `epaper-screen-3` |
| `06-stand` | `epaper-screen-6` |

## 已知限制

- 列印材料、層高、填充率與支撐設定尚未記錄。
- 上表尺寸取自 STL 的三角網格，是實際會列印出來的幾何。**STEP 與 STL 是否
  完全等價未經逐面比對**——本專案沒有 CAD kernel 可以做這件事，只確認過兩者
  的外形尺寸一致。

## 授權

與本 repository 相同（MIT），見 [LICENSE](https://github.com/Ning0612/esp32-paper-frame/blob/main/LICENSE)。

本檔會與 CAD 一起打包成 GitHub Release 的 `paperframe-enclosure.zip`，
所以上面的連結刻意用絕對網址——解壓後的資料夾沒有 repository 的目錄結構。
