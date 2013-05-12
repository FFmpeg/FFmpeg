/*
 * IFF ACBM/DEEP/ILBM/PBM bitmap decoder
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
 * Copyright (c) 2010 Sebastian Vater <cdgs.basty@googlemail.com>
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

/**
 * @file
 * IFF ACBM/DEEP/ILBM/PBM bitmap decoder
 */

#include "libavutil/imgutils.h"
#include "bytestream.h"
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"

// TODO: masking bits
typedef enum {
    MASK_NONE,
    MASK_HAS_MASK,
    MASK_HAS_TRANSPARENT_COLOR,
    MASK_LASSO
} mask_type;

typedef struct {
    AVFrame *frame;
    int planesize;
    uint8_t * planebuf;
    uint8_t * ham_buf;      ///< temporary buffer for planar to chunky conversation
    uint32_t *ham_palbuf;   ///< HAM decode table
    uint32_t *mask_buf;     ///< temporary buffer for palette indices
    uint32_t *mask_palbuf;  ///< masking palette table
    unsigned  compression;  ///< delta compression method used
    unsigned  bpp;          ///< bits per plane to decode (differs from bits_per_coded_sample if HAM)
    unsigned  ham;          ///< 0 if non-HAM or number of hold bits (6 for bpp > 6, 4 otherwise)
    unsigned  flags;        ///< 1 for EHB, 0 is no extra half darkening
    unsigned  transparency; ///< TODO: transparency color index in palette
    unsigned  masking;      ///< TODO: masking method used
    int init; // 1 if buffer and palette data already initialized, 0 otherwise
    int16_t   tvdc[16];     ///< TVDC lookup table
} IffContext;

#define LUT8_PART(plane, v)                             \
    AV_LE2NE64C(UINT64_C(0x0000000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010000)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0000100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010100)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0000001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010001)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0000101)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1000101)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x0010101)<<32 | v) << plane,  \
    AV_LE2NE64C(UINT64_C(0x1010101)<<32 | v) << plane

#define LUT8(plane) {                           \
    LUT8_PART(plane, 0x0000000),                \
    LUT8_PART(plane, 0x1000000),                \
    LUT8_PART(plane, 0x0010000),                \
    LUT8_PART(plane, 0x1010000),                \
    LUT8_PART(plane, 0x0000100),                \
    LUT8_PART(plane, 0x1000100),                \
    LUT8_PART(plane, 0x0010100),                \
    LUT8_PART(plane, 0x1010100),                \
    LUT8_PART(plane, 0x0000001),                \
    LUT8_PART(plane, 0x1000001),                \
    LUT8_PART(plane, 0x0010001),                \
    LUT8_PART(plane, 0x1010001),                \
    LUT8_PART(plane, 0x0000101),                \
    LUT8_PART(plane, 0x1000101),                \
    LUT8_PART(plane, 0x0010101),                \
    LUT8_PART(plane, 0x1010101),                \
}

// 8 planes * 8-bit mask
static const uint64_t plane8_lut[8][256] = {
    LUT8(0), LUT8(1), LUT8(2), LUT8(3),
    LUT8(4), LUT8(5), LUT8(6), LUT8(7),
};

#define LUT32(plane) {                                \
             0,          0,          0,          0,   \
             0,          0,          0, 1 << plane,   \
             0,          0, 1 << plane,          0,   \
             0,          0, 1 << plane, 1 << plane,   \
             0, 1 << plane,          0,          0,   \
             0, 1 << plane,          0, 1 << plane,   \
             0, 1 << plane, 1 << plane,          0,   \
             0, 1 << plane, 1 << plane, 1 << plane,   \
    1 << plane,          0,          0,          0,   \
    1 << plane,          0,          0, 1 << plane,   \
    1 << plane,          0, 1 << plane,          0,   \
    1 << plane,          0, 1 << plane, 1 << plane,   \
    1 << plane, 1 << plane,          0,          0,   \
    1 << plane, 1 << plane,          0, 1 << plane,   \
    1 << plane, 1 << plane, 1 << plane,          0,   \
    1 << plane, 1 << plane, 1 << plane, 1 << plane,   \
}

// 32 planes * 4-bit mask * 4 lookup tables each
static const uint32_t plane32_lut[32][16*4] = {
    LUT32( 0), LUT32( 1), LUT32( 2), LUT32( 3),
    LUT32( 4), LUT32( 5), LUT32( 6), LUT32( 7),
    LUT32( 8), LUT32( 9), LUT32(10), LUT32(11),
    LUT32(12), LUT32(13), LUT32(14), LUT32(15),
    LUT32(16), LUT32(17), LUT32(18), LUT32(19),
    LUT32(20), LUT32(21), LUT32(22), LUT32(23),
    LUT32(24), LUT32(25), LUT32(26), LUT32(27),
    LUT32(28), LUT32(29), LUT32(30), LUT32(31),
};

// Gray to RGB, required for palette table of grayscale images with bpp < 8
static av_always_inline uint32_t gray2rgb(const uint32_t x) {
    return x << 16 | x << 8 | x;
}

/**
 * Convert CMAP buffer (stored in extradata) to lavc palette format
 */
static int cmap_read_palette(AVCodecContext *avctx, uint32_t *pal)
{
    IffContext *s = avctx->priv_data;
    int count, i;
    const uint8_t *const palette = avctx->extradata + AV_RB16(avctx->extradata);
    int palette_size = avctx->extradata_size - AV_RB16(avctx->extradata);

    if (avctx->bits_per_coded_sample > 8) {
        av_log(avctx, AV_LOG_ERROR, "bits_per_coded_sample > 8 not supported\n");
        return AVERROR_INVALIDDATA;
    }

    count = 1 << avctx->bits_per_coded_sample;
    // If extradata is smaller than actually needed, fill the remaining with black.
    count = FFMIN(palette_size / 3, count);
    if (count) {
        for (i=0; i < count; i++) {
            pal[i] = 0xFF000000 | AV_RB24(palette + i*3);
        }
        if (s->flags && count >= 32) { // EHB
            for (i = 0; i < 32; i++)
                pal[i + 32] = 0xFF000000 | (AV_RB24(palette + i*3) & 0xFEFEFE) >> 1;
            count = FFMAX(count, 64);
        }
    } else { // Create gray-scale color palette for bps < 8
        count = 1 << avctx->bits_per_coded_sample;

        for (i=0; i < count; i++) {
            pal[i] = 0xFF000000 | gray2rgb((i * 255) >> avctx->bits_per_coded_sample);
        }
    }
    if (s->masking == MASK_HAS_MASK) {
        memcpy(pal + (1 << avctx->bits_per_coded_sample), pal, count * 4);
        for (i = 0; i < count; i++)
            pal[i] &= 0xFFFFFF;
    } else if (s->masking == MASK_HAS_TRANSPARENT_COLOR &&
        s->transparency < 1 << avctx->bits_per_coded_sample)
        pal[s->transparency] &= 0xFFFFFF;
    return 0;
}

/**
 * Extracts the IFF extra context and updates internal
 * decoder structures.
 *
 * @param avctx the AVCodecContext where to extract extra context to
 * @param avpkt the AVPacket to extract extra context from or NULL to use avctx
 * @return 0 in case of success, a negative error code otherwise
 */
static int extract_header(AVCodecContext *const avctx,
                          const AVPacket *const avpkt) {
    const uint8_t *buf;
    unsigned buf_size;
    IffContext *s = avctx->priv_data;
    int i, palette_size;

    if (avctx->extradata_size < 2) {
        av_log(avctx, AV_LOG_ERROR, "not enough extradata\n");
        return AVERROR_INVALIDDATA;
    }
    palette_size = avctx->extradata_size - AV_RB16(avctx->extradata);

    if (avpkt) {
        int image_size;
        if (avpkt->size < 2)
            return AVERROR_INVALIDDATA;
        image_size = avpkt->size - AV_RB16(avpkt->data);
        buf = avpkt->data;
        buf_size = bytestream_get_be16(&buf);
        if (buf_size <= 1 || image_size <= 1) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid image size received: %u -> image data offset: %d\n",
                   buf_size, image_size);
            return AVERROR_INVALIDDATA;
        }
    } else {
        buf = avctx->extradata;
        buf_size = bytestream_get_be16(&buf);
        if (buf_size <= 1 || palette_size < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid palette size received: %u -> palette data offset: %d\n",
                   buf_size, palette_size);
            return AVERROR_INVALIDDATA;
        }
    }

    if (buf_size >= 41) {
        s->compression  = bytestream_get_byte(&buf);
        s->bpp          = bytestream_get_byte(&buf);
        s->ham          = bytestream_get_byte(&buf);
        s->flags        = bytestream_get_byte(&buf);
        s->transparency = bytestream_get_be16(&buf);
        s->masking      = bytestream_get_byte(&buf);
        for (i = 0; i < 16; i++)
            s->tvdc[i] = bytestream_get_be16(&buf);

        if (s->masking == MASK_HAS_MASK) {
            if (s->bpp >= 8 && !s->ham) {
                avctx->pix_fmt = AV_PIX_FMT_RGB32;
                av_freep(&s->mask_buf);
                av_freep(&s->mask_palbuf);
                s->mask_buf = av_malloc((s->planesize * 32) + FF_INPUT_BUFFER_PADDING_SIZE);
                if (!s->mask_buf)
                    return AVERROR(ENOMEM);
                if (s->bpp > 16) {
                    av_log(avctx, AV_LOG_ERROR, "bpp %d too large for palette\n", s->bpp);
                    av_freep(&s->mask_buf);
                    return AVERROR(ENOMEM);
                }
                s->mask_palbuf = av_malloc((2 << s->bpp) * sizeof(uint32_t) + FF_INPUT_BUFFER_PADDING_SIZE);
                if (!s->mask_palbuf) {
                    av_freep(&s->mask_buf);
                    return AVERROR(ENOMEM);
                }
            }
            s->bpp++;
        } else if (s->masking != MASK_NONE && s->masking != MASK_HAS_TRANSPARENT_COLOR) {
            av_log(avctx, AV_LOG_ERROR, "Masking not supported\n");
            return AVERROR_PATCHWELCOME;
        }
        if (!s->bpp || s->bpp > 32) {
            av_log(avctx, AV_LOG_ERROR, "Invalid number of bitplanes: %u\n", s->bpp);
            return AVERROR_INVALIDDATA;
        } else if (s->ham >= 8) {
            av_log(avctx, AV_LOG_ERROR, "Invalid number of hold bits for HAM: %u\n", s->ham);
            return AVERROR_INVALIDDATA;
        }

        av_freep(&s->ham_buf);
        av_freep(&s->ham_palbuf);

        if (s->ham) {
            int i, count = FFMIN(palette_size / 3, 1 << s->ham);
            int ham_count;
            const uint8_t *const palette = avctx->extradata + AV_RB16(avctx->extradata);

            s->ham_buf = av_malloc((s->planesize * 8) + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!s->ham_buf)
                return AVERROR(ENOMEM);

            ham_count = 8 * (1 << s->ham);
            s->ham_palbuf = av_malloc((ham_count << !!(s->masking == MASK_HAS_MASK)) * sizeof (uint32_t) + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!s->ham_palbuf) {
                av_freep(&s->ham_buf);
                return AVERROR(ENOMEM);
            }

            if (count) { // HAM with color palette attached
                // prefill with black and palette and set HAM take direct value mask to zero
                memset(s->ham_palbuf, 0, (1 << s->ham) * 2 * sizeof (uint32_t));
                for (i=0; i < count; i++) {
                    s->ham_palbuf[i*2+1] = 0xFF000000 | AV_RL24(palette + i*3);
                }
                count = 1 << s->ham;
            } else { // HAM with grayscale color palette
                count = 1 << s->ham;
                for (i=0; i < count; i++) {
                    s->ham_palbuf[i*2]   = 0xFF000000; // take direct color value from palette
                    s->ham_palbuf[i*2+1] = 0xFF000000 | av_le2ne32(gray2rgb((i * 255) >> s->ham));
                }
            }
            for (i=0; i < count; i++) {
                uint32_t tmp = i << (8 - s->ham);
                tmp |= tmp >> s->ham;
                s->ham_palbuf[(i+count)*2]     = 0xFF00FFFF; // just modify blue color component
                s->ham_palbuf[(i+count*2)*2]   = 0xFFFFFF00; // just modify red color component
                s->ham_palbuf[(i+count*3)*2]   = 0xFFFF00FF; // just modify green color component
                s->ham_palbuf[(i+count)*2+1]   = 0xFF000000 | tmp << 16;
                s->ham_palbuf[(i+count*2)*2+1] = 0xFF000000 | tmp;
                s->ham_palbuf[(i+count*3)*2+1] = 0xFF000000 | tmp << 8;
            }
            if (s->masking == MASK_HAS_MASK) {
                for (i = 0; i < ham_count; i++)
                    s->ham_palbuf[(1 << s->bpp) + i] = s->ham_palbuf[i] | 0xFF000000;
            }
        }
    }

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    int err;

    if (avctx->bits_per_coded_sample <= 8) {
        int palette_size;

        if (avctx->extradata_size >= 2)
            palette_size = avctx->extradata_size - AV_RB16(avctx->extradata);
        else
            palette_size = 0;
        avctx->pix_fmt = (avctx->bits_per_coded_sample < 8) ||
                         (avctx->extradata_size >= 2 && palette_size) ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_GRAY8;
    } else if (avctx->bits_per_coded_sample <= 32) {
        if (avctx->codec_tag == MKTAG('R','G','B','8')) {
            avctx->pix_fmt = AV_PIX_FMT_RGB32;
        } else if (avctx->codec_tag == MKTAG('R','G','B','N')) {
            avctx->pix_fmt = AV_PIX_FMT_RGB444;
        } else if (avctx->codec_tag != MKTAG('D','E','E','P')) {
            if (avctx->bits_per_coded_sample == 24) {
                avctx->pix_fmt = AV_PIX_FMT_0BGR32;
            } else if (avctx->bits_per_coded_sample == 32) {
                avctx->pix_fmt = AV_PIX_FMT_BGR32;
            } else {
                avpriv_request_sample(avctx, "unknown bits_per_coded_sample");
                return AVERROR_PATCHWELCOME;
            }
        }
    } else {
        return AVERROR_INVALIDDATA;
    }

    if ((err = av_image_check_size(avctx->width, avctx->height, 0, avctx)))
        return err;
    s->planesize = FFALIGN(avctx->width, 16) >> 3; // Align plane size in bits to word-boundary
    s->planebuf = av_malloc(s->planesize + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!s->planebuf)
        return AVERROR(ENOMEM);

    s->bpp = avctx->bits_per_coded_sample;
    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    if ((err = extract_header(avctx, NULL)) < 0)
        return err;

    return 0;
}

/**
 * Decode interleaved plane buffer up to 8bpp
 * @param dst Destination buffer
 * @param buf Source buffer
 * @param buf_size
 * @param plane plane number to decode as
 */
static void decodeplane8(uint8_t *dst, const uint8_t *buf, int buf_size, int plane)
{
    const uint64_t *lut = plane8_lut[plane];
    if (plane >= 8) {
        av_log(NULL, AV_LOG_WARNING, "Ignoring extra planes beyond 8\n");
        return;
    }
    do {
        uint64_t v = AV_RN64A(dst) | lut[*buf++];
        AV_WN64A(dst, v);
        dst += 8;
    } while (--buf_size);
}

/**
 * Decode interleaved plane buffer up to 24bpp
 * @param dst Destination buffer
 * @param buf Source buffer
 * @param buf_size
 * @param plane plane number to decode as
 */
static void decodeplane32(uint32_t *dst, const uint8_t *buf, int buf_size, int plane)
{
    const uint32_t *lut = plane32_lut[plane];
    do {
        unsigned mask = (*buf >> 2) & ~3;
        dst[0] |= lut[mask++];
        dst[1] |= lut[mask++];
        dst[2] |= lut[mask++];
        dst[3] |= lut[mask];
        mask = (*buf++ << 2) & 0x3F;
        dst[4] |= lut[mask++];
        dst[5] |= lut[mask++];
        dst[6] |= lut[mask++];
        dst[7] |= lut[mask];
        dst += 8;
    } while (--buf_size);
}

#define DECODE_HAM_PLANE32(x)       \
    first       = buf[x] << 1;      \
    second      = buf[(x)+1] << 1;  \
    delta      &= pal[first++];     \
    delta      |= pal[first];       \
    dst[x]      = delta;            \
    delta      &= pal[second++];    \
    delta      |= pal[second];      \
    dst[(x)+1]  = delta

/**
 * Converts one line of HAM6/8-encoded chunky buffer to 24bpp.
 *
 * @param dst the destination 24bpp buffer
 * @param buf the source 8bpp chunky buffer
 * @param pal the HAM decode table
 * @param buf_size the plane size in bytes
 */
static void decode_ham_plane32(uint32_t *dst, const uint8_t  *buf,
                               const uint32_t *const pal, unsigned buf_size)
{
    uint32_t delta = pal[1]; /* first palette entry */
    do {
        uint32_t first, second;
        DECODE_HAM_PLANE32(0);
        DECODE_HAM_PLANE32(2);
        DECODE_HAM_PLANE32(4);
        DECODE_HAM_PLANE32(6);
        buf += 8;
        dst += 8;
    } while (--buf_size);
}

static void lookup_pal_indicies(uint32_t *dst, const uint32_t *buf,
                         const uint32_t *const pal, unsigned width)
{
    do {
        *dst++ = pal[*buf++];
    } while (--width);
}

/**
 * Decode one complete byterun1 encoded line.
 *
 * @param dst the destination buffer where to store decompressed bitstream
 * @param dst_size the destination plane size in bytes
 * @param buf the source byterun1 compressed bitstream
 * @param buf_end the EOF of source byterun1 compressed bitstream
 * @return number of consumed bytes in byterun1 compressed bitstream
*/
static int decode_byterun(uint8_t *dst, int dst_size,
                          const uint8_t *buf, const uint8_t *const buf_end) {
    const uint8_t *const buf_start = buf;
    unsigned x;
    for (x = 0; x < dst_size && buf < buf_end;) {
        unsigned length;
        const int8_t value = *buf++;
        if (value >= 0) {
            length = value + 1;
            memcpy(dst + x, buf, FFMIN3(length, dst_size - x, buf_end - buf));
            buf += length;
        } else if (value > -128) {
            length = -value + 1;
            memset(dst + x, *buf++, FFMIN(length, dst_size - x));
        } else { // noop
            continue;
        }
        x += length;
    }
    return buf - buf_start;
}

#define DECODE_RGBX_COMMON(type) \
    if (!length) { \
        length = bytestream2_get_byte(gb); \
        if (!length) { \
            length = bytestream2_get_be16(gb); \
            if (!length) \
                return; \
        } \
    } \
    for (i = 0; i < length; i++) { \
        *(type *)(dst + y*linesize + x * sizeof(type)) = pixel; \
        x += 1; \
        if (x >= width) { \
            y += 1; \
            if (y >= height) \
                return; \
            x = 0; \
        } \
    }

/**
 * Decode RGB8 buffer
 * @param[out] dst Destination buffer
 * @param width Width of destination buffer (pixels)
 * @param height Height of destination buffer (pixels)
 * @param linesize Line size of destination buffer (bytes)
 */
static void decode_rgb8(GetByteContext *gb, uint8_t *dst, int width, int height, int linesize)
{
    int x = 0, y = 0, i, length;
    while (bytestream2_get_bytes_left(gb) >= 4) {
        uint32_t pixel = 0xFF000000 | bytestream2_get_be24(gb);
        length = bytestream2_get_byte(gb) & 0x7F;
        DECODE_RGBX_COMMON(uint32_t)
    }
}

/**
 * Decode RGBN buffer
 * @param[out] dst Destination buffer
 * @param width Width of destination buffer (pixels)
 * @param height Height of destination buffer (pixels)
 * @param linesize Line size of destination buffer (bytes)
 */
static void decode_rgbn(GetByteContext *gb, uint8_t *dst, int width, int height, int linesize)
{
    int x = 0, y = 0, i, length;
    while (bytestream2_get_bytes_left(gb) >= 2) {
        uint32_t pixel = bytestream2_get_be16u(gb);
        length = pixel & 0x7;
        pixel >>= 4;
        DECODE_RGBX_COMMON(uint16_t)
    }
}

/**
 * Decode DEEP RLE 32-bit buffer
 * @param[out] dst Destination buffer
 * @param[in] src Source buffer
 * @param src_size Source buffer size (bytes)
 * @param width Width of destination buffer (pixels)
 * @param height Height of destination buffer (pixels)
 * @param linesize Line size of destination buffer (bytes)
 */
static void decode_deep_rle32(uint8_t *dst, const uint8_t *src, int src_size, int width, int height, int linesize)
{
    const uint8_t *src_end = src + src_size;
    int x = 0, y = 0, i;
    while (src + 5 <= src_end) {
        int opcode;
        opcode = *(int8_t *)src++;
        if (opcode >= 0) {
            int size = opcode + 1;
            for (i = 0; i < size; i++) {
                int length = FFMIN(size - i, width);
                memcpy(dst + y*linesize + x * 4, src, length * 4);
                src += length * 4;
                x += length;
                i += length;
                if (x >= width) {
                    x = 0;
                    y += 1;
                    if (y >= height)
                        return;
                }
            }
        } else {
            int size = -opcode + 1;
            uint32_t pixel = AV_RN32(src);
            for (i = 0; i < size; i++) {
                *(uint32_t *)(dst + y*linesize + x * 4) = pixel;
                x += 1;
                if (x >= width) {
                    x = 0;
                    y += 1;
                    if (y >= height)
                        return;
                }
            }
            src += 4;
        }
    }
}

/**
 * Decode DEEP TVDC 32-bit buffer
 * @param[out] dst Destination buffer
 * @param[in] src Source buffer
 * @param src_size Source buffer size (bytes)
 * @param width Width of destination buffer (pixels)
 * @param height Height of destination buffer (pixels)
 * @param linesize Line size of destination buffer (bytes)
 * @param[int] tvdc TVDC lookup table
 */
static void decode_deep_tvdc32(uint8_t *dst, const uint8_t *src, int src_size, int width, int height, int linesize, const int16_t *tvdc)
{
    int x = 0, y = 0, plane = 0;
    int8_t pixel = 0;
    int i, j;

    for (i = 0; i < src_size * 2;) {
#define GETNIBBLE ((i & 1) ?  (src[i>>1] & 0xF) : (src[i>>1] >> 4))
        int d = tvdc[GETNIBBLE];
        i++;
        if (d) {
            pixel += d;
            dst[y * linesize + x*4 + plane] = pixel;
            x++;
        } else {
            if (i >= src_size * 2)
                return;
            d = GETNIBBLE + 1;
            i++;
            d = FFMIN(d, width - x);
            for (j = 0; j < d; j++) {
                dst[y * linesize + x*4 + plane] = pixel;
                x++;
            }
        }
        if (x >= width) {
            plane++;
            if (plane >= 4) {
                y++;
                if (y >= height)
                    return;
                plane = 0;
            }
            x = 0;
            pixel = 0;
            i = (i + 1) & ~1;
        }
    }
}

static int unsupported(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    avpriv_request_sample(avctx, "bitmap (compression %i, bpp %i, ham %i)", s->compression, s->bpp, s->ham);
    return AVERROR_INVALIDDATA;
}

static int decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    IffContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->size >= 2 ? avpkt->data + AV_RB16(avpkt->data) : NULL;
    const int buf_size = avpkt->size >= 2 ? avpkt->size - AV_RB16(avpkt->data) : 0;
    const uint8_t *buf_end = buf+buf_size;
    int y, plane, res;
    GetByteContext gb;

    if ((res = extract_header(avctx, avpkt)) < 0)
        return res;
    if ((res = ff_reget_buffer(avctx, s->frame)) < 0)
        return res;
    if (!s->init && avctx->bits_per_coded_sample <= 8 &&
        avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        if ((res = cmap_read_palette(avctx, (uint32_t*)s->frame->data[1])) < 0)
            return res;
    } else if (!s->init && avctx->bits_per_coded_sample <= 8 &&
               avctx->pix_fmt == AV_PIX_FMT_RGB32) {
        if ((res = cmap_read_palette(avctx, s->mask_palbuf)) < 0)
            return res;
    }
    s->init = 1;

    switch (s->compression) {
    case 0:
        if (avctx->codec_tag == MKTAG('A','C','B','M')) {
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                memset(s->frame->data[0], 0, avctx->height * s->frame->linesize[0]);
                for (plane = 0; plane < s->bpp; plane++) {
                    for(y = 0; y < avctx->height && buf < buf_end; y++ ) {
                        uint8_t *row = &s->frame->data[0][ y*s->frame->linesize[0] ];
                        decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                }
            } else if (s->ham) { // HAM to AV_PIX_FMT_BGR32
                memset(s->frame->data[0], 0, avctx->height * s->frame->linesize[0]);
                for(y = 0; y < avctx->height; y++) {
                    uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
                    memset(s->ham_buf, 0, s->planesize * 8);
                    for (plane = 0; plane < s->bpp; plane++) {
                        const uint8_t * start = buf + (plane * avctx->height + y) * s->planesize;
                        if (start >= buf_end)
                            break;
                        decodeplane8(s->ham_buf, start, FFMIN(s->planesize, buf_end - start), plane);
                    }
                    decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else
                return unsupported(avctx);
        } else if (avctx->codec_tag == MKTAG('D','E','E','P')) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
            int raw_width = avctx->width * (av_get_bits_per_pixel(desc) >> 3);
            int x;
            for(y = 0; y < avctx->height && buf < buf_end; y++ ) {
                uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
                memcpy(row, buf, FFMIN(raw_width, buf_end - buf));
                buf += raw_width;
                if (avctx->pix_fmt == AV_PIX_FMT_BGR32) {
                    for(x = 0; x < avctx->width; x++)
                        row[4 * x + 3] = row[4 * x + 3] & 0xF0 | (row[4 * x + 3] >> 4);
                }
            }
        } else if (avctx->codec_tag == MKTAG('I','L','B','M')) { // interleaved
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                for(y = 0; y < avctx->height; y++ ) {
                    uint8_t *row = &s->frame->data[0][ y*s->frame->linesize[0] ];
                    memset(row, 0, avctx->width);
                    for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                        decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                }
            } else if (s->ham) { // HAM to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &s->frame->data[0][ y*s->frame->linesize[0] ];
                    memset(s->ham_buf, 0, s->planesize * 8);
                    for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                        decodeplane8(s->ham_buf, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                    decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else { // AV_PIX_FMT_BGR32
                for(y = 0; y < avctx->height; y++ ) {
                    uint8_t *row = &s->frame->data[0][y*s->frame->linesize[0]];
                    memset(row, 0, avctx->width << 2);
                    for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                        decodeplane32((uint32_t *) row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                }
            }
        } else if (avctx->codec_tag == MKTAG('P','B','M',' ')) { // IFF-PBM
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                for(y = 0; y < avctx->height && buf_end > buf; y++ ) {
                    uint8_t *row = &s->frame->data[0][y * s->frame->linesize[0]];
                    memcpy(row, buf, FFMIN(avctx->width, buf_end - buf));
                    buf += avctx->width + (avctx->width % 2); // padding if odd
                }
            } else if (s->ham) { // IFF-PBM: HAM to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height && buf_end > buf; y++) {
                    uint8_t *row = &s->frame->data[0][ y*s->frame->linesize[0] ];
                    memcpy(s->ham_buf, buf, FFMIN(avctx->width, buf_end - buf));
                    buf += avctx->width + (avctx->width & 1); // padding if odd
                    decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else
                return unsupported(avctx);
        }
        break;
    case 1:
        if (avctx->codec_tag == MKTAG('I','L','B','M')) { //interleaved
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                for(y = 0; y < avctx->height ; y++ ) {
                    uint8_t *row = &s->frame->data[0][ y*s->frame->linesize[0] ];
                    memset(row, 0, avctx->width);
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                        decodeplane8(row, s->planebuf, s->planesize, plane);
                    }
                }
            } else if (avctx->bits_per_coded_sample <= 8) { //8-bit (+ mask) to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height ; y++ ) {
                    uint8_t *row = &s->frame->data[0][y*s->frame->linesize[0]];
                    memset(s->mask_buf, 0, avctx->width * sizeof(uint32_t));
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                        decodeplane32(s->mask_buf, s->planebuf, s->planesize, plane);
                    }
                    lookup_pal_indicies((uint32_t *) row, s->mask_buf, s->mask_palbuf, avctx->width);
                }
            } else if (s->ham) { // HAM to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height ; y++) {
                    uint8_t *row = &s->frame->data[0][y*s->frame->linesize[0]];
                    memset(s->ham_buf, 0, s->planesize * 8);
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                        decodeplane8(s->ham_buf, s->planebuf, s->planesize, plane);
                    }
                    decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else { //AV_PIX_FMT_BGR32
                for(y = 0; y < avctx->height ; y++ ) {
                    uint8_t *row = &s->frame->data[0][y*s->frame->linesize[0]];
                    memset(row, 0, avctx->width << 2);
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                        decodeplane32((uint32_t *) row, s->planebuf, s->planesize, plane);
                    }
                }
            }
        } else if (avctx->codec_tag == MKTAG('P','B','M',' ')) { // IFF-PBM
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                for(y = 0; y < avctx->height ; y++ ) {
                    uint8_t *row = &s->frame->data[0][y*s->frame->linesize[0]];
                    buf += decode_byterun(row, avctx->width, buf, buf_end);
                }
            } else if (s->ham) { // IFF-PBM: HAM to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height ; y++) {
                    uint8_t *row = &s->frame->data[0][y*s->frame->linesize[0]];
                    buf += decode_byterun(s->ham_buf, avctx->width, buf, buf_end);
                    decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else
                return unsupported(avctx);
        } else if (avctx->codec_tag == MKTAG('D','E','E','P')) { // IFF-DEEP
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
            if (av_get_bits_per_pixel(desc) == 32)
                decode_deep_rle32(s->frame->data[0], buf, buf_size, avctx->width, avctx->height, s->frame->linesize[0]);
            else
                return unsupported(avctx);
        }
        break;
    case 4:
        bytestream2_init(&gb, buf, buf_size);
        if (avctx->codec_tag == MKTAG('R','G','B','8'))
            decode_rgb8(&gb, s->frame->data[0], avctx->width, avctx->height, s->frame->linesize[0]);
        else if (avctx->codec_tag == MKTAG('R','G','B','N'))
            decode_rgbn(&gb, s->frame->data[0], avctx->width, avctx->height, s->frame->linesize[0]);
        else
            return unsupported(avctx);
        break;
    case 5:
        if (avctx->codec_tag == MKTAG('D','E','E','P')) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
            if (av_get_bits_per_pixel(desc) == 32)
                decode_deep_tvdc32(s->frame->data[0], buf, buf_size, avctx->width, avctx->height, s->frame->linesize[0], s->tvdc);
            else
                return unsupported(avctx);
        } else
            return unsupported(avctx);
        break;
    default:
        return unsupported(avctx);
    }

    if ((res = av_frame_ref(data, s->frame)) < 0)
        return res;

    *got_frame = 1;

    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    av_frame_free(&s->frame);
    av_freep(&s->planebuf);
    av_freep(&s->ham_buf);
    av_freep(&s->ham_palbuf);
    return 0;
}

#if CONFIG_IFF_ILBM_DECODER
AVCodec ff_iff_ilbm_decoder = {
    .name           = "iff",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_IFF_ILBM,
    .priv_data_size = sizeof(IffContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("IFF"),
};
#endif
#if CONFIG_IFF_BYTERUN1_DECODER
AVCodec ff_iff_byterun1_decoder = {
    .name           = "iff",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_IFF_BYTERUN1,
    .priv_data_size = sizeof(IffContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("IFF"),
};
#endif
