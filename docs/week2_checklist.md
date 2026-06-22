# Month 1 W2 — libcamera 擷取 RAW Checklist

> ✅ **2026-06-22 完成。** 結論與技術細節見 [`notes_w2_libcamera.md`](notes_w2_libcamera.md)。
> 重點：RPi5/PiSP 強制壓縮 packed RAW10，改要求 unpacked **`SBGGR16`** 才拿到未壓縮
> Bayer；且 16-bit 為**左對齊**（10-bit 在高位 ×64），black level ≈ 64。

**目標**：週末結束時，能用 **自己寫的 libcamera C++ 程式** 把 IMX708 的 RAW
Bayer 抓下來存檔，並看得懂這個 raw 檔的格式（Bayer 排列、bit 深度、stride）。

> W1 只設了 `dtoverlay=imx708,cam0`，還沒實際抓圖。W2 第一件事就是**驗證 camera
> 真的抓得到**，再進到自己寫程式。

---

## Day 1 — 確認 camera 抓得到（用現成工具）

先不寫 code，用 `rpicam` 工具確認硬體 + libcamera stack 正常。

```bash
# 1. 認得到 IMX708 嗎？
rpicam-hello --list-cameras
```

預期：列出 `imx708`，以及它支援的解析度與格式（會看到 SBGGR10 / SRGGB10 之類）。

```bash
# 2. 預覽 5 秒（接 DSI 或 HDMI 的話會看到畫面；純 SSH 看不到視窗沒關係，不報錯就行）
rpicam-hello -t 5000

# 3. 拍一張一般 JPG（經過 ISP）
rpicam-still -o test.jpg

# 4. 拍一張 RAW（同時產生 .dng）—— 這就是「快速版」的 W2 raw 影像
rpicam-still --raw -o ref.jpg
ls -lh ref.jpg ref.dng
```

預期：`ref.dng` 約 10–25 MB。把 `ref.dng` 用 `scp` 拉回 Mac，用「預覽程式」或
RawTherapee 開得起來 → 硬體沒問題。

### ⚠ 卡關時
| 症狀 | debug |
|---|---|
| `--list-cameras` 沒看到 imx708 | `dmesg \| grep -i imx708`；檢查排線方向（金屬接點朝 PCB）、`config.txt` 是否有 `dtoverlay=imx708,cam0` |
| 認得到但拍出來全黑 | 鏡頭蓋沒拿、環境太暗；`rpicam-hello -t 5000` 加長曝光看看 |
| `dmesg` 出現 i2c / probe 錯誤 | overlay 對到的 port 不對（cam0 vs cam1），對照接線 |

---

## Day 2–3 — 自己寫 libcamera 擷取程式（W2 重點）

這是 firmware 相關的核心：理解 libcamera 怎麼把 sensor 資料交到你手上。

```bash
cd experiments/w2_libcamera_raw
make
./capture_raw
```

程式做的事（細節看 `capture_raw.cpp` 註解）：
1. `CameraManager` 啟動、取得 camera
2. `generateConfiguration({StreamRole::Raw})` → 拿到 sensor 原生 Bayer 格式
3. `FrameBufferAllocator` 配 dma-buf buffer
4. 建 `Request`、`start()`、`queueRequest()`
5. `requestCompleted` callback 回來 → `mmap` buffer → 寫成 `imx708_*.raw`

**驗收**：終端有印出「格式 / 尺寸 / stride」，且產出一個約 14–15 MB 的 `.raw` 檔。

### 要看懂的關鍵字（面試會問）
- **Bayer pattern**：`SBGGR10` = B/G/G/R 排列、10-bit。為什麼 sensor 給的是
  馬賽克而不是 RGB？→ 每個 photosite 只量一種顏色，RGB 要靠 demosaic 補。
- **CSI-2 packed (`_CSI2P`)**：10-bit 不對齊 byte，4 像素塞 5 bytes 省頻寬，
  所以 `stride ≠ width × 2`。
- **dma-buf**：`FrameBuffer::Plane::fd` 就是一個 dma-buf fd。W2 我們先 `mmap`
  到 CPU 讀；Month 3 會改成**不 copy**、直接把這個 fd 餵給 GPU（EGLImage）。
- **Stream role**：`Raw` vs `Viewfinder` vs `StillCapture` 的差別。

---

## Day 4 — 看懂 raw 檔 + 寫筆記

- [ ] 確認 `capture_raw` 印出的格式跟 `rpicam-hello --list-cameras` 一致
- [ ] 用 `xxd imx708_7.raw | head` 看前幾個 byte（不是全 0、不是全 ff 就有資料）
- [ ] 在 `docs/` 開一篇 `notes_w2_libcamera.md`，記下：
  - IMX708 原生 raw 格式與解析度（4608×2592？binned 2304×1296？）
  - packed RAW10 的 byte 排法（畫個圖：5 bytes = 4 像素 + 1 byte 低位）
  - libcamera 物件關係：CameraManager → Camera → Stream → FrameBuffer → Plane(fd)

---

## Day 5 — 把 raw 解出來看（選做但很值得）

寫一小段 unpack：把 CSI-2 packed RAW10 → 16-bit 灰階 PGM，用 Mac 開來看。
這會逼你真的理解 packing，對 Month 4 demosaic 很有幫助。

> 不一定要這週做完；卡住就先記 TODO，W2 主線（抓到 raw + 看懂格式）達成即可往 W3。

---

## Day 6–7 — 緩衝 + commit

- [ ] `experiments/w2_libcamera_raw/` commit 進 repo（**不要** commit `.raw`/`.dng`，太大）
- [ ] 更新 `CLAUDE.md` 進度、`README.md` 的 Week 2 勾選
- [ ] buffer day

---

## Week 2 驗收條件

往 W3 走之前要滿足：

1. ✅ `rpicam-hello --list-cameras` 看得到 IMX708 與其格式
2. ✅ `rpicam-still --raw` 能產生可開啟的 `.dng`
3. ✅ 自己寫的 `capture_raw` 能 build、跑出 `.raw`，並印出格式/尺寸/stride
4. ✅ 能用自己的話講清楚：Bayer pattern、RAW10 packed、dma-buf fd 是什麼
5. ✅ experiment 與筆記 commit 進 repo

---

## 給下週 (W3) 的伏筆

W3 要把 DRM/KMS framebuffer 直接寫到 DSI。W2 學到的 dma-buf fd，到
**Month 3** 會和 W3 的 DRM framebuffer 接起來——那時候「camera 的 buffer 不經
CPU 直接給 display」就是 zero-copy 的核心。W2 先把「拿到 buffer」這件事做熟。
