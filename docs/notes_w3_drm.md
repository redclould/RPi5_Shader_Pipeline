# W3 筆記 — DRM/KMS 最小 modeset：直接點亮 DSI

> ✅ 2026-06-25 完成。實驗碼：[`experiments/w3_drm_modeset/`](../experiments/w3_drm_modeset/)。
> 成果：不經 X/Wayland，自寫程式用 dumb buffer + `drmModeSetCrtc` 把 CPU 畫的
> 雙軸漸層點到 Waveshare 7" DSI 上，四角顏色正確、無撕裂。

---

## 1. DRM/KMS 物件模型（這週真正要內化的）

KMS（Kernel Mode Setting）把「一塊記憶體 → 螢幕」拆成幾個物件，一條鏈串起來：

```
Framebuffer ── CRTC ── Encoder ── Connector ── 實體面板
 (一塊 buffer)  (掃描   (訊號     (接頭，如
                 引擎)   編碼)      DSI/HDMI)
```

- **Framebuffer**：對「一塊 buffer + 它的寬高/pitch/像素格式」的描述。用
  `drmModeAddFB(fd, w, h, depth, bpp, pitch, handle, &fb_id)` 註冊，拿到 `fb_id`。
- **CRTC**（CRT Controller）：掃描引擎。把指定 framebuffer 依某個 **mode** 的時序
  一行一行掃出去。`drmModeSetCrtc(fd, crtc, fb, x, y, &conn, 1, &mode)` = **點亮的那一刻**。
- **Encoder**：把 CRTC 的像素流轉成接頭要的訊號（DSI / TMDS…）。我們不直接碰，
  靠它在 connector↔CRTC 間牽線。
- **Connector**：實體輸出口 + 它能跑的 mode 清單 + 連接狀態。
- **mode**：解析度 + 刷新率 + 同步時序。用 connector 的 `modes[0]`（preferred）。

找 CRTC 的兩段式邏輯（`find_crtc()`）：先看 connector 目前綁的 encoder 有沒有
現成 CRTC；沒有就掃 `encoder->possible_crtcs` 這個 **bitmask**（bit j=1 表示
`res->crtcs[j]` 可用）挑一個。

---

## 2. dumb buffer：CPU 可寫的最簡 framebuffer

W3 還沒上 GPU，所以用 **dumb buffer**——DRM 給的「笨」buffer，唯一賣點是
**CPU 能直接 mmap 寫**。三步（都走 ioctl，最底層、最好懂）：

```c
DRM_IOCTL_MODE_CREATE_DUMB   // 要一塊 buffer → 拿到 handle / pitch / size
drmModeAddFB(...)            // 包成 framebuffer 物件 → fb_id
DRM_IOCTL_MODE_MAP_DUMB      // 拿到可 mmap 的 offset → mmap 進 CPU 位址
```

之後 Month 3 的 zero-copy，就是把這塊「CPU 畫的 dumb buffer」換成
**GPU render 出來、用 dma-buf 包的 buffer**，但 `drmModeAddFB2` + `drmModeSetCrtc`
這條呈現路徑一模一樣。**W3 等於先把終點站蓋好。**

### pitch（stride）是這週最容易踩的坑

`pitch` = 每一列實際佔幾 bytes，**driver 回報、不保證等於 `width × bpp/8`**
（可能對齊 padding）。畫圖一定要用 `map + y * pitch` 定位每列起點：

```c
uint32_t *row = (uint32_t *)(fb->map + y * fb->pitch);
row[x] = (r << 16) | (g << 8) | b;   // XRGB8888：記憶體序 B,G,R,X
```

本次 1280×720 拿到 `pitch=5120`（= 1280×4，剛好無 padding），漸層平滑無斜向撕裂
→ 證明 stride 用對了。若誤用 `width*4` 而 driver 有 padding，畫面會斜著歪掉。

---

## 3. RPi5 專屬發現

### (a) 顯示/繪圖是三顆獨立 DRM 裝置，且 card 編號浮動

| node（本次） | driver | 角色 | KMS？ |
|---|---|---|---|
| `card0` | `drm-rp1-dsi` | **DSI 面板**（走 RP1 南橋） | ✅ |
| `card1` | `v3d` | GPU render（只算圖） | ❌ `drmModeGetResources` 失敗 |
| `card2` | `vc4` | HDMI ×2 | ✅（沒插→disconnected） |

關鍵：**DSI 不在 `vc4`，在 `drm-rp1-dsi`（RP1）**，跟 RPi4 以前不同。而且
`cardN` 編號開機會變（W1 時 DSI 還在 card1）。所以程式**不寫死編號**，改成
`open_best_card()`：掃 `/dev/dri/card*`，挑「`drmModeGetResources` 成功 **且** 有
CONNECTED connector」的那個。

### (b) DSI 沒有 hot-plug detect

沒接面板時 connector 仍報 `connected`！因為 DSI **沒有偵測線路**——panel 的存在/
規格全是 device tree overlay（`vc4-kms-dsi-waveshare-panel,7_0_inchH`）**宣告**的，
driver 一載入就把 connector 標 `connected`。對比 HDMI 有 HPD + EDID，是真的電氣偵測
（所以本次兩個 HDMI 都正確顯示 `disconnected`）。

> 實務影響：沒接面板也能 SetCrtc「成功」，但畫面無處可顯示。要看到圖必須實體接上。

### (c) DRM master 互斥 → 跑之前要停 compositor

`drmModeSetCrtc` 回 **`-EACCES`** 不是檔案權限，是 **DRM master 被占**：同一時間
只有一個程式能改顯示輸出。Pi OS Bookworm 桌面跑 **`labwc`**（Wayland，由 `lightdm`
拉起）當 master。從 SSH 跑的程式搶不到（master 發給 active VT 的 session，SSH 不綁 VT）。

解法：

```bash
sudo systemctl stop lightdm   # labwc 收掉，master 釋放
./modeset                     # 第一個開 card 者成為 master → SetCrtc 成功
# 看完按 Enter
sudo systemctl start lightdm  # 桌面叫回來
```

---

## 4. 面試會被問到的關鍵字（自我檢查能不能講清楚）

- KMS 物件：framebuffer / CRTC / encoder / connector / mode 各自負責什麼。
- dumb buffer vs dma-buf：前者 CPU 畫、後者給 zero-copy；呈現路徑相同。
- **pitch/stride 為什麼可能 ≠ width×bpp**（對齊、bandwidth）。
- DRM master / DRM lease：為何同時只能一個 master，compositor 與裸 KMS 程式的衝突。
- atomic modesetting（本次用 legacy `SetCrtc`；之後可升級 atomic commit）。
- DSI（無 HPD、device-tree 描述）vs HDMI（HPD + EDID 動態偵測）的差異。

---

## 5. 給下週 (W4) 的伏筆

W4：**GBM + EGL** 在無 X11 下跑 GLES，畫一個三角形。屆時 framebuffer 不再 CPU 畫，
而是 GPU render 到 GBM buffer，再透過 dma-buf / `drmModeAddFB2` 餵回**這週同一條**
`drmModeSetCrtc` 路徑。W3 的 KMS 終點站不變，只是換掉「誰來填 buffer」。

## TODO（非阻塞）

- 目前 single buffer + 靜態畫面。之後可加 **double buffering + `drmModePageFlip`
  + `drmHandleEvent`** 做不撕裂動畫——這也是 Month 3 串流的呈現模型。
- 升級到 **atomic API**（`drmModeAtomicCommit`）會比 legacy `SetCrtc` 更接近現代
  compositor 的做法，面試加分。
