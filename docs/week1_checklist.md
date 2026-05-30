# Month 1 W1 — 環境建置 Checklist

**目標**：週末結束時，能從 Mac SSH 到 RPi5、寫個 C 程式 build 並執行、Camera 能拍照、DSI display 能顯示。

> **重要決定**：host 是 macOS，**不建議**一開始就搞 cross-compile（Mac→ARM64 Linux 環境很雜）。第一個月**直接在 RPi5 上 native build** — RPi5 是 Cortex-A76 quad-core 2.4GHz，編譯 libcamera/Mesa 都夠快。Cross-compile 等 Month 3 真的有需要 debug 才加。

---

## Day 1 — 硬體 + OS 燒錄

- [ ] 確認硬體都到齊：RPi5、**5V/5A USB-C 電源**（重要，4A 以下會 throttle）、**Active Cooler**（RPi5 沒散熱會降頻）、microSD ≥32GB（建議 64GB，Class A2）
- [ ] **接線（先斷電！）**：
  - Camera v3 → **CAM/DISP 0 port**（RPi5 上有兩個，先用 0 號）
  - Waveshare 7" DSI → **CAM/DISP 1 port**（注意排線方向，金屬接點朝向 PCB）
  - Touch I2C 線（如果是分離式）→ GPIO
- [ ] 下載 **Raspberry Pi Imager**（Mac 版），燒錄 **Raspberry Pi OS (64-bit, Bookworm)** Desktop 版到 SD 卡
- [ ] 燒錄時點齒輪 ⚙：開 **SSH**、設 **username/password**、設 **Wi-Fi**、設 **hostname**（例如 `rpi5-gpu.local`）

### ⚠ 注意
- Waveshare 7" DSI 顯示器 **第一次開機可能黑屏** — 這正常，要先改 `config.txt`，Day 2 處理
- RPi5 的 DSI 接頭跟 RPi4 不同（變成 22-pin 細排線），Waveshare 出貨應該有附對應排線，若沒有要另買

---

## Day 2 — 首次開機 + 網路 + Display 設定

- [ ] 插 SD 卡、接電源、開機（先用 HDMI 連螢幕除錯，DSI 之後處理）
- [ ] 在 Mac terminal：`ssh pi@rpi5-gpu.local`（或你設的 hostname）能登入
- [ ] 更新系統：
  ```bash
  sudo apt update && sudo apt full-upgrade -y
  sudo rpi-update    # 更新到最新 kernel/firmware，這個專案需要新的 V3D driver
  sudo reboot
  ```
- [ ] **設定 Waveshare DSI display**：
  - 編輯 `/boot/firmware/config.txt`，加：
    ```
    dtoverlay=vc4-kms-dsi-waveshare-panel,7_0_inchH
    ```
  - （這是 Waveshare 7" H 版的標準 overlay，但**請對照你手上產品的官方 wiki 確認**型號代碼）
  - `sudo reboot`，確認 DSI 螢幕亮起來、觸控能動（觸控 IC 應該是 GT911）
- [ ] **設定 Camera v3**：
  - `/boot/firmware/config.txt` 加：`dtoverlay=imx708,cam0`
  - `sudo reboot`
  - 驗證：`rpicam-hello --list-cameras` 應該看到 IMX708

### 驗收
```bash
rpicam-still -o test.jpg     # 應該拍出一張照片
```

---

## Day 3 — Mac ↔ RPi5 開發工作流

- [ ] Mac 上設 SSH key 免密碼登入：
  ```bash
  ssh-keygen -t ed25519        # 如果還沒有
  ssh-copy-id pi@rpi5-gpu.local
  ```
- [ ] Mac 上安裝 **VS Code** + **Remote-SSH extension**
  - 連到 RPi5，之後在 Mac 上開檔案編輯，實際 build/run 在 RPi5 上跑
  - 這是 beginner 最舒服的工作流，比 cross-compile 省心 10 倍
- [ ] RPi5 上裝 dev 基本工具：
  ```bash
  sudo apt install -y \
    build-essential git cmake meson ninja-build pkg-config \
    vim tmux htop \
    v4l-utils mesa-utils libdrm-tests
  ```
- [ ] 設定 git：
  ```bash
  git config --global user.name "你的名字"
  git config --global user.email "redclould97@gmail.com"
  ```
- [ ] 在 GitHub 開一個 repo（建議名稱：`rpi5-zero-copy-camera-pipeline` 或類似），clone 到 RPi5 上

---

## Day 4 — 安裝專案需要的 dev libraries

```bash
sudo apt install -y \
  libcamera-dev libcamera-tools rpicam-apps \
  libdrm-dev \
  libgbm-dev \
  libegl-dev libgles2-mesa-dev \
  mesa-common-dev \
  libwayland-dev libxkbcommon-dev \
  linux-headers-rpi-2712
```

### 驗證每一個子系統有「能動」

```bash
# 1. Camera (libcamera userspace)
rpicam-hello --list-cameras

# 2. V4L2 (kernel 介面，後面 dma-buf 會用)
v4l2-ctl --list-devices

# 3. DRM (display kernel 介面)
ls /dev/dri/                  # 應該有 card0, card1, renderD128
modetest -M vc4 -c            # 列出 connector（DSI、HDMI）

# 4. GPU (Mesa V3D)
glxinfo -B | grep -i "render\|version"   # 應該看到 V3D 7.x
eglinfo | grep -i v3d                     # EGL 也要找得到 V3D

# 5. Kernel headers（之後寫 module 用）
ls /lib/modules/$(uname -r)/build
```

每一行都應該有合理輸出。**任何一個失敗就停下來查清楚**，不要往下走。

---

## Day 5 — 第一個 C 程式：DRM 列出 display 資訊

寫一個 30 行的小程式驗證 toolchain + DRM library 都能用：

```c
// drm_info.c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(void) {
    int fd = open("/dev/dri/card1", O_RDWR);
    drmModeRes *res = drmModeGetResources(fd);
    printf("Connectors: %d, CRTCs: %d, Encoders: %d\n",
           res->count_connectors, res->count_crtcs, res->count_encoders);
    drmModeFreeResources(res);
    close(fd);
    return 0;
}
```

```bash
gcc drm_info.c -o drm_info $(pkg-config --cflags --libs libdrm)
./drm_info
```

如果跑出來，**Week 1 就成功了**。把它 commit 到 repo。

> 注：card0 / card1 哪一個是 V3D、哪一個是 vc4 display controller，視 kernel 版本而定。如果 card1 不對換 card0 試試。

---

## Day 6-7 — 緩衝與整理

- [ ] **寫 README**：記錄硬體型號、OS 版本、kernel 版本、踩過的坑（這個 README 之後求職 / SoP 都會用到）
- [ ] **跑一次 `uname -a`、`cat /etc/os-release`、`vcgencmd version` 並截圖存檔**（之後 Mesa 版本對不上時可以查）
- [ ] **建 repo 目錄結構**：
  ```
  rpi5-zero-copy-camera-pipeline/
  ├── README.md
  ├── docs/
  │   └── setup.md          # Week 1 設定紀錄
  ├── experiments/          # 每週的小實驗
  │   └── w1_drm_info/
  └── src/                  # 主專案，Month 2 開始放
  ```
- [ ] **buffer day**：硬體一定會卡關（DSI 不亮、camera 認不到），預留時間

---

## Week 1 驗收條件

完成下列才算 pass，往 W2 走：

1. ✅ Mac SSH 到 RPi5 免密碼
2. ✅ Camera v3 能拍照 (`rpicam-still`)
3. ✅ DSI 螢幕能顯示 desktop（觸控能動）
4. ✅ `glxinfo` 看到 V3D 7.x
5. ✅ `modetest`、`v4l2-ctl` 都有合理輸出
6. ✅ 自己寫的 `drm_info.c` 能 compile + run
7. ✅ GitHub repo 建好、Week 1 setup 紀錄 commit 進去

---

## 常見坑

| 症狀 | 通常原因 |
|---|---|
| DSI 黑屏 | 排線反了 / `config.txt` overlay 名字錯 / 沒接 5V power 給觸控 |
| `rpicam-hello` 說找不到 camera | 排線反了 / 沒加 `dtoverlay=imx708` / 接到錯的 port 但 overlay 寫 `cam0` |
| `glxinfo` 顯示 llvmpipe 不是 V3D | GPU driver 沒載入，檢查 `dmesg \| grep v3d` |
| ssh 連不上 `rpi5-gpu.local` | mDNS 沒起來，先用 IP；裝 `avahi-daemon` |
| SD 卡讀寫變慢 | 不是 A2 class / 燒錄時 image 壞了，重燒 |
