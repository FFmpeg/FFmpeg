/*
 * Generate the sub-8-bit APNG streams used by the FATE tests.
 *
 * The frames are palette images. The first frame fills the canvas. Every
 * later frame is a smaller rectangle whose x offset is odd, so it is not
 * aligned to the byte that holds the packed samples. These files are built
 * here, the same way audiogen builds its wave files, so the tests do not
 * need media from the fate-suite.
 *
 * A file name containing "biganc" gets a large private ancillary chunk
 * before the first IDAT, to check that probing still finds the animation
 * (issue #26132). A file name containing "plain" omits acTL/fcTL and emits
 * a single frame, giving a non-animated PNG control case.
 *
 * "bndhdr" inserts a caBX immediately after IHDR sized so the next chunk
 * starts at offset 4096, where the probe buffer ends on a header boundary.
 * "bndplte" inserts a 4000-byte caBX there, so a 4-bit PLTE crosses offset
 * 4096. Both names require the depth marker as well.
 *
 * Copyright (c) 2026 Jun Zhao
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Large enough to push the first IDAT past the 2048-byte probe buffer. */
enum { CANVAS_W = 24, CANVAS_H = 20, FRAMES = 6, ANC_SIZE = 4096 };

static const uint8_t ancillary[ANC_SIZE] = { 0 };

static const uint8_t palette[16][3] = {
    {   0,   0,   0 }, { 255,   0,   0 }, {   0, 255,   0 }, {   0,   0, 255 },
    { 255, 255,   0 }, { 255,   0, 255 }, {   0, 255, 255 }, { 255, 128,   0 },
    { 128,   0, 255 }, { 128, 255,   0 }, {   0, 128, 255 }, { 255,   0, 128 },
    {  64,  64,  64 }, { 192, 192, 192 }, { 128,  64,   0 }, { 255, 255, 255 },
};

typedef struct {
    uint8_t data[ANC_SIZE + 2048];
    int len;
} Buf;

static void die(const char *path)
{
    fprintf(stderr, "apnggen: %s: %s\n", path, strerror(errno));
    exit(1);
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
    static uint32_t table[256];
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;

    if (!table[1]) {
        for (i = 0; i < 256; i++) {
            uint32_t c = i;
            int k;

            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    for (i = 0; i < size; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void put(Buf *b, const void *src, int n)
{
    if (n < 0 || b->len > (int)sizeof(b->data) - n) {
        fprintf(stderr, "apnggen: output too large\n");
        exit(1);
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void put_be32(Buf *b, uint32_t v)
{
    uint8_t p[4] = { v >> 24, v >> 16, v >> 8, v };

    put(b, p, 4);
}

static void chunk(Buf *b, const char type[4], const uint8_t *data, int n)
{
    int start = b->len + 4;

    put_be32(b, n);
    put(b, type, 4);
    if (n)
        put(b, data, n);
    put_be32(b, crc32(b->data + start, 4 + n));
}

/* First frame covers the canvas. Later frames are 7x5 rectangles on odd x
 * offsets, one pixel further right each frame. */
static int sample(int depth, int frame, int x, int y)
{
    int max = (1 << depth) - 1;

    if (!frame)
        return (x / 4 + y / 5) & max;
    if (depth == 1)
        return (x + y + frame) & 1;
    return 1 + (x + y + frame) % max;
}

static int pack_frame(uint8_t *dst, int depth, int frame, int w, int h)
{
    int row_bytes = (w * depth + 7) / 8;
    int out = 0, y, x;

    for (y = 0; y < h; y++) {
        dst[out++] = 0;
        memset(dst + out, 0, row_bytes);
        for (x = 0; x < w; x++) {
            int bit = x * depth;

            dst[out + bit / 8] |= sample(depth, frame, x, y) << (8 - depth - bit % 8);
        }
        out += row_bytes;
    }
    return out;
}

/* zlib wrapper around one stored deflate block. No compressor needed. */
static void put_stored(Buf *b, const uint8_t *raw, int n)
{
    unsigned neg = ~n & 0xFFFF;
    uint8_t hdr[7] = { 0x78, 0x01, 0x01, n, n >> 8, neg, neg >> 8 };
    uint32_t a = 1, s = 0;
    int i;

    put(b, hdr, 7);
    put(b, raw, n);
    for (i = 0; i < n; i++) {
        a += raw[i];
        if (a >= 65521)
            a -= 65521;
        s += a;
        if (s >= 65521)
            s -= 65521;
    }
    put_be32(b, (s << 16) | a);
}

static void write_apng(const char *path, int depth, int big_anc, int plain,
                        int anc_after_ihdr)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    uint8_t ihdr[13] = { 0, 0, 0, CANVAS_W, 0, 0, 0, CANVAS_H, depth, 3 };
    uint8_t actl[8] = { 0, 0, 0, FRAMES, 0, 0, 0, 1 };
    uint8_t plte[16 * 3], raw[512], fctl[26];
    Buf out = { 0 }, image = { 0 };
    FILE *f;
    int colors = 1 << depth, seq = 0, frame, frames = plain ? 1 : FRAMES, i;

    if (anc_after_ihdr < 0 || anc_after_ihdr > ANC_SIZE) {
        fprintf(stderr, "apnggen: ancillary payload does not fit\n");
        exit(1);
    }

    for (i = 0; i < colors; i++)
        memcpy(plte + 3 * i, palette[i], 3);

    put(&out, sig, 8);
    chunk(&out, "IHDR", ihdr, sizeof(ihdr));
    if (anc_after_ihdr)
        chunk(&out, "caBX", ancillary, anc_after_ihdr);
    if (!plain)
        chunk(&out, "acTL", actl, sizeof(actl));
    chunk(&out, "PLTE", plte, colors * 3);
    if (big_anc)
        chunk(&out, "caBX", ancillary, sizeof(ancillary));

    for (frame = 0; frame < frames; frame++) {
        int sub = frame != 0;
        int x = sub ? 1 + 2 * (frame - 1) : 0;
        int y = sub ? 1 + (frame - 1) : 0;
        int w = sub ? 7 : CANVAS_W;
        int h = sub ? 5 : CANVAS_H;

        if (!plain) {
            memset(fctl, 0, sizeof(fctl));
            fctl[3] = seq++;
            fctl[7] = w;
            fctl[11] = h;
            fctl[15] = x;
            fctl[19] = y;
            fctl[21] = 1;  /* delay numerator */
            fctl[23] = 10; /* delay denominator: 1/10 s */
            chunk(&out, "fcTL", fctl, sizeof(fctl));
        }

        image.len = 0;
        if (sub)
            put_be32(&image, seq++);
        put_stored(&image, raw, pack_frame(raw, depth, frame, w, h));
        chunk(&out, sub ? "fdAT" : "IDAT", image.data, image.len);
    }
    chunk(&out, "IEND", NULL, 0);

    f = fopen(path, "wb");
    if (!f || fwrite(out.data, 1, out.len, f) != (size_t)out.len || fclose(f))
        die(path);
}

int main(int argc, char **argv)
{
    const char *path = argc == 2 ? argv[1] : NULL;
    int depth = 0, anc_after_ihdr = 0;

    if (!path) {
        fprintf(stderr, "usage: apnggen OUTPUT.png\n");
        return 1;
    }
    if (strstr(path, "1bit"))
        depth = 1;
    else if (strstr(path, "2bit"))
        depth = 2;
    else if (strstr(path, "4bit"))
        depth = 4;
    if (!depth) {
        fprintf(stderr, "apnggen: unsupported file name %s\n", path);
        return 1;
    }
    /* IHDR ends at offset 33. A 4051-byte payload plus its 12-byte wrapper
     * ends at 4096, so the next header is absent from that probe buffer.
     * A 4000-byte payload leaves a 4-bit PLTE crossing the same offset:
     * the chunk ends at 4045, acTL at 4065, and PLTE at 4125. */
    if (strstr(path, "bndhdr"))
        anc_after_ihdr = 4051;
    else if (strstr(path, "bndplte")) {
        if (depth != 4) {
            fprintf(stderr, "apnggen: bndplte requires a 4-bit file name\n");
            return 1;
        }
        anc_after_ihdr = 4000;
    }
    write_apng(path, depth, strstr(path, "biganc") != NULL,
               strstr(path, "plain") != NULL, anc_after_ihdr);
    return 0;
}
