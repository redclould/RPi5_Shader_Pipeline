# ShaderPipeline — Zero-copy Camera→GPU→Display on RPi5

## 專案目標
在 Raspberry Pi 5 上建立 **zero-copy 影像 pipeline**：
Camera Module v3 (IMX708) → V3D GPU compute shader → DSI 顯示，
**繞過 vendor ISP**，自己用 GLSL 寫 demosaic / CCM / tone mapping。

## 為什麼做這個
作為作品集用於：
- 求職：**GPU firmware engineer**（Imagination / AMD / NVIDIA / Intel / Apple GPU 等）
- 申請研究所：**英國 / 美國** CS / EE 系所

完整背景與 6 個月規劃見 `docs/roadmap.md`。

## 硬體
- Raspberry Pi 5（BCM2712, ARM Cortex-A76 @ 2.4GHz, VideoCore VII GPU）
- Camera Module v3（Sony IMX708, CSI-2）
- Waveshare 7" DSI capacitive touch LCD (H)（1280×720, GT911 觸控）
- Host: macOS（用 SSH + VS Code Remote-SSH 開發，不做 cross-compile）

## 技術棧
- **Camera**: libcamera (userspace) + V4L2 (kernel)
- **GPU**: OpenGL ES 3.1 compute shader, EGL on GBM (no X11)
- **Display**: DRM/KMS direct rendering
- **Buffer sharing**: dma-buf, EGLImage, prime fd
- **Lang**: C/C++ for host code, GLSL for shaders

## 使用者背景（重要）
- **語言**：使用繁體中文（zh-TW）溝通
- **程度**：Linux kernel / driver 初學者；不要假設懂 V4L2/DRM/Device Tree/EGL
- **偏好**：實作優先，理論輔助；卡關時要具體 debug 指引，不要只給高層次建議
- **作風**：beginner 但目標明確、願意啃硬東西

## 目前進度
- [x] 6 個月 roadmap 規劃完成（`docs/roadmap.md`）
- [x] Month 1 Week 1 checklist 完成（`docs/week1_checklist.md`）
- [x] Month 1 Week 1 — 環境建置（2026-05-30 完成）
  - SSH、native build toolchain、`drm_info.c` 跑通
  - Waveshare DSI 設好（`vc4-kms-dsi-waveshare-panel,7_0_inchH`）、GT911 觸控 OK
  - Camera v3 overlay 設好（`imx708,cam0`，W2 才會實際抓圖驗證）
- [x] Month 1 Week 2 — libcamera 擷取 RAW（2026-06-22 完成）
  - 自寫 libcamera C++（`experiments/w2_libcamera_raw/capture_raw.cpp`）抓未壓縮 Bayer
  - **關鍵發現**：RPi5/PiSP 強制壓縮 packed RAW10（COMP1），改要求 unpacked `SBGGR16`
    才拿到未壓縮 Bayer；16-bit 為左對齊（10-bit 在高位 ×64），black level ≈ 64
  - `raw_to_bmp.c` 轉灰階圖驗證成功（場景可辨，未 demosaic）；筆記見 `docs/notes_w2_libcamera.md`
- [ ] **目前在做：Month 1 Week 3 — DRM/KMS 顯示 framebuffer**
- [ ] Month 1 Week 4：EGL + GLES 跑三角形

## 之後 commit 進來的 repo 結構（規劃中）
```
ShaderPipeline/
├── CLAUDE.md              # 本檔
├── README.md              # 對外的專案說明
├── docs/                  # 規劃、學習筆記、技術文章
├── experiments/           # 每週的小驗證程式
└── src/                   # 主 pipeline 程式（Month 2 起）
```

## 給 Claude 的協作守則
- 用繁體中文回答
- 解釋技術名詞前先確認使用者懂不懂；新概念給簡短背景再切入細節
- 給程式碼或指令時，**附上預期輸出**讓使用者驗證
- 卡關時優先給 debug 指令（`dmesg | grep`、`modetest`、`v4l2-ctl` 等），而不是猜原因
- Roadmap 是活的：實作遇到現實阻力時，主動建議調整週次目標
