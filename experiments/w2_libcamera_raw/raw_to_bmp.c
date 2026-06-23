// raw_to_bmp.c
//
// 把 capture_raw 存出的 SBGGR16 raw（unpacked 16-bit Bayer）轉成 8-bit 灰階 BMP，
// 方便在 macOS 預覽程式直接打開，確認真的有拍到畫面。
//
// 注意：這只是把 Bayer 馬賽克當灰階看，還沒 demosaic，所以會偏綠/有格狀紋理 —— 正常。
// 真正的 RAW→RGB demosaic 是 Month 4 的事。
//
// 用法： ./raw_to_bmp imx708_7.raw 4608 2592 out.bmp

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void put_u16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "用法: %s <in.raw> <width> <height> <out.bmp>\n", argv[0]);
        return 1;
    }
    const char *inPath = argv[1];
    int W = atoi(argv[2]);
    int H = atoi(argv[3]);
    const char *outPath = argv[4];

    if (W <= 0 || H <= 0 || W % 4 != 0) {
        fprintf(stderr, "width/height 不合理（width 需為 4 的倍數）\n");
        return 1;
    }

    size_t npix = (size_t)W * H;
    uint16_t *raw = malloc(npix * sizeof(uint16_t));
    uint8_t *gray = malloc(npix);
    if (!raw || !gray) { fprintf(stderr, "記憶體不足\n"); return 1; }

    FILE *in = fopen(inPath, "rb");
    if (!in) { perror("fopen in"); return 1; }
    if (fread(raw, sizeof(uint16_t), npix, in) != npix) {
        fprintf(stderr, "讀檔大小不符，確認 width/height 對不對\n");
        return 1;
    }
    fclose(in);

    // PiSP 的 SBGGR16 是「左對齊」：10-bit 值放在高位 [15:6]（等於 ×64）。
    // 先還原成 10-bit 再做顯示處理。
    uint16_t mn16 = 0xFFFF, mx16 = 0;
    unsigned long long sum = 0;
    unsigned max10 = 0;
    for (size_t i = 0; i < npix; i++) {
        uint16_t v = raw[i];
        if (v < mn16) mn16 = v;
        if (v > mx16) mx16 = v;
        sum += v;
        unsigned v10 = v >> 6;
        if (v10 > max10) max10 = v10;
    }
    printf("raw16 統計: min=%u max=%u mean=%.1f  (→ 10-bit max=%u)\n",
           mn16, mx16, (double)sum / npix, max10);

    // 顯示用：扣掉黑階、線性拉到亮點、再套 gamma 讓暗部看得見。
    // 注意這只是「為了人眼看」的處理，不是 demosaic（那是 Month 4）。
    const unsigned BL = 64;                 // IMX708 10-bit 黑階約 64
    unsigned span = (max10 > BL) ? (max10 - BL) : 1;
    for (size_t i = 0; i < npix; i++) {
        int s = (int)(raw[i] >> 6) - (int)BL;
        if (s < 0) s = 0;
        double norm = (double)s / span;          // 0..1
        double g = pow(norm, 1.0 / 2.2);         // gamma，提亮暗部
        unsigned v = (unsigned)(g * 255.0 + 0.5);
        gray[i] = v > 255 ? 255 : v;
    }

    // 8-bit 灰階 BMP：14 (file header) + 40 (info header) + 256*4 (palette)
    const uint32_t paletteBytes = 256 * 4;
    const uint32_t dataOffset = 14 + 40 + paletteBytes;
    const uint32_t imageBytes = npix;          // W%4==0 → 不需 row padding
    const uint32_t fileSize = dataOffset + imageBytes;

    uint8_t fh[14] = {0};
    fh[0]='B'; fh[1]='M';
    put_u32(fh+2, fileSize);
    put_u32(fh+10, dataOffset);

    uint8_t ih[40] = {0};
    put_u32(ih+0, 40);
    put_u32(ih+4, (uint32_t)W);
    put_u32(ih+8, (uint32_t)(-H));   // 負高度 = top-down，省得上下顛倒
    put_u16(ih+12, 1);               // planes
    put_u16(ih+14, 8);               // bpp
    put_u32(ih+20, imageBytes);
    put_u32(ih+32, 256);             // colors used
    put_u32(ih+36, 256);             // important colors

    FILE *out = fopen(outPath, "wb");
    if (!out) { perror("fopen out"); return 1; }
    fwrite(fh, 1, 14, out);
    fwrite(ih, 1, 40, out);
    for (int i = 0; i < 256; i++) {
        uint8_t entry[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, 0 }; // B,G,R,reserved
        fwrite(entry, 1, 4, out);
    }
    fwrite(gray, 1, npix, out);
    fclose(out);

    printf("已輸出 %s (%dx%d, 8-bit 灰階)\n", outPath, W, H);
    free(raw);
    free(gray);
    return 0;
}
