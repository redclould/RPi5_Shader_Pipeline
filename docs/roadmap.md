# 6-Month Roadmap — Custom GLSL Compute Shader Image Pipeline

**目標**：Camera v3 (IMX708) → GPU Compute Shader (V3D) → DSI Display，**zero-copy** pipeline，繞過 ISP 自己做 image processing。

**完成後可以說**：「我從 sensor raw data 一路到 display，每一層都自己寫過」— 這就是 GPU/camera firmware engineer 的核心能力。

---

## 技術棧總覽

```
┌─────────────────────────────────────────────────┐
│  libcamera (userspace) — IMX708 sensor control │
│             ↓ V4L2 / dma-buf fd                 │
│  EGL + GLES 3.1 Compute Shader (V3D GPU)        │
│             ↓ dma-buf fd                        │
│  DRM/KMS — DSI display output                   │
└─────────────────────────────────────────────────┘
```

---

## Month 1 — 環境建置 + 各子系統「Hello World」

**核心問題**：先讓三個子系統各自能動，不用整合。

| 週 | 任務 | 產出 |
|---|---|---|
| W1 | RPi5 dev 環境：SSH、native build、kernel headers、Mesa | 能 build 第一個 `drm_info.c` |
| W2 | libcamera 範例：跑 `cam` 工具，擷取 RAW10 Bayer 到檔案 | 一張 IMX708 raw 影像 |
| W3 | DRM/KMS 最小範例：直接寫 framebuffer 到 DSI（不經 X/Wayland） | DSI 顯示一張純色或漸層圖 |
| W4 | OpenGL ES 透過 GBM + EGL（無 X11）跑起來，畫一個三角形 | 三角形顯示在 DSI 上 |

**必讀**：
- `drm-howto`（GitHub david-mp/drm-howto）— 三天讀完
- libcamera 官方 tutorial
- 《Linux Device Drivers, 3rd Ed.》Ch.1-3（觀念熱身，不用全部讀完）

**驗收**：三個 stand-alone 程式都能跑。

> Week 1 細項見 `week1_checklist.md`

---

## Month 2 — GPU Compute Shader 基礎

**核心問題**：理解 V3D GPU 怎麼跑 shader，先處理「靜態圖片」。

| 週 | 任務 |
|---|---|
| W1 | 讀 OpenGL ES 3.1 compute shader 規格；理解 workgroup、shared memory、SSBO |
| W2 | 寫第一個 compute shader：load 一張 PNG → grayscale → 存回 PNG |
| W3 | 寫 Bayer pattern 視覺化 shader（把 RGGB 拆成四張單色圖） |
| W4 | 讀 Broadcom V3D 架構文件，理解 QPU、TMU、tile-based rendering；用 `v3d_perfmon` 量測 shader 執行時間 |

**必讀**：
- 《OpenGL ES 3.0 Programming Guide》Ch.4, 11
- Broadcom《VideoCore VI/VII 3D Architecture Reference》（Raspberry Pi 官網有 PDF）
- Mesa V3D driver 原始碼：`src/gallium/drivers/v3d/`（讀 README 就好，建立概念）

**驗收**：能寫一個 compute shader 把任意 input texture 做指定運算，並用 perf counter 量測 GPU 時間。

---

## Month 3 — Zero-copy Pipeline（最難、最有價值的一個月）

**核心問題**：dma-buf 串接 camera ↔ GPU ↔ display，**完全不經過 CPU 記憶體 copy**。

| 週 | 任務 |
|---|---|
| W1 | 深入讀 dma-buf 文件、`EGL_EXT_image_dma_buf_import`、`EGL_MESA_image_dma_buf_export` |
| W2 | 把 libcamera 輸出的 dma-buf fd import 成 EGLImage → GL texture |
| W3 | GPU render 結果 export 成 dma-buf fd → 餵給 DRM 當 framebuffer (`drmModeAddFB2`) |
| W4 | 整合：Camera → GL passthrough shader → Display，**全程 zero-copy** |

**必讀**：
- Linux kernel `Documentation/driver-api/dma-buf.rst`
- Collabora blog: "A new userspace API for dma-buf synchronization"
- Mesa 範例：`kmscube`（GitHub robclark/kmscube）— **這個 repo 是你這個月的聖經**

**驗收**：用 `vmstat` / `pidstat` 確認 pipeline 跑起來時 CPU 使用率極低；用 `perf` 確認沒有 memory copy。

**⚠ 風險點**：這個月最容易卡關。如果 W3 結束還沒打通，先用「CPU copy 版」繼續往下做，dma-buf 後面再回來補。**進度 > 完美**。

---

## Month 4 — 自訂 Image Processing（取代 ISP）

**核心問題**：自己用 shader 做完整的 RAW → RGB pipeline，理解每一步在做什麼。

| 週 | 任務 |
|---|---|
| W1 | Bayer demosaic compute shader（先做 bilinear，再做 Malvar-He-Cutler） |
| W2 | Black level correction、white balance、color correction matrix (CCM) |
| W3 | Gamma / tone mapping、sRGB encoding |
| W4 | 與 libcamera 內建 ISP 輸出做比較，寫一份對比報告（雜訊、色彩、邊緣） |

**必讀**：
- Malvar-He-Cutler 2004 paper "High-quality linear interpolation for demosaicing"
- libcamera IPA (Image Processing Algorithms) source — 看他們怎麼算 CCM

**驗收**：拍同一場景，custom pipeline vs ISP 並列顯示在 DSI 上。

---

## Month 5 — 進階亮點功能（選一個做深）

選一個當「招牌功能」，不要全做：

- **選項 A**：Real-time HDR — 多 frame fusion + adaptive tone mapping
- **選項 B**：On-GPU 直方圖 + 自動曝光 feedback loop
- **選項 C**：Edge-aware denoising (bilateral filter on compute shader)

每個都會用到 shared memory、atomic operations、multi-pass rendering — 這些是 GPU firmware 面試常考的主題。

**建議**：A 最有 demo 效果，B 最接近真實 ISP firmware 工作。

---

## Month 6 — Profiling、文件、Demo

| 週 | 任務 |
|---|---|
| W1 | 完整 profiling：每個 stage 的 GPU 時間、bandwidth、power（用 `vcgencmd measure_volts`） |
| W2 | 寫 3 篇 technical blog：①架構總覽 ②dma-buf zero-copy 怎麼打通 ③自訂 demosaic vs ISP |
| W3 | 錄 demo 影片（90 秒）；GitHub README 寫好架構圖、build instructions、benchmarks |
| W4 | 整理成研究所 SoP 段落 + 求職履歷條目；準備 5 分鐘技術簡報 |

---

## 履歷 / SoP 包裝方式

**履歷一行版**：
> Built a zero-copy camera-to-display pipeline on Raspberry Pi 5 (BCM2712) using libcamera, GLES 3.1 compute shaders on VideoCore VII GPU, and DRM/KMS — replaced the vendor ISP with custom GLSL demosaic, CCM, and tone-mapping shaders. Achieved <X ms latency at 1080p30 with <Y% CPU usage via dma-buf sharing.

**研究所 SoP 切角**：
- 不要寫「我做了一個 project」
- 要寫「我發現 vendor ISP 是 black box，所以我自己重建一份來理解 image pipeline 的每個自由度」— **展現 research mindset**

---

## 學習成果（面試會被問到的關鍵字）

完成後你能講清楚：
- dma-buf、prime fd、EGLImage 的關係
- Tile-based GPU 為什麼適合 mobile/embedded
- Bayer pattern、demosaic 演算法的 trade-off
- Compute shader 的 workgroup 大小怎麼決定
- Memory bandwidth 在 image pipeline 中為什麼是瓶頸
