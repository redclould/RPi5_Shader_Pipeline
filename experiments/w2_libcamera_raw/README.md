# w2_libcamera_raw — 用 libcamera 抓 IMX708 RAW（未壓縮 Bayer）

W2 的實驗：自己寫一個 libcamera C++ 程式，把 Camera Module v3 (IMX708) 的
**未壓縮 Bayer** frame 抓下來存檔，再用一個小工具轉成可看的灰階圖。
這是 Month 3 zero-copy pipeline 的前半段。

## 重點發現（RPi5 專屬）

RPi5 用的是 **PiSP**（BCM2712 的新 ISP），它的 CFE（Camera Front End）在把
sensor 資料寫進 DRAM 前**預設會做硬體壓縮**：

| 你要求的格式 | PiSP 實際給你 | 結果 |
|---|---|---|
| `SBGGR10_CSI2P`（packed RAW10） | `BGGR_PISP_COMP1`（CFE: `PC1B`） | **被壓縮**，私有格式 |
| `SBGGR16`（unpacked 16-bit） | `SBGGR16`（CFE: `BYR2`） | **未壓縮**，乾淨 Bayer ✅ |

所以本實驗**明確要求 `SBGGR16`**，拿到未壓縮、每像素一個 `uint16` 的 Bayer，
之後餵 GPU demosaic 最直接（不用自己拆 bit）。

⚠️ **左對齊**：`SBGGR16` 的 10-bit 值放在高位 `[15:6]`（等於原值 ×64），
低 6 bit 補 0。換算回 10-bit 要 `>> 6`。IMX708 的黑階（black level）約 64。

## Build（在 RPi5 上）

```bash
cd experiments/w2_libcamera_raw
make            # 產生 capture_raw 與 raw_to_bmp
```

需要 `libcamera-dev`（W1 已安裝）。

## Run

```bash
./capture_raw                                  # 抓 30 張，存最後一張為 imx708.raw
./raw_to_bmp imx708.raw 4608 2592 imx708.bmp   # 轉成可看的 8-bit 灰階 BMP
```

`capture_raw` 預期輸出（重點行）：

```
驗證後 Raw 設定: 4608x2592-SBGGR16/RAW
格式: SBGGR16  尺寸: 4608x2592  stride: 9216 bytes/row
配到 2 個 buffer
frame #0 ... frame #29
  已存檔: imx708.raw (23887872 bytes)
```

`raw_to_bmp` 會印出真實數值範圍，例如：

```
raw16 統計: min=3904 max=65472 mean=7063.1  (→ 10-bit max=1023)
```

## 看圖（拉回 macOS）

```bash
# 在 Mac terminal
scp jocelynlin@rpi5.local:~/940_ldd/shader-pipeline/experiments/w2_libcamera_raw/imx708.bmp ~/Desktop/
open ~/Desktop/imx708.bmp
```

會看到一張**還沒 demosaic 的灰階 Bayer 圖**：場景認得出來，但有細格狀紋理、
偏綠的部分在灰階下會偏亮（綠像素佔 Bayer 一半）。畫面可能上下顛倒（camera
安裝方向）。`raw_to_bmp` 只做「扣黑階 + gamma」方便人眼看，**不是 demosaic**
（真正的 RAW→RGB 是 Month 4）。

## 檔案

- `capture_raw.cpp` — libcamera 擷取主程式（CameraManager → Stream → FrameBuffer → mmap → 存檔）
- `raw_to_bmp.c` — SBGGR16 → 8-bit 灰階 BMP（左對齊還原 + 黑階 + gamma + auto-stretch）
- `Makefile` — `make` 一次 build 兩支

## 給之後的 TODO

- COMP1（PiSP 壓縮格式）的解碼演算法在開源的 `raspberrypi/libpisp` 裡，不是黑盒。
  若之後想直接吃 packed/壓縮路徑可回來研究（非必要，SBGGR16 已夠用）。
