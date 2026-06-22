# W2 技術筆記 — libcamera 擷取 IMX708 未壓縮 RAW

> 對應實驗：`experiments/w2_libcamera_raw/`
> 完成日：2026-06-22

## 做到了什麼

自己寫了一支 libcamera C++ 程式（`capture_raw.cpp`），不靠 `rpicam-*` 工具，
直接從 IMX708 抓 **未壓縮 16-bit Bayer**（4608×2592）存檔，再用 `raw_to_bmp.c`
轉成灰階圖確認——場景清楚可辨，整條 sensor → userspace 路徑打通。

## libcamera 物件關係（面試講得出來）

```
CameraManager      啟動、列舉 camera
  └─ Camera        acquire() 取得獨佔
       └─ CameraConfiguration   generateConfiguration({StreamRole::Raw})
            └─ StreamConfiguration  pixelFormat / size / stride
                 └─ Stream
FrameBufferAllocator  替 Stream 配 dma-buf buffer
  └─ FrameBuffer
       └─ Plane    .fd (dma-buf fd) / .offset / .length
Request           addBuffer(stream, buffer)；queueRequest 後非同步完成
  └─ requestCompleted signal → 在 libcamera 自己的 thread 回呼
```

關鍵流程：`configure → allocate → 建 Request → start → queueRequest →
requestCompleted 回呼裡讀 buffer → reuse + 重新 queue`。

## 最重要的發現：RPi5 PiSP 的 raw 格式陷阱

RPi5 不是 RPi4 的舊 ISP，而是 **PiSP**（BCM2712）。它的 **CFE（Camera Front
End）預設會把 raw 壓縮**成私有格式，省 DRAM 頻寬：

| 要求格式 | validate() 後 | CFE fourcc | 壓縮? |
|---|---|---|---|
| `SBGGR10_CSI2P`（packed RAW10） | `BGGR_PISP_COMP1` | `PC1B` | ✅ 是 |
| `SBGGR16`（unpacked 16-bit） | `SBGGR16` | `BYR2` | ❌ 否 |

- 連「明確指定」packed RAW10 都會被 `validate()` 改成 COMP1 → CFE 在這個全
  解析度模式強制壓縮 packed 路徑。
- 改要求 **unpacked `SBGGR16`** 就拿到未壓縮乾淨 Bayer。
- `rpicam-still --raw` 能輸出乾淨 DNG，是因為它在**軟體端解壓 COMP1**。
- COMP1 的演算法在開源的 `raspberrypi/libpisp`，不是黑盒（之後想碰可回來）。

**為什麼選未壓縮**：專案賣點是「每一層自己寫」，中間不想卡一個要解壓的 vendor
格式；而且 `SBGGR16` 每像素就是一個 `uint16`，餵 GPU demosaic 不必拆 bit。

## 第二個坑：16-bit 是「左對齊」

`SBGGR16` 的 10-bit 有效值放在 **高位 `[15:6]`**（等於原值 ×64，低 6 bit 補 0）。

- 一開始用 `>>2`（誤以為右對齊 0..1023）轉圖 → 黑階 62 都被放大成 ~992 → **全白**。
- 看 `raw16 統計`：`min=3968 max=65280` = `62×64 .. 1020×64` → 一眼看出左對齊。
- 正解：`>>6` 還原 10-bit。`min≈62` 正好是 IMX708 的 **black level**（黑階）。

debug 心法：**懷疑顯示不對時，先印原始數值的 min/max/mean，不要只看處理過的圖。**

## 名詞速記（之後 demosaic 會用到）

- **Bayer pattern**：每個 photosite 只量一種顏色（R/G/B），排成馬賽克。IMX708
  native 是 RGGB，但實際 crop 後我們拿到的是 **BGGR**（排列會隨 crop/翻轉位移）
  → demosaic 時 CFA 排列要從實際 config 讀，不能寫死。
- **Black level**：sensor 在全黑時的非零讀值（暗電流 + 偏壓），約 64@10-bit，
  之後要先扣掉再做白平衡/CCM。
- **dma-buf fd**：`Plane::fd` 就是一個 dma-buf。W2 先 `mmap` 到 CPU 讀；Month 3
  會改成不 copy、直接把 fd import 成 EGLImage 餵 GPU。

## 驗收

- [x] 自寫 libcamera 程式抓到未壓縮 `SBGGR16` raw，存出 23,887,872 bytes
- [x] 轉成灰階圖，場景清楚可辨（未 demosaic，偏綠 + 格紋，正常）
- [x] 講得出 libcamera 物件關係、PiSP 壓縮陷阱、左對齊、black level
