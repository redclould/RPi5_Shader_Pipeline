/*
 * drm_info — Week 1 sanity check
 * 列出 DRM device 的 driver / connector / CRTC / encoder 基本資訊。
 * Build: make
 * Run  : ./drm_info [device]   (預設 /dev/dri/card1)
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static const char *connector_type_name(uint32_t type) {
    switch (type) {
        case DRM_MODE_CONNECTOR_HDMIA:       return "HDMI-A";
        case DRM_MODE_CONNECTOR_HDMIB:       return "HDMI-B";
        case DRM_MODE_CONNECTOR_DSI:         return "DSI";
        case DRM_MODE_CONNECTOR_DPI:         return "DPI";
        case DRM_MODE_CONNECTOR_VGA:         return "VGA";
        case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
        case DRM_MODE_CONNECTOR_eDP:         return "eDP";
        default:                             return "Unknown";
    }
}

static const char *connection_status(drmModeConnection c) {
    switch (c) {
        case DRM_MODE_CONNECTED:         return "connected";
        case DRM_MODE_DISCONNECTED:      return "disconnected";
        case DRM_MODE_UNKNOWNCONNECTION: return "unknown";
        default:                         return "?";
    }
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/dev/dri/card1";

    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open"); return 1; }

    drmVersion *ver = drmGetVersion(fd);
    if (ver) {
        printf("Device : %s\n", path);
        printf("Driver : %s v%d.%d.%d (%s)\n",
               ver->name, ver->version_major, ver->version_minor,
               ver->version_patchlevel, ver->date);
        printf("Desc   : %s\n\n", ver->desc);
        drmFreeVersion(ver);
    }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        fprintf(stderr, "drmModeGetResources failed\n");
        close(fd);
        return 1;
    }

    printf("CRTCs      : %d\n", res->count_crtcs);
    printf("Encoders   : %d\n", res->count_encoders);
    printf("Connectors : %d\n\n", res->count_connectors);

    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) continue;

        printf("  [id=%u] %s-%u : %-12s  modes=%d\n",
               c->connector_id,
               connector_type_name(c->connector_type),
               c->connector_type_id,
               connection_status(c->connection),
               c->count_modes);

        if (c->count_modes > 0) {
            drmModeModeInfo *m = &c->modes[0];
            printf("            preferred: %ux%u @ %u Hz\n",
                   m->hdisplay, m->vdisplay, m->vrefresh);
        }
        drmModeFreeConnector(c);
    }

    drmModeFreeResources(res);
    close(fd);
    return 0;
}
