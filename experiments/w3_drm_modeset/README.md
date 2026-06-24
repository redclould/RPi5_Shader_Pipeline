# w3_drm_modeset — DRM/KMS 最小範例：直接點亮 DSI

W3 的實驗：不經 X11 / Wayland，直接用 **DRM/KMS** 把一張 CPU 畫的漸層推到
Waveshare DSI 螢幕。這是 W1 `drm_info`（只「讀」DRM 資訊）的下一步 —
這次真的「**寫**」framebuffer 並點亮螢幕。

也是 Month 3 zero-copy pipeline 的地基：W3 先用 **dumb buffer（CPU 可寫）**
把 KMS 這條路打通；之後只要把「CPU 畫漸層」換成「GPU render 的 dma-buf」即可。

## 核心概念（W1 沒碰到的新東西）

| 名詞 | 一句話 |
|---|---|
| **dumb buffer** | DRM 提供、CPU 可直接 `mmap` 寫入的 framebuffer 記憶體。不需 GPU/GBM，最適合先驗證 KMS。 |
| **CRTC** | 掃描引擎，把某塊 framebuffer 依 mode 時序「掃」到 connector。`drmModeSetCrtc` = 點亮螢幕的那一刻。 |
| **mode** | 解析度 + 刷新率 + 時序。用 connector 的 preferred mode（`modes[0]`）。 |
| **connector→encoder→CRTC** | 一條訊號鏈：影像來源(CRTC) → 編碼(encoder) → 實體接頭(connector)。 |

完整流程（`modeset.c` 註解有逐步說明）：

```
open card → 找 connected connector + preferred mode
  → find_crtc（沿 encoder 鏈 / possible_crtcs bitmask）
  → 存下原本 CRTC 狀態（離開時還原）
  → create_fb：CREATE_DUMB → drmModeAddFB → MAP_DUMB → mmap
  → draw_gradient（CPU 畫）
  → drmModeSetCrtc  ← 螢幕亮起
  → 按 Enter → 還原 → 清理
```

## Build（在 RPi5 上）

```bash
cd experiments/w3_drm_modeset
make
```

需要 `libdrm-dev`（W1 已裝；`drm_info` 能 build 就有）。

## 重點發現（RPi5 專屬）：DSI 不在 vc4 上，是獨立的 `drm-rp1-dsi`

RPi5 的顯示/繪圖子系統是**拆成多顆獨立 DRM 裝置**的，且 `card` 編號開機時會浮動：

| node（範例） | driver | 是什麼 | 有 KMS？ |
|---|---|---|---|
| `card0` | `drm-rp1-dsi` | **DSI 面板**（走 RP1 南橋）✅ 我們的目標 | 有 |
| `card1` | `v3d` | GPU render（只算圖、沒接頭） | **沒有**（`drmModeGetResources` 會失敗） |
| `card2` | `vc4` | HDMI 兩個輸出 | 有（但沒插就 disconnected） |

跟 RPi4 以前不同：**DSI 走 RP1，不在 `vc4` 上**。因為編號會浮動，
`modeset` 預設會**自動掃描** `/dev/dri/card*`、挑「有 KMS 且有 connected connector」
的那個（見 `open_best_card()`），不用寫死。

## Run

⚠️ **必須在沒有 compositor 占用顯示的環境跑**，否則搶不到 DRM master，
`drmModeSetCrtc` 會回 `-EACCES` / `-EBUSY`：

- 桌面版 Pi OS：先切到純文字 console（`Ctrl+Alt+F2`），或關掉桌面
  （`sudo systemctl stop lightdm` / `sudo raspi-config` 設 console boot）。
- Lite 版 / SSH 進無桌面系統：通常可直接跑。

```bash
./modeset                 # 自動掃描，挑有 KMS+connected 的 card
# 或手動指定（手動優先）：
./modeset /dev/dri/card0
```

預期輸出：

```
自動選用 KMS device: /dev/dri/card0
Connector id=36, 使用 mode: 1280x720 1280x720@62Hz
使用 CRTC id=YY
Dumb buffer: 1280x720 pitch=5120 size=3686400 fb_id=ZZ

>>> 螢幕上應該出現漸層了：左上黑、右上紅、左下綠、右下黃 <<<
>>> 按 Enter 還原並離開 <<<
```

此時 **DSI 螢幕應顯示雙軸漸層**：
- 左→右：黑 → 紅（R 隨 x）
- 上→下：黑 → 綠（G 隨 y）
- 四角：左上黑、右上紅、左下綠、右下黃

按 Enter 後還原原本畫面並離開。

## 怎麼驗證對不對

- **四角顏色對** → SetCrtc / connector / mode 全對。
- **漸層平滑、沒有斜向撕裂或錯位** → `pitch`（stride）算對了
  （程式用 driver 回報的 `pitch` 定位每列，不是自己用 `width*4`）。
- 若顏色通道錯（紅綠對調）→ 像素格式假設（XRGB8888 記憶體序 B,G,R,X）要再查。

## Debug（卡關時）

```bash
# 確認哪個 card node 有 KMS（有 connector/crtc 的那個）
./../w1_drm_info/drm_info /dev/dri/card0
./../w1_drm_info/drm_info /dev/dri/card1

# 看是不是有人占著 DRM master
sudo fuser -v /dev/dri/card1
ps aux | egrep -i 'X|wayland|lightdm|labwc|wf-panel'

# kernel 端有沒有抱怨
dmesg | tail -30
```

| 症狀 | 可能原因 / 解法 |
|---|---|
| `drmModeSetCrtc: Permission denied (-EACCES)` | 有 compositor 當 DRM master → 切 console 或停桌面 |
| `drmModeSetCrtc: Device or resource busy (-EBUSY)` | 同上，或別的程式正占用 CRTC |
| `drmModeGetResources failed` | 開到只有 render 的 node（RPi5 上 v3d 那個沒有 KMS）→ 換另一個 card |
| 螢幕黑 / 沒反應 | 確認 DSI 在 `drm_info` 顯示 `connected`；檢查 `dmesg` panel 有沒有 probe 成功 |

## 檔案

- `modeset.c` — 主程式（find_crtc / create_fb / draw_gradient / SetCrtc 還原）
- `Makefile` — `make` 一鍵 build

## 給之後的 TODO

- 目前是 **single buffer + 靜態畫面**。之後可加 **double buffering + page flip**
  （`drmModePageFlip` + `drmHandleEvent`）做不撕裂的動畫，這也是 Month 3
  zero-copy 串流會用到的呈現模型。
- W4 起改用 **GBM + EGL**：framebuffer 不再 CPU 畫，而是 GPU render，
  再透過 dma-buf 餵給這套同樣的 `drmModeSetCrtc` / `AddFB2` 路徑。
