/*
 * IFF PBM/ILBM bitmap decoder
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
 * IFF PBM/ILBM bitmap decoder
 */

#include "libavutil/imgutils.h"
#include "bytestream.h"
#include "avcodec.h"
#include "get_bits.h"

// TODO: masking bits
typedef enum {
    MASK_NONE,
    MASK_HAS_MASK,
    MASK_HAS_TRANSPARENT_COLOR,
    MASK_LASSO
} mask_type;

typedef struct {
    AVFrame frame;
    int planesize;
    uint8_t * planebuf;
    uint8_t * ham_buf;      ///< temporary buffer for planar to chunky conversation
    uint32_t *ham_palbuf;   ///< HAM decode table
    unsigned  compression;  ///< delta compression method used
    unsigned  bpp;          ///< bits per plane to decode (differs from bits_per_coded_sample if HAM)
    unsigned  ham;          ///< 0 if non-HAM or number of hold bits (6 for bpp > 6, 4 otherwise)
    unsigned  flags;        ///< 1 for EHB, 0 is no extra half darkening
    unsigned  transparency; ///< TODO: transparency color index in palette
    unsigned  masking;      ///< TODO: masking method used
    int init; // 1 if buffer and palette data already initialized, 0 otherwise
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
static int ff_cmap_read_palette(AVCodecContext *avctx, uint32_t *pal)
{
    int count, i;
    const uint8_t *const palette = avctx->extradata + AV_RB16(avctx->extradata);
    int palette_size = avctx->extradata_size - AV_RB16(avctx->extradata);

    if (avctx->bits_per_coded_sample > 8) {
        av_log(avctx, AV_LOG_ERROR, "bit_per_coded_sample > 8 not supported\n");
        return AVERROR_INVALIDDATA;
    }

    count = 1 << avctx->bits_per_coded_sample;
    // If extradata is smaller than actually needed, fill the remaining with black.
    count = FFMIN(palette_size / 3, count);
    if (count) {
        for (i=0; i < count; i++) {
            pal[i] = 0xFF000000 | AV_RB24(palette + i*3);
        }
    } else { // Create gray-scale color palette for bps < 8
        count = 1 << avctx->bits_per_coded_sample;

        for (i=0; i < count; i++) {
            pal[i] = 0xFF000000 | gray2rgb((i * 255) >> avctx->bits_per_coded_sample);
        }
    }
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
    int palette_size = avctx->extradata_size - AV_RB16(avctx->extradata);

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
        if (avctx->extradata_size < 2)
            return AVERROR_INVALIDDATA;
        buf = avctx->extradata;
        buf_size = bytestream_get_be16(&buf);
        if (buf_size <= 1 || palette_size < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid palette size received: %u -> palette data offset: %d\n",
                   buf_size, palette_size);
            return AVERROR_INVALIDDATA;
        }
    }

    if (buf_size > 8) {
        s->compression  = bytestream_get_byte(&buf);
        s->bpp          = bytestream_get_byte(&buf);
        s->ham          = bytestream_get_byte(&buf);
        s->flags        = bytestream_get_byte(&buf);
        s->transparency = bytestream_get_be16(&buf);
        s->masking      = bytestream_get_byte(&buf);
        if (s->masking == MASK_HAS_TRANSPARENT_COLOR) {
            av_log(avctx, AV_LOG_ERROR, "Transparency not supported\n");
            return AVERROR_PATCHWELCOME;
        } else if (s->masking != MASK_NONE) {
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
            const uint8_t *const palette = avctx->extradata + AV_RB16(avctx->extradata);
            s->ham_buf = av_malloc((s->planesize * 8) + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!s->ham_buf)
                return AVERROR(ENOMEM);

            s->ham_palbuf = av_malloc((8 * (1 << s->ham) * sizeof (uint32_t)) + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!s->ham_palbuf) {
                av_freep(&s->ham_buf);
                return AVERROR(ENOMEM);
            }

            if (count) { // HAM with color palette attached
                // prefill with black and palette and set HAM take direct value mask to zero
                memset(s->ham_palbuf, 0, (1 << s->ham) * 2 * sizeof (uint32_t));
                for (i=0; i < count; i++) {
                    s->ham_palbuf[i*2+1] = AV_RL24(palette + i*3);
                }
                count = 1 << s->ham;
            } else { // HAM with grayscale color palette
                count = 1 << s->ham;
                for (i=0; i < count; i++) {
                    s->ham_palbuf[i*2]   = 0; // take direct color value from palette
                    s->ham_palbuf[i*2+1] = av_le2ne32(gray2rgb((i * 255) >> s->ham));
                }
            }
            for (i=0; i < count; i++) {
                uint32_t tmp = i << (8 - s->ham);
                tmp |= tmp >> s->ham;
                s->ham_palbuf[(i+count)*2]     = 0x00FFFF; // just modify blue color component
                s->ham_palbuf[(i+count*2)*2]   = 0xFFFF00; // just modify red color component
                s->ham_palbuf[(i+count*3)*2]   = 0xFF00FF; // just modify green color component
                s->ham_palbuf[(i+count)*2+1]   = tmp << 16;
                s->ham_palbuf[(i+count*2)*2+1] = tmp;
                s->ham_palbuf[(i+count*3)*2+1] = tmp << 8;
            }
        } else if (s->flags & 1) { // EHB (ExtraHalfBrite) color palette
            av_log(avctx, AV_LOG_ERROR, "ExtraHalfBrite (EHB) mode not supported\n");
            return AVERROR_PATCHWELCOME;
        }
    }

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    int err;

    if (avctx->bits_per_coded_sample <= 8) {
        int palette_size = avctx->extradata_size - AV_RB16(avctx->extradata);
        avctx->pix_fmt = (avctx->bits_per_coded_sample < 8) ||
                         (avctx->extradata_size >= 2 && palette_size) ? PIX_FMT_PAL8 : PIX_FMT_GRAY8;
    } else if (avctx->bits_per_coded_sample <= 32) {
        avctx->pix_fmt = PIX_FMT_BGR32;
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
    avcodec_get_frame_defaults(&s->frame);

    if ((err = extract_header(avctx, NULL)) < 0)
        return err;
    s->frame.reference = 1;

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
    uint32_t delta = 0;
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

static int decode_frame_ilbm(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    IffContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->size >= 2 ? avpkt->data + AV_RB16(avpkt->data) : NULL;
    const int buf_size = avpkt->size >= 2 ? avpkt->size - AV_RB16(avpkt->data) : 0;
    const uint8_t *buf_end = buf+buf_size;
    int y, plane, res;

    if ((res = extract_header(avctx, avpkt)) < 0)
        return res;

    if (s->init) {
        if ((res = avctx->reget_buffer(avctx, &s->frame)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return res;
        }
    } else if ((res = avctx->get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return res;
    } else if (avctx->bits_per_coded_sample <= 8 && avctx->pix_fmt != PIX_FMT_GRAY8) {
        if ((res = ff_cmap_read_palette(avctx, (uint32_t*)s->frame.data[1])) < 0)
            return res;
    }
    s->init = 1;

    if (avctx->codec_tag == MKTAG('I','L','B','M')) { // interleaved
        if (avctx->pix_fmt == PIX_FMT_PAL8 || avctx->pix_fmt == PIX_FMT_GRAY8) {
            for(y = 0; y < avctx->height; y++ ) {
                uint8_t *row = &s->frame.data[0][ y*s->frame.linesize[0] ];
                memset(row, 0, avctx->width);
                for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                    decodeplane8(row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                    buf += s->planesize;
                }
            }
        } else if (s->ham) { // HAM to PIX_FMT_BGR32
            for (y = 0; y < avctx->height; y++) {
                uint8_t *row = &s->frame.data[0][ y*s->frame.linesize[0] ];
                memset(s->ham_buf, 0, avctx->width);
                for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                    decodeplane8(s->ham_buf, buf, FFMIN(s->planesize, buf_end - buf), plane);
                    buf += s->planesize;
                }
                decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, s->planesize);
            }
        } else { // PIX_FMT_BGR32
            for(y = 0; y < avctx->height; y++ ) {
                uint8_t *row = &s->frame.data[0][y*s->frame.linesize[0]];
                memset(row, 0, avctx->width << 2);
                for (plane = 0; plane < s->bpp && buf < buf_end; plane++) {
                    decodeplane32((uint32_t *) row, buf, FFMIN(s->planesize, buf_end - buf), plane);
                    buf += s->planesize;
                }
            }
        }
    } else if (avctx->pix_fmt == PIX_FMT_PAL8 || avctx->pix_fmt == PIX_FMT_GRAY8) { // IFF-PBM
        for(y = 0; y < avctx->height; y++ ) {
            uint8_t *row = &s->frame.data[0][y * s->frame.linesize[0]];
            memcpy(row, buf, FFMIN(avctx->width, buf_end - buf));
            buf += avctx->width + (avctx->width % 2); // padding if odd
        }
    } else { // IFF-PBM: HAM to PIX_FMT_BGR32
        for (y = 0; y < avctx->height; y++) {
            uint8_t *row = &s->frame.data[0][ y*s->frame.linesize[0] ];
            memcpy(s->ham_buf, buf, FFMIN(avctx->width, buf_end - buf));
            buf += avctx->width + (avctx->width & 1); // padding if odd
            decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, avctx->width);
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;
    return buf_size;
}

static int decode_frame_byterun1(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    IffContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->size >= 2 ? avpkt->data + AV_RB16(avpkt->data) : NULL;
    const int buf_size = avpkt->size >= 2 ? avpkt->size - AV_RB16(avpkt->data) : 0;
    const uint8_t *buf_end = buf+buf_size;
    int y, plane, res;

    if ((res = extract_header(avctx, avpkt)) < 0)
        return res;
    if (s->init) {
        if ((res = avctx->reget_buffer(avctx, &s->frame)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return res;
        }
    } else if ((res = avctx->get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return res;
    } else if (avctx->bits_per_coded_sample <= 8 && avctx->pix_fmt != PIX_FMT_GRAY8) {
        if ((res = ff_cmap_read_palette(avctx, (uint32_t*)s->frame.data[1])) < 0)
            return res;
    }
    s->init = 1;

    if (avctx->codec_tag == MKTAG('I','L','B','M')) { //interleaved
        if (avctx->pix_fmt == PIX_FMT_PAL8 || avctx->pix_fmt == PIX_FMT_GRAY8) {
            for(y = 0; y < avctx->height ; y++ ) {
                uint8_t *row = &s->frame.data[0][ y*s->frame.linesize[0] ];
                memset(row, 0, avctx->width);
                for (plane = 0; plane < s->bpp; plane++) {
                    buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                    decodeplane8(row, s->planebuf, s->planesize, plane);
                }
            }
        } else if (s->ham) { // HAM to PIX_FMT_BGR32
            for (y = 0; y < avctx->height ; y++) {
                uint8_t *row = &s->frame.data[0][y*s->frame.linesize[0]];
                memset(s->ham_buf, 0, avctx->width);
                for (plane = 0; plane < s->bpp; plane++) {
                    buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                    decodeplane8(s->ham_buf, s->planebuf, s->planesize, plane);
                }
                decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, s->planesize);
            }
        } else { //PIX_FMT_BGR32
            for(y = 0; y < avctx->height ; y++ ) {
                uint8_t *row = &s->frame.data[0][y*s->frame.linesize[0]];
                memset(row, 0, avctx->width << 2);
                for (plane = 0; plane < s->bpp; plane++) {
                    buf += decode_byterun(s->planebuf, s->planesize, buf, buf_end);
                    decodeplane32((uint32_t *) row, s->planebuf, s->planesize, plane);
                }
            }
        }
    } else if (avctx->pix_fmt == PIX_FMT_PAL8 || avctx->pix_fmt == PIX_FMT_GRAY8) { // IFF-PBM
        for(y = 0; y < avctx->height ; y++ ) {
            uint8_t *row = &s->frame.data[0][y*s->frame.linesize[0]];
            buf += decode_byterun(row, avctx->width, buf, buf_end);
        }
    } else { // IFF-PBM: HAM to PIX_FMT_BGR32
        for (y = 0; y < avctx->height ; y++) {
            uint8_t *row = &s->frame.data[0][y*s->frame.linesize[0]];
            buf += decode_byterun(s->ham_buf, avctx->width, buf, buf_end);
            decode_ham_plane32((uint32_t *) row, s->ham_buf, s->ham_palbuf, avctx->width);
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;
    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    IffContext *s = avctx->priv_data;
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);
    av_freep(&s->planebuf);
    av_freep(&s->ham_buf);
    av_freep(&s->ham_palbuf);
    return 0;
}

AVCodec ff_iff_ilbm_decoder = {
    "iff_ilbm",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_IFF_ILBM,
    sizeof(IffContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame_ilbm,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("IFF ILBM"),
};

AVCodec ff_iff_byterun1_decoder = {
    "iff_byterun1",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_IFF_BYTERUN1,
    sizeof(IffContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame_byterun1,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("IFF ByteRun1"),
};
