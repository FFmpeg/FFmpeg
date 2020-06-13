/*
 * IFF ACBM/ANIM/DEEP/ILBM/PBM/RGB8/RGBN bitmap decoder
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
 * Copyright (c) 2010 Sebastian Vater <cdgs.basty@googlemail.com>
 * Copyright (c) 2016 Paul B Mahol
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
 * IFF ACBM/ANIM/DEEP/ILBM/PBM/RGB8/RGBN bitmap decoder
 */

#include <stdint.h>

#include "libavutil/imgutils.h"

#include "bytestream.h"
#include "avcodec.h"
#include "internal.h"
#include "mathops.h"

// TODO: masking bits
typedef enum {
    MASK_NONE,
    MASK_HAS_MASK,
    MASK_HAS_TRANSPARENT_COLOR,
    MASK_LASSO
} mask_type;

typedef struct IffContext {
    AVFrame *frame;
    int planesize;
    uint8_t * planebuf;
    uint8_t * ham_buf;      ///< temporary buffer for planar to chunky conversation
    uint32_t *ham_palbuf;   ///< HAM decode table
    uint32_t *mask_buf;     ///< temporary buffer for palette indices
    uint32_t *mask_palbuf;  ///< masking palette table
    unsigned  compression;  ///< delta compression method used
    unsigned  is_short;     ///< short compression method used
    unsigned  is_interlaced;///< video is interlaced
    unsigned  is_brush;     ///< video is in ANBR format
    unsigned  bpp;          ///< bits per plane to decode (differs from bits_per_coded_sample if HAM)
    unsigned  ham;          ///< 0 if non-HAM or number of hold bits (6 for bpp > 6, 4 otherwise)
    unsigned  flags;        ///< 1 for EHB, 0 is no extra half darkening
    unsigned  transparency; ///< TODO: transparency color index in palette
    unsigned  masking;      ///< TODO: masking method used
    int init; // 1 if buffer and palette data already initialized, 0 otherwise
    int16_t   tvdc[16];     ///< TVDC lookup table
    GetByteContext gb;
    uint8_t *video[2];
    unsigned video_size;
    uint32_t *pal;
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

#define LUT32(plane) {                                    \
              0,           0,           0,           0,   \
              0,           0,           0, 1U << plane,   \
              0,           0, 1U << plane,           0,   \
              0,           0, 1U << plane, 1U << plane,   \
              0, 1U << plane,           0,           0,   \
              0, 1U << plane,           0, 1U << plane,   \
              0, 1U << plane, 1U << plane,           0,   \
              0, 1U << plane, 1U << plane, 1U << plane,   \
    1U << plane,           0,           0,           0,   \
    1U << plane,           0,           0, 1U << plane,   \
    1U << plane,           0, 1U << plane,           0,   \
    1U << plane,           0, 1U << plane, 1U << plane,   \
    1U << plane, 1U << plane,           0,           0,   \
    1U << plane, 1U << plane,           0, 1U << plane,   \
    1U << plane, 1U << plane, 1U << plane,           0,   \
    1U << plane, 1U << plane, 1U << plane, 1U << plane,   \
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
        for (i = 0; i < count; i++)
            pal[i] = 0xFF000000 | AV_RB24(palette + i*3);
        if (s->flags && count >= 32) { // EHB
            for (i = 0; i < 32; i++)
                pal[i + 32] = 0xFF000000 | (AV_RB24(palette + i*3) & 0xFEFEFE) >> 1;
            count = FFMAX(count, 64);
        }
    } else { // Create gray-scale color palette for bps < 8
        count = 1 << avctx->bits_per_coded_sample;

        for (i = 0; i < count; i++)
            pal[i] = 0xFF000000 | gray2rgb((i * 255) >> avctx->bits_per_coded_sample);
    }
    if (s->masking == MASK_HAS_MASK) {
        if ((1 << avctx->bits_per_coded_sample) < count) {
            avpriv_request_sample(avctx, "overlapping mask");
            return AVERROR_PATCHWELCOME;
        }
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
 * @return >= 0 in case of success, a negative error code otherwise
 */
static int extract_header(AVCodecContext *const avctx,
                          const AVPacket *const avpkt)
{
    IffContext *s = avctx->priv_data;
    const uint8_t *buf;
    unsigned buf_size = 0;
    int i, palette_size;

    if (avctx->extradata_size < 2) {
        av_log(avctx, AV_LOG_ERROR, "not enough extradata\n");
        return AVERROR_INVALIDDATA;
    }
    palette_size = avctx->extradata_size - AV_RB16(avctx->extradata);

    if (avpkt && avctx->codec_tag == MKTAG('A', 'N', 'I', 'M')) {
        uint32_t chunk_id;
        uint64_t data_size;
        GetByteContext *gb = &s->gb;

        bytestream2_skip(gb, 4);
        while (bytestream2_get_bytes_left(gb) >= 1) {
            chunk_id  = bytestream2_get_le32(gb);
            data_size = bytestream2_get_be32(gb);

            if (chunk_id == MKTAG('B', 'M', 'H', 'D')) {
                bytestream2_skip(gb, data_size + (data_size & 1));
            } else if (chunk_id == MKTAG('A', 'N', 'H', 'D')) {
                unsigned extra;
                if (data_size < 40)
                    return AVERROR_INVALIDDATA;

                s->compression = (bytestream2_get_byte(gb) << 8) | (s->compression & 0xFF);
                bytestream2_skip(gb, 19);
                extra = bytestream2_get_be32(gb);
                s->is_short = !(extra & 1);
                s->is_brush = extra == 2;
                s->is_interlaced = !!(extra & 0x40);
                data_size -= 24;
                bytestream2_skip(gb, data_size + (data_size & 1));
            } else if (chunk_id == MKTAG('D', 'L', 'T', 'A') ||
                       chunk_id == MKTAG('B', 'O', 'D', 'Y')) {
                if (chunk_id == MKTAG('B','O','D','Y'))
                    s->compression &= 0xFF;
                break;
            } else if (chunk_id == MKTAG('C', 'M', 'A', 'P')) {
                int count = data_size / 3;
                uint32_t *pal = s->pal;

                if (count > 256)
                    return AVERROR_INVALIDDATA;
                if (s->ham) {
                    for (i = 0; i < count; i++)
                        pal[i] = 0xFF000000 | bytestream2_get_le24(gb);
                } else {
                    for (i = 0; i < count; i++)
                        pal[i] = 0xFF000000 | bytestream2_get_be24(gb);
                }
                bytestream2_skip(gb, data_size & 1);
            } else {
                bytestream2_skip(gb, data_size + (data_size&1));
            }
        }
    } else if (!avpkt) {
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

        if (s->ham) {
            if (s->bpp > 8) {
                av_log(avctx, AV_LOG_ERROR, "Invalid number of hold bits for HAM: %u\n", s->ham);
                return AVERROR_INVALIDDATA;
            } if (s->ham != (s->bpp > 6 ? 6 : 4)) {
                av_log(avctx, AV_LOG_ERROR, "Invalid number of hold bits for HAM: %u, BPP: %u\n", s->ham, s->bpp);
                return AVERROR_INVALIDDATA;
            }
        }

        if (s->masking == MASK_HAS_MASK) {
            if (s->bpp >= 8 && !s->ham) {
                avctx->pix_fmt = AV_PIX_FMT_RGB32;
                av_freep(&s->mask_buf);
                av_freep(&s->mask_palbuf);
                s->mask_buf = av_malloc((s->planesize * 32) + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!s->mask_buf)
                    return AVERROR(ENOMEM);
                if (s->bpp > 16) {
                    av_log(avctx, AV_LOG_ERROR, "bpp %d too large for palette\n", s->bpp);
                    av_freep(&s->mask_buf);
                    return AVERROR(ENOMEM);
                }
                s->mask_palbuf = av_malloc((2 << s->bpp) * sizeof(uint32_t) + AV_INPUT_BUFFER_PADDING_SIZE);
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
        }
        if (s->video_size && s->planesize * s->bpp * avctx->height > s->video_size)
            return AVERROR_INVALIDDATA;

        av_freep(&s->ham_buf);
        av_freep(&s->ham_palbuf);

        if (s->ham) {
            int i, count = FFMIN(palette_size / 3, 1 << s->ham);
            int ham_count;
            const uint8_t *const palette = avctx->extradata + AV_RB16(avctx->extradata);
            int extra_space = 1;

            if (avctx->codec_tag == MKTAG('P', 'B', 'M', ' ') && s->ham == 4)
                extra_space = 4;

            s->ham_buf = av_malloc((s->planesize * 8) + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!s->ham_buf)
                return AVERROR(ENOMEM);

            ham_count = 8 * (1 << s->ham);
            s->ham_palbuf = av_malloc(extra_space * (ham_count << !!(s->masking == MASK_HAS_MASK)) * sizeof (uint32_t) + AV_INPUT_BUFFER_PADDING_SIZE);
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

static av_cold int decode_end(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    av_freep(&s->planebuf);
    av_freep(&s->ham_buf);
    av_freep(&s->ham_palbuf);
    av_freep(&s->mask_buf);
    av_freep(&s->mask_palbuf);
    av_freep(&s->video[0]);
    av_freep(&s->video[1]);
    av_freep(&s->pal);
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
        if (avctx->codec_tag == MKTAG('R', 'G', 'B', '8')) {
            avctx->pix_fmt = AV_PIX_FMT_RGB32;
        } else if (avctx->codec_tag == MKTAG('R', 'G', 'B', 'N')) {
            avctx->pix_fmt = AV_PIX_FMT_RGB444;
        } else if (avctx->codec_tag != MKTAG('D', 'E', 'E', 'P')) {
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
    s->planebuf  = av_malloc(s->planesize * avctx->height + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!s->planebuf)
        return AVERROR(ENOMEM);

    s->bpp = avctx->bits_per_coded_sample;

    if (avctx->codec_tag == MKTAG('A', 'N', 'I', 'M')) {
        s->video_size = FFALIGN(avctx->width, 2) * avctx->height * s->bpp;
        if (!s->video_size)
            return AVERROR_INVALIDDATA;
        s->video[0] = av_calloc(FFALIGN(avctx->width, 2) * avctx->height, s->bpp);
        s->video[1] = av_calloc(FFALIGN(avctx->width, 2) * avctx->height, s->bpp);
        s->pal = av_calloc(256, sizeof(*s->pal));
        if (!s->video[0] || !s->video[1] || !s->pal)
            return AVERROR(ENOMEM);
    }

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
    const uint64_t *lut;
    if (plane >= 8) {
        av_log(NULL, AV_LOG_WARNING, "Ignoring extra planes beyond 8\n");
        return;
    }
    lut = plane8_lut[plane];
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
        mask    = (*buf++ << 2) & 0x3F;
        dst[4] |= lut[mask++];
        dst[5] |= lut[mask++];
        dst[6] |= lut[mask++];
        dst[7] |= lut[mask];
        dst    += 8;
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
                          GetByteContext *gb)
{
    unsigned x;
    for (x = 0; x < dst_size && bytestream2_get_bytes_left(gb) > 0;) {
        unsigned length;
        const int8_t value = bytestream2_get_byte(gb);
        if (value >= 0) {
            length = FFMIN3(value + 1, dst_size - x, bytestream2_get_bytes_left(gb));
            bytestream2_get_buffer(gb, dst + x, length);
            if (length < value + 1)
                bytestream2_skip(gb, value + 1 - length);
        } else if (value > -128) {
            length = FFMIN(-value + 1, dst_size - x);
            memset(dst + x, bytestream2_get_byte(gb), length);
        } else { // noop
            continue;
        }
        x += length;
    }
    if (x < dst_size) {
        av_log(NULL, AV_LOG_WARNING, "decode_byterun ended before plane size\n");
        memset(dst+x, 0, dst_size - x);
    }
    return bytestream2_tell(gb);
}

static int decode_byterun2(uint8_t *dst, int height, int line_size,
                           GetByteContext *gb)
{
    GetByteContext cmds;
    unsigned count;
    int i, y_pos = 0, x_pos = 0;

    if (bytestream2_get_be32(gb) != MKBETAG('V', 'D', 'A', 'T'))
        return 0;

    bytestream2_skip(gb, 4);
    count = bytestream2_get_be16(gb) - 2;
    if (bytestream2_get_bytes_left(gb) < count)
        return 0;

    bytestream2_init(&cmds, gb->buffer, count);
    bytestream2_skip(gb, count);

    for (i = 0; i < count && x_pos < line_size; i++) {
        int8_t cmd = bytestream2_get_byte(&cmds);
        int l, r;

        if (cmd == 0) {
            l = bytestream2_get_be16(gb);
            while (l-- > 0 && x_pos < line_size) {
                dst[x_pos + y_pos   * line_size    ] = bytestream2_get_byte(gb);
                dst[x_pos + y_pos++ * line_size + 1] = bytestream2_get_byte(gb);
                if (y_pos >= height) {
                    y_pos  = 0;
                    x_pos += 2;
                }
            }
        } else if (cmd < 0) {
            l = -cmd;
            while (l-- > 0 && x_pos < line_size) {
                dst[x_pos + y_pos   * line_size    ] = bytestream2_get_byte(gb);
                dst[x_pos + y_pos++ * line_size + 1] = bytestream2_get_byte(gb);
                if (y_pos >= height) {
                    y_pos  = 0;
                    x_pos += 2;
                }
            }
        } else if (cmd == 1) {
            l = bytestream2_get_be16(gb);
            r = bytestream2_get_be16(gb);
            while (l-- > 0 && x_pos < line_size) {
                dst[x_pos + y_pos   * line_size    ] = r >> 8;
                dst[x_pos + y_pos++ * line_size + 1] = r & 0xFF;
                if (y_pos >= height) {
                    y_pos  = 0;
                    x_pos += 2;
                }
            }
        } else {
            l = cmd;
            r = bytestream2_get_be16(gb);
            while (l-- > 0 && x_pos < line_size) {
                dst[x_pos + y_pos   * line_size    ] = r >> 8;
                dst[x_pos + y_pos++ * line_size + 1] = r & 0xFF;
                if (y_pos >= height) {
                    y_pos  = 0;
                    x_pos += 2;
                }
            }
        }
    }

    return bytestream2_tell(gb);
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
    while (src_end - src >= 5) {
        int opcode;
        opcode = *(int8_t *)src++;
        if (opcode >= 0) {
            int size = opcode + 1;
            for (i = 0; i < size; i++) {
                int length = FFMIN(size - i, width - x);
                if (src_end - src < length * 4)
                    return;
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

static void decode_short_horizontal_delta(uint8_t *dst,
                                          const uint8_t *buf, const uint8_t *buf_end,
                                          int w, int bpp, int dst_size)
{
    int planepitch = FFALIGN(w, 16) >> 3;
    int pitch = planepitch * bpp;
    GetByteContext ptrs, gb;
    PutByteContext pb;
    unsigned ofssrc, pos;
    int i, k;

    bytestream2_init(&ptrs, buf, buf_end - buf);
    bytestream2_init_writer(&pb, dst, dst_size);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);
        pos = 0;

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            continue;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));
        while (bytestream2_peek_be16(&gb) != 0xFFFF && bytestream2_get_bytes_left(&gb) > 3) {
            int16_t offset = bytestream2_get_be16(&gb);
            unsigned noffset;

            if (offset >= 0) {
                unsigned data = bytestream2_get_be16(&gb);

                pos += offset * 2;
                noffset = (pos / planepitch) * pitch + (pos % planepitch) + k * planepitch;
                bytestream2_seek_p(&pb, noffset, SEEK_SET);
                bytestream2_put_be16(&pb, data);
            } else {
                uint16_t count = bytestream2_get_be16(&gb);

                pos += 2 * -(offset + 2);
                for (i = 0; i < count; i++) {
                    uint16_t data = bytestream2_get_be16(&gb);

                    pos += 2;
                    noffset = (pos / planepitch) * pitch + (pos % planepitch) + k * planepitch;
                    bytestream2_seek_p(&pb, noffset, SEEK_SET);
                    bytestream2_put_be16(&pb, data);
                }
            }
        }
    }
}

static void decode_byte_vertical_delta(uint8_t *dst,
                                       const uint8_t *buf, const uint8_t *buf_end,
                                       int w, int xor, int bpp, int dst_size)
{
    int ncolumns = ((w + 15) / 16) * 2;
    int dstpitch = ncolumns * bpp;
    unsigned ofsdst, ofssrc, opcode, x;
    GetByteContext ptrs, gb;
    PutByteContext pb;
    int i, j, k;

    bytestream2_init(&ptrs, buf, buf_end - buf);
    bytestream2_init_writer(&pb, dst, dst_size);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            continue;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));
        for (j = 0; j < ncolumns; j++) {
            ofsdst = j + k * ncolumns;

            i = bytestream2_get_byte(&gb);
            while (i > 0) {
                opcode = bytestream2_get_byte(&gb);

                if (opcode == 0) {
                    opcode  = bytestream2_get_byte(&gb);
                    x = bytestream2_get_byte(&gb);

                    while (opcode) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        if (xor && ofsdst < dst_size) {
                            bytestream2_put_byte(&pb, dst[ofsdst] ^ x);
                        } else {
                            bytestream2_put_byte(&pb, x);
                        }
                        ofsdst += dstpitch;
                        opcode--;
                    }
                } else if (opcode < 0x80) {
                    ofsdst += opcode * dstpitch;
                } else {
                    opcode &= 0x7f;

                    while (opcode) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        if (xor && ofsdst < dst_size) {
                            bytestream2_put_byte(&pb, dst[ofsdst] ^ bytestream2_get_byte(&gb));
                        } else {
                            bytestream2_put_byte(&pb, bytestream2_get_byte(&gb));
                        }
                        ofsdst += dstpitch;
                        opcode--;
                    }
                }
                i--;
            }
        }
    }
}

static void decode_delta_j(uint8_t *dst,
                           const uint8_t *buf, const uint8_t *buf_end,
                           int w, int h, int bpp, int dst_size)
{
    int32_t pitch;
    uint8_t *ptr;
    uint32_t type, flag, cols, groups, rows, bytes;
    uint32_t offset;
    int planepitch_byte = (w + 7) / 8;
    int planepitch = ((w + 15) / 16) * 2;
    int kludge_j, b, g, r, d;
    GetByteContext gb;

    pitch = planepitch * bpp;
    kludge_j = w < 320 ? (320 - w) / 8 / 2 : 0;

    bytestream2_init(&gb, buf, buf_end - buf);

    while (bytestream2_get_bytes_left(&gb) >= 2) {
        type = bytestream2_get_be16(&gb);

        switch (type) {
        case 0:
            return;
        case 1:
            flag   = bytestream2_get_be16(&gb);
            cols   = bytestream2_get_be16(&gb);
            groups = bytestream2_get_be16(&gb);

            for (g = 0; g < groups; g++) {
                offset = bytestream2_get_be16(&gb);

                if (cols * bpp == 0 || bytestream2_get_bytes_left(&gb) < cols * bpp) {
                    av_log(NULL, AV_LOG_ERROR, "cols*bpp is invalid (%"PRId32"*%d)", cols, bpp);
                    return;
                }

                if (kludge_j)
                    offset = ((offset / (320 / 8)) * pitch) + (offset % (320 / 8)) - kludge_j;
                else
                    offset = ((offset / planepitch_byte) * pitch) + (offset % planepitch_byte);

                for (b = 0; b < cols; b++) {
                    for (d = 0; d < bpp; d++) {
                        uint8_t value = bytestream2_get_byte(&gb);

                        if (offset >= dst_size)
                            return;
                        ptr = dst + offset;

                        if (flag)
                            ptr[0] ^= value;
                        else
                            ptr[0]  = value;

                        offset += planepitch;
                    }
                }
                if ((cols * bpp) & 1)
                    bytestream2_skip(&gb, 1);
            }
            break;
        case 2:
            flag   = bytestream2_get_be16(&gb);
            rows   = bytestream2_get_be16(&gb);
            bytes  = bytestream2_get_be16(&gb);
            groups = bytestream2_get_be16(&gb);

            for (g = 0; g < groups; g++) {
                offset = bytestream2_get_be16(&gb);

                if (kludge_j)
                    offset = ((offset / (320 / 8)) * pitch) + (offset % (320/ 8)) - kludge_j;
                else
                    offset = ((offset / planepitch_byte) * pitch) + (offset % planepitch_byte);

                for (r = 0; r < rows; r++) {
                    for (d = 0; d < bpp; d++) {
                        unsigned noffset = offset + (r * pitch) + d * planepitch;

                        if (!bytes || bytestream2_get_bytes_left(&gb) < bytes) {
                            av_log(NULL, AV_LOG_ERROR, "bytes %"PRId32" is invalid", bytes);
                            return;
                        }

                        for (b = 0; b < bytes; b++) {
                            uint8_t value = bytestream2_get_byte(&gb);

                            if (noffset >= dst_size)
                                return;
                            ptr = dst + noffset;

                            if (flag)
                                ptr[0] ^= value;
                            else
                                ptr[0]  = value;

                            noffset++;
                        }
                    }
                }
                if ((rows * bytes * bpp) & 1)
                    bytestream2_skip(&gb, 1);
            }
            break;
        default:
            return;
        }
    }
}

static void decode_short_vertical_delta(uint8_t *dst,
                                        const uint8_t *buf, const uint8_t *buf_end,
                                        int w, int bpp, int dst_size)
{
    int ncolumns = (w + 15) >> 4;
    int dstpitch = ncolumns * bpp * 2;
    unsigned ofsdst, ofssrc, ofsdata, opcode, x;
    GetByteContext ptrs, gb, dptrs, dgb;
    PutByteContext pb;
    int i, j, k;

    if (buf_end - buf <= 64)
        return;

    bytestream2_init(&ptrs, buf, buf_end - buf);
    bytestream2_init(&dptrs, buf + 32, (buf_end - buf) - 32);
    bytestream2_init_writer(&pb, dst, dst_size);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);
        ofsdata = bytestream2_get_be32(&dptrs);

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            return;

        if (ofsdata >= buf_end - buf)
            return;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));
        bytestream2_init(&dgb, buf + ofsdata, buf_end - (buf + ofsdata));
        for (j = 0; j < ncolumns; j++) {
            ofsdst = (j + k * ncolumns) * 2;

            i = bytestream2_get_byte(&gb);
            while (i > 0) {
                opcode = bytestream2_get_byte(&gb);

                if (opcode == 0) {
                    opcode = bytestream2_get_byte(&gb);
                    x = bytestream2_get_be16(&dgb);

                    while (opcode) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        bytestream2_put_be16(&pb, x);
                        ofsdst += dstpitch;
                        opcode--;
                    }
                } else if (opcode < 0x80) {
                    ofsdst += opcode * dstpitch;
                } else {
                    opcode &= 0x7f;

                    while (opcode) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        bytestream2_put_be16(&pb, bytestream2_get_be16(&dgb));
                        ofsdst += dstpitch;
                        opcode--;
                    }
                }
                i--;
            }
        }
    }
}

static void decode_long_vertical_delta(uint8_t *dst,
                                       const uint8_t *buf, const uint8_t *buf_end,
                                       int w, int bpp, int dst_size)
{
    int ncolumns = (w + 31) >> 5;
    int dstpitch = ((w + 15) / 16 * 2) * bpp;
    unsigned ofsdst, ofssrc, ofsdata, opcode, x;
    GetByteContext ptrs, gb, dptrs, dgb;
    PutByteContext pb;
    int i, j, k, h;

    if (buf_end - buf <= 64)
        return;

    h = (((w + 15) / 16 * 2) != ((w + 31) / 32 * 4)) ? 1 : 0;
    bytestream2_init(&ptrs, buf, buf_end - buf);
    bytestream2_init(&dptrs, buf + 32, (buf_end - buf) - 32);
    bytestream2_init_writer(&pb, dst, dst_size);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);
        ofsdata = bytestream2_get_be32(&dptrs);

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            return;

        if (ofsdata >= buf_end - buf)
            return;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));
        bytestream2_init(&dgb, buf + ofsdata, buf_end - (buf + ofsdata));
        for (j = 0; j < ncolumns; j++) {
            ofsdst = (j + k * ncolumns) * 4 - h * (2 * k);

            i = bytestream2_get_byte(&gb);
            while (i > 0) {
                opcode = bytestream2_get_byte(&gb);

                if (opcode == 0) {
                    opcode = bytestream2_get_byte(&gb);
                    if (h && (j == (ncolumns - 1))) {
                        x = bytestream2_get_be16(&dgb);
                        bytestream2_skip(&dgb, 2);
                    } else {
                        x = bytestream2_get_be32(&dgb);
                    }

                    if (ofsdst + (opcode - 1LL) * dstpitch > bytestream2_size_p(&pb))
                        return;

                    while (opcode) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        if (h && (j == (ncolumns - 1))) {
                            bytestream2_put_be16(&pb, x);
                        } else {
                            bytestream2_put_be32(&pb, x);
                        }
                        ofsdst += dstpitch;
                        opcode--;
                    }
                } else if (opcode < 0x80) {
                    ofsdst += opcode * dstpitch;
                } else {
                    opcode &= 0x7f;

                    while (opcode) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        if (h && (j == (ncolumns - 1))) {
                            bytestream2_put_be16(&pb, bytestream2_get_be16(&dgb));
                            bytestream2_skip(&dgb, 2);
                        } else {
                            bytestream2_put_be32(&pb, bytestream2_get_be32(&dgb));
                        }
                        ofsdst += dstpitch;
                        opcode--;
                    }
                }
                i--;
            }
        }
    }
}

static void decode_short_vertical_delta2(uint8_t *dst,
                                         const uint8_t *buf, const uint8_t *buf_end,
                                         int w, int bpp, int dst_size)
{
    int ncolumns = (w + 15) >> 4;
    int dstpitch = ncolumns * bpp * 2;
    unsigned ofsdst, ofssrc, opcode, x;
    GetByteContext ptrs, gb;
    PutByteContext pb;
    int i, j, k;

    bytestream2_init(&ptrs, buf, buf_end - buf);
    bytestream2_init_writer(&pb, dst, dst_size);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            continue;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));
        for (j = 0; j < ncolumns; j++) {
            ofsdst = (j + k * ncolumns) * 2;

            i = bytestream2_get_be16(&gb);
            while (i > 0 && bytestream2_get_bytes_left(&gb) > 4) {
                opcode = bytestream2_get_be16(&gb);

                if (opcode == 0) {
                    opcode = bytestream2_get_be16(&gb);
                    x = bytestream2_get_be16(&gb);

                    while (opcode && bytestream2_get_bytes_left_p(&pb) > 1) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        bytestream2_put_be16(&pb, x);
                        ofsdst += dstpitch;
                        opcode--;
                    }
                } else if (opcode < 0x8000) {
                    ofsdst += opcode * dstpitch;
                } else {
                    opcode &= 0x7fff;

                    while (opcode && bytestream2_get_bytes_left(&gb) > 1 &&
                           bytestream2_get_bytes_left_p(&pb) > 1) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        bytestream2_put_be16(&pb, bytestream2_get_be16(&gb));
                        ofsdst += dstpitch;
                        opcode--;
                    }
                }
                i--;
            }
        }
    }
}

static void decode_long_vertical_delta2(uint8_t *dst,
                                        const uint8_t *buf, const uint8_t *buf_end,
                                        int w, int bpp, int dst_size)
{
    int ncolumns = (w + 31) >> 5;
    int dstpitch = ((w + 15) / 16 * 2) * bpp;
    unsigned ofsdst, ofssrc, opcode, x;
    unsigned skip = 0x80000000, mask = skip - 1;
    GetByteContext ptrs, gb;
    PutByteContext pb;
    int i, j, k, h;

    h = (((w + 15) / 16 * 2) != ((w + 31) / 32 * 4)) ? 1 : 0;
    bytestream2_init(&ptrs, buf, buf_end - buf);
    bytestream2_init_writer(&pb, dst, dst_size);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            continue;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));
        for (j = 0; j < ncolumns; j++) {
            ofsdst = (j + k * ncolumns) * 4 - h * (2 * k);

            if (h && (j == (ncolumns - 1))) {
                skip = 0x8000;
                mask = skip - 1;
            }

            i = bytestream2_get_be32(&gb);
            while (i > 0 && bytestream2_get_bytes_left(&gb) > 4) {
                opcode = bytestream2_get_be32(&gb);

                if (opcode == 0) {
                    if (h && (j == ncolumns - 1)) {
                        opcode = bytestream2_get_be16(&gb);
                        x = bytestream2_get_be16(&gb);
                    } else {
                        opcode = bytestream2_get_be32(&gb);
                        x = bytestream2_get_be32(&gb);
                    }

                    if (ofsdst + (opcode - 1LL) * dstpitch > bytestream2_size_p(&pb))
                        return;

                    while (opcode && bytestream2_get_bytes_left_p(&pb) > 1) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        if (h && (j == ncolumns - 1))
                            bytestream2_put_be16(&pb, x);
                        else
                            bytestream2_put_be32(&pb, x);
                        ofsdst += dstpitch;
                        opcode--;
                    }
                } else if (opcode < skip) {
                    ofsdst += opcode * dstpitch;
                } else {
                    opcode &= mask;

                    while (opcode && bytestream2_get_bytes_left(&gb) > 1 &&
                           bytestream2_get_bytes_left_p(&pb) > 1) {
                        bytestream2_seek_p(&pb, ofsdst, SEEK_SET);
                        if (h && (j == ncolumns - 1)) {
                            bytestream2_put_be16(&pb, bytestream2_get_be16(&gb));
                        } else {
                            bytestream2_put_be32(&pb, bytestream2_get_be32(&gb));
                        }
                        ofsdst += dstpitch;
                        opcode--;
                    }
                }
                i--;
            }
        }
    }
}

static void decode_delta_d(uint8_t *dst,
                           const uint8_t *buf, const uint8_t *buf_end,
                           int w, int flag, int bpp, int dst_size)
{
    int planepitch = FFALIGN(w, 16) >> 3;
    int pitch = planepitch * bpp;
    int planepitch_byte = (w + 7) / 8;
    unsigned entries, ofssrc;
    GetByteContext gb, ptrs;
    PutByteContext pb;
    int k;

    if (buf_end - buf <= 4 * bpp)
        return;

    bytestream2_init_writer(&pb, dst, dst_size);
    bytestream2_init(&ptrs, buf, bpp * 4);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            continue;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));

        entries = bytestream2_get_be32(&gb);
        while (entries && bytestream2_get_bytes_left(&gb) >= 8) {
            int32_t opcode  = bytestream2_get_be32(&gb);
            unsigned offset = bytestream2_get_be32(&gb);

            bytestream2_seek_p(&pb, (offset / planepitch_byte) * pitch + (offset % planepitch_byte) + k * planepitch, SEEK_SET);
            if (opcode >= 0) {
                uint32_t x = bytestream2_get_be32(&gb);
                if (opcode && 4 + (opcode - 1LL) * pitch > bytestream2_get_bytes_left_p(&pb))
                    continue;
                while (opcode && bytestream2_get_bytes_left_p(&pb) > 0) {
                    bytestream2_put_be32(&pb, x);
                    bytestream2_skip_p(&pb, pitch - 4);
                    opcode--;
                }
            } else {
                while (opcode && bytestream2_get_bytes_left(&gb) > 0) {
                    bytestream2_put_be32(&pb, bytestream2_get_be32(&gb));
                    bytestream2_skip_p(&pb, pitch - 4);
                    opcode++;
                }
            }
            entries--;
        }
    }
}

static void decode_delta_e(uint8_t *dst,
                           const uint8_t *buf, const uint8_t *buf_end,
                           int w, int flag, int bpp, int dst_size)
{
    int planepitch = FFALIGN(w, 16) >> 3;
    int pitch = planepitch * bpp;
    int planepitch_byte = (w + 7) / 8;
    unsigned entries, ofssrc;
    GetByteContext gb, ptrs;
    PutByteContext pb;
    int k;

    if (buf_end - buf <= 4 * bpp)
        return;

    bytestream2_init_writer(&pb, dst, dst_size);
    bytestream2_init(&ptrs, buf, bpp * 4);

    for (k = 0; k < bpp; k++) {
        ofssrc = bytestream2_get_be32(&ptrs);

        if (!ofssrc)
            continue;

        if (ofssrc >= buf_end - buf)
            continue;

        bytestream2_init(&gb, buf + ofssrc, buf_end - (buf + ofssrc));

        entries = bytestream2_get_be16(&gb);
        while (entries && bytestream2_get_bytes_left(&gb) >= 6) {
            int16_t opcode  = bytestream2_get_be16(&gb);
            unsigned offset = bytestream2_get_be32(&gb);

            bytestream2_seek_p(&pb, (offset / planepitch_byte) * pitch + (offset % planepitch_byte) + k * planepitch, SEEK_SET);
            if (opcode >= 0) {
                uint16_t x = bytestream2_get_be16(&gb);
                while (opcode && bytestream2_get_bytes_left_p(&pb) > 0) {
                    bytestream2_put_be16(&pb, x);
                    bytestream2_skip_p(&pb, pitch - 2);
                    opcode--;
                }
            } else {
                opcode = -opcode;
                while (opcode && bytestream2_get_bytes_left(&gb) > 0) {
                    bytestream2_put_be16(&pb, bytestream2_get_be16(&gb));
                    bytestream2_skip_p(&pb, pitch - 2);
                    opcode--;
                }
            }
            entries--;
        }
    }
}

static void decode_delta_l(uint8_t *dst,
                           const uint8_t *buf, const uint8_t *buf_end,
                           int w, int flag, int bpp, int dst_size)
{
    GetByteContext off0, off1, dgb, ogb;
    PutByteContext pb;
    unsigned poff0, poff1;
    int i, k, dstpitch;
    int planepitch_byte = (w + 7) / 8;
    int planepitch = ((w + 15) / 16) * 2;
    int pitch = planepitch * bpp;

    if (buf_end - buf <= 64)
        return;

    bytestream2_init(&off0, buf, buf_end - buf);
    bytestream2_init(&off1, buf + 32, buf_end - (buf + 32));
    bytestream2_init_writer(&pb, dst, dst_size);

    dstpitch = flag ? (((w + 7) / 8) * bpp): 2;

    for (k = 0; k < bpp; k++) {
        poff0 = bytestream2_get_be32(&off0);
        poff1 = bytestream2_get_be32(&off1);

        if (!poff0)
            continue;

        if (2LL * poff0 >= buf_end - buf)
            return;

        if (2LL * poff1 >= buf_end - buf)
            return;

        bytestream2_init(&dgb, buf + 2 * poff0, buf_end - (buf + 2 * poff0));
        bytestream2_init(&ogb, buf + 2 * poff1, buf_end - (buf + 2 * poff1));

        while (bytestream2_peek_be16(&ogb) != 0xFFFF && bytestream2_get_bytes_left(&ogb) >= 4) {
            uint32_t offset = bytestream2_get_be16(&ogb);
            int16_t cnt = bytestream2_get_be16(&ogb);
            uint16_t data;

            offset = ((2 * offset) / planepitch_byte) * pitch + ((2 * offset) % planepitch_byte) + k * planepitch;
            if (cnt < 0) {
                if (bytestream2_get_bytes_left(&dgb) < 2)
                    break;
                bytestream2_seek_p(&pb, offset, SEEK_SET);
                cnt = -cnt;
                data = bytestream2_get_be16(&dgb);
                for (i = 0; i < cnt; i++) {
                    bytestream2_put_be16(&pb, data);
                    bytestream2_skip_p(&pb, dstpitch - 2);
                }
            } else {
                if (bytestream2_get_bytes_left(&dgb) < 2*cnt)
                    break;
                bytestream2_seek_p(&pb, offset, SEEK_SET);
                for (i = 0; i < cnt; i++) {
                    data = bytestream2_get_be16(&dgb);
                    bytestream2_put_be16(&pb, data);
                    bytestream2_skip_p(&pb, dstpitch - 2);
                }
            }
        }
    }
}

static int unsupported(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    avpriv_request_sample(avctx, "bitmap (compression 0x%0x, bpp %i, ham %i, interlaced %i)", s->compression, s->bpp, s->ham, s->is_interlaced);
    return AVERROR_INVALIDDATA;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    IffContext *s          = avctx->priv_data;
    AVFrame *frame         = data;
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    const uint8_t *buf_end = buf + buf_size;
    int y, plane, res;
    GetByteContext *gb = &s->gb;
    const AVPixFmtDescriptor *desc;

    bytestream2_init(gb, avpkt->data, avpkt->size);

    if ((res = extract_header(avctx, avpkt)) < 0)
        return res;

    if ((res = ff_get_buffer(avctx, frame, 0)) < 0)
        return res;
    s->frame = frame;

    buf      += bytestream2_tell(gb);
    buf_size -= bytestream2_tell(gb);
    desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    if (!s->init && avctx->bits_per_coded_sample <= 8 - (s->masking == MASK_HAS_MASK) &&
        avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        if ((res = cmap_read_palette(avctx, (uint32_t *)frame->data[1])) < 0)
            return res;
    } else if (!s->init && avctx->bits_per_coded_sample <= 8 &&
               avctx->pix_fmt == AV_PIX_FMT_RGB32) {
        if ((res = cmap_read_palette(avctx, s->mask_palbuf)) < 0)
            return res;
    }
    s->init = 1;

    if (s->compression <= 0xff && (avctx->codec_tag == MKTAG('A', 'N', 'I', 'M'))) {
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8)
            memcpy(s->pal, s->frame->data[1], 256 * 4);
    }

    switch (s->compression) {
    case 0x0:
        if (avctx->codec_tag == MKTAG('A', 'C', 'B', 'M')) {
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                memset(frame->data[0], 0, avctx->height * frame->linesize[0]);
                for (plane = 0; plane < s->bpp; plane++) {
                    for (y = 0; y < avctx->height && buf < buf_end; y++) {
                        uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                        decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                }
            } else if (s->ham) { // HAM to AV_PIX_FMT_BGR32
                memset(frame->data[0], 0, avctx->height * frame->linesize[0]);
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(s->ham_buf, 0, s->planesize * 8);
                    for (plane = 0; plane < s->bpp; plane++) {
                        const uint8_t * start = buf + (plane * avctx->height + y) * s->planesize;
                        if (start >= buf_end)
                            break;
                        decodeplane8(s->ham_buf, start, FFMIN(s->planesize, buf_end - start), plane);
                    }
                    decode_ham_plane32((uint32_t *)row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else
                return unsupported(avctx);
        } else if (avctx->codec_tag == MKTAG('D', 'E', 'E', 'P')) {
            int raw_width = avctx->width * (av_get_bits_per_pixel(desc) >> 3);
            int x;
            for (y = 0; y < avctx->height && buf < buf_end; y++) {
                uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                memcpy(row, buf, FFMIN(raw_width, buf_end - buf));
                buf += raw_width;
                if (avctx->pix_fmt == AV_PIX_FMT_BGR32) {
                    for (x = 0; x < avctx->width; x++)
                        row[4 * x + 3] = row[4 * x + 3] & 0xF0 | (row[4 * x + 3] >> 4);
                }
            }
        } else if (avctx->codec_tag == MKTAG('I', 'L', 'B', 'M') || // interleaved
                   avctx->codec_tag == MKTAG('A', 'N', 'I', 'M')) {
            if (avctx->codec_tag == MKTAG('A', 'N', 'I', 'M'))
                memcpy(s->video[0], buf, FFMIN(buf_end - buf, s->video_size));
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(row, 0, avctx->width);
                    for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                        decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                }
            } else if (s->ham) { // HAM to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(s->ham_buf, 0, s->planesize * 8);
                    for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                        decodeplane8(s->ham_buf, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                    decode_ham_plane32((uint32_t *)row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else { // AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(row, 0, avctx->width << 2);
                    for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                        decodeplane32((uint32_t *)row, buf,
                                      FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                }
            }
        } else if (avctx->codec_tag == MKTAG('P', 'B', 'M', ' ')) { // IFF-PBM
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                for (y = 0; y < avctx->height && buf_end > buf; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memcpy(row, buf, FFMIN(avctx->width, buf_end - buf));
                    buf += avctx->width + (avctx->width % 2); // padding if odd
                }
            } else if (s->ham) { // IFF-PBM: HAM to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height && buf_end > buf; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memcpy(s->ham_buf, buf, FFMIN(avctx->width, buf_end - buf));
                    buf += avctx->width + (avctx->width & 1); // padding if odd
                    decode_ham_plane32((uint32_t *)row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else
                return unsupported(avctx);
        } else {
            return unsupported(avctx);
        }
        break;
    case 0x1:
        if (avctx->codec_tag == MKTAG('I', 'L', 'B', 'M') || // interleaved
            avctx->codec_tag == MKTAG('A', 'N', 'I', 'M')) {
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                uint8_t *video = s->video[0];

                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(row, 0, avctx->width);
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, gb);
                        if (avctx->codec_tag == MKTAG('A', 'N', 'I', 'M')) {
                            memcpy(video, s->planebuf, s->planesize);
                            video += s->planesize;
                        }
                        decodeplane8(row, s->planebuf, s->planesize, plane);
                    }
                }
            } else if (avctx->bits_per_coded_sample <= 8) { //8-bit (+ mask) to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(s->mask_buf, 0, avctx->width * sizeof(uint32_t));
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, gb);
                        decodeplane32(s->mask_buf, s->planebuf, s->planesize, plane);
                    }
                    lookup_pal_indicies((uint32_t *)row, s->mask_buf, s->mask_palbuf, avctx->width);
                }
            } else if (s->ham) { // HAM to AV_PIX_FMT_BGR32
                uint8_t *video = s->video[0];
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(s->ham_buf, 0, s->planesize * 8);
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, gb);
                        if (avctx->codec_tag == MKTAG('A', 'N', 'I', 'M')) {
                            memcpy(video, s->planebuf, s->planesize);
                            video += s->planesize;
                        }
                        decodeplane8(s->ham_buf, s->planebuf, s->planesize, plane);
                    }
                    decode_ham_plane32((uint32_t *)row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else { // AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(row, 0, avctx->width << 2);
                    for (plane = 0; plane < s->bpp; plane++) {
                        buf += decode_byterun(s->planebuf, s->planesize, gb);
                        decodeplane32((uint32_t *)row, s->planebuf, s->planesize, plane);
                    }
                }
            }
        } else if (avctx->codec_tag == MKTAG('P', 'B', 'M', ' ')) { // IFF-PBM
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    buf += decode_byterun(row, avctx->width, gb);
                }
            } else if (s->ham) { // IFF-PBM: HAM to AV_PIX_FMT_BGR32
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    buf += decode_byterun(s->ham_buf, avctx->width, gb);
                    decode_ham_plane32((uint32_t *)row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else
                return unsupported(avctx);
        } else if (avctx->codec_tag == MKTAG('D', 'E', 'E', 'P')) { // IFF-DEEP
            if (av_get_bits_per_pixel(desc) == 32)
                decode_deep_rle32(frame->data[0], buf, buf_size, avctx->width, avctx->height, frame->linesize[0]);
            else
                return unsupported(avctx);
        } else if (avctx->codec_tag == MKTAG('A', 'C', 'B', 'M')) {
            if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                memset(frame->data[0], 0, avctx->height * frame->linesize[0]);
                for (plane = 0; plane < s->bpp; plane++) {
                    for (y = 0; y < avctx->height && buf < buf_end; y++) {
                        uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                        decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                        buf += s->planesize;
                    }
                }
            } else if (s->ham) { // HAM to AV_PIX_FMT_BGR32
                memset(frame->data[0], 0, avctx->height * frame->linesize[0]);
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    memset(s->ham_buf, 0, s->planesize * 8);
                    for (plane = 0; plane < s->bpp; plane++) {
                        const uint8_t * start = buf + (plane * avctx->height + y) * s->planesize;
                        if (start >= buf_end)
                            break;
                        decodeplane8(s->ham_buf, start, FFMIN(s->planesize, buf_end - start), plane);
                    }
                    decode_ham_plane32((uint32_t *)row, s->ham_buf, s->ham_palbuf, s->planesize);
                }
            } else {
                return unsupported(avctx);
            }
        } else {
            return unsupported(avctx);
        }
        break;
    case 0x2:
        if (avctx->codec_tag == MKTAG('I', 'L', 'B', 'M') && avctx->pix_fmt == AV_PIX_FMT_PAL8) {
            for (plane = 0; plane < s->bpp; plane++) {
                decode_byterun2(s->planebuf, avctx->height, s->planesize, gb);
                for (y = 0; y < avctx->height; y++) {
                    uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                    decodeplane8(row, s->planebuf + s->planesize * y, s->planesize, plane);
                }
            }
        } else {
            return unsupported(avctx);
        }
        break;
    case 0x4:
        if (avctx->codec_tag == MKTAG('R', 'G', 'B', '8') && avctx->pix_fmt == AV_PIX_FMT_RGB32)
            decode_rgb8(gb, frame->data[0], avctx->width, avctx->height, frame->linesize[0]);
        else if (avctx->codec_tag == MKTAG('R', 'G', 'B', 'N') && avctx->pix_fmt == AV_PIX_FMT_RGB444)
            decode_rgbn(gb, frame->data[0], avctx->width, avctx->height, frame->linesize[0]);
        else
            return unsupported(avctx);
        break;
    case 0x5:
        if (avctx->codec_tag == MKTAG('D', 'E', 'E', 'P')) {
            if (av_get_bits_per_pixel(desc) == 32)
                decode_deep_tvdc32(frame->data[0], buf, buf_size, avctx->width, avctx->height, frame->linesize[0], s->tvdc);
            else
                return unsupported(avctx);
        } else
            return unsupported(avctx);
        break;
    case 0x300:
    case 0x301:
        decode_short_horizontal_delta(s->video[0], buf, buf_end, avctx->width, s->bpp, s->video_size);
        break;
    case 0x500:
    case 0x501:
        decode_byte_vertical_delta(s->video[0], buf, buf_end, avctx->width, s->is_brush, s->bpp, s->video_size);
        break;
    case 0x700:
    case 0x701:
        if (s->is_short)
            decode_short_vertical_delta(s->video[0], buf, buf_end, avctx->width, s->bpp, s->video_size);
        else
            decode_long_vertical_delta(s->video[0], buf, buf_end, avctx->width, s->bpp, s->video_size);
        break;
    case 0x800:
    case 0x801:
        if (s->is_short)
            decode_short_vertical_delta2(s->video[0], buf, buf_end, avctx->width, s->bpp, s->video_size);
        else
            decode_long_vertical_delta2(s->video[0], buf, buf_end, avctx->width, s->bpp, s->video_size);
        break;
    case 0x4a00:
    case 0x4a01:
        decode_delta_j(s->video[0], buf, buf_end, avctx->width, avctx->height, s->bpp, s->video_size);
        break;
    case 0x6400:
    case 0x6401:
        if (s->is_interlaced)
            return unsupported(avctx);
        decode_delta_d(s->video[0], buf, buf_end, avctx->width, s->is_interlaced, s->bpp, s->video_size);
        break;
    case 0x6500:
    case 0x6501:
        if (s->is_interlaced)
            return unsupported(avctx);
        decode_delta_e(s->video[0], buf, buf_end, avctx->width, s->is_interlaced, s->bpp, s->video_size);
        break;
    case 0x6c00:
    case 0x6c01:
        decode_delta_l(s->video[0], buf, buf_end, avctx->width, s->is_short, s->bpp, s->video_size);
        break;
    default:
        return unsupported(avctx);
    }

    if (s->compression <= 0xff && (avctx->codec_tag == MKTAG('A', 'N', 'I', 'M'))) {
        memcpy(s->video[1], s->video[0], s->video_size);
    }

    if (s->compression > 0xff) {
        if (avctx->pix_fmt == AV_PIX_FMT_PAL8 || avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
            buf = s->video[0];
            for (y = 0; y < avctx->height; y++) {
                uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                memset(row, 0, avctx->width);
                for (plane = 0; plane < s->bpp; plane++) {
                    decodeplane8(row, buf, s->planesize, plane);
                    buf += s->planesize;
                }
            }
            memcpy(frame->data[1], s->pal, 256 * 4);
        } else if (s->ham) {
            int i, count = 1 << s->ham;

            buf = s->video[0];
            memset(s->ham_palbuf, 0, (1 << s->ham) * 2 * sizeof(uint32_t));
            for (i = 0; i < count; i++) {
                s->ham_palbuf[i*2+1] = s->pal[i];
            }
            for (i = 0; i < count; i++) {
                uint32_t tmp = i << (8 - s->ham);
                tmp |= tmp >> s->ham;
                s->ham_palbuf[(i+count)*2]     = 0xFF00FFFF;
                s->ham_palbuf[(i+count*2)*2]   = 0xFFFFFF00;
                s->ham_palbuf[(i+count*3)*2]   = 0xFFFF00FF;
                s->ham_palbuf[(i+count)*2+1]   = 0xFF000000 | tmp << 16;
                s->ham_palbuf[(i+count*2)*2+1] = 0xFF000000 | tmp;
                s->ham_palbuf[(i+count*3)*2+1] = 0xFF000000 | tmp << 8;
            }
            if (s->masking == MASK_HAS_MASK) {
                for (i = 0; i < 8 * (1 << s->ham); i++)
                    s->ham_palbuf[(1 << s->bpp) + i] = s->ham_palbuf[i] | 0xFF000000;
            }
            for (y = 0; y < avctx->height; y++) {
                uint8_t *row = &frame->data[0][y * frame->linesize[0]];
                memset(s->ham_buf, 0, s->planesize * 8);
                for (plane = 0; plane < s->bpp; plane++) {
                    decodeplane8(s->ham_buf, buf, s->planesize, plane);
                    buf += s->planesize;
                }
                decode_ham_plane32((uint32_t *)row, s->ham_buf, s->ham_palbuf, s->planesize);
            }
        } else {
            return unsupported(avctx);
        }

        if (!s->is_brush) {
            FFSWAP(uint8_t *, s->video[0], s->video[1]);
        }
    }

    if (avpkt->flags & AV_PKT_FLAG_KEY) {
        frame->key_frame = 1;
        frame->pict_type = AV_PICTURE_TYPE_I;
    } else {
        frame->key_frame = 0;
        frame->pict_type = AV_PICTURE_TYPE_P;
    }

    *got_frame = 1;

    return buf_size;
}

#if CONFIG_IFF_ILBM_DECODER
AVCodec ff_iff_ilbm_decoder = {
    .name           = "iff",
    .long_name      = NULL_IF_CONFIG_SMALL("IFF ACBM/ANIM/DEEP/ILBM/PBM/RGB8/RGBN"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_IFF_ILBM,
    .priv_data_size = sizeof(IffContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .capabilities   = AV_CODEC_CAP_DR1,
};
#endif
