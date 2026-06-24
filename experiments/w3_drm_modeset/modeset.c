/*
 * modeset — Week 3 最小 DRM/KMS 範例
 *
 * 目標：不經 X11 / Wayland，直接把一張「CPU 畫的漸層」推到 DSI 螢幕上。
 *
 * 流程：
 *   open /dev/dri/cardN
 *     → 找 connected 的 connector + 它的 preferred mode
 *     → 找一個能驅動它的 CRTC（掃描引擎）
 *     → 配 dumb buffer（CPU 可寫的 framebuffer）
 *     → drmModeAddFB 把 buffer 包成 DRM framebuffer 物件
 *     → mmap 進來用 CPU 畫漸層
 *     → drmModeSetCrtc：真正點亮螢幕
 *     → 按 Enter → 還原原本畫面 → 清理
 *
 * Build : make
 * Run   : ./modeset [device]      (預設 /dev/dri/card1)
 *
 * 注意：要在「沒有 compositor 占用顯示」的環境跑（純 console / SSH 進 console）。
 *       若 X11 或 wayland 正在跑，會搶不到 DRM master，SetCrtc 會 -EBUSY/-EACCES。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

/* 一塊 dumb buffer + 它對應的 DRM framebuffer 的所有家當 */
struct framebuffer {
    uint32_t handle;   /* GEM handle（kernel 內部的 buffer 編號） */
    uint32_t fb_id;    /* drmModeAddFB 後拿到的 framebuffer 物件 id */
    uint32_t pitch;    /* 每一列幾個 bytes（= width * 4，但 driver 可能 padding） */
    uint64_t size;     /* buffer 總 bytes */
    uint8_t  *map;     /* mmap 後的 CPU 指標 */
    uint32_t width;
    uint32_t height;
};

/*
 * 找一個能驅動這個 connector 的 CRTC。
 *
 * connector → encoder → CRTC 是一條鏈。最簡單情況：connector 目前已綁了一個
 * encoder（conn->encoder_id），那 encoder 綁的 crtc 就能直接用。
 * 萬一沒有（剛開機未設定），就掃所有 encoder，用 possible_crtcs bitmask
 * 找一個合法的 CRTC。
 */
static int find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
                     uint32_t *out_crtc_id) {
    /* 情況 1：connector 已經有 encoder，且 encoder 已綁 CRTC */
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc) {
            if (enc->crtc_id) {
                *out_crtc_id = enc->crtc_id;
                drmModeFreeEncoder(enc);
                return 0;
            }
            drmModeFreeEncoder(enc);
        }
    }

    /* 情況 2：掃 connector 支援的每個 encoder，找第一個可用的 CRTC */
    for (int i = 0; i < conn->count_encoders; i++) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
        if (!enc) continue;

        /* possible_crtcs 是 bitmask：bit j = 1 代表 res->crtcs[j] 可被這個 encoder 用 */
        for (int j = 0; j < res->count_crtcs; j++) {
            if (enc->possible_crtcs & (1u << j)) {
                *out_crtc_id = res->crtcs[j];
                drmModeFreeEncoder(enc);
                return 0;
            }
        }
        drmModeFreeEncoder(enc);
    }
    return -1;
}

/* 配一塊 dumb buffer，包成 DRM framebuffer，並 mmap 進來 */
static int create_fb(int fd, uint32_t width, uint32_t height,
                     struct framebuffer *fb) {
    memset(fb, 0, sizeof(*fb));
    fb->width = width;
    fb->height = height;

    /* 1) 跟 kernel 要一塊 dumb buffer（32 bpp = XRGB8888） */
    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));
    creq.width = width;
    creq.height = height;
    creq.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB");
        return -1;
    }
    fb->handle = creq.handle;
    fb->pitch  = creq.pitch;
    fb->size   = creq.size;

    /* 2) 把這塊 buffer 註冊成 DRM framebuffer 物件
     *    depth=24（RGB 有效位元）, bpp=32（每像素 4 bytes，含 1 byte padding） */
    if (drmModeAddFB(fd, width, height, 24, 32, fb->pitch, fb->handle,
                     &fb->fb_id) < 0) {
        perror("drmModeAddFB");
        goto err_destroy;
    }

    /* 3) 拿到可 mmap 的 offset，再 mmap 到 CPU 位址空間 */
    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = fb->handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB");
        goto err_rmfb;
    }
    fb->map = mmap(0, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, mreq.offset);
    if (fb->map == MAP_FAILED) {
        perror("mmap");
        goto err_rmfb;
    }
    memset(fb->map, 0, fb->size);
    return 0;

err_rmfb:
    drmModeRmFB(fd, fb->fb_id);
err_destroy:
    {
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = fb->handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
    return -1;
}

static void destroy_fb(int fd, struct framebuffer *fb) {
    if (fb->map && fb->map != MAP_FAILED)
        munmap(fb->map, fb->size);
    if (fb->fb_id)
        drmModeRmFB(fd, fb->fb_id);
    if (fb->handle) {
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = fb->handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
}

/*
 * 畫一張漸層：
 *   R 隨 x（左→右 由黑變紅）
 *   G 隨 y（上→下 由黑變綠）
 *   → 左上角黑、右上角紅、左下角綠、右下角黃
 * 這種雙軸漸層能一眼看出：方向有沒有顛倒、pitch（stride）有沒有算對。
 */
static void draw_gradient(struct framebuffer *fb) {
    for (uint32_t y = 0; y < fb->height; y++) {
        /* 用 pitch 而不是 width*4 來定位每一列起點：driver 可能在列尾 padding */
        uint32_t *row = (uint32_t *)(fb->map + y * fb->pitch);
        uint8_t g = (uint8_t)(255 * y / (fb->height - 1));
        for (uint32_t x = 0; x < fb->width; x++) {
            uint8_t r = (uint8_t)(255 * x / (fb->width - 1));
            uint8_t b = 0;
            /* XRGB8888（little-endian 記憶體序 B,G,R,X）→ 用 uint32 直接寫最快 */
            row[x] = (r << 16) | (g << 8) | b;
        }
    }
}

/*
 * 自動挑一個「有 KMS 且有 connected connector」的 card node。
 * 回傳開好的 fd（呼叫端負責 close）；找不到回 -1。
 *
 * 為什麼要掃：RPi5 上 card 編號會浮動，且子系統是拆開的多顆 DRM 裝置 —
 *   drm-rp1-dsi（DSI 面板）/ v3d（只 render、沒 KMS）/ vc4（HDMI）
 * 哪個是 cardN 開機時不保證固定，所以不要寫死，掃過去找對的那個。
 */
static int open_best_card(void) {
    char path[32];
    for (int i = 0; i < 16; i++) {
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;                 /* 編號不連續也沒關係，跳過 */

        drmModeRes *res = drmModeGetResources(fd);
        if (!res) { close(fd); continue; }     /* 只有 render 的 node（如 v3d） */

        int has_conn = 0;
        for (int j = 0; j < res->count_connectors && !has_conn; j++) {
            drmModeConnector *c = drmModeGetConnector(fd, res->connectors[j]);
            if (!c) continue;
            if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
                has_conn = 1;
            drmModeFreeConnector(c);
        }
        drmModeFreeResources(res);

        if (has_conn) {
            printf("自動選用 KMS device: %s\n", path);
            return fd;
        }
        close(fd);
    }
    return -1;
}

int main(int argc, char **argv) {
    int fd;
    if (argc > 1) {
        fd = open(argv[1], O_RDWR | O_CLOEXEC);   /* 手動指定優先 */
        if (fd < 0) { perror("open"); return 1; }
    } else {
        fd = open_best_card();                    /* 沒給就自動掃 */
        if (fd < 0) {
            fprintf(stderr, "找不到有 KMS + connected connector 的 /dev/dri/card*\n");
            return 1;
        }
    }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        fprintf(stderr, "drmModeGetResources failed（這個 node 可能只有 render 沒有 KMS？）\n");
        close(fd);
        return 1;
    }

    /* 找第一個 connected 且有 mode 的 connector */
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) continue;
        if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        drmModeFreeConnector(c);
    }
    if (!conn) {
        fprintf(stderr, "找不到 connected 的 connector\n");
        drmModeFreeResources(res);
        close(fd);
        return 1;
    }

    drmModeModeInfo mode = conn->modes[0];   /* preferred mode */
    printf("Connector id=%u, 使用 mode: %s %ux%u@%uHz\n",
           conn->connector_id, mode.name,
           mode.hdisplay, mode.vdisplay, mode.vrefresh);

    uint32_t crtc_id = 0;
    if (find_crtc(fd, res, conn, &crtc_id) < 0) {
        fprintf(stderr, "找不到可用的 CRTC\n");
        goto cleanup_conn;
    }
    printf("使用 CRTC id=%u\n", crtc_id);

    /* 存下目前 CRTC 狀態，結束時還原（不然螢幕會停在我們的漸層） */
    drmModeCrtc *saved_crtc = drmModeGetCrtc(fd, crtc_id);

    struct framebuffer fb;
    if (create_fb(fd, mode.hdisplay, mode.vdisplay, &fb) < 0) {
        fprintf(stderr, "create_fb 失敗\n");
        goto cleanup_saved;
    }
    printf("Dumb buffer: %ux%u pitch=%u size=%llu fb_id=%u\n",
           fb.width, fb.height, fb.pitch,
           (unsigned long long)fb.size, fb.fb_id);

    draw_gradient(&fb);

    /* 真正點亮螢幕：把 fb 掃到 crtc_id，輸出到 conn->connector_id，用 mode 的時序 */
    uint32_t conn_id = conn->connector_id;
    if (drmModeSetCrtc(fd, crtc_id, fb.fb_id, 0, 0,
                       &conn_id, 1, &mode) < 0) {
        perror("drmModeSetCrtc");
        fprintf(stderr, "（若是 -EACCES/-EBUSY：可能有 compositor 占著 DRM master）\n");
        goto cleanup_fb;
    }

    printf("\n>>> 螢幕上應該出現漸層了：左上黑、右上紅、左下綠、右下黃 <<<\n");
    printf(">>> 按 Enter 還原並離開 <<<\n");
    getchar();

    /* 還原原本的 CRTC 設定 */
    if (saved_crtc) {
        drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
                       saved_crtc->x, saved_crtc->y,
                       &conn_id, 1, &saved_crtc->mode);
    }

cleanup_fb:
    destroy_fb(fd, &fb);
cleanup_saved:
    if (saved_crtc) drmModeFreeCrtc(saved_crtc);
cleanup_conn:
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    close(fd);
    return 0;
}
