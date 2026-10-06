/* SPDX-License-Identifier: GPL-3.0-only */
/* LCD images: PNG (zlib from the macOS SDK) and PPM. The LCD RAM is RGB565 big-endian (as sent). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "emu.h"

static void rgb_of(uint16_t be, uint8_t *p)
{
    uint16_t c = (uint16_t)((be >> 8) | (be << 8));
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    p[0] = (uint8_t)((r << 3) | (r >> 2));
    p[1] = (uint8_t)((g << 2) | (g >> 4));
    p[2] = (uint8_t)((b << 3) | (b >> 2));
}

int emu_write_ppm(const char *path, const uint16_t *lcd, int w, int h)
{
    FILE *f = fopen(path, "wb");
    uint8_t *row;
    int x, y;
    if (!f)
        return -1;
    row = malloc((size_t)w * 3u);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            rgb_of(lcd[y * w + x], row + 3 * x);
        fwrite(row, 3, (size_t)w, f);
    }
    free(row);
    return fclose(f);
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t b[4];
    uLong crc = crc32(0, (const Bytef *)type, 4);
    be32(b, n);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (n)
        fwrite(d, 1, n, f);
    if (n)
        crc = crc32(crc, d, n);
    be32(b, (uint32_t)crc);
    fwrite(b, 1, 4, f);
}

static int png_rows(const char *path, uint8_t *raw, size_t raw_n, int w, int h);

int emu_write_png(const char *path, const uint16_t *lcd, int w, int h)
{
    size_t raw_n = (size_t)h * (size_t)(1 + 3 * w);
    uint8_t *raw = malloc(raw_n);
    int x, y;
    for (y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * (size_t)(1 + 3 * w);
        r[0] = 0;
        for (x = 0; x < w; x++)
            rgb_of(lcd[y * w + x], r + 1 + 3 * x);
    }
    return png_rows(path, raw, raw_n, w, h);
}

int emu_write_png_argb(const char *path, const uint32_t *px, int w, int h)
{
    size_t raw_n = (size_t)h * (size_t)(1 + 3 * w);
    uint8_t *raw = malloc(raw_n);
    int x, y;
    for (y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * (size_t)(1 + 3 * w);
        r[0] = 0;
        for (x = 0; x < w; x++) {
            uint32_t c = px[y * w + x];
            r[1 + 3 * x] = (uint8_t)(c >> 16);
            r[2 + 3 * x] = (uint8_t)(c >> 8);
            r[3 + 3 * x] = (uint8_t)c;
        }
    }
    return png_rows(path, raw, raw_n, w, h);
}

static int png_rows(const char *path, uint8_t *raw, size_t raw_n, int w, int h)   /* frees raw */
{
    uint8_t hdr[13];
    uLongf zn = compressBound((uLong)raw_n);
    uint8_t *z = malloc(zn);
    FILE *f;
    if (compress2(z, &zn, raw, (uLong)raw_n, 9) != Z_OK || !(f = fopen(path, "wb"))) {
        free(raw);
        free(z);
        return -1;
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(hdr, (uint32_t)w);
    be32(hdr + 4, (uint32_t)h);
    hdr[8] = 8; hdr[9] = 2; hdr[10] = 0; hdr[11] = 0; hdr[12] = 0;   /* 8-bit RGB */
    chunk(f, "IHDR", hdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    return fclose(f);
}
