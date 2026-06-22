# ShaderPipeline

A zero-copy image pipeline on Raspberry Pi 5: **Camera → GPU compute shader → DSI display**, bypassing the vendor ISP.

Portfolio project building toward GPU/camera firmware engineering roles and UK/US graduate school applications.

## What it does

```
Camera Module v3 (IMX708, Bayer RAW10)
        |
        | libcamera + V4L2 (dma-buf fd)
        v
GLES 3.1 compute shader on VideoCore VII (V3D)
  - Bayer demosaic
  - Black level / white balance / CCM
  - Gamma + tone mapping
        |
        | dma-buf fd
        v
DRM/KMS -> Waveshare 7" DSI display (drm-rp1-dsi)
```

No CPU-side memory copies between stages. The full RAW -> RGB pipeline runs in custom GLSL compute shaders, replacing the proprietary ISP.

## Hardware

- **Raspberry Pi 5** — BCM2712, Cortex-A76 @ 2.4 GHz, VideoCore VII GPU
- **Raspberry Pi Camera Module v3** — Sony IMX708, CSI-2
- **Waveshare 7" DSI capacitive touch LCD (H)** — 1280x720, GT911 touch

## Software stack

- Userspace: libcamera, EGL on GBM (no X11/Wayland), GLES 3.1 compute
- Kernel interfaces: V4L2, DRM/KMS, dma-buf
- Drivers: Mesa V3D, `drm-rp1-dsi`, `vc4`

## Status

Currently **Month 1, Week 3** of a 6-month plan. See [`docs/roadmap.md`](docs/roadmap.md).

- [x] Week 1 — Environment, toolchain, first DRM program
- [x] Week 2 — libcamera uncompressed RAW (SBGGR16) capture from IMX708
- [ ] Week 3 — DRM/KMS framebuffer to DSI
- [ ] Week 4 — EGL + GLES triangle on DSI

## Repository layout

```
ShaderPipeline/
├── README.md           # this file
├── CLAUDE.md           # collaboration notes (zh-TW)
├── docs/               # planning, roadmap, learning notes
├── experiments/        # weekly stand-alone validation programs
│   └── w1_drm_info/    # first DRM program — list connectors/CRTCs
└── src/                # main pipeline source (from Month 2)
```

## Building experiments

Each experiment is self-contained and built natively on the Pi 5 (no cross-compile):

```bash
cd experiments/w1_drm_info
make
./drm_info
```

Tested on Raspberry Pi OS Bookworm (64-bit), kernel 6.12.x.

## License

TBD.
